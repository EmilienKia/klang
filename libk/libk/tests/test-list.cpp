/*
 * K Language standard library — Generic list tests
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

TEST_CASE("LinkedList — empty list is empty", "[libk][list][linked]") {
    auto j = jit_k(R"SRC(
        module __ll_empty__;

        test() : bool {
            lst : LinkedList<Object>;
            return lst.isEmpty();
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<bool(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == true);
}

TEST_CASE("LinkedList — pushFront and pushBack update size and peeks", "[libk][list][linked]") {
    auto j = jit_k(R"SRC(
        module __ll_push_peek__;

        test() : int {
            lst : LinkedList<Object>;
            o1 : Object! = new Object();
            o2 : Object! = new Object();
            o3 : Object! = new Object();

            lst.pushFront(o1);
            lst.pushBack(o2);
            lst.pushFront(o3);

            result : int = 0;
            if (lst.getSize() == 3) result = result + 1;
            if (lst.peekFront() == o3) result = result + 10;
            if (lst.peekBack() == o2) result = result + 100;

            lst.clear();
            delete o1;
            delete o2;
            delete o3;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 111);
}

TEST_CASE("LinkedList — removeFront and removeBack shrink the list", "[libk][list][linked]") {
    auto j = jit_k(R"SRC(
        module __ll_remove_ends__;

        test() : int {
            lst : LinkedList<Object>;
            o1 : Object! = new Object();
            o2 : Object! = new Object();
            o3 : Object! = new Object();

            lst.pushBack(o1);
            lst.pushBack(o2);
            lst.pushBack(o3);

            result : int = 0;
            if (lst.removeFront()) result = result + 1;
            if (lst.removeBack()) result = result + 10;
            if (lst.getSize() == 1) result = result + 100;
            if (lst.peekFront() == o2) result = result + 1000;

            lst.clear();
            delete o1;
            delete o2;
            delete o3;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1111);
}

TEST_CASE("LinkedList — remove by value identity removes the matching node", "[libk][list][linked]") {
    auto j = jit_k(R"SRC(
        module __ll_remove_value__;

        test() : int {
            lst : LinkedList<Object>;
            o1 : Object! = new Object();
            o2 : Object! = new Object();
            o3 : Object! = new Object();

            lst.pushBack(o1);
            lst.pushBack(o2);
            lst.pushBack(o3);

            result : int = 0;
            if (lst.remove(o2)) result = result + 1;
            if (lst.getSize() == 2) result = result + 10;
            if (lst.peekFront() == o1) result = result + 100;
            if (lst.peekBack() == o3) result = result + 1000;

            lst.clear();
            delete o1;
            delete o2;
            delete o3;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1111);
}

TEST_CASE("DoubleLinkedList — empty list is empty", "[libk][list][double]") {
    auto j = jit_k(R"SRC(
        module __dll_empty__;

        test() : bool {
            lst : DoubleLinkedList<Object>;
            return lst.isEmpty();
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<bool(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == true);
}

TEST_CASE("DoubleLinkedList — mixed pushes preserve front and back", "[libk][list][double]") {
    auto j = jit_k(R"SRC(
        module __dll_push_peek__;

        test() : int {
            lst : DoubleLinkedList<Object>;
            o1 : Object! = new Object();
            o2 : Object! = new Object();
            o3 : Object! = new Object();

            lst.pushFront(o1);
            lst.pushBack(o2);
            lst.pushFront(o3);

            result : int = 0;
            if (lst.getSize() == 3) result = result + 1;
            if (lst.peekFront() == o3) result = result + 10;
            if (lst.peekBack() == o2) result = result + 100;

            lst.clear();
            delete o1;
            delete o2;
            delete o3;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 111);
}

TEST_CASE("DoubleLinkedList — removeFront and removeBack keep tail and head consistent", "[libk][list][double]") {
    auto j = jit_k(R"SRC(
        module __dll_remove_ends__;

        test() : int {
            lst : DoubleLinkedList<Object>;
            o1 : Object! = new Object();
            o2 : Object! = new Object();
            o3 : Object! = new Object();

            lst.pushBack(o1);
            lst.pushBack(o2);
            lst.pushBack(o3);

            result : int = 0;
            if (lst.removeFront()) result = result + 1;
            if (lst.removeBack()) result = result + 10;
            if (lst.getSize() == 1) result = result + 100;
            if (lst.peekFront() == o2) result = result + 1000;
            if (lst.peekBack() == o2) result = result + 10000;

            lst.clear();
            delete o1;
            delete o2;
            delete o3;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 11111);
}

TEST_CASE("DoubleLinkedList — remove by value identity unlinks middle node", "[libk][list][double]") {
    auto j = jit_k(R"SRC(
        module __dll_remove_value__;

        test() : int {
            lst : DoubleLinkedList<Object>;
            o1 : Object! = new Object();
            o2 : Object! = new Object();
            o3 : Object! = new Object();

            lst.pushBack(o1);
            lst.pushBack(o2);
            lst.pushBack(o3);

            result : int = 0;
            if (lst.remove(o2)) result = result + 1;
            if (lst.getSize() == 2) result = result + 10;
            if (lst.peekFront() == o1) result = result + 100;
            if (lst.peekBack() == o3) result = result + 1000;

            lst.clear();
            delete o1;
            delete o2;
            delete o3;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1111);
}



