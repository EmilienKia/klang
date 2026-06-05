/*
 * K Language runtime — I/O bitcast and native byte-order helpers
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
 *
 * Bit-level reinterpretation between floating-point and integer types,
 * used by DataInputStream / DataOutputStream for IEEE 754 I/O.
 *
 * Native byte-order helpers: read/write primitive types from/to byte
 * buffers using the platform memory layout (memcpy), so that
 * Data*Stream reads/writes in the platform native byte order.
 *
 * K arrays have a {uint32, data[]} layout; callers pass &buf[0] so
 * these functions receive a plain pointer to the raw byte data.
 */

#include <string.h>
#include <stdint.h>
#include <stdio.h>

/* ── Float / double bitcast helpers ─────────────────────────────────────── */

int32_t __k_io_float_to_bits(float f) {
    int32_t bits;
    memcpy(&bits, &f, sizeof(bits));
    return bits;
}

float __k_io_bits_to_float(int32_t bits) {
    float f;
    memcpy(&f, &bits, sizeof(f));
    return f;
}

int64_t __k_io_double_to_bits(double d) {
    int64_t bits;
    memcpy(&bits, &d, sizeof(bits));
    return bits;
}

double __k_io_bits_to_double(int64_t bits) {
    double d;
    memcpy(&d, &bits, sizeof(d));
    return d;
}

/* ── Native byte-order: read value from byte buffer ─────────────────────── */

int32_t __k_io_native_bytes_to_short(const uint8_t* buf) {
    int16_t v;
    memcpy(&v, buf, sizeof(v));
    return (int32_t)v;
}

int32_t __k_io_native_bytes_to_int(const uint8_t* buf) {
    int32_t v;
    memcpy(&v, buf, sizeof(v));
    return v;
}

int64_t __k_io_native_bytes_to_long(const uint8_t* buf) {
    int64_t v;
    memcpy(&v, buf, sizeof(v));
    return v;
}

/* ── Native byte-order: write value into byte buffer ────────────────────── */

void __k_io_short_to_native_bytes(int32_t v, uint8_t* buf) {
    int16_t sv = (int16_t)v;
    memcpy(buf, &sv, sizeof(sv));
}

void __k_io_int_to_native_bytes(int32_t v, uint8_t* buf) {
    memcpy(buf, &v, sizeof(v));
}

void __k_io_long_to_native_bytes(int64_t v, uint8_t* buf) {
    memcpy(buf, &v, sizeof(v));
}

/* ── Number-to-string helpers (used by PrintStream) ─────────────────────── */

int32_t __k_io_int_to_str(int32_t v, uint8_t* buf, int32_t bufLen) {
    int n = snprintf((char*)buf, (size_t)bufLen, "%d", (int)v);
    return (int32_t)(n < bufLen ? n : bufLen - 1);
}

int32_t __k_io_long_to_str(int64_t v, uint8_t* buf, int32_t bufLen) {
    int n = snprintf((char*)buf, (size_t)bufLen, "%lld", (long long)v);
    return (int32_t)(n < bufLen ? n : bufLen - 1);
}

int32_t __k_io_uint_to_str(uint32_t v, uint8_t* buf, int32_t bufLen) {
    int n = snprintf((char*)buf, (size_t)bufLen, "%u", (unsigned)v);
    return (int32_t)(n < bufLen ? n : bufLen - 1);
}

int32_t __k_io_ulong_to_str(uint64_t v, uint8_t* buf, int32_t bufLen) {
    int n = snprintf((char*)buf, (size_t)bufLen, "%llu", (unsigned long long)v);
    return (int32_t)(n < bufLen ? n : bufLen - 1);
}

int32_t __k_io_float_to_str(float v, uint8_t* buf, int32_t bufLen) {
    int n = snprintf((char*)buf, (size_t)bufLen, "%g", (double)v);
    return (int32_t)(n < bufLen ? n : bufLen - 1);
}

int32_t __k_io_double_to_str(double v, uint8_t* buf, int32_t bufLen) {
    int n = snprintf((char*)buf, (size_t)bufLen, "%g", v);
    return (int32_t)(n < bufLen ? n : bufLen - 1);
}

/* ── char-to-byte helper (used by PrintStream) ──────────────────────────── */

/* Encode `len` UTF-32 code points from `src` into UTF-8 bytes in `dst`.
 * Returns the number of bytes written. `dst` must have room for up to 4*len
 * bytes. K `char` is a 32-bit Unicode scalar value (UTF-32). */
int32_t __k_io_chars_to_bytes(const uint32_t* src, uint8_t* dst, int32_t len) {
    int32_t out = 0;
    for (int32_t i = 0; i < len; i++) {
        uint32_t cp = src[i];
        if (cp < 0x80) {
            dst[out++] = (uint8_t)cp;
        } else if (cp < 0x800) {
            dst[out++] = (uint8_t)(0xC0 | (cp >> 6));
            dst[out++] = (uint8_t)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            dst[out++] = (uint8_t)(0xE0 | (cp >> 12));
            dst[out++] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
            dst[out++] = (uint8_t)(0x80 | (cp & 0x3F));
        } else {
            dst[out++] = (uint8_t)(0xF0 | (cp >> 18));
            dst[out++] = (uint8_t)(0x80 | ((cp >> 12) & 0x3F));
            dst[out++] = (uint8_t)(0x80 | ((cp >> 6) & 0x3F));
            dst[out++] = (uint8_t)(0x80 | (cp & 0x3F));
        }
    }
    return out;
}
