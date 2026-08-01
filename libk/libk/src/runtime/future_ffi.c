/*
 * K Language runtime — Future/Promise FFI wrappers (C)
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
 * These functions are called from future.k via @ffi::Extern("C").
 *
 * Naming convention: __k_future_*
 *
 * NativeFuture:
 *   In K, NativeFuture is an opaque empty struct; NativeFuture* is just a
 *   pointer used as an opaque handle. On the C side it is a KFutureState.
 */

#define _GNU_SOURCE
#include "future_state.h"

#include <stdint.h>
#include <stdbool.h>

typedef KFutureState NativeFuture;

/** Allocate a pending shared state. Returns NULL on allocation failure. */
NativeFuture* __k_future_create(void) {
    return (NativeFuture*)k_future_state_create();
}

/** Release a shared state; the last K handle standing calls this. */
void __k_future_destroy(NativeFuture* native) {
    k_future_state_destroy((KFutureState*)native);
}

/** Take one more handle reference. */
void __k_future_retain(NativeFuture* native) {
    k_future_state_retain((KFutureState*)native);
}

/**
 * Drop one handle reference.
 * @return the new count; 0 means the caller owns the clean-up.
 */
int __k_future_release(NativeFuture* native) {
    return (int)k_future_state_release((KFutureState*)native);
}

/** Lock the K-side handle-chain mutex. */
void __k_future_lock(NativeFuture* native) {
    k_future_state_lock((KFutureState*)native);
}

/** Unlock the K-side handle-chain mutex. */
void __k_future_unlock(NativeFuture* native) {
    k_future_state_unlock((KFutureState*)native);
}

/** Current completion state: 0 pending, 1 success, 2 failed, 3 cancelled. */
int __k_future_state(const NativeFuture* native) {
    return k_future_state_get((const KFutureState*)native);
}

/** True once the state left the pending state. */
bool __k_future_is_done(const NativeFuture* native) {
    return k_future_state_is_done((const KFutureState*)native);
}

/**
 * Publish a terminal state and wake every waiter.
 * @return true when this call performed the transition.
 */
bool __k_future_complete(NativeFuture* native, int completion) {
    return k_future_state_complete((KFutureState*)native, completion);
}

/**
 * Block until completion, interruption or deadline.
 * @param timeout_nanos Negative for an unbounded wait.
 * @return 0 = done, 1 = interrupted, 2 = timeout.
 */
int __k_future_wait(NativeFuture* native, int64_t timeout_nanos) {
    return k_future_state_wait((KFutureState*)native, timeout_nanos);
}
