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

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <dlfcn.h>
#include <errno.h>
#include <limits.h>
#include <link.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "rtti_manifest.h"

typedef struct {
    uint32_t      size;
    unsigned char data[];
} KByteArray;

typedef struct {
    char* path;
    void* handle;
    struct link_map* identity;
    const KRTTIManifestEntryV1* entries;
    uint32_t entry_count;
    int manifest_status;
} KModuleImage;

typedef struct {
    KModuleImage* images;
    size_t count;
} KModuleSnapshot;

typedef struct {
    char** names;
    size_t count;
    size_t capacity;
} KImageNames;

enum {
    K_MANIFEST_NATIVE = 0,
    K_MANIFEST_VALID = 1,
    K_MANIFEST_INCOMPATIBLE = 2,
    K_MANIFEST_MALFORMED = 3
};

static pthread_mutex_t k_module_registry_mutex = PTHREAD_MUTEX_INITIALIZER;

void __k_module_registry_lock(void) {
    int result = pthread_mutex_lock(&k_module_registry_mutex);
    if (result != 0) {
        fputs("K module registry lock failed\n", stderr);
        abort();
    }
}

void __k_module_registry_unlock(void) {
    int result = pthread_mutex_unlock(&k_module_registry_mutex);
    if (result != 0) {
        fputs("K module registry unlock failed\n", stderr);
        abort();
    }
}

