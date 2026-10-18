/*
 * K Language standard library — base module
 *
 * Copyright 2026 Emilien Kia
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

module k;

namespace io {

/**
 * Public observable stages for an asynchronous I/O operation.
 * Derived from the underlying 2D state machine (I/O phase and callback phase).
 */
public enum IoOperationStage {
    Pending;
    Cancelling;
    CallbackQueued;
    CallbackRunning;
    Completed;
    Cancelled;
    InterruptionRequested;
    Interrupted;
    CallbackRejected;
}

public struct NativeIoOperation {
}
public struct NativeIoHandle {}

@ffi::Extern("C") private __k_io_op_retain(op: NativeIoOperation*) : void;
@ffi::Extern("C") private __k_io_op_release(op: NativeIoOperation*) : void;
@ffi::Extern("C") private __k_io_op_cancel(op: NativeIoOperation*) : int;
@ffi::Extern("C") private __k_io_op_stage(op: const NativeIoOperation*) : int;
@ffi::Extern("C") private __k_io_op_is_io_done(op: const NativeIoOperation*) : int;
@ffi::Extern("C") private __k_io_op_is_callback_running(op: const NativeIoOperation*) : int;
@ffi::Extern("C") private __k_io_op_is_done(op: const NativeIoOperation*) : int;
@ffi::Extern("C") private __k_io_op_is_cancelled(op: const NativeIoOperation*) : int;
@ffi::Extern("C") private __k_io_op_is_interruption_requested(op: const NativeIoOperation*) : int;
@ffi::Extern("C") private __k_io_op_bytes_transferred(op: const NativeIoOperation*) : long;
@ffi::Extern("C") private __k_io_op_error_kind(op: const NativeIoOperation*) : int;
@ffi::Extern("C") private __k_io_op_error_code(op: const NativeIoOperation*) : int;
@ffi::Extern("C") private __k_io_op_wait_quiescent(op: NativeIoOperation*, timeout_nanos: long) : int;
@ffi::Extern("C") private __k_io_failed_operation(context: byte*, invoke: *(byte*, byte*),
    drop: *(byte*), lease: NativeExecutorLease*, error: int) : NativeIoOperation*;
@ffi::Extern("C") private __k_io_op_set_continuation(op: NativeIoOperation*, context: byte*,
    invoke: *(byte*, byte*), drop: *(byte*), future: NativeFuture*) : int;
@ffi::Extern("C") private __k_io_prepare_request(opcode: int, handle: NativeIoHandle*,
    buffer: NativeByteBuffer*, length: int, offset: long, flags: int,
    context: byte*, invoke: *(byte*, byte*), drop: *(byte*),
    lease: NativeExecutorLease*) : NativeIoOperation*;
@ffi::Extern("C") private __k_io_submit_prepared(op: NativeIoOperation*, timeout: long) : int;
@ffi::Extern("C") private __k_io_op_set_request_error(op: NativeIoOperation*, error: int) : int;
@ffi::Extern("C") private __k_io_runtime_init() : int;
@ffi::Extern("C") private __k_io_runtime_request_shutdown() : int;

/** Process-wide Linux proactor initialization and permanent shutdown.
 *
 * ## Availability
 *
 * Channel operations initialize the runtime on demand; initialize can be used
 * to check availability explicitly. The backend requires Linux io_uring and
 * the required kernel transport opcodes. Unavailable kernel support is an
 * error, not a request to fall back to blocking file or socket transfers.
 * Inherited post-fork use is rejected.
 *
 * ## Completed stop
 *
 * Shutdown is an idempotent, completed process-wide barrier, not a pause or a
 * channel close. It rejects new work, cancels and drains accepted I/O and
 * resolver work, suppresses queued I/O callbacks, and finishes their capture
 * cleanup. Running callbacks receive a cooperative interruption request and
 * must return before shutdown can finish. This includes callbacks selected
 * onto a custom executor; a queued no-op shell may remain there after cleanup.
 *
 * Stop is permanent even when requested before initialization. Neither later
 * initialize calls nor new channel operations restart the runtime. Do not call
 * shutdown from runtime workers, I/O callbacks or their cleanup: it would wait
 * for itself and raises IllegalStateException instead. The Application exit
 * barrier performs runtime shutdown before Application fields are destroyed;
 * it does not extend the lifetime of ordinary local variables.
 */
