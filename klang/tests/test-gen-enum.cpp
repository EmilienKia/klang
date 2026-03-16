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

#include "../src/common/logger.hpp"
#include "../src/parse/parser.hpp"
#include "../src/model/model.hpp"
#include "../src/gen/generators.hpp"
#include "../src/compiler.hpp"

#include "helpers.hpp"

using namespace k::parse;
using namespace k::parse::ast;

// ============================================================
// Phase 1: Parser tests for enum declarations
// ============================================================

TEST_CASE("Parse enum — simple enum with explicit values", "[parser][enum]") {
    test_logger log;
    k::source src{"enum Color { RED = 0; GREEN = 1; BLUE = 2; };"};
    k::parse::parser parser(log, src);
    auto decl = parser.parse_enum_decl();
    REQUIRE(decl);
    CHECK(std::string{decl->name.content} == "Color");
    REQUIRE(decl->entries.size() == 3);
    CHECK(std::string{decl->entries[0]->name.content} == "RED");
    CHECK(std::string{decl->entries[1]->name.content} == "GREEN");
    CHECK(std::string{decl->entries[2]->name.content} == "BLUE");
    CHECK(decl->entries[0]->is_default == false);
    CHECK(decl->entries[1]->is_default == false);
    CHECK(decl->entries[2]->is_default == false);
}

TEST_CASE("Parse enum — auto-increment values", "[parser][enum]") {
    test_logger log;
    k::source src{"enum Dir { NORTH; SOUTH; EAST; WEST; };"};
    k::parse::parser parser(log, src);
    auto decl = parser.parse_enum_decl();
    REQUIRE(decl);
    CHECK(std::string{decl->name.content} == "Dir");
    REQUIRE(decl->entries.size() == 4);
    // No explicit values — parser doesn't resolve, just records absence
    CHECK(!decl->entries[0]->literal_value.has_value());
    CHECK(!decl->entries[0]->ref_value.has_value());
}

TEST_CASE("Parse enum — default keyword", "[parser][enum]") {
    test_logger log;
    k::source src{"enum Status { OK = 0; ERR = 1 default; WARN = 2; };"};
    k::parse::parser parser(log, src);
    auto decl = parser.parse_enum_decl();
    REQUIRE(decl);
    REQUIRE(decl->entries.size() == 3);
    CHECK(decl->entries[0]->is_default == false);
    CHECK(decl->entries[1]->is_default == true);
    CHECK(decl->entries[2]->is_default == false);
}

TEST_CASE("Parse enum — reference to another entry", "[parser][enum]") {
    test_logger log;
    k::source src{"enum X { A = 1; B = A; };"};
    k::parse::parser parser(log, src);
    auto decl = parser.parse_enum_decl();
    REQUIRE(decl);
    REQUIRE(decl->entries.size() == 2);
    CHECK(decl->entries[1]->ref_value.has_value());
    CHECK(std::string{decl->entries[1]->ref_value->content} == "A");
}

TEST_CASE("Parse enum — empty enum", "[parser][enum]") {
    test_logger log;
    k::source src{"enum Empty { };"};
    k::parse::parser parser(log, src);
    auto decl = parser.parse_enum_decl();
    REQUIRE(decl);
    CHECK(decl->entries.empty());
}

// ============================================================
// Phase 2: JIT tests — basic enum construction and access
// ============================================================

TEST_CASE("Enum — qualified access MonEnum::entry", "[gen][enum]") {
    auto jit = gen_jit(R"(
        module test;
        enum Color {
            RED = 0;
            GREEN = 1;
            BLUE = 2;
        };
        get_red() : int {
            c : Color = Color::RED;
            return c;
        }
        get_green() : int {
            c : Color = Color::GREEN;
            return c;
        }
        get_blue() : int {
            c : Color = Color::BLUE;
            return c;
        }
    )");
    REQUIRE(jit);
    auto get_red = jit->lookup_symbol<int(*)()>("get_red");
    auto get_green = jit->lookup_symbol<int(*)()>("get_green");
    auto get_blue = jit->lookup_symbol<int(*)()>("get_blue");
    REQUIRE(get_red);
    REQUIRE(get_green);
    REQUIRE(get_blue);
    CHECK(get_red() == 0);
    CHECK(get_green() == 1);
    CHECK(get_blue() == 2);
}

