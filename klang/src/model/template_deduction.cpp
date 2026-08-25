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
#include "model_aggregate.hpp"
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
    if (unres->has_template_args()) return "";
    const auto& tid = unres->type_id();
    if (tid.has_root_prefix() || tid.size() != 1) return "";
    const std::string& name = tid.front();
    if (type_param_names.count(name)) return name;
    return "";
}

// Forward declarations
bool deduce_from_types(
    const std::shared_ptr<type>& param_type,
    const std::shared_ptr<type>& arg_type,
    const std::unordered_set<std::string>& type_param_names,
    const std::unordered_set<std::string>& value_param_names,
    std::unordered_map<std::string, std::shared_ptr<type>>& type_deductions,
    std::unordered_map<std::string, k::value_type>& value_deductions);

bool match_ast_type_spec(
    const k::parse::ast::type_specifier* spec,
    const std::shared_ptr<type>& arg_type,
    const std::unordered_set<std::string>& type_param_names,
    const std::unordered_set<std::string>& value_param_names,
    std::unordered_map<std::string, std::shared_ptr<type>>& type_deductions,
    std::unordered_map<std::string, k::value_type>& value_deductions);

bool match_ast_template_arg(
    const std::shared_ptr<k::parse::ast::template_arg>& ast_arg,
    const template_argument& concrete_arg,
    const std::unordered_set<std::string>& type_param_names,
    const std::unordered_set<std::string>& value_param_names,
    std::unordered_map<std::string, std::shared_ptr<type>>& type_deductions,
    std::unordered_map<std::string, k::value_type>& value_deductions)
{
    if (!ast_arg) return false;

    if (ast_arg->is_type() && ast_arg->type_arg) {
        // In K, a value parameter identifier inside '<...>' may parse as an identified_type_specifier.
        if (auto id_spec = dynamic_cast<const k::parse::ast::identified_type_specifier*>(ast_arg->type_arg.get())) {
            if (!id_spec->has_explicit_template_args && id_spec->template_args.empty() &&
                id_spec->name.size() == 1 && !id_spec->name.has_root_prefix()) {
                std::string name{id_spec->name.names[0].content};
                if (value_param_names.count(name)) {
                    if (concrete_arg.value_arg.has_value()) {
                        auto it = value_deductions.find(name);
                        if (it != value_deductions.end()) {
                            if (it->second != *concrete_arg.value_arg) return false;
                        } else {
                            value_deductions[name] = *concrete_arg.value_arg;
                        }
                        return true;
                    }
                }
            }
        }
        if (concrete_arg.is_type() && concrete_arg.type_arg) {
            return match_ast_type_spec(
                ast_arg->type_arg.get(), concrete_arg.type_arg,
                type_param_names, value_param_names, type_deductions, value_deductions);
        }
        return false;
    }

    if (ast_arg->is_value() && ast_arg->value_arg) {
        if (auto ident_expr = dynamic_cast<const k::parse::ast::identifier_expr*>(ast_arg->value_arg.get())) {
            if (ident_expr->qident.size() == 1 && !ident_expr->qident.has_root_prefix()) {
                std::string name{ident_expr->qident.names[0].content};
                if (value_param_names.count(name)) {
                    if (concrete_arg.value_arg.has_value()) {
                        auto it = value_deductions.find(name);
                        if (it != value_deductions.end()) {
                            if (it->second != *concrete_arg.value_arg) return false;
                        } else {
                            value_deductions[name] = *concrete_arg.value_arg;
                        }
                        return true;
                    }
                }
            }
        }
        if (concrete_arg.value_arg.has_value()) {
            return true;
        }
    }

    return false;
}

