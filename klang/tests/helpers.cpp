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

#include "helpers.hpp"

#include <unistd.h>

#include <fmt/core.h>
#include <fmt/format.h>
#include <fmt/args.h>

#include <catch2/catch_all.hpp>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/TargetParser/Host.h>

#include "../src/common/logger.hpp"
#include "../src/parse/parser.hpp"
#include "../src/parse/ast_dump.hpp"
#include "../src/model/model.hpp"
#include "../src/model/model_builder.hpp"
#include "../src/model/model_dump.hpp"
#include "../src/gen/resolvers.hpp"
#include "../src/gen/generators.hpp"
#include "../src/compiler.hpp"

std::unique_ptr<k::model::gen::jit> gen_jit(std::string_view src, bool dump, bool optimize) {
    auto comp = k::compiler::create();
    try {
        comp->parse_source("", src, optimize, dump);
        return comp->to_jit();
    } catch (const k::log::compiler_error&) {
        // Diagnostic already printed by the compiler's report() before the throw.
        // Signal failure to the caller.
        return nullptr;
    } catch (std::exception& ex) {
        std::cerr << "Unexpected error during compilation: " << ex.what() << std::endl;
        return nullptr;
    }
}

std::unique_ptr<k::model::gen::jit> gen_jit_throws(std::string_view src, bool dump, bool optimize) {
    // parse_source always rethrows — compiler_error subclasses propagate directly to the caller.
    auto comp = k::compiler::create();
    comp->parse_source("", src, optimize, dump);
    return comp->to_jit();
}


bool compile_text(const std::string_view& source, const std::string& out_file) {
    // Ensure LLVM targets are registered before any target lookup.
    k::compiler::initialize();

    std::string target_triple = llvm::sys::getDefaultTargetTriple();
    std::string error;
    auto target = llvm::TargetRegistry::lookupTarget(target_triple, error);
    if(!target) {
        std::cerr << "Problem to find target: " << error << std::endl;
    }

    std::string cpu = "generic";
    std::string features = "";

    llvm::TargetOptions target_options;
    // Use PIC relocation model so the generated object is compatible with
    // PIE executables (avoids R_X86_64_32S relocations against .rodata).
    std::optional<llvm::Reloc::Model> reloc_model = llvm::Reloc::PIC_;
    auto target_machine = target->createTargetMachine(
            target_triple, cpu,
            features,
            target_options,
            reloc_model);

    auto compiler = k::compiler::create(target_machine);
    try {
        compiler->parse_source("", source, true, false);
    } catch (const k::log::compiler_error&) {
        return false;
    }
    return compiler->gen_executable(out_file);
}

k::tools::exec_result build_and_exec(const std::string_view& src) {
    char out_file[] = "/tmp/klang_test_XXXXXX";
    int fd = ::mkstemp(out_file);
    if (fd == -1) {
        throw std::runtime_error("Could not create temporary output file for building test");
    }
    ::close(fd);

    if (!compile_text(src, out_file)) {
        std::filesystem::remove(out_file);
        throw std::runtime_error("Error building source code");
    }

    auto res = k::tools::run_process(out_file, {});
    std::filesystem::remove(out_file);
    return res;
}

// ---------------------------------------------------------------------------
// Internal helper: create a PIC target machine for library tests
// ---------------------------------------------------------------------------
static llvm::TargetMachine* make_pic_target_machine() {
    k::compiler::initialize();

    std::string target_triple = llvm::sys::getDefaultTargetTriple();
    std::string error;
    auto target = llvm::TargetRegistry::lookupTarget(target_triple, error);
    if (!target) {
        throw std::runtime_error("Could not find LLVM target: " + error);
    }

    std::optional<llvm::Reloc::Model> reloc_model = llvm::Reloc::PIC_;
    return target->createTargetMachine(target_triple, "generic", "", {}, reloc_model);
}

// ---------------------------------------------------------------------------

