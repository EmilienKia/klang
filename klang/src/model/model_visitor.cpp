/*
 * K Language compiler
 *
 * Copyright 2023-2024 Emilien Kia
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

#include "model_visitor.hpp"

namespace k::model {

//
// Default element visitor
//

void default_model_visitor::visit_element(element &) {
}

void default_model_visitor::visit_unit(unit &unit) {
    visit_element(unit);
}

void default_model_visitor::visit_ns_element(ns_element &elem) {
    visit_element(elem);
}

void default_model_visitor::visit_namespace(ns &ns) {
    visit_ns_element(ns);
}

void default_model_visitor::visit_function(function &func) {
    visit_ns_element(func);
}

void default_model_visitor::visit_global_variable_definition(global_variable_definition &def) {
    visit_ns_element(def);
}

void default_model_visitor::visit_statement(statement &stmt) {
    visit_element(stmt);
}

void default_model_visitor::visit_block(block &stmt) {
    visit_statement(stmt);
}

void default_model_visitor::visit_return_statement(return_statement &stmt) {
    visit_statement(stmt);
}

void default_model_visitor::visit_if_else_statement(if_else_statement &stmt) {
    visit_statement(stmt);
}

void default_model_visitor::visit_while_statement(while_statement& stmt) {
    visit_statement(stmt);
}

void default_model_visitor::visit_for_statement(for_statement& stmt) {
    visit_statement(stmt);
}

void default_model_visitor::visit_expression_statement(expression_statement &stmt) {
    visit_statement(stmt);
}

void default_model_visitor::visit_variable_statement(variable_statement &stmt) {
    visit_statement(stmt);
}

void default_model_visitor::visit_expression(expression &expr) {
    visit_element(expr);
}

void default_model_visitor::visit_value_expression(value_expression &expr) {
    visit_expression(expr);
}

void default_model_visitor::visit_symbol_expression(symbol_expression &expr) {
    visit_expression(expr);
}

void default_model_visitor::visit_unary_expression(unary_expression &expr) {
    visit_expression(expr);
}

void default_model_visitor::visit_cast_expression(cast_expression &expr) {
    visit_unary_expression(expr);
}

void default_model_visitor::visit_binary_expression(binary_expression &expr) {
    visit_expression(expr);
}

void default_model_visitor::visit_arithmetic_binary_expression(arithmetic_binary_expression &expr) {
    visit_binary_expression(expr);
}

void default_model_visitor::visit_addition_expression(addition_expression &expr) {
    visit_arithmetic_binary_expression(expr);
}

void default_model_visitor::visit_substraction_expression(substraction_expression &expr) {
    visit_arithmetic_binary_expression(expr);
}

void default_model_visitor::visit_multiplication_expression(multiplication_expression &expr) {
    visit_arithmetic_binary_expression(expr);
}

void default_model_visitor::visit_division_expression(division_expression &expr) {
    visit_arithmetic_binary_expression(expr);
}

void default_model_visitor::visit_modulo_expression(modulo_expression &expr) {
    visit_arithmetic_binary_expression(expr);
}

void default_model_visitor::visit_bitwise_and_expression(bitwise_and_expression &expr) {
    visit_arithmetic_binary_expression(expr);
}

void default_model_visitor::visit_bitwise_or_expression(bitwise_or_expression &expr) {
    visit_arithmetic_binary_expression(expr);
}

void default_model_visitor::visit_bitwise_xor_expression(bitwise_xor_expression &expr) {
    visit_arithmetic_binary_expression(expr);
}

void default_model_visitor::visit_left_shift_expression(left_shift_expression &expr) {
    visit_arithmetic_binary_expression(expr);
}

void default_model_visitor::visit_right_shift_expression(right_shift_expression &expr) {
    visit_arithmetic_binary_expression(expr);
}

void default_model_visitor::visit_assignation_expression(assignation_expression &expr) {
    visit_binary_expression(expr);
}

void default_model_visitor::visit_simple_assignation_expression(simple_assignation_expression &expr) {
    visit_assignation_expression(expr);
}

void default_model_visitor::visit_arithmetic_assignation_expression(arithmetic_assignation_expression& expr) {
    visit_assignation_expression(expr);
}

void default_model_visitor::visit_addition_assignation_expression(additition_assignation_expression &expr) {
    visit_arithmetic_assignation_expression(expr);
}

void default_model_visitor::visit_substraction_assignation_expression(substraction_assignation_expression &expr) {
    visit_arithmetic_assignation_expression(expr);
}

void default_model_visitor::visit_multiplication_assignation_expression(multiplication_assignation_expression &expr) {
    visit_arithmetic_assignation_expression(expr);
}

void default_model_visitor::visit_division_assignation_expression(division_assignation_expression &expr) {
    visit_arithmetic_assignation_expression(expr);
}

void default_model_visitor::visit_modulo_assignation_expression(modulo_assignation_expression &expr) {
    visit_arithmetic_assignation_expression(expr);
}

void default_model_visitor::visit_bitwise_and_assignation_expression(bitwise_and_assignation_expression &expr) {
    visit_arithmetic_assignation_expression(expr);
}

void default_model_visitor::visit_bitwise_or_assignation_expression(bitwise_or_assignation_expression &expr) {
    visit_arithmetic_assignation_expression(expr);
}

void default_model_visitor::visit_bitwise_xor_assignation_expression(bitwise_xor_assignation_expression &expr) {
    visit_arithmetic_assignation_expression(expr);
}

void default_model_visitor::visit_left_shift_assignation_expression(left_shift_assignation_expression &expr) {
    visit_arithmetic_assignation_expression(expr);
}

void default_model_visitor::visit_right_shift_assignation_expression(right_shift_assignation_expression &expr) {
    visit_arithmetic_assignation_expression(expr);
}

void default_model_visitor::visit_arithmetic_unary_expression(arithmetic_unary_expression &expr) {
    visit_unary_expression(expr);
}

void default_model_visitor::visit_unary_plus_expression(unary_plus_expression &expr) {
    visit_arithmetic_unary_expression(expr);
}

void default_model_visitor::visit_unary_minus_expression(unary_minus_expression &expr) {
    visit_arithmetic_unary_expression(expr);
}

void default_model_visitor::visit_bitwise_not_expression(bitwise_not_expression &expr) {
    visit_arithmetic_unary_expression(expr);
}

void default_model_visitor::visit_logical_binary_expression(logical_binary_expression &expr) {
    visit_binary_expression(expr);
}

void default_model_visitor::visit_logical_and_expression(logical_and_expression &expr) {
    visit_logical_binary_expression(expr);
}

void default_model_visitor::visit_logical_or_expression(logical_or_expression &expr) {
    visit_logical_binary_expression(expr);
}

void default_model_visitor::visit_logical_not_expression(logical_not_expression &expr) {
    visit_unary_expression(expr);
}

void default_model_visitor::visit_load_value_expression(load_value_expression& expr) {
    visit_unary_expression(expr);
}

void default_model_visitor::visit_address_of_expression(address_of_expression &expr) {
    visit_unary_expression(expr);
}

void default_model_visitor::visit_dereference_expression(dereference_expression &expr) {
    visit_unary_expression(expr);
}

void default_model_visitor::visit_comparison_expression(comparison_expression &expr) {
    visit_binary_expression(expr);
}

void default_model_visitor::visit_equal_expression(equal_expression &expr) {
    visit_comparison_expression(expr);
}

void default_model_visitor::visit_different_expression(different_expression &expr) {
    visit_comparison_expression(expr);
}

void default_model_visitor::visit_lesser_expression(lesser_expression &expr) {
    visit_comparison_expression(expr);
}

void default_model_visitor::visit_greater_expression(greater_expression &expr) {
    visit_comparison_expression(expr);
}

void default_model_visitor::visit_lesser_equal_expression(lesser_equal_expression &expr) {
    visit_comparison_expression(expr);
}

void default_model_visitor::visit_greater_equal_expression(greater_equal_expression &expr) {
    visit_comparison_expression(expr);
}

void default_model_visitor::visit_subscript_expression(subscript_expression& expr) {
    visit_binary_expression(expr);
}

void default_model_visitor::visit_function_invocation_expression(function_invocation_expression &expr) {
    visit_expression(expr);
}




} // namespace k::model
