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

bool validate_template_arg_constraints(
    const std::vector<template_param_descriptor>& params,
    const std::vector<template_argument>& args,
    size_t& error_index,
    std::string& error_kind)
{
    error_index = static_cast<size_t>(-1);
    error_kind.clear();

    size_t count = std::min(params.size(), args.size());
    for (size_t i = 0; i < count; ++i) {
        const auto& param = params[i];
        const auto& arg = args[i];

        // ── Type parameter validation ─────────────────────────────────────
        if (param.is_type_param() && arg.is_type()) {
            auto arg_type = arg.type_arg;
            if (!arg_type) continue;

            // ── Kind filter: STRUCT / CLASS / INTERFACE ────────────────────────
            if (param.kind != template_param_kind::TYPENAME) {
                // The argument must be a struct_type to check the aggregate kind
                auto st = std::dynamic_pointer_cast<struct_type>(type::remove_const(arg_type));
                if (!st) {
                    error_index = i;
                    error_kind = "not_aggregate";
                    return false;
                }
                auto agg = st->get_struct();
                if (!agg) {
                    error_index = i;
                    error_kind = "not_aggregate";
                    return false;
                }

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
                if (!kind_ok) {
                    error_index = i;
                    error_kind = "kind";
                    return false;
                }
            }

            // ── Base-type constraint ──────────────────────────────────────
            if (param.constraint_type) {
                auto constraint_st = std::dynamic_pointer_cast<struct_type>(
                    type::remove_const(param.constraint_type));
                if (constraint_st) {
                    auto constraint_agg = constraint_st->get_struct();

                    auto arg_st = std::dynamic_pointer_cast<struct_type>(
                        type::remove_const(arg_type));
                    if (!arg_st) {
                        error_index = i;
                        error_kind = "constraint";
                        return false;
                    }
                    auto arg_agg = arg_st->get_struct();
                    if (!arg_agg) {
                        error_index = i;
                        error_kind = "constraint";
                        return false;
                    }

                    // Must be the same aggregate or derive from it
                    if (constraint_agg && arg_agg != constraint_agg &&
                        !arg_agg->is_derived_from(constraint_agg)) {
                        error_index = i;
                        error_kind = "constraint";
                        return false;
                    }
                }
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
                        if (!arg.value_arg) {
                            error_index = i;
                            error_kind = "type_mismatch";
                            return false;
                        }
                        
                        // Try to extract aggregate_value from the variant
                        auto agg_val = std::get_if<std::shared_ptr<aggregate_value>>(
                            &(*arg.value_arg));
                        
                        if (!agg_val || !*agg_val) {
                            error_index = i;
                            error_kind = "type_mismatch";
                            return false;
                        }
                        
                        // Verify that the aggregate value's type matches the expected type
                        auto arg_agg = (*agg_val)->get_type();
                        if (arg_agg != expected_agg) {
                            error_index = i;
                            error_kind = "constraint";
                            return false;
                        }
                    }
                    // If value_type is a struct_type but doesn't resolve to an aggregate,
                    // treat it like a type mismatch (this shouldn't happen in practice)
                }
                // For primitive value_type (int, double, etc.), validation would happen
                // elsewhere (type narrowing, range checks, etc.)
            }
        }
    }

    return true;
}

} // namespace k::model

