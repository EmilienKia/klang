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

#include <llvm/IR/DerivedTypes.h>

#include <queue>
#include <set>
#include <unordered_set>
#include <functional>

namespace k::model::gen {

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
symbol_resolver::resolve_symbol_from_root(const name& name) {
    if (name.empty()) return std::monostate{};

    auto root_ns = _unit.get_root_namespace();
    if (!root_ns) return std::monostate{};

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

    // Strategy 2: resolve directly from root namespace (omit module prefix)
    auto local = resolve_qualified_from(*root_ns, name);
    if (local.index() != 0) return local;

    // Strategy 3: fallback — search imported modules for a matching function or variable.
    if (auto* kdi_fn = _unit.find_imported_function(name)) {
        return _unit.get_or_create_imported_function(kdi_fn, _context);
    }
    if (auto* kdi_var = _unit.find_imported_variable(name)) {
        return _unit.get_or_create_imported_variable(kdi_var, _context);
    }

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
    visit_unit(_unit);

    // Resolve chained redirects: follow redirect targets transitively.
    // After visit_unit, each redirected function has its immediate target set.
    // Now resolve chains: a -> b -> c becomes a -> c, b -> c.
    resolve_redirect_chains(_unit);
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
        throw_error(0x0051, std::nullopt,
            "Circular redirect chain detected involving function '{}'",
            {fn.get_short_name()});
    }
    visited.insert(&fn);

    auto target = fn.get_redirect_target();
    if (!target) {
        throw_error(0x0052, std::nullopt,
            "Function redirector '{}' has no resolved target",
            {fn.get_short_name()});
    }

    // If the target is itself a redirect, follow the chain
    if (target->is_redirected()) {
        if (!target->get_redirect_target()) {
            throw_error(0x0053, std::nullopt,
                "Function redirector '{}' targets '{}', which is itself a redirector with no resolved target",
                {fn.get_short_name(), target->get_short_name()});
        }
        return resolve_redirect_chain(*target, visited);
    }

    // Target is a concrete function — check it's not abstract or deleted
    if (target->is_abstract_func()) {
        throw_error(0x0054, std::nullopt,
            "Function redirector '{}' targets abstract function '{}', which has no implementation",
            {fn.get_short_name(), target->get_short_name()});
    }
    if (target->is_deleted()) {
        throw_error(0x0055, std::nullopt,
            "Function redirector '{}' targets deleted function '{}'",
            {fn.get_short_name(), target->get_short_name()});
    }

    return target;
}

