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
 * A park lot is an internal mutex guarding some shared state together with the
 * set of threads currently blocked on it.
 *
 * Waiters park on the futex word of their own KInterruptToken — the very word
 * k_thread_interrupt() bumps — which is what makes every wait built on this
 * helper interruptible without any dedicated wake-up channel.  A thread
 * without a runtime token (e.g. the process main thread) polls instead, and is
 * therefore not interruptible.
 *
 * This module is shared by the synchronisation primitives (sync_primitives.c)
 * and by the asynchronous I/O substrate (async_io.c).
 */

#ifndef KLANG_PARK_LOT_H
#define KLANG_PARK_LOT_H

#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>

#include "runtime_thread.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Outcome of a park operation; mirrors the K_SYNC_* result codes. */
#define K_PARK_OK           0
#define K_PARK_INTERRUPTED  1
#define K_PARK_TIMEOUT      2

typedef struct {
    pthread_mutex_t  lock;
    KRuntimeThread** waiters;
    unsigned         count;
    unsigned         capacity;
} KParkLot;

/** Return the current CLOCK_MONOTONIC reading in nanoseconds. */
int64_t k_monotonic_nanos(void);

/** Wake a single thread parked on `addr`. */
int k_futex_wake_word(uint32_t* addr, int n);

/** Initialise a park lot.  Returns 0 on success, errno otherwise. */
int  k_park_lot_init(KParkLot* lot);

/** Release every resource held by a park lot. */
void k_park_lot_destroy(KParkLot* lot);

/**
 * Wake every parked thread.  The caller must hold `lot->lock`, which is what
 * guarantees the absence of lost wake-ups: a waiter samples its park
 * generation while holding the same lock.
 */
void k_park_lot_wake_all(KParkLot* lot);

typedef bool (*k_ready_fn)(void* ctx);
typedef void (*k_hook_fn)(void* ctx);
typedef void (*k_exit_fn)(void* ctx, int result);

/**
 * Block the calling thread until `ready` returns true, the calling thread is
 * interrupted (when `interruptible`), or `timeout_nanos` elapses (when it is
 * non-negative).
 *
 * `on_enter` (optional) runs once under the lot lock before the first check.
 * `ready` runs under the lot lock and returns true when the wait can end; it
 * may perform the state mutation that corresponds to acquiring the resource.
 * `on_exit` (optional) runs under the lot lock with the final outcome, so a
 * caller can undo a registration performed by `on_enter`.
 *
 * Returns K_PARK_OK, K_PARK_INTERRUPTED or K_PARK_TIMEOUT.
 */
int k_park_until(KParkLot* lot,
                 k_hook_fn on_enter,
                 k_ready_fn ready,
                 k_exit_fn on_exit,
                 void* ctx,
                 int64_t timeout_nanos,
                 bool interruptible);

#ifdef __cplusplus
}
#endif

#endif /* KLANG_PARK_LOT_H */
