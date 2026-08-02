/*
 * K Language runtime — asynchronous I/O substrate
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
 * Layer 1 of the portable runtime: the asynchronous I/O substrate backing
 * ::k::io channels and streams.
 *
 * Architecture
 * ------------
 *   - A single process-wide AsyncRuntime owns one io_uring instance and one
 *     dedicated completion thread reaping CQEs.
 *   - Every submitted operation is registered in an ABA-safe registry; the
 *     io_uring user_data word carries (slot index << 32) | generation, so a
 *     late completion for a recycled slot can never be mistaken for a live
 *     operation.
 *   - A caller blocks on its operation through the same interruptible park
 *     lot used by the synchronisation primitives (park_lot.c), so every
 *     asynchronous wait is interruptible and can be given a deadline for
 *     free.  On interruption or timeout the caller submits an
 *     IORING_OP_ASYNC_CANCEL for its own operation and then waits
 *     uninterruptibly for the real completion, so the kernel is never left
 *     writing into a buffer the caller has already released.
 *   - An AsyncHandle owns a file descriptor plus the list of operations
 *     currently in flight on it.  Closing a handle cancels every in-flight
 *     operation, drains them, and only then closes the descriptor; waiters
 *     observing a cancellation on a closing handle report K_ASYNC_CLOSED.
 *
 * When liburing is unavailable the same API is provided by a synchronous
 * fallback backend built on plain pread/pwrite; operations are then neither
 * interruptible nor cancellable (k_async_is_uring() returns 0 so tests can
 * skip the corresponding expectations).
 */

#define _GNU_SOURCE
#include "async_io.h"
#include "park_lot.h"

#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

#if K_ASYNC_HAVE_URING
#include <liburing.h>
#endif

/* ── Operation ──────────────────────────────────────────────────────────── */

#define K_OP_PENDING    0u
#define K_OP_COMPLETED  1u

struct KAsyncOp {
    _Atomic uint32_t state;
    int32_t          result;      /* >= 0 on success, -errno on failure */
    KParkLot         lot;
    KAsyncHandle*    handle;
    KAsyncOp*        next;
    KAsyncOp*        prev;
    uint64_t         tag;         /* (slot << 32) | generation */
    _Atomic uint32_t refs;        /* waiter + registry */
};

/* ── Handle ─────────────────────────────────────────────────────────────── */

#define K_HANDLE_OPEN     0u
#define K_HANDLE_CLOSING  1u
#define K_HANDLE_CLOSED   2u

struct KAsyncHandle {
    int              fd;
    _Atomic uint32_t state;
    _Atomic uint32_t refs;
    pthread_mutex_t  mutex;
    pthread_cond_t   drained;
    KAsyncOp*        inflight;    /* doubly-linked list head */
    unsigned         inflight_count;
};

/* ── Registry ───────────────────────────────────────────────────────────── */

typedef struct {
    KAsyncOp* op;         /* NULL when free */
    uint32_t  generation;
    uint32_t  next_free;  /* index of next free slot, or UINT32_MAX */
} KOpSlot;

typedef struct {
    pthread_mutex_t mutex;
    KOpSlot*        slots;
    uint32_t        capacity;
    uint32_t        free_head;
} KOpRegistry;

static KOpRegistry g_registry = {
    PTHREAD_MUTEX_INITIALIZER, NULL, 0u, UINT32_MAX
};

/* Returns the tag on success, 0 on allocation failure (tag 0 is never used
 * because generation counting starts at 1). */
