/*
 * K Language runtime — bounded proactor registry and lifecycle
 * Copyright 2023-2026 Emilien Kia
 * Licensed under the Apache License, Version 2.0.
 */
#define _GNU_SOURCE
#include "io_runtime.h"
#include "io_datagram_ffi.h"
#include <sys/stat.h>
#include <stdio.h>
#include "io_resolver.h"
#include "io_buffer.h"
#include "executor_runtime.h"
#include "runtime_thread.h"
#include "park_lot.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <netinet/in.h>

#define K_IO_REGISTRY_MAX_CAP 8192u
#define K_IO_POSITION_QUEUE_CAP 128u
#define K_RT_RUNNING 1u
#define K_RT_STOPPING 2u
#define K_RT_STOPPED 3u
static bool injected_backend;

typedef struct KOpSlot {
    KNativeIoOperation* op;
    uint32_t generation;
    unsigned remaining;
    int32_t target_result;
    bool target_seen;
    bool timed_out;
} KOpSlot;

struct KIoRuntime {
    KIoBackend* backend;
    KRuntimeThread* reaper;
    KIoResolver* resolver;
    pthread_mutex_t mutex;
    pthread_cond_t drained;
    KOpSlot slots[K_IO_REGISTRY_MAX_CAP];
    unsigned active;
    _Atomic unsigned state;
    _Atomic int64_t callbacks_queued;
    _Atomic int64_t callbacks_rejected;
    _Atomic int64_t registered_buffers;
};

static pthread_mutex_t delivery_mutex = PTHREAD_MUTEX_INITIALIZER;
static KNativeIoOperation* delivery_head;
static unsigned delivery_count;
static pthread_mutex_t init_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t shutdown_drained = PTHREAD_COND_INITIALIZER;
static _Atomic(KIoRuntime*) runtime;
static bool init_attempted;
static _Atomic bool shutdown_requested;
static bool shutdown_complete;
static _Thread_local bool shutdown_context;
static int init_error = ENOTSUP;
static volatile sig_atomic_t fork_child;
static _Thread_local bool reaper_context;
static _Thread_local bool runtime_worker_context;
static pthread_once_t lifecycle_once = PTHREAD_ONCE_INIT;

typedef struct KSystemExecutor {
    void* queue;
    KRuntimeThread* workers[4];
    bool stopped;
} KSystemExecutor;
static KSystemExecutor* system_executor;
static pthread_mutex_t system_mutex = PTHREAD_MUTEX_INITIALIZER;

static void system_worker(void* queue) {
    k_io_runtime_enter_worker();
    while (__k_native_queue_dequeue_and_run(queue)) {}
}
static void atfork_child(void) { fork_child = 1; }
static void register_lifecycle(void) {
    /* Inherited rings, worker queues and mappings are not usable in the child.
     * Reject rather than attempting to lock mutexes owned by vanished threads. */
    pthread_atfork(NULL, NULL, atfork_child);
    atexit(k_io_runtime_shutdown);
}
bool k_io_runtime_is_reaper(void) { return reaper_context; }
void k_io_runtime_enter_worker(void) { runtime_worker_context = true; }

KExecutorLease* k_executor_system_lease(void) {
    if (fork_child) return NULL;
    pthread_once(&lifecycle_once, register_lifecycle);
    pthread_mutex_lock(&system_mutex);
    if (!system_executor && !shutdown_requested) {
        KSystemExecutor* ex = calloc(1, sizeof(*ex));
        if (ex) ex->queue = __k_native_queue_create();
        if (!ex || !ex->queue) {
            free(ex);
            pthread_mutex_unlock(&system_mutex);
            return NULL;
        }
        unsigned started = 0;
        for (; started < 4; ++started) {
            ex->workers[started] = k_runtime_thread_create(system_worker, ex->queue);
            if (!ex->workers[started] || k_runtime_thread_start(ex->workers[started])) break;
        }
        if (started != 4) {
            __k_native_queue_close(ex->queue);
            for (unsigned i = 0; i <= started; ++i)
                k_runtime_thread_destroy(ex->workers[i]);
            __k_native_queue_release(ex->queue);
            free(ex);
            pthread_mutex_unlock(&system_mutex);
            return NULL;
        }
        system_executor = ex;
    }
    KExecutorLease* lease = system_executor && !system_executor->stopped
                          ? __k_native_queue_get_lease(system_executor->queue) : NULL;
    pthread_mutex_unlock(&system_mutex);
    return lease; /* owning native reference */
}
int k_io_runtime_dispatch(void (*run)(void*), void (*drop)(void*), void* arg) {
    KExecutorLease* lease = k_executor_system_lease();
    int rc = k_executor_lease_submit(lease, run, drop, arg);
    k_executor_lease_release(lease);
    return rc;
}

/* Registry owns one op ref until ALL target/linked-timeout CQEs are consumed. */
static uint64_t registry_insert(KIoRuntime* rt, KNativeIoOperation* op, bool timeout) {
    for (unsigned i = 0; i < K_IO_REGISTRY_MAX_CAP; ++i) {
        KOpSlot* slot = &rt->slots[i];
        if (slot->op) continue;
        if (slot->generation == UINT32_MAX) continue; /* retire instead of ABA wrap */
        ++slot->generation;
        slot->op = op;
        slot->remaining = timeout ? 2 : 1;
        slot->target_seen = false;
        slot->timed_out = false;
        ++rt->active;
        k_io_operation_retain(op);
        return ((uint64_t)i << 32) | slot->generation;
    }
    return 0;
}

