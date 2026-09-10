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

// ═════════════════════════════════════════════════════════════════════════════
// 8. Duration factories and normalization
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Duration: factories and normalization", "[libk][time][duration]") {
    auto jit = jit_k(R"SRC(
        module __test_duration_factories__;

        test_zero() : bool {
            d : k::time::Duration = k::time::Duration::zero();
            return d.secondsPart() == 0L && d.nanoAdjustment() == 0 && d.isZero() && !d.isNegative();
        }

        test_of_nanos() : int {
            d1 : k::time::Duration = k::time::Duration::ofNanos(500L);
            if (d1.secondsPart() != 0L) return 10;
            if (d1.nanoAdjustment() != 500) return 11;
            d2 : k::time::Duration = k::time::Duration::ofNanos(1500000000L);
            if (d2.secondsPart() != 1L) return 20;
            if (d2.nanoAdjustment() != 500000000) return 21;
            d3 : k::time::Duration = k::time::Duration::ofNanos(-500L);
            if (d3.secondsPart() != -1L) return 30;
            if (d3.nanoAdjustment() != 999999500) return 31;
            if (!d3.isNegative()) return 32;
            d4 : k::time::Duration = k::time::Duration::ofNanos(-1500000000L);
            if (d4.secondsPart() != -2L) return 40;
            if (d4.nanoAdjustment() != 500000000) return 41;
            if (!d4.isNegative()) return 42;
            return 0;
        }

        test_of_micros() : int {
            try {
                d1 : k::time::Duration = k::time::Duration::ofMicros(123456L);
                if (d1.secondsPart() != 0L) return 10;
                if (d1.nanoAdjustment() != 123456000) return 11;
                d2 : k::time::Duration = k::time::Duration::ofMicros(1500000L);
                if (d2.secondsPart() != 1L) return 20;
                if (d2.nanoAdjustment() != 500000000) return 21;
                d3 : k::time::Duration = k::time::Duration::ofMicros(-500L);
                if (d3.secondsPart() != -1L) return 30;
                if (d3.nanoAdjustment() != 999500000) return 31;
                if (!d3.isNegative()) return 32;
                return 0;
            } catch (e: k::time::TemporalArithmeticException&) {
                return 99;
            }
        }

        test_of_millis() : int {
            try {
                d1 : k::time::Duration = k::time::Duration::ofMillis(500L);
                if (d1.secondsPart() != 0L) return 10;
                if (d1.nanoAdjustment() != 500000000) return 11;
                d2 : k::time::Duration = k::time::Duration::ofMillis(1500L);
                if (d2.secondsPart() != 1L) return 20;
                if (d2.nanoAdjustment() != 500000000) return 21;
                d3 : k::time::Duration = k::time::Duration::ofMillis(-500L);
                if (d3.secondsPart() != -1L) return 30;
                if (d3.nanoAdjustment() != 500000000) return 31;
                if (!d3.isNegative()) return 32;
                return 0;
            } catch (e: k::time::TemporalArithmeticException&) {
                return 99;
            }
        }

        test_of_seconds() : int {
            try {
                d1 : k::time::Duration = k::time::Duration::ofSeconds(42L);
                if (d1.secondsPart() != 42L) return 1;
                if (d1.nanoAdjustment() != 0) return 2;
                d2 : k::time::Duration = k::time::Duration::ofSeconds(-42L);
                if (d2.secondsPart() != -42L) return 3;
                if (d2.nanoAdjustment() != 0) return 4;
                if (!d2.isNegative()) return 5;
                d3 : k::time::Duration = k::time::Duration::ofSeconds(10L, 500000000);
                if (d3.secondsPart() != 10L) return 6;
                if (d3.nanoAdjustment() != 500000000) return 7;
                d4 : k::time::Duration = k::time::Duration::ofSeconds(10L, 1500000000);
                if (d4.secondsPart() != 11L) return 8;
                if (d4.nanoAdjustment() != 500000000) return 9;
                d5 : k::time::Duration = k::time::Duration::ofSeconds(10L, -500000000);
                if (d5.secondsPart() != 9L) return 10;
                if (d5.nanoAdjustment() != 500000000) return 11;
                d6 : k::time::Duration = k::time::Duration::ofSeconds(-10L, -500000000);
                if (d6.secondsPart() != -11L) return 12;
                if (d6.nanoAdjustment() != 500000000) return 13;
                if (!d6.isNegative()) return 14;
                return 0;
            } catch (e: k::time::TemporalArithmeticException&) {
                return 99;
            }
        }

        test_of_minutes_and_hours() : int {
            try {
                dm : k::time::Duration = k::time::Duration::ofMinutes(5L);
                dh : k::time::Duration = k::time::Duration::ofHours(2L);
                if (dm.secondsPart() != 300L) return 1;
                if (dm.nanoAdjustment() != 0) return 2;
                if (dh.secondsPart() != 7200L) return 3;
                if (dh.nanoAdjustment() != 0) return 4;
                return 0;
            } catch (e: k::time::TemporalArithmeticException&) {
                return 99;
            }
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto check_b = [&](const char* sym) {
        auto fn = jit->lookup_symbol<bool(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == true);
    };

    check_b("test_zero");
    auto fn_nanos = jit->lookup_symbol<int(*)()>("test_of_nanos");
    REQUIRE(fn_nanos != nullptr);
    CHECK(fn_nanos() == 0);
    auto fn_micros = jit->lookup_symbol<int(*)()>("test_of_micros");
    REQUIRE(fn_micros != nullptr);
    CHECK(fn_micros() == 0);
    auto fn_millis = jit->lookup_symbol<int(*)()>("test_of_millis");
    REQUIRE(fn_millis != nullptr);
    CHECK(fn_millis() == 0);
    auto fn_sec = jit->lookup_symbol<int(*)()>("test_of_seconds");
    REQUIRE(fn_sec != nullptr);
    CHECK(fn_sec() == 0);
    auto fn_mh = jit->lookup_symbol<int(*)()>("test_of_minutes_and_hours");
    REQUIRE(fn_mh != nullptr);
    CHECK(fn_mh() == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// 9. Duration conversions and unit conversions
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Duration: unit conversions and overflow", "[libk][time][duration]") {
    auto jit = jit_k(R"SRC(
        module __test_duration_conversions__;

        test_positive_conversions() : bool throws(k::time::TemporalArithmeticException) {
            d : k::time::Duration = k::time::Duration::ofSeconds(2L, 500000000);
            return d.secondsPart() == 2L &&
                   d.nanoAdjustment() == 500000000 &&
                   d.toSeconds() == 2L &&
                   d.toMillis() == 2500L &&
                   d.toMicros() == 2500000L &&
                   d.toNanosExact() == 2500000000L;
        }

        test_negative_conversions() : bool throws(k::time::TemporalArithmeticException) {
            d : k::time::Duration = k::time::Duration::ofMillis(-1500L);
            return d.secondsPart() == -2L &&
                   d.nanoAdjustment() == 500000000 &&
                   d.toSeconds() == -2L &&
                   d.toMillis() == -1500L &&
                   d.toMicros() == -1500000L &&
                   d.toNanosExact() == -1500000000L;
        }

        test_to_nanos_overflow() : int {
            try {
                d : k::time::Duration = k::time::Duration::ofSeconds(9223372036854775807L);
                d.toNanosExact();
                return 0;
            } catch (e: k::time::TemporalArithmeticException&) {
                return 1;
            }
        }

        test_to_micros_overflow() : int {
            try {
                d : k::time::Duration = k::time::Duration::ofSeconds(9223372036854775807L);
                d.toMicros();
                return 0;
            } catch (e: k::time::TemporalArithmeticException&) {
                return 1;
            }
        }

        test_to_millis_overflow() : int {
            try {
                d : k::time::Duration = k::time::Duration::ofSeconds(9223372036854775807L);
                d.toMillis();
                return 0;
            } catch (e: k::time::TemporalArithmeticException&) {
                return 1;
            }
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto check_b = [&](const char* sym) {
        auto fn = jit->lookup_symbol<bool(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == true);
    };

    check_b("test_positive_conversions");
    check_b("test_negative_conversions");

    auto check_i = [&](const char* sym, int expected) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == expected);
    };

    check_i("test_to_nanos_overflow", 1);
    check_i("test_to_micros_overflow", 1);
    check_i("test_to_millis_overflow", 1);
}

// ═════════════════════════════════════════════════════════════════════════════
// 10. Duration comparisons and compareTo
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Duration: comparisons and relational operators", "[libk][time][duration]") {
    auto jit = jit_k(R"SRC(
        module __test_duration_comparisons__;

        test_compare_to() : bool {
            d1 : k::time::Duration = k::time::Duration::ofSeconds(10L, 100);
            d2 : k::time::Duration = k::time::Duration::ofSeconds(10L, 200);
            d3 : k::time::Duration = k::time::Duration::ofSeconds(10L, 100);
            d_neg : k::time::Duration = k::time::Duration::ofSeconds(-1L, 0);

            return (d1.compareTo(d2) < 0) &&
                   (d2.compareTo(d1) > 0) &&
                   (d1.compareTo(d3) == 0) &&
                   (d_neg.compareTo(d1) < 0) &&
                   (d1.compareTo(d_neg) > 0);
        }

        test_operators() : bool {
            a : k::time::Duration = k::time::Duration::ofSeconds(5L, 500);
            b : k::time::Duration = k::time::Duration::ofSeconds(10L, 100);
            c : k::time::Duration = k::time::Duration::ofSeconds(5L, 500);
            neg : k::time::Duration = k::time::Duration::ofSeconds(-5L, 0);

            ok : bool = true;
            ok = ok && (a == c);
            ok = ok && (a != b);
            ok = ok && (a < b);
            ok = ok && (a <= b);
            ok = ok && (a <= c);
            ok = ok && (b > a);
            ok = ok && (b >= a);
            ok = ok && (a >= c);
            ok = ok && (neg < a);
            ok = ok && (a > neg);
            return ok;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto check_b = [&](const char* sym) {
        auto fn = jit->lookup_symbol<bool(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == true);
    };

    check_b("test_compare_to");
    check_b("test_operators");
}

// ═════════════════════════════════════════════════════════════════════════════
// 11. Duration arithmetic (plus, minus, negate, multipliedBy, dividedBy)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Duration: arithmetic plus and minus", "[libk][time][duration]") {
    auto jit = jit_k(R"SRC(
        module __test_duration_arithmetic_plus_minus__;

        test_plus_no_carry() : bool throws(k::time::TemporalArithmeticException) {
            a : k::time::Duration = k::time::Duration::ofSeconds(10L, 100);
            b : k::time::Duration = k::time::Duration::ofSeconds(20L, 200);
            r : k::time::Duration = a.plus(b);
            op : k::time::Duration = a + b;
            return r.secondsPart() == 30L && r.nanoAdjustment() == 300 && r == op;
        }

        test_plus_with_carry() : bool throws(k::time::TemporalArithmeticException) {
            a : k::time::Duration = k::time::Duration::ofSeconds(10L, 600000000);
            b : k::time::Duration = k::time::Duration::ofSeconds(20L, 700000000);
            r : k::time::Duration = a.plus(b);
            op : k::time::Duration = a + b;
            return r.secondsPart() == 31L && r.nanoAdjustment() == 300000000 && r == op;
        }

        test_minus_no_borrow() : bool throws(k::time::TemporalArithmeticException) {
            a : k::time::Duration = k::time::Duration::ofSeconds(30L, 300);
            b : k::time::Duration = k::time::Duration::ofSeconds(10L, 100);
            r : k::time::Duration = a.minus(b);
            op : k::time::Duration = a - b;
            return r.secondsPart() == 20L && r.nanoAdjustment() == 200 && r == op;
        }

        test_minus_with_borrow() : bool throws(k::time::TemporalArithmeticException) {
            a : k::time::Duration = k::time::Duration::ofSeconds(30L, 200000000);
            b : k::time::Duration = k::time::Duration::ofSeconds(10L, 500000000);
            r : k::time::Duration = a.minus(b);
            op : k::time::Duration = a - b;
            return r.secondsPart() == 19L && r.nanoAdjustment() == 700000000 && r == op;
        }

        test_plus_overflow() : int {
            try {
                a : k::time::Duration = k::time::Duration::ofSeconds(9223372036854775807L);
                b : k::time::Duration = k::time::Duration::ofSeconds(1L);
                r : k::time::Duration = a + b;
                return 0;
            } catch (e: k::time::TemporalArithmeticException&) {
                return 1;
            }
        }

        test_plus_carry_overflow() : int {
            try {
                a : k::time::Duration = k::time::Duration::ofSeconds(9223372036854775807L, 500000000);
                b : k::time::Duration = k::time::Duration::ofSeconds(0L, 600000000);
                r : k::time::Duration = a + b;
                return 0;
            } catch (e: k::time::TemporalArithmeticException&) {
                return 1;
            }
        }

        test_minus_underflow() : int {
            try {
                a : k::time::Duration = k::time::Duration::ofSeconds(-9223372036854775807L - 1L);
                b : k::time::Duration = k::time::Duration::ofSeconds(1L);
                r : k::time::Duration = a - b;
                return 0;
            } catch (e: k::time::TemporalArithmeticException&) {
                return 1;
            }
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto check_b = [&](const char* sym) {
        auto fn = jit->lookup_symbol<bool(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == true);
    };

    check_b("test_plus_no_carry");
    check_b("test_plus_with_carry");
    check_b("test_minus_no_borrow");
    check_b("test_minus_with_borrow");

    auto check_i = [&](const char* sym, int expected) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == expected);
    };

    check_i("test_plus_overflow", 1);
    check_i("test_plus_carry_overflow", 1);
    check_i("test_minus_underflow", 1);
}

TEST_CASE("Duration: negation, multipliedBy, dividedBy", "[libk][time][duration]") {
    auto jit = jit_k(R"SRC(
        module __test_duration_scaling__;

        test_negation() : bool throws(k::time::TemporalArithmeticException) {
            z : k::time::Duration = -k::time::Duration::zero();
            d1 : k::time::Duration = -k::time::Duration::ofSeconds(5L);
            d2 : k::time::Duration = -d1;
            d3 : k::time::Duration = -k::time::Duration::ofSeconds(5L, 200000000);
            d4 : k::time::Duration = -d3;

            return z == k::time::Duration::zero() &&
                   d1.secondsPart() == -5L && d1.nanoAdjustment() == 0 &&
                   d2.secondsPart() == 5L && d2.nanoAdjustment() == 0 &&
                   d3.secondsPart() == -6L && d3.nanoAdjustment() == 800000000 &&
                   d4.secondsPart() == 5L && d4.nanoAdjustment() == 200000000;
        }

        test_multiplied_by() : bool throws(k::time::TemporalArithmeticException) {
            d : k::time::Duration = k::time::Duration::ofSeconds(2L, 500000000); // 2.5s
            m0 : k::time::Duration = d.multipliedBy(0L);
            m1 : k::time::Duration = d.multipliedBy(1L);
            m_neg1 : k::time::Duration = d.multipliedBy(-1L);
            m3 : k::time::Duration = d * 3L; // 7.5s
            m_neg2 : k::time::Duration = d * -2L; // -5.0s

            return m0 == k::time::Duration::zero() &&
                   m1 == d &&
                   m_neg1 == -d &&
                   m3.secondsPart() == 7L && m3.nanoAdjustment() == 500000000 &&
                   m_neg2.secondsPart() == -5L && m_neg2.nanoAdjustment() == 0;
        }

        test_divided_by() : bool throws(k::time::TemporalArithmeticException) {
            d : k::time::Duration = k::time::Duration::ofSeconds(10L);
            d1 : k::time::Duration = d.dividedBy(1L);
            d_neg1 : k::time::Duration = d.dividedBy(-1L);
            d2 : k::time::Duration = d / 2L;

            half : k::time::Duration = k::time::Duration::ofSeconds(1L) / 2L;
            neg_half1 : k::time::Duration = k::time::Duration::ofSeconds(1L) / -2L;
            neg_half2 : k::time::Duration = k::time::Duration::ofSeconds(-1L) / 2L;
            pos_half : k::time::Duration = k::time::Duration::ofSeconds(-1L) / -2L;

            trunc_pos : k::time::Duration = k::time::Duration::ofNanos(5L) / 2L;
            trunc_neg : k::time::Duration = k::time::Duration::ofNanos(-5L) / 2L;
            trunc_zero1 : k::time::Duration = k::time::Duration::ofNanos(1L) / 2L;
            trunc_zero2 : k::time::Duration = k::time::Duration::ofNanos(-1L) / 2L;

            return d1 == d &&
                   d_neg1 == -d &&
                   d2.secondsPart() == 5L && d2.nanoAdjustment() == 0 &&
                   half.secondsPart() == 0L && half.nanoAdjustment() == 500000000 &&
                   neg_half1.secondsPart() == -1L && neg_half1.nanoAdjustment() == 500000000 &&
                   neg_half2.secondsPart() == -1L && neg_half2.nanoAdjustment() == 500000000 &&
                   pos_half.secondsPart() == 0L && pos_half.nanoAdjustment() == 500000000 &&
                   trunc_pos.secondsPart() == 0L && trunc_pos.nanoAdjustment() == 2 &&
                   trunc_neg.secondsPart() == -1L && trunc_neg.nanoAdjustment() == 999999998 &&
                   trunc_zero1.isZero() &&
                   trunc_zero2.isZero();
        }

        test_divide_by_zero() : int {
            try {
                d : k::time::Duration = k::time::Duration::ofSeconds(10L);
                d / 0L;
                return 0;
            } catch (e: k::time::TemporalArithmeticException&) {
                return 1;
            }
        }

        test_multiply_overflow() : int {
            try {
                d : k::time::Duration = k::time::Duration::ofSeconds(9223372036854775807L);
                d * 2L;
                return 0;
            } catch (e: k::time::TemporalArithmeticException&) {
                return 1;
            }
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto check_b = [&](const char* sym) {
        auto fn = jit->lookup_symbol<bool(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == true);
    };

    check_b("test_negation");
    check_b("test_multiplied_by");
    check_b("test_divided_by");

    auto check_i = [&](const char* sym, int expected) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == expected);
    };

    check_i("test_divide_by_zero", 1);
    check_i("test_multiply_overflow", 1);
}

// ═════════════════════════════════════════════════════════════════════════════
// 12. Instant factories and accessors
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Instant: factories and accessors", "[libk][time][instant]") {
    auto jit = jit_k(R"SRC(
        module __test_instant_factories__;

        test_epoch() : bool {
            i : k::time::Instant = k::time::Instant::epoch();
            return i.epochSeconds() == 0L && i.nanoAdjustment() == 0;
        }

        test_of_epoch_second() : bool throws(k::time::TemporalArithmeticException) {
            i1 : k::time::Instant = k::time::Instant::ofEpochSecond(100L);
            i2 : k::time::Instant = k::time::Instant::ofEpochSecond(100L, 500);
            i3 : k::time::Instant = k::time::Instant::ofEpochSecond(100L, 1500000000);
            i4 : k::time::Instant = k::time::Instant::ofEpochSecond(100L, -500000000);
            i5 : k::time::Instant = k::time::Instant::ofEpochSecond(-100L, -500000000);

            return i1.epochSeconds() == 100L && i1.nanoAdjustment() == 0 &&
                   i2.epochSeconds() == 100L && i2.nanoAdjustment() == 500 &&
                   i3.epochSeconds() == 101L && i3.nanoAdjustment() == 500000000 &&
                   i4.epochSeconds() == 99L && i4.nanoAdjustment() == 500000000 &&
                   i5.epochSeconds() == -101L && i5.nanoAdjustment() == 500000000;
        }

        test_of_epoch_second_overflow() : int {
            try {
                k::time::Instant::ofEpochSecond(9223372036854775807L, 1000000000);
                return 0;
            } catch (e: k::time::TemporalArithmeticException&) {
                return 1;
            }
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto check_b = [&](const char* sym) {
        auto fn = jit->lookup_symbol<bool(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == true);
    };

    check_b("test_epoch");
    check_b("test_of_epoch_second");

    auto fn_of = jit->lookup_symbol<int(*)()>("test_of_epoch_second_overflow");
    REQUIRE(fn_of != nullptr);
    CHECK(fn_of() == 1);
}

// ═════════════════════════════════════════════════════════════════════════════
// 13. Instant comparisons and ordering
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Instant: comparisons and ordering", "[libk][time][instant]") {
    auto jit = jit_k(R"SRC(
        module __test_instant_comparisons__;

        test_compare_to() : bool {
            i1 : k::time::Instant = k::time::Instant::ofEpochSecond(100L, 100);
            i2 : k::time::Instant = k::time::Instant::ofEpochSecond(100L, 200);
            i3 : k::time::Instant = k::time::Instant::ofEpochSecond(100L, 100);
            i_neg : k::time::Instant = k::time::Instant::ofEpochSecond(-1L, 0);

            return (i1.compareTo(i2) < 0) &&
                   (i2.compareTo(i1) > 0) &&
                   (i1.compareTo(i3) == 0) &&
                   (i_neg.compareTo(i1) < 0) &&
                   (i1.compareTo(i_neg) > 0);
        }

        test_operators() : bool {
            a : k::time::Instant = k::time::Instant::ofEpochSecond(5L, 500);
            b : k::time::Instant = k::time::Instant::ofEpochSecond(10L, 100);
            c : k::time::Instant = k::time::Instant::ofEpochSecond(5L, 500);
            before_epoch : k::time::Instant = k::time::Instant::ofEpochSecond(-5L, 0);

            ok : bool = true;
            ok = ok && (a == c);
            ok = ok && (a != b);
            ok = ok && (a < b);
            ok = ok && (a <= b);
            ok = ok && (a <= c);
            ok = ok && (b > a);
            ok = ok && (b >= a);
            ok = ok && (a >= c);
            ok = ok && (before_epoch < a);
            ok = ok && (a > before_epoch);
            return ok;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto check_b = [&](const char* sym) {
        auto fn = jit->lookup_symbol<bool(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == true);
    };

    check_b("test_compare_to");
    check_b("test_operators");
}

// ═════════════════════════════════════════════════════════════════════════════
// 14. Instant timeline arithmetic and identities
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Instant: timeline arithmetic, until, and identities", "[libk][time][instant]") {
    auto jit = jit_k(R"SRC(
        module __test_instant_arithmetic__;

        test_plus_minus_duration() : bool throws(k::time::TemporalArithmeticException) {
            start : k::time::Instant = k::time::Instant::ofEpochSecond(100L, 500000000);
            d : k::time::Duration = k::time::Duration::ofSeconds(50L, 600000000);

            // 100.5s + 50.6s = 151.1s
            end : k::time::Instant = start.plus(d);
            end_op : k::time::Instant = start + d;

            back : k::time::Instant = end.minus(d);
            back_op : k::time::Instant = end - d;

            return end.epochSeconds() == 151L && end.nanoAdjustment() == 100000000 &&
                   end == end_op &&
                   back == start &&
                   back_op == start;
        }

        test_minus_instant_and_until() : bool throws(k::time::TemporalArithmeticException) {
            start : k::time::Instant = k::time::Instant::ofEpochSecond(100L, 200);
            end : k::time::Instant = k::time::Instant::ofEpochSecond(150L, 500);

            elapsed : k::time::Duration = end.minus(start);
            elapsed_op : k::time::Duration = end - start;
            until_dur : k::time::Duration = start.until(end);

            return elapsed.secondsPart() == 50L && elapsed.nanoAdjustment() == 300 &&
                   elapsed == elapsed_op &&
                   until_dur == elapsed;
        }

        test_identities() : bool throws(k::time::TemporalArithmeticException) {
            instant : k::time::Instant = k::time::Instant::ofEpochSecond(123456789L, 987654321);
            duration : k::time::Duration = k::time::Duration::ofSeconds(42L, 123456789);

            // instant + duration - duration == instant
            id1 : bool = ((instant + duration) - duration) == instant;

            // end.minus(start) == duration
            end : k::time::Instant = instant + duration;
            id2 : bool = end.minus(instant) == duration;

            // until == end - start
            id3 : bool = instant.until(end) == (end - instant);

            return id1 && id2 && id3;
        }

        test_instant_plus_overflow() : int {
            try {
                i : k::time::Instant = k::time::Instant::ofEpochSecond(9223372036854775807L);
                d : k::time::Duration = k::time::Duration::ofSeconds(1L);
                r : k::time::Instant = i + d;
                return 0;
            } catch (e: k::time::TemporalArithmeticException&) {
                return 1;
            }
        }

        test_instant_minus_overflow() : int {
            try {
                i : k::time::Instant = k::time::Instant::ofEpochSecond(-9223372036854775807L - 1L);
                d : k::time::Duration = k::time::Duration::ofSeconds(1L);
                r : k::time::Instant = i - d;
                return 0;
            } catch (e: k::time::TemporalArithmeticException&) {
                return 1;
            }
        }

        test_instant_until_overflow() : int {
            try {
                start : k::time::Instant = k::time::Instant::ofEpochSecond(-9223372036854775807L - 1L);
                end : k::time::Instant = k::time::Instant::ofEpochSecond(9223372036854775807L);
                d : k::time::Duration = start.until(end);
                return 0;
            } catch (e: k::time::TemporalArithmeticException&) {
                return 1;
            }
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto check_b = [&](const char* sym) {
        auto fn = jit->lookup_symbol<bool(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == true);
    };

    check_b("test_plus_minus_duration");
    check_b("test_minus_instant_and_until");
    check_b("test_identities");

    auto check_i = [&](const char* sym, int expected) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == expected);
    };

    check_i("test_instant_plus_overflow", 1);
    check_i("test_instant_minus_overflow", 1);
    check_i("test_instant_until_overflow", 1);
}

// ═════════════════════════════════════════════════════════════════════════════
// 15. Period factories, components, arithmetic, negation, identities
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Period: factories, components, arithmetic, negation, identities", "[libk][time][period]") {
    auto jit = jit_k(R"SRC(
        module __test_period_basics__;

        test_factories_and_components() : bool {
            z : k::time::Period = k::time::Period::zero();
            if (!z.isZero()) return false;
            if (z.yearsPart() != 0L || z.monthsPart() != 0L || z.weeksPart() != 0L ||
                z.daysPart() != 0L || z.hoursPart() != 0L || z.minutesPart() != 0L ||
                z.secondsPart() != 0L) return false;

            p : k::time::Period = k::time::Period::of(1L, 2L, 3L, 4L, 5L, 6L, 7L);
            if (p.yearsPart() != 1L || p.monthsPart() != 2L || p.weeksPart() != 3L ||
                p.daysPart() != 4L || p.hoursPart() != 5L || p.minutesPart() != 6L ||
                p.secondsPart() != 7L) return false;
            if (p.isZero()) return false;

            y : k::time::Period = k::time::Period::years(10L);
            if (y.yearsPart() != 10L || y.monthsPart() != 0L) return false;

            m : k::time::Period = k::time::Period::months(11L);
            if (m.monthsPart() != 11L || m.yearsPart() != 0L) return false;

            w : k::time::Period = k::time::Period::weeks(12L);
            if (w.weeksPart() != 12L || w.daysPart() != 0L) return false;

            d : k::time::Period = k::time::Period::days(13L);
            if (d.daysPart() != 13L || d.weeksPart() != 0L) return false;

            h : k::time::Period = k::time::Period::hours(14L);
            if (h.hoursPart() != 14L || h.minutesPart() != 0L) return false;

            min : k::time::Period = k::time::Period::minutes(15L);
            if (min.minutesPart() != 15L || min.secondsPart() != 0L) return false;

            s : k::time::Period = k::time::Period::seconds(16L);
            if (s.secondsPart() != 16L || s.hoursPart() != 0L) return false;

            return true;
        }

        test_arithmetic_and_negation() : bool throws(k::time::TemporalArithmeticException) {
            p1 : k::time::Period = k::time::Period::of(1L, 2L, 3L, 4L, 5L, 6L, 7L);
            p2 : k::time::Period = k::time::Period::of(2L, 3L, 4L, 5L, 6L, 7L, 8L);

            sum : k::time::Period = p1.plus(p2);
            sum_op : k::time::Period = p1 + p2;
            exp_sum : k::time::Period = k::time::Period::of(3L, 5L, 7L, 9L, 11L, 13L, 15L);
            if (sum != exp_sum || sum_op != exp_sum) return false;

            diff : k::time::Period = p1.minus(p2);
            diff_op : k::time::Period = p1 - p2;
            exp_diff : k::time::Period = k::time::Period::of(-1L, -1L, -1L, -1L, -1L, -1L, -1L);
            if (diff != exp_diff || diff_op != exp_diff) return false;

            neg : k::time::Period = p1.negated();
            neg_op : k::time::Period = -p1;
            exp_neg : k::time::Period = k::time::Period::of(-1L, -2L, -3L, -4L, -5L, -6L, -7L);
            if (neg != exp_neg || neg_op != exp_neg) return false;

            return true;
        }

        test_identities() : bool throws(k::time::TemporalArithmeticException) {
            p : k::time::Period = k::time::Period::of(10L, 20L, 30L, 40L, 50L, 60L, 70L);
            zero : k::time::Period = k::time::Period::zero();

            id1 : bool = (p + zero) == p;
            id2 : bool = (p - p) == zero;
            id3 : bool = (p + (-p)) == zero;
            id4 : bool = (-(-p)) == p;
            id5 : bool = (p == p);
            id6 : bool = (p != zero);

            return id1 && id2 && id3 && id4 && id5 && id6;
        }

        test_plus_overflow() : int {
            try {
                p1 : k::time::Period = k::time::Period::years(9223372036854775807L);
                p2 : k::time::Period = k::time::Period::years(1L);
                r : k::time::Period = p1 + p2;
                return 0;
            } catch (e: k::time::TemporalArithmeticException&) {
                return 1;
            }
        }

        test_negate_overflow() : int {
            try {
                p : k::time::Period = k::time::Period::days(-9223372036854775807L - 1L);
                r : k::time::Period = -p;
                return 0;
            } catch (e: k::time::TemporalArithmeticException&) {
                return 1;
            }
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto check_b = [&](const char* sym) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() != 0);
    };

    check_b("test_factories_and_components");
    check_b("test_arithmetic_and_negation");
    check_b("test_identities");

    auto check_i = [&](const char* sym, int expected) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == expected);
    };

    check_i("test_plus_overflow", 1);
    check_i("test_negate_overflow", 1);
}

