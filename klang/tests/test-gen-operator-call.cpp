/*
 * K Language compiler
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

TEST_CASE("Call operator: direct invocation on an aggregate", "[gen][operator][call-operator]")
{
    auto jit = gen_jit(R"SRC(
        module test;
        struct Adder {
            base : int;
            operator()(x : int) : int { return base + x; }
        }
        test() : int {
            a : Adder;
            a.base = 2;
            return a(40);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Call operator: a const object uses a const operator()", "[gen][operator][call-operator][const]")
{
    auto jit = gen_jit(R"SRC(
        module test;
        struct Adder {
            base : int;
            const operator()(x : int) : int { return base + x; }
        }
        bind_it(a : const Adder&) : int {
            return a(40);
        }
        test() : int {
            a : Adder;
            a.base = 2;
            return bind_it(a);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Call operator: overload resolution by arity and parameter type", "[gen][operator][call-operator][overload]")
{
    auto jit = gen_jit(R"SRC(
        module test;
        struct Multi {
            operator()(x : int) : int { return x + 2; }
            operator()(x : double) : int { return 99; }
        }
        test() : int {
            m : Multi;
            f : &(int):int = m;
            return f(40);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Call operator: binding a functor to a callable", "[gen][operator][call-operator][callable]")
{
    auto jit = gen_jit(R"SRC(
        module test;
        struct Multiplier {
            factor : int;
            operator()(x : int) : int { return factor * x; }
        }
        test() : int {
            m : Multiplier;
            m.factor = 42;
            f : &(int):int = m;
            return f(1);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Call operator: missing operator() is rejected", "[gen][operator][call-operator][error]")
{
    REQUIRE(compile_should_fail(R"SRC(
        module test;
        struct Plain { value : int; }
        test() : int {
            p : Plain;
            return p(40);
        }
    )SRC", nullptr));
}
