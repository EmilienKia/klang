/*
 * Deterministic native proactor regressions: no kernel availability dependency.
 * Copyright 2026 Emilien Kia. Licensed under the Apache License, Version 2.0.
 */
#include <catch2/catch_all.hpp>
#include "../src/runtime/io_backend.h"
#include "../src/runtime/callable_abi.h"
#include "../src/runtime/io_file_ffi.h"
#include "../src/runtime/io_stream_ffi.h"
#include "../../klang/tests/helpers.hpp"
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
struct FullyFile {
    Runtime& rt = runtime();
    decltype(&__k_io_prepare_request) prepare =
        symbol<decltype(prepare)>(rt.library, "__k_io_prepare_request");
    decltype(&__k_io_submit_prepared) submit =
        symbol<decltype(submit)>(rt.library, "__k_io_submit_prepared");
    decltype(&__k_io_op_set_fully) fully =
        symbol<decltype(fully)>(rt.library, "__k_io_op_set_fully");
    decltype(&__k_io_op_set_implicit_position) implicit =
        symbol<decltype(implicit)>(rt.library, "__k_io_op_set_implicit_position");
    void* (*allocate)(unsigned) = symbol<decltype(allocate)>(rt.library, "__k_io_buffer_allocate");
    void (*release_buffer)(void*) = symbol<decltype(release_buffer)>(rt.library, "__k_io_buffer_release");
    unsigned (*leases)(const void*) = symbol<decltype(leases)>(rt.library, "__k_io_buffer_get_lease_count");
    unsigned (*position)(const void*) = symbol<decltype(position)>(rt.library, "__k_io_buffer_get_position");
    int (*kind)(const void*) = symbol<decltype(kind)>(rt.library, "__k_io_op_error_kind");
    int (*error)(const void*) = symbol<decltype(error)>(rt.library, "__k_io_op_error_code");
    decltype(&__k_io_handle_position) cursor =
        symbol<decltype(cursor)>(rt.library, "__k_io_handle_position");
    void (*release_handle)(void*) = symbol<decltype(release_handle)>(rt.library, "__k_io_handle_release");
    int (*continuation)(void*, const Callback*) =
        symbol<decltype(continuation)>(rt.library, "k_io_operation_set_continuation");
    void* handle;
    FullyFile() {
        int fd = open("/dev/null", O_RDWR | O_CLOEXEC);
        REQUIRE(fd >= 0);
        auto create = symbol<void* (*)(int, int)>(rt.library, "__k_io_handle_create");
        handle = create(fd, 0);
        REQUIRE(handle);
    }
    ~FullyFile() { release_handle(handle); }
    void* operation(void* buffer, int count, int opcode = K_IO_OP_READ,
                    int64_t offset = 0, bool sequential = false,
                    const Callback* callback = nullptr, ManualExecutor* executor = nullptr) {
        void* op = prepare(opcode, handle, buffer, count, offset, 0,
            callback ? callback->context : nullptr,
            KCallable{callback ? reinterpret_cast<void*>(callback->invoke) : nullptr, nullptr},
            KCallable{callback ? reinterpret_cast<void*>(callback->drop) : nullptr, nullptr},
            executor ? &executor->lease : nullptr);
        REQUIRE(op);
        if (sequential) REQUIRE(implicit(op) == 0);
        fully(op); // invalid preparation remains observable at submission
        return op;
    }
};
struct FullyProbe {
    std::atomic<int> callbacks{0}, publications{0}, callback_drops{0}, publisher_drops{0};
    Result result{};
    static void callback(void* ctx, void* result) {
        auto& probe = *static_cast<FullyProbe*>(ctx);
        probe.result = *static_cast<Result*>(result);
        ++probe.callbacks;
    }
    static void publish(void* ctx, void* result) {
        auto& probe = *static_cast<FullyProbe*>(ctx);
        probe.result = *static_cast<Result*>(result);
        ++probe.publications;
    }
    static void drop_callback(void* ctx) { ++static_cast<FullyProbe*>(ctx)->callback_drops; }
    static void drop_publisher(void* ctx) { ++static_cast<FullyProbe*>(ctx)->publisher_drops; }
    Callback completion() { return {this, callback, drop_callback}; }
    Callback publisher() { return {this, publish, drop_publisher}; }
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

TEST_CASE("FileChannel fully native: short children aggregate once without executor progress",
          "[libk][io][proactor][mock][file-fully][short]") {
    FullyFile file;
    auto& rt = file.rt;
    int opcode = K_IO_OP_READ;
    SECTION("Short reads") {}
    SECTION("Short writes") { opcode = K_IO_OP_WRITE; }
    ManualExecutor executor;
    FullyProbe probe;
    auto callback = probe.completion(), publisher = probe.publisher();
    void* buffer = file.allocate(9);
    auto set_position = symbol<int (*)(void*, unsigned)>(rt.library, "__k_io_buffer_set_position");
    REQUIRE(set_position(buffer, 2) == 0);
    void* parent = file.operation(buffer, 7, opcode, 100, false, &callback, &executor);
    REQUIRE(file.continuation(parent, &publisher) == 0);
    const auto count = rt.mock->request_count();
    REQUIRE(file.submit(parent, -1) == 0);
    auto first = rt.mock->request_after(count);
    REQUIRE(first.offset == 100);
    REQUIRE(first.len == 7);
    rt.mock->complete(first.tag, 2);
    auto second = rt.mock->request_after(count + 1);
    REQUIRE(second.offset == 102);
    REQUIRE(second.len == 5);
    REQUIRE(second.buf == static_cast<char*>(first.buf) + 2);
    REQUIRE(rt.bytes(parent) == 2);
    REQUIRE(file.position(buffer) == 4);
    REQUIRE(file.leases(buffer) == 1);
    REQUIRE(set_position(buffer, 0) == -EBUSY);
    REQUIRE(probe.publications == 0);
    rt.mock->complete(second.tag, 1);
    auto third = rt.mock->request_after(count + 2);
    REQUIRE(third.offset == 103);
    REQUIRE(third.len == 4);
    REQUIRE(file.position(buffer) == 5);
    rt.mock->complete(third.tag, 4);
    REQUIRE(rt.wait(parent, 5000000000LL) == 0);
    Task task = executor.take();
    REQUIRE(probe.publications == 1);
    REQUIRE(probe.publisher_drops == 1);
    REQUIRE(probe.callbacks == 0);
    REQUIRE(probe.result.bytes == 7);
    REQUIRE(probe.result.value == 7);
    REQUIRE(file.position(buffer) == 9);
    REQUIRE(file.leases(buffer) == 0);
    REQUIRE(file.cursor(file.handle) == 0);
    REQUIRE(rt.bytes(parent) == 7);
    task.run(task.context);
    task.drop(task.context);
    executor.wait_released();
    REQUIRE(probe.callbacks == 1);
    REQUIRE(probe.callback_drops == 1);
    REQUIRE_FALSE(rt.cancel(parent));
    rt.release(parent);
    file.release_buffer(buffer);
}

TEST_CASE("FileChannel fully native: EOF, no-progress and child errors retain partial bytes",
          "[libk][io][proactor][mock][file-fully][partial]") {
    FullyFile file;
    auto& rt = file.rt;
    int opcode = K_IO_OP_READ, child_result = 0, expected_kind = 4, expected_error = ENODATA;
    SECTION("Short read followed by EOF") {}
    SECTION("Short write followed by zero progress") {
        opcode = K_IO_OP_WRITE; expected_kind = 0; expected_error = EIO;
    }
    SECTION("System error after short transfer") {
        child_result = -EIO; expected_kind = 0; expected_error = EIO;
    }
    SECTION("Interrupted child after short transfer") {
        child_result = -EINTR; expected_kind = 9; expected_error = EINTR;
    }
    SECTION("An impossible oversized child CQE cannot overrun the buffer cursor") {
        child_result = 5; expected_kind = 0; expected_error = EIO;
    }
    ManualExecutor executor;
    FullyProbe probe;
    auto callback = probe.completion(), publisher = probe.publisher();
    void* buffer = file.allocate(6);
    void* parent = file.operation(buffer, 6, opcode, 0, true, &callback, &executor);
    REQUIRE(file.continuation(parent, &publisher) == 0);
    auto count = rt.mock->request_count();
    REQUIRE(file.submit(parent, -1) == 0);
    rt.mock->complete(rt.mock->request_after(count).tag, 2);
    auto second = rt.mock->request_after(count + 1);
    REQUIRE(second.offset == 2);
    rt.mock->complete(second.tag, child_result);
    REQUIRE(rt.wait(parent, 5000000000LL) == 0);
    Task task = executor.take();
    REQUIRE(probe.result.kind == expected_kind);
    REQUIRE(probe.result.error == expected_error);
    REQUIRE(probe.result.bytes == 2);
    REQUIRE(probe.publications == 1);
    REQUIRE(file.kind(parent) == expected_kind);
    REQUIRE(file.error(parent) == expected_error);
    REQUIRE(rt.bytes(parent) == 2);
    REQUIRE(file.position(buffer) == 2);
    REQUIRE(file.cursor(file.handle) == 2);
    REQUIRE(file.leases(buffer) == 0);
    REQUIRE(rt.mock->request_count() == count + 2);
    task.run(task.context); task.drop(task.context);
    executor.wait_released();
    REQUIRE(probe.callbacks == 1);
    REQUIRE(probe.callback_drops == 1);
    REQUIRE(probe.publisher_drops == 1);
    rt.release(parent);
    file.release_buffer(buffer);
}

TEST_CASE("FileChannel fully native: cancellation at every child boundary suppresses delivery",
          "[libk][io][proactor][mock][file-fully][cancel]") {
    FullyFile file;
    auto& rt = file.rt;
    ManualExecutor executor;
    FullyProbe probe;
    auto callback = probe.completion(), publisher = probe.publisher();
    void* buffer = file.allocate(6);
    void* parent = file.operation(buffer, 6, K_IO_OP_READ, 0, true, &callback, &executor);
    REQUIRE(file.continuation(parent, &publisher) == 0);
    auto count = rt.mock->request_count();
    int expected_bytes = 0, expected_requests = 0;
    bool queued = false;
    SECTION("Before the first child is registered") {
        REQUIRE(rt.cancel(parent));
        REQUIRE(file.submit(parent, -1) == 0);
    }
    SECTION("Cancel current child; target and timeout must both drain") {
        REQUIRE(file.submit(parent, 5000000000LL) == 0);
        auto first = rt.mock->request_after(count);
        REQUIRE(rt.cancel(parent));
        rt.mock->complete(K_TAG_CANCEL_REQUEST_FLAG, 0);
        REQUIRE(rt.wait(parent, 0) == ETIMEDOUT);
        rt.mock->complete(first.tag, 2);
        REQUIRE(rt.wait(parent, 0) == ETIMEDOUT);
        REQUIRE(file.leases(buffer) == 1);
        rt.mock->complete(first.tag | K_TAG_LINKED_TIMEOUT_FLAG, -ECANCELED);
        expected_bytes = 2; expected_requests = 1;
    }
    SECTION("After a short target CQE, before the next child") {
        REQUIRE(file.submit(parent, 5000000000LL) == 0);
        auto first = rt.mock->request_after(count);
        rt.mock->complete(first.tag, 2);
        rt.mock->wait_processed(first.tag);
        REQUIRE(rt.cancel(parent));
        REQUIRE(rt.wait(parent, 0) == ETIMEDOUT);
        rt.mock->complete(first.tag | K_TAG_LINKED_TIMEOUT_FLAG, -ECANCELED);
        expected_bytes = 2; expected_requests = 1;
    }
    SECTION("Cancel a replacement child after irreversible first-child bytes") {
        REQUIRE(file.submit(parent, -1) == 0);
        rt.mock->complete(rt.mock->request_after(count).tag, 2);
        auto second = rt.mock->request_after(count + 1);
        REQUIRE(rt.cancel(parent));
        rt.mock->complete(second.tag, 1);
        expected_bytes = 3; expected_requests = 2;
    }
    SECTION("Cancel the queued final callback") {
        REQUIRE(file.submit(parent, -1) == 0);
        rt.mock->complete(rt.mock->request_after(count).tag, 6);
        REQUIRE(rt.wait(parent, 5000000000LL) == 0);
        Task task = executor.take();
        REQUIRE(rt.cancel(parent));
        task.run(task.context); task.drop(task.context);
        expected_bytes = 6; expected_requests = 1; queued = true;
    }
    REQUIRE(rt.wait(parent, 5000000000LL) == 0);
    executor.wait_released();
    REQUIRE(probe.publications == 1);
    REQUIRE(probe.result.bytes == expected_bytes);
    REQUIRE(probe.result.kind == (queued ? 0 : 1));
    REQUIRE(probe.callbacks == 0);
    REQUIRE(probe.callback_drops == 1);
    REQUIRE(probe.publisher_drops == 1);
    REQUIRE(file.kind(parent) == 1);
    REQUIRE(rt.stage(parent) == 5);
    REQUIRE(rt.bytes(parent) == expected_bytes);
    REQUIRE(file.position(buffer) == static_cast<unsigned>(expected_bytes));
    REQUIRE(file.cursor(file.handle) == expected_bytes);
    REQUIRE(file.leases(buffer) == 0);
    REQUIRE(rt.mock->request_count() == count + expected_requests);
    REQUIRE_FALSE(rt.cancel(parent));
    rt.release(parent);
    file.release_buffer(buffer);
}

TEST_CASE("FileChannel fully native: a single absolute deadline is not renewed by short children",
          "[libk][io][proactor][mock][file-fully][timeout]") {
    FullyFile file;
    auto& rt = file.rt;
    void* buffer = file.allocate(6);
    void* parent = file.operation(buffer, 6);
    auto count = rt.mock->request_count();
    REQUIRE(file.submit(parent, 5000000000LL) == 0);
    auto first = rt.mock->request_after(count);
    rt.mock->complete(first.tag, 2);
    REQUIRE(rt.wait(parent, 0) == ETIMEDOUT);
    REQUIRE(file.position(buffer) == 0);
    REQUIRE(file.leases(buffer) == 1);
    REQUIRE(rt.mock->request_count() == count + 1);
    rt.mock->complete(first.tag | K_TAG_LINKED_TIMEOUT_FLAG, -ECANCELED);
    auto second = rt.mock->request_after(count + 1);
    REQUIRE(second.timeout_nanos > 0);
    REQUIRE(second.timeout_nanos < first.timeout_nanos);
    REQUIRE(second.offset == 2);
    REQUIRE(file.position(buffer) == 2);
    rt.mock->complete(second.tag | K_TAG_LINKED_TIMEOUT_FLAG, -ETIME);
    REQUIRE(rt.wait(parent, 0) == ETIMEDOUT);
    REQUIRE(file.leases(buffer) == 1);
    rt.mock->complete(second.tag, -ECANCELED);
    REQUIRE(rt.wait(parent, 5000000000LL) == 0);
    REQUIRE(file.kind(parent) == 3);
    REQUIRE(file.error(parent) == ETIMEDOUT);
    REQUIRE(rt.bytes(parent) == 2);
    REQUIRE(file.position(buffer) == 2);
    REQUIRE(file.leases(buffer) == 0);
    REQUIRE(rt.mock->request_count() == count + 2);
    rt.release(parent);
    file.release_buffer(buffer);
}

TEST_CASE("FileChannel fully native: implicit FIFO reserves the entire composite",
          "[libk][io][proactor][mock][file-fully][position]") {
    FullyFile file;
    auto& rt = file.rt;
    void* buffers[3] = {file.allocate(6), file.allocate(2), file.allocate(3)};
    void* first = file.operation(buffers[0], 6, K_IO_OP_WRITE, 0, true);
    void* middle = file.prepare(K_IO_OP_READ, file.handle, buffers[1], 2, 0, 0,
                               nullptr, KCallable{}, KCallable{}, nullptr);
    REQUIRE(file.implicit(middle) == 0);
    void* last = file.operation(buffers[2], 3, K_IO_OP_WRITE, 0, true);
    auto count = rt.mock->request_count();
    REQUIRE(file.submit(first, -1) == 0);
    REQUIRE(file.submit(middle, -1) == 0);
    REQUIRE(file.submit(last, -1) == 0);
    REQUIRE(rt.mock->request_count() == count + 1);
    rt.mock->complete(rt.mock->request_after(count).tag, 2);
    auto child = rt.mock->request_after(count + 1);
    REQUIRE(child.opcode == K_IO_OP_WRITE);
    REQUIRE(child.len == 4);
    REQUIRE(child.offset == 2);
    REQUIRE(file.cursor(file.handle) == 2);
    REQUIRE(rt.wait(middle, 0) == ETIMEDOUT);
    rt.mock->complete(child.tag, 4);
    auto middle_request = rt.mock->request_after(count + 2);
    REQUIRE(middle_request.opcode == K_IO_OP_READ);
    REQUIRE(middle_request.offset == 6);
    rt.mock->complete(middle_request.tag, 2);
    auto last_request = rt.mock->request_after(count + 3);
    REQUIRE(last_request.opcode == K_IO_OP_WRITE);
    REQUIRE(last_request.offset == 8);
    rt.mock->complete(last_request.tag, 1);
    auto last_child = rt.mock->request_after(count + 4);
    REQUIRE(last_child.offset == 9);
    rt.mock->complete(last_child.tag, 2);
    for (void* op : {first, middle, last}) {
        REQUIRE(rt.wait(op, 5000000000LL) == 0);
        rt.release(op);
    }
    REQUIRE(file.cursor(file.handle) == 11);
    for (void* buffer : buffers) file.release_buffer(buffer);
}

TEST_CASE("FileChannel fully native: empty and invalid preparations publish observable operations",
          "[libk][io][proactor][mock][file-fully][prepare]") {
    FullyFile file;
    auto& rt = file.rt;
    ManualExecutor executor;
    FullyProbe probe;
    auto callback = probe.completion(), publisher = probe.publisher();
    unsigned capacity = 0;
    int length = 0, expected_error = 0, expected_kind = 0;
    int64_t offset = 0;
    bool read_only = false, overlapping = false;
    SECTION("Empty succeeds with zero") {}
    SECTION("Negative offset is invalid even for an empty buffer") {
        offset = -1; expected_error = EINVAL; expected_kind = 8;
    }
    SECTION("Offset plus remaining overflows") {
        capacity = 2; length = 2; offset = INT64_MAX - 1;
        expected_error = EINVAL; expected_kind = 8;
    }
    SECTION("Count beyond the signed API range is not a zero success") {
        length = INT32_MIN; expected_error = EINVAL; expected_kind = 8;
    }
    SECTION("Count beyond remaining") {
        capacity = 1; length = 2; expected_error = EINVAL; expected_kind = 8;
    }
    SECTION("Read-only mapped destination") {
        capacity = 4; length = 4; read_only = true;
        expected_error = EACCES;
    }
    SECTION("Overlapping preparation leases remain failures even after the first observer drops") {
        capacity = 4; length = 4; overlapping = true;
        expected_error = EBUSY; expected_kind = 6;
    }
    SECTION("Executor rejection does not block publication or leak its carrier") { executor.reject = true; }
    auto map = symbol<void* (*)(const uint32_t*, int, int64_t, int64_t, int)>(
        rt.library, "__k_io_buffer_map");
    void* buffer = read_only ? map(nullptr, -1, 0, capacity, 0) : file.allocate(capacity);
    REQUIRE(buffer);
    void* held = overlapping ? file.prepare(K_IO_OP_READ, file.handle, buffer, length, 0, 0,
        nullptr, KCallable{}, KCallable{}, nullptr) : nullptr;
    void* parent = file.operation(buffer, length, K_IO_OP_READ, offset, false, &callback, &executor);
    if (held) rt.release(held);
    REQUIRE(rt.stage(parent) == 0);
    REQUIRE(file.leases(buffer) == 1);
    REQUIRE(probe.publications == 0);
    REQUIRE(file.continuation(parent, &publisher) == 0);
    auto count = rt.mock->request_count();
    REQUIRE(file.submit(parent, -1) == -expected_error);
    REQUIRE(rt.wait(parent, 0) == 0);
    REQUIRE(rt.bytes(parent) == 0);
    REQUIRE(file.leases(buffer) == 0);
    REQUIRE(probe.publications == 1);
    REQUIRE(probe.publisher_drops == 1);
    REQUIRE(probe.result.kind == expected_kind);
    REQUIRE(probe.result.error == expected_error);
    REQUIRE(rt.mock->request_count() == count);
    if (!executor.reject) {
        Task task = executor.take();
        task.run(task.context); task.drop(task.context);
        REQUIRE(probe.callbacks == 1);
    } else REQUIRE(probe.callbacks == 0);
    executor.wait_released();
    REQUIRE(probe.callback_drops == 1);
    REQUIRE(file.kind(parent) == (executor.reject ? 7 : expected_kind));
    rt.release(parent);
    file.release_buffer(buffer);
}

TEST_CASE("FileChannel fully native: Future cancellation waits for the cumulative result",
          "[libk][io][proactor][mock][file-fully][future][cancel]") {
    FullyFile file;
    auto& rt = file.rt;
    auto create = symbol<void* (*)()>(rt.library, "k_future_state_create");
    auto destroy = symbol<void (*)(void*)>(rt.library, "k_future_state_destroy");
    auto state = symbol<int (*)(const void*)>(rt.library, "k_future_state_get");
    auto cancel = symbol<bool (*)(void*)>(rt.library, "k_future_state_cancel");
    auto future_wait = symbol<int (*)(void*, int64_t)>(rt.library, "k_future_state_wait_quiescent");
    auto bind = symbol<void (*)(void*, void*)>(rt.library, "k_io_operation_bind_future");
    struct Publisher {
        void* future;
        bool (*complete)(void*, int);
        Result result{};
        int publications = 0, drops = 0;
        static void publish(void* ctx, void* result) {
            auto& p = *static_cast<Publisher*>(ctx);
            p.result = *static_cast<Result*>(result);
            ++p.publications;
            p.complete(p.future, p.result.bytes ? 2 : 3);
        }
        static void drop(void* ctx) { ++static_cast<Publisher*>(ctx)->drops; }
    } publisher{create(), symbol<bool (*)(void*, int)>(rt.library, "k_future_state_complete")};
    REQUIRE(publisher.future);
    void* buffer = file.allocate(6);
    void* parent = file.operation(buffer, 6);
    Callback continuation{&publisher, Publisher::publish, Publisher::drop};
    REQUIRE(file.continuation(parent, &continuation) == 0);
    bind(parent, publisher.future);
    auto count = rt.mock->request_count();
    REQUIRE(file.submit(parent, 5000000000LL) == 0);
    auto first = rt.mock->request_after(count);
    rt.mock->complete(first.tag, 2);
    rt.mock->complete(first.tag | K_TAG_LINKED_TIMEOUT_FLAG, -ECANCELED);
    auto second = rt.mock->request_after(count + 1);
    REQUIRE(cancel(publisher.future));
    REQUIRE_FALSE(cancel(publisher.future));
    REQUIRE(state(publisher.future) == 0);
    REQUIRE(future_wait(publisher.future, 0) == ETIMEDOUT);
    rt.mock->complete(second.tag, 1);
    REQUIRE(future_wait(publisher.future, 0) == ETIMEDOUT);
    rt.mock->complete(second.tag | K_TAG_LINKED_TIMEOUT_FLAG, -ECANCELED);
    REQUIRE(future_wait(publisher.future, 5000000000LL) == 0);
    rt.mock->wait_processed(second.tag | K_TAG_LINKED_TIMEOUT_FLAG);
    REQUIRE(state(publisher.future) == 2);
    REQUIRE(publisher.result.kind == 1);
    REQUIRE(publisher.result.error == ECANCELED);
    REQUIRE(publisher.result.bytes == 3);
    REQUIRE(publisher.publications == 1);
    REQUIRE(publisher.drops == 1);
    REQUIRE(file.position(buffer) == 3);
    rt.release(parent);
    destroy(publisher.future);
    file.release_buffer(buffer);
}

TEST_CASE("FileChannel fully native: mapped wrapper drop keeps its lease through every timeout CQE",
          "[libk][io][proactor][mock][file-fully][buffer][lifetime]") {
    FullyFile file;
    auto& rt = file.rt;
    {
        std::lock_guard lock(rt.mock->mutex);
        rt.mock->registration_enabled = true;
    }
    auto map = symbol<void* (*)(const uint32_t*, int, int64_t, int64_t, int)>(
        rt.library, "__k_io_buffer_map");
    void* buffer = map(nullptr, -1, 0, 4096, 1);
    REQUIRE(buffer);
    void* parent = file.operation(buffer, 4);
    auto count = rt.mock->request_count();
    REQUIRE(file.submit(parent, 5000000000LL) == 0);
    auto first = rt.mock->request_after(count);
    REQUIRE(first.fixed_index >= 0);
    file.release_buffer(buffer);
    rt.mock->complete(first.tag, 2);
    rt.mock->wait_processed(first.tag);
    REQUIRE(file.leases(buffer) == 1);
    rt.mock->complete(first.tag | K_TAG_LINKED_TIMEOUT_FLAG, -ECANCELED);
    auto second = rt.mock->request_after(count + 1);
    REQUIRE(second.fixed_index == first.fixed_index);
    REQUIRE(file.position(buffer) == 2);
    static_cast<char*>(second.buf)[0] = 'z';
    rt.mock->complete(second.tag, 2);
    rt.mock->wait_processed(second.tag);
    REQUIRE(rt.wait(parent, 0) == ETIMEDOUT);
    REQUIRE(file.leases(buffer) == 1);
    {
        std::lock_guard lock(rt.mock->mutex);
        REQUIRE(rt.mock->registrations[first.fixed_index]);
    }
    rt.mock->complete(second.tag | K_TAG_LINKED_TIMEOUT_FLAG, -ECANCELED);
    REQUIRE(rt.wait(parent, 5000000000LL) == 0);
    {
        std::lock_guard lock(rt.mock->mutex);
        REQUIRE_FALSE(rt.mock->registrations[first.fixed_index]);
        rt.mock->registration_enabled = false;
    }
    REQUIRE(rt.bytes(parent) == 4);
    rt.release(parent);
}

TEST_CASE("FileChannel fully native: close drains parents and cancelled FIFO waiters",
          "[libk][io][proactor][mock][file-fully][close]") {
    FullyFile file;
    auto& rt = file.rt;
    void* buffers[2] = {file.allocate(6), file.allocate(3)};
    void* parents[2] = {
        file.operation(buffers[0], 6, K_IO_OP_WRITE, 0, true),
        file.operation(buffers[1], 3, K_IO_OP_READ, 0, true)};
    auto count = rt.mock->request_count();
    REQUIRE(file.submit(parents[0], -1) == 0);
    REQUIRE(file.submit(parents[1], -1) == 0);
    auto target = rt.mock->request_after(count);
    REQUIRE(rt.cancel(parents[1]));
    void* close_op = file.prepare(K_IO_OP_CLOSE, file.handle, nullptr, 0, 0, 0,
                                 nullptr, KCallable{}, KCallable{}, nullptr);
    REQUIRE(file.submit(close_op, -1) == 0);
    REQUIRE(rt.wait(close_op, 0) == ETIMEDOUT);
    REQUIRE(file.leases(buffers[0]) == 1);
    REQUIRE(file.leases(buffers[1]) == 1);
    rt.mock->complete(target.tag, 2);
    auto close_request = rt.mock->request_after(count + 1);
    REQUIRE(close_request.opcode == K_IO_OP_CLOSE);
    REQUIRE(file.cursor(file.handle) == 2);
    for (int i = 0; i < 2; ++i) {
        REQUIRE(rt.wait(parents[i], 5000000000LL) == 0);
        REQUIRE(file.leases(buffers[i]) == 0);
        REQUIRE(file.kind(parents[i]) == 1);
        rt.release(parents[i]);
        file.release_buffer(buffers[i]);
    }
    REQUIRE(close(close_request.fd) == 0);
    rt.mock->complete(close_request.tag, 0);
    REQUIRE(rt.wait(close_op, 5000000000LL) == 0);
    rt.release(close_op);
}

TEST_CASE("FileChannel fully native: FIFO and backend admission stay bounded",
          "[libk][io][proactor][mock][file-fully][resource]") {
    FullyFile file;
    auto& rt = file.rt;
    SECTION("One composite reserves one of the existing 128 FIFO slots") {
        std::vector<void*> parents;
        auto count = rt.mock->request_count();
        for (int i = 0; i < 129; ++i) {
            void* buffer = file.allocate(i ? 0 : 4);
            void* parent = file.operation(buffer, i ? 0 : 4, K_IO_OP_WRITE, 0, true);
            file.release_buffer(buffer);
            REQUIRE(file.submit(parent, -1) == (i == 128 ? -EBUSY : 0));
            parents.push_back(parent);
        }
        REQUIRE(rt.mock->request_count() == count + 1);
        REQUIRE(file.kind(parents.back()) == 6);
        for (int i = 1; i < 128; ++i) REQUIRE(rt.cancel(parents[i]));
        rt.mock->complete(rt.mock->request_after(count).tag, 4);
        for (void* parent : parents) {
            REQUIRE(rt.wait(parent, 5000000000LL) == 0);
            rt.release(parent);
        }
        REQUIRE(rt.mock->request_count() == count + 1);
    }
    SECTION("Replacement submission failure reports already transferred bytes") {
        void* buffer = file.allocate(4);
        void* parent = file.operation(buffer, 4);
        auto count = rt.mock->request_count();
        REQUIRE(file.submit(parent, -1) == 0);
        auto first = rt.mock->request_after(count);
        {
            std::lock_guard lock(rt.mock->mutex);
            rt.mock->reject = true;
        }
        rt.mock->complete(first.tag, 2);
        REQUIRE(rt.wait(parent, 5000000000LL) == 0);
        {
            std::lock_guard lock(rt.mock->mutex);
            rt.mock->reject = false;
        }
        REQUIRE(file.kind(parent) == 6);
        REQUIRE(rt.bytes(parent) == 2);
        REQUIRE(file.position(buffer) == 2);
        REQUIRE(file.leases(buffer) == 0);
        rt.release(parent);
        file.release_buffer(buffer);
    }
    SECTION("The first child cannot bypass the existing 8192 registry slots") {
        std::vector<void*> occupied;
        auto count = rt.mock->request_count();
        for (int i = 0; i < 8192; ++i) {
            void* op = rt.create(K_IO_OP_READ, -1, nullptr, 0, 0, 0,
                                 nullptr, nullptr, nullptr, nullptr);
            REQUIRE(op);
            REQUIRE(rt.submit(rt.get(), op, -1) == 0);
            occupied.push_back(op);
        }
        void* buffer = file.allocate(4);
        void* parent = file.operation(buffer, 4);
        REQUIRE(file.submit(parent, -1) == 0);
        REQUIRE(rt.wait(parent, 0) == 0);
        REQUIRE(file.kind(parent) == 6);
        REQUIRE(rt.bytes(parent) == 0);
        REQUIRE(file.leases(buffer) == 0);
        REQUIRE(rt.mock->request_count() == count + 8192);
        for (int i = 0; i < 8192; ++i)
            rt.mock->complete(rt.mock->request_after(count + i).tag, 0);
        for (void* op : occupied) {
            REQUIRE(rt.wait(op, 5000000000LL) == 0);
            rt.release(op);
        }
        rt.release(parent);
        file.release_buffer(buffer);
    }
}

TEST_CASE("FileChannel fully native: cancellation inside the final callback is cooperative",
          "[libk][io][proactor][mock][file-fully][callback]") {
    FullyFile file;
    auto& rt = file.rt;
    ManualExecutor executor;
    Tracker tracker;
    tracker.cancel_inside = true;
    auto callback = tracker.callback();
    void* buffer = file.allocate(3);
    void* parent = file.operation(buffer, 3, K_IO_OP_WRITE, 0, false, &callback, &executor);
    auto count = rt.mock->request_count();
    REQUIRE(file.submit(parent, -1) == 0);
    rt.mock->complete(rt.mock->request_after(count).tag, 1);
    auto child = rt.mock->request_after(count + 1);
    rt.mock->complete(child.tag, 2);
    Task task = executor.take();
    task.run(task.context); task.drop(task.context);
    executor.wait_released();
    REQUIRE(tracker.invokes == 1);
    REQUIRE(tracker.drops == 1);
    REQUIRE(tracker.saw_cooperative);
    REQUIRE(rt.stage(parent) == 7);
    REQUIRE(rt.bytes(parent) == 3);
    REQUIRE(file.position(buffer) == 3);
    REQUIRE(file.leases(buffer) == 0);
    REQUIRE_FALSE(rt.cancel(parent));
    rt.release(parent);
    file.release_buffer(buffer);
}

TEST_CASE("FileChannel fully K publisher: Future causes and getIoSync preserve partial counts",
          "[libk][io][proactor][mock][file-fully][k-api][partial]") {
    FullyFile file;
    auto& rt = file.rt;
    auto jit = gen_jit_with_stdlib(R"SRC(
        module __fully_partial_publisher__;
        @ffi::Extern("C") private __k_io_op_set_fully(operation: k::io::NativeIoOperation*) : int;
        operation : k::io::IoOperation!;
        future : Future<int>!;
        decode(value: long) : int { return (int) value; }
        start(handle: k::io::NativeIoHandle*, buffer: k::io::NativeByteBuffer*, opcode: int)
            : k::io::NativeIoOperation* {
            lease : ExecutorLease! = ExecutorLease::system();
            operation = k::io::prepareOperation<int>(opcode, handle, buffer, 4,
                20L, 0, null, *lease, decode);
            k::io::setOperationError(*operation, __k_io_op_set_fully(operation->native()));
            future = k::io::futureFor<int>(*operation, decode);
            k::io::submitOperation(*operation, k::io::IO_NO_TIMEOUT);
            return operation->native();
        }
        requestCancel() : int { return future->cancel() ? 1 : 0; }
        pending() : int { return future->isDone() ? 0 : 1; }
        observe(expected: int) : int throws(Throwable) {
            try { future->get(Duration::ofMillis(0L)); return -1; }
            catch (e: ExecutionException&) {
                cause : Throwable* = e.getCause();
                if (cause == null || cause->getCode() != expected) return -2;
                if (expected == 203) {
                    error : k::io::EndOfStreamException* = (k::io::EndOfStreamException*) cause;
                    if (error == null || error->getBytesTransferred() != 2L) return -3;
                } else if (expected == 204) {
                    error : k::io::InterruptedIOException* = (k::io::InterruptedIOException*) cause;
                    if (error == null || error->getBytesTransferred() != 2L) return -4;
                } else {
                    error : k::io::IOException* = (k::io::IOException*) cause;
                    if (error == null || error->getErrno() != 5) return -5;
                }
            }
            if (operation->bytesTransferred() != 2L) return -6;
            try { k::io::getIoSync<int>(*future, *operation); return -7; }
            catch (e: k::io::EndOfStreamException&) {
                return expected == 203 && e.getBytesTransferred() == 2L ? 1 : -8;
            } catch (e: k::io::InterruptedIOException&) {
                return expected == 204 && e.getBytesTransferred() == 2L ? 1 : -9;
            } catch (e: k::io::IOException&) {
                return expected == 200 && e.getErrno() == 5 ? 1 : -10;
            }
        }
        cleanup() { delete future; delete operation; }
    )SRC", LIBK_KDI_DIR, LIBK_LIB_DIR);
    REQUIRE(jit);
    auto start = jit->lookup_symbol<void* (*)(void*, void*, int)>("start");
    auto cancel = jit->lookup_symbol<int (*)()>("requestCancel");
    auto pending = jit->lookup_symbol<int (*)()>("pending");
    auto observe = jit->lookup_symbol<int (*)(int)>("observe");
    auto cleanup = jit->lookup_symbol<void (*)()>("cleanup");
    REQUIRE(start); REQUIRE(cancel); REQUIRE(pending); REQUIRE(observe); REQUIRE(cleanup);
    int opcode = K_IO_OP_READ, final_result = 0, expected = 203;
    bool cancelled = false;
    SECTION("Read EOF") {}
    SECTION("Write no progress") { opcode = K_IO_OP_WRITE; expected = 200; }
    SECTION("Future cancellation after a short child") {
        final_result = -ECANCELED; expected = 204; cancelled = true;
    }
    void* buffer = file.allocate(4);
    auto count = rt.mock->request_count();
    void* parent = nullptr;
    REQUIRE_NOTHROW(parent = start(file.handle, buffer, opcode));
    REQUIRE(parent);
    rt.mock->complete(rt.mock->request_after(count).tag, 2);
    auto second = rt.mock->request_after(count + 1);
    if (cancelled) {
        REQUIRE(cancel() == 1);
        REQUIRE(pending() == 1);
    }
    rt.mock->complete(second.tag, final_result);
    rt.mock->wait_processed(second.tag);
    REQUIRE(rt.wait(parent, 0) == 0);
    int result = 0;
    REQUIRE_NOTHROW(result = observe(expected));
    REQUIRE(result == 1);
    REQUIRE(file.position(buffer) == 2);
    REQUIRE(file.leases(buffer) == 0);
    REQUIRE_NOTHROW(cleanup());
    file.release_buffer(buffer);
}

TEST_CASE("FileChannel fully API: unsigned count limit fails without allocating physical gigabytes",
          "[libk][io][proactor][mock][file-fully][k-api][count-limit]") {
    auto& rt = runtime();
    auto map = symbol<void* (*)(const uint32_t*, int, int64_t, int64_t, int)>(
        rt.library, "__k_io_buffer_map");
    void* buffer = map(nullptr, -1, 0, 2147483648LL, 2); // anonymous, untouched virtual storage
    REQUIRE(buffer);
    auto jit = gen_jit_with_stdlib(R"SRC(
        module __fully_count_limit__;
        opened : Future<k::io::file::FileChannel>!;
        class AdoptedBuffer : public k::io::ByteBuffer {
        public:
            AdoptedBuffer(native: k::io::NativeByteBuffer*) : ByteBuffer(native) {}
        }
        start() {
            path : k::io::file::Path("mock-count-limit");
            opened = k::io::file::FileChannel::openAsync(path,
                k::io::file::OPEN_READ | k::io::file::OPEN_WRITE);
        }
        check(native: k::io::NativeByteBuffer*) : int throws(Throwable) {
            deadline : Duration = Duration::ofSeconds(5L);
            channel : k::io::file::FileChannel = opened->get(deadline);
            buffer : AdoptedBuffer! = new AdoptedBuffer(native);
            if (buffer->remaining() != 2147483648u) return -1;
            selected : ExecutorLease! = ExecutorLease::system();
            operation : k::io::IoOperation! = channel.readFully(buffer, 0L, null, *selected);
            if (!operation->isIoDone() || !operation->isDone() ||
                operation->error().kind() != k::io::IoErrorKind::InvalidArgument ||
                operation->bytesTransferred() != 0L || buffer->hasLease()) return -2;
            future : Future<int>! = channel.writeFullyAsync(buffer, 0L);
            valid : bool = false;
            try { future->get(deadline); }
            catch (e: ExecutionException&) {
                error : k::io::IOException* = (k::io::IOException*) e.getCause();
                valid = error != null && error->getErrno() == 22 &&
                    buffer->position() == 0u && !buffer->hasLease();
            }
            delete future; delete operation; delete selected; delete buffer; delete opened;
            return valid ? 1 : -3;
        }
    )SRC", LIBK_KDI_DIR, LIBK_LIB_DIR);
    REQUIRE(jit);
    auto start = jit->lookup_symbol<void (*)()>("start");
    auto check = jit->lookup_symbol<int (*)(void*)>("check");
    REQUIRE(start); REQUIRE(check);
    auto count = rt.mock->request_count();
    REQUIRE_NOTHROW(start());
    auto request = rt.mock->request_after(count);
    REQUIRE(request.opcode == K_IO_OP_OPENAT);
    int fd = open("/dev/null", O_RDWR | O_CLOEXEC);
    REQUIRE(fd >= 0);
    rt.mock->complete(request.tag, fd);
    rt.mock->wait_processed(request.tag);
    int result = 0;
    REQUIRE_NOTHROW(result = check(buffer));
    REQUIRE(result == 1);
    REQUIRE(rt.mock->request_count() == count + 1);
    REQUIRE(fcntl(fd, F_GETFD) == -1);
}

TEST_CASE("FileChannel fully native: registry-side child cancellation suppresses the parent",
          "[libk][io][proactor][mock][file-fully][cancel][shutdown]") {
    FullyFile file;
    auto& rt = file.rt;
    auto child_observer = symbol<void* (*)(void*)>(
        rt.library, "k_io_operation_retain_fully_child");
    ManualExecutor executor;
    FullyProbe probe;
    auto callback = probe.completion(), publisher = probe.publisher();
    void* buffer = file.allocate(6);
    void* parent = file.operation(buffer, 6, K_IO_OP_READ, 0, false, &callback, &executor);
    REQUIRE(file.continuation(parent, &publisher) == 0);
    auto count = rt.mock->request_count();
    REQUIRE(file.submit(parent, 5000000000LL) == 0);
    auto first = rt.mock->request_after(count);
    rt.mock->complete(first.tag, 2);
    rt.mock->complete(first.tag | K_TAG_LINKED_TIMEOUT_FLAG, -ECANCELED);
    auto second = rt.mock->request_after(count + 1);
    void* child = child_observer(parent);
    REQUIRE(child);
    REQUIRE(rt.cancel(child)); // exactly the registry-side shutdown path
    rt.mock->complete(second.tag, 1);
    rt.mock->wait_processed(second.tag);
    REQUIRE(rt.wait(parent, 0) == ETIMEDOUT);
    REQUIRE(file.leases(buffer) == 1);
    rt.mock->complete(second.tag | K_TAG_LINKED_TIMEOUT_FLAG, -ECANCELED);
    REQUIRE(rt.wait(parent, 5000000000LL) == 0);
    executor.wait_released();
    REQUIRE(file.kind(parent) == 1);
    REQUIRE(rt.bytes(parent) == 3);
    REQUIRE(probe.publications == 1);
    REQUIRE(probe.result.kind == 1);
    REQUIRE(probe.result.bytes == 3);
    REQUIRE(probe.callbacks == 0);
    REQUIRE(probe.callback_drops == 1);
    REQUIRE(probe.publisher_drops == 1);
    REQUIRE(child_observer(parent) == nullptr);
    rt.release(child);
    rt.release(parent);
    file.release_buffer(buffer);
}

TEST_CASE("FileChannel fully native: dropping both wrappers never cancels the child chain",
          "[libk][io][proactor][mock][file-fully][lifetime]") {
    FullyFile file;
    auto& rt = file.rt;
    ManualExecutor executor;
    FullyProbe probe;
    auto callback = probe.completion(), publisher = probe.publisher();
    void* buffer = file.allocate(6);
    void* parent = file.operation(buffer, 6, K_IO_OP_WRITE, 0, false, &callback, &executor);
    REQUIRE(file.continuation(parent, &publisher) == 0);
    auto count = rt.mock->request_count();
    REQUIRE(file.submit(parent, -1) == 0);
    auto first = rt.mock->request_after(count);
    file.release_buffer(buffer);
    rt.release(parent);
    static_cast<char*>(first.buf)[0] = 'a';
    rt.mock->complete(first.tag, 2);
    auto second = rt.mock->request_after(count + 1);
    REQUIRE(second.offset == 2);
    REQUIRE(second.buf == static_cast<char*>(first.buf) + 2);
    static_cast<char*>(second.buf)[0] = 'b';
    rt.mock->complete(second.tag, 4);
    Task task = executor.take();
    REQUIRE(probe.publications == 1);
    REQUIRE(probe.result.kind == 0);
    REQUIRE(probe.result.bytes == 6);
    REQUIRE(probe.result.value == 6);
    task.run(task.context); task.drop(task.context);
    executor.wait_released();
    REQUIRE(probe.callbacks == 1);
    REQUIRE(probe.callback_drops == 1);
    REQUIRE(probe.publisher_drops == 1);
}

namespace {
struct StreamNative : FullyFile {
    int peer = -1;
    StreamNative() {
        release_handle(handle);
        int pair[2];
        REQUIRE(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, pair) == 0);
        auto create = symbol<void* (*)(int, int)>(rt.library, "__k_io_handle_create");
        handle = create(pair[0], 1);
        REQUIRE(handle);
        peer = pair[1];
    }
    ~StreamNative() { if (peer >= 0) close(peer); }
    void* primitive(void* buffer, int count, int opcode) {
        auto op = prepare(opcode, handle, buffer, count, 0, 0, nullptr, {}, {}, nullptr);
        REQUIRE(op);
        return op;
    }
};
struct LookupBarrier {
    static inline LookupBarrier* active = nullptr;
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false, released = false;
    int failure = 0;
    int socket_type = 0, protocol = 0;
    LookupBarrier() { active = this; }
    ~LookupBarrier() { unblock(); active = nullptr; }
    static int lookup(const char* node, const char* service, const addrinfo* hints, addrinfo** result) {
        auto& barrier = *active;
        std::unique_lock lock(barrier.mutex);
        barrier.entered = true;
        barrier.socket_type = hints->ai_socktype;
        barrier.protocol = hints->ai_protocol;
        barrier.changed.notify_all();
        barrier.changed.wait(lock, [&] { return barrier.released; });
        int failure = barrier.failure;
        lock.unlock();
        return failure ? failure : getaddrinfo(node, service, hints, result);
    }
    void await() {
        std::unique_lock lock(mutex);
        REQUIRE(changed.wait_for(lock, std::chrono::seconds(5), [&] { return entered; }));
    }
    void unblock() {
        std::lock_guard lock(mutex);
        released = true;
        changed.notify_all();
    }
};
std::vector<uint32_t> native_text(const char* text) {
    std::vector<uint32_t> result;
    while (*text) result.push_back(static_cast<unsigned char>(*text++));
    result.push_back(0);
    return result;
}
void* prepared_connect(const char* endpoint, bool unix_socket, const Callback* callback = nullptr,
                       ManualExecutor* executor = nullptr) {
    auto& rt = runtime();
    auto prepare = symbol<decltype(&__k_io_prepare_request)>(rt.library, "__k_io_prepare_request");
    auto address = symbol<decltype(&__k_io_op_set_stream_endpoint)>(rt.library,
        "__k_io_op_set_stream_endpoint");
    void* op = prepare(K_IO_OP_CONNECT, nullptr, nullptr, 0, 0, 0,
        callback ? callback->context : nullptr,
        KCallable{callback ? reinterpret_cast<void*>(callback->invoke) : nullptr, nullptr},
        KCallable{callback ? reinterpret_cast<void*>(callback->drop) : nullptr, nullptr},
        executor ? &executor->lease : nullptr);
    REQUIRE(op);
    auto text = native_text(endpoint);
    REQUIRE(address(op, text.data(), 0, unix_socket ? 1 : 0) == 0);
    std::fill(text.begin(), text.end(), 0); // no request may borrow this storage
    return op;
}
}

