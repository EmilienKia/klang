/*
 * BigInteger fixed-width bridge regressions. Copyright 2026 Emilien Kia.
 * Licensed under the Apache License, Version 2.0.
 */
#include <catch2/catch_all.hpp>
#include "big_integer_ffi.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {
using Integer = std::unique_ptr<k_big_integer, decltype(&__k_big_integer_release)>;

Integer integer(int64_t value = 0) {
    Integer result(__k_big_integer_create(), &__k_big_integer_release);
    REQUIRE(result);
    __k_big_integer_set_i64(result.get(), value);
    return result;
}

int64_t signed_value(const Integer& value, int bits = 64) {
    int64_t result = 0;
    REQUIRE(__k_big_integer_get_i64(value.get(), bits, &result) == K_BIG_INTEGER_OK);
    return result;
}

std::string decimal(const Integer& value) {
    int32_t capacity = 0;
    REQUIRE(__k_big_integer_decimal_capacity(value.get(), &capacity) == K_BIG_INTEGER_OK);
    std::vector<uint8_t> text(capacity, 0xa5);
    REQUIRE(__k_big_integer_decimal_write(value.get(), text.data(), capacity) == K_BIG_INTEGER_OK);
    return reinterpret_cast<const char*>(text.data());
}

int32_t parse(const Integer& value, std::u32string_view source, int32_t base = 0) {
    std::vector<uint32_t> text(source.begin(), source.end());
    const auto saved = text;
    const auto status = __k_big_integer_parse(value.get(), text.data(),
                                             static_cast<uint32_t>(text.size()), base);
    CHECK(text == saved);
    return status;
}
}

TEST_CASE("BigInteger native arithmetic remains exact beyond 64 bits",
          "[libk][big-integer][native]") {
    auto maximum = integer();
    auto result = integer();
    auto one = integer(1);
    __k_big_integer_set_u64(maximum.get(), UINT64_MAX);
    REQUIRE(__k_big_integer_binary(result.get(), maximum.get(), maximum.get(),
                                  K_BIG_INTEGER_MULTIPLY) == K_BIG_INTEGER_OK);
    CHECK(decimal(result) == "340282366920938463426481119284349108225");
    REQUIRE(__k_big_integer_binary(result.get(), result.get(), maximum.get(),
                                  K_BIG_INTEGER_DIVIDE) == K_BIG_INTEGER_OK);
    CHECK(__k_big_integer_compare(result.get(), maximum.get()) == 0);
    REQUIRE(__k_big_integer_binary(result.get(), maximum.get(), one.get(),
                                  K_BIG_INTEGER_ADD) == K_BIG_INTEGER_OK);
    CHECK(decimal(result) == "18446744073709551616");
    __k_big_integer_negate(result.get(), result.get());
    CHECK(decimal(result) == "-18446744073709551616");
    CHECK(__k_big_integer_compare(result.get(), one.get()) == -1);
    CHECK(__k_big_integer_compare(one.get(), result.get()) == 1);
}

TEST_CASE("BigInteger native signed and unsigned extremes do not coerce",
          "[libk][big-integer][native]") {
    auto minimum = integer(INT64_MIN);
    auto maximum = integer();
    auto result = integer();
    __k_big_integer_set_u64(maximum.get(), UINT64_MAX);
    CHECK(signed_value(minimum) == INT64_MIN);
    CHECK(decimal(minimum) == "-9223372036854775808");
    CHECK(decimal(maximum) == "18446744073709551615");
    REQUIRE(__k_big_integer_binary(result.get(), minimum.get(), maximum.get(),
                                  K_BIG_INTEGER_ADD) == K_BIG_INTEGER_OK);
    CHECK(signed_value(result) == INT64_MAX);
    REQUIRE(__k_big_integer_binary(result.get(), maximum.get(), minimum.get(),
                                  K_BIG_INTEGER_ADD) == K_BIG_INTEGER_OK);
    CHECK(signed_value(result) == INT64_MAX);
    REQUIRE(__k_big_integer_binary(result.get(), minimum.get(), maximum.get(),
                                  K_BIG_INTEGER_SUBTRACT) == K_BIG_INTEGER_OK);
    CHECK(decimal(result) == "-27670116110564327423");
    REQUIRE(__k_big_integer_binary(result.get(), maximum.get(), minimum.get(),
                                  K_BIG_INTEGER_SUBTRACT) == K_BIG_INTEGER_OK);
    CHECK(decimal(result) == "27670116110564327423");
    REQUIRE(__k_big_integer_binary(result.get(), maximum.get(), minimum.get(),
                                  K_BIG_INTEGER_DIVIDE) == K_BIG_INTEGER_OK);
    CHECK(signed_value(result) == -1);
    REQUIRE(__k_big_integer_binary(result.get(), maximum.get(), minimum.get(),
                                  K_BIG_INTEGER_REMAINDER) == K_BIG_INTEGER_OK);
    CHECK(signed_value(result) == INT64_MAX);
}

