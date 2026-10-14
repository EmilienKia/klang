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

#ifndef KLANG_RUNTIME_IO_BUFFER_H
#define KLANG_RUNTIME_IO_BUFFER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Storage & Mapping Enums ────────────────────────────────────────────── */

typedef enum {
    K_BUFFER_HEAP   = 0,
    K_BUFFER_DIRECT = 1,
    K_BUFFER_MAPPED = 2
} KBufferStorageType;

typedef enum {
    K_MAP_READ_ONLY  = 0,
    K_MAP_READ_WRITE = 1,
    K_MAP_PRIVATE    = 2
} KMapMode;

/* ── Native Buffer Structure ────────────────────────────────────────────── */

typedef struct KNativeByteBuffer KNativeByteBuffer;

struct KNativeByteBuffer {
    _Atomic uint32_t   ref_count;
    _Atomic uint32_t   lease_count;
    pid_t              creator_pid;

    KBufferStorageType storage_type;
    uint8_t*           data;         /* Pointer to accessible data */
    size_t             capacity;
    size_t             position;
    size_t             limit;

    /* Direct allocation alignment (if any) */
    void*              raw_alloc;    /* Pointer for free() if distinct from data */

    /* Mapped buffer fields */
    void*              map_base;     /* Base address returned by mmap (page-aligned) */
    size_t             map_len;      /* Total length mapped */
    int                map_mode;     /* KMapMode */
    int                fd;           /* -1 if not kept / anonymous */

    /* Registration support (sparse table) */
    _Atomic bool       is_registered;
    _Atomic int        fixed_index;  /* Slot index in sparse table, -1 if non-fixed */
    bool               unregister_deferred;

    /* Synchronization for position/limit/lease mutations */
    pthread_mutex_t    lock;
};

/* ── Creation & Destruction ─────────────────────────────────────────────── */

KNativeByteBuffer* k_io_buffer_allocate(size_t capacity);
KNativeByteBuffer* k_io_buffer_allocate_direct(size_t capacity);
KNativeByteBuffer* k_io_buffer_map(int fd, int64_t offset, size_t length, KMapMode mode);
KNativeByteBuffer* k_io_buffer_map_path(const char* path, int64_t offset, size_t length, KMapMode mode);

void k_io_buffer_retain(KNativeByteBuffer* buf);
void k_io_buffer_release(KNativeByteBuffer* buf);

/* ── Lease Management ───────────────────────────────────────────────────── */

void     k_io_buffer_acquire_lease(void* buf);
void     k_io_buffer_release_lease(void* buf);
bool     k_io_buffer_has_lease(const KNativeByteBuffer* buf);
uint32_t k_io_buffer_get_lease_count(const KNativeByteBuffer* buf);

/* ── Cursor and Geometry ────────────────────────────────────────────────── */

size_t k_io_buffer_get_capacity(const KNativeByteBuffer* buf);
size_t k_io_buffer_get_position(const KNativeByteBuffer* buf);
size_t k_io_buffer_get_limit(const KNativeByteBuffer* buf);

int  k_io_buffer_set_position(KNativeByteBuffer* buf, size_t new_pos);
int  k_io_buffer_set_limit(KNativeByteBuffer* buf, size_t new_limit);
int  k_io_buffer_flip(KNativeByteBuffer* buf);
int  k_io_buffer_clear(KNativeByteBuffer* buf);
int  k_io_buffer_rewind(KNativeByteBuffer* buf);
int  k_io_buffer_compact(KNativeByteBuffer* buf);
void k_io_buffer_advance_position(void* buf, size_t n);

/* ── Data Access ────────────────────────────────────────────────────────── */

uint8_t* k_io_buffer_data_at(KNativeByteBuffer* buf, size_t index);
int      k_io_buffer_get_byte(const KNativeByteBuffer* buf, size_t index, uint8_t* out_byte);
int      k_io_buffer_put_byte(KNativeByteBuffer* buf, size_t index, uint8_t b);
int      k_io_buffer_copy_from(KNativeByteBuffer* buf, size_t pos, const uint8_t* src, size_t len);
int      k_io_buffer_copy_to(const KNativeByteBuffer* buf, size_t pos, uint8_t* dst, size_t len);

/* ── Registration ───────────────────────────────────────────────────────── */