static uint64_t registry_insert(KAsyncOp* op) {
    pthread_mutex_lock(&g_registry.mutex);
    if (g_registry.free_head == UINT32_MAX) {
        uint32_t old = g_registry.capacity;
        uint32_t cap = old == 0u ? 64u : old * 2u;
        KOpSlot* grown =
            (KOpSlot*)realloc(g_registry.slots, (size_t)cap * sizeof(KOpSlot));
        if (grown == NULL) {
            pthread_mutex_unlock(&g_registry.mutex);
            return 0u;
        }
        g_registry.slots    = grown;
        g_registry.capacity = cap;
        for (uint32_t i = cap; i > old; --i) {
            uint32_t idx = i - 1u;
            g_registry.slots[idx].op         = NULL;
            g_registry.slots[idx].generation = 1u;
            g_registry.slots[idx].next_free  = g_registry.free_head;
            g_registry.free_head             = idx;
        }
    }
    uint32_t idx = g_registry.free_head;
    g_registry.free_head       = g_registry.slots[idx].next_free;
    g_registry.slots[idx].op   = op;
    uint64_t tag = ((uint64_t)idx << 32) | (uint64_t)g_registry.slots[idx].generation;
    pthread_mutex_unlock(&g_registry.mutex);
    return tag;
}

/* Remove the operation designated by `tag` and return it, or NULL when the tag
 * is stale (slot already recycled). */
static KAsyncOp* registry_take(uint64_t tag) {
    uint32_t idx = (uint32_t)(tag >> 32);
    uint32_t gen = (uint32_t)(tag & 0xFFFFFFFFu);
    KAsyncOp* op = NULL;
    pthread_mutex_lock(&g_registry.mutex);
    if (idx < g_registry.capacity &&
        g_registry.slots[idx].op != NULL &&
        g_registry.slots[idx].generation == gen) {
        op = g_registry.slots[idx].op;
        g_registry.slots[idx].op = NULL;
        g_registry.slots[idx].generation++;
        if (g_registry.slots[idx].generation == 0u) {
            g_registry.slots[idx].generation = 1u;
        }
        g_registry.slots[idx].next_free = g_registry.free_head;
        g_registry.free_head            = idx;
    }
    pthread_mutex_unlock(&g_registry.mutex);
    return op;
}

/* ── Operation lifecycle ────────────────────────────────────────────────── */

static KAsyncOp* op_create(KAsyncHandle* h) {
    KAsyncOp* op = (KAsyncOp*)calloc(1, sizeof(KAsyncOp));
    if (op == NULL) {
        return NULL;
    }
    if (k_park_lot_init(&op->lot) != 0) {
        free(op);
        return NULL;
    }
    atomic_store(&op->state, K_OP_PENDING);
    atomic_store(&op->refs, 2u);   /* waiter + registry */
    op->handle = h;
    op->result = 0;
    return op;
}

static void op_release(KAsyncOp* op) {
    if (atomic_fetch_sub_explicit(&op->refs, 1u, memory_order_acq_rel) == 1u) {
        k_park_lot_destroy(&op->lot);
        free(op);
    }
}

/* Caller must hold h->mutex. */
static void handle_link(KAsyncHandle* h, KAsyncOp* op) {
    op->prev = NULL;
    op->next = h->inflight;
    if (h->inflight != NULL) {
        h->inflight->prev = op;
    }
    h->inflight = op;
    h->inflight_count++;
}

/* Caller must hold h->mutex. */
static void handle_unlink(KAsyncHandle* h, KAsyncOp* op) {
    if (op->prev != NULL) {
        op->prev->next = op->next;
    } else if (h->inflight == op) {
        h->inflight = op->next;
    }
    if (op->next != NULL) {
        op->next->prev = op->prev;
    }
    op->prev = NULL;
    op->next = NULL;
    if (h->inflight_count > 0u) {
        h->inflight_count--;
    }
    if (h->inflight_count == 0u) {
        pthread_cond_broadcast(&h->drained);
    }
}

/* Publish a completion result and wake every waiter. */
static void op_complete(KAsyncOp* op, int32_t result) {
    KAsyncHandle* h = op->handle;
    if (h != NULL) {
        pthread_mutex_lock(&h->mutex);
        handle_unlink(h, op);
        pthread_mutex_unlock(&h->mutex);
    }
    pthread_mutex_lock(&op->lot.lock);
    op->result = result;
    atomic_store_explicit(&op->state, K_OP_COMPLETED, memory_order_release);
    k_park_lot_wake_all(&op->lot);
    pthread_mutex_unlock(&op->lot.lock);
    op_release(op);   /* drop the registry reference */
}

