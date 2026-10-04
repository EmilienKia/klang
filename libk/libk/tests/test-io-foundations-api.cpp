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

TEST_CASE("I/O carriers: empty IoDone survives Future and union subobject cleanup before delivery",
          "[libk][io][proactor][api][io-done-carrier][callback-carrier][regression]") {
    auto jit = gen_jit_with_stdlib(R"SRC(
        module __io_done_deferred_carrier__;
        calls : int = 0;
        callbackState : int = -2;
        stage : int = 0;
        caught : bool = false;
        failureCode : int = 0;
        live : int = 0;
        drops : int = 0;
        invalid : int = 0;
        closureDrops : int = 0;
        preDeliveryClosureDrops : int = -1;
        deliveryReady : bool = false;
        fixtureCaptured : bool = false;
        struct Probe {
            ~Probe() { ++closureDrops; }
        }
        class Payload {
            magic : int = 73;
            Payload() { ++live; }
            Payload(other: const Payload&) {
                if (other.magic != 73) ++invalid;
                ++live;
            }
            ~Payload() {
                if (magic != 73) ++invalid;
                magic = 0;
                --live;
                ++drops;
            }
        }
        struct Slot {
            carrier : k::io::IoCallbackCarrier<k::io::IoDone>! = null;
        }
        decodeDone(value: long) : k::io::IoDone { return k::io::IoDone(); }
        decodeCount(value: long) : int { return (int) value; }
        @ffi::Extern("C") private k_io_operation_on_kernel_completion(
            operation: k::io::NativeIoOperation*, result: int);
        record(result: k::io::IoResult<k::io::IoDone>&) {
            ++calls;
            callbackState = result.hasResult() ? 1 : (result.hasError() ? 0 : -1);
            if (callbackState != 1) return;
            copied : k::io::IoResult<k::io::IoDone>(result);
            if (!copied.hasResult() || copied.hasError()) { callbackState = -3; return; }
            value : k::io::IoDone = copied.getResult();
            copied.setResult(value);
            if (!copied.hasResult() || copied.hasError()) callbackState = -4;
        }
        test(nativeResult: byte*, captured: bool) : int {
            calls = 0; callbackState = -2; stage = 0;
            caught = false; failureCode = 0;
            live = 0; drops = 0; invalid = 0; closureDrops = 0;
            preDeliveryClosureDrops = -1; deliveryReady = false; fixtureCaptured = captured;
            slot : Slot;
            slotPtr : Slot* = &slot;
            task : !() = [slotPtr, captured]() {
                stage = 1;
                probe : Probe! = new Probe();
                completion : k::io::IoCompletion<k::io::IoDone> = null;
                if (captured) {
                    completion = [probe](r: k::io::IoResult<k::io::IoDone>) { record(r); };
                } else {
                    delete probe;
                    completion = [](r: k::io::IoResult<k::io::IoDone>) { record(r); };
                }
                slotPtr->carrier = new k::io::IoCallbackCarrier<k::io::IoDone>(
                    completion, decodeDone);
                if (completion != null || probe != null) return;
                stage = 2;
                {
                    executor : DirectExecutor;
                    lease : ExecutorLease! = executor.acquireLease();
                    operation : k::io::IoOperation! = k::io::prepareOperation<int>(
                        1, null, null, 0, 0L, 0, null, *lease, decodeCount);
                    future : Future<int>! = k::io::futureFor<int>(*operation, decodeCount);
                    {
                        empty : Expected<Payload, int>;
                        copiedEmpty : Expected<Payload, int>(empty);
                    }
                    if (live != 0 || drops != 0 || invalid != 0) return;
                    source : Payload;
                    holder : Expected<Payload, int>;
                    holder.setResult(source);
                    k_io_operation_on_kernel_completion(operation->native(), 17);
                    deadline : Duration = Duration::ofSeconds(5L);
                    try {
                        if (future->get(deadline) != 17 || !holder.hasResult() ||
                            live != 2 || calls != 0) return;
                    } catch (e: Throwable&) {
                        caught = true;
                        failureCode = e.getCode();
                        return;
                    }
                    stage = 3;
                }
                if (live != 0 || drops != 2 || invalid != 0 || calls != 0) return;
                stage = 4;
            };
            {
                loop : k::io::EventLoop;
                deadline : Duration = Duration::ofSeconds(5L);
                loop.execute(task);
                if (task != null || !loop.runOnce(deadline)) return -1;
            }
            valid : bool = stage == 4 && calls == 0 &&
                closureDrops == (captured ? 0 : 1);
            preDeliveryClosureDrops = closureDrops;
            deliveryReady = valid;
            if (slot.carrier == null) return -2;
            k::io::IoCallbackCarrier<k::io::IoDone>::invoke((byte*) slot.carrier, nativeResult);
            delete slot.carrier;
            return valid && calls == 1 && callbackState == 1 && closureDrops == 1 &&
                live == 0 && drops == 2 && invalid == 0 ? 1 : -3;
        }
        deliveryState() : int { return callbackState; }
        progress() : int { return stage; }
        caughtException() : bool { return caught; }
        exceptionCode() : int { return failureCode; }
        callbackCount() : int { return calls; }
        closureDropCount() : int { return closureDrops; }
        preDeliveryClosureDropCount() : int { return preDeliveryClosureDrops; }
        livePayloadCount() : int { return live; }
        payloadDropCount() : int { return drops; }
        invalidPayloadCount() : int { return invalid; }
        readyForDelivery() : bool { return deliveryReady; }
        capturedMode() : bool { return fixtureCaptured; }
    )SRC", LIBK_KDI_DIR, LIBK_LIB_DIR, std::getenv("KLANG_F1_DUMP_IR") != nullptr);
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int (*)(CarrierResult*, bool)>("test");
    auto deliveryState = jit->lookup_symbol<int (*)()>("deliveryState");
    auto progress = jit->lookup_symbol<int (*)()>("progress");
    auto caughtException = jit->lookup_symbol<bool (*)()>("caughtException");
    auto exceptionCode = jit->lookup_symbol<int (*)()>("exceptionCode");
    auto callbackCount = jit->lookup_symbol<int (*)()>("callbackCount");
    auto closureDropCount = jit->lookup_symbol<int (*)()>("closureDropCount");
    auto preDeliveryClosureDropCount = jit->lookup_symbol<int (*)()>("preDeliveryClosureDropCount");
    auto livePayloadCount = jit->lookup_symbol<int (*)()>("livePayloadCount");
    auto payloadDropCount = jit->lookup_symbol<int (*)()>("payloadDropCount");
    auto invalidPayloadCount = jit->lookup_symbol<int (*)()>("invalidPayloadCount");
    auto readyForDelivery = jit->lookup_symbol<bool (*)()>("readyForDelivery");
    auto capturedMode = jit->lookup_symbol<bool (*)()>("capturedMode");
    REQUIRE(test);
    REQUIRE(deliveryState);
    REQUIRE(progress);
    REQUIRE(caughtException);
    REQUIRE(exceptionCode);
    REQUIRE(callbackCount);
    REQUIRE(closureDropCount);
    REQUIRE(preDeliveryClosureDropCount);
    REQUIRE(livePayloadCount);
    REQUIRE(payloadDropCount);
    REQUIRE(invalidPayloadCount);
    REQUIRE(readyForDelivery);
    REQUIRE(capturedMode);
    for (bool captured : {false, true}) {
        CarrierResult result{0, 0, 0, 0};
        int value = 0;
        REQUIRE_NOTHROW(value = test(&result, captured));
        INFO("captured=" << captured << " stage=" << progress()
             << " callback state=" << deliveryState() << " native claimed=" << result.resourceClaimed
             << " caught=" << caughtException() << " exception code=" << exceptionCode()
             << " fixture captured=" << capturedMode() << " ready for delivery=" << readyForDelivery()
             << " pre-delivery closure drops=" << preDeliveryClosureDropCount()
             << " closure drops=" << closureDropCount() << " live payloads=" << livePayloadCount()
             << " payload drops=" << payloadDropCount() << " invalid payloads=" << invalidPayloadCount()
             << " callback count=" << callbackCount());
        CHECK_FALSE(caughtException());
        CHECK(result.resourceClaimed == 1);
        CHECK(capturedMode() == captured);
        CHECK(readyForDelivery());
        CHECK(preDeliveryClosureDropCount() == (captured ? 0 : 1));
        CHECK(callbackCount() == 1);
        CHECK(closureDropCount() == 1);
        CHECK(livePayloadCount() == 0);
        CHECK(payloadDropCount() == 2);
        CHECK(invalidPayloadCount() == 0);
        CHECK(value == 1);
    }
}