// ═════════════════════════════════════════════════════════════════════════════
// 16. LocalTime validation and factories
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("LocalTime: validation, factories, and midnight", "[libk][time][local_time]") {
    auto jit = jit_k(R"SRC(
        module __test_local_time_validation__;

        test_valid_factories() : bool throws(k::time::InvalidTemporalValueException) {
            m : k::time::LocalTime = k::time::LocalTime::midnight();
            if (m.hour() != 0 || m.minute() != 0 || m.second() != 0 || m.nano() != 0) return false;

            t2 : k::time::LocalTime = k::time::LocalTime::of(14, 30);
            if (t2.hour() != 14 || t2.minute() != 30 || t2.second() != 0 || t2.nano() != 0) return false;

            t4 : k::time::LocalTime = k::time::LocalTime::of(23, 59, 58, 123456789);
            if (t4.hour() != 23 || t4.minute() != 59 || t4.second() != 58 || t4.nano() != 123456789) return false;

            return true;
        }

        test_reject_hour_negative() : int {
            try {
                k::time::LocalTime::of(-1, 0, 0, 0);
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
                return 1;
            }
        }

        test_reject_hour_24() : int {
            try {
                k::time::LocalTime::of(24, 0, 0, 0);
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
                return 1;
            }
        }

        test_reject_minute_negative() : int {
            try {
                k::time::LocalTime::of(0, -1, 0, 0);
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
                return 1;
            }
        }

        test_reject_minute_60() : int {
            try {
                k::time::LocalTime::of(0, 60, 0, 0);
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
                return 1;
            }
        }

        test_reject_second_negative() : int {
            try {
                k::time::LocalTime::of(0, 0, -1, 0);
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
                return 1;
            }
        }

        test_reject_second_61() : int {
            try {
                k::time::LocalTime::of(23, 59, 61, 0);
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
                return 1;
            }
        }

        test_reject_nano_negative() : int {
            try {
                k::time::LocalTime::of(0, 0, 0, -1);
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
                return 1;
            }
        }

        test_reject_nano_billion() : int {
            try {
                k::time::LocalTime::of(0, 0, 0, 1000000000);
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
                return 1;
            }
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto check_b = [&](const char* sym) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() != 0);
    };
    check_b("test_valid_factories");

    auto check_i = [&](const char* sym, int expected) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == expected);
    };

    check_i("test_reject_hour_negative", 1);
    check_i("test_reject_hour_24", 1);
    check_i("test_reject_minute_negative", 1);
    check_i("test_reject_minute_60", 1);
    check_i("test_reject_second_negative", 1);
    check_i("test_reject_second_61", 1);
    check_i("test_reject_nano_negative", 1);
    check_i("test_reject_nano_billion", 1);
}

