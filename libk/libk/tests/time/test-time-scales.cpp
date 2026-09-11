/*
 * K Language standard library — Time Scales, Sources, Observations, and CPU Clocks tests (Package D9)
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

struct TestClockReading {
    int64_t seconds;
    int32_t nanos;
    int32_t status;
};

using ClockProviderFn = TestClockReading (*)(int clock_kind);

void set_platform_clock_seam(ClockProviderFn seam) {
    std::string libpath = std::string(LIBK_LIB_DIR) + "/libk.so";
    void* handle = dlopen(libpath.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        handle = RTLD_DEFAULT;
    }
    auto fn = reinterpret_cast<void(*)(ClockProviderFn)>(dlsym(handle, "k_platform_clock_set_provider_seam"));
    if (fn) {
        fn(seam);
    }
}

struct SeamGuard {
    SeamGuard(ClockProviderFn seam) {
        set_platform_clock_seam(seam);
    }
    ~SeamGuard() {
        set_platform_clock_seam(nullptr);
    }
};

} // anonymous namespace

// ═════════════════════════════════════════════════════════════════════════════
// 1. TimeScaleId tests
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("TimeScaleId: creation, validation, equality", "[libk][time][scale][id]") {
    auto jit = jit_k(R"SRC(
        module __test_time_scale_id__;

        test_standard_ids() : int throws(k::time::InvalidTemporalValueException) {
            utc : k::time::TimeScaleId = k::time::TimeScaleId::utc();
            tai : k::time::TimeScaleId = k::time::TimeScaleId::tai();
            gps : k::time::TimeScaleId = k::time::TimeScaleId::gps();
            posix : k::time::TimeScaleId = k::time::TimeScaleId::posix();

            if (utc.value() != k::String("UTC")) return 0;
            if (tai.value() != k::String("TAI")) return 0;
            if (gps.value() != k::String("GPS")) return 0;
            if (posix.value() != k::String("POSIX")) return 0;

            if (!(utc == k::time::TimeScaleId::of(k::String("UTC")))) return 0;
            if (utc == tai) return 0;
            if (!(utc != tai)) return 0;

            return 1;
        }

        test_empty_id_throws() : int {
            try {
                k::time::TimeScaleId::of(k::String(""));
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
                if (e.getCode() == 501) return 1;
                return 0;
            }
        }

        test_invalid_char_throws() : int {
            try {
                k::time::TimeScaleId::of(k::String("UTC@SCALE"));
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
                if (e.getCode() == 501) return 1;
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

    check("test_standard_ids");
    check("test_empty_id_throws");
    check("test_invalid_char_throws");
}

// ═════════════════════════════════════════════════════════════════════════════
// 2. TimeScaleValue tests
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("TimeScaleValue: construction, normalization, equality", "[libk][time][scale][value]") {
    auto jit = jit_k(R"SRC(
        module __test_time_scale_value__;

        test_basic_construction() : int throws(k::time::TemporalArithmeticException) {
            val : k::time::TimeScaleValue = k::time::TimeScaleValue::of(100L, 500);
            if (val.wholeSeconds() != 100L) return 0;
            if (val.nanoAdjustment() != 500) return 0;

            val2 : k::time::TimeScaleValue = k::time::TimeScaleValue::ofSeconds(42L);
            if (val2.wholeSeconds() != 42L) return 0;
            if (val2.nanoAdjustment() != 0) return 0;

            return 1;
        }

        test_normalization_positive() : int throws(k::time::TemporalArithmeticException) {
            // 1_500_000_000 nanos = 1 second + 500_000_000 nanos
            val : k::time::TimeScaleValue = k::time::TimeScaleValue(10L, 1500000000);
            if (val.wholeSeconds() != 11L) return 0;
            if (val.nanoAdjustment() != 500000000) return 0;
            return 1;
        }

        test_normalization_negative() : int throws(k::time::TemporalArithmeticException) {
            // -200_000_000 nanos = -1 second + 800_000_000 nanos
            val : k::time::TimeScaleValue = k::time::TimeScaleValue(10L, -200000000);
            if (val.wholeSeconds() != 9L) return 0;
            if (val.nanoAdjustment() != 800000000) return 0;
            return 1;
        }

        test_equality() : int throws(k::time::TemporalArithmeticException) {
            v1 : k::time::TimeScaleValue = k::time::TimeScaleValue(100L, 500);
            v2 : k::time::TimeScaleValue = k::time::TimeScaleValue(100L, 500);
            v3 : k::time::TimeScaleValue = k::time::TimeScaleValue(100L, 600);
            v4 : k::time::TimeScaleValue = k::time::TimeScaleValue(101L, 500);

            if (!(v1 == v2)) return 0;
            if (v1 != v2) return 0;
            if (v1 == v3) return 0;
            if (!(v1 != v3)) return 0;
            if (v1 == v4) return 0;
            if (!(v1 != v4)) return 0;

            return 1;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto check = [&](const char* sym) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == 1);
    };

    check("test_basic_construction");
    check("test_normalization_positive");
    check("test_normalization_negative");
    check("test_equality");
}

// ═════════════════════════════════════════════════════════════════════════════
// 3. TimeScales singletons and basic properties
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("TimeScales: singletons, ids, and epochs", "[libk][time][scale][scales]") {
    auto jit = jit_k(R"SRC(
        module __test_time_scales_props__;

        test_ids_and_epochs() : int {
            utc : k::time::TimeScale& = k::time::TimeScales::utc();
            tai : k::time::TimeScale& = k::time::TimeScales::tai();
            gps : k::time::TimeScale& = k::time::TimeScales::gps();

            if (utc.id().value() != k::String("UTC")) return 0;
            if (tai.id().value() != k::String("TAI")) return 0;
            if (gps.id().value() != k::String("GPS")) return 0;

            // UTC epoch is 1970-01-01T00:00:00Z
            if (utc.epoch().epochSeconds() != 0L) return 0;
            if (utc.epoch().nanoAdjustment() != 0) return 0;

            // GPS epoch is 1980-01-06T00:00:00 UTC (continuous seconds = 315964809L)
            if (gps.epoch().epochSeconds() != 315964809L) return 0;
            if (gps.epoch().nanoAdjustment() != 0) return 0;

            return 1;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto check = [&](const char* sym) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == 1);
    };

    check("test_ids_and_epochs");
}

// ═════════════════════════════════════════════════════════════════════════════
// 4. Value from Instant and Instant from Value round-trips
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("TimeScale: round trips for UTC, TAI, GPS", "[libk][time][scale][roundtrip]") {
    auto jit = jit_k(R"SRC(
        module __test_scale_roundtrip__;

        test_utc_roundtrip() : int throws(k::time::TimeScaleDataUnavailableException, k::time::InvalidTemporalValueException) {
            inst : k::time::Instant = k::time::Instant::ofEpochSecond(1700000000L, 123456789);
            utc : k::time::TimeScale& = k::time::TimeScales::utc();

            val : k::time::TimeScaleValue = utc.valueFromInstant(inst);
            rt : k::time::Instant = utc.instantFromValue(val);

            if (rt.epochSeconds() != inst.epochSeconds()) return 0;
            if (rt.nanoAdjustment() != inst.nanoAdjustment()) return 0;
            return 1;
        }

        test_tai_roundtrip() : int throws(k::time::TimeScaleDataUnavailableException, k::time::InvalidTemporalValueException) {
            inst : k::time::Instant = k::time::Instant::ofEpochSecond(1700000000L, 987654321);
            tai : k::time::TimeScale& = k::time::TimeScales::tai();

            val : k::time::TimeScaleValue = tai.valueFromInstant(inst);
            rt : k::time::Instant = tai.instantFromValue(val);

            if (rt.epochSeconds() != inst.epochSeconds()) return 0;
            if (rt.nanoAdjustment() != inst.nanoAdjustment()) return 0;
            return 1;
        }

        test_gps_roundtrip() : int throws(k::time::TimeScaleDataUnavailableException, k::time::InvalidTemporalValueException) {
            inst : k::time::Instant = k::time::Instant::ofEpochSecond(1700000000L, 456789123);
            gps : k::time::TimeScale& = k::time::TimeScales::gps();

            val : k::time::TimeScaleValue = gps.valueFromInstant(inst);
            rt : k::time::Instant = gps.instantFromValue(val);

            if (rt.epochSeconds() != inst.epochSeconds()) return 0;
            if (rt.nanoAdjustment() != inst.nanoAdjustment()) return 0;
            return 1;
        }

        test_instant_to_time_scale() : int throws(k::time::TimeScaleDataUnavailableException) {
            inst : k::time::Instant = k::time::Instant::ofEpochSecond(1700000000L, 100);
            utcVal : k::time::TimeScaleValue = inst.toTimeScale(k::time::TimeScales::utc());
            taiVal : k::time::TimeScaleValue = inst.toTimeScale(k::time::TimeScales::tai());
            gpsVal : k::time::TimeScaleValue = inst.toTimeScale(k::time::TimeScales::gps());

            if (!(utcVal == k::time::TimeScales::utc().valueFromInstant(inst))) return 0;
            if (!(taiVal == k::time::TimeScales::tai().valueFromInstant(inst))) return 0;
            if (!(gpsVal == k::time::TimeScales::gps().valueFromInstant(inst))) return 0;

            return 1;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto check = [&](const char* sym) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == 1);
    };

    check("test_utc_roundtrip");
    check("test_tai_roundtrip");
    check("test_gps_roundtrip");
    check("test_instant_to_time_scale");
}

// ═════════════════════════════════════════════════════════════════════════════
// 5. TAI - GPS constant relation: TAI - GPS == 19 seconds
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("TimeScale: constant relation TAI - GPS == 19 seconds", "[libk][time][scale][tai_gps]") {
    auto jit = jit_k(R"SRC(
        module __test_tai_gps_relation__;

        test_constant_19s_diff(epochSec: long, nano: int) : int throws(k::time::TimeScaleDataUnavailableException) {
            inst : k::time::Instant = k::time::Instant::ofEpochSecond(epochSec, nano);
            tai : k::time::TimeScale& = k::time::TimeScales::tai();
            gps : k::time::TimeScale& = k::time::TimeScales::gps();

            taiVal : k::time::TimeScaleValue = tai.valueFromInstant(inst);
            gpsVal : k::time::TimeScaleValue = gps.valueFromInstant(inst);

            diffSec : long = taiVal.wholeSeconds() - gpsVal.wholeSeconds();
            if (diffSec != 19L) return 0;
            if (taiVal.nanoAdjustment() != gpsVal.nanoAdjustment()) return 0;

            return 1;
        }

        test_across_history() : int throws(k::time::TimeScaleDataUnavailableException) {
            // 1980 GPS epoch
            if (test_constant_19s_diff(315964809L, 0) == 0) return 0;
            // 2000 Y2K
            if (test_constant_19s_diff(946684800L, 500) == 0) return 0;
            // 2016 leap second
            if (test_constant_19s_diff(1483228826L, 123) == 0) return 0;
            // 2017
            if (test_constant_19s_diff(1483228827L, 0) == 0) return 0;
            // 2026 present
            if (test_constant_19s_diff(1773000000L, 999) == 0) return 0;

            return 1;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto check = [&](const char* sym) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == 1);
    };

    check("test_across_history");
}

// ═════════════════════════════════════════════════════════════════════════════
// 6. Leap second step in UTC vs continuous TAI and GPS
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("TimeScale: leap second step in UTC vs continuous TAI and GPS", "[libk][time][scale][leap_step]") {
    auto jit = jit_k(R"SRC(
        module __test_leap_step__;

        test_leap_step() : int throws(k::time::TimeScaleDataUnavailableException) {
            // 2016-12-31 23:59:59 UTC is continuous second 1483228825L
            instBefore : k::time::Instant = k::time::Instant::ofEpochSecond(1483228825L);
            // 2017-01-01 00:00:00 UTC is continuous second 1483228827L
            instAfter : k::time::Instant = k::time::Instant::ofEpochSecond(1483228827L);

            utc : k::time::TimeScale& = k::time::TimeScales::utc();
            tai : k::time::TimeScale& = k::time::TimeScales::tai();
            gps : k::time::TimeScale& = k::time::TimeScales::gps();

            utcBefore : k::time::TimeScaleValue = utc.valueFromInstant(instBefore);
            utcAfter : k::time::TimeScaleValue = utc.valueFromInstant(instAfter);

            taiBefore : k::time::TimeScaleValue = tai.valueFromInstant(instBefore);
            taiAfter : k::time::TimeScaleValue = tai.valueFromInstant(instAfter);

            gpsBefore : k::time::TimeScaleValue = gps.valueFromInstant(instBefore);
            gpsAfter : k::time::TimeScaleValue = gps.valueFromInstant(instAfter);

            // UTC step: from 23:59:59 (1483228799L) to 00:00:00 (1483228800L),
            // UTC wholeSeconds advances by only 1 second!
            utcDiff : long = utcAfter.wholeSeconds() - utcBefore.wholeSeconds();
            if (utcDiff != 1L) return 0;

            // TAI is continuous: advances by exactly 2 elapsed SI seconds!
            taiDiff : long = taiAfter.wholeSeconds() - taiBefore.wholeSeconds();
            if (taiDiff != 2L) return 0;

            // GPS is continuous: advances by exactly 2 elapsed SI seconds!
            gpsDiff : long = gpsAfter.wholeSeconds() - gpsBefore.wholeSeconds();
            if (gpsDiff != 2L) return 0;

            // TAI - GPS is always 19 seconds at both boundaries
            if (taiBefore.wholeSeconds() - gpsBefore.wholeSeconds() != 19L) return 0;
            if (taiAfter.wholeSeconds() - gpsAfter.wholeSeconds() != 19L) return 0;

            return 1;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto check = [&](const char* sym) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == 1);
    };

    check("test_leap_step");
}

// ═════════════════════════════════════════════════════════════════════════════
// 7. Duration invariance across scales
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("TimeScale: duration invariance across scales", "[libk][time][scale][duration_invariance]") {
    auto jit = jit_k(R"SRC(
        module __test_duration_invariance__;

        test_duration_invariance() : int
            throws(k::time::TemporalArithmeticException,
                   k::time::TimeScaleDataUnavailableException,
                   k::time::InvalidTemporalValueException) {
            inst1 : k::time::Instant = k::time::Instant::ofEpochSecond(1483228825L); // 23:59:59
            inst2 : k::time::Instant = k::time::Instant::ofEpochSecond(1483228827L); // 00:00:00

            // Baseline duration on timeline
            durTimeline : k::time::Duration = inst2 - inst1;
            if (durTimeline.secondsPart() != 2L) return 0;

            utc : k::time::TimeScale& = k::time::TimeScales::utc();
            tai : k::time::TimeScale& = k::time::TimeScales::tai();
            gps : k::time::TimeScale& = k::time::TimeScales::gps();

            // Reconstruct from UTC scale values
            utc1 : k::time::Instant = utc.instantFromValue(utc.valueFromInstant(inst1));
            utc2 : k::time::Instant = utc.instantFromValue(utc.valueFromInstant(inst2));
            durUtc : k::time::Duration = utc2 - utc1;
            if (durUtc.secondsPart() != 2L) return 0;

            // Reconstruct from TAI scale values
            tai1 : k::time::Instant = tai.instantFromValue(tai.valueFromInstant(inst1));
            tai2 : k::time::Instant = tai.instantFromValue(tai.valueFromInstant(inst2));
            durTai : k::time::Duration = tai2 - tai1;
            if (durTai.secondsPart() != 2L) return 0;

            // Reconstruct from GPS scale values
            gps1 : k::time::Instant = gps.instantFromValue(gps.valueFromInstant(inst1));
            gps2 : k::time::Instant = gps.instantFromValue(gps.valueFromInstant(inst2));
            durGps : k::time::Duration = gps2 - gps1;
            if (durGps.secondsPart() != 2L) return 0;

            // All reconstructed durations are identical
            if (!(durTimeline == durUtc)) return 0;
            if (!(durTimeline == durTai)) return 0;
            if (!(durTimeline == durGps)) return 0;

            return 1;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto check = [&](const char* sym) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == 1);
    };

    check("test_duration_invariance");
}

// ═════════════════════════════════════════════════════════════════════════════
// 8. TimeSourceId & TimeObservation tests
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("TimeSourceId and TimeObservation: creation, uncertainty, equality", "[libk][time][source][observation]") {
    auto jit = jit_k(R"SRC(
        module __test_time_observation__;

        test_source_id() : int throws(k::time::InvalidTemporalValueException) {
            sys : k::time::TimeSourceId = k::time::TimeSourceId::systemClock();
            ntp : k::time::TimeSourceId = k::time::TimeSourceId::ntp();
            ptp : k::time::TimeSourceId = k::time::TimeSourceId::ptp();
            gnss : k::time::TimeSourceId = k::time::TimeSourceId::gnss();
            rtc : k::time::TimeSourceId = k::time::TimeSourceId::rtc();

            if (sys.value() != k::String("SystemClock")) return 0;
            if (ntp.value() != k::String("NTP")) return 0;
            if (ptp.value() != k::String("PTP")) return 0;
            if (gnss.value() != k::String("GNSS")) return 0;
            if (rtc.value() != k::String("RTC")) return 0;

            if (!(sys == k::time::TimeSourceId::of(k::String("SystemClock")))) return 0;
            if (sys == ntp) return 0;
            if (!(sys != ntp)) return 0;

            return 1;
        }

        test_observation_without_uncertainty() : int throws(k::time::InvalidTemporalValueException) {
            inst : k::time::Instant = k::time::Instant::ofEpochSecond(1000L, 500);
            srcId : k::time::TimeSourceId = k::time::TimeSourceId::systemClock();
            res : k::time::Duration = k::time::Duration::ofNanos(1L);

            obs : k::time::TimeObservation = k::time::TimeObservation(inst, srcId, k::time::TimeScales::utc(), res);

            if (obs.instant().epochSeconds() != 1000L) return 0;
            if (obs.instant().nanoAdjustment() != 500) return 0;
            if (obs.sourceId().value() != k::String("SystemClock")) return 0;
            if (obs.scale().id().value() != k::String("UTC")) return 0;
            if (obs.resolution().nanoAdjustment() != 1) return 0;
            if (obs.hasUncertainty()) return 0;
            if (obs.uncertainty().hasValue()) return 0;

            return 1;
        }

        test_observation_with_uncertainty() : int throws(k::time::InvalidTemporalValueException) {
            inst : k::time::Instant = k::time::Instant::ofEpochSecond(2000L, 0);
            srcId : k::time::TimeSourceId = k::time::TimeSourceId::ntp();
            res : k::time::Duration = k::time::Duration::ofMillis(1L);
            unc : k::time::Duration = k::time::Duration::ofMillis(10L);

            obs : k::time::TimeObservation = k::time::TimeObservation(inst, srcId, k::time::TimeScales::utc(), res, unc);

            if (!obs.hasUncertainty()) return 0;
            if (!obs.uncertainty().hasValue()) return 0;
            if (obs.uncertainty().get().toMillis() != 10L) return 0;

            return 1;
        }

        test_observation_equality() : int throws(k::time::InvalidTemporalValueException) {
            inst : k::time::Instant = k::time::Instant::ofEpochSecond(2000L, 0);
            srcId : k::time::TimeSourceId = k::time::TimeSourceId::ptp();
            res : k::time::Duration = k::time::Duration::ofNanos(10L);
            unc : k::time::Duration = k::time::Duration::ofNanos(50L);

            obs1 : k::time::TimeObservation = k::time::TimeObservation(inst, srcId, k::time::TimeScales::tai(), res, unc);
            obs2 : k::time::TimeObservation = k::time::TimeObservation(inst, srcId, k::time::TimeScales::tai(), res, unc);
            obs3 : k::time::TimeObservation = k::time::TimeObservation(inst, srcId, k::time::TimeScales::tai(), res);

            if (!(obs1 == obs2)) return 0;
            if (obs1 != obs2) return 0;
            if (obs1 == obs3) return 0;
            if (!(obs1 != obs3)) return 0;

            return 1;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto check = [&](const char* sym) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == 1);
    };

    check("test_source_id");
    check("test_observation_without_uncertainty");
    check("test_observation_with_uncertainty");
    check("test_observation_equality");
}

// ═════════════════════════════════════════════════════════════════════════════
// 9. Mock TimeSource test
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("TimeSource: mock source returning observations", "[libk][time][source][mock]") {
    auto jit = jit_k(R"SRC(
        module __test_mock_source__;

        public class MockGpsSource : public k::time::TimeSource {
        private:
            _id          : k::time::TimeSourceId;
            _instant     : k::time::Instant;
            _uncertainty : k::time::Duration;

        public:
            MockGpsSource(inst: const k::time::Instant&, uncertainty: const k::time::Duration&) {
                _id = k::time::TimeSourceId::gnss();
                _instant = inst;
                _uncertainty = uncertainty;
            }

            override id() : k::time::TimeSourceId {
                return _id;
            }

            override scale() : ::k::time::TimeScale& {
                return k::time::TimeScales::gps();
            }

            override observation() : k::time::TimeObservation {
                inst : k::time::Instant = _instant;
                idVal : k::time::TimeSourceId = _id;
                sc : k::time::TimeScale& = k::time::TimeScales::gps();
                res : k::time::Duration = k::time::Duration::ofNanos(1L);
                unc : k::time::Duration = _uncertainty;
                return k::time::TimeObservation(inst, idVal, sc, res, unc);
            }
        }

        test_mock_source() : int throws(k::time::TimeDataUnavailableException, k::time::TimeScaleDataUnavailableException) {
            inst : k::time::Instant = k::time::Instant::ofEpochSecond(5000000L, 100);
            unc : k::time::Duration = k::time::Duration::ofNanos(25L);

            mock : MockGpsSource(inst, unc);
            srcRef : k::time::TimeSource& = mock;

            if (srcRef.id().value() != k::String("GNSS")) return 0;
            if (srcRef.scale().id().value() != k::String("GPS")) return 0;

            obs : k::time::TimeObservation = srcRef.observation();
            if (obs.instant().epochSeconds() != 5000000L) return 0;
            if (obs.instant().nanoAdjustment() != 100) return 0;
            if (obs.sourceId().value() != k::String("GNSS")) return 0;
            if (obs.scale().id().value() != k::String("GPS")) return 0;
            if (!obs.hasUncertainty()) return 0;
            if (obs.uncertainty().get().nanoAdjustment() != 25) return 0;

            return 1;
        }

        public class SystemTimeSource : public k::time::TimeSource {
        private:
            _id : k::time::TimeSourceId;

        public:
            SystemTimeSource() {
                _id = k::time::TimeSourceId::systemClock();
            }

            override id() : k::time::TimeSourceId {
                return _id;
            }

            override scale() : ::k::time::TimeScale& {
                return k::time::TimeScales::utc();
            }

            override observation() : k::time::TimeObservation throws(k::time::TimeDataUnavailableException, k::time::TimeScaleDataUnavailableException) {
                inst : k::time::Instant = k::time::SystemClock::instance().now();
                res : k::time::Duration = k::time::SystemClock::instance().resolution();
                idVal : k::time::TimeSourceId = _id;
                sc : k::time::TimeScale& = k::time::TimeScales::utc();
                return k::time::TimeObservation(inst, idVal, sc, res);
            }
        }

        test_system_time_source() : int throws(k::time::TimeDataUnavailableException, k::time::TimeScaleDataUnavailableException) {
            sysSrc : SystemTimeSource;
            src : k::time::TimeSource& = sysSrc;

            if (src.id().value() != k::String("SystemClock")) return 0;
            if (src.scale().id().value() != k::String("UTC")) return 0;

            obs : k::time::TimeObservation = src.observation();
            if (obs.sourceId().value() != k::String("SystemClock")) return 0;
            if (obs.scale().id().value() != k::String("UTC")) return 0;

            return 1;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto check = [&](const char* sym) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == 1);
    };

    check("test_mock_source");
    check("test_system_time_source");
}

// ═════════════════════════════════════════════════════════════════════════════
// 10. ProcessCpuClock and ThreadCpuClock tests
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("CPU clocks: SystemProcessCpuClock and SystemThreadCpuClock", "[libk][time][clock][cpu]") {
    auto jit = jit_k(R"SRC(
        module __test_cpu_clocks__;

        test_process_cpu_clock() : int throws(k::time::TimeDataUnavailableException) {
            clk : k::time::SystemProcessCpuClock& = k::time::SystemProcessCpuClock::instance();
            dur : k::time::Duration = clk.now();
            res : k::time::Duration = clk.resolution();

            // Process CPU time should be non-negative
            if (dur.secondsPart() < 0L) return 0;
            if (dur.nanoAdjustment() < 0) return 0;

            // Resolution should be strictly positive
            if (res.secondsPart() == 0L && res.nanoAdjustment() <= 0) return 0;

            // Test polymorphically via ProcessCpuClock&
            clkRef : k::time::ProcessCpuClock& = clk;
            dur2 : k::time::Duration = clkRef.now();
            if (dur2.secondsPart() < 0L) return 0;

            return 1;
        }

        test_thread_cpu_clock() : int throws(k::time::TimeDataUnavailableException) {
            clk : k::time::SystemThreadCpuClock& = k::time::SystemThreadCpuClock::instance();
            dur : k::time::Duration = clk.now();
            res : k::time::Duration = clk.resolution();

            // Thread CPU time should be non-negative
            if (dur.secondsPart() < 0L) return 0;
            if (dur.nanoAdjustment() < 0) return 0;

            // Resolution should be strictly positive
            if (res.secondsPart() == 0L && res.nanoAdjustment() <= 0) return 0;

            // Test polymorphically via ThreadCpuClock&
            clkRef : k::time::ThreadCpuClock& = clk;
            dur2 : k::time::Duration = clkRef.now();
            if (dur2.secondsPart() < 0L) return 0;

            return 1;
        }

        test_seam_process_cpu() : int throws(k::time::TimeDataUnavailableException) {
            clk : k::time::SystemProcessCpuClock& = k::time::SystemProcessCpuClock::instance();
            dur : k::time::Duration = clk.now();
            if (dur.secondsPart() != 123L || dur.nanoAdjustment() != 456789) return 0;
            return 1;
        }

        test_seam_thread_cpu() : int throws(k::time::TimeDataUnavailableException) {
            clk : k::time::SystemThreadCpuClock& = k::time::SystemThreadCpuClock::instance();
            dur : k::time::Duration = clk.now();
            if (dur.secondsPart() != 321L || dur.nanoAdjustment() != 987654) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto check = [&](const char* sym) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn != nullptr);
        CHECK(fn() == 1);
    };

    check("test_process_cpu_clock");
    check("test_thread_cpu_clock");

    // Test with injected seam provider
    {
        SeamGuard guard([](int kind) -> TestClockReading {
            if (kind == 3 /* K_CLOCK_PROCESS_CPU */) {
                return TestClockReading{123L, 456789, 0};
            }
            if (kind == 4 /* K_CLOCK_THREAD_CPU */) {
                return TestClockReading{321L, 987654, 0};
            }
            return TestClockReading{0L, 0, 1 /* UNAVAILABLE */};
        });

        check("test_seam_process_cpu");
        check("test_seam_thread_cpu");
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// 11. CPU clocks return Duration and domain separation from Instant
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("CPU clocks: return Duration and domain separation from Instant", "[libk][time][clock][cpu_duration]") {
    auto jit = jit_k(R"SRC(
        module __test_cpu_duration__;

        test_duration_operations() : int throws(k::time::TimeDataUnavailableException, k::time::TemporalArithmeticException) {
            procClk : k::time::ProcessCpuClock& = k::time::SystemProcessCpuClock::instance();
            thClk : k::time::ThreadCpuClock& = k::time::SystemThreadCpuClock::instance();

            // Return values are of type Duration
            procDur : k::time::Duration = procClk.now();
            thDur : k::time::Duration = thClk.now();

            // Duration methods and arithmetic apply
            totalSec : long = procDur.secondsPart() + thDur.secondsPart();
            if (totalSec < 0L) return 0;

            sum : k::time::Duration = procDur + thDur;
            if (sum.secondsPart() < 0L) return 0;

            // Duration represents consumed time, can measure elapsed CPU time
            d1 : k::time::Duration = k::time::Duration::ofSeconds(10L, 500);
            d2 : k::time::Duration = k::time::Duration::ofSeconds(15L, 800);
            elapsed : k::time::Duration = d2 - d1;
            if (elapsed.secondsPart() != 5L || elapsed.nanoAdjustment() != 300) return 0;

            return 1;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<int(*)()>("test_duration_operations");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 1);
}

