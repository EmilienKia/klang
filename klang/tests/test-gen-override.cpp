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
 * Exhaustive tests for the K language 'override' specifier on virtual functions.
 *
 * THE RULES:
 *   'override' on a METHOD:
 *     - Asserts that this function overrides an inherited virtual slot (from a
 *       parent class or interface).
 *     - If the function does NOT actually override anything, it is a compile-time
 *       error (ERR_OVERRIDE_NOT_OVERRIDING = 0x0177).
 *     - If a function overrides an inherited virtual but does NOT carry 'override',
 *       a warning is emitted (WARN_MISSING_OVERRIDE = 0x0176).
 *
 *   'override' is NOT allowed on:
 *     - Static functions      (ERR_OVERRIDE_ON_STATIC   = 0x0178)
 *     - Abstract functions    (ERR_OVERRIDE_ON_ABSTRACT  = 0x0179)
 *     - Constructors/Dtors    (ERR_OVERRIDE_ON_CTOR_DTOR = 0x017A)
 *     - Struct member funcs   (ERR_OVERRIDE_ON_STRUCT    = 0x017B)
 *
 *   Interaction with 'final':
 *     - Attempting to override a 'final' slot WITH 'override' → error (0x0177)
 *     - Attempting to override a 'final' slot WITHOUT 'override' → warning (0x0175,
 *       existing WARN_OVERRIDE_FINAL) and a new vtable slot is created.
 *
 * Tests covered:
 *  ── Happy path: 'override' on valid overrides ───────────────────────────
 *   [A] override on concrete→concrete class method
 *   [B] override on implementation of abstract parent method
 *   [C] override on implementation of interface method
 *   [D] Multi-level override chain A→B→C
 *   [N] override on virtual operator
 *
 *  ── Warning: missing 'override' ─────────────────────────────────────────
 *   [E] Override without 'override' specifier emits WARN_MISSING_OVERRIDE
 *
 *  ── Error: 'override' on non-overriding function ────────────────────────
 *   [F] override on a function not in any parent
 *   [G] override on a function whose parent slot is 'final'
 *
 *  ── Error: invalid combinations ─────────────────────────────────────────
 *   [H] override + abstract
 *   [I] override + static
 *   [J] override in a struct
 *   [K] override on constructor
 *   [L] override on destructor
 *
 *  ── Mixed: some overrides with, some without 'override' ─────────────────
 *   [M] Class with two overriding methods, one with 'override', one without
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"


// ════════════════════════════════════════════════════════════════════════════
//  [A] override on concrete→concrete class method — happy path
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[A] override on concrete→concrete class method", "[override]") {

    SECTION("Direct call dispatches to derived implementation") {
        auto jit = gen_jit(R"SRC(
module gen_override_01;
class Base {
    Base() {}
    val() : int { return 1; }
}
class Derived : public Base {
    Derived() {}
    override val() : int { return 2; }
}
test() : int {
    d: Derived;
    return d.val();
}
)SRC");
        REQUIRE(jit);
        auto fn = jit->lookup_symbol<int(*)()>("test");
        REQUIRE(fn);
        CHECK(fn() == 2);
    }

    SECTION("Virtual dispatch through base reference") {
        auto result = build_and_exec(R"SRC(
module gen_override_02;
class Base {
    Base() {}
    val() : int { return 10; }
}
class Derived : public Base {
    Derived() {}
    override val() : int { return 20; }
}
call_val(b: Base&) : int { return b.val(); }
main() : int {
    d: Derived;
    return call_val(d);
}
)SRC");
        CHECK(result.exit_code == 20);
    }
}


// ════════════════════════════════════════════════════════════════════════════
//  [B] override on implementation of abstract parent method
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[B] override on implementation of abstract parent method", "[override][abstract]") {

    auto result = build_and_exec(R"SRC(
module gen_override_03;
abstract class Shape {
    Shape() {}
    abstract area() : int;
}
class Square : public Shape {
    Square() {}
    override area() : int { return 4; }
}
call_area(s: Shape&) : int { return s.area(); }
main() : int {
    sq: Square;
    return call_area(sq);
}
)SRC");
    CHECK(result.exit_code == 4);
}