/* ── Backend: io_uring ──────────────────────────────────────────────────── */

#if K_ASYNC_HAVE_URING

#define K_URING_ENTRIES  256
#define K_URING_STOP_TAG 0xFFFFFFFFFFFFFFFFULL

typedef struct {
    struct io_uring  ring;
    pthread_mutex_t  submit_mutex;
    pthread_t        reaper;
    _Atomic uint32_t running;
} KAsyncRuntime;

static KAsyncRuntime  g_runtime;
static pthread_once_t g_runtime_once = PTHREAD_ONCE_INIT;
static _Atomic uint32_t g_runtime_ready = 0u;

static void* reaper_main(void* arg) {
    (void)arg;
    for (;;) {
        struct io_uring_cqe* cqe = NULL;
        int rc = io_uring_wait_cqe(&g_runtime.ring, &cqe);
        if (rc < 0) {
            if (rc == -EINTR) {
                continue;
            }
            break;
        }
        uint64_t tag = io_uring_cqe_get_data64(cqe);
        int32_t  res = cqe->res;
        io_uring_cqe_seen(&g_runtime.ring, cqe);

        if (tag == K_URING_STOP_TAG) {
            break;
        }
        if (tag == 0u) {
            continue;   /* fire-and-forget submission (cancel request) */
        }
        KAsyncOp* op = registry_take(tag);
        if (op != NULL) {
            op_complete(op, res);
        }
    }
    return NULL;
}

static void runtime_shutdown(void) {
    if (!atomic_load(&g_runtime_ready)) {
        return;
    }
    pthread_mutex_lock(&g_runtime.submit_mutex);
    struct io_uring_sqe* sqe = io_uring_get_sqe(&g_runtime.ring);
    if (sqe != NULL) {
        io_uring_prep_nop(sqe);
        io_uring_sqe_set_data64(sqe, K_URING_STOP_TAG);
        io_uring_submit(&g_runtime.ring);
    }
    pthread_mutex_unlock(&g_runtime.submit_mutex);
    pthread_join(g_runtime.reaper, NULL);
    io_uring_queue_exit(&g_runtime.ring);
    atomic_store(&g_runtime_ready, 0u);
}

static void runtime_init_once(void) {
    memset(&g_runtime, 0, sizeof(g_runtime));
    if (io_uring_queue_init(K_URING_ENTRIES, &g_runtime.ring, 0) < 0) {
        return;
    }
    pthread_mutex_init(&g_runtime.submit_mutex, NULL);
    if (pthread_create(&g_runtime.reaper, NULL, reaper_main, NULL) != 0) {
        io_uring_queue_exit(&g_runtime.ring);
        pthread_mutex_destroy(&g_runtime.submit_mutex);
        return;
    }
    atomic_store(&g_runtime_ready, 1u);
    atexit(runtime_shutdown);
}

static bool runtime_ready(void) {
    pthread_once(&g_runtime_once, runtime_init_once);
    return atomic_load(&g_runtime_ready) != 0u;
}

int k_async_is_uring(void) {
    return runtime_ready() ? 1 : 0;
}

/* Submit an asynchronous cancellation for `tag`.  Fire-and-forget: its own
 * completion carries user_data 0 and is discarded by the reaper. */
static void submit_cancel(uint64_t tag) {
    pthread_mutex_lock(&g_runtime.submit_mutex);
    struct io_uring_sqe* sqe = io_uring_get_sqe(&g_runtime.ring);
    if (sqe != NULL) {
        io_uring_prep_cancel64(sqe, tag, 0);
        io_uring_sqe_set_data64(sqe, 0u);
        io_uring_submit(&g_runtime.ring);
    }
    pthread_mutex_unlock(&g_runtime.submit_mutex);
}

typedef enum {
    K_OPK_READ  = 0,
    K_OPK_WRITE = 1,
    K_OPK_FSYNC = 2
} KOpKind;

