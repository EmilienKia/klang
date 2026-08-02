/*
 * K Language runtime — synchronisation primitives (C substrate)
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
#include "sync_primitives.h"
#include "runtime_thread.h"
#include "park_lot.h"

/* The interruptible park lot lives in park_lot.c; keep the historical short
 * names used throughout this file. */
#define park_lot_init      k_park_lot_init
#define park_lot_destroy   k_park_lot_destroy
#define park_lot_wake_all  k_park_lot_wake_all
#define park_until         k_park_until
#define monotonic_nanos    k_monotonic_nanos

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <pthread.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <unistd.h>

/* ── Thread identity ────────────────────────────────────────────────────── */

static _Thread_local uint64_t sp_tls_thread_id = 0u;
static _Atomic uint64_t       sp_next_thread_id = 1u;

uint64_t k_sync_current_thread_id(void) {
    if (sp_tls_thread_id == 0u) {
        sp_tls_thread_id = atomic_fetch_add_explicit(&sp_next_thread_id, 1u,
                                                     memory_order_relaxed);
    }
    return sp_tls_thread_id;
}

/* ── Mutex ──────────────────────────────────────────────────────────────── */

struct KMutex {
    KParkLot  lot;
    uint64_t  owner;      /* sync thread id, 0 when free */
    int       hold_count; /* >1 only for reentrant mutexes */
    bool      reentrant;
};

KMutex* k_mutex_create(bool reentrant) {
    KMutex* m = (KMutex*)calloc(1, sizeof(KMutex));
    if (m == NULL) {
        return NULL;
    }
    if (park_lot_init(&m->lot) != 0) {
        free(m);
        return NULL;
    }
    m->reentrant = reentrant;
    return m;
}

void k_mutex_destroy(KMutex* m) {
    if (m == NULL) {
        return;
    }
    park_lot_destroy(&m->lot);
    free(m);
}

/* Caller must hold m->lot.lock. */
static bool mutex_try_acquire_locked(KMutex* m) {
    uint64_t self = k_sync_current_thread_id();
    if (m->owner == 0u) {
        m->owner = self;
        m->hold_count = 1;
        return true;
    }
    if (m->reentrant && m->owner == self) {
        m->hold_count++;
        return true;
    }
    return false;
}

static bool mutex_ready(void* ctx) {
    return mutex_try_acquire_locked((KMutex*)ctx);
}

void k_mutex_lock(KMutex* m) {
    if (m == NULL) {
        return;
    }
    park_until(&m->lot, NULL, mutex_ready, NULL, m, -1, false);
}

int k_mutex_lock_interruptibly(KMutex* m, int64_t timeout_nanos) {
    if (m == NULL) {
        return K_SYNC_ILLEGAL;
    }
    return park_until(&m->lot, NULL, mutex_ready, NULL, m, timeout_nanos, true);
}

bool k_mutex_try_lock(KMutex* m) {
    if (m == NULL) {
        return false;
    }
    pthread_mutex_lock(&m->lot.lock);
    bool ok = mutex_try_acquire_locked(m);
    pthread_mutex_unlock(&m->lot.lock);
    return ok;
}

int k_mutex_unlock(KMutex* m) {
    if (m == NULL) {
        return K_SYNC_ILLEGAL;
    }
    int result = K_SYNC_OK;
    pthread_mutex_lock(&m->lot.lock);
    if (m->owner != k_sync_current_thread_id()) {
        result = K_SYNC_ILLEGAL;
    } else if (--m->hold_count == 0) {
        m->owner = 0u;
        park_lot_wake_all(&m->lot);
    }
    pthread_mutex_unlock(&m->lot.lock);
    return result;
}

bool k_mutex_is_held_by_current(const KMutex* m) {
    if (m == NULL) {
        return false;
    }
    KMutex* mm = (KMutex*)m;
    pthread_mutex_lock(&mm->lot.lock);
    bool held = mm->owner == k_sync_current_thread_id();
    pthread_mutex_unlock(&mm->lot.lock);
    return held;
}

int k_mutex_hold_count(const KMutex* m) {
    if (m == NULL) {
        return 0;
    }
    KMutex* mm = (KMutex*)m;
    pthread_mutex_lock(&mm->lot.lock);
    int n = mm->owner == k_sync_current_thread_id() ? mm->hold_count : 0;
    pthread_mutex_unlock(&mm->lot.lock);
    return n;
}

/* Fully release the mutex, remembering the hold count. Returns -1 if the
 * calling thread does not own it. */
