/*
 * K Language runtime — Linux io_uring backend implementation
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
#include "io_backend.h"

#include <liburing.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>

typedef struct KIoUringBackend {
    KIoBackend              base;
    struct io_uring         ring;
    pthread_mutex_t         submit_mutex;
    struct io_uring_probe*  probe;
    uint32_t                entries;
    bool                    sparse_available;
    bool                    buffer_slots[256];
} KIoUringBackend;

static bool uring_is_opcode_supported(struct io_uring_probe* probe, uint8_t op) {
    if (probe == NULL) {
        return false;
    }
    return io_uring_opcode_supported(probe, op);
}

static bool io_uring_backend_probe_opcode(KIoBackend* backend, KIoOpcode opcode) {
    KIoUringBackend* b = (KIoUringBackend*)backend;
    if (b->probe == NULL) {
        return false;
    }

    switch (opcode) {
        case K_IO_OP_READ:         return uring_is_opcode_supported(b->probe, IORING_OP_READ);
        case K_IO_OP_WRITE:        return uring_is_opcode_supported(b->probe, IORING_OP_WRITE);
        case K_IO_OP_RECV:         return uring_is_opcode_supported(b->probe, IORING_OP_RECV);
        case K_IO_OP_SEND:         return uring_is_opcode_supported(b->probe, IORING_OP_SEND);
        case K_IO_OP_ACCEPT:       return uring_is_opcode_supported(b->probe, IORING_OP_ACCEPT);
        case K_IO_OP_CONNECT:      return uring_is_opcode_supported(b->probe, IORING_OP_CONNECT);
        case K_IO_OP_CLOSE:        return uring_is_opcode_supported(b->probe, IORING_OP_CLOSE);
        case K_IO_OP_ASYNC_CANCEL: return uring_is_opcode_supported(b->probe, IORING_OP_ASYNC_CANCEL);
        case K_IO_OP_LINK_TIMEOUT: return uring_is_opcode_supported(b->probe, IORING_OP_LINK_TIMEOUT);
        case K_IO_OP_OPENAT:       return uring_is_opcode_supported(b->probe, IORING_OP_OPENAT);
        case K_IO_OP_STATX:        return uring_is_opcode_supported(b->probe, IORING_OP_STATX);
        case K_IO_OP_FSYNC:        return uring_is_opcode_supported(b->probe, IORING_OP_FSYNC);
        case K_IO_OP_RECVMSG:      return uring_is_opcode_supported(b->probe, IORING_OP_RECVMSG);
        case K_IO_OP_SENDMSG:      return uring_is_opcode_supported(b->probe, IORING_OP_SENDMSG);
        default:                   return false;
    }
}

static void io_uring_backend_destroy(KIoBackend* backend) {
    if (backend == NULL) {
        return;
    }
    KIoUringBackend* b = (KIoUringBackend*)backend;
    if (b->probe != NULL) {
        io_uring_free_probe(b->probe);
        b->probe = NULL;
    }
    io_uring_queue_exit(&b->ring);
    pthread_mutex_destroy(&b->submit_mutex);
    free(b);
}

static struct io_uring_sqe* get_sqe_with_flush(struct io_uring* ring) {
    struct io_uring_sqe* sqe = io_uring_get_sqe(ring);
    if (sqe == NULL) {
        io_uring_submit(ring);
        sqe = io_uring_get_sqe(ring);
    }
    return sqe;
}

static int io_uring_backend_submit_request(KIoBackend* backend, const KIoBackendRequest* req) {
    KIoUringBackend* b = (KIoUringBackend*)backend;

    pthread_mutex_lock(&b->submit_mutex);
    if (!io_uring_backend_probe_opcode(backend, req->opcode)) {
        pthread_mutex_unlock(&b->submit_mutex);
        return -ENOTSUP;
    }
    unsigned needed = req->timeout_nanos > 0 ? 2u : 1u;
    if (io_uring_sq_space_left(&b->ring) < needed) io_uring_submit(&b->ring);
    if (io_uring_sq_space_left(&b->ring) < needed) {
        pthread_mutex_unlock(&b->submit_mutex);
        return -EBUSY;
    }
    /* Reserve the pair before preparing either SQE; never flush a half link. */
    struct io_uring_sqe* sqe = io_uring_get_sqe(&b->ring);
    struct io_uring_sqe* sqe_to = needed == 2 ? io_uring_get_sqe(&b->ring) : NULL;

    switch (req->opcode) {
        case K_IO_OP_READ:
            if (req->fixed_index >= 0)
                io_uring_prep_read_fixed(sqe, req->fd, req->buf, req->len,
                                         req->offset, req->fixed_index);
            else io_uring_prep_read(sqe, req->fd, req->buf, req->len, (uint64_t)req->offset);
            break;
        case K_IO_OP_WRITE:
            if (req->fixed_index >= 0)
                io_uring_prep_write_fixed(sqe, req->fd, req->buf, req->len,
                                          req->offset, req->fixed_index);
            else io_uring_prep_write(sqe, req->fd, req->buf, req->len, (uint64_t)req->offset);
            break;
        case K_IO_OP_RECV:
            io_uring_prep_recv(sqe, req->fd, req->buf, req->len, req->flags);
            break;
        case K_IO_OP_SEND:
            io_uring_prep_send(sqe, req->fd, req->buf, req->len, req->flags);
            break;
        case K_IO_OP_ACCEPT:
            io_uring_prep_accept(sqe, req->fd, req->addr, req->addrlen_ptr, req->flags);
            break;
        case K_IO_OP_CONNECT:
            io_uring_prep_connect(sqe, req->fd, req->addr, req->addrlen);
            break;
        case K_IO_OP_CLOSE:
            io_uring_prep_close(sqe, req->fd);
            break;
        case K_IO_OP_RECVMSG:
            io_uring_prep_recvmsg(sqe, req->fd, req->message, req->flags);
            break;
        case K_IO_OP_SENDMSG:
            io_uring_prep_sendmsg(sqe, req->fd, req->message, req->flags);
            break;
        case K_IO_OP_FSYNC:
            io_uring_prep_fsync(sqe, req->fd, (unsigned)req->flags);
            break;
        case K_IO_OP_OPENAT:
            io_uring_prep_openat(sqe, req->fd, (const char*)req->buf, req->flags, (mode_t)req->offset);
            break;
        case K_IO_OP_STATX:
            io_uring_prep_statx(sqe, req->fd, (const char*)req->buf, req->flags,
                                (unsigned int)req->offset, (struct statx*)req->addr);
            break;
        default:
            io_uring_prep_nop(sqe);
            io_uring_sqe_set_data64(sqe, 0);
            if (sqe_to) {
                io_uring_prep_nop(sqe_to);
                io_uring_sqe_set_data64(sqe_to, 0);
            }
            pthread_mutex_unlock(&b->submit_mutex);
            return -ENOTSUP;
    }

    io_uring_sqe_set_data64(sqe, req->tag);

    /* Linked timeout */
    if (req->timeout_nanos > 0) {
        sqe->flags |= IOSQE_IO_LINK;

        req->timeout_storage->tv_sec = req->timeout_nanos / 1000000000LL;
        req->timeout_storage->tv_nsec = req->timeout_nanos % 1000000000LL;
        io_uring_prep_link_timeout(sqe_to,
            (struct __kernel_timespec*)req->timeout_storage, 0);
        io_uring_sqe_set_data64(sqe_to, req->tag | K_TAG_LINKED_TIMEOUT_FLAG);
    }

    /* Once SQEs have been published, the backend owns them even if enter fails.
     * They remain in the SQ and flush/wait retries; callers must retain memory. */
    io_uring_submit(&b->ring);
    pthread_mutex_unlock(&b->submit_mutex);

    return 0;
}

