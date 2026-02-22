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

#ifndef KLANG_RESOLVERS_HPP
#define KLANG_RESOLVERS_HPP

#include <limits>

#include "../model/model.hpp"
#include "../model/model_visitor.hpp"

#include "../model/context.hpp"

#include "../common/logger.hpp"
#include "../lex/lexer.hpp"

namespace k::model::gen {


class resolution_error : public std::runtime_error {
public:
    resolution_error(const std::string &arg);
    resolution_error(const char *string);
};


/**
 * Scope lookup utility: all symbol search algorithms with scope-chain traversal.
 * Kept entirely in the resolver layer; the model is unaware of resolution strategies.
 *
 * The three entry points are:
 *   - lookup_variable  : find a variable definition by name, walking up the scope chain.
 *   - lookup_function  : find the first function matching a name, walking up the scope chain.
 *   - lookup_functions : collect ALL overloads matching a name across the full scope chain.
 *   - lookup_structure : find a structure by name, walking up the scope chain.
 *
 * Each function accepts a std::shared_ptr<element> as the starting point and climbs the parent tree.
 */
class scope_lookup {
public:
    /**
     * Look up a variable by simple name, starting from elem and walking up the scope chain.
     * Checks variable_holder scopes (block, for_statement, ns, structure) and function parameters.
     */
    static std::shared_ptr<variable_definition>
    lookup_variable(std::shared_ptr<element> elem, const std::string& name);

    /**
     * Look up the first function matching name, starting from elem and walking up the scope chain.
     * Searches function_holder scopes (structure member functions, then enclosing namespaces).
     */
    static std::shared_ptr<function>
    lookup_function(std::shared_ptr<element> elem, const std::string& name);

    /**
     * Collect ALL functions (overloads) matching name, starting from elem and walking up the
     * full scope chain (structure members first, then all enclosing namespaces).
     */
    static std::vector<std::shared_ptr<function>>
    lookup_functions(std::shared_ptr<element> elem, const std::string& name);

    /**
     * Look up a structure by name, starting from elem and walking up the scope chain.
     */
    static std::shared_ptr<structure>
    lookup_structure(std::shared_ptr<element> elem, const std::string& name);

private:
    scope_lookup() = delete; // static-only utility class
};


/**
 * Unit symbol resolver
 * This helper class will resolve method and variable usages to their definitions.
 * It must be run after the model building phase and before type resolution and any code generation phase.
 */
class symbol_resolver : public default_model_visitor, protected k::lex::lexeme_logger {
protected:

    std::shared_ptr<context> _context;

    unit& _unit;

public:
    symbol_resolver(k::log::logger& logger, std::shared_ptr<context> context, unit& unit) :
    lexeme_logger(logger, 0x30000),
    _context(context),
    _unit(unit)  {
    }
    void resolve();

protected:

    static std::variant<std::monostate, std::shared_ptr<variable_definition>, std::shared_ptr<function>>
    resolve_symbol(const element& elem, const name& name);

    static std::variant<std::monostate, std::shared_ptr<variable_definition>, std::shared_ptr<function>>
    resolve_symbol(const symbol_expression& symbol) {
        return resolve_symbol(symbol, symbol.get_name());
    }

    [[noreturn]] void throw_error(unsigned int code, const lex::lexeme& lexeme, const std::string& message, const std::vector<std::string>& args = {}) {
        logger_relay::error(code, message, args, lexeme);
        throw resolution_error(message);
    }

    [[noreturn]] void throw_error(unsigned int code, const lex::opt_ref_any_lexeme& lexeme, const std::string& message, const std::vector<std::string>& args = {}) {
        error(code, lexeme, message, args);
        throw resolution_error(message);
    }

    void visit_named_element(named_element&);

    void visit_unit(unit&) override;

    void visit_namespace(ns&) override;
    void visit_structure(structure&) override;
    void visit_member_variable_definition(member_variable_definition&) override;
    void visit_global_variable_definition(global_variable_definition&) override;
    void visit_parameter(parameter &) override;
    void visit_function(function&) override;

    void visit_block(block&) override;
    void visit_return_statement(return_statement&) override;
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

