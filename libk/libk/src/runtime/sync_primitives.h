/*
 * K Language runtime — synchronisation primitives (C header)
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
 *
 * C substrate for the K synchronisation types: Mutex, ReentrantLock,
 * Condition, Semaphore, CountDownLatch, CyclicBarrier and ReadWriteLock.
 *
 * Design
 * ------
 * Every blocking operation is built on the same park primitive already used by
 * Thread.sleep() and Future.get(): a waiting thread blocks on the futex word of
 * its own KInterruptToken, which is a generation counter bumped both by
 * k_thread_interrupt() and by whoever makes the wait condition true.  As a
 * result *all* waits in this file are interruptible and timed for free, and a
 * single wake-up mechanism covers the whole runtime.
 *
 * Threads that have no KRuntimeThread (typically the process main thread, or a
 * thread created outside the K runtime) have no futex token; they fall back to
 * a short polling loop and are therefore not interruptible.  This mirrors the
 * behaviour of the future substrate.
 *
 * All primitives are allocated on the C heap and manipulated from K through
 * opaque pointers.
 */

#ifndef KLANG_SYNC_PRIMITIVES_H
#define KLANG_SYNC_PRIMITIVES_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Wait outcomes ──────────────────────────────────────────────────────── */

/** The wait completed: the resource was acquired or the condition was met. */
#define K_SYNC_OK           0
/** The calling thread was interrupted before or during the wait. */
#define K_SYNC_INTERRUPTED  1
/** The supplied deadline expired before the wait could complete. */
#define K_SYNC_TIMEOUT      2
/** The operation is invalid in the current state (e.g. unlocking a free lock). */
#define K_SYNC_ILLEGAL      3
/** The barrier generation this thread was waiting on was broken or reset. */
#define K_SYNC_BROKEN       4

/* ── Opaque primitive types ─────────────────────────────────────────────── */

typedef struct KMutex     KMutex;
typedef struct KCondition KCondition;
typedef struct KSemaphore KSemaphore;
typedef struct KLatch     KLatch;
typedef struct KBarrier   KBarrier;
typedef struct KRwLock    KRwLock;

/* ── Thread identity ────────────────────────────────────────────────────── */

/**
 * Return a process-unique, non-zero identifier for the calling thread.
 *
 * Unlike k_thread_current(), this works for every thread, including those
 * that were not created through the K runtime, so it can be used for lock
 * ownership tracking.
 */
uint64_t k_sync_current_thread_id(void);

/* ── Mutex ──────────────────────────────────────────────────────────────── */

/**
 * Create a mutex.
 * @param reentrant when true the mutex may be re-locked by its current owner,
 *        and must be unlocked as many times as it was locked.
 * @return the new mutex, or NULL on allocation failure.
 */
KMutex* k_mutex_create(bool reentrant);

/** Destroy a mutex. Waiters, if any, are not woken: destroy only unused locks. */
void    k_mutex_destroy(KMutex* m);

/** Acquire the mutex, blocking uninterruptibly until it is available. */
void    k_mutex_lock(KMutex* m);

/**
 * Acquire the mutex, honouring interruption and an optional deadline.
 * @param timeout_nanos negative to wait indefinitely.
 * @return K_SYNC_OK, K_SYNC_INTERRUPTED or K_SYNC_TIMEOUT.
 */
int     k_mutex_lock_interruptibly(KMutex* m, int64_t timeout_nanos);

/** Try to acquire the mutex without blocking. */
bool    k_mutex_try_lock(KMutex* m);

/**
 * Release the mutex.
 * @return K_SYNC_OK, or K_SYNC_ILLEGAL if the caller does not own the mutex.
 */
int     k_mutex_unlock(KMutex* m);

/** True if the calling thread currently owns the mutex. */
bool    k_mutex_is_held_by_current(const KMutex* m);

/** Number of times the current owner has locked the mutex (0 when free). */
int     k_mutex_hold_count(const KMutex* m);

/* ── Condition ──────────────────────────────────────────────────────────── */

/**
 * Create a condition variable associated with a mutex.
 * The mutex must outlive the condition.
 */
KCondition* k_condition_create(KMutex* assoc);
void        k_condition_destroy(KCondition* c);

/**
 * Atomically release the associated mutex and wait for a signal.
 * The mutex is always re-acquired before returning, whatever the outcome.
 *
 * @param timeout_nanos negative to wait indefinitely.
 * @param interruptible when false, interruption is recorded but does not end
 *        the wait (the flag stays set for the caller to observe later).
 * @return K_SYNC_OK on signal, K_SYNC_INTERRUPTED, K_SYNC_TIMEOUT, or
 *         K_SYNC_ILLEGAL if the calling thread does not own the mutex.
 */