TEST_CASE("Enum — constructor with entry name MonEnum(entry)", "[gen][enum]") {
    auto jit = gen_jit(R"(
        module test;
        enum Color {
            RED = 0;
            GREEN = 1;
            BLUE = 2;
        };
        get() : int {
            c : Color(GREEN);
            return c;
        }
    )");
    REQUIRE(jit);
    auto get = jit->lookup_symbol<int(*)()>("get");
    REQUIRE(get);
    CHECK(get() == 1);
}

TEST_CASE("Enum — constructor with numeric value MonEnum(3)", "[gen][enum]") {
    auto jit = gen_jit(R"(
        module test;
        enum Color {
            RED = 0;
            GREEN = 1;
            BLUE = 2;
        };
        get() : int {
            c : Color(2);
            return c;
        }
    )");
    REQUIRE(jit);
    auto get = jit->lookup_symbol<int(*)()>("get");
    REQUIRE(get);
    CHECK(get() == 2);
}

TEST_CASE("Enum — default construction", "[gen][enum]") {
    auto jit = gen_jit(R"(
        module test;
        enum Status {
            OK = 0;
            ERR = 1 default;
            WARN = 2;
        };
        get() : int {
            s : Status;
            return s;
        }
    )");
    REQUIRE(jit);
    auto get = jit->lookup_symbol<int(*)()>("get");
    REQUIRE(get);
    CHECK(get() == 1);  // ERR is the default
}

TEST_CASE("Enum — default construction uses first entry when no explicit default", "[gen][enum]") {
    auto jit = gen_jit(R"(
        module test;
        enum Color {
            RED = 10;
            GREEN = 20;
            BLUE = 30;
        };
        get() : int {
            c : Color;
            return c;
        }
    )");
    REQUIRE(jit);
    auto get = jit->lookup_symbol<int(*)()>("get");
    REQUIRE(get);
    CHECK(get() == 10);  // RED is first, so it's the default
}

TEST_CASE("Enum — assignment from numeric value", "[gen][enum]") {
    auto jit = gen_jit(R"(
        module test;
        enum Color {
            RED = 0;
            GREEN = 1;
            BLUE = 2;
        };
        get() : int {
            c : Color = 2;
            return c;
        }
    )");
    REQUIRE(jit);
    auto get = jit->lookup_symbol<int(*)()>("get");
    REQUIRE(get);
    CHECK(get() == 2);
}

// ============================================================
// Phase 3: Auto-increment and alias support
// ============================================================

TEST_CASE("Enum — auto-increment values", "[gen][enum]") {
    auto jit = gen_jit(R"(
        module test;
        enum Dir {
            NORTH;
            SOUTH;
            EAST;
            WEST;
        };
        get_north() : int { return Dir::NORTH; }
        get_south() : int { return Dir::SOUTH; }
        get_east() : int { return Dir::EAST; }
        get_west() : int { return Dir::WEST; }
    )");
    REQUIRE(jit);
    auto get_north = jit->lookup_symbol<int(*)()>("get_north");
    auto get_south = jit->lookup_symbol<int(*)()>("get_south");
    auto get_east = jit->lookup_symbol<int(*)()>("get_east");
    auto get_west = jit->lookup_symbol<int(*)()>("get_west");
    REQUIRE(get_north);
    REQUIRE(get_south);
    REQUIRE(get_east);
    REQUIRE(get_west);
    CHECK(get_north() == 0);
    CHECK(get_south() == 1);
    CHECK(get_east() == 2);
    CHECK(get_west() == 3);
}

