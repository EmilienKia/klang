/*
 * K Language runtime — thread runtime implementation (C)
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
#include "runtime_thread.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <sched.h>
#include <unistd.h>
#include <linux/futex.h>
#include <sys/syscall.h>

/* ── Thread-local current thread ────────────────────────────────────────── */
_Thread_local KRuntimeThread* k_current_thread = NULL;

/* ── K throw hook (installed by thread_ffi.c) ───────────────────────────── */
void (*k_throw_thread_interruption)(void) = NULL;

/* ── Futex helpers ───────────────────────────────────────────────────────── */

static int futex_wait(uint32_t* addr, uint32_t val,
                      const struct timespec* timeout) {
    return (int)syscall(SYS_futex, addr, FUTEX_WAIT_PRIVATE, val,
                        timeout, NULL, 0);
}

static int futex_wake(uint32_t* addr, int n) {
    return (int)syscall(SYS_futex, addr, FUTEX_WAKE_PRIVATE, n,
                        NULL, NULL, 0);
}

/* ── Global thread registry ──────────────────────────────────────────────── */

static KThreadRegistry g_registry;
static pthread_once_t  g_registry_once = PTHREAD_ONCE_INIT;

static void registry_init(void) {
    pthread_mutex_init(&g_registry.mutex, NULL);
    g_registry.head    = NULL;
    g_registry.next_id = 1;
}

KThreadRegistry* k_thread_registry_global(void) {
    pthread_once(&g_registry_once, registry_init);
    return &g_registry;
}

void k_thread_registry_register(KThreadRegistry* r, KRuntimeThread* t) {
    pthread_mutex_lock(&r->mutex);
    t->thread_id = r->next_id++;
    t->next = r->head;
    t->prev = NULL;
    if (r->head) r->head->prev = t;
    r->head = t;
    pthread_mutex_unlock(&r->mutex);
}

void k_thread_registry_unregister(KThreadRegistry* r, KRuntimeThread* t) {
    pthread_mutex_lock(&r->mutex);
    if (t->prev) t->prev->next = t->next;
    else         r->head       = t->next;
    if (t->next) t->next->prev = t->prev;
    t->next = t->prev = NULL;
    pthread_mutex_unlock(&r->mutex);
}

KRuntimeThread* k_thread_registry_find(KThreadRegistry* r, uint64_t id) {
    pthread_mutex_lock(&r->mutex);
    KRuntimeThread* cur = r->head;
    while (cur) {
        if (cur->thread_id == id) break;
        cur = cur->next;
    }
    pthread_mutex_unlock(&r->mutex);
    return cur;
}

/* ── OS thread trampoline ────────────────────────────────────────────────── */

static void* thread_trampoline(void* arg) {
    KRuntimeThread* t = (KRuntimeThread*)arg;
    k_current_thread = t;
    atomic_store_explicit(&t->state, K_THREAD_RUNNING, memory_order_release);

    /* Run the user task */
    if (t->task_fn) {
        t->task_fn(t->task_arg);
    }

    k_runtime_thread_exit(t);
    return NULL;
}

/* ── RuntimeThread lifecycle ─────────────────────────────────────────────── */

/*
 * Reap the backing OS thread exactly once.
 * Several joiners (plus destroy()) may reach this point for the same thread;
 * pthread_join() must be called only once per pthread_t, so the first caller
 * wins the atomic exchange and everybody else returns immediately.
 */
static void reap_os_thread(KRuntimeThread* t) {
    if (!atomic_load_explicit(&t->started, memory_order_acquire)) return;
    if (atomic_exchange_explicit(&t->os_joined, 1u, memory_order_acq_rel) == 0u) {
        pthread_join(t->os_thread, NULL);
    }
}

KRuntimeThread* k_runtime_thread_create(void (*task_fn)(void* arg),
                                          void* task_arg) {
    KRuntimeThread* t = (KRuntimeThread*)calloc(1, sizeof(KRuntimeThread));
    if (!t) return NULL;

    atomic_init(&t->interrupt.interrupted, 0);
    atomic_init(&t->interrupt.futex_word, 0);
    atomic_init(&t->interrupt.epoch, 0);
    atomic_init(&t->started, 0u);
    atomic_init(&t->os_joined, 0u);
    atomic_store_explicit(&t->state, K_THREAD_NEW, memory_order_relaxed);

    pthread_mutex_init(&t->join_mutex, NULL);
    pthread_cond_init(&t->join_cond, NULL);

    t->task_fn  = task_fn;
    t->task_arg = task_arg;

    k_thread_registry_register(k_thread_registry_global(), t);
    return t;
}