TEST_CASE("Stream native: fully parents reserve one whole direction across short CQEs",
          "[libk][io][proactor][mock][stream-channel][fully][direction]") {
    StreamNative stream;
    auto& rt = stream.rt;
    bool sending = false;
    SECTION("RECV") {}
    SECTION("SEND") { sending = true; }
    int opcode = sending ? K_IO_OP_SEND : K_IO_OP_RECV;
    void* buffer = stream.allocate(6);
    void* other = stream.allocate(1);
    void* opposite = stream.allocate(1);
    void* parent = stream.operation(buffer, 6, opcode);
    auto count = rt.mock->request_count();
    REQUIRE(stream.submit(parent, 5000000000LL) == 0);
    auto first = rt.mock->request_after(count);
    REQUIRE(first.opcode == opcode);
    REQUIRE(first.offset == 0);
    REQUIRE(first.fixed_index == -1);
    if (sending) REQUIRE((first.flags & MSG_NOSIGNAL) != 0);
    void* blocked = stream.primitive(other, 1, opcode);
    REQUIRE(stream.submit(blocked, -1) == -EBUSY);
    REQUIRE(stream.kind(blocked) == 6);
    REQUIRE(stream.leases(other) == 0);
    void* parallel = stream.primitive(opposite, 1, sending ? K_IO_OP_RECV : K_IO_OP_SEND);
    REQUIRE(stream.submit(parallel, -1) == 0);
    auto parallel_request = rt.mock->request_after(count + 1);
    rt.mock->complete(parallel_request.tag, 1);
    REQUIRE(rt.wait(parallel, 5000000000LL) == 0);
    rt.mock->complete(first.tag, 2);
    rt.mock->wait_processed(first.tag);
    REQUIRE(stream.position(buffer) == 0);
    REQUIRE(stream.leases(buffer) == 1);
    REQUIRE(rt.mock->request_count() == count + 2);
    rt.mock->complete(first.tag | K_TAG_LINKED_TIMEOUT_FLAG, -ECANCELED);
    auto second = rt.mock->request_after(count + 2);
    REQUIRE(second.buf == static_cast<char*>(first.buf) + 2);
    REQUIRE(second.len == 4);
    REQUIRE(second.offset == 0);
    REQUIRE(second.timeout_nanos < first.timeout_nanos);
    REQUIRE(stream.position(buffer) == 2);
    void* still_blocked = stream.primitive(other, 1, opcode);
    REQUIRE(stream.submit(still_blocked, -1) == -EBUSY);
    rt.mock->complete(second.tag, 4);
    rt.mock->complete(second.tag | K_TAG_LINKED_TIMEOUT_FLAG, -ECANCELED);
    REQUIRE(rt.wait(parent, 5000000000LL) == 0);
    REQUIRE(rt.bytes(parent) == 6);
    REQUIRE(stream.position(buffer) == 6);
    REQUIRE(stream.leases(buffer) == 0);
    REQUIRE(stream.cursor(stream.handle) == 0);
    void* next = stream.primitive(other, 1, opcode);
    REQUIRE(stream.submit(next, -1) == 0);
    auto next_request = rt.mock->request_after(count + 3);
    rt.mock->complete(next_request.tag, 1);
    REQUIRE(rt.wait(next, 5000000000LL) == 0);
    for (void* op : {parent, blocked, parallel, still_blocked, next}) rt.release(op);
    for (void* value : {buffer, other, opposite}) stream.release_buffer(value);
}

