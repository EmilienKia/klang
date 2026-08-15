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

#ifndef KLANG_MODEL_CONSTANT_EVALUATOR_HPP
#define KLANG_MODEL_CONSTANT_EVALUATOR_HPP

#include "constant_value.hpp"

#include <memory>
#include <optional>
#include <string>

namespace k::model {

class type;
class aggregate;
class union_type_def;

enum class binary_arith_op {
    ADD,
    SUB,
    MUL,
    DIV,
    MOD,
    BITWISE_AND,
    BITWISE_OR,
    BITWISE_XOR,
    SHIFT_LEFT,
    SHIFT_RIGHT
};

enum class comparison_op {
    EQUAL,
    NOT_EQUAL,
    LESS,
    LESS_EQUAL,
    GREATER,
    GREATER_EQUAL,
    SPACESHIP
};

enum class unary_op {
    PLUS,
    MINUS,
    BITWISE_NOT,
    LOGICAL_NOT
};

class constant_evaluator {
public:
    /**
     * Narrow/cast a constant value to a target type (primitives, enums).
     */
    static std::optional<constant_value> cast_to_type(
        const constant_value& val,
        const std::shared_ptr<type>& target_type
    );

    /**
     * Compute default/zero value for a given type (primitives, enums, structs).
     */
    static std::optional<constant_value> default_value_for_type(
        const std::shared_ptr<type>& target_type
    );

    /**
     * Evaluate a binary arithmetic operation.
     */
    static std::optional<constant_value> eval_binary_arithmetic(
        binary_arith_op op,
        const constant_value& left,
        const constant_value& right,
        const std::shared_ptr<type>& result_type
    );

    /**
     * Evaluate a binary comparison operation.
     */
    static std::optional<constant_value> eval_comparison(
        comparison_op op,
        const constant_value& left,
        const constant_value& right
    );

    /**
     * Evaluate a logical binary operation (&&, ||).
     */
    static std::optional<constant_value> eval_logical_binary(
        bool is_and,
        const constant_value& left,
        const constant_value& right
    );

    /**
     * Evaluate a unary operation (+, -, ~, !).
     */
    static std::optional<constant_value> eval_unary(
        unary_op op,
        const constant_value& operand,
        const std::shared_ptr<type>& result_type
    );

    /**
     * Evaluate a ternary conditional (cond ? then_val : else_val).
     */
    static std::optional<constant_value> eval_ternary(
        const constant_value& cond,
        const constant_value& then_val,
        const constant_value& else_val
    );

    /**
     * Access a member of a constant struct or active alternative of a constant union.
     */
    static std::optional<constant_value> eval_member_access(
        const constant_value& base,
        const std::string& member_name
    );

    /**
     * Build a constant struct_value from an aggregate definition and map of field values.
     * Unspecified fields are populated with their default values.
     */
    static std::optional<constant_value> eval_struct_init(
        const std::shared_ptr<aggregate>& struct_def,
        const std::map<std::string, constant_value>& field_values
    );

    /**
     * Build a constant union_value from a union definition, alternative name, and value.
     */
    static std::optional<constant_value> eval_union_init(
        const std::shared_ptr<union_type_def>& union_def,
        const std::string& alt_name,
        const constant_value& alt_value
    );
};

} // namespace k::model

#endif // KLANG_MODEL_CONSTANT_EVALUATOR_HPP

