/*
 * K Language standard library — Application FFI helpers
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
 * C implementations of the K-side FFI helpers declared in application.k.
 *
 * These helpers operate on the raw K array representation so that the
 * expensive K constructors (String, TreeMap) can be invoked from K code
 * rather than from C, keeping the ABI boundary thin and safe.
 *
 * K unsized-array layout (all element types):
 *   struct KArray {
 *       uint32_t size;       // number of elements
 *       T        data[];     // flexible array member
 *   };
 *
 * This layout is guaranteed by the K compiler (it is the LLVM
 * { i32, [0 x T] } trailing-array struct).
 */

#define _GNU_SOURCE

#include <dlfcn.h>
#include <link.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Extern declaration for the C environ array. */
extern char** environ;

typedef struct {
    uint32_t abi_version;
    uint32_t struct_size;
    _Atomic(void*) active_application;
    _Atomic(uint32_t) active_jit_host_count;
} KApplicationAnchorV1;

enum {
    K_APPLICATION_ABI_VERSION = 1,
    K_APPLICATION_OK = 0,
    K_APPLICATION_NO_PROVIDER = 1,
    K_APPLICATION_INCOMPATIBLE_PROVIDER = 2,
    K_APPLICATION_WRONG_PROVIDER_IMAGE = 3,
    K_APPLICATION_ALREADY_ACTIVE = 4,
    K_APPLICATION_NOT_ACTIVE = 5
};

typedef KApplicationAnchorV1* (*KApplicationAnchorFn)(void);

void __k_application_activation_error(int status);

static KApplicationAnchorV1 k_application_runtime_anchor = {
    .abi_version = K_APPLICATION_ABI_VERSION,
    .struct_size = sizeof(KApplicationAnchorV1),
    .active_application = ATOMIC_VAR_INIT(NULL),
    .active_jit_host_count = ATOMIC_VAR_INIT(0)
};

static KApplicationAnchorV1* resolve_application_anchor(
    int* status, int allow_unregistered_jit_host);

__attribute__((visibility("default")))
KApplicationAnchorV1* __k_application_runtime_anchor_v1(void) {
    return &k_application_runtime_anchor;
}

__attribute__((visibility("default")))
int __k_application_jit_host_enter(void) {
    int status = K_APPLICATION_OK;
    KApplicationAnchorV1* anchor = resolve_application_anchor(&status, 1);
    if (!anchor) return status;
    atomic_fetch_add_explicit(&anchor->active_jit_host_count, 1, memory_order_release);
    return K_APPLICATION_OK;
}

__attribute__((visibility("default")))
void __k_application_jit_host_leave(void) {
    int status = K_APPLICATION_OK;
    KApplicationAnchorV1* anchor = resolve_application_anchor(&status, 1);
    if (!anchor) {
        __k_application_activation_error(status);
        abort();
    }
    uint32_t count = atomic_load_explicit(&anchor->active_jit_host_count, memory_order_acquire);
    while (count != 0 && !atomic_compare_exchange_weak_explicit(
        &anchor->active_jit_host_count, &count, count - 1,
        memory_order_acq_rel, memory_order_acquire)) {
    }
    if (count == 0) {
        __k_application_activation_error(K_APPLICATION_NOT_ACTIVE);
        abort();
    }
}

static int symbol_owner(void* symbol, struct link_map** owner) {
    Dl_info info;
    struct link_map* map = NULL;
    if (!symbol || !dladdr1(symbol, &info, (void**)&map, RTLD_DL_LINKMAP) || !map) {
        return 0;
    }
    *owner = map;
    return 1;
}

static struct link_map* main_image(void) {
    void* handle = dlopen(NULL, RTLD_NOW);
    if (!handle) return NULL;
    struct link_map* map = NULL;
    if (dlinfo(handle, RTLD_DI_LINKMAP, &map) != 0) map = NULL;
    dlclose(handle);
    return map;
}

