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
            lst.pushBack(a);
            lst.pushBack(b);
            lst.pushBack(c);

            result : int = 0;
            if (lst.getSize() == 3)      result = result + 1;
            if (lst.peekFront() == 10)   result = result + 10;
            if (lst.peekBack() == 30)    result = result + 100;
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
            lst.pushFront(a);
            lst.pushFront(b);
            lst.pushFront(c);
            // order: 3, 2, 1

            result : int = 0;
            if (lst.peekFront() == 3)  result = result + 1;
            if (lst.peekBack() == 1)   result = result + 10;
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
            lst.pushBack(a);
            lst.pushBack(b);
            lst.pushBack(c);
            lst.removeFront();
            // list: 20, 30

            result : int = 0;
            if (lst.getSize() == 2)      result = result + 1;
            if (lst.peekFront() == 20)   result = result + 10;
            if (lst.peekBack() == 30)    result = result + 100;
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
            lst.pushBack(a);
            lst.pushBack(b);
            lst.pushBack(c);
            lst.removeBack();
            // list: 10, 20

            result : int = 0;
            if (lst.getSize() == 2)      result = result + 1;
            if (lst.peekFront() == 10)   result = result + 10;
            if (lst.peekBack() == 20)    result = result + 100;
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
            lst.pushBack(a);
            lst.pushBack(b);
            lst.clear();

            result : int = 0;
            if (lst.isEmpty())           result = result + 1;
            if (lst.getSize() == 0)      result = result + 10;
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

            lst.pushBack(p1);
            lst.pushBack(p2);

            result : int = 0;
            if (lst.getSize() == 2) result = result + 1;
            // front is a copy of p1
            if (lst.peekFront().x == 10) result = result + 10;
            if (lst.peekFront().y == 20) result = result + 100;
            // back is a copy of p2
            if (lst.peekBack().x == 30)  result = result + 1000;
            if (lst.peekBack().y == 40)  result = result + 10000;
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
            lst.pushBack(p);
            // mutate original — list copy must be unaffected
            p.x = 99;

            result : int = 0;
            if (lst.peekFront().x == 5) result = result + 1;
            if (lst.peekFront().y == 7) result = result + 10;
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
            lst.pushBack(p1);
            lst.pushBack(p2);
            lst.pushBack(p3);
            lst.removeFront();
            lst.removeBack();
            // only p2 remains

            result : int = 0;
            if (lst.getSize() == 1)       result = result + 1;
            if (lst.peekFront().x == 2)   result = result + 10;
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
            lst.pushBack(a);
            lst.pushBack(b);
            lst.pushBack(c);

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
            lst.pushBack(a);
            lst.pushBack(b);
            lst.pushBack(c);

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
            lst.pushBack(a);
            lst.pushBack(b);
            lst.pushBack(c);

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
            lst.pushBack(o1);
            lst.pushBack(o2);

            result : int = 0;
            if (lst.getSize() == 2)       result = result + 1;
            if (lst.peekFront() == o1)    result = result + 10;
            if (lst.peekBack() == o2)     result = result + 100;

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
            lst.pushBack(a);
            lst.pushBack(b);
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
            if (lst.getSize() == 5)  result = result + 1;
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
            lst.pushBack(p1);
            lst.pushBack(p3);
            // list: (1,2), (5,6)

            p2 : Point; p2.x = 3; p2.y = 4;
            lst.insert(1, p2);
            // list: (1,2), (3,4), (5,6)

            result : int = 0;
            if (lst.getSize() == 3)    result = result + 1;
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
            lst.pushBack(r);
            lst.pushBack(b);
            lst.insert(1, g);
            // order: RED, GREEN, BLUE

            result : int = 0;
            if (lst.getSize() == 3)              result = result + 1;
            if (lst.peekFront() == Color::RED)   result = result + 10;
            if (lst[1] == Color::GREEN)          result = result + 100;
            if (lst.peekBack() == Color::BLUE)   result = result + 1000;

            lst.removeFront();
            if (lst.peekFront() == Color::GREEN) result = result + 10000;

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
            lst.pushBack(n);
            lst.pushBack(e);
            lst.insert(1, s);
            // order: NORTH, SOUTH, EAST

            result : int = 0;
            if (lst.getSize() == 3)              result = result + 1;
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
            lst.pushBack(a);
            lst.pushBack(c);
            lst.insert(1, b);
            // order: A(0), B(1), C(2)

            result : int = 0;
            if (lst.getSize() == 3)            result = result + 1;
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

            lst.pushBack(o1);
            lst.pushBack(o2);

            result : int = 0;
            if (lst.getSize() == 2) result = result + 1;
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
            lst.pushBack(a);
            lst.pushBack(b);
            lst.pushBack(c);

            result : int = 0;
            if (lst.getSize() == 3)      result = result + 1;
            if (lst.peekFront() == 10)   result = result + 10;
            if (lst.peekBack() == 30)    result = result + 100;
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
            lst.pushFront(a);
            lst.pushFront(b);
            lst.pushFront(c);
            // order: 3, 2, 1

            result : int = 0;
            if (lst.peekFront() == 3)  result = result + 1;
            if (lst.peekBack() == 1)   result = result + 10;
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
            lst.pushBack(a);
            lst.pushBack(b);
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
            if (lst.getSize() == 5)  result = result + 1;
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
            lst.pushBack(a);
            lst.pushBack(b);
            lst.pushBack(c);
            lst.pushBack(d);
            lst.pushBack(e);
            // list: 1, 2, 3, 4, 5

            // Insert at index 4 (near end, should search from back)
            f : int = 99;
            lst.insert(4, f);
            // list: 1, 2, 3, 4, 99, 5

            result : int = 0;
            if (lst.getSize() == 6)   result = result + 1;
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
            lst.pushBack(a);
            lst.pushBack(b);
            lst.pushBack(c);
            lst.removeFront();
            // list: 20, 30

            result : int = 0;
            if (lst.getSize() == 2)      result = result + 1;
            if (lst.peekFront() == 20)   result = result + 10;
            if (lst.peekBack() == 30)    result = result + 100;
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
            lst.pushBack(a);
            lst.pushBack(b);
            lst.pushBack(c);
            lst.removeBack();
            // list: 10, 20

            result : int = 0;
            if (lst.getSize() == 2)      result = result + 1;
            if (lst.peekFront() == 10)   result = result + 10;
            if (lst.peekBack() == 20)    result = result + 100;
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
            lst.pushBack(a);
            lst.pushBack(b);
            lst.clear();

            result : int = 0;
            if (lst.isEmpty())           result = result + 1;
            if (lst.getSize() == 0)      result = result + 10;
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
            lst.pushBack(a);
            lst.pushBack(b);
            lst.pushBack(c);
            lst.pushBack(d);
            lst.pushBack(e);
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
            lst.pushBack(p1);
            lst.pushBack(p2);

            result : int = 0;
            if (lst.getSize() == 2)         result = result + 1;
            if (lst.peekFront().x == 10)    result = result + 10;
            if (lst.peekBack().x == 30)     result = result + 100;
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
            lst.pushBack(r);
            lst.pushBack(g);
            lst.pushBack(b);

            result : int = 0;
            if (lst.getSize() == 3)            result = result + 1;
            if (lst[0] == Color::RED)          result = result + 10;
            if (lst[1] == Color::GREEN)        result = result + 100;
            if (lst.peekBack() == Color::BLUE) result = result + 1000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1111);
}