static int mutex_release_fully(KMutex* m) {
    pthread_mutex_lock(&m->lot.lock);
    if (m->owner != k_sync_current_thread_id()) {
        pthread_mutex_unlock(&m->lot.lock);
        return -1;
    }
    int saved = m->hold_count;
    m->owner = 0u;
    m->hold_count = 0;
    park_lot_wake_all(&m->lot);
    pthread_mutex_unlock(&m->lot.lock);
    return saved;
}

static void mutex_reacquire(KMutex* m, int hold_count) {
    park_until(&m->lot, NULL, mutex_ready, NULL, m, -1, false);
    pthread_mutex_lock(&m->lot.lock);
    m->hold_count = hold_count;
    pthread_mutex_unlock(&m->lot.lock);
}

/* ── Condition ──────────────────────────────────────────────────────────── */

struct KCondition {
    KParkLot  lot;
    KMutex*   assoc;
    uint64_t  broadcast_gen; /* bumped by signal_all */
    uint64_t  tickets;       /* pending single signals */
};

typedef struct {
    KCondition* cond;
    uint64_t    entry_gen;
    KMutex*     mutex;
    int         saved_holds;
    bool        released;
} CondWaitCtx;

KCondition* k_condition_create(KMutex* assoc) {
    if (assoc == NULL) {
        return NULL;
    }
    KCondition* c = (KCondition*)calloc(1, sizeof(KCondition));
    if (c == NULL) {
        return NULL;
    }
    if (park_lot_init(&c->lot) != 0) {
        free(c);
        return NULL;
    }
    c->assoc = assoc;
    return c;
}

void k_condition_destroy(KCondition* c) {
    if (c == NULL) {
        return;
    }
    park_lot_destroy(&c->lot);
    free(c);
}

static void cond_on_enter(void* raw) {
    CondWaitCtx* ctx = (CondWaitCtx*)raw;
    ctx->entry_gen = ctx->cond->broadcast_gen;
    /* Release the associated mutex only once the condition lot is held, so a
     * signal issued by the thread that takes the mutex next cannot be missed. */
    ctx->saved_holds = mutex_release_fully(ctx->mutex);
    ctx->released = ctx->saved_holds >= 0;
}

static bool cond_ready(void* raw) {
    CondWaitCtx* ctx = (CondWaitCtx*)raw;
    if (!ctx->released) {
        /* The caller did not own the mutex: end the wait immediately, the
         * outcome is rewritten to K_SYNC_ILLEGAL by the caller. */
        return true;
    }
    if (ctx->cond->broadcast_gen != ctx->entry_gen) {
        return true;
    }
    if (ctx->cond->tickets > 0u) {
        ctx->cond->tickets--;
        return true;
    }
    return false;
}

int k_condition_await(KCondition* c, int64_t timeout_nanos, bool interruptible) {
    if (c == NULL || c->assoc == NULL) {
        return K_SYNC_ILLEGAL;
    }
    if (!k_mutex_is_held_by_current(c->assoc)) {
        return K_SYNC_ILLEGAL;
    }

    CondWaitCtx ctx;
    ctx.cond        = c;
    ctx.entry_gen   = 0u;
    ctx.mutex       = c->assoc;
    ctx.saved_holds = -1;
    ctx.released    = false;

    int result = park_until(&c->lot, cond_on_enter, cond_ready, NULL, &ctx,
                            timeout_nanos, interruptible);

    if (!ctx.released) {
        return K_SYNC_ILLEGAL;
    }
    /* The mutex is always re-acquired before returning, whatever the outcome. */
    mutex_reacquire(ctx.mutex, ctx.saved_holds);
    return result;
}

int k_condition_signal(KCondition* c) {
    if (c == NULL || c->assoc == NULL) {
        return K_SYNC_ILLEGAL;
    }
    if (!k_mutex_is_held_by_current(c->assoc)) {
        return K_SYNC_ILLEGAL;
    }
    pthread_mutex_lock(&c->lot.lock);
    c->tickets++;
    park_lot_wake_all(&c->lot);
    pthread_mutex_unlock(&c->lot.lock);
    return K_SYNC_OK;
}

int k_condition_signal_all(KCondition* c) {
    if (c == NULL || c->assoc == NULL) {
        return K_SYNC_ILLEGAL;
    }
    if (!k_mutex_is_held_by_current(c->assoc)) {
        return K_SYNC_ILLEGAL;
    }
    pthread_mutex_lock(&c->lot.lock);
    c->broadcast_gen++;
    park_lot_wake_all(&c->lot);
    pthread_mutex_unlock(&c->lot.lock);
    return K_SYNC_OK;
}

