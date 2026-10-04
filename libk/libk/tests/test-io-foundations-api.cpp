#include <catch2/catch_all.hpp>
#include "../../klang/tests/helpers.hpp"

#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fcntl.h>
#include <future>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

namespace {
// Mirrors KIoResult without importing C-only atomic operation internals.
struct CarrierResult {
    int kind;
    int error;
    int64_t bytes;
    int64_t value;
    int resourceClaimed = 0;
};

class NativeStderrCapture {
    int readFd = -1;
    int writeFd = -1;
    int savedFd = -1;

public:
    NativeStderrCapture() {
        int descriptors[2];
        if (pipe(descriptors) != 0) throw std::runtime_error("stderr pipe failed");
        readFd = descriptors[0];
        writeFd = descriptors[1];
        std::fflush(stderr);
        savedFd = dup(STDERR_FILENO);
        if (savedFd < 0 || dup2(writeFd, STDERR_FILENO) < 0) {
            if (savedFd >= 0) close(savedFd);
            close(readFd);
            close(writeFd);
            throw std::runtime_error("stderr redirect failed");
        }
    }

    ~NativeStderrCapture() {
        if (savedFd >= 0) {
            std::fflush(stderr);
            dup2(savedFd, STDERR_FILENO);
            close(savedFd);
        }
        if (writeFd >= 0) close(writeFd);
        if (readFd >= 0) close(readFd);
    }

    std::string finish() {
        std::fflush(stderr);
        if (dup2(savedFd, STDERR_FILENO) < 0)
            throw std::runtime_error("stderr restore failed");
        close(savedFd);
        savedFd = -1;
        close(writeFd);
        writeFd = -1;
        std::string output;
        char buffer[512];
        for (;;) {
            auto count = read(readFd, buffer, sizeof(buffer));
            if (count > 0) output.append(buffer, static_cast<size_t>(count));
            else if (count < 0 && errno == EINTR) continue;
            else if (count < 0) throw std::runtime_error("stderr read failed");
            else break;
        }
        return output;
    }
};
}

TEST_CASE("I/O foundations: execution contexts support const overrides and scoped destruction",
          "[libk][io][proactor][api][execution-context]") {
    auto jit = gen_jit_with_stdlib(R"SRC(
        module __io_foundation_execution_context__;
        destroyed : int = 0;
        class CancelledContext : public k::io::ExecutionContext {
        public:
            CancelledContext() : ExecutionContext() {}
            ~CancelledContext() { ++destroyed; }
            override const isCancellationRequested() : bool { return true; }
        }
        test() : int {
            destroyed = 0;
            {
                base : k::io::ExecutionContext;
                cancelled : CancelledContext;
                view : const k::io::ExecutionContext& = cancelled;
                operation : k::io::IoOperation;
                operationView : const k::io::ExecutionContext& = operation;
                if (base.isCancellationRequested() ||
                    !view.isCancellationRequested() ||
                    operationView.isCancellationRequested()) return -1;
                current : k::io::ExecutionContext! = k::io::ExecutionContext::current();
                currentOperation : k::io::IoOperation! = k::io::IoOperation::current();
                if (current != null || currentOperation != null) return -2;
            }
            return destroyed;
        }
    )SRC", LIBK_KDI_DIR, LIBK_LIB_DIR);
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(test);
    REQUIRE(test() == 1);
}

TEST_CASE("I/O foundations: result aliases and explicit callable carriers import through KDI",
          "[libk][io][proactor][api]") {
    auto jit = gen_jit_with_stdlib(R"SRC(
        module __io_foundation_aliases__;
        observed : int = 0;
        decode(value: long) : int { return (int) value; }
        test() : int {
            result : k::io::IoResult<int>;
            value : int = 42;
            result.setResult(value);
            done : k::io::IoResult<k::io::IoDone>;
            sentinel : k::io::IoDone;
            done.setResult(sentinel);
            callback : k::io::IoCompletion<int> = [](r: k::io::IoResult<int>) {
                if (r.hasResult()) observed = r.getResult();
                else observed = -r.getError().getErrno();
            };
            callback(result);
            if (observed != 42 || !done.hasResult()) return -1;
            error : k::io::IoError = k::io::IoError(k::io::IoErrorKind::System, 5);
            result.setError(error);
            callback(result);
            if (observed != -5) return -2;
            result.setResult(value);
            carrier : k::io::IoCallbackCarrier<int>(callback, decode);
            return result.getResult();
        }
    )SRC", LIBK_KDI_DIR, LIBK_LIB_DIR);
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(test);
    REQUIRE(test() == 42);
}

