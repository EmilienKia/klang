/*
 * K Language runtime — bounded DNS and optional control-plane pool
 * Copyright 2023-2026 Emilien Kia
 * Licensed under the Apache License, Version 2.0.
 */
#define _GNU_SOURCE
#include "io_resolver.h"
#include "io_runtime.h"
#include "runtime_thread.h"
#include "park_lot.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <netinet/in.h>

typedef struct KResolverJob {
    KNativeIoOperation* op;
    char* node;
    char* service;
    struct addrinfo hints;
    bool control;
    KIoResolverLookup lookup;
    struct KResolverJob* next;
} KResolverJob;
struct KIoResolver {
    KRuntimeThread** workers;
    unsigned worker_count;
    KResolverJob* queue;
    unsigned capacity;
    unsigned head;
    unsigned count;
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    bool accepting;
    bool stopping;
    KIoResolverLookup lookup;
    KResolverJob* running;
};

static int control_result(KNativeIoOperation* op) {
    int rc;
    switch (op->opcode) {
        case K_IO_OP_OPENAT:
            rc = openat(op->fd, op->buf, op->flags, (mode_t)op->offset);
            break;
        case K_IO_OP_FSYNC:
            rc = op->flags ? fdatasync(op->fd) : fsync(op->fd);
            break;
        case K_IO_OP_FTRUNCATE:
            rc = ftruncate(op->fd, op->offset);
            break;
        case K_IO_OP_STATX:
            rc = statx(op->fd, op->buf, op->flags, (unsigned)op->offset,
                       (struct statx*)op->sockaddr_storage);
            break;
        default: return -ENOTSUP;
    }
    return rc < 0 ? -errno : rc;
}

