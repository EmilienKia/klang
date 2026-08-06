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

#ifndef KLANG_RESOLVERS_TYPE_REF_HPP
#define KLANG_RESOLVERS_TYPE_REF_HPP

#include "resolvers_common.hpp"

#include <unordered_map>
#include <tuple>

namespace k::model::gen {

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
     * Stack of try-catch scopes: each entry lists the exception types caught at that level.
     * Used by the exception contract checker to determine whether a thrown/propagated
     * exception type is handled by an enclosing try-catch.
     */
    struct try_catch_scope {
        /** Types caught by the catch clauses at this try-catch level. */
        std::vector<std::shared_ptr<type>> caught_types;
    };
    std::vector<try_catch_scope> _try_catch_stack;

    /**
     * Stack of catch clauses currently being visited.
     * Used to validate that bare 'throw;' (rethrow) only appears inside a catch body.
     * Each entry holds the caught type for contract checking.
     */
    struct catch_scope {
        std::shared_ptr<type> caught_type;
    };
    std::vector<catch_scope> _catch_clause_stack;

    /**
     * Keeps callable_type objects alive for the duration of type resolution.
     * A frt created in visit_symbol_expression is a temporary shared_ptr; the only
     * strong reference to it is through fn_ref_type->reference (the cached ref_type).
     * If fn_ref_type goes out of scope, the reference_type's weak_ptr<subtype> expires
     * and any later is_resolved() call crashes.  Storing the frt here prevents that.
     */
    std::vector<std::shared_ptr<type>> _ephemeral_types;

    /**
     * Pending expression replacement — set by visit_function_invocation_expression
     * when a function call is rewritten into a temporary_construction_expression.
     * Callers that visit child expressions must check this after each accept() and
     * replace the child if non-null.
     */
    std::shared_ptr<expression> _replacement_expr;

    /**
     * Usage-site bindings captured while resolving generic aggregate type references.
     * Key: declaration element that carries the concrete generic type (variable, parameter, ...).
     */
    std::unordered_map<const variable_definition*, tpl_info::generic_usage_descriptor> _generic_usage_by_site;

    /**
     * Guard set of instantiated aggregates already processed by
     * resolve_instantiated_aggregate(), to keep that pass idempotent and to
     * break recursion on self-referential instantiations.
     */
    std::unordered_set<aggregate*> _resolved_instantiations;

public:

    type_reference_resolver(k::log::logger& logger, std::shared_ptr<context> context, unit& unit) :
    k::log::logger_relay(logger),
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
     * Resolve an unresolved_callable_type to a concrete callable_type
     * or member_function_reference_type.  The context_elem is used for scope lookup.
     */
    std::shared_ptr<type> resolve_callable_type(
        const std::shared_ptr<unresolved_callable_type>& ufrt,
        const element& context_elem);

    /**
     * Resolve an unresolved_type to a concrete type using the standard 4-step
     * fallback chain:  resolve_type_by_name → from_string → imported_aggregate → imported_enum.
     *
     * If the inner type is already resolved, delegates to context::resolve_type.
     *
     * @param inner          The type to resolve (may be an unresolved_type or already resolved).
     * @param scope_elem     The element used as starting scope for name-based lookups (may be nullptr).
     * @return The resolved type, or nullptr/unresolved if resolution failed.
     */
    std::shared_ptr<type> resolve_inner_type(
        const std::shared_ptr<type>& inner,
        const element* scope_elem);

    /**
     * Try to resolve an unresolved_type that carries AST template arguments
     * (e.g. Box<int>) by finding the template definition, converting the AST
     * args to model template_argument values, and triggering instantiation.
     *
     * Returns the struct_type of the concrete instantiated aggregate, or
     * nullptr if the base name is not a known template.
     */
    std::shared_ptr<type> try_instantiate_template_type(
        const std::shared_ptr<unresolved_type>& unres,
        const element& context_elem);

    /**
     * Resolve a use of a parameterised alias, e.g. 'Vec<int>' declared as
     * 'template<typename T> alias Vec : Array<T, 16>;'.
     *
     * A parameterised alias is never instantiated into an entity of its own:
     * the arguments are substituted into the target type, which is then
     * resolved as usual. A soft alias yields the substituted type directly; a
     * strong one yields a nominal alias_type, one per distinct argument list.
     *
     * Returns nullptr when the base name is not a parameterised alias, so the
     * caller can fall through to the regular template-instantiation path.
     */
    std::shared_ptr<type> try_resolve_alias_template(
        const std::shared_ptr<unresolved_type>& unres,
        const element& context_elem);

