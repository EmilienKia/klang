/*
 * K Language standard library — ZoneId, TZif Reader, TZDB Provider & Leap-Second Table tests (Package D6)
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

#include <dlfcn.h>
#include <string>
#include <fstream>
#include <vector>

#ifndef LIBK_KDI_DIR
#error "LIBK_KDI_DIR not defined"
#endif
#ifndef LIBK_LIB_DIR
#error "LIBK_LIB_DIR not defined"
#endif
#ifndef LIBK_TIME_FIXTURES_DIR
#define LIBK_TIME_FIXTURES_DIR "libk/libk/tests/time/fixtures"
#endif

namespace {

std::unique_ptr<k::model::gen::jit> jit_k(std::string_view src) {
    return gen_jit_with_stdlib(src, LIBK_KDI_DIR, LIBK_LIB_DIR);
}

} // anonymous namespace

// ═════════════════════════════════════════════════════════════════════════════
// 1. ZoneId: validation and equality
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("ZoneId: valid identifiers and normalization", "[libk][time][zone_id]") {
    auto jit = jit_k(R"SRC(
        module __test_zone_id_valid__;

        test_europe_paris() : int throws(k::time::InvalidTemporalValueException) {
            z : k::time::ZoneId = k::time::ZoneId::of(k::String("Europe/Paris"));
            if (z.name() == k::String("Europe/Paris")) return 1;
            return 0;
        }

        test_america_new_york() : int throws(k::time::InvalidTemporalValueException) {
            z : k::time::ZoneId = k::time::ZoneId::of(k::String("America/New_York"));
            if (z.name() == k::String("America/New_York")) return 1;
            return 0;
        }

        test_utc() : int throws(k::time::InvalidTemporalValueException) {
            z : k::time::ZoneId = k::time::ZoneId::of(k::String("UTC"));
            if (z.name() == k::String("UTC")) return 1;
            return 0;
        }

        test_etc_gmt_plus() : int throws(k::time::InvalidTemporalValueException) {
            z : k::time::ZoneId = k::time::ZoneId::of(k::String("Etc/GMT+1"));
            if (z.name() == k::String("Etc/GMT+1")) return 1;
            return 0;
        }

        test_asia_tokyo() : int throws(k::time::InvalidTemporalValueException) {
            z : k::time::ZoneId = k::time::ZoneId::of(k::String("Asia/Tokyo"));
            if (z.name() == k::String("Asia/Tokyo")) return 1;
            return 0;
        }

        test_gmt_minus() : int throws(k::time::InvalidTemporalValueException) {
            z : k::time::ZoneId = k::time::ZoneId::of(k::String("GMT-5"));
            if (z.name() == k::String("GMT-5")) return 1;
            return 0;
        }

        test_single_char() : int throws(k::time::InvalidTemporalValueException) {
            z : k::time::ZoneId = k::time::ZoneId::of(k::String("A"));
            if (z.name() == k::String("A")) return 1;
            return 0;
        }

        test_equality_same() : int throws(k::time::InvalidTemporalValueException) {
            z1 : k::time::ZoneId = k::time::ZoneId::of(k::String("Europe/Paris"));
            z2 : k::time::ZoneId = k::time::ZoneId::of(k::String("Europe/Paris"));
            if (z1 == z2 && !(z1 != z2)) return 1;
            return 0;
        }

        test_equality_different() : int throws(k::time::InvalidTemporalValueException) {
            z1 : k::time::ZoneId = k::time::ZoneId::of(k::String("Europe/Paris"));
            z2 : k::time::ZoneId = k::time::ZoneId::of(k::String("America/New_York"));
            if (z1 != z2 && !(z1 == z2)) return 1;
            return 0;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto check = [&](const char* sym) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == 1);
    };

    check("test_europe_paris");
    check("test_america_new_york");
    check("test_utc");
    check("test_etc_gmt_plus");
    check("test_asia_tokyo");
    check("test_gmt_minus");
    check("test_single_char");
    check("test_equality_same");
    check("test_equality_different");
}

TEST_CASE("ZoneId: invalid identifiers rejected with InvalidTemporalValueException(501)", "[libk][time][zone_id]") {
    auto jit = jit_k(R"SRC(
        module __test_zone_id_invalid__;

        test_empty() : int {
            try {
                k::time::ZoneId::of(k::String(""));
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
                return 1;
            }
        }

        test_leading_slash() : int {
            try {
                k::time::ZoneId::of(k::String("/Europe/Paris"));
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
                return 1;
            }
        }

        test_trailing_slash() : int {
            try {
                k::time::ZoneId::of(k::String("Europe/Paris/"));
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
                return 1;
            }
        }

        test_consecutive_slashes() : int {
            try {
                k::time::ZoneId::of(k::String("Europe//Paris"));
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
                return 1;
            }
        }

        test_path_traversal_dot_dot() : int {
            try {
                k::time::ZoneId::of(k::String("../etc/passwd"));
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
                return 1;
            }
        }

        test_path_traversal_middle() : int {
            try {
                k::time::ZoneId::of(k::String("Europe/../Paris"));
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
                return 1;
            }
        }

        test_invalid_characters_at() : int {
            try {
                k::time::ZoneId::of(k::String("Europe/Paris@Home"));
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
                return 1;
            }
        }

        test_invalid_characters_space() : int {
            try {
                k::time::ZoneId::of(k::String("Europe/ Paris"));
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
                return 1;
            }
        }

        test_invalid_characters_hash() : int {
            try {
                k::time::ZoneId::of(k::String("Zone#1"));
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

    check("test_empty");
    check("test_leading_slash");
    check("test_trailing_slash");
    check("test_consecutive_slashes");
    check("test_path_traversal_dot_dot");
    check("test_path_traversal_middle");
    check("test_invalid_characters_at");
    check("test_invalid_characters_space");
    check("test_invalid_characters_hash");
}

// ═════════════════════════════════════════════════════════════════════════════
// 2. PosixLeapSecondPolicy tests
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("PosixLeapSecondPolicy: singletons identity and equality", "[libk][time][leap_seconds][policy]") {
    auto jit = jit_k(R"SRC(
        module __test_posix_leap_policy__;

        test_reject_identity() : int {
            p1 : k::time::PosixLeapSecondPolicy& = k::time::PosixLeapSecondPolicy::reject();
            p2 : k::time::PosixLeapSecondPolicy& = k::time::PosixLeapSecondPolicy::reject();
            if (p1 == p2 && !(p1 != p2)) return 1;
            return 0;
        }

        test_fold_prev_identity() : int {
            p1 : k::time::PosixLeapSecondPolicy& = k::time::PosixLeapSecondPolicy::foldToPreviousSecond();
            p2 : k::time::PosixLeapSecondPolicy& = k::time::PosixLeapSecondPolicy::foldToPreviousSecond();
            if (p1 == p2 && !(p1 != p2)) return 1;
            return 0;
        }

        test_fold_next_identity() : int {
            p1 : k::time::PosixLeapSecondPolicy& = k::time::PosixLeapSecondPolicy::foldToFollowingSecond();
            p2 : k::time::PosixLeapSecondPolicy& = k::time::PosixLeapSecondPolicy::foldToFollowingSecond();
            if (p1 == p2 && !(p1 != p2)) return 1;
            return 0;
        }

        test_distinct_policies() : int {
            r : k::time::PosixLeapSecondPolicy& = k::time::PosixLeapSecondPolicy::reject();
            prev : k::time::PosixLeapSecondPolicy& = k::time::PosixLeapSecondPolicy::foldToPreviousSecond();
            next : k::time::PosixLeapSecondPolicy& = k::time::PosixLeapSecondPolicy::foldToFollowingSecond();

            if (r != prev && r != next && prev != next) return 1;
            return 0;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto check = [&](const char* sym) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == 1);
    };

    check("test_reject_identity");
    check("test_fold_prev_identity");
    check("test_fold_next_identity");
    check("test_distinct_policies");
}

// ═════════════════════════════════════════════════════════════════════════════
// 3. LeapSecondTable tests
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("LeapSecondTable: queries and validations", "[libk][time][leap_seconds][table]") {
    auto jit = jit_k(R"SRC(
        module __test_leap_second_table__;

        test_count() : int {
            c : int = k::time::LeapSecondTable::count();
            if (c >= 27) return 1;
            return 0;
        }

        test_is_leap_date_2016() : int {
            if (k::time::LeapSecondTable::isLeapDate(2016L, 12, 31)) return 1;
            return 0;
        }

        test_is_leap_date_1972() : int {
            if (k::time::LeapSecondTable::isLeapDate(1972L, 6, 30) &&
                k::time::LeapSecondTable::isLeapDate(1972L, 12, 31)) return 1;
            return 0;
        }

        test_is_not_leap_date() : int {
            if (!k::time::LeapSecondTable::isLeapDate(2016L, 12, 30) &&
                !k::time::LeapSecondTable::isLeapDate(2017L, 6, 30) &&
                !k::time::LeapSecondTable::isLeapDate(2020L, 1, 1)) return 1;
            return 0;
        }

        test_tai_offset() : int {
            // From 2017-01-01 onwards, TAI - UTC is 37 seconds
            off : long = k::time::LeapSecondTable::taiOffset(1483228800L);
            if (off == 37L) return 1;
            return 0;
        }

        test_validate_valid_2016() : int {
            try {
                k::time::LeapSecondTable::validateLeapSecond(2016L, 12, 31, 23, 59, 60);
                return 1;
            } catch (e: k::time::LeapSecondException&) {
                return 0;
            } catch (e2: k::time::InvalidTemporalValueException&) {
                return 0;
            }
        }

        test_validate_invalid_date() : int {
            try {
                k::time::LeapSecondTable::validateLeapSecond(2016L, 12, 30, 23, 59, 60);
                return 0;
            } catch (e: k::time::LeapSecondException&) {
                return 1;
            } catch (e2: k::time::InvalidTemporalValueException&) {
                return 0;
            }
        }

        test_validate_invalid_time_hour() : int {
            try {
                k::time::LeapSecondTable::validateLeapSecond(2016L, 12, 31, 12, 59, 60);
                return 0;
            } catch (e: k::time::LeapSecondException&) {
                return 1;
            } catch (e2: k::time::InvalidTemporalValueException&) {
                return 0;
            }
        }

        test_validate_invalid_time_minute() : int {
            try {
                k::time::LeapSecondTable::validateLeapSecond(2016L, 12, 31, 23, 58, 60);
                return 0;
            } catch (e: k::time::LeapSecondException&) {
                return 1;
            } catch (e2: k::time::InvalidTemporalValueException&) {
                return 0;
            }
        }

        test_validate_normal_second() : int {
            try {
                k::time::LeapSecondTable::validateLeapSecond(2020L, 5, 10, 15, 30, 45);
                return 1;
            } catch (e: k::time::LeapSecondException&) {
                return 0;
            } catch (e2: k::time::InvalidTemporalValueException&) {
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

    check("test_count");
    check("test_is_leap_date_2016");
    check("test_is_leap_date_1972");
    check("test_is_not_leap_date");
    check("test_tai_offset");
    check("test_validate_valid_2016");
    check("test_validate_invalid_date");
    check("test_validate_invalid_time_hour");
    check("test_validate_invalid_time_minute");
    check("test_validate_normal_second");
}

// ═════════════════════════════════════════════════════════════════════════════
// 4. TZif Parser tests on synthetic fixtures
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("TZif parser: synthetic fixtures and error handling", "[libk][time][tzif]") {
    std::string libpath = std::string(LIBK_LIB_DIR) + "/libk.so";
    void* handle = dlopen(libpath.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!handle) handle = RTLD_DEFAULT;

    auto fn_tzif_parse = reinterpret_cast<void*(*)(const uint8_t*, size_t)>(dlsym(handle, "k_tzif_parse"));
    auto fn_snap_release = reinterpret_cast<void(*)(void*)>(dlsym(handle, "k_zone_rules_snapshot_release"));
    auto fn_offset_at = reinterpret_cast<int32_t(*)(const void*, int64_t)>(dlsym(handle, "k_zone_rules_snapshot_offset_at"));
    auto fn_abbrev_at = reinterpret_cast<const char*(*)(const void*, int64_t)>(dlsym(handle, "k_zone_rules_snapshot_abbrev_at"));
    auto fn_is_dst_at = reinterpret_cast<int32_t(*)(const void*, int64_t)>(dlsym(handle, "k_zone_rules_snapshot_is_dst_at"));
    auto fn_trans_count = reinterpret_cast<int32_t(*)(const void*)>(dlsym(handle, "k_zone_rules_snapshot_transition_count"));

    REQUIRE(fn_tzif_parse != nullptr);
    REQUIRE(fn_snap_release != nullptr);
    REQUIRE(fn_offset_at != nullptr);

    SECTION("fixed_plus1.tzif: parses UTC+1 fixed offset with 0 transitions") {
        std::string path = std::string(LIBK_TIME_FIXTURES_DIR) + "/fixed_plus1.tzif";
        std::ifstream file(path, std::ios::binary);
        REQUIRE(file.is_open());
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        void* snap = fn_tzif_parse(data.data(), data.size());
        REQUIRE(snap != nullptr);

        CHECK(fn_trans_count(snap) == 0);
        CHECK(fn_offset_at(snap, 0L) == 3600);
        CHECK(fn_offset_at(snap, 1600000000L) == 3600);
        CHECK(std::string(fn_abbrev_at(snap, 0L)) == "CET");
        CHECK(fn_is_dst_at(snap, 0L) == 0);

        fn_snap_release(snap);
    }

    SECTION("simple_dst.tzif: parses transitions between standard time and DST") {
        std::string path = std::string(LIBK_TIME_FIXTURES_DIR) + "/simple_dst.tzif";
        std::ifstream file(path, std::ios::binary);
        REQUIRE(file.is_open());
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        void* snap = fn_tzif_parse(data.data(), data.size());
        REQUIRE(snap != nullptr);

        CHECK(fn_trans_count(snap) == 2);

        // Before first transition (< 1600000000): Standard time UTC+1 (3600), "CET", is_dst=0
        CHECK(fn_offset_at(snap, 1500000000L) == 3600);
        CHECK(std::string(fn_abbrev_at(snap, 1500000000L)) == "CET");
        CHECK(fn_is_dst_at(snap, 1500000000L) == 0);

        // During DST (1600000000 <= t < 1605000000): DST UTC+2 (7200), "CEST", is_dst=1
        CHECK(fn_offset_at(snap, 1600000000L) == 7200);
        CHECK(fn_offset_at(snap, 1602000000L) == 7200);
        CHECK(std::string(fn_abbrev_at(snap, 1602000000L)) == "CEST");
        CHECK(fn_is_dst_at(snap, 1602000000L) == 1);

        // After second transition (>= 1605000000): Standard time UTC+1 (3600), "CET", is_dst=0
        CHECK(fn_offset_at(snap, 1605000000L) == 3600);
        CHECK(fn_offset_at(snap, 1700000000L) == 3600);
        CHECK(std::string(fn_abbrev_at(snap, 1700000000L)) == "CET");
        CHECK(fn_is_dst_at(snap, 1700000000L) == 0);

        fn_snap_release(snap);
    }

    SECTION("truncated.tzif: returns NULL gracefully without memory fault") {
        std::string path = std::string(LIBK_TIME_FIXTURES_DIR) + "/truncated.tzif";
        std::ifstream file(path, std::ios::binary);
        REQUIRE(file.is_open());
        std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

        void* snap = fn_tzif_parse(data.data(), data.size());
        CHECK(snap == nullptr);
    }

    SECTION("corrupted buffer inputs: returns NULL gracefully") {
        uint8_t garbage[] = { 'T', 'Z', 'i', 'f', 99, 1, 2, 3 };
        CHECK(fn_tzif_parse(garbage, sizeof(garbage)) == nullptr);

        uint8_t not_magic[] = { 'N', 'O', 'P', 'E', 0, 0, 0, 0 };
        CHECK(fn_tzif_parse(not_magic, sizeof(not_magic)) == nullptr);

        CHECK(fn_tzif_parse(nullptr, 0) == nullptr);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// 5. System TZDB Provider tests
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Tzdb: version, system zones and custom fixture root", "[libk][time][tzdb]") {
    auto jit = jit_k(R"SRC(
        module __test_tzdb_system__;

        test_version_not_empty() : int {
            v : k::String = k::time::Tzdb::version();
            if (!v.empty()) return 1;
            return 0;
        }

        test_system_utc_available() : int throws(k::time::InvalidTemporalValueException) {
            utc : k::time::ZoneId = k::time::ZoneId::of(k::String("UTC"));
            if (k::time::Tzdb::isZoneAvailable(utc)) return 1;
            return 0;
        }

        test_system_europe_paris_available() : int throws(k::time::InvalidTemporalValueException) {
            paris : k::time::ZoneId = k::time::ZoneId::of(k::String("Europe/Paris"));
            if (k::time::Tzdb::isZoneAvailable(paris)) return 1;
            return 0;
        }

        test_nonexistent_zone() : int throws(k::time::InvalidTemporalValueException) {
            badZone : k::time::ZoneId = k::time::ZoneId::of(k::String("NonExistent/Zone_9999"));
            if (!k::time::Tzdb::isZoneAvailable(badZone)) return 1;
            return 0;
        }

        test_load_nonexistent_throws() : int throws(k::time::InvalidTemporalValueException) {
            badZone : k::time::ZoneId = k::time::ZoneId::of(k::String("NonExistent/Zone_9999"));
            try {
                snap : k::time::KZoneRulesSnapshot* = k::time::Tzdb::loadZone(badZone);
                k::time::Tzdb::releaseSnapshot(snap);
                return 0;
            } catch (e: k::time::ZoneRulesUnavailableException&) {
                return 1;
            }
        }

        test_load_utc_snapshot() : int throws(k::time::InvalidTemporalValueException, k::time::ZoneRulesUnavailableException) {
            utc : k::time::ZoneId = k::time::ZoneId::of(k::String("UTC"));
            snap : k::time::KZoneRulesSnapshot* = k::time::Tzdb::loadZone(utc);
            if (snap == null) return 0;
            off : int = k::time::Tzdb::offsetAt(snap, 0L);
            k::time::Tzdb::releaseSnapshot(snap);
            if (off == 0) return 1;
            return 0;
        }

        test_custom_fixture_root() : int throws(k::time::InvalidTemporalValueException, k::time::ZoneRulesUnavailableException) {
            v : k::String = k::time::Tzdb::version();
            if (v != k::String("2026_test_version")) return 0;

            zid : k::time::ZoneId = k::time::ZoneId::of(k::String("Fixed_Plus1"));
            if (!k::time::Tzdb::isZoneAvailable(zid)) return 0;

            snap : k::time::KZoneRulesSnapshot* = k::time::Tzdb::loadZone(zid);
            if (snap == null) return 0;
            off : int = k::time::Tzdb::offsetAt(snap, 1000L);
            abbrev : k::String = k::time::Tzdb::abbrevAt(snap, 1000L);
            k::time::Tzdb::releaseSnapshot(snap);

            if (off == 3600 && abbrev == k::String("CET")) return 1;
            return 0;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto check = [&](const char* sym) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == 1);
    };

    check("test_version_not_empty");
    check("test_system_utc_available");
    check("test_system_europe_paris_available");
    check("test_nonexistent_zone");
    check("test_load_nonexistent_throws");
    check("test_load_utc_snapshot");

    std::string libpath = std::string(LIBK_LIB_DIR) + "/libk.so";
    void* handle = dlopen(libpath.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!handle) handle = RTLD_DEFAULT;
    auto fn_set_root = reinterpret_cast<void(*)(const char*)>(dlsym(handle, "k_tzdb_set_root"));
    auto fn_load_zone = reinterpret_cast<void*(*)(const char*)>(dlsym(handle, "k_tzdb_load_zone"));
    REQUIRE(fn_set_root != nullptr);

    fn_set_root(LIBK_TIME_FIXTURES_DIR);
    check("test_custom_fixture_root");

    // Path traversal rejection via C API
    if (fn_load_zone) {
        CHECK(fn_load_zone("../etc/passwd") == nullptr);
        CHECK(fn_load_zone("/etc/passwd") == nullptr);
        CHECK(fn_load_zone("Fixed_Plus1/../../etc/passwd") == nullptr);
    }

    // Reset back to standard system root
    fn_set_root("/usr/share/zoneinfo");
}
