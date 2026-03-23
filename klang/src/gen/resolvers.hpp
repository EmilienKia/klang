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
#include <unordered_map>
#include <unordered_set>

#include "../model/model.hpp"
#include "../model/model_visitor.hpp"
#include "../model/statements.hpp"

#include "../model/context.hpp"

#include "../common/logger.hpp"
#include "../lex/lexer.hpp"

namespace k::model::gen {


class resolution_error : public k::log::compiler_error {
public:
    explicit resolution_error(k::log::diagnostic diag)
        : k::log::compiler_error(std::move(diag)) {}
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
     * Look up an aggregate (structure or class) by name, starting from elem and walking up the scope chain.
     */
    static std::shared_ptr<aggregate>
    lookup_structure(std::shared_ptr<element> elem, const std::string& name);

    /**
     * Look up an enumeration by name, starting from elem and walking up the scope chain.
     */
    static std::shared_ptr<enumeration>
    lookup_enumeration(std::shared_ptr<element> elem, const std::string& name);

    //
    // Visibility helpers
    //

    /** Return the direct enclosing namespace of an element (skips functions/blocks/structs). */
    static std::shared_ptr<ns> enclosing_namespace(const element& elem);

    /** Return the root (outermost) namespace of an element. */
    static std::shared_ptr<ns> root_namespace(const element& elem);

    /**
     * True if access_site is inside a member function of st, or any of st's nested aggregate
     * ancestors (used so nested aggregate methods can access the parent aggregate's protected members).
     */
    static bool is_inside_member_function_of_or_ancestor(const element& access_site, const aggregate& st);

    /** True if access_site is textually inside owner_ns (same ns pointer or any descendant). */
    static bool is_in_same_namespace(const element& access_site, const ns& owner_ns);

    /** True if access_site's root namespace is the same object as owner_root. */
    static bool is_in_same_module(const element& access_site, const ns& owner_root);

    /**
     * Check whether a struct member (variable or function) with the given visibility is
     * accessible from the context described by function_stack.
     *
     * - PUBLIC    : always accessible.
     * - PRIVATE   : accessible only from member functions of owner_st itself
     *               (or a struct nested inside owner_st via get_enclosing_structure).
     * - PROTECTED : accessible from member functions of owner_st OR any struct that
     *               transitively derives from owner_st (is_derived_from).
     *
     * @param vis             Visibility of the member being accessed.
     * @param owner_st        The struct that declares the member.
     * @param owner_st_shared Shared pointer to owner_st (needed for is_derived_from).
     * @param function_stack  The resolver's current function call stack (innermost last).
     * @return true if the access is permitted.
     */
    static bool is_struct_member_accessible(
        visibility vis,
        const aggregate& owner_st,
        const std::shared_ptr<aggregate>& owner_st_shared,
        const std::vector<std::shared_ptr<function>>& function_stack);

    /**
     * Check whether the current access site (described by function_stack) is
     * a friend of the given aggregate.
     *
     * Friendship rules:
     *  - If the friend target is an aggregate, any direct member function (not
     *    inherited, not from nested aggregates) of that aggregate is a friend.
     *  - If the friend target is a function, only that exact function is a friend.
     *  - Friendship does NOT propagate through inheritance or nesting.
     *
     * @param owner_agg       The aggregate whose friend list is checked.
     * @param function_stack  The resolver's current function call stack (innermost last).
     * @param unit            The compilation unit (for name resolution).
     * @return true if the access site is a friend of owner_agg.
     */
    static bool is_friend_of(
        const aggregate& owner_agg,
        const std::vector<std::shared_ptr<function>>& function_stack,
        const unit& unit);

private:
    scope_lookup() = delete; // static-only utility class
};


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
    k::log::logger_relay(logger, 0x30000),
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

    /**
     * Base offset for internal-error codes.
     * Internal errors indicate compiler bugs that cannot be triggered by any valid
     * (or invalid) K source file.  Their diagnostic codes are 0xA000 + local_code,
     * which keeps them visually distinct from user-facing errors (0x0001 … 0x09FF).
     */
    static constexpr unsigned int INTERNAL_ERROR_BASE = 0xA000;