// ═════════════════════════════════════════════════════════════════════════════
// 17. LocalTime structural leap second
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("LocalTime: structural leap second acceptance and rejection", "[libk][time][local_time]") {
    auto jit = jit_k(R"SRC(
        module __test_local_time_leap_second__;

        test_accept_23_59_60() : bool throws(k::time::InvalidTemporalValueException) {
            t : k::time::LocalTime = k::time::LocalTime::of(23, 59, 60, 500);
            return t.hour() == 23 && t.minute() == 59 && t.second() == 60 &&
                   t.nano() == 500 && t.isStructuralLeapSecond();
        }

        test_normal_is_not_leap() : bool throws(k::time::InvalidTemporalValueException) {
            t : k::time::LocalTime = k::time::LocalTime::of(23, 59, 59, 0);
            return !t.isStructuralLeapSecond();
        }

        test_reject_12_00_60() : int {
            try {
                k::time::LocalTime::of(12, 0, 60, 0);
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
                return 1;
            }
        }

        test_reject_23_58_60() : int {
            try {
                k::time::LocalTime::of(23, 58, 60, 0);
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
                return 1;
            }
        }

        test_reject_22_59_60() : int {
            try {
                k::time::LocalTime::of(22, 59, 60, 0);
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
                return 1;
            }
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto check_b = [&](const char* sym) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() != 0);
    };
    check_b("test_accept_23_59_60");
    check_b("test_normal_is_not_leap");

    auto check_i = [&](const char* sym, int expected) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == expected);
    };
    check_i("test_reject_12_00_60", 1);
    check_i("test_reject_23_58_60", 1);
    check_i("test_reject_22_59_60", 1);
}