TEST_CASE("Stream native: primitive EOF, fully EOF and zero-progress SEND retain partial counts",
          "[libk][io][proactor][mock][stream-channel][eof]") {
    StreamNative stream;
    auto& rt = stream.rt;
    int opcode = K_IO_OP_RECV;
    bool fully = true;
    SECTION("Primitive EOF") { fully = false; }
    SECTION("Fully EOF") {}
    SECTION("Zero-progress SEND") { opcode = K_IO_OP_SEND; }
    SECTION("Primitive zero-progress SEND") { opcode = K_IO_OP_SEND; fully = false; }
    void* buffer = stream.allocate(4);
    void* op = fully ? stream.operation(buffer, 4, opcode) : stream.primitive(buffer, 4, opcode);
    auto count = rt.mock->request_count();
    REQUIRE(stream.submit(op, -1) == 0);
    auto request = rt.mock->request_after(count);
    if (fully) {
        rt.mock->complete(request.tag, 2);
        request = rt.mock->request_after(count + 1);
    }
    rt.mock->complete(request.tag, 0);
    REQUIRE(rt.wait(op, 5000000000LL) == 0);
    REQUIRE(stream.kind(op) == (fully && opcode == K_IO_OP_RECV ? 4 : 0));
    REQUIRE(stream.error(op) == (opcode == K_IO_OP_SEND ? EIO : fully ? ENODATA : 0));
    REQUIRE(rt.bytes(op) == (fully ? 2 : 0));
    REQUIRE(stream.position(buffer) == (fully ? 2 : 0));
    REQUIRE(stream.leases(buffer) == 0);
    rt.release(op);
    stream.release_buffer(buffer);
}

