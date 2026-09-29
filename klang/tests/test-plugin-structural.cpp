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
#include "../src/plugin/structural/structural_pass.hpp"

namespace {

/** Helper to build a compiler with stdlib and default plugins enabled (e.g. static structural plugin). */
std::shared_ptr<k::compiler> make_default_compiler() {
    ensure_libk_loaded();
    auto comp = k::compiler::create();

    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_search_dir(std::filesystem::current_path());
#if defined(KLANG_STDLIB_LIB_DIR)
    resolver->add_search_dir(std::string(KLANG_STDLIB_LIB_DIR));
#endif
    comp->set_file_resolver(resolver);
    return comp;
}

/** Helper to build a compiler with stdlib and structural pass registered programmatically. */
std::shared_ptr<k::compiler> make_compiler_with_structural_pass() {
    ensure_libk_loaded();
    auto comp = k::compiler::create();

    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_search_dir(std::filesystem::current_path());
#if defined(KLANG_STDLIB_LIB_DIR)
    resolver->add_search_dir(std::string(KLANG_STDLIB_LIB_DIR));
#endif
    comp->set_file_resolver(resolver);

    comp->register_model_pass(std::make_unique<k::plugin::structural::structural_pass>());
    return comp;
}

/** Helper to build a compiler with stdlib and dynamic plugin loaded from .so. */
std::shared_ptr<k::compiler> make_compiler_with_dynamic_structural() {
    ensure_libk_loaded();
    auto comp = k::compiler::create();

    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_search_dir(std::filesystem::current_path());
#if defined(KLANG_STDLIB_LIB_DIR)
    resolver->add_search_dir(std::string(KLANG_STDLIB_LIB_DIR));
#endif
    comp->set_file_resolver(resolver);

    // Look for plugin .so in candidate directories
    std::vector<std::filesystem::path> candidate_paths = {
#if defined(KLANG_STDLIB_LIB_DIR)
        std::filesystem::path(KLANG_STDLIB_LIB_DIR).parent_path().parent_path() / "klang" / "libklang-plugin-structural.so",
#endif
        std::filesystem::current_path() / "klang" / "libklang-plugin-structural.so",
        std::filesystem::current_path() / "cmake-build-debug" / "klang" / "libklang-plugin-structural.so",
        "klang/libklang-plugin-structural.so",
        "cmake-build-debug/klang/libklang-plugin-structural.so",
        "libklang-plugin-structural.so"
    };

    std::filesystem::path plugin_path;
    for (const auto& p : candidate_paths) {
        if (std::filesystem::exists(p)) {
            plugin_path = p;
            break;
        }
    }

    bool loaded = comp->load_plugin(plugin_path);
    REQUIRE(loaded);

    return comp;
}

} // anonymous namespace

TEST_CASE("Plugin Structural: Programmatic Pass with Explicit Public Visibilities", "[plugin][structural]") {
    const std::string_view src = R"k(
        module test_structural_struct;
        using namespace k::lang;

        public struct Point {
            @::k::lang::Getter(AccessLevel::PUBLIC, AccessLevel::PUBLIC)
            @::k::lang::Setter(AccessLevel::PUBLIC)
            x : int;

            @::k::lang::Getter(AccessLevel::PUBLIC, AccessLevel::PUBLIC)
            @::k::lang::Setter(AccessLevel::PUBLIC)
            y : int;
        }

        public test_point() : int {
            pt : Point;
            // Chained setter returning this
            pt.x(10).y(20);

            // Read via mutable getter
            if (pt.x() != 10) return 1;
            if (pt.y() != 20) return 2;

            // Mutate via mutable getter
            pt.x() = 100;
            if (pt.x() != 100) return 3;

            // Read via const getter
            pt_ref : const Point& = pt;
            if (pt_ref.x() != 100) return 4;
            if (pt_ref.y() != 20) return 5;

            return 0;
        }
    )k";

    auto comp = make_compiler_with_structural_pass();
    comp->parse_source("test_point.k", src);

    auto jit = comp->to_jit();
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<int(*)()>("test_point");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 0);
}

TEST_CASE("Plugin Structural: Default Visibilities (Public Const, Protected Mut & Setter)", "[plugin][structural]") {
    const std::string_view src = R"k(
        module test_structural_defaults;

        public struct DataHolder {
            @::k::lang::Getter
            @::k::lang::Setter
            val : int;

            // Inside member method, protected setter and mutable getter are accessible!
            public init_and_mutate() : int {
                val(42);
                val() = 84;
                return 0;
            }
        }

        public test_defaults() : int {
            d : DataHolder;
            if (d.init_and_mutate() != 0) return 1;

            // Const getter is public by default, accessible from outside
            d_const : const DataHolder& = d;
            if (d_const.val() != 84) return 2;

            return 0;
        }
    )k";

    auto comp = make_default_compiler();
    comp->parse_source("test_defaults.k", src);

    auto jit = comp->to_jit();
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<int(*)()>("test_defaults");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 0);
}

