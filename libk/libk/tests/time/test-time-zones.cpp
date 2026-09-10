/*
 * K Language standard library — TimeZone, ZoneRules, ZonedDateTime & Resolution tests (Package D7)
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
// 1. Fixed TimeZone tests (UTC, UTC+2)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("TimeZone: fixed offset time zones (UTC, UTC+2)", "[libk][time][timezone][fixed]") {
    auto jit = jit_k(R"SRC(
        module __test_fixed_timezone__;

        test_utc_fixed() : int throws(k::time::TemporalArithmeticException) {
            tz : k::time::TimeZone = k::time::TimeZone::fixed(k::time::ZoneOffset::utc());
            if (tz.id().name() != k::String("UTC")) return 0;
            if (tz.version() != k::String("fixed")) return 0;

            epoch : k::time::Instant = k::time::Instant::epoch();
            off : k::time::ZoneOffset = tz.rules().offsetAt(epoch);
            if (off.totalSeconds() != 0) return 0;

            offs : k::time::ZoneOffset[]! = tz.rules().validOffsets(k::time::LocalDateTime());
            if (offs.size != 1u || offs[0u].totalSeconds() != 0) return 0;

            res : k::time::ZoneLocalResolution! = tz.rules().resolveLocal(k::time::LocalDateTime());
            if (!res.isUnique()) return 0;
            if (res.offset().totalSeconds() != 0) return 0;

            trans : k::Optional<k::time::ZoneTransition> = tz.rules().transition(k::time::LocalDateTime());
            if (trans.hasValue()) return 0;

            return 1;
        }

        test_plus2_fixed() : int throws(k::time::TemporalArithmeticException) {
            tz : k::time::TimeZone = k::time::TimeZone::fixed(k::time::ZoneOffset(7200));
            if (tz.id().name() != k::String("UTC+2")) return 0;
            if (tz.version() != k::String("fixed")) return 0;

            epoch : k::time::Instant = k::time::Instant::epoch();
            off : k::time::ZoneOffset = tz.rules().offsetAt(epoch);
            if (off.totalSeconds() != 7200) return 0;

            res : k::time::ZoneLocalResolution! = tz.rules().resolveLocal(k::time::LocalDateTime());
            if (!res.isUnique()) return 0;
            if (res.offset().totalSeconds() != 7200) return 0;

            return 1;
        }

        test_fixed_equality() : int {
            tz1 : k::time::TimeZone = k::time::TimeZone::fixed(k::time::ZoneOffset::utc());
            tz2 : k::time::TimeZone = k::time::TimeZone::fixed(k::time::ZoneOffset::utc());
            tz3 : k::time::TimeZone = k::time::TimeZone::fixed(k::time::ZoneOffset(7200));

            if (!(tz1 == tz2)) return 0;
            if (tz1 != tz2) return 0;
            if (tz1 == tz3) return 0;
            if (!(tz1 != tz3)) return 0;

            return 1;
        }

        test_zdt_from_instant_fixed() : int throws(k::time::TemporalArithmeticException) {
            tzUtc : k::time::TimeZone = k::time::TimeZone::fixed(k::time::ZoneOffset::utc());
            tzPlus2 : k::time::TimeZone = k::time::TimeZone::fixed(k::time::ZoneOffset(7200));

            epoch : k::time::Instant = k::time::Instant::epoch();
            zdtUtc : k::time::ZonedDateTime = k::time::ZonedDateTime::fromInstant(epoch, tzUtc);
            zdtPlus2 : k::time::ZonedDateTime = k::time::ZonedDateTime::fromInstant(epoch, tzPlus2);

            if (zdtUtc.localDate().year() != 1970L || zdtUtc.localDate().month() != 1 || zdtUtc.localDate().day() != 1) return 0;
            if (zdtUtc.localTime().hour() != 0 || zdtUtc.localTime().minute() != 0) return 0;
            if (zdtUtc.offset().totalSeconds() != 0) return 0;

            if (zdtPlus2.localDate().year() != 1970L || zdtPlus2.localDate().month() != 1 || zdtPlus2.localDate().day() != 1) return 0;
            if (zdtPlus2.localTime().hour() != 2 || zdtPlus2.localTime().minute() != 0) return 0;
            if (zdtPlus2.offset().totalSeconds() != 7200) return 0;

            if (!zdtUtc.isSameInstant(zdtPlus2)) return 0;
            // Equality compares instant AND zone
            if (zdtUtc == zdtPlus2) return 0;

            return 1;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto check = [&](const char* sym) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == 1);
    };

    check("test_utc_fixed");
    check("test_plus2_fixed");
    check("test_fixed_equality");
    check("test_zdt_from_instant_fixed");
}

// ═════════════════════════════════════════════════════════════════════════════
// 2. System TimeZone identification and test override
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("TimeZone: system timezone discovery and test seams", "[libk][time][timezone][system]") {
    auto jit = jit_k(R"SRC(
        module __test_system_timezone__;

        test_override_paris() : int throws(k::time::SystemTimeZoneUnavailableException, k::time::ZoneRulesUnavailableException) {
            k::time::Tzdb::setSystemZoneOverride(k::String("Europe/Paris"));
            sysTz : k::time::TimeZone = k::time::TimeZone::system();
            k::time::Tzdb::clearSystemZoneOverride();
            if (sysTz.id().name() == k::String("Europe/Paris")) return 1;
            return 0;
        }

        test_override_unavailable_throws() : int {
            k::time::Tzdb::setSystemZoneOverride(k::String("__UNAVAILABLE__"));
            try {
                k::time::TimeZone::system();
                k::time::Tzdb::clearSystemZoneOverride();
                return 0;
            } catch (e: k::time::SystemTimeZoneUnavailableException&) {
                k::time::Tzdb::clearSystemZoneOverride();
                if (e.getCode() == 512) return 1;
                return 0;
            } catch (e2: k::time::TemporalException&) {
                k::time::Tzdb::clearSystemZoneOverride();
                return 0;
            }
        }

        test_override_invalid_syntax_throws() : int {
            k::time::Tzdb::setSystemZoneOverride(k::String("Europe//Paris"));
            try {
                k::time::TimeZone::system();
                k::time::Tzdb::clearSystemZoneOverride();
                return 0;
            } catch (e: k::time::SystemTimeZoneUnavailableException&) {
                k::time::Tzdb::clearSystemZoneOverride();
                if (e.getCode() == 512) return 1;
                return 0;
            } catch (e2: k::time::TemporalException&) {
                k::time::Tzdb::clearSystemZoneOverride();
                return 0;
            }
        }

        test_override_nonexistent_zone_throws() : int {
            k::time::Tzdb::setSystemZoneOverride(k::String("NonExistent/Zone_9999"));
            try {
                k::time::TimeZone::system();
                k::time::Tzdb::clearSystemZoneOverride();
                return 0;
            } catch (e: k::time::ZoneRulesUnavailableException&) {
                k::time::Tzdb::clearSystemZoneOverride();
                if (e.getCode() == 511) return 1;
                return 0;
            } catch (e2: k::time::TemporalException&) {
                k::time::Tzdb::clearSystemZoneOverride();
                return 0;
            }
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto check = [&](const char* sym) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == 1);
    };

    check("test_override_paris");
    check("test_override_unavailable_throws");
    check("test_override_invalid_syntax_throws");
    check("test_override_nonexistent_zone_throws");
}

// ═════════════════════════════════════════════════════════════════════════════
// 3. Loaded zone rules: Europe/Paris DST spring gap and autumn overlap
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("ZoneRules: Europe/Paris spring gap and autumn overlap", "[libk][time][rules][paris]") {
    auto jit = jit_k(R"SRC(
        module __test_paris_rules__;

        test_spring_gap() : int throws(k::time::InvalidTemporalValueException, k::time::ZoneRulesUnavailableException) {
            zid : k::time::ZoneId = k::time::ZoneId::of(k::String("Europe/Paris"));
            tz : k::time::TimeZone = k::time::TimeZone::of(zid);

            // 2026-03-29 02:30:00 is in the spring-forward gap (02:00 -> 03:00)
            localGap : k::time::LocalDateTime = k::time::LocalDateTime::of(2026L, 3, 29, 2, 30, 0, 0);

            offs : k::time::ZoneOffset[]! = tz.rules().validOffsets(localGap);
            if (offs.size != 0u) return 0;

            res : k::time::ZoneLocalResolution! = tz.rules().resolveLocal(localGap);
            if (!res.isGap()) return 0;
            if (res.isUnique() || res.isOverlap()) return 0;
            if (res.duration().secondsPart() != 3600L) return 0;

            trans : k::time::ZoneTransition = res.transition();
            if (!trans.isGap()) return 0;
            if (trans.beforeOffset().totalSeconds() != 3600) return 0; // UTC+1
            if (trans.afterOffset().totalSeconds() != 7200) return 0;  // UTC+2

            optTrans : k::Optional<k::time::ZoneTransition> = tz.rules().transition(localGap);
            if (!optTrans.hasValue()) return 0;
            if (!optTrans.get().isGap()) return 0;

            return 1;
        }

        test_autumn_overlap() : int throws(k::time::InvalidTemporalValueException, k::time::ZoneRulesUnavailableException) {
            zid : k::time::ZoneId = k::time::ZoneId::of(k::String("Europe/Paris"));
            tz : k::time::TimeZone = k::time::TimeZone::of(zid);

            // 2026-10-25 02:30:00 is in the autumn-fallback overlap (03:00 -> 02:00)
            localOverlap : k::time::LocalDateTime = k::time::LocalDateTime::of(2026L, 10, 25, 2, 30, 0, 0);

            offs : k::time::ZoneOffset[]! = tz.rules().validOffsets(localOverlap);
            if (offs.size != 2u) return 0;
            if (offs[0u].totalSeconds() != 7200) return 0; // earlier offset: UTC+2
            if (offs[1u].totalSeconds() != 3600) return 0; // later offset: UTC+1

            res : k::time::ZoneLocalResolution! = tz.rules().resolveLocal(localOverlap);
            if (!res.isOverlap()) return 0;
            if (res.isUnique() || res.isGap()) return 0;

            if (res.earlierOffset().totalSeconds() != 7200) return 0;
            if (res.laterOffset().totalSeconds() != 3600) return 0;
            if (res.earlierInstant() >= res.laterInstant()) return 0;

            trans : k::time::ZoneTransition = res.transition();
            if (!trans.isOverlap()) return 0;

            optTrans : k::Optional<k::time::ZoneTransition> = tz.rules().transition(localOverlap);
            if (!optTrans.hasValue()) return 0;
            if (!optTrans.get().isOverlap()) return 0;

            return 1;
        }

        test_normal_time_unique() : int throws(k::time::InvalidTemporalValueException, k::time::ZoneRulesUnavailableException) {
            zid : k::time::ZoneId = k::time::ZoneId::of(k::String("Europe/Paris"));
            tz : k::time::TimeZone = k::time::TimeZone::of(zid);

            // 2026-06-15 12:00:00 is summer time (UTC+2)
            summerLocal : k::time::LocalDateTime = k::time::LocalDateTime::of(2026L, 6, 15, 12, 0, 0, 0);
            res : k::time::ZoneLocalResolution! = tz.rules().resolveLocal(summerLocal);
            if (!res.isUnique()) return 0;
            if (res.offset().totalSeconds() != 7200) return 0;

            // 2026-01-15 12:00:00 is winter standard time (UTC+1)
            winterLocal : k::time::LocalDateTime = k::time::LocalDateTime::of(2026L, 1, 15, 12, 0, 0, 0);
            resWinter : k::time::ZoneLocalResolution! = tz.rules().resolveLocal(winterLocal);
            if (!resWinter.isUnique()) return 0;
            if (resWinter.offset().totalSeconds() != 3600) return 0;

            return 1;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto check = [&](const char* sym) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == 1);
    };

    check("test_spring_gap");
    check("test_autumn_overlap");
    check("test_normal_time_unique");
}

// ═════════════════════════════════════════════════════════════════════════════
// 4. All 6 resolvers tested on gap and overlap
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Resolvers: all 6 policies on gap and overlap", "[libk][time][resolver]") {
    auto jit = jit_k(R"SRC(
        module __test_resolvers__;

        test_gap_resolvers() : int throws(k::time::InvalidTemporalValueException, k::time::ZoneRulesUnavailableException, k::time::TemporalArithmeticException) {
            zid : k::time::ZoneId = k::time::ZoneId::of(k::String("Europe/Paris"));
            tz : k::time::TimeZone = k::time::TimeZone::of(zid);
            localGap : k::time::LocalDateTime = k::time::LocalDateTime::of(2026L, 3, 29, 2, 30, 0, 0);

            // 1. StrictResolver throws LocalTimeGapException(521)
            try {
                k::time::ZonedDateTime::of(localGap, tz, k::time::StrictResolver::instance());
                return 0;
            } catch (e: k::time::LocalTimeGapException&) {
                if (e.getCode() != 521) return 0;
            } catch (e2: k::time::TemporalException&) {
                return 0;
            }

            // 2. EarlierResolver throws LocalTimeGapException
            try {
                k::time::ZonedDateTime::of(localGap, tz, k::time::EarlierResolver::instance());
                return 0;
            } catch (e: k::time::LocalTimeGapException&) {
                // expected
            } catch (e2: k::time::TemporalException&) {
                return 0;
            }

            // 3. LaterResolver throws LocalTimeGapException
            try {
                k::time::ZonedDateTime::of(localGap, tz, k::time::LaterResolver::instance());
                return 0;
            } catch (e: k::time::LocalTimeGapException&) {
                // expected
            } catch (e2: k::time::TemporalException&) {
                return 0;
            }

            // 4. PreferOffsetResolver throws LocalTimeGapException
            prefer : k::time::PreferOffsetResolver = k::time::PreferOffsetResolver(k::time::ZoneOffset(7200));
            try {
                k::time::ZonedDateTime::of(localGap, tz, prefer);
                return 0;
            } catch (e: k::time::LocalTimeGapException&) {
                // expected
            } catch (e2: k::time::TemporalException&) {
                return 0;
            }

            // 5. ShiftForwardResolver shifts forward by gap duration (02:30 -> 03:30 in +02:00)
            zdtForward : k::time::ZonedDateTime = k::time::ZonedDateTime::of(localGap, tz, k::time::ShiftForwardResolver::instance());
            if (zdtForward.localTime().hour() != 3 || zdtForward.localTime().minute() != 30) return 0;
            if (zdtForward.offset().totalSeconds() != 7200) return 0;

            // 6. ShiftBackwardResolver shifts backward by gap duration (02:30 -> 01:30 in +01:00)
            zdtBackward : k::time::ZonedDateTime = k::time::ZonedDateTime::of(localGap, tz, k::time::ShiftBackwardResolver::instance());
            if (zdtBackward.localTime().hour() != 1 || zdtBackward.localTime().minute() != 30) return 0;
            if (zdtBackward.offset().totalSeconds() != 3600) return 0;

            return 1;
        }

        test_overlap_resolvers() : int throws(k::time::InvalidTemporalValueException, k::time::ZoneRulesUnavailableException, k::time::TemporalArithmeticException) {
            zid : k::time::ZoneId = k::time::ZoneId::of(k::String("Europe/Paris"));
            tz : k::time::TimeZone = k::time::TimeZone::of(zid);
            localOverlap : k::time::LocalDateTime = k::time::LocalDateTime::of(2026L, 10, 25, 2, 30, 0, 0);

            // 1. StrictResolver throws LocalTimeOverlapException(522)
            try {
                k::time::ZonedDateTime::of(localOverlap, tz, k::time::StrictResolver::instance());
                return 0;
            } catch (e: k::time::LocalTimeOverlapException&) {
                if (e.getCode() != 522) return 0;
            } catch (e2: k::time::TemporalException&) {
                return 0;
            }

            // 2. EarlierResolver picks earlier instant (UTC+2)
            zdtEarlier : k::time::ZonedDateTime = k::time::ZonedDateTime::of(localOverlap, tz, k::time::EarlierResolver::instance());
            if (zdtEarlier.offset().totalSeconds() != 7200) return 0;
            if (zdtEarlier.localTime().hour() != 2 || zdtEarlier.localTime().minute() != 30) return 0;

            // 3. LaterResolver picks later instant (UTC+1)
            zdtLater : k::time::ZonedDateTime = k::time::ZonedDateTime::of(localOverlap, tz, k::time::LaterResolver::instance());
            if (zdtLater.offset().totalSeconds() != 3600) return 0;
            if (zdtLater.localTime().hour() != 2 || zdtLater.localTime().minute() != 30) return 0;
            if (zdtEarlier.toInstant() >= zdtLater.toInstant()) return 0;

            // 4. PreferOffsetResolver selects matching offset
            preferSummer : k::time::PreferOffsetResolver = k::time::PreferOffsetResolver(k::time::ZoneOffset(7200));
            zdtSummer : k::time::ZonedDateTime = k::time::ZonedDateTime::of(localOverlap, tz, preferSummer);
            if (zdtSummer.offset().totalSeconds() != 7200) return 0;

            preferWinter : k::time::PreferOffsetResolver = k::time::PreferOffsetResolver(k::time::ZoneOffset(3600));
            zdtWinter : k::time::ZonedDateTime = k::time::ZonedDateTime::of(localOverlap, tz, preferWinter);
            if (zdtWinter.offset().totalSeconds() != 3600) return 0;

            // 5. ShiftForwardResolver on overlap picks earlier instant
            zdtShiftFwd : k::time::ZonedDateTime = k::time::ZonedDateTime::of(localOverlap, tz, k::time::ShiftForwardResolver::instance());
            if (zdtShiftFwd.offset().totalSeconds() != 7200) return 0;

            // 6. ShiftBackwardResolver on overlap throws LocalTimeOverlapException(522)
            try {
                k::time::ZonedDateTime::of(localOverlap, tz, k::time::ShiftBackwardResolver::instance());
                return 0;
            } catch (e: k::time::LocalTimeOverlapException&) {
                if (e.getCode() != 522) return 0;
            } catch (e2: k::time::TemporalException&) {
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

    check("test_gap_resolvers");
    check("test_overlap_resolvers");
}

// ═════════════════════════════════════════════════════════════════════════════
// 5. ZonedDateTime: Duration vs Period arithmetic across DST
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("ZonedDateTime: duration arithmetic vs period arithmetic across DST", "[libk][time][zdt][arithmetic]") {
    auto jit = jit_k(R"SRC(
        module __test_zdt_dst_arithmetic__;

        test_dst_arithmetic() : int throws(k::time::InvalidTemporalValueException, k::time::ZoneRulesUnavailableException, k::time::LocalTimeGapException, k::time::LocalTimeOverlapException, k::time::TemporalArithmeticException) {
            zid : k::time::ZoneId = k::time::ZoneId::of(k::String("Europe/Paris"));
            tz : k::time::TimeZone = k::time::TimeZone::of(zid);

            // Saturday before spring forward: 2026-03-28 10:00:00 (offset UTC+1)
            sat : k::time::LocalDateTime = k::time::LocalDateTime::of(2026L, 3, 28, 10, 0, 0, 0);
            zdtSat : k::time::ZonedDateTime = k::time::ZonedDateTime::of(sat, tz);

            // Absolute duration arithmetic: add 24 chronological hours (86,400s)
            // Across spring forward gap (1h skipped), clock moves to 11:00:00!
            zdtDur : k::time::ZonedDateTime = zdtSat.plus(k::time::Duration::ofHours(24L));
            if (zdtDur.localDate().day() != 29) return 0;
            if (zdtDur.localTime().hour() != 11) return 0;
            if (zdtDur.offset().totalSeconds() != 7200) return 0;

            // Civil calendar period arithmetic: add 1 day
            // Clock stays at 10:00:00!
            zdtPer : k::time::ZonedDateTime = zdtSat.plus(k::time::Period::days(1L));
            if (zdtPer.localDate().day() != 29) return 0;
            if (zdtPer.localTime().hour() != 10) return 0;
            if (zdtPer.offset().totalSeconds() != 7200) return 0;

            // The duration between Saturday 10:00 and Sunday 10:00 across spring forward is 23 hours!
            diffDur : k::time::Duration = zdtPer.toInstant() - zdtSat.toInstant();
            if (diffDur.secondsPart() != 23L * 3600L) return 0;

            return 1;
        }

        test_with_zone() : int throws(k::time::InvalidTemporalValueException, k::time::ZoneRulesUnavailableException, k::time::LocalTimeGapException, k::time::LocalTimeOverlapException, k::time::TemporalArithmeticException) {
            parisZid : k::time::ZoneId = k::time::ZoneId::of(k::String("Europe/Paris"));
            parisTz : k::time::TimeZone = k::time::TimeZone::of(parisZid);
            utcTz : k::time::TimeZone = k::time::TimeZone::fixed(k::time::ZoneOffset::utc());

            // 2026-06-15 12:00:00 in Paris (UTC+2) -> 10:00:00 UTC
            parisLocal : k::time::LocalDateTime = k::time::LocalDateTime::of(2026L, 6, 15, 12, 0, 0, 0);
            zdtParis : k::time::ZonedDateTime = k::time::ZonedDateTime::of(parisLocal, parisTz);

            zdtUtc : k::time::ZonedDateTime = zdtParis.withZone(utcTz);
            if (!zdtUtc.isSameInstant(zdtParis)) return 0;
            if (zdtUtc.offset().totalSeconds() != 0) return 0;
            if (zdtUtc.localTime().hour() != 10) return 0;
            if (zdtUtc.localDate().day() != 15) return 0;

            return 1;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto check = [&](const char* sym) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == 1);
    };

    check("test_dst_arithmetic");
    check("test_with_zone");
}

// ═════════════════════════════════════════════════════════════════════════════
// 6. ISO format and parse: Instant and ZonedDateTime
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Iso: format and parse for Instant and ZonedDateTime", "[libk][time][iso][zoned]") {
    auto jit = jit_k(R"SRC(
        module __test_iso_zoned__;

        test_format_instant() : int {
            epoch : k::time::Instant = k::time::Instant::epoch();
            str : k::String = k::time::Iso::formatInstant(epoch);
            if (str != k::String("1970-01-01T00:00:00Z")) return 0;

            inst : k::time::Instant = k::time::Instant::ofEpochSecond(1000L, 500000000);
            str2 : k::String = k::time::Iso::formatInstant(inst);
            if (str2 != k::String("1970-01-01T00:16:40.5Z")) return 0;

            return 1;
        }

        test_parse_instant() : int throws(k::time::TemporalParseException, k::time::InvalidTemporalValueException, k::time::LeapSecondException, k::time::TemporalArithmeticException) {
            inst : k::time::Instant = k::time::Iso::parseInstant(k::String("1970-01-01T00:00:00Z"));
            if (inst.epochSeconds() != 0L || inst.nanoAdjustment() != 0) return 0;

            inst2 : k::time::Instant = k::time::Iso::parseInstant(k::String("2026-06-15T12:00:00+02:00"));
            // 12:00 - 02:00 = 10:00 UTC
            utc : k::time::LocalDateTime = inst2.toUtcDateTime();
            if (utc.hour() != 10 || utc.minute() != 0) return 0;

            return 1;
        }

        test_format_zonedDateTime() : int throws(k::time::InvalidTemporalValueException, k::time::ZoneRulesUnavailableException, k::time::LocalTimeGapException, k::time::LocalTimeOverlapException, k::time::TemporalArithmeticException) {
            parisZid : k::time::ZoneId = k::time::ZoneId::of(k::String("Europe/Paris"));
            parisTz : k::time::TimeZone = k::time::TimeZone::of(parisZid);

            parisLocal : k::time::LocalDateTime = k::time::LocalDateTime::of(2026L, 6, 15, 12, 0, 0, 0);
            zdt : k::time::ZonedDateTime = k::time::ZonedDateTime::of(parisLocal, parisTz);

            formatted : k::String = k::time::Iso::formatZonedDateTime(zdt);
            if (formatted != k::String("2026-06-15T12:00:00+02:00[Europe/Paris]")) return 0;

            return 1;
        }

        test_parse_zonedDateTime() : int throws(k::time::TemporalParseException, k::time::InvalidTemporalValueException, k::time::LocalTimeGapException, k::time::LocalTimeOverlapException, k::time::ZoneRulesUnavailableException, k::time::TemporalArithmeticException) {
            str : k::String = k::String("2026-06-15T12:00:00+02:00[Europe/Paris]");
            zdt : k::time::ZonedDateTime = k::time::Iso::parseZonedDateTime(str);

            if (zdt.zone().id().name() != k::String("Europe/Paris")) return 0;
            if (zdt.localDate().year() != 2026L || zdt.localDate().month() != 6 || zdt.localDate().day() != 15) return 0;
            if (zdt.localTime().hour() != 12 || zdt.localTime().minute() != 0) return 0;
            if (zdt.offset().totalSeconds() != 7200) return 0;

            return 1;
        }

        test_parse_zonedDateTime_gap_throws() : int throws(k::time::TemporalParseException, k::time::InvalidTemporalValueException, k::time::ZoneRulesUnavailableException, k::time::TemporalArithmeticException) {
            str : k::String = k::String("2026-03-29T02:30:00+01:00[Europe/Paris]");
            try {
                k::time::Iso::parseZonedDateTime(str, k::time::StrictResolver::instance());
                return 0;
            } catch (e: k::time::LocalTimeGapException&) {
                if (e.getCode() == 521) return 1;
                return 0;
            }
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto check = [&](const char* sym) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == 1);
    };

    check("test_format_instant");
    check("test_parse_instant");
    check("test_format_zonedDateTime");
    check("test_parse_zonedDateTime");
    check("test_parse_zonedDateTime_gap_throws");
}