int k_runtime_thread_start(KRuntimeThread* t) {
    atomic_store_explicit(&t->state, K_THREAD_RUNNABLE, memory_order_release);
    int rc = pthread_create(&t->os_thread, NULL, thread_trampoline, t);
    if (rc != 0) {
        atomic_store_explicit(&t->state, K_THREAD_NEW, memory_order_release);
    } else {
        atomic_store_explicit(&t->started, 1u, memory_order_release);
    }
    return rc;
}

void k_runtime_thread_exit(KRuntimeThread* t) {
    pthread_mutex_lock(&t->join_mutex);
    atomic_store_explicit(&t->state, K_THREAD_TERMINATED, memory_order_release);
    pthread_cond_broadcast(&t->join_cond);
    pthread_mutex_unlock(&t->join_mutex);

    k_thread_registry_unregister(k_thread_registry_global(), t);
}

void k_runtime_thread_destroy(KRuntimeThread* t) {
    /* An OS thread that is still running holds a pointer to this object (it is
     * its own trampoline argument) and will write its TERMINATED state into it.
     * Freeing now would corrupt memory, so wait for it to finish first.
     * This wait is deliberately NOT interruptible: it is a resource-reclamation
     * step, not a user-visible blocking point. */
    if (atomic_load_explicit(&t->started, memory_order_acquire)) {
        pthread_mutex_lock(&t->join_mutex);
        while (atomic_load_explicit(&t->state, memory_order_acquire)
               != K_THREAD_TERMINATED) {
            pthread_cond_wait(&t->join_cond, &t->join_mutex);
        }
        pthread_mutex_unlock(&t->join_mutex);
        reap_os_thread(t);
    }

    pthread_mutex_destroy(&t->join_mutex);
    pthread_cond_destroy(&t->join_cond);
    free(t);
}

/* ── Interruption ────────────────────────────────────────────────────────── */

void k_thread_interrupt(KRuntimeThread* t) {
    /* 1. Set the interrupted flag (release). */
    atomic_store_explicit(&t->interrupt.interrupted, 1u,
                          memory_order_release);

    /* 2. Increment futex_word and wake the sleeping thread. */
    atomic_fetch_add_explicit(&t->interrupt.futex_word, 1u,
                               memory_order_release);
    futex_wake((uint32_t*)&t->interrupt.futex_word, 1);

    /* 3. Also broadcast on join_cond in case the thread is joining. */
    pthread_mutex_lock(&t->join_mutex);
    pthread_cond_broadcast(&t->join_cond);
    pthread_mutex_unlock(&t->join_mutex);
}

bool k_thread_is_interrupted(const KRuntimeThread* t) {
    return atomic_load_explicit(&t->interrupt.interrupted,
                                memory_order_acquire) != 0u;
}

bool k_thread_interrupted_and_clear(void) {
    KRuntimeThread* t = k_current_thread;
    if (!t) return false;
    /* atomic exchange: set to 0 and return previous */
    return atomic_exchange_explicit(&t->interrupt.interrupted, 0u,
                                    memory_order_acq_rel) != 0u;
}

int k_thread_check_interrupted(void) {
    KRuntimeThread* t = k_current_thread;
    if (!t) return 0;
    if (!atomic_load_explicit(&t->interrupt.interrupted,
                              memory_order_acquire)) {
        return 0;
    }
    /* Do NOT clear the flag here — clearing is the caller's responsibility. */
    if (k_throw_thread_interruption) {
        k_throw_thread_interruption();
        /* If we return here the K exception is pending on the stack —
           the caller must propagate it. */
    }
    return 1;
}

/* ── Sleep ───────────────────────────────────────────────────────────────── */

