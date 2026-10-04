/*
 * K Language runtime — operation ownership and independent I/O/callback states
 * Copyright 2023-2026 Emilien Kia
 * Licensed under the Apache License, Version 2.0.
 */
#define _GNU_SOURCE
#include "io_operation.h"
#include "io_datagram_ffi.h"
#include "io_runtime.h"
#include "io_buffer.h"
#include "park_lot.h"
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

static _Thread_local KNativeIoOperation* current_context;
static _Thread_local bool callback_cleanup_context;
bool k_io_operation_is_callback_cleanup(void) { return callback_cleanup_context; }
KNativeIoOperation* k_execution_context_current(void) { return current_context; }
void k_execution_context_set_current(KNativeIoOperation* op) { current_context = op; }
bool k_execution_context_is_cancellation_requested(void) {
    return current_context && k_io_operation_is_interruption_requested(current_context);
}

void k_executor_lease_retain(KExecutorLease* lease) {
    if (lease && lease->ops && lease->ops->retain) lease->ops->retain(lease);
}
void k_executor_lease_release(KExecutorLease* lease) {
    if (lease && lease->ops && lease->ops->release) lease->ops->release(lease);
}
int k_executor_lease_submit(KExecutorLease* lease, void (*run)(void*),
                           void (*drop)(void*), void* arg) {
    if (!lease || !lease->ops || !lease->ops->submit) {
        if (drop) drop(arg);
        return -ESHUTDOWN;
    }
    /* All endpoints consume the task, including on rejection. */
    return lease->ops->submit(lease, run, drop, arg);
}

KIoErrorKind k_io_errno_to_error_kind(int err) {
    switch (err) {
        case ECANCELED: return K_IO_ERR_CANCELLED;
        case EINTR: return K_IO_ERR_INTERRUPTED;
        case ETIMEDOUT: case ETIME: return K_IO_ERR_TIMEOUT;
        case EBADF: case EPIPE: case ECONNRESET: case ENOTCONN: case ESHUTDOWN:
            return K_IO_ERR_CLOSED;
        case ENOTSUP:
#if EOPNOTSUPP != ENOTSUP
        case EOPNOTSUPP:
#endif
        case ENOSYS: return K_IO_ERR_UNSUPPORTED;
        case ENOMEM: case EMFILE: case ENFILE: case ENOBUFS: case EAGAIN: case EBUSY:
            return K_IO_ERR_RESOURCE_EXHAUSTED;
        case EINVAL: case EDESTADDRREQ: case EPROTOTYPE: case EAFNOSUPPORT: case EFAULT:
        case ENAMETOOLONG: case EILSEQ:
            return K_IO_ERR_INVALID_ARGUMENT;
        default: return K_IO_ERR_SYSTEM;
    }
}

static void drop_callback(KNativeIoOperation* op) {
    if (op->has_callback && !atomic_exchange(&op->callback_dropped, 1u)) {
        bool previous = callback_cleanup_context;
        callback_cleanup_context = true;
        if (op->callback.drop) op->callback.drop(op->callback.context);
        if (op->callback_counted) k_io_runtime_metric_callback_finished();
        pthread_mutex_lock(&op->lock);
        KExecutorLease* lease = op->lease;
        op->lease = NULL;
        pthread_mutex_unlock(&op->lock);
        k_executor_lease_release(lease);
        callback_cleanup_context = previous;
        pthread_mutex_lock(&op->lock);
        op->callback_drop_complete = true;
        pthread_cond_broadcast(&op->cond);
        pthread_mutex_unlock(&op->lock);
        k_io_runtime_untrack_delivery(op);
    }
}

/* Called with op->lock held, before a descriptor has been delivered. */
static void rollback(KNativeIoOperation* op) {
    if (op->publication_in_progress || op->resource_claimed) return;
    if (op->produced_fd >= 0) {
        close(op->produced_fd);
        op->produced_fd = -1;
    }
    if (op->opcode == K_IO_OP_CONNECT && op->handle) {
        pthread_mutex_lock(&op->handle->mutex);
        if (atomic_load(&op->handle->state) != K_HANDLE_CLOSING) {
            if (op->handle->fd >= 0) close(op->handle->fd);
            op->handle->fd = -1;
            atomic_store(&op->handle->state, K_HANDLE_CLOSED);
            pthread_cond_broadcast(&op->handle->drained);
        }
        pthread_mutex_unlock(&op->handle->mutex);
    }
}

