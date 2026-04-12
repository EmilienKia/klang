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
#include "../src/errors.hpp"

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
    // should throw a resolution_error with ERR_TPL_ARG_NOT_AGGREGATE.
    try {
        gen_jit_throws(R"SRC(
            module __m10_j__;
            template<struct S>
            struct Holder {
                public val : int;
            }
            struct User {
                public h : Holder<int>;
            }
        )SRC");
        FAIL("Expected resolution_error to be thrown");
    } catch (const k::model::gen::resolution_error& e) {
        CHECK(e.get_diagnostic().code ==
              static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_ARG_NOT_AGGREGATE));
    }
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

// ════════════════════════════════════════════════════════════════════════════
//  [L] Integration: class-constrained template rejects struct
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[L] M10: class-constrained template rejects struct in resolver",
          "[milestone10][template][constraints][integration]") {
    try {
        gen_jit_throws(R"SRC(
            module __m10_l__;
            struct Foo {
                public x : int;
            }
            template<class C>
            struct Wrapper {
                public val : int;
            }
            struct User {
                public w : Wrapper<Foo>;
            }
        )SRC");
        FAIL("Expected resolution_error to be thrown");
    } catch (const k::model::gen::resolution_error& e) {
        CHECK(e.get_diagnostic().code ==
              static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_ARG_WRONG_KIND));
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  [M] Integration: interface-constrained template rejects class
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[M] M10: interface-constrained template rejects class in resolver",
          "[milestone10][template][constraints][integration]") {
    try {
        gen_jit_throws(R"SRC(
            module __m10_m__;
            class Animal {
                public age : int;
            }
            template<interface I>
            struct Wrapper {
                public val : int;
            }
            struct User {
                public w : Wrapper<Animal>;
            }
        )SRC");
        FAIL("Expected resolution_error to be thrown");
    } catch (const k::model::gen::resolution_error& e) {
        CHECK(e.get_diagnostic().code ==
              static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_ARG_WRONG_KIND));
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  [N] Integration: base-type constraint rejects unrelated class
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[N] M10: base-type constraint rejects unrelated class in resolver",
          "[milestone10][template][constraints][integration]") {
    try {
        gen_jit_throws(R"SRC(
            module __m10_n__;
            class Animal {
                public age : int;
            }
            class Car {
                public speed : int;
            }
            template<class T : Animal>
            struct Cage {
                public val : int;
            }
            struct User {
                public c : Cage<Car>;
            }
        )SRC");
        FAIL("Expected resolution_error to be thrown");
    } catch (const k::model::gen::resolution_error& e) {
        CHECK(e.get_diagnostic().code ==
              static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_ARG_CONSTRAINT_VIOLATED));
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  [O] Integration: base-type constraint accepts derived class
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[O] M10: base-type constraint accepts derived class in resolver",
          "[milestone10][template][constraints][jit]") {
    auto jit = gen_jit(R"SRC(
        module __m10_o__;
        class Animal {
            public age : int;
        }
        class Dog : Animal {
            public name : int;
        }
        template<class T : Animal>
        struct Cage {
            public val : int;
        }
        test() : int {
            c : Cage<Dog>;
            c.val = 99;
            return c.val;
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto test_fn = jit->lookup_symbol<int(*)()>("_KFN9__m10_o__4testEv");
    REQUIRE(test_fn != nullptr);
    CHECK(test_fn() == 99);
}

// ════════════════════════════════════════════════════════════════════════════
//  [P] Integration: struct-constrained template accepts struct
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[P] M10: struct-constrained template accepts struct in resolver",
          "[milestone10][template][constraints][jit]") {
    auto jit = gen_jit(R"SRC(
        module __m10_p__;
        struct Vec2 {
            public x : int;
            public y : int;
        }
        template<struct S>
        struct Holder {
            public val : S;
        }
        test() : int {
            h : Holder<Vec2>;
            h.val.x = 10;
            h.val.y = 32;
            return h.val.x + h.val.y;
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto test_fn = jit->lookup_symbol<int(*)()>("_KFN9__m10_p__4testEv");
    REQUIRE(test_fn != nullptr);
    CHECK(test_fn() == 42);
}

// ════════════════════════════════════════════════════════════════════════════
//  [Q] Integration: class-constrained template accepts class
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[Q] M10: class-constrained template accepts class in resolver",
          "[milestone10][template][constraints][jit]") {
    auto jit = gen_jit(R"SRC(
        module __m10_q__;
        class Widget {
            public id : int;
        }
        template<class C>
        struct Container {
            public data : int;
        }
        test() : int {
            c : Container<Widget>;
            c.data = 55;
            return c.data;
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto test_fn = jit->lookup_symbol<int(*)()>("_KFN9__m10_q__4testEv");
    REQUIRE(test_fn != nullptr);
    CHECK(test_fn() == 55);
}

// ════════════════════════════════════════════════════════════════════════════
//  [R] Integration: interface-constrained template accepts interface
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[R] M10: interface-constrained template accepts interface in resolver",
          "[milestone10][template][constraints][jit]") {
    auto jit = gen_jit(R"SRC(
        module __m10_r__;
        interface Countable {
            count() : int;
        }
        template<interface I>
        struct Adapter {
            public tag : int;
        }
        test() : int {
            a : Adapter<Countable>;
            a.tag = 77;
            return a.tag;
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto test_fn = jit->lookup_symbol<int(*)()>("_KFN9__m10_r__4testEv");
    REQUIRE(test_fn != nullptr);
    CHECK(test_fn() == 77);
}

// ════════════════════════════════════════════════════════════════════════════
//  [S] Integration: function template with struct constraint — rejects int
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[S] M10: function template with struct constraint rejects int",
          "[milestone10][template][constraints][integration]") {
    // For function templates, constraint violations don't throw from the
    // resolver — they prevent instantiation, leading to further errors.
    REQUIRE_THROWS_AS(
        gen_jit_throws(R"SRC(
            module __m10_s__;
            template<struct S>
            process() : int { return 0; }

            test() : int {
                return process<int>();
            }
        )SRC"),
        k::log::compiler_error
    );
}

// ════════════════════════════════════════════════════════════════════════════
//  [T] Integration: function template with class constraint — accepts class
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[T] M10: function template with class constraint accepts class",
          "[milestone10][template][constraints][jit]") {
    auto jit = gen_jit(R"SRC(
        module __m10_t__;
        class Widget {
            public id : int;
        }
        template<class C>
        get_zero() : int { return 0; }

        test() : int {
            return get_zero<Widget>();
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto test_fn = jit->lookup_symbol<int(*)()>("_KFN9__m10_t__4testEv");
    REQUIRE(test_fn != nullptr);
    CHECK(test_fn() == 0);
}

// ════════════════════════════════════════════════════════════════════════════
//  [U] Integration: function template with base-type constraint — rejects
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[U] M10: function template with base-type constraint rejects unrelated",
          "[milestone10][template][constraints][integration]") {
    REQUIRE_THROWS_AS(
        gen_jit_throws(R"SRC(
            module __m10_u__;
            class Animal {
                public age : int;
            }
            class Car {
                public speed : int;
            }
            template<class T : Animal>
            feed() : int { return 0; }

            test() : int {
                return feed<Car>();
            }
        )SRC"),
        k::log::compiler_error
    );
}

// ════════════════════════════════════════════════════════════════════════════
//  [V] Integration: function template with base-type constraint — accepts derived
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[V] M10: function template with base-type constraint accepts derived",
          "[milestone10][template][constraints][jit]") {
    auto jit = gen_jit(R"SRC(
        module __m10_v__;
        class Animal {
            public age : int;
        }
        class Dog : Animal {
            public name : int;
        }
        template<class T : Animal>
        feed() : int { return 42; }

        test() : int {
            return feed<Dog>();
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto test_fn = jit->lookup_symbol<int(*)()>("_KFN9__m10_v__4testEv");
    REQUIRE(test_fn != nullptr);
    CHECK(test_fn() == 42);
}

// ════════════════════════════════════════════════════════════════════════════
//  [W] Error message content: "not_aggregate" includes param name and type
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[W] M10: error message for not_aggregate includes param name",
          "[milestone10][template][constraints][integration]") {
    try {
        gen_jit_throws(R"SRC(
            module __m10_w__;
            template<struct MyParam>
            struct Box { public val : int; }
            struct User { public b : Box<int>; }
        )SRC");
        FAIL("Expected resolution_error to be thrown");
    } catch (const k::model::gen::resolution_error& e) {
        CHECK(e.get_diagnostic().code ==
              static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_ARG_NOT_AGGREGATE));
        // Message should contain param name and template name
        CHECK(e.get_diagnostic().message.find("MyParam") != std::string::npos);
        CHECK(e.get_diagnostic().message.find("Box") != std::string::npos);
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  [X] Error message content: "kind" includes expected and actual kind
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[X] M10: error message for kind mismatch includes kinds",
          "[milestone10][template][constraints][integration]") {
    try {
        gen_jit_throws(R"SRC(
            module __m10_x__;
            struct Foo { public x : int; }
            template<class C>
            struct Wrapper { public val : int; }
            struct User { public w : Wrapper<Foo>; }
        )SRC");
        FAIL("Expected resolution_error to be thrown");
    } catch (const k::model::gen::resolution_error& e) {
        CHECK(e.get_diagnostic().code ==
              static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_ARG_WRONG_KIND));
        CHECK(e.get_diagnostic().message.find("class") != std::string::npos);
        CHECK(e.get_diagnostic().message.find("struct") != std::string::npos);
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  [Y] Error message content: "constraint" includes constraint type name
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[Y] M10: error message for constraint violation includes constraint type",
          "[milestone10][template][constraints][integration]") {
    try {
        gen_jit_throws(R"SRC(
            module __m10_y__;
            class Animal { public age : int; }
            class Car { public speed : int; }
            template<class T : Animal>
            struct Cage { public val : int; }
            struct User { public c : Cage<Car>; }
        )SRC");
        FAIL("Expected resolution_error to be thrown");
    } catch (const k::model::gen::resolution_error& e) {
        CHECK(e.get_diagnostic().code ==
              static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_ARG_CONSTRAINT_VIOLATED));
        CHECK(e.get_diagnostic().message.find("Animal") != std::string::npos);
        CHECK(e.get_diagnostic().message.find("Car") != std::string::npos);
    }
}

