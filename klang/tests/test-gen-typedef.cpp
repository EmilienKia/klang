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
 * Strong aliases: 'typedef Name : aliased_type;'
 *
 * A typedef introduces a nominally distinct type over an identical
 * representation. Conversion typedef -> underlying is implicit; the reverse
 * needs an explicit cast, except:
 *   - in a variable definition, where the type is spelled out just before, and
 *   - for compile-time literals, and
 *   - inside an expression already tainted by the typedef (expression locality).
 * A typedef always mangles to its underlying type, so it never distinguishes
 * an overload.
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

TEST_CASE("typedef — declaration, literal init and implicit widening to base", "[gen][typedef]") {
    auto jit = gen_jit(R"SRC(
        module gen_typedef_01;
        typedef identifier : int;
        f() : int {
            id : identifier = 4;
            id = 8;
            n : int = id;
            return n;
        }
    )SRC");
    auto f = jit->lookup_symbol<int(*)()>("f");
    REQUIRE(f != nullptr);
    CHECK(f() == 8);
}

TEST_CASE("typedef — explicit cast from the underlying type", "[gen][typedef]") {
    auto jit = gen_jit(R"SRC(
        module gen_typedef_02;
        typedef identifier : int;
        f(n : int) : int {
            id : identifier = (identifier) n;
            return id;
        }
    )SRC");
    auto f = jit->lookup_symbol<int(*)(int)>("f");
    REQUIRE(f != nullptr);
    CHECK(f(17) == 17);
}

TEST_CASE("typedef — assigning the underlying type without a cast is an error", "[gen][typedef]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module gen_typedef_03;
        typedef identifier : int;
        f(n : int) : int {
            id : identifier = 0;
            id = n;
            return id;
        }
    )SRC"), k::log::compiler_error);
}

TEST_CASE("typedef — expression locality: a tainted expression needs no cast", "[gen][typedef]") {
    auto jit = gen_jit(R"SRC(
        module gen_typedef_04;
        typedef identifier : int;
        f() : int {
            id : identifier = 10;
            next : identifier = id + 1;
            id = next * 2;
            return id;
        }
    )SRC");
    auto f = jit->lookup_symbol<int(*)()>("f");
    REQUIRE(f != nullptr);
    CHECK(f() == 22);
}

TEST_CASE("typedef — variable definition accepts the underlying type", "[gen][typedef]") {
    auto jit = gen_jit(R"SRC(
        module gen_typedef_05;
        typedef identifier : int;
        f(n : int) : int {
            id : identifier = n;
            return id;
        }
    )SRC");
    auto f = jit->lookup_symbol<int(*)(int)>("f");
    REQUIRE(f != nullptr);
    CHECK(f(5) == 5);
}

TEST_CASE("typedef — typedef of a struct type", "[gen][typedef]") {
    auto jit = gen_jit(R"SRC(
        module gen_typedef_06;
        struct Point { x : int; y : int; }
        typedef Coord : Point;
        f() : int {
            c : Coord;
            c.x = 3;
            c.y = 4;
            p : Point = c;
            return p.x * p.y;
        }
    )SRC");
    auto f = jit->lookup_symbol<int(*)()>("f");
    REQUIRE(f != nullptr);
    CHECK(f() == 12);
}

TEST_CASE("typedef — overloading on a typedef is forbidden", "[gen][typedef]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module gen_typedef_07;
        typedef identifier : int;
        f(v : int) : int { return 1; }
        f(v : identifier) : int { return 2; }
        g() : int { return f(0); }
    )SRC"), k::log::compiler_error);
}

TEST_CASE("typedef — parameter and return use the typedef", "[gen][typedef]") {
    auto jit = gen_jit(R"SRC(
        module gen_typedef_08;
        typedef identifier : int;
        inc(id : identifier) : identifier { return id + 1; }
        f() : int {
            id : identifier = 41;
            return inc(id);
        }
    )SRC");
    auto f = jit->lookup_symbol<int(*)()>("f");
    REQUIRE(f != nullptr);
    CHECK(f() == 42);
}

TEST_CASE("typedef — a typedef mangles with its underlying type", "[gen][typedef]") {
    auto jit = gen_jit(R"SRC(
        module gen_typedef_09;
        typedef identifier : int;
        plain(v : int) : int { return v; }
        tagged(v : identifier) : identifier { return v; }
    )SRC");
    // Both functions must be reachable; the typedef'd one is mangled as if it
    // took a plain 'int', so its symbol shape must match the plain one's.
    CHECK(jit->lookup_symbol<int(*)(int)>("plain") != nullptr);
    CHECK(jit->lookup_symbol<int(*)(int)>("tagged") != nullptr);
}

TEST_CASE("typedef — typedef inside a statement block", "[gen][typedef]") {
    auto jit = gen_jit(R"SRC(
        module gen_typedef_10;
        f() : int {
            typedef identifier : int;
            id : identifier = 6;
            n : int = id;
            return n * 7;
        }
    )SRC");
    auto f = jit->lookup_symbol<int(*)()>("f");
    REQUIRE(f != nullptr);
    CHECK(f() == 42);
}

TEST_CASE("typedef — soft alias over a typedef stays nominally distinct", "[gen][typedef][alias]") {
    auto jit = gen_jit(R"SRC(
        module gen_typedef_11;
        typedef identifier : int;
        alias id_t : identifier;
        f() : int {
            a : id_t = 3;
            b : identifier = a;
            n : int = b;
            return n;
        }
    )SRC");
    auto f = jit->lookup_symbol<int(*)()>("f");
    REQUIRE(f != nullptr);
    CHECK(f() == 3);
}

TEST_CASE("typedef — composite scenario mixing alias and typedef", "[gen][typedef][alias]") {
    auto jit = gen_jit(R"SRC(
        module gen_typedef_12;
        typedef identifier : int;
        alias num : int;
        gval : int = 7;
        alias galias : gval;
        add(a : int, b : int) : int { return a + b; }
        alias plus : add;
        f(): int {
            n : num = 4;
            id : identifier = 1;
            id = (identifier) n;
            m : int = id;
            id2 : identifier = id + 1;
            return m + galias + plus(1, 2) + id2;
        }
    )SRC");
    auto f = jit->lookup_symbol<int(*)()>("f");
    REQUIRE(f != nullptr);
    CHECK(f() == 19);
}
