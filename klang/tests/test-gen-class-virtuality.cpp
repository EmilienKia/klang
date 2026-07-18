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
 * Exhaustive tests for K language virtuality rules.
 *
 * THE RULES:
 *   struct = pure aggregation — NO virtuality at all:
 *     - No vtable, no virtual member functions
 *     - 'virtual' in base clause is a compile-time error
 *     - cross-inheritance struct↔class is a compile-time error (0x00BE)
 *
 *   class = full virtuality (automatic virtual dispatch):
 *     - All non-static, non-private, non-constructor/destructor member functions
 *       are automatically virtual (including public and protected functions).
 *     - Exception: a NEW function declared 'final' is NOT virtual (no vtable slot).
 *     - A function overriding an inherited virtual and declared 'final' remains
 *       virtual (takes the slot, overrides) but cannot be further overridden.
 *     - Private functions are NOT virtual.
 *     - Static functions are NOT virtual.
 *   - Constructors are NOT virtual (no dispatchable pointer).
 *   - Destructors ARE virtual once a class reaches ::k::Object (which
 *     declares the universal destructor vtable slot 0 — see
 *     test-gen-virtual-destructor.cpp for dedicated coverage); a class with
 *     no vtable at all (no stdlib / no Object in scope) still has no
 *     destructor vtable slot.
 *
 * Tests covered:
 *  ── struct no-virtuality enforcement ─────────────────────────────────────
 *   [A] struct methods are NOT virtual (no vtable, no dispatch)
 *   [B] struct with virtual base → error
 *   [C] class inheriting from struct → error 0x00BE
 *   [D] struct inheriting from class → error 0x00BE
 *  ── class auto-virtual public methods ────────────────────────────────────
 *   [E] public class methods are automatically virtual (vtable slot assigned)
 *   [F] virtual dispatch works through base reference
 *   [G] multi-level dispatch (A → B → C)
 *  ── class auto-virtual protected methods ─────────────────────────────────
 *   [H] protected class methods are automatically virtual
 *   [I] protected virtual dispatch works through base reference
 *  ── class private methods are NOT virtual ────────────────────────────────
 *   [J] private class method has no vtable slot
 *   [K] private method cannot override a virtual function
 *  ── class static methods are NOT virtual ─────────────────────────────────
 *   [L] static class method has no vtable slot
 *  ── constructors are NOT virtual ─────────────────────────────────────────
 *   [M] constructors have no vtable slot (no dispatchable pointer)
 *  ── 'final' on a NEW method — NOT virtual ────────────────────────────────
 *   [N] new 'final' method in class is NOT virtual
 *   [O] new 'final' method is callable directly
 *  ── 'final' on an OVERRIDING method — virtual but sealed ─────────────────
 *   [P] overriding 'final' method IS virtual (dispatches to sealed override)
 *   [Q] attempt to override a 'final' virtual → new vtable branch (warning)
 *  ── non-virtual qualified call ────────────────────────────────────────────
 *   [R] Base::method(obj) always calls Base's implementation (no dispatch)
 *  ── qualified parent call from inside an override ─────────────────────────
 *   [S] Base::method(this)   — free-function-style: bypasses virtual dispatch
 *   [T] this.Base::method()  — dot-qualified on this: bypasses virtual dispatch
 *   [U] Base::method()       — implicit this injection: bypasses virtual dispatch
 *        Multi-level: grandchild calls grandparent via qualified call
 *        With arguments: Base::method(a, b) from inside an override
 *  ── cross-struct/class error ──────────────────────────────────────────────
 *   [S] struct inheriting from class → error
 *   [T] class inheriting from struct → error
 *  ── 'final' is NOT allowed on constructors/destructors (ERR_FINAL_ON_CTOR_DTOR = 0x0195) ──
 *   [V] final on a class constructor → error
 *   [W] final on a class destructor → error
 *   [X] final on a struct constructor → error
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

