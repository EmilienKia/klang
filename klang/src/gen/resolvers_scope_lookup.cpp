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
#include "../model/template.hpp"
#include "../parse/ast.hpp"
#include "../errors.hpp"

#include <kdi.hpp>
#include <algorithm>
#include <typeinfo>

namespace k::model::gen {

namespace {

bool source_may_name(const std::string& source, const std::string& name) {
    const auto identifier_char = [](char c) {
        return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')
            || (c >= '0' && c <= '9') || c == '_';
    };
    for (auto pos = source.find(name); pos != std::string::npos;
         pos = source.find(name, pos + 1)) {
        const auto end = pos + name.size();
        if ((pos == 0 || !identifier_char(source[pos - 1]))
            && (end == source.size() || !identifier_char(source[end])))
            return true;
    }
    return false;
}

bool qualified_name_may_name(const std::string& qualified, const std::string& name) {
    return qualified == name
        || (qualified.size() > name.size() + 1
            && qualified.compare(qualified.size() - name.size() - 2, 2, "::") == 0
            && qualified.compare(qualified.size() - name.size(), name.size(), name) == 0);
}

bool imported_aggregate_may_name(const kdi::kdi_aggregate& agg, const std::string& name) {
    if (agg.name == name) return true;
    for (const auto& alias : agg.aliases)
        if (alias.name == name || source_may_name(alias.source, name)) return true;
    for (const auto& un : agg.nested_unions)
        if (un.name == name) return true;
    for (const auto& nested : agg.nested)
        if (imported_aggregate_may_name(nested, name)) return true;
    return false;
}

bool imported_namespace_may_name(const kdi::kdi_namespace& ns, const std::string& name) {
    if (ns.name == name) return true;
    for (const auto& agg : ns.aggregates)
        if (imported_aggregate_may_name(agg, name)) return true;
    for (const auto& en : ns.enums)
        if (en.name == name) return true;
    for (const auto& un : ns.unions)
        if (un.name == name) return true;
    for (const auto& alias : ns.aliases)
        if (alias.name == name || source_may_name(alias.source, name)) return true;
    for (const auto& def : ns.template_defs) {
        if (def.name == name || source_may_name(def.source, name)
            || (def.aggregate_signature && imported_aggregate_may_name(*def.aggregate_signature, name)))
            return true;
    }
    for (const auto& child : ns.namespaces)
        if (imported_namespace_may_name(child, name)) return true;
    return false;
}

bool imported_file_may_name(const kdi::kdi_file& file, const std::string& name) {
    if (imported_namespace_may_name(file.unit.root_ns, name)) return true;
    for (const auto& entry : file.types.aggregates)
        if (qualified_name_may_name(entry.fq_name, name)) return true;
    for (const auto& entry : file.types.enums)
        if (qualified_name_may_name(entry.fq_name, name)) return true;
    return false;
}

bool imports_prove_name_absent(const unit& u, const std::string& name) {
    for (const auto& [key, agg] : u.get_imported_aggregates())
        if (key == name || (agg && agg->get_short_name() == name)) return false;
    for (const auto& imp : u.get_imports())
        if (!imp.kdi || imported_file_may_name(*imp.kdi, name)) return false;
    for (const auto& kdi : u.get_transitive_kdis())
        if (!kdi || imported_file_may_name(*kdi, name)) return false;
    return true;
}

bool generated_namespace_using_proves_name_absent(
    const unit& u, const element& scope, const using_directive& directive,
    const std::string& name)
{
    // Imported namespaces can also contain local declarations. Check their
    // model holders here; the loaded KDI scan below covers unmaterialised names.
    if (&scope != u.get_root_namespace().get() || directive.ast_node
        || !directive.is_namespace() || directive.has_alias())
        return false;
    auto target = std::dynamic_pointer_cast<const ns>(
        resolve_using_target(directive.target_name, u));
    if (!target || !target->get_using_directives().empty()
        || target->get_aggregate(name) || target->get_enum(name)
        || target->get_union(name) || target->get_alias(name)
        || target->get_child_namespace(name))
        return false;
    return true;
}

std::shared_ptr<aggregate> local_qualified_bound(
    const unit& u, const element& declaration, const k::name& written)
{
    auto root = u.get_root_namespace();
    if (!root || written.empty()) return {};
    auto path = written.without_root_prefix();
    if (written.has_root_prefix()) {
        const auto module = u.get_unit_name().without_root_prefix();
        if (!module.empty() && path.size() > module.size() && path.start_with(module))
            path = path.without_front(module.size());
    }
    if (path.empty()) return {};
    for (const auto& part : path.parts())
        if (part.empty() || part.find("::") != std::string::npos) return {};
    // Imported declarations or aliases named like the leading component could
    // change lexical lookup even when a local namespace has been materialized.
    if (!imports_prove_name_absent(u, path.front())
        || scope_lookup::lookup_alias(declaration.shared_as<const element>(), written))
        return {};

    auto conflicts = [](const element& scope, const std::string& part) {
        if (auto h = dynamic_cast<const alias_holder*>(&scope); h && h->get_alias(part)) return true;
        if (auto h = dynamic_cast<const aggregate_holder*>(&scope); h && h->get_aggregate(part)) return true;
        if (auto h = dynamic_cast<const enum_holder*>(&scope); h && h->get_enum(part)) return true;
        if (auto h = dynamic_cast<const union_holder*>(&scope); h && h->get_union(part)) return true;
        if (auto h = dynamic_cast<const variable_holder*>(&scope); h && h->get_variable(part)) return true;
        if (auto h = dynamic_cast<const function_holder*>(&scope); h && h->get_function(part)) return true;
        return false;
    };

    std::shared_ptr<const ns> current;
    size_t next = 0;
    if (written.has_root_prefix()) {
        current = root;
    } else {
        if (path.size() < 2) return {};
        for (auto scope = declaration.shared_as<const element>(); scope;
             scope = scope->parent<const element>()) {
            if (scope_lookup::is_definition_namespace_boundary(*scope)) return {};
            if (auto owner = std::dynamic_pointer_cast<const aggregate>(scope)) {
                if ((scope.get() != &declaration
                     && (owner->is_template() || owner->is_instantiation()
                         || owner->is_imported() || !owner->get_ast_aggregate_decl()))
                    || !owner->get_bases().empty()) return {};
            }
            if (auto using_scope = std::dynamic_pointer_cast<const using_holder>(scope))
                for (const auto& directive : using_scope->get_using_directives())
                    if (!generated_namespace_using_proves_name_absent(
                            u, *scope, directive, path.front())) return {};
            if (current) continue;
            if (conflicts(*scope, path.front())) return {};
            if (auto owner = std::dynamic_pointer_cast<const ns>(scope)) {
                auto child = owner->get_child_namespace(path.front());
                if (child) {
                    if (!child->is_source_declared_namespace()) return {};
                    current = child;
                    next = 1;
                }
            }
        }
    }
    if (!current) return {};
    for (; next + 1 < path.size(); ++next) {
        if (conflicts(*current, path[next])) return {};
        auto child = current->get_child_namespace(path[next]);
        if (!child || !child->is_source_declared_namespace()
            || scope_lookup::is_definition_namespace_boundary(*child)) return {};
        current = child;
    }
    const auto& last = path.back();
    if (current->get_alias(last) || current->get_enum(last) || current->get_union(last)
        || current->get_child_namespace(last) || current->get_variable(last)
        || current->get_function(last)) return {};
    auto candidate = current->get_aggregate(last);
    if (!candidate || candidate->is_template() || candidate->is_instantiation()
        || candidate->is_imported() || !candidate->get_ast_aggregate_decl()
        || !candidate->get_struct_type()
        || candidate->get_struct_type()->get_struct() != candidate) return {};
    return candidate;
}

} // namespace

std::optional<size_t> scope_lookup::same_declaration_constraint_binder(
    const tpl_info& info, size_t index)
{
    if (info.is_generic || !info.binder_scope || index >= info.params.size()) return {};
    const auto& source = info.params[index].constraint_source_type;
    if (!source || source->has_unknown_bindings() || source->has_unmodelled_arguments())
        return {};
    auto written = std::dynamic_pointer_cast<unresolved_type>(source->get_type());
    if (!written || !written->source_binder() || written->has_template_args()
        || written->has_model_template_args()) return {};
    const auto& binder = *written->source_binder();
    if (binder.scope != info.binder_scope || binder.ordinal >= index
        || !info.params[binder.ordinal].is_type_param()
        || info.params[binder.ordinal].is_pack) return {};
    const auto& id = written->type_id();
    if (id.has_root_prefix() || id.size() != 1
        || id.front() != info.params[binder.ordinal].name) return {};
    return binder.ordinal;
}

scope_lookup::local_bound_result scope_lookup::inspect_local_constraint_bound(
    const unit& u, const element& declaration, const tpl_info& info, size_t index,
    const std::vector<template_argument>& args)
{
    local_bound_result result;
    if (info.is_generic || info.is_imported_signature_only
        || !info.origin_module_ns_fq.empty() || info.private_definition_scope
        || index >= info.params.size() || is_imported_definition_context(declaration))
        return result;
    result.parameter_index = index;
    const auto& param = info.params[index];
    const auto& source = param.constraint_source_type;
    if (param.is_pack || !param.is_type_param() || !param.constraint_type || !source
        || source->has_unknown_bindings() || source->has_unmodelled_arguments())
        return result;
    if (auto binder = same_declaration_constraint_binder(info, index)) {
        if (args.size() != info.params.size()
            || !is_local_concrete_constraint_argument(args[*binder])) return result;
        auto bound_type = std::dynamic_pointer_cast<struct_type>(
            type::remove_const(args[*binder].type_arg));
        result.bound = bound_type ? bound_type->get_struct() : nullptr;
        if (result.bound) result.state = local_bound_state::aggregate;
        return result;
    }
    auto written = std::dynamic_pointer_cast<unresolved_type>(source->get_type());
    if (!written || written->source_binder() || written->is_template_param_placeholder()
        || written->has_template_args() || written->has_model_template_args())
        return result;
    const auto& id = written->type_id();
    if (id.empty() || id.front().empty() || id.front().find("::") != std::string::npos
        || source->lexical_bindings().contains(id.front()))
        return result;
    for (const auto& other : info.params)
        if (other.name == id.front()) return result;

    if (id.has_root_prefix() || id.size() > 1) {
        if (auto candidate = local_qualified_bound(u, declaration, id)) {
            result.state = local_bound_state::aggregate;
            result.name = id.to_string();
            result.bound = std::move(candidate);
        }
        return result;
    }

    result.name = id.front();
    if (auto alias = lookup_alias(declaration.shared_as<const element>(), id)) {
        // Only a direct namespace-level soft alias to a sibling source aggregate
        // has enough lexical evidence here; cached alias targets can be stale.
        auto scope = std::dynamic_pointer_cast<const ns>(declaration.parent<const element>());
        if (!dynamic_cast<const function*>(&declaration) || !scope
            || scope->get_alias(id.front()) != alias || alias->is_imported()
            || alias->is_template() || !alias->is_soft()
            || scope->get_aggregate(id.front()) || scope->get_enum(id.front())
            || scope->get_union(id.front()) || scope->get_child_namespace(id.front())
            || scope->get_variable(id.front()) || scope->get_function(id.front())) return result;
        auto ast = alias->get_ast_alias_decl();
        auto target = ast
            ? std::dynamic_pointer_cast<k::parse::ast::identified_type_specifier>(ast->type)
            : nullptr;
        if (!target || target->has_explicit_template_args || !target->template_args.empty())
            return result;
        const auto target_name = target->name.to_name();
        if (target_name.has_root_prefix() || target_name.size() != 1
            || target_name.front().empty()
            || scope->get_alias(target_name.front())
            || scope->get_enum(target_name.front())
            || scope->get_union(target_name.front())
            || scope->get_child_namespace(target_name.front())
            || scope->get_variable(target_name.front())
            || scope->get_function(target_name.front())) return result;
        auto candidate = scope->get_aggregate(target_name.front());
        if (!candidate || candidate->is_template() || candidate->is_instantiation()
            || candidate->is_imported() || !candidate->get_ast_aggregate_decl()
            || !candidate->get_struct_type()) return result;
        result.state = local_bound_state::aggregate;
        result.bound = candidate;
        return result;
    }
    for (auto scope = declaration.shared_as<const element>(); scope;
         scope = scope->parent<element>()) {
        if (is_definition_namespace_boundary(*scope)) return result;
        if (auto owner = std::dynamic_pointer_cast<const aggregate>(scope)) {
            if (scope.get() != &declaration
                && (owner->is_template() || owner->is_instantiation() || owner->is_imported()
                    || !owner->get_ast_aggregate_decl() || !owner->get_bases().empty()))
                return result;
            if (scope.get() == &declaration && !owner->get_bases().empty()) return result;
        }
        if (auto holder = std::dynamic_pointer_cast<const aggregate_holder>(scope)) {
            if (auto candidate = holder->get_aggregate(id.front())) {
                if (!candidate->is_template() && !candidate->is_instantiation()
                    && !candidate->is_imported() && candidate->get_ast_aggregate_decl()
                    && candidate->get_struct_type()) {
                    for (auto owner = candidate->ancestor<aggregate>(); owner;
                         owner = owner->ancestor<aggregate>())
                        if (owner->is_template() || owner->is_instantiation()
                            || owner->is_imported()) return result;
                    result.state = local_bound_state::aggregate;
                    result.bound = candidate;
                }
                return result;
            }
        }
        if (auto enums = std::dynamic_pointer_cast<const enum_holder>(scope);
            enums && enums->get_enum(id.front())) return result;
        if (auto unions = std::dynamic_pointer_cast<const union_holder>(scope);
            unions && unions->get_union(id.front())) return result;
        if (auto usings = std::dynamic_pointer_cast<const using_holder>(scope))
            for (const auto& directive : usings->get_using_directives())
                if (!generated_namespace_using_proves_name_absent(
                        u, *scope, directive, id.front()))
                    return result;
    }
    if (imports_prove_name_absent(u, id.front()))
        result.state = local_bound_state::missing;
    return result;
}

bool scope_lookup::is_local_concrete_constraint_argument(const template_argument& arg) {
    if (!arg.is_type()) return false;
    auto arg_st = std::dynamic_pointer_cast<struct_type>(type::remove_const(arg.type_arg));
    auto arg_agg = arg_st ? arg_st->get_struct() : nullptr;
    if (!arg_agg || arg_agg->is_template() || arg_agg->is_instantiation()
        || arg_agg->is_imported() || !arg_agg->get_ast_aggregate_decl()) return false;
    for (auto owner = arg_agg->ancestor<aggregate>(); owner; owner = owner->ancestor<aggregate>())
        if (owner->is_template() || owner->is_instantiation() || owner->is_imported())
            return false;
    return true;
}

bool scope_lookup::is_proven_unrelated_with_implicit_object(
    const unit& u, const template_argument& arg, const std::shared_ptr<aggregate>& bound)
{
    if (!is_local_concrete_constraint_argument(arg) || !bound || !bound->get_struct_type()
        || bound->get_struct_type()->get_struct() != bound || bound->is_template())
        return false;
    const auto* imported_k = u.find_import(k::name("k"));
    if (!imported_k || !imported_k->kdi || imported_k->kdi->header.module_name != "k")
        return false;
    const auto is_root_object = [&](const std::shared_ptr<aggregate>& candidate) {
        auto imported = std::dynamic_pointer_cast<imported_aggregate>(candidate);
        auto descriptor = imported ? imported->get_kdi_aggregate() : nullptr;
        return descriptor && descriptor->name == "Object"
            && (descriptor->fq_name == "k::Object" || descriptor->fq_name == "::k::Object")
            && descriptor->kind == kdi::kdi_aggregate_kind::class_
            && !descriptor->template_origin && descriptor->bases.empty()
            && candidate->get_name().without_root_prefix().to_string() == "k::Object"
            && candidate->get_bases().empty()
            && std::any_of(imported_k->kdi->unit.root_ns.aggregates.begin(),
                           imported_k->kdi->unit.root_ns.aggregates.end(),
                           [&](const auto& agg) { return &agg == descriptor; });
    };
    auto st = std::dynamic_pointer_cast<struct_type>(type::remove_const(arg.type_arg));
    struct visit {
        std::shared_ptr<aggregate> agg;
        bool leaving;
    };
    std::vector<visit> pending{{st->get_struct(), false}};
    std::unordered_set<const aggregate*> active, finished;
    bool saw_implicit_object = false;
    while (!pending.empty()) {
        auto item = std::move(pending.back());
        pending.pop_back();
        if (!item.agg) return false;
        if (item.leaving) {
            active.erase(item.agg.get());
            finished.insert(item.agg.get());
            continue;
        }
        if (active.contains(item.agg.get())) return false;
        if (finished.contains(item.agg.get())) continue;
        if (item.agg == bound || item.agg->get_struct_type() == bound->get_struct_type()
            || (!item.agg->get_fq_name().empty()
                && item.agg->get_fq_name() == bound->get_fq_name())
            || (!item.agg->get_mangled_name().empty()
                && item.agg->get_mangled_name() == bound->get_mangled_name()))
            return false;
        if (is_root_object(item.agg)) {
            finished.insert(item.agg.get());
            continue;
        }
        if (!item.agg->get_struct_type()
            || item.agg->get_struct_type()->get_struct() != item.agg
            || !is_local_concrete_constraint_argument(
                template_argument::make_type(item.agg->get_struct_type())))
            return false;
        auto declaration = item.agg->get_ast_aggregate_decl();
        const auto& bases = item.agg->get_bases();
        const bool implicit_object = declaration->bases.empty() && bases.size() == 1
            && item.agg->is_class() && bases[0].raw_name == "Object"
            && is_root_object(bases[0].base);
        if (!implicit_object && declaration->bases.size() != bases.size()) return false;
        if (!bases.empty() && !item.agg->bases_resolved()) return false;
        if (!implicit_object)
            for (size_t i = 0; i < bases.size(); ++i)
                if (bases[i].raw_name != declaration->bases[i].qualified_name) return false;
        saw_implicit_object |= implicit_object;
        active.insert(item.agg.get());
        pending.push_back({item.agg, true});
        for (const auto& base : bases) {
            if (!base.base) return false;
            pending.push_back({base.base, false});
        }
    }
    return saw_implicit_object;
}

bool scope_lookup::has_proven_local_ctad_default(
    const aggregate& declaration, const tpl_info& info,
    const std::vector<template_argument>& args)
{
    auto ast = declaration.get_ast_aggregate_decl();
    auto scope = std::dynamic_pointer_cast<const ns>(declaration.parent<const element>());
    if (info.is_generic || info.is_imported_signature_only || declaration.is_imported()
        || declaration.is_instantiation() || !info.origin_module_ns_fq.empty()
        || info.private_definition_scope || !scope
        || is_imported_definition_context(declaration)
        || !ast || !ast->bases.empty() || !declaration.get_bases().empty()
        || !declaration.get_using_directives().empty()
        || info.params.size() != 1 || args.size() != 1
        || ast->template_params.size() != 1 || !ast->template_params[0]
        || !info.params[0].is_type_param() || info.params[0].is_pack
        || !is_local_concrete_constraint_argument(args[0])) return false;
    auto written = std::dynamic_pointer_cast<k::parse::ast::identified_type_specifier>(
        ast->template_params[0]->default_type_spec);
    if (!written || written->has_explicit_template_args || !written->template_args.empty())
        return false;
    const auto name = written->name.to_name();
    if (name.has_root_prefix() || name.size() != 1 || name.front().empty()
        || name.front() == info.params[0].name) return false;
    const auto& id = name.front();
    if (declaration.get_aggregate(id) || declaration.get_alias(id)
        || declaration.get_enum(id) || declaration.get_union(id)
        || declaration.get_variable(id) || declaration.get_function(id)
        || scope->get_alias(id) || scope->get_enum(id) || scope->get_union(id)
        || scope->get_child_namespace(id) || scope->get_variable(id)
        || scope->get_function(id)) return false;
    auto expected = scope->get_aggregate(id);
    auto actual = std::dynamic_pointer_cast<struct_type>(type::remove_const(args[0].type_arg));
    return expected && expected->get_ast_aggregate_decl() && !expected->is_imported()
        && !expected->is_template() && !expected->is_instantiation()
        && actual && actual->get_struct() == expected;
}

scope_lookup::local_bound_result scope_lookup::analyze_local_aggregate_constraint(
    const unit& u, const aggregate& declaration, const tpl_info& info,
    const std::vector<template_argument>& args, const element& use_site)
{
    local_bound_result deferred;
    if (!declaration.get_ast_aggregate_decl() || declaration.is_imported()
        || declaration.is_instantiation() || args.size() != info.params.size()
        || std::any_of(info.params.begin(), info.params.end(),
                       [](const auto& param) { return param.is_pack; }))
        return deferred;
    bool has_source_function = false;
    for (auto scope = use_site.shared_as<const element>(); scope;
         scope = scope->parent<const element>()) {
        if (auto fn = std::dynamic_pointer_cast<const function>(scope)) {
            if (fn->is_template() || fn->is_instantiation() || !fn->get_ast_function_decl())
                return deferred;
            has_source_function = true;
        }
        if (auto owner = std::dynamic_pointer_cast<const aggregate>(scope);
            owner && (owner->is_template() || owner->is_instantiation()
                      || owner->is_imported() || !owner->get_ast_aggregate_decl()))
            return deferred;
    }
    if (!has_source_function) return deferred;

    local_bound_result result;
    bool has_deferred_bound = false;
    for (size_t i = 0; i < info.params.size(); ++i) {
        const auto& param = info.params[i];
        if (!param.is_type_param() || (!param.constraint_type && !param.constraint_source_type))
            continue;
        if (!is_local_concrete_constraint_argument(args[i])) {
            has_deferred_bound = true;
            continue;
        }
        auto bound = inspect_local_constraint_bound(u, declaration, info, i, args);
        if (bound.state == local_bound_state::missing) return bound;
        if (bound.state != local_bound_state::aggregate) {
            has_deferred_bound = true;
            continue;
        }
        tpl_info one;
        one.binder_scope = info.binder_scope;
        one.params.push_back(param);
        one.params.front().constraint_type = bound.bound->get_struct_type();
        one.params.front().constraint_source_type =
            generic_source_type::capture(bound.bound->get_struct_type(), {});
        auto evidence = analyze_template_arg_constraints(
            one, {args[i]}, {generic_source_type::capture(args[i].type_arg, {})});
        if (evidence.state == constraint_analysis_state::verified) {
            result.verified_indices.push_back(i);
        } else if (evidence.checks.size() == 1
                   && ((evidence.state == constraint_analysis_state::invalid
                        && evidence.checks.front().reason == constraint_analysis_reason::constraint)
                       || (evidence.state == constraint_analysis_state::deferred
                           && evidence.checks.front().reason == constraint_analysis_reason::incomplete_hierarchy
                           && is_proven_unrelated_with_implicit_object(u, args[i], bound.bound)))) {
            bound.state = local_bound_state::violated;
            return bound;
        } else {
            has_deferred_bound = true;
        }
    }
    if (!result.verified_indices.empty() && !has_deferred_bound)
        result.state = local_bound_state::verified;
    return result;
}

std::shared_ptr<aggregate> scope_lookup::lookup_imported_lexical_aggregate(
    unit& u, const std::shared_ptr<context>& ctx,
    const element& scope, const k::name& name)
{
    if (name.empty() || name.has_root_prefix()) return {};
    auto host = dynamic_cast<const aggregate*>(&scope);

    k::name origin;
    if (host) {
        auto definition = host->get_tpl_definition();
        auto ti = definition ? definition->get_tpl_info() : host->get_tpl_info();
        if (ti && !ti->origin_module_ns_fq.empty()) {
            origin = k::name::from(ti->origin_module_ns_fq).without_root_prefix();
        } else if (host->is_imported()) {
            origin = k::name::from(host->get_fq_name()).without_root_prefix().without_back();
        }
    } else if (auto definition_scope = dynamic_cast<const ns*>(&scope);
               definition_scope && is_definition_namespace_boundary(scope)) {
        origin = definition_scope->get_name().without_root_prefix();
    }

    while (!origin.empty()) {
        auto qualified = origin;
        for (const auto& part : name.parts()) qualified = qualified.with_back(part);
        if (auto imported = u.get_or_create_imported_aggregate(qualified.with_root_prefix(), ctx)) {
            return imported;
        }
        // Source recipes are flattened into the consumer root. Only accept a
        // recipe whose recorded declaration origin matches this lexical lookup.
        if (auto root = u.get_root_namespace()) {
            if (auto recipe = root->get_aggregate(qualified.back())) {
                auto recipe_ti = recipe->get_tpl_info();
                if (recipe_ti && !recipe_ti->origin_module_ns_fq.empty()
                    && unit::make_origin_absolute_name(
                        recipe_ti->origin_module_ns_fq, recipe->get_short_name())
                        == qualified.with_root_prefix()) {
                    return recipe;
                }
            }
        }
        origin = origin.without_back();
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

bool scope_lookup::is_definition_namespace_boundary(const element& scope) {
    auto current = dynamic_cast<const ns*>(&scope);
    auto parent = current ? current->parent<ns>() : nullptr;
    return parent && parent->is_root()
        && current->get_name().without_back() != parent->get_name();
}

bool scope_lookup::is_imported_definition_context(const element& context) {
    for (auto current = context.shared_as<const element>(); current; current = current->parent<element>()) {
        if (is_definition_namespace_boundary(*current)) return true;
    }
    return false;
}

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
    auto find_target = [](const std::shared_ptr<const element>& from,
                          const k::name& name) -> std::shared_ptr<const element> {
        auto scope = navigate_scope(from, name);
        if (auto ah = std::dynamic_pointer_cast<const aggregate_holder>(scope)) {
            if (auto agg = ah->get_aggregate(name.back())) return agg;
        }
        if (auto fh = std::dynamic_pointer_cast<const function_holder>(scope)) {
            if (auto fn = fh->get_function(name.back())) return fn;
        }
        return {};
    };

    for (const auto& dir : directives) {
        auto name = dir.target_name.without_root_prefix();
        if (name.empty()) continue;

        std::shared_ptr<const element> target;
        if (!dir.target_name.has_root_prefix()) {
            auto declaration = owner_agg.get_tpl_definition();
            for (auto scope = declaration ? declaration->shared_as<const element>()
                                          : owner_agg.shared_as<const element>(); scope;
                 scope = scope->parent<const element>()) {
                if ((target = find_target(scope, name))) break;
                if (is_definition_namespace_boundary(*scope)) break;
            }
        }
        if (!target) {
            auto root = unit.get_root_namespace();
            if (!root) continue;
            const auto module = root->get_name().without_root_prefix();
            if (name.size() > module.size() && name.start_with(module)) {
                name = name.without_front(module.size());
                target = find_target(root, name);
            } else if (dir.target_name.has_root_prefix()) {
                target = find_target(root, name);
            }
        }

        if (!target) continue;

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

bool scope_lookup::same_signature_type(const std::shared_ptr<type>& a, const std::shared_ptr<type>& b) {
    auto ta = type::canonical(a);
    auto tb = type::canonical(b);
    auto is_void = [](const std::shared_ptr<type>& t) {
        auto u = std::dynamic_pointer_cast<unresolved_type>(t);
        return !t || (u && u->type_id().to_string() == "void");
    };
    if (is_void(ta) || is_void(tb)) return is_void(ta) == is_void(tb);

    auto ua = std::dynamic_pointer_cast<unresolved_callable_type>(ta);
    auto ub = std::dynamic_pointer_cast<unresolved_callable_type>(tb);
    auto ca = std::dynamic_pointer_cast<callable_type>(ta);
    auto cb = std::dynamic_pointer_cast<callable_type>(tb);
    if (ua || ub || ca || cb) {
        if ((!ua && !ca) || (!ub && !cb)) return false;
        if ((ua ? ua->get_addresser() : ca->get_addresser())
            != (ub ? ub->get_addresser() : cb->get_addresser())) return false;
        auto owner_name = [](const auto& unresolved, const auto& callable) {
            if (unresolved) return unresolved->owner_name();
            auto member = std::dynamic_pointer_cast<member_function_reference_type>(callable);
            return member && member->get_member_of() ? member->get_member_of()->get_name() : k::name{};
        };
        if (owner_name(ua, ca) != owner_name(ub, cb)) return false;
        const auto& pa = ua ? ua->parameter_types() : ca->get_parameter_types();
        const auto& pb = ub ? ub->parameter_types() : cb->get_parameter_types();
        if (pa.size() != pb.size()) return false;
        for (size_t i = 0; i < pa.size(); ++i) {
            if (!same_signature_type(pa[i], pb[i])) return false;
        }
        if (!same_signature_type(ua ? ua->get_return_type() : ca->get_return_type(),
                                 ub ? ub->get_return_type() : cb->get_return_type())) return false;
        const auto& ea = ua ? ua->get_throws() : ca->get_throws();
        const auto& eb = ub ? ub->get_throws() : cb->get_throws();
        if (ea.size() != eb.size()) return false;
        std::vector<bool> matched(eb.size(), false);
        for (const auto& exception : ea) {
            size_t i = 0;
            for (; i < eb.size(); ++i) {
                if (!matched[i] && same_signature_type(exception, eb[i])) {
                    matched[i] = true;
                    break;
                }
            }
            if (i == eb.size()) return false;
        }
        return true;
    }
    auto generic_a = std::dynamic_pointer_cast<unresolved_type>(ta);
    auto generic_b = std::dynamic_pointer_cast<unresolved_type>(tb);
    if ((generic_a && generic_a->has_model_only_template_args())
        || (generic_b && generic_b->has_model_only_template_args()))
        return type::are_equal(ta, tb);
    if (type::are_equal(ta, tb)) return true;
    if (ta->get_subtype() && tb->get_subtype() && typeid(*ta) == typeid(*tb)) {
        if (auto array = std::dynamic_pointer_cast<sized_array_type>(ta)) {
            if (array->get_size() != std::dynamic_pointer_cast<sized_array_type>(tb)->get_size()) return false;
        }
        return same_signature_type(ta->get_subtype(), tb->get_subtype());
    }
    return ta->to_string() == tb->to_string();
}

bool scope_lookup::same_parameter_signature(const function& a, const function& b) {
    if (a.is_static() != b.is_static() || a.is_const_member() != b.is_const_member()) return false;
    if (a.get_parameter_size() != b.get_parameter_size()) return false;
    for (size_t i = 0; i < a.get_parameter_size(); ++i) {
        if (!same_signature_type(std::const_pointer_cast<type>(a.get_parameter(i)->get_type()),
                                 std::const_pointer_cast<type>(b.get_parameter(i)->get_type()))) return false;
    }
    return true;
}

std::vector<std::shared_ptr<function>>
scope_lookup::lookup_member_functions(const std::shared_ptr<aggregate>& agg, const std::string& name) {
    std::vector<std::shared_ptr<function>> result;
    std::unordered_set<const function*> seen_functions;
    std::unordered_set<const aggregate*> seen_aggregates;
    auto overrides = [](const std::shared_ptr<function>& derived, const std::shared_ptr<function>& base) {
        for (auto overridden = derived->get_overrides(); overridden; overridden = overridden->get_overrides()) {
            if (overridden == base) return true;
        }
        return false;
    };
    std::function<void(const std::shared_ptr<aggregate>&)> collect;
    collect = [&](const std::shared_ptr<aggregate>& current) {
        if (!current || !seen_aggregates.insert(current.get()).second) return;
        for (const auto& fn : current->get_functions(name)) {
            if (!fn || !seen_functions.insert(fn.get()).second) continue;
            bool hidden = false;
            for (auto it = result.begin(); it != result.end();) {
                const auto& existing = *it;
                if (!same_parameter_signature(*existing, *fn)) {
                    ++it;
                    continue;
                }
                auto owner = existing->get_owner();
                // Preserve declaration-order selection for same-owner signatures,
                // including the stdlib's covariant Writer adapters.
                if (owner == current) {
                    hidden = true;
                    break;
                }
                if (owner && ((owner != current && owner->is_derived_from(current))
                              || overrides(existing, fn))) {
                    hidden = true;
                    break;
                }
                // A more-derived base can also be visited after its ancestor;
                // keep the final overrider already selected by the vtable.
                if (owner && ((owner != current && current->is_derived_from(owner))
                              || overrides(fn, existing))) {
                    it = result.erase(it);
                } else {
                    ++it;
                }
            }
            if (!hidden) result.push_back(fn);
        }
        for (const auto& base : current->get_bases()) collect(base.base);
    };
    collect(agg);
    return result;
}

std::vector<std::shared_ptr<function>>
scope_lookup::lookup_functions(std::shared_ptr<element> elem, const std::string& name) {
    std::vector<std::shared_ptr<function>> result;
    std::unordered_set<const function*> seen_functions;
    auto append = [&](const std::shared_ptr<function>& fn) {
        if (fn && seen_functions.insert(fn.get()).second) result.push_back(fn);
    };

    for (auto current = elem; current; current = current->parent<element>()) {
        if (auto param = std::dynamic_pointer_cast<parameter>(current)) {
            if (auto scope = param->get_default_definition_scope())
                return lookup_functions(scope, name);
        }
        if (auto agg = std::dynamic_pointer_cast<aggregate>(current)) {
            // Member call lookup includes inherited member functions so unqualified
            // calls inside member/default-method bodies can resolve base methods.
            for (const auto& fn : lookup_member_functions(agg, name)) append(fn);
            continue;
        }
        if (auto fh = std::dynamic_pointer_cast<function_holder>(current)) {
            for (auto& fn : fh->get_functions(name)) {
                append(fn);
            }
        }
        if (auto fn = std::dynamic_pointer_cast<function>(current)) {
            if (auto scope = fn->get_template_definition_scope()) {
                auto dependencies = scope->get_functions(name);
                if (!dependencies.empty()) {
                    for (const auto& dependency : dependencies) append(dependency);
                    return result;
                }
            }
        }
        if (auto uh = std::dynamic_pointer_cast<using_holder>(current)) {
            auto root = root_namespace(*current);
            auto owner = root ? root->parent<unit>() : nullptr;
            if (owner) {
                for (const auto& directive : uh->get_using_directives()) {
                    if (!directive.is_namespace() || directive.has_alias()) continue;
                    auto target = resolve_using_target(directive.target_name, *owner);
                    if (auto holder = std::dynamic_pointer_cast<const function_holder>(target)) {
                        for (const auto& fn : holder->get_functions(name)) append(fn);
                    }
                }
            }
        }
        if (is_definition_namespace_boundary(*current)) break;
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
    // A leading "::" roots the name: the scope chain is not searched.
    const bool rooted = name.rfind("::", 0) == 0;
    const std::string body = rooted ? name.substr(2) : name;
    if (body.empty()) return {};
    k::name qname{false, split_qualified_name(body)};

    // Simple (unqualified) name: standard scope-chain lookup among locally
    // declared aggregates first.
    if (!rooted && body.find("::") == std::string::npos) {
        if (auto agg = lookup_structure(elem, body)) return agg;
    } else {
        // Namespace-qualified name (e.g. "k::Object"): descend namespaces from
        // the compilation unit's root, matching locally-declared aggregates.
        // A rooted name may repeat the current module name as its first part.
        if (auto root_ns_ptr = root_namespace(*elem)) {
            if (auto res = aggregate_type_resolver::resolve_struct_from(*root_ns_ptr, qname)) {
                return res;
            }
            if (rooted) {
                const auto unit_name = u.get_unit_name().without_root_prefix();
                if (!unit_name.empty() && qname.size() > unit_name.size()
                    && qname.start_with(unit_name)) {
                    if (auto res = aggregate_type_resolver::resolve_struct_from(
                            *root_ns_ptr, qname.without_front(unit_name.size()))) {
                        return res;
                    }
                }
            }
        }
    }

    // Fallback: the name may refer to a type materialised from a KDI-imported
    // module (e.g. "Object" or "k::Object" reachable via `import k;`), which is
    // not part of the locally-declared aggregate tree at all. This handles both
    // simple and qualified forms uniformly (find_imported_type searches across
    // all imported modules by whatever name form is given).
    if (auto imp_agg = u.get_or_create_imported_aggregate(qname, ctx)) {
        return std::dynamic_pointer_cast<aggregate>(imp_agg);
    }
    return {};
}

std::shared_ptr<enumeration>
scope_lookup::lookup_enumeration(std::shared_ptr<element> elem, const std::string& name) {
    if (!elem || name.empty()) return {};
    const auto qualified = k::name::from(name);
    if (qualified.empty()) return {};
    const auto lookup_from = [](const auto& self, const std::shared_ptr<const element>& scope,
                                const k::name& remaining) -> std::shared_ptr<enumeration> {
        if (remaining.empty()) return {};
        if (remaining.size() == 1) {
            if (auto holder = std::dynamic_pointer_cast<const enum_holder>(scope)) {
                return holder->get_enum(remaining.front());
            }
            return {};
        }
        const auto rest = remaining.without_front();
        if (auto namespace_scope = std::dynamic_pointer_cast<const ns>(scope)) {
            if (auto child = namespace_scope->get_child_namespace(remaining.front())) {
                if (auto en = self(self, child, rest)) return en;
            }
        }
        if (auto holder = std::dynamic_pointer_cast<const aggregate_holder>(scope)) {
            if (auto child = holder->get_aggregate(remaining.front())) {
                if (auto en = self(self, child, rest)) return en;
            }
        }
        return {};
    };
    const auto lookup_root = [&]() -> std::shared_ptr<enumeration> {
        auto root = root_namespace(*elem);
        if (!root) return {};
        const auto module = root->get_name().without_root_prefix();
        const auto relative = qualified.without_root_prefix();
        if (!module.empty() && relative.start_with(module)) {
            if (auto en = lookup_from(lookup_from, root, relative.without_front(module.size()))) {
                return en;
            }
        }
        return lookup_from(lookup_from, root, relative);
    };
    if (qualified.has_root_prefix()) return lookup_root();

    for (auto current = elem; current; current = current->parent<element>()) {
        if (auto en = lookup_from(lookup_from, current, qualified)) return en;
        if (auto holder = std::dynamic_pointer_cast<const using_holder>(current)) {
            auto root = root_namespace(*current);
            auto owner = root ? root->parent<unit>() : nullptr;
            if (owner) {
                for (const auto& directive : holder->get_using_directives()) {
                    auto target_name = directive.target_name;
                    auto remaining = qualified;
                    if (directive.is_namespace()) {
                        if (directive.has_alias()) {
                            if (remaining.front() != *directive.alias_name || remaining.size() == 1) continue;
                            remaining = remaining.without_front();
                        }
                    } else {
                        const auto& lookup_name = directive.has_alias()
                            ? *directive.alias_name : target_name.back();
                        if (remaining.front() != lookup_name) continue;
                        remaining = k::name{target_name.back()};
                        for (size_t i = 1; i < qualified.size(); ++i)
                            remaining = remaining.with_back(qualified[i]);
                        target_name = target_name.without_back();
                    }
                    auto target = target_name.empty()
                        ? std::static_pointer_cast<const element>(root)
                        : resolve_using_target(target_name, *owner);
                    if (auto en = lookup_from(lookup_from, target, remaining)) return en;
                }
            }
        }
        if (is_definition_namespace_boundary(*current)) return {};
    }
    return lookup_root();
}

std::shared_ptr<union_type_def>
scope_lookup::lookup_union(std::shared_ptr<element> elem, const std::string& name) {
    // Handle qualified names (e.g. "ns::MyUnion") by splitting on "::"
    auto sep = name.find("::");
    if (sep != std::string::npos) {
        // Qualified: walk from root namespace
        std::vector<std::string> parts;
        std::size_t pos = name.starts_with("::") ? 2 : 0;
        while (true) {
            auto s = name.find("::", pos);
            if (s == std::string::npos) { parts.push_back(name.substr(pos)); break; }
            parts.push_back(name.substr(pos, s - pos));
            pos = s + 2;
        }
        // Traverse from root
        auto root = root_namespace(*elem);
        if (!root) return {};
        const auto module = root->get_name().without_root_prefix();
        if (parts.size() > module.size() && std::equal(
                module.parts().begin(), module.parts().end(), parts.begin())) {
            parts.erase(parts.begin(), parts.begin() + module.size());
        }
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
        if (auto holder = std::dynamic_pointer_cast<using_holder>(current)) {
            if (auto owner = current->ancestor<unit>()) {
                for (const auto& directive : holder->get_using_directives()) {
                    if (!directive.is_namespace() || directive.has_alias()) continue;
                    if (auto target = resolve_using_target(directive.target_name, *owner)) {
                        if (auto unions = std::dynamic_pointer_cast<const union_holder>(target)) {
                            if (auto un = unions->get_union(name)) return un;
                        }
                    }
                }
            }
        }
        if (is_definition_namespace_boundary(*current)) break;
    }
    return {};
}

std::shared_ptr<alias_definition>
scope_lookup::lookup_alias(std::shared_ptr<const element> elem, const k::name& name) {
    if (!elem || name.empty()) return {};

    if (name.has_root_prefix()) {
        auto root = root_namespace(*elem);
        if (!root) return {};
        auto relative = name.without_root_prefix();
        auto module = root->get_name().without_root_prefix();
        if (relative.size() > module.size() && relative.start_with(module)) {
            relative = relative.without_front(module.size());
        }
        auto scope = navigate_scope(std::static_pointer_cast<const element>(root),
                                    relative);
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
        if (name.size() == 1) {
            if (auto holder = std::dynamic_pointer_cast<const aggregate_holder>(current);
                holder && holder->get_aggregate(name.front())) return {};
            if (auto holder = std::dynamic_pointer_cast<const enum_holder>(current);
                holder && holder->get_enum(name.front())) return {};
            if (auto holder = std::dynamic_pointer_cast<const union_holder>(current);
                holder && holder->get_union(name.front())) return {};
        }
        if (auto holder = std::dynamic_pointer_cast<const using_holder>(current)) {
            auto root = root_namespace(*current);
            auto owner = root ? root->parent<unit>() : nullptr;
            if (owner) {
                for (const auto& directive : holder->get_using_directives()) {
                    if (!directive.is_namespace() || directive.has_alias()) continue;
                    auto target = resolve_using_target(directive.target_name, *owner);
                    auto scope = navigate_scope(target, name);
                    if (auto aliases = std::dynamic_pointer_cast<const alias_holder>(scope)) {
                        if (auto alias = aliases->get_alias(name.back())) return alias;
                    }
                }
            }
        }
        if (is_definition_namespace_boundary(*current)) break;
    }
    return {};
}

std::optional<scope_lookup::qualified_using_target>
scope_lookup::lookup_qualified_using(std::shared_ptr<const element> elem, const k::name& name) {
    if (!elem || name.size() < 2) return {};
    auto root = root_namespace(*elem);
    if (!root) return {};
    auto path = name.without_root_prefix();
    if (name.has_root_prefix()) {
        const auto module = root->get_name().without_root_prefix();
        if (!module.empty() && path.size() > module.size() && path.start_with(module))
            path = path.without_front(module.size());
    }
    if (path.size() < 2) return {};
    if (!name.has_root_prefix()) {
        if (is_enclosing_template_param_name(*elem, path.front())) return {};
        for (auto scope = elem; scope; scope = scope->parent<const element>()) {
            if (auto owner = std::dynamic_pointer_cast<const aggregate>(scope)) {
                auto definition = owner->get_tpl_definition();
                auto info = definition ? definition->get_tpl_info() : nullptr;
                if (info && std::any_of(info->params.begin(), info->params.end(),
                    [&](const auto& param) { return param.name == path.front(); })) return {};
            }
            if (auto fn = std::dynamic_pointer_cast<const function>(scope);
                fn && fn->has_tpl_instantiation_subst()
                && fn->get_tpl_instantiation_subst().contains(path.front())) return {};
        }
    }

    auto competing_using = [](const using_holder& holder, const std::string& part) {
        for (const auto& dir : holder.get_using_directives()) {
            if (dir.is_namespace() && !dir.has_alias()) {
                if (dir.ast_node) return true;
            } else if (dir.has_alias() ? *dir.alias_name == part
                                      : !dir.target_name.empty() && dir.target_name.back() == part) {
                return true;
            }
        }
        return false;
    };
    auto find_in = [&](const std::shared_ptr<const ns>& first)
        -> std::optional<qualified_using_target> {
        auto current = first;
        for (size_t i = 1; i + 1 < path.size(); ++i) {
            if (current->get_aggregate(path[i]) || current->get_alias(path[i])
                || current->get_enum(path[i]) || current->get_union(path[i])
                || competing_using(*current, path[i])) return {};
            current = current->get_child_namespace(path[i]);
            if (!current) return {};
        }
        const auto& last = path.back();
        if (current->get_aggregate(last) || current->get_alias(last)
            || current->get_enum(last) || current->get_union(last)
            || current->get_child_namespace(last) || current->get_variable(last)
            || current->get_function(last)) return {};
        const using_directive* match = nullptr;
        for (const auto& dir : current->get_using_directives()) {
            if (dir.is_namespace()) {
                if (!dir.has_alias() && dir.ast_node) return {};
                if (dir.has_alias() && *dir.alias_name == last) return {};
                continue;
            }
            if (dir.target_name.empty()) continue;
            const auto& exposed = dir.has_alias() ? *dir.alias_name : dir.target_name.back();
            if (exposed != last) continue;
            if (match) return {};
            match = &dir;
        }
        if (!match) return {};
        return qualified_using_target{current, match->target_name};
    };

    if (name.has_root_prefix()) {
        std::shared_ptr<const ns> root_scope = root;
        if (root_scope->get_aggregate(path.front()) || root_scope->get_alias(path.front())
            || root_scope->get_enum(path.front()) || root_scope->get_union(path.front())
            || competing_using(*root_scope, path.front())) return {};
        auto first = root_scope->get_child_namespace(path.front());
        return first ? find_in(first) : std::nullopt;
    }
    for (auto scope = elem; scope; scope = scope->parent<const element>()) {
        if (auto owner = std::dynamic_pointer_cast<const aggregate>(scope);
            owner && !owner->get_bases().empty()) return {};
        if (auto holder = std::dynamic_pointer_cast<const aggregate_holder>(scope);
            holder && holder->get_aggregate(path.front())) return {};
        if (auto holder = std::dynamic_pointer_cast<const alias_holder>(scope);
            holder && holder->get_alias(path.front())) return {};
        if (auto holder = std::dynamic_pointer_cast<const enum_holder>(scope);
            holder && holder->get_enum(path.front())) return {};
        if (auto holder = std::dynamic_pointer_cast<const union_holder>(scope);
            holder && holder->get_union(path.front())) return {};
        if (auto holder = std::dynamic_pointer_cast<const using_holder>(scope);
            holder && competing_using(*holder, path.front())) return {};
        if (auto current = std::dynamic_pointer_cast<const ns>(scope)) {
            if (auto first = current->get_child_namespace(path.front()))
                return find_in(first);
        }
        if (is_definition_namespace_boundary(*scope)) break;
    }
    return {};
}

std::shared_ptr<type>
scope_lookup::materialize_alias_type(const std::shared_ptr<alias_definition>& alias,
                                     const std::shared_ptr<context>& ctx,
                                     const std::function<std::shared_ptr<type>(const k::name&, const element&)>& resolve_by_name,
                                     bool& cycle,
                                     const std::function<std::shared_ptr<type>(const std::shared_ptr<type>&, const element&)>& resolve_chain,
                                     bool allow_unlaid_out_aggregate) {
    cycle = false;
    if (!alias) return {};
    const auto usable_target = [allow_unlaid_out_aggregate](const std::shared_ptr<type>& t) {
        return type::is_resolved(t)
            || (allow_unlaid_out_aggregate && std::dynamic_pointer_cast<struct_type>(t));
    };

    // A parameterised alias denotes no type by itself: 'Vec' alone is not a
    // type, only 'Vec<int>' is. Resolution happens at the use site, through
    // type_reference_resolver::try_resolve_alias_template().
    if (alias->is_template()) return {};

    if (alias->_resolved) {
        if (ctx && !alias->is_strong()) {
            alias->_target_type = ctx->collapse_callable_addresser(alias->_target_type);
        }
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
        if (!usable_target(target)) {
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
    if (!usable_target(target) && scope && resolve_chain) {
        if (auto resolved = resolve_chain(target, *scope)) {
            if (usable_target(resolved)) target = resolved;
        }
    }
    if (!usable_target(target) && ctx) {
        if (auto resolved = ctx->resolve_type(target)) {
            if (usable_target(resolved)) target = resolved;
        }
    }
    if (!usable_target(target) && scope && resolve_by_name) {
        if (auto unres = std::dynamic_pointer_cast<unresolved_type>(target)) {
            if (auto resolved = resolve_by_name(unres->type_id(), *scope)) {
                if (usable_target(resolved)) target = resolved;
            }
        }
    }

    alias->_resolving = false;

    if (!usable_target(target)) {
        return {};
    }

    if (ctx) target = ctx->collapse_callable_addresser(target);
    alias->_target_type = target;
    alias->_target_kind = alias_definition::target_kind_t::TYPE;

    if (alias->is_strong() && ctx) {
        alias->_alias_type = ctx->create_alias_type(alias, target);
    }
    alias->_resolved = true;
    return alias->get_declared_type();
}

namespace {

bool contains_template_parameter(const std::shared_ptr<type>& t,
                                 const element& scope,
                                 const std::shared_ptr<context>& ctx) {
    if (!t) return false;
    if (auto unres = std::dynamic_pointer_cast<unresolved_type>(t)) {
        if (unres->is_template_param_placeholder()
            || (unres->type_id().size() == 1 && !unres->type_id().has_root_prefix()
                && is_enclosing_template_param_name(scope, unres->type_id().back()))) {
            return true;
        }
        const auto& model_args = unres->get_model_template_args();
        const auto& ast_args = unres->get_ast_template_args();
        const auto count = unres->has_model_only_template_args() ? model_args.size() : ast_args.size();
        for (std::size_t i = 0; i < count; ++i) {
            auto arg = i < model_args.size() ? model_args[i] : nullptr;
            if (!arg && i < ast_args.size() && ast_args[i] && ast_args[i]->is_type() && ast_args[i]->type_arg) {
                arg = ctx->from_type_specifier(*ast_args[i]->type_arg);
            }
            if (contains_template_parameter(arg, scope, ctx)) return true;
        }
        return false;
    }
    if (auto callable = std::dynamic_pointer_cast<unresolved_callable_type>(t)) {
        if (contains_template_parameter(callable->get_return_type(), scope, ctx)) return true;
        for (const auto& param : callable->parameter_types()) {
            if (contains_template_parameter(param, scope, ctx)) return true;
        }
        for (const auto& thrown : callable->get_throws()) {
            if (contains_template_parameter(thrown, scope, ctx)) return true;
        }
    }
    if (auto callable = std::dynamic_pointer_cast<callable_type>(t)) {
        if (contains_template_parameter(callable->get_return_type(), scope, ctx)) return true;
        for (const auto& param : callable->get_parameter_types()) {
            if (contains_template_parameter(param, scope, ctx)) return true;
        }
        for (const auto& thrown : callable->get_throws()) {
            if (contains_template_parameter(thrown, scope, ctx)) return true;
        }
    }
    return contains_template_parameter(t->get_subtype(), scope, ctx);
}

} // anonymous namespace

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
    const bool use_model_args = unres->has_model_only_template_args() || unres->has_model_template_args();

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
            if (!arg_type && i < model_args.size() && unres->has_model_only_template_args()) {
                report_error(static_cast<unsigned int>(::k::diag::alias_diag::ERR_ALIAS_TEMPLATE_ARG_MISMATCH),
                             "Template argument for parameter '{}' of parameterised {} '{}' could not be resolved",
                             {param.name, kind, alias->get_short_name()});
                return {};
            }
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

        const auto declaration_scope = alias->parent<element>();
        const element& argument_scope = i < given || !declaration_scope
            ? context_elem : *declaration_scope;
        // Definition signatures can still contain symbolic caller arguments
        // (Consumer<const T&>, Completion<Result<T>>). Do not expand the alias
        // or bind those parameters to same-named concrete declarations yet.
        if (contains_template_parameter(arg_type, argument_scope, ctx)) return {};
        if (auto res_arg = resolve_chain(arg_type, argument_scope)) {
            arg_type = res_arg;
        }
        if (!arg_type || type::contains_unresolved(arg_type)) {
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
    auto declaration_scope = alias->parent<element>();
    if (auto res_sub = resolve_chain(substituted, declaration_scope ? *declaration_scope : context_elem)) {
        substituted = res_sub;
    }
    if (!substituted || (!type::is_resolved(substituted) && type::contains_unresolved(substituted))) {
        report_error(static_cast<unsigned int>(::k::diag::alias_diag::ERR_ALIAS_TEMPLATE_TARGET_UNRESOLVED),
                     "The renamed type '{}' of parameterised {} '{}' could not be resolved in its declaration scope",
                     {substituted ? substituted->to_string() : "<none>", kind, alias->get_fq_name()});
    }

    substituted = ctx->collapse_callable_addresser(substituted);

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
        } else if (auto fn = std::dynamic_pointer_cast<const function>(current)) {
            if (fn->is_template()) {
                if (auto* ti = fn->get_tpl_info()) {
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