    [[noreturn]] void throw_error(unsigned int code, const lex::lexeme& lexeme, const std::string& message, const std::vector<std::string>& args = {}) {
        auto diag = k::log::diagnostic::make_error(with_flag(code), message, args);
        logger_relay::report(diag);
        throw resolution_error(std::move(diag));
    }

    [[noreturn]] void throw_error(unsigned int code, const lex::opt_any_lexeme& lexeme, const std::string& message, const std::vector<std::string>& args = {}) {
        auto diag = k::log::diagnostic::make_error(with_flag(code), message, args);
        if (lexeme) diag.at(*lexeme);
        logger_relay::report(diag);
        throw resolution_error(std::move(diag));
    }

    /** Throw an internal-compiler-error (should never be reachable via any K source input). */
    [[noreturn]] void throw_internal_error(unsigned int code, const lex::opt_any_lexeme& lexeme, const std::string& message, const std::vector<std::string>& args = {}) {
        throw_error(INTERNAL_ERROR_BASE + code, lexeme, message, args);
    }

protected:

    /**
     * Check if a variable (member or global) is accessible from the given access-site element.
     * Throws a resolution_error (code 0x3000E for namespace-level, 0x3000F for struct-level)
     * if the element is not accessible.
     * @param var   The variable definition being accessed.
     * @param access_site  The element from which the access occurs.
     */
    void check_variable_visibility(const variable_definition& var, const element& access_site);

    void visit_named_element(named_element&);

    void visit_unit(unit&) override;

    void visit_namespace(ns&) override;
    void visit_aggregate(aggregate&) override;
    void visit_klass(klass&) override;
    void visit_interface(interface&) override;
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


/**
 * Aggregate type resolver — Phase 1.a
 *
 * Resolves the types of all unit-level declarations: aggregates (structs, classes,
 * interfaces), functions (signatures only — no bodies), constructors, destructors,
 * and their parameters and member variables.
 *
 * It does NOT visit blocks, expressions, or statements. Its sole purpose is to
 * guarantee that, when type_reference_resolver runs, every aggregate and function
 * signature type is already resolved — including the LLVM vtable struct types for
 * polymorphic classes.
 *
 * Must be run AFTER symbol_resolver and BEFORE type_reference_resolver.
 */
class aggregate_type_resolver : public default_model_visitor, protected k::log::logger_relay {
protected:
    std::shared_ptr<context> _context;
    unit& _unit;

public:
    aggregate_type_resolver(k::log::logger& logger, std::shared_ptr<context> context, unit& unit)
        : k::log::logger_relay(logger, 0x50000),
          _context(context),
          _unit(unit) {}

    void resolve();

    // Type resolution helpers (public so they can be used by free helpers)
    std::shared_ptr<type> resolve_type_by_name(const k::name& type_name, const element& context_elem);
    static std::shared_ptr<aggregate> resolve_struct_from(const element& elem, const k::name& qualified_name);
    std::shared_ptr<type> resolve_type_from_root(const k::name& name_without_prefix);

protected:
    static constexpr unsigned int INTERNAL_ERROR_BASE = 0xA000;

    [[noreturn]] void throw_error(unsigned int code, const lex::opt_any_lexeme& lexeme,
                                  const std::string& message, const std::vector<std::string>& args = {}) {
        auto diag = k::log::diagnostic::make_error(with_flag(code), message, args);
        if (lexeme) diag.at(*lexeme);
        logger_relay::report(diag);
        throw resolution_error(std::move(diag));
    }

    [[noreturn]] void throw_internal_error(unsigned int code, const lex::opt_any_lexeme& lexeme,
                                           const std::string& message, const std::vector<std::string>& args = {}) {
        throw_error(INTERNAL_ERROR_BASE + code, lexeme, message, args);
    }

