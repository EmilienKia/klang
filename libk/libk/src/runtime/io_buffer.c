/*
 * K Language runtime — Native I/O buffer and mapped memory substrate
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
#include "io_buffer.h"
#include "io_runtime.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/resource.h>

/* ── UTF-32 (K char) → UTF-8 Path Transcoding ───────────────────────────── */

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

/* ── Sparse Buffer Table ────────────────────────────────────────────────── */

#define K_SPARSE_BUFFER_TABLE_SIZE 256

typedef struct KSparseSlot {
    void*  base;
    size_t len;
    bool   in_use;
    KNativeByteBuffer* buffer;
    bool owns_buffer; /* failed unregister keeps abandoned storage alive */
} KSparseSlot;

typedef struct KSparseBufferTable {
    pthread_mutex_t mutex;
    KSparseSlot     slots[K_SPARSE_BUFFER_TABLE_SIZE];
    uint32_t        active_count;
    bool            test_force_table_full;
} KSparseBufferTable;

static KSparseBufferTable g_sparse_table = {
    .mutex                 = PTHREAD_MUTEX_INITIALIZER,
    .active_count          = 0,
    .test_force_table_full = false
};

void k_io_buffer_test_set_table_full(bool full) {
    pthread_mutex_lock(&g_sparse_table.mutex);
    g_sparse_table.test_force_table_full = full;
    pthread_mutex_unlock(&g_sparse_table.mutex);
}

size_t k_io_buffer_get_registered_count(void) {
    pthread_mutex_lock(&g_sparse_table.mutex);
    size_t c = (size_t)g_sparse_table.active_count;
    pthread_mutex_unlock(&g_sparse_table.mutex);
    return c;
}

/* ── Internal Destroy Routine ───────────────────────────────────────────── */

static void report_unregister_failure(int index, int result) {
    fprintf(stderr, "K runtime: buffer %d unregister failed during teardown: %s (%d); "
            "backend teardown releases the registration\n", index, strerror(-result), -result);
}

static void k_io_buffer_destroy_internal(KNativeByteBuffer* buf) {
    if (buf == NULL) {
        return;
    }

    if (buf->is_registered) {
        int idx = buf->fixed_index;
        if (idx >= 0 && idx < K_SPARSE_BUFFER_TABLE_SIZE) {
            pthread_mutex_lock(&g_sparse_table.mutex);
            if (g_sparse_table.slots[idx].in_use) {
                int rc = k_io_runtime_unregister_buffer(idx);
                if (rc < 0 && k_io_runtime_is_active()) {
                    atomic_store(&buf->ref_count, 1u);
                    g_sparse_table.slots[idx].owns_buffer = true;
                    pthread_mutex_unlock(&g_sparse_table.mutex);
                    return;
                }
                if (rc < 0) report_unregister_failure(idx, rc);
                g_sparse_table.slots[idx].in_use = false;
                g_sparse_table.slots[idx].base   = NULL;
                g_sparse_table.slots[idx].len    = 0;
                g_sparse_table.slots[idx].buffer = NULL;
                if (g_sparse_table.active_count > 0) {
                    g_sparse_table.active_count--;
                }
                k_io_runtime_metric_buffer_unregistered();
            }
            pthread_mutex_unlock(&g_sparse_table.mutex);
        }
        buf->is_registered = false;
        buf->fixed_index   = -1;
    }

    if (buf->storage_type == K_BUFFER_MAPPED) {
        if (buf->map_base != NULL && buf->map_len > 0) {
            munmap(buf->map_base, buf->map_len);
            buf->map_base = NULL;
        }
        if (buf->fd >= 0) {
            close(buf->fd);
            buf->fd = -1;
        }
    } else if (buf->storage_type == K_BUFFER_DIRECT) {
        if (buf->raw_alloc != NULL) {
            free(buf->raw_alloc);
            buf->raw_alloc = NULL;
        } else if (buf->data != NULL) {
            free(buf->data);
            buf->data = NULL;
        }
    } else if (buf->storage_type == K_BUFFER_HEAP) {
        if (buf->data != NULL) {
            free(buf->data);
            buf->data = NULL;
        }
    }

    pthread_mutex_destroy(&buf->lock);
    free(buf);
}

/* ── Creation & Allocation ──────────────────────────────────────────────── */

KNativeByteBuffer* k_io_buffer_allocate(size_t capacity) {
    KNativeByteBuffer* buf = (KNativeByteBuffer*)calloc(1, sizeof(KNativeByteBuffer));
    if (buf == NULL) {
        return NULL;
    }

    size_t alloc_sz = (capacity > 0) ? capacity : 1;
    buf->data = (uint8_t*)calloc(1, alloc_sz);
    if (buf->data == NULL) {
        free(buf);
        return NULL;
    }

    atomic_store(&buf->ref_count, 1u);
    buf->creator_pid = getpid();
    atomic_store(&buf->lease_count, 0u);
    buf->storage_type        = K_BUFFER_HEAP;
    buf->capacity            = capacity;
    buf->position            = 0;
    buf->limit               = capacity;
    buf->raw_alloc           = NULL;
    buf->map_base            = NULL;
    buf->map_len             = 0;
    buf->map_mode            = 0;
    buf->fd                  = -1;
    buf->is_registered       = false;
    buf->fixed_index         = -1;
    buf->unregister_deferred = false;

    pthread_mutex_init(&buf->lock, NULL);
    return buf;
}

