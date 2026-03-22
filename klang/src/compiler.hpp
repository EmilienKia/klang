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
#ifndef KLANG_COMPILER_HPP
#define KLANG_COMPILER_HPP
#include <string>
#include <string_view>
#include <vector>

#include "common/logger.hpp"
#include "common/file_resolver.hpp"
#include "parse/parser.hpp"

namespace llvm {
class TargetMachine;
}

namespace k {
namespace model {
class element;

namespace gen {
class implementation_generator;
class jit;
}

class unit;
class context;
}

/**
 * Options controlling LLVM IR text export.
 * Each flag independently enables export at a specific pipeline stage.
 * If a file path is empty, the output goes to stdout.
 */
struct IrOutputOptions {
    bool emit_raw_ir  = false;      ///< Export IR after code generation (before optimisation)
    std::string raw_ir_file;        ///< Destination file (empty = stdout)
    bool emit_opt_ir  = false;      ///< Export IR after optimisation
    std::string opt_ir_file;        ///< Destination file (empty = stdout)
    bool emit_kdi_json = false;     ///< Also write a .kdi.json alongside every .kdi
    bool no_emit_kdi  = false;      ///< Suppress .kdi generation even when producing a library
};

class compiler : protected log::logger,  public std::enable_shared_from_this<compiler> {
protected:
    static bool _compiler_class_init;

    /** All source files for the current compilation unit.
     *  MUST be reserve()'d before any lexing/parsing to prevent
     *  reallocation that would invalidate the string_views held by lexemes. */
    std::vector<source> _sources;

    /** Guard: set to true once lexing/parsing has started.
     *  Any attempt to add more sources after this point is a programming error. */
    bool _sources_locked = false;

    std::shared_ptr<k::parse::ast::unit> _ast_unit;
    std::shared_ptr<model::context> _context;
    std::shared_ptr<model::unit> _model_unit;

    llvm::TargetMachine* _target;

    /** Set to true when a fatal compilation error occurs (e.g. overload collision).
     *  to_jit() returns nullptr when this flag is set. */
    bool _has_compilation_error = false;

    IrOutputOptions _ir_output_options;

    /** File resolver used to locate .kdi files for imports. */
    std::shared_ptr<k::file_resolver> _file_resolver;

    /** When true, the current unit's root namespace must not collide with imports. */
    bool _enforce_ns_collision = false;

    /** Extra object files (.o) to include in the final link step.
     *  These are passed as-is to clang (or ar) alongside the K-generated .o. */
    std::vector<std::string> _extra_object_files;

    void process_generation(bool optimize = true, bool dump = true);

    compiler(llvm::TargetMachine* target = nullptr);

public:
    static void initialize();

    static std::shared_ptr<compiler> create(llvm::TargetMachine* target_machine = nullptr);

    std::shared_ptr<model::unit> get_unit() {
        return _model_unit;
    }

    std::shared_ptr<const model::unit> get_unit() const {
        return _model_unit;
    }

    /** Test-only: direct access to the compilation context (LLVM module etc.). */
    std::shared_ptr<model::context> get_context_for_test() const {
        return _context;
    }

    /**
     * Try to find elements recursively by their name.
     * If the name is absolute (starting by the "::" prefix, the lookup is done including the root namespace of the module,
     * or directly look at imported dependencies (importing dependencies is not implemented yet).
     * If the name is relative:
     * - First look at the members of the root namespace of the module
     * - Then if the name corresponds to the root namespace of the module, look into the module
     * - Then Look at the imported modules (not implemented yet)
     * @param name Name of element to look for
     * @return List of elements with the corresponding name
     */
    std::vector<std::shared_ptr<model::element>> find_elements(const name& name) const;
    std::vector<std::shared_ptr<model::element>> find_elements(const std::string& name) const {
        return find_elements(name::from(name));
    }

    /**
     * Try to find an element from its name and return its fully mangled name.
     * Works only when only one element with this exact name exists.
     * Only callable elements are considered (methods or global (or static) variable).
     * @param name Name of element to look for
     * @return Mangled name of found element
     * @throw std::runtime_exception If multiple elements with same name exists or not found element.
     */
    std::string get_element_mangled_name(const name& name) const;
    std::string get_element_mangled_name(const std::string& name) const {
        return get_element_mangled_name(name::from(name));
    }


    void parse_source(const std::string_view& path, const std::string_view& src, bool optimize = true, bool dump = false);

    /**
     * Compile multiple source files as a single compilation unit (module).
     * All source contents are loaded before parsing starts so that the
     * internal source vector can be reserve()'d once, ensuring that the
     * string_views held by lexemes remain valid throughout compilation.
     *
     * @param sources   Pairs of {path, content} for every source file.
     * @param optimize  Run optimisation passes after code generation.
     * @param dump      Dump intermediate representations to stdout.
     * @param forced_module_name  If non-empty, override any module declaration
     *                            found in the source files (CLI --module-name).
     */
    void parse_sources(std::vector<std::pair<std::string, std::string>> sources,
                       bool optimize = true, bool dump = false,
                       const std::string& forced_module_name = "");

    /**
     * Configure the LLVM IR text export options.
     * Must be called before parse_source() / gen_object_file() / gen_executable().
     * Automatic file names are resolved lazily when the output file is known.
     */
    void set_ir_output_options(const IrOutputOptions& opts);