    void visit_unit(unit&) override;
    void visit_namespace(ns&) override;
    void visit_aggregate(aggregate&) override;
    void visit_klass(klass&) override;
    void visit_interface(interface&) override;
    void visit_member_variable_definition(member_variable_definition&) override;
    void visit_global_variable_definition(global_variable_definition&) override;
    void visit_parameter(parameter&) override;
    void visit_function(function&) override;
    void visit_constructor(constructor&) override;
    void visit_destructor(destructor&) override;
    void visit_static_constructor(static_constructor&) override;
    void visit_static_destructor(static_destructor&) override;
    void visit_global_constructor_function(global_constructor_function&) override;
    void visit_global_destructor_function(global_destructor_function&) override;
    void visit_global_main_function(global_main_function&) override;
};


/**
 * Model materializer — Phase 2
 *
 * Synthesizes and materializes all internal model information needed for virtual
 * dispatch and constructor/destructor variant generation.  Runs AFTER
 * aggregate_type_resolver (Phase 1.a) and BEFORE type_reference_resolver (Phase 1.b).
 *
 * Responsibilities:
 *  1. Validate vtable consistency: every non-abstract class must have all inherited
 *     abstract slots concretely implemented.
 *  2. Compute secondary-vtable thunk descriptors: for each non-primary base class
 *     with a vtable embedded at a non-zero byte offset in a derived class, compute
 *     the this-adjustment offset and build thunk_info records stored in
 *     vtable_layout::secondary_vtables.  No LLVM types are involved — only byte offsets
 *     computed from the LLVM DataLayout.
 *  3. Validate abstract function specifiers on interfaces and classes.
 *
 * All output is pure model data (no llvm::*** in model elements).
 * The generators (declaration_generator / implementation_generator) read these
 * pre-computed descriptors instead of recomputing them on the fly.
 *
 * Must be run AFTER aggregate_type_resolver and BEFORE type_reference_resolver.
 */
class model_materializer : public default_model_visitor, protected k::log::logger_relay {
protected:
    std::shared_ptr<context> _context;
    unit& _unit;

public:
    model_materializer(k::log::logger& logger, std::shared_ptr<context> context, unit& unit)
        : k::log::logger_relay(logger, 0x60000),
          _context(context),
          _unit(unit) {}

    void materialize();

protected:
    static constexpr unsigned int INTERNAL_ERROR_BASE = 0xB000;

    [[noreturn]] void throw_error(unsigned int code, const lex::opt_any_lexeme& lexeme,
                                  const std::string& message, const std::vector<std::string>& args = {}) {
        auto diag = k::log::diagnostic::make_error(with_flag(code), message, args);
        if (lexeme) diag.at(*lexeme);
        logger_relay::report(diag);
        throw resolution_error(std::move(diag));
    }

    [[noreturn]] void throw_internal_error(unsigned int code, const lex::opt_any_lexeme& lexeme,
                                           const std::string& message, const std::vector<std::string>& args = {}) {
        throw_error(INTERNAL_ERROR_BASE + code, lexeme, message, args);
    }

    void visit_unit(unit&) override;
    void visit_namespace(ns&) override;
    void visit_aggregate(aggregate&) override;
    void visit_klass(klass&) override;
    void visit_interface(interface&) override;

    /**
     * Validate vtable consistency for a class:
     * - All inherited abstract slots must be concretely implemented in non-abstract classes.
     * - All abstract methods in an abstract class have a vtable slot.
     * Returns false and emits an error if any inconsistency is found.
     */
    bool validate_vtable(klass& klass);

    /**
     * Compute secondary vtable thunk descriptors for a class with multiple class bases.
     * Populates vtable_layout::secondary_vtables with thunk_info records.
     * The byte offsets are computed from the LLVM StructLayout (requires that
     * context::init_module has NOT yet run; we use DataLayout from the target machine).
     * If no target machine is available, offsets are estimated from field indices.
     */
    void compute_secondary_vtable_specs(klass& klass);
};


/**
 * Signature resolver — pre-pass within type_reference_resolver
 *
 * Resolves function parameter and return types for all aggregates within a
 * namespace WITHOUT processing function bodies, expressions, or statements.
 *
 * This is used as a pre-pass inside type_reference_resolver::visit_namespace
 * to ensure that when function bodies reference types from sibling classes
 * (e.g. String's operator+ returning StringBuilder, where StringBuilder is
 * declared later), those constructor/function parameter types are already
 * resolved.
 *
 * Must be run AFTER symbol_resolver / aggregate_type_resolver / model_materializer
 * and BEFORE the full type_reference_resolver pass over function bodies.
 */
class signature_resolver : public default_model_visitor, protected k::log::logger_relay {
protected:
    std::shared_ptr<context> _context;
    unit& _unit;

public:
    signature_resolver(k::log::logger& logger, std::shared_ptr<context> context, unit& unit)
        : k::log::logger_relay(logger, 0x45000),
          _context(context),
          _unit(unit) {}