static int io_uring_backend_submit_cancel(KIoBackend* backend, uint64_t target_tag) {
    KIoUringBackend* b = (KIoUringBackend*)backend;

    pthread_mutex_lock(&b->submit_mutex);
    struct io_uring_sqe* sqe = get_sqe_with_flush(&b->ring);
    if (sqe == NULL) {
        pthread_mutex_unlock(&b->submit_mutex);
        return -EBUSY;
    }

    io_uring_prep_cancel64(sqe, target_tag, 0);
    io_uring_sqe_set_data64(sqe, K_TAG_CANCEL_REQUEST_FLAG);
    int rc = io_uring_submit(&b->ring);
    pthread_mutex_unlock(&b->submit_mutex);

    return (rc >= 0) ? 0 : rc;
}

static int io_uring_backend_flush(KIoBackend* backend) {
    KIoUringBackend* b = (KIoUringBackend*)backend;
    pthread_mutex_lock(&b->submit_mutex);
    int rc = io_uring_submit(&b->ring);
    pthread_mutex_unlock(&b->submit_mutex);
    return (rc >= 0) ? 0 : rc;
}

static int io_uring_backend_wait_cqe(KIoBackend* backend, KIoBackendCqe* out_cqe) {
    KIoUringBackend* b = (KIoUringBackend*)backend;
    struct io_uring_cqe* cqe = NULL;

    for (;;) {
        io_uring_backend_flush(backend);
        int rc = io_uring_wait_cqe(&b->ring, &cqe);
        if (rc < 0) {
            if (rc == -EINTR) {
                continue;
            }
            return rc;
        }
        if (cqe == NULL) {
            return -EIO;
        }
        out_cqe->tag   = io_uring_cqe_get_data64(cqe);
        out_cqe->res   = cqe->res;
        out_cqe->flags = cqe->flags;
        io_uring_cqe_seen(&b->ring, cqe);
        return 0;
    }
}

