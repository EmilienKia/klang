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

/** Linux proactor lifecycle. Reinitialization and post-fork use are rejected. */
public class IoRuntime {
public:
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

/**
 * IoOperation — handle to an active or completed asynchronous I/O operation.
 *
 * Wraps an atomically refcounted native operation handle. Destroying this K
 * wrapper only detaches the observer and does not cancel the underlying operation.
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

    /**
     * Request cancellation of this operation.
     *
     * @return true if the cancellation was accepted or cooperative interruption was requested.
     */
    cancel() : bool {
        if (_native == null) return false;
        return __k_io_op_cancel(_native) != 0;
    }

    /**
     * Wait for this operation to reach quiescent state.
     *
     * @param timeout Maximum duration to wait.
     * @return 0 on success.
     */
    waitQuiescent(timeout: Duration&) : int {
        if (_native == null) return 0;
        nanos : long = timeout.toNanos();
        if (nanos < 0L) {
            nanos = 0L;
        }
        return __k_io_op_wait_quiescent(_native, nanos);
    }

    /**
     * Wait indefinitely for this operation to reach quiescent state.
     *
     * @return 0 on success.
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