static int is_libk_runtime_image(const struct link_map* image) {
    if (!image || !image->l_name) return 0;
    const char* basename = strrchr(image->l_name, '/');
    basename = basename ? basename + 1 : image->l_name;
    return strncmp(basename, "libk.so", sizeof("libk.so") - 1) == 0 &&
        (basename[sizeof("libk.so") - 1] == '\0' ||
         basename[sizeof("libk.so") - 1] == '.');
}

static int is_libk_runtime_name(const char* name) {
    if (!name || !*name) return 0;
    const char* basename = strrchr(name, '/');
    basename = basename ? basename + 1 : name;
    return strncmp(basename, "libk.so", sizeof("libk.so") - 1) == 0 &&
        (basename[sizeof("libk.so") - 1] == '\0' ||
         basename[sizeof("libk.so") - 1] == '.');
}

typedef struct {
    char** names;
    size_t count;
    size_t capacity;
    int allocation_failed;
} runtime_image_names;

static void free_runtime_image_names(runtime_image_names* names) {
    for (size_t i = 0; i < names->count; ++i) free(names->names[i]);
    free(names->names);
}

static int collect_runtime_image_name(struct dl_phdr_info* info, size_t size, void* data) {
    (void)size;
    runtime_image_names* names = data;
    if (!is_libk_runtime_name(info->dlpi_name)) return 0;

    if (names->count == names->capacity) {
        size_t capacity = names->capacity ? names->capacity * 2 : 4;
        char** expanded = realloc(names->names, capacity * sizeof(*expanded));
        if (!expanded) {
            names->allocation_failed = 1;
            return 1;
        }
        names->names = expanded;
        names->capacity = capacity;
    }
    names->names[names->count] = strdup(info->dlpi_name);
    if (!names->names[names->count]) {
        names->allocation_failed = 1;
        return 1;
    }
    ++names->count;
    return 0;
}

static KApplicationAnchorV1* call_anchor(void* symbol, struct link_map* expected_owner, int* status) {
    struct link_map* symbol_map = NULL;
    if (!symbol_owner(symbol, &symbol_map) || symbol_map != expected_owner) {
        *status = K_APPLICATION_WRONG_PROVIDER_IMAGE;
        return NULL;
    }

    KApplicationAnchorFn anchor_fn = (KApplicationAnchorFn)symbol;
    KApplicationAnchorV1* anchor = anchor_fn();
    if (!anchor) {
        *status = K_APPLICATION_INCOMPATIBLE_PROVIDER;
        return NULL;
    }
    struct link_map* anchor_map = NULL;
    if (!symbol_owner(anchor, &anchor_map) || anchor_map != expected_owner) {
        *status = K_APPLICATION_WRONG_PROVIDER_IMAGE;
        return NULL;
    }
    if (anchor->abi_version != K_APPLICATION_ABI_VERSION ||
        anchor->struct_size != sizeof(KApplicationAnchorV1)) {
        *status = K_APPLICATION_INCOMPATIBLE_PROVIDER;
        return NULL;
    }
    return anchor;
}