TEST_CASE("Plugin Structural: NO_ACCESS blocks accessor generation", "[plugin][structural]") {
    const std::string_view src = R"k(
        module test_structural_no_access;
        using namespace k::lang;

        public struct ReadOnlyItem {
            @::k::lang::Getter(AccessLevel::PUBLIC, AccessLevel::NO_ACCESS)
            @::k::lang::Setter(AccessLevel::NO_ACCESS)
            code : int;

            public init(c : int) {
                this.code = c;
            }
        }

        public test_readonly() : int {
            item : ReadOnlyItem;
            item.init(123);

            // Const getter works
            item_const : const ReadOnlyItem& = item;
            if (item_const.code() != 123) return 1;

            return 0;
        }
    )k";

    auto comp = make_default_compiler();
    comp->parse_source("test_readonly.k", src);

    auto jit = comp->to_jit();
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<int(*)()>("test_readonly");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 0);
}

TEST_CASE("Plugin Structural: Designated Init on Getter and Setter", "[plugin][structural]") {
    const std::string_view src = R"k(
        module test_structural_designated;
        using namespace k::lang;

        public struct ConfigItem {
            @::k::lang::Getter{.constAccess = AccessLevel::PUBLIC, .mutAccess = AccessLevel::NO_ACCESS}
            @::k::lang::Setter{.access = AccessLevel::PUBLIC}
            setting : int;
        }

        public test_designated() : int {
            cfg : ConfigItem;
            cfg.setting(777);

            cfg_const : const ConfigItem& = cfg;
            if (cfg_const.setting() != 777) return 1;

            return 0;
        }
    )k";

    auto comp = make_default_compiler();
    comp->parse_source("test_designated.k", src);

    auto jit = comp->to_jit();
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<int(*)()>("test_designated");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 0);
}

TEST_CASE("Plugin Structural: Dynamic Loading .so on Class and Static Fields", "[plugin][structural]") {
    const std::string_view src = R"k(
        module test_structural_class;
        using namespace k::lang;

        public class Counter {
            @::k::lang::Getter(AccessLevel::PUBLIC, AccessLevel::PUBLIC)
            @::k::lang::Setter(AccessLevel::PUBLIC)
            value : int;

            @::k::lang::Getter(AccessLevel::PUBLIC, AccessLevel::NO_ACCESS)
            @::k::lang::Setter(AccessLevel::PUBLIC)
            static total : int;
        }

        public test_class() : int {
            c : Counter;
            c.value(42);

            if (c.value() != 42) return 1;

            c.value() = 84;
            if (c.value() != 84) return 2;

            // Test static field accessors
            Counter::total(100);
            if (Counter::total() != 100) return 3;

            return 0;
        }
    )k";

    auto comp = make_compiler_with_dynamic_structural();
    comp->parse_source("test_class.k", src);

    auto jit = comp->to_jit();
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<int(*)()>("test_class");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 0);
}

TEST_CASE("Plugin Structural: Pre-existing Accessors Not Overwritten", "[plugin][structural]") {
    const std::string_view src = R"k(
        module test_structural_manual;
        using namespace k::lang;

        public struct Box {
            @::k::lang::Getter(AccessLevel::PUBLIC, AccessLevel::PUBLIC)
            @::k::lang::Setter(AccessLevel::PUBLIC)
            size : int;

            // Manual getter overriding default getter behavior
            public const size() : const int& {
                return size;
            }
        }

        public test_box() : int {
            b : Box;
            b.size(50);
            if (b.size() != 50) return 1;
            return 0;
        }
    )k";

    auto comp = make_compiler_with_structural_pass();
    comp->parse_source("test_box.k", src);

    auto jit = comp->to_jit();
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<int(*)()>("test_box");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 0);
}

