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

#ifndef KLANG_MODEL_VISITOR_HPP
#define KLANG_MODEL_VISITOR_HPP

#include "model.hpp"
#include "expressions.hpp"
#include "statements.hpp"

namespace k::model {


class model_visitor {
public:
    virtual void visit_element(element&) =0;

    virtual void visit_unit(unit&) =0;

    virtual void visit_ns_element(ns_element&) =0;
    virtual void visit_namespace(ns&) =0;
    virtual void visit_function(function&) =0;
    virtual void visit_global_variable_definition(global_variable_definition&) =0;

    virtual void visit_statement(statement&) =0;
    virtual void visit_block(block&) =0;
    virtual void visit_return_statement(return_statement&) =0;
    virtual void visit_if_else_statement(if_else_statement&) =0;
    virtual void visit_while_statement(while_statement&) =0;
    virtual void visit_for_statement(for_statement&) =0;
    virtual void visit_expression_statement(expression_statement&) =0;
    virtual void visit_variable_statement(variable_statement&) =0;

    virtual void visit_expression(expression&) =0;
    virtual void visit_value_expression(value_expression&) =0;
    virtual void visit_symbol_expression(symbol_expression&) =0;

    virtual void visit_unary_expression(unary_expression&) =0;
    virtual void visit_cast_expression(cast_expression&) =0;

    virtual void visit_binary_expression(binary_expression&) =0;
    virtual void visit_arithmetic_binary_expression(arithmetic_binary_expression&) =0;
    virtual void visit_addition_expression(addition_expression&) =0;
    virtual void visit_substraction_expression(substraction_expression&) =0;
    virtual void visit_multiplication_expression(multiplication_expression&) =0;
    virtual void visit_division_expression(division_expression&) =0;
    virtual void visit_modulo_expression(modulo_expression&) =0;
    virtual void visit_bitwise_and_expression(bitwise_and_expression&) =0;
    virtual void visit_bitwise_or_expression(bitwise_or_expression&) =0;
    virtual void visit_bitwise_xor_expression(bitwise_xor_expression&) =0;
    virtual void visit_left_shift_expression(left_shift_expression&) =0;
    virtual void visit_right_shift_expression(right_shift_expression&) =0;

    virtual void visit_assignation_expression(assignation_expression&) =0;
    virtual void visit_simple_assignation_expression(simple_assignation_expression&) =0;
    virtual void visit_addition_assignation_expression(additition_assignation_expression&) =0;
    virtual void visit_substraction_assignation_expression(substraction_assignation_expression&) =0;
    virtual void visit_multiplication_assignation_expression(multiplication_assignation_expression&) =0;
    virtual void visit_division_assignation_expression(division_assignation_expression&) =0;
    virtual void visit_modulo_assignation_expression(modulo_assignation_expression&) =0;
    virtual void visit_bitwise_and_assignation_expression(bitwise_and_assignation_expression&) =0;
    virtual void visit_bitwise_or_assignation_expression(bitwise_or_assignation_expression&) =0;
    virtual void visit_bitwise_xor_assignation_expression(bitwise_xor_assignation_expression&) =0;
    virtual void visit_left_shift_assignation_expression(left_shift_assignation_expression&) =0;
    virtual void visit_right_shift_assignation_expression(right_shift_assignation_expression&) =0;

    virtual void visit_arithmetic_unary_expression(arithmetic_unary_expression&) =0;
    virtual void visit_unary_plus_expression(unary_plus_expression&) =0;
    virtual void visit_unary_minus_expression(unary_minus_expression&) =0;
    virtual void visit_bitwise_not_expression(bitwise_not_expression&) =0;

    virtual void visit_logical_binary_expression(logical_binary_expression&) =0;
    virtual void visit_logical_and_expression(logical_and_expression&) =0;
    virtual void visit_logical_or_expression(logical_or_expression&) =0;
    virtual void visit_logical_not_expression(logical_not_expression&) =0;

    virtual void visit_comparison_expression(comparison_expression&) =0;
    virtual void visit_equal_expression(equal_expression&) =0;
    virtual void visit_different_expression(different_expression&) =0;
    virtual void visit_lesser_expression(lesser_expression&) =0;
    virtual void visit_greater_expression(greater_expression&) =0;
    virtual void visit_lesser_equal_expression(lesser_equal_expression&) =0;
    virtual void visit_greater_equal_expression(greater_equal_expression&) =0;

