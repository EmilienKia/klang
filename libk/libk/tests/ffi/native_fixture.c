/*
 * K Language standard library — native runtime FFI fixture
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

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define FIXTURE_EXPORT __attribute__((visibility("default")))

FIXTURE_EXPORT int klang_ffi_fixture_int(int value) {
    return value ^ 0x55;
}

FIXTURE_EXPORT int64_t klang_ffi_fixture_sint64(int64_t value) {
    return value;
}

FIXTURE_EXPORT uint64_t klang_ffi_fixture_uint64(uint64_t value) {
    return value;
}

FIXTURE_EXPORT float klang_ffi_fixture_float(float value) {
    return value;
}

FIXTURE_EXPORT double klang_ffi_fixture_double(double value) {
    return value;
}

FIXTURE_EXPORT bool klang_ffi_fixture_bool(bool value) {
    return !value;
}

FIXTURE_EXPORT double klang_ffi_fixture_mixed(int32_t first, double second,
                                             uint64_t third, float fourth) {
    return (double)first + second + (double)third + (double)fourth;
}

FIXTURE_EXPORT void *klang_ffi_fixture_pointer(void *value) {
    return value;
}

FIXTURE_EXPORT size_t klang_ffi_fixture_strlen(const char *value) {
    return value != NULL ? strlen(value) : 0;
}

FIXTURE_EXPORT size_t klang_ffi_fixture_fill(unsigned char *buffer, size_t size,
                                            unsigned char value) {
    if (buffer == NULL) {
        return 0;
    }
    memset(buffer, value, size);
    return size;
}

FIXTURE_EXPORT void klang_ffi_fixture_out(int64_t *output, int64_t value) {
    if (output != NULL) {
        *output = value;
    }
}

FIXTURE_EXPORT int klang_ffi_fixture_set_errno(int error_number) {
    errno = error_number;
    return -1;
}

FIXTURE_EXPORT int64_t klang_ffi_fixture_sum_ints(const int32_t *values, size_t count) {
    int64_t sum = 0;
    for (size_t i = 0; i < count; ++i) {
        sum += values[i];
    }
    return sum;
}

FIXTURE_EXPORT double klang_ffi_fixture_sum_doubles(const double *values, size_t count) {
    double sum = 0;
    for (size_t i = 0; i < count; ++i) {
        sum += values[i];
    }
    return sum;
}

FIXTURE_EXPORT void *klang_ffi_fixture_array_pointer(void *const *values, size_t index) {
    return values[index];
}
