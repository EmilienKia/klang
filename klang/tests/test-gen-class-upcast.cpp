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
 * Tests for static pointer upcast (ref/lien/pin/ptr of Derived class/interface
 * → ref/lien/pin/ptr of Base class/interface).
 *
 * Unlike the "downcast" tests (which use structs), these tests use classes and
 * interfaces — types that have vtables and RTTI.
 *
 * The feature is implemented as a static (compile-time) GEP-based address
 * adjustment, identical to the struct upcast, but applicable to klass and
 * interface types.
 *
 * Test categories:
 *   [T1]  ref:   init ref<Base> from Derived class object           → success
 *   [T2]  lien:  init lien<Base> from &Derived class               → success
 *   [T3]  lien:  rebind lien<Base> to &other Derived               → success
 *   [T4]  pin:   init pin<Base> from &Derived class                → success
 *   [T5]  ptr:   init ptr<Base> from &Derived class                → success
 *   [T6]  ptr:   rebind ptr<Base> to &other Derived                → success
 *   [T7]  lien:  init lien<Base> from non-null ptr<Derived>        → success
 *   [T8]  ref:   init ref<Base> from Derived object, virtual call  → success
 *   [T9]  ptr:   ptr<interface> init from implementing class       → success
 *   [T10] ptr:   transitive upcast class C→B→A                    → success
 *   [T11] ptr:   ptr<abstract base> from Derived                   → success
 *   [T12] ref:   init ref<Base class> from unrelated class         → compile error
 *   [T13] lien:  init lien<Base class> from &unrelated class       → compile error
 *   [T14] pin:   rebind after construction                         → compile error
 *   [T15] ref:   virtual call via ref<Base> bound to Derived       → correct dispatch
 *   [T16] lien:  virtual call via lien<interface> bound to class   → correct dispatch
 */

#include <catch2/catch_all.hpp>
#include "helpers.hpp"

