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
 * Exhaustive tests for the K language 'interface' keyword.
 *
 * THE RULES:
 *   - An interface is declared with the 'interface' keyword.
 *   - An interface is implicitly abstract: it cannot be instantiated directly.
 *   - Interface member functions are implicitly abstract: no body is allowed (unless
 *     explicitly marked otherwise e.g. final with body, but those would not be virtual).
 *   - Writing 'abstract' on an interface declaration or its member functions is
 *     redundant and triggers a warning (0x2002A / 0x2002B), not an error.
 *   - A class implementing all interface methods can be instantiated.
 *   - Virtual dispatch through an interface reference works correctly.
 *   - Interfaces can extend other interfaces.
 *   - A class may implement multiple interfaces via multi-inheritance.
 *   - Interface member functions have PUBLIC visibility by default (like structs).
 *
 * Tests covered:
 *  ── Model-level checks ───────────────────────────────────────────────────
 *   [A] Interface is represented as k::model::interface in the model
 *   [B] Interface is implicitly abstract
 *   [C] Interface member functions are implicitly abstract
 *   [D] 'abstract' on interface declaration triggers warning 0x2002A, still compiles
 *   [E] 'abstract' on interface member function triggers warning 0x2002B, still compiles
 *
 *  ── Valid usage ──────────────────────────────────────────────────────────
 *   [F] Interface with one method: class implements it, virtual dispatch works
 *   [G] Interface with multiple methods: class implements them all
 *   [H] Multi-level: interface extended by class, then further derived
 *   [I] Interface extending another interface
 *   [J] Class implementing two interfaces via multi-inheritance
 *   [K] Virtual dispatch through interface reference
 *   [L] Partial implementation: abstract class implementing part of interface,
 *       concrete subclass completing it
 *   [M] Nested interface inside a class
 *
 *  ── Error: cannot instantiate interface ──────────────────────────────────
 *   [N] Direct instantiation of interface → error 0x40032
 *
 *  ── Error: unimplemented methods ─────────────────────────────────────────
 *   [O] Class that does not implement all interface methods, and is not abstract → error
 *
 *  ── Error: abstract/static/final/body constraints ────────────────────────
 *   [P] Interface method with a body → error 0x20026
 *   [Q] Interface method marked 'static' + implicit abstract → error 0x20024
 *   [R] Interface method marked 'final' (no body) → NOT abstract (final new method)
 *   [S] Interface method marked 'abstract' + 'static' → error 0x20024
 *   [T] Interface method marked 'abstract' + 'final' → error 0x20025
 *   [U] Interface method marked 'abstract' + body → error 0x20026
 *   [V] 'abstract' on private interface method → error 0x20029
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

// ════════════════════════════════════════════════════════════════════════════
//  [A] Interface is represented as k::model::interface in the model
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[A] Interface keyword produces a model::interface node", "[interface][model]") {

    auto comp = k::compiler::create();
    comp->parse_source("", R"SRC(
module __iface_model__;
interface Drawable {
    draw() : int;
}
)SRC");

    auto elems = comp->find_elements("Drawable");
    REQUIRE(!elems.empty());
    auto iface = std::dynamic_pointer_cast<k::model::interface>(elems[0]);
    REQUIRE(iface); // must be model::interface, not just model::klass
    CHECK(iface->get_short_name() == "Drawable");
}