std::variant<std::monostate, std::shared_ptr<variable_definition>, std::shared_ptr<function>> // TODO Add traversal direction flag
symbol_resolver::resolve_symbol(const element& elem, const name& name) {

    // Specifically look at the "this" symbol (non-static function specific parameter)
    if (name.size() == 1 && name.to_string() == "this") {
        auto func = elem.ancestor<function>();
        while (func) {
            if (func->is_member() && func->get_this_parameter()) {
                return std::const_pointer_cast<parameter>(func->get_this_parameter());
            }
            func = func->ancestor<function>();
        }
        throw_error(0x0002, std::nullopt,
            "'this' can only be used inside a non-static member function");
    }

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

        // TODO: Workaround, remove it when function will be a (parameter) variable_holder
        if (auto blck = dynamic_cast<const block*>(&elem)) {
            if (auto func = blck->get_direct_function()) {
                if (auto param = func->get_parameter(name.to_string())) {
                    return std::const_pointer_cast<parameter>(param);
                }
            }
        }
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

void symbol_resolver::check_variable_visibility(const variable_definition& var, const element& /*access_site*/) {
    // Member variable in a struct
    if (auto mv = dynamic_cast<const member_variable_definition*>(&var)) {
        auto owner_agg = std::const_pointer_cast<aggregate>(mv->parent<aggregate>());
        if (!owner_agg) return;
        auto vis = mv->get_visibility();
        if (vis == PUBLIC) return;
        if (scope_lookup::is_struct_member_accessible(vis, *owner_agg, owner_agg, _function_stack)) return;
        throw_error(0x000F, std::nullopt,
            "{} member variable '{}' of struct '{}' is not accessible here; "
            "it can only be accessed from member functions of '{}'{}",
            {vis == PROTECTED ? "protected" : "private",
             mv->get_short_name(), owner_agg->get_short_name(), owner_agg->get_short_name(),
             vis == PROTECTED ? " or its subclasses" : ""});
    }

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
            throw_error(0x000E, std::nullopt,
                "protected variable '{}' is only accessible within the same module; "
                "it is declared in module '{}' but accessed from outside",
                {gv->get_short_name(), owner_root->get_short_name()});
        } else { // PRIVATE
            if (scope_lookup::is_in_same_namespace(*site, *owner_ns)) return;
            throw_error(0x000E, std::nullopt,
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


std::shared_ptr<expression> symbol_resolver::adapt_type(std::shared_ptr<expression> expr, const std::shared_ptr<type>& type) {
    if(!expr || !type::is_resolved(type) || !type::is_resolved(expr->get_type())) {
        // Arguments must not be null, expr must have a type and types (expr and target) must be resolved.
        return nullptr;
    }

    auto type_src = expr->get_type();

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

    if(type::is_double_reference(type_src)) {
        auto ref_src = std::dynamic_pointer_cast<reference_type>(type_src);
        auto deref = load_value_expression::make_shared(expr);
        deref->set_type(ref_src->get_subtype());
        expr = deref;
        type_src = ref_src->get_subtype();
    }

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

std::shared_ptr<type>
aggregate_type_resolver::resolve_type_by_name(const k::name& type_name, const element& context_elem) {
    if (type_name.empty()) return {};

    if (type_name.has_root_prefix()) {
        return resolve_type_from_root(type_name.without_root_prefix());
    }

    if (type_name.size() == 1) {
        auto prim = _context->from_string(type_name.front());
        if (prim && type::is_resolved(prim)) return prim;
    }

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
    }

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
    visit_unit(_unit);
}

void aggregate_type_resolver::visit_unit(unit& /*unit*/) {
    visit_namespace(*_unit.get_root_namespace());
    // Note: global_constructor_function, global_destructor_function and global_main_function
    // are handled by type_reference_resolver — they contain expressions and statements.
    // aggregate_type_resolver only handles declaration-level type resolution.
}

void aggregate_type_resolver::visit_namespace(ns& ns) {
    for (auto& child : ns.get_children()) {
        child->accept(*this);
    }
}

void aggregate_type_resolver::visit_aggregate(aggregate& st) {
    // Visit nested aggregate children first (depth-first), so their types are available
    // before we process members of the outer aggregate.
    for (auto& child : st.get_children()) {
        if (auto nested = std::dynamic_pointer_cast<aggregate>(child)) {
            nested->accept(*this);
        }
    }

    // NOTE: member variable type resolution, global variable type resolution, and
    // function signature resolution are intentionally NOT done here for Phase 1.
    // type_reference_resolver handles all of these in Phase 1.b.
    // aggregate_type_resolver's role is limited to building LLVM vtable struct types
    // in visit_klass (see below) so they are available before type_reference_resolver runs.
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

    if (!type::is_resolved(var.get_type())) {
        auto resolved = resolve_one_type(var.get_type(), *this, var, _context);
        if (resolved && type::is_resolved(resolved)) {
            var.set_type(resolved);
        }
    }
    // Do NOT visit init expressions — those are expressions, handled by type_reference_resolver
}

void aggregate_type_resolver::visit_global_variable_definition(global_variable_definition& var) {
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

void aggregate_type_resolver::visit_parameter(parameter& param) {
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
            // find the inner unresolved name, resolve the inner aggregate, then
            // rebuild the composite wrapper around the resolved inner type.
            auto owner_func = param.parent<function>();
            if (owner_func) {
                // Collect wrapper kinds (from outermost to innermost unresolved_type)
                enum class WrapKind { Ref, Ptr, Link, Pin, Const };
                std::vector<WrapKind> wrappers;
                auto inner = param.get_type();
                while (inner && !std::dynamic_pointer_cast<unresolved_type>(inner)) {
                    if      (type::is_reference(inner))  wrappers.push_back(WrapKind::Ref);
                    else if (type::is_pointer(inner))    wrappers.push_back(WrapKind::Ptr);
                    else if (type::is_link(inner))       wrappers.push_back(WrapKind::Link);
                    else if (type::is_pinned(inner))     wrappers.push_back(WrapKind::Pin);
                    else if (type::is_const(inner))      wrappers.push_back(WrapKind::Const);
                    else break;
                    inner = inner->get_subtype();
                }
                auto unres = std::dynamic_pointer_cast<unresolved_type>(inner);
                if (unres && !unres->type_id().empty()) {
                    // Resolve the inner aggregate type
                    auto inner_resolved = resolve_type_by_name(unres->type_id(), *owner_func);
                    if (type::is_resolved(inner_resolved)) {
                        // Re-apply wrappers in reverse order (innermost first)
                        res_type = inner_resolved;
                        for (auto it = wrappers.rbegin(); it != wrappers.rend(); ++it) {
                            switch (*it) {
                                case WrapKind::Ref:   res_type = res_type->get_reference(); break;
                                case WrapKind::Ptr:   res_type = res_type->get_pointer();   break;
                                case WrapKind::Link:  res_type = res_type->get_link();      break;
                                case WrapKind::Pin:   res_type = res_type->get_pinned();    break;
                                case WrapKind::Const: res_type = res_type->get_const();     break;
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

void aggregate_type_resolver::visit_function(function& fn) {
    // Resolve 'this' parameter type for non-static member functions
    if (fn.is_member() && !fn.is_static() && fn.get_this_parameter()) {
        auto this_param = std::const_pointer_cast<parameter>(fn.get_this_parameter());
        this_param->accept(*this);
    }

    // Resolve parameter types (signatures only, no default expressions)
    for (auto param : fn.parameters()) {
        param->accept(*this);
    }

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
        auto resolved = resolve_type_by_name(
            std::dynamic_pointer_cast<unresolved_type>(fn.get_return_type())
                ? std::dynamic_pointer_cast<unresolved_type>(fn.get_return_type())->type_id()
                : k::name{},
            fn);
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
    visit_unit(_unit);
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
    // Visit nested aggregates first (depth-first)
    for (auto& child : st.get_children()) {
        if (auto nested = std::dynamic_pointer_cast<aggregate>(child)) {
            nested->accept(*this);
        }
    }
}

void model_materializer::visit_klass(klass& kl) {
    // Recurse into nested aggregates
    visit_aggregate(kl);

    if (!kl.has_vtable()) return;

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
            throw_error(0x0001, std::nullopt,
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

    // Walk ALL non-virtual sub-objects transitively reachable from kl,
    // computing their cumulative byte offsets in kl's layout.
    // For each sub-object with a vtable, generate a secondary_vtable_spec.
    // This includes both "primary" and "secondary" bases at all levels —
    // in K's layout every base sub-object (including the primary) is at a
    // non-zero offset because kl's own __vptr__ occupies field 0.
    // We use `already_processed` to avoid duplicating specs for the same type.
    std::unordered_set<const klass*> already_processed;

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
        throw_error(0x002F, std::nullopt,
            "{} member function '{}' of struct '{}' is not accessible here; "
            "it can only be called from member functions of '{}'{}",
            {vis == PROTECTED ? "protected" : "private",
             func.get_short_name(), owner_agg->get_short_name(), owner_agg->get_short_name(),
             vis == PROTECTED ? " or its subclasses" : ""});
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
            throw_error(0x002E, std::nullopt,
                "protected function '{}' is only accessible within the same module; "
                "it is declared in module '{}' but accessed from outside",
                {func.get_short_name(), owner_root->get_short_name()});
        } else {
            if (scope_lookup::is_in_same_namespace(*site, *owner_ns)) return;
            throw_error(0x002E, std::nullopt,
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

    throw_error(0x0030, std::nullopt,
        "{} constructor of struct '{}' is not accessible here; "
        "it can only be called from member functions of '{}'{}",
        {vis == PROTECTED ? "protected" : "private",
         owner_agg->get_short_name(), owner_agg->get_short_name(),
         vis == PROTECTED ? " or its subclasses" : ""});
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
type_reference_resolver::resolve_type_by_name(const k::name& type_name, const element& context_elem) {
    if (type_name.empty()) return {};

    // Root-prefixed: anchor at unit root
    if (type_name.has_root_prefix()) {
        return resolve_type_from_root(type_name.without_root_prefix());
    }

    // Try primitive types first via context (for simple names only)
    if (type_name.size() == 1) {
        auto prim = _context->from_string(type_name.front());
        if (prim && type::is_resolved(prim)) {
            return prim;
        }
    }

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
    }

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
    visit_unit(_unit);
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
                    throw_error(0x0002, std::nullopt,
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
                throw_error(0x0003, std::nullopt,
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
type_reference_resolver::resolve_function_ref_type(
    const std::shared_ptr<unresolved_function_ref_type>& ufrt,
    const element& context_elem)
{
    if (!ufrt) return {};

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
            throw_error(0x4042, std::nullopt,
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

    // Member function reference: resolve the owner structure
    if (!ufrt->owner_name().empty()) {
        auto owner_agg = resolve_struct_from(context_elem, ufrt->owner_name());
        if (!owner_agg) {
            // Try from root
            auto root_ns = _unit.get_root_namespace();
            if (root_ns) owner_agg = resolve_struct_from(*root_ns, ufrt->owner_name());
        }
        if (!owner_agg) {
            throw_error(0x4043, std::nullopt,
                "Cannot find owner struct '{}' for member function reference type",
                {ufrt->owner_name().to_string()});
        }
        // Accept both structure and klass as owner aggregates
        if (!std::dynamic_pointer_cast<structure>(owner_agg) &&
            !std::dynamic_pointer_cast<klass>(owner_agg)) {
            throw_error(0x4044, std::nullopt,
                "'{}' is not a structure or class; member function pointers require a struct/class owner",
                {ufrt->owner_name().to_string()});
        }
        builder.member_of(owner_agg);
    }

    auto resolved_type = builder.build();
    // Cache the resolved type into the unresolved placeholder
    const_cast<unresolved_function_ref_type*>(ufrt.get())->resolve(resolved_type);
    return resolved_type;
}


void type_reference_resolver::visit_variable_definition(variable_definition& var)
{
    if(!type::is_resolved(var.get_type())) {
        // First: handle unresolved_function_ref_type (function pointer/pin/link type)
        if (auto ufrt = std::dynamic_pointer_cast<unresolved_function_ref_type>(var.get_type())) {
            // variable_definition is not an element directly; use dynamic_cast to get element context
            const element* var_elem = dynamic_cast<const element*>(&var);
            std::shared_ptr<type> resolved;
            if (var_elem) {
                resolved = resolve_function_ref_type(ufrt, *var_elem);
            } else {
                // Fallback: no scope context, resolve without name lookup (free functions only)
                resolved = resolve_function_ref_type(ufrt, _unit);
            }
            if (resolved && type::is_resolved(resolved)) {
                var.set_type(resolved);
            } else {
                throw_internal_error(0x0002, std::nullopt,
                    "Internal error: cannot resolve function reference type for variable '{}'",
                    {var.get_fq_name()});
            }
        } else if (auto own_type = std::dynamic_pointer_cast<owner_type>(var.get_type())) {
            // owner<UnresolvedType> — resolve the inner type then rebuild the owner wrapper
            auto inner = own_type->get_subtype();
            if (!type::is_resolved(inner)) {
                auto unres_inner = std::dynamic_pointer_cast<unresolved_type>(inner);
                std::shared_ptr<type> resolved_inner;
                if (unres_inner) {
                    const element* var_elem = dynamic_cast<const element*>(&var);
                    if (var_elem) {
                        resolved_inner = resolve_type_by_name(unres_inner->type_id(), *var_elem);
                    }
                    if (!resolved_inner || !type::is_resolved(resolved_inner)) {
                        resolved_inner = _context->from_string(unres_inner->type_id());
                    }
                    if (!resolved_inner || !type::is_resolved(resolved_inner)) {
                        auto imported_agg = _unit.get_or_create_imported_aggregate(
                            unres_inner->type_id(), _context);
                        if (imported_agg && imported_agg->get_struct_type()) {
                            resolved_inner = imported_agg->get_struct_type();
                        }
                    }
                    if (!resolved_inner || !type::is_resolved(resolved_inner)) {
                        auto imported_en = _unit.get_or_create_imported_enum(
                            unres_inner->type_id(), _context);
                        if (imported_en && imported_en->get_enum_type()) {
                            resolved_inner = imported_en->get_enum_type();
                        }
                    }
                } else {
                    resolved_inner = _context->resolve_type(inner);
                }
                if (resolved_inner && type::is_resolved(resolved_inner)) {
                    // Unsized arrays are canonicalised to ref<array<T>> by resolve_type,
                    // but inside an owner we want owner(array(T)), not owner(ref<array<T>>).
                    // Unwrap the spurious reference layer.
                    if (auto ref = std::dynamic_pointer_cast<reference_type>(resolved_inner)) {
                        if (auto arr = std::dynamic_pointer_cast<array_type>(ref->get_subtype())) {
                            if (!arr->is_sized()) {
                                resolved_inner = arr;
                            }
                        }
                    }
                    var.set_type(resolved_inner->get_owner());
                } else {
                    throw_error(0x0005, std::nullopt,
                        "Unknown inner type for owner variable '{}': cannot resolve '{}'",
                        {var.get_fq_name(), inner ? inner->to_string() : "?"});
                }
            }
        } else {
        auto unres_type = std::dynamic_pointer_cast<unresolved_type>(var.get_type());
        if(!unres_type) {
            // The type is not resolved but is not a plain unresolved_type either.
            // It may be a pointer/link/pin/reference wrapping an unresolved inner type
            // (e.g. Point*, Point~, Point^).  context::resolve_type has no scope context,
            // so we first try to resolve the inner unresolved_type using resolve_type_by_name,
            // then rebuild the wrapper.
            auto try_resolve_wrapped = [&](std::shared_ptr<k::model::type> inner)
                -> std::shared_ptr<k::model::type>
            {
                if (type::is_resolved(inner)) return inner;
                if (auto unres_inner = std::dynamic_pointer_cast<unresolved_type>(inner)) {
                    std::shared_ptr<type> resolved_inner;
                    const element* var_elem = dynamic_cast<const element*>(&var);
                    if (var_elem) {
                        resolved_inner = resolve_type_by_name(unres_inner->type_id(), *var_elem);
                    }
                    if (!resolved_inner || !type::is_resolved(resolved_inner)) {
                        resolved_inner = _context->from_string(unres_inner->type_id());
                    }
                    if (!resolved_inner || !type::is_resolved(resolved_inner)) {
                        auto imported_agg = _unit.get_or_create_imported_aggregate(
                            unres_inner->type_id(), _context);
                        if (imported_agg && imported_agg->get_struct_type()) {
                            resolved_inner = imported_agg->get_struct_type();
                        }
                    }
                    if (!resolved_inner || !type::is_resolved(resolved_inner)) {
                        auto imported_en = _unit.get_or_create_imported_enum(
                            unres_inner->type_id(), _context);
                        if (imported_en && imported_en->get_enum_type()) {
                            resolved_inner = imported_en->get_enum_type();
                        }
                    }
                    return resolved_inner;
                }
                return _context->resolve_type(inner);
            };

            std::shared_ptr<type> resolved;
            if (type::is_pointer(var.get_type())) {
                auto inner = try_resolve_wrapped(var.get_type()->get_subtype());
                if (inner && type::is_resolved(inner)) resolved = inner->get_pointer();
            } else if (type::is_link(var.get_type())) {
                auto inner = try_resolve_wrapped(var.get_type()->get_subtype());
                if (inner && type::is_resolved(inner)) resolved = inner->get_link();
            } else if (type::is_pinned(var.get_type())) {
                auto inner = try_resolve_wrapped(var.get_type()->get_subtype());
                if (inner && type::is_resolved(inner)) resolved = inner->get_pinned();
            } else if (type::is_reference(var.get_type())) {
                auto inner = try_resolve_wrapped(var.get_type()->get_subtype());
                if (inner && type::is_resolved(inner)) resolved = inner->get_reference();
            } else {
                // Fallback: delegate to context->resolve_type
                resolved = _context->resolve_type(var.get_type());
            }

            if (resolved && type::is_resolved(resolved)) {
                var.set_type(resolved);
            } else {
                throw_internal_error(0x0001, std::nullopt,
                    "Internal error: variable '{}' has an unresolvable type that is not an unresolved_type instance; "
                    "this indicates a compiler bug",
                    {var.get_fq_name()});
            }
        } else {
            // First try qualified name resolution from the unit root (handles namespaced
            // types like shapes::rect, or root-prefixed like ::shapes::rect).
            std::shared_ptr<type> resolved;
            if (unres_type->type_id().has_root_prefix()) {
                resolved = resolve_type_from_root(unres_type->type_id().without_root_prefix());
            } else {
                // Walk up from unit root namespace for global variables
                // (for local variables the block context is searched via parent chain in resolve_type_by_name)
                auto root_ns = _unit.get_root_namespace();
                if (root_ns) {
                    resolved = resolve_type_by_name(unres_type->type_id(), *root_ns);
                }
            }
            if(!resolved || !type::is_resolved(resolved)) {
                // Fall back to context->from_string (handles primitive types by string)
                resolved = _context->from_string(unres_type->type_id());
            }
            if(!resolved || !type::is_resolved(resolved)) {
                // Fall back to imported aggregates
                auto imported_agg = _unit.get_or_create_imported_aggregate(
                    unres_type->type_id(), _context);
                if (imported_agg && imported_agg->get_struct_type()) {
                    resolved = imported_agg->get_struct_type();
                }
            }
            if(!resolved || !type::is_resolved(resolved)) {
                // Fall back to imported enums
                auto imported_en = _unit.get_or_create_imported_enum(
                    unres_type->type_id(), _context);
                if (imported_en && imported_en->get_enum_type()) {
                    resolved = imported_en->get_enum_type();
                }
            }
            if(!resolved || !type::is_resolved(resolved)) {
                throw_error(0x0005, std::nullopt,
                    "Unknown type '{}' for variable '{}': no type with this name could be found in scope",
                    {unres_type->type_id().to_string(), var.get_fq_name()});
            } else {
                var.set_type(resolved);
            }
        }
        } // end else (not unresolved_function_ref_type)
    }

    // Resolve init expressions if any
    auto init_expr_base = var.get_init_expr();
    if (init_expr_base) {
        init_expr_base->accept(*this);
    }

    // For owner-type and indirection-type (pointer/link/pin) variables, the init_expr is stored
    // directly as a plain expression (symbol_expression, new_expression, etc.).
    // For all other variables it is a constructor_invocation_expression.
    auto init_expr = std::dynamic_pointer_cast<constructor_invocation_expression>(init_expr_base);

    // Helper: get the single init argument regardless of storage form.
    // For direct-stored expressions (owner/pointer/link/pin), init_expr is null but init_expr_base holds the arg.
    // For constructor_invocation_expression, use argument(0).
    auto get_single_init_arg = [&]() -> std::shared_ptr<expression> {
        if (init_expr && !init_expr->empty()) return init_expr->argument(0);
        if (!init_expr && init_expr_base) return init_expr_base;
        return nullptr;
    };
    auto has_single_init_arg = [&]() -> bool {
        return get_single_init_arg() != nullptr;
    };
    auto assign_single_init_arg = [&](std::shared_ptr<expression> new_arg) {
        if (init_expr && !init_expr->empty()) {
            init_expr->assign_argument(0, new_arg);
        } else {
            // direct-stored: update in-place
            var.set_init_expr(new_arg);
        }
    };

    // If the variable has a function_reference_type with no return type yet (e.g. 'fp : *(int) = add_one'),
    // propagate the return type from the initializer's function symbol into the existing frt in-place.
    // IMPORTANT: we must NOT replace the frt object (var.set_type(new_frt)) because any reference_type
    // wrapping it (ref<frt>) holds a weak_ptr to frt — replacing frt would expire those weak_ptrs and
    // cause use-after-free crashes in is_resolved() checks.  Mutating the existing object is safe.
    if (auto frt = std::dynamic_pointer_cast<function_reference_type>(var.get_type())) {
        if (!frt->get_return_type() && init_expr && !init_expr->empty()) {
            if (auto sym = std::dynamic_pointer_cast<symbol_expression>(init_expr->argument(0))) {
                if (sym->is_function() && sym->get_function()) {
                    auto fn_ret = sym->get_function()->get_return_type();
                    if (fn_ret) {
                        // Mutate in place — preserves all existing weak_ptr references to frt
                        frt->set_return_type(fn_ret);
                    }
                }
            }
        }
    }

    auto var_type = var.get_type();

    if (type::is_primitive(var_type)) {
        // Primitive type supports only one init expression, always try to cast it, if any.
        if (!init_expr || init_expr->empty()) {
            // If no explicit initialization, let's have 0-filled initialization:
        } else if (init_expr->size() > 1) {
            throw_error(0x0006, std::nullopt,
                "Variable '{}' of primitive type '{}' can only be initialised with a single expression, "
                "but {} were provided",
                {var.get_fq_name(), var_type ? var_type->to_string() : "?", std::to_string(init_expr->size())});
        } else if (auto expr = init_expr->argument(0)) {

            // Align init expr type to variable type
            auto cast = adapt_type(expr, var_type);
            if(!cast) {
                // TODO throw_error(0x0004, var.get_ast_for_stmt()->for_kw, "For test expression type must be convertible to bool");
            } else if(cast != expr) {
                // Casted, assign casted expression as return expr.
                init_expr->assign_argument(0, cast);
            } else {
                // Compatible type, no need to cast.
            }

        } else {
            throw_internal_error(0x0002, std::nullopt,
                "Variable '{}' of primitive type '{}' has an empty initialisation expression list; "
                "this is an internal inconsistency",
                {var.get_fq_name(), var_type ? var_type->to_string() : "?"});
        }

    } else if (auto st_type = std::dynamic_pointer_cast<struct_type>(var.get_type())) {
        // Check for designated struct init first
        auto desig_init = std::dynamic_pointer_cast<designated_struct_init_expression>(init_expr_base);
        if (desig_init) {
            // Designated struct init — resolve handled in visit_designated_struct_init_expression
            desig_init->accept(*this);
            return;
        }

        // Structure, try to find the right constructor
        auto struct_model = st_type->get_struct();
        std::vector<std::shared_ptr<expression>> ctor_args = init_expr ? init_expr->arguments() : std::vector<std::shared_ptr<expression>>{};

        // For non-static inner structs: the constructor's first parameter is __parent__.
        // If we are inside a method of the direct enclosing struct, auto-prepend 'this'.
        // Otherwise the caller must supply the parent explicitly (it is included in ctor_args).
        if (struct_model && struct_model->is_inner()) {
            auto outer_struct = struct_model->get_enclosing_structure();
            // Detect if this variable lives inside a method of the direct enclosing struct
            auto var_elem = dynamic_cast<const element*>(&var);
            bool in_outer_method = false;
            if (var_elem) {
                auto enclosing_func = var_elem->ancestor<function>();
                if (enclosing_func && enclosing_func->is_member() && !enclosing_func->is_static()) {
                    auto owner_st = enclosing_func->get_owner();
                    if (owner_st == outer_struct) {
                        in_outer_method = true;
                    }
                }
            }
            if (in_outer_method) {
                // Prepend 'this' (as a symbol expression) to the constructor arguments
                auto this_sym = symbol_expression::from_identifier(k::name("this"));
                // We need the type: it should be ref<outer_struct>
                auto func_elem = dynamic_cast<const element*>(&var);
                if (func_elem) {
                    auto enclosing_func = func_elem->ancestor<function>();
                    if (enclosing_func && enclosing_func->get_this_parameter()) {
                        this_sym->set_target(std::const_pointer_cast<parameter>(enclosing_func->get_this_parameter()));
                        this_sym->set_type(enclosing_func->get_this_parameter()->get_type());
                    }
                }
                // Auto-prepend: only if not already provided (check ctor_args count vs constructor arity)
                // We check: if the number of args already matches a constructor with __parent__, don't prepend.
                // Simple heuristic: prepend if ctor_args.size() < constructors[0].parameters().size()
                // (i.e., user didn't supply parent)
                bool needs_inject = true;
                if (!struct_model->constructors().empty()) {
                    size_t n_params = struct_model->constructors()[0]->parameters().size();
                    if (ctor_args.size() == n_params) needs_inject = false; // already has parent
                }
                if (needs_inject) {
                    ctor_args.insert(ctor_args.begin(), this_sym);
                    if (init_expr) {
                        init_expr->arguments(ctor_args);
                    }
                }
            }
        }

        // ── Direct struct copy: if single arg has the same struct type (by value or by ref),
        //    allow direct aggregate copy without a constructor.
        bool handled_as_direct_copy = false;
        if (ctor_args.size() == 1) {
            auto arg_type = ctor_args[0]->get_type();
            auto arg_type_nc = type::remove_const(arg_type);
            bool is_direct_copy = false;
            // Check bare struct type (rvalue from function return)
            if (arg_type_nc == st_type) {
                is_direct_copy = true;
            }
            // Check ref<struct> (lvalue variable)
            if (!is_direct_copy && type::is_reference(arg_type_nc)) {
                auto ref_sub = type::remove_const(std::dynamic_pointer_cast<reference_type>(arg_type_nc)->get_subtype());
                if (ref_sub == st_type) {
                    is_direct_copy = true;
                }
            }
            if (is_direct_copy) {
                // Direct copy: null constructor signals aggregate store in impl_gen
                var.set_var_constructor(nullptr);
                if (init_expr) {
                    init_expr->set_constructor(nullptr);
                    init_expr->arguments(ctor_args);
                }
                handled_as_direct_copy = true;
            }
        }

        if (!handled_as_direct_copy) {
            auto [best_constructor, adapted_args] = get_best_matching_constructor(struct_model->constructors(), ctor_args);
            if (!best_constructor) {
                throw_error(0x0008, std::nullopt,
                    "No matching constructor found for variable '{}' of type '{}': "
                    "none of the available constructors can be called with the provided arguments",
                    {var.get_fq_name(), st_type->to_string()});
            } else {
                // Check constructor visibility from the variable's declaration site
                if (auto var_elem = dynamic_cast<const element*>(&var)) {
                    check_constructor_visibility(*best_constructor, *var_elem);
                }
                var.set_var_constructor(best_constructor);
                if (init_expr) {
                    init_expr->set_constructor(best_constructor);
                    init_expr->arguments(adapted_args);
                }
            }
        }

    } else if (type::is_reference(var.get_type())) {
        // Reference variable: must be initialized at declaration, and the
        // initializer must itself be a reference (lvalue), not a bare value.
        auto ref_var_type = std::dynamic_pointer_cast<reference_type>(var.get_type());
        auto ref_sub = ref_var_type->get_subtype();

        // ------------------------------------------------------------------
        // Case A: ref to sized array, i.e.  int[N]&
        // The initialiser must be a reference to an array whose element type
        // matches.  Copy-initialisation semantics apply (see spec).
        // ------------------------------------------------------------------
        if (type::is_sized_array(ref_sub)) {
            auto dest_arr = std::dynamic_pointer_cast<sized_array_type>(ref_sub);
            if (!init_expr || init_expr->empty()) {
                throw_error(0x4101, std::nullopt,
                    "Array reference variable '{}' of type '{}' must be initialised at its declaration; "
                    "an array reference cannot be left unbound",
                    {var.get_fq_name(), var_type ? var_type->to_string() : "?"});
                return;
            }
            if (init_expr->size() > 1) {
                throw_error(0x4102, std::nullopt,
                    "Array reference variable '{}' of type '{}' must be initialised with exactly one "
                    "expression, but {} were provided",
                    {var.get_fq_name(), var_type ? var_type->to_string() : "?",
                     std::to_string(init_expr->size())});
                return;
            }
            auto arg = init_expr->argument(0);
            auto arg_type = arg ? arg->get_type() : nullptr;
            // Initialiser must be a reference to a sized array of the same element type.
            if (!arg_type || !type::is_reference(arg_type)) {
                throw_error(0x4104, std::nullopt,
                    "Array reference variable '{}' of type '{}' must be initialised with an array "
                    "reference (lvalue), but the initialiser has type '{}' which is not a reference",
                    {var.get_fq_name(), var_type ? var_type->to_string() : "?",
                     arg_type ? arg_type->to_string() : "?"});
                return;
            }
            auto arg_ref = std::dynamic_pointer_cast<reference_type>(arg_type);
            auto arg_sub = arg_ref->get_subtype();
            auto src_arr = std::dynamic_pointer_cast<sized_array_type>(arg_sub);
            if (!type::is_sized_array(arg_sub)) {
                throw_error(0x4105, std::nullopt,
                    "Array reference variable '{}' of type '{}' can only be initialised from another "
                    "array reference, but the initialiser refers to type '{}' which is not a sized array",
                    {var.get_fq_name(), var_type ? var_type->to_string() : "?",
                     arg_sub ? arg_sub->to_string() : "?"});
                return;
            }
            // Element types must match exactly.
            if (!type::are_equal(dest_arr->get_subtype(), src_arr->get_subtype())) {
                throw_error(0x4106, std::nullopt,
                    "Array reference variable '{}' of type '{}' cannot be initialised from an array of "
                    "type '{}': element types must match exactly",
                    {var.get_fq_name(), var_type ? var_type->to_string() : "?",
                     arg_type ? arg_type->to_string() : "?"});
                return;
            }
            // Validation OK — element-wise copy will be emitted at code generation time.
            return;
        }

        // ------------------------------------------------------------------
        // Case B: plain reference (non-array), e.g.  int&
        // ------------------------------------------------------------------

        // 1. Initialization is mandatory
        if (!init_expr || init_expr->empty()) {
            throw_error(0x4001, std::nullopt,
                "Reference variable '{}' of type '{}' must be initialised at its declaration: "
                "a reference is an alias for an existing object and cannot be left unbound",
                {var.get_fq_name(), var_type ? var_type->to_string() : "?"});
            return;
        }

        // 2. Only one initializer expression is allowed (e.g. ref<int> x = a, b; is invalid)
        if (init_expr->size() > 1) {
            throw_error(0x4002, std::nullopt,
                "Reference variable '{}' of type '{}' must be initialised with exactly one expression, "
                "but {} were provided",
                {var.get_fq_name(), var_type ? var_type->to_string() : "?",
                 std::to_string(init_expr->size())});
            return;
        }

        auto arg = init_expr->argument(0);
        if (!arg) {
            throw_internal_error(0x4003, std::nullopt,
                "Reference variable '{}': initialisation argument is null; "
                "this is an internal compiler inconsistency",
                {var.get_fq_name()});
            return;
        }

        auto arg_type = arg->get_type();

        // 3. Initialization must be a reference (lvalue), not a bare value: ref<T> x = y; is valid if y is ref<T>, but not if y is T.
        if (!type::is_reference(arg_type)) {
            throw_error(0x4004, std::nullopt,
                "Reference variable '{}' of type '{}' must be initialised with a reference (an addressable "
                "object), but the initialiser has type '{}' which is not a reference; "
                "you cannot bind a reference to a temporary or rvalue",
                {var.get_fq_name(), var_type ? var_type->to_string() : "?",
                 arg_type ? arg_type->to_string() : "?"});
            return;
        }

        // 4. Type compatibility check: the referenced type must match exactly or be an upcast-compatible struct type.
        auto arg_ref = std::dynamic_pointer_cast<reference_type>(arg_type);
        auto arg_sub = arg_ref ? arg_ref->get_subtype() : nullptr;
        auto var_sub = ref_var_type->get_subtype();

        if (!arg_sub || !var_sub) {
            throw_error(0x4005, std::nullopt,
                "Reference variable '{}' of type '{}' cannot be bound to an expression of type '{}': "
                "the referenced type must match exactly",
                {var.get_fq_name(), var_type ? var_type->to_string() : "?",
                 arg_type ? arg_type->to_string() : "?"});
            return;
        }

        if (!type::are_equal(arg_sub, var_sub)) {
            // Allow implicit static upcast: Derived& can bind to Base&
            auto arg_st = std::dynamic_pointer_cast<struct_type>(arg_sub);
            auto var_st = std::dynamic_pointer_cast<struct_type>(var_sub);
            bool is_static_upcast = arg_st && var_st &&
                             arg_st->get_struct() && var_st->get_struct() &&
                             arg_st->get_struct()->is_derived_from(var_st->get_struct());
            if (is_static_upcast) {
                // Insert a static cast_expression so IR can GEP to the right subobject
                auto upcast = cast_expression::make_shared(arg, var_type);
                upcast->set_type(var_type);
                init_expr->assign_argument(0, upcast);
            } else {
                // Allow implicit dynamic downcast: Base& bound to Derived& (klass/interface only)
                bool is_dynamic_downcast = arg_st && var_st &&
                    arg_st->get_struct() && var_st->get_struct() &&
                    var_st->get_struct()->is_derived_from(arg_st->get_struct()) &&
                    std::dynamic_pointer_cast<klass>(var_st->get_struct()) != nullptr;
                if (is_dynamic_downcast) {
                    // ref is non-null — fatal if RTTI check fails
                    auto dc = cast_expression::make_shared(arg, var_type, /*null_is_fatal=*/true);
                    init_expr->assign_argument(0, dc);
                } else {
                    throw_error(0x4005, std::nullopt,
                        "Reference variable '{}' of type '{}' cannot be bound to an expression of type '{}': "
                        "the referenced type must match exactly",
                        {var.get_fq_name(), var_type ? var_type->to_string() : "?",
                         arg_type ? arg_type->to_string() : "?"});
                    return;
                }
            }
        }
    } else if (type::is_pointer(var.get_type())) {
        // Pointer variable (*): validate const-compatibility of initializer and type compatibility.
        if (has_single_init_arg()) {
            if (auto arg = get_single_init_arg()) {
                // Null literal is always compatible with any pointer type — skip type checks.
                bool is_null_init = type::is_null(arg->get_type());
                if (!is_null_init) {
                    if (auto ve = std::dynamic_pointer_cast<value_expression>(arg)) {
                        is_null_init = std::holds_alternative<std::nullptr_t>(ve->get_value())
                                       || (ve->is_literal() && std::holds_alternative<lex::null>(ve->any_literal()));
                    }
                }
                if (!is_null_init) {
                auto arg_type = arg->get_type();
                auto effective_arg = arg_type;
                if (type::is_reference(arg_type)) {
                    effective_arg = std::dynamic_pointer_cast<reference_type>(arg_type)->get_subtype();
                }
                // Also accept owner as source
                if (auto own_t = std::dynamic_pointer_cast<owner_type>(effective_arg)) {
                    effective_arg = own_t->get_owned_type()->get_pointer();
                }
                auto tgt_ptr = std::dynamic_pointer_cast<pointer_type>(var.get_type());
                std::shared_ptr<type> src_sub;
                if (auto src_ptr = std::dynamic_pointer_cast<pointer_type>(effective_arg)) {
                    src_sub = src_ptr->get_subtype();
                } else if (auto src_lnk = std::dynamic_pointer_cast<link_type>(effective_arg)) {
                    src_sub = src_lnk->get_linked_type();
                } else if (auto src_pin = std::dynamic_pointer_cast<pinned_type>(effective_arg)) {
                    src_sub = src_pin->get_pinned_type();
                } else if (auto src_own = std::dynamic_pointer_cast<owner_type>(effective_arg)) {
                    src_sub = src_own->get_owned_type();
                }
                if (tgt_ptr && src_sub) {
                    auto tgt_sub = tgt_ptr->get_subtype();
                    if (type::is_const(src_sub) && !type::is_const(tgt_sub)) {
                        throw_error(0x0081, std::nullopt,
                            "Cannot initialise a pointer-to-mutable ('{}') from a pointer-to-const ('{}'): "
                            "this would allow modification of a const object through the mutable pointer",
                            {var.get_type()->to_string(), arg_type ? arg_type->to_string() : "?"});
                    }
                    auto src_sub_nc = type::remove_const(src_sub);
                    auto tgt_sub_nc = type::remove_const(tgt_sub);
                    if (!type::are_equal(src_sub_nc, tgt_sub_nc)) {
                        auto src_st = std::dynamic_pointer_cast<struct_type>(src_sub_nc);
                        auto tgt_st = std::dynamic_pointer_cast<struct_type>(tgt_sub_nc);
                        bool is_static_upcast = src_st && tgt_st &&
                                         src_st->get_struct() && tgt_st->get_struct() &&
                                         src_st->get_struct()->is_derived_from(tgt_st->get_struct());
                        if (is_static_upcast) {
                            auto upcast = cast_expression::make_shared(arg, var.get_type());
                            upcast->set_type(var.get_type());
                            assign_single_init_arg(upcast);
                        } else {
                            bool is_dynamic_downcast = src_st && tgt_st &&
                                src_st->get_struct() && tgt_st->get_struct() &&
                                tgt_st->get_struct()->is_derived_from(src_st->get_struct()) &&
                                std::dynamic_pointer_cast<klass>(tgt_st->get_struct()) != nullptr;
                            if (is_dynamic_downcast) {
                                auto dc = cast_expression::make_shared(arg, var.get_type(), /*null_is_fatal=*/false);
                                assign_single_init_arg(dc);
                            } else {
                                throw_error(0x4700, std::nullopt,
                                    "Pointer variable '{}' of type '{}' cannot be initialised from an expression of type '{}': "
                                    "the pointed types are incompatible (no inheritance relationship)",
                                    {var.get_fq_name(), var_type ? var_type->to_string() : "?",
                                     arg_type ? arg_type->to_string() : "?"});
                                return;
                            }
                        }
                    }
                } // if (tgt_ptr && src_sub)
                } // if (!is_null_init)
            }
        }
    } else if (type::is_link(var.get_type())) {
        // Link variable (~): validate const-compatibility of initializer (for rebind semantics).
        auto link_var_type = std::dynamic_pointer_cast<link_type>(var.get_type());

        if (!has_single_init_arg()) {
            throw_error(0x4501, std::nullopt,
                "Link variable '{}' of type '{}' must be initialised at its declaration: "
                "a link is non-null and cannot be left unbound",
                {var.get_fq_name(), var_type ? var_type->to_string() : "?"});
            return;
        }
        auto arg = get_single_init_arg();
        if (!arg) {
            throw_internal_error(0x4503, std::nullopt,
                "Link variable '{}': initialisation argument is null; "
                "this is an internal compiler inconsistency",
                {var.get_fq_name()});
            return;
        }
        auto arg_type = arg->get_type();
        // The initialiser must provide an address: reference, link, pinned, pointer or owner.
        if (!type::is_any_indirection(arg_type) && !type::is_owner(arg_type)) {
            throw_error(0x4504, std::nullopt,
                "Link variable '{}' of type '{}' must be initialised with an addressable expression "
                "(reference, link, pinned, pointer or owner), but the initialiser has type '{}' "
                "which is not an indirection type",
                {var.get_fq_name(), var_type ? var_type->to_string() : "?",
                 arg_type ? arg_type->to_string() : "?"});
            return;
        }
        // Const-compatibility: link-to-mutable cannot be init from link/pointer/ref-to-const
        {
            auto link_sub = link_var_type->get_linked_type();
            std::shared_ptr<type> src_pointed_type;
            auto effective_arg = arg_type;
            if (auto ref_t = std::dynamic_pointer_cast<reference_type>(arg_type)) {
                effective_arg = ref_t->get_subtype();
            }
            if (auto lnk_t = std::dynamic_pointer_cast<link_type>(effective_arg)) {
                src_pointed_type = lnk_t->get_linked_type();
            } else if (auto ptr_t = std::dynamic_pointer_cast<pointer_type>(effective_arg)) {
                src_pointed_type = ptr_t->get_pointed_type();
            } else if (auto own_t = std::dynamic_pointer_cast<owner_type>(effective_arg)) {
                src_pointed_type = own_t->get_owned_type();
            } else if (auto ref_t2 = std::dynamic_pointer_cast<reference_type>(effective_arg)) {
                src_pointed_type = ref_t2->get_subtype();
            } else if (type::is_const(effective_arg)) {
                src_pointed_type = effective_arg;
            }
            if (src_pointed_type && type::is_const(src_pointed_type) && !type::is_const(link_sub)) {
                throw_error(0x0082, std::nullopt,
                    "Cannot initialise link-to-mutable ('{}') from a const source (type '{}'): "
                    "this would allow modification of a const object",
                    {var.get_type()->to_string(), arg_type ? arg_type->to_string() : "?"});
            }
        }
        // If initialising from a nullable indirection (pinned, pointer or owner), emit a warning:
        if (type::is_nullable_indirection(arg_type) || type::is_owner(arg_type)) {
            auto diag = k::log::diagnostic::make_warning(with_flag(0x4505),
                "Link variable '{}' of type '{}' is being initialised from a nullable source "
                "(type '{}'): a runtime null-check will be inserted",
                {var.get_fq_name(), var_type ? var_type->to_string() : "?",
                 arg_type ? arg_type->to_string() : "?"});
            logger_relay::report(diag);
        }
        // Type compatibility
        {
            auto link_sub_nc = type::remove_const(link_var_type->get_linked_type());
            auto effective_arg = arg_type;
            if (auto ref_t = std::dynamic_pointer_cast<reference_type>(arg_type)) {
                effective_arg = ref_t->get_subtype();
            }
            std::shared_ptr<type> src_pointed_nc;
            if (auto lnk_t = std::dynamic_pointer_cast<link_type>(effective_arg)) {
                src_pointed_nc = type::remove_const(lnk_t->get_linked_type());
            } else if (auto ptr_t = std::dynamic_pointer_cast<pointer_type>(effective_arg)) {
                src_pointed_nc = type::remove_const(ptr_t->get_pointed_type());
            } else if (auto pin_t = std::dynamic_pointer_cast<pinned_type>(effective_arg)) {
                src_pointed_nc = type::remove_const(pin_t->get_pinned_type());
            } else if (auto own_t = std::dynamic_pointer_cast<owner_type>(effective_arg)) {
                src_pointed_nc = type::remove_const(own_t->get_owned_type());
            } else if (auto ref_t2 = std::dynamic_pointer_cast<reference_type>(effective_arg)) {
                src_pointed_nc = type::remove_const(ref_t2->get_subtype());
            }
            if (src_pointed_nc && !type::are_equal(src_pointed_nc, link_sub_nc)) {
                auto src_st = std::dynamic_pointer_cast<struct_type>(src_pointed_nc);
                auto tgt_st = std::dynamic_pointer_cast<struct_type>(link_sub_nc);
                bool is_static_upcast = src_st && tgt_st &&
                                 src_st->get_struct() && tgt_st->get_struct() &&
                                 src_st->get_struct()->is_derived_from(tgt_st->get_struct());
                if (is_static_upcast) {
                    auto upcast = cast_expression::make_shared(arg, var_type);
                    upcast->set_type(var_type);
                    assign_single_init_arg(upcast);
                } else {
                    bool is_dynamic_downcast = src_st && tgt_st &&
                        src_st->get_struct() && tgt_st->get_struct() &&
                        tgt_st->get_struct()->is_derived_from(src_st->get_struct()) &&
                        std::dynamic_pointer_cast<klass>(tgt_st->get_struct()) != nullptr;
                    if (is_dynamic_downcast) {
                        auto dc = cast_expression::make_shared(arg, var_type, /*null_is_fatal=*/true);
                        assign_single_init_arg(dc);
                    } else {
                        throw_error(0x4506, std::nullopt,
                            "Link variable '{}' of type '{}' cannot be bound to an expression of type '{}': "
                            "the linked types are incompatible (no inheritance relationship)",
                            {var.get_fq_name(), var_type ? var_type->to_string() : "?",
                             arg_type ? arg_type->to_string() : "?"});
                        return;
                    }
                }
            }
        }

    } else if (type::is_pinned(var.get_type())) {
        // Pinned variable (^): immutable (not rebindable after init), nullable.
        // Must be initialised at declaration; initialiser can be any indirection, owner or null.
        if (!has_single_init_arg()) {
            throw_error(0x4601, std::nullopt,
                "Pinned variable '{}' of type '{}' must be initialised at its declaration: "
                "a pinned indirection cannot be left unbound",
                {var.get_fq_name(), var_type ? var_type->to_string() : "?"});
            return;
        }
        auto arg = get_single_init_arg();
        if (!arg) {
            throw_internal_error(0x4603, std::nullopt,
                "Pinned variable '{}': initialisation argument is null; "
                "this is an internal compiler inconsistency",
                {var.get_fq_name()});
            return;
        }
        auto arg_type = arg->get_type();
        bool is_null_init = type::is_null(arg_type);
        if (!is_null_init) {
            if (auto ve = std::dynamic_pointer_cast<value_expression>(arg)) {
                is_null_init = std::holds_alternative<std::nullptr_t>(ve->get_value());
            }
        }
        if (!is_null_init && !type::is_any_indirection(arg_type) && !type::is_owner(arg_type)) {
            throw_error(0x4604, std::nullopt,
                "Pinned variable '{}' of type '{}' must be initialised with an addressable expression "
                "(reference, link, pinned, pointer, owner or null), but the initialiser has type '{}' "
                "which is not an indirection type",
                {var.get_fq_name(), var_type ? var_type->to_string() : "?",
                 arg_type ? arg_type->to_string() : "?"});
            return;
        }
        if (!is_null_init && (type::is_any_indirection(arg_type) || type::is_owner(arg_type))) {
            auto pin_var_type = std::dynamic_pointer_cast<pinned_type>(var.get_type());
            auto pin_sub_nc = type::remove_const(pin_var_type->get_pinned_type());
            auto effective_arg = arg_type;
            if (auto ref_t = std::dynamic_pointer_cast<reference_type>(arg_type)) {
                effective_arg = ref_t->get_subtype();
            }
            std::shared_ptr<type> src_pointed_nc;
            if (auto lnk_t = std::dynamic_pointer_cast<link_type>(effective_arg)) {
                src_pointed_nc = type::remove_const(lnk_t->get_linked_type());
            } else if (auto ptr_t = std::dynamic_pointer_cast<pointer_type>(effective_arg)) {
                src_pointed_nc = type::remove_const(ptr_t->get_pointed_type());
            } else if (auto pin_t = std::dynamic_pointer_cast<pinned_type>(effective_arg)) {
                src_pointed_nc = type::remove_const(pin_t->get_pinned_type());
            } else if (auto own_t = std::dynamic_pointer_cast<owner_type>(effective_arg)) {
                src_pointed_nc = type::remove_const(own_t->get_owned_type());
            } else if (auto ref_t2 = std::dynamic_pointer_cast<reference_type>(effective_arg)) {
                src_pointed_nc = type::remove_const(ref_t2->get_subtype());
            }
            if (src_pointed_nc && !type::are_equal(src_pointed_nc, pin_sub_nc)) {
                auto src_st = std::dynamic_pointer_cast<struct_type>(src_pointed_nc);
                auto tgt_st = std::dynamic_pointer_cast<struct_type>(pin_sub_nc);
                bool is_static_upcast = src_st && tgt_st &&
                                 src_st->get_struct() && tgt_st->get_struct() &&
                                 src_st->get_struct()->is_derived_from(tgt_st->get_struct());
                if (is_static_upcast) {
                    auto upcast = cast_expression::make_shared(arg, var_type);
                    upcast->set_type(var_type);
                    assign_single_init_arg(upcast);
                } else {
                    bool is_dynamic_downcast = src_st && tgt_st &&
                        src_st->get_struct() && tgt_st->get_struct() &&
                        tgt_st->get_struct()->is_derived_from(src_st->get_struct()) &&
                        std::dynamic_pointer_cast<klass>(tgt_st->get_struct()) != nullptr;
                    if (is_dynamic_downcast) {
                        auto dc = cast_expression::make_shared(arg, var_type, /*null_is_fatal=*/false);
                        assign_single_init_arg(dc);
                    } else {
                        throw_error(0x4605, std::nullopt,
                            "Pinned variable '{}' of type '{}' cannot be bound to an expression of type '{}': "
                            "the pinned types are incompatible (no inheritance relationship)",
                            {var.get_fq_name(), var_type ? var_type->to_string() : "?",
                             arg_type ? arg_type->to_string() : "?"});
                        return;
                    }
                }
            }
        }

    } else if (type::is_owner(var.get_type())) {
        // Owner variable (!): owns a heap-allocated object.
        // Accepted initialisers:
        //   - new_expression       → type owner<T>
        //   - another owner var    → type ref<owner<T>>  (move: wrap in owner_move_expression)
        //   - null literal         → no type (is_null_init)
        // Note: for owner variables, init_expr (constructor_invocation_expression) is null;
        // the initialiser is stored directly in init_expr_base.
        if (has_single_init_arg()) {
            auto arg = get_single_init_arg();
            if (arg) {
                auto arg_type = arg->get_type();
                // Accept: new_expression (owner<T>), null literal, or ref<owner<T>> / owner<compatible_T>
                bool is_null_init = type::is_null(arg_type);
                if (!is_null_init) {
                    if (auto ve = std::dynamic_pointer_cast<value_expression>(arg)) {
                        is_null_init = std::holds_alternative<std::nullptr_t>(ve->get_value());
                    }
                }
                if (!is_null_init) {
                    // Unwrap ref<owner<T>> to owner<T> for type checks
                    auto effective_arg_type = arg_type;
                    bool is_ref_owner = false;
                    if (type::is_reference(arg_type)) {
                        auto inner = std::dynamic_pointer_cast<reference_type>(arg_type)->get_subtype();
                        if (type::is_owner(inner)) {
                            effective_arg_type = inner;
                            is_ref_owner = true;
                        }
                    }
                    if (!type::is_owner(effective_arg_type)) {
                        throw_error(0x4802, std::nullopt,
                            "Owner variable '{}' of type '{}' must be initialised with a 'new' expression, "
                            "another owner variable, or null, but the initialiser has type '{}' which is not "
                            "an owner type",
                            {var.get_fq_name(), var_type ? var_type->to_string() : "?",
                             arg_type ? arg_type->to_string() : "?"});
                        return;
                    }
                    // Check owned-type compatibility
                    auto own_var = std::dynamic_pointer_cast<owner_type>(var_type);
                    auto own_arg = std::dynamic_pointer_cast<owner_type>(effective_arg_type);
                    if (own_var && own_arg) {
                        auto var_sub = type::remove_const(own_var->get_owned_type());
                        auto arg_sub = type::remove_const(own_arg->get_owned_type());
                        if (!type::are_equal(var_sub, arg_sub)) {
                            // Allow static upcast for polymorphic types
                            auto src_st = std::dynamic_pointer_cast<struct_type>(arg_sub);
                            auto tgt_st = std::dynamic_pointer_cast<struct_type>(var_sub);
                            bool is_upcast = src_st && tgt_st &&
                                src_st->get_struct() && tgt_st->get_struct() &&
                                src_st->get_struct()->is_derived_from(tgt_st->get_struct());
                            if (!is_upcast) {
                                throw_error(0x4803, std::nullopt,
                                    "Owner variable '{}' of type '{}' cannot be initialised from "
                                    "an owner of incompatible type '{}'",
                                    {var.get_fq_name(), var_type ? var_type->to_string() : "?",
                                     arg_type ? arg_type->to_string() : "?"});
                                return;
                            }
                        }
                    }
                    // If the source is ref<owner<T>>, wrap it in owner_move_expression
                    // (load + null source = transfer ownership)
                    if (is_ref_owner) {
                        auto move = owner_move_expression::make_shared(arg);
                        move->set_type(effective_arg_type);
                        assign_single_init_arg(move);
                    }
                }
            }
        }
    } else if (type::is_sized_array(var.get_type())) {
        // Sized array variable: int[N]
        // Check if it has an array_init_expression (brace init)
        auto arr_init = std::dynamic_pointer_cast<array_init_expression>(init_expr_base);
        if (arr_init) {
            // Array brace init — resolve handled in visit_array_init_expression
            arr_init->accept(*this);
        } else if (init_expr && !init_expr->empty()) {
            // Non-brace-init explicit initializer is not supported
            throw_error(0x4201, std::nullopt,
                "Array variable '{}' of type '{}' cannot have an explicit initialiser at declaration; "
                "use brace initialization syntax: arr : T[N] {{elem1, elem2, ...}}",
                {var.get_fq_name(), var_type ? var_type->to_string() : "?"});
            return;
        }
        // No initializer = zero-init (always valid for any element type).
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
static bool types_match_array_const_compatible(
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
type_reference_resolver::compute_cast_weight(const std::shared_ptr<expression>& expr, const std::shared_ptr<k::model::type>& tgt) {
    if (!expr || !type::is_resolved(tgt) || !type::is_resolved(expr->get_type())) {
        return CAST_IMPOSSIBLE;
    }

    auto type_src = expr->get_type();

    // Strip const from both sides: const T and T are interchangeable for value conversions.
    // Const-checking for assignment targets is done separately in visit_assignation_expression.
    auto tgt_nc = type::remove_const(tgt);

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
    if (type::is_pinned(type_src)) {
        if (type::is_pinned(tgt_nc) || type::is_pointer(tgt_nc)) {
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
        // pin<T> → ref<T>: borrow pinned target as reference
        if (type::is_reference(tgt_nc)) {
            auto tgt_sub_nc = type::remove_const(std::dynamic_pointer_cast<reference_type>(tgt_nc)->get_subtype());
            auto src_sub_nc = type::remove_const(type_src->get_subtype());
            if (src_sub_nc == tgt_sub_nc || types_match_array_const_compatible(src_sub_nc, tgt_sub_nc))
                return CAST_WIDENING;
        }
        return CAST_IMPOSSIBLE;
    }

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
            if (src_sub_nc == tgt_sub_nc) {
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
        // owner<T> → lnk<T> / pin<T>: borrow as link or pinned
        if (type::is_link(tgt_nc) || type::is_pinned(tgt_nc)) {
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
            // ref<owner<T>> → lnk<T> / pin<T>: load owner, borrow as link or pinned
            if (type::is_link(tgt_nc) || type::is_pinned(tgt_nc)) {
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
        if (type::is_pointer(inner) || type::is_link(inner) || type::is_pinned(inner)) {
            if (type::is_pointer(tgt_nc) || type::is_link(tgt_nc) || type::is_pinned(tgt_nc)) {
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
            // ref<ptr/lnk/pin<T>> → ref<T>: load indirection value, use as reference
            if (type::is_reference(tgt_nc)) {
                auto tgt_sub_nc = type::remove_const(std::dynamic_pointer_cast<reference_type>(tgt_nc)->get_subtype());
                auto src_sub_nc = type::remove_const(inner->get_subtype());
                if (src_sub_nc == tgt_sub_nc || types_match_array_const_compatible(src_sub_nc, tgt_sub_nc))
                    return CAST_REF_CONV;
            }
        }
    }

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
        // ref<T> → pin<T>: passing an object as a pinned reference
        if (type::is_pinned(tgt_nc)) {
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

    return CAST_IMPOSSIBLE;
}

std::pair<std::shared_ptr<constructor>/*best_constructor*/, std::vector<std::shared_ptr<expression>>/*adapted_args*/>
type_reference_resolver::get_best_matching_constructor(const std::vector<std::shared_ptr<constructor>>& constructors, const std::vector<std::shared_ptr<expression>>& args) {
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
        auto d = k::log::diagnostic::make_error(0x30006,
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
            auto d = k::log::diagnostic::make_error(0x30007,
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
        auto d = k::log::diagnostic::make_error(0x30007,
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
        auto d = k::log::diagnostic::make_error(0x30008,
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
        throw_error(0x0009, std::nullopt,
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
        auto d = k::log::diagnostic::make_error(0x3000A,
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
    } else {
        return expr;
    }
}

std::shared_ptr<expression> type_reference_resolver::adapt_type(std::shared_ptr<expression> expr, const std::shared_ptr<type>& type) {
    if(!expr || !type::is_resolved(type) || !type::is_resolved(expr->get_type())) {
        // Arguments must not be null, expr must have a type and types (expr and target) must be resolved.
        return nullptr;
    }

    auto type_src = expr->get_type();
    // For value-level adaptation, strip const from both sides.
    auto type_nc = type::remove_const(type);

    // ── Function reference types ────────────────────────────────────────────────
    // Case 1: target is a function_reference_type (frt)
    //   - If source is frt itself (direct function symbol): return as-is
    //     (impl_gen returns llvm::Function* directly, no load needed).
    //   - If source is ref<frt> from a direct function symbol (is_function()): return as-is —
    //     impl_gen returns the llvm::Function* directly without going through an alloca.
    //   - If source is ref<frt> from a variable (not is_function()): need a load.
    if (auto tgt_frt = std::dynamic_pointer_cast<function_reference_type>(type_nc)) {
        if (std::dynamic_pointer_cast<function_reference_type>(type_src)) {
            // Source is already a bare frt — no load needed.
            return expr;
        }
        if (type::is_reference(type_src)) {
            auto src_inner = type::remove_const(std::dynamic_pointer_cast<reference_type>(type_src)->get_subtype());
            if (std::dynamic_pointer_cast<function_reference_type>(src_inner)) {
                // Check if this is a direct function symbol (impl_gen returns Function* directly).
                auto sym = std::dynamic_pointer_cast<symbol_expression>(expr);
                if (sym && sym->is_function()) {
                    // Direct function address: no load needed, impl_gen produces the ptr directly.
                    return expr;
                }
                // Variable holding a function pointer: load the stored function pointer from the alloca.
                return adapt_reference_load_value(expr);
            }
        }
        return {};
    }
    // Case 2: source is ref<frt> and target is also ref<frt> — pass through unchanged.
    if (type::is_reference(type_nc)) {
        auto tgt_sub_nc = type::remove_const(std::dynamic_pointer_cast<reference_type>(type_nc)->get_subtype());
        if (std::dynamic_pointer_cast<function_reference_type>(tgt_sub_nc)) {
            if (type_src == type_nc || type_src == type) return expr;
            auto src_sub = type::is_reference(type_src)
                ? std::dynamic_pointer_cast<reference_type>(type_src)->get_subtype()
                : type_src;
            if (std::dynamic_pointer_cast<function_reference_type>(type::remove_const(src_sub))) {
                return expr; // compatible frt ref
            }
        }
    }
    // ── End function reference types ────────────────────────────────────────────

    if(type::is_pointer(type_src)) {
        if(type::is_pointer(type_nc) || type::is_link(type_nc)) {
            if (type_nc == type_src || type == type_src) {
                // Pointers to same type, return the expression
                return expr;
            }
            // Check struct upcast: ptr<Derived> → ptr<Base> or ptr<Derived> → lien<Base>
            auto src_sub = type_src->get_subtype();
            auto tgt_sub = type_nc->get_subtype();
            auto src_sub_nc = type::remove_const(src_sub);
            auto tgt_sub_nc = type::remove_const(tgt_sub);
            if (src_sub_nc != tgt_sub_nc) {
                auto src_st_type = std::dynamic_pointer_cast<struct_type>(src_sub_nc);
                auto tgt_st_type = std::dynamic_pointer_cast<struct_type>(tgt_sub_nc);
                if (src_st_type && tgt_st_type) {
                    auto src_st = src_st_type->get_struct();
                    auto tgt_st = tgt_st_type->get_struct();
                    if (src_st && tgt_st && src_st->is_derived_from(tgt_st)) {
                        auto upcast = cast_expression::make_shared(expr, type_nc);
                        upcast->set_type(type_nc);
                        return upcast;
                    }
                }
                return {};
            }
            return expr;
        } else {
            // ptr<T> → ref<T>: borrow pointer target as reference (LLVM-level identical)
            if (type::is_reference(type_nc)) {
                auto tgt_sub_nc = type::remove_const(std::dynamic_pointer_cast<reference_type>(type_nc)->get_subtype());
                auto src_sub_nc = type::remove_const(type_src->get_subtype());
                if (src_sub_nc == tgt_sub_nc) {
                    auto cast = cast_expression::make_shared(expr, type_nc);
                    cast->set_type(type_nc);
                    return cast;
                }
            }
            // Error : Source is a pointer, and asked to be cast to an object.
            return {};
        }
    }

    if(type::is_link(type_src)) {
        if(type::is_link(type_nc) || type::is_pointer(type_nc) || type::is_pinned(type_nc)) {
            if (type_nc == type_src || type == type_src) {
                return expr;
            }
            // Check struct upcast: lien<Derived> → lien<Base> or lien<Derived> → ptr<Base>
            auto src_sub = type_src->get_subtype();
            auto tgt_sub = type_nc->get_subtype();
            auto src_sub_nc = type::remove_const(src_sub);
            auto tgt_sub_nc = type::remove_const(tgt_sub);
            if (src_sub_nc != tgt_sub_nc) {
                auto src_st_type = std::dynamic_pointer_cast<struct_type>(src_sub_nc);
                auto tgt_st_type = std::dynamic_pointer_cast<struct_type>(tgt_sub_nc);
                if (src_st_type && tgt_st_type) {
                    auto src_st = src_st_type->get_struct();
                    auto tgt_st = tgt_st_type->get_struct();
                    if (src_st && tgt_st && src_st->is_derived_from(tgt_st)) {
                        auto upcast = cast_expression::make_shared(expr, type_nc);
                        upcast->set_type(type_nc);
                        return upcast;
                    }
                }
                return {};
            }
            return expr;
        } else {
            // lnk<T> → ref<T>: borrow link target as reference
            if (type::is_reference(type_nc)) {
                auto tgt_sub_nc = type::remove_const(std::dynamic_pointer_cast<reference_type>(type_nc)->get_subtype());
                auto src_sub_nc = type::remove_const(type_src->get_subtype());
                if (src_sub_nc == tgt_sub_nc) {
                    auto cast = cast_expression::make_shared(expr, type_nc);
                    cast->set_type(type_nc);
                    return cast;
                }
            }
            return {};
        }
    }

    if(type::is_pinned(type_src)) {
        if(type::is_pinned(type_nc) || type::is_pointer(type_nc)) {
            if (type_nc == type_src || type == type_src) {
                return expr;
            }
            // Check struct upcast: pin<Derived> → pin<Base> or pin<Derived> → ptr<Base>
            auto src_sub = type_src->get_subtype();
            auto tgt_sub = type_nc->get_subtype();
            auto src_sub_nc = type::remove_const(src_sub);
            auto tgt_sub_nc = type::remove_const(tgt_sub);
            if (src_sub_nc != tgt_sub_nc) {
                auto src_st_type = std::dynamic_pointer_cast<struct_type>(src_sub_nc);
                auto tgt_st_type = std::dynamic_pointer_cast<struct_type>(tgt_sub_nc);
                if (src_st_type && tgt_st_type) {
                    auto src_st = src_st_type->get_struct();
                    auto tgt_st = tgt_st_type->get_struct();
                    if (src_st && tgt_st && src_st->is_derived_from(tgt_st)) {
                        auto upcast = cast_expression::make_shared(expr, type_nc);
                        upcast->set_type(type_nc);
                        return upcast;
                    }
                }
                return {};
            }
            return expr;
        } else {
            // pin<T> → ref<T>: borrow pinned target as reference
            if (type::is_reference(type_nc)) {
                auto tgt_sub_nc = type::remove_const(std::dynamic_pointer_cast<reference_type>(type_nc)->get_subtype());
                auto src_sub_nc = type::remove_const(type_src->get_subtype());
                if (src_sub_nc == tgt_sub_nc) {
                    auto cast = cast_expression::make_shared(expr, type_nc);
                    cast->set_type(type_nc);
                    return cast;
                }
            }
            return {};
        }
    }

    // --- Owner type adaptation ---
    // owner<T> → owner<T>     : same type, return as-is (move semantics at IR level)
    // owner<T> → owner<Base>  : static upcast, insert cast_expression
    // owner<T> → ptr<T>       : borrow as observer pointer (just a value copy of the address)
    if (type::is_owner(type_src)) {
        auto src_sub = type_src->get_subtype();
        auto src_sub_nc = type::remove_const(src_sub);
        if (type::is_owner(type_nc)) {
            if (type_nc == type_src || type == type_src) return expr;
            auto tgt_sub_nc = type::remove_const(type_nc->get_subtype());
            if (src_sub_nc == tgt_sub_nc) return expr;
            // Upcast owner<Derived> → owner<Base>
            auto src_st = std::dynamic_pointer_cast<struct_type>(src_sub_nc);
            auto tgt_st = std::dynamic_pointer_cast<struct_type>(tgt_sub_nc);
            if (src_st && tgt_st && src_st->get_struct() && tgt_st->get_struct() &&
                src_st->get_struct()->is_derived_from(tgt_st->get_struct())) {
                auto upcast = cast_expression::make_shared(expr, type_nc);
                upcast->set_type(type_nc);
                return upcast;
            }
            return {};
        }
        if (type::is_pointer(type_nc)) {
            // Borrow as pointer observer — address is used, ownership stays in the owner
            auto tgt_sub_nc = type::remove_const(type_nc->get_subtype());
            if (src_sub_nc == tgt_sub_nc) {
                // Same subtype: reinterpret owner value as pointer (same LLVM representation)
                auto cast = cast_expression::make_shared(expr, type_nc);
                cast->set_type(type_nc);
                return cast;
            }
            // Upcast: owner<Derived> → ptr<Base>
            auto src_st = std::dynamic_pointer_cast<struct_type>(src_sub_nc);
            auto tgt_st = std::dynamic_pointer_cast<struct_type>(tgt_sub_nc);
            if (src_st && tgt_st && src_st->get_struct() && tgt_st->get_struct() &&
                src_st->get_struct()->is_derived_from(tgt_st->get_struct())) {
                auto upcast = cast_expression::make_shared(expr, type_nc);
                upcast->set_type(type_nc);
                return upcast;
            }
            return {};
        }
        // owner<T> → lnk<T> / pin<T>: borrow as link or pinned (same LLVM representation)
        if (type::is_link(type_nc) || type::is_pinned(type_nc)) {
            auto tgt_sub_nc = type::remove_const(type_nc->get_subtype());
            if (src_sub_nc == tgt_sub_nc) {
                auto cast = cast_expression::make_shared(expr, type_nc);
                cast->set_type(type_nc);
                return cast;
            }
        }
        // owner<T> → ref<T>: borrow owned object as reference (same LLVM representation)
        if (type::is_reference(type_nc)) {
            auto tgt_sub_nc = type::remove_const(std::dynamic_pointer_cast<reference_type>(type_nc)->get_subtype());
            if (src_sub_nc == tgt_sub_nc) {
                auto cast = cast_expression::make_shared(expr, type_nc);
                cast->set_type(type_nc);
                return cast;
            }
        }
        return {};
    }

    // ref<owner<T>> → ptr<T>: load the owner value (address) as an observer pointer
    // Also handles ref<const<owner<T>>> (const class member) → ptr<T>
    if (type::is_reference(type_src) && !type::is_double_reference(type_src)) {
        auto ref_src = std::dynamic_pointer_cast<reference_type>(type_src);
        auto inner = ref_src->get_subtype();
        auto inner_nc = type::remove_const(inner);
        if (type::is_owner(inner_nc)) {
            auto own_sub_nc = type::remove_const(inner_nc->get_subtype());
            // ── ref<owner<T>> → owner<T>: move ownership (load + null source) ──────
            if (type::is_owner(type_nc)) {
                auto tgt_sub_nc = type::remove_const(type_nc->get_subtype());
                if (type::are_equal(own_sub_nc, tgt_sub_nc)) {
                    auto move = owner_move_expression::make_shared(expr);
                    move->set_type(inner);  // owner<T>
                    return move;
                }
                // Upcast: ref<owner<Derived>> → owner<Base>
                auto src_st = std::dynamic_pointer_cast<struct_type>(own_sub_nc);
                auto tgt_st = std::dynamic_pointer_cast<struct_type>(tgt_sub_nc);
                if (src_st && tgt_st && src_st->get_struct() && tgt_st->get_struct() &&
                    src_st->get_struct()->is_derived_from(tgt_st->get_struct())) {
                    auto move = owner_move_expression::make_shared(expr);
                    move->set_type(inner);  // owner<Derived>
                    auto upcast = cast_expression::make_shared(move, type_nc);
                    upcast->set_type(type_nc);
                    return upcast;
                }
            }
            if (type::is_pointer(type_nc)) {
                auto tgt_sub_nc = type::remove_const(type_nc->get_subtype());
                if (own_sub_nc == tgt_sub_nc || types_match_array_const_compatible(own_sub_nc, tgt_sub_nc)) {
                    // Load the stored pointer from the owner slot
                    auto loaded = load_value_expression::make_shared(expr);
                    loaded->set_type(inner_nc);
                    // Reinterpret as pointer
                    auto cast = cast_expression::make_shared(loaded, type_nc);
                    cast->set_type(type_nc);
                    return cast;
                }
                // Upcast variant
                auto src_st = std::dynamic_pointer_cast<struct_type>(own_sub_nc);
                auto tgt_st = std::dynamic_pointer_cast<struct_type>(tgt_sub_nc);
                if (src_st && tgt_st && src_st->get_struct() && tgt_st->get_struct() &&
                    src_st->get_struct()->is_derived_from(tgt_st->get_struct())) {
                    auto loaded = load_value_expression::make_shared(expr);
                    loaded->set_type(inner_nc);
                    auto upcast = cast_expression::make_shared(loaded, type_nc);
                    upcast->set_type(type_nc);
                    return upcast;
                }
            }
            // ref<owner<T>> → lnk<T> / pin<T>: load owner, borrow as link or pinned
            if (type::is_link(type_nc) || type::is_pinned(type_nc)) {
                auto tgt_sub_nc = type::remove_const(type_nc->get_subtype());
                if (own_sub_nc == tgt_sub_nc || types_match_array_const_compatible(own_sub_nc, tgt_sub_nc)) {
                    auto loaded = load_value_expression::make_shared(expr);
                    loaded->set_type(inner_nc);
                    auto cast = cast_expression::make_shared(loaded, type_nc);
                    cast->set_type(type_nc);
                    return cast;
                }
            }
            // ref<owner<T>> → ref<T>: load owner pointer value, borrow as reference
            if (type::is_reference(type_nc)) {
                auto tgt_sub_nc = type::remove_const(std::dynamic_pointer_cast<reference_type>(type_nc)->get_subtype());
                if (own_sub_nc == tgt_sub_nc || types_match_array_const_compatible(own_sub_nc, tgt_sub_nc)) {
                    auto loaded = load_value_expression::make_shared(expr);
                    loaded->set_type(inner_nc);  // owner<T>
                    auto cast = cast_expression::make_shared(loaded, type_nc);
                    cast->set_type(type_nc);
                    return cast;
                }
            }
        }
    }

    if(type::is_double_reference(type_src)) {
        auto ref_src = std::dynamic_pointer_cast<reference_type>(type_src);
        auto deref = load_value_expression::make_shared(expr);
        deref->set_type(ref_src->get_subtype());
        expr = deref;
        type_src = ref_src->get_subtype();
    }

    if(type::is_reference(type_src)) {
        // ── ref<ptr/lnk/pin<T>> → ptr/lnk/pin<Base>: load the stored pointer then upcast ──
        // ── ref<ptr/lnk/pin<T>> → ref<T>: load the indirection value, use as reference ──
        if (!type::is_reference(type_nc)) {
            auto ref_src = std::dynamic_pointer_cast<reference_type>(type_src);
            auto inner = ref_src->get_subtype();
            if ((type::is_pointer(inner) || type::is_link(inner) || type::is_pinned(inner)) &&
                (type::is_pointer(type_nc) || type::is_link(type_nc) || type::is_pinned(type_nc))) {
                // Load the pointer value stored in the ref slot
                auto loaded = load_value_expression::make_shared(expr);
                loaded->set_type(inner);
                // Now adapt the loaded indirection to the target indirection type
                auto adapted = adapt_type(loaded, type_nc);
                return adapted ? adapted : loaded;
            }
        } else {
            // ref<ptr/lnk/pin<T>> → ref<T>: load indirection value, reinterpret as reference
            auto ref_src = std::dynamic_pointer_cast<reference_type>(type_src);
            auto inner = ref_src->get_subtype();
            auto inner_nc = type::remove_const(inner);
            if (type::is_pointer(inner_nc) || type::is_link(inner_nc) || type::is_pinned(inner_nc)) {
                auto tgt_ref = std::dynamic_pointer_cast<reference_type>(type_nc);
                auto tgt_sub_nc = type::remove_const(tgt_ref->get_subtype());
                auto src_sub_nc = type::remove_const(inner_nc->get_subtype());
                if (src_sub_nc == tgt_sub_nc || types_match_array_const_compatible(src_sub_nc, tgt_sub_nc)) {
                    auto loaded = load_value_expression::make_shared(expr);
                    loaded->set_type(inner_nc);
                    auto cast = cast_expression::make_shared(loaded, type_nc);
                    cast->set_type(type_nc);
                    return cast;
                }
            }
        }

        if(type::is_reference(type_nc)) {
            if (type_nc == type_src) {
                // Reference to same type, return the expression
                return expr;
            }
            auto src_ref = std::dynamic_pointer_cast<reference_type>(type_src);
            auto tgt_ref = std::dynamic_pointer_cast<reference_type>(type_nc);
            auto src_sub = src_ref->get_referenced_type();
            auto tgt_sub = tgt_ref->get_referenced_type();
            auto src_sub_nc = type::remove_const(src_sub);
            auto tgt_sub_nc = type::remove_const(tgt_sub);
            // Exact match: ref<T> → ref<T> (structural equality, not just pointer identity)
            if (type::are_equal(src_sub_nc, tgt_sub_nc) && type::is_const(src_sub) == type::is_const(tgt_sub)) {
                return expr;
            }
            // Mutable → const widening: ref<T> → ref<const T>
            if (src_sub_nc == tgt_sub_nc && type::is_const(tgt_sub) && !type::is_const(src_sub)) {
                // Bitwise-identical at IR level (just a different type annotation).
                // Wrap in a cast so implementation_generator passes the right LLVM type.
                auto cast = cast_expression::make_shared(expr, type_nc);
                cast->set_type(type_nc);
                return cast;
            }
            // Array element const-widening: ref<array<T>> → ref<array<const<T>>>
            // At IR level this is a no-op (same pointer).
            if (types_match_array_const_compatible(src_sub_nc, tgt_sub_nc)) {
                auto cast = cast_expression::make_shared(expr, type_nc);
                cast->set_type(type_nc);
                return cast;
            }
            // Upcast: ref<Derived> → ref<Base> (also handles const variants on both sides)
            auto src_st_type = std::dynamic_pointer_cast<struct_type>(src_sub_nc);
            auto tgt_st_type = std::dynamic_pointer_cast<struct_type>(tgt_sub_nc);
            if (src_st_type && tgt_st_type) {
                auto src_st = src_st_type->get_struct();
                auto tgt_st = tgt_st_type->get_struct();
                if (src_st && tgt_st && src_st->is_derived_from(tgt_st)) {
                    auto upcast = cast_expression::make_shared(expr, type_nc);
                    upcast->set_type(type_nc);
                    return upcast;
                }
            }
            // Sized→unsized array widening: ref<T[N]> → ref<T[]>
            // At LLVM IR level both are opaque pointers, so this is a no-op cast.
            if (auto src_sized = std::dynamic_pointer_cast<sized_array_type>(src_sub_nc)) {
                auto tgt_arr = std::dynamic_pointer_cast<array_type>(tgt_sub_nc);
                if (tgt_arr && !tgt_arr->is_sized()) {
                    auto src_elem = type::remove_const(src_sized->get_subtype());
                    auto tgt_elem = type::remove_const(tgt_arr->get_subtype());
                    if (src_elem == tgt_elem) {
                        auto cast = cast_expression::make_shared(expr, type_nc);
                        cast->set_type(type_nc);
                        return cast;
                    }
                }
            }
            return {};
        }
        auto ref_src = std::dynamic_pointer_cast<reference_type>(type_src);
        auto ref_subtype = type::remove_const(ref_src->get_subtype());
        // ── Owner move: ref<owner<T>> → owner<T> ─────────────────────────────
        // Transfer of ownership: load raw ptr AND null out the source alloca.
        if (type::is_owner(ref_subtype) && type::is_owner(type_nc)) {
            auto own_src_nc = type::remove_const(ref_subtype->get_subtype());
            auto own_tgt_nc = type::remove_const(type_nc->get_subtype());
            if (type::are_equal(own_src_nc, own_tgt_nc)) {
                auto move = owner_move_expression::make_shared(expr);
                move->set_type(type_nc);
                return move;
            }
            // Upcast owner: ref<owner<Derived>> → owner<Base>
            auto src_st = std::dynamic_pointer_cast<struct_type>(own_src_nc);
            auto tgt_st = std::dynamic_pointer_cast<struct_type>(own_tgt_nc);
            if (src_st && tgt_st && src_st->get_struct() && tgt_st->get_struct() &&
                src_st->get_struct()->is_derived_from(tgt_st->get_struct())) {
                auto move = owner_move_expression::make_shared(expr);
                move->set_type(ref_subtype); // owner<Derived>
                auto upcast = cast_expression::make_shared(move, type_nc);
                upcast->set_type(type_nc);   // owner<Base>
                return upcast;
            }
            return {}; // incompatible owner types
        }
        // ─────────────────────────────────────────────────────────────────────
        if(ref_subtype == type_nc) {
            // ref<T> -> T : simple load
            return adapt_reference_load_value(expr);
        }
        // ref<T> → link<T> or ref<T> → pin<T>: pass the address directly (LLVM ptr is compatible)
        if (type::is_link(type_nc) || type::is_pinned(type_nc)) {
            auto tgt_sub_nc = type::remove_const(type_nc->get_subtype());
            if (ref_subtype == tgt_sub_nc) {
                // Same underlying type: the ref address IS the link address — no conversion needed.
                // Wrap in cast to change the K type annotation without emitting any IR cast.
                auto cast = cast_expression::make_shared(expr, type_nc);
                cast->set_type(type_nc);
                return cast;
            }
            // Struct upcast: ref<Derived> → link<Base>
            auto src_st = std::dynamic_pointer_cast<struct_type>(ref_subtype);
            auto tgt_st = std::dynamic_pointer_cast<struct_type>(tgt_sub_nc);
            if (src_st && tgt_st && src_st->get_struct() && tgt_st->get_struct() &&
                src_st->get_struct()->is_derived_from(tgt_st->get_struct())) {
                auto cast = cast_expression::make_shared(expr, type_nc);
                cast->set_type(type_nc);
                return cast;
            }
        }
        // ref<primA> -> primB : load first, then cast between primitives
        auto prim_sub = std::dynamic_pointer_cast<primitive_type>(ref_subtype);
        auto prim_tgt = std::dynamic_pointer_cast<primitive_type>(type_nc);
        if (prim_sub && prim_tgt) {
            auto loaded = adapt_reference_load_value(expr);
            if (!loaded) return {};
            if (*prim_sub == *prim_tgt) return loaded;
            auto cast = cast_expression::make_shared(loaded, prim_tgt);
            cast->set_type(prim_tgt);
            return cast;
        }
        // ref<enum> → enum or ref<enum> → primitive: load first, then adapt
        auto ref_enum_sub = std::dynamic_pointer_cast<enum_type>(ref_subtype);
        if (ref_enum_sub) {
            auto loaded = adapt_reference_load_value(expr);
            if (!loaded) return {};
            return adapt_type(loaded, type_nc);
        }
        // ref<indirection> → bool: load the pointer then compare to null.
        if (type::is_prim_bool(type_nc)) {
            if (type::is_pointer(ref_subtype) || type::is_link(ref_subtype) ||
                type::is_pinned(ref_subtype) || type::is_owner(ref_subtype)) {
                auto loaded = adapt_reference_load_value(expr);
                if (!loaded) return {};
                auto bool_type = _context->from_type(primitive_type::BOOL);
                auto cast = cast_expression::make_shared(loaded, bool_type);
                cast->set_type(bool_type);
                return cast;
            }
        }
        return {};
    }

    // ── Indirection/null → bool: implicit null check ─────────────────────────
    // If the target is bool and the source is an indirection (ptr, link, pin, owner)
    // or the null literal type, emit a cast_expression that will be lowered to
    // ICmpNE(value, null) at codegen time.
    if (type::is_prim_bool(type_nc)) {
        if (type::is_pointer(type_src) || type::is_link(type_src) ||
            type::is_pinned(type_src) || type::is_owner(type_src) ||
            type::is_null(type_src)) {
            auto bool_type = _context->from_type(primitive_type::BOOL);
            auto cast = cast_expression::make_shared(expr, bool_type);
            cast->set_type(bool_type);
            return cast;
        }
    }
    // ─────────────────────────────────────────────────────────────────────────

    auto prim_src = std::dynamic_pointer_cast<primitive_type>(type::remove_const(expr->get_type()));
    auto prim_tgt = std::dynamic_pointer_cast<primitive_type>(type_nc);

    // ── Enum implicit conversions ──────────────────────────────────────────────
    auto enum_src = std::dynamic_pointer_cast<enum_type>(type::remove_const(expr->get_type()));
    auto enum_tgt = std::dynamic_pointer_cast<enum_type>(type_nc);

    // enum → enum (same enum): identity
    if (enum_src && enum_tgt && enum_src->get_enumeration() == enum_tgt->get_enumeration()) {
        return expr;
    }

    // enum → enum (different enums): allowed with warning (both primitive-backed)
    if (enum_src && enum_tgt && enum_src->get_enumeration() != enum_tgt->get_enumeration()) {
        // Implicit conversion between different enum types — emit a warning
        // TODO: emit a warning diagnostic here
        auto cast = cast_expression::make_shared(expr, enum_tgt);
        cast->set_type(enum_tgt);
        return cast;
    }

    // enum → primitive int: implicit (use underlying type)
    if (enum_src && !enum_tgt) {
        if (!prim_tgt) prim_tgt = std::dynamic_pointer_cast<primitive_type>(type_nc);
        if (prim_tgt) {
            auto underlying = enum_src->get_underlying_type();
            if (*underlying == *prim_tgt) {
                // Same underlying type: just reinterpret
                auto cast = cast_expression::make_shared(expr, prim_tgt);
                cast->set_type(prim_tgt);
                return cast;
            }
            // Different primitive widths: cast through underlying
            auto cast = cast_expression::make_shared(expr, prim_tgt);
            cast->set_type(prim_tgt);
            return cast;
        }
    }

    // primitive int → enum: implicit
    if (!enum_src && enum_tgt) {
        if (!prim_src) prim_src = std::dynamic_pointer_cast<primitive_type>(type::remove_const(expr->get_type()));
        if (prim_src) {
            auto cast = cast_expression::make_shared(expr, enum_tgt);
            cast->set_type(enum_tgt);
            return cast;
        }
    }
    // ── End enum conversions ───────────────────────────────────────────────────

    if(!prim_src || !prim_tgt) {
        // For non-primitive (struct/class) value types: accept if they are the same type object.
        auto src_nc = type::remove_const(expr->get_type());
        if (src_nc == type_nc) return expr;
        // Also accept struct-type upcast by value (same struct_type ptr = same type).
        if (auto src_st = std::dynamic_pointer_cast<struct_type>(src_nc)) {
            if (auto tgt_st = std::dynamic_pointer_cast<struct_type>(type_nc)) {
                if (src_st.get() == tgt_st.get()) return expr;
            }
        }
        return {};
    }

    if(*prim_src==*prim_tgt) {
        return expr;
    }

    auto cast = cast_expression::make_shared(expr, prim_tgt);
    cast->set_type(prim_tgt);
    return cast;
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

void init_order_resolver::collect_deps_for_global(
        const std::shared_ptr<global_variable_definition>& gv,
        const std::unordered_map<const static_constructor*, size_t>& sctor_index,
        const std::unordered_map<const global_variable_definition*, size_t>& gv_index,
        std::vector<std::vector<size_t>>& adj,
        size_t my_idx)
{
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

void init_order_resolver::collect_deps_for_sctor(
        const std::shared_ptr<static_constructor>& sctor,
        const std::unordered_map<const static_constructor*, size_t>& sctor_index,
        const std::unordered_map<const global_variable_definition*, size_t>& gv_index,
        std::vector<std::vector<size_t>>& adj,
        size_t my_idx)
{
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

    // Rule 2 (implicit): static members of owner struct must be initialized AFTER this SC.
    // This is handled in collect_deps_for_global (rule 3): each static member variable
    // of struct S depends on SC(S).
}

void init_order_resolver::resolve() {
    auto& ctor_func = _unit.get_global_constructor_function();
    auto& dtor_func = _unit.get_global_destructor_function();

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

    // Deduplicate adjacency lists
    for (auto& list : adj) {
        std::sort(list.begin(), list.end());
        list.erase(std::unique(list.begin(), list.end()), list.end());
    }

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
        throw_error(0x0002,
            "Cycle detected in global initialization dependency graph. "
            "The following items form a circular dependency: {}",
            {cycle_members});
    }

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