    /**
     * Pre-resolve all function signatures (parameter and return types) in the
     * aggregates of the given namespace.  Does NOT process function bodies.
     */
    void resolve_signatures(ns& ns);

protected:
    void visit_namespace(ns&) override;
    void visit_aggregate(aggregate&) override;
    void visit_klass(klass&) override;
    void visit_interface(interface&) override;
    void visit_function(function&) override;
    void visit_constructor(constructor&) override;
    void visit_destructor(destructor&) override;
    void visit_static_constructor(static_constructor&) override;
    void visit_static_destructor(static_destructor&) override;
    void visit_parameter(parameter&) override;
};


/**
 * Unit type resolver
 * This helper class will resolve all types usages, and particularly set types for expressions and variables.
 * It must be run after symbol resolution and before any code generation phase.
 */
class type_reference_resolver : public default_model_visitor, protected k::log::logger_relay {
protected:

    std::shared_ptr<context> _context;

    unit& _unit;

    /** Stack of functions currently being visited (for visibility access-site context). */
    std::vector<std::shared_ptr<function>> _function_stack;

    /**
     * Keeps function_reference_type objects alive for the duration of type resolution.
     * A frt created in visit_symbol_expression is a temporary shared_ptr; the only
     * strong reference to it is through fn_ref_type->reference (the cached ref_type).
     * If fn_ref_type goes out of scope, the reference_type's weak_ptr<subtype> expires
     * and any later is_resolved() call crashes.  Storing the frt here prevents that.
     */
    std::vector<std::shared_ptr<type>> _ephemeral_types;

public:

    type_reference_resolver(k::log::logger& logger, std::shared_ptr<context> context, unit& unit) :
    k::log::logger_relay(logger, 0x40000),
    _context(context),
    _unit(unit)  {
    }

    void resolve();

protected:

    /**
     * Resolve a struct type by qualified name, searching from elem upward.
     * Handles simple names (e.g. "rect"), qualified names (e.g. "shapes::rect"),
     * and root-prefixed names (e.g. "::shapes::rect" or "::module::shapes::rect").
     * All resolution logic stays in the resolver, not in the model.
     */
    std::shared_ptr<type> resolve_type_by_name(const k::name& type_name, const element& context_elem);

    /** Resolve a struct type from a given element, without climbing to parents. */
    static std::shared_ptr<aggregate> resolve_struct_from(const element& elem, const k::name& qualified_name);

    /** Resolve a struct type from the root namespace of the unit. */
    std::shared_ptr<type> resolve_type_from_root(const k::name& name_without_prefix);

    /**
     * Resolve an unresolved_function_ref_type to a concrete function_reference_type
     * or member_function_reference_type.  The context_elem is used for scope lookup.
     */
    std::shared_ptr<type> resolve_function_ref_type(
        const std::shared_ptr<unresolved_function_ref_type>& ufrt,
        const element& context_elem);

    static constexpr unsigned int INTERNAL_ERROR_BASE = 0xA000;

    [[noreturn]] void throw_error(unsigned int code, const lex::lexeme& lexeme, const std::string& message, const std::vector<std::string>& args = {}) {
        auto diag = k::log::diagnostic::make_error(with_flag(code), message, args);
        logger_relay::report(diag);
        throw resolution_error(std::move(diag));
    }

    [[noreturn]] void throw_error(unsigned int code, const lex::opt_any_lexeme& lexeme, const std::string& message, const std::vector<std::string>& args = {}) {
        auto diag = k::log::diagnostic::make_error(with_flag(code), message, args);
        if (lexeme) diag.at(*lexeme);
        logger_relay::report(diag);
        throw resolution_error(std::move(diag));
    }

