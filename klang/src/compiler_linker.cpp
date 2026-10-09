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

#include <atomic>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unordered_set>
#include <unistd.h>
#include <llvm/ADT/ScopeExit.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/IR/Verifier.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/TargetParser/Triple.h>
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

namespace {

std::filesystem::path make_temp_object_path(const std::string& prefix) {
    static std::atomic<uint64_t> counter{0};
    auto pid = ::getpid();
    auto count = counter.fetch_add(1);
    auto filename = fmt::format("{}_{}_{}.o", prefix, pid, count);
    return std::filesystem::temp_directory_path() / filename;
}

bool create_static_archive(log::logger& logger,
                           const std::filesystem::path& output_path,
                           const std::filesystem::path& object_path,
                           const std::vector<std::string>& extra_objects) {
    const auto failure_code =
        static_cast<unsigned int>(diag::compiler_diag::ERR_CREATE_STATIC_LIB_FAILED);
    std::error_code ec;
    const auto destination = std::filesystem::absolute(output_path, ec);
    if (ec) {
        logger.error(failure_code, "Cannot resolve static library output '{}': {}",
                     {output_path.string(), ec.message()});
        return false;
    }
    llvm::SmallString<128> temp_dir;
    const auto prefix = destination.parent_path() /
        ("." + destination.filename().string() + ".tmp");
    ec = llvm::sys::fs::createUniqueDirectory(prefix.string(), temp_dir);
    if (ec) {
        logger.error(failure_code, "Cannot prepare static library '{}': {}",
                     {output_path.string(), ec.message()});
        return false;
    }

    const std::filesystem::path directory(temp_dir.c_str());
    const auto archive_path = directory / destination.filename();
    auto cleanup = llvm::make_scope_exit([&] {
        for (const auto& path : {archive_path, directory}) {
            std::error_code ec;
            std::filesystem::remove(path, ec);
            if (ec) {
                logger.error(failure_code, "Cannot remove static library temporary '{}': {}",
                             {path.string(), ec.message()});
            }
        }
    });

    // ar updates existing archives, so build a fresh one on the destination filesystem.
    tools::exec_result result;
    try {
        std::vector<std::string> args = {"rcs", archive_path.string(), object_path.string()};
        args.insert(args.end(), extra_objects.begin(), extra_objects.end());
        result = tools::lookup_run_process("ar", args);
    } catch (const tools::tool_not_found& e) {
        logger.error(
            static_cast<unsigned int>(diag::compiler_diag::ERR_TOOL_NOT_FOUND_CREATE_STATIC_LIB),
            "{} (needed to create static library)", {e.what()});
        return false;
    }
    if (!result.out.empty()) std::cout << result.out << std::endl;
    if (!result.err.empty()) std::cerr << result.err << std::endl;
    if (result.exit_code != 0) {
        logger.error(failure_code, "Cannot create static library '{}': ar exited with status {}",
                     {output_path.string(), std::to_string(result.exit_code)});
        return false;
    }

    std::filesystem::rename(archive_path, destination, ec);
    if (ec) {
        logger.error(failure_code, "Cannot publish static library '{}': {}",
                     {output_path.string(), ec.message()});
        return false;
    }
    return true;
}

} // anonymous namespace

void compiler::validate_export_symbol(std::string_view symbol) {
    if (!symbol.empty()) {
        bool valid = true;
        for (unsigned char c : symbol) {
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '$')) {
                valid = false;
                break;
            }
        }
        if (valid) return;
    }
    throw std::invalid_argument(
        "--export-symbol requires a nonempty exact native name using only ASCII "
        "letters, digits, '_', '.', or '$'; NUL, whitespace, commas, options and "
        "wildcard patterns are not allowed");
}

void compiler::set_export_symbols(std::vector<std::string> symbols) {
    std::set<std::string> exports;
    for (const auto& symbol : symbols) {
        validate_export_symbol(symbol);
        exports.insert(symbol);
    }
    _export_symbols = std::move(exports);
}

bool compiler::supports_export_symbols(const std::string& target_triple) {
    const llvm::Triple host(llvm::Triple::normalize(llvm::sys::getDefaultTargetTriple()));
    const llvm::Triple target(llvm::Triple::normalize(target_triple));
    return host.isOSLinux() && host.isOSBinFormatELF() &&
           target.isOSLinux() && target.isOSBinFormatELF();
}

