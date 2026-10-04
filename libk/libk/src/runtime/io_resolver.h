/*
 * K Language runtime — Asynchronous I/O DNS & control-plane resolver pool
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

#ifndef KLANG_RUNTIME_IO_RESOLVER_H
#define KLANG_RUNTIME_IO_RESOLVER_H

#include "io_operation.h"

#include <stdint.h>
#include <stdbool.h>
#include <netdb.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct KIoResolver KIoResolver;
typedef int (*KIoResolverLookup)(const char*, const char*, const struct addrinfo*,
                                 struct addrinfo**);
/* Native deterministic fixtures install a lookup before admitting jobs. */
void k_io_resolver_set_test_lookup(KIoResolver* resolver, KIoResolverLookup lookup);

/**
 * Create a bounded resolver worker pool.
 * @param num_workers Number of worker threads (e.g. 2 to 4).
 * @param queue_capacity Maximum backlog of resolution requests (e.g. 128).
 */
KIoResolver* k_io_resolver_create(uint32_t num_workers, uint32_t queue_capacity);

/**
 * Submit an asynchronous DNS resolution job.
 * If cancelled before or during resolution, frees results and marks operation Cancelled.
 * @return 0 on success, -EBUSY / -ESHUTDOWN on failure.
 */
int k_io_resolver_submit_resolve(KIoResolver* resolver, KNativeIoOperation* op,
                                 const char* node, const char* service,
                                 const struct addrinfo* hints);

/**
 * Shut down the resolver pool: cancels accepted queued/running jobs, drains
 * their request storage and joins worker threads.
 */
void k_io_resolver_shutdown(KIoResolver* resolver);

/**
 * Destroy the resolver pool.
 */
void k_io_resolver_destroy(KIoResolver* resolver);
/* Reject new jobs and cancel accepted jobs without joining their workers. */
void k_io_resolver_stop_accepting(KIoResolver* resolver);
int k_io_resolver_submit_control(KIoResolver* resolver, KNativeIoOperation* op);

#ifdef __cplusplus
}
#endif

#endif /* KLANG_RUNTIME_IO_RESOLVER_H */
