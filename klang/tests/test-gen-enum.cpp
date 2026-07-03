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

#include "../src/errors.hpp"
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

TEST_CASE("Parse enum — no trailing semicolon required", "[parser][enum]") {
    test_logger log;
    k::source src{"enum Color { RED = 0; GREEN = 1; BLUE = 2; }"};
    k::parse::parser parser(log, src);
    auto decl = parser.parse_enum_decl();
    REQUIRE(decl);
    CHECK(std::string{decl->name.content} == "Color");
    REQUIRE(decl->entries.size() == 3);
}

TEST_CASE("Parse enum — no trailing semicolon between declarations", "[parser][enum]") {
    test_logger log;
    // Two enums back-to-back without trailing ';' followed by a function.
    k::source src{"enum A { X; } enum B { Y; } f() : int { return 0; }"};
    k::parse::parser parser(log, src);
    auto unit = parser.parse_unit();
    REQUIRE(unit);
    REQUIRE(unit->declarations.size() == 3);
    CHECK_FALSE(log.has_warning());
}

namespace {
    bool has_spurious_semicolon_warning(const test_logger& log) {
        for (const auto& d : log.diagnostics) {
            if (d.level == k::log::diagnostic::severity::warning
                && d.code == static_cast<unsigned int>(k::diag::parser_diag::WARN_SPURIOUS_SEMICOLON)) {
                return true;
            }
        }
        return false;
    }
}

TEST_CASE("Parse enum — stray trailing ';' is a warned empty declaration", "[parser][enum][warning]") {
    test_logger log;
    k::source src{"enum Color { RED; GREEN; };"};
    k::parse::parser parser(log, src);
    auto unit = parser.parse_unit();
    REQUIRE(unit);
    REQUIRE(unit->declarations.size() == 1);
    CHECK(has_spurious_semicolon_warning(log));
}

TEST_CASE("Parse — stray ';' between declarations is warned, one per ';'", "[parser][warning]") {
    test_logger log;
    k::source src{"struct A { } ;; struct B { } ;"};
    k::parse::parser parser(log, src);
    auto unit = parser.parse_unit();
    REQUIRE(unit);
    REQUIRE(unit->declarations.size() == 2);
    unsigned int warnings = 0;
    for (const auto& d : log.diagnostics) {
        if (d.level == k::log::diagnostic::severity::warning
            && d.code == static_cast<unsigned int>(k::diag::parser_diag::WARN_SPURIOUS_SEMICOLON)) {
            ++warnings;
        }
    }
    CHECK(warnings == 3);
}

TEST_CASE("Parse — stray ';' warned inside namespace and aggregate", "[parser][warning]") {
    test_logger log;
    k::source src{"namespace n { struct S { } ; } enum E { A; } ;"};
    k::parse::parser parser(log, src);
    auto unit = parser.parse_unit();
    REQUIRE(unit);
    unsigned int warnings = 0;
    for (const auto& d : log.diagnostics) {
        if (d.level == k::log::diagnostic::severity::warning
            && d.code == static_cast<unsigned int>(k::diag::parser_diag::WARN_SPURIOUS_SEMICOLON)) {
            ++warnings;
        }
    }
    CHECK(warnings == 2);
}