    /**
     * If @p ret_type is (or wraps, through a single owner/pointer/reference/link/
     * view/drain indirection, plus optional const) an unresolved template type
     * carrying template arguments, instantiate that inner type and rebuild the
     * wrapper. Returns the rebuilt concrete type, or nullptr if nothing to do.
     * Handles e.g. Sequence<T>::constIterator() : ConstIterator<T>! whose
     * substituted return type is owner<unresolved ConstIterator<int>>.
     */
    std::shared_ptr<type> instantiate_wrapped_return_type(
        const std::shared_ptr<type>& ret_type,
        const element& context_elem);

    /**
     * Recursively resolve a type chain, triggering template instantiation
     * for any inner unresolved_type that carries template arguments.
     *
     * Walks through wrapper types (pointer, reference, owner, link, view,
     * drain, const, array), resolves the leaf unresolved_type via
     * resolve_inner_type (which can trigger try_instantiate_template_type),
     * then rebuilds the wrapper chain with the resolved inner type.
     *
     * @param t           The type to resolve (may be a wrapper or leaf).
     * @param scope_elem  The element used as starting scope for name lookups.
     * @return The resolved type, or the original type if resolution failed.
     */
    std::shared_ptr<type> resolve_type_chain(
        const std::shared_ptr<type>& t,
        const element* scope_elem);

    /**
     * Post-instantiation: resolve all internal types of a freshly instantiated
     * template aggregate so that code generation can proceed.
     *
     * Steps:
     *   1. Resolve member variable types (transitive template instantiation).
     *   2. Resolve function signature types (parameters + return types).
     *   3. Visit the aggregate (expressions in method bodies).
     *
     * Called from try_instantiate_template_type after the struct_type and
     * LLVM struct body are created.
     */
    void resolve_instantiated_aggregate(aggregate& agg);

    /** Return captured generic usage bindings for a declaration site, if any. */
    const tpl_info::generic_usage_descriptor* find_generic_usage_for_site(const variable_definition* site) const;

    /** Infer generic usage bindings from a receiver expression (symbol/member/unary wrapper cases). */
    const tpl_info::generic_usage_descriptor* find_generic_usage_for_receiver(const std::shared_ptr<expression>& receiver) const;

    /**
     * Compute a concrete return type for generic member calls using receiver bindings.
     * Falls back to nullptr when no generic mapping can be inferred.
     */
    std::shared_ptr<type> resolve_generic_call_return_type(
        const function& called_func,
        const std::shared_ptr<expression>& receiver_expr);

    /**
     * Strip the spurious reference(array(T)) → array(T) layer that resolve_type
     * adds for unsized arrays.  Indirection wrappers (pointer, link, view, owner)
     * should NOT contain the reference layer.
     */
    static std::shared_ptr<type> strip_ref_array(const std::shared_ptr<type>& t);

    /**
     * Look up a single member function by name on 'agg' (including inherited
     * members) and return its return type. Used by the foreach ITERATOR/SEQUENCE
     * resolver to determine the hidden helper variables' types (e.g. the return
     * type of 'next()'/'iterator()'/'constIterator()') without needing to resolve
     * a synthesized call expression first. Throws an internal error if the method
     * cannot be found (defensive: the libk interfaces this is used for always
     * declare exactly the expected methods).
     */
    std::shared_ptr<type> lookup_method_return_type(
        const std::shared_ptr<aggregate>& agg,
        const std::string& method_name,
        const lex::opt_any_lexeme& lexeme);

    // ── Strong alias (typedef) conversion checking ─────────────────────────────

    /**
     * Position at which an implicit conversion towards a strong alias occurs.
     * Determines whether an untainted base-typed expression is rejected or only
     * reported: an assignment can be enforced at compile time, whereas an
     * argument or a return value cannot — the symbol is mangled with the
     * canonical type, so the linker would accept the base-typed call anyway.
     */
    enum class alias_conv_site { ASSIGNMENT, ARGUMENT, RETURN };

    /**
     * Check an implicit conversion of @p expr towards @p target when @p target
     * denotes a strong alias (typedef).
     *
     * Accepted without any diagnostic when the expression is a compile-time
     * literal or when it is "tainted" by the alias — that is, when it derives
     * from an operand of that very alias type through operations that preserve
     * the type (an expression is a sufficient locality to drop the cast).
     * Otherwise an error (ASSIGNMENT) or a warning (ARGUMENT, RETURN) is issued.
     */
    void check_strong_alias_conversion(const std::shared_ptr<expression>& expr,
                                       const std::shared_ptr<type>& target,
                                       alias_conv_site site,
                                       const lex::opt_any_lexeme& lexeme);

