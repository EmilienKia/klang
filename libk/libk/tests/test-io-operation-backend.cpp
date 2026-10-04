/*
 * Deterministic native proactor regressions: no kernel availability dependency.
 * Copyright 2026 Emilien Kia. Licensed under the Apache License, Version 2.0.
 */
#include <catch2/catch_all.hpp>
#include "../src/runtime/io_backend.h"
#include "../src/runtime/callable_abi.h"
#include "../src/runtime/io_file_ffi.h"
#include <dlfcn.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <vector>
#include <unistd.h>
#include <fcntl.h>
#include <thread>
#include <algorithm>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/wait.h>
#include <arpa/inet.h>
#include <filesystem>
#include <fstream>
#include <cstring>

namespace {
template<class T> T symbol(void* lib, const char* name) {
    auto ptr = dlsym(lib, name);
    REQUIRE(ptr != nullptr);
    return reinterpret_cast<T>(ptr);
}
struct MockBackend {
    KIoBackend base;
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<KIoBackendCqe> completions;
    std::vector<KIoBackendRequest> requests;
    std::vector<uint64_t> cancellations;
    std::vector<uint64_t> processed;
    uint64_t delivered = 0;
    bool reject = false;
    std::atomic<int> unsupported_opcode{-1};
    bool registration_enabled = false, reject_unregister = false;
    bool registrations[256]{};
    static MockBackend& self(KIoBackend* b) { return *reinterpret_cast<MockBackend*>(b); }
    static void destroy(KIoBackend* b) { delete &self(b); }
    static bool probe(KIoBackend* backend, KIoOpcode opcode) {
        return self(backend).unsupported_opcode.load() != opcode;
    }
    static int submit(KIoBackend* b, const KIoBackendRequest* request) {
        auto& m = self(b);
        std::lock_guard lock(m.mutex);
        m.requests.push_back(*request);
        m.changed.notify_all();
        return m.reject ? -EBUSY : 0;
    }
    static int cancel(KIoBackend* b, uint64_t tag) {
        auto& m = self(b);
        std::lock_guard lock(m.mutex);
        m.cancellations.push_back(tag);
        return 0; // cancel CQE alone deliberately does not complete its target
    }
    static int flush(KIoBackend*) { return 0; }
    static int wait(KIoBackend* b, KIoBackendCqe* out) {
        auto& m = self(b);
        std::unique_lock lock(m.mutex);
        if (m.delivered) {
            m.processed.push_back(m.delivered);
            m.changed.notify_all();
        }
        m.changed.wait(lock, [&] { return !m.completions.empty(); });
        *out = m.completions.front();
        m.completions.pop_front();
        m.delivered = out->tag;
        return 0;
    }
    static unsigned peek(KIoBackend*, KIoBackendCqe*, unsigned) { return 0; }
    static void seen(KIoBackend*, const KIoBackendCqe*) {}
    static int wake(KIoBackend* b) {
        self(b).complete(K_TAG_STOP, 0);
        return 0;
    }
    static int register_buffer(KIoBackend* b, void*, size_t) {
        auto& m = self(b);
        std::lock_guard lock(m.mutex);
        if (!m.registration_enabled) return -ENOTSUP;
        for (unsigned i = 0; i < 256; ++i) {
            if (!m.registrations[i]) {
                m.registrations[i] = true;
                return i;
            }
        }
        return -EBUSY;
    }
    static int unregister_buffer(KIoBackend* b, int index) {
        auto& m = self(b);
        std::lock_guard lock(m.mutex);
        if (m.reject_unregister) return -EIO;
        if (index < 0 || index >= 256 || !m.registrations[index]) return -EINVAL;
        m.registrations[index] = false;
        return 0;
    }
    void complete(uint64_t tag, int result) {
        std::lock_guard lock(mutex);
        completions.push_back({tag, result, 0});
        changed.notify_all();
    }
    KIoBackendRequest last() {
        std::lock_guard lock(mutex);
        REQUIRE_FALSE(requests.empty());
        return requests.back();
    }
    size_t request_count() {
        std::lock_guard lock(mutex);
        return requests.size();
    }
    void wait_processed(uint64_t tag) {
        std::unique_lock lock(mutex);
        REQUIRE(changed.wait_for(lock, std::chrono::seconds(5), [&] {
            return std::find(processed.begin(), processed.end(), tag) != processed.end();
        }));
    }
    void wait_processed_count(uint64_t tag, size_t count) {
        std::unique_lock lock(mutex);
        REQUIRE(changed.wait_for(lock, std::chrono::seconds(5), [&] {
            return static_cast<size_t>(std::count(processed.begin(), processed.end(), tag)) >= count;
        }));
    }
    KIoBackendRequest request_after(size_t count) {
        std::unique_lock lock(mutex);
        REQUIRE(changed.wait_for(lock, std::chrono::seconds(5),
                                [&] { return requests.size() > count; }));
        return requests[count];
    }
};
const KIoBackendOps mock_ops = {
    MockBackend::destroy, MockBackend::probe, MockBackend::submit,
    MockBackend::cancel, MockBackend::flush, MockBackend::wait, MockBackend::peek,
    MockBackend::seen, MockBackend::wake,
    MockBackend::register_buffer, MockBackend::unregister_buffer
};
struct Callback {
    void* context;
    void (*invoke)(void*, void*);
    void (*drop)(void*);
};
struct Result { int kind, error; int64_t bytes, value; };
struct Lease;
struct LeaseOps {
    void (*retain)(Lease*);
    void (*release)(Lease*);
    int (*submit)(Lease*, void (*)(void*), void (*)(void*), void*);
};
struct Lease { const LeaseOps* ops; void* context; uint32_t refs; };
struct Task { void (*run)(void*); void (*drop)(void*); void* context; };
struct ManualExecutor {
    Lease lease;
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<Task> tasks;
    std::atomic<int> refs{1};
    bool reject = false;
    static ManualExecutor& self(Lease* l) { return *static_cast<ManualExecutor*>(l->context); }
    static void retain(Lease* l) { ++self(l).refs; }
    static void release(Lease* l) {
        auto& ex = self(l);
        std::lock_guard lock(ex.mutex);
        --ex.refs;
        ex.changed.notify_all();
    }
    static int submit(Lease* l, void (*run)(void*), void (*drop)(void*), void* ctx) {
        auto& ex = self(l);
        if (ex.reject) {
            drop(ctx);
            return -EBUSY;
        }
        std::lock_guard lock(ex.mutex);
        ex.tasks.push_back({run, drop, ctx});
        ex.changed.notify_all();
        return 0;
    }
    ManualExecutor() {
        static const LeaseOps ops{retain, release, submit};
        lease = {&ops, this, 1};
    }
    Task take() {
        std::unique_lock lock(mutex);
        REQUIRE(changed.wait_for(lock, std::chrono::seconds(5), [&] { return !tasks.empty(); }));
        Task task = tasks.front();
        tasks.pop_front();
        return task;
    }
    void wait_released() {
        std::unique_lock lock(mutex);
        REQUIRE(changed.wait_for(lock, std::chrono::seconds(5), [&] { return refs == 1; }));
    }
};
struct Runtime {
    void* library;
    MockBackend* mock;
    void* (*create)(int, int, void*, uint32_t, int64_t, int, void*, const Callback*, void*, void*);
    void* (*get)();
    int (*submit)(void*, void*, int64_t);
    void (*complete)(void*, int32_t);
    bool (*cancel)(void*);
    void (*release)(void*);
    int (*wait)(void*, int64_t);
    int (*stage)(const void*);
    long long (*bytes)(const void*);
    void* (*current)();
    int (*cooperative)();
    Runtime() {
        library = dlopen((std::string(LIBK_LIB_DIR) + "/libk.so").c_str(), RTLD_NOW | RTLD_GLOBAL);
        REQUIRE(library);
        create = symbol<decltype(create)>(library, "k_io_operation_create");
        get = symbol<decltype(get)>(library, "k_io_runtime_get");
        submit = symbol<decltype(submit)>(library, "k_io_runtime_submit");
        complete = symbol<decltype(complete)>(library, "k_io_operation_on_kernel_completion");
        cancel = symbol<decltype(cancel)>(library, "k_io_operation_cancel");
        release = symbol<decltype(release)>(library, "k_io_operation_release");
        wait = symbol<decltype(wait)>(library, "k_io_operation_wait_quiescent");
        stage = symbol<decltype(stage)>(library, "__k_io_op_stage");
        bytes = symbol<decltype(bytes)>(library, "__k_io_op_bytes_transferred");
        current = symbol<decltype(current)>(library, "__k_io_execution_context_current");
        cooperative = symbol<decltype(cooperative)>(library,
            "__k_io_execution_context_is_cancellation_requested");
        mock = new MockBackend;
        mock->base.ops = &mock_ops;
        auto install = symbol<int (*)(KIoBackend*)>(library, "k_io_runtime_install_test_backend");
        REQUIRE(install(&mock->base) == 0);
    }
};
Runtime& runtime() { static Runtime* rt = new Runtime; return *rt; }
struct Tracker {
    std::atomic<int> invokes{0}, drops{0}, publishes{0};
    bool cancel_inside = false;
    bool saw_cooperative = false;
    static void invoke(void* ctx, void*) {
        auto& t = *static_cast<Tracker*>(ctx);
        ++t.invokes;
        if (t.cancel_inside) {
            runtime().cancel(runtime().current());
            t.saw_cooperative = runtime().cooperative();
        }
    }
    static void drop(void* ctx) { ++static_cast<Tracker*>(ctx)->drops; }
    static void publish(void* ctx, void*) { ++static_cast<Tracker*>(ctx)->publishes; }
    Callback callback() { return {this, invoke, drop}; }
};
}

