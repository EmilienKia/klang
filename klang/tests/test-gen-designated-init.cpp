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

using namespace k::parse;
using namespace k::parse::ast;

// ============================================================
// Phase 1: Parser tests for designated initializer lists
// ============================================================

TEST_CASE("Parse designated init — simple assignment form", "[parser][designated-init]") {
    test_logger log;
    k::source src{"s : S { .x = 1, .y = 2 };"};
    k::parse::parser parser(log, src);
    auto var = parser.parse_variable_decl();
    REQUIRE(var);
    REQUIRE(var->name.content == "s");
    REQUIRE(var->is_brace_init == true);
    REQUIRE(var->init != nullptr);

    auto init = std::dynamic_pointer_cast<brace_init_list>(var->init);
    REQUIRE(init);
    REQUIRE(init->is_designated == true);
    REQUIRE(init->elements.size() == 2);

    auto d0 = std::dynamic_pointer_cast<designated_init_element>(init->elements[0]);
    REQUIRE(d0);
    CHECK(d0->member_name.content == "x");
    CHECK(d0->is_call_form == false);
    CHECK(d0->qualifier.empty());
    CHECK(d0->value != nullptr);

    auto d1 = std::dynamic_pointer_cast<designated_init_element>(init->elements[1]);
    REQUIRE(d1);
    CHECK(d1->member_name.content == "y");
    CHECK(d1->is_call_form == false);
    CHECK(d1->value != nullptr);
}

TEST_CASE("Parse designated init — constructor call form", "[parser][designated-init]") {
    test_logger log;
    k::source src{"s : S { .x(1, 2), .y(3) };"};
    k::parse::parser parser(log, src);
    auto var = parser.parse_variable_decl();
    REQUIRE(var);
    REQUIRE(var->is_brace_init == true);

    auto init = std::dynamic_pointer_cast<brace_init_list>(var->init);
    REQUIRE(init);
    REQUIRE(init->is_designated == true);
    REQUIRE(init->elements.size() == 2);

    auto d0 = std::dynamic_pointer_cast<designated_init_element>(init->elements[0]);
    REQUIRE(d0);
    CHECK(d0->member_name.content == "x");
    CHECK(d0->is_call_form == true);
    CHECK(d0->args.size() == 2);

    auto d1 = std::dynamic_pointer_cast<designated_init_element>(init->elements[1]);
    REQUIRE(d1);
    CHECK(d1->member_name.content == "y");
    CHECK(d1->is_call_form == true);
    CHECK(d1->args.size() == 1);
}

TEST_CASE("Parse designated init — qualified member name", "[parser][designated-init]") {
    test_logger log;
    k::source src{"s : S { .Base::x = 42 };"};
    k::parse::parser parser(log, src);
    auto var = parser.parse_variable_decl();
    REQUIRE(var);
    REQUIRE(var->is_brace_init == true);

    auto init = std::dynamic_pointer_cast<brace_init_list>(var->init);
    REQUIRE(init);
    REQUIRE(init->is_designated == true);
    REQUIRE(init->elements.size() == 1);

    auto d0 = std::dynamic_pointer_cast<designated_init_element>(init->elements[0]);
    REQUIRE(d0);
    CHECK(d0->member_name.content == "x");
    REQUIRE(d0->qualifier.size() == 1);
    CHECK(std::string{d0->qualifier[0].content} == "Base");
    CHECK(d0->is_call_form == false);
    CHECK(d0->value != nullptr);
    CHECK(d0->qualified_member_name() == "Base::x");
}

TEST_CASE("Parse designated init — mixed positional/designated is rejected", "[parser][designated-init]") {
    test_logger log;
    k::source src{"s : S { 1, .y = 2 };"};
    k::parse::parser parser(log, src);
    REQUIRE_THROWS(parser.parse_variable_decl());
}

TEST_CASE("Parse designated init — mixed designated/positional is rejected", "[parser][designated-init]") {
    test_logger log;
    k::source src{"s : S { .x = 1, 2 };"};
    k::parse::parser parser(log, src);
    REQUIRE_THROWS(parser.parse_variable_decl());
}