static KApplicationAnchorV1* resolve_application_anchor(int* status, int allow_unregistered_jit_host) {
    struct link_map* executable = main_image();
    if (!executable) {
        *status = K_APPLICATION_INCOMPATIBLE_PROVIDER;
        return NULL;
    }

    dlerror();
    void* executable_symbol = dlsym(RTLD_DEFAULT, "__k_application_anchor_v1");
    const char* executable_error = dlerror();
    if (!executable_error && executable_symbol) {
        KApplicationAnchorV1* anchor = call_anchor(executable_symbol, executable, status);
        if (!anchor) {
            return NULL;
        }
        *status = K_APPLICATION_OK;
        return anchor;
    }

    /* Enumerate under the loader's synchronization, then acquire NOLOAD handles
     * only after the callback has released the loader lock. */
    runtime_image_names runtime_images = {0};
    dl_iterate_phdr(collect_runtime_image_name, &runtime_images);
    if (runtime_images.allocation_failed) {
        free_runtime_image_names(&runtime_images);
        *status = K_APPLICATION_INCOMPATIBLE_PROVIDER;
        return NULL;
    }

    int advertised_runtime_provider = runtime_images.count != 0;
    for (size_t i = 0; i < runtime_images.count; ++i) {
        void* runtime_handle = dlopen(runtime_images.names[i], RTLD_NOW | RTLD_NOLOAD);
        free(runtime_images.names[i]);
        runtime_images.names[i] = NULL;
        if (!runtime_handle) continue;

        struct link_map* runtime_image = NULL;
        if (dlinfo(runtime_handle, RTLD_DI_LINKMAP, &runtime_image) != 0 ||
            !is_libk_runtime_image(runtime_image)) {
            dlclose(runtime_handle);
            continue;
        }

        dlerror();
        void* runtime_symbol = dlsym(runtime_handle, "__k_application_runtime_anchor_v1");
        const char* runtime_error = dlerror();
        if (runtime_error || !runtime_symbol) {
            dlclose(runtime_handle);
            continue;
        }
        advertised_runtime_provider = 1;

        int provider_status = K_APPLICATION_OK;
        KApplicationAnchorV1* anchor = call_anchor(runtime_symbol, runtime_image, &provider_status);
        dlclose(runtime_handle);
        if (!anchor) {
            free_runtime_image_names(&runtime_images);
            *status = provider_status;
            return NULL;
        }
        if (!allow_unregistered_jit_host &&
            atomic_load_explicit(&anchor->active_jit_host_count, memory_order_acquire) == 0) {
            free_runtime_image_names(&runtime_images);
            *status = K_APPLICATION_NO_PROVIDER;
            return NULL;
        }
        free_runtime_image_names(&runtime_images);
        *status = K_APPLICATION_OK;
        return anchor;
    }
    free_runtime_image_names(&runtime_images);
    *status = advertised_runtime_provider
        ? K_APPLICATION_INCOMPATIBLE_PROVIDER
        : K_APPLICATION_NO_PROVIDER;
    return NULL;
}

void* __k_application_current(void) {
    int status = K_APPLICATION_OK;
    KApplicationAnchorV1* anchor = resolve_application_anchor(&status, 0);
    if (status == K_APPLICATION_NO_PROVIDER) return NULL;
    if (status != K_APPLICATION_OK) {
        fprintf(stderr, "K Application singleton provider is invalid (status %d)\n", status);
        abort();
    }
    return atomic_load_explicit(&anchor->active_application, memory_order_acquire);
}

int __k_application_activate(void* application) {
    if (!application) return K_APPLICATION_INCOMPATIBLE_PROVIDER;
    int status = K_APPLICATION_OK;
    KApplicationAnchorV1* anchor = resolve_application_anchor(&status, 0);
    if (!anchor) return status;

    void* expected = NULL;
    if (!atomic_compare_exchange_strong_explicit(&anchor->active_application, &expected, application,
            memory_order_release, memory_order_acquire)) {
        return K_APPLICATION_ALREADY_ACTIVE;
    }
    return K_APPLICATION_OK;
}

int __k_application_deactivate(void* application) {
    if (!application) return K_APPLICATION_NOT_ACTIVE;
    int status = K_APPLICATION_OK;
    KApplicationAnchorV1* anchor = resolve_application_anchor(&status, 0);
    if (!anchor) {
        __k_application_activation_error(status);
        return status;
    }

    void* expected = application;
    if (!atomic_compare_exchange_strong_explicit(&anchor->active_application, &expected, NULL,
            memory_order_acq_rel, memory_order_acquire)) {
        __k_application_activation_error(K_APPLICATION_NOT_ACTIVE);
        return K_APPLICATION_NOT_ACTIVE;
    }
    return K_APPLICATION_OK;
}

