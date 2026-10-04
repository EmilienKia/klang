/*
 * K Language runtime — Asynchronous I/O FFI bridge
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
#include "io_runtime.h"
#include "io_operation.h"
#include "io_resolver.h"
#include "io_file_ffi.h"
#include "io_stream_ffi.h"
#include "io_datagram_ffi.h"
#include "park_lot.h"

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stddef.h>
#include <stdio.h>

#define K_PATH_BUF 4096

/* ── UTF-32 (K `char`) → UTF-8 Transcoding ──────────────────────────────── */

static int k_utf32_to_utf8(const uint32_t* src, char* dst, size_t dst_cap) {
    size_t out = 0;
    if (!src) {
        if (dst_cap) dst[0] = '\0';
        return -EINVAL;
    }
    for (size_t i = 0; src[i] != 0; ++i) {
        uint32_t cp = src[i];
        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return -EILSEQ;
        size_t needed = cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
        if (needed >= dst_cap - out) return -ENAMETOOLONG;
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
    return 0;
}

/* ── Open Flag Decoding ─────────────────────────────────────────────────── */

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
    if (k_flags & K_OPEN_APPEND)    flags |= O_APPEND;
    if (k_flags & K_OPEN_CREATE)    flags |= O_CREAT;
    if (k_flags & K_OPEN_TRUNCATE)  flags |= O_TRUNC;
    if (k_flags & K_OPEN_EXCLUSIVE) flags |= O_EXCL;
    flags |= O_CLOEXEC;
    return flags;
}

/* ── Runtime Lifecycle ──────────────────────────────────────────────────── */

int __k_io_is_backend_active(void) {
    return k_io_runtime_is_active() ? 1 : 0;
}

int __k_io_runtime_init(void) {
    return k_io_runtime_init();
}

void __k_io_runtime_shutdown(void) {
    k_io_runtime_shutdown();
}
int __k_io_runtime_request_shutdown(void) { return k_io_runtime_request_shutdown(); }

void __k_io_get_metrics(long long* inflight, long long* cb_queued,
                        long long* cb_rejected, long long* reg_buffers) {
    KIoMetrics m;
    k_io_runtime_get_metrics(&m);
    if (inflight)     *inflight     = m.inflight_ops;
    if (cb_queued)    *cb_queued    = m.callbacks_queued;
    if (cb_rejected)  *cb_rejected  = m.callbacks_rejected;
    if (reg_buffers)  *reg_buffers  = m.registered_buffers;
}

/* ── System Lease ───────────────────────────────────────────────────────── */

void* __k_io_system_lease(void) {
    return (void*)k_executor_system_lease();
}
int __k_io_is_reaper(void) { return k_io_runtime_is_reaper(); }
int __k_io_errno_kind(int error) { return k_io_errno_to_error_kind(error); }

int __k_io_result_errno(const void* result) { return ((const KIoResult*)result)->err_no; }
int __k_io_result_kind(const void* result) { return ((const KIoResult*)result)->kind; }
long long __k_io_result_bytes(const void* result) {
    return ((const KIoResult*)result)->bytes_transferred;
}
long long __k_io_result_value(const void* result) { return ((const KIoResult*)result)->value; }
void __k_io_result_claim(void* result) { ((KIoResult*)result)->resource_claimed = 1; }

int __k_io_op_set_continuation(void* native, void* context,
    KCallable invoke, KCallable drop, void* future) {
    if (invoke.ctx || drop.ctx) {
        k_callable_drop(drop, context);
        return -EINVAL;
    }
    KNativeIoOperation* op = native;
    KOwnedCallback cb = { context, (void (*)(void*, void*))invoke.fn,
                         (void (*)(void*))drop.fn };
    int rc = k_io_operation_set_continuation(op, &cb);
    if (!rc && future) k_io_operation_bind_future(op, future);
    return rc;
}
void* __k_io_failed_operation(void* context, KCallable invoke,
                             KCallable drop, void* lease, int error) {
    if (invoke.ctx || drop.ctx) {
        k_callable_drop(drop, context);
        return NULL;
    }
    KOwnedCallback cb = { context, (void (*)(void*, void*))invoke.fn,
                         (void (*)(void*))drop.fn };
    KNativeIoOperation* op = k_io_operation_create(K_IO_OP_NONE, -1, NULL, 0, 0, 0,
                                                   NULL, &cb, lease, NULL);
    if (!op) {
        if (cb.drop) cb.drop(context);
        return NULL;
    }
    k_io_operation_on_kernel_completion(op, -error);
    return op;
}
/* Prepared requests allow binding an internal Promise publisher BEFORE submit.
 * Immediate __k_io_submit_* entry points are native callback-only adapters. */
