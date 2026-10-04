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

@ffi::Extern("C") private __k_io_result_errno(result: byte*) : int;
@ffi::Extern("C") private __k_io_result_kind(result: byte*) : int;
@ffi::Extern("C") private __k_io_result_bytes(result: byte*) : long;
@ffi::Extern("C") private __k_io_result_value(result: byte*) : long;
@ffi::Extern("C") private __k_io_result_claim(result: byte*) : void;

/**
 * High-level error classification for asynchronous I/O operations.
 */
public enum IoErrorKind {
    System;
    Cancelled;
    Closed;
    Timeout;
    EndOfStream;
    Unsupported;
    ResourceExhausted;
    ExecutorRejected;
    InvalidArgument;
    Interrupted;
}

/**
 * IoError — captures an I/O failure cause, platform errno, and transferred bytes.
 */
public struct IoError {
    @ffi::Extern("C") private static __k_io_errno_kind(error: int) : int;
    _kind             : IoErrorKind;
    _errno            : int;
    _bytesTransferred : long;

    IoError() {
        _kind = IoErrorKind::System;
        _errno = 0;
        _bytesTransferred = 0L;
    }

    IoError(kind: IoErrorKind) {
        _kind = kind;
        _errno = 0;
        _bytesTransferred = 0L;
    }

    IoError(kind: IoErrorKind, err: int) {
        _kind = kind;
        _errno = err;
        _bytesTransferred = 0L;
    }

    IoError(kind: IoErrorKind, err: int, bytesTransferred: long) {
        _kind = kind;
        _errno = err;
        _bytesTransferred = bytesTransferred;
    }

    static fromErrno(error: int, bytesTransferred: long) : IoError {
        return IoError((IoErrorKind) __k_io_errno_kind(error), error, bytesTransferred);
    }

    const kind() : IoErrorKind {
        return _kind;
    }

    const getErrno() : int {
        return _errno;
    }

    const bytesTransferred() : long {
        return _bytesTransferred;
    }

    /** Failure cause for Future publication, using the same taxonomy as raiseSync. */
    const failure() : Throwable! {
        if (_kind == IoErrorKind::System && (_errno == 2 || _errno == 20))
            return new FileNotFoundException(_errno);
        if (_kind == IoErrorKind::Closed) return new ClosedChannelException(_errno);
        if (_kind == IoErrorKind::Timeout) return new TimeoutException();
        if (_kind == IoErrorKind::EndOfStream) return new EndOfStreamException(_bytesTransferred);
        if (_kind == IoErrorKind::Cancelled) {
            if (_bytesTransferred > 0L) return new InterruptedIOException(_bytesTransferred);
            return new CancellationException();
        }
        if (_kind == IoErrorKind::Interrupted) {
            if (_bytesTransferred > 0L) return new InterruptedIOException(_bytesTransferred);
            return new ThreadInterruptionException();
        }
        if (_kind == IoErrorKind::ExecutorRejected) return new RejectedExecutionException();
        return new IOException(_errno);
    }

    /** Shared taxonomy for the synchronous wrapper, not a second errno table. */
    raiseSync() {
        if (_kind == IoErrorKind::System && (_errno == 2 || _errno == 20))
            throw FileNotFoundException(_errno);
        if (_kind == IoErrorKind::Closed) throw ClosedChannelException(_errno);
        if (_kind == IoErrorKind::Timeout) throw TimeoutException();
        if (_kind == IoErrorKind::EndOfStream) throw EndOfStreamException(_bytesTransferred);
        if (_kind == IoErrorKind::Cancelled) {
            if (_bytesTransferred > 0L) throw InterruptedIOException(_bytesTransferred);
            throw CancellationException();
        }
        if (_kind == IoErrorKind::Interrupted) {
            if (_bytesTransferred > 0L) throw InterruptedIOException(_bytesTransferred);
            throw ThreadInterruptionException();
        }
        if (_kind == IoErrorKind::ExecutorRejected) throw RejectedExecutionException();
        throw IOException(_errno);
    }
}

/**
 * IoDone — sentinel struct representing successful completion of a void I/O operation.
 */
public struct IoDone {
}

/**
 * IoResult<T> — Expected result of an I/O operation (value or IoError).
 */
public template<typename T>
alias IoResult : Expected<T, IoError>;

/**
 * IoCompletion<T> — completion callback callable receiving IoResult<T>.
 */
public template<typename T>
alias IoCompletion : !(IoResult<T>);

/**
 * The transport has no implicit closure-layout or destructor assumptions.
 * Decode failures deliver System with errno 0 (not a platform error) and are
 * diagnosed with the original Throwable code. Callback/cleanup failures are
 * diagnosed and contained so the native operation can finish normally.
 * An optional internal claim hook must not throw: resource decoders initially
 * produce non-owning values, and commit only after the result was copied safely.
 */
