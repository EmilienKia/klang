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
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Module.h>

#include "helpers.hpp"
#include "../src/errors.hpp"
#include "../src/gen/generators.hpp"
#include "../src/model/context.hpp"

namespace {

struct DiagnosticArtifacts {
    std::filesystem::path path;

    DiagnosticArtifacts() {
        static std::atomic<unsigned int> counter{0};
        path = std::filesystem::current_path() /
            ("klang_fatal_diagnostics_" + std::to_string(::getpid()) +
             "_" + std::to_string(++counter));
        REQUIRE(std::filesystem::create_directory(path));
    }

    ~DiagnosticArtifacts() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

constexpr std::string_view valid_source = R"SRC(
    module k;
    answer() : int { return 42; }
)SRC";

// Each overload needs one equally weighted conversion; neither is a duplicate.
constexpr std::string_view ambiguous_source = R"SRC(
    module k;
    pick(a: int, b: double) {}
    pick(a: double, b: int) {}
    main() : int { pick(1, 1); return 0; }
)SRC";

constexpr std::string_view template_symbol_source = R"SRC(
    module k;
    enum State { Pending; Ready; }
    template<typename T>
    struct Carrier {
        static invoke() : bool { return State::Missing == State::Ready; }
    }
    main() : int {
        if (Carrier<int>::invoke()) return 1;
        return 0;
    }
)SRC";

void require_failed_outputs(const std::shared_ptr<k::compiler>& comp,
                            const DiagnosticArtifacts& artifacts) {
    REQUIRE(comp->has_compilation_error());
    CHECK(comp->to_jit(false) == nullptr);
    CHECK_FALSE(comp->gen_object_file((artifacts.path / "failed.o").string()));
    CHECK_FALSE(comp->gen_executable((artifacts.path / "failed").string()));
    CHECK_FALSE(comp->gen_shared_library((artifacts.path / "failed.so").string()));
    CHECK_FALSE(comp->gen_static_library((artifacts.path / "failed.a").string()));
    CHECK_FALSE(comp->gen_libraries((artifacts.path / "failed.so").string(),
                                   (artifacts.path / "failed.a").string()));
    CHECK_FALSE(comp->gen_kdi((artifacts.path / "failed.so").string()));

    auto options = k::IrOutputOptions{};
    options.no_emit_kdi = true;
    comp->set_ir_output_options(options);
    CHECK_FALSE(comp->gen_kdi((artifacts.path / "failed.so").string()));
    CHECK(std::filesystem::is_empty(artifacts.path));
}

void add_unterminated_test_function(const std::shared_ptr<k::compiler>& comp) {
    auto ctx = comp->get_context_for_test();
    auto type = llvm::FunctionType::get(llvm::Type::getVoidTy(ctx->llvm_context()), false);
    auto function = llvm::Function::Create(type, llvm::Function::ExternalLinkage,
                                          "unterminated_test_function", ctx->module());
    llvm::BasicBlock::Create(ctx->llvm_context(), "entry", function);
}

} // namespace

TEST_CASE("Compiler errors latch failure through logger relays and filters",
          "[compiler][diagnostics][fatal-diagnostics]") {
    DiagnosticArtifacts artifacts;
    auto comp = k::compiler::create(make_pic_target_machine());
    comp->parse_source("valid.k", valid_source, false, false);
    REQUIRE_FALSE(comp->has_compilation_error());
    constexpr unsigned int code = 0x0170;
    comp->set_ignored_diagnostic_codes({code});
    comp->set_log_level(k::log::diagnostic::severity::fatal);
    k::log::logger_relay first(comp->diagnostics());
    k::log::logger_relay second(first);

    SECTION("Non-throwing error") {
        REQUIRE_NOTHROW(second.error(code, "Reported error must fail compilation"));
    }
    SECTION("Non-throwing fatal") {
        REQUIRE_NOTHROW(second.fatal(code, "Reported fatal must fail compilation"));
    }

    second.warn(code, "A later warning must not clear failure");
    require_failed_outputs(comp, artifacts);
}

