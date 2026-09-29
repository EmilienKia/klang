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

#include "naming_pass.hpp"
#include "casing_utils.hpp"

#include "../../model/model.hpp"
#include "../../model/model_aggregate.hpp"
#include "../../model/model_function.hpp"
#include "../../model/model_ns.hpp"
#include "../../model/model_enum.hpp"
#include "../../model/imported.hpp"
#include "../../model/statements.hpp"
#include "../../model/expressions.hpp"
#include "../../model/type.hpp"

namespace k::plugin::naming {

bool naming_pass::is_local(const model::element& elem) const {
    if (dynamic_cast<const model::imported_function*>(&elem)) return false;
    if (dynamic_cast<const model::imported_aggregate*>(&elem)) return false;
    if (dynamic_cast<const model::imported_variable*>(&elem)) return false;

    if (get_unit()) {
        auto u = elem.ancestor<model::unit>();
        if (u && u.get() != get_unit()) return false;
    }
    return true;
}

void naming_pass::check_and_rename(model::named_element& decl, const std::string& target_name) {
    if (decl.get_short_name() == target_name) return;

    std::string reason;
    if (!decl.can_change_name(target_name, &reason)) {
        return;
    }

    if (decl.change_name(target_name)) {
        mark_modified();
    }
}

namespace {

std::string extract_type_name(const std::shared_ptr<model::type>& t) {
    if (!t) return "";
    auto cur = model::type::canonical(model::type::remove_const(t));
    while (model::type::is_any_indirection(cur)) {
        cur = model::type::canonical(model::type::remove_const(cur->get_subtype()));
    }
    if (auto st = std::dynamic_pointer_cast<model::struct_type>(cur)) {
        return st->name();
    } else if (auto unres = std::dynamic_pointer_cast<model::unresolved_type>(cur)) {
        return unres->type_id().empty() ? "" : unres->type_id().back();
    }
    return "";
}

} // anonymous namespace

void naming_pass::check_and_rename_member(model::aggregate& agg, model::named_element& member, const std::string& target_name) {
    if (member.get_short_name() == target_name) return;
    std::string old_name = member.get_short_name();
    std::string reason;
    if (!member.can_change_name(target_name, &reason)) {
        return;
    }
    std::string agg_name = agg.get_short_name();
    if (member.change_name(target_name)) {
        _member_renames_by_name[agg_name][old_name] = target_name;
        mark_modified();
    }
}

void naming_pass::visit_namespace(model::ns& ns) {
    if (!ns.is_root() && is_local(ns)) {
        check_and_rename(ns, to_snake_case(ns.get_short_name()));
    }
    model_pass::visit_namespace(ns);
}

void naming_pass::visit_aggregate(model::aggregate& agg) {
    if (is_local(agg)) {
        std::string old_agg_name = agg.get_short_name();
        std::string target_agg_name = to_pascal_case(old_agg_name);
        check_and_rename(agg, target_agg_name);
        if (old_agg_name != target_agg_name && _member_renames_by_name.count(old_agg_name)) {
            _member_renames_by_name[target_agg_name] = _member_renames_by_name[old_agg_name];
        }
    }
    model_pass::visit_aggregate(agg);
}

void naming_pass::visit_structure(model::structure& st) {
    visit_aggregate(st);
}

void naming_pass::visit_klass(model::klass& kl) {
    visit_aggregate(kl);
}

void naming_pass::visit_enumeration(model::enumeration& en) {
    if (is_local(en)) {
        check_and_rename(en, to_pascal_case(en.get_short_name()));
    }
    default_model_visitor::visit_enumeration(en);
}

void naming_pass::visit_function(model::function& fn) {
    if (!is_local(fn)) return;
    if (fn.get_short_name() == "main") return;
    if (fn.is_operator()) return;
    if (dynamic_cast<model::constructor*>(&fn) || dynamic_cast<model::destructor*>(&fn)) return;
    if (dynamic_cast<model::static_constructor*>(&fn) || dynamic_cast<model::static_destructor*>(&fn)) return;
    if (fn.is_extern()) return;
    if (fn.is_compiler_generated()) return;

    std::string target = to_camel_case(fn.get_short_name());
    if (auto owner = fn.parent<model::aggregate>()) {
        check_and_rename_member(*owner, fn, target);
    } else {
        check_and_rename(fn, target);
    }

    if (auto blk = fn.get_block()) {
        blk->accept(*this);
    }
}

void naming_pass::visit_global_variable_definition(model::global_variable_definition& var) {
    if (!is_local(var)) return;

    if (var.is_const()) {
        check_and_rename(var, to_screaming_snake_case(var.get_short_name()));
    } else {
        check_and_rename(var, to_camel_case(var.get_short_name()));
    }
    if (auto expr = var.get_init_expr()) {
        expr->accept(*this);
    }
}

void naming_pass::visit_member_variable_definition(model::member_variable_definition& var) {
    if (!is_local(var)) return;

    std::string target = var.is_const()
        ? to_screaming_snake_case(var.get_short_name())
        : to_camel_case(var.get_short_name());

    if (auto owner = var.parent<model::aggregate>()) {
        check_and_rename_member(*owner, var, target);
    } else {
        check_and_rename(var, target);
    }
    if (auto expr = var.get_init_expr()) {
        expr->accept(*this);
    }
}

void naming_pass::visit_block(model::block& blk) {
    for (auto it = blk.begin(); it != blk.end(); ++it) {
        if (*it) (*it)->accept(*this);
    }
}

void naming_pass::visit_expression_statement(model::expression_statement& stmt) {
    if (auto expr = stmt.get_expression()) expr->accept(*this);
}

void naming_pass::visit_return_statement(model::return_statement& stmt) {
    if (auto expr = stmt.get_expression()) expr->accept(*this);
}

void naming_pass::visit_if_else_statement(model::if_else_statement& stmt) {
    if (stmt.get_test_expr()) stmt.get_test_expr()->accept(*this);
    if (stmt.get_then_stmt()) stmt.get_then_stmt()->accept(*this);
    if (stmt.get_else_stmt()) stmt.get_else_stmt()->accept(*this);
}

void naming_pass::visit_while_statement(model::while_statement& stmt) {
    if (stmt.get_test_expr()) stmt.get_test_expr()->accept(*this);
    if (stmt.get_nested_stmt()) stmt.get_nested_stmt()->accept(*this);
}

void naming_pass::visit_for_statement(model::for_statement& stmt) {
    if (stmt.get_decl_stmt()) stmt.get_decl_stmt()->accept(*this);
    if (stmt.get_test_expr()) stmt.get_test_expr()->accept(*this);
    if (stmt.get_step_expr()) stmt.get_step_expr()->accept(*this);
    if (stmt.get_nested_stmt()) stmt.get_nested_stmt()->accept(*this);
}

void naming_pass::visit_binary_expression(model::binary_expression& expr) {
    if (expr.left()) expr.left()->accept(*this);
    if (expr.right()) expr.right()->accept(*this);
}

void naming_pass::visit_unary_expression(model::unary_expression& expr) {
    if (expr.sub_expr()) expr.sub_expr()->accept(*this);
}

void naming_pass::visit_function_invocation_expression(model::function_invocation_expression& expr) {
    if (expr.callee_expr()) expr.callee_expr()->accept(*this);
    for (auto& arg : expr.arguments()) {
        if (arg) arg->accept(*this);
    }
}

void naming_pass::visit_member_of_expression(model::member_of_expression& expr) {
    if (expr.sub_expr()) expr.sub_expr()->accept(*this);

    auto sub = expr.sub_expr();
    if (!sub) return;

    std::string type_name;
    if (auto sym = std::dynamic_pointer_cast<model::symbol_expression>(sub)) {
        if (sym->is_variable_def() && sym->get_variable_def()) {
            type_name = extract_type_name(sym->get_variable_def()->get_type());
        }
    }
    if (type_name.empty()) {
        if (auto func = expr.ancestor<model::function>()) {
            if (func->is_member() && func->get_owner()) {
                type_name = func->get_owner()->get_short_name();
            }
        }
    }

    if (!type_name.empty()) {
        auto it_agg = _member_renames_by_name.find(type_name);
        if (it_agg != _member_renames_by_name.end()) {
            const std::string old_name = expr.symbol().get_name().to_string();
            auto it_name = it_agg->second.find(old_name);
            if (it_name != it_agg->second.end()) {
                expr.symbol().update_referenced_name(it_name->second);
            }
        }
    }
}

} // namespace k::plugin::naming