KNativeIoOperation* k_io_operation_create(KIoOpcode opcode, int fd, void* buf,
    uint32_t len, int64_t offset, int flags, void* buffer_obj,
    const KOwnedCallback* cb, KExecutorLease* lease, KFutureState* fut) {
    if (fut) { errno = EINVAL; return NULL; }
    KNativeIoOperation* op = calloc(1, sizeof(*op));
    if (!op) return NULL;
    op->creator_pid = getpid();
    if (buffer_obj && ((KNativeByteBuffer*)buffer_obj)->creator_pid != getpid()) {
        free(op);
        return NULL;
    }
    atomic_init(&op->ref_count, 1u); /* observer; registry/task refs are independent */
    atomic_init(&op->submitted, false);
    atomic_init(&op->io_phase, K_IO_PHASE_SUBMITTED);
    atomic_init(&op->cb_phase, K_CB_PHASE_NONE);
    atomic_init(&op->callback_dropped, 0u);
    atomic_init(&op->callback_dispatched, 0u);
    atomic_init(&op->interruption_requested, 0u);
    pthread_mutex_init(&op->lock, NULL);
    pthread_condattr_t attr;
    pthread_condattr_init(&attr);
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
    pthread_cond_init(&op->cond, &attr);
    pthread_condattr_destroy(&attr);
    op->opcode = opcode;
    op->fd = fd;
    op->buf = buf;
    op->len = len;
    op->offset = offset;
    op->flags = flags;
    bool transfer = opcode == K_IO_OP_READ || opcode == K_IO_OP_WRITE ||
                    opcode == K_IO_OP_RECV || opcode == K_IO_OP_SEND ||
                    opcode == K_IO_OP_RECVMSG || opcode == K_IO_OP_SENDMSG;
    if (transfer && len && !buf && !buffer_obj) op->request_error = EINVAL;
    op->produced_fd = -1;
    op->buffer_obj = buffer_obj;
    if (buffer_obj) {
        KNativeByteBuffer* buffer = buffer_obj;
        k_io_buffer_acquire_lease(buffer);
        pthread_mutex_lock(&buffer->lock);
        bool read = opcode == K_IO_OP_READ || opcode == K_IO_OP_RECV ||
                    opcode == K_IO_OP_RECVMSG;
        if (atomic_load(&buffer->lease_count) != 1u) op->request_error = EBUSY;
        else if (read && buffer->storage_type == K_BUFFER_MAPPED &&
                 buffer->map_mode == K_MAP_READ_ONLY) op->request_error = EACCES;
        else if (len > buffer->limit - buffer->position) op->request_error = EINVAL;
        op->buf = buffer->data ? buffer->data + buffer->position : NULL;
        pthread_mutex_unlock(&buffer->lock);
    }
    if (cb) {
        op->callback = *cb;
        op->has_callback = cb->invoke || cb->drop;
    }
    op->lease = lease;
    k_executor_lease_retain(lease);
    return op;
}
void k_io_operation_retain(KNativeIoOperation* op) {
    if (op) atomic_fetch_add_explicit(&op->ref_count, 1u, memory_order_relaxed);
}
void k_io_operation_release(KNativeIoOperation* op) {
    if (!op || op->creator_pid != getpid() ||
        atomic_fetch_sub_explicit(&op->ref_count, 1u, memory_order_acq_rel) != 1u)
        return;
    drop_callback(op);
    if (op->continuation.drop) op->continuation.drop(op->continuation.context);
    if (op->produced_fd >= 0) close(op->produced_fd);
    if (op->buffer_obj) k_io_buffer_release_lease(op->buffer_obj);
    if (op->owns_request_buffer) free(op->buf);
    free(op->sockaddr_storage);
    free(op->resolver_node);
    free(op->resolver_service);
    if (op->handle && op->handle->is_datagram_socket)
        __k_io_datagram_message_release(op->msghdr);
    else free(op->msghdr);
    k_io_handle_release(op->handle);
    k_executor_lease_release(op->lease);
    pthread_cond_destroy(&op->cond);
    pthread_mutex_destroy(&op->lock);
    free(op);
}