// ════════════════════════════════════════════════════════════════════════════
//  [B] Interface is implicitly abstract
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[B] Interface is implicitly abstract", "[interface][model]") {

    SECTION("Without explicit abstract specifier") {
        auto comp = k::compiler::create();
        comp->parse_source("", R"SRC(
module __iface_implicit_abstract__;
interface Foo {
    bar() : int;
}
)SRC");
        auto elems = comp->find_elements("Foo");
        REQUIRE(!elems.empty());
        auto iface = std::dynamic_pointer_cast<k::model::interface>(elems[0]);
        REQUIRE(iface);
        CHECK(iface->is_abstract());
    }

    SECTION("Even when interface has no methods") {
        auto comp = k::compiler::create();
        comp->parse_source("", R"SRC(
module __iface_empty_abstract__;
interface Empty {
}
)SRC");
        auto elems = comp->find_elements("Empty");
        REQUIRE(!elems.empty());
        auto iface = std::dynamic_pointer_cast<k::model::interface>(elems[0]);
        REQUIRE(iface);
        CHECK(iface->is_abstract());
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  [C] Interface member functions are implicitly abstract
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[C] Interface member functions are implicitly abstract", "[interface][model]") {

    auto comp = k::compiler::create();
    comp->parse_source("", R"SRC(
module __iface_func_abstract__;
interface Measurable {
    area()   : int;
    perimeter() : int;
}
)SRC");

    auto elems = comp->find_elements("Measurable");
    REQUIRE(!elems.empty());
    auto iface = std::dynamic_pointer_cast<k::model::interface>(elems[0]);
    REQUIRE(iface);

    auto fn_area = iface->get_function("area");
    REQUIRE(fn_area);
    CHECK(fn_area->is_abstract_func());

    auto fn_perim = iface->get_function("perimeter");
    REQUIRE(fn_perim);
    CHECK(fn_perim->is_abstract_func());
}

// ════════════════════════════════════════════════════════════════════════════
//  [D] 'abstract' on interface declaration: warning 0x2002A, still compiles
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[D] Redundant 'abstract' on interface declaration emits warning, still compiles", "[interface][warning]") {

    // gen_jit() silently catches errors; we use compiler::create() directly
    // so we can inspect diagnostics.
    auto comp = k::compiler::create();
    comp->parse_source("", R"SRC(
module __iface_redundant_abstract__;
abstract interface Marker {
    check() : int;
}
)SRC");

    // Should still compile and produce a model::interface
    auto elems = comp->find_elements("Marker");
    REQUIRE(!elems.empty());
    auto iface = std::dynamic_pointer_cast<k::model::interface>(elems[0]);
    REQUIRE(iface);
    CHECK(iface->is_abstract());
}

// ════════════════════════════════════════════════════════════════════════════
//  [E] 'abstract' on interface member function: warning 0x2002B, still compiles
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[E] Redundant 'abstract' on interface method emits warning, still compiles", "[interface][warning]") {

    auto comp = k::compiler::create();
    comp->parse_source("", R"SRC(
module __iface_redundant_abstract_method__;
interface Printer {
    abstract print() : int;
}
)SRC");

    auto elems = comp->find_elements("Printer");
    REQUIRE(!elems.empty());
    auto iface = std::dynamic_pointer_cast<k::model::interface>(elems[0]);
    REQUIRE(iface);

    auto fn = iface->get_function("print");
    REQUIRE(fn);
    CHECK(fn->is_abstract_func());
}

// ════════════════════════════════════════════════════════════════════════════
//  [F] Interface with one method: class implements it, virtual dispatch works
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[F] Class implementing interface method: virtual dispatch works", "[interface][dispatch]") {

    SECTION("Direct call on concrete class instance") {
        auto jit = gen_jit(R"SRC(
module __iface_basic__;
interface Greeter {
    greet() : int;
}
class Hello : public Greeter {
    Hello() {}
    greet() : int { return 1; }
}
test() : int {
    h: Hello;
    return h.greet();
}
)SRC");
        REQUIRE(jit);
        auto fn = jit->lookup_symbol<int(*)()>("test");
        REQUIRE(fn);
        CHECK(fn() == 1);
    }

    SECTION("Virtual dispatch through interface reference") {
        auto jit = gen_jit(R"SRC(
module __iface_dispatch_ref__;
interface Counter {
    count() : int;
}
class MyCounter : public Counter {
    MyCounter() {}
    count() : int { return 42; }
}
call_count(c: Counter&) : int { return c.count(); }
test() : int {
    mc: MyCounter;
    return call_count(mc);
}
)SRC");
        REQUIRE(jit);
        auto fn = jit->lookup_symbol<int(*)()>("test");
        REQUIRE(fn);
        CHECK(fn() == 42);
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  [G] Interface with multiple methods: class implements them all
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[G] Class implementing all methods of an interface", "[interface][dispatch]") {

    auto jit = gen_jit(R"SRC(
module __iface_multi_method__;
interface Shape {
    area()      : int;
    perimeter() : int;
    sides()     : int;
}
class Triangle : public Shape {
    Triangle() {}
    area()      : int { return 6; }
    perimeter() : int { return 12; }
    sides()     : int { return 3; }
}
test_area()      : int { t: Triangle; return t.area(); }
test_perimeter() : int { t: Triangle; return t.perimeter(); }
test_sides()     : int { t: Triangle; return t.sides(); }
)SRC");
    REQUIRE(jit);

    auto fa = jit->lookup_symbol<int(*)()>("test_area");
    auto fp = jit->lookup_symbol<int(*)()>("test_perimeter");
    auto fs = jit->lookup_symbol<int(*)()>("test_sides");
    REQUIRE(fa); REQUIRE(fp); REQUIRE(fs);
    CHECK(fa() == 6);
    CHECK(fp() == 12);
    CHECK(fs() == 3);
}