KNativeByteBuffer* k_io_buffer_allocate_direct(size_t capacity) {
    KNativeByteBuffer* buf = (KNativeByteBuffer*)calloc(1, sizeof(KNativeByteBuffer));
    if (buf == NULL) {
        return NULL;
    }

    long ps = sysconf(_SC_PAGESIZE);
    size_t page_size = (ps > 0) ? (size_t)ps : 4096;
    size_t alloc_sz  = (capacity > 0) ? capacity : 1;

    void* ptr = NULL;
    int rc = posix_memalign(&ptr, page_size, alloc_sz);
    if (rc != 0 || ptr == NULL) {
        free(buf);
        return NULL;
    }

    memset(ptr, 0, alloc_sz);

    atomic_store(&buf->ref_count, 1u);
    buf->creator_pid = getpid();
    atomic_store(&buf->lease_count, 0u);
    buf->storage_type        = K_BUFFER_DIRECT;
    buf->data                = (uint8_t*)ptr;
    buf->raw_alloc           = ptr;
    buf->capacity            = capacity;
    buf->position            = 0;
    buf->limit               = capacity;
    buf->map_base            = NULL;
    buf->map_len             = 0;
    buf->map_mode            = 0;
    buf->fd                  = -1;
    buf->is_registered       = false;
    buf->fixed_index         = -1;
    buf->unregister_deferred = false;

    pthread_mutex_init(&buf->lock, NULL);
    return buf;
}

KNativeByteBuffer* k_io_buffer_map(int fd, int64_t offset, size_t length, KMapMode mode) {
    if (length == 0 || offset < 0 || length > UINT32_MAX) {
        return NULL;
    }

    int prot = 0;
    int flags = 0;

    switch (mode) {
        case K_MAP_READ_ONLY:
            prot  = PROT_READ;
            flags = MAP_SHARED;
            break;
        case K_MAP_READ_WRITE:
            prot  = PROT_READ | PROT_WRITE;
            flags = MAP_SHARED;
            break;
        case K_MAP_PRIVATE:
            prot  = PROT_READ | PROT_WRITE;
            flags = MAP_PRIVATE;
            break;
        default:
            return NULL;
    }

    if (fd < 0) {
        flags |= MAP_ANONYMOUS;
    }

    long ps = sysconf(_SC_PAGESIZE);
    size_t page_size = (ps > 0) ? (size_t)ps : 4096;

    size_t page_offset   = (size_t)(offset % (int64_t)page_size);
    off_t aligned_offset = (off_t)(offset - (int64_t)page_offset);
    size_t map_len       = length + page_offset;
    if (map_len < length) return NULL;
    if (fd >= 0) {
        struct stat info;
        if (fstat(fd, &info) < 0 || offset > info.st_size ||
            length > (uint64_t)(info.st_size - offset)) return NULL;
    }

    void* map_base = mmap(NULL, map_len, prot, flags, fd, aligned_offset);
    if (map_base == MAP_FAILED) {
        return NULL;
    }

    KNativeByteBuffer* buf = (KNativeByteBuffer*)calloc(1, sizeof(KNativeByteBuffer));
    if (buf == NULL) {
        munmap(map_base, map_len);
        return NULL;
    }

    atomic_store(&buf->ref_count, 1u);
    buf->creator_pid = getpid();
    atomic_store(&buf->lease_count, 0u);
    buf->storage_type        = K_BUFFER_MAPPED;
    buf->map_base            = map_base;
    buf->map_len             = map_len;
    buf->data                = (uint8_t*)map_base + page_offset;
    buf->capacity            = length;
    buf->position            = 0;
    buf->limit               = length;
    buf->raw_alloc           = NULL;
    buf->map_mode            = (int)mode;
    buf->fd                  = -1;
    buf->is_registered       = false;
    buf->fixed_index         = -1;
    buf->unregister_deferred = false;

    pthread_mutex_init(&buf->lock, NULL);

    /* Automatic sparse registration attempt (fallback non-fixed on failure) */
    k_io_buffer_register(buf);

    return buf;
}

