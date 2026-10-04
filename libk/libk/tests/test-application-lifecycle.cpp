/*
 * Isolated Application/runtime shutdown regressions.
 * Copyright 2026 Emilien Kia. Licensed under the Apache License, Version 2.0.
 */
#include <catch2/catch_all.hpp>
#include "helpers.hpp"
#include "../src/runtime/io_backend.h"
#include "../src/runtime/callable_abi.h"
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <dlfcn.h>
#include <fcntl.h>
#include <mutex>
#include <signal.h>
#include <string>
#include <sys/resource.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace {
template<class T> T symbol(void* library, const char* name) {
    auto address = dlsym(library, name);
    REQUIRE(address);
    return reinterpret_cast<T>(address);
}

struct Backend {
    KIoBackend base;
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<KIoBackendCqe> completions;
    std::vector<KIoBackendRequest> requests;
    std::vector<uint64_t> cancelled;
    std::atomic<int>* destroyed;
    std::atomic<int>* unregistered;
    std::atomic<int>* registered_at_destroy;
    bool registration_enabled = false;
    int registered = 0;
    static Backend& self(KIoBackend* b) { return *reinterpret_cast<Backend*>(b); }
    static void destroy(KIoBackend* b) {
        *self(b).registered_at_destroy = self(b).registered;
        ++*self(b).destroyed;
        delete &self(b);
    }
    static bool probe(KIoBackend*, KIoOpcode) { return true; }
    static int submit(KIoBackend* b, const KIoBackendRequest* request) {
        auto& backend = self(b);
        std::lock_guard lock(backend.mutex);
        backend.requests.push_back(*request);
        backend.changed.notify_all();
        return 0;
    }
    static int cancel(KIoBackend* b, uint64_t tag) {
        auto& backend = self(b);
        std::lock_guard lock(backend.mutex);
        for (uint64_t seen : backend.cancelled) if (seen == tag) return 0;
        backend.cancelled.push_back(tag);
        for (const auto& request : backend.requests) {
            if (request.tag != tag) continue;
            backend.completions.push_back({K_TAG_CANCEL_REQUEST_FLAG, 0, 0});
            backend.completions.push_back({tag, -ECANCELED, 0});
            if (request.timeout_nanos > 0)
                backend.completions.push_back({tag | K_TAG_LINKED_TIMEOUT_FLAG, -ECANCELED, 0});
            break;
        }
        backend.changed.notify_all();
        return 0;
    }
    static int flush(KIoBackend*) { return 0; }
    static int wait(KIoBackend* b, KIoBackendCqe* out) {
        auto& backend = self(b);
        std::unique_lock lock(backend.mutex);
        backend.changed.wait(lock, [&] { return !backend.completions.empty(); });
        *out = backend.completions.front();
        backend.completions.pop_front();
        return 0;
    }
    static unsigned peek(KIoBackend*, KIoBackendCqe*, unsigned) { return 0; }
    static void seen(KIoBackend*, const KIoBackendCqe*) {}
    static int wake(KIoBackend* b) {
        self(b).complete(K_TAG_STOP, 0);
        return 0;
    }
    static int register_buffer(KIoBackend* b, void*, size_t) {
        auto& backend = self(b);
        if (!backend.registration_enabled) return -ENOTSUP;
        ++backend.registered;
        return 0;
    }
    static int unregister_buffer(KIoBackend* b, int index) {
        auto& backend = self(b);
        if (index != 0 || backend.registered != 1) return -EINVAL;
        --backend.registered;
        ++*backend.unregistered;
        return 0;
    }
    void complete(uint64_t tag, int result) {
        std::lock_guard lock(mutex);
        completions.push_back({tag, result, 0});
        changed.notify_all();
    }
    bool request(KIoBackendRequest& out) {
        std::lock_guard lock(mutex);
        if (requests.empty()) return false;
        out = requests.back();
        return true;
    }
    KIoBackendRequest request() {
        KIoBackendRequest out{};
        REQUIRE(request(out));
        return out;
    }
};
const KIoBackendOps backend_ops{
    Backend::destroy, Backend::probe, Backend::submit, Backend::cancel,
    Backend::flush, Backend::wait, Backend::peek, Backend::seen, Backend::wake,
    Backend::register_buffer, Backend::unregister_buffer
};

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
    std::atomic<int> refs{1};
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<Task> tasks;
    static ManualExecutor& self(Lease* lease) {
        return *static_cast<ManualExecutor*>(lease->context);
    }
    static void retain(Lease* lease) { ++self(lease).refs; }
    static void release(Lease* lease) { --self(lease).refs; }
    static int submit(Lease* lease, void (*run)(void*), void (*drop)(void*), void* context) {
        auto& executor = self(lease);
        std::lock_guard lock(executor.mutex);
        executor.tasks.push_back({run, drop, context});
        executor.changed.notify_all();
        return 0;
    }
    ManualExecutor() {
        static const LeaseOps ops{retain, release, submit};
        lease = {&ops, this, 1};
    }
    bool take(Task& out) {
        std::unique_lock lock(mutex);
        if (!changed.wait_for(lock, std::chrono::seconds(5), [&] { return !tasks.empty(); }))
            return false;
        out = tasks.front();
        tasks.pop_front();
        return true;
    }
    Task take() {
        Task out{};
        REQUIRE(take(out));
        return out;
    }
};

