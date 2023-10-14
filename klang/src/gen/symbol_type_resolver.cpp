/*
 * K Language compiler
 *
 * Copyright 2023 Emilien Kia
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
//
// Note: Last resolver log number: 0x30004
//

#include "symbol_type_resolver.hpp"

namespace k::model {


//
// Exceptions
//
resolution_error::resolution_error(const std::string &arg) :
        runtime_error(arg)
{}

resolution_error::resolution_error(const char *string) :
        runtime_error(string)
{}

//
// Symabol and type resolver
//

void symbol_type_resolver::resolve()
{
    visit_unit(_unit);
}

void symbol_type_resolver::visit_unit(unit& unit)
{
    visit_namespace(*_unit.get_root_namespace());
}

void symbol_type_resolver::visit_namespace(ns& ns)
{
    bool has_name = false;
    if(!ns.get_name().empty()) {
        has_name = true;
        _naming_context.push_back(ns.get_name());
    }

    for(auto& child : ns.get_children()) {
        child->accept(*this);
    }

    if(has_name) {
        _naming_context.pop_back();
    }
}
void symbol_type_resolver::visit_global_variable_definition(global_variable_definition& var)
{
    // TODO visit parameter definition (just in case default init is referencing a variable).
}

void symbol_type_resolver::visit_function(function& fn)
{
    _naming_context.push_back(fn.name());

    // TODO visit parameter definition (just in case default init is referencing a variable).

    if(auto block = fn.get_block()) {
        visit_block(*block);
    }

    _naming_context.pop_back();
}

void symbol_type_resolver::visit_block(block& block)
{
    for(auto& stmt : block.get_statements()) {
        stmt->accept(*this);
    }
}

void symbol_type_resolver::visit_return_statement(return_statement& stmt)
{
    auto func = stmt.get_block()->get_function();
    auto ret_type = func->return_type();
    // TODO check if return type is void to prevent to return sometinhg

    if(auto expr = stmt.get_expression()) {
        expr->accept(*this);
        auto cast = adapt_type(expr, ret_type);
        if(!cast) {
            throw_error(0x0001, stmt.get_ast_return_statement()->ret, "Return expression type must be compatible to the expected function return type");
        } else if(cast != expr ) {
            // Casted, assign casted expression as return expr.
            stmt.set_expression(cast);
        } else {
            // Compatible type, no need to cast.
        }
    }
}

void symbol_type_resolver::visit_if_else_statement(if_else_statement& stmt)
{
    // Resolve and cast test
    {
        auto expr = stmt.get_test_expr();
        expr->accept(*this);
        auto cast = adapt_type(expr, primitive_type::from_type(primitive_type::BOOL));
        if(!cast) {
            throw_error(0x0002, stmt.get_ast_if_else_stmt()->if_kw, "If test expression type must be convertible to bool");
        } else if(cast != expr ) {
            // Casted, assign casted expression as return expr.
            stmt.set_test_expr(cast);
        } else {
            // Compatible type, no need to cast.
        }
    }

    // Resolve then statement
    stmt.get_then_stmt()->accept(*this);

    // Resolve else statement
    if(auto expr = stmt.get_else_stmt()) {
        expr->accept(*this);
    }
}

void symbol_type_resolver::visit_while_statement(while_statement& stmt)
{
    // Resolve and cast test
    {
        auto expr = stmt.get_test_expr();
        expr->accept(*this);
        auto cast = adapt_type(expr, primitive_type::from_type(primitive_type::BOOL));
        if(!cast) {
            throw_error(0x0003, stmt.get_ast_while_stmt()->while_kw, "While test expression type must be convertible to bool");
        } else if(cast != expr ) {
            // Casted, assign casted expression as return expr.
            stmt.set_test_expr(cast);
        } else {
            // Compatible type, no need to cast.
        }
    }

    // Resolve nested statement
    stmt.get_nested_stmt()->accept(*this);
}

void symbol_type_resolver::visit_for_statement(for_statement& stmt)
{
    // Resolve variable decl, if any
    if(auto decl = stmt.get_decl_stmt()) {
        decl->accept(*this);
    }

    // Resolve and cast test
    if(auto expr = stmt.get_test_expr()) {
        expr->accept(*this);
        auto cast = adapt_type(expr, primitive_type::from_type(primitive_type::BOOL));
        if(!cast) {
            throw_error(0x0004, stmt.get_ast_for_stmt()->for_kw, "For test expression type must be convertible to bool");
        } else if(cast != expr ) {
            // Casted, assign casted expression as return expr.
            stmt.set_test_expr(cast);
        } else {
            // Compatible type, no need to cast.
        }
    }

    // Resolve step
    if(auto step = stmt.get_step_expr()) {
        step->accept(*this);
    }

    // Resolve nested statement
    stmt.get_nested_stmt()->accept(*this);
}


void symbol_type_resolver::visit_expression_statement(expression_statement& stmt)
{
    if(auto expr = stmt.get_expression()) {
        expr->accept(*this);
    }
}

void symbol_type_resolver::visit_variable_statement(variable_statement& var)
{
    if(auto expr = var.get_init_expr()) {
        expr->accept(*this);
    }
}

void symbol_type_resolver::visit_value_expression(value_expression& expr)
{
    // Nothing to view here:
    // - No symbol to resolve
    // - Type is supposed to have already been set at value_expression construction.
    // See value_expression::type_from_literal(const k::lex::any_literal& literal)
}

void symbol_type_resolver::visit_symbol_expression(symbol_expression& symbol)
{
    // TODO: Have to check the variable definition (for block-variable) is done before the expression;
    // TODO: Have to support all symbol types, not only variables
    if(!symbol.is_resolved()) {
        if(auto stmt = symbol.find_statement()) {
            if(auto var_holder = stmt->get_variable_holder()) {
                if(auto def = var_holder->lookup_variable(symbol.get_name())) {
                    symbol.resolve(def);
                    // Note: type is supposed to be applied at resolution
                }
            }
        }
    }
}

void symbol_type_resolver::visit_unary_expression(unary_expression& expr)
{
    auto& sub = expr.sub_expr();

    if(!sub) {
        // TODO throw an exception
        // Error 0x0002: unary expression must have non-null sub expresssion
        std::cerr << "Error: unary expression must have non-null sub expresssion" << std::endl;
    }

    sub->accept(*this);

    if(!type::is_resolved(sub->get_type())) {
        // TODO throw an exception
        // Error 0x0003: unary expression must have resolved type for its sub-expression
        std::cerr << "Error: unary expression must have resolved type for its sub-expression" << std::endl;
    }
}

void symbol_type_resolver::visit_binary_expression(binary_expression& expr)
{
    auto& left = expr.left();
    auto& right = expr.right();

    if(!left || !right) {
        // TODO throw an exception
        // Error 0x0004: binary expression must have non-null left and right expresssion
        std::cerr << "Error: binary expression must have non-null left and right expresssion" << std::endl;
    }

    left->accept(*this);
    right->accept(*this);

    if(!type::is_resolved(left->get_type())) {
        // TODO throw an exception
        // Error 0x0005: Error: left sub-expression of binary expression must have resolved type
        std::cerr << "Error: left sub-expression of binary expression must have resolved type" << std::endl;
    }
    if(!type::is_resolved(right->get_type())) {
        // TODO throw an exception
        // Error 0x0005b: Error: right sub-expression of binary expression must have resolved type
        std::cerr << "Error: right sub-expression of binary expression must have resolved type" << std::endl;
    }
}

void symbol_type_resolver::process_arithmetic(binary_expression& expr) {
    // TODO Rework conversions and promotions
    visit_binary_expression(expr);

    auto& left = expr.left();
    auto& right = expr.right();

    auto type = left->get_type();
    if(!type::is_primitive(type)) {
        // TODO throw an exception
        // Arithmetic for non-primitive types is not supported.
        std::cerr << "Error: Arithmetic for non-primitive types is not supported yet." << std::endl;
    }
    if(type::is_prim_bool(type)) {
        // TODO throw an exception
        // Arithmetic for boolean is not supported.
        std::cerr << "Error: Arithmetic for boolean is not supported." << std::endl;
    }

    expr.set_type(left->get_type());
    // TODO Promote to largest type instread to align to left operand.
    auto cast = adapt_type(right, left->get_type());
    if(!cast) {
        // TODO throw an exception
        // Error: right type is not compatible (cannot be cast).
        std::cerr << "Error: binary arithmetic expression must have resolved type at left and right sub-expression" << std::endl;
    } else if(cast != right ) {
        // Casted, assign casted expression instead of right source.
        expr.assign_right(cast);
    } else {
        // Compatible type, no need to cast.
    }
}

void symbol_type_resolver::visit_arithmetic_binary_expression(arithmetic_binary_expression &expr) {
    process_arithmetic(expr);
}

void symbol_type_resolver::visit_assignation_expression(assignation_expression &expr) {
    process_arithmetic(expr);
}

void symbol_type_resolver::visit_arithmetic_unary_expression(arithmetic_unary_expression& expr) {
    visit_unary_expression(expr);

    auto& sub = expr.sub_expr();
    auto type = sub->get_type();

    if(!type::is_primitive(type)) {
        // TODO throw an exception
        // Arithmetic for non-primitive types is not supported.
        std::cerr << "Error: Arithmetic for non-primitive types is not supported yet." << std::endl;
    }

    expr.set_type(type);
}

void symbol_type_resolver::visit_logical_binary_expression(logical_binary_expression& expr) {
    visit_binary_expression(expr);

    auto& left = expr.left();
    auto& right = expr.right();

    if(!type::is_primitive( left->get_type()) || !type::is_primitive(right->get_type())) {
        // TODO throw an exception
        // Logical for non-primitive types is not supported.
        std::cerr << "Error: Arithmetic for non-primitive types is not supported yet." << std::endl;
    }

    auto bool_type = primitive_type::from_type(primitive_type::BOOL);

    auto cast_left = adapt_type(left, bool_type);
    if(!cast_left) {
        // TODO throw an exception
        // Error: left type is not compatible (cannot be cast).
        std::cerr << "Error: Logical binary operand must be casted to boolean" << std::endl;
    } else if(cast_left != left ) {
        // Casted, assign casted expression instead of source.
        expr.assign_left(cast_left);
    } else {
        // Compatible type, no need to cast.
    }

    auto cast_right = adapt_type(right, bool_type);
    if(!cast_right) {
        // TODO throw an exception
        // Error: right type is not compatible (cannot be cast).
        std::cerr << "Error: Logical binary operand must be casted to boolean" << std::endl;
    } else if(cast_right != right ) {
        // Casted, assign casted expression instead of source.
        expr.assign_right(cast_right);
    } else {
        // Compatible type, no need to cast.
    }

    // For primitive type, logical is always returning boolean
    expr.set_type(primitive_type::from_type(primitive_type::BOOL));
}

void symbol_type_resolver::visit_logical_not_expression(logical_not_expression& expr) {
    visit_unary_expression(expr);

    auto& sub = expr.sub_expr();
    auto type = sub->get_type();

    if(!type::is_primitive(type)) {
        // TODO throw an exception
        // Logical negation for non-primitive types is not supported.
        std::cerr << "Error: Logical negation for non-primitive types is not supported yet." << std::endl;
    }

    static auto bool_type = primitive_type::from_type(primitive_type::BOOL);
    auto cast = adapt_type(sub, bool_type);
    if(!cast) {
        // TODO throw an exception
        // Error: right type is not compatible (cannot be cast).
        std::cerr << "Error: Logical negation operand must be casted to boolean" << std::endl;
    } else if(cast != sub ) {
        // Casted, assign casted expression instead of source.
        expr.assign(cast);
    } else {
        // Compatible type, no need to cast.
    }

    // For primitive type, logical is always returning boolean
    expr.set_type(bool_type);
}

void symbol_type_resolver::visit_comparison_expression(comparison_expression& expr) {
    visit_binary_expression(expr);

    auto& left = expr.left();
    auto& right = expr.right();

    if(!type::is_primitive(left->get_type()) || !type::is_primitive(right->get_type())) {
        // TODO throw an exception
        // Logical for non-primitive types is not supported.
        std::cerr << "Error: Arithmetic for non-primitive types is not supported yet." << std::endl;
    }

    auto left_type = std::dynamic_pointer_cast<primitive_type>(left->get_type());
    auto right_type = std::dynamic_pointer_cast<primitive_type>(right->get_type());

    auto adapted_left = left;
    auto adapted_right = right;

    if(left_type->is_boolean() && !right_type->is_boolean()) {
        // Adapt right to boolean
        adapted_right = adapt_type(right, left_type);
    } else if(!left_type->is_boolean() && right_type->is_boolean()) {
        // Adapt left to boolean
        adapted_left = adapt_type(left, right_type);
    }  else {
        // Adapt right to left type
        // TODO rework to promote to biggest integer of both
        adapted_right = adapt_type(right, left_type);
    }

    if(!adapted_left || !adapted_right) {
        // TODO throw an exception
        // Adaptation is not possible
        std::cerr << "Error: Type alignment for comparison expression is not possible." << std::endl;
    }

    if(adapted_left!=left) {
        expr.assign_left(adapted_left);
    }
    if(adapted_right!=right) {
        expr.assign_right(adapted_right);
    }

    // For primitive type, logical is always returning boolean
    static auto bool_type = primitive_type::from_type(primitive_type::BOOL);
    expr.set_type(bool_type);
}

void symbol_type_resolver::visit_function_invocation_expression(function_invocation_expression &expr) {
    auto callee = std::dynamic_pointer_cast<symbol_expression>(expr.callee_expr());
    if(!callee) {
        // TODO Support only variable expr as function name for now.
        std::cerr << "Error : only support symbol expr as function name for now" << std::endl;
    }

    for(auto& arg : expr.arguments()) {
        arg->accept(*this);
    }

    if(auto stmt = callee->find_statement()) {
        if(auto block = stmt->get_block()) {
            if(auto func = block->get_function()) {
                if(auto ns = func->parent_ns()) {
                    if(auto function = ns->lookup_function(callee->get_name())) {
                        // TODO support overloading
                        // TODO enforce prototype matching
                        // Function prototype and expression type are set at resolution
                        callee->resolve(function);
                        expr.set_type(function->return_type());
                    }
                }
            }
        }
    }

    if(!callee->is_resolved() || !callee->is_function()) {
        // Error: cannot resolve function.
        // TODO throw an exception
        std::cerr << "Cannot resolve function '" << callee->get_name().to_string() << "'" << std::endl;
    }

    auto params = callee->get_function()->parameters();
    if(expr.arguments().size() != params.size()) {
        // Error : callee and function have not the same argument count.
        // TODO throw an exception
        std::cerr << "Error : callee and function '" << callee->get_name().to_string() << "' have not the same argument count" << std::endl;
    }

    for(size_t n=0; n<expr.arguments().size(); ++n) {
        auto arg = expr.arguments().at(n);
        auto param = callee->get_function()->parameters().at(n);

        if(!param->get_type() || !param->get_type()->is_resolved() ||
           !arg->get_type() || !arg->get_type()->is_resolved()) {
            // TODO throw an exception
            // Error: function argument or function invocation parameter have undefined type
            std::cerr << "Error: function invocation must have defined types" << std::endl;
        }

        auto cast = adapt_type(arg, param->get_type());
        if(!cast) {
            // TODO throw an exception
            // Error: function parameter is not compatible (cannot be cast).
            std::cerr << "Error: function argument must be compatible to parameter" << std::endl;
        } else if(cast != arg ) {
            // Casted, assign casted expression instead of right source.
            expr.assign_argument(n, cast);
        } else {
            // Compatible type, no need to cast.
        }
    }
}

void symbol_type_resolver::visit_cast_expression(cast_expression& expr) {
    expr.sub_expr()->accept(*this);

    // TODO check if cast is possible (expr.expr().get_type() && expr.get_cast_type() compatibility)

    expr.set_type(expr.get_cast_type());
}

std::shared_ptr<expression> symbol_type_resolver::adapt_type(const std::shared_ptr<expression>& expr, const std::shared_ptr<type>& type) {
    if(!expr || !type::is_resolved(type) || !type::is_resolved(expr->get_type())) {
        // Arguments must not be null, expr must have a type and types (expr and target) must be resolved.
        return nullptr;
    }

    auto prim_src = std::dynamic_pointer_cast<primitive_type>(expr->get_type());
    auto prim_tgt = std::dynamic_pointer_cast<primitive_type>(type);

    if(!prim_src || !prim_tgt) {
        // Support only primitive types for now.
        // TODO support not-primitive type casting
        return {};
    }

    if(prim_src==prim_tgt) {
        // Trivially agree for same types
        return expr;
    }

    auto cast = cast_expression::make_shared(expr, prim_tgt);
    cast->set_type(prim_tgt);
    return cast;

}



} // k::model