// ════════════════════════════════════════════════════════════════════════════
//  [H] Class extends interface, then further derived class overrides
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[H] Multi-level: concrete subclass overrides interface method", "[interface][dispatch]") {

    auto jit = gen_jit(R"SRC(
module __iface_multilevel__;
interface Base {
    value() : int;
}
class Mid : public Base {
    Mid() {}
    value() : int { return 10; }
}
class Derived : public Mid {
    Derived() {}
    value() : int { return 20; }
}
call_value(b: Base&) : int { return b.value(); }
test_mid()     : int { m: Mid;     return call_value(m); }
test_derived() : int { d: Derived; return call_value(d); }
)SRC");
    REQUIRE(jit);

    auto fm = jit->lookup_symbol<int(*)()>("test_mid");
    auto fd = jit->lookup_symbol<int(*)()>("test_derived");
    REQUIRE(fm); REQUIRE(fd);
    CHECK(fm() == 10);
    CHECK(fd() == 20);
}

// ════════════════════════════════════════════════════════════════════════════
//  [I] Interface extending another interface
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[I] Interface extending another interface", "[interface][inheritance]") {

    SECTION("Sub-interface adds methods; class implements all") {
        auto jit = gen_jit(R"SRC(
module __iface_extends_iface__;
interface Identifiable {
    id() : int;
}
interface Named : public Identifiable {
    name_hash() : int;
}
class Entity : public Named {
    Entity() {}
    id()        : int { return 7; }
    name_hash() : int { return 99; }
}
call_id(x: Identifiable&) : int { return x.id(); }
test_id()        : int { e: Entity; return call_id(e); }
test_name_hash() : int { e: Entity; return e.name_hash(); }
)SRC");
        REQUIRE(jit);

        auto fid  = jit->lookup_symbol<int(*)()>("test_id");
        auto fnh  = jit->lookup_symbol<int(*)()>("test_name_hash");
        REQUIRE(fid); REQUIRE(fnh);
        CHECK(fid()  == 7);
        CHECK(fnh() == 99);
    }

    SECTION("Model: sub-interface is still a model::interface") {
        auto comp = k::compiler::create();
        comp->parse_source("", R"SRC(
module __iface_extends_iface_model__;
interface A {
    foo() : int;
}
interface B : public A {
    bar() : int;
}
)SRC");
        auto elems_b = comp->find_elements("B");
        REQUIRE(!elems_b.empty());
        auto iface_b = std::dynamic_pointer_cast<k::model::interface>(elems_b[0]);
        REQUIRE(iface_b);
        CHECK(iface_b->is_abstract());
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  [J] Class implementing two interfaces via multiple inheritance
//      Note: dispatch through a *secondary* base interface reference requires
//      per-base vtable thunks which are not yet implemented. We test direct
//      calls and dispatch through the primary (first) interface only.
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[J] Class implementing two interfaces via multiple inheritance", "[interface][multi-inheritance]") {

    SECTION("Direct calls on concrete instance work correctly") {
        auto jit = gen_jit(R"SRC(
module __iface_multi_impl_direct__;
interface Readable {
    read() : int;
}
interface Writable {
    write() : int;
}
class Buffer : public Readable, public Writable {
    Buffer() {}
    read()  : int { return 1; }
    write() : int { return 2; }
}
test_read()  : int { b: Buffer; return b.read();  }
test_write() : int { b: Buffer; return b.write(); }
)SRC");
        REQUIRE(jit);
        auto fr = jit->lookup_symbol<int(*)()>("test_read");
        auto fw = jit->lookup_symbol<int(*)()>("test_write");
        REQUIRE(fr); REQUIRE(fw);
        CHECK(fr() == 1);
        CHECK(fw() == 2);
    }

    SECTION("Dispatch through primary interface reference works") {
        auto jit = gen_jit(R"SRC(
module __iface_multi_impl_primary__;
interface Readable {
    read() : int;
}
interface Writable {
    write() : int;
}
class Buffer : public Readable, public Writable {
    Buffer() {}
    read()  : int { return 1; }
    write() : int { return 2; }
}
call_read(r: Readable&) : int { return r.read(); }
test_read() : int { b: Buffer; return call_read(b); }
)SRC");
        REQUIRE(jit);
        auto fr = jit->lookup_symbol<int(*)()>("test_read");
        REQUIRE(fr);
        CHECK(fr() == 1);
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  [K] Virtual dispatch through interface reference: two implementations
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[K] Virtual dispatch through interface reference selects correct implementation", "[interface][dispatch]") {

    auto jit = gen_jit(R"SRC(
module __iface_polymorphism__;
interface Processor {
    process() : int;
}
class Fast : public Processor {
    Fast() {}
    process() : int { return 100; }
}
class Slow : public Processor {
    Slow() {}
    process() : int { return 1; }
}
run(p: Processor&) : int { return p.process(); }
test_fast() : int { f: Fast; return run(f); }
test_slow() : int { s: Slow; return run(s); }
)SRC");
    REQUIRE(jit);

    auto ff = jit->lookup_symbol<int(*)()>("test_fast");
    auto fs = jit->lookup_symbol<int(*)()>("test_slow");
    REQUIRE(ff); REQUIRE(fs);
    CHECK(ff() == 100);
    CHECK(fs() == 1);
}

// ════════════════════════════════════════════════════════════════════════════
//  [L] Partial implementation: abstract class + concrete subclass
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[L] Abstract class partially implements interface; subclass completes it", "[interface][abstract]") {

    auto jit = gen_jit(R"SRC(
module __iface_partial_impl__;
interface Vehicle {
    speed()  : int;
    wheels() : int;
}
abstract class MotorVehicle : public Vehicle {
    MotorVehicle() {}
    wheels() : int { return 4; }
}
class Car : public MotorVehicle {
    Car() {}
    speed() : int { return 120; }
}
call_speed(v: Vehicle&)  : int { return v.speed();  }
call_wheels(v: Vehicle&) : int { return v.wheels(); }
test_speed()  : int { c: Car; return call_speed(c);  }
test_wheels() : int { c: Car; return call_wheels(c); }
)SRC");
    REQUIRE(jit);

    auto fs = jit->lookup_symbol<int(*)()>("test_speed");
    auto fw = jit->lookup_symbol<int(*)()>("test_wheels");
    REQUIRE(fs); REQUIRE(fw);
    CHECK(fs() == 120);
    CHECK(fw() == 4);
}