void* __k_io_prepare_request(int opcode, void* handle, void* buffer, int length,
    long long offset, int flags, void* context, KCallable invoke,
    KCallable drop, void* lease) {
    if (invoke.ctx || drop.ctx) {
        k_callable_drop(drop, context);
        return NULL;
    }
    KOwnedCallback cb = { context, (void (*)(void*, void*))invoke.fn,
                         (void (*)(void*))drop.fn };
    KIoHandle* h = handle;
    KNativeIoOperation* op = k_io_operation_create(opcode, h ? k_io_handle_fd(h) : -1,
        NULL, length < 0 ? 0 : (uint32_t)length, offset, flags, buffer, &cb, lease, NULL);
    if (!op) {
        if (cb.drop) cb.drop(context);
        return NULL;
    }
    if (length < 0) op->request_error = EINVAL;
    if (opcode == K_IO_OP_OPENAT) op->fd = AT_FDCWD;
    if (opcode == K_IO_OP_CLOSE) op->fd = -1; /* assigned when close is scheduled */
    if (opcode == K_IO_OP_ACCEPT) op->flags |= SOCK_CLOEXEC | SOCK_NONBLOCK;
    if (opcode == K_IO_OP_SEND || opcode == K_IO_OP_SENDMSG) op->flags |= MSG_NOSIGNAL;
    if (opcode == K_IO_OP_STATX) {
        op->sockaddr_storage = calloc(1, sizeof(struct statx));
        if (!op->sockaddr_storage) op->request_error = ENOMEM;
        op->buf = strdup("");
        op->owns_request_buffer = true;
        if (!op->buf) op->request_error = ENOMEM;
        op->flags = AT_EMPTY_PATH;
        op->offset = STATX_SIZE;
    }
    if (!h && opcode != K_IO_OP_OPENAT && opcode != K_IO_OP_CONNECT && opcode != K_IO_OP_NONE &&
        opcode != K_IO_OP_CLOSE) op->request_error = EBADF;
    if (h) {
        k_io_handle_retain(h);
        op->handle = h;
    }
    return op;
}
int __k_io_submit_prepared(void* native, long long timeout) {
    KNativeIoOperation* op = native;
    if (!op) return -EINVAL;
    if (op->opcode == K_IO_OP_CLOSE && !op->handle) {
        if (atomic_exchange(&op->submitted, true)) return -EALREADY;
        k_io_operation_on_kernel_completion(op, 0);
        return 0;
    }
    int rc = k_io_runtime_submit(k_io_runtime_get(), op, timeout);
    /* A repeated submit is only an observer error: the first target still owns
     * its kernel storage and must not be completed with a synthetic CQE. */
    if (rc < 0 && rc != -EALREADY) k_io_operation_on_kernel_completion(op, rc);
    return rc;
}
int __k_io_op_set_address(void* native, const void* address, int length) {
    KNativeIoOperation* op = native;
    if (!op || atomic_load(&op->submitted) || !address || length <= 0 ||
        length > (int)sizeof(struct sockaddr_storage)) return -EINVAL;
    void* copy = malloc((size_t)length);
    if (!copy) return -ENOMEM;
    memcpy(copy, address, (size_t)length);
    free(op->sockaddr_storage);
    op->sockaddr_storage = copy;
    op->sockaddr_len = (uint32_t)length;
    return 0;
}
int __k_io_op_set_path(void* native, const uint32_t* path) {
    KNativeIoOperation* op = native;
    if (!op || atomic_load(&op->submitted)) return -EINVAL;
    char bytes[K_PATH_BUF];
    int rc = k_utf32_to_utf8(path, bytes, sizeof(bytes));
    if (rc < 0) return __k_io_op_set_request_error(op, rc);
    char* copy = strdup(bytes);
    if (!copy) return __k_io_op_set_request_error(op, -ENOMEM);
    if (op->owns_request_buffer) free(op->buf);
    op->buf = copy;
    op->owns_request_buffer = true;
    return 0;
}
int __k_io_op_set_request_error(void* native, int error) {
    KNativeIoOperation* op = native;
    if (!op || atomic_load(&op->submitted)) return -EINVAL;
    if (error) {
        int code = error == INT32_MIN ? EINVAL : error < 0 ? -error : error;
        if (!op->request_error) op->request_error = code;
        return -op->request_error;
    }
    return 0;
}
int __k_io_op_set_file_options(void* native, int options) {
    KNativeIoOperation* op = native;
    if (!op || atomic_load(&op->submitted) || op->opcode != K_IO_OP_OPENAT)
        return -EINVAL;
    if ((options & ~63) || !(options & (K_OPEN_READ | K_OPEN_WRITE)) ||
        ((options & K_OPEN_EXCLUSIVE) && !(options & K_OPEN_CREATE)) ||
        ((options & (K_OPEN_APPEND | K_OPEN_TRUNCATE)) && !(options & K_OPEN_WRITE)))
        return __k_io_op_set_request_error(op, -EINVAL);
    op->flags = decode_open_flags(options);
    return 0;
}
int __k_io_op_set_implicit_position(void* native) {
    KNativeIoOperation* op = native;
    if (!op || atomic_load(&op->submitted)) return -EINVAL;
    if (!op->handle || op->handle->is_stream_socket ||
        (op->opcode != K_IO_OP_READ && op->opcode != K_IO_OP_WRITE))
        return __k_io_op_set_request_error(op, -EINVAL);
    op->implicit_position = true;
    return 0;
}
int __k_io_op_set_fully(void* native) {
    KNativeIoOperation* op = native;
    if (!op || atomic_load(&op->submitted)) return -EINVAL;
    op->fully = true;
    bool stream = op->handle && op->handle->is_stream_socket;
    if (op->handle && op->handle->is_datagram_socket)
        return __k_io_op_set_request_error(op, -EINVAL);
    if ((op->opcode != K_IO_OP_READ && op->opcode != K_IO_OP_WRITE &&
         op->opcode != K_IO_OP_RECV && op->opcode != K_IO_OP_SEND) ||
        !op->buffer_obj || op->len > INT32_MAX ||
        (stream && op->opcode != K_IO_OP_RECV && op->opcode != K_IO_OP_SEND) ||
        (!stream && (op->opcode == K_IO_OP_RECV || op->opcode == K_IO_OP_SEND)) ||
        (!stream && !op->implicit_position && (op->offset < 0 ||
            op->len > INT64_MAX - op->offset)))
        return __k_io_op_set_request_error(op, -EINVAL);
    return 0;
}
void* __k_io_file_handle_prepare(int fd) {
    KIoHandle* h = k_io_handle_create(fd, false);
    /* Allocation failure leaves ownership with the operation's rollback. */
    if (h) h->owns_fd = false;
    return h;
}
void __k_io_file_handle_claim(void* native, void* result) {
    KIoHandle* h = native;
    pthread_mutex_lock(&h->mutex);
    h->owns_fd = true;
    pthread_mutex_unlock(&h->mutex);
    __k_io_result_claim(result);
}
long long __k_io_handle_position(const void* h) { return k_io_handle_position(h); }
int __k_io_handle_set_position(void* h, long long position) {
    return k_io_handle_set_position(h, position);
}
long long __k_io_statx_size(long long result) {
    const struct statx* value = (const struct statx*)(intptr_t)result;
    if (!value || !(value->stx_mask & STATX_SIZE)) return -EIO;
    return value->stx_size > INT64_MAX ? -EOVERFLOW : (long long)value->stx_size;
}

