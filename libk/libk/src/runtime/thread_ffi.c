/*
 * K Language runtime — Thread FFI wrappers (C)
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
 * These functions are called from thread.k via @ffi::Extern("C").
 * They translate K-level thread operations into KRuntimeThread calls.
 *
 * Naming convention: __k_thread_*
 *
 * NativeThread:
 *   In K, NativeThread is an opaque empty struct; NativeThread* is just a
 *   pointer used as an opaque handle.  On the C side we typedef it to
 *   KRuntimeThread so casts are explicit and type-safe within this file.
 *
 * Runnable invocation:
 *   __k_thread_create() captures a single K callable (the fat `{ fn, ctx }`
 *   reference) and invokes it from the OS thread via runnable_task_fn().  The
 *   virtual dispatch of run() is therefore performed in K, never by decoding
 *   the vtable from C.
 */

#define _GNU_SOURCE
#include "runtime_thread.h"

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

/* On the C side, NativeThread* == KRuntimeThread* (opaque handle). */
typedef KRuntimeThread NativeThread;

/* ── Runnable task trampoline ────────────────────────────────────────────── */

/*
 * Runnable invocation:
 *   The OS thread trampoline cannot perform a K virtual dispatch by itself.
 *   Hard-coding the vtable slot of Runnable::run() here would be silently
 *   broken by any new virtual method added to ::k::Object or ::k::Runnable.
 *   Instead, Thread(Runnable*) passes a K *callable* bound to the Thread itself
 *   (`this.runGuarded`); the trampoline simply calls it back, and K performs the
 *   virtual dispatch of Runnable::run() — and catches anything escaping it.
 *
 *   A K callable is the fat reference `{ void* fn; void* ctx; }`.  A null ctx
 *   denotes a free function or a static method, called as `fn(args…)`; a
 *   non-null one is the receiver and is passed first, as `fn(ctx, args…)`.
 *   Both fields travel in registers, exactly like the two-word struct below.
 */
typedef struct {
    void* fn;
    void* ctx;
} KCallable;

typedef void (*KCallableFreeFn)(void);
typedef void (*KCallableBoundFn)(void* ctx);

static void runnable_task_fn(void* arg) {
    KCallable* stored = (KCallable*)arg;
    KCallable  run    = *stored;
    free(stored);

    if (!run.fn) return;
    if (run.ctx) {
        ((KCallableBoundFn)run.fn)(run.ctx);
    } else {
        ((KCallableFreeFn)run.fn)();
    }
}

/* ── Public FFI functions ────────────────────────────────────────────────── */

/**
 * Allocate a new KRuntimeThread for a K thread body.
 * Returns NativeThread* (== KRuntimeThread*) stored in Thread._native.
 * @param run  K callable { fn, ctx } that runs the thread body (Thread.runGuarded,
 *             bound to the owning Thread, which performs the virtual dispatch of
 *             Runnable::run() and swallows any escaping Throwable).
 */
NativeThread* __k_thread_create(KCallable run) {
    KCallable* ta = (KCallable*)malloc(sizeof(KCallable));
    if (!ta) return NULL;
    *ta = run;

    KRuntimeThread* t = k_runtime_thread_create(runnable_task_fn, ta);
    if (!t) { free(ta); return NULL; }
    return (NativeThread*)t;
}

/**
 * Start the thread.  Returns 0 on success, non-zero on OS error.
 */
int __k_thread_start(NativeThread* native) {
    return k_runtime_thread_start((KRuntimeThread*)native);
}

/**
 * Block until the target thread terminates.
 * Returns: 0 = joined, 1 = interrupted.
 */
int __k_thread_join(NativeThread* native) {
    return k_thread_join((KRuntimeThread*)native, 0LL);
}

/**
 * Block until the target thread terminates or timeout_nanos elapses.
 * Returns: 0 = joined, 1 = interrupted, 2 = timeout.
 */
int __k_thread_join_timed(NativeThread* native, int64_t timeout_nanos) {
    return k_thread_join((KRuntimeThread*)native, timeout_nanos);
}

/**
 * Set the interrupted flag and wake the target thread.
 */
void __k_thread_interrupt(NativeThread* native) {
    if (native) k_thread_interrupt((KRuntimeThread*)native);
}

/**
 * Return true if the interrupted flag is set (does not clear it).
 */
bool __k_thread_is_interrupted(const NativeThread* native) {
    return native ? k_thread_is_interrupted((const KRuntimeThread*)native) : false;
}

/**
 * Read and clear the current thread's interrupted flag.
 */
bool __k_thread_interrupted_and_clear(void) {
    return k_thread_interrupted_and_clear();
}

/**
 * Sleep the current thread for nanos nanoseconds.
 * Returns 0 = normal, 1 = interrupted.
 */
int __k_thread_sleep_nanos(int64_t nanos) {
    return k_thread_sleep_nanos(nanos);
}

/** Yield the CPU. */
void __k_thread_yield(void) {
    k_thread_yield();
}

/** Return the current thread's NativeThread handle. */
NativeThread* __k_thread_current_native(void) {
    return (NativeThread*)k_thread_current();
}

/**
 * Destroy a KRuntimeThread.  Must only be called after the thread has
 * terminated.
 */
void __k_thread_destroy(NativeThread* native) {
    if (native) k_runtime_thread_destroy((KRuntimeThread*)native);
}

/**
 * Register the calling (main) OS thread as a KRuntimeThread.
 * Safe to call multiple times; returns the existing handle on re-entry.
 */
NativeThread* __k_thread_register_main(void) {
    if (k_current_thread) return (NativeThread*)k_current_thread;

    KRuntimeThread* t = k_runtime_thread_create(NULL, NULL);
    if (!t) return NULL;

    atomic_store_explicit(&t->state, K_THREAD_RUNNING, memory_order_release);
    t->os_thread    = pthread_self();
    k_current_thread = t;
    return (NativeThread*)t;
}