static void process_cqe(KIoRuntime* rt, const KIoBackendCqe* cqe) {
    if (!cqe->tag || cqe->tag == K_TAG_STOP ||
        cqe->tag == K_TAG_CANCEL_REQUEST_FLAG) return;
    uint64_t tag = cqe->tag & ~K_TAG_LINKED_TIMEOUT_FLAG;
    unsigned index = (unsigned)(tag >> 32);
    unsigned generation = (unsigned)tag;
    if (index >= K_IO_REGISTRY_MAX_CAP) return;
    pthread_mutex_lock(&rt->mutex);
    KOpSlot* slot = &rt->slots[index];
    if (!slot->op || slot->generation != generation) {
        pthread_mutex_unlock(&rt->mutex);
        return;
    }
    bool timeout = cqe->tag & K_TAG_LINKED_TIMEOUT_FLAG;
    if (timeout) slot->timed_out = cqe->res == -ETIME;
    else {
        slot->target_seen = true;
        slot->target_result = cqe->res;
    }
    --slot->remaining;
    if (slot->remaining || !slot->target_seen) {
        pthread_mutex_unlock(&rt->mutex);
        return;
    }
    KNativeIoOperation* op = slot->op;
    int32_t result = slot->target_result;
    if (result == -ECANCELED && slot->timed_out) result = -ETIMEDOUT;
    slot->op = NULL;
    /* Keep active counted through the internal publication and resource drain. */
    pthread_mutex_unlock(&rt->mutex);
    k_io_operation_on_kernel_completion(op, result);
    k_io_operation_release(op);
    pthread_mutex_lock(&rt->mutex);
    --rt->active;
    pthread_cond_broadcast(&rt->drained);
    pthread_mutex_unlock(&rt->mutex);
}

static void reaper_main(void* arg) {
    KIoRuntime* rt = arg;
    k_io_runtime_enter_worker();
    reaper_context = true;
    for (;;) {
        KIoBackendCqe cqe;
        int rc = rt->backend->ops->wait_cqe(rt->backend, &cqe);
        if (rc == -EINTR) continue;
        if (rc < 0) {
            /* Closing the ring is the only safe release barrier when CQ
             * consumption itself fails. Never manufacture quiescence first. */
            pthread_mutex_lock(&rt->mutex);
            atomic_store(&rt->state, K_RT_STOPPING);
            rt->backend->ops->destroy(rt->backend);
            rt->backend = NULL;
            pthread_mutex_unlock(&rt->mutex);
            for (unsigned i = 0; i < K_IO_REGISTRY_MAX_CAP; ++i) {
                pthread_mutex_lock(&rt->mutex);
                KNativeIoOperation* op = rt->slots[i].op;
                rt->slots[i].op = NULL;
                pthread_mutex_unlock(&rt->mutex);
                if (!op) continue;
                k_io_operation_on_kernel_completion(op, rc);
                k_io_operation_release(op);
                pthread_mutex_lock(&rt->mutex);
                --rt->active;
                pthread_cond_broadcast(&rt->drained);
                pthread_mutex_unlock(&rt->mutex);
            }
            break;
        }
        if (cqe.tag == K_TAG_STOP && atomic_load(&rt->state) != K_RT_RUNNING) break;
        process_cqe(rt, &cqe);
        KIoBackendCqe batch[32];
        unsigned count = rt->backend->ops->peek_batch_cqes(rt->backend, batch, 32);
        for (unsigned i = 0; i < count; ++i) process_cqe(rt, &batch[i]);
    }
}

static KIoRuntime* create_runtime(KIoBackend* backend) {
    KIoRuntime* rt = calloc(1, sizeof(*rt));
    if (!rt) return NULL;
    rt->backend = backend;
    pthread_mutex_init(&rt->mutex, NULL);
    pthread_cond_init(&rt->drained, NULL);
    rt->resolver = k_io_resolver_create(2, 128);
    if (!rt->resolver) goto fail;
    atomic_init(&rt->state, K_RT_RUNNING);
    rt->reaper = k_runtime_thread_create(reaper_main, rt);
    if (!rt->reaper || k_runtime_thread_start(rt->reaper)) goto fail;
    return rt;
fail:
    k_runtime_thread_destroy(rt->reaper);
    k_io_resolver_destroy(rt->resolver);
    pthread_cond_destroy(&rt->drained);
    pthread_mutex_destroy(&rt->mutex);
    free(rt);
    return NULL;
}

KIoRuntime* k_io_runtime_get(void) {
    if (fork_child) return NULL;
    pthread_once(&lifecycle_once, register_lifecycle);
    pthread_mutex_lock(&init_mutex);
    if (!init_attempted && !shutdown_requested) {
        init_attempted = true;
        KIoBackend* backend = k_io_uring_backend_create(256);
        if (backend) {
            runtime = create_runtime(backend);
            if (!runtime) {
                backend->ops->destroy(backend);
                init_error = ENOMEM;
            }
        } else init_error = errno ? errno : ENOTSUP;
    }
    KIoRuntime* rt = runtime;
    pthread_mutex_unlock(&init_mutex);
    return rt;
}
int k_io_runtime_install_test_backend(KIoBackend* backend) {
    if (!backend || fork_child) return -EINVAL;
    pthread_once(&lifecycle_once, register_lifecycle);
    pthread_mutex_lock(&init_mutex);
    if (init_attempted || shutdown_requested) {
        pthread_mutex_unlock(&init_mutex);
        return -EBUSY;
    }
    runtime = create_runtime(backend);
    if (runtime) { init_attempted = true; injected_backend = true; }
    pthread_mutex_unlock(&init_mutex);
    return runtime ? 0 : -ENOMEM;
}
int k_io_runtime_set_test_resolver_lookup(void* lookup) {
    KIoRuntime* rt = k_io_runtime_get();
    if (!rt || !injected_backend || fork_child) return -ENOTSUP;
    pthread_mutex_lock(&rt->mutex);
    int rc = atomic_load(&rt->state) == K_RT_RUNNING ? 0 : -ESHUTDOWN;
    if (!rc) k_io_resolver_set_test_lookup(rt->resolver, (KIoResolverLookup)lookup);
    pthread_mutex_unlock(&rt->mutex);
    return rc;
}
int k_io_runtime_init(void) {
    KIoRuntime* rt = k_io_runtime_get();
    return rt && atomic_load(&rt->state) == K_RT_RUNNING ? 0
         : -(fork_child ? ENOTSUP : init_error);
}
bool k_io_runtime_is_active(void) {
    return !fork_child && runtime && atomic_load(&runtime->state) == K_RT_RUNNING;
}

