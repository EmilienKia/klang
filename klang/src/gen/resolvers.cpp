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
// Note: Last resolver log number: 0x30005
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

#include <set>

namespace k::model::gen {



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
            throw_error(0x0004, std::nullopt,
                "Internal error: variable '{}' has an unresolvable type that is not an unresolved_type instance; "
                "this indicates a compiler bug",
                {var.get_fq_name()});
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
            throw_error(0x0007, std::nullopt,
                "Variable '{}' of primitive type '{}' has an empty initialisation expression list; "
                "this is an internal inconsistency",
                {var.get_fq_name(), var_type ? var_type->to_string() : "?"});
        }

    } else if (auto st_type = std::dynamic_pointer_cast<struct_type>(var.get_type())) {
        // Structure, try to find the right constructor
        auto [best_constructor, adapted_args] = get_best_matching_constructor(st_type->get_struct()->constructors(), init_expr ? init_expr->arguments() : std::vector<std::shared_ptr<expression>>{});
        if (!best_constructor) {
            throw_error(0x0008, std::nullopt,
                "No matching constructor found for variable '{}' of type '{}': "
                "none of the available constructors can be called with the provided arguments",
                {var.get_fq_name(), st_type->to_string()});
        }
        var.set_var_constructor(best_constructor);
        if (init_expr) {
            init_expr->set_constructor(best_constructor);
            init_expr->arguments(adapted_args);
        }

    } else {
        // Unsupported construction for other types for now
        // TODO Support construction for other types (ref, array, etc.)
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



} // k::model::gen