bool compiler::check_export_symbol_output(bool executable) {
    if (_export_symbols.empty()) return true;
    if (!executable) {
        error(static_cast<unsigned int>(diag::compiler_diag::ERR_EXPORT_SYMBOL_INCOMPATIBLE_OUTPUT),
              "--export-symbol requires a GNU/Linux ELF executable; "
              "object, library, KDI and JIT output are not supported");
        return false;
    }
    if (!supports_export_symbols(_target->getTargetTriple().getTriple())) {
        error(static_cast<unsigned int>(diag::compiler_diag::ERR_EXPORT_SYMBOL_UNSUPPORTED_TARGET),
              "--export-symbol is supported only on a GNU/Linux ELF host and target");
        return false;
    }
    return true;
}

std::vector<std::string> compiler::build_export_symbol_link_args() const {
    std::vector<std::string> args;
    args.reserve(_export_symbols.size() * 2);
    for (const auto& symbol : _export_symbols) {
        // Pass one linker argument, not clang's comma-separated -Wl syntax.
        args.push_back("-Xlinker");
        args.push_back("--export-dynamic-symbol=" + symbol);
    }
    return args;
}

bool compiler::gen_executable(const std::string& output_file) {
    if (_has_compilation_error) return false;
    if (!check_export_symbol_output(true)) return false;
    if (!has_main_method()) {
        error(static_cast<unsigned int>(k::diag::compiler_diag::ERR_NO_MAIN_FOR_EXECUTABLE),
              "Cannot generate executable: no main function");
        return false;
    }

    std::filesystem::path output_path(
        output_file.empty() ? unit_name_to_lib_base(get_unit()->get_unit_name().to_string())
                            : output_file);
    resolve_ir_filenames(output_path.string());

    auto object_path = make_temp_object_path(output_path.stem().string());

    trace("[compiler::gen_executable] generating object '{}'", {object_path.string()});
    if (!emit_object_file(object_path, false)) {
        std::filesystem::remove(object_path);
        return false;
    }

    trace("[compiler::gen_executable] generating executable '{}'", {output_path.string()});
    tools::exec_result exec_res;
    try {
        std::vector<std::string> clang_args = {"-pie", "-o", output_path.string(), object_path.string()};
        clang_args.push_back("-Wl,--export-dynamic-symbol=__k_rtti_manifest_v1");
        clang_args.push_back("-Wl,-u,__k_rtti_manifest_v1");
        if (get_unit()->has_application_class()) {
            clang_args.push_back("-Wl,--export-dynamic-symbol=__k_application_anchor_v1");
            clang_args.push_back("-Wl,-u,__k_application_anchor_v1");
        }
        auto export_args = build_export_symbol_link_args();
        clang_args.insert(clang_args.end(), export_args.begin(), export_args.end());
        if (_debug_info_options.enabled) {
            clang_args.push_back("-g");
        }
        // Append extra pre-compiled object files (.o)
        clang_args.insert(clang_args.end(), _extra_object_files.begin(), _extra_object_files.end());
        // Append -L/-l flags for used imports
        auto import_args = build_import_link_args();
        clang_args.insert(clang_args.end(), import_args.begin(), import_args.end());
        auto extra_args = build_extra_link_args();
        clang_args.insert(clang_args.end(), extra_args.begin(), extra_args.end());
        // Use clang++ (not clang) so the C++ ABI runtime (exceptions, RTTI)
        // is linked automatically — K exceptions use the Itanium C++ ABI.
        exec_res = tools::lookup_run_process("clang++", clang_args);
    } catch (const tools::tool_not_found& e) {
        error(static_cast<unsigned int>(k::diag::compiler_diag::ERR_TOOL_NOT_FOUND_LINK_EXECUTABLE),
              "{} (needed to link executable)", {e.what()});
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

std::vector<std::string> compiler::build_extra_link_args() const {
    std::vector<std::string> args;
    args.reserve(_extra_library_dirs.size() + _extra_libraries.size());
    for (const auto& dir : _extra_library_dirs) {
        args.push_back("-L" + dir);
    }
    for (const auto& lib : _extra_libraries) {
        // A value that looks like a path (contains a separator) or that names a
        // library file explicitly is forwarded as-is; anything else is a short
        // name and becomes -l<name>.
        std::filesystem::path p(lib);
        const std::string ext = p.extension().string();
        if (lib.find('/') != std::string::npos || ext == ".so" || ext == ".a") {
            args.push_back(lib);
        } else {
            args.push_back("-l" + lib);
        }
    }
    return args;
}

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
    if (_has_compilation_error) return false;
    if (!check_export_symbol_output(false)) return false;
    // --no-emit-kdi: silently skip KDI generation
    if (_ir_output_options.no_emit_kdi) return true;

    if (!_model_unit) {
        error(static_cast<unsigned int>(k::diag::compiler_diag::ERR_GEN_KDI_NO_UNIT),
              "gen_kdi: no compiled unit available");
        return false;
    }
    if (!verify_gen_code()) return false;

    // Derive .kdi path: same stem as lib_path, extension = ".kdi"
    std::filesystem::path kdi_path(lib_path);
    kdi_path.replace_extension(".kdi");

    const std::string ver = "klangc-" + std::string(PROJECT_VER);

    trace("[compiler::gen_kdi] building KDI '{}'", {kdi_path.string()});
    kdi::kdi_file kdi_file;
    try {
        kdi_file = k::model::build_kdi(*_context, *_model_unit, lib_path, ver);
    } catch (const k::log::compiler_error& e) {
        auto diag = e.get_diagnostic();
        diag.add_note("While building KDI '{}' for module '{}'",
                      {kdi_path.string(), _model_unit->get_unit_name().to_string()});
        report(diag);
        throw;
    } catch (const std::exception& e) {
        auto diag = k::log::diagnostic::make_fatal(
            static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F001),
            "KDI export failed for module '{}' while building '{}': {}",
            {_model_unit->get_unit_name().to_string(), kdi_path.string(), e.what()});
        report(diag);
        throw k::log::compiler_error(std::move(diag));
    }

    if (!kdi::kdi_write_cbor_file(kdi_file, kdi_path.string())) {
        error(static_cast<unsigned int>(k::diag::compiler_diag::ERR_GEN_KDI_WRITE_FAILED),
              "gen_kdi: failed to write '{}'", {kdi_path.string()});
        return false;
    }
    trace("[compiler::gen_kdi] generated KDI '{}'", {kdi_path.string()});

    // Optionally also write the JSON equivalent (.kdi.json)
    if (_ir_output_options.emit_kdi_json) {
        std::string json_path = kdi_path.string() + ".json";
        if (!kdi::kdi_write_json_file(kdi_file, json_path)) {
            error(static_cast<unsigned int>(k::diag::compiler_diag::ERR_GEN_KDI_JSON_WRITE_FAILED),
                  "gen_kdi: failed to write JSON '{}'", {json_path});
            return false;
        } else {
            trace("[compiler::gen_kdi] generated KDI JSON '{}'", {json_path});
        }
    }
    return true;
}

