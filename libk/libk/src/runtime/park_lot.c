/*
 * K Language runtime — interruptible park lot
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

/*
 * Implementation of the interruptible park lot shared by the synchronisation
 * primitives and by the asynchronous I/O substrate.
 */

#define _GNU_SOURCE
#include "park_lot.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>

/* ── Futex helpers ──────────────────────────────────────────────────────── */

static int sp_futex_wait(uint32_t* addr, uint32_t val,
                         const struct timespec* timeout) {
    return (int)syscall(SYS_futex, addr, FUTEX_WAIT_PRIVATE, val,
                        timeout, NULL, 0);
}

int k_futex_wake_word(uint32_t* addr, int n) {
    return (int)syscall(SYS_futex, addr, FUTEX_WAKE_PRIVATE, n,
                        NULL, NULL, 0);
}

int64_t k_monotonic_nanos(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
}

/* ── Park lot ───────────────────────────────────────────────────────────── */

/*
 * A park lot is an internal mutex guarding a primitive's state together with
 * the set of threads currently blocked on it.
 *
 * Waiters park on the futex word of their own KInterruptToken — the very word
 * k_thread_interrupt() bumps — which is what makes every wait in this file
 * interruptible without any dedicated wake-up channel.  A thread without a
 * runtime token (e.g. the process main thread) polls instead, and is therefore
 * not interruptible.
 */

/* Longest single park for a thread that has no interrupt token. */
#define K_PARK_TOKENLESS_POLL_NANOS (500000LL) /* 0.5 ms */

int k_park_lot_init(KParkLot* lot) {
    lot->waiters  = NULL;
    lot->count    = 0u;
    lot->capacity = 0u;
    return pthread_mutex_init(&lot->lock, NULL);
}

void k_park_lot_destroy(KParkLot* lot) {
    pthread_mutex_destroy(&lot->lock);
    free(lot->waiters);
    lot->waiters  = NULL;
    lot->count    = 0u;
    lot->capacity = 0u;
}

/* Caller must hold lot->lock. */
static int k_park_lot_add(KParkLot* lot, KRuntimeThread* t) {
    if (lot->count == lot->capacity) {
        unsigned cap = lot->capacity == 0u ? 4u : lot->capacity * 2u;
        KRuntimeThread** grown =
            (KRuntimeThread**)realloc(lot->waiters, cap * sizeof(*grown));
        if (grown == NULL) {
            return 0;
        }
        lot->waiters  = grown;
        lot->capacity = cap;
    }
    lot->waiters[lot->count++] = t;
    return 1;
}

/* Caller must hold lot->lock. */
static void k_park_lot_remove(KParkLot* lot, KRuntimeThread* t) {
    for (unsigned i = 0; i < lot->count; ++i) {
        if (lot->waiters[i] == t) {
            lot->waiters[i] = lot->waiters[lot->count - 1u];
            lot->count--;
            return;
        }
    }
}

/*
 * Wake every parked thread.  Caller must hold lot->lock, which is what
 * guarantees the absence of lost wake-ups: a waiter samples its park
 * generation while holding the same lock.
 */
void k_park_lot_wake_all(KParkLot* lot) {
    for (unsigned i = 0; i < lot->count; ++i) {
        KRuntimeThread* w = lot->waiters[i];
        atomic_fetch_add_explicit(&w->interrupt.futex_word, 1u,
                                  memory_order_release);
        k_futex_wake_word((uint32_t*)&w->interrupt.futex_word, 1);
    }
}

/*
 * Blocking helper shared by every primitive.
 *
 * `on_enter` (optional) runs once under the lot lock before the first check.
 * `ready` runs under the lot lock and returns true when the wait can end; it
 * may perform the state mutation that corresponds to acquiring the resource.
 * `on_exit` (optional) runs under the lot lock with the final outcome, so a
 * primitive can undo a registration performed by `on_enter`.
 */

int k_park_until(KParkLot* lot,
                      k_hook_fn on_enter,
                      k_ready_fn ready,
                      k_exit_fn on_exit,
                      void* ctx,
                      int64_t timeout_nanos,
                      bool interruptible) {
    KRuntimeThread* self = k_thread_current();
    const int64_t deadline =
        timeout_nanos < 0 ? -1 : k_monotonic_nanos() + timeout_nanos;
    int result = K_PARK_OK;

    pthread_mutex_lock(&lot->lock);
    if (on_enter != NULL) {
        on_enter(ctx);
    }

    for (;;) {
        if (ready(ctx)) {
            result = K_PARK_OK;
            break;
        }
        if (interruptible && self != NULL &&
            atomic_load_explicit(&self->interrupt.interrupted,
                                 memory_order_acquire)) {
            result = K_PARK_INTERRUPTED;
            break;
        }

        int64_t remaining = -1;
        if (deadline >= 0) {
            remaining = deadline - k_monotonic_nanos();
            if (remaining <= 0) {
                /* Give the predicate one last chance before giving up. */
                result = ready(ctx) ? K_PARK_OK : K_PARK_TIMEOUT;
                break;
            }
        }

        if (self == NULL) {
            /* No interrupt token: poll in short slices. */
            int64_t slice = K_PARK_TOKENLESS_POLL_NANOS;
            if (remaining >= 0 && remaining < slice) {
                slice = remaining;
            }
            pthread_mutex_unlock(&lot->lock);
            struct timespec ts;
            ts.tv_sec  = (time_t)(slice / 1000000000LL);
            ts.tv_nsec = (long)(slice % 1000000000LL);
            nanosleep(&ts, NULL);
            pthread_mutex_lock(&lot->lock);
            continue;
        }

        if (!k_park_lot_add(lot, self)) {
            /* Out of memory while registering: degrade to polling rather than
             * blocking on a wake-up that would never be delivered. */
            pthread_mutex_unlock(&lot->lock);
            struct timespec ts;
            ts.tv_sec  = 0;
            ts.tv_nsec = (long)K_PARK_TOKENLESS_POLL_NANOS;
            nanosleep(&ts, NULL);
            pthread_mutex_lock(&lot->lock);
            continue;
        }

        /* Sample the park generation under the lot lock: any waker must take
         * the same lock to bump it, so no wake-up can slip through. */
        uint32_t gen = atomic_load_explicit(&self->interrupt.futex_word,
                                            memory_order_acquire);
        pthread_mutex_unlock(&lot->lock);

        struct timespec ts;
        struct timespec* to = NULL;
        if (remaining >= 0) {
            ts.tv_sec  = (time_t)(remaining / 1000000000LL);
            ts.tv_nsec = (long)(remaining % 1000000000LL);
            to = &ts;
        }
        sp_futex_wait((uint32_t*)&self->interrupt.futex_word, gen, to);

        pthread_mutex_lock(&lot->lock);
        k_park_lot_remove(lot, self);
    }

    if (on_exit != NULL) {
        on_exit(ctx, result);
    }
    pthread_mutex_unlock(&lot->lock);
    return result;
}