KNativeByteBuffer* k_io_buffer_map_path(const char* path, int64_t offset, size_t length, KMapMode mode) {
    if (path == NULL || length == 0 || offset < 0) {
        return NULL;
    }

    int oflags = (mode == K_MAP_READ_WRITE) ? O_RDWR : O_RDONLY;
    int fd = open(path, oflags | O_CLOEXEC);
    if (fd < 0) {
        return NULL;
    }

    KNativeByteBuffer* buf = k_io_buffer_map(fd, offset, length, mode);
    /* In POSIX, fd can be closed once mmap succeeds */
    close(fd);
    return buf;
}

/* ── Lifecycle & Refcounting ────────────────────────────────────────────── */

void k_io_buffer_retain(KNativeByteBuffer* buf) {
    if (buf != NULL) {
        atomic_fetch_add_explicit(&buf->ref_count, 1u, memory_order_relaxed);
    }
}

void k_io_buffer_release(KNativeByteBuffer* buf) {
    if (buf == NULL || buf->creator_pid != getpid()) {
        return;
    }

    if (atomic_fetch_sub_explicit(&buf->ref_count, 1u, memory_order_acq_rel) == 1u)
        k_io_buffer_destroy_internal(buf);
}

/* ── Lease Management ───────────────────────────────────────────────────── */

void k_io_buffer_acquire_lease(void* b) {
    KNativeByteBuffer* buf = (KNativeByteBuffer*)b;
    if (buf != NULL && buf->creator_pid == getpid()) {
        pthread_mutex_lock(&buf->lock);
        k_io_buffer_retain(buf); /* every kernel lease is a strong native ref */
        atomic_fetch_add_explicit(&buf->lease_count, 1u, memory_order_relaxed);
        pthread_mutex_unlock(&buf->lock);
    }
}

void k_io_buffer_release_lease(void* b) {
    KNativeByteBuffer* buf = (KNativeByteBuffer*)b;
    if (buf == NULL || buf->creator_pid != getpid()) {
        return;
    }

    pthread_mutex_lock(&buf->lock);
    if (atomic_load(&buf->lease_count) == 0u) {
        pthread_mutex_unlock(&buf->lock);
        return;
    }
    uint32_t prev_leases = atomic_fetch_sub(&buf->lease_count, 1u);
    if (prev_leases == 1u) {
        /* Leases reached zero */
        if (buf->unregister_deferred) {
            int idx = buf->fixed_index;
            if (idx >= 0 && idx < K_SPARSE_BUFFER_TABLE_SIZE) {
                pthread_mutex_lock(&g_sparse_table.mutex);
                if (g_sparse_table.slots[idx].in_use) {
                    if (k_io_runtime_unregister_buffer(idx) == 0) {
                        g_sparse_table.slots[idx] = (KSparseSlot){0};
                        buf->unregister_deferred = false;
                        buf->is_registered = false;
                        buf->fixed_index = -1;
                        if (g_sparse_table.active_count > 0) {
                            g_sparse_table.active_count--;
                        }
                        k_io_runtime_metric_buffer_unregistered();
                    }
                }
                pthread_mutex_unlock(&g_sparse_table.mutex);
            }
        }

    }
    pthread_mutex_unlock(&buf->lock);
    k_io_buffer_release(buf);
}

bool k_io_buffer_has_lease(const KNativeByteBuffer* buf) {
    if (buf == NULL) {
        return false;
    }
    return atomic_load(&buf->lease_count) > 0u;
}

uint32_t k_io_buffer_get_lease_count(const KNativeByteBuffer* buf) {
    if (buf == NULL) {
        return 0u;
    }
    return atomic_load(&buf->lease_count);
}

/* ── Geometry & Cursor ──────────────────────────────────────────────────── */

size_t k_io_buffer_get_capacity(const KNativeByteBuffer* buf) {
    return (buf != NULL) ? buf->capacity : 0;
}

size_t k_io_buffer_get_position(const KNativeByteBuffer* buf) {
    if (buf == NULL || buf->creator_pid != getpid()) {
        return 0;
    }
    pthread_mutex_lock((pthread_mutex_t*)&buf->lock);
    size_t pos = buf->position;
    pthread_mutex_unlock((pthread_mutex_t*)&buf->lock);
    return pos;
}

size_t k_io_buffer_get_limit(const KNativeByteBuffer* buf) {
    if (buf == NULL || buf->creator_pid != getpid()) {
        return 0;
    }
    pthread_mutex_lock((pthread_mutex_t*)&buf->lock);
    size_t lim = buf->limit;
    pthread_mutex_unlock((pthread_mutex_t*)&buf->lock);
    return lim;
}

int k_io_buffer_set_position(KNativeByteBuffer* buf, size_t new_pos) {
    if (buf == NULL || buf->creator_pid != getpid()) {
        return -EINVAL;
    }

    pthread_mutex_lock(&buf->lock);
    if (atomic_load(&buf->lease_count) > 0u) {
        pthread_mutex_unlock(&buf->lock);
        return -EBUSY; /* BufferInUse */
    }

    if (new_pos > buf->limit) {
        pthread_mutex_unlock(&buf->lock);
        return -ERANGE; /* IndexOutOfBounds */
    }

    buf->position = new_pos;
    pthread_mutex_unlock(&buf->lock);
    return 0;
}