/* Numeric addresses are immediate POD preparation. Hostnames are copied here;
 * only the bounded resolver workers may call getaddrinfo. */
static int stream_address(const uint32_t* endpoint, int port, bool unix_socket,
                          struct sockaddr_storage* storage, socklen_t* length,
                          char* host, size_t capacity) {
    int rc = k_utf32_to_utf8(endpoint, host, capacity);
    if (rc < 0) return rc;
    memset(storage, 0, sizeof(*storage));
    if (unix_socket) {
        struct sockaddr_un* address = (struct sockaddr_un*)storage;
        size_t size = strlen(host);
        if (!size) return -EINVAL;
        if (size >= sizeof(address->sun_path)) return -ENAMETOOLONG;
        address->sun_family = AF_UNIX;
        memcpy(address->sun_path, host, size + 1);
        *length = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + size + 1);
        return 0;
    }
    if (port < 0 || port > 65535 || !host[0]) return -EINVAL;
    struct sockaddr_in* address = (struct sockaddr_in*)storage;
    address->sin_family = AF_INET;
    address->sin_port = htons((uint16_t)port);
    *length = sizeof(*address);
    if (!strcmp(host, "*")) {
        address->sin_addr.s_addr = htonl(INADDR_ANY);
        return 0;
    }
    return inet_pton(AF_INET, host, &address->sin_addr) == 1 ? 0 : 1;
}

int __k_io_op_set_stream_endpoint(void* native, const uint32_t* endpoint,
                                int port, int unix_socket) {
    KNativeIoOperation* op = native;
    if (!op || atomic_load(&op->submitted) || op->opcode != K_IO_OP_CONNECT || op->handle)
        return -EINVAL;
    struct sockaddr_storage address;
    socklen_t length;
    char host[K_PATH_BUF];
    int rc = stream_address(endpoint, port, unix_socket != 0, &address, &length,
                            host, sizeof(host));
    if (rc < 0) return __k_io_op_set_request_error(op, rc);
    if (rc == 1) {
        char service[6];
        snprintf(service, sizeof(service), "%d", port);
        op->resolver_node = strdup(host);
        op->resolver_service = strdup(service);
        if (!op->resolver_node || !op->resolver_service)
            return __k_io_op_set_request_error(op, -ENOMEM);
        op->resolving = true;
    } else {
        rc = __k_io_op_set_address(op, &address, length);
        if (rc < 0) return __k_io_op_set_request_error(op, rc);
    }
    /* Reject inherited/runtime-shutdown use before creating a descriptor. */
    rc = k_io_runtime_init();
    if (rc < 0) return __k_io_op_set_request_error(op, rc);
    int fd = socket(unix_socket ? AF_UNIX : AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) return __k_io_op_set_request_error(op, -errno);
    op->handle = k_io_handle_create(fd, true);
    if (!op->handle) {
        close(fd);
        return __k_io_op_set_request_error(op, -ENOMEM);
    }
    op->fd = fd;
    op->connect_owned = true;
    return 0;
}

void* __k_io_stream_handle_prepare(int fd) {
    KIoHandle* h = k_io_handle_create(fd, true);
    if (h) h->owns_fd = false;
    return h;
}
void* __k_io_stream_handle_create(int fd) {
    KIoHandle* h = k_io_handle_create(fd, true);
    if (!h && fd >= 0) close(fd);
    return h;
}
int __k_io_stream_bind(const uint32_t* endpoint, int port, int unix_socket, int backlog) {
    if (backlog <= 0) return -EINVAL;
    struct sockaddr_storage address;
    socklen_t length;
    char host[K_PATH_BUF];
    int rc = stream_address(endpoint, port, unix_socket != 0, &address, &length,
                            host, sizeof(host));
    /* Binding must never resolve a name on the caller. */
    if (rc) return rc < 0 ? rc : -EINVAL;
    rc = k_io_runtime_init();
    if (rc < 0) return rc;
    int fd = socket(unix_socket ? AF_UNIX : AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) return -errno;
    int enabled = 1;
    if (!unix_socket && setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) < 0)
        rc = -errno;
    else if (bind(fd, (struct sockaddr*)&address, length) < 0) rc = -errno;
    else if (listen(fd, backlog) < 0) rc = -errno;
    if (rc < 0) close(fd);
    return rc < 0 ? rc : fd;
}
int __k_io_stream_local_port(void* native) {
    KIoHandle* h = native;
    if (!h || h->creator_pid != getpid()) return -EBADF;
    pthread_mutex_lock(&h->mutex);
    struct sockaddr_in address;
    socklen_t length = sizeof(address);
    int rc = atomic_load(&h->state) != K_HANDLE_OPEN ? -EBADF
           : getsockname(h->fd, (struct sockaddr*)&address, &length) < 0 ? -errno
           : address.sin_family != AF_INET ? -EAFNOSUPPORT : ntohs(address.sin_port);
    pthread_mutex_unlock(&h->mutex);
    return rc;
}
int __k_io_stream_configure(int fd) {
    int flags = fcntl(fd, F_GETFL);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        int error = errno;
        close(fd);
        return -error;
    }
    flags = fcntl(fd, F_GETFD);
    if (flags < 0 || fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0) {
        int error = errno;
        close(fd);
        return -error;
    }
    return 0;
}

