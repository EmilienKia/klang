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
 * Lambda lowering smoke tests.
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

TEST_CASE("Lambda: capture-free lambda binds to a callable", "[gen][lambda]") {
    auto jit = gen_jit(R"SRC(
        module gen_lambda_01;
        apply(f : *(int):int, x : int) : int { return f(x); }
        test() : int {
            return apply([](x : int) { return x + 1; }, 41);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Lambda: capture by value in borrowed callable", "[gen][lambda][capture]") {
    auto jit = gen_jit(R"SRC(
        module gen_lambda_02;
        apply(f : &(int):int, x : int) : int { return f(x); }
        test() : int {
            base : int = 10;
            return apply([base](x : int) : int { return base + x; }, 32);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Lambda: capture by value in owned callable", "[gen][lambda][owner]") {
    auto jit = gen_jit(R"SRC(
        module gen_lambda_03;
        test() : int {
            base : int = 40;
            fp : !(int):int = [base](x : int) : int { return base + x; };
            return fp(2);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Lambda: return owned capturing lambda from function", "[gen][lambda][owner][return]") {
    auto jit = gen_jit(R"SRC(
        module gen_lambda_04;
        makeAdder(base : int) : !(int):int {
            return [base](x : int) : int { return base + x; };
        }
        test() : int {
            adder : !(int):int = makeAdder(40);
            return adder(2);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Lambda: owned lambda rejecting by-reference capture of local variable", "[gen][lambda][owner][error]")
{
    REQUIRE(compile_should_fail(R"SRC(
        module gen_lambda_05;
        test() : int {
            base : int = 40;
            fp : !(int):int = [&base](x : int) : int { return base + x; };
            return fp(2);
        }
    )SRC", nullptr));
}

TEST_CASE("Lambda: move owned lambda nulls source", "[gen][lambda][owner][move]") {
    auto jit = gen_jit(R"SRC(
        module gen_lambda_06;
        test() : int {
            base : int = 30;
            f1 : !(int):int = [base](x : int) : int { return base + x; };
            f2 : !(int):int = f1; // move
            return f2(12);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Lambda: capture multiple values in owned callable", "[gen][lambda][owner][capture]") {
    auto jit = gen_jit(R"SRC(
        module gen_lambda_07;
        test() : int {
            a : int = 10;
            b : int = 20;
            c : int = 12;
            fp : !():int = [a, b, c]() : int { return a + b + c; };
            return fp();
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Lambda: capture owner object in owned lambda runs destructor on cleanup", "[gen][lambda][owner][lifecycle]") {
    auto jit = gen_jit(R"SRC(
        module gen_lambda_08;
        g_dtor_count : int = 0;
        struct Resource {
            val : int;
            Resource(v : int) { val = v; }
            ~Resource() { g_dtor_count = g_dtor_count + 1; }
        }
        test() : int {
            g_dtor_count = 0;
            {
                r : Resource! = new Resource(42);
                fp : !():int = [r]() : int { return r.val; };
                // fp goes out of scope here and drops r
            }
            return g_dtor_count;
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 1);
}

TEST_CASE("Lambda: re-assignment of owned callable cleans up old closure", "[gen][lambda][owner][assign]") {
    auto jit = gen_jit(R"SRC(
        module gen_lambda_09;
        g_dtor_count : int = 0;
        struct Resource {
            val : int;
            Resource(v : int) { val = v; }
            ~Resource() { g_dtor_count = g_dtor_count + 1; }
        }
        test() : int {
            g_dtor_count = 0;
            r1 : Resource! = new Resource(10);
            fp : !():int = [r1]() : int { return r1.val; };
            
            r2 : Resource! = new Resource(20);
            fp = [r2]() : int { return r2.val; }; // drops r1 closure
            
            return g_dtor_count;
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 1);
}

