/*
 * K Language runtime — network I/O FFI wrappers
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
#include "runtime_thread.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define K_PATH_BUF 1024

#define K_RES_INTERRUPTED (-1LL)
#define K_RES_TIMEOUT     (-2LL)
#define K_RES_CLOSED      (-3LL)
#define K_RES_ERROR_BASE  (-1000LL)

#define K_WAIT_SLICE_NS   20000000LL

static int64_t monotonic_nanos(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
}

static int is_interrupted(void) {
    KRuntimeThread* self = k_thread_current();
    if (self == NULL) {
        return 0;
    }
    return atomic_load_explicit(&self->interrupt.interrupted, memory_order_acquire) != 0u;
}

static long long encode_errno(int err) {
    return K_RES_ERROR_BASE - (long long)err;
}

static void k_utf32_to_utf8(const uint32_t* src, char* dst, size_t dst_cap) {
    size_t out = 0;
    if (src == NULL) {
        if (dst_cap > 0u) {
            dst[0] = '\0';
        }
        return;
    }
    for (size_t i = 0u; src[i] != 0u && out + 4u < dst_cap; ++i) {
        uint32_t cp = src[i];
        if (cp < 0x80u) {
            dst[out++] = (char)cp;
        } else if (cp < 0x800u) {
            dst[out++] = (char)(0xC0u | (cp >> 6u));
            dst[out++] = (char)(0x80u | (cp & 0x3Fu));
        } else if (cp < 0x10000u) {
            dst[out++] = (char)(0xE0u | (cp >> 12u));
            dst[out++] = (char)(0x80u | ((cp >> 6u) & 0x3Fu));
            dst[out++] = (char)(0x80u | (cp & 0x3Fu));
        } else {
            dst[out++] = (char)(0xF0u | (cp >> 18u));
            dst[out++] = (char)(0x80u | ((cp >> 12u) & 0x3Fu));
            dst[out++] = (char)(0x80u | ((cp >> 6u) & 0x3Fu));
            dst[out++] = (char)(0x80u | (cp & 0x3Fu));
        }
    }
    dst[out] = '\0';
}

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return errno;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        return errno;
    }
    return 0;
}

static int set_cloexec(int fd) {
    int flags = fcntl(fd, F_GETFD, 0);
    if (flags < 0) {
        return errno;
    }
    if (fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0) {
        return errno;
    }
    return 0;
}

/* Wait until fd becomes ready for `events` (POLLIN/POLLOUT), or interruption/timeout. */
static int64_t make_deadline(int64_t timeout_nanos) {
    if (timeout_nanos < 0) {
        return -1;
    }
    return monotonic_nanos() + timeout_nanos;
}

static int64_t remaining_to_deadline(int64_t deadline_nanos) {
    if (deadline_nanos < 0) {
        return -1;
    }
    return deadline_nanos - monotonic_nanos();
}

static long long wait_fd_ready(int fd, short events, int64_t deadline_nanos) {
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = events;
    pfd.revents = 0;

    for (;;) {
        if (is_interrupted()) {
            return K_RES_INTERRUPTED;
        }

        int timeout_ms = 20;
        if (deadline_nanos >= 0) {
            int64_t remaining_ns = remaining_to_deadline(deadline_nanos);
            if (remaining_ns <= 0) {
                return K_RES_TIMEOUT;
            }
            int64_t slice_ns = remaining_ns < K_WAIT_SLICE_NS ? remaining_ns : K_WAIT_SLICE_NS;
            timeout_ms = (int)(slice_ns / 1000000LL);
            if (timeout_ms <= 0) {
                timeout_ms = 1;
            }
        }

        int rc = poll(&pfd, 1, timeout_ms);
        if (rc > 0) {
            if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
                return K_RES_CLOSED;
            }
            if ((pfd.revents & events) != 0) {
                return 0;
            }
            continue;
        }
        if (rc == 0) {
            if (deadline_nanos >= 0 && remaining_to_deadline(deadline_nanos) <= 0) {
                return K_RES_TIMEOUT;
            }
            continue;
        }
        if (errno == EINTR) {
            continue;
        }
        return encode_errno(errno);
    }
}

static int sockaddr_from_host_port(const uint32_t* host_utf32, int port,
                                   int for_bind, struct sockaddr_in* out) {
    if (port < 0 || port > 65535) {
        return EINVAL;
    }

    memset(out, 0, sizeof(*out));
    out->sin_family = AF_INET;
    out->sin_port = htons((uint16_t)port);

    char host[K_PATH_BUF];
    k_utf32_to_utf8(host_utf32, host, sizeof(host));

    if (host[0] == '\0') {
        out->sin_addr.s_addr = for_bind ? htonl(INADDR_ANY) : htonl(INADDR_LOOPBACK);
        return 0;
    }
    if (strcmp(host, "localhost") == 0) {
        out->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        return 0;
    }
    if (strcmp(host, "*") == 0 && for_bind) {
        out->sin_addr.s_addr = htonl(INADDR_ANY);
        return 0;
    }
    if (inet_pton(AF_INET, host, &out->sin_addr) != 1) {
        return EINVAL;
    }
    return 0;
}

