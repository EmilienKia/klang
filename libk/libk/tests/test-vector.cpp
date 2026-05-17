/*
 * K Language standard library — Vector tests
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

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

#ifndef LIBK_KDI_DIR
#error "LIBK_KDI_DIR must be defined — check CMakeLists.txt"
#endif
#ifndef LIBK_LIB_DIR
#error "LIBK_LIB_DIR must be defined — check CMakeLists.txt"
#endif

namespace {

std::unique_ptr<k::model::gen::jit> jit_k(std::string_view src) {
    return gen_jit_with_stdlib(src, LIBK_KDI_DIR, LIBK_LIB_DIR);
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════════
//  1. Primitive type — Vector<int>
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Vector<int> — empty vector", "[libk][vector][int]") {
    auto j = jit_k(R"SRC(
        module __vec_empty__;

        test() : int {
            vec : Vector<int>;
            result : int = 0;
            if (vec.isEmpty())          result = result + 1;
            if (vec.getSize() == 0)     result = result + 10;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 11);
}

TEST_CASE("Vector<int> — pushBack and peek", "[libk][vector][int]") {
    auto j = jit_k(R"SRC(
        module __vec_int_push__;

        test() : int {
            vec : Vector<int>;
            a : int = 10;
            b : int = 20;
            c : int = 30;
            vec.pushBack(a);
            vec.pushBack(b);
            vec.pushBack(c);

            result : int = 0;
            if (vec.getSize() == 3)      result = result + 1;
            if (vec.peekFront() == 10)   result = result + 10;
            if (vec.peekBack() == 30)    result = result + 100;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 111);
}

TEST_CASE("Vector<int> — subscript operator", "[libk][vector][int]") {
    auto j = jit_k(R"SRC(
        module __vec_int_subscript__;

        test() : int {
            vec : Vector<int>;
            a : int = 100;
            b : int = 200;
            c : int = 300;
            vec.pushBack(a);
            vec.pushBack(b);
            vec.pushBack(c);

            result : int = 0;
            if (vec[0] == 100) result = result + 1;
            if (vec[1] == 200) result = result + 10;
            if (vec[2] == 300) result = result + 100;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 111);
}

TEST_CASE("Vector<int> — removeBack", "[libk][vector][int]") {
    auto j = jit_k(R"SRC(
        module __vec_int_rmback__;

        test() : int {
            vec : Vector<int>;
            a : int = 10;
            b : int = 20;
            c : int = 30;
            vec.pushBack(a);
            vec.pushBack(b);
            vec.pushBack(c);
            vec.removeBack();
            // vec: 10, 20

            result : int = 0;
            if (vec.getSize() == 2)      result = result + 1;
            if (vec.peekFront() == 10)   result = result + 10;
            if (vec.peekBack() == 20)    result = result + 100;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 111);
}

TEST_CASE("Vector<int> — removeAt", "[libk][vector][int]") {
    auto j = jit_k(R"SRC(
        module __vec_int_rmat__;

        test() : int {
            vec : Vector<int>;
            a : int = 10;
            b : int = 20;
            c : int = 30;
            vec.pushBack(a);
            vec.pushBack(b);
            vec.pushBack(c);
            vec.removeAt(1);
            // vec: 10, 30

            result : int = 0;
            if (vec.getSize() == 2)      result = result + 1;
            if (vec[0] == 10)            result = result + 10;
            if (vec[1] == 30)            result = result + 100;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 111);
}

TEST_CASE("Vector<int> — insert at index", "[libk][vector][int]") {
    auto j = jit_k(R"SRC(
        module __vec_int_insert__;

        test() : int {
            vec : Vector<int>;
            a : int = 10;
            b : int = 30;
            c : int = 20;
            vec.pushBack(a);
            vec.pushBack(b);
            vec.insert(1, c);
            // vec: 10, 20, 30

            result : int = 0;
            if (vec.getSize() == 3)  result = result + 1;
            if (vec[0] == 10)        result = result + 10;
            if (vec[1] == 20)        result = result + 100;
            if (vec[2] == 30)        result = result + 1000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1111);
}

TEST_CASE("Vector<int> — clear empties", "[libk][vector][int]") {
    auto j = jit_k(R"SRC(
        module __vec_int_clear__;

        test() : int {
            vec : Vector<int>;
            a : int = 1;
            b : int = 2;
            vec.pushBack(a);
            vec.pushBack(b);
            vec.clear();

            result : int = 0;
            if (vec.isEmpty())           result = result + 1;
            if (vec.getSize() == 0)      result = result + 10;
            if (vec.getCapacity() >= 2)  result = result + 100;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 111);
}

TEST_CASE("Vector<int> — reserve", "[libk][vector][int]") {
    auto j = jit_k(R"SRC(
        module __vec_int_reserve__;

        test() : int {
            vec : Vector<int>;
            vec.reserve(100);

            result : int = 0;
            if (vec.getCapacity() >= 100) result = result + 1;
            if (vec.getSize() == 0)       result = result + 10;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 11);
}

TEST_CASE("Vector<int> — growth strategy", "[libk][vector][int]") {
    auto j = jit_k(R"SRC(
        module __vec_int_growth__;

        test() : int {
            vec : Vector<int>;
            // Push 5 elements — should trigger at least one growth
            i : int = 0;
            while (i < 5) {
                vec.pushBack(i);
                i = i + 1;
            }

            result : int = 0;
            if (vec.getSize() == 5)       result = result + 1;
            if (vec.getCapacity() >= 5)   result = result + 10;
            if (vec[0] == 0)              result = result + 100;
            if (vec[4] == 4)              result = result + 1000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1111);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  2. Aggregate type — Vector<Point>
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Vector<Point> — struct stored by value", "[libk][vector][struct]") {
    auto j = jit_k(R"SRC(
        module __vec_pt_push__;

        struct Point {
            x : int;
            y : int;
            Point() { x = 0; y = 0; }
            Point(ax : int, ay : int) { x = ax; y = ay; }
        }

        test() : int {
            vec : Vector<Point>;
            p1 : Point(1, 2);
            p2 : Point(3, 4);
            p3 : Point(5, 6);
            vec.pushBack(p1);
            vec.pushBack(p2);
            vec.pushBack(p3);

            result : int = 0;
            if (vec.getSize() == 3)  result = result + 1;
            if (vec[0].x == 1)       result = result + 10;
            if (vec[0].y == 2)       result = result + 100;
            if (vec[1].x == 3)       result = result + 1000;
            if (vec[2].y == 6)       result = result + 10000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 11111);
}

TEST_CASE("Vector<Point> — removeAt with struct", "[libk][vector][struct]") {
    auto j = jit_k(R"SRC(
        module __vec_pt_rmat__;

        struct Point {
            x : int;
            y : int;
            Point() { x = 0; y = 0; }
            Point(ax : int, ay : int) { x = ax; y = ay; }
        }

        test() : int {
            vec : Vector<Point>;
            p1 : Point(1, 2);
            p2 : Point(3, 4);
            p3 : Point(5, 6);
            vec.pushBack(p1);
            vec.pushBack(p2);
            vec.pushBack(p3);
            vec.removeAt(0);
            // vec: Point(3,4), Point(5,6)

            result : int = 0;
            if (vec.getSize() == 2)  result = result + 1;
            if (vec[0].x == 3)       result = result + 10;
            if (vec[0].y == 4)       result = result + 100;
            if (vec[1].x == 5)       result = result + 1000;
            if (vec[1].y == 6)       result = result + 10000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 11111);
}

TEST_CASE("Vector<Point> — insert with struct", "[libk][vector][struct]") {
    auto j = jit_k(R"SRC(
        module __vec_pt_insert__;

        struct Point {
            x : int;
            y : int;
            Point() { x = 0; y = 0; }
            Point(ax : int, ay : int) { x = ax; y = ay; }
        }

        test() : int {
            vec : Vector<Point>;
            p1 : Point(1, 2);
            p2 : Point(5, 6);
            p3 : Point(3, 4);
            vec.pushBack(p1);
            vec.pushBack(p2);
            vec.insert(1, p3);
            // vec: Point(1,2), Point(3,4), Point(5,6)

            result : int = 0;
            if (vec.getSize() == 3)  result = result + 1;
            if (vec[0].x == 1)       result = result + 10;
            if (vec[1].x == 3)       result = result + 100;
            if (vec[1].y == 4)       result = result + 1000;
            if (vec[2].x == 5)       result = result + 10000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 11111);
}

TEST_CASE("Vector<int> — e2e build and exec", "[libk][vector][run]") {
    auto result = build_and_exec(R"SRC(
        module __vec_e2e__;
        main() : int {
            vec : Vector<int>;
            i : int = 0;
            while (i < 10) {
                vec.pushBack(i);
                i = i + 1;
            }
            // Sum all elements
            sum : int = 0;
            j : int = 0;
            while (j < vec.getSize()) {
                sum = sum + vec[j];
                j = j + 1;
            }
            // 0+1+2+...+9 = 45
            if (sum == 45) return 0;
            return 1;
        }
    )SRC");
    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE(result.exit_code == 0);
}