TEST_CASE("Parse — required ';' of non-block declarations is not warned", "[parser][warning]") {
    test_logger log;
    // 'using' and a global variable declaration both legitimately end with ';'.
    k::source src{"using foo::bar; x : int = 3; enum E { A; }"};
    k::parse::parser parser(log, src);
    auto unit = parser.parse_unit();
    REQUIRE(unit);
    REQUIRE(unit->declarations.size() == 3);
    CHECK_FALSE(has_spurious_semicolon_warning(log));
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

// ============================================================
// Phase 3: Enum derivation — Parser tests
// ============================================================

TEST_CASE("Parse enum — derivation clause", "[parser][enum]") {
    test_logger log;
    k::source src{"enum Derived : Base { X = 10; };"};
    k::parse::parser parser(log, src);
    auto decl = parser.parse_enum_decl();
    REQUIRE(decl);
    CHECK(std::string{decl->name.content} == "Derived");
    REQUIRE(decl->base_name.has_value());
    CHECK(*decl->base_name == "Base");
    REQUIRE(decl->entries.size() == 1);
}

TEST_CASE("Parse enum — derivation with qualified base name", "[parser][enum]") {
    test_logger log;
    k::source src{"enum D : ns::Base { X; };"};
    k::parse::parser parser(log, src);
    auto decl = parser.parse_enum_decl();
    REQUIRE(decl);
    REQUIRE(decl->base_name.has_value());
    CHECK(*decl->base_name == "ns::Base");
}

TEST_CASE("Parse enum — no derivation clause", "[parser][enum]") {
    test_logger log;
    k::source src{"enum E { A; B; };"};
    k::parse::parser parser(log, src);
    auto decl = parser.parse_enum_decl();
    REQUIRE(decl);
    CHECK_FALSE(decl->base_name.has_value());
}

// ============================================================
// Phase 4: Enum derivation — JIT tests
// ============================================================

TEST_CASE("Enum derivation — inherited entries accessible", "[gen][enum]") {
    auto jit = gen_jit(R"(
        module test;
        enum Base { A = 1; B = 2; };
        enum Derived : Base { C = 3; };
        get_a() : int { return Derived::A; }
        get_b() : int { return Derived::B; }
        get_c() : int { return Derived::C; }
    )");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<int(*)()>("get_a")() == 1);
    CHECK(jit->lookup_symbol<int(*)()>("get_b")() == 2);
    CHECK(jit->lookup_symbol<int(*)()>("get_c")() == 3);
}

TEST_CASE("Enum derivation — auto-increment continues from base", "[gen][enum]") {
    auto jit = gen_jit(R"(
        module test;
        enum Base { A = 5; B = 6; };
        enum Derived : Base { C; D; };
        get_c() : int { return Derived::C; }
        get_d() : int { return Derived::D; }
    )");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<int(*)()>("get_c")() == 7);
    CHECK(jit->lookup_symbol<int(*)()>("get_d")() == 8);
}

TEST_CASE("Enum derivation — new entry references base entry", "[gen][enum]") {
    auto jit = gen_jit(R"(
        module test;
        enum Base { A = 10; B = 20; };
        enum Derived : Base { C = A; D = B; };
        get_c() : int { return Derived::C; }
        get_d() : int { return Derived::D; }
    )");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<int(*)()>("get_c")() == 10);
    CHECK(jit->lookup_symbol<int(*)()>("get_d")() == 20);
}

TEST_CASE("Enum derivation — inherits default from base", "[gen][enum]") {
    auto jit = gen_jit(R"(
        module test;
        enum Base { A = 1; B = 2 default; };
        enum Derived : Base { C = 3; };
        get_default() : int {
            d : Derived;
            return d;
        }
    )");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<int(*)()>("get_default")() == 2);
}

TEST_CASE("Enum derivation — override default in derived", "[gen][enum]") {
    auto jit = gen_jit(R"(
        module test;
        enum Base { A = 1; B = 2 default; };
        enum Derived : Base { C = 3 default; };
        get_default() : int {
            d : Derived;
            return d;
        }
    )");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<int(*)()>("get_default")() == 3);
}

TEST_CASE("Enum derivation — default is first entry when no explicit default", "[gen][enum]") {
    auto jit = gen_jit(R"(
        module test;
        enum Base { A = 1; B = 2; };
        enum Derived : Base { C = 3; };
        get_default() : int {
            d : Derived;
            return d;
        }
    )");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<int(*)()>("get_default")() == 1);
}

TEST_CASE("Enum derivation — constructor with inherited entry name", "[gen][enum]") {
    auto jit = gen_jit(R"(
        module test;
        enum Base { A = 10; B = 20; };
        enum Derived : Base { C = 30; };
        get_val() : int {
            d : Derived(A);
            return d;
        }
    )");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<int(*)()>("get_val")() == 10);
}

TEST_CASE("Enum derivation — constructor with numeric value", "[gen][enum]") {
    auto jit = gen_jit(R"(
        module test;
        enum Base { A = 1; };
        enum Derived : Base { B = 2; };
        get_val() : int {
            d : Derived(1);
            return d;
        }
    )");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<int(*)()>("get_val")() == 1);
}

TEST_CASE("Enum derivation — multi-level inheritance", "[gen][enum]") {
    auto jit = gen_jit(R"(
        module test;
        enum A { X = 1; };
        enum B : A { Y = 2; };
        enum C : B { Z = 3; };
        get_x() : int { return C::X; }
        get_y() : int { return C::Y; }
        get_z() : int { return C::Z; }
    )");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<int(*)()>("get_x")() == 1);
    CHECK(jit->lookup_symbol<int(*)()>("get_y")() == 2);
    CHECK(jit->lookup_symbol<int(*)()>("get_z")() == 3);
}

TEST_CASE("Enum derivation — multi-level auto-increment", "[gen][enum]") {
    auto jit = gen_jit(R"(
        module test;
        enum A { X = 1; };
        enum B : A { Y; };
        enum C : B { Z; };
        get_y() : int { return C::Y; }
        get_z() : int { return C::Z; }
    )");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<int(*)()>("get_y")() == 2);
    CHECK(jit->lookup_symbol<int(*)()>("get_z")() == 3);
}

TEST_CASE("Enum derivation — duplicate values with base (alias)", "[gen][enum]") {
    auto jit = gen_jit(R"(
        module test;
        enum Base { A = 1; };
        enum Derived : Base { B = 1; };
        get_a() : int { return Derived::A; }
        get_b() : int { return Derived::B; }
    )");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<int(*)()>("get_a")() == 1);
    CHECK(jit->lookup_symbol<int(*)()>("get_b")() == 1);
}

TEST_CASE("Enum derivation — Derived to int conversion", "[gen][enum]") {
    auto jit = gen_jit(R"(
        module test;
        enum Base { A = 1; B = 2; };
        enum Derived : Base { C = 3; };
        get_val() : int {
            d : Derived = Derived::C;
            return d;
        }
    )");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<int(*)()>("get_val")() == 3);
}

TEST_CASE("Enum derivation — comparison between inherited and new entries", "[gen][enum]") {
    auto jit = gen_jit(R"(
        module test;
        enum Base { A = 1; B = 2; };
        enum Derived : Base { C = 3; };
        check_eq() : int {
            d : Derived = Derived::A;
            if (d == Derived::A) { return 1; }
            return 0;
        }
        check_lt() : int {
            d : Derived = Derived::A;
            if (d < Derived::C) { return 1; }
            return 0;
        }
    )");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<int(*)()>("check_eq")() == 1);
    CHECK(jit->lookup_symbol<int(*)()>("check_lt")() == 1);
}

TEST_CASE("Enum derivation — forward declaration order (base after derived)", "[gen][enum]") {
    auto jit = gen_jit(R"(
        module test;
        enum Derived : Base { C = 3; };
        enum Base { A = 1; B = 2; };
        get_a() : int { return Derived::A; }
        get_c() : int { return Derived::C; }
    )");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<int(*)()>("get_a")() == 1);
    CHECK(jit->lookup_symbol<int(*)()>("get_c")() == 3);
}

TEST_CASE("Enum derivation — multiple derived enums from same base", "[gen][enum]") {
    auto jit = gen_jit(R"(
        module test;
        enum Base { A = 1; };
        enum D1 : Base { B = 2; };
        enum D2 : Base { C = 3; };
        get_d1b() : int { return D1::B; }
        get_d2c() : int { return D2::C; }
        get_d1a() : int { return D1::A; }
        get_d2a() : int { return D2::A; }
    )");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<int(*)()>("get_d1b")() == 2);
    CHECK(jit->lookup_symbol<int(*)()>("get_d2c")() == 3);
    CHECK(jit->lookup_symbol<int(*)()>("get_d1a")() == 1);
    CHECK(jit->lookup_symbol<int(*)()>("get_d2a")() == 1);
}

TEST_CASE("Enum derivation — cycle detection", "[gen][enum]") {
    // This should fail with a circular derivation error
    REQUIRE_THROWS(gen_jit_throws(R"(
        module test;
        enum A : B { X = 1; };
        enum B : A { Y = 2; };
    )"));
}

TEST_CASE("Enum derivation — base not found error", "[gen][enum]") {
    REQUIRE_THROWS(gen_jit_throws(R"(
        module test;
        enum D : NonExistent { X = 1; };
    )"));
}

TEST_CASE("Enum derivation — Derived to Base implicit conversion", "[gen][enum]") {
    auto jit = gen_jit(R"(
        module test;
        enum Base { A = 1; B = 2; };
        enum Derived : Base { C = 3; };
        accept_base(b : Base) : int { return b; }
        test_upcast() : int {
            d : Derived = Derived::C;
            return accept_base(d);
        }
    )");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<int(*)()>("test_upcast")() == 3);
}

TEST_CASE("Enum derivation — empty derived enum", "[gen][enum]") {
    auto jit = gen_jit(R"(
        module test;
        enum Base { A = 1; B = 2 default; };
        enum Derived : Base { };
        get_a() : int { return Derived::A; }
        get_default() : int {
            d : Derived;
            return d;
        }
    )");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<int(*)()>("get_a")() == 1);
    CHECK(jit->lookup_symbol<int(*)()>("get_default")() == 2);
}

// ============================================================
// Phase 7: Typed enum tests
// ============================================================

TEST_CASE("Typed enum — parser supports object-backed entry forms", "[parser][enum][typed]") {

    test_logger log;
    k::source src{R"(
        enum MyEnum : MyStruct {
            FIRST_VALUE(1, 2);
            SECOND_VALUE() default;
            THIRD_VALUE{.a = 42};
            ANOTHER_SECOND_VALUE = SECOND_VALUE;
        };
    )"};
    k::parse::parser parser(log, src);
    auto decl = parser.parse_enum_decl();
    REQUIRE(decl);

    CHECK(std::string{decl->name.content} == "MyEnum");
    REQUIRE(decl->explicit_underlying_type != nullptr);
    REQUIRE(decl->base_name.has_value());
    CHECK(*decl->base_name == "MyStruct");

    REQUIRE(decl->entries.size() == 4);
    CHECK(std::string{decl->entries[0]->name.content} == "FIRST_VALUE");
    CHECK(std::string{decl->entries[1]->name.content} == "SECOND_VALUE");
    CHECK(std::string{decl->entries[2]->name.content} == "THIRD_VALUE");
    CHECK(std::string{decl->entries[3]->name.content} == "ANOTHER_SECOND_VALUE");

    CHECK(decl->entries[0]->has_paren_initializer());
    CHECK(decl->entries[0]->ctor_args.size() == 2);
    CHECK_FALSE(decl->entries[0]->is_default);

    CHECK(decl->entries[1]->has_paren_initializer());
    CHECK(decl->entries[1]->ctor_args.empty());
    CHECK(decl->entries[1]->is_default);

    CHECK(decl->entries[2]->has_brace_initializer());
    CHECK_FALSE(decl->entries[2]->is_default);

    CHECK(decl->entries[3]->has_ref_initializer());
    REQUIRE(decl->entries[3]->ref_value.has_value());
    CHECK(std::string{decl->entries[3]->ref_value->content} == "SECOND_VALUE");
}

TEST_CASE("Typed enum — parser parses class-backed implicit entries", "[parser][enum][typed]") {
    test_logger log;
    k::source src{R"(
        enum E : MyClass {
            A;
            B;
            C;
        };
    )"};
    k::parse::parser parser(log, src);
    auto decl = parser.parse_enum_decl();
    REQUIRE(decl);

    CHECK(std::string{decl->name.content} == "E");
    REQUIRE(decl->explicit_underlying_type != nullptr);
    REQUIRE(decl->base_name.has_value());
    CHECK(*decl->base_name == "MyClass");

    REQUIRE(decl->entries.size() == 3);
    CHECK(std::string{decl->entries[0]->name.content} == "A");
    CHECK(std::string{decl->entries[1]->name.content} == "B");
    CHECK(std::string{decl->entries[2]->name.content} == "C");
    CHECK_FALSE(decl->entries[0]->has_explicit_initializer());
    CHECK_FALSE(decl->entries[1]->has_explicit_initializer());
    CHECK_FALSE(decl->entries[2]->has_explicit_initializer());
}

TEST_CASE("Typed enum — explicit integer underlying keeps classic behavior", "[gen][enum][typed][expected]") {
    auto jit = gen_jit(R"(
        module test;
        enum Small : unsigned byte {
            A = 1;
            B = 2;
        };
        test() : int {
            v : Small = Small::B;
            return v;
        }
    )");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<int(*)()>("test")() == 2);
}

TEST_CASE("Typed enum — derived enum inherits explicit integer underlying", "[gen][enum][typed][expected]") {
    auto jit = gen_jit(R"(
        module test;
        enum Small : unsigned byte {
            A = 250;
            B;
        };
        enum SmallMore : Small {
            C;
        };
        get_a() : int { return SmallMore::A; }
        get_b() : int { return SmallMore::B; }
        get_c() : int { return SmallMore::C; }
    )");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<int(*)()>("get_a")() == 250);
    CHECK(jit->lookup_symbol<int(*)()>("get_b")() == 251);
    CHECK(jit->lookup_symbol<int(*)()>("get_c")() == 252);
}

TEST_CASE("Typed enum — object-backed zero-init entry", "[gen][enum][typed][expected]") {
    auto jit = gen_jit(R"(
        module test;
        struct Vec2 {
            x : int;
            y : int;
        }
        enum Dir : Vec2 {
            UP;
        };
        test() : int {
            p: const Vec2& = Dir::UP;
            return p.x + p.y;
        }
    )");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<int(*)()>("test")() == 0);
}

TEST_CASE("Typed enum — class-backed implicit auto-increment values", "[gen][enum][typed][expected]") {
    auto jit = gen_jit(R"(
        module test;
        class Marker {
            public id : int;
        }
        enum Kind : Marker {
            FIRST;
            SECOND;
            THIRD;
        };
        get_first() : int { return Kind::FIRST; }
        get_second() : int { return Kind::SECOND; }
        get_third() : int { return Kind::THIRD; }
    )");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<int(*)()>("get_first")() == 0);
    CHECK(jit->lookup_symbol<int(*)()>("get_second")() == 1);
    CHECK(jit->lookup_symbol<int(*)()>("get_third")() == 2);
}

TEST_CASE("Typed enum — class-backed implicit ++ from previous value", "[gen][enum][typed][expected]") {
    auto jit = gen_jit(R"(
        module test;
        class Counter {
            public value : int;
            public Counter() : value(0) {}
            public Counter(v : int) : value(v) {}
        }
        enum Numbers : Counter {
            TEN{.value = 10};
            ELEVEN;
            TWELVE;
        };
        get_eleven_value() : int {
            c : const Counter& = Numbers::ELEVEN;
            return c.value;
        }
        get_twelve_value() : int {
            c : const Counter& = Numbers::TWELVE;
            return c.value;
        }
    )");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<int(*)()>("get_eleven_value")() == 11);
    CHECK(jit->lookup_symbol<int(*)()>("get_twelve_value")() == 12);
}

TEST_CASE("Typed enum — enum entry to const underlying reference", "[gen][enum][typed][expected]") {
    auto jit = gen_jit(R"(
        module test;
        struct Vec2 {
            x: int;
            y: int;
        }
        enum Dir : Vec2 {
            UP{.x = 0, .y = 1};
            RIGHT{.x = 1, .y = 0};
        };
        sum_right() : int {
            p: const Vec2& = Dir::RIGHT;
            return p.x + p.y;
        }
    )");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<int(*)()>("sum_right")() == 1);
}

TEST_CASE("Typed enum — object to enum conversion and soft-fail in if", "[gen][enum][typed][expected]") {
    auto jit = gen_jit(R"(
        module test;
        struct S {
            a: int;
            equals(other: S&) : bool { return a == other.a; }
        }
        enum E : S {
            V1{.a = 1} default;
            V2{.a = 2};
        };
        test_match() : int {
            s : S{.a = 2};
            e : E = s;
            return e;
        }
        test_softfail_if() : int {
            s : S{.a = 99};
            if(e : E = s) {
                return 10 + e;
            } else {
                return 7;
            }
        }
    )");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<int(*)()>("test_match")() == 1);
    CHECK(jit->lookup_symbol<int(*)()>("test_softfail_if")() == 7);
}

TEST_CASE("Typed enum — object to enum conversion hard-fails outside if", "[gen][enum][typed][expected]") {
    auto res = build_and_exec(R"(
        module test;
        struct S {
            a: int;
            equals(other: S&) : bool { return a == other.a; }
        }
        enum E : S {
            V1{.a = 1} default;
            V2{.a = 2};
        };
        main() : int {
            s : S{.a = 99};
            e : E = s;
            return e;
        }
    )");
    REQUIRE(res.exit_code != 0);
}

TEST_CASE("Typed enum — constructor entry args initialize backing object", "[gen][enum][typed][expected]") {
    auto jit = gen_jit(R"(
        module test;
        struct Point {
            x: int;
            y: int;
        }
        enum Dir : Point {
            UP(0, 1);
        };
        get_y() : int {
            p : const Point& = Dir::UP;
            return p.y;
        }
    )");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<int(*)()>("get_y")() == 1);
}

TEST_CASE("Typed enum — alias shares backing object slot", "[gen][enum][typed][expected]") {
    auto jit = gen_jit(R"(
        module test;
        struct S {
            a: int;
        }
        enum E : S {
            V1{.a = 10};
            A1 = V1;
        };
        read_alias() : int {
            s : const S& = E::A1;
            return s.a;
        }
    )");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<int(*)()>("read_alias")() == 10);
}

TEST_CASE("Typed enum — object to enum conversion requires equality", "[gen][enum][typed][expected]") {
    REQUIRE_THROWS(gen_jit_throws(R"(
        module test;
        struct S {
            a: int;
        }
        enum E : S {
            V1{.a = 1} default;
        };
        main() : int {
            s : S{.a = 1};
            e : E = s;
            return e;
        }
    )"));
}

// ════════════════════════════════════════════════════════════════════════════
//  Regression: enum member default-initialization must use the enum's underlying
//  integer width — NOT the raw `long long` (i128) variant value.
//
//  A struct member of enum type with no explicit initializer is default-
//  constructed by the constructor. The default value was built as a
//  value_expression holding a `long long` (see
//  type_reference_resolver::visit_constructor_invocation_expression), which
//  get_llvm_constant_from_value emitted as an i128 constant. Storing that i128
//  into the enum field (whose underlying type is typically i8) overran the field
//  and corrupted the surrounding stack, producing a SIGSEGV at runtime.
//
//  The fix coerces the constant to the enum's underlying integer width in
//  context::get_llvm_constant_from_value_expression. This test constructs such a
//  struct and checks every field keeps its correct default value (no corruption).
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Enum member default init keeps underlying width (no stack corruption)",
          "[gen][enum][default-init][regression]") {
    auto jit = gen_jit(R"(
        module test;
        enum Status { Idle; Running = 5; Done; }
        struct Task {
            id : unsigned int;
            status : Status;
            active : bool;
        public:
            Task() { id = 42u; }
            const getId() : unsigned int { return id; }
            const getStatus() : int { return (int) status; }
            const isActive() : bool { return active; }
        }
        // id set by the constructor
        task_id() : int {
            t : Task;
            return (int) t.getId();
        }
        // status defaults to the default entry (Idle = 0)
        task_status() : int {
            t : Task;
            return t.getStatus();
        }
        // active defaults to false (0)
        task_active() : int {
            t : Task;
            if (t.isActive()) return 1;
            return 0;
        }
    )");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<int(*)()>("task_id")() == 42);
    CHECK(jit->lookup_symbol<int(*)()>("task_status")() == 0);
    CHECK(jit->lookup_symbol<int(*)()>("task_active")() == 0);
}

// Same enum member default-init issue but exercised through a template struct
// instantiated with the enum as an explicit template argument — the original
// symptom from the k::Expected<R, E> / libk pattern.
TEST_CASE("Enum template-argument member default init keeps underlying width",
          "[gen][enum][default-init][template][regression]") {
    auto jit = gen_jit(R"(
        module test;
        enum Err { OutOfData; Closed; }

        template<typename R, typename E>
        struct Result {
            _val : R;
            _err : E;
            _hasErr : bool;
        public:
            Result() { _hasErr = false; }
            const value() : R { return _val; }
            const hasError() : bool { return _hasErr; }

            public static ok(v : R&) : Result<R, E> {
                r : Result<R, E>;
                r._val = v;
                r._hasErr = false;
                return r;
            }
        }

        run() : int {
            r : Result<unsigned int, Err> = Result<unsigned int, Err>::ok(7u);
            if (r.hasError()) return -1;
            return (int) r.value();   // 7
        }
    )");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<int(*)()>("run")() == 7);
}

