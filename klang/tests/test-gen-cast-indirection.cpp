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
 * Tests for explicit and implicit casting of indirection types
 * (ref &, link ~, pin ^, ptr *) with struct, class and interface types.
 *
 * Covers both static (compile-time, GEP) and dynamic (RTTI) casts.
 *
 * ── Explicit cast (operator `(Type)expr`) ────────────────────────────────────
 *
 * Static upcast (Derived→Base):
 *   [EC1]  (Base*)  ptr<Derived>    → ptr<Base>  success
 *   [EC2]  (Base~)  lnk<Derived>    → lnk<Base>  success
 *   [EC3]  (Base^)  pin<Derived>    → pin<Base>  success
 *   [EC4]  (Base&)  ref<Derived>    → ref<Base>  success
 *   [EC5]  (Base*)  lnk<Derived>    → ptr<Base>  success
 *   [EC6]  (Base*)  pin<Derived>    → ptr<Base>  success
 *   [EC7]  (Base*)  ref<Derived>    → ptr<Base>  — via ref<ptr<Derived>>=ref; success via address-of
 *
 * Dynamic downcast (Base→Derived, RTTI for class/interface):
 *   [ED1]  (Derived*)  ptr<Base> actual Derived  → success (non-null)
 *   [ED2]  (Derived*)  ptr<Base> wrong type      → null  → crash on deref
 *   [ED3]  (Derived~)  lnk<Base> actual Derived  → success (non-null)
 *   [ED4]  (Derived~)  ptr<Base> wrong type      → fatal trap (lnk is non-null)
 *   [ED5]  (Derived^)  ptr<Base> actual Derived  → success
 *   [ED6]  (Derived^)  ptr<Base> wrong type      → null → crash
 *   [ED7]  (Derived&)  ref<Base> actual Derived  → success (fatal if wrong)
 *   [ED8]  (Derived*)  ptr<interface> actual     → success
 *
 * Error cases:
 *   [EE1]  explicit cast ptr<Base>→ptr<Unrelated>           → compile error
 *   [EE2]  explicit cast lnk<Base>→lnk<Unrelated>           → compile error
 *   [EE3]  (Derived*) ptr<Base>  where Derived is a struct  → compile error
 *
 * ── Implicit cast (function call arguments / upcast) ─────────────────────────
 *
 * Passing an indirection of derived type where base type is expected:
 *   [IC1]  f(p : Base*)    called with ptr<Derived>   → upcast implicit
 *   [IC2]  f(l : Base~)    called with lnk<Derived>   → upcast implicit
 *   [IC3]  f(p : Base^)    called with pin<Derived>   → upcast implicit
 *   [IC4]  f(r : Base&)    called with ref<Derived>   → upcast implicit (via ref)
 *   [IC5]  f(p : Base*)    called with ref<ptr<Derived>> — load+upcast
 *   [IC6]  f(l : Base~)    called with ref<lnk<Derived>> — load+upcast
 */

#include <catch2/catch_all.hpp>
#include "helpers.hpp"

// =============================================================================
// [EC1] Explicit (Base*) ptr<Derived> — static upcast
// =============================================================================
TEST_CASE("Explicit cast: (Base*) ptr<Derived> is static upcast", "[gen][cast][explicit][ptr]") {
    auto jit = gen_jit(R"SRC(
        module __ec_ptr_upcast__;

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
            d : Derived(55);
            pd : Derived* = &d;
            pb : Base* = (Base*) pd;   // explicit static upcast
            return pb->val;            // must see 55
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 55);
}

// =============================================================================
// [EC2] Explicit (Base~) lnk<Derived> — static upcast
// =============================================================================
TEST_CASE("Explicit cast: (Base~) lnk<Derived> is static upcast", "[gen][cast][explicit][lnk]") {
    auto jit = gen_jit(R"SRC(
        module __ec_lnk_upcast__;

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
            d : Derived(77);
            ld : Derived~ = &d;
            lb : Base~ = (Base~) ld;   // explicit static upcast
            return lb->val;            // must see 77
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 77);
}

