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
//
// Note: Last resolver log number: 0x40032 (type_reference_resolver), 0x30039 (symbol_resolver)
//
// How symbols are resolved:
// A symbol is resolved by trying to match a looked name from a searched context.
// The searched context is defined by the element from which the symbol is searched, and its ancestors or children.
// - If the name is qualified (e.g. has multiple parts like "A::B::C"), the resolver will try to match the first part of the name (e.g. "A") in the searched context,
// and then try to resolve the rest of the name (e.g. "B::C") from the matched element (e.g. "A").
// If the qualified name has a root prefix (e.g. "::A::B::C"), the resolver will try to match the first part of the name (e.g. "A") in the namespace of the unit directly,
// and then try to resolve the rest of the name (e.g. "B::C") from the matched element (e.g. "A").
// If not found in the unit, the resolver will try to match the name from the imported modules, based on the import statements of the unit,
// and then try to resolve the rest of the name (e.g. "C") from the matched imported module (e.g. "A::B").
// A special case could be to explicitly specify the root namespace of the current module to avoid ambiguity with imported modules,
// like "::my::module::A::B::C" to specify that the name must be resolved from the current module directly, and not from an imported module.
// - If the name is simple (e.g. has only one part like "A"), the resolver will try to match the name directly in the searched context,
// and then try to resolve the name from the matched element (e.g. "A") if found.
// If the name is not found in the searched context, the resolver will try to resolve the name from the parent element context,
// and then from the parent's parent element context, and so on until the name is resolved or there is no more parent element to search from.
//
// How implicit cast conversions are done:
// When look for a symbol, if the symbol is found but its type is not compatible with the expected type, the resolver will try to adapt the symbol to the expected type by applying implicit cast conversions.
// Or if many symbols sharing the same name are found, the resolver will try to adapt each symbol to the expected type by applying implicit cast conversions, and then select the best match based on the adapted types.
// Selection is done by comparing the adapted types with the expected type, and selecting the one with the best match.
// Matching is done following this order (most preferred to least preferred):
// - Exact match: The adapted type is the same as the expected type.
// - Ref conversion: The adapted type is a reference to the expected type, and the symbol can be loaded as a value (e.g. variable or function parameter).
// - Type widening: The adapted primitive type is a type that can be implicitly converted to the expected type without losing data (e.g. int to float).
// - Type narrowing: The adapted primitive type is a type that can be implicitly converted to the expected type but may lose data (e.g. float to int).
// - Construction: The adapted type can be implicitly constructed to the expected type, an explicit one-argument constructor exists.
// - No match: The adapted type cannot be converted to the expected type, or the symbol cannot be adapted to the expected type.
// Note: The resolver will not perform implicit conversions that may have side effects (e.g. user-defined conversion operators, or constructors with side effects),
//
// Unified calling syntax:
// In K language, the calling syntax is unified for functions, meaning that both member functions and free functions having a first parameter of type reference to the object can be called using the same syntaxes:
// - Member function syntax: obj.method(args...) can be used to call both member functions and free functions with a first parameter of type reference to the object, and the resolver will resolve the method symbol and adapt it to the expected type if necessary (e.g. by adding a "this" parameter for free functions).
// - Free function syntax: func(obj, args...) can be used to call both free functions and member functions, and the resolver will resolve the function symbol and adapt it to the expected type if necessary (e.g. by adding a "this" parameter for member functions).
// In consequence a static or global function with a first parameter of type reference to the object can be called using the member function syntax, and a member function can be called using the free function syntax, and the resolver will adapt the symbol to the expected type if necessary (e.g. by adding a "this" parameter for member functions).

#include "resolvers.hpp"

#include "../model/imported.hpp"
#include "../model/statements.hpp"
#include "../model/expressions.hpp"
#include "../model/template.hpp"
#include "../model/template_instantiator.hpp"
#include "../parse/ast.hpp"

#include <llvm/IR/DerivedTypes.h>

#include <queue>
#include <set>
#include <unordered_set>
#include <functional>
#include "../errors.hpp"

namespace k::model::gen {

// ═══════════════════════════════════════════════════════════════════════════
// Template value argument extraction helper
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Extract a concrete k::value_type from an AST expression node.
 * Only literal expressions are supported (compile-time constants).
 * Returns true on success, false if the expression is not a supported literal.
 */
static bool extract_value_from_ast_expr(
    const k::parse::ast::expression* expr,
    k::value_type& out_value)
{
    auto lit = dynamic_cast<const k::parse::ast::literal_expr*>(expr);
    if (!lit) return false;
    auto val = lit->literal.value().value();
    return std::visit([&out_value](auto&& v) -> bool {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::monostate> || std::is_same_v<T, std::nullptr_t>) {
            return false;
        } else {
            out_value = k::value_type{v};
            return true;
        }
    }, val);
}

//
// Visibility helpers — implemented as scope_lookup static methods
//

std::shared_ptr<ns> scope_lookup::enclosing_namespace(const element& elem) {
    auto cur = elem.shared_as<const element>();
    while (cur) {
        if (auto n = std::dynamic_pointer_cast<const ns>(cur))
            return std::const_pointer_cast<ns>(n);
        cur = cur->parent<element>();
    }
    return {};
}

std::shared_ptr<ns> scope_lookup::root_namespace(const element& elem) {
    auto cur = elem.shared_as<const element>();
    std::shared_ptr<ns> last_ns;
    while (cur) {
        if (auto n = std::dynamic_pointer_cast<const ns>(cur))
            last_ns = std::const_pointer_cast<ns>(n);
        cur = cur->parent<element>();
    }
    return last_ns;
}

bool scope_lookup::is_inside_member_function_of_or_ancestor(const element& access_site, const aggregate& st) {
    auto cur = access_site.shared_as<const element>();
    while (cur) {
        if (auto fn = std::dynamic_pointer_cast<const function>(cur)) {
            if (fn->is_member() && !fn->is_static()) {
                auto check_st = fn->get_owner();
                while (check_st) {
                    if (check_st.get() == &st) return true;
                    check_st = check_st->get_enclosing_aggregate();
                }
            }
        }
        cur = cur->parent<element>();
    }
    return false;
}

bool scope_lookup::is_in_same_namespace(const element& access_site, const ns& owner_ns) {
    auto cur = access_site.shared_as<const element>();
    while (cur) {
        if (auto n = std::dynamic_pointer_cast<const ns>(cur))
            if (n.get() == &owner_ns) return true;
        cur = cur->parent<element>();
    }
    return false;
}

bool scope_lookup::is_in_same_module(const element& access_site, const ns& owner_root) {
    auto access_root = root_namespace(access_site);
    return access_root && access_root.get() == &owner_root;
}

bool scope_lookup::is_struct_member_accessible(
    visibility vis,
    const aggregate& owner_st,
    const std::shared_ptr<aggregate>& owner_st_shared,
    const std::vector<std::shared_ptr<function>>& function_stack)
{
    if (vis == PUBLIC) return true;

    for (auto it = function_stack.rbegin(); it != function_stack.rend(); ++it) {
        const auto& fn = *it;
        if (!fn->is_member() || fn->is_static()) continue;

        auto check_st = fn->get_owner();
        while (check_st) {
            if (vis == PRIVATE) {
                if (check_st.get() == &owner_st) return true;
            } else {
                if (check_st.get() == &owner_st) return true;
                if (owner_st_shared && check_st->is_derived_from(owner_st_shared)) return true;
            }
            check_st = check_st->get_enclosing_aggregate();
        }
    }
    return false;
}

/**
 * Determine whether the current access site is a friend of the given aggregate.
 *
 * Steps:
 *   1. Resolve the friend target name from the unit root by walking namespaces/aggregates.
 *   2. Apply the type filter (struct/class/interface) if specified.
 *   3. If the target is an aggregate, check if the current function is a direct member.
 *   4. If the target is a function, check if the current function is that exact function.
 */
bool scope_lookup::is_friend_of(
    const aggregate& owner_agg,
    const std::vector<std::shared_ptr<function>>& function_stack,
    const unit& unit)
{
    if (function_stack.empty()) return false;

    const auto& directives = owner_agg.get_friend_directives();
    if (directives.empty()) return false;

    // The innermost function on the stack is the current access site
    const auto& current_fn = function_stack.back();

    // Step 1: Resolve the friend target name from the unit root by walking namespaces/aggregates
    for (const auto& dir : directives) {
        // Resolve the friend target name from the unit root
        auto root = unit.get_root_namespace();
        if (!root) continue;

        // Resolve the target name step by step from root
        std::shared_ptr<const element> current = root;
        bool resolved = true;
        for (size_t i = 0; i < dir.target_name.size(); ++i) {
            const auto& part = dir.target_name[i];
            bool stepped = false;

            // Try child namespace
            if (auto nspc = std::dynamic_pointer_cast<const ns>(current)) {
                if (auto child = nspc->get_child_namespace(part)) {
                    current = child;
                    stepped = true;
                }
            }
            // Try aggregate
            if (!stepped) {
                if (auto ah = std::dynamic_pointer_cast<const aggregate_holder>(current)) {
                    if (auto agg = ah->get_aggregate(part)) {
                        current = std::dynamic_pointer_cast<const element>(agg);
                        stepped = true;
                    }
                }
            }
            // Try function (only if last component)
            if (!stepped && i == dir.target_name.size() - 1) {
                if (auto fh = std::dynamic_pointer_cast<const function_holder>(current)) {
                    if (auto fn = fh->get_function(part)) {
                        current = std::dynamic_pointer_cast<const element>(fn);
                        stepped = true;
                    }
                }
            }
            if (!stepped) {
                resolved = false;
                break;
            }
        }

        if (!resolved || !current) continue;

        auto target = current;

        // Step 2: Apply the type filter (struct/class/interface) if specified
        // Check filter: if a type filter is specified, the target must match
        if (auto target_agg = std::dynamic_pointer_cast<const aggregate>(target)) {
            if (dir.filter != friend_directive::filter_t::NONE) {
                bool filter_match = false;
                if (dir.filter == friend_directive::filter_t::STRUCT) {
                    filter_match = (dynamic_cast<const structure*>(target.get()) != nullptr);
                } else if (dir.filter == friend_directive::filter_t::CLASS) {
                    filter_match = target_agg->is_class() && (dynamic_cast<const interface*>(target.get()) == nullptr);
                } else if (dir.filter == friend_directive::filter_t::INTERFACE) {
                    filter_match = (dynamic_cast<const interface*>(target.get()) != nullptr);
                }
                if (!filter_match) continue;
            }

            // Step 3: If the target is an aggregate, check if the current function is a direct member
            // Friend is an aggregate: check if current function is a DIRECT member
            // (not inherited, not from nested aggregates).
            auto fn_owner = current_fn->get_owner();
            if (fn_owner && fn_owner.get() == target_agg.get()) {
                return true;
            }
        } else if (auto target_fn = std::dynamic_pointer_cast<const function>(target)) {
            // Friend is a function: only that exact function is a friend
            if (dir.filter != friend_directive::filter_t::NONE) {
                // A type filter on a function target means no match
                continue;
            }
            if (current_fn.get() == target_fn.get()) {
                return true;
            }
        }
    }

    // Step 4: If the target is a function, check if the current function is that exact function
    return false;
}


//
// scope_lookup — all scope-chain resolution logic, isolated from the model
//

std::shared_ptr<variable_definition>
scope_lookup::lookup_variable(std::shared_ptr<element> elem, const std::string& name) {
    for (auto current = elem; current; current = current->parent<element>()) {
        if (auto vh = std::dynamic_pointer_cast<variable_holder>(current)) {
            if (auto var = vh->get_variable(name)) {
                return var;
            }
        }
        // Extra: the base block of a function exposes the function's parameters
        if (auto blck = std::dynamic_pointer_cast<block>(current)) {
            if (auto func = blck->get_direct_function()) {
                if (auto param = func->get_parameter(name)) {
                    return std::const_pointer_cast<parameter>(param);
                }
            }
        }
    }
    return {};
}

std::shared_ptr<function>
scope_lookup::lookup_function(std::shared_ptr<element> elem, const std::string& name) {
    for (auto current = elem; current; current = current->parent<element>()) {
        if (auto fh = std::dynamic_pointer_cast<function_holder>(current)) {
            if (auto func = fh->get_function(name)) {
                return func;
            }
        }
    }
    return {};
}

std::vector<std::shared_ptr<function>>
scope_lookup::lookup_functions(std::shared_ptr<element> elem, const std::string& name) {
    std::vector<std::shared_ptr<function>> result;
    for (auto current = elem; current; current = current->parent<element>()) {
        if (auto fh = std::dynamic_pointer_cast<function_holder>(current)) {
            auto local = fh->get_functions(name);
            result.insert(result.end(), local.begin(), local.end());
        }
    }
    return result;
}

std::shared_ptr<aggregate>
scope_lookup::lookup_structure(std::shared_ptr<element> elem, const std::string& name) {
    for (auto current = elem; current; current = current->parent<element>()) {
        if (auto sh = std::dynamic_pointer_cast<aggregate_holder>(current)) {
            if (auto agg = sh->get_aggregate(name)) {
                return agg;
            }
        }
    }
    return {};
}

std::shared_ptr<enumeration>
scope_lookup::lookup_enumeration(std::shared_ptr<element> elem, const std::string& name) {
    for (auto current = elem; current; current = current->parent<element>()) {
        if (auto eh = std::dynamic_pointer_cast<enum_holder>(current)) {
            if (auto en = eh->get_enum(name)) {
                return en;
            }
        }
    }
    return {};
}

//
// Symbol resolver
//

/**
 * Resolve a qualified name strictly descending from elem, without climbing to parents.
 * name must be non-empty and have no root prefix.
 */
std::variant<std::monostate, std::shared_ptr<variable_definition>, std::shared_ptr<function>>
symbol_resolver::resolve_qualified_from(const element& elem, const name& name) {
    if (name.empty()) return std::monostate{};

    if (name.size() == 1) {
        // Simple name: look in this element only (no parent walk)
        if (auto var_holder = dynamic_cast<const variable_holder*>(&elem)) {
            if (auto def = var_holder->get_variable(name)) {
                return def;
            }
        }
        if (auto func_holder = dynamic_cast<const function_holder*>(&elem)) {
            if (auto func = func_holder->get_function(name.to_string())) {
                return func;
            }
        }
        return std::monostate{};
    }

    // Qualified: first component selects namespace or struct, rest is resolved recursively
    const auto& first = name.front();
    const auto rest   = name.without_front();

    // Try child namespace
    if (auto nspc = dynamic_cast<const ns*>(&elem)) {
        if (auto child = nspc->get_child_namespace(first)) {
            auto res = resolve_qualified_from(*child, rest);
            if (res.index() != 0) return res;
        }
    }

    // Try aggregate (structure or class)
    if (auto st_holder = dynamic_cast<const aggregate_holder*>(&elem)) {
        if (auto agg = st_holder->get_aggregate(first)) {
            auto res = resolve_qualified_from(*agg, rest);
            if (res.index() != 0) return res;
        }
    }

    return std::monostate{};
}

/**
 * Resolve a name from the root namespace of the unit.
 * name must already have its root prefix stripped (call after without_root_prefix()).
 *
 * Strategy:
 *  1. If the first component matches the module name, enter that namespace and resolve
 *     the rest from there (explicit full-path: ::module::ns::func).
 *  2. Otherwise, resolve directly from the root namespace (omitted module prefix:
 *     ::func, ::struct::method, ::subns::func).
 */
std::variant<std::monostate, std::shared_ptr<variable_definition>, std::shared_ptr<function>>
/**
 * Resolve a name anchored at the root namespace of the unit.
 *
 * Steps:
 *   1. If first component matches the module name, enter root_ns and resolve the rest.
 *   2. Otherwise resolve directly from root_ns.
 *   3. Fallback: search imported functions and variables.
 *   4. Fallback: try imported aggregate static methods.
 */
symbol_resolver::resolve_symbol_from_root(const name& name) {
    if (name.empty()) return std::monostate{};

    auto root_ns = _unit.get_root_namespace();
    if (!root_ns) return std::monostate{};

    // Step 1: If first component matches the module name, enter root_ns and resolve the rest
    // Strategy 1: first component is the module/unit namespace name
    // The unit name may be multi-part (e.g. "the::test"), so only its last component
    // is the immediate child namespace of the root.  But since the root_namespace IS
    // the module namespace already (it IS the ns named by the module declaration),
    // we just try to enter it if name.front() == the last part of the unit name.
    const auto& unit_name = _unit.get_unit_name(); // k::name, e.g. "the::test"
    if (!unit_name.empty() && name.front() == unit_name.back()) {
        // The caller wrote ::module_last_part::...  — but actually root_ns IS that
        // namespace, so we continue resolving the rest from root_ns.
        auto rest = name.without_front();
        if (rest.empty()) {
            // ::module_name alone — doesn't resolve to a symbol
            return std::monostate{};
        }
        auto res = resolve_qualified_from(*root_ns, rest);
        if (res.index() != 0) return res;
        // Fall through to strategy 2 in case name.front() happens to collide with
        // a child namespace that has the same name as the module.
    }

    // Step 2: Otherwise resolve directly from root_ns
    // Strategy 2: resolve directly from root namespace (omit module prefix)
    auto local = resolve_qualified_from(*root_ns, name);
    if (local.index() != 0) return local;

    // Step 3: Fallback: search imported functions and variables
    // Strategy 3: fallback — search imported modules for a matching function or variable.
    if (auto* kdi_fn = _unit.find_imported_function(name)) {
        return _unit.get_or_create_imported_function(kdi_fn, _context);
    }
    if (auto* kdi_var = _unit.find_imported_variable(name)) {
        return _unit.get_or_create_imported_variable(kdi_var, _context);
    }

    // Step 4: Fallback: try imported aggregate static methods
    // Strategy 4: try to resolve a method inside an imported aggregate.
    if (name.size() >= 2) {
        auto agg_name = name.without_back();
        auto func_name = name.back();
        if (auto imp_agg = _unit.get_or_create_imported_aggregate(agg_name, _context)) {
            if (auto fn = imp_agg->get_function(func_name)) {
                return fn;
            }
        }
    }

    return std::monostate{};
}

void symbol_resolver::resolve()
{
    trace("[symbol_resolver::resolve] begin");
    visit_unit(_unit);

    // Resolve chained redirects: follow redirect targets transitively.
    trace("[symbol_resolver::resolve] resolving redirect chains");
    // After visit_unit, each redirected function has its immediate target set.
    // Now resolve chains: a -> b -> c becomes a -> c, b -> c.
    resolve_redirect_chains(_unit);
    trace("[symbol_resolver::resolve] done");
}

static void collect_all_functions_from_aggregate(aggregate& agg, std::vector<std::shared_ptr<function>>& out) {
    for (auto& fn : agg.functions()) {
        out.push_back(fn);
    }
    // Recurse into nested aggregates
    for (auto& [name, nested] : agg.aggregates()) {
        collect_all_functions_from_aggregate(*nested, out);
    }
}

static void collect_all_functions(const ns& nspc, std::vector<std::shared_ptr<function>>& out) {
    for (auto& child : nspc.get_children()) {
        if (auto fn = std::dynamic_pointer_cast<function>(child)) {
            out.push_back(fn);
        } else if (auto agg = std::dynamic_pointer_cast<aggregate>(child)) {
            collect_all_functions_from_aggregate(*agg, out);
        } else if (auto child_ns = std::dynamic_pointer_cast<ns>(child)) {
            collect_all_functions(*child_ns, out);
        }
    }
}

void symbol_resolver::resolve_redirect_chains(unit& unit) {
    // Collect all functions from the unit
    std::vector<std::shared_ptr<function>> all_functions;
    if (auto root = unit.get_root_namespace()) {
        collect_all_functions(*root, all_functions);
    }

    // For each redirected function, follow the chain to the final target
    for (auto& fn : all_functions) {
        if (fn->is_redirected() && fn->get_redirect_target()) {
            std::unordered_set<function*> visited;
            fn->set_redirect_target(resolve_redirect_chain(*fn, visited));
        }
    }
}

std::shared_ptr<function> symbol_resolver::resolve_redirect_chain(function& fn, std::unordered_set<function*>& visited) {
    if (visited.count(&fn)) {
        throw_error(static_cast<unsigned int>(k::diag::symbol_diag::ERR_REDIRECT_CHAIN_CYCLE), fn.get_ast_function_decl() ? lex::opt_any_lexeme{lex::any_lexeme{fn.get_ast_function_decl()->name}} : lex::opt_any_lexeme{},
            "Circular redirect chain detected involving function '{}'",
            {fn.get_short_name()});
    }
    visited.insert(&fn);

    auto target = fn.get_redirect_target();
    if (!target) {
        throw_error(static_cast<unsigned int>(k::diag::symbol_diag::ERR_REDIRECT_TARGET_NOT_FOUND), fn.get_ast_function_decl() ? lex::opt_any_lexeme{lex::any_lexeme{fn.get_ast_function_decl()->name}} : lex::opt_any_lexeme{},
            "Function redirector '{}' has no resolved target",
            {fn.get_short_name()});
    }

    // If the target is itself a redirect, follow the chain
    if (target->is_redirected()) {
        if (!target->get_redirect_target()) {
            throw_error(static_cast<unsigned int>(k::diag::symbol_diag::ERR_REDIRECT_AMBIGUOUS), fn.get_ast_function_decl() ? lex::opt_any_lexeme{lex::any_lexeme{fn.get_ast_function_decl()->name}} : lex::opt_any_lexeme{},
                "Function redirector '{}' targets '{}', which is itself a redirector with no resolved target",
                {fn.get_short_name(), target->get_short_name()});
        }
        return resolve_redirect_chain(*target, visited);
    }

    // Target is a concrete function — check it's not abstract or deleted
    if (target->is_abstract_func()) {
        throw_error(static_cast<unsigned int>(k::diag::symbol_diag::ERR_REDIRECT_SELF_REF), fn.get_ast_function_decl() ? lex::opt_any_lexeme{lex::any_lexeme{fn.get_ast_function_decl()->name}} : lex::opt_any_lexeme{},
            "Function redirector '{}' targets abstract function '{}', which has no implementation",
            {fn.get_short_name(), target->get_short_name()});
    }
    if (target->is_deleted()) {
        throw_error(static_cast<unsigned int>(k::diag::symbol_diag::ERR_REDIRECT_INCOMPATIBLE_SIG), fn.get_ast_function_decl() ? lex::opt_any_lexeme{lex::any_lexeme{fn.get_ast_function_decl()->name}} : lex::opt_any_lexeme{},
            "Function redirector '{}' targets deleted function '{}'",
            {fn.get_short_name(), target->get_short_name()});
    }

    return target;
}

std::variant<std::monostate, std::shared_ptr<variable_definition>, std::shared_ptr<function>> // TODO Add traversal direction flag
/**
 * Resolve a symbol (variable or function) from the given scope, climbing the parent chain.
 *
 * Steps:
 *   1. Handle 'this' keyword: find nearest non-static member function's this parameter.
 *   2. Root-prefixed names: delegate to resolve_symbol_from_root.
 *   3. Qualified names: search aggregates, enumerations, namespaces.
 *   4. Simple names: search variables, functions, inherited members (BFS), parameters.
 *   5. Check using directives at this scope level.
 *   6. Recurse to parent, or fall back to imported modules.
 */