int k_io_buffer_set_limit(KNativeByteBuffer* buf, size_t new_limit) {
    if (buf == NULL || buf->creator_pid != getpid()) {
        return -EINVAL;
    }

    pthread_mutex_lock(&buf->lock);
    if (atomic_load(&buf->lease_count) > 0u) {
        pthread_mutex_unlock(&buf->lock);
        return -EBUSY; /* BufferInUse */
    }

    if (new_limit > buf->capacity) {
        pthread_mutex_unlock(&buf->lock);
        return -ERANGE; /* IndexOutOfBounds */
    }

    buf->limit = new_limit;
    if (buf->position > buf->limit) {
        buf->position = buf->limit;
    }
    pthread_mutex_unlock(&buf->lock);
    return 0;
}

int k_io_buffer_flip(KNativeByteBuffer* buf) {
    if (buf == NULL || buf->creator_pid != getpid()) {
        return -EINVAL;
    }

    pthread_mutex_lock(&buf->lock);
    if (atomic_load(&buf->lease_count) > 0u) {
        pthread_mutex_unlock(&buf->lock);
        return -EBUSY; /* BufferInUse */
    }

    buf->limit    = buf->position;
    buf->position = 0;
    pthread_mutex_unlock(&buf->lock);
    return 0;
}

int k_io_buffer_clear(KNativeByteBuffer* buf) {
    if (buf == NULL || buf->creator_pid != getpid()) {
        return -EINVAL;
    }

    pthread_mutex_lock(&buf->lock);
    if (atomic_load(&buf->lease_count) > 0u) {
        pthread_mutex_unlock(&buf->lock);
        return -EBUSY; /* BufferInUse */
    }

    buf->position = 0;
    buf->limit    = buf->capacity;
    pthread_mutex_unlock(&buf->lock);
    return 0;
}

int k_io_buffer_rewind(KNativeByteBuffer* buf) {
    if (buf == NULL || buf->creator_pid != getpid()) {
        return -EINVAL;
    }

    pthread_mutex_lock(&buf->lock);
    if (atomic_load(&buf->lease_count) > 0u) {
        pthread_mutex_unlock(&buf->lock);
        return -EBUSY; /* BufferInUse */
    }

    buf->position = 0;
    pthread_mutex_unlock(&buf->lock);
    return 0;
}

int k_io_buffer_compact(KNativeByteBuffer* buf) {
    if (buf == NULL || buf->creator_pid != getpid()) {
        return -EINVAL;
    }

    pthread_mutex_lock(&buf->lock);
    if (buf->storage_type == K_BUFFER_MAPPED && buf->map_mode == K_MAP_READ_ONLY) {
        pthread_mutex_unlock(&buf->lock);
        return -EACCES;
    }
    if (atomic_load(&buf->lease_count) > 0u) {
        pthread_mutex_unlock(&buf->lock);
        return -EBUSY; /* BufferInUse */
    }

    if (buf->position > 0 && buf->position < buf->limit) {
        size_t rem = buf->limit - buf->position;
        memmove(buf->data, buf->data + buf->position, rem);
        buf->position = rem;
    } else if (buf->position >= buf->limit) {
        buf->position = 0;
    }

    buf->limit = buf->capacity;
    pthread_mutex_unlock(&buf->lock);
    return 0;
}

void k_io_buffer_advance_position(void* b, size_t n) {
    KNativeByteBuffer* buf = (KNativeByteBuffer*)b;
    if (buf == NULL || n == 0) {
        return;
    }

    pthread_mutex_lock(&buf->lock);
    size_t new_pos = buf->position + n;
    if (new_pos > buf->limit) {
        new_pos = buf->limit;
    }
    buf->position = new_pos;
    pthread_mutex_unlock(&buf->lock);
}

/* ── Data Access ────────────────────────────────────────────────────────── */

uint8_t* k_io_buffer_data_at(KNativeByteBuffer* buf, size_t index) {
    if (buf == NULL || buf->creator_pid != getpid() || buf->data == NULL) {
        return NULL;
    }
    if (index > buf->capacity || k_io_buffer_has_lease(buf)) {
        return NULL;
    }
    return buf->data ? buf->data + index : NULL;
}

int k_io_buffer_get_byte(const KNativeByteBuffer* buf, size_t index, uint8_t* out_byte) {
    if (buf == NULL || buf->creator_pid != getpid() || out_byte == NULL) {
        return -EINVAL;
    }

    pthread_mutex_lock((pthread_mutex_t*)&buf->lock);
    if (atomic_load(&buf->lease_count)) {
        pthread_mutex_unlock((pthread_mutex_t*)&buf->lock);
        return -EBUSY;
    }
    if (index >= buf->limit) {
        pthread_mutex_unlock((pthread_mutex_t*)&buf->lock);
        return -ERANGE;
    }
    *out_byte = buf->data[index];
    pthread_mutex_unlock((pthread_mutex_t*)&buf->lock);
    return 0;
}

