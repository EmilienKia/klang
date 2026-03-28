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

#include <atomic>
#include <fstream>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <filesystem>

#include "../src/common/logger.hpp"
#include "../src/common/common.hpp"
#include "../src/common/process.hpp"
#include "../src/common/path_lookup_file_resolver.hpp"
#include "../src/gen/resolvers.hpp"
#include "../src/gen/generators.hpp"
#include "../src/compiler.hpp"
#include "../src/model/model.hpp"
#include "../src/model/imported.hpp"
#include "../src/model/expressions.hpp"
#include "../src/model/statements.hpp"
#include "../src/parse/parser.hpp"
#include "../src/model/tools/kdi_importer.hpp"

#include <kdi.hpp>

#include <llvm/Target/TargetMachine.h>

/**
 * Create a TargetMachine configured for position-independent code (PIC),
 * suitable for shared-library compilation.
 */
llvm::TargetMachine* make_pic_target_machine();

std::unique_ptr<k::model::gen::jit> gen_jit(std::string_view src, bool dump = false, bool optimize = true);

/**
 * Like gen_jit() but configures a file resolver so that the K base standard
 * library (module "k") is importable, and loads libk.so into the current
 * process so the JIT can resolve its symbols.
 *
 * @param src             K source code (should contain `import k;`).
 * @param stdlib_kdi_dir  Directory containing k.kdi (libk build dir).
 * @param stdlib_lib_dir  Directory containing libk.so (same or separate).
 * @param dump            Dump intermediate representations.
 * @param optimize        Run optimisation passes.
 */
std::unique_ptr<k::model::gen::jit> gen_jit_with_stdlib(
    std::string_view src,
    const std::string& stdlib_kdi_dir,
    const std::string& stdlib_lib_dir,
    bool dump = false, bool optimize = true);

/**
 * Like gen_jit() but compiles multiple source files as a single compilation unit.
 * @param sources   Pairs of {path, content} for every source file in the unit.
 * @param dump      Dump intermediate representations.
 * @param optimize  Run optimisation passes.
 * @param forced_module_name  Override module name (empty = use source declarations).
 */
std::unique_ptr<k::model::gen::jit> gen_jit_multi(
    std::vector<std::pair<std::string, std::string>> sources,
    bool dump = false, bool optimize = true,
    const std::string& forced_module_name = "");

/**
 * Like gen_jit_multi() but lets compiler exceptions propagate to the caller.
 * Use in tests that expect compilation to fail.
 */
std::unique_ptr<k::model::gen::jit> gen_jit_multi_throws(
    std::vector<std::pair<std::string, std::string>> sources,
    bool dump = false, bool optimize = true,
    const std::string& forced_module_name = "");

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

// ═══════════════════════════════════════════════════════════════════════════
// Model inspection helpers
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Compile K source and return the compiler (which provides access to the model unit).
 * Returns nullptr if compilation fails.
 * Note: parse_source() runs ALL passes including code generation; we inspect
 * the model AFTER full compilation so all materializer passes have run.
 */
std::shared_ptr<k::compiler> compile_model(std::string_view src);

/**
 * Navigate to an aggregate by its short name within the root namespace.
 */
std::shared_ptr<k::model::aggregate>
find_aggregate(const std::shared_ptr<k::compiler>& comp, const std::string& name);

/**
 * Navigate to a klass by its short name within the root namespace.
 */
std::shared_ptr<k::model::klass>
find_klass(const std::shared_ptr<k::compiler>& comp, const std::string& name);

/**
 * Navigate to an annotation_type by its short name within the root namespace.
 */
std::shared_ptr<k::model::annotation_type>
find_annotation_type(const std::shared_ptr<k::compiler>& comp, const std::string& name);

// ═══════════════════════════════════════════════════════════════════════════
// AST traversal helpers
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Recursively collect all function_invocation_expression nodes within an expression tree.
 */
void collect_in_expr(k::model::expression* expr,
                     std::vector<k::model::function_invocation_expression*>& out);

/**
 * Recursively collect all function_invocation_expression nodes within a statement tree.
 */
void collect_in_stmt(k::model::statement* stmt,
                     std::vector<k::model::function_invocation_expression*>& out);

/**
 * Find all function invocations inside a named function within the root namespace.
 */
std::vector<k::model::function_invocation_expression*>
collect_invocations_in(const std::shared_ptr<k::compiler>& comp, const std::string& func_name);