symbol_resolver::resolve_symbol(const element& elem, const name& name) {
    debug("[symbol_resolver::resolve_symbol] resolving '{}'", {name.to_string()});

    // Step 1: Handle 'this' keyword: find nearest non-static member function's this parameter
    // Specifically look at the "this" symbol (non-static function specific parameter)
    if (name.size() == 1 && name.to_string() == "this") {
        auto func = elem.ancestor<function>();
        while (func) {
            if (func->is_member() && func->get_this_parameter()) {
                return std::const_pointer_cast<parameter>(func->get_this_parameter());
            }
            func = func->ancestor<function>();
        }
        throw_error(static_cast<unsigned int>(k::diag::symbol_diag::ERR_SYMBOL_NOT_FOUND), std::nullopt,
            "'this' can only be used inside a non-static member function");
    }

    // Step 2: Root-prefixed names: delegate to resolve_symbol_from_root
    if (name.has_root_prefix()) {
        return resolve_symbol_from_root(name.without_root_prefix());
    } else if (name.empty()) {
        // Invalid name, must have at least one part
        return std::monostate{};
    } else if(name.size() > 1) {
        // Qualified name

        // Look at aggregates (structures and classes)
        if (auto st_holder = dynamic_cast<const aggregate_holder*>(&elem)) {
            if (auto agg = st_holder->get_aggregate(name.front())) {
                if (auto res = resolve_symbol(*agg, name.without_front()); res.index()!=0) {
                    return res;
                }
            }
        }

        // Look at enumerations (e.g. MyEnum::entry)
        if (name.size() == 2) {
            if (auto eh = dynamic_cast<const enum_holder*>(&elem)) {
                if (auto en = eh->get_enum(name.front())) {
                    // Found an enum — but entry resolution happens in visit_symbol_expression
                    // We cannot return enum entries as variables or functions.
                    // Instead, we skip here and handle it in visit_symbol_expression directly.
                }
            }
        }

        // Step 3: Qualified names: search aggregates, enumerations, namespaces
        // Look at namespace
        if (auto nspc = dynamic_cast<const ns*>(&elem)) {
            if (auto child = nspc->get_child_namespace(name.front())) {
                if (auto res = resolve_symbol(*child, name.without_front()); res.index()!=0) {
                    return res;
                }
            }
        }

    } else /*(name.size() == 1)*/ {
        // Simple name, try to resolve it directly

        // Look at a variable
        if (auto var_holder = dynamic_cast<const variable_holder*>(&elem)) {
            if (auto def = var_holder->get_variable(name)) {
                return def;
            }
        }

        // Look at a function
        if (auto func_holder = dynamic_cast<const function_holder*>(&elem)) {
            if (auto func = func_holder->get_function(name.to_string())) {
                return func;
            }
        }

        // Step 4: Simple names: search variables, functions, inherited members (BFS), parameters
        // Look at inherited members from base classes (recursive BFS)
        if (auto agg = dynamic_cast<const aggregate*>(&elem)) {
            std::queue<std::shared_ptr<aggregate>> base_queue;
            for (auto& bs : agg->get_bases()) {
                if (bs.base) base_queue.push(bs.base);
            }
            while (!base_queue.empty()) {
                auto cur = base_queue.front();
                base_queue.pop();
                if (auto def = cur->get_variable(name.to_string())) {
                    return def;
                }
                if (auto func = cur->get_function(name.to_string())) {
                    return func;
                }
                for (auto& bs : cur->get_bases()) {
                    if (bs.base) base_queue.push(bs.base);
                }
            }
        }

        // TODO: Workaround, remove it when function will be a (parameter) variable_holder
        if (auto blck = dynamic_cast<const block*>(&elem)) {
            if (auto func = blck->get_direct_function()) {
                if (auto param = func->get_parameter(name.to_string())) {
                    return std::const_pointer_cast<parameter>(param);
                }
            }
        }
    }

    // Step 5: Check using directives at this scope level
    // Check using directives at this scope level (between direct members and parent scope)
    {
        auto using_result = resolve_via_using(elem, name);
        if (using_result.index() != 0) return using_result;
    }

    if (auto parent_elem = elem.parent<element>()) {
        // Try to find the symbol in the parent element context
        return resolve_symbol(*parent_elem, name);
    } else {
        // Reached the top of the scope chain without finding the symbol locally.
        // Last resort: search imported modules.
        if (auto* kdi_fn = _unit.find_imported_function(name)) {
            return _unit.get_or_create_imported_function(kdi_fn, _context);
        }
        if (auto* kdi_var = _unit.find_imported_variable(name)) {
            return _unit.get_or_create_imported_variable(kdi_var, _context);
        }

        // Step 6: Recurse to parent, or fall back to imported modules
        // Fallback: try to resolve a method (possibly static) inside an imported
        // aggregate.  For a name like "k::math::Math::abs", peel off the last
        // component as the function name and try to find the rest as an imported
        // aggregate, then look for the function within it.
        if (name.size() >= 2) {
            auto agg_name = name.without_back();
            auto func_name = name.back();
            if (auto imp_agg = _unit.get_or_create_imported_aggregate(agg_name, _context)) {
                if (auto fn = imp_agg->get_function(func_name)) {
                    return fn;
                }
            }
        }

        return std::monostate{};
    }
}

// ── Using directive resolution ────────────────────────────────────────────────

/**
 * Helper: resolve the model element targeted by a using directive's target_name.
 * Returns the element (as ns or aggregate) or nullptr if not found.
 */
static std::shared_ptr<const element>
resolve_using_target(const k::name& target_name, const unit& unit) {
    auto root = unit.get_root_namespace();
    if (!root) return nullptr;

    // Walk from root namespace, descending through child namespaces / aggregates
    std::shared_ptr<const element> current = root;
    for (size_t i = 0; i < target_name.size(); ++i) {
        const auto& part = target_name[i];

        // Try child namespace
        if (auto nspc = std::dynamic_pointer_cast<const ns>(current)) {
            if (auto child = nspc->get_child_namespace(part)) {
                current = child;
                continue;
            }
        }
        // Try aggregate
        if (auto ah = std::dynamic_pointer_cast<const aggregate_holder>(current)) {
            if (auto agg = ah->get_aggregate(part)) {
                current = std::dynamic_pointer_cast<const element>(agg);
                continue;
            }
        }
        // Not found at this level
        return nullptr;
    }
    return current;
}

std::variant<std::monostate, std::shared_ptr<variable_definition>, std::shared_ptr<function>>
/**
 * Resolve a symbol through the using directives of the given scope element.
 *
 * Handles three directive kinds:
 *   1. Anonymous namespace using: all members of the target are injected.
 *   2. Aliased namespace using: alias acts as a prefix for member access.
 *   3. Specific element using (with or without alias): only that element is accessible.
 *
 * Detects ambiguity when multiple directives match the same name.
 */
symbol_resolver::resolve_via_using(const element& elem, const name& name) {
    const using_holder* uh = dynamic_cast<const using_holder*>(&elem);
    if (!uh) return std::monostate{};

    const auto& directives = uh->get_using_directives();
    if (directives.empty()) return std::monostate{};

    // Collect all matches from using directives (for ambiguity detection)
    std::variant<std::monostate, std::shared_ptr<variable_definition>, std::shared_ptr<function>> result = std::monostate{};
    // Track which directive produced the first match (for error reporting)
    const using_directive* first_match_dir = nullptr;

    for (const auto& dir : directives) {
        std::variant<std::monostate, std::shared_ptr<variable_definition>, std::shared_ptr<function>> candidate = std::monostate{};

        if (dir.is_namespace() && !dir.has_alias()) {
            // Case 1: 'using namespace X::Y;' (anonymous)
            // All members of X::Y are virtually injected.
            auto target_elem = resolve_using_target(dir.target_name, _unit);
            if (!target_elem) {
                if (auto imp_agg = _unit.get_or_create_imported_aggregate(dir.target_name, _context)) {
                    target_elem = std::dynamic_pointer_cast<const element>(imp_agg);
                }
            }
            if (target_elem) {
                candidate = resolve_qualified_from(*target_elem, name);
            }

        } else if (dir.is_namespace() && dir.has_alias()) {
            // Case 2: 'using M = namespace X::Y;' (aliased namespace)
            // The namespace is accessible as a prefix: M::member
            if (name.front() == *dir.alias_name && name.size() > 1) {
                auto rest = name.without_front();
                auto target_elem = resolve_using_target(dir.target_name, _unit);
                if (!target_elem) {
                    if (auto imp_agg = _unit.get_or_create_imported_aggregate(dir.target_name, _context)) {
                        target_elem = std::dynamic_pointer_cast<const element>(imp_agg);
                    }
                }
                if (target_elem) {
                    candidate = resolve_qualified_from(*target_elem, rest);
                }
                // Fallback: construct fully-qualified name and search imported modules directly
                if (candidate.index() == 0) {
                    auto fq = dir.target_name;
                    for (size_t i = 0; i < rest.size(); ++i) fq = fq.with_back(rest[i]);
                    // Try imported function
                    if (auto* kdi_fn = _unit.find_imported_function(fq)) {
                        candidate = _unit.get_or_create_imported_function(kdi_fn, _context);
                    }
                    // Try imported variable
                    if (candidate.index() == 0) {
                        if (auto* kdi_var = _unit.find_imported_variable(fq)) {
                            candidate = _unit.get_or_create_imported_variable(kdi_var, _context);
                        }
                    }
                    // Try imported aggregate static method (rest has 2+ parts: Struct::method)
                    if (candidate.index() == 0 && rest.size() >= 2) {
                        auto agg_name = fq.without_back();
                        auto func_name = fq.back();
                        if (auto imp_agg = _unit.get_or_create_imported_aggregate(agg_name, _context)) {
                            if (auto fn = imp_agg->get_function(func_name)) {
                                candidate = fn;
                            }
                        }
                    }
                }
            }

        } else {
            // Cases 3 & 4: specific element using, with or without alias
            // 'using X::Y::foo;' or 'using Bar = X::Y::foo;'
            const std::string& real_name = dir.target_name.back();
            const std::string& lookup_name = dir.has_alias() ? *dir.alias_name : real_name;

            if (name.front() == lookup_name) {
                auto target_name_parent = dir.target_name.without_back();

                std::shared_ptr<const element> target_parent;
                if (target_name_parent.empty()) {
                    target_parent = _unit.get_root_namespace();
                } else {
                    target_parent = resolve_using_target(target_name_parent, _unit);
                    if (!target_parent) {
                        if (auto imp_agg = _unit.get_or_create_imported_aggregate(target_name_parent, _context)) {
                            target_parent = std::dynamic_pointer_cast<const element>(imp_agg);
                        }
                    }
                }

                if (target_parent) {
                    if (name.size() == 1) {
                        // Simple name match: resolve the real target symbol directly
                        candidate = resolve_qualified_from(*target_parent, k::name{real_name});
                    } else {
                        // Qualified name: matched first component (alias or real name), descend for the rest.
                        auto target_elem = resolve_qualified_from(*target_parent, k::name{real_name});
                        if (target_elem.index() != 0) {
                            // The target is a function or variable — cannot descend further
                        } else {
                            // Target might be an aggregate or namespace
                            if (auto ah = std::dynamic_pointer_cast<const aggregate_holder>(target_parent)) {
                                if (auto agg = ah->get_aggregate(real_name)) {
                                    candidate = resolve_qualified_from(*agg, name.without_front());
                                }
                            }
                            if (candidate.index() == 0) {
                                if (auto nspc = std::dynamic_pointer_cast<const ns>(target_parent)) {
                                    if (auto child = nspc->get_child_namespace(real_name)) {
                                        candidate = resolve_qualified_from(*child, name.without_front());
                                    }
                                }
                            }
                        }
                    }
                }

                // Fallback: try imported modules directly using the full target name
                if (candidate.index() == 0 && name.size() == 1) {
                    if (auto* kdi_fn = _unit.find_imported_function(dir.target_name)) {
                        candidate = _unit.get_or_create_imported_function(kdi_fn, _context);
                    }
                    if (candidate.index() == 0) {
                        if (auto* kdi_var = _unit.find_imported_variable(dir.target_name)) {
                            candidate = _unit.get_or_create_imported_variable(kdi_var, _context);
                        }
                    }
                }
            }
        }

        if (candidate.index() != 0) {
            if (result.index() != 0 && first_match_dir) {
                // Ambiguity: two different using directives matched the same name
                // For now, we just take the first match (TODO: proper ambiguity error)
                continue;
            }
            result = candidate;
            first_match_dir = &dir;
        }
    }

    return result;
}

/**
 * Check if a variable (member or global) is accessible from the current access site.
 *
 * Steps:
 *   1. Member variable: check struct member visibility and friend access.
 *   2. Global variable: check namespace visibility (protected = same module, private = same ns).
 *
 * Throws a resolution_error if the variable is not accessible.
 */
void symbol_resolver::check_variable_visibility(const variable_definition& var, const element& /*access_site*/) {
    // Step 1: Member variable: check struct member visibility and friend access
    // Member variable in a struct
    if (auto mv = dynamic_cast<const member_variable_definition*>(&var)) {
        auto owner_agg = std::const_pointer_cast<aggregate>(mv->parent<aggregate>());
        if (!owner_agg) return;
        auto vis = mv->get_visibility();
        if (vis == PUBLIC) return;
        if (scope_lookup::is_struct_member_accessible(vis, *owner_agg, owner_agg, _function_stack)) return;
        if (vis == PROTECTED && scope_lookup::is_friend_of(*owner_agg, _function_stack, _unit)) return;
        lex::opt_any_lexeme agg_lexeme;
        if (auto ast_ad = owner_agg->get_ast_aggregate_decl()) agg_lexeme = lex::any_lexeme{ast_ad->name};
        throw_error(static_cast<unsigned int>(k::diag::symbol_diag::ERR_AGGREGATE_VISIBILITY_DENIED), agg_lexeme,
            "{} member variable '{}' of struct '{}' is not accessible here; "
            "it can only be accessed from member functions of '{}'{}",
            {vis == PROTECTED ? "protected" : "private",
             mv->get_short_name(), owner_agg->get_short_name(), owner_agg->get_short_name(),
             vis == PROTECTED ? " or its subclasses or friends" : ""});
    }

    // Step 2: Global variable: check namespace visibility (protected = same module, private = same ns)
    // Global variable in a namespace
    if (auto gv = dynamic_cast<const global_variable_definition*>(&var)) {
        auto vis = gv->get_visibility();
        if (vis == PUBLIC) return;

        auto owner_ns = scope_lookup::enclosing_namespace(*gv);
        if (!owner_ns) return;

        // Determine the access site: the innermost function on the stack, or the var itself
        const element* site = gv;
        if (!_function_stack.empty()) site = _function_stack.back().get();

        if (vis == PROTECTED) {
            auto owner_root = scope_lookup::root_namespace(*owner_ns);
            if (!owner_root || scope_lookup::is_in_same_module(*site, *owner_root)) return;
            throw_error(static_cast<unsigned int>(k::diag::symbol_diag::ERR_VISIBILITY_ACCESS_DENIED), std::nullopt,
                "protected variable '{}' is only accessible within the same module; "
                "it is declared in module '{}' but accessed from outside",
                {gv->get_short_name(), owner_root->get_short_name()});
        } else { // PRIVATE
            if (scope_lookup::is_in_same_namespace(*site, *owner_ns)) return;
            throw_error(static_cast<unsigned int>(k::diag::symbol_diag::ERR_VISIBILITY_ACCESS_DENIED), std::nullopt,
                "private variable '{}' is only accessible within namespace '{}'; "
                "it cannot be accessed from outside that namespace",
                {gv->get_short_name(), owner_ns->get_short_name()});
        }
    }
}

std::shared_ptr<expression> symbol_resolver::adapt_reference_load_value(const std::shared_ptr<expression>& expr) {
    auto type = expr->get_type();

    if(!expr || !type::is_resolved(type)) {
        // Arguments must not be null, expr must have a type and this must be resolved.
        return nullptr;
    }

    if(type::is_reference(type)) {
        auto deref = load_value_expression::make_shared(expr);
        deref->set_type(type->get_subtype());
        return deref;
    } else {
        return expr;
    }
}


/**
 * Adapt an expression to match a target type by applying implicit casts (symbol_resolver version).
 *
 * Steps:
 *   1. Pointer source: check pointer-to-pointer compatibility only.
 *   2. Double reference: unwrap one level.
 *   3. Reference source: unwrap ref if inner type matches target.
 *   4. Primitive-to-primitive: insert a cast_expression.
 *
 * @return The expression if compatible, a cast expression, or nullptr if impossible.
 */
std::shared_ptr<expression> symbol_resolver::adapt_type(std::shared_ptr<expression> expr, const std::shared_ptr<type>& type) {
    if(!expr || !type::is_resolved(type) || !type::is_resolved(expr->get_type())) {
        // Arguments must not be null, expr must have a type and types (expr and target) must be resolved.
        return nullptr;
    }

    auto type_src = expr->get_type();

    // Step 1: Pointer source: check pointer-to-pointer compatibility only
    if(type::is_pointer(type_src)) {
        if(type::is_pointer(type)) {
            if (type == type_src) {
                // Pointers to same type, return the expression
                return expr;
            } else {
                // Pointers to different types
                // TODO verify casting
                return {};
            }
        } else {
            // Error : Source is a pointer, and asked to be cast to an object.
            return {};
        }
    }

    // Step 2: Double reference: unwrap one level
    if(type::is_double_reference(type_src)) {
        auto ref_src = std::dynamic_pointer_cast<reference_type>(type_src);
        auto deref = load_value_expression::make_shared(expr);
        deref->set_type(ref_src->get_subtype());
        expr = deref;
        type_src = ref_src->get_subtype();
    }

    // Step 3: Reference source: unwrap ref if inner type matches target
    if(type::is_reference(type_src)) {
        if(type::is_reference(type)) {
            if (type == type_src) {
                // Reference to same type, return the expression
                return expr;
            } else {
                // Reference to different types
                // TODO verify casting
                return {};
            }
        }
        auto ref_src = std::dynamic_pointer_cast<reference_type>(type_src);
        if(ref_src->get_subtype() == type) {
            return adapt_reference_load_value(expr);
        }
    }

    // Step 4: Primitive-to-primitive: insert a cast_expression
    auto prim_src = std::dynamic_pointer_cast<primitive_type>(expr->get_type());
    auto prim_tgt = std::dynamic_pointer_cast<primitive_type>(type);

    if(!prim_src || !prim_tgt) {
        // Support only primitive types for now.
        // TODO support not-primitive type casting
        return {};
    }

    if(prim_src && prim_tgt && *prim_src==*prim_tgt) {
        // Trivially agree for same types
        return expr;
    }

    auto cast = cast_expression::make_shared(expr, prim_tgt);
    cast->set_type(prim_tgt);
    return cast;
}

//
// Aggregate type resolver (Phase 1.a)
//

// ── Type resolution helpers (mirror of type_reference_resolver) ──────────────

std::shared_ptr<aggregate>
aggregate_type_resolver::resolve_struct_from(const element& elem, const k::name& qualified_name) {
    if (qualified_name.empty()) return {};

    if (qualified_name.size() == 1) {
        if (auto st_holder = dynamic_cast<const aggregate_holder*>(&elem)) {
            if (auto agg = st_holder->get_aggregate(qualified_name.front())) return agg;
        }
        return {};
    }

    const auto& first = qualified_name.front();
    const auto  rest  = qualified_name.without_front();

    if (auto nspc = dynamic_cast<const ns*>(&elem)) {
        if (auto child = nspc->get_child_namespace(first)) {
            if (auto st = resolve_struct_from(*child, rest)) return st;
        }
    }
    if (auto st_holder = dynamic_cast<const aggregate_holder*>(&elem)) {
        if (auto agg = st_holder->get_aggregate(first)) {
            if (auto nested = resolve_struct_from(*agg, rest)) return nested;
        }
    }
    return {};
}

std::shared_ptr<type>
aggregate_type_resolver::resolve_type_from_root(const k::name& name_without_prefix) {
    if (name_without_prefix.empty()) return {};
    auto root_ns = _unit.get_root_namespace();
    if (!root_ns) return {};

    const auto& unit_name = _unit.get_unit_name();
    if (!unit_name.empty() && name_without_prefix.front() == unit_name.back()) {
        auto rest = name_without_prefix.without_front();
        if (!rest.empty()) {
            if (auto st = resolve_struct_from(*root_ns, rest)) return st->get_struct_type();
        }
    }
    if (auto st = resolve_struct_from(*root_ns, name_without_prefix)) return st->get_struct_type();

    // Fallback: search imported modules.
    if (auto agg = _unit.get_or_create_imported_aggregate(name_without_prefix, _context)) {
        return agg->get_struct_type();
    }
    return {};
}

// ── Template instantiation from type reference (aggregate_type_resolver) ─────

std::shared_ptr<type> aggregate_type_resolver::try_instantiate_template_type(
    const std::shared_ptr<unresolved_type>& unres,
    const element& context_elem)
{
    const auto& base_name = unres->type_id();
    const auto& ast_args = unres->get_ast_template_args();

    // 1. Look up the template aggregate by base name (walking scope chain)
    std::shared_ptr<aggregate> tpl_agg;
    for (auto current = context_elem.shared_as<const element>(); current; current = current->parent<element>()) {
        if (auto st = resolve_struct_from(*current, base_name)) {
            if (st->is_template()) { tpl_agg = st; break; }
            return {}; // Found non-template — not a template instantiation
        }
    }
    if (!tpl_agg) {
        auto root_ns = _unit.get_root_namespace();
        if (root_ns) {
            if (auto st = resolve_struct_from(*root_ns, base_name)) {
                if (st->is_template()) tpl_agg = st;
            }
        }
    }
    if (!tpl_agg) return {};

    auto* ti = tpl_agg->get_tpl_info();
    if (!ti) return {};

    // 2. Validate argument count (allow fewer args if trailing params have defaults)
    if (ast_args.size() > ti->params.size()) return {};
    if (ast_args.size() < ti->params.size()) {
        // Check that all missing params have defaults
        for (size_t i = ast_args.size(); i < ti->params.size(); ++i) {
            auto& param = ti->params[i];
            if (param.is_type_param() && !param.default_type) return {};
            if (param.is_value_param() && !param.default_value.has_value()) return {};
        }
    }

    // 3. Convert AST template args to model template_arguments
    std::vector<template_argument> model_args;
    model_args.reserve(ti->params.size());
    for (size_t i = 0; i < ast_args.size(); ++i) {
        const auto& ast_arg = ast_args[i];
        if (ast_arg->is_type()) {
            auto arg_type = _context->from_type_specifier(*ast_arg->type_arg);
            if (!arg_type || !type::is_resolved(arg_type)) {
                if (auto unres_arg = std::dynamic_pointer_cast<unresolved_type>(arg_type)) {
                    auto resolved = resolve_type_by_name(unres_arg->type_id(), context_elem);
                    if (resolved && type::is_resolved(resolved)) {
                        arg_type = resolved;
                    }
                }
            }
            if (!arg_type || !type::is_resolved(arg_type)) return {};
            model_args.push_back(template_argument::make_type(arg_type));
        } else {
            // Value template argument — extract compile-time constant literal
            k::value_type val;
            if (!extract_value_from_ast_expr(ast_arg->value_arg.get(), val)) return {};
            model_args.push_back(template_argument::make_value(val));
        }
    }
    // 3b. Fill in default arguments for missing trailing parameters
    for (size_t i = ast_args.size(); i < ti->params.size(); ++i) {
        auto& param = ti->params[i];
        if (param.is_type_param() && param.default_type) {
            // Resolve the default type if needed
            auto def_type = param.default_type;
            if (!type::is_resolved(def_type)) {
                def_type = _context->resolve_type(def_type);
                if (!def_type || !type::is_resolved(def_type)) {
                    if (auto unres = std::dynamic_pointer_cast<unresolved_type>(param.default_type)) {
                        auto resolved = resolve_type_by_name(unres->type_id(), context_elem);
                        if (resolved && type::is_resolved(resolved)) def_type = resolved;
                    }
                }
            }
            if (!def_type || !type::is_resolved(def_type)) return {};
            model_args.push_back(template_argument::make_type(def_type));
        } else if (param.is_value_param() && param.default_value.has_value()) {
            model_args.push_back(template_argument::make_value(*param.default_value));
        } else {
            return {};
        }
    }

    // 3c. Resolve constraint types in template params if still unresolved
    for (auto& param : ti->params) {
        if (param.is_type_param() && param.constraint_type && !type::is_resolved(param.constraint_type)) {
            auto resolved = _context->resolve_type(param.constraint_type);
            if (resolved && type::is_resolved(resolved)) {
                param.constraint_type = resolved;
            } else if (auto unres = std::dynamic_pointer_cast<unresolved_type>(param.constraint_type)) {
                auto r = resolve_type_by_name(unres->type_id(), *tpl_agg);
                if (r && type::is_resolved(r)) param.constraint_type = r;
            }
        }
    }

    // 3d. Validate type constraints (kind filter + base-type constraint)
    {
        size_t err_idx;
        std::string err_reason;
        if (!validate_template_arg_constraints(ti->params, model_args, err_idx, err_reason)) {
            return {};  // Constraint violated — silently fail (error reporting TODO)
        }
    }

    // 4. Instantiate the template aggregate
    auto parent_ns = scope_lookup::enclosing_namespace(*tpl_agg);
    if (!parent_ns) return {};

    auto concrete = template_instantiator::instantiate_aggregate(
        *tpl_agg, model_args, parent_ns, _unit, _context, *this);
    if (!concrete) return {};

    // 5. Return existing struct_type or create a new one
    if (concrete->get_struct_type()) return concrete->get_struct_type();

    std::shared_ptr<struct_type> st_type{
        new struct_type(concrete->get_short_name(), concrete->shared_as<aggregate>())};
    _context->add_struct(st_type);
    concrete->set_struct_type(st_type);

    // 5b. Create 'this' parameters for member functions (requires struct_type)
    for (auto& child : concrete->get_children()) {
        if (auto fn = std::dynamic_pointer_cast<function>(child)) {
            if (fn->is_member() && !fn->is_static()) {
                fn->create_this_parameter();
            }
        }
    }
    // 5c. Assign FQ (fully-qualified) name to the concrete aggregate.
    //     symbol_resolver::visit_named_element normally does this, but the
    //     concrete aggregate was created after that pass already ran.
    //     Without a root-prefixed FQ name, update_mangled_name() produces
    //     an empty mangled name which breaks code generation and the JIT.
    if (concrete->get_fq_name().empty() && !concrete->get_short_name().empty()) {
        if (auto ancestor = concrete->template ancestor<named_element>()) {
            concrete->assign_name(ancestor->get_name().with_back(concrete->get_short_name()));
        }
    }
    concrete->update_mangled_name();

    // 5d. Update FQ names and mangled names for children (functions, constructors, etc.)
    for (auto& child : concrete->get_children()) {
        if (auto fn = std::dynamic_pointer_cast<function>(child)) {
            // Build FQ name from parent chain (mirrors symbol_resolver::visit_named_element)
            if (fn->get_fq_name().empty() && !fn->get_short_name().empty()) {
                if (auto parent_named = fn->template parent<named_element>()) {
                    fn->assign_name(parent_named->get_name().with_back(fn->get_short_name()));
                }
            }
            fn->update_mangled_name();
        }
    }

    // 6. Resolve the LLVM struct type immediately (member types are already
    //    concrete thanks to the instantiator's type substitution).
    std::unordered_set<struct_type*> in_progress;
    _context->resolve_struct_type(st_type, in_progress);

    return st_type;
}

