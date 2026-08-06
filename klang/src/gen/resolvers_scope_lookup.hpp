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

#ifndef KLANG_RESOLVERS_SCOPE_LOOKUP_HPP
#define KLANG_RESOLVERS_SCOPE_LOOKUP_HPP

#include "resolvers_common.hpp"

namespace k::model::gen {

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
     * Fully general aggregate lookup: like lookup_structure(), but additionally
     * handles namespace-qualified names (e.g. "k::Object") and falls back to
     * materialising an imported aggregate (via unit::get_or_create_imported_aggregate)
     * when the name is not found among locally-declared aggregates.
     *
     * This is the resolution strategy any base-class / type name lookup needs as
     * soon as it may reference a qualified and/or imported type (as opposed to
     * lookup_structure(), which only walks the local scope chain by simple name).
     * Use this instead of lookup_structure() whenever the raw name may come from
     * KDI-imported modules or be namespace-qualified — e.g. resolving base_spec
     * raw names for diamond (virtual base) detection.
     */
    static std::shared_ptr<aggregate>
    lookup_structure_or_import(unit& u, const std::shared_ptr<context>& ctx,
                                std::shared_ptr<element> elem, const std::string& name);

    /**
     * Look up an enumeration by name, starting from elem and walking up the scope chain.
     */
    static std::shared_ptr<enumeration>
    lookup_enumeration(std::shared_ptr<element> elem, const std::string& name);

    /**
     * Look up an alias/typedef declaration by simple or qualified name, starting
     * from elem and walking up the scope chain.
     *
     * A simple name is searched in every alias_holder scope on the chain (block,
     * for statement, aggregate, namespace). A qualified name is resolved by
     * navigating namespaces and aggregates from each scope on the chain, and
     * finally from the root namespace.
     */
    static std::shared_ptr<alias_definition>
    lookup_alias(std::shared_ptr<const element> elem, const k::name& name);

    /**
     * Resolve the target type of an alias declaration and, for a strong alias
     * (typedef), build its nominal alias_type.
     *
     * The declared target type is resolved lazily and only once: an alias may be
     * declared before the type it renames, and may itself rename another alias.
     * @p resolve_by_name is the caller's own name-to-type resolution routine, so
     * that this helper can be shared by every resolution pass.
     *
     * Returns the type the alias denotes (the nominal alias_type for a typedef,
     * the renamed type itself for a soft alias), or nullptr if it cannot be
     * resolved yet or if the alias does not denote a type.
     * Sets @p cycle when an alias cycle is detected.
     */
    static std::shared_ptr<type>
    materialize_alias_type(const std::shared_ptr<alias_definition>& alias,
                           const std::shared_ptr<context>& ctx,
                           const std::function<std::shared_ptr<type>(const k::name&, const element&)>& resolve_by_name,
                           bool& cycle);

    /**
     * Resolve a use of a parameterised alias, e.g. 'Vec<int>' declared as
     * 'template<typename T> alias Vec : Array<T, 16>;'.
     *
     * A parameterised alias is never instantiated into an entity of its own:
     * the template arguments are substituted into the renamed type, which is
     * then resolved as usual. A soft alias yields the substituted type itself;
     * a strong one yields a nominal alias_type, one per distinct argument list.
     *
     * @p resolve_chain is the caller's own type resolution routine (it must
     * handle wrapper chains and nested template instantiations) and
     * @p report_error is the caller's own diagnostic thrower, so that this
     * helper can be shared by every resolution pass.
     *
     * Returns nullptr when @p alias is not parameterised, so that the caller
     * can fall through to the regular template-instantiation path.
     */
    static std::shared_ptr<type> resolve_alias_template(
        const std::shared_ptr<alias_definition>& alias,
        const std::shared_ptr<unresolved_type>& unres,
        const element& context_elem,
        const std::shared_ptr<context>& ctx,
        const std::function<std::shared_ptr<type>(const std::shared_ptr<type>&, const element&)>& resolve_chain,
        const std::function<void(unsigned int, const std::string&, const std::vector<std::string>&)>& report_error);

    /**
     * Look up a union_type_def by simple or qualified name, starting from elem
     * and walking up the scope chain.
     */
    static std::shared_ptr<union_type_def>
    lookup_union(std::shared_ptr<element> elem, const std::string& name);

    /**
     * Return true if candidate_base is an ancestor (direct or transitive) of
     * candidate_derived in the union inheritance chain, or if they are the same union.
     */
    static bool is_base_union_of(const union_type_def& candidate_base,
                                  const union_type_def& candidate_derived);

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

} // namespace k::model::gen

#endif //KLANG_RESOLVERS_SCOPE_LOOKUP_HPP