std::string build_shared_library(const std::string_view& src) {
    char tmp_stem[] = "/tmp/klang_lib_test_XXXXXX";
    int fd = ::mkstemp(tmp_stem);
    if (fd == -1) {
        throw std::runtime_error("Could not create temporary file for shared library test");
    }
    ::close(fd);
    std::string out_file = std::string(tmp_stem) + ".so";
    std::filesystem::remove(tmp_stem);

    auto compiler = k::compiler::create(make_pic_target_machine());
    try {
        compiler->parse_source("", src, true, false);
    } catch (const k::log::compiler_error&) {
        throw std::runtime_error("Compilation error while building shared library");
    }

    if (!compiler->gen_shared_library(out_file)) {
        std::filesystem::remove(out_file);
        throw std::runtime_error("Error generating shared library");
    }

    return out_file;
}

std::string build_static_library(const std::string_view& src) {
    char tmp_stem[] = "/tmp/klang_lib_test_XXXXXX";
    int fd = ::mkstemp(tmp_stem);
    if (fd == -1) {
        throw std::runtime_error("Could not create temporary file for static library test");
    }
    ::close(fd);
    std::string out_file = std::string(tmp_stem) + ".a";
    std::filesystem::remove(tmp_stem);

    auto compiler = k::compiler::create(make_pic_target_machine());
    try {
        compiler->parse_source("", src, true, false);
    } catch (const k::log::compiler_error&) {
        throw std::runtime_error("Compilation error while building static library");
    }

    if (!compiler->gen_static_library(out_file)) {
        std::filesystem::remove(out_file);
        throw std::runtime_error("Error generating static library");
    }

    return out_file;
}

std::pair<std::string, std::string> build_both_libraries(const std::string_view& src) {
    // Generate unique names for both outputs
    char tmp_so[] = "/tmp/klang_lib_test_XXXXXX";
    int fd_so = ::mkstemp(tmp_so);
    if (fd_so == -1) throw std::runtime_error("Could not create temp file for .so");
    ::close(fd_so);
    std::string so_file = std::string(tmp_so) + ".so";
    std::filesystem::remove(tmp_so);

    char tmp_a[] = "/tmp/klang_lib_test_XXXXXX";
    int fd_a = ::mkstemp(tmp_a);
    if (fd_a == -1) throw std::runtime_error("Could not create temp file for .a");
    ::close(fd_a);
    std::string a_file = std::string(tmp_a) + ".a";
    std::filesystem::remove(tmp_a);

    auto compiler = k::compiler::create(make_pic_target_machine());
    try {
        compiler->parse_source("", src, true, false);
    } catch (const k::log::compiler_error&) {
        throw std::runtime_error("Compilation error while building libraries");
    }

    if (!compiler->gen_libraries(so_file, a_file)) {
        std::filesystem::remove(so_file);
        std::filesystem::remove(a_file);
        throw std::runtime_error("Error generating libraries");
    }

    return {so_file, a_file};
}


//
// test_logger
//

void test_logger::report(const k::log::diagnostic& diag) {
    static constexpr auto FORMAT = "{:0>5X} : {}\n";

    // Store for post-hoc inspection
    diagnostics.push_back(diag);

    std::string msg = diag.message;
    if (!diag.args.empty()) {
        fmt::dynamic_format_arg_store<fmt::format_context> store;
        for (const auto& arg : diag.args) store.push_back(arg);
        try { msg = fmt::vformat(diag.message, store); } catch(...) {}
    }

    std::string str = fmt::format(FORMAT, diag.code, msg);

    switch (diag.level) {
        case k::log::diagnostic::severity::info: {
            INFO("INFO " << str);
            break;
        }
        case k::log::diagnostic::severity::warning: {
            INFO("WARN " << str);
            break;
        }
        case k::log::diagnostic::severity::error:
        case k::log::diagnostic::severity::fatal: {
            WARN("ERR  " << str);
            break;
        }
    }

    for (const auto& note : diag.notes) {
        std::string note_msg = note.message;
        if (!note.args.empty()) {
            fmt::dynamic_format_arg_store<fmt::format_context> store;
            for (const auto& arg : note.args) store.push_back(arg);
            try { note_msg = fmt::vformat(note.message, store); } catch(...) {}
        }
        INFO("NOTE " << note_msg);
    }
}
