/*
 * K Language runtime — asynchronous I/O FFI wrappers
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
 * Thin C entry points consumed by libk/libk/src/io/*.k.  Every function takes
 * and returns plain scalars or opaque pointers so that the K side never needs
 * to know the layout of the underlying C structures.
 *
 * Operations that can report several outcomes take an `int* status` output
 * parameter carrying one of the K_ASYNC_* codes.
 */

#define _GNU_SOURCE
#include "async_io.h"

#include <fcntl.h>
#include <stddef.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>


/* ── UTF-32 (K `char`) → UTF-8 path transcoding ─────────────────────────── */

/* K represents `char` as a 32-bit code point, so every `const char*` coming
 * from the K side is a null-terminated UTF-32 string that must be transcoded
 * before it can be handed to the platform. */

#define K_PATH_BUF 8192

static void k_utf32_to_utf8(const uint32_t* src, char* dst, size_t dst_cap) {
    size_t out = 0;
    if (!src) { if (dst_cap) dst[0] = '\0'; return; }
    for (size_t i = 0; src[i] != 0 && out + 4 < dst_cap; ++i) {
        uint32_t cp = src[i];
        if (cp < 0x80) {
            dst[out++] = (char)cp;
        } else if (cp < 0x800) {
            dst[out++] = (char)(0xC0 | (cp >> 6));
            dst[out++] = (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            dst[out++] = (char)(0xE0 | (cp >> 12));
            dst[out++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            dst[out++] = (char)(0x80 | (cp & 0x3F));
        } else {
            dst[out++] = (char)(0xF0 | (cp >> 18));
            dst[out++] = (char)(0x80 | ((cp >> 12) & 0x3F));
            dst[out++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            dst[out++] = (char)(0x80 | (cp & 0x3F));
        }
    }
    dst[out] = '\0';
}

/* ── Open flag encoding (portable, K-side constants) ────────────────────── */

#define K_OPEN_READ      0x01
#define K_OPEN_WRITE     0x02
#define K_OPEN_APPEND    0x04
#define K_OPEN_CREATE    0x08
#define K_OPEN_TRUNCATE  0x10
#define K_OPEN_EXCLUSIVE 0x20

static int decode_open_flags(int k_flags) {
    int flags = 0;
    const int rd = (k_flags & K_OPEN_READ)  != 0;
    const int wr = (k_flags & K_OPEN_WRITE) != 0;
    if (rd && wr) {
        flags = O_RDWR;
    } else if (wr) {
        flags = O_WRONLY;
    } else {
        flags = O_RDONLY;
    }
    if (k_flags & K_OPEN_APPEND) {
        flags |= O_APPEND;
    }
    if (k_flags & K_OPEN_CREATE) {
        flags |= O_CREAT;
    }
    if (k_flags & K_OPEN_TRUNCATE) {
        flags |= O_TRUNC;
    }
    if (k_flags & K_OPEN_EXCLUSIVE) {
        flags |= O_EXCL;
    }
    return flags;
}

/* ── Result encoding ────────────────────────────────────────────────────
 *
 * K has no convenient out-parameter idiom, so every operation encodes its
 * outcome in a single signed value:
 *
 *    >= 0            success — number of bytes transferred (0 = end of stream)
 *    K_RES_INTERRUPTED (-1)  the calling thread was interrupted
 *    K_RES_TIMEOUT     (-2)  the deadline elapsed
 *    K_RES_CLOSED      (-3)  the channel was closed, possibly by another thread
 *    -(1000 + errno)         platform failure
 */

#define K_RES_INTERRUPTED (-1LL)
#define K_RES_TIMEOUT     (-2LL)
#define K_RES_CLOSED      (-3LL)
#define K_RES_ERROR_BASE  (-1000LL)

static long long encode_result(int status, long long value) {
    switch (status) {
        case K_ASYNC_OK:          return value;
        case K_ASYNC_INTERRUPTED: return K_RES_INTERRUPTED;
        case K_ASYNC_TIMEOUT:     return K_RES_TIMEOUT;
        case K_ASYNC_CLOSED:      return K_RES_CLOSED;
        default:                  return K_RES_ERROR_BASE - value;
    }
}

/* Last errno reported by __k_async_open on this thread. */
static _Thread_local int g_last_open_errno = 0;

/* ── Runtime ────────────────────────────────────────────────────────────── */

int __k_async_is_uring(void) {
    return k_async_is_uring();
}

/* ── Handle ─────────────────────────────────────────────────────────────── */

void* __k_async_open(const uint32_t* path, int k_flags, int mode) {
    char pbuf[K_PATH_BUF];
    k_utf32_to_utf8(path, pbuf, sizeof(pbuf));
    int err = 0;
    void* h = (void*)k_async_open(pbuf, decode_open_flags(k_flags), mode, &err);
    g_last_open_errno = err;
    return h;
}

int __k_async_last_errno(void) {
    return g_last_open_errno;
}

void __k_async_retain(void* h) {
    k_async_retain((KAsyncHandle*)h);
}

void __k_async_release(void* h) {
    k_async_release((KAsyncHandle*)h);
}

int __k_async_close(void* h) {
    return k_async_close((KAsyncHandle*)h);
}

int __k_async_is_open(const void* h) {
    return k_async_is_open((const KAsyncHandle*)h);
}

int __k_async_fd(const void* h) {
    return k_async_handle_fd((const KAsyncHandle*)h);
}

/* ── Operations ─────────────────────────────────────────────────────────── */

long long __k_async_read_bytes(void* h, void* buf, int len, long long offset,
                               long long timeout_nanos) {
    int status = K_ASYNC_ERROR;
    int64_t r = k_async_read((KAsyncHandle*)h, buf, (uint32_t)len,
                             (int64_t)offset, (int64_t)timeout_nanos, &status);
    return encode_result(status, (long long)r);
}

long long __k_async_write_bytes(void* h, const void* buf, int len,
                                long long offset, long long timeout_nanos) {
    int status = K_ASYNC_ERROR;
    int64_t r = k_async_write((KAsyncHandle*)h, buf, (uint32_t)len,
                              (int64_t)offset, (int64_t)timeout_nanos, &status);
    return encode_result(status, (long long)r);
}

long long __k_async_fsync(void* h, long long timeout_nanos) {
    int status = K_ASYNC_ERROR;
    int r = k_async_fsync((KAsyncHandle*)h, (int64_t)timeout_nanos, &status);
    return encode_result(status, (long long)r);
}

long long __k_async_size(const void* h) {
    return (long long)k_async_size((const KAsyncHandle*)h);
}

int __k_async_truncate(void* h, long long size) {
    return k_async_truncate((KAsyncHandle*)h, (int64_t)size);
}

/* ── Path helpers ───────────────────────────────────────────────────────── */

int __k_async_path_exists(const uint32_t* path) {
    char pbuf[K_PATH_BUF];
    k_utf32_to_utf8(path, pbuf, sizeof(pbuf));
    struct stat st;
    return stat(pbuf, &st) == 0 ? 1 : 0;
}

int __k_async_path_is_file(const uint32_t* path) {
    char pbuf[K_PATH_BUF];
    k_utf32_to_utf8(path, pbuf, sizeof(pbuf));
    struct stat st;
    return (stat(pbuf, &st) == 0 && S_ISREG(st.st_mode)) ? 1 : 0;
}

int __k_async_path_is_directory(const uint32_t* path) {
    char pbuf[K_PATH_BUF];
    k_utf32_to_utf8(path, pbuf, sizeof(pbuf));
    struct stat st;
    return (stat(pbuf, &st) == 0 && S_ISDIR(st.st_mode)) ? 1 : 0;
}

long long __k_async_path_size(const uint32_t* path) {
    char pbuf[K_PATH_BUF];
    k_utf32_to_utf8(path, pbuf, sizeof(pbuf));
    struct stat st;
    return stat(pbuf, &st) == 0 ? (long long)st.st_size : -1LL;
}

int __k_async_path_delete(const uint32_t* path) {
    char pbuf[K_PATH_BUF];
    k_utf32_to_utf8(path, pbuf, sizeof(pbuf));
    return unlink(pbuf) == 0 ? 1 : 0;
}

int __k_async_path_mkdir(const uint32_t* path) {
    char pbuf[K_PATH_BUF];
    k_utf32_to_utf8(path, pbuf, sizeof(pbuf));
    return mkdir(pbuf, 0777) == 0 ? 1 : 0;
}
