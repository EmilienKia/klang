/*
 * K Language compiler
 *
 * Copyright 2023-2026 Emilien Kia
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
#include <catch2/catch_all.hpp>
#include "helpers.hpp"

TEST_CASE("Polymorphic union: basic concrete class base", "[gen][union][polymorphic]") {
    auto jit = gen_jit(R"SRC(
        module gen_poly_union_01;
        class Animal {
        public:
            val: int;
            get_val() : int { return val; }
        }
        class Dog : Animal {
        public:
            dog_bonus: int;
        }
        class Cat : Animal {
        public:
            cat_bonus: int;
        }
        union AnyAnimal : Animal {
            dog: Dog;
            cat: Cat;
        }

        test_arrow() : int {
            a : AnyAnimal;
            a.dog = Dog();
            a.dog.val = 10;
            return a->get_val();
        }

        test_star() : int {
            a : AnyAnimal;
            a.dog = Dog();
            a.dog.val = 25;
            return (*a).get_val();
        }

        test_switch_alt() : int {
            a : AnyAnimal;
            a.cat = Cat();
            a.cat.val = 42;
            return a->get_val();
        }
    )SRC");
    REQUIRE(jit);
    auto f_arrow = jit->lookup_symbol<int(*)()>("test_arrow");
    auto f_star  = jit->lookup_symbol<int(*)()>("test_star");
    auto f_sw    = jit->lookup_symbol<int(*)()>("test_switch_alt");
    REQUIRE(f_arrow); REQUIRE(f_star); REQUIRE(f_sw);
    REQUIRE(f_arrow() == 10);
    REQUIRE(f_star() == 25);
    REQUIRE(f_sw() == 42);
}

TEST_CASE("Polymorphic union: abstract class base with virtual dispatch", "[gen][union][polymorphic]") {
    auto jit = gen_jit(R"SRC(
        module gen_poly_union_02;
        abstract class Shape {
        public:
            abstract area() : int;
        }
        class Rect : Shape {
        public:
            w: int;
            h: int;
            override area() : int { return w * h; }
        }
        class Triangle : Shape {
        public:
            b: int;
            h: int;
            override area() : int { return (b * h) / 2; }
        }
        union AnyShape : Shape {
            rect: Rect;
            tri: Triangle;
        }

        test_rect(w: int, h: int) : int {
            s : AnyShape;
            s.rect = Rect();
            s.rect.w = w;
            s.rect.h = h;
            return s->area();
        }

        test_tri(b: int, h: int) : int {
            s : AnyShape;
            s.tri = Triangle();
            s.tri.b = b;
            s.tri.h = h;
            return s->area();
        }

        test_deref_call(w: int, h: int) : int {
            s : AnyShape;
            s.rect = Rect();
            s.rect.w = w;
            s.rect.h = h;
            return (*s).area();
        }
    )SRC");
    REQUIRE(jit);
    auto f_rect = jit->lookup_symbol<int(*)(int, int)>("test_rect");
    auto f_tri  = jit->lookup_symbol<int(*)(int, int)>("test_tri");
    auto f_deref = jit->lookup_symbol<int(*)(int, int)>("test_deref_call");
    REQUIRE(f_rect); REQUIRE(f_tri); REQUIRE(f_deref);
    REQUIRE(f_rect(5, 6) == 30);
    REQUIRE(f_tri(10, 4) == 20);
    REQUIRE(f_deref(7, 8) == 56);
}

TEST_CASE("Polymorphic union: interface base", "[gen][union][polymorphic]") {
    auto jit = gen_jit(R"SRC(
        module gen_poly_union_03;
        interface Describable {
            describe() : int;
        }
        class Alpha : Describable {
        public:
            override describe() : int { return 100; }
        }
        class Beta : Describable {
        public:
            override describe() : int { return 200; }
        }
        union AnyItem : Describable {
            a: Alpha;
            b: Beta;
        }

        test_alpha() : int {
            item : AnyItem;
            item.a = Alpha();
            return item->describe();
        }

        test_beta() : int {
            item : AnyItem;
            item.b = Beta();
            return item->describe();
        }
    )SRC");
    REQUIRE(jit);
    auto f_a = jit->lookup_symbol<int(*)()>("test_alpha");
    auto f_b = jit->lookup_symbol<int(*)()>("test_beta");
    REQUIRE(f_a); REQUIRE(f_b);
    REQUIRE(f_a() == 100);
    REQUIRE(f_b() == 200);
}

TEST_CASE("Polymorphic union: passing *u by reference to free function", "[gen][union][polymorphic]") {
    auto jit = gen_jit(R"SRC(
        module gen_poly_union_04;
        abstract class Base {
        public:
            abstract code() : int;
        }
        class First : Base {
        public:
            override code() : int { return 111; }
        }
        class Second : Base {
        public:
            override code() : int { return 222; }
        }
        union Poly : Base {
            first: First;
            second: Second;
        }

        inspect_ref(b: Base&) : int {
            return b.code();
        }

        test_pass_first() : int {
            p : Poly;
            p.first = First();
            return inspect_ref(*p);
        }

        test_pass_second() : int {
            p : Poly;
            p.second = Second();
            return inspect_ref(*p);
        }
    )SRC");
    REQUIRE(jit);
    auto f1 = jit->lookup_symbol<int(*)()>("test_pass_first");
    auto f2 = jit->lookup_symbol<int(*)()>("test_pass_second");
    REQUIRE(f1); REQUIRE(f2);
    REQUIRE(f1() == 111);
    REQUIRE(f2() == 222);
}

TEST_CASE("Polymorphic union: multiple inheritance with non-zero sub-object offset", "[gen][union][polymorphic]") {
    auto jit = gen_jit(R"SRC(
        module gen_poly_union_05;
        class FirstBase {
        public:
            x: int;
            y: int;
            z: int;
        }
        abstract class TargetBase {
        public:
            abstract calc() : int;
        }
        class MultiDerived : FirstBase, TargetBase {
        public:
            override calc() : int { return x + y + z; }
        }
        class SimpleDerived : TargetBase {
        public:
            v: int;
            override calc() : int { return v; }
        }
        union PolyMulti : TargetBase {
            multi: MultiDerived;
            simple: SimpleDerived;
        }

        test_multi(x: int, y: int, z: int) : int {
            p : PolyMulti;
            p.multi = MultiDerived();
            p.multi.x = x;
            p.multi.y = y;
            p.multi.z = z;
            return p->calc();
        }

        test_simple(v: int) : int {
            p : PolyMulti;
            p.simple = SimpleDerived();
            p.simple.v = v;
            return p->calc();
        }
    )SRC");
    REQUIRE(jit);
    auto f_multi  = jit->lookup_symbol<int(*)(int, int, int)>("test_multi");
    auto f_simple = jit->lookup_symbol<int(*)(int)>("test_simple");
    REQUIRE(f_multi); REQUIRE(f_simple);
    REQUIRE(f_multi(10, 20, 30) == 60);
    REQUIRE(f_simple(99) == 99);
}

TEST_CASE("Polymorphic union: inheritance of polymorphic union", "[gen][union][polymorphic]") {
    auto jit = gen_jit(R"SRC(
        module gen_poly_union_06;
        abstract class Animal {
        public:
            abstract speak() : int;
        }
        class Dog : Animal {
        public:
            override speak() : int { return 1; }
        }
        class Cat : Animal {
        public:
            override speak() : int { return 2; }
        }
        class Bird : Animal {
        public:
            override speak() : int { return 3; }
        }
        union BaseUnion : Animal {
            dog: Dog;
            cat: Cat;
        }
        union DerivedUnion : BaseUnion {
            bird: Bird;
        }

        test_inherited_dog() : int {
            d : DerivedUnion;
            d.dog = Dog();
            return d->speak();
        }

        test_inherited_cat() : int {
            d : DerivedUnion;
            d.cat = Cat();
            return d->speak();
        }

        test_own_bird() : int {
            d : DerivedUnion;
            d.bird = Bird();
            return d->speak();
        }
    )SRC");
    REQUIRE(jit);
    auto f_dog  = jit->lookup_symbol<int(*)()>("test_inherited_dog");
    auto f_cat  = jit->lookup_symbol<int(*)()>("test_inherited_cat");
    auto f_bird = jit->lookup_symbol<int(*)()>("test_own_bird");
    REQUIRE(f_dog); REQUIRE(f_cat); REQUIRE(f_bird);
    REQUIRE(f_dog() == 1);
    REQUIRE(f_cat() == 2);
    REQUIRE(f_bird() == 3);
}

TEST_CASE("Polymorphic union: lifecycle and destruction on reassignment", "[gen][union][polymorphic]") {
    auto jit = gen_jit(R"SRC(
        module gen_poly_union_07;
        dtor_count: int = 0;

        abstract class Resource {
        public:
            abstract id() : int;
        }
        class ResA : Resource {
        public:
            override id() : int { return 1; }
            ~ResA() { dtor_count = dtor_count + 1; }
        }
        class ResB : Resource {
        public:
            override id() : int { return 2; }
            ~ResB() { dtor_count = dtor_count + 10; }
        }
        union AnyRes : Resource {
            a: ResA;
            b: ResB;
        }

        run_lifecycle() : int {
            dtor_count = 0;
            {
                r : AnyRes;
                r.a = ResA();
                r.b = ResB(); // destroys ResA (+1)
            } // destroys ResB (+10)
            return dtor_count;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("run_lifecycle");
    REQUIRE(fn);
    REQUIRE(fn() == 11);
}

TEST_CASE("Polymorphic union: compile errors for invalid declarations", "[gen][union][polymorphic]") {
    SECTION("Alternative is not a class type") {
        REQUIRE(compile_should_fail(R"SRC(
            module gen_poly_err_01;
            class Animal { }
            union Bad : Animal {
                i: int;
            }
        )SRC", nullptr));
    }

    SECTION("Alternative does not derive from base") {
        REQUIRE(compile_should_fail(R"SRC(
            module gen_poly_err_02;
            class Animal { }
            class Vehicle { }
            union Bad : Animal {
                v: Vehicle;
            }
        )SRC", nullptr));
    }

    SECTION("Alternative is an abstract class") {
        REQUIRE(compile_should_fail(R"SRC(
            module gen_poly_err_03;
            abstract class Animal { }
            abstract class Mammal : Animal { }
            union Bad : Animal {
                m: Mammal;
            }
        )SRC", nullptr));
    }

    SECTION("Base is a struct") {
        REQUIRE(compile_should_fail(R"SRC(
            module gen_poly_err_04;
            struct Point { x: int; }
            union Bad : Point {
                x: int;
            }
        )SRC", nullptr));
    }
}

TEST_CASE("Polymorphic union: cross-module export and import", "[gen][union][polymorphic][import]") {
    auto result = build_exec_with_lib(R"SRC(
        module gen_poly_import_lib;
        public:
        abstract class BaseGreeter {
        public:
            abstract greet() : int;
        }
        class EnglishGreeter : BaseGreeter {
        public:
            override greet() : int { return 10; }
        }
        class FrenchGreeter : BaseGreeter {
        public:
            override greet() : int { return 20; }
        }
        union AnyGreeter : BaseGreeter {
            en: EnglishGreeter;
            fr: FrenchGreeter;
        }
        dummy() : int { return 0; }
    )SRC", R"SRC(
        module gen_poly_import_app;
        import gen_poly_import_lib;

        main() : int {
            g : gen_poly_import_lib::AnyGreeter;
            g.fr = gen_poly_import_lib::FrenchGreeter();
            if (g->greet() == 20) {
                return 0;
            }
            return 1;
        }
    )SRC");
    REQUIRE(result.exit_code == 0);
}
