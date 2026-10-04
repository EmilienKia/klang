/*
 * K Language runtime — shared Future state and producer cancellation hooks
 * Copyright 2023-2026 Emilien Kia
 * Licensed under the Apache License, Version 2.0.
 */
#define _GNU_SOURCE
#include "future_state.h"
#include "park_lot.h"
#include <stdatomic.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>

struct KFutureState {
    _Atomic unsigned refs;
    _Atomic int completion;
    KParkLot waiters;
    pthread_mutex_t chain_mutex;
    pthread_mutex_t hooks_mutex;
    KFutureCancelHook cancel_hook;
    KFutureQuiescentHook quiescent_hook;
    bool has_cancel_hook;
    bool has_quiescent_hook;
    bool cancel_claimed;
    bool cancel_deferred;
    pid_t creator_pid;
};
KFutureState* k_future_state_create(void) {
    KFutureState* st = calloc(1, sizeof(*st));
    if (!st) return NULL;
    atomic_init(&st->refs, 1u);
    st->creator_pid = getpid();
    atomic_init(&st->completion, K_FUTURE_PENDING);
    if (k_park_lot_init(&st->waiters)) {
        free(st);
        return NULL;
    }
    pthread_mutex_init(&st->chain_mutex, NULL);
    pthread_mutex_init(&st->hooks_mutex, NULL);
    return st;
}
void k_future_state_destroy(KFutureState* st) {
    if (!st || st->creator_pid != getpid()) return;
    if (st->has_cancel_hook && st->cancel_hook.drop)
        st->cancel_hook.drop(st->cancel_hook.context);
    if (st->has_quiescent_hook && st->quiescent_hook.drop)
        st->quiescent_hook.drop(st->quiescent_hook.context);
    pthread_mutex_destroy(&st->hooks_mutex);
    pthread_mutex_destroy(&st->chain_mutex);
    k_park_lot_destroy(&st->waiters);
    free(st);
}
void k_future_state_retain(KFutureState* st) {
    if (st) atomic_fetch_add_explicit(&st->refs, 1u, memory_order_relaxed);
}
unsigned k_future_state_release(KFutureState* st) {
    if (st && st->creator_pid != getpid()) return 1u;
    return st ? atomic_fetch_sub_explicit(&st->refs, 1u, memory_order_acq_rel) - 1u : 0u;
}
void k_future_state_lock(KFutureState* st) {
    if (st && st->creator_pid == getpid()) pthread_mutex_lock(&st->chain_mutex);
}
void k_future_state_unlock(KFutureState* st) {
    if (st && st->creator_pid == getpid()) pthread_mutex_unlock(&st->chain_mutex);
}
int k_future_state_get(const KFutureState* st) {
    return st ? atomic_load_explicit(&st->completion, memory_order_acquire) : K_FUTURE_PENDING;
}
bool k_future_state_is_done(const KFutureState* st) {
    return k_future_state_get(st) != K_FUTURE_PENDING;
}
bool k_future_state_complete(KFutureState* st, int completion) {
    if (!st || st->creator_pid != getpid() ||
        completion <= K_FUTURE_PENDING || completion > K_FUTURE_CANCELLED)
        return false;
    int expected = K_FUTURE_PENDING;
    if (!atomic_compare_exchange_strong_explicit(&st->completion, &expected, completion,
                                                 memory_order_release, memory_order_acquire))
        return false;
    pthread_mutex_lock(&st->waiters.lock);
    k_park_lot_wake_all(&st->waiters);
    pthread_mutex_unlock(&st->waiters.lock);
    KFutureCancelHook hook = {0};
    pthread_mutex_lock(&st->hooks_mutex);
    if (st->has_cancel_hook) {
        hook = st->cancel_hook;
        st->has_cancel_hook = false;
    }
    pthread_mutex_unlock(&st->hooks_mutex);
    if (hook.drop) hook.drop(hook.context);
    /* Keep the quiescence hook until the last handle: cancellation publishes
     * BEFORE the target CQE and interruption wrappers must still drain it. */
    return true;
}
static bool future_ready(void* raw) { return k_future_state_is_done(raw); }
int k_future_state_wait(KFutureState* st, int64_t timeout) {
    if (!st) return K_FUTURE_WAIT_DONE;
    if (st->creator_pid != getpid()) return K_FUTURE_WAIT_UNSUPPORTED;
    return k_park_until(&st->waiters, NULL, future_ready, NULL, st, timeout, true);
}
void k_future_state_set_cancel_hook(KFutureState* st, void* ctx,
                                    bool (*cancel)(void*), void (*drop)(void*)) {
    if (!st || st->creator_pid != getpid()) {
        if (drop) drop(ctx);
        return;
    }
    pthread_mutex_lock(&st->hooks_mutex);
    bool rejected = k_future_state_is_done(st) || st->has_cancel_hook || st->cancel_claimed;
    if (!rejected) {
        st->cancel_hook = (KFutureCancelHook){ctx, cancel, drop};
        st->has_cancel_hook = true;
    }
    pthread_mutex_unlock(&st->hooks_mutex);
    if (rejected && drop) drop(ctx);
}
void k_future_state_set_quiescent_hook(KFutureState* st, void* ctx,
                                       int (*wait)(void*, int64_t), void (*drop)(void*)) {
    if (!st || st->creator_pid != getpid()) {
        if (drop) drop(ctx);
        return;
    }
    pthread_mutex_lock(&st->hooks_mutex);
    bool rejected = st->has_quiescent_hook;
    if (!rejected) {
        st->quiescent_hook = (KFutureQuiescentHook){ctx, wait, drop};
        st->has_quiescent_hook = true;
    }
    pthread_mutex_unlock(&st->hooks_mutex);
    if (rejected && drop) drop(ctx);
}
void k_future_state_defer_cancellation(KFutureState* st) {
    if (!st || st->creator_pid != getpid()) return;
    pthread_mutex_lock(&st->hooks_mutex);
    st->cancel_deferred = true;
    pthread_mutex_unlock(&st->hooks_mutex);
}
bool k_future_state_cancel(KFutureState* st) {
    if (!st || st->creator_pid != getpid()) return false;
    pthread_mutex_lock(&st->hooks_mutex);
    if (k_future_state_is_done(st) || st->cancel_claimed) {
        pthread_mutex_unlock(&st->hooks_mutex);
        return false;
    }
    st->cancel_claimed = true;
    KFutureCancelHook hook = st->has_cancel_hook ? st->cancel_hook : (KFutureCancelHook){0};
    bool deferred = st->cancel_deferred && hook.cancel;
    st->has_cancel_hook = false;
    pthread_mutex_unlock(&st->hooks_mutex);
    bool accepted = !hook.cancel || hook.cancel(hook.context);
    if (hook.drop) hook.drop(hook.context);
    return accepted && (deferred || k_future_state_complete(st, K_FUTURE_CANCELLED));
}
int k_future_state_wait_quiescent(KFutureState* st, int64_t timeout) {
    if (!st) return 0;
    if (st->creator_pid != getpid()) return ENOTSUP;
    pthread_mutex_lock(&st->hooks_mutex);
    KFutureQuiescentHook hook = st->has_quiescent_hook
                              ? st->quiescent_hook : (KFutureQuiescentHook){0};
    pthread_mutex_unlock(&st->hooks_mutex);
    /* The caller owns a shared-state reference throughout this call. */
    return hook.wait_quiescent ? hook.wait_quiescent(hook.context, timeout) : 0;
}