TEST_CASE("BigInteger native division truncates toward zero and preserves failed destinations",
          "[libk][big-integer][native]") {
    for (const int64_t numerator : {-17, 17}) {
        for (const int64_t denominator : {-5, 5}) {
            auto left = integer(numerator);
            auto right = integer(denominator);
            auto result = integer();
            REQUIRE(__k_big_integer_binary(result.get(), left.get(), right.get(),
                                          K_BIG_INTEGER_DIVIDE) == K_BIG_INTEGER_OK);
            CHECK(signed_value(result) == numerator / denominator);
            REQUIRE(__k_big_integer_binary(result.get(), left.get(), right.get(),
                                          K_BIG_INTEGER_REMAINDER) == K_BIG_INTEGER_OK);
            CHECK(signed_value(result) == numerator % denominator);
            // Exercise a destination aliasing the divisor as well.
            REQUIRE(__k_big_integer_binary(right.get(), left.get(), right.get(),
                                          K_BIG_INTEGER_DIVIDE) == K_BIG_INTEGER_OK);
            CHECK(signed_value(right) == numerator / denominator);
        }
    }
    auto receiver = integer(71);
    auto zero = integer();
    for (const auto operation : {K_BIG_INTEGER_DIVIDE, K_BIG_INTEGER_REMAINDER}) {
        CHECK(__k_big_integer_binary(receiver.get(), receiver.get(), zero.get(),
                                    operation) == K_BIG_INTEGER_DIVISION_BY_ZERO);
        CHECK(signed_value(receiver) == 71);
        CHECK(__k_big_integer_binary(zero.get(), receiver.get(), zero.get(),
                                    operation) == K_BIG_INTEGER_DIVISION_BY_ZERO);
        CHECK(__k_big_integer_sign(zero.get()) == 0);
    }
    CHECK(__k_big_integer_binary(receiver.get(), receiver.get(), zero.get(), 99)
          == K_BIG_INTEGER_INVALID_ARGUMENT);
    CHECK(signed_value(receiver) == 71);
}

TEST_CASE("BigInteger native copies and drain swaps own independent initialized values",
          "[libk][big-integer][native]") {
    auto original = integer(INT64_MIN);
    auto copy = integer();
    auto drained = integer();
    __k_big_integer_copy(copy.get(), original.get());
    __k_big_integer_negate(copy.get(), copy.get());
    CHECK(signed_value(original) == INT64_MIN);
    CHECK(decimal(copy) == "9223372036854775808");
    __k_big_integer_copy(original.get(), original.get());
    __k_big_integer_swap(original.get(), original.get());
    CHECK(signed_value(original) == INT64_MIN);
    __k_big_integer_swap(drained.get(), original.get());
    CHECK(__k_big_integer_sign(original.get()) == 0);
    CHECK(signed_value(drained) == INT64_MIN);
    __k_big_integer_set_i64(original.get(), 42);
    CHECK(signed_value(drained) == INT64_MIN);
    CHECK(signed_value(original) == 42);
}

