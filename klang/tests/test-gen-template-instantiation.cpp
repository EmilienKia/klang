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
 * Tests for Milestone 5: Template instantiation integration.
 *
 * These tests verify that:
 *  [A] A template struct instantiated via type reference compiles and
 *      generates correct code.
 *  [B] Template struct member variable has the correct concrete type.
 *  [C] Two distinct instantiations produce different types.
 *  [D] Duplicate instantiation of the same template args is cached.
 *  [E] Template struct used in function parameter and return type.
 *  [F] Template struct with multiple type parameters.
 */

#include <catch2/catch_all.hpp>
#include <sstream>

#include "helpers.hpp"
#include "../src/model/template.hpp"
#include "../src/model/template_instantiator.hpp"

// ════════════════════════════════════════════════════════════════════════════
//  [A] Template struct instantiated via type reference — basic end-to-end
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[A] M5: template struct instantiation via type reference",
          "[milestone5][template][instantiation]") {
    auto jit = gen_jit(R"SRC(
        module __m5_inst_a__;
        template<typename T>
        struct Box {
            public value : T;
        }

        get_value() : int {
            b : Box<int>;
            b.value = 42;
            return b.value;
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto get_value = jit->lookup_symbol<int(*)()>("_KFN13__m5_inst_a__9get_valueEv");
    REQUIRE(get_value != nullptr);
    CHECK(get_value() == 42);
}

// ════════════════════════════════════════════════════════════════════════════
//  [B] Template struct member variable has correct concrete type
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[B] M5: instantiated template member has correct type",
          "[milestone5][template][instantiation]") {
    auto comp = compile_model(R"SRC(
        module __m5_inst_b__;
        template<typename T>
        struct Wrapper {
            public inner : T;
        }
        struct User {
            public w : Wrapper<int>;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    // The concrete instantiation "Wrapper__int" should exist
    auto unit = comp->get_unit();
    auto root_ns = unit->get_root_namespace();
    REQUIRE(root_ns != nullptr);

    // Find the concrete instantiation
    auto wrapper_int = root_ns->get_aggregate("Wrapper__int");
    REQUIRE(wrapper_int != nullptr);
    CHECK_FALSE(wrapper_int->is_template());
    CHECK(wrapper_int->get_struct_type() != nullptr);
}

// ════════════════════════════════════════════════════════════════════════════
//  [C] Two distinct instantiations produce different types
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[C] M5: distinct instantiations are different types",
          "[milestone5][template][instantiation]") {
    auto comp = compile_model(R"SRC(
        module __m5_inst_c__;
        template<typename T>
        struct Box {
            public value : T;
        }
        struct User {
            public bi : Box<int>;
            public bf : Box<float>;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto root_ns = comp->get_unit()->get_root_namespace();
    auto box_int = root_ns->get_aggregate("Box__int");
    auto box_float = root_ns->get_aggregate("Box__float");
    REQUIRE(box_int != nullptr);
    REQUIRE(box_float != nullptr);
    CHECK(box_int != box_float);
    CHECK(box_int->get_struct_type() != box_float->get_struct_type());
}

// ════════════════════════════════════════════════════════════════════════════
//  [D] Duplicate instantiation of same template args is cached
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[D] M5: same template args are cached (same entity)",
          "[milestone5][template][instantiation]") {
    auto comp = compile_model(R"SRC(
        module __m5_inst_d__;
        template<typename T>
        struct Box {
            public value : T;
        }
        struct A {
            public b : Box<int>;
        }
        struct B {
            public b : Box<int>;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto root_ns = comp->get_unit()->get_root_namespace();
    // Only one Box__int should exist (the instantiation is cached)
    auto box_int = root_ns->get_aggregate("Box__int");
    REQUIRE(box_int != nullptr);

    // Both A and B should reference the same Box__int struct_type
    auto user_a = root_ns->get_aggregate("A");
    auto user_b = root_ns->get_aggregate("B");
    REQUIRE(user_a != nullptr);
    REQUIRE(user_b != nullptr);
}

// ════════════════════════════════════════════════════════════════════════════
//  [E] Template struct used in function parameter and return type
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[E] M5: template struct in function param and return",
          "[milestone5][template][instantiation]") {
    auto jit = gen_jit(R"SRC(
        module __m5_inst_e__;
        template<typename T>
        struct Box {
            public value : T;
        }

        make_box(v : int) : Box<int> {
            b : Box<int>;
            b.value = v;
            return b;
        }

        unbox(b : Box<int>&) : int {
            return b.value;
        }

        roundtrip() : int {
            b : Box<int> = make_box(99);
            return unbox(b);
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto roundtrip = jit->lookup_symbol<int(*)()>("_KFN13__m5_inst_e__9roundtripEv");
    REQUIRE(roundtrip != nullptr);
    CHECK(roundtrip() == 99);
}

// ════════════════════════════════════════════════════════════════════════════
//  [F] Template struct with multiple type parameters
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[F] M5: template struct with multiple type params",
          "[milestone5][template][instantiation]") {
    auto jit = gen_jit(R"SRC(
        module __m5_inst_f__;
        template<typename K, typename V>
        struct Pair {
            public first : K;
            public second : V;
        }

        test_pair() : int {
            p : Pair<int, int>;
            p.first = 10;
            p.second = 20;
            return p.first + p.second;
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto test_pair = jit->lookup_symbol<int(*)()>("_KFN13__m5_inst_f__9test_pairEv");
    REQUIRE(test_pair != nullptr);
    CHECK(test_pair() == 30);
}

// ════════════════════════════════════════════════════════════════════════════
//  [G] No cosmetic "cannot resolve type" messages during template compilation
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[G] M6: no cosmetic error messages on stderr",
          "[milestone6][template][diagnostics]") {
    // Capture stderr
    std::ostringstream captured;
    auto* old_buf = std::cerr.rdbuf(captured.rdbuf());

    auto jit = gen_jit(R"SRC(
        module __m6_diag__;
        template<typename T>
        struct Box {
            public value : T;
        }
        get_value() : int {
            b : Box<int>;
            b.value = 42;
            return b.value;
        }
    )SRC");

    // Restore stderr
    std::cerr.rdbuf(old_buf);

    REQUIRE(jit != nullptr);
    std::string stderr_output = captured.str();
    // Should not contain "cannot resolve type"
    CHECK(stderr_output.find("cannot resolve type") == std::string::npos);
    CHECK(stderr_output.find("cannot resolve reference subtype") == std::string::npos);
}

// ════════════════════════════════════════════════════════════════════════════
//  [H] M7: Template struct with default type parameter (all defaults, <> syntax)
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[H] M7: template struct with default type param — all defaults",
          "[milestone7][template][defaults]") {
    auto jit = gen_jit(R"SRC(
        module __m7_def_a__;
        template<typename T = int>
        struct Box {
            public value : T;
        }

        get_value() : int {
            b : Box<>;
            b.value = 77;
            return b.value;
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto get_value = jit->lookup_symbol<int(*)()>("_KFN12__m7_def_a__9get_valueEv");
    REQUIRE(get_value != nullptr);
    CHECK(get_value() == 77);
}

// ════════════════════════════════════════════════════════════════════════════
//  [I] M7: Template struct with partial defaults
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[I] M7: template struct with partial default type params",
          "[milestone7][template][defaults]") {
    auto jit = gen_jit(R"SRC(
        module __m7_def_b__;
        template<typename K, typename V = int>
        struct Pair {
            public first : K;
            public second : V;
        }

        test_pair() : int {
            p : Pair<int>;
            p.first = 10;
            p.second = 20;
            return p.first + p.second;
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto test_pair = jit->lookup_symbol<int(*)()>("_KFN12__m7_def_b__9test_pairEv");
    REQUIRE(test_pair != nullptr);
    CHECK(test_pair() == 30);
}

// ════════════════════════════════════════════════════════════════════════════
//  [J] M7: Default and explicit instantiations produce the same cached type
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[J] M7: default and explicit args produce same cached type",
          "[milestone7][template][defaults]") {
    auto comp = compile_model(R"SRC(
        module __m7_def_c__;
        template<typename T = int>
        struct Box {
            public value : T;
        }
        struct A {
            public b1 : Box<int>;
            public b2 : Box<>;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto root_ns = comp->get_unit()->get_root_namespace();
    // Both Box<int> and Box<> should resolve to the same Box__int
    auto box_int = root_ns->get_aggregate("Box__int");
    REQUIRE(box_int != nullptr);
    CHECK_FALSE(box_int->is_template());
}



