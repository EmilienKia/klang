/*
 * K Language standard library — Shared<T> tests
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
 * Tests for ::k::Shared<T>.
 *
 * These tests exercise the behaviour of the libk Shared smart pointer
 * by JIT-compiling small K programs that use the stdlib type.
 *
 * The base standard library (module "k") is implicitly imported by the
 * compiler — no explicit "import k;" is needed in the K sources.
 *
 * Shared<T> is a template struct, so each test instantiates it with a
 * concrete class type (IntBox or Tracker).
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
//  1. Multislot
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Multislot<int> — drain constructor", "[libk][multislot][drain]") {
    auto j = jit_k(R"SRC(
        module __multislot_int_drain_construct__;
        test() : int {

            slots : MultiSlot<int>;
            slots.allocate(4);

            slots.get(0) = 10;
            slots.get(1) = 20;
            slots.get(2) = 30;
            slots.get(3) = 40;

            slots2 : MultiSlot<int>(#slots);

            result : int = slots.getCapacity() * 2 + slots2.getCapacity();
            result += slots2.get(0) + slots2.get(1) + slots2.get(2) + slots2.get(3);

            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == (0*2 + 4 + 10 + 20 + 30 + 40));
}


TEST_CASE("Multislot<int> — drain assignment", "[libk][multislot][drain]") {
    auto j = jit_k(R"SRC(
        module __multislot_int_drain_assign__;
        test() : int {

            slots : MultiSlot<int>;
            slots2 : MultiSlot<int>;
            slots.allocate(4);

            slots.get(0) = 10;
            slots.get(1) = 20;
            slots.get(2) = 30;
            slots.get(3) = 40;

            slots2 = #slots;

            result : int = slots.getCapacity() * 2 + slots2.getCapacity();
            result += slots2.get(0) + slots2.get(1) + slots2.get(2) + slots2.get(3);

            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == (0*2 + 4 + 10 + 20 + 30 + 40));
}


TEST_CASE("Multislot<Point> — drain constructor", "[libk][multislot][drain]") {
    auto j = jit_k(R"SRC(
        module __multislot_point_drain_construct__;

        struct Point {
            x : int;
            y : int;
            Point() { x = 0; y = 0; }
            Point(ax : int, ay : int) { x = ax; y = ay; }
        }

        test() : int {

            slots : MultiSlot<Point>;
            slots.allocate(4);

            slots.construct(0, 10, 60);
            slots.construct(1, 20, 70);
            slots.construct(2, 30, 80);
            slots.construct(3, 40, 90);

            slots2 : MultiSlot<Point>(#slots);

            result : int = slots.getCapacity() * 2 + slots2.getCapacity();
            result += slots2.get(0).x + slots2.get(1).x + slots2.get(2).x + slots2.get(3).x;

            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == (0*2 + 4 + 10 + 20 + 30 + 40));
}


TEST_CASE("Multislot<Point> — drain assignment", "[libk][multislot][drain]") {
    auto j = jit_k(R"SRC(
        module __multislot_int_drain_assign__;

        struct Point {
            x : int;
            y : int;
            Point() { x = 0; y = 0; }
            Point(ax : int, ay : int) { x = ax; y = ay; }
        }

        test() : int {

            slots : MultiSlot<Point>;
            slots2 : MultiSlot<Point>;
            slots.allocate(4);

            slots.get(0) = Point(10, 60);
            slots.get(1) = Point(20, 70);
            slots.get(2) = Point(30, 80);
            slots.get(3) = Point(40, 90);

            slots2 = #slots;

            result : int = slots.getCapacity() * 2 + slots2.getCapacity();
            result += slots2.get(0).x + slots2.get(1).x + slots2.get(2).x + slots2.get(3).x;

            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == (0*2 + 4 + 10 + 20 + 30 + 40));
}