/* Prepare, register and submit one operation.  Returns the operation (with the
 * caller's reference held) or NULL on failure. */
static KAsyncOp* submit_op(KAsyncHandle* h, KOpKind kind,
                           void* buf, uint32_t len, int64_t offset) {
    KAsyncOp* op = op_create(h);
    if (op == NULL) {
        return NULL;
    }
    uint64_t tag = registry_insert(op);
    if (tag == 0u) {
        op_release(op);
        op_release(op);
        return NULL;
    }
    op->tag = tag;

    pthread_mutex_lock(&h->mutex);
    handle_link(h, op);
    pthread_mutex_unlock(&h->mutex);

    pthread_mutex_lock(&g_runtime.submit_mutex);
    struct io_uring_sqe* sqe = io_uring_get_sqe(&g_runtime.ring);
    if (sqe == NULL) {
        pthread_mutex_unlock(&g_runtime.submit_mutex);
        registry_take(tag);
        pthread_mutex_lock(&h->mutex);
        handle_unlink(h, op);
        pthread_mutex_unlock(&h->mutex);
        op_release(op);
        op_release(op);
        return NULL;
    }
    switch (kind) {
        case K_OPK_READ:
            io_uring_prep_read(sqe, h->fd, buf, len, (unsigned long long)offset);
            break;
        case K_OPK_WRITE:
            io_uring_prep_write(sqe, h->fd, buf, len, (unsigned long long)offset);
            break;
        case K_OPK_FSYNC:
        default:
            io_uring_prep_fsync(sqe, h->fd, 0);
            break;
    }
    io_uring_sqe_set_data64(sqe, tag);
    int submitted = io_uring_submit(&g_runtime.ring);
    pthread_mutex_unlock(&g_runtime.submit_mutex);

    if (submitted < 0) {
        registry_take(tag);
        pthread_mutex_lock(&h->mutex);
        handle_unlink(h, op);
        pthread_mutex_unlock(&h->mutex);
        op_release(op);
        op_release(op);
        return NULL;
    }
    return op;
}

static bool op_ready(void* ctx) {
    KAsyncOp* op = (KAsyncOp*)ctx;
    return atomic_load_explicit(&op->state, memory_order_acquire) == K_OP_COMPLETED;
}

/*
 * Wait for `op`, cancelling it on interruption or timeout.  Fills *status and
 * returns the operation result (bytes transferred, or -errno).
 */
static int64_t await_op(KAsyncOp* op, int64_t timeout_nanos, int* status) {
    int parked = k_park_until(&op->lot, NULL, op_ready, NULL, op,
                              timeout_nanos, true);
    if (parked != K_PARK_OK) {
        submit_cancel(op->tag);
        /* Uninterruptible, unbounded: the kernel still owns the buffer. */
        k_park_until(&op->lot, NULL, op_ready, NULL, op, -1, false);
    }

    int32_t res = op->result;
    KAsyncHandle* h = op->handle;
    op_release(op);

    if (res >= 0) {
        /* The operation actually completed, even if the caller asked to give
         * up: report the transfer rather than losing data. */
        *status = K_ASYNC_OK;
        return (int64_t)res;
    }
    if (res == -ECANCELED || res == -EINTR) {
        if (h != NULL &&
            atomic_load(&h->state) != K_HANDLE_OPEN) {
            *status = K_ASYNC_CLOSED;
        } else if (parked == K_PARK_TIMEOUT) {
            *status = K_ASYNC_TIMEOUT;
        } else if (parked == K_PARK_INTERRUPTED) {
            *status = K_ASYNC_INTERRUPTED;
        } else {
            *status = K_ASYNC_CLOSED;
        }
        return 0;
    }
    *status = K_ASYNC_ERROR;
    return (int64_t)(-res);
}

