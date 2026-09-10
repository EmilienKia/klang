/*
 * K Language standard library — Clock, Monotonic, PosixTimestamp tests (Phase 2)
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
// 1. FixedClock tests
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("FixedClock: returns fixed instant and 1ns resolution", "[libk][time][clock][fixed]") {
    auto jit = jit_k(R"SRC(
        module __test_fixed_clock__;

        test_now_sec() : long throws(k::time::TimeScaleDataUnavailableException, k::time::TimeDataUnavailableException) {
            inst : k::time::Instant = k::time::Instant::ofEpochSecond(1000L, 500);
            clock : k::time::FixedClock(inst);
            return clock.now().epochSeconds();
        }

        test_now_nano() : int throws(k::time::TimeScaleDataUnavailableException, k::time::TimeDataUnavailableException) {
            inst : k::time::Instant = k::time::Instant::ofEpochSecond(1000L, 500);
            clock : k::time::FixedClock(inst);
            return clock.now().nanoAdjustment();
        }

        test_resolution_nanos() : long throws(k::time::TimeDataUnavailableException, k::time::TemporalArithmeticException) {
            inst : k::time::Instant = k::time::Instant::ofEpochSecond(1000L, 500);
            clock : k::time::FixedClock(inst);
            return clock.resolution().toNanosExact();
        }

        // Test polymorphically via Clock interface reference
        test_via_interface() : long throws(k::time::TimeScaleDataUnavailableException, k::time::TimeDataUnavailableException) {
            inst : k::time::Instant = k::time::Instant::ofEpochSecond(42L, 0);
            clock : k::time::FixedClock(inst);
            clkRef : k::time::Clock& = clock;
            return clkRef.now().epochSeconds();
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn_sec = jit->lookup_symbol<int64_t(*)()>("test_now_sec");
    REQUIRE(fn_sec != nullptr);
    CHECK(fn_sec() == 1000L);

    auto fn_nano = jit->lookup_symbol<int(*)()>("test_now_nano");
    REQUIRE(fn_nano != nullptr);
    CHECK(fn_nano() == 500);

    auto fn_res = jit->lookup_symbol<int64_t(*)()>("test_resolution_nanos");
    REQUIRE(fn_res != nullptr);
    CHECK(fn_res() == 1L);

    auto fn_iface = jit->lookup_symbol<int64_t(*)()>("test_via_interface");
    REQUIRE(fn_iface != nullptr);
    CHECK(fn_iface() == 42L);
}

// ═════════════════════════════════════════════════════════════════════════════
// 2. SequenceClock tests
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("SequenceClock: sequence progression and remaining count", "[libk][time][clock][sequence]") {
    auto jit = jit_k(R"SRC(
        module __test_sequence_clock__;

        test_progression() : int throws(k::time::TimeDataUnavailableException) {
            values : k::time::Instant[3]{
                k::time::Instant::ofEpochSecond(10L, 0),
                k::time::Instant::ofEpochSecond(20L, 0),
                k::time::Instant::ofEpochSecond(30L, 0)
            };
            clock : k::time::SequenceClock(values);

            if (clock.remaining() != 3) return 1;

            t1 : k::time::Instant = clock.now();
            if (t1.epochSeconds() != 10L) return 2;
            if (clock.remaining() != 2) return 3;

            t2 : k::time::Instant = clock.now();
            if (t2.epochSeconds() != 20L) return 4;
            if (clock.remaining() != 1) return 5;

            t3 : k::time::Instant = clock.now();
            if (t3.epochSeconds() != 30L) return 6;
            if (clock.remaining() != 0) return 7;

            return 0; // all succeeded
        }

        test_exhaustion() : int {
            values : k::time::Instant[1]{
                k::time::Instant::ofEpochSecond(100L, 0)
            };
            clock : k::time::SequenceClock(values);

            try {
                clock.now(); // 1st succeeds
            } catch (e: k::time::TimeDataUnavailableException&) {
                return 1; // unexpected
            }

            try {
                clock.now(); // 2nd exhausted -> throws TimeDataUnavailableException(515)
                return 2; // should not reach
            } catch (e: k::time::TimeDataUnavailableException&) {
                return e.getCode(); // should be 515
            }
        }

        test_empty_clock() : int {
            values : k::time::Instant[0]{};
            clock : k::time::SequenceClock(values);
            if (clock.remaining() != 0) return -1;
            try {
                clock.now();
                return -2;
            } catch (e: k::time::TimeDataUnavailableException&) {
                return e.getCode(); // 515
            }
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn_prog = jit->lookup_symbol<int(*)()>("test_progression");
    REQUIRE(fn_prog != nullptr);
    CHECK(fn_prog() == 0);

    auto fn_exh = jit->lookup_symbol<int(*)()>("test_exhaustion");
    REQUIRE(fn_exh != nullptr);
    CHECK(fn_exh() == 515);

    auto fn_empty = jit->lookup_symbol<int(*)()>("test_empty_clock");
    REQUIRE(fn_empty != nullptr);
    CHECK(fn_empty() == 515);
}

// ═════════════════════════════════════════════════════════════════════════════
// 3. SystemClock tests
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("SystemClock: now() throws 513 in Phase 2, resolution succeeds", "[libk][time][clock][system]") {
    auto jit = jit_k(R"SRC(
        module __test_system_clock__;

        test_now_throws_513() : int {
            clock : k::time::SystemClock& = k::time::SystemClock::instance();
            try {
                clock.now();
                return 0; // should not reach
            } catch (e: k::time::TimeScaleDataUnavailableException&) {
                return e.getCode(); // should be 513
            } catch (e: k::time::TimeDataUnavailableException&) {
                return 1;
            }
        }

        test_resolution() : int throws(k::time::TimeDataUnavailableException) {
            clock : k::time::SystemClock& = k::time::SystemClock::instance();
            res : k::time::Duration = clock.resolution();
            // Resolution must be non-negative
            if (res.isNegative()) return 1;
            return 0;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn_now = jit->lookup_symbol<int(*)()>("test_now_throws_513");
    REQUIRE(fn_now != nullptr);
    CHECK(fn_now() == 513);

    auto fn_res = jit->lookup_symbol<int(*)()>("test_resolution");
    REQUIRE(fn_res != nullptr);
    CHECK(fn_res() == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// 4. MonotonicClock and MonotonicInstant tests
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("FixedMonotonicClock: deterministic behavior and advance", "[libk][time][monotonic][fixed]") {
    auto jit = jit_k(R"SRC(
        module __test_fixed_monotonic_clock__;

        test_fixed() : int throws(k::time::TimeDataUnavailableException, k::time::TemporalArithmeticException, k::time::InvalidTemporalValueException) {
            id : k::time::MonotonicClockId(42);
            initial : k::time::MonotonicInstant(id, 100L, 500);
            clock : k::time::FixedMonotonicClock(id, initial);

            if (clock.id() != id) return 1;
            if (clock.includesSuspend()) return 2;

            t1 : k::time::MonotonicInstant = clock.now();
            if (t1.secondsPart() != 100L) return 3;
            if (t1.nanoAdjustment() != 500) return 4;

            clock.advance(k::time::Duration::ofSeconds(5L, 100));
            t2 : k::time::MonotonicInstant = clock.now();
            if (t2.secondsPart() != 105L) return 5;
            if (t2.nanoAdjustment() != 600) return 6;

            dur : k::time::Duration = t2 - t1;
            if (dur.secondsPart() != 5L || dur.nanoAdjustment() != 100) return 7;

            return 0;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<int(*)()>("test_fixed");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 0);
}

TEST_CASE("SystemMonotonicClock: activeTime and elapsedTime", "[libk][time][monotonic][system]") {
    auto jit = jit_k(R"SRC(
        module __test_system_monotonic_clock__;

        test_ids_differ() : int throws(k::time::TimeDataUnavailableException) {
            active : k::time::SystemMonotonicClock& = k::time::SystemMonotonicClock::activeTime();
            elapsed : k::time::SystemMonotonicClock& = k::time::SystemMonotonicClock::elapsedTime();

            if (active.id() == elapsed.id()) return 1;
            if (active.includesSuspend()) return 2;
            if (!elapsed.includesSuspend()) return 3;

            return 0;
        }

        test_read_monotonic() : int throws(k::time::TimeDataUnavailableException, k::time::InvalidTemporalValueException) {
            active : k::time::SystemMonotonicClock& = k::time::SystemMonotonicClock::activeTime();
            t1 : k::time::MonotonicInstant = active.now();
            t2 : k::time::MonotonicInstant = active.now();

            if (t1.clockId() != active.id()) return 1;
            if (t1.nanoAdjustment() < 0 || t1.nanoAdjustment() >= 1000000000) return 2;

            // t2 must be >= t1
            if (t2 < t1) return 3;

            return 0;
        }

        test_resolution() : int throws(k::time::TimeDataUnavailableException) {
            active : k::time::SystemMonotonicClock& = k::time::SystemMonotonicClock::activeTime();
            elapsed : k::time::SystemMonotonicClock& = k::time::SystemMonotonicClock::elapsedTime();

            r1 : k::time::Duration = active.resolution();
            r2 : k::time::Duration = elapsed.resolution();

            if (r1.isNegative() || r2.isNegative()) return 1;
            return 0;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn_ids = jit->lookup_symbol<int(*)()>("test_ids_differ");
    REQUIRE(fn_ids != nullptr);
    CHECK(fn_ids() == 0);

    auto fn_read = jit->lookup_symbol<int(*)()>("test_read_monotonic");
    REQUIRE(fn_read != nullptr);
    CHECK(fn_read() == 0);

    auto fn_res = jit->lookup_symbol<int(*)()>("test_resolution");
    REQUIRE(fn_res != nullptr);
    CHECK(fn_res() == 0);
}

TEST_CASE("MonotonicInstant: cross-domain rejection throws InvalidTemporalValueException", "[libk][time][monotonic][cross-domain]") {
    auto jit = jit_k(R"SRC(
        module __test_monotonic_cross_domain__;

        test_cross_sub() : int {
            id1 : k::time::MonotonicClockId(1);
            id2 : k::time::MonotonicClockId(2);
            t1 : k::time::MonotonicInstant(id1, 100L, 0);
            t2 : k::time::MonotonicInstant(id2, 200L, 0);

            try {
                dur : k::time::Duration = t2 - t1;
                return 0; // should not reach
            } catch (e: k::time::InvalidTemporalValueException&) {
                return e.getCode(); // 501
            } catch (e: k::time::TemporalException&) {
                return 1;
            }
        }

        test_cross_until() : int {
            id1 : k::time::MonotonicClockId(1);
            id2 : k::time::MonotonicClockId(2);
            t1 : k::time::MonotonicInstant(id1, 100L, 0);
            t2 : k::time::MonotonicInstant(id2, 200L, 0);

            try {
                dur : k::time::Duration = t1.until(t2);
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
                return e.getCode(); // 501
            } catch (e: k::time::TemporalException&) {
                return 1;
            }
        }

        test_cross_less() : int {
            id1 : k::time::MonotonicClockId(1);
            id2 : k::time::MonotonicClockId(2);
            t1 : k::time::MonotonicInstant(id1, 100L, 0);
            t2 : k::time::MonotonicInstant(id2, 200L, 0);

            try {
                b : bool = t1 < t2;
                return 0;
            } catch (e: k::time::InvalidTemporalValueException&) {
                return e.getCode(); // 501
            } catch (e: k::time::TemporalException&) {
                return 1;
            }
        }

        test_cross_equality() : int {
            id1 : k::time::MonotonicClockId(1);
            id2 : k::time::MonotonicClockId(2);
            t1 : k::time::MonotonicInstant(id1, 100L, 0);
            t2 : k::time::MonotonicInstant(id2, 100L, 0);

            // Equality across different clock domains returns false, does NOT throw
            if (t1 == t2) return 1;
            if (!(t1 != t2)) return 2;

            return 0;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn_sub = jit->lookup_symbol<int(*)()>("test_cross_sub");
    REQUIRE(fn_sub != nullptr);
    CHECK(fn_sub() == 501);

    auto fn_until = jit->lookup_symbol<int(*)()>("test_cross_until");
    REQUIRE(fn_until != nullptr);
    CHECK(fn_until() == 501);

    auto fn_less = jit->lookup_symbol<int(*)()>("test_cross_less");
    REQUIRE(fn_less != nullptr);
    CHECK(fn_less() == 501);

    auto fn_eq = jit->lookup_symbol<int(*)()>("test_cross_equality");
    REQUIRE(fn_eq != nullptr);
    CHECK(fn_eq() == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// 5. PosixTimestamp tests
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("PosixTimestamp: normalization, ordering, duration arithmetic", "[libk][time][posix]") {
    auto jit = jit_k(R"SRC(
        module __test_posix_timestamp__;

        test_normalization() : int throws(k::time::TemporalArithmeticException) {
            pt : k::time::PosixTimestamp = k::time::PosixTimestamp::ofSeconds(10L, 1500000000);
            if (pt.seconds() != 11L) return 1;
            if (pt.nanoAdjustment() != 500000000) return 2;
            return 0;
        }

        test_ordering() : int throws(k::time::TemporalArithmeticException) {
            p1 : k::time::PosixTimestamp = k::time::PosixTimestamp::ofSeconds(10L, 100);
            p2 : k::time::PosixTimestamp = k::time::PosixTimestamp::ofSeconds(10L, 200);
            p3 : k::time::PosixTimestamp = k::time::PosixTimestamp::ofSeconds(11L, 50);

            if (!(p1 < p2)) return 1;
            if (!(p2 < p3)) return 2;
            if (!(p1 < p3)) return 3;
            if (p3 < p1) return 4;
            if (!(p1 == p1)) return 5;
            if (p1 != p1) return 6;
            if (!(p1 != p2)) return 7;

            return 0;
        }

        test_arithmetic() : int throws(k::time::TemporalArithmeticException) {
            p1 : k::time::PosixTimestamp = k::time::PosixTimestamp::ofSeconds(100L, 500000000);
            d : k::time::Duration = k::time::Duration::ofSeconds(10L, 600000000);

            // p1 + d = 100s + 10s + 1100000000ns = 111s 100000000ns
            p2 : k::time::PosixTimestamp = p1 + d;
            if (p2.seconds() != 111L || p2.nanoAdjustment() != 100000000) return 1;

            // p2 - d = p1
            p3 : k::time::PosixTimestamp = p2 - d;
            if (p3.seconds() != 100L || p3.nanoAdjustment() != 500000000) return 2;

            // p2 - p1 = d
            diff : k::time::Duration = p2 - p1;
            if (diff.secondsPart() != 10L || diff.nanoAdjustment() != 600000000) return 3;

            return 0;
        }

        test_to_instant_throws_513() : int {
            pt : k::time::PosixTimestamp = k::time::PosixTimestamp::ofSeconds(100L, 0);
            try {
                pt.toInstant();
                return 0;
            } catch (e: k::time::TimeScaleDataUnavailableException&) {
                return e.getCode(); // 513
            } catch (e: k::time::TemporalException&) {
                return 1;
            }
        }

        test_instant_from_posix_throws_513() : int {
            pt : k::time::PosixTimestamp = k::time::PosixTimestamp::ofSeconds(100L, 0);
            try {
                k::time::Instant::fromPosixTimestamp(pt);
                return 0;
            } catch (e: k::time::TimeScaleDataUnavailableException&) {
                return e.getCode(); // 513
            } catch (e: k::time::TemporalException&) {
                return 1;
            }
        }

        test_system_now() : int throws(k::time::TimeDataUnavailableException) {
            now : k::time::PosixTimestamp = k::time::PosixTimestamp::systemNow();
            if (now.seconds() <= 0L) return 1;
            if (now.nanoAdjustment() < 0 || now.nanoAdjustment() >= 1000000000) return 2;
            return 0;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn_norm = jit->lookup_symbol<int(*)()>("test_normalization");
    REQUIRE(fn_norm != nullptr);
    CHECK(fn_norm() == 0);

    auto fn_ord = jit->lookup_symbol<int(*)()>("test_ordering");
    REQUIRE(fn_ord != nullptr);
    CHECK(fn_ord() == 0);

    auto fn_arith = jit->lookup_symbol<int(*)()>("test_arithmetic");
    REQUIRE(fn_arith != nullptr);
    CHECK(fn_arith() == 0);

    auto fn_to_inst = jit->lookup_symbol<int(*)()>("test_to_instant_throws_513");
    REQUIRE(fn_to_inst != nullptr);
    CHECK(fn_to_inst() == 513);

    auto fn_from_posix = jit->lookup_symbol<int(*)()>("test_instant_from_posix_throws_513");
    REQUIRE(fn_from_posix != nullptr);
    CHECK(fn_from_posix() == 513);

    auto fn_now = jit->lookup_symbol<int(*)()>("test_system_now");
    REQUIRE(fn_now != nullptr);
    CHECK(fn_now() == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// 6. Injected seam / fake provider tests
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Platform clock seam: injected readings and status mapping", "[libk][time][clock][seam]") {
    auto jit = jit_k(R"SRC(
        module __test_clock_seam__;

        test_read_posix_now() : int throws(k::time::TimeDataUnavailableException) {
            pt : k::time::PosixTimestamp = k::time::PosixTimestamp::systemNow();
            if (pt.seconds() != 1700000000L) return 1;
            if (pt.nanoAdjustment() != 123456789) return 2;
            return 0;
        }

        test_posix_unavailable() : int {
            try {
                k::time::PosixTimestamp::systemNow();
                return 0; // should not reach
            } catch (e: k::time::TimeDataUnavailableException&) {
                return e.getCode(); // should be 510
            } catch (e: k::time::TemporalException&) {
                return 1;
            }
        }

        test_monotonic_failed() : int {
            try {
                active : k::time::SystemMonotonicClock& = k::time::SystemMonotonicClock::activeTime();
                active.now();
                return 0;
            } catch (e: k::time::TimeDataUnavailableException&) {
                return e.getCode(); // should be 510
            } catch (e: k::time::TemporalException&) {
                return 1;
            }
        }
    )SRC");
    REQUIRE(jit != nullptr);

    SECTION("Seam returns successful fixed reading") {
        SeamGuard guard([](int kind) -> TestClockReading {
            if (kind == 0) { // K_CLOCK_REALTIME
                return {1700000000LL, 123456789, 0}; // OK
            }
            return {0, 0, 0};
        });

        auto fn = jit->lookup_symbol<int(*)()>("test_read_posix_now");
        REQUIRE(fn != nullptr);
        CHECK(fn() == 0);
    }

    SECTION("Seam returns K_CLOCK_STATUS_UNAVAILABLE -> TimeDataUnavailableException(510)") {
        SeamGuard guard([](int kind) -> TestClockReading {
            (void)kind;
            return {0, 0, 1}; // K_CLOCK_STATUS_UNAVAILABLE
        });

        auto fn = jit->lookup_symbol<int(*)()>("test_posix_unavailable");
        REQUIRE(fn != nullptr);
        CHECK(fn() == 510);
    }

    SECTION("Seam returns K_CLOCK_STATUS_FAILED -> TimeDataUnavailableException(510)") {
        SeamGuard guard([](int kind) -> TestClockReading {
            (void)kind;
            return {0, 0, 2}; // K_CLOCK_STATUS_FAILED
        });

        auto fn = jit->lookup_symbol<int(*)()>("test_monotonic_failed");
        REQUIRE(fn != nullptr);
        CHECK(fn() == 510);
    }
}