TEST_CASE("Proactor mock: target CQE and linked timeout both retain storage",
          "[libk][io][proactor][mock][timeout]") {
    auto& rt = runtime();
    auto allocate = symbol<void* (*)(unsigned)>(rt.library, "__k_io_buffer_allocate");
    auto buffer_release = symbol<void (*)(void*)>(rt.library, "__k_io_buffer_release");
    auto leases = symbol<unsigned (*)(const void*)>(rt.library, "__k_io_buffer_get_lease_count");
    auto position = symbol<unsigned (*)(const void*)>(rt.library, "__k_io_buffer_get_position");
    auto mutate = symbol<int (*)(void*, unsigned)>(rt.library, "__k_io_buffer_set_position");
    void* buffer = allocate(16);
    void* op = rt.create(K_IO_OP_READ, -1, nullptr, 8, 0, 0, buffer, nullptr, nullptr, nullptr);
    REQUIRE(rt.submit(rt.get(), op, 1000000) == 0);
    const auto request = rt.mock->last();
    REQUIRE(request.timeout_storage != nullptr);
    REQUIRE(leases(buffer) == 1);
    REQUIRE(mutate(buffer, 1) == -EBUSY);
    rt.mock->complete(request.tag | K_TAG_LINKED_TIMEOUT_FLAG, -ECANCELED);
    REQUIRE(rt.wait(op, 0) == ETIMEDOUT);
    REQUIRE(leases(buffer) == 1);
    rt.mock->complete(request.tag, 5);
    REQUIRE(rt.wait(op, 5000000000LL) == 0);
    REQUIRE(position(buffer) == 5);
    REQUIRE(leases(buffer) == 0);
    rt.release(op);
    buffer_release(buffer);
}

TEST_CASE("Proactor mock: cancel CQE cannot release a target buffer",
          "[libk][io][proactor][mock][cancel]") {
    auto& rt = runtime();
    auto allocate = symbol<void* (*)(unsigned)>(rt.library, "__k_io_buffer_allocate_direct");
    auto buffer_release = symbol<void (*)(void*)>(rt.library, "__k_io_buffer_release");
    void* buffer = allocate(16);
    void* op = rt.create(K_IO_OP_READ, -1, nullptr, 8, 0, 0, buffer, nullptr, nullptr, nullptr);
    REQUIRE(rt.submit(rt.get(), op, -1) == 0);
    const auto tag = rt.mock->last().tag;
    buffer_release(buffer); // kernel lease now owns the storage
    REQUIRE(rt.cancel(op));
    rt.mock->complete(K_TAG_CANCEL_REQUEST_FLAG, 0);
    REQUIRE(rt.wait(op, 0) == ETIMEDOUT);
    rt.mock->complete(tag, 3); // irreversible transfer still counts after cancel
    REQUIRE(rt.wait(op, 5000000000LL) == 0);
    REQUIRE(rt.bytes(op) == 3);
    REQUIRE(rt.stage(op) == 5);
    REQUIRE_FALSE(rt.cancel(op));
    rt.release(op);
}

TEST_CASE("Proactor mock: zero-length buffers require no borrowed storage",
          "[libk][io][proactor][mock][buffer]") {
    auto& rt = runtime();
    auto allocate = symbol<void* (*)(unsigned)>(rt.library, "__k_io_buffer_allocate");
    auto release = symbol<void (*)(void*)>(rt.library, "__k_io_buffer_release");
    auto relative = symbol<int (*)(void*, unsigned char*, unsigned, int)>(
        rt.library, "__k_io_buffer_relative");
    void* buffer = allocate(0);
    REQUIRE(buffer);
    REQUIRE(relative(buffer, nullptr, 0, 1) == 0);
    void* op = rt.create(K_IO_OP_READ, -1, nullptr, 0, 0, 0, buffer, nullptr, nullptr, nullptr);
    REQUIRE(rt.submit(rt.get(), op, -1) == 0);
    rt.mock->complete(rt.mock->last().tag, 0);
    REQUIRE(rt.wait(op, 5000000000LL) == 0);
    REQUIRE(rt.bytes(op) == 0);
    rt.release(op);
    release(buffer);
}