public class IoRuntime {
public:
    /** Initialize or observe the existing runtime; raise IOException on failure. */
    static initialize() {
        rc : int = __k_io_runtime_init();
        if (rc != 0) throw IOException(-rc);
    }
    /** Permanent completed stop, including before initialization. Not callable
     * from runtime workers, I/O callbacks (even custom executors), or their cleanup. */
    static shutdown() {
        rc : int = __k_io_runtime_request_shutdown();
        if (rc == -35) throw IllegalStateException();
        if (rc != 0) throw IOException(-rc);
    }
}

/** Owning observer of an active or completed asynchronous I/O operation.
 *
 * Wraps an atomically refcounted native operation handle. Destroying this K
 * wrapper only detaches the observer and does not cancel the underlying operation.
 * share creates another observer of the same operation, not another submission.
 *
 * ## I/O completion versus user delivery
 *
 * Native completion updates byte counts and buffer/channel positions and
 * releases I/O leases before publishing the Future or dispatching a callback.
 * Future publication does not require the selected callback executor to run.
 * Callback forms instead deliver an IoResult on that executor, which can
 * reject delivery. A returned observer alone is not evidence of I/O success.
 *
 * isIoDone describes native I/O, while isCallbackRunning and stage distinguish
 * queued/running user delivery. isDone describes terminal I/O/callback state;
 * it is not a join on callback capture destruction. waitQuiescent waits for
 * native drain, not queued callback execution or running callback completion.
 * Keep K objects borrowed by callback code or capture cleanup alive until that
 * delivery/cleanup has finished, even if native drain already succeeded.
 *
 * ## Cancellation and progress
 *
 * cancel requests native cancellation and suppresses callbacks that have not
 * started. If a callback is already running, it requests cooperative
 * interruption through ExecutionContext instead of preempting its thread.
 * Accepted cancellation can race with completed bytes and never rolls back
 * buffer or file-position progress. bytesTransferred reports that progress;
 * fully operations accumulate it across children, including on failure.
 *
 * A true cancel result is not quiescence. Drain before reusing a leased buffer;
 * a nonzero timed drain result is not permission to access it. Primitive Future
 * cancellation can become observable before native drain, whereas fully
 * Futures defer their cancellation outcome until drain. An observation timeout
 * from Future.get likewise does not cancel I/O. Sync bridges request and drain
 * cancellation/interruption before returning control.
 *
 * Closing a channel rejects new work and drains that handle's I/O before
 * descriptor close; it does not join unrelated queued user callback cleanup.
 * Close operations themselves are not cancellable. Process-wide completed
 * user-delivery cleanup belongs to IoRuntime::shutdown, not waitQuiescent.
 */
public class IoOperation : public ExecutionContext {
private:
    _native : NativeIoOperation*;

public:
    IoOperation() : _native(null) {}

    IoOperation(native: NativeIoOperation*) {
        _native = native;
    }
    share() : IoOperation! {
        if (_native != null) __k_io_op_retain(_native);
        return new IoOperation(_native);
    }

    ~IoOperation() {
        if (_native != null) {
            __k_io_op_release(_native);
            _native = null;
        }
    }

    const native() : NativeIoOperation* {
        return _native;
    }

    const stage() : IoOperationStage {
        if (_native == null) {
            return IoOperationStage::Completed;
        }
        s : int = __k_io_op_stage(_native);
        if (s == 0) return IoOperationStage::Pending;
        if (s == 1) return IoOperationStage::Cancelling;
        if (s == 2) return IoOperationStage::CallbackQueued;
        if (s == 3) return IoOperationStage::CallbackRunning;
        if (s == 4) return IoOperationStage::Completed;
        if (s == 5) return IoOperationStage::Cancelled;
        if (s == 6) return IoOperationStage::InterruptionRequested;
        if (s == 7) return IoOperationStage::Interrupted;
        if (s == 8) return IoOperationStage::CallbackRejected;
        return IoOperationStage::Completed;
    }

    const isIoDone() : bool {
        if (_native == null) return true;
        return __k_io_op_is_io_done(_native) != 0;
    }

    const isCallbackRunning() : bool {
        if (_native == null) return false;
        return __k_io_op_is_callback_running(_native) != 0;
    }