TEST_CASE("I/O carriers: KDI aliases move owned completions through null decoder construction",
          "[libk][io][proactor][api][callback-carrier][owner][null]") {
    auto jit = gen_jit_with_stdlib(R"SRC(
        module __io_carrier_owned_aliases__;
        drops : int = 0;
        calls : int = 0;
        struct Probe {
            ~Probe() { ++drops; }
        }
        template<typename T>
        makeCarrier(completion: k::io::IoCompletion<T>) : k::io::IoCallbackCarrier<T>! {
            return new k::io::IoCallbackCarrier<T>(completion, null);
        }
        testDone(result: byte*) : int {
            drops = 0;
            calls = 0;
            probe : Probe! = new Probe();
            callback : k::io::IoCompletion<k::io::IoDone> =
                [probe](r: k::io::IoResult<k::io::IoDone>) {
                    if (r.hasError()) {
                        error : k::io::IoError = r.getError();
                        if (error.kind() == k::io::IoErrorKind::Unsupported) ++calls;
                    }
                };
            carrier : k::io::IoCallbackCarrier<k::io::IoDone>! =
                makeCarrier<k::io::IoDone>(callback);
            moved : bool = callback == null && probe == null && drops == 0;
            k::io::IoCallbackCarrier<k::io::IoDone>::invoke((byte*) carrier, result);
            delete carrier;
            return moved && calls == 1 && drops == 1 ? 1 : 0;
        }
        testInt(result: byte*) : int {
            drops = 0;
            calls = 0;
            probe : Probe! = new Probe();
            callback : k::io::IoCompletion<int> =
                [probe](r: k::io::IoResult<int>) {
                    if (r.hasError()) ++calls;
                };
            carrier : k::io::IoCallbackCarrier<int>! = makeCarrier<int>(callback);
            moved : bool = callback == null && probe == null && drops == 0;
            k::io::IoCallbackCarrier<int>::invoke((byte*) carrier, result);
            delete carrier;
            return moved && calls == 1 && drops == 1 ? 1 : 0;
        }
        testNull() : int {
            callback : k::io::IoCallbackCarrier<k::io::IoDone>! =
                new k::io::IoCallbackCarrier<k::io::IoDone>(null, null);
            promise : k::io::IoPromiseCarrier<k::io::IoDone>! =
                new k::io::IoPromiseCarrier<k::io::IoDone>(null);
            future : Future<k::io::IoDone>! = promise->future();
            delete promise;
            pending : bool = !future->isDone();
            delete future;
            delete callback;
            return pending ? 1 : 0;
        }
    )SRC", LIBK_KDI_DIR, LIBK_LIB_DIR);
    REQUIRE(jit);
    auto done = jit->lookup_symbol<int (*)(CarrierResult*)>("testDone");
    auto integer = jit->lookup_symbol<int (*)(CarrierResult*)>("testInt");
    auto null = jit->lookup_symbol<int (*)()>("testNull");
    REQUIRE(done);
    REQUIRE(integer);
    REQUIRE(null);
    CarrierResult result{5, 95, 0, 0};
    CHECK(done(&result) == 1);
    CHECK(integer(&result) == 1);
    CHECK(null() == 1);
    CHECK(result.resourceClaimed == 0);
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

TEST_CASE("I/O carriers: Future failure uses the shared I/O taxonomy",
          "[libk][io][proactor][api][future][boundary-errors]") {
    auto jit = gen_jit_with_stdlib(R"SRC(
        module __io_carrier_taxonomy__;
        decode(value: long) : int { return (int) value; }
        test(result: byte*, cancelled: bool, expectedErr: int, expectedBytes: long) : int throws(Throwable) {
            carrier : k::io::IoPromiseCarrier<int>* =
                new k::io::IoPromiseCarrier<int>(decode);
            future : Future<int>! = carrier->future();
            k::io::IoPromiseCarrier<int>::invoke((byte*) carrier, result);
            k::io::IoPromiseCarrier<int>::drop((byte*) carrier);
            if (!future->isDone() || future->isSuccess() ||
                future->isCancelled() != cancelled) {
                delete future;
                return -1;
            }
            code : int = -2;
            try {
                future->get(Duration::ofMillis(0L));
            } catch (e: CancellationException&) {
                code = e.getCode();
            } catch (e: ExecutionException&) {
                cause : Throwable* = e.getCause();
                if (cause != null) {
                    code = cause->getCode();
                    if (code == 200) {
                        error : k::io::IOException* = (k::io::IOException*) cause;
                        if (error == null || error->getErrno() != expectedErr) code = -3;
                    } else if (code == 202) {
                        error : k::io::ClosedChannelException* =
                            (k::io::ClosedChannelException*) cause;
                        if (error == null || error->getErrno() != expectedErr) code = -3;
                    } else if (code == 203) {
                        error : k::io::EndOfStreamException* =
                            (k::io::EndOfStreamException*) cause;
                        if (error == null || error->getBytesTransferred() != expectedBytes)
                            code = -4;
                    } else if (code == 204) {
                        error : k::io::InterruptedIOException* =
                            (k::io::InterruptedIOException*) cause;
                        if (error == null || error->getBytesTransferred() != expectedBytes)
                            code = -5;
                    }
                }
            }
            delete future;
            return code;
        }
    )SRC", LIBK_KDI_DIR, LIBK_LIB_DIR);
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int (*)(CarrierResult*, bool, int, int64_t)>("test");
    REQUIRE(test);
    struct Case {
        CarrierResult result;
        int code;
        bool cancelled;
    };
    const Case cases[] = {
        {{0, EACCES, 0, 0}, 200, false},
        {{1, ECANCELED, 0, 0}, 103, true},
        {{1, ECANCELED, 9, 0}, 204, false},
        {{2, EBADF, 0, 0}, 202, false},
        {{3, ETIMEDOUT, 0, 0}, 102, false},
        {{4, 0, 7, 0}, 203, false},
        {{5, ENOTSUP, 0, 0}, 200, false},
        {{7, ESHUTDOWN, 0, 0}, 107, false},
        {{9, EINTR, 0, 0}, 101, false},
        {{9, EINTR, 4, 0}, 204, false},
        // Kind, not a magic platform errno, determines cancellation.
        {{0, ECANCELED, 0, 0}, 200, false}
    };
    for (auto entry : cases) {
        INFO("kind=" << entry.result.kind << " errno=" << entry.result.error);
        REQUIRE(test(&entry.result, entry.cancelled, entry.result.error,
                     entry.result.bytes) == entry.code);
        REQUIRE(entry.result.resourceClaimed == 0);
    }
}

TEST_CASE("I/O carriers: throwing decode fails the Future and diagnoses its original code",
          "[libk][io][proactor][api][future][boundary-errors]") {
    auto jit = gen_jit_with_stdlib(R"SRC(
        module __io_carrier_decode_failure__;
        decode(value: long) : int { throw Exception(911); }
        test(result: byte*) : int throws(Throwable) {
            carrier : k::io::IoPromiseCarrier<int>* =
                new k::io::IoPromiseCarrier<int>(decode);
            future : Future<int>! = carrier->future();
            k::io::IoPromiseCarrier<int>::invoke((byte*) carrier, result);
            k::io::IoPromiseCarrier<int>::drop((byte*) carrier);
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
    REQUIRE(code == 911);
    REQUIRE(result.resourceClaimed == 0);
    REQUIRE(output.find("origin=io.promise.publish code=911") != std::string::npos);
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

TEST_CASE("I/O carriers: callback decode failure is delivered once and diagnosed",
          "[libk][io][proactor][api][boundary-errors]") {
    auto jit = gen_jit_with_stdlib(R"SRC(
        module __io_callback_decode_failure__;
        calls : int = 0;
        decode(value: long) : int { throw Exception(913); }
        test(result: byte*) : int {
            calls = 0;
            callback : k::io::IoCompletion<int> = [](r: k::io::IoResult<int>) {
                if (r.hasError()) {
                    error : k::io::IoError = r.getError();
                    if (error.kind() == k::io::IoErrorKind::System &&
                        error.getErrno() == 0) ++calls;
                }
            };
            carrier : k::io::IoCallbackCarrier<int>* =
                new k::io::IoCallbackCarrier<int>(callback, decode);
            k::io::IoCallbackCarrier<int>::invoke((byte*) carrier, result);
            k::io::IoCallbackCarrier<int>::drop((byte*) carrier);
            return calls;
        }
    )SRC", LIBK_KDI_DIR, LIBK_LIB_DIR);
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int (*)(CarrierResult*)>("test");
    REQUIRE(test);
    CarrierResult result{0, 0, 0, 17};
    NativeStderrCapture capture;
    const int calls = test(&result);
    const auto output = capture.finish();
    REQUIRE(calls == 1);
    REQUIRE(result.resourceClaimed == 0);
    REQUIRE(output.find("origin=io.decode code=913") != std::string::npos);
}

TEST_CASE("I/O carriers: a throwing callback completes its operation without false cancellation",
          "[libk][io][proactor][api][boundary-errors]") {
    auto jit = gen_jit_with_stdlib(R"SRC(
        module __io_callback_failure_terminal__;
        calls : int = 0;
        observedContext : bool = false;
        decode(value: long) : int { return (int) value; }
        @ffi::Extern("C") private k_io_operation_on_kernel_completion(
            operation: k::io::NativeIoOperation*, result: int) : void;
        test() : int {
            calls = 0;
            observedContext = false;
            loop : k::io::EventLoop;
            lease : ExecutorLease! = loop.acquireLease();
            callback : k::io::IoCompletion<int> = [](r: k::io::IoResult<int>) {
                ++calls;
                context : k::io::ExecutionContext! = k::io::ExecutionContext::current();
                currentOperation : k::io::IoOperation! = k::io::IoOperation::current();
                observedContext = context != null && currentOperation != null &&
                    !context->isCancellationRequested() &&
                    currentOperation->isCallbackRunning();
                delete currentOperation;
                delete context;
                throw Exception(914);
            };
            operation : k::io::IoOperation! = k::io::prepareOperation<int>(
                1, null, null, 0, 0L, 0, callback, *lease, decode);
            k_io_operation_on_kernel_completion(operation->native(), 17);
            // Dispatch is bounded, even if the worker has not enqueued yet.
            limit : int = 0;
            while (!operation->isDone() && limit < 20) {
                loop.runOnce(Duration::ofMillis(100L));
                ++limit;
            }
            result : int = calls == 1 && observedContext && operation->isDone() &&
                !operation->isCancelled() &&
                operation->stage() == k::io::IoOperationStage::Completed ? 1 : 0;
            delete operation;
            delete lease;
            return result;
        }
    )SRC", LIBK_KDI_DIR, LIBK_LIB_DIR);
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(test);
    NativeStderrCapture capture;
    const int result = test();
    const auto output = capture.finish();
    REQUIRE(result == 1);
    REQUIRE(output.find("origin=io.callback code=914") != std::string::npos);
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

TEST_CASE("I/O carriers: callback and Promise drop failures are diagnosed exactly once",
          "[libk][io][proactor][api][boundary-errors]") {
    auto jit = gen_jit_with_stdlib(R"SRC(
        module __io_carrier_drop_failure__;
        drops : int = 0;
        struct Probe {
            ~Probe() { ++drops; throw Exception(918); }
        }
        struct Payload {
            Payload() {}
            Payload(other: const Payload&) {}
            ~Payload() { ++drops; throw Exception(919); }
        }
        decode(value: long) : int { return (int) value; }
        testCallback() : int {
            drops = 0;
            probe : Probe! = new Probe();
            callback : k::io::IoCompletion<int> =
                [probe](r: k::io::IoResult<int>) {};
            carrier : k::io::IoCallbackCarrier<int>* =
                new k::io::IoCallbackCarrier<int>(callback, decode);
            k::io::IoCallbackCarrier<int>::drop((byte*) carrier);
            return drops;
        }
        testPromise() : int {
            drops = 0;
            carrier : k::io::IoPromiseCarrier<Payload>* =
                new k::io::IoPromiseCarrier<Payload>(null);
            k::io::IoPromiseCarrier<Payload>::drop((byte*) carrier);
            return drops;
        }
    )SRC", LIBK_KDI_DIR, LIBK_LIB_DIR);
    REQUIRE(jit);
    auto callback = jit->lookup_symbol<int (*)()>("testCallback");
    auto promise = jit->lookup_symbol<int (*)()>("testPromise");
    REQUIRE(callback);
    REQUIRE(promise);
    NativeStderrCapture capture;
    const int callbackDrops = callback();
    const int promiseDrops = promise();
    const auto output = capture.finish();
    REQUIRE(callbackDrops == 1);
    REQUIRE(promiseDrops == 1);
    const std::string diagnostics[] = {
        "origin=io.callback.drop code=918", "origin=io.promise.drop code=919"
    };
    for (const auto& diagnostic : diagnostics) {
        const auto first = output.find(diagnostic);
        REQUIRE(first != std::string::npos);
        REQUIRE(output.find(diagnostic, first + diagnostic.size()) == std::string::npos);
    }
}