static int schedule_close(KNativeIoOperation* op);
static int submit_target(KIoRuntime* rt, KNativeIoOperation* op, int64_t timeout);
static int start_fully(KIoRuntime* rt, KNativeIoOperation* op);
static int start_connect_resolve(KIoRuntime* rt, KNativeIoOperation* op);

static int prepare_callback_dispatch(KNativeIoOperation* op) {
    if (!op->has_callback) return 0;
    if (shutdown_requested) return -ESHUTDOWN;
    /* Accepted callbacks need a registered cleanup worker even if their first
     * completion arrives during shutdown, before any dispatch has run. */
    KExecutorLease* lease = k_executor_system_lease();
    if (!lease) return shutdown_requested ? -ESHUTDOWN : -ENOMEM;
    k_executor_lease_release(lease);
    return 0;
}

static void link_handle_op(KIoHandle* h, KNativeIoOperation* op) {
    op->next = h->inflight_head;
    if (op->next) op->next->prev = op;
    h->inflight_head = op;
    ++h->inflight_count;
    op->handle_linked = true;
}

static int enqueue_position(KNativeIoOperation* op, int64_t timeout) {
    KIoHandle* h = op->handle;
    if (!h || h->creator_pid != getpid()) return -EBADF;
    pthread_mutex_lock(&h->mutex);
    if (atomic_load(&h->state) != K_HANDLE_OPEN) {
        pthread_mutex_unlock(&h->mutex);
        return -EBADF;
    }
    if (h->position_count == K_IO_POSITION_QUEUE_CAP) {
        pthread_mutex_unlock(&h->mutex);
        return -EBUSY;
    }
    if (timeout > 0 && !op->deadline_nanos) {
        int64_t now = k_monotonic_nanos();
        op->deadline_nanos = timeout > INT64_MAX - now ? INT64_MAX : now + timeout;
    }
    k_io_operation_retain(op); /* FIFO owns storage, including unsent requests */
    op->implicit_queued = true;
    link_handle_op(h, op);
    if (h->position_tail) h->position_tail->position_next = op;
    else h->position_head = op;
    h->position_tail = op;
    ++h->position_count;
    bool start = h->position_head == op;
    if (start) k_io_operation_retain(op); /* promotion consumes this reference */
    pthread_mutex_unlock(&h->mutex);
    if (start) k_io_runtime_promote(op);
    return 0;
}

int k_io_runtime_submit(KIoRuntime* rt, KNativeIoOperation* op, int64_t timeout) {
    if (!op || fork_child) return -ENOTSUP;
    if (atomic_exchange(&op->submitted, true)) return -EALREADY;
    if (op->request_error) return -op->request_error;
    if (!rt) return -ENOTSUP;
    int dispatch = prepare_callback_dispatch(op);
    if (dispatch < 0) return dispatch;
    if ((op->fully || op->resolving) && timeout > 0) {
        int64_t now = k_monotonic_nanos();
        op->deadline_nanos = timeout > INT64_MAX - now ? INT64_MAX : now + timeout;
    }
    if (op->opcode == K_IO_OP_CLOSE && op->handle) return schedule_close(op);
    if (op->resolving) return start_connect_resolve(rt, op);
    if (op->implicit_position || (op->opcode == K_IO_OP_FTRUNCATE && op->handle &&
                                 !op->handle->is_stream_socket))
        return enqueue_position(op, timeout);
    if (op->fully) return start_fully(rt, op);
    return submit_target(rt, op, timeout);
}

static int start_fully(KIoRuntime* rt, KNativeIoOperation* op) {
    if (!rt || !op->handle || fork_child) return -ENOTSUP;
    pthread_mutex_lock(&rt->mutex);
    if (atomic_load(&rt->state) != K_RT_RUNNING) {
        pthread_mutex_unlock(&rt->mutex);
        return -ESHUTDOWN;
    }
    KIoHandle* h = op->handle;
    pthread_mutex_lock(&h->mutex);
    bool read = op->opcode == K_IO_OP_READ || op->opcode == K_IO_OP_RECV;
    bool write = op->opcode == K_IO_OP_WRITE || op->opcode == K_IO_OP_SEND;
    int rc = atomic_load(&h->state) != K_HANDLE_OPEN ? -EBADF
           : !h->is_stream_socket &&
             (op->offset < 0 || op->len > INT64_MAX - op->offset) ? -EINVAL
           : h->is_stream_socket &&
             ((read && atomic_load(&h->read_inflight)) ||
              (write && atomic_load(&h->write_inflight))) ? -EBUSY : 0;
    if (!rc) {
        op->fd = h->fd;
        if (h->is_stream_socket) {
            if (read) atomic_store(&h->read_inflight, 1u);
            if (write) atomic_store(&h->write_inflight, 1u);
            op->owns_direction = read || write;
        }
        if (!op->handle_linked) link_handle_op(h, op);
    }
    pthread_mutex_unlock(&h->mutex);
    pthread_mutex_unlock(&rt->mutex);
    if (!rc) k_io_operation_start_fully(op);
    return rc;
}