struct Runtime {
    void* library = dlopen((std::string(LIBK_LIB_DIR) + "/libk.so").c_str(),
                          RTLD_NOW | RTLD_GLOBAL);
    std::atomic<int> destroyed{0};
    std::atomic<int> unregistered{0}, registered_at_destroy{-1};
    Backend* backend = nullptr;
    void* (*get)();
    void* (*create)(int, int, void*, uint32_t, int64_t, int, void*,
                    const KOwnedCallback*, void*, void*);
    int (*submit)(void*, void*, int64_t);
    int (*submit_prepared)(void*, long long);
    void (*retain)(void*);
    void (*release)(void*);
    int (*wait)(void*, int64_t);
    int (*stage)(const void*);
    int (*shutdown)();
    void (*application_exit)();
    int (*active)();
    int (*cooperative)();
    void* (*current_thread)();
    bool (*thread_interrupted)(const void*);
    void (*metrics)(long long*, long long*, long long*, long long*);
    Runtime(bool install = true) {
        REQUIRE(library);
        get = symbol<decltype(get)>(library, "k_io_runtime_get");
        create = symbol<decltype(create)>(library, "k_io_operation_create");
        submit = symbol<decltype(submit)>(library, "k_io_runtime_submit");
        submit_prepared = symbol<decltype(submit_prepared)>(library, "__k_io_submit_prepared");
        retain = symbol<decltype(retain)>(library, "k_io_operation_retain");
        release = symbol<decltype(release)>(library, "k_io_operation_release");
        wait = symbol<decltype(wait)>(library, "__k_io_op_wait_quiescent");
        stage = symbol<decltype(stage)>(library, "__k_io_op_stage");
        shutdown = symbol<decltype(shutdown)>(library, "k_io_runtime_request_shutdown");
        application_exit = symbol<decltype(application_exit)>(library, "__k_application_shutdown");
        active = symbol<decltype(active)>(library, "__k_io_is_backend_active");
        cooperative = symbol<decltype(cooperative)>(library, "__k_io_execution_context_is_cancellation_requested");
        current_thread = symbol<decltype(current_thread)>(library, "k_thread_current");
        thread_interrupted = symbol<decltype(thread_interrupted)>(library, "k_thread_is_interrupted");
        metrics = symbol<decltype(metrics)>(library, "__k_io_get_metrics");
        if (install) install_backend();
    }
    void install_backend() {
        backend = new Backend;
        backend->base.ops = &backend_ops;
        backend->destroyed = &destroyed;
        backend->unregistered = &unregistered;
        backend->registered_at_destroy = &registered_at_destroy;
        REQUIRE(symbol<int (*)(KIoBackend*)>(library, "k_io_runtime_install_test_backend")
                (&backend->base) == 0);
    }
    bool drained() {
        long long inflight, queued, rejected, registered;
        metrics(&inflight, &queued, &rejected, &registered);
        return !active() && !inflight && !queued && !registered && destroyed == 1;
    }
};

struct MockHandle {
    void* value = nullptr;
    int fd = -1;
    void (*release)(void*);
    explicit MockHandle(Runtime& runtime) {
        auto create = symbol<void* (*)(int, int)>(runtime.library, "__k_io_handle_create");
        release = symbol<decltype(release)>(runtime.library, "__k_io_handle_release");
        fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
        REQUIRE(fd >= 0);
        value = create(fd, 0);
        if (!value) close(fd);
        REQUIRE(value);
    }
    MockHandle(const MockHandle&) = delete;
    MockHandle& operator=(const MockHandle&) = delete;
    void reset() noexcept {
        if (value) {
            release(value);
            value = nullptr;
        }
    }
    ~MockHandle() { reset(); }
};

