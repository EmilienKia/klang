/*
 * K Language standard library — Time arithmetic & Gregorian tests
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

#include <catch2/catch_all.hpp>
#include "helpers.hpp"

#ifndef LIBK_KDI_DIR
#error "LIBK_KDI_DIR not defined"
#endif
#ifndef LIBK_LIB_DIR
#error "LIBK_LIB_DIR not defined"
#endif

namespace {

std::unique_ptr<k::model::gen::jit> jit_k(std::string_view src) {
    return gen_jit_with_stdlib(src, LIBK_KDI_DIR, LIBK_LIB_DIR);
}

} // anonymous namespace

// ═════════════════════════════════════════════════════════════════════════════
// 1. Temporal exception hierarchy and error codes
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Temporal exceptions: default error codes", "[libk][time][exceptions]") {
    auto jit = jit_k(R"SRC(
        module __test_temporal_error_codes__;

        test_temporal() : int {
            e : k::time::TemporalException;
            return e.getCode();
        }

        test_invalid_value() : int {
            e : k::time::InvalidTemporalValueException;
            return e.getCode();
        }

        test_arithmetic() : int {
            e : k::time::TemporalArithmeticException;
            return e.getCode();
        }

        test_parse() : int {
            e : k::time::TemporalParseException;
            return e.getCode();
        }

        test_leap_second() : int {
            e : k::time::LeapSecondException;
            return e.getCode();
        }

        test_data_unavailable() : int {
            e : k::time::TimeDataUnavailableException;
            return e.getCode();
        }

        test_zone_rules_unavailable() : int {
            e : k::time::ZoneRulesUnavailableException;
            return e.getCode();
        }

        test_sys_timezone_unavailable() : int {
            e : k::time::SystemTimeZoneUnavailableException;
            return e.getCode();
        }

        test_timescale_unavailable() : int {
            e : k::time::TimeScaleDataUnavailableException;
            return e.getCode();
        }

        test_resolution() : int {
            e : k::time::LocalTimeResolutionException;
            return e.getCode();
        }

        test_gap() : int {
            e : k::time::LocalTimeGapException;
            return e.getCode();
        }

        test_overlap() : int {
            e : k::time::LocalTimeOverlapException;
            return e.getCode();
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto check = [&](const char* sym, int expected) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == expected);
    };

    check("test_temporal", 500);
    check("test_invalid_value", 501);
    check("test_arithmetic", 502);
    check("test_parse", 503);
    check("test_leap_second", 504);
    check("test_data_unavailable", 510);
    check("test_zone_rules_unavailable", 511);
    check("test_sys_timezone_unavailable", 512);
    check("test_timescale_unavailable", 513);
    check("test_resolution", 520);
    check("test_gap", 521);
    check("test_overlap", 522);
}

TEST_CASE("Temporal exceptions: custom error code constructor", "[libk][time][exceptions]") {
    auto jit = jit_k(R"SRC(
        module __test_temporal_custom_codes__;

        test_custom() : int {
            e : k::time::TemporalArithmeticException(999);
            return e.getCode();
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test_custom");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 999);
}

TEST_CASE("Temporal exceptions: throw and catch by base classes", "[libk][time][exceptions]") {
    auto jit = jit_k(R"SRC(
        module __test_temporal_catch_hierarchy__;

        test_catch_by_temporal() : int {
            try {
                throw k::time::TemporalArithmeticException();
            } catch (e: k::time::TemporalException&) {
                return e.getCode();
            }
            return 0;
        }

        test_catch_by_exception() : int {
            try {
                throw k::time::ZoneRulesUnavailableException();
            } catch (e: Exception&) {
                return e.getCode();
            }
            return 0;
        }

        test_catch_by_data_unavailable() : int {
            try {
                throw k::time::SystemTimeZoneUnavailableException();
            } catch (e: k::time::TimeDataUnavailableException&) {
                return e.getCode();
            }
            return 0;
        }

        test_catch_by_resolution() : int {
            try {
                throw k::time::LocalTimeGapException();
            } catch (e: k::time::LocalTimeResolutionException&) {
                return e.getCode();
            }
            return 0;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto check = [&](const char* sym, int expected) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == expected);
    };

    check("test_catch_by_temporal", 502);
    check("test_catch_by_exception", 511);
    check("test_catch_by_data_unavailable", 512);
    check("test_catch_by_resolution", 521);
}

// ═════════════════════════════════════════════════════════════════════════════
// 2. Unit conversion constants
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Time constants: conversion factors", "[libk][time][constants]") {
    auto jit = jit_k(R"SRC(
        module __test_time_constants__;

        test_nanos_per_micro()    : long { return k::time::NANOS_PER_MICRO; }
        test_micros_per_milli()   : long { return k::time::MICROS_PER_MILLI; }
        test_millis_per_second()  : long { return k::time::MILLIS_PER_SECOND; }
        test_nanos_per_milli()    : long { return k::time::NANOS_PER_MILLI; }
        test_nanos_per_second()   : long { return k::time::NANOS_PER_SECOND; }
        test_seconds_per_minute() : long { return k::time::SECONDS_PER_MINUTE; }
        test_minutes_per_hour()   : long { return k::time::MINUTES_PER_HOUR; }
        test_seconds_per_hour()   : long { return k::time::SECONDS_PER_HOUR; }
        test_hours_per_day()      : long { return k::time::HOURS_PER_DAY; }
        test_seconds_per_day()    : long { return k::time::SECONDS_PER_DAY; }
    )SRC");
    REQUIRE(jit != nullptr);

    auto check = [&](const char* sym, int64_t expected) {
        auto fn = jit->lookup_symbol<int64_t(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == expected);
    };

    check("test_nanos_per_micro", 1000L);
    check("test_micros_per_milli", 1000L);
    check("test_millis_per_second", 1000L);
    check("test_nanos_per_milli", 1000000L);
    check("test_nanos_per_second", 1000000000L);
    check("test_seconds_per_minute", 60L);
    check("test_minutes_per_hour", 60L);
    check("test_seconds_per_hour", 3600L);
    check("test_hours_per_day", 24L);
    check("test_seconds_per_day", 86400L);
}

// ═════════════════════════════════════════════════════════════════════════════
// 3. Checked arithmetic: add, subtract, multiply, negate, divide
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Checked arithmetic: addChecked normal and overflow", "[libk][time][arithmetic]") {
    auto jit = jit_k(R"SRC(
        module __test_add_checked__;

        test_add_normal() : long throws(k::time::TemporalArithmeticException) {
            return k::time::addChecked(100L, 200L);
        }

        test_add_overflow() : int {
            try {
                k::time::addChecked(9223372036854775807L, 1L);
                return 0; // should not reach
            } catch (e: k::time::TemporalArithmeticException&) {
                return 1;
            }
        }

        test_add_underflow() : int {
            try {
                k::time::addChecked((-9223372036854775807L - 1L), -1L);
                return 0; // should not reach
            } catch (e: k::time::TemporalArithmeticException&) {
                return 1;
            }
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn_norm = jit->lookup_symbol<int64_t(*)()>("test_add_normal");
    REQUIRE(fn_norm != nullptr);
    CHECK(fn_norm() == 300L);

    auto fn_of = jit->lookup_symbol<int(*)()>("test_add_overflow");
    REQUIRE(fn_of != nullptr);
    CHECK(fn_of() == 1);

    auto fn_uf = jit->lookup_symbol<int(*)()>("test_add_underflow");
    REQUIRE(fn_uf != nullptr);
    CHECK(fn_uf() == 1);
}

TEST_CASE("Checked arithmetic: subtractChecked normal and overflow", "[libk][time][arithmetic]") {
    auto jit = jit_k(R"SRC(
        module __test_sub_checked__;

        test_sub_normal() : long throws(k::time::TemporalArithmeticException) {
            return k::time::subtractChecked(500L, 200L);
        }

        test_sub_underflow() : int {
            try {
                k::time::subtractChecked((-9223372036854775807L - 1L), 1L);
                return 0;
            } catch (e: k::time::TemporalArithmeticException&) {
                return 1;
            }
        }

        test_sub_overflow() : int {
            try {
                k::time::subtractChecked(9223372036854775807L, -1L);
                return 0;
            } catch (e: k::time::TemporalArithmeticException&) {
                return 1;
            }
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn_norm = jit->lookup_symbol<int64_t(*)()>("test_sub_normal");
    REQUIRE(fn_norm != nullptr);
    CHECK(fn_norm() == 300L);

    auto fn_uf = jit->lookup_symbol<int(*)()>("test_sub_underflow");
    REQUIRE(fn_uf != nullptr);
    CHECK(fn_uf() == 1);

    auto fn_of = jit->lookup_symbol<int(*)()>("test_sub_overflow");
    REQUIRE(fn_of != nullptr);
    CHECK(fn_of() == 1);
}

TEST_CASE("Checked arithmetic: multiplyChecked normal and overflow", "[libk][time][arithmetic]") {
    auto jit = jit_k(R"SRC(
        module __test_mul_checked__;

        test_mul_normal() : long throws(k::time::TemporalArithmeticException) {
            return k::time::multiplyChecked(25L, 4L);
        }

        test_mul_zero() : long throws(k::time::TemporalArithmeticException) {
            return k::time::multiplyChecked(0L, 9223372036854775807L);
        }

        test_mul_overflow_pos() : int {
            try {
                k::time::multiplyChecked(9223372036854775807L, 2L);
                return 0;
            } catch (e: k::time::TemporalArithmeticException&) {
                return 1;
            }
        }

        test_mul_overflow_neg() : int {
            try {
                k::time::multiplyChecked((-9223372036854775807L - 1L), -1L);
                return 0;
            } catch (e: k::time::TemporalArithmeticException&) {
                return 1;
            }
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn_norm = jit->lookup_symbol<int64_t(*)()>("test_mul_normal");
    REQUIRE(fn_norm != nullptr);
    CHECK(fn_norm() == 100L);

    auto fn_zero = jit->lookup_symbol<int64_t(*)()>("test_mul_zero");
    REQUIRE(fn_zero != nullptr);
    CHECK(fn_zero() == 0L);

    auto fn_of_pos = jit->lookup_symbol<int(*)()>("test_mul_overflow_pos");
    REQUIRE(fn_of_pos != nullptr);
    CHECK(fn_of_pos() == 1);

    auto fn_of_neg = jit->lookup_symbol<int(*)()>("test_mul_overflow_neg");
    REQUIRE(fn_of_neg != nullptr);
    CHECK(fn_of_neg() == 1);
}

TEST_CASE("Checked arithmetic: negateChecked normal and overflow", "[libk][time][arithmetic]") {
    auto jit = jit_k(R"SRC(
        module __test_neg_checked__;

        test_neg_normal() : long throws(k::time::TemporalArithmeticException) {
            return k::time::negateChecked(42L);
        }

        test_neg_zero() : long throws(k::time::TemporalArithmeticException) {
            return k::time::negateChecked(0L);
        }

        test_neg_overflow() : int {
            try {
                k::time::negateChecked((-9223372036854775807L - 1L));
                return 0;
            } catch (e: k::time::TemporalArithmeticException&) {
                return 1;
            }
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn_norm = jit->lookup_symbol<int64_t(*)()>("test_neg_normal");
    REQUIRE(fn_norm != nullptr);
    CHECK(fn_norm() == -42L);

    auto fn_zero = jit->lookup_symbol<int64_t(*)()>("test_neg_zero");
    REQUIRE(fn_zero != nullptr);
    CHECK(fn_zero() == 0L);

    auto fn_of = jit->lookup_symbol<int(*)()>("test_neg_overflow");
    REQUIRE(fn_of != nullptr);
    CHECK(fn_of() == 1);
}

TEST_CASE("Checked arithmetic: divideChecked normal, div-by-zero, overflow", "[libk][time][arithmetic]") {
    auto jit = jit_k(R"SRC(
        module __test_div_checked__;

        test_div_normal() : long throws(k::time::TemporalArithmeticException) {
            return k::time::divideChecked(100L, 5L);
        }

        test_div_zero() : int {
            try {
                k::time::divideChecked(42L, 0L);
                return 0;
            } catch (e: k::time::TemporalArithmeticException&) {
                return 1;
            }
        }

        test_div_overflow() : int {
            try {
                k::time::divideChecked((-9223372036854775807L - 1L), -1L);
                return 0;
            } catch (e: k::time::TemporalArithmeticException&) {
                return 1;
            }
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn_norm = jit->lookup_symbol<int64_t(*)()>("test_div_normal");
    REQUIRE(fn_norm != nullptr);
    CHECK(fn_norm() == 20L);

    auto fn_zero = jit->lookup_symbol<int(*)()>("test_div_zero");
    REQUIRE(fn_zero != nullptr);
    CHECK(fn_zero() == 1);

    auto fn_of = jit->lookup_symbol<int(*)()>("test_div_overflow");
    REQUIRE(fn_of != nullptr);
    CHECK(fn_of() == 1);
}

TEST_CASE("Checked arithmetic: floorDiv and floorMod all quadrants", "[libk][time][arithmetic]") {
    auto jit = jit_k(R"SRC(
        module __test_floor_div_mod__;

        test_div_pos_pos() : long { return k::time::floorDiv(7L, 3L); }
        test_mod_pos_pos() : long { return k::time::floorMod(7L, 3L); }

        test_div_neg_pos() : long { return k::time::floorDiv(-7L, 3L); }
        test_mod_neg_pos() : long { return k::time::floorMod(-7L, 3L); }

        test_div_pos_neg() : long { return k::time::floorDiv(7L, -3L); }
        test_mod_pos_neg() : long { return k::time::floorMod(7L, -3L); }

        test_div_neg_neg() : long { return k::time::floorDiv(-7L, -3L); }
        test_mod_neg_neg() : long { return k::time::floorMod(-7L, -3L); }

        test_div_exact_neg() : long { return k::time::floorDiv(-6L, 3L); }
        test_mod_exact_neg() : long { return k::time::floorMod(-6L, 3L); }
    )SRC");
    REQUIRE(jit != nullptr);

    auto check = [&](const char* sym, int64_t expected) {
        auto fn = jit->lookup_symbol<int64_t(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == expected);
    };

    check("test_div_pos_pos", 2L);
    check("test_mod_pos_pos", 1L);

    check("test_div_neg_pos", -3L);
    check("test_mod_neg_pos", 2L);

    check("test_div_pos_neg", -3L);
    check("test_mod_pos_neg", -2L);

    check("test_div_neg_neg", 2L);
    check("test_mod_neg_neg", -1L);

    check("test_div_exact_neg", -2L);
    check("test_mod_exact_neg", 0L);
}

TEST_CASE("CheckedArithmetic class static helpers", "[libk][time][arithmetic]") {
    auto jit = jit_k(R"SRC(
        module __test_checked_arith_class__;

        test_class_add() : long throws(k::time::TemporalArithmeticException) {
            return k::time::CheckedArithmetic::addChecked(10L, 20L);
        }

        test_class_sub() : long throws(k::time::TemporalArithmeticException) {
            return k::time::CheckedArithmetic::subtractChecked(50L, 15L);
        }

        test_class_floor_div() : long {
            return k::time::CheckedArithmetic::floorDiv(-5L, 2L);
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn_add = jit->lookup_symbol<int64_t(*)()>("test_class_add");
    REQUIRE(fn_add != nullptr);
    CHECK(fn_add() == 30L);

    auto fn_sub = jit->lookup_symbol<int64_t(*)()>("test_class_sub");
    REQUIRE(fn_sub != nullptr);
    CHECK(fn_sub() == 35L);

    auto fn_div = jit->lookup_symbol<int64_t(*)()>("test_class_floor_div");
    REQUIRE(fn_div != nullptr);
    CHECK(fn_div() == -3L);
}

// ═════════════════════════════════════════════════════════════════════════════
// 4. NormalizedSecondNanos
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("NormalizedSecondNanos: normal, rollover, negative adjust", "[libk][time][normalized]") {
    auto jit = jit_k(R"SRC(
        module __test_normalized_pair__;

        test_default() : bool {
            p : k::time::NormalizedSecondNanos;
            return p.seconds == 0L && p.nanos == 0;
        }

        test_in_range() : bool throws(k::time::TemporalArithmeticException) {
            p : k::time::NormalizedSecondNanos = k::time::NormalizedSecondNanos(10L, 500);
            return p.getSeconds() == 10L && p.getNanos() == 500;
        }

        test_positive_rollover() : bool throws(k::time::TemporalArithmeticException) {
            // 1_000_000_005 nanos -> +1 second, 5 nanos
            p : k::time::NormalizedSecondNanos = k::time::NormalizedSecondNanos(10L, 1000000005L);
            return p.seconds == 11L && p.nanos == 5;
        }

        test_negative_rollover() : bool throws(k::time::TemporalArithmeticException) {
            // -1 nano -> -1 second, 999_999_999 nanos
            p : k::time::NormalizedSecondNanos = k::time::NormalizedSecondNanos(10L, -1L);
            return p.seconds == 9L && p.nanos == 999999999;
        }

        test_exact_boundary_pos() : bool throws(k::time::TemporalArithmeticException) {
            // exactly 1_000_000_000 nanos -> +1 second, 0 nanos
            p : k::time::NormalizedSecondNanos = k::time::NormalizedSecondNanos(0L, 1000000000L);
            return p.seconds == 1L && p.nanos == 0;
        }

        test_exact_boundary_neg() : bool throws(k::time::TemporalArithmeticException) {
            // exactly -1_000_000_000 nanos -> -1 second, 0 nanos
            p : k::time::NormalizedSecondNanos = k::time::NormalizedSecondNanos(0L, -1000000000L);
            return p.seconds == -1L && p.nanos == 0;
        }

        test_multi_second_negative() : bool throws(k::time::TemporalArithmeticException) {
            // -2_000_000_001 nanos -> floorDiv is -3, floorMod is 999_999_999
            p : k::time::NormalizedSecondNanos = k::time::NormalizedSecondNanos(0L, -2000000001L);
            return p.seconds == -3L && p.nanos == 999999999;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto check = [&](const char* sym) {
        auto fn = jit->lookup_symbol<bool(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == true);
    };

    check("test_default");
    check("test_in_range");
    check("test_positive_rollover");
    check("test_negative_rollover");
    check("test_exact_boundary_pos");
    check("test_exact_boundary_neg");
    check("test_multi_second_negative");
}

TEST_CASE("NormalizedSecondNanos: overflow during seconds adjust throws", "[libk][time][normalized]") {
    auto jit = jit_k(R"SRC(
        module __test_normalized_overflow__;

        test_overflow_pos() : int {
            try {
                p : k::time::NormalizedSecondNanos = k::time::NormalizedSecondNanos(9223372036854775807L, 1000000000L);
                return 0;
            } catch (e: k::time::TemporalArithmeticException&) {
                return 1;
            }
        }

        test_overflow_neg() : int {
            try {
                p : k::time::NormalizedSecondNanos = k::time::NormalizedSecondNanos((-9223372036854775807L - 1L), -1L);
                return 0;
            } catch (e: k::time::TemporalArithmeticException&) {
                return 1;
            }
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn_pos = jit->lookup_symbol<int(*)()>("test_overflow_pos");
    REQUIRE(fn_pos != nullptr);
    CHECK(fn_pos() == 1);

    auto fn_neg = jit->lookup_symbol<int(*)()>("test_overflow_neg");
    REQUIRE(fn_neg != nullptr);
    CHECK(fn_neg() == 1);
}

TEST_CASE("NormalizedSecondNanos: comparisons", "[libk][time][normalized]") {
    auto jit = jit_k(R"SRC(
        module __test_normalized_cmp__;

        test_eq() : bool throws(k::time::TemporalArithmeticException) {
            a : k::time::NormalizedSecondNanos = k::time::NormalizedSecondNanos(5L, 500);
            b : k::time::NormalizedSecondNanos = k::time::NormalizedSecondNanos(5L, 500);
            return a == b;
        }

        test_ne() : bool throws(k::time::TemporalArithmeticException) {
            a : k::time::NormalizedSecondNanos = k::time::NormalizedSecondNanos(5L, 500);
            b : k::time::NormalizedSecondNanos = k::time::NormalizedSecondNanos(5L, 501);
            return a != b;
        }

        test_lt() : bool throws(k::time::TemporalArithmeticException) {
            a : k::time::NormalizedSecondNanos = k::time::NormalizedSecondNanos(5L, 100);
            b : k::time::NormalizedSecondNanos = k::time::NormalizedSecondNanos(5L, 200);
            c : k::time::NormalizedSecondNanos = k::time::NormalizedSecondNanos(6L, 0);
            return (a < b) && (b < c) && (a <= b) && (b <= b) && (c > b) && (c >= b);
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn_eq = jit->lookup_symbol<bool(*)()>("test_eq");
    REQUIRE(fn_eq != nullptr);
    CHECK(fn_eq() == true);

    auto fn_ne = jit->lookup_symbol<bool(*)()>("test_ne");
    REQUIRE(fn_ne != nullptr);
    CHECK(fn_ne() == true);

    auto fn_lt = jit->lookup_symbol<bool(*)()>("test_lt");
    REQUIRE(fn_lt != nullptr);
    CHECK(fn_lt() == true);
}

// ═════════════════════════════════════════════════════════════════════════════
// 5. GregorianMath: leap years, month length, day of year
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("GregorianMath: isLeapYear rules", "[libk][time][gregorian]") {
    auto jit = jit_k(R"SRC(
        module __test_leap_years__;

        test_2024() : bool { return k::time::GregorianMath::isLeapYear(2024L); }
        test_2023() : bool { return k::time::GregorianMath::isLeapYear(2023L); }
        test_2000() : bool { return k::time::GregorianMath::isLeapYear(2000L); }
        test_1900() : bool { return k::time::GregorianMath::isLeapYear(1900L); }
        test_2100() : bool { return k::time::GregorianMath::isLeapYear(2100L); }
        test_1600() : bool { return k::time::GregorianMath::isLeapYear(1600L); }
        test_year0() : bool { return k::time::GregorianMath::isLeapYear(0L); }
        test_neg1() : bool { return k::time::GregorianMath::isLeapYear(-1L); }
        test_neg4() : bool { return k::time::GregorianMath::isLeapYear(-4L); }
        test_neg100() : bool { return k::time::GregorianMath::isLeapYear(-100L); }
        test_neg400() : bool { return k::time::GregorianMath::isLeapYear(-400L); }
    )SRC");
    REQUIRE(jit != nullptr);

    auto check = [&](const char* sym, bool expected) {
        auto fn = jit->lookup_symbol<bool(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == expected);
    };

    check("test_2024", true);
    check("test_2023", false);
    check("test_2000", true);
    check("test_1900", false);
    check("test_2100", false);
    check("test_1600", true);
    check("test_year0", true);
    check("test_neg1", false);
    check("test_neg4", true);
    check("test_neg100", false);
    check("test_neg400", true);
}

TEST_CASE("GregorianMath: lengthOfMonth and lengthOfYear", "[libk][time][gregorian]") {
    auto jit = jit_k(R"SRC(
        module __test_month_lengths__;

        test_jan() : int { return k::time::GregorianMath::lengthOfMonth(2024L, 1); }
        test_feb_leap() : int { return k::time::GregorianMath::lengthOfMonth(2024L, 2); }
        test_feb_common() : int { return k::time::GregorianMath::lengthOfMonth(2023L, 2); }
        test_apr() : int { return k::time::GregorianMath::lengthOfMonth(2024L, 4); }
        test_jul() : int { return k::time::GregorianMath::lengthOfMonth(2024L, 7); }
        test_nov() : int { return k::time::GregorianMath::lengthOfMonth(2024L, 11); }
        test_dec() : int { return k::time::GregorianMath::lengthOfMonth(2024L, 12); }

        test_year_leap() : int { return k::time::GregorianMath::lengthOfYear(2024L); }
        test_year_common() : int { return k::time::GregorianMath::lengthOfYear(2023L); }
    )SRC");
    REQUIRE(jit != nullptr);

    auto check = [&](const char* sym, int expected) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == expected);
    };

    check("test_jan", 31);
    check("test_feb_leap", 29);
    check("test_feb_common", 28);
    check("test_apr", 30);
    check("test_jul", 31);
    check("test_nov", 30);
    check("test_dec", 31);

    check("test_year_leap", 366);
    check("test_year_common", 365);
}

TEST_CASE("GregorianMath: dayOfYear", "[libk][time][gregorian]") {
    auto jit = jit_k(R"SRC(
        module __test_day_of_year__;

        test_jan1() : int { return k::time::GregorianMath::dayOfYear(2024L, 1, 1); }
        test_feb29() : int { return k::time::GregorianMath::dayOfYear(2024L, 2, 29); }
        test_mar1_leap() : int { return k::time::GregorianMath::dayOfYear(2024L, 3, 1); }
        test_mar1_common() : int { return k::time::GregorianMath::dayOfYear(2023L, 3, 1); }
        test_dec31_leap() : int { return k::time::GregorianMath::dayOfYear(2024L, 12, 31); }
        test_dec31_common() : int { return k::time::GregorianMath::dayOfYear(2023L, 12, 31); }
    )SRC");
    REQUIRE(jit != nullptr);

    auto check = [&](const char* sym, int expected) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == expected);
    };

    check("test_jan1", 1);
    check("test_feb29", 60);
    check("test_mar1_leap", 61);
    check("test_mar1_common", 60);
    check("test_dec31_leap", 366);
    check("test_dec31_common", 365);
}

// ═════════════════════════════════════════════════════════════════════════════
// 6. GregorianMath & EpochDay: date <-> epochDay conversion round-trips
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("GregorianMath: dateToEpochDay known values", "[libk][time][gregorian]") {
    auto jit = jit_k(R"SRC(
        module __test_date_to_epoch_day__;

        test_epoch() : long throws(k::time::TemporalArithmeticException) {
            return k::time::GregorianMath::dateToEpochDay(1970L, 1, 1);
        }

        test_epoch_plus1() : long throws(k::time::TemporalArithmeticException) {
            return k::time::GregorianMath::dateToEpochDay(1970L, 1, 2);
        }

        test_epoch_minus1() : long throws(k::time::TemporalArithmeticException) {
            return k::time::GregorianMath::dateToEpochDay(1969L, 12, 31);
        }

        test_y2000() : long throws(k::time::TemporalArithmeticException) {
            return k::time::GregorianMath::dateToEpochDay(2000L, 1, 1);
        }

        test_leap_1600() : long throws(k::time::TemporalArithmeticException) {
            return k::time::GregorianMath::dateToEpochDay(1600L, 2, 29);
        }

        test_year0() : long throws(k::time::TemporalArithmeticException) {
            return k::time::GregorianMath::dateToEpochDay(0L, 1, 1);
        }

        test_bce1() : long throws(k::time::TemporalArithmeticException) {
            return k::time::GregorianMath::dateToEpochDay(-1L, 12, 31);
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto check = [&](const char* sym, int64_t expected) {
        auto fn = jit->lookup_symbol<int64_t(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == expected);
    };

    check("test_epoch", 0L);
    check("test_epoch_plus1", 1L);
    check("test_epoch_minus1", -1L);
    check("test_y2000", 10957L);
    check("test_leap_1600", -135081L);
    check("test_year0", -719528L);
    check("test_bce1", -719529L);
}

TEST_CASE("GregorianMath: epochDayToDate and round-trip", "[libk][time][gregorian]") {
    auto jit = jit_k(R"SRC(
        module __test_epoch_day_round_trip__;

        test_round_trip(y: long, m: int, d: int) : bool throws(k::time::TemporalArithmeticException) {
            ed : long = k::time::GregorianMath::dateToEpochDay(y, m, d);
            ymd : k::time::YearMonthDay = k::time::GregorianMath::epochDayToDate(ed);
            return ymd.year == y && ymd.month == m && ymd.day == d;
        }

        test_suite() : bool throws(k::time::TemporalArithmeticException) {
            ok : bool = true;
            ok = ok && test_round_trip(1970L, 1, 1);
            ok = ok && test_round_trip(1970L, 1, 2);
            ok = ok && test_round_trip(1969L, 12, 31);
            ok = ok && test_round_trip(2000L, 1, 1);
            ok = ok && test_round_trip(2000L, 2, 29);
            ok = ok && test_round_trip(2024L, 2, 29);
            ok = ok && test_round_trip(1600L, 2, 29);
            ok = ok && test_round_trip(1900L, 2, 28);
            ok = ok && test_round_trip(0L, 1, 1);
            ok = ok && test_round_trip(-1L, 12, 31);
            ok = ok && test_round_trip(-4L, 2, 29);
            ok = ok && test_round_trip(-400L, 2, 29);
            ok = ok && test_round_trip(2026L, 9, 6);
            return ok;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<bool(*)()>("test_suite");
    REQUIRE(fn != nullptr);
    CHECK(fn() == true);
}

// ═════════════════════════════════════════════════════════════════════════════
// 7. EpochDay struct: operations, comparison, arithmetic
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("EpochDay: construction and daysSinceEpoch", "[libk][time][epoch_day]") {
    auto jit = jit_k(R"SRC(
        module __test_epoch_day_basics__;

        test_default() : long {
            ed : k::time::EpochDay;
            return ed.daysSinceEpoch();
        }

        test_of_days() : long {
            ed : k::time::EpochDay = k::time::EpochDay::ofDaysSinceEpoch(12345L);
            return ed.daysSinceEpoch();
        }

        test_from_date() : long throws(k::time::TemporalArithmeticException) {
            ed : k::time::EpochDay = k::time::EpochDay::fromDate(2000L, 1, 1);
            return ed.daysSinceEpoch();
        }

        test_to_date() : bool throws(k::time::TemporalArithmeticException) {
            ed : k::time::EpochDay = k::time::EpochDay::ofDaysSinceEpoch(0L);
            ymd : k::time::YearMonthDay = ed.toDate();
            return ymd.getYear() == 1970L && ymd.getMonth() == 1 && ymd.getDay() == 1;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn_def = jit->lookup_symbol<int64_t(*)()>("test_default");
    REQUIRE(fn_def != nullptr);
    CHECK(fn_def() == 0L);

    auto fn_of = jit->lookup_symbol<int64_t(*)()>("test_of_days");
    REQUIRE(fn_of != nullptr);
    CHECK(fn_of() == 12345L);

    auto fn_from_date = jit->lookup_symbol<int64_t(*)()>("test_from_date");
    REQUIRE(fn_from_date != nullptr);
    CHECK(fn_from_date() == 10957L);

    auto fn_to_date = jit->lookup_symbol<bool(*)()>("test_to_date");
    REQUIRE(fn_to_date != nullptr);
    CHECK(fn_to_date() == true);
}

TEST_CASE("EpochDay: arithmetic plusDays, minusDays, operators", "[libk][time][epoch_day]") {
    auto jit = jit_k(R"SRC(
        module __test_epoch_day_arithmetic__;

        test_plus_days() : long throws(k::time::TemporalArithmeticException) {
            ed : k::time::EpochDay = k::time::EpochDay(100L);
            r : k::time::EpochDay = ed.plusDays(50L);
            return r.daysSinceEpoch();
        }

        test_minus_days() : long throws(k::time::TemporalArithmeticException) {
            ed : k::time::EpochDay = k::time::EpochDay(100L);
            r : k::time::EpochDay = ed.minusDays(30L);
            return r.daysSinceEpoch();
        }

        test_operator_plus() : long throws(k::time::TemporalArithmeticException) {
            ed : k::time::EpochDay = k::time::EpochDay(100L);
            r : k::time::EpochDay = ed + 25L;
            return r.daysSinceEpoch();
        }

        test_operator_minus_days() : long throws(k::time::TemporalArithmeticException) {
            ed : k::time::EpochDay = k::time::EpochDay(100L);
            r : k::time::EpochDay = ed - 40L;
            return r.daysSinceEpoch();
        }

        test_operator_minus_epoch_day() : long throws(k::time::TemporalArithmeticException) {
            a : k::time::EpochDay = k::time::EpochDay(150L);
            b : k::time::EpochDay = k::time::EpochDay(50L);
            return a - b;
        }

        test_plus_overflow() : int {
            try {
                ed : k::time::EpochDay = k::time::EpochDay(9223372036854775807L);
                r : k::time::EpochDay = ed + 1L;
                return 0;
            } catch (e: k::time::TemporalArithmeticException&) {
                return 1;
            }
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto check_l = [&](const char* sym, int64_t expected) {
        auto fn = jit->lookup_symbol<int64_t(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == expected);
    };

    check_l("test_plus_days", 150L);
    check_l("test_minus_days", 70L);
    check_l("test_operator_plus", 125L);
    check_l("test_operator_minus_days", 60L);
    check_l("test_operator_minus_epoch_day", 100L);

    auto fn_of = jit->lookup_symbol<int(*)()>("test_plus_overflow");
    REQUIRE(fn_of != nullptr);
    CHECK(fn_of() == 1);
}

TEST_CASE("EpochDay: comparisons and compareTo", "[libk][time][epoch_day]") {
    auto jit = jit_k(R"SRC(
        module __test_epoch_day_comparisons__;

        test_compare_to() : bool {
            a : k::time::EpochDay = k::time::EpochDay(10L);
            b : k::time::EpochDay = k::time::EpochDay(20L);
            c : k::time::EpochDay = k::time::EpochDay(10L);
            return (a.compareTo(b) < 0) && (b.compareTo(a) > 0) && (a.compareTo(c) == 0);
        }

        test_operators() : bool {
            a : k::time::EpochDay = k::time::EpochDay(10L);
            b : k::time::EpochDay = k::time::EpochDay(20L);
            c : k::time::EpochDay = k::time::EpochDay(10L);
            return (a == c) && (a != b) && (a < b) && (a <= b) && (a <= c) && (b > a) && (b >= a) && (a >= c);
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn_cmp = jit->lookup_symbol<bool(*)()>("test_compare_to");
    REQUIRE(fn_cmp != nullptr);
    CHECK(fn_cmp() == true);

    auto fn_ops = jit->lookup_symbol<bool(*)()>("test_operators");
    REQUIRE(fn_ops != nullptr);
    CHECK(fn_ops() == true);
}