// =============================================================================
// [T1] ref<Base class> init from Derived class object — success
// =============================================================================
TEST_CASE("Class upcast: ref<Base> init from Derived class object reads Base field", "[gen][upcast][class][ref]") {
    auto jit = gen_jit(R"SRC(
        module __cu_ref_init__;

        class Base {
            public val : int;
            Base() : val(0) {}
            Base(v : int) : val(v) {}
        }
        class Derived : public Base {
            public extra : int;
            Derived() : Base(42), extra(7) {}
        }

        test() : int {
            d : Derived();
            r : Base& = d;      // ref<Base> bound to Derived class object
            return r.val;       // must see d.val = 42
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

// =============================================================================
// [T2] lien<Base class> init from &Derived — success
// =============================================================================
TEST_CASE("Class upcast: lien<Base> init from &Derived class reads Base field", "[gen][upcast][class][lien]") {
    auto jit = gen_jit(R"SRC(
        module __cu_lien_init__;

        class Base {
            public val : int;
            Base() : val(0) {}
            Base(v : int) : val(v) {}
        }
        class Derived : public Base {
            public extra : int;
            Derived() : Base(55), extra(9) {}
        }

        test() : int {
            d : Derived();
            lnk : Base+ = &d;   // lien<Base class> bound to Derived object
            return lnk->val;    // must see d.val = 55
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 55);
}

// =============================================================================
// [T3] lien<Base class> rebind to &other Derived — success
// =============================================================================
TEST_CASE("Class upcast: lien<Base> can be rebound to another Derived class", "[gen][upcast][class][lien]") {
    auto jit = gen_jit(R"SRC(
        module __cu_lien_rebind__;

        class Base {
            public val : int;
            Base() : val(0) {}
            Base(v : int) : val(v) {}
        }
        class Derived : public Base {
            public extra : int;
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
// [T4] pin<Base class> init from &Derived — success
// =============================================================================
TEST_CASE("Class upcast: pin<Base> init from &Derived class reads Base field", "[gen][upcast][class][view]") {
    auto jit = gen_jit(R"SRC(
        module __cu_pin_init__;

        class Base {
            public val : int;
            Base() : val(0) {}
            Base(v : int) : val(v) {}
        }
        class Derived : public Base {
            public extra : int;
            Derived(v : int) : Base(v), extra(3) {}
        }

        test() : int {
            d : Derived(77);
            p : Base? = &d;     // pin<Base class> bound to Derived object
            return p->val;      // must see d.val = 77
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 77);
}

// =============================================================================
// [T5] ptr<Base class> init from &Derived — success
// =============================================================================
TEST_CASE("Class upcast: ptr<Base> init from &Derived class reads Base field", "[gen][upcast][class][ptr]") {
    auto jit = gen_jit(R"SRC(
        module __cu_ptr_init__;

        class Base {
            public val : int;
            Base() : val(0) {}
            Base(v : int) : val(v) {}
        }
        class Derived : public Base {
            public extra : int;
            Derived(v : int) : Base(v), extra(2) {}
        }

        test() : int {
            d : Derived(88);
            p : Base* = &d;     // ptr<Base class> bound to Derived object
            return p->val;      // must see d.val = 88
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 88);
}

// =============================================================================
// [T6] ptr<Base class> rebind to &other Derived — success
// =============================================================================
TEST_CASE("Class upcast: ptr<Base> can be reassigned to another Derived class", "[gen][upcast][class][ptr]") {
    auto jit = gen_jit(R"SRC(
        module __cu_ptr_rebind__;

        class Base {
            public val : int;
            Base() : val(0) {}
            Base(v : int) : val(v) {}
        }
        class Derived : public Base {
            public extra : int;
            Derived(v : int) : Base(v), extra(0) {}
        }

        test() : int {
            d1 : Derived(11);
            d2 : Derived(99);
            p : Base* = &d1;
            p = &d2;            // rebind to d2
            return p->val;      // must see d2.val = 99
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 99);
}

// =============================================================================
// [T7] lien<Base class> init from non-null ptr<Derived> — success
// =============================================================================
TEST_CASE("Class upcast: lien<Base> init from non-null ptr<Derived class> succeeds", "[gen][upcast][class][lien]") {
    auto jit = gen_jit(R"SRC(
        module __cu_lien_from_ptr__;

        class Base {
            public val : int;
            Base() : val(0) {}
            Base(v : int) : val(v) {}
        }
        class Derived : public Base {
            public extra : int;
            Derived(v : int) : Base(v), extra(1) {}
        }

        test() : int {
            d : Derived(33);
            p : Derived* = &d;
            lnk : Base+ = p;    // lien<Base class> init from non-null ptr<Derived>
            return lnk->val;    // must see 33
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 33);
}

// =============================================================================
// [T8] virtual call via ref<Base> bound to Derived class — correct dispatch
// =============================================================================
TEST_CASE("Class upcast: virtual call via ref<Base> bound to Derived dispatches correctly", "[gen][upcast][class][ref][virtual]") {
    auto jit = gen_jit(R"SRC(
        module __cu_ref_virtual__;

        class Base {
            public val : int;
            Base() : val(0) {}
            Base(v : int) : val(v) {}
            get() : int { return val; }
        }
        class Derived : public Base {
            public extra : int;
            Derived() : Base(0), extra(0) {}
            Derived(v : int) : Base(v), extra(0) {}
            get() : int { return this.val * 2; }
        }

        call_get(b : Base&) : int { return b.get(); }

        test() : int {
            d : Derived(5);
            r : Base& = d;      // ref<Base class> bound to Derived object
            return call_get(r); // virtual dispatch: must call Derived::get() → 5*2=10
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 10);
}

// =============================================================================
// [T9] ptr<interface> init from implementing class — success
// =============================================================================
TEST_CASE("Class upcast: ptr<interface> init from implementing class, virtual dispatch", "[gen][upcast][class][interface]") {
    auto jit = gen_jit(R"SRC(
        module __cu_interface_ptr__;

        interface Countable {
            count() : int;
        }

        class MyCounter : public Countable {
            public n : int;
            public MyCounter(v : int) : n(v) {}
            public count() : int { return n; }
        }

        call_count(c : Countable&) : int { return c.count(); }

        test() : int {
            obj : MyCounter(7);
            p : Countable* = &obj;  // ptr<interface> bound to implementing class
            return call_count(*p);  // dispatch via ref — must return 7
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 7);
}

// =============================================================================
// [T10] transitive upcast class C→B→A via ptr — success
// =============================================================================
TEST_CASE("Class upcast: ptr<A> from C class where C→B→A (transitive upcast)", "[gen][upcast][class][ptr]") {
    auto jit = gen_jit(R"SRC(
        module __cu_transitive_ptr__;

        class A {
            public val : int;
            A() : val(0) {}
            A(v : int) : val(v) {}
        }
        class B : public A {
            public b_extra : int;
            B() : A(0), b_extra(0) {}
            B(v : int) : A(v), b_extra(0) {}
        }
        class C : public B {
            public c_extra : int;
            C() : B(0), c_extra(0) {}
            C(v : int) : B(v), c_extra(0) {}
        }

        test() : int {
            c : C(123);
            p : A* = &c;         // transitive upcast: C class → B → A
            return p->val;       // must see c.val = 123
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 123);
}

// =============================================================================
// [T11] ptr<abstract Base class> from Derived — success
// An abstract class has a RTTI global even if no vtable global is emitted.
// A ptr<AbstractBase> from a Derived object must work.
// =============================================================================
TEST_CASE("Class upcast: ptr<abstract Base> from Derived class succeeds", "[gen][upcast][class][abstract][ptr]") {
    auto jit = gen_jit(R"SRC(
        module __cu_abstract_ptr__;

        abstract class Shape {
            public abstract area() : int;
        }

        class Square : public Shape {
            public side : int;
            Square(s : int) : side(s) {}
            area() : int { return side * side; }
        }

        call_area(s : Shape&) : int { return s.area(); }

        test() : int {
            sq : Square(6);
            p : Shape* = &sq;        // ptr<abstract base> from Derived
            return call_area(*p);    // virtual dispatch → 6*6 = 36
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 36);
}

// =============================================================================
// [T12] ref<Base class> init from unrelated class — compile error
// =============================================================================
TEST_CASE("Class upcast error: ref<Base class> init from unrelated type is rejected", "[gen][upcast][class][error]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module __cu_ref_unrelated__;

        class A {
            public x : int;
            A() : x(0) {}
        }
        class B {
            public y : int;
            B() : y(0) {}
        }

        test() : int {
            b : B();
            r : A& = b;   // ERROR: B is not derived from A
            return 0;
        }
    )SRC"));
}

// =============================================================================
// [T13] lien<Base class> init from &unrelated class — compile error
// =============================================================================
TEST_CASE("Class upcast error: lien<Base class> init from &unrelated class is rejected", "[gen][upcast][class][error]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module __cu_lien_unrelated__;

        class A {
            public x : int;
            A() : x(0) {}
        }
        class B {
            public y : int;
            B() : y(0) {}
        }

        test() : int {
            b : B();
            lnk : A+ = &b;   // ERROR: B is not derived from A
            return 0;
        }
    )SRC"));
}

// =============================================================================
// [T14] pin<Base class> cannot be rebound after construction — compile error
// =============================================================================
TEST_CASE("Class upcast error: pin<Base class> cannot be rebound after construction", "[gen][upcast][class][error]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module __cu_pin_rebind__;

        class Base {
            public val : int;
            Base() : val(0) {}
        }
        class Derived : public Base {
            public extra : int;
            Derived() : Base(1), extra(2) {}
        }

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
// [T15] virtual call via ref<Base class> dispatches to most-derived override
// =============================================================================
TEST_CASE("Class upcast: writing through lien<Base class> modifies Derived's Base field", "[gen][upcast][class][lien][write]") {
    auto jit = gen_jit(R"SRC(
        module __cu_lien_write__;

        class Base {
            public val : int;
            Base() : val(0) {}
            Base(v : int) : val(v) {}
        }
        class Derived : public Base {
            public extra : int;
            Derived(v : int) : Base(v), extra(9) {}
        }

        test() : int {
            d : Derived(5);
            lnk : Base+ = &d;
            lnk->val = 200;      // write through base class link
            return d.val;        // must see 200
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 200);
}

// =============================================================================
// [T16] virtual call via lien<interface> bound to class — correct dispatch
// =============================================================================
TEST_CASE("Class upcast: virtual call via lien<interface> bound to class dispatches correctly", "[gen][upcast][interface][lien][virtual]") {
    auto jit = gen_jit(R"SRC(
        module __cu_interface_lien__;

        interface Describable {
            describe() : int;
        }

        class Widget : public Describable {
            public id : int;
            public Widget(v : int) : id(v) {}
            public describe() : int { return id + 100; }
        }

        call_describe(d : Describable&) : int { return d.describe(); }

        test() : int {
            w : Widget(5);
            lnk : Describable+ = &w;   // lien<interface> bound to class
            return call_describe(*lnk); // dispatch: Widget::describe() → 5+100 = 105
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 105);
}

// =============================================================================
// [T17] ptr<Base class> assigned from pin<Derived class> — success
// =============================================================================
TEST_CASE("Class upcast: ptr<Base class> assigned from pin<Derived class>", "[gen][upcast][class][ptr]") {
    auto jit = gen_jit(R"SRC(
        module __cu_ptr_from_pin__;

        class Base {
            public val : int;
            Base() : val(0) {}
            Base(v : int) : val(v) {}
        }
        class Derived : public Base {
            public extra : int;
            Derived(v : int) : Base(v), extra(1) {}
        }

        test() : int {
            d : Derived(66);
            pin : Derived? = &d;
            p : Base* = pin;     // ptr<Base class> from pin<Derived class>
            return p->val;       // must see 66
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 66);
}


// =============================================================================
// Owner (T!) sources — regression: the indirection-upcast path used to ignore
// owner and drain sources, so `b : Base* = d;` (with `d : Derived!`) stored the
// address of the owner slot instead of the owned pointer.  Any subsequent
// member access or virtual dispatch through `b` then corrupted memory.
// =============================================================================

TEST_CASE("Class upcast: ptr<Base class> initialised from owner<Derived class>", "[gen][upcast][class][ptr][owner]") {
    auto jit = gen_jit(R"SRC(
        module __cu_ptr_from_owner__;

        class Base {
            public val : int;
            Base(v : int) : val(v) {}
        }
        class Derived : public Base {
            public extra : int;
            Derived(v : int) : Base(v), extra(1) {}
        }

        test() : int {
            d : Derived! = new Derived(77);
            b : Base* = d;       // ptr<Base> from owner<Derived>
            return b->val;       // must see 77
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 77);
}

TEST_CASE("Class upcast: virtual call through ptr<interface> initialised from owner<class>", "[gen][upcast][interface][ptr][owner][virtual]") {
    auto jit = gen_jit(R"SRC(
        module __cu_iface_from_owner__;

        interface Task {
            run() : void;
        }
        class Job : public Task {
            public outcome : int;
            Job() : outcome(0) {}
            override run() : void { outcome = 42; }
        }

        test() : int {
            j : Job! = new Job();
            t : Task* = j;       // ptr<interface> from owner<class>
            t->run();            // virtual dispatch through the upcast pointer
            return j->outcome;   // must see 42
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

TEST_CASE("Class upcast: lien<Base class> initialised from owner<Derived class>", "[gen][upcast][class][lien][owner]") {
    auto jit = gen_jit(R"SRC(
        module __cu_lien_from_owner__;

        class Base {
            public val : int;
            Base(v : int) : val(v) {}
        }
        class Derived : public Base {
            Derived(v : int) : Base(v) {}
        }

        test() : int {
            d : Derived! = new Derived(88);
            b : Base+ = d;       // lien<Base> from owner<Derived>
            return b->val;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 88);
}