TEST_CASE("Stream native: close cancels both fully directions and waits for target and timeout drain",
          "[libk][io][proactor][mock][stream-channel][close]") {
    StreamNative stream;
    auto& rt = stream.rt;
    void* read_buffer = stream.allocate(4);
    void* write_buffer = stream.allocate(4);
    void* read = stream.operation(read_buffer, 4, K_IO_OP_RECV);
    void* write = stream.operation(write_buffer, 4, K_IO_OP_SEND);
    auto count = rt.mock->request_count();
    REQUIRE(stream.submit(read, 5000000000LL) == 0);
    REQUIRE(stream.submit(write, 5000000000LL) == 0);
    auto first = rt.mock->request_after(count), second = rt.mock->request_after(count + 1);
    void* closing = stream.prepare(K_IO_OP_CLOSE, stream.handle, nullptr, 0, 0, 0, nullptr, {}, {}, nullptr);
    REQUIRE(closing);
    REQUIRE(stream.submit(closing, -1) == 0);
    REQUIRE(rt.wait(closing, 0) == ETIMEDOUT);
    rt.mock->complete(first.tag, 2);
    rt.mock->complete(second.tag, -ECANCELED);
    rt.mock->wait_processed(second.tag);
    REQUIRE(rt.mock->request_count() == count + 2);
    REQUIRE(stream.leases(read_buffer) == 1);
    REQUIRE(stream.leases(write_buffer) == 1);
    rt.mock->complete(first.tag | K_TAG_LINKED_TIMEOUT_FLAG, -ECANCELED);
    REQUIRE(rt.wait(read, 5000000000LL) == 0);
    REQUIRE(rt.bytes(read) == 2);
    REQUIRE(rt.wait(closing, 0) == ETIMEDOUT);
    rt.mock->complete(second.tag | K_TAG_LINKED_TIMEOUT_FLAG, -ECANCELED);
    auto close_request = rt.mock->request_after(count + 2);
    REQUIRE(close_request.opcode == K_IO_OP_CLOSE);
    REQUIRE(stream.leases(read_buffer) == 0);
    REQUIRE(stream.leases(write_buffer) == 0);
    REQUIRE(close(close_request.fd) == 0); // injected backend models successful close
    rt.mock->complete(close_request.tag, 0);
    REQUIRE(rt.wait(closing, 5000000000LL) == 0);
    REQUIRE(rt.stage(read) == 5);
    REQUIRE(rt.stage(write) == 5);
    for (void* op : {read, write, closing}) rt.release(op);
    stream.release_buffer(read_buffer);
    stream.release_buffer(write_buffer);
}

