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

#include <filesystem>
#include <fstream>
#include <sstream>
#include <type_traits>
#include <unistd.h>

#include <llvm/MC/TargetRegistry.h>

#include "helpers.hpp"
#include "../src/common/llvm_target_triple.hpp"
#include "../src/model/template.hpp"
#include "../src/model/template_instantiator.hpp"

TEST_CASE("Const model elements preserve constness in shared casts",
          "[model][shared-as]") {
    auto comp = compile_model("module shared_cast;");
    REQUIRE(comp);
    auto unit = comp->get_unit();
    REQUIRE(unit);

    const k::model::element& source = *unit;
    auto base = source.shared_as<k::model::element>();
    auto derived = source.shared_as<k::model::unit>();
    auto unrelated = source.shared_as<k::model::aggregate>();
    static_assert(std::is_same_v<decltype(base), std::shared_ptr<const k::model::element>>);
    static_assert(std::is_same_v<decltype(derived), std::shared_ptr<const k::model::unit>>);
    CHECK(base.get() == unit.get());
    CHECK(derived.get() == unit.get());
    CHECK_FALSE(unrelated);
    CHECK(unit->shared_as<k::model::unit>() == unit);
}

TEST_CASE("Concrete template arguments do not satisfy unresolved base constraints",
          "[model][unresolved-template-constraint]") {
    using namespace k::model;
    auto comp = compile_model(R"SRC(
        module unresolved_template_constraint;
        class Animal {}
        class Car {}
    )SRC");
    REQUIRE(comp);
    auto unit = comp->get_unit();
    REQUIRE(unit);
    auto root = unit->get_root_namespace();
    REQUIRE(root);
    auto animal = root->get_aggregate("Animal");
    auto car = root->get_aggregate("Car");
    REQUIRE(animal);
    REQUIRE(car);
    auto animal_type = animal->get_struct_type();
    auto car_type = car->get_struct_type();
    REQUIRE(animal_type);
    REQUIRE(car_type);
    REQUIRE(type::is_resolved(animal_type));
    REQUIRE(type::is_resolved(car_type));

    template_param_descriptor param;
    param.kind = template_param_kind::CLASS;
    param.name = "T";
    param.constraint_type = animal_type;
    tpl_info info;
    info.params.push_back(param);
    std::vector<template_argument> animal_args{template_argument::make_type(animal_type)};
    std::vector<template_argument> car_args{template_argument::make_type(car_type)};
    size_t error_index;
    std::string error_kind;
    CHECK(validate_template_arg_constraints(info.params, animal_args, error_index, error_kind));
    CHECK_FALSE(validate_template_arg_constraints(info.params, car_args, error_index, error_kind));
    CHECK(error_index == 0);
    CHECK(error_kind == "constraint");

    auto ctx = comp->get_context_for_test();
    REQUIRE(ctx);
    auto unresolved = ctx->from_string("MissingBase");
    REQUIRE(unresolved);
    REQUIRE_FALSE(type::is_resolved(unresolved));
    info.params[0].constraint_type = unresolved;
    CHECK_FALSE(validate_template_arg_constraints(info, animal_args, error_index, error_kind));
}

