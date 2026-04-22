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

} // namespace k::model::gen

#endif //KLANG_RESOLVERS_SCOPE_LOOKUP_HPP

