/*
 * GMP-backed BigInteger storage. Copyright 2026 Emilien Kia.
 * Licensed under the Apache License, Version 2.0.
 */
#include "big_integer_ffi.h"

#include <gmp.h>
#include <limits.h>
#include <stdlib.h>

struct k_big_integer {
    mpz_t value;
};

static int32_t normalized(int comparison) {
    return (comparison > 0) - (comparison < 0);
}

static void set_unsigned(mpz_t destination, uint64_t value) {
    mpz_import(destination, 1, 1, sizeof(value), 0, 0, &value);
}

k_big_integer* __k_big_integer_create(void) {
    k_big_integer* result = malloc(sizeof(*result));
    if (!result) return NULL;
    mpz_init(result->value);
    return result;
}

void __k_big_integer_release(k_big_integer* value) {
    if (!value) return;
    mpz_clear(value->value);
    free(value);
}

void __k_big_integer_copy(k_big_integer* destination, const k_big_integer* source) {
    if (destination != source) mpz_set(destination->value, source->value);
}

void __k_big_integer_swap(k_big_integer* left, k_big_integer* right) {
    if (left != right) mpz_swap(left->value, right->value);
}

void __k_big_integer_set_u64(k_big_integer* destination, uint64_t value) {
    set_unsigned(destination->value, value);
}

void __k_big_integer_set_i64(k_big_integer* destination, int64_t value) {
    /* Unsigned subtraction is defined even for INT64_MIN. */
    uint64_t magnitude = value < 0 ? UINT64_C(0) - (uint64_t)value : (uint64_t)value;
    set_unsigned(destination->value, magnitude);
    if (value < 0) mpz_neg(destination->value, destination->value);
}

static int32_t digit_value(uint32_t character, int32_t base) {
    if (character >= '0' && character <= '9') return (int32_t)(character - '0');
    if (character >= 'A' && character <= 'Z') return (int32_t)(character - 'A') + 10;
    if (character >= 'a' && character <= 'z') {
        return (int32_t)(character - 'a') + (base <= 36 ? 10 : 36);
    }
    return -1;
}

int32_t __k_big_integer_parse(k_big_integer* destination,
        const uint32_t* text, uint32_t length, int32_t base) {
    if (base != 0 && (base < 2 || base > 62)) return K_BIG_INTEGER_INVALID_FORMAT;
    if (!text || length == 0) return K_BIG_INTEGER_INVALID_FORMAT;

    uint32_t start = 0;
    int negative = text[0] == '-';
    if (negative || text[0] == '+') ++start;
    int32_t effective_base = base == 0 ? 10 : base;
    if (length - start >= 2 && text[start] == '0') {
        int32_t prefix_base = 0;
        switch (text[start + 1]) {
            case 'b': case 'B': prefix_base = 2; break;
            case 'o': case 'O': prefix_base = 8; break;
            case 'x': case 'X': prefix_base = 16; break;
        }
        if (prefix_base != 0 && (base == 0 || base == prefix_base)) {
            effective_base = prefix_base;
            start += 2;
        }
    }
    if (start == length) return K_BIG_INTEGER_INVALID_FORMAT;
    for (uint32_t i = start; i < length; ++i) {
        int32_t digit = digit_value(text[i], effective_base);
        if (digit < 0 || digit >= effective_base) return K_BIG_INTEGER_INVALID_FORMAT;
    }

    uint32_t digit_count = length - start;
    if ((uint64_t)digit_count + 1 > SIZE_MAX) return K_BIG_INTEGER_OVERFLOW;
    char* digits = malloc((size_t)digit_count + 1);
    if (!digits) return K_BIG_INTEGER_OUT_OF_MEMORY;
    for (uint32_t i = 0; i < digit_count; ++i) digits[i] = (char)text[start + i];
    digits[digit_count] = '\0';

    mpz_t parsed;
    mpz_init(parsed);
    int32_t status = K_BIG_INTEGER_INVALID_FORMAT;
    if (mpz_set_str(parsed, digits, effective_base) == 0) {
        if (negative) mpz_neg(parsed, parsed);
        mpz_swap(destination->value, parsed);
        status = K_BIG_INTEGER_OK;
    }
    mpz_clear(parsed);
    free(digits);
    return status;
}

