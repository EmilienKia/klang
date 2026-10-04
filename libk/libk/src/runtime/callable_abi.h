/*
 * K Language runtime — Transport ABI for K callables across FFI
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

#ifndef KLANG_RUNTIME_CALLABLE_ABI_H
#define KLANG_RUNTIME_CALLABLE_ABI_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Borrowed K callable transport, matching *(Params) as a two-word value.
 * The proactor bridges accept only free/static invoke functions: the explicit
 * carrier below supplies their context separately. This is not a raw C function
 * pointer, and an owned callable must never be transported without its carrier.
 */
typedef struct KCallable {
    void* fn;
    void* ctx;
} KCallable;

/* Rejection must still call the explicitly supplied cleanup exactly once,
 * including when rejecting a bound callable rather than a static trampoline. */
static inline void k_callable_drop(KCallable drop, void* context) {
    if (!drop.fn) return;
    if (drop.ctx) {
        ((void (*)(void*, void*))drop.fn)(drop.ctx, context);
    } else {
        ((void (*)(void*))drop.fn)(context);
    }
}

/*
 * KOwnedCallback — transport carrier for K callables passed across the FFI.
 *
 * A K callable may be a free function, a bound method, a functor, or an
 * owned closure. To avoid leaking or double-freeing closures and capture
 * frames, the runtime never attempts to deduce a destructor from `context`.
 * Instead, the caller provides an explicit carrier:
 *
 *   - `context`: opaque payload pointer (closure frame, object pointer, etc.)
 *   - `invoke`:  function pointer to execute the callback with the result.
 *   - `drop`:    function pointer to destroy/release the context.
 *
 * Contract:
 *   The runtime guarantees that `drop` will be invoked EXACTLY ONCE:
 *   - after successful callback execution on the executor,
 *   - on callback suppression (cancelled before execution began),
 *   - on callback rejection (executor rejected the task),
 *   - or during runtime shutdown/drain for any unhandled operations.
 *   If `drop` is NULL, it is treated as a no-op.
 */
typedef struct KOwnedCallback {
    void* context;
    void  (*invoke)(void* context, void* result);
    void  (*drop)(void* context);
} KOwnedCallback;

#ifdef __cplusplus
}
#endif

#endif /* KLANG_RUNTIME_CALLABLE_ABI_H */
