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

#ifndef KLANG_MODEL_VISITOR_HPP
#define KLANG_MODEL_VISITOR_HPP

#include "model.hpp"
#include "expressions.hpp"
#include "statements.hpp"

namespace k::model {

class global_tool_function;
class global_constructor_function;
class global_destructor_function;


class model_visitor {
public:
    virtual void visit_element(element&) =0;

    virtual void visit_unit(unit&) =0;

    virtual void visit_namespace(ns&) =0;
    virtual void visit_structure(structure&) =0;
    virtual void visit_function(function&) =0;
    virtual void visit_global_tool_function(global_tool_function&) =0;
    virtual void visit_global_constructor_function(global_constructor_function&) =0;
    virtual void visit_global_destructor_function(global_destructor_function&) =0;
    virtual void visit_parameter(parameter&) =0;
    virtual void visit_global_variable_definition(global_variable_definition&) =0;
    virtual void visit_member_variable_definition(member_variable_definition&) =0;

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
    virtual void visit_arithmetic_assignation_expression(arithmetic_assignation_expression&) =0;
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

    virtual void visit_load_value_expression(load_value_expression&) =0;
    virtual void visit_address_of_expression(address_of_expression&) =0;
    virtual void visit_dereference_expression(dereference_expression&) =0;
    virtual void visit_member_of_expression(member_of_expression&) =0;
    virtual void visit_member_of_object_expression(member_of_object_expression&) =0;
    virtual void visit_member_of_pointer_expression(member_of_pointer_expression&) =0;

    virtual void visit_comparison_expression(comparison_expression&) =0;
    virtual void visit_equal_expression(equal_expression&) =0;
    virtual void visit_different_expression(different_expression&) =0;
    virtual void visit_lesser_expression(lesser_expression&) =0;
    virtual void visit_greater_expression(greater_expression&) =0;
    virtual void visit_lesser_equal_expression(lesser_equal_expression&) =0;
    virtual void visit_greater_equal_expression(greater_equal_expression&) =0;

    virtual void visit_subscript_expression(subscript_expression&) =0;
    virtual void visit_function_invocation_expression(function_invocation_expression&) =0;
};


class default_model_visitor : public model_visitor {
public:
    void visit_element(element&) override;

    void visit_unit(unit&) override;

    void visit_namespace(ns&) override;
    void visit_structure(structure&) override;
    void visit_function(function&) override;
    void visit_global_tool_function(global_tool_function&) override;
    void visit_global_constructor_function(global_constructor_function&) override;
    void visit_global_destructor_function(global_destructor_function&) override;
    void visit_parameter(parameter&) override;
    void visit_global_variable_definition(global_variable_definition&) override;
    void visit_member_variable_definition(member_variable_definition&) override;

    void visit_statement(statement&) override;
    void visit_block(block&) override;
    void visit_return_statement(return_statement&) override;
    void visit_if_else_statement(if_else_statement&) override;
    void visit_while_statement(while_statement&) override;
    void visit_for_statement(for_statement&) override;
    void visit_expression_statement(expression_statement&) override;
    void visit_variable_statement(variable_statement&) override;

    void visit_expression(expression&) override;
    void visit_value_expression(value_expression&) override;
    void visit_symbol_expression(symbol_expression&) override;

    void visit_unary_expression(unary_expression&) override;
    void visit_cast_expression(cast_expression&) override;

    void visit_binary_expression(binary_expression&) override;

    void visit_arithmetic_binary_expression(arithmetic_binary_expression&) override;
    void visit_addition_expression(addition_expression&) override;
    void visit_substraction_expression(substraction_expression&) override;
    void visit_multiplication_expression(multiplication_expression&) override;
    void visit_division_expression(division_expression&) override;
    void visit_modulo_expression(modulo_expression&) override;
    void visit_bitwise_and_expression(bitwise_and_expression&) override;
    void visit_bitwise_or_expression(bitwise_or_expression&) override;
    void visit_bitwise_xor_expression(bitwise_xor_expression&) override;
    void visit_left_shift_expression(left_shift_expression&) override;
    void visit_right_shift_expression(right_shift_expression&) override;

    void visit_assignation_expression(assignation_expression&) override;
    void visit_simple_assignation_expression(simple_assignation_expression&) override;
    void visit_arithmetic_assignation_expression(arithmetic_assignation_expression&) override;
    void visit_addition_assignation_expression(additition_assignation_expression&) override;
    void visit_substraction_assignation_expression(substraction_assignation_expression&) override;
    void visit_multiplication_assignation_expression(multiplication_assignation_expression&) override;
    void visit_division_assignation_expression(division_assignation_expression&) override;
    void visit_modulo_assignation_expression(modulo_assignation_expression&) override;
    void visit_bitwise_and_assignation_expression(bitwise_and_assignation_expression&) override;
    void visit_bitwise_or_assignation_expression(bitwise_or_assignation_expression&) override;
    void visit_bitwise_xor_assignation_expression(bitwise_xor_assignation_expression&) override;
    void visit_left_shift_assignation_expression(left_shift_assignation_expression&) override;
    void visit_right_shift_assignation_expression(right_shift_assignation_expression&) override;

    void visit_arithmetic_unary_expression(arithmetic_unary_expression&) override;
    void visit_unary_plus_expression(unary_plus_expression&) override;
    void visit_unary_minus_expression(unary_minus_expression&) override;
    void visit_bitwise_not_expression(bitwise_not_expression&) override;

    void visit_logical_binary_expression(logical_binary_expression&) override;
    void visit_logical_and_expression(logical_and_expression&) override;
    void visit_logical_or_expression(logical_or_expression&) override;
    void visit_logical_not_expression(logical_not_expression&) override;
    void visit_member_of_expression(member_of_expression&) override;
    void visit_member_of_object_expression(member_of_object_expression&) override;
    void visit_member_of_pointer_expression(member_of_pointer_expression&) override;

    void visit_load_value_expression(load_value_expression&) override;
    void visit_address_of_expression(address_of_expression&) override;
    void visit_dereference_expression(dereference_expression&) override;

    void visit_comparison_expression(comparison_expression&) override;
    void visit_equal_expression(equal_expression&) override;
    void visit_different_expression(different_expression&) override;
    void visit_lesser_expression(lesser_expression&) override;
    void visit_greater_expression(greater_expression&) override;
    void visit_lesser_equal_expression(lesser_equal_expression&) override;
    void visit_greater_equal_expression(greater_equal_expression&) override;

    void visit_subscript_expression(subscript_expression&) override;
    void visit_function_invocation_expression(function_invocation_expression&) override;
};

} // namespace k::model
#endif //KLANG_MODEL_VISITOR_HPP
