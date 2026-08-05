/*
 * K Language standard library — Mutex, ReentrantLock and Condition
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
 * Opaque handle to the C-level KMutex structure.
 * Users never touch this struct; it is manipulated only through Mutex methods.
 */
public struct NativeMutex {
}

/**
 * Opaque handle to the C-level KCondition structure.
 */
public struct NativeCondition {
}

// ═════════════════════════════════════════════════════════════════════════════
// Wait outcome codes shared by every synchronisation primitive.
// They mirror the K_SYNC_* constants of runtime/sync_primitives.h.
// ═════════════════════════════════════════════════════════════════════════════

/** The wait completed: the resource was acquired or the condition was met. */
public const SYNC_OK : int = 0;
/** The calling thread was interrupted before or during the wait. */
public const SYNC_INTERRUPTED : int = 1;
/** The supplied deadline expired before the wait could complete. */
public const SYNC_TIMEOUT : int = 2;
/** The operation is invalid in the current state (e.g. unlocking a free lock). */
public const SYNC_ILLEGAL : int = 3;
/** The barrier generation this thread was waiting on was broken. */
public const SYNC_BROKEN : int = 4;

// ═════════════════════════════════════════════════════════════════════════════
// Lock — common contract of every mutual-exclusion primitive.
// ═════════════════════════════════════════════════════════════════════════════

/**
 * Lock — a mutual exclusion device.
 *
 * A lock is held by exactly one thread at a time. Every acquisition must be
 * paired with exactly one `unlock()`; the recommended shape is:
 *
 * ```k
 * m.lock();
 * try {
 *     // critical section
 * } finally {
 *     m.unlock();
 * }
 * ```
 */
public interface Lock {
    /** Acquire the lock, blocking uninterruptibly until it is available. */
    lock() : void;

    /**
     * Acquire the lock, honouring interruption.
     * @throws ThreadInterruptionException the calling thread was interrupted.
     */
    lockInterruptibly() : void throws ThreadInterruptionException;

    /**
     * Try to acquire the lock without blocking.
     * @return true if the lock was acquired.
     */
    tryLock() : bool;

    /**
     * Release the lock.
     * @throws IllegalMonitorStateException the calling thread does not hold it.
     */
    unlock() : void throws IllegalMonitorStateException;
}

// ═════════════════════════════════════════════════════════════════════════════
// Mutex — non-reentrant mutual exclusion.
// ═════════════════════════════════════════════════════════════════════════════

/**
 * Mutex — a plain, non-reentrant lock.
 *
 * Re-locking a mutex from the thread that already owns it deadlocks; use
 * ReentrantLock when reentrancy is required.
 *
 * A Mutex owns its native state and is **not** copyable: pass it around by
 * pointer, link or reference.
 */
public class Mutex : public Lock {
    protected:
    _native : NativeMutex*;
    @ffi::Extern("C") private static __k_mutex_create(reentrant: int) : NativeMutex*;
    @ffi::Extern("C") private static __k_mutex_destroy(m: NativeMutex*) : void;
    @ffi::Extern("C") private static __k_mutex_lock(m: NativeMutex*) : void;
    @ffi::Extern("C") private static __k_mutex_lock_interruptibly(m: NativeMutex*, timeout_nanos: long) : int;
    @ffi::Extern("C") private static __k_mutex_try_lock(m: NativeMutex*) : int;
    @ffi::Extern("C") private static __k_mutex_unlock(m: NativeMutex*) : int;
    @ffi::Extern("C") private static __k_mutex_is_held(m: const NativeMutex*) : int;
    @ffi::Extern("C") private static __k_mutex_hold_count(m: const NativeMutex*) : int;

    /**
     * Construct a lock with an explicit reentrancy mode.
     * @param reentrant true to allow the owner to re-lock.
     */
    Mutex(reentrant: bool) {
        if (reentrant) {
            _native = __k_mutex_create(1);
        } else {
            _native = __k_mutex_create(0);
        }
    }

    public:
    /** Construct a non-reentrant mutex. */
    Mutex() {
        _native = __k_mutex_create(0);
    }

    /** Release the native state. The mutex must not be in use. */
    ~Mutex() {
        if (_native != null) {
            __k_mutex_destroy(_native);
            _native = null;
        }
    }

    /**
     * Acquire the lock, blocking uninterruptibly until it is available.
     * Interruption requests issued while blocked stay pending.
     */
    override lock() : void {
        __k_mutex_lock(_native);
    }

    /**
     * Acquire the lock, honouring interruption.
     * @throws ThreadInterruptionException the calling thread was interrupted;
     *         the interrupted flag is cleared.
     */
    override lockInterruptibly() : void throws ThreadInterruptionException {
        rc : int = __k_mutex_lock_interruptibly(_native, -1L);
        if (rc == SYNC_INTERRUPTED) {
            Thread::interrupted();
            throw ThreadInterruptionException();
        }
    }

    /**
     * Try to acquire the lock without blocking.
     * @return true if the lock was acquired.
     */
    override tryLock() : bool {
        return __k_mutex_try_lock(_native) != 0;
    }

    /**
     * Try to acquire the lock, giving up after `timeout`.
     * A non-positive timeout behaves like tryLock().
     *
     * @param timeout Maximum time to wait.
     * @return true if the lock was acquired.
     * @throws ThreadInterruptionException the calling thread was interrupted;
     *         the interrupted flag is cleared.
     */
    tryLock(timeout: Duration&) : bool throws ThreadInterruptionException {
        rc : int = __k_mutex_lock_interruptibly(_native, timeout.toNanos());
        if (rc == SYNC_INTERRUPTED) {
            Thread::interrupted();
            throw ThreadInterruptionException();
        }
        return rc == SYNC_OK;
    }

