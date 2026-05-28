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

#include "compiler.hpp"
#include "config.h"

#include "config.h"
#include "common/path_lookup_file_resolver.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unordered_set>
#include <llvm/IR/Verifier.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/Transforms/Scalar.h>
#include <llvm/Transforms/InstCombine/InstCombine.h>
#include <llvm/Transforms/Scalar/GVN.h>

#include <llvm/ExecutionEngine/Orc/CompileUtils.h>
#include <llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h>
#include <llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h>
#include <llvm/ExecutionEngine/SectionMemoryManager.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/IR/LegacyPassManager.h>

#include <fmt/core.h>
#include <fmt/format.h>
#include <fmt/args.h>

#include "common/process.hpp"
#include "gen/resolvers.hpp"
#include "gen/generators.hpp"
#include "parse/ast_dump.hpp"
#include "model/model_builder.hpp"
#include "model/model_dump.hpp"
#include "model/tools/kdi_exporter.hpp"
#include "model/tools/kdi_importer.hpp"
#include "common/path_lookup_file_resolver.hpp"

#include <kdi.hpp>
#include "errors.hpp"

namespace k {

bool compiler::gen_executable(const std::string& output_file) {
    if (!has_main_method()) {
        std::cerr << "Cannot generate executable : no main function." << std::endl;
        return false;
    }

    std::filesystem::path output_path(
        output_file.empty() ? unit_name_to_lib_base(get_unit()->get_unit_name().to_string())
                            : output_file);
    resolve_ir_filenames(output_path.string());

    auto object_path = std::filesystem::temp_directory_path() / (output_path.filename().generic_string() + ".o");

    std::cout << "Generating object: " << object_path << std::endl;
    gen_object_file(object_path);

    std::cout << "Generating executable: " << output_path << std::endl;
    tools::exec_result exec_res;
    try {
        std::vector<std::string> clang_args = {"-pie", "-o", output_path.string(), object_path.string()};
        // Append extra pre-compiled object files (.o)
        clang_args.insert(clang_args.end(), _extra_object_files.begin(), _extra_object_files.end());
        // Append -L/-l flags for used imports
        auto import_args = build_import_link_args();
        clang_args.insert(clang_args.end(), import_args.begin(), import_args.end());
        // Use clang++ (not clang) so the C++ ABI runtime (exceptions, RTTI)
        // is linked automatically — K exceptions use the Itanium C++ ABI.
        exec_res = tools::lookup_run_process("clang++", clang_args);
    } catch (const tools::tool_not_found& e) {
        std::cerr << "Error: " << e.what() << " (needed to link executable)" << std::endl;
        std::filesystem::remove(object_path);
        return false;
    }

    std::filesystem::remove(object_path);

    if (!exec_res.out.empty()) std::cout << exec_res.out << std::endl;
    if (!exec_res.err.empty()) std::cerr << exec_res.err << std::endl;
    return exec_res.exit_code == 0;
}

// ---------------------------------------------------------------------------
// Library name utilities
// ---------------------------------------------------------------------------

std::string compiler::unit_name_to_lib_base(const std::string& unit_name) {
    std::string base = unit_name;
    size_t pos = 0;
    while ((pos = base.find("::", pos)) != std::string::npos) {
        base.replace(pos, 2, ".");
        pos += 1;
    }
    return base;
}

std::string compiler::get_lib_base_name() const {
    return unit_name_to_lib_base(get_unit()->get_unit_name().to_string());
}

// ---------------------------------------------------------------------------
// Import link args
// ---------------------------------------------------------------------------

std::vector<std::string> compiler::build_import_link_args() const {
    std::vector<std::string> args;

    if (!_model_unit) return args;

    // ── -L<dir> and -rpath<dir> flags from the file resolver search paths ──
    if (auto plr = std::dynamic_pointer_cast<const path_lookup_file_resolver>(_file_resolver)) {
        for (const auto& dir : plr->get_lib_search_dirs()) {
            args.push_back("-L" + dir.string());
            args.push_back("-Wl,-rpath," + dir.string());
        }
    }

    // ── -l<base> for each used direct import ──────────────────────────────
    // Track added lib bases to avoid duplicates (direct + transitive may overlap)
    std::unordered_set<std::string> added_libs;

    // Always link the base standard library (libk) unless we ARE building it.
    // The stdlib provides essential runtime functions (__k_fatal_memory_allocation,
    // __k_fatal_null_dereference, etc.) that the compiler emits calls to in
    // generated code, even if the user module doesn't explicitly import k.
    {
        const auto unit_name = _model_unit->get_unit_name().to_string();
        if (unit_name != "k") {
            added_libs.insert("k");
            args.push_back("-lk");
            // Ensure the stdlib lib directory is in the linker search path
            // even if the user's file resolver doesn't include it.
#if defined(KLANG_STDLIB_LIB_DIR)
            const std::string stdlib_lib_dir(KLANG_STDLIB_LIB_DIR);
            if (!stdlib_lib_dir.empty()) {
                args.push_back("-L" + stdlib_lib_dir);
                args.push_back("-Wl,-rpath," + stdlib_lib_dir);
            }
#endif
        }
    }

    for (const auto& imp : _model_unit->get_imports()) {
        if (!imp.used) continue;
        if (!imp.kdi)  continue;
        const std::string& lib_base = imp.kdi->header.lib_base;
        if (!lib_base.empty() && added_libs.insert(lib_base).second) {
            args.push_back("-l" + lib_base);
        }
    }

    // ── -l<base> for transitive dependencies of used imports ──────────────
    // When lib A depends on lib B, and an executable imports A, the linker
    // needs -lB as well because libA.so has a DT_NEEDED on libB.so and the
    // linker must resolve its undefined symbols at link time.
    for (const auto& tkdi : _model_unit->get_transitive_kdis()) {
        if (!tkdi) continue;
        const std::string& lib_base = tkdi->header.lib_base;
        if (!lib_base.empty() && added_libs.insert(lib_base).second) {
            args.push_back("-l" + lib_base);
        }
    }

    return args;
}

bool compiler::gen_kdi(const std::string& lib_path) {
    // --no-emit-kdi: silently skip KDI generation
    if (_ir_output_options.no_emit_kdi) return true;

    if (!_model_unit) {
        std::cerr << "gen_kdi: no compiled unit available." << std::endl;
        return false;
    }

    // Derive .kdi path: same stem as lib_path, extension = ".kdi"
    std::filesystem::path kdi_path(lib_path);
    kdi_path.replace_extension(".kdi");

    const std::string ver = "klangc-" + std::string(PROJECT_VER);

    kdi::kdi_file kdi_file = k::model::build_kdi(*_context, *_model_unit, lib_path, ver);

    if (!kdi::kdi_write_cbor_file(kdi_file, kdi_path.string())) {
        std::cerr << "gen_kdi: failed to write '" << kdi_path << "'" << std::endl;
        return false;
    }
    std::cout << "Generated KDI: " << kdi_path << std::endl;

    // Optionally also write the JSON equivalent (.kdi.json)
    if (_ir_output_options.emit_kdi_json) {
        std::string json_path = kdi_path.string() + ".json";
        if (!kdi::kdi_write_json_file(kdi_file, json_path)) {
            std::cerr << "gen_kdi: failed to write JSON '" << json_path << "'" << std::endl;
        } else {
            std::cout << "Generated KDI JSON: " << json_path << std::endl;
        }
    }
    return true;
}

bool compiler::gen_shared_library(const std::string& output_file) {
    std::filesystem::path output_path(
        output_file.empty() ? "lib" + get_lib_base_name() + ".so"
                            : output_file);
    resolve_ir_filenames(output_path.string());

    auto object_path = std::filesystem::temp_directory_path() / (output_path.filename().generic_string() + ".o");

    std::cout << "Generating object: " << object_path << std::endl;
    gen_object_file(object_path);

    std::cout << "Generating shared library: " << output_path << std::endl;
    tools::exec_result exec_res;
    try {
        std::vector<std::string> clang_args = {"-shared", "-fPIC", "-o", output_path.string(), object_path.string()};
        // Append extra pre-compiled object files (.o)
        clang_args.insert(clang_args.end(), _extra_object_files.begin(), _extra_object_files.end());
        auto import_args = build_import_link_args();
        clang_args.insert(clang_args.end(), import_args.begin(), import_args.end());
        exec_res = tools::lookup_run_process("clang++", clang_args);
    } catch (const tools::tool_not_found& e) {
        std::cerr << "Error: " << e.what() << " (needed to link shared library)" << std::endl;
        std::filesystem::remove(object_path);
        return false;
    }

    std::filesystem::remove(object_path);

    if (!exec_res.out.empty()) std::cout << exec_res.out << std::endl;
    if (!exec_res.err.empty()) std::cerr << exec_res.err << std::endl;
    if (exec_res.exit_code == 0) gen_kdi(output_path.string());
    return exec_res.exit_code == 0;
}

bool compiler::gen_static_library(const std::string& output_file) {
    std::filesystem::path output_path(
        output_file.empty() ? "lib" + get_lib_base_name() + ".a"
                            : output_file);
    resolve_ir_filenames(output_path.string());

    auto object_path = std::filesystem::temp_directory_path() / (output_path.filename().generic_string() + ".o");

    std::cout << "Generating object: " << object_path << std::endl;
    gen_object_file(object_path);

    std::cout << "Generating static library: " << output_path << std::endl;
    // ar rcs: create archive, add index, be silent on missing files
    tools::exec_result exec_res;
    try {
        std::vector<std::string> ar_args = {"rcs", output_path.string(), object_path.string()};
        // Append extra pre-compiled object files (.o)
        ar_args.insert(ar_args.end(), _extra_object_files.begin(), _extra_object_files.end());
        exec_res = tools::lookup_run_process("ar", ar_args);
    } catch (const tools::tool_not_found& e) {
        std::cerr << "Error: " << e.what() << " (needed to create static library)" << std::endl;
        std::filesystem::remove(object_path);
        return false;
    }

    std::filesystem::remove(object_path);

    if (!exec_res.out.empty()) std::cout << exec_res.out << std::endl;
    if (!exec_res.err.empty()) std::cerr << exec_res.err << std::endl;
    if (exec_res.exit_code == 0) gen_kdi(output_path.string());
    return exec_res.exit_code == 0;
}

bool compiler::gen_libraries(const std::string& shared_out, const std::string& static_out) {
    // Determine output paths up front
    const std::string base = get_lib_base_name();
    std::filesystem::path so_path(shared_out.empty() ? "lib" + base + ".so" : shared_out);
    std::filesystem::path  a_path(static_out.empty() ? "lib" + base + ".a"  : static_out);

    resolve_ir_filenames(so_path.string());

    // Generate the object file once
    auto object_path = std::filesystem::temp_directory_path() / ("lib" + base + ".o");
    std::cout << "Generating object: " << object_path << std::endl;
    if (!gen_object_file(object_path)) {
        return false;
    }

    bool ok = true;

    // Shared library
    std::cout << "Generating shared library: " << so_path << std::endl;
    tools::exec_result so_res;
    try {
        std::vector<std::string> clang_args = {"-shared", "-fPIC", "-o", so_path.string(), object_path.string()};
        // Append extra pre-compiled object files (.o)
        clang_args.insert(clang_args.end(), _extra_object_files.begin(), _extra_object_files.end());
        auto import_args = build_import_link_args();
        clang_args.insert(clang_args.end(), import_args.begin(), import_args.end());
        so_res = tools::lookup_run_process("clang++", clang_args);
    } catch (const tools::tool_not_found& e) {
        std::cerr << "Error: " << e.what() << " (needed to link shared library)" << std::endl;
        std::filesystem::remove(object_path);
        return false;
    }
    if (!so_res.out.empty()) std::cout << so_res.out << std::endl;
    if (!so_res.err.empty()) std::cerr << so_res.err << std::endl;
    ok &= (so_res.exit_code == 0);

    // Static library
    std::cout << "Generating static library: " << a_path << std::endl;
    tools::exec_result ar_res;
    try {
        std::vector<std::string> ar_args = {"rcs", a_path.string(), object_path.string()};
        // Append extra pre-compiled object files (.o)
        ar_args.insert(ar_args.end(), _extra_object_files.begin(), _extra_object_files.end());
        ar_res = tools::lookup_run_process("ar", ar_args);
    } catch (const tools::tool_not_found& e) {
        std::cerr << "Error: " << e.what() << " (needed to create static library)" << std::endl;
        std::filesystem::remove(object_path);
        return false;
    }
    if (!ar_res.out.empty()) std::cout << ar_res.out << std::endl;
    if (!ar_res.err.empty()) std::cerr << ar_res.err << std::endl;
    ok &= (ar_res.exit_code == 0);

    std::filesystem::remove(object_path);
    // Generate KDI once (keyed on the .so path; same content for both libs)
    if (ok) gen_kdi(so_path.string());
    return ok;
}

} // k