    /**
     * Set the file resolver used to locate .kdi files for imports.
     * If not set, a default path_lookup_file_resolver (current directory only)
     * is used.
     */
    void set_file_resolver(std::shared_ptr<k::file_resolver> resolver);

    /**
     * When set to true, the root namespace of the unit being compiled must
     * not collide with the root namespace of any imported module.
     * Default: false (the current unit always prevails over imports).
     */
    void set_enforce_ns_collision(bool enforce);

    /**
     * Set additional pre-compiled object files (.o) to include in the link
     * step (gen_executable, gen_shared_library, gen_static_library, gen_libraries).
     * The paths are passed verbatim to clang / ar after the K-generated .o.
     */
    void set_extra_object_files(std::vector<std::string> paths);

    /**
     * Resolve automatic IR file names based on the output file path.
     * Safe to call multiple times (already-set file names are not overwritten).
     * @param output_file  The object/executable output file path.
     */
    void resolve_ir_filenames(const std::string& output_file);

    bool has_main_method() const;

    /**
     * Convert a K unit/module name to a library base name by replacing every
     * "::" separator with ".".
     * Example: "my::test::lib"  →  "my.test.lib"
     * This is a pure utility — it does not require a compiler instance.
     */
    static std::string unit_name_to_lib_base(const std::string& unit_name);

    /**
     * Return the library base name derived from the current module's unit name.
     * Convenience wrapper around unit_name_to_lib_base(get_unit()->get_unit_name().to_string()).
     */
    std::string get_lib_base_name() const;

    /**
     * Build the list of linker arguments (-L<dir> and -l<base>) for all
     * imported modules that were actually used during this compilation.
     *
     * For each used import whose KDI header carries a non-empty lib_base, a
     * "-l<lib_base>" argument is added.  The directories come from the current
     * file_resolver search paths (cast to path_lookup_file_resolver), emitted
     * once each as "-L<dir>".
     *
     * Called automatically by gen_executable(), gen_shared_library() and
     * gen_libraries() just before invoking clang.
     */
    std::vector<std::string> build_import_link_args() const;

    void dump_gen_code();
    bool verify_gen_code();
    void optimize_gen_code();

    std::unique_ptr<k::model::gen::jit> to_jit(bool init_runtime = true);

    /**
     * Generate a KDI description file (.kdi) for the current module.
     * The KDI file is placed alongside the binary (same directory, same stem,
     * extension replaced with ".kdi").
     *
     * Called automatically by gen_shared_library(), gen_static_library() and
     * gen_libraries() after a successful link.
     *
     * @param lib_path   Path to the produced binary (.so or .a).
     * @return true on success.
     */
    bool gen_kdi(const std::string& lib_path);

    bool gen_object_file(const std::string& output_file);

    /**
     * Compile and link into a native executable.
     * Requires a main() function in the module.
     * If output_file is empty, the name is derived from the module's unit name.
     */
    bool gen_executable(const std::string& output_file = "");

    /**
     * Compile and link the current module as a shared library (.so).
     * If output_file is empty, the output is named lib<base>.so in the current
     * directory, where <base> = unit_name_to_lib_base(module_name).
     */
    bool gen_shared_library(const std::string& output_file = "");

    /**
     * Archive the current module into a static library (.a).
     * If output_file is empty, the output is named lib<base>.a in the current
     * directory, where <base> = unit_name_to_lib_base(module_name).
     * Uses "ar rcs" — no linker involved.
     */
    bool gen_static_library(const std::string& output_file = "");

    /**
     * Produce both a shared library and a static library in a single object-file
     * generation pass.  At least one of shared_out / static_out must be non-empty
     * when the module name cannot be determined, otherwise the names are derived
     * automatically (lib<base>.so and lib<base>.a).
     *
     * @param shared_out  Output path for the .so, or empty for automatic naming.
     * @param static_out  Output path for the .a,  or empty for automatic naming.
     * @return true if all requested outputs were produced successfully.
     */
    bool gen_libraries(const std::string& shared_out = "", const std::string& static_out = "");

    void print_logs();

protected:
    void find_elements_from(const name& name, const std::shared_ptr<model::element>& element, std::vector<std::shared_ptr<model::element>>& res) const;

    /**
     * Emit the current LLVM module as text IR.
     * @param filepath  Destination file path. If empty, writes to stdout.
     */
    void emit_ir(const std::string& filepath);

    /**
     * Find which source file a character pointer belongs to, by comparing
     * the address against the data ranges of all loaded sources.
     * This is only used for diagnostic messages (error path), so O(n) on the
     * number of source files is perfectly acceptable.
     *
     * @param ptr  Pointer into the content of some source file.
     * @return     Pointer to the matching source, or nullptr if not found.
     */
    const source* source_for_position(const char* ptr) const;

    char_coord coordinates_from_pos(const k::char_pos& coord) const;
    std::pair<char_coord,char_coord> coordinates_from_lex(const lex::lexeme& lex) const;

    void log_source_line(const source& src, char_coord pos);
    void log_source_line(const source& src, unsigned int line, unsigned int col);

    void log_source_line(const source& src, char_coord start, char_coord end);
    void log_source_line(const source& src, unsigned int line, unsigned int start, unsigned int end);
    void log_source_lines(const source& src, unsigned int line_start, unsigned int start, unsigned int line_end, unsigned int end);

    void report(const k::log::diagnostic& diag) override;


};

} // k
#endif //KLANG_COMPILER_HPP