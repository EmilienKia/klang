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
 * Tests for Milestone 3: Template aggregate model definitions.
 *
 * These tests verify that:
 *  [A] A template struct definition is parsed and model-built without error.
 *  [B] The template aggregate is marked is_template() = true.
 *  [C] The tpl_info has the correct number and kind of parameters.
 *  [D] The template aggregate is NOT emitted as LLVM IR (no instantiation yet).
 *  [E] Non-template aggregates alongside template ones are still processed normally.
 *  [F] Template class with multiple parameters: all params recorded correctly.
 *  [G] Template struct with a value parameter.
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"
#include "../src/model/template.hpp"

// ════════════════════════════════════════════════════════════════════════════
//  [A] Template struct definition compiles without error
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[A] M3: template struct compiles without error",
          "[milestone3][template][aggregate]") {
    auto comp = compile_model(R"SRC(
        module gen_template_aggregates_01;
        template<typename T>
        struct Box {
            public value : T;
        }
    )SRC");
    REQUIRE(comp != nullptr);
}

// ════════════════════════════════════════════════════════════════════════════
//  [B] Template struct is marked is_template() = true
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[B] M3: template struct is marked is_template()",
          "[milestone3][template][aggregate]") {
    auto comp = compile_model(R"SRC(
        module gen_template_aggregates_02;
        template<typename T>
        struct Wrapper {
            public inner : T;
        }
    )SRC");
    REQUIRE(comp != nullptr);
    auto agg = find_aggregate(comp, "Wrapper");
    REQUIRE(agg != nullptr);
    CHECK(agg->is_template());
}

// ════════════════════════════════════════════════════════════════════════════
//  [C] tpl_info has correct parameter count and kind
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[C] M3: template struct tpl_info has correct params",
          "[milestone3][template][aggregate]") {
    auto comp = compile_model(R"SRC(
        module gen_template_aggregates_03;
        template<typename T>
        struct Container {
            public data : T;
        }
    )SRC");
    REQUIRE(comp != nullptr);
    auto agg = find_aggregate(comp, "Container");
    REQUIRE(agg != nullptr);
    REQUIRE(agg->is_template());

    auto* ti = agg->get_tpl_info();
    REQUIRE(ti != nullptr);
    REQUIRE(ti->params.size() == 1);
    CHECK(ti->params[0].name == "T");
    CHECK(ti->params[0].kind == k::model::template_param_kind::TYPENAME);
}

// ════════════════════════════════════════════════════════════════════════════
//  [D] Template aggregate is NOT emitted as LLVM IR
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[D] M3: template struct is not emitted as LLVM IR",
          "[milestone3][template][aggregate]") {
    // A template struct has no struct_type (the symbol_resolver skips it)
    auto comp = compile_model(R"SRC(
        module gen_template_aggregates_04;
        template<typename T>
        struct Phantom {
            public x : T;
        }
    )SRC");
    REQUIRE(comp != nullptr);
    auto agg = find_aggregate(comp, "Phantom");
    REQUIRE(agg != nullptr);
    REQUIRE(agg->is_template());
    // Template aggregates do not get a struct_type because symbol_resolver skips them
    CHECK(agg->get_struct_type() == nullptr);
}

// ════════════════════════════════════════════════════════════════════════════
//  [E] Non-template aggregate alongside template is processed normally
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[E] M3: non-template aggregate alongside template is processed normally",
          "[milestone3][template][aggregate]") {
    auto comp = compile_model(R"SRC(
        module gen_template_aggregates_05;
        template<typename T>
        struct TplBox {
            public value : T;
        }
        struct PlainBox {
            public value : int;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto tpl = find_aggregate(comp, "TplBox");
    REQUIRE(tpl != nullptr);
    CHECK(tpl->is_template());

    auto plain = find_aggregate(comp, "PlainBox");
    REQUIRE(plain != nullptr);
    CHECK_FALSE(plain->is_template());
    // PlainBox should have a struct_type (it is a concrete struct)
    CHECK(plain->get_struct_type() != nullptr);
}

// ════════════════════════════════════════════════════════════════════════════
//  [F] Template class with multiple type parameters
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[F] M3: template class with multiple type params",
          "[milestone3][template][aggregate]") {
    auto comp = compile_model(R"SRC(
        module gen_template_aggregates_06;
        template<typename K, typename V>
        struct Pair {
            public first : K;
            public second : V;
        }
    )SRC");
    REQUIRE(comp != nullptr);
    auto agg = find_aggregate(comp, "Pair");
    REQUIRE(agg != nullptr);
    REQUIRE(agg->is_template());

    auto* ti = agg->get_tpl_info();
    REQUIRE(ti != nullptr);
    REQUIRE(ti->params.size() == 2);
    CHECK(ti->params[0].name == "K");
    CHECK(ti->params[0].kind == k::model::template_param_kind::TYPENAME);
    CHECK(ti->params[1].name == "V");
    CHECK(ti->params[1].kind == k::model::template_param_kind::TYPENAME);
}

// ════════════════════════════════════════════════════════════════════════════
//  [G] Template struct with a value parameter
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[G] M3: template struct with value parameter",
          "[milestone3][template][aggregate]") {
    auto comp = compile_model(R"SRC(
        module gen_template_aggregates_07;
        template<typename T, unsigned int N>
        struct FixedArray {
            public data : T;
        }
    )SRC");
    REQUIRE(comp != nullptr);
    auto agg = find_aggregate(comp, "FixedArray");
    REQUIRE(agg != nullptr);
    REQUIRE(agg->is_template());

    auto* ti = agg->get_tpl_info();
    REQUIRE(ti != nullptr);
    REQUIRE(ti->params.size() == 2);
    CHECK(ti->params[0].name == "T");
    CHECK(ti->params[0].is_type_param());
    CHECK(ti->params[1].name == "N");
    CHECK(ti->params[1].is_value_param());
}