struct ApplicationFixture {
    Runtime runtime;
    ManualExecutor executor;
    void* handle = nullptr; // borrowed; each prepared operation retains its own reference
    int fd = -1;
    void* operation = nullptr;
    Task queued{};
    int submission = -EINPROGRESS;
    const char* hook_failure = nullptr;
    std::atomic<int> invokes{0}, drops{0}, fields{0}, modules{0}, partial_drops{0};
    std::atomic<bool> valid{true};
    bool pending = false;
    void fail(const char* message) noexcept {
        if (!hook_failure) hook_failure = message;
        valid = false;
    }
    ~ApplicationFixture() { if (operation) runtime.release(operation); }
};
ApplicationFixture* fixture;

void replay(Task task) { task.run(task.context); task.drop(task.context); }
int run_main(k::model::gen::jit& jit, bool throwing = false) {
    auto main = jit.lookup_main_entry_symbol<int (*)(int, char**)>();
    REQUIRE(main);
    char program[] = "lifecycle", argument[] = "argument";
    char* argv[]{program, argument, nullptr};
    if (!throwing) return main(2, argv);
    auto catcher = jit.lookup_symbol<int (*)(KCallable, int, char**)>("catchMain");
    REQUIRE(catcher);
    return catcher({reinterpret_cast<void*>(main), nullptr}, 2, argv);
}

const std::string application_prelude = R"K(
    module __application_lifecycle__;
    @ffi::Extern("C") lifecycle_lease() : NativeExecutorLease*;
    @ffi::Extern("C") lifecycle_handle() : k::io::NativeIoHandle*;
    @ffi::Extern("C") lifecycle_publish(op: k::io::NativeIoOperation*);
    @ffi::Extern("C") lifecycle_capture_drop(alive: int);
    @ffi::Extern("C") lifecycle_field_destroy();
    @ffi::Extern("C") lifecycle_module_destroy();
    @ffi::Extern("C") lifecycle_invoked();
    struct Field {
        value : int = 41;
        ~Field() { lifecycle_field_destroy(); value = 0; }
    }
    struct ModuleProbe { ~ModuleProbe() { lifecycle_module_destroy(); } }
    moduleProbe : ModuleProbe;
    struct CaptureProbe {
        field : Field*;
        CaptureProbe(p: Field*) : field(p) {}
        ~CaptureProbe() { lifecycle_capture_drop(field->value); }
    }
    decode(value: long) : int { return (int) value; }
    catchMain(entry: *(int, byte*):int, argc: int, argv: byte*) : int {
        try { return entry(argc, argv); }
        catch (e: Throwable&) { return e.getCode(); }
    }
)K";
const std::string application_body = R"K(
    probe : CaptureProbe! = new CaptureProbe(&_field);
    lease : ExecutorLease(lifecycle_lease());
    completion : k::io::IoCompletion<int> =
        [probe](r: k::io::IoResult<int>) { lifecycle_invoked(); probe->field->value = 0; };
    op : k::io::IoOperation! = k::io::prepareOperation<int>(
        1, lifecycle_handle(), null, 0, 0L, 0, completion, lease, decode);
    lifecycle_publish(op.native());
)K";