std::shared_ptr<type>
/**
 * Resolve a type by qualified name from a context element, walking up the scope chain
 * (aggregate_type_resolver version, used during Phase 1.a).
 *
 * Steps:
 *   1. Root-prefixed: delegate to resolve_type_from_root.
 *   2. Try primitive types via context->from_string.
 *   3. Walk up the scope chain looking for aggregates and enumerations.
 *   4. At each scope level, check using directives (anonymous, aliased, specific).
 *   5. Fallback: imported aggregates and enums.
 */
aggregate_type_resolver::resolve_type_by_name(const k::name& type_name, const element& context_elem) {
    // Step 1: Root-prefixed: delegate to resolve_type_from_root
    if (type_name.empty()) return {};

    if (type_name.has_root_prefix()) {
        return resolve_type_from_root(type_name.without_root_prefix());
    }

    // Step 2: Try primitive types via context->from_string
    if (type_name.size() == 1) {
        auto prim = _context->from_string(type_name.front());
        if (prim && type::is_resolved(prim)) return prim;
    }

    // Step 3: Walk up the scope chain looking for aggregates and enumerations
    for (auto current = context_elem.shared_as<const element>(); current; current = current->parent<element>()) {
        if (auto st = resolve_struct_from(*current, type_name)) return st->get_struct_type();
        // Also look for enum types (simple names only for now)
        if (type_name.size() == 1) {
            if (auto eh = std::dynamic_pointer_cast<const enum_holder>(current)) {
                if (auto en = eh->get_enum(type_name.front())) {
                    return en->get_enum_type();
                }
            }
        }

        // Step 4: At each scope level, check using directives
        // Check using directives at this scope level for type resolution
        if (auto uh = std::dynamic_pointer_cast<const using_holder>(current)) {
            for (const auto& dir : uh->get_using_directives()) {
                if (dir.is_namespace() && !dir.has_alias()) {
                    // 'using namespace X::Y;' (anonymous) — search type_name within the target
                    auto target_elem = resolve_using_target(dir.target_name, _unit);
                    if (!target_elem) {
                        if (auto imp_agg = _unit.get_or_create_imported_aggregate(dir.target_name, _context)) {
                            target_elem = std::dynamic_pointer_cast<const element>(imp_agg);
                        }
                    }
                    if (target_elem) {
                        if (auto st = resolve_struct_from(*target_elem, type_name)) return st->get_struct_type();
                        if (type_name.size() == 1) {
                            if (auto eh = std::dynamic_pointer_cast<const enum_holder>(target_elem)) {
                                if (auto en = eh->get_enum(type_name.front())) {
                                    return en->get_enum_type();
                                }
                            }
                        }
                    }

                } else if (dir.is_namespace() && dir.has_alias()) {
                    // 'using M = namespace X::Y;' — M acts as a prefix: M::Type
                    if (type_name.front() == *dir.alias_name && type_name.size() > 1) {
                        auto rest = type_name.without_front();
                        auto target_elem = resolve_using_target(dir.target_name, _unit);
                        if (!target_elem) {
                            if (auto imp_agg = _unit.get_or_create_imported_aggregate(dir.target_name, _context)) {
                                target_elem = std::dynamic_pointer_cast<const element>(imp_agg);
                            }
                        }
                        if (target_elem) {
                            if (auto st = resolve_struct_from(*target_elem, rest)) return st->get_struct_type();
                            if (rest.size() == 1) {
                                if (auto eh = std::dynamic_pointer_cast<const enum_holder>(target_elem)) {
                                    if (auto en = eh->get_enum(rest.front())) {
                                        return en->get_enum_type();
                                    }
                                }
                            }
                        }
                        // Fallback: construct FQ name and search imported modules
                        {
                            auto fq = dir.target_name;
                            for (size_t i = 0; i < rest.size(); ++i) fq = fq.with_back(rest[i]);
                            if (auto imp_agg = _unit.get_or_create_imported_aggregate(fq, _context)) {
                                return imp_agg->get_struct_type();
                            }
                            if (auto imp_en = _unit.get_or_create_imported_enum(fq, _context)) {
                                return imp_en->get_enum_type();
                            }
                        }
                    }

                } else {
                    // Specific using, with or without alias
                    const std::string& real_name = dir.target_name.back();
                    const std::string& lookup_name = dir.has_alias() ? *dir.alias_name : real_name;
                    if (type_name.front() == lookup_name) {
                        auto parent_name = dir.target_name.without_back();
                        std::shared_ptr<const element> parent_elem;
                        if (parent_name.empty()) {
                            parent_elem = _unit.get_root_namespace();
                        } else {
                            parent_elem = resolve_using_target(parent_name, _unit);
                            if (!parent_elem) {
                                if (auto imp_agg = _unit.get_or_create_imported_aggregate(parent_name, _context)) {
                                    parent_elem = std::dynamic_pointer_cast<const element>(imp_agg);
                                }
                            }
                        }
                        if (parent_elem) {
                            if (type_name.size() == 1) {
                                if (auto st = resolve_struct_from(*parent_elem, k::name{real_name})) return st->get_struct_type();
                                if (auto eh = std::dynamic_pointer_cast<const enum_holder>(parent_elem)) {
                                    if (auto en = eh->get_enum(real_name)) return en->get_enum_type();
                                }
                            } else {
                                if (auto ah = std::dynamic_pointer_cast<const aggregate_holder>(parent_elem)) {
                                    if (auto agg = ah->get_aggregate(real_name)) {
                                        if (auto st = resolve_struct_from(*agg, type_name.without_front())) return st->get_struct_type();
                                    }
                                }
                                if (auto nspc = std::dynamic_pointer_cast<const ns>(parent_elem)) {
                                    if (auto child = nspc->get_child_namespace(real_name)) {
                                        if (auto st = resolve_struct_from(*child, type_name.without_front())) return st->get_struct_type();
                                    }
                                }
                            }
                        }
                        // Fallback: try imported modules directly using the full target name
                        if (type_name.size() == 1) {
                            if (auto imp_agg = _unit.get_or_create_imported_aggregate(dir.target_name, _context)) {
                                return imp_agg->get_struct_type();
                            }
                            if (auto imp_en = _unit.get_or_create_imported_enum(dir.target_name, _context)) {
                                return imp_en->get_enum_type();
                            }
                        }
                    }
                }
            }
        }
    }

    // Step 5: Fallback: imported aggregates and enums
    // Fallback: search imported modules (relative name, scope chain exhausted)
    if (auto agg = _unit.get_or_create_imported_aggregate(type_name, _context)) {
        return agg->get_struct_type();
    }
    // Fallback: search imported enums
    if (auto en = _unit.get_or_create_imported_enum(type_name, _context)) {
        return en->get_enum_type();
    }
    return {};
}

// ── Resolve a single type reference (for parameters and member variables) ────

// ── Resolve a single type reference (for parameters and member variables) ────

static std::shared_ptr<type>
resolve_one_type(const std::shared_ptr<type>& t,
                 aggregate_type_resolver& resolver,
                 const element& context_elem,
                 std::shared_ptr<context> ctx) {
    if (type::is_resolved(t)) return t;

    // ── Template instantiation path (try FIRST for template types) ───────
    // If the type is an unresolved_type carrying AST template arguments
    // (e.g. Box<int>), try template instantiation before calling resolve_type,
    // which would emit a spurious "cannot resolve type" for the base name.
    if (auto unres = std::dynamic_pointer_cast<unresolved_type>(t)) {
        if (unres->has_template_args()) {
            auto tpl_resolved = resolver.try_instantiate_template_type(unres, context_elem);
            if (tpl_resolved && type::is_resolved(tpl_resolved)) return tpl_resolved;
        }
    }

    // Composite type (reference_type, pointer_type, etc. wrapping an unresolved subtype)
    auto resolved_composite = ctx->resolve_type(t);
    if (type::is_resolved(resolved_composite)) return resolved_composite;

    auto unres = std::dynamic_pointer_cast<unresolved_type>(t);
    if (!unres) return t; // cannot resolve further

    auto resolved = resolver.resolve_type_by_name(unres->type_id(), context_elem);
    if (!resolved || !type::is_resolved(resolved)) {
        resolved = ctx->from_string(unres->type_id());
    }
    return resolved;
}

// ── Visitors ─────────────────────────────────────────────────────────────────

void aggregate_type_resolver::resolve() {
    trace("[aggregate_type_resolver::resolve] begin");
    visit_unit(_unit);
    trace("[aggregate_type_resolver::resolve] done");
}

void aggregate_type_resolver::visit_unit(unit& /*unit*/) {
    visit_namespace(*_unit.get_root_namespace());
    // Note: global_constructor_function, global_destructor_function and global_main_function
    // are handled by type_reference_resolver — they contain expressions and statements.
    // aggregate_type_resolver only handles declaration-level type resolution.
}

void aggregate_type_resolver::visit_namespace(ns& ns) {
    // Use index-based loop: template instantiation can add new aggregates
    // to this namespace's children list, invalidating range-based iterators.
    for (size_t i = 0; i < ns.get_children().size(); ++i) {
        ns.get_children()[i]->accept(*this);
    }
}

void aggregate_type_resolver::visit_aggregate(aggregate& st) {
    // Skip template definitions — they are not instantiated yet.
    // But first resolve constraint_type and default_type in their tpl_info
    // so that constraint validation works correctly.
    if (st.is_template()) {
        if (auto* ti = st.get_tpl_info()) {
            for (auto& param : ti->params) {
                if (param.constraint_type && !type::is_resolved(param.constraint_type)) {
                    auto resolved = _context->resolve_type(param.constraint_type);
                    if (resolved && type::is_resolved(resolved)) {
                        param.constraint_type = resolved;
                    } else if (auto unres = std::dynamic_pointer_cast<unresolved_type>(param.constraint_type)) {
                        auto r = resolve_type_by_name(unres->type_id(), st);
                        if (r && type::is_resolved(r)) param.constraint_type = r;
                    }
                }
                if (param.default_type && !type::is_resolved(param.default_type)) {
                    auto resolved = _context->resolve_type(param.default_type);
                    if (resolved && type::is_resolved(resolved)) {
                        param.default_type = resolved;
                    } else if (auto unres = std::dynamic_pointer_cast<unresolved_type>(param.default_type)) {
                        auto r = resolve_type_by_name(unres->type_id(), st);
                        if (r && type::is_resolved(r)) param.default_type = r;
                    }
                }
            }
        }
        return;
    }

    // Visit nested aggregate children first (depth-first), so their types are available
    // before we process members of the outer aggregate.
    for (auto& child : st.get_children()) {
        if (auto nested = std::dynamic_pointer_cast<aggregate>(child)) {
            nested->accept(*this);
        }
    }

    // Visit member variable children to resolve their types.
    // This triggers template instantiation for types like Wrapper<int>,
    // ensuring concrete instantiations exist before resolve_types() builds
    // LLVM struct types.
    for (auto& child : st.get_children()) {
        if (auto mv = std::dynamic_pointer_cast<member_variable_definition>(child)) {
            mv->accept(*this);
        } else if (auto gv = std::dynamic_pointer_cast<global_variable_definition>(child)) {
            gv->accept(*this);
        }
    }
}

void aggregate_type_resolver::visit_klass(klass& klass) {
    visit_aggregate(klass);

    // Build the LLVM struct type for the vtable (mirrors type_reference_resolver::visit_klass)
    if (!klass.has_vtable()) return;

    auto vt = klass.get_vtable();
    size_t num_slots = vt->slot_count();

    llvm::LLVMContext& llvm_ctx = **_context;
    llvm::Type* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);

    std::vector<llvm::Type*> vtable_fields;
    vtable_fields.push_back(ptr_ty); // RTTI placeholder
    for (size_t i = 0; i < num_slots; ++i) {
        vtable_fields.push_back(ptr_ty);
    }
    std::string vtable_struct_name = "__vtable_" + klass.get_short_name() + "__";
    // Only create if not already created (idempotency)
    auto* existing = llvm::StructType::getTypeByName(llvm_ctx, vtable_struct_name);
    if (!existing) {
        existing = llvm::StructType::create(llvm_ctx, vtable_fields, vtable_struct_name);
    }
    vt->llvm_type = existing;
}

void aggregate_type_resolver::visit_interface(interface& iface) {
    visit_klass(iface);
}

void aggregate_type_resolver::visit_member_variable_definition(member_variable_definition& var) {
    // __parent__ is already assigned a resolved type by symbol_resolver
    if (var.get_short_name() == "__parent__") return;
    // Skip members with no type yet (e.g. annotation fields before resolution)
    if (!var.get_type()) return;

    if (!type::is_resolved(var.get_type())) {
        auto resolved = resolve_one_type(var.get_type(), *this, var, _context);
        if (resolved && type::is_resolved(resolved)) {
            var.set_type(resolved);
        }
    }
    // Do NOT visit init expressions — those are expressions, handled by type_reference_resolver
}

/**
 * Visit and resolve the type of a global variable during Phase 1.a.
 *
 * Handles unresolved_function_ref_type by resolving parameter types.
 * For other types, delegates to resolve_one_type.
 * Does NOT visit init expressions (Phase 1.b handles those).
 */
void aggregate_type_resolver::visit_global_variable_definition(global_variable_definition& var) {
    if (!var.get_type()) return; // No type yet (e.g. unprocessed static member)
    if (!type::is_resolved(var.get_type())) {
        if (auto ufrt = std::dynamic_pointer_cast<unresolved_function_ref_type>(var.get_type())) {
            // Function reference type for a global variable: resolve it using the variable's scope.
            // We need a type_reference_resolver to call resolve_function_ref_type, but
            // aggregate_type_resolver is an earlier pass. We can do a best-effort resolution:
            // parameter types that are primitive/identified are already resolved by context::from_type_specifier.
            // Build the function_reference_type directly from the already-resolved param types.
            function_reference_type_builder builder(_context);
            builder.ref_kind(ufrt->get_ref_kind());
            bool all_resolved = true;
            for (const auto& pt : ufrt->parameter_types()) {
                if (!type::is_resolved(pt)) {
                    // Try to resolve via name
                    if (auto u = std::dynamic_pointer_cast<unresolved_type>(pt)) {
                        auto rpt = resolve_type_by_name(u->type_id(), var);
                        if (!rpt || !type::is_resolved(rpt)) { all_resolved = false; break; }
                        builder.append_parameter_type(rpt);
                    } else { all_resolved = false; break; }
                } else {
                    builder.append_parameter_type(pt);
                }
            }
            if (all_resolved) {
                // No return type known yet (no init expression resolved), leave null for now
                auto resolved = builder.build();
                if (resolved) {
                    var.set_type(resolved);
                }
            }
        } else {
            auto resolved = resolve_one_type(var.get_type(), *this, var, _context);
            if (resolved && type::is_resolved(resolved)) {
                var.set_type(resolved);
            }
        }
    }
    // Do NOT register in global_constructor — that is done by type_reference_resolver
    // Do NOT visit init expressions — those are expressions, handled by type_reference_resolver
}

/**
 * Resolve the type of a function parameter during Phase 1.a (signatures only).
 *
 * Steps:
 *   1. Handle unresolved_function_ref_type: resolve parameter types and optional owner.
 *   2. For other types: try context->resolve_type, then peel composite wrappers to find
 *      the inner unresolved_type, resolve it by name, and re-apply wrappers.
 *
 * Does NOT process default expressions (type_reference_resolver handles those).
 */
void aggregate_type_resolver::visit_parameter(parameter& param) {
    // Step 1: Handle unresolved_function_ref_type: resolve parameter types and optional owner
    // Resolve the type only (no default expressions — those are handled by type_reference_resolver)
    if (!type::is_resolved(param.get_type())) {
        // Handle unresolved_function_ref_type (function pointer/pin/link parameter type)
        if (auto ufrt = std::dynamic_pointer_cast<unresolved_function_ref_type>(param.get_type())) {
            function_reference_type_builder builder(_context);
            builder.ref_kind(ufrt->get_ref_kind());
            bool all_resolved = true;
            auto owner_func = param.parent<function>();
            // Resolve owner for member function reference parameters (e.g. Counter::*(int))
            if (!ufrt->owner_name().empty()) {
                std::shared_ptr<aggregate> owner_agg;
                if (owner_func) owner_agg = resolve_struct_from(*owner_func, ufrt->owner_name());
                if (!owner_agg) {
                    auto root_ns = _unit.get_root_namespace();
                    if (root_ns) owner_agg = resolve_struct_from(*root_ns, ufrt->owner_name());
                }
                if (owner_agg) {
                    builder.member_of(owner_agg);
                } else {
                    all_resolved = false;
                }
            }
            for (const auto& pt : ufrt->parameter_types()) {
                if (!type::is_resolved(pt)) {
                    if (auto u = std::dynamic_pointer_cast<unresolved_type>(pt)) {
                        std::shared_ptr<type> rpt;
                        if (owner_func) rpt = resolve_type_by_name(u->type_id(), *owner_func);
                        if (!rpt || !type::is_resolved(rpt)) rpt = _context->from_string(u->type_id());
                        if (!rpt || !type::is_resolved(rpt)) { all_resolved = false; break; }
                        builder.append_parameter_type(rpt);
                    } else { all_resolved = false; break; }
                } else {
                    builder.append_parameter_type(pt);
                }
            }
            if (all_resolved) {
                auto resolved = builder.build();
                if (resolved) param.set_type(resolved);
            }
            return;
        }
        auto res_type = _context->resolve_type(param.get_type());
        if (!type::is_resolved(res_type)) {
            // Try name-based resolution.
            // The parameter type may be a composite wrapping an unresolved_type
            // (e.g. reference_type("iface_one::ICounter&")), so we peel wrappers to
            // Step 2: For other types: try context->resolve_type, then peel composite wrappers to find the inner unreso...
            // find the inner unresolved name, resolve the inner aggregate, then
            // rebuild the composite wrapper around the resolved inner type.
            auto owner_func = param.parent<function>();
            if (owner_func) {
                // Collect wrapper kinds (from outermost to innermost unresolved_type)
                enum class WrapKind { Ref, Ptr, Link, View, Const, Owner, Drain };
                std::vector<WrapKind> wrappers;
                auto inner = param.get_type();
                while (inner && !std::dynamic_pointer_cast<unresolved_type>(inner)) {
                    if      (type::is_reference(inner))  wrappers.push_back(WrapKind::Ref);
                    else if (type::is_pointer(inner))    wrappers.push_back(WrapKind::Ptr);
                    else if (type::is_link(inner))       wrappers.push_back(WrapKind::Link);
                    else if (type::is_view(inner))       wrappers.push_back(WrapKind::View);
                    else if (type::is_const(inner))      wrappers.push_back(WrapKind::Const);
                    else if (type::is_owner(inner))      wrappers.push_back(WrapKind::Owner);
                    else if (type::is_drain(inner))      wrappers.push_back(WrapKind::Drain);
                    else break;
                    inner = inner->get_subtype();
                }
                auto unres = std::dynamic_pointer_cast<unresolved_type>(inner);
                if (unres && !unres->type_id().empty()) {
                    // Resolve the inner aggregate type
                    std::shared_ptr<type> inner_resolved;
                    // If the inner type has template args, try template instantiation first
                    if (unres->has_template_args()) {
                        inner_resolved = try_instantiate_template_type(unres, *owner_func);
                    }
                    if (!inner_resolved || !type::is_resolved(inner_resolved)) {
                        inner_resolved = resolve_type_by_name(unres->type_id(), *owner_func);
                    }
                    if (type::is_resolved(inner_resolved)) {
                        // Re-apply wrappers in reverse order (innermost first)
                        res_type = inner_resolved;
                        for (auto it = wrappers.rbegin(); it != wrappers.rend(); ++it) {
                            switch (*it) {
                                case WrapKind::Ref:   res_type = res_type->get_reference(); break;
                                case WrapKind::Ptr:   res_type = res_type->get_pointer();   break;
                                case WrapKind::Link:  res_type = res_type->get_link();      break;
                                case WrapKind::View:  res_type = res_type->get_view();      break;
                                case WrapKind::Const: res_type = res_type->get_const();     break;
                                case WrapKind::Owner: res_type = res_type->get_owner();     break;
                                case WrapKind::Drain: res_type = res_type->get_drain();     break;
                            }
                        }
                    }
                }
            }
        }
        if (type::is_resolved(res_type)) {
            param.set_type(res_type);
        }
    }
}

/**
 * Resolve function signatures during Phase 1.a: this parameter, parameters, and return type.
 *
 * Steps:
 *   1. Resolve 'this' parameter type for non-static member functions.
 *   2. Resolve all parameter types.
 *   3. Resolve return type (including function pointer return types).
 *
 * Does NOT visit the function body (Phase 1.b).
 */