TEST_CASE("klangc CLI: --plugin flag with Structural JIT execution", "[plugin][structural][cli]") {
    auto klangc = find_klangc();

    std::vector<std::filesystem::path> candidate_paths = {
#if defined(KLANG_STDLIB_LIB_DIR)
        std::filesystem::path(KLANG_STDLIB_LIB_DIR).parent_path().parent_path() / "klang" / "libklang-plugin-structural.so",
#endif
        std::filesystem::current_path() / "klang" / "libklang-plugin-structural.so",
        std::filesystem::current_path() / "cmake-build-debug" / "klang" / "libklang-plugin-structural.so",
        "klang/libklang-plugin-structural.so",
        "cmake-build-debug/klang/libklang-plugin-structural.so",
        "libklang-plugin-structural.so"
    };

    std::filesystem::path plugin_path;
    for (const auto& p : candidate_paths) {
        if (std::filesystem::exists(p)) {
            plugin_path = p;
            break;
        }
    }
    REQUIRE(!plugin_path.empty());

    std::string src = R"(
        module test_cli_structural;
        using namespace k::lang;

        struct Data {
            @::k::lang::Getter(AccessLevel::PUBLIC, AccessLevel::PUBLIC)
            @::k::lang::Setter(AccessLevel::PUBLIC)
            val : int;
        }

        main() : int {
            d : Data;
            d.val(42);
            return d.val();
        }
    )";

    auto res = k::tools::run_process(
        klangc.string(),
        {"--plugin", plugin_path.string(), "--stdin", "--jit-exec"},
        src
    );

    INFO("klangc stdout: " << res.out);
    INFO("klangc stderr: " << res.err);
    CHECK(res.exit_code == 42);
}

TEST_CASE("Plugin Structural: Default Static Execution without explicit registration", "[plugin][structural][static]") {
    const std::string_view src = R"k(
        module test_structural_default_static;
        using namespace k::lang;

        public struct Vector {
            @::k::lang::Getter(AccessLevel::PUBLIC, AccessLevel::PUBLIC)
            @::k::lang::Setter(AccessLevel::PUBLIC)
            x : int;

            @::k::lang::Getter(AccessLevel::PUBLIC, AccessLevel::PUBLIC)
            @::k::lang::Setter(AccessLevel::PUBLIC)
            y : int;
        }

        public test_vec() : int {
            v : Vector;
            v.x(11).y(22);
            if (v.x() != 11 || v.y() != 22) return 1;
            return 0;
        }
    )k";

    // No load_plugin, no register_model_pass — structural plugin is enabled by default!
    auto comp = make_default_compiler();
    comp->parse_source("test_vec.k", src);

    auto jit = comp->to_jit();
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<int(*)()>("test_vec");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 0);
}

TEST_CASE("Plugin Structural: Disable default plugins via set_enable_default_plugins(false)", "[plugin][structural][static]") {
    const std::string_view src = R"k(
        module test_structural_disabled;
        using namespace k::lang;

        public struct Vector {
            @::k::lang::Getter(AccessLevel::PUBLIC, AccessLevel::PUBLIC)
            @::k::lang::Setter(AccessLevel::PUBLIC)
            x : int;
        }

        public test_fail() : int {
            v : Vector;
            v.x(11);
            return 0;
        }
    )k";

    auto comp = make_default_compiler();
    comp->set_enable_default_plugins(false);

    // Compilation must fail because structural plugin was disabled, so v.x(11) does not exist
    REQUIRE_THROWS(comp->parse_source("test_fail.k", src));
}

TEST_CASE("Plugin Structural: Disable via disable_plugin(\"structural\")", "[plugin][structural][static]") {
    const std::string_view src = R"k(
        module test_structural_named_disabled;
        using namespace k::lang;

        public struct Vector {
            @::k::lang::Getter(AccessLevel::PUBLIC, AccessLevel::PUBLIC)
            @::k::lang::Setter(AccessLevel::PUBLIC)
            x : int;
        }

        public test_fail() : int {
            v : Vector;
            v.x(11);
            return 0;
        }
    )k";

    auto comp = make_default_compiler();
    comp->disable_plugin("structural");

    // Compilation must fail because structural plugin was specifically disabled
    REQUIRE_THROWS(comp->parse_source("test_fail.k", src));
}

TEST_CASE("klangc CLI: Default execution runs Structural without --plugin flag", "[plugin][structural][cli]") {
    auto klangc = find_klangc();

    std::string src = R"(
        module test_cli_default;
        using namespace k::lang;

        struct Data {
            @::k::lang::Getter(AccessLevel::PUBLIC, AccessLevel::PUBLIC)
            @::k::lang::Setter(AccessLevel::PUBLIC)
            val : int;
        }

        main() : int {
            d : Data;
            d.val(99);
            return d.val();
        }
    )";

    // Notice: NO --plugin flag! Structural plugin is active by default.
    auto res = k::tools::run_process(
        klangc.string(),
        {"--stdin", "--jit-exec"},
        src
    );

    INFO("klangc stdout: " << res.out);
    INFO("klangc stderr: " << res.err);
    CHECK(res.exit_code == 99);
}