TEST_CASE("Ambiguous overload diagnostics propagate and suppress all compiler outputs",
          "[compiler][diagnostics][fatal-diagnostics][resolution]") {
    DiagnosticArtifacts artifacts;
    auto comp = k::compiler::create(make_pic_target_machine());
    auto options = k::IrOutputOptions{};
    options.raw_ir_file = (artifacts.path / "failed.raw.ll").string();
    options.opt_ir_file = (artifacts.path / "failed.opt.ll").string();
    comp->set_ir_output_options(options);

    try {
        comp->parse_source("ambiguous.k", ambiguous_source, true, false);
        FAIL("Expected an ambiguous overload diagnostic");
    } catch (const k::model::gen::resolution_error& error) {
        const auto& diagnostic = error.get_diagnostic();
        CHECK(diagnostic.code ==
              static_cast<unsigned int>(k::diag::symbol_diag::ERR_OVERLOAD_AMBIGUOUS));
        CHECK(diagnostic.level == k::log::diagnostic::severity::error);
        CHECK(diagnostic.message.find("Ambiguous call") != std::string::npos);
        REQUIRE(diagnostic.args.size() == 2);
        CHECK(diagnostic.args[0] == "pick");
        CHECK(diagnostic.args[1] == "2");
        REQUIRE(diagnostic.notes.size() == 2);
        for (const auto* signature : {"pick(a : int, b : double)",
                                      "pick(a : double, b : int)"}) {
            CHECK(std::any_of(diagnostic.notes.begin(), diagnostic.notes.end(),
                [&](const k::log::diagnostic::note& note) {
                    return note.args.size() == 2 && note.args[1] == signature;
                }));
        }
    }
    require_failed_outputs(comp, artifacts);
}

TEST_CASE("Instantiated body symbol errors propagate failure and suppress outputs",
          "[compiler][diagnostics][fatal-diagnostics][template]") {
    DiagnosticArtifacts artifacts;
    auto comp = k::compiler::create(make_pic_target_machine());
    try {
        comp->parse_source("template-symbol.k", template_symbol_source, true, false);
        FAIL("Expected an instantiated-body symbol diagnostic");
    } catch (const k::model::gen::resolution_error& error) {
        CHECK(error.get_diagnostic().code ==
              static_cast<unsigned int>(k::diag::function_diag::ERR_FUNC_ANNOTATION_MISMATCH));
        CHECK(error.get_diagnostic().message.find("has no entry named") != std::string::npos);
    }
    require_failed_outputs(comp, artifacts);
}

TEST_CASE("LLVM verification failure blocks publication before files are opened",
          "[compiler][diagnostics][fatal-diagnostics][verification]") {
    DiagnosticArtifacts artifacts;
    auto comp = k::compiler::create(make_pic_target_machine());
    comp->parse_source("valid.k", valid_source, false, false);
    REQUIRE(comp->verify_gen_code());
    add_unterminated_test_function(comp);
    REQUIRE_FALSE(comp->has_compilation_error());

    SECTION("Explicit module verification") {
        CHECK_FALSE(comp->verify_gen_code());
    }
    SECTION("Object emission validates the module itself") {
        CHECK_FALSE(comp->gen_object_file((artifacts.path / "failed.o").string()));
    }
    SECTION("JIT transfer validates the module itself") {
        CHECK(comp->to_jit(false) == nullptr);
    }
    SECTION("KDI publication validates the module itself") {
        CHECK_FALSE(comp->gen_kdi((artifacts.path / "failed.so").string()));
    }
    SECTION("Optimization refuses an invalid module") {
        comp->optimize_gen_code();
    }

    require_failed_outputs(comp, artifacts);
}

TEST_CASE("Warnings still permit verified object generation",
          "[compiler][diagnostics][fatal-diagnostics]") {
    DiagnosticArtifacts artifacts;
    auto comp = k::compiler::create(make_pic_target_machine());
    comp->parse_source("valid.k", valid_source, false, false);
    comp->diagnostics().warn(0x0170, "Warnings do not fail compilation");
    REQUIRE_FALSE(comp->has_compilation_error());
    REQUIRE(comp->verify_gen_code());
    auto object = artifacts.path / "valid.o";
    REQUIRE(comp->gen_object_file(object.string()));
    CHECK(std::filesystem::file_size(object) > 0);
}

