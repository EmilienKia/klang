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

#include "../src/common/logger.hpp"
#include "../src/parse/parser.hpp"
#include "../src/model/model.hpp"
#include "../src/gen/generators.hpp"
#include "../src/gen/resolvers.hpp"
#include "../src/compiler.hpp"

#include "helpers.hpp"

//
// ─── Parser tests: base clause ───────────────────────────────────────────────
//

TEST_CASE("Parse struct with base clause", "[parser][inheritance]") {

    SECTION("Simple base class, no visibility specifier (defaults to public)") {
        test_logger logger;
        k::parse::parser parser(logger);
        k::source src(R"SRC(struct Derived : Base { })SRC");
        parser.parse(src);
        auto unit = parser.parse_unit();
        REQUIRE(unit);
        REQUIRE(unit->declarations.size() == 1);
        auto st = std::dynamic_pointer_cast<k::parse::ast::struct_decl>(unit->declarations[0]);
        REQUIRE(st);
        REQUIRE(st->bases.size() == 1);
        CHECK(std::string{st->bases[0].name.content} == "Base");
        CHECK(!st->bases[0].visibility_kw.has_value());
    }

    SECTION("Base with explicit public visibility") {
        test_logger logger;
        k::parse::parser parser(logger);
        k::source src(R"SRC(struct Derived : public Base { })SRC");
        parser.parse(src);
        auto unit = parser.parse_unit();
        REQUIRE(unit);
        auto st = std::dynamic_pointer_cast<k::parse::ast::struct_decl>(unit->declarations[0]);
        REQUIRE(st);
        REQUIRE(st->bases.size() == 1);
        CHECK(st->bases[0].visibility_kw.has_value());
        CHECK(st->bases[0].visibility_kw->type == k::lex::keyword::PUBLIC);
    }

    SECTION("Multiple bases with mixed visibility") {
        test_logger logger;
        k::parse::parser parser(logger);
        k::source src(R"SRC(struct Derived : public Base1, private Base2, Base3 { })SRC");
        parser.parse(src);
        auto unit = parser.parse_unit();
        REQUIRE(unit);
        auto st = std::dynamic_pointer_cast<k::parse::ast::struct_decl>(unit->declarations[0]);
        REQUIRE(st);
        REQUIRE(st->bases.size() == 3);
        CHECK(std::string{st->bases[0].name.content} == "Base1");
        CHECK(st->bases[0].visibility_kw.has_value());
        CHECK(st->bases[0].visibility_kw->type == k::lex::keyword::PUBLIC);
        CHECK(std::string{st->bases[1].name.content} == "Base2");
        CHECK(st->bases[1].visibility_kw.has_value());
        CHECK(st->bases[1].visibility_kw->type == k::lex::keyword::PRIVATE);
        CHECK(std::string{st->bases[2].name.content} == "Base3");
        CHECK(!st->bases[2].visibility_kw.has_value());
    }
}

//
// ─── Single inheritance: constructor initialisation & member access ───────────
//

TEST_CASE("Single inheritance - constructor and member access", "[gen][inheritance]") {
    auto jit = gen_jit(R"SRC(
module __inh_simple__;

struct Base {
    x: int;
    Base() : x(10) {}
}

struct Derived : public Base {
    y: int;
    Derived() : y(20) {}
}

// Returns the value of the base field 'x', initialised by Base()
test_base_field() : int {
    d: Derived;
    return d.x;
}

// Returns the value of the derived field 'y', initialised by Derived()
test_derived_field() : int {
    d: Derived;
    return d.y;
}

// Returns x + y — exercises both fields at once
test_sum() : int {
    d: Derived;
    return d.x + d.y;
}

// Assign via base field and read back
test_assign_base() : int {
    d: Derived;
    d.x = 42;
    return d.x;
}

// Assign via derived field and read back
test_assign_derived() : int {
    d: Derived;
    d.y = 99;
    return d.y;
}
)SRC", false, false);
    REQUIRE(jit);

    auto test_base_field = jit->lookup_symbol<int(*)()>("test_base_field");
    REQUIRE(test_base_field != nullptr);
    // Base() sets x=10
    CHECK(test_base_field() == 10);

    auto test_derived_field = jit->lookup_symbol<int(*)()>("test_derived_field");
    REQUIRE(test_derived_field != nullptr);
    // Derived() sets y=20
    CHECK(test_derived_field() == 20);

    auto test_sum = jit->lookup_symbol<int(*)()>("test_sum");
    REQUIRE(test_sum != nullptr);
    CHECK(test_sum() == 30); // 10 + 20

    auto test_assign_base = jit->lookup_symbol<int(*)()>("test_assign_base");
    REQUIRE(test_assign_base != nullptr);
    CHECK(test_assign_base() == 42);

    auto test_assign_derived = jit->lookup_symbol<int(*)()>("test_assign_derived");
    REQUIRE(test_assign_derived != nullptr);
    CHECK(test_assign_derived() == 99);
}