enum {
    K_MODULE_NOT_FOUND          = 0,
    K_MODULE_ACCESS_DENIED      = 1,
    K_MODULE_INVALID_PATH       = 2,
    K_MODULE_NOT_A_LIBRARY      = 3,
    K_MODULE_MISSING_DEPENDENCY = 4,
    K_MODULE_UNRESOLVED_SYMBOL  = 5,
    K_MODULE_LOAD_FAILED        = 6,
    K_MODULE_INVALID_MANIFEST   = 7,
    K_MODULE_INCOMPATIBLE_MANIFEST = 8
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

/*
 * Return a process-local identity for the loaded image. A dlopen handle is
 * opaque and can be an alias; link_map is the loader's canonical image record.
 * Fall back to the handle on platforms without the Linux dlinfo extension.
 */
void* __k_module_identity(void* handle) {
#if defined(__linux__)
    struct link_map* image = NULL;
    if (handle && dlinfo(handle, RTLD_DI_LINKMAP, &image) == 0 && image) {
        return image;
    }
#endif
    return handle;
}

static int k_module_collect_name(struct dl_phdr_info* info, size_t size, void* data) {
    (void) size;
    KImageNames* names = (KImageNames*) data;
    const char* name = info->dlpi_name;
    if (name && name[0] && strncmp(name, "linux-vdso", 10) != 0) {
        if (names->count == names->capacity) {
            size_t capacity = names->capacity ? names->capacity * 2 : 16;
            char** next = (char**) realloc(names->names, capacity * sizeof(char*));
            if (!next) abort();
            names->names = next;
            names->capacity = capacity;
        }
        names->names[names->count] = strdup(name);
        if (!names->names[names->count]) abort();
        ++names->count;
    } else if (!name || !name[0]) {
        /* The main executable is always the first image in dl_iterate_phdr. */
        if (names->count == names->capacity) {
            size_t capacity = names->capacity ? names->capacity * 2 : 16;
            char** next = (char**) realloc(names->names, capacity * sizeof(char*));
            if (!next) abort();
            names->names = next;
            names->capacity = capacity;
        }
        names->names[names->count] = strdup("");
        if (!names->names[names->count]) abort();
        ++names->count;
    }
    return 0;
}

static int k_module_same_image(void* address, struct link_map* image) {
    if (!address || !image) return 0;
    Dl_info info;
    struct link_map* owner = NULL;
    return dladdr1(address, &info, (void**) &owner, RTLD_DL_LINKMAP) != 0
        && owner == image;
}

typedef struct {
    struct link_map* image;
    uintptr_t start;
    uintptr_t end;
    int found;
} KImageLoadRange;

static int k_module_find_load_range(struct dl_phdr_info* info, size_t size, void* data) {
    (void) size;
    KImageLoadRange* range = (KImageLoadRange*) data;
    if ((ElfW(Addr)) info->dlpi_addr != (ElfW(Addr)) range->image->l_addr) return 0;
    for (ElfW(Half) i = 0; i < info->dlpi_phnum; ++i) {
        const ElfW(Phdr)* phdr = &info->dlpi_phdr[i];
        if (phdr->p_type != PT_LOAD || phdr->p_memsz == 0) continue;
        uintptr_t begin = (uintptr_t) info->dlpi_addr + (uintptr_t) phdr->p_vaddr;
        if (begin > UINTPTR_MAX - (uintptr_t) phdr->p_memsz) continue;
        uintptr_t end = begin + (uintptr_t) phdr->p_memsz;
        if (range->start >= begin && range->end <= end) {
            range->found = 1;
            return 1;
        }
    }
    return 0;
}

static int k_module_range_in_image(const void* address, size_t length,
                                   struct link_map* image) {
    uintptr_t begin = (uintptr_t) address;
    if (!address || begin > UINTPTR_MAX - length) return 0;
    KImageLoadRange range = {
        .image = image,
        .start = begin,
        .end = begin + length,
        .found = 0
    };
    dl_iterate_phdr(k_module_find_load_range, &range);
    return range.found;
}

static void k_module_probe_manifest(KModuleImage* image) {
    dlerror();
    void* symbol = dlsym(image->handle, "__k_rtti_manifest_v1");
    const char* error = dlerror();
    if (error || !symbol) {
        image->manifest_status = K_MANIFEST_NATIVE;
        return;
    }

    /* dlsym(handle, ...) also searches DT_NEEDED dependencies. Never invoke a
       dependency's accessor as if it described the requested image. */
    if (!k_module_same_image(symbol, image->identity)) {
        image->manifest_status = K_MANIFEST_NATIVE;
        return;
    }

    const KRTTIManifestEntryV1* entries = NULL;
    uint32_t count = 0;
    int result = ((KRTTIManifestAccessorV1) symbol)(&entries, &count);
    if (result != 0 || (count && !entries) || count > 1000000u) {
        image->manifest_status = K_MANIFEST_MALFORMED;
        return;
    }
    if (count && (((uintptr_t) entries % _Alignof(KRTTIManifestEntryV1)) != 0
                  || (size_t) count > SIZE_MAX / sizeof(KRTTIManifestEntryV1)
                  || !k_module_range_in_image(
                      entries, (size_t) count * sizeof(KRTTIManifestEntryV1),
                      image->identity))) {
        image->manifest_status = K_MANIFEST_MALFORMED;
        return;
    }
    for (uint32_t i = 0; i < count; ++i) {
        const KRTTIManifestEntryV1* entry = &entries[i];
        if (!k_module_same_image((void*) entry, image->identity)
            || !k_module_same_image((void*) entry->unit_descriptor, image->identity)) {
            image->manifest_status = K_MANIFEST_MALFORMED;
            return;
        }
        if (entry->abi_version != K_RTTI_MANIFEST_ABI_V1) {
            image->manifest_status = K_MANIFEST_INCOMPATIBLE;
            return;
        }
        if (entry->entry_size != sizeof(KRTTIManifestEntryV1)) {
            image->manifest_status = K_MANIFEST_MALFORMED;
            return;
        }
        if (!entry->unit_descriptor) {
            image->manifest_status = K_MANIFEST_MALFORMED;
            return;
        }
    }
    image->entries = entries;
    image->entry_count = count;
    image->manifest_status = K_MANIFEST_VALID;
}

static const char* k_module_image_soname(struct link_map* image) {
    if (!image || !image->l_ld) return NULL;
    uintptr_t strings_address = 0;
    size_t string_size = 0;
    uintptr_t soname_offset = UINTPTR_MAX;
    size_t count = 0;
    for (ElfW(Dyn)* dyn = image->l_ld; dyn->d_tag != DT_NULL && count < 65536;
         ++dyn, ++count) {
        if (dyn->d_tag == DT_STRTAB) {
            uintptr_t base = (uintptr_t) image->l_addr;
            uintptr_t offset = (uintptr_t) dyn->d_un.d_ptr;
            if (offset > UINTPTR_MAX - base) return NULL;
            strings_address = base + offset;
        }
        if (dyn->d_tag == DT_STRSZ) string_size = (size_t) dyn->d_un.d_val;
        if (dyn->d_tag == DT_SONAME) soname_offset = (uintptr_t) dyn->d_un.d_val;
    }
    if (!strings_address || !string_size || soname_offset >= string_size
        || !k_module_range_in_image((const void*) strings_address, string_size, image)) {
        return NULL;
    }
    const char* strings = (const char*) strings_address;
    const char* soname = strings + soname_offset;
    if (!memchr(soname, '\0', string_size - soname_offset)) return NULL;
    return soname;
}

static const char* k_module_basename(const char* path) {
    if (!path) return "";
    const char* slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static struct link_map* k_module_match_needed(struct link_map* requester,
                                               const char* needed) {
    if (!requester || !needed) return NULL;
    struct link_map* root = requester;
    while (root->l_prev) root = root->l_prev;
    for (struct link_map* candidate = root; candidate; candidate = candidate->l_next) {
        if (candidate == requester) continue;
        const char* soname = k_module_image_soname(candidate);
        if ((soname && strcmp(soname, needed) == 0)
            || strcmp(k_module_basename(candidate->l_name), needed) == 0) {
            return candidate;
        }
    }
    return NULL;
}

static struct link_map* k_module_dependency_at(struct link_map* requester, int index) {
    if (!requester || index < 0 || !requester->l_ld) return NULL;
    const char* strings = NULL;
    size_t string_size = 0;
    for (ElfW(Dyn)* dyn = requester->l_ld; dyn->d_tag != DT_NULL; ++dyn) {
        if (dyn->d_tag == DT_STRTAB) strings = (const char*) dyn->d_un.d_ptr;
        if (dyn->d_tag == DT_STRSZ) string_size = (size_t) dyn->d_un.d_val;
    }
    if (!strings || !string_size) return NULL;

    int found = 0;
    size_t count = 0;
    for (ElfW(Dyn)* dyn = requester->l_ld; dyn->d_tag != DT_NULL && count < 65536;
         ++dyn, ++count) {
        if (dyn->d_tag != DT_NEEDED) continue;
        uintptr_t offset = (uintptr_t) dyn->d_un.d_val;
        if (offset >= string_size) continue;
        const char* needed = strings + offset;
        if (!memchr(needed, '\0', string_size - offset)) continue;
        struct link_map* dependency = k_module_match_needed(requester, needed);
        if (dependency && found++ == index) return dependency;
    }
    return NULL;
}

int __k_module_dependency_count(void* identity) {
    int count = 0;
    while (count < 65536 && k_module_dependency_at((struct link_map*) identity, count)) {
        ++count;
    }
    return count;
}

void* __k_module_dependency_at(void* identity, int index) {
    return k_module_dependency_at((struct link_map*) identity, index);
}

int __k_module_depends_on(void* identity, void* candidate_identity) {
    struct link_map* requester = (struct link_map*) identity;
    struct link_map* candidate = (struct link_map*) candidate_identity;
    if (!requester || !candidate) return 0;

    struct link_map* root = requester;
    while (root->l_prev) root = root->l_prev;
    size_t capacity = 0;
    for (struct link_map* image = root; image; image = image->l_next) ++capacity;
    if (capacity == 0 || capacity > SIZE_MAX / sizeof(struct link_map*)) return 0;
    struct link_map** seen = (struct link_map**) malloc(capacity * sizeof(struct link_map*));
    if (!seen) abort();
    size_t count = 1;
    seen[0] = requester;
    int found = 0;
    for (size_t i = 0; i < count && !found; ++i) {
        for (int dependency_index = 0; dependency_index < 65536; ++dependency_index) {
            struct link_map* dependency = k_module_dependency_at(seen[i], dependency_index);
            if (!dependency) break;
            if (dependency == candidate) {
                found = 1;
                break;
            }
            size_t j = 0;
            while (j < count && seen[j] != dependency) ++j;
            if (j == count && count < capacity) seen[count++] = dependency;
        }
    }
    free(seen);
    return found;
}

/*
 * Capture a process-local snapshot of loaded ELF images. Snapshot records own
 * NOLOAD handles until closed or explicitly transferred to a Module.
 */
void* __k_module_snapshot_open(void) {
    KImageNames names = {0};
    dl_iterate_phdr(k_module_collect_name, &names);

    KModuleSnapshot* snapshot = (KModuleSnapshot*) calloc(1, sizeof(KModuleSnapshot));
    if (!snapshot) abort();
    snapshot->images = (KModuleImage*) calloc(names.count, sizeof(KModuleImage));
    if (!snapshot->images && names.count) abort();

    for (size_t i = 0; i < names.count; ++i) {
        KModuleImage* image = &snapshot->images[snapshot->count];
        const char* name = names.names[i];
        image->handle = name[0]
            ? dlopen(name, RTLD_NOW | RTLD_NOLOAD | RTLD_LOCAL)
            : dlopen(NULL, RTLD_NOW);
        if (!image->handle || dlinfo(image->handle, RTLD_DI_LINKMAP, &image->identity) != 0
            || !image->identity) {
            if (image->handle) dlclose(image->handle);
            free(names.names[i]);
            continue;
        }

        char* resolved = name[0] ? realpath(name, NULL) : NULL;
        if (!resolved && !name[0]) {
            resolved = realpath("/proc/self/exe", NULL);
        }
        image->path = resolved ? resolved : strdup(name);
        if (!image->path) abort();
        k_module_probe_manifest(image);
        ++snapshot->count;
        free(names.names[i]);
    }
    free(names.names);
    return snapshot;
}

int __k_module_snapshot_count(void* opaque) {
    KModuleSnapshot* snapshot = (KModuleSnapshot*) opaque;
    return snapshot ? (int) snapshot->count : 0;
}

void* __k_module_snapshot_identity(void* opaque, int index) {
    KModuleSnapshot* snapshot = (KModuleSnapshot*) opaque;
    if (!snapshot || index < 0 || (size_t) index >= snapshot->count) return NULL;
    return snapshot->images[index].identity;
}

void* __k_module_snapshot_take_handle(void* opaque, int index) {
    KModuleSnapshot* snapshot = (KModuleSnapshot*) opaque;
    if (!snapshot || index < 0 || (size_t) index >= snapshot->count) return NULL;
    void* handle = snapshot->images[index].handle;
    snapshot->images[index].handle = NULL;
    return handle;
}

int __k_module_snapshot_manifest_status(void* opaque, int index) {
    KModuleSnapshot* snapshot = (KModuleSnapshot*) opaque;
    if (!snapshot || index < 0 || (size_t) index >= snapshot->count) return K_MANIFEST_MALFORMED;
    return snapshot->images[index].manifest_status;
}

int __k_module_snapshot_unit_count(void* opaque, int image_index) {
    KModuleSnapshot* snapshot = (KModuleSnapshot*) opaque;
    if (!snapshot || image_index < 0 || (size_t) image_index >= snapshot->count) return 0;
    return (int) snapshot->images[image_index].entry_count;
}

void* __k_module_snapshot_unit_at(void* opaque, int image_index, int unit_index) {
    KModuleSnapshot* snapshot = (KModuleSnapshot*) opaque;
    if (!snapshot || image_index < 0 || (size_t) image_index >= snapshot->count) return NULL;
    KModuleImage* image = &snapshot->images[image_index];
    if (unit_index < 0 || (uint32_t) unit_index >= image->entry_count) return NULL;
    return (void*) image->entries[unit_index].unit_descriptor;
}

KByteArray* __k_module_snapshot_path(void* opaque, int index) {
    KModuleSnapshot* snapshot = (KModuleSnapshot*) opaque;
    if (!snapshot || index < 0 || (size_t) index >= snapshot->count) return NULL;
    const char* path = snapshot->images[index].path;
    size_t length = strlen(path) + 1;
    KByteArray* result = (KByteArray*) malloc(sizeof(uint32_t) + length);
    if (!result) abort();
    result->size = (uint32_t) length;
    memcpy(result->data, path, length);
    return result;
}

void __k_module_snapshot_close(void* opaque) {
    KModuleSnapshot* snapshot = (KModuleSnapshot*) opaque;
    if (!snapshot) return;
    for (size_t i = 0; i < snapshot->count; ++i) {
        KModuleImage* image = &snapshot->images[i];
        if (image->handle) dlclose(image->handle);
        free(image->path);
    }
    free(snapshot->images);
    free(snapshot);
}

void __k_module_close(void* handle) {
    if (handle) dlclose(handle);
}

const void* __k_rtti_unit_ref(void* descriptor) {
    return descriptor;
}

const void* __k_rtti_namespace_ref(void* descriptor) {
    return descriptor;
}

void* __k_rtti_namespace_ptr(const void* descriptor) {
    return (void*) descriptor;
}

typedef struct {
    uint32_t size;
    const void* data[];
} KNamespaceArray;

uint32_t __k_rtti_namespace_count(const KNamespaceArray* descriptors) {
    return descriptors ? descriptors->size : 0;
}

const void* __k_rtti_namespace_at(const KNamespaceArray* descriptors, uint32_t index) {
    if (!descriptors || index >= descriptors->size) return NULL;
    return descriptors->data[index];
}

/*
 * Return the canonical path of the main executable as a K byte array. Linux
 * exposes this path through /proc/self/exe; the runtime is currently gated to
 * the POSIX/Linux module-loading backend.
 */
KByteArray* __k_module_current_path(void) {
    size_t capacity = 256;
    char* path = NULL;
    for (;;) {
        char* next = (char*) realloc(path, capacity + 1);
        if (!next) abort();
        path = next;
        ssize_t count = readlink("/proc/self/exe", path, capacity);
        if (count < 0) {
            free(path);
            return NULL;
        }
        if ((size_t) count < capacity) {
            path[count] = '\0';
            break;
        }
        if (capacity > UINT32_MAX / 2) {
            free(path);
            return NULL;
        }
        capacity *= 2;
    }

    size_t length = strlen(path);
    KByteArray* result = (KByteArray*) malloc(sizeof(uint32_t) + length + 1);
    if (!result) abort();
    result->size = (uint32_t) (length + 1);
    memcpy(result->data, path, length + 1);
    free(path);
    return result;
}
