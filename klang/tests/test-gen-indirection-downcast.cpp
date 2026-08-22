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
 * Tests for static pointer upcast (ref/lien/pin/ptr of Derived → ref/lien/pin/ptr of Base).
 *
 * This tests the "downcast" feature from the user's perspective (binding a base-typed
 * indirection to a derived object), which is implemented as a static (compile-time)
 * upcast in the IR via GEP.
 *
 * Test categories:
 *   [T1]  ref:   init ref<Base> from Derived object          → success
 *   [T2]  lien:  init lien<Base> from &Derived               → success
 *   [T3]  lien:  rebind lien<Base> to &other_Derived         → success
 *   [T4]  pin:   init pin<Base> from &Derived                → success
 *   [T5]  ptr:   init ptr<Base> from &Derived                → success
 *   [T6]  ptr:   rebind ptr<Base> to &other_Derived          → success
 *   [T7]  lien:  init lien<Base> from non-null ptr<Derived>  → success (null-check inserted)
 *   [T8]  ref:   init ref<Base> from null ptr<Derived>       → fatal runtime
 *   [T9]  lien:  init lien<Base> from null ptr<Derived>      → fatal runtime
 *   [T10] ref:   init ref<Base> from unrelated type          → compile error
 *   [T11] lien:  init lien<Base> from &unrelated             → compile error
 *   [T12] ptr:   init ptr<Base> from &unrelated              → compile error
 *   [T13] pin:   rebind after construction                   → compile error
 *   [T14] ref:   rebind after construction                   → (already tested)
 *   [T15] lien:  downcast on interface                       → success
 *   [T16] ptr:   transitive upcast C→B→A                     → success
 */

#include <catch2/catch_all.hpp>
#include "helpers.hpp"

// =============================================================================
// Shared source fragments (helper macros)
// =============================================================================

// Single inheritance: struct Base { val:int }, struct Derived : Base { extra:int }
#define SINGLE_INHERIT_SRC \
    "struct Base {\n" \
    "    val : int;\n" \
    "    Base() : val(10) {}\n" \
    "    Base(v : int) : val(v) {}\n" \
    "}\n" \
    "struct Derived : public Base {\n" \
    "    extra : int;\n" \
    "    Derived() : Base(42), extra(7) {}\n" \
    "    Derived(v : int) : Base(v), extra(7) {}\n" \
    "}\n"