struct GNativeAddress {
    _Atomic uint32_t refs;
    struct sockaddr_storage storage;
    socklen_t length;
    uint32_t* host;
    int host_length;
    int port;
    bool resolved;
};

static GNativeAddress* address_allocate(const uint32_t* host, size_t length) {
    GNativeAddress* address = calloc(1, sizeof(*address));
    if (!address) return NULL;
    address->host = calloc(length + 1, sizeof(uint32_t));
    if (!address->host) { free(address); return NULL; }
    memcpy(address->host, host, length * sizeof(uint32_t));
    address->host_length = (int)length;
    atomic_init(&address->refs, 1u);
    return address;
}
void* __k_io_address_create(const uint32_t* endpoint, int port, int unix_socket) {
    struct sockaddr_storage storage;
    socklen_t length;
    char text[K_PATH_BUF];
    int rc = stream_address(endpoint, port, unix_socket != 0, &storage, &length,
                            text, sizeof(text));
    if (rc < 0) { errno = -rc; return NULL; }
    size_t count = 0;
    while (endpoint[count]) ++count;
    GNativeAddress* address = address_allocate(endpoint, count);
    if (!address) { errno = ENOMEM; return NULL; }
    address->storage = storage;
    address->length = length;
    address->port = unix_socket ? 0 : port;
    address->resolved = rc == 0;
    return address;
}
void __k_io_address_retain(void* native) {
    GNativeAddress* address = native;
    if (address) atomic_fetch_add(&address->refs, 1u);
}
void __k_io_address_release(void* native) {
    GNativeAddress* address = native;
    if (!address || atomic_fetch_sub(&address->refs, 1u) != 1u) return;
    free(address->host);
    free(address);
}
const uint32_t* __k_io_address_host(const void* native) {
    const GNativeAddress* address = native;
    return address ? address->host : NULL;
}
int __k_io_address_host_length(const void* native) {
    const GNativeAddress* address = native;
    return address ? address->host_length : 0;
}
int __k_io_address_copy_host(const void* native, uint32_t* destination, int capacity) {
    const GNativeAddress* address = native;
    int length = address ? address->host_length : 0;
    if (!destination || capacity < 0) return -EINVAL;
    if (capacity <= length) return -ERANGE;
    if (address)
        memcpy(destination, address->host, ((size_t)length + 1) * sizeof(*destination));
    else
        destination[0] = 0;
    return length;
}
int __k_io_address_port(const void* native) {
    const GNativeAddress* address = native;
    return address ? address->port : 0;
}
int __k_io_address_family(const void* native) {
    const GNativeAddress* address = native;
    return !address ? 0 : address->storage.ss_family == AF_INET ? 4
                        : address->storage.ss_family == AF_UNIX ? 1 : 0;
}
int __k_io_address_resolved(const void* native) {
    const GNativeAddress* address = native;
    return address && address->resolved;
}
int __k_io_setup_errno(void) { return errno; }

/* Kernel lengths, not the allocated storage size, determine what is readable. */
static GNativeAddress* address_from_sockaddr(const struct sockaddr* native, socklen_t length) {
    if (!native || length < sizeof(sa_family_t) ||
        length > sizeof(struct sockaddr_storage)) { errno = EINVAL; return NULL; }
    char text[K_PATH_BUF];
    int port = 0;
    if (native->sa_family == AF_INET) {
        if (length < sizeof(struct sockaddr_in)) { errno = EINVAL; return NULL; }
        const struct sockaddr_in* address = (const struct sockaddr_in*)native;
        if (!inet_ntop(AF_INET, &address->sin_addr, text, sizeof(text))) return NULL;
        port = ntohs(address->sin_port);
    } else if (native->sa_family == AF_UNIX) {
        size_t base = offsetof(struct sockaddr_un, sun_path);
        if (length < base || length > sizeof(struct sockaddr_un)) { errno = EINVAL; return NULL; }
        const struct sockaddr_un* address = (const struct sockaddr_un*)native;
        size_t size = length - base;
        if (size && address->sun_path[0] == 0) { errno = EAFNOSUPPORT; return NULL; }
        size = strnlen(address->sun_path, size);
        memcpy(text, address->sun_path, size);
        text[size] = 0;
    } else { errno = EAFNOSUPPORT; return NULL; }
    uint32_t host[K_PATH_BUF];
    size_t count = 0;
    const unsigned char* input = (const unsigned char*)text;
    while (*input) {
        uint32_t cp = *input++;
        unsigned continuation = 0;
        uint32_t minimum = 0;
        if (cp >= 0xC2 && cp <= 0xDF) { cp &= 0x1F; continuation = 1; minimum = 0x80; }
        else if (cp >= 0xE0 && cp <= 0xEF) { cp &= 0x0F; continuation = 2; minimum = 0x800; }
        else if (cp >= 0xF0 && cp <= 0xF4) { cp &= 7; continuation = 3; minimum = 0x10000; }
        else if (cp >= 0x80) { errno = EILSEQ; return NULL; }
        for (unsigned i = 0; i < continuation; ++i) {
            if ((*input & 0xC0) != 0x80) { errno = EILSEQ; return NULL; }
            cp = (cp << 6) | (*input++ & 0x3F);
        }
        if (cp < minimum || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            errno = EILSEQ; return NULL;
        }
        host[count++] = cp;
    }
    GNativeAddress* address = address_allocate(host, count);
    if (!address) { errno = ENOMEM; return NULL; }
    memcpy(&address->storage, native, length);
    address->length = length;
    address->port = port;
    address->resolved = true;
    return address;
}