// =============================================================================
// [EC3] Explicit (Base^) pin<Derived> — static upcast
// =============================================================================
TEST_CASE("Explicit cast: (Base^) pin<Derived> is static upcast", "[gen][cast][explicit][pin]") {
    auto jit = gen_jit(R"SRC(
        module __ec_pin_upcast__;

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
            d : Derived(33);
            pd : Derived^ = &d;
            pb : Base^ = (Base^) pd;   // explicit static upcast
            return pb->val;            // must see 33
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 33);
}

// =============================================================================
// [EC4] Explicit (Base&) ref<Derived> — static upcast
// =============================================================================
TEST_CASE("Explicit cast: (Base&) ref<Derived> is static upcast", "[gen][cast][explicit][ref]") {
    auto jit = gen_jit(R"SRC(
        module __ec_ref_upcast__;

        struct Base {
            val : int;
            Base() : val(0) {}
            Base(v : int) : val(v) {}
        }
        struct Derived : public Base {
            extra : int;
            Derived(v : int) : Base(v), extra(3) {}
        }

        get_val(r : Base&) : int { return r.val; }

        test() : int {
            d : Derived(88);
            dr : Derived& = d;
            return get_val((Base&) dr);   // explicit static upcast ref
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 88);
}

// =============================================================================
// [EC5] Explicit (Base*) lnk<Derived> — cross-kind upcast (lnk→ptr)
// =============================================================================
TEST_CASE("Explicit cast: (Base*) lnk<Derived> cross-kind upcast", "[gen][cast][explicit][lnk][ptr]") {
    auto jit = gen_jit(R"SRC(
        module __ec_lnk_to_ptr_upcast__;

        struct Base {
            val : int;
            Base() : val(0) {}
            Base(v : int) : val(v) {}
        }
        struct Derived : public Base {
            extra : int;
            Derived(v : int) : Base(v), extra(4) {}
        }

        test() : int {
            d : Derived(44);
            ld : Derived~ = &d;
            pb : Base* = (Base*) ld;   // lnk<Derived>→ptr<Base>
            return pb->val;            // must see 44
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 44);
}

// =============================================================================
// [EC6] Explicit (Base*) pin<Derived> — cross-kind upcast (pin→ptr)
// =============================================================================
TEST_CASE("Explicit cast: (Base*) pin<Derived> cross-kind upcast", "[gen][cast][explicit][pin][ptr]") {
    auto jit = gen_jit(R"SRC(
        module __ec_pin_to_ptr_upcast__;

        struct Base {
            val : int;
            Base() : val(0) {}
            Base(v : int) : val(v) {}
        }
        struct Derived : public Base {
            extra : int;
            Derived(v : int) : Base(v), extra(5) {}
        }

        test() : int {
            d : Derived(11);
            pd : Derived^ = &d;
            pb : Base* = (Base*) pd;   // pin<Derived>→ptr<Base>
            return pb->val;            // must see 11
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 11);
}

// =============================================================================
// [ED1] Explicit (Derived*) ptr<Base> — dynamic downcast, correct type → non-null
// =============================================================================
TEST_CASE("Explicit cast: (Derived*) ptr<Base> dynamic downcast — correct type succeeds", "[gen][cast][explicit][dyncast][ptr]") {
    auto jit = gen_jit(R"SRC(
        module __ed_ptr_ok__;

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
            dp : Derived* = (Derived*) bp;   // explicit dynamic downcast
            return get_extra_fn(*dp);        // must return 99
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 99);
}

// =============================================================================
// [ED2] Explicit (Derived*) ptr<Base> — wrong type → null → crash on deref
// =============================================================================
TEST_CASE("Explicit cast: (Derived*) ptr<Base> wrong type → null → crash", "[gen][cast][explicit][dyncast][ptr][null]") {
    auto res = build_and_exec(R"SRC(
        module __ed_ptr_null__;

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
            b : DerivedB(10);
            bp : Base* = &b;
            ap : DerivedA* = (DerivedA*) bp;   // wrong type → null
            return get_extra_fn(*ap);           // null deref → crash
        }
    )SRC");
    REQUIRE(res.exit_code != 0);
}

// =============================================================================
// [ED3] Explicit (Derived~) lnk<Base> — dynamic downcast, correct type → success
// =============================================================================
TEST_CASE("Explicit cast: (Derived~) lnk<Base> dynamic downcast correct type succeeds", "[gen][cast][explicit][dyncast][lnk]") {
    auto jit = gen_jit(R"SRC(
        module __ed_lnk_ok__;

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
            bl : Base~ = &d;
            dl : Derived~ = (Derived~) bl;    // explicit dynamic downcast lnk
            return get_extra_fn(*dl);          // must return 77
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 77);
}

// =============================================================================
// [ED4] Explicit (Derived~) ptr<Base> — wrong type → fatal trap (lnk non-null)
// =============================================================================
TEST_CASE("Explicit cast: (Derived~) ptr<Base> wrong type → fatal trap", "[gen][cast][explicit][dyncast][lnk][fatal]") {
    auto res = build_and_exec(R"SRC(
        module __ed_lnk_fatal__;

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
            dl : Derived~ = (Derived~) bp;   // RTTI fail → null → debugtrap (lnk non-null)
            return 0;
        }
    )SRC");
    REQUIRE(res.exit_code != 0);
}

// =============================================================================
// [ED5] Explicit (Derived^) ptr<Base> — correct type → success
// =============================================================================
TEST_CASE("Explicit cast: (Derived^) ptr<Base> dynamic downcast correct type succeeds", "[gen][cast][explicit][dyncast][pin]") {
    auto jit = gen_jit(R"SRC(
        module __ed_pin_ok__;

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
            dp : Derived^ = (Derived^) bp;    // explicit dynamic downcast pin
            return get_extra_fn(*dp);          // must return 55
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 55);
}

// =============================================================================
// [ED6] Explicit (Derived^) ptr<Base> — wrong type → null → crash
// =============================================================================
TEST_CASE("Explicit cast: (Derived^) ptr<Base> wrong type → null → crash", "[gen][cast][explicit][dyncast][pin][null]") {
    auto res = build_and_exec(R"SRC(
        module __ed_pin_null__;

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
            dp : Derived^ = (Derived^) bp;         // RTTI fail → null
            return get_extra_fn(*dp);               // null deref → crash
        }
    )SRC");
    REQUIRE(res.exit_code != 0);
}

