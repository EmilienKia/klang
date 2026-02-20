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

#include "operators.hpp"

#include "context.hpp"
#include "model_visitor.hpp"

namespace k::model {


//
// Arithmetic binary expression
//
void arithmetic_binary_expression::accept(model_visitor &visitor) {
    visitor.visit_arithmetic_binary_expression(*this);
}

//
// Addition expression
//
void addition_expression::accept(model_visitor &visitor) {
    visitor.visit_addition_expression(*this);
}

//
// Substraction expression
//
void substraction_expression::accept(model_visitor &visitor) {
    visitor.visit_substraction_expression(*this);
}

//
// Multiplication expression
//
void multiplication_expression::accept(model_visitor &visitor) {
    visitor.visit_multiplication_expression(*this);
}

//
// Division expression
//
void division_expression::accept(model_visitor &visitor) {
    visitor.visit_division_expression(*this);
}

//
// Modulo expression
//
void modulo_expression::accept(model_visitor &visitor) {
    visitor.visit_modulo_expression(*this);
}

//
// Bitwise AND expression
//
void bitwise_and_expression::accept(model_visitor &visitor) {
    visitor.visit_bitwise_and_expression(*this);
}

//
// Bitwise OR expression
//
void bitwise_or_expression::accept(model_visitor &visitor) {
    visitor.visit_bitwise_or_expression(*this);
}

//
// Bitwise XOR expression
//
void bitwise_xor_expression::accept(model_visitor &visitor) {
    visitor.visit_bitwise_xor_expression(*this);
}

//
// Bitwise left shift expression
//
void left_shift_expression::accept(model_visitor &visitor) {
    visitor.visit_left_shift_expression(*this);
}

//
// Bitwise right shift expression
//
void right_shift_expression::accept(model_visitor &visitor) {
    visitor.visit_right_shift_expression(*this);
}

//
// Assignation expression
//
void assignation_expression::accept(model_visitor &visitor) {
    visitor.visit_assignation_expression(*this);
}

//
// Simple assignation expression
//
void simple_assignation_expression::accept(model_visitor &visitor) {
    visitor.visit_simple_assignation_expression(*this);
}

//
// Arithmetic assignation expression
//
void arithmetic_assignation_expression::accept(model_visitor &visitor) {
    visitor.visit_arithmetic_assignation_expression(*this);
}

//
// Addition assignation expression
//
void additition_assignation_expression::accept(model_visitor &visitor) {
    visitor.visit_addition_assignation_expression(*this);
}

//
// Substraction assignation expression
//
void substraction_assignation_expression::accept(model_visitor &visitor) {
    visitor.visit_substraction_assignation_expression(*this);
}

//
// Multiplication assignation expression
//
void multiplication_assignation_expression::accept(model_visitor &visitor) {
    visitor.visit_multiplication_assignation_expression(*this);
}

//
// Division assignation expression
//
void division_assignation_expression::accept(model_visitor &visitor) {
    visitor.visit_division_assignation_expression(*this);
}

//
// Modulo assignation expression
//
void modulo_assignation_expression::accept(model_visitor &visitor) {
    visitor.visit_modulo_assignation_expression(*this);
}

//
// Bitwise AND assignation expression
//
void bitwise_and_assignation_expression::accept(model_visitor &visitor) {
    visitor.visit_bitwise_and_assignation_expression(*this);
}

//
// Bitwise OR assignation expression
//
void bitwise_or_assignation_expression::accept(model_visitor &visitor) {
    visitor.visit_bitwise_or_assignation_expression(*this);
}

//
// Bitwise XOR assignation expression
//
void bitwise_xor_assignation_expression::accept(model_visitor &visitor) {
    visitor.visit_bitwise_xor_assignation_expression(*this);
}

//
// Bitwise left shift assignation expression
//
void left_shift_assignation_expression::accept(model_visitor &visitor) {
    visitor.visit_left_shift_assignation_expression(*this);
}

//
// Bitwise right shift assignation expression
//
void right_shift_assignation_expression::accept(model_visitor &visitor) {
    visitor.visit_right_shift_assignation_expression(*this);
}

//
// Arithmetic unary expression
//
void arithmetic_unary_expression::accept(model_visitor &visitor) {
    visitor.visit_arithmetic_unary_expression(*this);
}

//
// Arithmetic unary plus expression
//
void unary_plus_expression::accept(model_visitor &visitor) {
    visitor.visit_unary_plus_expression(*this);
}

//
// Arithmetic unary minus expression
//
void unary_minus_expression::accept(model_visitor &visitor) {
    visitor.visit_unary_minus_expression(*this);
}

//
// Bitwise not expression
//
void bitwise_not_expression::accept(model_visitor &visitor) {
    visitor.visit_bitwise_not_expression(*this);
}

//
// Logical binary expression
//
void logical_binary_expression::accept(model_visitor &visitor) {
    visitor.visit_logical_binary_expression(*this);
}

//
// Logical AND expression
//
void logical_and_expression::accept(model_visitor &visitor) {
    visitor.visit_logical_and_expression(*this);
}

//
// Logical OR exception
//
void logical_or_expression::accept(model_visitor &visitor) {
    visitor.visit_logical_or_expression(*this);
}

//
// Logical NOT exception
//
void logical_not_expression::accept(model_visitor &visitor) {
    visitor.visit_logical_not_expression(*this);
}

//
// Comparison expression
//
void comparison_expression::accept(model_visitor &visitor) {
    visitor.visit_comparison_expression(*this);
}

//
// Comparison equal expression
//
void equal_expression::accept(model_visitor &visitor) {
    visitor.visit_equal_expression(*this);
}

//
// Comparison different expression
//
void different_expression::accept(model_visitor &visitor) {
    visitor.visit_different_expression(*this);
}

//
// Comparison lesser expression
//
void lesser_expression::accept(model_visitor &visitor) {
    visitor.visit_lesser_expression(*this);
}

//
// Comparison greater expression
//
void greater_expression::accept(model_visitor &visitor) {
    visitor.visit_greater_expression(*this);
}

//
// Comparison lesser or equal expression
//
void lesser_equal_expression::accept(model_visitor &visitor) {
    visitor.visit_lesser_equal_expression(*this);
}

//
// Comparison greater or equal expression
//
void greater_equal_expression::accept(model_visitor &visitor) {
    visitor.visit_greater_equal_expression(*this);
}


} // namespace k::model
