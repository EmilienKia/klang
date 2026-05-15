/*
 * K Language compiler
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

TEST_CASE("Recursive struct field through pointer is supported", "[gen][indirection][recursive][pointer]") {
    auto jit = gen_jit(R"SRC(
        module __rec_ptr_self__;

        struct Node {
            value : int = 0;
            next : Node* = null;
        }

        test() : int {
            a : Node;
            b : Node;
            b.value = 42;
            a.next = &b;
            return a.next->value;
        }
    )SRC");

    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

TEST_CASE("Recursive struct field through owner is supported", "[gen][indirection][recursive][owner]") {
    auto jit = gen_jit(R"SRC(
        module __rec_owner_self__;

        struct Node {
            value : int = 0;
            next : Node! = null;
        }

        test() : int {
            head : Node! = new Node();
            second : Node! = new Node();
            head.value = 10;
            second.value = 32;

            head.next = second;

            moved : int = 0;
            if (second == null) {
                moved = 1;
            }

            result : int = head.next.value + moved;
            delete head;
            return result;
        }
    )SRC");

    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 33);
}

TEST_CASE("Recursive struct field through view is supported", "[gen][indirection][recursive][view]") {
    auto jit = gen_jit(R"SRC(
        module __rec_view_self__;

        struct Node {
            value : int = 0;
            next : Node? = null;
        }

        test() : int {
            a : Node;
            if (a.next == null) {
                return 99;
            }
            return -1;
        }
    )SRC");

    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 99);
}

TEST_CASE("Recursive struct field through reference is rejected with diagnostic", "[gen][indirection][recursive][reference]") {
    REQUIRE_THROWS_WITH(gen_jit_throws(R"SRC(
        module __rec_ref_self__;

        struct Node {
            next : Node&;
        }

        test() : int { return 0; }
    )SRC"),
    Catch::Matchers::ContainsSubstring("Forbidden recursive indirection")
    && Catch::Matchers::ContainsSubstring("field 'next'")
    && Catch::Matchers::ContainsSubstring("Node&"));
}

TEST_CASE("Recursive struct field through link is rejected with diagnostic", "[gen][indirection][recursive][link]") {
    REQUIRE_THROWS_WITH(gen_jit_throws(R"SRC(
        module __rec_link_self__;

        struct Node {
            next : Node+;
        }

        test() : int { return 0; }
    )SRC"),
    Catch::Matchers::ContainsSubstring("Forbidden recursive indirection")
    && Catch::Matchers::ContainsSubstring("field 'next'")
    && Catch::Matchers::ContainsSubstring("Node+"));
}










