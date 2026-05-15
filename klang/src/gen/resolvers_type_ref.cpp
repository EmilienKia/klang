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
#include "resolvers_type_ref.hpp"
#include "resolvers_aggregate.hpp"
#include "resolvers_signature.hpp"
#include "resolvers_scope_lookup.hpp"
#include "gen_helpers.hpp"
#include "gen_intrinsics.hpp"
#include "../model/imported.hpp"
#include "../model/statements.hpp"
#include "../model/expressions.hpp"
#include "../model/template.hpp"
#include "../model/template_instantiator.hpp"
#include "../model/tools/kdi_type_converter.hpp"
#include "../parse/ast.hpp"
#include <kdi.hpp>
#include <llvm/IR/DerivedTypes.h>
#include <queue>
#include <set>
#include <unordered_set>
#include <functional>
#include "../errors.hpp"
namespace k::model::gen {

namespace {

const kdi::kdi_template_def* find_kdi_template_def(const kdi::kdi_namespace& ns,
                                                   const std::string& fq_name) {
    for (const auto& tdef : ns.template_defs) {
        if (tdef.fq_name == fq_name) {
            return &tdef;
        }
    }
    for (const auto& child : ns.namespaces) {
        if (auto* found = find_kdi_template_def(child, fq_name)) {
            return found;
        }
    }
    return nullptr;
}

void collect_matching_kdi_method_template_defs(
    const kdi::kdi_namespace& ns,
    const std::string& method_name,
    size_t param_count,
    std::vector<const kdi::kdi_template_def*>& matches) {
    for (const auto& tdef : ns.template_defs) {
        if (!tdef.aggregate_signature) continue;
        for (const auto& method_sig : tdef.aggregate_signature->methods) {
            if (method_sig.name == method_name && method_sig.params.size() == param_count) {
                matches.push_back(&tdef);
                break;
            }
        }
    }
    for (const auto& child : ns.namespaces) {
        collect_matching_kdi_method_template_defs(child, method_name, param_count, matches);
    }
}

const kdi::kdi_template_def* find_imported_kdi_template_def(const unit& u,
                                                            const std::string& fq_name) {
    for (const auto& imp : u.get_imports()) {
        if (imp.kdi) {
            if (auto* found = find_kdi_template_def(imp.kdi->unit.root_ns, fq_name)) {
                return found;
            }
        }
    }
    for (const auto& tdep : u.get_transitive_kdis()) {
        if (tdep) {
            if (auto* found = find_kdi_template_def(tdep->unit.root_ns, fq_name)) {
                return found;
            }
        }
    }
    return nullptr;
}

const kdi::kdi_template_def* find_unique_imported_kdi_method_template_def(
    const unit& u,
    const std::string& method_name,
    size_t param_count) {
    std::vector<const kdi::kdi_template_def*> matches;
    for (const auto& imp : u.get_imports()) {
        if (imp.kdi) {
            collect_matching_kdi_method_template_defs(
                imp.kdi->unit.root_ns, method_name, param_count, matches);
        }
    }
    for (const auto& tdep : u.get_transitive_kdis()) {
        if (tdep) {
            collect_matching_kdi_method_template_defs(
                tdep->unit.root_ns, method_name, param_count, matches);
        }
    }
    return matches.size() == 1 ? matches.front() : nullptr;
}

std::shared_ptr<type> build_model_type_from_kdi_signature(const kdi::kdi_type& sig_type,
                                                          const kdi::kdi_template_def& tdef,
                                                          unit& owner,
                                                          const std::shared_ptr<context>& ctx) {
    std::unordered_set<std::string> param_names;
    for (const auto& param : tdef.params) {
        if (!param.name.empty()) {
            param_names.insert(param.name);
        }
    }

    ctx->push_template_param_scope(param_names);
    auto guard = std::unique_ptr<void, std::function<void(void*)>>(
        nullptr,
        [&](void*) { ctx->pop_template_param_scope(); });

    return k::model::kdi_type_to_model_type(sig_type, owner, ctx);
}

} // namespace

// type_reference_resolver

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

const tpl_info::generic_usage_descriptor*
type_reference_resolver::find_generic_usage_for_site(const variable_definition* site) const
{
    if (!site) return nullptr;
    auto it = _generic_usage_by_site.find(site);
    if (it == _generic_usage_by_site.end()) return nullptr;
    return &it->second;
}

const tpl_info::generic_usage_descriptor*
type_reference_resolver::find_generic_usage_for_receiver(const std::shared_ptr<expression>& receiver) const
{
    if (!receiver) return nullptr;

    if (auto sym = std::dynamic_pointer_cast<symbol_expression>(receiver)) {
        if (sym->is_variable_def() && sym->get_variable_def()) {
            return find_generic_usage_for_site(sym->get_variable_def().get());
        }
    }

    if (auto mem_obj = std::dynamic_pointer_cast<member_of_object_expression>(receiver)) {
        auto& member_sym = mem_obj->symbol();
        if (member_sym.is_variable_def() && member_sym.get_variable_def()) {
            if (auto usage = find_generic_usage_for_site(member_sym.get_variable_def().get())) {
                return usage;
            }
        }
        return find_generic_usage_for_receiver(std::const_pointer_cast<expression>(mem_obj->sub_expr()));
    }

    if (auto mem_ptr = std::dynamic_pointer_cast<member_of_pointer_expression>(receiver)) {
        auto& member_sym = mem_ptr->symbol();
        if (member_sym.is_variable_def() && member_sym.get_variable_def()) {
            if (auto usage = find_generic_usage_for_site(member_sym.get_variable_def().get())) {
                return usage;
            }
        }
        return find_generic_usage_for_receiver(std::const_pointer_cast<expression>(mem_ptr->sub_expr()));
    }

    if (auto unary = std::dynamic_pointer_cast<unary_expression>(receiver)) {
        return find_generic_usage_for_receiver(std::const_pointer_cast<expression>(unary->sub_expr()));
    }

    return nullptr;
}

std::shared_ptr<type>
type_reference_resolver::resolve_generic_call_return_type(
    const function& called_func,
    const std::shared_ptr<expression>& receiver_expr)
{
    if (!called_func.has_return_type() || !receiver_expr) return {};

    const auto* usage = find_generic_usage_for_receiver(receiver_expr);
    if (!usage || usage->type_bindings.empty()) return {};

    std::shared_ptr<type> declared_ret;
    if (auto ast_decl = called_func.get_ast_function_decl(); ast_decl && ast_decl->type) {
        declared_ret = _context->from_type_specifier(*ast_decl->type);
    }

    if (!declared_ret) {
        if (auto owner_agg = called_func.parent<aggregate>()) {
            auto agg_fq = owner_agg->get_fq_name();
            if (agg_fq.size() >= 2 && agg_fq[0] == ':' && agg_fq[1] == ':') {
                agg_fq = agg_fq.substr(2);
            }

            const kdi::kdi_template_def* tdef = nullptr;
            if (!agg_fq.empty()) {
                tdef = find_imported_kdi_template_def(_unit, agg_fq);
            }
            if (!tdef) {
                tdef = find_unique_imported_kdi_method_template_def(
                    _unit, called_func.get_short_name(), called_func.parameters().size());
            }

            if (tdef && tdef->aggregate_signature) {
                for (const auto& method_sig : tdef->aggregate_signature->methods) {
                    if (method_sig.name == called_func.get_short_name()
                        && method_sig.params.size() == called_func.parameters().size()) {
                        declared_ret = build_model_type_from_kdi_signature(
                            method_sig.return_type, *tdef, _unit, _context);
                        if (declared_ret) break;
                    }
                }
            }
        } else {
            auto func_fq = called_func.get_fq_name();
            if (func_fq.size() >= 2 && func_fq[0] == ':' && func_fq[1] == ':') {
                func_fq = func_fq.substr(2);
            }
            if (const auto* tdef = find_imported_kdi_template_def(_unit, func_fq)) {
                if (tdef->function_signature) {
                    declared_ret = build_model_type_from_kdi_signature(
                        tdef->function_signature->return_type, *tdef, _unit, _context);
                }
            }
        }
    }

    if (!declared_ret) return {};

    type_substitution_map subst;
    for (const auto& kv : usage->type_bindings) {
        if (kv.second) subst[kv.first] = kv.second;
    }
    if (subst.empty()) return {};

    auto specialized = substitute_type(declared_ret, subst);
    if (!specialized) return {};

    auto resolved = _context->resolve_type(specialized);
    if (!resolved) resolved = specialized;
    if (!resolved || type::contains_unresolved(resolved) || !type::is_resolved(resolved)) return {};
    return resolved;
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
    if (!tpl_agg && base_name.size() == 1) {
        for (const auto& imp : _unit.get_imports()) {
            if (imp.module_name.empty()) continue;

            auto imported_ns = _unit.get_root_namespace();
            for (std::size_t i = 0; imported_ns && i < imp.module_name.size(); ++i) {
                imported_ns = imported_ns->get_child_namespace(imp.module_name[i]);
            }

            if (!imported_ns) continue;
            if (auto st = imported_ns->get_aggregate(base_name.front())) {
                if (st->is_template()) {
                    tpl_agg = st;
                    break;
                }
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
            auto [code, msg] = format_constraint_error(
                tpl_agg->get_short_name(), ti->params, model_args, err_idx, err_reason);
            throw_error(code, lex::opt_any_lexeme{}, msg);
        }
    }

    // 4. Instantiate the template aggregate
    auto parent_ns_ptr = scope_lookup::enclosing_namespace(*tpl_agg);
    if (!parent_ns_ptr) return {};

    std::shared_ptr<aggregate> concrete_agg;
    const bool is_imported_signature_only_generic =
        tpl_agg->is_generic() && tpl_agg->get_tpl_info() && tpl_agg->get_tpl_info()->is_imported_signature_only;
    if (tpl_agg->is_generic() && !is_imported_signature_only_generic) {
        concrete_agg = template_instantiator::synthesize_generic_aggregate(
            *tpl_agg, parent_ns_ptr, _unit, _context, *this);
        if (concrete_agg) {
            // Keep one synthesized body and track per-argument usage aliases.
            const auto key = build_instantiation_key(model_args);
            ti->instantiations[key] = concrete_agg;
            record_generic_usage(*ti, model_args);
            if (auto site_var = dynamic_cast<const variable_definition*>(&context_elem)) {
                _generic_usage_by_site[site_var] = build_generic_usage_descriptor(*ti, model_args);
            }
        }
    } else {
        concrete_agg = template_instantiator::instantiate_aggregate(
            *tpl_agg, model_args, parent_ns_ptr, _unit, _context, *this);
        if (concrete_agg && tpl_agg->is_generic()) {
            const auto key = build_instantiation_key(model_args);
            ti->instantiations[key] = concrete_agg;
            record_generic_usage(*ti, model_args);
            if (auto site_var = dynamic_cast<const variable_definition*>(&context_elem)) {
                _generic_usage_by_site[site_var] = build_generic_usage_descriptor(*ti, model_args);
            }
        }
    }
    if (!concrete_agg) return {};

    // 4b. Resolve unresolved member-variable references in method bodies.
    //     The symbol_resolver skipped the template aggregate, so bare names
    //     like 'x' (meaning 'this.x') are still unresolved in the cloned body.
    template_instantiator::resolve_body_symbols(concrete_agg);

    // 4c. Inject member-initializer expressions into concrete constructor blocks.
    //     symbol_resolver::visit_constructor normally does this, but template
    //     definitions are skipped and the concrete ctors are created after that pass.
    template_instantiator::inject_constructor_member_inits(concrete_agg);

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
    } else if (type::is_enum(var_type)) {
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

        if (enum_eff_tgt->is_object_backed()) {
            auto obj_tgt = enum_eff_tgt->get_object_type();
            auto src_st = std::dynamic_pointer_cast<struct_type>(eff_src_nc);
            if (!src_st && type::is_reference(effective_src)) {
                auto src_ref = std::dynamic_pointer_cast<reference_type>(effective_src);
                src_st = std::dynamic_pointer_cast<struct_type>(
                    type::remove_const(src_ref->get_subtype()));
            }
            if (obj_tgt && src_st && src_st == obj_tgt) return CAST_WIDENING;
        }
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

    auto ctor_is_callable = [&](const std::shared_ptr<constructor>& ctor) -> bool {
        const auto& params = ctor->parameters();
        if (params.size() == arg_count) return true;
        if (params.size() < arg_count) return false;
        for (size_t i = arg_count; i < params.size(); ++i) {
            if (!params[i]->has_default_expr()) return false;
        }
        return true;
    };

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

    {
        std::vector<std::shared_ptr<constructor>> non_deleted;
        for (auto& ctor : arity_matched) {
            if (!ctor->is_deleted()) non_deleted.push_back(ctor);
        }
        if (non_deleted.empty()) {
            auto d = k::log::diagnostic::make_error(static_cast<unsigned int>(k::diag::symbol_diag::ERR_OVERLOAD_AMBIGUOUS),
                "Use of deleted constructor: a constructor matching {} argument(s) exists but has been explicitly deleted with '-> delete'",
                {std::to_string(arg_count)});
            report(d);
            return {nullptr, {}};
        }
        arity_matched = std::move(non_deleted);
    }

    struct Candidate {
        std::shared_ptr<constructor> ctor;
        std::vector<std::shared_ptr<expression>> adapted_args;
        cast_weight score;
        size_t defaults_used;
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

        for (size_t i = 0; i < arg_count; ++i) {
            auto param_type = params[i]->get_type();
            cast_weight w = compute_cast_weight(args[i], param_type);
            if (w == CAST_IMPOSSIBLE) {
                has_impossible = true;
                failed_indices.push_back(i);
            } else {
                if (w > max_weight) max_weight = w;
                auto a = adapt_type(args[i], param_type);
                adapted_args.push_back(a ? a : args[i]);
            }
        }

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

    cast_weight best_score = CAST_IMPOSSIBLE;
    size_t best_defaults = std::numeric_limits<size_t>::max();
    for (auto& cand : valid_candidates) {
        if (cand.score < best_score || (cand.score == best_score && cand.defaults_used < best_defaults)) {
            best_score = cand.score;
            best_defaults = cand.defaults_used;
        }
    }

    if (best_score == CAST_NONE && best_defaults == 0) {
        for (auto& cand : valid_candidates) {
            if (cand.score == CAST_NONE && cand.defaults_used == 0) {
                return {cand.ctor, cand.adapted_args};
            }
        }
    }

    std::vector<Candidate*> best_candidates;
    for (auto& cand : valid_candidates) {
        if (cand.score == best_score && cand.defaults_used == best_defaults) {
            best_candidates.push_back(&cand);
        }
    }

    if (best_candidates.size() > 1) {
        auto d = k::log::diagnostic::make_error(static_cast<unsigned int>(k::diag::symbol_diag::ERR_FUNC_VISIBILITY_DENIED),
            "Ambiguous constructor call: {} equally viable overloads",
            {std::to_string(best_candidates.size())});
        for (auto* c : best_candidates) {
            std::string sig;
            bool first = true;
            for (auto& p : c->ctor->parameters()) {
                if (!first) sig += ", ";
                sig += p->get_type() ? p->get_type()->to_string() : "?";
                first = false;
            }
            d.add_note("candidate constructor({})", {sig});
        }
        report(d);
        return {nullptr, {}};
    }

    return {best_candidates[0]->ctor, best_candidates[0]->adapted_args};
}


type_reference_resolver::FunctionCandidate
/**
 * Choose the best-matching function among candidates given arguments.
 */
type_reference_resolver::get_best_matching_function(
        const std::vector<std::shared_ptr<function>>& candidates,
        const std::vector<std::shared_ptr<expression>>& args,
        const std::shared_ptr<expression>& this_expr,
        const std::vector<std::shared_ptr<expression>>* direct_args)
{
    struct CandInfo {
        std::shared_ptr<function> func;
        std::vector<std::shared_ptr<expression>> adapted_args;
        cast_weight score;
        bool is_unified;
        std::shared_ptr<expression> this_for_unified;
        int preference;
        size_t defaults_used;
    };

    std::vector<CandInfo> valid;

    const auto build_concrete_member_param_types = [&](const std::shared_ptr<function>& func)
            -> std::vector<std::shared_ptr<type>>
    {
        std::vector<std::shared_ptr<type>> result;
        if (!func) return result;

        const auto* usage = find_generic_usage_for_receiver(this_expr);
        if (!usage || usage->type_bindings.empty()) return result;

        auto ast_decl = func->get_ast_function_decl();
        if (!ast_decl) return result;

        result.reserve(func->parameters().size());

        type_substitution_map subst;
        for (const auto& [name, bound] : usage->type_bindings) {
            if (bound) subst[name] = bound;
        }
        if (subst.empty()) return {};

        for (size_t i = 0; i < func->parameters().size(); ++i) {
            auto fallback = func->parameters()[i] ? func->parameters()[i]->get_type() : nullptr;
            std::shared_ptr<type> resolved = fallback;

            if (i < ast_decl->params.size() && ast_decl->params[i] && ast_decl->params[i]->type) {
                auto declared = _context->from_type_specifier(*ast_decl->params[i]->type);
                if (declared) {
                    auto specialized = substitute_type(declared, subst);
                    if (specialized) {
                        auto concrete = _context->resolve_type(specialized);
                        if (concrete && !type::contains_unresolved(concrete) && type::is_resolved(concrete)) {
                            resolved = concrete;
                        } else if (!type::contains_unresolved(specialized)) {
                            resolved = specialized;
                        }
                    }
                }
            }

            result.push_back(resolved);
        }

        return result;
    };

    auto score_with_defaults = [&](const std::vector<std::shared_ptr<expression>>& exprs,
                                   const std::vector<std::shared_ptr<parameter>>& params,
                                   const std::vector<std::shared_ptr<type>>* concrete_param_types,
                                   size_t offset = 0)
            -> std::pair<cast_weight, std::vector<std::shared_ptr<expression>>>
    {
        const auto get_param_type = [&](size_t index) -> std::shared_ptr<type> {
            if (concrete_param_types && index < concrete_param_types->size() && (*concrete_param_types)[index]) {
                return (*concrete_param_types)[index];
            }
            return params[index] ? params[index]->get_type() : nullptr;
        };

        const size_t n_params = params.size() - offset;
        const size_t n_exprs  = exprs.size();

        // Check if last param is varargs
        bool last_is_varargs = (n_params > 0) && params.back()->is_varargs();

        if (last_is_varargs) {
            // Fixed params = all except the last (varargs) param
            const size_t n_fixed = n_params - 1;
            // Must have at least enough args for fixed params
            if (n_exprs < n_fixed) {
                for (size_t i = n_exprs; i < n_fixed; ++i)
                    if (!params[offset + i]->has_default_expr()) return {CAST_IMPOSSIBLE, {}};
            }

            cast_weight max_w = CAST_NONE;
            std::vector<std::shared_ptr<expression>> adapted;

            // Score fixed params
            size_t fixed_provided = std::min(n_exprs, n_fixed);
            for (size_t i = 0; i < fixed_provided; ++i) {
                auto target_type = get_param_type(offset + i);
                auto w = compute_cast_weight(exprs[i], target_type);
                if (w == CAST_IMPOSSIBLE) return {CAST_IMPOSSIBLE, {}};
                if (w > max_w) max_w = w;
                auto a = adapt_type(exprs[i], target_type);
                adapted.push_back(a ? a : exprs[i]);
            }
            // Fill defaults for missing fixed params
            for (size_t i = fixed_provided; i < n_fixed; ++i)
                adapted.push_back(params[offset + i]->get_default_expr()->clone());

            // Score varargs trailing arguments
            size_t n_varargs_exprs = (n_exprs > n_fixed) ? (n_exprs - n_fixed) : 0;
            auto varargs_param_type = get_param_type(params.size() - 1); // T[]& or T[] (array_type)
            // Unwrap reference if parameter type is ref<T[]>
            if (type::is_reference(varargs_param_type))
                varargs_param_type = varargs_param_type->get_subtype();
            auto varargs_elem_type = varargs_param_type ? varargs_param_type->get_subtype() : nullptr; // T

            if (!varargs_elem_type) return {CAST_IMPOSSIBLE, {}};

            if (n_varargs_exprs == 1) {
                // Check if passing a single array directly (exact match to T[] or ref<T[]>)
                // Try against the original (possibly ref-wrapped) param type first
                auto orig_param_type = get_param_type(params.size() - 1);
                auto w_direct = compute_cast_weight(exprs[n_fixed], orig_param_type);
                if (w_direct == CAST_IMPOSSIBLE) {
                    // Also try against the unwrapped array type
                    w_direct = compute_cast_weight(exprs[n_fixed], varargs_param_type);
                }
                if (w_direct != CAST_IMPOSSIBLE) {
                    if (w_direct > max_w) max_w = w_direct;
                    auto a = adapt_type(exprs[n_fixed], orig_param_type);
                    if (!a) a = adapt_type(exprs[n_fixed], varargs_param_type);
                    adapted.push_back(a ? a : exprs[n_fixed]);
                    return {max_w, adapted};
                }
            }

            if (n_varargs_exprs == 0) {
                if (CAST_VARARGS_PACK > max_w) max_w = CAST_VARARGS_PACK;
            }

            // Score each trailing arg against the element type
            for (size_t i = n_fixed; i < n_exprs; ++i) {
                auto w = compute_cast_weight(exprs[i], varargs_elem_type);
                if (w == CAST_IMPOSSIBLE) return {CAST_IMPOSSIBLE, {}};
                if (w > max_w) max_w = w;
                auto a = adapt_type(exprs[i], varargs_elem_type);
                adapted.push_back(a ? a : exprs[i]);
            }

            if (n_varargs_exprs > 0 && CAST_VARARGS_PACK > max_w) max_w = CAST_VARARGS_PACK;

            return {max_w, adapted};
        }

        // Non-varargs path (original logic)
        if (n_exprs > n_params) return {CAST_IMPOSSIBLE, {}};
        if (n_exprs < n_params) {
            for (size_t i = n_exprs; i < n_params; ++i)
                if (!params[offset + i]->has_default_expr()) return {CAST_IMPOSSIBLE, {}};
        }
        cast_weight max_w = CAST_NONE;
        std::vector<std::shared_ptr<expression>> adapted;
        for (size_t i = 0; i < n_exprs; ++i) {
            auto target_type = get_param_type(offset + i);
            auto w = compute_cast_weight(exprs[i], target_type);
            if (w == CAST_IMPOSSIBLE) return {CAST_IMPOSSIBLE, {}};
            if (w > max_w) max_w = w;
            auto a = adapt_type(exprs[i], target_type);
            adapted.push_back(a ? a : exprs[i]);
        }
        for (size_t i = n_exprs; i < n_params; ++i)
            adapted.push_back(params[offset + i]->get_default_expr()->clone());
        return {max_w, adapted};
    };

    for (auto& func : candidates) {
        const auto& params = func->parameters();
        bool func_has_varargs = func->has_varargs();
        const auto concrete_member_param_types = build_concrete_member_param_types(func);
        const auto* member_param_types = concrete_member_param_types.empty() ? nullptr : &concrete_member_param_types;

        // ── Intrinsic magic: accept any arguments for intrinsic functions ────────
        // Intrinsic functions bypass normal parameter matching — they accept whatever
        // arguments the call site provides. The intrinsic codegen handler reads the
        // actual arguments directly.
        if (func->is_member() && !func->is_static() && this_expr && get_intrinsic_name(*func).has_value()) {
            // Dynamically add parameters matching the call-site argument types
            // so LLVM function signature matches.
            if (args.size() > params.size()) {
                for (size_t i = params.size(); i < args.size(); ++i) {
                    auto arg_type = args[i]->get_type();
                    if (arg_type) {
                        func->append_parameter("__intrinsic_arg_" + std::to_string(i), arg_type);
                    }
                }
            }
            // Re-read params after dynamic addition
            const auto& new_params = func->parameters();
            std::vector<std::shared_ptr<expression>> adapted_args;
            for (size_t i = 0; i < args.size(); ++i) {
                adapted_args.push_back(args[i]);
            }
            valid.push_back({func, std::move(adapted_args), CAST_NONE, false, nullptr, 0, 0});
            continue;
        }

        if (func->is_member() && !func->is_static() && this_expr) {
            if (args.size() <= params.size() || func_has_varargs) {
                auto [w, adapted] = score_with_defaults(args, params, member_param_types, 0);
                if (w != CAST_IMPOSSIBLE) {
                    size_t def = (args.size() < params.size()) ? params.size() - args.size() : 0;
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

        if (!func->is_member() || func->is_static()) {
            const auto& b_args = direct_args ? *direct_args : args;
            if (b_args.size() <= params.size() || func_has_varargs) {
                auto [w, adapted] = score_with_defaults(b_args, params, nullptr, 0);
                if (w != CAST_IMPOSSIBLE) {
                    size_t def = (b_args.size() < params.size()) ? params.size() - b_args.size() : 0;
                    valid.push_back({func, std::move(adapted), w, false, nullptr, 1, def});
                }
            }
        }

        if ((!func->is_member() || func->is_static()) && this_expr && !params.empty()
            && (args.size() <= params.size() - 1 || func_has_varargs)) {
            auto first_param_type = params[0]->get_type();
            if (type::is_reference(first_param_type)) {
                auto w_this = compute_cast_weight(this_expr, first_param_type);
                if (w_this != CAST_IMPOSSIBLE) {
                    auto [w_rest, adapted_rest] = score_with_defaults(args, params, nullptr, 1);
                    if (w_rest != CAST_IMPOSSIBLE) {
                        cast_weight total = std::max(w_this, w_rest);
                        auto adapted_this = adapt_type(this_expr, first_param_type);
                        size_t def = (args.size() < params.size() - 1) ? (params.size() - 1) - args.size() : 0;
                        valid.push_back({func, std::move(adapted_rest), total, true,
                                         adapted_this ? adapted_this : this_expr, 2, def});
                    }
                }
            }
        }
    }

    if (valid.empty()) {
        if (candidates.size() == 1 && candidates.front()) {
            auto fn = candidates.front();
            const auto& params = fn->parameters();
            const auto& b_args = direct_args ? *direct_args : args;

            // Member-call shape-compatible fallback.
            if (fn->is_member() && !fn->is_static() && this_expr
                && (args.size() <= params.size() || fn->has_varargs())) {
                    auto [w, adapted] = score_with_defaults(args, params, nullptr, 0);
                if (w != CAST_IMPOSSIBLE) {
                    return {fn, adapted, false, nullptr};
                }
            }

            // Direct/free/static shape-compatible fallback.
            if ((!fn->is_member() || fn->is_static())
                && (b_args.size() <= params.size() || fn->has_varargs())) {
                    auto [w, adapted] = score_with_defaults(b_args, params, nullptr, 0);
                if (w != CAST_IMPOSSIBLE) {
                    return {fn, adapted, false, nullptr};
                }
            }
        }

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




} // namespace k::model::gen
