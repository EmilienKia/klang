/*
 * K Language standard library — Math tests
 *
 * Copyright 2023-2026 Emilien Kia
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

/**
 * Tests for k::math::Math static utility class.
 *
 * These tests exercise the behaviour of the optional k::math library by
 * JIT-compiling small K programs that call Math::abs, Math::min, Math::max
 * and Math::clamp.
 *
 * The test executable loads both libk.so (base stdlib) and libk.math.so
 * (optional math library) via dlopen at test startup.
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

#include <dlfcn.h>
#include <unordered_map>

// Compile-time paths injected by CMake (see libk/libkmath/CMakeLists.txt).
#ifndef LIBK_KDI_DIR
#error "LIBK_KDI_DIR must be defined — check CMakeLists.txt"
#endif
#ifndef LIBK_LIB_DIR
#error "LIBK_LIB_DIR must be defined — check CMakeLists.txt"
#endif
#ifndef LIBKMATH_KDI_DIR
#error "LIBKMATH_KDI_DIR must be defined — check CMakeLists.txt"
#endif
#ifndef LIBKMATH_LIB_DIR
#error "LIBKMATH_LIB_DIR must be defined — check CMakeLists.txt"
#endif

namespace {

/// Load a shared library once into the current process.
void load_lib_once(const std::string& path) {
    static std::unordered_map<std::string, void*> loaded;
    if (loaded.count(path)) return;
    void* h = dlopen(path.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!h) {
        FAIL("Cannot load " << path << ": " << dlerror());
    }
    loaded[path] = h;
}

/// Compile K source that imports both k and k::math, then JIT it.
std::unique_ptr<k::model::gen::jit> jit_math(std::string_view src) {
    // Load both libraries into the current process.
    // LD_LIBRARY_PATH is set by CTest so dlopen can find libk.so as a
    // transitive dependency of libk.math.so.
    load_lib_once(std::string(LIBK_LIB_DIR) + "/libk.so");
    load_lib_once(std::string(LIBKMATH_LIB_DIR) + "/libk.math.so");

    auto comp = k::compiler::create();
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_search_dir(LIBK_KDI_DIR);
    resolver->add_search_dir(LIBKMATH_KDI_DIR);
    comp->set_file_resolver(resolver);

    try {
        comp->parse_source("", src, true, false);
        return comp->to_jit();
    } catch (const k::log::compiler_error&) {
        return nullptr;
    } catch (std::exception& ex) {
        std::cerr << "Unexpected error: " << ex.what() << std::endl;
        return nullptr;
    }
}

} // anonymous namespace


// ═════════════════════════════════════════════════════════════════════════════
// 1. Math::abs
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Math::abs — positive and negative integers", "[libkmath][math]") {
    auto jit = jit_math(R"SRC(
        module __math_abs__;
        import k::math;

        test_abs_pos() : int {
            return k::math::Math::abs(42);
        }

        test_abs_neg() : int {
            return k::math::Math::abs(-7);
        }

        test_abs_zero() : int {
            return k::math::Math::abs(0);
        }
    )SRC");
    REQUIRE(jit);

    auto abs_pos = jit->lookup_symbol<int(*)()>("test_abs_pos");
    REQUIRE(abs_pos);
    CHECK(abs_pos() == 42);

    auto abs_neg = jit->lookup_symbol<int(*)()>("test_abs_neg");
    REQUIRE(abs_neg);
    CHECK(abs_neg() == 7);

    auto abs_zero = jit->lookup_symbol<int(*)()>("test_abs_zero");
    REQUIRE(abs_zero);
    CHECK(abs_zero() == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// 2. Math::min and Math::max
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Math::min and Math::max", "[libkmath][math]") {
    auto jit = jit_math(R"SRC(
        module __math_minmax__;
        import k::math;

        test_min() : int {
            return k::math::Math::min(3, 7);
        }

        test_max() : int {
            return k::math::Math::max(3, 7);
        }

        test_min_equal() : int {
            return k::math::Math::min(5, 5);
        }

        test_max_equal() : int {
            return k::math::Math::max(5, 5);
        }

        test_min_neg() : int {
            return k::math::Math::min(-10, -3);
        }

        test_max_neg() : int {
            return k::math::Math::max(-10, -3);
        }
    )SRC");
    REQUIRE(jit);

    auto test_min = jit->lookup_symbol<int(*)()>("test_min");
    REQUIRE(test_min);
    CHECK(test_min() == 3);

    auto test_max = jit->lookup_symbol<int(*)()>("test_max");
    REQUIRE(test_max);
    CHECK(test_max() == 7);

    auto test_min_eq = jit->lookup_symbol<int(*)()>("test_min_equal");
    REQUIRE(test_min_eq);
    CHECK(test_min_eq() == 5);

    auto test_max_eq = jit->lookup_symbol<int(*)()>("test_max_equal");
    REQUIRE(test_max_eq);
    CHECK(test_max_eq() == 5);

    auto test_min_neg = jit->lookup_symbol<int(*)()>("test_min_neg");
    REQUIRE(test_min_neg);
    CHECK(test_min_neg() == -10);

    auto test_max_neg = jit->lookup_symbol<int(*)()>("test_max_neg");
    REQUIRE(test_max_neg);
    CHECK(test_max_neg() == -3);
}

// ═════════════════════════════════════════════════════════════════════════════
// 3. Math::clamp
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Math::clamp", "[libkmath][math]") {
    auto jit = jit_math(R"SRC(
        module __math_clamp__;
        import k::math;

        test_clamp_within() : int {
            return k::math::Math::clamp(50, 0, 100);
        }

        test_clamp_below() : int {
            return k::math::Math::clamp(-5, 0, 100);
        }

        test_clamp_above() : int {
            return k::math::Math::clamp(200, 0, 100);
        }

        test_clamp_at_lo() : int {
            return k::math::Math::clamp(0, 0, 100);
        }

        test_clamp_at_hi() : int {
            return k::math::Math::clamp(100, 0, 100);
        }
    )SRC");
    REQUIRE(jit);

    auto within = jit->lookup_symbol<int(*)()>("test_clamp_within");
    REQUIRE(within);
    CHECK(within() == 50);

    auto below = jit->lookup_symbol<int(*)()>("test_clamp_below");
    REQUIRE(below);
    CHECK(below() == 0);

    auto above = jit->lookup_symbol<int(*)()>("test_clamp_above");
    REQUIRE(above);
    CHECK(above() == 100);

    auto at_lo = jit->lookup_symbol<int(*)()>("test_clamp_at_lo");
    REQUIRE(at_lo);
    CHECK(at_lo() == 0);

    auto at_hi = jit->lookup_symbol<int(*)()>("test_clamp_at_hi");
    REQUIRE(at_hi);
    CHECK(at_hi() == 100);
}

// ═════════════════════════════════════════════════════════════════════════════
// 4. Math::absLong
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Math::absLong — positive and negative longs", "[libkmath][math]") {
    auto jit = jit_math(R"SRC(
        module __math_abs_long__;
        import k::math;

        test_abs_pos() : long {
            return k::math::Math::absLong(42L);
        }

        test_abs_neg() : long {
            return k::math::Math::absLong(-7L);
        }

        test_abs_zero() : long {
            return k::math::Math::absLong(0L);
        }

        test_abs_large() : long {
            return k::math::Math::absLong(-2147483648L);
        }
    )SRC");
    REQUIRE(jit);

    auto abs_pos = jit->lookup_symbol<long(*)()>("test_abs_pos");
    REQUIRE(abs_pos);
    CHECK(abs_pos() == 42L);

    auto abs_neg = jit->lookup_symbol<long(*)()>("test_abs_neg");
    REQUIRE(abs_neg);
    CHECK(abs_neg() == 7L);

    auto abs_zero = jit->lookup_symbol<long(*)()>("test_abs_zero");
    REQUIRE(abs_zero);
    CHECK(abs_zero() == 0L);

    auto abs_large = jit->lookup_symbol<long(*)()>("test_abs_large");
    REQUIRE(abs_large);
    CHECK(abs_large() == 2147483648L);
}

// ═════════════════════════════════════════════════════════════════════════════
// 5. Math::minLong and Math::maxLong
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Math::minLong and Math::maxLong", "[libkmath][math]") {
    auto jit = jit_math(R"SRC(
        module __math_minmax_long__;
        import k::math;

        test_min() : long {
            return k::math::Math::minLong(3L, 7L);
        }

        test_max() : long {
            return k::math::Math::maxLong(3L, 7L);
        }

        test_min_equal() : long {
            return k::math::Math::minLong(5L, 5L);
        }

        test_max_equal() : long {
            return k::math::Math::maxLong(5L, 5L);
        }

        test_min_neg() : long {
            return k::math::Math::minLong(-10L, -3L);
        }

        test_max_neg() : long {
            return k::math::Math::maxLong(-10L, -3L);
        }
    )SRC");
    REQUIRE(jit);

    auto test_min = jit->lookup_symbol<long(*)()>("test_min");
    REQUIRE(test_min);
    CHECK(test_min() == 3L);

    auto test_max = jit->lookup_symbol<long(*)()>("test_max");
    REQUIRE(test_max);
    CHECK(test_max() == 7L);

    auto test_min_eq = jit->lookup_symbol<long(*)()>("test_min_equal");
    REQUIRE(test_min_eq);
    CHECK(test_min_eq() == 5L);

    auto test_max_eq = jit->lookup_symbol<long(*)()>("test_max_equal");
    REQUIRE(test_max_eq);
    CHECK(test_max_eq() == 5L);

    auto test_min_neg = jit->lookup_symbol<long(*)()>("test_min_neg");
    REQUIRE(test_min_neg);
    CHECK(test_min_neg() == -10L);

    auto test_max_neg = jit->lookup_symbol<long(*)()>("test_max_neg");
    REQUIRE(test_max_neg);
    CHECK(test_max_neg() == -3L);
}

// ═════════════════════════════════════════════════════════════════════════════
// 6. Math::clampLong
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Math::clampLong", "[libkmath][math]") {
    auto jit = jit_math(R"SRC(
        module __math_clamp_long__;
        import k::math;

        test_clamp_within() : long {
            return k::math::Math::clampLong(50L, 0L, 100L);
        }

        test_clamp_below() : long {
            return k::math::Math::clampLong(-5L, 0L, 100L);
        }

        test_clamp_above() : long {
            return k::math::Math::clampLong(200L, 0L, 100L);
        }

        test_clamp_at_lo() : long {
            return k::math::Math::clampLong(0L, 0L, 100L);
        }

        test_clamp_at_hi() : long {
            return k::math::Math::clampLong(100L, 0L, 100L);
        }
    )SRC");
    REQUIRE(jit);

    auto within = jit->lookup_symbol<long(*)()>("test_clamp_within");
    REQUIRE(within);
    CHECK(within() == 50L);

    auto below = jit->lookup_symbol<long(*)()>("test_clamp_below");
    REQUIRE(below);
    CHECK(below() == 0L);

    auto above = jit->lookup_symbol<long(*)()>("test_clamp_above");
    REQUIRE(above);
    CHECK(above() == 100L);

    auto at_lo = jit->lookup_symbol<long(*)()>("test_clamp_at_lo");
    REQUIRE(at_lo);
    CHECK(at_lo() == 0L);

    auto at_hi = jit->lookup_symbol<long(*)()>("test_clamp_at_hi");
    REQUIRE(at_hi);
    CHECK(at_hi() == 100L);
}


