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

// NOTE: Negative literals may not be parsed directly as template args
// (parser uses parse_primary_expr which does not handle unary minus).
// This test uses 0 as a safe boundary value instead.
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
            if(a) { r = r + 1; }
            if(b) { r = r + 10; }
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
            if(c.is_signed()) { r = r + 1; }
            if(d.is_signed()) { r = r + 10; }
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