void* __k_io_datagram_handle_create(int fd) {
    int type = 0;
    socklen_t size = sizeof(type);
    int status = getsockopt(fd, SOL_SOCKET, SO_TYPE, &type, &size);
    if (status < 0 || type != SOCK_DGRAM) {
        int error = status < 0 ? errno : EINVAL;
        close(fd); errno = error; return NULL;
    }
    int rc = __k_io_stream_configure(fd);
    if (rc < 0) { errno = -rc; return NULL; }
    struct sockaddr_storage local;
    socklen_t local_length = sizeof(local);
    if (getsockname(fd, (struct sockaddr*)&local, &local_length) < 0) {
        int error = errno; close(fd); errno = error; return NULL;
    }
    if (local.ss_family != AF_INET && local.ss_family != AF_UNIX) {
        close(fd); errno = EAFNOSUPPORT; return NULL;
    }
    KIoHandle* handle = k_io_handle_create(fd, false);
    if (!handle) { close(fd); errno = ENOMEM; return NULL; }
    handle->is_datagram_socket = true;
    struct sockaddr_storage peer;
    socklen_t peer_length = sizeof(peer);
    if (getpeername(fd, (struct sockaddr*)&peer, &peer_length) == 0) {
        handle->remote_address = address_from_sockaddr((struct sockaddr*)&peer, peer_length);
        if (!handle->remote_address) {
            int error = errno; k_io_handle_release(handle); errno = error; return NULL;
        }
    } else if (errno != ENOTCONN) {
        int error = errno; k_io_handle_release(handle); errno = error; return NULL;
    }
    return handle;
}
void* __k_io_datagram_bind(const uint32_t* endpoint, int port, int unix_socket) {
    struct sockaddr_storage address;
    socklen_t length;
    char path[K_PATH_BUF];
    int rc = stream_address(endpoint, port, unix_socket != 0, &address, &length,
                            path, sizeof(path));
    if (rc) { errno = rc < 0 ? -rc : EINVAL; return NULL; }
    rc = k_io_runtime_init();
    if (rc < 0) { errno = -rc; return NULL; }
    int fd = socket(unix_socket ? AF_UNIX : AF_INET,
                    SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) return NULL;
    KIoHandle* handle = k_io_handle_create(fd, false);
    char* owned_path = unix_socket ? strdup(path) : NULL;
    if (!handle || (unix_socket && !owned_path)) {
        free(owned_path);
        if (handle) k_io_handle_release(handle); else close(fd);
        errno = ENOMEM; return NULL;
    }
    handle->is_datagram_socket = true;
    if (bind(fd, (struct sockaddr*)&address, length) < 0) {
        int error = errno;
        free(owned_path);
        k_io_handle_release(handle);
        errno = error; return NULL;
    }
    if (unix_socket) {
        struct stat identity;
        if (lstat(path, &identity) < 0) {
            int error = errno;
            /* Without an identity, never risk unlinking a replacement path. */
            fprintf(stderr, "klang: datagram bind pathname identity: %s\n", strerror(error));
            free(owned_path); k_io_handle_release(handle);
            errno = error; return NULL;
        }
        handle->owned_socket_path = owned_path;
        handle->socket_path_device = identity.st_dev;
        handle->socket_path_inode = identity.st_ino;
    }
    return handle;
}
void* __k_io_datagram_bound_address(void* native) {
    KIoHandle* handle = native;
    if (!handle || handle->creator_pid != getpid()) { errno = EBADF; return NULL; }
    struct sockaddr_storage address;
    socklen_t length = sizeof(address);
    pthread_mutex_lock(&handle->mutex);
    int rc = atomic_load(&handle->state) != K_HANDLE_OPEN ? EBADF
           : getsockname(handle->fd, (struct sockaddr*)&address, &length) < 0 ? errno : 0;
    pthread_mutex_unlock(&handle->mutex);
    if (rc) { errno = rc; return NULL; }
    return address_from_sockaddr((struct sockaddr*)&address, length);
}
int __k_io_op_set_datagram_message(void* native) {
    KNativeIoOperation* op = native;
    if (!op || atomic_load(&op->submitted) || !op->handle ||
        !op->handle->is_datagram_socket ||
        (op->opcode != K_IO_OP_RECVMSG && op->opcode != K_IO_OP_SENDMSG))
        return -EINVAL;
    if (op->msghdr) return -EALREADY;
    KDatagramMessage* context = calloc(1, sizeof(*context));
    if (!context) return -ENOMEM;
    context->datagram_length = -1;
    context->vector = (struct iovec){ op->buf, op->len };
    context->message.msg_iov = &context->vector;
    context->message.msg_iovlen = 1;
    if (op->opcode == K_IO_OP_RECVMSG) {
        context->message.msg_name = &context->source_storage;
        context->message.msg_namelen = sizeof(context->source_storage);
        pthread_mutex_lock(&op->handle->mutex);
        context->source = op->handle->remote_address;
        __k_io_address_retain(context->source);
        pthread_mutex_unlock(&op->handle->mutex);
        op->flags |= MSG_TRUNC;
    }
    op->msghdr = context;
    return 0;
}
int __k_io_op_set_datagram_endpoint(void* native, const void* endpoint) {
    KNativeIoOperation* op = native;
    const GNativeAddress* address = endpoint;
    if (!op || atomic_load(&op->submitted) || !op->handle ||
        !op->handle->is_datagram_socket || !address ||
        (op->opcode != K_IO_OP_CONNECT && op->opcode != K_IO_OP_SENDMSG))
        return -EINVAL;
    if (op->opcode == K_IO_OP_SENDMSG && !op->msghdr) return -EINVAL;
    if (op->resolving || op->sockaddr_storage) return -EALREADY;
    if (address->storage.ss_family == AF_INET && address->port == 0) return -EINVAL;
    if (address->storage.ss_family == AF_UNIX &&
        (address->length <= offsetof(struct sockaddr_un, sun_path) ||
         !((const struct sockaddr_un*)&address->storage)->sun_path[0])) return -EINVAL;
    if (!address->resolved) {
        if (address->storage.ss_family != AF_INET) return -EAFNOSUPPORT;
        char host[K_PATH_BUF], service[6];
        int rc = k_utf32_to_utf8(address->host, host, sizeof(host));
        if (rc < 0) return rc;
        snprintf(service, sizeof(service), "%d", address->port);
        op->resolver_node = strdup(host);
        op->resolver_service = strdup(service);
        if (!op->resolver_node || !op->resolver_service) return -ENOMEM;
        op->resolving = true;
        return 0;
    }
    if (address->storage.ss_family == AF_INET &&
        (((const struct sockaddr_in*)&address->storage)->sin_addr.s_addr == INADDR_ANY ||
         address->port == 0)) return -EINVAL;
    int rc = __k_io_op_set_address(op, &address->storage, address->length);
    if (!rc && op->opcode == K_IO_OP_SENDMSG) {
        struct msghdr* message = op->msghdr;
        message->msg_name = op->sockaddr_storage;
        message->msg_namelen = op->sockaddr_len;
    }
    return rc;
}
int __k_io_datagram_finish(void* native, int result) {
    KNativeIoOperation* op = native;
    if (op->opcode == K_IO_OP_CONNECT) {
        GNativeAddress* address = address_from_sockaddr(op->sockaddr_storage, op->sockaddr_len);
        if (!address) return -errno;
        pthread_mutex_lock(&op->handle->mutex);
        void* previous = op->handle->remote_address;
        op->handle->remote_address = address;
        pthread_mutex_unlock(&op->handle->mutex);
        __k_io_address_release(previous);
    } else if (op->opcode == K_IO_OP_RECVMSG) {
        KDatagramMessage* context = op->msghdr;
        if (!context) return -EINVAL;
        context->copied = (uint32_t)result < op->len ? (uint32_t)result : op->len;
        context->datagram_length = result;
        context->truncated = (context->message.msg_flags & MSG_TRUNC) != 0 ||
                             (uint32_t)result > op->len;
        if (context->message.msg_namelen) {
            GNativeAddress* source = address_from_sockaddr(context->message.msg_name,
                                                         context->message.msg_namelen);
            if (!source) return -errno;
            __k_io_address_release(context->source);
            context->source = source;
        } else if (!context->source) {
            pthread_mutex_lock(&op->handle->mutex);
            context->source = op->handle->remote_address;
            __k_io_address_retain(context->source);
            pthread_mutex_unlock(&op->handle->mutex);
            if (!context->source) return -EDESTADDRREQ;
        }
    }
    return 0;
}
void __k_io_datagram_message_release(void* native) {
    KDatagramMessage* context = native;
    if (context) __k_io_address_release(context->source);
    free(context);
}
void* __k_io_datagram_source(int64_t value) {
    KDatagramMessage* context = (KDatagramMessage*)(intptr_t)value;
    if (!context || !context->source) { errno = EINVAL; return NULL; }
    __k_io_address_retain(context->source);
    return context->source;
}
int __k_io_datagram_copied(int64_t value) {
    return (int)((KDatagramMessage*)(intptr_t)value)->copied;
}
int __k_io_datagram_truncated(int64_t value) {
    return ((KDatagramMessage*)(intptr_t)value)->truncated;
}
int64_t __k_io_datagram_length(int64_t value) {
    return ((KDatagramMessage*)(intptr_t)value)->datagram_length;
}

