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
#include <dlfcn.h>
#include <cstdint>
#include <set>
#include <regex>
#include <cerrno>
#include <cstdlib>
#include <system_error>
#include <sys/un.h>
#include <fcntl.h>
#include <llvm/ADT/ScopeExit.h>

#include <fmt/core.h>
#include <fmt/format.h>
#include <fmt/args.h>

#include <catch2/catch_all.hpp>
#include <llvm/IR/Constants.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/TargetParser/Host.h>

#include "../src/parse/ast_dump.hpp"
#include "../src/common/llvm_target_triple.hpp"
#include "../src/model/model_builder.hpp"
#include "../src/model/model_dump.hpp"

namespace {
struct ReservedOutput {
    std::filesystem::path path;
    std::filesystem::path kdi_path;
    bool released = false;

    explicit ReservedOutput(const std::string& suffix) {
        for (;;) {
            std::string name = (std::filesystem::temp_directory_path() /
                                ("kt-XXXXXX" + suffix)).string();
            int fd = ::mkstemps(name.data(), static_cast<int>(suffix.size()));
            if (fd < 0) throw std::system_error(errno, std::generic_category(), "reserve test output");
            bool open = true;
            auto rollback = llvm::make_scope_exit([&] {
                if (open) ::close(fd);
                ::unlink(name.c_str());
            });
            path = name;
            if (::close(fd) != 0) {
                const int error = errno;
                open = false;
                throw std::system_error(error, std::generic_category(), "close test output");
            }
            open = false;
            if (suffix == ".kdi") {
                rollback.release();
                break;
            }
            kdi_path = path;
            kdi_path.replace_extension(".kdi");
            fd = ::open(kdi_path.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
            if (fd < 0) {
                const int error = errno;
                if (error == EEXIST) continue;
                throw std::system_error(error, std::generic_category(), "reserve test KDI");
            }
            if (::close(fd) != 0) {
                const int error = errno;
                std::error_code ec;
                std::filesystem::remove(kdi_path, ec);
                throw std::system_error(error, std::generic_category(), "close test KDI");
            }
            rollback.release();
            break;
        }
    }
    ~ReservedOutput() {
        if (!released) {
            std::error_code ec;
            std::filesystem::remove(path, ec);
            if (!kdi_path.empty()) std::filesystem::remove(kdi_path, ec);
        }
    }
    void release() noexcept {
        std::error_code ec;
        if (!kdi_path.empty() && std::filesystem::file_size(kdi_path, ec) == 0 && !ec)
            std::filesystem::remove(kdi_path, ec);
        released = true;
    }
};

void link_test_file(const std::filesystem::path& source,
                    const std::filesystem::path& destination) {
    std::error_code ec;
    std::filesystem::create_symlink(source.filename(), destination, ec);
    if (ec) {
        std::filesystem::copy_file(source, destination,
                                   std::filesystem::copy_options::none, ec);
        if (ec) throw std::system_error(ec, "create test library link");
    }
}
}

/**
 * Ensure that libk.so is loaded into the current process so that the JIT's
 * DynamicLibrarySearchGenerator can resolve symbols defined in libk (in
 * particular the fatal runtime helpers).
 *
 * Uses the KLANG_STDLIB_LIB_DIR macro set by CMake; does nothing if the
 * library has already been loaded.  Returns true on success.
 */
bool ensure_libk_loaded() {
    static void* libk_handle = nullptr;
    if (libk_handle)
        return true;
    std::string libk_path = std::string(KLANG_STDLIB_LIB_DIR) + "/libk.so";
    libk_handle = dlopen(libk_path.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!libk_handle) {
        std::cerr << "ensure_libk_loaded: cannot load " << libk_path
                  << ": " << dlerror() << std::endl;
        return false;
    }
    return true;
}

bool bind_jit_fixture(llvm::Function* declaration, void* address) {
    if (!declaration || !declaration->isDeclaration() || !address) return false;
    // ORC's process-symbol generator cannot see LLVM DynamicLibrary::AddSymbol entries.
    auto& context = declaration->getContext();
    auto& module = *declaration->getParent();
    auto* entry = llvm::BasicBlock::Create(context, "entry", declaration);
    llvm::IRBuilder<> builder(entry);
    auto* integer = llvm::ConstantInt::get(module.getDataLayout().getIntPtrType(context),
                                           reinterpret_cast<std::uintptr_t>(address));
    auto* target = llvm::ConstantExpr::getIntToPtr(integer, llvm::PointerType::get(context, 0));
    std::vector<llvm::Value*> args;
    for (auto& arg : declaration->args()) args.push_back(&arg);
    auto* call = builder.CreateCall(declaration->getFunctionType(), target, args);
    call->setCallingConv(declaration->getCallingConv());
    call->setAttributes(declaration->getAttributes());
    if (declaration->getReturnType()->isVoidTy()) builder.CreateRetVoid();
    else builder.CreateRet(call);
    return true;
}

/**
 * Create a file resolver that knows where the K standard library lives.
 * This allows the compiler to resolve the implicit `import k` and to add
 * -L<dir> for libk when linking executables and shared libraries.
 */
static std::shared_ptr<k::path_lookup_file_resolver> make_stdlib_resolver() {
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
    return resolver;
}

/**
 * Apply a per-test list of diagnostic codes to silence (see
 * k::compiler::set_ignored_diagnostic_codes). No-op when the list is empty.
 * Must be called right after k::compiler::create(), before parsing.
 */
static void apply_ignored_diag_codes(k::compiler& comp, const IgnoredDiagCodes& ignored_diag_codes) {
    if (!ignored_diag_codes.empty()) {
        comp.set_ignored_diagnostic_codes(ignored_diag_codes);
    }
}

/**
 * Diagnostic path for a compiled fragment: honours an explicit @p file_name
 * override when given, otherwise auto-derives one from the fragment's own
 * `module <name>;` declaration (e.g. `module foo;` -> "foo.k",
 * `module math::utils;` -> "math.utils.k"). Falls back to @p fallback when
 * the source declares no module. This path is purely a diagnostic label
 * (see compiler::parse_sources()); it has no effect on compilation.
 */
static std::string derive_source_path(std::string_view src, const std::string& file_name,
                                       const std::string& fallback = "") {
    if (!file_name.empty()) return file_name;
    static const std::regex module_re(
        R"(\bmodule\s+([A-Za-z_][A-Za-z0-9_]*(?:::[A-Za-z_][A-Za-z0-9_]*)*)\s*;)");
    std::cmatch m;
    if (std::regex_search(src.data(), src.data() + src.size(), m, module_re)) {
        std::string name = m[1].str();
        for (auto& c : name) if (c == ':') c = '.';
        std::string cleaned;
        cleaned.reserve(name.size());
        for (std::size_t i = 0; i < name.size(); ++i) {
            if (name[i] == '.' && i + 1 < name.size() && name[i + 1] == '.') continue;
            cleaned += name[i];
        }
        return cleaned + ".k";
    }
    return fallback;
}

std::unique_ptr<k::model::gen::jit> gen_jit(std::string_view src, bool dump, bool optimize,
                                             const IgnoredDiagCodes& ignored_diag_codes,
                                             const std::string& file_name) {
    if (!ensure_libk_loaded()) return nullptr;
    auto comp = k::compiler::create();
    comp->set_file_resolver(make_stdlib_resolver());
    apply_ignored_diag_codes(*comp, ignored_diag_codes);
    try {
        comp->parse_source(derive_source_path(src, file_name), src, optimize, dump);
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

std::unique_ptr<k::model::gen::jit> gen_jit_throws(std::string_view src, bool dump, bool optimize,
                                                    const IgnoredDiagCodes& ignored_diag_codes,
                                                    const std::string& file_name) {
    // parse_source always rethrows — compiler_error subclasses propagate directly to the caller.
    if (!ensure_libk_loaded()) return nullptr;
    auto comp = k::compiler::create();
    comp->set_file_resolver(make_stdlib_resolver());
    apply_ignored_diag_codes(*comp, ignored_diag_codes);
    comp->parse_source(derive_source_path(src, file_name), src, optimize, dump);
    return comp->to_jit();
}

std::unique_ptr<k::model::gen::jit> gen_jit_with_stdlib(
    std::string_view src,
    const std::string& stdlib_kdi_dir,
    const std::string& stdlib_lib_dir,
    bool dump, bool optimize,
    const IgnoredDiagCodes& ignored_diag_codes,
    const std::string& file_name)
{
    if (!ensure_libk_loaded()) return nullptr;

    auto comp = k::compiler::create();
    // Configure the file resolver so `import k;` finds k.kdi.
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_search_dir(stdlib_kdi_dir);
    comp->set_file_resolver(resolver);
    apply_ignored_diag_codes(*comp, ignored_diag_codes);

    try {
        comp->parse_source(derive_source_path(src, file_name), src, optimize, dump);
        return comp->to_jit();
    } catch (const k::log::compiler_error&) {
        return nullptr;
    } catch (std::exception& ex) {
        std::cerr << "Unexpected error during compilation: " << ex.what() << std::endl;
        return nullptr;
    }
}

std::unique_ptr<k::model::gen::jit> gen_jit_multi(
    std::vector<std::pair<std::string, std::string>> sources,
    bool dump, bool optimize,
    const std::string& forced_module_name,
    const IgnoredDiagCodes& ignored_diag_codes) {
    if (!ensure_libk_loaded()) return nullptr;
    auto comp = k::compiler::create();
    comp->set_file_resolver(make_stdlib_resolver());
    apply_ignored_diag_codes(*comp, ignored_diag_codes);
    try {
        comp->parse_sources(std::move(sources), optimize, dump, forced_module_name);
        return comp->to_jit();
    } catch (const k::log::compiler_error&) {
        return nullptr;
    } catch (std::exception& ex) {
        std::cerr << "Unexpected error during multi-source compilation: " << ex.what() << std::endl;
        return nullptr;
    }
}

std::unique_ptr<k::model::gen::jit> gen_jit_multi_throws(
    std::vector<std::pair<std::string, std::string>> sources,
    bool dump, bool optimize,
    const std::string& forced_module_name,
    const IgnoredDiagCodes& ignored_diag_codes) {
    if (!ensure_libk_loaded()) return nullptr;
    auto comp = k::compiler::create();
    comp->set_file_resolver(make_stdlib_resolver());
    apply_ignored_diag_codes(*comp, ignored_diag_codes);
    comp->parse_sources(std::move(sources), optimize, dump, forced_module_name);
    return comp->to_jit();
}



bool compile_text(const std::string_view& source, const std::string& out_file,
                   const IgnoredDiagCodes& ignored_diag_codes = {},
                   const std::string& file_name = "") {
    // Ensure LLVM targets are registered before any target lookup.
    k::compiler::initialize();

    llvm::Triple target_triple{llvm::sys::getDefaultTargetTriple()};
    std::string error;
    auto target = llvm::TargetRegistry::lookupTarget(k::llvm_target_triple_arg(target_triple), error);
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
            k::llvm_target_triple_arg(target_triple), cpu,
            features,
            target_options,
            reloc_model);

    auto compiler = k::compiler::create(target_machine);
    compiler->set_file_resolver(make_stdlib_resolver());
    apply_ignored_diag_codes(*compiler, ignored_diag_codes);
    try {
        compiler->parse_source(derive_source_path(source, file_name), source, true, false);
    } catch (const k::log::compiler_error&) {
        return false;
    }
    return compiler->gen_executable(out_file);
}

k::tools::exec_result build_and_exec(const std::string_view& src, const IgnoredDiagCodes& ignored_diag_codes,
                                      const std::string& file_name) {
    TmpDir workspace;
    const auto out_file = (workspace.path / "program").string();

    if (!compile_text(src, out_file, ignored_diag_codes, file_name)) {
        std::filesystem::remove(out_file);
        throw std::runtime_error("Error building source code");
    }

    auto res = k::tools::run_process(out_file, {});
    std::filesystem::remove(out_file);
    return res;
}

// ---------------------------------------------------------------------------
// make_pic_target_machine (declared in helpers.hpp)
// ---------------------------------------------------------------------------
llvm::TargetMachine* make_pic_target_machine() {
    k::compiler::initialize();

    llvm::Triple target_triple{llvm::sys::getDefaultTargetTriple()};
    std::string error;
    auto target = llvm::TargetRegistry::lookupTarget(k::llvm_target_triple_arg(target_triple), error);
    if (!target) {
        throw std::runtime_error("Could not find LLVM target: " + error);
    }

    std::optional<llvm::Reloc::Model> reloc_model = llvm::Reloc::PIC_;
    return target->createTargetMachine(k::llvm_target_triple_arg(target_triple), "generic", "", {}, reloc_model);
}

// ---------------------------------------------------------------------------

std::string build_shared_library(const std::string_view& src, const IgnoredDiagCodes& ignored_diag_codes,
                                  const std::string& file_name, bool with_stdlib) {
    ReservedOutput output(".so");
    std::string out_file = output.path.string();

    auto compiler = k::compiler::create(make_pic_target_machine());
    apply_ignored_diag_codes(*compiler, ignored_diag_codes);
    if (with_stdlib) compiler->set_file_resolver(make_stdlib_resolver());
    try {
        compiler->parse_source(derive_source_path(src, file_name), src, true, false);
    } catch (const k::log::compiler_error&) {
        throw std::runtime_error("Compilation error while building shared library");
    }

    if (!compiler->gen_shared_library(out_file)) {
        std::filesystem::remove(out_file);
        throw std::runtime_error("Error generating shared library");
    }

    output.release();
    return out_file;
}

std::string build_static_library(const std::string_view& src, const IgnoredDiagCodes& ignored_diag_codes,
                                  const std::string& file_name) {
    ReservedOutput output(".a");
    std::string out_file = output.path.string();

    auto compiler = k::compiler::create(make_pic_target_machine());
    apply_ignored_diag_codes(*compiler, ignored_diag_codes);
    try {
        compiler->parse_source(derive_source_path(src, file_name), src, true, false);
    } catch (const k::log::compiler_error&) {
        throw std::runtime_error("Compilation error while building static library");
    }

    if (!compiler->gen_static_library(out_file)) {
        std::filesystem::remove(out_file);
        throw std::runtime_error("Error generating static library");
    }

    output.release();
    return out_file;
}

std::pair<std::string, std::string> build_both_libraries(const std::string_view& src,
                                                          const IgnoredDiagCodes& ignored_diag_codes,
                                                          const std::string& file_name) {
    ReservedOutput so_output(".so");
    ReservedOutput a_output(".a");
    std::string so_file = so_output.path.string();
    std::string a_file = a_output.path.string();

    auto compiler = k::compiler::create(make_pic_target_machine());
    apply_ignored_diag_codes(*compiler, ignored_diag_codes);
    try {
        compiler->parse_source(derive_source_path(src, file_name), src, true, false);
    } catch (const k::log::compiler_error&) {
        throw std::runtime_error("Compilation error while building libraries");
    }

    if (!compiler->gen_libraries(so_file, a_file)) {
        std::filesystem::remove(so_file);
        std::filesystem::remove(a_file);
        throw std::runtime_error("Error generating libraries");
    }

    so_output.release();
    a_output.release();
    return {std::move(so_file), std::move(a_file)};
}

// ---------------------------------------------------------------------------
// build_exec_with_libs  (multiple libraries)
// ---------------------------------------------------------------------------

k::tools::exec_result build_exec_with_libs(std::vector<LibSpec>& libs,
                                            const std::string_view& exec_src,
                                            const IgnoredDiagCodes& ignored_diag_codes,
                                            const std::string& exec_file_name)
{
    TmpDir workspace;
    for (auto& spec : libs) {
        spec.so_path.clear();
        spec.kdi_path.clear();
        spec.symlink_path.clear();
    }
    // ── Step 1: compile each library ──────────────────────────────────────
    for (std::size_t i = 0; i < libs.size(); ++i) {
        auto& spec = libs[i];
        spec.so_path = (workspace.path / ("provider-" + std::to_string(i) + ".so")).string();

        auto lib_comp = k::compiler::create(make_pic_target_machine());
        apply_ignored_diag_codes(*lib_comp, ignored_diag_codes);

        // Give this library access to the KDIs of all previously-compiled libs
        // (it may import types/functions from earlier libs in the list)
        {
            auto resolver = std::make_shared<k::path_lookup_file_resolver>();
            resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
            for (std::size_t j = 0; j < i; ++j) {
                if (!libs[j].kdi_path.empty()) {
                    kdi::kdi_file prev_kdi = kdi::kdi_read_cbor_file(libs[j].kdi_path);
                    resolver->add_explicit_path(prev_kdi.header.module_name, libs[j].kdi_path);
                    resolver->add_search_dir(
                        std::filesystem::path(libs[j].so_path).parent_path().string());
                }
            }
            lib_comp->set_file_resolver(resolver);
        }

        try {
            lib_comp->parse_source(derive_source_path(spec.src, spec.file_name, "lib.k"), spec.src, true, false);
        } catch (const k::log::compiler_error& e) {
            // Clean up already-built libs
            for (auto& s : libs) {
                if (!s.so_path.empty())      std::filesystem::remove(s.so_path);
                if (!s.kdi_path.empty())     std::filesystem::remove(s.kdi_path);
                if (!s.symlink_path.empty()) std::filesystem::remove(s.symlink_path);
            }
            throw std::runtime_error(std::string("Library compilation error: ") + e.what());
        }
        if (!lib_comp->gen_shared_library(spec.so_path)) {
            for (auto& s : libs) {
                if (!s.so_path.empty())      std::filesystem::remove(s.so_path);
                if (!s.kdi_path.empty())     std::filesystem::remove(s.kdi_path);
                if (!s.symlink_path.empty()) std::filesystem::remove(s.symlink_path);
            }
            throw std::runtime_error("Library link failed");
        }

        std::filesystem::path so_path(spec.so_path);
        std::filesystem::path kdi_path_fs = so_path;
        kdi_path_fs.replace_extension(".kdi");
        if (!std::filesystem::is_regular_file(kdi_path_fs)) {
            for (auto& s : libs) {
                if (!s.so_path.empty())      std::filesystem::remove(s.so_path);
                if (!s.kdi_path.empty())     std::filesystem::remove(s.kdi_path);
                if (!s.symlink_path.empty()) std::filesystem::remove(s.symlink_path);
            }
            throw std::runtime_error("Library KDI not produced: " + kdi_path_fs.string());
        }
        spec.kdi_path = kdi_path_fs.string();

        // Create symlink lib<base>.so so the linker can find -l<base>
        kdi::kdi_file kdi_data = kdi::kdi_read_cbor_file(spec.kdi_path);
        const std::string lib_base = k::compiler::unit_name_to_lib_base(kdi_data.header.module_name);
        if (!lib_base.empty()) {
            std::filesystem::path sl = so_path.parent_path() / ("lib" + lib_base + ".so");
            link_test_file(so_path, sl);
            spec.symlink_path = sl.string();
        }
    }

    // ── Step 2: compile the executable ────────────────────────────────────
    std::string exe_file = (workspace.path / "program").string();

    // Build resolver with all KDIs and all library search dirs
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
    for (auto& spec : libs) {
        kdi::kdi_file kdi_data = kdi::kdi_read_cbor_file(spec.kdi_path);
        resolver->add_explicit_path(kdi_data.header.module_name, spec.kdi_path);
        resolver->add_search_dir(std::filesystem::path(spec.so_path).parent_path().string());
    }

    auto exe_comp = k::compiler::create(make_pic_target_machine());
    apply_ignored_diag_codes(*exe_comp, ignored_diag_codes);
    exe_comp->set_file_resolver(resolver);
    try {
        exe_comp->parse_source(derive_source_path(exec_src, exec_file_name, "main.k"), exec_src, true, false);
    } catch (const k::log::compiler_error& e) {
        for (auto& s : libs) {
            if (!s.so_path.empty())      std::filesystem::remove(s.so_path);
            if (!s.kdi_path.empty())     std::filesystem::remove(s.kdi_path);
            if (!s.symlink_path.empty()) std::filesystem::remove(s.symlink_path);
        }
        std::filesystem::remove(exe_file);
        throw std::runtime_error(std::string("Executable compilation error: ") + e.what());
    }
    if (!exe_comp->gen_executable(exe_file)) {
        for (auto& s : libs) {
            if (!s.so_path.empty())      std::filesystem::remove(s.so_path);
            if (!s.kdi_path.empty())     std::filesystem::remove(s.kdi_path);
            if (!s.symlink_path.empty()) std::filesystem::remove(s.symlink_path);
        }
        std::filesystem::remove(exe_file);
        throw std::runtime_error("Executable link failed");
    }

    // ── Step 3: run with LD_LIBRARY_PATH covering all lib dirs ────────────
    // Collect unique library directories
    std::string ld_dirs;
    std::set<std::string> seen_dirs;
    for (auto& spec : libs) {
        std::string dir = std::filesystem::path(spec.so_path).parent_path().string();
        if (seen_dirs.insert(dir).second) {
            if (!ld_dirs.empty()) ld_dirs += ":";
            ld_dirs += dir;
        }
    }

    ScopedLdLibraryPath ld_scope(ld_dirs);

    k::tools::exec_result result;
    try {
        result = k::tools::run_process(exe_file, {});
    } catch (...) {
        for (auto& s : libs) {
            if (!s.so_path.empty())      std::filesystem::remove(s.so_path);
            if (!s.kdi_path.empty())     std::filesystem::remove(s.kdi_path);
            if (!s.symlink_path.empty()) std::filesystem::remove(s.symlink_path);
        }
        std::filesystem::remove(exe_file);
        throw;
    }

    // ── Cleanup ────────────────────────────────────────────────────────────
    for (auto& s : libs) {
        if (!s.so_path.empty())      std::filesystem::remove(s.so_path);
        if (!s.kdi_path.empty())     std::filesystem::remove(s.kdi_path);
        if (!s.symlink_path.empty()) std::filesystem::remove(s.symlink_path);
    }
    std::filesystem::remove(exe_file);

    return result;
}

// ---------------------------------------------------------------------------
// build_exec_with_libs_direct_only
// ---------------------------------------------------------------------------
// Like build_exec_with_libs but for the executable step only registers the
// KDIs of the explicitly-listed direct imports.  All other libs are reachable
// solely via the search directories, simulating real CLI transitive resolution.

k::tools::exec_result build_exec_with_libs_direct_only(
    std::vector<LibSpec>& libs,
    const std::string_view& exec_src,
    const std::vector<std::string>& direct_imports,
    const IgnoredDiagCodes& ignored_diag_codes,
    const std::string& exec_file_name)
{
    TmpDir workspace;
    for (auto& spec : libs) {
        spec.so_path.clear();
        spec.kdi_path.clear();
        spec.symlink_path.clear();
    }
    // ── Step 1: compile all libraries (same as build_exec_with_libs) ──────
    for (std::size_t i = 0; i < libs.size(); ++i) {
        auto& spec = libs[i];
        spec.so_path = (workspace.path / ("provider-" + std::to_string(i) + ".so")).string();

        auto lib_comp = k::compiler::create(make_pic_target_machine());
        apply_ignored_diag_codes(*lib_comp, ignored_diag_codes);
        if (i > 0) {
            auto resolver = std::make_shared<k::path_lookup_file_resolver>();
            for (std::size_t j = 0; j < i; ++j) {
                if (!libs[j].kdi_path.empty()) {
                    kdi::kdi_file prev_kdi = kdi::kdi_read_cbor_file(libs[j].kdi_path);
                    resolver->add_explicit_path(prev_kdi.header.module_name, libs[j].kdi_path);
                    resolver->add_search_dir(
                        std::filesystem::path(libs[j].so_path).parent_path().string());
                }
            }
            lib_comp->set_file_resolver(resolver);
        }

        try {
            lib_comp->parse_source(derive_source_path(spec.src, spec.file_name, "lib.k"), spec.src, true, false);
        } catch (const k::log::compiler_error& e) {
            for (auto& s : libs) {
                if (!s.so_path.empty())      std::filesystem::remove(s.so_path);
                if (!s.kdi_path.empty())     std::filesystem::remove(s.kdi_path);
                if (!s.symlink_path.empty()) std::filesystem::remove(s.symlink_path);
            }
            throw std::runtime_error(std::string("Library compilation error: ") + e.what());
        }
        if (!lib_comp->gen_shared_library(spec.so_path)) {
            for (auto& s : libs) {
                if (!s.so_path.empty())      std::filesystem::remove(s.so_path);
                if (!s.kdi_path.empty())     std::filesystem::remove(s.kdi_path);
                if (!s.symlink_path.empty()) std::filesystem::remove(s.symlink_path);
            }
            throw std::runtime_error("Library link failed");
        }

        std::filesystem::path so_path(spec.so_path);
        std::filesystem::path kdi_path_fs = so_path;
        kdi_path_fs.replace_extension(".kdi");
        if (!std::filesystem::is_regular_file(kdi_path_fs)) {
            for (auto& s : libs) {
                if (!s.so_path.empty())      std::filesystem::remove(s.so_path);
                if (!s.kdi_path.empty())     std::filesystem::remove(s.kdi_path);
                if (!s.symlink_path.empty()) std::filesystem::remove(s.symlink_path);
            }
            throw std::runtime_error("Library KDI not produced: " + kdi_path_fs.string());
        }
        spec.kdi_path = kdi_path_fs.string();

        kdi::kdi_file kdi_data = kdi::kdi_read_cbor_file(spec.kdi_path);
        const std::string lib_base = k::compiler::unit_name_to_lib_base(kdi_data.header.module_name);
        if (!lib_base.empty()) {
            std::filesystem::path sl = so_path.parent_path() / ("lib" + lib_base + ".so");
            link_test_file(so_path, sl);
            spec.symlink_path = sl.string();
        }
    }

    // ── Step 2: compile the executable ────────────────────────────────────
    // Key difference: only register explicit_path for modules in direct_imports;
    // all others are reachable via search_dir only.
    std::string exe_file = (workspace.path / "program").string();

    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    for (auto& spec : libs) {
        kdi::kdi_file kdi_data = kdi::kdi_read_cbor_file(spec.kdi_path);
        const std::string mod_name = kdi_data.header.module_name;
        const std::string lib_base = k::compiler::unit_name_to_lib_base(mod_name);
        // Always add the search dir so the .so can be linked and transitives found
        resolver->add_search_dir(std::filesystem::path(spec.so_path).parent_path().string());
        // Only register explicit KDI path for direct imports
        bool is_direct = std::find(direct_imports.begin(), direct_imports.end(), mod_name)
                         != direct_imports.end();
        if (is_direct) {
            resolver->add_explicit_path(mod_name, spec.kdi_path);
        } else {
            // For transitive libs: create a canonical-name symlink (<base>.kdi)
            // in the same directory so that path_lookup_file_resolver can find
            // it by module name via the search_dir.
            if (!lib_base.empty()) {
                std::filesystem::path kdi_fs(spec.kdi_path);
                std::filesystem::path kdi_symlink = kdi_fs.parent_path() / (lib_base + ".kdi");
                link_test_file(kdi_fs, kdi_symlink);
            }
        }
    }

    auto exe_comp = k::compiler::create(make_pic_target_machine());
    apply_ignored_diag_codes(*exe_comp, ignored_diag_codes);
    exe_comp->set_file_resolver(resolver);
    try {
        exe_comp->parse_source(derive_source_path(exec_src, exec_file_name, "main.k"), exec_src, true, false);
    } catch (const k::log::compiler_error& e) {
        for (auto& s : libs) {
            if (!s.so_path.empty())      std::filesystem::remove(s.so_path);
            if (!s.kdi_path.empty())     std::filesystem::remove(s.kdi_path);
            if (!s.symlink_path.empty()) std::filesystem::remove(s.symlink_path);
        }
        std::filesystem::remove(exe_file);
        throw std::runtime_error(std::string("Executable compilation error: ") + e.what());
    }
    if (!exe_comp->gen_executable(exe_file)) {
        for (auto& s : libs) {
            if (!s.so_path.empty())      std::filesystem::remove(s.so_path);
            if (!s.kdi_path.empty())     std::filesystem::remove(s.kdi_path);
            if (!s.symlink_path.empty()) std::filesystem::remove(s.symlink_path);
        }
        std::filesystem::remove(exe_file);
        throw std::runtime_error("Executable link failed");
    }

    // ── Step 3: run ────────────────────────────────────────────────────────
    // Collect all lib dirs for LD_LIBRARY_PATH
    std::string ld_dirs;
    for (auto& spec : libs) {
        if (!spec.so_path.empty()) {
            std::string dir = std::filesystem::path(spec.so_path).parent_path().string();
            if (!ld_dirs.empty()) ld_dirs += ':';
            ld_dirs += dir;
        }
    }
    ScopedLdLibraryPath ld_scope(ld_dirs);

    k::tools::exec_result result;
    try {
        result = k::tools::run_process(exe_file, {});
    } catch (...) {
        for (auto& s : libs) {
            if (!s.so_path.empty())      std::filesystem::remove(s.so_path);
            if (!s.kdi_path.empty())     std::filesystem::remove(s.kdi_path);
            if (!s.symlink_path.empty()) std::filesystem::remove(s.symlink_path);
        }
        std::filesystem::remove(exe_file);
        throw;
    }

    // Collect .kdi symlink paths before removing files
    std::vector<std::filesystem::path> kdi_symlinks;
    for (auto& s : libs) {
        if (!s.kdi_path.empty()) {
            std::error_code ec;
            kdi::kdi_file kd = kdi::kdi_read_cbor_file(s.kdi_path);
            const std::string lb = k::compiler::unit_name_to_lib_base(kd.header.module_name);
            if (!lb.empty()) {
                kdi_symlinks.push_back(
                    std::filesystem::path(s.kdi_path).parent_path() / (lb + ".kdi"));
            }
        }
    }

    for (auto& s : libs) {
        if (!s.so_path.empty())      std::filesystem::remove(s.so_path);
        if (!s.kdi_path.empty())     std::filesystem::remove(s.kdi_path);
        if (!s.symlink_path.empty()) std::filesystem::remove(s.symlink_path);
    }
    for (const auto& ksl : kdi_symlinks) {
        std::error_code ec;
        std::filesystem::remove(ksl, ec);
    }
    std::filesystem::remove(exe_file);
    return result;
}

// ---------------------------------------------------------------------------
// compile_should_fail
// ---------------------------------------------------------------------------

bool compile_should_fail(const std::string_view& src,
                         std::shared_ptr<k::path_lookup_file_resolver> resolver,
                         const IgnoredDiagCodes& ignored_diag_codes,
                         const std::string& file_name)
{
    try {
        auto comp = k::compiler::create(make_pic_target_machine());
        apply_ignored_diag_codes(*comp, ignored_diag_codes);
        if (resolver) comp->set_file_resolver(resolver);
        comp->parse_source(derive_source_path(src, file_name, "test.k"), src, true, false);
        return false; // No exception thrown — compilation succeeded unexpectedly
    } catch (const k::log::compiler_error&) {
        return true;  // Expected failure
    }
}

// ---------------------------------------------------------------------------
// compile_collect_diagnostics
// ---------------------------------------------------------------------------

bool compile_collect_diagnostics(
    const std::string_view& src,
    std::shared_ptr<k::path_lookup_file_resolver> resolver,
    test_logger& out_logger,
    const IgnoredDiagCodes& ignored_diag_codes,
    const std::string& file_name)
{
    // We cannot inject a logger into k::compiler directly (it IS a logger and
    // prints to stderr).  Instead we compile normally and capture the warnings
    // via the kdi_importer's check_unused_imports().  Because compiler::report()
    // already prints to stderr, we do a full compile here and rely on the fact
    // that the test_logger is passed to kdi_importer directly in the unit tests
    // that call kdi_importer themselves.
    //
    // For end-to-end tests we do a full compile and check whether it succeeds
    // (no compiler_error thrown).  The out_logger.diagnostics will always be
    // empty in this path — use gen_jit_throws / compile_should_fail for
    // error-path tests.
    //
    // For warning-level tests (unused import), use the kdi_importer directly
    // (see test_kdi_importer_unused_import_* tests in test-import.cpp).
    try {
        auto comp = k::compiler::create(make_pic_target_machine());
        apply_ignored_diag_codes(*comp, ignored_diag_codes);
        if (resolver) comp->set_file_resolver(resolver);
        comp->parse_source(derive_source_path(src, file_name, "test.k"), src, true, false);
        return true;
    } catch (const k::log::compiler_error& e) {
        out_logger.report(e.get_diagnostic());
        return false;
    }
}

// ---------------------------------------------------------------------------
// build_exec_with_lib
// ---------------------------------------------------------------------------

k::tools::exec_result build_exec_with_lib(const std::string_view& lib_src,
                                           const std::string_view& exec_src,
                                           const IgnoredDiagCodes& ignored_diag_codes,
                                           const std::string& lib_file_name,
                                           const std::string& exec_file_name)
{
    TmpDir workspace;
    // ── Step 1: compile the library ────────────────────────────────────────
    std::string so_file = (workspace.path / "provider.so").string();

    // Compiler for the library
    auto lib_comp = k::compiler::create(make_pic_target_machine());
    apply_ignored_diag_codes(*lib_comp, ignored_diag_codes);
    lib_comp->set_file_resolver(make_stdlib_resolver());
    try {
        lib_comp->parse_source(derive_source_path(lib_src, lib_file_name, "lib.k"), lib_src, true, false);
    } catch (const k::log::compiler_error& e) {
        throw std::runtime_error(std::string("Library compilation error: ") + e.what());
    }
    if (!lib_comp->gen_shared_library(so_file)) {
        std::filesystem::remove(so_file);
        throw std::runtime_error("Library link failed");
    }

    // Derive the .kdi path (same stem as .so)
    std::filesystem::path so_path(so_file);
    std::filesystem::path kdi_path = so_path;
    kdi_path.replace_extension(".kdi");
    if (!std::filesystem::is_regular_file(kdi_path)) {
        std::filesystem::remove(so_file);
        throw std::runtime_error("Library KDI not produced: " + kdi_path.string());
    }

    const std::string lib_dir = so_path.parent_path().string();
    // Derive the module name from the KDI header
    kdi::kdi_file kdi_data = kdi::kdi_read_cbor_file(kdi_path.string());
    const std::string module_name = kdi_data.header.module_name;

    // Create a symlink "lib<base>.so" → so_file so that "-lmylib" can find it.
    // Without this, the linker looks for "libmylib.so" but the file has a
    // private provider filename.
    const std::string lib_base = k::compiler::unit_name_to_lib_base(module_name);
    std::filesystem::path symlink_path;
    if (!lib_base.empty()) {
        symlink_path = so_path.parent_path() / ("lib" + lib_base + ".so");
        link_test_file(so_path, symlink_path);
    }

    // ── Step 2: compile the executable ────────────────────────────────────
    std::string exe_file = (workspace.path / "program").string();

    // Build a resolver: register the lib's KDI by module name, and add the
    // lib dir as a library search dir (→ -L flag) for the linker.
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
    resolver->add_explicit_path(module_name, kdi_path.string());
    resolver->add_search_dir(lib_dir);

    auto exe_comp = k::compiler::create(make_pic_target_machine());
    apply_ignored_diag_codes(*exe_comp, ignored_diag_codes);
    exe_comp->set_file_resolver(resolver);
    try {
        exe_comp->parse_source(derive_source_path(exec_src, exec_file_name, "main.k"), exec_src, true, false);
    } catch (const k::log::compiler_error& e) {
        std::filesystem::remove(so_file);
        std::filesystem::remove(kdi_path);
        std::filesystem::remove(exe_file);
        if (!symlink_path.empty()) std::filesystem::remove(symlink_path);
        throw std::runtime_error(std::string("Executable compilation error: ") + e.what());
    }
    if (!exe_comp->gen_executable(exe_file)) {
        std::filesystem::remove(so_file);
        std::filesystem::remove(kdi_path);
        std::filesystem::remove(exe_file);
        if (!symlink_path.empty()) std::filesystem::remove(symlink_path);
        throw std::runtime_error("Executable link failed");
    }

    // ── Step 3: run the executable with LD_LIBRARY_PATH set ───────────────
    // Save the old value and prepend the lib dir.
    ScopedLdLibraryPath ld_scope(lib_dir);

    k::tools::exec_result result;
    try {
        result = k::tools::run_process(exe_file, {});
    } catch (...) {
        std::filesystem::remove(so_file);
        std::filesystem::remove(kdi_path);
        std::filesystem::remove(exe_file);
        if (!symlink_path.empty()) std::filesystem::remove(symlink_path);
        throw;
    }

    // ── Cleanup ────────────────────────────────────────────────────────────
    std::filesystem::remove(so_file);
    std::filesystem::remove(kdi_path);
    std::filesystem::remove(exe_file);
    if (!symlink_path.empty()) std::filesystem::remove(symlink_path);

    return result;
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

// ═══════════════════════════════════════════════════════════════════════════
// Model inspection helpers
// ═══════════════════════════════════════════════════════════════════════════

std::shared_ptr<k::compiler> compile_model(std::string_view src, const IgnoredDiagCodes& ignored_diag_codes,
                                            const std::string& file_name) {
    auto comp = k::compiler::create();
    apply_ignored_diag_codes(*comp, ignored_diag_codes);
    try {
        comp->parse_source(derive_source_path(src, file_name), src, /*optimize=*/false, /*dump=*/false);
        return comp;
    } catch (const k::log::compiler_error&) {
        return nullptr;
    } catch (const std::exception& ex) {
        std::cerr << "Unexpected error: " << ex.what() << std::endl;
        return nullptr;
    }
}

std::shared_ptr<k::compiler> compile_model_with_stdlib(std::string_view src,
                                                        const IgnoredDiagCodes& ignored_diag_codes,
                                                        const std::string& file_name) {
    if (!ensure_libk_loaded()) return nullptr;
    auto comp = k::compiler::create();
    apply_ignored_diag_codes(*comp, ignored_diag_codes);
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
    comp->set_file_resolver(resolver);
    try {
        comp->parse_source(derive_source_path(src, file_name), src, /*optimize=*/false, /*dump=*/false);
        return comp;
    } catch (const k::log::compiler_error&) {
        return nullptr;
    } catch (const std::exception& ex) {
        std::cerr << "Unexpected error (compile_model_with_stdlib): " << ex.what() << std::endl;
        return nullptr;
    }
}

std::shared_ptr<k::model::aggregate>
find_aggregate(const std::shared_ptr<k::compiler>& comp, const std::string& name) {
    if (!comp || !comp->get_unit()) return nullptr;
    auto root = comp->get_unit()->get_root_namespace();
    if (!root) return nullptr;
    return root->get_aggregate(name);
}

std::shared_ptr<k::model::klass>
find_klass(const std::shared_ptr<k::compiler>& comp, const std::string& name) {
    return std::dynamic_pointer_cast<k::model::klass>(find_aggregate(comp, name));
}

std::shared_ptr<k::model::annotation_type>
find_annotation_type(const std::shared_ptr<k::compiler>& comp, const std::string& name) {
    return std::dynamic_pointer_cast<k::model::annotation_type>(find_aggregate(comp, name));
}

std::shared_ptr<k::model::enumeration>
find_enum(const std::shared_ptr<k::compiler>& comp, const std::string& name) {
    if (!comp || !comp->get_unit()) return nullptr;
    auto root = comp->get_unit()->get_root_namespace();
    if (!root) return nullptr;
    return root->get_enum(name);
}

// ═══════════════════════════════════════════════════════════════════════════
// AST traversal helpers
// ═══════════════════════════════════════════════════════════════════════════

void collect_in_expr(k::model::expression* expr,
                     std::vector<k::model::function_invocation_expression*>& out)
{
    if (!expr) return;
    using namespace k::model;

    if (auto* inv = dynamic_cast<function_invocation_expression*>(expr)) {
        out.push_back(inv);
        if (inv->callee_expr()) collect_in_expr(inv->callee_expr().get(), out);
        for (auto& arg : inv->arguments()) collect_in_expr(arg.get(), out);
        return;
    }
    if (auto* bin = dynamic_cast<binary_expression*>(expr)) {
        collect_in_expr(bin->left().get(), out);
        collect_in_expr(bin->right().get(), out);
        return;
    }
    if (auto* un = dynamic_cast<unary_expression*>(expr)) {
        collect_in_expr(un->sub_expr().get(), out);
        return;
    }
    if (auto* mem = dynamic_cast<member_of_object_expression*>(expr)) {
        collect_in_expr(mem->sub_expr().get(), out);
        return;
    }
}

void collect_in_stmt(k::model::statement* stmt,
                     std::vector<k::model::function_invocation_expression*>& out)
{
    if (!stmt) return;
    using namespace k::model;

    if (auto* blk = dynamic_cast<block*>(stmt)) {
        for (auto& s : blk->get_statements()) collect_in_stmt(s.get(), out);
        return;
    }
    if (auto* ret = dynamic_cast<return_statement*>(stmt)) {
        if (ret->get_expression()) collect_in_expr(ret->get_expression().get(), out);
        return;
    }
    if (auto* es = dynamic_cast<expression_statement*>(stmt)) {
        if (es->get_expression()) collect_in_expr(es->get_expression().get(), out);
        return;
    }
    if (auto* vs = dynamic_cast<variable_statement*>(stmt)) {
        if (auto ctor = std::dynamic_pointer_cast<constructor_invocation_expression>(vs->get_init_expr())) {
            for (auto& arg : ctor->arguments()) collect_in_expr(arg.get(), out);
        }
        return;
    }
    if (auto* ifs = dynamic_cast<if_else_statement*>(stmt)) {
        if (ifs->get_test_expr()) collect_in_expr(ifs->get_test_expr().get(), out);
        if (ifs->get_then_stmt()) collect_in_stmt(ifs->get_then_stmt().get(), out);
        if (ifs->get_else_stmt()) collect_in_stmt(ifs->get_else_stmt().get(), out);
        return;
    }
    if (auto* ws = dynamic_cast<while_statement*>(stmt)) {
        if (ws->get_test_expr()) collect_in_expr(ws->get_test_expr().get(), out);
        if (ws->get_nested_stmt()) collect_in_stmt(ws->get_nested_stmt().get(), out);
        return;
    }
}

std::vector<k::model::function_invocation_expression*>
collect_invocations_in(const std::shared_ptr<k::compiler>& comp, const std::string& func_name) {
    if (!comp || !comp->get_unit()) return {};
    auto root = comp->get_unit()->get_root_namespace();
    if (!root) return {};

    std::shared_ptr<k::model::function> target_func;
    for (auto& fn : root->functions()) {
        if (fn && fn->get_short_name() == func_name) {
            target_func = fn;
            break;
        }
    }
    if (!target_func || !target_func->get_block()) return {};

    std::vector<k::model::function_invocation_expression*> result;
    collect_in_stmt(target_func->get_block().get(), result);
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// Parser AST comparison helpers
// ═══════════════════════════════════════════════════════════════════════════

bool is_same(const k::parse::ast::qualified_identifier& ident1, const k::name& ident2) {
    if (ident1.has_root_prefix() != ident2.has_root_prefix()) {
        return false;
    }
    if (ident1.size() != ident2.size()) {
        return false;
    }
    for (size_t idx = 0; idx < ident1.size(); idx++) {
        if (ident1[idx] != ident2[idx]) {
            return false;
        }
    }
    return true;
}

bool is_same(const k::parse::ast::identifier_expr& ident1, const k::name& ident2) {
    return is_same(ident1.qident, ident2);
}

// ═══════════════════════════════════════════════════════════════════════════
// Library / symbol inspection helpers
// ═══════════════════════════════════════════════════════════════════════════

bool has_defined_symbol_containing(const std::string& file, const std::string& substr) {
    auto res = k::tools::lookup_run_process(
        "nm", {"--defined-only", file});
    if (res.exit_code != 0) return false;
    return res.out.find(substr) != std::string::npos;
}

bool has_section_containing(const std::string& file, const std::string& section_substr) {
    auto contains_section = [&](const std::string& tool_name) -> std::optional<bool> {
        try {
            auto res = k::tools::lookup_run_process(tool_name, {"-S", file});
            if (res.exit_code != 0) {
                return false;
            }
            return res.out.find(section_substr) != std::string::npos;
        } catch (const k::tools::tool_not_found&) {
            return std::nullopt;
        }
    };

    if (auto v = contains_section("readelf"); v.has_value()) {
        return *v;
    }
    if (auto v = contains_section("llvm-readelf"); v.has_value()) {
        return *v;
    }
    return false;
}

std::filesystem::path kdi_path_for(const std::string& lib_path) {
    std::filesystem::path p(lib_path);
    p.replace_extension(".kdi");
    return p;
}

// ═══════════════════════════════════════════════════════════════════════════
// KDI import testing helpers
// ═══════════════════════════════════════════════════════════════════════════

std::string build_kdi_for_import_warning_test(const std::string_view& lib_src) {
    std::string so = build_shared_library(lib_src);
    auto rollback = llvm::make_scope_exit([&] {
        std::error_code ec;
        std::filesystem::remove(so, ec);
    });
    std::filesystem::path kdi = std::filesystem::path(so).replace_extension(".kdi");
    if (!std::filesystem::exists(kdi))
        throw std::runtime_error("expected .kdi not produced: " + kdi.string());
    auto result = kdi.string();
    rollback.release();
    return result;
}

std::vector<k::log::diagnostic> run_importer_with_logger(
    const std::string& unit_name,
    const std::vector<std::string>& module_names,
    k::path_lookup_file_resolver& resolver,
    std::vector<std::string> pre_used)
{
    auto comp = k::compiler::create();
    auto* model_unit = comp->get_unit().get();
    if (!model_unit) throw std::runtime_error("unit not created by compiler");

    model_unit->set_unit_name(k::name::from(unit_name));
    for (const auto& mn : module_names)
        model_unit->add_import(k::name::from(mn));

    test_logger tl;
    k::model::kdi_importer importer(*model_unit, resolver, tl);
    importer.import_all();
    importer.materialise_all(comp->get_context_for_test());

    for (auto& imp : model_unit->get_imports()) {
        const std::string canon = imp.module_name.to_string();
        for (const auto& pu : pre_used)
            if (canon == pu) imp.used = true;
    }

    importer.check_unused_imports();
    return tl.diagnostics;
}

std::string write_minimal_kdi(const std::string& module_name,
                               const std::vector<std::string>& deps)
{
    kdi::kdi_file f;
    f.header.module_name   = module_name;
    f.header.lib_base      = module_name;
    f.header.lib_path      = "lib" + module_name + ".so";
    f.header.target_triple = "x86_64-pc-linux-gnu";
    f.header.compiler_ver  = "0.0.0-test";
    f.header.dependencies  = deps;
    f.unit.name            = module_name;
    f.unit.root_ns.name    = "";
    f.unit.root_ns.fq_name = "";

    ReservedOutput output(".kdi");
    std::string path = output.path.string();
    if (!kdi::kdi_write_cbor_file(f, path))
        throw std::runtime_error("Cannot write test kdi: " + path);
    output.release();
    return path;
}

bool try_import(const std::string& unit_name,
                const std::string& first_import,
                const std::unordered_map<std::string,std::string>& kdi_paths,
                std::string* out_what)
{
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    for (const auto& [mod, path] : kdi_paths)
        resolver->add_explicit_path(mod, path);

    auto comp = k::compiler::create();
    comp->set_file_resolver(resolver);
    auto* model_unit = comp->get_unit().get();
    if (!model_unit) throw std::runtime_error("unit not created");

    model_unit->set_unit_name(k::name::from(unit_name));
    model_unit->add_import(k::name::from(first_import));

    test_logger tl;
    k::model::kdi_importer importer(*model_unit, *resolver, tl);
    try {
        importer.import_all();
        return false;
    } catch (const k::log::compiler_error& e) {
        if (out_what) *out_what = e.what();
        return true;
    } catch (const std::exception& e) {
        if (out_what) *out_what = std::string("std::exception: ") + e.what();
        return true;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// TmpDir
// ═══════════════════════════════════════════════════════════════════════════

TmpDir::TmpDir() : TmpDir(std::filesystem::temp_directory_path()) {}

TmpDir::TmpDir(const std::filesystem::path& parent) {
    std::string name = (std::filesystem::absolute(parent) / "kt-XXXXXX").string();
    if (!::mkdtemp(name.data()))
        throw std::system_error(errno, std::generic_category(), "create test directory");
    try {
        path = name;
        owned_path = path;
    } catch (...) {
        ::rmdir(name.c_str());
        throw;
    }
}

TmpDir::~TmpDir() noexcept {
    std::error_code ec;
    std::filesystem::remove_all(owned_path, ec);
}

std::string replace_k_path(std::string source, std::string_view token,
                           const std::filesystem::path& path) {
    std::string escaped;
    for (char c : path.generic_string()) {
        switch (c) {
            case '\\': escaped += "\\\\"; break;
            case '"': escaped += "\\\""; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default: escaped += c; break;
        }
    }
    std::size_t offset = 0;
    while ((offset = source.find(token, offset)) != std::string::npos) {
        source.replace(offset, token.size(), escaped);
        offset += escaped.size();
    }
    return source;
}

void require_unix_socket_path(const std::filesystem::path& path) {
    sockaddr_un address{};
    if (path.native().size() >= sizeof(address.sun_path))
        throw std::runtime_error("Unix socket test path exceeds sockaddr_un::sun_path");
}

std::filesystem::path TmpDir::create_file(const std::string& name) const {
    auto p = path / name;
    std::ofstream{p.string()}.close();
    return p;
}

// ═══════════════════════════════════════════════════════════════════════════
// TmpKdi
// ═══════════════════════════════════════════════════════════════════════════

TmpKdi::TmpKdi(const std::string_view& src, bool with_stdlib) {
    so_path = build_shared_library(src, {}, "", with_stdlib);
    auto rollback = llvm::make_scope_exit([&] {
        std::error_code ec;
        std::filesystem::remove(so_path, ec);
        if (!kdi_path.empty()) std::filesystem::remove(kdi_path, ec);
    });
    kdi_path = [&]() {
        std::filesystem::path p(so_path);
        p.replace_extension(".kdi");
        return p.string();
    }();
    if (!std::filesystem::is_regular_file(kdi_path)) {
        throw std::runtime_error("build_shared_library did not produce " + kdi_path);
    }
    rollback.release();
}

TmpKdi::~TmpKdi() {
    std::error_code ec;
    std::filesystem::remove(so_path, ec);
    std::filesystem::remove(kdi_path, ec);
}

std::filesystem::path TmpKdi::dir() const {
    return std::filesystem::path(kdi_path).parent_path();
}

// ═══════════════════════════════════════════════════════════════════════════
// klangc binary test helpers
// ═══════════════════════════════════════════════════════════════════════════

std::filesystem::path find_klangc() {
    std::error_code ec;
    auto self = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec) {
        throw std::runtime_error("Cannot resolve /proc/self/exe: " + ec.message());
    }
    auto klangc = self.parent_path() / "klangc";
    if (!std::filesystem::exists(klangc)) {
        throw std::runtime_error("Cannot find klangc binary at " + klangc.string());
    }
    return klangc;
}

std::string find_libk_dir() {
    std::error_code ec;
    auto self = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec) return {};
    auto build_dir = self.parent_path().parent_path(); // <build>
    auto libk_dir = build_dir / "libk" / "libk";
    if (std::filesystem::exists(libk_dir / "libk.so")) {
        return libk_dir.string();
    }
    return {};
}

ScopedLdLibraryPath::ScopedLdLibraryPath(const std::string& dir) {
    if (dir.empty()) return;
    const char* old_ld = ::getenv("LD_LIBRARY_PATH");
    had_old = (old_ld != nullptr);
    if (had_old) old_value = old_ld;
    std::string new_ld = dir + (had_old ? (":" + old_value) : "");
    if (::setenv("LD_LIBRARY_PATH", new_ld.c_str(), 1) != 0)
        throw std::system_error(errno, std::generic_category(), "set LD_LIBRARY_PATH");
    active = true;
}

ScopedLdLibraryPath::~ScopedLdLibraryPath() {
    if (!active) return;
    if (had_old) ::setenv("LD_LIBRARY_PATH", old_value.c_str(), 1);
    else         ::unsetenv("LD_LIBRARY_PATH");
}
