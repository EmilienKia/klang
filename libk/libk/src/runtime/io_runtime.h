/*
 * K Language runtime — Asynchronous I/O runtime (singleton & management)
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

#ifndef KLANG_RUNTIME_IO_RUNTIME_H
#define KLANG_RUNTIME_IO_RUNTIME_H

#include "io_backend.h"
#include "io_operation.h"

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Forward Declarations ───────────────────────────────────────────────── */

typedef struct KIoRuntime   KIoRuntime;
typedef struct KIoHandle    KIoHandle;

/* ── Runtime Metrics ────────────────────────────────────────────────────── */

typedef struct KIoMetrics {
    int64_t inflight_ops;
    int64_t callbacks_queued;
    int64_t callbacks_rejected;
    int64_t registered_buffers;
} KIoMetrics;

/* ── Runtime Lifecycle ──────────────────────────────────────────────────── */

/**
 * Get or initialize the process-wide I/O runtime.
 * Returns NULL on initialization failure.
 */
KIoRuntime* k_io_runtime_get(void);

/**
 * Initialize the runtime explicitly. Returns 0 on success, negative errno on failure.
 */
int k_io_runtime_init(void);

/**
 * Shut down the runtime, canceling in-flight operations, draining CQEs,
 * stopping threads, and releasing all resources.
 */
void k_io_runtime_shutdown(void);
int k_io_runtime_request_shutdown(void);
/* Application exit is a no-op for an unused runtime, unlike explicit shutdown. */
int k_io_runtime_shutdown_if_started(void);

/**
 * Return true if the runtime is currently running and ready to accept operations.
 */
bool k_io_runtime_is_active(void);
bool k_io_runtime_is_reaper(void);
/* Internal marker also covers runtime-owned worker TLS finalizers. */
void k_io_runtime_enter_worker(void);

/**
 * Submit an operation to the runtime.
 * Acquires a registry slot and submits to the backend.
 * @param timeout_nanos <= 0 for no timeout; > 0 for linked timeout.
 * @return 0 on success, negative error code (e.g. -EBUSY, -ESHUTDOWN).
 */
int k_io_runtime_submit(KIoRuntime* rt, KNativeIoOperation* op, int64_t timeout_nanos);

/**
 * Cancel an operation in the runtime by user_data tag.
 */
int k_io_runtime_cancel(KIoRuntime* rt, uint64_t tag);

/**
 * Submit a DNS / control-plane resolution job through the runtime's resolver pool.
 */
int k_io_runtime_submit_resolve(KIoRuntime* rt, KNativeIoOperation* op,
                                const char* node, const char* service,
                                const void* hints);
/* Continue the same accepted connect after resolver storage has been drained. */
void k_io_runtime_connect_resolved(KNativeIoOperation* op, int result);

/**
 * Query current runtime metrics.
 */
void k_io_runtime_get_metrics(KIoMetrics* out_metrics);

/* Metrics increment helpers */
bool k_io_runtime_metric_callback_queued(void);
void k_io_runtime_metric_callback_rejected(void);
void k_io_runtime_metric_callback_finished(void);
void k_io_runtime_metric_buffer_registered(void);
void k_io_runtime_metric_buffer_unregistered(void);

/* ── Handle Lifecycle & Management ──────────────────────────────────────── */

#define K_HANDLE_OPEN    0u
#define K_HANDLE_CLOSING 1u
#define K_HANDLE_CLOSED  2u

struct KIoHandle {
    int                 fd;
    pid_t               creator_pid;
    _Atomic uint32_t    state;
    _Atomic uint32_t    refs;
    pthread_mutex_t     mutex;
    pthread_cond_t      drained;

    /* Inflight list */
    KNativeIoOperation* inflight_head;
    uint32_t            inflight_count;
    KNativeIoOperation* close_operation; /* strong ref while waiting for target CQEs */
    bool                owns_fd;
    int64_t             file_position;
    KNativeIoOperation* position_head;
    KNativeIoOperation* position_tail;
    uint32_t            position_count;

    /* Socket directional constraints (one inflight receive and send/connect). */
    bool                is_stream_socket;
    bool                is_datagram_socket;
    void*               remote_address; /* shared immutable GNativeAddress */
    char*               owned_socket_path;
    dev_t               socket_path_device;
    ino_t               socket_path_inode;
    _Atomic uint32_t    read_inflight;
    _Atomic uint32_t    write_inflight;
};

/**
 * Create a new handle wrapping an OS file descriptor.
 */
KIoHandle* k_io_handle_create(int fd, bool is_stream);

void k_io_handle_retain(KIoHandle* h);
void k_io_handle_release(KIoHandle* h);
int k_io_handle_cleanup_path(KIoHandle* h);

int  k_io_handle_fd(const KIoHandle* h);
bool k_io_handle_is_open(const KIoHandle* h);
int64_t k_io_handle_position(const KIoHandle* h);
int k_io_handle_set_position(KIoHandle* h, int64_t position);

/**
 * Asynchronously close handle: marks CLOSING, cancels all in-flight ops,
 * drains them, and closes the fd. Returns an IoOperation observing the close.
 */
KNativeIoOperation* k_io_handle_close(KIoHandle* h, const KOwnedCallback* cb,
                                      KExecutorLease* lease, KFutureState* fut);

/**
 * Synchronously close handle and block until drained.
 */
int k_io_handle_close_sync(KIoHandle* h);

/**
 * Unlink an operation from the handle's inflight list (called on quiescence).
 */
/* Returns a retained promotion (next implicit request or drained close).
 * The completion path must promote it only AFTER releasing operation locks. */
KNativeIoOperation* k_io_handle_unlink_op(KIoHandle* h, KNativeIoOperation* op);
void k_io_runtime_promote(KNativeIoOperation* op);

/* ── System Default Executor ────────────────────────────────────────────── */

/**
 * Return a lease to the process-wide bounded system executor pool.
 */
KExecutorLease* k_executor_system_lease(void);
int k_io_runtime_dispatch(void (*run)(void*), void (*drop)(void*), void* arg);
int k_io_runtime_register_buffer(void* data, size_t length);
int k_io_runtime_unregister_buffer(int index);
/* A test backend is owned by the runtime after a successful installation. */
int k_io_runtime_install_test_backend(KIoBackend* backend);
/* lookup has getaddrinfo's ABI; valid only with an injected backend. */
int k_io_runtime_set_test_resolver_lookup(void* lookup);
int k_io_runtime_track_delivery(KNativeIoOperation* op);
void k_io_runtime_untrack_delivery(KNativeIoOperation* op);

#ifdef __cplusplus
}
#endif

#endif /* KLANG_RUNTIME_IO_RUNTIME_H */