int k_io_buffer_put_byte(KNativeByteBuffer* buf, size_t index, uint8_t b) {
    if (buf == NULL || buf->creator_pid != getpid()) {
        return -EINVAL;
    }

    pthread_mutex_lock(&buf->lock);
    if (buf->storage_type == K_BUFFER_MAPPED && buf->map_mode == K_MAP_READ_ONLY) {
        pthread_mutex_unlock(&buf->lock);
        return -EACCES;
    }
    if (atomic_load(&buf->lease_count) > 0u) {
        pthread_mutex_unlock(&buf->lock);
        return -EBUSY; /* BufferInUse */
    }

    if (index >= buf->limit) {
        pthread_mutex_unlock(&buf->lock);
        return -ERANGE;
    }

    buf->data[index] = b;
    pthread_mutex_unlock(&buf->lock);
    return 0;
}

int k_io_buffer_copy_from(KNativeByteBuffer* buf, size_t pos, const uint8_t* src, size_t len) {
    if (buf == NULL || buf->creator_pid != getpid() || src == NULL) {
        return -EINVAL;
    }
    if (len == 0) {
        return 0;
    }

    pthread_mutex_lock(&buf->lock);
    if (buf->storage_type == K_BUFFER_MAPPED && buf->map_mode == K_MAP_READ_ONLY) {
        pthread_mutex_unlock(&buf->lock);
        return -EACCES;
    }
    if (atomic_load(&buf->lease_count) > 0u) {
        pthread_mutex_unlock(&buf->lock);
        return -EBUSY; /* BufferInUse */
    }

    if (pos > buf->limit || len > buf->limit - pos) {
        pthread_mutex_unlock(&buf->lock);
        return -ERANGE;
    }

    if (len) memcpy(buf->data + pos, src, len);
    pthread_mutex_unlock(&buf->lock);
    return 0;
}

int k_io_buffer_copy_to(const KNativeByteBuffer* buf, size_t pos, uint8_t* dst, size_t len) {
    if (buf == NULL || buf->creator_pid != getpid() || dst == NULL) {
        return -EINVAL;
    }
    if (len == 0) {
        return 0;
    }

    pthread_mutex_lock((pthread_mutex_t*)&buf->lock);
    if (atomic_load(&buf->lease_count)) {
        pthread_mutex_unlock((pthread_mutex_t*)&buf->lock);
        return -EBUSY;
    }
    if (pos > buf->limit || len > buf->limit - pos) {
        pthread_mutex_unlock((pthread_mutex_t*)&buf->lock);
        return -ERANGE;
    }

    if (len) memcpy(dst, buf->data + pos, len);
    pthread_mutex_unlock((pthread_mutex_t*)&buf->lock);
    return 0;
}

/* ── Registration ───────────────────────────────────────────────────────── */

bool k_io_buffer_is_registered(const KNativeByteBuffer* buf) {
    if (buf == NULL || buf->creator_pid != getpid()) {
        return false;
    }
    pthread_mutex_lock((pthread_mutex_t*)&buf->lock);
    bool r = buf->is_registered;
    pthread_mutex_unlock((pthread_mutex_t*)&buf->lock);
    return r;
}

bool k_io_buffer_register(KNativeByteBuffer* buf) {
    if (buf == NULL || buf->creator_pid != getpid() || buf->data == NULL || buf->capacity == 0) {
        return false;
    }

    pthread_mutex_lock(&buf->lock);
    if (buf->is_registered) {
        pthread_mutex_unlock(&buf->lock);
        return true;
    }

    /* RLIMIT_MEMLOCK check */
    struct rlimit rl;
    if (getrlimit(RLIMIT_MEMLOCK, &rl) == 0) {
        if (rl.rlim_cur != RLIM_INFINITY && (uint64_t)buf->capacity > (uint64_t)rl.rlim_cur) {
            /* Memlock pressure -> fallback non-fixed without invalidating buffer */
            pthread_mutex_unlock(&buf->lock);
            return false;
        }
    }

    pthread_mutex_lock(&g_sparse_table.mutex);
    if (g_sparse_table.test_force_table_full ||
        g_sparse_table.active_count >= K_SPARSE_BUFFER_TABLE_SIZE) {
        /* Table full -> fallback non-fixed */
        pthread_mutex_unlock(&g_sparse_table.mutex);
        pthread_mutex_unlock(&buf->lock);
        return false;
    }

    int slot_idx = k_io_runtime_register_buffer(buf->data, buf->capacity);
    if (slot_idx < 0) {
        pthread_mutex_unlock(&g_sparse_table.mutex);
        pthread_mutex_unlock(&buf->lock);
        return false;
    }

    g_sparse_table.slots[slot_idx].base   = buf->data;
    g_sparse_table.slots[slot_idx].len    = buf->capacity;
    g_sparse_table.slots[slot_idx].in_use = true;
    g_sparse_table.slots[slot_idx].buffer = buf;
    g_sparse_table.slots[slot_idx].owns_buffer = false;
    g_sparse_table.active_count++;

    buf->is_registered       = true;
    buf->fixed_index         = slot_idx;
    buf->unregister_deferred = false;

    k_io_runtime_metric_buffer_registered();
    pthread_mutex_unlock(&g_sparse_table.mutex);
    pthread_mutex_unlock(&buf->lock);
    return true;
}