static bool future_cancel(void* ctx) { return k_io_operation_cancel(ctx); }
static int future_wait(void* ctx, int64_t timeout) {
    return k_io_operation_wait_quiescent(ctx, timeout);
}
static void future_drop(void* ctx) { k_io_operation_release(ctx); }
void k_io_operation_bind_future(KNativeIoOperation* op, KFutureState* future) {
    /* Hooks each own an op ref. Op does not retain Future: no ownership cycle. */
    k_io_operation_retain(op);
    k_future_state_set_cancel_hook(future, op, future_cancel, future_drop);
    if (op->fully) k_future_state_defer_cancellation(future);
    k_io_operation_retain(op);
    k_future_state_set_quiescent_hook(future, op, future_wait, future_drop);
}
int k_io_operation_set_continuation(KNativeIoOperation* op, const KOwnedCallback* cb) {
    if (!op || !cb) return -EINVAL;
    pthread_mutex_lock(&op->lock);
    bool invalid = atomic_load(&op->submitted) || op->result_published ||
                   op->continuation.invoke || op->continuation.drop;
    if (!invalid) op->continuation = *cb;
    pthread_mutex_unlock(&op->lock);
    if (invalid && cb->drop) cb->drop(cb->context);
    return invalid ? -EINVAL : 0;
}

bool k_io_operation_is_io_done(const KNativeIoOperation* op) {
    return op && atomic_load_explicit(&op->io_phase, memory_order_acquire) >=
                 K_IO_PHASE_KERNEL_COMPLETED;
}
bool k_io_operation_is_callback_running(const KNativeIoOperation* op) {
    return op && atomic_load(&op->cb_phase) == K_CB_PHASE_RUNNING;
}
bool k_io_operation_is_done(const KNativeIoOperation* op) {
    if (!k_io_operation_is_io_done(op)) return false;
    unsigned cb = atomic_load(&op->cb_phase);
    return (!op->has_callback && cb == K_CB_PHASE_NONE) || cb >= K_CB_PHASE_SUPPRESSED;
}
KIoOperationStage k_io_operation_derive_stage(const KNativeIoOperation* op) {
    unsigned cb = atomic_load(&op->cb_phase);
    if (cb == K_CB_PHASE_RUNNING)
        return atomic_load(&op->interruption_requested)
             ? K_IO_STAGE_INTERRUPTION_REQUESTED : K_IO_STAGE_CALLBACK_RUNNING;
    if (!k_io_operation_is_io_done(op))
        return atomic_load(&op->io_phase) == K_IO_PHASE_CANCEL_REQUESTED
             ? K_IO_STAGE_CANCELLING : K_IO_STAGE_PENDING;
    switch (cb) {
        case K_CB_PHASE_QUEUED: return K_IO_STAGE_CALLBACK_QUEUED;
        case K_CB_PHASE_SUPPRESSED: return K_IO_STAGE_CANCELLED;
        case K_CB_PHASE_INTERRUPTED: return K_IO_STAGE_INTERRUPTED;
        case K_CB_PHASE_REJECTED: return K_IO_STAGE_CALLBACK_REJECTED;
        default: return op->err_no == ECANCELED ? K_IO_STAGE_CANCELLED : K_IO_STAGE_COMPLETED;
    }
}
bool k_io_operation_is_cancelled(const KNativeIoOperation* op) {
    return op && (atomic_load(&op->cb_phase) == K_CB_PHASE_SUPPRESSED ||
                  atomic_load(&op->cb_phase) == K_CB_PHASE_INTERRUPTED ||
                  (k_io_operation_is_io_done(op) && op->err_no == ECANCELED));
}
bool k_io_operation_is_interruption_requested(const KNativeIoOperation* op) {
    return op && atomic_load(&op->interruption_requested);
}
int64_t k_io_operation_bytes_transferred(const KNativeIoOperation* op) {
    if (!op || op->creator_pid != getpid()) return 0;
    pthread_mutex_lock((pthread_mutex_t*)&op->lock);
    int64_t bytes = op->fully || op->result_published ? op->bytes_transferred : 0;
    pthread_mutex_unlock((pthread_mutex_t*)&op->lock);
    return bytes;
}
void k_io_operation_error(const KNativeIoOperation* op, KIoResult* result) {
    if (!result) return;
    if (op->creator_pid != getpid()) {
        *result = (KIoResult){K_IO_ERR_UNSUPPORTED, ENOTSUP, 0, 0, 0};
        return;
    }
    pthread_mutex_lock((pthread_mutex_t*)&op->lock);
    result->err_no = op->result_published ? op->err_no : 0;
    result->kind = op->result_published ? op->result_kind : K_IO_ERR_SYSTEM;
    if (atomic_load(&op->cb_phase) == K_CB_PHASE_SUPPRESSED)
        result->kind = K_IO_ERR_CANCELLED;
    if (atomic_load(&op->cb_phase) == K_CB_PHASE_REJECTED)
        result->kind = K_IO_ERR_EXECUTOR_REJECTED;
    result->bytes_transferred = op->fully || op->result_published ? op->bytes_transferred : 0;
    result->value = op->opcode == K_IO_OP_STATX && op->res >= 0
                  ? (intptr_t)op->sockaddr_storage
                  : op->opcode == K_IO_OP_RECVMSG && op->res >= 0 &&
                    op->handle && op->handle->is_datagram_socket
                  ? (intptr_t)op->msghdr : op->res;
    result->resource_claimed = op->resource_claimed;
    pthread_mutex_unlock((pthread_mutex_t*)&op->lock);
}