TEST_CASE("Stream native: numeric and Unix connect storage outlives target CQE and undelivered fd rolls back",
          "[libk][io][proactor][mock][stream-channel][connect][resource]") {
    auto& rt = runtime();
    bool unix_socket = false;
    int mode = 0;
    SECTION("IPv4") {}
    SECTION("AF_UNIX") { unix_socket = true; }
    SECTION("Executor rejection") { mode = 1; }
    SECTION("Executor abandonment") { mode = 2; }
    ManualExecutor executor;
    executor.reject = mode == 1;
    Tracker tracker;
    auto callback = tracker.callback();
    void* op = prepared_connect(unix_socket ? "/tmp/klang-mock-no-filesystem-operation" : "127.0.0.1",
                                unix_socket, &callback, &executor);
    auto submit = symbol<decltype(&__k_io_submit_prepared)>(rt.library, "__k_io_submit_prepared");
    auto count = rt.mock->request_count();
    REQUIRE(submit(op, 5000000000LL) == 0);
    auto request = rt.mock->request_after(count);
    REQUIRE(request.opcode == K_IO_OP_CONNECT);
    REQUIRE(request.addr->sa_family == (unix_socket ? AF_UNIX : AF_INET));
    REQUIRE((fcntl(request.fd, F_GETFL) & O_NONBLOCK) != 0);
    REQUIRE((fcntl(request.fd, F_GETFD) & FD_CLOEXEC) != 0);
    rt.mock->complete(request.tag, 0);
    rt.mock->wait_processed(request.tag);
    REQUIRE(rt.wait(op, 0) == ETIMEDOUT);
    REQUIRE(request.addr->sa_family == (unix_socket ? AF_UNIX : AF_INET));
    if (!mode) REQUIRE(rt.cancel(op));
    REQUIRE(fcntl(request.fd, F_GETFD) >= 0);
    rt.mock->complete(request.tag | K_TAG_LINKED_TIMEOUT_FLAG, -ECANCELED);
    REQUIRE(rt.wait(op, 5000000000LL) == 0);
    if (mode != 1) {
        Task task = executor.take();
        if (!mode) task.run(task.context);
        task.drop(task.context);
    }
    executor.wait_released();
    REQUIRE(tracker.invokes == 0);
    REQUIRE(tracker.drops == 1);
    REQUIRE(fcntl(request.fd, F_GETFD) == -1);
    REQUIRE(errno == EBADF);
    rt.release(op);
}