bool k_io_buffer_unregister(KNativeByteBuffer* buf) {
    if (buf == NULL || buf->creator_pid != getpid()) {
        return false;
    }

    pthread_mutex_lock(&buf->lock);
    if (!buf->is_registered) {
        pthread_mutex_unlock(&buf->lock);
        return true;
    }

    if (atomic_load(&buf->lease_count) > 0u) {
        /* Deferred unregistration until lease count is 0 */
        buf->unregister_deferred = true;
        pthread_mutex_unlock(&buf->lock);
        return true;
    }

    int idx = buf->fixed_index;
    if (idx >= 0 && idx < K_SPARSE_BUFFER_TABLE_SIZE) {
        pthread_mutex_lock(&g_sparse_table.mutex);
        if (g_sparse_table.slots[idx].in_use) {
            if (k_io_runtime_unregister_buffer(idx) < 0) {
                pthread_mutex_unlock(&g_sparse_table.mutex);
                pthread_mutex_unlock(&buf->lock);
                return false;
            }
            g_sparse_table.slots[idx].in_use = false;
            g_sparse_table.slots[idx].base   = NULL;
            g_sparse_table.slots[idx].len    = 0;
            g_sparse_table.slots[idx].buffer = NULL;
            if (g_sparse_table.active_count > 0) {
                g_sparse_table.active_count--;
            }
            k_io_runtime_metric_buffer_unregistered();
        }
        pthread_mutex_unlock(&g_sparse_table.mutex);
    }
    buf->is_registered       = false;
    buf->fixed_index         = -1;
    buf->unregister_deferred = false;
    pthread_mutex_unlock(&buf->lock);
    return true;
}

/* ── Mapped Operations ──────────────────────────────────────────────────── */

int k_io_buffer_force(KNativeByteBuffer* buf) {
    if (buf == NULL || buf->creator_pid != getpid()) {
        return -EINVAL;
    }

    pthread_mutex_lock(&buf->lock);
    if (buf->storage_type != K_BUFFER_MAPPED || buf->map_base == NULL) {
        pthread_mutex_unlock(&buf->lock);
        return -EINVAL;
    }
    void*  base = buf->map_base;
    size_t len  = buf->map_len;
    pthread_mutex_unlock(&buf->lock);

    int rc = msync(base, len, MS_SYNC);
    return (rc == 0) ? 0 : -errno;
}

int k_io_buffer_load(KNativeByteBuffer* buf) {
    if (buf == NULL || buf->creator_pid != getpid()) {
        return -EINVAL;
    }

    pthread_mutex_lock(&buf->lock);
    if (buf->storage_type != K_BUFFER_MAPPED || buf->map_base == NULL) {
        pthread_mutex_unlock(&buf->lock);
        return -EINVAL;
    }
    void*  base = buf->map_base;
    size_t len  = buf->map_len;
    pthread_mutex_unlock(&buf->lock);

    madvise(base, len, MADV_WILLNEED);

    long ps = sysconf(_SC_PAGESIZE);
    size_t page_size = (ps > 0) ? (size_t)ps : 4096;

    volatile uint8_t dummy = 0;
    for (size_t i = 0; i < len; i += page_size) {
        dummy += ((const volatile uint8_t*)base)[i];
    }
    (void)dummy;
    return 0;
}

bool k_io_buffer_is_loaded(const KNativeByteBuffer* buf) {
    if (buf == NULL || buf->creator_pid != getpid()) {
        return false;
    }

    pthread_mutex_lock((pthread_mutex_t*)&buf->lock);
    if (buf->storage_type != K_BUFFER_MAPPED || buf->map_base == NULL) {
        pthread_mutex_unlock((pthread_mutex_t*)&buf->lock);
        return false;
    }
    void*  base = buf->map_base;
    size_t len  = buf->map_len;
    pthread_mutex_unlock((pthread_mutex_t*)&buf->lock);

    long ps = sysconf(_SC_PAGESIZE);
    size_t page_size = (ps > 0) ? (size_t)ps : 4096;
    size_t pages = (len + page_size - 1) / page_size;

    unsigned char* vec = (unsigned char*)malloc(pages);
    if (vec == NULL) {
        return false;
    }

    if (mincore(base, len, vec) != 0) {
        free(vec);
        return false;
    }

    bool all_loaded = true;
    for (size_t i = 0; i < pages; ++i) {
        if ((vec[i] & 1) == 0) {
            all_loaded = false;
            break;
        }
    }

    free(vec);
    return all_loaded;
}