bool k_io_operation_cancel(KNativeIoOperation* op) {
    if (!op || op->creator_pid != getpid() || op->opcode == K_IO_OP_CLOSE) return false;
    pthread_mutex_lock(&op->lock);
    unsigned cb = atomic_load(&op->cb_phase);
    if (cb == K_CB_PHASE_RUNNING) {
        bool first = !atomic_exchange(&op->interruption_requested, 1u);
        pthread_mutex_unlock(&op->lock);
        return first;
    }
    if (cb >= K_CB_PHASE_SUPPRESSED ||
        (op->result_published && !op->has_callback)) {
        pthread_mutex_unlock(&op->lock);
        return false;
    }
    atomic_store(&op->cb_phase, K_CB_PHASE_SUPPRESSED);
    bool pending = !op->result_published;
    if (pending) atomic_store(&op->io_phase, K_IO_PHASE_CANCEL_REQUESTED);
    else rollback(op);
    uint64_t tag = op->tag;
    KNativeIoOperation* child = op->fully_child;
    k_io_operation_retain(child);
    pthread_mutex_unlock(&op->lock);
    if (child) {
        k_io_operation_cancel(child);
        k_io_operation_release(child);
    }
    /* Queued task owns the callback until its drop; no racing destructor. */
    if (pending && tag) k_io_runtime_cancel(k_io_runtime_get(), tag);
    return true;
}

static void reject_callback(KNativeIoOperation* op) {
    pthread_mutex_lock(&op->lock);
    unsigned cb = atomic_load(&op->cb_phase);
    if (cb == K_CB_PHASE_QUEUED || cb == K_CB_PHASE_NONE) {
        atomic_store(&op->cb_phase, K_CB_PHASE_REJECTED);
        k_io_runtime_metric_callback_rejected();
    }

    rollback(op);
    pthread_mutex_unlock(&op->lock);
}

void k_io_operation_cancel_for_shutdown(KNativeIoOperation* op) {
    k_io_operation_cancel(op);
    pthread_mutex_lock(&op->lock);
    if (op->has_callback) {
        unsigned cb = atomic_load(&op->cb_phase);
        if (cb == K_CB_PHASE_RUNNING) atomic_store(&op->interruption_requested, 1u);
        else if (cb == K_CB_PHASE_QUEUED) {
            atomic_store(&op->cb_phase, K_CB_PHASE_SUPPRESSED);
            rollback(op);
        }
    }
    pthread_mutex_unlock(&op->lock);
}

