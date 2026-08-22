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

/*
 * Soft aliases: 'alias Name : aliased_symbol;'
 *
 * A soft alias is a fully transparent second name for an existing type,
 * function or global variable. It synthesises no symbol of its own: the alias
 * and the aliased entity are interchangeable in both directions, with no cast.
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

TEST_CASE("alias — soft alias of a primitive type", "[gen][alias]") {
    auto jit = gen_jit(R"SRC(
        module gen_alias_soft_01;
        alias num : int;
        f(a : num, b : int) : num {
            n : num = a + b;
            m : int = n;
            return m;
        }
    )SRC");
    auto f = jit->lookup_symbol<int(*)(int,int)>("f");
    REQUIRE(f != nullptr);
    CHECK(f(3, 4) == 7);
}

TEST_CASE("alias — soft alias of a struct type", "[gen][alias]") {
    auto jit = gen_jit(R"SRC(
        module gen_alias_soft_02;
        struct Point { x : int; y : int; }
        alias P : Point;
        f() : int {
            p : P;
            p.x = 11;
            p.y = 22;
            q : Point = p;
            return q.x + q.y;
        }
    )SRC");
    auto f = jit->lookup_symbol<int(*)()>("f");
    REQUIRE(f != nullptr);
    CHECK(f() == 33);
}

TEST_CASE("alias — soft alias of a global variable", "[gen][alias]") {
    auto jit = gen_jit(R"SRC(
        module gen_alias_soft_03;
        gval : int = 7;
        alias galias : gval;
        f() : int { return galias; }
    )SRC");
    auto f = jit->lookup_symbol<int(*)()>("f");
    REQUIRE(f != nullptr);
    CHECK(f() == 7);
}

TEST_CASE("alias — soft alias of a function", "[gen][alias]") {
    auto jit = gen_jit(R"SRC(
        module gen_alias_soft_04;
        add(a : int, b : int) : int { return a + b; }
        alias plus : add;
        f() : int { return plus(20, 22); }
    )SRC");
    auto f = jit->lookup_symbol<int(*)()>("f");
    REQUIRE(f != nullptr);
    CHECK(f() == 42);
}

TEST_CASE("alias — chained soft aliases resolve to the final type", "[gen][alias]") {
    auto jit = gen_jit(R"SRC(
        module gen_alias_soft_05;
        alias a1 : int;
        alias a2 : a1;
        alias a3 : a2;
        f(v : a3) : int { return v + 1; }
    )SRC");
    auto f = jit->lookup_symbol<int(*)(int)>("f");
    REQUIRE(f != nullptr);
    CHECK(f(41) == 42);
}

TEST_CASE("alias — soft alias inside a statement block", "[gen][alias]") {
    auto jit = gen_jit(R"SRC(
        module gen_alias_soft_06;
        f() : int {
            alias num : int;
            n : num = 5;
            return n * 2;
        }
    )SRC");
    auto f = jit->lookup_symbol<int(*)()>("f");
    REQUIRE(f != nullptr);
    CHECK(f() == 10);
}

TEST_CASE("alias — soft alias inside a namespace", "[gen][alias]") {
    auto jit = gen_jit(R"SRC(
        module gen_alias_soft_07;
        namespace ns {
            alias num : int;
            f(v : num) : num { return v + 1; }
        }
        g() : int { return ns::f(1); }
    )SRC");
    auto g = jit->lookup_symbol<int(*)()>("g");
    REQUIRE(g != nullptr);
    CHECK(g() == 2);
}

TEST_CASE("alias — aliasing a namespace is forbidden", "[gen][alias]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module gen_alias_soft_08;
        namespace deep { f() : int { return 1; } }
        alias d : deep;
        g() : int { return 0; }
    )SRC"), k::log::compiler_error);
}

TEST_CASE("alias — an unknown alias target is rejected", "[gen][alias]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module gen_alias_soft_09;
        alias what : DoesNotExist;
        f(v : what) : int { return 0; }
    )SRC"), k::log::compiler_error);
}
