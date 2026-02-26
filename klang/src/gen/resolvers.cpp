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
// Note: Last resolver log number: 0x40031 (type_reference_resolver)
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

#include "../model/statements.hpp"
#include "../model/expressions.hpp"

#include <queue>
#include <set>

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

bool scope_lookup::is_inside_member_function_of_or_ancestor(const element& access_site, const structure& st) {
    auto cur = access_site.shared_as<const element>();
    while (cur) {
        if (auto fn = std::dynamic_pointer_cast<const function>(cur)) {
            if (fn->is_member() && !fn->is_static()) {
                auto check_st = fn->get_owner();
                while (check_st) {
                    if (check_st.get() == &st) return true;
                    check_st = check_st->get_enclosing_structure();
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

std::shared_ptr<structure>
scope_lookup::lookup_structure(std::shared_ptr<element> elem, const std::string& name) {
    for (auto current = elem; current; current = current->parent<element>()) {
        if (auto sh = std::dynamic_pointer_cast<structure_holder>(current)) {
            if (auto st = sh->get_structure(name)) {
                return st;
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

    // Try structure
    if (auto st_holder = dynamic_cast<const structure_holder*>(&elem)) {
        if (auto st = st_holder->get_structure(first)) {
            auto res = resolve_qualified_from(*st, rest);
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
    return resolve_qualified_from(*root_ns, name);
}

void symbol_resolver::resolve()
{
    visit_unit(_unit);
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

        // Look at structures
        if (auto st_holder = dynamic_cast<const structure_holder*>(&elem)) {
            if (auto st = st_holder->get_structure(name.front())) {
                if (auto res = resolve_symbol(*st, name.without_front()); res.index()!=0) {
                    return res;
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
        // No parent element, cannot resolve symbol here
        return std::monostate{};
    }
}

void symbol_resolver::check_variable_visibility(const variable_definition& var, const element& /*access_site*/) {
    // Member variable in a struct
    if (auto mv = dynamic_cast<const member_variable_definition*>(&var)) {
        auto owner_st = mv->parent<structure>();
        if (!owner_st) return;
        auto vis = mv->get_visibility();
        if (vis == PUBLIC) return;
        // PROTECTED and PRIVATE: only from member functions of the same struct (or nested)
        // Use the function stack instead of expression parent-chain (which is unreliable).
        for (auto it = _function_stack.rbegin(); it != _function_stack.rend(); ++it) {
            const auto& fn = *it;
            if (fn->is_member() && !fn->is_static()) {
                auto check_st = fn->get_owner();
                while (check_st) {
                    if (check_st.get() == owner_st.get()) return; // accessible
                    check_st = check_st->get_enclosing_structure();
                }
            }
        }
        throw_error(0x000F, std::nullopt,
            "{} member variable '{}' of struct '{}' is not accessible here; "
            "it can only be accessed from member functions of '{}'",
            {vis == PROTECTED ? "protected" : "private",
             mv->get_short_name(), owner_st->get_short_name(), owner_st->get_short_name()});
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

    auto owner_st = func.get_owner();
    if (owner_st) {
        // Struct member function: accessible only from member functions of the same struct (or nested)
        for (auto it = _function_stack.rbegin(); it != _function_stack.rend(); ++it) {
            const auto& fn = *it;
            if (fn->is_member() && !fn->is_static()) {
                auto check_st = fn->get_owner();
                while (check_st) {
                    if (check_st.get() == owner_st.get()) return;
                    check_st = check_st->get_enclosing_structure();
                }
            }
        }
        throw_error(0x002F, std::nullopt,
            "{} member function '{}' of struct '{}' is not accessible here; "
            "it can only be called from member functions of '{}'",
            {vis == PROTECTED ? "protected" : "private",
             func.get_short_name(), owner_st->get_short_name(), owner_st->get_short_name()});
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

    auto owner_st = ctor.get_owner();
    if (!owner_st) return;

    for (auto it = _function_stack.rbegin(); it != _function_stack.rend(); ++it) {
        const auto& fn = *it;
        if (fn->is_member() && !fn->is_static()) {
            auto check_st = fn->get_owner();
            while (check_st) {
                if (check_st.get() == owner_st.get()) return;
                check_st = check_st->get_enclosing_structure();
            }
        }
    }
    throw_error(0x0030, std::nullopt,
        "{} constructor of struct '{}' is not accessible here; "
        "it can only be called from member functions of '{}'",
        {vis == PROTECTED ? "protected" : "private",
         owner_st->get_short_name(), owner_st->get_short_name()});
}

/**
 * Resolve a structure by qualified name descending from elem, without climbing to parents.
 */
std::shared_ptr<structure>
type_reference_resolver::resolve_struct_from(const element& elem, const k::name& qualified_name) {
    if (qualified_name.empty()) return {};

    if (qualified_name.size() == 1) {
        // Simple name: look for structure directly in this element
        if (auto st_holder = dynamic_cast<const structure_holder*>(&elem)) {
            if (auto st = st_holder->get_structure(qualified_name.front())) {
                return st;
            }
        }
        return {};
    }

    // Qualified: first component is namespace or struct, rest continues recursively
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

    // Try nested structure
    if (auto st_holder = dynamic_cast<const structure_holder*>(&elem)) {
        if (auto st = st_holder->get_structure(first)) {
            if (auto nested = resolve_struct_from(*st, rest)) {
                return nested;
            }
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

void type_reference_resolver::check_constructor_overload_collisions(structure& st)
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


void type_reference_resolver::visit_variable_definition(variable_definition& var)
{
    if(!type::is_resolved(var.get_type())) {
        auto unres_type = std::dynamic_pointer_cast<unresolved_type>(var.get_type());
        if(!unres_type) {
            // The type is not resolved but is not a plain unresolved_type either (e.g. it
            // is a sized_array_type or reference_type whose subtype is still unresolved).
            // Delegate to context->resolve_type which recursively resolves composite types.
            auto resolved = _context->resolve_type(var.get_type());
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
                throw_error(0x0005, std::nullopt,
                    "Unknown type '{}' for variable '{}': no type with this name could be found in scope",
                    {unres_type->type_id().to_string(), var.get_fq_name()});
            } else {
                var.set_type(resolved);
            }
        }
    }

    // Resolve init expressions if any
    auto init_expr = var.get_init_expr();
    if (init_expr) {
        init_expr->accept(*this);
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

        auto [best_constructor, adapted_args] = get_best_matching_constructor(struct_model->constructors(), ctor_args);
        if (!best_constructor) {
            throw_error(0x0008, std::nullopt,
                "No matching constructor found for variable '{}' of type '{}': "
                "none of the available constructors can be called with the provided arguments",
                {var.get_fq_name(), st_type->to_string()});
        }
        // Check constructor visibility from the variable's declaration site
        if (auto var_elem = dynamic_cast<const element*>(&var)) {
            check_constructor_visibility(*best_constructor, *var_elem);
        }
        var.set_var_constructor(best_constructor);
        if (init_expr) {
            init_expr->set_constructor(best_constructor);
            init_expr->arguments(adapted_args);
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
            if (!type::is_sized_array(arg_sub)) {
                throw_error(0x4105, std::nullopt,
                    "Array reference variable '{}' of type '{}' can only be initialised from another "
                    "array reference, but the initialiser refers to type '{}' which is not a sized array",
                    {var.get_fq_name(), var_type ? var_type->to_string() : "?",
                     arg_sub ? arg_sub->to_string() : "?"});
                return;
            }
            auto src_arr = std::dynamic_pointer_cast<sized_array_type>(arg_sub);
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

        // 4. Type compatibility check: the referenced type must match exactly (no conversions allowed, not even between compatible primitives)
        auto arg_ref = std::dynamic_pointer_cast<reference_type>(arg_type);
        auto arg_sub = arg_ref ? arg_ref->get_subtype() : nullptr;
        auto var_sub = ref_var_type->get_subtype();

        if (!arg_sub || !var_sub || !type::are_equal(arg_sub, var_sub)) {
            throw_error(0x4005, std::nullopt,
                "Reference variable '{}' of type '{}' cannot be bound to an expression of type '{}': "
                "the referenced type must match exactly",
                {var.get_fq_name(), var_type ? var_type->to_string() : "?",
                 arg_type ? arg_type->to_string() : "?"});
            return;
        }
    } else if (type::is_sized_array(var.get_type())) {
        // Sized array variable: int[N]
        // No initializer = zero-init (always valid for any element type).
        // An explicit initializer is not yet supported at declaration for value arrays.
        if (init_expr && !init_expr->empty()) {
            throw_error(0x4201, std::nullopt,
                "Array variable '{}' of type '{}' cannot have an explicit initialiser at declaration; "
                "arrays are always zero-initialised at construction",
                {var.get_fq_name(), var_type ? var_type->to_string() : "?"});
            return;
        }
    } else {
        // Unsupported construction for other types for now
        // TODO Support construction for other types (array, etc.)
    }
}

type_reference_resolver::cast_weight
type_reference_resolver::compute_cast_weight(const std::shared_ptr<expression>& expr, const std::shared_ptr<k::model::type>& tgt) {
    if (!expr || !type::is_resolved(tgt) || !type::is_resolved(expr->get_type())) {
        return CAST_IMPOSSIBLE;
    }

    auto type_src = expr->get_type();

    // --- Pointer cases ---
    if (type::is_pointer(type_src)) {
        if (type::is_pointer(tgt)) {
            // Pointed types must be identical for now, no pointer conversions supported yet.
            return (type_src == tgt) ? CAST_NONE : CAST_IMPOSSIBLE;
        }
        return CAST_IMPOSSIBLE;
    }

    // --- Double reference: unwrap one level ---
    std::shared_ptr<k::model::type> effective_src = type_src;
    if (type::is_double_reference(type_src)) {
        effective_src = std::dynamic_pointer_cast<reference_type>(type_src)->get_subtype();
    }

    // --- Reference cases ---
    if (type::is_reference(effective_src)) {
        auto ref_src = std::dynamic_pointer_cast<reference_type>(effective_src);
        if (type::is_reference(tgt)) {
            return (effective_src == tgt) ? CAST_NONE : CAST_IMPOSSIBLE;
        }
        // ref -> value: need a load
        auto sub = ref_src->get_subtype();
        if (sub == tgt) {
            return CAST_REF_CONV;
        }
        // ref -> different primitive: load + cast
        auto prim_sub = std::dynamic_pointer_cast<primitive_type>(sub);
        auto prim_tgt = std::dynamic_pointer_cast<primitive_type>(tgt);
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
        return CAST_IMPOSSIBLE;
    }

    // --- Both primitive ---
    auto prim_src = std::dynamic_pointer_cast<primitive_type>(effective_src);
    auto prim_tgt = std::dynamic_pointer_cast<primitive_type>(tgt);
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

    // --- Struct construction via single-arg constructor ---
    if (auto st_tgt = std::dynamic_pointer_cast<struct_type>(tgt)) {
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
                    valid.push_back({func, std::move(adapted), w, false, nullptr, 0, def});
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
        deref->set_type(type->get_subtype());
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
        auto ref_subtype = ref_src->get_subtype();
        if(ref_subtype == type) {
            // ref<T> -> T : simple load
            return adapt_reference_load_value(expr);
        }
        // ref<primA> -> primB : load first, then cast between primitives
        auto prim_sub = std::dynamic_pointer_cast<primitive_type>(ref_subtype);
        auto prim_tgt = std::dynamic_pointer_cast<primitive_type>(type);
        if (prim_sub && prim_tgt) {
            // Load the reference to get the value
            auto loaded = adapt_reference_load_value(expr);
            if (!loaded) return {};
            if (*prim_sub == *prim_tgt) return loaded;
            // Cast the loaded value to the target primitive type
            auto cast = cast_expression::make_shared(loaded, prim_tgt);
            cast->set_type(prim_tgt);
            return cast;
        }
        return {};
    }

    auto prim_src = std::dynamic_pointer_cast<primitive_type>(expr->get_type());
    auto prim_tgt = std::dynamic_pointer_cast<primitive_type>(type);

    if(!prim_src || !prim_tgt) {
        // Support only primitive types for now.
        // TODO support not-primitive type casting
        return {};
    }

    if(*prim_src==*prim_tgt) {
        // Trivially agree for same types
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
    auto init_expr = gv->get_init_expr();
    if (!init_expr) return;

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
        std::unordered_set<const structure*> has_sctor;
        for (auto& sc : raw_sctors) {
            if (auto owner = sc->get_owner()) has_sctor.insert(owner.get());
        }
        // Walk the root namespace recursively
        std::function<void(const ns&)> scan_ns = [&](const ns& n) {
            // Walk children to find structure nodes (structures are in _children as well as _structs)
            for (auto& child : n.get_children()) {
                if (auto st = std::dynamic_pointer_cast<structure>(child)) {
                    if (!has_sctor.count(st.get())) {
                        if (auto sdtor = st->get_static_destructor()) {
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
