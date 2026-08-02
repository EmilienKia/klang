/*
 * K Language runtime — asynchronous I/O substrate (public C API)
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

#ifndef KLANG_ASYNC_IO_H
#define KLANG_ASYNC_IO_H

#include <stdint.h>

#ifndef K_ASYNC_HAVE_URING
#define K_ASYNC_HAVE_URING 0
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct KAsyncHandle KAsyncHandle;
typedef struct KAsyncOp     KAsyncOp;

/** Operation outcome codes, mirrored by ::k::io on the K side. */
#define K_ASYNC_OK           0
#define K_ASYNC_INTERRUPTED  1
#define K_ASYNC_TIMEOUT      2
#define K_ASYNC_CLOSED       3
#define K_ASYNC_ERROR        4

/** Return 1 when the io_uring backend is active, 0 for the POSIX fallback. */
int k_async_is_uring(void);

/**
 * Open `path` and wrap the resulting descriptor into an asynchronous handle.
 * Returns NULL on failure and stores errno in *out_errno when non-NULL.
 */
KAsyncHandle* k_async_open(const char* path, int flags, int mode, int* out_errno);

/** Wrap an already-open descriptor.  The handle takes ownership of `fd`. */
KAsyncHandle* k_async_wrap_fd(int fd);

/** Return the wrapped descriptor, or -1. */
int k_async_handle_fd(const KAsyncHandle* h);

/** Return 1 while the handle is open, 0 once closing or closed. */
int k_async_is_open(const KAsyncHandle* h);

/** Increment the handle reference count. */
void k_async_retain(KAsyncHandle* h);

/**
 * Cancel every in-flight operation, drain them, then close the descriptor.
 * Idempotent.  Returns 0 on success, errno otherwise.
 */
int k_async_close(KAsyncHandle* h);

/** Drop one reference; closes and frees the handle when the last one goes. */
void k_async_release(KAsyncHandle* h);

/**
 * Read up to `len` bytes into `buf`.
 *
 * `offset` < 0 uses the descriptor's own file position; a non-negative value
 * performs a positional read that leaves the file position untouched.
 * `timeout_nanos` < 0 waits indefinitely.
 *
 * Stores the outcome code in *status and returns the number of bytes read
 * (K_ASYNC_OK) or the errno value (K_ASYNC_ERROR).  A zero-byte read with
 * K_ASYNC_OK means end of stream.
 */
int64_t k_async_read(KAsyncHandle* h, void* buf, uint32_t len, int64_t offset,
                     int64_t timeout_nanos, int* status);

/** Write up to `len` bytes from `buf`; see k_async_read for the conventions. */
int64_t k_async_write(KAsyncHandle* h, const void* buf, uint32_t len,
                      int64_t offset, int64_t timeout_nanos, int* status);

/** Flush the file to stable storage.  Returns 0 or the errno value. */
int k_async_fsync(KAsyncHandle* h, int64_t timeout_nanos, int* status);

/** Return the current file size in bytes, or -1 on failure. */
int64_t k_async_size(const KAsyncHandle* h);

/** Truncate or extend the file to `size` bytes.  Returns 0 or errno. */
int k_async_truncate(KAsyncHandle* h, int64_t size);

#ifdef __cplusplus
}
#endif

#endif /* KLANG_ASYNC_IO_H */
