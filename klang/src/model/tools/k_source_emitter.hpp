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

#ifndef KLANG_K_SOURCE_EMITTER_HPP
#define KLANG_K_SOURCE_EMITTER_HPP

/**
 * @file k_source_emitter.hpp
 *
 * k_source_emitter — reconstructs syntactically valid K source text from the
 * semantic model of a template entity (aggregate or function).
 *
 * The emitted source uses fully-qualified type names (de-aliased from `using`
 * directives) so that importing compilers can re-parse the fragment without
 * needing the original module's `using` declarations.
 *
 * Template parameter placeholder types (unresolved_type with
 * is_template_param_placeholder() == true) are emitted as bare names (e.g. "T")
 * since they are expected to remain unresolved until instantiation.
 *
 * This replaces the raw-source-text capture mechanism for KDI template export.
 */

#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>

#include "../../common/common.hpp"

namespace k::model {

class type;
class aggregate;
class function;
class constructor;
class destructor;
class expression;
class statement;
class block;
class parameter;
class member_variable_definition;
struct tpl_info;

/**
 * Emits syntactically valid K source text from semantic model nodes.
 *
 * This is NOT a model_visitor — it is a standalone utility class that walks
 * the model graph explicitly and writes to an internal string stream.
 *
 * Entry points:
 *   - emit_template_aggregate()  — for template struct/class/interface
 *   - emit_template_function()   — for template free functions
 *
 * Each entry point returns a self-contained K source fragment suitable for
 * storage in kdi_template_def::source and later re-parsing by the importer.
 */
class k_source_emitter {
public:
    k_source_emitter() = default;

    /**
     * Set a map of using-alias names to fully-qualified type names.
     * When the emitter encounters an unresolved type whose name matches a key,
     * it emits the mapped FQ name instead.
     */
    void set_alias_map(std::unordered_map<std::string, std::string> map) {
        _alias_map = std::move(map);
    }

    /**
     * Return the fully-qualified name of a type suitable for K source,
     * stripping the leading "::" prefix.
     */
    static std::string fq_name_for_source(const std::string& fq);

    /**
     * Emit a complete template aggregate definition as K source.
     * Includes the `template<...>` clause, the aggregate keyword + name,
     * bases, body (members, methods, constructors, destructor), and closing brace.
     *
     * @param agg  The template aggregate to emit.
     * @return     Self-contained K source fragment.
     */
    std::string emit_template_aggregate(const aggregate& agg);

    /**
     * Emit a complete template function definition as K source.
     * Includes the `template<...>` clause, the function signature, and body.
     *
     * @param fn   The template function to emit.
     * @return     Self-contained K source fragment.
     */
    std::string emit_template_function(const function& fn);

private:
    std::ostringstream _os;

    /** Map from using-alias names to fully-qualified type names. */
    std::unordered_map<std::string, std::string> _alias_map;

    /** Reset the internal stream. */
    void reset();

    // ── Type emission ────────────────────────────────────────────────────────

    /** Emit a type as K source text. Uses FQ names for aggregate/enum types. */
    void emit_type(const std::shared_ptr<type>& t);

    // ── Template parameter clause ────────────────────────────────────────────

    /** Emit `template<...>` clause from tpl_info. */
    void emit_template_clause(const tpl_info& ti);

    // ── Aggregate parts ──────────────────────────────────────────────────────

    /** Emit base class list (": Base1, Base2"). */
    void emit_bases(const aggregate& agg);

    /** Emit a single member variable declaration. */
    void emit_member_variable(const member_variable_definition& var);

    /** Emit a function/method signature and body. */
    void emit_function(const function& fn);

    /** Emit a constructor signature and body. */
    void emit_constructor(const constructor& ctor);

    /** Emit a destructor signature and body. */
    void emit_destructor(const destructor& dtor);

    /** Emit parameter list "(name : type, ...)". */
    void emit_parameter_list(const function& fn);

    // ── Statement emission ───────────────────────────────────────────────────

    void emit_statement(const std::shared_ptr<statement>& stmt, int indent);
    void emit_block(const block& blk, int indent);

    // ── Expression emission ──────────────────────────────────────────────────

    void emit_expression(const std::shared_ptr<expression>& expr);

    // ── Value emission ───────────────────────────────────────────────────────

    /** Emit a compile-time value (for template default values). */
    void emit_value(const k::value_type& val);

    // ── Helpers ──────────────────────────────────────────────────────────────

    /** Emit indentation (tabs). */
    void indent(int level);

    /** Return the K keyword for an aggregate: "struct", "class", or "interface". */
    static const char* aggregate_keyword(const aggregate& agg);
};

} // namespace k::model

#endif // KLANG_K_SOURCE_EMITTER_HPP