/* ── Semaphore ──────────────────────────────────────────────────────────── */

struct KSemaphore {
    KParkLot lot;
    int32_t  permits;
};

typedef struct {
    KSemaphore* sem;
    int32_t     want;
} SemCtx;

KSemaphore* k_semaphore_create(int32_t permits) {
    KSemaphore* s = (KSemaphore*)calloc(1, sizeof(KSemaphore));
    if (s == NULL) {
        return NULL;
    }
    if (park_lot_init(&s->lot) != 0) {
        free(s);
        return NULL;
    }
    s->permits = permits;
    return s;
}

void k_semaphore_destroy(KSemaphore* s) {
    if (s == NULL) {
        return;
    }
    park_lot_destroy(&s->lot);
    free(s);
}

static bool sem_ready(void* raw) {
    SemCtx* ctx = (SemCtx*)raw;
    if (ctx->sem->permits >= ctx->want) {
        ctx->sem->permits -= ctx->want;
        return true;
    }
    return false;
}

int k_semaphore_acquire(KSemaphore* s, int32_t permits, int64_t timeout_nanos) {
    if (s == NULL || permits < 0) {
        return K_SYNC_ILLEGAL;
    }
    SemCtx ctx = { s, permits };
    return park_until(&s->lot, NULL, sem_ready, NULL, &ctx, timeout_nanos, true);
}

bool k_semaphore_try_acquire(KSemaphore* s, int32_t permits) {
    if (s == NULL || permits < 0) {
        return false;
    }
    SemCtx ctx = { s, permits };
    pthread_mutex_lock(&s->lot.lock);
    bool ok = sem_ready(&ctx);
    pthread_mutex_unlock(&s->lot.lock);
    return ok;
}

void k_semaphore_release(KSemaphore* s, int32_t permits) {
    if (s == NULL || permits <= 0) {
        return;
    }
    pthread_mutex_lock(&s->lot.lock);
    s->permits += permits;
    park_lot_wake_all(&s->lot);
    pthread_mutex_unlock(&s->lot.lock);
}

int32_t k_semaphore_available(const KSemaphore* s) {
    if (s == NULL) {
        return 0;
    }
    KSemaphore* ss = (KSemaphore*)s;
    pthread_mutex_lock(&ss->lot.lock);
    int32_t n = ss->permits;
    pthread_mutex_unlock(&ss->lot.lock);
    return n;
}

int32_t k_semaphore_drain(KSemaphore* s) {
    if (s == NULL) {
        return 0;
    }
    pthread_mutex_lock(&s->lot.lock);
    int32_t n = s->permits > 0 ? s->permits : 0;
    s->permits -= n;
    pthread_mutex_unlock(&s->lot.lock);
    return n;
}

/* ── CountDownLatch ─────────────────────────────────────────────────────── */

struct KLatch {
    KParkLot lot;
    int64_t  count;
};

KLatch* k_latch_create(int64_t count) {
    KLatch* l = (KLatch*)calloc(1, sizeof(KLatch));
    if (l == NULL) {
        return NULL;
    }
    if (park_lot_init(&l->lot) != 0) {
        free(l);
        return NULL;
    }
    l->count = count < 0 ? 0 : count;
    return l;
}

void k_latch_destroy(KLatch* l) {
    if (l == NULL) {
        return;
    }
    park_lot_destroy(&l->lot);
    free(l);
}

static bool latch_ready(void* raw) {
    return ((KLatch*)raw)->count <= 0;
}

int k_latch_await(KLatch* l, int64_t timeout_nanos) {
    if (l == NULL) {
        return K_SYNC_OK;
    }
    return park_until(&l->lot, NULL, latch_ready, NULL, l, timeout_nanos, true);
}

void k_latch_count_down(KLatch* l) {
    if (l == NULL) {
        return;
    }
    pthread_mutex_lock(&l->lot.lock);
    if (l->count > 0) {
        l->count--;
        if (l->count == 0) {
            park_lot_wake_all(&l->lot);
        }
    }
    pthread_mutex_unlock(&l->lot.lock);
}

int64_t k_latch_count(const KLatch* l) {
    if (l == NULL) {
        return 0;
    }
    KLatch* ll = (KLatch*)l;
    pthread_mutex_lock(&ll->lot.lock);
    int64_t n = ll->count;
    pthread_mutex_unlock(&ll->lot.lock);
    return n;
}

/* ── CyclicBarrier ──────────────────────────────────────────────────────── */