bool match_ast_type_spec(
    const k::parse::ast::type_specifier* spec,
    const std::shared_ptr<type>& arg_type,
    const std::unordered_set<std::string>& type_param_names,
    const std::unordered_set<std::string>& value_param_names,
    std::unordered_map<std::string, std::shared_ptr<type>>& type_deductions,
    std::unordered_map<std::string, k::value_type>& value_deductions)
{
    if (!spec || !arg_type) return false;

    if (auto ct = dynamic_cast<const k::parse::ast::const_type_specifier*>(spec)) {
        return match_ast_type_spec(
            ct->subtype.get(), type::remove_const(arg_type),
            type_param_names, value_param_names, type_deductions, value_deductions);
    }

    if (auto ptr = dynamic_cast<const k::parse::ast::pointer_type_specifier*>(spec)) {
        if (ptr->pointer_type == k::lex::operator_::STAR && type::is_any_indirection(arg_type)) {
            return match_ast_type_spec(
                ptr->subtype.get(), arg_type->get_subtype(),
                type_param_names, value_param_names, type_deductions, value_deductions);
        }
        if (ptr->pointer_type == k::lex::operator_::AMPERSAND) {
            auto clean_arg = arg_type;
            if (type::is_any_indirection(clean_arg)) clean_arg = clean_arg->get_subtype();
            return match_ast_type_spec(
                ptr->subtype.get(), clean_arg,
                type_param_names, value_param_names, type_deductions, value_deductions);
        }
        if (ptr->pointer_type == k::lex::operator_::PLUS && type::is_link(arg_type)) {
            return match_ast_type_spec(
                ptr->subtype.get(), arg_type->get_subtype(),
                type_param_names, value_param_names, type_deductions, value_deductions);
        }
        if (ptr->pointer_type == k::lex::operator_::QUESTION_MARK && type::is_any_indirection(arg_type)) {
            return match_ast_type_spec(
                ptr->subtype.get(), arg_type->get_subtype(),
                type_param_names, value_param_names, type_deductions, value_deductions);
        }
        if (ptr->pointer_type == k::lex::operator_::HASH && (type::is_drain(arg_type) || type::is_owner(arg_type))) {
            return match_ast_type_spec(
                ptr->subtype.get(), arg_type->get_subtype(),
                type_param_names, value_param_names, type_deductions, value_deductions);
        }
        return false;
    }

    if (auto own = dynamic_cast<const k::parse::ast::owner_type_specifier*>(spec)) {
        if (type::is_owner(arg_type)) {
            return match_ast_type_spec(
                own->subtype.get(), arg_type->get_subtype(),
                type_param_names, value_param_names, type_deductions, value_deductions);
        }
        return false;
    }

    if (auto arr = dynamic_cast<const k::parse::ast::array_type_specifier*>(spec)) {
        if (type::is_array(arg_type)) {
            return match_ast_type_spec(
                arr->subtype.get(), arg_type->get_subtype(),
                type_param_names, value_param_names, type_deductions, value_deductions);
        }
        return false;
    }

    if (auto id_spec = dynamic_cast<const k::parse::ast::identified_type_specifier*>(spec)) {
        // Direct template type parameter placeholder (e.g. "T")
        if (!id_spec->has_explicit_template_args && id_spec->template_args.empty() &&
            id_spec->name.size() == 1 && !id_spec->name.has_root_prefix()) {
            std::string name{id_spec->name.names[0].content};
            if (type_param_names.count(name)) {
                auto clean_arg = arg_type;
                if (type::is_reference(clean_arg) || type::is_drain(clean_arg)) {
                    clean_arg = clean_arg->get_subtype();
                }
                auto it = type_deductions.find(name);
                if (it != type_deductions.end()) {
                    if (!type::are_equal(it->second, clean_arg) &&
                        !type::are_layout_equal(it->second, clean_arg) &&
                        it->second->to_string() != clean_arg->to_string()) {
                        return false; // Conflict
                    }
                } else {
                    type_deductions[name] = clean_arg;
                }
                return true;
            }
        }

        // Composite template instance pattern (e.g. "Vector<T>", "Map<K, V>")
        if (id_spec->has_explicit_template_args || !id_spec->template_args.empty()) {
            auto bare = type::canonical(type::remove_const(arg_type));
            if (type::is_reference(bare) || type::is_drain(bare)) {
                bare = bare->get_subtype();
            }
            bare = type::remove_const(bare);

            if (auto st = std::dynamic_pointer_cast<struct_type>(bare)) {
                if (auto agg = st->get_struct()) {
                    if (agg->has_tpl_args()) {
                        if (std::string(id_spec->name.names.back().content) != agg->get_tpl_base_name()) {
                            return false;
                        }
                        const auto& concrete_args = agg->get_tpl_args();
                        if (id_spec->template_args.size() != concrete_args.size()) {
                            return false;
                        }
                        for (size_t i = 0; i < id_spec->template_args.size(); ++i) {
                            if (!match_ast_template_arg(
                                    id_spec->template_args[i], concrete_args[i],
                                    type_param_names, value_param_names,
                                    type_deductions, value_deductions)) {
                                return false;
                            }
                        }
                        return true;
                    }
                }
            }
            return false;
        }

        // Concrete nominal type
        return true;
    }

    if (auto kw = dynamic_cast<const k::parse::ast::keyword_type_specifier*>(spec)) {
        return true;
    }

    return false;
}