// ════════════════════════════════════════════════════════════════════════════
//  [A] struct methods are NOT virtual — no dispatch, no vtable
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[A] struct methods are NOT virtual", "[struct][virtuality]") {

    SECTION("Calling struct method via base ref does NOT dispatch to derived override") {
        // Unlike class, struct has no vtable: calling through a Base& calls Base::method,
        // even if the object is actually a Derived.
        auto jit = gen_jit(R"SRC(
module __struct_no_virt_dispatch__;
struct Base {
    value() : int { return 1; }
}
struct Derived : public Base {
    value() : int { return 2; }
}
call_value(b: Base&) : int {
    return b.value();
}
test() : int {
    d: Derived;
    return call_value(d);
}
)SRC", false, false);
        REQUIRE(jit);
        auto fn = jit->lookup_symbol<int(*)()>("test");
        REQUIRE(fn);
        // No vtable: call_value calls Base::value → 1
        CHECK(fn() == 1);
    }

    SECTION("Struct method can still be called directly on derived") {
        auto jit = gen_jit(R"SRC(
module __struct_direct_call__;
struct Base {
    compute() : int { return 10; }
}
struct Derived : public Base {
    compute() : int { return 20; }
}
test_base() : int {
    b: Base;
    return b.compute();
}
test_derived() : int {
    d: Derived;
    return d.compute();
}
)SRC", false, false);
        REQUIRE(jit);
        auto fn_base    = jit->lookup_symbol<int(*)()>("test_base");
        auto fn_derived = jit->lookup_symbol<int(*)()>("test_derived");
        REQUIRE(fn_base);
        REQUIRE(fn_derived);
        CHECK(fn_base()    == 10);
        CHECK(fn_derived() == 20);
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  [B] struct cannot use virtual inheritance — the 'virtual' keyword does not
//      exist in K language base clauses. For classes, ALL bases are implicitly
//      virtual. For structs, there is no virtuality at all.
//      (This test block is intentionally empty — the rule is enforced by the
//       type system, not a runtime keyword check.)
// ════════════════════════════════════════════════════════════════════════════

// ════════════════════════════════════════════════════════════════════════════
//  [C,D] / [S,T] cross-struct/class inheritance → error 0x00BE
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[C/D] cross-struct/class inheritance is forbidden", "[struct][class][virtuality][error]") {

    SECTION("[C] class inheriting from struct → error") {
        REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
module __class_from_struct_err__;
struct S { S() {} }
class C : public S { C() {} }
)SRC", false, false), k::model::gen::resolution_error);
    }

    SECTION("[D] struct inheriting from class → error") {
        REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
module __struct_from_class_err__;
class C { C() {} }
struct S : public C { S() {} }
)SRC", false, false), k::model::gen::resolution_error);
    }

    SECTION("struct inheriting from struct is OK") {
        auto jit = gen_jit(R"SRC(
module __struct_from_struct_ok__;
struct A { x: int; A() : x(5) {} }
struct B : public A { y: int; B() : y(3) {} }
test() : int {
    b: B;
    return b.x + b.y;
}
)SRC", false, false);
        REQUIRE(jit);
        auto fn = jit->lookup_symbol<int(*)()>("test");
        REQUIRE(fn);
        CHECK(fn() == 8);
    }

    SECTION("class inheriting from class is OK") {
        auto jit = gen_jit(R"SRC(
module __class_from_class_ok__;
class A {
    val() : int { return 1; }
}
class B : public A {
    val() : int { return 2; }
}
test() : int {
    b: B;
    return b.val();
}
)SRC", false, false);
        REQUIRE(jit);
        auto fn = jit->lookup_symbol<int(*)()>("test");
        REQUIRE(fn);
        CHECK(fn() == 2);
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  [E] public class methods are automatically virtual
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[E] public class methods are automatically virtual", "[class][virtuality]") {

    SECTION("public method in base class gets vtable slot") {
        auto jit = gen_jit(R"SRC(
module __cls_pub_auto_virt__;
class Animal {
    sound() : int { return 1; }
}
class Cat : public Animal {
    sound() : int { return 3; }
}
call_sound(a: Animal&) : int {
    return a.sound();
}
test_animal() : int {
    a: Animal;
    return call_sound(a);
}
test_cat() : int {
    c: Cat;
    return call_sound(c);
}
)SRC", false, false);
        REQUIRE(jit);
        auto fn_a = jit->lookup_symbol<int(*)()>("test_animal");
        auto fn_c = jit->lookup_symbol<int(*)()>("test_cat");
        REQUIRE(fn_a); REQUIRE(fn_c);
        CHECK(fn_a() == 1); // Base dispatch
        CHECK(fn_c() == 3); // Derived dispatch
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  [F] virtual dispatch works through base reference
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[F] virtual dispatch through base reference", "[class][virtuality]") {

    auto jit = gen_jit(R"SRC(
module __cls_virt_dispatch__;
class Shape {
    area() : int { return 0; }
}
class Circle : public Shape {
    area() : int { return 314; }
}
class Square : public Shape {
    area() : int { return 100; }
}
get_area(s: Shape&) : int {
    return s.area();
}
test_circle() : int {
    c: Circle;
    return get_area(c);
}
test_square() : int {
    s: Square;
    return get_area(s);
}
test_shape() : int {
    s: Shape;
    return get_area(s);
}
)SRC", false, false);
    REQUIRE(jit);

    auto fn_circle = jit->lookup_symbol<int(*)()>("test_circle");
    auto fn_square = jit->lookup_symbol<int(*)()>("test_square");
    auto fn_shape  = jit->lookup_symbol<int(*)()>("test_shape");
    REQUIRE(fn_circle); REQUIRE(fn_square); REQUIRE(fn_shape);
    CHECK(fn_circle() == 314);
    CHECK(fn_square() == 100);
    CHECK(fn_shape()  == 0);
}

// ════════════════════════════════════════════════════════════════════════════
//  [G] multi-level virtual dispatch (A → B → C)
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[G] multi-level class virtual dispatch (3 levels)", "[class][virtuality]") {

    auto jit = gen_jit(R"SRC(
module __cls_multilevel_dispatch__;
class A {
    id() : int { return 1; }
}
class B : public A {
    id() : int { return 2; }
}
class C : public B {
    id() : int { return 3; }
}
call_id(a: A&) : int { return a.id(); }
test_a() : int { a: A; return call_id(a); }
test_b() : int { b: B; return call_id(b); }
test_c() : int { c: C; return call_id(c); }
)SRC", false, false);
    REQUIRE(jit);

    auto fn_a = jit->lookup_symbol<int(*)()>("test_a");
    auto fn_b = jit->lookup_symbol<int(*)()>("test_b");
    auto fn_c = jit->lookup_symbol<int(*)()>("test_c");
    REQUIRE(fn_a); REQUIRE(fn_b); REQUIRE(fn_c);
    CHECK(fn_a() == 1);
    CHECK(fn_b() == 2);
    CHECK(fn_c() == 3);
}