TEST_CASE("Proactor mock: expired linked timeout is classified after both CQEs",
          "[libk][io][proactor][mock][timeout]") {
    auto& rt = runtime();
    auto kind = symbol<int (*)(const void*)>(rt.library, "__k_io_op_error_kind");
    void* op = rt.create(K_IO_OP_READ, -1, nullptr, 0, 0, 0, nullptr, nullptr, nullptr, nullptr);
    REQUIRE(rt.submit(rt.get(), op, 100) == 0);
    const auto tag = rt.mock->last().tag;
    rt.mock->complete(tag, -ECANCELED);
    REQUIRE(rt.wait(op, 0) == ETIMEDOUT);
    rt.mock->complete(tag | K_TAG_LINKED_TIMEOUT_FLAG, -ETIME);
    REQUIRE(rt.wait(op, 5000000000LL) == 0);
    REQUIRE(kind(op) == 3);
    rt.release(op);
}

TEST_CASE("Proactor mock: cancellation cannot close a descriptor being published",
          "[libk][io][proactor][mock][publication][race]") {
    auto& rt = runtime();
    ManualExecutor ex;
    Tracker tracker;
    auto cb = tracker.callback();
    struct Publisher {
        std::mutex mutex;
        std::condition_variable changed;
        bool entered = false, proceed = false;
        void (*claim)(void*);
        static void invoke(void* ctx, void* result) {
            auto& p = *static_cast<Publisher*>(ctx);
            std::unique_lock lock(p.mutex);
            p.entered = true;
            p.changed.notify_all();
            p.changed.wait(lock, [&] { return p.proceed; });
            p.claim(result);
        }
    } publisher;
    publisher.claim = symbol<void (*)(void*)>(rt.library, "__k_io_result_claim");
    void* op = rt.create(K_IO_OP_OPENAT, -1, nullptr, 0, 0, 0, nullptr, &cb, &ex.lease, nullptr);
    auto set = symbol<int (*)(void*, const Callback*)>(rt.library, "k_io_operation_set_continuation");
    Callback continuation{&publisher, Publisher::invoke, nullptr};
    REQUIRE(set(op, &continuation) == 0);
    int fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
    REQUIRE(fd >= 0);
    std::thread complete([&] { rt.complete(op, fd); });
    {
        std::unique_lock lock(publisher.mutex);
        publisher.changed.wait(lock, [&] { return publisher.entered; });
    }
    bool cancelled = rt.cancel(op);
    bool still_open = fcntl(fd, F_GETFD) >= 0;
    {
        std::lock_guard lock(publisher.mutex);
        publisher.proceed = true;
        publisher.changed.notify_all();
    }
    complete.join();
    Task task = ex.take();
    task.run(task.context);
    task.drop(task.context);
    rt.release(op);
    ex.wait_released();
    REQUIRE(cancelled);
    REQUIRE(still_open);
    REQUIRE(fcntl(fd, F_GETFD) >= 0);
    REQUIRE(tracker.invokes == 0);
    REQUIRE(tracker.drops == 1);
    REQUIRE(close(fd) == 0);
}

TEST_CASE("Proactor mock: internal publication precedes an unavailable executor",
          "[libk][io][proactor][mock][publication]") {
    auto& rt = runtime();
    ManualExecutor ex;
    Tracker tracker;
    const auto cb = tracker.callback();
    void* op = rt.create(K_IO_OP_READ, -1, nullptr, 0, 0, 0, nullptr, &cb, &ex.lease, nullptr);
    auto set = symbol<int (*)(void*, const Callback*)>(rt.library, "k_io_operation_set_continuation");
    Callback publisher{&tracker, Tracker::publish, nullptr};
    REQUIRE(set(op, &publisher) == 0);
    rt.complete(op, 7);
    REQUIRE(tracker.publishes == 1);
    REQUIRE(tracker.invokes == 0);
    Task task = ex.take();
    REQUIRE(rt.cancel(op));
    task.run(task.context);
    task.drop(task.context);
    REQUIRE(tracker.invokes == 0);
    REQUIRE(tracker.drops == 1);
    REQUIRE(rt.bytes(op) == 7);
    rt.release(op);
    ex.wait_released();
    REQUIRE(ex.refs == 1);
}

TEST_CASE("Proactor mock: callback running uses operation TLS, not Thread interrupt",
          "[libk][io][proactor][mock][cooperative]") {
    auto& rt = runtime();
    ManualExecutor ex;
    Tracker tracker;
    tracker.cancel_inside = true;
    const auto cb = tracker.callback();
    void* op = rt.create(K_IO_OP_READ, -1, nullptr, 0, 0, 0, nullptr, &cb, &ex.lease, nullptr);
    rt.complete(op, 2);
    Task task = ex.take();
    task.run(task.context);
    task.drop(task.context);
    REQUIRE(tracker.saw_cooperative);
    REQUIRE(tracker.invokes == 1);
    REQUIRE(tracker.drops == 1);
    REQUIRE(rt.current() == nullptr);
    REQUIRE(rt.stage(op) == 7);
    rt.release(op);
    ex.wait_released();
}

TEST_CASE("Proactor mock: rejection consumes task exactly once",
          "[libk][io][proactor][mock][executor]") {
    auto& rt = runtime();
    ManualExecutor ex;
    Tracker tracker;
    const auto cb = tracker.callback();
    void* op = rt.create(K_IO_OP_OPENAT, -1, nullptr, 0, 0, 0, nullptr, &cb, &ex.lease, nullptr);
    int fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
    REQUIRE(fd >= 0);
    rt.complete(op, fd);
    Task task = ex.take();
    task.drop(task.context); // shutdownNow / queue clear path
    REQUIRE(tracker.invokes == 0);
    REQUIRE(tracker.drops == 1);
    REQUIRE(rt.stage(op) == 8);
    REQUIRE(fcntl(fd, F_GETFD) == -1);
    rt.release(op);
    ex.wait_released();
}