template<typename T>
struct IoCallbackCarrier {
    _completion : IoCompletion<T>;
    _decode : *(long):T;
    _claim : *(T&, byte*);
    IoCallbackCarrier(completion: IoCompletion<T>, decode: *(long):T) {
        _completion = completion;
        _decode = decode;
        _claim = null;
    }
    IoCallbackCarrier(completion: IoCompletion<T>, decode: *(long):T,
        claim: *(T&, byte*)) {
        _completion = completion;
        _decode = decode;
        _claim = claim;
    }
    ~IoCallbackCarrier() { _completion = null; }
    static invoke(context: byte*, nativeResult: byte*) {
        carrier : IoCallbackCarrier<T>* = (IoCallbackCarrier<T>*) context;
        try {
            result : IoResult<T>;
            err : int = __k_io_result_errno(nativeResult);
            kind : IoErrorKind = (IoErrorKind) __k_io_result_kind(nativeResult);
            try {
                if (err == 0 && kind == IoErrorKind::System) {
                    if (carrier->_decode == null) throw IllegalStateException();
                    value : T = carrier->_decode(__k_io_result_value(nativeResult));
                    result.setResult(value);
                    if (carrier->_claim != null) carrier->_claim(value, nativeResult);
                    else __k_io_result_claim(nativeResult);
                } else {
                    error : IoError = IoError(kind, err, __k_io_result_bytes(nativeResult));
                    result.setError(error);
                }
            } catch (e: Throwable&) {
                __k_executor_report_failure(3, e.getCode());
                error : IoError = IoError(IoErrorKind::System, 0,
                    __k_io_result_bytes(nativeResult));
                result.setError(error);
            }
            if (carrier->_completion != null) carrier->_completion(result);
        } catch (e: Throwable&) {
            __k_executor_report_failure(4, e.getCode());
        }
    }
    static drop(context: byte*) {
        carrier : IoCallbackCarrier<T>! = (IoCallbackCarrier<T>!) context;
        try {
            delete carrier;
        } catch (e: Throwable&) {
            __k_executor_report_failure(5, e.getCode());
        }
    }
}

/**
 * Internal publisher: it is independent of the user's Executor.
 * A decode/publication exception fails the Future with its Throwable code.
 * If allocating that cause also fails, native FAILED publication is allocation
 * free: get() throws ExecutionException without a cause, and both failures are
 * diagnosed on stderr. No failure is represented as cancellation or success.
 * A resource claim adopts the decoded value before trySuccess can wake waiters.
 * Rejected or throwing publication destroys that owning value; the native
 * result stays claimed so operation rollback cannot close the resource twice.
 */
template<typename T>
struct IoPromiseCarrier {
    _promise : Promise<T>;
    _decode : *(long):T;
    _claim : *(T&, byte*);
    IoPromiseCarrier(decode: *(long):T) { _decode = decode; _claim = null; }
    IoPromiseCarrier(decode: *(long):T, claim: *(T&, byte*)) {
        _decode = decode;
        _claim = claim;
    }
    future() : Future<T>! { return _promise.future(); }
    native() : NativeFuture* { return _promise.native(); }
    static invoke(context: byte*, nativeResult: byte*) {
        carrier : IoPromiseCarrier<T>* = (IoPromiseCarrier<T>*) context;
        try {
            err : int = __k_io_result_errno(nativeResult);
            kind : IoErrorKind = (IoErrorKind) __k_io_result_kind(nativeResult);
            transferred : long = __k_io_result_bytes(nativeResult);
            if (err == 0 && kind == IoErrorKind::System) {
                if (carrier->_decode == null) throw IllegalStateException();
                value : T = carrier->_decode(__k_io_result_value(nativeResult));
                if (carrier->_claim != null) carrier->_claim(value, nativeResult);
                if (carrier->_promise.trySuccess(value)) {
                    if (carrier->_claim == null) __k_io_result_claim(nativeResult);
                }
            } else if (kind == IoErrorKind::Cancelled && transferred == 0L) {
                carrier->_promise.tryCancel();
            } else {
                error : IoError = IoError(kind, err, transferred);
                failure : Throwable! = error.failure();
                carrier->_promise.tryFailure(failure);
            }
        } catch (e: Throwable&) {
            __k_executor_report_failure(6, e.getCode());
            try {
                failure : Throwable! = new Exception(e.getCode());
                carrier->_promise.tryFailure(failure);
            } catch (publicationError: Throwable&) {
                __k_executor_report_failure(6, publicationError.getCode());
                __k_future_complete(carrier->_promise.native(), FUTURE_FAILED);
            }
        }
    }
    static drop(context: byte*) {
        carrier : IoPromiseCarrier<T>! = (IoPromiseCarrier<T>!) context;
        try {
            delete carrier;
        } catch (e: Throwable&) {
            __k_executor_report_failure(7, e.getCode());
        }
    }
}

} // namespace io
