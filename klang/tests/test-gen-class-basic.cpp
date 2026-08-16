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

/**
 * Tests for K language class support — basic features.
 *
 * Tests covered:
 *  - Parser: 'class' keyword recognized
 *  - Default visibility: member variables PROTECTED, member functions PUBLIC
 *  - is_class() flag on the model structure
 *  - Simple class with constructor and member functions
 *  - Class with virtual functions (vtable generated)
 *  - Single inheritance with virtual override
 *  - Virtual dispatch (polymorphism)
 *  - Non-virtual qualified call (MyClass::method())
 *  - 'final' specifier on a new function (makes it non-virtual)
 *  - 'final' specifier on a virtual function (warns + becomes new branch if overridden)
 *  - Private functions: not virtual, cannot override virtual
 *  - Class cross-struct inheritance forbidden
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// Parser tests
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Parse class keyword", "[parser][class]") {

    SECTION("Simple class declaration") {
        test_logger logger;
        k::parse::parser parser(logger);
        k::source src(R"SRC(class Foo { })SRC");
        parser.parse(src);
        auto unit = parser.parse_unit();
        REQUIRE(unit);
        REQUIRE(unit->declarations.size() == 1);
        auto st = std::dynamic_pointer_cast<k::parse::ast::aggregate_decl>(unit->declarations[0]);
        REQUIRE(st);
        CHECK(std::string{st->name.content} == "Foo");
        CHECK(st->kw_aggregate_type.type == k::lex::keyword::CLASS);
    }

    SECTION("Class with base clause") {
        test_logger logger;
        k::parse::parser parser(logger);
        k::source src(R"SRC(class Derived : public Base { })SRC");
        parser.parse(src);
        auto unit = parser.parse_unit();
        REQUIRE(unit);
        auto st = std::dynamic_pointer_cast<k::parse::ast::aggregate_decl>(unit->declarations[0]);
        REQUIRE(st);
        REQUIRE(st->bases.size() == 1);
        CHECK(std::string{st->bases[0].name.content} == "Base");
        CHECK(st->kw_aggregate_type.type == k::lex::keyword::CLASS);
    }

    SECTION("Class with multiple bases") {
        test_logger logger;
        k::parse::parser parser(logger);
        k::source src(R"SRC(class D : public B, protected C { })SRC");
        parser.parse(src);
        auto unit = parser.parse_unit();
        REQUIRE(unit);
        auto st = std::dynamic_pointer_cast<k::parse::ast::aggregate_decl>(unit->declarations[0]);
        REQUIRE(st);
        REQUIRE(st->bases.size() == 2);
        CHECK(st->bases[0].visibility_kw->type == k::lex::keyword::PUBLIC);
        CHECK(st->bases[1].visibility_kw->type == k::lex::keyword::PROTECTED);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Model tests: is_class flag, default visibility
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Class model: is_class flag", "[model][class]") {

    SECTION("struct keyword -> is_class() == false") {
        auto jit = gen_jit(R"SRC(
module __cls_model_struct__;
struct S {
    x: int;
    get() : int { return x; }
}
)SRC");
        REQUIRE(jit);
    }

    SECTION("class keyword -> is_class() == true") {
        auto jit = gen_jit(R"SRC(
module __cls_model_class__;
class C {
    x: int;
    get() : int { return x; }
}
)SRC");
        REQUIRE(jit);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Class default visibility tests
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Class default visibility: member variables PROTECTED", "[class][visibility]") {

    SECTION("Accessing class variable from outside should fail (PROTECTED default)") {
        REQUIRE_THROWS(gen_jit_throws(R"SRC(
module __cls_vis_prot__;
class C {
    x: int;
    C() : x(42) {}
}
test() : int {
    c: C;
    return c.x;
}
)SRC"));
    }

    SECTION("Accessing class variable from member function is OK") {
        auto jit = gen_jit(R"SRC(
module __cls_vis_member_ok__;
class C {
    x: int;
    C() : x(42) {}
    get() : int { return x; }
}
test() : int {
    c: C;
    return c.get();
}
)SRC");
        REQUIRE(jit);
        auto fn = jit->lookup_symbol<int(*)()>("_KFN21__cls_vis_member_ok__4testEv");
        REQUIRE(fn);
        CHECK(fn() == 42);
    }

    SECTION("Class member functions are PUBLIC by default") {
        auto jit = gen_jit(R"SRC(
module __cls_vis_func_pub__;
class C {
    x: int;
    C() : x(7) {}
    get() : int { return x; }
}
test() : int {
    c: C;
    return c.get();
}
)SRC");
        REQUIRE(jit);
        auto fn = jit->lookup_symbol<int(*)()>("_KFN20__cls_vis_func_pub__4testEv");
        REQUIRE(fn);
        CHECK(fn() == 7);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Simple class: constructor, method call
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Simple class with constructor and methods", "[class][gen]") {

    auto jit = gen_jit(R"SRC(
module __cls_simple__;
class Counter {
    count: int;
    Counter() : count(0) {}
    increment() { ++count; }
    get() : int { return count; }
}
test() : int {
    c: Counter;
    c.increment();
    c.increment();
    c.increment();
    return c.get();
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN14__cls_simple__4testEv");
    REQUIRE(fn);
    CHECK(fn() == 3);
}

// ─────────────────────────────────────────────────────────────────────────────
// Virtual dispatch: single inheritance
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Virtual dispatch - single inheritance", "[class][virtual][gen]") {

    SECTION("Base class method called on derived object through reference") {
        auto jit = gen_jit(R"SRC(
module __cls_virt_single__;
class Animal {
    sound() : int { return 1; }
}
class Dog : public Animal {
    sound() : int { return 2; }
}
call_sound(a: Animal&) : int {
    return a.sound();
}
test() : int {
    d: Dog;
    return call_sound(d);
}
)SRC");
        REQUIRE(jit);
        auto fn = jit->lookup_symbol<int(*)()>("_KFN19__cls_virt_single__4testEv");
        REQUIRE(fn);
        // Virtual dispatch: should call Dog::sound(), returning 2
        CHECK(fn() == 2);
    }

    SECTION("Base method called directly on base object returns base result") {
        auto jit = gen_jit(R"SRC(
module __cls_virt_base_direct__;
class Animal {
    sound() : int { return 1; }
}
class Dog : public Animal {
    sound() : int { return 2; }
}
test() : int {
    a: Animal;
    return a.sound();
}
)SRC");
        REQUIRE(jit);
        auto fn = jit->lookup_symbol<int(*)()>("_KFN24__cls_virt_base_direct__4testEv");
        REQUIRE(fn);
        CHECK(fn() == 1);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Non-virtual qualified call
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Non-virtual qualified call MyClass::method()", "[class][virtual][gen]") {

    // When calling Base::method(obj) explicitly, the call is non-virtual
    // and invokes Base::method directly, even if obj is a Derived.
    auto jit = gen_jit(R"SRC(
module __cls_nonvirt_qual__;
class Base {
    value() : int { return 10; }
}
class Derived : public Base {
    value() : int { return 20; }
}
test_direct_base(d: Derived&) : int {
    return Base::value(d);
}
test() : int {
    d: Derived;
    return test_direct_base(d);
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN20__cls_nonvirt_qual__4testEv");
    REQUIRE(fn);
    // Should call Base::value directly (non-virtual), returning 10
    CHECK(fn() == 10);
}

// ─────────────────────────────────────────────────────────────────────────────
// 'final' specifier on functions
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("'final' on new function makes it non-virtual", "[class][final][gen]") {

    auto jit = gen_jit(R"SRC(
module __cls_final_new__;
class C {
    final compute() : int { return 99; }
}
test() : int {
    c: C;
    return c.compute();
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN17__cls_final_new__4testEv");
    REQUIRE(fn);
    CHECK(fn() == 99);
}

// ─────────────────────────────────────────────────────────────────────────────
// Private function in class
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Private class function not accessible outside", "[class][visibility][gen]") {

    SECTION("Calling private method from outside fails") {
        REQUIRE_THROWS(gen_jit_throws(R"SRC(
module __cls_priv_func__;
class C {
    private helper() : int { return 1; }
    get() : int { return this.helper(); }
}
test() : int {
    c: C;
    return c.helper();
}
)SRC"));
    }

    SECTION("Private method callable from within class") {
        auto jit = gen_jit(R"SRC(
module __cls_priv_internal__;
class C {
    private helper() : int { return 42; }
    get() : int { return this.helper(); }
}
test() : int {
    c: C;
    return c.get();
}
)SRC");
        REQUIRE(jit);
        auto fn = jit->lookup_symbol<int(*)()>("_KFN21__cls_priv_internal__4testEv");
        REQUIRE(fn);
        CHECK(fn() == 42);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Class inheritance: multi-level
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Multi-level class inheritance virtual dispatch", "[class][virtual][gen]") {

    auto jit = gen_jit(R"SRC(
module __cls_multilevel__;
class A {
    val() : int { return 1; }
}
class B : public A {
    val() : int { return 2; }
}
class C : public B {
    val() : int { return 3; }
}
call_val(a: A&) : int {
    return a.val();
}
test() : int {
    c: C;
    return call_val(c);
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN18__cls_multilevel__4testEv");
    REQUIRE(fn);
    // Virtual dispatch through 3 levels: should call C::val(), returning 3
    CHECK(fn() == 3);
}

// ─────────────────────────────────────────────────────────────────────────────
// Class with explicit public/protected/private sections
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Class with explicit visibility sections", "[class][visibility][gen]") {

    auto jit = gen_jit(R"SRC(
module __cls_vis_sections__;
class BankAccount {
    private:
        balance: int;
    public:
        BankAccount() : balance(0) {}
        deposit(amount: int) { balance += amount; }
        get_balance() : int { return balance; }
}
test() : int {
    acc: BankAccount;
    acc.deposit(100);
    acc.deposit(50);
    return acc.get_balance();
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN20__cls_vis_sections__4testEv");
    REQUIRE(fn);
    CHECK(fn() == 150);
}

TEST_CASE("static method can call private static member", "[gen][class][static][visibility]") {
    // Regression: is_struct_member_accessible used to skip static member
    // functions entirely, so a private static helper was inaccessible even
    // from another static method of the same class.
    auto jit = gen_jit(R"SRC(
module test;
class Counter {
private:
    static impl_add(a: int, b: int) : int { return a + b; }
public:
    static add(a: int, b: int) : int { return impl_add(a, b); }
}
test() : int { return Counter::add(21, 21); }
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN4test4testEv");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("static method cannot access private member of another class", "[gen][class][static][visibility]") {
    // A static method of a *different* class must still be rejected.
    REQUIRE(compile_should_fail(R"SRC(
module test;
class A {
private:
    static secret() : int { return 1; }
}
class B {
public:
    static steal() : int { return A::secret(); }
}
)SRC", nullptr));
}
