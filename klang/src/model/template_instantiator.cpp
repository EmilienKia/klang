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
#include "type.hpp"
#include "statements.hpp"
#include "expressions.hpp"

#include <sstream>

namespace k::model {

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
        if (args[i].is_type()) {
            oss << type_display_name(args[i].type_arg);
        } else {
            oss << args[i].value_arg.value_or(0);
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
        if (args[i].is_type()) {
            std::string tn = type_display_name(args[i].type_arg);
            for (char& c : tn) {
                if (!std::isalnum(c)) c = '_';
            }
            oss << tn;
        } else {
            oss << args[i].value_arg.value_or(0);
        }
    }
    return oss.str();
}

// ═══════════════════════════════════════════════════════════════════════════
// Build substitution map
// ═══════════════════════════════════════════════════════════════════════════

type_substitution_map template_instantiator::build_substitution_map(
    const tpl_info& ti,
    const std::vector<template_argument>& args)
{
    type_substitution_map result;
    size_t count = std::min(ti.params.size(), args.size());
    for (size_t i = 0; i < count; ++i) {
        if (args[i].is_type() && args[i].type_arg) {
            result[ti.params[i].name] = args[i].type_arg;
        }
        // Value parameters: no type substitution needed
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
        for (auto& arg : fie->arguments()) {
            substitute_expr_types(std::const_pointer_cast<expression>(arg), subst);
        }
    } else if (auto cie = std::dynamic_pointer_cast<constructor_invocation_expression>(expr)) {
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
    const type_substitution_map& subst)
{
    if (!src) return nullptr;
    auto cloned = src->clone();
    substitute_expr_types(cloned, subst);
    return cloned;
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
    const type_substitution_map& subst)
{
    // Return statement
    if (auto rs = dynamic_cast<const return_statement*>(&src)) {
        auto new_rs = std::make_shared<return_statement>(parent_stmt);
        new_rs->_ast_node = rs->get_ast_node(); // optional, for diagnostics
        if (rs->get_expression()) {
            new_rs->set_expression(clone_and_substitute_expr(
                std::const_pointer_cast<expression>(rs->get_expression()), subst));
        }
        return new_rs;
    }

    // If-else statement
    if (auto ies = dynamic_cast<const if_else_statement*>(&src)) {
        auto new_ies = std::make_shared<if_else_statement>(parent_stmt);
        new_ies->_ast_node = ies->get_ast_node();
        if (ies->get_test_expr()) {
            new_ies->set_test_expr(clone_and_substitute_expr(
                std::const_pointer_cast<expression>(ies->get_test_expr()), subst));
        }
        if (ies->get_then_stmt()) {
            new_ies->set_then_stmt(clone_statement(*ies->get_then_stmt(), new_ies, subst));
        }
        if (ies->get_else_stmt()) {
            new_ies->set_else_stmt(clone_statement(*ies->get_else_stmt(), new_ies, subst));
        }
        return new_ies;
    }

    // While statement
    if (auto ws = dynamic_cast<const while_statement*>(&src)) {
        auto new_ws = std::make_shared<while_statement>(parent_stmt);
        new_ws->_ast_node = ws->get_ast_node();
        if (ws->get_test_expr()) {
            new_ws->set_test_expr(clone_and_substitute_expr(
                std::const_pointer_cast<expression>(ws->get_test_expr()), subst));
        }
        if (ws->get_nested_stmt()) {
            new_ws->set_nested_stmt(clone_statement(*ws->get_nested_stmt(), new_ws, subst));
        }
        return new_ws;
    }

    // Expression statement
    if (auto es = dynamic_cast<const expression_statement*>(&src)) {
        auto new_es = std::make_shared<expression_statement>(parent_stmt);
        new_es->_ast_node = es->get_ast_node();
        if (es->get_expression()) {
            new_es->set_expression(clone_and_substitute_expr(
                std::const_pointer_cast<expression>(es->get_expression()), subst));
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
            static_cast<variable_definition*>(new_vs.get())->set_init_expr(
                clone_and_substitute_expr(
                    std::const_pointer_cast<expression>(vs->get_init_expr()), subst));
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
        clone_block_contents(*blk, new_blk, subst);
        return new_blk;
    }

    // For statement
    if (auto fs = dynamic_cast<const for_statement*>(&src)) {
        auto new_fs = std::make_shared<for_statement>(parent_stmt);
        new_fs->_ast_node = fs->get_ast_node();
        if (fs->get_decl_stmt()) {
            auto cloned_decl = std::dynamic_pointer_cast<variable_statement>(
                clone_statement(*fs->get_decl_stmt(), new_fs, subst));
            new_fs->set_decl_stmt(cloned_decl);
        }
        if (fs->get_test_expr()) {
            new_fs->set_test_expr(clone_and_substitute_expr(
                std::const_pointer_cast<expression>(fs->get_test_expr()), subst));
        }
        if (fs->get_step_expr()) {
            new_fs->set_step_expr(clone_and_substitute_expr(
                std::const_pointer_cast<expression>(fs->get_step_expr()), subst));
        }
        if (fs->get_nested_stmt()) {
            new_fs->set_nested_stmt(clone_statement(*fs->get_nested_stmt(), new_fs, subst));
        }
        return new_fs;
    }

    // Fallback: unknown statement type — return empty
    return nullptr;
}

void template_instantiator::clone_block_contents(
    const block& src,
    std::shared_ptr<block> dst,
    const type_substitution_map& subst)
{
    for (auto& stmt : src.get_statements()) {
        if (!stmt) continue;
        auto cloned = clone_statement(*stmt, dst, subst);
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
    const type_substitution_map& subst)
{
    // Skip the synthetic __parent__ field (it will be recreated by resolution passes)
    if (src.get_short_name() == "__parent__") return;

    bool is_static = false; // member variables detected as static are global_variable_definition, not member
    auto new_var = target->append_variable(src.get_short_name(), is_static);
    if (!new_var) return;

    // Substitute type
    auto src_type = std::const_pointer_cast<type>(src.get_type());
    new_var->set_type(substitute_type(src_type, subst));
    new_var->set_const(src.is_const());

    // Clone init expression and retarget to new variable
    if (src.get_init_expr()) {
        auto cloned_init = clone_and_substitute_expr(
            std::const_pointer_cast<expression>(src.get_init_expr()), subst);
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
// Populate function from template source
// ═══════════════════════════════════════════════════════════════════════════

void template_instantiator::populate_function_from_template(
    std::shared_ptr<function> dst,
    const function& src,
    const type_substitution_map& subst)
{
    // Set return type
    if (src.has_return_type()) {
        dst->set_return_type(substitute_type(
            std::const_pointer_cast<type>(src.get_return_type()), subst));
    }

    // Clone parameters (skip 'this' — will be recreated by resolution passes)
    for (auto& param : src.parameters()) {
        if (param == src.get_this_parameter()) continue;
        auto param_type = substitute_type(
            std::const_pointer_cast<type>(param->get_type()), subst);
        auto new_param = dst->append_parameter(param->get_short_name(), param_type);
        new_param->set_const(param->is_const());
        new_param->_ast_node = param->get_ast_node(); // diagnostics
    }

    // Clone body
    auto src_block = const_cast<function&>(src).get_block();
    if (src_block) {
        auto dst_block = dst->get_block();
        if (dst_block) {
            clone_block_contents(*src_block, dst_block, subst);
        }
    }

    // Copy AST node (optional, for diagnostics)
    dst->_ast_node = src.get_ast_node();
}

// ═══════════════════════════════════════════════════════════════════════════
// Clone method
// ═══════════════════════════════════════════════════════════════════════════

void template_instantiator::clone_method(
    const function& src,
    std::shared_ptr<aggregate> target,
    const type_substitution_map& subst)
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

    populate_function_from_template(new_func, src, subst);
}

// ═══════════════════════════════════════════════════════════════════════════
// Clone constructor
// ═══════════════════════════════════════════════════════════════════════════

void template_instantiator::clone_constructor(
    const constructor& src,
    std::shared_ptr<aggregate> target,
    const type_substitution_map& subst)
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
            new_args.push_back(clone_and_substitute_expr(arg, subst));
        }
        new_ctor->add_member_init(mi.member_name, std::move(new_args), mi.is_base_init);
    }

    populate_function_from_template(new_ctor, src, subst);
}

// ═══════════════════════════════════════════════════════════════════════════
// Clone destructor
// ═══════════════════════════════════════════════════════════════════════════

void template_instantiator::clone_destructor(
    const destructor& src,
    std::shared_ptr<aggregate> target,
    const type_substitution_map& subst)
{
    // define_function with "~" + aggregate name creates a destructor
    auto new_func = target->define_function("~" + target->get_short_name(), false);
    auto new_dtor = std::dynamic_pointer_cast<destructor>(new_func);
    if (!new_dtor) return;

    new_dtor->set_visibility(src.get_visibility());
    populate_function_from_template(new_dtor, src, subst);
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

    // 1. Create a new concrete aggregate in the parent namespace
    std::shared_ptr<aggregate> concrete;
    if (tpl_def.is_class()) {
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

    // Copy bases
    for (auto& bs : tpl_def.get_bases()) {
        concrete->add_base(bs.raw_name, bs.vis);
    }

    // 2. Clone children from the template aggregate
    for (auto& child : tpl_def.get_children()) {
        if (auto mv = std::dynamic_pointer_cast<member_variable_definition>(child)) {
            clone_member_variable(*mv, concrete, subst);
        } else if (auto ctor = std::dynamic_pointer_cast<constructor>(child)) {
            clone_constructor(*ctor, concrete, subst);
        } else if (auto dtor = std::dynamic_pointer_cast<destructor>(child)) {
            clone_destructor(*dtor, concrete, subst);
        } else if (auto fn = std::dynamic_pointer_cast<function>(child)) {
            clone_method(*fn, concrete, subst);
        } else if (auto gv = std::dynamic_pointer_cast<global_variable_definition>(child)) {
            // Static member variable — clone similarly to member variable
            auto new_var = concrete->append_variable(gv->get_short_name(), /*is_static=*/true);
            if (new_var) {
                auto src_type = std::const_pointer_cast<type>(gv->get_type());
                new_var->set_type(substitute_type(src_type, subst));
                new_var->set_const(gv->is_const());
                if (gv->get_init_expr()) {
                    auto cloned_init = clone_and_substitute_expr(
                        std::const_pointer_cast<expression>(gv->get_init_expr()), subst);
                    new_var->set_init_expr(cloned_init);
                    retarget_init_expr(cloned_init, new_var);
                }
                if (auto gv_new = std::dynamic_pointer_cast<global_variable_definition>(new_var)) {
                    gv_new->set_visibility(gv->get_visibility());
                }
            }
        }
        // Nested aggregates, enums, using declarations — handled as needed in future
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
    populate_function_from_template(concrete, tpl_def, subst);

    // 3. Register in the instantiation cache
    ti->instantiations[key] = concrete;

    return concrete;
}

} // namespace k::model










