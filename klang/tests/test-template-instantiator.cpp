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
 * Tests for Milestone 4: Template Instantiator (model-level).
 *
 * These tests verify that:
 *  [C] Template instantiator: instantiate a template struct with T=int, verify concrete aggregate.
 *  [D] Template instantiator: instantiate a template function with T=int, verify concrete function.
 *  [E] Template instantiator: cache hit — same arguments return the same instance.
 *  [F] Template instantiator: different arguments return different instances.
 *  [G] Name helpers.
 *  [H] Template instantiator: model-level — member types are correctly substituted.
 *  [I] Template instantiator: model-level — function body is cloned.
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"
#include "../src/model/template.hpp"
#include "../src/model/template_instantiator.hpp"

// ═══════════════════════════════════════════════════════════════════════════
//  [C] Template instantiator: instantiate template struct with T=int
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("[C] M4: instantiate template struct Box<int>",
          "[milestone4][instantiator][aggregate]") {
    auto comp = compile_model(R"SRC(
        module __m4_c__;
        template<typename T>
        struct Box {
            public value : T;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto unit = comp->get_unit();
    REQUIRE(unit != nullptr);
    auto root_ns = unit->get_root_namespace();
    REQUIRE(root_ns != nullptr);

    // Find the template aggregate
    auto tpl = root_ns->get_aggregate("Box");
    REQUIRE(tpl != nullptr);
    REQUIRE(tpl->is_template());

    // Get the context from the unit
    auto ctx = comp->get_context_for_test();
    REQUIRE(ctx != nullptr);

    // Create a type argument: int
    auto int_type = ctx->from_type(k::model::primitive_type::INT);
    REQUIRE(int_type != nullptr);

    std::vector<k::model::template_argument> args;
    args.push_back(k::model::template_argument::make_type(int_type));

    test_logger logger;

    // Instantiate
    auto concrete = k::model::template_instantiator::instantiate_aggregate(
        *tpl, args, root_ns, *unit, ctx, logger);

    REQUIRE(concrete != nullptr);
    CHECK_FALSE(concrete->is_template());

    // The instantiated name should contain "Box" and "int"
    std::string name = concrete->get_short_name();
    CHECK(name.find("Box") != std::string::npos);
    CHECK(name.find("int") != std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════════════
//  [D] Template instantiator: instantiate template function with T=int
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("[D] M4: instantiate template function identity<int>",
          "[milestone4][instantiator][function]") {
    auto comp = compile_model(R"SRC(
        module __m4_d__;
        template<typename T>
        identity(x : T) : T { return x; }
    )SRC");
    REQUIRE(comp != nullptr);

    auto unit = comp->get_unit();
    auto root_ns = unit->get_root_namespace();
    REQUIRE(root_ns != nullptr);

    // Find the template function
    auto tpl = root_ns->get_function("identity");
    REQUIRE(tpl != nullptr);
    REQUIRE(tpl->is_template());

    auto ctx = comp->get_context_for_test();
    auto int_type = ctx->from_type(k::model::primitive_type::INT);

    std::vector<k::model::template_argument> args;
    args.push_back(k::model::template_argument::make_type(int_type));

    test_logger logger;

    auto concrete = k::model::template_instantiator::instantiate_function(
        *tpl, args, root_ns, *unit, ctx, logger);

    REQUIRE(concrete != nullptr);
    CHECK_FALSE(concrete->is_template());

    std::string name = concrete->get_short_name();
    CHECK(name.find("identity") != std::string::npos);
    CHECK(name.find("int") != std::string::npos);
}

// ═══════════════════════════════════════════════════════════════════════════
//  [E] Template instantiator: cache hit — same args -> same instance
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("[E] M4: instantiation cache returns same instance for same args",
          "[milestone4][instantiator][cache]") {
    auto comp = compile_model(R"SRC(
        module __m4_e__;
        template<typename T>
        struct Wrapper {
            public data : T;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto unit = comp->get_unit();
    auto root_ns = unit->get_root_namespace();
    auto tpl = root_ns->get_aggregate("Wrapper");
    REQUIRE(tpl != nullptr);
    REQUIRE(tpl->is_template());

    auto ctx = comp->get_context_for_test();
    auto int_type = ctx->from_type(k::model::primitive_type::INT);

    std::vector<k::model::template_argument> args;
    args.push_back(k::model::template_argument::make_type(int_type));

    test_logger logger;

    auto inst1 = k::model::template_instantiator::instantiate_aggregate(
        *tpl, args, root_ns, *unit, ctx, logger);
    REQUIRE(inst1 != nullptr);

    auto inst2 = k::model::template_instantiator::instantiate_aggregate(
        *tpl, args, root_ns, *unit, ctx, logger);
    REQUIRE(inst2 != nullptr);

    // Same instance from cache
    CHECK(inst1.get() == inst2.get());
}

// ═══════════════════════════════════════════════════════════════════════════
//  [F] Template instantiator: different args -> different instances
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("[F] M4: different args produce different instances",
          "[milestone4][instantiator]") {
    auto comp = compile_model(R"SRC(
        module __m4_f__;
        template<typename T>
        struct Container {
            public val : T;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto unit = comp->get_unit();
    auto root_ns = unit->get_root_namespace();
    auto tpl = root_ns->get_aggregate("Container");
    REQUIRE(tpl != nullptr);

    auto ctx = comp->get_context_for_test();
    auto int_type = ctx->from_type(k::model::primitive_type::INT);
    auto float_type = ctx->from_type(k::model::primitive_type::FLOAT);

    test_logger logger;

    std::vector<k::model::template_argument> int_args;
    int_args.push_back(k::model::template_argument::make_type(int_type));

    std::vector<k::model::template_argument> float_args;
    float_args.push_back(k::model::template_argument::make_type(float_type));

    auto inst_int = k::model::template_instantiator::instantiate_aggregate(
        *tpl, int_args, root_ns, *unit, ctx, logger);
    auto inst_float = k::model::template_instantiator::instantiate_aggregate(
        *tpl, float_args, root_ns, *unit, ctx, logger);

    REQUIRE(inst_int != nullptr);
    REQUIRE(inst_float != nullptr);
    CHECK(inst_int.get() != inst_float.get());
    CHECK(inst_int->get_short_name() != inst_float->get_short_name());
}

// ═══════════════════════════════════════════════════════════════════════════
//  [G] Name helpers
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("[G] M4: build_instantiation_key and build_instantiated_name",
          "[milestone4][names]") {
    auto ctx = k::model::context::create();
    auto int_type = ctx->from_type(k::model::primitive_type::INT);

    // Single type arg
    {
        std::vector<k::model::template_argument> args;
        args.push_back(k::model::template_argument::make_type(int_type));
        auto key = k::model::build_instantiation_key(args);
        CHECK(key.find("int") != std::string::npos);

        auto name = k::model::build_instantiated_name("Box", args);
        CHECK(name.find("Box") != std::string::npos);
        CHECK(name.find("int") != std::string::npos);
    }

    // Value arg
    {
        std::vector<k::model::template_argument> args;
        args.push_back(k::model::template_argument::make_type(int_type));
        args.push_back(k::model::template_argument::make_value(10));
        auto key = k::model::build_instantiation_key(args);
        CHECK(key.find("10") != std::string::npos);

        auto name = k::model::build_instantiated_name("Array", args);
        CHECK(name.find("Array") != std::string::npos);
        CHECK(name.find("10") != std::string::npos);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
//  [H] Template instantiator: member types are correctly substituted
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("[H] M4: instantiated struct has member with substituted type",
          "[milestone4][instantiator][model_level]") {
    auto comp = compile_model(R"SRC(
        module __m4_h__;
        template<typename T>
        struct Box {
            public value : T;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto unit = comp->get_unit();
    auto root_ns = unit->get_root_namespace();
    auto tpl = root_ns->get_aggregate("Box");
    REQUIRE(tpl != nullptr);
    REQUIRE(tpl->is_template());

    auto ctx = comp->get_context_for_test();
    auto int_type = ctx->from_type(k::model::primitive_type::INT);

    std::vector<k::model::template_argument> args;
    args.push_back(k::model::template_argument::make_type(int_type));

    test_logger logger;

    auto concrete = k::model::template_instantiator::instantiate_aggregate(
        *tpl, args, root_ns, *unit, ctx, logger);

    REQUIRE(concrete != nullptr);
    CHECK_FALSE(concrete->is_template());

    // Verify the member variable "value" exists with substituted type
    auto var = concrete->get_variable("value");
    REQUIRE(var != nullptr);
    auto var_type = var->get_type();
    REQUIRE(var_type != nullptr);
    // The type should be int (substituted from T)
    CHECK(k::model::type::is_primitive(var_type));
    auto prim = std::dynamic_pointer_cast<k::model::primitive_type>(var_type);
    REQUIRE(prim != nullptr);
    CHECK(prim->get_type() == k::model::primitive_type::INT);
}

// ═══════════════════════════════════════════════════════════════════════════
//  [I] Template instantiator: function body is cloned at model level
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("[I] M4: instantiated function has cloned body",
          "[milestone4][instantiator][model_level]") {
    auto comp = compile_model(R"SRC(
        module __m4_i__;
        template<typename T>
        identity(x : T) : T { return x; }
    )SRC");
    REQUIRE(comp != nullptr);

    auto unit = comp->get_unit();
    auto root_ns = unit->get_root_namespace();

    auto tpl = root_ns->get_function("identity");
    REQUIRE(tpl != nullptr);
    REQUIRE(tpl->is_template());

    auto ctx = comp->get_context_for_test();
    auto int_type = ctx->from_type(k::model::primitive_type::INT);

    std::vector<k::model::template_argument> args;
    args.push_back(k::model::template_argument::make_type(int_type));

    test_logger logger;

    auto concrete = k::model::template_instantiator::instantiate_function(
        *tpl, args, root_ns, *unit, ctx, logger);

    REQUIRE(concrete != nullptr);
    CHECK_FALSE(concrete->is_template());

    // Verify the return type is int
    REQUIRE(concrete->has_return_type());
    auto ret_type = concrete->get_return_type();
    REQUIRE(ret_type != nullptr);
    CHECK(k::model::type::is_primitive(ret_type));

    // Verify at least one parameter with substituted type
    REQUIRE(concrete->get_parameter_size() >= 1);
    auto param = concrete->get_parameter(static_cast<size_t>(0));
    REQUIRE(param != nullptr);
    auto param_type = param->get_type();
    REQUIRE(param_type != nullptr);
    CHECK(k::model::type::is_primitive(param_type));

    // Verify the body block exists and has statements
    auto blk = concrete->get_block();
    REQUIRE(blk != nullptr);
    CHECK_FALSE(blk->get_statements().empty());
}