void __k_application_activation_error(int status) {
    const char* message = "unknown activation failure";
    switch (status) {
        case K_APPLICATION_NO_PROVIDER:
            message = "no canonical Application provider is available";
            break;
        case K_APPLICATION_INCOMPATIBLE_PROVIDER:
            message = "the Application provider has an incompatible ABI";
            break;
        case K_APPLICATION_WRONG_PROVIDER_IMAGE:
            message = "the Application provider is not owned by the host image";
            break;
        case K_APPLICATION_ALREADY_ACTIVE:
            message = "another Application is already active";
            break;
        case K_APPLICATION_NOT_ACTIVE:
            message = "the active Application does not match the object being withdrawn";
            break;
    }
    fprintf(stderr, "K Application singleton activation failed: %s (status %d)\n", message, status);
}

/* ── K array helpers ─────────────────────────────────────────────────────── */

/*
 * K unsized-byte-array struct mirroring the LLVM { i32, [0 x i8] } layout.
 * The single uint32_t field is followed immediately by the byte data with no
 * padding (both fields have alignment ≤ 4 bytes on every supported target).
 */
typedef struct {
    uint32_t      size;
    unsigned char data[];
} KByteArray;

/* ── __k_cstr_at ─────────────────────────────────────────────────────────── */

/*
 * Return argv[i] as an owned, heap-allocated K unsigned-byte array.
 *
 * Corresponds to:
 *   @ffi::Extern("C") private __k_cstr_at(argv: const byte**, i: int): unsigned byte[]!
 *
 * The returned array does NOT include a null terminator; K arrays carry an
 * explicit size field.  Ownership of the returned pointer is transferred to
 * the K runtime (it will be freed by the K array destructor).
 */
KByteArray* __k_cstr_at(const char* argv_opaque, int i) {
    const char** argv = (const char**) argv_opaque;
    const char*  s   = argv[i];
    size_t       len = strlen(s);
    /* +1 for the trailing null terminator: K's String(const unsigned byte[])
     * constructor treats `size` as including a trailing '\0' (see toUtf8() in
     * string.k, which always allocates n+1 bytes for the same reason). */
    KByteArray*  arr = (KByteArray*) malloc(sizeof(uint32_t) + len + 1);
    if (!arr) abort();
    arr->size = (uint32_t) (len + 1);
    memcpy(arr->data, s, len);
    arr->data[len] = '\0';
    return arr;
}

/* ── Environment-variable helpers ────────────────────────────────────────── */

/*
 * Return the number of entries in the process environment.
 *
 * Corresponds to:
 *   @ffi::Extern("C") private __k_env_count(): int
 */
int __k_env_count(void) {
    int n = 0;
    if (environ) {
        while (environ[n]) ++n;
    }
    return n;
}

/*
 * Build an owned K byte array from a segment [start, end) of a C string.
 * Internal helper used by __k_env_key_at and __k_env_value_at.
 *
 * The returned array is null-terminated with `size` including the trailing
 * '\0', matching the convention expected by K's String(const unsigned
 * byte[]) constructor (see toUtf8() in string.k).
 */
static KByteArray* make_byte_array(const char* start, size_t len) {
    KByteArray* arr = (KByteArray*) malloc(sizeof(uint32_t) + len + 1);
    if (!arr) abort();
    arr->size = (uint32_t) (len + 1);
    memcpy(arr->data, start, len);
    arr->data[len] = '\0';
    return arr;
}

/*
 * Return environ[i]'s key (the part before '=') as an owned K byte array.
 *
 * Corresponds to:
 *   @ffi::Extern("C") private __k_env_key_at(i: int): unsigned byte[]!
 */
KByteArray* __k_env_key_at(int i) {
    const char* entry = environ[i];
    const char* eq    = strchr(entry, '=');
    size_t      len   = eq ? (size_t)(eq - entry) : strlen(entry);
    return make_byte_array(entry, len);
}

/*
 * Return environ[i]'s value (the part after '=') as an owned K byte array.
 *
 * Corresponds to:
 *   @ffi::Extern("C") private __k_env_value_at(i: int): unsigned byte[]!
 */
KByteArray* __k_env_value_at(int i) {
    const char* entry = environ[i];
    const char* eq    = strchr(entry, '=');
    if (!eq) {
        /* Entry without '=': empty value. */
        return make_byte_array("", 0);
    }
    const char* val = eq + 1;
    return make_byte_array(val, strlen(val));
}