    /** Throw an internal-compiler-error (should never be reachable via any K source input). */
    [[noreturn]] void throw_internal_error(unsigned int code, const lex::opt_any_lexeme& lexeme, const std::string& message, const std::vector<std::string>& args = {}) {
        throw_error(INTERNAL_ERROR_BASE + code, lexeme, message, args);
    }

    void visit_unit(unit&) override;

    void visit_namespace(ns& ) override;
    void visit_aggregate(aggregate&) override;
    void visit_klass(klass&) override;
    void visit_interface(interface&) override;
    void visit_variable_definition(variable_definition&);

    /**
     * Inject vptr fields into the LLVM struct type for a polymorphic class.
     * Records section first-slot indices in the vtable_layout.
     * Called after the LLVM struct type has been built by visit_klass.
     */
    void inject_vptr_fields(klass& st);
    void visit_member_variable_definition(member_variable_definition&) override;
    void visit_global_variable_definition(global_variable_definition&) override;
    void visit_parameter(parameter &) override;
    void visit_function(function&) override;
    void visit_constructor(constructor &) override;
    void visit_destructor(destructor&) override;
    void visit_static_constructor(static_constructor&) override;
    void visit_static_destructor(static_destructor&) override;
    void visit_global_constructor_function(global_constructor_function&) override;
    void visit_global_destructor_function(global_destructor_function&) override;
    void visit_global_main_function(global_main_function&) override;

    /**
     * Check if a function is accessible from the given access-site element.
     * For namespace-level functions: public = open, protected = same module, private = same namespace.
     * For struct member functions: public = open, protected/private = member functions of the same struct only.
     * Throws a resolution_error (code 0x002E for namespace-level, 0x002F for struct-level) if not accessible.
     * @param func         The function being accessed.
     * @param access_site  The element from which the access occurs.
     */
    void check_function_visibility(const function& func, const element& access_site);

    /**
     * Check if a constructor is accessible from the given access-site element.
     * Throws a resolution_error (code 0x0030) if not accessible.
     */
    void check_constructor_visibility(const constructor& ctor, const element& access_site);

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

    void visit_prefix_increment_expression(prefix_increment_expression&) override;
    void visit_prefix_decrement_expression(prefix_decrement_expression&) override;
    void visit_postfix_increment_expression(postfix_increment_expression&) override;
    void visit_postfix_decrement_expression(postfix_decrement_expression&) override;

    void visit_logical_binary_expression(logical_binary_expression&) override;
    void visit_logical_not_expression(logical_not_expression&) override;

    void visit_address_of_expression(address_of_expression&) override;
    void visit_drain_expression(drain_expression&) override;
    void visit_load_value_expression(load_value_expression&) override;
    void visit_dereference_expression(dereference_expression&) override;
//    void visit_member_of_expression(member_of_expression&) override;
    void visit_member_of_object_expression(member_of_object_expression&) override;
    void visit_member_of_pointer_expression(member_of_pointer_expression&) override;
    void visit_pm_expression(pm_expression&) override;

    void visit_comparison_expression(comparison_expression&) override;

    void visit_subscript_expression(subscript_expression&) override;
    void visit_function_invocation_expression(function_invocation_expression &) override;
    void visit_constructor_invocation_expression(constructor_invocation_expression &) override;
    void visit_new_expression(new_expression &) override;
    void visit_delete_expression(delete_expression &) override;
    void visit_owner_move_expression(owner_move_expression &) override;
    void visit_array_init_expression(array_init_expression &) override;
    void visit_designated_struct_init_expression(designated_struct_init_expression &) override;

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
     * Check all groups of same-named free functions in a function_holder for arity-overlap
     * collisions caused by default-parameter values.
     * Reports an error for every colliding pair found.
     */
    void check_overload_collisions(function_holder& fh);

    /**
     * Check all constructor overloads of an aggregate for arity-overlap collisions caused
     * by default-parameter values.
     * Reports an error for every colliding pair found.
     */
    void check_constructor_overload_collisions(aggregate& st);

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