/* ── Execution Context TLS ──────────────────────────────────────────────── */

void* __k_io_execution_context_current(void) {
    return (void*)k_execution_context_current();
}

int __k_io_execution_context_is_cancellation_requested(void) {
    return k_execution_context_is_cancellation_requested() ? 1 : 0;
}

/* ── Handle Lifecycle ───────────────────────────────────────────────────── */

void* __k_io_handle_create(int fd, int is_stream) {
    return (void*)k_io_handle_create(fd, is_stream != 0);
}

void __k_io_handle_retain(void* h) {
    k_io_handle_retain((KIoHandle*)h);
}

void __k_io_handle_release(void* h) {
    k_io_handle_release((KIoHandle*)h);
}

int __k_io_handle_fd(const void* h) {
    return k_io_handle_fd((const KIoHandle*)h);
}

int __k_io_handle_is_open(const void* h) {
    return k_io_handle_is_open((const KIoHandle*)h) ? 1 : 0;
}

void* __k_io_handle_close(void* h, const KOwnedCallback* cb, void* lease, void* fut) {
    KNativeIoOperation* op = k_io_handle_close(h, cb, lease, fut);
    if (!op && cb && cb->drop) cb->drop(cb->context);
    return op;
}

int __k_io_handle_close_sync(void* h) {
    return k_io_handle_close_sync((KIoHandle*)h);
}

/* ── Operation Inspection & Management ──────────────────────────────────── */

void __k_io_op_retain(void* op) {
    k_io_operation_retain((KNativeIoOperation*)op);
}

void __k_io_op_release(void* op) {
    k_io_operation_release((KNativeIoOperation*)op);
}

int __k_io_op_cancel(void* op) {
    return k_io_operation_cancel((KNativeIoOperation*)op) ? 1 : 0;
}

int __k_io_op_stage(const void* op) {
    return (int)k_io_operation_derive_stage((const KNativeIoOperation*)op);
}

int __k_io_op_is_io_done(const void* op) {
    return k_io_operation_is_io_done((const KNativeIoOperation*)op) ? 1 : 0;
}

int __k_io_op_is_callback_running(const void* op) {
    return k_io_operation_is_callback_running((const KNativeIoOperation*)op) ? 1 : 0;
}