TEST_CASE("Proactor mock: a callback that cannot adopt its descriptor rolls it back",
          "[libk][io][proactor][mock][ownership]") {
    auto& rt = runtime();
    ManualExecutor ex;
    Tracker tracker;
    const auto cb = tracker.callback(); // deliberately does not claim its result
    void* op = rt.create(K_IO_OP_OPENAT, -1, nullptr, 0, 0, 0, nullptr, &cb, &ex.lease, nullptr);
    int fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
    REQUIRE(fd >= 0);
    rt.complete(op, fd);
    Task task = ex.take();
    task.run(task.context);
    task.drop(task.context);
    REQUIRE(tracker.invokes == 1);
    REQUIRE(tracker.drops == 1);
    REQUIRE(fcntl(fd, F_GETFD) == -1);
    rt.release(op);
    ex.wait_released();
}

TEST_CASE("Proactor mock: failed linked submission releases every registry CQE ref",
          "[libk][io][proactor][mock][registry]") {
    auto& rt = runtime();
    void* op = rt.create(K_IO_OP_READ, -1, nullptr, 0, 0, 0, nullptr, nullptr, nullptr, nullptr);
    {
        std::lock_guard lock(rt.mock->mutex);
        rt.mock->reject = true;
    }
    REQUIRE(rt.submit(rt.get(), op, 100) == -EBUSY);
    {
        std::lock_guard lock(rt.mock->mutex);
        rt.mock->reject = false;
    }
    rt.complete(op, -EBUSY);
    REQUIRE(rt.wait(op, 0) == 0);
    rt.release(op);
}

TEST_CASE("Proactor mock: close returns before pending target CQEs and closes through backend",
          "[libk][io][proactor][mock][close]") {
    auto& rt = runtime();
    auto handle_create = symbol<void* (*)(int, int)>(rt.library, "__k_io_handle_create");
    auto handle_release = symbol<void (*)(void*)>(rt.library, "__k_io_handle_release");
    auto submit_read = symbol<void* (*)(void*, void*, int, long long, long long,
                                       void*, const Callback*, void*, void*)>(
        rt.library, "__k_io_submit_read");
    auto close_handle = symbol<void* (*)(void*, const Callback*, void*, void*)>(
        rt.library, "__k_io_handle_close");
    int fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
    REQUIRE(fd >= 0);
    void* handle = handle_create(fd, 0);
    char bytes[8];
    void* read = submit_read(handle, bytes, 8, 0, -1, nullptr, nullptr, nullptr, nullptr);
    REQUIRE(read);
    const auto target = rt.mock->last().tag;
    void* close_op = nullptr;
    SECTION("Immediate native close") {
        close_op = close_handle(handle, nullptr, nullptr, nullptr);
    }
    SECTION("Prepared close permits installing an internal publisher before submission") {
        auto prepare = symbol<void* (*)(int, void*, void*, int, long long, int,
            void*, KCallable, KCallable, void*)>(
                rt.library, "__k_io_prepare_request");
        close_op = prepare(K_IO_OP_CLOSE, handle, nullptr, 0, 0, 0,
                           nullptr, KCallable{}, KCallable{}, nullptr);
        REQUIRE(close_op);
        REQUIRE(rt.submit(rt.get(), close_op, -1) == 0);
    }
    REQUIRE(close_op);
    REQUIRE(rt.wait(close_op, 0) == ETIMEDOUT);
    const auto submitted = rt.mock->request_count();
    rt.mock->complete(target, -ECANCELED);
    REQUIRE(rt.wait(read, 5000000000LL) == 0);
    const auto close_request = rt.mock->request_after(submitted);
    REQUIRE(close_request.opcode == K_IO_OP_CLOSE);
    REQUIRE(close(fd) == 0); // the mock executes the fd effect before its CQE
    rt.mock->complete(close_request.tag, 0);
    REQUIRE(rt.wait(close_op, 5000000000LL) == 0);
    rt.release(read);
    rt.release(close_op);
    handle_release(handle);
}

TEST_CASE("Proactor mock: mapped storage remains valid without sparse registration",
          "[libk][io][proactor][mock][mapped]") {
    auto& rt = runtime();
    auto map = symbol<void* (*)(const uint32_t*, int, int64_t, int64_t, int)>(
        rt.library, "__k_io_buffer_map");
    auto registered = symbol<int (*)(const void*)>(rt.library, "__k_io_buffer_is_registered");
    auto put = symbol<int (*)(void*, unsigned, uint8_t)>(rt.library, "__k_io_buffer_put_byte");
    auto release = symbol<void (*)(void*)>(rt.library, "__k_io_buffer_release");
    void* mapping = map(nullptr, -1, 0, 4096, 1);
    REQUIRE(mapping);
    REQUIRE(registered(mapping) == 0); // mock rejects registration by default
    REQUIRE(put(mapping, 0, 42) == 0);
    release(mapping);
    mapping = map(nullptr, -1, 0, 4096, 0);
    REQUIRE(mapping);
    REQUIRE(put(mapping, 0, 42) == -EACCES); // never segfault on read-only mmap
    release(mapping);
}

TEST_CASE("Proactor mock: sparse unregistration waits for target and preserves failures",
          "[libk][io][proactor][mock][mapped][registration]") {
    auto& rt = runtime();
    auto map = symbol<void* (*)(const uint32_t*, int, int64_t, int64_t, int)>(
        rt.library, "__k_io_buffer_map");
    auto registered = symbol<int (*)(const void*)>(rt.library, "__k_io_buffer_is_registered");
    auto unregister = symbol<int (*)(void*)>(rt.library, "__k_io_buffer_unregister");
    auto release = symbol<void (*)(void*)>(rt.library, "__k_io_buffer_release");
    {
        std::lock_guard lock(rt.mock->mutex);
        rt.mock->registration_enabled = true;
    }
    void* mapping = map(nullptr, -1, 0, 4096, 1);
    REQUIRE(mapping);
    REQUIRE(registered(mapping) == 1);
    void* op = rt.create(K_IO_OP_READ, -1, nullptr, 8, 0, 0, mapping, nullptr, nullptr, nullptr);
    REQUIRE(rt.submit(rt.get(), op, -1) == 0);
    const auto request = rt.mock->last();
    REQUIRE(request.fixed_index >= 0);
    REQUIRE(unregister(mapping) == 1);
    REQUIRE(registered(mapping) == 1);
    {
        std::lock_guard lock(rt.mock->mutex);
        rt.mock->reject_unregister = true;
    }
    rt.mock->complete(request.tag, 4);
    REQUIRE(rt.wait(op, 5000000000LL) == 0);
    REQUIRE(registered(mapping) == 1);
    REQUIRE(unregister(mapping) == 0);
    {
        std::lock_guard lock(rt.mock->mutex);
        rt.mock->reject_unregister = false;
    }
    REQUIRE(unregister(mapping) == 1);
    REQUIRE(registered(mapping) == 0);
    rt.release(op);
    release(mapping);
    {
        std::lock_guard lock(rt.mock->mutex);
        rt.mock->registration_enabled = false;
    }
}

