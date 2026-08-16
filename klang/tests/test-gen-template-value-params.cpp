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
 * Tests for Milestone 11: Template value parameter support.
 *
 * These tests verify that:
 *  [A] Template function with a value param: return the value param directly.
 *  [B] Template function with type + value params: mixed usage.
 *  [C] Template struct with a value param used in a method body.
 *  [D] Different value args produce distinct instantiations.
 *  [E] Same value args use cache (single instantiation).
 *  [F] Template function with value param default.
 *  [G] Value param in arithmetic expression within function body.
 *  [H] Template struct with value param used in member init.
 *  [I] Negative value template argument.
 *  [J] Name mangling encodes value args correctly (Li<n>E).
 *
 * Tests below cover the constexpr template value-argument evaluator
 * (enum constants, dependent value-param propagation, constexpr
 * arithmetic/logical/cast/ternary expressions -- see TODO.md gap on
 * "Value template arguments are limited to primitive types"):
 *  [S] Real negative literal value template argument (unary minus).
 *  [T] Parenthesized constexpr arithmetic expression as value arg.
 *  [U] Ternary (conditional) expression as value arg.
 *  [V] Enum constant as a value template argument.
 *  [W] Enum type mismatch between value arg and value param is rejected.
 *  [X] Dependent value parameter propagated to a nested template instantiation.
 *  [Y] Division-by-zero in a constexpr value arg is rejected (not constant).
 *  [Z] Non-constant (runtime-only local) value arg is rejected.
 *  [AB] Enum constant as a default value parameter.
 *  [AA] Runtime ternary expression regression (non-template context).
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"
#include "../src/model/template.hpp"
#include "../src/model/template_instantiator.hpp"

