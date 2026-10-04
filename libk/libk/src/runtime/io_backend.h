/*
 * K Language runtime — Asynchronous I/O backend interface
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

#ifndef KLANG_RUNTIME_IO_BACKEND_H
#define KLANG_RUNTIME_IO_BACKEND_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Operation Opcodes ──────────────────────────────────────────────────── */

typedef enum {
    K_IO_OP_NONE         = 0,
    K_IO_OP_READ         = 1,
    K_IO_OP_WRITE        = 2,
    K_IO_OP_RECV         = 3,
    K_IO_OP_SEND         = 4,
    K_IO_OP_ACCEPT       = 5,
    K_IO_OP_CONNECT      = 6,
    K_IO_OP_CLOSE        = 7,
    K_IO_OP_ASYNC_CANCEL = 8,
    K_IO_OP_LINK_TIMEOUT = 9,
    K_IO_OP_OPENAT       = 10,
    K_IO_OP_STATX        = 11,
    K_IO_OP_FSYNC        = 12,
    K_IO_OP_FTRUNCATE    = 13,
    K_IO_OP_RECVMSG      = 14,
    K_IO_OP_SENDMSG      = 15
} KIoOpcode;

/* ── Special user_data tags ─────────────────────────────────────────────── */

#define K_TAG_CANCEL_REQUEST_FLAG (1ULL << 62)
#define K_TAG_LINKED_TIMEOUT_FLAG (1ULL << 63)
#define K_TAG_SPECIAL_FLAGS       (K_TAG_CANCEL_REQUEST_FLAG | K_TAG_LINKED_TIMEOUT_FLAG)
#define K_TAG_STOP                0xFFFFFFFFFFFFFFFFULL

/* ── Submission request ─────────────────────────────────────────────────── */

typedef struct KIoBackendRequest {
    KIoOpcode        opcode;
    int              fd;
    void*            buf;
    uint32_t         len;
    int64_t          offset;
    int              flags;
    uint64_t         tag;
    int64_t          timeout_nanos;   /* <= 0 for none; > 0 for linked timeout */
    struct sockaddr* addr;            /* for connect / accept / recvfrom / sendto */
    socklen_t        addrlen;         /* for connect / sendto */
    socklen_t*       addrlen_ptr;     /* for accept / recvfrom */
    struct timespec* timeout_storage; /* owned by the operation, not stack */
    struct msghdr*   message;
    int             fixed_index;     /* -1 for ordinary read/write */
} KIoBackendRequest;

/* ── Completion record ──────────────────────────────────────────────────── */

typedef struct KIoBackendCqe {
    uint64_t tag;
    int32_t  res;
    uint32_t flags;
} KIoBackendCqe;

/* ── Backend interface (vtable) ─────────────────────────────────────────── */

typedef struct KIoBackend KIoBackend;

typedef struct KIoBackendOps {
    void     (*destroy)(KIoBackend* backend);
    bool     (*probe_opcode)(KIoBackend* backend, KIoOpcode opcode);
    int      (*submit_request)(KIoBackend* backend, const KIoBackendRequest* req);
    int      (*submit_cancel)(KIoBackend* backend, uint64_t target_tag);
    int      (*flush)(KIoBackend* backend);
    int      (*wait_cqe)(KIoBackend* backend, KIoBackendCqe* out_cqe);
    unsigned (*peek_batch_cqes)(KIoBackend* backend, KIoBackendCqe* out_cqes, unsigned max_cqes);
    void     (*seen_cqe)(KIoBackend* backend, const KIoBackendCqe* cqe);
    int      (*wakeup)(KIoBackend* backend);
    int      (*register_buffer)(KIoBackend* backend, void* data, size_t length);
    int      (*unregister_buffer)(KIoBackend* backend, int index);
} KIoBackendOps;

struct KIoBackend {
    const KIoBackendOps* ops;
};

/* ── Backend constructors ───────────────────────────────────────────────── */

/**
 * Instantiate Linux io_uring backend with the given number of ring entries.
 * Returns NULL on initialization failure or unsupported kernel.
 */
KIoBackend* k_io_uring_backend_create(uint32_t entries);

#ifdef __cplusplus
}
#endif

#endif /* KLANG_RUNTIME_IO_BACKEND_H */
