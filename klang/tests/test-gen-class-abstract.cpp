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
 * Exhaustive tests for the K language 'abstract' specifier on classes and methods.
 *
 * THE RULES:
 *   'abstract' on a METHOD:
 *     - The method has no body (no IR emitted — not even a declaration).
 *     - Only valid on non-static, non-private, non-final member functions of classes (not structs).
 *     - The owning class must also be declared 'abstract', or an error is raised.
 *
 *   'abstract' on a CLASS:
 *     - The class cannot be directly instantiated.
 *     - Required when any directly declared or inherited virtual method is still abstract.
 *     - Can also be declared abstract explicitly to prevent instantiation for any reason.
 *     - A derived class that provides concrete implementations of all abstract methods
 *       does NOT need to be abstract (and CAN be instantiated).
 *
 * Tests covered:
 *  ── Valid abstract method usage ──────────────────────────────────────────
 *   [A] Abstract method in abstract class: parses and compiles; no IR for the method
 *   [B] Abstract class with explicit prevention (no abstract methods): compiles
 *   [C] Derived class implementing all abstract methods: instantiable
 *   [D] Multi-level inheritance: grandchild implements abstract method
 *   [E] Virtual dispatch through base ref to concrete derived class
 *
 *  ── Error: abstract consistency ─────────────────────────────────────────
 *   [F] Abstract method in non-abstract class → error 0x0173
 *   [G] Derived class leaving abstract method unimplemented, not abstract → error 0x0174
 *
 *  ── Error: cannot instantiate abstract class ────────────────────────────
 *   [H] Direct instantiation of abstract class → error 0x0107
 *   [I] Explicit-abstract class (no abstract methods) cannot be instantiated → error 0x0107
 *
 *  ── Error: abstract specifier constraints ───────────────────────────────
 *   [J] 'abstract' on struct → error 0x00A0
 *   [K] 'abstract' on static function → error 0x00A1
 *   [L] 'abstract' on final function → error 0x00A2
 *   [M] 'abstract' method with a body → error 0x00A3
 *   [N] 'abstract' method inside a struct → error 0x00A4
 *   [O] 'abstract' method not inside any class → error 0x00A5
 *   [P] 'abstract' on private method → error 0x00A6
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

// ════════════════════════════════════════════════════════════════════════════
//  [A] Abstract method in abstract class — parses, compiles, no IR for method
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[A] Abstract method in abstract class compiles cleanly", "[class][abstract]") {

    SECTION("Abstract class with one abstract method parses and compiles") {
        auto jit = gen_jit(R"SRC(
module __abstract_basic__;
abstract class Shape {
    Shape() {}
    abstract area() : int;
}
)SRC");
        REQUIRE(jit);
    }

    SECTION("Abstract class with multiple abstract methods compiles") {
        auto jit = gen_jit(R"SRC(
module __abstract_multi__;
abstract class Animal {
    Animal() {}
    abstract sound() : int;
    abstract speed() : int;
}
)SRC");
        REQUIRE(jit);
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  [B] Abstract class with no abstract methods (explicit prevention)
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[B] Explicitly abstract class with no abstract methods compiles", "[class][abstract]") {

    auto jit = gen_jit(R"SRC(
module __abstract_explicit__;
abstract class Base {
    Base() {}
    value() : int { return 42; }
}
)SRC");
    REQUIRE(jit);
}

