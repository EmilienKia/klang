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
            if (vec.isEmpty())          ++result;
            if (vec.size() == 0)     result += 10;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 11);
}

TEST_CASE("Vector<int> — append and peek", "[libk][vector][int]") {
    auto j = jit_k(R"SRC(
        module __vec_int_append__;

        test() : int {
            vec : Vector<int>;
            a : int = 10;
            b : int = 20;
            c : int = 30;
            vec.append(a);
            vec.append(b);
            vec.append(c);

            result : int = 0;
            if (vec.size() == 3)      ++result;
            if (vec.first() == 10)   result += 10;
            if (vec.last() == 30)    result += 100;
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
            vec.append(a);
            vec.append(b);
            vec.append(c);

            result : int = 0;
            if (vec[0] == 100) ++result;
            if (vec[1] == 200) result += 10;
            if (vec[2] == 300) result += 100;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 111);
}

TEST_CASE("Vector<int> — removeLast", "[libk][vector][int]") {
    auto j = jit_k(R"SRC(
        module __vec_int_rmback__;

        test() : int {
            vec : Vector<int>;
            a : int = 10;
            b : int = 20;
            c : int = 30;
            vec.append(a);
            vec.append(b);
            vec.append(c);
            vec.removeLast();
            // vec: 10, 20

            result : int = 0;
            if (vec.size() == 2)      ++result;
            if (vec.first() == 10)   result += 10;
            if (vec.last() == 20)    result += 100;
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
            vec.append(a);
            vec.append(b);
            vec.append(c);
            vec.removeAt(1);
            // vec: 10, 30

            result : int = 0;
            if (vec.size() == 2)      ++result;
            if (vec[0] == 10)            result += 10;
            if (vec[1] == 30)            result += 100;
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
            vec.append(a);
            vec.append(b);
            vec.insert(1, c);
            // vec: 10, 20, 30

            result : int = 0;
            if (vec.size() == 3)  ++result;
            if (vec[0] == 10)        result += 10;
            if (vec[1] == 20)        result += 100;
            if (vec[2] == 30)        result += 1000;
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
            vec.append(a);
            vec.append(b);
            vec.clear();

            result : int = 0;
            if (vec.isEmpty())           ++result;
            if (vec.size() == 0)      result += 10;
            if (vec.getCapacity() >= 2)  result += 100;
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
            if (vec.getCapacity() >= 100) ++result;
            if (vec.size() == 0)       result += 10;
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
                vec.append(i);
                ++i;
            }

            result : int = 0;
            if (vec.size() == 5)       ++result;
            if (vec.getCapacity() >= 5)   result += 10;
            if (vec[0] == 0)              result += 100;
            if (vec[4] == 4)              result += 1000;
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
            vec.append(p1);
            vec.append(p2);
            vec.append(p3);

            result : int = 0;
            if (vec.size() == 3)  ++result;
            if (vec[0].x == 1)       result += 10;
            if (vec[0].y == 2)       result += 100;
            if (vec[1].x == 3)       result += 1000;
            if (vec[2].y == 6)       result += 10000;
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
            vec.append(p1);
            vec.append(p2);
            vec.append(p3);
            vec.removeAt(0);
            // vec: Point(3,4), Point(5,6)

            result : int = 0;
            if (vec.size() == 2)  ++result;
            if (vec[0].x == 3)       result += 10;
            if (vec[0].y == 4)       result += 100;
            if (vec[1].x == 5)       result += 1000;
            if (vec[1].y == 6)       result += 10000;
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
            vec.append(p1);
            vec.append(p2);
            vec.insert(1, p3);
            // vec: Point(1,2), Point(3,4), Point(5,6)

            result : int = 0;
            if (vec.size() == 3)  ++result;
            if (vec[0].x == 1)       result += 10;
            if (vec[1].x == 3)       result += 100;
            if (vec[1].y == 4)       result += 1000;
            if (vec[2].x == 5)       result += 10000;
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
                vec.append(i);
                ++i;
            }
            // Sum all elements
            sum : int = 0;
            j : int = 0;
            while (j < vec.size()) {
                sum += vec[j];
                ++j;
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

// ═══════════════════════════════════════════════════════════════════════════════
//  3. Enum type — Vector<Color>
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Vector<Color> — append, insert, peek with enum", "[libk][vector][enum]") {
    auto j = jit_k(R"SRC(
        module __vec_enum_full__;

        enum Color {
            RED = 0;
            GREEN = 1;
            BLUE = 2;
        };

        test() : int {
            vec : Vector<Color>;
            r : Color = Color::RED;
            g : Color = Color::GREEN;
            b : Color = Color::BLUE;
            vec.append(r);
            vec.append(b);
            vec.insert(1, g);
            // order: RED, GREEN, BLUE

            result : int = 0;
            if (vec.size() == 3)              ++result;
            if (vec.first() == Color::RED)   result += 10;
            if (vec[1] == Color::GREEN)          result += 100;
            if (vec.last() == Color::BLUE)   result += 1000;

            vec.removeLast();
            if (vec.last() == Color::GREEN)  result += 10000;

            vec.clear();
            if (vec.isEmpty())                   result += 100000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 111111);
}

TEST_CASE("Vector<Direction> — plain enum with auto values", "[libk][vector][enum]") {
    auto j = jit_k(R"SRC(
        module __vec_enum_plain__;

        enum Direction {
            NORTH;
            SOUTH;
            EAST;
            WEST;
        };

        test() : int {
            vec : Vector<Direction>;
            n : Direction = Direction::NORTH;
            s : Direction = Direction::SOUTH;
            e : Direction = Direction::EAST;
            vec.append(n);
            vec.append(e);
            vec.insert(1, s);
            // order: NORTH, SOUTH, EAST

            result : int = 0;
            if (vec.size() == 3)              ++result;
            if (vec[0] == Direction::NORTH)      result += 10;
            if (vec[1] == Direction::SOUTH)      result += 100;
            if (vec[2] == Direction::EAST)       result += 1000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1111);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  4. Typed enum — Vector<TypedEnum>
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Vector<TypedEnum> — typed enum (byte)", "[libk][vector][enum]") {
    auto j = jit_k(R"SRC(
        module __vec_tenum__;

        enum Status : byte {
            OK = 0;
            WARN = 1;
            ERR = 2;
        };

        test() : int {
            vec : Vector<Status>;
            o : Status = Status::OK;
            w : Status = Status::WARN;
            e : Status = Status::ERR;
            vec.append(o);
            vec.append(e);
            vec.insert(1, w);
            // order: OK, WARN, ERR

            result : int = 0;
            if (vec.size() == 3)          ++result;
            if (vec[0] == Status::OK)        result += 10;
            if (vec[1] == Status::WARN)      result += 100;
            if (vec[2] == Status::ERR)       result += 1000;

            vec.removeAt(1);
            // order: OK, ERR
            if (vec[1] == Status::ERR)       result += 10000;
            if (vec.size() == 2)          result += 100000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 111111);
}

TEST_CASE("Vector<Derived> — derived enum", "[libk][vector][enum]") {
    auto j = jit_k(R"SRC(
        module __vec_derived_enum__;

        enum Base {
            A = 0;
            B = 1;
        };
        enum Derived : Base {
            C = 2;
        };

        test() : int {
            vec : Vector<Derived>;
            a : Derived = Derived::A;
            b : Derived = Derived::B;
            c : Derived = Derived::C;
            vec.append(a);
            vec.append(c);
            vec.insert(1, b);
            // order: A(0), B(1), C(2)

            result : int = 0;
            if (vec.size() == 3)        ++result;
            if (vec[0] == Derived::A)      result += 10;
            if (vec[1] == Derived::B)      result += 100;
            if (vec[2] == Derived::C)      result += 1000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1111);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  5. Emplace operations — Vector<T>
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Vector<int> — emplaceBack zero-arg", "[libk][vector][emplace]") {
    auto j = jit_k(R"SRC(
        module __vec_emplace_int0__;

        test() : int {
            vec : Vector<int>;
            vec.emplaceBack();
            vec.emplaceBack();
            vec.emplaceBack();

            result : int = 0;
            if (vec.size() == 3)  ++result;
            // Default-constructed ints are 0 (zero-initialized by MultiSlot)
            if (vec[0] == 0)         result += 10;
            if (vec[1] == 0)         result += 100;
            if (vec[2] == 0)         result += 1000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1111);
}

TEST_CASE("Vector<Point> — emplaceBack with constructor args", "[libk][vector][emplace]") {
    auto j = jit_k(R"SRC(
        module __vec_emplace_pt__;

        struct Point {
            x : int;
            y : int;
            Point() { x = 0; y = 0; }
            Point(ax : int, ay : int) { x = ax; y = ay; }
        }

        test() : int {
            vec : Vector<Point>;
            vec.emplaceBack(10, 20);
            vec.emplaceBack(30, 40);
            vec.emplaceBack(50, 60);

            result : int = 0;
            if (vec.size() == 3)  ++result;
            if (vec[0].x == 10)      result += 10;
            if (vec[0].y == 20)      result += 100;
            if (vec[1].x == 30)      result += 1000;
            if (vec[2].y == 60)      result += 10000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 11111);
}

TEST_CASE("Vector<Point> — emplaceBack zero-arg (default ctor)", "[libk][vector][emplace]") {
    auto j = jit_k(R"SRC(
        module __vec_emplace_pt0__;

        struct Point {
            x : int;
            y : int;
            Point() { x = 99; y = 77; }
        }

        test() : int {
            vec : Vector<Point>;
            vec.emplaceBack();
            vec.emplaceBack();

            result : int = 0;
            if (vec.size() == 2)  ++result;
            if (vec[0].x == 99)      result += 10;
            if (vec[0].y == 77)      result += 100;
            if (vec[1].x == 99)      result += 1000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1111);
}

TEST_CASE("Vector<int> — emplace at index zero-arg", "[libk][vector][emplace]") {
    auto j = jit_k(R"SRC(
        module __vec_emplace_idx__;

        test() : int {
            vec : Vector<int>;
            a : int = 10;
            b : int = 30;
            vec.append(a);
            vec.append(b);
            // Emplace at index 1 — default int (0)
            vec.emplace(1);
            // vec: 10, 0, 30

            result : int = 0;
            if (vec.size() == 3)  ++result;
            if (vec[0] == 10)        result += 10;
            if (vec[1] == 0)         result += 100;
            if (vec[2] == 30)        result += 1000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1111);
}

TEST_CASE("Vector<Point> — emplace at index with args", "[libk][vector][emplace]") {
    auto j = jit_k(R"SRC(
        module __vec_emplace_pt_idx__;

        struct Point {
            x : int;
            y : int;
            Point() { x = 0; y = 0; }
            Point(ax : int, ay : int) { x = ax; y = ay; }
        }

        test() : int {
            vec : Vector<Point>;
            vec.emplaceBack(1, 2);
            vec.emplaceBack(5, 6);
            vec.emplace(1, 3, 4);
            // vec: Point(1,2), Point(3,4), Point(5,6)

            result : int = 0;
            if (vec.size() == 3)  ++result;
            if (vec[0].x == 1)       result += 10;
            if (vec[1].x == 3)       result += 100;
            if (vec[1].y == 4)       result += 1000;
            if (vec[2].x == 5)       result += 10000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 11111);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  6. Aggregate emplace — Vector<T> with structs without explicit constructors
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Vector<AggPoint> — emplaceBack aggregate init", "[libk][vector][emplace][aggregate]") {
    auto j = jit_k(R"SRC(
        module __vec_emplace_agg__;

        struct AggPoint {
            x : int;
            y : int;
        }

        test() : int {
            vec : Vector<AggPoint>;
            vec.emplaceBack(10, 20);
            vec.emplaceBack(30, 40);
            vec.emplaceBack(50, 60);

            result : int = 0;
            if (vec.size() == 3)  ++result;
            if (vec[0].x == 10)      result += 10;
            if (vec[0].y == 20)      result += 100;
            if (vec[1].x == 30)      result += 1000;
            if (vec[2].y == 60)      result += 10000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 11111);
}

TEST_CASE("Vector<AggPoint> — emplace at index aggregate init", "[libk][vector][emplace][aggregate]") {
    auto j = jit_k(R"SRC(
        module __vec_emplace_agg_idx__;

        struct AggPoint {
            x : int;
            y : int;
        }

        test() : int {
            vec : Vector<AggPoint>;
            vec.emplaceBack(1, 2);
            vec.emplaceBack(5, 6);
            vec.emplace(1, 3, 4);
            // vec: AggPoint{1,2}, AggPoint{3,4}, AggPoint{5,6}

            result : int = 0;
            if (vec.size() == 3)  ++result;
            if (vec[0].x == 1)       result += 10;
            if (vec[1].x == 3)       result += 100;
            if (vec[1].y == 4)       result += 1000;
            if (vec[2].x == 5)       result += 10000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 11111);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  7. Collection<T> interface — polymorphism
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Collection<int> — Vector through interface reference", "[libk][vector][collection]") {
    auto j = jit_k(R"SRC(
        module __vec_coll_iface__;

        sizeOf(coll : Vector<int>&) : unsigned int {
            return coll.size();
        }

        isNotEmpty(coll : Vector<int>&) : bool {
            return coll.isEmpty() == false;
        }

        sumFirstAndLast(coll : Vector<int>&) : int {
            sz : unsigned int = coll.size();
            if (sz == 0) return 0;
            if (sz == 1) return coll[0];
            lastIndex : unsigned int = sz - 1;
            return coll[0] + coll[lastIndex];
        }

        fill(coll : Vector<int>&) {
            a : int = 10;
            b : int = 20;
            c : int = 30;
            coll.append(a);
            coll.append(b);
            coll.append(c);
        }

        test() : int {
            vec : Vector<int>;
            fill(vec);
            total : int = sumFirstAndLast(vec);

            result : int = 0;
            if (sizeOf(vec) == 3) ++result;
            if (total == 40)         result += 10;
            if (isNotEmpty(vec))      result += 100;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 111);
}

TEST_CASE("Collection<int> — LinkedList through interface reference", "[libk][list][collection]") {
    auto j = jit_k(R"SRC(
        module __ll_coll_iface__;

        test() : int {
            lst : LinkedList<int>;
            a : int = 1;
            b : int = 2;
            lst.append(a);
            lst.append(b);

            result : int = 0;
            if (lst.size() == 2)  ++result;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  8. Value semantics of an owning aggregate — Vector<int>
//
//  These tests guard the value-semantics wiring for owning aggregates (a Vector
//  owns a heap buffer): a prvalue Vector returned or passed by value must be
//  MOVED into its single destination (its buffer transferred, the source's
//  scheduled destruction cancelled), never shallow-copied and then freed twice.
//  A regression to a shallow byte copy would alias the heap buffer and cause a
//  double free / use-after-free; here it would manifest as corrupted element
//  reads (wrong sum) or a crash.  Promoted from the /tmp `vbyval.k` / `vsret.k`
//  repros and strengthened from a trivial counter struct to a real Vector<int>.
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Vector<int> — prvalue returned by value is moved (buffer intact)",
          "[libk][vector][value-semantics]") {
    auto j = jit_k(R"SRC(
        module __vec_ret_move__;

        makeVec(n: int) : Vector<int> {
            v : Vector<int>;
            i : int = 0;
            while (i < n) {
                v.append(i * 10);
                ++i;
            }
            return v;
        }

        test() : int {
            r : Vector<int> = makeVec(5);   // return-by-value: temporary moved into r
            total : int = 0;
            i : unsigned int = 0;
            while (i < r.size()) {
                total += r[i];        // 0+10+20+30+40 = 100 (buffer survived)
                ++i;
            }
            return total;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 100);
}

TEST_CASE("Vector<int> — prvalue passed by value is moved (buffer intact)",
          "[libk][vector][value-semantics]") {
    auto j = jit_k(R"SRC(
        module __vec_byval_move__;

        makeVec(n: int) : Vector<int> {
            v : Vector<int>;
            i : int = 0;
            while (i < n) {
                v.append(i * 10);
                ++i;
            }
            return v;
        }

        sumByValue(v: Vector<int>) : int {
            s : int = 0;
            i : unsigned int = 0;
            while (i < v.size()) {
                s += v[i];
                ++i;
            }
            return s;
        }

        test() : int {
            // The prvalue produced by makeVec(5) is moved into the by-value
            // parameter of sumByValue: a single owner, no double free.
            return sumByValue(makeVec(5));   // 0+10+20+30+40 = 100
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 100);
}

TEST_CASE("Vector<int> — prvalue returned by value, e2e build and exec",
          "[libk][vector][value-semantics][run]") {
    auto result = build_and_exec(R"SRC(
        module __vec_ret_move_e2e__;

        makeVec(n: int) : Vector<int> {
            v : Vector<int>;
            i : int = 0;
            while (i < n) {
                v.append(i);
                ++i;
            }
            return v;
        }

        main() : int {
            r : Vector<int> = makeVec(10);
            sum : int = 0;
            j : unsigned int = 0;
            while (j < r.size()) {
                sum += r[j];
                ++j;
            }
            // 0+1+...+9 = 45; a shallow-copy double free would crash on scope exit.
            if (sum == 45) return 0;
            return 1;
        }
    )SRC");
    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE(result.exit_code == 0);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  9. Abstract interface polymorphism — diamond dispatch through the full
//     Collection/Sequence hierarchy
//
//  Vector<T> implements MutableIndexedCollection<T> AND (since the fix for the
//  multi-layer template/diamond-interface compiler bug — see AGENTS.md history)
//  MutableReversibleSequence<T> as well. Both interfaces converge back onto
//  MutableSequence<T> (via MutableIndexedCollection<T>→MutableCollection<T>→
//  MutableSequence<T> on one side, and directly on the other), forming a real
//  diamond in the vtable/base-subobject layout. These tests exercise every
//  interface reference type in the hierarchy — not just the concrete Vector<T>
//  type used by the existing "Collection<int> — Vector through interface
//  reference" test above — to guard against regressions in base-subobject GEP
//  offsets / vtable thunk generation for repeated/diamond interface bases.
// ═══════════════════════════════════════════════════════════════════════════════
TEST_CASE("Vector<int> — through Sequence<int>& (const iteration)", "[libk][vector][interface][sequence]") {
    auto j = jit_k(R"SRC(
        module __vec_iface_sequence__;

        sumAll(seq : Sequence<int>&) : int {
            it : ConstIterator<int>! = seq.constIterator();
            total : int = 0;
            n : OptionalConstRef<int> = it.next();
            while (n.hasValue()) {
                total += n.get();
                n = it.next();
            }
            return total;
        }

        test() : int {
            vec : Vector<int>;
            vec.append(1);
            vec.append(2);
            vec.append(3);
            return sumAll(vec);
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 6);
}

TEST_CASE("Vector<int> — through MutableSequence<int>& (mutating iteration)", "[libk][vector][interface][sequence]") {
    auto j = jit_k(R"SRC(
        module __vec_iface_mutseq__;

        doubleAll(seq : MutableSequence<int>&) {
            it : Iterator<int>! = seq.iterator();
            n : OptionalRef<int> = it.next();
            while (n.hasValue()) {
                n.get() = n.get() * 2;
                n = it.next();
            }
        }

        test() : int {
            vec : Vector<int>;
            vec.append(1);
            vec.append(2);
            vec.append(3);
            doubleAll(vec);
            return vec[0] + vec[1] + vec[2];
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 12);
}

TEST_CASE("Vector<int> — through ReversibleSequence<int>& (const reverse iteration)", "[libk][vector][interface][reversible]") {
    auto j = jit_k(R"SRC(
        module __vec_iface_reversible__;

        firstFromEnd(seq : ReversibleSequence<int>&) : int {
            it : ConstIterator<int>! = seq.constReverseIterator();
            n : OptionalConstRef<int> = it.next();
            if (n.hasValue()) return n.get();
            return -1;
        }

        test() : int {
            vec : Vector<int>;
            vec.append(10);
            vec.append(20);
            vec.append(30);
            return firstFromEnd(vec);
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 30);
}

TEST_CASE("Vector<int> — through MutableReversibleSequence<int>& (mutating reverse iteration)",
          "[libk][vector][interface][reversible]") {
    auto j = jit_k(R"SRC(
        module __vec_iface_mutreversible__;

        sumReverseAndZeroLast(seq : MutableReversibleSequence<int>&) : int {
            it : Iterator<int>! = seq.reverseIterator();
            total : int = 0;
            first : bool = true;
            n : OptionalRef<int> = it.next();
            while (n.hasValue()) {
                total += n.get();
                if (first) {
                    n.get() = 0;
                    first = false;
                }
                n = it.next();
            }
            return total;
        }

        test() : int {
            vec : Vector<int>;
            vec.append(1);
            vec.append(2);
            vec.append(3);
            total : int = sumReverseAndZeroLast(vec);
            result : int = 0;
            if (total == 6)       ++result;
            if (vec[2] == 0)      result += 10; // last element zeroed via reverse iterator
            if (vec[0] == 1)      result += 100;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 111);
}

TEST_CASE("Vector<int> — through IndexedCollection<int>& (indexed access)", "[libk][vector][interface][indexed]") {
    auto j = jit_k(R"SRC(
        module __vec_iface_indexed__;

        sumFirstAndLast(coll : IndexedCollection<int>&) : int {
            sz : unsigned int = coll.size();
            if (sz == 0) return 0;
            return coll.get(0) + coll.get(sz - 1);
        }

        test() : int {
            vec : Vector<int>;
            vec.append(5);
            vec.append(6);
            vec.append(7);
            return sumFirstAndLast(vec);
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 12);
}

TEST_CASE("Vector<int> — through MutableIndexedCollection<int>& (set/insert/removeAt)",
          "[libk][vector][interface][indexed]") {
    auto j = jit_k(R"SRC(
        module __vec_iface_mutindexed__;

        mutate(coll : MutableIndexedCollection<int>&) {
            coll.insert(1, 99);
            coll.set(0, 42);
            coll.removeAt(2);
        }

        test() : int {
            vec : Vector<int>;
            vec.append(1);
            vec.append(2);
            vec.append(3);
            mutate(vec);
            // Started [1,2,3]; insert(1,99) -> [1,99,2,3]; set(0,42) -> [42,99,2,3];
            // removeAt(2) removes '2' -> [42,99,3].
            result : int = 0;
            if (vec.size() == 3)  ++result;
            if (vec[0] == 42)     result += 10;
            if (vec[1] == 99)     result += 100;
            if (vec[2] == 3)      result += 1000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1111);
}

TEST_CASE("Vector<int> — diamond dispatch: same instance through two independent interface paths",
          "[libk][vector][interface][diamond]") {
    // This is the direct regression test for the multi-layer template/diamond-
    // interface compiler bug: Vector<T> reaches MutableSequence<T> through TWO
    // distinct paths — MutableIndexedCollection<T>→MutableCollection<T>→
    // MutableSequence<T>, and directly via MutableReversibleSequence<T>→
    // MutableSequence<T>. Exercising both a MutableIndexedCollection<int>& and a
    // MutableReversibleSequence<int>& reference to the SAME Vector instance in one
    // function call catches wrong base-subobject GEP offsets / vtable thunks that
    // only manifest when both diamond paths are used together (see the getCode()
    // upcast regression fixed alongside this).
    auto j = jit_k(R"SRC(
        module __vec_iface_diamond__;

        viaIndexed(coll : MutableIndexedCollection<int>&) : int {
            return coll.get(0) + coll.get(coll.size() - 1);
        }

        viaReversible(seq : MutableReversibleSequence<int>&) : int {
            it : Iterator<int>! = seq.reverseIterator();
            n : OptionalRef<int> = it.next();
            total : int = 0;
            while (n.hasValue()) {
                total += n.get();
                n = it.next();
            }
            return total;
        }

        test() : int {
            vec : Vector<int>;
            vec.append(1);
            vec.append(2);
            vec.append(3);

            a : int = viaIndexed(vec);     // 1 + 3 = 4
            b : int = viaReversible(vec);  // 1 + 2 + 3 = 6

            result : int = 0;
            if (a == 4) ++result;
            if (b == 6) result += 10;
            if (vec.size() == 3) result += 100;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 111);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  8 (continued): lvalue copy paths — Sites 2, 3, 4
//
//  A non-trivial lvalue Vector<int> (owns a heap buffer) initialised, passed, or
//  returned by value must invoke the copy constructor so the two copies hold
//  independent buffers.  Without the fix, a shallow byte-copy aliases the heap
//  buffer: mutating one copy corrupts the other, and the double free crashes on
//  scope exit.
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Vector<int> — lvalue init deep copies buffer (site 2)",
          "[libk][vector][value-semantics]") {
    auto j = jit_k(R"SRC(
        module __vec_lval_init__;

        test() : int {
            v1 : Vector<int>;
            v1.append(1);
            v1.append(2);
            v1.append(3);

            v2 : Vector<int> = v1;    // lvalue copy — must be deep
            v2.append(99);            // mutate copy; must NOT affect v1

            // v1 must still have exactly [1, 2, 3]
            ok : int = 1;
            if (v1.size() != 3)          ok = 0;
            if (v1[0] != 1)              ok = 0;
            if (v1[1] != 2)              ok = 0;
            if (v1[2] != 3)              ok = 0;
            // v2 must have [1, 2, 3, 99]
            if (v2.size() != 4)          ok = 0;
            if (v2[3] != 99)             ok = 0;
            return ok;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

TEST_CASE("Vector<int> — lvalue passed by value deep copies buffer (site 3)",
          "[libk][vector][value-semantics]") {
    auto j = jit_k(R"SRC(
        module __vec_lval_byval__;

        // Mutates the local copy; caller's original must be unaffected.
        mutAndSum(v: Vector<int>) : int {
            v.append(99);
            s : int = 0;
            i : unsigned int = 0;
            while (i < v.size()) {
                s += v[i];
                ++i;
            }
            return s;    // 1+2+3+99 = 105
        }

        test() : int {
            orig : Vector<int>;
            orig.append(1);
            orig.append(2);
            orig.append(3);

            r : int = mutAndSum(orig);   // lvalue by-value — must copy

            ok : int = 1;
            if (r != 105)          ok = 0;   // callee got a full copy
            if (orig.size() != 3)  ok = 0;   // original unchanged
            if (orig[0] != 1)      ok = 0;
            if (orig[1] != 2)      ok = 0;
            if (orig[2] != 3)      ok = 0;
            return ok;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

TEST_CASE("Vector<int> — lvalue returned by value deep copies buffer (site 4)",
          "[libk][vector][value-semantics]") {
    auto j = jit_k(R"SRC(
        module __vec_lval_retval__;

        // Returns one of two local vectors depending on flag — neither is the
        // NRVO candidate, so the compiler must copy into the caller's sret slot.
        pick(flag: int) : Vector<int> {
            a : Vector<int>;
            a.append(10);
            a.append(20);
            b : Vector<int>;
            b.append(100);
            b.append(200);
            if (flag != 0) {
                return a;   // non-NRVO copy of 'a'
            }
            return b;       // non-NRVO copy of 'b'
        }

        test() : int {
            r : Vector<int> = pick(1);  // should deep-copy 'a'
            ok : int = 1;
            if (r.size() != 2)  ok = 0;
            if (r[0] != 10)     ok = 0;
            if (r[1] != 20)     ok = 0;
            return ok;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

TEST_CASE("Vector<int> — lvalue copy deep copy e2e build and exec (site 2-3-4)",
          "[libk][vector][value-semantics][run]") {
    auto result = build_and_exec(R"SRC(
        module __vec_lval_copy_e2e__;

        mutAndSum(v: Vector<int>) : int {
            v.append(999);
            s : int = 0;
            i : unsigned int = 0;
            while (i < v.size()) { s += v[i]; ++i; }
            return s;
        }

        main() : int {
            orig : Vector<int>;
            orig.append(1);
            orig.append(2);
            orig.append(3);

            copy : Vector<int> = orig;  // site 2
            r : int = mutAndSum(orig);  // site 3 — pass orig by value

            // orig must be untouched (size==3), copy must be independent (size==3)
            if (orig.size() != 3) return 1;
            if (copy.size() != 3) return 2;
            if (r != 1005) return 3;    // 1+2+3+999
            return 0;
        }
    )SRC");
    REQUIRE(result.exit_code == 0);
}
