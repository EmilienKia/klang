/*
 * K Language standard library — dynamic module loading FFI helpers
 *
 * Copyright 2026 Emilien Kia
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
 * C implementations of the K-side FFI helpers declared in application.k for
 * ::k::Application::loadModule().
 *
 * Paths cross the boundary as K UTF-8 byte arrays (struct { uint32_t size;
 * unsigned char data[]; }, size including the trailing NUL).
 *
 * Error codes returned through `*err` mirror the ::k::LoadModuleError enum
 * declared in module.k and MUST be kept in sync with it.
 */

#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t      size;
    unsigned char data[];
} KByteArray;

enum {
    K_MODULE_NOT_FOUND          = 0,
    K_MODULE_ACCESS_DENIED      = 1,
    K_MODULE_INVALID_PATH       = 2,
    K_MODULE_NOT_A_LIBRARY      = 3,
    K_MODULE_MISSING_DEPENDENCY = 4,
    K_MODULE_UNRESOLVED_SYMBOL  = 5,
    K_MODULE_LOAD_FAILED        = 6
};

/* Copy a K byte array into a freshly allocated, always NUL-terminated C string. */
static char* k_module_to_cstr(const KByteArray* arr) {
    size_t n = strnlen((const char*) arr->data, arr->size);
    char* s = (char*) malloc(n + 1);
    if (!s) abort();
    memcpy(s, arr->data, n);
    s[n] = '\0';
    return s;
}

/*
 * Resolve `path` to an absolute, symlink-free path.
 *
 * Corresponds to:
 *   @ffi::Extern("C") private __k_module_realpath(path: const unsigned byte[], err: int*) : unsigned byte[]!
 *
 * Returns an owned K byte array, or NULL with *err set.
 */
KByteArray* __k_module_realpath(const KByteArray* path, int* err) {
    char* in = k_module_to_cstr(path);
    char* resolved = realpath(in, NULL);
    free(in);
    if (!resolved) {
        switch (errno) {
        case ENOENT:
        case ENOTDIR:       *err = K_MODULE_NOT_FOUND;     break;
        case EACCES:
        case EPERM:         *err = K_MODULE_ACCESS_DENIED; break;
        case ENAMETOOLONG:
        case ELOOP:
        case EINVAL:        *err = K_MODULE_INVALID_PATH;  break;
        default:            *err = K_MODULE_LOAD_FAILED;   break;
        }
        return NULL;
    }
    size_t len = strlen(resolved);
    KByteArray* out = (KByteArray*) malloc(sizeof(uint32_t) + len + 1);
    if (!out) abort();
    out->size = (uint32_t) (len + 1);
    memcpy(out->data, resolved, len + 1);
    free(resolved);
    return out;
}

/* Classify a glibc dlerror() message. */
static int k_module_classify(const char* msg, const char* path) {
    if (!msg) return K_MODULE_LOAD_FAILED;
    size_t plen = strlen(path);
    int about_target = strncmp(msg, path, plen) == 0;

    if (strstr(msg, "undefined symbol") || strstr(msg, "not found (required by"))
        return K_MODULE_UNRESOLVED_SYMBOL;
    if (strstr(msg, "cannot open shared object file")) {
        if (!about_target) return K_MODULE_MISSING_DEPENDENCY;
        if (strstr(msg, "Permission denied")) return K_MODULE_ACCESS_DENIED;
        if (strstr(msg, "No such file")) return K_MODULE_NOT_FOUND;
        return K_MODULE_LOAD_FAILED;
    }
    if (strstr(msg, "invalid ELF header") || strstr(msg, "file too short")
        || strstr(msg, "not an ELF") || strstr(msg, "wrong ELF class")
        || strstr(msg, "ELF file OS ABI invalid") || strstr(msg, "cannot read file data")
        || strstr(msg, "cannot dynamically load"))
        return K_MODULE_NOT_A_LIBRARY;
    return K_MODULE_LOAD_FAILED;
}

/*
 * Load the shared library at the absolute path `path`: dependencies are loaded,
 * symbols are resolved immediately and global constructors have run on return.
 *
 * Corresponds to:
 *   @ffi::Extern("C") private __k_module_open(path: const unsigned byte[], err: int*) : byte*
 *
 * Returns the native handle (the same one for an already loaded object), or
 * NULL with *err set.
 */
void* __k_module_open(const KByteArray* path, int* err) {
    char* in = k_module_to_cstr(path);
    dlerror(); /* clear any stale state */
    void* handle = dlopen(in, RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        *err = k_module_classify(dlerror(), in);
    }
    free(in);
    return handle;
}
