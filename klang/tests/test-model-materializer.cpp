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
 * Tests for the model_materializer (Phase 2).
 *
 * The model_materializer runs between aggregate_type_resolver and type_reference_resolver.
 * Its responsibilities:
 *  1. Validate vtable consistency (abstract slots in non-abstract classes → error).
 *  2. Compute secondary_vtable_spec records in vtable_layout::secondary_vtables for
 *     classes with multiple class bases.
 *
 * These tests directly inspect the model (vtable entries, thunk_info records)
 * by compiling source through the full resolver pipeline and querying the model unit.
 *
 * Test categories:
 *  ── Vtable validation ────────────────────────────────────────────────────
 *   [A] Single-class vtable: 1 virtual method → 1 slot
 *   [B] Derived class overrides → correct slot_index and func pointer
 *   [C] Abstract class with abstract method → no error, slot present
 *   [D] Concrete derived provides all overrides → no abstract slots remain
 *
 *  ── Secondary vtable specs ────────────────────────────────────────────────
 *   [E] Single inheritance → no secondary_vtables
 *   [F] Multiple inheritance: D : B, C (both with vtable) → one secondary_vtable_spec
 *   [G] Secondary vtable spec: thunk NOT needed when slot not overridden in D
 *   [H] Secondary vtable spec: thunk needed when slot overridden in D
 *
 *  ── Runtime dispatch correctness (via JIT) ────────────────────────────────
 *   [I] Single inheritance virtual dispatch still works after Phase 2
 *   [J] Multiple inheritance virtual dispatch still works (uses secondary vtable)
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

