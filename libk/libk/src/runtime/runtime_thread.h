/*
 * K Language runtime — thread runtime (C header)
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
 * Defines RuntimeThread, InterruptToken, BlockingPoint, and ThreadRegistry.
 * These are the C-level substrate for k::Thread.  All K-visible threading
 * APIs call through the FFI wrappers in thread_ffi.c.
 *
 * Implementation notes:
 *  - One RuntimeThread per OS thread (no virtual threads in Phase 1).
 *  - InterruptToken uses a futex word + atomic flag so that interrupt()
 *    can wake a sleeping thread with no race window.
 *  - BlockingPoint for SLEEP uses clock_nanosleep(CLOCK_MONOTONIC, ...)
 *    and wakes early when interrupted.
 *  - Thread join uses a condition variable on a POSIX mutex.
 */

#ifndef KLANG_RUNTIME_THREAD_H
#define KLANG_RUNTIME_THREAD_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Forward declarations ───────────────────────────────────────────────── */
typedef struct KRuntimeThread   KRuntimeThread;
typedef struct KInterruptToken  KInterruptToken;
typedef struct KBlockingPoint   KBlockingPoint;
typedef struct KThreadRegistry  KThreadRegistry;

/* ── Thread state ───────────────────────────────────────────────────────── */
typedef enum {
    K_THREAD_NEW        = 0,
    K_THREAD_RUNNABLE   = 1,
    K_THREAD_RUNNING    = 2,
    K_THREAD_SLEEPING   = 3,  /* inside Thread.sleep() */
    K_THREAD_JOINING    = 4,  /* inside Thread.join()  */
    K_THREAD_TERMINATED = 5
} KThreadState;

/* ── InterruptToken ─────────────────────────────────────────────────────── */
/*
 * Each RuntimeThread owns one InterruptToken.
 * The 'interrupted' field is the primary flag; it is set with
 * atomic_store(..., memory_order_release) and read with
 * atomic_load(..., memory_order_acquire) at every blocking point.
 *
 * 'futex_word' is used on Linux to implement a lightweight park/unpark:
 *   - sleeping thread calls futex(FUTEX_WAIT) on futex_word when it is 0.
 *   - interrupt() increments futex_word and calls futex(FUTEX_WAKE).
 */
struct KInterruptToken {
    _Atomic uint32_t  interrupted;    /* 0 = not interrupted, 1 = interrupted */
    _Atomic uint32_t  futex_word;     /* park/unpark token                    */
    _Atomic uint64_t  epoch;          /* generation counter (ABA protection)  */
};

/* ── RuntimeThread ──────────────────────────────────────────────────────── */
struct KRuntimeThread {
    uint64_t            thread_id;    /* unique runtime ID                    */
    pthread_t           os_thread;    /* backing OS thread                    */
    KInterruptToken     interrupt;    /* interruption state                   */
    _Atomic KThreadState state;       /* current thread state                 */

    /* Set to 1 once pthread_create() succeeded, so that destroy() knows an OS
     * thread is (or was) backing this object and must be reaped. */
    _Atomic uint32_t    started;
    /* Set to 1 by the first pthread_join(); guarantees the OS thread resource
     * is reaped exactly once even with several joiners plus destroy(). */
    _Atomic uint32_t    os_joined;

    /* join support: target waits here, joiners hold join_mutex */
    pthread_mutex_t     join_mutex;
    pthread_cond_t      join_cond;

    /* pointer to the K-side Thread object (opaque, managed by K GC) */
    void*               k_thread_obj;

    /* task: the function pointer and its argument, set at creation */
    void  (*task_fn)(void* arg);
    void*               task_arg;

    /* link in global thread registry */
    KRuntimeThread*     next;
    KRuntimeThread*     prev;
};

/* ── ThreadRegistry ─────────────────────────────────────────────────────── */
struct KThreadRegistry {
    pthread_mutex_t  mutex;
    KRuntimeThread*  head;
    uint64_t         next_id;
};