// ════════════════════════════════════════════════════════════════════════════
//  [H,I] protected class methods are automatically virtual
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[H] protected class methods are automatically virtual", "[class][virtuality]") {

    SECTION("protected method gets vtable slot and dispatches virtually") {
        auto jit = gen_jit(R"SRC(
module __cls_prot_auto_virt__;
class Base {
protected:
    compute() : int { return 10; }
public:
    run() : int { return this.compute(); }
}
class Derived : public Base {
protected:
    compute() : int { return 20; }
}
call_run(b: Base&) : int {
    return b.run();
}
test_base() : int {
    b: Base;
    return call_run(b);
}
test_derived() : int {
    d: Derived;
    return call_run(d);
}
)SRC", false, false);
        REQUIRE(jit);
        auto fn_b = jit->lookup_symbol<int(*)()>("test_base");
        auto fn_d = jit->lookup_symbol<int(*)()>("test_derived");
        REQUIRE(fn_b); REQUIRE(fn_d);
        // run() calls compute() virtually
        CHECK(fn_b() == 10); // Base::compute
        CHECK(fn_d() == 20); // Derived::compute (virtual dispatch)
    }
}

TEST_CASE("[I] protected virtual dispatch through run() bridge", "[class][virtuality]") {

    auto jit = gen_jit(R"SRC(
module __cls_prot_virt_dispatch__;
class Formatter {
protected:
    format_val() : int { return 0; }
public:
    render() : int { return this.format_val() * 2; }
}
class HexFormatter : public Formatter {
protected:
    format_val() : int { return 16; }
}
class DecFormatter : public Formatter {
protected:
    format_val() : int { return 10; }
}
render_it(f: Formatter&) : int {
    return f.render();
}
test_hex() : int { h: HexFormatter; return render_it(h); }
test_dec() : int { d: DecFormatter; return render_it(d); }
test_base() : int { f: Formatter; return render_it(f); }
)SRC", false, false);
    REQUIRE(jit);

    auto fn_hex  = jit->lookup_symbol<int(*)()>("test_hex");
    auto fn_dec  = jit->lookup_symbol<int(*)()>("test_dec");
    auto fn_base = jit->lookup_symbol<int(*)()>("test_base");
    REQUIRE(fn_hex); REQUIRE(fn_dec); REQUIRE(fn_base);
    CHECK(fn_hex()  == 32);  // 16 * 2
    CHECK(fn_dec()  == 20);  // 10 * 2
    CHECK(fn_base() == 0);   //  0 * 2
}

// ════════════════════════════════════════════════════════════════════════════
//  [J,K] private class methods are NOT virtual
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[J] private class method is NOT virtual", "[class][virtuality]") {

    SECTION("private method in base class is not dispatched virtually") {
        // A private method defines new, non-virtual behaviour.
        // A derived class can define a method with the same name: it is also
        // a new non-virtual function, not an override.
        auto jit = gen_jit(R"SRC(
module __cls_priv_not_virt__;
class Base {
    private helper() : int { return 1; }
    public run() : int { return this.helper(); }
}
class Derived : public Base {
    private helper() : int { return 2; }
}
call_run(b: Base&) : int {
    return b.run();
}
test_base() : int {
    b: Base;
    return call_run(b);
}
test_derived() : int {
    d: Derived;
    return call_run(d);
}
)SRC", false, false);
        REQUIRE(jit);
        auto fn_b = jit->lookup_symbol<int(*)()>("test_base");
        auto fn_d = jit->lookup_symbol<int(*)()>("test_derived");
        REQUIRE(fn_b); REQUIRE(fn_d);
        // Private helper is NOT virtual: run() always calls Base::helper
        CHECK(fn_b() == 1);
        CHECK(fn_d() == 1); // Derived::helper is not dispatched by Base::run()
    }
}

