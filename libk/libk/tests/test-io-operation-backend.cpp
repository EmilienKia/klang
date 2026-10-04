/*
 * Deterministic native proactor regressions: no kernel availability dependency.
 * Copyright 2026 Emilien Kia. Licensed under the Apache License, Version 2.0.
 */
#include <catch2/catch_all.hpp>
#include "../src/runtime/io_backend.h"
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