TEST_CASE("BigInteger native checked conversions validate every width before extraction",
          "[libk][big-integer][native]") {
    auto value = integer();
    auto one = integer(1);
    for (const int bits : {8, 16, 32, 64}) {
        INFO("bits = " << bits);
        const uint64_t signed_max = (UINT64_C(1) << (bits - 1)) - 1;
        const uint64_t unsigned_max = bits == 64 ? UINT64_MAX : (UINT64_C(1) << bits) - 1;
        __k_big_integer_set_u64(value.get(), signed_max);
        CHECK(signed_value(value, bits) == static_cast<int64_t>(signed_max));
        REQUIRE(__k_big_integer_binary(value.get(), value.get(), one.get(),
                                      K_BIG_INTEGER_ADD) == K_BIG_INTEGER_OK);
        int64_t signed_result = 42;
        CHECK(__k_big_integer_get_i64(value.get(), bits, &signed_result) == K_BIG_INTEGER_OVERFLOW);
        CHECK(signed_result == 42);
        __k_big_integer_negate(value.get(), value.get());
        const int64_t signed_min = bits == 64 ? INT64_MIN : -static_cast<int64_t>(signed_max + 1);
        CHECK(signed_value(value, bits) == signed_min);
        REQUIRE(__k_big_integer_binary(value.get(), value.get(), one.get(),
                                      K_BIG_INTEGER_SUBTRACT) == K_BIG_INTEGER_OK);
        CHECK(__k_big_integer_get_i64(value.get(), bits, &signed_result) == K_BIG_INTEGER_OVERFLOW);
        CHECK(signed_result == 42);

        __k_big_integer_set_u64(value.get(), unsigned_max);
        uint64_t unsigned_result = 42;
        REQUIRE(__k_big_integer_get_u64(value.get(), bits, &unsigned_result) == K_BIG_INTEGER_OK);
        CHECK(unsigned_result == unsigned_max);
        REQUIRE(__k_big_integer_binary(value.get(), value.get(), one.get(),
                                      K_BIG_INTEGER_ADD) == K_BIG_INTEGER_OK);
        unsigned_result = 42;
        CHECK(__k_big_integer_get_u64(value.get(), bits, &unsigned_result) == K_BIG_INTEGER_OVERFLOW);
        CHECK(unsigned_result == 42);
        __k_big_integer_set_i64(value.get(), -1);
        CHECK(__k_big_integer_get_u64(value.get(), bits, &unsigned_result) == K_BIG_INTEGER_NEGATIVE_UNSIGNED);
        CHECK(unsigned_result == 42);
        __k_big_integer_set_i64(value.get(), 73);
        REQUIRE(__k_big_integer_get_u64(value.get(), bits, &unsigned_result) == K_BIG_INTEGER_OK);
        CHECK(unsigned_result == 73);
        __k_big_integer_set_i64(value.get(), 0);
        REQUIRE(__k_big_integer_get_u64(value.get(), bits, &unsigned_result) == K_BIG_INTEGER_OK);
        CHECK(unsigned_result == 0);
    }
}

TEST_CASE("BigInteger native decimal output handles zero and rejects undersized buffers",
          "[libk][big-integer][native]") {
    auto value = integer();
    CHECK(decimal(value) == "0");
    __k_big_integer_set_i64(value.get(), -1000);
    CHECK(decimal(value) == "-1000");
    std::array<uint8_t, 2> buffer{0xa5, 0xa5};
    CHECK(__k_big_integer_decimal_write(value.get(), buffer.data(), buffer.size())
          == K_BIG_INTEGER_INVALID_ARGUMENT);
    CHECK((buffer == std::array<uint8_t, 2>{0xa5, 0xa5}));
}

