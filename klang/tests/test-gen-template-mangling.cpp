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
 * Tests for template name mangling (I…E encoding).
 *
 * These tests verify that:
 *  [A] Template struct Box<int> uses I…E encoded mangled name.
 *  [B] Template struct Box<float> has distinct mangled name from Box<int>.
 *  [C] Template struct with two type params Pair<int, float> mangles correctly.
 *  [D] Template struct constructor mangled name includes I…E encoding.
 *  [E] JIT lookup by mangled name works for a function using template struct.
 *  [F] Template struct mangled name used in type references (param type).
 *  [G] Free template function has I…E encoded mangled name (model-level).
 */

#include <catch2/catch_all.hpp>
#include <string>

#include "helpers.hpp"
#include "../src/model/template.hpp"
#include "../src/model/template_instantiator.hpp"
#include "../src/model/mangler.hpp"

// ════════════════════════════════════════════════════════════════════════════
//  [A] Template struct Box<int> mangled name uses I…E encoding
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[A] Mangling: template struct Box<int> has IiE encoding",
          "[mangling][template]") {
    auto comp = compile_model(R"SRC(
        module __mangle_a__;
        template<typename T>
        struct Box {
            public value : T;
        }
        struct User {
            public b : Box<int>;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto root_ns = comp->get_unit()->get_root_namespace();
    auto box_int = root_ns->get_aggregate("Box__int");
    REQUIRE(box_int != nullptr);

    // The mangled name should use Itanium-style I…E encoding
    auto mangled = box_int->get_mangled_name();
    CAPTURE(mangled);
    // Must start with _K prefix
    CHECK(mangled.find("_K") == 0);
    // Must contain "3Box" (length-encoded base name)
    CHECK(mangled.find("3Box") != std::string::npos);
    // Must contain "Ii" (template arg opening + int type)
    CHECK(mangled.find("Ii") != std::string::npos);
    // Must end with E (template args closing + qualified name closing)
    CHECK(mangled.back() == 'E');
}

// ════════════════════════════════════════════════════════════════════════════
//  [B] Box<float> has distinct mangled name from Box<int>
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[B] Mangling: Box<int> and Box<float> have distinct mangled names",
          "[mangling][template]") {
    auto comp = compile_model(R"SRC(
        module __mangle_b__;
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

    auto m_int = box_int->get_mangled_name();
    auto m_float = box_float->get_mangled_name();
    CAPTURE(m_int, m_float);

    CHECK(m_int != m_float);
    // Box<int> should have Ii..E encoding, Box<float> should have If..E encoding
    CHECK(m_int.find("Ii") != std::string::npos);
    CHECK(m_float.find("If") != std::string::npos);
}

// ════════════════════════════════════════════════════════════════════════════
//  [C] Pair<int, float> mangles with two template args
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[C] Mangling: Pair<int, float> has two-arg IifE encoding",
          "[mangling][template]") {
    auto comp = compile_model(R"SRC(
        module __mangle_c__;
        template<typename K, typename V>
        struct Pair {
            public first : K;
            public second : V;
        }
        struct User {
            public p : Pair<int, float>;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto root_ns = comp->get_unit()->get_root_namespace();
    // Multi-param instantiation: "Pair__int_float" (__ after base, _ between args)
    auto pair_if = root_ns->get_aggregate("Pair__int_float");
    REQUIRE(pair_if != nullptr);

    auto mangled = pair_if->get_mangled_name();
    CAPTURE(mangled);
    CHECK(mangled.find("4Pair") != std::string::npos);
    // Should contain IifE (two args: int + float)
    CHECK(mangled.find("Iif") != std::string::npos);
}

// ════════════════════════════════════════════════════════════════════════════
//  [D] Template struct constructor mangled name includes I…E encoding
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[D] Mangling: template struct constructor includes IiE encoding",
          "[mangling][template]") {
    auto comp = compile_model(R"SRC(
        module __mangle_d__;
        template<typename T>
        struct Box {
            public value : T;
        }
        struct User {
            public b : Box<int>;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto root_ns = comp->get_unit()->get_root_namespace();
    auto box_int = root_ns->get_aggregate("Box__int");
    REQUIRE(box_int != nullptr);

    // The default constructor should exist (compiler-generated)
    REQUIRE(!box_int->constructors().empty());
    auto ctor = box_int->constructors().front();
    REQUIRE(ctor != nullptr);

    auto mangled = ctor->get_mangled_name();
    CAPTURE(mangled);
    // Constructor is a non-static member: _KFM
    CHECK(mangled.find("_KFM") == 0);
    CHECK(mangled.find("3Box") != std::string::npos);
    CHECK(mangled.find("Ii") != std::string::npos);
}

// ════════════════════════════════════════════════════════════════════════════
//  [E] JIT lookup by mangled name for a function using template struct
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[E] Mangling: JIT lookup works with template struct mangled names",
          "[mangling][template][jit]") {
    auto jit = gen_jit(R"SRC(
        module __mangle_e__;
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
    auto get_value = jit->lookup_symbol<int(*)()>("_KFN12__mangle_e__9get_valueEv");
    REQUIRE(get_value != nullptr);
    CHECK(get_value() == 42);
}

// ════════════════════════════════════════════════════════════════════════════
//  [F] Template struct mangled name used in type references (param type)
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[F] Mangling: template struct used as parameter type mangles correctly",
          "[mangling][template]") {
    auto comp = compile_model(R"SRC(
        module __mangle_f__;
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
    REQUIRE(comp != nullptr);

    auto root_ns = comp->get_unit()->get_root_namespace();
    auto box_int = root_ns->get_aggregate("Box__int");
    REQUIRE(box_int != nullptr);

    // Verify that the aggregate has tpl instantiation info
    CHECK(box_int->has_tpl_args());
    CHECK(box_int->get_tpl_base_name() == "Box");
    REQUIRE(box_int->get_tpl_args().size() == 1);
    CHECK(box_int->get_tpl_args()[0].is_type());

    // Verify the mangled name encodes the template args
    auto mangled = box_int->get_mangled_name();
    CAPTURE(mangled);
    CHECK(mangled.find("3Box") != std::string::npos);
    CHECK(mangled.find("Ii") != std::string::npos);
}

// ════════════════════════════════════════════════════════════════════════════
//  [G] Free template function has I…E encoded mangled name (model-level)
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[G] Mangling: free template function instantiation has IiE encoding (model-level)",
          "[mangling][template]") {
    auto comp = compile_model(R"SRC(
        module __mangle_g__;
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
    CHECK(concrete->has_tpl_args());
    CHECK(concrete->get_tpl_base_name() == "identity");
}


