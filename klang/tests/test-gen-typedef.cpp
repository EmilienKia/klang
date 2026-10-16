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

TEST_CASE("Typedef cast resolution preserves nominal explicit-cast types",
          "[gen][typedef][model][typedef-cast-stability][regression]") {
    auto comp = compile_model(R"SRC(
        module typedef_cast_stability;
        typedef Token : int;
        struct Box { Box(value : int) {} }
        probe(value : int) { Box((Token)value); }
    )SRC");
    REQUIRE(comp);
    auto root = comp->get_unit()->get_root_namespace();
    auto token = root->get_alias("Token");
    REQUIRE(token);
    auto probe = root->get_function("probe");
    REQUIRE(probe);
    REQUIRE(probe->get_block());
    std::shared_ptr<k::model::temporary_construction_expression> temporary;
    for (const auto& stmt : probe->get_block()->get_statements()) {
        if (auto expression = std::dynamic_pointer_cast<k::model::expression_statement>(stmt)) {
            temporary = std::dynamic_pointer_cast<k::model::temporary_construction_expression>(
                expression->get_expression());
            if (temporary) break;
        }
    }
    REQUIRE(temporary);
    REQUIRE(temporary->size() == 1);
    unsigned nominal_casts = 0;
    auto argument = temporary->argument(0);
    while (auto unary = std::dynamic_pointer_cast<k::model::unary_expression>(argument)) {
        if (auto cast = std::dynamic_pointer_cast<k::model::cast_expression>(argument);
            cast && k::model::type::are_equal(cast->get_type(), token->get_alias_type())) {
            ++nominal_casts;
            CHECK(k::model::type::are_equal(cast->get_cast_type(),
                k::model::type::canonical(token->get_alias_type())));
        }
        argument = unary->sub_expr();
    }
    CHECK(nominal_casts == 1);
}

TEST_CASE("Typedef owner initialization preserves the heap referent",
          "[gen][typedef][owner][typedef-owner-init][regression]") {
    auto jit = gen_jit(R"SRC(
        module typedef_owner_initialization;
        dropped : int = 0;
        struct Resource {
            value : int;
            Resource(v : int) : value(v) {}
            ~Resource() { ++dropped; }
        }
        typedef Owned : Resource!;
        probe() : int {
            dropped = 0;
            source : Owned = new Resource(42);
            if (source.value != 42 || (Resource*)source == null) return -1;
            ordinary : Resource! = (Resource!)source;
            if ((Resource*)source != null) return -2;
            if (ordinary.value != 42 || dropped != 0) return -3;
            return ordinary.value;
        }
        drops() : int { return dropped; }
    )SRC");
    REQUIRE(jit);
    auto probe = jit->lookup_symbol<int(*)()>("probe");
    auto drops = jit->lookup_symbol<int(*)()>("drops");
    REQUIRE(probe);
    REQUIRE(drops);
    CHECK(probe() == 42);
    CHECK(drops() == 1);
}

TEST_CASE("Typedef owned locals destroy resources on normal exit and unwind",
          "[gen][typedef][owner][exceptions][typedef-owner-cleanup][regression]") {
    auto jit = gen_jit(R"SRC(
        module typedef_owner_cleanup;
        dropped : int = 0;
        order : int = 0;
        struct Resource {
            value : int;
            Resource(v : int) : value(v) {}
            ~Resource() { ++dropped; order = order * 10 + value; }
        }
        typedef Owned : Resource!;
        fail() { throw Exception(891); }
        normal() : int {
            dropped = 0;
            order = 0;
            {
                a : Resource! = new Resource(1);
                first : Owned = a;
                b : Resource! = new Resource(2);
                second : Owned = b;
                if ((Resource*)a != null || (Resource*)b != null) return -1;
                if (first.value != 1 || second.value != 2 || dropped != 0) return -2;
            }
            return dropped * 100 + order;
        }
        unwind(beforeDeclaration : bool) : int {
            dropped = 0;
            order = 0;
            try {
                if (beforeDeclaration) fail();
                a : Resource! = new Resource(3);
                first : Owned = a;
                b : Resource! = new Resource(4);
                second : Owned = b;
                fail();
            } catch (error : Throwable&) {
                if (error.getCode() != 891) return -1;
            }
            return dropped * 100 + order;
        }
    )SRC");
    REQUIRE(jit);
    auto normal = jit->lookup_symbol<int(*)()>("normal");
    auto unwind = jit->lookup_symbol<int(*)(bool)>("unwind");
    REQUIRE(normal);
    REQUIRE(unwind);
    CHECK(normal() == 221);
    CHECK(unwind(false) == 243);
    CHECK(unwind(true) == 0);
}
