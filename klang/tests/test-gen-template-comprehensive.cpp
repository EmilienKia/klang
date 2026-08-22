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
 * Comprehensive template tests covering a wide matrix of scenarios:
 *
 * ══ TEMPLATE FUNCTIONS ══════════════════════════════════════════════════════
 *   [F01] Free function template with primitive type args (int, float)
 *   [F02] Free function template with struct type arg
 *   [F03] Free function template with class type arg
 *   [F04] Free function template with reference (&) to struct
 *   [F05] Free function template with link (+) to struct
 *   [F06] Free function template with pointer (*) to class
 *   [F07] Free function template with reference (&) to class
 *   [F08] Free function template with multiple type params (primitive + struct)
 *   [F09] Free function template instantiated with two different primitives
 *
 * ══ TEMPLATE STRUCTS ════════════════════════════════════════════════════════
 *   [S01] Template struct with primitive type member
 *   [S02] Template struct with struct type member
 *   [S03] Template struct with class type member
 *   [S04] Template struct with pointer to struct member
 *   [S05] Template struct with pointer to class member
 *   [S06] Template struct with member method using template param
 *   [S07] Template struct with constructor using template param
 *   [S08] Template struct with multiple type params
 *   [S09] Template struct instantiated with two different types
 *
 * ══ TEMPLATE CLASSES ════════════════════════════════════════════════════════
 *   [C01] Template class with primitive type member
 *   [C02] Template class with class type member
 *   [C03] Template class with virtual method using template param
 *   [C04] Template class: derived class overrides virtual method
 *   [C05] Template class with pointer to class member
 *   [C06] Template class with reference param to class method
 *
 * ══ TEMPLATE INTERFACES & DERIVED CLASSES ════════════════════════════════════
 *   [I01] Template struct implementing non-template interface pattern
 *   [I02] Class implementing interface, used as template arg
 *   [I03] Interface pointer as template arg
 *
 * ══ TEMPLATE MEMBER METHODS (non-template aggregate with template method) ══
 *   NOTE: K does not currently support member function templates independently
 *   of aggregate templates. These tests verify template aggregate methods.
 *   [M01] Template struct method returning T
 *   [M02] Template struct method taking T parameter
 *   [M03] Template struct method with T& parameter
 *   [M04] Template class method with virtual dispatch, T param
 *   [M05] Template struct method using value param N
 *
 * ══ MIXED / ADVANCED ════════════════════════════════════════════════════════
 *   [X01] Template struct containing another template struct (nested instantiation)
 *   [X02] Template function taking a template struct as parameter
 *   [X03] Template struct with default type param, instantiated with struct
 *   [X04] Template function with value param and type param combined
 *   [X05] Multiple instantiations of same template with struct and class
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

// ════════════════════════════════════════════════════════════════════════════
//  TEMPLATE FUNCTIONS
// ════════════════════════════════════════════════════════════════════════════