void application_case(const std::string& declaration, bool pending = false,
                      bool throwing = false, bool module_field = false) {
    ApplicationFixture state;
    MockHandle handle(state.runtime);
    fixture = &state;
    state.handle = handle.value;
    state.fd = handle.fd;
    state.pending = pending;
    auto jit = gen_jit_with_stdlib(application_prelude + declaration,
                                   LIBK_KDI_DIR, LIBK_LIB_DIR);
    REQUIRE(jit);
    const int result = run_main(*jit, throwing);
    INFO("lifecycle hook failure: " << (state.hook_failure ? state.hook_failure : "none"));
    REQUIRE(state.submission == 0);
    REQUIRE_FALSE(state.hook_failure);
    REQUIRE(result == (throwing ? 723 : 42));
    REQUIRE(state.operation);
    REQUIRE(state.runtime.drained());
    REQUIRE(state.invokes == 0);
    REQUIRE(state.drops == 1);
    REQUIRE(state.fields == (module_field ? 0 : 1));
    REQUIRE(state.valid);
    REQUIRE(state.executor.refs == 1);
    if (!pending) {
        replay(state.queued); // an external task can survive Application destruction
        REQUIRE(state.invokes == 0);
        REQUIRE(state.drops == 1);
    }
    REQUIRE(state.runtime.stage(state.operation) == 5);
    state.runtime.application_exit(); // idempotent, as is the atexit fallback
    jit.reset();
    REQUIRE(state.fields == 1);
    REQUIRE(state.modules == 1);
    REQUIRE(state.valid);
    handle.reset();
    REQUIRE(fcntl(state.fd, F_GETFD) >= 0); // the native observer still retains the handle
    state.runtime.release(state.operation);
    state.operation = nullptr;
    state.handle = nullptr;
    errno = 0;
    const int descriptor_state = fcntl(state.fd, F_GETFD);
    const int descriptor_error = errno;
    REQUIRE(descriptor_state == -1);
    REQUIRE(descriptor_error == EBADF);
    const int reused = dup2(STDERR_FILENO, state.fd);
    REQUIRE(reused == state.fd);
    handle.reset();
    state.runtime.application_exit();
    const int reused_state = fcntl(reused, F_GETFD);
    close(reused);
    REQUIRE(reused_state >= 0);
    fixture = nullptr;
}
} // namespace

extern "C" void* lifecycle_lease() noexcept {
    ManualExecutor::retain(&fixture->executor.lease);
    return &fixture->executor.lease;
}
extern "C" void* lifecycle_handle() noexcept { return fixture->handle; }
extern "C" void lifecycle_publish(void* operation) noexcept {
    auto& state = *fixture;
    if (!operation || state.operation) {
        state.fail("missing or duplicate prepared operation");
        return;
    }
    state.operation = operation;
    state.runtime.retain(operation);
    state.submission = state.runtime.submit_prepared(operation, 5000000000LL);
    if (state.submission != 0) {
        state.fail("prepared READ submission failed");
        return;
    }
    KIoBackendRequest request{};
    if (!state.runtime.backend->request(request)) {
        state.fail("submitted READ did not reach the mock backend");
        return;
    }
    if (request.opcode != K_IO_OP_READ || request.fd != state.fd ||
        request.buf != nullptr || request.len != 0 || request.offset != 0 ||
        request.timeout_nanos != 5000000000LL) {
        state.fail("mock READ differs from the owned-handle zero-length fixture");
        return;
    }
    if (!state.pending) {
        state.runtime.backend->complete(request.tag, 0);
        state.runtime.backend->complete(request.tag | K_TAG_LINKED_TIMEOUT_FLAG, -ECANCELED);
        if (!state.executor.take(state.queued))
            state.fail("completed READ did not queue its executor delivery");
    }
}
extern "C" void lifecycle_invoked() noexcept { ++fixture->invokes; }
extern "C" void lifecycle_capture_drop(int alive) noexcept {
    if (alive != 41 || fixture->fields != 0) fixture->valid = false;
    ++fixture->drops;
}
extern "C" void lifecycle_field_destroy() noexcept {
    if (!fixture->runtime.drained() || fixture->drops != 1 || fixture->invokes != 0)
        fixture->valid = false;
    ++fixture->fields;
}
extern "C" void lifecycle_module_destroy() noexcept {
    if (!fixture->runtime.drained() || fixture->drops != 1 || fixture->invokes != 0)
        fixture->valid = false;
    ++fixture->modules;
}
extern "C" void lifecycle_partial_drop() noexcept { ++fixture->partial_drops; }

TEST_CASE("Lifecycle child: explicit Application args and queued capture",
          "[.application-lifecycle-child]") {
    application_case(R"K(
        class Application : public k::Application {
            _field : Field;
            public main(args: const String[]) : int {
    )K" + application_body + R"K(
                return args.size + 40;
            }
        }
    )K");
}

TEST_CASE("Lifecycle child: pending target and linked timeout before Application fields",
          "[.application-lifecycle-child]") {
    application_case(R"K(
        class Application : public k::Application {
            _field : Field;
            public main() : int {
    )K" + application_body + R"K(
                return 42;
            }
        }
    )K", true);
}

TEST_CASE("Lifecycle child: implicit Application protects module field captures",
          "[.application-lifecycle-child]") {
    application_case(R"K(
        _field : Field;
        main() : int {
    )K" + application_body + R"K(
            return 42;
        }
    )K", false, false, true);
}