// ═════════════════════════════════════════════════════════════════════════════
// 18. LocalTime toNanoOfDay and duration arithmetic
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("LocalTime: toNanoOfDay and duration arithmetic with 24h wrap", "[libk][time][local_time]") {
    auto jit = jit_k(R"SRC(
        module __test_local_time_arithmetic__;

        test_to_nano_of_day() : bool throws(k::time::InvalidTemporalValueException) {
            m : k::time::LocalTime = k::time::LocalTime::midnight();
            if (m.toNanoOfDay() != 0L) return false;

            t1 : k::time::LocalTime = k::time::LocalTime::of(1, 0, 0, 0);
            if (t1.toNanoOfDay() != 3600000000000L) return false;

            t2 : k::time::LocalTime = k::time::LocalTime::of(23, 59, 59, 999999999);
            if (t2.toNanoOfDay() != 86399999999999L) return false;

            ls : k::time::LocalTime = k::time::LocalTime::of(23, 59, 60, 0);
            if (ls.toNanoOfDay() != 86400000000000L) return false;

            return true;
        }

        test_duration_arithmetic() : bool throws(k::time::InvalidTemporalValueException,
                                                 k::time::TemporalArithmeticException) {
            t : k::time::LocalTime = k::time::LocalTime::of(12, 0, 0, 0);
            d : k::time::Duration = k::time::Duration::ofHours(3L);

            plus_res : k::time::LocalTime = t.plus(d);
            plus_op : k::time::LocalTime = t + d;
            exp_plus : k::time::LocalTime = k::time::LocalTime::of(15, 0, 0, 0);
            if (plus_res != exp_plus || plus_op != exp_plus) return false;

            minus_res : k::time::LocalTime = t.minus(d);
            minus_op : k::time::LocalTime = t - d;
            exp_minus : k::time::LocalTime = k::time::LocalTime::of(9, 0, 0, 0);
            if (minus_res != exp_minus || minus_op != exp_minus) return false;

            // 24h wrap forward: 23:00 + 2h = 01:00
            t_late : k::time::LocalTime = k::time::LocalTime::of(23, 0, 0, 0);
            wrap_fwd : k::time::LocalTime = t_late + k::time::Duration::ofHours(2L);
            if (wrap_fwd != k::time::LocalTime::of(1, 0, 0, 0)) return false;

            // 24h wrap backward: 01:00 - 2h = 23:00
            t_early : k::time::LocalTime = k::time::LocalTime::of(1, 0, 0, 0);
            wrap_back : k::time::LocalTime = t_early - k::time::Duration::ofHours(2L);
            if (wrap_back != k::time::LocalTime::of(23, 0, 0, 0)) return false;

            // Nanosecond wrap backward: 00:00:00 - 1ns = 23:59:59.999999999
            wrap_nano : k::time::LocalTime = k::time::LocalTime::midnight() - k::time::Duration::ofNanos(1L);
            if (wrap_nano != k::time::LocalTime::of(23, 59, 59, 999999999)) return false;

            return true;
        }

        test_leap_second_plus_rejected() : int {
            try {
                ls : k::time::LocalTime = k::time::LocalTime::of(23, 59, 60, 0);
                d : k::time::Duration = k::time::Duration::ofSeconds(1L);
                r : k::time::LocalTime = ls + d;
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
                return 1;
            }
        }

        test_leap_second_minus_rejected() : int {
            try {
                ls : k::time::LocalTime = k::time::LocalTime::of(23, 59, 60, 0);
                d : k::time::Duration = k::time::Duration::ofSeconds(1L);
                r : k::time::LocalTime = ls - d;
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
                return 1;
            }
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto check_b = [&](const char* sym) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() != 0);
    };
    check_b("test_to_nano_of_day");
    check_b("test_duration_arithmetic");

    auto check_i = [&](const char* sym, int expected) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == expected);
    };
    check_i("test_leap_second_plus_rejected", 1);
    check_i("test_leap_second_minus_rejected", 1);
}