TEST_CASE("[K] private method in derived cannot override virtual of same name", "[class][virtuality][error]") {

    SECTION("private function with same signature as inherited virtual → error") {
        REQUIRE_THROWS(gen_jit_throws(R"SRC(
module __cls_priv_override_err__;
class Base {
    foo() : int { return 1; }
}
class Derived : public Base {
    private foo() : int { return 2; }
}
)SRC", false, false));
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  [L] static class methods are NOT virtual
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[L] static class methods are NOT virtual", "[class][virtuality]") {

    auto jit = gen_jit(R"SRC(
module __cls_static_not_virt__;
class Counter {
    static count: int;
    static reset() { count = 0; }
    static increment() { count = count + 1; }
    static get() : int { return count; }
}
test() : int {
    Counter::reset();
    Counter::increment();
    Counter::increment();
    Counter::increment();
    return Counter::get();
}
)SRC", false, false);
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 3);
}

// ════════════════════════════════════════════════════════════════════════════
//  [N,O] 'final' on a NEW method in class — NOT virtual (no vtable slot)
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[N] 'final' new method in class is NOT virtual", "[class][virtuality][final]") {

    SECTION("final new method: no dispatch through base ref — always calls the owner") {
        // A 'final' new function is NOT placed in the vtable.
        // When called through a Base& on a Derived object, the static type determines
        // which function is called (Base::compute for Base&).
        auto jit = gen_jit(R"SRC(
module __cls_final_new_not_virt__;
class Base {
    final compute() : int { return 10; }
}
class Derived : public Base {
    // 'compute' in Derived: Base::compute is final (not in vtable),
    // so Derived cannot override it.  Derived declares its own new function.
    extra() : int { return 20; }
}
test_base() : int {
    b: Base;
    return b.compute();
}
test_derived_compute() : int {
    d: Derived;
    return d.compute();
}
)SRC", false, false);
        REQUIRE(jit);
        auto fn_b = jit->lookup_symbol<int(*)()>("test_base");
        auto fn_d = jit->lookup_symbol<int(*)()>("test_derived_compute");
        REQUIRE(fn_b); REQUIRE(fn_d);
        CHECK(fn_b() == 10);
        CHECK(fn_d() == 10); // calls Base::compute (no override possible since it's final non-virtual)
    }
}

TEST_CASE("[O] 'final' new method is callable directly", "[class][virtuality][final]") {

    auto jit = gen_jit(R"SRC(
module __cls_final_direct_call__;
class C {
    final compute() : int { return 42; }
}
test() : int {
    c: C;
    return c.compute();
}
)SRC", false, false);
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

// ════════════════════════════════════════════════════════════════════════════
//  [P,Q] 'final' on an OVERRIDING method — virtual but sealed
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[P] overriding 'final' method IS virtual but sealed", "[class][virtuality][final]") {

    SECTION("final override is dispatched virtually") {
        auto jit = gen_jit(R"SRC(
module __cls_final_override_virt__;
class A {
    val() : int { return 1; }
}
class B : public A {
    final val() : int { return 2; }
}
call_val(a: A&) : int {
    return a.val();
}
test_a() : int { a: A; return call_val(a); }
test_b() : int { b: B; return call_val(b); }
)SRC", false, false);
        REQUIRE(jit);
        auto fn_a = jit->lookup_symbol<int(*)()>("test_a");
        auto fn_b = jit->lookup_symbol<int(*)()>("test_b");
        REQUIRE(fn_a); REQUIRE(fn_b);
        CHECK(fn_a() == 1); // A::val
        CHECK(fn_b() == 2); // B::val (final override, still dispatched)
    }
}

TEST_CASE("[Q] overriding a 'final' virtual in grandchild: warning + new vtable slot", "[class][virtuality][final]") {

    // When C tries to override B::val which is declared final:
    // - compiler emits a warning
    // - C::val gets a NEW vtable slot (it does NOT replace B::val's slot, which is sealed)
    // - dispatch through A& or B& still uses the original sealed slot → B::val = 2
    // - C::val is only reachable through its own new slot (direct call or via C's vtable)
    auto jit = gen_jit(R"SRC(
module __cls_final_override_branch__;
class A {
    val() : int { return 1; }
}
class B : public A {
    final val() : int { return 2; }
}
class C : public B {
    // Overriding B::val which is final → compiler warning, C::val gets NEW slot
    val() : int { return 3; }
}
call_val_via_a(a: A&) : int {
    return a.val();
}
call_val_via_b(b: B&) : int {
    return b.val();
}
test_a() : int { a: A; return call_val_via_a(a); }
test_b() : int { b: B; return call_val_via_a(b); }
test_c_via_a() : int { c: C; return call_val_via_a(c); }
test_c_via_b() : int { c: C; return call_val_via_b(c); }
)SRC", false, false);
    REQUIRE(jit);

    auto fn_a       = jit->lookup_symbol<int(*)()>("test_a");
    auto fn_b       = jit->lookup_symbol<int(*)()>("test_b");
    auto fn_c_via_a = jit->lookup_symbol<int(*)()>("test_c_via_a");
    auto fn_c_via_b = jit->lookup_symbol<int(*)()>("test_c_via_b");
    REQUIRE(fn_a); REQUIRE(fn_b); REQUIRE(fn_c_via_a); REQUIRE(fn_c_via_b);
    CHECK(fn_a()       == 1); // A::val
    CHECK(fn_b()       == 2); // B::val (final override, dispatched via A's vtable slot)
    // C::val attempts to override B's final val → compiler warning.
    // C::val is placed in a NEW vtable slot (not A's slot) because B::val is final-sealed.
    // Dispatch via A& uses A's slot (slot 0) → B::val = 2 (not C::val).
    // Dispatch via B& uses B's slot (also slot 0, sealed) → B::val = 2.
    CHECK(fn_c_via_a() == 2); // A's slot → B::val (C::val is in a different slot)
    CHECK(fn_c_via_b() == 2); // B's slot (sealed by final) → B::val
}

