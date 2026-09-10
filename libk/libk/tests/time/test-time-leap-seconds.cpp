/*
 * K Language standard library — Leap Second & POSIX Interop tests (Package D7)
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

#include <string>

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
// 1. Leap second: civil UTC representation (23:59:60)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Leap second: UTC civil representation of 2016-12-31 23:59:60", "[libk][time][leap][utc]") {
    auto jit = jit_k(R"SRC(
        module __test_leap_utc__;

        test_leap_second_2016() : int {
            // Continuous second for 2016-12-31 23:59:60 UTC is 1483228826L
            leapInst : k::time::Instant = k::time::Instant::ofEpochSecond(1483228826L);
            utc : k::time::LocalDateTime = leapInst.toUtcDateTime();

            if (utc.date().year() != 2016L) return 0;
            if (utc.date().month() != 12) return 0;
            if (utc.date().day() != 31) return 0;
            if (utc.time().hour() != 23) return 0;
            if (utc.time().minute() != 59) return 0;
            if (utc.time().second() != 60) return 0;

            return 1;
        }

        test_second_before_leap() : int {
            // 2016-12-31 23:59:59 is continuous second 1483228825L
            beforeInst : k::time::Instant = k::time::Instant::ofEpochSecond(1483228825L);
            utc : k::time::LocalDateTime = beforeInst.toUtcDateTime();

            if (utc.date().year() != 2016L || utc.date().month() != 12 || utc.date().day() != 31) return 0;
            if (utc.time().hour() != 23 || utc.time().minute() != 59 || utc.time().second() != 59) return 0;

            return 1;
        }

        test_second_after_leap() : int {
            // 2017-01-01 00:00:00 is continuous second 1483228827L
            afterInst : k::time::Instant = k::time::Instant::ofEpochSecond(1483228827L);
            utc : k::time::LocalDateTime = afterInst.toUtcDateTime();

            if (utc.date().year() != 2017L || utc.date().month() != 1 || utc.date().day() != 1) return 0;
            if (utc.time().hour() != 0 || utc.time().minute() != 0 || utc.time().second() != 0) return 0;

            return 1;
        }

        test_leap_interval_two_seconds() : int throws(k::time::TemporalArithmeticException) {
            beforeInst : k::time::Instant = k::time::Instant::ofEpochSecond(1483228825L);
            afterInst : k::time::Instant = k::time::Instant::ofEpochSecond(1483228827L);

            diff : k::time::Duration = afterInst - beforeInst;
            // Elapsed time between 23:59:59 and 00:00:00 across leap second is exactly 2 seconds!
            if (diff.secondsPart() != 2L || diff.nanoAdjustment() != 0) return 0;

            return 1;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto check = [&](const char* sym) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == 1);
    };

    check("test_leap_second_2016");
    check("test_second_before_leap");
    check("test_second_after_leap");
    check("test_leap_interval_two_seconds");
}

// ═════════════════════════════════════════════════════════════════════════════
// 2. PosixLeapSecondPolicy: reject, fold to previous, fold to following
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Leap second: POSIX conversion policies at leap second", "[libk][time][leap][policy]") {
    auto jit = jit_k(R"SRC(
        module __test_leap_policy_conversion__;

        test_policy_reject() : int {
            leapInst : k::time::Instant = k::time::Instant::ofEpochSecond(1483228826L);
            try {
                leapInst.toPosixTimestamp(k::time::PosixLeapSecondPolicy::reject());
                return 0; // should throw
            } catch (e: k::time::LeapSecondException&) {
                if (e.getCode() == 504) return 1;
                return 0;
            } catch (e2: k::time::TemporalException&) {
                return 0;
            }
        }

        test_policy_fold_previous() : int throws(k::time::LeapSecondException) {
            leapInst : k::time::Instant = k::time::Instant::ofEpochSecond(1483228826L);
            posix : k::time::PosixTimestamp = leapInst.toPosixTimestamp(k::time::PosixLeapSecondPolicy::foldToPreviousSecond());
            // Folds to 23:59:59 (POSIX 1483228799L)
            if (posix.seconds() != 1483228799L) return 0;
            return 1;
        }

        test_policy_fold_following() : int throws(k::time::LeapSecondException) {
            leapInst : k::time::Instant = k::time::Instant::ofEpochSecond(1483228826L);
            posix : k::time::PosixTimestamp = leapInst.toPosixTimestamp(k::time::PosixLeapSecondPolicy::foldToFollowingSecond());
            // Folds to 00:00:00 (POSIX 1483228800L)
            if (posix.seconds() != 1483228800L) return 0;
            return 1;
        }

        test_ordinary_posix_round_trip() : int throws(k::time::LeapSecondException) {
            orig : k::time::PosixTimestamp = k::time::PosixTimestamp::ofSeconds(1600000000L, 500);
            inst : k::time::Instant = orig.toInstant();
            rt : k::time::PosixTimestamp = inst.toPosixTimestamp();

            if (rt.seconds() != orig.seconds() || rt.nanoAdjustment() != orig.nanoAdjustment()) return 0;
            if (!(rt == orig)) return 0;

            return 1;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto check = [&](const char* sym) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == 1);
    };

    check("test_policy_reject");
    check("test_policy_fold_previous");
    check("test_policy_fold_following");
    check("test_ordinary_posix_round_trip");
}

// ═════════════════════════════════════════════════════════════════════════════
// 3. ISO text: Instant format and parse at leap second
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Leap second: ISO formatting and parsing", "[libk][time][leap][iso]") {
    auto jit = jit_k(R"SRC(
        module __test_leap_iso__;

        test_format_leap_instant() : int {
            leapInst : k::time::Instant = k::time::Instant::ofEpochSecond(1483228826L);
            str : k::String = k::time::Iso::formatInstant(leapInst);
            if (str != k::String("2016-12-31T23:59:60Z")) return 0;
            return 1;
        }

        test_parse_leap_instant() : int throws(k::time::TemporalParseException, k::time::InvalidTemporalValueException, k::time::LeapSecondException, k::time::TemporalArithmeticException) {
            inst : k::time::Instant = k::time::Iso::parseInstant(k::String("2016-12-31T23:59:60Z"));
            if (inst.epochSeconds() != 1483228826L) return 0;
            return 1;
        }

        test_parse_invalid_leap_date_throws() : int throws(k::time::TemporalParseException, k::time::InvalidTemporalValueException, k::time::TemporalArithmeticException) {
            try {
                k::time::Iso::parseInstant(k::String("2016-12-30T23:59:60Z"));
                return 0;
            } catch (e: k::time::LeapSecondException&) {
                if (e.getCode() == 504) return 1;
                return 0;
            }
        }

        test_parse_invalid_leap_time_throws() : int throws(k::time::TemporalParseException, k::time::InvalidTemporalValueException, k::time::TemporalArithmeticException) {
            try {
                k::time::Iso::parseInstant(k::String("2016-12-31T23:58:60Z"));
                return 0;
            } catch (e: k::time::LeapSecondException&) {
                if (e.getCode() == 504) return 1;
                return 0;
            } catch (e2: k::time::InvalidTemporalValueException&) {
                return 1;
            }
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto check = [&](const char* sym) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == 1);
    };

    check("test_format_leap_instant");
    check("test_parse_leap_instant");
    check("test_parse_invalid_leap_date_throws");
    check("test_parse_invalid_leap_time_throws");
}
