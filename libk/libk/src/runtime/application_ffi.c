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

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Extern declaration for the C environ array. */
extern char** environ;

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
