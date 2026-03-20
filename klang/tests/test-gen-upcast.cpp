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
 * Tests for dynamic upcast (RTTI-based): binding a derived-typed indirection
 * from a base-typed indirection (pointer, link, pinned, reference) via
 * runtime type checking.
 *
 * "Dynamic upcast" here means: Base* → Derived* where Derived derives from Base.
 * Only applicable to class and interface types (not structs or primitives).
 *
 * Notes:
 *   - K does not support ptr==null comparisons nor ->method() calls as callees.
 *   - Success cases use gen_jit() with wrapper functions accepting Derived&.
 *   - Null dereference / fatal trap cases use build_and_exec() (native binary);
 *     they are verified by a non-zero exit code (signal / debugtrap).
 *   - Source class must have at least one public virtual method (polymorphic).
 *
 * Test categories:
 *   [U1]  ptr:   ptr<Derived> from ptr<Base> to actual Derived → success
 *   [U2]  ptr:   ptr<Derived> from ptr<Base> to wrong type → null → crash
 *   [U3]  lnk:   lnk<Derived> from lnk<Base> to actual Derived → success
 *   [U4]  lnk:   lnk<Derived> from ptr<Base> wrong type → fatal trap
 *   [U5]  pin:   pin<Derived> from ptr<Base> to actual Derived → success
 *   [U6]  pin:   pin<Derived> from ptr<Base> to wrong type → null → crash
 *   [U7]  ref:   ref<Derived> from ref<Base> to actual Derived → success
 *   [U8]  lnk:   rebind lnk<Derived> from ptr<Base> → success
 *   [U9]  ptr:   rebind ptr<Derived> from ptr<Base> to wrong → null → crash
 *   [U10] ptr:   ptr<Derived> from ptr<interface> → success
 *   [U11] ptr:   transitive C→B→A, ptr<C> from ptr<A> → success
 *   [U12] lnk:   lnk<Dog> from lnk<Animal> → success
 *   [E1]  struct: dynamic upcast not applicable to structs → compile error
 *   [E2]  ptr:   unrelated class → compile error
 */

#include <catch2/catch_all.hpp>
#include "helpers.hpp"