// ════════════════════════════════════════════════════════════════════════════
//  [R] Non-virtual qualified call Base::method(obj)
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[R] non-virtual qualified call Base::method(obj) bypasses dispatch", "[class][virtuality]") {

    auto jit = gen_jit(R"SRC(
module __cls_nonvirt_qual_call__;
class Base {
    value() : int { return 10; }
}
class Derived : public Base {
    value() : int { return 20; }
}
call_base_direct(d: Derived&) : int {
    return Base::value(d);
}
call_dispatch(b: Base&) : int {
    return b.value();
}
test_direct() : int {
    d: Derived;
    return call_base_direct(d);
}
test_dispatch() : int {
    d: Derived;
    return call_dispatch(d);
}
)SRC", false, false);
    REQUIRE(jit);

    auto fn_direct   = jit->lookup_symbol<int(*)()>("test_direct");
    auto fn_dispatch = jit->lookup_symbol<int(*)()>("test_dispatch");
    REQUIRE(fn_direct); REQUIRE(fn_dispatch);
    // Non-virtual qualified call always goes to Base::value → 10
    CHECK(fn_direct()   == 10);
    // Virtual dispatch through Base& calls Derived::value → 20
    CHECK(fn_dispatch() == 20);
}

// ════════════════════════════════════════════════════════════════════════════
//  [S] Base::method(this) — explicit this, free-function-style qualified call
//  [T] this.Base::method() — dot-qualified call on this reference
//  [U] Base::method()      — implicit this injection from inside a member
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[S] Base::method(this) from inside an override calls base non-virtually", "[class][virtuality][super]") {

    auto jit = gen_jit(R"SRC(
module __cls_super_explicit__;
class Base {
    value() : int { return 10; }
}
class Derived : public Base {
    value() : int {
        return Base::value(this) + 1;
    }
}
call_virtual(b: Base&) : int { return b.value(); }
test_direct() : int {
    d: Derived;
    return d.value();
}
test_virtual() : int {
    d: Derived;
    return call_virtual(d);
}
)SRC", false, false);
    REQUIRE(jit);

    auto fn_direct  = jit->lookup_symbol<int(*)()>("test_direct");
    auto fn_virtual = jit->lookup_symbol<int(*)()>("test_virtual");
    REQUIRE(fn_direct); REQUIRE(fn_virtual);
    // Base::value(this) + 1 = 10 + 1 = 11 (non-virtual, base implementation)
    CHECK(fn_direct()  == 11);
    // virtual dispatch through Base& → Derived::value → 11
    CHECK(fn_virtual() == 11);
}

TEST_CASE("[T] this.Base::method() from inside an override calls base non-virtually", "[class][virtuality][super]") {

    auto jit = gen_jit(R"SRC(
module __cls_super_dot__;
class Base {
    value() : int { return 10; }
}
class Derived : public Base {
    value() : int {
        return this.Base::value() + 2;
    }
}
call_virtual(b: Base&) : int { return b.value(); }
test_direct() : int {
    d: Derived;
    return d.value();
}
test_virtual() : int {
    d: Derived;
    return call_virtual(d);
}
)SRC", false, false);
    REQUIRE(jit);

    auto fn_direct  = jit->lookup_symbol<int(*)()>("test_direct");
    auto fn_virtual = jit->lookup_symbol<int(*)()>("test_virtual");
    REQUIRE(fn_direct); REQUIRE(fn_virtual);
    // this.Base::value() + 2 = 10 + 2 = 12
    CHECK(fn_direct()  == 12);
    // virtual dispatch → Derived::value → 12
    CHECK(fn_virtual() == 12);
}

TEST_CASE("[U] Base::method() with implicit this from inside an override", "[class][virtuality][super]") {

    auto jit = gen_jit(R"SRC(
module __cls_super_implicit__;
class Base {
    value() : int { return 10; }
}
class Derived : public Base {
    value() : int {
        return Base::value() + 3;
    }
}
call_virtual(b: Base&) : int { return b.value(); }
test_direct() : int {
    d: Derived;
    return d.value();
}
test_virtual() : int {
    d: Derived;
    return call_virtual(d);
}
)SRC", false, false);
    REQUIRE(jit);

    auto fn_direct  = jit->lookup_symbol<int(*)()>("test_direct");
    auto fn_virtual = jit->lookup_symbol<int(*)()>("test_virtual");
    REQUIRE(fn_direct); REQUIRE(fn_virtual);
    // Base::value() + 3 = 10 + 3 = 13
    CHECK(fn_direct()  == 13);
    // virtual dispatch → Derived::value → 13
    CHECK(fn_virtual() == 13);
}