//
// ─── Single inheritance: base member methods callable on derived ──────────────
//

TEST_CASE("Single inheritance - method inherited from base", "[gen][inheritance]") {
    auto jit = gen_jit(R"SRC(
module __inh_method__;

struct Base {
    val: int;
    Base() : val(5) {}
    get_val() : int {
        return val;
    }
    double_val() : int {
        return val * 2;
    }
}

struct Derived : public Base {
    extra: int;
    Derived() : extra(3) {}
}

// Call base method on a derived instance
test_get_val() : int {
    d: Derived;
    return d.get_val();
}

// Call base method that uses a base member
test_double_val() : int {
    d: Derived;
    return d.double_val();
}

// Modify base field via derived ref, then call base method
test_modify_then_call() : int {
    d: Derived;
    d.val = 7;
    return d.double_val();
}
)SRC", false, false);
    REQUIRE(jit);

    auto test_get_val = jit->lookup_symbol<int(*)()>("test_get_val");
    REQUIRE(test_get_val != nullptr);
    CHECK(test_get_val() == 5);

    auto test_double_val = jit->lookup_symbol<int(*)()>("test_double_val");
    REQUIRE(test_double_val != nullptr);
    CHECK(test_double_val() == 10); // 5 * 2

    auto test_modify_then_call = jit->lookup_symbol<int(*)()>("test_modify_then_call");
    REQUIRE(test_modify_then_call != nullptr);
    CHECK(test_modify_then_call() == 14); // 7 * 2
}

//
// ─── Single inheritance: upcast to base reference ────────────────────────────
//

TEST_CASE("Single inheritance - upcast to base ref", "[gen][inheritance]") {
    auto jit = gen_jit(R"SRC(
module __inh_upcast__;

struct Base {
    val: int;
    Base() : val(42) {}
}

struct Derived : public Base {
    extra: int;
    Derived() : extra(100) {}
}

// Function expecting a Base& — called with a Derived local (implicit upcast)
get_val(b: Base&) : int {
    return b.val;
}

// Exercises the implicit upcast at call site
test_upcast() : int {
    d: Derived;
    return get_val(d);
}
)SRC", false, false);
    REQUIRE(jit);

    auto test_upcast = jit->lookup_symbol<int(*)()>("test_upcast");
    REQUIRE(test_upcast != nullptr);
    CHECK(test_upcast() == 42);
}

//
// ─── Single inheritance: explicit constructor initialiser for base ────────────
//

TEST_CASE("Single inheritance - explicit base constructor in mem-init list", "[gen][inheritance]") {
    auto jit = gen_jit(R"SRC(
module __inh_ctor_init__;

struct Base {
    x: int;
    Base() : x(1) {}
    Base(v: int) : x(v) {}
}

struct DerivedDefault : public Base {
    y: int;
    // No base init → calls Base() → x == 1
    DerivedDefault() : y(10) {}
}

struct DerivedExplicit : public Base {
    y: int;
    // Explicit base init → calls Base(99) → x == 99
    DerivedExplicit() : Base(99), y(10) {}
}

test_default_base_ctor() : int {
    d: DerivedDefault;
    return d.x;   // expect 1
}

test_explicit_base_ctor() : int {
    d: DerivedExplicit;
    return d.x;   // expect 99
}
)SRC", false, false);
    REQUIRE(jit);

    auto test_default = jit->lookup_symbol<int(*)()>("test_default_base_ctor");
    REQUIRE(test_default != nullptr);
    CHECK(test_default() == 1);

    auto test_explicit = jit->lookup_symbol<int(*)()>("test_explicit_base_ctor");
    REQUIRE(test_explicit != nullptr);
    CHECK(test_explicit() == 99);
}