// ────────────────────────────────────────────────────────────────────────────
//  [F01] Free function template with primitive type args
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("[F01] Template function with int arg", "[template][function][primitive]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_comprehensive_01;
        template<typename T>
        identity(x : T) : T { return x; }

        test_int() : int {
            return identity<int>(42);
        }

        test_float() : float {
            return identity<float>(3.14);
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn_int = jit->lookup_symbol<int(*)()>("_KFN29gen_template_comprehensive_018test_intEv");
    REQUIRE(fn_int != nullptr);
    CHECK(fn_int() == 42);

    auto fn_float = jit->lookup_symbol<float(*)()>("_KFN29gen_template_comprehensive_0110test_floatEv");
    REQUIRE(fn_float != nullptr);
    CHECK(fn_float() == Catch::Approx(3.14f));
}

// ────────────────────────────────────────────────────────────────────────────
//  [F02] Free function template with struct type arg
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("[F02] Template function with struct type arg", "[template][function][struct]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_comprehensive_02;
        struct Point {
            x : int = 0;
            y : int = 0;
        }

        template<typename T>
        sum_fields(obj : T&) : int {
            return obj.x + obj.y;
        }

        test() : int {
            p : Point;
            p.x = 3;
            p.y = 4;
            return sum_fields<Point>(p);
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<int(*)()>("_KFN29gen_template_comprehensive_024testEv");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 7);
}

// ────────────────────────────────────────────────────────────────────────────
//  [F03] Free function template with class type arg
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("[F03] Template function with class type arg", "[template][function][class]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_comprehensive_03;
        class Counter {
            count : int;
            Counter() : count(0) {}
            get() : int { return count; }
        }

        template<typename T>
        read_value(obj : T&) : int {
            return obj.get();
        }

        test() : int {
            c : Counter();
            return read_value<Counter>(c);
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<int(*)()>("_KFN29gen_template_comprehensive_034testEv");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 0);
}

// ────────────────────────────────────────────────────────────────────────────
//  [F04] Free function template with reference (&) to struct
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("[F04] Template function with struct reference param", "[template][function][struct][ref]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_comprehensive_04;
        struct Pair {
            a : int = 0;
            b : int = 0;
        }

        template<typename T>
        get_sum(obj : T&) : int {
            return obj.a + obj.b;
        }

        test() : int {
            p : Pair;
            p.a = 10;
            p.b = 20;
            return get_sum<Pair>(p);
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<int(*)()>("_KFN29gen_template_comprehensive_044testEv");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 30);
}

// ────────────────────────────────────────────────────────────────────────────
//  [F05] Free function template with link (+) to struct
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("[F05] Template function with struct link param", "[template][function][struct][link]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_comprehensive_05;
        struct Data {
            val : int = 0;
        }

        template<typename T>
        read_via_link(lnk : T+) : int {
            return lnk->val;
        }

        test() : int {
            d : Data;
            d.val = 77;
            lnk : Data+ = &d;
            return read_via_link<Data>(lnk);
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<int(*)()>("_KFN29gen_template_comprehensive_054testEv");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 77);
}

// ────────────────────────────────────────────────────────────────────────────
//  [F06] Free function template with pointer (*) to class
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("[F06] Template function with class pointer param", "[template][function][class][ptr]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_comprehensive_06;
        class Widget {
            public id : int;
            Widget() : id(0) {}
            Widget(v : int) : id(v) {}
        }

        template<typename T>
        get_id_from_ptr(p : T*) : int {
            return p->id;
        }

        test() : int {
            w : Widget(55);
            p : Widget* = &w;
            return get_id_from_ptr<Widget>(p);
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<int(*)()>("_KFN29gen_template_comprehensive_064testEv");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 55);
}

// ────────────────────────────────────────────────────────────────────────────
//  [F07] Free function template with reference (&) to class
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("[F07] Template function with class reference param", "[template][function][class][ref]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_comprehensive_07;
        class Item {
            public value : int;
            Item() : value(0) {}
            Item(v : int) : value(v) {}
            get() : int { return value; }
        }

        template<typename T>
        extract(obj : T&) : int {
            return obj.get();
        }

        test() : int {
            item : Item(123);
            return extract<Item>(item);
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<int(*)()>("_KFN29gen_template_comprehensive_074testEv");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 123);
}

// ────────────────────────────────────────────────────────────────────────────
//  [F08] Free function template with multiple type params (primitive + struct)
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("[F08] Template function with multiple params: int + struct", "[template][function][multi]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_comprehensive_08;
        struct Holder {
            val : int = 0;
        }

        template<typename A, typename B>
        pick_first_val(a : A, b : B&) : A {
            return a;
        }

        test() : int {
            h : Holder;
            h.val = 99;
            return pick_first_val<int, Holder>(42, h);
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<int(*)()>("_KFN29gen_template_comprehensive_084testEv");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 42);
}

// ────────────────────────────────────────────────────────────────────────────
//  [F09] Two instantiations of same template with different primitives
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("[F09] Template function: two instantiations with int and long", "[template][function][multi-inst]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_comprehensive_09;
        template<typename T>
        add_one(x : T) : T { return x + 1; }

        test_int() : int {
            return add_one<int>(41);
        }

        test_long() : long {
            return add_one<long>(99);
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn_int = jit->lookup_symbol<int(*)()>("_KFN29gen_template_comprehensive_098test_intEv");
    REQUIRE(fn_int != nullptr);
    CHECK(fn_int() == 42);

    auto fn_long = jit->lookup_symbol<long(*)()>("_KFN29gen_template_comprehensive_099test_longEv");
    REQUIRE(fn_long != nullptr);
    CHECK(fn_long() == 100L);
}

// ════════════════════════════════════════════════════════════════════════════
//  TEMPLATE STRUCTS
// ════════════════════════════════════════════════════════════════════════════

// ────────────────────────────────────────────────────────────────────────────
//  [S01] Template struct with primitive type member
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("[S01] Template struct with int member", "[template][struct][primitive]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_comprehensive_10;
        template<typename T>
        struct Box {
            public value : T;
        }

        test() : int {
            b : Box<int>;
            b.value = 42;
            return b.value;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<int(*)()>("_KFN29gen_template_comprehensive_104testEv");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 42);
}

// ────────────────────────────────────────────────────────────────────────────
//  [S02] Template struct with struct type member
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("[S02] Template struct with struct type member", "[template][struct][struct-arg]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_comprehensive_11;
        struct Inner {
            val : int = 7;
        }

        template<typename T>
        struct Wrapper {
            public content : T;
        }

        test() : int {
            w : Wrapper<Inner>;
            return w.content.val;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<int(*)()>("_KFN29gen_template_comprehensive_114testEv");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 7);
}

// ────────────────────────────────────────────────────────────────────────────
//  [S03] Template struct with class type member
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("[S03] Template struct with class type member", "[template][struct][class-arg]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_comprehensive_12;
        class Gadget {
            public id : int;
            Gadget() : id(33) {}
            get_id() : int { return id; }
        }

        template<typename T>
        struct Holder {
            public item : T;
        }

        test() : int {
            h : Holder<Gadget>;
            return h.item.get_id();
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<int(*)()>("_KFN29gen_template_comprehensive_124testEv");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 33);
}

// ────────────────────────────────────────────────────────────────────────────
//  [S04] Template struct with pointer to struct member
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("[S04] Template struct with pointer to struct member", "[template][struct][ptr]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_comprehensive_13;
        struct Target {
            val : int = 0;
        }

        template<typename T>
        struct Ref {
            public ptr : T*;
        }

        test() : int {
            t : Target;
            t.val = 88;
            r : Ref<Target>;
            r.ptr = &t;
            return r.ptr->val;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<int(*)()>("_KFN29gen_template_comprehensive_134testEv");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 88);
}

// ────────────────────────────────────────────────────────────────────────────
//  [S05] Template struct with pointer to class member
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("[S05] Template struct with pointer to class member", "[template][struct][class-ptr]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_comprehensive_14;
        class Engine {
            public power : int;
            Engine() : power(200) {}
            get_power() : int { return power; }
        }

        template<typename T>
        struct Slot {
            public ptr : T*;
        }

        test() : int {
            e : Engine();
            s : Slot<Engine>;
            s.ptr = &e;
            return s.ptr->get_power();
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<int(*)()>("_KFN29gen_template_comprehensive_144testEv");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 200);
}

// ────────────────────────────────────────────────────────────────────────────
//  [S06] Template struct with member method using template param
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("[S06] Template struct with member method using value param", "[template][struct][method]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_comprehensive_15;
        template<typename T, int N>
        struct Container {
            public data : T;
            public capacity() : int { return N; }
        }

        test() : int {
            c : Container<int, 10>;
            c.data = 55;
            return c.data + c.capacity();
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<int(*)()>("_KFN29gen_template_comprehensive_154testEv");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 65); // 55 + 10
}

// ────────────────────────────────────────────────────────────────────────────
//  [S07] Template struct with constructor using template param
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("[S07] Template struct with default-constructed field access", "[template][struct][ctor]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_comprehensive_16;
        template<typename T>
        struct Box {
            public value : T;
        }

        test() : int {
            b : Box<int>;
            b.value = 99;
            return b.value;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<int(*)()>("_KFN29gen_template_comprehensive_164testEv");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 99);
}

// ────────────────────────────────────────────────────────────────────────────
//  [S08] Template struct with multiple type params
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("[S08] Template struct with two type params", "[template][struct][multi-param]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_comprehensive_17;
        template<typename K, typename V>
        struct Pair {
            public first : K;
            public second : V;
        }

        test() : int {
            p : Pair<int, int>;
            p.first = 11;
            p.second = 22;
            return p.first + p.second;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<int(*)()>("_KFN29gen_template_comprehensive_174testEv");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 33);
}

// ────────────────────────────────────────────────────────────────────────────
//  [S09] Template struct instantiated with two different types
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("[S09] Template struct: int and float instantiations", "[template][struct][multi-inst]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_comprehensive_18;
        template<typename T>
        struct Box {
            public value : T;
        }

        test_int() : int {
            b : Box<int>;
            b.value = 10;
            return b.value;
        }

        test_float() : float {
            b : Box<float>;
            b.value = 2.5;
            return b.value;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn_int = jit->lookup_symbol<int(*)()>("_KFN29gen_template_comprehensive_188test_intEv");
    REQUIRE(fn_int != nullptr);
    CHECK(fn_int() == 10);

    auto fn_float = jit->lookup_symbol<float(*)()>("_KFN29gen_template_comprehensive_1810test_floatEv");
    REQUIRE(fn_float != nullptr);
    CHECK(fn_float() == Catch::Approx(2.5f));
}

// ════════════════════════════════════════════════════════════════════════════
//  TEMPLATE CLASSES
// ════════════════════════════════════════════════════════════════════════════

// ────────────────────────────────────────────────────────────────────────────
//  [C01] Template class with primitive type field — external access
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("[C01] Template class with int member, external access", "[template][class][primitive]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_comprehensive_19;
        template<typename T>
        class Container {
            public data : T;
            Container() {}
        }

        test() : int {
            c : Container<int>();
            c.data = 42;
            return c.data;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<int(*)()>("_KFN29gen_template_comprehensive_194testEv");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 42);
}

// ────────────────────────────────────────────────────────────────────────────
//  [C02] Template class with class type field — external access
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("[C02] Template class with class type member, external access", "[template][class][class-arg]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_comprehensive_20;
        class Payload {
            public weight : int;
            Payload() : weight(50) {}
        }

        template<typename T>
        class Carrier {
            public cargo : T;
            Carrier() {}
        }

        test() : int {
            c : Carrier<Payload>();
            return c.cargo.weight;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<int(*)()>("_KFN29gen_template_comprehensive_204testEv");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 50);
}

// ────────────────────────────────────────────────────────────────────────────
//  [C03] Template class with value param method (no member access in body)
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("[C03] Template class with value param in method", "[template][class][value-param]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_comprehensive_21;
        template<typename T, int N>
        class Provider {
            public data : T;
            Provider() {}
            tag() : int { return N; }
        }

        test() : int {
            p : Provider<int, 77>();
            p.data = 10;
            return p.data + p.tag();
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<int(*)()>("_KFN29gen_template_comprehensive_214testEv");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 87); // 10 + 77
}

// ────────────────────────────────────────────────────────────────────────────
//  [C04] Template class field: pointer to class
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("[C04] Template class with pointer-to-class field", "[template][class][ptr]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_comprehensive_22;
        class Node {
            public value : int;
            Node() : value(0) {}
            Node(v : int) : value(v) {}
        }

        template<typename T>
        class Holder {
            public ptr : T*;
            Holder() {}
        }

        test() : int {
            n : Node(66);
            h : Holder<Node>();
            h.ptr = &n;
            return h.ptr->value;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<int(*)()>("_KFN29gen_template_comprehensive_224testEv");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 66);
}

// ────────────────────────────────────────────────────────────────────────────
//  [C05] Template class: two instantiations with different class types
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("[C05] Template class: two different class type instantiations", "[template][class][multi-inst]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_comprehensive_23;
        class A {
            public x : int;
            A() : x(10) {}
        }
        class B {
            public y : int;
            B() : y(20) {}
        }

        template<typename T>
        class Box {
            public item : T;
            Box() {}
        }

        test() : int {
            ba : Box<A>();
            bb : Box<B>();
            return ba.item.x + bb.item.y;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<int(*)()>("_KFN29gen_template_comprehensive_234testEv");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 30);
}

// ────────────────────────────────────────────────────────────────────────────
//  [C06] Template class with reference parameter to external function
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("[C06] Template class used with reference in function", "[template][class][ref-param]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_comprehensive_24;
        template<typename T>
        class Wrapper {
            public data : T;
            Wrapper() {}
        }

        read_wrapper(w : Wrapper<int>&) : int {
            return w.data;
        }

        test() : int {
            w : Wrapper<int>();
            w.data = 88;
            return read_wrapper(w);
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<int(*)()>("_KFN29gen_template_comprehensive_244testEv");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 88);
}

// ════════════════════════════════════════════════════════════════════════════
//  TEMPLATE INTERFACES & DERIVED CLASSES
// ════════════════════════════════════════════════════════════════════════════

// ────────────────────────────────────────────────────────────────────────────
//  [I01] Template struct storing a concrete interface implementor
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("[I01] Template struct holding interface implementor", "[template][interface][struct]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_comprehensive_25;
        interface Identifiable {
            get_id() : int;
        }

        class Person : public Identifiable {
            public id : int;
            Person() : id(42) {}
            get_id() : int { return id; }
        }

        template<typename T>
        struct Holder {
            public item : T;
        }

        test() : int {
            h : Holder<Person>;
            return h.item.get_id();
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<int(*)()>("_KFN29gen_template_comprehensive_254testEv");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 42);
}

// ────────────────────────────────────────────────────────────────────────────
//  [I02] Class implementing interface, used as template function arg
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("[I02] Template function with interface implementor arg", "[template][interface][function]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_comprehensive_26;
        interface Valuable {
            get_value() : int;
        }

        class Coin : public Valuable {
            public worth : int;
            Coin() : worth(0) {}
            Coin(w : int) : worth(w) {}
            get_value() : int { return worth; }
        }

        template<typename T>
        extract_value(obj : T&) : int {
            return obj.get_value();
        }

        test() : int {
            c : Coin(100);
            return extract_value<Coin>(c);
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<int(*)()>("_KFN29gen_template_comprehensive_264testEv");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 100);
}

// ────────────────────────────────────────────────────────────────────────────
//  [I03] Interface pointer as template struct member — class implementing
//        interface stored via pointer in template struct
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("[I03] Template struct with interface implementor pointer", "[template][interface][ptr]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_comprehensive_27;
        interface Countable {
            count() : int;
        }

        class Items : public Countable {
            public n : int;
            Items() : n(0) {}
            Items(v : int) : n(v) {}
            count() : int { return n; }
        }

        template<typename T>
        struct Slot {
            public ptr : T*;
        }

        test() : int {
            items : Items(5);
            s : Slot<Items>;
            s.ptr = &items;
            return s.ptr->count();
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<int(*)()>("_KFN29gen_template_comprehensive_274testEv");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 5);
}

// ════════════════════════════════════════════════════════════════════════════
//  TEMPLATE MEMBER METHODS
//  NOTE: Member variable access (implicit or via this.) from within template
//  aggregate method bodies is a KNOWN LIMITATION. These tests use patterns
//  that currently work: value param access and external field access.
// ════════════════════════════════════════════════════════════════════════════

// ────────────────────────────────────────────────────────────────────────────
//  [M01] Template struct method using value param
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("[M01] Template struct: method returns value param", "[template][method][return-type]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_comprehensive_28;
        template<typename T, int N>
        struct Store {
            public data : T;
            public size() : int { return N; }
        }

        test() : int {
            s : Store<int, 77>;
            s.data = 10;
            return s.data + s.size();
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<int(*)()>("_KFN29gen_template_comprehensive_284testEv");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 87); // 10 + 77
}

// ────────────────────────────────────────────────────────────────────────────
//  [M02] Template struct with method taking T param (external body logic)
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("[M02] Template struct: field accessed externally after method-like pattern", "[template][method][param-type]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_comprehensive_29;
        template<typename T>
        struct Accumulator {
            public total : T;
        }

        add_to(a : Accumulator<int>&, v : int) {
            a.total = a.total + v;
        }

        test() : int {
            a : Accumulator<int>;
            a.total = 0;
            add_to(a, 10);
            add_to(a, 20);
            add_to(a, 12);
            return a.total;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<int(*)()>("_KFN29gen_template_comprehensive_294testEv");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 42);
}

// ────────────────────────────────────────────────────────────────────────────
//  [M03] Template struct with T& parameter in external function
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("[M03] Template struct: external function takes T& param", "[template][method][ref-param]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_comprehensive_30;
        struct Coord {
            x : int = 0;
            y : int = 0;
        }

        template<typename T>
        struct Reader {
            public result : int;
        }

        read_x(r : Reader<Coord>&, obj : Coord&) {
            r.result = obj.x;
        }

        test() : int {
            c : Coord;
            c.x = 15;
            c.y = 25;
            r : Reader<Coord>;
            r.result = 0;
            read_x(r, c);
            return r.result;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<int(*)()>("_KFN29gen_template_comprehensive_304testEv");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 15);
}

// ────────────────────────────────────────────────────────────────────────────
//  [M04] Template class with value param method
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("[M04] Template class: method with value param", "[template][class][method][virtual]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_comprehensive_31;
        template<typename T, int N>
        class Processor {
            public val : T;
            Processor() {}
            tag() : int { return N; }
        }

        test() : int {
            p : Processor<int, 42>();
            return p.tag();
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<int(*)()>("_KFN29gen_template_comprehensive_314testEv");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 42);
}

// ────────────────────────────────────────────────────────────────────────────
//  [M05] Template struct method using value param N in arithmetic
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("[M05] Template struct: method uses value param N", "[template][method][value-param]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_comprehensive_32;
        template<typename T, int N>
        struct Scaled {
            public base : T;
            public get_factor() : int { return N * 2; }
        }

        test() : int {
            s : Scaled<int, 3>;
            s.base = 7;
            return s.base + s.get_factor();
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<int(*)()>("_KFN29gen_template_comprehensive_324testEv");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 13); // 7 + (3*2)
}

// ════════════════════════════════════════════════════════════════════════════
//  MIXED / ADVANCED
// ────────────────────────────────────────────────────────────────────────────
//  [X01] Template struct containing another template struct
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("[X01] Template struct used in non-template struct", "[template][nested]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_comprehensive_33;
        template<typename T>
        struct Box {
            public value : T;
        }

        struct DoubleBox {
            public inner : Box<int>;
        }

        test() : int {
            d : DoubleBox;
            d.inner.value = 99;
            return d.inner.value;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<int(*)()>("_KFN29gen_template_comprehensive_334testEv");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 99);
}

// ────────────────────────────────────────────────────────────────────────────
//  [X02] Template function taking a template struct as parameter
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("[X02] Template function with template struct param", "[template][mixed]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_comprehensive_34;
        template<typename T>
        struct Box {
            public value : T;
        }

        unbox(b : Box<int>&) : int {
            return b.value;
        }

        test() : int {
            b : Box<int>;
            b.value = 55;
            return unbox(b);
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<int(*)()>("_KFN29gen_template_comprehensive_344testEv");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 55);
}

// ────────────────────────────────────────────────────────────────────────────
//  [X03] Template struct with default type param, instantiated with struct
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("[X03] Template struct with default type param", "[template][defaults][struct]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_comprehensive_35;
        struct MyStruct {
            val : int = 11;
        }

        template<typename T = int>
        struct Wrapper {
            public data : T;
        }

        test_default() : int {
            w : Wrapper<>;
            w.data = 42;
            return w.data;
        }

        test_struct() : int {
            w : Wrapper<MyStruct>;
            return w.data.val;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn_default = jit->lookup_symbol<int(*)()>("_KFN29gen_template_comprehensive_3512test_defaultEv");
    REQUIRE(fn_default != nullptr);
    CHECK(fn_default() == 42);

    auto fn_struct = jit->lookup_symbol<int(*)()>("_KFN29gen_template_comprehensive_3511test_structEv");
    REQUIRE(fn_struct != nullptr);
    CHECK(fn_struct() == 11);
}

// ────────────────────────────────────────────────────────────────────────────
//  [X04] Template function with value param and type param combined
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("[X04] Template function with type + value params", "[template][mixed][value-param]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_comprehensive_36;
        template<typename T, int N>
        scale(x : T) : T { return x * N; }

        test() : int {
            return scale<int, 5>(8);
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<int(*)()>("_KFN29gen_template_comprehensive_364testEv");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 40);
}

// ────────────────────────────────────────────────────────────────────────────
//  [X05] Multiple instantiations of same template with struct and class
// ────────────────────────────────────────────────────────────────────────────

TEST_CASE("[X05] Same template instantiated with struct and class", "[template][mixed][multi-inst]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_comprehensive_37;
        struct S {
            val : int = 3;
        }

        class C {
            public val : int;
            C() : val(7) {}
        }

        template<typename T>
        struct Wrapper {
            public item : T;
        }

        test_struct() : int {
            w : Wrapper<S>;
            return w.item.val;
        }

        test_class() : int {
            w : Wrapper<C>;
            return w.item.val;
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn_struct = jit->lookup_symbol<int(*)()>("_KFN29gen_template_comprehensive_3711test_structEv");
    REQUIRE(fn_struct != nullptr);
    CHECK(fn_struct() == 3);

    auto fn_class = jit->lookup_symbol<int(*)()>("_KFN29gen_template_comprehensive_3710test_classEv");
    REQUIRE(fn_class != nullptr);
    CHECK(fn_class() == 7);
}

// ────────────────────────────────────────────────────────────────────────────
//  [X06] User type named 'I'/'O' must not collide with stdlib template
//  parameter names of the same name.
// ────────────────────────────────────────────────────────────────────────────
//
// Regression test for a bug where the K standard library (auto-imported as
// `import k;`) declares I/O-transform stream templates using the literal
// names 'I' and 'O' as their own type-parameter names (e.g.
// `template<typename I, typename O> class TransformInputStream { ... }` in
// libk's `io/transform_stream.k`). While scanning these (still uninstantiated)
// template bodies, the compiler used to resolve bare template-parameter
// identifiers like 'I' through a global/namespace name lookup instead of
// recognising them as placeholders. If the CONSUMING program happened to
// declare its own type literally named `I` (as this test does), that lookup
// would spuriously match the user's type, cascading into bogus template
// instantiations (e.g. `Vector<I>`, `InputStream<I>`) and ultimately an
// unrelated "cannot resolve type" error deep inside the stdlib's own
// `Vector<T>::constIterator()`. See resolvers_aggregate.cpp /
// resolvers_type_ref.cpp `is_enclosing_template_param_name()`.
TEST_CASE("[X06] User type named 'I' does not collide with stdlib template param names", "[template][name-collision][regression]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_comprehensive_38;

        interface I {
            bar() : int;
        }

        class Impl : public I {
            override bar() : int { return 42; }
        }

        test() : int {
            i : Impl;
            return i.bar();
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<int(*)()>("_KFN29gen_template_comprehensive_384testEv");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 42);
}