    /**
     * Adapt a reference expression to load its value.
     * @param expr Reference expression.
     * @return The given arg if already not a reference or the newly loaded-value expr if is a reference.
     */
    std::shared_ptr<expression> adapt_reference_load_value(const std::shared_ptr<expression>& expr);

    /**
     * Adapt an expression to ensure it maps to a given type, by casting it.
     * @param expr Expression to map.
     * @param type Type to target
     * @return The given arg expression if already compatible, the new wrapping casting expr if mapping, nullptr if not possible.
     */
    std::shared_ptr<expression> adapt_type(std::shared_ptr<expression> expr, const std::shared_ptr<type>& type);
};


// TODO Add the type definition resolver here in the meantime


/**
 * Unit type resolver
 * This helper class will resolve all types usages, and particularly set types for expressions and variables.
 * It must be run after symbol resolution and before any code generation phase.
 */
class type_reference_resolver : public default_model_visitor, protected k::lex::lexeme_logger {
protected:

    std::shared_ptr<context> _context;

    unit& _unit;

public:

    type_reference_resolver(k::log::logger& logger, std::shared_ptr<context> context, unit& unit) :
    lexeme_logger(logger, 0x30000),
    _context(context),
    _unit(unit)  {
    }

    void resolve();

protected:

    /*
    static std::variant<std::monostate, std::shared_ptr<variable_definition>, std::shared_ptr<function>>
    resolve_symbol(const element& elem, const name& name);

    static std::variant<std::monostate, std::shared_ptr<variable_definition>, std::shared_ptr<function>>
    resolve_symbol(const symbol_expression& symbol) {
        return resolve_symbol(symbol, symbol.get_name());
    }
    */

    [[noreturn]] void throw_error(unsigned int code, const lex::lexeme& lexeme, const std::string& message, const std::vector<std::string>& args = {}) {
        logger_relay::error(code, message, args, lexeme);
        throw resolution_error(message);
    }

    [[noreturn]] void throw_error(unsigned int code, const lex::opt_ref_any_lexeme& lexeme, const std::string& message, const std::vector<std::string>& args = {}) {
        error(code, lexeme, message, args);
        throw resolution_error(message);
    }

    void visit_unit(unit&) override;

    void visit_namespace(ns&) override;
    void visit_structure(structure&) override;
    void visit_variable_definition(variable_definition&);
    void visit_member_variable_definition(member_variable_definition&) override;
    void visit_global_variable_definition(global_variable_definition&) override;
    void visit_parameter(parameter &) override;
    void visit_function(function&) override;
    void visit_constructor(constructor &) override;
    void visit_destructor(destructor&) override;
    void visit_global_constructor_function(global_constructor_function&) override;
    void visit_global_destructor_function(global_destructor_function&) override;
    void visit_global_main_function(global_main_function&) override;

    void visit_block(block&) override;
    void visit_return_statement(return_statement&) override;
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
    void visit_assignation_expression(assignation_expression &expression) override;
    void visit_arithmetic_assignation_expression(arithmetic_assignation_expression &expression) override;

    void visit_arithmetic_unary_expression(arithmetic_unary_expression&) override;

    void visit_logical_binary_expression(logical_binary_expression&) override;
    void visit_logical_not_expression(logical_not_expression&) override;

    void visit_address_of_expression(address_of_expression&) override;
    void visit_load_value_expression(load_value_expression&) override;
    void visit_dereference_expression(dereference_expression&) override;
//    void visit_member_of_expression(member_of_expression&) override;
    void visit_member_of_object_expression(member_of_object_expression&) override;
    void visit_member_of_pointer_expression(member_of_pointer_expression&) override;

    void visit_comparison_expression(comparison_expression&) override;

    void visit_subscript_expression(subscript_expression&) override;
    void visit_function_invocation_expression(function_invocation_expression &) override;
    void visit_constructor_invocation_expression(constructor_invocation_expression &) override;

    void visit_cast_expression(cast_expression&)override;