int32_t __k_big_integer_binary(k_big_integer* destination,
        const k_big_integer* left, const k_big_integer* right, int32_t operation) {
    if ((operation == K_BIG_INTEGER_DIVIDE || operation == K_BIG_INTEGER_REMAINDER)
            && mpz_sgn(right->value) == 0) {
        return K_BIG_INTEGER_DIVISION_BY_ZERO;
    }
    switch (operation) {
        case K_BIG_INTEGER_ADD:
            mpz_add(destination->value, left->value, right->value);
            break;
        case K_BIG_INTEGER_SUBTRACT:
            mpz_sub(destination->value, left->value, right->value);
            break;
        case K_BIG_INTEGER_MULTIPLY:
            mpz_mul(destination->value, left->value, right->value);
            break;
        case K_BIG_INTEGER_DIVIDE:
            mpz_tdiv_q(destination->value, left->value, right->value);
            break;
        case K_BIG_INTEGER_REMAINDER:
            mpz_tdiv_r(destination->value, left->value, right->value);
            break;
        default:
            return K_BIG_INTEGER_INVALID_ARGUMENT;
    }
    return K_BIG_INTEGER_OK;
}

void __k_big_integer_negate(k_big_integer* destination, const k_big_integer* source) {
    mpz_neg(destination->value, source->value);
}

int32_t __k_big_integer_sign(const k_big_integer* value) {
    return normalized(mpz_sgn(value->value));
}

int32_t __k_big_integer_compare(const k_big_integer* left, const k_big_integer* right) {
    return normalized(mpz_cmp(left->value, right->value));
}

static int valid_width(int32_t bits) {
    return bits == 8 || bits == 16 || bits == 32 || bits == 64;
}

int32_t __k_big_integer_get_i64(const k_big_integer* value, int32_t bits, int64_t* result) {
    if (!valid_width(bits)) return K_BIG_INTEGER_INVALID_ARGUMENT;
    int negative = mpz_sgn(value->value) < 0;
    uint64_t limit = (UINT64_C(1) << (bits - 1)) - 1;
    if (negative) ++limit;
    mpz_t bound;
    mpz_init(bound);
    set_unsigned(bound, limit);
    int out_of_range = mpz_cmpabs(value->value, bound) > 0;
    mpz_clear(bound);
    if (out_of_range) return K_BIG_INTEGER_OVERFLOW;

    uint64_t magnitude = 0;
    mpz_export(&magnitude, NULL, 1, sizeof(magnitude), 0, 0, value->value);
    if (negative && magnitude == (UINT64_C(1) << 63)) {
        *result = INT64_MIN;
    } else {
        *result = negative ? -(int64_t)magnitude : (int64_t)magnitude;
    }
    return K_BIG_INTEGER_OK;
}

int32_t __k_big_integer_get_u64(const k_big_integer* value, int32_t bits, uint64_t* result) {
    if (!valid_width(bits)) return K_BIG_INTEGER_INVALID_ARGUMENT;
    if (mpz_sgn(value->value) < 0) return K_BIG_INTEGER_NEGATIVE_UNSIGNED;
    if (mpz_sizeinbase(value->value, 2) > (size_t)bits) return K_BIG_INTEGER_OVERFLOW;
    uint64_t magnitude = 0;
    mpz_export(&magnitude, NULL, 1, sizeof(magnitude), 0, 0, value->value);
    *result = magnitude;
    return K_BIG_INTEGER_OK;
}

int32_t __k_big_integer_decimal_capacity(const k_big_integer* value, int32_t* capacity) {
    size_t digits = mpz_sizeinbase(value->value, 10);
    if (digits > INT32_MAX - 2) return K_BIG_INTEGER_OVERFLOW;
    *capacity = (int32_t)digits + 2;
    return K_BIG_INTEGER_OK;
}

int32_t __k_big_integer_decimal_write(const k_big_integer* value,
        uint8_t* buffer, int32_t capacity) {
    int32_t required;
    int32_t status = __k_big_integer_decimal_capacity(value, &required);
    if (status != K_BIG_INTEGER_OK) return status;
    if (!buffer || capacity < required) return K_BIG_INTEGER_INVALID_ARGUMENT;
    mpz_get_str((char*)buffer, 10, value->value);
    return K_BIG_INTEGER_OK;
}