static unsigned io_uring_backend_peek_batch_cqes(KIoBackend* backend, KIoBackendCqe* out_cqes, unsigned max_cqes) {
    KIoUringBackend* b = (KIoUringBackend*)backend;
    if (max_cqes == 0 || out_cqes == NULL) {
        return 0;
    }

    struct io_uring_cqe* cqes[64];
    unsigned to_peek = max_cqes > 64 ? 64 : max_cqes;
    unsigned count = io_uring_peek_batch_cqe(&b->ring, cqes, to_peek);

    for (unsigned i = 0; i < count; ++i) {
        out_cqes[i].tag   = io_uring_cqe_get_data64(cqes[i]);
        out_cqes[i].res   = cqes[i]->res;
        out_cqes[i].flags = cqes[i]->flags;
        io_uring_cqe_seen(&b->ring, cqes[i]);
    }

    return count;
}

static void io_uring_backend_seen_cqe(KIoBackend* backend, const KIoBackendCqe* cqe) {
    (void)backend;
    (void)cqe;
    /* CQEs are marked seen during wait_cqe / peek_batch_cqes */
}

static int io_uring_backend_wakeup(KIoBackend* backend) {
    KIoUringBackend* b = (KIoUringBackend*)backend;

    pthread_mutex_lock(&b->submit_mutex);
    struct io_uring_sqe* sqe = get_sqe_with_flush(&b->ring);
    if (sqe != NULL) {
        io_uring_prep_nop(sqe);
        io_uring_sqe_set_data64(sqe, K_TAG_STOP);
        io_uring_submit(&b->ring);
    }
    pthread_mutex_unlock(&b->submit_mutex);
    return sqe ? 0 : -EBUSY;
}

