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

#include "resolvers_generic.hpp"
#include "../errors.hpp"
#include "../model/model.hpp"
#include "../model/model_visitor.hpp"
#include "../model/expressions.hpp"
#include "../model/statements.hpp"
#include "../model/type.hpp"
#include "../parse/ast.hpp"

namespace k::model::gen {

namespace {

std::string find_direct_generic_usage(const std::shared_ptr<type>& t,
                                      const std::unordered_set<std::string>& pnames,
                                      bool behind_addresser) {
    if (t == nullptr) return "";

    if (auto unresolved = std::dynamic_pointer_cast<unresolved_type>(t)) {
        const auto& id = unresolved->type_id();
        if (id.has_root_prefix() || id.empty()) return "";
        const std::string& simple_name = id[id.size() - 1];
        if (behind_addresser == false && pnames.count(simple_name)) return simple_name;
        return "";
    }

    if (type::is_any_indirection(t)) {
        return find_direct_generic_usage(t->get_subtype(), pnames, true);
    }

    if (auto const_t = std::dynamic_pointer_cast<const_type>(t)) {
        return find_direct_generic_usage(const_t->get_inner_type(), pnames, behind_addresser);
    }

    if (auto array_t = std::dynamic_pointer_cast<array_type>(t)) {
        return find_direct_generic_usage(array_t->get_subtype(), pnames, behind_addresser);
    }

    if (auto fn_ref_t = std::dynamic_pointer_cast<callable_type>(t)) {
        auto ret_err = find_direct_generic_usage(fn_ref_t->get_return_type(), pnames, behind_addresser);
        if (ret_err.empty() == false) return ret_err;
        for (const auto& ptype : fn_ref_t->get_parameter_types()) {
            auto p_err = find_direct_generic_usage(ptype, pnames, behind_addresser);
            if (p_err.empty() == false) return p_err;
        }
        return "";
    }

    if (auto unresolved_fn_ref_t = std::dynamic_pointer_cast<unresolved_callable_type>(t)) {
        for (const auto& ptype : unresolved_fn_ref_t->parameter_types()) {
            auto p_err = find_direct_generic_usage(ptype, pnames, behind_addresser);
            if (p_err.empty() == false) return p_err;
        }
        return "";
    }

    return "";
}

std::shared_ptr<type> strip_const_layers(std::shared_ptr<type> t) {
    while (auto const_t = std::dynamic_pointer_cast<const_type>(t)) {
        t = const_t->get_inner_type();
    }
    return t;
}

/**
 * Find an owner ('!') or drain ('#') addresser applied to a generic type
 * parameter, anywhere in the type. Returns the parameter name (and sets
 * @p is_drain) or an empty string.
 */
std::string find_owner_or_drain_of_param(
    const std::shared_ptr<type>& t,
    const std::unordered_set<std::string>& pnames,
    bool& is_drain) {
    if (t == nullptr) return "";

    const bool own = std::dynamic_pointer_cast<owner_type>(t) != nullptr;
    const bool drn = std::dynamic_pointer_cast<drain_type>(t) != nullptr;
    if (own || drn) {
        auto sub = strip_const_layers(t->get_subtype());
        if (auto unresolved = std::dynamic_pointer_cast<unresolved_type>(sub)) {
            const auto& id = unresolved->type_id();
            if (id.has_root_prefix() == false && id.empty() == false) {
                const std::string& simple_name = id[id.size() - 1];
                if (pnames.count(simple_name)) {
                    is_drain = drn;
                    return simple_name;
                }
            }
        }
    }

    if (type::is_any_indirection(t)) {
        return find_owner_or_drain_of_param(t->get_subtype(), pnames, is_drain);
    }

    if (auto const_t = std::dynamic_pointer_cast<const_type>(t)) {
        return find_owner_or_drain_of_param(const_t->get_inner_type(), pnames, is_drain);
    }

    if (auto array_t = std::dynamic_pointer_cast<array_type>(t)) {
        return find_owner_or_drain_of_param(array_t->get_subtype(), pnames, is_drain);
    }

    if (auto fn_ref_t = std::dynamic_pointer_cast<callable_type>(t)) {
        auto ret_err = find_owner_or_drain_of_param(fn_ref_t->get_return_type(), pnames, is_drain);
        if (ret_err.empty() == false) return ret_err;
        for (const auto& ptype : fn_ref_t->get_parameter_types()) {
            auto p_err = find_owner_or_drain_of_param(ptype, pnames, is_drain);
            if (p_err.empty() == false) return p_err;
        }
        return "";
    }

    if (auto unresolved_fn_ref_t = std::dynamic_pointer_cast<unresolved_callable_type>(t)) {
        for (const auto& ptype : unresolved_fn_ref_t->parameter_types()) {
            auto p_err = find_owner_or_drain_of_param(ptype, pnames, is_drain);
            if (p_err.empty() == false) return p_err;
        }
        return "";
    }

    return "";
}

/**
 * True when @p t designates an erased generic type parameter behind one or
 * more addressers or array levels (e.g. 'T*', 'T&', 'T?', 'T+', 'T[]').
 */
bool is_erased_param_type(std::shared_ptr<type> t,
                          const std::unordered_set<std::string>& pnames) {
    while (t != nullptr) {
        t = strip_const_layers(t);
        if (type::is_any_indirection(t) || std::dynamic_pointer_cast<array_type>(t)) {
            t = t->get_subtype();
            continue;
        }
        break;
    }
    auto unresolved = std::dynamic_pointer_cast<unresolved_type>(t);
    if (unresolved == nullptr) return false;
    const auto& id = unresolved->type_id();
    if (id.has_root_prefix() || id.empty()) return false;
    return pnames.count(id[id.size() - 1]) != 0;
}

} // namespace

void generic_constraint_validator::validate() {
    auto root = _unit.get_root_namespace();
    if (root) root->accept(*this);
}

void generic_constraint_validator::visit_unit(k::model::unit& u) {
    auto root = u.get_root_namespace();
    if (root) root->accept(*this);
}

void generic_constraint_validator::visit_namespace(k::model::ns& n) {
    for (auto& fn : n.functions()) {
        if (fn && fn->is_generic()) {
            auto* ti = fn->get_tpl_info();
            std::unordered_set<std::string> pnames;
            for (auto& p : ti->params) { pnames.insert(p.name); }
            validate_generic_function(*fn, pnames, ti->params);
        }
    }
    for (size_t i = 0; i < n.get_children().size(); ++i) {
        n.get_children()[i]->accept(*this);
    }
}

void generic_constraint_validator::visit_aggregate(k::model::aggregate& agg) {
    if (agg.is_generic()) {
        validate_generic_aggregate(agg);
    } else {
        // Generic methods of a non-generic host aggregate.
        for (auto& child : agg.get_children()) {
            auto fn_elem = std::dynamic_pointer_cast<function>(child);
            if (fn_elem == nullptr || fn_elem->is_generic() == false) continue;
            auto* fn_ti = fn_elem->get_tpl_info();
            if (fn_ti == nullptr) continue;
            std::unordered_set<std::string> fn_pnames;
            for (auto& p : fn_ti->params) { fn_pnames.insert(p.name); }
            validate_generic_function(*fn_elem, fn_pnames, fn_ti->params);
        }
    }
    for (auto& child : agg.get_children()) {
        child->accept(*this);
    }
}

void generic_constraint_validator::visit_structure(k::model::structure& s) {
    visit_aggregate(s);
}

void generic_constraint_validator::visit_klass(k::model::klass& k) {
    visit_aggregate(k);
}

void generic_constraint_validator::visit_interface(k::model::interface& i) {
    visit_aggregate(i);
}

void generic_constraint_validator::validate_generic_aggregate(aggregate& agg) {
    auto* ti = agg.get_tpl_info();
    if (ti == nullptr) return;

    std::unordered_set<std::string> pnames;
    for (auto& p : ti->params) { pnames.insert(p.name); }

    // Fallback location: the aggregate's own declaration name, used when a more
    // precise location (e.g. a member's own declaration) is not tracked in the model.
    lex::opt_any_lexeme agg_lexeme;
    if (auto ast_ad = agg.get_ast_aggregate_decl()) agg_lexeme = lex::any_lexeme{ast_ad->name};

    _member_scope.clear();
    for (auto& child : agg.get_children()) {
        if (auto mv = std::dynamic_pointer_cast<member_variable_definition>(child)) {
            _member_scope[mv->get_short_name()] = is_erased_param_type(mv->get_type(), pnames);
        }
    }

    for (auto& child : agg.get_children()) {
        auto mv = std::dynamic_pointer_cast<member_variable_definition>(child);
        if (mv) {
            const std::string ctx = "member '" + mv->get_short_name() + "'";
            validate_type_usage(mv->get_type(), ctx, pnames, ti->params, agg_lexeme);
            continue;
        }
        auto fn_elem = std::dynamic_pointer_cast<function>(child);
        if (fn_elem) {
            if (fn_elem->is_template() && fn_elem->is_generic() == false) continue;

            auto fn_param_names = pnames;
            auto fn_param_descs = ti->params;
            if (fn_elem->is_generic()) {
                if (auto* fn_ti = fn_elem->get_tpl_info()) {
                    merge_template_context(fn_ti->params, fn_param_names, fn_param_descs);
                }
            }

            validate_generic_function(*fn_elem, fn_param_names, fn_param_descs);
        }
    }

    for (auto& ctor : agg.constructors()) {
        if (ctor == nullptr) continue;
        lex::opt_any_lexeme ctor_lexeme = agg_lexeme;
        if (auto ast_fd = ctor->get_ast_function_decl()) ctor_lexeme = lex::any_lexeme{ast_fd->name};
        for (size_t i = 0; i < ctor->get_parameter_size(); ++i) {
            auto param = ctor->get_parameter(i);
            if (param == nullptr) continue;
            const std::string ctx = "constructor parameter '" + param->get_short_name() + "'";
            lex::opt_any_lexeme param_lexeme = ctor_lexeme;
            if (auto ast_ps = param->get_ast_parameter_spec(); ast_ps && ast_ps->name) {
                param_lexeme = lex::any_lexeme{*ast_ps->name};
            }
            validate_type_usage(param->get_type(), ctx, pnames, ti->params, param_lexeme);
        }

        push_scope();
        for (size_t i = 0; i < ctor->get_parameter_size(); ++i) {
            if (auto param = ctor->get_parameter(i)) {
                declare_local(param->get_short_name(), param->get_type(), pnames);
            }
        }
        validate_statement_tree(ctor->get_existing_block(), pnames, ti->params);
        pop_scope();
    }
    _member_scope.clear();
}

void generic_constraint_validator::validate_generic_function(
    function& fn,
    const std::unordered_set<std::string>& pnames,
    const std::vector<template_param_descriptor>& pdescs)
{
    lex::opt_any_lexeme fn_lexeme;
    if (auto ast_fd = fn.get_ast_function_decl()) fn_lexeme = lex::any_lexeme{ast_fd->name};

    if (auto ret = fn.get_return_type()) {
        const std::string ctx = "return type of '" + fn.get_short_name() + "'";
        validate_type_usage(ret, ctx, pnames, pdescs, fn_lexeme);
    }
    for (size_t i = 0; i < fn.get_parameter_size(); ++i) {
        auto param = fn.get_parameter(i);
        if (param == nullptr) continue;
        const std::string ctx = "parameter '" + param->get_short_name()
                                + "' of '" + fn.get_short_name() + "'";
        lex::opt_any_lexeme param_lexeme = fn_lexeme;
        if (auto ast_ps = param->get_ast_parameter_spec(); ast_ps && ast_ps->name) {
            param_lexeme = lex::any_lexeme{*ast_ps->name};
        }
        validate_type_usage(param->get_type(), ctx, pnames, pdescs, param_lexeme);
    }

    push_scope();
    for (size_t i = 0; i < fn.get_parameter_size(); ++i) {
        if (auto param = fn.get_parameter(i)) {
            declare_local(param->get_short_name(), param->get_type(), pnames);
        }
    }
    validate_statement_tree(fn.get_existing_block(), pnames, pdescs);
    pop_scope();
}

void generic_constraint_validator::push_scope() {
    _scopes.emplace_back();
}

void generic_constraint_validator::pop_scope() {
    if (_scopes.empty() == false) _scopes.pop_back();
}

void generic_constraint_validator::declare_local(
    const std::string& name,
    const std::shared_ptr<type>& t,
    const std::unordered_set<std::string>& pnames)
{
    if (_scopes.empty()) return;
    _scopes.back()[name] = is_erased_param_type(t, pnames);
}

bool generic_constraint_validator::is_erased_expression(
    const std::shared_ptr<expression>& expr,
    const std::unordered_set<std::string>& pnames) const
{
    if (expr == nullptr) return false;

    if (auto sym = std::dynamic_pointer_cast<symbol_expression>(expr)) {
        const auto& nm = sym->get_name();
        if (nm.has_root_prefix() || nm.size() != 1) return false;
        const std::string ident = std::string(nm.front());
        for (auto it = _scopes.rbegin(); it != _scopes.rend(); ++it) {
            auto found = it->find(ident);
            if (found != it->end()) return found->second;
        }
        auto member = _member_scope.find(ident);
        return member != _member_scope.end() && member->second;
    }

    if (auto mem = std::dynamic_pointer_cast<member_of_expression>(expr)) {
        auto base = std::dynamic_pointer_cast<symbol_expression>(mem->sub_expr());
        if (base == nullptr || base->get_name().to_string() != "this") return false;
        auto member = _member_scope.find(mem->symbol().get_name().to_string());
        return member != _member_scope.end() && member->second;
    }

    if (std::dynamic_pointer_cast<dereference_expression>(expr) ||
        std::dynamic_pointer_cast<load_value_expression>(expr)) {
        return is_erased_expression(std::static_pointer_cast<unary_expression>(expr)->sub_expr(), pnames);
    }

    if (auto sub = std::dynamic_pointer_cast<subscript_expression>(expr)) {
        return is_erased_expression(sub->left(), pnames);
    }

    return false;
}

void generic_constraint_validator::validate_type_usage(
    const std::shared_ptr<type>& t,
    const std::string& ctx,
    const std::unordered_set<std::string>& pnames,
    const std::vector<template_param_descriptor>& pdescs,
    const lex::opt_any_lexeme& lexeme)
{
    if (t == nullptr) return;

    std::string off = check_direct_usage(t, pnames);
    if (off.empty() == false) report_direct_usage_error(off, ctx, lexeme);

    bool is_drain = false;
    std::string oe = check_owner_or_drain(t, pnames, is_drain);
    if (oe.empty() == false) report_owner_drain_error(oe, is_drain, ctx, lexeme);
}

void generic_constraint_validator::validate_statement_tree(
    const std::shared_ptr<statement>& stmt,
    const std::unordered_set<std::string>& pnames,
    const std::vector<template_param_descriptor>& pdescs)
{
    if (stmt == nullptr) return;

    if (auto block_stmt = std::dynamic_pointer_cast<block>(stmt)) {
        push_scope();
        for (auto& nested_stmt : block_stmt->get_statements()) {
            validate_statement_tree(nested_stmt, pnames, pdescs);
        }
        pop_scope();
        return;
    }

    if (auto var_stmt = std::dynamic_pointer_cast<variable_statement>(stmt)) {
        const std::string ctx = "local variable '" + var_stmt->get_short_name() + "'";
        lex::opt_any_lexeme var_lexeme;
        if (auto ast_vd = var_stmt->get_ast_variable_decl()) var_lexeme = lex::any_lexeme{ast_vd->name};
        validate_type_usage(var_stmt->get_type(), ctx, pnames, pdescs, var_lexeme);
        validate_expression_tree(var_stmt->get_init_expr(), pnames, pdescs);
        declare_local(var_stmt->get_short_name(), var_stmt->get_type(), pnames);
        return;
    }

    if (auto return_stmt = std::dynamic_pointer_cast<return_statement>(stmt)) {
        validate_expression_tree(return_stmt->get_expression(), pnames, pdescs);
        return;
    }

    if (auto expr_stmt = std::dynamic_pointer_cast<expression_statement>(stmt)) {
        validate_expression_tree(expr_stmt->get_expression(), pnames, pdescs);
        return;
    }

    if (auto if_stmt = std::dynamic_pointer_cast<if_else_statement>(stmt)) {
        validate_expression_tree(if_stmt->get_test_expr(), pnames, pdescs);

        for (auto& cond_var : if_stmt->get_cond_vars()) {
            if (cond_var == nullptr) continue;
            const std::string ctx = "condition variable '" + cond_var->get_short_name() + "'";
            lex::opt_any_lexeme var_lexeme;
            if (auto ast_vd = cond_var->get_ast_variable_decl()) var_lexeme = lex::any_lexeme{ast_vd->name};
            validate_type_usage(cond_var->get_type(), ctx, pnames, pdescs, var_lexeme);
            validate_expression_tree(cond_var->get_init_expr(), pnames, pdescs);
            declare_local(cond_var->get_short_name(), cond_var->get_type(), pnames);
        }

        validate_statement_tree(if_stmt->get_then_stmt(), pnames, pdescs);
        validate_statement_tree(if_stmt->get_else_stmt(), pnames, pdescs);
        return;
    }

    if (auto while_stmt = std::dynamic_pointer_cast<while_statement>(stmt)) {
        validate_expression_tree(while_stmt->get_test_expr(), pnames, pdescs);
        validate_statement_tree(while_stmt->get_nested_stmt(), pnames, pdescs);
        return;
    }

    if (auto for_stmt = std::dynamic_pointer_cast<for_statement>(stmt)) {
        validate_statement_tree(for_stmt->get_decl_stmt(), pnames, pdescs);
        validate_expression_tree(for_stmt->get_test_expr(), pnames, pdescs);
        validate_expression_tree(for_stmt->get_step_expr(), pnames, pdescs);

        validate_statement_tree(for_stmt->get_nested_stmt(), pnames, pdescs);
        return;
    }
}

void generic_constraint_validator::validate_expression_tree(
    const std::shared_ptr<expression>& expr,
    const std::unordered_set<std::string>& pnames,
    const std::vector<template_param_descriptor>& pdescs)
{
    if (expr == nullptr) return;

    if (auto sym_expr = std::dynamic_pointer_cast<symbol_expression>(expr)) {
        validate_explicit_template_args(sym_expr, pnames, pdescs);
    }

    if (auto member_expr = std::dynamic_pointer_cast<member_of_expression>(expr)) {
        if (is_erased_expression(member_expr->sub_expr(), pnames)) {
            report_erased_member_access(member_expr->symbol().get_name().to_string(),
                                        member_expr->first_lexeme());
        }
    }

    if (auto cast_expr = std::dynamic_pointer_cast<cast_expression>(expr)) {
        validate_type_usage(cast_expr->get_cast_type(), "cast target type", pnames, pdescs, cast_expr->first_lexeme());
    }

    if (auto tmp_ctor_expr = std::dynamic_pointer_cast<temporary_construction_expression>(expr)) {
        validate_type_usage(tmp_ctor_expr->constructed_type(), "temporary construction type", pnames, pdescs, tmp_ctor_expr->first_lexeme());
        for (auto& arg : tmp_ctor_expr->arguments()) {
            validate_expression_tree(arg, pnames, pdescs);
        }
        return;
    }

    if (auto new_expr = std::dynamic_pointer_cast<new_expression>(expr)) {
        validate_type_usage(new_expr->allocated_type(), "new allocation type", pnames, pdescs, new_expr->first_lexeme());

        for (auto& arg : new_expr->arguments()) {
            validate_expression_tree(arg, pnames, pdescs);
        }
        validate_expression_tree(new_expr->array_size_expr(), pnames, pdescs);

        for (auto& elem : new_expr->array_init_elements()) {
            validate_expression_tree(elem, pnames, pdescs);
        }
        for (auto& arg : new_expr->uniform_ctor_args()) {
            validate_expression_tree(arg, pnames, pdescs);
        }
        return;
    }

    if (auto unary_expr = std::dynamic_pointer_cast<unary_expression>(expr)) {
        validate_expression_tree(unary_expr->sub_expr(), pnames, pdescs);
        return;
    }

    if (auto binary_expr = std::dynamic_pointer_cast<binary_expression>(expr)) {
        validate_expression_tree(binary_expr->left(), pnames, pdescs);
        validate_expression_tree(binary_expr->right(), pnames, pdescs);
        return;
    }

    if (auto fn_call_expr = std::dynamic_pointer_cast<function_invocation_expression>(expr)) {
        validate_expression_tree(fn_call_expr->callee_expr(), pnames, pdescs);
        for (auto& arg : fn_call_expr->arguments()) {
            validate_expression_tree(arg, pnames, pdescs);
        }
        return;
    }

    if (auto ctor_call_expr = std::dynamic_pointer_cast<constructor_invocation_expression>(expr)) {
        for (auto& arg : ctor_call_expr->arguments()) {
            validate_expression_tree(arg, pnames, pdescs);
        }
        return;
    }

    if (auto array_init_expr = std::dynamic_pointer_cast<array_init_expression>(expr)) {
        for (auto& elem : array_init_expr->elements()) {
            validate_expression_tree(elem, pnames, pdescs);
        }
        for (auto& arg : array_init_expr->uniform_ctor_args()) {
            validate_expression_tree(arg, pnames, pdescs);
        }
        return;
    }

    if (auto designated_init_expr = std::dynamic_pointer_cast<designated_struct_init_expression>(expr)) {
        for (auto& member : designated_init_expr->members()) {
            validate_expression_tree(member.value, pnames, pdescs);
            for (auto& arg : member.args) {
                validate_expression_tree(arg, pnames, pdescs);
            }
        }
    }
}

void generic_constraint_validator::validate_explicit_template_args(
    const std::shared_ptr<symbol_expression>& sym_expr,
    const std::unordered_set<std::string>& pnames,
    const std::vector<template_param_descriptor>& pdescs)
{
    if (sym_expr == nullptr || sym_expr->has_ast_template_args() == false) return;

    const auto& ast_args = sym_expr->get_ast_template_args();
    if (ast_args.empty()) return;

    const std::string call_name = sym_expr->get_name().empty()
                                  ? std::string("<anonymous>")
                                  : sym_expr->get_name().to_string();
    lex::opt_any_lexeme call_lexeme = sym_expr->first_lexeme();

    for (size_t i = 0; i < ast_args.size(); ++i) {
        const auto& arg = ast_args[i];
        if (arg == nullptr || arg->is_type() == false || arg->type_arg == nullptr) {
            continue;
        }

        auto arg_type = _context->from_type_specifier(*arg->type_arg);
        const std::string ctx = "explicit template argument #" + std::to_string(i + 1)
                                + " for call '" + call_name + "'";
        validate_type_usage(arg_type, ctx, pnames, pdescs, call_lexeme);
    }
}

void generic_constraint_validator::merge_template_context(
    const std::vector<template_param_descriptor>& extra,
    std::unordered_set<std::string>& pnames,
    std::vector<template_param_descriptor>& pdescs)
{
    for (const auto& desc : extra) {
        pnames.insert(desc.name);
        pdescs.push_back(desc);
    }
}

std::string generic_constraint_validator::check_direct_usage(
    const std::shared_ptr<type>& t,
    const std::unordered_set<std::string>& pnames)
{
    return find_direct_generic_usage(t, pnames, false);
}

std::string generic_constraint_validator::check_owner_or_drain(
    const std::shared_ptr<type>& t,
    const std::unordered_set<std::string>& pnames,
    bool& is_drain)
{
    return find_owner_or_drain_of_param(t, pnames, is_drain);
}

void generic_constraint_validator::report_direct_usage_error(
    const std::string& param_name,
    const std::string& ctx,
    const lex::opt_any_lexeme& lexeme)
{
    throw_error(
        static_cast<unsigned int>(k::diag::generic_diag::ERR_GENERIC_DIRECT_TYPE_USAGE),
        lexeme,
        "Generic type parameter '" + param_name + "' in " + ctx +
        " is used directly without an addresser. "
        "Wrap it with an addresser: '" + param_name + "*', '" + param_name + "?', '" +
        param_name + "+', or '" + param_name + "&'.");
}

void generic_constraint_validator::report_owner_drain_error(
    const std::string& param_name,
    bool is_drain,
    const std::string& ctx,
    const lex::opt_any_lexeme& lexeme)
{
    if (is_drain) {
        throw_error(
            static_cast<unsigned int>(k::diag::generic_diag::ERR_GENERIC_DRAIN_NOT_SUPPORTED),
            lexeme,
            "Drain ('#') of generic type parameter '" + param_name + "' in " + ctx +
            " is not supported: a generic body is synthesised once with '" + param_name +
            "' erased and cannot perform the explicit member calls a drain requires. "
            "Use '" + param_name + "&', '" + param_name + "*', '" + param_name +
            "?' or '" + param_name + "+' instead.");
    }
    throw_error(
        static_cast<unsigned int>(k::diag::generic_diag::ERR_GENERIC_OWNER_NOT_SUPPORTED),
        lexeme,
        "Owner ('!') of generic type parameter '" + param_name + "' in " + ctx +
        " is not supported: a generic body is synthesised once with '" + param_name +
        "' erased and cannot run the destructor of an owned object. "
        "Use '" + param_name + "&', '" + param_name + "*', '" + param_name +
        "?' or '" + param_name + "+' instead.");
}

void generic_constraint_validator::report_erased_member_access(
    const std::string& member_name,
    const lex::opt_any_lexeme& lexeme)
{
    throw_error(
        static_cast<unsigned int>(k::diag::generic_diag::ERR_GENERIC_MEMBER_ACCESS_ON_ERASED_PARAM),
        lexeme,
        "Cannot access member '" + member_name + "' through a generic type parameter: "
        "a generic body only sees an opaque, erased pointer. Perform the member access "
        "at the call site, outside the generic body.");
}

} // namespace k::model::gen