TEST_CASE("Future native: concurrent cancellation cannot bypass an extracted producer hook",
          "[libk][future][proactor][mock][race]") {
    auto& rt = runtime();
    auto create = symbol<void* (*)()>(rt.library, "__k_future_create");
    auto destroy = symbol<void (*)(void*)>(rt.library, "__k_future_destroy");
    auto set = symbol<void (*)(void*, void*, bool (*)(void*), void (*)(void*))>(
        rt.library, "__k_future_set_cancel_hook");
    auto cancel = symbol<bool (*)(void*)>(rt.library, "__k_future_cancel");
    auto complete = symbol<bool (*)(void*, int)>(rt.library, "__k_future_complete");
    auto state = symbol<int (*)(const void*)>(rt.library, "__k_future_state");
    struct Hook {
        std::mutex mutex;
        std::condition_variable changed;
        bool entered = false, proceed = false;
        std::atomic<int> drops{0};
        static bool cancel(void* ctx) {
            auto& h = *static_cast<Hook*>(ctx);
            std::unique_lock lock(h.mutex);
            h.entered = true;
            h.changed.notify_all();
            h.changed.wait(lock, [&] { return h.proceed; });
            return true;
        }
        static void drop(void* ctx) { ++static_cast<Hook*>(ctx)->drops; }
    } hook;
    void* future = create();
    set(future, &hook, Hook::cancel, Hook::drop);
    bool first = true;
    std::thread producer([&] { first = cancel(future); });
    {
        std::unique_lock lock(hook.mutex);
        hook.changed.wait(lock, [&] { return hook.entered; });
    }
    bool second = cancel(future);
    bool publication = complete(future, 1);
    {
        std::lock_guard lock(hook.mutex);
        hook.proceed = true;
        hook.changed.notify_all();
    }
    producer.join();
    REQUIRE_FALSE(second);
    REQUIRE(publication);
    REQUIRE_FALSE(first);
    REQUIRE(state(future) == 1);
    REQUIRE(hook.drops == 1);
    destroy(future);
}

TEST_CASE("Proactor file: implicit FIFO advances actual bytes, not reservations",
          "[libk][io][proactor][mock][file-primitives][position]") {
    auto& rt = runtime();
    auto prepare = symbol<decltype(&__k_io_prepare_request)>(rt.library, "__k_io_prepare_request");
    auto submit = symbol<decltype(&__k_io_submit_prepared)>(rt.library, "__k_io_submit_prepared");
    auto implicit = symbol<decltype(&__k_io_op_set_implicit_position)>(
        rt.library, "__k_io_op_set_implicit_position");
    auto create = symbol<void* (*)(int, int)>(rt.library, "__k_io_handle_create");
    auto release_handle = symbol<void (*)(void*)>(rt.library, "__k_io_handle_release");
    auto position = symbol<decltype(&__k_io_handle_position)>(rt.library, "__k_io_handle_position");
    auto set_position = symbol<decltype(&__k_io_handle_set_position)>(
        rt.library, "__k_io_handle_set_position");
    auto allocate = symbol<void* (*)(unsigned)>(rt.library, "__k_io_buffer_allocate");
    auto release_buffer = symbol<void (*)(void*)>(rt.library, "__k_io_buffer_release");
    int fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
    REQUIRE(fd >= 0);
    void* handle = create(fd, 0);
    void* buffers[3] = {allocate(8), allocate(8), allocate(8)};
    void* operations[3]{};
    for (int i = 0; i < 3; ++i) {
        REQUIRE(buffers[i]);
        operations[i] = prepare(K_IO_OP_READ, handle, buffers[i], 8,
            i == 2 ? 100 : 0, 0, nullptr, KCallable{}, KCallable{}, nullptr);
        REQUIRE(operations[i]);
        if (i != 2) REQUIRE(implicit(operations[i]) == 0);
        release_buffer(buffers[i]); // no K wrapper is retained by the operation
    }
    const auto count = rt.mock->request_count();
    REQUIRE(submit(operations[0], -1) == 0);
    REQUIRE(submit(operations[1], -1) == 0);
    REQUIRE(rt.mock->request_count() == count + 1);
    REQUIRE(set_position(handle, 12) == -EBUSY);
    REQUIRE(submit(operations[2], -1) == 0);
    auto first = rt.mock->request_after(count);
    auto positional = rt.mock->request_after(count + 1);
    REQUIRE(first.offset == 0);
    REQUIRE(positional.offset == 100);
    REQUIRE(rt.cancel(operations[0]));
    static_cast<char*>(first.buf)[0] = 'a'; // kernel-owned storage survived wrapper destruction
    rt.mock->complete(first.tag, 2); // cancellation cannot undo these two bytes
    auto second = rt.mock->request_after(count + 2);
    REQUIRE(second.offset == 2);
    REQUIRE(position(handle) == 2);
    REQUIRE(rt.wait(operations[0], 5000000000LL) == 0);
    REQUIRE(rt.bytes(operations[0]) == 2);
    REQUIRE(rt.stage(operations[0]) == 5);
    rt.mock->complete(positional.tag, 7);
    REQUIRE(rt.wait(operations[2], 5000000000LL) == 0);
    REQUIRE(position(handle) == 2);
    release_handle(handle); // requests, not a borrowed wrapper, own the descriptor
    REQUIRE(fcntl(fd, F_GETFD) >= 0);
    static_cast<char*>(second.buf)[0] = 'b';
    rt.mock->complete(second.tag, 3);
    REQUIRE(rt.wait(operations[1], 5000000000LL) == 0);
    for (auto operation : operations) rt.release(operation);
    REQUIRE(fcntl(fd, F_GETFD) == -1);
}

