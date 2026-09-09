/*
 * K Language runtime — Thread-local storage runtime (C implementation)
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
#include "runtime_tls.h"

#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>

typedef struct KTlsDtorEntry {
    KTlsDtorFn            dtor;
    void*                 obj;
    struct KTlsDtorEntry* prev;
} KTlsDtorEntry;

static _Thread_local KTlsDtorEntry* k_tls_dtor_stack = NULL;
static _Thread_local bool            k_tls_finalizing  = false;

static pthread_key_t  g_tls_cleanup_key;
static pthread_once_t g_tls_key_once = PTHREAD_ONCE_INIT;

static void tls_pthread_destructor(void* val) {
    (void)val;
    __k_tls_thread_exit();
}

static void make_tls_cleanup_key(void) {
    pthread_key_create(&g_tls_cleanup_key, tls_pthread_destructor);
    atexit(__k_tls_thread_exit);
}

void __k_tls_register_dtor(void* obj, KTlsDtorFn dtor) {
    if (!obj || !dtor) return;
    if (k_tls_finalizing) {
        return;
    }

    pthread_once(&g_tls_key_once, make_tls_cleanup_key);
    if (pthread_getspecific(g_tls_cleanup_key) == NULL) {
        pthread_setspecific(g_tls_cleanup_key, (void*)1);
    }

    KTlsDtorEntry* entry = (KTlsDtorEntry*)malloc(sizeof(KTlsDtorEntry));
    if (!entry) return;
    entry->dtor = dtor;
    entry->obj  = obj;
    entry->prev = k_tls_dtor_stack;
    k_tls_dtor_stack = entry;
}

void __k_tls_thread_exit(void) {
    if (k_tls_finalizing) return;
    k_tls_finalizing = true;

    while (k_tls_dtor_stack) {
        KTlsDtorEntry* cur = k_tls_dtor_stack;
        k_tls_dtor_stack = cur->prev;

        if (cur->dtor && cur->obj) {
            cur->dtor(cur->obj);
        }
        free(cur);
    }

    k_tls_finalizing = false;
}
