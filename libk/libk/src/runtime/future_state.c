/*
 * K Language standard library — Future/Promise shared state (C substrate)
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

#define _GNU_SOURCE
#include "future_state.h"
#include "runtime_thread.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>

/* ── Futex helpers ──────────────────────────────────────────────────────── */

static int fs_futex_wait(uint32_t* addr, uint32_t val,
                         const struct timespec* timeout) {
    return (int)syscall(SYS_futex, addr, FUTEX_WAIT_PRIVATE, val,
                        timeout, NULL, 0);
}

static int fs_futex_wake(uint32_t* addr, int n) {
    return (int)syscall(SYS_futex, addr, FUTEX_WAKE_PRIVATE, n,
                        NULL, NULL, 0);
}

/* ── Monotonic time ─────────────────────────────────────────────────────── */

static int64_t monotonic_nanos(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
}

/* ── State ──────────────────────────────────────────────────────────────── */

/*
 * Waiters are parked on their own interrupt token, exactly like Thread.sleep():
 * the token's futex word is a generation counter that both k_thread_interrupt()
 * and a completion bump, so a blocked waiter is released promptly by either
 * event without any polling.
 *
 * The waiter set is a small dynamic array guarded by `waiters_mutex`. It only
 * ever holds threads currently blocked inside k_future_state_wait(), which in
 * practice is a handful at most.
 */
struct KFutureState {
    _Atomic unsigned ref_count;       /* number of live K handles            */
    _Atomic int      completion;      /* KFutureCompletion, published once   */

    pthread_mutex_t  waiters_mutex;   /* guards the waiter array             */
    KRuntimeThread** waiters;
    unsigned         waiter_count;
    unsigned         waiter_capacity;

    pthread_mutex_t  chain_mutex;     /* guards the K-side handle chain      */
};

KFutureState* k_future_state_create(void) {
    KFutureState* st = (KFutureState*)calloc(1, sizeof(KFutureState));
    if (st == NULL) {
        return NULL;
    }
    atomic_store_explicit(&st->ref_count, 1u, memory_order_relaxed);
    atomic_store_explicit(&st->completion, K_FUTURE_PENDING,
                          memory_order_relaxed);
    if (pthread_mutex_init(&st->waiters_mutex, NULL) != 0) {
        free(st);
        return NULL;
    }
    if (pthread_mutex_init(&st->chain_mutex, NULL) != 0) {
        pthread_mutex_destroy(&st->waiters_mutex);
        free(st);
        return NULL;
    }
    return st;
}

void k_future_state_destroy(KFutureState* st) {
    if (st == NULL) {
        return;
    }
    pthread_mutex_destroy(&st->waiters_mutex);
    pthread_mutex_destroy(&st->chain_mutex);
    free(st->waiters);
    free(st);
}

/* ── Handle reference counting ──────────────────────────────────────────── */

void k_future_state_retain(KFutureState* st) {
    if (st != NULL) {
        atomic_fetch_add_explicit(&st->ref_count, 1u, memory_order_relaxed);
    }
}

unsigned k_future_state_release(KFutureState* st) {
    if (st == NULL) {
        return 0u;
    }
    /* Release ordering so that everything the dropping handle did is visible
     * to the handle that observes the count reaching zero. */
    unsigned previous = atomic_fetch_sub_explicit(&st->ref_count, 1u,
                                                  memory_order_acq_rel);
    return previous - 1u;
}

/* ── Handle-chain mutex ─────────────────────────────────────────────────── */

void k_future_state_lock(KFutureState* st) {
    if (st != NULL) {
        pthread_mutex_lock(&st->chain_mutex);
    }
}

void k_future_state_unlock(KFutureState* st) {
    if (st != NULL) {
        pthread_mutex_unlock(&st->chain_mutex);
    }
}

/* ── Completion ─────────────────────────────────────────────────────────── */

int k_future_state_get(const KFutureState* st) {
    if (st == NULL) {
        return K_FUTURE_PENDING;
    }
    return atomic_load_explicit(&st->completion, memory_order_acquire);
}

bool k_future_state_is_done(const KFutureState* st) {
    return k_future_state_get(st) != K_FUTURE_PENDING;
}

bool k_future_state_complete(KFutureState* st, int completion) {
    if (st == NULL || completion == K_FUTURE_PENDING) {
        return false;
    }

    int expected = K_FUTURE_PENDING;
    if (!atomic_compare_exchange_strong_explicit(
            &st->completion, &expected, completion,
            memory_order_release, memory_order_acquire)) {
        return false;
    }

    /* Release every blocked waiter by bumping its park generation. */
    pthread_mutex_lock(&st->waiters_mutex);
    for (unsigned i = 0; i < st->waiter_count; ++i) {
        KRuntimeThread* w = st->waiters[i];
        atomic_fetch_add_explicit(&w->interrupt.futex_word, 1u,
                                  memory_order_release);
        fs_futex_wake((uint32_t*)&w->interrupt.futex_word, 1);
    }
    pthread_mutex_unlock(&st->waiters_mutex);
    return true;
}