TEST_CASE("Stream native: accepted descriptors rollback on suppression rejection and task abandonment",
          "[libk][io][proactor][mock][stream-channel][accept][resource]") {
    StreamNative stream;
    auto& rt = stream.rt;
    int mode = 0;
    SECTION("Suppressed before callback") {}
    SECTION("Rejected executor") { mode = 1; }
    SECTION("Accepted executor drops task") { mode = 2; }
    ManualExecutor executor;
    executor.reject = mode == 1;
    Tracker tracker;
    auto callback = tracker.callback();
    void* op = stream.prepare(K_IO_OP_ACCEPT, stream.handle, nullptr, 0, 0, 0,
        callback.context, KCallable{reinterpret_cast<void*>(callback.invoke), nullptr},
        KCallable{reinterpret_cast<void*>(callback.drop), nullptr}, &executor.lease);
    REQUIRE(op);
    auto count = rt.mock->request_count();
    REQUIRE(stream.submit(op, -1) == 0);
    auto request = rt.mock->request_after(count);
    REQUIRE((request.flags & (SOCK_NONBLOCK | SOCK_CLOEXEC)) == (SOCK_NONBLOCK | SOCK_CLOEXEC));
    int accepted = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    REQUIRE(accepted >= 0);
    rt.mock->complete(request.tag, accepted);
    REQUIRE(rt.wait(op, 5000000000LL) == 0);
    if (mode != 1) {
        Task task = executor.take();
        if (mode == 0) REQUIRE(rt.cancel(op));
        if (mode == 0) task.run(task.context);
        task.drop(task.context);
    }
    executor.wait_released();
    REQUIRE(tracker.invokes == 0);
    REQUIRE(tracker.drops == 1);
    REQUIRE(fcntl(accepted, F_GETFD) == -1);
    REQUIRE(errno == EBADF);
    REQUIRE(rt.stage(op) == (mode == 0 ? 5 : 8));
    rt.release(op);
}

TEST_CASE("Stream native: DNS cancellation awaits the accepted lookup and submits no connect",
          "[libk][io][proactor][mock][stream-channel][dns][cancel]") {
    auto& rt = runtime();
    LookupBarrier barrier;
    auto install = symbol<int (*)(void*)>(rt.library, "k_io_runtime_set_test_resolver_lookup");
    REQUIRE(install(reinterpret_cast<void*>(LookupBarrier::lookup)) == 0);
    void* op = prepared_connect("localhost", false);
    auto submit = symbol<decltype(&__k_io_submit_prepared)>(rt.library, "__k_io_submit_prepared");
    auto count = rt.mock->request_count();
    REQUIRE(submit(op, 5000000000LL) == 0);
    barrier.await();
    REQUIRE(rt.cancel(op));
    REQUIRE(rt.wait(op, 0) == ETIMEDOUT);
    REQUIRE(rt.mock->request_count() == count);
    barrier.unblock();
    REQUIRE(rt.wait(op, 5000000000LL) == 0);
    REQUIRE(rt.stage(op) == 5);
    REQUIRE(rt.mock->request_count() == count);
    REQUIRE(install(nullptr) == 0);
    rt.release(op);
}