    /**
     * Resolve a binary operator overload for an aggregate type, using cast-weight scoring
     * on the right operand to select the best match among multiple candidates.
     * Member operators are preferred over non-member when scores are equal.
     * For non-member operators, both left and right parameter compatibility are validated.
     * When is_const_this is true, only const member operators are considered.
     * @param expr          The binary expression node.
     * @param left_agg      The aggregate type of the left operand.
     * @param left_expr     The left operand expression (used for scope lookup and non-member left param scoring).
     * @param right_expr    The right operand expression.
     * @param is_const_this True if the left operand is a const object (only const member operators are viable).
     * @return {best_func, adapted_right} or {nullptr, nullptr} if no viable match.
     */
    std::pair<std::shared_ptr<function>, std::shared_ptr<expression>>
    resolve_binary_operator_overload(
        const binary_expression& expr,
        const std::shared_ptr<aggregate>& left_agg,
        const std::shared_ptr<expression>& left_expr,
        const std::shared_ptr<expression>& right_expr,
        bool is_const_this = false);

    /**
     * Resolve a unary operator overload for an aggregate type, using cast-weight scoring
     * to select the best match among multiple candidates.
     * Member operators are preferred over non-member when scores are equal.
     * For non-member operators, the operand parameter compatibility is validated.
     * When is_const_this is true, only const member operators are considered.
     * @param expr          The unary expression node.
     * @param operand_agg   The aggregate type of the operand.
     * @param operand_expr  The operand expression (used for scope lookup and non-member param scoring).
     * @param is_const_this True if the operand is a const object (only const member operators are viable).
     * @return The best matching function, or nullptr if no viable match.
     */
    std::shared_ptr<function>
    resolve_unary_operator_overload(
        const unary_expression& expr,
        const std::shared_ptr<aggregate>& operand_agg,
        const std::shared_ptr<expression>& operand_expr,
        bool is_const_this = false);

    /**
     * Resolve a casting operator overload for an aggregate type.
     * Looks for a member function named "operator_cast_<encoded_type>" matching the
     * target type of the cast.
     * @param source_agg    The aggregate type of the source expression.
     * @param target_type   The target type of the cast.
     * @param is_const_this True if the source is a const object.
     * @return The matching casting operator function, or nullptr if no viable match.
     */
    std::shared_ptr<function>
    resolve_cast_operator_overload(
        const std::shared_ptr<aggregate>& source_agg,
        const std::shared_ptr<type>& target_type,
        bool is_const_this = false);
};


/**
 * Global initialization/finalization order resolver.
 *
 * This pass runs AFTER type_reference_resolver has registered all global
 * variables and static constructors into the global_constructor_function.
 * It computes a single unified topological ordering over all "init items"
 * (static_constructors and global_variable_definitions) and stores it in
 * the global_constructor_function and global_destructor_function.
 *
 * ─── Dependency rules ────────────────────────────────────────────────────────
 *
 * For every static_constructor SC of struct S:
 *   1. Explicit deps from mem-init list:
 *      `static S() : A(), gvar() {}`
 *      → SC depends on:
 *        - static_constructor(A)  (if A is a known struct)
 *        - global_variable(gvar)  (if gvar is a known global/static var)
 *   2. Implicit: all static members of S (global_variable_definition whose
 *      owner is S) must be initialized AFTER SC (i.e. they depend on SC).
 *
 * For every global_variable_definition GV:
 *   3. If GV has a struct type T that has a static_constructor:
 *      → GV depends on static_constructor(T).
 *   4. For every symbol_expression E in the init-expression of GV that
 *      resolves to a global_variable_definition D:
 *      → GV depends on D.
 *   5. For every constructor_invocation_expression in the init-expression
 *      of GV that references a constructor of struct T:
 *      - if T has a static_constructor SC: → GV depends on SC.
 *      - for every global referenced inside that constructor's body: → GV
 *        depends on that global.
 *   6. For every function_invocation_expression used in the init-expression
 *      of GV: apply the same analysis to the callee's body.
 *
 * ─── Algorithm ───────────────────────────────────────────────────────────────
 *
 *   The resolver builds a directed graph Node → {dependencies}.
 *   A dependency edge  A → B  means "A must be initialized before B"
 *   (equivalently: B depends on A).
 *
 *   It then performs an iterative Kahn's algorithm (BFS topological sort):
 *     1. Compute in-degree for every node.
 *     2. Seed the queue with all nodes of in-degree 0.
 *     3. Repeatedly dequeue a node, append it to the result, and decrement
 *        in-degree of its successors; re-enqueue successors reaching 0.
 *     4. If the result does not contain all nodes, a cycle exists → error.
 *
 *   Construction order = topological order.
 *   Destruction order  = exact reverse.
 *
 * ─── Error reporting ─────────────────────────────────────────────────────────
 *
 *   - Cycle in dependency graph → resolution_error listing the cycle members.
 *   - Unknown name in mem-init list → resolution_error.
 */
class init_order_resolver : protected k::log::logger_relay {
protected:
    std::shared_ptr<context> _context;
    unit& _unit;

