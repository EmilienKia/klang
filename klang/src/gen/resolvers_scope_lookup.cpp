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