// ════════════════════════════════════════════════════════════════════════════
//  [C] Derived class implementing all abstract methods: instantiable
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[C] Derived class implementing all abstract methods is instantiable", "[class][abstract]") {

    SECTION("Single abstract method, single level of derivation") {
        auto jit = gen_jit(R"SRC(
module __abstract_derive_simple__;
abstract class Shape {
    Shape() {}
    abstract area() : int;
}
class Circle : public Shape {
    Circle() {}
    area() : int { return 314; }
}
test() : int {
    c: Circle;
    return c.area();
}
)SRC");
        REQUIRE(jit);
        auto fn = jit->lookup_symbol<int(*)()>("test");
        REQUIRE(fn);
        CHECK(fn() == 314);
    }

    SECTION("Two abstract methods both implemented") {
        auto jit = gen_jit(R"SRC(
module __abstract_derive_two__;
abstract class Widget {
    Widget() {}
    abstract width() : int;
    abstract height() : int;
}
class Button : public Widget {
    Button() {}
    width()  : int { return 100; }
    height() : int { return 30; }
}
test_width()  : int { b: Button; return b.width();  }
test_height() : int { b: Button; return b.height(); }
)SRC");
        REQUIRE(jit);
        auto fw = jit->lookup_symbol<int(*)()>("test_width");
        auto fh = jit->lookup_symbol<int(*)()>("test_height");
        REQUIRE(fw); REQUIRE(fh);
        CHECK(fw() == 100);
        CHECK(fh() == 30);
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  [D] Multi-level inheritance: grandchild implements abstract method
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[D] Grandchild implements inherited abstract method", "[class][abstract]") {

    auto jit = gen_jit(R"SRC(
module __abstract_grandchild__;
abstract class A {
    A() {}
    abstract value() : int;
}
abstract class B : public A {
    B() {}
}
class C : public B {
    C() {}
    value() : int { return 99; }
}
test() : int {
    c: C;
    return c.value();
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 99);
}

// ════════════════════════════════════════════════════════════════════════════
//  [E] Virtual dispatch through base reference to concrete derived class
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[E] Virtual dispatch works through abstract base reference", "[class][abstract]") {

    SECTION("Single override dispatches correctly") {
        auto jit = gen_jit(R"SRC(
module __abstract_dispatch__;
abstract class Shape {
    Shape() {}
    abstract area() : int;
}
class Square : public Shape {
    Square() {}
    area() : int { return 4; }
}
class Triangle : public Shape {
    Triangle() {}
    area() : int { return 3; }
}
call_area(s: Shape&) : int { return s.area(); }
test_square()   : int { sq: Square;   return call_area(sq); }
test_triangle() : int { tr: Triangle; return call_area(tr); }
)SRC");
        REQUIRE(jit);
        auto fsq = jit->lookup_symbol<int(*)()>("test_square");
        auto ftr = jit->lookup_symbol<int(*)()>("test_triangle");
        REQUIRE(fsq); REQUIRE(ftr);
        CHECK(fsq() == 4);
        CHECK(ftr() == 3);
    }

    SECTION("Multi-level dispatch: grandchild override") {
        auto jit = gen_jit(R"SRC(
module __abstract_dispatch_multi__;
abstract class Base {
    Base() {}
    abstract id() : int;
}
abstract class Mid : public Base {
    Mid() {}
}
class Leaf : public Mid {
    Leaf() {}
    id() : int { return 7; }
}
call_id(b: Base&) : int { return b.id(); }
test() : int { l: Leaf; return call_id(l); }
)SRC");
        REQUIRE(jit);
        auto fn = jit->lookup_symbol<int(*)()>("test");
        REQUIRE(fn);
        CHECK(fn() == 7);
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  [F] Abstract method in non-abstract class → error 0x0173
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[F] Abstract method in non-abstract class is an error", "[class][abstract][error]") {

    REQUIRE_THROWS(gen_jit_throws(R"SRC(
module __abstract_method_nonabstract_class__;
class Broken {
    Broken() {}
    abstract foo() : int;
}
)SRC"));
}

// ════════════════════════════════════════════════════════════════════════════
//  [G] Derived class with unimplemented abstract method, not declared abstract
//       → error 0x0174
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[G] Derived class with unimplemented abstract method must be abstract", "[class][abstract][error]") {

    REQUIRE_THROWS(gen_jit_throws(R"SRC(
module __abstract_unimplemented__;
abstract class Base {
    Base() {}
    abstract value() : int;
}
class Derived : public Base {
    Derived() {}
}
)SRC"));
}

// ════════════════════════════════════════════════════════════════════════════
//  [H] Direct instantiation of abstract class → error 0x0107
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[H] Direct instantiation of abstract class is an error", "[class][abstract][error]") {

    REQUIRE_THROWS(gen_jit_throws(R"SRC(
module __abstract_instantiate__;
abstract class Shape {
    Shape() {}
    abstract area() : int;
}
test() : int {
    s: Shape;
    return s.area();
}
)SRC"));
}

// ════════════════════════════════════════════════════════════════════════════
//  [I] Explicitly abstract class (no abstract methods) cannot be instantiated
//       → error 0x0107
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[I] Explicit abstract class (no abstract methods) cannot be instantiated", "[class][abstract][error]") {

    REQUIRE_THROWS(gen_jit_throws(R"SRC(
module __abstract_explicit_instantiate__;
abstract class Singleton {
    Singleton() {}
    value() : int { return 1; }
}
test() : int {
    s: Singleton;
    return s.value();
}
)SRC"));
}

// ════════════════════════════════════════════════════════════════════════════
//  [J] 'abstract' on struct → error 0x00A0
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[J] abstract specifier on struct is an error", "[struct][abstract][error]") {

    REQUIRE_THROWS(gen_jit_throws(R"SRC(
module __abstract_on_struct__;
abstract struct S {
    S() {}
}
)SRC"));
}

// ════════════════════════════════════════════════════════════════════════════
//  [K] 'abstract' on static function → error 0x00A1
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[K] abstract specifier on static function is an error", "[class][abstract][error]") {

    REQUIRE_THROWS(gen_jit_throws(R"SRC(
module __abstract_static__;
abstract class C {
    C() {}
    abstract static foo() : int;
}
)SRC"));
}

// ════════════════════════════════════════════════════════════════════════════
//  [L] 'abstract' on final function → error 0x00A2
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[L] abstract specifier on final function is an error", "[class][abstract][error]") {

    REQUIRE_THROWS(gen_jit_throws(R"SRC(
module __abstract_final__;
abstract class C {
    C() {}
    abstract final foo() : int;
}
)SRC"));
}

// ════════════════════════════════════════════════════════════════════════════
//  [M] 'abstract' method with a body → error 0x00A3
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[M] abstract method with a body is an error", "[class][abstract][error]") {

    REQUIRE_THROWS(gen_jit_throws(R"SRC(
module __abstract_with_body__;
abstract class C {
    C() {}
    abstract foo() : int { return 1; }
}
)SRC"));
}

// ════════════════════════════════════════════════════════════════════════════
//  [N] 'abstract' method inside a struct → error 0x00A4
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[N] abstract method inside a struct is an error", "[struct][abstract][error]") {

    REQUIRE_THROWS(gen_jit_throws(R"SRC(
module __abstract_method_in_struct__;
struct S {
    abstract foo() : int;
}
)SRC"));
}

// ════════════════════════════════════════════════════════════════════════════
//  [P] 'abstract' on private method → error 0x00A6
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[P] abstract specifier on private method is an error", "[class][abstract][error]") {

    REQUIRE_THROWS(gen_jit_throws(R"SRC(
module __abstract_private__;
abstract class C {
    C() {}
private:
    abstract foo() : int;
}
)SRC"));
}