    const isDone() : bool {
        if (_native == null) return true;
        return __k_io_op_is_done(_native) != 0;
    }

    const isCancelled() : bool {
        if (_native == null) return false;
        return __k_io_op_is_cancelled(_native) != 0;
    }

    const isInterruptionRequested() : bool {
        if (_native == null) return false;
        return __k_io_op_is_interruption_requested(_native) != 0;
    }

    override const isCancellationRequested() : bool {
        return isInterruptionRequested() || isCancelled();
    }

    const bytesTransferred() : long {
        if (_native == null) return 0L;
        return __k_io_op_bytes_transferred(_native);
    }

    const error() : IoError {
        if (_native == null) {
            return IoError(IoErrorKind::System, 0, 0L);
        }
        k : int = __k_io_op_error_kind(_native);
        kind : IoErrorKind = IoErrorKind::System;
        if (k == 0) kind = IoErrorKind::System;
        else if (k == 1) kind = IoErrorKind::Cancelled;
        else if (k == 2) kind = IoErrorKind::Closed;
        else if (k == 3) kind = IoErrorKind::Timeout;
        else if (k == 4) kind = IoErrorKind::EndOfStream;
        else if (k == 5) kind = IoErrorKind::Unsupported;
        else if (k == 6) kind = IoErrorKind::ResourceExhausted;
        else if (k == 7) kind = IoErrorKind::ExecutorRejected;
        else if (k == 8) kind = IoErrorKind::InvalidArgument;
        else if (k == 9) kind = IoErrorKind::Interrupted;

        errCode : int = __k_io_op_error_code(_native);
        transferred : long = __k_io_op_bytes_transferred(_native);
        return IoError(kind, errCode, transferred);
    }

    /** Request cancellation of this operation without waiting for drain.
     *
     * Before callback entry this suppresses delivery; during execution it only
     * requests cooperation. It does not wait for native drain or callback exit.
     *
     * @return true if the cancellation was accepted or cooperative interruption was requested.
     */
    cancel() : bool {
        if (_native == null) return false;
        return __k_io_op_cancel(_native) != 0;
    }

    /** Wait for native I/O to drain, without joining user callback execution.
     *
     * @param timeout Maximum duration to wait.
     * @return 0 after drain; nonzero on timeout or a forbidden wait.
     *
     * Negative durations are treated as zero. A failed wait does not release
     * the caller from buffer lifetime/access obligations. Waiting on the
     * native completion thread or through an inherited post-fork operation is
     * rejected.
     */
    waitQuiescent(timeout: Duration&) : int {
        if (_native == null) return 0;
        nanos : long = timeout.toNanos();
        if (nanos < 0L) {
            nanos = 0L;
        }
        return __k_io_op_wait_quiescent(_native, nanos);
    }

    /** Wait indefinitely for native drain, not callback execution or cleanup.
     *
     * @return 0 after drain; nonzero if the wait is forbidden.
     */
    waitQuiescent() : int {
        if (_native == null) return 0;
        return __k_io_op_wait_quiescent(_native, -1L);
    }

    /**
     * Return the IoOperation currently executing on the calling thread's TLS context.
     */
    static current() : IoOperation! {
        p : NativeIoOperation* = __k_io_execution_context_current();
        if (p == null) {
            return null;
        }
        __k_io_op_retain(p);
        return new IoOperation(p);
    }
}

/**
 * Temporary legacy-channel adapter. It does NOT perform synchronous I/O or
 * report synthetic success: the selected Executor receives Unsupported.
 */
template<typename T>
unsupportedOperation(completion: IoCompletion<T>, executor: ExecutorLease&) : IoOperation! {
    carrier : IoCallbackCarrier<T>* = new IoCallbackCarrier<T>(completion, null);
    invokeFn : *(byte*, byte*) = IoCallbackCarrier<T>::invoke;
    dropFn : *(byte*) = IoCallbackCarrier<T>::drop;
    op : NativeIoOperation* = __k_io_failed_operation((byte*) carrier, invokeFn, dropFn,
        executor.native(), 95);
    if (op == null) throw OutOfMemory();
    return new IoOperation(op);
}

/**
 * Install before native submission; the returned Future shares both hooks.
 * decode is an internal result translator, not a user callback: it must neither
 * block nor execute user-supplied code because publication runs on the reaper.
 */
