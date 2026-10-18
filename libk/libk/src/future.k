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
@ffi::Extern("C") private __k_future_cancel(native: NativeFuture*) : bool;
@ffi::Extern("C") private __k_future_wait_quiescent(native: NativeFuture*, timeout_nanos: long) : int;

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
/** An inherited Future cannot be waited on after fork. */
public const FUTURE_WAIT_UNSUPPORTED : int = 3;

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
     * Destroy the failure cause it owns. The compiler then destroys the
     * default-constructed value, even if no result was published.
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
 * Read handle for a value, failure, or cancellation produced asynchronously.
 *
 * A Future is obtained from a Promise<T> and observes the same shared state.
 * It never produces the value itself; it only waits for it and reports the
 * outcome.
 *
 * ### Terminal outcomes
 *
 * A shared state starts PENDING and can publish one terminal outcome:
 * - SUCCESS: get() returns a copy of the value.
 * - FAILED: get() throws ExecutionException with the stored cause, if any.
 * - CANCELLED: get() throws CancellationException.
 *
 * isDone() covers all three outcomes, not just success. Reading a result does
 * not consume it; independent handles can observe the same terminal outcome.
 *
 * ### Cancellation is not necessarily completion
 *
 * cancel() requests cancellation through the producer's hook when present.
 * A true result means the request was accepted, not that work has stopped or
 * isDone() must already be true. Deferred I/O producers may finish draining
 * before publishing, and may report InterruptedIOException rather than the
 * CANCELLED state. Inspect the final outcome through get() and state queries.
 *
 * Even terminal publication need not mean an underlying I/O operation is
 * quiescent. waitQuiescent() is a separate producer-defined drain operation;
 * without a registered hook it succeeds immediately. It is not a replacement
 * for get(), and does not itself produce a value.
 *
 * ### Waiting and lifetime
 *
 * get() and get(Duration) are interruptible. A thread blocked in
 * either of them and interrupted through Thread.interrupt() throws
 * ThreadInterruptionException. Completion always wins over interruption, so a
 * value published before the interrupt request is never lost. A timeout or
 * ThreadInterruptionException ends only this wait: neither cancels the
 * producer, consumes the result, nor clears the thread's interrupted flag.
 *
 * A Future must not be copied by assignment; use share() to obtain an
 * additional independent handle on the same result. Future and Promise
 * handles retain the shared state; destroying a handle is not cancellation.
 * A Future may outlive its Promise, including remaining pending forever if
 * that Promise was destroyed without publishing.
 *
 * Thread safety: all methods are safe to call concurrently from several
 * threads on distinct handles. A single handle must not be used concurrently.
 * Sharing the result does not synchronise unrelated objects reachable from T.
 *
 * ### Example: publish once and read through independent handles
 *
 * This example publishes inline for clarity; an asynchronous producer must
 * keep its Promise alive until it finishes using it.
 *
 * ```k
 * module future_example;
 *
 * main() : int throws(Throwable) {
 *     producer : Promise<int>;
 *     first : Future<int>! = producer.future();
 *     second : Future<int>! = first->share();
 *     producer.trySuccess(42);
 *     if (producer.trySuccess(7)) {
 *         return 1;
 *     }
 *     return first->get() - second->get();
 * }
 * ```
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
                try {
                    delete o;
                } catch (e: Throwable&) {
                    __k_future_destroy(_native);
                    _native = null;
                    _box = null;
                    throw;
                }
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
     * Invokes the producer's cancellation hook if registered. If accepted,
     * ordinary futures release waiters with CancellationException. Composite
     * I/O producers defer publication until kernel drain and may instead fail
     * with InterruptedIOException carrying the final partial byte count.
     *
     * @return true when cancellation was accepted, not necessarily published.
     *         False when already terminal, already requested or rejected.
     */
    cancel() : bool {
        return __k_future_cancel(_native);
    }

    /**
     * Wait for quiescence on the underlying asynchronous operation.
     *
     * This waits through the producer's quiescence hook, if supplied. It does
     * not wait for ordinary Promise publication when no hook is installed.
     *
     * @param timeout Maximum duration to wait.
     * @return 0 on success, or non-zero error/timeout code.
     */
    waitQuiescent(timeout: Duration&) : int {
        nanos : long = timeout.toNanos();
        if (nanos < 0L) {
            nanos = 0L;
        }
        return __k_future_wait_quiescent(_native, nanos);
    }

    /**
     * Wait indefinitely for quiescence on the underlying asynchronous operation.
     *
     * Without a producer hook this returns immediately, even while pending.
     *
     * @return 0 on success.
     */
    waitQuiescent() : int {
        return __k_future_wait_quiescent(_native, -1L);
    }

    /**
     * Return the native handle pointer.
     */
    const native() : NativeFuture* {
        return _native;
    }

    /**
     * Wait indefinitely for the result and return it.
     *
     * @return The produced value.
     * @throws ThreadInterruptionException The calling thread was interrupted
     *         while waiting.
     * @throws CancellationException The future was cancelled.
     * @throws ExecutionException The computation terminated with a throwable;
     *         the original throwable is the cause when available. A native
     *         I/O publisher unable to allocate a cause diagnoses the failure
     *         and publishes FAILED with no cause instead of leaving it pending.
     * @throws IllegalStateException Waiting from the I/O completion thread is
     *         forbidden because it would prevent completion progress.
     */
    get() : T throws(ThreadInterruptionException, CancellationException, ExecutionException, IllegalStateException) {
        r : int = __k_future_wait(_native, -1L);
        if (r == FUTURE_WAIT_UNSUPPORTED) throw IllegalStateException();
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
     *         the original throwable is the cause when available.
     * @throws IllegalStateException Waiting from the I/O completion thread is
     *         forbidden because it would prevent completion progress.
     */
    get(timeout: Duration&) : T throws(ThreadInterruptionException, TimeoutException, CancellationException, ExecutionException, IllegalStateException) {
        nanos : long = timeout.toNanos();
        if (nanos < 0L) {
            nanos = 0L;
        }
        r : int = __k_future_wait(_native, nanos);
        if (r == FUTURE_WAIT_UNSUPPORTED) throw IllegalStateException();
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
 * Producer handle that publishes one terminal outcome to its Futures.
 *
 * A Promise creates a shared state, hands out Future<T> handles observing it,
 * and publishes exactly one terminal outcome: a value, a failure or a
 * cancellation. Every publication attempt after the first one is rejected and
 * reported by a `false` return value, so competing producers never corrupt the
 * result.
 *
 * ### Producer responsibilities
 *
 * Keep this object alive until all producer access has ended. Do not copy it
 * by assignment to manufacture another producer handle. future() can be
 * called repeatedly to create independently owned readers of the same state.
 * Retaining the state through a Future does not retain this Promise object.
 *
 * Destruction releases a handle; it does not automatically cancel or publish
 * a broken-promise failure. If the last producer disappears while still
 * pending, surviving Futures can wait forever. Arrange a terminal outcome on
 * every producer exit path that is meant to release readers.
 *
 * tryCancel() directly publishes CANCELLED for this Promise's result; it does
 * not interrupt a worker thread. Future.cancel() is the consumer-facing
 * request path and can involve a producer hook for runtime-backed operations.
 * Neither API forcibly stops arbitrary producer code.
 *
 * Publication serialises the shared result, not unrelated application data.
 * Callers must still manage producer-object lifetime and any shared payload
 * objects themselves. See Future for a complete Promise/Future example.
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
                try {
                    delete o;
                } catch (e: Throwable&) {
                    __k_future_destroy(_native);
                    _native = null;
                    _box = null;
                    throw;
                }
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
        try {
            _box->_value = value;
        } catch (e: Throwable&) {
            // A throwing payload assignment must not deadlock failure publication.
            __k_future_unlock(_native);
            throw;
        }
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

    /**
     * Return the native handle pointer.
     */
    const native() : NativeFuture* {
        return _native;
    }
}