    virtual void visit_function_invocation_expression(function_invocation_expression&) =0;
};


class default_model_visitor : public model_visitor {
public:
    virtual void visit_element(element&) override;

    virtual void visit_unit(unit&) override;

    virtual void visit_ns_element(ns_element&) override;
    virtual void visit_namespace(ns&) override;
    virtual void visit_function(function&) override;
    virtual void visit_global_variable_definition(global_variable_definition&) override;

    virtual void visit_statement(statement&) override;
    virtual void visit_block(block&) override;
    virtual void visit_return_statement(return_statement&) override;
    virtual void visit_if_else_statement(if_else_statement&) override;
    virtual void visit_while_statement(while_statement&) override;
    virtual void visit_for_statement(for_statement&) override;
    virtual void visit_expression_statement(expression_statement&) override;
    virtual void visit_variable_statement(variable_statement&) override;

    virtual void visit_expression(expression&) override;
    virtual void visit_value_expression(value_expression&) override;
    virtual void visit_symbol_expression(symbol_expression&) override;

    virtual void visit_unary_expression(unary_expression&) override;
    virtual void visit_cast_expression(cast_expression&) override;

    virtual void visit_binary_expression(binary_expression&) override;

    virtual void visit_arithmetic_binary_expression(arithmetic_binary_expression&) override;
    virtual void visit_addition_expression(addition_expression&) override;
    virtual void visit_substraction_expression(substraction_expression&) override;
    virtual void visit_multiplication_expression(multiplication_expression&) override;
    virtual void visit_division_expression(division_expression&) override;
    virtual void visit_modulo_expression(modulo_expression&) override;
    virtual void visit_bitwise_and_expression(bitwise_and_expression&) override;
    virtual void visit_bitwise_or_expression(bitwise_or_expression&) override;
    virtual void visit_bitwise_xor_expression(bitwise_xor_expression&) override;
    virtual void visit_left_shift_expression(left_shift_expression&) override;
    virtual void visit_right_shift_expression(right_shift_expression&) override;

    virtual void visit_assignation_expression(assignation_expression&) override;
    virtual void visit_simple_assignation_expression(simple_assignation_expression&) override;
    virtual void visit_addition_assignation_expression(additition_assignation_expression&) override;
    virtual void visit_substraction_assignation_expression(substraction_assignation_expression&) override;
    virtual void visit_multiplication_assignation_expression(multiplication_assignation_expression&) override;
    virtual void visit_division_assignation_expression(division_assignation_expression&) override;
    virtual void visit_modulo_assignation_expression(modulo_assignation_expression&) override;
    virtual void visit_bitwise_and_assignation_expression(bitwise_and_assignation_expression&) override;
    virtual void visit_bitwise_or_assignation_expression(bitwise_or_assignation_expression&) override;
    virtual void visit_bitwise_xor_assignation_expression(bitwise_xor_assignation_expression&) override;
    virtual void visit_left_shift_assignation_expression(left_shift_assignation_expression&) override;
    virtual void visit_right_shift_assignation_expression(right_shift_assignation_expression&) override;

    virtual void visit_arithmetic_unary_expression(arithmetic_unary_expression&) override;
    virtual void visit_unary_plus_expression(unary_plus_expression&) override;
    virtual void visit_unary_minus_expression(unary_minus_expression&) override;
    virtual void visit_bitwise_not_expression(bitwise_not_expression&) override;

    virtual void visit_logical_binary_expression(logical_binary_expression&) override;
    virtual void visit_logical_and_expression(logical_and_expression&) override;
    virtual void visit_logical_or_expression(logical_or_expression&) override;
    virtual void visit_logical_not_expression(logical_not_expression&) override;

    virtual void visit_comparison_expression(comparison_expression&) override;
    virtual void visit_equal_expression(equal_expression&) override;
    virtual void visit_different_expression(different_expression&) override;
    virtual void visit_lesser_expression(lesser_expression&) override;
    virtual void visit_greater_expression(greater_expression&) override;
    virtual void visit_lesser_equal_expression(lesser_equal_expression&) override;
    virtual void visit_greater_equal_expression(greater_equal_expression&) override;

    virtual void visit_function_invocation_expression(function_invocation_expression&) override;
};

} // namespace k::model
#endif //KLANG_MODEL_VISITOR_HPP