void aggregate_type_resolver::visit_function(function& fn) {
    // Step 1: Resolve 'this' parameter type for non-static member functions
    // Resolve 'this' parameter type for non-static member functions
    if (fn.is_member() && !fn.is_static() && fn.get_this_parameter()) {
        auto this_param = std::const_pointer_cast<parameter>(fn.get_this_parameter());
        this_param->accept(*this);
    }

    // Step 2: Resolve all parameter types
    // Resolve parameter types (signatures only, no default expressions)
    for (auto param : fn.parameters()) {
        param->accept(*this);
    }

    // Step 3: Resolve return type (including function pointer return types)
    // Resolve return type
    if (fn.get_return_type() && !type::is_resolved(fn.get_return_type())) {
        // Handle unresolved_function_ref_type (function pointer return type)
        if (auto ufrt = std::dynamic_pointer_cast<unresolved_function_ref_type>(fn.get_return_type())) {
            function_reference_type_builder builder(_context);
            builder.ref_kind(ufrt->get_ref_kind());
            bool all_resolved = true;
            for (const auto& pt : ufrt->parameter_types()) {
                if (!type::is_resolved(pt)) {
                    if (auto u = std::dynamic_pointer_cast<unresolved_type>(pt)) {
                        auto rpt = resolve_type_by_name(u->type_id(), fn);
                        if (!rpt || !type::is_resolved(rpt)) { all_resolved = false; break; }
                        builder.append_parameter_type(rpt);
                    } else { all_resolved = false; break; }
                } else {
                    builder.append_parameter_type(pt);
                }
            }
            if (all_resolved) {
                auto resolved = builder.build();
                if (resolved) fn.set_return_type(resolved);
            }
        } else {
        // Try template instantiation for return types with template args (e.g. Box<int>)
        auto unres_ret = std::dynamic_pointer_cast<unresolved_type>(fn.get_return_type());
        std::shared_ptr<type> resolved;
        if (unres_ret && unres_ret->has_template_args()) {
            resolved = try_instantiate_template_type(unres_ret, fn);
        }
        if (!resolved || !type::is_resolved(resolved)) {
            resolved = resolve_type_by_name(
                unres_ret ? unres_ret->type_id() : k::name{},
                fn);
        }
        if (resolved && type::is_resolved(resolved)) {
            fn.set_return_type(resolved);
        } else {
            auto resolved2 = _context->resolve_type(fn.get_return_type());
            if (type::is_resolved(resolved2)) fn.set_return_type(resolved2);
        }
        } // end else (not unresolved_function_ref_type)
    }

    // NOTE: the block / body is NOT visited here.
    // That is the responsibility of type_reference_resolver (Phase 1.b).
}

void aggregate_type_resolver::visit_constructor(constructor& ctor) {
    visit_function(ctor);
}

void aggregate_type_resolver::visit_destructor(destructor& dtor) {
    visit_function(dtor);
}

void aggregate_type_resolver::visit_static_constructor(static_constructor& sctor) {
    visit_function(sctor);
}

void aggregate_type_resolver::visit_static_destructor(static_destructor& sdtor) {
    visit_function(sdtor);
}

void aggregate_type_resolver::visit_global_constructor_function(global_constructor_function& func) {
    visit_function(func);
}

void aggregate_type_resolver::visit_global_destructor_function(global_destructor_function& func) {
    visit_function(func);
}

void aggregate_type_resolver::visit_global_main_function(global_main_function& /*func*/) {
    // Nothing to do — global_main_function is created and resolved in type_reference_resolver
}

//
// Model materializer (Phase 2)
//

void model_materializer::materialize() {
    trace("[model_materializer::materialize] begin");
    visit_unit(_unit);
    trace("[model_materializer::materialize] done");
}

void model_materializer::visit_unit(unit& /*u*/) {
    visit_namespace(*_unit.get_root_namespace());
}

void model_materializer::visit_namespace(ns& n) {
    for (auto& child : n.get_children()) {
        child->accept(*this);
    }
}

void model_materializer::visit_aggregate(aggregate& st) {
    // Skip template definitions — they are not instantiated yet.
    if (st.is_template()) return;

    // Visit nested aggregates first (depth-first)
    for (auto& child : st.get_children()) {
        if (auto nested = std::dynamic_pointer_cast<aggregate>(child)) {
            nested->accept(*this);
        }
    }
}

void model_materializer::visit_klass(klass& kl) {
    trace("[model_materializer::visit_klass] '{}'", {kl.get_short_name()});
    // Recurse into nested aggregates
    visit_aggregate(kl);

    if (!kl.has_vtable()) return;

    debug("[model_materializer::visit_klass] '{}' has vtable, validating and computing secondary specs", {kl.get_short_name()});

    // 1. Validate vtable consistency
    validate_vtable(kl);

    // 2. Compute secondary vtable thunk specs (requires LLVM struct types to exist)
    compute_secondary_vtable_specs(kl);
}

void model_materializer::visit_interface(interface& iface) {
    visit_klass(iface);
}

bool model_materializer::validate_vtable(klass& kl) {
    auto vt = kl.get_vtable();
    if (!vt) return true;

    bool ok = true;
    for (auto& entry : vt->entries) {
        if (!entry.func) continue;
        // If the slot is still occupied by an abstract function and the class is NOT abstract,
        // that is a compilation error (should have been caught by symbol_resolver, but we
        // double-check here as a defensive measure).
        if (entry.func->is_abstract_func() && !kl.is_abstract()) {
            throw_error(static_cast<unsigned int>(k::diag::symbol_diag::ERR_DUPLICATE_BASE_CLASS), kl.get_ast_aggregate_decl() ? lex::opt_any_lexeme{lex::any_lexeme{kl.get_ast_aggregate_decl()->name}} : lex::opt_any_lexeme{},
                "class '{}' must implement abstract method '{}' (introduced in '{}') "
                "or be declared 'abstract'",
                {kl.get_short_name(),
                 entry.func->get_short_name(),
                 entry.introducing_func && entry.introducing_func->get_owner()
                     ? entry.introducing_func->get_owner()->get_short_name()
                     : "?"});
            ok = false;
        }
    }
    return ok;
}

/**
 * Compute secondary vtable thunk descriptors for a class with multiple base classes.
 *
 * Steps:
 *   1. Walk all non-virtual sub-objects transitively, computing cumulative byte offsets.
 *   2. For each sub-object with a vtable, build a secondary_vtable_spec with thunk_info
 *      records: slot index, real function, this-adjustment, and needs_thunk flag.
 *   3. Handle virtual bases separately via __vbase_X__ fields.
 *
 * Populates vtable_layout::secondary_vtables.
 */
void model_materializer::compute_secondary_vtable_specs(klass& kl) {
    auto vt = kl.get_vtable();
    if (!vt) return;

    // Clear any previously computed specs (idempotency)
    vt->secondary_vtables.clear();

    auto kl_struct_type = kl.get_struct_type();
    if (!kl_struct_type) return;

    llvm::StructType* kl_llvm_type = llvm::cast_or_null<llvm::StructType>(
        kl_struct_type->get_llvm_type());
    if (!kl_llvm_type) return;

    constexpr size_t PTR_SIZE = 8; // bytes, 64-bit assumption

    // Helper: compute byte offset of a named field in an LLVM struct type
    auto field_byte_offset = [&](llvm::StructType* sty, unsigned field_idx) -> size_t {
        size_t off = 0;
        for (unsigned fi = 0; fi < field_idx; ++fi) {
            llvm::Type* ft = sty->getElementType(fi);
            if (!ft) { off += PTR_SIZE; continue; }
            if (ft->isPointerTy())      off += PTR_SIZE;
            else if (ft->isIntegerTy()) off += (ft->getIntegerBitWidth() + 7) / 8;
            else if (ft->isFloatTy())   off += 4;
            else if (ft->isDoubleTy())  off += 8;
            else if (auto* sty2 = llvm::dyn_cast<llvm::StructType>(ft))
                                        off += PTR_SIZE * sty2->getNumElements();
            else                        off += PTR_SIZE;
        }
        return off;
    };

    // Helper: does derived_func transitively override base_func?
    auto overrides_base_func = [&](const function& derived_func,
                                   const function& base_func) -> bool {
        const function* cur = &derived_func;
        while (cur) {
            if (cur == &base_func) return true;
            auto ov = cur->get_overrides();
            cur = ov ? ov.get() : nullptr;
        }
        return false;
    };

    // Helper: build a secondary_vtable_spec for base_klass at byte_offset in kl
    auto build_spec = [&](std::shared_ptr<klass> base_klass, size_t byte_offset) {
        auto base_vt = base_klass->get_vtable();
        if (!base_vt || !base_vt->llvm_type) return;

        secondary_vtable_spec spec;
        spec.base_class  = base_klass;
        spec.base_offset = static_cast<ptrdiff_t>(byte_offset);

        for (auto& base_entry : base_vt->entries) {
            thunk_info ti;
            ti.slot_index = base_entry.slot_index;

            const vtable_entry* derived_entry = nullptr;
            for (auto& de : vt->entries) {
                if (de.func && base_entry.introducing_func
                    && (overrides_base_func(*de.func, *base_entry.introducing_func)
                        || (base_entry.func && overrides_base_func(*de.func, *base_entry.func)))) {
                    derived_entry = &de;
                    break;
                }
            }

            if (!derived_entry || !derived_entry->func) {
                ti.real_func       = base_entry.func;
                ti.this_adjustment = 0;
                ti.needs_thunk     = false;
            } else {
                bool is_overridden = (derived_entry->func.get() != base_entry.func.get());
                ti.real_func       = derived_entry->func;
                ti.this_adjustment = is_overridden ? static_cast<ptrdiff_t>(byte_offset) : 0;
                ti.needs_thunk     = is_overridden && (byte_offset > 0);
            }
            spec.slot_thunks.push_back(ti);
        }

        vt->secondary_vtables.push_back(std::move(spec));
    };

    // Step 1: Walk all non-virtual sub-objects transitively, computing cumulative byte offsets
    // Walk ALL non-virtual sub-objects transitively reachable from kl,
    // computing their cumulative byte offsets in kl's layout.
    // For each sub-object with a vtable, generate a secondary_vtable_spec.
    // This includes both "primary" and "secondary" bases at all levels —
    // in K's layout every base sub-object (including the primary) is at a
    // non-zero offset because kl's own __vptr__ occupies field 0.
    // We use `already_processed` to avoid duplicating specs for the same type.
    std::unordered_set<const klass*> already_processed;

    // Step 2: For each sub-object with a vtable, build a secondary_vtable_spec with thunk_info records
    // DFS: for each aggregate, walk its non-virtual bases and build specs for
    // all sub-objects that have a vtable, at their correct cumulative offsets.
    std::function<void(const aggregate&, llvm::StructType*, size_t)> walk;
    walk = [&](const aggregate& cur, llvm::StructType* cur_llvm_type, size_t cum_offset) {
        for (auto& bs : cur.get_bases()) {
            if (!bs.base || bs.is_virtual) continue;
            auto base_klass = std::dynamic_pointer_cast<klass>(bs.base);
            if (!base_klass) continue;

            // Find the field in cur's LLVM struct for this base sub-object
            std::string field_name = "__base_" + bs.sanitised_name() + "__";
            auto field_opt = (cur.get_struct_type())
                ? cur.get_struct_type()->get_member(field_name) : std::nullopt;

            size_t this_offset = cum_offset;
            if (field_opt && cur_llvm_type) {
                this_offset += field_byte_offset(cur_llvm_type, (unsigned)field_opt->index);
            }

            // Build spec for this base if not already done and it has a vtable
            if (!already_processed.count(base_klass.get()) && base_klass->has_vtable()) {
                already_processed.insert(base_klass.get());
                build_spec(base_klass, this_offset);
            }

            // Recurse into this base to pick up its own sub-objects
            auto base_llvm_type = base_klass->get_struct_type()
                ? llvm::cast_or_null<llvm::StructType>(base_klass->get_struct_type()->get_llvm_type())
                : nullptr;
            walk(*base_klass, base_llvm_type, this_offset);
        }
    };

    walk(kl, kl_llvm_type, 0);

    // Step 3: Handle virtual bases separately via __vbase_X__ fields. Populates vtable_layout::secondary_vtables
    // ── Virtual bases: generate secondary vtable specs for __vbase_X__ ───────
    // (same logic as before, unchanged)
    {
        auto vbases = kl.get_all_virtual_base_structs();
        for (auto& vbase_agg : vbases) {
            auto vbase_klass = std::dynamic_pointer_cast<klass>(vbase_agg);
            if (!vbase_klass || !vbase_klass->has_vtable()) continue;

            auto vbase_vt = vbase_klass->get_vtable();
            if (!vbase_vt || !vbase_vt->llvm_type) continue;

            std::string vbase_field_name = "__vbase_" + vbase_klass->get_short_name() + "__";
            auto vbase_field = kl_struct_type->get_member(vbase_field_name);
            if (!vbase_field) continue;

            size_t byte_offset = field_byte_offset(kl_llvm_type, (unsigned)vbase_field->index);

            if (!already_processed.count(vbase_klass.get())) {
                already_processed.insert(vbase_klass.get());
                build_spec(vbase_klass, byte_offset);
            }
        }
    }
}

//
// Type resolver
//

/**
 * Check function visibility from access site.
 * - Namespace-level functions: public = open, protected = same module, private = same ns.
 * - Struct member functions: public = open, protected/private = member functions of same struct (or nested).
 * Static constructors and destructors inherit the struct's own visibility — they are not
 * individually checked (their owner struct visibility gates them).
 */
void type_reference_resolver::check_function_visibility(const function& func, const element& /*access_site*/) {
    auto vis = func.get_visibility();
    if (vis == PUBLIC) return;

    auto owner_agg = std::const_pointer_cast<aggregate>(func.get_owner());
    if (owner_agg) {
        if (scope_lookup::is_struct_member_accessible(vis, *owner_agg, owner_agg, _function_stack)) return;
        if (vis == PROTECTED && scope_lookup::is_friend_of(*owner_agg, _function_stack, _unit)) return;
        throw_error(static_cast<unsigned int>(k::diag::function_diag::ERR_FUNC_CTOR_ACCESS_DENIED), func.get_ast_function_decl() ? lex::opt_any_lexeme{lex::any_lexeme{func.get_ast_function_decl()->name}} : lex::opt_any_lexeme{},
            "{} member function '{}' of struct '{}' is not accessible here; "
            "it can only be called from member functions of '{}'{}",
            {vis == PROTECTED ? "protected" : "private",
             func.get_short_name(), owner_agg->get_short_name(), owner_agg->get_short_name(),
             vis == PROTECTED ? " or its subclasses or friends" : ""});
    } else {
        // Namespace-level function
        auto owner_ns = scope_lookup::enclosing_namespace(func);
        if (!owner_ns) return;

        // Use the innermost function on the stack as access site, or fall back to the func itself
        const element* site = &func;
        if (!_function_stack.empty()) site = _function_stack.back().get();

        if (vis == PROTECTED) {
            auto owner_root = scope_lookup::root_namespace(*owner_ns);
            if (!owner_root || scope_lookup::is_in_same_module(*site, *owner_root)) return;
            throw_error(static_cast<unsigned int>(k::diag::function_diag::ERR_FUNC_ACCESS_DENIED), func.get_ast_function_decl() ? lex::opt_any_lexeme{lex::any_lexeme{func.get_ast_function_decl()->name}} : lex::opt_any_lexeme{},
                "protected function '{}' is only accessible within the same module; "
                "it is declared in module '{}' but accessed from outside",
                {func.get_short_name(), owner_root->get_short_name()});
        } else {
            if (scope_lookup::is_in_same_namespace(*site, *owner_ns)) return;
            throw_error(static_cast<unsigned int>(k::diag::function_diag::ERR_FUNC_ACCESS_DENIED), func.get_ast_function_decl() ? lex::opt_any_lexeme{lex::any_lexeme{func.get_ast_function_decl()->name}} : lex::opt_any_lexeme{},
                "private function '{}' is only accessible within namespace '{}'; "
                "it cannot be called from outside that namespace",
                {func.get_short_name(), owner_ns->get_short_name()});
        }
    }
}

void type_reference_resolver::check_constructor_visibility(const constructor& ctor, const element& /*access_site*/) {
    auto vis = ctor.get_visibility();
    if (vis == PUBLIC) return;

    auto owner_agg = std::const_pointer_cast<aggregate>(ctor.get_owner());
    if (!owner_agg) return;

    if (scope_lookup::is_struct_member_accessible(vis, *owner_agg, owner_agg, _function_stack)) return;
    if (vis == PROTECTED && scope_lookup::is_friend_of(*owner_agg, _function_stack, _unit)) return;

    throw_error(static_cast<unsigned int>(k::diag::function_diag::ERR_FUNC_CTOR_VISIBILITY_MISMATCH), ctor.get_ast_function_decl() ? lex::opt_any_lexeme{lex::any_lexeme{ctor.get_ast_function_decl()->name}} : lex::opt_any_lexeme{},
        "{} constructor of struct '{}' is not accessible here; "
        "it can only be called from member functions of '{}'{}",
        {vis == PROTECTED ? "protected" : "private",
         owner_agg->get_short_name(), owner_agg->get_short_name(),
         vis == PROTECTED ? " or its subclasses or friends" : ""});
}

/**
 * Resolve an aggregate (structure or class) by qualified name descending from elem, without climbing to parents.
 */
std::shared_ptr<aggregate>
type_reference_resolver::resolve_struct_from(const element& elem, const k::name& qualified_name) {
    if (qualified_name.empty()) return {};

    if (qualified_name.size() == 1) {
        // Simple name: look for aggregate directly in this element
        if (auto st_holder = dynamic_cast<const aggregate_holder*>(&elem)) {
            if (auto agg = st_holder->get_aggregate(qualified_name.front())) {
                return agg;
            }
        }
        return {};
    }

    // Qualified: first component is namespace or aggregate, rest continues recursively
    const auto& first = qualified_name.front();
    const auto  rest  = qualified_name.without_front();

    // Try child namespace
    if (auto nspc = dynamic_cast<const ns*>(&elem)) {
        if (auto child = nspc->get_child_namespace(first)) {
            if (auto st = resolve_struct_from(*child, rest)) {
                return st;
            }
        }
    }

    // Try nested aggregate
    if (auto st_holder = dynamic_cast<const aggregate_holder*>(&elem)) {
        if (auto agg = st_holder->get_aggregate(first)) {
            if (auto nested = resolve_struct_from(*agg, rest)) return nested;
        }
    }

    return {};
}

/**
 * Resolve a struct type from the root namespace of the unit.
 * name_without_prefix must have the :: prefix already stripped.
 *
 * Strategy (mirrors resolve_symbol_from_root):
 *  1. If the first component matches the last part of the module name, skip it
 *     and continue from the root namespace.
 *  2. Otherwise resolve directly from the root namespace (omit module prefix).
 */
std::shared_ptr<type>
type_reference_resolver::resolve_type_from_root(const k::name& name_without_prefix) {
    if (name_without_prefix.empty()) return {};

    auto root_ns = _unit.get_root_namespace();
    if (!root_ns) return {};

    const auto& unit_name = _unit.get_unit_name();

    // Strategy 1: first component is the module name last part
    if (!unit_name.empty() && name_without_prefix.front() == unit_name.back()) {
        auto rest = name_without_prefix.without_front();
        if (!rest.empty()) {
            if (auto st = resolve_struct_from(*root_ns, rest)) {
                return st->get_struct_type();
            }
        }
        // Fall through to strategy 2
    }

    // Strategy 2: resolve directly from root namespace (omit module prefix)
    if (auto st = resolve_struct_from(*root_ns, name_without_prefix)) {
        return st->get_struct_type();
    }

    // Strategy 3: fallback — search imported modules.
    if (auto agg = _unit.get_or_create_imported_aggregate(name_without_prefix, _context)) {
        return agg->get_struct_type();
    }
    // Strategy 4: fallback — search imported enums.
    if (auto en = _unit.get_or_create_imported_enum(name_without_prefix, _context)) {
        return en->get_enum_type();
    }

    return {};
}

/**
 * Resolve a type by name from the context of context_elem.
 * Handles simple names, qualified names, and root-prefixed names.
 * Falls back to context->from_string for primitive types.
 */
std::shared_ptr<type>
/**
 * Resolve a type by name from a context element, walking up the scope chain.
 *
 * Steps:
 *   1. Root-prefixed: delegate to resolve_type_from_root.
 *   2. Try primitive types via context->from_string.
 *   3. Walk the scope chain: aggregates, enums, using directives.
 *   4. Fallback: imported aggregates and enums.
 */
type_reference_resolver::resolve_type_by_name(const k::name& type_name, const element& context_elem) {
    debug("[type_reference_resolver::resolve_type_by_name] '{}'", {type_name.to_string()});
    // Step 1: Root-prefixed: delegate to resolve_type_from_root
    if (type_name.empty()) return {};

    // Root-prefixed: anchor at unit root
    if (type_name.has_root_prefix()) {
        return resolve_type_from_root(type_name.without_root_prefix());
    }

    // Step 2: Try primitive types via context->from_string
    // Try primitive types first via context (for simple names only)
    if (type_name.size() == 1) {
        auto prim = _context->from_string(type_name.front());
        if (prim && type::is_resolved(prim)) {
            return prim;
        }
    }

    // Step 3: Walk the scope chain: aggregates, enums, using directives
    // Walk up the scope chain looking for the type
    for (auto current = context_elem.shared_as<const element>(); current; current = current->parent<element>()) {
        if (auto st = resolve_struct_from(*current, type_name)) {
            return st->get_struct_type();
        }
        // Also look for enum types (simple names only for now)
        if (type_name.size() == 1) {
            if (auto eh = std::dynamic_pointer_cast<const enum_holder>(current)) {
                if (auto en = eh->get_enum(type_name.front())) {
                    return en->get_enum_type();
                }
            }
        }

        // Check using directives at this scope level for type resolution
        if (auto uh = std::dynamic_pointer_cast<const using_holder>(current)) {
            for (const auto& dir : uh->get_using_directives()) {
                if (dir.is_namespace() && !dir.has_alias()) {
                    // 'using namespace X::Y;' (anonymous) — search type_name within the target
                    auto target_elem = resolve_using_target(dir.target_name, _unit);
                    if (!target_elem) {
                        if (auto imp_agg = _unit.get_or_create_imported_aggregate(dir.target_name, _context)) {
                            target_elem = std::dynamic_pointer_cast<const element>(imp_agg);
                        }
                    }
                    if (target_elem) {
                        if (auto st = resolve_struct_from(*target_elem, type_name)) return st->get_struct_type();
                        if (type_name.size() == 1) {
                            if (auto eh = std::dynamic_pointer_cast<const enum_holder>(target_elem)) {
                                if (auto en = eh->get_enum(type_name.front())) {
                                    return en->get_enum_type();
                                }
                            }
                        }
                    }

                } else if (dir.is_namespace() && dir.has_alias()) {
                    // 'using M = namespace X::Y;' — M acts as a prefix: M::Type
                    if (type_name.front() == *dir.alias_name && type_name.size() > 1) {
                        auto rest = type_name.without_front();
                        auto target_elem = resolve_using_target(dir.target_name, _unit);
                        if (!target_elem) {
                            if (auto imp_agg = _unit.get_or_create_imported_aggregate(dir.target_name, _context)) {
                                target_elem = std::dynamic_pointer_cast<const element>(imp_agg);
                            }
                        }
                        if (target_elem) {
                            if (auto st = resolve_struct_from(*target_elem, rest)) return st->get_struct_type();
                            if (rest.size() == 1) {
                                if (auto eh = std::dynamic_pointer_cast<const enum_holder>(target_elem)) {
                                    if (auto en = eh->get_enum(rest.front())) {
                                        return en->get_enum_type();
                                    }
                                }
                            }
                        }
                        // Fallback: construct fully-qualified name and search imported modules
                        {
                            auto fq = dir.target_name;
                            for (size_t i = 0; i < rest.size(); ++i) fq = fq.with_back(rest[i]);
                            if (auto imp_agg = _unit.get_or_create_imported_aggregate(fq, _context)) {
                                return imp_agg->get_struct_type();
                            }
                            if (auto imp_en = _unit.get_or_create_imported_enum(fq, _context)) {
                                return imp_en->get_enum_type();
                            }
                        }
                    }

                } else {
                    // Specific using, with or without alias
                    const std::string& real_name = dir.target_name.back();
                    const std::string& lookup_name = dir.has_alias() ? *dir.alias_name : real_name;
                    if (type_name.front() == lookup_name) {
                        auto parent_name = dir.target_name.without_back();
                        std::shared_ptr<const element> parent_elem;
                        if (parent_name.empty()) {
                            parent_elem = _unit.get_root_namespace();
                        } else {
                            parent_elem = resolve_using_target(parent_name, _unit);
                            if (!parent_elem) {
                                if (auto imp_agg = _unit.get_or_create_imported_aggregate(parent_name, _context)) {
                                    parent_elem = std::dynamic_pointer_cast<const element>(imp_agg);
                                }
                            }
                        }
                        if (parent_elem) {
                            if (type_name.size() == 1) {
                                if (auto st = resolve_struct_from(*parent_elem, k::name{real_name})) return st->get_struct_type();
                                if (auto eh = std::dynamic_pointer_cast<const enum_holder>(parent_elem)) {
                                    if (auto en = eh->get_enum(real_name)) return en->get_enum_type();
                                }
                            } else {
                                if (auto ah = std::dynamic_pointer_cast<const aggregate_holder>(parent_elem)) {
                                    if (auto agg = ah->get_aggregate(real_name)) {
                                        if (auto st = resolve_struct_from(*agg, type_name.without_front())) return st->get_struct_type();
                                    }
                                }
                                if (auto nspc = std::dynamic_pointer_cast<const ns>(parent_elem)) {
                                    if (auto child = nspc->get_child_namespace(real_name)) {
                                        if (auto st = resolve_struct_from(*child, type_name.without_front())) return st->get_struct_type();
                                    }
                                }
                            }
                        }
                        // Fallback: try imported modules directly using the full target name
                        if (type_name.size() == 1) {
                            if (auto imp_agg = _unit.get_or_create_imported_aggregate(dir.target_name, _context)) {
                                return imp_agg->get_struct_type();
                            }
                            if (auto imp_en = _unit.get_or_create_imported_enum(dir.target_name, _context)) {
                                return imp_en->get_enum_type();
                            }
                        }
                    }
                }
            }
        }
    }

    // Step 4: Fallback: imported aggregates and enums
    // Fallback: search imported modules (scope chain exhausted)
    if (auto agg = _unit.get_or_create_imported_aggregate(type_name, _context)) {
        return agg->get_struct_type();
    }
    // Fallback: search imported enums
    if (auto en = _unit.get_or_create_imported_enum(type_name, _context)) {
        return en->get_enum_type();
    }

    return {};
}