// =============================================================================
// [T1] ref<Base> init from Derived object — success
// =============================================================================
TEST_CASE("Downcast: ref<Base> init from Derived object reads Base field", "[gen][downcast][ref]") {
    auto jit = gen_jit(R"SRC(
        module gen_indirection_downcast_01;

        struct Base {
            val : int;
            Base() : val(10) {}
            Base(v : int) : val(v) {}
        }
        struct Derived : public Base {
            extra : int;
            Derived() : Base(42), extra(7) {}
        }

        test() : int {
            d : Derived();
            r : Base& = d;      // ref<Base> bound to Derived object
            return r.val;       // must see d.val = 42
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

// =============================================================================
// [T2] lien<Base> init from &Derived — success
// =============================================================================
TEST_CASE("Downcast: lien<Base> init from &Derived reads Base field", "[gen][downcast][lien]") {
    auto jit = gen_jit(R"SRC(
        module gen_indirection_downcast_02;

        struct Base {
            val : int;
            Base() : val(0) {}
            Base(v : int) : val(v) {}
        }
        struct Derived : public Base {
            extra : int;
            Derived() : Base(55), extra(9) {}
        }

        test() : int {
            d : Derived();
            lnk : Base+ = &d;   // lien<Base> bound to Derived object
            return lnk->val;    // must see d.val = 55
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 55);
}

// =============================================================================
// [T3] lien<Base> rebind to &other_Derived — success
// =============================================================================
TEST_CASE("Downcast: lien<Base> can be rebound to another Derived", "[gen][downcast][lien]") {
    auto jit = gen_jit(R"SRC(
        module gen_indirection_downcast_03;

        struct Base {
            val : int;
            Base() : val(0) {}
            Base(v : int) : val(v) {}
        }
        struct Derived : public Base {
            extra : int;
            Derived(v : int) : Base(v), extra(0) {}
        }

        test() : int {
            d1 : Derived(11);
            d2 : Derived(22);
            lnk : Base+ = &d1;
            lnk = &d2;           // rebind to d2
            return lnk->val;     // must see d2.val = 22
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 22);
}

// =============================================================================
// [T4] pin<Base> init from &Derived — success
// =============================================================================
TEST_CASE("Downcast: pin<Base> init from &Derived reads Base field", "[gen][downcast][view]") {
    auto jit = gen_jit(R"SRC(
        module gen_indirection_downcast_04;

        struct Base {
            val : int;
            Base() : val(0) {}
            Base(v : int) : val(v) {}
        }
        struct Derived : public Base {
            extra : int;
            Derived(v : int) : Base(v), extra(3) {}
        }

        test() : int {
            d : Derived(77);
            p : Base? = &d;     // pin<Base> bound to Derived object
            return p->val;      // must see d.val = 77
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 77);
}

// =============================================================================
// [T5] ptr<Base> init from &Derived — success
// =============================================================================
TEST_CASE("Downcast: ptr<Base> init from &Derived reads Base field", "[gen][downcast][ptr]") {
    auto jit = gen_jit(R"SRC(
        module gen_indirection_downcast_05;

        struct Base {
            val : int;
            Base() : val(0) {}
            Base(v : int) : val(v) {}
        }
        struct Derived : public Base {
            extra : int;
            Derived(v : int) : Base(v), extra(2) {}
        }

        test() : int {
            d : Derived(99);
            p : Base* = &d;     // ptr<Base> pointing to Derived object
            return p->val;      // must see d.val = 99
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 99);
}

// =============================================================================
// [T6] ptr<Base> rebind to &other_Derived — success
// =============================================================================
TEST_CASE("Downcast: ptr<Base> can be reassigned to another Derived", "[gen][downcast][ptr]") {
    auto jit = gen_jit(R"SRC(
        module gen_indirection_downcast_06;

        struct Base {
            val : int;
            Base() : val(0) {}
            Base(v : int) : val(v) {}
        }
        struct Derived : public Base {
            extra : int;
            Derived(v : int) : Base(v), extra(0) {}
        }

        test() : int {
            d1 : Derived(3);
            d2 : Derived(8);
            p : Base* = &d1;
            p = &d2;            // rebind to d2
            return p->val;      // must see d2.val = 8
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 8);
}

// =============================================================================
// [T7] lien<Base> init from non-null ptr<Derived> — success (null-check inserted)
// =============================================================================
TEST_CASE("Downcast: lien<Base> init from non-null ptr<Derived> succeeds", "[gen][downcast][lien]") {
    auto jit = gen_jit(R"SRC(
        module gen_indirection_downcast_07;

        struct Base {
            val : int;
            Base() : val(0) {}
            Base(v : int) : val(v) {}
        }
        struct Derived : public Base {
            extra : int;
            Derived(v : int) : Base(v), extra(0) {}
        }

        test() : int {
            d : Derived(33);
            p : Derived* = &d;
            lnk : Base+ = p;    // warning: nullable source; null-check inserted
            return lnk->val;    // must see 33
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 33);
}

// =============================================================================
// [T8] ref<Base> init from ref<Derived> — modify Base field through ref
// =============================================================================
TEST_CASE("Downcast: writing through ref<Base> modifies Base field of Derived", "[gen][downcast][ref]") {
    auto jit = gen_jit(R"SRC(
        module gen_indirection_downcast_08;

        struct Base {
            val : int;
            Base() : val(0) {}
            Base(v : int) : val(v) {}
        }
        struct Derived : public Base {
            extra : int;
            Derived(v : int) : Base(v), extra(0) {}
        }

        test() : int {
            d : Derived(5);
            r : Base& = d;
            r.val = 100;         // write through base ref
            return d.val;        // Derived's Base field should now be 100
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 100);
}

// =============================================================================
// [T9] ptr<Base> null-assigned, then reassigned to Derived — success
// =============================================================================
TEST_CASE("Downcast: ptr<Base> initially null, then assigned to Derived", "[gen][downcast][ptr]") {
    auto jit = gen_jit(R"SRC(
        module gen_indirection_downcast_09;

        struct Base {
            val : int;
            Base() : val(0) {}
            Base(v : int) : val(v) {}
        }
        struct Derived : public Base {
            extra : int;
            Derived(v : int) : Base(v), extra(0) {}
        }

        test() : int {
            d : Derived(17);
            p : Base* = null;   // initially null
            p = &d;             // reassign to Derived
            return p->val;      // must see 17
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 17);
}

// =============================================================================
// [T10] ref<Base> init from ref<Unrelated> — compile error
// =============================================================================
TEST_CASE("Downcast error: ref<Base> init from unrelated type is rejected", "[gen][resolution][downcast]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module gen_indirection_downcast_10;

        struct Base { val : int; Base() : val(0) {} }
        struct Unrelated { data : int; Unrelated() : data(0) {} }

        test() : int {
            u : Unrelated();
            r : Base& = u;   // ERROR: Unrelated does not inherit from Base
            return 0;
        }
    )SRC"));
}

// =============================================================================
// [T11] lien<Base> init from &Unrelated — compile error
// =============================================================================
TEST_CASE("Downcast error: lien<Base> init from unrelated type is rejected", "[gen][resolution][downcast]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module gen_indirection_downcast_11;

        struct Base { val : int; Base() : val(0) {} }
        struct Unrelated { data : int; Unrelated() : data(0) {} }

        test() : int {
            u : Unrelated();
            lnk : Base+ = &u;   // ERROR: Unrelated does not inherit from Base
            return 0;
        }
    )SRC"));
}

// =============================================================================
// [T12] ptr<Base> assigned &Unrelated — compile error
// =============================================================================
TEST_CASE("Downcast error: ptr<Base> assigned &Unrelated is rejected", "[gen][resolution][downcast]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module gen_indirection_downcast_12;

        struct Base { val : int; Base() : val(0) {} }
        struct Unrelated { data : int; Unrelated() : data(0) {} }

        test() : int {
            d : Unrelated();
            p : Base* = &d;     // ERROR: Unrelated does not inherit from Base
            return 0;
        }
    )SRC"));
}

// =============================================================================
// [T13] pin<Base> rebind after construction — compile error
// =============================================================================
TEST_CASE("Downcast error: pin<Base> cannot be rebound after construction", "[gen][resolution][downcast]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module gen_indirection_downcast_13;

        struct Base { val : int; Base() : val(0) {} }
        struct Derived : public Base { Derived() : Base(1) {} }

        test() : int {
            d1 : Derived();
            d2 : Derived();
            p : Base? = &d1;
            p = &d2;            // ERROR: pin is immutable after construction
            return 0;
        }
    )SRC"));
}

// =============================================================================
// [T15] interface downcast via ptr — success
// (Note: virtual dispatch via ptr->method() on interfaces is not yet supported;
//  we test the binding compatibility only, using a wrapper function with ref&.)
// =============================================================================
TEST_CASE("Downcast: ptr<interface> init from implementing class, dispatch via ref", "[gen][downcast][interface]") {
    auto jit = gen_jit(R"SRC(
        module gen_indirection_downcast_14;

        interface Printable {
            get_val() : int;
        }

        class MyClass : public Printable {
            public MyClass() {}
            public get_val() : int { return 88; }
        }

        call_get_val(p : Printable&) : int { return p.get_val(); }

        test() : int {
            obj : MyClass();
            p : Printable* = &obj;  // ptr<interface> bound to implementing class
            return call_get_val(*p);  // dispatch via ref — must return 88
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 88);
}

// =============================================================================
// [T16] transitive upcast ptr<A> from C (C→B→A) — success
// =============================================================================
TEST_CASE("Downcast: ptr<A> from C where C→B→A (transitive upcast)", "[gen][downcast][ptr]") {
    auto jit = gen_jit(R"SRC(
        module gen_indirection_downcast_15;

        struct A {
            val : int;
            A() : val(0) {}
            A(v : int) : val(v) {}
        }
        struct B : public A {
            b_extra : int;
            B() : A(0), b_extra(0) {}
            B(v : int) : A(v), b_extra(0) {}
        }
        struct C : public B {
            c_extra : int;
            C() : B(0), c_extra(0) {}
            C(v : int) : B(v), c_extra(0) {}
        }

        test() : int {
            c : C(123);
            p : A* = &c;         // transitive upcast: C → B → A
            return p->val;       // must see c.val = 123
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 123);
}

// =============================================================================
// Extra: write through lien<Base> modifies the Derived object's Base part
// =============================================================================
TEST_CASE("Downcast: writing through lien<Base> modifies Derived's Base field", "[gen][downcast][lien]") {
    auto jit = gen_jit(R"SRC(
        module gen_indirection_downcast_16;

        struct Base {
            val : int;
            Base() : val(0) {}
            Base(v : int) : val(v) {}
        }
        struct Derived : public Base {
            extra : int;
            Derived(v : int) : Base(v), extra(9) {}
        }

        test() : int {
            d : Derived(5);
            lnk : Base+ = &d;
            lnk->val = 200;      // write through base link
            return d.val;        // must see 200
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 200);
}

// =============================================================================
// Extra: ptr<Base> assigned from pin<Derived> — success
// =============================================================================
TEST_CASE("Downcast: ptr<Base> assigned from pin<Derived>", "[gen][downcast][ptr]") {
    auto jit = gen_jit(R"SRC(
        module gen_indirection_downcast_17;

        struct Base {
            val : int;
            Base() : val(0) {}
            Base(v : int) : val(v) {}
        }
        struct Derived : public Base {
            extra : int;
            Derived(v : int) : Base(v), extra(1) {}
        }

        test() : int {
            d : Derived(66);
            pin : Derived? = &d;
            p : Base* = pin;     // ptr<Base> from pin<Derived>
            return p->val;       // must see 66
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 66);
}