/* Immediate failures can drain an entire FIFO. A per-thread pump avoids
 * recursive completion/submission and never depends on a callback executor. */
void k_io_runtime_promote(KNativeIoOperation* op) {
    static _Thread_local KNativeIoOperation* head;
    static _Thread_local KNativeIoOperation* tail;
    static _Thread_local bool pumping;
    op->promotion_next = NULL;
    if (tail) tail->promotion_next = op;
    else head = op;
    tail = op;
    if (pumping) return;
    pumping = true;
    while (head) {
        KNativeIoOperation* current = head;
        head = current->promotion_next;
        if (!head) tail = NULL;
        int rc = 0;
        int64_t timeout = -1;
        if (current->implicit_queued) {
            KIoHandle* h = current->handle;
            pthread_mutex_lock(&h->mutex);
            if (current->implicit_position) current->offset = h->file_position;
            if (atomic_load(&h->state) != K_HANDLE_OPEN) rc = -EBADF;
            else if (current->implicit_position &&
                     current->len > INT64_MAX - h->file_position) rc = -EINVAL;
            pthread_mutex_unlock(&h->mutex);
            if (!rc && k_io_operation_is_cancelled(current)) rc = -ECANCELED;
            if (!rc && current->deadline_nanos > 0) {
                timeout = current->deadline_nanos - k_monotonic_nanos();
                if (timeout <= 0) rc = -ETIMEDOUT;
            }
        }
        if (!rc) rc = current->fully ? start_fully(k_io_runtime_get(), current)
                                    : submit_target(k_io_runtime_get(), current, timeout);
        if (rc < 0) k_io_operation_on_kernel_completion(current, rc);
        k_io_operation_release(current);
    }
    pumping = false;
}

static int submit_target(KIoRuntime* rt, KNativeIoOperation* op, int64_t timeout) {
    if (!rt || fork_child) return -ENOTSUP;
    pthread_mutex_lock(&rt->mutex);
    if (!rt->backend) {
        pthread_mutex_unlock(&rt->mutex);
        return -ESHUTDOWN;
    }
    if (atomic_load(&op->cb_phase) == K_CB_PHASE_SUPPRESSED ||
        (op->fully_parent &&
         atomic_load(&op->fully_parent->cb_phase) == K_CB_PHASE_SUPPRESSED)) {
        pthread_mutex_unlock(&rt->mutex);
        return -ECANCELED;
    }
    if ((op->fully_parent || op->resolving) && op->deadline_nanos > 0) {
        timeout = op->deadline_nanos - k_monotonic_nanos();
        if (timeout <= 0) {
            pthread_mutex_unlock(&rt->mutex);
            return -ETIMEDOUT;
        }
    }
    if (atomic_load(&rt->state) != K_RT_RUNNING &&
        !(op->opcode == K_IO_OP_CLOSE && atomic_load(&rt->state) == K_RT_STOPPING)) {
        pthread_mutex_unlock(&rt->mutex);
        return -ESHUTDOWN;
    }
    KIoHandle* h = op->handle;
    if (h) {
        pthread_mutex_lock(&h->mutex);
        if (atomic_load(&h->state) != K_HANDLE_OPEN && op->opcode != K_IO_OP_CLOSE) {
            pthread_mutex_unlock(&h->mutex);
            pthread_mutex_unlock(&rt->mutex);
            return -EBADF;
        }
        bool read = op->opcode == K_IO_OP_READ || op->opcode == K_IO_OP_RECV ||
                    op->opcode == K_IO_OP_RECVMSG;
        bool write = op->opcode == K_IO_OP_WRITE || op->opcode == K_IO_OP_SEND ||
                     op->opcode == K_IO_OP_SENDMSG ||
                     (h->is_datagram_socket && op->opcode == K_IO_OP_CONNECT);
        bool socket = h->is_stream_socket || h->is_datagram_socket;
        bool parent_direction = op->fully_parent && op->fully_parent->handle == h &&
                                op->fully_parent->owns_direction &&
                                op->fully_parent->opcode == op->opcode;
        if (socket && !parent_direction && !op->owns_direction &&
                                 ((read && atomic_load(&h->read_inflight)) ||
                                  (write && atomic_load(&h->write_inflight)))) {
            pthread_mutex_unlock(&h->mutex);
            pthread_mutex_unlock(&rt->mutex);
            return -EBUSY;
        }
        op->fd = h->fd;
        if (socket && !parent_direction && !op->owns_direction) {
            if (read) atomic_store(&h->read_inflight, 1u);
            if (write) atomic_store(&h->write_inflight, 1u);
            op->owns_direction = read || write;
        }
        if (!op->handle_linked && !op->fully_parent) link_handle_op(h, op);
        pthread_mutex_unlock(&h->mutex);
    }
    if (op->opcode == K_IO_OP_SENDMSG && op->resolving && op->msghdr) {
        struct msghdr* message = op->msghdr;
        message->msg_name = op->sockaddr_storage;
        message->msg_namelen = op->sockaddr_len;
    }
    if (!rt->backend->ops->probe_opcode(rt->backend, op->opcode)) {
        if (timeout > 0) {
            int64_t now = k_monotonic_nanos();
            op->deadline_nanos = timeout > INT64_MAX - now ? INT64_MAX : now + timeout;
        }
        int rc = k_io_resolver_submit_control(rt->resolver, op);
        pthread_mutex_unlock(&rt->mutex);
        return rc;
    }
    uint64_t tag = registry_insert(rt, op, timeout > 0);
    if (!tag) {
        pthread_mutex_unlock(&rt->mutex);
        return -EBUSY;
    }
    pthread_mutex_lock(&op->lock);
    if (op->result_published) {
        pthread_mutex_unlock(&op->lock);
        rt->slots[tag >> 32].op = NULL;
        --rt->active;
        pthread_mutex_unlock(&rt->mutex);
        k_io_operation_release(op);
        return -EALREADY;
    }
    op->tag = tag;
    bool cancelled = atomic_load(&op->cb_phase) == K_CB_PHASE_SUPPRESSED;
    KIoBackendRequest req = {
        .opcode = op->opcode, .fd = op->fd, .buf = op->buf, .len = op->len,
        .offset = op->offset, .flags = op->flags, .tag = tag, .timeout_nanos = timeout,
        .addr = op->sockaddr_storage, .addrlen = op->sockaddr_len,
        .addrlen_ptr = (socklen_t*)&op->sockaddr_len,
        .timeout_storage = &op->timeout_storage, .message = op->msghdr, .fixed_index = -1
    };
    if (op->opcode == K_IO_OP_READ || op->opcode == K_IO_OP_WRITE) {
        if (op->buffer_obj) req.fixed_index = k_io_buffer_fixed_index(op->buffer_obj);
        else if (op->fully_parent)
            req.fixed_index = k_io_buffer_fixed_index(op->fully_parent->buffer_obj);
    }
    if (op->fully_parent &&
        atomic_load(&op->fully_parent->cb_phase) == K_CB_PHASE_SUPPRESSED) cancelled = true;
    int rc = cancelled ? -ECANCELED : rt->backend->ops->submit_request(rt->backend, &req);
    pthread_mutex_unlock(&op->lock);
    if (rc < 0) {
        rt->slots[tag >> 32].op = NULL;
        --rt->active;
        pthread_cond_broadcast(&rt->drained);
    }

    pthread_mutex_unlock(&rt->mutex);
    if (rc < 0) k_io_operation_release(op);
    return rc;
}