int __k_io_op_is_done(const void* op) {
    return k_io_operation_is_done((const KNativeIoOperation*)op) ? 1 : 0;
}

int __k_io_op_is_cancelled(const void* op) {
    return k_io_operation_is_cancelled((const KNativeIoOperation*)op) ? 1 : 0;
}

int __k_io_op_is_interruption_requested(const void* op) {
    return k_io_operation_is_interruption_requested((const KNativeIoOperation*)op) ? 1 : 0;
}

long long __k_io_op_bytes_transferred(const void* op) {
    return (long long)k_io_operation_bytes_transferred((const KNativeIoOperation*)op);
}

int __k_io_op_error_kind(const void* op) {
    KIoResult r;
    k_io_operation_error((const KNativeIoOperation*)op, &r);
    return (int)r.kind;
}

int __k_io_op_error_code(const void* op) {
    KIoResult r;
    k_io_operation_error((const KNativeIoOperation*)op, &r);
    return r.err_no;
}

int __k_io_op_wait_quiescent(void* op, long long timeout_nanos) {
    return k_io_operation_wait_quiescent((KNativeIoOperation*)op, (int64_t)timeout_nanos);
}

/* ── Submissions ────────────────────────────────────────────────────────── */

static void* reject_carrier(const KOwnedCallback* cb) {
    if (cb && cb->drop) cb->drop(cb->context);
    return NULL;
}

void* __k_io_submit_read(void* h, void* buf, int len, long long offset,
                         long long timeout_nanos, void* buffer_obj,
                         const KOwnedCallback* cb, void* lease, void* fut) {
    KIoHandle* handle = (KIoHandle*)h;
    if (handle == NULL || len < 0) return reject_carrier(cb);

    KNativeIoOperation* op = k_io_operation_create(K_IO_OP_READ, handle->fd, buf,
                                                   (uint32_t)len, (int64_t)offset, 0,
                                                   buffer_obj, cb,
                                                   (KExecutorLease*)lease, (KFutureState*)fut);
    if (op == NULL) return reject_carrier(cb);
    k_io_handle_retain(handle);
    op->handle = handle;

    KIoRuntime* rt = k_io_runtime_get();
    int rc = k_io_runtime_submit(rt, op, (int64_t)timeout_nanos);
    if (rc < 0) {
        k_io_operation_on_kernel_completion(op, rc);
    }
    return (void*)op;
}

void* __k_io_submit_write(void* h, const void* buf, int len, long long offset,
                          long long timeout_nanos, void* buffer_obj,
                          const KOwnedCallback* cb, void* lease, void* fut) {
    KIoHandle* handle = (KIoHandle*)h;
    if (handle == NULL || len < 0) return reject_carrier(cb);

    KNativeIoOperation* op = k_io_operation_create(K_IO_OP_WRITE, handle->fd, (void*)buf,
                                                   (uint32_t)len, (int64_t)offset, 0,
                                                   buffer_obj, cb,
                                                   (KExecutorLease*)lease, (KFutureState*)fut);
    if (op == NULL) return reject_carrier(cb);
    k_io_handle_retain(handle);
    op->handle = handle;

    KIoRuntime* rt = k_io_runtime_get();
    int rc = k_io_runtime_submit(rt, op, (int64_t)timeout_nanos);
    if (rc < 0) {
        k_io_operation_on_kernel_completion(op, rc);
    }
    return (void*)op;
}

void* __k_io_submit_fsync(void* h, long long timeout_nanos,
                          const KOwnedCallback* cb, void* lease, void* fut) {
    KIoHandle* handle = (KIoHandle*)h;
    if (handle == NULL) return reject_carrier(cb);

    KNativeIoOperation* op = k_io_operation_create(K_IO_OP_FSYNC, handle->fd, NULL,
                                                   0, 0, 0, NULL, cb,
                                                   (KExecutorLease*)lease, (KFutureState*)fut);
    if (op == NULL) return reject_carrier(cb);
    k_io_handle_retain(handle);
    op->handle = handle;

    KIoRuntime* rt = k_io_runtime_get();
    int rc = k_io_runtime_submit(rt, op, (int64_t)timeout_nanos);
    if (rc < 0) {
        k_io_operation_on_kernel_completion(op, rc);
    }
    return (void*)op;
}

void* __k_io_submit_open(const uint32_t* path, int k_flags, int mode,
                         long long timeout_nanos, const KOwnedCallback* cb,
                         void* lease, void* fut) {
    char pbuf[K_PATH_BUF];
    int path_error = k_utf32_to_utf8(path, pbuf, sizeof(pbuf));
    if (path_error < 0) {
        KNativeIoOperation* failed = k_io_operation_create(K_IO_OP_OPENAT, -1, NULL,
            0, 0, 0, NULL, cb, lease, fut);
        if (failed) k_io_operation_on_kernel_completion(failed, path_error);
        else if (cb && cb->drop) cb->drop(cb->context);
        return failed;
    }
    int flags = decode_open_flags(k_flags);

    char* path_copy = strdup(pbuf);
    if (path_copy == NULL) return reject_carrier(cb);

    KNativeIoOperation* op = k_io_operation_create(K_IO_OP_OPENAT, AT_FDCWD, path_copy,
                                                   0, mode, flags, NULL, cb,
                                                   (KExecutorLease*)lease, (KFutureState*)fut);
    if (op == NULL) {
        free(path_copy);
        return reject_carrier(cb);
    }
    op->owns_request_buffer = true;

    KIoRuntime* rt = k_io_runtime_get();
    int rc = k_io_runtime_submit(rt, op, (int64_t)timeout_nanos);
    if (rc < 0) {
        k_io_operation_on_kernel_completion(op, rc);
    }
    return (void*)op;
}