void k_io_operation_abandon_delivery(KNativeIoOperation* op) {
    k_io_operation_cancel_for_shutdown(op);
    pthread_mutex_lock(&op->lock);
    unsigned cb = atomic_load(&op->cb_phase);
    /* CLOSE itself is not cancellable, but its queued user delivery is. */
    if (cb == K_CB_PHASE_NONE || cb == K_CB_PHASE_QUEUED) {
        atomic_store(&op->cb_phase, K_CB_PHASE_SUPPRESSED);
        rollback(op);
        cb = K_CB_PHASE_SUPPRESSED;
    }
    pthread_mutex_unlock(&op->lock);
    if (cb == K_CB_PHASE_SUPPRESSED) drop_callback(op);
    pthread_mutex_lock(&op->lock);
    while (!op->callback_drop_complete) pthread_cond_wait(&op->cond, &op->lock);
    pthread_mutex_unlock(&op->lock);
    k_io_runtime_untrack_delivery(op);
}

void k_io_operation_run_callback_trampoline(void* arg) {
    KNativeIoOperation* op = arg;
    pthread_mutex_lock(&op->lock);
    if (atomic_load(&op->cb_phase) != K_CB_PHASE_QUEUED) {
        pthread_mutex_unlock(&op->lock);
        return;
    }
    atomic_store(&op->cb_phase, K_CB_PHASE_RUNNING);
    KIoResult result = { op->result_kind,
                        op->err_no, op->bytes_transferred,
                        op->opcode == K_IO_OP_STATX && op->res >= 0
                            ? (intptr_t)op->sockaddr_storage
                            : op->opcode == K_IO_OP_RECVMSG && op->res >= 0 &&
                              op->handle && op->handle->is_datagram_socket
                            ? (intptr_t)op->msghdr
                            : op->connect_owned && op->res >= 0 ? op->fd : op->res, 0 };
    pthread_mutex_unlock(&op->lock);
    KNativeIoOperation* previous = current_context;
    current_context = op;
    if (op->callback.invoke) op->callback.invoke(op->callback.context, &result);
    current_context = previous;
    pthread_mutex_lock(&op->lock);
    if (result.resource_claimed) {
        op->resource_claimed = true;
        op->produced_fd = -1;
    } else rollback(op);
    atomic_store(&op->cb_phase, atomic_load(&op->interruption_requested)
                 ? K_CB_PHASE_INTERRUPTED : K_CB_PHASE_COMPLETED);
    pthread_cond_broadcast(&op->cond);
    pthread_mutex_unlock(&op->lock);
}
void k_io_operation_drop_callback_trampoline(void* arg) {
    KNativeIoOperation* op = arg;
    unsigned cb = atomic_load(&op->cb_phase);
    if (cb != K_CB_PHASE_COMPLETED && cb != K_CB_PHASE_INTERRUPTED) reject_callback(op);
    drop_callback(op);
    k_io_operation_release(op); /* task ref, on every accepted/rejected path */
}

static void dispatch_run(void* arg) {
    KNativeIoOperation* op = arg;
    atomic_store(&op->callback_dispatched, 1u);
    k_io_operation_retain(op);
    pthread_mutex_lock(&op->lock);
    KExecutorLease* lease = op->lease;
    k_executor_lease_retain(lease);
    pthread_mutex_unlock(&op->lock);
    if (lease) {
        k_executor_lease_submit(lease, k_io_operation_run_callback_trampoline,
                                k_io_operation_drop_callback_trampoline, op);
        k_executor_lease_release(lease);
    } else {
        k_io_operation_run_callback_trampoline(op);
        k_io_operation_drop_callback_trampoline(op);
    }
}
static void dispatch_drop(void* arg) {
    KNativeIoOperation* op = arg;
    if (atomic_load(&op->cb_phase) == K_CB_PHASE_QUEUED) {
        /* A custom lease may have queued it. Only reject if dispatch never ran. */
        if (!atomic_load(&op->callback_dispatched)) {
            reject_callback(op);
            drop_callback(op);
        }
    } else if (atomic_load(&op->cb_phase) == K_CB_PHASE_SUPPRESSED) {
        drop_callback(op);
    }
    k_io_operation_release(op);
}