TEST_CASE("Stream native: DNS and connect consume one deadline and resolver errors remain observable",
          "[libk][io][proactor][mock][stream-channel][dns][timeout]") {
    auto& rt = runtime();
    int mode = 0;
    SECTION("Remaining deadline") {}
    SECTION("Expires during DNS") { mode = 1; }
    SECTION("DNS temporary failure") { mode = 2; }
    LookupBarrier barrier;
    barrier.failure = mode == 2 ? EAI_AGAIN : 0;
    auto install = symbol<int (*)(void*)>(rt.library, "k_io_runtime_set_test_resolver_lookup");
    auto submit = symbol<decltype(&__k_io_submit_prepared)>(rt.library, "__k_io_submit_prepared");
    auto error = symbol<int (*)(const void*)>(rt.library, "__k_io_op_error_code");
    REQUIRE(install(reinterpret_cast<void*>(LookupBarrier::lookup)) == 0);
    void* op = prepared_connect("localhost", false);
    auto count = rt.mock->request_count();
    REQUIRE(submit(op, mode == 1 ? 1 : 5000000000LL) == 0);
    if (mode != 1) barrier.await();
    barrier.unblock();
    if (!mode) {
        auto request = rt.mock->request_after(count);
        REQUIRE(request.opcode == K_IO_OP_CONNECT);
        REQUIRE(request.addr->sa_family == AF_INET);
        REQUIRE(request.timeout_nanos > 0);
        REQUIRE(request.timeout_nanos < 5000000000LL);
        REQUIRE(rt.cancel(op));
        rt.mock->complete(request.tag, -ECANCELED);
        rt.mock->complete(request.tag | K_TAG_LINKED_TIMEOUT_FLAG, -ECANCELED);
    }
    REQUIRE(rt.wait(op, 5000000000LL) == 0);
    if (mode) {
        REQUIRE(rt.mock->request_count() == count);
        REQUIRE(error(op) == (mode == 1 ? ETIMEDOUT : EAGAIN));
    }
    REQUIRE(install(nullptr) == 0);
    rt.release(op);
}

TEST_CASE("Stream native: bounded resolver rejects queue overflow and shutdown but drains accepted jobs",
          "[libk][io][proactor][mock][stream-channel][dns][capacity]") {
    auto& rt = runtime();
    LookupBarrier barrier;
    auto create = symbol<void* (*)(unsigned, unsigned)>(rt.library, "k_io_resolver_create");
    auto lookup = symbol<void (*)(void*, decltype(&LookupBarrier::lookup))>(rt.library,
        "k_io_resolver_set_test_lookup");
    auto submit = symbol<int (*)(void*, void*, const char*, const char*, const addrinfo*)>(rt.library,
        "k_io_resolver_submit_resolve");
    auto stop = symbol<void (*)(void*)>(rt.library, "k_io_resolver_stop_accepting");
    auto destroy = symbol<void (*)(void*)>(rt.library, "k_io_resolver_destroy");
    void* pool = create(1, 1);
    REQUIRE(pool);
    lookup(pool, LookupBarrier::lookup);
    addrinfo hints{};
    hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
    void* first = rt.create(K_IO_OP_NONE, -1, nullptr, 0, 0, 0, nullptr, nullptr, nullptr, nullptr);
    void* queued = rt.create(K_IO_OP_NONE, -1, nullptr, 0, 0, 0, nullptr, nullptr, nullptr, nullptr);
    void* rejected = rt.create(K_IO_OP_NONE, -1, nullptr, 0, 0, 0, nullptr, nullptr, nullptr, nullptr);
    REQUIRE(first); REQUIRE(queued); REQUIRE(rejected);
    REQUIRE(submit(pool, first, "localhost", "0", &hints) == 0);
    barrier.await();
    REQUIRE(submit(pool, queued, "localhost", "0", &hints) == 0);
    REQUIRE(submit(pool, rejected, "localhost", "0", &hints) == -EBUSY);
    stop(pool);
    REQUIRE(submit(pool, rejected, "localhost", "0", &hints) == -ESHUTDOWN);
    REQUIRE_FALSE(rt.cancel(first)); REQUIRE_FALSE(rt.cancel(queued));
    REQUIRE(rt.wait(first, 0) == ETIMEDOUT);
    barrier.unblock();
    destroy(pool);
    REQUIRE(rt.wait(first, 0) == 0);
    REQUIRE(rt.wait(queued, 0) == 0);
    rt.complete(rejected, -ESHUTDOWN);
    rt.release(first); rt.release(queued); rt.release(rejected);
}

TEST_CASE("Stream native: accept claims a pending handle before publication and never double-closes a reused fd",
          "[libk][io][proactor][mock][stream-channel][accept][claim]") {
    StreamNative stream;
    auto& rt = stream.rt;
    struct Publication {
        decltype(&__k_io_stream_handle_prepare) prepare;
        decltype(&__k_io_file_handle_claim) claim;
        void (*release)(void*);
        void* handle = nullptr;
        int fd = -1;
        int replacement = -1;
        bool reject = false;
        std::mutex mutex;
        std::condition_variable changed;
        bool published = false;
        int drops = 0;
        static void invoke(void* context, void* result) {
            auto& self = *static_cast<Publication*>(context);
            self.fd = static_cast<int>(static_cast<Result*>(result)->value);
            self.handle = self.prepare(self.fd);
            if (self.handle) {
                self.claim(self.handle, result);
                if (self.reject) {
                    self.release(self.handle);
                    self.handle = nullptr;
                    self.replacement = open("/dev/null", O_RDWR | O_CLOEXEC);
                    if (self.replacement >= 0 && self.replacement != self.fd) {
                        if (dup2(self.replacement, self.fd) >= 0) {
                            close(self.replacement);
                            self.replacement = self.fd;
                        }
                    }
                }
            }
            std::lock_guard lock(self.mutex);
            self.published = true;
            self.changed.notify_all();
        }
        static void drop(void* context) {
            auto& self = *static_cast<Publication*>(context);
            std::lock_guard lock(self.mutex);
            ++self.drops;
            self.changed.notify_all();
        }
    } publication{
        symbol<decltype(&__k_io_stream_handle_prepare)>(rt.library, "__k_io_stream_handle_prepare"),
        symbol<decltype(&__k_io_file_handle_claim)>(rt.library, "__k_io_file_handle_claim"),
        stream.release_handle
    };
    SECTION("Delivered resource") {}
    SECTION("Rejected publication and descriptor reuse") { publication.reject = true; }
    auto publisher = Callback{&publication, Publication::invoke, Publication::drop};
    void* op = stream.prepare(K_IO_OP_ACCEPT, stream.handle, nullptr, 0, 0, 0, nullptr, {}, {}, nullptr);
    REQUIRE(op);
    REQUIRE(stream.continuation(op, &publisher) == 0);
    auto count = rt.mock->request_count();
    REQUIRE(stream.submit(op, -1) == 0);
    auto request = rt.mock->request_after(count);
    int accepted = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    REQUIRE(accepted >= 0);
    rt.mock->complete(request.tag, accepted);
    {
        std::unique_lock lock(publication.mutex);
        REQUIRE(publication.changed.wait_for(lock, std::chrono::seconds(5),
            [&] { return publication.published && publication.drops == 1; }));
    }
    REQUIRE(publication.fd == accepted);
    REQUIRE_FALSE(rt.cancel(op)); // immutable Future-like terminal publication
    rt.mock->wait_processed(request.tag);
    // A stale generation cannot publish or reclaim the adopted/reused fd.
    rt.mock->complete(request.tag, accepted);
    rt.mock->wait_processed_count(request.tag, 2);
    rt.release(op);
    if (publication.reject) {
        REQUIRE(publication.replacement == accepted);
        REQUIRE(fcntl(accepted, F_GETFD) >= 0);
        REQUIRE(close(accepted) == 0);
    } else {
        REQUIRE(publication.handle);
        REQUIRE(fcntl(accepted, F_GETFD) >= 0);
        stream.release_handle(publication.handle);
        REQUIRE(fcntl(accepted, F_GETFD) == -1);
        REQUIRE(errno == EBADF);
    }
}

TEST_CASE("Stream native: post-fork preparation rejects inherited runtime before descriptor creation",
          "[libk][io][proactor][mock][stream-channel][fork]") {
    auto& rt = runtime();
    auto prepare = symbol<decltype(&__k_io_prepare_request)>(rt.library, "__k_io_prepare_request");
    auto endpoint = symbol<decltype(&__k_io_op_set_stream_endpoint)>(rt.library,
        "__k_io_op_set_stream_endpoint");
    auto text = native_text("127.0.0.1");
    pid_t child = fork();
    REQUIRE(child >= 0);
    if (!child) {
        void* op = prepare(K_IO_OP_CONNECT, nullptr, nullptr, 0, 0, 0, nullptr, {}, {}, nullptr);
        if (!op) _exit(1);
        int result = endpoint(op, text.data(), 0, 0);
        rt.release(op);
        _exit(result == -ENOTSUP ? 0 : 2);
    }
    int status = 0;
    REQUIRE(waitpid(child, &status, 0) == child);
    REQUIRE(WIFEXITED(status));
    REQUIRE(WEXITSTATUS(status) == 0);
}