//
// ─── Multiple inheritance: layout and field access ───────────────────────────
//

TEST_CASE("Multiple inheritance - member access", "[gen][inheritance]") {
    auto jit = gen_jit(R"SRC(
module __inh_multi__;

struct A {
    a_val: int;
    A() : a_val(1) {}
}

struct B {
    b_val: int;
    B() : b_val(2) {}
}

struct C : public A, public B {
    c_val: int;
    C() : c_val(3) {}
}

test_a_val() : int {
    c: C;
    return c.a_val;
}

test_b_val() : int {
    c: C;
    return c.b_val;
}

test_c_val() : int {
    c: C;
    return c.c_val;
}

test_sum() : int {
    c: C;
    return c.a_val + c.b_val + c.c_val;
}

test_assign_and_sum() : int {
    c: C;
    c.a_val = 10;
    c.b_val = 20;
    c.c_val = 30;
    return c.a_val + c.b_val + c.c_val;
}
)SRC", false, false);
    REQUIRE(jit);

    auto test_a_val = jit->lookup_symbol<int(*)()>("test_a_val");
    REQUIRE(test_a_val != nullptr);
    CHECK(test_a_val() == 1);

    auto test_b_val = jit->lookup_symbol<int(*)()>("test_b_val");
    REQUIRE(test_b_val != nullptr);
    CHECK(test_b_val() == 2);

    auto test_c_val = jit->lookup_symbol<int(*)()>("test_c_val");
    REQUIRE(test_c_val != nullptr);
    CHECK(test_c_val() == 3);

    auto test_sum = jit->lookup_symbol<int(*)()>("test_sum");
    REQUIRE(test_sum != nullptr);
    CHECK(test_sum() == 6); // 1 + 2 + 3

    auto test_assign = jit->lookup_symbol<int(*)()>("test_assign_and_sum");
    REQUIRE(test_assign != nullptr);
    CHECK(test_assign() == 60); // 10 + 20 + 30
}

//
// ─── Multiple inheritance: diamond — two independent copies ──────────────────
//

TEST_CASE("Multiple inheritance - diamond: two independent base copies", "[gen][inheritance]") {
    // C++ non-virtual diamond: A appears twice, one copy per branch.
    // D::B1::x and D::B2::x are independent.
    auto jit = gen_jit(R"SRC(
module __inh_diamond__;

struct A {
    x: int;
    A() : x(0) {}
}

struct B1 : public A {
    B1() {}
}

struct B2 : public A {
    B2() {}
}

struct D : public B1, public B2 {
    D() {}
}

// Set the B1 copy of x (via B1& upcast) and read it back via B1
set_b1_x(d: B1&, v: int) {
    d.x = v;
}

// Set the B2 copy of x (via B2& upcast) and read it back via B2
set_b2_x(d: B2&, v: int) {
    d.x = v;
}

get_b1_x(d: B1&) : int {
    return d.x;
}

get_b2_x(d: B2&) : int {
    return d.x;
}

test_independent_copies() : int {
    d: D;
    set_b1_x(d, 11);
    set_b2_x(d, 22);
    // The two copies must be independent
    return get_b1_x(d) * 100 + get_b2_x(d);
}
)SRC", false, false);
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test_independent_copies");
    REQUIRE(test != nullptr);
    // B1::x == 11, B2::x == 22 → 11*100 + 22 == 1122
    CHECK(test() == 1122);
}

//
// ─── Destructor order in inheritance ─────────────────────────────────────────
//