int  k_condition_await(KCondition* c, int64_t timeout_nanos, bool interruptible);

/** Wake one waiter. The caller must own the associated mutex. */
int  k_condition_signal(KCondition* c);

/** Wake every waiter. The caller must own the associated mutex. */
int  k_condition_signal_all(KCondition* c);

/* ── Semaphore ──────────────────────────────────────────────────────────── */

KSemaphore* k_semaphore_create(int32_t permits);
void        k_semaphore_destroy(KSemaphore* s);

/**
 * Acquire `permits` permits, blocking until they are available.
 * @param timeout_nanos negative to wait indefinitely.
 * @return K_SYNC_OK, K_SYNC_INTERRUPTED or K_SYNC_TIMEOUT.
 */
int     k_semaphore_acquire(KSemaphore* s, int32_t permits, int64_t timeout_nanos);

/** Try to acquire `permits` permits without blocking. */
bool    k_semaphore_try_acquire(KSemaphore* s, int32_t permits);

/** Return `permits` permits, waking any waiter. */
void    k_semaphore_release(KSemaphore* s, int32_t permits);

/** Current number of available permits (may be negative for no primitive). */
int32_t k_semaphore_available(const KSemaphore* s);

/** Atomically take all remaining permits and return how many were taken. */
int32_t k_semaphore_drain(KSemaphore* s);

/* ── CountDownLatch ─────────────────────────────────────────────────────── */

KLatch* k_latch_create(int64_t count);
void    k_latch_destroy(KLatch* l);

/**
 * Wait until the latch reaches zero.
 * @param timeout_nanos negative to wait indefinitely.
 * @return K_SYNC_OK, K_SYNC_INTERRUPTED or K_SYNC_TIMEOUT.
 */
int     k_latch_await(KLatch* l, int64_t timeout_nanos);

/** Decrement the latch; releases every waiter when it reaches zero. */
void    k_latch_count_down(KLatch* l);

/** Current count. */
int64_t k_latch_count(const KLatch* l);

/* ── CyclicBarrier ──────────────────────────────────────────────────────── */

KBarrier* k_barrier_create(int32_t parties);
void      k_barrier_destroy(KBarrier* b);

/**
 * Wait until `parties` threads have reached the barrier.
 *
 * @param out_arrival receives the arrival index: parties-1 for the first
 *        thread to arrive, 0 for the last one (which trips the barrier).
 * @return K_SYNC_OK, K_SYNC_INTERRUPTED, K_SYNC_TIMEOUT or K_SYNC_BROKEN.
 *         When a thread leaves through interruption or timeout the current
 *         generation is broken and every other waiter also returns
 *         K_SYNC_BROKEN.
 */
int     k_barrier_await(KBarrier* b, int64_t timeout_nanos, int32_t* out_arrival);

/** Break the current generation and start a fresh one. */
void    k_barrier_reset(KBarrier* b);

/** Number of parties required to trip the barrier. */
int32_t k_barrier_parties(const KBarrier* b);

/** Number of parties currently waiting at the barrier. */
int32_t k_barrier_waiting(const KBarrier* b);

/** True if the current generation is broken. */
bool    k_barrier_is_broken(const KBarrier* b);

/* ── ReadWriteLock ──────────────────────────────────────────────────────── */

KRwLock* k_rwlock_create(void);
void     k_rwlock_destroy(KRwLock* l);

/**
 * Acquire the lock in shared (read) mode.
 * Blocks while a writer holds the lock or is waiting for it, so writers cannot
 * be starved by a continuous stream of readers.
 */
int      k_rwlock_read_lock(KRwLock* l, int64_t timeout_nanos, bool interruptible);
bool     k_rwlock_try_read_lock(KRwLock* l);
int      k_rwlock_read_unlock(KRwLock* l);

/** Acquire the lock in exclusive (write) mode. */
int      k_rwlock_write_lock(KRwLock* l, int64_t timeout_nanos, bool interruptible);
bool     k_rwlock_try_write_lock(KRwLock* l);
int      k_rwlock_write_unlock(KRwLock* l);

/** Number of readers currently holding the lock. */
int32_t  k_rwlock_read_count(const KRwLock* l);

/** True if a writer currently holds the lock. */
bool     k_rwlock_is_write_locked(const KRwLock* l);

#ifdef __cplusplus
}
#endif

#endif /* KLANG_SYNC_PRIMITIVES_H */