static int start_connect_resolve(KIoRuntime* rt, KNativeIoOperation* op) {
    pthread_mutex_lock(&rt->mutex);
    int rc = atomic_load(&rt->state) == K_RT_RUNNING ? 0 : -ESHUTDOWN;
    if (!rc) {
        pthread_mutex_lock(&op->handle->mutex);
        if (atomic_load(&op->handle->state) != K_HANDLE_OPEN) rc = -EBADF;
        else if (op->handle->is_datagram_socket &&
                 atomic_load(&op->handle->write_inflight)) rc = -EBUSY;
        else {
            if (op->handle->is_datagram_socket) {
                atomic_store(&op->handle->write_inflight, 1u);
                op->owns_direction = true;
            }
            if (!op->handle_linked) link_handle_op(op->handle, op);
        }
        pthread_mutex_unlock(&op->handle->mutex);
    }
    bool datagram = op->handle->is_datagram_socket;
    struct addrinfo hints = { .ai_family = AF_INET,
                             .ai_socktype = datagram ? SOCK_DGRAM : SOCK_STREAM,
                             .ai_protocol = datagram ? IPPROTO_UDP : IPPROTO_TCP };
    if (!rc) rc = k_io_resolver_submit_resolve(rt->resolver, op,
        op->resolver_node, op->resolver_service, &hints);
    pthread_mutex_unlock(&rt->mutex);
    return rc;
}

void k_io_runtime_connect_resolved(KNativeIoOperation* op, int result) {
    if (!result && k_io_operation_is_cancelled(op)) result = -ECANCELED;
    int64_t timeout = -1;
    if (!result && op->deadline_nanos > 0) {
        timeout = op->deadline_nanos - k_monotonic_nanos();
        if (timeout <= 0) result = -ETIMEDOUT;
    }
    if (!result) result = submit_target(k_io_runtime_get(), op, timeout);
    if (result == -ESHUTDOWN) k_io_operation_cancel(op);
    if (result < 0) k_io_operation_on_kernel_completion(op, result);
}

int k_io_runtime_cancel(KIoRuntime* rt, uint64_t tag) {
    if (!rt || fork_child) return -ESHUTDOWN;
    pthread_mutex_lock(&rt->mutex);
    int rc = rt->backend ? rt->backend->ops->submit_cancel(rt->backend, tag) : -ESHUTDOWN;
    pthread_mutex_unlock(&rt->mutex);
    return rc;
}
int k_io_runtime_submit_resolve(KIoRuntime* rt, KNativeIoOperation* op,
                                const char* node, const char* service, const void* hints) {
    if (!rt || !op || fork_child) return -ENOTSUP;
    if (atomic_exchange(&op->submitted, true)) return -EALREADY;
    int dispatch = prepare_callback_dispatch(op);
    if (dispatch < 0) return dispatch;
    pthread_mutex_lock(&rt->mutex);
    int rc = atomic_load(&rt->state) == K_RT_RUNNING
           ? k_io_resolver_submit_resolve(rt->resolver, op, node, service, hints)
           : -ESHUTDOWN;
    pthread_mutex_unlock(&rt->mutex);
    return rc;
}