    static constexpr unsigned int LOG_BASE = 0x60000;
    static constexpr unsigned int INTERNAL_ERROR_BASE = 0xA000;

    [[noreturn]] void throw_error(unsigned int code,
                                  const std::string& message,
                                  const std::vector<std::string>& args = {}) {
        auto diag = k::log::diagnostic::make_error(with_flag(code), message, args);
        logger_relay::report(diag);
        throw resolution_error(std::move(diag));
    }

    [[noreturn]] void throw_internal_error(unsigned int code,
                                           const std::string& message,
                                           const std::vector<std::string>& args = {}) {
        throw_error(INTERNAL_ERROR_BASE + code, message, args);
    }

public:
    init_order_resolver(k::log::logger& logger, std::shared_ptr<context> context, unit& u)
        : k::log::logger_relay(logger, LOG_BASE), _context(context), _unit(u) {}

    /**
     * Run the resolver: compute the unified ordered init/finit sequence and
     * store it into the global_constructor_function and global_destructor_function.
     */
    void resolve();

private:
    /** An init node is either a static_constructor or a global_variable_definition. */
    using node_t = init_item; // alias for clarity

    /** Return a human-readable label for a node (for error messages). */
    static std::string node_label(const node_t& n);

    /**
     * Collect all global_variable_definition and global_variable_definition-typed
     * symbol references that appear anywhere inside an expression tree.
     */
    void collect_global_deps_from_expr(
        const std::shared_ptr<expression>& expr,
        std::vector<std::shared_ptr<global_variable_definition>>& out_globals,
        std::vector<std::shared_ptr<struct_type>>&               out_struct_types,
        std::unordered_set<const function*>&                     visited_funcs);

    /**
     * Collect dependencies of a global_variable_definition node:
     * - struct type's static_constructor (rule 3)
     * - globals referenced in init expression (rule 4)
     * - struct type of constructor args & callee bodies (rules 5–6)
     */
    void collect_deps_for_global(
        const std::shared_ptr<global_variable_definition>& gv,
        const std::unordered_map<const static_constructor*, size_t>& sctor_index,
        const std::unordered_map<const global_variable_definition*, size_t>& gv_index,
        std::vector<std::vector<size_t>>& adj,   // adj[from] → list of to
        size_t my_idx);

    /**
     * Collect dependencies of a static_constructor node:
     * - explicit deps from mem-init list, already resolved to concrete model elements
     *   by symbol_resolver::visit_static_constructor (rules 1–2).
     *   Reads static_dep_spec::resolved — no name lookup is performed here.
     * - implicit: static members of the owning struct depend ON this node
     *   (handled during global dep collection instead, to keep this function simple).
     */
    void collect_deps_for_sctor(
        const std::shared_ptr<static_constructor>& sctor,
        const std::unordered_map<const static_constructor*, size_t>& sctor_index,
        const std::unordered_map<const global_variable_definition*, size_t>& gv_index,
        std::vector<std::vector<size_t>>& adj,
        size_t my_idx);
};



} // k::model::gen

#endif //KLANG_RESOLVERS_HPP
