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

#include "template_instantiator.hpp"
#include "context.hpp"
#include "type.hpp"
#include "statements.hpp"
#include "expressions.hpp"

#include <sstream>
#include <queue>

namespace k::model {

namespace {
constexpr const char* generic_synthesis_key = "<generic_synthesis>";
}

// ═══════════════════════════════════════════════════════════════════════════
// Name / key helpers
// ═══════════════════════════════════════════════════════════════════════════

std::string type_display_name(const std::shared_ptr<type>& t) {
    if (!t) return "?";
    return t->to_string();
}

std::string build_instantiation_key(const std::vector<template_argument>& args) {
    std::ostringstream oss;
    oss << "<";
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) oss << ",";
        if (args[i].is_pack()) {
            // Pack argument: encode as "{type1,type2,...}" to distinguish different pack sizes
            oss << "{";
            for (size_t j = 0; j < args[i].pack_types.size(); ++j) {
                if (j > 0) oss << ",";
                oss << type_display_name(args[i].pack_types[j]);
            }
            oss << "}";
        } else if (args[i].is_type()) {
            oss << type_display_name(args[i].type_arg);
        } else if (args[i].value_arg.has_value()) {
            std::visit([&oss](auto&& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    oss << "void";
                } else if constexpr (std::is_same_v<T, std::nullptr_t>) {
                    oss << "null";
                } else if constexpr (std::is_same_v<T, bool>) {
                    oss << (v ? "true" : "false");
                } else if constexpr (std::is_same_v<T, std::string>) {
                    oss << "\"" << v << "\"";
                } else {
                    oss << v;
                }
            }, *args[i].value_arg);
        } else {
            oss << "?";
        }
    }
    oss << ">";
    return oss.str();
}

std::string build_instantiated_name(const std::string& base_name,
                                     const std::vector<template_argument>& args) {
    std::ostringstream oss;
    oss << base_name << "__";
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) oss << "_";
        if (args[i].is_pack()) {
            // Pack argument: encode count and types
            for (size_t j = 0; j < args[i].pack_types.size(); ++j) {
                if (j > 0) oss << "_";
                std::string tn = type_display_name(args[i].pack_types[j]);
                for (char& c : tn) {
                    if (!std::isalnum(c)) c = '_';
                }
                oss << tn;
            }
            if (args[i].pack_types.empty()) {
                oss << "0"; // empty pack
            }
        } else if (args[i].is_type()) {
            std::string tn = type_display_name(args[i].type_arg);
            for (char& c : tn) {
                if (!std::isalnum(c)) c = '_';
            }
            oss << tn;
        } else if (args[i].value_arg.has_value()) {
            std::visit([&oss](auto&& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    oss << "void";
                } else if constexpr (std::is_same_v<T, std::nullptr_t>) {
                    oss << "null";
                } else if constexpr (std::is_same_v<T, bool>) {
                    oss << (v ? "true" : "false");
                } else if constexpr (std::is_same_v<T, std::string>) {
                    std::string s = v;
                    for (char& c : s) {
                        if (!std::isalnum(c)) c = '_';
                    }
                    oss << s;
                } else if constexpr (std::is_floating_point_v<T>) {
                    // Use a sanitized representation for floats in names
                    std::string s = std::to_string(v);
                    for (char& c : s) {
                        if (c == '.' || c == '-') c = '_';
                    }
                    oss << s;
                } else {
                    oss << v;
                }
            }, *args[i].value_arg);
        } else {
            oss << "0";
        }
    }
    return oss.str();
}

tpl_info::generic_usage_descriptor build_generic_usage_descriptor(
    const tpl_info& ti,
    const std::vector<template_argument>& args)
{
    tpl_info::generic_usage_descriptor usage;
    const size_t count = std::min(ti.params.size(), args.size());
    for (size_t i = 0; i < count; ++i) {
        const auto& param = ti.params[i];
        const auto& arg = args[i];
        if (!param.is_type_param() || !arg.is_type() || !arg.type_arg) {
            continue;
        }
        usage.type_bindings[param.name] = arg.type_arg;
    }
    return usage;
}

void record_generic_usage(
    tpl_info& ti,
    const std::vector<template_argument>& args)
{
    if (!ti.is_generic) return;
    const auto key = build_instantiation_key(args);
    ti.generic_usages[key] = build_generic_usage_descriptor(ti, args);
}

// ═══════════════════════════════════════════════════════════════════════════
// Substitute template params in base class raw names
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Given a base class raw_name that may contain template arguments
 * (e.g. "Collection<T>"), substitute type parameter names using the
 * substitution map (e.g. T→int → "Collection<int>").
 */
