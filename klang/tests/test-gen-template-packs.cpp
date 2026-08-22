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

/**
 * Template parameter packs (variadic templates) test suite.
 *
 * Covers:
 *   [PARSE] Parsing: typename... Ts in template parameter list.
 *   [PARSE] Parsing: Ts... args in function parameter list.
 *   [PARSE] Parsing: args... in function call arguments (pack expansion).
 *   [GEN]   Codegen: perfect forwarding with single pack type.
 *   [GEN]   Codegen: perfect forwarding with multiple pack types.
 *   [GEN]   Codegen: non-pack param + pack param.
 *   [GEN]   Codegen: empty pack (zero types).
 */

#include <catch2/catch_all.hpp>
#include "helpers.hpp"

TEST_CASE("template pack - simple forwarding two args", "[gen][template-packs]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_packs_01;

        fun target(a: int, b: int) : int {
            return a + b;
        }

        template<typename... Ts>
        fun forward_call(Ts... args) : int {
            return target(args...);
        }

        fun test_one() : int {
            return forward_call<int, int>(10, 32);
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_one");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

TEST_CASE("template pack - forwarding three args", "[gen][template-packs]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_packs_02;

        fun sum3(a: int, b: int, c: int) : int {
            return a + b + c;
        }

        template<typename... Ts>
        fun fwd3(Ts... args) : int {
            return sum3(args...);
        }

        fun test_three() : int {
            return fwd3<int, int, int>(10, 20, 12);
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_three");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

TEST_CASE("template pack - non-pack param before pack", "[gen][template-packs]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_packs_03;

        fun add(a: int, b: int) : int {
            return a + b;
        }

        template<typename T, typename... Ts>
        fun make_sum(first: T, Ts... rest) : int {
            return add(first, rest...);
        }

        fun test_mixed() : int {
            return make_sum<int, int>(40, 2);
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_mixed");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

TEST_CASE("template pack - empty pack", "[gen][template-packs]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_packs_04;

        fun no_args() : int {
            return 99;
        }

        template<typename... Ts>
        fun fwd_empty(Ts... args) : int {
            return no_args(args...);
        }

        fun test_empty() : int {
            return fwd_empty<>();
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_empty");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 99);
}
