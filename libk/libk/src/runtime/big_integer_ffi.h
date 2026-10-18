/*
 * Opaque, fixed-width BigInteger bridge. Copyright 2026 Emilien Kia.
 * Licensed under the Apache License, Version 2.0.
 */
#ifndef K_BIG_INTEGER_FFI_H
#define K_BIG_INTEGER_FFI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct k_big_integer k_big_integer;

enum k_big_integer_status {
    K_BIG_INTEGER_OK = 0,
    K_BIG_INTEGER_OUT_OF_MEMORY = 1,
    K_BIG_INTEGER_DIVISION_BY_ZERO = 2,
    K_BIG_INTEGER_OVERFLOW = 3,
    K_BIG_INTEGER_NEGATIVE_UNSIGNED = 4,
    K_BIG_INTEGER_INVALID_ARGUMENT = 5,
    K_BIG_INTEGER_INVALID_FORMAT = 6
};

enum k_big_integer_operation {
    K_BIG_INTEGER_ADD = 0,
    K_BIG_INTEGER_SUBTRACT = 1,
    K_BIG_INTEGER_MULTIPLY = 2,
    K_BIG_INTEGER_DIVIDE = 3,
    K_BIG_INTEGER_REMAINDER = 4
};

/* All handles except release's argument must be non-null initialized objects.
 * A null create result means header allocation failed. GMP's internal
 * allocation failure remains fatal; no process-global allocator is installed.
 * Output arguments and arithmetic destinations are unchanged on error.
 * Arithmetic destinations may alias either operand.
 */
k_big_integer* __k_big_integer_create(void);
void __k_big_integer_release(k_big_integer* value);
void __k_big_integer_copy(k_big_integer* destination, const k_big_integer* source);
void __k_big_integer_swap(k_big_integer* left, k_big_integer* right);
void __k_big_integer_set_i64(k_big_integer* destination, int64_t value);
void __k_big_integer_set_u64(k_big_integer* destination, uint64_t value);
/* Counted UTF-32 input: no terminator is stripped. Base 0 is private decimal-
 * default prefix detection; otherwise bases 2..62 are accepted. Parsing is
 * strict and leaves both the destination and source unchanged on error.
 */
int32_t __k_big_integer_parse(k_big_integer* destination,
    const uint32_t* text, uint32_t length, int32_t base);
int32_t __k_big_integer_binary(k_big_integer* destination,
    const k_big_integer* left, const k_big_integer* right, int32_t operation);
void __k_big_integer_negate(k_big_integer* destination, const k_big_integer* source);
int32_t __k_big_integer_sign(const k_big_integer* value);
int32_t __k_big_integer_compare(const k_big_integer* left, const k_big_integer* right);
int32_t __k_big_integer_get_i64(const k_big_integer* value, int32_t bits, int64_t* result);
int32_t __k_big_integer_get_u64(const k_big_integer* value, int32_t bits, uint64_t* result);
/* Capacity includes the terminator and may overestimate the decimal length by
 * one byte. The output is ASCII/UTF-8, never GMP-owned memory.
 */
int32_t __k_big_integer_decimal_capacity(const k_big_integer* value, int32_t* capacity);
int32_t __k_big_integer_decimal_write(const k_big_integer* value, uint8_t* buffer, int32_t capacity);

#ifdef __cplusplus
}
#endif
#endif