    /**
     * Apply check_strong_alias_conversion() at ARGUMENT position for every
     * argument of a resolved call.
     */
    /**
     * Eagerly resolve every alias declared by a scope, so that unused aliases
     * are still fully typed when the KDI is exported.
     */
    void materialize_aliases(const alias_holder& holder, element& scope);

    void check_typedef_arguments(function& fn,
                                 const std::vector<std::shared_ptr<expression>>& args,
                                 const lex::opt_any_lexeme& lexeme);

    [[noreturn]] void throw_error(unsigned int code, const lex::lexeme& lexeme, const std::string& message, const std::vector<std::string>& args = {}) {        auto diag = k::log::diagnostic::make_error(code, message, args);
        logger_relay::report(diag);
        throw resolution_error(std::move(diag));
    }

    [[noreturn]] void throw_error(unsigned int code, const lex::opt_any_lexeme& lexeme, const std::string& message, const std::vector<std::string>& args = {}) {
        auto diag = k::log::diagnostic::make_error(code, message, args);
        if (lexeme) diag.at(*lexeme);
        logger_relay::report(diag);
        throw resolution_error(std::move(diag));
    }


    // ── Exception contract checking helpers ────────────────────────────────────

    /**
     * Extract the underlying struct aggregate from a type, peeling pointer/reference/const wrappers.
     * Returns nullptr if the inner type is not a struct_type with an aggregate.
     */
    static std::shared_ptr<aggregate> get_exception_aggregate(const std::shared_ptr<type>& t);

    /**
     * Check whether an exception type is covered by a list of declared types.
     * A type is covered if it is the same aggregate or derives from one of the declared types.
     */
    static bool is_exception_type_covered(const std::shared_ptr<aggregate>& thrown_agg,
                                          const std::vector<std::shared_ptr<type>>& declared_types);

    /**
     * Check whether an exception type is caught by any enclosing try-catch scope.
     */
    bool is_exception_caught_by_try_catch(const std::shared_ptr<aggregate>& thrown_agg) const;

    /**
     * Validate that a thrown exception type is declared in the current function's throws clause
     * or caught by an enclosing try-catch. Emits ERR_THROW_UNDECLARED_EXCEPTION if not.
     */
    void check_throw_contract(const std::shared_ptr<type>& thrown_type, const lex::opt_any_lexeme& lexeme);

    /**
     * Validate that all exception types declared by a called function are handled
     * (either caught by enclosing try-catch or declared in the caller's throws clause).
     * Emits ERR_UNCAUGHT_EXCEPTION for each unhandled type.
     */
    void check_call_contract(const function& called_func, const lex::opt_any_lexeme& lexeme);

    void visit_unit(unit&) override;

    void visit_namespace(ns& ) override;
    void visit_aggregate(aggregate&) override;
    void visit_klass(klass&) override;
    void visit_interface(interface&) override;
    void visit_annotation_type(annotation_type&) override;
    void visit_variable_definition(variable_definition&);

    /**
     * Helper context passed to the per-type-category variable validation helpers.
     * Bundles the common state needed by all validation paths so that each helper
     * has a clean, uniform signature.
     */
    struct var_init_context {
        variable_definition& var;
        const lex::opt_any_lexeme& var_lexeme;
        std::shared_ptr<type> var_type;
        std::shared_ptr<expression> init_expr_base;
        std::shared_ptr<constructor_invocation_expression> init_expr;

        /** Get the single init argument regardless of storage form. */
        std::shared_ptr<expression> get_single_init_arg() const;
        /** True if there is a single init argument. */
        bool has_single_init_arg() const;
        /** Assign a new expression as the single init argument. */
        void assign_single_init_arg(std::shared_ptr<expression> new_arg);
    };

    /** Resolve the unresolved type of a variable (phase 1 of visit_variable_definition). */
    void resolve_variable_type(variable_definition& var, const lex::opt_any_lexeme& var_lexeme);

    /** Validate init expression for a primitive-typed variable. */
    void validate_primitive_variable(var_init_context& ctx);

    /** Validate init expression for a struct-typed variable (constructor resolution). */
    void validate_struct_variable(var_init_context& ctx);

    /** Validate init expression for a reference-typed variable. */
    void validate_reference_variable(var_init_context& ctx);

