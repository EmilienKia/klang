/*
 * K Language standard library — Optional<T> tests
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
 * Tests for ::k::Optional<T>.
 *
 * These tests exercise Optional<T> by JIT-compiling small K programs that
 * use the stdlib type.
 *
 * The base standard library (module "k") is implicitly imported by the
 * compiler — no explicit "import k;" is needed in the K sources.
 */

#include <catch2/catch_all.hpp>
#include "helpers.hpp"

#ifndef LIBK_KDI_DIR
#error "LIBK_KDI_DIR must be defined — check CMakeLists.txt"
#endif
#ifndef LIBK_LIB_DIR
#error "LIBK_LIB_DIR must be defined — check CMakeLists.txt"
#endif

namespace {

/// Shorthand: compile K source that uses the base stdlib and JIT it.
std::unique_ptr<k::model::gen::jit> jit_k(std::string_view src) {
    return gen_jit_with_stdlib(src, LIBK_KDI_DIR, LIBK_LIB_DIR);
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════════
//  Default constructor — empty optional
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Optional default constructor is empty", "[libk][optional]") {
    auto j = jit_k(R"SRC(
        module __opt_default__;
        test() : int {
            opt : Optional<int>;
            if (opt.hasValue()) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Value constructor
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Optional value constructor", "[libk][optional]") {
    auto j = jit_k(R"SRC(
        module __opt_value_ctor__;
        test() : int {
            opt : Optional<int>(42);
            if (!opt.hasValue()) return 0;
            if (opt.get() != 42) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Copy constructor — with value
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Optional copy constructor — with value", "[libk][optional][copy]") {
    auto j = jit_k(R"SRC(
        module __opt_copy_value__;
        test() : int {
            src : Optional<int>(99);
            dst : Optional<int>(src);
            if (!dst.hasValue()) return 0;
            if (dst.get() != 99) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Copy constructor — empty
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Optional copy constructor — empty", "[libk][optional][copy]") {
    auto j = jit_k(R"SRC(
        module __opt_copy_empty__;
        test() : int {
            src : Optional<int>;
            dst : Optional<int>(src);
            if (dst.hasValue()) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  set() on empty optional
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Optional set on empty", "[libk][optional]") {
    auto j = jit_k(R"SRC(
        module __opt_set_empty__;
        test() : int {
            opt : Optional<int>;
            opt.set(7);
            if (!opt.hasValue()) return 0;
            if (opt.get() != 7) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  set() replaces existing value
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Optional set replaces existing value", "[libk][optional]") {
    auto j = jit_k(R"SRC(
        module __opt_set_replace__;
        test() : int {
            opt : Optional<int>(10);
            opt.set(20);
            if (!opt.hasValue()) return 0;
            if (opt.get() != 20) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  reset() clears value
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Optional reset clears value", "[libk][optional]") {
    auto j = jit_k(R"SRC(
        module __opt_reset__;
        test() : int {
            opt : Optional<int>(55);
            opt.reset();
            if (opt.hasValue()) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  reset() on empty is safe (no-op)
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Optional reset on empty is safe", "[libk][optional]") {
    auto j = jit_k(R"SRC(
        module __opt_reset_empty__;
        test() : int {
            opt : Optional<int>;
            opt.reset();
            if (opt.hasValue()) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  getOr() with value
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Optional getOr with value", "[libk][optional]") {
    auto j = jit_k(R"SRC(
        module __opt_getor_value__;
        test() : int {
            opt : Optional<int>(33);
            return opt.getOr(0);
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 33);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  getOr() without value
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Optional getOr without value", "[libk][optional]") {
    auto j = jit_k(R"SRC(
        module __opt_getor_empty__;
        test() : int {
            opt : Optional<int>;
            return opt.getOr(77);
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 77);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Optional<T>::empty() static factory
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Optional::empty static factory", "[libk][optional]") {
    auto j = jit_k(R"SRC(
        module __opt_empty_static__;
        test() : int {
            opt : Optional<int> = Optional<int>::empty();
            if (opt.hasValue()) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Struct type in Optional
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Optional with struct type", "[libk][optional]") {
    auto j = jit_k(R"SRC(
        module __opt_struct__;
        struct Point {
            x : int;
            y : int;
            Point(ax : int, ay : int) {
                x = ax;
                y = ay;
            }
        }
        test() : int {
            p : Point(3, 7);
            opt : Optional<Point>(p);
            if (!opt.hasValue()) return 0;
            if (opt.get().x != 3) return 0;
            if (opt.get().y != 7) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  set() then reset() then set() — lifecycle correctness
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Optional set-reset-set lifecycle", "[libk][optional]") {
    auto j = jit_k(R"SRC(
        module __opt_lifecycle__;
        test() : int {
            opt : Optional<int>;
            opt.set(1);
            if (opt.get() != 1) return 0;
            opt.reset();
            if (opt.hasValue()) return 0;
            opt.set(2);
            if (opt.get() != 2) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