TEST_CASE("Lifecycle child: virtual main exception drains before fields and resumes",
          "[.application-lifecycle-child]") {
    application_case(R"K(
        abstract class Layer : public k::Application {
            public main() -> delete;
            public main(args: const String[]) -> delete;
            public main() : int -> delete;
            public abstract main(args: const String[]) : int throws(Throwable);
        }
        class Application : public Layer {
            _field : Field;
            public main(args: const String[]) : int throws(Throwable) {
    )K" + application_body + R"K(
                throw Exception(723);
            }
        }
    )K", false, true);
}

TEST_CASE("Lifecycle child: unused implicit Applications leave runtime virgin",
          "[.application-lifecycle-child]") {
    Runtime runtime(false);
    auto jit = gen_jit_with_stdlib(R"K(
        module __lifecycle_unused__;
        main(args: const String[]) : int { return args.size + 40; }
    )K", LIBK_KDI_DIR, LIBK_LIB_DIR);
    REQUIRE(jit);
    REQUIRE(run_main(*jit) == 42);
    REQUIRE(run_main(*jit) == 42);
    REQUIRE(runtime.active() == 0);
    runtime.install_backend();
    REQUIRE(runtime.active() == 1);
    REQUIRE(runtime.shutdown() == 0);
    REQUIRE(runtime.drained());
    REQUIRE(symbol<int (*)()>(runtime.library, "__k_io_runtime_init")() != 0);
}

TEST_CASE("Lifecycle child: failed Application constructor only destroys completed members",
          "[.application-lifecycle-child]") {
    ApplicationFixture state;
    fixture = &state;
    auto jit = gen_jit_with_stdlib(R"K(
        module __lifecycle_constructor__;
        @ffi::Extern("C") lifecycle_partial_drop();
        struct Member { ~Member() { lifecycle_partial_drop(); } }
        class Application : public k::Application {
            member : Member;
            Application() { throw Exception(724); }
            ~Application() { lifecycle_partial_drop(); lifecycle_partial_drop(); }
            public main() : int { return 0; }
        }
        catchMain(entry: *(int, byte*):int, argc: int, argv: byte*) : int {
            try { return entry(argc, argv); }
            catch (e: Throwable&) { return e.getCode(); }
        }
    )K", LIBK_KDI_DIR, LIBK_LIB_DIR);
    REQUIRE(jit);
    REQUIRE(run_main(*jit, true) == 724);
    REQUIRE(state.partial_drops == 1);
    REQUIRE(state.runtime.drained());
    fixture = nullptr;
}