    /** Validate init expression for a pointer-typed variable. */
    void validate_pointer_variable(var_init_context& ctx);

    /** Validate init expression for a link-typed variable. */
    void validate_link_variable(var_init_context& ctx);

    /** Validate init expression for a view-typed variable. */
    void validate_view_variable(var_init_context& ctx);

    /** Validate init expression for an owner-typed variable. */
    void validate_owner_variable(var_init_context& ctx);

    /** Validate init expression for a sized-array-typed variable. */
    void validate_sized_array_variable(var_init_context& ctx);

    /**
     * Extract the pointed/linked/viewed/owned sub-type from any indirection type,
     * after stripping a reference wrapper.  Returns nullptr if the type is not an
     * indirection or owner.
     */
    static std::shared_ptr<type> extract_indirection_subtype(const std::shared_ptr<type>& arg_type);

    /**
     * Check whether two types (after const-stripping) are compatible considering
     * unsized array element const widening (e.g. array<T> matches array<const<T>>).
     */
    static bool types_match_array_const_compatible(const std::shared_ptr<type>& src_nc, const std::shared_ptr<type>& tgt_nc);

    /**
     * Check inheritance-based type compatibility and insert cast expressions
     * for upcast/downcast when necessary.
     * @return true if types are compatible (equal or cast inserted), false if incompatible.
     */
    bool check_and_insert_inheritance_cast(
        const std::shared_ptr<type>& src_sub_nc,
        const std::shared_ptr<type>& tgt_sub_nc,
        const std::shared_ptr<expression>& arg,
        const std::shared_ptr<type>& target_type,
        std::function<void(std::shared_ptr<expression>)> assign_arg,
        bool null_is_fatal);

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
    void visit_break_statement(break_statement&) override;
    void visit_continue_statement(continue_statement&) override;
    void visit_throw_statement(throw_statement&) override;
    void visit_try_catch_statement(try_catch_statement&) override;
    void visit_catch_clause(catch_clause&) override;
    void visit_if_else_statement(if_else_statement&) override;
    void visit_while_statement(while_statement&) override;
    void visit_for_statement(for_statement&) override;
    void visit_foreach_statement(foreach_statement&) override;
    void visit_expression_statement(expression_statement&) override;
    void visit_variable_statement(variable_statement&) override;

    void visit_value_expression(value_expression&) override;
    void visit_symbol_expression(symbol_expression&) override;
    void visit_unary_expression(unary_expression&) override;
    void visit_binary_expression(binary_expression&) override;
    void visit_conditional_expression(conditional_expression&) override;

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

    void visit_spaceship_expression(spaceship_expression&) override;

    void visit_subscript_expression(subscript_expression&) override;
    void visit_function_invocation_expression(function_invocation_expression &) override;
    void visit_constructor_invocation_expression(constructor_invocation_expression &) override;
    void visit_temporary_construction_expression(temporary_construction_expression &) override;
    void visit_new_expression(new_expression &) override;
    void visit_delete_expression(delete_expression &) override;
    void visit_callable_bind_expression(callable_bind_expression &) override;
    void visit_callable_invocation_expression(callable_invocation_expression &) override;
    void visit_owner_move_expression(owner_move_expression &) override;
    void visit_array_init_expression(array_init_expression &) override;
    void visit_designated_struct_init_expression(designated_struct_init_expression &) override;

    void visit_cast_expression(cast_expression&) override;

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
        CAST_VARARGS_PACK = 5,
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

    // ── adapt_type per-category helpers (defined in gen_adapt_type.cpp) ──────

