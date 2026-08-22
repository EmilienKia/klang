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
 * Parameterised aliases: 'template<typename T> alias Vec : Array<T, 16>;'
 *
 * A parameterised alias renames a *family* of types. It is never instantiated
 * into an entity of its own: a use such as 'Vec<int>' substitutes the arguments
 * into the renamed type and resolves the result. A parameterised 'typedef'
 * keeps a nominal identity per distinct argument list.
 */

#include <catch2/catch_test_macros.hpp>

#include "helpers.hpp"

TEST_CASE("template alias — renames a template aggregate", "[gen][alias][template]") {
    auto jit = gen_jit(R"SRC(
        module gen_alias_template_01;
        template<typename T>
        struct Box { v : T; }
        template<typename T> alias BoxOf : Box<T>;
        f() : int {
            b : BoxOf<int>;
            b.v = 41;
            return b.v + 1;
        }
    )SRC");
    auto f = jit->lookup_symbol<int(*)()>("f");
    REQUIRE(f != nullptr);
    CHECK(f() == 42);
}

TEST_CASE("template alias — renames a bare parameter", "[gen][alias][template]") {
    auto jit = gen_jit(R"SRC(
        module gen_alias_template_02;
        template<typename T> alias Same : T;
        f(v : Same<int>) : Same<int> { return v * 2; }
        g() : int { return f(21); }
    )SRC");
    auto g = jit->lookup_symbol<int(*)()>("g");
    REQUIRE(g != nullptr);
    CHECK(g() == 42);
}

TEST_CASE("template alias — renames an addresser of a parameter", "[gen][alias][template]") {
    auto jit = gen_jit(R"SRC(
        module gen_alias_template_03;
        template<typename T> alias Ptr : T*;
        f() : int {
            v : int = 42;
            p : Ptr<int> = &v;
            return *p;
        }
    )SRC");
    auto f = jit->lookup_symbol<int(*)()>("f");
    REQUIRE(f != nullptr);
    CHECK(f() == 42);
}

TEST_CASE("template alias — renames an array of a parameter", "[gen][alias][template]") {
    auto jit = gen_jit(R"SRC(
        module gen_alias_template_04;
        template<typename T> alias Arr : T[3];
        f() : int {
            a : Arr<int>;
            a[0] = 40;
            a[1] = 2;
            return a[0] + a[1];
        }
    )SRC");
    auto f = jit->lookup_symbol<int(*)()>("f");
    REQUIRE(f != nullptr);
    CHECK(f() == 42);
}

TEST_CASE("template alias — partially applies a multi-parameter template", "[gen][alias][template]") {
    auto jit = gen_jit(R"SRC(
        module gen_alias_template_05;
        template<typename T, typename U>
        struct Pair { a : T; b : U; }
        template<typename U> alias IntPair : Pair<int, U>;
        struct Holder { p : IntPair<int>; }
        f() : int {
            h : Holder;
            h.p.a = 40;
            h.p.b = 2;
            return h.p.a + h.p.b;
        }
    )SRC");
    auto f = jit->lookup_symbol<int(*)()>("f");
    REQUIRE(f != nullptr);
    CHECK(f() == 42);
}

TEST_CASE("template alias — several parameters are substituted positionally", "[gen][alias][template]") {
    auto jit = gen_jit(R"SRC(
        module gen_alias_template_06;
        template<typename T, typename U>
        struct Pair { a : T; b : U; }
        template<typename A, typename B> alias P : Pair<B, A>;
        f() : long {
            p : P<int, long>;
            p.a = 40;
            p.b = 2;
            return p.a + (long)p.b;
        }
    )SRC");
    auto f = jit->lookup_symbol<long(*)()>("f");
    REQUIRE(f != nullptr);
    CHECK(f() == 42);
}

TEST_CASE("template alias — a default template argument is applied", "[gen][alias][template]") {
    auto jit = gen_jit(R"SRC(
        module gen_alias_template_07;
        template<typename T>
        struct Box { v : T; }
        template<typename T = int> alias BoxOf : Box<T>;
        f() : int {
            b : BoxOf<>;
            b.v = 42;
            return b.v;
        }
    )SRC");
    auto f = jit->lookup_symbol<int(*)()>("f");
    REQUIRE(f != nullptr);
    CHECK(f() == 42);
}