TEST_CASE("[S/T/U] Multi-level: grandchild calls grandparent via qualified call", "[class][virtuality][super]") {

    // A → B → C, each overrides value().
    // C::value calls B::value() (not A's), B::value calls A::value().
    auto jit = gen_jit(R"SRC(
module __cls_super_multilevel__;
class A {
    value() : int { return 1; }
}
class B : public A {
    value() : int { return A::value() + 10; }
}
class C : public B {
    value() : int { return B::value() + 100; }
}
call_virtual(a: A&) : int { return a.value(); }
test_c() : int {
    c: C;
    return c.value();
}
test_dispatch() : int {
    c: C;
    return call_virtual(c);
}
)SRC", false, false);
    REQUIRE(jit);

    auto fn_c        = jit->lookup_symbol<int(*)()>("test_c");
    auto fn_dispatch = jit->lookup_symbol<int(*)()>("test_dispatch");
    REQUIRE(fn_c); REQUIRE(fn_dispatch);
    // C::value = B::value + 100 = (A::value + 10) + 100 = 1 + 10 + 100 = 111
    CHECK(fn_c()        == 111);
    // virtual dispatch → C::value → 111
    CHECK(fn_dispatch() == 111);
}

TEST_CASE("[S/T/U] Base::method() with arguments from inside an override", "[class][virtuality][super]") {

    auto jit = gen_jit(R"SRC(
module __cls_super_args__;
class Calc {
    add(a: int, b: int) : int { return a + b; }
}
class ExtCalc : public Calc {
    add(a: int, b: int) : int {
        return Calc::add(a, b) * 2;
    }
}
test() : int {
    e: ExtCalc;
    return e.add(3, 4);
}
)SRC", false, false);
    REQUIRE(jit);

    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    // Calc::add(3,4) = 7, ExtCalc::add = 7 * 2 = 14
    CHECK(fn() == 14);
}

// ════════════════════════════════════════════════════════════════════════════
//  Additional: class with mixed public/protected/private in same class
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Class with mixed visibility sections — virtual/non-virtual", "[class][virtuality]") {

    auto jit = gen_jit(R"SRC(
module __cls_mixed_vis_virt__;
class Engine {
public:
    start() : int { return this.init() + this.run_loop(); }
protected:
    init() : int { return 1; }
    run_loop() : int { return 2; }
private:
    internal_check() : int { return 99; }
}
class TurboEngine : public Engine {
protected:
    init() : int { return 10; }
    run_loop() : int { return 20; }
}
call_start(e: Engine&) : int {
    return e.start();
}
test_engine() : int {
    e: Engine;
    return call_start(e);
}
test_turbo() : int {
    t: TurboEngine;
    return call_start(t);
}
)SRC", false, false);
    REQUIRE(jit);

    auto fn_engine = jit->lookup_symbol<int(*)()>("test_engine");
    auto fn_turbo  = jit->lookup_symbol<int(*)()>("test_turbo");
    REQUIRE(fn_engine); REQUIRE(fn_turbo);
    // Engine: init()=1 + run_loop()=2 = 3
    CHECK(fn_engine() == 3);
    // TurboEngine: init()=10 + run_loop()=20 = 30 (virtual dispatch for both)
    CHECK(fn_turbo()  == 30);
}

