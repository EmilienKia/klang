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
 *  [6] Secondary interface base through a multi-level chain: the destructor
 *      is introduced two hops above the most-derived class; destroying
 *      through the secondary base still resolves the whole chain in order.
 *  [7] Secondary interface base with a template-instantiated hierarchy:
 *      same scenario as [5]/[6] but with template classes/interfaces,
 *      exercising the template-instantiation vtable-building path
 *      (ensure_klass_vtable_built() in resolvers_aggregate.cpp) rather than
 *      the plain-class path (build_vtable_layout() in gen_class.cpp).
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

// ════════════════════════════════════════════════════════════════════════════
//  [1] Structural: destructor vtable slot
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Virtual destructor: class destructor occupies universal vtable slot 0", "[model][vdtor]") {
    auto comp = compile_model_with_stdlib(R"SRC(
        module gen_virtual_destructor_01;
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
        module gen_virtual_destructor_02;
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
        module gen_virtual_destructor_03;

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
        module gen_virtual_destructor_04;

        dtor_count : int = 0;

        interface Greeter {
            greet() : int;
        }

        class FooGreeter : Greeter {
            ~FooGreeter() { ++dtor_count; }
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
        module gen_virtual_destructor_05;

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
        module gen_virtual_destructor_06;

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
                ++dtor_count;
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

// ════════════════════════════════════════════════════════════════════════════
//  [6] Secondary interface base, multi-level chain (destructor introduced two
//      hops above the most-derived class)
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Virtual destructor: secondary base still resolves through a multi-level chain", "[gen][vdtor]") {
    auto jit = gen_jit(R"SRC(
        module gen_virtual_destructor_07;

        dtor_log : int = 0;

        interface IX { x() : int; }
        interface IY { y() : int; }

        class GrandParent : IX, IY {
            ~GrandParent() { dtor_log = dtor_log * 10 + 1; }
            override x() : int { return 1; }
            override y() : int { return 2; }
        }
        class Parent : GrandParent {
            ~Parent() { dtor_log = dtor_log * 10 + 2; }
        }
        class Child : Parent {
            ~Child() { dtor_log = dtor_log * 10 + 3; }
        }

        test() : int {
            // Destroy two hops removed, through the secondary base IY.
            y : IY! = new Child();
            delete y;
            return dtor_log;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    // Child's dtor runs first (3), then Parent (32), then GrandParent (321).
    CHECK(test() == 321);
}

// ════════════════════════════════════════════════════════════════════════════
//  [7] Secondary interface base with a template-instantiated hierarchy
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Virtual destructor: secondary base still resolves for a template-instantiated hierarchy", "[gen][vdtor][templates]") {
    auto jit = gen_jit(R"SRC(
        module gen_virtual_destructor_08;

        dtor_log : int = 0;

        template<typename T>
        interface IX { }
        template<typename T>
        interface IY { }

        template<typename T>
        class GrandParent : IX<T>, IY<T> {
            ~GrandParent() { dtor_log = dtor_log * 10 + 1; }
        }
        template<typename T>
        class Child : GrandParent<T> {
            ~Child() { dtor_log = dtor_log * 10 + 2; }
        }

        test() : int {
            // Destroy the template-instantiated Child<int> through the
            // secondary base IY<int>.
            y : IY<int>! = new Child<int>();
            delete y;
            return dtor_log;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    // Child's dtor runs first (2), then GrandParent's (21).
    CHECK(test() == 21);
}