TEST_CASE("Stream shutdown fixture: queued accept and connect resources are suppressed",
          "[.stream-shutdown-only]") {
    StreamNative stream;
    auto& rt = stream.rt;
    ManualExecutor executor;
    Tracker accept_tracker, connect_tracker;
    auto accept_callback = accept_tracker.callback(), connect_callback = connect_tracker.callback();
    void* accepting = stream.prepare(K_IO_OP_ACCEPT, stream.handle, nullptr, 0, 0, 0,
        accept_callback.context, KCallable{reinterpret_cast<void*>(accept_callback.invoke), nullptr},
        KCallable{reinterpret_cast<void*>(accept_callback.drop), nullptr}, &executor.lease);
    REQUIRE(accepting);
    void* connecting = prepared_connect("127.0.0.1", false, &connect_callback, &executor);
    auto count = rt.mock->request_count();
    REQUIRE(stream.submit(accepting, -1) == 0);
    REQUIRE(stream.submit(connecting, -1) == 0);
    auto accept_request = rt.mock->request_after(count), connect_request = rt.mock->request_after(count + 1);
    int accepted_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    REQUIRE(accepted_fd >= 0);
    rt.mock->complete(accept_request.tag, accepted_fd);
    rt.mock->complete(connect_request.tag, 0);
    rt.mock->wait_processed(accept_request.tag);
    rt.mock->wait_processed(connect_request.tag);
    Task first = executor.take(), second = executor.take();
    auto shutdown = symbol<int (*)()>(rt.library, "k_io_runtime_request_shutdown");
    REQUIRE(shutdown() == 0);
    REQUIRE(fcntl(accepted_fd, F_GETFD) == -1);
    REQUIRE(errno == EBADF);
    REQUIRE(fcntl(connect_request.fd, F_GETFD) == -1);
    REQUIRE(errno == EBADF);
    REQUIRE(accept_tracker.invokes == 0);
    REQUIRE(connect_tracker.invokes == 0);
    REQUIRE(accept_tracker.drops == 1);
    REQUIRE(connect_tracker.drops == 1);
    REQUIRE(rt.stage(accepting) == 5);
    REQUIRE(rt.stage(connecting) == 5);
    executor.wait_released();
    first.run(first.context); first.drop(first.context);
    second.run(second.context); second.drop(second.context);
    REQUIRE(accept_tracker.invokes == 0);
    REQUIRE(connect_tracker.invokes == 0);
    REQUIRE(accept_tracker.drops == 1);
    REQUIRE(connect_tracker.drops == 1);
    rt.release(accepting); rt.release(connecting);
}

TEST_CASE("Stream native: global shutdown rolls back resources already queued on a live executor",
          "[libk][io][proactor][mock][stream-channel][shutdown]") {
    // A fresh executable isolates the one-way runtime shutdown from other cases.
    pid_t child = fork();
    REQUIRE(child >= 0);
    if (!child) {
        execl("/proc/self/exe", "/proc/self/exe",
              "Stream shutdown fixture: queued accept and connect resources are suppressed",
              static_cast<char*>(nullptr));
        _exit(127);
    }
    int status = 0;
    REQUIRE(waitpid(child, &status, 0) == child);
    REQUIRE(WIFEXITED(status));
    REQUIRE(WEXITSTATUS(status) == 0);
}

TEST_CASE("Stream native: K fully Future cancellation publishes the final partial count after child drain",
          "[libk][io][proactor][mock][stream-channel][k-api][partial]") {
    auto& rt = runtime();
    auto jit = gen_jit_with_stdlib(R"SRC(
        module __stream_mock_partial_future__;
        channel : k::io::SocketChannel;
        buffer : k::io::ByteBuffer!;
        future : Future<int>!;
        closed : Future<k::io::IoDone>!;
        start(fd: int) : void throws(Throwable) {
            channel = k::io::SocketChannel(fd);
            buffer = k::io::ByteBuffer::allocate(4u);
            future = channel.readFullyAsync(buffer, 5000000000L);
        }
        cancel() : bool { return future->cancel(); }
        pending() : bool { return !future->isDone() && buffer->hasLease(); }
        verify() : int throws(Throwable) {
            deadline : Duration = Duration::ofSeconds(5L);
            partial : bool = false;
            try { future->get(deadline); }
            catch (e: ExecutionException&) {
                cause : k::io::InterruptedIOException* = (k::io::InterruptedIOException*) e.getCause();
                if (cause != null) partial = cause->getBytesTransferred() == 2L;
            }
            return partial && buffer->position() == 2u && !buffer->hasLease() ? 1 : -1;
        }
        close() : void throws(Throwable) { closed = channel.closeAsync(); }
        finish() : bool throws(Throwable) {
            deadline : Duration = Duration::ofSeconds(5L);
            closed->get(deadline);
            valid : bool = !channel.isOpen();
            delete future; delete closed; delete buffer;
            return valid;
        }
    )SRC", LIBK_KDI_DIR, LIBK_LIB_DIR);
    REQUIRE(jit);
    auto start = jit->lookup_symbol<void (*)(int)>("start");
    auto cancel = jit->lookup_symbol<bool (*)()>("cancel");
    auto pending = jit->lookup_symbol<bool (*)()>("pending");
    auto verify = jit->lookup_symbol<int (*)()>("verify");
    auto closing = jit->lookup_symbol<void (*)()>("close");
    auto finish = jit->lookup_symbol<bool (*)()>("finish");
    REQUIRE(start); REQUIRE(cancel); REQUIRE(pending); REQUIRE(verify); REQUIRE(closing); REQUIRE(finish);
    int pair[2];
    REQUIRE(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, pair) == 0);
    struct Peer {
        int fd;
        ~Peer() { ::close(fd); }
    } peer{pair[1]};
    auto count = rt.mock->request_count();
    REQUIRE_NOTHROW(start(pair[0]));
    auto first = rt.mock->request_after(count);
    REQUIRE(first.opcode == K_IO_OP_RECV);
    rt.mock->complete(first.tag, 2);
    rt.mock->complete(first.tag | K_TAG_LINKED_TIMEOUT_FLAG, -ECANCELED);
    auto second = rt.mock->request_after(count + 1);
    bool cancelled = false, waiting = false;
    REQUIRE_NOTHROW(cancelled = cancel());
    REQUIRE(cancelled);
    REQUIRE_NOTHROW(waiting = pending());
    REQUIRE(waiting);
    rt.mock->complete(second.tag | K_TAG_LINKED_TIMEOUT_FLAG, -ECANCELED);
    rt.mock->wait_processed(second.tag | K_TAG_LINKED_TIMEOUT_FLAG);
    REQUIRE_NOTHROW(waiting = pending());
    REQUIRE(waiting);
    rt.mock->complete(second.tag, -ECANCELED);
    rt.mock->wait_processed(second.tag);
    int result = 0;
    REQUIRE_NOTHROW(result = verify());
    REQUIRE(result == 1);
    REQUIRE_NOTHROW(closing());
    auto close_request = rt.mock->request_after(count + 2);
    REQUIRE(close_request.opcode == K_IO_OP_CLOSE);
    REQUIRE(::close(close_request.fd) == 0);
    rt.mock->complete(close_request.tag, 0);
    bool released = false;
    REQUIRE_NOTHROW(released = finish());
    REQUIRE(released);
}

TEST_CASE("Stream native: close observes DNS and connect parents and owns the descriptor close exactly once",
          "[libk][io][proactor][mock][stream-channel][connect][dns][close]") {
    auto& rt = runtime();
    bool resolving = false;
    SECTION("Kernel connect success races close") {}
    SECTION("Accepted DNS job drains before close") { resolving = true; }
    LookupBarrier barrier;
    auto install = symbol<int (*)(void*)>(rt.library, "k_io_runtime_set_test_resolver_lookup");
    auto prepare = symbol<decltype(&__k_io_prepare_request)>(rt.library, "__k_io_prepare_request");
    auto submit = symbol<decltype(&__k_io_submit_prepared)>(rt.library, "__k_io_submit_prepared");
    auto handle_of = symbol<void* (*)(void*)>(rt.library, "k_io_operation_retain_handle");
    auto descriptor = symbol<int (*)(void*)>(rt.library, "__k_io_handle_fd");
    auto release_handle = symbol<void (*)(void*)>(rt.library, "__k_io_handle_release");
    auto error = symbol<int (*)(void*)>(rt.library, "__k_io_op_error_code");
    if (resolving) REQUIRE(install(reinterpret_cast<void*>(LookupBarrier::lookup)) == 0);
    void* parent = prepared_connect(resolving ? "localhost" : "127.0.0.1", false);
    void* handle = handle_of(parent);
    REQUIRE(handle);
    int fd = descriptor(handle);
    REQUIRE(fd >= 0);
    auto count = rt.mock->request_count();
    REQUIRE(submit(parent, 5000000000LL) == 0);
    KIoBackendRequest connect{};
    if (resolving) barrier.await();
    else connect = rt.mock->request_after(count);
    void* closing = prepare(K_IO_OP_CLOSE, handle, nullptr, 0, 0, 0, nullptr, {}, {}, nullptr);
    REQUIRE(closing);
    REQUIRE(submit(closing, -1) == 0);
    REQUIRE(rt.wait(closing, 0) == ETIMEDOUT);
    REQUIRE(fcntl(fd, F_GETFD) >= 0);
    if (resolving) barrier.unblock();
    else {
        rt.mock->complete(connect.tag, 0);
        rt.mock->complete(connect.tag | K_TAG_LINKED_TIMEOUT_FLAG, -ECANCELED);
    }
    auto close_request = rt.mock->request_after(count + (resolving ? 0 : 1));
    REQUIRE(close_request.opcode == K_IO_OP_CLOSE);
    REQUIRE(close_request.fd == fd);
    REQUIRE(fcntl(fd, F_GETFD) >= 0);
    REQUIRE(::close(fd) == 0);
    rt.mock->complete(close_request.tag, 0);
    REQUIRE(rt.wait(closing, 5000000000LL) == 0);
    REQUIRE(rt.wait(parent, 5000000000LL) == 0);
    REQUIRE(rt.stage(parent) == 5);
    REQUIRE(error(closing) == 0);
    REQUIRE(descriptor(handle) == -1);
    rt.release(parent); rt.release(closing); release_handle(handle);
    if (resolving) REQUIRE(install(nullptr) == 0);
}

