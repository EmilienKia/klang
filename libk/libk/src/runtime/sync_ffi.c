/*
 * K Language standard library — synchronisation FFI bridge
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
 * Thin C entry points consumed by libk/libk/src/sync/*.k.  Every function
 * takes and returns plain scalars or opaque pointers so that the K side never
 * needs to know the layout of the underlying C structures.
 */

#define _GNU_SOURCE
#include "sync_primitives.h"

#include <stddef.h>

/* ── Mutex ──────────────────────────────────────────────────────────────── */

void* __k_mutex_create(int reentrant) {
    return (void*)k_mutex_create(reentrant != 0);
}

void __k_mutex_destroy(void* m) {
    k_mutex_destroy((KMutex*)m);
}

void __k_mutex_lock(void* m) {
    k_mutex_lock((KMutex*)m);
}

int __k_mutex_lock_interruptibly(void* m, long long timeout_nanos) {
    return k_mutex_lock_interruptibly((KMutex*)m, (int64_t)timeout_nanos);
}

int __k_mutex_try_lock(void* m) {
    return k_mutex_try_lock((KMutex*)m) ? 1 : 0;
}

int __k_mutex_unlock(void* m) {
    return k_mutex_unlock((KMutex*)m);
}

int __k_mutex_is_held(const void* m) {
    return k_mutex_is_held_by_current((const KMutex*)m) ? 1 : 0;
}

int __k_mutex_hold_count(const void* m) {
    return k_mutex_hold_count((const KMutex*)m);
}

/* ── Condition ──────────────────────────────────────────────────────────── */

void* __k_cond_create(void* mutex) {
    return (void*)k_condition_create((KMutex*)mutex);
}

void __k_cond_destroy(void* c) {
    k_condition_destroy((KCondition*)c);
}

int __k_cond_await(void* c, long long timeout_nanos, int interruptible) {
    return k_condition_await((KCondition*)c, (int64_t)timeout_nanos,
                             interruptible != 0);
}

int __k_cond_signal(void* c) {
    return k_condition_signal((KCondition*)c);
}

int __k_cond_signal_all(void* c) {
    return k_condition_signal_all((KCondition*)c);
}

/* ── Semaphore ──────────────────────────────────────────────────────────── */

void* __k_sem_create(int permits) {
    return (void*)k_semaphore_create((int32_t)permits);
}

void __k_sem_destroy(void* s) {
    k_semaphore_destroy((KSemaphore*)s);
}

int __k_sem_acquire(void* s, int permits, long long timeout_nanos) {
    return k_semaphore_acquire((KSemaphore*)s, (int32_t)permits,
                               (int64_t)timeout_nanos);
}

int __k_sem_try_acquire(void* s, int permits) {
    return k_semaphore_try_acquire((KSemaphore*)s, (int32_t)permits) ? 1 : 0;
}

void __k_sem_release(void* s, int permits) {
    k_semaphore_release((KSemaphore*)s, (int32_t)permits);
}

int __k_sem_available(const void* s) {
    return (int)k_semaphore_available((const KSemaphore*)s);
}

int __k_sem_drain(void* s) {
    return (int)k_semaphore_drain((KSemaphore*)s);
}

/* ── CountDownLatch ─────────────────────────────────────────────────────── */

void* __k_latch_create(long long count) {
    return (void*)k_latch_create((int64_t)count);
}

void __k_latch_destroy(void* l) {
    k_latch_destroy((KLatch*)l);
}

int __k_latch_await(void* l, long long timeout_nanos) {
    return k_latch_await((KLatch*)l, (int64_t)timeout_nanos);
}

void __k_latch_count_down(void* l) {
    k_latch_count_down((KLatch*)l);
}

long long __k_latch_count(const void* l) {
    return (long long)k_latch_count((const KLatch*)l);
}

/* ── CyclicBarrier ──────────────────────────────────────────────────────── */

void* __k_barrier_create(int parties) {
    return (void*)k_barrier_create((int32_t)parties);
}

void __k_barrier_destroy(void* b) {
    k_barrier_destroy((KBarrier*)b);
}

/*
 * Await on the barrier.
 * Returns the arrival index (>= 0) on success, or a negative code:
 *   -1 interrupted, -2 timed out, -3 the generation was broken.
 */
int __k_barrier_await(void* b, long long timeout_nanos) {
    int32_t arrival = 0;
    int rc = k_barrier_await((KBarrier*)b, (int64_t)timeout_nanos, &arrival);
    switch (rc) {
        case K_SYNC_OK:          return (int)arrival;
        case K_SYNC_INTERRUPTED: return -1;
        case K_SYNC_TIMEOUT:     return -2;
        default:                 return -3;
    }
}

void __k_barrier_reset(void* b) {
    k_barrier_reset((KBarrier*)b);
}

int __k_barrier_parties(const void* b) {
    return (int)k_barrier_parties((const KBarrier*)b);
}

int __k_barrier_waiting(const void* b) {
    return (int)k_barrier_waiting((const KBarrier*)b);
}

int __k_barrier_is_broken(const void* b) {
    return k_barrier_is_broken((const KBarrier*)b) ? 1 : 0;
}

/* ── ReadWriteLock ──────────────────────────────────────────────────────── */

void* __k_rwlock_create(void) {
    return (void*)k_rwlock_create();
}

void __k_rwlock_destroy(void* l) {
    k_rwlock_destroy((KRwLock*)l);
}

int __k_rwlock_read_lock(void* l, long long timeout_nanos, int interruptible) {
    return k_rwlock_read_lock((KRwLock*)l, (int64_t)timeout_nanos,
                              interruptible != 0);
}

int __k_rwlock_try_read_lock(void* l) {
    return k_rwlock_try_read_lock((KRwLock*)l) ? 1 : 0;
}

int __k_rwlock_read_unlock(void* l) {
    return k_rwlock_read_unlock((KRwLock*)l);
}

int __k_rwlock_write_lock(void* l, long long timeout_nanos, int interruptible) {
    return k_rwlock_write_lock((KRwLock*)l, (int64_t)timeout_nanos,
                               interruptible != 0);
}

int __k_rwlock_try_write_lock(void* l) {
    return k_rwlock_try_write_lock((KRwLock*)l) ? 1 : 0;
}

int __k_rwlock_write_unlock(void* l) {
    return k_rwlock_write_unlock((KRwLock*)l);
}

int __k_rwlock_read_count(const void* l) {
    return (int)k_rwlock_read_count((const KRwLock*)l);
}

int __k_rwlock_is_write_locked(const void* l) {
    return k_rwlock_is_write_locked((const KRwLock*)l) ? 1 : 0;
}