// ═══════════════════════════════════════════════════════════════════════════
// Parser AST comparison helpers
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Compare a parsed qualified_identifier against a k::name for equality.
 */
bool is_same(const k::parse::ast::qualified_identifier& ident1, const k::name& ident2);

/**
 * Compare a parsed identifier_expr against a k::name for equality.
 */
bool is_same(const k::parse::ast::identifier_expr& ident1, const k::name& ident2);

// ═══════════════════════════════════════════════════════════════════════════
// Library / symbol inspection helpers
// ═══════════════════════════════════════════════════════════════════════════

/**
 * True if the nm output for the file contains a defined symbol whose name
 * includes the given substring. Works for both .so (--dynamic) and .a.
 */
bool has_defined_symbol_containing(const std::string& file, const std::string& substr);

/**
 * Given a library path (e.g. /tmp/foo.so or /tmp/foo.a), derive the expected
 * path of the KDI file (same stem, extension = .kdi).
 */
std::filesystem::path kdi_path_for(const std::string& lib_path);

/**
 * Build a .kdi in /tmp from a simple lib source and return its path.
 */
std::string build_kdi_for_import_warning_test(const std::string_view& lib_src);

/**
 * Run kdi_importer phases A+B+C on a fresh unit importing the given
 * module names.  Returns all diagnostics collected by the test_logger.
 * @param pre_used  module names to mark as used before checking for unused imports.
 */
std::vector<k::log::diagnostic> run_importer_with_logger(
    const std::string& unit_name,
    const std::vector<std::string>& module_names,
    k::path_lookup_file_resolver& resolver,
    std::vector<std::string> pre_used = {});

/**
 * Build a minimal kdi_file with forged dependencies and write it to /tmp.
 * Returns the path to the written .kdi file.
 */
std::string write_minimal_kdi(const std::string& module_name,
                               const std::vector<std::string>& deps);

/**
 * Attempt to load a single import via kdi_importer and return whether
 * a compiler_error was thrown.  Fills *out_what with e.what() on throw.
 * @param kdi_paths  map of module_name → kdi file path (for explicit resolution)
 */
bool try_import(const std::string& unit_name,
                const std::string& first_import,
                const std::unordered_map<std::string,std::string>& kdi_paths,
                std::string* out_what = nullptr);

// ═══════════════════════════════════════════════════════════════════════════
// Temporary directory / file RAII helpers
// ═══════════════════════════════════════════════════════════════════════════

/**
 * RAII wrapper around a temporary directory.
 *
 * Creates a unique directory under the system temp path on construction
 * and recursively removes it on destruction.
 */
struct TmpDir {
    std::filesystem::path path;

    TmpDir();
    ~TmpDir();

    /// Create an empty file inside the tmp dir; returns its path.
    std::filesystem::path create_file(const std::string& name) const;
};

/**
 * RAII wrapper that compiles a K library source into a shared library,
 * verifies that the .kdi companion file was produced, and removes both
 * on destruction.
 *
 * Usage:
 *   TmpKdi kdi(some_k_source);
 *   kdi.kdi_path;   // path to the .kdi file
 *   kdi.so_path;    // path to the .so  file
 *   kdi.dir();      // parent directory
 */
struct TmpKdi {
    std::string so_path;
    std::string kdi_path;

    explicit TmpKdi(const std::string_view& src);
    ~TmpKdi();

    /// Directory that contains the .kdi file.
    std::filesystem::path dir() const;
};

// ═══════════════════════════════════════════════════════════════════════════
// klangc binary test helpers
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Locate the klangc binary next to the current test executable.
 * On Linux, resolves /proc/self/exe to find the directory.
 * @throws std::runtime_error if the binary cannot be found.
 */
std::filesystem::path find_klangc();

/**
 * Locate the libk build directory (for LD_LIBRARY_PATH).
 * Test binary is at <build>/klang/klang-tests, libk.so is at <build>/libk/libk/.
 * @return The directory containing libk.so, or empty string if not found.
 */
std::string find_libk_dir();

/**
 * RAII helper to temporarily prepend a directory to LD_LIBRARY_PATH.
 * Restores the original value on destruction.
 */
struct ScopedLdLibraryPath {
    std::string old_value;
    bool had_old = false;

    explicit ScopedLdLibraryPath(const std::string& dir);
    ~ScopedLdLibraryPath();
};

#endif //KLANG_HELPERS_HPP