/* ── FFI Bridge Wrappers (__k_io_buffer_*) ──────────────────────────────── */

void* __k_io_buffer_allocate(unsigned int capacity) {
    return (void*)k_io_buffer_allocate((size_t)capacity);
}

void* __k_io_buffer_allocate_direct(unsigned int capacity) {
    return (void*)k_io_buffer_allocate_direct((size_t)capacity);
}

void* __k_io_buffer_map(const uint32_t* k_path, int fd, int64_t offset, int64_t length, int mode) {
    if (length <= 0 || length > UINT32_MAX) return NULL;
    if (k_path != NULL) {
        char pbuf[4096];
        if (k_utf32_to_utf8(k_path, pbuf, sizeof(pbuf)) < 0) return NULL;
        return (void*)k_io_buffer_map_path(pbuf, offset, (size_t)length, (KMapMode)mode);
    }
    return (void*)k_io_buffer_map(fd, offset, (size_t)length, (KMapMode)mode);
}

int k_io_buffer_fixed_index(const KNativeByteBuffer* buf) {
    return buf && atomic_load(&buf->is_registered) ? atomic_load(&buf->fixed_index) : -1;
}

void k_io_buffer_shutdown_registration(void) {
    KNativeByteBuffer* abandoned[K_SPARSE_BUFFER_TABLE_SIZE];
    unsigned abandoned_count = 0;
    pthread_mutex_lock(&g_sparse_table.mutex);
    for (int i = 0; i < K_SPARSE_BUFFER_TABLE_SIZE; ++i) {
        KSparseSlot* slot = &g_sparse_table.slots[i];
        if (!slot->in_use) continue;
        int rc = k_io_runtime_unregister_buffer(i);
        if (rc < 0) report_unregister_failure(i, rc);
        if (slot->owns_buffer) abandoned[abandoned_count++] = slot->buffer;
        atomic_store(&slot->buffer->is_registered, false);
        atomic_store(&slot->buffer->fixed_index, -1);
        *slot = (KSparseSlot){0};
        k_io_runtime_metric_buffer_unregistered();
    }
    g_sparse_table.active_count = 0;
    pthread_mutex_unlock(&g_sparse_table.mutex);
    for (unsigned i = 0; i < abandoned_count; ++i) k_io_buffer_release(abandoned[i]);
}

static int k_io_buffer_relative(void* native, const uint8_t* src, uint8_t* dst,
                                unsigned int length, bool write) {
    KNativeByteBuffer* buf = native;
    if (!buf || buf->creator_pid != getpid() || (length && (write ? !src : !dst))) return -EINVAL;
    pthread_mutex_lock(&buf->lock);
    int rc = 0;
    if (atomic_load(&buf->lease_count)) rc = -EBUSY;
    else if (write && buf->storage_type == K_BUFFER_MAPPED &&
             buf->map_mode == K_MAP_READ_ONLY) rc = -EACCES;
    else if (length > buf->limit - buf->position) rc = -ERANGE;
    else {
        if (length) {
            if (write) memcpy(buf->data + buf->position, src, length);
            else memcpy(dst, buf->data + buf->position, length);
        }
        buf->position += length;
    }
    pthread_mutex_unlock(&buf->lock);
    return rc;
}

int __k_io_buffer_get_relative(void* native, uint8_t* dst, unsigned int length) {
    return k_io_buffer_relative(native, NULL, dst, length, false);
}

int __k_io_buffer_put_relative(void* native, const uint8_t* src, unsigned int length) {
    return k_io_buffer_relative(native, src, NULL, length, true);
}

void __k_io_buffer_retain(void* buf) {
    k_io_buffer_retain((KNativeByteBuffer*)buf);
}

void __k_io_buffer_release(void* buf) {
    k_io_buffer_release((KNativeByteBuffer*)buf);
}

void __k_io_buffer_acquire_lease(void* buf) {
    k_io_buffer_acquire_lease(buf);
}

void __k_io_buffer_release_lease(void* buf) {
    k_io_buffer_release_lease(buf);
}

int __k_io_buffer_has_lease(const void* buf) {
    return k_io_buffer_has_lease((const KNativeByteBuffer*)buf) ? 1 : 0;
}

unsigned int __k_io_buffer_get_lease_count(const void* buf) {
    return k_io_buffer_get_lease_count((const KNativeByteBuffer*)buf);
}

unsigned int __k_io_buffer_get_capacity(const void* buf) {
    return (unsigned int)k_io_buffer_get_capacity((const KNativeByteBuffer*)buf);
}