    /** Adapt function reference types (frt → frt, ref<frt> → frt, etc.). Returns nullptr if not a frt case. */
    std::shared_ptr<expression> adapt_callable_type(std::shared_ptr<expression> expr, const std::shared_ptr<type>& type_src, const std::shared_ptr<type>& type_nc);
    /** Adapt when source is a pointer type (ptr<T> → ptr/lnk/ref). */
    std::shared_ptr<expression> adapt_from_pointer(std::shared_ptr<expression> expr, const std::shared_ptr<type>& type_src, const std::shared_ptr<type>& type_nc);
    /** Adapt when source is a link type (lnk<T> → lnk/ptr/view/ref). */
    std::shared_ptr<expression> adapt_from_link(std::shared_ptr<expression> expr, const std::shared_ptr<type>& type_src, const std::shared_ptr<type>& type_nc);
    /** Adapt when source is a view type (view<T> → view/ptr/ref). */
    std::shared_ptr<expression> adapt_from_view(std::shared_ptr<expression> expr, const std::shared_ptr<type>& type_src, const std::shared_ptr<type>& type_nc);
    /** Adapt when source is an owner type (owner<T> → owner/ptr/lnk/view/ref). */
    std::shared_ptr<expression> adapt_from_owner(std::shared_ptr<expression> expr, const std::shared_ptr<type>& type_src, const std::shared_ptr<type>& type_nc);
    /** Adapt when source is a drain type (drain<T> → drain/ref/lnk/view/ptr/value). */
    std::shared_ptr<expression> adapt_from_drain(std::shared_ptr<expression> expr, const std::shared_ptr<type>& type_src, const std::shared_ptr<type>& type_nc);
    /** Adapt ref<owner<T>> to owner/ptr/lnk/view/ref (owner borrow and move patterns). */
    std::shared_ptr<expression> adapt_from_ref_owner(std::shared_ptr<expression> expr, const std::shared_ptr<type>& type_src, const std::shared_ptr<type>& type_nc);
    /** Adapt when source is a reference type (ref<T> → ref/value/lnk/view/owner + loads and casts). */
    std::shared_ptr<expression> adapt_from_reference(std::shared_ptr<expression> expr, const std::shared_ptr<type>& type_src, const std::shared_ptr<type>& type_nc, const std::shared_ptr<type>& type_orig);
    /** Adapt enum conversions (enum ↔ enum, enum ↔ primitive). Returns nullptr if not an enum case. */
    std::shared_ptr<expression> adapt_enum_type(std::shared_ptr<expression> expr, const std::shared_ptr<type>& type_nc);
    /** Adapt primitive-to-primitive or struct identity. Terminal fallback. */
    std::shared_ptr<expression> adapt_primitive_or_struct_type(std::shared_ptr<expression> expr, const std::shared_ptr<type>& type_nc);

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
     * Resolve a binary operator overload for an explicitly-named operator, rather than the
     * operator naturally matching `expr`'s dynamic type. Shared core behind
     * resolve_binary_operator_overload(); also used by resolve_comparison_with_fallback to
     * probe for alternative source operators (e.g. probing `>=` while resolving a `<`
     * expression) that can synthesize the operator actually written in source.
     * @param expr          The expression node (used for diagnostics only).
     * @param op_name       Canonical operator function name to search for (e.g. "__operator_eq_").
     * @param left_agg      The aggregate type used as the member-lookup receiver ("this" side).
     * @param left_expr     The expression bound to the receiver ("this") / first non-member param.
     * @param right_expr    The expression bound to the sole member param / second non-member param.
     * @param is_const_this True if left_expr is a const object (only const member operators are viable).
     * @return {best_func, adapted_right, best_score} or {nullptr, nullptr, CAST_IMPOSSIBLE} if no
     *         viable match exists.
     */
    std::tuple<std::shared_ptr<function>, std::shared_ptr<expression>, cast_weight>
    resolve_named_binary_operator_overload(
        const binary_expression& expr,
        const std::string& op_name,
        const std::shared_ptr<aggregate>& left_agg,
        const std::shared_ptr<expression>& left_expr,
        const std::shared_ptr<expression>& right_expr,
        bool is_const_this = false);

    /**
     * Result of resolve_comparison_with_fallback(): describes the source operator function
     * to call and how to combine its result(s) to produce the wanted comparison operator.
     * See k::model::cmp_synthesis for the full set of synthesis kinds and their semantics.
     */
    struct comparison_fallback_result {
        /** The single source operator function to call (used once, or twice for COMPOSITE_*). */
        std::shared_ptr<function> func;
        /** How to combine call(s) to `func` to produce the wanted comparison result. */
        cmp_synthesis synthesis = cmp_synthesis::DIRECT;
        /** Only meaningful for COMPOSITE_AND/COMPOSITE_OR; see cmp_synthesis docs. */
        bool composite_negate_terms = false;
        /**
         * Adapted "argument"-role operand for the primary (or only) call, or nullptr if no
         * adaptation is needed. For DIRECT/NEGATE this replaces the right operand; for
         * SWAP/SWAP_NEGATE this replaces the left operand. Always nullptr for COMPOSITE_*
         * (composite candidates require an exact type match on both sides, see cmp_synthesis).
         */
        std::shared_ptr<expression> adapted_arg;
        /**
         * Phase 2 (aggregate `operator <=>` return type): only meaningful for
         * SPACESHIP/SPACESHIP_SWAP when `func`'s return type is an aggregate rather than a
         * primitive int/float. Names the resolved, bool-returning comparison operator used
         * to compare that aggregate result against the integer literal 0 (DIRECT match only
         * — see try_resolve_spaceship_zero_comparison()). Null => `func`'s return type is a
         * primitive int/float and the ordinary sign-test-against-zero codegen applies.
         */
        std::shared_ptr<function> spaceship_zero_func;
        /** Numeric primitive type used for the integer-zero argument of `spaceship_zero_func`. */
        std::shared_ptr<type> spaceship_zero_arg_type;
    };

