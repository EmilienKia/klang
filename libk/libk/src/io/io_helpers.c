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