TEST_CASE("klangc CLI: --no-default-plugins disables Structural plugin", "[plugin][structural][cli]") {
    auto klangc = find_klangc();

    std::string src = R"(
        module test_cli_no_default;
        using namespace k::lang;

        struct Data {
            @::k::lang::Getter(AccessLevel::PUBLIC, AccessLevel::PUBLIC)
            @::k::lang::Setter(AccessLevel::PUBLIC)
            val : int;
        }

        main() : int {
            d : Data;
            d.val(99);
            return d.val();
        }
    )";

    // With --no-default-plugins, structural plugin is disabled so d.val(99) fails to compile
    auto res = k::tools::run_process(
        klangc.string(),
        {"--no-default-plugins", "--stdin", "--jit-exec"},
        src
    );

    CHECK(res.exit_code != 0);
}

TEST_CASE("klangc CLI: --disable-plugin=structural disables Structural plugin", "[plugin][structural][cli]") {
    auto klangc = find_klangc();

    std::string src = R"(
        module test_cli_disable_specific;
        using namespace k::lang;

        struct Data {
            @::k::lang::Getter(AccessLevel::PUBLIC, AccessLevel::PUBLIC)
            @::k::lang::Setter(AccessLevel::PUBLIC)
            val : int;
        }

        main() : int {
            d : Data;
            d.val(99);
            return d.val();
        }
    )";

    auto res = k::tools::run_process(
        klangc.string(),
        {"--disable-plugin=structural", "--stdin", "--jit-exec"},
        src
    );

    CHECK(res.exit_code != 0);
}

TEST_CASE("Plugin Structural: NoArgConstructor and AllArgsConstructor", "[plugin][structural][ctors]") {
    const std::string_view src = R"k(
        module test_structural_noarg_allargs;
        using namespace k::lang;

        @::k::lang::NoArgConstructor
        @::k::lang::AllArgsConstructor
        public struct Point {
            x : int = 5;
            y : int = 15;
        }

        public test_ctors() : int {
            // 1. Default constructor via @NoArgConstructor
            p1 : Point;
            if (p1.x != 5 || p1.y != 15) return 1;

            // 2. All-arguments constructor via @AllArgsConstructor
            p2 : Point(100, 200);
            if (p2.x != 100 || p2.y != 200) return 2;

            return 0;
        }
    )k";

    auto comp = make_default_compiler();
    comp->parse_source("test_noarg_allargs.k", src);

    auto jit = comp->to_jit();
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<int(*)()>("test_ctors");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 0);
}

TEST_CASE("Plugin Structural: NoArgConstructor and AllArgsConstructor with Inheritance", "[plugin][structural][ctors]") {
    const std::string_view src = R"k(
        module test_structural_inheritance;
        using namespace k::lang;

        public class Base {
            public baseVal : int = 10;

            public Base() {
                this.baseVal = 77;
            }
        }

        @::k::lang::NoArgConstructor
        @::k::lang::AllArgsConstructor
        public class Derived : public Base {
            public derivedVal : int;
        }

        public test_inheritance() : int {
            // 1. NoArgConstructor calls Base default constructor
            d1 : Derived;
            if (d1.baseVal != 77 || d1.derivedVal != 0) return 1;

            // 2. AllArgsConstructor calls Base default constructor and initializes derived fields
            d2 : Derived(55);
            if (d2.baseVal != 77 || d2.derivedVal != 55) return 2;

            return 0;
        }
    )k";

    auto comp = make_default_compiler();
    comp->parse_source("test_inheritance.k", src);

    auto jit = comp->to_jit();
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<int(*)()>("test_inheritance");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 0);
}

TEST_CASE("Plugin Structural: CopyConstructor and CopyAssigner", "[plugin][structural][copy]") {
    const std::string_view src = R"k(
        module test_structural_copy;
        using namespace k::lang;

        @::k::lang::AllArgsConstructor
        @::k::lang::CopyConstructor
        @::k::lang::CopyAssigner
        public struct Sample {
            a : int;
            b : int;
        }

        public test_copy() : int {
            s1 : Sample(10, 20);

            // Copy constructor
            s2 : Sample(s1);
            if (s2.a != 10 || s2.b != 20) return 1;

            // Copy assigner
            s3 : Sample(0, 0);
            s3 = s1;
            if (s3.a != 10 || s3.b != 20) return 2;

            return 0;
        }
    )k";

    auto comp = make_default_compiler();
    comp->parse_source("test_copy.k", src);

    auto jit = comp->to_jit();
    REQUIRE(jit != nullptr);

    auto fn = jit->lookup_symbol<int(*)()>("test_copy");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 0);
}

