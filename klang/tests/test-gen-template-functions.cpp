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
 * Tests for Milestone 3: Template function model definitions.
 *
 * These tests verify that:
 *  [A] A template function definition is parsed and model-built without error.
 *  [B] The template function is marked is_template() = true.
 *  [C] The tpl_info has the correct number and kind of parameters.
 *  [D] The template function is NOT emitted as LLVM IR (no instantiation yet).
 *  [E] Non-template functions alongside template ones are still processed normally.
 *  [F] Template function with multiple parameters.
 *  [G] Template function with a value parameter.
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"
#include "../src/model/template.hpp"

/**
 * Helper: find a function by short name in the root namespace.
 */
static std::shared_ptr<k::model::function>
find_function(const std::shared_ptr<k::compiler>& comp, const std::string& name) {
    if (!comp || !comp->get_unit()) return nullptr;
    auto root = comp->get_unit()->get_root_namespace();
    if (!root) return nullptr;
    return root->get_function(name);
}

// ════════════════════════════════════════════════════════════════════════════
//  [A] Template function definition compiles without error
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[A] M3: template function compiles without error",
          "[milestone3][template][function]") {
    auto comp = compile_model(R"SRC(
        module gen_template_functions_01;
        template<typename T>
        identity(x : T) : T { return x; }
    )SRC");
    REQUIRE(comp != nullptr);
}

// ════════════════════════════════════════════════════════════════════════════
//  [B] Template function is marked is_template() = true
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[B] M3: template function is marked is_template()",
          "[milestone3][template][function]") {
    auto comp = compile_model(R"SRC(
        module gen_template_functions_02;
        template<typename T>
        identity(x : T) : T { return x; }
    )SRC");
    REQUIRE(comp != nullptr);
    auto fn = find_function(comp, "identity");
    REQUIRE(fn != nullptr);
    CHECK(fn->is_template());
}

// ════════════════════════════════════════════════════════════════════════════
//  [C] tpl_info has correct parameter count and kind
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[C] M3: template function tpl_info has correct params",
          "[milestone3][template][function]") {
    auto comp = compile_model(R"SRC(
        module gen_template_functions_03;
        template<typename T>
        identity(x : T) : T { return x; }
    )SRC");
    REQUIRE(comp != nullptr);
    auto fn = find_function(comp, "identity");
    REQUIRE(fn != nullptr);
    REQUIRE(fn->is_template());

    auto* ti = fn->get_tpl_info();
    REQUIRE(ti != nullptr);
    REQUIRE(ti->params.size() == 1);
    CHECK(ti->params[0].name == "T");
    CHECK(ti->params[0].kind == k::model::template_param_kind::TYPENAME);
}

// ════════════════════════════════════════════════════════════════════════════
//  [D] Template function is NOT emitted as LLVM IR
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[D] M3: template function is not emitted as LLVM IR",
          "[milestone3][template][function]") {
    // A template function with no instantiation should still compile,
    // and a non-template function alongside it should still work via JIT.
    auto jit = gen_jit(R"SRC(
        module gen_template_functions_04;
        template<typename T>
        identity(x : T) : T { return x; }
        concrete() : int { return 42; }
    )SRC");
    REQUIRE(jit != nullptr);
    // The concrete function works
    auto fn = jit->lookup_symbol<int(*)()>("concrete");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 42);
    // The template function should NOT have an LLVM symbol
    // (lookup_symbol returns null for missing symbols)
    auto tpl_fn = jit->lookup_symbol<int(*)(int)>("identity");
    CHECK(tpl_fn == nullptr);
}

// ════════════════════════════════════════════════════════════════════════════
//  [E] Non-template function alongside template is processed normally
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[E] M3: non-template function alongside template works",
          "[milestone3][template][function]") {
    auto comp = compile_model(R"SRC(
        module gen_template_functions_05;
        template<typename T>
        swap(a : T, b : T) : T { return b; }
        add(a : int, b : int) : int { return a + b; }
    )SRC");
    REQUIRE(comp != nullptr);

    auto tpl = find_function(comp, "swap");
    REQUIRE(tpl != nullptr);
    CHECK(tpl->is_template());

    auto plain = find_function(comp, "add");
    REQUIRE(plain != nullptr);
    CHECK_FALSE(plain->is_template());
}

// ════════════════════════════════════════════════════════════════════════════
//  [F] Template function with multiple type parameters
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[F] M3: template function with multiple type params",
          "[milestone3][template][function]") {
    auto comp = compile_model(R"SRC(
        module gen_template_functions_06;
        template<typename A, typename B>
        make_pair(a : A, b : B) : A { return a; }
    )SRC");
    REQUIRE(comp != nullptr);
    auto fn = find_function(comp, "make_pair");
    REQUIRE(fn != nullptr);
    REQUIRE(fn->is_template());

    auto* ti = fn->get_tpl_info();
    REQUIRE(ti != nullptr);
    REQUIRE(ti->params.size() == 2);
    CHECK(ti->params[0].name == "A");
    CHECK(ti->params[0].kind == k::model::template_param_kind::TYPENAME);
    CHECK(ti->params[1].name == "B");
    CHECK(ti->params[1].kind == k::model::template_param_kind::TYPENAME);
}

// ════════════════════════════════════════════════════════════════════════════
//  [G] Template function with a value parameter
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[G] M3: template function with value parameter",
          "[milestone3][template][function]") {
    auto comp = compile_model(R"SRC(
        module gen_template_functions_07;
        template<typename T, unsigned int N>
        repeat(x : T) : T { return x; }
    )SRC");
    REQUIRE(comp != nullptr);
    auto fn = find_function(comp, "repeat");
    REQUIRE(fn != nullptr);
    REQUIRE(fn->is_template());

    auto* ti = fn->get_tpl_info();
    REQUIRE(ti != nullptr);
    REQUIRE(ti->params.size() == 2);
    CHECK(ti->params[0].name == "T");
    CHECK(ti->params[0].is_type_param());
    CHECK(ti->params[1].name == "N");
    CHECK(ti->params[1].is_value_param());
}