namespace {
struct RunningCallback {
    Runtime& runtime;
    std::mutex mutex;
    std::condition_variable changed;
    bool entered = false, dropping = false, allow_drop = false;
    std::atomic<int> invokes{0}, drops{0}, self_shutdown{0};
    int (*check_shutdown)() = nullptr;
    std::atomic<int> k_self_shutdown{0};
    std::atomic<bool> thread_interrupted{false};
    std::atomic<bool> thread_registered{false};
    static void invoke(void* context, void*) {
        auto& callback = *static_cast<RunningCallback*>(context);
        callback.self_shutdown = callback.runtime.shutdown();
        if (callback.check_shutdown) callback.k_self_shutdown = callback.check_shutdown();
        ++callback.invokes;
        {
            std::lock_guard lock(callback.mutex);
            callback.entered = true;
            callback.changed.notify_all();
        }
        while (!callback.runtime.cooperative()) std::this_thread::yield();
        void* thread = callback.runtime.current_thread();
        callback.thread_registered = thread != nullptr;
        callback.thread_interrupted = thread && callback.runtime.thread_interrupted(thread);
    }
    static void drop(void* context) {
        auto& callback = *static_cast<RunningCallback*>(context);
        std::unique_lock lock(callback.mutex);
        callback.dropping = true;
        callback.changed.notify_all();
        callback.changed.wait(lock, [&] { return callback.allow_drop; });
        ++callback.drops;
    }
    void wait(bool& flag) {
        std::unique_lock lock(mutex);
        REQUIRE(changed.wait_for(lock, std::chrono::seconds(5), [&] { return flag; }));
    }
};
void running_case(bool custom) {
    Runtime runtime;
    ManualExecutor executor;
    RunningCallback callback{runtime};
    auto jit = gen_jit_with_stdlib(R"K(
        module __lifecycle_callback_shutdown__;
        checkShutdown() : int {
            try { k::io::IoRuntime::shutdown(); return 0; }
            catch (e: IllegalStateException&) { return 1; }
        }
    )K", LIBK_KDI_DIR, LIBK_LIB_DIR);
    REQUIRE(jit);
    callback.check_shutdown = jit->lookup_symbol<int (*)()>("checkShutdown");
    REQUIRE(callback.check_shutdown);
    KOwnedCallback carrier{&callback, RunningCallback::invoke, RunningCallback::drop};
    void* operation = runtime.create(custom ? K_IO_OP_CLOSE : K_IO_OP_READ,
                                     -1, nullptr, 0, 0, 0, nullptr,
                                     &carrier, custom ? &executor.lease : nullptr, nullptr);
    REQUIRE(operation);
    REQUIRE(runtime.submit(runtime.get(), operation, -1) == 0);
    runtime.backend->complete(runtime.backend->request().tag, 0);
    std::thread worker;
    if (custom) {
        Task task = executor.take();
        worker = std::thread([task] { replay(task); });
    }
    callback.wait(callback.entered);
    REQUIRE(callback.self_shutdown == -EDEADLK);
    REQUIRE(callback.k_self_shutdown == 1);
    if (custom)
        REQUIRE_FALSE(symbol<bool (*)(void*)>(runtime.library, "k_io_operation_cancel")(operation));
    std::atomic<bool> exited{false};
    std::thread exit([&] { runtime.application_exit(); exited = true; });
    callback.wait(callback.dropping);
    REQUIRE_FALSE(exited);
    REQUIRE(callback.drops == 0);
    {
        std::lock_guard lock(callback.mutex);
        callback.allow_drop = true;
        callback.changed.notify_all();
    }
    exit.join();
    if (worker.joinable()) worker.join();
    REQUIRE(exited);
    REQUIRE(callback.invokes == 1);
    REQUIRE(callback.drops == 1);
    REQUIRE_FALSE(callback.thread_interrupted);
    if (!custom) REQUIRE(callback.thread_registered);
    REQUIRE(runtime.stage(operation) == 7);
    REQUIRE(executor.refs == 1);
    REQUIRE(runtime.drained());
    runtime.release(operation);
}
}

TEST_CASE("Lifecycle child: system callback cooperation and completed drop",
          "[.application-lifecycle-child]") {
    running_case(false);
}

TEST_CASE("Lifecycle child: custom callback cooperation and completed drop",
          "[.application-lifecycle-child]") {
    running_case(true);
}

TEST_CASE("Lifecycle child: mapped leases and sparse registration drain before backend destruction",
          "[.application-lifecycle-child]") {
    Runtime runtime;
    runtime.backend->registration_enabled = true;
    auto map = symbol<void* (*)(int, int64_t, size_t, int)>(runtime.library, "k_io_buffer_map");
    auto leases = symbol<uint32_t (*)(const void*)>(runtime.library, "k_io_buffer_get_lease_count");
    auto registered = symbol<bool (*)(const void*)>(runtime.library, "k_io_buffer_is_registered");
    auto release_buffer = symbol<void (*)(void*)>(runtime.library, "k_io_buffer_release");
    void* buffer = map(-1, 0, 1, 2); // anonymous private mapping, no pathname or memlock pressure
    REQUIRE(buffer);
    rlimit limit{};
    REQUIRE(getrlimit(RLIMIT_MEMLOCK, &limit) == 0);
    const bool fixed = limit.rlim_cur != 0;
    REQUIRE(registered(buffer) == fixed);
    void* operation = runtime.create(K_IO_OP_READ, -1, nullptr, 1, 0, 0,
                                     buffer, nullptr, nullptr, nullptr);
    REQUIRE(operation);
    REQUIRE(leases(buffer) == 1);
    REQUIRE(runtime.submit(runtime.get(), operation, 5000000000LL) == 0);
    runtime.application_exit();
    REQUIRE(runtime.drained());
    REQUIRE(leases(buffer) == 0);
    REQUIRE_FALSE(registered(buffer));
    REQUIRE(runtime.unregistered == (fixed ? 1 : 0));
    REQUIRE(runtime.registered_at_destroy == 0);
    runtime.release(operation);
    release_buffer(buffer);
    REQUIRE(runtime.unregistered == (fixed ? 1 : 0));
}