    /**
     * Cast weight values, representing the cost of an implicit conversion.
     * NONE      (0)          : no conversion needed, types are identical.
     * REF_CONV  (1)          : reference/pointer load (ref -> value).
     * WIDENING  (2)          : lossless primitive widening (e.g. short -> int).
     * NARROWING (3)          : lossy primitive narrowing (e.g. int -> short, possible overflow).
     * CONSTRUCT (4)          : construction of an intermediate object via a 1-arg constructor.
     * IMPOSSIBLE(UINT32_MAX) : conversion is not possible.
     */
    enum cast_weight : unsigned int {
        CAST_NONE      = 0,
        CAST_REF_CONV  = 1,
        CAST_WIDENING  = 2,
        CAST_NARROWING = 3,
        CAST_CONSTRUCT = 4,
        CAST_IMPOSSIBLE = std::numeric_limits<unsigned int>::max()
    };

    /**
     * Compute the cost (weight) of an implicit conversion from expr's type to target type,
     * without actually building any new expression node.
     * @param expr   Source expression (must have a resolved type).
     * @param type   Target type (must be resolved).
     * @return The cast_weight value for this conversion.
     */
    cast_weight compute_cast_weight(const std::shared_ptr<expression>& expr, const std::shared_ptr<type>& type);

    /**
     * Choose the best-matching constructor among a list of candidates given a set of arguments.
     * Scoring: score of a candidate = max cast_weight over all its parameters.
     * If no candidate has the right parameter count, emits a specific message.
     * If all arity-matching candidates have at least one impossible cast, lists them with details.
     * If multiple candidates share the same (lowest) score, emits an ambiguity error.
     * @param constructors  List of constructor candidates.
     * @param args          Argument expressions.
     * @return {best_constructor, adapted_args} or {nullptr, {}} on failure.
     */
    std::pair<std::shared_ptr<constructor>/*best_constructor*/, std::vector<std::shared_ptr<expression>>/*adapted_args*/>
    get_best_matching_constructor(const std::vector<std::shared_ptr<constructor>>& constructors, const std::vector<std::shared_ptr<expression>>& args);


    /**
     * Result of function overload resolution.
     * When 'is_unified_call' is true, the function is a free function called via unified call syntax,
     * and 'this_expr' contains the object expression that will be passed as the first argument.
     */
    struct FunctionCandidate {
        std::shared_ptr<function> func;
        std::vector<std::shared_ptr<expression>> adapted_args;
        /** If true, the match is via unified-call syntax (free fn with first param = ref to struct). */
        bool is_unified_call = false;
        /** The object expression used as 'this' when is_unified_call is true. */
        std::shared_ptr<expression> this_expr;
    };

    /**
     * Choose the best-matching function among a list of candidates given a set of arguments.
     * Supports both regular calls and unified-call syntax.
     * @param candidates     List of function candidates (member or free).
     * @param args           For Mode A/C: explicit args after 'this'. For Mode B: ignored if direct_args set.
     * @param this_expr      Optional object expression for member (Mode A) / unified (Mode C) calls.
     * @param direct_args    Optional full args for Mode B (free/static direct call). If null, uses args.
     *                       Pass full args (including obj) here to enable direct matching of free functions
     *                       alongside member/unified matching in the same scorer invocation.
     * @return FunctionCandidate with the best match, or {nullptr,...} on failure.
     */
    FunctionCandidate
    get_best_matching_function(const std::vector<std::shared_ptr<function>>& candidates,
                               const std::vector<std::shared_ptr<expression>>& args,
                               const std::shared_ptr<expression>& this_expr = nullptr,
                               const std::vector<std::shared_ptr<expression>>* direct_args = nullptr);


    /**
     * Adapt a reference expression to load its value.
     * @param expr Reference expression.
     * @return The given arg if already not a reference or the newly loaded-value expr if is a reference.
     */
    std::shared_ptr<expression> adapt_reference_load_value(const std::shared_ptr<expression>& expr);

    /**
     * Adapt an expression to ensure it maps to a given type, by casting it.
     * @param expr Expression to map.
     * @param type Type to target
     * @return The given arg expression if already compatible, the new wrapping casting expr if mapping, nullptr if not possible.
     */
    std::shared_ptr<expression> adapt_type(std::shared_ptr<expression> expr, const std::shared_ptr<type>& type);
};



} // k::model::gen

#endif //KLANG_RESOLVERS_HPP