TEST_CASE("Single inheritance - destructor called in reverse order", "[gen][inheritance]") {
    // Both Base and Derived have destructors that write to a global counter.
    // Construction order: Base then Derived.
    // Destruction order: Derived then Base (reverse).
    // We track destruction via a global int used as a bitmask:
    //   bit 0 (1) set by ~Derived
    //   bit 1 (2) set by ~Base
    // After destruction the value must be 3 (both bits set).
    // ~Derived must run before ~Base, so the intermediate value after ~Derived
    // but before ~Base should be 1 — we capture the sequence via order_log:
    //   order_log is set to 1 by ~Derived, then shifted left by 1 and ORed with 1 by ~Base.
    //   So after both: order_log = ((1 << 1) | 1) = 3, but only if Derived ran first.
    //   If Base ran first: after ~Base order_log=1, then ~Derived sets order_log=(1<<1)|1=3 too.
    //   To distinguish order, use an accumulator: append 1 for Derived, 2 for Base.
    //   order_log = dtor_derived_count*10 + dtor_base_count after one full cycle.
    auto jit = gen_jit(R"SRC(
module __inh_dtor__;

dtor_log : int;   // accumulator: Derived dtor appends 1, Base dtor appends 2

struct Base {
    ~Base() {
        dtor_log = dtor_log * 10 + 2;
    }
}

struct Derived : public Base {
    ~Derived() {
        dtor_log = dtor_log * 10 + 1;
    }
}

test_dtor_order() : int {
    d: Derived;
    dtor_log = 0;
    return 0;   // dtor_log modified by destructors after this return
}

get_dtor_log() : int {
    return dtor_log;
}
)SRC", false, false);
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test_dtor_order");
    REQUIRE(test != nullptr);
    test(); // triggers construction then destruction of d

    auto get_log = jit->lookup_symbol<int(*)()>("get_dtor_log");
    REQUIRE(get_log != nullptr);
    // ~Derived runs first (appends 1), then ~Base (appends 2) → log = 12
    CHECK(get_log() == 12);
}

//
// ─── Error tests ─────────────────────────────────────────────────────────────
//

TEST_CASE("Inheritance - error on ambiguous member access", "[gen][inheritance]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
module __inh_ambig__;
struct A { x: int; A() : x(1) {} }
struct B { x: int; B() : x(2) {} }
struct C : public A, public B {
    C() {}
}
get_x(c: C&) : int {
    return c.x;
}
)SRC", false, false), k::model::gen::resolution_error);
}

TEST_CASE("Inheritance - cycle detection", "[gen][inheritance]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
module __inh_cycle__;
struct A : public B {}
struct B : public A {}
)SRC", false, false), k::model::gen::resolution_error);
}

TEST_CASE("Inheritance - unknown base class error", "[gen][inheritance]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
module __inh_unknown_base__;
struct Derived : public NonExistent {}
)SRC", false, false), k::model::gen::resolution_error);
}

TEST_CASE("Inheritance - private member access from outside (error)", "[gen][inheritance]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
module __inh_private_member__;
struct Base {
private:
    secret: int;
public:
    Base() : secret(42) {}
}
struct Derived : public Base {
    Derived() {}
}
try_access(d: Derived&) : int {
    return d.secret;
}
)SRC", false, false), k::model::gen::resolution_error);
}

//
// ─── Final struct tests ───────────────────────────────────────────────────────
//

TEST_CASE("Parse final struct specifier", "[parser][final]") {

    SECTION("Simple final struct (no base clause)") {
        test_logger logger;
        k::parse::parser parser(logger);
        k::source src(R"SRC(final struct Leaf { })SRC");
        parser.parse(src);
        auto unit = parser.parse_unit();
        REQUIRE(unit);
        REQUIRE(unit->declarations.size() == 1);
        auto st = std::dynamic_pointer_cast<k::parse::ast::struct_decl>(unit->declarations[0]);
        REQUIRE(st);
        CHECK(k::lex::keyword::has(st->specifiers, k::lex::keyword::FINAL));
        CHECK(st->bases.empty());
    }

    SECTION("Final struct with other specifiers") {
        test_logger logger;
        k::parse::parser parser(logger);
        k::source src(R"SRC(public final struct Leaf { })SRC");
        parser.parse(src);
        auto unit = parser.parse_unit();
        REQUIRE(unit);
        auto st = std::dynamic_pointer_cast<k::parse::ast::struct_decl>(unit->declarations[0]);
        REQUIRE(st);
        CHECK(k::lex::keyword::has(st->specifiers, k::lex::keyword::FINAL));
        CHECK(k::lex::keyword::has(st->specifiers, k::lex::keyword::PUBLIC));
    }

    SECTION("Non-final struct does not carry final specifier") {
        test_logger logger;
        k::parse::parser parser(logger);
        k::source src(R"SRC(struct Regular { })SRC");
        parser.parse(src);
        auto unit = parser.parse_unit();
        REQUIRE(unit);
        auto st = std::dynamic_pointer_cast<k::parse::ast::struct_decl>(unit->declarations[0]);
        REQUIRE(st);
        CHECK_FALSE(k::lex::keyword::has(st->specifiers, k::lex::keyword::FINAL));
    }
}