TEST_CASE("Enum — auto-increment after explicit value", "[gen][enum]") {
    auto jit = gen_jit(R"(
        module test;
        enum E {
            A = 5;
            B;
            C;
            D = 10;
            E_;
        };
        get_a() : int { return E::A; }
        get_b() : int { return E::B; }
        get_c() : int { return E::C; }
        get_d() : int { return E::D; }
        get_e() : int { return E::E_; }
    )");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<int(*)()>("get_a")() == 5);
    CHECK(jit->lookup_symbol<int(*)()>("get_b")() == 6);
    CHECK(jit->lookup_symbol<int(*)()>("get_c")() == 7);
    CHECK(jit->lookup_symbol<int(*)()>("get_d")() == 10);
    CHECK(jit->lookup_symbol<int(*)()>("get_e")() == 11);
}

TEST_CASE("Enum — alias entries (same value)", "[gen][enum]") {
    auto jit = gen_jit(R"(
        module test;
        enum E {
            A = 1;
            B = A;
        };
        get_a() : int { return E::A; }
        get_b() : int { return E::B; }
        check_equal() : int {
            a : E = E::A;
            b : E = E::B;
            if (a == b) { return 1; }
            return 0;
        }
    )");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<int(*)()>("get_a")() == 1);
    CHECK(jit->lookup_symbol<int(*)()>("get_b")() == 1);
    CHECK(jit->lookup_symbol<int(*)()>("check_equal")() == 1);
}

// ============================================================
// Phase 4: Comparison operators
// ============================================================

TEST_CASE("Enum — equality operator", "[gen][enum]") {
    auto jit = gen_jit(R"(
        module test;
        enum Color {
            RED = 0;
            GREEN = 1;
            BLUE = 2;
        };
        test_eq() : int {
            a : Color = Color::RED;
            b : Color = Color::RED;
            if (a == b) { return 1; }
            return 0;
        }
        test_neq() : int {
            a : Color = Color::RED;
            b : Color = Color::GREEN;
            if (a != b) { return 1; }
            return 0;
        }
    )");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<int(*)()>("test_eq")() == 1);
    CHECK(jit->lookup_symbol<int(*)()>("test_neq")() == 1);
}

TEST_CASE("Enum — relational operators", "[gen][enum]") {
    auto jit = gen_jit(R"(
        module test;
        enum Priority {
            LOW = 1;
            MEDIUM = 5;
            HIGH = 10;
        };
        test_less() : int {
            a : Priority = Priority::LOW;
            b : Priority = Priority::HIGH;
            if (a < b) { return 1; }
            return 0;
        }
        test_greater() : int {
            a : Priority = Priority::HIGH;
            b : Priority = Priority::LOW;
            if (a > b) { return 1; }
            return 0;
        }
        test_leq() : int {
            a : Priority = Priority::MEDIUM;
            b : Priority = Priority::MEDIUM;
            if (a <= b) { return 1; }
            return 0;
        }
        test_geq() : int {
            a : Priority = Priority::HIGH;
            b : Priority = Priority::MEDIUM;
            if (a >= b) { return 1; }
            return 0;
        }
    )");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<int(*)()>("test_less")() == 1);
    CHECK(jit->lookup_symbol<int(*)()>("test_greater")() == 1);
    CHECK(jit->lookup_symbol<int(*)()>("test_leq")() == 1);
    CHECK(jit->lookup_symbol<int(*)()>("test_geq")() == 1);
}

// ============================================================
// Phase 5: Enum ↔ int implicit conversions
// ============================================================

TEST_CASE("Enum — implicit conversion to int", "[gen][enum]") {
    auto jit = gen_jit(R"(
        module test;
        enum Color {
            RED = 0;
            GREEN = 1;
            BLUE = 2;
        };
        to_int() : int {
            c : Color = Color::BLUE;
            result : int = c;
            return result;
        }
    )");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<int(*)()>("to_int")() == 2);
}

TEST_CASE("Enum — pass enum to function expecting int", "[gen][enum]") {
    auto jit = gen_jit(R"(
        module test;
        enum Color {
            RED = 0;
            GREEN = 1;
            BLUE = 2;
        };
        identity(x : int) : int { return x; }
        get() : int {
            c : Color = Color::GREEN;
            return identity(c);
        }
    )");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<int(*)()>("get")() == 1);
}

