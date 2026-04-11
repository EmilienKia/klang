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
 * Tests for Milestone 10: Template constraint validation.
 *
 * These tests verify that:
 *  [A] validate_template_arg_constraints: typename accepts any type (primitive).
 *  [B] validate_template_arg_constraints: struct kind rejects non-aggregate type.
 *  [C] validate_template_arg_constraints: struct kind accepts a struct type.
 *  [D] validate_template_arg_constraints: class kind rejects a struct type.
 *  [E] validate_template_arg_constraints: class kind accepts a class type.
 *  [F] validate_template_arg_constraints: interface kind accepts an interface type.
 *  [G] validate_template_arg_constraints: base-type constraint accepts derived type.
 *  [H] validate_template_arg_constraints: base-type constraint rejects unrelated type.
 *  [I] validate_template_arg_constraints: value param is skipped.
 *  [J] Integration: struct-constrained template param rejects int at resolver level.
 *  [K] Integration: typename-constrained template accepts struct at resolver level.
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"
#include "../src/model/template.hpp"
#include "../src/model/template_instantiator.hpp"

using namespace k::model;

// ════════════════════════════════════════════════════════════════════════════
//  [A] typename accepts any type (primitive)
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[A] M10: typename param accepts primitive type",
          "[milestone10][template][constraints]") {
    auto ctx = context::create();
    auto int_type = ctx->from_type(primitive_type::INT);

    std::vector<template_param_descriptor> params;
    template_param_descriptor p;
    p.kind = template_param_kind::TYPENAME;
    p.name = "T";
    params.push_back(p);

    std::vector<template_argument> args;
    args.push_back(template_argument::make_type(int_type));

    size_t err_idx;
    std::string err_kind;
    CHECK(validate_template_arg_constraints(params, args, err_idx, err_kind));
}

// ════════════════════════════════════════════════════════════════════════════
//  [B] struct kind rejects non-aggregate type (int)
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[B] M10: struct kind rejects primitive type",
          "[milestone10][template][constraints]") {
    auto ctx = context::create();
    auto int_type = ctx->from_type(primitive_type::INT);

    std::vector<template_param_descriptor> params;
    template_param_descriptor p;
    p.kind = template_param_kind::STRUCT;
    p.name = "S";
    params.push_back(p);

    std::vector<template_argument> args;
    args.push_back(template_argument::make_type(int_type));

    size_t err_idx;
    std::string err_kind;
    CHECK_FALSE(validate_template_arg_constraints(params, args, err_idx, err_kind));
    CHECK(err_idx == 0);
    CHECK(err_kind == "not_aggregate");
}