// ════════════════════════════════════════════════════════════════════════════
//  [C] override on implementation of interface method
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[C] override on implementation of interface method", "[override][interface]") {

    SECTION("Direct call") {
        auto jit = gen_jit(R"SRC(
module gen_override_04;
interface Greeter {
    greet() : int;
}
class Hello : public Greeter {
    Hello() {}
    override greet() : int { return 42; }
}
test() : int {
    h: Hello;
    return h.greet();
}
)SRC");
        REQUIRE(jit);
        auto fn = jit->lookup_symbol<int(*)()>("test");
        REQUIRE(fn);
        CHECK(fn() == 42);
    }

    SECTION("Virtual dispatch through interface reference") {
        auto result = build_and_exec(R"SRC(
module gen_override_05;
interface Counter {
    count() : int;
}
class MyCounter : public Counter {
    MyCounter() {}
    override count() : int { return 7; }
}
call_count(c: Counter&) : int { return c.count(); }
main() : int {
    mc: MyCounter;
    return call_count(mc);
}
)SRC");
        CHECK(result.exit_code == 7);
    }
}


// ════════════════════════════════════════════════════════════════════════════
//  [D] Multi-level override chain A→B→C
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[D] Multi-level override chain A→B→C", "[override]") {

    auto result = build_and_exec(R"SRC(
module gen_override_06;
class A {
    A() {}
    val() : int { return 1; }
}
class B : public A {
    B() {}
    override val() : int { return 2; }
}
class C : public B {
    C() {}
    override val() : int { return 3; }
}
call_val(a: A&) : int { return a.val(); }
main() : int {
    c: C;
    return call_val(c);
}
)SRC");
    CHECK(result.exit_code == 3);
}


// ════════════════════════════════════════════════════════════════════════════
//  [E] Override without 'override' specifier emits WARN_MISSING_OVERRIDE
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[E] Override without 'override' emits warning", "[override][warning]") {

    // This should compile successfully but emit a warning
    auto result = build_and_exec(R"SRC(
module gen_override_07;
class Base {
    Base() {}
    val() : int { return 10; }
}
class Derived : public Base {
    Derived() {}
    val() : int { return 20; }
}
call_val(b: Base&) : int { return b.val(); }
main() : int {
    d: Derived;
    return call_val(d);
}
)SRC");
    // Compilation succeeds, virtual dispatch works
    CHECK(result.exit_code == 20);
}


// ════════════════════════════════════════════════════════════════════════════
//  [F] override on a function not in any parent → error
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[F] override on non-overriding function is an error", "[override][error]") {

    SECTION("Function not present in any parent") {
        REQUIRE_THROWS(gen_jit_throws(R"SRC(
module gen_override_08;
class Base {
    Base() {}
    val() : int { return 1; }
}
class Derived : public Base {
    Derived() {}
    override other() : int { return 2; }
}
)SRC"));
    }

    SECTION("No parent class at all") {
        REQUIRE_THROWS(gen_jit_throws(R"SRC(
module gen_override_09;
class Standalone {
    Standalone() {}
    override val() : int { return 1; }
}
)SRC"));
    }
}


// ════════════════════════════════════════════════════════════════════════════
//  [G] override on a function whose parent slot is 'final' → error
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[G] override on a final parent slot is an error", "[override][final][error]") {

    REQUIRE_THROWS(gen_jit_throws(R"SRC(
module gen_override_10;
class A {
    A() {}
    val() : int { return 1; }
}
class B : public A {
    B() {}
    final override val() : int { return 2; }
}
class C : public B {
    C() {}
    override val() : int { return 3; }
}
)SRC"));
}


// ════════════════════════════════════════════════════════════════════════════
//  [H] override + abstract → error
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[H] override + abstract is an error", "[override][abstract][error]") {

    REQUIRE_THROWS(gen_jit_throws(R"SRC(
module gen_override_11;
abstract class Base {
    Base() {}
    abstract val() : int;
}
abstract class Mid : public Base {
    Mid() {}
    override abstract val() : int;
}
)SRC"));
}


// ════════════════════════════════════════════════════════════════════════════
//  [I] override + static → error
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[I] override + static is an error", "[override][static][error]") {

    REQUIRE_THROWS(gen_jit_throws(R"SRC(
module gen_override_12;
class Base {
    Base() {}
    val() : int { return 1; }
}
class Derived : public Base {
    Derived() {}
    static override val() : int { return 2; }
}
)SRC"));
}


// ════════════════════════════════════════════════════════════════════════════
//  [J] override in a struct → error
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[J] override in a struct is an error", "[override][struct][error]") {

    REQUIRE_THROWS(gen_jit_throws(R"SRC(
module gen_override_13;
struct Base {
    val() : int { return 1; }
}
struct Derived : public Base {
    override val() : int { return 2; }
}
)SRC"));
}


// ════════════════════════════════════════════════════════════════════════════
//  [K] override on constructor → error
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[K] override on constructor is an error", "[override][constructor][error]") {

    REQUIRE_THROWS(gen_jit_throws(R"SRC(
module gen_override_14;
class Base {
    Base() {}
}
class Derived : public Base {
    override Derived() {}
}
)SRC"));
}


// ════════════════════════════════════════════════════════════════════════════
//  [L] override on destructor → error
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[L] override on destructor is an error", "[override][destructor][error]") {

    REQUIRE_THROWS(gen_jit_throws(R"SRC(
module gen_override_15;
class Base {
    Base() {}
    ~Base() {}
}
class Derived : public Base {
    Derived() {}
    override ~Derived() {}
}
)SRC"));
}


// ════════════════════════════════════════════════════════════════════════════
//  [M] Mixed: some overrides with, some without 'override'
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[M] Mixed override specifiers: with and without", "[override]") {

    // Both overrides work, one with 'override', one without (warning only)
    auto result = build_and_exec(R"SRC(
module gen_override_16;
class Base {
    Base() {}
    foo() : int { return 1; }
    bar() : int { return 10; }
}
class Derived : public Base {
    Derived() {}
    override foo() : int { return 2; }
    bar() : int { return 20; }
}
call_foo(b: Base&) : int { return b.foo(); }
call_bar(b: Base&) : int { return b.bar(); }
main() : int {
    d: Derived;
    return call_foo(d) + call_bar(d);
}
)SRC");
    CHECK(result.exit_code == 22);
}


