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

#ifndef KLANG_HELPERS_HPP
#define KLANG_HELPERS_HPP

#include <memory>
#include <string>

#include "../src/common/logger.hpp"
#include "../src/common/common.hpp"
#include "../src/common/process.hpp"
#include "../src/common/path_lookup_file_resolver.hpp"
#include "../src/gen/resolvers.hpp"
#include "../src/gen/generators.hpp"

#include <llvm/Target/TargetMachine.h>

/**
 * Create a TargetMachine configured for position-independent code (PIC),
 * suitable for shared-library compilation.
 */
llvm::TargetMachine* make_pic_target_machine();

std::unique_ptr<k::model::gen::jit> gen_jit(std::string_view src, bool dump = false, bool optimize = true);

/**
 * Like gen_jit() but lets k::log::compiler_error (and its subclasses) propagate
 * to the caller instead of catching them.  Use this in tests that verify the
 * compiler throws the expected exception for invalid input:
 *
 *   REQUIRE_THROWS_AS(gen_jit_throws(src), k::model::gen::resolution_error);
 */
std::unique_ptr<k::model::gen::jit> gen_jit_throws(std::string_view src, bool dump = false, bool optimize = true);

k::tools::exec_result build_and_exec(const std::string_view& src);

/**
 * Compile the given K source into a shared library (.so).
 * Returns the path to the generated .so file (in /tmp).
 * Throws std::runtime_error on failure.
 * The caller is responsible for removing the file when done.
 */
std::string build_shared_library(const std::string_view& src);

/**
 * Compile the given K source into a static library (.a).
 * Returns the path to the generated .a file (in /tmp).
 * Throws std::runtime_error on failure.
 * The caller is responsible for removing the file when done.
 */
std::string build_static_library(const std::string_view& src);

/**
 * Compile the given K source into both a shared library (.so) and a static
 * library (.a) in a single object-file generation pass.
 * Returns {so_path, a_path} — both located in /tmp.
 * Throws std::runtime_error on failure.
 * The caller is responsible for removing both files when done.
 */
std::pair<std::string, std::string> build_both_libraries(const std::string_view& src);

/**
 * Compile a library from @p lib_src and an executable from @p exec_src
 * that imports it, then run the executable and return its exit code.
 *
 * The executable is linked against the shared library; the library directory
 * is added to LD_LIBRARY_PATH (and as -rpath in the link) so the dynamic
 * loader can find it without any prior installation.
 *
 * @param lib_src   K source for the library module (no main()).
 * @param exec_src  K source for the executable module (has main()).
 * @return          exec_result from running the executable.
 * @throws std::runtime_error on any compilation / link failure.
 */
k::tools::exec_result build_exec_with_lib(const std::string_view& lib_src,
                                           const std::string_view& exec_src);

/**
 * Description of a library to build for build_exec_with_libs().
 */
struct LibSpec {
    std::string_view src;       ///< K source of the library module
    std::string      kdi_path;  ///< [out] filled in after build with the .kdi path
    std::string      so_path;   ///< [out] filled in after build with the .so path
    std::string      symlink_path; ///< [out] filled in with the lib<base>.so symlink
};

/**
 * Compile several library modules and one executable that imports them, then
 * run the executable.
 *
 * Each LibSpec in @p libs is compiled independently.  Their KDI descriptors
 * and shared-library paths are then made available to the executable compiler
 * via a path_lookup_file_resolver.
 *
 * @param libs      One entry per library to compile.
 * @param exec_src  K source for the executable module (must have main()).
 * @return          exec_result from running the final executable.
 * @throws std::runtime_error on any compilation / link / run failure.
 */
k::tools::exec_result build_exec_with_libs(std::vector<LibSpec>& libs,
                                            const std::string_view& exec_src);

/**
 * Identical to build_exec_with_libs() but when building the executable only
 * the KDIs for modules explicitly listed in @p direct_imports are registered
 * in the resolver by explicit path.  The remaining libraries are reachable
 * only through the resolver's search directories (simulating the real CLI
 * behaviour where transitive dependencies are discovered automatically).
 *
 * This lets tests verify that transitive KDI resolution via search-dirs works
 * correctly (i.e. no manual registration of every transitive KDI is needed).
 *
 * @param libs            All libraries to compile (same as build_exec_with_libs).
 * @param exec_src        K source for the executable.
 * @param direct_imports  Module names that the exe imports directly
 *                        (e.g. {"ival_lib","cval_lib"}).  Only these get
 *                        add_explicit_path(); all others are found via
 *                        add_search_dir().
 */
k::tools::exec_result build_exec_with_libs_direct_only(
    std::vector<LibSpec>& libs,
    const std::string_view& exec_src,
    const std::vector<std::string>& direct_imports);

/**
 * Compile @p src as a K source for an executable, with the given resolver,
 * and expect a k::log::compiler_error to be thrown.
 * Returns true if the compilation threw as expected, false otherwise.
 *
 * Usage:
 *   REQUIRE( compile_should_fail(src, resolver) );
 */
bool compile_should_fail(const std::string_view& src,
                         std::shared_ptr<k::path_lookup_file_resolver> resolver);

class test_logger : public k::log::logger {
public:
    /** All diagnostics reported via this logger (for inspection in tests). */
    std::vector<k::log::diagnostic> diagnostics;

    void report(const k::log::diagnostic& diag) override;

    /** True if at least one warning-level diagnostic was reported. */
    bool has_warning() const {
        return std::any_of(diagnostics.begin(), diagnostics.end(), [](const k::log::diagnostic& d){
            return d.level == k::log::diagnostic::severity::warning;
        });
    }

    /** True if at least one error-or-fatal-level diagnostic was reported. */
    bool has_error() const {
        return std::any_of(diagnostics.begin(), diagnostics.end(), [](const k::log::diagnostic& d){
            return d.level == k::log::diagnostic::severity::error
                || d.level == k::log::diagnostic::severity::fatal;
        });
    }

    /** Reset collected diagnostics. */
    void clear() { diagnostics.clear(); }
};

/**
 * Compile @p src (library or executable) with the given resolver and collect
 * all diagnostics emitted during compilation into @p out_logger.
 *
 * Does NOT throw on warnings; throws k::log::compiler_error on fatal errors
 * (same behaviour as the normal compilation path).
 *
 * Typical use: verify that a specific warning code is (or is not) emitted.
 *
 * @param src        K source to compile.
 * @param resolver   File resolver (may be nullptr for no imports).
 * @param out_logger logger that receives every diagnostic; inspectable after
 *                   the call via out_logger.diagnostics / out_logger.has_warning().
 * @return true if compilation completed without errors, false otherwise.
 */
bool compile_collect_diagnostics(
    const std::string_view& src,
    std::shared_ptr<k::path_lookup_file_resolver> resolver,
    test_logger& out_logger);

#endif //KLANG_HELPERS_HPP

