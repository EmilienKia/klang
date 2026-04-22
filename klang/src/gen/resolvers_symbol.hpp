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

#ifndef KLANG_RESOLVERS_SYMBOL_HPP
#define KLANG_RESOLVERS_SYMBOL_HPP

#include "resolvers_scope_lookup.hpp"

namespace k::model::gen {

/**
 * Unit symbol resolver
 * This helper class will resolve method and variable usages to their definitions.
 * It must be run after the model building phase and before type resolution and any code generation phase.
 */
class symbol_resolver : public default_model_visitor, protected k::log::logger_relay {
protected:

    std::shared_ptr<context> _context;

    unit& _unit;

    /** Stack of functions currently being visited (for visibility access-site context). */
    std::vector<std::shared_ptr<function>> _function_stack;

public:
    symbol_resolver(k::log::logger& logger, std::shared_ptr<context> context, unit& unit) :
    k::log::logger_relay(logger),
    _context(context),
    _unit(unit)  {
    }
    void resolve();

protected:

    std::variant<std::monostate, std::shared_ptr<variable_definition>, std::shared_ptr<function>>
    resolve_symbol(const element& elem, const name& name);

    std::variant<std::monostate, std::shared_ptr<variable_definition>, std::shared_ptr<function>>
    resolve_symbol(const symbol_expression& symbol) {
        return resolve_symbol(symbol, symbol.get_name());
    }

    /** Resolve a name anchored at the root namespace of the unit (handles :: prefix). */
    std::variant<std::monostate, std::shared_ptr<variable_definition>, std::shared_ptr<function>>
    resolve_symbol_from_root(const name& name);

    /** Resolve a qualified name (no root prefix) anchored at a given element, without climbing to parents. */
    static std::variant<std::monostate, std::shared_ptr<variable_definition>, std::shared_ptr<function>>
    resolve_qualified_from(const element& elem, const name& qualified_name);

    /**
     * Try to resolve a name through the using directives of the given scope element.
     * Returns a non-monostate result if the name can be resolved via a using directive.
     * If multiple using directives match (ambiguity), reports an error.
     */
    std::variant<std::monostate, std::shared_ptr<variable_definition>, std::shared_ptr<function>>
    resolve_via_using(const element& elem, const name& name);

    [[noreturn]] void throw_error(unsigned int code, const lex::lexeme& lexeme, const std::string& message, const std::vector<std::string>& args = {}) {
        auto diag = k::log::diagnostic::make_error(code, message, args);
        logger_relay::report(diag);
        throw resolution_error(std::move(diag));
    }

    [[noreturn]] void throw_error(unsigned int code, const lex::opt_any_lexeme& lexeme, const std::string& message, const std::vector<std::string>& args = {}) {
        auto diag = k::log::diagnostic::make_error(code, message, args);
        if (lexeme) diag.at(*lexeme);
        logger_relay::report(diag);
        throw resolution_error(std::move(diag));
    }


protected:

    /**
     * Check if a variable (member or global) is accessible from the given access-site element.
     * Throws a resolution_error if the element is not accessible.
     * @param var   The variable definition being accessed.
     * @param access_site  The element from which the access occurs.
     */
    void check_variable_visibility(const variable_definition& var, const element& access_site);

    /**
     * Resolve and validate annotation instances on any annotation_holder.
     * - Looks up each annotation type by name (local scope + imports).
     * - Validates that the target is an annotation_type.
     * - Enforces @Target constraints (element_kind must be in the allowed list).
     *
     * @param holder       The annotation_holder carrying unresolved annotation instances.
     * @param scope        The element used as the starting scope for type lookups.
     * @param err_lexeme   Lexeme used for error location reporting.
     * @param element_kind The kind string for @Target validation: "CLASS", "INTERFACE", "ANNOTATION", "FUNCTION", etc.
     */
    void resolve_and_validate_annotations(
        annotation_holder& holder,
        element& scope,
        const std::string& element_name,
        const lex::opt_any_lexeme& err_lexeme,
        const std::string& element_kind);

    void visit_named_element(named_element&);

    void visit_unit(unit&) override;

    void visit_namespace(ns&) override;
    void visit_aggregate(aggregate&) override;
    void visit_klass(klass&) override;
    void visit_interface(interface&) override;
    void visit_annotation_type(annotation_type&) override;
    void visit_enumeration(enumeration&) override;

    void visit_member_variable_definition(member_variable_definition&) override;
    void visit_global_variable_definition(global_variable_definition&) override;
    void visit_parameter(parameter &) override;
    void visit_function(function&) override;
    void visit_constructor(constructor&) override;
    void visit_static_constructor(static_constructor&) override;
    void visit_static_destructor(static_destructor&) override;

    void visit_block(block&) override;
    void visit_return_statement(return_statement&) override;
    void visit_break_statement(break_statement&) override;
    void visit_continue_statement(continue_statement&) override;
    void visit_if_else_statement(if_else_statement&) override;
    void visit_while_statement(while_statement&) override;
    void visit_for_statement(for_statement&) override;
    void visit_expression_statement(expression_statement&) override;
    void visit_variable_statement(variable_statement&) override;

    void visit_value_expression(value_expression&) override;
    void visit_symbol_expression(symbol_expression&) override;
    void visit_unary_expression(unary_expression&) override;
    void visit_binary_expression(binary_expression&) override;

    void process_arithmetic(binary_expression&);

    void visit_arithmetic_binary_expression(arithmetic_binary_expression &expression) override;
    void visit_arithmetic_assignation_expression(arithmetic_assignation_expression &expression) override;

    void visit_member_of_expression(member_of_expression &) override;
    void visit_function_invocation_expression(function_invocation_expression &) override;
    void visit_constructor_invocation_expression(constructor_invocation_expression &) override;
    void visit_temporary_construction_expression(temporary_construction_expression &) override;
    void visit_new_expression(new_expression &) override;
    void visit_delete_expression(delete_expression &) override;
    void visit_owner_move_expression(owner_move_expression &) override;
    void visit_array_init_expression(array_init_expression &) override;
    void visit_designated_struct_init_expression(designated_struct_init_expression &) override;
    std::shared_ptr<expression> adapt_reference_load_value(const std::shared_ptr<expression>& expr);

    /**
     * Adapt an expression to ensure it maps to a given type, by casting it.
     * @param expr Expression to map.
     * @param type Type to target
     * @return The given arg expression if already compatible, the new wrapping casting expr if mapping, nullptr if not possible.
     */
    std::shared_ptr<expression> adapt_type(std::shared_ptr<expression> expr, const std::shared_ptr<type>& type);

    /**
     * After initial redirect target resolution, follow chains transitively.
     * For each redirected function, follow the chain to the final non-redirected target.
     * Detects and reports circular redirect chains.
     */
    void resolve_redirect_chains(unit& unit);

    /**
     * Follow the redirect chain from a single function to its final target.
     * @param fn The redirected function.
     * @param visited Set of functions already in the current chain (for cycle detection).
     * @return The final non-redirected target.
     */
    std::shared_ptr<function> resolve_redirect_chain(function& fn, std::unordered_set<function*>& visited);

    /**
     * Resolve an enumeration's entries, base, underlying type and enum_type.
     * Handles recursive resolution for derived enums whose base may not yet be resolved.
     * @param en The enumeration to resolve.
     */
    void resolve_enumeration(enumeration& en);
};

} // namespace k::model::gen

#endif //KLANG_RESOLVERS_SYMBOL_HPP