TEST_CASE("I/O foundations: shared Future cancel hooks survive until target quiescence",
          "[libk][io][proactor][api][future][cancel]") {
    auto jit = gen_jit_with_stdlib(R"SRC(
        module __io_foundation_future_cancel__;
        decode(value: long) : int { return (int) value; }
        @ffi::Extern("C") private k_io_operation_on_kernel_completion(
            operation: k::io::NativeIoOperation*, result: int) : void;
        test() : int {
            executor : DirectExecutor;
            lease : ExecutorLease! = executor.acquireLease();
            operation : k::io::IoOperation! = k::io::prepareOperation<int>(
                1, null, null, 0, 0L, 0, null, *lease, decode);
            future : Future<int>! = k::io::futureFor<int>(*operation, decode);
            shared : Future<int>! = future->share();
            first : bool = shared->cancel();
            second : bool = future->cancel();
            k_io_operation_on_kernel_completion(operation->native(), 3);
            quiescent : int = future->waitQuiescent();
            result : int = first && !second && quiescent == 0 &&
                future->isCancelled() && operation->bytesTransferred() == 3L ? 1 : 0;
            delete shared;
            delete future;
            delete operation;
            delete lease;
            return result;
        }
    )SRC", LIBK_KDI_DIR, LIBK_LIB_DIR);
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(test);
    REQUIRE(test() == 1);
}

TEST_CASE("I/O boundaries: inherited Future waits and unavailable system leases fail explicitly",
          "[libk][io][proactor][api][file-primitives][executor-rejection][boundary-errors]") {
    auto jit = gen_jit_with_stdlib(R"SRC(
        module __io_file_default_executor_rejection__;
        inherited : Future<int>! = null;
        initialize() : int {
            try {
                lease : ExecutorLease! = ExecutorLease::system();
                delete lease;
                promise : Promise<int>;
                inherited = promise.future();
                return 1;
            } catch (e: RejectedExecutionException&) {
                return 0;
            }
        }
        test() : int {
            rejected : int = 0;
            path : k::io::file::Path(".klang-rejected-open");
            try {
                completion : k::io::IoCompletion<k::io::file::FileChannel> =
                    [](r: k::io::IoResult<k::io::file::FileChannel>) {};
                operation : k::io::IoOperation! = k::io::file::FileChannel::open(
                    path, k::io::file::OPEN_READ, completion);
            } catch (e: RejectedExecutionException&) { ++rejected; }
            try {
                future : Future<k::io::file::FileChannel>! =
                    k::io::file::FileChannel::openAsync(path.nativePath());
            } catch (e: RejectedExecutionException&) { ++rejected; }
            try {
                channel : k::io::file::FileChannel =
                    k::io::file::FileChannel::openSync(path);
            } catch (e: RejectedExecutionException&) { ++rejected; }
            try {
                channel : k::io::file::FileChannel;
                channel.closeSync();
            } catch (e: RejectedExecutionException&) { ++rejected; }
            try {
                operation : k::io::IoOperation;
                k::io::getIoSync<int>(*inherited, operation);
            } catch (e: IllegalStateException&) { ++rejected; }
            return rejected;
        }
        cleanup() { delete inherited; }
    )SRC", LIBK_KDI_DIR, LIBK_LIB_DIR);
    REQUIRE(jit);
    auto initialize = jit->lookup_symbol<int (*)()>("initialize");
    auto test = jit->lookup_symbol<int (*)()>("test");
    auto cleanup = jit->lookup_symbol<void (*)()>("cleanup");
    REQUIRE(initialize);
    REQUIRE(test);
    REQUIRE(cleanup);
    int initialized = 0;
    REQUIRE_NOTHROW(initialized = initialize());
    REQUIRE(initialized == 1);
    // The registered lifecycle rejects inherited endpoints without shutting down the parent.
    const pid_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) _exit(test() == 5 ? 0 : 1);
    int status = 0;
    pid_t waited;
    do {
        waited = waitpid(child, &status, 0);
    } while (waited < 0 && errno == EINTR);
    REQUIRE_NOTHROW(cleanup());
    REQUIRE(waited == child);
    REQUIRE(WIFEXITED(status));
    CHECK(WEXITSTATUS(status) == 0);
}

TEST_CASE("I/O carriers: throwing payload publication unlocks the Promise and fails it",
          "[libk][io][proactor][api][future][boundary-errors]") {
    auto jit = gen_jit_with_stdlib(R"SRC(
        module __io_carrier_publication_failure__;
        failAssignment : bool = false;
        struct Payload {
            value : int = 0;
            Payload() {}
            Payload(other: Payload&) { value = other.value; }
            operator=(other: Payload&) : Payload& {
                if (failAssignment) throw Exception(912);
                value = other.value;
                return this;
            }
        }
        decode(value: long) : Payload {
            payload : Payload;
            payload.value = (int) value;
            return payload;
        }
        test(result: byte*) : int throws(Throwable) {
            failAssignment = false;
            carrier : k::io::IoPromiseCarrier<Payload>* =
                new k::io::IoPromiseCarrier<Payload>(decode);
            future : Future<Payload>! = carrier->future();
            failAssignment = true;
            k::io::IoPromiseCarrier<Payload>::invoke((byte*) carrier, result);
            failAssignment = false;
            k::io::IoPromiseCarrier<Payload>::drop((byte*) carrier);
            if (!future->isFailed() || future->isCancelled()) {
                delete future;
                return -1;
            }
            code : int = -2;
            try {
                future->get(Duration::ofMillis(0L));
            } catch (e: ExecutionException&) {
                cause : Throwable* = e.getCause();
                if (cause != null) code = cause->getCode();
            }
            delete future;
            return code;
        }
    )SRC", LIBK_KDI_DIR, LIBK_LIB_DIR);
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int (*)(CarrierResult*)>("test");
    REQUIRE(test);
    CarrierResult result{0, 0, 0, 17};
    NativeStderrCapture capture;
    const int code = test(&result);
    const auto output = capture.finish();
    REQUIRE(code == 912);
    REQUIRE(result.resourceClaimed == 0);
    REQUIRE(output.find("origin=io.promise.publish code=912") != std::string::npos);
}