static int process_job(KResolverJob* job) {
    KNativeIoOperation* op = job->op;
    int result = 0;
    if (k_io_operation_is_cancelled(op)) result = -ECANCELED;
    else if (op->deadline_nanos > 0 && k_monotonic_nanos() >= op->deadline_nanos)
        result = -ETIMEDOUT;
    else if (job->control) {
        result = control_result(op);
        if (result >= 0 && op->deadline_nanos > 0 &&
            k_monotonic_nanos() >= op->deadline_nanos) {
            if (op->opcode == K_IO_OP_OPENAT) close(result);
            result = -ETIMEDOUT;
        }
    }
    else {
        struct addrinfo* addresses = NULL;
        int rc = job->lookup(job->node, job->service, &job->hints, &addresses);
        if (rc) {
            if (rc == EAI_SYSTEM) result = -(errno ? errno : EIO);
            else if (rc == EAI_MEMORY) result = -ENOMEM;
            else if (rc == EAI_AGAIN) result = -EAGAIN;
            else if (rc == EAI_NONAME) result = -ENOENT;
            else if (rc == EAI_FAMILY) result = -EAFNOSUPPORT;
            else if (rc == EAI_BADFLAGS || rc == EAI_SERVICE || rc == EAI_SOCKTYPE) result = -EINVAL;
            else result = -EHOSTUNREACH;
        }
        else if (!addresses || !addresses->ai_addr) result = -EHOSTUNREACH;
        else if (addresses->ai_addrlen > sizeof(struct sockaddr_storage) ||
                 (job->hints.ai_family == AF_INET &&
                  (addresses->ai_addr->sa_family != AF_INET ||
                   addresses->ai_addrlen < sizeof(struct sockaddr_in)))) result = -EAFNOSUPPORT;
        else if (k_io_operation_is_cancelled(op)) result = -ECANCELED;
        else if (op->deadline_nanos > 0 && k_monotonic_nanos() >= op->deadline_nanos)
            result = -ETIMEDOUT;
        else {
            void* copy = malloc(addresses->ai_addrlen);
            if (!copy) result = -ENOMEM;
            else {
                memcpy(copy, addresses->ai_addr, addresses->ai_addrlen);
                free(op->sockaddr_storage);
                op->sockaddr_storage = copy;
                op->sockaddr_len = addresses->ai_addrlen;
            }
        }
        if (addresses) freeaddrinfo(addresses);
    }
    free(job->node);
    free(job->service);
    return result;
}
static void resolver_worker(void* arg) {
    k_io_runtime_enter_worker();
    KIoResolver* r = arg;
    for (;;) {
        pthread_mutex_lock(&r->mutex);
        while (!r->count && !r->stopping) pthread_cond_wait(&r->changed, &r->mutex);
        if (!r->count && r->stopping) {
            pthread_mutex_unlock(&r->mutex);
            return;
        }
        KResolverJob job = r->queue[r->head];
        r->head = (r->head + 1) % r->capacity;
        --r->count;
        job.next = r->running;
        r->running = &job;
        pthread_mutex_unlock(&r->mutex);
        int result = process_job(&job);
        /* Resolver allocations are freed before completion. Keep the accepted
         * job cancellable through publication/registry admission so shutdown
         * cannot miss the boundary between the resolver and the kernel. */
        if (!job.control && (job.op->opcode == K_IO_OP_CONNECT ||
                            job.op->opcode == K_IO_OP_SENDMSG))
            k_io_runtime_connect_resolved(job.op, result);
        else k_io_operation_on_kernel_completion(job.op, result);
        pthread_mutex_lock(&r->mutex);
        KResolverJob** link = &r->running;
        while (*link != &job) link = &(*link)->next;
        *link = job.next;
        pthread_mutex_unlock(&r->mutex);
        k_io_operation_release(job.op);
    }
}
KIoResolver* k_io_resolver_create(uint32_t workers, uint32_t capacity) {
    if (!workers) workers = 2;
    if (!capacity) capacity = 128;
    KIoResolver* r = calloc(1, sizeof(*r));
    if (!r) return NULL;
    r->workers = calloc(workers, sizeof(*r->workers));
    r->queue = calloc(capacity, sizeof(*r->queue));
    if (!r->workers || !r->queue) {
        free(r->workers);
        free(r->queue);
        free(r);
        return NULL;
    }
    r->capacity = capacity;
    r->accepting = true;
    r->lookup = getaddrinfo;
    pthread_mutex_init(&r->mutex, NULL);
    pthread_cond_init(&r->changed, NULL);
    for (unsigned i = 0; i < workers; ++i) {
        r->workers[i] = k_runtime_thread_create(resolver_worker, r);
        if (!r->workers[i] || k_runtime_thread_start(r->workers[i])) {
            k_runtime_thread_destroy(r->workers[i]);
            k_io_resolver_destroy(r);
            return NULL;
        }
        ++r->worker_count;
    }
    return r;
}
static int submit_job(KIoResolver* r, KResolverJob* job) {
    if (!r) return -ESHUTDOWN;
    pthread_mutex_lock(&r->mutex);
    if (!r->accepting || r->count == r->capacity) {
        int rc = r->accepting ? -EBUSY : -ESHUTDOWN;
        pthread_mutex_unlock(&r->mutex);
        return rc;
    }
    k_io_operation_retain(job->op);
    job->lookup = r->lookup;
    r->queue[(r->head + r->count) % r->capacity] = *job;
    ++r->count;
    pthread_cond_signal(&r->changed);
    pthread_mutex_unlock(&r->mutex);
    return 0;
}
int k_io_resolver_submit_resolve(KIoResolver* r, KNativeIoOperation* op,
                                 const char* node, const char* service,
                                 const struct addrinfo* hints) {
    KResolverJob job = { .op = op };
    if (node) job.node = strdup(node);
    if (service) job.service = strdup(service);
    if ((node && !job.node) || (service && !job.service)) {
        free(job.node);
        free(job.service);
        return -ENOMEM;
    }
    if (hints) {
        job.hints.ai_family = hints->ai_family;
        job.hints.ai_socktype = hints->ai_socktype;
        job.hints.ai_protocol = hints->ai_protocol;
        job.hints.ai_flags = hints->ai_flags;
    } else {
        job.hints.ai_family = AF_UNSPEC;
        job.hints.ai_socktype = SOCK_STREAM;
    }
    int rc = submit_job(r, &job);
    if (rc < 0) {
        free(job.node);
        free(job.service);
    }
    return rc;
}
int k_io_resolver_submit_control(KIoResolver* r, KNativeIoOperation* op) {
    switch (op->opcode) {
        case K_IO_OP_OPENAT: case K_IO_OP_STATX: case K_IO_OP_FSYNC: case K_IO_OP_FTRUNCATE:
            break;
        default: return -ENOTSUP;
    }
    KResolverJob job = { .op = op, .control = true };
    return submit_job(r, &job);
}
void k_io_resolver_stop_accepting(KIoResolver* r) {
    if (!r) return;
    pthread_mutex_lock(&r->mutex);
    r->accepting = false;
    for (unsigned i = 0; i < r->count; ++i)
        k_io_operation_cancel(r->queue[(r->head + i) % r->capacity].op);
    for (KResolverJob* job = r->running; job; job = job->next)
        k_io_operation_cancel(job->op);
    pthread_mutex_unlock(&r->mutex);
}
void k_io_resolver_set_test_lookup(KIoResolver* r, KIoResolverLookup lookup) {
    pthread_mutex_lock(&r->mutex);
    r->lookup = lookup ? lookup : getaddrinfo;
    pthread_mutex_unlock(&r->mutex);
}
void k_io_resolver_shutdown(KIoResolver* r) {
    if (!r) return;
    k_io_resolver_stop_accepting(r);
    pthread_mutex_lock(&r->mutex);
    r->accepting = false;
    r->stopping = true;
    /* Accepted jobs are drained on registered workers, not on the caller. */
    pthread_cond_broadcast(&r->changed);
    pthread_mutex_unlock(&r->mutex);
    for (unsigned i = 0; i < r->worker_count; ++i) {
        k_runtime_thread_destroy(r->workers[i]);
        r->workers[i] = NULL;
    }
    r->worker_count = 0;
}
void k_io_resolver_destroy(KIoResolver* r) {
    if (!r) return;
    k_io_resolver_shutdown(r);
    pthread_cond_destroy(&r->changed);
    pthread_mutex_destroy(&r->mutex);
    free(r->workers);
    free(r->queue);
    free(r);
}
