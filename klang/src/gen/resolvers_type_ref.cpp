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
#include "resolvers_constexpr.hpp"
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

    // Strategy 2b: local enums/unions — navigate the namespace tree from the root.
    // resolve_struct_from only handles aggregates, so multi-part enum/union names
    // (e.g. ::k::io::StreamOutOfData) must be resolved here explicitly. This keeps
    // the resulting type identity consistent with resolve_type_by_name so that two
    // instantiations of the same template (e.g. return type vs static-factory call)
    // share one struct_type.
    {
        std::vector<k::name> candidates_names;
        candidates_names.push_back(name_without_prefix);
        if (!unit_name.empty() && name_without_prefix.front() == unit_name.back()) {
            auto rest = name_without_prefix.without_front();
            if (!rest.empty()) candidates_names.push_back(rest);
        }
        for (const auto& cand : candidates_names) {
            if (cand.size() == 1) {
                if (auto en = root_ns->get_enum(cand.front())) return en->get_enum_type();
                if (auto un = root_ns->get_union(cand.front())) return un->get_struct_type();
                continue;
            }
            auto target_ns = root_ns;
            bool found_path = true;
            for (size_t i = 0; i + 1 < cand.size(); ++i) {
                auto child = target_ns->get_child_namespace(cand[i]);
                if (child) {
                    target_ns = child;
                } else {
                    found_path = false;
                    break;
                }
            }
            if (found_path) {
                if (auto en = target_ns->get_enum(cand.back())) return en->get_enum_type();
                if (auto un = target_ns->get_union(cand.back())) return un->get_struct_type();
            }
        }
    }

    // Strategy 3: fallback — search imported modules.
    if (auto agg = _unit.get_or_create_imported_aggregate(name_without_prefix, _context)) {
        return agg->get_struct_type();
    }
    // Strategy 3b: when an imported module is redundantly used as leading
    // qualifier (e.g. ::k::Expected), retry after stripping that prefix.
    for (const auto& imp : _unit.get_imports()) {
        if (imp.module_name.empty()) continue;
        if (name_without_prefix.size() <= imp.module_name.size()) continue;
        if (!name_without_prefix.start_with(imp.module_name)) continue;
        auto rest = name_without_prefix.without_front(imp.module_name.size());
        if (auto st = resolve_struct_from(*root_ns, rest)) {
            return st->get_struct_type();
        }
        if (auto agg = _unit.get_or_create_imported_aggregate(rest, _context)) {
            return agg->get_struct_type();
        }
    }
    // Strategy 4: fallback — search imported enums.
    if (auto en = _unit.get_or_create_imported_enum(name_without_prefix, _context)) {
        return en->get_enum_type();
    }
    for (const auto& imp : _unit.get_imports()) {
        if (imp.module_name.empty()) continue;
        if (name_without_prefix.size() <= imp.module_name.size()) continue;
        if (!name_without_prefix.start_with(imp.module_name)) continue;
        auto rest = name_without_prefix.without_front(imp.module_name.size());
        if (auto en = _unit.get_or_create_imported_enum(rest, _context)) {
            return en->get_enum_type();
        }
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


    // Step 2b: aliases and typedefs. An alias is looked up before aggregates and
    // enums so that it can also shadow an outer type name, exactly like a using
    // declaration would.
    if (auto al = scope_lookup::lookup_alias(context_elem.shared_as<const element>(), type_name)) {
        bool cycle = false;
        auto aliased = scope_lookup::materialize_alias_type(
            al, _context,
            [this](const k::name& n, const element& e) { return resolve_type_by_name(n, e); },
            cycle);
        if (cycle) {
            throw_error(static_cast<unsigned int>(k::diag::alias_diag::ERR_ALIAS_CYCLE), al->get_decl_lexeme(),
                "Alias '{}' is defined in terms of itself", {al->get_fq_name()});
        }
        if (aliased) return aliased;
    }

    // Step 3: Walk the scope chain: aggregates, enums, unions, using directives
    // Walk up the scope chain looking for the type
    for (auto current = context_elem.shared_as<const element>(); current; current = current->parent<element>()) {
        if (auto st = resolve_struct_from(*current, type_name)) {
            return st->get_struct_type();
        }
        // Look for union types (single-part name)
        if (type_name.size() == 1) {
            if (auto uh_ptr = std::dynamic_pointer_cast<const union_holder>(current)) {
                if (auto un = uh_ptr->get_union(type_name.front())) {
                    return un->get_struct_type();
                }
            }
        } else {
            // Multi-part union name: navigate through namespaces
            if (auto nspc = std::dynamic_pointer_cast<const ns>(current)) {
                auto target_ns = nspc;
                bool found_path = true;
                for (size_t i = 0; i + 1 < type_name.size(); ++i) {
                    auto child = target_ns->get_child_namespace(type_name[i]);
                    if (child) {
                        target_ns = child;
                    } else {
                        found_path = false;
                        break;
                    }
                }
                if (found_path) {
                    if (auto un = target_ns->get_union(type_name.back())) {
                        return un->get_struct_type();
                    }
                }
            }
            // Multi-part union name: navigate through aggregates
            if (auto ah_ptr = std::dynamic_pointer_cast<const aggregate_holder>(current)) {
                if (auto first_agg = ah_ptr->get_aggregate(type_name.front())) {
                    std::shared_ptr<const aggregate> nav_agg = first_agg;
                    bool found_path = true;
                    for (size_t i = 1; i + 1 < type_name.size(); ++i) {
                        auto nested = nav_agg->get_aggregate(type_name[i]);
                        if (nested) {
                            nav_agg = nested;
                        } else {
                            found_path = false;
                            break;
                        }
                    }
                    if (found_path) {
                        if (auto un = nav_agg->get_union(type_name.back())) {
                            return un->get_struct_type();
                        }
                    }
                }
            }
        }
        // Check if this scope is a concrete template instantiation whose
        // template parameter names match the sought type_name.  This allows
        // nested structs inside a template to reference the outer template's
        // type parameters (e.g. UniSlot<T> inside LinkedList<int>::Node).
        if (type_name.size() == 1) {
            if (auto agg = std::dynamic_pointer_cast<const aggregate>(current)) {
                if (agg->has_tpl_args()) {
                    const auto& args = agg->get_tpl_args();
                    // Try AST template params first (available for locally-parsed templates)
                    if (auto ast_agg = agg->get_ast_aggregate_decl()) {
                        const auto& params = ast_agg->template_params;
                        const size_t count = std::min(params.size(), args.size());
                        for (size_t i = 0; i < count; ++i) {
                            if (!params[i]) continue;
                            if (std::string(params[i]->name.content) == type_name.front()) {
                                if (args[i].is_type()) return args[i].type_arg;
                                break;
                            }
                        }
                    }
                    // Fallback: find the original template by base name and use tpl_info
                    if (!agg->get_tpl_base_name().empty()) {
                        auto parent_elem = agg->parent<element>();
                        if (parent_elem) {
                            if (auto tpl_agg = resolve_struct_from(*parent_elem, k::name{agg->get_tpl_base_name()})) {
                                if (auto* ti = tpl_agg->get_tpl_info()) {
                                    const size_t count = std::min(ti->params.size(), args.size());
                                    for (size_t i = 0; i < count; ++i) {
                                        if (ti->params[i].name == type_name.front()) {
                                            if (args[i].is_type()) return args[i].type_arg;
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        // Also look for enum types (simple names only for now)
        if (type_name.size() == 1) {
            if (auto eh = std::dynamic_pointer_cast<const enum_holder>(current)) {
                if (auto en = eh->get_enum(type_name.front())) {
                    return en->get_enum_type();
                }
            }
        } else {
            // Multi-part enum name: navigate through namespaces
            if (auto nspc = std::dynamic_pointer_cast<const ns>(current)) {
                auto target_ns = nspc;
                bool found_path = true;
                for (size_t i = 0; i + 1 < type_name.size(); ++i) {
                    auto child = target_ns->get_child_namespace(type_name[i]);
                    if (child) {
                        target_ns = child;
                    } else {
                        found_path = false;
                        break;
                    }
                }
                if (found_path) {
                    if (auto en = target_ns->get_enum(type_name.back())) {
                        return en->get_enum_type();
                    }
                }
            }
            // Multi-part enum name: navigate through aggregates (nested enum)
            if (auto ah_ptr = std::dynamic_pointer_cast<const aggregate_holder>(current)) {
                if (auto first_agg = ah_ptr->get_aggregate(type_name.front())) {
                    std::shared_ptr<const aggregate> nav_agg = first_agg;
                    bool found_path = true;
                    for (size_t i = 1; i + 1 < type_name.size(); ++i) {
                        auto nested = nav_agg->get_aggregate(type_name[i]);
                        if (nested) {
                            nav_agg = nested;
                        } else {
                            found_path = false;
                            break;
                        }
                    }
                    if (found_path) {
                        if (auto en = nav_agg->get_enum(type_name.back())) {
                            return en->get_enum_type();
                        }
                    }
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
    for (const auto& imp : _unit.get_imports()) {
        if (imp.module_name.empty()) continue;
        if (type_name.size() <= imp.module_name.size()) continue;
        if (!type_name.start_with(imp.module_name)) continue;
        auto rest = type_name.without_front(imp.module_name.size());
        if (auto agg = _unit.get_or_create_imported_aggregate(rest, _context)) {
            return agg->get_struct_type();
        }
    }
    // Fallback: search imported enums
    if (auto en = _unit.get_or_create_imported_enum(type_name, _context)) {
        return en->get_enum_type();
    }
    for (const auto& imp : _unit.get_imports()) {
        if (imp.module_name.empty()) continue;
        if (type_name.size() <= imp.module_name.size()) continue;
        if (!type_name.start_with(imp.module_name)) continue;
        auto rest = type_name.without_front(imp.module_name.size());
        if (auto en = _unit.get_or_create_imported_enum(rest, _context)) {
            return en->get_enum_type();
        }
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
 * Resolve an unresolved_callable_type to a concrete callable_type.
 *
 * Steps:
 *   1. Resolve each parameter type via resolve_type_by_name / from_string.
 *   2. If member function reference: resolve the owner aggregate.
 *   3. Build using callable_type_builder.
 *   4. Cache the resolved type into the unresolved placeholder.
 */
type_reference_resolver::resolve_callable_type(
    const std::shared_ptr<unresolved_callable_type>& ufrt,
    const element& context_elem)
{
    if (!ufrt) return {};

    // Step 1: Resolve each parameter type via resolve_type_by_name / from_string
    const auto resolve_component = [&](const std::shared_ptr<type>& pt,
                                       const char* what) -> std::shared_ptr<type> {
        std::shared_ptr<type> resolved;
        if (type::is_resolved(pt)) {
            resolved = pt;
        } else if (auto u = std::dynamic_pointer_cast<unresolved_type>(pt)) {
            resolved = resolve_type_by_name(u->type_id(), context_elem);
            if (!resolved || !type::is_resolved(resolved)) {
                resolved = _context->from_string(u->type_id());
            }
        } else if (auto nested = std::dynamic_pointer_cast<unresolved_callable_type>(pt)) {
            resolved = resolve_callable_type(nested, context_elem);
        } else {
            resolved = _context->resolve_type(pt);
        }
        if (!resolved || !type::is_resolved(resolved)) {
            throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_SIGNATURE_STRUCT_NOT_FOUND), std::nullopt,
                "Cannot resolve {} in callable type", {what});
        }
        return resolved;
    };

    std::vector<std::shared_ptr<type>> resolved_params;
    for (const auto& pt : ufrt->parameter_types()) {
        resolved_params.push_back(resolve_component(pt, "parameter type"));
    }

    // Declared return type: absent means void.
    std::shared_ptr<type> resolved_return;
    if (ufrt->get_return_type()) {
        resolved_return = resolve_component(ufrt->get_return_type(), "return type");
    }

    std::vector<std::shared_ptr<type>> resolved_throws;
    for (const auto& th : ufrt->get_throws()) {
        resolved_throws.push_back(resolve_component(th, "exception type"));
    }

    callable_type_builder builder(_context);
    builder.addresser(ufrt->get_addresser());
    if (resolved_return) builder.return_type(resolved_return);
    builder.throws(resolved_throws);
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

    // Step 3: Build using callable_type_builder
    auto resolved_type = builder.build();
    // Step 4: Cache the resolved type into the unresolved placeholder
    // Cache the resolved type into the unresolved placeholder
    const_cast<unresolved_callable_type*>(ufrt.get())->resolve(resolved_type);
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

std::shared_ptr<type> type_reference_resolver::try_resolve_alias_template(
    const std::shared_ptr<unresolved_type>& unres,
    const element& context_elem)
{
    auto al = scope_lookup::lookup_alias(context_elem.shared_as<const element>(), unres->type_id());
    if (!al) return {};

    return scope_lookup::resolve_alias_template(
        al, unres, context_elem, _context,
        [this](const std::shared_ptr<type>& t, const element& e) { return resolve_type_chain(t, &e); },
        [this, &al](unsigned int code, const std::string& msg, const std::vector<std::string>& args) {
            throw_error(code, al->get_decl_lexeme(), msg, args);
        });
}

std::shared_ptr<type> type_reference_resolver::try_instantiate_template_type(
    const std::shared_ptr<unresolved_type>& unres,
    const element& context_elem)
{
    const auto& base_name = unres->type_id();
    const auto& ast_args = unres->get_ast_template_args();

    // 0. A parameterised alias renames a family of types and is resolved by
    // substitution, never by instantiating an entity of its own.
    if (auto aliased = try_resolve_alias_template(unres, context_elem)) {
        return aliased;
    }

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
            // For root-prefixed names (e.g. ::k::Optional), apply the same
            // module-name-stripping logic as resolve_type_from_root.  This is
            // needed when the unit has no imports (e.g. module k compiling itself)
            // where the multi-component import-based fallback below never fires.
            if (!tpl_agg && base_name.has_root_prefix()) {
                auto name_no_prefix = base_name.without_root_prefix();
                const auto& unit_name = _unit.get_unit_name();
                // Strategy 1: strip the module-name prefix if the first component matches
                if (!unit_name.empty() && !name_no_prefix.empty()
                    && name_no_prefix.front() == unit_name.back()) {
                    auto rest = name_no_prefix.without_front();
                    if (!rest.empty()) {
                        if (auto st = resolve_struct_from(*root_ns, rest)) {
                            if (st->is_template()) tpl_agg = st;
                        }
                    }
                }
                // Strategy 2: try without module-name stripping
                if (!tpl_agg) {
                    if (auto st = resolve_struct_from(*root_ns, name_no_prefix)) {
                        if (st->is_template()) tpl_agg = st;
                    }
                }
                // Strategy 3: strip any import-module prefix
                if (!tpl_agg) {
                    for (const auto& imp : _unit.get_imports()) {
                        if (imp.module_name.empty()) continue;
                        if (!name_no_prefix.start_with(imp.module_name)) continue;
                        auto rest = name_no_prefix.without_front(imp.module_name.size());
                        if (rest.empty()) continue;
                        if (auto st = resolve_struct_from(*root_ns, rest)) {
                            if (st->is_template()) { tpl_agg = st; break; }
                        }
                    }
                }
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
    if (!tpl_agg && base_name.size() > 1) {
        for (const auto& imp : _unit.get_imports()) {
            if (imp.module_name.empty()) continue;
            if (!base_name.start_with(imp.module_name)) continue;
            auto rest = base_name.without_front(imp.module_name.size());
            if (rest.empty()) continue;
            auto root_ns = _unit.get_root_namespace();
            if (root_ns) {
                if (auto st = resolve_struct_from(*root_ns, rest)) {
                    if (st->is_template()) {
                        tpl_agg = st;
                        break;
                    }
                }
            }
            if (auto imp_st = _unit.get_or_create_imported_aggregate(rest, _context)) {
                auto st = std::dynamic_pointer_cast<aggregate>(imp_st);
                if (st && st->is_template()) {
                    tpl_agg = st;
                    break;
                }
            }
            // Imported template definitions with bodies are re-homed (flattened)
            // directly into the consumer's root namespace under their SHORT name by
            // the kdi_importer `module <ns>;` re-parse trick (intermediate namespaces
            // such as `io` are dropped). So when the remainder still carries namespace
            // components (e.g. [io, FilterInputStream]), also try the bare short name.
            if (rest.size() > 1 && root_ns) {
                if (auto st = resolve_struct_from(*root_ns, k::name(false, rest.back()))) {
                    if (st->is_template()) {
                        tpl_agg = st;
                        break;
                    }
                }
            }
        }
    }
    // 1b. If no template aggregate found, look for template unions
    std::shared_ptr<union_type_def> tpl_union;
    if (!tpl_agg) {
        for (auto current = context_elem.shared_as<const element>(); current; current = current->parent<element>()) {
            if (auto uh = std::dynamic_pointer_cast<const union_holder>(current)) {
                if (base_name.size() == 1) {
                    if (auto un = uh->get_union(base_name.front())) {
                        if (un->is_template()) { tpl_union = un; break; }
                        return {};
                    }
                }
            }
        }
        if (!tpl_union) {
            auto root_ns = _unit.get_root_namespace();
            if (root_ns && base_name.size() == 1) {
                if (auto un = root_ns->get_union(base_name.front())) {
                    if (un->is_template()) tpl_union = un;
                }
            }
        }
        if (!tpl_union && base_name.size() == 1) {
            for (const auto& imp : _unit.get_imports()) {
                if (imp.module_name.empty()) continue;
                auto imported_ns = _unit.get_root_namespace();
                for (std::size_t i = 0; imported_ns && i < imp.module_name.size(); ++i) {
                    imported_ns = imported_ns->get_child_namespace(imp.module_name[i]);
                }
                if (!imported_ns) continue;
                if (auto un = imported_ns->get_union(base_name.front())) {
                    if (un->is_template()) { tpl_union = un; break; }
                }
            }
        }
    }
    if (!tpl_agg && !tpl_union) return {};

    tpl_info* ti = tpl_agg ? tpl_agg->get_tpl_info() : tpl_union->get_tpl_info();
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
    // Pre-resolved model-level template arguments, set by substitute_type() during
    // template instantiation when the AST arg name (e.g. "T") is no longer in scope
    // in the concrete entity. Indexed parallel to ast_args; a null slot means "use
    // the normal AST resolution path below".
    const auto& model_targs = unres->get_model_template_args();
    for (size_t i = 0; i < ast_args.size(); ++i) {
        const auto& ast_arg = ast_args[i];
        // Resolve the value parameter's declared type once, up-front, so that
        // enum-typed value params (still an unresolved_type placeholder at
        // model_builder time) are properly validated by the evaluator below.
        std::shared_ptr<type> expected_value_type;
        if (i < ti->params.size()) {
            expected_value_type = ti->params[i].value_type;
            if (expected_value_type && !type::is_resolved(expected_value_type)) {
                auto resolved_vt = _context->resolve_type(expected_value_type);
                if (resolved_vt && type::is_resolved(resolved_vt)) expected_value_type = resolved_vt;
            }
        }
        if (i < ti->params.size() && ti->params[i].is_value_param() && ast_arg->is_type()) {
            // Bare qualified name always parses as a type-specifier arg; since
            // this parameter position is a VALUE parameter, reinterpret it as
            // a constant expression (enum constant, dependent value param, ...).
            auto eval = evaluate_template_value_arg_from_type_spec(
                ast_arg->type_arg.get(), context_elem, _context, expected_value_type, &_unit);
            if (eval.is_error()) {
                throw_error(eval.error_code, lex::opt_any_lexeme{}, eval.message, eval.message_args);
            }
            if (eval.ok()) {
                model_args.push_back(template_argument::make_value(*eval.value));
                continue;
            }
            return {};
        }
        if (ast_arg->is_type()) {
            // Prefer a pre-resolved model-level template argument when present.
            if (i < model_targs.size() && model_targs[i]
                && (type::is_resolved(model_targs[i])
                    || std::dynamic_pointer_cast<struct_type>(model_targs[i]))) {
                model_args.push_back(template_argument::make_type(model_targs[i]));
                continue;
            }
            // A nested template reference produced by substitution (e.g. 'Box<T>'
            // in 'Pair<Box<T>, int>') is itself an unresolved_type carrying
            // substituted model arguments: resolve it recursively.
            if (i < model_targs.size() && model_targs[i]) {
                if (auto nested = std::dynamic_pointer_cast<unresolved_type>(model_targs[i])) {
                    if (nested->has_model_template_args()) {
                        if (auto nested_res = try_instantiate_template_type(nested, context_elem)) {
                            if (type::is_resolved(nested_res)) {
                                model_args.push_back(template_argument::make_type(nested_res));
                                continue;
                            }
                        }
                    }
                }
            }
            // Resolve the type argument through the context (normal path)
            auto arg_type = _context->from_type_specifier(*ast_arg->type_arg);
            if (!arg_type || !type::is_resolved(arg_type)) {
                // Try resolving it further through the resolver
                if (arg_type) {
                    if (auto unres_arg = std::dynamic_pointer_cast<unresolved_type>(arg_type)) {
                        // Nested template instantiation (e.g. 'Box<K>' used as a template
                        // argument, where 'K' is itself an enclosing template parameter that
                        // still needs substitution). Recurse so the inner placeholder(s) go
                        // through the very same substitution-map machinery used below for a
                        // bare placeholder -- otherwise only the base name ('Box') would be
                        // looked up (ignoring its own template arguments) and resolution
                        // would spuriously fail or match the uninstantiated template itself.
                        if (unres_arg->has_template_args()) {
                            auto nested = try_instantiate_template_type(unres_arg, context_elem);
                            if (nested && type::is_resolved(nested)) {
                                arg_type = nested;
                            }
                        }
                        // A bare template-parameter placeholder (e.g. 'I'/'O'/'T' used
                        // inside the defining template's own body) must NOT be resolved
                        // via a global/namespace name lookup — an unrelated user type
                        // that happens to share the same short name (e.g. `interface I`)
                        // would be spuriously matched. Only the substitution-map
                        // recovery paths below may legitimately resolve it.
                        if (!type::is_resolved(arg_type)
                            && !unres_arg->is_template_param_placeholder()
                            && !is_enclosing_template_param_name(context_elem, unres_arg->type_id().to_string())) {
                            auto resolved = resolve_type_by_name(unres_arg->type_id(), context_elem);
                            if (resolved && type::is_resolved(resolved)) {
                                arg_type = resolved;
                            }
                        }
                        // If still not resolved, check if we are inside a concrete template
                        // function instantiation that carries the substitution map.  This
                        // handles cases like  e : Expected<R,E>  in expected__int_int where
                        // "R" and "E" are no longer in scope but the subst map is stored on the
                        // containing function.
                        if (!type::is_resolved(arg_type)) {
                            // Check if context_elem itself is the concrete function
                            const function* owning_fn = dynamic_cast<const function*>(&context_elem);
                            if (!owning_fn) {
                                // Walk up to find the nearest enclosing function
                                if (auto parent_fn = context_elem.ancestor<function>()) {
                                    owning_fn = parent_fn.get();
                                }
                            }
                            if (owning_fn && owning_fn->has_tpl_instantiation_subst()) {
                                const auto& fn_subst = owning_fn->get_tpl_instantiation_subst();
                                auto sit = fn_subst.find(unres_arg->type_id().to_string());
                                if (sit == fn_subst.end() && !unres_arg->type_id().empty()) {
                                    sit = fn_subst.find(unres_arg->type_id().back());
                                }
                                if (sit != fn_subst.end() && sit->second && type::is_resolved(sit->second)) {
                                    arg_type = sit->second;
                                }
                            }
                            // If still not resolved, check if context is inside a
                            // template-instantiated aggregate that carries param→arg mapping.
                            if (!type::is_resolved(arg_type)) {
                                const aggregate* owning_agg = dynamic_cast<const aggregate*>(&context_elem);
                                if (!owning_agg) {
                                    if (auto parent_agg = context_elem.ancestor<aggregate>()) {
                                        owning_agg = parent_agg.get();
                                    }
                                }
                                if (owning_agg && owning_agg->has_tpl_args()) {
                                    // Reconstruct subst map from template params and args
                                    auto tpl_base_name = owning_agg->get_tpl_base_name();
                                    // Find the template definition to get param names
                                    std::shared_ptr<aggregate> tpl_def;
                                    if (auto parent_ns = owning_agg->parent<ns>()) {
                                        tpl_def = parent_ns->get_aggregate(tpl_base_name);
                                    }
                                    if (tpl_def && tpl_def->get_tpl_info()) {
                                        const auto& params = tpl_def->get_tpl_info()->params;
                                        const auto& args = owning_agg->get_tpl_args();
                                        for (size_t pi = 0; pi < params.size() && pi < args.size(); ++pi) {
                                            if (params[pi].is_type_param() && args[pi].is_type()) {
                                                std::string pname = params[pi].name;
                                                if (pname == unres_arg->type_id().to_string() ||
                                                    (!unres_arg->type_id().empty() && pname == unres_arg->type_id().back())) {
                                                    if (args[pi].type_arg && type::is_resolved(args[pi].type_arg)) {
                                                        arg_type = args[pi].type_arg;
                                                        break;
                                                    }
                                                }
                                            }
                                        }
                                    } else {
                                    }
                                } else {
                                }
                            }
                        }
                    } else {
                        // Wrapper type (pointer, owner, reference, etc.) around an unresolved inner type.
                        // Use resolve_type_chain to recursively resolve inner types.
                        auto resolved = resolve_type_chain(arg_type, &context_elem);
                        if (resolved && type::is_resolved(resolved)) {
                            arg_type = resolved;
                        }
                    }
                }
            }
            if (!arg_type || !type::is_resolved(arg_type)) return {};
            model_args.push_back(template_argument::make_type(arg_type));
        } else if (ast_arg->is_value()) {
            // Value template argument — evaluate as a compile-time constant
            // expression (literal, enum constant, dependent value parameter,
            // or an arithmetic/logical/cast combination thereof).
            auto eval = evaluate_template_value_arg(ast_arg->value_arg.get(), context_elem, _context, expected_value_type, &_unit);
            if (eval.is_error()) {
                throw_error(eval.error_code, lex::opt_any_lexeme{}, eval.message, eval.message_args);
            }
            if (!eval.ok()) return {};
            model_args.push_back(template_argument::make_value(*eval.value));
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
    {
        const element& constraint_ctx = tpl_agg ? static_cast<const element&>(*tpl_agg) : static_cast<const element&>(*tpl_union);
        for (auto& param : ti->params) {
            if (param.is_type_param() && param.constraint_type && !type::is_resolved(param.constraint_type)) {
                auto resolved = _context->resolve_type(param.constraint_type);
                if (resolved && type::is_resolved(resolved)) {
                    param.constraint_type = resolved;
                } else if (auto unres = std::dynamic_pointer_cast<unresolved_type>(param.constraint_type)) {
                    auto r = resolve_type_by_name(unres->type_id(), constraint_ctx);
                    if (r && type::is_resolved(r)) param.constraint_type = r;
                }
            }
        }
    }

    // 3d. Validate type constraints (kind filter + base-type constraint)
    {
        std::string tpl_short_name = tpl_agg ? tpl_agg->get_short_name() : tpl_union->get_short_name();
        size_t err_idx;
        std::string err_reason;
        if (!validate_template_arg_constraints(ti->params, model_args, err_idx, err_reason)) {
            auto [code, msg] = format_constraint_error(
                tpl_short_name, ti->params, model_args, err_idx, err_reason);
            throw_error(code, lex::opt_any_lexeme{}, msg);
        }
    }

    // 4. Instantiate the template (aggregate or union)
    if (tpl_union) {
        // Template union instantiation
        auto parent_ns_ptr = scope_lookup::enclosing_namespace(*tpl_union);
        if (!parent_ns_ptr) return {};

        auto concrete_union = template_instantiator::instantiate_union(
            *tpl_union, model_args, parent_ns_ptr, _unit, _context, *this);
        if (!concrete_union) return {};

        // If the union already has an LLVM type (from cache), return it
        if (concrete_union->get_struct_type()) return concrete_union->get_struct_type();

        // Create a struct_type and run LLVM type resolution for it
        auto& llvm_ctx = _context->llvm_context();
        auto* union_llvm_type = llvm::StructType::create(llvm_ctx, concrete_union->get_mangled_name() + "_union");
        auto st_type = std::make_shared<struct_type>(concrete_union->get_short_name(), std::weak_ptr<aggregate>{});
        _context->attach_llvm_struct_type(st_type, union_llvm_type);
        concrete_union->set_struct_type(st_type);
        _context->add_struct(st_type);

        // Resolve alternative types
        for (auto& alt : concrete_union->alternatives_mutable()) {
            if (alt.resolved_type && !type::is_resolved(alt.resolved_type)) {
                auto resolved = _context->resolve_type(alt.resolved_type);
                if (resolved && type::is_resolved(resolved)) {
                    alt.resolved_type = resolved;
                } else if (auto unres_alt = std::dynamic_pointer_cast<unresolved_type>(alt.resolved_type)) {
                    auto by_name = resolve_type_by_name(unres_alt->type_id(), context_elem);
                    if (by_name && type::is_resolved(by_name)) {
                        alt.resolved_type = by_name;
                    }
                }
            }
        }

        // Body finalization (discriminant + storage) is deferred to
        // declaration_generator::visit_union, which runs after the LLVM module
        // is initialized.

        return st_type;
    }

    // Template aggregate instantiation
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

    // 4bb. Inject base sub-object fields (__base_X__) for resolved bases, and
    //      recursively for every base's own bases (see
    //      template_instantiator::inject_base_subobject_fields for why the
    //      recursion matters). Must run BEFORE inject_constructor_member_inits
    //      below, which looks up these fields to inject base-constructor-call
    //      statements — without them present yet, that injection silently
    //      no-ops, leaving base constructors uncalled (e.g. base vtable
    //      pointers never initialized).
    template_instantiator::inject_base_subobject_fields(concrete_agg);
    template_instantiator::ensure_virtual_base_layout_fields(concrete_agg);

    // 4c. Inject member-initializer expressions into concrete constructor blocks.
    //     symbol_resolver::visit_constructor normally does this, but template
    //     definitions are skipped and the concrete ctors are created after that pass.
    template_instantiator::inject_constructor_member_inits(concrete_agg);

    // 5. If the concrete aggregate already has a struct_type, it was created by a
    //    previous call (e.g. the template_instantiator materialised it as a base
    //    class of another instantiation, which only builds the struct_type — not
    //    the method signatures/bodies). Ensure its signatures and bodies are still
    //    resolved (idempotent via the _resolved_instantiations guard) before
    //    returning, so callers that invoke its methods see resolved return/param
    //    types.
    if (concrete_agg->get_struct_type()) {
        resolve_instantiated_aggregate(*concrete_agg);
        return concrete_agg->get_struct_type();
    }

    // 6. Create a struct_type EARLY — before resolving member types.
    //    This is essential for self-referential types (e.g. _next : Node<T>*):
    //    the recursive try_instantiate_template_type call hits step 5 above
    //    and returns the already-created struct_type instead of recursing.
    //
    //    For unification with any KDI-imported instantiation of the same template,
    //    consult the registry on unit. The key is qualified by the template's
    //    originating namespace (its origin tag for imported templates, or its
    //    enclosing namespace for locally declared ones) so that same-named templates
    //    from different namespaces never collide (see
    //    unit::make_instantiation_registry_key / _instantiation_struct_types).
    std::string origin_ns_fq = (ti && !ti->origin_module_ns_fq.empty())
                               ? ti->origin_module_ns_fq
                               : (parent_ns_ptr ? parent_ns_ptr->get_fq_name() : std::string{});
    const std::string inst_key = unit::make_instantiation_registry_key(
        origin_ns_fq, concrete_agg->get_short_name());
    std::shared_ptr<struct_type> st_type;
    if (auto existing = _unit.find_instantiation_struct_type(inst_key)) {
        // Reuse the struct_type already created for this instantiation (possibly by
        // the KDI importer). Rebind it to this locally-synthesised concrete aggregate,
        // which carries real method/constructor bodies for code generation.
        st_type = existing;
        st_type->reassign_aggregate(concrete_agg->shared_as<aggregate>());
    } else {
        st_type = std::shared_ptr<struct_type>{
            new struct_type(concrete_agg->get_short_name(), concrete_agg->shared_as<aggregate>())};
        _context->add_struct(st_type);
        _unit.register_instantiation_struct_type(inst_key, st_type);
    }
    concrete_agg->set_struct_type(st_type);

    // 6b. Create 'this' parameters for member functions, constructors,
    //     destructor, and nested struct members (requires struct_type).
    for (auto& child : concrete_agg->get_children()) {
        if (auto fn = std::dynamic_pointer_cast<function>(child)) {
            if (fn->is_member() && !fn->is_static()) {
                fn->create_this_parameter();
            }
        }
    }
    for (auto& ctor : concrete_agg->constructors()) {
        if (ctor && !ctor->get_this_parameter()) {
            ctor->create_this_parameter();
        }
    }
    if (auto dtor = concrete_agg->get_destructor()) {
        if (!dtor->get_this_parameter()) {
            dtor->create_this_parameter();
        }
    }
    // Nested structs: create struct_type and 'this' parameters for their members
    for (auto& child : concrete_agg->get_children()) {
        auto nested = std::dynamic_pointer_cast<aggregate>(child);
        if (!nested) continue;
        if (!nested->get_struct_type()) {
            auto nested_st_type = std::make_shared<struct_type>(
                nested->get_short_name(), nested->shared_as<aggregate>());
            _context->add_struct(nested_st_type);
            nested->set_struct_type(nested_st_type);
        }
        for (auto& nc : nested->get_children()) {
            if (auto fn = std::dynamic_pointer_cast<function>(nc)) {
                if (fn->is_member() && !fn->is_static() && !fn->get_this_parameter()) {
                    fn->create_this_parameter();
                }
            }
        }
        for (auto& nc : nested->constructors()) {
            if (nc && !nc->get_this_parameter()) {
                nc->create_this_parameter();
            }
        }
        if (auto nd = nested->get_destructor()) {
            if (!nd->get_this_parameter()) {
                nd->create_this_parameter();
            }
        }
    }
    // 6c. Assign FQ (fully-qualified) name to the concrete aggregate.
    if (concrete_agg->get_fq_name().empty() && !concrete_agg->get_short_name().empty()) {
        if (auto ancestor = concrete_agg->template ancestor<named_element>()) {
            concrete_agg->assign_name(ancestor->get_name().with_back(concrete_agg->get_short_name()));
        }
    }
    // For instantiations of IMPORTED templates, override with an ORIGIN-ABSOLUTE
    // name so the mangled symbol is identical across modules (linkonce_odr/COMDAT
    // dedup). See unit::make_origin_absolute_name.
    if (ti && !ti->origin_module_ns_fq.empty()) {
        concrete_agg->assign_name(unit::make_origin_absolute_name(
            ti->origin_module_ns_fq, concrete_agg->get_short_name()));
    }
    concrete_agg->update_mangled_name();

    // 6d. Update FQ names and mangled names for children (functions, constructors, etc.)
    //     Re-derive unconditionally so an origin-absolute rename of the parent
    //     aggregate propagates to the methods' mangled symbols.
    auto update_children_names = [](aggregate& agg) {
        for (auto& child : agg.get_children()) {
            if (auto fn = std::dynamic_pointer_cast<function>(child)) {
                if (auto parent_named = fn->template parent<named_element>()) {
                    fn->assign_name(parent_named->get_name().with_back(fn->get_short_name()));
                }
                fn->update_mangled_name();
            } else if (auto gv = std::dynamic_pointer_cast<global_variable_definition>(child)) {
                // Static member variables (cloned as global_variable_definition) also need
                // their FQ/mangled name re-derived here now that the parent aggregate has
                // its own root-prefixed name: without it, declaration_generator::
                // visit_global_variable_definition emits the LLVM global with an empty
                // name (get_mangled_name() == ""), which LLVM auto-numbers as an anonymous
                // global (e.g. "@0"), breaking JIT module loading.
                if (auto parent_named = gv->template parent<named_element>()) {
                    gv->assign_name(parent_named->get_name().with_back(gv->get_short_name()));
                }
            }
        }
    };
    update_children_names(*concrete_agg);

    // Also update FQ/mangled names for nested aggregates and their children
    for (auto& child : concrete_agg->get_children()) {
        if (auto nested = std::dynamic_pointer_cast<aggregate>(child)) {
            if (auto ancestor = nested->template ancestor<named_element>()) {
                nested->assign_name(ancestor->get_name().with_back(nested->get_short_name()));
            }
            nested->update_mangled_name();
            update_children_names(*nested);
        } else if (auto nested_un = std::dynamic_pointer_cast<union_type_def>(child)) {
            // Nested unions (e.g. Expected<R,E>::Storage) were created by
            // template_instantiator before the enclosing instantiation had a
            // root-prefixed name; without this their mangled name stays empty and every
            // instantiation's union would be emitted as the same anonymous LLVM type.
            if (auto ancestor = nested_un->template ancestor<named_element>()) {
                nested_un->assign_name(ancestor->get_name().with_back(nested_un->get_short_name()));
            }
            nested_un->update_mangled_name();
            if (auto kind_enum = nested_un->get_kind_enum()) {
                // assign_name() recomputes the mangled name.
                kind_enum->assign_name(nested_un->get_name().with_back(kind_enum->get_short_name()));
            }
        }
    }

    // 6d2. (base sub-object field injection moved earlier — see 4bb above,
    //      which must run before inject_constructor_member_inits.)

    // 6e. Build vtable for class/interface instantiations.
    //     Template instantiations created here bypass symbol_resolver and
    //     model_materializer, so their vtable must be built now. See
    //     ensure_klass_vtable_built() in resolvers_common.hpp for the recursive,
    //     multi-level-hierarchy-aware implementation.
    if (auto kl = std::dynamic_pointer_cast<model::klass>(concrete_agg)) {
        ensure_klass_vtable_built(*kl);
    }

    // 7. Transitively resolve member variable types containing unresolved
    //    template types (e.g. _head : LinkedListNode<T>* in LinkedList).
    //    Self-referential pointers (e.g. _next : Node<T>*) safely resolve
    //    because the struct_type was created in step 6 above — recursive
    //    calls hit step 5 and return immediately.
    //
    //    Also remap struct_type references that point to a template's nested
    //    aggregate to the corresponding cloned nested aggregate's struct_type.
    //    This happens because aggregate_type_resolver resolves member types
    //    inside the template body before instantiation.

    // Build a map from template nested aggregate → cloned nested aggregate's struct_type
    // for quick remapping of already-resolved types.
    std::unordered_map<aggregate*, std::shared_ptr<struct_type>> nested_remap;
    if (tpl_agg) {
        for (auto& tpl_child : tpl_agg->get_children()) {
            auto tpl_nested = std::dynamic_pointer_cast<aggregate>(tpl_child);
            if (!tpl_nested) continue;
            // Find the corresponding cloned nested in concrete_agg
            for (auto& conc_child : concrete_agg->get_children()) {
                auto conc_nested = std::dynamic_pointer_cast<aggregate>(conc_child);
                if (!conc_nested) continue;
                if (conc_nested->get_short_name() == tpl_nested->get_short_name()) {
                    // Create struct_type for the cloned nested if it doesn't have one yet
                    if (!conc_nested->get_struct_type()) {
                        auto nested_st = std::make_shared<struct_type>(
                            conc_nested->get_short_name(), conc_nested->shared_as<aggregate>());
                        _context->add_struct(nested_st);
                        conc_nested->set_struct_type(nested_st);
                    }
                    nested_remap[tpl_nested.get()] = conc_nested->get_struct_type();
                    break;
                }
            }
        }
    }

    auto remap_nested_type = [&](const std::shared_ptr<type>& t) -> std::shared_ptr<type> {
        if (!t || nested_remap.empty()) return t;
        // Unwrap unresolved types that have been resolved
        std::shared_ptr<type> effective = t;
        if (auto ut = std::dynamic_pointer_cast<unresolved_type>(t)) {
            if (ut->is_resolved()) effective = ut->get_resolved();
        }
        // Direct struct_type match
        if (auto st = std::dynamic_pointer_cast<struct_type>(effective)) {
            auto agg = st->get_struct();
            if (agg) {
                auto it = nested_remap.find(agg.get());
                if (it != nested_remap.end()) return it->second;
            }
            return t;
        }
        // Wrapped struct_type (owner, pointer, reference, etc.)
        auto inner = effective->get_subtype();
        if (!inner) return t;
        // Recursive peel
        std::function<std::shared_ptr<type>(const std::shared_ptr<type>&)> remap_recursive;
        remap_recursive = [&](const std::shared_ptr<type>& ty) -> std::shared_ptr<type> {
            if (!ty) return ty;
            if (auto st = std::dynamic_pointer_cast<struct_type>(ty)) {
                auto agg = st->get_struct();
                if (agg) {
                    auto it = nested_remap.find(agg.get());
                    if (it != nested_remap.end()) return it->second;
                }
                return ty;
            }
            // Handle resolved unresolved_types — unwrap to see if they point
            // to a template-internal struct that needs remapping.
            if (auto ut = std::dynamic_pointer_cast<unresolved_type>(ty)) {
                if (ut->is_resolved()) {
                    auto resolved = ut->get_resolved();
                    if (auto st = std::dynamic_pointer_cast<struct_type>(resolved)) {
                        auto agg = st->get_struct();
                        if (agg) {
                            auto it = nested_remap.find(agg.get());
                            if (it != nested_remap.end()) return it->second;
                        }
                    }
                }
                return ty;
            }
            auto sub = ty->get_subtype();
            if (!sub) return ty;
            auto new_sub = remap_recursive(sub);
            if (new_sub == sub) return ty;
            if (type::is_reference(ty))   return new_sub->get_reference();
            if (type::is_pointer(ty))     return new_sub->get_pointer();
            if (type::is_link(ty))        return new_sub->get_link();
            if (type::is_view(ty))        return new_sub->get_view();
            if (type::is_owner(ty))       return new_sub->get_owner();
            if (type::is_drain(ty))       return new_sub->get_drain();
            if (type::is_const(ty))       return new_sub->get_const();
            if (type::is_array(ty)) {
                if (auto sa = std::dynamic_pointer_cast<sized_array_type>(ty))
                    return new_sub->get_array(sa->get_size());
                return new_sub->get_array();
            }
            return ty;
        };
        auto remapped = remap_recursive(effective);
        if (remapped != effective) return remapped;
        return t;
    };

    auto resolve_member_vars = [&](aggregate& agg) {
        for (auto& child : agg.get_children()) {
            auto mv = std::dynamic_pointer_cast<member_variable_definition>(child);
            if (!mv) continue;
            auto var_type = mv->get_type();
            if (!var_type) continue;
            // Always try remap first — type objects may be shared between template
            // and clone (same shared_ptr), so even "resolved" types may point to
            // template-internal structs that need remapping to cloned counterparts.
            auto remapped = remap_nested_type(var_type);
            if (remapped != var_type) {
                mv->set_type(remapped);
                continue;
            }
            if (type::is_resolved(var_type)) continue;
            auto resolved = resolve_type_chain(var_type, &agg);
            if (resolved && (type::is_resolved(resolved) || std::dynamic_pointer_cast<struct_type>(resolved)
                             || (resolved->get_subtype() && std::dynamic_pointer_cast<struct_type>(resolved->get_subtype())))) {
                mv->set_type(resolved);
            }
        }
    };
    resolve_member_vars(*concrete_agg);
    for (auto& child : concrete_agg->get_children()) {
        if (auto nested = std::dynamic_pointer_cast<aggregate>(child)) {
            resolve_member_vars(*nested);
        }
    }

    // 8. Resolve the LLVM struct type immediately (member types are now
    //    concrete thanks to step 7 + the instantiator's type substitution)
    std::unordered_set<struct_type*> in_progress;
    // Resolve nested struct types first
    for (auto& child : concrete_agg->get_children()) {
        if (auto nested = std::dynamic_pointer_cast<aggregate>(child)) {
            if (auto nst = nested->get_struct_type()) {
                _context->resolve_struct_type(nst, in_progress);
            }
        }
    }
    _context->resolve_struct_type(st_type, in_progress);

    // 9. Fully resolve internal types of the newly instantiated aggregate.
    //    Templates instantiated during type_reference_resolver are added to
    //    namespaces that were already fully visited.  Their method bodies must
    //    be type-resolved before code generation can proceed.
    resolve_instantiated_aggregate(*concrete_agg);

    return st_type;
}

std::shared_ptr<type> type_reference_resolver::resolve_type_chain(
    const std::shared_ptr<type>& t,
    const element* scope_elem)
{
    if (!t || type::is_resolved(t)) return t;
    // A struct_type is semantically resolved even before its LLVM type is materialized.
    if (std::dynamic_pointer_cast<struct_type>(t)) return t;

    // Leaf: unresolved_type — delegate to resolve_inner_type which can
    // trigger template instantiation via try_instantiate_template_type.
    if (auto unres = std::dynamic_pointer_cast<unresolved_type>(t)) {
        return resolve_inner_type(t, scope_elem);
    }

    // Wrapper: peel one layer, resolve the subtype recursively, then
    // rebuild the wrapper around the resolved subtype.
    auto sub = t->get_subtype();
    if (!sub) return t;

    auto resolved_sub = resolve_type_chain(sub, scope_elem);
    if (!resolved_sub || (!type::is_resolved(resolved_sub) && !std::dynamic_pointer_cast<struct_type>(resolved_sub))) return t;

    if (std::dynamic_pointer_cast<pointer_type>(t))   return resolved_sub->get_pointer();
    if (std::dynamic_pointer_cast<reference_type>(t))  return resolved_sub->get_reference();
    if (std::dynamic_pointer_cast<owner_type>(t))      return resolved_sub->get_owner();
    if (std::dynamic_pointer_cast<link_type>(t))       return resolved_sub->get_link();
    if (std::dynamic_pointer_cast<view_type>(t))       return resolved_sub->get_view();
    if (std::dynamic_pointer_cast<drain_type>(t))      return resolved_sub->get_drain();
    if (std::dynamic_pointer_cast<const_type>(t))      return resolved_sub->get_const();
    if (auto sarr = std::dynamic_pointer_cast<sized_array_type>(t))
        return resolved_sub->get_array(sarr->get_size());
    if (std::dynamic_pointer_cast<array_type>(t))      return resolved_sub->get_array();

    return t;
}

void type_reference_resolver::resolve_instantiated_aggregate(aggregate& agg) {
    // Idempotency / recursion guard: each instantiated aggregate is resolved once.
    if (!_resolved_instantiations.insert(&agg).second) return;

    // Resolve instantiated base classes first, so this aggregate's own method
    // resolution and its base-constructor calls see fully-resolved base signatures
    // (return/param types, constructors). Without this, a transitively-instantiated
    // base that is never itself the direct instantiation target — e.g.
    // ConstIterator<int> as the base of a late Iterator<int> — keeps unresolved
    // signatures and its constructor never gets declared (error 0F031 at codegen).
    // Imported bases carry no cloned bodies and need no resolution here.
    for (auto& bs : agg.get_bases()) {
        if (!bs.base) continue;
        if (std::dynamic_pointer_cast<imported_aggregate>(bs.base)) continue;
        if (bs.base->is_instantiation()) {
            resolve_instantiated_aggregate(*bs.base);
        }
    }

    // Step 0: Resolve member variable types that contain unresolved template types.
    //         After template instantiation, member fields like `next : Node<T>*` may
    //         still carry unresolved inner types that need template instantiation.
    for (auto& [name, var] : agg.variables()) {
        if (var && var->get_type() && !type::is_resolved(var->get_type())) {
            auto resolved = resolve_type_chain(var->get_type(), &agg);
            if (resolved && (type::is_resolved(resolved) || std::dynamic_pointer_cast<struct_type>(resolved))) {
                var->set_type(resolved);
            }
        }
    }

    // Step 1: Resolve function parameter and return types that contain
    //         unresolved template types.  This is needed because the
    //         signature_resolver pre-pass only runs on namespaces, and the
    //         aggregate was created after that pass finished.
    auto resolve_fn_types = [&](function& fn) {
        // Resolve return type
        if (fn.get_return_type() && !type::is_resolved(fn.get_return_type())) {
            auto resolved = resolve_type_chain(fn.get_return_type(), &agg);
            if (resolved && (type::is_resolved(resolved) || std::dynamic_pointer_cast<struct_type>(resolved))) {
                fn.set_return_type(resolved);
            }
        }
        // Resolve parameter types
        for (auto& param : fn.parameters()) {
            if (param && param->get_type() && !type::is_resolved(param->get_type())) {
                auto resolved = resolve_type_chain(param->get_type(), &agg);
                if (resolved && (type::is_resolved(resolved) || std::dynamic_pointer_cast<struct_type>(resolved))) {
                    param->set_type(resolved);
                }
            }
        }
    };

    auto resolve_agg_fn_types = [&](aggregate& a) {
        for (auto& child : a.get_children()) {
            if (auto fn = std::dynamic_pointer_cast<function>(child)) {
                resolve_fn_types(*fn);
            }
        }
        for (auto& ctor : a.constructors()) {
            if (ctor) resolve_fn_types(*ctor);
        }
        if (auto dtor = a.get_destructor()) {
            resolve_fn_types(*dtor);
        }
    };

    resolve_agg_fn_types(agg);

    // Also resolve function types in nested aggregates
    for (auto& child : agg.get_children()) {
        if (auto nested = std::dynamic_pointer_cast<aggregate>(child)) {
            resolve_agg_fn_types(*nested);
        }
    }

    // Step 2: Visit the aggregate with type_reference_resolver to resolve
    //         all expressions and statements in method bodies.
    visit_aggregate(agg);
}

std::shared_ptr<type> type_reference_resolver::resolve_inner_type(
    const std::shared_ptr<type>& inner,
    const element* scope_elem)
{
    if (type::is_resolved(inner)) return inner;

    // Peel a const wrapper around an otherwise-unresolved inner type
    // (e.g. `const Vector<int>` inside `const Vector<int>&`): resolve the
    // wrapped type first -- which may trigger template instantiation -- then
    // re-apply const to the resolved result. Without this, callers that pass
    // a reference/pointer/etc.'s subtype straight into resolve_inner_type
    // (as gen_variable_definition.cpp's Step 4 does) would see a const_type
    // that is neither already-resolved nor a plain unresolved_type, and fall
    // through to the unconditional _context->resolve_type(inner) call below,
    // which cannot instantiate templates and thus fails to resolve at all.
    if (auto const_inner = std::dynamic_pointer_cast<const_type>(inner)) {
        auto resolved_sub = resolve_inner_type(const_inner->get_subtype(), scope_elem);
        if (resolved_sub && (type::is_resolved(resolved_sub) || std::dynamic_pointer_cast<struct_type>(resolved_sub))) {
            return resolved_sub->get_const();
        }
        return nullptr;
    }

    if (auto unres_inner = std::dynamic_pointer_cast<unresolved_type>(inner)) {

        // ── Template instantiation path ─────────────────────────────────
        // If the unresolved type carries AST template arguments (e.g. Box<int>),
        // look up the template definition, convert the AST args to model-level
        // template_argument values, instantiate, and return the concrete type.
        if (unres_inner->has_template_args() && scope_elem) {
            auto resolved = try_instantiate_template_type(unres_inner, *scope_elem);
            if (resolved && (type::is_resolved(resolved) || std::dynamic_pointer_cast<struct_type>(resolved))) return resolved;
            // If instantiation failed (e.g. not a template), fall through
            // to normal resolution for a better error message.
        }

        std::shared_ptr<type> resolved;
        if (scope_elem) {
            resolved = resolve_type_by_name(unres_inner->type_id(), *scope_elem);
        }
        if (!resolved || (!type::is_resolved(resolved) && !std::dynamic_pointer_cast<struct_type>(resolved))) {
            resolved = _context->from_string(unres_inner->type_id());
        }
        if (!resolved || (!type::is_resolved(resolved) && !std::dynamic_pointer_cast<struct_type>(resolved))) {
            auto imported_agg = _unit.get_or_create_imported_aggregate(unres_inner->type_id(), _context);
            if (imported_agg && imported_agg->get_struct_type()) resolved = imported_agg->get_struct_type();
        }
        if (!resolved || (!type::is_resolved(resolved) && !std::dynamic_pointer_cast<struct_type>(resolved))) {
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
 *   4. For callable_type: propagate return type from init symbol.
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

    // Step 4: nothing to infer for a callable type — the declared type is complete.
    // (A callable type is interned and shared by every declaration with the same
    //  signature, so it must never be mutated in place to adopt an initialiser's
    //  return type; the return type is part of the declaration syntax.)
    auto init_expr = std::dynamic_pointer_cast<constructor_invocation_expression>(init_expr_base);

    // Step 5: Phase 2: dispatch to per-type-category validation
    // Phase 2: validate init expression per type category
    // The initialiser is validated against the canonical (alias-free) type: the
    // declaration states the intended type immediately before the initialiser, so
    // no explicit cast is required there even for a strong alias.
    auto var_type = type::canonical(var.get_type());
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
    } else if (auto ct = std::dynamic_pointer_cast<callable_type>(var_type)) {
        if (ct->is_prototype()) {
            throw_error(static_cast<unsigned int>(k::diag::callable_diag::ERR_CALLABLE_PROTOTYPE_NOT_INSTANTIABLE),
                var_lexeme,
                "A bare callable prototype '{}' denotes a signature, not a value type; "
                "add an addresser ('*', '?', '+' or '&') to declare a variable",
                {var_type->to_string()});
        }
        if (!ct->is_nullable() && !ctx.has_single_init_arg()) {
            throw_error(static_cast<unsigned int>(k::diag::callable_model_diag::ERR_CALLABLE_NONNULL_UNINITIALIZED),
                var_lexeme,
                "A non-null callable of type '{}' must be explicitly initialised",
                {var_type->to_string()});
        }
    } else {
        // Unsupported construction for other types for now
        // TODO Support construction for other types (array, etc.)
    }
}

/**
 * Compare two array types allowing const-widening on elements, and/or a
 * sized→unsized widening on the source side. Returns true when:
 *   - src_nc == tgt_nc (identity), or
 *   - both are unsized array types whose element types match after
 *     stripping const (e.g. array<char> matches array<const<char>>), or
 *   - src is a *sized* array T[N] and tgt is an *unsized* array T[] with the
 *     same element type after stripping const (sized→unsized widening; both
 *     share the same { count, data[] } heap/opaque-pointer representation).
 * The caller is responsible for ensuring the conversion direction is safe:
 * this helper only ever widens (source non-const → target const, or source
 * sized → target unsized); it never narrows in the reverse direction.
 */
bool type_reference_resolver::types_match_array_const_compatible(
        const std::shared_ptr<type>& src_nc,
        const std::shared_ptr<type>& tgt_nc) {
    if (src_nc == tgt_nc) return true;
    auto src_arr = std::dynamic_pointer_cast<array_type>(src_nc);
    auto tgt_arr = std::dynamic_pointer_cast<array_type>(tgt_nc);
    if (src_arr && tgt_arr && !tgt_arr->is_sized()) {
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

    // Aliases (soft and strong) are nominal only: conversion weight is always
    // computed on the real underlying types. The nominal distinction of a
    // strong alias is enforced separately by check_strong_alias_conversion().
    {
        auto canon_tgt = type::canonical(tgt);
        auto canon_src = type::canonical(expr->get_type());
        if (canon_tgt != tgt || canon_src != expr->get_type()) {
            auto stripped = cast_expression::make_shared(expr, canon_src);
            stripped->set_type(canon_src);
            return compute_cast_weight(stripped, canon_tgt);
        }
    }

    auto type_src = expr->get_type();

    // Step 0: an unprefixed string literal can adopt an `unsigned byte` /
    // `unsigned short` element type from context (committed in adapt_type by
    // cloning). Treat such a target as a viable conversion here so overload
    // resolution does not reject it.
    if (auto ve = std::dynamic_pointer_cast<value_expression>(expr)) {
        if (ve->is_literal() && std::holds_alternative<lex::string>(ve->any_literal())
            && ve->any_literal().get<lex::string>().enc == lex::literal_encoding::unspecified) {
            auto t = type::remove_const(tgt);
            if (auto ref = std::dynamic_pointer_cast<reference_type>(t)) {
                t = type::remove_const(ref->get_subtype());
            }
            if (auto arr = std::dynamic_pointer_cast<array_type>(t)) {
                auto elem = type::remove_const(arr->get_subtype());
                if (auto prim = std::dynamic_pointer_cast<primitive_type>(elem)) {
                    if (prim->get_type() == primitive_type::UNSIGNED_BYTE
                        || prim->get_type() == primitive_type::UNSIGNED_SHORT) {
                        // Worse than the native char[] match so an unprefixed
                        // literal still prefers char[] when several array
                        // overloads are viable; only chosen when char[] is not.
                        return CAST_NARROWING;
                    }
                }
            }
        }
    }

    // Strip const from both sides: const T and T are interchangeable for value conversions.
    // Const-checking for assignment targets is done separately in visit_assignation_expression.
    auto tgt_nc = type::remove_const(tgt);

    // Step 1: Function reference type cases
    // ── Function reference type cases ─────────────────────────────────────────
    // frt → frt (any addresser combination): free conversion (same LLVM type).
    // ref<frt> → frt: allowed (load from variable / direct function address).
    if (auto tgt_frt = std::dynamic_pointer_cast<callable_type>(tgt_nc)) {
        if (std::dynamic_pointer_cast<callable_type>(type_src)) {
            return CAST_NONE;
        }
        if (type::is_reference(type_src)) {
            auto src_inner = type::remove_const(std::dynamic_pointer_cast<reference_type>(type_src)->get_subtype());
            if (std::dynamic_pointer_cast<callable_type>(src_inner)) {
                return CAST_REF_CONV; // ref<frt> → frt: load needed
            }
        }
        return CAST_IMPOSSIBLE;
    }
    // ref<frt> → ref<frt>: pass through.
    if (type::is_reference(tgt_nc)) {
        auto tgt_sub_nc = type::remove_const(std::dynamic_pointer_cast<reference_type>(tgt_nc)->get_subtype());
        if (std::dynamic_pointer_cast<callable_type>(tgt_sub_nc)) {
            if (type_src == tgt_nc || type_src == tgt) return CAST_NONE;
            if (type::is_reference(type_src)) {
                auto src_sub = type::remove_const(std::dynamic_pointer_cast<reference_type>(type_src)->get_subtype());
                if (std::dynamic_pointer_cast<callable_type>(src_sub)) return CAST_NONE;
            }
            return CAST_IMPOSSIBLE;
        }
    }
    // ── End function reference type cases ─────────────────────────────────────

    // --- Enumeration identity -------------------------------------------------
    // Two unrelated enumerations are never implicitly convertible into each other,
    // even though they share the same underlying integer representation. Without this
    // rule, overload resolution scores f(EnumA) and f(EnumB) identically for an EnumB
    // argument and silently selects the first candidate. Derivation (enum B : A) stays
    // allowed in the derived → base direction.
    {
        const auto peel_enum = [](std::shared_ptr<type> t) -> std::shared_ptr<enum_type> {
            t = type::remove_const(t);
            if (type::is_reference(t)) t = type::remove_const(t->get_subtype());
            return std::dynamic_pointer_cast<enum_type>(t);
        };
        auto src_enum = peel_enum(type_src);
        auto tgt_enum = peel_enum(tgt_nc);
        if (src_enum && tgt_enum && src_enum != tgt_enum) {
            auto src_en = src_enum->get_enumeration();
            auto tgt_en = tgt_enum->get_enumeration();
            const auto derives_from = [](std::shared_ptr<enumeration> derived,
                                         const std::shared_ptr<enumeration>& base) {
                for (auto e = std::move(derived); e; e = e->get_base()) {
                    if (e == base) return true;
                }
                return false;
            };
            const bool related = src_en && tgt_en
                && (src_en == tgt_en || derives_from(src_en, tgt_en));
            if (!related) {
                return CAST_IMPOSSIBLE;
            }
        }
    }

    // --- Null literal → any nullable indirection: always valid ─────────────
    if (type::is_null(type_src)) {
        if (type::is_pointer(tgt_nc) || type::is_view(tgt_nc) ||
            type::is_link(tgt_nc) || type::is_owner(tgt_nc)) {
            return CAST_WIDENING;
        }
    }

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
            // Generic erasure: ptr<Concrete> ↔ ptr<byte*> or ptr<byte*> ↔ ptr<Concrete>
            {
                auto is_generic_opaque_sub = [](const std::shared_ptr<k::model::type>& sub) -> bool {
                    auto ptr = std::dynamic_pointer_cast<pointer_type>(type::remove_const(sub));
                    if (!ptr) return false;
                    auto inner = type::remove_const(ptr->get_subtype());
                    auto prim = std::dynamic_pointer_cast<primitive_type>(inner);
                    return prim && prim->get_type() == primitive_type::BYTE;
                };
                if ((src_st_type && is_generic_opaque_sub(tgt_sub_nc)) ||
                    (tgt_st_type && is_generic_opaque_sub(src_sub_nc)) ||
                    (is_generic_opaque_sub(src_sub_nc) && is_generic_opaque_sub(tgt_sub_nc))) {
                    return CAST_WIDENING;
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
            // Generic erasure: owner<Concrete> ↔ owner<byte*>
            {
                auto is_generic_opaque_sub = [](const std::shared_ptr<k::model::type>& sub) -> bool {
                    auto ptr = std::dynamic_pointer_cast<pointer_type>(type::remove_const(sub));
                    if (!ptr) return false;
                    auto inner = type::remove_const(ptr->get_subtype());
                    auto prim = std::dynamic_pointer_cast<primitive_type>(inner);
                    return prim && prim->get_type() == primitive_type::BYTE;
                };
                if ((std::dynamic_pointer_cast<struct_type>(src_sub_nc) && is_generic_opaque_sub(tgt_sub_nc)) ||
                    (is_generic_opaque_sub(src_sub_nc) && std::dynamic_pointer_cast<struct_type>(tgt_sub_nc))) {
                    return CAST_WIDENING;
                }
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
                // Generic erasure: ref<owner<Concrete>> → owner<byte*>
                {
                    auto is_generic_opaque_sub = [](const std::shared_ptr<k::model::type>& sub) -> bool {
                        auto ptr = std::dynamic_pointer_cast<pointer_type>(type::remove_const(sub));
                        if (!ptr) return false;
                        auto inner = type::remove_const(ptr->get_subtype());
                        auto prim = std::dynamic_pointer_cast<primitive_type>(inner);
                        return prim && prim->get_type() == primitive_type::BYTE;
                    };
                    if (std::dynamic_pointer_cast<struct_type>(own_sub_nc) && is_generic_opaque_sub(tgt_sub_nc))
                        return CAST_WIDENING;
                }
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
            // Also check struct upcast: ref<Derived> → view<Base>
            auto src_st = std::dynamic_pointer_cast<struct_type>(sub);
            auto tgt_st = std::dynamic_pointer_cast<struct_type>(tgt_sub_nc);
            if (src_st && tgt_st && src_st->get_struct() && tgt_st->get_struct() &&
                src_st->get_struct()->is_derived_from(tgt_st->get_struct())) {
                return CAST_REF_CONV;
            }
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
        } else {
            // Union value construction: allow U(x) when x is convertible to one
            // union alternative type.
            if (auto union_def = find_union_by_struct_type(_unit.get_root_namespace(), st_tgt)) {
                for (const auto* alt : union_def->all_alternatives_ptrs()) {
                    if (!alt || !alt->resolved_type) continue;
                    auto w = compute_cast_weight(expr, alt->resolved_type);
                    if (w != CAST_IMPOSSIBLE) {
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

    // ── Generic erasure: addresser<ConcreteClass> ↔ addresser<byte*> ────────
    // When a generic is synthesized, type parameter T is mapped to byte*
    // (opaque pointer). At the call site, we have ConcreteType wrapped in
    // the same addresser kind. Since all pointers are `ptr` in LLVM IR
    // (opaque pointers), this is a no-op bitcast.
    {
        auto is_generic_opaque = [](const std::shared_ptr<k::model::type>& sub) -> bool {
            auto ptr = std::dynamic_pointer_cast<pointer_type>(type::remove_const(sub));
            if (!ptr) return false;
            auto inner = type::remove_const(ptr->get_subtype());
            auto prim = std::dynamic_pointer_cast<primitive_type>(inner);
            return prim && prim->get_type() == primitive_type::BYTE;
        };

        auto check_generic_erasure = [&](const std::shared_ptr<k::model::type>& src,
                                          const std::shared_ptr<k::model::type>& tgt_t) -> bool {
            auto src_sub = type::remove_const(src->get_subtype());
            auto tgt_sub = type::remove_const(tgt_t->get_subtype());
            // ConcreteClass → byte* (calling generic method)
            if (std::dynamic_pointer_cast<struct_type>(src_sub) && is_generic_opaque(tgt_sub))
                return true;
            // byte* → ConcreteClass (return from generic method)
            if (is_generic_opaque(src_sub) && std::dynamic_pointer_cast<struct_type>(tgt_sub))
                return true;
            return false;
        };

        // Same addresser kind on both sides
        if ((type::is_owner(type_src) && type::is_owner(tgt_nc)) ||
            (type::is_pointer(type_src) && type::is_pointer(tgt_nc)) ||
            (type::is_link(type_src) && type::is_link(tgt_nc)) ||
            (type::is_view(type_src) && type::is_view(tgt_nc))) {
            if (check_generic_erasure(type_src, tgt_nc)) {
                return CAST_WIDENING;
            }
        }
        // ref<owner<Concrete>> → owner<byte*> (owner move into generic method)
        if (type::is_reference(type_src) && type::is_owner(tgt_nc)) {
            auto ref_src = std::dynamic_pointer_cast<reference_type>(type_src);
            auto inner_nc = type::remove_const(ref_src->get_subtype());
            if (type::is_owner(inner_nc)) {
                auto own_sub_nc = type::remove_const(inner_nc->get_subtype());
                auto tgt_sub_nc = type::remove_const(tgt_nc->get_subtype());
                if (std::dynamic_pointer_cast<struct_type>(own_sub_nc) && is_generic_opaque(tgt_sub_nc))
                    return CAST_WIDENING;
            }
        }
        // pointer<byte*> → pointer<Concrete> (return value from generic)
        if (type::is_pointer(type_src) && type::is_pointer(tgt_nc)) {
            // Already handled above
        }
    }

    // Step 6: value T → ref<T>: rvalue-to-reference binding (materialize a temporary).
    // Enables passing primitive or struct values to reference parameters (like C++ const T&).
    if (!type::is_reference(effective_src) && type::is_reference(tgt_nc)) {
        auto tgt_ref = std::dynamic_pointer_cast<reference_type>(tgt_nc);
        auto tgt_sub_nc = type::remove_const(tgt_ref->get_subtype());
        auto eff_src_nc = type::remove_const(effective_src);
        // primitive value → ref<primitive>
        if (auto p_src = std::dynamic_pointer_cast<primitive_type>(eff_src_nc)) {
            if (auto p_tgt = std::dynamic_pointer_cast<primitive_type>(tgt_sub_nc)) {
                if (*p_src == *p_tgt) return CAST_REF_CONV;
                // Widening: same signedness/float category, target is wider
                if (p_src->is_integer() && p_tgt->is_integer() &&
                    p_src->is_unsigned() == p_tgt->is_unsigned() &&
                    p_tgt->type_size() >= p_src->type_size()) return CAST_WIDENING;
            }
        }
        // struct value → ref<same struct>: copy-materialize
        if (auto st_src = std::dynamic_pointer_cast<struct_type>(eff_src_nc)) {
            if (auto st_tgt = std::dynamic_pointer_cast<struct_type>(tgt_sub_nc)) {
                if (st_src == st_tgt) return CAST_REF_CONV;
            }
        }
    }

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
                // Adapt arguments to match declared parameter types (load from reference, etc.)
                if (i < new_params.size() && new_params[i]->get_type()) {
                    auto a = adapt_type(args[i], new_params[i]->get_type());
                    adapted_args.push_back(a ? a : args[i]);
                } else {
                    adapted_args.push_back(args[i]);
                }
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

        // Plain free/static call form. Skipped for a pure member-of-object call
        // (this_expr set with no direct_args): there, a free function must consume
        // the receiver object via the unified-call path below — it cannot match as
        // a plain call that ignores the object (which would let an unrelated free
        // function of the same name spuriously compete, e.g. against an imported
        // template struct's member of identical name/arity).
        if ((!func->is_member() || func->is_static()) && (!this_expr || direct_args)) {
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
namespace {

/** Innermost type under any indirection / const / array wrapper. */
std::shared_ptr<type> innermost_type(std::shared_ptr<type> t) {
    while (t) {
        if (std::dynamic_pointer_cast<alias_type>(t)) return t;
        auto sub = t->get_subtype();
        if (!sub) return t;
        t = sub;
    }
    return t;
}

/** The strong alias denoted by a type, if any (looking through indirections). */
std::shared_ptr<alias_definition> denoted_strong_alias(const std::shared_ptr<type>& t) {
    auto at = std::dynamic_pointer_cast<alias_type>(innermost_type(t));
    if (!at) return {};
    auto al = at->get_alias();
    return (al && al->is_strong()) ? al : nullptr;
}

/**
 * True when the expression derives from an operand of the given strong alias
 * through type-preserving operations. Explicit casts and constructions break
 * the chain, since they already state the intended type.
 */
bool carries_alias(const std::shared_ptr<expression>& e, const alias_definition* alias, unsigned int depth = 0) {
    if (!e || depth > 32) return false;
    if (auto al = denoted_strong_alias(e->get_type())) {
        if (al.get() == alias) return true;
    }
    if (e->get_alias_taint().get() == alias) return true;
    if (std::dynamic_pointer_cast<cast_expression>(e)) return false;
    if (auto bin = std::dynamic_pointer_cast<binary_expression>(e)) {
        return carries_alias(bin->left(), alias, depth + 1)
            || carries_alias(bin->right(), alias, depth + 1);
    }
    if (auto un = std::dynamic_pointer_cast<unary_expression>(e)) {
        return carries_alias(un->sub_expr(), alias, depth + 1);
    }
    return false;
}

/** True when the expression is a compile-time literal (possibly negated). */
bool is_literal_expression(const std::shared_ptr<expression>& e, unsigned int depth = 0) {
    if (!e || depth > 8) return false;
    if (auto ve = std::dynamic_pointer_cast<value_expression>(e)) {
        return ve->is_literal();
    }
    if (auto un = std::dynamic_pointer_cast<arithmetic_unary_expression>(e)) {
        return is_literal_expression(un->sub_expr(), depth + 1);
    }
    return false;
}

} // anonymous namespace

void type_reference_resolver::check_strong_alias_conversion(
        const std::shared_ptr<expression>& expr,
        const std::shared_ptr<type>& target,
        alias_conv_site site,
        const lex::opt_any_lexeme& lexeme)
{
    if (!expr || !target) return;

    auto alias = denoted_strong_alias(target);
    if (!alias) return;

    // Same alias on both sides: nothing to convert.
    auto src_alias = denoted_strong_alias(expr->get_type());
    if (src_alias == alias) return;

    // The expression is a compile-time constant, or it is tainted by the alias
    // somewhere in its operand tree: no explicit cast is required.
    if (is_literal_expression(expr)) return;
    if (carries_alias(expr, alias.get())) return;

    const std::string alias_name = alias->get_short_name();
    const std::string src_name = expr->get_type() ? expr->get_type()->to_string() : "?";

    if (site == alias_conv_site::ASSIGNMENT) {
        throw_error(static_cast<unsigned int>(k::diag::alias_diag::ERR_TYPEDEF_REQUIRES_EXPLICIT_CAST), lexeme,
            "Cannot implicitly convert '{}' to the typedef '{}'; "
            "a typedef is a distinct type: write an explicit cast '({}) expr'",
            {src_name, alias_name, alias_name});
    }

    warn(static_cast<unsigned int>(k::diag::alias_diag::WARN_TYPEDEF_BASE_TYPE_ARGUMENT), lexeme,
        "Passing '{}' where the typedef '{}' is expected; "
        "the symbol is mangled with the underlying type so this cannot be enforced at link time. "
        "Either cast explicitly to '{}', or declare the {} with the underlying type '{}'",
        {src_name, alias_name, alias_name,
         site == alias_conv_site::RETURN ? "return type" : "parameter",
         type::canonical(target) ? type::canonical(target)->to_string() : "?"});
}

void type_reference_resolver::materialize_aliases(const alias_holder& holder, element& scope) {
    for (const auto& al : holder.get_aliases()) {
        if (!al || al->is_resolved()) continue;
        // A parameterised alias has no type of its own: its target still holds
        // the template parameter placeholders and is only resolved at a use
        // site, once the arguments are known.
        if (al->is_template()) continue;
        bool cycle = false;
        scope_lookup::materialize_alias_type(
            al, _context,
            [this](const k::name& n, const element& e) { return resolve_type_by_name(n, e); },
            cycle);
        if (cycle) {
            throw_error(static_cast<unsigned int>(k::diag::alias_diag::ERR_ALIAS_CYCLE),
                        al->get_decl_lexeme(),
                        "Alias '{}' is defined in terms of itself", {al->get_fq_name()});
        }
    }
}

void type_reference_resolver::check_typedef_arguments(
        function& fn,
        const std::vector<std::shared_ptr<expression>>& args,
        const lex::opt_any_lexeme& lexeme)
{
    const auto& params = fn.parameters();
    for (size_t n = 0; n < args.size() && n < params.size(); ++n) {
        if (!params[n]) continue;
        check_strong_alias_conversion(args[n], params[n]->get_type(),
                                      alias_conv_site::ARGUMENT, lexeme);
    }
}

std::shared_ptr<expression> type_reference_resolver::adapt_type(std::shared_ptr<expression> expr, const std::shared_ptr<type>& type) {    // Accept types that are either fully resolved (LLVM type present) or
    // semantically complete (no unresolved_type nodes remain).  struct_type
    // objects created during template instantiation may not have an LLVM type
    // yet — that is materialized later by context::resolve_types().
    auto type_is_usable = [](const std::shared_ptr<k::model::type>& t) {
        if (!t) return false;
        if (k::model::type::is_resolved(t)) return true;
        return !k::model::type::contains_unresolved(t);
    };
    if(!expr || !type_is_usable(type) || !type_is_usable(expr->get_type())) {
        // Arguments must not be null, expr must have a type and types (expr and target) must be resolved.
        return nullptr;
    }

    // ── Context-driven element type for an unprefixed string literal ─────────────
    // If the literal has no encoding prefix and the target is an array of
    // `unsigned byte` or `unsigned short`, adapt a *clone* re-typed to the
    // matching encoding (UTF-8 / UTF-16). The original literal is left untouched
    // so that overload resolution can score it against several candidates.
    // A `char` target keeps the default (UTF-32) path.
    if (auto ve = std::dynamic_pointer_cast<value_expression>(expr)) {
        if (ve->is_literal() && std::holds_alternative<lex::string>(ve->any_literal())
            && ve->any_literal().get<lex::string>().enc == lex::literal_encoding::unspecified) {
            auto tgt = type::remove_const(type);
            if (auto ref = std::dynamic_pointer_cast<reference_type>(tgt)) {
                tgt = type::remove_const(ref->get_subtype());
            }
            if (auto arr = std::dynamic_pointer_cast<array_type>(tgt)) {
                auto elem = type::remove_const(arr->get_subtype());
                if (auto prim = std::dynamic_pointer_cast<primitive_type>(elem)) {
                    std::optional<lex::literal_encoding> new_enc;
                    if (prim->get_type() == primitive_type::UNSIGNED_BYTE) {
                        new_enc = lex::literal_encoding::utf8;
                    } else if (prim->get_type() == primitive_type::UNSIGNED_SHORT) {
                        new_enc = lex::literal_encoding::utf16;
                    }
                    if (new_enc) {
                        auto cloned = std::dynamic_pointer_cast<value_expression>(ve->clone());
                        cloned->set_literal_encoding(*new_enc);
                        cloned->set_type(_context->from_literal(cloned->any_literal()));
                        expr = cloned;
                    }
                }
            }
        }
    }

    auto type_src = expr->get_type();
    // For value-level adaptation, strip const from both sides.
    auto type_nc = type::remove_const(type);

    // ── Alias / typedef transparency ────────────────────────────────────────────
    // An alias layer is a pure renaming: it never changes the representation.
    // Adaptation is therefore always performed on the canonical (alias-free)
    // types, and the result is re-tagged with the requested type. The cast nodes
    // introduced here are bitwise identity at IR level — they only carry the type
    // annotation, exactly like the const-widening casts below.
    //
    // Whether an implicit base → typedef conversion is *allowed* is decided by
    // check_strong_alias_conversion() at the few syntactic positions where it
    // matters; adaptation itself stays purely mechanical.
    {
        auto canon_src = type::canonical(type_src);
        auto canon_tgt = type::canonical(type_nc);
        if (canon_src != type_src || canon_tgt != type_nc) {
            auto src_expr = expr;
            if (canon_src != type_src) {
                auto strip = cast_expression::make_shared(expr, canon_src);
                strip->set_type(canon_src);
                // Stripping the alias layer is not an explicit cast: the alias
                // identity is kept as a taint so the expression can still flow
                // back into a destination of the same alias type.
                if (auto al = std::dynamic_pointer_cast<alias_type>(innermost_type(type_src))) {
                    strip->set_alias_taint(al->get_alias());
                } else {
                    strip->set_alias_taint(expr->get_alias_taint());
                }
                src_expr = strip;
            }
            auto adapted = adapt_type(src_expr, canon_tgt);
            if (!adapted) return {};
            if (canon_tgt != type_nc) {
                // The cast node itself must stay alias-free: code generation only
                // ever sees the canonical type, while the expression carries the
                // aliased type for the remaining K-level checks.
                auto retag = cast_expression::make_shared(adapted, canon_tgt);
                retag->set_type(type_nc);
                return retag;
            }
            return adapted;
        }
    }

    // ── Null literal → any nullable indirection type ────────────────────────────
    // The `null` literal has no intrinsic pointed-to type: it is valid wherever a
    // nullable indirection (pointer, link, view or owner) is expected, regardless
    // of context (variable init, array literal element, argument, ...).
    if (type::is_null(type_src)) {
        if (type::is_pointer(type_nc) || type::is_link(type_nc) ||
            type::is_view(type_nc) || type::is_owner(type_nc)) {
            auto cast = cast_expression::make_shared(expr, type_nc);
            cast->set_type(type_nc);
            return cast;
        }
    }

    // ── Function reference types ────────────────────────────────────────────────
    if (std::dynamic_pointer_cast<callable_type>(type_nc) ||
        std::dynamic_pointer_cast<callable_type>(type_src)) {
        auto result = adapt_callable_type(expr, type_src, type_nc);
        if (result) return result;
        if (std::dynamic_pointer_cast<callable_type>(type_nc)) return {};
    }
    if (type::is_reference(type_nc)) {
        auto tgt_sub_nc = type::remove_const(std::dynamic_pointer_cast<reference_type>(type_nc)->get_subtype());
        if (std::dynamic_pointer_cast<callable_type>(tgt_sub_nc)) {
            auto result = adapt_callable_type(expr, type_src, type_nc);
            if (result) return result;
        }
    }
    if (type::is_reference(type_src)) {
        auto src_inner = type::remove_const(std::dynamic_pointer_cast<reference_type>(type_src)->get_subtype());
        if (std::dynamic_pointer_cast<callable_type>(src_inner)) {
            auto result = adapt_callable_type(expr, type_src, type_nc);
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

    // ── value T → ref<T>: rvalue-to-reference binding (materialize a stack temporary) ──
    // Allows passing a primitive/struct value to a reference parameter.
    // Equivalent to C++ const T& binding to an rvalue.
    if (type::is_reference(type_nc) && !type::is_reference(type_src)) {
        auto tgt_ref = std::dynamic_pointer_cast<reference_type>(type_nc);
        auto tgt_sub_nc = type::remove_const(tgt_ref->get_subtype());
        auto src_nc = type::remove_const(type_src);
        // Primitive → ref<primitive>
        if (auto p_src = std::dynamic_pointer_cast<primitive_type>(src_nc)) {
            if (auto p_tgt = std::dynamic_pointer_cast<primitive_type>(tgt_sub_nc)) {
                std::shared_ptr<expression> converted = expr;
                // Widen the primitive if necessary (e.g. int → ref<long>)
                if (*p_src != *p_tgt) {
                    auto cast_expr = cast_expression::make_shared(expr, p_tgt);
                    cast_expr->set_type(p_tgt);
                    converted = cast_expr;
                }
                auto temp = temporary_construction_expression::make_shared(p_tgt, {converted});
                temp->set_type(p_tgt->get_reference());
                return temp;
            }
        }
        // Struct value → ref<same struct>: materialize a copy
        if (auto st_src = std::dynamic_pointer_cast<struct_type>(src_nc)) {
            if (auto st_tgt = std::dynamic_pointer_cast<struct_type>(tgt_sub_nc)) {
                if (st_src == st_tgt) {
                    auto temp = temporary_construction_expression::make_shared(st_tgt, {expr});
                    temp->set_type(st_tgt->get_reference());
                    return temp;
                }
            }
        }
    }

    // ── Primitive / struct fallback ─────────────────────────────────────────────
    return adapt_primitive_or_struct_type(expr, type_nc);
}




} // namespace k::model::gen