bool compiler::gen_shared_library(const std::string& output_file) {
    if (_has_compilation_error) return false;
    if (!check_export_symbol_output(false)) return false;
    std::filesystem::path output_path(
        output_file.empty() ? "lib" + get_lib_base_name() + ".so"
                            : output_file);
    resolve_ir_filenames(output_path.string());

    auto object_path = make_temp_object_path(output_path.stem().string());

    trace("[compiler::gen_shared_library] generating object '{}'", {object_path.string()});
    if (!emit_object_file(object_path, true)) {
        std::filesystem::remove(object_path);
        return false;
    }

    trace("[compiler::gen_shared_library] generating shared library '{}'", {output_path.string()});
    tools::exec_result exec_res;
    try {
        std::vector<std::string> clang_args = {"-shared", "-fPIC", "-o", output_path.string(), object_path.string()};
        if (_debug_info_options.enabled) {
            clang_args.push_back("-g");
        }
        // Append extra pre-compiled object files (.o)
        clang_args.insert(clang_args.end(), _extra_object_files.begin(), _extra_object_files.end());
        auto import_args = build_import_link_args();
        clang_args.insert(clang_args.end(), import_args.begin(), import_args.end());
        auto extra_args = build_extra_link_args();
        clang_args.insert(clang_args.end(), extra_args.begin(), extra_args.end());
        exec_res = tools::lookup_run_process("clang++", clang_args);
    } catch (const tools::tool_not_found& e) {
        error(static_cast<unsigned int>(k::diag::compiler_diag::ERR_TOOL_NOT_FOUND_LINK_SHARED_LIB),
              "{} (needed to link shared library)", {e.what()});
        std::filesystem::remove(object_path);
        return false;
    }

    std::filesystem::remove(object_path);

    if (!exec_res.out.empty()) std::cout << exec_res.out << std::endl;
    if (!exec_res.err.empty()) std::cerr << exec_res.err << std::endl;
    return exec_res.exit_code == 0 && gen_kdi(output_path.string());
}