TEST_CASE("BigInteger native parsing handles decimal defaults prefixes and arbitrary precision",
          "[libk][big-integer][parsing][native]") {
    struct Example {
        std::u32string_view text;
        std::string_view expected;
    };
    const Example examples[]{
        {U"0", "0"}, {U"-000", "0"}, {U"+010", "10"}, {U"09", "9"},
        {U"00042", "42"}, {U"+42", "42"}, {U"-42", "-42"},
        {U"0b101", "5"}, {U"-0B101", "-5"},
        {U"0o17", "15"}, {U"+0O17", "15"},
        {U"0xAf", "175"}, {U"-0XfF", "-255"},
        {U"0x100000000000000000000000000000000", "340282366920938463463374607431768211456"},
        {U"-340282366920938463463374607431768211457", "-340282366920938463463374607431768211457"},
        {U"+12345678901234567890123456789012345678901234567890",
         "12345678901234567890123456789012345678901234567890"}
    };
    auto value = integer(71);
    for (const auto& example : examples) {
        INFO("expected = " << example.expected);
        REQUIRE(parse(value, example.text) == K_BIG_INTEGER_OK);
        CHECK(decimal(value) == example.expected);
    }
}

TEST_CASE("BigInteger native parsing obeys explicit bases and GMP digit case",
          "[libk][big-integer][parsing][native]") {
    struct Example {
        std::u32string_view text;
        int32_t base;
        std::string_view expected;
    };
    const Example examples[]{
        {U"101", 2, "5"}, {U"+0B101", 2, "5"},
        {U"17", 8, "15"}, {U"-0o17", 8, "-15"},
        {U"010", 10, "10"}, {U"Af", 16, "175"}, {U"0XaF", 16, "175"},
        {U"0b10", 16, "2832"}, {U"0B10", 16, "2832"},
        {U"0x", 36, "33"}, {U"zZ", 36, "1295"},
        {U"Aa", 37, "406"}, {U"aA", 37, "1342"},
        {U"Zz", 62, "2231"}, {U"zZ", 62, "3817"},
        {U"-0", 62, "0"}
    };
    auto value = integer();
    for (const auto& example : examples) {
        INFO("base = " << example.base << ", expected = " << example.expected);
        REQUIRE(parse(value, example.text, example.base) == K_BIG_INTEGER_OK);
        CHECK(decimal(value) == example.expected);
    }
}

TEST_CASE("BigInteger native parsing rejects malformed counted text without mutation",
          "[libk][big-integer][parsing][native]") {
    const std::u32string_view malformed[]{
        U"", U"+", U"-", U"0b", U"+0O", U"-0x", U"++1", U"+-1", U"--1",
        U" 12", U"12 ", U"1 2", U"\t12", U"12\n", U"1_2", U"1,2",
        U"12!", U"0b2", U"0o8", U"0xg", U"12x", U"\u0661", U"\uff11",
        std::u32string_view(U"1\0" U"2", 3),
        std::u32string_view(U"12\0", 3),
        std::u32string_view(U"\0", 1),
        std::u32string_view(U"\0\0", 2)
    };
    auto value = integer(71);
    for (const auto text : malformed) {
        CAPTURE(text.size());
        CHECK(parse(value, text) == K_BIG_INTEGER_INVALID_FORMAT);
        CHECK(signed_value(value) == 71);
    }
    for (const int32_t base : {-2, 1, 63, INT32_MIN, INT32_MAX}) {
        CAPTURE(base);
        CHECK(parse(value, U"10", base) == K_BIG_INTEGER_INVALID_FORMAT);
        CHECK(signed_value(value) == 71);
    }
    CHECK(parse(value, U"2", 2) == K_BIG_INTEGER_INVALID_FORMAT);
    CHECK(parse(value, U"b", 37) == K_BIG_INTEGER_INVALID_FORMAT);
    CHECK(parse(value, U"0x10", 10) == K_BIG_INTEGER_INVALID_FORMAT);
    CHECK(parse(value, U"0b", 2) == K_BIG_INTEGER_INVALID_FORMAT);
    CHECK(parse(value, U"0o", 8) == K_BIG_INTEGER_INVALID_FORMAT);
    CHECK(parse(value, U"0x", 16) == K_BIG_INTEGER_INVALID_FORMAT);
    CHECK(__k_big_integer_parse(value.get(), nullptr, 1, 10) == K_BIG_INTEGER_INVALID_FORMAT);
    CHECK(signed_value(value) == 71);
}
