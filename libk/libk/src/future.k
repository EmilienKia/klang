/*
 * K Language standard library — Future / Promise
 *
 * Copyright 2023-2026 Emilien Kia
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

/**
 * Opaque handle to the C-level shared completion state of a Future/Promise
 * pair. Users never touch this struct; it is manipulated only through the
 * Promise<T> and Future<T> methods.
 */
public struct NativeFuture {
}

// ── FFI declarations ─────────────────────────────────────────────────────────
@ffi::Extern("C") __k_future_create() : NativeFuture*;
@ffi::Extern("C") __k_future_destroy(native: NativeFuture*) : void;
@ffi::Extern("C") __k_future_retain(native: NativeFuture*) : void;
@ffi::Extern("C") __k_future_release(native: NativeFuture*) : int;
@ffi::Extern("C") __k_future_lock(native: NativeFuture*) : void;
@ffi::Extern("C") __k_future_unlock(native: NativeFuture*) : void;
@ffi::Extern("C") __k_future_state(native: const NativeFuture*) : int;
@ffi::Extern("C") __k_future_is_done(native: const NativeFuture*) : bool;
@ffi::Extern("C") __k_future_complete(native: NativeFuture*, completion: int) : bool;
@ffi::Extern("C") __k_future_wait(native: NativeFuture*, timeout_nanos: long) : int;

// ── Completion states (mirror KFutureCompletion in future_state.h) ───────────

/** Completion state: the result has not been produced yet. */
public const FUTURE_PENDING : int = 0;
/** Completion state: the computation produced a value. */
public const FUTURE_SUCCESS : int = 1;
/** Completion state: the computation terminated with a throwable. */
public const FUTURE_FAILED : int = 2;
/** Completion state: the computation was cancelled before completing. */
public const FUTURE_CANCELLED : int = 3;

// ── Wait outcomes (mirror KFutureWaitResult in future_state.h) ───────────────

/** Wait outcome: the future reached a terminal state. */
public const FUTURE_WAIT_DONE : int = 0;
/** Wait outcome: the waiting thread was interrupted. */
public const FUTURE_WAIT_INTERRUPTED : int = 1;
/** Wait outcome: the deadline expired while the future was still pending. */
public const FUTURE_WAIT_TIMEOUT : int = 2;

// ═════════════════════════════════════════════════════════════════════════════
// FutureBox — payload shared by a Promise and its Futures.
// ═════════════════════════════════════════════════════════════════════════════

/**
 * FutureBox<T> — internal payload of a Future/Promise group.
 *
 * Holds the produced value and, when the computation failed, the throwable
 * that caused the failure. Exactly one box exists per group; it is allocated
 * by the Promise and destroyed by whichever handle drops the shared reference
 * count to zero.
 *
 * This struct is an implementation detail: it is never manipulated directly
 * by user code.
 */
template<typename T>
public final struct FutureBox {
    /** The produced value; only meaningful once the state is FUTURE_SUCCESS. */
    _value : T;
    /** The failure cause; only meaningful once the state is FUTURE_FAILED. */
    _error : Throwable!;

    /** Create an empty box. */
    FutureBox() {
        _error = null;
    }