bool compiler::gen_static_library(const std::string& output_file) {
    if (_has_compilation_error) return false;
    if (!check_export_symbol_output(false)) return false;
    std::filesystem::path output_path(
        output_file.empty() ? "lib" + get_lib_base_name() + ".a"
                            : output_file);
    resolve_ir_filenames(output_path.string());

    auto object_path = make_temp_object_path(output_path.stem().string());

    trace("[compiler::gen_static_library] generating object '{}'", {object_path.string()});
    if (!emit_object_file(object_path, true)) {
        std::filesystem::remove(object_path);
        return false;
    }

    trace("[compiler::gen_static_library] generating static library '{}'", {output_path.string()});
    const bool ok = create_static_archive(diagnostics(), output_path, object_path, _extra_object_files);
    std::filesystem::remove(object_path);
    return ok && gen_kdi(output_path.string());
}

bool compiler::gen_libraries(const std::string& shared_out, const std::string& static_out) {
    if (_has_compilation_error) return false;
    if (!check_export_symbol_output(false)) return false;
    // Determine output paths up front
    const std::string base = get_lib_base_name();
    std::filesystem::path so_path(shared_out.empty() ? "lib" + base + ".so" : shared_out);
    std::filesystem::path  a_path(static_out.empty() ? "lib" + base + ".a"  : static_out);

    resolve_ir_filenames(so_path.string());

    // Generate the object file once
    auto object_path = make_temp_object_path("lib" + base);
    trace("[compiler::gen_libraries] generating object '{}'", {object_path.string()});
    if (!emit_object_file(object_path, true)) {
        std::filesystem::remove(object_path);
        return false;
    }

    bool ok = true;

    // Shared library
    trace("[compiler::gen_libraries] generating shared library '{}'", {so_path.string()});
    tools::exec_result so_res;
    try {
        std::vector<std::string> clang_args = {"-shared", "-fPIC", "-o", so_path.string(), object_path.string()};
        if (_debug_info_options.enabled) {
            clang_args.push_back("-g");
        }
        // Append extra pre-compiled object files (.o)
        clang_args.insert(clang_args.end(), _extra_object_files.begin(), _extra_object_files.end());
        auto import_args = build_import_link_args();
        clang_args.insert(clang_args.end(), import_args.begin(), import_args.end());
        auto extra_args = build_extra_link_args();
        clang_args.insert(clang_args.end(), extra_args.begin(), extra_args.end());
        so_res = tools::lookup_run_process("clang++", clang_args);
    } catch (const tools::tool_not_found& e) {
        error(static_cast<unsigned int>(k::diag::compiler_diag::ERR_TOOL_NOT_FOUND_LINK_SHARED_LIB),
              "{} (needed to link shared library)", {e.what()});
        std::filesystem::remove(object_path);
        return false;
    }
    if (!so_res.out.empty()) std::cout << so_res.out << std::endl;
    if (!so_res.err.empty()) std::cerr << so_res.err << std::endl;
    ok &= (so_res.exit_code == 0);

    // Static library
    trace("[compiler::gen_libraries] generating static library '{}'", {a_path.string()});
    ok &= create_static_archive(diagnostics(), a_path, object_path, _extra_object_files);

    std::filesystem::remove(object_path);
    // Generate KDI once (keyed on the .so path; same content for both libs)
    return ok && gen_kdi(so_path.string());
}

} // k