// =============================================================================
// [ED7] Explicit (Derived&) ref<Base> — correct type → success
// =============================================================================
TEST_CASE("Explicit cast: (Derived&) ref<Base> dynamic downcast correct type succeeds", "[gen][cast][explicit][dyncast][ref]") {
    auto jit = gen_jit(R"SRC(
        module __ed_ref_ok__;

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
            dr : Derived& = (Derived&) br;   // explicit dynamic downcast ref
            return get_extra_fn(dr);         // must return 33
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 33);
}

// =============================================================================
// [ED8] Explicit (Derived*) ptr<interface> — dynamic downcast via interface
// =============================================================================
TEST_CASE("Explicit cast: (Derived*) ptr<interface> dynamic downcast via interface", "[gen][cast][explicit][dyncast][interface]") {
    auto jit = gen_jit(R"SRC(
        module __ed_iface__;

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
            dp : Derived* = (Derived*) ip;   // explicit dynamic downcast via interface
            return get_extra_fn(*dp);        // must return 42
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

// =============================================================================
// [EE1] Error: explicit cast ptr<Base>→ptr<Unrelated> — compile error
// =============================================================================
TEST_CASE("Explicit cast error: ptr<Base>→ptr<Unrelated> is rejected", "[gen][cast][explicit][error]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module __ee_ptr_unrelated__;

        struct Base { val : int; Base() : val(0) {} }
        struct Unrelated { data : int; Unrelated() : data(0) {} }

        test() : int {
            b : Base();
            bp : Base* = &b;
            up : Unrelated* = (Unrelated*) bp;   // ERROR: no inheritance
            return 0;
        }
    )SRC"));
}

// =============================================================================
// [EE2] Error: explicit cast lnk<Base>→lnk<Unrelated> — compile error
// =============================================================================
TEST_CASE("Explicit cast error: lnk<Base>→lnk<Unrelated> is rejected", "[gen][cast][explicit][error]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module __ee_lnk_unrelated__;

        struct Base { val : int; Base() : val(0) {} }
        struct Unrelated { data : int; Unrelated() : data(0) {} }

        test() : int {
            b : Base();
            bl : Base~ = &b;
            ul : Unrelated~ = (Unrelated~) bl;   // ERROR: no inheritance
            return 0;
        }
    )SRC"));
}