/* ── Thread-local current thread pointer ───────────────────────────────── */
extern _Thread_local KRuntimeThread* k_current_thread;

/* ── Registry ───────────────────────────────────────────────────────────── */
KThreadRegistry* k_thread_registry_global(void);
void             k_thread_registry_register(KThreadRegistry* r, KRuntimeThread* t);
void             k_thread_registry_unregister(KThreadRegistry* r, KRuntimeThread* t);
KRuntimeThread*  k_thread_registry_find(KThreadRegistry* r, uint64_t thread_id);

/* ── RuntimeThread lifecycle ────────────────────────────────────────────── */

/**
 * Allocate and initialise a new RuntimeThread.
 * The thread is not started.  task_fn will be called with task_arg when
 * start() is called.
 */
KRuntimeThread* k_runtime_thread_create(void (*task_fn)(void* arg), void* task_arg);

/**
 * Start execution of a previously created RuntimeThread.
 * Returns 0 on success, errno on failure.
 */
int k_runtime_thread_start(KRuntimeThread* t);

/**
 * Called from the OS thread trampoline at thread exit.
 * Transitions state to TERMINATED and wakes all joiners.
 */
void k_runtime_thread_exit(KRuntimeThread* t);

/**
 * Free a RuntimeThread object.
 * If an OS thread was started and has not terminated yet, this blocks
 * (uninterruptibly) until it does, then reaps it.  Freeing a still-running
 * thread would leave it writing into released memory.
 */
void k_runtime_thread_destroy(KRuntimeThread* t);

/* ── Interruption ───────────────────────────────────────────────────────── */

/**
 * Set the interrupted flag on t and wake any blocking point.
 * Thread-safe; may be called from any thread.
 */
void k_thread_interrupt(KRuntimeThread* t);

/**
 * Return true if t's interrupted flag is set.  Does not clear the flag.
 */
bool k_thread_is_interrupted(const KRuntimeThread* t);

/**
 * Read and clear the interrupted flag of the CURRENT thread.
 * Returns true if the flag was set.
 */
bool k_thread_interrupted_and_clear(void);

/**
 * If the current thread's interrupted flag is set, throw
 * ThreadInterruptionException (by calling the registered K throw helper).
 * Returns 0 if not interrupted, 1 if interrupted (caller should propagate).
 */
int k_thread_check_interrupted(void);

/* ── Sleep ──────────────────────────────────────────────────────────────── */

/**
 * Sleep the current thread for the given number of nanoseconds.
 * Returns:
 *   0  — sleep completed normally.
 *   1  — interrupted (interrupted flag was set; flag is NOT cleared here).
 */
int k_thread_sleep_nanos(int64_t nanos);

/* ── Join ───────────────────────────────────────────────────────────────── */

/**
 * Wait for target thread to terminate.
 * timeout_nanos == 0 means wait indefinitely.
 * Returns:
 *   0  — target terminated.
 *   1  — interrupted.
 *   2  — timeout elapsed.
 */
int k_thread_join(KRuntimeThread* target, int64_t timeout_nanos);

/* ── Current thread ─────────────────────────────────────────────────────── */

/** Return the RuntimeThread for the calling OS thread. */
KRuntimeThread* k_thread_current(void);

/** Yield the CPU (scheduler hint). */
void k_thread_yield(void);

/* ── K-side throw hook ──────────────────────────────────────────────────── */

/*
 * The K runtime installs a function pointer here so that C runtime code
 * can trigger a K-level ThreadInterruptionException without depending on
 * the K ABI directly.  If not installed, k_thread_check_interrupted returns
 * 1 and the caller is responsible for propagating the exception.
 */
extern void (*k_throw_thread_interruption)(void);

#ifdef __cplusplus
}
#endif

#endif /* KLANG_RUNTIME_THREAD_H */