unsigned int __k_io_buffer_get_position(const void* buf) {
    return (unsigned int)k_io_buffer_get_position((const KNativeByteBuffer*)buf);
}

unsigned int __k_io_buffer_get_limit(const void* buf) {
    return (unsigned int)k_io_buffer_get_limit((const KNativeByteBuffer*)buf);
}

int __k_io_buffer_set_position(void* buf, unsigned int new_pos) {
    return k_io_buffer_set_position((KNativeByteBuffer*)buf, (size_t)new_pos);
}

int __k_io_buffer_set_limit(void* buf, unsigned int new_limit) {
    return k_io_buffer_set_limit((KNativeByteBuffer*)buf, (size_t)new_limit);
}

int __k_io_buffer_flip(void* buf) {
    return k_io_buffer_flip((KNativeByteBuffer*)buf);
}

int __k_io_buffer_clear(void* buf) {
    return k_io_buffer_clear((KNativeByteBuffer*)buf);
}

int __k_io_buffer_rewind(void* buf) {
    return k_io_buffer_rewind((KNativeByteBuffer*)buf);
}

int __k_io_buffer_compact(void* buf) {
    return k_io_buffer_compact((KNativeByteBuffer*)buf);
}

uint8_t* __k_io_buffer_data_at(void* buf, unsigned int index) {
    return k_io_buffer_data_at((KNativeByteBuffer*)buf, (size_t)index);
}

int __k_io_buffer_get_byte(const void* buf, unsigned int index, uint8_t* out_byte) {
    return k_io_buffer_get_byte((const KNativeByteBuffer*)buf, (size_t)index, out_byte);
}

int __k_io_buffer_put_byte(void* buf, unsigned int index, uint8_t b) {
    return k_io_buffer_put_byte((KNativeByteBuffer*)buf, (size_t)index, b);
}

int __k_io_buffer_copy_from(void* buf, unsigned int pos, const uint8_t* src, unsigned int len) {
    return k_io_buffer_copy_from((KNativeByteBuffer*)buf, (size_t)pos, src, (size_t)len);
}

int __k_io_buffer_copy_to(const void* buf, unsigned int pos, uint8_t* dst, unsigned int len) {
    return k_io_buffer_copy_to((const KNativeByteBuffer*)buf, (size_t)pos, dst, (size_t)len);
}

int __k_io_buffer_is_registered(const void* buf) {
    return k_io_buffer_is_registered((const KNativeByteBuffer*)buf) ? 1 : 0;
}

int __k_io_buffer_register(void* buf) {
    return k_io_buffer_register((KNativeByteBuffer*)buf) ? 1 : 0;
}

int __k_io_buffer_unregister(void* buf) {
    return k_io_buffer_unregister((KNativeByteBuffer*)buf) ? 1 : 0;
}

int __k_io_buffer_force(void* buf) {
    return k_io_buffer_force((KNativeByteBuffer*)buf);
}

int __k_io_buffer_load(void* buf) {
    return k_io_buffer_load((KNativeByteBuffer*)buf);
}

int __k_io_buffer_is_loaded(const void* buf) {
    return k_io_buffer_is_loaded((const KNativeByteBuffer*)buf) ? 1 : 0;
}

/* ── FFI NativeBuffer bridge: unique symbols for the K FFI module ─────── */

void* __k_ffi_native_buffer_allocate(unsigned int capacity) {
    return __k_io_buffer_allocate(capacity);
}

void __k_ffi_native_buffer_release(void* buf) {
    __k_io_buffer_release(buf);
}

void __k_ffi_native_buffer_acquire_lease(void* buf) {
    __k_io_buffer_acquire_lease(buf);
}

void __k_ffi_native_buffer_release_lease(void* buf) {
    __k_io_buffer_release_lease(buf);
}

int __k_ffi_native_buffer_has_lease(const void* buf) {
    return __k_io_buffer_has_lease(buf);
}

uint8_t* __k_ffi_native_buffer_data_at(void* buf, unsigned int index) {
    return __k_io_buffer_data_at(buf, index);
}

int __k_ffi_native_buffer_get_byte(const void* buf, unsigned int index, uint8_t* out_byte) {
    return __k_io_buffer_get_byte(buf, index, out_byte);
}

int __k_ffi_native_buffer_put_byte(void* buf, unsigned int index, uint8_t b) {
    return __k_io_buffer_put_byte(buf, index, b);
}

int __k_ffi_native_buffer_copy_from(void* buf, unsigned int pos, const uint8_t* src, unsigned int len) {
    return __k_io_buffer_copy_from(buf, pos, src, len);
}

int __k_ffi_native_buffer_copy_to(const void* buf, unsigned int pos, uint8_t* dst, unsigned int len) {
    return __k_io_buffer_copy_to(buf, pos, dst, len);
}