struct KBarrier {
    KParkLot lot;
    int32_t  parties;
    int32_t  remaining;  /* parties still to arrive in this generation */
    uint64_t generation;
    bool     broken;
};

typedef struct {
    KBarrier* barrier;
    uint64_t  entry_gen;
    int32_t   arrival;
    bool      tripped;   /* this thread was the one that tripped the barrier */
    bool      illegal;
} BarrierCtx;

KBarrier* k_barrier_create(int32_t parties) {
    if (parties <= 0) {
        return NULL;
    }
    KBarrier* b = (KBarrier*)calloc(1, sizeof(KBarrier));
    if (b == NULL) {
        return NULL;
    }
    if (park_lot_init(&b->lot) != 0) {
        free(b);
        return NULL;
    }
    b->parties   = parties;
    b->remaining = parties;
    return b;
}

void k_barrier_destroy(KBarrier* b) {
    if (b == NULL) {
        return;
    }
    park_lot_destroy(&b->lot);
    free(b);
}

static void barrier_on_enter(void* raw) {
    BarrierCtx* ctx = (BarrierCtx*)raw;
    KBarrier* b = ctx->barrier;
    if (b->broken) {
        ctx->illegal = true;
        return;
    }
    ctx->entry_gen = b->generation;
    b->remaining--;
    ctx->arrival = b->remaining;
    if (b->remaining == 0) {
        /* Last arrival trips the barrier and opens the next generation. */
        b->generation++;
        b->remaining = b->parties;
        ctx->tripped = true;
        park_lot_wake_all(&b->lot);
    }
}

static bool barrier_ready(void* raw) {
    BarrierCtx* ctx = (BarrierCtx*)raw;
    KBarrier* b = ctx->barrier;
    if (ctx->illegal || ctx->tripped) {
        return true;
    }
    if (b->broken) {
        return true;
    }
    return b->generation != ctx->entry_gen;
}

static void barrier_on_exit(void* raw, int result) {
    BarrierCtx* ctx = (BarrierCtx*)raw;
    KBarrier* b = ctx->barrier;
    if (ctx->illegal || ctx->tripped) {
        return;
    }
    if (result != K_SYNC_OK) {
        /* Leaving early breaks the generation for everybody else. */
        if (b->generation == ctx->entry_gen && !b->broken) {
            b->broken = true;
            b->remaining = b->parties;
            park_lot_wake_all(&b->lot);
        }
    }
}

int k_barrier_await(KBarrier* b, int64_t timeout_nanos, int32_t* out_arrival) {
    if (b == NULL) {
        return K_SYNC_ILLEGAL;
    }
    BarrierCtx ctx;
    ctx.barrier   = b;
    ctx.entry_gen = 0u;
    ctx.arrival   = 0;
    ctx.tripped   = false;
    ctx.illegal   = false;

    int result = park_until(&b->lot, barrier_on_enter, barrier_ready,
                            barrier_on_exit, &ctx, timeout_nanos, true);

    if (out_arrival != NULL) {
        *out_arrival = ctx.arrival;
    }
    if (ctx.illegal) {
        return K_SYNC_BROKEN;
    }
    if (result == K_SYNC_OK && !ctx.tripped) {
        pthread_mutex_lock(&b->lot.lock);
        bool broken = b->broken;
        pthread_mutex_unlock(&b->lot.lock);
        if (broken) {
            return K_SYNC_BROKEN;
        }
    }
    return result;
}

void k_barrier_reset(KBarrier* b) {
    if (b == NULL) {
        return;
    }
    pthread_mutex_lock(&b->lot.lock);
    b->generation++;
    b->remaining = b->parties;
    b->broken = false;
    park_lot_wake_all(&b->lot);
    pthread_mutex_unlock(&b->lot.lock);
}

int32_t k_barrier_parties(const KBarrier* b) {
    return b == NULL ? 0 : b->parties;
}

int32_t k_barrier_waiting(const KBarrier* b) {
    if (b == NULL) {
        return 0;
    }
    KBarrier* bb = (KBarrier*)b;
    pthread_mutex_lock(&bb->lot.lock);
    int32_t n = bb->parties - bb->remaining;
    pthread_mutex_unlock(&bb->lot.lock);
    return n;
}

bool k_barrier_is_broken(const KBarrier* b) {
    if (b == NULL) {
        return false;
    }
    KBarrier* bb = (KBarrier*)b;
    pthread_mutex_lock(&bb->lot.lock);
    bool broken = bb->broken;
    pthread_mutex_unlock(&bb->lot.lock);
    return broken;
}