void type_reference_resolver::resolve()
{
    trace("[type_reference_resolver::resolve] begin");
    visit_unit(_unit);
    trace("[type_reference_resolver::resolve] done");
}

//
// Overload collision helpers
//

namespace {
    // Build a human-readable parameter list string for a function overload.
    static std::string param_list_str(const std::vector<std::shared_ptr<model::parameter>>& params) {
        std::string s = "(";
        bool first = true;
        for (auto& p : params) {
            if (!first) s += ", ";
            s += p->get_type() ? p->get_type()->to_string() : "?";
            if (p->has_default_expr()) s += " = <default>";
            first = false;
        }
        s += ")";
        return s;
    }

    // Compute [min_arity, max_arity] for a function (accounting for default parameters).
    static std::pair<size_t,size_t> arity_range(const std::shared_ptr<model::function>& fn) {
        size_t min_a = 0, max_a = fn->parameters().size();
        for (auto& p : fn->parameters()) if (!p->has_default_expr()) ++min_a;
        return {min_a, max_a};
    }

    // True if ranges [a,b] and [c,d] overlap.
    static bool ranges_overlap(size_t a, size_t b, size_t c, size_t d) {
        return a <= d && c <= b;
    }
} // anonymous namespace

/**
 * Check all function overloads in a function_holder for arity-overlap collisions
 * caused by default-parameter values.
 *
 * For each pair of same-named overloads, if at least one has default params and their
 * arity ranges overlap, throw an ambiguity error.
 */
void type_reference_resolver::check_overload_collisions(function_holder& fh)
{
    // Collect all unique function names.
    std::set<std::string> names;
    for (auto& fn : fh.functions()) {
        names.insert(fn->get_short_name());
    }

    for (const auto& fname : names) {
        auto overloads = fh.get_functions(fname);
        if (overloads.size() < 2) continue;

        // For each pair (i < j), check for arity-range overlap when at least one has defaults.
        for (size_t i = 0; i < overloads.size(); ++i) {
            auto [min_i, max_i] = arity_range(overloads[i]);
            bool has_default_i = (min_i < max_i);

            for (size_t j = i + 1; j < overloads.size(); ++j) {
                auto [min_j, max_j] = arity_range(overloads[j]);
                bool has_default_j = (min_j < max_j);

                if ((has_default_i || has_default_j) && ranges_overlap(min_i, max_i, min_j, max_j)) {
                    lex::opt_any_lexeme fn_lexeme;
                    if (auto ast_fd = overloads[i]->get_ast_function_decl()) fn_lexeme = lex::any_lexeme{ast_fd->name};
                    throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_ARITH_TYPE_MISMATCH), fn_lexeme,
                        "Ambiguous overload: '{}{}' and overload '{}{}' can both be called with the same number of arguments "
                        "because of default parameter values; rename one overload or remove the default value(s) to resolve the ambiguity",
                        {fname, param_list_str(overloads[i]->parameters()),
                         fname, param_list_str(overloads[j]->parameters())});
                }
            }
        }
    }
}

void type_reference_resolver::check_constructor_overload_collisions(aggregate& st)
{
    const auto& ctors = st.constructors();
    if (ctors.size() < 2) return;

    for (size_t i = 0; i < ctors.size(); ++i) {
        auto [min_i, max_i] = arity_range(ctors[i]);
        bool has_default_i = (min_i < max_i);

        for (size_t j = i + 1; j < ctors.size(); ++j) {
            auto [min_j, max_j] = arity_range(ctors[j]);
            bool has_default_j = (min_j < max_j);

            if ((has_default_i || has_default_j) && ranges_overlap(min_i, max_i, min_j, max_j)) {
                const std::string& sname = st.get_short_name();
                throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_ARITH_NO_COMMON_TYPE), st.get_ast_aggregate_decl() ? lex::opt_any_lexeme{lex::any_lexeme{st.get_ast_aggregate_decl()->name}} : lex::opt_any_lexeme{},
                    "Ambiguous constructor overload in '{}': constructor '{}{}' and constructor '{}{}' can both be called with the same number of arguments "
                    "because of default parameter values; remove one constructor or remove the default value(s) to resolve the ambiguity",
                    {sname,
                     sname, param_list_str(ctors[i]->parameters()),
                     sname, param_list_str(ctors[j]->parameters())});
            }
        }
    }
}


std::shared_ptr<type>
/**
 * Resolve an unresolved_function_ref_type to a concrete function_reference_type.
 *
 * Steps:
 *   1. Resolve each parameter type via resolve_type_by_name / from_string.
 *   2. If member function reference: resolve the owner aggregate.
 *   3. Build using function_reference_type_builder.
 *   4. Cache the resolved type into the unresolved placeholder.
 */
type_reference_resolver::resolve_function_ref_type(
    const std::shared_ptr<unresolved_function_ref_type>& ufrt,
    const element& context_elem)
{
    if (!ufrt) return {};

    // Step 1: Resolve each parameter type via resolve_type_by_name / from_string
    // Resolve parameter types
    std::vector<std::shared_ptr<type>> resolved_params;
    for (const auto& pt : ufrt->parameter_types()) {
        std::shared_ptr<type> resolved;
        if (type::is_resolved(pt)) {
            resolved = pt;
        } else if (auto u = std::dynamic_pointer_cast<unresolved_type>(pt)) {
            resolved = resolve_type_by_name(u->type_id(), context_elem);
            if (!resolved || !type::is_resolved(resolved)) {
                resolved = _context->from_string(u->type_id());
            }
        } else {
            resolved = _context->resolve_type(pt);
        }
        if (!resolved || !type::is_resolved(resolved)) {
            throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_SIGNATURE_STRUCT_NOT_FOUND), std::nullopt,
                "Cannot resolve parameter type in function reference type",
                {});
        }
        resolved_params.push_back(resolved);
    }

    function_reference_type_builder builder(_context);
    builder.ref_kind(ufrt->get_ref_kind());
    for (const auto& rp : resolved_params) {
        builder.append_parameter_type(rp);
    }

    // Step 2: If member function reference: resolve the owner aggregate
    // Member function reference: resolve the owner structure
    if (!ufrt->owner_name().empty()) {
        auto owner_agg = resolve_struct_from(context_elem, ufrt->owner_name());
        if (!owner_agg) {
            // Try from root
            auto root_ns = _unit.get_root_namespace();
            if (root_ns) owner_agg = resolve_struct_from(*root_ns, ufrt->owner_name());
        }
        if (!owner_agg) {
            throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_SIGNATURE_STRUCT_WRONG_KIND), std::nullopt,
                "Cannot find owner struct '{}' for member function reference type",
                {ufrt->owner_name().to_string()});
        }
        // Accept both structure and klass as owner aggregates
        if (!std::dynamic_pointer_cast<structure>(owner_agg) &&
            !std::dynamic_pointer_cast<klass>(owner_agg)) {
            throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_SIGNATURE_ENUM_BAD_UNDERLYING), std::nullopt,
                "'{}' is not a structure or class; member function pointers require a struct/class owner",
                {ufrt->owner_name().to_string()});
        }
        builder.member_of(owner_agg);
    }

    // Step 3: Build using function_reference_type_builder
    auto resolved_type = builder.build();
    // Step 4: Cache the resolved type into the unresolved placeholder
    // Cache the resolved type into the unresolved placeholder
    const_cast<unresolved_function_ref_type*>(ufrt.get())->resolve(resolved_type);
    return resolved_type;
}

// ── Template instantiation from type reference ──────────────────────────────

std::shared_ptr<type> type_reference_resolver::try_instantiate_template_type(
    const std::shared_ptr<unresolved_type>& unres,
    const element& context_elem)
{
    const auto& base_name = unres->type_id();
    const auto& ast_args = unres->get_ast_template_args();

    // 1. Look up the template aggregate by base name (walking scope chain)
    std::shared_ptr<aggregate> tpl_agg;
    for (auto current = context_elem.shared_as<const element>(); current; current = current->parent<element>()) {
        if (auto st = resolve_struct_from(*current, base_name)) {
            if (st->is_template()) {
                tpl_agg = st;
                break;
            }
            // Found a non-template aggregate with that name — not a template instantiation
            return {};
        }
    }
    if (!tpl_agg) {
        // Try root namespace
        auto root_ns = _unit.get_root_namespace();
        if (root_ns) {
            if (auto st = resolve_struct_from(*root_ns, base_name)) {
                if (st->is_template()) tpl_agg = st;
            }
        }
    }
    if (!tpl_agg) return {};

    auto* ti = tpl_agg->get_tpl_info();
    if (!ti) return {};

    // 2. Validate argument count (allow fewer args if trailing params have defaults)
    if (ast_args.size() > ti->params.size()) return {};
    if (ast_args.size() < ti->params.size()) {
        for (size_t i = ast_args.size(); i < ti->params.size(); ++i) {
            auto& param = ti->params[i];
            if (param.is_type_param() && !param.default_type) return {};
            if (param.is_value_param() && !param.default_value.has_value()) return {};
        }
    }

    // 3. Convert AST template args to model template_arguments
    std::vector<template_argument> model_args;
    model_args.reserve(ti->params.size());
    for (size_t i = 0; i < ast_args.size(); ++i) {
        const auto& ast_arg = ast_args[i];
        if (ast_arg->is_type()) {
            // Resolve the type argument through the context
            auto arg_type = _context->from_type_specifier(*ast_arg->type_arg);
            if (!arg_type || !type::is_resolved(arg_type)) {
                // Try resolving it further through the resolver
                if (arg_type) {
                    if (auto unres_arg = std::dynamic_pointer_cast<unresolved_type>(arg_type)) {
                        auto resolved = resolve_type_by_name(unres_arg->type_id(), context_elem);
                        if (resolved && type::is_resolved(resolved)) {
                            arg_type = resolved;
                        }
                    }
                }
            }
            if (!arg_type || !type::is_resolved(arg_type)) return {};
            model_args.push_back(template_argument::make_type(arg_type));
        } else if (ast_arg->is_value()) {
            // Value template argument — extract compile-time constant literal
            k::value_type val;
            if (!extract_value_from_ast_expr(ast_arg->value_arg.get(), val)) return {};
            model_args.push_back(template_argument::make_value(val));
        } else {
            return {};
        }
    }
    // 3b. Fill in default arguments for missing trailing parameters
    for (size_t i = ast_args.size(); i < ti->params.size(); ++i) {
        auto& param = ti->params[i];
        if (param.is_type_param() && param.default_type) {
            auto def_type = param.default_type;
            if (!type::is_resolved(def_type)) {
                def_type = _context->resolve_type(def_type);
                if (!def_type || !type::is_resolved(def_type)) {
                    if (auto unres_def = std::dynamic_pointer_cast<unresolved_type>(param.default_type)) {
                        auto resolved = resolve_type_by_name(unres_def->type_id(), context_elem);
                        if (resolved && type::is_resolved(resolved)) def_type = resolved;
                    }
                }
            }
            if (!def_type || !type::is_resolved(def_type)) return {};
            model_args.push_back(template_argument::make_type(def_type));
        } else if (param.is_value_param() && param.default_value.has_value()) {
            model_args.push_back(template_argument::make_value(*param.default_value));
        } else {
            return {};
        }
    }

    // 3c. Resolve constraint types in template params if still unresolved
    for (auto& param : ti->params) {
        if (param.is_type_param() && param.constraint_type && !type::is_resolved(param.constraint_type)) {
            auto resolved = _context->resolve_type(param.constraint_type);
            if (resolved && type::is_resolved(resolved)) {
                param.constraint_type = resolved;
            } else if (auto unres = std::dynamic_pointer_cast<unresolved_type>(param.constraint_type)) {
                auto r = resolve_type_by_name(unres->type_id(), *tpl_agg);
                if (r && type::is_resolved(r)) param.constraint_type = r;
            }
        }
    }

    // 3d. Validate type constraints (kind filter + base-type constraint)
    {
        size_t err_idx;
        std::string err_reason;
        if (!validate_template_arg_constraints(ti->params, model_args, err_idx, err_reason)) {
            return {};  // Constraint violated — silently fail (error reporting TODO)
        }
    }

    // 4. Instantiate the template aggregate
    auto parent_ns_ptr = scope_lookup::enclosing_namespace(*tpl_agg);
    if (!parent_ns_ptr) return {};

    auto concrete_agg = template_instantiator::instantiate_aggregate(
        *tpl_agg, model_args, parent_ns_ptr, _unit, _context, *this);
    if (!concrete_agg) return {};

    // 5. If the concrete aggregate already has a struct_type, return it
    if (concrete_agg->get_struct_type()) {
        return concrete_agg->get_struct_type();
    }

    // 6. Create a struct_type for the freshly instantiated aggregate
    //    (mimics what symbol_resolver::visit_aggregate does)
    std::shared_ptr<struct_type> st_type{
        new struct_type(concrete_agg->get_short_name(), concrete_agg->shared_as<aggregate>())};
    _context->add_struct(st_type);
    concrete_agg->set_struct_type(st_type);

    // 6b. Create 'this' parameters for member functions (requires struct_type)
    for (auto& child : concrete_agg->get_children()) {
        if (auto fn = std::dynamic_pointer_cast<function>(child)) {
            if (fn->is_member() && !fn->is_static()) {
                fn->create_this_parameter();
            }
        }
    }
    // 6c. Assign FQ (fully-qualified) name to the concrete aggregate.
    //     symbol_resolver::visit_named_element normally does this, but the
    //     concrete aggregate was created after that pass already ran.
    //     Without a root-prefixed FQ name, update_mangled_name() produces
    //     an empty mangled name which breaks code generation and the JIT.
    if (concrete_agg->get_fq_name().empty() && !concrete_agg->get_short_name().empty()) {
        if (auto ancestor = concrete_agg->template ancestor<named_element>()) {
            concrete_agg->assign_name(ancestor->get_name().with_back(concrete_agg->get_short_name()));
        }
    }
    concrete_agg->update_mangled_name();

    // 6d. Update FQ names and mangled names for children (functions, constructors, etc.)
    for (auto& child : concrete_agg->get_children()) {
        if (auto fn = std::dynamic_pointer_cast<function>(child)) {
            // Build FQ name from parent chain (mirrors symbol_resolver::visit_named_element)
            if (fn->get_fq_name().empty() && !fn->get_short_name().empty()) {
                if (auto parent_named = fn->template parent<named_element>()) {
                    fn->assign_name(parent_named->get_name().with_back(fn->get_short_name()));
                }
            }
            fn->update_mangled_name();
        }
    }

    // 7. Resolve the LLVM struct type immediately (member types are already
    //    concrete thanks to the instantiator's type substitution)
    std::unordered_set<struct_type*> in_progress;
    _context->resolve_struct_type(st_type, in_progress);

    return st_type;
}

std::shared_ptr<type> type_reference_resolver::resolve_inner_type(
    const std::shared_ptr<type>& inner,
    const element* scope_elem)
{
    if (type::is_resolved(inner)) return inner;
    if (auto unres_inner = std::dynamic_pointer_cast<unresolved_type>(inner)) {

        // ── Template instantiation path ─────────────────────────────────
        // If the unresolved type carries AST template arguments (e.g. Box<int>),
        // look up the template definition, convert the AST args to model-level
        // template_argument values, instantiate, and return the concrete type.
        if (unres_inner->has_template_args() && scope_elem) {
            auto resolved = try_instantiate_template_type(unres_inner, *scope_elem);
            if (resolved && type::is_resolved(resolved)) return resolved;
            // If instantiation failed (e.g. not a template), fall through
            // to normal resolution for a better error message.
        }

        std::shared_ptr<type> resolved;
        if (scope_elem) {
            resolved = resolve_type_by_name(unres_inner->type_id(), *scope_elem);
        }
        if (!resolved || !type::is_resolved(resolved)) {
            resolved = _context->from_string(unres_inner->type_id());
        }
        if (!resolved || !type::is_resolved(resolved)) {
            auto imported_agg = _unit.get_or_create_imported_aggregate(unres_inner->type_id(), _context);
            if (imported_agg && imported_agg->get_struct_type()) resolved = imported_agg->get_struct_type();
        }
        if (!resolved || !type::is_resolved(resolved)) {
            auto imported_en = _unit.get_or_create_imported_enum(unres_inner->type_id(), _context);
            if (imported_en && imported_en->get_enum_type()) resolved = imported_en->get_enum_type();
        }
        return resolved;
    }
    return _context->resolve_type(inner);
}
std::shared_ptr<type> type_reference_resolver::strip_ref_array(const std::shared_ptr<type>& t) {
    if (auto ref = std::dynamic_pointer_cast<reference_type>(t)) {
        if (auto arr = std::dynamic_pointer_cast<array_type>(ref->get_subtype())) {
            if (!arr->is_sized()) return arr;
        }
    }
    return t;
}


/**
 * Visit a variable definition: resolve its type, process init expressions, and validate.
 *
 * Steps:
 *   1. Extract AST lexeme for error reporting.
 *   2. Phase 1: resolve the unresolved type (resolve_variable_type).
 *   3. Resolve init expressions via visitor.
 *   4. For function_reference_type: propagate return type from init symbol.
 *   5. Phase 2: dispatch to per-type-category validation.
 */
void type_reference_resolver::visit_variable_definition(variable_definition& var)
{
    // Step 1: Extract AST lexeme for error reporting
    // Extract AST lexeme for error reporting
    lex::opt_any_lexeme var_lexeme;
    if (auto* vs = dynamic_cast<variable_statement*>(&var)) {
        if (auto ast_vd = vs->get_ast_variable_decl()) var_lexeme = lex::any_lexeme{ast_vd->name};
    } else if (auto* var_elem = dynamic_cast<element*>(&var)) {
        if (auto ast_node = var_elem->get_ast_node()) {
            if (auto ast_vd = std::dynamic_pointer_cast<k::parse::ast::variable_decl>(ast_node))
                var_lexeme = lex::any_lexeme{ast_vd->name};
        }
    }

    // Step 2: Phase 1: resolve the unresolved type (resolve_variable_type)
    // Phase 1: resolve the unresolved type
    resolve_variable_type(var, var_lexeme);

    // Step 3: Resolve init expressions via visitor
    // Resolve init expressions if any
    auto init_expr_base = var.get_init_expr();
    if (init_expr_base) {
        init_expr_base->accept(*this);
    }

    // Step 4: For function_reference_type: propagate return type from init symbol
    auto init_expr = std::dynamic_pointer_cast<constructor_invocation_expression>(init_expr_base);

    // If the variable has a function_reference_type with no return type yet (e.g. 'fp : *(int) = add_one'),
    // propagate the return type from the initializer's function symbol into the existing frt in-place.
    // IMPORTANT: we must NOT replace the frt object (var.set_type(new_frt)) because any reference_type
    // wrapping it (ref<frt>) holds a weak_ptr to frt -- replacing frt would expire those weak_ptrs and
    // cause use-after-free crashes in is_resolved() checks.  Mutating the existing object is safe.
    if (auto frt = std::dynamic_pointer_cast<function_reference_type>(var.get_type())) {
        if (!frt->get_return_type() && init_expr && !init_expr->empty()) {
            if (auto sym = std::dynamic_pointer_cast<symbol_expression>(init_expr->argument(0))) {
                if (sym->is_function() && sym->get_function()) {
                    auto fn_ret = sym->get_function()->get_return_type();
                    if (fn_ret) {
                        // Mutate in place -- preserves all existing weak_ptr references to frt
                        frt->set_return_type(fn_ret);
                    }
                }
            }
        }
    }

    // Step 5: Phase 2: dispatch to per-type-category validation
    // Phase 2: validate init expression per type category
    auto var_type = var.get_type();
    var_init_context ctx{var, var_lexeme, var_type, init_expr_base, init_expr};

    if (type::is_primitive(var_type)) {
        validate_primitive_variable(ctx);
    } else if (std::dynamic_pointer_cast<struct_type>(var_type)) {
        validate_struct_variable(ctx);
    } else if (type::is_reference(var_type)) {
        validate_reference_variable(ctx);
    } else if (type::is_pointer(var_type)) {
        validate_pointer_variable(ctx);
    } else if (type::is_link(var_type)) {
        validate_link_variable(ctx);
    } else if (type::is_view(var_type)) {
        validate_view_variable(ctx);
    } else if (type::is_owner(var_type)) {
        validate_owner_variable(ctx);
    } else if (type::is_sized_array(var_type)) {
        validate_sized_array_variable(ctx);
    } else {
        // Unsupported construction for other types for now
        // TODO Support construction for other types (array, etc.)
    }
}

/**
 * Compare two types allowing const-widening on array elements.
 * Returns true when:
 *   - src_nc == tgt_nc (identity), or
 *   - both are (unsized) array types whose element types match after
 *     stripping const (e.g. array<char> matches array<const<char>>).
 * The caller is responsible for ensuring the conversion direction is safe
 * (source non-const → target const is widening, reverse is not).
 */
