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
#include "resolvers_scope_lookup.hpp"
#include "resolvers_aggregate.hpp"
#include "../model/imported.hpp"
#include "../errors.hpp"

namespace k::model::gen {

// ─────────────────────────────────────────────────────────────────────────────
// scope_lookup — visibility helpers
// ─────────────────────────────────────────────────────────────────────────────

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
            if (fn->is_member()) {
                auto check_st = fn->get_owner();
                if (fn->is_static()) {
                    // Static members only grant access to their own declaring class.
                    if (check_st && check_st.get() == &st) return true;
                } else {
                    while (check_st) {
                        if (check_st.get() == &st) return true;
                        check_st = check_st->get_enclosing_aggregate();
                    }
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
        if (!fn->is_member()) continue;

        auto check_st = fn->get_owner();
        if (fn->is_static()) {
            // Static member functions can only access members of exactly their
            // own class (no implicit upcast via 'this', so no protected-through-
            // derived-class rule applies).
            if (check_st) {
                if (vis == PRIVATE) {
                    if (check_st.get() == &owner_st) return true;
                } else { // PROTECTED
                    if (check_st.get() == &owner_st) return true;
                    if (owner_st_shared && check_st->is_derived_from(owner_st_shared)) return true;
                }
            }
        } else {
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
    }
    return false;
}

// Returns true if 'instantiation_args' (the concrete template args of the
// calling function or owning aggregate's instantiation) match the expected args
// recorded in 'dir'.
//
// Two sources of truth for the expected args (preferred in order):
//  1. dir.resolved_tpl_arg_types[i] — concrete type pointer, available when the
//     declaring aggregate was itself a template instantiation and the instantiator
//     substituted T→int etc.
//  2. dir.raw_template_arg_names[i]  — raw string ("int", "MyStruct") — used as
//     fallback when the declaring aggregate is NOT a template (instantiator never
//     ran), so resolved_tpl_arg_types was never populated.
//
// Comparison: prefer pointer identity, then to_string() equality.
static bool template_args_match(
    const std::vector<template_argument>& instantiation_args,
    const friend_directive& dir)
{
    const auto& dir_types = dir.resolved_tpl_arg_types;
    const auto& dir_raw   = dir.raw_template_arg_names;

    // Expected argument count from whichever source has data.
    size_t expected = std::max(dir_types.size(), dir_raw.size());
    if (instantiation_args.size() != expected) return false;

    for (size_t i = 0; i < instantiation_args.size(); ++i) {
        if (!instantiation_args[i].is_type()) return false;
        const auto& actual = instantiation_args[i].type_arg;

        if (i < dir_types.size() && dir_types[i]) {
            // Resolved type available — compare by pointer then by display name.
            if (actual.get() != dir_types[i].get() &&
                actual->to_string() != dir_types[i]->to_string()) {
                return false;
            }
        } else if (i < dir_raw.size() && !dir_raw[i].empty()) {
            // Fall back to raw name string comparison (non-template declaring struct).
            if (actual->to_string() != dir_raw[i]) return false;
        } else {
            return false;
        }
    }
    return true;
}

bool scope_lookup::is_friend_of(
    const aggregate& owner_agg,
    const std::vector<std::shared_ptr<function>>& function_stack,
    const unit& unit)
{
    if (function_stack.empty()) return false;

    const auto& directives = owner_agg.get_friend_directives();
    if (directives.empty()) return false;

    const auto& current_fn = function_stack.back();

    for (const auto& dir : directives) {
        auto root = unit.get_root_namespace();
        if (!root) continue;

        std::shared_ptr<const element> current = root;
        bool resolved = true;
        for (size_t i = 0; i < dir.target_name.size(); ++i) {
            const auto& part = dir.target_name[i];
            bool stepped = false;

            if (auto nspc = std::dynamic_pointer_cast<const ns>(current)) {
                if (auto child = nspc->get_child_namespace(part)) {
                    current = child;
                    stepped = true;
                }
            }
            if (!stepped) {
                if (auto ah = std::dynamic_pointer_cast<const aggregate_holder>(current)) {
                    if (auto agg = ah->get_aggregate(part)) {
                        current = std::dynamic_pointer_cast<const element>(agg);
                        stepped = true;
                    }
                }
            }
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

            auto fn_owner = current_fn->get_owner();
            if (fn_owner && fn_owner.get() == target_agg.get()) {
                return true;
            }
            // Template instantiation match: fn_owner may be a concrete instantiation
            // (e.g. OptionalConstRef__int) while target_agg is the template definition
            // (OptionalConstRef). Match when base template names agree and, if explicit
            // template args were written, those args also match.
            if (fn_owner && fn_owner->has_tpl_args() && target_agg->is_template()) {
                if (fn_owner->get_tpl_base_name() == target_agg->get_short_name()) {
                    if (!dir.has_explicit_template_args) {
                        // Unparameterized friend — all instantiations are friends.
                        return true;
                    }
                    if (template_args_match(fn_owner->get_tpl_args(), dir)) return true;
                }
            }
        } else if (auto target_fn = std::dynamic_pointer_cast<const function>(target)) {
            if (dir.filter != friend_directive::filter_t::NONE) {
                continue;
            }
            // Exact match (non-template or same concrete instance).
            if (current_fn.get() == target_fn.get()) {
                return true;
            }
            // Template instantiation match: current_fn may be a concrete instantiation
            // (e.g. peek__int) while target_fn is the template definition (peek).
            if (current_fn->has_tpl_args() && target_fn->is_template()) {
                if (current_fn->get_tpl_base_name() == target_fn->get_short_name()) {
                    if (!dir.has_explicit_template_args) {
                        // Unparameterized friend — all instantiations are friends.
                        return true;
                    }
                    if (template_args_match(current_fn->get_tpl_args(), dir)) return true;
                }
            }
        }
    }

    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// scope_lookup — scope-chain lookup methods
// ─────────────────────────────────────────────────────────────────────────────

std::shared_ptr<variable_definition>
scope_lookup::lookup_variable(std::shared_ptr<element> elem, const std::string& name) {
    for (auto current = elem; current; current = current->parent<element>()) {
        if (auto vh = std::dynamic_pointer_cast<variable_holder>(current)) {
            if (auto var = vh->get_variable(name)) {
                return var;
            }
        }
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
    std::unordered_set<const function*> seen_functions;
    std::unordered_set<const aggregate*> seen_aggregates;

    // True if two candidate functions share the same parameter-type signature
    // (same short name already guaranteed by construction here, since every
    // candidate comes from a get_functions(name) lookup). Used below to detect
    // when a base-class overload is truly shadowed/overridden by an
    // already-collected (more-derived) function, as opposed to being a
    // distinct overload with a different arity/parameter list (e.g. a getter
    // declared in a base interface and a setter of the same name declared in
    // a derived interface).
    auto same_param_signature = [](const function& a, const function& b) {
        if (a.is_const_member() != b.is_const_member()) return false;
        if (a.get_parameter_size() != b.get_parameter_size()) return false;
        for (size_t i = 0; i < a.get_parameter_size(); ++i) {
            auto ta = std::const_pointer_cast<type>(a.get_parameter(i)->get_type());
            auto tb = std::const_pointer_cast<type>(b.get_parameter(i)->get_type());
            bool eq = type::are_equal(ta, tb);
            if (!eq) eq = ta && tb && ta->to_string() == tb->to_string();
            if (!eq) return false;
        }
        return true;
    };

    // Plain identity-based append: used for candidates collected at the same
    // scope level (e.g. several free-function overloads in a namespace, or
    // successive enclosing scopes). Duplicate signatures here are genuine
    // ambiguities and must both be kept so overload resolution can report them.
    auto append_unique_simple = [&](const std::shared_ptr<function>& fn) {
        if (!fn) return;
        if (!seen_functions.insert(fn.get()).second) return;
        result.push_back(fn);
    };

    // Append a candidate coming from an aggregate's own member list, skipping
    // it only if shadowed by an already-collected, more-derived function with
    // the identical parameter signature (a true override/shadow) — the
    // more-derived version already in `result` must win. A same-named but
    // differently-shaped overload from a base is kept. This shadow check must
    // NOT apply across unrelated same-level candidates (e.g. free-function
    // overloads), only across the derived-to-base aggregate hierarchy, since
    // `collect_aggregate_functions` recurses most-derived first.
    auto append_unique_member = [&](const std::shared_ptr<function>& fn) {
        if (!fn) return;
        if (!seen_functions.insert(fn.get()).second) return;
        for (auto& existing : result) {
            if (same_param_signature(*existing, *fn)) return;
        }
        result.push_back(fn);
    };

    std::function<void(const std::shared_ptr<aggregate>&)> collect_aggregate_functions;
    collect_aggregate_functions = [&](const std::shared_ptr<aggregate>& agg) {
        if (!agg) return;
        if (!seen_aggregates.insert(agg.get()).second) return;

        auto local = agg->get_functions(name);
        for (auto& fn : local) {
            append_unique_member(fn);
        }
        // Always continue into bases: append_unique_member() already skips any
        // base overload whose parameter signature is identical to an
        // already-collected (more-derived) one, so a true override still hides
        // its base slot, while a distinct overload (different arity/params)
        // declared in a base — e.g. Entry<K,V>::value() (getter) vs
        // MutableEntry<K,V>::value(v) (setter) — remains part of the candidate
        // set instead of being unconditionally hidden just because the derived
        // type also declares a same-named member.
        for (const auto& bs : agg->get_bases()) {
            if (bs.base) collect_aggregate_functions(bs.base);
        }
    };

    for (auto current = elem; current; current = current->parent<element>()) {
        if (auto agg = std::dynamic_pointer_cast<aggregate>(current)) {
            // Member call lookup includes inherited member functions so unqualified
            // calls inside member/default-method bodies can resolve base methods.
            collect_aggregate_functions(agg);
            continue;
        }
        if (auto fh = std::dynamic_pointer_cast<function_holder>(current)) {
            for (auto& fn : fh->get_functions(name)) {
                append_unique_simple(fn);
            }
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

namespace {
/// Split a "::"-separated qualified name into its parts (no root-prefix handling).
std::vector<std::string> split_qualified_name(const std::string& name) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true) {
        auto pos = name.find("::", start);
        if (pos == std::string::npos) {
            parts.push_back(name.substr(start));
            break;
        }
        parts.push_back(name.substr(start, pos - start));
        start = pos + 2;
    }
    return parts;
}
} // anonymous namespace

std::shared_ptr<aggregate>
scope_lookup::lookup_structure_or_import(unit& u, const std::shared_ptr<context>& ctx,
                                          std::shared_ptr<element> elem, const std::string& name) {
    // Simple (unqualified) name: standard scope-chain lookup among locally
    // declared aggregates first.
    if (name.find("::") == std::string::npos) {
        if (auto agg = lookup_structure(elem, name)) return agg;
    } else {
        // Namespace-qualified name (e.g. "k::Object"): descend namespaces from
        // the compilation unit's root, matching locally-declared aggregates.
        if (auto root_ns_ptr = root_namespace(*elem)) {
            k::name qname{false, split_qualified_name(name)};
            if (auto res = aggregate_type_resolver::resolve_struct_from(*root_ns_ptr, qname)) {
                return res;
            }
        }
    }

    // Fallback: the name may refer to a type materialised from a KDI-imported
    // module (e.g. "Object" or "k::Object" reachable via `import k;`), which is
    // not part of the locally-declared aggregate tree at all. This handles both
    // simple and qualified forms uniformly (find_imported_type searches across
    // all imported modules by whatever name form is given).
    k::name qname{false, split_qualified_name(name)};
    if (auto imp_agg = u.get_or_create_imported_aggregate(qname, ctx)) {
        return std::dynamic_pointer_cast<aggregate>(imp_agg);
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

std::shared_ptr<union_type_def>
scope_lookup::lookup_union(std::shared_ptr<element> elem, const std::string& name) {
    // Handle qualified names (e.g. "ns::MyUnion") by splitting on "::"
    auto sep = name.find("::");
    if (sep != std::string::npos) {
        // Qualified: walk from root namespace
        std::vector<std::string> parts;
        std::size_t pos = 0;
        while (true) {
            auto s = name.find("::", pos);
            if (s == std::string::npos) { parts.push_back(name.substr(pos)); break; }
            parts.push_back(name.substr(pos, s - pos));
            pos = s + 2;
        }
        // Traverse from root
        auto root = root_namespace(*elem);
        if (!root) return {};
        std::shared_ptr<element> current = root;
        for (size_t i = 0; i + 1 < parts.size(); ++i) {
            bool stepped = false;
            if (auto nspc = std::dynamic_pointer_cast<ns>(current)) {
                if (auto child = nspc->get_child_namespace(parts[i])) {
                    current = child; stepped = true;
                }
            }
            if (!stepped) return {};
        }
        // Last part: the union name
        if (auto uh = std::dynamic_pointer_cast<union_holder>(current)) {
            return uh->get_union(parts.back());
        }
        return {};
    }
    // Simple name: walk up scope chain
    for (auto current = elem; current; current = current->parent<element>()) {
        if (auto uh = std::dynamic_pointer_cast<union_holder>(current)) {
            if (auto un = uh->get_union(name)) return un;
        }
    }
    return {};
}

namespace {

/** Navigate from @p from through the leading parts of @p name (all but the last). */
std::shared_ptr<const element> navigate_scope(const std::shared_ptr<const element>& from,
                                              const k::name& name) {
    std::shared_ptr<const element> current = from;
    for (std::size_t i = 0; i + 1 < name.size() && current; ++i) {
        std::shared_ptr<const element> next;
        if (auto nspc = std::dynamic_pointer_cast<const ns>(current)) {
            next = nspc->get_child_namespace(name[i]);
        }
        if (!next) {
            if (auto ah = std::dynamic_pointer_cast<const aggregate_holder>(current)) {
                next = ah->get_aggregate(name[i]);
            }
        }
        current = next;
    }
    return current;
}

} // anonymous namespace

std::shared_ptr<alias_definition>
scope_lookup::lookup_alias(std::shared_ptr<const element> elem, const k::name& name) {
    if (!elem || name.empty()) return {};

    if (name.has_root_prefix()) {
        auto root = root_namespace(*elem);
        if (!root) return {};
        auto scope = navigate_scope(std::static_pointer_cast<const element>(root),
                                    name.without_root_prefix());
        if (auto ah = std::dynamic_pointer_cast<const alias_holder>(scope)) {
            return ah->get_alias(name.back());
        }
        return {};
    }

    for (auto current = elem; current; current = current->parent<const element>()) {
        if (name.size() == 1) {
            if (auto ah = std::dynamic_pointer_cast<const alias_holder>(current)) {
                if (auto al = ah->get_alias(name.front())) return al;
            }
        } else {
            if (auto scope = navigate_scope(current, name)) {
                if (auto ah = std::dynamic_pointer_cast<const alias_holder>(scope)) {
                    if (auto al = ah->get_alias(name.back())) return al;
                }
            }
        }
    }
    return {};
}

std::shared_ptr<type>
scope_lookup::materialize_alias_type(const std::shared_ptr<alias_definition>& alias,
                                     const std::shared_ptr<context>& ctx,
                                     const std::function<std::shared_ptr<type>(const k::name&, const element&)>& resolve_by_name,
                                     bool& cycle,
                                     const std::function<std::shared_ptr<type>(const std::shared_ptr<type>&, const element&)>& resolve_chain) {
    cycle = false;
    if (!alias) return {};

    // A parameterised alias denotes no type by itself: 'Vec' alone is not a
    // type, only 'Vec<int>' is. Resolution happens at the use site, through
    // type_reference_resolver::try_resolve_alias_template().
    if (alias->is_template()) return {};

    if (alias->_resolved) {
        return alias->get_declared_type();
    }
    if (alias->_resolving) {
        cycle = true;
        return {};
    }

    auto target = alias->_target_type;
    auto scope = alias->parent<element>();

    // A soft alias only carries the aliased name: try to read it as a type name.
    if (!target && !alias->get_target_name().empty() && scope && resolve_by_name) {
        alias->_resolving = true;
        target = resolve_by_name(alias->get_target_name(), *scope);
        alias->_resolving = false;
        if (!target || !type::is_resolved(target)) {
            // Not a type — the alias targets a function or a variable, resolved elsewhere.
            return {};
        }
    }

    if (!target) {
        return {};
    }

    alias->_resolving = true;

    // The alias body is resolved in the scope that declares it, never in the
    // scope that uses it.

    // A composite target — most notably a callable prototype `(int):bool` —
    // is resolved through the caller's own type-resolution chain: its
    // components are types, not a name.
    if (!type::is_resolved(target) && scope && resolve_chain) {
        if (auto resolved = resolve_chain(target, *scope)) {
            if (type::is_resolved(resolved)) target = resolved;
        }
    }
    if (!type::is_resolved(target) && ctx) {
        if (auto resolved = ctx->resolve_type(target)) {
            if (type::is_resolved(resolved)) target = resolved;
        }
    }
    if (!type::is_resolved(target) && scope && resolve_by_name) {
        if (auto unres = std::dynamic_pointer_cast<unresolved_type>(target)) {
            if (auto resolved = resolve_by_name(unres->type_id(), *scope)) {
                if (type::is_resolved(resolved)) target = resolved;
            }
        }
    }

    alias->_resolving = false;

    if (!type::is_resolved(target)) {
        return {};
    }

    alias->_target_type = target;
    alias->_target_kind = alias_definition::target_kind_t::TYPE;

    if (alias->is_strong() && ctx) {
        alias->_alias_type = ctx->create_alias_type(alias, target);
    }
    alias->_resolved = true;
    return alias->get_declared_type();
}

std::shared_ptr<type> scope_lookup::resolve_alias_template(
    const std::shared_ptr<alias_definition>& alias,
    const std::shared_ptr<unresolved_type>& unres,
    const element& context_elem,
    const std::shared_ptr<context>& ctx,
    const std::function<std::shared_ptr<type>(const std::shared_ptr<type>&, const element&)>& resolve_chain,
    const std::function<void(unsigned int, const std::string&, const std::vector<std::string>&)>& report_error)
{
    if (!alias || !unres || !ctx) return {};

    const std::string kind = alias->is_strong() ? "typedef" : "alias";

    if (!alias->is_template()) {
        // A plain alias given template arguments: 'Coord<int>' where 'Coord' is
        // not parameterised. Reported here rather than as an unknown type name,
        // which would be misleading.
        if (unres->has_template_args()) {
            report_error(static_cast<unsigned int>(::k::diag::alias_diag::ERR_ALIAS_NOT_A_TEMPLATE),
                         "'{}' is not a parameterised {}: it takes no template argument",
                         {alias->get_short_name(), kind});
        }
        return {};
    }

    auto* ti = alias->get_tpl_info();
    if (!ti) return {};

    // A nested alias reference produced by substitution (e.g. 'A<T>' inside
    // 'template<typename T> alias B : A<T>;') carries already-substituted model
    // arguments; they take precedence over the AST arguments, which still name
    // the enclosing alias's own parameters.
    const auto& model_args = unres->get_model_template_args();
    const bool use_model_args = unres->has_model_template_args();

    const auto& ast_args = unres->get_ast_template_args();
    const std::size_t given = use_model_args ? model_args.size() : ast_args.size();
    if (given > ti->params.size()) {
        report_error(static_cast<unsigned int>(::k::diag::alias_diag::ERR_ALIAS_TEMPLATE_ARG_MISMATCH),
                     "Parameterised {} '{}' takes {} template argument(s), {} given",
                     {kind, alias->get_short_name(),
                      std::to_string(ti->params.size()), std::to_string(given)});
    }

    // Resolve each argument, falling back on the parameter default when the
    // argument list is shorter than the parameter list.
    type_substitution_map subst;
    std::string args_key;
    std::string display_args;
    for (std::size_t i = 0; i < ti->params.size(); ++i) {
        const auto& param = ti->params[i];
        std::shared_ptr<type> arg_type;

        if (use_model_args) {
            if (i < model_args.size()) arg_type = model_args[i];
            // A substituted argument list carries a null slot for every argument
            // that names no template parameter ('bool' in 'Fn<T,bool>'): the AST
            // argument at the same index still describes it exactly.
            if (!arg_type && i < ast_args.size()) {
                const auto& ast_arg = ast_args[i];
                if (ast_arg && ast_arg->is_type() && ast_arg->type_arg) {
                    arg_type = ctx->from_type_specifier(*ast_arg->type_arg);
                }
            }
            if (!arg_type) arg_type = param.default_type;
            if (!arg_type) {
                report_error(static_cast<unsigned int>(::k::diag::alias_diag::ERR_ALIAS_TEMPLATE_ARG_MISMATCH),
                             "Parameterised {} '{}' takes {} template argument(s), {} given",
                             {kind, alias->get_short_name(),
                              std::to_string(ti->params.size()), std::to_string(given)});
            }
        } else if (i < ast_args.size()) {
            const auto& ast_arg = ast_args[i];
            if (!ast_arg || !ast_arg->is_type() || !ast_arg->type_arg) {
                report_error(static_cast<unsigned int>(::k::diag::alias_diag::ERR_ALIAS_TEMPLATE_VALUE_PARAM),
                             "Parameterised {} '{}' expects a type argument for parameter '{}'",
                             {kind, alias->get_short_name(), param.name});
            }
            arg_type = ctx->from_type_specifier(*ast_arg->type_arg);
        } else {
            arg_type = param.default_type;
            if (!arg_type) {
                report_error(static_cast<unsigned int>(::k::diag::alias_diag::ERR_ALIAS_TEMPLATE_ARG_MISMATCH),
                             "Parameterised {} '{}' takes {} template argument(s), {} given",
                             {kind, alias->get_short_name(),
                              std::to_string(ti->params.size()), std::to_string(given)});
            }
        }

        if (auto res_arg = resolve_chain(arg_type, context_elem)) {
            arg_type = res_arg;
        }
        if (!arg_type) {
            report_error(static_cast<unsigned int>(::k::diag::alias_diag::ERR_ALIAS_TEMPLATE_ARG_MISMATCH),
                         "Template argument for parameter '{}' of parameterised {} '{}' could not be resolved",
                         {param.name, kind, alias->get_short_name()});
        }

        subst[param.name] = arg_type;
        if (!args_key.empty()) { args_key += ","; display_args += ", "; }
        args_key += arg_type->to_string();
        display_args += arg_type->to_string();
    }

    // Substitute the arguments into the renamed type and resolve the result.
    auto target = alias->get_target_type();
    if (!target) {
        report_error(static_cast<unsigned int>(::k::diag::alias_diag::ERR_ALIAS_TEMPLATE_TARGET_UNRESOLVED),
                     "Parameterised {} '{}' does not rename a type",
                     {kind, alias->get_short_name()});
    }

    auto substituted = substitute_type(target, subst);
    if (auto res_sub = resolve_chain(substituted, context_elem)) {
        substituted = res_sub;
    }
    if (substituted && !type::is_resolved(substituted)) {
        if (auto retry = ctx->resolve_type(substituted)) {
            if (type::is_resolved(retry)) substituted = retry;
        }
    }
    if (!substituted) {
        report_error(static_cast<unsigned int>(::k::diag::alias_diag::ERR_ALIAS_TEMPLATE_TARGET_UNRESOLVED),
                     "The renamed type of parameterised {} '{}' could not be resolved with the given arguments",
                     {kind, alias->get_short_name()});
    }

    // A soft alias is transparent: the substituted type is the answer. A strong
    // one keeps a nominal identity, distinct for every argument list.
    if (!alias->is_strong()) return substituted;

    return ctx->create_template_alias_type(
        alias, substituted, args_key,
        alias->get_short_name() + "<" + display_args + ">");
}

bool scope_lookup::is_base_union_of(const union_type_def& candidate_base,
                                     const union_type_def& candidate_derived) {
    const union_type_def* cur = candidate_derived.get_base_union().get();
    while (cur) {
        if (cur == &candidate_base) return true;
        cur = cur->get_base_union().get();
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────────
// resolve_using_target — free helper (declared in resolvers_common.hpp)
// ─────────────────────────────────────────────────────────────────────────────

std::shared_ptr<const element>
resolve_using_target(const k::name& target_name, const unit& unit) {
    auto root = unit.get_root_namespace();
    if (!root) return nullptr;

    std::shared_ptr<const element> current = root;
    for (size_t i = 0; i < target_name.size(); ++i) {
        const auto& part = target_name[i];

        if (auto nspc = std::dynamic_pointer_cast<const ns>(current)) {
            if (auto child = nspc->get_child_namespace(part)) {
                current = child;
                continue;
            }
        }
        if (auto ah = std::dynamic_pointer_cast<const aggregate_holder>(current)) {
            if (auto agg = ah->get_aggregate(part)) {
                current = std::dynamic_pointer_cast<const element>(agg);
                continue;
            }
        }
        return nullptr;
    }
    return current;
}

// ─────────────────────────────────────────────────────────────────────────────
// is_enclosing_template_param_name — free helper (declared in resolvers_common.hpp)
// ─────────────────────────────────────────────────────────────────────────────

bool
is_enclosing_template_param_name(const element& context_elem, const std::string& arg_name) {
    for (auto current = context_elem.shared_as<const element>(); current; current = current->parent<element>()) {
        if (auto agg = std::dynamic_pointer_cast<const aggregate>(current)) {
            if (agg->is_template()) {
                if (auto* ti = agg->get_tpl_info()) {
                    for (auto& param : ti->params) {
                        if (param.name == arg_name) return true;
                    }
                }
            }
        } else if (auto un = std::dynamic_pointer_cast<const union_type_def>(current)) {
            if (un->is_template()) {
                if (auto* ti = un->get_tpl_info()) {
                    for (auto& param : ti->params) {
                        if (param.name == arg_name) return true;
                    }
                }
            }
        }
    }
    return false;
}

} // namespace k::model::gen
