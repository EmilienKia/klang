/*
 * K Language runtime — Thread-local storage runtime (C header)
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

#ifndef KLANG_RUNTIME_TLS_H
#define KLANG_RUNTIME_TLS_H

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*KTlsDtorFn)(void*);

/**
 * Register a destructor function to be called on thread exit for @p obj.
 * Destruction order is reverse of registration order (LIFO).
 */
void __k_tls_register_dtor(void* obj, KTlsDtorFn dtor);

/**
 * Run all registered thread-local destructors for the calling thread in LIFO order.
 * Safe to call multiple times (subsequent calls are no-ops).
 */
void __k_tls_thread_exit(void);

#ifdef __cplusplus
}
#endif

#endif // KLANG_RUNTIME_TLS_H