template<typename T>
futureFor(operation: IoOperation&, decode: *(long):T) : Future<T>! {
    return futureFor<T>(operation, decode, null);
}

/** Resource decoders adopt ownership before publishing an observable payload. */
template<typename T>
futureFor(operation: IoOperation&, decode: *(long):T,
    claim: *(T&, byte*)) : Future<T>! {
    carrier : IoPromiseCarrier<T>* = new IoPromiseCarrier<T>(decode, claim);
    future : Future<T>! = carrier->future();
    nativeFuture : NativeFuture* = carrier->native();
    invokeFn : *(byte*, byte*) = IoPromiseCarrier<T>::invoke;
    dropFn : *(byte*) = IoPromiseCarrier<T>::drop;
    if (__k_io_op_set_continuation(operation.native(), (byte*) carrier,
        invokeFn, dropFn, nativeFuture) != 0) throw IllegalStateException();
    return future;
}

/**
 * Foundations used by migrated channels: prepare, attach futureFor, submit.
 * A null completion creates no user callback; Future publication needs no worker.
 * decode must be an internal exception-contained result translator.
 */
template<typename T>
prepareOperation(opcode: int, handle: NativeIoHandle*, buffer: NativeByteBuffer*,
    length: int, offset: long, flags: int, completion: IoCompletion<T>,
    executor: ExecutorLease&, decode: *(long):T) : IoOperation! {
    return prepareOperation<T>(opcode, handle, buffer, length, offset, flags,
        completion, executor, decode, null);
}

template<typename T>
prepareOperation(opcode: int, handle: NativeIoHandle*, buffer: NativeByteBuffer*,
    length: int, offset: long, flags: int, completion: IoCompletion<T>,
    executor: ExecutorLease&, decode: *(long):T, claim: *(T&, byte*)) : IoOperation! {
    carrier : IoCallbackCarrier<T>* = null;
    invokeFn : *(byte*, byte*) = null;
    dropFn : *(byte*) = null;
    if (completion != null) {
        carrier = new IoCallbackCarrier<T>(completion, decode, claim);
        invokeFn = IoCallbackCarrier<T>::invoke;
        dropFn = IoCallbackCarrier<T>::drop;
    }
    native : NativeIoOperation* = __k_io_prepare_request(opcode, handle, buffer,
        length, offset, flags, (byte*) carrier, invokeFn, dropFn, executor.native());
    if (native == null) throw OutOfMemory();
    return new IoOperation(native);
}
submitOperation(operation: IoOperation&, timeoutNanos: long) {
    __k_io_submit_prepared(operation.native(), timeoutNanos);
}
/** Store preparation failures without publishing before futureFor is attached. */
setOperationError(operation: IoOperation&, error: int) {
    __k_io_op_set_request_error(operation.native(), error);
}

checkIoSyncThread() {
    if (__k_io_is_reaper() != 0) throw IllegalStateException();
}

/** Cancellation and interruption propagate only after native kernel drain. */
template<typename T>
getIoSync(future: Future<T>&, operation: IoOperation&) : T {
    checkIoSyncThread();
    try {
        return future.get();
    } catch (e: ThreadInterruptionException&) {
        future.cancel();
        operation.cancel();
        if (operation.waitQuiescent() != 0) throw IllegalStateException();
        transferred : long = operation.bytesTransferred();
        if (transferred > 0L) throw InterruptedIOException(transferred);
        throw ThreadInterruptionException();
    } catch (e: CancellationException&) {
        operation.cancel();
        if (operation.waitQuiescent() != 0) throw IllegalStateException();
        error : IoError = IoError(IoErrorKind::Cancelled,
            operation.error().getErrno(), operation.bytesTransferred());
        error.raiseSync();
        throw CancellationException();
    } catch (e: ExecutionException&) {
        error : IoError = operation.error();
        // A decode/publication failure is not an OS error with errno 0.
        if (error.kind() == IoErrorKind::System && error.getErrno() == 0) throw;
        error.raiseSync();
        throw IOException();
    } catch (e: IllegalStateException&) {
        // A forbidden or inherited Future wait must not attempt another wait.
        throw IllegalStateException();
    }
}

} // namespace io