TEST_CASE("Proactor file: queued cancellation and truncate preserve FIFO cursor order",
          "[libk][io][proactor][mock][file-primitives][cancel][truncate]") {
    auto& rt = runtime();
    auto prepare = symbol<decltype(&__k_io_prepare_request)>(rt.library, "__k_io_prepare_request");
    auto submit = symbol<decltype(&__k_io_submit_prepared)>(rt.library, "__k_io_submit_prepared");
    auto implicit = symbol<decltype(&__k_io_op_set_implicit_position)>(
        rt.library, "__k_io_op_set_implicit_position");
    auto create = symbol<void* (*)(int, int)>(rt.library, "__k_io_handle_create");
    auto release = symbol<void (*)(void*)>(rt.library, "__k_io_handle_release");
    auto position = symbol<decltype(&__k_io_handle_position)>(rt.library, "__k_io_handle_position");
    auto allocate = symbol<void* (*)(unsigned)>(rt.library, "__k_io_buffer_allocate");
    auto release_buffer = symbol<void (*)(void*)>(rt.library, "__k_io_buffer_release");
    int fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
    REQUIRE(fd >= 0);
    void* handle = create(fd, 0);
    std::vector<void*> operations;
    for (int i = 0; i < 2; ++i) {
        void* buffer = allocate(8);
        REQUIRE(buffer);
        void* op = prepare(K_IO_OP_READ, handle, buffer, 8, 0, 0,
                           nullptr, KCallable{}, KCallable{}, nullptr);
        REQUIRE(op);
        REQUIRE(implicit(op) == 0);
        release_buffer(buffer);
        operations.push_back(op);
    }
    void* truncate = prepare(K_IO_OP_FTRUNCATE, handle, nullptr, 0, 1, 0,
                            nullptr, KCallable{}, KCallable{}, nullptr);
    REQUIRE(truncate);
    const auto count = rt.mock->request_count();
    REQUIRE(submit(operations[0], -1) == 0);
    REQUIRE(submit(operations[1], -1) == 0);
    REQUIRE(submit(truncate, -1) == 0);
    REQUIRE(rt.cancel(operations[1]));
    REQUIRE(rt.mock->request_count() == count + 1);
    rt.mock->complete(rt.mock->request_after(count).tag, 4);
    auto request = rt.mock->request_after(count + 1);
    REQUIRE(request.opcode == K_IO_OP_FTRUNCATE); // cancelled waiter never submitted data I/O
    REQUIRE(position(handle) == 4);
    REQUIRE(rt.wait(operations[1], 5000000000LL) == 0);
    REQUIRE(rt.bytes(operations[1]) == 0);
    rt.mock->complete(request.tag, 0);
    REQUIRE(rt.wait(truncate, 5000000000LL) == 0);
    REQUIRE(position(handle) == 1);
    for (auto op : operations) rt.release(op);
    rt.release(truncate);
    release(handle);
}

TEST_CASE("Proactor file: preparation errors publish only after installing continuation",
          "[libk][io][proactor][mock][file-primitives][prepare]") {
    auto& rt = runtime();
    auto prepare = symbol<decltype(&__k_io_prepare_request)>(rt.library, "__k_io_prepare_request");
    auto set_path = symbol<decltype(&__k_io_op_set_path)>(rt.library, "__k_io_op_set_path");
    auto options = symbol<decltype(&__k_io_op_set_file_options)>(rt.library, "__k_io_op_set_file_options");
    auto submit = symbol<decltype(&__k_io_submit_prepared)>(rt.library, "__k_io_submit_prepared");
    auto continuation = symbol<int (*)(void*, const Callback*)>(
        rt.library, "k_io_operation_set_continuation");
    ManualExecutor executor;
    struct Probe {
        std::atomic<int> callbacks{0}, publications{0}, drops{0}, error{0};
        std::thread::id caller = std::this_thread::get_id();
        bool dropped_off_caller = false;
        static void callback(void* ctx, void* result) {
            auto& p = *static_cast<Probe*>(ctx);
            p.error = static_cast<Result*>(result)->error;
            ++p.callbacks;
        }
        static void publish(void* ctx, void* result) {
            auto& p = *static_cast<Probe*>(ctx);
            p.error = static_cast<Result*>(result)->error;
            ++p.publications;
        }
        static void drop(void* ctx) {
            auto& p = *static_cast<Probe*>(ctx);
            p.dropped_off_caller = std::this_thread::get_id() != p.caller;
            ++p.drops;
        }
    } probe;
    int expected_error = EINVAL;
    void* op = prepare(K_IO_OP_OPENAT, nullptr, nullptr, 0, 438, 0,
        &probe, KCallable{reinterpret_cast<void*>(Probe::callback), nullptr},
        KCallable{reinterpret_cast<void*>(Probe::drop), nullptr}, &executor.lease);
    REQUIRE(op);
    SECTION("Invalid Unicode") {
        uint32_t path[] = {0xD800, 0};
        expected_error = EILSEQ;
        REQUIRE(set_path(op, path) == -EILSEQ);
    }
    SECTION("Null path") { REQUIRE(set_path(op, nullptr) == -EINVAL); }
    SECTION("Exclusive requires create") { REQUIRE(options(op, 1 | 32) == -EINVAL); }
    SECTION("Truncate requires write") { REQUIRE(options(op, 1 | 16) == -EINVAL); }
    SECTION("Rejected executor still drops the callback on a dispatch worker") {
        REQUIRE(options(op, 1 | 32) == -EINVAL);
        executor.reject = true;
    }
    REQUIRE(probe.publications == 0);
    REQUIRE(probe.callbacks == 0);
    const auto count = rt.mock->request_count();
    Callback publisher{&probe, Probe::publish, nullptr};
    REQUIRE(continuation(op, &publisher) == 0);
    REQUIRE(submit(op, -1) == -expected_error);
    REQUIRE(probe.publications == 1);
    REQUIRE(probe.error == expected_error);
    REQUIRE(rt.mock->request_count() == count);
    if (executor.reject) {
        executor.wait_released();
        REQUIRE(probe.callbacks == 0);
        REQUIRE(probe.drops == 1);
        REQUIRE(probe.dropped_off_caller);
        REQUIRE(rt.stage(op) == 8);
    } else {
        Task task = executor.take();
        task.run(task.context);
        task.drop(task.context);
        REQUIRE(probe.callbacks == 1);
        REQUIRE(probe.drops == 1);
        executor.wait_released();
    }
    rt.release(op);
}

TEST_CASE("Proactor file: bounded FIFO rejects excess and drains without recursive failures",
          "[libk][io][proactor][mock][file-primitives][resource]") {
    auto& rt = runtime();
    auto prepare = symbol<decltype(&__k_io_prepare_request)>(rt.library, "__k_io_prepare_request");
    auto submit = symbol<decltype(&__k_io_submit_prepared)>(rt.library, "__k_io_submit_prepared");
    auto implicit = symbol<decltype(&__k_io_op_set_implicit_position)>(
        rt.library, "__k_io_op_set_implicit_position");
    auto create = symbol<void* (*)(int, int)>(rt.library, "__k_io_handle_create");
    auto release = symbol<void (*)(void*)>(rt.library, "__k_io_handle_release");
    auto set_position = symbol<decltype(&__k_io_handle_set_position)>(
        rt.library, "__k_io_handle_set_position");
    int fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
    REQUIRE(fd >= 0);
    void* handle = create(fd, 0);
    std::vector<void*> operations;
    const auto count = rt.mock->request_count();
    for (int i = 0; i < 129; ++i) {
        void* op = prepare(K_IO_OP_READ, handle, nullptr, 0, 0, 0,
                          nullptr, KCallable{}, KCallable{}, nullptr);
        REQUIRE(op);
        REQUIRE(implicit(op) == 0);
        REQUIRE(submit(op, -1) == (i == 128 ? -EBUSY : 0));
        operations.push_back(op);
    }
    REQUIRE(rt.mock->request_count() == count + 1);
    {
        std::lock_guard lock(rt.mock->mutex);
        rt.mock->reject = true;
    }
    rt.mock->complete(rt.mock->request_after(count).tag, 0);
    REQUIRE(rt.wait(operations[127], 5000000000LL) == 0);
    REQUIRE(set_position(handle, 9) == 0);
    {
        std::lock_guard lock(rt.mock->mutex);
        rt.mock->reject = false;
    }
    for (auto op : operations) {
        REQUIRE(rt.wait(op, 5000000000LL) == 0);
        rt.release(op);
    }
    release(handle);
}

