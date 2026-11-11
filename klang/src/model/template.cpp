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

#include "template.hpp"
#include "model.hpp"
#include "type.hpp"
#include "aggregate_value.hpp"
#include "../parse/ast.hpp"
#include "../errors.hpp"

namespace k::model {

// ── Helper: determine the actual kind string of a type argument ─────────
static std::string actual_kind_str(const std::shared_ptr<type>& t) {
    auto st = std::dynamic_pointer_cast<struct_type>(type::remove_const(t));
    if (!st) return "non-aggregate";
    auto agg = st->get_struct();
    if (!agg) return "non-aggregate";
    if (dynamic_cast<interface*>(agg.get())) return "interface";
    if (agg->is_class()) return "class";
    return "struct";
}

std::pair<unsigned int, std::string> format_constraint_error(
    const std::string& template_name,
    const std::vector<template_param_descriptor>& params,
    const std::vector<template_argument>& args,
    size_t error_index,
    const std::string& error_kind)
{
    const auto& param = params[error_index];
    const auto& arg = args[error_index];
    std::string arg_type_str = arg.type_arg ? arg.type_arg->to_string() : "?";

    if (error_kind == "generic_owner_arg" || error_kind == "generic_drain_arg") {
        const bool is_owner = error_kind == "generic_owner_arg";
        return {
            static_cast<unsigned int>(k::diag::generic_diag::ERR_GENERIC_ARG_OWNER_DRAIN_NOT_ALLOWED),
            "generic '" + template_name + "': argument " + std::to_string(error_index + 1)
                + " ('" + arg_type_str + "') for parameter '" + param.name
                + "' is " + (is_owner ? "an owner ('!')" : "a drain ('#')")
                + " type; owners and drains are not supported as generic type arguments"
        };
    }
    if (error_kind == "not_aggregate") {
        return {
            static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_ARG_NOT_AGGREGATE),
            "template '" + template_name + "': argument " + std::to_string(error_index + 1)
                + " ('" + arg_type_str + "') for parameter '" + param.name
                + "' must be an aggregate type (" + to_string(param.kind)
                + "), but is not"
        };
    } else if (error_kind == "kind") {
        return {
            static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_ARG_WRONG_KIND),
            "template '" + template_name + "': argument " + std::to_string(error_index + 1)
                + " ('" + arg_type_str + "') for parameter '" + param.name
                + "' must be a " + to_string(param.kind)
                + ", but is a " + actual_kind_str(arg.type_arg)
        };
    } else if (error_kind == "constraint") {
        std::string constraint_str = param.constraint_type ? param.constraint_type->to_string() : "?";
        return {
            static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_ARG_CONSTRAINT_VIOLATED),
            "template '" + template_name + "': argument " + std::to_string(error_index + 1)
                + " ('" + arg_type_str + "') for parameter '" + param.name
                + "' does not derive from constraint type '" + constraint_str + "'"
        };
    } else if (error_kind == "unresolved_bound") {
        std::string constraint_str = param.constraint_type ? param.constraint_type->to_string() : "?";
        return {
            static_cast<unsigned int>(k::diag::type_diag::ERR_UNRESOLVED_TYPE_EXPR),
            "Template '" + template_name + "' has unresolved bound '" + constraint_str + "'"
        };
    } else if (error_kind == "type_mismatch") {
        std::string expected = param.value_type ? param.value_type->to_string() : "?";
        return {
            static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_VALUE_ARG_TYPE_MISMATCH),
            "template '" + template_name + "': argument " + std::to_string(error_index + 1)
                + " for value parameter '" + param.name
                + "' is not compatible with expected type '" + expected + "'"
        };
    }
    // Fallback
    return {
        static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_ARG_WRONG_KIND),
        "template '" + template_name + "': argument " + std::to_string(error_index + 1)
            + " for parameter '" + param.name + "' violates constraint"
    };
}

namespace {

bool is_unproven_bound_reason(constraint_analysis_reason reason) {
    return reason == constraint_analysis_reason::unresolved_bound
        || reason == constraint_analysis_reason::bound_aggregate_missing
        || reason == constraint_analysis_reason::unsupported_bound;
}

// Fail closed: a bound that could not be resolved to an aggregate is never satisfied.
bool to_legacy_result(const template_constraint_verdict& verdict,
                      size_t& error_index, std::string& error_kind) {
    error_index = verdict.error_index;
    error_kind = verdict.error_kind;
    if (verdict.state == constraint_analysis_state::invalid) return false;
    if (verdict.state == constraint_analysis_state::deferred
        && is_unproven_bound_reason(verdict.reason)) {
        error_kind = "unresolved_bound";
        return false;
    }
    error_index = static_cast<size_t>(-1);
    error_kind.clear();
    return true;
}

} // namespace

bool template_constraint_verdict::is_unproven_bound() const {
    return state == constraint_analysis_state::deferred && is_unproven_bound_reason(reason);
}

