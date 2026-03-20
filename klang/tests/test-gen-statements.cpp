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

//
// If-then-else
//

TEST_CASE("If-then-else", "[gen][if-else]") {
    auto jit = gen_jit(R"SRC(
        module __if__;
        min(a: int, b: int) : int {
            if(a<b)
                return a;
            else
                return b;
        }
        max(a: int, b: int) : int {
            if(a>b) {
                return a;
            } else {
                return b;
            }
        }
        fibo(i: unsigned short) : unsigned long {
            if(i==0) return 1;
            else if(i==1) return 1;
            return fibo(i-1) + fibo(i-2);
        }
        )SRC");
    REQUIRE(jit);

    SECTION("if-then-else simple return statement") {
        auto min = jit->lookup_symbol<int(*)(int,int)>("min");
        REQUIRE(min != nullptr);
        REQUIRE(min(4,2) == 2);
        REQUIRE(min(2,4) == 2);
    }

    SECTION("if-then-else with blocks") {
        auto max = jit->lookup_symbol<int(*)(int,int)>("max");
        REQUIRE(max != nullptr);
        REQUIRE(max(4,2) == 4);
        REQUIRE(max(2,4) == 4);
    }

    SECTION("if-then-else with nested if and no else") {
        auto fibo = jit->lookup_symbol<uint64_t(*)(unsigned short)>("fibo");
        REQUIRE(fibo != nullptr);
        REQUIRE(fibo(0) == 1);
        REQUIRE(fibo(1) == 1);
        REQUIRE(fibo(2) == 2);
        REQUIRE(fibo(3) == 3);
        REQUIRE(fibo(4) == 5);
        REQUIRE(fibo(5) == 8);
    }
}

//
// While
//

TEST_CASE("While", "[gen][while]") {
    auto jit = gen_jit(R"SRC(
        module __while__;
        cumul(i : int) : int {
            r : int;
            r = 0;
            while(i>0) {
                r += i;
                i = i - 1;
            }
            return r;
        }
        )SRC");
    REQUIRE(jit);

    SECTION("while simple statement") {
        auto cumul = jit->lookup_symbol<int(*)(int)>("cumul");
        REQUIRE(cumul != nullptr);
        REQUIRE(cumul(0) == 0);
        REQUIRE(cumul(1) == 1);
        REQUIRE(cumul(2) == 3);
        REQUIRE(cumul(3) == 6);
        REQUIRE(cumul(4) == 10);
        REQUIRE(cumul(5) == 15);
    }
}

//
// For
//

TEST_CASE("For", "[gen][for]") {
    auto jit = gen_jit(R"SRC(
        module __for__;
        sum(i : short) : int {
            r : int;
            r = 0;
            for(n: short = 0; n<i; n+=1) {
                r += n;
            }
            return r;
        }
        )SRC");
    REQUIRE(jit);

    SECTION("for simple statement") {
        auto sum = jit->lookup_symbol<int(*)(short)>("sum");
        REQUIRE(sum != nullptr);
        REQUIRE(sum(0) == 0);
        REQUIRE(sum(1) == 0);
        REQUIRE(sum(2) == 1);
        REQUIRE(sum(3) == 3);
        REQUIRE(sum(4) == 6);
        REQUIRE(sum(5) == 10);
    }
}