static int64_t backend_submit_and_wait(KAsyncHandle* h, KOpKind kind,
                                       void* buf, uint32_t len, int64_t offset,
                                       int64_t timeout_nanos, int* status) {
    if (!runtime_ready()) {
        *status = K_ASYNC_ERROR;
        return ENOSYS;
    }
    if (atomic_load(&h->state) != K_HANDLE_OPEN) {
        *status = K_ASYNC_CLOSED;
        return 0;
    }
    KAsyncOp* op = submit_op(h, kind, buf, len, offset);
    if (op == NULL) {
        *status = K_ASYNC_ERROR;
        return ENOMEM;
    }
    return await_op(op, timeout_nanos, status);
}

/* Cancel every in-flight operation of `h` and wait until the list drains. */
static void backend_cancel_all(KAsyncHandle* h) {
    for (;;) {
        pthread_mutex_lock(&h->mutex);
        if (h->inflight_count == 0u) {
            pthread_mutex_unlock(&h->mutex);
            return;
        }
        /* Snapshot the tags, then cancel outside the handle lock. */
        unsigned  n    = h->inflight_count;
        uint64_t* tags = (uint64_t*)malloc((size_t)n * sizeof(uint64_t));
        unsigned  i    = 0u;
        for (KAsyncOp* o = h->inflight; o != NULL && tags != NULL && i < n;
             o = o->next) {
            tags[i++] = o->tag;
        }
        pthread_mutex_unlock(&h->mutex);

        for (unsigned j = 0; j < i; ++j) {
            submit_cancel(tags[j]);
        }
        free(tags);

        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_nsec += 20000000L;   /* 20 ms */
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec  += 1;
            ts.tv_nsec -= 1000000000L;
        }
        pthread_mutex_lock(&h->mutex);
        while (h->inflight_count > 0u) {
            if (pthread_cond_timedwait(&h->drained, &h->mutex, &ts) == ETIMEDOUT) {
                break;
            }
        }
        unsigned remaining = h->inflight_count;
        pthread_mutex_unlock(&h->mutex);
        if (remaining == 0u) {
            return;
        }
    }
}

#else /* ── Backend: synchronous POSIX fallback ─────────────────────────── */

int k_async_is_uring(void) {
    return 0;
}

typedef enum {
    K_OPK_READ  = 0,
    K_OPK_WRITE = 1,
    K_OPK_FSYNC = 2
} KOpKind;

static int64_t backend_submit_and_wait(KAsyncHandle* h, KOpKind kind,
                                       void* buf, uint32_t len, int64_t offset,
                                       int64_t timeout_nanos, int* status) {
    (void)timeout_nanos;
    if (atomic_load(&h->state) != K_HANDLE_OPEN) {
        *status = K_ASYNC_CLOSED;
        return 0;
    }
    ssize_t r;
    do {
        if (kind == K_OPK_READ) {
            r = (offset < 0) ? read(h->fd, buf, (size_t)len)
                             : pread(h->fd, buf, (size_t)len, (off_t)offset);
        } else if (kind == K_OPK_WRITE) {
            r = (offset < 0) ? write(h->fd, buf, (size_t)len)
                             : pwrite(h->fd, buf, (size_t)len, (off_t)offset);
        } else {
            r = fsync(h->fd);
        }
    } while (r < 0 && errno == EINTR);

    if (r < 0) {
        *status = K_ASYNC_ERROR;
        return (int64_t)errno;
    }
    *status = K_ASYNC_OK;
    return (int64_t)r;
}

static void backend_cancel_all(KAsyncHandle* h) {
    (void)h;
}

#endif

/* ── Handle API ─────────────────────────────────────────────────────────── */

KAsyncHandle* k_async_open(const char* path, int flags, int mode, int* out_errno) {
    int fd = open(path, flags, (mode_t)mode);
    if (fd < 0) {
        if (out_errno != NULL) {
            *out_errno = errno;
        }
        return NULL;
    }
    KAsyncHandle* h = k_async_wrap_fd(fd);
    if (h == NULL) {
        close(fd);
        if (out_errno != NULL) {
            *out_errno = ENOMEM;
        }
        return NULL;
    }
    if (out_errno != NULL) {
        *out_errno = 0;
    }
    return h;
}