TEST_CASE("Parse designated init — empty call form", "[parser][designated-init]") {
    test_logger log;
    k::source src{"s : S { .x() };"};
    k::parse::parser parser(log, src);
    auto var = parser.parse_variable_decl();
    REQUIRE(var);

    auto init = std::dynamic_pointer_cast<brace_init_list>(var->init);
    REQUIRE(init);
    REQUIRE(init->is_designated == true);
    REQUIRE(init->elements.size() == 1);

    auto d0 = std::dynamic_pointer_cast<designated_init_element>(init->elements[0]);
    REQUIRE(d0);
    CHECK(d0->member_name.content == "x");
    CHECK(d0->is_call_form == true);
    CHECK(d0->args.empty());
}

// ============================================================
// Phase 2: End-to-end (JIT) tests for struct designated init
// ============================================================

TEST_CASE("Designated init — simple struct with primitives", "[gen][designated-init]") {
    auto jit = gen_jit(R"(
        module gen_designated_init_01;
        struct Point {
            x : int;
            y : int;
        }
        get_x() : int {
            p : Point { .x = 10, .y = 20 };
            return p.x;
        }
        get_y() : int {
            p : Point { .x = 10, .y = 20 };
            return p.y;
        }
    )");
    REQUIRE(jit);
    auto get_x = jit->lookup_symbol<int(*)()>("get_x");
    auto get_y = jit->lookup_symbol<int(*)()>("get_y");
    REQUIRE(get_x);
    REQUIRE(get_y);
    CHECK(get_x() == 10);
    CHECK(get_y() == 20);
}

TEST_CASE("Designated init — partial init, remaining defaults to zero", "[gen][designated-init]") {
    auto jit = gen_jit(R"(
        module gen_designated_init_02;
        struct Trio {
            a : int;
            b : int;
            c : int;
        }
        get_a() : int {
            t : Trio { .b = 42 };
            return t.a;
        }
        get_b() : int {
            t : Trio { .b = 42 };
            return t.b;
        }
        get_c() : int {
            t : Trio { .b = 42 };
            return t.c;
        }
    )");
    REQUIRE(jit);
    auto get_a = jit->lookup_symbol<int(*)()>("get_a");
    auto get_b = jit->lookup_symbol<int(*)()>("get_b");
    auto get_c = jit->lookup_symbol<int(*)()>("get_c");
    REQUIRE(get_a);
    REQUIRE(get_b);
    REQUIRE(get_c);
    CHECK(get_a() == 0);
    CHECK(get_b() == 42);
    CHECK(get_c() == 0);
}

TEST_CASE("Designated init — order independent", "[gen][designated-init]") {
    auto jit = gen_jit(R"(
        module gen_designated_init_03;
        struct Point {
            x : int;
            y : int;
        }
        get_x() : int {
            p : Point { .y = 20, .x = 10 };
            return p.x;
        }
        get_y() : int {
            p : Point { .y = 20, .x = 10 };
            return p.y;
        }
    )");
    REQUIRE(jit);
    auto get_x = jit->lookup_symbol<int(*)()>("get_x");
    auto get_y = jit->lookup_symbol<int(*)()>("get_y");
    REQUIRE(get_x);
    REQUIRE(get_y);
    CHECK(get_x() == 10);
    CHECK(get_y() == 20);
}