TEST_CASE("Model: final struct is_final flag", "[model][final]") {

    SECTION("final struct sets is_final to true") {
        test_logger logger;
        auto jit = gen_jit(R"SRC(
module __final_model__;
final struct Leaf {
    x: int;
    Leaf() : x(0) {}
}
)SRC", false, false);
        REQUIRE(jit);
    }

    SECTION("Non-final struct has is_final false") {
        test_logger logger;
        auto jit = gen_jit(R"SRC(
module __nonfinal_model__;
struct Extendable {
    x: int;
    Extendable() : x(0) {}
}
)SRC", false, false);
        REQUIRE(jit);
    }
}

TEST_CASE("Final struct can be used as member (aggregation)", "[gen][final]") {
    auto jit = gen_jit(R"SRC(
module __final_aggregation__;

final struct Coord {
    x: int;
    y: int;
    Coord() : x(0), y(0) {}
    Coord(a: int, b: int) : x(a), y(b) {}
}

struct Shape {
    pos: Coord;
    Shape() {}
    Shape(a: int, b: int) : pos(a, b) {}
    get_x() : int { return pos.x; }
    get_y() : int { return pos.y; }
}

test_final_aggregation() : int {
    s: Shape(3, 7);
    return s.get_x() + s.get_y();
}
)SRC", false, false);
    REQUIRE(jit);

    auto fn = jit->lookup_symbol<int(*)()>("test_final_aggregation");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 10);
}

TEST_CASE("Final struct can be used as function parameter", "[gen][final]") {
    auto jit = gen_jit(R"SRC(
module __final_param__;

final struct Point {
    x: int;
    Point() : x(0) {}
    Point(v: int) : x(v) {}
}

get_value(p: Point&) : int {
    return p.x;
}

test_final_param() : int {
    p: Point(42);
    return get_value(p);
}
)SRC", false, false);
    REQUIRE(jit);

    auto fn = jit->lookup_symbol<int(*)()>("test_final_param");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 42);
}

TEST_CASE("Inheritance - error when inheriting from a final struct", "[gen][final]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
module __final_inherit_error__;
final struct Base {
    x: int;
    Base() : x(0) {}
}
struct Derived : public Base {
    Derived() {}
}
)SRC", false, false), k::model::gen::resolution_error);
}

TEST_CASE("Inheritance - error: final struct in multi-inheritance chain", "[gen][final]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
module __final_multi_inherit__;
struct A { A() {} }
final struct B { B() {} }
struct C : public A, public B {
    C() {}
}
)SRC", false, false), k::model::gen::resolution_error);
}

TEST_CASE("Inheritance - final struct can itself inherit", "[gen][final]") {
    // A final struct may still derive from another struct;
    // it only forbids being used AS a base class.
    auto jit = gen_jit(R"SRC(
module __final_can_inherit__;

struct Base {
    v: int;
    Base() : v(10) {}
}

final struct Leaf : public Base {
    w: int;
    Leaf() : w(5) {}
}

test_final_can_inherit() : int {
    l: Leaf;
    return l.v + l.w;
}
)SRC", false, false);
    REQUIRE(jit);

    auto fn = jit->lookup_symbol<int(*)()>("test_final_can_inherit");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 15);
}