static void dispatch_abandoned(void* arg) { drop_callback(arg); }

KNativeIoOperation* k_io_operation_retain_fully_child(KNativeIoOperation* op) {
    if (!op || op->creator_pid != getpid()) return NULL;
    pthread_mutex_lock(&op->lock);
    KNativeIoOperation* child = op->fully_child;
    k_io_operation_retain(child);
    pthread_mutex_unlock(&op->lock);
    return child;
}

KIoHandle* k_io_operation_retain_handle(KNativeIoOperation* op) {
    if (!op || op->creator_pid != getpid()) return NULL;
    pthread_mutex_lock(&op->lock);
    KIoHandle* handle = op->handle;
    k_io_handle_retain(handle);
    pthread_mutex_unlock(&op->lock);
    return handle;
}

void k_io_operation_start_fully(KNativeIoOperation* op) {
    pthread_mutex_lock(&op->lock);
    int32_t result = (int32_t)op->bytes_transferred;
    int64_t timeout = -1;
    if (atomic_load(&op->cb_phase) == K_CB_PHASE_SUPPRESSED) result = -ECANCELED;
    else if (op->bytes_transferred < op->len && op->deadline_nanos > 0) {
        timeout = op->deadline_nanos - k_monotonic_nanos();
        if (timeout <= 0) result = -ETIMEDOUT;
    }
    if (result < 0 || op->bytes_transferred == op->len) {
        pthread_mutex_unlock(&op->lock);
        k_io_operation_on_kernel_completion(op, result);
        return;
    }
    KNativeIoOperation* child = k_io_operation_create(op->opcode, op->fd,
        (unsigned char*)op->buf + op->bytes_transferred,
        op->len - (uint32_t)op->bytes_transferred,
        op->handle->is_stream_socket ? 0 : op->offset + op->bytes_transferred,
        op->flags, NULL, NULL, NULL, NULL);
    if (!child) {
        pthread_mutex_unlock(&op->lock);
        k_io_operation_on_kernel_completion(op, -ENOMEM);
        return;
    }
    child->handle = op->handle;
    k_io_handle_retain(child->handle);
    child->fully_parent = op;
    child->deadline_nanos = op->deadline_nanos;
    k_io_operation_retain(op);
    op->fully_child = child;
    pthread_mutex_unlock(&op->lock);
    /* Cancellation sees the child before registry admission. The submit path
     * checks its cancellation under the registry lock before publishing SQEs. */
    int rc = k_io_runtime_submit(k_io_runtime_get(), child, timeout);
    if (rc < 0) k_io_operation_on_kernel_completion(child, rc);
}

static void fully_child_drained(KNativeIoOperation* child) {
    KNativeIoOperation* parent = child->fully_parent;
    pthread_mutex_lock(&parent->lock);
    parent->fully_child = NULL;
    child->fully_parent = NULL;
    int32_t result = child->res;
    if (result > 0 && (uint32_t)result > child->len) result = -EIO;
    if (result > 0) {
        parent->bytes_transferred += result;
        k_io_buffer_advance_position(parent->buffer_obj, (size_t)result);
        if (parent->implicit_queued) {
            pthread_mutex_lock(&parent->handle->mutex);
            parent->handle->file_position += result;
            pthread_mutex_unlock(&parent->handle->mutex);
        }
    }
    bool terminal = result <= 0 || parent->bytes_transferred == parent->len;
    /* Shutdown may cancel the registry child directly rather than the handle's
     * parent. Preserve that suppression across the native composition boundary. */
    if (atomic_load(&child->cb_phase) == K_CB_PHASE_SUPPRESSED) {
        atomic_store(&parent->cb_phase, K_CB_PHASE_SUPPRESSED);
        atomic_store(&parent->io_phase, K_IO_PHASE_CANCEL_REQUESTED);
    }
    if (atomic_load(&parent->cb_phase) == K_CB_PHASE_SUPPRESSED) {
        result = -ECANCELED;
        terminal = true;
    } else if (result == 0) {
        parent->fully_eof = parent->opcode == K_IO_OP_READ || parent->opcode == K_IO_OP_RECV;
        result = parent->fully_eof ? -ENODATA : -EIO;
    } else if (result > 0) result = (int32_t)parent->bytes_transferred;
    pthread_mutex_unlock(&parent->lock);
    k_io_operation_release(child); /* parent's current-child ownership */
    if (terminal) k_io_operation_on_kernel_completion(parent, result);
    else k_io_operation_start_fully(parent);
    k_io_operation_release(parent); /* drained child's parent ownership */
}