TEST_CASE("Native declaration ABI failures are explicit latched diagnostics",
          "[compiler][diagnostics][fatal-diagnostics][native-abi][regression]") {
    DiagnosticArtifacts artifacts;
    auto comp = k::compiler::create(make_pic_target_machine());
    comp->parse_source("native-abi.k", R"K(
        module k;
        native(value: int): int { return value; }
    )K", false, false);
    auto ctx = comp->get_context_for_test();
    auto function = comp->get_unit()->get_root_namespace()->get_function("native");
    REQUIRE(function);
    function->set_extern_c_symbol("diagnostic_native");
    function->assign_name(function->get_name());
    constexpr auto code = static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F01B);
    std::string expected;
    SECTION("Missing parameter type") {
        function->parameters().front()->set_type(nullptr);
        expected = "parameter 'value'";
    }
    SECTION("Unresolved parameter type") {
        function->parameters().front()->set_type(ctx->from_string("MissingABI"));
        expected = "MissingABI";
    }
    SECTION("Unresolved return type") {
        function->set_return_type(ctx->from_string("MissingReturnABI"));
        expected = "MissingReturnABI";
    }

    k::model::gen::declaration_generator declarations(comp->diagnostics(), *comp, ctx, *comp->get_unit());
    try {
        declarations.visit_function(*function);
        FAIL("Expected an explicit native ABI declaration diagnostic");
    } catch (const k::log::compiler_error& error) {
        CHECK(error.get_diagnostic().code == code);
        CHECK(std::string(error.what()).find("diagnostic_native") != std::string::npos);
        CHECK(std::string(error.what()).find(expected) != std::string::npos);
    }
    CHECK(ctx->module().getFunction("diagnostic_native") == nullptr);
    require_failed_outputs(comp, artifacts);
}

TEST_CASE("klangc errors exit nonzero without publishing binary KDI or IR",
          "[cli][live][diagnostics][fatal-diagnostics]") {
    DiagnosticArtifacts artifacts;
    auto klangc = find_klangc();
    std::string source;
    std::string expected_diagnostic;
    SECTION("Non-throwing overload error") {
        source = ambiguous_source;
        expected_diagnostic = "0050F";
    }
    SECTION("Template-instantiated body symbol error") {
        source = template_symbol_source;
        expected_diagnostic = "has no entry named";
    }

    const std::vector<std::vector<std::string>> modes = {
        {"-c"}, {"--dyn-lib"}, {"--static-lib"}, {}, {"--jit-exec"},
        {"--dyn-lib", "--static-lib"}
    };
    auto input = artifacts.path / "failed.k";
    {
        std::ofstream file(input);
        REQUIRE(file.is_open());
        file << source;
    }

    for (const auto& mode : modes) {
        CAPTURE(mode);
        std::vector<std::string> args = {
            input.string(), "-o", (artifacts.path / "failed").string(),
            "--raw-ir-file", (artifacts.path / "failed.raw.ll").string(),
            "--opt-ir-file", (artifacts.path / "failed.opt.ll").string(),
            "--emit-kdi-json"
        };
        args.insert(args.end(), mode.begin(), mode.end());
        // The dual-library mode derives names in its working directory.
        std::string command = "cd '" + artifacts.path.string() + "' && '" + klangc.string() + "'";
        for (const auto& arg : args) command += " '" + arg + "'";
        auto result = k::tools::run_process("/bin/sh", {"-c", command});
        INFO("klangc stdout: " << result.out);
        INFO("klangc stderr: " << result.err);
        CHECK(result.exit_code != 0);
        CHECK((result.out + result.err).find(expected_diagnostic) != std::string::npos);
        for (const auto& entry : std::filesystem::directory_iterator(artifacts.path)) {
            CHECK(entry.path() == input);
        }
    }
}
