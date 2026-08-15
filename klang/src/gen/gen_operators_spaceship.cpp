/*
 * K Language compiler
 *
 * Copyright 2026 Emilien Kia
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

#include "resolvers.hpp"
#include "generators.hpp"
#include "gen_helpers.hpp"
#include "../common/operator_names.hpp"
#include "../parse/ast.hpp"

#include "../errors.hpp"
#include "../model/constant_evaluator.hpp"
#include "gen_operators_helpers.hpp"

namespace k::model::gen {

//
// Spaceship (three-way comparison) expression: `a <=> b`
//

void type_reference_resolver::visit_spaceship_expression(spaceship_expression& expr) {
    // Step 1: Resolve both operands.
    visit_binary_expression(expr);

    auto left = expr.left();
    auto right = expr.right();

    auto left_type = left->get_type();
    auto right_type = right->get_type();

    // Strip one level of reference/drain to inspect the underlying type; remember constness.
    auto check_left = left_type;
    if (type::is_reference(check_left) || type::is_drain(check_left)) {
        check_left = check_left->get_subtype();
    }
    bool is_const_left = type::is_const(check_left);
    check_left = type::remove_const(check_left);

    // ── Operator overload for aggregate (struct/class/interface) left operand ──
    if (type::is_struct(check_left)) {
        auto st_type = std::dynamic_pointer_cast<struct_type>(check_left);
        if (st_type) {
            auto agg = st_type->get_struct();
            if (agg) {
                auto [op_func, adapted_right] = resolve_binary_operator_overload(expr, agg, left, right, is_const_left);
                if (op_func) {
                    expr.set_operator_func(op_func);
                    if (adapted_right && adapted_right != right) {
                        expr.assign_right(adapted_right);
                    }
                    if (!op_func->has_return_type() || !is_spaceship_return_shape_ok(op_func->get_return_type())) {
                        throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_SPACESHIP_BAD_RETURN_TYPE), expr.first_lexeme(),
                            "'operator <=>' must return a signed integer or floating-point primitive type, "
                            "or an aggregate (struct/class) type comparable to the integer literal 0; "
                            "'{}' declares a return type of '{}'",
                            {agg->get_short_name(),
                             op_func->has_return_type() ? op_func->get_return_type()->to_string() : "void"});
                    }
                    expr.set_type(op_func->get_return_type());
                    if (op_func->is_member()) {
                        auto di = compute_operator_dispatch_info(op_func, left_type);
                        expr.set_operator_dispatch_info(std::move(di));
                    } else {
                        virtual_dispatch_info di;
                        di.kind = virtual_dispatch_info::dispatch_kind::DIRECT;
                        expr.set_operator_dispatch_info(std::move(di));
                    }
                    return;
                }
            }
        }
        throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_SPACESHIP_OVERLOAD_NOT_FOUND), expr.first_lexeme(),
            "No matching 'operator <=>' overload found for type '{}': "
            "declare a member or non-member 'operator <=>' to use the spaceship operator on this type",
            {check_left ? check_left->to_string() : "?"});
    }

    // ── Builtin primitive spaceship: result is always the signed `int` type ──
    if(type::is_reference(left_type) || type::is_drain(left_type)) {
        left = adapt_reference_load_value(left);
        expr.assign_left(left);
        left_type = left_type->get_subtype();
    }
    left_type = type::remove_const(left_type);

    if(type::is_reference(right_type) || type::is_drain(right_type)) {
        right = adapt_reference_load_value(right);
        expr.assign_right(right);
        right_type = right_type->get_subtype();
    }
    right_type = type::remove_const(right_type);

    // Enum → underlying primitive conversion for both operands.
    {
        auto left_enum = std::dynamic_pointer_cast<enum_type>(left_type);
        auto right_enum = std::dynamic_pointer_cast<enum_type>(right_type);
        if (left_enum || right_enum) {
            auto left_underlying = left_enum ? left_enum->get_underlying_type()
                                             : std::dynamic_pointer_cast<primitive_type>(left_type);
            auto right_underlying = right_enum ? right_enum->get_underlying_type()
                                               : std::dynamic_pointer_cast<primitive_type>(right_type);
            if (!left_underlying || !right_underlying) {
                throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_SPACESHIP_NOT_PRIMITIVE), expr.first_lexeme(),
                    "Cannot apply '<=>' between an enum and a non-primitive type: "
                    "operands have types '{}' and '{}'",
                    {left_type ? left_type->to_string() : "?",
                     right_type ? right_type->to_string() : "?"});
            }
            auto common_type = left_underlying;
            if (right_underlying->type_size() > left_underlying->type_size()) {
                common_type = right_underlying;
            }
            left = adapt_type(left, common_type);
            right = adapt_type(right, common_type);
            if (left) expr.assign_left(left);
            if (right) expr.assign_right(right);
            left_type = common_type;
            right_type = common_type;
        }
    }

    if(!type::is_primitive(left_type) || !type::is_primitive(right_type)) {
        throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_SPACESHIP_NOT_PRIMITIVE), expr.first_lexeme(),
            "The '<=>' operator is not supported for non-primitive types without a declared 'operator <=>': "
            "operands have types '{}' and '{}'",
            {left_type ? left_type->to_string() : "?",
             right_type ? right_type->to_string() : "?"});
    }

    auto left_prim_type = std::dynamic_pointer_cast<primitive_type>(type::remove_const(left_type));
    auto right_prim_type = std::dynamic_pointer_cast<primitive_type>(type::remove_const(right_type));

    auto adapted_left = left;
    auto adapted_right = right;

    if(left_prim_type->is_boolean() && !right_prim_type->is_boolean()) {
        adapted_right = adapt_type(right, left_prim_type);
    } else if(!left_prim_type->is_boolean() && right_prim_type->is_boolean()) {
        adapted_left = adapt_type(left, right_prim_type);
    } else {
        // TODO rework to promote to the largest integer/float type of both, like arithmetic.
        adapted_right = adapt_type(right, left_prim_type);
    }

    if(!adapted_left || !adapted_right) {
        throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_SPACESHIP_NOT_PRIMITIVE), expr.first_lexeme(),
            "Incompatible types in spaceship comparison: "
            "cannot align operand types '{}' and '{}' for '<=>'; "
            "use an explicit cast to make the types comparable",
            {left_type ? left_type->to_string() : "?",
             right_type ? right_type->to_string() : "?"});
    }

    if(adapted_left != left) {
        expr.assign_left(adapted_left);
    }
    if(adapted_right != right) {
        expr.assign_right(adapted_right);
    }

    // Result is always the builtin signed `int`, regardless of the operand type.
    // NB: intentionally not cached in a `static` local — from_type() returns a
    // type bound to *this* compiler instance's LLVMContext, and a `static` cache
    // would leak a dangling type across independent compiler/context instances
    // (e.g. across successive unit tests, each with its own short-lived JIT).
    expr.set_type(_context->from_type(primitive_type::INT));

    if (!expr.has_operator_overload() && expr.left()->is_constant() && expr.right()->is_constant()) {
        auto res = constant_evaluator::eval_comparison(
            comparison_op::SPACESHIP, expr.left()->get_constant_value(), expr.right()->get_constant_value());
        if (res) {
            expr.set_constant_value(*res);
        }
    }
}

//
// Codegen
//

llvm::Value* implementation_generator::compare_spaceship_result_to_zero(llvm::Value* spaceship_result, const std::string& wanted_op_name) {
    llvm::Type* llvm_type = spaceship_result->getType();
    if (llvm_type->isFloatingPointTy()) {
        llvm::Value* zero = llvm::ConstantFP::get(llvm_type, 0.0);
        if (wanted_op_name == k::op::OP_EQ) return _builder->CreateFCmpOEQ(spaceship_result, zero);
        if (wanted_op_name == k::op::OP_NE) return _builder->CreateFCmpONE(spaceship_result, zero);
        if (wanted_op_name == k::op::OP_LT) return _builder->CreateFCmpOLT(spaceship_result, zero);
        if (wanted_op_name == k::op::OP_GT) return _builder->CreateFCmpOGT(spaceship_result, zero);
        if (wanted_op_name == k::op::OP_LE) return _builder->CreateFCmpOLE(spaceship_result, zero);
        if (wanted_op_name == k::op::OP_GE) return _builder->CreateFCmpOGE(spaceship_result, zero);
    } else {
        // Spaceship results are always signed (Phase 1 restriction), so always use signed comparisons.
        llvm::Value* zero = llvm::ConstantInt::get(llvm_type, 0);
        if (wanted_op_name == k::op::OP_EQ) return _builder->CreateICmpEQ(spaceship_result, zero);
        if (wanted_op_name == k::op::OP_NE) return _builder->CreateICmpNE(spaceship_result, zero);
        if (wanted_op_name == k::op::OP_LT) return _builder->CreateICmpSLT(spaceship_result, zero);
        if (wanted_op_name == k::op::OP_GT) return _builder->CreateICmpSGT(spaceship_result, zero);
        if (wanted_op_name == k::op::OP_LE) return _builder->CreateICmpSLE(spaceship_result, zero);
        if (wanted_op_name == k::op::OP_GE) return _builder->CreateICmpSGE(spaceship_result, zero);
    }
    return nullptr;
}

void implementation_generator::visit_spaceship_expression(spaceship_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    if (expr.is_constant()) {
        _value = _context->get_llvm_constant_from_constant_value(expr.get_constant_value(), expr.get_type());
        if (_value) return;
    }

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F057), expr.first_lexeme(),
            "Internal error: '<=>' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    auto prim_left = std::dynamic_pointer_cast<primitive_type>(type::remove_const(expr.left()->get_type()));
    if (!prim_left) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F058), expr.first_lexeme(),
            "Internal error: builtin '<=>' operator has a non-primitive operand during code generation; "
            "this should have been rejected during type resolution");
    }

    llvm::Value* gt;
    llvm::Value* lt;
    if (prim_left->is_integer_or_bool()) {
        if (prim_left->is_unsigned()) {
            gt = _builder->CreateICmpUGT(left, right);
            lt = _builder->CreateICmpULT(left, right);
        } else {
            gt = _builder->CreateICmpSGT(left, right);
            lt = _builder->CreateICmpSLT(left, right);
        }
    } else {
        // Ordered comparisons: NaN/infinite operands yield an unspecified (but well-defined,
        // both-false) result, matching the documented "no meaning for NaN/inf" semantics.
        gt = _builder->CreateFCmpOGT(left, right);
        lt = _builder->CreateFCmpOLT(left, right);
    }

    llvm::Type* result_llvm_type = _context->get_llvm_type(expr.get_type());
    llvm::Value* gt_i = _builder->CreateZExt(gt, result_llvm_type);
    llvm::Value* lt_i = _builder->CreateZExt(lt, result_llvm_type);
    _value = _builder->CreateSub(gt_i, lt_i);
}

} // namespace k::model::gen