TEST_CASE("Proactor file: open copies paths and uncommitted values leave rollback ownership intact",
          "[libk][io][proactor][mock][file-primitives][open][ownership]") {
    auto& rt = runtime();
    auto prepare = symbol<decltype(&__k_io_prepare_request)>(rt.library, "__k_io_prepare_request");
    auto set_path = symbol<decltype(&__k_io_op_set_path)>(rt.library, "__k_io_op_set_path");
    auto options = symbol<decltype(&__k_io_op_set_file_options)>(rt.library, "__k_io_op_set_file_options");
    auto submit = symbol<decltype(&__k_io_submit_prepared)>(rt.library, "__k_io_submit_prepared");
    auto create_value = symbol<decltype(&__k_io_file_handle_prepare)>(rt.library, "__k_io_file_handle_prepare");
    auto release_handle = symbol<void (*)(void*)>(rt.library, "__k_io_handle_release");
    ManualExecutor executor;
    Tracker tracker;
    void* op = prepare(K_IO_OP_OPENAT, nullptr, nullptr, 0, 438, 0,
        &tracker, KCallable{reinterpret_cast<void*>(Tracker::invoke), nullptr},
        KCallable{reinterpret_cast<void*>(Tracker::drop), nullptr}, &executor.lease);
    REQUIRE(op);
    uint32_t path[] = {'f', 0x00E9, 0};
    REQUIRE(set_path(op, path) == 0);
    REQUIRE(options(op, 1 | 2 | 8 | 32) == 0);
    path[0] = 'x'; // storage does not borrow the caller's UTF-32 path
    REQUIRE(submit(op, -1) == 0);
    auto request = rt.mock->last();
    REQUIRE(std::string(static_cast<char*>(request.buf)) == "f\xc3\xa9");
    REQUIRE((request.flags & O_ACCMODE) == O_RDWR);
    REQUIRE((request.flags & (O_CREAT | O_EXCL | O_CLOEXEC)) == (O_CREAT | O_EXCL | O_CLOEXEC));
    REQUIRE(request.offset == 438);
    int fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
    REQUIRE(fd >= 0);
    void* value = create_value(fd); // model an internal decode whose publication fails
    REQUIRE(value);
    release_handle(value);
    REQUIRE(fcntl(fd, F_GETFD) >= 0);
    REQUIRE(rt.cancel(op));
    rt.mock->complete(request.tag, fd);
    REQUIRE(rt.wait(op, 5000000000LL) == 0);
    REQUIRE(fcntl(fd, F_GETFD) == -1); // operation alone rolled it back, exactly once
    executor.wait_released();
    REQUIRE(tracker.invokes == 0);
    REQUIRE(tracker.drops == 1);
    rt.release(op);
}

TEST_CASE("Proactor file: close drains queued implicit leases and concurrent close stays observable",
          "[libk][io][proactor][mock][file-primitives][close]") {
    auto& rt = runtime();
    auto prepare = symbol<decltype(&__k_io_prepare_request)>(rt.library, "__k_io_prepare_request");
    auto submit = symbol<decltype(&__k_io_submit_prepared)>(rt.library, "__k_io_submit_prepared");
    auto implicit = symbol<decltype(&__k_io_op_set_implicit_position)>(
        rt.library, "__k_io_op_set_implicit_position");
    auto create = symbol<void* (*)(int, int)>(rt.library, "__k_io_handle_create");
    auto release = symbol<void (*)(void*)>(rt.library, "__k_io_handle_release");
    auto position = symbol<decltype(&__k_io_handle_position)>(rt.library, "__k_io_handle_position");
    auto error = symbol<int (*)(const void*)>(rt.library, "__k_io_op_error_code");
    auto allocate = symbol<void* (*)(unsigned)>(rt.library, "__k_io_buffer_allocate");
    auto release_buffer = symbol<void (*)(void*)>(rt.library, "__k_io_buffer_release");
    int fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
    REQUIRE(fd >= 0);
    void* handle = create(fd, 0);
    REQUIRE(handle);
    std::vector<void*> operations;
    const auto count = rt.mock->request_count();
    for (int i = 0; i < 2; ++i) {
        void* buffer = allocate(4);
        REQUIRE(buffer);
        void* op = prepare(K_IO_OP_READ, handle, buffer, 4, 0, 0,
                          nullptr, KCallable{}, KCallable{}, nullptr);
        REQUIRE(op);
        REQUIRE(implicit(op) == 0);
        release_buffer(buffer);
        REQUIRE(submit(op, -1) == 0);
        operations.push_back(op);
    }
    void* close_op = prepare(K_IO_OP_CLOSE, handle, nullptr, 0, 0, 0,
                            nullptr, KCallable{}, KCallable{}, nullptr);
    REQUIRE(close_op);
    REQUIRE(submit(close_op, -1) == 0);
    REQUIRE(rt.wait(close_op, 0) == ETIMEDOUT);
    void* second_close = prepare(K_IO_OP_CLOSE, handle, nullptr, 0, 0, 0,
                                nullptr, KCallable{}, KCallable{}, nullptr);
    REQUIRE(second_close);
    REQUIRE(submit(second_close, -1) == 0);
    REQUIRE(rt.wait(second_close, 0) == 0);
    REQUIRE(error(second_close) == EBUSY);
    REQUIRE(rt.mock->request_count() == count + 1);
    rt.mock->complete(rt.mock->request_after(count).tag, 2);
    auto close_request = rt.mock->request_after(count + 1);
    REQUIRE(close_request.opcode == K_IO_OP_CLOSE);
    REQUIRE(rt.wait(operations[1], 5000000000LL) == 0);
    REQUIRE(position(handle) == 2);
    REQUIRE(rt.stage(operations[0]) == 5);
    REQUIRE(rt.stage(operations[1]) == 5);
    REQUIRE(close(fd) == 0);
    rt.mock->complete(close_request.tag, 0);
    REQUIRE(rt.wait(close_op, 5000000000LL) == 0);
    for (auto operation : operations) rt.release(operation);
    rt.release(close_op);
    rt.release(second_close);
    release(handle);
}