std::string substitute_base_name(const std::string& raw_name,
                                  const type_substitution_map& subst) {
    // Find '<' — if absent, no substitution needed
    auto lt_pos = raw_name.find('<');
    if (lt_pos == std::string::npos) return raw_name;
    auto gt_pos = raw_name.rfind('>');
    if (gt_pos == std::string::npos || gt_pos <= lt_pos) return raw_name;

    std::string prefix = raw_name.substr(0, lt_pos + 1); // "Collection<"
    std::string args_str = raw_name.substr(lt_pos + 1, gt_pos - lt_pos - 1); // "T" or "T,U"

    // Split by ',' and substitute each arg
    std::string result = prefix;
    size_t pos = 0;
    bool first = true;
    while (pos < args_str.size()) {
        if (!first) result += ",";
        first = false;
        auto comma = args_str.find(',', pos);
        std::string arg = (comma == std::string::npos)
            ? args_str.substr(pos)
            : args_str.substr(pos, comma - pos);
        // Trim whitespace
        while (!arg.empty() && arg.front() == ' ') arg.erase(arg.begin());
        while (!arg.empty() && arg.back() == ' ') arg.pop_back();

        // Check if this arg name is a template parameter to substitute
        auto it = subst.find(arg);
        if (it != subst.end() && it->second) {
            result += it->second->to_string();
        } else {
            result += arg;
        }
        pos = (comma == std::string::npos) ? args_str.size() : comma + 1;
    }
    result += ">";
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// Build substitution map
// ═══════════════════════════════════════════════════════════════════════════

type_substitution_map template_instantiator::build_substitution_map(
    const tpl_info& ti,
    const std::vector<template_argument>& args)
{
    type_substitution_map result;
    size_t arg_idx = 0;
    for (size_t i = 0; i < ti.params.size() && arg_idx < args.size(); ++i) {
        if (ti.params[i].is_pack) {
            // Skip pack params — handled by build_pack_substitution_map.
            // Consume the args that belong to this pack.
            size_t remaining_params_after = 0;
            for (size_t j = i + 1; j < ti.params.size(); ++j) {
                if (!ti.params[j].is_pack) remaining_params_after++;
            }
            size_t remaining_args = args.size() - arg_idx;
            size_t pack_count = remaining_args > remaining_params_after ? remaining_args - remaining_params_after : 0;
            arg_idx += pack_count;
        } else if (args[arg_idx].is_type() && args[arg_idx].type_arg) {
            result[ti.params[i].name] = args[arg_idx].type_arg;
            arg_idx++;
        } else {
            arg_idx++;
        }
    }
    return result;
}

value_substitution_map template_instantiator::build_value_substitution_map(
    const tpl_info& ti,
    const std::vector<template_argument>& args)
{
    value_substitution_map result;
    size_t count = std::min(ti.params.size(), args.size());
    for (size_t i = 0; i < count; ++i) {
        if (args[i].is_value() && args[i].value_arg.has_value()) {
            result[ti.params[i].name] = *args[i].value_arg;
        }
    }
    return result;
}

pack_substitution_map template_instantiator::build_pack_substitution_map(
    const tpl_info& ti,
    const std::vector<template_argument>& args)
{
    pack_substitution_map result;
    size_t arg_idx = 0;
    for (size_t i = 0; i < ti.params.size(); ++i) {
        if (ti.params[i].is_pack) {
            std::vector<std::shared_ptr<type>> pack_types;
            if (arg_idx < args.size() && args[arg_idx].is_pack()) {
                // Already packed into a single argument
                pack_types = args[arg_idx].pack_types;
                arg_idx++;
            } else {
                // Consume individual type args until we run out or hit the next non-pack param
                size_t remaining_params_after = 0;
                for (size_t j = i + 1; j < ti.params.size(); ++j) {
                    if (!ti.params[j].is_pack) remaining_params_after++;
                }
                size_t remaining_args = args.size() - arg_idx;
                size_t pack_count = remaining_args > remaining_params_after ? remaining_args - remaining_params_after : 0;
                for (size_t j = 0; j < pack_count && arg_idx < args.size(); ++j) {
                    if (args[arg_idx].is_type() && args[arg_idx].type_arg) {
                        pack_types.push_back(args[arg_idx].type_arg);
                    }
                    arg_idx++;
                }
            }
            result[ti.params[i].name] = std::move(pack_types);
        } else {
            if (arg_idx < args.size()) arg_idx++;
        }
    }
    return result;
}

static type_substitution_map build_generic_substitution_map(
    const tpl_info& ti,
    const std::shared_ptr<context>& ctx)
{
    type_substitution_map result;
    if (!ctx) return result;

    auto byte_type = ctx->from_type(primitive_type::BYTE);
    if (!byte_type) return result;

    // Generic synthesis uses a uniform opaque pointer model type (i8*).
    auto opaque_ptr_type = byte_type->get_pointer();
    for (const auto& param : ti.params) {
        if (param.is_type_param() && !param.name.empty()) {
            result[param.name] = opaque_ptr_type;
        }
    }
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// Expression type substitution
// ═══════════════════════════════════════════════════════════════════════════

void template_instantiator::substitute_expr_types(
    std::shared_ptr<expression> expr,
    const type_substitution_map& subst)
{
    if (!expr) return;

    // Substitute the expression's own type
    if (expr->get_type()) {
        auto new_type = substitute_type(expr->get_type(), subst);
        if (new_type != expr->get_type()) {
            expr->set_type(new_type);
        }
    }

    // Substitute type in cast expressions
    if (auto ce = std::dynamic_pointer_cast<cast_expression>(expr)) {
        if (ce->get_cast_type()) {
            auto new_cast = substitute_type(ce->get_cast_type(), subst);
            if (new_cast != ce->get_cast_type()) {
                ce->set_cast_type(new_cast);
            }
        }
    }

    // Recurse into sub-expressions via the expression hierarchy
    if (auto ue = std::dynamic_pointer_cast<unary_expression>(expr)) {
        substitute_expr_types(std::const_pointer_cast<expression>(ue->sub_expr()), subst);
    } else if (auto be = std::dynamic_pointer_cast<binary_expression>(expr)) {
        substitute_expr_types(be->left(), subst);
        substitute_expr_types(be->right(), subst);
    } else if (auto fie = std::dynamic_pointer_cast<function_invocation_expression>(expr)) {
        substitute_expr_types(std::const_pointer_cast<expression>(fie->callee_expr()), subst);
        for (auto& arg : fie->arguments()) {
            substitute_expr_types(std::const_pointer_cast<expression>(arg), subst);
        }
    } else if (auto cie = std::dynamic_pointer_cast<constructor_invocation_expression>(expr)) {
        substitute_expr_types(std::static_pointer_cast<expression>(cie->constructed_symbol()), subst);
        for (auto& arg : cie->arguments()) {
            substitute_expr_types(std::const_pointer_cast<expression>(arg), subst);
        }
    } else if (auto aie = std::dynamic_pointer_cast<array_init_expression>(expr)) {
        for (auto& elem : aie->elements()) {
            substitute_expr_types(std::const_pointer_cast<expression>(elem), subst);
        }
    } else if (auto dsie = std::dynamic_pointer_cast<designated_struct_init_expression>(expr)) {
        for (auto& mi : dsie->members_mutable()) {
            if (mi.value) substitute_expr_types(mi.value, subst);
            for (auto& a : mi.args) substitute_expr_types(a, subst);
        }
    } else if (auto tce = std::dynamic_pointer_cast<temporary_construction_expression>(expr)) {
        for (auto& arg : tce->arguments()) {
            substitute_expr_types(std::const_pointer_cast<expression>(arg), subst);
        }
    } else if (auto ne = std::dynamic_pointer_cast<new_expression>(expr)) {
        for (auto& arg : ne->arguments()) {
            substitute_expr_types(std::const_pointer_cast<expression>(arg), subst);
        }
        substitute_expr_types(std::const_pointer_cast<expression>(ne->array_size_expr()), subst);
    } else if (auto de = std::dynamic_pointer_cast<delete_expression>(expr)) {
        substitute_expr_types(std::const_pointer_cast<expression>(de->sub_expr()), subst);
    }
}

std::shared_ptr<expression> template_instantiator::clone_and_substitute_expr(
    const std::shared_ptr<expression>& src,
    const type_substitution_map& subst,
    const value_substitution_map& val_subst)
{
    if (!src) return nullptr;

    auto cloned = src->clone();
    substitute_expr_types(cloned, subst);
    if (!val_subst.empty()) {
        substitute_value_params(cloned, val_subst);
    }
    return cloned;
}

void template_instantiator::substitute_value_params(
    std::shared_ptr<expression>& expr,
    const value_substitution_map& val_subst)
{
    if (!expr || val_subst.empty()) return;

    // Check if this expression itself is a symbol matching a value parameter
    if (auto sym = std::dynamic_pointer_cast<symbol_expression>(expr)) {
        if (!sym->is_resolved()) {
            const auto& nm = sym->get_name();
            if (nm.size() == 1 && !nm.has_root_prefix()) {
                auto it = val_subst.find(nm.front());
                if (it != val_subst.end()) {
                    // Replace with a value_expression holding the concrete value
                    // using the actual type from the value_type variant.
                    expr = std::visit([](auto&& v) -> std::shared_ptr<expression> {
                        using T = std::decay_t<decltype(v)>;
                        if constexpr (std::is_same_v<T, std::monostate>) {
                            return value_expression::from_value<int>(0);
                        } else if constexpr (std::is_same_v<T, std::nullptr_t>) {
                            return value_expression::from_value<int>(0);
                        } else if constexpr (std::is_same_v<T, std::string>) {
                            return value_expression::from_value(v);
                        } else {
                            return value_expression::from_value<T>(v);
                        }
                    }, it->second);
                    return;
                }
            }
        }
    }

    // Recurse into sub-expressions
    if (auto be = std::dynamic_pointer_cast<binary_expression>(expr)) {
        auto l = be->left();
        auto r = be->right();
        substitute_value_params(l, val_subst);
        substitute_value_params(r, val_subst);
        if (l != be->left()) be->assign_left(l);
        if (r != be->right()) be->assign_right(r);
    } else if (auto ue = std::dynamic_pointer_cast<unary_expression>(expr)) {
        auto s = std::const_pointer_cast<expression>(ue->sub_expr());
        substitute_value_params(s, val_subst);
        if (s != ue->sub_expr()) ue->assign(s);
    } else if (auto fie = std::dynamic_pointer_cast<function_invocation_expression>(expr)) {
        for (size_t i = 0; i < fie->arguments().size(); ++i) {
            auto arg = std::const_pointer_cast<expression>(fie->arguments()[i]);
            substitute_value_params(arg, val_subst);
            if (arg != fie->arguments()[i]) fie->assign_argument(i, arg);
        }
    } else if (auto cie = std::dynamic_pointer_cast<constructor_invocation_expression>(expr)) {
        for (size_t i = 0; i < cie->arguments().size(); ++i) {
            auto arg = std::const_pointer_cast<expression>(cie->arguments()[i]);
            substitute_value_params(arg, val_subst);
            if (arg != cie->arguments()[i]) cie->assign_argument(i, arg);
        }
    } else if (auto ce = std::dynamic_pointer_cast<cast_expression>(expr)) {
        auto s = std::const_pointer_cast<expression>(ce->sub_expr());
        substitute_value_params(s, val_subst);
        if (s != ce->sub_expr()) ce->assign(s);
    }
}

void template_instantiator::retarget_init_expr(
    const std::shared_ptr<expression>& init_expr,
    const std::shared_ptr<variable_definition>& new_var)
{
    if (!init_expr || !new_var) return;

    // constructor_invocation_expression
    if (auto cie = std::dynamic_pointer_cast<constructor_invocation_expression>(init_expr)) {
        if (cie->constructed_symbol()) {
            cie->constructed_symbol()->set_target(new_var);
        }
        return;
    }

    // designated_struct_init_expression
    if (auto dsie = std::dynamic_pointer_cast<designated_struct_init_expression>(init_expr)) {
        auto sym = dsie->constructed_symbol();
        if (sym) {
            sym->set_target(new_var);
        }
        return;
    }

    // array_init_expression
    if (auto aie = std::dynamic_pointer_cast<array_init_expression>(init_expr)) {
        auto sym = aie->constructed_symbol();
        if (sym) {
            sym->set_target(new_var);
        }
        return;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Statement cloning with type substitution
// ═══════════════════════════════════════════════════════════════════════════

std::shared_ptr<statement> template_instantiator::clone_statement(
    const statement& src,
    std::shared_ptr<statement> parent_stmt,
    const type_substitution_map& subst,
    const value_substitution_map& val_subst)
{
    // Return statement
    if (auto rs = dynamic_cast<const return_statement*>(&src)) {
        auto new_rs = std::make_shared<return_statement>(parent_stmt);
        new_rs->_ast_node = rs->get_ast_node(); // optional, for diagnostics
        if (rs->get_expression()) {
            new_rs->set_expression(clone_and_substitute_expr(
                std::const_pointer_cast<expression>(rs->get_expression()), subst, val_subst));
        }
        return new_rs;
    }

    // Break statement
    if (auto bs = dynamic_cast<const break_statement*>(&src)) {
        auto new_bs = std::make_shared<break_statement>(parent_stmt);
        new_bs->_ast_node = bs->get_ast_node();
        return new_bs;
    }

    // Continue statement
    if (auto cs = dynamic_cast<const continue_statement*>(&src)) {
        auto new_cs = std::make_shared<continue_statement>(parent_stmt);
        new_cs->_ast_node = cs->get_ast_node();
        return new_cs;
    }

    // If-else statement
    if (auto ies = dynamic_cast<const if_else_statement*>(&src)) {
        auto new_ies = std::make_shared<if_else_statement>(parent_stmt);
        new_ies->_ast_node = ies->get_ast_node();
        if (ies->has_cond_var()) {
            // Clone the condition variable by cloning it as a statement
            auto cloned_var = clone_statement(*ies->get_cond_var(), new_ies, subst, val_subst);
            // The variable_holder mechanism should have registered it via on_variable_defined
        }
        if (ies->get_test_expr()) {
            new_ies->set_test_expr(clone_and_substitute_expr(
                std::const_pointer_cast<expression>(ies->get_test_expr()), subst, val_subst));
        }
        if (ies->get_then_stmt()) {
            new_ies->set_then_stmt(clone_statement(*ies->get_then_stmt(), new_ies, subst, val_subst));
        }
        if (ies->get_else_stmt()) {
            new_ies->set_else_stmt(clone_statement(*ies->get_else_stmt(), new_ies, subst, val_subst));
        }
        return new_ies;
    }

    // While statement
    if (auto ws = dynamic_cast<const while_statement*>(&src)) {
        auto new_ws = std::make_shared<while_statement>(parent_stmt);
        new_ws->_ast_node = ws->get_ast_node();
        if (ws->get_test_expr()) {
            new_ws->set_test_expr(clone_and_substitute_expr(
                std::const_pointer_cast<expression>(ws->get_test_expr()), subst, val_subst));
        }
        if (ws->get_nested_stmt()) {
            new_ws->set_nested_stmt(clone_statement(*ws->get_nested_stmt(), new_ws, subst, val_subst));
        }
        return new_ws;
    }

    // Expression statement
    if (auto es = dynamic_cast<const expression_statement*>(&src)) {
        auto new_es = std::make_shared<expression_statement>(parent_stmt);
        new_es->_ast_node = es->get_ast_node();
        if (es->get_expression()) {
            new_es->set_expression(clone_and_substitute_expr(
                std::const_pointer_cast<expression>(es->get_expression()), subst, val_subst));
        }
        return new_es;
    }

    // Variable statement
    if (auto vs = dynamic_cast<const variable_statement*>(&src)) {
        auto new_vs = variable_statement::make_shared(parent_stmt, vs->get_short_name());
        new_vs->_ast_node = vs->get_ast_node();
        new_vs->set_type(substitute_type(std::const_pointer_cast<type>(vs->get_type()), subst));
        new_vs->set_const(vs->is_const());
        if (vs->get_init_expr()) {
            // variable_definition::set_init_expr(shared_ptr<expression>) is the base version
            auto cloned_init = clone_and_substitute_expr(
                    std::const_pointer_cast<expression>(vs->get_init_expr()), subst, val_subst);
            // If the init expression is a constructor_invocation_expression, retarget its
            // constructed_symbol to the new cloned variable.  Without this, the CIE would
            // still reference the original template's variable_statement, which was never
            // registered in the LLVM codegen context.
            if (auto cie = std::dynamic_pointer_cast<constructor_invocation_expression>(cloned_init)) {
                auto new_sym = symbol_expression::from_variable(
                    std::static_pointer_cast<variable_definition>(new_vs));
                new_sym->set_parent_expression(cie);
                cie->constructed_symbol(new_sym);
            }
            static_cast<variable_definition*>(new_vs.get())->set_init_expr(cloned_init);
        }
        // Register in the block's variable holder if parent is a block
        if (auto blk = std::dynamic_pointer_cast<block>(parent_stmt)) {
            // The variable is already created with the parent; it needs to be
            // registered in the block's variable map. append_variable would
            // create a new one, so we manually register.
            blk->_vars[vs->get_short_name()] = new_vs;
        }
        return new_vs;
    }

    // Block (nested)
    if (auto blk = dynamic_cast<const block*>(&src)) {
        auto new_blk = std::make_shared<block>(parent_stmt);
        new_blk->_ast_node = blk->get_ast_node();
        clone_block_contents(*blk, new_blk, subst, val_subst);
        return new_blk;
    }

    // For statement
    if (auto fs = dynamic_cast<const for_statement*>(&src)) {
        auto new_fs = std::make_shared<for_statement>(parent_stmt);
        new_fs->_ast_node = fs->get_ast_node();
        if (fs->get_decl_stmt()) {
            auto cloned_decl = std::dynamic_pointer_cast<variable_statement>(
                clone_statement(*fs->get_decl_stmt(), new_fs, subst, val_subst));
            new_fs->set_decl_stmt(cloned_decl);
        }
        if (fs->get_test_expr()) {
            new_fs->set_test_expr(clone_and_substitute_expr(
                std::const_pointer_cast<expression>(fs->get_test_expr()), subst, val_subst));
        }
        if (fs->get_step_expr()) {
            new_fs->set_step_expr(clone_and_substitute_expr(
                std::const_pointer_cast<expression>(fs->get_step_expr()), subst, val_subst));
        }
        if (fs->get_nested_stmt()) {
            new_fs->set_nested_stmt(clone_statement(*fs->get_nested_stmt(), new_fs, subst, val_subst));
        }
        return new_fs;
    }

    // Fallback: unknown statement type — return empty
    return nullptr;
}

void template_instantiator::clone_block_contents(
    const block& src,
    std::shared_ptr<block> dst,
    const type_substitution_map& subst,
    const value_substitution_map& val_subst)
{
    for (auto& stmt : src.get_statements()) {
        if (!stmt) continue;
        auto cloned = clone_statement(*stmt, dst, subst, val_subst);
        if (cloned) {
            dst->append_statement(cloned);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Clone member variable
// ═══════════════════════════════════════════════════════════════════════════

void template_instantiator::clone_member_variable(
    const member_variable_definition& src,
    std::shared_ptr<aggregate> target,
    const type_substitution_map& subst,
    const value_substitution_map& val_subst)
{
    // Skip the synthetic __parent__ field
    if (src.get_short_name() == "__parent__") return;
    // Skip synthetic vptr fields (__vptr__) — they are re-injected by the
    // resolution passes via klass::inject_vptr_field after vtable building.
    if (src.get_short_name().rfind("__vptr", 0) == 0) return;
    // Skip synthetic base sub-object fields (__base_X__, __vbptr_X__, __vbase_X__)
    if (src.get_short_name().rfind("__base_", 0) == 0) return;
    if (src.get_short_name().rfind("__vbptr_", 0) == 0) return;
    if (src.get_short_name().rfind("__vbase_", 0) == 0) return;

    bool is_static = false;
    auto new_var = target->append_variable(src.get_short_name(), is_static);
    if (!new_var) return;

    // Substitute type
    auto src_type = std::const_pointer_cast<type>(src.get_type());
    new_var->set_type(substitute_type(src_type, subst));
    new_var->set_const(src.is_const());

    // Clone init expression and retarget to new variable
    if (src.get_init_expr()) {
        auto cloned_init = clone_and_substitute_expr(
            std::const_pointer_cast<expression>(src.get_init_expr()), subst, val_subst);
        new_var->set_init_expr(cloned_init);
        retarget_init_expr(cloned_init, new_var);
    }

    // Copy visibility
    if (auto mv = std::dynamic_pointer_cast<member_variable_definition>(new_var)) {
        mv->set_visibility(src.get_visibility());
    }

    // Copy AST node reference (optional, for diagnostics)
    if (auto elem = std::dynamic_pointer_cast<element>(new_var)) {
        elem->_ast_node = src.get_ast_node();
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Pack expansion helpers
// ═══════════════════════════════════════════════════════════════════════════

namespace {

using pack_names_map = std::unordered_map<std::string, std::vector<std::string>>;

/**
 * Expand pack_expansion_expression arguments in a function_invocation_expression.
 * Replaces `f(args...)` with `f(args_0, args_1, ..., args_N)`.
 */
void expand_pack_in_invocation_args(
    std::vector<std::shared_ptr<expression>>& args,
    const pack_names_map& pack_expansion_names)
{
    std::vector<std::shared_ptr<expression>> new_args;
    bool expanded = false;
    for (auto& arg : args) {
        if (auto pe = std::dynamic_pointer_cast<pack_expansion_expression>(arg)) {
            // Look up the pack name in the expansion map
            const auto& pack_name = pe->pack_name();
            auto it = pack_expansion_names.find(pack_name);
            if (it != pack_expansion_names.end()) {
                // Replace with symbol expressions referencing each concrete parameter
                for (const auto& concrete_name : it->second) {
                    auto sym = symbol_expression::from_identifier(
                        name(false, {concrete_name}));
                    new_args.push_back(sym);
                }
                expanded = true;
            } else {
                new_args.push_back(arg);
            }
        } else {
            new_args.push_back(arg);
        }
    }
    if (expanded) {
        args = std::move(new_args);
    }
}

/**
 * Recursively walk an expression tree and expand pack expressions in invocations.
 */
void expand_pack_in_expr(
    std::shared_ptr<expression>& expr,
    const pack_names_map& pack_expansion_names)
{
    if (!expr) return;

    if (auto fie = std::dynamic_pointer_cast<function_invocation_expression>(expr)) {
        auto callee = std::const_pointer_cast<expression>(fie->callee_expr());
        expand_pack_in_expr(callee, pack_expansion_names);
        // Expand packs in arguments
        std::vector<std::shared_ptr<expression>> args;
        for (auto& a : fie->arguments()) {
            args.push_back(std::const_pointer_cast<expression>(a));
        }
        expand_pack_in_invocation_args(args, pack_expansion_names);
        fie->arguments(args);
    } else if (auto cie = std::dynamic_pointer_cast<constructor_invocation_expression>(expr)) {
        std::vector<std::shared_ptr<expression>> args;
        for (auto& a : cie->arguments()) {
            args.push_back(std::const_pointer_cast<expression>(a));
        }
        expand_pack_in_invocation_args(args, pack_expansion_names);
        cie->arguments(args);
    } else if (auto ne = std::dynamic_pointer_cast<new_expression>(expr)) {
        std::vector<std::shared_ptr<expression>> args;
        for (auto& a : ne->arguments()) {
            args.push_back(std::const_pointer_cast<expression>(a));
        }
        expand_pack_in_invocation_args(args, pack_expansion_names);
        ne->assign_arguments(args);
    }
}

/**
 * Walk all statements in a block and expand pack expressions in invocations.
 */
void expand_pack_in_block(
    std::shared_ptr<block> blk,
    const pack_names_map& pack_expansion_names);

void expand_pack_in_statement(
    std::shared_ptr<statement> stmt,
    const pack_names_map& pack_expansion_names)
{
    if (!stmt) return;

    if (auto es = std::dynamic_pointer_cast<expression_statement>(stmt)) {
        auto expr = es->get_expression();
        expand_pack_in_expr(expr, pack_expansion_names);
        es->set_expression(expr);
    } else if (auto rs = std::dynamic_pointer_cast<return_statement>(stmt)) {
        auto expr = rs->get_expression();
        expand_pack_in_expr(expr, pack_expansion_names);
        if (expr) rs->set_expression(expr);
    } else if (auto bs = std::dynamic_pointer_cast<block>(stmt)) {
        expand_pack_in_block(bs, pack_expansion_names);
    } else if (auto ifs = std::dynamic_pointer_cast<if_else_statement>(stmt)) {
        expand_pack_in_statement(std::const_pointer_cast<statement>(ifs->get_then_stmt()), pack_expansion_names);
        expand_pack_in_statement(std::const_pointer_cast<statement>(ifs->get_else_stmt()), pack_expansion_names);
    } else if (auto ws = std::dynamic_pointer_cast<while_statement>(stmt)) {
        expand_pack_in_statement(std::const_pointer_cast<statement>(ws->get_nested_stmt()), pack_expansion_names);
    } else if (auto fs = std::dynamic_pointer_cast<for_statement>(stmt)) {
        expand_pack_in_statement(std::const_pointer_cast<statement>(fs->get_nested_stmt()), pack_expansion_names);
    } else if (auto vs = std::dynamic_pointer_cast<variable_statement>(stmt)) {
        auto expr = vs->get_init_expr();
        if (expr) {
            expand_pack_in_expr(expr, pack_expansion_names);
            vs->variable_definition::set_init_expr(expr);
        }
    }
}

void expand_pack_in_block(
    std::shared_ptr<block> blk,
    const pack_names_map& pack_expansion_names)
{
    if (!blk) return;
    for (auto& stmt : blk->get_statements()) {
        expand_pack_in_statement(stmt, pack_expansion_names);
    }
}

} // anonymous namespace

void template_instantiator::expand_pack_expressions_in_block(
    std::shared_ptr<block> blk,
    const std::unordered_map<std::string, std::vector<std::string>>& pack_expansion_names)
{
    expand_pack_in_block(blk, pack_expansion_names);
}

// ═══════════════════════════════════════════════════════════════════════════
// Populate function from template source
// ═══════════════════════════════════════════════════════════════════════════

void template_instantiator::populate_function_from_template(
    std::shared_ptr<function> dst,
    const function& src,
    const type_substitution_map& subst,
    const value_substitution_map& val_subst,
    const pack_substitution_map& pack_subst)
{
    // Set return type
    if (src.has_return_type()) {
        dst->set_return_type(substitute_type(
            std::const_pointer_cast<type>(src.get_return_type()), subst));
    }

    // Clone parameters (skip 'this' — will be recreated by resolution passes)
    // Map from original pack param names to generated concrete param names
    std::unordered_map<std::string, std::vector<std::string>> pack_expansion_names;
    for (auto& param : src.parameters()) {
        if (param == src.get_this_parameter()) continue;

        if (param->is_pack_expansion() && !param->pack_param_name().empty()) {
            // Expand pack parameter into N concrete parameters
            auto it = pack_subst.find(param->pack_param_name());
            if (it != pack_subst.end()) {
                const auto& pack_types = it->second;
                std::vector<std::string> generated_names;
                for (size_t i = 0; i < pack_types.size(); ++i) {
                    std::string concrete_name = param->get_short_name() + "_" + std::to_string(i);
                    auto new_param = dst->append_parameter(concrete_name, pack_types[i]);
                    new_param->set_const(param->is_const());
                    new_param->_ast_node = param->get_ast_node();
                    generated_names.push_back(concrete_name);
                }
                pack_expansion_names[param->get_short_name()] = std::move(generated_names);
            } else {
                // Pack param not in pack_subst — preserve as-is for member templates
                // whose inner template parameters haven't been instantiated yet.
                auto param_type = substitute_type(
                    std::const_pointer_cast<type>(param->get_type()), subst);
                auto new_param = dst->append_parameter(param->get_short_name(), param_type);
                new_param->set_const(param->is_const());
                new_param->set_pack_expansion(true);
                new_param->set_pack_param_name(param->pack_param_name());
                new_param->_ast_node = param->get_ast_node();
            }
        } else {
            auto param_type = substitute_type(
                std::const_pointer_cast<type>(param->get_type()), subst);
            auto new_param = dst->append_parameter(param->get_short_name(), param_type);
            new_param->set_const(param->is_const());
            new_param->set_varargs(param->is_varargs());
            new_param->_ast_node = param->get_ast_node(); // diagnostics
        }
    }

    // Clone body only when the source function actually has one.
    // Using get_block() would synthesize empty blocks for signature-only imports.
    auto src_block = src._block;
    if (src_block) {
        auto dst_block = dst->get_block();
        if (dst_block) {
            clone_block_contents(*src_block, dst_block, subst, val_subst);
            // Post-process: expand pack_expansion_expression in function/constructor invocations
            if (!pack_expansion_names.empty()) {
                expand_pack_expressions_in_block(dst_block, pack_expansion_names);
            }
        }
    }

    // Copy AST node (optional, for diagnostics)
    dst->_ast_node = src.get_ast_node();

    // Copy annotations (preserves @Intrinsic and other compile-time annotations)
    for (auto& ann : src.get_annotations()) {
        dst->add_annotation(ann);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Clone method
// ═══════════════════════════════════════════════════════════════════════════

void template_instantiator::clone_method(
    const function& src,
    std::shared_ptr<aggregate> target,
    const type_substitution_map& subst,
    const value_substitution_map& val_subst)
{
    auto new_func = target->define_function(src.get_short_name(), src.is_static());
    if (!new_func) return;

    // Copy flags
    new_func->set_visibility(src.get_visibility());
    new_func->set_const_member(src.is_const_member());
    new_func->set_operator(src.is_operator());
    new_func->set_virtual(src.is_virtual());
    new_func->set_abstract_func(src.is_abstract_func());
    new_func->set_final_func(src.is_final_func());
    new_func->set_override_specifier(src.is_override_specifier());
    new_func->set_aliasing(src.get_aliasing());
    new_func->set_compiler_generated(src.is_compiler_generated());

    // If the source method is itself a template (member template function),
    // preserve its tpl_info on the cloned method. The method's own template
    // parameters remain unresolved — they will be instantiated when the member
    // template is invoked with explicit template arguments.
    if (src.get_tpl_info()) {
        auto* src_ti = src.get_tpl_info();
        auto new_ti = std::make_unique<tpl_info>();
        new_ti->params = src_ti->params;
        new_ti->is_generic = src_ti->is_generic;
        new_ti->source_text = src_ti->source_text;
        new_func->set_tpl_info(std::move(new_ti));
        // For member templates, apply outer-struct type substitution to the
        // return type and body, but do NOT expand the method's own template params.
        // We still need to populate the function signature/body with the outer subst applied.
    }

    populate_function_from_template(new_func, src, subst, val_subst);
}

// ═══════════════════════════════════════════════════════════════════════════
// Clone constructor
// ═══════════════════════════════════════════════════════════════════════════

void template_instantiator::clone_constructor(
    const constructor& src,
    std::shared_ptr<aggregate> target,
    const type_substitution_map& subst,
    const value_substitution_map& val_subst)
{
    // define_function with the aggregate name creates a constructor
    auto new_func = target->define_function(target->get_short_name(), false);
    auto new_ctor = std::dynamic_pointer_cast<constructor>(new_func);
    if (!new_ctor) return;

    // Copy flags
    new_ctor->set_visibility(src.get_visibility());
    new_ctor->set_aliasing(src.get_aliasing());
    new_ctor->set_compiler_generated(src.is_compiler_generated());
    new_ctor->set_copy_constructor(src.is_copy_constructor());

    // Clone member inits
    for (auto& mi : src.member_inits()) {
        std::vector<std::shared_ptr<expression>> new_args;
        for (auto& arg : mi.args) {
            new_args.push_back(clone_and_substitute_expr(arg, subst, val_subst));
        }
        new_ctor->add_member_init(mi.member_name, std::move(new_args), mi.is_base_init);
    }

    populate_function_from_template(new_ctor, src, subst, val_subst);
}

// ═══════════════════════════════════════════════════════════════════════════
// Clone destructor
// ═══════════════════════════════════════════════════════════════════════════

void template_instantiator::clone_destructor(
    const destructor& src,
    std::shared_ptr<aggregate> target,
    const type_substitution_map& subst,
    const value_substitution_map& val_subst)
{
    // define_function with "~" + aggregate name creates a destructor
    auto new_func = target->define_function("~" + target->get_short_name(), false);
    auto new_dtor = std::dynamic_pointer_cast<destructor>(new_func);
    if (!new_dtor) return;

    new_dtor->set_visibility(src.get_visibility());
    populate_function_from_template(new_dtor, src, subst, val_subst);
}

// ═══════════════════════════════════════════════════════════════════════════
// Clone nested aggregate
// ═══════════════════════════════════════════════════════════════════════════

void template_instantiator::clone_nested_aggregate(
    const aggregate& src,
    std::shared_ptr<aggregate> target,
    const type_substitution_map& subst,
    const value_substitution_map& val_subst,
    std::shared_ptr<context> ctx)
{
    // Create the nested aggregate inside target (not in the parent namespace).
    std::shared_ptr<aggregate> nested;
    if (src.is_class()) {
        nested = target->define_class(src.get_short_name());
    } else {
        nested = target->define_structure(src.get_short_name());
    }
    if (!nested) return;

    // Copy aggregate flags
    nested->set_final(src.is_final());
    nested->set_abstract(src.is_abstract());
    nested->set_const_struct(src.is_const_struct());
    nested->set_visibility(src.get_visibility());
    nested->_ast_node = src.get_ast_node();

    // Copy base class specs (raw names — resolved later by resolution passes)
    // Substitute template type parameters in base names (e.g. "Collection<T>" → "Collection<int>")
    for (auto& bs : src.get_bases()) {
        nested->add_base(substitute_base_name(bs.raw_name, subst), bs.vis);
    }

    // Clone all children with type substitution
    for (auto& child : src.get_children()) {
        if (auto mv = std::dynamic_pointer_cast<member_variable_definition>(child)) {
            clone_member_variable(*mv, nested, subst, val_subst);
        } else if (auto ctor = std::dynamic_pointer_cast<constructor>(child)) {
            clone_constructor(*ctor, nested, subst, val_subst);
        } else if (auto dtor = std::dynamic_pointer_cast<destructor>(child)) {
            clone_destructor(*dtor, nested, subst, val_subst);
        } else if (auto fn = std::dynamic_pointer_cast<function>(child)) {
            clone_method(*fn, nested, subst, val_subst);
        } else if (auto inner = std::dynamic_pointer_cast<aggregate>(child)) {
            // Recursively clone deeper nested aggregates
            clone_nested_aggregate(*inner, nested, subst, val_subst, ctx);
        } else if (auto inner_un = std::dynamic_pointer_cast<union_type_def>(child)) {
            // Clone nested union types (e.g. an inner union inside a nested struct)
            clone_nested_union(*inner_un, nested, subst, ctx);
        }
    }

    // Generate a default constructor when no explicit constructor was cloned
    if (nested->constructors().empty()) {
        auto default_ctor = constructor::make_shared(nested->shared_as<aggregate>());
        default_ctor->set_compiler_generated(true);
        nested->_constructors.push_back(default_ctor);
        nested->_children.push_back(default_ctor);
    }

    nested->update_mangled_name();
}

// ═══════════════════════════════════════════════════════════════════════════
// Clone: nested union inside a template aggregate
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Clone a nested union_type_def from a template aggregate into a concrete
 * aggregate, applying type substitution to each alternative's type.
 *
 * After cloning, the nested union is registered in target->_unions (via
 * define_union()), making it visible to scope lookups that search for
 * "UnionName::Kind::entry" inside method bodies of the concrete aggregate.
 */
void template_instantiator::clone_nested_union(
    const union_type_def& src,
    std::shared_ptr<aggregate> target,
    const type_substitution_map& subst,
    std::shared_ptr<context> ctx)
{
    // Create the nested union inside target (registers it in _unions).
    auto nested = target->define_union(src.get_short_name());
    if (!nested) return;

    nested->set_visibility(src.get_visibility());

    // Reuse the source union's struct_type (if it has one) so that any member-variable
    // type references that were already resolved against the source struct_type continue
    // to match in find_union_by_struct_type lookups.  The LLVM layout is shared with the
    // template definition; concrete size computations happen in declaration_generator.
    if (src.get_struct_type()) {
        nested->set_struct_type(src.get_struct_type());
    } else if (ctx) {
        // Source has no struct_type yet (uncommon): create an opaque one for type-lookup purposes.
        auto st_name = src.get_short_name();
        auto st_type = std::make_shared<struct_type>(st_name, std::weak_ptr<aggregate>{});
        ctx->add_struct(st_type);
        nested->set_struct_type(st_type);
    }

    // Clone alternatives with type substitution.
    for (const auto& alt : src.alternatives()) {
        std::string raw = alt.raw_type_name;
        // Substitute raw type name if it refers to a template parameter (e.g. "R" → "int").
        auto subst_it = subst.find(raw);
        if (subst_it != subst.end() && subst_it->second) {
            raw = subst_it->second->to_string();
        }
        nested->add_alternative(alt.name, raw, alt.is_const);

        // Substitute the resolved type as well.
        auto& new_alt = nested->alternatives_mutable().back();
        if (alt.resolved_type) {
            new_alt.resolved_type = substitute_type(alt.resolved_type, subst);
        } else if (!raw.empty() && ctx) {
            auto from_ctx = ctx->from_string(raw);
            if (from_ctx) new_alt.resolved_type = from_ctx;
        }
    }

    nested->update_mangled_name();
}

// ═══════════════════════════════════════════════════════════════════════════
// Instantiation: aggregate
// ═══════════════════════════════════════════════════════════════════════════

std::shared_ptr<aggregate> template_instantiator::instantiate_aggregate(
    aggregate& tpl_def,
    const std::vector<template_argument>& args,
    std::shared_ptr<ns> parent_ns,
    k::model::unit& unit,
    std::shared_ptr<context> ctx,
    k::log::logger& logger)
{
    auto* ti = tpl_def.get_tpl_info();
    if (!ti) return nullptr;

    // Check instantiation cache
    std::string key = build_instantiation_key(args);
    auto it = ti->instantiations.find(key);
    if (it != ti->instantiations.end()) {
        if (auto* agg_ptr = std::get_if<std::shared_ptr<aggregate>>(&it->second)) {
            return *agg_ptr;
        }
    }

    // Build the instantiated name
    std::string base_name = tpl_def.get_short_name();
    std::string inst_name = build_instantiated_name(base_name, args);

    // Build type substitution map
    auto subst = build_substitution_map(*ti, args);
    auto val_subst = build_value_substitution_map(*ti, args);

    // 1. Create a new concrete aggregate in the parent namespace
    std::shared_ptr<aggregate> concrete;
    if (std::dynamic_pointer_cast<model::interface>(tpl_def.shared_as<element>())) {
        concrete = parent_ns->define_interface(inst_name);
    } else if (tpl_def.is_class()) {
        concrete = parent_ns->define_class(inst_name);
    } else if (tpl_def.is_annotation()) {
        concrete = parent_ns->define_annotation(inst_name);
    } else {
        concrete = parent_ns->define_structure(inst_name);
    }
    if (!concrete) return nullptr;

    // Copy aggregate flags
    concrete->set_final(tpl_def.is_final());
    concrete->set_abstract(tpl_def.is_abstract());
    concrete->set_const_struct(tpl_def.is_const_struct());
    concrete->set_visibility(tpl_def.get_visibility());

    // Copy AST reference (optional, for diagnostics)
    concrete->_ast_node = tpl_def.get_ast_node();

    // Store template instantiation info for mangling (I…E encoding)
    concrete->set_tpl_instantiation_info(base_name, args);

    // Copy bases (with template parameter substitution in raw names)
    for (auto& bs : tpl_def.get_bases()) {
        concrete->add_base(substitute_base_name(bs.raw_name, subst), bs.vis);
    }

    // Resolve template base classes immediately (e.g. "Collection<int>")
    for (auto& bs : concrete->get_bases_mutable()) {
        if (bs.base) continue; // Already resolved
        auto lt_pos = bs.raw_name.find('<');
        if (lt_pos != std::string::npos) {
            auto gt_pos = bs.raw_name.rfind('>');
            if (gt_pos != std::string::npos && gt_pos > lt_pos) {
                std::string tpl_base_name = bs.raw_name.substr(0, lt_pos);
                std::string args_str = bs.raw_name.substr(lt_pos + 1, gt_pos - lt_pos - 1);

                // Look up the template aggregate by name in parent namespace
                std::shared_ptr<aggregate> tpl_base_agg;
                if (auto found = parent_ns->get_aggregate(tpl_base_name)) {
                    tpl_base_agg = found;
                }

                if (tpl_base_agg && tpl_base_agg->is_template()) {
                    auto* base_ti = tpl_base_agg->get_tpl_info();
                    if (base_ti) {
                        // Parse comma-separated arg names and resolve to types
                        std::vector<std::string> arg_names;
                        {
                            size_t pos = 0;
                            while (pos < args_str.size()) {
                                auto comma = args_str.find(',', pos);
                                std::string arg = (comma == std::string::npos)
                                    ? args_str.substr(pos)
                                    : args_str.substr(pos, comma - pos);
                                while (!arg.empty() && arg.front() == ' ') arg.erase(arg.begin());
                                while (!arg.empty() && arg.back() == ' ') arg.pop_back();
                                arg_names.push_back(arg);
                                pos = (comma == std::string::npos) ? args_str.size() : comma + 1;
                            }
                        }

                        // Resolve each arg name to a model type
                        std::vector<template_argument> base_args;
                        bool all_resolved = true;
                        static const std::unordered_map<std::string, primitive_type::PRIMITIVE_TYPE> prim_map = {
                            {"bool", primitive_type::BOOL}, {"byte", primitive_type::BYTE},
                            {"char", primitive_type::CHAR}, {"short", primitive_type::SHORT},
                            {"int", primitive_type::INT}, {"long", primitive_type::LONG},
                            {"float", primitive_type::FLOAT}, {"double", primitive_type::DOUBLE},
                        };
                        for (const auto& arg_name : arg_names) {
                            std::shared_ptr<type> arg_type;
                            auto prim_it = prim_map.find(arg_name);
                            if (prim_it != prim_map.end()) {
                                arg_type = ctx->from_type(prim_it->second);
                            } else {
                                // Try as user-defined aggregate type
                                auto agg = parent_ns->get_aggregate(arg_name);
                                if (agg && agg->get_struct_type()) {
                                    arg_type = agg->get_struct_type();
                                }
                            }
                            if (arg_type && type::is_resolved(arg_type)) {
                                base_args.push_back(template_argument::make_type(arg_type));
                            } else {
                                all_resolved = false;
                                break;
                            }
                        }

                        if (all_resolved && !base_args.empty()) {
                            bs.base = instantiate_aggregate(
                                *tpl_base_agg, base_args, parent_ns, unit, ctx, logger);
                            // Ensure the instantiated base has a struct_type
                            if (bs.base && !bs.base->get_struct_type()) {
                                auto st = std::make_shared<struct_type>(
                                    bs.base->get_short_name(), bs.base->shared_as<aggregate>());
                                ctx->add_struct(st);
                                bs.base->set_struct_type(st);
                            }
                        }
                    }
                }
            }
        } else {
            // Simple (non-template) base name: look up directly
            if (auto found = parent_ns->get_aggregate(bs.raw_name)) {
                bs.base = found;
            }
        }
    }

    // 2. Clone children from the template aggregate
    for (auto& child : tpl_def.get_children()) {
        if (auto mv = std::dynamic_pointer_cast<member_variable_definition>(child)) {
            clone_member_variable(*mv, concrete, subst, val_subst);
        } else if (auto ctor = std::dynamic_pointer_cast<constructor>(child)) {
            clone_constructor(*ctor, concrete, subst, val_subst);
        } else if (auto dtor = std::dynamic_pointer_cast<destructor>(child)) {
            clone_destructor(*dtor, concrete, subst, val_subst);
        } else if (auto fn = std::dynamic_pointer_cast<function>(child)) {
            clone_method(*fn, concrete, subst, val_subst);
        } else if (auto gv = std::dynamic_pointer_cast<global_variable_definition>(child)) {
            // Static member variable — clone similarly to member variable
            auto new_var = concrete->append_variable(gv->get_short_name(), /*is_static=*/true);
            if (new_var) {
                auto src_type = std::const_pointer_cast<type>(gv->get_type());
                new_var->set_type(substitute_type(src_type, subst));
                new_var->set_const(gv->is_const());
                if (gv->get_init_expr()) {
                    auto cloned_init = clone_and_substitute_expr(
                        std::const_pointer_cast<expression>(gv->get_init_expr()), subst, val_subst);
                    new_var->set_init_expr(cloned_init);
                    retarget_init_expr(cloned_init, new_var);
                }
                if (auto gv_new = std::dynamic_pointer_cast<global_variable_definition>(new_var)) {
                    gv_new->set_visibility(gv->get_visibility());
                }
            }
        } else if (auto inner = std::dynamic_pointer_cast<aggregate>(child)) {
            // Clone nested aggregate types (structs/classes defined inside the template)
            clone_nested_aggregate(*inner, concrete, subst, val_subst, ctx);
        } else if (auto inner_un = std::dynamic_pointer_cast<union_type_def>(child)) {
            // Clone nested union types (e.g. Expected<R,E>::Storage)
            clone_nested_union(*inner_un, concrete, subst, ctx);
        }
        // Enums and using declarations — handled as needed in future
    }

    // 3. Post-instantiation: generate default constructor and set up this parameters
    //    The concrete aggregate was not visited by symbol_resolver, so it needs
    //    these essential setup steps that symbol_resolver::visit_aggregate normally does.

    // 3a. Generate a default constructor if no explicit constructor was cloned
    if (concrete->constructors().empty()) {
        auto default_ctor = constructor::make_shared(concrete->shared_as<aggregate>());
        default_ctor->set_compiler_generated(true);
        concrete->_constructors.push_back(default_ctor);
        concrete->_children.push_back(default_ctor);
    }

    // 3b. Set up 'this' parameters for all member functions and constructors
    //     (requires struct_type to be set — callers must ensure this after creating the struct_type)

    // 3c. Update mangled names (if possible — struct_type may not be set yet)
    concrete->update_mangled_name();

    // 4. Register in the instantiation cache
    ti->instantiations[key] = concrete;

    return concrete;
}

std::shared_ptr<aggregate> template_instantiator::synthesize_generic_aggregate(
    aggregate& tpl_def,
    std::shared_ptr<ns> parent_ns,
    k::model::unit& unit,
    std::shared_ptr<context> ctx,
    k::log::logger& logger)
{
    auto* ti = tpl_def.get_tpl_info();
    if (!ti || !ti->is_generic) return nullptr;

    auto cached = ti->instantiations.find(generic_synthesis_key);
    if (cached != ti->instantiations.end()) {
        if (auto* agg_ptr = std::get_if<std::shared_ptr<aggregate>>(&cached->second)) {
            return *agg_ptr;
        }
    }

    const std::string base_name = tpl_def.get_short_name();
    auto subst = build_generic_substitution_map(*ti, ctx);
    value_substitution_map val_subst;

    std::shared_ptr<aggregate> concrete;
    if (std::dynamic_pointer_cast<model::interface>(tpl_def.shared_as<element>())) {
        concrete = parent_ns->define_interface(base_name);
    } else if (tpl_def.is_class()) {
        concrete = parent_ns->define_class(base_name);
    } else if (tpl_def.is_annotation()) {
        concrete = parent_ns->define_annotation(base_name);
    } else {
        concrete = parent_ns->define_structure(base_name);
    }
    if (!concrete) return nullptr;

    (void)unit;
    (void)logger;

    concrete->set_final(tpl_def.is_final());
    concrete->set_abstract(tpl_def.is_abstract());
    concrete->set_const_struct(tpl_def.is_const_struct());
    concrete->set_visibility(tpl_def.get_visibility());
    concrete->_ast_node = tpl_def.get_ast_node();

    // Keep the synthesized symbol on the base aggregate name (no arg suffix).

    for (auto& bs : tpl_def.get_bases()) {
        concrete->add_base(substitute_base_name(bs.raw_name, subst), bs.vis);
    }

    for (auto& child : tpl_def.get_children()) {
        if (auto mv = std::dynamic_pointer_cast<member_variable_definition>(child)) {
            clone_member_variable(*mv, concrete, subst, val_subst);
        } else if (auto ctor = std::dynamic_pointer_cast<constructor>(child)) {
            clone_constructor(*ctor, concrete, subst, val_subst);
        } else if (auto dtor = std::dynamic_pointer_cast<destructor>(child)) {
            clone_destructor(*dtor, concrete, subst, val_subst);
        } else if (auto fn = std::dynamic_pointer_cast<function>(child)) {
            clone_method(*fn, concrete, subst, val_subst);
        } else if (auto gv = std::dynamic_pointer_cast<global_variable_definition>(child)) {
            auto new_var = concrete->append_variable(gv->get_short_name(), /*is_static=*/true);
            if (new_var) {
                auto src_type = std::const_pointer_cast<type>(gv->get_type());
                new_var->set_type(substitute_type(src_type, subst));
                new_var->set_const(gv->is_const());
                if (gv->get_init_expr()) {
                    auto cloned_init = clone_and_substitute_expr(
                        std::const_pointer_cast<expression>(gv->get_init_expr()), subst, val_subst);
                    new_var->set_init_expr(cloned_init);
                    retarget_init_expr(cloned_init, new_var);
                }
                if (auto gv_new = std::dynamic_pointer_cast<global_variable_definition>(new_var)) {
                    gv_new->set_visibility(gv->get_visibility());
                }
            }
        } else if (auto inner = std::dynamic_pointer_cast<aggregate>(child)) {
            // Clone nested aggregate types (e.g. private Node struct inside LinkedList)
            clone_nested_aggregate(*inner, concrete, subst, val_subst);
        } else if (auto inner_un = std::dynamic_pointer_cast<union_type_def>(child)) {
            // Clone nested union types
            clone_nested_union(*inner_un, concrete, subst, ctx);
        }
    }

    if (concrete->constructors().empty()) {
        auto default_ctor = constructor::make_shared(concrete->shared_as<aggregate>());
        default_ctor->set_compiler_generated(true);
        concrete->_constructors.push_back(default_ctor);
        concrete->_children.push_back(default_ctor);
    }

    concrete->update_mangled_name();
    ti->instantiations[generic_synthesis_key] = concrete;
    return concrete;
}

// ═══════════════════════════════════════════════════════════════════════════
// Instantiation: function
// ═══════════════════════════════════════════════════════════════════════════

std::shared_ptr<function> template_instantiator::instantiate_function(
    function& tpl_def,
    const std::vector<template_argument>& args,
    std::shared_ptr<ns> parent_ns,
    k::model::unit& unit,
    std::shared_ptr<context> ctx,
    k::log::logger& logger)
{
    auto* ti = tpl_def.get_tpl_info();
    if (!ti) return nullptr;

    // Check instantiation cache
    std::string key = build_instantiation_key(args);
    auto it = ti->instantiations.find(key);
    if (it != ti->instantiations.end()) {
        if (auto* fn_ptr = std::get_if<std::shared_ptr<function>>(&it->second)) {
            return *fn_ptr;
        }
    }

    // Build the instantiated name
    std::string base_name = tpl_def.get_short_name();
    std::string inst_name = build_instantiated_name(base_name, args);

    // Build type substitution map
    auto subst = build_substitution_map(*ti, args);
    auto val_subst = build_value_substitution_map(*ti, args);
    auto pack_subst = build_pack_substitution_map(*ti, args);

    // 1. Create a new concrete function in the parent namespace
    auto concrete = parent_ns->define_function(inst_name, tpl_def.is_static());
    if (!concrete) return nullptr;

    // Copy flags
    concrete->set_visibility(tpl_def.get_visibility());
    concrete->set_const_member(tpl_def.is_const_member());
    concrete->set_operator(tpl_def.is_operator());
    concrete->set_aliasing(tpl_def.get_aliasing());
    concrete->set_compiler_generated(tpl_def.is_compiler_generated());

    // 2. Populate from template (params, return type, body)
    populate_function_from_template(concrete, tpl_def, subst, val_subst, pack_subst);

    // Store template instantiation info for mangling (I…E encoding)
    concrete->set_tpl_instantiation_info(base_name, args);

    // Store the type substitution map so that type_reference_resolver can
    // resolve template aggregate types (e.g. Expected<R,E>) used inside the
    // function body where original template params are no longer in scope.
    concrete->set_tpl_instantiation_subst(subst);

    // 3. Register in the instantiation cache
    ti->instantiations[key] = concrete;

    return concrete;
}

// ═══════════════════════════════════════════════════════════════════════════
// Post-instantiation symbol resolution for method bodies
// ═══════════════════════════════════════════════════════════════════════════

// Walk an expression tree and resolve unresolved symbol_expression nodes
// by climbing the element parent chain (block → function → aggregate).
// This mimics what symbol_resolver::resolve_symbol does for simple names.
static void resolve_symbols_in_expr(const std::shared_ptr<expression>& expr) {
    if (!expr) return;

    if (auto sym = std::dynamic_pointer_cast<symbol_expression>(expr)) {
        if (!sym->is_resolved() && sym->get_name().size() == 1
            && !sym->get_name().has_root_prefix()) {
            const std::string& var_name = sym->get_name().front();
            // Walk up the element parent chain
            for (auto cur = sym->parent<element>(); cur;
                 cur = cur->parent<element>()) {
                // Check variable holders (block locals, aggregate members)
                if (auto* vh = dynamic_cast<variable_holder*>(cur.get())) {
                    if (auto var = vh->get_variable(var_name)) {
                        sym->set_target(var);
                        break;
                    }
                }
                // Check function parameters
                if (auto blk = std::dynamic_pointer_cast<block>(cur)) {
                    if (auto fn = blk->get_direct_function()) {
                        if (auto param = fn->get_parameter(var_name)) {
                            sym->set_target(
                                std::const_pointer_cast<parameter>(param));
                            break;
                        }
                    }
                }
                // Also check inherited members when reaching an aggregate
                if (auto agg = std::dynamic_pointer_cast<aggregate>(cur)) {
                    std::queue<std::shared_ptr<aggregate>> base_queue;
                    for (auto& bs : agg->get_bases()) {
                        if (bs.base) base_queue.push(bs.base);
                    }
                    bool found = false;
                    while (!base_queue.empty()) {
                        auto base = base_queue.front();
                        base_queue.pop();
                        if (auto var = base->get_variable(var_name)) {
                            sym->set_target(var);
                            found = true;
                            break;
                        }
                        for (auto& bs : base->get_bases()) {
                            if (bs.base) base_queue.push(bs.base);
                        }
                    }
                    if (found) break;
                }
            }
        }
    }

    // Recurse into sub-expressions
    if (auto be = std::dynamic_pointer_cast<binary_expression>(expr)) {
        resolve_symbols_in_expr(be->left());
        resolve_symbols_in_expr(be->right());
    } else if (auto ue = std::dynamic_pointer_cast<unary_expression>(expr)) {
        resolve_symbols_in_expr(
            std::const_pointer_cast<expression>(ue->sub_expr()));
    } else if (auto fie =
                   std::dynamic_pointer_cast<function_invocation_expression>(
                       expr)) {
        resolve_symbols_in_expr(
            std::const_pointer_cast<expression>(fie->callee_expr()));
        for (auto& arg : fie->arguments()) {
            resolve_symbols_in_expr(
                std::const_pointer_cast<expression>(arg));
        }
    } else if (auto cie =
                   std::dynamic_pointer_cast<constructor_invocation_expression>(
                       expr)) {
        for (auto& arg : cie->arguments()) {
            resolve_symbols_in_expr(
                std::const_pointer_cast<expression>(arg));
        }
    } else if (auto dsie =
                   std::dynamic_pointer_cast<designated_struct_init_expression>(
                       expr)) {
        for (auto& mi : dsie->members_mutable()) {
            if (mi.value) resolve_symbols_in_expr(mi.value);
            for (auto& a : mi.args) resolve_symbols_in_expr(a);
        }
    } else if (auto tce =
                   std::dynamic_pointer_cast<temporary_construction_expression>(
                       expr)) {
        for (auto& arg : tce->arguments()) {
            resolve_symbols_in_expr(
                std::const_pointer_cast<expression>(arg));
        }
    } else if (auto ne = std::dynamic_pointer_cast<new_expression>(expr)) {
        for (auto& arg : ne->arguments()) {
            resolve_symbols_in_expr(
                std::const_pointer_cast<expression>(arg));
        }
        resolve_symbols_in_expr(
            std::const_pointer_cast<expression>(ne->array_size_expr()));
    } else if (auto de = std::dynamic_pointer_cast<delete_expression>(expr)) {
        resolve_symbols_in_expr(
            std::const_pointer_cast<expression>(de->sub_expr()));
    } else if (auto ce = std::dynamic_pointer_cast<cast_expression>(expr)) {
        resolve_symbols_in_expr(
            std::const_pointer_cast<expression>(ce->sub_expr()));
    } else if (auto aie =
                   std::dynamic_pointer_cast<array_init_expression>(expr)) {
        for (auto& elem : aie->elements()) {
            resolve_symbols_in_expr(
                std::const_pointer_cast<expression>(elem));
        }
    }
}

// Walk a statement tree and resolve unresolved symbols in all expressions.
static void resolve_symbols_in_stmt(const std::shared_ptr<statement>& stmt) {
    if (!stmt) return;

    if (auto rs = std::dynamic_pointer_cast<return_statement>(stmt)) {
        if (rs->get_expression())
            resolve_symbols_in_expr(rs->get_expression());
    } else if (auto es = std::dynamic_pointer_cast<expression_statement>(stmt)) {
        if (es->get_expression())
            resolve_symbols_in_expr(es->get_expression());
    } else if (auto vs = std::dynamic_pointer_cast<variable_statement>(stmt)) {
        // Compute the fully-qualified name for the variable (equivalent to
        // symbol_resolver::visit_named_element) so that codegen can find it.
        if (vs->get_fq_name().empty() && !vs->get_short_name().empty()) {
            if (auto ancestor = vs->ancestor<named_element>()) {
                vs->assign_name(ancestor->get_name().with_back(vs->get_short_name()));
            }
        }
        if (vs->get_init_expr())
            resolve_symbols_in_expr(
                std::const_pointer_cast<expression>(vs->get_init_expr()));
    } else if (auto ies = std::dynamic_pointer_cast<if_else_statement>(stmt)) {
        if (ies->has_cond_var()) {
            resolve_symbols_in_stmt(ies->get_cond_var());
        }
        if (ies->get_test_expr())
            resolve_symbols_in_expr(
                std::const_pointer_cast<expression>(ies->get_test_expr()));
        resolve_symbols_in_stmt(ies->get_then_stmt());
        resolve_symbols_in_stmt(ies->get_else_stmt());
    } else if (auto ws = std::dynamic_pointer_cast<while_statement>(stmt)) {
        if (ws->get_test_expr())
            resolve_symbols_in_expr(
                std::const_pointer_cast<expression>(ws->get_test_expr()));
        resolve_symbols_in_stmt(ws->get_nested_stmt());
    } else if (auto fs = std::dynamic_pointer_cast<for_statement>(stmt)) {
        if (fs->get_decl_stmt())
            resolve_symbols_in_stmt(fs->get_decl_stmt());
        if (fs->get_test_expr())
            resolve_symbols_in_expr(
                std::const_pointer_cast<expression>(fs->get_test_expr()));
        if (fs->get_step_expr())
            resolve_symbols_in_expr(
                std::const_pointer_cast<expression>(fs->get_step_expr()));
        resolve_symbols_in_stmt(fs->get_nested_stmt());
    } else if (auto blk = std::dynamic_pointer_cast<block>(stmt)) {
        for (auto& s : blk->get_statements()) {
            resolve_symbols_in_stmt(s);
        }
    }
}

void template_instantiator::resolve_body_symbols(
    std::shared_ptr<aggregate> concrete)
{
    if (!concrete) return;

    // ── Assign fully-qualified names to all children (functions, nested aggregates)
    //    and their local variable statements.  The symbol_resolver normally does this,
    //    but template-instantiated aggregates are created after that pass finishes.
    auto assign_names_recursive = [](aggregate& agg) {
        for (auto& child : agg.get_children()) {
            if (auto fn = std::dynamic_pointer_cast<function>(child)) {
                // Assign FQ name to the function itself
                if (fn->get_fq_name().empty() && !fn->get_short_name().empty()) {
                    if (auto anc = fn->ancestor<named_element>()) {
                        fn->assign_name(anc->get_name().with_back(fn->get_short_name()));
                    }
                }
                // Assign FQ names to parameters
                for (auto& param : fn->parameters()) {
                    if (param && param->get_fq_name().empty() && !param->get_short_name().empty()) {
                        param->assign_name(fn->get_name().with_back(param->get_short_name()));
                    }
                }
            }
        }
    };
    assign_names_recursive(*concrete);
    for (auto& child : concrete->get_children()) {
        if (auto nested = std::dynamic_pointer_cast<aggregate>(child)) {
            assign_names_recursive(*nested);
        }
    }

    auto resolve_fn_bodies = [](aggregate& agg) {
        for (auto& child : agg.get_children()) {
            if (auto fn = std::dynamic_pointer_cast<function>(child)) {
                auto blk = fn->_block;
                if (blk) {
                    for (auto& stmt : blk->get_statements()) {
                        resolve_symbols_in_stmt(stmt);
                    }
                }
            }
        }
    };

    resolve_fn_bodies(*concrete);

    // Also resolve body symbols in nested aggregates
    for (auto& child : concrete->get_children()) {
        if (auto nested = std::dynamic_pointer_cast<aggregate>(child)) {
            resolve_fn_bodies(*nested);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Post-instantiation: inject constructor member-initializer expressions
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Recursively walk an expression tree and re-target any symbol_expression that
 * references a parameter definition to point at the matching parameter in
 * @p param_by_name (by short name).  This is needed because cloned member-init
 * arg expressions still point at the template constructor's parameters.
 */
static void retarget_param_refs(
    std::shared_ptr<expression>& expr,
    const std::unordered_map<std::string, std::shared_ptr<parameter>>& param_by_name)
{
    if (!expr) return;

    if (auto sym = std::dynamic_pointer_cast<symbol_expression>(expr)) {
        if (sym->is_variable_def()) {
            auto vd = sym->get_variable_def();
            if (auto pd = std::dynamic_pointer_cast<parameter>(vd)) {
                auto it = param_by_name.find(pd->get_short_name());
                if (it != param_by_name.end()) {
                    expr = symbol_expression::from_variable(it->second);
                }
            }
        } else if (!sym->is_resolved()) {
            // Unresolved name — try to match against a concrete param
            const auto& nm = sym->get_name();
            if (nm.size() == 1 && !nm.has_root_prefix()) {
                auto it = param_by_name.find(nm.front());
                if (it != param_by_name.end()) {
                    expr = symbol_expression::from_variable(it->second);
                }
            }
        }
        return;
    }

    // Recurse into sub-expressions
    if (auto be = std::dynamic_pointer_cast<binary_expression>(expr)) {
        retarget_param_refs(be->left(), param_by_name);
        retarget_param_refs(be->right(), param_by_name);
    } else if (auto ue = std::dynamic_pointer_cast<unary_expression>(expr)) {
        auto sub = std::const_pointer_cast<expression>(ue->sub_expr());
        retarget_param_refs(sub, param_by_name);
    } else if (auto fie = std::dynamic_pointer_cast<function_invocation_expression>(expr)) {
        for (auto& a : fie->arguments()) {
            auto mut = std::const_pointer_cast<expression>(a);
            retarget_param_refs(mut, param_by_name);
        }
    } else if (auto cie = std::dynamic_pointer_cast<constructor_invocation_expression>(expr)) {
        for (auto& a : cie->arguments()) {
            auto mut = std::const_pointer_cast<expression>(a);
            retarget_param_refs(mut, param_by_name);
        }
    }
}

void template_instantiator::inject_constructor_member_inits(std::shared_ptr<aggregate> concrete) {
    if (!concrete) return;

    auto inject_for_aggregate = [](std::shared_ptr<aggregate> agg) {
        for (auto& ctor : agg->constructors()) {
            if (!ctor || ctor->is_compiler_generated()) continue;
            if (ctor->member_inits().empty()) continue;

            auto blck = ctor->get_block();
            if (!blck) continue;

            // Build a lookup map from member name to mem_init_spec
            std::unordered_map<std::string, const constructor::member_init_spec*> init_by_name;
            for (auto& mi : ctor->member_inits()) {
                if (!mi.is_base_init) init_by_name[mi.member_name] = &mi;
            }

            // Build a map from old parameter names to new concrete parameters for re-targeting.
            std::unordered_map<std::string, std::shared_ptr<parameter>> param_by_name;
            for (auto& p : ctor->parameters()) {
                if (p == ctor->get_this_parameter()) continue;
                param_by_name[p->get_short_name()] = p;
            }

            // Insert member-init statements at the front of the block, in member
            // declaration order (same logic as symbol_resolver::visit_constructor step 2).
            size_t insert_idx = 0;
            for (auto& var_entry : agg->variables()) {
                if (auto var = std::dynamic_pointer_cast<member_variable_definition>(var_entry.second)) {
                    // Skip synthetic fields
                    if (var->get_short_name() == "__parent__") continue;
                    if (var->get_short_name().rfind("__base_", 0) == 0) continue;
                    if (var->get_short_name().rfind("__vbptr_", 0) == 0) continue;
                    if (var->get_short_name().rfind("__vbase_", 0) == 0) continue;
                    if (var->get_short_name().rfind("__vptr", 0) == 0) continue;

                    auto it = init_by_name.find(var->get_short_name());
                    if (it == init_by_name.end()) continue;
                    const auto& mi = *it->second;

                    std::vector<std::shared_ptr<expression>> args;
                    args.reserve(mi.args.size());
                    for (auto& arg : mi.args) {
                        auto cloned = arg->clone();
                        retarget_param_refs(cloned, param_by_name);
                        args.push_back(cloned);
                    }
                    auto init_expr = constructor_invocation_expression::make_shared(var, args);
                    auto stmt = std::make_shared<expression_statement>(blck);
                    stmt->set_expression(init_expr);
                    auto pos = blck->begin();
                    std::advance(pos, insert_idx);
                    blck->insert_statement(pos, stmt);
                    ++insert_idx;
                }
            }
        }
    };

    inject_for_aggregate(concrete);

    // Also inject for nested aggregates
    for (auto& child : concrete->get_children()) {
        if (auto nested = std::dynamic_pointer_cast<aggregate>(child)) {
            inject_for_aggregate(nested);
        }
    }
}

} // namespace k::model


// ═══════════════════════════════════════════════════════════════════════════
// instantiate_union
// ═══════════════════════════════════════════════════════════════════════════

namespace k::model {

std::shared_ptr<union_type_def> template_instantiator::instantiate_union(
    union_type_def& tpl_def,
    const std::vector<template_argument>& args,
    std::shared_ptr<ns> parent_ns,
    k::model::unit& unit,
    std::shared_ptr<context> ctx,
    k::log::logger& logger)
{
    auto* ti = tpl_def.get_tpl_info();
    if (!ti) return nullptr;

    // Check instantiation cache
    std::string key = build_instantiation_key(args);
    auto it = ti->instantiations.find(key);
    if (it != ti->instantiations.end()) {
        if (auto* un_ptr = std::get_if<std::shared_ptr<union_type_def>>(&it->second)) {
            return *un_ptr;
        }
    }

    // Build the instantiated name
    std::string base_name = tpl_def.get_short_name();
    std::string inst_name = build_instantiated_name(base_name, args);

    // Build type substitution map
    auto subst = build_substitution_map(*ti, args);
    auto val_subst = build_value_substitution_map(*ti, args);

    // Create a new concrete union in the parent namespace
    auto concrete = parent_ns->define_union(inst_name);
    if (!concrete) return nullptr;

    concrete->set_visibility(tpl_def.get_visibility());

    // Copy alternatives with type substitution
    for (const auto& alt : tpl_def.alternatives()) {
        std::string raw = alt.raw_type_name;
        // Apply type substitution to the raw_type_name (for template params like "T")
        auto subst_it = subst.find(raw);
        if (subst_it != subst.end() && subst_it->second) {
            raw = subst_it->second->to_string();
        }
        concrete->add_alternative(alt.name, raw, alt.is_const);

        // Substitute the resolved type as well
        auto& new_alt = concrete->alternatives_mutable().back();
        if (alt.resolved_type) {
            new_alt.resolved_type = substitute_type(alt.resolved_type, subst);
        } else if (!raw.empty()) {
            // Try to get model type from substitution map or context
            auto from_ctx = ctx->from_string(raw);
            if (from_ctx) new_alt.resolved_type = from_ctx;
        }
    }

    // Store template instantiation info for mangling
    concrete->set_tpl_instantiation_info(base_name, args);

    // Cache the instantiation
    ti->instantiations[key] = concrete;

    // Assign FQ name
    if (concrete->get_fq_name().empty() && !concrete->get_short_name().empty()) {
        if (auto ancestor = concrete->template ancestor<named_element>()) {
            concrete->assign_name(ancestor->get_name().with_back(concrete->get_short_name()));
        }
    }
    concrete->update_mangled_name();

    return concrete;
}

} // namespace k::model