TEST_CASE("Enum — return enum from function returning int", "[gen][enum]") {
    auto jit = gen_jit(R"(
        module test;
        enum Color {
            RED = 0;
            GREEN = 1;
            BLUE = 2;
        };
        get_color() : Color {
            return Color::BLUE;
        }
        get() : int {
            c : Color = get_color();
            return c;
        }
    )");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<int(*)()>("get")() == 2);
}

// ============================================================
// Phase 6: Mixed usage and edge cases
// ============================================================

TEST_CASE("Enum — use in if-else chain", "[gen][enum]") {
    auto jit = gen_jit(R"(
        module test;
        enum Color {
            RED = 0;
            GREEN = 1;
            BLUE = 2;
        };
        describe(c : Color) : int {
            if (c == Color::RED) { return 10; }
            if (c == Color::GREEN) { return 20; }
            if (c == Color::BLUE) { return 30; }
            return -1;
        }
        get_red() : int { return describe(Color::RED); }
        get_green() : int { return describe(Color::GREEN); }
        get_blue() : int { return describe(Color::BLUE); }
    )");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<int(*)()>("get_red")() == 10);
    CHECK(jit->lookup_symbol<int(*)()>("get_green")() == 20);
    CHECK(jit->lookup_symbol<int(*)()>("get_blue")() == 30);
}

TEST_CASE("Enum — constructor form with qualified entry", "[gen][enum]") {
    auto jit = gen_jit(R"(
        module test;
        enum Color {
            RED = 0;
            GREEN = 1;
            BLUE = 2;
        };
        get() : int {
            c : Color = Color::GREEN;
            return c;
        }
    )");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<int(*)()>("get")() == 1);
}

TEST_CASE("Enum — multiple enums in same module", "[gen][enum]") {
    auto jit = gen_jit(R"(
        module test;
        enum Color {
            RED = 0;
            GREEN = 1;
        };
        enum Shape {
            CIRCLE = 10;
            SQUARE = 20;
        };
        get_color() : int {
            c : Color = Color::GREEN;
            return c;
        }
        get_shape() : int {
            s : Shape = Shape::SQUARE;
            return s;
        }
    )");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<int(*)()>("get_color")() == 1);
    CHECK(jit->lookup_symbol<int(*)()>("get_shape")() == 20);
}

TEST_CASE("Enum — entry with default and explicit value", "[gen][enum]") {
    auto jit = gen_jit(R"(
        module test;
        enum E {
            A = 10;
            B = 20 default;
            C = 30;
        };
        get_default() : int {
            e : E;
            return e;
        }
        get_a() : int { return E::A; }
        get_b() : int { return E::B; }
        get_c() : int { return E::C; }
    )");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<int(*)()>("get_default")() == 20);
    CHECK(jit->lookup_symbol<int(*)()>("get_a")() == 10);
    CHECK(jit->lookup_symbol<int(*)()>("get_b")() == 20);
    CHECK(jit->lookup_symbol<int(*)()>("get_c")() == 30);
}

TEST_CASE("Enum — auto-increment produces duplicate values (allowed)", "[gen][enum]") {
    auto jit = gen_jit(R"(
        module test;
        enum E {
            A = 1;
            B;
            C = 1;
            D;
        };
        get_a() : int { return E::A; }
        get_b() : int { return E::B; }
        get_c() : int { return E::C; }
        get_d() : int { return E::D; }
    )");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<int(*)()>("get_a")() == 1);
    CHECK(jit->lookup_symbol<int(*)()>("get_b")() == 2);
    CHECK(jit->lookup_symbol<int(*)()>("get_c")() == 1);  // Same as A
    CHECK(jit->lookup_symbol<int(*)()>("get_d")() == 2);  // Same as B
}

TEST_CASE("Enum — comparison between enum and integer literal", "[gen][enum]") {
    auto jit = gen_jit(R"(
        module test;
        enum Color {
            RED = 0;
            GREEN = 1;
            BLUE = 2;
        };
        check() : int {
            c : Color = Color::GREEN;
            if (c == 1) { return 1; }
            return 0;
        }
    )");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<int(*)()>("check")() == 1);
}