static int register_buffer(KIoBackend* backend, void* data, size_t length) {
    KIoUringBackend* b = (KIoUringBackend*)backend;
    if (!b->sparse_available) return -ENOTSUP;
    pthread_mutex_lock(&b->submit_mutex);
    int index;
    for (index = 0; index < 256 && b->buffer_slots[index]; ++index) {}
    if (index == 256) {
        pthread_mutex_unlock(&b->submit_mutex);
        return -EBUSY;
    }
    struct iovec vec = { data, length };
    int rc = io_uring_register_buffers_update_tag(&b->ring, index, &vec, NULL, 1);
    if (rc == 1) b->buffer_slots[index] = true;
    pthread_mutex_unlock(&b->submit_mutex);
    return rc == 1 ? index : (rc < 0 ? rc : -EIO);
}
static int unregister_buffer(KIoBackend* backend, int index) {
    KIoUringBackend* b = (KIoUringBackend*)backend;
    if (index < 0 || index >= 256) return -EINVAL;
    pthread_mutex_lock(&b->submit_mutex);
    struct iovec empty = { NULL, 0 };
    int rc = io_uring_register_buffers_update_tag(&b->ring, index, &empty, NULL, 1);
    if (rc == 1) b->buffer_slots[index] = false;
    pthread_mutex_unlock(&b->submit_mutex);
    return rc == 1 ? 0 : (rc < 0 ? rc : -EIO);
}
static const KIoBackendOps g_uring_backend_ops = {
    .destroy         = io_uring_backend_destroy,
    .probe_opcode    = io_uring_backend_probe_opcode,
    .submit_request  = io_uring_backend_submit_request,
    .submit_cancel   = io_uring_backend_submit_cancel,
    .flush           = io_uring_backend_flush,
    .wait_cqe        = io_uring_backend_wait_cqe,
    .peek_batch_cqes = io_uring_backend_peek_batch_cqes,
    .seen_cqe        = io_uring_backend_seen_cqe,
    .wakeup          = io_uring_backend_wakeup,
    .register_buffer = register_buffer,
    .unregister_buffer = unregister_buffer
};

KIoBackend* k_io_uring_backend_create(uint32_t entries) {
    if (entries == 0) {
        entries = 256;
    }

    KIoUringBackend* b = (KIoUringBackend*)calloc(1, sizeof(KIoUringBackend));
    if (b == NULL) {
        errno = ENOMEM;
        return NULL;
    }

    b->base.ops = &g_uring_backend_ops;
    b->entries  = entries;

    int init_rc = io_uring_queue_init(entries, &b->ring, 0);
    if (init_rc < 0) {
        errno = -init_rc;
        free(b);
        return NULL;
    }

    pthread_mutex_init(&b->submit_mutex, NULL);

    b->probe = io_uring_get_probe_ring(&b->ring);
    if (b->probe == NULL) {
        /* Probe unavailable; clean up */
        io_uring_queue_exit(&b->ring);
        pthread_mutex_destroy(&b->submit_mutex);
        free(b);
        errno = ENOTSUP;
        return NULL;
    }

    /* Probe required data plane opcodes */
    static const uint8_t required_ops[] = {
        IORING_OP_READ,
        IORING_OP_WRITE,
        IORING_OP_RECV,
        IORING_OP_SEND,
        IORING_OP_RECVMSG,
        IORING_OP_SENDMSG,
        IORING_OP_ACCEPT,
        IORING_OP_CONNECT,
        IORING_OP_ASYNC_CANCEL,
        IORING_OP_LINK_TIMEOUT,
        IORING_OP_CLOSE
    };

    for (size_t i = 0; i < sizeof(required_ops) / sizeof(required_ops[0]); ++i) {
        if (!uring_is_opcode_supported(b->probe, required_ops[i])) {
            io_uring_free_probe(b->probe);
            io_uring_queue_exit(&b->ring);
            pthread_mutex_destroy(&b->submit_mutex);
            free(b);
            errno = ENOTSUP;
            return NULL; /* Required opcode unsupported by kernel */
        }
    }

    b->sparse_available = uring_is_opcode_supported(b->probe, IORING_OP_READ_FIXED) &&
        uring_is_opcode_supported(b->probe, IORING_OP_WRITE_FIXED) &&
        io_uring_register_buffers_sparse(&b->ring, 256) == 0;
    return &b->base;
}
