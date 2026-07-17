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
 * Virtual destructor dispatch tests.
 *
 * Every class/interface transitively extends ::k::Object (either directly or
 * through its base chain), and ::k::Object declares the first-ever virtual
 * destructor at the universal vtable slot 0 (see build_vtable_layout() in
 * gen_class.cpp). Every derived class's destructor (explicit or
 * compiler-generated) overrides that same slot rather than introducing a new
 * one, so the destructor is reachable at a fixed vtable offset from any
 * class/interface-typed reference, pointer or owner in the whole hierarchy.
 *
 * Tests covered:
 *  [1] Structural: a class's destructor occupies vtable slot 0 and is virtual.
 *  [2] Simple class inheriting implicit Object: destroying through an
 *      Object! owner calls the derived destructor.
 *  [3] Interface-implementing class: destroying through an interface-typed
 *      owner calls the derived destructor exactly once (the central case for
 *      foreach: ConstIterator<T>/Iterator<T>-typed hidden loop variables).
 *  [4] Three-level inheritance chain: destructor call order is most-derived
 *      to least-derived (C, then B, then A).
 *  [5] Secondary interface base (this-adjustment): destroying through the
 *      SECOND listed interface base (not the primary vtable path) still
 *      calls the most-derived destructor exactly once, with the correctly
 *      adjusted 'this' pointer (verified via distinguishable field values).
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

// ════════════════════════════════════════════════════════════════════════════
//  [1] Structural: destructor vtable slot
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Virtual destructor: class destructor occupies universal vtable slot 0", "[model][vdtor]") {
    auto comp = compile_model_with_stdlib(R"SRC(
        module __test_vdtor_slot0__;
        class Foo {
            ~Foo() {}
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto foo = find_klass(comp, "Foo");
    REQUIRE(foo != nullptr);

    auto dtor = foo->get_destructor();
    REQUIRE(dtor != nullptr);
    CHECK(dtor->is_virtual());
    CHECK(dtor->get_vtable_slot() == 0);
}

TEST_CASE("Virtual destructor: compiler-generated destructor is also virtual at slot 0", "[model][vdtor]") {
    auto comp = compile_model_with_stdlib(R"SRC(
        module __test_vdtor_slot0_implicit__;
        class Foo {
            dummy() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto foo = find_klass(comp, "Foo");
    REQUIRE(foo != nullptr);

    auto dtor = foo->get_destructor();
    REQUIRE(dtor != nullptr);
    CHECK(dtor->is_virtual());
    CHECK(dtor->get_vtable_slot() == 0);
}

// ════════════════════════════════════════════════════════════════════════════
//  [2] Simple class destroyed through an Object! owner
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Virtual destructor: destroying through Object! owner calls derived dtor", "[gen][vdtor]") {
    auto jit = gen_jit(R"SRC(
        module __test_vdtor_object_owner__;

        dtor_ran : int = 0;

        class Foo {
            ~Foo() { dtor_ran = 1; }
        }

        test() : int {
            o : Object! = new Foo();
            delete o;
            return dtor_ran;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 1);
}

// ════════════════════════════════════════════════════════════════════════════
//  [3] Interface-implementing class destroyed through interface owner
//      (central case for foreach: ConstIterator<T>/Iterator<T>-like usage)
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Virtual destructor: destroying through interface owner calls derived dtor exactly once", "[gen][vdtor]") {
    auto jit = gen_jit(R"SRC(
        module __test_vdtor_interface_owner__;

        dtor_count : int = 0;

        interface Greeter {
            greet() : int;
        }

        class FooGreeter : Greeter {
            ~FooGreeter() { dtor_count = dtor_count + 1; }
            override greet() : int { return 42; }
        }

        test() : int {
            g : Greeter! = new FooGreeter();
            delete g;
            return dtor_count;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 1);
}

// ════════════════════════════════════════════════════════════════════════════
//  [4] Three-level inheritance chain: destruction order
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Virtual destructor: 3-level chain destroyed most-derived first", "[gen][vdtor]") {
    auto jit = gen_jit(R"SRC(
        module __test_vdtor_chain__;

        dtor_log : int = 0;

        class A {
            ~A() { dtor_log = dtor_log * 10 + 1; }
        }
        class B : A {
            ~B() { dtor_log = dtor_log * 10 + 2; }
        }
        class C : B {
            ~C() { dtor_log = dtor_log * 10 + 3; }
        }

        test() : int {
            a : A! = new C();
            delete a;
            return dtor_log;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    // C's dtor runs first (3), then B (32), then A (321).
    CHECK(test() == 321);
}

// ════════════════════════════════════════════════════════════════════════════
//  [5] Secondary interface base — this-adjustment correctness
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Virtual destructor: destroying through secondary interface base still calls derived dtor once", "[gen][vdtor]") {
    auto jit = gen_jit(R"SRC(
        module __test_vdtor_secondary_base__;

        dtor_count : int = 0;
        dtor_field_sum : int = 0;

        interface IA {
            a() : int;
        }
        interface IB {
            b() : int;
        }

        class Impl : IA, IB {
            x : int = 111;
            y : int = 222;
            ~Impl() {
                // If the 'this' pointer were not correctly adjusted back to
                // the Impl subobject when destroyed through the SECOND
                // (non-primary) base IB, these field reads would be garbage.
                dtor_field_sum = x + y;
                dtor_count = dtor_count + 1;
            }
            override a() : int { return 1; }
            override b() : int { return 2; }
        }

        test() : int {
            // Destroy through IB (the secondary base, not the first listed).
            b : IB! = new Impl();
            delete b;
            return dtor_count;
        }

        get_field_sum() : int { return dtor_field_sum; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 1);

    auto get_field_sum = jit->lookup_symbol<int(*)()>("get_field_sum");
    REQUIRE(get_field_sum != nullptr);
    CHECK(get_field_sum() == 333);
}