bool is_independent_written_bound(
    const std::vector<template_param_descriptor>& params, size_t index, k::name* written_name)
{
    if (index >= params.size()) return false;
    const auto& param = params[index];
    if (!param.is_type_param() || param.is_pack || !param.constraint_type) return false;
    auto written = std::dynamic_pointer_cast<unresolved_type>(param.constraint_type);
    if (!written || written->source_binder() || written->is_template_param_placeholder()
        || written->has_template_args() || written->has_model_template_args()) return false;
    const auto& id = written->type_id();
    if (id.empty() || id.front().empty() || id.front().find("::") != std::string::npos)
        return false;
    if (param.constraint_source_type
        && (param.constraint_source_type->has_unknown_bindings()
            || param.constraint_source_type->has_unmodelled_arguments()
            || param.constraint_source_type->lexical_bindings().contains(id.front())))
        return false;
    for (const auto& other : params)
        if (other.name == id.front()) return false;
    if (written_name) *written_name = id;
    return true;
}

std::vector<template_param_descriptor> bind_same_list_dependent_bounds(
    const tpl_info& ti, const std::vector<template_argument>& args)
{
    auto params = ti.params;
    if (ti.is_generic || !ti.binder_scope) return params;
    for (size_t i = 0; i < params.size() && i < args.size(); ++i) {
        auto& param = params[i];
        const auto& source = param.constraint_source_type;
        if (param.is_pack || !param.is_type_param() || !param.constraint_type || !source
            || type::is_resolved(param.constraint_type)
            || source->has_unknown_bindings() || source->has_unmodelled_arguments()) continue;
        auto written = std::dynamic_pointer_cast<unresolved_type>(source->get_type());
        if (!written || !written->source_binder() || written->has_template_args()
            || written->has_model_template_args()) continue;
        const auto& binder = *written->source_binder();
        if (binder.scope != ti.binder_scope || binder.ordinal >= i
            || !ti.params[binder.ordinal].is_type_param() || ti.params[binder.ordinal].is_pack)
            continue;
        const auto& id = written->type_id();
        if (id.has_root_prefix() || id.size() != 1 || id.front() != ti.params[binder.ordinal].name)
            continue;
        const auto& bound_arg = args[binder.ordinal];
        if (!bound_arg.is_type()) continue;
        auto bound = std::dynamic_pointer_cast<struct_type>(type::remove_const(bound_arg.type_arg));
        if (bound && bound->get_struct()) param.constraint_type = bound;
    }
    return params;
}

template_constraint_verdict evaluate_template_arg_constraints(
    const tpl_info& ti,
    const std::vector<template_argument>& args)
{
    if (ti.is_generic) {
        const size_t count = std::min(ti.params.size(), args.size());
        for (size_t i = 0; i < count; ++i) {
            if (!ti.params[i].is_type_param() || !args[i].is_type() || !args[i].type_arg) continue;
            auto arg_type = type::remove_const(args[i].type_arg);
            const bool is_owner = type::is_owner(arg_type);
            if (is_owner || type::is_drain(arg_type)) {
                template_constraint_verdict verdict;
                verdict.state = constraint_analysis_state::invalid;
                verdict.reason = is_owner ? constraint_analysis_reason::generic_owner_arg
                                          : constraint_analysis_reason::generic_drain_arg;
                verdict.error_index = i;
                verdict.error_kind = is_owner ? "generic_owner_arg" : "generic_drain_arg";
                return verdict;
            }
        }
    }
    return evaluate_template_arg_constraints(ti.params, args);
}

bool validate_template_arg_constraints(
    const tpl_info& ti,
    const std::vector<template_argument>& args,
    size_t& error_index,
    std::string& error_kind)
{
    return to_legacy_result(evaluate_template_arg_constraints(ti, args), error_index, error_kind);
}

bool validate_template_arg_constraints(
    const std::vector<template_param_descriptor>& params,
    const std::vector<template_argument>& args,
    size_t& error_index,
    std::string& error_kind)
{
    return to_legacy_result(evaluate_template_arg_constraints(params, args), error_index, error_kind);
}

