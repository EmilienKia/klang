/*
 * K Language runtime — Asynchronous I/O operation and execution context
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

#ifndef KLANG_RUNTIME_IO_OPERATION_H
#define KLANG_RUNTIME_IO_OPERATION_H

#include "callable_abi.h"
#include "io_backend.h"
#include "future_state.h"

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Forward Declarations ───────────────────────────────────────────────── */

typedef struct KIoHandle           KIoHandle;
typedef struct KExecutorLease      KExecutorLease;
typedef struct KNativeIoOperation  KNativeIoOperation;

/* ── 2D State Machine: I/O Phase ────────────────────────────────────────── */

typedef enum {
    K_IO_PHASE_SUBMITTED        = 0,
    K_IO_PHASE_CANCEL_REQUESTED = 1,
    K_IO_PHASE_KERNEL_COMPLETED = 2,
    K_IO_PHASE_KERNEL_CANCELLED = 3,
    K_IO_PHASE_QUIESCENT        = 4
} KIoPhase;

/* ── 2D State Machine: Callback Phase ───────────────────────────────────── */

typedef enum {
    K_CB_PHASE_NONE        = 0,
    K_CB_PHASE_QUEUED      = 1,
    K_CB_PHASE_RUNNING     = 2,
    K_CB_PHASE_SUPPRESSED  = 3,
    K_CB_PHASE_COMPLETED   = 4,
    K_CB_PHASE_INTERRUPTED = 5,
    K_CB_PHASE_REJECTED    = 6
} KCallbackPhase;

/* ── Public Observable Stage (Derived) ──────────────────────────────────── */

typedef enum {
    K_IO_STAGE_PENDING                = 0,
    K_IO_STAGE_CANCELLING             = 1,
    K_IO_STAGE_CALLBACK_QUEUED        = 2,
    K_IO_STAGE_CALLBACK_RUNNING       = 3,
    K_IO_STAGE_COMPLETED              = 4,
    K_IO_STAGE_CANCELLED              = 5,
    K_IO_STAGE_INTERRUPTION_REQUESTED = 6,
    K_IO_STAGE_INTERRUPTED            = 7,
    K_IO_STAGE_CALLBACK_REJECTED      = 8
} KIoOperationStage;

/* ── Error Classification ───────────────────────────────────────────────── */

typedef enum {
    K_IO_ERR_SYSTEM             = 0,
    K_IO_ERR_CANCELLED          = 1,
    K_IO_ERR_CLOSED             = 2,
    K_IO_ERR_TIMEOUT            = 3,
    K_IO_ERR_END_OF_STREAM      = 4,
    K_IO_ERR_UNSUPPORTED        = 5,
    K_IO_ERR_RESOURCE_EXHAUSTED = 6,
    K_IO_ERR_EXECUTOR_REJECTED  = 7,
    K_IO_ERR_INVALID_ARGUMENT   = 8,
    K_IO_ERR_INTERRUPTED        = 9
} KIoErrorKind;

/* ── Result Struct ──────────────────────────────────────────────────────── */

typedef struct KIoResult {
    KIoErrorKind kind;
    int          err_no;
    int64_t      bytes_transferred;
    int64_t      value;             /* bytes or produced fd */
    int          resource_claimed; /* publisher/callback accepted ownership */
} KIoResult;

/* ── Executor Lease Interface ───────────────────────────────────────────── */

typedef struct KExecutorLeaseOps {
    void (*retain)(KExecutorLease* lease);
    void (*release)(KExecutorLease* lease);
    int  (*submit)(KExecutorLease* lease, void (*run)(void*), void (*drop)(void*), void* arg);
} KExecutorLeaseOps;

struct KExecutorLease {
    const KExecutorLeaseOps* ops;
    void*                    ctx;
    _Atomic uint32_t         refs;
};

void k_executor_lease_retain(KExecutorLease* lease);
void k_executor_lease_release(KExecutorLease* lease);
int  k_executor_lease_submit(KExecutorLease* lease, void (*run)(void*), void (*drop)(void*), void* arg);

/* ── Native IoOperation Structure ───────────────────────────────────────── */

struct KNativeIoOperation {
    _Atomic uint32_t        ref_count;
    _Atomic bool            submitted;
    pid_t                   creator_pid;

    /* 2D State Machine */
    _Atomic uint32_t        io_phase;               /* KIoPhase */
    _Atomic uint32_t        cb_phase;               /* KCallbackPhase */
    _Atomic uint32_t        interruption_requested; /* Cooperative flag */

    /* Kernel Immutable Results */
    int32_t                 res;                    /* Raw CQE / fallback res */
    int32_t                 err_no;                 /* 0 or positive errno */
    KIoErrorKind            result_kind;
    int64_t                 bytes_transferred;      /* Transferred bytes */
    int                     produced_fd;            /* -1 or new fd */

    /* Request Description */
    uint64_t                tag;
    KIoOpcode               opcode;
    int                     fd;
    int64_t                 offset;
    void*                   buf;
    uint32_t                len;
    int                     flags;

    /* Attached Resources */
    void*                   buffer_obj;             /* NativeByteBuffer* */
    void*                   sockaddr_storage;       /* Heap sockaddr if any */
    uint32_t                sockaddr_len;
    void*                   msghdr;