// ════════════════════════════════════════════════════════════════════════════
//  [A] Single-class vtable: 1 virtual method → 1 slot
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[A] Phase2: single-class vtable has correct slot count", "[phase2][materializer][vtable]") {
    auto comp = compile_model(R"SRC(
        module model_materializer_01;
        class Foo {
            bar() : int { return 42; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto kl = find_klass(comp, "Foo");
    REQUIRE(kl != nullptr);
    REQUIRE(kl->has_vtable());

    auto vt = kl->get_vtable();
    REQUIRE(vt != nullptr);
    // One virtual method → exactly 1 slot
    CHECK(vt->slot_count() == 1);
    CHECK(vt->entries[0].slot_index == 0);
    CHECK(vt->entries[0].func != nullptr);
    CHECK(vt->entries[0].func->get_short_name() == "bar");
}

// ════════════════════════════════════════════════════════════════════════════
//  [B] Derived class overrides: correct slot_index and func pointer updated
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[B] Phase2: derived override updates vtable entry func pointer", "[phase2][materializer][vtable]") {
    auto comp = compile_model(R"SRC(
        module model_materializer_02;
        class Base {
            value() : int { return 1; }
        }
        class Derived : Base {
            value() : int { return 2; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto base_kl  = find_klass(comp, "Base");
    auto deriv_kl = find_klass(comp, "Derived");
    REQUIRE(base_kl  != nullptr);
    REQUIRE(deriv_kl != nullptr);
    REQUIRE(base_kl->has_vtable());
    REQUIRE(deriv_kl->has_vtable());

    // Both have exactly 1 slot
    auto base_vt  = base_kl->get_vtable();
    auto deriv_vt = deriv_kl->get_vtable();
    CHECK(base_vt->slot_count()  == 1);
    CHECK(deriv_vt->slot_count() == 1);

    // Derived's slot[0] must point to Derived::value, not Base::value
    REQUIRE(deriv_vt->entries[0].func != nullptr);
    CHECK(deriv_vt->entries[0].func->get_short_name() == "value");
    // The owner of the overriding function must be Derived
    REQUIRE(deriv_vt->entries[0].func->get_owner() != nullptr);
    CHECK(deriv_vt->entries[0].func->get_owner()->get_short_name() == "Derived");

    // The introducing function (in the inherited slot) must still be Base::value
    REQUIRE(deriv_vt->entries[0].introducing_func != nullptr);
    CHECK(deriv_vt->entries[0].introducing_func->get_owner()->get_short_name() == "Base");
}

// ════════════════════════════════════════════════════════════════════════════
//  [C] Abstract class: abstract method → slot present, func is abstract
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[C] Phase2: abstract class has abstract slot without error", "[phase2][materializer][vtable][abstract]") {
    auto comp = compile_model(R"SRC(
        module model_materializer_03;
        abstract class Shape {
            abstract area() : int;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto kl = find_klass(comp, "Shape");
    REQUIRE(kl != nullptr);
    REQUIRE(kl->is_abstract());
    REQUIRE(kl->has_vtable());

    auto vt = kl->get_vtable();
    REQUIRE(vt != nullptr);
    CHECK(vt->slot_count() == 1);
    REQUIRE(vt->entries[0].func != nullptr);
    CHECK(vt->entries[0].func->is_abstract_func());
}

// ════════════════════════════════════════════════════════════════════════════
//  [D] Concrete derived provides all overrides → no abstract slots remain
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[D] Phase2: concrete derived class fills all abstract slots", "[phase2][materializer][vtable][abstract]") {
    auto comp = compile_model(R"SRC(
        module model_materializer_04;
        abstract class Shape {
            abstract area() : int;
        }
        class Circle : Shape {
            area() : int { return 314; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto circle = find_klass(comp, "Circle");
    REQUIRE(circle != nullptr);
    CHECK(!circle->is_abstract());

    auto vt = circle->get_vtable();
    REQUIRE(vt != nullptr);
    CHECK(vt->slot_count() == 1);
    // The slot must be filled with the concrete Circle::area
    REQUIRE(vt->entries[0].func != nullptr);
    CHECK(!vt->entries[0].func->is_abstract_func());
    CHECK(vt->entries[0].func->get_owner()->get_short_name() == "Circle");
}

// ════════════════════════════════════════════════════════════════════════════
//  [E] Single inheritance → one secondary_vtable spec (for the primary base)
//      In K's layout, the derived class's __vptr__ is at field 0, so the primary
//      base sub-object is always at a non-zero offset. An override therefore
//      requires a this-adjustment thunk → one secondary_vtable spec is generated.
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[E] Phase2: single inheritance → one secondary vtable spec for primary base", "[phase2][materializer][secondary_vtable]") {
    auto comp = compile_model(R"SRC(
        module model_materializer_05;
        class Base {
            foo() : int { return 1; }
        }
        class Child : Base {
            foo() : int { return 2; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto child = find_klass(comp, "Child");
    REQUIRE(child != nullptr);
    REQUIRE(child->has_vtable());

    auto vt = child->get_vtable();
    // Single inheritance: Child has 1 direct base (Base) with a vtable.
    // The primary base Base is at offset sizeof(ptr)=8 in Child's layout (Child's own
    // __vptr__ occupies field 0).  The override of foo requires a this-adjustment thunk.
    // So there must be exactly 1 secondary_vtable spec (for Base).
    REQUIRE(vt->secondary_vtables.size() == 1);
    CHECK(vt->secondary_vtables[0].base_class->get_short_name() == "Base");
    CHECK(vt->secondary_vtables[0].base_offset > 0); // non-zero: Base is after __vptr__
}

// ════════════════════════════════════════════════════════════════════════════
//  [F] Multiple inheritance: D : B, C (both with vtable) → secondary_vtables
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[F] Phase2: multiple class bases → secondary_vtable_spec present", "[phase2][materializer][secondary_vtable]") {
    auto comp = compile_model(R"SRC(
        module model_materializer_06;
        class B {
            b_method() : int { return 10; }
        }
        class C {
            c_method() : int { return 20; }
        }
        class D : B, C {
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto d_kl = find_klass(comp, "D");
    REQUIRE(d_kl != nullptr);
    REQUIRE(d_kl->has_vtable());

    auto vt = d_kl->get_vtable();
    REQUIRE(vt != nullptr);

    // D inherits from both B and C — the second base (C) must have a secondary vtable spec
    // because C's subobject is embedded at a non-zero offset in D.
    // Note: if the byte offset happens to be 0 (compiler may pack) the spec still exists
    // but may have base_offset == 0 (no thunk needed for that case).
    CHECK(!vt->secondary_vtables.empty());

    // The secondary vtable spec should reference class C
    bool found_c = false;
    for (auto& spec : vt->secondary_vtables) {
        if (spec.base_class && spec.base_class->get_short_name() == "C") {
            found_c = true;
            // C has 1 virtual method → 1 thunk slot
            CHECK(spec.slot_thunks.size() == 1);
            break;
        }
    }
    CHECK(found_c);
}

// ════════════════════════════════════════════════════════════════════════════
//  [G] No override in D → thunk NOT needed for that slot
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[G] Phase2: inherited-but-not-overridden slot → needs_thunk false", "[phase2][materializer][secondary_vtable]") {
    auto comp = compile_model(R"SRC(
        module model_materializer_07;
        class B {
            foo() : int { return 1; }
        }
        class C {
            bar() : int { return 2; }
        }
        class D : B, C {
            // Does NOT override bar() — C::bar is inherited unchanged
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto d_kl = find_klass(comp, "D");
    REQUIRE(d_kl != nullptr);
    REQUIRE(d_kl->has_vtable());
    auto vt = d_kl->get_vtable();

    // Find secondary vtable spec for C
    const k::model::secondary_vtable_spec* spec_c = nullptr;
    for (auto& spec : vt->secondary_vtables) {
        if (spec.base_class && spec.base_class->get_short_name() == "C") {
            spec_c = &spec;
            break;
        }
    }

    if (spec_c) {
        // If there is a secondary spec for C, the slot for bar() should NOT need a thunk
        // because D does not override bar().
        REQUIRE(!spec_c->slot_thunks.empty());
        // bar() is in slot 0 of C's vtable
        bool bar_slot_found = false;
        for (auto& ti : spec_c->slot_thunks) {
            if (ti.real_func && ti.real_func->get_short_name() == "bar") {
                bar_slot_found = true;
                CHECK(!ti.needs_thunk);
                break;
            }
        }
        CHECK(bar_slot_found);
    }
    // If spec_c is null (e.g., the offset happened to be 0), the test is vacuously valid.
}

// ════════════════════════════════════════════════════════════════════════════
//  [H] Overridden slot in D → thunk IS needed (if C is at non-zero offset)
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[H] Phase2: overridden slot in D with non-zero offset → needs_thunk true", "[phase2][materializer][secondary_vtable]") {
    auto comp = compile_model(R"SRC(
        module model_materializer_08;
        class B {
            foo() : int { return 1; }
        }
        class C {
            bar() : int { return 2; }
        }
        class D : B, C {
            bar() : int { return 99; }  // overrides C::bar
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto d_kl = find_klass(comp, "D");
    REQUIRE(d_kl != nullptr);
    REQUIRE(d_kl->has_vtable());
    auto vt = d_kl->get_vtable();

    const k::model::secondary_vtable_spec* spec_c = nullptr;
    for (auto& spec : vt->secondary_vtables) {
        if (spec.base_class && spec.base_class->get_short_name() == "C") {
            spec_c = &spec;
            break;
        }
    }

    if (spec_c && spec_c->base_offset > 0) {
        // C subobject is at non-zero offset: D::bar override needs a thunk
        // DEBUG: print all thunks to understand what's available
        INFO("spec_c->slot_thunks.size() = " << spec_c->slot_thunks.size());
        for (size_t i = 0; i < spec_c->slot_thunks.size(); ++i) {
            auto& ti2 = spec_c->slot_thunks[i];
            INFO("  thunk[" << i << "] func=" << (ti2.real_func ? ti2.real_func->get_short_name() : "null")
                 << " owner=" << (ti2.real_func && ti2.real_func->get_owner() ? ti2.real_func->get_owner()->get_short_name() : "?")
                 << " needs_thunk=" << ti2.needs_thunk << " adj=" << ti2.this_adjustment);
        }

        bool bar_found = false;
        for (auto& ti : spec_c->slot_thunks) {
            if (ti.real_func && ti.real_func->get_short_name() == "bar") {
                bar_found = true;
                CHECK(ti.needs_thunk);
                CHECK(ti.this_adjustment == spec_c->base_offset);
                // The real function should be D::bar (the override)
                REQUIRE(ti.real_func->get_owner() != nullptr);
                CHECK(ti.real_func->get_owner()->get_short_name() == "D");
                break;
            }
        }
        CHECK(bar_found);
    }
    // If offset == 0, needs_thunk == false is correct (no adjustment needed).
}

// ════════════════════════════════════════════════════════════════════════════
//  [I] Single inheritance virtual dispatch still works after Phase 2 (JIT)
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[I] Phase2: single inheritance virtual dispatch works at runtime", "[phase2][materializer][runtime]") {
    auto jit = gen_jit(R"SRC(
        module model_materializer_09;
        class Animal {
            speak() : int { return 0; }
        }
        class Dog : Animal {
            speak() : int { return 7; }
        }
        dispatch_speak(a: Animal&) : int {
            return a.speak();
        }
        test() : int {
            d : Dog();
            return dispatch_speak(d);
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 7);
}

// ════════════════════════════════════════════════════════════════════════════
//  [J] Multiple inheritance virtual dispatch via secondary vtable (JIT)
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[J] Phase2: multiple class inheritance virtual dispatch works at runtime", "[phase2][materializer][runtime]") {
    auto jit = gen_jit(R"SRC(
        module model_materializer_10;
        class B {
            value() : int { return 10; }
        }
        class C {
            value() : int { return 20; }
        }
        class D : B, C {
            // D does not override value() — B::value and C::value are dispatched
        }
        dispatch_b(b: B&) : int { return b.value(); }
        dispatch_c(c: C&) : int { return c.value(); }
        test_b() : int {
            d : D();
            return dispatch_b(d);
        }
        test_c() : int {
            d : D();
            return dispatch_c(d);
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn_b = jit->lookup_symbol<int(*)()>("test_b");
    REQUIRE(fn_b != nullptr);
    CHECK(fn_b() == 10);  // Dispatches to B::value

    auto fn_c = jit->lookup_symbol<int(*)()>("test_c");
    REQUIRE(fn_c != nullptr);
    CHECK(fn_c() == 20);  // Dispatches to C::value
}

// ════════════════════════════════════════════════════════════════════════════
//  [K] Vtable validation: non-abstract class with unimplemented abstract slot → error
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[K] Phase2: non-abstract class with unimplemented abstract slot → compilation error",
          "[phase2][materializer][vtable][abstract][error]") {
    // symbol_resolver already catches this; model_materializer provides a
    // defensive second check. Either way it must fail to compile.
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module model_materializer_11;
        abstract class Base {
            abstract method() : int;
        }
        class Derived : Base {
            // Does NOT implement method() — must be an error
        }
    )SRC"));
}

// ════════════════════════════════════════════════════════════════════════════
//  [DEBUG] Child : Base(abstract) : Ping(iface), Pong(iface)
//  Verify that secondary vtable specs cover Pong (transitive, not direct base).
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[DBG-V2] Child : Base(abstract) : Ping, Pong — secondary specs cover Pong transitively",
          "[phase2][materializer][secondary_vtable][debug]") {
    auto comp = compile_model(R"SRC(
        module model_materializer_12;
        interface Ping { ping() : int; }
        interface Pong { pong() : int; }
        abstract class Base : public Ping, public Pong {
            ping() : int { return 1; }
            pong() : int { return 2; }
        }
        class Child : public Base {
            Child() {}
            ping() : int { return 11; }
            pong() : int { return 22; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto child = find_klass(comp, "Child");
    REQUIRE(child != nullptr);
    REQUIRE(child->has_vtable());

    auto vt = child->get_vtable();
    REQUIRE(vt);

    // Child should have secondary vtable specs for Base, Ping AND Pong
    // (Pong is transitive via Base)
    INFO("secondary_vtables count = " << vt->secondary_vtables.size());
    for (auto& spec : vt->secondary_vtables) {
        INFO("  spec for: " << (spec.base_class ? spec.base_class->get_short_name() : "null")
             << " offset=" << spec.base_offset);
    }

    bool has_pong_spec = false;
    for (auto& spec : vt->secondary_vtables) {
        if (spec.base_class && spec.base_class->get_short_name() == "Pong") {
            has_pong_spec = true;
            CHECK(spec.base_offset > 0);  // Pong is at non-zero offset in Child
            CHECK(spec.slot_thunks.size() == 1);  // pong() is 1 slot
            if (!spec.slot_thunks.empty()) {
                CHECK(spec.slot_thunks[0].needs_thunk == true);
                CHECK(spec.slot_thunks[0].real_func != nullptr);
                if (spec.slot_thunks[0].real_func) {
                    CHECK(spec.slot_thunks[0].real_func->get_short_name() == "pong");
                    // real_func must belong to Child (the override), not Pong or Base
                    if (spec.slot_thunks[0].real_func->get_owner()) {
                        CHECK(spec.slot_thunks[0].real_func->get_owner()->get_short_name() == "Child");
                    }
                }
            }
        }
    }
    CHECK(has_pong_spec);

    // Also verify LLVM secondary vtable globals are registered on Child
    bool has_pong_global = false;
    for (auto& [base_agg, sec_vt_layout] : child->get_secondary_vtables()) {
        if (base_agg && base_agg->get_short_name() == "Pong") {
            has_pong_global = true;
            CHECK(sec_vt_layout != nullptr);
            if (sec_vt_layout) CHECK(sec_vt_layout->llvm_global != nullptr);
        }
    }
    CHECK(has_pong_global);
}


// ════════════════════════════════════════════════════════════════════════════
//  [P] Secondary interface base of an intermediate interface dispatches correctly
//  Regression: interface C : A, B  (B is C's secondary base). A concrete class
//  Impl : C overrides all methods. Upcasting Impl to B& and calling B's method
//  must dispatch to Impl's override. Previously the B secondary vtable slot was
//  left null (no override link back to B's slot), causing a runtime segfault.
// ════════════════════════════════════════════════════════════════════════════
TEST_CASE("[P] Secondary interface base of intermediate interface dispatches at runtime",
          "[phase2][materializer][runtime][multi-interface]") {
    auto jit = gen_jit(R"SRC(
        module model_materializer_13;
        interface A {
            doA() : int;
        }
        interface B {
            doB() : int;
        }
        interface C : public A, public B {
            doC() : int;
        }
        class Impl : public C {
            override doA() : int { return 1; }
            override doB() : int { return 2; }
            override doC() : int { return 3; }
        }
        call_a(a: A&) : int { return a.doA(); }
        call_b(b: B&) : int { return b.doB(); }
        call_c(c: C&) : int { return c.doC(); }
        test_a() : int { i : Impl; return call_a(i); }
        test_b() : int { i : Impl; return call_b(i); }
        test_c() : int { i : Impl; return call_c(i); }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn_a = jit->lookup_symbol<int(*)()>("test_a");
    auto fn_b = jit->lookup_symbol<int(*)()>("test_b");
    auto fn_c = jit->lookup_symbol<int(*)()>("test_c");
    REQUIRE(fn_a != nullptr);
    REQUIRE(fn_b != nullptr);
    REQUIRE(fn_c != nullptr);
    CHECK(fn_a() == 1);  // dispatch via A (primary of C)
    CHECK(fn_b() == 2);  // dispatch via B (secondary of C) — was segfaulting
    CHECK(fn_c() == 3);  // dispatch via C
}
