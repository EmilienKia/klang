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

        // Only check type parameters — value parameters are validated elsewhere
        if (!param.is_type_param() || !arg.is_type()) continue;

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

        // ── Base-type constraint ──────────────────────────────────────────
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

    return true;
}

} // namespace k::model