KAsyncHandle* k_async_wrap_fd(int fd) {
    KAsyncHandle* h = (KAsyncHandle*)calloc(1, sizeof(KAsyncHandle));
    if (h == NULL) {
        return NULL;
    }
    h->fd = fd;
    atomic_store(&h->state, K_HANDLE_OPEN);
    atomic_store(&h->refs, 1u);
    pthread_mutex_init(&h->mutex, NULL);
    pthread_cond_init(&h->drained, NULL);
    return h;
}

int k_async_handle_fd(const KAsyncHandle* h) {
    return h == NULL ? -1 : h->fd;
}

int k_async_is_open(const KAsyncHandle* h) {
    return (h != NULL && atomic_load(&h->state) == K_HANDLE_OPEN) ? 1 : 0;
}

void k_async_retain(KAsyncHandle* h) {
    if (h != NULL) {
        atomic_fetch_add_explicit(&h->refs, 1u, memory_order_relaxed);
    }
}

int k_async_close(KAsyncHandle* h) {
    if (h == NULL) {
        return EINVAL;
    }
    uint32_t expected = K_HANDLE_OPEN;
    if (!atomic_compare_exchange_strong(&h->state, &expected, K_HANDLE_CLOSING)) {
        return 0;   /* already closing or closed */
    }
    backend_cancel_all(h);
    int rc = 0;
    if (h->fd >= 0) {
        rc = close(h->fd) < 0 ? errno : 0;
        h->fd = -1;
    }
    atomic_store(&h->state, K_HANDLE_CLOSED);
    return rc;
}

void k_async_release(KAsyncHandle* h) {
    if (h == NULL) {
        return;
    }
    if (atomic_fetch_sub_explicit(&h->refs, 1u, memory_order_acq_rel) == 1u) {
        k_async_close(h);
        pthread_cond_destroy(&h->drained);
        pthread_mutex_destroy(&h->mutex);
        free(h);
    }
}

/* ── Operation API ──────────────────────────────────────────────────────── */

int64_t k_async_read(KAsyncHandle* h, void* buf, uint32_t len, int64_t offset,
                     int64_t timeout_nanos, int* status) {
    if (h == NULL || buf == NULL) {
        *status = K_ASYNC_ERROR;
        return EINVAL;
    }
    if (len == 0u) {
        *status = K_ASYNC_OK;
        return 0;
    }
    return backend_submit_and_wait(h, K_OPK_READ, buf, len, offset,
                                   timeout_nanos, status);
}

int64_t k_async_write(KAsyncHandle* h, const void* buf, uint32_t len,
                      int64_t offset, int64_t timeout_nanos, int* status) {
    if (h == NULL || buf == NULL) {
        *status = K_ASYNC_ERROR;
        return EINVAL;
    }
    if (len == 0u) {
        *status = K_ASYNC_OK;
        return 0;
    }
    return backend_submit_and_wait(h, K_OPK_WRITE, (void*)buf, len, offset,
                                   timeout_nanos, status);
}

int k_async_fsync(KAsyncHandle* h, int64_t timeout_nanos, int* status) {
    if (h == NULL) {
        *status = K_ASYNC_ERROR;
        return EINVAL;
    }
    int64_t r = backend_submit_and_wait(h, K_OPK_FSYNC, NULL, 0u, 0,
                                        timeout_nanos, status);
    return (int)r;
}

int64_t k_async_size(const KAsyncHandle* h) {
    if (h == NULL || h->fd < 0) {
        return -1;
    }
    off_t cur = lseek(h->fd, 0, SEEK_END);
    return cur < 0 ? -1 : (int64_t)cur;
}

int k_async_truncate(KAsyncHandle* h, int64_t size) {
    if (h == NULL || h->fd < 0) {
        return EINVAL;
    }
    return ftruncate(h->fd, (off_t)size) < 0 ? errno : 0;
}