void k_io_operation_on_kernel_completion(KNativeIoOperation* op, int32_t res) {
    if (op->opcode == K_IO_OP_SEND && op->len && res == 0) res = -EIO;
    k_io_operation_retain(op); /* unlink may consume the last FIFO reference */
    pthread_mutex_lock(&op->lock);
    if (op->result_published) {
        pthread_mutex_unlock(&op->lock);
        k_io_operation_release(op);
        return;
    }
    if (op->handle && op->handle->is_datagram_socket && res >= 0) {
        int rc = __k_io_datagram_finish(op, res);
        if (rc < 0) {
            /* A metadata failure must not erase irreversible packet bytes. */
            if (op->opcode == K_IO_OP_RECVMSG && op->msghdr)
                op->bytes_transferred = ((KDatagramMessage*)op->msghdr)->copied;
            res = rc;
        }
    }
    op->result_published = true;
    op->res = res;
    op->err_no = res < 0 ? -res : 0;
    op->result_kind = op->fully_eof ? K_IO_ERR_END_OF_STREAM
                                  : k_io_errno_to_error_kind(op->err_no);
    bool transfer = op->opcode == K_IO_OP_READ || op->opcode == K_IO_OP_WRITE ||
                    op->opcode == K_IO_OP_RECV || op->opcode == K_IO_OP_SEND ||
                    op->opcode == K_IO_OP_RECVMSG || op->opcode == K_IO_OP_SENDMSG;
    if (!op->fully && !(op->handle && op->handle->is_datagram_socket &&
                       op->opcode == K_IO_OP_RECVMSG))
        op->bytes_transferred = transfer && res > 0 ? res : 0;
    if (op->opcode == K_IO_OP_RECVMSG && op->handle &&
        op->handle->is_datagram_socket && op->msghdr)
        op->bytes_transferred = ((KDatagramMessage*)op->msghdr)->copied;
    if (res >= 0 && (op->opcode == K_IO_OP_OPENAT || op->opcode == K_IO_OP_ACCEPT))
        op->produced_fd = res;
    if (res >= 0 && op->connect_owned && op->handle) {
        pthread_mutex_lock(&op->handle->mutex);
        if (atomic_load(&op->handle->state) != K_HANDLE_CLOSING) {
            op->produced_fd = op->fd;
            op->handle->owns_fd = false;
            op->handle->fd = -1;
            atomic_store(&op->handle->state, K_HANDLE_CLOSED);
        }
        pthread_mutex_unlock(&op->handle->mutex);
    }
    if (op->buffer_obj) {
        if (!op->fully)
            k_io_buffer_advance_position(op->buffer_obj, (size_t)op->bytes_transferred);
        k_io_buffer_release_lease(op->buffer_obj);
        op->buffer_obj = NULL;
    }
    KNativeIoOperation* ready = NULL;
    if (op->handle_linked) {
        ready = k_io_handle_unlink_op(op->handle, op);
        op->handle_linked = false;
    }
    bool suppressed = atomic_load(&op->cb_phase) == K_CB_PHASE_SUPPRESSED;
    if (suppressed) rollback(op);
    if (op->opcode == K_IO_OP_CLOSE && op->handle && res >= 0) {
        pthread_mutex_lock(&op->handle->mutex);
        op->handle->fd = -1;
        atomic_store(&op->handle->state, K_HANDLE_CLOSED);
        pthread_cond_broadcast(&op->handle->drained);
        pthread_mutex_unlock(&op->handle->mutex);
        int cleanup = k_io_handle_cleanup_path(op->handle);
        if (cleanup < 0) {
            res = cleanup;
            op->res = cleanup;
            op->err_no = -cleanup;
            op->result_kind = k_io_errno_to_error_kind(-cleanup);
        }
    } else if (op->opcode == K_IO_OP_CLOSE && op->handle && op->fd >= 0) {
        pthread_mutex_lock(&op->handle->mutex);
        atomic_store(&op->handle->state, K_HANDLE_OPEN);
        pthread_cond_broadcast(&op->handle->drained);
        pthread_mutex_unlock(&op->handle->mutex);
    }
    atomic_store_explicit(&op->io_phase, K_IO_PHASE_QUIESCENT, memory_order_release);
    pthread_cond_broadcast(&op->cond);
    KOwnedCallback continuation = op->continuation;
    op->continuation = (KOwnedCallback){0};
    op->publication_in_progress = continuation.invoke != NULL;
    KIoResult result = { suppressed ? K_IO_ERR_CANCELLED : op->result_kind,
                        suppressed ? ECANCELED : op->err_no, op->bytes_transferred,
                        op->opcode == K_IO_OP_STATX && res >= 0
                            ? (intptr_t)op->sockaddr_storage
                            : op->opcode == K_IO_OP_RECVMSG && res >= 0 &&
                              op->handle && op->handle->is_datagram_socket
                            ? (intptr_t)op->msghdr
                            : op->connect_owned && res >= 0 ? op->fd : res, 0 };
    if (op->has_callback && !suppressed) atomic_store(&op->cb_phase, K_CB_PHASE_QUEUED);
    pthread_mutex_unlock(&op->lock);

    /* Only this internal, exception-contained trampoline may run on the reaper.
     * It writes the K payload and publishes Promise before any user dispatch. */
    if (continuation.invoke) continuation.invoke(continuation.context, &result);
    pthread_mutex_lock(&op->lock);
    op->publication_in_progress = false;
    op->resource_claimed = result.resource_claimed != 0;
    if (op->resource_claimed) {
        op->produced_fd = -1;
    } else if (atomic_load(&op->cb_phase) == K_CB_PHASE_SUPPRESSED ||
               (!op->has_callback && continuation.invoke)) rollback(op);
    pthread_mutex_unlock(&op->lock);
    if (continuation.drop) continuation.drop(continuation.context);
    if (op->has_callback) {
        k_io_operation_retain(op); /* dispatch task ref */
        op->callback_counted = k_io_runtime_metric_callback_queued();
        int delivery = k_io_runtime_track_delivery(op);
        if (delivery < 0) {
            if (delivery == -ESHUTDOWN) k_io_operation_cancel_for_shutdown(op);
            else reject_callback(op);
        }
        k_io_runtime_dispatch(delivery < 0 ? dispatch_abandoned : dispatch_run, dispatch_drop, op);
    }
    if (ready) k_io_runtime_promote(ready);
    if (op->fully_parent) fully_child_drained(op);
    k_io_operation_release(op);
}

int k_io_operation_wait_quiescent(KNativeIoOperation* op, int64_t timeout) {
    if (!op) return 0;
    if (op->creator_pid != getpid()) return ENOTSUP;
    if (k_io_runtime_is_reaper()) return EDEADLK;
    pthread_mutex_lock(&op->lock);
    struct timespec deadline;
    clock_gettime(CLOCK_MONOTONIC, &deadline);
    if (timeout >= 0) {
        deadline.tv_sec += timeout / 1000000000LL;
        deadline.tv_nsec += timeout % 1000000000LL;
        if (deadline.tv_nsec >= 1000000000L) {
            ++deadline.tv_sec;
            deadline.tv_nsec -= 1000000000L;
        }
    }
    int rc = 0;
    while (atomic_load(&op->io_phase) != K_IO_PHASE_QUIESCENT && !rc)
        rc = timeout < 0 ? pthread_cond_wait(&op->cond, &op->lock)
                         : pthread_cond_timedwait(&op->cond, &op->lock, &deadline);
    pthread_mutex_unlock(&op->lock);
    return rc;
}
