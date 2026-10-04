/*
 * K Language runtime — Future/Promise FFI wrappers
 * Copyright 2023-2026 Emilien Kia
 * Licensed under the Apache License, Version 2.0.
 */
#include "future_state.h"
#include <stdint.h>
#include <stdbool.h>

typedef KFutureState NativeFuture;
NativeFuture* __k_future_create(void) { return k_future_state_create(); }
void __k_future_destroy(NativeFuture* st) { k_future_state_destroy(st); }
void __k_future_retain(NativeFuture* st) { k_future_state_retain(st); }
int __k_future_release(NativeFuture* st) { return (int)k_future_state_release(st); }
void __k_future_lock(NativeFuture* st) { k_future_state_lock(st); }
void __k_future_unlock(NativeFuture* st) { k_future_state_unlock(st); }
int __k_future_state(const NativeFuture* st) { return k_future_state_get(st); }
bool __k_future_is_done(const NativeFuture* st) { return k_future_state_is_done(st); }
bool __k_future_complete(NativeFuture* st, int completion) {
    return k_future_state_complete(st, completion);
}
int __k_future_wait(NativeFuture* st, int64_t timeout) {
    return k_future_state_wait(st, timeout);
}
bool __k_future_cancel(NativeFuture* st) { return k_future_state_cancel(st); }
int __k_future_wait_quiescent(NativeFuture* st, int64_t timeout) {
    return k_future_state_wait_quiescent(st, timeout);
}
void __k_future_set_cancel_hook(NativeFuture* st, void* ctx,
                                bool (*cancel)(void*), void (*drop)(void*)) {
    k_future_state_set_cancel_hook(st, ctx, cancel, drop);
}
void __k_future_set_quiescent_hook(NativeFuture* st, void* ctx,
                                   int (*wait)(void*, int64_t), void (*drop)(void*)) {
    k_future_state_set_quiescent_hook(st, ctx, wait, drop);
}