TEST_CASE("Designated init — error on non-struct type", "[gen][designated-init]") {
    REQUIRE_THROWS(gen_jit_throws(R"(
        module gen_designated_init_04;
        get() : int {
            x : int { .a = 1 };
            return x;
        }
    )"));
}

TEST_CASE("Designated init — error on unknown member", "[gen][designated-init]") {
    REQUIRE_THROWS(gen_jit_throws(R"(
        module gen_designated_init_05;
        struct S {
            x : int;
        }
        get() : int {
            s : S { .z = 1 };
            return s.x;
        }
    )"));
}

TEST_CASE("Designated init — error on duplicate member", "[gen][designated-init]") {
    REQUIRE_THROWS(gen_jit_throws(R"(
        module gen_designated_init_06;
        struct S {
            x : int;
        }
        get() : int {
            s : S { .x = 1, .x = 2 };
            return s.x;
        }
    )"));
}

TEST_CASE("Designated init — error on private member", "[gen][designated-init]") {
    REQUIRE_THROWS(gen_jit_throws(R"(
        module gen_designated_init_07;
        struct S {
            private:
            x : int;
            public:
            y : int;
        }
        get() : int {
            s : S { .x = 1 };
            return s.y;
        }
    )"));
}

TEST_CASE("Designated init — error on protected member from outside", "[gen][designated-init]") {
    REQUIRE_THROWS(gen_jit_throws(R"(
        module gen_designated_init_08;
        struct S {
            protected:
            x : int;
            public:
            y : int;
        }
        get() : int {
            s : S { .x = 1 };
            return s.y;
        }
    )"));
}

TEST_CASE("Designated init — mixed assignment and constructor forms", "[gen][designated-init]") {
    auto jit = gen_jit(R"(
        module gen_designated_init_09;
        struct Pair {
            x : int;
            y : int;
        }
        get_x() : int {
            p : Pair { .x = 10, .y(20) };
            return p.x;
        }
        get_y() : int {
            p : Pair { .x = 10, .y(20) };
            return p.y;
        }
    )");
    REQUIRE(jit);
    auto get_x = jit->lookup_symbol<int(*)()>("get_x");
    auto get_y = jit->lookup_symbol<int(*)()>("get_y");
    REQUIRE(get_x);
    REQUIRE(get_y);
    CHECK(get_x() == 10);
    CHECK(get_y() == 20);
}

TEST_CASE("Designated init — constructor form for primitive member", "[gen][designated-init]") {
    auto jit = gen_jit(R"(
        module gen_designated_init_10;
        struct S {
            x : int;
        }
        get() : int {
            s : S { .x(42) };
            return s.x;
        }
    )");
    REQUIRE(jit);
    auto get = jit->lookup_symbol<int(*)()>("get");
    REQUIRE(get);
    CHECK(get() == 42);
}

TEST_CASE("Designated init — float members", "[gen][designated-init]") {
    auto jit = gen_jit(R"(
        module gen_designated_init_11;
        struct Vec {
            x : float;
            y : float;
        }
        get_x() : float {
            v : Vec { .x = 1.5f, .y = 2.5f };
            return v.x;
        }
        get_y() : float {
            v : Vec { .x = 1.5f, .y = 2.5f };
            return v.y;
        }
    )");
    REQUIRE(jit);
    auto get_x = jit->lookup_symbol<float(*)()>("get_x");
    auto get_y = jit->lookup_symbol<float(*)()>("get_y");
    REQUIRE(get_x);
    REQUIRE(get_y);
    CHECK(get_x() == Catch::Approx(1.5f));
    CHECK(get_y() == Catch::Approx(2.5f));
}

TEST_CASE("Designated init — inherited member from base struct", "[gen][designated-init]") {
    auto jit = gen_jit(R"(
        module gen_designated_init_12;
        struct Base {
            x : int;
        }
        struct Derived : public Base {
            y : int;
        }
        get_x() : int {
            d : Derived { .x = 10, .y = 20 };
            return d.x;
        }
        get_y() : int {
            d : Derived { .x = 10, .y = 20 };
            return d.y;
        }
    )");
    REQUIRE(jit);
    auto get_x = jit->lookup_symbol<int(*)()>("get_x");
    auto get_y = jit->lookup_symbol<int(*)()>("get_y");
    REQUIRE(get_x);
    REQUIRE(get_y);
    CHECK(get_x() == 10);
    CHECK(get_y() == 20);
}

TEST_CASE("Designated init — qualified member disambiguation", "[gen][designated-init]") {
    auto jit = gen_jit(R"(
        module gen_designated_init_13;
        struct A {
            v : int;
        }
        struct B {
            v : int;
        }
        struct D : public A, public B {
            w : int;
        }
        get_a() : int {
            d : D { .A::v = 10, .B::v = 20, .w = 30 };
            return d.A::v;
        }
        get_b() : int {
            d : D { .A::v = 10, .B::v = 20, .w = 30 };
            return d.B::v;
        }
        get_w() : int {
            d : D { .A::v = 10, .B::v = 20, .w = 30 };
            return d.w;
        }
    )");
    REQUIRE(jit);
    auto get_a = jit->lookup_symbol<int(*)()>("get_a");
    auto get_b = jit->lookup_symbol<int(*)()>("get_b");
    auto get_w = jit->lookup_symbol<int(*)()>("get_w");
    REQUIRE(get_a);
    REQUIRE(get_b);
    REQUIRE(get_w);
    CHECK(get_a() == 10);
    CHECK(get_b() == 20);
    CHECK(get_w() == 30);
}

TEST_CASE("Designated init — single member struct", "[gen][designated-init]") {
    auto jit = gen_jit(R"(
        module gen_designated_init_14;
        struct Wrap {
            val : int;
        }
        get() : int {
            w : Wrap { .val = 99 };
            return w.val;
        }
    )");
    REQUIRE(jit);
    auto get = jit->lookup_symbol<int(*)()>("get");
    REQUIRE(get);
    CHECK(get() == 99);
}

TEST_CASE("Designated init — all members omitted uses defaults", "[gen][designated-init]") {
    auto jit = gen_jit(R"(
        module gen_designated_init_15;
        struct Pair {
            x : int;
            y : int;
        }
        get_x() : int {
            p : Pair {};
            return p.x;
        }
        get_y() : int {
            p : Pair {};
            return p.y;
        }
    )");
    REQUIRE(jit);
    auto get_x = jit->lookup_symbol<int(*)()>("get_x");
    auto get_y = jit->lookup_symbol<int(*)()>("get_y");
    REQUIRE(get_x);
    REQUIRE(get_y);
    CHECK(get_x() == 0);
    CHECK(get_y() == 0);
}

TEST_CASE("Designated init — error on ambiguous inherited member without qualifier", "[gen][designated-init]") {
    REQUIRE_THROWS(gen_jit_throws(R"(
        module gen_designated_init_16;
        struct A {
            v : int;
        }
        struct B {
            v : int;
        }
        struct D : public A, public B {
        }
        get() : int {
            d : D { .v = 1 };
            return 0;
        }
    )"));
}

// ============================================================
// Phase 3: Default construction of non-designated members
// ============================================================

TEST_CASE("Designated init — non-designated struct member gets default-constructed", "[gen][designated-init]") {
    auto jit = gen_jit(R"(
        module gen_designated_init_17;
        struct Inner {
            val : int;
            Inner() { val = 42; }
        }
        struct Outer {
            inner : Inner;
            x : int;
        }
        get_inner_val() : int {
            o : Outer { .x = 10 };
            return o.inner.val;
        }
        get_x() : int {
            o : Outer { .x = 10 };
            return o.x;
        }
    )");
    REQUIRE(jit);
    auto get_inner_val = jit->lookup_symbol<int(*)()>("get_inner_val");
    auto get_x = jit->lookup_symbol<int(*)()>("get_x");
    REQUIRE(get_inner_val);
    REQUIRE(get_x);
    CHECK(get_inner_val() == 42);  // default ctor called for non-designated member
    CHECK(get_x() == 10);
}

TEST_CASE("Designated init — private struct member gets default-constructed", "[gen][designated-init]") {
    auto jit = gen_jit(R"(
        module gen_designated_init_18;
        struct Counter {
            count : int;
            Counter() { count = 99; }
        }
        struct S {
            private:
            c : Counter;
            public:
            x : int;
            get_count() : int { return c.count; }
        }
        get() : int {
            s : S { .x = 10 };
            return s.get_count();
        }
    )");
    REQUIRE(jit);
    auto get = jit->lookup_symbol<int(*)()>("get");
    REQUIRE(get);
    CHECK(get() == 99);  // private member's default ctor was called
}

TEST_CASE("Designated init — protected struct member gets default-constructed", "[gen][designated-init]") {
    auto jit = gen_jit(R"(
        module gen_designated_init_19;
        struct Counter {
            count : int;
            Counter() { count = 77; }
        }
        struct S {
            protected:
            c : Counter;
            public:
            x : int;
            get_count() : int { return c.count; }
        }
        get() : int {
            s : S { .x = 10 };
            return s.get_count();
        }
    )");
    REQUIRE(jit);
    auto get = jit->lookup_symbol<int(*)()>("get");
    REQUIRE(get);
    CHECK(get() == 77);  // protected member's default ctor was called
}

TEST_CASE("Designated init — all struct-typed members default-constructed when none designated", "[gen][designated-init]") {
    auto jit = gen_jit(R"(
        module gen_designated_init_20;
        struct A {
            val : int;
            A() { val = 11; }
        }
        struct B {
            val : int;
            B() { val = 22; }
        }
        struct S {
            a : A;
            b : B;
        }
        get_a() : int {
            s : S {};
            return s.a.val;
        }
        get_b() : int {
            s : S {};
            return s.b.val;
        }
    )");
    REQUIRE(jit);
    auto get_a = jit->lookup_symbol<int(*)()>("get_a");
    auto get_b = jit->lookup_symbol<int(*)()>("get_b");
    REQUIRE(get_a);
    REQUIRE(get_b);
    CHECK(get_a() == 11);  // A's default ctor called
    CHECK(get_b() == 22);  // B's default ctor called
}

TEST_CASE("Designated init — designated member constructed, non-designated gets default ctor", "[gen][designated-init]") {
    auto jit = gen_jit(R"(
        module gen_designated_init_21;
        struct A {
            val : int;
            A() { val = 11; }
            A(v : int) { val = v; }
        }
        struct B {
            val : int;
            B() { val = 22; }
        }
        struct S {
            a : A;
            b : B;
        }
        get_a() : int {
            s : S { .a(55) };
            return s.a.val;
        }
        get_b() : int {
            s : S { .a(55) };
            return s.b.val;
        }
    )");
    REQUIRE(jit);
    auto get_a = jit->lookup_symbol<int(*)()>("get_a");
    auto get_b = jit->lookup_symbol<int(*)()>("get_b");
    REQUIRE(get_a);
    REQUIRE(get_b);
    CHECK(get_a() == 55);  // explicitly designated with ctor form
    CHECK(get_b() == 22);  // non-designated → default ctor called
}

TEST_CASE("Designated init — private and public struct members, only public designated", "[gen][designated-init]") {
    auto jit = gen_jit(R"(
        module gen_designated_init_22;
        struct Inner {
            val : int;
            Inner() { val = 33; }
            Inner(v : int) { val = v; }
        }
        struct S {
            private:
            priv : Inner;
            public:
            pub : Inner;
            get_priv() : int { return priv.val; }
        }
        get_priv() : int {
            s : S { .pub(100) };
            return s.get_priv();
        }
        get_pub() : int {
            s : S { .pub(100) };
            return s.pub.val;
        }
    )");
    REQUIRE(jit);
    auto get_priv = jit->lookup_symbol<int(*)()>("get_priv");
    auto get_pub = jit->lookup_symbol<int(*)()>("get_pub");
    REQUIRE(get_priv);
    REQUIRE(get_pub);
    CHECK(get_priv() == 33);   // private member → default ctor called
    CHECK(get_pub() == 100);   // public member → explicitly designated
}

TEST_CASE("Parse designated init — mixed forms in same list", "[parser][designated-init]") {
    test_logger log;
    k::source src{"s : S { .x = 1, .y(2, 3) };"};
    k::parse::parser parser(log, src);
    auto var = parser.parse_variable_decl();
    REQUIRE(var);
    REQUIRE(var->is_brace_init == true);

    auto init = std::dynamic_pointer_cast<brace_init_list>(var->init);
    REQUIRE(init);
    REQUIRE(init->is_designated == true);
    REQUIRE(init->elements.size() == 2);

    auto d0 = std::dynamic_pointer_cast<designated_init_element>(init->elements[0]);
    REQUIRE(d0);
    CHECK(d0->member_name.content == "x");
    CHECK(d0->is_call_form == false);

    auto d1 = std::dynamic_pointer_cast<designated_init_element>(init->elements[1]);
    REQUIRE(d1);
    CHECK(d1->member_name.content == "y");
    CHECK(d1->is_call_form == true);
    CHECK(d1->args.size() == 2);
}

TEST_CASE("Parse designated init — multi-level qualified name", "[parser][designated-init]") {
    test_logger log;
    k::source src{"s : S { .A::B::x = 1 };"};
    k::parse::parser parser(log, src);
    auto var = parser.parse_variable_decl();
    REQUIRE(var);

    auto init = std::dynamic_pointer_cast<brace_init_list>(var->init);
    REQUIRE(init);
    REQUIRE(init->is_designated == true);

    auto d0 = std::dynamic_pointer_cast<designated_init_element>(init->elements[0]);
    REQUIRE(d0);
    CHECK(d0->member_name.content == "x");
    REQUIRE(d0->qualifier.size() == 2);
    CHECK(std::string{d0->qualifier[0].content} == "A");
    CHECK(std::string{d0->qualifier[1].content} == "B");
    CHECK(d0->qualified_member_name() == "A::B::x");
}

// ============================================================
// Phase 4: Multi-level inheritance and nested struct tests
// ============================================================

TEST_CASE("Designated init — multi-level inheritance (grandparent member)", "[gen][designated-init]") {
    auto jit = gen_jit(R"(
        module gen_designated_init_23;
        struct A {
            x : int;
        }
        struct B : public A {
            y : int;
        }
        struct C : public B {
            z : int;
        }
        get_x() : int {
            c : C { .x = 10, .y = 20, .z = 30 };
            return c.x;
        }
        get_y() : int {
            c : C { .x = 10, .y = 20, .z = 30 };
            return c.y;
        }
        get_z() : int {
            c : C { .x = 10, .y = 20, .z = 30 };
            return c.z;
        }
    )");
    REQUIRE(jit);
    auto get_x = jit->lookup_symbol<int(*)()>("get_x");
    auto get_y = jit->lookup_symbol<int(*)()>("get_y");
    auto get_z = jit->lookup_symbol<int(*)()>("get_z");
    REQUIRE(get_x);
    REQUIRE(get_y);
    REQUIRE(get_z);
    CHECK(get_x() == 10);
    CHECK(get_y() == 20);
    CHECK(get_z() == 30);
}

TEST_CASE("Designated init — multi-level inheritance, only grandparent member designated", "[gen][designated-init]") {
    auto jit = gen_jit(R"(
        module gen_designated_init_24;
        struct A {
            x : int;
        }
        struct B : public A {
            y : int;
        }
        struct C : public B {
            z : int;
        }
        get_x() : int {
            c : C { .x = 42 };
            return c.x;
        }
        get_y() : int {
            c : C { .x = 42 };
            return c.y;
        }
        get_z() : int {
            c : C { .x = 42 };
            return c.z;
        }
    )");
    REQUIRE(jit);
    auto get_x = jit->lookup_symbol<int(*)()>("get_x");
    auto get_y = jit->lookup_symbol<int(*)()>("get_y");
    auto get_z = jit->lookup_symbol<int(*)()>("get_z");
    REQUIRE(get_x);
    REQUIRE(get_y);
    REQUIRE(get_z);
    CHECK(get_x() == 42);
    CHECK(get_y() == 0);  // non-designated → zero-init
    CHECK(get_z() == 0);  // non-designated → zero-init
}

TEST_CASE("Designated init — nested struct designated init", "[gen][designated-init]") {
    auto jit = gen_jit(R"(
        module gen_designated_init_25;
        struct Inner {
            a : int;
            b : int;
        }
        struct Outer {
            inner : Inner;
            c : int;
        }
        get_a() : int {
            o : Outer { .inner = { .a = 10, .b = 20 }, .c = 30 };
            return o.inner.a;
        }
        get_b() : int {
            o : Outer { .inner = { .a = 10, .b = 20 }, .c = 30 };
            return o.inner.b;
        }
        get_c() : int {
            o : Outer { .inner = { .a = 10, .b = 20 }, .c = 30 };
            return o.c;
        }
    )");
    REQUIRE(jit);
    auto get_a = jit->lookup_symbol<int(*)()>("get_a");
    auto get_b = jit->lookup_symbol<int(*)()>("get_b");
    auto get_c = jit->lookup_symbol<int(*)()>("get_c");
    REQUIRE(get_a);
    REQUIRE(get_b);
    REQUIRE(get_c);
    CHECK(get_a() == 10);
    CHECK(get_b() == 20);
    CHECK(get_c() == 30);
}

TEST_CASE("Designated init — expression values", "[gen][designated-init]") {
    auto jit = gen_jit(R"(
        module gen_designated_init_26;
        struct Point {
            x : int;
            y : int;
        }
        make(a : int, b : int) : int {
            p : Point { .x = a + 1, .y = b * 2 };
            return p.x + p.y;
        }
    )");
    REQUIRE(jit);
    auto make = jit->lookup_symbol<int(*)(int, int)>("make");
    REQUIRE(make);
    CHECK(make(5, 3) == 12);  // (5+1) + (3*2) = 6 + 6 = 12
}

TEST_CASE("Designated init — qualified constructor form", "[gen][designated-init]") {
    auto jit = gen_jit(R"(
        module gen_designated_init_27;
        struct A {
            v : int;
        }
        struct B {
            v : int;
        }
        struct D : public A, public B {
            w : int;
        }
        get_a() : int {
            d : D { .A::v(10), .B::v(20), .w = 30 };
            return d.A::v;
        }
        get_b() : int {
            d : D { .A::v(10), .B::v(20), .w = 30 };
            return d.B::v;
        }
        get_w() : int {
            d : D { .A::v(10), .B::v(20), .w = 30 };
            return d.w;
        }
    )");
    REQUIRE(jit);
    auto get_a = jit->lookup_symbol<int(*)()>("get_a");
    auto get_b = jit->lookup_symbol<int(*)()>("get_b");
    auto get_w = jit->lookup_symbol<int(*)()>("get_w");
    REQUIRE(get_a);
    REQUIRE(get_b);
    REQUIRE(get_w);
    CHECK(get_a() == 10);
    CHECK(get_b() == 20);
    CHECK(get_w() == 30);
}

TEST_CASE("Designated init — inherited struct member with base default ctor", "[gen][designated-init]") {
    auto jit = gen_jit(R"(
        module gen_designated_init_28;
        struct Counter {
            val : int;
            Counter() { val = 55; }
        }
        struct Base {
            c : Counter;
            x : int;
        }
        struct Derived : public Base {
            y : int;
        }
        get_c() : int {
            d : Derived { .y = 10 };
            return d.c.val;
        }
        get_x() : int {
            d : Derived { .y = 10 };
            return d.x;
        }
        get_y() : int {
            d : Derived { .y = 10 };
            return d.y;
        }
    )");
    REQUIRE(jit);
    auto get_c = jit->lookup_symbol<int(*)()>("get_c");
    auto get_x = jit->lookup_symbol<int(*)()>("get_x");
    auto get_y = jit->lookup_symbol<int(*)()>("get_y");
    REQUIRE(get_c);
    REQUIRE(get_x);
    REQUIRE(get_y);
    CHECK(get_c() == 55);  // inherited Counter member default-constructed
    CHECK(get_x() == 0);   // inherited int member zero-init
    CHECK(get_y() == 10);  // designated
}

TEST_CASE("Designated init — inherited member partially designated, rest defaults", "[gen][designated-init]") {
    auto jit = gen_jit(R"(
        module gen_designated_init_29;
        struct Base {
            a : int;
            b : int;
            c : int;
        }
        struct Derived : public Base {
            d : int;
        }
        get_a() : int {
            x : Derived { .b = 42, .d = 99 };
            return x.a;
        }
        get_b() : int {
            x : Derived { .b = 42, .d = 99 };
            return x.b;
        }
        get_c() : int {
            x : Derived { .b = 42, .d = 99 };
            return x.c;
        }
        get_d() : int {
            x : Derived { .b = 42, .d = 99 };
            return x.d;
        }
    )");
    REQUIRE(jit);
    auto get_a = jit->lookup_symbol<int(*)()>("get_a");
    auto get_b = jit->lookup_symbol<int(*)()>("get_b");
    auto get_c = jit->lookup_symbol<int(*)()>("get_c");
    auto get_d = jit->lookup_symbol<int(*)()>("get_d");
    REQUIRE(get_a);
    REQUIRE(get_b);
    REQUIRE(get_c);
    REQUIRE(get_d);
    CHECK(get_a() == 0);   // non-designated inherited → zero-init
    CHECK(get_b() == 42);  // designated inherited
    CHECK(get_c() == 0);   // non-designated inherited → zero-init
    CHECK(get_d() == 99);  // designated direct
}

// ============================================================
// Phase 5: Cross-library import tests for designated init
// ============================================================

TEST_CASE("Designated init — imported parent struct with private member",
          "[gen][designated-init][import]") {
    // Library defines a struct with a private member, a public member, and a getter.
    // Executable inherits from the imported struct and uses designated init on
    // the public inherited member and the derived member only.
    // The private member is an opaque block in the KDI: it should be zero-initialised
    // (the base constructor is NOT called by designated init).
    auto result = build_exec_with_lib(
        R"K(
            module gen_designated_init_30;
            struct Base {
            private:
                secret : int;
            public:
                pub_val : int;
                get_secret() : int { return this.secret; }
            }
        )K",
        R"K(
            module gen_designated_init_31;
            import gen_designated_init_30;
            struct Derived : public gen_designated_init_30::Base {
                extra : int;
            }
            main() : int {
                d : Derived { .pub_val = 10, .extra = 20 };
                // private member 'secret' is zero-initialised (base ctor not called)
                if (d.get_secret() != 0) { return 1; }
                if (d.pub_val != 10) { return 2; }
                if (d.extra != 20) { return 3; }
                return 42;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE(result.exit_code == 42);
}

TEST_CASE("Designated init — error: cannot designate private member of imported parent",
          "[gen][designated-init][import]") {
    // The private member 'secret' is exported as an opaque block in the KDI,
    // so it does not appear as a named member → the compiler should reject it
    // with a "no member" or "not accessible" error.
    REQUIRE_THROWS(build_exec_with_lib(
        R"K(
            module gen_designated_init_32;
            struct Base {
            private:
                secret : int;
            public:
                pub_val : int;
            }
        )K",
        R"K(
            module gen_designated_init_33;
            import gen_designated_init_32;
            struct Derived : public gen_designated_init_32::Base {
                extra : int;
            }
            main() : int {
                d : Derived { .secret = 1, .extra = 2 };
                return 0;
            }
        )K"));
}

TEST_CASE("Designated init — imported parent struct with private member and base default ctor",
          "[gen][designated-init][import]") {
    // Like the previous success test, but the base struct has an explicit
    // default constructor that sets the private member.  Since designated init
    // zero-inits and does NOT call the base ctor, get_secret() still returns 0.
    auto result = build_exec_with_lib(
        R"K(
            module gen_designated_init_34;
            struct Base {
            private:
                secret : int;
            public:
                Base() { secret = 99; }
                pub_val : int;
                get_secret() : int { return this.secret; }
            }
        )K",
        R"K(
            module gen_designated_init_35;
            import gen_designated_init_34;
            struct Derived : public gen_designated_init_34::Base {
                extra : int;
            }
            main() : int {
                d : Derived { .pub_val = 7, .extra = 3 };
                // base ctor is NOT called by designated init → secret is 0, not 99
                if (d.get_secret() != 0) { return 1; }
                if (d.pub_val != 7) { return 2; }
                if (d.extra != 3) { return 3; }
                return 42;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE(result.exit_code == 42);
}

