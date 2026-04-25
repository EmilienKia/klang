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

// Compile-time paths injected by CMake (see libk/libk/CMakeLists.txt).
#ifndef LIBK_KDI_DIR
#error "LIBK_KDI_DIR must be defined — check CMakeLists.txt"
#endif
#ifndef LIBK_LIB_DIR
#error "LIBK_LIB_DIR must be defined — check CMakeLists.txt"
#endif

namespace {

/// Compile K source against libk and JIT it.
std::unique_ptr<k::model::gen::jit> jit_k(std::string_view src) {
    return gen_jit_with_stdlib(src, LIBK_KDI_DIR, LIBK_LIB_DIR);
}

} // anonymous namespace

// ═════════════════════════════════════════════════════════════════════════════
// 1. LinkedList — basic operations
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("LinkedList — empty list is empty", "[libk][list][linked]") {
    auto j = jit_k(R"SRC(
        module __ll_empty__;

        test() : bool {
            lst : LinkedList<Object>! = new LinkedList<Object>();
            return lst.isEmpty();
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<bool(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == true);
}

TEST_CASE("LinkedList — getSize after pushFront", "[libk][list][linked]") {
    auto j = jit_k(R"SRC(
        module __ll_size__;

        test() : int {
            lst : LinkedList<Object>! = new LinkedList<Object>();
            o1 : Object! = new Object();
            o2 : Object! = new Object();
            lst.pushFront(o1);
            lst.pushFront(o2);
            return lst.getSize();
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 2);
}

TEST_CASE("LinkedList — isEmpty after pushFront", "[libk][list][linked]") {
    auto j = jit_k(R"SRC(
        module __ll_not_empty__;

        test() : bool {
            lst : LinkedList<Object>! = new LinkedList<Object>();
            o : Object! = new Object();
            lst.pushFront(o);
            return lst.isEmpty();
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<bool(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == false);
}

TEST_CASE("LinkedList — pushFront / popFront decrements size", "[libk][list][linked]") {
    auto j = jit_k(R"SRC(
        module __ll_lifo__;

        test() : int {
            lst : LinkedList<Object>! = new LinkedList<Object>();
            o1 : Object! = new Object();
            o2 : Object! = new Object();
            lst.pushFront(o1);
            lst.pushFront(o2);
            v2 : Object!? = lst.popFront();
            if (v2 == null) { return -1; }
            return lst.getSize();
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}

TEST_CASE("LinkedList — pushBack / popFront FIFO order", "[libk][list][linked]") {
    auto j = jit_k(R"SRC(
        module __ll_fifo__;

        test() : int {
            lst : LinkedList<Object>! = new LinkedList<Object>();
            o1 : Object! = new Object();
            o2 : Object! = new Object();
            o3 : Object! = new Object();
            lst.pushBack(o1);
            lst.pushBack(o2);
            lst.pushBack(o3);
            v : Object!? = lst.popFront();
            if (v == null) { return -1; }
            return lst.getSize();
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 2);
}

TEST_CASE("LinkedList — clear empties the list", "[libk][list][linked]") {
    auto j = jit_k(R"SRC(
        module __ll_clear__;

        test() : int {
            lst : LinkedList<Object>! = new LinkedList<Object>();
            o1 : Object! = new Object();
            o2 : Object! = new Object();
            lst.pushFront(o1);
            lst.pushFront(o2);
            lst.clear();
            return lst.getSize();
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// 2. DoubleLinkedList — basic operations
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("DoubleLinkedList — empty list is empty", "[libk][list][double]") {
    auto j = jit_k(R"SRC(
        module __dll_empty__;

        test() : bool {
            lst : DoubleLinkedList<Object>! = new DoubleLinkedList<Object>();
            return lst.isEmpty();
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<bool(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == true);
}

TEST_CASE("DoubleLinkedList — getSize after pushFront and pushBack",
          "[libk][list][double]") {
    auto j = jit_k(R"SRC(
        module __dll_size__;

        test() : int {
            lst : DoubleLinkedList<Object>! = new DoubleLinkedList<Object>();
            o1 : Object! = new Object();
            o2 : Object! = new Object();
            o3 : Object! = new Object();
            lst.pushFront(o1);
            lst.pushBack(o2);
            lst.pushFront(o3);
            return lst.getSize();
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 3);
}

TEST_CASE("DoubleLinkedList — pushBack / popBack round-trip", "[libk][list][double]") {
    auto j = jit_k(R"SRC(
        module __dll_back__;

        test() : int {
            lst : DoubleLinkedList<Object>! = new DoubleLinkedList<Object>();
            o1 : Object! = new Object();
            o2 : Object! = new Object();
            lst.pushBack(o1);
            lst.pushBack(o2);
            v : Object!? = lst.popBack();
            if (v == null) { return -1; }
            return lst.getSize();
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}

TEST_CASE("DoubleLinkedList — clear empties the list", "[libk][list][double]") {
    auto j = jit_k(R"SRC(
        module __dll_clear__;

        test() : int {
            lst : DoubleLinkedList<Object>! = new DoubleLinkedList<Object>();
            o1 : Object! = new Object();
            o2 : Object! = new Object();
            lst.pushFront(o1);
            lst.pushBack(o2);
            lst.clear();
            return lst.getSize();
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 0);
}