TEST_CASE("Proactor file: a queued deadline expires without issuing a late data request",
          "[libk][io][proactor][mock][file-primitives][timeout]") {
    auto& rt = runtime();
    auto prepare = symbol<decltype(&__k_io_prepare_request)>(rt.library, "__k_io_prepare_request");
    auto submit = symbol<decltype(&__k_io_submit_prepared)>(rt.library, "__k_io_submit_prepared");
    auto implicit = symbol<decltype(&__k_io_op_set_implicit_position)>(
        rt.library, "__k_io_op_set_implicit_position");
    auto create = symbol<void* (*)(int, int)>(rt.library, "__k_io_handle_create");
    auto release = symbol<void (*)(void*)>(rt.library, "__k_io_handle_release");
    auto error = symbol<int (*)(const void*)>(rt.library, "__k_io_op_error_kind");
    int fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
    REQUIRE(fd >= 0);
    void* handle = create(fd, 0);
    REQUIRE(handle);
    void* operations[2]{};
    const auto count = rt.mock->request_count();
    for (int i = 0; i < 2; ++i) {
        operations[i] = prepare(K_IO_OP_READ, handle, nullptr, 0, 0, 0,
            nullptr, KCallable{}, KCallable{}, nullptr);
        REQUIRE(operations[i]);
        REQUIRE(implicit(operations[i]) == 0);
        REQUIRE(submit(operations[i], i == 0 ? -1 : 1000000) == 0);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    rt.mock->complete(rt.mock->request_after(count).tag, 0);
    REQUIRE(rt.wait(operations[1], 5000000000LL) == 0);
    REQUIRE(error(operations[1]) == 3);
    REQUIRE(rt.mock->request_count() == count + 1);
    for (auto op : operations) rt.release(op);
    release(handle);
}

TEST_CASE("Proactor file: prepared buffer failures are terminal operations with balanced leases",
          "[libk][io][proactor][mock][file-primitives][buffer]") {
    auto& rt = runtime();
    auto prepare = symbol<decltype(&__k_io_prepare_request)>(rt.library, "__k_io_prepare_request");
    auto submit = symbol<decltype(&__k_io_submit_prepared)>(rt.library, "__k_io_submit_prepared");
    auto create = symbol<void* (*)(int, int)>(rt.library, "__k_io_handle_create");
    auto release = symbol<void (*)(void*)>(rt.library, "__k_io_handle_release");
    auto allocate = symbol<void* (*)(unsigned)>(rt.library, "__k_io_buffer_allocate");
    auto map = symbol<void* (*)(const uint32_t*, int, int64_t, int64_t, int)>(
        rt.library, "__k_io_buffer_map");
    auto release_buffer = symbol<void (*)(void*)>(rt.library, "__k_io_buffer_release");
    auto leases = symbol<unsigned (*)(const void*)>(rt.library, "__k_io_buffer_get_lease_count");
    int fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
    REQUIRE(fd >= 0);
    void* handle = create(fd, 0);
    REQUIRE(handle);
    void* buffer = nullptr;
    void* held = nullptr;
    int expected_error = EINVAL;
    SECTION("Mapped read-only destination") {
        buffer = map(nullptr, -1, 0, 4096, 0);
        expected_error = EACCES;
    }
    SECTION("Overlapping prepared leases") {
        buffer = allocate(8);
        REQUIRE(buffer);
        held = prepare(K_IO_OP_READ, handle, buffer, 8, 0, 0,
                       nullptr, KCallable{}, KCallable{}, nullptr);
        REQUIRE(held);
        expected_error = EBUSY;
    }
    SECTION("Bounds beyond remaining") { buffer = allocate(4); }
    REQUIRE(buffer);
    const auto count = rt.mock->request_count();
    void* op = prepare(K_IO_OP_READ, handle, buffer, 8, 0, 0,
                      nullptr, KCallable{}, KCallable{}, nullptr);
    REQUIRE(op);
    REQUIRE(submit(op, -1) == -expected_error);
    REQUIRE(rt.wait(op, 0) == 0);
    REQUIRE(rt.mock->request_count() == count);
    REQUIRE(leases(buffer) == (held ? 1 : 0));
    if (held) {
        REQUIRE(submit(held, -1) == 0);
        rt.mock->complete(rt.mock->request_after(count).tag, 3);
        REQUIRE(rt.wait(held, 5000000000LL) == 0);
        REQUIRE(leases(buffer) == 0);
        rt.release(held);
    }
    rt.release(op);
    release_buffer(buffer);
    release(handle);
}

TEST_CASE("Proactor file: repeated prepared submission cannot manufacture kernel quiescence",
          "[libk][io][proactor][mock][file-primitives][prepare][lifetime]") {
    auto& rt = runtime();
    auto prepare = symbol<decltype(&__k_io_prepare_request)>(rt.library, "__k_io_prepare_request");
    auto submit = symbol<decltype(&__k_io_submit_prepared)>(rt.library, "__k_io_submit_prepared");
    auto create = symbol<void* (*)(int, int)>(rt.library, "__k_io_handle_create");
    auto release = symbol<void (*)(void*)>(rt.library, "__k_io_handle_release");
    auto allocate = symbol<void* (*)(unsigned)>(rt.library, "__k_io_buffer_allocate");
    auto release_buffer = symbol<void (*)(void*)>(rt.library, "__k_io_buffer_release");
    auto leases = symbol<unsigned (*)(const void*)>(rt.library, "__k_io_buffer_get_lease_count");
    int fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
    REQUIRE(fd >= 0);
    void* handle = create(fd, 0);
    REQUIRE(handle);
    void* buffer = allocate(8);
    REQUIRE(buffer);
    void* op = prepare(K_IO_OP_READ, handle, buffer, 8, 0, 0,
                      nullptr, KCallable{}, KCallable{}, nullptr);
    REQUIRE(op);
    const auto count = rt.mock->request_count();
    REQUIRE(submit(op, -1) == 0);
    auto target = rt.mock->request_after(count);
    REQUIRE(submit(op, -1) == -EALREADY);
    REQUIRE(rt.wait(op, 0) == ETIMEDOUT);
    REQUIRE(leases(buffer) == 1);
    REQUIRE(rt.mock->request_count() == count + 1);
    rt.mock->complete(target.tag, 3);
    REQUIRE(rt.wait(op, 5000000000LL) == 0);
    REQUIRE(rt.bytes(op) == 3);
    REQUIRE(leases(buffer) == 0);
    rt.release(op);
    release_buffer(buffer);
    release(handle);
}

