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

#include <catch2/catch_all.hpp>

#include "helpers.hpp"
#include "../src/plugin/plugin_manager.hpp"
#include "../src/plugin/naming/casing_utils.hpp"
#include "../src/plugin/naming/naming_pass.hpp"

namespace {

std::filesystem::path find_naming_plugin_so() {
    std::vector<std::filesystem::path> candidate_paths = {
#if defined(KLANG_STDLIB_LIB_DIR)
        std::filesystem::path(KLANG_STDLIB_LIB_DIR).parent_path().parent_path() / "klang" / "libklang-plugin-naming.so",
#endif
        std::filesystem::current_path() / "klang" / "libklang-plugin-naming.so",
        std::filesystem::current_path() / "cmake-build-debug" / "klang" / "libklang-plugin-naming.so",
        "klang/libklang-plugin-naming.so",
        "cmake-build-debug/klang/libklang-plugin-naming.so",
        "libklang-plugin-naming.so"
    };

    for (const auto& p : candidate_paths) {
        if (std::filesystem::exists(p)) {
            return p;
        }
    }
    return {};
}

std::shared_ptr<k::compiler> make_compiler_with_naming_plugin() {
    ensure_libk_loaded();
    auto comp = k::compiler::create();

    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_search_dir(std::filesystem::current_path());
#if defined(KLANG_STDLIB_LIB_DIR)
    resolver->add_search_dir(std::string(KLANG_STDLIB_LIB_DIR));
#endif
    comp->set_file_resolver(resolver);

    auto so_path = find_naming_plugin_so();
    REQUIRE(!so_path.empty());
    bool loaded = comp->load_plugin(so_path);
    REQUIRE(loaded);

    return comp;
}

} // anonymous namespace

// ── 1. Casing Utilities Unit Tests ──────────────────────────────────────────

TEST_CASE("Casing Utilities: PascalCase", "[naming][casing]") {
    using namespace k::plugin::naming;
    CHECK(to_pascal_case("point_2d") == "Point2d");
    CHECK(to_pascal_case("my_struct") == "MyStruct");
    CHECK(to_pascal_case("myStruct") == "MyStruct");
    CHECK(to_pascal_case("MyStruct") == "MyStruct");
    CHECK(to_pascal_case("MY_CLASS") == "MyClass");
    CHECK(to_pascal_case("_hidden_struct") == "_HiddenStruct");
}

TEST_CASE("Casing Utilities: camelCase", "[naming][casing]") {
    using namespace k::plugin::naming;
    CHECK(to_camel_case("My_Method") == "myMethod");
    CHECK(to_camel_case("my_variable") == "myVariable");
    CHECK(to_camel_case("ComputeTotal") == "computeTotal");
    CHECK(to_camel_case("computeTotal") == "computeTotal");
    CHECK(to_camel_case("IS_VALID") == "isValid");
    CHECK(to_camel_case("_private_field") == "_privateField");
}

TEST_CASE("Casing Utilities: SCREAMING_SNAKE_CASE", "[naming][casing]") {
    using namespace k::plugin::naming;
    CHECK(to_screaming_snake_case("max_size") == "MAX_SIZE");
    CHECK(to_screaming_snake_case("maxSize") == "MAX_SIZE");
    CHECK(to_screaming_snake_case("MaxSize") == "MAX_SIZE");
    CHECK(to_screaming_snake_case("MAX_SIZE") == "MAX_SIZE");
    CHECK(to_screaming_snake_case("_internal_const") == "_INTERNAL_CONST");
}

TEST_CASE("Casing Utilities: snake_case", "[naming][casing]") {
    using namespace k::plugin::naming;
    CHECK(to_snake_case("MyNamespace") == "my_namespace");
    CHECK(to_snake_case("my_namespace") == "my_namespace");
    CHECK(to_snake_case("MY_NAMESPACE") == "my_namespace");
}

// ── 2. Model Refactoring Engine & Use-Def Tracking ──────────────────────────

TEST_CASE("Model Refactoring: change_name propagates to references", "[model][refactoring]") {
    auto comp = k::compiler::create();
    auto u = comp->get_unit();
    auto root_ns = u->get_root_namespace();

    // Create a variable in root_ns
    auto var = root_ns->append_variable("oldName");
    REQUIRE(var != nullptr);
    CHECK(var->get_short_name() == "oldName");
    CHECK(root_ns->get_variable("oldName") == var);

    // Create symbol_expression referencing var
    auto sym_ref = k::model::symbol_expression::from_variable(var);
    REQUIRE(sym_ref != nullptr);
    CHECK(sym_ref->get_name().to_string() == "oldName");

    // Check Use-Def registration
    CHECK(var->get_references().size() == 1);

    // Test collision check before rename
    root_ns->append_variable("alreadyExists");
    std::string reason;
    CHECK_FALSE(var->can_change_name("alreadyExists", &reason));
    CHECK_FALSE(reason.empty());

    // Execute rename: oldName -> newName
    bool success = var->change_name("newName");
    CHECK(success);
    CHECK(var->get_short_name() == "newName");

    // Check re-keying in parent variable_holder
    CHECK(root_ns->get_variable("oldName") == nullptr);
    CHECK(root_ns->get_variable("newName") == var);

    // Check automatic propagation to referencing symbol_expression
    CHECK(sym_ref->get_name().to_string() == "newName");
}

// ── 3. End-to-end Naming Plugin JIT Execution ───────────────────────────────

TEST_CASE("Plugin Naming: End-to-End JIT Renaming and Execution", "[plugin][naming][live]") {
    const std::string_view src = R"k(
        module test_naming_e2e;

        public struct my_data {
            My_Field : int;
            max_value : const int = 100;

            Compute_Sum(delta : int) : int {
                return My_Field + delta;
            }
        }

        public Run_Test() : int {
            d : my_data;
            d.My_Field = 42;
            if (d.Compute_Sum(8) != 50) return 1;
            if (d.max_value != 100) return 2;
            return 0;
        }
    )k";

    auto comp = make_compiler_with_naming_plugin();
    comp->parse_source("test_naming.k", src);

    auto jit = comp->to_jit();
    REQUIRE(jit != nullptr);

    // Run_Test was renamed to runTest by the naming pass
    auto fn = jit->lookup_symbol<int(*)()>("runTest");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 0);
}

// ── 4. Collision Avoidance in Naming Plugin ─────────────────────────────────

TEST_CASE("Plugin Naming: Collision Avoidance Preserves Existing Code", "[plugin][naming]") {
    const std::string_view src = R"k(
        module test_naming_collision;

        public struct Pair {
            item : int;
            Item : int; // Renaming to 'item' would collide with existing field

            public test_collision() : int {
                return item + Item;
            }
        }

        public test_runner() : int {
            p : Pair;
            p.item = 10;
            p.Item = 20;
            if (p.test_collision() != 30) return 1;
            return 0;
        }
    )k";

    auto comp = make_compiler_with_naming_plugin();
    comp->parse_source("test_collision.k", src);

    auto jit = comp->to_jit();
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<int(*)()>("testRunner");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 0);
}