bool deduce_from_types(
    const std::shared_ptr<type>& param_type,
    const std::shared_ptr<type>& arg_type,
    const std::unordered_set<std::string>& type_param_names,
    const std::unordered_set<std::string>& value_param_names,
    std::unordered_map<std::string, std::shared_ptr<type>>& type_deductions,
    std::unordered_map<std::string, k::value_type>& value_deductions)
{
    if (!param_type || !arg_type) return false;

    // Check if param_type is directly a template parameter placeholder (e.g. "T")
    std::string param_name = get_template_param_name(param_type, type_param_names);
    if (!param_name.empty()) {
        auto clean_arg = arg_type;
        // Strip top-level reference/drain for by-value parameters
        if (type::is_reference(clean_arg) || type::is_drain(clean_arg)) {
            clean_arg = clean_arg->get_subtype();
        }
        auto it = type_deductions.find(param_name);
        if (it != type_deductions.end()) {
            if (!type::are_equal(it->second, clean_arg) &&
                !type::are_layout_equal(it->second, clean_arg) &&
                it->second->to_string() != clean_arg->to_string()) {
                return false; // Conflict
            }
        } else {
            type_deductions[param_name] = clean_arg;
        }
        return true;
    }

    // Check if param_type is a composite template type (e.g. Vector<T>, Map<K, V>)
    if (auto unres_param = std::dynamic_pointer_cast<unresolved_type>(param_type)) {
        if (unres_param->has_template_args()) {
            auto bare_arg = type::canonical(type::remove_const(arg_type));
            if (type::is_reference(bare_arg) || type::is_drain(bare_arg)) {
                bare_arg = bare_arg->get_subtype();
            }
            bare_arg = type::remove_const(bare_arg);

            if (auto st = std::dynamic_pointer_cast<struct_type>(bare_arg)) {
                if (auto agg = st->get_struct()) {
                    if (agg->has_tpl_args()) {
                        if (unres_param->type_id().back() != agg->get_tpl_base_name()) {
                            return false;
                        }
                        const auto& ast_args = unres_param->get_ast_template_args();
                        const auto& concrete_args = agg->get_tpl_args();
                        if (ast_args.size() != concrete_args.size()) {
                            return false;
                        }
                        for (size_t i = 0; i < ast_args.size(); ++i) {
                            if (!match_ast_template_arg(
                                    ast_args[i], concrete_args[i],
                                    type_param_names, value_param_names,
                                    type_deductions, value_deductions)) {
                                return false;
                            }
                        }
                        return true;
                    }
                }
            }
            return false;
        }
    }

    // Check callable types (*(T):R, +(T, U):void)
    auto bare_callable_arg = type::canonical(type::remove_const(arg_type));
    if (type::is_reference(bare_callable_arg) || type::is_drain(bare_callable_arg)) {
        bare_callable_arg = bare_callable_arg->get_subtype();
    }
    bare_callable_arg = type::remove_const(bare_callable_arg);

    if (auto uct_param = std::dynamic_pointer_cast<unresolved_callable_type>(type::remove_const(param_type))) {
        if (auto ct_arg = std::dynamic_pointer_cast<callable_type>(bare_callable_arg)) {
            if (uct_param->get_return_type() && ct_arg->get_return_type()) {
                if (!deduce_from_types(
                        uct_param->get_return_type(), ct_arg->get_return_type(),
                        type_param_names, value_param_names,
                        type_deductions, value_deductions)) {
                    return false;
                }
            }
            const auto& p_params = uct_param->parameter_types();
            const auto& a_params = ct_arg->get_parameter_types();
            if (p_params.size() != a_params.size()) return false;
            for (size_t i = 0; i < p_params.size(); ++i) {
                if (!deduce_from_types(
                        p_params[i], a_params[i],
                        type_param_names, value_param_names,
                        type_deductions, value_deductions)) {
                    return false;
                }
            }
            return true;
        }
        if (auto uct_arg = std::dynamic_pointer_cast<unresolved_callable_type>(bare_callable_arg)) {
            if (uct_param->get_return_type() && uct_arg->get_return_type()) {
                if (!deduce_from_types(
                        uct_param->get_return_type(), uct_arg->get_return_type(),
                        type_param_names, value_param_names,
                        type_deductions, value_deductions)) {
                    return false;
                }
            }
            const auto& p_params = uct_param->parameter_types();
            const auto& a_params = uct_arg->parameter_types();
            if (p_params.size() != a_params.size()) return false;
            for (size_t i = 0; i < p_params.size(); ++i) {
                if (!deduce_from_types(
                        p_params[i], a_params[i],
                        type_param_names, value_param_names,
                        type_deductions, value_deductions)) {
                    return false;
                }
            }
            return true;
        }
    }

    if (type::is_callable(param_type)) {
        auto ct_param = std::dynamic_pointer_cast<callable_type>(type::remove_const(param_type));
        auto ct_arg = std::dynamic_pointer_cast<callable_type>(bare_callable_arg);
        if (ct_param && ct_arg) {
            if (ct_param->get_return_type() && ct_arg->get_return_type()) {
                if (!deduce_from_types(
                        ct_param->get_return_type(), ct_arg->get_return_type(),
                        type_param_names, value_param_names,
                        type_deductions, value_deductions)) {
                    return false;
                }
            }
            const auto& p_params = ct_param->get_parameter_types();
            const auto& a_params = ct_arg->get_parameter_types();
            if (p_params.size() != a_params.size()) return false;
            for (size_t i = 0; i < p_params.size(); ++i) {
                if (!deduce_from_types(
                        p_params[i], a_params[i],
                        type_param_names, value_param_names,
                        type_deductions, value_deductions)) {
                    return false;
                }
            }
            return true;
        }
    }

    // Check sized array and array types
    auto bare_array_param = type::canonical(type::remove_const(param_type));
    if (type::is_reference(bare_array_param) || type::is_drain(bare_array_param)) {
        bare_array_param = bare_array_param->get_subtype();
    }
    bare_array_param = type::remove_const(bare_array_param);

    auto bare_array_arg = type::canonical(type::remove_const(arg_type));
    if (type::is_reference(bare_array_arg) || type::is_drain(bare_array_arg)) {
        bare_array_arg = bare_array_arg->get_subtype();
    }
    bare_array_arg = type::remove_const(bare_array_arg);

    if (type::is_sized_array(bare_array_param) && type::is_sized_array(bare_array_arg)) {
        auto sa_param = std::dynamic_pointer_cast<sized_array_type>(bare_array_param);
        auto sa_arg = std::dynamic_pointer_cast<sized_array_type>(bare_array_arg);
        if (sa_param && sa_arg) {
            if (sa_param->get_size() != sa_arg->get_size()) return false;
            return deduce_from_types(
                sa_param->get_subtype(), sa_arg->get_subtype(),
                type_param_names, value_param_names,
                type_deductions, value_deductions);
        }
    }

    if (type::is_array(bare_array_param) && type::is_array(bare_array_arg)) {
        auto a_param = std::dynamic_pointer_cast<array_type>(bare_array_param);
        auto a_arg = std::dynamic_pointer_cast<array_type>(bare_array_arg);
        if (a_param && a_arg) {
            return deduce_from_types(
                a_param->get_subtype(), a_arg->get_subtype(),
                type_param_names, value_param_names,
                type_deductions, value_deductions);
        }
    }

    // Check wrapper types
    auto param_sub = param_type->get_subtype();
    if (!param_sub) {
        // Concrete type, no template param inside — no deduction needed
        return true;
    }

    auto arg_sub = arg_type->get_subtype();
    if (!arg_sub) {
        // Param is const T& or const T, arg is non-const / non-ref value
        if (type::is_const(param_type)) {
            return deduce_from_types(
                param_sub, arg_type,
                type_param_names, value_param_names,
                type_deductions, value_deductions);
        }
        if (type::is_reference(param_type)) {
            // const T& can bind to rvalue
            if (type::is_const(param_sub)) {
                return deduce_from_types(
                    param_sub->get_subtype(), arg_type,
                    type_param_names, value_param_names,
                    type_deductions, value_deductions);
            }
        }
        std::string sub_name = get_template_param_name(param_sub, type_param_names);
        if (sub_name.empty()) return true; // Not a template param, skip
        return false; // Wrapper mismatch
    }

    // Indirection matching: if both are indirections, check compatibility
    if (type::is_any_indirection(param_type) && type::is_any_indirection(arg_type)) {
        bool indirection_compatible = false;
        if (type::is_pointer(param_type)) {
            // T* can accept pointer (*), link (+), view (?), reference (&)
            indirection_compatible = true;
        } else if (type::is_view(param_type)) {
            // T? can accept view (?), pointer (*), link (+), reference (&)
            indirection_compatible = true;
        } else if (type::is_reference(param_type)) {
            // T& can accept reference (&), link (+), drain (#)
            indirection_compatible = true;
        } else if (type::is_link(param_type)) {
            // T+ can accept link (+)
            indirection_compatible = type::is_link(arg_type);
        } else if (type::is_owner(param_type)) {
            // T! can accept owner (!)
            indirection_compatible = type::is_owner(arg_type);
        } else if (type::is_drain(param_type)) {
            // T# can accept drain (#) or owner (!)
            indirection_compatible = type::is_drain(arg_type) || type::is_owner(arg_type);
        }

        if (indirection_compatible) {
            return deduce_from_types(
                param_sub, arg_sub,
                type_param_names, value_param_names,
                type_deductions, value_deductions);
        }
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
        return deduce_from_types(
            param_sub, arg_sub,
            type_param_names, value_param_names,
            type_deductions, value_deductions);
    }

    // Special case: const T& vs non-const T&
    if (type::is_reference(param_type) && type::is_const(param_sub) && type::is_reference(arg_type)) {
        return deduce_from_types(
            param_sub->get_subtype(), arg_sub,
            type_param_names, value_param_names,
            type_deductions, value_deductions);
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

    // Collect type, pack, and value parameter names
    std::unordered_set<std::string> type_param_names;
    std::unordered_set<std::string> pack_param_names;
    std::unordered_set<std::string> value_param_names;
    for (const auto& param : ti.params) {
        if (param.is_type_param()) {
            if (param.is_pack) {
                pack_param_names.insert(param.name);
            } else {
                type_param_names.insert(param.name);
            }
        } else if (param.is_value_param()) {
            value_param_names.insert(param.name);
        }
    }

    // Deduction maps
    std::unordered_map<std::string, std::shared_ptr<type>> type_deductions;
    std::unordered_map<std::string, std::vector<std::shared_ptr<type>>> pack_deductions;
    std::unordered_map<std::string, k::value_type> value_deductions;

    // Process each function parameter against the call arguments
    size_t arg_idx = 0;
    for (size_t p_idx = 0; p_idx < params.size(); ++p_idx) {
        const auto& param = params[p_idx];
        if (param->is_pack_expansion()) {
            // Pack expansion: collect all remaining argument types
            const std::string& pack_name = param->pack_param_name();
            std::vector<std::shared_ptr<type>> pack_types;
            while (arg_idx < arg_types.size()) {
                // Strip reference/drain wrappers — like C++ deduction stripping
                // top-level references. The call-site expression type for a
                // variable access is T& but the deduced pack type should be T.
                auto at = arg_types[arg_idx];
                if (at && (type::is_reference(at) || type::is_drain(at))) {
                    at = at->get_subtype();
                }
                pack_types.push_back(at);
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
        if (!deduce_from_types(param_type, arg_type, type_param_names, value_param_names, type_deductions, value_deductions)) {
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
            auto it = type_deductions.find(tpl_param.name);
            if (it != type_deductions.end()) {
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
            // Value params
            auto it = value_deductions.find(tpl_param.name);
            if (it != value_deductions.end()) {
                deduced_args.push_back(template_argument::make_value(it->second));
            } else if (tpl_param.default_value.has_value()) {
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