static int shutdown_runtime(bool only_if_started) {
    if (fork_child)
        return only_if_started && !runtime && !system_executor ? 0 : -ENOTSUP;
    if (runtime_worker_context || reaper_context || k_execution_context_current() ||
        k_io_operation_is_callback_cleanup() || shutdown_context)
        return -EDEADLK;
    pthread_once(&lifecycle_once, register_lifecycle);
    pthread_mutex_lock(&system_mutex);
    pthread_mutex_lock(&init_mutex);
    if (only_if_started && !runtime && !system_executor && !shutdown_requested) {
        pthread_mutex_unlock(&init_mutex);
        pthread_mutex_unlock(&system_mutex);
        return 0;
    }
    pthread_mutex_unlock(&system_mutex);
    if (shutdown_requested) {
        while (!shutdown_complete) pthread_cond_wait(&shutdown_drained, &init_mutex);
        pthread_mutex_unlock(&init_mutex);
        return 0;
    }
    shutdown_requested = true;
    shutdown_context = true;
    KIoRuntime* rt = runtime;
    pthread_mutex_unlock(&init_mutex);
    if (rt) {
        pthread_mutex_lock(&rt->mutex);
        atomic_store(&rt->state, K_RT_STOPPING);
        pthread_mutex_unlock(&rt->mutex);
    }
    /* The delivery registry also covers explicit failures when no ring could
     * be created. Those callbacks still own Application captures and leases. */
    KNativeIoOperation* deliveries[K_IO_REGISTRY_MAX_CAP];
    unsigned count = 0;
    pthread_mutex_lock(&delivery_mutex);
    for (KNativeIoOperation* op = delivery_head; op; op = op->delivery_next) {
        k_io_operation_retain(op);
        deliveries[count++] = op;
    }
    pthread_mutex_unlock(&delivery_mutex);
    for (unsigned i = 0; i < count; ++i) {
        k_io_operation_cancel_for_shutdown(deliveries[i]);
        k_io_operation_release(deliveries[i]);
    }
    if (rt) {
        /* No new runtime submission can enter the resolver after STOPPING.
         * Cancellation must not hold the registry mutex while it locks an op. */
        k_io_resolver_stop_accepting(rt->resolver);
        for (unsigned i = 0; i < K_IO_REGISTRY_MAX_CAP; ++i) {
            pthread_mutex_lock(&rt->mutex);
            KNativeIoOperation* op = rt->slots[i].op;
            if (op) k_io_operation_retain(op);
            pthread_mutex_unlock(&rt->mutex);
            if (op) {
                k_io_operation_cancel(op);
                /* Retry a full cancellation SQ without freeing kernel storage. */
                int rc;
                if (op->opcode != K_IO_OP_CLOSE) {
                    do { rc = k_io_runtime_cancel(rt, op->tag); } while (rc == -EBUSY);
                }
                k_io_operation_release(op);
            }
        }
        pthread_mutex_lock(&rt->mutex);
        while (rt->active) pthread_cond_wait(&rt->drained, &rt->mutex);
        pthread_mutex_unlock(&rt->mutex);
        /* DNS/control jobs already accepted still own request storage. */
        k_io_resolver_destroy(rt->resolver);
        rt->resolver = NULL;
        pthread_mutex_lock(&rt->mutex);
        while (rt->active) pthread_cond_wait(&rt->drained, &rt->mutex);
        atomic_store(&rt->state, K_RT_STOPPED);
        pthread_mutex_unlock(&rt->mutex);
        if (rt->backend) {
            while (rt->backend->ops->wakeup(rt->backend) == -EBUSY) {}
        }
        k_runtime_thread_destroy(rt->reaper);
        rt->reaper = NULL;
    }
    for (;;) {
        pthread_mutex_lock(&delivery_mutex);
        KNativeIoOperation* delivery = delivery_head;
        k_io_operation_retain(delivery);
        pthread_mutex_unlock(&delivery_mutex);
        if (!delivery) break;
        k_io_operation_abandon_delivery(delivery);
        k_io_operation_release(delivery);
    }
    pthread_mutex_lock(&system_mutex);
    KSystemExecutor* ex = system_executor;
    if (ex) {
        ex->stopped = true;
        __k_native_queue_close(ex->queue);
    }
    pthread_mutex_unlock(&system_mutex);
    if (ex) {
        for (unsigned i = 0; i < 4; ++i) k_runtime_thread_destroy(ex->workers[i]);
        __k_native_queue_release(ex->queue);
        ex->queue = NULL;
    }
    if (rt) {
        k_io_buffer_shutdown_registration();
        pthread_mutex_lock(&rt->mutex);
        if (rt->backend) rt->backend->ops->destroy(rt->backend);
        rt->backend = NULL;
        atomic_store(&rt->state, K_RT_STOPPED);
        pthread_mutex_unlock(&rt->mutex);
        /* Keep the small tombstone valid for surviving observers and leases. */
    }
    pthread_mutex_lock(&init_mutex);
    shutdown_complete = true;
    pthread_cond_broadcast(&shutdown_drained);
    pthread_mutex_unlock(&init_mutex);
    shutdown_context = false;
    return 0;
}

int k_io_runtime_request_shutdown(void) {
    return shutdown_runtime(false);
}

int k_io_runtime_shutdown_if_started(void) {
    return shutdown_runtime(true);
}

void k_io_runtime_shutdown(void) {
    int rc = k_io_runtime_request_shutdown();
    if (rc < 0)
        fprintf(stderr, "K runtime: shutdown failed: %s (%d)\n", strerror(-rc), -rc);
}