    /**
     * Destroy the box and the failure cause it owns.
     * Explicit, because the compiler does not auto-delete owner fields of a
     * template struct.
     */
    ~FutureBox() {
        delete _error;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Future — read side of an asynchronous result.
// ═════════════════════════════════════════════════════════════════════════════

/**
 * Future<T> — the read side of a value produced asynchronously.
 *
 * A Future is obtained from a Promise<T> and observes the same shared state.
 * It never produces the value itself; it only waits for it and reports the
 * outcome.
 *
 * Usage:
 *   p : Promise<int>;
 *   f : Future<int>! = p.future();
 *   // ... another thread ...
 *   p.trySuccess(42);
 *   // ... consumer ...
 *   v : int = f->get();
 *
 * Blocking: get() and get(Duration) are interruptible. A thread blocked in
 * either of them and interrupted through Thread.interrupt() throws
 * ThreadInterruptionException. Completion always wins over interruption, so a
 * value published before the interrupt request is never lost.
 *
 * Copying: a Future must not be copied by assignment; use share() to obtain an
 * additional independent handle on the same result.
 *
 * Thread safety: all methods are safe to call concurrently from several
 * threads on distinct handles. A single handle must not be used concurrently.
 *
 * @param T The produced value type. It must be default-constructible and
 *          copyable.
 */
template<typename T>
public final struct Future {
    private:
    _native : NativeFuture*;
    _box    : FutureBox<T>*;

    public:
    /**
     * Create a detached, always-pending future.
     * Mostly useful as a placeholder; prefer Promise<T>.future().
     */
    Future() {
        _native = __k_future_create();
        _box = new FutureBox<T>();
    }

    /**
     * Attach a new handle to an existing shared state.
     *
     * Internal constructor used by Promise<T>.future() and Future<T>.share().
     * The caller must already have taken the reference on `native`.
     *
     * @param native The shared completion state.
     * @param box    The shared payload.
     */
    Future(native: NativeFuture*, box: FutureBox<T>*) {
        _native = native;
        _box = box;
    }

    /** Drop this handle; releases the shared state when it is the last one. */
    ~Future() {
        if (_native != null) {
            if (__k_future_release(_native) == 0) {
                o : FutureBox<T>! = (FutureBox<T>!) _box;
                delete o;
                __k_future_destroy(_native);
            }
            _native = null;
            _box = null;
        }
    }

    /**
     * Return an additional handle on the same result.
     * @return A new heap-allocated Future observing the same shared state.
     */
    share() : Future<T>! {
        __k_future_retain(_native);
        return new Future<T>(_native, _box);
    }

    /** @return true once the result, a failure or a cancellation is available. */
    const isDone() : bool {
        return __k_future_is_done(_native);
    }

    /** @return true when the computation completed with a value. */
    const isSuccess() : bool {
        return __k_future_state(_native) == FUTURE_SUCCESS;
    }

    /** @return true when the computation terminated with a throwable. */
    const isFailed() : bool {
        return __k_future_state(_native) == FUTURE_FAILED;
    }

    /** @return true when the future was cancelled before completing. */
    const isCancelled() : bool {
        return __k_future_state(_native) == FUTURE_CANCELLED;
    }

    /**
     * Cancel the future if it has not completed yet.
     *
     * Cancellation only marks the shared state; it does not interrupt the
     * thread computing the value. Every waiter blocked in get() is released
     * and throws CancellationException.
     *
     * @return true when this call performed the cancellation, false when the
     *         future had already reached a terminal state.
     */
    cancel() : bool {
        return __k_future_complete(_native, FUTURE_CANCELLED);
    }

    /**
     * Wait indefinitely for the result and return it.
     *
     * @return The produced value.
     * @throws ThreadInterruptionException The calling thread was interrupted
     *         while waiting.
     * @throws CancellationException The future was cancelled.
     * @throws ExecutionException The computation terminated with a throwable;
     *         the original throwable is the cause.
     */
    get() : T throws(ThreadInterruptionException, CancellationException, ExecutionException) {
        r : int = __k_future_wait(_native, -1L);
        if (r == FUTURE_WAIT_INTERRUPTED) {
            throw ThreadInterruptionException();
        }
        s : int = __k_future_state(_native);
        if (s == FUTURE_CANCELLED) {
            throw CancellationException();
        }
        if (s == FUTURE_FAILED) {
            throw ExecutionException(_box->_error);
        }
        return _box->_value;
    }

    /**
     * Wait at most `timeout` for the result and return it.
     *
     * @param timeout Maximum time to block. A non-positive duration polls the
     *                state once without blocking.
     * @return The produced value.
     * @throws ThreadInterruptionException The calling thread was interrupted
     *         while waiting.
     * @throws TimeoutException The deadline expired before completion.
     * @throws CancellationException The future was cancelled.
     * @throws ExecutionException The computation terminated with a throwable;
     *         the original throwable is the cause.
     */
    get(timeout: Duration&) : T throws(ThreadInterruptionException, TimeoutException, CancellationException, ExecutionException) {
        nanos : long = timeout.toNanos();
        if (nanos < 0L) {
            nanos = 0L;
        }
        r : int = __k_future_wait(_native, nanos);
        if (r == FUTURE_WAIT_INTERRUPTED) {
            throw ThreadInterruptionException();
        }
        if (r == FUTURE_WAIT_TIMEOUT) {
            throw TimeoutException();
        }
        s : int = __k_future_state(_native);
        if (s == FUTURE_CANCELLED) {
            throw CancellationException();
        }
        if (s == FUTURE_FAILED) {
            throw ExecutionException(_box->_error);
        }
        return _box->_value;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Promise — write side of an asynchronous result.
// ═════════════════════════════════════════════════════════════════════════════

/**
 * Promise<T> — the write side of a value produced asynchronously.
 *
 * A Promise creates a shared state, hands out Future<T> handles observing it,
 * and publishes exactly one terminal outcome: a value, a failure or a
 * cancellation. Every publication attempt after the first one is rejected and
 * reported by a `false` return value, so competing producers never corrupt the
 * result.
 *
 * Usage:
 *   p : Promise<int>;
 *   f : Future<int>! = p.future();
 *   p.trySuccess(42);
 *
 * Thread safety: all methods are safe to call concurrently from several
 * threads on distinct handles. A single handle must not be used concurrently.
 *
 * @param T The produced value type. It must be default-constructible and
 *          copyable.
 */
template<typename T>
public final struct Promise {
    private:
    _native : NativeFuture*;
    _box    : FutureBox<T>*;

    public:
    /** Create a pending promise with its own shared state. */
    Promise() {
        _native = __k_future_create();
        _box = new FutureBox<T>();
    }

    /** Drop this handle; releases the shared state when it is the last one. */
    ~Promise() {
        if (_native != null) {
            if (__k_future_release(_native) == 0) {
                o : FutureBox<T>! = (FutureBox<T>!) _box;
                delete o;
                __k_future_destroy(_native);
            }
            _native = null;
            _box = null;
        }
    }

    /**
     * Return a new Future observing this promise's result.
     * May be called any number of times; every returned handle is independent.
     *
     * @return A heap-allocated Future handle owned by the caller.
     */
    future() : Future<T>! {
        __k_future_retain(_native);
        return new Future<T>(_native, _box);
    }

    /** @return true once this promise reached a terminal state. */
    const isDone() : bool {
        return __k_future_is_done(_native);
    }

    /**
     * Publish a value and wake every waiter.
     *
     * @param value The produced value.
     * @return true when this call published the outcome, false when the state
     *         was already terminal (the value is then discarded).
     */
    trySuccess(value: T) : bool {
        __k_future_lock(_native);
        if (__k_future_is_done(_native)) {
            __k_future_unlock(_native);
            return false;
        }
        _box->_value = value;
        done : bool = __k_future_complete(_native, FUTURE_SUCCESS);
        __k_future_unlock(_native);
        return done;
    }

    /**
     * Publish a failure and wake every waiter.
     *
     * @param error The throwable that terminated the computation. Ownership is
     *              transferred to the shared state; waiters receive it as the
     *              cause of an ExecutionException.
     * @return true when this call published the outcome, false when the state
     *         was already terminal (the throwable is then destroyed).
     */
    tryFailure(error: Throwable!) : bool {
        __k_future_lock(_native);
        if (__k_future_is_done(_native)) {
            __k_future_unlock(_native);
            delete error;
            return false;
        }
        _box->_error = error;
        done : bool = __k_future_complete(_native, FUTURE_FAILED);
        __k_future_unlock(_native);
        return done;
    }

    /**
     * Cancel the result if it has not been published yet. Every waiter blocked
     * in Future.get() is released and throws CancellationException.
     *
     * @return true when this call performed the cancellation.
     */
    tryCancel() : bool {
        return __k_future_complete(_native, FUTURE_CANCELLED);
    }
}