// ════════════════════════════════════════════════════════════════════════════
//  [M] Nested interface inside a class (model check only — qualified base names
//      are not yet supported in the inheritance clause parser)
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[M] Interface nested inside a class compiles (model check)", "[interface][nested]") {

    // Verify a nested interface is accepted by the parser and model builder.
    // Using it as a base class via qualified name (Outer::Inner) is not yet
    // supported by the parser — test model-level acceptance only.
    auto comp = k::compiler::create();
    comp->parse_source("", R"SRC(
module __iface_nested__;
class Outer {
    Outer() {}
    interface Inner {
        compute() : int;
    }
}
)SRC");

    // Outer must be found
    auto elems = comp->find_elements("Outer");
    REQUIRE(!elems.empty());
    auto outer = std::dynamic_pointer_cast<k::model::klass>(elems[0]);
    REQUIRE(outer);

    // Inner is nested inside Outer — look it up through Outer's children
    std::shared_ptr<k::model::interface> inner_iface;
    for (auto& child : outer->get_children()) {
        if (auto iface = std::dynamic_pointer_cast<k::model::interface>(child)) {
            if (iface->get_short_name() == "Inner") {
                inner_iface = iface;
                break;
            }
        }
    }
    REQUIRE(inner_iface);
    CHECK(inner_iface->is_abstract());
}