// ═════════════════════════════════════════════════════════════════════════════
// 19. LocalTime comparisons and ordering
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("LocalTime: comparisons and ordering", "[libk][time][local_time]") {
    auto jit = jit_k(R"SRC(
        module __test_local_time_comparisons__;

        test_comparisons() : bool throws(k::time::InvalidTemporalValueException) {
            t1 : k::time::LocalTime = k::time::LocalTime::of(10, 0, 0, 0);
            t2 : k::time::LocalTime = k::time::LocalTime::of(10, 0, 0, 1);
            t3 : k::time::LocalTime = k::time::LocalTime::of(10, 0, 1, 0);
            t4 : k::time::LocalTime = k::time::LocalTime::of(10, 1, 0, 0);
            t5 : k::time::LocalTime = k::time::LocalTime::of(11, 0, 0, 0);

            if (!(t1 < t2 && t2 < t3 && t3 < t4 && t4 < t5)) return false;
            if (!(t5 > t4 && t4 > t3 && t3 > t2 && t2 > t1)) return false;
            if (!(t1 <= t2 && t1 <= t1)) return false;
            if (!(t2 >= t1 && t1 >= t1)) return false;
            if (!(t1 == t1 && t1 != t2)) return false;

            if (t1.compareTo(t2) >= 0) return false;
            if (t2.compareTo(t1) <= 0) return false;
            if (t1.compareTo(t1) != 0) return false;

            // Structural leap second ordering: 23:59:59 < 23:59:60
            t_59 : k::time::LocalTime = k::time::LocalTime::of(23, 59, 59, 999999999);
            t_60 : k::time::LocalTime = k::time::LocalTime::of(23, 59, 60, 0);
            if (!(t_59 < t_60)) return false;
            if (!(t_60 > t_59)) return false;

            return true;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<int(*)()>("test_comparisons");
    REQUIRE(fn != nullptr);
    CHECK(fn() != 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// 20. LocalDate field validation and properties
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("LocalDate: field validation, properties, and accessors", "[libk][time][local_date]") {
    auto jit = jit_k(R"SRC(
        module __test_local_date_validation__;

        test_valid_dates() : bool throws(k::time::InvalidTemporalValueException) {
            d : k::time::LocalDate = k::time::LocalDate::of(2026L, 9, 10);
            if (d.year() != 2026L || d.month() != 9 || d.day() != 10) return false;
            if (d.isLeapYear()) return false;
            if (d.lengthOfMonth() != 30) return false;
            if (d.dayOfYear() != 253) return false;

            leap_d : k::time::LocalDate = k::time::LocalDate::of(2024L, 2, 29);
            if (!leap_d.isLeapYear()) return false;
            if (leap_d.lengthOfMonth() != 29) return false;
            if (leap_d.dayOfYear() != 60) return false;

            y0 : k::time::LocalDate = k::time::LocalDate::of(0L, 1, 1);
            if (y0.year() != 0L || y0.month() != 1 || y0.day() != 1) return false;

            return true;
        }

        test_reject_month_0() : int {
            try {
                k::time::LocalDate::of(2026L, 0, 1);
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
                return 1;
            }
        }

        test_reject_month_13() : int {
            try {
                k::time::LocalDate::of(2026L, 13, 1);
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
                return 1;
            }
        }

        test_reject_day_0() : int {
            try {
                k::time::LocalDate::of(2026L, 1, 0);
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
                return 1;
            }
        }

        test_reject_day_32() : int {
            try {
                k::time::LocalDate::of(2026L, 1, 32);
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
                return 1;
            }
        }

        test_reject_april_31() : int {
            try {
                k::time::LocalDate::of(2026L, 4, 31);
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
                return 1;
            }
        }

        test_reject_feb_29_common_year() : int {
            try {
                k::time::LocalDate::of(2023L, 2, 29);
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
                return 1;
            }
        }

        test_reject_feb_29_century_common_year() : int {
            try {
                k::time::LocalDate::of(1900L, 2, 29);
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
                return 1;
            }
        }

        test_reject_feb_30_leap_year() : int {
            try {
                k::time::LocalDate::of(2024L, 2, 30);
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
                return 1;
            }
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto check_b = [&](const char* sym) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() != 0);
    };
    auto check_i = [&](const char* sym, int expected) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == expected);
    };
    check_b("test_valid_dates");
    check_i("test_reject_month_0", 1);
    check_i("test_reject_month_13", 1);
    check_i("test_reject_day_0", 1);
    check_i("test_reject_day_32", 1);
    check_i("test_reject_april_31", 1);
    check_i("test_reject_feb_29_common_year", 1);
    check_i("test_reject_feb_29_century_common_year", 1);
    check_i("test_reject_feb_30_leap_year", 1);
}

// ═════════════════════════════════════════════════════════════════════════════
// 21. LocalDate EpochDay conversions and BCE years
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("LocalDate: EpochDay conversions, BCE years, round-trip", "[libk][time][local_date]") {
    auto jit = jit_k(R"SRC(
        module __test_local_date_epoch_day__;

        test_conversions() : bool throws(k::time::InvalidTemporalValueException,
                                         k::time::TemporalArithmeticException) {
            d_epoch : k::time::LocalDate = k::time::LocalDate::of(1970L, 1, 1);
            if (d_epoch.toEpochDay().daysSinceEpoch() != 0L) return false;

            d_next : k::time::LocalDate = k::time::LocalDate::of(1970L, 1, 2);
            if (d_next.toEpochDay().daysSinceEpoch() != 1L) return false;

            d_prev : k::time::LocalDate = k::time::LocalDate::of(1969L, 12, 31);
            if (d_prev.toEpochDay().daysSinceEpoch() != -1L) return false;

            d_y2k : k::time::LocalDate = k::time::LocalDate::of(2000L, 1, 1);
            if (d_y2k.toEpochDay().daysSinceEpoch() != 10957L) return false;

            d_bce : k::time::LocalDate = k::time::LocalDate::of(0L, 1, 1);
            if (d_bce.toEpochDay().daysSinceEpoch() != -719528L) return false;

            d_bce_prev : k::time::LocalDate = k::time::LocalDate::of(-1L, 12, 31);
            if (d_bce_prev.toEpochDay().daysSinceEpoch() != -719529L) return false;

            // fromEpochDay
            ed0 : k::time::EpochDay = k::time::EpochDay::ofDaysSinceEpoch(0L);
            if (k::time::LocalDate::fromEpochDay(ed0) != d_epoch) return false;

            ed_y2k : k::time::EpochDay = k::time::EpochDay::ofDaysSinceEpoch(10957L);
            if (k::time::LocalDate::fromEpochDay(ed_y2k) != d_y2k) return false;

            // Round trip
            d_rt : k::time::LocalDate = k::time::LocalDate::of(2026L, 9, 10);
            if (k::time::LocalDate::fromEpochDay(d_rt.toEpochDay()) != d_rt) return false;

            return true;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<int(*)()>("test_conversions");
    REQUIRE(fn != nullptr);
    CHECK(fn() != 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// 22. LocalDate Period arithmetic and end-of-month clamping
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("LocalDate: Period arithmetic and end-of-month clamping", "[libk][time][local_date]") {
    auto jit = jit_k(R"SRC(
        module __test_local_date_period__;

        test_month_clamping() : bool throws(k::time::InvalidTemporalValueException,
                                            k::time::TemporalArithmeticException) {
            // 2026-01-31 + 1 month == 2026-02-28 (common year clamping)
            d1 : k::time::LocalDate = k::time::LocalDate::of(2026L, 1, 31);
            r1 : k::time::LocalDate = d1 + k::time::Period::months(1L);
            if (r1 != k::time::LocalDate::of(2026L, 2, 28)) return false;

            // 2024-01-31 + 1 month == 2024-02-29 (leap year clamping)
            d2 : k::time::LocalDate = k::time::LocalDate::of(2024L, 1, 31);
            r2 : k::time::LocalDate = d2 + k::time::Period::months(1L);
            if (r2 != k::time::LocalDate::of(2024L, 2, 29)) return false;

            // 2026-03-31 + 1 month == 2026-04-30
            d3 : k::time::LocalDate = k::time::LocalDate::of(2026L, 3, 31);
            r3 : k::time::LocalDate = d3 + k::time::Period::months(1L);
            if (r3 != k::time::LocalDate::of(2026L, 4, 30)) return false;

            // 2026-05-31 - 1 month == 2026-04-30
            d4 : k::time::LocalDate = k::time::LocalDate::of(2026L, 5, 31);
            r4 : k::time::LocalDate = d4 - k::time::Period::months(1L);
            if (r4 != k::time::LocalDate::of(2026L, 4, 30)) return false;

            // 2024-02-29 + 1 year == 2025-02-28
            d5 : k::time::LocalDate = k::time::LocalDate::of(2024L, 2, 29);
            r5 : k::time::LocalDate = d5 + k::time::Period::years(1L);
            if (r5 != k::time::LocalDate::of(2025L, 2, 28)) return false;

            // 2024-02-29 + 4 years == 2028-02-29
            r6 : k::time::LocalDate = d5 + k::time::Period::years(4L);
            if (r6 != k::time::LocalDate::of(2028L, 2, 29)) return false;

            return true;
        }

        test_weeks_and_days() : bool throws(k::time::InvalidTemporalValueException,
                                            k::time::TemporalArithmeticException) {
            d : k::time::LocalDate = k::time::LocalDate::of(2026L, 1, 1);

            // + 2 weeks == 2026-01-15
            w : k::time::LocalDate = d + k::time::Period::weeks(2L);
            if (w != k::time::LocalDate::of(2026L, 1, 15)) return false;

            // + 10 days == 2026-01-11
            days : k::time::LocalDate = d + k::time::Period::days(10L);
            if (days != k::time::LocalDate::of(2026L, 1, 11)) return false;

            // 2026-01-31 + 1 month + 5 days:
            // Month applied first with clamping -> 2026-02-28, then + 5 days -> 2026-03-05
            p_combo : k::time::Period = k::time::Period::of(0L, 1L, 0L, 5L, 0L, 0L, 0L);
            d_combo : k::time::LocalDate = k::time::LocalDate::of(2026L, 1, 31);
            r_combo : k::time::LocalDate = d_combo + p_combo;
            if (r_combo != k::time::LocalDate::of(2026L, 3, 5)) return false;

            // Negative period: 2026-03-01 - 1 day == 2026-02-28
            d_march : k::time::LocalDate = k::time::LocalDate::of(2026L, 3, 1);
            if ((d_march - k::time::Period::days(1L)) != k::time::LocalDate::of(2026L, 2, 28)) return false;

            // Negative period in leap year: 2024-03-01 - 1 day == 2024-02-29
            d_leap_march : k::time::LocalDate = k::time::LocalDate::of(2024L, 3, 1);
            if ((d_leap_march - k::time::Period::days(1L)) != k::time::LocalDate::of(2024L, 2, 29)) return false;

            return true;
        }

        test_at_time() : bool throws(k::time::InvalidTemporalValueException) {
            d : k::time::LocalDate = k::time::LocalDate::of(2026L, 9, 10);
            t : k::time::LocalTime = k::time::LocalTime::of(14, 30);
            dt : k::time::LocalDateTime = d.atTime(t);

            return dt.year() == 2026L && dt.month() == 9 && dt.day() == 10 &&
                   dt.hour() == 14 && dt.minute() == 30 && dt.second() == 0 &&
                   dt.nano() == 0;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto check_b = [&](const char* sym) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() != 0);
    };

    check_b("test_month_clamping");
    check_b("test_weeks_and_days");
    check_b("test_at_time");
}

// ═════════════════════════════════════════════════════════════════════════════
// 23. LocalDate comparisons and ordering
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("LocalDate: comparisons and ordering", "[libk][time][local_date]") {
    auto jit = jit_k(R"SRC(
        module __test_local_date_comparisons__;

        test_comparisons() : bool throws(k::time::InvalidTemporalValueException) {
            d1 : k::time::LocalDate = k::time::LocalDate::of(2025L, 12, 31);
            d2 : k::time::LocalDate = k::time::LocalDate::of(2026L, 1, 15);
            d3 : k::time::LocalDate = k::time::LocalDate::of(2026L, 2, 10);
            d4 : k::time::LocalDate = k::time::LocalDate::of(2026L, 2, 20);

            if (!(d1 < d2 && d2 < d3 && d3 < d4)) return false;
            if (!(d4 > d3 && d3 > d2 && d2 > d1)) return false;
            if (!(d1 <= d2 && d1 <= d1)) return false;
            if (!(d2 >= d1 && d1 >= d1)) return false;
            if (!(d1 == d1 && d1 != d2)) return false;

            if (d1.compareTo(d2) >= 0) return false;
            if (d2.compareTo(d1) <= 0) return false;
            if (d1.compareTo(d1) != 0) return false;

            return true;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<int(*)()>("test_comparisons");
    REQUIRE(fn != nullptr);
    CHECK(fn() != 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// 24. LocalDateTime composition and field accessors
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("LocalDateTime: composition and field accessors", "[libk][time][local_date_time]") {
    auto jit = jit_k(R"SRC(
        module __test_local_date_time_composition__;

        test_composition() : bool throws(k::time::InvalidTemporalValueException) {
            d : k::time::LocalDate = k::time::LocalDate::of(2026L, 9, 10);
            t : k::time::LocalTime = k::time::LocalTime::of(15, 45, 30, 500);

            dt1 : k::time::LocalDateTime = k::time::LocalDateTime::of(d, t);
            if (dt1.date() != d || dt1.time() != t) return false;
            if (dt1.year() != 2026L || dt1.month() != 9 || dt1.day() != 10) return false;
            if (dt1.hour() != 15 || dt1.minute() != 45 || dt1.second() != 30 || dt1.nano() != 500) return false;

            dt2 : k::time::LocalDateTime = k::time::LocalDateTime::of(2026L, 9, 10, 15, 45, 30, 500);
            if (dt1 != dt2) return false;

            return true;
        }

        test_reject_invalid_date_component() : int {
            try {
                k::time::LocalDateTime::of(2026L, 2, 29, 12, 0, 0, 0);
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
                return 1;
            }
        }

        test_reject_invalid_time_component() : int {
            try {
                k::time::LocalDateTime::of(2026L, 9, 10, 25, 0, 0, 0);
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
                return 1;
            }
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto check_b = [&](const char* sym) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() != 0);
    };
    check_b("test_composition");

    auto check_i = [&](const char* sym, int expected) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == expected);
    };
    check_i("test_reject_invalid_date_component", 1);
    check_i("test_reject_invalid_time_component", 1);
}

// ═════════════════════════════════════════════════════════════════════════════
// 25. LocalDateTime Period arithmetic with time carry
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("LocalDateTime: Period arithmetic with time carry", "[libk][time][local_date_time]") {
    auto jit = jit_k(R"SRC(
        module __test_local_date_time_period__;

        test_period_time_carry() : bool throws(k::time::InvalidTemporalValueException,
                                               k::time::TemporalArithmeticException) {
            // 2026-01-31T23:00:00 + 2 hours -> 2026-02-01T01:00:00 (crosses into new day and month)
            dt : k::time::LocalDateTime = k::time::LocalDateTime::of(2026L, 1, 31, 23, 0, 0, 0);
            r : k::time::LocalDateTime = dt + k::time::Period::hours(2L);
            exp : k::time::LocalDateTime = k::time::LocalDateTime::of(2026L, 2, 1, 1, 0, 0, 0);
            if (r != exp) return false;

            // 2026-01-31T23:00:00 + (1 month + 2 hours):
            // 2026-01-31 + 1 month = 2026-02-28; then 23:00 + 2h = next day 01:00; 2026-02-28 + 1d = 2026-03-01!
            p_combo : k::time::Period = k::time::Period::of(0L, 1L, 0L, 0L, 2L, 0L, 0L);
            r_combo : k::time::LocalDateTime = dt + p_combo;
            exp_combo : k::time::LocalDateTime = k::time::LocalDateTime::of(2026L, 3, 1, 1, 0, 0, 0);
            if (r_combo != exp_combo) return false;

            // Borrow: 2026-02-01T01:00:00 - 2 hours -> 2026-01-31T23:00:00
            back : k::time::LocalDateTime = exp - k::time::Period::hours(2L);
            if (back != dt) return false;

            return true;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<int(*)()>("test_period_time_carry");
    REQUIRE(fn != nullptr);
    CHECK(fn() != 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// 26. LocalDateTime Duration arithmetic across days
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("LocalDateTime: Duration arithmetic across days", "[libk][time][local_date_time]") {
    auto jit = jit_k(R"SRC(
        module __test_local_date_time_duration__;

        test_duration_across_days() : bool throws(k::time::InvalidTemporalValueException,
                                                  k::time::TemporalArithmeticException) {
            // 2026-01-01T12:00:00 + 36 hours -> 2026-01-03T00:00:00
            dt : k::time::LocalDateTime = k::time::LocalDateTime::of(2026L, 1, 1, 12, 0, 0, 0);
            d : k::time::Duration = k::time::Duration::ofHours(36L);
            r : k::time::LocalDateTime = dt + d;
            exp : k::time::LocalDateTime = k::time::LocalDateTime::of(2026L, 1, 3, 0, 0, 0, 0);
            if (r != exp) return false;

            // Subtract back
            back : k::time::LocalDateTime = r - d;
            if (back != dt) return false;

            // Subtract 1 ns from midnight: 2026-01-01T00:00:00 - 1ns = 2025-12-31T23:59:59.999999999
            dt_mid : k::time::LocalDateTime = k::time::LocalDateTime::of(2026L, 1, 1, 0, 0, 0, 0);
            r_nano : k::time::LocalDateTime = dt_mid - k::time::Duration::ofNanos(1L);
            exp_nano : k::time::LocalDateTime = k::time::LocalDateTime::of(2025L, 12, 31, 23, 59, 59, 999999999);
            if (r_nano != exp_nano) return false;

            return true;
        }

        test_leap_second_plus_rejected() : int {
            try {
                dt : k::time::LocalDateTime = k::time::LocalDateTime::of(2026L, 6, 30, 23, 59, 60, 0);
                d : k::time::Duration = k::time::Duration::ofSeconds(1L);
                r : k::time::LocalDateTime = dt + d;
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
                return 1;
            }
        }

        test_leap_second_minus_rejected() : int {
            try {
                dt : k::time::LocalDateTime = k::time::LocalDateTime::of(2026L, 6, 30, 23, 59, 60, 0);
                d : k::time::Duration = k::time::Duration::ofSeconds(1L);
                r : k::time::LocalDateTime = dt - d;
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
                return 1;
            }
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto check_b = [&](const char* sym) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() != 0);
    };
    check_b("test_duration_across_days");

    auto check_i = [&](const char* sym, int expected) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == expected);
    };
    check_i("test_leap_second_plus_rejected", 1);
    check_i("test_leap_second_minus_rejected", 1);
}

// ═════════════════════════════════════════════════════════════════════════════
// 27. LocalDateTime comparisons and ordering
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("LocalDateTime: comparisons and ordering", "[libk][time][local_date_time]") {
    auto jit = jit_k(R"SRC(
        module __test_local_date_time_comparisons__;

        test_comparisons() : bool throws(k::time::InvalidTemporalValueException) {
            dt1 : k::time::LocalDateTime = k::time::LocalDateTime::of(2025L, 12, 31, 23, 59, 59, 0);
            dt2 : k::time::LocalDateTime = k::time::LocalDateTime::of(2026L, 1, 1, 0, 0, 0, 0);
            dt3 : k::time::LocalDateTime = k::time::LocalDateTime::of(2026L, 1, 1, 10, 0, 0, 0);
            dt4 : k::time::LocalDateTime = k::time::LocalDateTime::of(2026L, 1, 1, 10, 0, 0, 1);
            dt5 : k::time::LocalDateTime = k::time::LocalDateTime::of(2026L, 1, 2, 0, 0, 0, 0);

            if (!(dt1 < dt2 && dt2 < dt3 && dt3 < dt4 && dt4 < dt5)) return false;
            if (!(dt5 > dt4 && dt4 > dt3 && dt3 > dt2 && dt2 > dt1)) return false;
            if (!(dt1 <= dt2 && dt1 <= dt1)) return false;
            if (!(dt2 >= dt1 && dt1 >= dt1)) return false;
            if (!(dt1 == dt1 && dt1 != dt2)) return false;

            if (dt1.compareTo(dt2) >= 0) return false;
            if (dt2.compareTo(dt1) <= 0) return false;
            if (dt1.compareTo(dt1) != 0) return false;

            return true;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<int(*)()>("test_comparisons");
    REQUIRE(fn != nullptr);
    CHECK(fn() != 0);
}