bool k_io_buffer_is_registered(const KNativeByteBuffer* buf);
bool k_io_buffer_register(KNativeByteBuffer* buf);
bool k_io_buffer_unregister(KNativeByteBuffer* buf);

/* ── Mapped Operations ──────────────────────────────────────────────────── */

int  k_io_buffer_force(KNativeByteBuffer* buf);
int  k_io_buffer_load(KNativeByteBuffer* buf);
bool k_io_buffer_is_loaded(const KNativeByteBuffer* buf);

/* ── Test Helpers ───────────────────────────────────────────────────────── */

void   k_io_buffer_test_set_table_full(bool full);
size_t k_io_buffer_get_registered_count(void);
int k_io_buffer_fixed_index(const KNativeByteBuffer* buf);
void k_io_buffer_shutdown_registration(void);

/* ── FFI Bridge Exports (__k_io_buffer_*) ───────────────────────────────── */

void*        __k_io_buffer_allocate(unsigned int capacity);
void*        __k_io_buffer_allocate_direct(unsigned int capacity);
void*        __k_io_buffer_map(const uint32_t* k_path, int fd, int64_t offset, int64_t length, int mode);
void         __k_io_buffer_retain(void* buf);
void         __k_io_buffer_release(void* buf);
void         __k_io_buffer_acquire_lease(void* buf);
void         __k_io_buffer_release_lease(void* buf);
int          __k_io_buffer_has_lease(const void* buf);
unsigned int __k_io_buffer_get_lease_count(const void* buf);
unsigned int __k_io_buffer_get_capacity(const void* buf);
unsigned int __k_io_buffer_get_position(const void* buf);
unsigned int __k_io_buffer_get_limit(const void* buf);
int          __k_io_buffer_set_position(void* buf, unsigned int new_pos);
int          __k_io_buffer_set_limit(void* buf, unsigned int new_limit);
int          __k_io_buffer_flip(void* buf);
int          __k_io_buffer_clear(void* buf);
int          __k_io_buffer_rewind(void* buf);
int          __k_io_buffer_compact(void* buf);
uint8_t*     __k_io_buffer_data_at(void* buf, unsigned int index);
int          __k_io_buffer_get_byte(const void* buf, unsigned int index, uint8_t* out_byte);
int          __k_io_buffer_put_byte(void* buf, unsigned int index, uint8_t b);
/* Relative transfers copy and advance the cursor under the same lease-checked lock.
 * Get writes caller memory; put only reads it. */
int          __k_io_buffer_get_relative(void* buf, uint8_t* dst, unsigned int length);
int          __k_io_buffer_put_relative(void* buf, const uint8_t* src, unsigned int length);
int          __k_io_buffer_copy_from(void* buf, unsigned int pos, const uint8_t* src, unsigned int len);
int          __k_io_buffer_copy_to(const void* buf, unsigned int pos, uint8_t* dst, unsigned int len);
int          __k_io_buffer_is_registered(const void* buf);
int          __k_io_buffer_register(void* buf);
int          __k_io_buffer_unregister(void* buf);
int          __k_io_buffer_force(void* buf);
int          __k_io_buffer_load(void* buf);
int          __k_io_buffer_is_loaded(const void* buf);

/* ── FFI NativeBuffer bridge: unique symbols for the K FFI module ─────── */
void*        __k_ffi_native_buffer_allocate(unsigned int capacity);
void         __k_ffi_native_buffer_release(void* buf);
void         __k_ffi_native_buffer_acquire_lease(void* buf);
void         __k_ffi_native_buffer_release_lease(void* buf);
int          __k_ffi_native_buffer_has_lease(const void* buf);
uint8_t*     __k_ffi_native_buffer_data_at(void* buf, unsigned int index);
int          __k_ffi_native_buffer_get_byte(const void* buf, unsigned int index, uint8_t* out_byte);
int          __k_ffi_native_buffer_put_byte(void* buf, unsigned int index, uint8_t b);
int          __k_ffi_native_buffer_copy_from(void* buf, unsigned int pos, const uint8_t* src, unsigned int len);
int          __k_ffi_native_buffer_copy_to(const void* buf, unsigned int pos, uint8_t* dst, unsigned int len);

#ifdef __cplusplus
}
#endif

#endif /* KLANG_RUNTIME_IO_BUFFER_H */