TEST_CASE("Lifecycle child: executor-only failed I/O retains and drops queued delivery",
          "[.application-lifecycle-child]") {
    Runtime runtime(false);
    ManualExecutor executor;
    struct Counters { std::atomic<int> invokes{0}, drops{0}; } counters;
    KOwnedCallback carrier{
        &counters,
        [](void* p, void*) { ++static_cast<Counters*>(p)->invokes; },
        [](void* p) { ++static_cast<Counters*>(p)->drops; }
    };
    void* operation = runtime.create(K_IO_OP_CLOSE, -1, nullptr, 0, 0, 0,
                                     nullptr, &carrier, &executor.lease, nullptr);
    REQUIRE(operation);
    symbol<void (*)(void*, int32_t)>(runtime.library, "k_io_operation_on_kernel_completion")
        (operation, -ENOTSUP);
    Task task = executor.take();
    REQUIRE(runtime.active() == 0);
    REQUIRE(counters.drops == 0);
    runtime.application_exit();
    REQUIRE(runtime.active() == 0);
    REQUIRE(runtime.destroyed == 0);
    REQUIRE(counters.invokes == 0);
    REQUIRE(counters.drops == 1);
    REQUIRE(executor.refs == 1);
    replay(task);
    REQUIRE(counters.invokes == 0);
    REQUIRE(counters.drops == 1);
    runtime.release(operation);
    REQUIRE_FALSE(symbol<void* (*)()>(runtime.library, "k_executor_system_lease")());
}

TEST_CASE("Lifecycle child: inherited runtime is rejected without locking or closing parent",
          "[.application-lifecycle-child]") {
    Runtime runtime;
    pid_t child = fork();
    REQUIRE(child >= 0);
    if (!child) _exit(runtime.shutdown() == -ENOTSUP ? 0 : 1);
    int status = 0;
    REQUIRE(waitpid(child, &status, 0) == child);
    REQUIRE(WIFEXITED(status));
    REQUIRE(WEXITSTATUS(status) == 0);
    child = fork();
    REQUIRE(child >= 0);
    if (!child) {
        rlimit limit{0, 0};
        if (setrlimit(RLIMIT_CORE, &limit) != 0) _exit(2);
        runtime.application_exit();
        _exit(1);
    }
    REQUIRE(waitpid(child, &status, 0) == child);
    REQUIRE(WIFSIGNALED(status));
    REQUIRE(WTERMSIG(status) == SIGABRT);
    REQUIRE(runtime.active() == 1);
    REQUIRE(runtime.shutdown() == 0);
    REQUIRE(runtime.drained());
}

TEST_CASE("Lifecycle child: compiled K loopback fields release only after runtime stop",
          "[.application-lifecycle-child]") {
    auto result = build_and_exec(R"K(
        module __lifecycle_loopback__;
        @ffi::Extern("C") private __k_io_is_backend_active() : int;
        class Application : public k::Application {
            server : k::io::ServerSocket!;
            client : k::io::SocketChannel;
            peer : k::io::SocketChannel;
            buffer : k::io::ByteBuffer!;
            pending : k::io::IoOperation!;
            ~Application() {
                if (__k_io_is_backend_active() != 0) throw Exception(725);
                if (buffer != null && buffer->leaseCount() != 0u) throw Exception(726);
                if (pending != null && !pending->isDone()) throw Exception(727);
            }
            public main() : int throws(Throwable) {
                try { k::io::IoRuntime::initialize(); }
                catch (e: k::io::IOException&) {
                    code : int = e.getCode();
                    if (code == 95 || code == 38 || code == 1 || code == 13) return 77;
                    throw Exception(code);
                }
                deadline : Duration = Duration::ofSeconds(5L);
                local : k::io::NetworkAddress("127.0.0.1", 0);
                server = k::io::ServerSocket::bind(local);
                remote : k::io::NetworkAddress("127.0.0.1", server->localPort());
                accepted : Future<k::io::SocketChannel>! = server->acceptAsync(deadline);
                client = k::io::SocketChannel::connectSync(remote, deadline);
                peer = accepted->get(deadline);
                buffer = k::io::ByteBuffer::allocateDirect(1u);
                buffer->put((byte) 42); buffer->flip();
                client.writeFullySync(buffer, deadline);
                buffer->clear();
                peer.readFullySync(buffer, deadline);
                if (buffer->get(0u) != (byte) 42) return 1;
                buffer->clear();
                pending = peer.read(buffer, deadline, [](r: k::io::IoResult<int>) {});
                return 0;
            }
        }
    )K");
    if (result.exit_code == 77) SKIP("io_uring unavailable or prohibited");
    REQUIRE(result.exit_code == 0);
}

