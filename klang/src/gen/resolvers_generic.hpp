/*
 * K Language compiler
 *
 * Copyright 2023-2026 Emilien Kia
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

#ifndef KLANG_RESOLVERS_GENERIC_HPP
#define KLANG_RESOLVERS_GENERIC_HPP

#include "resolvers_common.hpp"

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace k::model {
class expression;
class statement;
}

namespace k::model::gen {

/**
 * Generic constraint validator — runs after model_builder.
 *
 * Validates that all declarations annotated with the 'generic' keyword satisfy
 * the structural constraints required for uniform code synthesis:
 *
 *  1. All type parameters of a generic declaration must appear only via
 *     addressers in member/parameter/return types and local variable types.
 *     Direct usage (bare T without an addresser wrapper) is forbidden.
 *
 *  2. The owner ('!') and drain ('#') addressers must not be applied to a
 *     generic type parameter: the erased synthesised body cannot run the
 *     destructor / explicit member calls they require. Owners and drains are
 *     also rejected as type arguments (see validate_template_arg_constraints).
 *
 *  3. No member (field or method) may be accessed through an expression whose
 *     type is an erased generic type parameter (e.g. 'v->field', 'v.method()'
 *     with 'v : T*'). This is a syntactic check on declared types; the
 *     resolvers still reject the remaining cases.
 *
 * Errors are reported via the logger using codes from k::diag::generic_diag.
 * A k::log::resolution_error is thrown on the first fatal violation.
 */
class generic_constraint_validator : public default_model_visitor,
                                     protected k::log::logger_relay {
public:
    generic_constraint_validator(k::log::logger& logger,
                                 std::shared_ptr<context> context,
                                 unit& unit)
        : k::log::logger_relay(logger),
          _context(std::move(context)),
          _unit(unit) {}

    /** Run the validation pass over the entire unit. */
    void validate();

    /**
     * Report a fatal resolution error at the model level.
     */
    [[noreturn]] void throw_error(unsigned int code,
                                  const lex::opt_any_lexeme& lexeme,
                                  const std::string& message,
                                  const std::vector<std::string>& args = {}) {
        auto diag = k::log::diagnostic::make_error(code, message, args);
        if (lexeme) diag.at(*lexeme);
        logger_relay::report(diag);
        throw resolution_error(std::move(diag));
    }

    // Visitor overrides
    void visit_unit(k::model::unit& u) override;
    void visit_namespace(k::model::ns& n) override;
    void visit_aggregate(k::model::aggregate& agg) override;
    void visit_structure(k::model::structure& s) override;
    void visit_klass(k::model::klass& k) override;
    void visit_interface(k::model::interface& i) override;

private:
    std::shared_ptr<context> _context;
    unit& _unit;

    /** Local scopes: name -> "declared type is an erased generic parameter behind addressers". */
    std::vector<std::unordered_map<std::string, bool>> _scopes;
    /** Same information for the member variables of the generic aggregate being validated. */
    std::unordered_map<std::string, bool> _member_scope;

    void push_scope();
    void pop_scope();
    void declare_local(const std::string& name,
                       const std::shared_ptr<type>& t,
                       const std::unordered_set<std::string>& param_names);

    /** True when @p expr is a name/member/dereference whose declared type is an erased generic parameter. */
    bool is_erased_expression(const std::shared_ptr<expression>& expr,
                              const std::unordered_set<std::string>& param_names) const;

    /**
     * Validate all generic-specific constraints for a given aggregate that has
     * is_generic() == true.
     *
     * Walks member variables, function signatures (parameters + return type),
     * and checks that generic type params are only used via addressers.
     */
    void validate_generic_aggregate(aggregate& agg);

    /**
     * Validate all generic-specific constraints for a standalone generic function
     * (is_generic() == true).
     */
    void validate_generic_function(function& fn,
                                   const std::unordered_set<std::string>& param_names,
                                   const std::vector<template_param_descriptor>& param_descs);

    /** Validate one declaration/local-variable type in the current generic context.
     *  @param lexeme  Best-effort source location of the declaration/usage being validated. */
    void validate_type_usage(const std::shared_ptr<type>& t,
                             const std::string& context_desc,
                             const std::unordered_set<std::string>& param_names,
                             const std::vector<template_param_descriptor>& param_descs,
                             const lex::opt_any_lexeme& lexeme = std::nullopt);

    /** Recursively validate local-variable declarations inside a statement tree. */
    void validate_statement_tree(const std::shared_ptr<statement>& stmt,
                                 const std::unordered_set<std::string>& param_names,
                                 const std::vector<template_param_descriptor>& param_descs);

    /** Recursively validate expression trees for generic type usages in typed expression nodes. */
    void validate_expression_tree(const std::shared_ptr<expression>& expr,
                                  const std::unordered_set<std::string>& param_names,
                                  const std::vector<template_param_descriptor>& param_descs);

    /** Validate explicit template type arguments attached to a symbol expression (e.g. foo<T>()). */
    void validate_explicit_template_args(const std::shared_ptr<symbol_expression>& sym_expr,
                                         const std::unordered_set<std::string>& param_names,
                                         const std::vector<template_param_descriptor>& param_descs);

    /** Merge nested generic parameters into the active validation context. */
    static void merge_template_context(const std::vector<template_param_descriptor>& extra,
                                       std::unordered_set<std::string>& param_names,
                                       std::vector<template_param_descriptor>& param_descs);

    /**
     * Check whether a type uses a generic type parameter directly (not via an
     * addresser). Returns the offending parameter name, or empty string if OK.
     *
     * A direct usage is when the outermost type node is an unresolved_type
     * whose name matches one of the generic param names.  A usage through an
     * addresser (reference, pointer, link, view) is acceptable
     * because all addressers map to a pointer at the LLVM level.
     *
     * @param t           The type to check.
     * @param param_names Set of generic type parameter names.
     * @return The offending parameter name if the type is a direct usage, or "".
     */
    static std::string check_direct_usage(const std::shared_ptr<type>& t,
                                          const std::unordered_set<std::string>& param_names);

    /**
     * Check whether an owner ('!') or drain ('#') addresser is applied to a
     * generic type parameter anywhere in the type.
     *
     * @param t           The type to check.
     * @param param_names Set of generic type parameter names.
     * @param is_drain    Set to true when the offending addresser is a drain.
     * @return The offending parameter name, or "" if the type is acceptable.
     */
    static std::string check_owner_or_drain(
        const std::shared_ptr<type>& t,
        const std::unordered_set<std::string>& param_names,
        bool& is_drain);

    /**
     * Report a direct-usage violation for the given member context.
     * @param param_name  The offending generic parameter name.
     * @param context_desc  Human-readable context (e.g. "member 'val'").
     * @param lexeme  Best-effort source location.
     */
    [[noreturn]] void report_direct_usage_error(const std::string& param_name,
                                   const std::string& context_desc,
                                   const lex::opt_any_lexeme& lexeme);

    /** Report an owner ('!') or drain ('#') applied to a generic type parameter. */
    [[noreturn]] void report_owner_drain_error(const std::string& param_name,
                                               bool is_drain,
                                               const std::string& context_desc,
                                               const lex::opt_any_lexeme& lexeme);

    /** Report a member access through an erased generic type parameter. */
    [[noreturn]] void report_erased_member_access(const std::string& member_name,
                                                  const lex::opt_any_lexeme& lexeme);
};

} // namespace k::model::gen

#endif // KLANG_RESOLVERS_GENERIC_HPP