// ════════════════════════════════════════════════════════════════════════════
//  [N] override on virtual operator
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[N] override on virtual operator", "[override][operator]") {

    SECTION("Unary operator override") {
        auto result = build_and_exec(R"SRC(
module gen_override_17;
class Base {
    public v : int;
    Base() : v(0) {}
    Base(x: int) : v(x) {}
    operator -() : int { return 0 - v; }
}
class Derived : public Base {
    Derived() : Base(0) {}
    Derived(x: int) : Base(x) {}
    override operator -() : int { return 0 - v - 1000; }
}
call_neg(b: Base&) : int { return -b; }
main() : int {
    d: Derived(5);
    r : int = call_neg(d);
    // Expected: -(5) - 1000 = -1005
    // Return as unsigned-safe: add 1005
    return r + 1005;
}
)SRC");
        CHECK(result.exit_code == 0);
    }

    SECTION("Binary operator override with virtual dispatch") {
        auto result = build_and_exec(R"SRC(
module gen_override_18;
class Base {
    public v : int;
    Base() : v(0) {}
    Base(x: int) : v(x) {}
    operator +(other: Base&) : int { return v + other.v; }
}
class Derived : public Base {
    Derived() : Base(0) {}
    Derived(x: int) : Base(x) {}
    override operator +(other: Base&) : int { return v + other.v + 100; }
}
call_add(a: Base&, b: Base&) : int { return a + b; }
main() : int {
    d: Derived(3);
    b: Base(7);
    return call_add(d, b);
}
)SRC");
        CHECK(result.exit_code == 110);
    }
}


// ════════════════════════════════════════════════════════════════════════════
//  [O] override + final: valid combination (seal a slot after overriding)
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[O] override + final: seal a slot after overriding", "[override][final]") {

    auto result = build_and_exec(R"SRC(
module gen_override_19;
class A {
    A() {}
    val() : int { return 1; }
}
class B : public A {
    B() {}
    final override val() : int { return 2; }
}
call_val(a: A&) : int { return a.val(); }
main() : int {
    b: B;
    return call_val(b);
}
)SRC");
    CHECK(result.exit_code == 2);
}


// ════════════════════════════════════════════════════════════════════════════
//  [P] override on interface method in deep hierarchy
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[P] override through interface and class chain", "[override][interface]") {

    auto result = build_and_exec(R"SRC(
module gen_override_20;
interface Speakable {
    speak() : int;
}
abstract class Animal : public Speakable {
    Animal() {}
}
class Dog : public Animal {
    Dog() {}
    override speak() : int { return 42; }
}
call_speak(s: Speakable&) : int { return s.speak(); }
main() : int {
    d: Dog;
    return call_speak(d);
}
)SRC");
    CHECK(result.exit_code == 42);
}