void* __k_io_submit_connect(void* h, const void* addr, int addrlen,
                            long long timeout_nanos, const KOwnedCallback* cb,
                            void* lease, void* fut) {
    KIoHandle* handle = (KIoHandle*)h;
    if (handle == NULL || !addr || addrlen <= 0 ||
        addrlen > (int)sizeof(struct sockaddr_storage)) {
        if (cb && cb->drop) cb->drop(cb->context);
        return NULL;
    }

    void* addr_copy = malloc(addrlen);
    if (addr_copy == NULL) return reject_carrier(cb);
    memcpy(addr_copy, addr, addrlen);

    KNativeIoOperation* op = k_io_operation_create(K_IO_OP_CONNECT, handle->fd, NULL,
                                                   0, 0, 0, NULL, cb,
                                                   (KExecutorLease*)lease, (KFutureState*)fut);
    if (op == NULL) {
        free(addr_copy);
        return reject_carrier(cb);
    }
    k_io_handle_retain(handle);
    op->handle = handle;
    op->sockaddr_storage = addr_copy;
    op->sockaddr_len     = (uint32_t)addrlen;

    KIoRuntime* rt = k_io_runtime_get();
    int rc = k_io_runtime_submit(rt, op, (int64_t)timeout_nanos);
    if (rc < 0) {
        k_io_operation_on_kernel_completion(op, rc);
    }
    return (void*)op;
}

void* __k_io_submit_accept(void* h, long long timeout_nanos,
                           const KOwnedCallback* cb, void* lease, void* fut) {
    KIoHandle* handle = (KIoHandle*)h;
    if (handle == NULL) return reject_carrier(cb);

    KNativeIoOperation* op = k_io_operation_create(K_IO_OP_ACCEPT, handle->fd, NULL,
                                                   0, 0, 0, NULL, cb,
                                                   (KExecutorLease*)lease, (KFutureState*)fut);
    if (op == NULL) return reject_carrier(cb);
    k_io_handle_retain(handle);
    op->handle = handle;
    op->flags = SOCK_CLOEXEC | SOCK_NONBLOCK;

    KIoRuntime* rt = k_io_runtime_get();
    int rc = k_io_runtime_submit(rt, op, (int64_t)timeout_nanos);
    if (rc < 0) {
        k_io_operation_on_kernel_completion(op, rc);
    }
    return (void*)op;
}

void* __k_io_submit_recv(void* h, void* buf, int len, int flags,
                         long long timeout_nanos, void* buffer_obj,
                         const KOwnedCallback* cb, void* lease, void* fut) {
    KIoHandle* handle = (KIoHandle*)h;
    if (handle == NULL || len < 0) return reject_carrier(cb);

    KNativeIoOperation* op = k_io_operation_create(K_IO_OP_RECV, handle->fd, buf,
                                                   (uint32_t)len, 0, flags,
                                                   buffer_obj, cb,
                                                   (KExecutorLease*)lease, (KFutureState*)fut);
    if (op == NULL) return reject_carrier(cb);
    k_io_handle_retain(handle);
    op->handle = handle;

    KIoRuntime* rt = k_io_runtime_get();
    int rc = k_io_runtime_submit(rt, op, (int64_t)timeout_nanos);
    if (rc < 0) {
        k_io_operation_on_kernel_completion(op, rc);
    }
    return (void*)op;
}

void* __k_io_submit_send(void* h, const void* buf, int len, int flags,
                         long long timeout_nanos, void* buffer_obj,
                         const KOwnedCallback* cb, void* lease, void* fut) {
    KIoHandle* handle = (KIoHandle*)h;
    if (handle == NULL || len < 0) return reject_carrier(cb);

    KNativeIoOperation* op = k_io_operation_create(K_IO_OP_SEND, handle->fd, (void*)buf,
                                                   (uint32_t)len, 0, flags | MSG_NOSIGNAL,
                                                   buffer_obj, cb,
                                                   (KExecutorLease*)lease, (KFutureState*)fut);
    if (op == NULL) return reject_carrier(cb);
    k_io_handle_retain(handle);
    op->handle = handle;

    KIoRuntime* rt = k_io_runtime_get();
    int rc = k_io_runtime_submit(rt, op, (int64_t)timeout_nanos);
    if (rc < 0) {
        k_io_operation_on_kernel_completion(op, rc);
    }
    return (void*)op;
}

void* __k_io_submit_resolve(const uint32_t* host, const uint32_t* service,
                            int family, int socktype, int protocol,
                            long long timeout_nanos, const KOwnedCallback* cb,
                            void* lease, void* fut) {
    char hbuf[K_PATH_BUF];
    char sbuf[256];
    int encoding_error = k_utf32_to_utf8(host, hbuf, sizeof(hbuf));
    if (service != NULL) {
        int rc = k_utf32_to_utf8(service, sbuf, sizeof(sbuf));
        if (rc < 0) encoding_error = rc;
    }

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = family;
    hints.ai_socktype = socktype;
    hints.ai_protocol = protocol;

    KNativeIoOperation* op = k_io_operation_create(K_IO_OP_NONE, -1, NULL, 0, 0, 0,
                                                   NULL, cb,
                                                   (KExecutorLease*)lease, (KFutureState*)fut);
    if (op == NULL) return reject_carrier(cb);

    if (encoding_error < 0) {
        k_io_operation_on_kernel_completion(op, encoding_error);
        return op;
    }
    if (timeout_nanos > 0) op->deadline_nanos = k_monotonic_nanos() + timeout_nanos;
    KIoRuntime* rt = k_io_runtime_get();
    int rc = k_io_runtime_submit_resolve(rt, op, hbuf,
                                         (service != NULL) ? sbuf : NULL, &hints);
    if (rc < 0) {
        k_io_operation_on_kernel_completion(op, rc);
    }
    return (void*)op;
}