// ════════════════════════════════════════════════════════════════════════════
//  [C] struct kind accepts a struct type
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[C] M10: struct kind accepts struct type from model",
          "[milestone10][template][constraints]") {
    auto comp = compile_model(R"SRC(
        module __m10_c__;
        struct Foo {
            public x : int;
        }
        template<struct S>
        struct Holder {
            public val : int;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto unit = comp->get_unit();
    auto root_ns = unit->get_root_namespace();

    // Get the struct Foo's struct_type
    auto foo_agg = root_ns->get_aggregate("Foo");
    REQUIRE(foo_agg != nullptr);
    auto foo_st = foo_agg->get_struct_type();
    REQUIRE(foo_st != nullptr);

    // Get the template Holder's tpl_info
    auto holder = root_ns->get_aggregate("Holder");
    REQUIRE(holder != nullptr);
    REQUIRE(holder->is_template());
    auto* ti = holder->get_tpl_info();
    REQUIRE(ti != nullptr);
    REQUIRE(ti->params.size() == 1);
    CHECK(ti->params[0].kind == template_param_kind::STRUCT);

    // Validate: Foo should be accepted
    std::vector<template_argument> args;
    args.push_back(template_argument::make_type(foo_st));

    size_t err_idx;
    std::string err_kind;
    CHECK(validate_template_arg_constraints(ti->params, args, err_idx, err_kind));
}

// ════════════════════════════════════════════════════════════════════════════
//  [D] class kind rejects a struct type
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[D] M10: class kind rejects struct type",
          "[milestone10][template][constraints]") {
    auto comp = compile_model(R"SRC(
        module __m10_d__;
        struct Foo {
            public x : int;
        }
        template<class C>
        struct Holder {
            public val : int;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto unit = comp->get_unit();
    auto root_ns = unit->get_root_namespace();

    auto foo_agg = root_ns->get_aggregate("Foo");
    REQUIRE(foo_agg != nullptr);
    auto foo_st = foo_agg->get_struct_type();
    REQUIRE(foo_st != nullptr);

    auto holder = root_ns->get_aggregate("Holder");
    REQUIRE(holder != nullptr);
    auto* ti = holder->get_tpl_info();
    REQUIRE(ti != nullptr);

    std::vector<template_argument> args;
    args.push_back(template_argument::make_type(foo_st));

    size_t err_idx;
    std::string err_kind;
    CHECK_FALSE(validate_template_arg_constraints(ti->params, args, err_idx, err_kind));
    CHECK(err_idx == 0);
    CHECK(err_kind == "kind");
}

// ════════════════════════════════════════════════════════════════════════════
//  [E] class kind accepts a class type
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[E] M10: class kind accepts class type",
          "[milestone10][template][constraints]") {
    auto comp = compile_model(R"SRC(
        module __m10_e__;
        class Animal {
            public name : int;
        }
        template<class C>
        struct Holder {
            public val : int;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto unit = comp->get_unit();
    auto root_ns = unit->get_root_namespace();

    auto animal_agg = root_ns->get_aggregate("Animal");
    REQUIRE(animal_agg != nullptr);
    auto animal_st = animal_agg->get_struct_type();
    REQUIRE(animal_st != nullptr);

    auto holder = root_ns->get_aggregate("Holder");
    REQUIRE(holder != nullptr);
    auto* ti = holder->get_tpl_info();
    REQUIRE(ti != nullptr);

    std::vector<template_argument> args;
    args.push_back(template_argument::make_type(animal_st));

    size_t err_idx;
    std::string err_kind;
    CHECK(validate_template_arg_constraints(ti->params, args, err_idx, err_kind));
}

// ════════════════════════════════════════════════════════════════════════════
//  [F] interface kind accepts an interface type
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[F] M10: interface kind accepts interface type",
          "[milestone10][template][constraints]") {
    auto comp = compile_model(R"SRC(
        module __m10_f__;
        interface Printable {
            print() : int;
        }
        template<interface I>
        struct Holder {
            public val : int;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto unit = comp->get_unit();
    auto root_ns = unit->get_root_namespace();

    auto iface = root_ns->get_aggregate("Printable");
    REQUIRE(iface != nullptr);
    auto iface_st = iface->get_struct_type();
    REQUIRE(iface_st != nullptr);

    auto holder = root_ns->get_aggregate("Holder");
    REQUIRE(holder != nullptr);
    auto* ti = holder->get_tpl_info();
    REQUIRE(ti != nullptr);

    std::vector<template_argument> args;
    args.push_back(template_argument::make_type(iface_st));

    size_t err_idx;
    std::string err_kind;
    CHECK(validate_template_arg_constraints(ti->params, args, err_idx, err_kind));
}

// ════════════════════════════════════════════════════════════════════════════
//  [G] base-type constraint accepts same or derived type
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[G] M10: base-type constraint accepts derived type",
          "[milestone10][template][constraints]") {
    auto comp = compile_model(R"SRC(
        module __m10_g__;
        class Animal {
            public age : int;
        }
        class Dog : Animal {
            public name : int;
        }
        template<class T : Animal>
        struct Holder {
            public val : int;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto unit = comp->get_unit();
    auto root_ns = unit->get_root_namespace();

    auto dog = root_ns->get_aggregate("Dog");
    REQUIRE(dog != nullptr);
    auto dog_st = dog->get_struct_type();
    REQUIRE(dog_st != nullptr);

    auto holder = root_ns->get_aggregate("Holder");
    REQUIRE(holder != nullptr);
    auto* ti = holder->get_tpl_info();
    REQUIRE(ti != nullptr);
    REQUIRE(ti->params.size() == 1);
    CHECK(ti->params[0].kind == template_param_kind::CLASS);

    // Dog derives from Animal — should pass
    std::vector<template_argument> args;
    args.push_back(template_argument::make_type(dog_st));

    size_t err_idx;
    std::string err_kind;
    CHECK(validate_template_arg_constraints(ti->params, args, err_idx, err_kind));

    // Also check: Animal itself should pass
    auto animal = root_ns->get_aggregate("Animal");
    REQUIRE(animal != nullptr);
    auto animal_st = animal->get_struct_type();
    REQUIRE(animal_st != nullptr);

    std::vector<template_argument> args2;
    args2.push_back(template_argument::make_type(animal_st));
    CHECK(validate_template_arg_constraints(ti->params, args2, err_idx, err_kind));
}

// ════════════════════════════════════════════════════════════════════════════
//  [H] base-type constraint rejects unrelated type
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[H] M10: base-type constraint rejects unrelated type",
          "[milestone10][template][constraints]") {
    auto comp = compile_model(R"SRC(
        module __m10_h__;
        class Animal {
            public age : int;
        }
        class Car {
            public speed : int;
        }
        template<class T : Animal>
        struct Holder {
            public val : int;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto unit = comp->get_unit();
    auto root_ns = unit->get_root_namespace();

    auto car = root_ns->get_aggregate("Car");
    REQUIRE(car != nullptr);
    auto car_st = car->get_struct_type();
    REQUIRE(car_st != nullptr);

    auto holder = root_ns->get_aggregate("Holder");
    REQUIRE(holder != nullptr);
    auto* ti = holder->get_tpl_info();
    REQUIRE(ti != nullptr);

    std::vector<template_argument> args;
    args.push_back(template_argument::make_type(car_st));

    size_t err_idx;
    std::string err_kind;
    CHECK_FALSE(validate_template_arg_constraints(ti->params, args, err_idx, err_kind));
    CHECK(err_idx == 0);
    CHECK(err_kind == "constraint");
}

// ════════════════════════════════════════════════════════════════════════════
//  [I] value param is skipped by constraint validation
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[I] M10: value parameter is skipped by constraint validation",
          "[milestone10][template][constraints]") {
    std::vector<template_param_descriptor> params;
    template_param_descriptor p;
    p.kind = template_param_kind::VALUE;
    p.name = "N";
    params.push_back(p);

    std::vector<template_argument> args;
    args.push_back(template_argument::make_value(42));

    size_t err_idx;
    std::string err_kind;
    CHECK(validate_template_arg_constraints(params, args, err_idx, err_kind));
}

// ════════════════════════════════════════════════════════════════════════════
//  [J] Integration: struct-constrained template rejects int at resolver level
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[J] M10: struct-constrained template rejects int in resolver",
          "[milestone10][template][constraints][integration]") {
    // Attempting to instantiate Holder<int> where Holder has `struct S` param
    // should fail — the resolver returns {} and the type is not instantiated.
    auto comp = compile_model(R"SRC(
        module __m10_j__;
        template<struct S>
        struct Holder {
            public val : int;
        }
        struct User {
            public h : Holder<int>;
        }
    )SRC");
    // Compilation may succeed but the Holder<int> type is NOT instantiated
    // (the resolver silently fails), so User::h remains unresolved.
    // The model should show that no Holder__int exists.
    if (comp) {
        auto root_ns = comp->get_unit()->get_root_namespace();
        auto holder_int = root_ns->get_aggregate("Holder__int");
        CHECK(holder_int == nullptr);  // Constraint prevented instantiation
    }
    // If comp == nullptr, compilation failed entirely, which is also acceptable
}

// ════════════════════════════════════════════════════════════════════════════
//  [K] Integration: typename accepts struct at resolver level
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[K] M10: typename-constrained template accepts int in resolver",
          "[milestone10][template][constraints][integration]") {
    auto jit = gen_jit(R"SRC(
        module __m10_k__;
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
    auto get_value = jit->lookup_symbol<int(*)()>("_KFN9__m10_k__9get_valueEv");
    REQUIRE(get_value != nullptr);
    CHECK(get_value() == 42);
}





