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

#ifndef KLANG_RUNTIME_FUTURE_STATE_H
#define KLANG_RUNTIME_FUTURE_STATE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Completion states ──────────────────────────────────────────────────── */

typedef enum {
    K_FUTURE_PENDING   = 0,
    K_FUTURE_SUCCESS   = 1,
    K_FUTURE_FAILED    = 2,
    K_FUTURE_CANCELLED = 3
} KFutureCompletion;

/* ── Wait outcomes ──────────────────────────────────────────────────────── */

typedef enum {
    K_FUTURE_WAIT_DONE        = 0,  /* the future reached a terminal state    */
    K_FUTURE_WAIT_INTERRUPTED = 1,  /* the calling thread has been interrupted*/
    K_FUTURE_WAIT_TIMEOUT     = 2   /* the deadline expired while pending     */
} KFutureWaitResult;

/*
 * KFutureState — synchronisation state shared by one Promise and its Futures.
 *
 * The state deliberately holds no payload: the produced value and the failure
 * cause live in K memory (a FutureBox<T> owned by the K-side handle group).
 * This substrate only owns:
 *   - the atomic completion word, published exactly once,
 *   - the mutex / condition variable pair used by blocked waiters,
 *   - a mutex guarding the K-side handle chain.
 *
 * Publication protocol: the producer fills the K payload first, then calls
 * k_future_state_complete(), which performs a release store of the completion
 * word. Consumers observe the completion word with an acquire load before
 * reading the payload, so the payload writes are visible to them.
 */
typedef struct KFutureState KFutureState;

/** Allocate a pending state. Returns NULL on allocation failure. */
KFutureState* k_future_state_create(void);

/** Destroy a state. The caller must guarantee that no thread still uses it. */
void k_future_state_destroy(KFutureState* st);

/* ── Handle reference counting ──────────────────────────────────────────── */
/*
 * A freshly created state has a reference count of 1. Every K handle
 * (Promise<T> or Future<T>) sharing the state holds one reference. The K
 * payload — the FutureBox<T> holding the produced value or the failure cause —
 * is deleted by whichever handle brings the count down to zero, which then
 * destroys the state as well.
 *
 * The count is atomic, so handles may be dropped concurrently from several
 * threads.
 */

/** Take one more reference. */
void k_future_state_retain(KFutureState* st);

/** Drop one reference. @return the new count; 0 means the caller must clean up. */
unsigned k_future_state_release(KFutureState* st);

/* ── Handle-chain mutex ─────────────────────────────────────────────────── */
/*
 * The K side keeps the Promise and its Futures in an intrusive doubly-linked
 * chain, so that the last handle standing can release the shared payload.
 * Chain surgery happens in K but must be serialised; these two entry points
 * provide the lock. The lock is NOT the same as the completion lock, so K may
 * hold it while calling any other function of this API.
 */
void k_future_state_lock(KFutureState* st);
void k_future_state_unlock(KFutureState* st);

/* ── Completion ─────────────────────────────────────────────────────────── */

/** @return The current completion state (acquire load). */
int  k_future_state_get(const KFutureState* st);

/** @return true when the state left K_FUTURE_PENDING. */
bool k_future_state_is_done(const KFutureState* st);

/**
 * Publish a terminal completion state and wake every waiter.
 *
 * @param completion One of K_FUTURE_SUCCESS / FAILED / CANCELLED.
 * @return true when this call performed the transition, false when the state
 *         was already terminal (the caller then keeps ownership of whatever
 *         payload it intended to publish).
 */
bool k_future_state_complete(KFutureState* st, int completion);

/* ── Waiting ────────────────────────────────────────────────────────────── */

/**
 * Block until the state becomes terminal, the calling thread is interrupted,
 * or the deadline expires.
 *
 * Interruption is only observable for threads created through the K threading
 * substrate; the process main thread has no interrupt token and can only be
 * released by completion or by the timeout.
 *
 * @param timeout_nanos Negative for an unbounded wait, otherwise the maximum
 *                      duration to block, measured on the monotonic clock.
 * @return One of the KFutureWaitResult values. Completion always wins over a
 *         pending interruption, so a value that arrived before the interrupt
 *         is never lost; the interrupted flag itself is left untouched.
 */
int k_future_state_wait(KFutureState* st, int64_t timeout_nanos);

#ifdef __cplusplus
}
#endif

#endif /* KLANG_RUNTIME_FUTURE_STATE_H */
