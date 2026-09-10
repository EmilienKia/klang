/*
 * K Language standard library — ISO canonical text & ZoneOffset tests
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
// 1. ZoneOffset: factories and bounds
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("ZoneOffset: factories and bounds", "[libk][time][zone_offset]") {
    auto jit = jit_k(R"SRC(
        module __test_zone_offset_factories__;

        test_default() : int {
            z : k::time::ZoneOffset;
            if (z.totalSeconds() == 0 && z.isUtc()) return 1;
            return 0;
        }

        test_utc() : int {
            z : k::time::ZoneOffset = k::time::ZoneOffset::utc();
            if (z.totalSeconds() == 0 && z.isUtc()) return 1;
            return 0;
        }

        test_of_total_seconds() : int {
            z : k::time::ZoneOffset = k::time::ZoneOffset::ofTotalSeconds(7200);
            if (z.totalSeconds() == 7200 && !z.isUtc()) return 1;
            return 0;
        }

        test_min_bound() : int {
            z : k::time::ZoneOffset = k::time::ZoneOffset::ofTotalSeconds(-64800);
            if (z.totalSeconds() == -64800) return 1;
            return 0;
        }

        test_max_bound() : int {
            z : k::time::ZoneOffset = k::time::ZoneOffset::ofTotalSeconds(64800);
            if (z.totalSeconds() == 64800) return 1;
            return 0;
        }

        test_underflow() : int {
            try {
                k::time::ZoneOffset::ofTotalSeconds(-64801);
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
                return 1;
            }
        }

        test_overflow() : int {
            try {
                k::time::ZoneOffset::ofTotalSeconds(64801);
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
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

    check("test_default");
    check("test_utc");
    check("test_of_total_seconds");
    check("test_min_bound");
    check("test_max_bound");
    check("test_underflow");
    check("test_overflow");
}

// ═════════════════════════════════════════════════════════════════════════════
// 2. ZoneOffset: ISO formatting and parsing
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("ZoneOffset: ISO formatting and parsing", "[libk][time][zone_offset]") {
    auto jit = jit_k(R"SRC(
        module __test_zone_offset_iso__;

        test_format_utc() : int {
            z : k::time::ZoneOffset = k::time::ZoneOffset::utc();
            if (z.toIsoString() == k::String("Z")) return 1;
            return 0;
        }

        test_format_plus_hours() : int {
            z : k::time::ZoneOffset = k::time::ZoneOffset::ofTotalSeconds(7200);
            if (z.toIsoString() == k::String("+02:00")) return 1;
            return 0;
        }

        test_format_minus_hours() : int {
            z : k::time::ZoneOffset = k::time::ZoneOffset::ofTotalSeconds(-18000);
            if (z.toIsoString() == k::String("-05:00")) return 1;
            return 0;
        }

        test_format_with_seconds() : int {
            // +05:30:15 = 5*3600 + 30*60 + 15 = 18000 + 1800 + 15 = 19815
            z : k::time::ZoneOffset = k::time::ZoneOffset::ofTotalSeconds(19815);
            if (z.toIsoString() == k::String("+05:30:15")) return 1;
            return 0;
        }

        test_format_minus_with_seconds() : int {
            // -04:00:30 = -(4*3600 + 30) = -14430
            z : k::time::ZoneOffset = k::time::ZoneOffset::ofTotalSeconds(-14430);
            if (z.toIsoString() == k::String("-04:00:30")) return 1;
            return 0;
        }

        test_parse_utc() : int {
            z : k::time::ZoneOffset = k::time::ZoneOffset::parseIso("Z");
            if (z.totalSeconds() == 0 && z.isUtc()) return 1;
            return 0;
        }

        test_parse_plus() : int {
            z : k::time::ZoneOffset = k::time::ZoneOffset::parseIso("+01:00");
            if (z.totalSeconds() == 3600) return 1;
            return 0;
        }

        test_parse_minus() : int {
            z : k::time::ZoneOffset = k::time::ZoneOffset::parseIso("-04:00");
            if (z.totalSeconds() == -14400) return 1;
            return 0;
        }

        test_parse_seconds() : int {
            z : k::time::ZoneOffset = k::time::ZoneOffset::parseIso("+05:30:45");
            if (z.totalSeconds() == 19845) return 1;
            return 0;
        }

        test_parse_compact() : int {
            z1 : k::time::ZoneOffset = k::time::ZoneOffset::parseIso("+0200");
            z2 : k::time::ZoneOffset = k::time::ZoneOffset::parseIso("-0330");
            if (z1.totalSeconds() == 7200 && z2.totalSeconds() == -12600) return 1;
            return 0;
        }

        test_parse_compact_seconds() : int {
            z : k::time::ZoneOffset = k::time::ZoneOffset::parseIso("+010203");
            if (z.totalSeconds() == 3723) return 1;
            return 0;
        }

        test_parse_invalid_syntax() : int {
            try {
                k::time::ZoneOffset::parseIso("bad");
                return 0;
            } catch (e: k::time::TemporalParseException&) {
                return 1;
            }
        }

        test_parse_invalid_hour() : int {
            try {
                k::time::ZoneOffset::parseIso("+19:00");
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
                return 1;
            }
        }

        test_parse_invalid_bound() : int {
            try {
                k::time::ZoneOffset::parseIso("+18:01");
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
                return 1;
            }
        }

        test_parse_invalid_minute() : int {
            try {
                k::time::ZoneOffset::parseIso("+05:60");
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
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

    check("test_format_utc");
    check("test_format_plus_hours");
    check("test_format_minus_hours");
    check("test_format_with_seconds");
    check("test_format_minus_with_seconds");
    check("test_parse_utc");
    check("test_parse_plus");
    check("test_parse_minus");
    check("test_parse_seconds");
    check("test_parse_compact");
    check("test_parse_compact_seconds");
    check("test_parse_invalid_syntax");
    check("test_parse_invalid_hour");
    check("test_parse_invalid_bound");
    check("test_parse_invalid_minute");
}

// ═════════════════════════════════════════════════════════════════════════════
// 3. ZoneOffset: comparisons and operators
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("ZoneOffset: comparisons and operators", "[libk][time][zone_offset]") {
    auto jit = jit_k(R"SRC(
        module __test_zone_offset_ops__;

        test_equality() : int {
            z1 : k::time::ZoneOffset = k::time::ZoneOffset::ofTotalSeconds(3600);
            z2 : k::time::ZoneOffset = k::time::ZoneOffset::ofTotalSeconds(3600);
            z3 : k::time::ZoneOffset = k::time::ZoneOffset::ofTotalSeconds(7200);

            if (!(z1 == z2)) return 0;
            if (z1 != z2) return 0;
            if (!(z1 != z3)) return 0;
            if (z1 == z3) return 0;
            return 1;
        }

        test_relational() : int {
            zNeg : k::time::ZoneOffset = k::time::ZoneOffset::ofTotalSeconds(-3600);
            zUtc : k::time::ZoneOffset = k::time::ZoneOffset::utc();
            zPos : k::time::ZoneOffset = k::time::ZoneOffset::ofTotalSeconds(3600);

            if (!(zNeg < zUtc)) return 0;
            if (!(zUtc < zPos)) return 0;
            if (!(zNeg <= zUtc)) return 0;
            if (!(zUtc <= zUtc)) return 0;
            if (!(zPos > zUtc)) return 0;
            if (!(zUtc > zNeg)) return 0;
            if (!(zPos >= zUtc)) return 0;
            if (!(zUtc >= zUtc)) return 0;

            if (zNeg.compareTo(zUtc) >= 0) return 0;
            if (zUtc.compareTo(zNeg) <= 0) return 0;
            if (zUtc.compareTo(zUtc) != 0) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto check = [&](const char* sym) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == 1);
    };

    check("test_equality");
    check("test_relational");
}

// ═════════════════════════════════════════════════════════════════════════════
// 4. Iso: formatDuration and parseDuration round-trip
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Iso: formatDuration and parseDuration round-trip", "[libk][time][iso]") {
    auto jit = jit_k(R"SRC(
        module __test_iso_duration__;

        test_zero() : int {
            d : k::time::Duration = k::time::Duration::zero();
            s : k::String = k::time::Iso::formatDuration(d);
            if (s != k::String("PT0S")) return 0;
            parsed : k::time::Duration = k::time::Iso::parseDuration(s);
            if (parsed.secondsPart() != 0L || parsed.nanoAdjustment() != 0) return 0;
            return 1;
        }

        test_positive_seconds() : int {
            d : k::time::Duration = k::time::Duration::ofSeconds(42L);
            s : k::String = k::time::Iso::formatDuration(d);
            if (s != k::String("PT42S")) return 0;
            parsed : k::time::Duration = k::time::Iso::parseDuration(s);
            if (parsed.secondsPart() != 42L || parsed.nanoAdjustment() != 0) return 0;
            return 1;
        }

        test_negative_seconds() : int {
            d : k::time::Duration = k::time::Duration::ofSeconds(-1L);
            s : k::String = k::time::Iso::formatDuration(d);
            if (s != k::String("PT-1S")) return 0;
            parsed : k::time::Duration = k::time::Iso::parseDuration(s);
            if (parsed.secondsPart() != -1L || parsed.nanoAdjustment() != 0) return 0;
            return 1;
        }

        test_fractional_positive() : int {
            d : k::time::Duration = k::time::Duration::ofSeconds(1L, 500000000);
            s : k::String = k::time::Iso::formatDuration(d);
            if (s != k::String("PT1.5S")) return 0;
            parsed : k::time::Duration = k::time::Iso::parseDuration(s);
            if (parsed.secondsPart() != 1L || parsed.nanoAdjustment() != 500000000) return 0;
            return 1;
        }

        test_fractional_negative() : int {
            // -1.5s = secondsPart -2, nanos 500_000_000
            d : k::time::Duration = k::time::Duration(-2L, 500000000);
            s : k::String = k::time::Iso::formatDuration(d);
            if (s != k::String("PT-1.5S")) return 0;
            parsed : k::time::Duration = k::time::Iso::parseDuration(s);
            if (parsed.secondsPart() != -2L || parsed.nanoAdjustment() != 500000000) return 0;
            return 1;
        }

        test_sub_second_negative() : int {
            // -0.000000001s = secondsPart -1, nanos 999_999_999
            d : k::time::Duration = k::time::Duration(-1L, 999999999);
            s : k::String = k::time::Iso::formatDuration(d);
            if (s != k::String("PT-0.000000001S")) return 0;
            parsed : k::time::Duration = k::time::Iso::parseDuration(s);
            if (parsed.secondsPart() != -1L || parsed.nanoAdjustment() != 999999999) return 0;
            return 1;
        }

        test_sub_second_positive() : int {
            d : k::time::Duration = k::time::Duration(0L, 1);
            s : k::String = k::time::Iso::formatDuration(d);
            if (s != k::String("PT0.000000001S")) return 0;
            parsed : k::time::Duration = k::time::Iso::parseDuration(s);
            if (parsed.secondsPart() != 0L || parsed.nanoAdjustment() != 1) return 0;
            return 1;
        }

        test_hours_minutes_seconds_parse() : int {
            // PT1H2M3S = 3600 + 120 + 3 = 3723s
            parsed : k::time::Duration = k::time::Iso::parseDuration("PT1H2M3S");
            if (parsed.secondsPart() != 3723L || parsed.nanoAdjustment() != 0) return 0;
            s : k::String = k::time::Iso::formatDuration(parsed);
            if (s != k::String("PT3723S")) return 0;
            return 1;
        }

        test_negative_prefix_parse() : int {
            // -PT1H = -3600s
            parsed : k::time::Duration = k::time::Iso::parseDuration("-PT1H");
            if (parsed.secondsPart() != -3600L || parsed.nanoAdjustment() != 0) return 0;
            return 1;
        }

        test_extreme_durations() : int {
            maxD : k::time::Duration = k::time::Iso::parseDuration("PT9223372036854775807S");
            if (maxD.secondsPart() != 9223372036854775807L) return 0;
            if (k::time::Iso::formatDuration(maxD) != k::String("PT9223372036854775807S")) return 0;

            minD : k::time::Duration = k::time::Iso::parseDuration("PT-9223372036854775808S");
            if (minD.secondsPart() != (-9223372036854775807L - 1L)) return 0;
            if (k::time::Iso::formatDuration(minD) != k::String("PT-9223372036854775808S")) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto check = [&](const char* sym) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == 1);
    };

    check("test_zero");
    check("test_positive_seconds");
    check("test_negative_seconds");
    check("test_fractional_positive");
    check("test_fractional_negative");
    check("test_sub_second_negative");
    check("test_sub_second_positive");
    check("test_hours_minutes_seconds_parse");
    check("test_negative_prefix_parse");
    check("test_extreme_durations");
}

// ═════════════════════════════════════════════════════════════════════════════
// 5. Iso: formatLocalDate and parseLocalDate round-trip
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Iso: formatLocalDate and parseLocalDate round-trip", "[libk][time][iso]") {
    auto jit = jit_k(R"SRC(
        module __test_iso_local_date__;

        test_standard_date() : int {
            d : k::time::LocalDate = k::time::LocalDate::of(2026L, 9, 10);
            s : k::String = k::time::Iso::formatLocalDate(d);
            if (s != k::String("2026-09-10")) return 0;
            parsed : k::time::LocalDate = k::time::Iso::parseLocalDate(s);
            if (parsed.year() != 2026L || parsed.month() != 9 || parsed.day() != 10) return 0;
            return 1;
        }

        test_leap_year() : int {
            d : k::time::LocalDate = k::time::LocalDate::of(2024L, 2, 29);
            s : k::String = k::time::Iso::formatLocalDate(d);
            if (s != k::String("2024-02-29")) return 0;
            parsed : k::time::LocalDate = k::time::Iso::parseLocalDate(s);
            if (parsed.year() != 2024L || parsed.month() != 2 || parsed.day() != 29) return 0;
            return 1;
        }

        test_century_leap_year() : int {
            d : k::time::LocalDate = k::time::LocalDate::of(2000L, 2, 29);
            s : k::String = k::time::Iso::formatLocalDate(d);
            if (s != k::String("2000-02-29")) return 0;
            parsed : k::time::LocalDate = k::time::Iso::parseLocalDate(s);
            if (parsed.year() != 2000L || parsed.month() != 2 || parsed.day() != 29) return 0;
            return 1;
        }

        test_year_zero() : int {
            d : k::time::LocalDate = k::time::LocalDate::of(0L, 1, 1);
            s : k::String = k::time::Iso::formatLocalDate(d);
            if (s != k::String("0000-01-01")) return 0;
            parsed : k::time::LocalDate = k::time::Iso::parseLocalDate(s);
            if (parsed.year() != 0L || parsed.month() != 1 || parsed.day() != 1) return 0;
            return 1;
        }

        test_negative_year() : int {
            d : k::time::LocalDate = k::time::LocalDate::of(-1L, 12, 31);
            s : k::String = k::time::Iso::formatLocalDate(d);
            if (s != k::String("-0001-12-31")) return 0;
            parsed : k::time::LocalDate = k::time::Iso::parseLocalDate(s);
            if (parsed.year() != -1L || parsed.month() != 12 || parsed.day() != 31) return 0;
            return 1;
        }

        test_large_year() : int {
            d : k::time::LocalDate = k::time::LocalDate::of(10000L, 1, 1);
            s : k::String = k::time::Iso::formatLocalDate(d);
            if (s != k::String("+10000-01-01")) return 0;
            parsed : k::time::LocalDate = k::time::Iso::parseLocalDate(s);
            if (parsed.year() != 10000L || parsed.month() != 1 || parsed.day() != 1) return 0;
            return 1;
        }

        test_invalid_feb29() : int {
            try {
                k::time::Iso::parseLocalDate("2026-02-29");
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
                return 1;
            }
        }

        test_invalid_century_feb29() : int {
            try {
                k::time::Iso::parseLocalDate("1900-02-29");
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
                return 1;
            }
        }

        test_invalid_month() : int {
            try {
                k::time::Iso::parseLocalDate("2026-13-01");
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
                return 1;
            }
        }

        test_invalid_day() : int {
            try {
                k::time::Iso::parseLocalDate("2026-04-31");
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
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

    check("test_standard_date");
    check("test_leap_year");
    check("test_century_leap_year");
    check("test_year_zero");
    check("test_negative_year");
    check("test_large_year");
    check("test_invalid_feb29");
    check("test_invalid_century_feb29");
    check("test_invalid_month");
    check("test_invalid_day");
}

// ═════════════════════════════════════════════════════════════════════════════
// 6. Iso: formatLocalTime and parseLocalTime round-trip
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Iso: formatLocalTime and parseLocalTime round-trip", "[libk][time][iso]") {
    auto jit = jit_k(R"SRC(
        module __test_iso_local_time__;

        test_midnight() : int {
            t : k::time::LocalTime = k::time::LocalTime::midnight();
            s : k::String = k::time::Iso::formatLocalTime(t);
            if (s != k::String("00:00:00")) return 0;
            parsed : k::time::LocalTime = k::time::Iso::parseLocalTime(s);
            if (parsed.hour() != 0 || parsed.minute() != 0 || parsed.second() != 0 || parsed.nano() != 0) return 0;
            return 1;
        }

        test_noon() : int {
            t : k::time::LocalTime = k::time::LocalTime::of(12, 0, 0);
            s : k::String = k::time::Iso::formatLocalTime(t);
            if (s != k::String("12:00:00")) return 0;
            parsed : k::time::LocalTime = k::time::Iso::parseLocalTime(s);
            if (parsed.hour() != 12 || parsed.minute() != 0 || parsed.second() != 0) return 0;
            return 1;
        }

        test_with_nanos() : int {
            t : k::time::LocalTime = k::time::LocalTime::of(12, 34, 56, 123456789);
            s : k::String = k::time::Iso::formatLocalTime(t);
            if (s != k::String("12:34:56.123456789")) return 0;
            parsed : k::time::LocalTime = k::time::Iso::parseLocalTime(s);
            if (parsed.hour() != 12 || parsed.minute() != 34 || parsed.second() != 56 || parsed.nano() != 123456789) return 0;
            return 1;
        }

        test_minimal_fraction() : int {
            t : k::time::LocalTime = k::time::LocalTime::of(8, 15, 30, 500000000);
            s : k::String = k::time::Iso::formatLocalTime(t);
            if (s != k::String("08:15:30.5")) return 0;
            parsed : k::time::LocalTime = k::time::Iso::parseLocalTime(s);
            if (parsed.hour() != 8 || parsed.minute() != 15 || parsed.second() != 30 || parsed.nano() != 500000000) return 0;
            return 1;
        }

        test_structural_leap_second() : int {
            t : k::time::LocalTime = k::time::LocalTime::of(23, 59, 60, 0);
            if (!t.isStructuralLeapSecond()) return 0;
            s : k::String = k::time::Iso::formatLocalTime(t);
            if (s != k::String("23:59:60")) return 0;
            parsed : k::time::LocalTime = k::time::Iso::parseLocalTime(s);
            if (parsed.hour() != 23 || parsed.minute() != 59 || parsed.second() != 60) return 0;
            if (!parsed.isStructuralLeapSecond()) return 0;
            return 1;
        }

        test_parse_hh_mm() : int {
            parsed : k::time::LocalTime = k::time::Iso::parseLocalTime("14:30");
            if (parsed.hour() != 14 || parsed.minute() != 30 || parsed.second() != 0 || parsed.nano() != 0) return 0;
            if (k::time::Iso::formatLocalTime(parsed) != k::String("14:30:00")) return 0;
            return 1;
        }

        test_reject_24_00() : int {
            try {
                k::time::Iso::parseLocalTime("24:00");
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
                return 1;
            }
        }

        test_reject_invalid_leap_second_hour() : int {
            try {
                k::time::Iso::parseLocalTime("12:00:60");
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
                return 1;
            }
        }

        test_reject_invalid_leap_second_minute() : int {
            try {
                k::time::Iso::parseLocalTime("23:58:60");
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
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

    check("test_midnight");
    check("test_noon");
    check("test_with_nanos");
    check("test_minimal_fraction");
    check("test_structural_leap_second");
    check("test_parse_hh_mm");
    check("test_reject_24_00");
    check("test_reject_invalid_leap_second_hour");
    check("test_reject_invalid_leap_second_minute");
}

// ═════════════════════════════════════════════════════════════════════════════
// 7. Iso: formatLocalDateTime and parseLocalDateTime round-trip
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Iso: formatLocalDateTime and parseLocalDateTime round-trip", "[libk][time][iso]") {
    auto jit = jit_k(R"SRC(
        module __test_iso_local_date_time__;

        test_round_trip_standard() : int {
            d : k::time::LocalDate = k::time::LocalDate::of(2026L, 9, 10);
            t : k::time::LocalTime = k::time::LocalTime::of(12, 34, 56);
            dt : k::time::LocalDateTime = k::time::LocalDateTime::of(d, t);

            s : k::String = k::time::Iso::formatLocalDateTime(dt);
            if (s != k::String("2026-09-10T12:34:56")) return 0;

            parsed : k::time::LocalDateTime = k::time::Iso::parseLocalDateTime(s);
            if (parsed.year() != 2026L || parsed.month() != 9 || parsed.day() != 10) return 0;
            if (parsed.hour() != 12 || parsed.minute() != 34 || parsed.second() != 56) return 0;
            return 1;
        }

        test_round_trip_with_nanos() : int {
            d : k::time::LocalDate = k::time::LocalDate::of(2026L, 1, 15);
            t : k::time::LocalTime = k::time::LocalTime::of(23, 59, 59, 123400000);
            dt : k::time::LocalDateTime = k::time::LocalDateTime::of(d, t);

            s : k::String = k::time::Iso::formatLocalDateTime(dt);
            if (s != k::String("2026-01-15T23:59:59.1234")) return 0;

            parsed : k::time::LocalDateTime = k::time::Iso::parseLocalDateTime(s);
            if (parsed.year() != 2026L || parsed.month() != 1 || parsed.day() != 15) return 0;
            if (parsed.hour() != 23 || parsed.minute() != 59 || parsed.second() != 59 || parsed.nano() != 123400000) return 0;
            return 1;
        }

        test_round_trip_leap_second() : int {
            d : k::time::LocalDate = k::time::LocalDate::of(2016L, 12, 31);
            t : k::time::LocalTime = k::time::LocalTime::of(23, 59, 60);
            dt : k::time::LocalDateTime = k::time::LocalDateTime::of(d, t);

            s : k::String = k::time::Iso::formatLocalDateTime(dt);
            if (s != k::String("2016-12-31T23:59:60")) return 0;

            parsed : k::time::LocalDateTime = k::time::Iso::parseLocalDateTime(s);
            if (parsed.second() != 60) return 0;
            return 1;
        }

        test_missing_t() : int {
            try {
                k::time::Iso::parseLocalDateTime("2026-09-10 12:34:56");
                return 0;
            } catch (e: k::time::TemporalParseException&) {
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

    check("test_round_trip_standard");
    check("test_round_trip_with_nanos");
    check("test_round_trip_leap_second");
    check("test_missing_t");
}

// ═════════════════════════════════════════════════════════════════════════════
// 8. Iso: rejection tests (whitespace, punctuation, over-precision)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Iso: rejection tests", "[libk][time][iso]") {
    auto jit = jit_k(R"SRC(
        module __test_iso_rejection__;

        test_reject_date_whitespace() : int {
            try {
                k::time::Iso::parseLocalDate(" 2026-09-10");
                return 0;
            } catch (e: k::time::TemporalParseException&) {
                // ok
            }
            try {
                k::time::Iso::parseLocalDate("2026-09-10 ");
                return 0;
            } catch (e: k::time::TemporalParseException&) {
                // ok
            }
            return 1;
        }

        test_reject_date_punctuation() : int {
            try {
                k::time::Iso::parseLocalDate("2026/09/10");
                return 0;
            } catch (e: k::time::TemporalParseException&) {
                return 1;
            }
        }

        test_reject_date_letters() : int {
            try {
                k::time::Iso::parseLocalDate("202a-09-10");
                return 0;
            } catch (e: k::time::TemporalParseException&) {
                return 1;
            }
        }

        test_reject_time_over_precision() : int {
            // 10 fractional digits (> 9)
            try {
                k::time::Iso::parseLocalTime("12:34:56.1234567890");
                return 0;
            } catch (e: k::time::TemporalParseException&) {
                return 1;
            }
        }

        test_reject_time_empty_fraction() : int {
            try {
                k::time::Iso::parseLocalTime("12:34:56.");
                return 0;
            } catch (e: k::time::TemporalParseException&) {
                return 1;
            }
        }

        test_reject_time_whitespace() : int {
            try {
                k::time::Iso::parseLocalTime(" 12:34:56");
                return 0;
            } catch (e: k::time::TemporalParseException&) {
                // ok
            }
            try {
                k::time::Iso::parseLocalTime("12:34:56 ");
                return 0;
            } catch (e: k::time::TemporalParseException&) {
                // ok
            }
            return 1;
        }

        test_reject_duration_over_precision() : int {
            try {
                k::time::Iso::parseDuration("PT1.1234567890S");
                return 0;
            } catch (e: k::time::TemporalParseException&) {
                return 1;
            }
        }

        test_reject_duration_whitespace() : int {
            try {
                k::time::Iso::parseDuration(" PT1S");
                return 0;
            } catch (e: k::time::TemporalParseException&) {
                return 1;
            }
        }

        test_reject_duration_no_components() : int {
            try {
                k::time::Iso::parseDuration("PT");
                return 0;
            } catch (e: k::time::TemporalParseException&) {
                return 1;
            }
        }

        test_reject_duration_out_of_order() : int {
            try {
                k::time::Iso::parseDuration("PT1S2M");
                return 0;
            } catch (e: k::time::TemporalParseException&) {
                return 1;
            }
        }

        test_reject_duration_fraction_on_hour() : int {
            try {
                k::time::Iso::parseDuration("PT1.5H");
                return 0;
            } catch (e: k::time::TemporalParseException&) {
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

    check("test_reject_date_whitespace");
    check("test_reject_date_punctuation");
    check("test_reject_date_letters");
    check("test_reject_time_over_precision");
    check("test_reject_time_empty_fraction");
    check("test_reject_time_whitespace");
    check("test_reject_duration_over_precision");
    check("test_reject_duration_whitespace");
    check("test_reject_duration_no_components");
    check("test_reject_duration_out_of_order");
    check("test_reject_duration_fraction_on_hour");
}
