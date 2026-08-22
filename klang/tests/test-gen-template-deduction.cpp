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
 * Template argument deduction test suite.
 *
 * Tests that template function arguments can be deduced from call-site
 * argument types without explicit template argument specification.
 */
#include <catch2/catch_all.hpp>
#include "helpers.hpp"
TEST_CASE("template deduction - simple single param", "[gen][template-deduction]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_deduction_01;
        template<typename T>
        fun identity(a: T) : T {
            return a;
        }
        fun test_deduction() : int {
            return identity(42);
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_deduction");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}
TEST_CASE("template deduction - two different params", "[gen][template-deduction]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_deduction_02;
        template<typename T, typename U>
        fun first_of(a: T, b: U) : T {
            return a;
        }
        fun test_two() : int {
            return first_of(99, 3l);
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_two");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 99);
}
TEST_CASE("template deduction - same param used twice", "[gen][template-deduction]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_deduction_03;
        template<typename T>
        fun add_same(a: T, b: T) : T {
            return a + b;
        }
        fun test_same() : int {
            return add_same(20, 22);
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_same");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}
TEST_CASE("template deduction - pack deduction", "[gen][template-deduction]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_deduction_04;
        fun sum2(a: int, b: int) : int {
            return a + b;
        }
        template<typename... Ts>
        fun forward_sum(Ts... args) : int {
            return sum2(args...);
        }
        fun test_pack() : int {
            return forward_sum(20, 22);
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_pack");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}
TEST_CASE("template deduction - mixed param + pack", "[gen][template-deduction]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_deduction_05;
        fun combine(a: int, b: int, c: int) : int {
            return a + b + c;
        }
        template<typename T, typename... Ts>
        fun fwd_mixed(first: T, Ts... rest) : int {
            return combine(first, rest...);
        }
        fun test_mixed() : int {
            return fwd_mixed(10, 20, 12);
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_mixed");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}
TEST_CASE("template deduction - forwarding chain", "[gen][template-deduction]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_deduction_06;
        fun target(a: int, b: int) : int {
            return a * b;
        }
        template<typename... Ts>
        fun wrapper(Ts... args) : int {
            return target(args...);
        }
        template<typename... Ts>
        fun outer(Ts... args) : int {
            return wrapper(args...);
        }
        fun test_chain() : int {
            return outer(6, 7);
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_chain");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}
TEST_CASE("template deduction - empty pack", "[gen][template-deduction]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_deduction_07;
        fun no_args() : int {
            return 77;
        }
        template<typename... Ts>
        fun fwd_empty(Ts... args) : int {
            return no_args(args...);
        }
        fun test_empty() : int {
            return fwd_empty();
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_empty");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 77);
}
TEST_CASE("template deduction - prefers non-template exact match", "[gen][template-deduction]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_deduction_08;
        fun process(a: int) : int {
            return a + 1;
        }
        template<typename T>
        fun process(a: T) : T {
            return a;
        }
        fun test_prefer() : int {
            return process(41);
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_prefer");
    REQUIRE(fn != nullptr);
    // Non-template should be preferred: 41 + 1 = 42
    REQUIRE(fn() == 42);
}

TEST_CASE("template deduction - deduces when non-template has different arity", "[gen][template-deduction]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_deduction_09;
        // Non-template with 2 params (different arity than the call)
        fun work(a: int, b: int) : int {
            return a + b;
        }
        // Template with 1 param — should be deduced for single-arg calls
        template<typename T>
        fun work(a: T) : T {
            return a * 2;
        }
        fun test_deduced_different_arity() : int {
            return work(21);
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_deduced_different_arity");
    REQUIRE(fn != nullptr);
    // Template deduced with T=int: 21*2 = 42
    REQUIRE(fn() == 42);
}

TEST_CASE("template deduction - fails on type inconsistency", "[gen][template-deduction]") {
    // Same T deduced to int from first arg and long from second arg
    // should fail deduction, leaving no viable candidate → compilation error
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module gen_template_deduction_10;
        template<typename T>
        fun same_type(a: T, b: T) : T {
            return a + b;
        }
        fun test_fail() : int {
            return same_type(42, 99l);
        }
    )SRC"));
}


TEST_CASE("template deduction - overloaded targets with single type param", "[gen][template-deduction]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_deduction_11;

        // Overloaded target functions with different parameter types
        fun compute(a: int) : int {
            return a * 2;
        }

        fun compute(a: long) : long {
            return a * 3l;
        }

        fun compute(a: int, b: int) : int {
            return a + b;
        }

        fun compute(a: int, b: long) : long {
            return ((long)a) + b;
        }

        fun compute(a: long, b: long) : long {
            return a * b;
        }

        fun compute(a: int, b: int, c: int) : int {
            return a + b + c;
        }

        // Template forwarder with a single type param
        template<typename T>
        fun forward_one(x: T) : T {
            return compute(x);
        }

        // Template forwarder with two type params
        template<typename T, typename U>
        fun forward_two(x: T, y: U) : U {
            return compute(x, y);
        }

        // Template forwarder with three params (same type)
        template<typename T>
        fun forward_three_same(a: T, b: T, c: T) : T {
            return compute(a, b, c);
        }

        // --- Test functions ---

        // Deduce T=int, calls compute(int) -> 21*2 = 42
        fun test_one_int() : int {
            return forward_one(21);
        }

        // Deduce T=long, calls compute(long) -> 14*3 = 42
        fun test_one_long() : long {
            return forward_one(14l);
        }

        // Deduce T=int, U=int, calls compute(int,int) -> 20+22 = 42
        fun test_two_int_int() : int {
            return forward_two(20, 22);
        }

        // Deduce T=int, U=long, calls compute(int,long) -> 10+32 = 42
        fun test_two_int_long() : long {
            return forward_two(10, 32l);
        }

        // Deduce T=long, U=long, calls compute(long,long) -> 6*7 = 42
        fun test_two_long_long() : long {
            return forward_two(6l, 7l);
        }

        // Deduce T=int, calls compute(int,int,int) -> 10+20+12 = 42
        fun test_three_same() : int {
            return forward_three_same(10, 20, 12);
        }
    )SRC");

    SECTION("forward_one with int - deduces T=int") {
        auto fn = jit->lookup_symbol<int(*)()>("test_one_int");
        REQUIRE(fn != nullptr);
        REQUIRE(fn() == 42);
    }

    SECTION("forward_one with long - deduces T=long") {
        auto fn = jit->lookup_symbol<long(*)()>("test_one_long");
        REQUIRE(fn != nullptr);
        REQUIRE(fn() == 42l);
    }

    SECTION("forward_two with int,int - deduces T=int, U=int") {
        auto fn = jit->lookup_symbol<int(*)()>("test_two_int_int");
        REQUIRE(fn != nullptr);
        REQUIRE(fn() == 42);
    }

    SECTION("forward_two with int,long - deduces T=int, U=long") {
        auto fn = jit->lookup_symbol<long(*)()>("test_two_int_long");
        REQUIRE(fn != nullptr);
        REQUIRE(fn() == 42l);
    }

    SECTION("forward_two with long,long - deduces T=long, U=long") {
        auto fn = jit->lookup_symbol<long(*)()>("test_two_long_long");
        REQUIRE(fn != nullptr);
        REQUIRE(fn() == 42l);
    }

    SECTION("forward_three_same with int,int,int - deduces T=int") {
        auto fn = jit->lookup_symbol<int(*)()>("test_three_same");
        REQUIRE(fn != nullptr);
        REQUIRE(fn() == 42);
    }
}

TEST_CASE("template deduction - pack forwarding to distinct targets", "[gen][template-deduction]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_deduction_12;

        // Distinct target functions (no overloading)
        fun target_one(a: int) : int {
            return a * 2;
        }

        fun target_two(a: int, b: int) : int {
            return a + b;
        }

        fun target_three(a: int, b: int, c: int) : int {
            return a + b + c;
        }

        // Pack forwarders to each distinct target
        template<typename... Ts>
        fun fwd_one(Ts... args) : int {
            return target_one(args...);
        }

        template<typename... Ts>
        fun fwd_two(Ts... args) : int {
            return target_two(args...);
        }

        template<typename... Ts>
        fun fwd_three(Ts... args) : int {
            return target_three(args...);
        }

        // Mixed: one fixed param + pack
        template<typename T, typename... Ts>
        fun fwd_first_rest(first: T, Ts... rest) : int {
            return target_two(first, rest...);
        }

        template<typename T, typename... Ts>
        fun fwd_first_rest3(first: T, Ts... rest) : int {
            return target_three(first, rest...);
        }

        // --- Test functions ---

        fun test_fwd_one() : int {
            return fwd_one(21);
        }

        fun test_fwd_two() : int {
            return fwd_two(20, 22);
        }

        fun test_fwd_three() : int {
            return fwd_three(10, 20, 12);
        }

        fun test_fwd_first_rest() : int {
            return fwd_first_rest(30, 12);
        }

        fun test_fwd_first_rest3() : int {
            return fwd_first_rest3(10, 20, 12);
        }
    )SRC");

    SECTION("pack deduction with 1 arg") {
        auto fn = jit->lookup_symbol<int(*)()>("test_fwd_one");
        REQUIRE(fn != nullptr);
        REQUIRE(fn() == 42);
    }

    SECTION("pack deduction with 2 args") {
        auto fn = jit->lookup_symbol<int(*)()>("test_fwd_two");
        REQUIRE(fn != nullptr);
        REQUIRE(fn() == 42);
    }

    SECTION("pack deduction with 3 args") {
        auto fn = jit->lookup_symbol<int(*)()>("test_fwd_three");
        REQUIRE(fn != nullptr);
        REQUIRE(fn() == 42);
    }

    SECTION("fixed param + pack deduction with 2 args") {
        auto fn = jit->lookup_symbol<int(*)()>("test_fwd_first_rest");
        REQUIRE(fn != nullptr);
        REQUIRE(fn() == 42);
    }

    SECTION("fixed param + pack deduction with 3 args") {
        auto fn = jit->lookup_symbol<int(*)()>("test_fwd_first_rest3");
        REQUIRE(fn != nullptr);
        REQUIRE(fn() == 42);
    }
}