    /* Callback Transport */
    bool                    has_callback;
    KOwnedCallback          callback;
    _Atomic uint32_t        callback_dropped;       /* 1 if drop called */
    bool                    callback_drop_complete; /* guarded by lock */
    _Atomic uint32_t        callback_dispatched;
    bool                    callback_counted;
    KOwnedCallback          continuation;           /* Internal payload publisher */
    bool                    result_published;       /* guarded by lock */
    bool                    publication_in_progress;
    bool                    resource_claimed;
    bool                    handle_linked;
    bool                    owns_request_buffer;
    int                     request_error;
    int64_t                 deadline_nanos;
    struct timespec         timeout_storage;
    bool                    implicit_position;
    bool                    implicit_queued;
    KNativeIoOperation*     position_next;
    KNativeIoOperation*     promotion_next;
    bool                    fully;
    bool                    fully_eof;
    bool                    owns_direction;
    bool                    resolving;
    bool                    connect_owned;
    bool                    delivery_linked;
    KNativeIoOperation*     delivery_next;
    KNativeIoOperation*     delivery_prev;
    char*                   resolver_node;
    char*                   resolver_service;
    KNativeIoOperation*     fully_child;             /* owned, guarded by lock */
    KNativeIoOperation*     fully_parent;            /* retained until child drain */

    /* Executor Lease */
    KExecutorLease*         lease;

    /* Associated Handle */
    KIoHandle*              handle;

    /* Quiescence & Wait Synchronization */
    pthread_mutex_t         lock;
    pthread_cond_t          cond;

    /* Intrusive Handle Linking */
    KNativeIoOperation*     prev;
    KNativeIoOperation*     next;
};

/* ── Lifecycle & Operations ─────────────────────────────────────────────── */

/* fut is reserved and must be NULL. A Future requires both an explicit payload
 * continuation and k_io_operation_bind_future, installed before submission.
 * A successful fd-producing callback must set KIoResult.resource_claimed;
 * otherwise its unadopted descriptor is rolled back when invoke returns. */
KNativeIoOperation* k_io_operation_create(KIoOpcode opcode, int fd, void* buf,
                                          uint32_t len, int64_t offset, int flags,
                                          void* buffer_obj, const KOwnedCallback* cb,
                                          KExecutorLease* lease, KFutureState* fut);

void k_io_operation_retain(KNativeIoOperation* op);
void k_io_operation_release(KNativeIoOperation* op);
/* Must be installed before submission; consumes a carrier, never a K ABI closure. */
int k_io_operation_set_continuation(KNativeIoOperation* op, const KOwnedCallback* continuation);
void k_io_operation_bind_future(KNativeIoOperation* op, KFutureState* future);

/**
 * Request cancellation of the operation.
 * Conforms to the 2D cancellation contract:
 * - If terminal: returns false.
 * - If I/O submitted: marks CancelRequested and calls backend cancel.
 * - If CQE received but callback not running: marks Suppressed, closes produced_fd,
 *   arranges callback cleanup off the reaper, never runs callback.
 * - If callback is currently running: sets cooperative interruption flag on ExecutionContext.
 */
bool k_io_operation_cancel(KNativeIoOperation* op);

/**
 * Derive the public observable stage from the 2D state machine.
 */
KIoOperationStage k_io_operation_derive_stage(const KNativeIoOperation* op);

bool k_io_operation_is_io_done(const KNativeIoOperation* op);
bool k_io_operation_is_callback_running(const KNativeIoOperation* op);
bool k_io_operation_is_done(const KNativeIoOperation* op);
bool k_io_operation_is_cancelled(const KNativeIoOperation* op);
bool k_io_operation_is_interruption_requested(const KNativeIoOperation* op);
int64_t k_io_operation_bytes_transferred(const KNativeIoOperation* op);
void k_io_operation_error(const KNativeIoOperation* op, KIoResult* out_result);

/**
 * Block until the operation reaches Quiescent state.
 * @param timeout_nanos Negative for infinite wait.
 * @return 0 on success, ETIMEDOUT on timeout.
 */
int k_io_operation_wait_quiescent(KNativeIoOperation* op, int64_t timeout_nanos);

/**
 * Handle target completion (called from reaper or bounded control workers).
 */
void k_io_operation_on_kernel_completion(KNativeIoOperation* op, int32_t res);
/* Internal file composition; never dispatches a child on a user executor. */
void k_io_operation_start_fully(KNativeIoOperation* op);
/* Retained child observer for native lifecycle diagnostics/tests. */
KNativeIoOperation* k_io_operation_retain_fully_child(KNativeIoOperation* op);
KIoHandle* k_io_operation_retain_handle(KNativeIoOperation* op);

/**
 * Callback trampolines for executor task execution.
 */
void k_io_operation_run_callback_trampoline(void* arg);
void k_io_operation_drop_callback_trampoline(void* arg);
/* Suppress queued delivery; await running callback and completed carrier/lease drop. */
void k_io_operation_abandon_delivery(KNativeIoOperation* op);
/* CLOSE I/O is non-cancellable; its user delivery still follows shutdown rules. */
void k_io_operation_cancel_for_shutdown(KNativeIoOperation* op);
bool k_io_operation_is_callback_cleanup(void);

/**
 * Map errno to KIoErrorKind.
 */
KIoErrorKind k_io_errno_to_error_kind(int err);

/* ── ExecutionContext TLS ───────────────────────────────────────────────── */

/**
 * Return current executing operation on the calling thread, or NULL.
 */
KNativeIoOperation* k_execution_context_current(void);

/**
 * Set current executing operation for calling thread (internal use).
 */
void k_execution_context_set_current(KNativeIoOperation* op);

/**
 * Check if cooperative cancellation was requested for the current callback.
 */
bool k_execution_context_is_cancellation_requested(void);

#ifdef __cplusplus
}
#endif

#endif /* KLANG_RUNTIME_IO_OPERATION_H */