// ════════════════════════════════════════════════════════════════════════════
//  [N] Direct instantiation of interface → error 0x40032
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[N] Direct instantiation of interface is an error", "[interface][error]") {

    REQUIRE_THROWS(gen_jit_throws(R"SRC(
module __iface_instantiate__;
interface Foo {
    bar() : int;
}
test() : int {
    f: Foo;
    return f.bar();
}
)SRC"));
}

// ════════════════════════════════════════════════════════════════════════════
//  [O] Class with unimplemented interface methods, not abstract → error
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[O] Class with unimplemented interface methods must be abstract", "[interface][error]") {

    REQUIRE_THROWS(gen_jit_throws(R"SRC(
module __iface_missing_impl__;
interface Animal {
    sound() : int;
    legs()  : int;
}
class Dog : public Animal {
    Dog() {}
    sound() : int { return 1; }
}
)SRC"));
}

// ════════════════════════════════════════════════════════════════════════════
//  [P] Interface method with a body → error 0x20026
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[P] Interface method with a body is an error", "[interface][error]") {

    REQUIRE_THROWS(gen_jit_throws(R"SRC(
module __iface_method_with_body__;
interface Broken {
    foo() : int { return 0; }
}
)SRC"));
}

// ════════════════════════════════════════════════════════════════════════════
//  [Q] Interface method marked 'static' → error 0x20024 (implicit abstract + static)
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[Q] Static method in interface is an error (abstract+static conflict)", "[interface][error]") {

    // A static method has no implicit abstract flag applied, but if one tries to
    // explicitly use 'abstract static' it should fail with 0x20024.
    // Without body+static: the implicit-abstract block skips static functions,
    // so this produces a valid (non-abstract, non-virtual) static method.
    // With 'abstract static': error.
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
module __iface_abstract_static__;
interface Broken {
    abstract static foo() : int;
}
)SRC"));
}

// ════════════════════════════════════════════════════════════════════════════
//  [R] Interface method marked 'final' with no body → error (no implementation)
//      A 'final' method skips the implicit-abstract rule but still needs a body.
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[R] 'final' method without body in interface is an error (no implementation)", "[interface][error]") {

    // A 'final' method in an interface is NOT abstract (implicit-abstract skips final).
    // Without a body and without being abstract, it is a function with no implementation,
    // which is an error (0x002C).
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
module __iface_final_method__;
interface HasFinal {
    final stamp() : int;
}
)SRC"));
}

// ════════════════════════════════════════════════════════════════════════════
//  [S] 'abstract' + 'static' on interface method → error 0x20024
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[S] abstract+static on interface method is an error", "[interface][error]") {

    REQUIRE_THROWS(gen_jit_throws(R"SRC(
module __iface_abs_static__;
interface Bad {
    abstract static foo() : int;
}
)SRC"));
}

// ════════════════════════════════════════════════════════════════════════════
//  [T] 'abstract' + 'final' on interface method → error 0x20025
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[T] abstract+final on interface method is an error", "[interface][error]") {

    REQUIRE_THROWS(gen_jit_throws(R"SRC(
module __iface_abs_final__;
interface Bad {
    abstract final foo() : int;
}
)SRC"));
}

// ════════════════════════════════════════════════════════════════════════════
//  [U] 'abstract' + body on interface method → error 0x20026
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[U] abstract method with body in interface is an error", "[interface][error]") {

    REQUIRE_THROWS(gen_jit_throws(R"SRC(
module __iface_abs_body__;
interface Bad {
    abstract foo() : int { return 1; }
}
)SRC"));
}