TEST_CASE("Executor carriers: Runnable failures are contained and diagnosed once per submission",
          "[libk][executor][io][proactor][api][boundary-errors]") {
    auto jit = gen_jit_with_stdlib(R"SRC(
        module __executor_runnable_boundary__;
        runs : int = 0;
        drops : int = 0;
        class ThrowingCommand : public Runnable {
        public:
            ~ThrowingCommand() { ++drops; }
            override run() : void throws(Throwable) { ++runs; throw Exception(915); }
        }
        counts() : int { return runs * 10 + drops; }
        testLease() : int {
            runs = 0;
            drops = 0;
            executor : DirectExecutor;
            lease : ExecutorLease! = executor.acquireLease();
            command : ThrowingCommand;
            lease->execute(&command);
            delete lease;
            return 1;
        }
        testDirect() : int {
            runs = 0;
            drops = 0;
            executor : DirectExecutor;
            endpoint : Executor+ = &executor;
            command : ThrowingCommand;
            try {
                endpoint->execute(&command);
            } catch (e: Throwable&) {
                return e.getCode();
            }
            return -1;
        }
        testLoop() : int {
            runs = 0;
            drops = 0;
            loop : k::io::EventLoop;
            command : ThrowingCommand;
            loop.execute(&command);
            return loop.runOnce(Duration::ofMillis(0L)) ? 1 : 0;
        }
    )SRC", LIBK_KDI_DIR, LIBK_LIB_DIR);
    REQUIRE(jit);
    auto lease = jit->lookup_symbol<int (*)()>("testLease");
    auto direct = jit->lookup_symbol<int (*)()>("testDirect");
    auto loop = jit->lookup_symbol<int (*)()>("testLoop");
    auto counts = jit->lookup_symbol<int (*)()>("counts");
    REQUIRE(lease);
    REQUIRE(direct);
    REQUIRE(loop);
    REQUIRE(counts);
    const int expected[] = {1, -1, 1};
    const auto submissions = {lease, direct, loop};
    const std::string diagnostic = "origin=task.run code=915";
    size_t index = 0;
    for (auto submission : submissions) {
        NativeStderrCapture capture;
        const int result = submission();
        const auto output = capture.finish();
        REQUIRE(result == expected[index++]);
        REQUIRE(counts() == 11);
        const auto first = output.find(diagnostic);
        REQUIRE(first != std::string::npos);
        REQUIRE(output.find(diagnostic, first + diagnostic.size()) == std::string::npos);
    }
}

TEST_CASE("Executor carriers: throwing task cleanup is diagnosed and dropped exactly once",
          "[libk][executor][io][proactor][api][boundary-errors]") {
    auto jit = gen_jit_with_stdlib(R"SRC(
        module __executor_drop_failure__;
        drops : int = 0;
        runs : int = 0;
        struct Probe {
            ~Probe() {
                ++drops;
                throw Exception(917);
            }
        }
        test() : int {
            drops = 0;
            runs = 0;
            executor : DirectExecutor;
            lease : ExecutorLease! = executor.acquireLease();
            probe : Probe! = new Probe();
            task : !() = [probe]() {
                ++runs;
                throw Exception(916);
            };
            lease->execute(task);
            lease->execute([]() { ++runs; });
            delete lease;
            return drops == 1 && runs == 2 ? 1 : 0;
        }
        testRejected() : int {
            drops = 0;
            runs = 0;
            lease : ExecutorLease;
            probe : Probe! = new Probe();
            task : !() = [probe]() { ++runs; };
            rejected : bool = false;
            try {
                lease.execute(task);
            } catch (e: RejectedExecutionException&) {
                rejected = true;
            }
            return rejected && drops == 1 && runs == 0 ? 1 : 0;
        }
    )SRC", LIBK_KDI_DIR, LIBK_LIB_DIR);
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int (*)()>("test");
    auto rejected = jit->lookup_symbol<int (*)()>("testRejected");
    REQUIRE(test);
    REQUIRE(rejected);
    NativeStderrCapture capture;
    const int result = test();
    const int rejectedResult = rejected();
    const auto output = capture.finish();
    REQUIRE(result == 1);
    REQUIRE(rejectedResult == 1);
    REQUIRE(output.find("origin=task.run code=916") != std::string::npos);
    const std::string diagnostic = "origin=task.drop code=917";
    const auto first = output.find(diagnostic);
    REQUIRE(first != std::string::npos);
    const auto second = output.find(diagnostic, first + diagnostic.size());
    REQUIRE(second != std::string::npos);
    REQUIRE(output.find(diagnostic, second + diagnostic.size()) == std::string::npos);
}