// =============================================================================
// [U1] ptr<Derived> from ptr<Base> to actual Derived — success
// =============================================================================
TEST_CASE("Dynamic upcast: ptr<Derived> from ptr<Base> to actual Derived is non-null", "[gen][dyncast][ptr]") {
    auto jit = gen_jit(R"SRC(

        module __du_ptr_ok__;

        class Base {
            public val : int;
            public Base() : val(0) {}
            public Base(v : int) : val(v) {}
            public dummy() : int { return 0; }
        }
        class Derived : public Base {
            public extra : int;
            public Derived(v : int) : Base(v), extra(99) {}
            public get_extra() : int { return extra; }
        }

        get_extra_fn(d : Derived&) : int { return d.get_extra(); }

        test() : int {
            d : Derived(42);
            bp : Base* = &d;
            dp : Derived* = bp;
            return get_extra_fn(*dp);
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 99);
}

// =============================================================================
// [U2] ptr<Derived> from ptr<Base> to wrong type → null → crash on deref
// =============================================================================
TEST_CASE("Dynamic upcast: ptr<Derived> from ptr<Base> wrong type → null → crash on use", "[gen][dyncast][ptr][null]") {
    // Null pointer dereference raises a signal; use build_and_exec and verify crash.
    auto res = build_and_exec(R"SRC(
        module __du_ptr_null__;

        class Base {
            public val : int;
            public Base() : val(0) {}
            public Base(v : int) : val(v) {}
            public dummy() : int { return 0; }
        }
        class DerivedA : public Base {
            public extra : int;
            public DerivedA(v : int) : Base(v), extra(1) {}
            public get_extra() : int { return extra; }
        }
        class DerivedB : public Base {
            public other : int;
            public DerivedB(v : int) : Base(v), other(2) {}
        }

        get_extra_fn(d : DerivedA&) : int { return d.get_extra(); }

        main() : int {
            b_obj : DerivedB(10);
            bp : Base* = &b_obj;
            ap : DerivedA* = bp;       // RTTI: DerivedB != DerivedA → null
            return get_extra_fn(*ap);  // null deref → crash
        }
    )SRC");
    REQUIRE(res.exit_code != 0);
}

// =============================================================================
// [U3] lnk<Derived> from lnk<Base> to actual Derived — success
// =============================================================================
TEST_CASE("Dynamic upcast: lnk<Derived> from lnk<Base> to actual Derived succeeds", "[gen][dyncast][lnk]") {
    auto jit = gen_jit(R"SRC(
        module __du_lnk_ok__;

        class Base {
            public val : int;
            public Base() : val(0) {}
            public Base(v : int) : val(v) {}
            public dummy() : int { return 0; }
        }
        class Derived : public Base {
            public extra : int;
            public Derived(v : int) : Base(v), extra(77) {}
            public get_extra() : int { return extra; }
        }

        get_extra_fn(d : Derived&) : int { return d.get_extra(); }

        test() : int {
            d : Derived(5);
            blnk : Base~ = &d;
            dlnk : Derived~ = blnk;
            return get_extra_fn(*dlnk);
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 77);
}

// =============================================================================
// [U4] lnk<Derived> from ptr<Base> wrong type → fatal trap (lnk non-null)
// =============================================================================
TEST_CASE("Dynamic upcast: lnk<Derived> from ptr<Base> wrong type triggers fatal trap", "[gen][dyncast][lnk][fatal]") {
    auto res = build_and_exec(R"SRC(
        module __du_lnk_fatal__;

        class Base {
            public val : int;
            public Base() : val(0) {}
            public Base(v : int) : val(v) {}
            public dummy() : int { return 0; }
        }
        class Derived : public Base {
            public extra : int;
            public Derived(v : int) : Base(v), extra(0) {}
        }
        class Other : public Base {
            public data : int;
            public Other(v : int) : Base(v), data(0) {}
        }

        main() : int {
            o : Other(1);
            bp : Base* = &o;
            dlnk : Derived~ = bp;    // RTTI fail → null → debugtrap
            return 0;
        }
    )SRC");
    REQUIRE(res.exit_code != 0);
}

// =============================================================================
// [U5] pin<Derived> from ptr<Base> to actual Derived — success
// =============================================================================
TEST_CASE("Dynamic upcast: pin<Derived> from ptr<Base> to actual Derived is non-null", "[gen][dyncast][pin]") {
    auto jit = gen_jit(R"SRC(
        module __du_pin_ok__;

        class Base {
            public val : int;
            public Base() : val(0) {}
            public Base(v : int) : val(v) {}
            public dummy() : int { return 0; }
        }
        class Derived : public Base {
            public extra : int;
            public Derived(v : int) : Base(v), extra(55) {}
            public get_extra() : int { return extra; }
        }

        get_extra_fn(d : Derived&) : int { return d.get_extra(); }

        test() : int {
            d : Derived(3);
            bp : Base* = &d;
            dp : Derived^ = bp;
            return get_extra_fn(*dp);
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 55);
}

// =============================================================================
// [U6] pin<Derived> from ptr<Base> wrong type → null → crash on deref
// =============================================================================
TEST_CASE("Dynamic upcast: pin<Derived> from ptr<Base> wrong type → null → crash", "[gen][dyncast][pin][null]") {
    auto res = build_and_exec(R"SRC(
        module __du_pin_null__;

        class Base {
            public val : int;
            public Base() : val(0) {}
            public Base(v : int) : val(v) {}
            public dummy() : int { return 0; }
        }
        class Derived : public Base {
            public extra : int;
            public Derived(v : int) : Base(v), extra(0) {}
            public get_extra() : int { return extra; }
        }
        class Other : public Base {
            public data : int;
            public Other(v : int) : Base(v), data(0) {}
        }

        get_extra_fn(d : Derived&) : int { return d.get_extra(); }

        main() : int {
            o : Other(7);
            bp : Base* = &o;
            dp : Derived^ = bp;         // RTTI fail → null
            return get_extra_fn(*dp);   // null deref → crash
        }
    )SRC");
    REQUIRE(res.exit_code != 0);
}

// =============================================================================
// [U7] ref<Derived> from ref<Base> to actual Derived — success
// =============================================================================
TEST_CASE("Dynamic upcast: ref<Derived> from ref<Base> of actual Derived succeeds", "[gen][dyncast][ref]") {
    auto jit = gen_jit(R"SRC(
        module __du_ref_ok__;

        class Base {
            public val : int;
            public Base() : val(0) {}
            public Base(v : int) : val(v) {}
            public dummy() : int { return 0; }
        }
        class Derived : public Base {
            public extra : int;
            public Derived(v : int) : Base(v), extra(33) {}
            public get_extra() : int { return extra; }
        }

        get_extra_fn(d : Derived&) : int { return d.get_extra(); }

        test() : int {
            d : Derived(7);
            br : Base& = d;
            dr : Derived& = br;
            return get_extra_fn(dr);
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 33);
}

// =============================================================================
// [U8] lnk<Derived> rebind from ptr<Base> to actual Derived — success
// =============================================================================
TEST_CASE("Dynamic upcast: lnk<Derived> rebind from ptr<Base> of actual Derived succeeds", "[gen][dyncast][lnk][rebind]") {
    auto jit = gen_jit(R"SRC(
        module __du_lnk_rebind__;

        class Base {
            public val : int;
            public Base() : val(0) {}
            public Base(v : int) : val(v) {}
            public dummy() : int { return 0; }
        }
        class Derived : public Base {
            public extra : int;
            public Derived(v : int) : Base(v), extra(88) {}
            public get_extra() : int { return extra; }
        }

        get_extra_fn(d : Derived&) : int { return d.get_extra(); }

        test() : int {
            d1 : Derived(1);
            d2 : Derived(2);
            lnk : Derived~ = &d1;
            bp : Base* = &d2;
            lnk = bp;
            return get_extra_fn(*lnk);
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 88);
}

// =============================================================================
// [U9] ptr<Derived> rebind from ptr<Base> wrong type → null → crash
// =============================================================================
TEST_CASE("Dynamic upcast: ptr<Derived> rebind from ptr<Base> wrong type → null → crash", "[gen][dyncast][ptr][rebind][null]") {
    auto res = build_and_exec(R"SRC(
        module __du_ptr_rebind_null__;

        class Base {
            public val : int;
            public Base() : val(0) {}
            public Base(v : int) : val(v) {}
            public dummy() : int { return 0; }
        }
        class Derived : public Base {
            public extra : int;
            public Derived(v : int) : Base(v), extra(0) {}
            public get_extra() : int { return extra; }
        }
        class Other : public Base {
            public data : int;
            public Other(v : int) : Base(v), data(0) {}
        }

        get_extra_fn(d : Derived&) : int { return d.get_extra(); }

        main() : int {
            d : Derived(1);
            o : Other(2);
            dp : Derived* = &d;
            bp : Base* = &o;
            dp = bp;                   // Other != Derived → null
            return get_extra_fn(*dp);  // null deref → crash
        }
    )SRC");
    REQUIRE(res.exit_code != 0);
}

// =============================================================================
// [U10] ptr<Derived> from ptr<interface> — success
// =============================================================================
TEST_CASE("Dynamic upcast: ptr<Derived> from ptr<interface> of actual Derived succeeds", "[gen][dyncast][interface]") {
    auto jit = gen_jit(R"SRC(
        module __du_interface__;

        interface IBase {
            get_val() : int;
        }

        class Derived : public IBase {
            public val : int;
            public Derived(v : int) : val(v) {}
            public get_val() : int { return val; }
            public get_extra() : int { return val * 2; }
        }

        get_extra_fn(d : Derived&) : int { return d.get_extra(); }

        test() : int {
            d : Derived(21);
            ip : IBase* = &d;
            dp : Derived* = ip;
            return get_extra_fn(*dp);
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

// =============================================================================
// [U11] Transitive C→B→A: ptr<C> from ptr<A> — success
// =============================================================================
TEST_CASE("Dynamic upcast: ptr<C> from ptr<A> transitive hierarchy (C→B→A) succeeds", "[gen][dyncast][transitive]") {
    auto jit = gen_jit(R"SRC(
        module __du_transitive__;

        class A {
            public x : int;
            public A() : x(0) {}
            public A(v : int) : x(v) {}
            public dummy() : int { return 0; }
        }
        class B : public A {
            public y : int;
            public B() : A(0), y(0) {}
            public B(v : int) : A(v), y(0) {}
        }
        class C : public B {
            public z : int;
            public C(v : int) : B(v), z(99) {}
            public get_z() : int { return z; }
        }

        get_z_fn(c : C&) : int { return c.get_z(); }

        test() : int {
            c : C(5);
            ap : A* = &c;
            cp : C* = ap;
            return get_z_fn(*cp);
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 99);
}

// =============================================================================
// [U12] lnk<Dog> from lnk<Animal> to actual Dog — success
// =============================================================================
TEST_CASE("Dynamic upcast: lnk<Dog> from lnk<Animal> to actual Dog succeeds", "[gen][dyncast][lnk2]") {
    auto jit = gen_jit(R"SRC(
        module __du_lnk_ok2__;

        class Animal {
            public name_code : int;
            public Animal() : name_code(0) {}
            public Animal(v : int) : name_code(v) {}
            public speak() : int { return name_code; }
        }
        class Dog : public Animal {
            public tricks : int;
            public Dog() : Animal(0), tricks(0) {}
            public Dog(v : int) : Animal(v), tricks(v * 2) {}
            public speak() : int { return tricks; }
            public get_tricks() : int { return tricks; }
        }

        tricks_fn(d : Dog&) : int { return d.get_tricks(); }

        test() : int {
            d : Dog(7);
            al : Animal~ = &d;
            dl : Dog~ = al;
            return tricks_fn(*dl);   // must return 14 (7*2)
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 14);
}

// =============================================================================
// [E1] Dynamic upcast not applicable to structs → compile error
// =============================================================================
TEST_CASE("Dynamic upcast error: cannot dynamic-cast struct ptr (no RTTI)", "[gen][dyncast][error][struct]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module __du_err_struct__;

        struct Base { val : int; Base() : val(0) {} }
        struct Derived : public Base { extra : int; Derived() : Base(), extra(0) {} }

        test() : int {
            b : Base();
            bp : Base* = &b;
            dp : Derived* = bp;   // ERROR: struct types have no RTTI
            return 0;
        }
    )SRC"));
}

// =============================================================================
// [E2] Dynamic upcast of unrelated classes → compile error
// =============================================================================
TEST_CASE("Dynamic upcast error: ptr<Unrelated> from ptr<Base> is rejected at compile time", "[gen][dyncast][error][unrelated]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module __du_err_unrelated__;

        class Base {
            public val : int;
            public Base() : val(0) {}
        }
        class Unrelated {
            public data : int;
            public Unrelated() : data(0) {}
        }

        test() : int {
            b : Base();
            bp : Base* = &b;
            up : Unrelated* = bp;   // ERROR: no inheritance relationship
            return 0;
        }
    )SRC"));
}