/* ── ReadWriteLock ──────────────────────────────────────────────────────── */

struct KRwLock {
    KParkLot lot;
    int32_t  readers;         /* number of shared holders                */
    uint64_t writer;          /* sync thread id of the exclusive holder   */
    int32_t  waiting_writers; /* writers queued, used to avoid starvation */
};

KRwLock* k_rwlock_create(void) {
    KRwLock* l = (KRwLock*)calloc(1, sizeof(KRwLock));
    if (l == NULL) {
        return NULL;
    }
    if (park_lot_init(&l->lot) != 0) {
        free(l);
        return NULL;
    }
    return l;
}

void k_rwlock_destroy(KRwLock* l) {
    if (l == NULL) {
        return;
    }
    park_lot_destroy(&l->lot);
    free(l);
}

static bool rw_read_ready(void* raw) {
    KRwLock* l = (KRwLock*)raw;
    if (l->writer == 0u && l->waiting_writers == 0) {
        l->readers++;
        return true;
    }
    return false;
}

static void rw_write_enter(void* raw) {
    ((KRwLock*)raw)->waiting_writers++;
}

static bool rw_write_ready(void* raw) {
    KRwLock* l = (KRwLock*)raw;
    if (l->writer == 0u && l->readers == 0) {
        l->writer = k_sync_current_thread_id();
        return true;
    }
    return false;
}

static void rw_write_exit(void* raw, int result) {
    KRwLock* l = (KRwLock*)raw;
    l->waiting_writers--;
    if (result != K_SYNC_OK && l->waiting_writers == 0) {
        /* No writer left in the queue: let blocked readers proceed. */
        park_lot_wake_all(&l->lot);
    }
}

int k_rwlock_read_lock(KRwLock* l, int64_t timeout_nanos, bool interruptible) {
    if (l == NULL) {
        return K_SYNC_ILLEGAL;
    }
    return park_until(&l->lot, NULL, rw_read_ready, NULL, l,
                      timeout_nanos, interruptible);
}

bool k_rwlock_try_read_lock(KRwLock* l) {
    if (l == NULL) {
        return false;
    }
    pthread_mutex_lock(&l->lot.lock);
    bool ok = rw_read_ready(l);
    pthread_mutex_unlock(&l->lot.lock);
    return ok;
}

int k_rwlock_read_unlock(KRwLock* l) {
    if (l == NULL) {
        return K_SYNC_ILLEGAL;
    }
    int result = K_SYNC_OK;
    pthread_mutex_lock(&l->lot.lock);
    if (l->readers <= 0) {
        result = K_SYNC_ILLEGAL;
    } else if (--l->readers == 0) {
        park_lot_wake_all(&l->lot);
    }
    pthread_mutex_unlock(&l->lot.lock);
    return result;
}

int k_rwlock_write_lock(KRwLock* l, int64_t timeout_nanos, bool interruptible) {
    if (l == NULL) {
        return K_SYNC_ILLEGAL;
    }
    return park_until(&l->lot, rw_write_enter, rw_write_ready, rw_write_exit, l,
                      timeout_nanos, interruptible);
}

bool k_rwlock_try_write_lock(KRwLock* l) {
    if (l == NULL) {
        return false;
    }
    pthread_mutex_lock(&l->lot.lock);
    bool ok = rw_write_ready(l);
    pthread_mutex_unlock(&l->lot.lock);
    return ok;
}

int k_rwlock_write_unlock(KRwLock* l) {
    if (l == NULL) {
        return K_SYNC_ILLEGAL;
    }
    int result = K_SYNC_OK;
    pthread_mutex_lock(&l->lot.lock);
    if (l->writer != k_sync_current_thread_id()) {
        result = K_SYNC_ILLEGAL;
    } else {
        l->writer = 0u;
        park_lot_wake_all(&l->lot);
    }
    pthread_mutex_unlock(&l->lot.lock);
    return result;
}

int32_t k_rwlock_read_count(const KRwLock* l) {
    if (l == NULL) {
        return 0;
    }
    KRwLock* ll = (KRwLock*)l;
    pthread_mutex_lock(&ll->lot.lock);
    int32_t n = ll->readers;
    pthread_mutex_unlock(&ll->lot.lock);
    return n;
}

bool k_rwlock_is_write_locked(const KRwLock* l) {
    if (l == NULL) {
        return false;
    }
    KRwLock* ll = (KRwLock*)l;
    pthread_mutex_lock(&ll->lot.lock);
    bool held = ll->writer != 0u;
    pthread_mutex_unlock(&ll->lot.lock);
    return held;
}