int k_thread_sleep_nanos(int64_t nanos) {
    if (nanos <= 0) return 0;

    KRuntimeThread* t = k_current_thread;

    /* Check interrupted before entering sleep. */
    if (t && atomic_load_explicit(&t->interrupt.interrupted,
                                  memory_order_acquire)) {
        return 1;
    }

    /* Compute absolute deadline on CLOCK_MONOTONIC. */
    struct timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec  += nanos / 1000000000LL;
    deadline.tv_nsec += nanos % 1000000000LL;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec  += 1;
        deadline.tv_nsec -= 1000000000L;
    }

    if (t) {
        atomic_store_explicit(&t->state, K_THREAD_SLEEPING,
                              memory_order_release);
    }

    int result = 0;

    if (!t) {
        /* Unregistered thread (e.g. main thread in tests): fall back to a
           simple clock_nanosleep loop without interrupt support. */
        struct timespec now;
        for (;;) {
            clock_gettime(CLOCK_MONOTONIC, &now);
            int64_t remaining_ns =
                (deadline.tv_sec  - now.tv_sec)  * 1000000000LL +
                (deadline.tv_nsec - now.tv_nsec);
            if (remaining_ns <= 0) break;
            struct timespec rel = {
                .tv_sec  = remaining_ns / 1000000000LL,
                .tv_nsec = remaining_ns % 1000000000LL
            };
            clock_nanosleep(CLOCK_MONOTONIC, 0, &rel, NULL);
        }
    } else {
        for (;;) {
            /* Snapshot the futex_word before sleeping. */
            uint32_t snapshot = atomic_load_explicit(&t->interrupt.futex_word,
                                                     memory_order_acquire);

            /* Re-check interrupted (snapshot taken after flag check). */
            if (atomic_load_explicit(&t->interrupt.interrupted,
                                     memory_order_acquire)) {
                result = 1;
                break;
            }

            /* Compute remaining time. */
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            int64_t remaining_ns =
                (deadline.tv_sec  - now.tv_sec)  * 1000000000LL +
                (deadline.tv_nsec - now.tv_nsec);
            if (remaining_ns <= 0) break;  /* timeout reached */

            struct timespec rel = {
                .tv_sec  = remaining_ns / 1000000000LL,
                .tv_nsec = remaining_ns % 1000000000LL
            };

            /* Park on futex until woken or remaining time expires. */
            futex_wait((uint32_t*)&t->interrupt.futex_word, snapshot, &rel);
            /* Loop: check interrupt or recalculate remaining time. */
        }
        atomic_store_explicit(&t->state, K_THREAD_RUNNING,
                              memory_order_release);
    }

    return result;
}

/* ── Join ────────────────────────────────────────────────────────────────── */

int k_thread_join(KRuntimeThread* target, int64_t timeout_nanos) {
    KRuntimeThread* self = k_current_thread;

    /* Pre-check interrupted. */
    if (self && atomic_load_explicit(&self->interrupt.interrupted,
                                     memory_order_acquire)) {
        return 1;
    }

    /* Quick check: already terminated? */
    if (atomic_load_explicit(&target->state, memory_order_acquire)
        == K_THREAD_TERMINATED) {
        reap_os_thread(target);
        return 0;
    }

    if (self) {
        atomic_store_explicit(&self->state, K_THREAD_JOINING,
                              memory_order_release);
    }

    int result = 0;

    pthread_mutex_lock(&target->join_mutex);

    for (;;) {
        KThreadState st = atomic_load_explicit(&target->state,
                                              memory_order_acquire);
        if (st == K_THREAD_TERMINATED) {
            break;
        }

        /* Check interrupt flag. */
        if (self && atomic_load_explicit(&self->interrupt.interrupted,
                                         memory_order_acquire)) {
            result = 1;
            break;
        }

        if (timeout_nanos > 0) {
            struct timespec abs_ts;
            clock_gettime(CLOCK_REALTIME, &abs_ts);
            abs_ts.tv_sec  += timeout_nanos / 1000000000LL;
            abs_ts.tv_nsec += timeout_nanos % 1000000000LL;
            if (abs_ts.tv_nsec >= 1000000000L) {
                abs_ts.tv_sec  += 1;
                abs_ts.tv_nsec -= 1000000000L;
            }
            int rc = pthread_cond_timedwait(&target->join_cond,
                                            &target->join_mutex,
                                            &abs_ts);
            if (rc == ETIMEDOUT) {
                result = 2;
                break;
            }
        } else {
            pthread_cond_wait(&target->join_cond, &target->join_mutex);
        }
    }

    pthread_mutex_unlock(&target->join_mutex);

    if (result == 0) {
        reap_os_thread(target);
    }

    if (self) {
        atomic_store_explicit(&self->state, K_THREAD_RUNNING,
                              memory_order_release);
    }

    return result;
}

/* ── Current thread ─────────────────────────────────────────────────────── */

KRuntimeThread* k_thread_current(void) {
    return k_current_thread;
}

void k_thread_yield(void) {
    sched_yield();
}
