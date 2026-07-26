/*
 * K Language standard library — LinkedList tests
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
//  1. Primitive type — LinkedList<int>
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("LinkedList<int> — empty list", "[libk][list][int]") {
    auto result = build_and_exec(R"SRC(
        module __ll_e2e_empty__;
        main() : int {
            lst : LinkedList<int>;
            if (lst.isEmpty()) return 1;
            return 0;
        }
    )SRC");
    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE(result.exit_code == 1);
}

TEST_CASE("LinkedList<int> — pushBack and peek", "[libk][list][int]") {
    auto j = jit_k(R"SRC(
        module __ll_int_push__;

        test() : int {
            lst : LinkedList<int>;
            a : int = 10;
            b : int = 20;
            c : int = 30;
            lst.append(a);
            lst.append(b);
            lst.append(c);

            result : int = 0;
            if (lst.size() == 3)      result = result + 1;
            if (lst.first() == 10)   result = result + 10;
            if (lst.last() == 30)    result = result + 100;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 111);
}

TEST_CASE("LinkedList<int> — pushFront ordering", "[libk][list][int]") {
    auto j = jit_k(R"SRC(
        module __ll_int_front__;

        test() : int {
            lst : LinkedList<int>;
            a : int = 1;
            b : int = 2;
            c : int = 3;
            lst.prepend(a);
            lst.prepend(b);
            lst.prepend(c);
            // order: 3, 2, 1

            result : int = 0;
            if (lst.first() == 3)  result = result + 1;
            if (lst.last() == 1)   result = result + 10;
            if (lst[1] == 2)           result = result + 100;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 111);
}

TEST_CASE("LinkedList<int> — removeFront", "[libk][list][int]") {
    auto j = jit_k(R"SRC(
        module __ll_int_rmfront__;

        test() : int {
            lst : LinkedList<int>;
            a : int = 10;
            b : int = 20;
            c : int = 30;
            lst.append(a);
            lst.append(b);
            lst.append(c);
            lst.removeFront();
            // list: 20, 30

            result : int = 0;
            if (lst.size() == 2)      result = result + 1;
            if (lst.first() == 20)   result = result + 10;
            if (lst.last() == 30)    result = result + 100;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 111);
}

TEST_CASE("LinkedList<int> — removeBack", "[libk][list][int]") {
    auto j = jit_k(R"SRC(
        module __ll_int_rmback__;

        test() : int {
            lst : LinkedList<int>;
            a : int = 10;
            b : int = 20;
            c : int = 30;
            lst.append(a);
            lst.append(b);
            lst.append(c);
            lst.removeBack();
            // list: 10, 20

            result : int = 0;
            if (lst.size() == 2)      result = result + 1;
            if (lst.first() == 10)   result = result + 10;
            if (lst.last() == 20)    result = result + 100;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 111);
}

TEST_CASE("LinkedList<int> — clear empties", "[libk][list][int]") {
    auto j = jit_k(R"SRC(
        module __ll_int_clear__;

        test() : int {
            lst : LinkedList<int>;
            a : int = 1;
            b : int = 2;
            lst.append(a);
            lst.append(b);
            lst.clear();

            result : int = 0;
            if (lst.isEmpty())           result = result + 1;
            if (lst.size() == 0)      result = result + 10;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 11);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  2. Aggregate type — LinkedList<Point>
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("LinkedList<Point> — struct stored by value", "[libk][list][struct]") {
    auto j = jit_k(R"SRC(
        module __ll_struct_push__;

        struct Point {
            x : int;
            y : int;
            Point() { x = 0; y = 0; }
        }

        test() : int {
            lst : LinkedList<Point>;
            p1 : Point;
            p1.x = 10;
            p1.y = 20;
            p2 : Point;
            p2.x = 30;
            p2.y = 40;

            lst.append(p1);
            lst.append(p2);

            result : int = 0;
            if (lst.size() == 2) result = result + 1;
            // front is a copy of p1
            if (lst.first().x == 10) result = result + 10;
            if (lst.first().y == 20) result = result + 100;
            // back is a copy of p2
            if (lst.last().x == 30)  result = result + 1000;
            if (lst.last().y == 40)  result = result + 10000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 11111);
}

TEST_CASE("LinkedList<Point> — value semantics (mutation does not affect list)", "[libk][list][struct]") {
    auto j = jit_k(R"SRC(
        module __ll_struct_val__;

        struct Point {
            x : int;
            y : int;
            Point() { x = 0; y = 0; }
        }

        test() : int {
            lst : LinkedList<Point>;
            p : Point;
            p.x = 5;
            p.y = 7;
            lst.append(p);
            // mutate original — list copy must be unaffected
            p.x = 99;

            result : int = 0;
            if (lst.first().x == 5) result = result + 1;
            if (lst.first().y == 7) result = result + 10;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 11);
}

TEST_CASE("LinkedList<Point> — removeFront / removeBack", "[libk][list][struct]") {
    auto j = jit_k(R"SRC(
        module __ll_struct_rm__;

        struct Point {
            x : int;
            y : int;
            Point() { x = 0; y = 0; }
        }

        test() : int {
            lst : LinkedList<Point>;
            p1 : Point; p1.x = 1;
            p2 : Point; p2.x = 2;
            p3 : Point; p3.x = 3;
            lst.append(p1);
            lst.append(p2);
            lst.append(p3);
            lst.removeFront();
            lst.removeBack();
            // only p2 remains

            result : int = 0;
            if (lst.size() == 1)       result = result + 1;
            if (lst.first().x == 2)   result = result + 10;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 11);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  2b. Indexed access — get() and operator[]
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("LinkedList<int> — get by index", "[libk][list][int][index]") {
    auto j = jit_k(R"SRC(
        module __ll_get__;

        test() : int {
            lst : LinkedList<int>;
            a : int = 10;
            b : int = 20;
            c : int = 30;
            lst.append(a);
            lst.append(b);
            lst.append(c);

            result : int = 0;
            if (lst.get(0) == 10) result = result + 1;
            if (lst.get(1) == 20) result = result + 10;
            if (lst.get(2) == 30) result = result + 100;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 111);
}

TEST_CASE("LinkedList<int> — operator[] read", "[libk][list][int][index]") {
    auto j = jit_k(R"SRC(
        module __ll_subscript__;

        test() : int {
            lst : LinkedList<int>;
            a : int = 5;
            b : int = 15;
            c : int = 25;
            lst.append(a);
            lst.append(b);
            lst.append(c);

            result : int = 0;
            if (lst[0] == 5)  result = result + 1;
            if (lst[1] == 15) result = result + 10;
            if (lst[2] == 25) result = result + 100;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 111);
}

TEST_CASE("LinkedList<int> — operator[] write", "[libk][list][int][index]") {
    auto j = jit_k(R"SRC(
        module __ll_subscript_w__;

        test() : int {
            lst : LinkedList<int>;
            a : int = 0;
            b : int = 0;
            c : int = 0;
            lst.append(a);
            lst.append(b);
            lst.append(c);

            lst[0] = 42;
            lst[1] = 77;
            lst[2] = 99;

            return lst[0] + lst[1] + lst[2];
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 218);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  3. Pointer type — LinkedList storing pointers to heap objects
//     (T = Object*, the list holds raw mutable pointers)
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("LinkedList<Object*> — store and retrieve heap object pointers", "[libk][list][pointer]") {
    auto j = jit_k(R"SRC(
        module __ll_ptr__;

        test() : int {
            own1 : Object! = new Object();
            own2 : Object! = new Object();
            o1 : Object* = own1;
            o2 : Object* = own2;

            lst : LinkedList<Object*>;
            lst.append(o1);
            lst.append(o2);

            result : int = 0;
            if (lst.size() == 2)       result = result + 1;
            if (lst.first() == o1)    result = result + 10;
            if (lst.last() == o2)     result = result + 100;

            lst.clear();
            delete own1;
            delete own2;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 111);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  4. Owner type — LinkedList storing owners to heap objects
//     (T = Object!, the list holds owned pointers)
// ═══════════════════════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════════════════════
//  5. insert — LinkedList<int>
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("LinkedList<int> — insert at beginning, middle, end", "[libk][list][int][insert]") {
    auto j = jit_k(R"SRC(
        module __ll_int_insert__;

        test() : int {
            lst : LinkedList<int>;
            a : int = 10;
            b : int = 30;
            lst.append(a);
            lst.append(b);
            // list: 10, 30

            c : int = 20;
            lst.insert(1, c);
            // list: 10, 20, 30

            d : int = 5;
            lst.insert(0, d);
            // list: 5, 10, 20, 30

            e : int = 40;
            lst.insert(100, e);
            // list: 5, 10, 20, 30, 40

            result : int = 0;
            if (lst.size() == 5)  result = result + 1;
            if (lst[0] == 5)         result = result + 10;
            if (lst[1] == 10)        result = result + 100;
            if (lst[2] == 20)        result = result + 1000;
            if (lst[3] == 30)        result = result + 10000;
            if (lst[4] == 40)        result = result + 100000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 111111);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  6. insert — LinkedList<struct>
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("LinkedList<Point> — insert struct at index", "[libk][list][struct][insert]") {
    auto j = jit_k(R"SRC(
        module __ll_struct_insert__;

        struct Point {
            x : int;
            y : int;
            Point() { x = 0; y = 0; }
        }

        test() : int {
            lst : LinkedList<Point>;
            p1 : Point; p1.x = 1; p1.y = 2;
            p3 : Point; p3.x = 5; p3.y = 6;
            lst.append(p1);
            lst.append(p3);
            // list: (1,2), (5,6)

            p2 : Point; p2.x = 3; p2.y = 4;
            lst.insert(1, p2);
            // list: (1,2), (3,4), (5,6)

            result : int = 0;
            if (lst.size() == 3)    result = result + 1;
            if (lst[0].x == 1)         result = result + 10;
            if (lst[1].x == 3)         result = result + 100;
            if (lst[1].y == 4)         result = result + 1000;
            if (lst[2].x == 5)         result = result + 10000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 11111);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  7. Enum type — LinkedList<Color>
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("LinkedList<Color> — pushBack, insert, peek with enum", "[libk][list][enum]") {
    auto j = jit_k(R"SRC(
        module __ll_enum_full__;

        enum Color {
            RED = 0;
            GREEN = 1;
            BLUE = 2;
        };

        test() : int {
            lst : LinkedList<Color>;
            r : Color = Color::RED;
            g : Color = Color::GREEN;
            b : Color = Color::BLUE;
            lst.append(r);
            lst.append(b);
            lst.insert(1, g);
            // order: RED, GREEN, BLUE

            result : int = 0;
            if (lst.size() == 3)              result = result + 1;
            if (lst.first() == Color::RED)   result = result + 10;
            if (lst[1] == Color::GREEN)          result = result + 100;
            if (lst.last() == Color::BLUE)   result = result + 1000;

            lst.removeFront();
            if (lst.first() == Color::GREEN) result = result + 10000;

            lst.clear();
            if (lst.isEmpty())                   result = result + 100000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 111111);
}

TEST_CASE("LinkedList<Direction> — plain enum with auto values", "[libk][list][enum]") {
    auto j = jit_k(R"SRC(
        module __ll_enum_plain__;

        enum Direction {
            NORTH;
            SOUTH;
            EAST;
            WEST;
        };

        test() : int {
            lst : LinkedList<Direction>;
            n : Direction = Direction::NORTH;
            s : Direction = Direction::SOUTH;
            e : Direction = Direction::EAST;
            lst.append(n);
            lst.append(e);
            lst.insert(1, s);
            // order: NORTH, SOUTH, EAST

            result : int = 0;
            if (lst.size() == 3)              result = result + 1;
            if (lst[0] == Direction::NORTH)      result = result + 10;
            if (lst[1] == Direction::SOUTH)      result = result + 100;
            if (lst[2] == Direction::EAST)       result = result + 1000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1111);
}

TEST_CASE("LinkedList<Derived> — derived enum insert and access", "[libk][list][enum]") {
    auto j = jit_k(R"SRC(
        module __ll_derived_enum__;

        enum Base {
            A = 0;
            B = 1;
        };
        enum Derived : Base {
            C = 2;
        };

        test() : int {
            lst : LinkedList<Derived>;
            a : Derived = Derived::A;
            b : Derived = Derived::B;
            c : Derived = Derived::C;
            lst.append(a);
            lst.append(c);
            lst.insert(1, b);
            // order: A(0), B(1), C(2)

            result : int = 0;
            if (lst.size() == 3)            result = result + 1;
            if (lst[0] == Derived::A)          result = result + 10;
            if (lst[1] == Derived::B)          result = result + 100;
            if (lst[2] == Derived::C)          result = result + 1000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1111);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Existing owner tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("LinkedList<Object!> — store and retrieve owners", "[libk][list][owner]") {
    auto j = jit_k(R"SRC(
        module __ll_owner__;

        test() : int {
            lst : LinkedList<Object!>;
            o1 : Object! = new Object();
            o2 : Object! = new Object();

            lst.append(o1);
            lst.append(o2);

            result : int = 0;
            if (lst.size() == 2) result = result + 1;
            // owners were copied into the list by value,
            // so the list owns its own copy of the pointer
            lst.clear();
            delete o1;
            delete o2;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  DoubleLinkedList<T> tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("DoubleLinkedList<int> — pushBack and peek", "[libk][list][dlist][int]") {
    auto j = jit_k(R"SRC(
        module __dll_int_push__;

        test() : int {
            lst : DoubleLinkedList<int>;
            a : int = 10;
            b : int = 20;
            c : int = 30;
            lst.append(a);
            lst.append(b);
            lst.append(c);

            result : int = 0;
            if (lst.size() == 3)      result = result + 1;
            if (lst.first() == 10)   result = result + 10;
            if (lst.last() == 30)    result = result + 100;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 111);
}

TEST_CASE("DoubleLinkedList<int> — pushFront ordering", "[libk][list][dlist][int]") {
    auto j = jit_k(R"SRC(
        module __dll_int_front__;

        test() : int {
            lst : DoubleLinkedList<int>;
            a : int = 1;
            b : int = 2;
            c : int = 3;
            lst.prepend(a);
            lst.prepend(b);
            lst.prepend(c);
            // order: 3, 2, 1

            result : int = 0;
            if (lst.first() == 3)  result = result + 1;
            if (lst.last() == 1)   result = result + 10;
            if (lst[1] == 2)           result = result + 100;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 111);
}

TEST_CASE("DoubleLinkedList<int> — insert at beginning, middle, end", "[libk][list][dlist][int][insert]") {
    auto j = jit_k(R"SRC(
        module __dll_int_insert__;

        test() : int {
            lst : DoubleLinkedList<int>;
            a : int = 10;
            b : int = 30;
            lst.append(a);
            lst.append(b);
            // list: 10, 30

            c : int = 20;
            lst.insert(1, c);
            // list: 10, 20, 30

            d : int = 5;
            lst.insert(0, d);
            // list: 5, 10, 20, 30

            e : int = 40;
            lst.insert(100, e);
            // list: 5, 10, 20, 30, 40

            result : int = 0;
            if (lst.size() == 5)  result = result + 1;
            if (lst[0] == 5)         result = result + 10;
            if (lst[1] == 10)        result = result + 100;
            if (lst[2] == 20)        result = result + 1000;
            if (lst[3] == 30)        result = result + 10000;
            if (lst[4] == 40)        result = result + 100000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 111111);
}

TEST_CASE("DoubleLinkedList<int> — insert near end uses backward search", "[libk][list][dlist][int][insert]") {
    auto j = jit_k(R"SRC(
        module __dll_int_insert_back__;

        test() : int {
            lst : DoubleLinkedList<int>;
            a : int = 1;
            b : int = 2;
            c : int = 3;
            d : int = 4;
            e : int = 5;
            lst.append(a);
            lst.append(b);
            lst.append(c);
            lst.append(d);
            lst.append(e);
            // list: 1, 2, 3, 4, 5

            // Insert at index 4 (near end, should search from back)
            f : int = 99;
            lst.insert(4, f);
            // list: 1, 2, 3, 4, 99, 5

            result : int = 0;
            if (lst.size() == 6)   result = result + 1;
            if (lst[3] == 4)          result = result + 10;
            if (lst[4] == 99)         result = result + 100;
            if (lst[5] == 5)          result = result + 1000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1111);
}

TEST_CASE("DoubleLinkedList<int> — removeFront", "[libk][list][dlist][int]") {
    auto j = jit_k(R"SRC(
        module __dll_int_rmfront__;

        test() : int {
            lst : DoubleLinkedList<int>;
            a : int = 10;
            b : int = 20;
            c : int = 30;
            lst.append(a);
            lst.append(b);
            lst.append(c);
            lst.removeFront();
            // list: 20, 30

            result : int = 0;
            if (lst.size() == 2)      result = result + 1;
            if (lst.first() == 20)   result = result + 10;
            if (lst.last() == 30)    result = result + 100;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 111);
}

TEST_CASE("DoubleLinkedList<int> — removeBack O(1)", "[libk][list][dlist][int]") {
    auto j = jit_k(R"SRC(
        module __dll_int_rmback__;

        test() : int {
            lst : DoubleLinkedList<int>;
            a : int = 10;
            b : int = 20;
            c : int = 30;
            lst.append(a);
            lst.append(b);
            lst.append(c);
            lst.removeBack();
            // list: 10, 20

            result : int = 0;
            if (lst.size() == 2)      result = result + 1;
            if (lst.first() == 10)   result = result + 10;
            if (lst.last() == 20)    result = result + 100;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 111);
}

TEST_CASE("DoubleLinkedList<int> — clear", "[libk][list][dlist][int]") {
    auto j = jit_k(R"SRC(
        module __dll_int_clear__;

        test() : int {
            lst : DoubleLinkedList<int>;
            a : int = 1;
            b : int = 2;
            lst.append(a);
            lst.append(b);
            lst.clear();

            result : int = 0;
            if (lst.isEmpty())           result = result + 1;
            if (lst.size() == 0)      result = result + 10;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 11);
}

TEST_CASE("DoubleLinkedList<int> — indexed access from both ends", "[libk][list][dlist][int][index]") {
    auto j = jit_k(R"SRC(
        module __dll_int_idx__;

        test() : int {
            lst : DoubleLinkedList<int>;
            a : int = 10;
            b : int = 20;
            c : int = 30;
            d : int = 40;
            e : int = 50;
            lst.append(a);
            lst.append(b);
            lst.append(c);
            lst.append(d);
            lst.append(e);
            // list: 10, 20, 30, 40, 50

            result : int = 0;
            // Access from front (index 0,1 <= size/2=2)
            if (lst[0] == 10)  result = result + 1;
            if (lst[1] == 20)  result = result + 10;
            // Access from back (index 3,4 > size/2=2)
            if (lst[3] == 40)  result = result + 100;
            if (lst[4] == 50)  result = result + 1000;
            // Middle
            if (lst[2] == 30)  result = result + 10000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 11111);
}

TEST_CASE("DoubleLinkedList<Point> — struct stored by value", "[libk][list][dlist][struct]") {
    auto j = jit_k(R"SRC(
        module __dll_struct_push__;

        struct Point {
            x : int;
            y : int;
            Point() { x = 0; y = 0; }
        }

        test() : int {
            lst : DoubleLinkedList<Point>;
            p1 : Point; p1.x = 10; p1.y = 20;
            p2 : Point; p2.x = 30; p2.y = 40;
            lst.append(p1);
            lst.append(p2);

            result : int = 0;
            if (lst.size() == 2)         result = result + 1;
            if (lst.first().x == 10)    result = result + 10;
            if (lst.last().x == 30)     result = result + 100;
            if (lst[1].y == 40)             result = result + 1000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1111);
}

TEST_CASE("DoubleLinkedList<Color> — enum type", "[libk][list][dlist][enum]") {
    auto j = jit_k(R"SRC(
        module __dll_enum__;

        enum Color {
            RED = 0;
            GREEN = 1;
            BLUE = 2;
        };

        test() : int {
            lst : DoubleLinkedList<Color>;
            r : Color = Color::RED;
            g : Color = Color::GREEN;
            b : Color = Color::BLUE;
            lst.append(r);
            lst.append(g);
            lst.append(b);

            result : int = 0;
            if (lst.size() == 3)            result = result + 1;
            if (lst[0] == Color::RED)          result = result + 10;
            if (lst[1] == Color::GREEN)        result = result + 100;
            if (lst.last() == Color::BLUE) result = result + 1000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1111);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  8. Emplace operations — zero-arg (default constructor)
//
//  Note: emplace with explicit constructor args (e.g. lst.emplaceBack<int>(10))
//  is currently blocked by a compiler limitation: nested variadic template pack
//  forwarding does not correctly deduce the inner template's parameter pack from
//  the outer pack expansion. The generated code selects the zero-arg construct
//  intrinsic instead of the arg-forwarding variant.
//  See test-gen-member-template.cpp for the canonical documentation of this limitation.
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("LinkedList<Point> — emplaceBack zero-arg (default ctor)", "[libk][list][struct][emplace]") {
    auto j = jit_k(R"SRC(
        module __ll_pt_emb0__;

        struct Point {
            x : int;
            y : int;
            Point() { x = 0; y = 0; }
        }

        test() : int {
            lst : LinkedList<Point>;
            lst.emplaceBack<>();
            lst.emplaceBack<>();

            result : int = 0;
            if (lst.size() == 2)      result = result + 1;
            if (lst[0].x == 0)           result = result + 10;
            if (lst[0].y == 0)           result = result + 100;
            if (lst[1].x == 0)           result = result + 1000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1111);
}

TEST_CASE("LinkedList<Point> — emplaceFront zero-arg (default ctor)", "[libk][list][struct][emplace]") {
    auto j = jit_k(R"SRC(
        module __ll_pt_emf0__;

        struct Point {
            x : int;
            y : int;
            Point() { x = 7; y = 9; }
        }

        test() : int {
            lst : LinkedList<Point>;
            lst.emplaceFront<>();
            lst.emplaceFront<>();
            // both nodes constructed with defaults (7,9)

            result : int = 0;
            if (lst.size() == 2)      result = result + 1;
            if (lst[0].x == 7)           result = result + 10;
            if (lst[0].y == 9)           result = result + 100;
            if (lst[1].x == 7)           result = result + 1000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1111);
}

TEST_CASE("LinkedList<Point> — emplace zero-arg at index", "[libk][list][struct][emplace]") {
    auto j = jit_k(R"SRC(
        module __ll_pt_emi0__;

        struct Point {
            x : int;
            y : int;
            Point() { x = 5; y = 3; }
        }

        test() : int {
            lst : LinkedList<Point>;
            lst.emplaceBack<>();
            lst.emplaceBack<>();
            lst.emplace<>(1);
            // list: (5,3), (5,3), (5,3)

            result : int = 0;
            if (lst.size() == 3)      result = result + 1;
            if (lst[1].x == 5)           result = result + 10;
            if (lst[1].y == 3)           result = result + 100;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 111);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  9. DoubleLinkedList — zero-arg emplace operations
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("DoubleLinkedList<Point> — emplaceBack/emplaceFront/emplace zero-arg", "[libk][list][dlist][struct][emplace]") {
    auto j = jit_k(R"SRC(
        module __dll_pt_emp0__;

        struct Point {
            x : int;
            y : int;
            Point() { x = 1; y = 2; }
        }

        test() : int {
            lst : DoubleLinkedList<Point>;
            lst.emplaceBack<>();
            lst.emplaceBack<>();
            lst.emplaceFront<>();
            lst.emplace<>(2);
            // all have default values (1,2)

            result : int = 0;
            if (lst.size() == 4)  result = result + 1;
            if (lst[0].x == 1)       result = result + 10;
            if (lst[1].x == 1)       result = result + 100;
            if (lst[2].x == 1)       result = result + 1000;
            if (lst[3].y == 2)       result = result + 10000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 11111);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  10. DoubleLinkedList — typed enum
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("DoubleLinkedList<TypedEnum> — typed enum (short)", "[libk][list][dlist][enum]") {
    auto j = jit_k(R"SRC(
        module __dll_tenum__;

        enum Priority : short {
            LOW = 1;
            MED = 2;
            HIGH = 3;
        };

        test() : int {
            lst : DoubleLinkedList<Priority>;
            l : Priority = Priority::LOW;
            m : Priority = Priority::MED;
            h : Priority = Priority::HIGH;
            lst.append(l);
            lst.append(h);
            lst.insert(1, m);
            // order: LOW, MED, HIGH

            result : int = 0;
            if (lst.size() == 3)              result = result + 1;
            if (lst[0] == Priority::LOW)         result = result + 10;
            if (lst[1] == Priority::MED)         result = result + 100;
            if (lst[2] == Priority::HIGH)        result = result + 1000;

            lst.removeFront();
            if (lst.first() == Priority::MED) result = result + 10000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 11111);
}


// ═══════════════════════════════════════════════════════════════════════════════
//  Abstract interface polymorphism — LinkedList<T> / DoubleLinkedList<T> through
//  the Collection/Sequence hierarchy
//
//  DoubleLinkedList<T> implements MutableIndexedCollection<T> AND (since the fix
//  for the multi-layer template/diamond-interface compiler bug — see AGENTS.md
//  history) MutableReversibleSequence<T> as well, forming the same diamond as
//  Vector<T> (see test-vector.cpp §9). LinkedList<T> is singly-linked and does
//  NOT implement ReversibleSequence<T> — only the non-reversible interfaces are
//  exercised for it below.
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("LinkedList<int> — through MutableSequence<int>& (mutating iteration)",
          "[libk][list][interface][sequence]") {
    auto j = jit_k(R"SRC(
        module __ll_iface_mutseq__;

        doubleAll(seq : MutableSequence<int>&) {
            it : Iterator<int>! = seq.iterator();
            n : OptionalRef<int> = it.next();
            while (n.hasValue()) {
                n.get() = n.get() * 2;
                n = it.next();
            }
        }

        test() : int {
            lst : LinkedList<int>;
            lst.append(1);
            lst.append(2);
            lst.append(3);
            doubleAll(lst);
            return lst[0] + lst[1] + lst[2];
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 12);
}

TEST_CASE("LinkedList<int> — through MutableIndexedCollection<int>& (set/insert/removeAt)",
          "[libk][list][interface][indexed]") {
    auto j = jit_k(R"SRC(
        module __ll_iface_mutindexed__;

        mutate(coll : MutableIndexedCollection<int>&) {
            coll.insert(1, 99);
            coll.set(0, 42);
            coll.removeAt(2);
        }

        test() : int {
            lst : LinkedList<int>;
            lst.append(1);
            lst.append(2);
            lst.append(3);
            mutate(lst);
            // Started [1,2,3]; insert(1,99) -> [1,99,2,3]; set(0,42) -> [42,99,2,3];
            // removeAt(2) removes '2' -> [42,99,3].
            result : int = 0;
            if (lst.size() == 3)  result = result + 1;
            if (lst[0] == 42)     result = result + 10;
            if (lst[1] == 99)     result = result + 100;
            if (lst[2] == 3)      result = result + 1000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1111);
}

TEST_CASE("DoubleLinkedList<int> — through Sequence<int>& (const iteration)",
          "[libk][list][dlist][interface][sequence]") {
    auto j = jit_k(R"SRC(
        module __dll_iface_sequence__;

        sumAll(seq : Sequence<int>&) : int {
            it : ConstIterator<int>! = seq.constIterator();
            total : int = 0;
            n : OptionalConstRef<int> = it.next();
            while (n.hasValue()) {
                total = total + n.get();
                n = it.next();
            }
            return total;
        }

        test() : int {
            lst : DoubleLinkedList<int>;
            lst.append(1);
            lst.append(2);
            lst.append(3);
            return sumAll(lst);
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 6);
}

TEST_CASE("DoubleLinkedList<int> — through ReversibleSequence<int>& (const reverse iteration)",
          "[libk][list][dlist][interface][reversible]") {
    auto j = jit_k(R"SRC(
        module __dll_iface_reversible__;

        firstFromEnd(seq : ReversibleSequence<int>&) : int {
            it : ConstIterator<int>! = seq.constReverseIterator();
            n : OptionalConstRef<int> = it.next();
            if (n.hasValue()) return n.get();
            return -1;
        }

        test() : int {
            lst : DoubleLinkedList<int>;
            lst.append(10);
            lst.append(20);
            lst.append(30);
            return firstFromEnd(lst);
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 30);
}

TEST_CASE("DoubleLinkedList<int> — through MutableReversibleSequence<int>& (mutating reverse iteration)",
          "[libk][list][dlist][interface][reversible]") {
    auto j = jit_k(R"SRC(
        module __dll_iface_mutreversible__;

        sumReverseAndZeroLast(seq : MutableReversibleSequence<int>&) : int {
            it : Iterator<int>! = seq.reverseIterator();
            total : int = 0;
            first : bool = true;
            n : OptionalRef<int> = it.next();
            while (n.hasValue()) {
                total = total + n.get();
                if (first) {
                    n.get() = 0;
                    first = false;
                }
                n = it.next();
            }
            return total;
        }

        test() : int {
            lst : DoubleLinkedList<int>;
            lst.append(1);
            lst.append(2);
            lst.append(3);
            total : int = sumReverseAndZeroLast(lst);
            result : int = 0;
            if (total == 6)       result = result + 1;
            if (lst[2] == 0)      result = result + 10; // last element zeroed via reverse iterator
            if (lst[0] == 1)      result = result + 100;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 111);
}

TEST_CASE("DoubleLinkedList<int> — diamond dispatch: same instance through two independent interface paths",
          "[libk][list][dlist][interface][diamond]") {
    // Direct regression test for the multi-layer template/diamond-interface
    // compiler bug (see test-vector.cpp §9 for the Vector<T> counterpart and
    // full rationale): DoubleLinkedList<T> reaches MutableSequence<T> through
    // two distinct paths (MutableIndexedCollection<T>→MutableCollection<T>→
    // MutableSequence<T>, and directly via MutableReversibleSequence<T>→
    // MutableSequence<T>). Exercising both in one call catches wrong
    // base-subobject GEP offsets / vtable thunks specific to this class.
    auto j = jit_k(R"SRC(
        module __dll_iface_diamond__;

        viaIndexed(coll : MutableIndexedCollection<int>&) : int {
            return coll.get(0) + coll.get(coll.size() - 1);
        }

        viaReversible(seq : MutableReversibleSequence<int>&) : int {
            it : Iterator<int>! = seq.reverseIterator();
            n : OptionalRef<int> = it.next();
            total : int = 0;
            while (n.hasValue()) {
                total = total + n.get();
                n = it.next();
            }
            return total;
        }

        test() : int {
            lst : DoubleLinkedList<int>;
            lst.append(1);
            lst.append(2);
            lst.append(3);

            a : int = viaIndexed(lst);     // 1 + 3 = 4
            b : int = viaReversible(lst);  // 1 + 2 + 3 = 6

            result : int = 0;
            if (a == 4) result = result + 1;
            if (b == 6) result = result + 10;
            if (lst.size() == 3) result = result + 100;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 111);
}