// ════════════════════════════════════════════════════════════════════════════
//  [A] Template function returning a value parameter directly
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[A] M11: template function returns value param",
          "[milestone11][template][value-param][jit]") {
    auto jit = gen_jit(R"SRC(
        module __m11_a__;
        template<int N>
        get_n() : int { return N; }

        test() : int {
            return get_n<42>();
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto test_fn = jit->lookup_symbol<int(*)()>("_KFN9__m11_a__4testEv");
    REQUIRE(test_fn != nullptr);
    CHECK(test_fn() == 42);
}

// ════════════════════════════════════════════════════════════════════════════
//  [B] Template function with type + value params
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[B] M11: template function with type and value params",
          "[milestone11][template][value-param][jit]") {
    auto jit = gen_jit(R"SRC(
        module __m11_b__;
        template<typename T, int N>
        add_n(x : T) : T { return x + N; }

        test() : int {
            return add_n<int, 10>(32);
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto test_fn = jit->lookup_symbol<int(*)()>("_KFN9__m11_b__4testEv");
    REQUIRE(test_fn != nullptr);
    CHECK(test_fn() == 42);
}

// ════════════════════════════════════════════════════════════════════════════
//  [C] Template struct with a value param used in a method body
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[C] M11: template struct with value param in method",
          "[milestone11][template][value-param][jit]") {
    auto jit = gen_jit(R"SRC(
        module __m11_c__;
        template<typename T, int N>
        struct Fixed {
            public val : T;
            public get_size() : int { return N; }
        }

        test() : int {
            f : Fixed<int, 5>;
            return f.get_size();
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto test_fn = jit->lookup_symbol<int(*)()>("_KFN9__m11_c__4testEv");
    REQUIRE(test_fn != nullptr);
    CHECK(test_fn() == 5);
}

// ════════════════════════════════════════════════════════════════════════════
//  [D] Different value args produce distinct instantiations
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[D] M11: different value args produce distinct instances",
          "[milestone11][template][value-param][jit]") {
    auto jit = gen_jit(R"SRC(
        module __m11_d__;
        template<int N>
        get_n() : int { return N; }

        test() : int {
            a : int = get_n<10>();
            b : int = get_n<20>();
            return a + b;
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto test_fn = jit->lookup_symbol<int(*)()>("_KFN9__m11_d__4testEv");
    REQUIRE(test_fn != nullptr);
    CHECK(test_fn() == 30);
}

// ════════════════════════════════════════════════════════════════════════════
//  [E] Same value args use cache (single instantiation)
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[E] M11: same value args use cache",
          "[milestone11][template][value-param][jit]") {
    auto jit = gen_jit(R"SRC(
        module __m11_e__;
        template<int N>
        get_n() : int { return N; }

        test() : int {
            a : int = get_n<7>();
            b : int = get_n<7>();
            return a + b;
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto test_fn = jit->lookup_symbol<int(*)()>("_KFN9__m11_e__4testEv");
    REQUIRE(test_fn != nullptr);
    CHECK(test_fn() == 14);
}

// ════════════════════════════════════════════════════════════════════════════
//  [F] Template function with value param default
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[F] M11: template function with value param default",
          "[milestone11][template][value-param][jit]") {
    auto jit = gen_jit(R"SRC(
        module __m11_f__;
        template<int N = 100>
        get_n() : int { return N; }

        test() : int {
            return get_n<>();
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto test_fn = jit->lookup_symbol<int(*)()>("_KFN9__m11_f__4testEv");
    REQUIRE(test_fn != nullptr);
    CHECK(test_fn() == 100);
}

// ════════════════════════════════════════════════════════════════════════════
//  [G] Value param in arithmetic expression
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[G] M11: value param in arithmetic expression",
          "[milestone11][template][value-param][jit]") {
    auto jit = gen_jit(R"SRC(
        module __m11_g__;
        template<int N>
        double_n() : int { return N * 2; }

        test() : int {
            return double_n<21>();
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto test_fn = jit->lookup_symbol<int(*)()>("_KFN9__m11_g__4testEv");
    REQUIRE(test_fn != nullptr);
    CHECK(test_fn() == 42);
}

// ════════════════════════════════════════════════════════════════════════════
//  [H] Template struct with value param used in member init
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[H] M11: template struct value param in member init via method",
          "[milestone11][template][value-param][jit]") {
    auto jit = gen_jit(R"SRC(
        module __m11_h__;
        template<typename T, int N>
        struct Container {
            public size : int;
            public get_capacity() : int { return N; }
        }

        test() : int {
            c : Container<int, 8>;
            return c.get_capacity();
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto test_fn = jit->lookup_symbol<int(*)()>("_KFN9__m11_h__4testEv");
    REQUIRE(test_fn != nullptr);
    CHECK(test_fn() == 8);
}

// ════════════════════════════════════════════════════════════════════════════
//  [I] Negative value template argument
// ════════════════════════════════════════════════════════════════════════════

// This test uses 0 as a safe boundary value.
// See [S] below for a real negative literal template argument, now
// supported via parse_template_arg_value_expr() + the constexpr evaluator.
TEST_CASE("[I] M11: zero value template argument",
          "[milestone11][template][value-param][jit]") {
    auto jit = gen_jit(R"SRC(
        module __m11_i__;
        template<int N>
        get_n() : int { return N; }

        test() : int {
            return get_n<0>();
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto test_fn = jit->lookup_symbol<int(*)()>("_KFN9__m11_i__4testEv");
    REQUIRE(test_fn != nullptr);
    CHECK(test_fn() == 0);
}

// ════════════════════════════════════════════════════════════════════════════
//  [J] Name mangling: value args encoded as Li<n>E
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[J] M11: mangling of value template argument",
          "[milestone11][template][value-param][mangling]") {
    auto comp = compile_model(R"SRC(
        module __m11_j__;
        template<int N>
        get_n() : int { return N; }

        test() : int {
            return get_n<42>();
        }
    )SRC");
    REQUIRE(comp != nullptr);

    // The instantiated function should be get_n__42
    auto root_ns = comp->get_unit()->get_root_namespace();
    auto fn = root_ns->get_function("get_n__42");
    REQUIRE(fn != nullptr);

    // Check that the mangled name contains the ILi42EE encoding
    auto mangled = fn->get_mangled_name();
    CHECK(mangled.find("ILi42EE") != std::string::npos);
}

// ════════════════════════════════════════════════════════════════════════════
//  [K] Template function with long value param
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[K] M11: template function with long value param",
          "[milestone11][template][value-param][jit]") {
    auto jit = gen_jit(R"SRC(
        module __m11_k__;
        template<long N>
        get_n() : long { return N; }

        test() : long {
            return get_n<100000L>();
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto test_fn = jit->lookup_symbol<long(*)()>("_KFN9__m11_k__4testEv");
    REQUIRE(test_fn != nullptr);
    CHECK(test_fn() == 100000L);
}

// ════════════════════════════════════════════════════════════════════════════
//  [L] Template function with short value param
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[L] M11: template function with short value param",
          "[milestone11][template][value-param][jit]") {
    auto jit = gen_jit(R"SRC(
        module __m11_l__;
        template<short N>
        get_n() : short { return N; }

        test() : short {
            return get_n<7S>();
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto test_fn = jit->lookup_symbol<short(*)()>("_KFN9__m11_l__4testEv");
    REQUIRE(test_fn != nullptr);
    CHECK(test_fn() == 7);
}

// ════════════════════════════════════════════════════════════════════════════
//  [M] Template function with bool value param
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[M] M11: template function with bool value param",
          "[milestone11][template][value-param][jit]") {
    auto jit = gen_jit(R"SRC(
        module __m11_m__;
        template<bool B>
        get_b() : bool { return B; }

        test() : int {
            a : bool = get_b<true>();
            b : bool = get_b<false>();
            r : int = 0;
            if(a) { ++r; }
            if(b) { r += 10; }
            return r;
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto test_fn = jit->lookup_symbol<int(*)()>("_KFN9__m11_m__4testEv");
    REQUIRE(test_fn != nullptr);
    CHECK(test_fn() == 1);
}

// ════════════════════════════════════════════════════════════════════════════
//  [N] Multiple value params with different primitive types
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[N] M11: multiple value params with different types",
          "[milestone11][template][value-param][jit]") {
    auto jit = gen_jit(R"SRC(
        module __m11_n__;
        template<int A, int B>
        sum() : int { return A + B; }

        test() : int {
            return sum<17, 25>();
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto test_fn = jit->lookup_symbol<int(*)()>("_KFN9__m11_n__4testEv");
    REQUIRE(test_fn != nullptr);
    CHECK(test_fn() == 42);
}

// ════════════════════════════════════════════════════════════════════════════
//  [O] Template function with value param and default (long)
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[O] M11: template function with long value param default",
          "[milestone11][template][value-param][jit]") {
    auto jit = gen_jit(R"SRC(
        module __m11_o__;
        template<long N = 999L>
        get_n() : long { return N; }

        test() : long {
            return get_n<>();
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto test_fn = jit->lookup_symbol<long(*)()>("_KFN9__m11_o__4testEv");
    REQUIRE(test_fn != nullptr);
    CHECK(test_fn() == 999L);
}

// ════════════════════════════════════════════════════════════════════════════
//  [P] Template struct with bool value param in method
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[P] M11: template struct with bool value param",
          "[milestone11][template][value-param][jit]") {
    auto jit = gen_jit(R"SRC(
        module __m11_p__;
        template<typename T, bool Signed>
        struct Config {
            public data : T;
            public is_signed() : bool { return Signed; }
        }

        test() : int {
            c : Config<int, true>;
            d : Config<int, false>;
            r : int = 0;
            if(c.is_signed()) { ++r; }
            if(d.is_signed()) { r += 10; }
            return r;
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto test_fn = jit->lookup_symbol<int(*)()>("_KFN9__m11_p__4testEv");
    REQUIRE(test_fn != nullptr);
    CHECK(test_fn() == 1);
}

// ════════════════════════════════════════════════════════════════════════════
//  [Q] Large int value param
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[Q] M11: large int value param",
          "[milestone11][template][value-param][jit]") {
    auto jit = gen_jit(R"SRC(
        module __m11_q__;
        template<int N>
        get_n() : int { return N; }

        test() : int {
            return get_n<2000000>();
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto test_fn = jit->lookup_symbol<int(*)()>("_KFN9__m11_q__4testEv");
    REQUIRE(test_fn != nullptr);
    CHECK(test_fn() == 2000000);
}

// ════════════════════════════════════════════════════════════════════════════
//  [R] Value param used in conditional expression
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[R] M11: value param used in conditional",
          "[milestone11][template][value-param][jit]") {
    auto jit = gen_jit(R"SRC(
        module __m11_r__;
        template<int N>
        check() : int {
            if(N > 10) { return 1; }
            return 0;
        }

        test() : int {
            a : int = check<20>();
            b : int = check<5>();
            return a * 10 + b;
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto test_fn = jit->lookup_symbol<int(*)()>("_KFN9__m11_r__4testEv");
    REQUIRE(test_fn != nullptr);
    CHECK(test_fn() == 10);
}

// ════════════════════════════════════════════════════════════════════════════
//  [S] Real negative literal value template argument (unary minus)
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[S] M11: negative literal value template argument",
          "[milestone11][template][value-param][constexpr][jit]") {
    auto jit = gen_jit(R"SRC(
        module __m11_s__;
        template<int N>
        get_n() : int { return N; }

        test() : int {
            return get_n<-5>();
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto test_fn = jit->lookup_symbol<int(*)()>("_KFN9__m11_s__4testEv");
    REQUIRE(test_fn != nullptr);
    CHECK(test_fn() == -5);
}

// ════════════════════════════════════════════════════════════════════════════
//  [T] Parenthesized constexpr arithmetic expression as value arg
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[T] M11: parenthesized constexpr arithmetic value arg",
          "[milestone11][template][value-param][constexpr][jit]") {
    auto jit = gen_jit(R"SRC(
        module __m11_t__;
        template<int N>
        get_n() : int { return N; }

        test() : int {
            return get_n<((2 + 3) * 4)>();
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto test_fn = jit->lookup_symbol<int(*)()>("_KFN9__m11_t__4testEv");
    REQUIRE(test_fn != nullptr);
    CHECK(test_fn() == 20);
}

// ════════════════════════════════════════════════════════════════════════════
//  [U] Ternary (conditional) expression as value arg
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[U] M11: ternary constexpr expression as value arg",
          "[milestone11][template][value-param][constexpr][jit]") {
    auto jit = gen_jit(R"SRC(
        module __m11_u__;
        template<int N>
        get_n() : int { return N; }

        test() : int {
            return get_n<(1 == 1 ? 10 : 20)>();
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto test_fn = jit->lookup_symbol<int(*)()>("_KFN9__m11_u__4testEv");
    REQUIRE(test_fn != nullptr);
    CHECK(test_fn() == 10);
}

// ════════════════════════════════════════════════════════════════════════════
//  [V] Enum constant as a value template argument
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[V] M11: enum constant as value template argument",
          "[milestone11][template][value-param][constexpr][enum][jit]") {
    auto jit = gen_jit(R"SRC(
        module __m11_v__;
        enum Color { Red; Green; Blue; }

        template<Color C>
        get_c() : int { return C; }

        test() : int {
            return get_c<Color::Blue>();
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto test_fn = jit->lookup_symbol<int(*)()>("_KFN9__m11_v__4testEv");
    REQUIRE(test_fn != nullptr);
    CHECK(test_fn() == 2);
}

// ════════════════════════════════════════════════════════════════════════════
//  [W] Enum-typed value param mismatch is rejected at resolution time
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[W] M11: enum type mismatch in value template argument rejected",
          "[milestone11][template][value-param][constexpr][enum][error]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __m11_w__;
        enum Color { Red; Green; Blue; }
        enum Fruit { Apple; Banana; }

        template<Color C>
        get_c() : int { return C; }

        test() : int {
            return get_c<Fruit::Apple>();
        }
    )SRC"), k::model::gen::resolution_error);
}

// ════════════════════════════════════════════════════════════════════════════
//  [X] Dependent value parameter propagated to a nested template instantiation
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[X] M11: dependent value param propagated to nested template",
          "[milestone11][template][value-param][constexpr][jit]") {
    auto jit = gen_jit(R"SRC(
        module __m11_x__;
        template<int N>
        struct Inner {
            getVal() : int { return N * 2; }
        }

        template<int N>
        struct Outer {
            inner: Inner<N>;
            getInnerVal() : int { return inner.getVal(); }
        }

        test() : int {
            o : Outer<5>;
            return o.getInnerVal();
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto test_fn = jit->lookup_symbol<int(*)()>("_KFN9__m11_x__4testEv");
    REQUIRE(test_fn != nullptr);
    CHECK(test_fn() == 10);
}

// ════════════════════════════════════════════════════════════════════════════
//  [Y] Division-by-zero in a constexpr value arg is rejected
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[Y] M11: division by zero in constexpr value arg rejected",
          "[milestone11][template][value-param][constexpr][error]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __m11_y__;
        template<int N>
        get_n() : int { return N; }

        test() : int {
            return get_n<(1 / 0)>();
        }
    )SRC"), k::model::gen::resolution_error);
}

// ════════════════════════════════════════════════════════════════════════════
//  [Z] Non-constant (runtime-only) expression as value arg is rejected
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[Z] M11: non-constant value arg referencing a local is rejected",
          "[milestone11][template][value-param][constexpr][error]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __m11_z__;
        template<int N>
        get_n() : int { return N; }

        test() : int {
            x : int = 5;
            return get_n<x>();
        }
    )SRC"), k::log::compiler_error);
}

// ════════════════════════════════════════════════════════════════════════════
//  [AB] Enum constant as a default value parameter
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[AB] M11: enum constant as default value template parameter",
          "[milestone11][template][value-param][constexpr][enum][default][jit]") {
    auto jit = gen_jit(R"SRC(
        module __m11_ab__;
        enum Color { Red; Green; Blue; }

        template<Color C = Color::Blue>
        get_c() : int { return C; }

        test() : int {
            return get_c<>() + get_c();
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto test_fn = jit->lookup_symbol<int(*)()>("_KFN10__m11_ab__4testEv");
    REQUIRE(test_fn != nullptr);
    CHECK(test_fn() == 4);
}

// ════════════════════════════════════════════════════════════════════════════
//  [AA] Standalone (non-template) ternary expression regression test
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[AA] M11: runtime ternary expression compiles and runs",
          "[milestone11][expression][ternary][jit]") {
    auto jit = gen_jit(R"SRC(
        module __m11_aa__;

        test() : int {
            return (1 == 1 ? 10 : 20) + (1 == 0 ? 30 : 40);
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    CHECK(test_fn() == 50);
}
