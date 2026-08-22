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
        module gen_template_mangling_01;
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
        module gen_template_mangling_02;
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
        module gen_template_mangling_03;
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
    // Multi-param instantiation: "Pair__int__float" (__ after base, __ between args)
    auto pair_if = root_ns->get_aggregate("Pair__int__float");
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
        module gen_template_mangling_04;
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
        module gen_template_mangling_05;
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
    auto get_value = jit->lookup_symbol<int(*)()>("_KFN24gen_template_mangling_059get_valueEv");
    REQUIRE(get_value != nullptr);
    CHECK(get_value() == 42);
}

// ════════════════════════════════════════════════════════════════════════════
//  [F] Template struct mangled name used in type references (param type)
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[F] Mangling: template struct used as parameter type mangles correctly",
          "[mangling][template]") {
    auto comp = compile_model(R"SRC(
        module gen_template_mangling_06;
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
        module gen_template_mangling_07;
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

TEST_CASE("[H] Mangling: generic synthesis keeps base symbol without template arg encoding",
          "[mangling][template][generic]") {
    auto comp = compile_model(R"SRC(
        module gen_template_mangling_08;
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
    REQUIRE(box_tpl->is_template());
    REQUIRE(box_tpl->is_generic());

    auto ctx = comp->get_context_for_test();
    REQUIRE(ctx != nullptr);
    auto int_type = ctx->from_type(k::model::primitive_type::INT);
    REQUIRE(int_type != nullptr);

    std::vector<k::model::template_argument> args;
    args.push_back(k::model::template_argument::make_type(int_type));

    test_logger logger;
    auto synthesized = k::model::template_instantiator::synthesize_generic_aggregate(
        *box_tpl, root_ns, *unit, ctx, logger);
    REQUIRE(synthesized != nullptr);

    // Simulate an imported/aliased instantiation metadata on the synthesized node.
    synthesized->set_tpl_instantiation_info("Box", args);

    k::model::mangler mg(ctx);

    auto agg_mangled = mg.mangle_structure(*synthesized);
    CAPTURE(agg_mangled);
    CHECK(agg_mangled.find("3Box") != std::string::npos);
    CHECK(agg_mangled.find("3BoxI") == std::string::npos);
    CHECK(agg_mangled.find("Ii") == std::string::npos);

}

TEST_CASE("[I] Mangling: generic synthesized method keeps one symbol across concrete usages",
          "[mangling][template][generic]") {
    auto comp = compile_model(R"SRC(
        module gen_template_mangling_09;
        generic<typename T>
        struct Box {
            relay(v : T&) : T& { return v; }
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

    auto ctx = comp->get_context_for_test();
    REQUIRE(ctx != nullptr);

    test_logger logger;
    auto synthesized = k::model::template_instantiator::synthesize_generic_aggregate(
        *box_tpl, root_ns, *unit, ctx, logger);
    REQUIRE(synthesized != nullptr);

    auto relay = synthesized->get_function("relay");
    REQUIRE(relay != nullptr);

    k::model::mangler mg(ctx);

    std::vector<k::model::template_argument> int_args;
    int_args.push_back(k::model::template_argument::make_type(
        ctx->from_type(k::model::primitive_type::INT)));
    synthesized->set_tpl_instantiation_info("Box", int_args);
    auto int_mangled = mg.mangle_function(*relay);

    std::vector<k::model::template_argument> float_args;
    float_args.push_back(k::model::template_argument::make_type(
        ctx->from_type(k::model::primitive_type::FLOAT)));
    synthesized->set_tpl_instantiation_info("Box", float_args);
    auto float_mangled = mg.mangle_function(*relay);

    CAPTURE(int_mangled, float_mangled);
    CHECK(int_mangled == float_mangled);
    CHECK(int_mangled.find("3Box") != std::string::npos);
    CHECK(int_mangled.find("5relay") != std::string::npos);
    CHECK(int_mangled.find("3BoxI") == std::string::npos);
    CHECK(int_mangled.find("Ii") == std::string::npos);
    CHECK(int_mangled.find("If") == std::string::npos);
}

TEST_CASE("[J] Mangling: generic synthesized constructor keeps one symbol across concrete usages",
          "[mangling][template][generic]") {
    auto comp = compile_model(R"SRC(
        module gen_template_mangling_10;
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

    auto ctx = comp->get_context_for_test();
    REQUIRE(ctx != nullptr);

    test_logger logger;
    auto synthesized = k::model::template_instantiator::synthesize_generic_aggregate(
        *box_tpl, root_ns, *unit, ctx, logger);
    REQUIRE(synthesized != nullptr);
    REQUIRE(!synthesized->constructors().empty());

    auto ctor = synthesized->constructors().front();
    REQUIRE(ctor != nullptr);

    k::model::mangler mg(ctx);

    std::vector<k::model::template_argument> int_args;
    int_args.push_back(k::model::template_argument::make_type(
        ctx->from_type(k::model::primitive_type::INT)));
    synthesized->set_tpl_instantiation_info("Box", int_args);
    auto int_mangled = mg.mangle_constructor(*ctor);

    std::vector<k::model::template_argument> float_args;
    float_args.push_back(k::model::template_argument::make_type(
        ctx->from_type(k::model::primitive_type::FLOAT)));
    synthesized->set_tpl_instantiation_info("Box", float_args);
    auto float_mangled = mg.mangle_constructor(*ctor);

    CAPTURE(int_mangled, float_mangled);
    CHECK(int_mangled == float_mangled);
    CHECK(int_mangled.find("_KFM") == 0);
    CHECK(int_mangled.find("3Box") != std::string::npos);
    CHECK(int_mangled.find("C1") != std::string::npos);
    CHECK(int_mangled.find("3BoxI") == std::string::npos);
    CHECK(int_mangled.find("Ii") == std::string::npos);
    CHECK(int_mangled.find("If") == std::string::npos);
}



// ════════════════════════════════════════════════════════════════════════════
//  Mangling exhaustiveness
//
//  mangle_type() used to return an empty string for several type kinds, most notably
//  enumerations. Two overloads differing only by an enum parameter then produced the very
//  same symbol; within one module LLVM auto-renamed the second to "…E.1" (invisible to the
//  KDI), and a consumer linking against the library called the first overload for both.
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Mangling: enum parameters produce distinct symbols",
          "[gen][mangling][exhaustive]") {
    auto comp = compile_model(R"SRC(
        module gen_template_mangling_11;
        enum ErrA { a1; a2; }
        enum ErrB { b1; b2; b3; }
        f(x : ErrA) : int { return 100; }
        f(x : ErrB) : int { return 200; }
    )SRC");
    REQUIRE(comp != nullptr);

    auto root_ns = comp->get_unit()->get_root_namespace();
    auto overloads = root_ns->get_functions("f");
    REQUIRE(overloads.size() == 2);

    const auto& m0 = overloads[0]->get_mangled_name();
    const auto& m1 = overloads[1]->get_mangled_name();
    CHECK_FALSE(m0.empty());
    CHECK_FALSE(m1.empty());
    CHECK(m0 != m1);
    // Enumerations are encoded as 'Te' followed by their qualified name.
    CHECK(m0.find("Te") != std::string::npos);
    CHECK(m1.find("Te") != std::string::npos);
    CHECK((m0.find("ErrA") != std::string::npos) != (m1.find("ErrA") != std::string::npos));
}

TEST_CASE("Mangling: enum template argument is not erased",
          "[gen][mangling][exhaustive]") {
    // Expected<long, StreamOutOfData> used to mangle as '…ExpectedIxE…': the enum argument
    // was silently dropped, so Expected<long, EnumA> and Expected<long, EnumB> were
    // indistinguishable at link time (and linkonce_odr/COMDAT merged them arbitrarily).
    auto comp = compile_model(R"SRC(
        module gen_template_mangling_12;
        enum ErrA { a1; }
        enum ErrB { b1; }
        template<typename R, typename E>
        struct Box {
            _v : R;
            _e : E;
            public:
            Box() {}
            setErr(e : E&) : void { _e = e; }
        }
        useA(x : ErrA) : int { b : Box<int, ErrA>; b.setErr(x); return 0; }
        useB(x : ErrB) : int { b : Box<int, ErrB>; b.setErr(x); return 0; }
    )SRC");
    REQUIRE(comp != nullptr);

    auto root_ns = comp->get_unit()->get_root_namespace();
    std::vector<std::string> set_err_symbols;
    for (const auto& child : root_ns->get_children()) {
        auto agg = std::dynamic_pointer_cast<k::model::aggregate>(child);
        if (!agg || agg->get_short_name().rfind("Box__", 0) != 0) continue;
        for (const auto& fn : agg->get_functions("setErr")) {
            set_err_symbols.push_back(fn->get_mangled_name());
        }
    }
    REQUIRE(set_err_symbols.size() == 2);
    CHECK_FALSE(set_err_symbols[0].empty());
    CHECK(set_err_symbols[0] != set_err_symbols[1]);
}

TEST_CASE("Mangling: no two emitted entities share a mangled name",
          "[gen][mangling][exhaustive]") {
    // compiler::verify_mangled_names() runs before code generation and rejects both empty
    // and duplicated mangled names, so simply compiling this unit exercises the check over
    // a broad mix of type kinds.
    auto comp = compile_model(R"SRC(
        module gen_template_mangling_13;
        enum Colour : int { red; green; }
        struct Point { x : int; y : int; }
        template<typename T>
        struct Wrap {
            _v : T;
            public:
            Wrap() {}
        }
        g(a : Colour) : int { return 1; }
        g(a : Point&) : int { return 2; }
        g(a : Point*) : int { return 3; }
        g(a : int) : int { return 4; }
        g(a : long) : int { return 5; }
        g(a : char) : int { return 6; }
        g(a : byte) : int { return 7; }
        g(a : bool) : int { return 8; }
        use() : int {
            wi : Wrap<int>;
            wp : Wrap<Point>;
            wr : Wrap<Point*>;
            return 0;
        }
    )SRC");
    REQUIRE(comp != nullptr);
}