bool type_reference_resolver::types_match_array_const_compatible(
        const std::shared_ptr<type>& src_nc,
        const std::shared_ptr<type>& tgt_nc) {
    if (src_nc == tgt_nc) return true;
    auto src_arr = std::dynamic_pointer_cast<array_type>(src_nc);
    auto tgt_arr = std::dynamic_pointer_cast<array_type>(tgt_nc);
    if (src_arr && tgt_arr && !src_arr->is_sized() && !tgt_arr->is_sized()) {
        auto src_elem = type::remove_const(src_arr->get_subtype());
        auto tgt_elem = type::remove_const(tgt_arr->get_subtype());
        return src_elem == tgt_elem;
    }
    return false;
}

type_reference_resolver::cast_weight
/**
 * Compute the cost (weight) of an implicit conversion from expr's type to the target type.
 *
 * Returns a cast_weight value (NONE, REF_CONV, WIDENING, NARROWING, CONSTRUCT, IMPOSSIBLE).
 *
 * Steps:
 *   1. Function reference type cases.
 *   2. Pointer/link/view/owner/drain cases: same-kind, cross-kind, ref borrow, struct upcast.
 *   3. ref<owner/ptr/lnk/view/drain> unwrapping.
 *   4. Double reference: unwrap one level.
 *   5. Reference cases: const widening, struct upcast, value load, primitive cast.
 *   6. Null → pointer/link/view/owner.
 *   7. Enum, struct, primitive conversions.
 */
type_reference_resolver::compute_cast_weight(const std::shared_ptr<expression>& expr, const std::shared_ptr<k::model::type>& tgt) {
    if (!expr || !type::is_resolved(tgt) || !type::is_resolved(expr->get_type())) {
        return CAST_IMPOSSIBLE;
    }

    auto type_src = expr->get_type();

    // Strip const from both sides: const T and T are interchangeable for value conversions.
    // Const-checking for assignment targets is done separately in visit_assignation_expression.
    auto tgt_nc = type::remove_const(tgt);

    // Step 1: Function reference type cases
    // ── Function reference type cases ─────────────────────────────────────────
    // frt → frt (any ref_kind combination): free conversion (same LLVM type).
    // ref<frt> → frt: allowed (load from variable / direct function address).
    if (auto tgt_frt = std::dynamic_pointer_cast<function_reference_type>(tgt_nc)) {
        if (std::dynamic_pointer_cast<function_reference_type>(type_src)) {
            return CAST_NONE;
        }
        if (type::is_reference(type_src)) {
            auto src_inner = type::remove_const(std::dynamic_pointer_cast<reference_type>(type_src)->get_subtype());
            if (std::dynamic_pointer_cast<function_reference_type>(src_inner)) {
                return CAST_REF_CONV; // ref<frt> → frt: load needed
            }
        }
        return CAST_IMPOSSIBLE;
    }
    // ref<frt> → ref<frt>: pass through.
    if (type::is_reference(tgt_nc)) {
        auto tgt_sub_nc = type::remove_const(std::dynamic_pointer_cast<reference_type>(tgt_nc)->get_subtype());
        if (std::dynamic_pointer_cast<function_reference_type>(tgt_sub_nc)) {
            if (type_src == tgt_nc || type_src == tgt) return CAST_NONE;
            if (type::is_reference(type_src)) {
                auto src_sub = type::remove_const(std::dynamic_pointer_cast<reference_type>(type_src)->get_subtype());
                if (std::dynamic_pointer_cast<function_reference_type>(src_sub)) return CAST_NONE;
            }
            return CAST_IMPOSSIBLE;
        }
    }
    // ── End function reference type cases ─────────────────────────────────────

    // --- Pointer cases ---
    if (type::is_pointer(type_src)) {
        if (type::is_pointer(tgt_nc) || type::is_link(tgt_nc)) {
            // For pointer weight, compare after stripping const on pointed types (widening const allowed).
            auto src_sub = type_src->get_subtype();
            auto tgt_sub = tgt_nc->get_subtype();
            auto src_sub_nc = type::remove_const(src_sub);
            auto tgt_sub_nc = type::remove_const(tgt_sub);
            if (src_sub_nc == tgt_sub_nc) {
                // const T* <- T*: allowed (widening), T* <- const T*: forbidden
                if (type::is_const(src_sub) && !type::is_const(tgt_sub)) return CAST_IMPOSSIBLE;
                return (type_src == tgt) ? CAST_NONE : CAST_WIDENING;
            }
            // Struct upcast: ptr<Derived> → ptr<Base> or ptr<Derived> → lien<Base>
            auto src_st_type = std::dynamic_pointer_cast<struct_type>(src_sub_nc);
            auto tgt_st_type = std::dynamic_pointer_cast<struct_type>(tgt_sub_nc);
            if (src_st_type && tgt_st_type) {
                auto src_st = src_st_type->get_struct();
                auto tgt_st = tgt_st_type->get_struct();
                if (src_st && tgt_st && src_st->is_derived_from(tgt_st)) {
                    return CAST_REF_CONV;
                }
            }
            return CAST_IMPOSSIBLE;
        }
        // ptr<T> → ref<T>: borrow pointer target as reference (same LLVM representation)
        if (type::is_reference(tgt_nc)) {
            auto tgt_sub_nc = type::remove_const(std::dynamic_pointer_cast<reference_type>(tgt_nc)->get_subtype());
            auto src_sub_nc = type::remove_const(type_src->get_subtype());
            if (src_sub_nc == tgt_sub_nc || types_match_array_const_compatible(src_sub_nc, tgt_sub_nc))
                return CAST_WIDENING;
        }
        return CAST_IMPOSSIBLE;
    }

    // --- Link cases ---
    if (type::is_link(type_src)) {
        if (type::is_link(tgt_nc) || type::is_pointer(tgt_nc)) {
            auto src_sub = type_src->get_subtype();
            auto tgt_sub = tgt_nc->get_subtype();
            auto src_sub_nc = type::remove_const(src_sub);
            auto tgt_sub_nc = type::remove_const(tgt_sub);
            if (src_sub_nc == tgt_sub_nc) {
                if (type::is_const(src_sub) && !type::is_const(tgt_sub)) return CAST_IMPOSSIBLE;
                return (type_src == tgt) ? CAST_NONE : CAST_WIDENING;
            }
            // Struct upcast: lien<Derived> → lien<Base> or lien<Derived> → ptr<Base>
            auto src_st_type = std::dynamic_pointer_cast<struct_type>(src_sub_nc);
            auto tgt_st_type = std::dynamic_pointer_cast<struct_type>(tgt_sub_nc);
            if (src_st_type && tgt_st_type) {
                auto src_st = src_st_type->get_struct();
                auto tgt_st = tgt_st_type->get_struct();
                if (src_st && tgt_st && src_st->is_derived_from(tgt_st)) {
                    return CAST_REF_CONV;
                }
            }
            return CAST_IMPOSSIBLE;
        }
        // lnk<T> → ref<T>: borrow link target as reference
        if (type::is_reference(tgt_nc)) {
            auto tgt_sub_nc = type::remove_const(std::dynamic_pointer_cast<reference_type>(tgt_nc)->get_subtype());
            auto src_sub_nc = type::remove_const(type_src->get_subtype());
            if (src_sub_nc == tgt_sub_nc || types_match_array_const_compatible(src_sub_nc, tgt_sub_nc))
                return CAST_WIDENING;
        }
        return CAST_IMPOSSIBLE;
    }

    // --- Pinned cases ---
    if (type::is_view(type_src)) {
        if (type::is_view(tgt_nc) || type::is_pointer(tgt_nc)) {
            auto src_sub = type_src->get_subtype();
            auto tgt_sub = tgt_nc->get_subtype();
            auto src_sub_nc = type::remove_const(src_sub);
            auto tgt_sub_nc = type::remove_const(tgt_sub);
            if (src_sub_nc == tgt_sub_nc) {
                if (type::is_const(src_sub) && !type::is_const(tgt_sub)) return CAST_IMPOSSIBLE;
                return (type_src == tgt) ? CAST_NONE : CAST_WIDENING;
            }
            // Struct upcast: pin<Derived> → pin<Base> or pin<Derived> → ptr<Base>
            auto src_st_type = std::dynamic_pointer_cast<struct_type>(src_sub_nc);
            auto tgt_st_type = std::dynamic_pointer_cast<struct_type>(tgt_sub_nc);
            if (src_st_type && tgt_st_type) {
                auto src_st = src_st_type->get_struct();
                auto tgt_st = tgt_st_type->get_struct();
                if (src_st && tgt_st && src_st->is_derived_from(tgt_st)) {
                    return CAST_REF_CONV;
                }
            }
            return CAST_IMPOSSIBLE;
        }
        // view<T> → ref<T>: borrow view target as reference
        if (type::is_reference(tgt_nc)) {
            auto tgt_sub_nc = type::remove_const(std::dynamic_pointer_cast<reference_type>(tgt_nc)->get_subtype());
            auto src_sub_nc = type::remove_const(type_src->get_subtype());
            if (src_sub_nc == tgt_sub_nc || types_match_array_const_compatible(src_sub_nc, tgt_sub_nc))
                return CAST_WIDENING;
        }
        return CAST_IMPOSSIBLE;
    }

    // Step 2: Pointer/link/view/owner/drain cases: same-kind, cross-kind, ref borrow, struct upcast
    // --- Owner cases ---
    // owner<T> → owner<T>  : move (CAST_NONE)
    // owner<T> → owner<Base>: implicit upcast-move (CAST_REF_CONV)
    // owner<T> → ptr<T>    : borrow/observer view (CAST_WIDENING, no ownership transfer)
    if (type::is_owner(type_src)) {
        auto src_sub = type_src->get_subtype();
        auto src_sub_nc = type::remove_const(src_sub);
        if (type::is_owner(tgt_nc)) {
            auto tgt_sub = tgt_nc->get_subtype();
            auto tgt_sub_nc = type::remove_const(tgt_sub);
            if (src_sub_nc == tgt_sub_nc) {
                return (type_src == tgt) ? CAST_NONE : CAST_WIDENING;
            }
            // Upcast: owner<Derived> → owner<Base>
            auto src_st = std::dynamic_pointer_cast<struct_type>(src_sub_nc);
            auto tgt_st = std::dynamic_pointer_cast<struct_type>(tgt_sub_nc);
            if (src_st && tgt_st && src_st->get_struct() && tgt_st->get_struct() &&
                src_st->get_struct()->is_derived_from(tgt_st->get_struct())) {
                return CAST_REF_CONV;
            }
            return CAST_IMPOSSIBLE;
        }
        if (type::is_pointer(tgt_nc)) {
            // owner<T> as an observer pointer: address is borrowed, no ownership change
            auto tgt_sub = tgt_nc->get_subtype();
            auto tgt_sub_nc = type::remove_const(tgt_sub);
            if (src_sub_nc == tgt_sub_nc || types_match_array_const_compatible(src_sub_nc, tgt_sub_nc)) {
                return CAST_WIDENING;
            }
            // Upcast observer: owner<Derived> → ptr<Base>
            auto src_st = std::dynamic_pointer_cast<struct_type>(src_sub_nc);
            auto tgt_st = std::dynamic_pointer_cast<struct_type>(tgt_sub_nc);
            if (src_st && tgt_st && src_st->get_struct() && tgt_st->get_struct() &&
                src_st->get_struct()->is_derived_from(tgt_st->get_struct())) {
                return CAST_REF_CONV;
            }
            return CAST_IMPOSSIBLE;
        }
        // owner<T> → lnk<T> / view<T>: borrow as link or view
        if (type::is_link(tgt_nc) || type::is_view(tgt_nc)) {
            auto tgt_sub_nc = type::remove_const(tgt_nc->get_subtype());
            if (src_sub_nc == tgt_sub_nc || types_match_array_const_compatible(src_sub_nc, tgt_sub_nc))
                return CAST_WIDENING;
        }
        // owner<T> → ref<T>: borrow owned object as reference
        if (type::is_reference(tgt_nc)) {
            auto tgt_sub_nc = type::remove_const(std::dynamic_pointer_cast<reference_type>(tgt_nc)->get_subtype());
            if (src_sub_nc == tgt_sub_nc || types_match_array_const_compatible(src_sub_nc, tgt_sub_nc))
                return CAST_WIDENING;
        }
        return CAST_IMPOSSIBLE;
    }

    // --- Drain cases ---
    // drain<T> behaves like a reference for conversion purposes, except that
    // a reference is NOT implicitly convertible to a drain (explicit # required).
    if (type::is_drain(type_src)) {
        auto drn = std::dynamic_pointer_cast<drain_type>(type_src);
        auto src_sub = drn->get_drained_type();
        auto src_sub_nc = type::remove_const(src_sub);

        // drain<T> → drain<T>: identity
        if (type::is_drain(tgt_nc)) {
            auto tgt_sub_nc = type::remove_const(tgt_nc->get_subtype());
            if (src_sub_nc == tgt_sub_nc) return CAST_NONE;
            // Struct upcast: drain<Derived> → drain<Base>
            auto src_st = std::dynamic_pointer_cast<struct_type>(src_sub_nc);
            auto tgt_st = std::dynamic_pointer_cast<struct_type>(tgt_sub_nc);
            if (src_st && tgt_st && src_st->get_struct() && tgt_st->get_struct() &&
                src_st->get_struct()->is_derived_from(tgt_st->get_struct())) {
                return CAST_REF_CONV;
            }
            return CAST_IMPOSSIBLE;
        }
        // drain<T> → ref<T>: implicit (drain can always be used as a reference)
        if (type::is_reference(tgt_nc)) {
            auto tgt_sub_nc = type::remove_const(std::dynamic_pointer_cast<reference_type>(tgt_nc)->get_subtype());
            if (src_sub_nc == tgt_sub_nc) return CAST_REF_CONV;
            if (types_match_array_const_compatible(src_sub_nc, tgt_sub_nc)) return CAST_REF_CONV;
            // Struct upcast: drain<Derived> → ref<Base>
            auto src_st = std::dynamic_pointer_cast<struct_type>(src_sub_nc);
            auto tgt_st = std::dynamic_pointer_cast<struct_type>(tgt_sub_nc);
            if (src_st && tgt_st && src_st->get_struct() && tgt_st->get_struct() &&
                src_st->get_struct()->is_derived_from(tgt_st->get_struct())) {
                return CAST_REF_CONV;
            }
            return CAST_IMPOSSIBLE;
        }
        // drain<T> → link<T>: implicit borrow
        if (type::is_link(tgt_nc)) {
            auto tgt_sub_nc = type::remove_const(tgt_nc->get_subtype());
            if (src_sub_nc == tgt_sub_nc) return CAST_REF_CONV;
            return CAST_IMPOSSIBLE;
        }
        // drain<T> → view<T> / ptr<T>: implicit borrow
        if (type::is_view(tgt_nc) || type::is_pointer(tgt_nc)) {
            auto tgt_sub_nc = type::remove_const(tgt_nc->get_subtype());
            if (src_sub_nc == tgt_sub_nc) return CAST_REF_CONV;
            return CAST_IMPOSSIBLE;
        }
        // drain<T> → value T: load through drain (like ref → value)
        if (src_sub_nc == tgt_nc) return CAST_REF_CONV;

        // drain<T> → different primitive: load + cast
        auto prim_sub = std::dynamic_pointer_cast<primitive_type>(src_sub_nc);
        auto prim_tgt_d = std::dynamic_pointer_cast<primitive_type>(tgt_nc);
        if (prim_sub && prim_tgt_d) {
            if (*prim_sub == *prim_tgt_d) return CAST_REF_CONV;
            if (prim_sub->is_integer() && prim_tgt_d->is_integer() &&
                prim_sub->is_unsigned() == prim_tgt_d->is_unsigned() &&
                prim_tgt_d->type_size() >= prim_sub->type_size()) {
                return CAST_WIDENING;
            }
            if (prim_sub->is_float() && prim_tgt_d->is_float() &&
                prim_tgt_d->type_size() >= prim_sub->type_size()) {
                return CAST_WIDENING;
            }
            return CAST_NARROWING;
        }

        return CAST_IMPOSSIBLE;
    }

    // Step 3: ref<owner/ptr/lnk/view/drain> unwrapping
    // ref<owner<T>> → ptr<T>: load owner and borrow as pointer
    // Also handles ref<const<owner<T>>> (const class member) → ptr<T>
    if (type::is_reference(type_src) && !type::is_double_reference(type_src)) {
        auto ref_src = std::dynamic_pointer_cast<reference_type>(type_src);
        auto inner = ref_src->get_subtype();
        auto inner_nc = type::remove_const(inner);
        if (type::is_owner(inner_nc)) {
            auto own_sub = inner_nc->get_subtype();
            auto own_sub_nc = type::remove_const(own_sub);
            if (type::is_pointer(tgt_nc)) {
                auto tgt_sub_nc = type::remove_const(tgt_nc->get_subtype());
                if (own_sub_nc == tgt_sub_nc || types_match_array_const_compatible(own_sub_nc, tgt_sub_nc))
                    return CAST_REF_CONV;
                auto src_st = std::dynamic_pointer_cast<struct_type>(own_sub_nc);
                auto tgt_st = std::dynamic_pointer_cast<struct_type>(tgt_sub_nc);
                if (src_st && tgt_st && src_st->get_struct() && tgt_st->get_struct() &&
                    src_st->get_struct()->is_derived_from(tgt_st->get_struct())) {
                    return CAST_REF_CONV;
                }
            }
            if (type::is_owner(tgt_nc)) {
                // Move from ref<owner>: allowed (load the owner, move it)
                auto tgt_sub_nc = type::remove_const(tgt_nc->get_subtype());
                if (own_sub_nc == tgt_sub_nc) return CAST_REF_CONV;
            }
            // ref<owner<T>> → lnk<T> / view<T>: load owner, borrow as link or view
            if (type::is_link(tgt_nc) || type::is_view(tgt_nc)) {
                auto tgt_sub_nc = type::remove_const(tgt_nc->get_subtype());
                if (own_sub_nc == tgt_sub_nc || types_match_array_const_compatible(own_sub_nc, tgt_sub_nc))
                    return CAST_REF_CONV;
            }
            // ref<owner<T>> → ref<T>: load owner pointer value, borrow as reference
            if (type::is_reference(tgt_nc)) {
                auto tgt_sub_nc = type::remove_const(std::dynamic_pointer_cast<reference_type>(tgt_nc)->get_subtype());
                if (own_sub_nc == tgt_sub_nc || types_match_array_const_compatible(own_sub_nc, tgt_sub_nc))
                    return CAST_REF_CONV;
            }
        }
    }

    // --- ref<ptr/lnk/pin> → ptr/lnk/pin: unwrap the ref and delegate ─────────
    // Allows passing a ref<ptr<Derived>> where a ptr<Base> is expected, for example.
    if (type::is_reference(type_src) && !type::is_double_reference(type_src)) {
        auto ref_src = std::dynamic_pointer_cast<reference_type>(type_src);
        auto inner = ref_src->get_subtype();
        if (type::is_pointer(inner) || type::is_link(inner) || type::is_view(inner)) {
            if (type::is_pointer(tgt_nc) || type::is_link(tgt_nc) || type::is_view(tgt_nc)) {
                auto src_sub = inner->get_subtype();
                auto tgt_sub = tgt_nc->get_subtype();
                auto src_sub_nc = type::remove_const(src_sub);
                auto tgt_sub_nc = type::remove_const(tgt_sub);
                if (src_sub_nc == tgt_sub_nc) {
                    if (type::is_const(src_sub) && !type::is_const(tgt_sub)) return CAST_IMPOSSIBLE;
                    return CAST_REF_CONV;
                }
                // Struct upcast: ref<lnk/pin/ptr<Derived>> → lnk/pin/ptr<Base>
                auto src_st_type = std::dynamic_pointer_cast<struct_type>(src_sub_nc);
                auto tgt_st_type = std::dynamic_pointer_cast<struct_type>(tgt_sub_nc);
                if (src_st_type && tgt_st_type) {
                    auto src_st = src_st_type->get_struct();
                    auto tgt_st = tgt_st_type->get_struct();
                    if (src_st && tgt_st && src_st->is_derived_from(tgt_st)) {
                        return CAST_REF_CONV;
                    }
                }
                return CAST_IMPOSSIBLE;
            }
            // ref<ptr/lnk/view<T>> → ref<T>: load indirection value, use as reference
            if (type::is_reference(tgt_nc)) {
                auto tgt_sub_nc = type::remove_const(std::dynamic_pointer_cast<reference_type>(tgt_nc)->get_subtype());
                auto src_sub_nc = type::remove_const(inner->get_subtype());
                if (src_sub_nc == tgt_sub_nc || types_match_array_const_compatible(src_sub_nc, tgt_sub_nc))
                    return CAST_REF_CONV;
            }
        }
        // --- ref<drain<T>> → ...: unwrap the ref, then check drain conversions ──
        if (type::is_drain(inner)) {
            auto drn_sub_nc = type::remove_const(inner->get_subtype());
            // ref<drain<T>> → drain<T>: load the stored drain
            if (type::is_drain(tgt_nc)) {
                auto tgt_sub_nc = type::remove_const(tgt_nc->get_subtype());
                if (drn_sub_nc == tgt_sub_nc) return CAST_REF_CONV;
            }
            // ref<drain<T>> → ref<T>: load drain, use as ref
            if (type::is_reference(tgt_nc)) {
                auto tgt_sub_nc = type::remove_const(std::dynamic_pointer_cast<reference_type>(tgt_nc)->get_subtype());
                if (drn_sub_nc == tgt_sub_nc) return CAST_REF_CONV;
            }
            // ref<drain<T>> → T: load drain, load value
            if (drn_sub_nc == tgt_nc) return CAST_REF_CONV;
            // ref<drain<T>> → link<T>, view<T>, ptr<T>: load drain, borrow
            if (type::is_link(tgt_nc) || type::is_view(tgt_nc) || type::is_pointer(tgt_nc)) {
                auto tgt_sub_nc = type::remove_const(tgt_nc->get_subtype());
                if (drn_sub_nc == tgt_sub_nc) return CAST_REF_CONV;
            }
            // ref<drain<T>> → different primitive: load + cast
            auto prim_drn = std::dynamic_pointer_cast<primitive_type>(drn_sub_nc);
            auto prim_tgt_w = std::dynamic_pointer_cast<primitive_type>(tgt_nc);
            if (prim_drn && prim_tgt_w) {
                if (*prim_drn == *prim_tgt_w) return CAST_REF_CONV;
                return CAST_WIDENING;
            }
        }
    }

    // Step 4: Double reference: unwrap one level
    // --- Double reference: unwrap one level ---
    std::shared_ptr<k::model::type> effective_src = type_src;
    if (type::is_double_reference(type_src)) {
        effective_src = std::dynamic_pointer_cast<reference_type>(type_src)->get_subtype();
    }

    // --- Reference cases ---
    if (type::is_reference(effective_src)) {
        auto ref_src = std::dynamic_pointer_cast<reference_type>(effective_src);
        if (type::is_reference(tgt_nc)) {
            if (effective_src == tgt_nc) return CAST_NONE;
            // Mutable → const widening: ref<T> → ref<const T>
            auto src_sub = ref_src->get_referenced_type();
            auto tgt_ref = std::dynamic_pointer_cast<reference_type>(tgt_nc);
            auto tgt_sub = tgt_ref->get_referenced_type();
            auto src_sub_nc = type::remove_const(src_sub);
            auto tgt_sub_nc = type::remove_const(tgt_sub);
            // ref<T> → ref<const T>: allowed if base types are the same (strip const)
            if (src_sub_nc == tgt_sub_nc) {
                // If target has const but source doesn't: widening
                if (type::is_const(tgt_sub) && !type::is_const(src_sub)) return CAST_WIDENING;
                // If source has const but target doesn't: narrowing (forbidden for assignments)
                if (type::is_const(src_sub) && !type::is_const(tgt_sub)) return CAST_IMPOSSIBLE;
                // Same constness (both or neither): same type, already handled above
                return CAST_NONE;
            }
            // Array element const-widening: ref<array<T>> → ref<array<const<T>>>
            if (types_match_array_const_compatible(src_sub_nc, tgt_sub_nc)) {
                return CAST_WIDENING;
            }
            // ref<T[N]> → ref<T[]>: sized array to unsized array widening
            // Also handles ref<const T[N]> → ref<const T[]> (const element types match)
            if (auto src_sized = std::dynamic_pointer_cast<sized_array_type>(src_sub_nc)) {
                auto tgt_arr = std::dynamic_pointer_cast<array_type>(tgt_sub_nc);
                if (tgt_arr && !tgt_arr->is_sized()) {
                    // Element types must match (after stripping const)
                    auto src_elem = type::remove_const(src_sized->get_subtype());
                    auto tgt_elem = type::remove_const(tgt_arr->get_subtype());
                    if (src_elem == tgt_elem) {
                        return CAST_WIDENING;
                    }
                }
            }
            // Check struct upcast: ref<Derived> → ref<Base> (also handle const variants)
            auto src_st_type = std::dynamic_pointer_cast<struct_type>(src_sub_nc);
            auto tgt_st_type = std::dynamic_pointer_cast<struct_type>(tgt_sub_nc);
            if (src_st_type && tgt_st_type) {
                auto src_st = src_st_type->get_struct();
                auto tgt_st = tgt_st_type->get_struct();
                if (src_st && tgt_st && src_st->is_derived_from(tgt_st)) {
                    return CAST_REF_CONV;
                }
            }
            return CAST_IMPOSSIBLE;
        }
        // ref -> value: need a load
        auto sub = type::remove_const(ref_src->get_subtype());
        if (sub == tgt_nc) {
            return CAST_REF_CONV;
        }
        // ref<T> → link<T>: passing an object by reference as a link parameter (implicit borrow)
        if (type::is_link(tgt_nc)) {
            auto tgt_sub_nc = type::remove_const(tgt_nc->get_subtype());
            if (sub == tgt_sub_nc) return CAST_REF_CONV;
            // Also check struct upcast: ref<Derived> → link<Base>
            auto src_st = std::dynamic_pointer_cast<struct_type>(sub);
            auto tgt_st = std::dynamic_pointer_cast<struct_type>(tgt_sub_nc);
            if (src_st && tgt_st && src_st->get_struct() && tgt_st->get_struct() &&
                src_st->get_struct()->is_derived_from(tgt_st->get_struct())) {
                return CAST_REF_CONV;
            }
        }
        // ref<T> → view<T>: passing an object as a view reference
        if (type::is_view(tgt_nc)) {
            auto tgt_sub_nc = type::remove_const(tgt_nc->get_subtype());
            if (sub == tgt_sub_nc) return CAST_REF_CONV;
        }
        // ref -> different primitive: load + cast
        auto prim_sub = std::dynamic_pointer_cast<primitive_type>(sub);
        auto prim_tgt = std::dynamic_pointer_cast<primitive_type>(tgt_nc);
        if (prim_sub && prim_tgt) {
            if (*prim_sub == *prim_tgt) return CAST_REF_CONV;
            // Widening: same category, target is larger or same
            if (prim_sub->is_integer() && prim_tgt->is_integer() &&
                prim_sub->is_unsigned() == prim_tgt->is_unsigned() &&
                prim_tgt->type_size() >= prim_sub->type_size()) {
                return CAST_WIDENING;
            }
            if (prim_sub->is_float() && prim_tgt->is_float() &&
                prim_tgt->type_size() >= prim_sub->type_size()) {
                return CAST_WIDENING;
            }
            // Everything else between primitives is narrowing
            return CAST_NARROWING;
        }
        // ref<enum> → value: load + convert
        auto enum_sub = std::dynamic_pointer_cast<enum_type>(sub);
        if (enum_sub) {
            auto enum_tgt = std::dynamic_pointer_cast<enum_type>(tgt_nc);
            if (enum_tgt) {
                if (enum_sub->get_enumeration() == enum_tgt->get_enumeration()) {
                    return CAST_REF_CONV;
                }
                // Upcast only: ref<Derived> → Base
                auto src_en = enum_sub->get_enumeration();
                auto tgt_en = enum_tgt->get_enumeration();
                if (src_en && tgt_en && src_en->is_derived_from(tgt_en)) {
                    return CAST_WIDENING;
                }
                return CAST_IMPOSSIBLE;
            }
            if (prim_tgt) return CAST_WIDENING;
        }
        return CAST_IMPOSSIBLE;
    }

    // --- Enum implicit conversions ---
    auto eff_src_nc = type::remove_const(effective_src);
    auto enum_eff_src = std::dynamic_pointer_cast<enum_type>(eff_src_nc);
    auto enum_eff_tgt = std::dynamic_pointer_cast<enum_type>(tgt_nc);

    // enum → same enum: identity
    if (enum_eff_src && enum_eff_tgt && enum_eff_src->get_enumeration() == enum_eff_tgt->get_enumeration()) {
        return CAST_NONE;
    }
    // enum → different enum: only Derived → Base (upcast) is allowed
    if (enum_eff_src && enum_eff_tgt) {
        auto src_en = enum_eff_src->get_enumeration();
        auto tgt_en = enum_eff_tgt->get_enumeration();
        if (src_en && tgt_en && src_en->is_derived_from(tgt_en)) {
            return CAST_WIDENING; // upcast: Derived → Base
        }
        // Base → Derived (downcast) or unrelated enums: not allowed
        return CAST_IMPOSSIBLE;
    }
    // enum → primitive: widening (implicit)
    if (enum_eff_src && !enum_eff_tgt) {
        auto p_tgt = std::dynamic_pointer_cast<primitive_type>(tgt_nc);
        if (p_tgt) return CAST_WIDENING;
    }
    // primitive → enum: widening (implicit)
    if (!enum_eff_src && enum_eff_tgt) {
        auto p_src = std::dynamic_pointer_cast<primitive_type>(eff_src_nc);
        if (p_src) return CAST_WIDENING;
    }
    // ref<enum> cases: load + convert
    if (type::is_reference(effective_src)) {
        auto ref_sub = type::remove_const(std::dynamic_pointer_cast<reference_type>(effective_src)->get_subtype());
        auto ref_enum_src = std::dynamic_pointer_cast<enum_type>(ref_sub);
        if (ref_enum_src) {
            if (enum_eff_tgt) {
                if (ref_enum_src->get_enumeration() == enum_eff_tgt->get_enumeration()) {
                    return CAST_REF_CONV;
                }
                // Upcast only: ref<Derived> → Base
                auto src_en = ref_enum_src->get_enumeration();
                auto tgt_en = enum_eff_tgt->get_enumeration();
                if (src_en && tgt_en && src_en->is_derived_from(tgt_en)) {
                    return CAST_WIDENING;
                }
                return CAST_IMPOSSIBLE;
            }
            auto p_tgt = std::dynamic_pointer_cast<primitive_type>(tgt_nc);
            if (p_tgt) return CAST_WIDENING;
        }
    }

    // --- Both primitive ---
    auto prim_src = std::dynamic_pointer_cast<primitive_type>(eff_src_nc);
    auto prim_tgt = std::dynamic_pointer_cast<primitive_type>(tgt_nc);
    if (prim_src && prim_tgt) {
        if (*prim_src == *prim_tgt) return CAST_NONE;
        // Widening: integers same signedness, target wider or equal; or float widening
        if (prim_src->is_integer() && prim_tgt->is_integer() &&
            prim_src->is_unsigned() == prim_tgt->is_unsigned() &&
            prim_tgt->type_size() >= prim_src->type_size()) {
            return CAST_WIDENING;
        }
        if (prim_src->is_float() && prim_tgt->is_float() &&
            prim_tgt->type_size() >= prim_src->type_size()) {
            return CAST_WIDENING;
        }
        // All other primitive conversions (int<->float, narrowing int, bool<->int, etc.)
        return CAST_NARROWING;
    }

    // --- Struct value identity: bare struct S → S (same type) ---
    // This handles rvalues of struct type (e.g. from function return) being
    // passed as by-value struct parameters of the same type.
    if (auto st_src = std::dynamic_pointer_cast<struct_type>(eff_src_nc)) {
        auto st_tgt_val = std::dynamic_pointer_cast<struct_type>(tgt_nc);
        if (st_tgt_val && st_src == st_tgt_val) {
            return CAST_NONE;
        }
    }

    // --- Struct construction via single-arg constructor ---
    if (auto st_tgt = std::dynamic_pointer_cast<struct_type>(tgt_nc)) {
        auto st = st_tgt->get_struct();
        if (st) {
            for (auto& ctor : st->constructors()) {
                if (ctor->parameters().size() == 1) {
                    auto param_type = ctor->parameters()[0]->get_type();
                    // Recursively check if expr can be passed to the constructor parameter
                    auto sub_weight = compute_cast_weight(expr, param_type);
                    if (sub_weight != CAST_IMPOSSIBLE) {
                        return CAST_CONSTRUCT;
                    }
                }
            }
        }
    }

    // Step 5: Reference cases: const widening, struct upcast, value load, primitive cast
    // --- Upcast: struct ref → base struct ref (implicit) ---
    if (type::is_reference(tgt_nc) && type::is_reference(effective_src)) {
        auto src_st_type = std::dynamic_pointer_cast<struct_type>(
            std::dynamic_pointer_cast<reference_type>(effective_src)->get_referenced_type());
        auto tgt_st_type = std::dynamic_pointer_cast<struct_type>(
            std::dynamic_pointer_cast<reference_type>(tgt_nc)->get_referenced_type());
        if (src_st_type && tgt_st_type && src_st_type != tgt_st_type) {
            auto src_st = src_st_type->get_struct();
            auto tgt_st = tgt_st_type->get_struct();
            if (src_st && tgt_st && src_st->is_derived_from(tgt_st)) {
                return CAST_REF_CONV; // upcast is cheap (just a GEP offset)
            }
        }
    }
    // --- Upcast: struct value → base struct value ---
    if (auto src_st_type = std::dynamic_pointer_cast<struct_type>(effective_src)) {
        if (auto tgt_st_type = std::dynamic_pointer_cast<struct_type>(tgt_nc)) {
            if (src_st_type != tgt_st_type) {
                auto src_st = src_st_type->get_struct();
                auto tgt_st = tgt_st_type->get_struct();
                if (src_st && tgt_st && src_st->is_derived_from(tgt_st)) {
                    // Slicing: struct by value copy to base
                    return CAST_CONSTRUCT; // copy constructor
                }
            }
        }
    }

    // Step 6: Null → pointer/link/view/owner
    return CAST_IMPOSSIBLE;
}

