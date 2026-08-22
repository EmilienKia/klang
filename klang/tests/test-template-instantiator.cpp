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

#include "helpers.hpp"
#include "../src/model/template.hpp"
#include "../src/model/template_instantiator.hpp"

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
          "[milestone7][generic][e2e]") {
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
          "[milestone7][generic][e2e]") {
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