template_constraint_verdict evaluate_template_arg_constraints(
    const std::vector<template_param_descriptor>& params,
    const std::vector<template_argument>& args)
{
    using state = constraint_analysis_state;
    using reason = constraint_analysis_reason;
    template_constraint_verdict verdict;
    auto reject = [&](size_t i, reason r, const char* kind) {
        verdict.state = state::invalid;
        verdict.reason = r;
        verdict.error_index = i;
        verdict.error_kind = kind;
        return verdict;
    };
    // Report the first deferral, except that an undecided bound outranks other
    // reasons: callers must be able to see it. A later invalid result still wins.
    auto defer = [&](size_t i, reason r) {
        if (verdict.state == state::invalid) return;
        if (verdict.state == state::deferred
            && (is_unproven_bound_reason(verdict.reason) || !is_unproven_bound_reason(r))) return;
        verdict.state = state::deferred;
        verdict.reason = r;
        verdict.error_index = i;
    };

    size_t count = std::min(params.size(), args.size());
    for (size_t i = 0; i < count; ++i) {
        const auto& param = params[i];
        const auto& arg = args[i];

        // ── Type parameter validation ─────────────────────────────────────
        if (param.is_type_param() && arg.is_type()) {
            auto arg_type = arg.type_arg;
            if (!arg_type) continue;

            // An erased generic parameter (a generic body instantiating another generic
            // with its own erased parameter) has no kind or base type to check yet.
            if (type::is_erased_param(arg_type)) {
                defer(i, reason::erased_argument);
                continue;
            }

            // ── Kind filter: STRUCT / CLASS / INTERFACE ────────────────────────
            if (param.kind != template_param_kind::TYPENAME) {
                // The argument must be a struct_type to check the aggregate kind
                auto st = std::dynamic_pointer_cast<struct_type>(type::remove_const(arg_type));
                if (!st) return reject(i, reason::not_aggregate, "not_aggregate");
                auto agg = st->get_struct();
                if (!agg) return reject(i, reason::not_aggregate, "not_aggregate");

                bool kind_ok = false;
                switch (param.kind) {
                    case template_param_kind::STRUCT:
                        // Must be a structure (not klass, not interface)
                        kind_ok = !agg->is_class() && (dynamic_cast<structure*>(agg.get()) != nullptr);
                        break;
                    case template_param_kind::CLASS:
                        // Must be a klass (not interface)
                        kind_ok = agg->is_class() && (dynamic_cast<interface*>(agg.get()) == nullptr);
                        break;
                    case template_param_kind::INTERFACE:
                        kind_ok = (dynamic_cast<interface*>(agg.get()) != nullptr);
                        break;
                    default:
                        kind_ok = true;
                        break;
                }
                if (!kind_ok) return reject(i, reason::kind, "kind");
            }

            // ── Base-type constraint ──────────────────────────────────────
            if (param.constraint_type) {
                auto bound_type = type::remove_const(param.constraint_type);
                if (std::dynamic_pointer_cast<alias_type>(bound_type))
                    bound_type = type::remove_const(type::canonical(bound_type));
                if (!bound_type || std::dynamic_pointer_cast<unresolved_type>(bound_type)) {
                    defer(i, reason::unresolved_bound);
                    continue;
                }
                if (type::is_erased_param(bound_type)) {
                    defer(i, reason::erased_bound);
                    continue;
                }
                auto constraint_st = std::dynamic_pointer_cast<struct_type>(bound_type);
                if (!constraint_st) {
                    defer(i, type::is_resolved(bound_type) ? reason::unsupported_bound
                                                           : reason::unresolved_bound);
                    continue;
                }
                auto constraint_agg = constraint_st->get_struct();
                if (!constraint_agg) {
                    defer(i, reason::bound_aggregate_missing);
                    continue;
                }

                auto arg_st = std::dynamic_pointer_cast<struct_type>(
                    type::remove_const(arg_type));
                if (!arg_st) return reject(i, reason::constraint, "constraint");
                auto arg_agg = arg_st->get_struct();
                if (!arg_agg) return reject(i, reason::constraint, "constraint");

                // Must be the same aggregate or derive from it
                if (arg_agg != constraint_agg && !arg_agg->is_derived_from(constraint_agg))
                    return reject(i, reason::constraint, "constraint");
            }
        }

        // ── Value parameter validation ────────────────────────────────────
        if (param.is_value_param() && arg.is_value()) {
            // For value parameters, validate that the argument type matches
            // the declared value_type. When value_type is a struct_type,
            // the argument must be an aggregate_value of that type.
            
            if (param.value_type) {
                auto expected_struct_type = std::dynamic_pointer_cast<struct_type>(
                    type::remove_const(param.value_type));
                
                if (expected_struct_type) {
                    auto expected_agg = expected_struct_type->get_struct();
                    if (expected_agg) {
                        // This parameter expects an aggregate value.
                        // Check that the argument contains an aggregate_value.
                        if (!arg.value_arg)
                            return reject(i, reason::value_type_mismatch, "type_mismatch");
                        
                        // Try to extract aggregate_value from the variant
                        auto agg_val = std::get_if<std::shared_ptr<aggregate_value>>(
                            &(*arg.value_arg));
                        
                        if (!agg_val || !*agg_val)
                            return reject(i, reason::value_type_mismatch, "type_mismatch");
                        
                        // Verify that the aggregate value's type matches the expected type
                        auto arg_agg = (*agg_val)->get_type();
                        if (arg_agg != expected_agg)
                            return reject(i, reason::value_type_mismatch, "constraint");
                    }
                    // If value_type is a struct_type but doesn't resolve to an aggregate,
                    // treat it like a type mismatch (this shouldn't happen in practice)
                }
                // For primitive value_type (int, double, etc.), validation would happen
                // elsewhere (type narrowing, range checks, etc.)
            }
        }
    }

    return verdict;
}

} // namespace k::model

