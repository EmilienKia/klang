/*
 * K Language standard library — WeekRules, Locale, TemporalFormatter & TemporalParser tests (Package D8)
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
// 1. WeekRules tests
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Locale: WeekRules ISO defaults and custom configurations", "[libk][time][locale][weekrules]") {
    auto jit = jit_k(R"SRC(
        module __test_week_rules__;

        test_week_rules_iso() : int {
            iso : k::time::WeekRules = k::time::WeekRules::iso();
            if (iso.firstDayOfWeek() != 1) return 0; // Monday
            if (iso.minimalDaysInFirstWeek() != 4) return 0; // 4 days

            return 1;
        }

        test_week_rules_custom() : int throws(k::time::InvalidTemporalValueException) {
            us : k::time::WeekRules = k::time::WeekRules::of(7, 1); // Sunday, 1 day
            if (us.firstDayOfWeek() != 7) return 0;
            if (us.minimalDaysInFirstWeek() != 1) return 0;

            if (us == k::time::WeekRules::iso()) return 0;
            if (!(us != k::time::WeekRules::iso())) return 0;

            return 1;
        }

        test_week_rules_invalid() : int {
            try {
                k::time::WeekRules::of(0, 4); // day 0 invalid
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
                // Expected
            }

            try {
                k::time::WeekRules::of(8, 4); // day 8 invalid
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
                // Expected
            }

            try {
                k::time::WeekRules::of(1, 0); // min days 0 invalid
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
                // Expected
            }

            try {
                k::time::WeekRules::of(1, 8); // min days 8 invalid
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
                // Expected
            }

            return 1;
        }
    )SRC");

    REQUIRE(jit != nullptr);
    auto check = [&](const char* sym) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == 1);
    };

    check("test_week_rules_iso");
    check("test_week_rules_custom");
    check("test_week_rules_invalid");
}

// ═════════════════════════════════════════════════════════════════════════════
// 2. Locale creation, system locale, and configuration
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Locale: BCP 47 creation, validation, system locale and immutability", "[libk][time][locale][core]") {
    auto jit = jit_k(R"SRC(
        module __test_locale_core__;

        test_locale_valid() : int throws(k::time::InvalidTemporalValueException, k::time::TimeDataUnavailableException) {
            enUs : k::time::Locale = k::time::Locale::of(k::String("en-US"));
            if (enUs.languageTag() != k::String("en-US")) return 0;
            if (enUs.weekRules() != k::time::WeekRules::iso()) return 0;
            if (enUs.chronology() != null) return 0;

            frFr : k::time::Locale = k::time::Locale::of(k::String("fr-FR"));
            if (frFr.languageTag() != k::String("fr-FR")) return 0;

            if (enUs == frFr) return 0;
            if (!(enUs != frFr)) return 0;

            return 1;
        }

        test_locale_with_configuration() : int throws(k::time::InvalidTemporalValueException, k::time::TimeDataUnavailableException) {
            base : k::time::Locale = k::time::Locale::of(k::String("en-US"));
            customRules : k::time::WeekRules = k::time::WeekRules::of(7, 1);
            withRules : k::time::Locale = base.withWeekRules(customRules);

            // Immutability: base is unchanged
            if (base.weekRules() != k::time::WeekRules::iso()) return 0;
            if (withRules.weekRules() != customRules) return 0;

            greg : k::time::GregorianChronology& = k::time::GregorianChronology::instance();
            withChron : k::time::Locale = base.withChronology(greg);
            if (base.chronology() != null) return 0;
            if (withChron.chronology() == null) return 0;

            return 1;
        }

        test_locale_system() : int {
            try {
                sys : k::time::Locale = k::time::Locale::system();
                if (sys.languageTag().empty()) return 0;
            } catch (e: k::time::TimeDataUnavailableException&) {
                // If system locale data unavailable in environment, valid to throw
            }
            return 1;
        }

        test_locale_invalid() : int {
            try {
                k::time::Locale::of(k::String(""));
                return 0; // expected exception
            } catch (e: k::time::InvalidTemporalValueException&) {
                // Expected
            } catch (e2: k::time::TimeDataUnavailableException&) {
                return 0;
            }

            try {
                k::time::Locale::of(k::String("-invalid-tag-"));
                return 0; // expected exception
            } catch (e: k::time::InvalidTemporalValueException&) {
                // Expected
            } catch (e2: k::time::TimeDataUnavailableException&) {
                return 0;
            }

            return 1;
        }
    )SRC");

    REQUIRE(jit != nullptr);
    auto check = [&](const char* sym) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == 1);
    };

    check("test_locale_valid");
    check("test_locale_with_configuration");
    check("test_locale_system");
    check("test_locale_invalid");
}

// ═════════════════════════════════════════════════════════════════════════════
// 3. TemporalFormatters and TemporalParsers ISO tests
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("TemporalFormatter & TemporalParser: ISO formatting and parsing", "[libk][time][formatter][iso]") {
    auto jit = jit_k(R"SRC(
        module __test_formatter_iso__;

        test_iso_format() : int throws(k::time::InvalidTemporalValueException, k::time::TimeDataUnavailableException) {
            fmt : k::time::TemporalFormatter! = k::time::TemporalFormatters::iso();

            ld : k::time::LocalDate = k::time::LocalDate::of(2026L, 9, 11);
            sDate : k::String = fmt->formatLocalDate(ld);
            if (sDate != k::String("2026-09-11")) return 0;

            lt : k::time::LocalTime = k::time::LocalTime::of(8, 30, 0);
            ldt : k::time::LocalDateTime = k::time::LocalDateTime::of(ld, lt);
            sDt : k::String = fmt->formatLocalDateTime(ldt);
            if (sDt != k::String("2026-09-11T08:30:00")) return 0;

            tzUtc : k::time::TimeZone = k::time::TimeZone::fixed(k::time::ZoneOffset::utc());
            zdt : k::time::ZonedDateTime = k::time::ZonedDateTime::of(ldt, tzUtc);
            sZdt : k::String = fmt->formatZonedDateTime(zdt);
            if (sZdt != k::String("2026-09-11T08:30:00Z[UTC]")) return 0;

            sInst : k::String = fmt->formatInstant(zdt.instant(), tzUtc);
            if (sInst != k::String("2026-09-11T08:30:00Z")) return 0;

            greg : k::time::GregorianChronology& = k::time::GregorianChronology::instance();
            calDate : k::time::CalendarDate = k::time::CalendarDate::of(greg, 2026L, 9, 11);
            calDt : k::time::CalendarDateTime = k::time::CalendarDateTime::of(calDate, lt);
            sCalDt : k::String = fmt->formatCalendarDateTime(calDt);
            if (sCalDt != k::String("2026-09-11T08:30:00")) return 0;

            return 1;
        }

        test_iso_parse() : int throws(k::time::TemporalParseException, k::time::InvalidTemporalValueException,
                                      k::time::LeapSecondException, k::time::TimeScaleDataUnavailableException,
                                      k::time::ZoneRulesUnavailableException, k::time::LocalTimeGapException,
                                      k::time::LocalTimeOverlapException, k::time::TimeDataUnavailableException) {
            parser : k::time::TemporalParser! = k::time::TemporalParsers::iso();

            ld : k::time::LocalDate = parser->parseLocalDate(k::String("2026-09-11"));
            if (ld.year() != 2026L || ld.month() != 9 || ld.day() != 11) return 0;

            ldt : k::time::LocalDateTime = parser->parseLocalDateTime(k::String("2026-09-11T08:30:00"));
            if (ldt.year() != 2026L || ldt.month() != 9 || ldt.day() != 11) return 0;
            if (ldt.hour() != 8 || ldt.minute() != 30 || ldt.second() != 0) return 0;

            calDt : k::time::CalendarDateTime = parser->parseCalendarDateTime(k::String("2026-09-11T08:30:00"));
            if (calDt.date().fields().yearOfEra() != 2026L) return 0;
            if (calDt.date().fields().month().number() != 9) return 0;
            if (calDt.date().fields().day() != 11) return 0;
            if (calDt.time().hour() != 8 || calDt.time().minute() != 30) return 0;

            return 1;
        }
    )SRC");

    REQUIRE(jit != nullptr);
    auto check = [&](const char* sym) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == 1);
    };

    check("test_iso_format");
    check("test_iso_parse");
}

// ═════════════════════════════════════════════════════════════════════════════
// 4. Pattern formatting and parsing tests
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("TemporalFormatter & TemporalParser: pattern formatting and parsing", "[libk][time][formatter][pattern]") {
    auto jit = jit_k(R"SRC(
        module __test_formatter_pattern__;

        test_pattern_date_time() : int throws(k::time::TemporalParseException, k::time::TimeDataUnavailableException,
                                              k::time::InvalidTemporalValueException) {
            fmt : k::time::TemporalFormatter! = k::time::TemporalFormatters::ofPattern(k::String("yyyy-MM-dd HH:mm:ss"));
            parser : k::time::TemporalParser! = k::time::TemporalParsers::ofPattern(k::String("yyyy-MM-dd HH:mm:ss"));

            ld : k::time::LocalDate = k::time::LocalDate::of(2026L, 9, 11);
            lt : k::time::LocalTime = k::time::LocalTime::of(8, 30, 0);
            ldt : k::time::LocalDateTime = k::time::LocalDateTime::of(ld, lt);

            s : k::String = fmt->formatLocalDateTime(ldt);
            if (s != k::String("2026-09-11 08:30:00")) return 0;

            parsed : k::time::LocalDateTime = parser->parseLocalDateTime(s);
            if (parsed.year() != 2026L || parsed.month() != 9 || parsed.day() != 11) return 0;
            if (parsed.hour() != 8 || parsed.minute() != 30 || parsed.second() != 0) return 0;

            return 1;
        }

        test_pattern_slash_date() : int throws(k::time::TemporalParseException, k::time::TimeDataUnavailableException,
                                               k::time::InvalidTemporalValueException) {
            fmt : k::time::TemporalFormatter! = k::time::TemporalFormatters::ofPattern(k::String("dd/MM/yyyy"));
            parser : k::time::TemporalParser! = k::time::TemporalParsers::ofPattern(k::String("dd/MM/yyyy"));

            ld : k::time::LocalDate = k::time::LocalDate::of(2026L, 9, 11);
            s : k::String = fmt->formatLocalDate(ld);
            if (s != k::String("11/09/2026")) return 0;

            parsed : k::time::LocalDate = parser->parseLocalDate(s);
            if (parsed.year() != 2026L || parsed.month() != 9 || parsed.day() != 11) return 0;

            return 1;
        }

        test_pattern_immutability() : int throws(k::time::TemporalParseException, k::time::TimeDataUnavailableException,
                                                 k::time::InvalidTemporalValueException) {
            fmt1 : k::time::TemporalFormatter! = k::time::TemporalFormatters::ofPattern(k::String("yyyy-MM-dd"));
            locFr : k::time::Locale = k::time::Locale::of(k::String("fr-FR"));
            fmt2 : k::time::TemporalFormatter! = fmt1->withLocale(locFr);

            // fmt1 and fmt2 are distinct independent instances
            tzUtc : k::time::TimeZone = k::time::TimeZone::fixed(k::time::ZoneOffset::utc());
            fmt3 : k::time::TemporalFormatter! = fmt1->withZone(tzUtc);

            greg : k::time::GregorianChronology& = k::time::GregorianChronology::instance();
            fmt4 : k::time::TemporalFormatter! = fmt1->withChronology(greg);

            ld : k::time::LocalDate = k::time::LocalDate::of(2026L, 9, 11);
            if (fmt1->formatLocalDate(ld) != k::String("2026-09-11")) return 0;
            if (fmt2->formatLocalDate(ld) != k::String("2026-09-11")) return 0;
            if (fmt3->formatLocalDate(ld) != k::String("2026-09-11")) return 0;
            if (fmt4->formatLocalDate(ld) != k::String("2026-09-11")) return 0;

            // Parser immutability
            p1 : k::time::TemporalParser! = k::time::TemporalParsers::ofPattern(k::String("yyyy-MM-dd"));
            p2 : k::time::TemporalParser! = p1->withLocale(locFr);
            p3 : k::time::TemporalParser! = p1->withZone(tzUtc);
            p4 : k::time::TemporalParser! = p1->withChronology(greg);

            ld1 : k::time::LocalDate = p1->parseLocalDate(k::String("2026-09-11"));
            ld2 : k::time::LocalDate = p2->parseLocalDate(k::String("2026-09-11"));
            if (ld1 != ld2) return 0;

            return 1;
        }

        test_pattern_instant_and_calendar() : int
            throws(k::time::TemporalParseException, k::time::InvalidTemporalValueException,
                   k::time::TimeDataUnavailableException, k::time::LeapSecondException,
                   k::time::TimeScaleDataUnavailableException) {
            parser : k::time::TemporalParser! = k::time::TemporalParsers::ofPattern(k::String("yyyy-MM-dd HH:mm:ss"));
            inst : k::time::Instant = parser->parseInstant(k::String("1970-01-01 00:00:00"));
            if (inst.toPosixSeconds() != 0L) return 0;

            jul : k::time::JulianChronology& = k::time::JulianChronology::instance();
            pJul : k::time::TemporalParser! = parser->withChronology(jul);
            cdt : k::time::CalendarDateTime = pJul->parseCalendarDateTime(k::String("1900-02-28 10:15:30"));
            if (cdt.chronology().id() != jul.id()) return 0;
            if (cdt.date().fields().yearOfEra() != 1900L) return 0;
            if (cdt.date().fields().month().number() != 2) return 0;
            if (cdt.date().fields().day() != 28) return 0;
            if (cdt.time().hour() != 10 || cdt.time().minute() != 15) return 0;

            return 1;
        }
    )SRC");

    REQUIRE(jit != nullptr);
    auto check = [&](const char* sym) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == 1);
    };

    check("test_pattern_date_time");
    check("test_pattern_slash_date");
    check("test_pattern_immutability");
    check("test_pattern_instant_and_calendar");
}