// =============================================================================
// [EE3] Error: explicit (Derived*) ptr<Base> where Derived is a struct — compile error
// =============================================================================
TEST_CASE("Explicit cast error: dynamic downcast to struct (no RTTI) is rejected", "[gen][cast][explicit][error][struct]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module __ee_struct_downcast__;

        struct Base { val : int; Base() : val(0) {} }
        struct Derived : public Base { extra : int; Derived() : Base(), extra(0) {} }

        test() : int {
            b : Base();
            bp : Base* = &b;
            dp : Derived* = (Derived*) bp;   // ERROR: struct has no RTTI
            return 0;
        }
    )SRC"));
}

// =============================================================================
// [IC1] Implicit upcast — f(p : Base*) called with ptr<Derived>
// =============================================================================
TEST_CASE("Implicit upcast: f(Base*) called with ptr<Derived> — succeeds", "[gen][cast][implicit][ptr]") {
    auto jit = gen_jit(R"SRC(
        module __ic_ptr__;

        struct Base {
            val : int;
            Base() : val(0) {}
            Base(v : int) : val(v) {}
        }
        struct Derived : public Base {
            extra : int;
            Derived(v : int) : Base(v), extra(0) {}
        }

        get_val(p : Base*) : int { return p->val; }

        test() : int {
            d : Derived(66);
            pd : Derived* = &d;
            return get_val(pd);   // implicit upcast ptr<Derived>→ptr<Base>
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 66);
}

// =============================================================================
// [IC2] Implicit upcast — f(l : Base~) called with lnk<Derived>
// =============================================================================
TEST_CASE("Implicit upcast: f(Base~) called with lnk<Derived> — succeeds", "[gen][cast][implicit][lnk]") {
    auto jit = gen_jit(R"SRC(
        module __ic_lnk__;

        struct Base {
            val : int;
            Base() : val(0) {}
            Base(v : int) : val(v) {}
        }
        struct Derived : public Base {
            extra : int;
            Derived(v : int) : Base(v), extra(1) {}
        }

        get_val(l : Base~) : int { return l->val; }

        test() : int {
            d : Derived(88);
            ld : Derived~ = &d;
            return get_val(ld);   // implicit upcast lnk<Derived>→lnk<Base>
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 88);
}

// =============================================================================
// [IC3] Implicit upcast — f(p : Base^) called with pin<Derived>
// =============================================================================
TEST_CASE("Implicit upcast: f(Base^) called with pin<Derived> — succeeds", "[gen][cast][implicit][pin]") {
    auto jit = gen_jit(R"SRC(
        module __ic_pin__;

        struct Base {
            val : int;
            Base() : val(0) {}
            Base(v : int) : val(v) {}
        }
        struct Derived : public Base {
            extra : int;
            Derived(v : int) : Base(v), extra(2) {}
        }

        get_val(p : Base^) : int { return p->val; }

        test() : int {
            d : Derived(13);
            pd : Derived^ = &d;
            return get_val(pd);   // implicit upcast pin<Derived>→pin<Base>
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 13);
}

// =============================================================================
// [IC4] Implicit upcast — f(r : Base&) called with ref<Derived> (via obj)
// =============================================================================
TEST_CASE("Implicit upcast: f(Base&) called with Derived object (ref implicit) — succeeds", "[gen][cast][implicit][ref]") {
    auto jit = gen_jit(R"SRC(
        module __ic_ref__;

        struct Base {
            val : int;
            Base() : val(0) {}
            Base(v : int) : val(v) {}
        }
        struct Derived : public Base {
            extra : int;
            Derived(v : int) : Base(v), extra(3) {}
        }

        get_val(r : Base&) : int { return r.val; }

        test() : int {
            d : Derived(27);
            return get_val(d);   // implicit upcast: Derived → ref<Base>
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 27);
}

// =============================================================================
// [IC5] Implicit upcast — f(p : Base*) called with ref<ptr<Derived>> (load+upcast)
// =============================================================================
TEST_CASE("Implicit upcast: f(Base*) called with ref<ptr<Derived>> — load+upcast", "[gen][cast][implicit][ref][ptr]") {
    auto jit = gen_jit(R"SRC(
        module __ic_ref_ptr__;

        struct Base {
            val : int;
            Base() : val(0) {}
            Base(v : int) : val(v) {}
        }
        struct Derived : public Base {
            extra : int;
            Derived(v : int) : Base(v), extra(4) {}
        }

        get_val(p : Base*) : int { return p->val; }

        test() : int {
            d : Derived(50);
            pd : Derived* = &d;
            return get_val(pd);   // ref<ptr<Derived>> loaded and upcast to ptr<Base>
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 50);
}

// =============================================================================
// [IC6] Implicit upcast — f(l : Base~) called with ref<lnk<Derived>> (load+upcast)
// =============================================================================
TEST_CASE("Implicit upcast: f(Base~) called with ref<lnk<Derived>> — load+upcast", "[gen][cast][implicit][ref][lnk]") {
    auto jit = gen_jit(R"SRC(
        module __ic_ref_lnk__;

        struct Base {
            val : int;
            Base() : val(0) {}
            Base(v : int) : val(v) {}
        }
        struct Derived : public Base {
            extra : int;
            Derived(v : int) : Base(v), extra(5) {}
        }

        get_val(l : Base~) : int { return l->val; }

        test() : int {
            d : Derived(37);
            ld : Derived~ = &d;
            return get_val(ld);   // ref<lnk<Derived>> loaded and upcast to lnk<Base>
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 37);
}

// =============================================================================
// Extra: explicit cast preserves field read/write through upcast ptr
// =============================================================================
TEST_CASE("Explicit cast: write through (Base*) ptr<Derived> modifies Base field", "[gen][cast][explicit][write]") {
    auto jit = gen_jit(R"SRC(
        module __ec_write__;

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
            pb : Base* = (Base*) (&d);
            pb->val = 200;
            return d.val;   // must see 200
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 200);
}

// =============================================================================
// Extra: explicit cross-kind cast lnk<Derived>→ptr<Base>→lnk<Base>
// =============================================================================
TEST_CASE("Explicit cast: chain lnk<Derived>→ptr<Base>→lnk<Base> via intermediate", "[gen][cast][explicit][chain]") {
    auto jit = gen_jit(R"SRC(
        module __ec_chain__;

        struct Base {
            val : int;
            Base() : val(0) {}
            Base(v : int) : val(v) {}
        }
        struct Derived : public Base {
            extra : int;
            Derived(v : int) : Base(v), extra(6) {}
        }

        test() : int {
            d : Derived(22);
            ld : Derived~ = &d;
            pb : Base* = (Base*) ld;    // lnk<Derived>→ptr<Base>
            lb : Base~ = pb;            // ptr<Base>→lnk<Base> (warning: nullable→non-null)
            return lb->val;             // must see 22
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 22);
}

// =============================================================================
// Extra: implicit upcast through interface — f(r : IFace&) with class ref
// =============================================================================
TEST_CASE("Implicit upcast: f(IFace&) called with class ref implementing IFace", "[gen][cast][implicit][interface]") {
    auto jit = gen_jit(R"SRC(
        module __ic_iface__;

        interface IAnimal {
            speak() : int;
        }
        class Dog : public IAnimal {
            public tricks : int;
            public Dog(v : int) : tricks(v) {}
            public speak() : int { return tricks; }
        }

        do_speak(a : IAnimal&) : int { return a.speak(); }

        test() : int {
            d : Dog(42);
            return do_speak(d);   // implicit upcast Dog→IAnimal&
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}