    /**
     * Phase 2 (aggregate `operator <=>` return type): when a resolved `operator <=>`
     * returns an aggregate rather than a primitive signed int/float, attempts to resolve a
     * direct, bool-returning comparison of that aggregate against the integer literal `0`,
     * probing `wanted_op_name` (the caller passes the already swap-adjusted name for
     * SPACESHIP_SWAP). This is intentionally restricted to a single DIRECT match: no
     * NEGATE/SWAP/SWAP_NEGATE/COMPOSITE synthesis and no recursion into a nested
     * `operator <=>` declared on the result type itself — see
     * doc/spec/language/functions/operators.md §9 for the documented scope of this
     * restriction. The candidate's parameter (once adapted) must be a non-reference numeric
     * primitive (int/long/short/byte/float/double), so the codegen side can build a plain
     * zero constant of that type.
     *
     * @param expr           The outer comparison expression (used for diagnostics only).
     * @param result_type    The (non-primitive) declared return type of the resolved
     *                        `operator <=>`.
     * @param wanted_op_name Canonical comparison operator name to probe (e.g. "__operator_lt_").
     * @param scope_host     An already-resolved expression whose parent chain provides the
     *                        enclosing scope for non-member operator lookup. Borrowed only
     *                        for scope traversal — its own type/value is never used.
     * @return {func, adapted_zero_arg_type} if a viable bool-returning candidate is found,
     *         else std::nullopt.
     */
    std::optional<std::pair<std::shared_ptr<function>, std::shared_ptr<type>>>
    try_resolve_spaceship_zero_comparison(
        const comparison_expression& expr,
        const std::shared_ptr<type>& result_type,
        const std::string& wanted_op_name,
        const std::shared_ptr<expression>& scope_host);

    /**
     * Resolve the comparison operator actually written in `expr` (==, !=, <, >, <=, >=)
     * against aggregate operand(s), trying — in strict priority order — the exact operator
     * first, then progressively more complex fallback syntheses from other declared
     * comparison operators (see k::model::cmp_synthesis), before giving up.
     *
     * Priority: candidates are ranked by ascending (cast_weight, tier) pairs, i.e. type
     * relaxation is the primary criterion (exact-typed candidates always beat any candidate
     * requiring an implicit conversion, regardless of synthesis complexity) and synthesis
     * tier (0=exact .. 4=composite) only breaks ties at equal cast_weight. Ties at equal
     * (cast_weight, tier) are broken by a fixed enumeration order (exact, negate, swap,
     * swap+negate, then composite over <, >, <=, >= in that order) rather than raising an
     * ambiguity error.
     *
     * @param expr           The comparison expression node (used to determine the wanted
     *                        operator and for diagnostics).
     * @param left_agg       Aggregate type of the left operand (required; this mirrors the
     *                        pre-existing restriction that operator-overload resolution for
     *                        comparisons only triggers when the left operand is an aggregate).
     * @param right_agg      Aggregate type of the right operand, or nullptr if the right
     *                        operand is not an aggregate (SWAP/SWAP_NEGATE/COMPOSITE tiers,
     *                        which use the right operand as receiver, are skipped in that case).
     * @param left_expr      The left operand expression.
     * @param right_expr     The right operand expression.
     * @param is_const_left  True if the left operand is a const object.
     * @param is_const_right True if the right operand is a const object.
     * @return The resolved result, or std::nullopt if no viable candidate exists at any tier.
     */
    std::optional<comparison_fallback_result> resolve_comparison_with_fallback(
        const comparison_expression& expr,
        const std::shared_ptr<aggregate>& left_agg,
        const std::shared_ptr<aggregate>& right_agg,
        const std::shared_ptr<expression>& left_expr,
        const std::shared_ptr<expression>& right_expr,
        bool is_const_left,
        bool is_const_right);

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

} // namespace k::model::gen

#endif //KLANG_RESOLVERS_TYPE_REF_HPP
