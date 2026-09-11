/*
 * K Language standard library — Chronology, CalendarDate & CalendarDateTime tests (Package D8)
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
// 1. ChronologyId tests
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Chronology: ChronologyId creation, equality, and validation", "[libk][time][chronology][id]") {
    auto jit = jit_k(R"SRC(
        module __test_chronology_id__;

        test_chronology_id_standard() : int throws(k::time::InvalidTemporalValueException) {
            idGreg : k::time::ChronologyId = k::time::ChronologyId::gregorian();
            if (idGreg.value() != k::String("Gregorian")) return 0;

            idIso : k::time::ChronologyId = k::time::ChronologyId::iso();
            if (idIso.value() != k::String("ISO")) return 0;

            idJul : k::time::ChronologyId = k::time::ChronologyId::julian();
            if (idJul.value() != k::String("Julian")) return 0;

            idJap : k::time::ChronologyId = k::time::ChronologyId::japanese();
            if (idJap.value() != k::String("Japanese")) return 0;

            idBud : k::time::ChronologyId = k::time::ChronologyId::buddhist();
            if (idBud.value() != k::String("Buddhist")) return 0;

            idHeb : k::time::ChronologyId = k::time::ChronologyId::hebrew();
            if (idHeb.value() != k::String("Hebrew")) return 0;

            idIsl : k::time::ChronologyId = k::time::ChronologyId::islamic();
            if (idIsl.value() != k::String("Islamic")) return 0;

            if (idGreg == idJul) return 0;
            if (!(idGreg != idJul)) return 0;
            if (idGreg != k::time::ChronologyId::of(k::String("Gregorian"))) return 0;

            return 1;
        }

        test_chronology_id_invalid() : int {
            try {
                k::time::ChronologyId::of(k::String(""));
                return 0; // expected exception
            } catch (e: k::time::InvalidTemporalValueException&) {
                // Expected
            }

            try {
                k::time::ChronologyId::of(k::String("Bad Chronology Name!"));
                return 0; // expected exception
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

    check("test_chronology_id_standard");
    check("test_chronology_id_invalid");
}

// ═════════════════════════════════════════════════════════════════════════════
// 2. GregorianChronology and JulianChronology rules
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Chronology: Gregorian and Julian rules", "[libk][time][chronology][rules]") {
    auto jit = jit_k(R"SRC(
        module __test_chronology_rules__;

        test_gregorian_rules() : int throws(k::time::InvalidTemporalValueException) {
            greg : k::time::GregorianChronology& = k::time::GregorianChronology::instance();

            if (greg.id() != k::time::ChronologyId::gregorian()) return 0;
            if (!greg.isLeapYear(2024L)) return 0;
            if (greg.isLeapYear(2026L)) return 0;
            if (greg.isLeapYear(1900L)) return 0; // 1900 not leap in Gregorian
            if (!greg.isLeapYear(2000L)) return 0; // 2000 leap in Gregorian

            m2 : k::time::CalendarMonth = k::time::CalendarMonth::of(2, false);
            if (greg.daysInMonth(2024L, m2) != 29) return 0;
            if (greg.daysInMonth(2026L, m2) != 28) return 0;
            if (greg.daysInMonth(1900L, m2) != 28) return 0;

            if (greg.monthsInYear(2026L) != 12) return 0;

            return 1;
        }

        test_julian_rules() : int throws(k::time::InvalidTemporalValueException) {
            jul : k::time::JulianChronology& = k::time::JulianChronology::instance();

            if (jul.id() != k::time::ChronologyId::julian()) return 0;
            if (!jul.isLeapYear(2024L)) return 0;
            if (jul.isLeapYear(2025L)) return 0;
            if (!jul.isLeapYear(1900L)) return 0; // 1900 IS leap in Julian (1900 % 4 == 0)

            m2 : k::time::CalendarMonth = k::time::CalendarMonth::of(2, false);
            if (jul.daysInMonth(1900L, m2) != 29) return 0;
            if (jul.daysInMonth(1901L, m2) != 28) return 0;

            if (jul.monthsInYear(1900L) != 12) return 0;

            return 1;
        }
    )SRC");

    REQUIRE(jit != nullptr);
    auto check = [&](const char* sym) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == 1);
    };

    check("test_gregorian_rules");
    check("test_julian_rules");
}

// ═════════════════════════════════════════════════════════════════════════════
// 3. EpochDay <-> CalendarDate round trips
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Chronology: EpochDay <-> CalendarDate round trips", "[libk][time][chronology][roundtrip]") {
    auto jit = jit_k(R"SRC(
        module __test_chronology_roundtrip__;

        test_gregorian_roundtrip() : int throws(k::time::InvalidTemporalValueException) {
            greg : k::time::GregorianChronology& = k::time::GregorianChronology::instance();

            d1 : k::time::CalendarDate = k::time::CalendarDate::of(greg, 2026L, 9, 11);
            ed : k::time::EpochDay = d1.toEpochDay();
            d2 : k::time::CalendarDate = greg.fromEpochDay(ed);

            if (d1 != d2) return 0;
            if (d2.fields().yearOfEra() != 2026L) return 0;
            if (d2.fields().month().number() != 9) return 0;
            if (d2.fields().day() != 11) return 0;

            // Epoch 1970-01-01 should be epoch day 0
            dEpoch : k::time::CalendarDate = k::time::CalendarDate::of(greg, 1970L, 1, 1);
            if (dEpoch.toEpochDay().daysSinceEpoch() != 0L) return 0;

            return 1;
        }

        test_julian_roundtrip() : int throws(k::time::InvalidTemporalValueException) {
            jul : k::time::JulianChronology& = k::time::JulianChronology::instance();

            d1 : k::time::CalendarDate = k::time::CalendarDate::of(jul, 1900L, 2, 28);
            ed : k::time::EpochDay = d1.toEpochDay();
            d2 : k::time::CalendarDate = jul.fromEpochDay(ed);

            if (d1 != d2) return 0;
            if (d2.fields().yearOfEra() != 1900L) return 0;
            if (d2.fields().month().number() != 2) return 0;
            if (d2.fields().day() != 28) return 0;

            // Leap day in 1900 Julian
            dLeap : k::time::CalendarDate = k::time::CalendarDate::of(jul, 1900L, 2, 29);
            edLeap : k::time::EpochDay = dLeap.toEpochDay();
            dLeap2 : k::time::CalendarDate = jul.fromEpochDay(edLeap);
            if (dLeap != dLeap2) return 0;
            if (dLeap2.fields().day() != 29) return 0;

            return 1;
        }
    )SRC");

    REQUIRE(jit != nullptr);
    auto check = [&](const char* sym) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == 1);
    };

    check("test_gregorian_roundtrip");
    check("test_julian_roundtrip");
}

// ═════════════════════════════════════════════════════════════════════════════
// 4. Gregorian vs Julian date differences and cross-conversion
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Chronology: Gregorian vs Julian differences and toChronology()", "[libk][time][chronology][conversion]") {
    auto jit = jit_k(R"SRC(
        module __test_chronology_conversion__;

        test_1900_julian_vs_gregorian() : int throws(k::time::InvalidTemporalValueException) {
            greg : k::time::GregorianChronology& = k::time::GregorianChronology::instance();
            jul : k::time::JulianChronology& = k::time::JulianChronology::instance();

            // 1900-02-28 Julian is 1900-03-12 Gregorian
            dJul : k::time::CalendarDate = k::time::CalendarDate::of(jul, 1900L, 2, 28);
            dGreg : k::time::CalendarDate = dJul.toChronology(greg);

            if (dGreg.chronology().id() != greg.id()) return 0;
            if (dGreg.fields().yearOfEra() != 1900L) return 0;
            if (dGreg.fields().month().number() != 3) return 0;
            if (dGreg.fields().day() != 12) return 0;

            // Round trip back to Julian
            dJulRound : k::time::CalendarDate = dGreg.toChronology(jul);
            if (dJulRound != dJul) return 0;
            if (dJulRound.fields().yearOfEra() != 1900L) return 0;
            if (dJulRound.fields().month().number() != 2) return 0;
            if (dJulRound.fields().day() != 28) return 0;

            // Dates in different chronologies MUST NOT be equal even if same civil instant
            if (dJul == dGreg) return 0;
            if (!(dJul != dGreg)) return 0;

            // compareTo between different chronologies must throw InvalidTemporalValueException
            try {
                dJul.compareTo(dGreg);
                return 0; // expected exception
            } catch (e: k::time::InvalidTemporalValueException&) {
                // Expected
            }

            return 1;
        }

        test_to_local_date() : int throws(k::time::InvalidTemporalValueException) {
            jul : k::time::JulianChronology& = k::time::JulianChronology::instance();

            // 1900-02-28 Julian converted to LocalDate gives Gregorian 1900-03-12
            dJul : k::time::CalendarDate = k::time::CalendarDate::of(jul, 1900L, 2, 28);
            ld : k::time::LocalDate = dJul.toLocalDate();

            if (ld.year() != 1900L) return 0;
            if (ld.month() != 3) return 0;
            if (ld.day() != 12) return 0;

            return 1;
        }
    )SRC");

    REQUIRE(jit != nullptr);
    auto check = [&](const char* sym) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == 1);
    };

    check("test_1900_julian_vs_gregorian");
    check("test_to_local_date");
}

// ═════════════════════════════════════════════════════════════════════════════
// 5. CalendarDate plus/minus Period arithmetic
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Chronology: CalendarDate period arithmetic", "[libk][time][chronology][arithmetic]") {
    auto jit = jit_k(R"SRC(
        module __test_chronology_arithmetic__;

        test_calendar_date_arithmetic() : int throws(k::time::TemporalArithmeticException, k::time::InvalidTemporalValueException) {
            greg : k::time::GregorianChronology& = k::time::GregorianChronology::instance();
            jul : k::time::JulianChronology& = k::time::JulianChronology::instance();

            // Gregorian arithmetic
            dGreg : k::time::CalendarDate = k::time::CalendarDate::of(greg, 2026L, 9, 11);
            pMonths : k::time::Period = k::time::Period::months(2L);
            dGregPlus : k::time::CalendarDate = dGreg.plus(pMonths);
            if (dGregPlus.fields().month().number() != 11) return 0;
            if (dGregPlus.fields().day() != 11) return 0;

            dGregMinus : k::time::CalendarDate = dGregPlus.minus(pMonths);
            if (dGregMinus != dGreg) return 0;

            // Julian arithmetic across leap year boundary
            dJul : k::time::CalendarDate = k::time::CalendarDate::of(jul, 1900L, 2, 28);
            pDays : k::time::Period = k::time::Period::days(1L);
            dJulNext : k::time::CalendarDate = dJul.plus(pDays);
            // 1900 is leap year in Julian -> next day is Feb 29
            if (dJulNext.fields().yearOfEra() != 1900L) return 0;
            if (dJulNext.fields().month().number() != 2) return 0;
            if (dJulNext.fields().day() != 29) return 0;

            dJulMarch : k::time::CalendarDate = dJulNext.plus(pDays);
            if (dJulMarch.fields().month().number() != 3) return 0;
            if (dJulMarch.fields().day() != 1) return 0;

            return 1;
        }
    )SRC");

    REQUIRE(jit != nullptr);
    auto check = [&](const char* sym) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == 1);
    };

    check("test_calendar_date_arithmetic");
}

// ═════════════════════════════════════════════════════════════════════════════
// 6. CalendarDateTime atZone and ZonedDateTime toCalendarDateTime
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Chronology: CalendarDateTime.atZone and ZonedDateTime.toCalendarDateTime", "[libk][time][chronology][zdt]") {
    auto jit = jit_k(R"SRC(
        module __test_chronology_zdt__;

        test_calendar_datetime_zone() : int
            throws(k::time::LocalTimeGapException, k::time::LocalTimeOverlapException,
                   k::time::TemporalArithmeticException, k::time::InvalidTemporalValueException) {
            greg : k::time::GregorianChronology& = k::time::GregorianChronology::instance();
            jul : k::time::JulianChronology& = k::time::JulianChronology::instance();

            dGreg : k::time::CalendarDate = k::time::CalendarDate::of(greg, 2026L, 9, 11);
            t : k::time::LocalTime = k::time::LocalTime::of(14, 30, 0);
            cdtGreg : k::time::CalendarDateTime = k::time::CalendarDateTime::of(dGreg, t);

            tzUtc : k::time::TimeZone = k::time::TimeZone::fixed(k::time::ZoneOffset::utc());
            zdt : k::time::ZonedDateTime = cdtGreg.atZone(tzUtc);

            if (zdt.localDate().year() != 2026L) return 0;
            if (zdt.localDate().month() != 9) return 0;
            if (zdt.localDate().day() != 11) return 0;
            if (zdt.localTime().hour() != 14) return 0;
            if (zdt.localTime().minute() != 30) return 0;

            // Project back to CalendarDateTime in Gregorian
            cdtGreg2 : k::time::CalendarDateTime = zdt.toCalendarDateTime(greg);
            if (cdtGreg != cdtGreg2) return 0;

            // Project to CalendarDateTime in Julian
            cdtJul : k::time::CalendarDateTime = zdt.toCalendarDateTime(jul);
            if (cdtJul.chronology().id() != jul.id()) return 0;
            if (cdtJul.time().hour() != 14 || cdtJul.time().minute() != 30) return 0;

            return 1;
        }
    )SRC");

    REQUIRE(jit != nullptr);
    auto check = [&](const char* sym) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == 1);
    };

    check("test_calendar_datetime_zone");
}

// ═════════════════════════════════════════════════════════════════════════════
// 7. Era, CalendarMonth, and CalendarFields tests
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Chronology: Era, CalendarMonth, CalendarFields, and localized Era displayName", "[libk][time][chronology][fields]") {
    auto jit = jit_k(R"SRC(
        module __test_era_fields__;

        test_era_and_fields() : int throws(k::time::InvalidTemporalValueException, k::time::TimeDataUnavailableException) {
            eraAd : k::time::Era = k::time::Era::of(k::String("AD"));
            if (eraAd.id() != k::String("AD")) return 0;

            eraBc : k::time::Era = k::time::Era::of(k::String("BC"));
            if (eraAd == eraBc) return 0;
            if (!(eraAd != eraBc)) return 0;

            // Localized display name via ICU
            locFr : k::time::Locale = k::time::Locale::of(k::String("fr-FR"));
            nameFr : k::String = eraAd.displayName(locFr);
            if (nameFr.empty()) return 0;

            locEn : k::time::Locale = k::time::Locale::of(k::String("en-US"));
            nameEn : k::String = eraAd.displayName(locEn);
            if (nameEn.empty()) return 0;

            // CalendarMonth
            cm : k::time::CalendarMonth = k::time::CalendarMonth::of(9, false);
            if (cm.number() != 9) return 0;
            if (cm.isLeapMonth()) return 0;

            cmLeap : k::time::CalendarMonth = k::time::CalendarMonth::of(6, true);
            if (cmLeap.number() != 6) return 0;
            if (!cmLeap.isLeapMonth()) return 0;

            // CalendarFields
            eraView : k::time::Era? = &eraAd;
            cf : k::time::CalendarFields = k::time::CalendarFields::of(eraView, 2026L, cm, 11);
            if (!cf.hasEra()) return 0;
            if (cf.era() == null) return 0;
            if (cf.yearOfEra() != 2026L) return 0;
            if (cf.month() != cm) return 0;
            if (cf.day() != 11) return 0;

            cfNoEra : k::time::CalendarFields = k::time::CalendarFields::of(2026L, cm, 11);
            if (cfNoEra.hasEra()) return 0;
            if (cfNoEra.era() != null) return 0;

            return 1;
        }

        test_icu_chronologies() : int throws(k::time::InvalidTemporalValueException, k::time::TimeDataUnavailableException) {
            jap : k::time::JapaneseChronology& = k::time::JapaneseChronology::instance();
            if (jap.id() != k::time::ChronologyId::japanese()) return 0;

            // 2026-09-11 Gregorian is Reiwa 8, Month 9, Day 11 in Japanese calendar
            greg : k::time::GregorianChronology& = k::time::GregorianChronology::instance();
            dGreg : k::time::CalendarDate = k::time::CalendarDate::of(greg, 2026L, 9, 11);
            dJap : k::time::CalendarDate = dGreg.toChronology(jap);

            if (dJap.chronology().id() != jap.id()) return 0;
            if (dJap.fields().yearOfEra() != 8L) return 0;
            if (dJap.fields().month().number() != 9) return 0;
            if (dJap.fields().day() != 11) return 0;

            // Convert back to Gregorian
            dGregBack : k::time::CalendarDate = dJap.toChronology(greg);
            if (dGregBack != dGreg) return 0;

            return 1;
        }
    )SRC");

    REQUIRE(jit != nullptr);
    auto check = [&](const char* sym) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == 1);
    };

    check("test_era_and_fields");
    check("test_icu_chronologies");
}