// ════════════════════════════════════════════════════════════════════════════
//  Additional: class default visibility (variables PROTECTED, functions PUBLIC)
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Class default visibility: variables PROTECTED, functions PUBLIC", "[class][virtuality][visibility]") {

    SECTION("Class variable accessed from outside (PROTECTED default) → error") {
        REQUIRE_THROWS(gen_jit_throws(R"SRC(
module __cls_defvis_prot__;
class C {
    x: int;
    C() : x(0) {}
}
test() : int {
    c: C;
    return c.x;
}
)SRC"));
    }

    SECTION("Class function public by default → callable from outside") {
        auto jit = gen_jit(R"SRC(
module __cls_defvis_pub_fn__;
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
        auto fn = jit->lookup_symbol<int(*)()>("test");
        REQUIRE(fn);
        CHECK(fn() == 42);
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  KNOWN BUGS — Virtual dispatch through diamond paths
//  Same SKIP() convention as test-gen-virtual-inheritance.cpp: remove SKIP
//  when the corresponding infrastructure bug is fixed.
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[BUG] Diamond class: override in D not reached via secondary base C&", "[gen][class][virtuality][diamond][bug]") {
    // ── Bug description ──────────────────────────────────────────────────────
    // When D overrides a method inherited from A (through B and C), dispatching
    // through a C& reference requires:
    //   1. A single shared __vbase_A__ in D (transitivity bug in compute_virtual_bases).
    //   2. A this-adjustment thunk in C's vtable slot for that method.
    //
    // Without (1), C still has its own __base_A__; its vtable slot points to
    // C's own implementation (or A's), not D's.
    // Without (2), even with a shared A the thunk is not generated.
    //
    // Expected (correct): call_via_c(d) == 42  (D::foo reached through C&)
    // Current (broken):   call_via_c(d) == 1   (C::foo, no thunk to D)
    // ─────────────────────────────────────────────────────────────────────────
    // Expected (correct): call_via_c(d) == 42  (D::foo reached through C&)

    auto jit = gen_jit(R"SRC(
module __bug_virt_diamond_secondary__;
class A { foo() : int { return 0; } }
class B : public A {}
class C : public A { foo() : int { return 1; } }
class D : public B, public C { foo() : int { return 42; } }

call_via_c(c: C&) : int { return c.foo(); }
test() : int { d: D; return call_via_c(d); }
)SRC", false, false);
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    // Must be 42 once fixed.
    CHECK(fn() == 42);
}

TEST_CASE("[BUG] Diamond class: two overrides in B and C — D wins via both refs", "[gen][class][virtuality][diamond][bug]") {
    // ── Bug description ──────────────────────────────────────────────────────
    // Both B and C override foo().  D then overrides it again.
    // Calling via B& on a D object must return D's value.
    // Calling via C& on a D object must return D's value.
    //
    // The B& path may already work (primary vtable).
    // The C& path requires the thunk mechanism described above.
    // ─────────────────────────────────────────────────────────────────────────
    // The B& path may already work (primary vtable).
    // The C& path requires the thunk mechanism described above.

    auto jit = gen_jit(R"SRC(
module __bug_virt_diamond_both_override__;
class A { foo() : int { return 0; } }
class B : public A { foo() : int { return 1; } }
class C : public A { foo() : int { return 2; } }
class D : public B, public C { foo() : int { return 99; } }

call_via_b(b: B&) : int { return b.foo(); }
call_via_c(c: C&) : int { return c.foo(); }
test_b() : int { d: D; return call_via_b(d); }
test_c() : int { d: D; return call_via_c(d); }
)SRC", false, false);
    REQUIRE(jit);

    {
        auto fn = jit->lookup_symbol<int(*)()>("test_b");
        REQUIRE(fn);
        CHECK(fn() == 99);   // B& → primary vtable → D::foo
    }
    {
        auto fn = jit->lookup_symbol<int(*)()>("test_c");
        REQUIRE(fn);
        CHECK(fn() == 99);   // C& → thunk → D::foo
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  EXPECTED BEHAVIOURS — Diamond class virtuality
//  These tests describe the full correct semantics.  Each is gated with SKIP
//  until the underlying bugs are resolved; remove SKIP one by one as fixes land.
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[EXPECTED] Diamond class: dispatch via primary B& reaches D override", "[gen][class][virtuality][diamond][expected]") {
    // Primary vtable path: B is the first base of D so D's vtable slot is
    // directly reachable through a B& reference.

    auto jit = gen_jit(R"SRC(
module __exp_virt_diamond_primary__;
class A { foo() : int { return 0; } }
class B : public A { foo() : int { return 1; } }
class C : public A {}
class D : public B, public C { foo() : int { return 42; } }

call_via_b(b: B&) : int { return b.foo(); }
test() : int { d: D; return call_via_b(d); }
)SRC", false, false);
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("[EXPECTED] Diamond class: dispatch via secondary C& reaches D override", "[gen][class][virtuality][diamond][expected]") {
    // The C& path requires a this-adjustment thunk in C's vtable.
    // After the fix, calling foo() via C& on a D object must return 42.

    auto jit = gen_jit(R"SRC(
module __exp_virt_diamond_secondary__;
class A { foo() : int { return 0; } }
class B : public A {}
class C : public A { foo() : int { return 1; } }
class D : public B, public C { foo() : int { return 42; } }

call_via_c(c: C&) : int { return c.foo(); }
test() : int { d: D; return call_via_c(d); }
)SRC", false, false);
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("[EXPECTED] Diamond class: dispatch via virtual base A& reaches D override", "[gen][class][virtuality][diamond][expected]") {
    // Dispatch directly through an A& reference.  Since A is the shared virtual
    // base, A's vtable slot must point to D::foo after D is constructed.

    auto jit = gen_jit(R"SRC(
module __exp_virt_diamond_via_a__;
class A { foo() : int { return 0; } }
class B : public A {}
class C : public A {}
class D : public B, public C { foo() : int { return 42; } }

call_via_a(a: A&) : int { return a.foo(); }
test() : int { d: D; return call_via_a(d); }
)SRC", false, false);
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("[EXPECTED] Diamond class: multi-level — E inherits from B and C, each inheriting A", "[gen][class][virtuality][diamond][expected]") {
    // Three-level diamond: A ← B ← E, A ← C ← E
    // E overrides foo(); dispatch via B&, C& and A& must all reach E::foo.

    auto jit = gen_jit(R"SRC(
module __exp_virt_multi_diamond__;
class A { foo() : int { return 0; } }
class B : public A { foo() : int { return 1; } }
class C : public A { foo() : int { return 2; } }
class E : public B, public C { foo() : int { return 99; } }

call_via_b(b: B&) : int { return b.foo(); }
call_via_c(c: C&) : int { return c.foo(); }
call_via_a(a: A&) : int { return a.foo(); }
test_b() : int { e: E; return call_via_b(e); }
test_c() : int { e: E; return call_via_c(e); }
test_a() : int { e: E; return call_via_a(e); }
)SRC", false, false);
    REQUIRE(jit);

    for (const char* sym : {"test_b", "test_c", "test_a"}) {
        auto fn = jit->lookup_symbol<int(*)()>(sym);
        REQUIRE(fn);
        CHECK(fn() == 99);   // E::foo must be reached from every base path
    }
}

TEST_CASE("[EXPECTED] Diamond class: interface + virtual base — dispatch via I& reaches D", "[gen][class][virtuality][diamond][expected]") {
    // An interface I is implemented by A; D inherits (via diamond B/C) and
    // overrides method().  Dispatch via I& on a D object must call D::method.

    auto jit = gen_jit(R"SRC(
module __exp_virt_diamond_interface__;
interface I { method() : int; }
class A : public I { method() : int { return 0; } }
class B : public A {}
class C : public A {}
class D : public B, public C { method() : int { return 7; } }

call_via_i(i: I&) : int { return i.method(); }
test() : int { d: D; return call_via_i(d); }
)SRC", false, false);
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 7);
}

TEST_CASE("[EXPECTED] Diamond class: final override in B is sealed — C and D cannot re-override", "[gen][class][virtuality][diamond][expected]") {
    // B declares foo() as 'final': it overrides A::foo and seals it.
    // C and D cannot further override it.

    auto jit = gen_jit(R"SRC(
module __exp_virt_diamond_final__;
class A { foo() : int { return 0; } }
class B : public A { final foo() : int { return 10; } }
class C : public A {}
class D : public B, public C {}   // cannot override foo() — B sealed it

call_via_b(b: B&) : int { return b.foo(); }
call_via_a(a: A&) : int { return a.foo(); }
test_b() : int { d: D; return call_via_b(d); }
test_a() : int { d: D; return call_via_a(d); }
)SRC", false, false);
    REQUIRE(jit);

    {
        auto fn = jit->lookup_symbol<int(*)()>("test_b");
        REQUIRE(fn);
        CHECK(fn() == 10);   // B::foo (final)
    }
    {
        auto fn = jit->lookup_symbol<int(*)()>("test_a");
        REQUIRE(fn);
        CHECK(fn() == 10);   // Sealed in B → 10
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  Additional: struct has no virtual dispatch even with multiple inheritance
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Struct with multiple inheritance — no virtual dispatch", "[struct][virtuality]") {

    auto jit = gen_jit(R"SRC(
module __struct_multi_no_virt__;
struct A {
    who() : int { return 1; }
}
struct B : public A {
    who() : int { return 2; }
}
struct C : public A {
    who() : int { return 3; }
}
struct D : public B, public C {
    // D has two 'who()' from B and C — no ambiguity since called directly
}
call_b(b: B&) : int { return b.who(); }
call_c(c: C&) : int { return c.who(); }
test_b() : int { d: D; return call_b(d); }
test_c() : int { d: D; return call_c(d); }
)SRC", false, false);
    REQUIRE(jit);

    auto fn_b = jit->lookup_symbol<int(*)()>("test_b");
    auto fn_c = jit->lookup_symbol<int(*)()>("test_c");
    REQUIRE(fn_b); REQUIRE(fn_c);
    // Struct: no dispatch; B::who=2, C::who=3
    CHECK(fn_b() == 2);
    CHECK(fn_c() == 3);
}

// ════════════════════════════════════════════════════════════════════════════
//  [V,W,X] 'final' is NOT allowed on constructors/destructors
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[V] final on a class constructor is an error", "[class][virtuality][final][error]") {

    REQUIRE_THROWS(gen_jit_throws(R"SRC(
module __cls_final_on_ctor__;
class Foo {
    final Foo() {}
}
)SRC"));
}

TEST_CASE("[W] final on a class destructor is an error", "[class][virtuality][final][error]") {

    REQUIRE_THROWS(gen_jit_throws(R"SRC(
module __cls_final_on_dtor__;
class Foo {
    Foo() {}
    final ~Foo() {}
}
)SRC"));
}

TEST_CASE("[X] final on a struct constructor is an error", "[struct][virtuality][final][error]") {

    REQUIRE_THROWS(gen_jit_throws(R"SRC(
module __struct_final_on_ctor__;
struct Foo {
    final Foo() {}
}
)SRC"));
}

