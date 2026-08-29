/*
 * K Language standard library — Counting semaphore
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
 * Opaque handle to the C-level KSemaphore structure.
 */
public struct NativeSemaphore {
}

/**
 * Semaphore — a counting permit dispenser.
 *
 * A semaphore holds a number of permits. `acquire()` consumes one, blocking
 * while none is available; `release()` gives one back. Permits are not tied to
 * the thread that took them: a permit taken by one thread may be released by
 * another, which makes semaphores suitable for producer/consumer signalling as
 * well as for bounding concurrent access to a resource.
 *
 * ```k
 * s : Semaphore = Semaphore(2);
 * s.acquire();
 * try {
 *     // at most two threads run here at once
 * } finally {
 *     s.release();
 * }
 * ```
 *
 * A Semaphore owns its native state and is **not** copyable.
 */
public class Semaphore {
    private:
    _native : NativeSemaphore*;
    @ffi::Extern("C") private static __k_sem_create(permits: int) : NativeSemaphore*;
    @ffi::Extern("C") private static __k_sem_destroy(s: NativeSemaphore*) : void;
    @ffi::Extern("C") private static __k_sem_acquire(s: NativeSemaphore*, permits: int, timeout_nanos: long) : int;
    @ffi::Extern("C") private static __k_sem_try_acquire(s: NativeSemaphore*, permits: int) : int;
    @ffi::Extern("C") private static __k_sem_release(s: NativeSemaphore*, permits: int) : void;
    @ffi::Extern("C") private static __k_sem_available(s: const NativeSemaphore*) : int;
    @ffi::Extern("C") private static __k_sem_drain(s: NativeSemaphore*) : int;

    public:
    /**
     * Construct a semaphore with an initial permit count.
     * @param permits Initial number of available permits, clamped to 0.
     */
    Semaphore(permits: int) {
        _native = __k_sem_create(permits);
    }

    /** Construct a semaphore with no permit available. */
    Semaphore() {
        _native = __k_sem_create(0);
    }

    /** Release the native state. The semaphore must not be in use. */
    ~Semaphore() {
        if (_native != null) {
            __k_sem_destroy(_native);
            _native = null;
        }
    }

    /**
     * Acquire one permit, blocking until one is available.
     *
     * @throws ThreadInterruptionException the calling thread was interrupted;
     *         the interrupted flag is cleared and no permit is consumed.
     */
    acquire() : void throws(ThreadInterruptionException) {
        if (__k_sem_acquire(_native, 1, -1L) == SYNC_INTERRUPTED) {
            Thread::interrupted();
            throw ThreadInterruptionException();
        }
    }

    /**
     * Acquire several permits at once, blocking until they are all available.
     * The acquisition is atomic: either every permit is taken, or none is.
     *
     * @param permits How many permits to take.
     * @throws ThreadInterruptionException the calling thread was interrupted;
     *         the interrupted flag is cleared and no permit is consumed.
     */
    acquire(permits: int) : void throws(ThreadInterruptionException) {
        if (__k_sem_acquire(_native, permits, -1L) == SYNC_INTERRUPTED) {
            Thread::interrupted();
            throw ThreadInterruptionException();
        }
    }

    /**
     * Try to take one permit without blocking.
     * @return true if a permit was taken.
     */
    tryAcquire() : bool {
        return __k_sem_try_acquire(_native, 1) != 0;
    }

    /**
     * Try to take several permits without blocking.
     * @param permits How many permits to take.
     * @return true if they were all taken.
     */
    tryAcquire(permits: int) : bool {
        return __k_sem_try_acquire(_native, permits) != 0;
    }

    /**
     * Try to take one permit, giving up after `timeout`.
     *
     * @param timeout Maximum time to wait.
     * @return true if a permit was taken.
     * @throws ThreadInterruptionException the calling thread was interrupted;
     *         the interrupted flag is cleared and no permit is consumed.
     */
    tryAcquire(timeout: Duration&) : bool throws(ThreadInterruptionException) {
        rc : int = __k_sem_acquire(_native, 1, timeout.toNanos());
        if (rc == SYNC_INTERRUPTED) {
            Thread::interrupted();
            throw ThreadInterruptionException();
        }
        return rc == SYNC_OK;
    }

    /**
     * Try to take several permits, giving up after `timeout`.
     *
     * @param permits How many permits to take.
     * @param timeout Maximum time to wait.
     * @return true if they were all taken.
     * @throws ThreadInterruptionException the calling thread was interrupted;
     *         the interrupted flag is cleared and no permit is consumed.
     */
    tryAcquire(permits: int, timeout: Duration&) : bool throws(ThreadInterruptionException) {
        rc : int = __k_sem_acquire(_native, permits, timeout.toNanos());
        if (rc == SYNC_INTERRUPTED) {
            Thread::interrupted();
            throw ThreadInterruptionException();
        }
        return rc == SYNC_OK;
    }

    /** Give one permit back, waking one waiter if any. */
    release() : void {
        __k_sem_release(_native, 1);
    }

    /**
     * Give several permits back at once.
     * @param permits How many permits to add.
     */
    release(permits: int) : void {
        __k_sem_release(_native, permits);
    }

    /**
     * @return the number of permits currently available. The value is a
     *         snapshot and may be stale as soon as it is read; it is meant for
     *         diagnostics, not for control flow.
     */
    availablePermits() : int {
        return __k_sem_available(_native);
    }

    /**
     * Atomically take every available permit.
     * @return how many permits were taken, possibly 0.
     */
    drainPermits() : int {
        return __k_sem_drain(_native);
    }
}
