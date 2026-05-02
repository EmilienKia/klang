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
#include "template_deduction.hpp"
#include "model_function.hpp"
#include "../parse/ast.hpp"
#include <unordered_map>
#include <unordered_set>
namespace k::model {
namespace {
std::string get_template_param_name(
    const std::shared_ptr<type>& t,
    const std::unordered_set<std::string>& type_param_names)
{
    if (!t) return "";
    auto unres = std::dynamic_pointer_cast<unresolved_type>(t);
    if (!unres) return "";
    const auto& tid = unres->type_id();
    if (tid.has_root_prefix() || tid.size() != 1) return "";
    const std::string& name = tid.front();
    if (type_param_names.count(name)) return name;
    return "";
}
bool deduce_from_types(
    const std::shared_ptr<type>& param_type,
    const std::shared_ptr<type>& arg_type,
    const std::unordered_set<std::string>& type_param_names,
    std::unordered_map<std::string, std::shared_ptr<type>>& deductions)
{
    if (!param_type || !arg_type) return false;
    // Check if param_type is directly a template parameter placeholder
    std::string param_name = get_template_param_name(param_type, type_param_names);
    if (!param_name.empty()) {
        // Direct deduction: T = arg_type
        auto it = deductions.find(param_name);
        if (it != deductions.end()) {
            // Consistency check: must be the same type
            if (it->second->to_string() != arg_type->to_string()) {
                return false; // Conflict
            }
        } else {
            deductions[param_name] = arg_type;
        }
        return true;
    }
    // Check wrapper types
    auto param_sub = param_type->get_subtype();
    if (!param_sub) {
        // Concrete type, no template param inside — no deduction needed
        return true;
    }
    auto arg_sub = arg_type->get_subtype();
    if (!arg_sub) {
        // param has wrapper but arg doesn't — check if inner is a template param
        std::string sub_name = get_template_param_name(param_sub, type_param_names);
        if (sub_name.empty()) return true; // Not a template param, skip
        return false; // Wrapper mismatch
    }
    // Both have subtypes — check same wrapper kind
    bool same_wrapper = false;
    if (type::is_pointer(param_type) && type::is_pointer(arg_type)) same_wrapper = true;
    else if (type::is_reference(param_type) && type::is_reference(arg_type)) same_wrapper = true;
    else if (type::is_owner(param_type) && type::is_owner(arg_type)) same_wrapper = true;
    else if (type::is_link(param_type) && type::is_link(arg_type)) same_wrapper = true;
    else if (type::is_view(param_type) && type::is_view(arg_type)) same_wrapper = true;
    else if (type::is_drain(param_type) && type::is_drain(arg_type)) same_wrapper = true;
    else if (type::is_const(param_type) && type::is_const(arg_type)) same_wrapper = true;
    else if (type::is_array(param_type) && type::is_array(arg_type)) same_wrapper = true;
    if (same_wrapper) {
        return deduce_from_types(param_sub, arg_sub, type_param_names, deductions);
    }
    // Wrapper mismatch
    std::string sub_param_name = get_template_param_name(param_sub, type_param_names);
    if (sub_param_name.empty()) {
        return true; // Not a template param — no deduction, no error
    }
    return false; // Cannot deduce due to wrapper mismatch
}
} // anonymous namespace
deduction_result deduce_template_arguments(
    const tpl_info& ti,
    const std::vector<std::shared_ptr<parameter>>& params,
    const std::vector<std::shared_ptr<type>>& arg_types)
{
    deduction_result result;
    // Collect type parameter names and pack parameter names
    std::unordered_set<std::string> type_param_names;
    std::unordered_set<std::string> pack_param_names;
    for (const auto& param : ti.params) {
        if (param.is_type_param()) {
            if (param.is_pack) {
                pack_param_names.insert(param.name);
            } else {
                type_param_names.insert(param.name);
            }
        }
    }
    // Deduction maps
    std::unordered_map<std::string, std::shared_ptr<type>> deductions;
    std::unordered_map<std::string, std::vector<std::shared_ptr<type>>> pack_deductions;
    // Process each function parameter against the call arguments
    size_t arg_idx = 0;
    for (size_t p_idx = 0; p_idx < params.size(); ++p_idx) {
        const auto& param = params[p_idx];
        if (param->is_pack_expansion()) {
            // Pack expansion: collect all remaining argument types
            const std::string& pack_name = param->pack_param_name();
            std::vector<std::shared_ptr<type>> pack_types;
            while (arg_idx < arg_types.size()) {
                pack_types.push_back(arg_types[arg_idx]);
                ++arg_idx;
            }
            pack_deductions[pack_name] = std::move(pack_types);
            break; // Pack consumes all remaining args
        }
        if (arg_idx >= arg_types.size()) {
            // No more arguments — remaining params must have defaults
            break;
        }
        auto param_type = param->get_type();
        auto arg_type = arg_types[arg_idx];
        if (!deduce_from_types(param_type, arg_type, type_param_names, deductions)) {
            result.success = false;
            result.failure_reason = "Type conflict or mismatch deducing parameter '" +
                param->get_short_name() + "' at position " + std::to_string(p_idx);
            return result;
        }
        ++arg_idx;
    }
    // Build the final template arguments vector
    std::vector<template_argument> deduced_args;
    for (const auto& tpl_param : ti.params) {
        if (tpl_param.is_pack) {
            auto it = pack_deductions.find(tpl_param.name);
            if (it != pack_deductions.end()) {
                deduced_args.push_back(template_argument::make_pack(it->second));
            } else {
                // Empty pack (no arguments consumed)
                deduced_args.push_back(template_argument::make_pack({}));
            }
        } else if (tpl_param.is_type_param()) {
            auto it = deductions.find(tpl_param.name);
            if (it != deductions.end()) {
                deduced_args.push_back(template_argument::make_type(it->second));
            } else if (tpl_param.default_type) {
                deduced_args.push_back(template_argument::make_type(tpl_param.default_type));
            } else {
                result.success = false;
                result.failure_reason = "Cannot deduce template parameter '" + tpl_param.name +
                    "': not referenced in any function parameter";
                return result;
            }
        } else {
            // Value params: cannot be deduced, must have default
            if (tpl_param.default_value.has_value()) {
                deduced_args.push_back(template_argument::make_value(*tpl_param.default_value));
            } else {
                result.success = false;
                result.failure_reason = "Cannot deduce value template parameter '" + tpl_param.name + "'";
                return result;
            }
        }
    }
    result.success = true;
    result.deduced_args = std::move(deduced_args);
    return result;
}
} // namespace k::model
