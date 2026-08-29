/*
 * K Language standard library — Read/write lock
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
 * Opaque handle to the C-level KRwLock structure.
 */
public struct NativeRwLock {
}

/**
 * ReadWriteLock — a lock with a shared read mode and an exclusive write mode.
 *
 * Any number of threads may hold the read lock simultaneously as long as no
 * thread holds the write lock; the write lock is exclusive against both readers
 * and other writers. This favours workloads that read far more often than they
 * write.
 *
 * The implementation is **writer-preferring**: a reader arriving while a writer
 * is queued waits behind it, so a steady stream of readers cannot starve a
 * writer.
 *
 * The lock is *not* reentrant and does *not* support upgrading a read lock into
 * a write lock — attempting either deadlocks.
 *
 * ```k
 * lock : ReadWriteLock = ReadWriteLock();
 * lock.readLock();
 * try {
 *     // shared access
 * } finally {
 *     lock.readUnlock();
 * }
 * ```
 *
 * A ReadWriteLock owns its native state and is **not** copyable.
 */
public class ReadWriteLock {
    private:
    _native : NativeRwLock*;
    @ffi::Extern("C") private static __k_rwlock_create() : NativeRwLock*;
    @ffi::Extern("C") private static __k_rwlock_destroy(l: NativeRwLock*) : void;
    @ffi::Extern("C") private static __k_rwlock_read_lock(l: NativeRwLock*, timeout_nanos: long, interruptible: int) : int;
    @ffi::Extern("C") private static __k_rwlock_try_read_lock(l: NativeRwLock*) : int;
    @ffi::Extern("C") private static __k_rwlock_read_unlock(l: NativeRwLock*) : int;
    @ffi::Extern("C") private static __k_rwlock_write_lock(l: NativeRwLock*, timeout_nanos: long, interruptible: int) : int;
    @ffi::Extern("C") private static __k_rwlock_try_write_lock(l: NativeRwLock*) : int;
    @ffi::Extern("C") private static __k_rwlock_write_unlock(l: NativeRwLock*) : int;
    @ffi::Extern("C") private static __k_rwlock_read_count(l: const NativeRwLock*) : int;
    @ffi::Extern("C") private static __k_rwlock_is_write_locked(l: const NativeRwLock*) : int;

    public:
    /** Construct a free read/write lock. */
    ReadWriteLock() {
        _native = __k_rwlock_create();
    }

    /** Release the native state. The lock must not be in use. */
    ~ReadWriteLock() {
        if (_native != null) {
            __k_rwlock_destroy(_native);
            _native = null;
        }
    }

    /**
     * Acquire the lock in shared read mode, blocking uninterruptibly.
     * Interruption requests issued while blocked stay pending.
     */
    readLock() : void {
        __k_rwlock_read_lock(_native, -1L, 0);
    }

    /**
     * Acquire the read lock, honouring interruption.
     * @throws ThreadInterruptionException the calling thread was interrupted;
     *         the interrupted flag is cleared and the lock is not acquired.
     */
    readLockInterruptibly() : void throws(ThreadInterruptionException) {
        if (__k_rwlock_read_lock(_native, -1L, 1) == SYNC_INTERRUPTED) {
            Thread::interrupted();
            throw ThreadInterruptionException();
        }
    }

    /**
     * Try to acquire the read lock without blocking.
     * @return true if the read lock was acquired.
     */
    tryReadLock() : bool {
        return __k_rwlock_try_read_lock(_native) != 0;
    }

    /**
     * Try to acquire the read lock, giving up after `timeout`.
     *
     * @param timeout Maximum time to wait.
     * @return true if the read lock was acquired.
     * @throws ThreadInterruptionException the calling thread was interrupted;
     *         the interrupted flag is cleared and the lock is not acquired.
     */
    tryReadLock(timeout: Duration&) : bool throws(ThreadInterruptionException) {
        rc : int = __k_rwlock_read_lock(_native, timeout.toNanos(), 1);
        if (rc == SYNC_INTERRUPTED) {
            Thread::interrupted();
            throw ThreadInterruptionException();
        }
        return rc == SYNC_OK;
    }

    /**
     * Release one shared read hold.
     * @throws IllegalMonitorStateException no read lock is currently held.
     */
    readUnlock() : void throws(IllegalMonitorStateException) {
        if (__k_rwlock_read_unlock(_native) != SYNC_OK) {
            throw IllegalMonitorStateException();
        }
    }

    /**
     * Acquire the lock in exclusive write mode, blocking uninterruptibly.
     * Interruption requests issued while blocked stay pending.
     */
    writeLock() : void {
        __k_rwlock_write_lock(_native, -1L, 0);
    }

    /**
     * Acquire the write lock, honouring interruption.
     * @throws ThreadInterruptionException the calling thread was interrupted;
     *         the interrupted flag is cleared and the lock is not acquired.
     */
    writeLockInterruptibly() : void throws(ThreadInterruptionException) {
        if (__k_rwlock_write_lock(_native, -1L, 1) == SYNC_INTERRUPTED) {
            Thread::interrupted();
            throw ThreadInterruptionException();
        }
    }

    /**
     * Try to acquire the write lock without blocking.
     * @return true if the write lock was acquired.
     */
    tryWriteLock() : bool {
        return __k_rwlock_try_write_lock(_native) != 0;
    }

    /**
     * Try to acquire the write lock, giving up after `timeout`.
     *
     * @param timeout Maximum time to wait.
     * @return true if the write lock was acquired.
     * @throws ThreadInterruptionException the calling thread was interrupted;
     *         the interrupted flag is cleared and the lock is not acquired.
     */
    tryWriteLock(timeout: Duration&) : bool throws(ThreadInterruptionException) {
        rc : int = __k_rwlock_write_lock(_native, timeout.toNanos(), 1);
        if (rc == SYNC_INTERRUPTED) {
            Thread::interrupted();
            throw ThreadInterruptionException();
        }
        return rc == SYNC_OK;
    }

    /**
     * Release the exclusive write hold.
     * @throws IllegalMonitorStateException the calling thread does not hold the
     *         write lock.
     */
    writeUnlock() : void throws(IllegalMonitorStateException) {
        if (__k_rwlock_write_unlock(_native) != SYNC_OK) {
            throw IllegalMonitorStateException();
        }
    }

    /**
     * @return how many shared read holds are currently active. The value is a
     *         snapshot meant for diagnostics.
     */
    readCount() : int {
        return __k_rwlock_read_count(_native);
    }

    /** @return true if some thread currently holds the write lock. */
    isWriteLocked() : bool {
        return __k_rwlock_is_write_locked(_native) != 0;
    }
}