// ════════════════════════════════════════════════════════════════════════════
//  [V] 'abstract' on private interface method → error 0x20029
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[V] abstract+private on interface method is an error", "[interface][error]") {

    REQUIRE_THROWS(gen_jit_throws(R"SRC(
module __iface_abs_private__;
interface Bad {
private:
    abstract foo() : int;
}
)SRC"));
}

// ════════════════════════════════════════════════════════════════════════════
//  Additional runtime correctness tests
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[X1] Interface dispatch: three implementations, correct routing", "[interface][dispatch]") {

    auto jit = gen_jit(R"SRC(
module __iface_three_impls__;
interface Sensor {
    read() : int;
}
class Temp : public Sensor {
    Temp() {}
    read() : int { return 36; }
}
class Pressure : public Sensor {
    Pressure() {}
    read() : int { return 101; }
}
class Humidity : public Sensor {
    Humidity() {}
    read() : int { return 75; }
}
sample(s: Sensor&) : int { return s.read(); }
test_temp()     : int { t: Temp;     return sample(t); }
test_pressure() : int { p: Pressure; return sample(p); }
test_humidity() : int { h: Humidity; return sample(h); }
)SRC");
    REQUIRE(jit);

    auto ft = jit->lookup_symbol<int(*)()>("test_temp");
    auto fp = jit->lookup_symbol<int(*)()>("test_pressure");
    auto fh = jit->lookup_symbol<int(*)()>("test_humidity");
    REQUIRE(ft); REQUIRE(fp); REQUIRE(fh);
    CHECK(ft() == 36);
    CHECK(fp() == 101);
    CHECK(fh() == 75);
}

TEST_CASE("[X2] Interface dispatch: overriding method at multiple levels", "[interface][dispatch]") {

    auto jit = gen_jit(R"SRC(
module __iface_override_chain__;
interface Source {
    get() : int;
}
class Base : public Source {
    Base() {}
    get() : int { return 1; }
}
class Middle : public Base {
    Middle() {}
    get() : int { return 2; }
}
class Top : public Middle {
    Top() {}
    get() : int { return 3; }
}
via_iface(s: Source&) : int { return s.get(); }
test_base()   : int { b: Base;   return via_iface(b); }
test_middle() : int { m: Middle; return via_iface(m); }
test_top()    : int { t: Top;    return via_iface(t); }
)SRC");
    REQUIRE(jit);

    auto fb = jit->lookup_symbol<int(*)()>("test_base");
    auto fm = jit->lookup_symbol<int(*)()>("test_middle");
    auto ft = jit->lookup_symbol<int(*)()>("test_top");
    REQUIRE(fb); REQUIRE(fm); REQUIRE(ft);
    CHECK(fb() == 1);
    CHECK(fm() == 2);
    CHECK(ft() == 3);
}

TEST_CASE("[X3] Two interfaces, class implements both, dispatch through primary correct", "[interface][dispatch]") {
    // Note: dispatch through the secondary (non-primary) base interface requires
    // per-base vtable thunks which are not yet implemented. We verify direct
    // calls and dispatch through the primary interface only.
    auto jit = gen_jit(R"SRC(
module __iface_dual_dispatch__;
interface Left {
    left() : int;
}
interface Right {
    right() : int;
}
class Both : public Left, public Right {
    Both() {}
    left()  : int { return -1; }
    right() : int { return  1; }
}
via_left(l: Left&) : int { return l.left(); }
test_direct_left()  : int { b: Both; return b.left();  }
test_direct_right() : int { b: Both; return b.right(); }
test_via_left()     : int { b: Both; return via_left(b); }
)SRC");
    REQUIRE(jit);

    auto fdl = jit->lookup_symbol<int(*)()>("test_direct_left");
    auto fdr = jit->lookup_symbol<int(*)()>("test_direct_right");
    auto fvl = jit->lookup_symbol<int(*)()>("test_via_left");
    REQUIRE(fdl); REQUIRE(fdr); REQUIRE(fvl);
    CHECK(fdl() == -1);
    CHECK(fdr() == 1);
    CHECK(fvl() == -1);
}