std::pair<std::shared_ptr<constructor>/*best_constructor*/, std::vector<std::shared_ptr<expression>>/*adapted_args*/>
/**
 * Choose the best-matching constructor among candidates given a set of arguments.
 *
 * Scoring: max cast_weight across all parameters.
 * Handles arity mismatch, impossible casts, and ambiguity (same lowest score).
 *
 * @return {best_constructor, adapted_args} or {nullptr, {}} on failure.
 */
type_reference_resolver::get_best_matching_constructor(const std::vector<std::shared_ptr<constructor>>& constructors, const std::vector<std::shared_ptr<expression>>& args) {
    debug("[type_reference_resolver::get_best_matching_constructor] {} candidates, {} args", {std::to_string(constructors.size()), std::to_string(args.size())});
    const size_t arg_count = args.size();

    // Helper: a constructor is callable with arg_count args if it has exactly arg_count params,
    // OR if it has more params and all trailing params (beyond arg_count) have default values.
    auto ctor_is_callable = [&](const std::shared_ptr<constructor>& ctor) -> bool {
        const auto& params = ctor->parameters();
        if (params.size() == arg_count) return true;
        if (params.size() < arg_count) return false;
        for (size_t i = arg_count; i < params.size(); ++i) {
            if (!params[i]->has_default_expr()) return false;
        }
        return true;
    };

    // --- Step 1: filter by arity (exact or using defaults for trailing params) ---
    std::vector<std::shared_ptr<constructor>> arity_matched;
    for (auto& ctor : constructors) {
        if (ctor_is_callable(ctor)) {
            arity_matched.push_back(ctor);
        }
    }

    if (arity_matched.empty()) {
        std::string avail;
        if (!constructors.empty()) {
            bool first = true;
            for (auto& ctor : constructors) {
                if (!first) avail += ", ";
                avail += std::to_string(ctor->parameters().size()) + " parameter(s)";
                first = false;
            }
        }
        auto d = k::log::diagnostic::make_error(static_cast<unsigned int>(k::diag::symbol_diag::ERR_STATIC_CTOR_INIT_FAILED),
            "No constructor found with {} argument(s){}",
            {std::to_string(arg_count), avail.empty() ? "" : "; available constructors have: " + avail});
        report(d);
        return {nullptr, {}};
    }

    // --- Step 1b: among arity-matched, check if all are deleted ---
    // If the best match would be a deleted constructor, report a dedicated error.
    {
        std::vector<std::shared_ptr<constructor>> non_deleted;
        for (auto& ctor : arity_matched) {
            if (!ctor->is_deleted()) non_deleted.push_back(ctor);
        }
        if (non_deleted.empty()) {
            // All arity-matched constructors are deleted
            auto d = k::log::diagnostic::make_error(static_cast<unsigned int>(k::diag::symbol_diag::ERR_OVERLOAD_AMBIGUOUS),
                "Use of deleted constructor: a constructor matching {} argument(s) exists but has been explicitly deleted with '-> delete'",
                {std::to_string(arg_count)});
            report(d);
            return {nullptr, {}};
        }
        arity_matched = std::move(non_deleted);
    }

    // --- Step 2: compute per-candidate score (max of per-param weights) ---
    struct Candidate {
        std::shared_ptr<constructor> ctor;
        std::vector<std::shared_ptr<expression>> adapted_args;
        cast_weight score; // worst (max) cast weight across all parameters
        size_t defaults_used; // number of default values used (fewer = better)
    };

    struct FailedCandidate {
        std::shared_ptr<constructor> ctor;
        std::vector<size_t> failed_param_indices;
    };

    std::vector<Candidate> valid_candidates;
    std::vector<FailedCandidate> failed_candidates;

    for (auto& ctor : arity_matched) {
        const auto& params = ctor->parameters();
        const size_t total_params = params.size();
        const size_t defaults_used = total_params - arg_count;

        cast_weight max_weight = CAST_NONE;
        bool has_impossible = false;
        std::vector<size_t> failed_indices;
        std::vector<std::shared_ptr<expression>> adapted_args;

        // Score provided arguments
        for (size_t i = 0; i < arg_count; ++i) {
            auto param_type = params[i]->get_type();
            cast_weight w = compute_cast_weight(args[i], param_type);
            if (w == CAST_IMPOSSIBLE) {
                has_impossible = true;
                failed_indices.push_back(i);
            } else {
                if (w > max_weight) max_weight = w;
                auto adapted = adapt_type(args[i], param_type);
                adapted_args.push_back(adapted ? adapted : args[i]);
            }
        }

        // Append cloned default expressions for trailing parameters
        if (!has_impossible) {
            for (size_t i = arg_count; i < total_params; ++i) {
                adapted_args.push_back(params[i]->get_default_expr()->clone());
            }
        }

        if (has_impossible) {
            failed_candidates.push_back({ctor, std::move(failed_indices)});
        } else {
            valid_candidates.push_back({ctor, std::move(adapted_args), max_weight, defaults_used});
        }
    }

    // --- Step 3: no valid candidates ---
    if (valid_candidates.empty()) {
        auto d = k::log::diagnostic::make_error(static_cast<unsigned int>(k::diag::symbol_diag::ERR_OVERLOAD_AMBIGUOUS),
            "No viable constructor found: none of the {} candidate(s) accept the provided argument types",
            {std::to_string(failed_candidates.size())});
        for (auto& fc : failed_candidates) {
            std::string sig;
            bool first = true;
            for (auto& param : fc.ctor->parameters()) {
                if (!first) sig += ", ";
                sig += param->get_type() ? param->get_type()->to_string() : "?";
                first = false;
            }
            std::string failed_idxs;
            for (size_t idx : fc.failed_param_indices) {
                if (!failed_idxs.empty()) failed_idxs += ", ";
                failed_idxs += std::to_string(idx);
            }
            d.add_note("candidate constructor({}) — cannot implicitly cast argument(s) at position(s): {}", {sig, failed_idxs});
        }
        report(d);
        return {nullptr, {}};
    }

    // --- Step 4: find minimum score, then fewest defaults used ---
    cast_weight best_score = CAST_IMPOSSIBLE;
    size_t best_defaults = std::numeric_limits<size_t>::max();
    for (auto& cand : valid_candidates) {
        if (cand.score < best_score || (cand.score == best_score && cand.defaults_used < best_defaults)) {
            best_score = cand.score;
            best_defaults = cand.defaults_used;
        }
    }

    // Perfect match short-circuit
    if (best_score == CAST_NONE && best_defaults == 0) {
        for (auto& cand : valid_candidates) {
            if (cand.score == CAST_NONE && cand.defaults_used == 0) {
                return {cand.ctor, cand.adapted_args};
            }
        }
    }

    // --- Step 5: collect all candidates with best score + best defaults ---
    std::vector<Candidate*> best_candidates;
    for (auto& cand : valid_candidates) {
        if (cand.score == best_score && cand.defaults_used == best_defaults) {
            best_candidates.push_back(&cand);
        }
    }

    // --- Step 6: ambiguity check ---
    if (best_candidates.size() > 1) {
        auto d = k::log::diagnostic::make_error(static_cast<unsigned int>(k::diag::symbol_diag::ERR_OVERLOAD_NO_MATCH),
            "Ambiguous constructor call: {} equally viable candidates",
            {std::to_string(best_candidates.size())});
        for (auto* cand : best_candidates) {
            std::string sig;
            bool first = true;
            for (auto& param : cand->ctor->parameters()) {
                if (!first) sig += ", ";
                sig += param->get_type() ? param->get_type()->to_string() : "?";
                first = false;
            }
            d.add_note("candidate constructor({})", {sig});
        }
        report(d);
        return {nullptr, {}};
    }

    // --- Step 7: unique best candidate ---
    return {best_candidates[0]->ctor, best_candidates[0]->adapted_args};
}


type_reference_resolver::FunctionCandidate
/**
 * Choose the best-matching function among candidates given arguments.
 *
 * Supports three modes:
 *   A. Member call: this_expr + args (member functions, skip this in scoring).
 *   B. Direct call: direct_args (free/static functions, full arg list).
 *   C. Unified call: free functions with first param = ref to struct.
 *
 * Scoring: max cast_weight across all parameters. Member operators preferred
 * over non-member when scores are equal.
 *
 * @return FunctionCandidate with the best match, or {nullptr,...} on failure.
 */
type_reference_resolver::get_best_matching_function(
        const std::vector<std::shared_ptr<function>>& candidates,
        const std::vector<std::shared_ptr<expression>>& args,
        const std::shared_ptr<expression>& this_expr,
        const std::vector<std::shared_ptr<expression>>* direct_args)
{
    // Call modes:
    //   A) Member call: this_expr supplies 'this', args are explicit params (with possible defaults).
    //   B) Free/static direct call: uses direct_args or args (with possible defaults).
    //   C) Unified call: this_expr is first arg (params[0]), args match params[1..] (with defaults).

    struct CandInfo {
        std::shared_ptr<function> func;
        std::vector<std::shared_ptr<expression>> adapted_args;
        cast_weight score;
        bool is_unified;
        std::shared_ptr<expression> this_for_unified;
        int preference; // 0=member, 1=free/static direct, 2=unified
        size_t defaults_used;
    };

    std::vector<CandInfo> valid;

    // Score exprs against params[offset..], filling defaults for trailing missing params.
    auto score_with_defaults = [&](const std::vector<std::shared_ptr<expression>>& exprs,
                                   const std::vector<std::shared_ptr<parameter>>& params,
                                   size_t offset = 0)
            -> std::pair<cast_weight, std::vector<std::shared_ptr<expression>>>
    {
        const size_t n_params = params.size() - offset;
        const size_t n_exprs  = exprs.size();
        if (n_exprs > n_params) return {CAST_IMPOSSIBLE, {}};
        if (n_exprs < n_params) {
            for (size_t i = n_exprs; i < n_params; ++i)
                if (!params[offset + i]->has_default_expr()) return {CAST_IMPOSSIBLE, {}};
        }
        cast_weight max_w = CAST_NONE;
        std::vector<std::shared_ptr<expression>> adapted;
        for (size_t i = 0; i < n_exprs; ++i) {
            auto w = compute_cast_weight(exprs[i], params[offset + i]->get_type());
            if (w == CAST_IMPOSSIBLE) return {CAST_IMPOSSIBLE, {}};
            if (w > max_w) max_w = w;
            auto a = adapt_type(exprs[i], params[offset + i]->get_type());
            adapted.push_back(a ? a : exprs[i]);
        }
        for (size_t i = n_exprs; i < n_params; ++i)
            adapted.push_back(params[offset + i]->get_default_expr()->clone());
        return {max_w, adapted};
    };

    for (auto& func : candidates) {
        const auto& params = func->parameters();

        // -------- Mode A: member function called with this_expr --------
        if (func->is_member() && !func->is_static() && this_expr) {
            if (args.size() <= params.size()) {
                auto [w, adapted] = score_with_defaults(args, params, 0);
                if (w != CAST_IMPOSSIBLE) {
                    size_t def = params.size() - args.size();
                    // Const/mutable tie-breaker: on a mutable this, prefer mutable overload (pref=0)
                    // over const overload (pref=1). On a const this, both are filtered upstream
                    // (only const methods remain in candidates), so both get pref=0.
                    bool this_is_const = false;
                    if (this_expr && this_expr->get_type()) {
                        auto t = this_expr->get_type();
                        auto sub = type::is_reference(t) ? t->get_subtype() : t;
                        this_is_const = type::is_const(sub);
                    }
                    int pref = (!this_is_const && func->is_const_member()) ? 1 : 0;
                    valid.push_back({func, std::move(adapted), w, false, nullptr, pref, def});
                }
            }
        }

        // -------- Mode B: free/static function called directly --------
        if (!func->is_member() || func->is_static()) {
            const auto& b_args = direct_args ? *direct_args : args;
            if (b_args.size() <= params.size()) {
                auto [w, adapted] = score_with_defaults(b_args, params, 0);
                if (w != CAST_IMPOSSIBLE) {
                    size_t def = params.size() - b_args.size();
                    valid.push_back({func, std::move(adapted), w, false, nullptr, 1, def});
                }
            }
        }

        // -------- Mode C: unified call (free/static, params[0]=ref<struct>, args match params[1..]) --------
        if ((!func->is_member() || func->is_static()) && this_expr && !params.empty()
            && args.size() <= params.size() - 1) {
            auto first_param_type = params[0]->get_type();
            if (type::is_reference(first_param_type)) {
                auto w_this = compute_cast_weight(this_expr, first_param_type);
                if (w_this != CAST_IMPOSSIBLE) {
                    auto [w_rest, adapted_rest] = score_with_defaults(args, params, 1);
                    if (w_rest != CAST_IMPOSSIBLE) {
                        cast_weight total = std::max(w_this, w_rest);
                        auto adapted_this = adapt_type(this_expr, first_param_type);
                        size_t def = (params.size() - 1) - args.size();
                        valid.push_back({func, std::move(adapted_rest), total, true,
                                         adapted_this ? adapted_this : this_expr, 2, def});
                    }
                }
            }
        }
    }

    if (valid.empty()) {
        std::string fname = candidates.empty() ? "<unknown>" : candidates.front()->get_short_name();
        lex::opt_any_lexeme fn_lexeme;
        if (!candidates.empty()) {
            if (auto ast_fd = candidates.front()->get_ast_function_decl()) fn_lexeme = lex::any_lexeme{ast_fd->name};
        }
        throw_error(static_cast<unsigned int>(k::diag::function_diag::ERR_FUNC_VISIBILITY_MISMATCH), fn_lexeme,
            "No viable overload found for '{}' with {} argument(s): "
            "none of the {} candidate(s) can be called with the provided arguments",
            {fname, std::to_string(args.size()), std::to_string(candidates.size())});
    }

    // Best = lowest score, then fewest defaults, then lowest preference
    cast_weight best_score = CAST_IMPOSSIBLE;
    size_t best_def = std::numeric_limits<size_t>::max();
    int best_pref = 999;
    for (auto& c : valid) {
        if (c.score < best_score
            || (c.score == best_score && c.defaults_used < best_def)
            || (c.score == best_score && c.defaults_used == best_def && c.preference < best_pref)) {
            best_score = c.score;
            best_def = c.defaults_used;
            best_pref = c.preference;
        }
    }

    std::vector<CandInfo*> best;
    for (auto& c : valid) {
        if (c.score == best_score && c.defaults_used == best_def && c.preference == best_pref)
            best.push_back(&c);
    }

    if (best.size() > 1) {
        std::string fname = best[0]->func ? best[0]->func->get_short_name() : "<unknown>";
        auto d = k::log::diagnostic::make_error(static_cast<unsigned int>(k::diag::symbol_diag::ERR_FUNC_VISIBILITY_DENIED),
            "Ambiguous call to '{}': {} equally viable overloads",
            {fname, std::to_string(best.size())});
        for (auto* c : best) {
            std::string sig;
            bool first = true;
            for (auto& p : c->func->parameters()) {
                if (!first) sig += ", ";
                sig += p->get_type() ? p->get_type()->to_string() : "?";
                first = false;
            }
            d.add_note("{} {}({})", {c->is_unified ? "[unified]" : "", c->func->get_fq_name(), sig});
        }
        report(d);
        return {nullptr, {}, false, nullptr};
    }

    auto* b = best[0];
    debug("[type_reference_resolver::get_best_matching_function] selected '{}' (score={}, unified={})",
          {b->func->get_fq_name(), std::to_string(b->score), b->is_unified ? "yes" : "no"});
    return {b->func, b->adapted_args, b->is_unified, b->this_for_unified};
}