int k_io_runtime_register_buffer(void* data, size_t length) {
    KIoRuntime* rt = k_io_runtime_get();
    if (!rt) return -ENOTSUP;
    pthread_mutex_lock(&rt->mutex);
    int rc = atomic_load(&rt->state) == K_RT_RUNNING && rt->backend->ops->register_buffer
           ? rt->backend->ops->register_buffer(rt->backend, data, length) : -ENOTSUP;
    pthread_mutex_unlock(&rt->mutex);
    return rc;
}
int k_io_runtime_unregister_buffer(int index) {
    if (!runtime || fork_child) return -ENOTSUP;
    pthread_mutex_lock(&runtime->mutex);
    int rc = runtime->backend && runtime->backend->ops->unregister_buffer
           ? runtime->backend->ops->unregister_buffer(runtime->backend, index) : -ENOTSUP;
    pthread_mutex_unlock(&runtime->mutex);
    return rc;
}
void k_io_runtime_get_metrics(KIoMetrics* out) {
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!runtime || fork_child) return;
    pthread_mutex_lock(&runtime->mutex);
    out->inflight_ops = runtime->active;
    pthread_mutex_unlock(&runtime->mutex);
    out->callbacks_queued = atomic_load(&runtime->callbacks_queued);
    out->callbacks_rejected = atomic_load(&runtime->callbacks_rejected);
    out->registered_buffers = atomic_load(&runtime->registered_buffers);
}
bool k_io_runtime_metric_callback_queued(void) {
    if (!runtime) return false;
    atomic_fetch_add(&runtime->callbacks_queued, 1);
    return true;
}
void k_io_runtime_metric_callback_finished(void) {
    if (runtime) atomic_fetch_sub(&runtime->callbacks_queued, 1);
}
void k_io_runtime_metric_callback_rejected(void) {
    if (runtime) atomic_fetch_add(&runtime->callbacks_rejected, 1);
}
int k_io_runtime_track_delivery(KNativeIoOperation* op) {
    if (fork_child) return -ENOTSUP;
    pthread_mutex_lock(&delivery_mutex);
    int rc = shutdown_requested ||
             (runtime && atomic_load(&runtime->state) != K_RT_RUNNING) ? -ESHUTDOWN
           : delivery_count == K_IO_REGISTRY_MAX_CAP ? -EBUSY : 0;
    if (!rc) {
        k_io_operation_retain(op);
        op->delivery_linked = true;
        op->delivery_next = delivery_head;
        if (op->delivery_next) op->delivery_next->delivery_prev = op;
        delivery_head = op;
        ++delivery_count;
    }
    pthread_mutex_unlock(&delivery_mutex);
    return rc;
}
void k_io_runtime_untrack_delivery(KNativeIoOperation* op) {
    if (fork_child) return;
    pthread_mutex_lock(&delivery_mutex);
    bool linked = op->delivery_linked;
    if (linked) {
        if (op->delivery_prev) op->delivery_prev->delivery_next = op->delivery_next;
        else delivery_head = op->delivery_next;
        if (op->delivery_next) op->delivery_next->delivery_prev = op->delivery_prev;
        op->delivery_prev = op->delivery_next = NULL;
        op->delivery_linked = false;
        --delivery_count;
    }
    pthread_mutex_unlock(&delivery_mutex);
    if (linked) k_io_operation_release(op);
}
void k_io_runtime_metric_buffer_registered(void) {
    if (runtime) atomic_fetch_add(&runtime->registered_buffers, 1);
}
void k_io_runtime_metric_buffer_unregistered(void) {
    if (runtime) atomic_fetch_sub(&runtime->registered_buffers, 1);
}

