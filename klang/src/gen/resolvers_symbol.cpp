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
#include "resolvers_symbol.hpp"
#include "gen_helpers.hpp"
#include "../model/imported.hpp"
#include "../model/statements.hpp"
#include "../model/expressions.hpp"
#include "../errors.hpp"
#include <queue>
#include <unordered_set>
namespace k::model::gen {
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
        lex::opt_any_lexeme this_lexeme;
        if (auto sym_expr = dynamic_cast<const symbol_expression*>(&elem)) {
            this_lexeme = sym_expr->first_lexeme();
        }
        throw_error(static_cast<unsigned int>(k::diag::symbol_diag::ERR_SYMBOL_NOT_FOUND), this_lexeme,
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

        // Soft alias redirection: 'alias N : symbol;' makes N a fully transparent
        // second name for the aliased variable or function. The target is resolved
        // in the scope that declares the alias, never in the scope that uses it.
        if (auto ah = dynamic_cast<const alias_holder*>(&elem)) {
            if (auto al = ah->get_alias(name.to_string())) {
                if (al->is_soft() && !al->get_target_name().empty() && !al->_resolving) {
                    if (auto scope = al->parent<element>()) {
                        al->_resolving = true;
                        auto res = resolve_symbol(*scope, al->get_target_name());
                        al->_resolving = false;
                        if (res.index() != 0) return res;
                    }
                }
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
        } else if (auto target_block = elem.ancestor<block>()) {
            if (auto func = target_block->get_direct_function()) {
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

namespace {

/**
 * True when @p target_name denotes a namespace reachable from @p scope, either
 * root-anchored (::a::b) or by climbing the enclosing namespace chain.
 */
bool alias_target_denotes_namespace(const element& scope, const k::name& target_name) {
    auto enclosing = scope_lookup::enclosing_namespace(scope);
    if (!enclosing) return false;

    auto walk = [](std::shared_ptr<const ns> from, const k::name& name) -> bool {
        std::shared_ptr<const ns> current = std::move(from);
        for (size_t i = 0; i < name.size(); ++i) {
            if (!current) return false;
            current = current->get_child_namespace(name[i]);
        }
        return (bool) current;
    };

    if (target_name.has_root_prefix()) {
        return walk(scope_lookup::root_namespace(scope), target_name.without_root_prefix());
    }

    for (std::shared_ptr<const ns> current = enclosing; current; ) {
        if (walk(current, target_name)) return true;
        if (current->is_root()) break;
        current = current->parent<ns>();
    }
    return false;
}

} // anonymous namespace

void symbol_resolver::check_alias_declarations(const alias_holder& holder, const element& scope) {
    for (const auto& al : holder.get_aliases()) {
        if (!al) continue;
        // Give the alias its fully-qualified name: it is needed to export the
        // declaration and to reference it from an exported signature.
        visit_named_element(*al);
        const k::name& target = al->get_target_name();
        if (target.empty()) continue;
        if (alias_target_denotes_namespace(scope, target)) {
            throw_error(static_cast<unsigned int>(k::diag::alias_diag::ERR_ALIAS_NAMESPACE_TARGET),
                        al->get_decl_lexeme(),
                        "'{}' names a namespace: a namespace cannot be aliased with '{}'; "
                        "use 'using {} = namespace {};' instead",
                        {target.to_string(), al->is_strong() ? "typedef" : "alias",
                         al->get_short_name(), target.to_string()});
        }
    }
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

void symbol_resolver::check_variable_visibility(const variable_definition& var, const element& access_site) {
    // The access site is usually the symbol_expression referencing the variable;
    // use its source position for diagnostics when available.
    lex::opt_any_lexeme access_lexeme;
    if (auto sym_expr = dynamic_cast<const symbol_expression*>(&access_site)) {
        access_lexeme = sym_expr->first_lexeme();
    }
    // Step 1: Member variable: check struct member visibility and friend access
    // Member variable in a struct
    if (auto mv = dynamic_cast<const member_variable_definition*>(&var)) {
        auto owner_agg = std::const_pointer_cast<aggregate>(mv->parent<aggregate>());
        if (!owner_agg) return;
        auto vis = mv->get_visibility();
        if (vis == PUBLIC) return;
        if (scope_lookup::is_struct_member_accessible(vis, *owner_agg, owner_agg, _function_stack)) return;
        if (vis == PROTECTED && scope_lookup::is_friend_of(*owner_agg, _function_stack, _unit)) return;
        lex::opt_any_lexeme agg_lexeme = access_lexeme;
        if (!agg_lexeme) {
            if (auto ast_ad = owner_agg->get_ast_aggregate_decl()) agg_lexeme = lex::any_lexeme{ast_ad->name};
        }
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
            throw_error(static_cast<unsigned int>(k::diag::symbol_diag::ERR_VISIBILITY_ACCESS_DENIED), access_lexeme,
                "protected variable '{}' is only accessible within the same module; "
                "it is declared in module '{}' but accessed from outside",
                {gv->get_short_name(), owner_root->get_short_name()});
        } else { // PRIVATE
            if (scope_lookup::is_in_same_namespace(*site, *owner_ns)) return;
            throw_error(static_cast<unsigned int>(k::diag::symbol_diag::ERR_VISIBILITY_ACCESS_DENIED), access_lexeme,
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


} // namespace k::model::gen