// ═══════════════════════════════════════════════════════════════════════════
//  [C] Template instantiator: instantiate template struct with T=int
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("[C] M4: instantiate template struct Box<int>",
          "[milestone4][instantiator][aggregate]") {
    auto comp = compile_model(R"SRC(
        module template_instantiator_01;
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
        module template_instantiator_02;
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
        module template_instantiator_03;
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
        module template_instantiator_04;
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
        module template_instantiator_05;
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
        module template_instantiator_06;
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

TEST_CASE("[J] M7: generic aggregate synthesis is unique and uses base name",
          "[milestone7][generic][instantiator]") {
    auto comp = compile_model(R"SRC(
        module template_instantiator_07;
        generic<typename T>
        struct Box {
            public value : T&;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto unit = comp->get_unit();
    REQUIRE(unit != nullptr);
    auto root_ns = unit->get_root_namespace();
    REQUIRE(root_ns != nullptr);

    auto tpl = root_ns->get_aggregate("Box");
    REQUIRE(tpl != nullptr);
    REQUIRE(tpl->is_template());
    REQUIRE(tpl->is_generic());

    auto ctx = comp->get_context_for_test();
    REQUIRE(ctx != nullptr);

    test_logger logger;

    auto syn1 = k::model::template_instantiator::synthesize_generic_aggregate(
        *tpl, root_ns, *unit, ctx, logger);
    auto syn2 = k::model::template_instantiator::synthesize_generic_aggregate(
        *tpl, root_ns, *unit, ctx, logger);

    REQUIRE(syn1 != nullptr);
    REQUIRE(syn2 != nullptr);
    CHECK(syn1.get() == syn2.get());
    CHECK(syn1->get_short_name() == "Box");

    auto value = syn1->get_variable("value");
    REQUIRE(value != nullptr);
    REQUIRE(value->get_type() != nullptr);
    CHECK(k::model::type::is_reference(value->get_type()));

    auto ref_inner = value->get_type()->get_subtype();
    REQUIRE(ref_inner != nullptr);
    CHECK(k::model::type::is_pointer(ref_inner));

    auto ptr_inner = ref_inner->get_subtype();
    REQUIRE(ptr_inner != nullptr);
    CHECK(k::model::type::is_primitive(ptr_inner));
    auto prim = std::dynamic_pointer_cast<k::model::primitive_type>(ptr_inner);
    REQUIRE(prim != nullptr);
    CHECK(prim->get_type() == k::model::primitive_type::BYTE);
}

TEST_CASE("Generic source binders survive synthesis and scoped substitution",
          "[model][generic-source][instantiator]") {
    using namespace k::model;
    auto comp = compile_model(R"SRC(
        module generic_source_binders;
        generic<typename T, typename U>
        struct Host {
            public:
            cell : U*;
            outer(a : T*, b : U*) : T* { return a; }
            generic<typename T>
            inner(a : T*, b : U*) : T* { return a; }
        }
    )SRC");
    REQUIRE(comp);
    auto unit = comp->get_unit();
    auto root = unit->get_root_namespace();
    auto host = root->get_aggregate("Host");
    REQUIRE(host);
    auto inner = host->get_function("inner");
    REQUIRE(inner);
    REQUIRE(host->get_tpl_info());
    REQUIRE(inner->get_tpl_info());

    auto ctx = comp->get_context_for_test();
    test_logger logger;
    auto synth = template_instantiator::synthesize_generic_aggregate(
        *host, root, *unit, ctx, logger);
    REQUIRE(synth);
    auto outer_clone = synth->get_function("outer");
    auto inner_clone = synth->get_function("inner");
    REQUIRE(outer_clone);
    REQUIRE(inner_clone);
    REQUIRE(inner_clone->get_tpl_info());
    CHECK(inner_clone->get_tpl_info()->binder_scope == inner->get_tpl_info()->binder_scope);

    REQUIRE(outer_clone->get_parameter("a"));
    REQUIRE(outer_clone->get_parameter("b"));
    REQUIRE(inner_clone->get_parameter("a"));
    REQUIRE(inner_clone->get_parameter("b"));
    auto outer_a = outer_clone->get_parameter("a")->get_generic_source_type();
    auto outer_b = outer_clone->get_parameter("b")->get_generic_source_type();
    auto inner_a = inner_clone->get_parameter("a")->get_generic_source_type();
    auto inner_b = inner_clone->get_parameter("b")->get_generic_source_type();
    REQUIRE(outer_a);
    REQUIRE(outer_b);
    REQUIRE(inner_a);
    REQUIRE(inner_b);
    auto outer_t = std::dynamic_pointer_cast<unresolved_type>(outer_a->get_type()->get_subtype());
    auto outer_u = std::dynamic_pointer_cast<unresolved_type>(outer_b->get_type()->get_subtype());
    auto inner_t = std::dynamic_pointer_cast<unresolved_type>(inner_a->get_type()->get_subtype());
    auto inner_u = std::dynamic_pointer_cast<unresolved_type>(inner_b->get_type()->get_subtype());
    REQUIRE(outer_t);
    REQUIRE(outer_u);
    REQUIRE(inner_t);
    REQUIRE(inner_u);
    REQUIRE(outer_t->source_binder());
    REQUIRE(outer_u->source_binder());
    REQUIRE(inner_t->source_binder());
    REQUIRE(inner_u->source_binder());
    CHECK(outer_t->source_binder()->scope == host->get_tpl_info()->binder_scope);
    CHECK(outer_t->source_binder()->ordinal == 0);
    CHECK(outer_u->source_binder()->ordinal == 1);
    CHECK(inner_t->source_binder()->scope == inner->get_tpl_info()->binder_scope);
    CHECK(inner_t->source_binder()->scope != outer_t->source_binder()->scope);
    CHECK(*inner_u->source_binder() == *outer_u->source_binder());
    auto cell = synth->get_variable("cell");
    REQUIRE(cell);
    auto cell_source = cell->get_generic_source_type();
    REQUIRE(cell_source);
    auto cell_u = std::dynamic_pointer_cast<unresolved_type>(
        cell_source->get_type()->get_subtype());
    REQUIRE(cell_u);
    REQUIRE(cell_u->source_binder());
    CHECK(*cell_u->source_binder() == *outer_u->source_binder());

    auto reclone = synth->define_function("copy_inner", false);
    REQUIRE(reclone);
    template_instantiator::populate_function_from_template_public(
        reclone, *inner_clone, {}, {}, {}, true);
    REQUIRE(reclone->get_parameter("a"));
    CHECK(reclone->get_parameter("a")->get_generic_source_type() == inner_a);

    auto int_type = ctx->from_type(primitive_type::INT);
    auto char_type = ctx->from_type(primitive_type::CHAR);
    auto byte_type = ctx->from_type(primitive_type::BYTE);
    source_type_substitution_map bindings{
        {*outer_t->source_binder(), int_type},
        {*outer_u->source_binder(), byte_type},
        {*inner_t->source_binder(), char_type},
    };
    auto concrete_outer = outer_a->substitute(bindings);
    auto concrete_inner = inner_a->substitute(bindings);
    auto concrete_other = inner_b->substitute(bindings);
    CHECK(type::are_equal(concrete_outer->get_type(), int_type->get_pointer()));
    CHECK(type::are_equal(concrete_inner->get_type(), char_type->get_pointer()));
    CHECK(type::are_equal(concrete_other->get_type(), byte_type->get_pointer()));
    auto other_bindings = bindings;
    other_bindings[*outer_t->source_binder()] = char_type;
    CHECK(type::are_equal(outer_a->substitute(other_bindings)->get_type(),
                          char_type->get_pointer()));
    CHECK(type::are_equal(concrete_outer->get_type(), int_type->get_pointer()));

    auto return_source = inner_clone->get_generic_source_return_type();
    REQUIRE(return_source);
    CHECK(type::are_equal(return_source->substitute(bindings)->get_type(),
                          char_type->get_pointer()));
    CHECK(type::is_erased_param(outer_clone->get_parameter("a")->get_type()->get_subtype()));

    tpl_info other_host;
    other_host.params = host->get_tpl_info()->params;
    auto other = generic_source_type::capture(
        host->get_function("outer")->get_parameter("a")->get_type(), {&other_host});
    REQUIRE(other);
    auto other_t = std::dynamic_pointer_cast<unresolved_type>(other->get_type()->get_subtype());
    REQUIRE(other_t);
    REQUIRE(other_t->source_binder());
    CHECK(other_t->source_binder()->scope != outer_t->source_binder()->scope);
    CHECK(type::is_pointer(other->substitute(bindings)->get_type()));
    auto other_result = std::dynamic_pointer_cast<unresolved_type>(
        other->substitute(bindings)->get_type()->get_subtype());
    REQUIRE(other_result);
    CHECK(other_result->source_binder());
    auto unknown_binding = outer_a->substitute({{*outer_t->source_binder(), nullptr}});
    CHECK(unknown_binding->has_unknown_bindings());
    CHECK_FALSE(outer_a->has_unknown_bindings());
    auto still_symbolic = std::dynamic_pointer_cast<unresolved_type>(
        unknown_binding->get_type()->get_subtype());
    REQUIRE(still_symbolic);
    CHECK(still_symbolic->source_binder());

    auto nested = unresolved_type::make_model_template(k::name("Alias"),
        {outer_a->get_type(), inner_a->get_type(), outer_b->get_type()});
    auto nested_source = generic_source_type::capture(nested, {});
    REQUIRE(nested_source);
    auto nested_result = std::dynamic_pointer_cast<unresolved_type>(
        nested_source->substitute(bindings)->get_type());
    REQUIRE(nested_result);
    REQUIRE(nested_result->get_model_template_args().size() == 3);
    CHECK(type::are_equal(nested_result->get_model_template_args()[0], int_type->get_pointer()));
    CHECK(type::are_equal(nested_result->get_model_template_args()[1], char_type->get_pointer()));
    CHECK(type::are_equal(nested_result->get_model_template_args()[2], byte_type->get_pointer()));

    auto callable = unresolved_callable_type::make_shared(k::name{}, callable_type::addresser::pointer,
        {outer_a->get_type(), inner_a->get_type()}, outer_b->get_type(), {});
    auto callable_source = generic_source_type::capture(callable, {});
    auto callable_result = std::dynamic_pointer_cast<unresolved_callable_type>(
        callable_source->substitute(bindings)->get_type());
    REQUIRE(callable_result);
    REQUIRE(callable_result->parameter_types().size() == 2);
    CHECK(type::are_equal(callable_result->parameter_types()[0], int_type->get_pointer()));
    CHECK(type::are_equal(callable_result->parameter_types()[1], char_type->get_pointer()));
    CHECK(type::are_equal(callable_result->get_return_type(), byte_type->get_pointer()));
}

TEST_CASE("Generic source types retain aliases wrappers and erased markers",
          "[model][generic-source][instantiator]") {
    using namespace k::model;
    auto comp = compile_model(R"SRC(
        module generic_source_alias;
        typedef Octet : unsigned byte;
        alias_probe(value : Octet*) : int { return 0; }
        generic<typename T> use(value : const T*) : int { return 0; }
    )SRC");
    REQUIRE(comp);
    auto ctx = comp->get_context_for_test();
    auto root = comp->get_unit()->get_root_namespace();
    auto fn = root->get_function("use");
    REQUIRE(fn);
    REQUIRE(fn->get_tpl_info());
    bool created = false;
    auto synth = template_instantiator::synthesize_generic_function(
        *fn, root, nullptr, ctx, created);
    REQUIRE(synth);
    REQUIRE(created);
    auto parameter = synth->get_parameter("value");
    REQUIRE(parameter);
    auto source = parameter->get_generic_source_type();
    REQUIRE(source);
    auto const_inner = source->get_type()->get_subtype();
    REQUIRE(type::is_const(const_inner));
    auto binder = std::dynamic_pointer_cast<unresolved_type>(const_inner->get_subtype());
    REQUIRE(binder);
    REQUIRE(binder->source_binder());
    auto alias = root->get_alias("Octet");
    REQUIRE(alias);
    auto nominal = alias->get_alias_type();
    REQUIRE(nominal);
    auto specialized = source->substitute({{*binder->source_binder(), nominal}});
    auto specialized_inner = specialized->get_type()->get_subtype();
    REQUIRE(type::is_const(specialized_inner));
    CHECK(specialized_inner->get_subtype() == nominal);

    auto byte_type = ctx->from_type(primitive_type::BYTE);
    auto plain = generic_source_type::capture(byte_type->get_pointer(), {fn->get_tpl_info()});
    auto erased = generic_source_type::capture(byte_type->get_erased_param(), {fn->get_tpl_info()});
    REQUIRE(plain);
    REQUIRE(erased);
    CHECK_FALSE(type::is_erased_param(plain->get_type()));
    CHECK(type::is_erased_param(erased->get_type()));
    CHECK(type::is_erased_param(erased->substitute({})->get_type()));

    auto non_placeholder = ctx->from_string("T");
    auto unknown_name = generic_source_type::capture(
        non_placeholder, {fn->get_tpl_info()});
    auto unknown_type = std::dynamic_pointer_cast<unresolved_type>(unknown_name->get_type());
    REQUIRE(unknown_type);
    CHECK_FALSE(unknown_type->source_binder());

    auto deferred = unresolved_type::make_model_template(k::name("Alias"), {nullptr});
    auto unknown = generic_source_type::capture(deferred, {fn->get_tpl_info()});
    REQUIRE(unknown);
    CHECK(unknown->has_unmodelled_arguments());
    CHECK(unknown->substitute({{*binder->source_binder(), nominal}})
              ->has_unmodelled_arguments());

    std::shared_ptr<generic_source_type> retained;
    std::weak_ptr<const template_binder_scope> weak_scope;
    {
        tpl_info local;
        local.params.push_back({.name = "T"});
        weak_scope = local.binder_scope;
        ctx->push_template_param_scope({"T"});
        auto raw = ctx->from_string("T");
        ctx->pop_template_param_scope();
        retained = generic_source_type::capture(raw->get_pointer(), {&local});
    }
    REQUIRE(retained);
    CHECK_FALSE(weak_scope.expired());
    auto retained_binder = std::dynamic_pointer_cast<unresolved_type>(
        retained->get_type()->get_subtype());
    REQUIRE(retained_binder);
    REQUIRE(retained_binder->source_binder());
    CHECK(retained_binder->source_binder()->scope == weak_scope.lock());
    retained_binder.reset();
    retained.reset();
    CHECK(weak_scope.expired());
}

TEST_CASE("Classic specialization preserves generic member source binders",
          "[model][generic-source][instantiator]") {
    using namespace k::model;
    auto comp = compile_model(R"SRC(
        module generic_source_classic_host;
        template<typename T, typename U>
        struct Host {
            public:
            generic<typename T>
            shadow(a : T*, b : U*) : U* { return b; }
            generic<typename V>
            plain(a : T*, b : V*) : T* { return a; }
        }
    )SRC");
    REQUIRE(comp);
    auto unit = comp->get_unit();
    auto root = unit->get_root_namespace();
    auto host = root->get_aggregate("Host");
    REQUIRE(host);
    REQUIRE(host->get_tpl_info());
    auto ctx = comp->get_context_for_test();
    auto int_type = ctx->from_type(primitive_type::INT);
    auto byte_type = ctx->from_type(primitive_type::BYTE);
    auto char_type = ctx->from_type(primitive_type::CHAR);
    test_logger logger;

    auto specialize = [&](const std::shared_ptr<type>& first,
                          const std::shared_ptr<type>& second) {
        return template_instantiator::instantiate_aggregate(
            *host, {template_argument::make_type(first), template_argument::make_type(second)},
            root, *unit, ctx, logger);
    };
    auto first = specialize(int_type, byte_type);
    auto second = specialize(char_type, int_type);
    REQUIRE(first);
    REQUIRE(second);
    CHECK(first != second);
    CHECK(specialize(int_type, byte_type) == first);

    auto shadow = first->get_function("shadow");
    auto plain = first->get_function("plain");
    auto other_shadow = second->get_function("shadow");
    REQUIRE(shadow);
    REQUIRE(plain);
    REQUIRE(other_shadow);
    REQUIRE(shadow->get_tpl_info());
    REQUIRE(shadow->get_parameter("a"));
    REQUIRE(shadow->get_parameter("b"));
    REQUIRE(plain->get_parameter("a"));
    REQUIRE(other_shadow->get_parameter("b"));
    auto shadow_a = shadow->get_parameter("a")->get_generic_source_type();
    auto shadow_b = shadow->get_parameter("b")->get_generic_source_type();
    auto plain_a = plain->get_parameter("a")->get_generic_source_type();
    auto shadow_return = shadow->get_generic_source_return_type();
    REQUIRE(shadow_a);
    REQUIRE(shadow_b);
    REQUIRE(plain_a);
    REQUIRE(shadow_return);
    auto inner_t = std::dynamic_pointer_cast<unresolved_type>(
        shadow_a->get_type()->get_subtype());
    REQUIRE(inner_t);
    REQUIRE(inner_t->source_binder());
    CHECK(inner_t->source_binder()->scope == shadow->get_tpl_info()->binder_scope);
    CHECK(inner_t->source_binder()->scope != host->get_tpl_info()->binder_scope);
    CHECK(type::are_equal(shadow_b->get_type(), byte_type->get_pointer()));
    CHECK(type::are_equal(shadow_return->get_type(), byte_type->get_pointer()));
    CHECK(type::are_equal(plain_a->get_type(), int_type->get_pointer()));
    auto other_b = other_shadow->get_parameter("b")->get_generic_source_type();
    REQUIRE(other_b);
    CHECK(type::are_equal(other_b->get_type(), int_type->get_pointer()));
    CHECK(type::are_equal(shadow_b->get_type(), byte_type->get_pointer()));

    bool created = false;
    auto synthesis = template_instantiator::synthesize_generic_function(
        *shadow, root, first, ctx, created);
    REQUIRE(synthesis);
    REQUIRE(created);
    REQUIRE(synthesis->get_parameter("a"));
    REQUIRE(synthesis->get_parameter("b"));
    auto retained = synthesis->get_parameter("b")->get_generic_source_type();
    REQUIRE(retained);
    CHECK(type::are_equal(retained->get_type(), byte_type->get_pointer()));
    auto synth_a = synthesis->get_parameter("a")->get_generic_source_type();
    REQUIRE(synth_a);
    auto synth_t = std::dynamic_pointer_cast<unresolved_type>(
        synth_a->get_type()->get_subtype());
    REQUIRE(synth_t);
    REQUIRE(synth_t->source_binder());
    CHECK(*synth_t->source_binder() == *inner_t->source_binder());

    auto bound = first->define_function("bound", false);
    REQUIRE(bound);
    template_instantiator::populate_function_from_template_public(
        bound, *shadow, {{"T", char_type}}, {}, {}, true);
    REQUIRE(bound->get_parameter("a"));
    REQUIRE(bound->get_parameter("b"));
    auto bound_a = bound->get_parameter("a")->get_generic_source_type();
    auto bound_b = bound->get_parameter("b")->get_generic_source_type();
    REQUIRE(bound_a);
    REQUIRE(bound_b);
    CHECK(type::are_equal(bound_a->get_type(), char_type->get_pointer()));
    CHECK(type::are_equal(bound_b->get_type(), byte_type->get_pointer()));
    CHECK_FALSE(bound_a->has_unknown_bindings());
    CHECK(type::are_equal(shadow_b->get_type(), byte_type->get_pointer()));

    auto unknown = first->define_function("unknown", false);
    REQUIRE(unknown);
    template_instantiator::populate_function_from_template_public(
        unknown, *shadow, {{"T", byte_type->get_erased_param()}}, {}, {}, true);
    REQUIRE(unknown->get_parameter("a"));
    REQUIRE(unknown->get_parameter("b"));
    auto unknown_a = unknown->get_parameter("a")->get_generic_source_type();
    auto known_b = unknown->get_parameter("b")->get_generic_source_type();
    REQUIRE(unknown_a);
    REQUIRE(known_b);
    CHECK(unknown_a->has_unknown_bindings());
    CHECK_FALSE(known_b->has_unknown_bindings());
    CHECK(type::are_equal(known_b->get_type(), byte_type->get_pointer()));
    CHECK(unknown_a->substitute({})->has_unknown_bindings());
    CHECK(shadow_a->substitute({{*inner_t->source_binder(), byte_type->get_erased_param()}})
              ->has_unknown_bindings());

    ctx->push_template_param_scope({"Caller"});
    auto caller_placeholder = ctx->from_string("Caller");
    ctx->pop_template_param_scope();
    auto unresolved = first->define_function("unresolved", false);
    REQUIRE(unresolved);
    template_instantiator::populate_function_from_template_public(
        unresolved, *shadow, {{"T", caller_placeholder}}, {}, {}, true);
    REQUIRE(unresolved->get_parameter("a"));
    auto unresolved_source = unresolved->get_parameter("a")->get_generic_source_type();
    REQUIRE(unresolved_source);
    CHECK(unresolved_source->has_unknown_bindings());

    std::weak_ptr<const template_binder_scope> weak_caller;
    std::shared_ptr<generic_source_type> symbolic;
    {
        auto caller_scope = std::make_shared<const template_binder_scope>();
        weak_caller = caller_scope;
        auto caller_binder = unresolved_type::make_source_binder(
            k::name("Caller"), {caller_scope, 0});
        auto caller_pointer = type::make_pinned_pointer(caller_binder);
        symbolic = shadow_a->substitute({{*inner_t->source_binder(), caller_pointer}});
    }
    REQUIRE(symbolic);
    CHECK_FALSE(weak_caller.expired());
    CHECK_FALSE(symbolic->has_unknown_bindings());
    auto pointer = symbolic->get_type()->get_subtype();
    REQUIRE(pointer);
    auto caller = std::dynamic_pointer_cast<unresolved_type>(pointer->get_subtype());
    REQUIRE(caller);
    REQUIRE(caller->source_binder());
    CHECK(caller->source_binder()->scope == weak_caller.lock());
    caller.reset();
    pointer.reset();
    symbolic.reset();
    CHECK(weak_caller.expired());
}

TEST_CASE("Constraint source binders survive enclosing specialization and erasure",
          "[model][generic-source][constraint-source]") {
    using namespace k::model;
    auto comp = compile_model(R"SRC(
        module constraint_source_identity;
        class Base {}
        namespace other { class Base {} }
        class First {}
        class Second {}
        template<typename T> class Family {}
        template<class Base>
        struct Classic {
            public:
            generic<class U : Base> bound(value : U*) : int { return 1; }
            generic<class Base : Base> shadow(value : Base*) : int { return 1; }
            generic<class U : other::Base> nominal(value : U*) : int { return 1; }
            generic<class U : Family<Base> > compound(value : U*) : int { return 1; }
            generic<class Base, class U : Base> earlier(value : U*) : int { return 1; }
            generic<class Base, class U : Family<Base> > earlier_compound(value : U*) : int { return 1; }
            generic<class Base, class U : other::Base> qualified(value : U*) : int { return 1; }
            generic<class U : Base> static struct Inner {}
            template<class U : Base> Classic(value : U*) {}
        }
        generic<class Base>
        struct Generic {
            public:
            generic<class Base : Base> shadow(value : Base*) : int { return 1; }
            template<class U : Base> Generic(value : U*) {}
        }
        generic<class Base : Base> free(value : Base*) : int { return 1; }
    )SRC");
    REQUIRE(comp);
    auto unit = comp->get_unit();
    auto root = unit->get_root_namespace();
    auto classic = root->get_aggregate("Classic");
    auto generic = root->get_aggregate("Generic");
    auto first_class = root->get_aggregate("First");
    auto second_class = root->get_aggregate("Second");
    auto free_fn = root->get_function("free");
    REQUIRE(classic);
    REQUIRE(generic);
    REQUIRE(first_class);
    REQUIRE(second_class);
    REQUIRE(free_fn);
    REQUIRE(classic->get_tpl_info());
    REQUIRE(generic->get_tpl_info());
    REQUIRE(free_fn->get_tpl_info());
    auto inner = classic->get_aggregate("Inner");
    REQUIRE(inner);
    REQUIRE(inner->get_tpl_info());
    REQUIRE(inner->get_tpl_info()->params[0].constraint_source_type);
    auto first_type = first_class->get_struct_type();
    auto second_type = second_class->get_struct_type();
    REQUIRE(first_type);
    REQUIRE(second_type);

    auto bound = classic->get_function("bound");
    auto shadow = classic->get_function("shadow");
    auto nominal = classic->get_function("nominal");
    auto compound = classic->get_function("compound");
    auto earlier = classic->get_function("earlier");
    auto earlier_compound = classic->get_function("earlier_compound");
    auto qualified = classic->get_function("qualified");
    REQUIRE(bound);
    REQUIRE(shadow);
    REQUIRE(nominal);
    REQUIRE(compound);
    REQUIRE(earlier);
    REQUIRE(earlier_compound);
    REQUIRE(qualified);
    REQUIRE(bound->get_tpl_info());
    REQUIRE(shadow->get_tpl_info());
    REQUIRE(nominal->get_tpl_info());
    REQUIRE(compound->get_tpl_info());
    REQUIRE(earlier->get_tpl_info());
    REQUIRE(earlier_compound->get_tpl_info());
    REQUIRE(qualified->get_tpl_info());
    auto bound_source = bound->get_tpl_info()->params[0].constraint_source_type;
    auto shadow_source = shadow->get_tpl_info()->params[0].constraint_source_type;
    auto nominal_source = nominal->get_tpl_info()->params[0].constraint_source_type;
    auto compound_source = compound->get_tpl_info()->params[0].constraint_source_type;
    auto free_source = free_fn->get_tpl_info()->params[0].constraint_source_type;
    auto earlier_source = earlier->get_tpl_info()->params[1].constraint_source_type;
    auto earlier_compound_source = earlier_compound->get_tpl_info()->params[1].constraint_source_type;
    auto qualified_source = qualified->get_tpl_info()->params[1].constraint_source_type;
    REQUIRE(bound_source);
    REQUIRE(shadow_source);
    REQUIRE(nominal_source);
    REQUIRE(compound_source);
    REQUIRE(free_source);
    REQUIRE(earlier_source);
    REQUIRE(earlier_compound_source);
    REQUIRE(qualified_source);
    auto outer_binder = std::dynamic_pointer_cast<unresolved_type>(bound_source->get_type());
    auto shadow_binder = std::dynamic_pointer_cast<unresolved_type>(shadow_source->get_type());
    auto ordinary = std::dynamic_pointer_cast<unresolved_type>(nominal_source->get_type());
    auto free_nominal = std::dynamic_pointer_cast<unresolved_type>(free_source->get_type());
    REQUIRE(outer_binder);
    REQUIRE(shadow_binder);
    REQUIRE(ordinary);
    REQUIRE(free_nominal);
    REQUIRE(outer_binder->source_binder());
    REQUIRE(shadow_binder->source_binder());
    auto inner_binder = std::dynamic_pointer_cast<unresolved_type>(
        inner->get_tpl_info()->params[0].constraint_source_type->get_type());
    REQUIRE(inner_binder);
    REQUIRE(inner_binder->source_binder());
    CHECK(*inner_binder->source_binder() == *outer_binder->source_binder());
    CHECK(outer_binder->source_binder()->scope == classic->get_tpl_info()->binder_scope);
    CHECK(*shadow_binder->source_binder() == *outer_binder->source_binder());
    CHECK(shadow_binder->source_binder()->scope != shadow->get_tpl_info()->binder_scope);
    CHECK_FALSE(ordinary->source_binder());
    CHECK(ordinary->type_id().to_string() == "other::Base");
    CHECK_FALSE(ordinary->is_resolved());
    CHECK_FALSE(free_nominal->source_binder());
    CHECK(free_nominal->type_id().to_string() == "Base");
    CHECK_FALSE(bound_source->has_unknown_bindings());
    CHECK_FALSE(free_source->has_unknown_bindings());
    CHECK(earlier_source->has_unknown_bindings());
    CHECK(earlier_compound_source->has_unknown_bindings());
    auto earlier_name = std::dynamic_pointer_cast<unresolved_type>(earlier_source->get_type());
    REQUIRE(earlier_name);
    CHECK_FALSE(earlier_name->source_binder());
    CHECK_FALSE(qualified_source->has_unknown_bindings());
    auto compound_type = std::dynamic_pointer_cast<unresolved_type>(compound_source->get_type());
    REQUIRE(compound_type);
    REQUIRE(compound_type->get_model_template_args().size() == 1);
    auto compound_binder = std::dynamic_pointer_cast<unresolved_type>(
        compound_type->get_model_template_args()[0]);
    REQUIRE(compound_binder);
    REQUIRE(compound_binder->source_binder());
    CHECK(*compound_binder->source_binder() == *outer_binder->source_binder());
    CHECK_FALSE(compound_source->has_unmodelled_arguments());

    auto ctx = comp->get_context_for_test();
    auto free_raw = std::dynamic_pointer_cast<unresolved_type>(
        free_fn->get_tpl_info()->params[0].constraint_type);
    REQUIRE(free_raw);
    REQUIRE(ctx->resolve_type(free_raw));
    CHECK(free_raw->is_resolved());
    CHECK_FALSE(free_nominal->is_resolved());
    test_logger logger;
    auto specialize = [&](const std::shared_ptr<type>& arg) {
        return template_instantiator::instantiate_aggregate(
            *classic, {template_argument::make_type(arg)}, root, *unit, ctx, logger);
    };
    auto first = specialize(first_type);
    auto second = specialize(second_type);
    REQUIRE(first);
    REQUIRE(second);
    CHECK(first != second);
    CHECK(specialize(first_type) == first);
    auto first_bound = first->get_function("bound");
    auto second_bound = second->get_function("bound");
    auto first_shadow = first->get_function("shadow");
    auto first_nominal = first->get_function("nominal");
    auto first_compound = first->get_function("compound");
    auto first_earlier = first->get_function("earlier");
    REQUIRE(first_bound);
    REQUIRE(second_bound);
    REQUIRE(first_shadow);
    REQUIRE(first_nominal);
    REQUIRE(first_compound);
    REQUIRE(first_earlier);
    auto first_info = first_bound->get_tpl_info();
    auto second_info = second_bound->get_tpl_info();
    REQUIRE(first_info);
    REQUIRE(second_info);
    REQUIRE(first_shadow->get_tpl_info());
    REQUIRE(first_nominal->get_tpl_info());
    REQUIRE(first_compound->get_tpl_info());
    REQUIRE(first_earlier->get_tpl_info());
    CHECK(first_info != second_info);
    CHECK(first_info->binder_scope == bound->get_tpl_info()->binder_scope);
    CHECK(second_info->binder_scope == bound->get_tpl_info()->binder_scope);
    REQUIRE(first_info->params[0].constraint_source_type);
    REQUIRE(second_info->params[0].constraint_source_type);
    REQUIRE(first_shadow->get_tpl_info()->params[0].constraint_source_type);
    REQUIRE(first_nominal->get_tpl_info()->params[0].constraint_source_type);
    REQUIRE(first_compound->get_tpl_info()->params[0].constraint_source_type);
    REQUIRE(first_earlier->get_tpl_info()->params[1].constraint_source_type);
    CHECK(first_info->params[0].constraint_source_type->get_type() == first_type);
    CHECK(second_info->params[0].constraint_source_type->get_type() == second_type);
    CHECK(first_shadow->get_tpl_info()->params[0].constraint_source_type->get_type() == first_type);
    auto specialized_nominal = std::dynamic_pointer_cast<unresolved_type>(
        first_nominal->get_tpl_info()->params[0].constraint_source_type->get_type());
    REQUIRE(specialized_nominal);
    CHECK_FALSE(specialized_nominal->source_binder());
    CHECK(specialized_nominal->type_id().to_string() == "other::Base");
    auto specialized_compound = std::dynamic_pointer_cast<unresolved_type>(
        first_compound->get_tpl_info()->params[0].constraint_source_type->get_type());
    REQUIRE(specialized_compound);
    REQUIRE(specialized_compound->get_model_template_args().size() == 1);
    CHECK(specialized_compound->get_model_template_args()[0] == first_type);
    auto specialized_earlier = first_earlier->get_tpl_info()->params[1].constraint_source_type;
    CHECK(specialized_earlier->has_unknown_bindings());
    auto specialized_earlier_name = std::dynamic_pointer_cast<unresolved_type>(
        specialized_earlier->get_type());
    REQUIRE(specialized_earlier_name);
    CHECK_FALSE(specialized_earlier_name->source_binder());
    first_info->instantiations["test"] = std::shared_ptr<function>{};
    first_info->generic_usages["test"] = {};
    CHECK(second_info->instantiations.empty());
    CHECK(second_info->generic_usages.empty());
    CHECK(first_info->params[0].constraint_source_type->substitute({})->get_type() == first_type);
    CHECK(shadow_source->get_type() == shadow_binder);
    bound->get_tpl_info()->params[0].constraint_source_type.reset();
    auto nominal_class = root->get_aggregate("Base");
    REQUIRE(nominal_class);
    auto no_provenance = specialize(nominal_class->get_struct_type());
    bound->get_tpl_info()->params[0].constraint_source_type = bound_source;
    REQUIRE(no_provenance);
    auto unknown_bound = no_provenance->get_function("bound");
    REQUIRE(unknown_bound);
    REQUIRE(unknown_bound->get_tpl_info());
    REQUIRE(unknown_bound->get_tpl_info()->params[0].constraint_source_type);
    CHECK(unknown_bound->get_tpl_info()->params[0].constraint_source_type->has_unknown_bindings());

    REQUIRE_FALSE(classic->constructors().empty());
    REQUIRE_FALSE(first->constructors().empty());
    auto original_ctor = classic->constructors().front();
    auto first_ctor = first->constructors().front();
    REQUIRE(original_ctor);
    REQUIRE(first_ctor);
    REQUIRE(original_ctor->get_tpl_info());
    REQUIRE(first_ctor->get_tpl_info());
    REQUIRE(original_ctor->get_tpl_info()->params[0].constraint_source_type);
    REQUIRE(first_ctor->get_tpl_info()->params[0].constraint_source_type);
    CHECK(first_ctor->get_tpl_info()->binder_scope == original_ctor->get_tpl_info()->binder_scope);
    CHECK(first_ctor->get_tpl_info()->params[0].constraint_source_type->get_type() == first_type);

    auto synthesis = template_instantiator::synthesize_generic_aggregate(
        *generic, root, *unit, ctx, logger);
    REQUIRE(synthesis);
    auto generic_shadow = synthesis->get_function("shadow");
    REQUIRE(generic_shadow);
    REQUIRE(generic_shadow->get_tpl_info());
    auto& generic_param = generic_shadow->get_tpl_info()->params[0];
    REQUIRE(generic_param.constraint_source_type);
    CHECK(type::is_erased_param(generic_param.constraint_type));
    auto generic_binder = std::dynamic_pointer_cast<unresolved_type>(
        generic_param.constraint_source_type->get_type());
    REQUIRE(generic_binder);
    REQUIRE(generic_binder->source_binder());
    CHECK(generic_binder->source_binder()->scope == generic->get_tpl_info()->binder_scope);
    CHECK(generic_binder->source_binder()->scope != generic_shadow->get_tpl_info()->binder_scope);
    CHECK_FALSE(generic_param.constraint_source_type->has_unknown_bindings());
    auto rebound = generic_param.constraint_source_type->substitute(
        {{*generic_binder->source_binder(), first_type}});
    CHECK(rebound->get_type() == first_type);
    auto unknown = generic_param.constraint_source_type->substitute(
        {{*generic_binder->source_binder(), ctx->from_type(primitive_type::BYTE)->get_erased_param()}});
    CHECK(unknown->has_unknown_bindings());
    CHECK(unknown->get_type() == generic_binder);
    CHECK(unknown->substitute({})->has_unknown_bindings());
    CHECK_FALSE(generic_param.constraint_source_type->has_unknown_bindings());

    // Re-clone a descriptor whose ABI bound has already been erased.
    auto rehost = root->define_structure("Rehost");
    REQUIRE(rehost);
    auto rehost_info = std::make_unique<tpl_info>();
    rehost_info->binder_scope = generic->get_tpl_info()->binder_scope;
    rehost_info->params = generic->get_tpl_info()->params;
    rehost->set_tpl_info(std::move(rehost_info));
    auto retained_method = rehost->define_function("retained", false);
    REQUIRE(retained_method);
    auto retained_info = std::make_unique<tpl_info>();
    retained_info->binder_scope = generic_shadow->get_tpl_info()->binder_scope;
    retained_info->params = generic_shadow->get_tpl_info()->params;
    retained_method->set_tpl_info(std::move(retained_info));
    auto recloned = template_instantiator::instantiate_aggregate(
        *rehost, {template_argument::make_type(first_type)}, root, *unit, ctx, logger);
    REQUIRE(recloned);
    auto recloned_method = recloned->get_function("retained");
    REQUIRE(recloned_method);
    REQUIRE(recloned_method->get_tpl_info());
    auto& recloned_param = recloned_method->get_tpl_info()->params[0];
    REQUIRE(recloned_param.constraint_source_type);
    CHECK(type::is_erased_param(recloned_param.constraint_type));
    CHECK(recloned_param.constraint_source_type->get_type() == first_type);
    CHECK(generic_param.constraint_source_type->get_type() == generic_binder);

    REQUIRE_FALSE(synthesis->constructors().empty());
    auto generic_ctor = synthesis->constructors().front();
    REQUIRE(generic_ctor);
    REQUIRE(generic_ctor->get_tpl_info());
    REQUIRE(generic_ctor->get_tpl_info()->params[0].constraint_source_type);
    auto ctor_binder = std::dynamic_pointer_cast<unresolved_type>(
        generic_ctor->get_tpl_info()->params[0].constraint_source_type->get_type());
    REQUIRE(ctor_binder);
    REQUIRE(ctor_binder->source_binder());
    CHECK(*ctor_binder->source_binder() == *generic_binder->source_binder());
}

TEST_CASE("Retained constraint source outlives its declaration",
          "[model][generic-source][constraint-source]") {
    using namespace k::model;
    std::shared_ptr<generic_source_type> retained;
    std::weak_ptr<const template_binder_scope> scope;
    auto ctx = context::create();
    {
        tpl_info host;
        host.params.push_back({.name = "B"});
        ctx->push_template_param_scope({"B"});
        auto raw = ctx->from_string("B");
        ctx->pop_template_param_scope();
        template_param_descriptor param;
        param.constraint_type = raw;
        param.constraint_source_type = generic_source_type::capture(raw, {&host});
        auto copied = param;
        retained = copied.constraint_source_type;
        REQUIRE(retained);
        scope = host.binder_scope;
    }
    CHECK_FALSE(scope.expired());
    auto binder = std::dynamic_pointer_cast<unresolved_type>(retained->get_type());
    REQUIRE(binder);
    REQUIRE(binder->source_binder());
    CHECK(binder->source_binder()->scope == scope.lock());
    auto reclone = retained->substitute({});
    retained.reset();
    binder.reset();
    CHECK_FALSE(scope.expired());
    reclone.reset();
    CHECK(scope.expired());

    ctx->push_template_param_scope({"B"});
    auto missing = ctx->from_string("B");
    ctx->pop_template_param_scope();
    auto unknown = generic_source_type::capture(missing, {});
    REQUIRE(unknown);
    CHECK(unknown->has_unknown_bindings());
    CHECK(unknown->substitute({})->has_unknown_bindings());
    auto erased = generic_source_type::capture(
        ctx->from_type(primitive_type::BYTE)->get_erased_param(), {});
    REQUIRE(erased);
    CHECK(erased->has_unknown_bindings());
}

TEST_CASE("Constraint analysis distinguishes model evidence from unknowns",
          "[model][constraint-analysis]") {
    using namespace k::model;
    using state = constraint_analysis_state;
    using reason = constraint_analysis_reason;
    auto comp = compile_model(R"SRC(
        module constraint_analysis;
        class Animal {}
        class Dog : Animal {}
        class Car {}
        struct Record {}
        generic<typename T> class Generic {}
    )SRC");
    REQUIRE(comp);
    auto root = comp->get_unit()->get_root_namespace();
    auto animal = root->get_aggregate("Animal");
    auto dog = root->get_aggregate("Dog");
    auto car = root->get_aggregate("Car");
    auto record = root->get_aggregate("Record");
    REQUIRE(animal);
    REQUIRE(dog);
    REQUIRE(car);
    REQUIRE(record);
    auto animal_type = animal->get_struct_type();
    auto dog_type = dog->get_struct_type();
    auto car_type = car->get_struct_type();
    auto record_type = record->get_struct_type();
    REQUIRE(animal_type);
    REQUIRE(dog_type);
    REQUIRE(car_type);
    REQUIRE(record_type);
    auto ctx = comp->get_context_for_test();

    tpl_info info;
    template_param_descriptor param;
    param.kind = template_param_kind::CLASS;
    param.name = "T";
    param.constraint_type = animal_type;
    param.constraint_source_type = generic_source_type::capture(animal_type, {});
    info.params.push_back(param);
    auto analyze = [&](const std::shared_ptr<type>& t) {
        return analyze_template_arg_constraints(
            info, {template_argument::make_type(t)}, {generic_source_type::capture(t, {})});
    };
    auto same = analyze(animal_type);
    REQUIRE(same.checks.size() == 1);
    CHECK(same.state == state::verified);
    CHECK(same.checks[0].reason == reason::none);
    CHECK(same.declaration_scope == info.binder_scope);
    CHECK(same.checks[0].parameter_index == 0);
    CHECK(same.checks[0].argument_index == 0);
    auto missing_argument_source = analyze_template_arg_constraints(
        info, {template_argument::make_type(animal_type)});
    CHECK(missing_argument_source.state == state::deferred);
    CHECK(missing_argument_source.checks[0].reason == reason::argument_source_unknown);
    CHECK(analyze(dog_type).state == state::verified);
    auto unrelated = analyze(car_type);
    REQUIRE(unrelated.checks.size() == 1);
    CHECK(unrelated.state == state::invalid);
    CHECK(unrelated.checks[0].reason == reason::constraint);
    auto wrong_kind = analyze(record_type);
    CHECK(wrong_kind.state == state::invalid);
    CHECK(wrong_kind.checks[0].reason == reason::kind);
    auto primitive = analyze(ctx->from_type(primitive_type::INT));
    CHECK(primitive.state == state::invalid);
    CHECK(primitive.checks[0].reason == reason::not_aggregate);

    info.params[0].constraint_source_type.reset();
    auto missing_origin = analyze(animal_type);
    CHECK(missing_origin.state == state::deferred);
    CHECK(missing_origin.checks[0].reason == reason::bound_source_unknown);
    info.params[0].constraint_source_type = generic_source_type::capture(car_type, {});
    CHECK(analyze(animal_type).checks[0].reason == reason::bound_source_unknown);
    info.params[0].constraint_source_type = param.constraint_source_type;
    auto wrong_source = analyze_template_arg_constraints(
        info, {template_argument::make_type(animal_type)},
        {generic_source_type::capture(car_type, {})});
    CHECK(wrong_source.state == state::deferred);
    CHECK(wrong_source.checks[0].reason == reason::argument_source_unknown);

    car->add_base("MissingBase");
    auto incomplete = analyze(car_type);
    CHECK(incomplete.state == state::deferred);
    CHECK(incomplete.checks[0].reason == reason::incomplete_hierarchy);

    REQUIRE(dog->get_bases_mutable().size() == 1);
    auto& dog_base = dog->get_bases_mutable()[0];
    auto original_base = dog_base.base;
    dog_base.base = dog;
    auto cyclic = analyze(dog_type);
    dog_base.base = original_base;
    CHECK(cyclic.state == state::deferred);
    CHECK(cyclic.checks[0].reason == reason::incomplete_hierarchy);

    auto missing = ctx->from_string("MissingBase");
    info.params[0].constraint_type = missing;
    info.params[0].constraint_source_type = generic_source_type::capture(missing, {});
    auto unresolved = analyze(animal_type);
    CHECK(unresolved.state == state::deferred);
    CHECK(unresolved.checks[0].reason == reason::unresolved_bound);
    auto no_aggregate = std::make_shared<struct_type>(
        "MissingAggregate", std::weak_ptr<aggregate>{});
    info.params[0].constraint_type = no_aggregate;
    info.params[0].constraint_source_type = generic_source_type::capture(no_aggregate, {});
    auto incomplete_bound = analyze(animal_type);
    CHECK(incomplete_bound.state == state::deferred);
    CHECK(incomplete_bound.checks[0].reason == reason::bound_aggregate_missing);
    auto incomplete_arg = analyze(no_aggregate);
    CHECK(incomplete_arg.state == state::deferred);
    CHECK(incomplete_arg.checks[0].reason == reason::argument_aggregate_missing);
    CHECK(same.params[0].constraint_type == animal_type);
    CHECK(same.state == state::verified);

    info.params[0].constraint_type = animal_type;
    info.params[0].constraint_source_type = param.constraint_source_type;
    auto erased_type = ctx->from_type(primitive_type::BYTE)->get_erased_param();
    auto erased_arg = analyze_template_arg_constraints(
        info, {template_argument::make_type(erased_type)});
    CHECK(erased_arg.state == state::deferred);
    CHECK(erased_arg.checks[0].reason == reason::erased_argument);
    info.params[0].constraint_type = erased_type;
    auto erased_bound = analyze(animal_type);
    CHECK(erased_bound.state == state::deferred);
    CHECK(erased_bound.checks[0].reason == reason::erased_bound);

    info.is_generic = true;
    auto owner_source = generic_source_type::capture(
        ctx->from_type(primitive_type::INT)->get_owner(), {});
    auto owner = analyze_template_arg_constraints(
        info, {template_argument::make_type(owner_source->get_type())}, {owner_source});
    CHECK(owner.state == state::invalid);
    CHECK(owner.checks[0].reason == reason::generic_owner_arg);
    auto drain_source = generic_source_type::capture(
        ctx->from_type(primitive_type::INT)->get_drain(), {});
    auto drain = analyze_template_arg_constraints(
        info, {template_argument::make_type(drain_source->get_type())}, {drain_source});
    CHECK(drain.state == state::invalid);
    CHECK(drain.checks[0].reason == reason::generic_drain_arg);

    auto generic = root->get_aggregate("Generic");
    REQUIRE(generic);
    test_logger logger;
    auto synthesis = template_instantiator::synthesize_generic_aggregate(
        *generic, root, *comp->get_unit(), ctx, logger);
    REQUIRE(synthesis);
    auto synthetic_type = std::make_shared<struct_type>(
        "Generic", std::weak_ptr<aggregate>{synthesis});
    auto erased_identity = analyze(synthetic_type);
    CHECK(erased_identity.state == state::deferred);
    CHECK(erased_identity.checks[0].reason == reason::argument_source_unknown);
}

TEST_CASE("Constraint analysis retains distinct symbolic binders and unknowns",
          "[model][constraint-analysis]") {
    using namespace k::model;
    using state = constraint_analysis_state;
    using reason = constraint_analysis_reason;
    auto comp = compile_model("module constraint_analysis_symbolic; class Animal {}");
    REQUIRE(comp);
    auto animal = comp->get_unit()->get_root_namespace()->get_aggregate("Animal");
    REQUIRE(animal);
    auto animal_type = animal->get_struct_type();
    REQUIRE(animal_type);
    auto ctx = comp->get_context_for_test();
    auto erased = ctx->from_type(primitive_type::BYTE)->get_erased_param();
    auto animal_source = generic_source_type::capture(animal_type, {});
    template_constraint_analysis retained;
    std::weak_ptr<const template_binder_scope> outer_lifetime;
    std::weak_ptr<const template_binder_scope> inner_lifetime;
    {
        tpl_info outer;
        outer.params.push_back({.name = "B"});
        tpl_info inner;
        inner.params.push_back({.name = "B"});
        auto outer_type = unresolved_type::make_source_binder(
            k::name("B"), {outer.binder_scope, 0});
        auto inner_type = unresolved_type::make_source_binder(
            k::name("B"), {inner.binder_scope, 0});
        auto bound_source = generic_source_type::capture(outer_type, {});
        auto argument_source = generic_source_type::capture(inner_type, {});
        template_param_descriptor param;
        param.name = "T";
        param.constraint_type = animal_type;
        param.constraint_source_type = bound_source;
        tpl_info target;
        target.params.push_back(param);

        retained = analyze_template_arg_constraints(
            target, {template_argument::make_type(erased)}, {argument_source});
        CHECK(retained.state == state::deferred);
        REQUIRE(retained.checks.size() == 1);
        CHECK(retained.checks[0].reason == reason::symbolic_argument);
        auto bound = std::dynamic_pointer_cast<unresolved_type>(
            retained.params[0].constraint_source_type->get_type());
        auto argument = std::dynamic_pointer_cast<unresolved_type>(
            retained.argument_sources[0]->get_type());
        REQUIRE(bound);
        REQUIRE(argument);
        REQUIRE(bound->source_binder());
        REQUIRE(argument->source_binder());
        CHECK(bound->source_binder()->scope == outer.binder_scope);
        CHECK(argument->source_binder()->scope == inner.binder_scope);
        CHECK(*bound->source_binder() != *argument->source_binder());
        outer_lifetime = outer.binder_scope;
        inner_lifetime = inner.binder_scope;

        auto source_argument = analyze_template_arg_constraints(
            target, {template_argument::make_type(animal_type)}, {argument_source});
        CHECK(source_argument.state == state::deferred);
        CHECK(source_argument.checks[0].reason == reason::symbolic_argument);
        auto symbolic_bound = analyze_template_arg_constraints(
            target, {template_argument::make_type(animal_type)}, {animal_source});
        CHECK(symbolic_bound.state == state::deferred);
        CHECK(symbolic_bound.checks[0].reason == reason::symbolic_bound);
        target.params[0].constraint_type = erased;
        auto dependent = analyze_template_arg_constraints(
            target, {template_argument::make_type(animal_type)}, {animal_source});
        CHECK(dependent.state == state::deferred);
        CHECK(dependent.checks[0].reason == reason::erased_bound);
        target.params[0].constraint_source_type = bound_source->substitute(
            {{*outer_type->source_binder(), nullptr}});
        target.params[0].constraint_type = animal_type;
        auto unknown = analyze_template_arg_constraints(
            target, {template_argument::make_type(animal_type)}, {animal_source});
        CHECK(unknown.state == state::deferred);
        CHECK(unknown.params[0].constraint_source_type->has_unknown_bindings());
    }
    CHECK_FALSE(outer_lifetime.expired());
    CHECK_FALSE(inner_lifetime.expired());
    retained = {};
    CHECK(outer_lifetime.expired());
    CHECK(inner_lifetime.expired());
}

TEST_CASE("Constraint analysis does not verify unanalysed argument forms",
          "[model][constraint-analysis]") {
    using namespace k::model;
    using state = constraint_analysis_state;
    using reason = constraint_analysis_reason;
    auto ctx = context::create();
    auto int_type = ctx->from_type(primitive_type::INT);
    auto int_source = generic_source_type::capture(int_type, {});
    tpl_info info;
    info.params.push_back({.name = "T"});
    auto verified = analyze_template_arg_constraints(
        info, {template_argument::make_type(int_type)}, {int_source});
    CHECK(verified.state == state::verified);
    auto scope = info.binder_scope;
    info.binder_scope.reset();
    auto no_scope = analyze_template_arg_constraints(
        info, {template_argument::make_type(int_type)}, {int_source});
    CHECK(no_scope.state == state::deferred);
    CHECK(no_scope.checks[0].reason == reason::declaration_provenance);
    info.binder_scope = scope;
    auto malformed_arg = template_argument::make_type(int_type);
    malformed_arg.value_arg = k::value_type{1};
    auto malformed = analyze_template_arg_constraints(info, {malformed_arg}, {int_source});
    CHECK(malformed.state == state::deferred);
    CHECK(malformed.checks[0].reason == reason::argument_shape);
    auto missing = analyze_template_arg_constraints(info, {});
    CHECK(missing.state == state::deferred);
    CHECK(missing.checks[0].reason == reason::missing_argument);
    auto extra = analyze_template_arg_constraints(
        info, {template_argument::make_type(int_type), template_argument::make_type(int_type)},
        {int_source, int_source});
    CHECK(extra.state == state::deferred);
    REQUIRE(extra.checks.size() == 2);
    CHECK(extra.checks[1].reason == reason::extra_argument);
    info.params[0].is_pack = true;
    auto pack = analyze_template_arg_constraints(
        info, {template_argument::make_pack({int_type})});
    CHECK(pack.state == state::deferred);
    CHECK(pack.checks[0].reason == reason::parameter_pack);
    CHECK(pack.args[0].pack_types[0] == int_type);
    info.params[0].is_pack = false;
    info.params[0].kind = template_param_kind::VALUE;
    auto value = analyze_template_arg_constraints(info, {template_argument::make_value(1)});
    CHECK(value.state == state::deferred);
    CHECK(value.checks[0].reason == reason::value_parameter);

    info.params[0].kind = template_param_kind::CLASS;
    info.params[0].constraint_type = ctx->from_string("Missing");
    info.params.push_back({.kind = template_param_kind::CLASS, .name = "U"});
    auto mixed = analyze_template_arg_constraints(
        info, {template_argument::make_type(ctx->from_type(primitive_type::BYTE)->get_erased_param()),
               template_argument::make_type(int_type)}, {nullptr, int_source});
    CHECK(mixed.state == state::invalid);
    REQUIRE(mixed.checks.size() == 2);
    CHECK(mixed.checks[0].state == state::deferred);
    CHECK(mixed.checks[1].reason == reason::not_aggregate);

    template_constraint_analysis retained;
    CHECK(retained.state == state::deferred);
    std::weak_ptr<type> weak_subtype;
    {
        auto short_context = context::create();
        auto subtype = short_context->from_type(primitive_type::BYTE);
        weak_subtype = subtype;
        tpl_info one;
        one.params.push_back({.name = "T"});
        retained = analyze_template_arg_constraints(
            one, {template_argument::make_type(subtype->get_pointer())},
            {generic_source_type::capture(subtype->get_pointer(), {})});
        CHECK(retained.state == state::deferred);
    }
    CHECK_FALSE(weak_subtype.expired());
    CHECK(retained.args[0].type_arg->get_subtype() == weak_subtype.lock());
    retained = {};
    CHECK(weak_subtype.expired());
}

TEST_CASE("Nested generic descriptors survive classic outer specialization",
          "[model][generic-source][nested-generic-descriptor]") {
    using namespace k::model;
    auto comp = compile_model(R"SRC(
        module generic_source_nested_descriptor;
        template<typename T>
        struct Outer {
            public:
            generic<typename U> static struct Inner {
                public:
                from_outer : T*;
                own : U*;
                outer_arg(value : T*) : T* { return value; }
            }
            generic<typename T> static struct Shadow {
                public: own : T*;
            }
        }
    )SRC");
    REQUIRE(comp);
    auto unit = comp->get_unit();
    REQUIRE(unit);
    auto root = unit->get_root_namespace();
    REQUIRE(root);
    auto outer = root->get_aggregate("Outer");
    REQUIRE(outer);
    REQUIRE(outer->get_tpl_info());
    auto inner = outer->get_aggregate("Inner");
    auto shadow = outer->get_aggregate("Shadow");
    REQUIRE(inner);
    REQUIRE(shadow);
    REQUIRE(inner->is_generic());
    REQUIRE(shadow->is_generic());
    auto inner_info = inner->get_tpl_info();
    auto shadow_info = shadow->get_tpl_info();
    REQUIRE(inner_info);
    REQUIRE(shadow_info);
    REQUIRE(inner_info->params.size() == 1);
    REQUIRE(shadow_info->params.size() == 1);
    auto original_outer_field = inner->get_variable("from_outer");
    REQUIRE(original_outer_field);
    REQUIRE(original_outer_field->get_type());
    auto outer_placeholder = original_outer_field->get_type()->get_subtype();
    REQUIRE(outer_placeholder);
    // Exercise descriptor cloning directly; no constraint validation is performed here.
    inner_info->params[0].constraint_type = outer_placeholder;
    inner_info->params[0].default_type = outer_placeholder;
    shadow_info->params[0].default_type = outer_placeholder;
    auto private_scope = root->get_child_namespace("nested_identity_private");
    REQUIRE(private_scope);
    outer->get_tpl_info()->private_definition_scope = private_scope;
    auto cached = root->define_structure("nested_identity_cached");
    REQUIRE(cached);
    inner_info->instantiations["<generic_synthesis>"] = std::static_pointer_cast<aggregate>(cached);
    inner_info->generic_usages["original"] = {};

    auto ctx = comp->get_context_for_test();
    REQUIRE(ctx);
    auto int_type = ctx->from_type(primitive_type::INT);
    auto byte_type = ctx->from_type(primitive_type::BYTE);
    REQUIRE(int_type);
    REQUIRE(byte_type);
    test_logger logger;
    auto first = template_instantiator::instantiate_aggregate(
        *outer, {template_argument::make_type(int_type)}, root, *unit, ctx, logger);
    auto second = template_instantiator::instantiate_aggregate(
        *outer, {template_argument::make_type(byte_type)}, root, *unit, ctx, logger);
    REQUIRE(first);
    REQUIRE(second);
    REQUIRE(first != second);
    auto first_inner = first->get_aggregate("Inner");
    auto second_inner = second->get_aggregate("Inner");
    auto first_shadow = first->get_aggregate("Shadow");
    REQUIRE(first_inner);
    REQUIRE(second_inner);
    REQUIRE(first_shadow);

    REQUIRE(first_inner->get_tpl_info());
    REQUIRE(second_inner->get_tpl_info());
    REQUIRE(first_shadow->get_tpl_info());
    CHECK(first_inner->is_generic());
    CHECK(second_inner->is_generic());
    CHECK(first_shadow->is_generic());
    CHECK(first_inner->get_tpl_info() != second_inner->get_tpl_info());
    CHECK(first_inner->get_tpl_info()->binder_scope == inner_info->binder_scope);
    CHECK(second_inner->get_tpl_info()->binder_scope == inner_info->binder_scope);
    CHECK(first_shadow->get_tpl_info()->binder_scope == shadow_info->binder_scope);
    CHECK(first_shadow->get_tpl_info()->binder_scope != outer->get_tpl_info()->binder_scope);
    CHECK(first_inner->get_tpl_info()->private_definition_scope == private_scope);
    CHECK(first_inner->get_tpl_info()->instantiations.empty());
    CHECK(second_inner->get_tpl_info()->instantiations.empty());
    CHECK(first_inner->get_tpl_info()->generic_usages.empty());
    CHECK_FALSE(first_inner->is_instantiation());
    CHECK_FALSE(first_inner->get_struct_type());
    CHECK(first_inner->constructors().empty());
    REQUIRE(first_inner->get_tpl_info()->params.size() == 1);
    REQUIRE(second_inner->get_tpl_info()->params.size() == 1);
    CHECK(first_inner->get_tpl_info()->params[0].name == "U");
    CHECK(type::are_equal(first_inner->get_tpl_info()->params[0].constraint_type, int_type));
    CHECK(type::are_equal(first_inner->get_tpl_info()->params[0].default_type, int_type));
    CHECK(type::are_equal(second_inner->get_tpl_info()->params[0].constraint_type, byte_type));
    CHECK(type::are_equal(second_inner->get_tpl_info()->params[0].default_type, byte_type));
    REQUIRE(first_shadow->get_tpl_info()->params.size() == 1);
    CHECK(first_shadow->get_tpl_info()->params[0].name == "T");
    CHECK(type::are_equal(first_shadow->get_tpl_info()->params[0].default_type, int_type));

    auto first_outer_field = first_inner->get_variable("from_outer");
    auto second_outer_field = second_inner->get_variable("from_outer");
    auto own_field = first_inner->get_variable("own");
    auto shadow_field = first_shadow->get_variable("own");
    REQUIRE(first_outer_field);
    REQUIRE(second_outer_field);
    REQUIRE(own_field);
    REQUIRE(shadow_field);
    auto first_source = first_outer_field->get_generic_source_type();
    auto second_source = second_outer_field->get_generic_source_type();
    auto own_source = own_field->get_generic_source_type();
    auto shadow_source = shadow_field->get_generic_source_type();
    REQUIRE(first_source);
    REQUIRE(second_source);
    REQUIRE(own_source);
    REQUIRE(shadow_source);
    CHECK(type::are_equal(first_source->get_type(), int_type->get_pointer()));
    CHECK(type::are_equal(second_source->get_type(), byte_type->get_pointer()));
    auto own_binder = std::dynamic_pointer_cast<unresolved_type>(
        own_source->get_type()->get_subtype());
    auto shadow_binder = std::dynamic_pointer_cast<unresolved_type>(
        shadow_source->get_type()->get_subtype());
    REQUIRE(own_binder);
    REQUIRE(shadow_binder);
    REQUIRE(own_binder->source_binder());
    REQUIRE(shadow_binder->source_binder());
    CHECK(own_binder->source_binder()->scope == inner_info->binder_scope);
    CHECK(shadow_binder->source_binder()->scope == shadow_info->binder_scope);
    REQUIRE(shadow_field->get_type());
    CHECK(std::dynamic_pointer_cast<unresolved_type>(shadow_field->get_type()->get_subtype()));
    auto outer_arg = first_inner->get_function("outer_arg");
    REQUIRE(outer_arg);
    CHECK(outer_arg->get_template_definition_scope() == private_scope);
    REQUIRE(outer_arg->get_parameter("value"));
    auto outer_arg_source = outer_arg->get_parameter("value")->get_generic_source_type();
    auto outer_return_source = outer_arg->get_generic_source_return_type();
    REQUIRE(outer_arg_source);
    REQUIRE(outer_return_source);
    CHECK(type::are_equal(outer_arg_source->get_type(), int_type->get_pointer()));
    CHECK(type::are_equal(outer_return_source->get_type(), int_type->get_pointer()));

    // The synthesis API accepts namespaces, so isolate same-named model syntheses.
    auto first_ns = root->get_child_namespace("nested_identity_first");
    auto second_ns = root->get_child_namespace("nested_identity_second");
    auto shadow_ns = root->get_child_namespace("nested_identity_shadow");
    auto first_synth = template_instantiator::synthesize_generic_aggregate(
        *first_inner, first_ns, *unit, ctx, logger);
    auto second_synth = template_instantiator::synthesize_generic_aggregate(
        *second_inner, second_ns, *unit, ctx, logger);
    auto shadow_synth = template_instantiator::synthesize_generic_aggregate(
        *first_shadow, shadow_ns, *unit, ctx, logger);
    REQUIRE(first_synth);
    REQUIRE(second_synth);
    REQUIRE(shadow_synth);
    CHECK(first_synth != second_synth);
    CHECK(first_synth->get_tpl_definition() == first_inner);
    CHECK(second_synth->get_tpl_definition() == second_inner);
    CHECK(template_instantiator::synthesize_generic_aggregate(
        *first_inner, first_ns, *unit, ctx, logger) == first_synth);
    auto first_synth_outer = first_synth->get_variable("from_outer");
    auto second_synth_outer = second_synth->get_variable("from_outer");
    auto first_synth_own = first_synth->get_variable("own");
    auto shadow_synth_own = shadow_synth->get_variable("own");
    REQUIRE(first_synth_outer);
    REQUIRE(second_synth_outer);
    REQUIRE(first_synth_own);
    REQUIRE(shadow_synth_own);
    auto first_synth_source = first_synth_outer->get_generic_source_type();
    auto second_synth_source = second_synth_outer->get_generic_source_type();
    auto own_synth_source = first_synth_own->get_generic_source_type();
    auto shadow_synth_source = shadow_synth_own->get_generic_source_type();
    REQUIRE(first_synth_source);
    REQUIRE(second_synth_source);
    REQUIRE(own_synth_source);
    REQUIRE(shadow_synth_source);
    CHECK(type::are_equal(first_synth_source->get_type(), int_type->get_pointer()));
    CHECK(type::are_equal(second_synth_source->get_type(), byte_type->get_pointer()));
    auto own_synth_binder = std::dynamic_pointer_cast<unresolved_type>(
        own_synth_source->get_type()->get_subtype());
    auto shadow_synth_binder = std::dynamic_pointer_cast<unresolved_type>(
        shadow_synth_source->get_type()->get_subtype());
    REQUIRE(own_synth_binder);
    REQUIRE(shadow_synth_binder);
    REQUIRE(own_synth_binder->source_binder());
    REQUIRE(shadow_synth_binder->source_binder());
    CHECK(*own_synth_binder->source_binder() == *own_binder->source_binder());
    CHECK(*shadow_synth_binder->source_binder() == *shadow_binder->source_binder());
    REQUIRE(first_synth_own->get_type());
    CHECK(type::is_erased_param(first_synth_own->get_type()->get_subtype()));
    REQUIRE(shadow_synth_own->get_type());
    CHECK(type::is_erased_param(shadow_synth_own->get_type()->get_subtype()));
    CHECK(inner_info->instantiations.at("<generic_synthesis>") ==
          tpl_info::instantiation_entry{std::static_pointer_cast<aggregate>(cached)});
}

TEST_CASE("Nested classic template specializations retain their enclosing identity",
          "[model][nested-template-descriptor]") {
    using namespace k::model;
    auto comp = compile_model(R"SRC(
        module nested_template_descriptor;
        template<typename T>
        struct Host {
            public:
            template<typename U = T> static struct Inner {
                public:
                outer : T;
                own : U;
            }
            template<typename T> static struct Shadow {
                public:
                own : T*;
                template<typename T>
                Shadow(value : T*) {}
                template<typename T>
                echo(value : T*) : T* { return value; }
            }
            template<typename T, typename U = T> static struct Chained {
                public: value : U;
            }
            static struct Plain { public: value : T; }
        }
    )SRC");
    REQUIRE(comp);
    auto unit = comp->get_unit();
    auto ctx = comp->get_context_for_test();
    REQUIRE(unit);
    REQUIRE(ctx);
    auto root = unit->get_root_namespace();
    REQUIRE(root);
    auto host = root->get_aggregate("Host");
    REQUIRE(host);
    auto inner = host->get_aggregate("Inner");
    REQUIRE(inner);
    REQUIRE(inner->get_tpl_info());
    auto int_type = ctx->from_type(primitive_type::INT);
    auto long_type = ctx->from_type(primitive_type::LONG);
    REQUIRE(int_type);
    REQUIRE(long_type);
    test_logger logger;
    auto int_host = template_instantiator::instantiate_aggregate(
        *host, {template_argument::make_type(int_type)}, root, *unit, ctx, logger);
    auto long_host = template_instantiator::instantiate_aggregate(
        *host, {template_argument::make_type(long_type)}, root, *unit, ctx, logger);
    REQUIRE(int_host);
    REQUIRE(long_host);
    auto int_plain = int_host->get_aggregate("Plain");
    REQUIRE(int_plain);
    CHECK(int_plain->is_instantiation());
    CHECK(int_plain->get_struct_type());
    CHECK_FALSE(int_plain->constructors().empty());

    auto int_inner = int_host->get_aggregate("Inner");
    auto long_inner = long_host->get_aggregate("Inner");
    REQUIRE(int_inner);
    REQUIRE(long_inner);
    REQUIRE(int_inner->get_tpl_info());
    REQUIRE(long_inner->get_tpl_info());
    CHECK_FALSE(int_inner->is_instantiation());
    CHECK_FALSE(int_inner->get_struct_type());
    CHECK(int_inner->constructors().empty());
    CHECK(int_inner->get_tpl_info()->binder_scope == inner->get_tpl_info()->binder_scope);
    REQUIRE(int_inner->get_tpl_info()->params.size() == 1);
    REQUIRE(long_inner->get_tpl_info()->params.size() == 1);
    CHECK(type::are_equal(int_inner->get_tpl_info()->params[0].default_type, int_type));
    CHECK(type::are_equal(long_inner->get_tpl_info()->params[0].default_type, long_type));

    auto shadow = int_host->get_aggregate("Shadow");
    REQUIRE(shadow);
    REQUIRE(shadow->get_tpl_info());
    auto own = shadow->get_variable("own");
    REQUIRE(own);
    REQUIRE(own->get_type());
    CHECK(std::dynamic_pointer_cast<unresolved_type>(own->get_type()->get_subtype()));
    REQUIRE(shadow->constructors().size() == 1);
    auto ctor = shadow->constructors().front();
    REQUIRE(ctor);
    REQUIRE(ctor->get_tpl_info());
    REQUIRE(ctor->get_parameter("value"));
    REQUIRE(ctor->get_parameter("value")->get_type());
    CHECK(std::dynamic_pointer_cast<unresolved_type>(
        ctor->get_parameter("value")->get_type()->get_subtype()));
    auto echo = shadow->get_function("echo");
    REQUIRE(echo);
    REQUIRE(echo->get_tpl_info());
    REQUIRE(echo->get_parameter("value"));
    REQUIRE(echo->get_parameter("value")->get_type());
    CHECK(std::dynamic_pointer_cast<unresolved_type>(
        echo->get_parameter("value")->get_type()->get_subtype()));
    REQUIRE(echo->get_return_type());
    CHECK(std::dynamic_pointer_cast<unresolved_type>(echo->get_return_type()->get_subtype()));

    auto chained = int_host->get_aggregate("Chained");
    REQUIRE(chained);
    REQUIRE(chained->get_tpl_info());
    REQUIRE(chained->get_tpl_info()->params.size() == 2);
    auto chained_default = std::dynamic_pointer_cast<unresolved_type>(
        chained->get_tpl_info()->params[1].default_type);
    REQUIRE(chained_default);
    CHECK(chained_default->type_id().to_string() == "T");

    auto int_instance = template_instantiator::instantiate_aggregate(
        *int_inner, {template_argument::make_type(int_type)}, root, *unit, ctx, logger);
    auto long_instance = template_instantiator::instantiate_aggregate(
        *long_inner, {template_argument::make_type(int_type)}, root, *unit, ctx, logger);
    REQUIRE(int_instance);
    REQUIRE(long_instance);
    CHECK(int_instance != long_instance);
    CHECK(int_instance->get_fq_name() != long_instance->get_fq_name());
    REQUIRE(int_instance->get_variable("outer"));
    REQUIRE(long_instance->get_variable("outer"));
    CHECK(type::are_equal(int_instance->get_variable("outer")->get_type(), int_type));
    CHECK(type::are_equal(long_instance->get_variable("outer")->get_type(), long_type));
}

TEST_CASE("[K] M7: generic uses track concrete arguments without re-synthesis",
          "[milestone7][generic][resolver]") {
    auto comp = compile_model(R"SRC(
        module template_instantiator_08;
        generic<typename T>
        struct Box {
            public value : T&;
        }

        struct Uses {
            public a : Box<int>;
            public b : Box<float>;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto unit = comp->get_unit();
    REQUIRE(unit != nullptr);
    auto root_ns = unit->get_root_namespace();
    REQUIRE(root_ns != nullptr);

    auto box_tpl = root_ns->get_aggregate("Box");
    REQUIRE(box_tpl != nullptr);
    REQUIRE(box_tpl->is_generic());

    auto* ti = box_tpl->get_tpl_info();
    REQUIRE(ti != nullptr);

    auto ctx = comp->get_context_for_test();
    REQUIRE(ctx != nullptr);
    auto int_type = ctx->from_type(k::model::primitive_type::INT);
    auto float_type = ctx->from_type(k::model::primitive_type::FLOAT);
    REQUIRE(int_type != nullptr);
    REQUIRE(float_type != nullptr);

    std::vector<k::model::template_argument> int_args;
    int_args.push_back(k::model::template_argument::make_type(int_type));
    const std::string int_key = k::model::build_instantiation_key(int_args);

    std::vector<k::model::template_argument> float_args;
    float_args.push_back(k::model::template_argument::make_type(float_type));
    const std::string float_key = k::model::build_instantiation_key(float_args);

    auto syn_it = ti->instantiations.find("<generic_synthesis>");
    REQUIRE(syn_it != ti->instantiations.end());

    auto int_it = ti->instantiations.find(int_key);
    auto float_it = ti->instantiations.find(float_key);
    REQUIRE(int_it != ti->instantiations.end());
    REQUIRE(float_it != ti->instantiations.end());

    auto int_usage_it = ti->generic_usages.find(int_key);
    auto float_usage_it = ti->generic_usages.find(float_key);
    REQUIRE(int_usage_it != ti->generic_usages.end());
    REQUIRE(float_usage_it != ti->generic_usages.end());

    auto int_binding_it = int_usage_it->second.type_bindings.find("T");
    auto float_binding_it = float_usage_it->second.type_bindings.find("T");
    REQUIRE(int_binding_it != int_usage_it->second.type_bindings.end());
    REQUIRE(float_binding_it != float_usage_it->second.type_bindings.end());
    CHECK(int_binding_it->second == int_type);
    CHECK(float_binding_it->second == float_type);

    auto syn = std::get<std::shared_ptr<k::model::aggregate>>(syn_it->second);
    auto as_int = std::get<std::shared_ptr<k::model::aggregate>>(int_it->second);
    auto as_float = std::get<std::shared_ptr<k::model::aggregate>>(float_it->second);
    REQUIRE(syn != nullptr);
    REQUIRE(as_int != nullptr);
    REQUIRE(as_float != nullptr);
    CHECK(syn.get() == as_int.get());
    CHECK(syn.get() == as_float.get());
}

TEST_CASE("[L] M7: generic flag preserved for class-kind parameter",
          "[milestone7][generic][model]") {
    auto comp = compile_model(R"SRC(
        module template_instantiator_09;
        generic<class T>
        class Box {
            public value : T&;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto unit = comp->get_unit();
    REQUIRE(unit != nullptr);
    auto root_ns = unit->get_root_namespace();
    REQUIRE(root_ns != nullptr);

    auto tpl = root_ns->get_aggregate("Box");
    REQUIRE(tpl != nullptr);
    REQUIRE(tpl->is_template());
    REQUIRE(tpl->get_tpl_info() != nullptr);
    CHECK(tpl->get_tpl_info()->is_generic);
    CHECK(tpl->is_generic());
}

TEST_CASE("[M] M7: generic flag preserved for module k nested aggregate",
          "[milestone7][generic][model]") {
    auto comp = compile_model(R"SRC(
        module template_instantiator_10;
        generic<class TYPE>
        public class LinkedList {
            private struct Node {
                public val : TYPE&;
            }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto unit = comp->get_unit();
    REQUIRE(unit != nullptr);
    auto root_ns = unit->get_root_namespace();
    REQUIRE(root_ns != nullptr);

    auto tpl = root_ns->get_aggregate("LinkedList");
    REQUIRE(tpl != nullptr);
    REQUIRE(tpl->is_template());
    REQUIRE(tpl->get_tpl_info() != nullptr);
    CHECK(tpl->get_tpl_info()->is_generic);
    CHECK(tpl->is_generic());
}

TEST_CASE("[N] M7: generic flag preserved with optimize=true parse pipeline",
          "[milestone7][generic][model]") {
    auto comp = k::compiler::create();
    REQUIRE(comp != nullptr);

    REQUIRE_NOTHROW(comp->parse_source("", R"SRC(
        module template_instantiator_11;
        generic<class TYPE>
        public class LinkedList {
            private struct Node {
                public val : TYPE&;
            }
        }
    )SRC", /*optimize=*/true, /*dump=*/false));

    auto unit = comp->get_unit();
    REQUIRE(unit != nullptr);
    auto root_ns = unit->get_root_namespace();
    REQUIRE(root_ns != nullptr);

    auto tpl = root_ns->get_aggregate("LinkedList");
    REQUIRE(tpl != nullptr);
    REQUIRE(tpl->is_template());
    REQUIRE(tpl->get_tpl_info() != nullptr);
    CHECK(tpl->get_tpl_info()->is_generic);
    CHECK(tpl->is_generic());
}

TEST_CASE("[O] M7: generic flag preserved in parse_sources with forced module",
          "[milestone7][generic][model]") {
    auto comp = k::compiler::create();
    REQUIRE(comp != nullptr);

    std::vector<std::pair<std::string, std::string>> sources;
    sources.emplace_back("list.k", R"SRC(
        module template_instantiator_12;
        generic<class TYPE>
        public class LinkedList {
            private struct Node {
                public val : TYPE&;
            }
        }
    )SRC");

    REQUIRE_NOTHROW(comp->parse_sources(std::move(sources), /*optimize=*/true, /*dump=*/false, /*forced_module_name=*/"k"));

    auto unit = comp->get_unit();
    REQUIRE(unit != nullptr);
    auto root_ns = unit->get_root_namespace();
    REQUIRE(root_ns != nullptr);

    auto tpl = root_ns->get_aggregate("LinkedList");
    REQUIRE(tpl != nullptr);
    REQUIRE(tpl->is_template());
    REQUIRE(tpl->get_tpl_info() != nullptr);
    CHECK(tpl->get_tpl_info()->is_generic);
    CHECK(tpl->is_generic());
}

TEST_CASE("[P] M7: generic flag survives shared-lib generation and KDI export",
          "[milestone7][generic][e2e][pic]") {
    auto comp = k::compiler::create();
    REQUIRE(comp != nullptr);

    std::vector<std::pair<std::string, std::string>> sources;
    sources.emplace_back("list.k", R"SRC(
        module template_instantiator_13;
        generic<class TYPE>
        public class LinkedList {
            private struct Node {
                public val : TYPE&;
            }
        }
    )SRC");

    REQUIRE_NOTHROW(comp->parse_sources(std::move(sources), /*optimize=*/true, /*dump=*/false, /*forced_module_name=*/"k"));

    auto root_ns = comp->get_unit()->get_root_namespace();
    REQUIRE(root_ns != nullptr);
    auto tpl_before = root_ns->get_aggregate("LinkedList");
    REQUIRE(tpl_before != nullptr);
    REQUIRE(tpl_before->get_tpl_info() != nullptr);
    CHECK(tpl_before->get_tpl_info()->is_generic);

    const auto out_so = (std::filesystem::temp_directory_path() / "klang_m7_p.so").string();
    REQUIRE(comp->gen_shared_library(out_so));

    auto tpl_after = root_ns->get_aggregate("LinkedList");
    REQUIRE(tpl_after != nullptr);
    REQUIRE(tpl_after->get_tpl_info() != nullptr);
    CHECK(tpl_after->get_tpl_info()->is_generic);

    const auto out_kdi = (std::filesystem::path(out_so).replace_extension(".kdi")).string();
    auto kdi = kdi::kdi_read_cbor_file(out_kdi);

    bool found = false;
    for (const auto& td : kdi.unit.root_ns.template_defs) {
        if (td.name == "LinkedList") {
            CHECK(td.is_generic);
            CHECK(td.aggregate_signature != nullptr);
            found = true;
        }
    }
    CHECK(found);
}

TEST_CASE("[Q] M7: generic KDI export with file-backed source and absolute path",
          "[milestone7][generic][e2e][pic]") {
    const auto src_path = (std::filesystem::temp_directory_path() / "klang_m7_q.k").string();
    const auto out_so = (std::filesystem::temp_directory_path() / "klang_m7_q.so").string();

    {
        std::ofstream os(src_path);
        REQUIRE(os.is_open());
        os << R"SRC(
module template_instantiator_14;
generic<class TYPE>
public class LinkedList {
    private struct Node {
        public val: TYPE&;
    }
}
)SRC";
    }

    std::ifstream is(src_path);
    REQUIRE(is.is_open());
    std::stringstream buffer;
    buffer << is.rdbuf();

    auto comp = k::compiler::create();
    REQUIRE(comp != nullptr);

    std::vector<std::pair<std::string, std::string>> sources;
    sources.emplace_back(src_path, buffer.str());

    REQUIRE_NOTHROW(comp->parse_sources(std::move(sources), /*optimize=*/true, /*dump=*/false, /*forced_module_name=*/"k"));
    REQUIRE(comp->gen_shared_library(out_so));

    const auto out_kdi = (std::filesystem::path(out_so).replace_extension(".kdi")).string();
    auto kdi = kdi::kdi_read_cbor_file(out_kdi);

    bool found = false;
    for (const auto& td : kdi.unit.root_ns.template_defs) {
        if (td.name == "LinkedList") {
            CHECK(td.is_generic);
            CHECK(td.aggregate_signature != nullptr);
            found = true;
        }
    }
    CHECK(found);
}

TEST_CASE("Native libraries: default and static targets emit PIC RTTI manifests",
          "[native-library][pic][model]") {
    const bool explicit_static_target = GENERATE(false, true);
    const auto output_kind = GENERATE(std::string{"shared"}, std::string{"static"},
                                     std::string{"both"});
    CAPTURE(explicit_static_target, output_kind);

    std::unique_ptr<llvm::TargetMachine> static_target;
    if (explicit_static_target) {
        std::unique_ptr<llvm::TargetMachine> pic_target{make_pic_target_machine()};
        REQUIRE(pic_target != nullptr);
        static_target.reset(pic_target->getTarget().createTargetMachine(
            k::llvm_target_triple_arg(pic_target->getTargetTriple()),
            pic_target->getTargetCPU(), pic_target->getTargetFeatureString(),
            pic_target->Options, llvm::Reloc::Static,
            pic_target->getCodeModel(), pic_target->getOptLevel()));
        REQUIRE(static_target != nullptr);
    }
    auto comp = k::compiler::create(static_target.get());
    REQUIRE_NOTHROW(comp->parse_sources({{"native_pic.k", R"SRC(
        module native_pic;
        public value : int = 42;
        public answer() : int { return value; }
    )SRC"}}, /*optimize=*/true, /*dump=*/false, /*forced_module_name=*/"k"));

    const auto artifact_path = std::filesystem::path{
        "klang_native_pic_" + std::to_string(::getpid()) + "_" + output_kind +
        (explicit_static_target ? "_static_target" : "_default_target")};
    REQUIRE(std::filesystem::create_directory(artifact_path));
    struct Artifacts {
        std::filesystem::path path;
        ~Artifacts() {
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    } artifacts{artifact_path};
    const auto shared_path = (artifacts.path / "native.so").string();
    const auto static_path = (artifacts.path / "native.a").string();

    if (output_kind == "shared") {
        REQUIRE(comp->gen_shared_library(shared_path));
    } else if (output_kind == "static") {
        REQUIRE(comp->gen_static_library(static_path));
    } else {
        REQUIRE(comp->gen_libraries(shared_path, static_path));
    }
    REQUIRE(std::filesystem::exists(artifacts.path / "native.kdi"));

    if (static_target) {
        CHECK(static_target->getRelocationModel() == llvm::Reloc::Static);
    }
    if (output_kind != "shared") {
        // Force inclusion of the archive member even without an external caller.
        auto link = k::tools::lookup_run_process("clang++", {
            "-shared", "-Wl,-z,text", "-o", shared_path,
            "-Wl,--whole-archive", static_path, "-Wl,--no-whole-archive"
        });
        INFO("archive-to-DSO stdout: " << link.out);
        INFO("archive-to-DSO stderr: " << link.err);
        REQUIRE(link.exit_code == 0);
    }
    auto symbols = k::tools::lookup_run_process(
        "nm", {"--dynamic", "--defined-only", shared_path});
    INFO("nm stderr: " << symbols.err);
    REQUIRE(symbols.exit_code == 0);
    CHECK(symbols.out.find("__k_rtti_manifest_v1") != std::string::npos);
}