TEST_CASE("template alias — renames another parameterised alias", "[gen][alias][template]") {
    auto jit = gen_jit(R"SRC(
        module gen_alias_template_08;
        template<typename T> alias Ptr : T*;
        template<typename T> alias Handle : Ptr<T>;
        f() : int {
            v : int = 42;
            h : Handle<int> = &v;
            return *h;
        }
    )SRC");
    auto f = jit->lookup_symbol<int(*)()>("f");
    REQUIRE(f != nullptr);
    CHECK(f() == 42);
}

TEST_CASE("template alias — a nested template argument is substituted", "[gen][alias][template]") {
    auto jit = gen_jit(R"SRC(
        module gen_alias_template_09;
        template<typename T> struct Box { v : T; }
        template<typename T, typename U> struct Pair { a : T; b : U; }
        template<typename T> alias BoxPair : Pair<Box<T>, int>;
        f() : int {
            p : BoxPair<int>;
            p.a.v = 40;
            p.b = 2;
            return p.a.v + p.b;
        }
    )SRC");
    auto f = jit->lookup_symbol<int(*)()>("f");
    REQUIRE(f != nullptr);
    CHECK(f() == 42);
}

TEST_CASE("template alias — a nested alias argument is substituted", "[gen][alias][template]") {
    auto jit = gen_jit(R"SRC(
        module gen_alias_template_10;
        template<typename T> struct Box { v : T; }
        template<typename T> alias BoxOf : Box<T>;
        template<typename T> alias BoxOfBox : BoxOf<Box<T>>;
        f() : int {
            b : BoxOfBox<int>;
            b.v.v = 42;
            return b.v.v;
        }
    )SRC");
    auto f = jit->lookup_symbol<int(*)()>("f");
    REQUIRE(f != nullptr);
    CHECK(f() == 42);
}

TEST_CASE("template typedef — an explicit cast converts from the renamed type", "[gen][typedef][template]") {
    auto jit = gen_jit(R"SRC(
        module gen_alias_template_11;
        template<typename T> typedef Id : T;
        f() : int {
            n : int = 40;
            a : Id<int> = (Id<int>)n;
            b : Id<int> = 2;
            return (int)a + (int)b;
        }
    )SRC");
    auto f = jit->lookup_symbol<int(*)()>("f");
    REQUIRE(f != nullptr);
    CHECK(f() == 42);
}

TEST_CASE("template typedef — the renamed type accepts the strong alias without a cast", "[gen][typedef][template]") {
    auto jit = gen_jit(R"SRC(
        module gen_alias_template_12;
        template<typename T> typedef Id : T;
        f() : int {
            a : Id<int> = 42;
            n : int = a;
            return n;
        }
    )SRC");
    auto f = jit->lookup_symbol<int(*)()>("f");
    REQUIRE(f != nullptr);
    CHECK(f() == 42);
}

TEST_CASE("template alias — too many template arguments are rejected", "[gen][alias][template]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module gen_alias_template_13;
        template<typename T> alias Same : T;
        f(v : Same<int, long>) : int { return 0; }
    )SRC"), k::log::compiler_error);
}

TEST_CASE("template alias — a missing template argument is rejected", "[gen][alias][template]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module gen_alias_template_14;
        template<typename T, typename U> alias Two : T;
        f(v : Two<int>) : int { return 0; }
    )SRC"), k::log::compiler_error);
}

TEST_CASE("template alias — arguments given to a plain alias are rejected", "[gen][alias][template]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module gen_alias_template_15;
        alias Plain : int;
        f(v : Plain<int>) : int { return 0; }
    )SRC"), k::log::compiler_error);
}

TEST_CASE("template alias — a value template parameter is rejected", "[gen][alias][template]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module gen_alias_template_16;
        template<int N> alias Buf : int;
        f(v : Buf<4>) : int { return 0; }
    )SRC"), k::log::compiler_error);
}

TEST_CASE("template alias — an unknown target is rejected", "[gen][alias][template]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module gen_alias_template_17;
        template<typename T> alias Nope : DoesNotExist<T>;
        f(v : Nope<int>) : int { return 0; }
    )SRC"), k::log::compiler_error);
}