std::shared_ptr<expression> type_reference_resolver::adapt_reference_load_value(const std::shared_ptr<expression>& expr) {
    auto type = expr->get_type();

    if(!expr || !type::is_resolved(type)) {
        // Arguments must not be null, expr must have a type and this must be resolved.
        return nullptr;
    }

    if(type::is_reference(type)) {
        auto deref = load_value_expression::make_shared(expr);
        // Strip const when loading a value: const is compile-time only.
        deref->set_type(k::model::type::remove_const(type->get_subtype()));
        return deref;
    } else if(type::is_drain(type)) {
        auto deref = load_value_expression::make_shared(expr);
        deref->set_type(k::model::type::remove_const(type->get_subtype()));
        return deref;
    } else {
        return expr;
    }
}

/**
 * Adapt an expression to match a target type by applying implicit casts.
 *
 * Dispatches to per-category helpers: function_ref, pointer, link, view, owner, drain,
 * ref<owner>, double-reference, reference, enum, primitive/struct.
 *
 * @return The expression if compatible, a cast expression, or nullptr if impossible.
 */
std::shared_ptr<expression> type_reference_resolver::adapt_type(std::shared_ptr<expression> expr, const std::shared_ptr<type>& type) {
    if(!expr || !type::is_resolved(type) || !type::is_resolved(expr->get_type())) {
        // Arguments must not be null, expr must have a type and types (expr and target) must be resolved.
        return nullptr;
    }

    auto type_src = expr->get_type();
    // For value-level adaptation, strip const from both sides.
    auto type_nc = type::remove_const(type);

    // ── Function reference types ────────────────────────────────────────────────
    if (std::dynamic_pointer_cast<function_reference_type>(type_nc) ||
        std::dynamic_pointer_cast<function_reference_type>(type_src)) {
        auto result = adapt_function_ref_type(expr, type_src, type_nc);
        if (result) return result;
        if (std::dynamic_pointer_cast<function_reference_type>(type_nc)) return {};
    }
    if (type::is_reference(type_nc)) {
        auto tgt_sub_nc = type::remove_const(std::dynamic_pointer_cast<reference_type>(type_nc)->get_subtype());
        if (std::dynamic_pointer_cast<function_reference_type>(tgt_sub_nc)) {
            auto result = adapt_function_ref_type(expr, type_src, type_nc);
            if (result) return result;
        }
    }
    if (type::is_reference(type_src)) {
        auto src_inner = type::remove_const(std::dynamic_pointer_cast<reference_type>(type_src)->get_subtype());
        if (std::dynamic_pointer_cast<function_reference_type>(src_inner)) {
            auto result = adapt_function_ref_type(expr, type_src, type_nc);
            if (result) return result;
        }
    }
    // ── End function reference types ────────────────────────────────────────────

    if(type::is_pointer(type_src)) {
        return adapt_from_pointer(expr, type_src, type_nc);
    }

    if(type::is_link(type_src)) {
        return adapt_from_link(expr, type_src, type_nc);
    }

    if(type::is_view(type_src)) {
        return adapt_from_view(expr, type_src, type_nc);
    }

    if (type::is_owner(type_src)) {
        return adapt_from_owner(expr, type_src, type_nc);
    }

    if (type::is_drain(type_src)) {
        return adapt_from_drain(expr, type_src, type_nc);
    }

    // ref<owner<T>> → various target types (owner move, borrow as ptr/lnk/view/ref)
    if (type::is_reference(type_src) && !type::is_double_reference(type_src)) {
        auto ref_src = std::dynamic_pointer_cast<reference_type>(type_src);
        auto inner_nc = type::remove_const(ref_src->get_subtype());
        if (type::is_owner(inner_nc)) {
            auto result = adapt_from_ref_owner(expr, type_src, type_nc);
            if (result) return result;
            // Fall through to general reference handling if ref<owner> did not match
        }
    }

    // Double reference: collapse one level
    if(type::is_double_reference(type_src)) {
        auto ref_src = std::dynamic_pointer_cast<reference_type>(type_src);
        auto deref = load_value_expression::make_shared(expr);
        deref->set_type(ref_src->get_subtype());
        expr = deref;
        type_src = ref_src->get_subtype();
    }

    if(type::is_reference(type_src)) {
        return adapt_from_reference(expr, type_src, type_nc, type);
    }

    // ── Indirection/null → bool: implicit null check ─────────────────────────
    if (type::is_prim_bool(type_nc)) {
        if (type::is_pointer(type_src) || type::is_link(type_src) ||
            type::is_view(type_src) || type::is_owner(type_src) ||
            type::is_null(type_src)) {
            auto bool_type = _context->from_type(primitive_type::BOOL);
            auto cast = cast_expression::make_shared(expr, bool_type);
            cast->set_type(bool_type);
            return cast;
        }
    }

    // ── Enum implicit conversions ──────────────────────────────────────────────
    auto enum_result = adapt_enum_type(expr, type_nc);
    if (enum_result) return enum_result;

    // ── Primitive / struct fallback ─────────────────────────────────────────────
    return adapt_primitive_or_struct_type(expr, type_nc);
}




// =============================================================================
// init_order_resolver — Implementation
// =============================================================================

std::string init_order_resolver::node_label(const node_t& n) {
    if (auto sc = std::get_if<std::shared_ptr<static_constructor>>(&n)) {
        return "static_ctor(" + (*sc)->get_fq_name() + ")";
    } else if (auto gv = std::get_if<std::shared_ptr<global_variable_definition>>(&n)) {
        return "global_var(" + (*gv)->get_fq_name() + ")";
    }
    return "<unknown>";
}

/**
 * Recursively walk an expression tree and collect:
 *  - all global_variable_definition targets reached via symbol_expression
 *  - all struct_types referenced (for constructor invocations and variable types)
 *
 * visited_funcs prevents infinite recursion when traversing function bodies.
 */
void init_order_resolver::collect_global_deps_from_expr(
        const std::shared_ptr<expression>& expr,
        std::vector<std::shared_ptr<global_variable_definition>>& out_globals,
        std::vector<std::shared_ptr<struct_type>>&               out_struct_types,
        std::unordered_set<const function*>&                     visited_funcs)
{
    if (!expr) return;

    // symbol_expression — may refer to a global variable
    if (auto sym = std::dynamic_pointer_cast<symbol_expression>(expr)) {
        if (sym->is_variable_def()) {
            if (auto gv = std::dynamic_pointer_cast<global_variable_definition>(sym->get_variable_def())) {
                out_globals.push_back(gv);
            }
        }
        return;
    }

    // constructor_invocation_expression — struct type dependency + dig into ctor body
    if (auto cie = std::dynamic_pointer_cast<constructor_invocation_expression>(expr)) {
        auto ctor = cie->get_constructor();
        if (ctor) {
            auto owner = ctor->get_owner();
            if (owner && owner->get_struct_type()) {
                out_struct_types.push_back(owner->get_struct_type());
            }
            // Recurse into constructor body
            if (visited_funcs.insert(ctor.get()).second) {
                if (auto blk = ctor->get_block()) {
                    for (auto& stmt : blk->get_statements()) {
                        if (auto es = std::dynamic_pointer_cast<expression_statement>(stmt)) {
                            collect_global_deps_from_expr(es->get_expression(), out_globals, out_struct_types, visited_funcs);
                        }
                    }
                }
            }
        }
        for (auto& arg : cie->arguments()) {
            collect_global_deps_from_expr(arg, out_globals, out_struct_types, visited_funcs);
        }
        return;
    }

    // function_invocation_expression — recurse into callee body
    if (auto fie = std::dynamic_pointer_cast<function_invocation_expression>(expr)) {
        // Recurse into arguments first
        for (auto& arg : fie->arguments()) {
            collect_global_deps_from_expr(arg, out_globals, out_struct_types, visited_funcs);
        }
        // Recurse into callee body if it's a symbol_expression resolving to a function
        auto callee_sym = std::dynamic_pointer_cast<symbol_expression>(fie->callee_expr());
        if (callee_sym && callee_sym->is_function()) {
            auto fn = callee_sym->get_function();
            if (fn && visited_funcs.insert(fn.get()).second) {
                if (auto blk = fn->get_block()) {
                    for (auto& stmt : blk->get_statements()) {
                        if (auto es = std::dynamic_pointer_cast<expression_statement>(stmt)) {
                            collect_global_deps_from_expr(es->get_expression(), out_globals, out_struct_types, visited_funcs);
                        } else if (auto rs = std::dynamic_pointer_cast<return_statement>(stmt)) {
                            if (rs->get_expression()) {
                                collect_global_deps_from_expr(rs->get_expression(), out_globals, out_struct_types, visited_funcs);
                            }
                        }
                    }
                }
            }
        }
        return;
    }

    // unary_expression — recurse into sub_expr
    if (auto ue = std::dynamic_pointer_cast<unary_expression>(expr)) {
        collect_global_deps_from_expr(ue->sub_expr(), out_globals, out_struct_types, visited_funcs);
        return;
    }

    // binary_expression — recurse into left and right
    if (auto be = std::dynamic_pointer_cast<binary_expression>(expr)) {
        collect_global_deps_from_expr(be->left(), out_globals, out_struct_types, visited_funcs);
        collect_global_deps_from_expr(be->right(), out_globals, out_struct_types, visited_funcs);
        return;
    }

    // member_of_object_expression — recurse into sub_expr
    if (auto moe = std::dynamic_pointer_cast<member_of_object_expression>(expr)) {
        collect_global_deps_from_expr(moe->sub_expr(), out_globals, out_struct_types, visited_funcs);
        return;
    }

    // value_expression — no dependencies
}

/**
 * Collect dependencies of a global_variable_definition node.
 *
 * Steps:
 *   1. Rule 3: if GV has a struct type with a static constructor, depend on that SC.
 *   2. Rules 4-6: inspect init expression for global variable references and struct types
 *      from constructor invocations and function call bodies.
 */
void init_order_resolver::collect_deps_for_global(
        const std::shared_ptr<global_variable_definition>& gv,
        const std::unordered_map<const static_constructor*, size_t>& sctor_index,
        const std::unordered_map<const global_variable_definition*, size_t>& gv_index,
        std::vector<std::vector<size_t>>& adj,
        size_t my_idx)
{
    // Step 1: Rule 3: if GV has a struct type with a static constructor, depend on that SC
    // Rule 3: if GV has a struct type with a static constructor, it depends on that SC
    if (auto st_type = std::dynamic_pointer_cast<struct_type>(gv->get_type())) {
        if (auto st = st_type->get_struct()) {
            if (auto sc = st->get_static_constructor()) {
                auto it = sctor_index.find(sc.get());
                if (it != sctor_index.end()) {
                    adj[it->second].push_back(my_idx); // sc → gv (sc must come before gv)
                }
            }
        }
    }

    // Step 2: Rules 4-6: inspect init expression for global variable references and struct types from construct...
    // Rules 4–6: inspect init expression
    auto init_expr_base = gv->get_init_expr();
    if (!init_expr_base) return;
    auto init_expr = std::dynamic_pointer_cast<constructor_invocation_expression>(init_expr_base);
    if (!init_expr) return; // owner or other non-ctor init — no global deps to track

    std::vector<std::shared_ptr<global_variable_definition>> dep_globals;
    std::vector<std::shared_ptr<struct_type>> dep_structs;
    std::unordered_set<const function*> visited;

    for (size_t i = 0; i < init_expr->size(); ++i) {
        collect_global_deps_from_expr(init_expr->argument(i), dep_globals, dep_structs, visited);
    }

    // Rule 4: direct global variable references
    for (auto& dep_gv : dep_globals) {
        if (dep_gv.get() == gv.get()) continue; // skip self
        auto it = gv_index.find(dep_gv.get());
        if (it != gv_index.end()) {
            adj[it->second].push_back(my_idx); // dep_gv → gv
        }
    }

    // Rule 5: struct type from constructors → their SC must run first
    for (auto& dep_st : dep_structs) {
        if (auto st = dep_st->get_struct()) {
            if (auto sc = st->get_static_constructor()) {
                auto it = sctor_index.find(sc.get());
                if (it != sctor_index.end()) {
                    adj[it->second].push_back(my_idx); // sc → gv
                }
            }
        }
    }
}

/**
 * Collect dependencies of a static_constructor node.
 *
 * Steps:
 *   1. Rule 1: explicit deps from mem-init list (already resolved by symbol_resolver).
 *   2. Implicit: static constructors of base classes must run before this one.
 *   3. Rule 2 (handled elsewhere): static members of the owning struct depend on this SC.
 */
void init_order_resolver::collect_deps_for_sctor(
        const std::shared_ptr<static_constructor>& sctor,
        const std::unordered_map<const static_constructor*, size_t>& sctor_index,
        const std::unordered_map<const global_variable_definition*, size_t>& gv_index,
        std::vector<std::vector<size_t>>& adj,
        size_t my_idx)
{
    // Step 1: Rule 1: explicit deps from mem-init list (already resolved by symbol_resolver)
    // Rule 1: explicit deps from static constructor mem-init list.
    // `static S() : A(), gvar() {}`
    // By this point every static_dep_spec has already been resolved to a concrete model
    // element by symbol_resolver::visit_static_constructor.  We simply read the resolved
    // variant — no name lookup is performed here.
    for (auto& dep : sctor->member_inits()) {
        if (!dep.is_resolved()) {
            // Should not happen: symbol_resolver would have thrown already.
            // Guard against stale data just in case.
            continue;
        }

        if (dep.is_structure()) {
            auto dep_st = dep.get_structure();
            if (auto sc = dep_st->get_static_constructor()) {
                auto it = sctor_index.find(sc.get());
                if (it != sctor_index.end()) {
                    adj[it->second].push_back(my_idx); // SC(dep_st) → SC(sctor)
                }
                // If dep_st has no static ctor, no ordering constraint needed.
            }
        } else if (dep.is_global_variable()) {
            auto dep_gv = dep.get_global_variable();
            auto it = gv_index.find(dep_gv.get());
            if (it != gv_index.end()) {
                adj[it->second].push_back(my_idx); // dep_gv → SC(sctor)
            }
        }
    }

    // Step 2: Implicit: static constructors of base classes must run before this one
    // Implicit: static constructors of BASE CLASSES must run BEFORE this one.
    // (A derived struct's static constructor depends on its bases' static constructors.)
    if (auto owner = sctor->get_owner()) {
        for (auto& bs : owner->get_bases()) {
            if (!bs.base) continue;
            if (auto base_sc = bs.base->get_static_constructor()) {
                auto it = sctor_index.find(base_sc.get());
                if (it != sctor_index.end()) {
                    adj[it->second].push_back(my_idx); // SC(base) → SC(derived)
                }
            }
        }
    }

    // Step 3: Rule 2 (handled elsewhere): static members of the owning struct depend on this SC
    // Rule 2 (implicit): static members of owner struct must be initialized AFTER this SC.
    // This is handled in collect_deps_for_global (rule 3): each static member variable
    // of struct S depends on SC(S).
}

/**
 * Compute the unified ordered init/finit sequence using topological sort.
 *
 * Steps:
 *   1. Collect standalone static destructors (no matching static constructor).
 *   2. Build node index: [static constructors | global variables].
 *   3. Build dependency graph (adjacency list) using collect_deps_for_sctor/global.
 *   4. Kahn's topological sort (BFS) to produce construction order.
 *   5. Detect cycles (if topo sort didn't consume all nodes).
 *   6. Store construction order on global_constructor_function,
 *      reverse as destruction order on global_destructor_function.
 */
void init_order_resolver::resolve() {
    trace("[init_order_resolver::resolve] begin");
    auto& ctor_func = _unit.get_global_constructor_function();
    auto& dtor_func = _unit.get_global_destructor_function();

    // Step 1: Collect standalone static destructors (no matching static constructor)
    const auto& raw_sctors = ctor_func.get_static_constructors();
    const auto& raw_gvars  = ctor_func.get_global_variables();

    // Collect static destructors that have NO matching static constructor.
    // These structs need finalization but no initialization.
    // We gather them by scanning all structures in the unit.
    std::vector<std::shared_ptr<static_destructor>> standalone_sdtors;
    {
        std::unordered_set<const aggregate*> has_sctor;
        for (auto& sc : raw_sctors) {
            if (auto owner = sc->get_owner()) has_sctor.insert(owner.get());
        }
        // Walk the root namespace recursively
        std::function<void(const ns&)> scan_ns = [&](const ns& n) {
            for (auto& child : n.get_children()) {
                if (auto agg = std::dynamic_pointer_cast<aggregate>(child)) {
                    if (!has_sctor.count(agg.get())) {
                        if (auto sdtor = agg->get_static_destructor()) {
                            standalone_sdtors.push_back(sdtor);
                        }
                    }
                } else if (auto sub_ns = std::dynamic_pointer_cast<ns>(child)) {
                    scan_ns(*sub_ns);
                }
            }
        };
        if (auto root = _unit.get_root_namespace()) {
            scan_ns(*root);
        }
    }

    // Nothing to do if no items at all
    if (raw_sctors.empty() && raw_gvars.empty() && standalone_sdtors.empty()) return;

    // -------------------------------------------------------------------------
    // Build index: node pointer → index in combined nodes array
    // Nodes layout: [sctors 0..S-1] [gvars S..S+G-1]
    // -------------------------------------------------------------------------
    const size_t S = raw_sctors.size();
    const size_t G = raw_gvars.size();
    const size_t N = S + G;

    // Step 2: Build node index: [static constructors | global variables]
    std::unordered_map<const static_constructor*, size_t>           sctor_index;
    std::unordered_map<const global_variable_definition*, size_t>   gv_index;

    for (size_t i = 0; i < S; ++i) sctor_index[raw_sctors[i].get()] = i;
    for (size_t i = 0; i < G; ++i) gv_index[raw_gvars[i].get()]     = S + i;

    // Build combined node list
    std::vector<node_t> nodes;
    nodes.reserve(N);
    for (auto& sc : raw_sctors) nodes.push_back(sc);
    for (auto& gv : raw_gvars)  nodes.push_back(gv);

    // -------------------------------------------------------------------------
    // Build adjacency list: adj[i] = list of nodes that must come AFTER node i
    // (i.e. i is a dependency of adj[i][j])
    // -------------------------------------------------------------------------
    std::vector<std::vector<size_t>> adj(N);

    for (size_t i = 0; i < S; ++i) {
        collect_deps_for_sctor(raw_sctors[i], sctor_index, gv_index, adj, i);
    }
    for (size_t i = 0; i < G; ++i) {
        collect_deps_for_global(raw_gvars[i], sctor_index, gv_index, adj, S + i);
    }

    // Step 3: Build dependency graph (adjacency list) using collect_deps_for_sctor/global
    // Deduplicate adjacency lists
    for (auto& list : adj) {
        std::sort(list.begin(), list.end());
        list.erase(std::unique(list.begin(), list.end()), list.end());
    }

    // Step 4: Kahn's topological sort (BFS) to produce construction order
    // -------------------------------------------------------------------------
    // Kahn's topological sort (BFS)
    // -------------------------------------------------------------------------
    std::vector<size_t> in_degree(N, 0);
    for (size_t i = 0; i < N; ++i) {
        for (size_t j : adj[i]) {
            if (j < N) ++in_degree[j];
        }
    }

    std::queue<size_t> ready;
    for (size_t i = 0; i < N; ++i) {
        if (in_degree[i] == 0) ready.push(i);
    }

    std::vector<node_t> construction_order;
    construction_order.reserve(N);

    while (!ready.empty()) {
        size_t cur = ready.front(); ready.pop();
        construction_order.push_back(nodes[cur]);
        for (size_t next : adj[cur]) {
            if (--in_degree[next] == 0) ready.push(next);
        }
    }

    // Step 5: Detect cycles (if topo sort didn't consume all nodes)
    // -------------------------------------------------------------------------
    // Cycle detection
    // -------------------------------------------------------------------------
    if (construction_order.size() < N) {
        // Collect all nodes still in a cycle (in_degree > 0)
        std::string cycle_members;
        for (size_t i = 0; i < N; ++i) {
            if (in_degree[i] > 0) {
                if (!cycle_members.empty()) cycle_members += ", ";
                cycle_members += node_label(nodes[i]);
            }
        }
        throw_error(static_cast<unsigned int>(k::diag::symbol_diag::ERR_INIT_ORDER_CYCLE),
            "Cycle detected in global initialization dependency graph. "
            "The following items form a circular dependency: {}",
            {cycle_members});
    }

    // Step 6: Store construction order on global_constructor_function, reverse as destruction order on global_d...
    // -------------------------------------------------------------------------
    // Store construction order into the constructor function,
    // and the REVERSE as the destruction order into the destructor function.
    // Standalone static destructors (no matching static ctor) are appended to
    // the destruction order at the front (they run first during finalization,
    // i.e. they were logically "initialized last" — but have no init step).
    // -------------------------------------------------------------------------
    ctor_func.set_ordered_items(construction_order);

    std::vector<node_t> destruction_order(construction_order.rbegin(), construction_order.rend());
    // Prepend standalone static dtors: they finalize first (no ordering constraint
    // relative to items in the main graph since they have no static ctor node).
    // Use static_constructor as a sentinel carrier — we wrap them as init_items
    // containing the static_constructor of the same struct if it exists.
    // Since these are standalone (no static ctor), we emit them via a special path
    // in implementation_generator: we store them as a separate list on dtor_func.
    dtor_func.set_ordered_items(std::move(destruction_order));
    dtor_func.add_standalone_static_dtors(standalone_sdtors);
}



} // k::model::gen
