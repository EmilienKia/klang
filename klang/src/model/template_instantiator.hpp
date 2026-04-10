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

#ifndef KLANG_TEMPLATE_INSTANTIATOR_HPP
#define KLANG_TEMPLATE_INSTANTIATOR_HPP

#include "template.hpp"
#include "model.hpp"
#include "type.hpp"
#include "../common/logger.hpp"

#include <memory>
#include <string>
#include <vector>

namespace k::model {

/**
 * Builds a canonical key string from a list of template arguments.
 * Used to look up existing instantiations in tpl_info::instantiations.
 *
 * Format: "<arg1,arg2,...>" where each arg is either the type's display
 * name (for type arguments) or the integer value (for value arguments).
 */
std::string build_instantiation_key(const std::vector<template_argument>& args);

/**
 * Builds a human-readable instantiation name from the base template name
 * and the list of template arguments.
 *
 * Example: "Box" + [int] -> "Box__int"
 *          "Pair" + [int, float] -> "Pair__int_float"
 *          "Array" + [int, 10] -> "Array__int_10"
 */
std::string build_instantiated_name(const std::string& base_name,
                                     const std::vector<template_argument>& args);

/**
 * Returns a display name for a type, suitable for use in instantiation
 * keys and instantiated names.
 */
std::string type_display_name(const std::shared_ptr<type>& t);

/**
 * Template instantiator: operates at the model level.
 *
 * Instead of cloning and rewriting AST, the instantiator walks the
 * template's model members (variables, functions, body statements and
 * expressions) and recreates them in a new concrete entity with all
 * template parameter types substituted by the concrete argument types.
 *
 * Usage (for aggregates):
 *   auto concrete_agg = template_instantiator::instantiate_aggregate(
 *       tpl_def, args, parent_ns, unit, ctx, logger);
 *
 * Usage (for functions):
 *   auto concrete_fn = template_instantiator::instantiate_function(
 *       tpl_def, args, parent_ns, unit, ctx, logger);
 */
class template_instantiator {
public:
    /**
     * Instantiate a template aggregate with concrete arguments.
     *
     * @param tpl_def     The template aggregate definition (must be is_template()).
     * @param args        Concrete template arguments.
     * @param parent_ns   The namespace containing the template definition.
     * @param unit        The compilation unit.
     * @param ctx         The model context.
     * @param logger      Logger for diagnostics.
     * @return The concrete (non-template) aggregate, or nullptr on failure.
     */
    static std::shared_ptr<aggregate> instantiate_aggregate(
        aggregate& tpl_def,
        const std::vector<template_argument>& args,
        std::shared_ptr<ns> parent_ns,
        k::model::unit& unit,
        std::shared_ptr<context> ctx,
        k::log::logger& logger);

    /**
     * Instantiate a template function with concrete arguments.
     *
     * @param tpl_def     The template function definition (must be is_template()).
     * @param args        Concrete template arguments.
     * @param parent_ns   The namespace containing the template definition.
     * @param unit        The compilation unit.
     * @param ctx         The model context.
     * @param logger      Logger for diagnostics.
     * @return The concrete (non-template) function, or nullptr on failure.
     */
    static std::shared_ptr<function> instantiate_function(
        function& tpl_def,
        const std::vector<template_argument>& args,
        std::shared_ptr<ns> parent_ns,
        k::model::unit& unit,
        std::shared_ptr<context> ctx,
        k::log::logger& logger);

private:
    /**
     * Build the type substitution map from template params and concrete args.
     */
    static type_substitution_map build_substitution_map(
        const tpl_info& ti,
        const std::vector<template_argument>& args);

    /**
     * Clone a member variable from a template aggregate into a concrete aggregate,
     * substituting types.
     */
    static void clone_member_variable(
        const member_variable_definition& src,
        std::shared_ptr<aggregate> target,
        const type_substitution_map& subst);

    /**
     * Clone a function (method) from a template aggregate into a concrete aggregate,
     * substituting types in parameters, return type, and body expressions.
     */
    static void clone_method(
        const function& src,
        std::shared_ptr<aggregate> target,
        const type_substitution_map& subst);

    /**
     * Clone a constructor from a template aggregate into a concrete aggregate.
     */
    static void clone_constructor(
        const constructor& src,
        std::shared_ptr<aggregate> target,
        const type_substitution_map& subst);

    /**
     * Clone a destructor from a template aggregate into a concrete aggregate.
     */
    static void clone_destructor(
        const destructor& src,
        std::shared_ptr<aggregate> target,
        const type_substitution_map& subst);

    /**
     * Populate a function's parameters and body from a template source.
     */
    static void populate_function_from_template(
        std::shared_ptr<function> dst,
        const function& src,
        const type_substitution_map& subst);

    /**
     * Clone the contents of a block (statements) from source to destination,
     * substituting types in all expressions.
     */
    static void clone_block_contents(
        const block& src,
        std::shared_ptr<block> dst,
        const type_substitution_map& subst);

    /**
     * Clone a single statement, substituting types.
     * @param parent_stmt  The parent statement (block) for the new statement.
     */
    static std::shared_ptr<statement> clone_statement(
        const statement& src,
        std::shared_ptr<statement> parent_stmt,
        const type_substitution_map& subst);

    /**
     * Clone an expression and substitute all types within it.
     */
    static std::shared_ptr<expression> clone_and_substitute_expr(
        const std::shared_ptr<expression>& src,
        const type_substitution_map& subst);

    /**
     * Walk a cloned expression tree and substitute types in-place.
     */
    static void substitute_expr_types(
        std::shared_ptr<expression> expr,
        const type_substitution_map& subst);

    /**
     * Retarget the _constructed_symbol in a cloned init expression to point
     * to the new (concrete) variable instead of the original template variable.
     *
     * Handles constructor_invocation_expression, designated_struct_init_expression,
     * and array_init_expression — all of which carry a _constructed_symbol set by
     * model_builder targeting the original variable definition.
     */
    static void retarget_init_expr(
        const std::shared_ptr<expression>& init_expr,
        const std::shared_ptr<variable_definition>& new_var);
};

} // namespace k::model

#endif // KLANG_TEMPLATE_INSTANTIATOR_HPP