KIoHandle* k_io_handle_create(int fd, bool stream) {
    KIoHandle* h = calloc(1, sizeof(*h));
    if (!h) return NULL;
    h->fd = fd;
    h->owns_fd = true;
    h->creator_pid = getpid();
    h->is_stream_socket = stream;
    atomic_init(&h->refs, 1u);
    atomic_init(&h->state, K_HANDLE_OPEN);
    atomic_init(&h->read_inflight, 0u);
    atomic_init(&h->write_inflight, 0u);
    pthread_mutex_init(&h->mutex, NULL);
    pthread_cond_init(&h->drained, NULL);
    return h;
}
void k_io_handle_retain(KIoHandle* h) { if (h) atomic_fetch_add(&h->refs, 1u); }
void k_io_handle_release(KIoHandle* h) {
    if (!h || h->creator_pid != getpid() || atomic_fetch_sub(&h->refs, 1u) != 1u) return;
    if (h->owns_fd && h->fd >= 0) close(h->fd);
    int cleanup = k_io_handle_cleanup_path(h);
    if (cleanup < 0)
        fprintf(stderr, "klang: socket pathname cleanup: %s\n", strerror(-cleanup));
    __k_io_address_release(h->remote_address);
    pthread_cond_destroy(&h->drained);
    pthread_mutex_destroy(&h->mutex);
    free(h);
}
int k_io_handle_cleanup_path(KIoHandle* h) {
    pthread_mutex_lock(&h->mutex);
    char* path = h->owned_socket_path;
    h->owned_socket_path = NULL;
    dev_t device = h->socket_path_device;
    ino_t inode = h->socket_path_inode;
    pthread_mutex_unlock(&h->mutex);
    if (!path) return 0;
    struct stat identity;
    int rc = 0;
    if (lstat(path, &identity) < 0) {
        if (errno != ENOENT) rc = -errno;
    } else if (identity.st_dev == device && identity.st_ino == inode &&
               S_ISSOCK(identity.st_mode) && unlink(path) < 0 && errno != ENOENT) rc = -errno;
    free(path);
    return rc;
}
int k_io_handle_fd(const KIoHandle* h) {
    if (!h || h->creator_pid != getpid()) return -1;
    pthread_mutex_lock((pthread_mutex_t*)&h->mutex);
    int fd = h->fd;
    pthread_mutex_unlock((pthread_mutex_t*)&h->mutex);
    return fd;
}
bool k_io_handle_is_open(const KIoHandle* h) {
    return h && h->creator_pid == getpid() && atomic_load(&h->state) == K_HANDLE_OPEN;
}
int64_t k_io_handle_position(const KIoHandle* h) {
    if (!h || h->creator_pid != getpid()) return 0;
    pthread_mutex_lock((pthread_mutex_t*)&h->mutex);
    int64_t position = h->file_position;
    pthread_mutex_unlock((pthread_mutex_t*)&h->mutex);
    return position;
}
int k_io_handle_set_position(KIoHandle* h, int64_t position) {
    if (!h || h->creator_pid != getpid()) return -EBADF;
    pthread_mutex_lock(&h->mutex);
    int rc = position < 0 ? -EINVAL
           : atomic_load(&h->state) != K_HANDLE_OPEN ? -EBADF
           : h->position_count ? -EBUSY : 0;
    if (!rc) h->file_position = position;
    pthread_mutex_unlock(&h->mutex);
    return rc;
}
static void submit_close(KNativeIoOperation* op) {
    k_io_runtime_promote(op); /* consumes pending-close ref */
}
KNativeIoOperation* k_io_handle_unlink_op(KIoHandle* h, KNativeIoOperation* op) {
    pthread_mutex_lock(&h->mutex);
    KNativeIoOperation* ready = NULL;
    bool queued = op->implicit_queued;
    if (queued) {
        if (!op->fully) h->file_position += op->bytes_transferred;
        h->position_head = op->position_next;
        if (!h->position_head) h->position_tail = NULL;
        --h->position_count;
        op->position_next = NULL;
        op->implicit_queued = false;
        ready = h->position_head;
        if (ready) k_io_operation_retain(ready);
    }
    if (op->opcode == K_IO_OP_FTRUNCATE && op->res >= 0 &&
        h->file_position > op->offset) h->file_position = op->offset;
    if (op->prev) op->prev->next = op->next;
    else if (h->inflight_head == op) h->inflight_head = op->next;
    if (op->next) op->next->prev = op->prev;
    op->prev = op->next = NULL;
    if ((h->is_stream_socket || h->is_datagram_socket) && op->owns_direction) {
        if (op->opcode == K_IO_OP_READ || op->opcode == K_IO_OP_RECV ||
            op->opcode == K_IO_OP_RECVMSG)
            atomic_store(&h->read_inflight, 0u);
        if (op->opcode == K_IO_OP_WRITE || op->opcode == K_IO_OP_SEND ||
            op->opcode == K_IO_OP_SENDMSG || op->opcode == K_IO_OP_CONNECT)
            atomic_store(&h->write_inflight, 0u);
        op->owns_direction = false;
    }
    if (h->inflight_count) --h->inflight_count;
    if (!h->inflight_count) {
        ready = h->close_operation;
        h->close_operation = NULL;
        pthread_cond_broadcast(&h->drained);
    }
    pthread_mutex_unlock(&h->mutex);
    if (queued) k_io_operation_release(op); /* FIFO ref, completion retains op */
    return ready;
}
KNativeIoOperation* k_io_handle_close(KIoHandle* h, const KOwnedCallback* cb,
                                      KExecutorLease* lease, KFutureState* fut) {
    if (!h || h->creator_pid != getpid()) return NULL;
    KNativeIoOperation* op = k_io_operation_create(K_IO_OP_CLOSE, -1, NULL, 0, 0, 0,
                                                   NULL, cb, lease, fut);
    if (!op) return NULL;
    k_io_handle_retain(h);
    op->handle = h;
    int rc = k_io_runtime_submit(k_io_runtime_get(), op, -1);
    if (rc < 0) k_io_operation_on_kernel_completion(op, rc);
    return op;
}

static int schedule_close(KNativeIoOperation* op) {
    KIoHandle* h = op->handle;
    pthread_mutex_lock(&h->mutex);
    if (atomic_load(&h->state) != K_HANDLE_OPEN) {
        bool closed = atomic_load(&h->state) == K_HANDLE_CLOSED;
        op->fd = -1; /* observer is not the owner of the pending close */
        pthread_mutex_unlock(&h->mutex);
        k_io_operation_on_kernel_completion(op, closed ? 0 : -EBUSY);
        return 0;
    }
    atomic_store(&h->state, K_HANDLE_CLOSING);
    op->fd = h->fd;
    /* Snapshot refs, release handle lock before operation lock/cancel. */
    KNativeIoOperation** pending = h->inflight_count
        ? calloc(h->inflight_count, sizeof(*pending)) : NULL;
    if (h->inflight_count && !pending) {
        atomic_store(&h->state, K_HANDLE_OPEN);
        pthread_mutex_unlock(&h->mutex);
        return -ENOMEM;
    }
    unsigned count = 0;
    for (KNativeIoOperation* cur = h->inflight_head; cur; cur = cur->next) {
        k_io_operation_retain(cur);
        pending[count++] = cur;
    }
    k_io_operation_retain(op);
    bool submit = h->inflight_count == 0;
    if (!submit) h->close_operation = op;
    pthread_mutex_unlock(&h->mutex);
    for (unsigned i = 0; i < count; ++i) {
        k_io_operation_cancel(pending[i]);
        k_io_operation_release(pending[i]);
    }
    free(pending);
    if (submit) submit_close(op);
    return 0;
}
int k_io_handle_close_sync(KIoHandle* h) {
    if (reaper_context) return -EDEADLK;
    KNativeIoOperation* op = k_io_handle_close(h, NULL, NULL, NULL);
    if (!op) return -EINVAL;
    int rc = k_io_operation_wait_quiescent(op, -1);
    if (!rc) rc = -op->err_no;
    k_io_operation_release(op);
    return rc;
}