/* ── Waiter registration ────────────────────────────────────────────────── */

static int add_waiter(KFutureState* st, KRuntimeThread* t) {
    int ok = 1;
    pthread_mutex_lock(&st->waiters_mutex);
    if (st->waiter_count == st->waiter_capacity) {
        unsigned cap = st->waiter_capacity == 0 ? 4u : st->waiter_capacity * 2u;
        KRuntimeThread** grown =
            (KRuntimeThread**)realloc(st->waiters, cap * sizeof(*grown));
        if (grown == NULL) {
            ok = 0;
        } else {
            st->waiters = grown;
            st->waiter_capacity = cap;
        }
    }
    if (ok) {
        st->waiters[st->waiter_count++] = t;
    }
    pthread_mutex_unlock(&st->waiters_mutex);
    return ok;
}

static void remove_waiter(KFutureState* st, KRuntimeThread* t) {
    pthread_mutex_lock(&st->waiters_mutex);
    for (unsigned i = 0; i < st->waiter_count; ++i) {
        if (st->waiters[i] == t) {
            st->waiters[i] = st->waiters[st->waiter_count - 1];
            st->waiter_count--;
            break;
        }
    }
    pthread_mutex_unlock(&st->waiters_mutex);
}

/* ── Waiting ────────────────────────────────────────────────────────────── */

/* Longest single park for a thread that has no interrupt token: such a thread
 * cannot be woken by k_thread_interrupt(), so it re-checks periodically. */
#define K_FUTURE_TOKENLESS_POLL_NANOS (1000000LL) /* 1 ms */

static int wait_without_token(KFutureState* st, int64_t timeout_nanos) {
    const int64_t deadline =
        timeout_nanos < 0 ? -1 : monotonic_nanos() + timeout_nanos;

    for (;;) {
        if (k_future_state_is_done(st)) {
            return K_FUTURE_WAIT_DONE;
        }

        int64_t slice = K_FUTURE_TOKENLESS_POLL_NANOS;
        if (deadline >= 0) {
            int64_t remaining = deadline - monotonic_nanos();
            if (remaining <= 0) {
                return k_future_state_is_done(st) ? K_FUTURE_WAIT_DONE
                                                  : K_FUTURE_WAIT_TIMEOUT;
            }
            if (remaining < slice) {
                slice = remaining;
            }
        }

        struct timespec ts;
        ts.tv_sec  = (time_t)(slice / 1000000000LL);
        ts.tv_nsec = (long)(slice % 1000000000LL);
        nanosleep(&ts, NULL);
    }
}

int k_future_state_wait(KFutureState* st, int64_t timeout_nanos) {
    if (st == NULL) {
        return K_FUTURE_WAIT_DONE;
    }
    if (k_future_state_is_done(st)) {
        return K_FUTURE_WAIT_DONE;
    }

    KRuntimeThread* self = k_thread_current();
    if (self == NULL) {
        return wait_without_token(st, timeout_nanos);
    }

    /* An already-interrupted thread must not start a blocking wait. */
    if (atomic_load_explicit(&self->interrupt.interrupted,
                             memory_order_acquire)) {
        return K_FUTURE_WAIT_INTERRUPTED;
    }

    if (!add_waiter(st, self)) {
        /* Out of memory while registering: degrade to the poll-based wait
         * rather than blocking forever on a wake-up that will never come. */
        return wait_without_token(st, timeout_nanos);
    }

    const int64_t deadline =
        timeout_nanos < 0 ? -1 : monotonic_nanos() + timeout_nanos;
    int result;

    for (;;) {
        /* Completion always wins: a value that arrived before the interrupt
         * request must not be reported as an interruption. */
        if (k_future_state_is_done(st)) {
            result = K_FUTURE_WAIT_DONE;
            break;
        }
        if (atomic_load_explicit(&self->interrupt.interrupted,
                                 memory_order_acquire)) {
            result = K_FUTURE_WAIT_INTERRUPTED;
            break;
        }

        struct timespec ts;
        struct timespec* timeout = NULL;
        if (deadline >= 0) {
            int64_t remaining = deadline - monotonic_nanos();
            if (remaining <= 0) {
                result = k_future_state_is_done(st) ? K_FUTURE_WAIT_DONE
                                                    : K_FUTURE_WAIT_TIMEOUT;
                break;
            }
            ts.tv_sec  = (time_t)(remaining / 1000000000LL);
            ts.tv_nsec = (long)(remaining % 1000000000LL);
            timeout = &ts;
        }

        /* Sample the park generation, then re-check the conditions before
         * blocking: any wake-up that happened in between changed the word and
         * makes FUTEX_WAIT return EAGAIN immediately. */
        uint32_t gen = atomic_load_explicit(&self->interrupt.futex_word,
                                            memory_order_acquire);
        if (k_future_state_is_done(st)) {
            result = K_FUTURE_WAIT_DONE;
            break;
        }
        fs_futex_wait((uint32_t*)&self->interrupt.futex_word, gen, timeout);
    }

    remove_waiter(st, self);
    return result;
}