long long __k_net_connect(const uint32_t* host, int port, long long timeout_nanos) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return encode_errno(errno);
    }
    int rc = set_cloexec(fd);
    if (rc != 0) {
        close(fd);
        return encode_errno(rc);
    }
    rc = set_nonblocking(fd);
    if (rc != 0) {
        close(fd);
        return encode_errno(rc);
    }

    struct sockaddr_in addr;
    rc = sockaddr_from_host_port(host, port, 0, &addr);
    if (rc != 0) {
        close(fd);
        return encode_errno(rc);
    }

    if (connect(fd, (const struct sockaddr*)&addr, sizeof(addr)) == 0) {
        return (long long)fd;
    }
    if (errno != EINPROGRESS) {
        rc = errno;
        close(fd);
        return encode_errno(rc);
    }

    int64_t deadline = make_deadline((int64_t)timeout_nanos);
    long long wr = wait_fd_ready(fd, POLLOUT, deadline);
    if (wr != 0) {
        close(fd);
        return wr;
    }

    int so_err = 0;
    socklen_t so_len = (socklen_t)sizeof(so_err);
    if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &so_err, &so_len) < 0) {
        rc = errno;
        close(fd);
        return encode_errno(rc);
    }
    if (so_err != 0) {
        close(fd);
        return encode_errno(so_err);
    }
    return (long long)fd;
}

int __k_net_bind_listen(const uint32_t* host, int port, int backlog, int reuse_addr) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return (int)encode_errno(errno);
    }
    int rc = set_cloexec(fd);
    if (rc != 0) {
        close(fd);
        return (int)encode_errno(rc);
    }
    rc = set_nonblocking(fd);
    if (rc != 0) {
        close(fd);
        return (int)encode_errno(rc);
    }

    if (reuse_addr != 0) {
        int one = 1;
        if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one)) < 0) {
            rc = errno;
            close(fd);
            return (int)encode_errno(rc);
        }
    }

    struct sockaddr_in addr;
    rc = sockaddr_from_host_port(host, port, 1, &addr);
    if (rc != 0) {
        close(fd);
        return (int)encode_errno(rc);
    }
    if (bind(fd, (const struct sockaddr*)&addr, sizeof(addr)) < 0) {
        rc = errno;
        close(fd);
        return (int)encode_errno(rc);
    }
    if (listen(fd, backlog > 0 ? backlog : 16) < 0) {
        rc = errno;
        close(fd);
        return (int)encode_errno(rc);
    }
    return fd;
}

long long __k_net_local_port(int fd) {
    if (fd < 0) {
        return -1LL;
    }
    struct sockaddr_in addr;
    socklen_t len = (socklen_t)sizeof(addr);
    if (getsockname(fd, (struct sockaddr*)&addr, &len) < 0) {
        return -1LL;
    }
    return (long long)ntohs(addr.sin_port);
}

long long __k_net_accept(int server_fd, long long timeout_nanos) {
    if (server_fd < 0) {
        return K_RES_CLOSED;
    }
    int64_t deadline = make_deadline((int64_t)timeout_nanos);
    for (;;) {
        long long rd = wait_fd_ready(server_fd, POLLIN, deadline);
        if (rd != 0) {
            return rd;
        }
#if defined(__linux__)
        int fd = accept4(server_fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
#else
        int fd = accept(server_fd, NULL, NULL);
#endif
        if (fd >= 0) {
#if !defined(__linux__)
            int rc = set_cloexec(fd);
            if (rc != 0) {
                close(fd);
                return encode_errno(rc);
            }
            rc = set_nonblocking(fd);
            if (rc != 0) {
                close(fd);
                return encode_errno(rc);
            }
#endif
            return (long long)fd;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            continue;
        }
        if (errno == EBADF) {
            return K_RES_CLOSED;
        }
        return encode_errno(errno);
    }
}

long long __k_net_recv(int fd, void* buf, int len, long long timeout_nanos) {
    if (fd < 0) {
        return K_RES_CLOSED;
    }
    if (buf == NULL || len < 0) {
        return encode_errno(EINVAL);
    }
    if (len == 0) {
        return 0;
    }
    int64_t deadline = make_deadline((int64_t)timeout_nanos);
    for (;;) {
        long long rd = wait_fd_ready(fd, POLLIN, deadline);
        if (rd != 0) {
            return rd;
        }
        ssize_t got = recv(fd, buf, (size_t)len, MSG_DONTWAIT);
        if (got >= 0) {
            return (long long)got;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            continue;
        }
        if (errno == EBADF) {
            return K_RES_CLOSED;
        }
        return encode_errno(errno);
    }
}

long long __k_net_send(int fd, const void* buf, int len, long long timeout_nanos) {
    if (fd < 0) {
        return K_RES_CLOSED;
    }
    if (buf == NULL || len < 0) {
        return encode_errno(EINVAL);
    }
    if (len == 0) {
        return 0;
    }
    int64_t deadline = make_deadline((int64_t)timeout_nanos);
    for (;;) {
        long long wr = wait_fd_ready(fd, POLLOUT, deadline);
        if (wr != 0) {
            return wr;
        }
        ssize_t sent = send(fd, buf, (size_t)len, MSG_DONTWAIT | MSG_NOSIGNAL);
        if (sent >= 0) {
            return (long long)sent;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            continue;
        }
        if (errno == EBADF) {
            return K_RES_CLOSED;
        }
        return encode_errno(errno);
    }
}

int __k_net_is_open(int fd) {
    if (fd < 0) {
        return 0;
    }
    int flags = fcntl(fd, F_GETFD, 0);
    return (flags >= 0 || errno != EBADF) ? 1 : 0;
}

int __k_net_close(int fd) {
    if (fd < 0) {
        return 0;
    }
    if (close(fd) == 0) {
        return 0;
    }
    return errno;
}
