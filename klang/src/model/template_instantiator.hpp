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
 * Build usage-site bindings for a generic template from concrete arguments.
 */
tpl_info::generic_usage_descriptor build_generic_usage_descriptor(
    const tpl_info& ti,
    const std::vector<template_argument>& args);

/**
 * Record usage-site bindings keyed by build_instantiation_key(args).
 */
void record_generic_usage(
    tpl_info& ti,
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
     * Synthesize a generic aggregate exactly once.
     *
     * For declarations marked with the `generic` keyword, all type parameters
     * are substituted with a uniform opaque pointer model type (i8*), and the
     * resulting aggregate is cached under a dedicated synthesis key.
     */
    static std::shared_ptr<aggregate> synthesize_generic_aggregate(
        aggregate& tpl_def,
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

    /**
     * Instantiate a template union with concrete arguments.
     *
     * @param tpl_def     The template union definition (must be is_template()).
     * @param args        Concrete template arguments.
     * @param parent_ns   The namespace containing the template definition.
     * @param unit        The compilation unit.
     * @param ctx         The model context.
     * @param logger      Logger for diagnostics.
     * @return The concrete (non-template) union, or nullptr on failure.
     */
    static std::shared_ptr<union_type_def> instantiate_union(
        union_type_def& tpl_def,
        const std::vector<template_argument>& args,
        std::shared_ptr<ns> parent_ns,
        k::model::unit& unit,
        std::shared_ptr<context> ctx,
        k::log::logger& logger);

    /**
     * Resolve unresolved symbol_expression nodes in the method bodies of a
     * freshly instantiated concrete aggregate.
     *
     * The symbol_resolver skips template aggregates, so member-variable
     * references (e.g. bare 'x' for 'this.x') inside method bodies remain
     * unresolved after cloning.  This helper walks every method's body and
     * resolves each unresolved symbol by climbing the element parent chain
     * (block → function → aggregate) to find the matching variable or
     * parameter definition — exactly as the symbol_resolver would do if it
     * visited the concrete aggregate.
     *
     * Must be called after the concrete aggregate's children, parent chain,
     * and block contents are fully set up (i.e. after instantiate_aggregate).
     */
    static void resolve_body_symbols(std::shared_ptr<aggregate> concrete);

    /**
     * Inject member-initializer expressions into concrete constructors' blocks.
     *
     * The symbol_resolver::visit_constructor normally injects member_init
     * expressions as statements at the start of the constructor body.  But
     * template definitions are skipped by the symbol_resolver, and concrete
     * constructors are created after that pass.  This helper performs the
     * same injection for all constructors of a freshly instantiated aggregate.
     *
     * Must be called after instantiate_aggregate and resolve_body_symbols.
     */
    static void inject_constructor_member_inits(std::shared_ptr<aggregate> concrete);

    /**
     * Public wrappers for member template instantiation from gen code.
     * These delegate to the private static methods.
     */
    static type_substitution_map build_substitution_map_public(
        const tpl_info& ti,
        const std::vector<template_argument>& args) { return build_substitution_map(ti, args); }

    static value_substitution_map build_value_substitution_map_public(
        const tpl_info& ti,
        const std::vector<template_argument>& args) { return build_value_substitution_map(ti, args); }

    static pack_substitution_map build_pack_substitution_map_public(
        const tpl_info& ti,
        const std::vector<template_argument>& args) { return build_pack_substitution_map(ti, args); }

    static void populate_function_from_template_public(
        std::shared_ptr<function> dst,
        const function& src,
        const type_substitution_map& subst,
        const value_substitution_map& val_subst,
        const pack_substitution_map& pack_subst = {}) {
        populate_function_from_template(dst, src, subst, val_subst, pack_subst);
    }

private:
    /**
     * Build the type substitution map from template params and concrete args.
     */
    static type_substitution_map build_substitution_map(
        const tpl_info& ti,
        const std::vector<template_argument>& args);

    /**
     * Build the pack substitution map from template params and concrete args.
     * Maps pack parameter names to their list of concrete types.
     */
    static pack_substitution_map build_pack_substitution_map(
        const tpl_info& ti,
        const std::vector<template_argument>& args);

    /**
     * Build the value substitution map from template params and concrete args.
     * Maps value parameter names to their concrete integer values.
     */
    static value_substitution_map build_value_substitution_map(
        const tpl_info& ti,
        const std::vector<template_argument>& args);

    /**
     * Clone a member variable from a template aggregate into a concrete aggregate,
     * substituting types.
     */
    static void clone_member_variable(
        const member_variable_definition& src,
        std::shared_ptr<aggregate> target,
        const type_substitution_map& subst,
        const value_substitution_map& val_subst);

    /**
     * Clone a function (method) from a template aggregate into a concrete aggregate,
     * substituting types in parameters, return type, and body expressions.
     */
    static void clone_method(
        const function& src,
        std::shared_ptr<aggregate> target,
        const type_substitution_map& subst,
        const value_substitution_map& val_subst);

    /**
     * Clone a constructor from a template aggregate into a concrete aggregate.
     */
    static void clone_constructor(
        const constructor& src,
        std::shared_ptr<aggregate> target,
        const type_substitution_map& subst,
        const value_substitution_map& val_subst);

    /**
     * Clone a destructor from a template aggregate into a concrete aggregate.
     */
    static void clone_destructor(
        const destructor& src,
        std::shared_ptr<aggregate> target,
        const type_substitution_map& subst,
        const value_substitution_map& val_subst);

    /**
     * Populate a function's parameters and body from a template source.
     */
    static void populate_function_from_template(
        std::shared_ptr<function> dst,
        const function& src,
        const type_substitution_map& subst,
        const value_substitution_map& val_subst,
        const pack_substitution_map& pack_subst = {});

    /**
     * Clone the contents of a block (statements) from source to destination,
     * substituting types in all expressions.
     */
    static void clone_block_contents(
        const block& src,
        std::shared_ptr<block> dst,
        const type_substitution_map& subst,
        const value_substitution_map& val_subst);

    /**
     * Clone a single statement, substituting types.
     * @param parent_stmt  The parent statement (block) for the new statement.
     */
    static std::shared_ptr<statement> clone_statement(
        const statement& src,
        std::shared_ptr<statement> parent_stmt,
        const type_substitution_map& subst,
        const value_substitution_map& val_subst);

    /**
     * Clone an expression and substitute all types and value params within it.
     */
    static std::shared_ptr<expression> clone_and_substitute_expr(
        const std::shared_ptr<expression>& src,
        const type_substitution_map& subst,
        const value_substitution_map& val_subst);

    /**
     * Walk a cloned expression tree and substitute types in-place.
     */
    static void substitute_expr_types(
        std::shared_ptr<expression> expr,
        const type_substitution_map& subst);

    /**
     * Walk a cloned expression tree and replace symbol_expression nodes
     * that match value parameter names with concrete value_expressions.
     * Modifies the expression in-place (may replace the root via reference).
     */
    static void substitute_value_params(
        std::shared_ptr<expression>& expr,
        const value_substitution_map& val_subst);

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

    /**
     * Clone a nested aggregate (struct/class/interface) from a template aggregate
     * into a concrete aggregate, recursively cloning its children with type
     * substitution applied.
     *
     * This handles the case where a generic/template aggregate contains an inner
     * struct/class whose member types reference the outer template parameters
     * (e.g. a Node struct with a field of type TYPE!).
     */
    static void clone_nested_aggregate(
        const aggregate& src,
        std::shared_ptr<aggregate> target,
        const type_substitution_map& subst,
        const value_substitution_map& val_subst);

    /**
     * Walk all statements in a block and expand pack_expansion_expression
     * arguments in function/constructor invocations into concrete parameter
     * symbol references.
     *
     * @param blk                   The block to process.
     * @param pack_expansion_names  Map from original pack param name to the
     *                              list of generated concrete parameter names.
     */
    static void expand_pack_expressions_in_block(
        std::shared_ptr<block> blk,
        const std::unordered_map<std::string, std::vector<std::string>>& pack_expansion_names);
};

} // namespace k::model

#endif // KLANG_TEMPLATE_INSTANTIATOR_HPP