TEST_CASE("Application lifecycle: isolated native and compiled K exit gates",
          "[libk][application][application-lifecycle]") {
    auto selector = GENERATE(
        "Lifecycle child: explicit Application args and queued capture",
        "Lifecycle child: pending target and linked timeout before Application fields",
        "Lifecycle child: implicit Application protects module field captures",
        "Lifecycle child: virtual main exception drains before fields and resumes",
        "Lifecycle child: unused implicit Applications leave runtime virgin",
        "Lifecycle child: failed Application constructor only destroys completed members",
        "Lifecycle child: system callback cooperation and completed drop",
        "Lifecycle child: custom callback cooperation and completed drop",
        "Lifecycle child: mapped leases and sparse registration drain before backend destruction",
        "Lifecycle child: executor-only failed I/O retains and drops queued delivery",
        "Lifecycle child: inherited runtime is rejected without locking or closing parent",
        "Lifecycle child: compiled K loopback fields release only after runtime stop");
    INFO(selector);
    pid_t child = fork();
    REQUIRE(child >= 0);
    if (!child) {
        execl("/proc/self/exe", "/proc/self/exe", selector, static_cast<char*>(nullptr));
        _exit(127);
    }
    int status = 0;
    pid_t waited;
    do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
    REQUIRE(waited == child);
    REQUIRE(WIFEXITED(status));
    REQUIRE(WEXITSTATUS(status) == 0);
}

TEST_CASE("Application lifecycle: proxy IR protects construction args and direct or virtual main",
          "[libk][application][application-lifecycle][ir]") {
    const bool indirect = GENERATE(false, true);
    INFO("virtual main: " << indirect);
    auto compiler = k::compiler::create();
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_search_dir(LIBK_KDI_DIR);
    compiler->set_file_resolver(resolver);
    std::string source = "module __lifecycle_proxy_ir__;\n";
    if (indirect) source += R"K(
        abstract class Layer : public k::Application {
            public main() -> delete;
            public main() : int -> delete;
            public main(args: const String[]) -> delete;
            public abstract main(args: const String[]) : int throws(Throwable);
        }
    )K";
    source += indirect ? "class Application : public Layer {\n"
                       : "class Application : public k::Application {\n";
    source += R"K(
            public main(args: const String[]) : int throws(Throwable) {
                throw Exception(723);
            }
        }
    )K";
    compiler->parse_source("lifecycle-proxy-ir.k", source, false, false);
    auto context = compiler->get_context_for_test();
    REQUIRE(context);
    auto& module = context->module();
    llvm::Function* proxy = nullptr;
    llvm::BasicBlock* cleanup = nullptr;
    for (auto& function : module) {
        for (auto& block : function) {
            if (block.getName() == "application_cleanup_lpad") {
                proxy = &function;
                cleanup = &block;
            }
        }
    }
    REQUIRE(proxy);
    REQUIRE(proxy->arg_size() == 2);
    REQUIRE(proxy->hasPersonalityFn());
    REQUIRE_FALSE(llvm::verifyFunction(*proxy, &llvm::errs()));
    int invokes = 0, shutdown_calls = 0, argv_invokes = 0, indirect_invokes = 0;
    for (auto& block : *proxy) {
        for (auto& instruction : block) {
            if (auto* invoke = llvm::dyn_cast<llvm::InvokeInst>(&instruction)) {
                ++invokes;
                REQUIRE(invoke->getUnwindDest() == cleanup);
                auto* callee = invoke->getCalledFunction();
                if (!callee) ++indirect_invokes;
                else if (callee->getName().contains("__k_argv_to_string_array")) ++argv_invokes;
            }
            if (auto* call = llvm::dyn_cast<llvm::CallBase>(&instruction)) {
                auto* callee = call->getCalledFunction();
                if (callee && callee->getName() == "__k_application_shutdown") ++shutdown_calls;
            }
        }
    }
    REQUIRE(invokes >= 3); // constructor, argv conversion, user main
    REQUIRE(argv_invokes == 1);
    REQUIRE(shutdown_calls == 2); // normal and exceptional cleanup
    REQUIRE(indirect_invokes == (indirect ? 1 : 0));
}