    /**
     * Release the lock.
     * @throws IllegalMonitorStateException the calling thread does not hold it.
     */
    override unlock() : void throws IllegalMonitorStateException {
        if (__k_mutex_unlock(_native) != SYNC_OK) {
            throw IllegalMonitorStateException();
        }
    }

    /** @return true if the calling thread currently holds this lock. */
    isHeldByCurrentThread() : bool {
        return __k_mutex_is_held(_native) != 0;
    }

    /**
     * @return how many times the calling thread has acquired this lock,
     *         0 when it does not hold it.
     */
    holdCount() : int {
        return __k_mutex_hold_count(_native);
    }

    /**
     * Create a condition variable bound to this lock.
     * The returned Condition must not outlive this lock.
     */
    newCondition() : Condition! {
        return new Condition(_native);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// ReentrantLock — a Mutex that its owner may re-acquire.
// ═════════════════════════════════════════════════════════════════════════════

/**
 * ReentrantLock — a lock that the owning thread may acquire several times.
 *
 * Each acquisition must be matched by one `unlock()`; the lock is released
 * only when the hold count drops back to zero.
 */
public class ReentrantLock : public Mutex {
    public:
    /** Construct a free reentrant lock. */
    ReentrantLock() : Mutex(true) {}
}

// ═════════════════════════════════════════════════════════════════════════════
// Condition — wait/notify bound to a Lock.
// ═════════════════════════════════════════════════════════════════════════════

/**
 * Condition — a wait queue attached to a Mutex.
 *
 * The lock must be held when calling any method. `await()` atomically releases
 * the lock and blocks; the lock is always re-acquired before the call returns,
 * whatever the outcome, including when an exception is thrown.
 *
 * Because wake-ups can be spurious, always wait inside a loop:
 *
 * ```k
 * m.lock();
 * try {
 *     while (!condition) { c->await(); }
 *     // condition holds here
 * } finally {
 *     m.unlock();
 * }
 * ```
 *
 * Obtain instances through `Mutex.newCondition()`.
 */
public class Condition {
    private:
    _native : NativeCondition*;
    @ffi::Extern("C") private static __k_cond_create(m: NativeMutex*) : NativeCondition*;
    @ffi::Extern("C") private static __k_cond_destroy(c: NativeCondition*) : void;
    @ffi::Extern("C") private static __k_cond_await(c: NativeCondition*, timeout_nanos: long, interruptible: int) : int;
    @ffi::Extern("C") private static __k_cond_signal(c: NativeCondition*) : int;
    @ffi::Extern("C") private static __k_cond_signal_all(c: NativeCondition*) : int;

    public:
    /**
     * Bind a new condition to a native lock.
     * Prefer `Mutex.newCondition()`.
     *
     * @param mutex The native lock this condition is associated with.
     */
    Condition(mutex: NativeMutex*) {
        _native = __k_cond_create(mutex);
    }

    /** Release the native state. */
    ~Condition() {
        if (_native != null) {
            __k_cond_destroy(_native);
            _native = null;
        }
    }

    /**
     * Release the lock and wait until signalled.
     *
     * @throws ThreadInterruptionException the calling thread was interrupted;
     *         the interrupted flag is cleared and the lock is re-acquired first.
     * @throws IllegalMonitorStateException the calling thread does not hold the
     *         associated lock.
     */
    await() : void throws ThreadInterruptionException, IllegalMonitorStateException {
        rc : int = __k_cond_await(_native, -1L, 1);
        if (rc == SYNC_ILLEGAL) {
            throw IllegalMonitorStateException();
        }
        if (rc == SYNC_INTERRUPTED) {
            Thread::interrupted();
            throw ThreadInterruptionException();
        }
    }

    /**
     * Release the lock and wait until signalled or until `timeout` elapses.
     *
     * @param timeout Maximum time to wait.
     * @return true if the wait ended with a signal, false on timeout.
     * @throws ThreadInterruptionException the calling thread was interrupted;
     *         the interrupted flag is cleared and the lock is re-acquired first.
     * @throws IllegalMonitorStateException the calling thread does not hold the
     *         associated lock.
     */
    await(timeout: Duration&) : bool throws ThreadInterruptionException, IllegalMonitorStateException {
        rc : int = __k_cond_await(_native, timeout.toNanos(), 1);
        if (rc == SYNC_ILLEGAL) {
            throw IllegalMonitorStateException();
        }
        if (rc == SYNC_INTERRUPTED) {
            Thread::interrupted();
            throw ThreadInterruptionException();
        }
        return rc == SYNC_OK;
    }

    /**
     * Release the lock and wait until signalled, ignoring interruption.
     * An interruption request issued while blocked stays pending and can be
     * observed by the caller afterwards.
     *
     * @throws IllegalMonitorStateException the calling thread does not hold the
     *         associated lock.
     */
    awaitUninterruptibly() : void throws IllegalMonitorStateException {
        if (__k_cond_await(_native, -1L, 0) == SYNC_ILLEGAL) {
            throw IllegalMonitorStateException();
        }
    }

    /**
     * Wake one thread waiting on this condition.
     * @throws IllegalMonitorStateException the calling thread does not hold the
     *         associated lock.
     */
    signal() : void throws IllegalMonitorStateException {
        if (__k_cond_signal(_native) != SYNC_OK) {
            throw IllegalMonitorStateException();
        }
    }

    /**
     * Wake every thread waiting on this condition.
     * @throws IllegalMonitorStateException the calling thread does not hold the
     *         associated lock.
     */
    signalAll() : void throws IllegalMonitorStateException {
        if (__k_cond_signal_all(_native) != SYNC_OK) {
            throw IllegalMonitorStateException();
        }
    }
}
