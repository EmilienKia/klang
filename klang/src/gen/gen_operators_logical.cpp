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

#include "resolvers.hpp"
#include "generators.hpp"
#include "gen_helpers.hpp"
#include "gen_callable_helpers.hpp"
#include "../common/operator_names.hpp"
#include "../parse/ast.hpp"

#include "../errors.hpp"
#include "../model/constant_evaluator.hpp"
#include "gen_operators_helpers.hpp"

#include <algorithm>
#include <optional>
#include <tuple>

namespace k::model::gen {


//
// Logical binary expression
//

/**
 * Resolve a logical binary expression (&& or ||): validate both operands are boolean
 * or can be converted to boolean.
 *
 * Steps:
 *   1. Resolve both operands.
 *   2. For struct types: look for operator overload.
 *   3. For non-bool types: insert implicit cast to bool.
 *   4. Set result type to bool.
 */
void type_reference_resolver::visit_logical_binary_expression(logical_binary_expression& expr) {
    // Step 1: Resolve both operands
    visit_binary_expression(expr);

    auto left = expr.left();
    auto right = expr.right();

    auto left_type = left->get_type();
    auto right_type = right->get_type();

    // Helper: is the type boolean-compatible? (primitive or indirection/null → bool via adapt_type)
    auto is_bool_compatible = [](const std::shared_ptr<type>& t) {
        if (type::is_primitive(t)) return true;
        if (type::is_pointer(t) || type::is_link(t) || type::is_view(t)
            || type::is_owner(t) || type::is_null(t)) return true;
        // Also accept ref<indirection>
        if (type::is_reference(t)) {
            auto inner = t->get_subtype();
            if (type::is_pointer(inner) || type::is_link(inner) ||
                type::is_view(inner) || type::is_owner(inner)) return true;
        }
        return false;
    };

    // Step 2: For struct types: look for operator overload
    // ── Operator overload for aggregate types (before reference stripping) ──
    {
        auto check_left = left_type;
        if (type::is_reference(check_left)) {
            check_left = check_left->get_subtype();
        }
        bool is_const_left = type::is_const(check_left);
        check_left = type::remove_const(check_left);
        if (type::is_struct(check_left)) {
            auto st_type = std::dynamic_pointer_cast<struct_type>(check_left);
            if (st_type) {
                auto agg = st_type->get_struct();
                if (agg) {
                    auto [op_func, adapted_right] = resolve_binary_operator_overload(expr, agg, left, right, is_const_left);
                    if (op_func) {
                        expr.set_operator_func(op_func);
                        // Apply the adapted right operand (implicit cast if needed)
                        if (adapted_right && adapted_right != right) {
                            expr.assign_right(adapted_right);
                        }
                        if (op_func->has_return_type()) {
                            expr.set_type(op_func->get_return_type());
                        } else {
                            expr.set_type(_context->from_type(primitive_type::BOOL));
                        }
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
        }
    }

    if(type::is_reference(left_type)) {
        // For ref<indirection>, don't unwrap — adapt_type handles ref<indirection>→bool.
        auto inner = left_type->get_subtype();
        if (type::is_pointer(inner) || type::is_link(inner) || type::is_view(inner)
            || type::is_owner(inner)) {
            // Leave as-is; adapt_type will handle ref<indirection>→bool.
        } else {
            left = adapt_reference_load_value(left);
            expr.assign_left(left);
            left_type = left_type->get_subtype();
        }
    } else if(type::is_drain(left_type)) {
        left = adapt_reference_load_value(left);
        expr.assign_left(left);
        left_type = left_type->get_subtype();
    }

    if(type::is_reference(right_type)) {
        auto inner = right_type->get_subtype();
        if (type::is_pointer(inner) || type::is_link(inner) || type::is_view(inner)
            || type::is_owner(inner)) {
            // Leave as-is; adapt_type will handle ref<indirection>→bool.
        } else {
            right = adapt_reference_load_value(right);
            expr.assign_right(right);
            right_type = right_type->get_subtype();
        }
    } else if(type::is_drain(right_type)) {
        right = adapt_reference_load_value(right);
        expr.assign_right(right);
        right_type = right_type->get_subtype();
    }

    if(!is_bool_compatible(left->get_type()) || !is_bool_compatible(right->get_type())) {
        throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_INVOKE_TOO_MANY_ARGS), expr.first_lexeme(),
            "Logical operators ('&&', '||') are not supported for non-primitive types: "
            "operands must be of a primitive type or indirection type convertible to boolean, "
            "but found '{}' and '{}'",
            {left->get_type() ? left->get_type()->to_string() : "?",
             right->get_type() ? right->get_type()->to_string() : "?"});
    }

    auto bool_type = _context->from_type(primitive_type::BOOL);

    // Step 3: For non-bool types: insert implicit cast to bool
    auto cast_left = adapt_type(left, bool_type);
    if(!cast_left) {
        throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_INVOKE_TOO_FEW_ARGS), expr.first_lexeme(),
            "The left operand of a logical operator cannot be implicitly converted to bool: "
            "the operand has type '{}'; logical operators require boolean-compatible operands",
            {left->get_type() ? left->get_type()->to_string() : "?"});
    } else if(cast_left != left ) {
        // Casted, assign casted expression instead of source.
        expr.assign_left(cast_left);
    } else {
        // Compatible type, no need to cast.
    }

    // Step 4: Set result type to bool
    auto cast_right = adapt_type(right, bool_type);
    if(!cast_right) {
        throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_INVOKE_NO_MATCHING_OVERLOAD), expr.first_lexeme(),
            "The right operand of a logical operator cannot be implicitly converted to bool: "
            "the operand has type '{}'; logical operators require boolean-compatible operands",
            {right->get_type() ? right->get_type()->to_string() : "?"});
    } else if(cast_right != right ) {
        // Casted, assign casted expression instead of source.
        expr.assign_right(cast_right);
    } else {
        // Compatible type, no need to cast.
    }

    // For primitive type, logical is always returning boolean
    expr.set_type(_context->from_type(primitive_type::BOOL));

    if (!expr.has_operator_overload()) {
        bool is_and = dynamic_cast<logical_and_expression*>(&expr) != nullptr;
        if (expr.left()->is_constant()) {
            bool l_val = expr.left()->get_constant_value().get_bool();
            if (is_and && !l_val) {
                expr.set_constant_value(constant_value(false));
            } else if (!is_and && l_val) {
                expr.set_constant_value(constant_value(true));
            } else if (expr.right()->is_constant()) {
                auto res = constant_evaluator::eval_logical_binary(
                    is_and, expr.left()->get_constant_value(), expr.right()->get_constant_value());
                if (res) expr.set_constant_value(*res);
            }
        }
    }
}

//
// Logical and expression (&&)
//

void implementation_generator::visit_logical_and_expression(logical_and_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    if (expr.is_constant()) {
        _value = _context->get_llvm_constant_from_constant_value(expr.get_constant_value(), expr.get_type());
        if (_value) return;
    }

    // ── Short-circuit evaluation (and-then) ─────────────────────────────────
    // Evaluate left first; if false, skip right entirely and yield false.

    // 1. Evaluate left operand
    _value = nullptr;
    expr.left()->accept(*this);
    llvm::Value* left = _value;
    if (!left) {
        _value = nullptr;
        return;
    }

    // If left operand is a reference, dereference it.
    if (type::is_reference(expr.left()->get_type()) || type::is_drain(expr.left()->get_type())) {
        llvm::Type* ty = _context->get_llvm_type(expr.left()->get_type());
        left = _builder->CreateLoad(ty, left);
    }

    // 2. Create basic blocks for short-circuit
    llvm::Function* func = _builder->GetInsertBlock()->getParent();
    llvm::BasicBlock* entry_bb = _builder->GetInsertBlock();
    llvm::BasicBlock* rhs_bb   = llvm::BasicBlock::Create(**_context, "land-rhs", func);
    llvm::BasicBlock* merge_bb = llvm::BasicBlock::Create(**_context, "land-merge");

    // 3. Branch: if left is true, evaluate right; otherwise skip to merge with false
    _builder->CreateCondBr(left, rhs_bb, merge_bb);

    // 4. Evaluate right operand (only reached if left was true)
    _builder->SetInsertPoint(rhs_bb);
    _value = nullptr;
    expr.right()->accept(*this);
    llvm::Value* right = _value;
    if (!right) {
        _value = nullptr;
        return;
    }
    // Capture the actual block after visiting right (it may have created sub-blocks)
    llvm::BasicBlock* rhs_end_bb = _builder->GetInsertBlock();
    _builder->CreateBr(merge_bb);

    // 5. Merge block with PHI
    func->insert(func->end(), merge_bb);
    _builder->SetInsertPoint(merge_bb);
    llvm::PHINode* phi = _builder->CreatePHI(_builder->getInt1Ty(), 2, "land");
    phi->addIncoming(_builder->getFalse(), entry_bb);  // left was false → result is false
    phi->addIncoming(right, rhs_end_bb);               // left was true → result is right

    _value = phi;
}


void implementation_generator::visit_logical_or_expression(logical_or_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    if (expr.is_constant()) {
        _value = _context->get_llvm_constant_from_constant_value(expr.get_constant_value(), expr.get_type());
        if (_value) return;
    }

    // ── Short-circuit evaluation (or-else) ─────────────────────────────────
    // Evaluate left first; if true, skip right entirely and yield true.

    // 1. Evaluate left operand
    _value = nullptr;
    expr.left()->accept(*this);
    llvm::Value* left = _value;
    if (!left) {
        _value = nullptr;
        return;
    }

    // If left operand is a reference, dereference it.
    if (type::is_reference(expr.left()->get_type()) || type::is_drain(expr.left()->get_type())) {
        llvm::Type* ty = _context->get_llvm_type(expr.left()->get_type());
        left = _builder->CreateLoad(ty, left);
    }

    // 2. Create basic blocks for short-circuit
    llvm::Function* func = _builder->GetInsertBlock()->getParent();
    llvm::BasicBlock* entry_bb = _builder->GetInsertBlock();
    llvm::BasicBlock* rhs_bb   = llvm::BasicBlock::Create(**_context, "lor-rhs", func);
    llvm::BasicBlock* merge_bb = llvm::BasicBlock::Create(**_context, "lor-merge");

    // 3. Branch: if left is true, skip to merge with true; otherwise evaluate right
    _builder->CreateCondBr(left, merge_bb, rhs_bb);

    // 4. Evaluate right operand (only reached if left was false)
    _builder->SetInsertPoint(rhs_bb);
    _value = nullptr;
    expr.right()->accept(*this);
    llvm::Value* right = _value;
    if (!right) {
        _value = nullptr;
        return;
    }
    // Capture the actual block after visiting right (it may have created sub-blocks)
    llvm::BasicBlock* rhs_end_bb = _builder->GetInsertBlock();
    _builder->CreateBr(merge_bb);

    // 5. Merge block with PHI
    func->insert(func->end(), merge_bb);
    _builder->SetInsertPoint(merge_bb);
    llvm::PHINode* phi = _builder->CreatePHI(_builder->getInt1Ty(), 2, "lor");
    phi->addIncoming(_builder->getTrue(), entry_bb);   // left was true → result is true
    phi->addIncoming(right, rhs_end_bb);               // left was false → result is right

    _value = phi;
}

//
// Logical not expression (!)
//

/**
 * Resolve a logical not expression (!expr): validate operand is boolean or convertible.
 *
 * Steps:
 *   1. Resolve the operand.
 *   2. For struct types: look for operator! overload.
 *   3. For non-bool types: insert implicit cast to bool.
 *   4. Set result type to bool.
 */
void type_reference_resolver::visit_logical_not_expression(logical_not_expression& expr) {
    // Step 1: Resolve the operand
    visit_unary_expression(expr);

    auto& sub = expr.sub_expr();
    auto type = sub->get_type();

    if(type::is_reference(type)) {
        // For ref<indirection>, don't unwrap — adapt_type handles it.
        auto inner = type->get_subtype();
        if (!type::is_pointer(inner) && !type::is_link(inner) &&
            !type::is_view(inner) && !type::is_owner(inner)) {
            type = type->get_subtype();
        }
    }

    // Step 2: For struct types: look for operator! overload
    // ── Operator overload for aggregate types ──
    {
        auto check_type = type::remove_const(type);
        bool is_const_operand = type::is_const(type);
        if (type::is_struct(check_type)) {
            auto st_type = std::dynamic_pointer_cast<struct_type>(check_type);
            if (st_type) {
                auto agg = st_type->get_struct();
                if (agg) {
                    auto op_func = resolve_unary_operator_overload(expr, agg, sub, is_const_operand);
                    if (op_func) {
                        expr.set_operator_func(op_func);
                        if (op_func->has_return_type()) {
                            expr.set_type(op_func->get_return_type());
                        } else {
                            expr.set_type(_context->from_type(primitive_type::BOOL));
                        }
                        auto orig_type = sub->get_type();
                        if (op_func->is_member()) {
                            auto di = compute_operator_dispatch_info(op_func, orig_type);
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
        }
    }

    // Check bool-compatibility: primitive, indirection, or null.
    auto is_bool_compatible = [](const std::shared_ptr<k::model::type>& t) {
        if (type::is_primitive(t)) return true;
        if (type::is_pointer(t) || type::is_link(t) || type::is_view(t)
            || type::is_owner(t) || type::is_null(t)) return true;
        // Also accept ref<indirection>
        if (type::is_reference(t)) {
            auto inner = t->get_subtype();
            if (type::is_pointer(inner) || type::is_link(inner) ||
                type::is_view(inner) || type::is_owner(inner)) return true;
        }
        return false;
    };
    if(!is_bool_compatible(type)) {
        throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_INVOKE_ASSIGN_RESULT), expr.first_lexeme(),
            "Logical NOT ('!') is not supported for non-primitive types: "
            "the operand has type '{}'; only primitive or indirection types convertible to boolean are supported",
            {type ? type->to_string() : "?"});
    }

    // Step 3: For non-bool types: insert implicit cast to bool
    static auto bool_type = _context->from_type(primitive_type::BOOL);
    auto cast = adapt_type(sub, bool_type);
    if(!cast) {
        throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_INVOKE_MEMBER_NO_MATCH), expr.first_lexeme(),
            "The operand of logical NOT ('!') cannot be implicitly converted to bool: "
            "the operand has type '{}'; logical NOT requires a boolean-compatible operand",
            {type ? type->to_string() : "?"});
    } else if(cast != sub ) {
        // Casted, assign casted expression instead of source.
        expr.assign(cast);
    } else {
        // Compatible type, no need to cast.
    }

    // Step 4: Set result type to bool
    // For primitive type, logical is always returning boolean
    expr.set_type(bool_type);

    if (!expr.has_operator_overload() && expr.sub_expr()->is_constant()) {
        auto res = constant_evaluator::eval_unary(
            unary_op::LOGICAL_NOT, expr.sub_expr()->get_constant_value(), expr.get_type());
        if (res) {
            expr.set_constant_value(*res);
        }
    }
}

void implementation_generator::visit_logical_not_expression(logical_not_expression& expr) {
    if (generate_unary_operator_overload(expr)) return;

    if (expr.is_constant()) {
        _value = _context->get_llvm_constant_from_constant_value(expr.get_constant_value(), expr.get_type());
        if (_value) return;
    }

    auto value = process_unary_expression(expr);

    if(!value) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    auto& sub = expr.sub_expr();
    auto type = sub->get_type();

    if(type::is_reference(type)) {
        // Dereference
        type = type->get_subtype();
        value = _builder->CreateLoad(_context->get_llvm_type(type), value);
    }

    if(!type::is_primitive(type)) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F00B), expr.first_lexeme(),
            "Internal error: '!' operator has a non-primitive operand during code generation; "
            "this should have been rejected during type resolution");
    }

    _value = _builder->CreateNot(value);
}

void symbol_resolver::visit_conditional_expression(conditional_expression& expr)
{
    auto& cond = expr.lexpr();
    auto& then_expr = expr.mexpr();
    auto& else_expr = expr.rexpr();

    if (!cond || !then_expr || !else_expr) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F006), expr.first_lexeme(),
            "Internal error: ternary expression has a null condition, then-expression, or else-expression; "
            "this indicates a malformed AST or a compiler bug");
    }

    cond->accept(*this);
    then_expr->accept(*this);
    else_expr->accept(*this);
}

void type_reference_resolver::visit_conditional_expression(conditional_expression& expr)
{
    auto& cond = expr.lexpr();
    auto& then_expr = expr.mexpr();
    auto& else_expr = expr.rexpr();

    if (!cond || !then_expr || !else_expr) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F006), expr.first_lexeme(),
            "Internal error: ternary expression has a null condition, then-expression, or else-expression; "
            "this indicates a malformed AST or a compiler bug");
    }

    _replacement_expr = nullptr;
    cond->accept(*this);
    if (_replacement_expr) {
        expr.lexpr() = _replacement_expr;
        _replacement_expr = nullptr;
    }

    auto expected = current_expected_type();

    _replacement_expr = nullptr;
    {
        target_scope scope(*this, expected, target_context_kind::TERNARY_BRANCH);
        then_expr->accept(*this);
    }
    if (_replacement_expr) {
        expr.mexpr() = _replacement_expr;
        _replacement_expr = nullptr;
    }

    // If no expected type was provided from outer context, but then_expr resolved to a concrete type,
    // use it as candidate expected type for else_expr.
    auto else_expected = expected;
    if (!else_expected && expr.mexpr()->get_type() && type::is_resolved(expr.mexpr()->get_type())) {
        else_expected = expr.mexpr()->get_type();
    }

    _replacement_expr = nullptr;
    {
        target_scope scope(*this, else_expected, target_context_kind::TERNARY_BRANCH);
        else_expr->accept(*this);
    }
    if (_replacement_expr) {
        expr.rexpr() = _replacement_expr;
        _replacement_expr = nullptr;
    }

    auto cond_type = expr.lexpr()->get_type();
    if (!type::is_resolved(cond_type)) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F005), expr.first_lexeme(),
            "Internal error: the condition of a ternary expression could not be type-resolved; "
            "the condition must be known before the ternary expression can be typed");
    }

    auto bool_type = _context->from_type(primitive_type::BOOL);
    auto cast_cond = adapt_type(expr.lexpr(), bool_type);
    if (!cast_cond) {
        throw_error(static_cast<unsigned int>(k::diag::statement_diag::ERR_IF_COND_NOT_BOOL), expr.first_lexeme(),
            "The condition of a ternary expression must be convertible to bool");
    } else if (cast_cond != expr.lexpr()) {
        expr.lexpr() = cast_cond;
    }

    auto then_type = expr.mexpr()->get_type();
    auto else_type = expr.rexpr()->get_type();
    if (!type::is_resolved(then_type) || !type::is_resolved(else_type)) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F008), expr.first_lexeme(),
            "Internal error: one branch of a ternary expression could not be type-resolved; "
            "both branches must be known before the ternary expression can be typed");
    }

    struct candidate {
        std::shared_ptr<type> target;
        std::shared_ptr<expression> then_expr;
        std::shared_ptr<expression> else_expr;
        unsigned int max_weight = 0;
        unsigned int sum_weight = 0;
        unsigned int exact_count = 0;
    };

    auto build_candidate = [&](const std::shared_ptr<type>& target) -> std::optional<candidate> {
        if (!target) return std::nullopt;
        auto then_weight = compute_cast_weight(expr.mexpr(), target);
        auto else_weight = compute_cast_weight(expr.rexpr(), target);
        if (then_weight == CAST_IMPOSSIBLE || else_weight == CAST_IMPOSSIBLE) {
            return std::nullopt;
        }
        auto then_cast = adapt_type(expr.mexpr(), target);
        auto else_cast = adapt_type(expr.rexpr(), target);
        if (!then_cast || !else_cast) {
            return std::nullopt;
        }
        candidate c;
        c.target = target;
        c.then_expr = then_cast;
        c.else_expr = else_cast;
        c.max_weight = std::max(static_cast<unsigned int>(then_weight), static_cast<unsigned int>(else_weight));
        c.sum_weight = static_cast<unsigned int>(then_weight) + static_cast<unsigned int>(else_weight);
        c.exact_count = (then_weight == CAST_NONE ? 1U : 0U) + (else_weight == CAST_NONE ? 1U : 0U);
        return c;
    };

    auto best = build_candidate(then_type);
    auto other = build_candidate(else_type);
    auto best_score = [&](const candidate& c) {
        return std::tuple<unsigned int, unsigned int, unsigned int>{c.max_weight, c.sum_weight, 2U - c.exact_count};
    };

    if (other) {
        if (!best || best_score(*other) < best_score(*best)) {
            best = std::move(other);
        }
    }

    if (!best) {
        throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_INVOKE_METHOD_ARG_MISMATCH), expr.first_lexeme(),
            "The two branches of a ternary expression cannot be implicitly converted to a common type: "
            "found '{}' and '{}'",
            {then_type ? then_type->to_string() : "?", else_type ? else_type->to_string() : "?"});
    }

    if (best->then_expr != expr.mexpr()) {
        expr.mexpr() = best->then_expr;
    }
    if (best->else_expr != expr.rexpr()) {
        expr.rexpr() = best->else_expr;
    }
    expr.set_type(best->target);

    if (expr.lexpr()->is_constant()) {
        bool cond_val = expr.lexpr()->get_constant_value().get_bool();
        if (cond_val && expr.mexpr()->is_constant()) {
            auto cast_val = constant_evaluator::cast_to_type(expr.mexpr()->get_constant_value(), expr.get_type());
            if (cast_val) expr.set_constant_value(*cast_val);
            else expr.set_constant_value(expr.mexpr()->get_constant_value());
        } else if (!cond_val && expr.rexpr()->is_constant()) {
            auto cast_val = constant_evaluator::cast_to_type(expr.rexpr()->get_constant_value(), expr.get_type());
            if (cast_val) expr.set_constant_value(*cast_val);
            else expr.set_constant_value(expr.rexpr()->get_constant_value());
        }
    }
}

void implementation_generator::visit_conditional_expression(conditional_expression& expr)
{
    set_debug_location(expr.first_lexeme());

    if (expr.is_constant()) {
        auto t = type::remove_const(expr.get_type());
        if (t && (type::is_primitive(t) || type::is_enum(t) || type::is_null(t))) {
            _value = _context->get_llvm_constant_from_constant_value(expr.get_constant_value(), expr.get_type());
            if (_value) return;
        }
    }

    auto cond_expr = expr.lexpr();
    auto then_expr = expr.mexpr();
    auto else_expr = expr.rexpr();
    if (!cond_expr || !then_expr || !else_expr) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F006), expr.first_lexeme(),
            "Internal error: ternary expression has a null condition, then-expression, or else-expression; "
            "this indicates a malformed AST or a compiler bug");
    }

    llvm::Function* func = _builder->GetInsertBlock()->getParent();
    llvm::BasicBlock* then_bb = llvm::BasicBlock::Create(**_context, "ternary-then", func);
    llvm::BasicBlock* else_bb = llvm::BasicBlock::Create(**_context, "ternary-else", func);
    llvm::BasicBlock* merge_bb = llvm::BasicBlock::Create(**_context, "ternary-merge");
    auto result_type = expr.get_type();
    if (!result_type) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F006), expr.first_lexeme(),
            "Internal error: ternary expression has no resolved result type; "
            "this indicates a compiler bug");
    }
    llvm::Type* result_llvm_type = _context->get_llvm_type(result_type);
    if (!result_llvm_type) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F006), expr.first_lexeme(),
            "Internal error: ternary expression result type could not be converted to LLVM IR; "
            "this indicates a compiler bug");
    }

    auto saved_sret_destination = _sret_destination;
    _sret_destination = nullptr;

    auto cleanup_temporaries_since = [&](size_t start_index, const lex::opt_any_lexeme& anchor) {
        if (_expression_temporaries.size() <= start_index) return;
        auto previous_debug_loc = _builder->getCurrentDebugLocation();
        if (anchor) {
            set_debug_location(anchor);
        }
        for (size_t i = _expression_temporaries.size(); i > start_index; --i) {
            auto& tmp = _expression_temporaries[i - 1];
            if (tmp.array_type) {
                emit_sized_array_elements_cleanup(_builder.get(), get_module(), _context->_functions,
                    tmp.alloca, tmp.array_type);
            } else if (tmp.destructor) {
                _builder->CreateCall(tmp.destructor, {tmp.alloca});
            }
        }
        _expression_temporaries.resize(start_index);
        _builder->SetCurrentDebugLocation(previous_debug_loc);
    };

    // Evaluate the condition first; temporary cleanup must happen before branching.
    size_t cond_temp_start = _expression_temporaries.size();
    _value = nullptr;
    cond_expr->accept(*this);
    llvm::Value* cond_value = _value;
    if (!cond_value) {
        _sret_destination = saved_sret_destination;
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F006), expr.first_lexeme(),
            "Internal error: the condition of a ternary expression produced no LLVM value during code generation; "
            "this indicates a compiler bug");
    }
    if (cond_expr->get_type() && (type::is_reference(cond_expr->get_type()) || type::is_drain(cond_expr->get_type()))) {
        cond_value = _builder->CreateLoad(_context->get_llvm_type(cond_expr->get_type()), cond_value, "ternary_cond_load");
    }
    cleanup_temporaries_since(cond_temp_start, expr.first_lexeme());

    auto ret_type_nc = type::remove_const(result_type);
    const bool result_is_reference_like = type::is_reference(result_type) || type::is_drain(result_type);
    bool use_sret = needs_sret_return(result_type);
    
    llvm::AllocaInst* result_alloca = nullptr;
    std::vector<llvm::Value*> phi_values; // For non-sret types
    std::vector<llvm::BasicBlock*> phi_blocks;

    if (use_sret) {
        // For sret aggregates: use caller-provided _sret_destination if available,
        // otherwise allocate a dedicated destination for the ternary result
        if (_sret_destination) {
            // Caller provided a destination (e.g., from variable_statement or return statement)
            result_alloca = llvm::dyn_cast<llvm::AllocaInst>(_sret_destination);
            if (!result_alloca) {
                throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F006), expr.first_lexeme(),
                    "Internal error: _sret_destination is not an AllocaInst; "
                    "this indicates a compiler bug");
            }
            // Don't register for cleanup - the caller owns it
        } else {
            // No caller-provided destination: allocate our own
            llvm::IRBuilder<> entry_builder(&func->getEntryBlock(), func->getEntryBlock().begin());
            result_alloca = entry_builder.CreateAlloca(result_llvm_type, nullptr, "ternary_result");
            
            // Track for cleanup (we own this temporary)
            auto ret_st = std::dynamic_pointer_cast<struct_type>(ret_type_nc);
            if (ret_st && ret_st->get_struct()) {
                auto dtor = ret_st->get_struct()->get_destructor();
                if (dtor) {
                    auto dtor_fn = dtor->shared_as<k::model::function>();
                    auto dtor_it = _context->_functions.find(dtor_fn);
                    if (dtor_it != _context->_functions.end()) {
                        _expression_temporaries.push_back({result_alloca, dtor_it->second, nullptr});
                    }
                }
            } else if (auto arr_type = std::dynamic_pointer_cast<sized_array_type>(ret_type_nc)) {
                _expression_temporaries.push_back({result_alloca, nullptr, arr_type});
            }
        }
    }

    _builder->CreateCondBr(cond_value, then_bb, else_bb);

    auto eval_branch = [&](const std::shared_ptr<expression>& branch_expr, llvm::BasicBlock* branch_bb) {
        _builder->SetInsertPoint(branch_bb);
        auto branch_temp_start = _expression_temporaries.size();
        auto saved_branch_sret = _sret_destination;
        // For sret types, set _sret_destination to our ternary result_alloca
        // so that both branches write their results to the same place
        if (use_sret) {
            _sret_destination = result_alloca;
        }
        // For non-sret types, leave _sret_destination as-is (don't override external settings)
        _value = nullptr;
        branch_expr->accept(*this);
        llvm::Value* branch_value = _value;
        // Restore the original _sret_destination (don't let branch modifications leak out)
        _sret_destination = saved_branch_sret;
        if (!branch_value) {
            throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F006), expr.first_lexeme(),
                "Internal error: a ternary branch produced no LLVM value during code generation; "
                "this indicates a compiler bug");
        }
        
        // For value-result ternaries, load reference/drain branches to values.
        // For reference-result ternaries, keep branch pointers intact.
        if (!result_is_reference_like
            && branch_expr->get_type()
            && (type::is_reference(branch_expr->get_type()) || type::is_drain(branch_expr->get_type()))) {
            branch_value = _builder->CreateLoad(_context->get_llvm_type(branch_expr->get_type()), branch_value, "ternary_branch_load");
        }
        
        // For sret types: branch_value IS result_alloca (written by sret mechanism via temporary_construction)
        // For non-sret types: collect branch_value for PHI node
        if (!use_sret) {
            phi_values.push_back(branch_value);
            phi_blocks.push_back(_builder->GetInsertBlock());
        }
        
        cleanup_temporaries_since(branch_temp_start, expr.first_lexeme());
        if (!_builder->GetInsertBlock()->getTerminator()) {
            _builder->CreateBr(merge_bb);
        }
    };

    eval_branch(then_expr, then_bb);
    eval_branch(else_expr, else_bb);

    func->insert(func->end(), merge_bb);
    _builder->SetInsertPoint(merge_bb);
    _sret_destination = saved_sret_destination;
    
    if (use_sret) {
        // For sret: result is in result_alloca (written by both branches)
        // Return it as a pointer so the caller can handle it (or store it if needed)
        _value = result_alloca;
    } else {
        // For non-sret: use PHI node to merge the two branch values
        if (phi_values.size() == 2 && phi_blocks.size() == 2) {
            auto* phi = _builder->CreatePHI(result_llvm_type, 2, "ternary_result");
            phi->addIncoming(phi_values[0], phi_blocks[0]);
            phi->addIncoming(phi_values[1], phi_blocks[1]);
            _value = phi;
        } else {
            throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F006), expr.first_lexeme(),
                "Internal error: ternary expression phi node setup failed; "
                "this indicates a compiler bug");
        }
    }
}

//
// Comparison expressions
//
/**
 * Resolve a comparison expression (==, !=, <, >, <=, >=): validate operand types,
 * check for operator overloads on struct types.
 *
 * Steps:
 *   1. Resolve both operands.
 *   2. If either operand is a struct type: look for operator overload.
 *   3. For pointers: validate pointed-type compatibility.
 *   4. For primitives: adapt types for comparison.
 *   5. Set result type to bool.
 */
void type_reference_resolver::visit_comparison_expression(comparison_expression& expr) {
    // Step 1: Resolve both operands
    visit_binary_expression(expr);

    auto& left = expr.left();
    auto& right = expr.right();

    auto left_type = left->get_type();
    auto right_type = right->get_type();

    // ── Helper: is this type address-comparable? ─────────────────────────────
    // Pointer, link, pinned, owner, and the null literal type can all participate
    // in address equality/inequality comparisons.
    auto is_address_comparable = [](const std::shared_ptr<type>& t) -> bool {
        return type::is_pointer(t) || type::is_link(t) || type::is_view(t)
            || type::is_owner(t)   || type::is_null(t)
            || type::is_fat_callable(t);
    };

    // Step 2: If either operand is a struct type: look for operator overload
    // Strip one level of reference to get the underlying type.
    // For ref<ptr<T>>, ref<link<T>>, ref<pin<T>>, ref<owner<T>>:
    //   load the stored pointer/link/pin/owner so we can compare addresses.
    auto unwrap_ref_indirection = [&](std::shared_ptr<expression>& operand,
                                      std::shared_ptr<type>& operand_type) {
        if (type::is_reference(operand_type)) {
            auto inner = std::dynamic_pointer_cast<reference_type>(operand_type)->get_subtype();
            if (is_address_comparable(inner)) {
                operand = adapt_reference_load_value(operand);
                operand_type = inner;
            }
        }
    };

    // Step 3: For pointers: validate pointed-type compatibility
    unwrap_ref_indirection(left, left_type);
    unwrap_ref_indirection(right, right_type);

    // ── Address comparison path ──────────────────────────────────────────────
    if (is_address_comparable(left_type) || is_address_comparable(right_type)) {
        // Both sides must be address-comparable.
        if (!is_address_comparable(left_type) || !is_address_comparable(right_type)) {
            throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_INVOKE_CTOR_RESULT), expr.first_lexeme(),
                "Address comparison requires both operands to be indirections "
                "(pointer, link, pinned, owner) or null, but found '{}' and '{}'",
                {left_type ? left_type->to_string() : "?",
                 right_type ? right_type->to_string() : "?"});
        }
        // Only == and != are valid for address comparison (not <, >, <=, >=).
        if (!dynamic_cast<equal_expression*>(&expr) &&
            !dynamic_cast<different_expression*>(&expr)) {
            if (type::is_fat_callable(left_type) || type::is_fat_callable(right_type)) {
                throw_error(static_cast<unsigned int>(k::diag::callable_diag::ERR_CALLABLE_OP_FORBIDDEN), expr.first_lexeme(),
                    "Relational operators (<, >, <=, >=) are not defined on callables; "
                    "only '==' and '!=' (against another callable or null) are allowed for '{}' and '{}'",
                    {left_type ? left_type->to_string() : "?",
                     right_type ? right_type->to_string() : "?"});
            }
            throw_error(static_cast<unsigned int>(k::diag::function_diag::ERR_FUNC_ACCESS_DENIED), expr.first_lexeme(),
                "Only '==' and '!=' are valid for address comparison between indirections; "
                "relational operators (<, >, <=, >=) are not supported for types '{}' and '{}'",
                {left_type ? left_type->to_string() : "?",
                 right_type ? right_type->to_string() : "?"});
        }
        // Update the expression operands if unwrapped
        expr.assign_left(left);
        expr.assign_right(right);

        static auto bool_type = _context->from_type(primitive_type::BOOL);
        expr.set_type(bool_type);
        return;
    }
    // ─────────────────────────────────────────────────────────────────────────

    // ── Operator overload for aggregate types (before reference stripping) ──
    // Tries the exact comparison operator first, then — if not declared — progressively
    // more complex fallback syntheses from other declared comparison operators.
    // See k::model::cmp_synthesis and resolve_comparison_with_fallback() for the full rule.
    {
        auto check_left = left_type;
        if (type::is_reference(check_left)) {
            check_left = check_left->get_subtype();
        }
        bool is_const_left = type::is_const(check_left);
        check_left = type::remove_const(check_left);

        auto check_right = right_type;
        if (type::is_reference(check_right)) {
            check_right = check_right->get_subtype();
        }
        bool is_const_right = type::is_const(check_right);
        check_right = type::remove_const(check_right);

        if (type::is_struct(check_left)) {
            auto st_type = std::dynamic_pointer_cast<struct_type>(check_left);
            if (st_type) {
                auto agg = st_type->get_struct();
                if (agg) {
                    std::shared_ptr<aggregate> right_agg;
                    if (type::is_struct(check_right)) {
                        if (auto right_st_type = std::dynamic_pointer_cast<struct_type>(check_right)) {
                            right_agg = right_st_type->get_struct();
                        }
                    }

                    auto result = resolve_comparison_with_fallback(
                        expr, agg, right_agg, left, right, is_const_left, is_const_right);
                    if (result) {
                        auto op_func = result->func;
                        expr.set_operator_func(op_func);
                        expr.set_cmp_synthesis(result->synthesis);
                        expr.set_composite_negate_terms(result->composite_negate_terms);

                        // Apply argument adaptation (implicit cast), if any: for DIRECT/NEGATE
                        // the adapted operand replaces the right operand (arg role); for
                        // SWAP/SWAP_NEGATE it replaces the left operand (arg role there).
                        // COMPOSITE_* never adapts (requires an exact type match, see
                        // cmp_synthesis docs), so result->adapted_arg is always null there.
                        bool receiver_is_right = (result->synthesis == cmp_synthesis::SWAP
                                                || result->synthesis == cmp_synthesis::SWAP_NEGATE
                                                || result->synthesis == cmp_synthesis::SPACESHIP_SWAP);
                        if (result->adapted_arg) {
                            if (receiver_is_right) {
                                if (result->adapted_arg != left) expr.assign_left(result->adapted_arg);
                            } else {
                                if (result->adapted_arg != right) expr.assign_right(result->adapted_arg);
                            }
                        }

                        if (op_func->has_return_type() && result->synthesis == cmp_synthesis::DIRECT) {
                            expr.set_type(op_func->get_return_type());
                        } else {
                            static auto bool_type_cached = _context->from_type(primitive_type::BOOL);
                            expr.set_type(bool_type_cached);
                        }

                        // Primary call's dispatch info: receiver = left, unless SWAP/SWAP_NEGATE
                        // (receiver = right).
                        auto primary_receiver_type = receiver_is_right ? right_type : left_type;
                        if (op_func->is_member()) {
                            auto di = compute_operator_dispatch_info(op_func, primary_receiver_type);
                            expr.set_operator_dispatch_info(std::move(di));
                        } else {
                            virtual_dispatch_info di;
                            di.kind = virtual_dispatch_info::dispatch_kind::DIRECT;
                            expr.set_operator_dispatch_info(std::move(di));
                        }

                        // Composite kinds need a second dispatch info for the swapped-receiver
                        // (right operand) call.
                        if (result->synthesis == cmp_synthesis::COMPOSITE_AND
                            || result->synthesis == cmp_synthesis::COMPOSITE_OR) {
                            if (op_func->is_member()) {
                                auto di2 = compute_operator_dispatch_info(op_func, right_type);
                                expr.set_composite_dispatch_info(std::move(di2));
                            } else {
                                virtual_dispatch_info di2;
                                di2.kind = virtual_dispatch_info::dispatch_kind::DIRECT;
                                expr.set_composite_dispatch_info(std::move(di2));
                            }
                        }

                        // Phase 2: if the spaceship source's return type is an aggregate, the
                        // resolved "aggregate-result vs 0" comparison also needs its own
                        // dispatch info, since it is called on the (aggregate) spaceship
                        // result — a receiver distinct from either original operand.
                        if (result->spaceship_zero_func) {
                            expr.set_spaceship_zero_func(result->spaceship_zero_func);
                            expr.set_spaceship_zero_arg_type(result->spaceship_zero_arg_type);
                            if (result->spaceship_zero_func->is_member()) {
                                auto zdi = compute_operator_dispatch_info(
                                    result->spaceship_zero_func, op_func->get_return_type());
                                expr.set_spaceship_zero_dispatch_info(std::move(zdi));
                            } else {
                                virtual_dispatch_info zdi;
                                zdi.kind = virtual_dispatch_info::dispatch_kind::DIRECT;
                                expr.set_spaceship_zero_dispatch_info(std::move(zdi));
                            }
                        }
                        return;
                    }
                }
            }
        }
    }

    // ── Primitive comparison (existing path) ─────────────────────────────────
    if(type::is_reference(left_type)) {
        left = adapt_reference_load_value(left);
        expr.assign_left(left);
        left_type = left_type->get_subtype();
    } else if(type::is_drain(left_type)) {
        left = adapt_reference_load_value(left);
        expr.assign_left(left);
        left_type = left_type->get_subtype();
    }
    left_type = type::remove_const(left_type);

    if(type::is_reference(right_type)) {
        right = adapt_reference_load_value(right);
        expr.assign_right(right);
        right_type = right_type->get_subtype();
    } else if(type::is_drain(right_type)) {
        right = adapt_reference_load_value(right);
        expr.assign_right(right);
        right_type = right_type->get_subtype();
    }
    right_type = type::remove_const(right_type);

    // ── Enum comparison: convert enum operands to their underlying primitive type ──
    {
        auto left_enum = std::dynamic_pointer_cast<enum_type>(left_type);
        auto right_enum = std::dynamic_pointer_cast<enum_type>(right_type);
        if (left_enum || right_enum) {
            // Determine the common underlying primitive type for comparison
            auto left_underlying = left_enum ? left_enum->get_underlying_type()
                                             : std::dynamic_pointer_cast<primitive_type>(left_type);
            auto right_underlying = right_enum ? right_enum->get_underlying_type()
                                               : std::dynamic_pointer_cast<primitive_type>(right_type);
            if (!left_underlying || !right_underlying) {
                throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_OVERLOAD_VISIBILITY_DENIED), expr.first_lexeme(),
                    "Cannot compare enum with non-primitive type: "
                    "operands have types '{}' and '{}'",
                    {left_type ? left_type->to_string() : "?",
                     right_type ? right_type->to_string() : "?"});
            }
            // Adapt both operands to a common type (use right's underlying if both are enum, left otherwise)
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
        throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_INVOKE_CTOR_RESULT), expr.first_lexeme(),
            "Comparison operators are not supported for non-primitive types: "
            "operands must be primitive types, but found '{}' and '{}'",
            {left_type ? left_type->to_string() : "?",
             right_type ? right_type->to_string() : "?"});
    }

    auto left_prim_type = std::dynamic_pointer_cast<primitive_type>(type::remove_const(left_type));
    auto right_prim_type = std::dynamic_pointer_cast<primitive_type>(type::remove_const(right_type));

    auto adapted_left = left;
    auto adapted_right = right;

    if(left_prim_type->is_boolean() && !right_prim_type->is_boolean()) {
        // Adapt right to boolean
        adapted_right = adapt_type(right, left_prim_type);
    } else if(!left_prim_type->is_boolean() && right_prim_type->is_boolean()) {
        // Adapt left to boolean
        adapted_left = adapt_type(left, right_prim_type);
    }  else {
        // Adapt right to left type
        // TODO rework to promote to biggest integer of both
        adapted_right = adapt_type(right, left_prim_type);
    }

    // Step 4: For primitives: adapt types for comparison
    if(!adapted_left || !adapted_right) {
        throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_INVOKE_METHOD_ARG_MISMATCH), expr.first_lexeme(),
            "Incompatible types in comparison: "
            "cannot align operand types '{}' and '{}' for comparison; "
            "use an explicit cast to make the types comparable",
            {left_type ? left_type->to_string() : "?",
             right_type ? right_type->to_string() : "?"});
    }

    if(adapted_left!=left) {
        expr.assign_left(adapted_left);
    }
    if(adapted_right!=right) {
        expr.assign_right(adapted_right);
    }

    // Step 5: Set result type to bool
    // For primitive type, logical is always returning boolean
    static auto bool_type = _context->from_type(primitive_type::BOOL);
    expr.set_type(bool_type);

    if (!expr.has_operator_overload() && expr.left()->is_constant() && expr.right()->is_constant()) {
        comparison_op op = comparison_op::EQUAL;
        if (dynamic_cast<equal_expression*>(&expr)) op = comparison_op::EQUAL;
        else if (dynamic_cast<different_expression*>(&expr)) op = comparison_op::NOT_EQUAL;
        else if (dynamic_cast<lesser_expression*>(&expr)) op = comparison_op::LESS;
        else if (dynamic_cast<lesser_equal_expression*>(&expr)) op = comparison_op::LESS_EQUAL;
        else if (dynamic_cast<greater_expression*>(&expr)) op = comparison_op::GREATER;
        else if (dynamic_cast<greater_equal_expression*>(&expr)) op = comparison_op::GREATER_EQUAL;

        auto res = constant_evaluator::eval_comparison(
            op, expr.left()->get_constant_value(), expr.right()->get_constant_value());
        if (res) {
            expr.set_constant_value(*res);
        }
    }
}

//
// Comparison operator overload / fallback synthesis codegen
//

/**
 * Evaluate a single call to a resolved comparison "source" operator function, given
 * already-evaluated LLVM values for its receiver ('this', or first non-member arg) and
 * argument (or second non-member arg). See generators.hpp for the full contract.
 *
 * Historically comparison source operators always returned bool (never sret). Phase 2
 * (aggregate `operator <=>` return type) needs this helper to also support calling the
 * spaceship operator itself when it returns an aggregate (sret ABI) — see
 * generate_binary_operator_overload() in gen_operators_overload.cpp for the reference sret
 * pattern this mirrors (entry-block alloca, prepended sret arg, destructor-cleanup tracking).
 * When sret is used, the returned llvm::Value* is the sret destination pointer (not a call
 * result), exactly like every other sret-returning expression in this codebase.
 */
llvm::Value* implementation_generator::call_comparison_source_operator(
    const std::shared_ptr<function>& op_func,
    const virtual_dispatch_info& dispatch_info,
    llvm::Value* receiver_or_first_val,
    llvm::Value* arg_or_second_val)
{
    auto it = _context->_functions.find(op_func);
    llvm::Function* llvm_func = (it != _context->_functions.end()) ? it->second : nullptr;
    if (!llvm_func && !(op_func->is_virtual() && (op_func->is_abstract_func() || op_func->is_external()))) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F055), std::nullopt,
            "Internal error: comparison source operator '{}' has no LLVM definition",
            {op_func->get_short_name()});
    }

    std::vector<llvm::Value*> args = { receiver_or_first_val, arg_or_second_val };

    bool op_needs_sret = op_func->has_return_type() && needs_sret_return(op_func->get_return_type());

    auto build_fn_type = [&]() -> llvm::FunctionType* {
        if (llvm_func) return llvm_func->getFunctionType();
        std::vector<llvm::Type*> param_types;
        if (op_needs_sret)
            param_types.push_back(llvm::PointerType::get(**_context, 0));
        if (op_func->is_member() && !op_func->is_static() && op_func->get_this_parameter())
            param_types.push_back(_context->get_llvm_type(op_func->get_this_parameter()->get_type()));
        for (const auto& param : op_func->parameters())
            param_types.push_back(_context->get_llvm_type(param->get_type()));
        llvm::Type* ret_type = op_needs_sret
            ? llvm::Type::getVoidTy(**_context)
            : _context->get_llvm_type(op_func->get_return_type());
        return llvm::FunctionType::get(ret_type, param_types, false);
    };

    // Allocate an entry-block sret temporary, prepend it to `args`, and register it for
    // destructor cleanup at end-of-statement (see emit_expression_temporaries_cleanup()).
    // Returns the alloca pointer, which becomes this call's "result" value.
    auto prepare_sret = [&]() -> llvm::AllocaInst* {
        auto ret_type_nc = type::remove_const(op_func->get_return_type());
        llvm::Type* llvm_ret = _context->get_llvm_type(ret_type_nc);
        llvm::Function* cur_fn = _builder->GetInsertBlock()->getParent();
        llvm::IRBuilder<> entry_builder(&cur_fn->getEntryBlock(), cur_fn->getEntryBlock().begin());
        llvm::AllocaInst* sret_dest = entry_builder.CreateAlloca(llvm_ret, nullptr, "cmp_ss_sret_tmp");
        args.insert(args.begin(), sret_dest);
        auto ret_st = std::dynamic_pointer_cast<struct_type>(ret_type_nc);
        if (ret_st && ret_st->get_struct()) {
            auto dtor = ret_st->get_struct()->get_destructor();
            if (dtor) {
                auto dtor_fn = dtor->shared_as<k::model::function>();
                auto dtor_it = _context->_functions.find(dtor_fn);
                if (dtor_it != _context->_functions.end())
                    _expression_temporaries.push_back({sret_dest, dtor_it->second, nullptr});
            }
        }
        return sret_dest;
    };

    if (dispatch_info.kind == virtual_dispatch_info::dispatch_kind::VTABLE) {
        llvm::FunctionType* fn_type = build_fn_type();
        if (dispatch_info.dispatch_class) {
            if (op_needs_sret) {
                auto* sret_dest = prepare_sret();
                emit_virtual_dispatch_call(*_builder, *dispatch_info.dispatch_class, args[1],
                    dispatch_info.slot_index, fn_type, args, _context, "cmp_vcall",
                    make_virtual_call_emitter());
                return sret_dest;
            }
            auto* result = emit_virtual_dispatch_call(*_builder, *dispatch_info.dispatch_class, args[0],
                dispatch_info.slot_index, fn_type, args, _context, "cmp_vcall",
                    make_virtual_call_emitter());
            if (result) return result;
            // Fallback: emit_virtual_dispatch_call returned nullptr (vtable not ready?) — fall
            // through to the direct-call path below.
        } else if (dispatch_info.imported_dispatch_agg) {
            auto imp_agg = dispatch_info.imported_dispatch_agg;
            auto* struct_llvm_type = imp_agg->get_struct_type() ? imp_agg->get_struct_type()->get_llvm_type() : nullptr;
            if (struct_llvm_type) {
                llvm::LLVMContext& llvm_ctx = **_context;
                llvm::Type* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);
                llvm::Value* this_val = args[0]; // capture before prepare_sret() shifts indices
                llvm::AllocaInst* sret_dest = op_needs_sret ? prepare_sret() : nullptr;
                llvm::Value* vptr_addr = _builder->CreateStructGEP(struct_llvm_type, this_val, 0, "cmp_imp_vptr_addr");
                llvm::Value* vptr = _builder->CreateLoad(ptr_ty, vptr_addr, "cmp_imp_vptr");
                const uint64_t ptr_size = 8;
                llvm::Value* slot_offset = llvm::ConstantInt::get(
                    llvm::Type::getInt64Ty(llvm_ctx), (dispatch_info.slot_index + 1) * ptr_size);
                llvm::Value* fn_ptr_addr = _builder->CreateInBoundsGEP(
                    llvm::Type::getInt8Ty(llvm_ctx), vptr, slot_offset, "cmp_imp_vtbl_slot");
                llvm::Value* fn_ptr = _builder->CreateLoad(ptr_ty, fn_ptr_addr, "cmp_imp_fn_ptr");
                auto* call = _builder->CreateCall(fn_type, fn_ptr, args, "cmp_imp_vcall");
                return sret_dest ? static_cast<llvm::Value*>(sret_dest) : static_cast<llvm::Value*>(call);
            }
        }
    }

    if (!llvm_func) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F056), std::nullopt,
            "Internal error: comparison source operator '{}' has no LLVM definition and is not dispatched virtually",
            {op_func->get_short_name()});
    }
    if (op_needs_sret) {
        auto* sret_dest = prepare_sret();
        _builder->CreateCall(llvm_func, args, "");
        return sret_dest;
    }
    return _builder->CreateCall(llvm_func, args, "cmp_call");
}

/**
 * Phase 2 (aggregate `operator <=>` return type): see declaration in generators.hpp.
 */
llvm::Value* implementation_generator::generate_spaceship_zero_comparison(comparison_expression& expr, llvm::Value* ss_result) {
    auto zero_func = expr.get_spaceship_zero_func();
    auto arg_type = expr.get_spaceship_zero_arg_type();
    llvm::Type* llvm_arg_type = _context->get_llvm_type(arg_type);

    auto prim_arg = std::dynamic_pointer_cast<primitive_type>(type::remove_const(arg_type));
    llvm::Value* zero_const = (prim_arg && prim_arg->is_float())
        ? static_cast<llvm::Value*>(llvm::ConstantFP::get(llvm_arg_type, 0.0))
        : static_cast<llvm::Value*>(llvm::ConstantInt::get(llvm_arg_type, 0));

    return call_comparison_source_operator(
        zero_func, expr.get_spaceship_zero_dispatch_info(), ss_result, zero_const);
}

/**
 * Generate code for a comparison expression whose result comes from an aggregate operator
 * overload, handling both the exact ("DIRECT") operator and all fallback synthesis kinds.
 * See k::model::cmp_synthesis for the semantics of each kind.
 */
bool implementation_generator::generate_comparison_operator(comparison_expression& expr) {
    if (!expr.has_operator_overload()) return false;

    auto op_func = expr.get_operator_func();
    auto kind = expr.get_cmp_synthesis();

    // Evaluate both operands exactly once; every synthesis kind below reuses these two
    // LLVM values (in different receiver/argument roles), never re-evaluating the operand
    // expression trees, so operand side effects only ever happen once.
    expr.left()->accept(*this);
    llvm::Value* left_val = _value;
    if (!left_val) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F055), expr.first_lexeme(),
            "Internal error: left operand for comparison operator overload produced no LLVM value");
    }
    expr.right()->accept(*this);
    llvm::Value* right_val = _value;
    if (!right_val) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F056), expr.first_lexeme(),
            "Internal error: right operand for comparison operator overload produced no LLVM value");
    }

    llvm::Value* result = nullptr;
    switch (kind) {
        case cmp_synthesis::DIRECT:
            // src(left, right)
            result = call_comparison_source_operator(op_func, expr.get_operator_dispatch_info(), left_val, right_val);
            break;
        case cmp_synthesis::NEGATE:
            // !src(left, right)
            result = call_comparison_source_operator(op_func, expr.get_operator_dispatch_info(), left_val, right_val);
            result = _builder->CreateNot(result, "cmp_negate");
            break;
        case cmp_synthesis::SWAP:
            // src(right, left)
            result = call_comparison_source_operator(op_func, expr.get_operator_dispatch_info(), right_val, left_val);
            break;
        case cmp_synthesis::SWAP_NEGATE:
            // !src(right, left)
            result = call_comparison_source_operator(op_func, expr.get_operator_dispatch_info(), right_val, left_val);
            result = _builder->CreateNot(result, "cmp_negate");
            break;
        case cmp_synthesis::SPACESHIP: {
            // (left <=> right) OP 0, where OP is the expr's own wanted comparison.
            llvm::Value* ss = call_comparison_source_operator(
                op_func, expr.get_operator_dispatch_info(), left_val, right_val);
            if (expr.has_spaceship_zero_func()) {
                result = generate_spaceship_zero_comparison(expr, ss);
            } else {
                std::string wanted_op = get_binary_operator_name(expr);
                result = compare_spaceship_result_to_zero(ss, wanted_op);
            }
            break;
        }
        case cmp_synthesis::SPACESHIP_SWAP: {
            // (right <=> left) OP' 0, where OP' is the swap of the expr's wanted comparison
            // (the spaceship source is declared on the right operand's aggregate).
            llvm::Value* ss = call_comparison_source_operator(
                op_func, expr.get_operator_dispatch_info(), right_val, left_val);
            if (expr.has_spaceship_zero_func()) {
                result = generate_spaceship_zero_comparison(expr, ss);
            } else {
                std::string wanted_op = swap_of_cmp_op(get_binary_operator_name(expr));
                result = compare_spaceship_result_to_zero(ss, wanted_op);
            }
            break;
        }
        case cmp_synthesis::COMPOSITE_AND:
        case cmp_synthesis::COMPOSITE_OR: {
            llvm::Value* term1 = call_comparison_source_operator(
                op_func, expr.get_operator_dispatch_info(), left_val, right_val);
            llvm::Value* term2 = call_comparison_source_operator(
                op_func, expr.get_composite_dispatch_info(), right_val, left_val);
            if (expr.composite_negate_terms()) {
                term1 = _builder->CreateNot(term1, "cmp_composite_neg1");
                term2 = _builder->CreateNot(term2, "cmp_composite_neg2");
            }
            result = (kind == cmp_synthesis::COMPOSITE_AND)
                ? _builder->CreateAnd(term1, term2, "cmp_composite_and")
                : _builder->CreateOr(term1, term2, "cmp_composite_or");
            break;
        }
    }

    _value = result;
    return true;
}

//
// Equal expression (==)
//

void implementation_generator::visit_equal_expression(equal_expression& expr) {
    if (generate_comparison_operator(expr)) return;

    if (expr.is_constant()) {
        _value = _context->get_llvm_constant_from_constant_value(expr.get_constant_value(), expr.get_type());
        if (_value) return;
    }

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F00C), expr.first_lexeme(),
            "Internal error: '==' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    auto left_type = expr.left()->get_type();
    auto right_type = expr.right()->get_type();

    // ── Address comparison for indirection types ─────────────────────────────
    auto is_addr = [](const std::shared_ptr<type>& t) {
        if (!t) return false;
        auto inner = type::is_reference(t) ? t->get_subtype() : t;
        inner = type::remove_const(inner);
        return type::is_pointer(inner) || type::is_link(inner) || type::is_view(inner)
            || type::is_owner(inner)   || type::is_null(inner)
            || type::is_fat_callable(inner);
    };
    if (is_addr(left_type) || is_addr(right_type)) {
        auto* ptr_ty = llvm::PointerType::get(_builder->getContext(), 0);
        if (type::is_reference(left_type) && is_addr(left_type)) {
            left = _builder->CreateLoad(ptr_ty, left, "left_addr");
        }
        if (type::is_reference(right_type) && is_addr(right_type)) {
            right = _builder->CreateLoad(ptr_ty, right, "right_addr");
        }
        // A callable compares by its target address only; the context is irrelevant.
        if (type::is_fat_callable(left_type)) left = extract_fn(*_builder, left);
        if (type::is_fat_callable(right_type)) right = extract_fn(*_builder, right);
        if (left->getType() != ptr_ty) left = _builder->CreateBitCast(left, ptr_ty);
        if (right->getType() != ptr_ty) right = _builder->CreateBitCast(right, ptr_ty);
        _value = _builder->CreateICmpEQ(left, right);
        return;
    }
    // ─────────────────────────────────────────────────────────────────────────

    // If operands are references or drains, dereference them.
    llvm::Type* type = _context->get_llvm_type(expr.get_type());
    if(type::is_reference(left_type) || type::is_drain(left_type)) {
        left = _builder->CreateLoad(type, left);
    }
    if(type::is_reference(right_type) || type::is_drain(right_type)) {
        right = _builder->CreateLoad(type, right);
    }

    if(!type::is_primitive(left_type) || !type::is_primitive(right_type)) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F00D), expr.first_lexeme(),
            "Internal error: '==' operator has a non-primitive operand during code generation; "
            "this should have been rejected during type resolution");
    }

    // For primitives, operand types are supposed to be aligned
    static auto bool_type = _context->from_type(primitive_type::BOOL);
    auto prim_left = (type::is_reference(left_type) || type::is_drain(left_type)) ? left_type->get_subtype() : left_type;
    auto prim = std::dynamic_pointer_cast<primitive_type>(type::remove_const(prim_left));

    if(prim->is_integer_or_bool()) {
        _value = _builder->CreateICmpEQ(left, right);
    } else if(prim->is_float()) {
        _value = _builder->CreateFCmpOEQ(left, right);
    } else {
        // TODO support for other types
    }
}

//
// Different expression (!=)
//

void implementation_generator::visit_different_expression(different_expression& expr) {
    if (generate_comparison_operator(expr)) return;

    if (expr.is_constant()) {
        _value = _context->get_llvm_constant_from_constant_value(expr.get_constant_value(), expr.get_type());
        if (_value) return;
    }

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F00E), expr.first_lexeme(),
            "Internal error: '!=' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    auto left_type = expr.left()->get_type();
    auto right_type = expr.right()->get_type();

    // ── Address comparison for indirection types ─────────────────────────────
    auto is_addr = [](const std::shared_ptr<type>& t) {
        if (!t) return false;
        auto inner = type::is_reference(t) ? t->get_subtype() : t;
        inner = type::remove_const(inner);
        return type::is_pointer(inner) || type::is_link(inner) || type::is_view(inner)
            || type::is_owner(inner)   || type::is_null(inner)
            || type::is_fat_callable(inner);
    };
    if (is_addr(left_type) || is_addr(right_type)) {
        auto* ptr_ty = llvm::PointerType::get(_builder->getContext(), 0);
        if (type::is_reference(left_type) && is_addr(left_type)) {
            left = _builder->CreateLoad(ptr_ty, left, "left_addr");
        }
        if (type::is_reference(right_type) && is_addr(right_type)) {
            right = _builder->CreateLoad(ptr_ty, right, "right_addr");
        }
        // A callable compares by its target address only; the context is irrelevant.
        if (type::is_fat_callable(left_type)) left = extract_fn(*_builder, left);
        if (type::is_fat_callable(right_type)) right = extract_fn(*_builder, right);
        if (left->getType() != ptr_ty) left = _builder->CreateBitCast(left, ptr_ty);
        if (right->getType() != ptr_ty) right = _builder->CreateBitCast(right, ptr_ty);
        _value = _builder->CreateICmpNE(left, right);
        return;
    }
    // ─────────────────────────────────────────────────────────────────────────

    // If operands are references or drains, dereference them.
    llvm::Type* type = _context->get_llvm_type(expr.get_type());
    if(type::is_reference(left_type) || type::is_drain(left_type)) {
        left = _builder->CreateLoad(type, left);
    }
    if(type::is_reference(right_type) || type::is_drain(right_type)) {
        right = _builder->CreateLoad(type, right);
    }

    if(!type::is_primitive(left_type) || !type::is_primitive(right_type)) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F00F), expr.first_lexeme(),
            "Internal error: '!=' operator has non-primitive operand during code generation; "
            "this should have been rejected during type resolution");
    }

    // For primitives, operand types are supposed to be aligned
    static auto bool_type = _context->from_type(primitive_type::BOOL);
    auto prim_left_ne = (type::is_reference(left_type) || type::is_drain(left_type)) ? left_type->get_subtype() : left_type;
    auto prim = std::dynamic_pointer_cast<primitive_type>(type::remove_const(prim_left_ne));

    if(prim->is_integer_or_bool()) {
        _value = _builder->CreateICmpNE(left, right);
    } else if(prim->is_float()) {
        _value = _builder->CreateFCmpONE(left, right);
    } else {
        // TODO support for other types
    }
}

//
// Lesser than expression (<)
//

void implementation_generator::visit_lesser_expression(lesser_expression& expr) {
    if (generate_comparison_operator(expr)) return;

    if (expr.is_constant()) {
        _value = _context->get_llvm_constant_from_constant_value(expr.get_constant_value(), expr.get_type());
        if (_value) return;
    }

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F010), expr.first_lexeme(),
            "Internal error: '<' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    // If operands are references, dereference them.
    llvm::Type* type = _context->get_llvm_type(expr.get_type());
    if(type::is_reference(expr.left()->get_type()) || type::is_drain(expr.left()->get_type())) {
        left = _builder->CreateLoad(type, left);
    }
    if(type::is_reference(expr.right()->get_type())) {
        right = _builder->CreateLoad(type, right);
    }

    if(!type::is_primitive(expr.left()->get_type()) || !type::is_primitive(expr.right()->get_type())) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F011), expr.first_lexeme(),
            "Internal error: '<' operator has non-primitive operand during code generation; "
            "this should have been rejected during type resolution");
    }

    // For primitives, operand types are supposed to be aligned
    static auto bool_type = _context->from_type(primitive_type::BOOL);
    auto prim_left_lt = (type::is_reference(expr.left()->get_type()) || type::is_drain(expr.left()->get_type())) ? expr.left()->get_type()->get_subtype() : expr.left()->get_type();
    auto prim = std::dynamic_pointer_cast<primitive_type>(type::remove_const(prim_left_lt));

    if(prim->is_integer_or_bool()) {
        if(prim->is_unsigned()) {
            _value = _builder->CreateICmpULT(left, right);
        } else {
            _value = _builder->CreateICmpSLT(left, right);
        }
    } else if(prim->is_float()) {
        _value = _builder->CreateFCmpOLT(left, right);
    } else {
        // TODO support for other types
    }
}

//
// Greater than expression (>)
//

void implementation_generator::visit_greater_expression(greater_expression& expr) {
    if (generate_comparison_operator(expr)) return;

    if (expr.is_constant()) {
        _value = _context->get_llvm_constant_from_constant_value(expr.get_constant_value(), expr.get_type());
        if (_value) return;
    }

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F012), expr.first_lexeme(),
            "Internal error: '>' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    // If operands are references, dereference them.
    llvm::Type* type = _context->get_llvm_type(expr.get_type());
    if(type::is_reference(expr.left()->get_type()) || type::is_drain(expr.left()->get_type())) {
        left = _builder->CreateLoad(type, left);
    }
    if(type::is_reference(expr.right()->get_type())) {
        right = _builder->CreateLoad(type, right);
    }

    if(!type::is_primitive(expr.left()->get_type()) || !type::is_primitive(expr.right()->get_type())) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F013), expr.first_lexeme(),
            "Internal error: '>' operator has non-primitive operand during code generation; "
            "this should have been rejected during type resolution");
    }

    // For primitives, operand types are supposed to be aligned
    static auto bool_type = _context->from_type(primitive_type::BOOL);
    auto prim_left_gt = (type::is_reference(expr.left()->get_type()) || type::is_drain(expr.left()->get_type())) ? expr.left()->get_type()->get_subtype() : expr.left()->get_type();
    auto prim = std::dynamic_pointer_cast<primitive_type>(type::remove_const(prim_left_gt));

    if(prim->is_integer_or_bool()) {
        if(prim->is_unsigned()) {
            _value = _builder->CreateICmpUGT(left, right);
        } else {
            _value = _builder->CreateICmpSGT(left, right);
        }
    } else if(prim->is_float()) {
        _value = _builder->CreateFCmpOGT(left, right);
    } else {
        // TODO support for other types
    }
}

//
// Lesser than or equal expression (<=)
//

void implementation_generator::visit_lesser_equal_expression(lesser_equal_expression& expr) {
    if (generate_comparison_operator(expr)) return;

    if (expr.is_constant()) {
        _value = _context->get_llvm_constant_from_constant_value(expr.get_constant_value(), expr.get_type());
        if (_value) return;
    }

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F014), expr.first_lexeme(),
            "Internal error: '<=' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    // If operands are references, dereference them.
    llvm::Type* type = _context->get_llvm_type(expr.get_type());
    if(type::is_reference(expr.left()->get_type()) || type::is_drain(expr.left()->get_type())) {
        left = _builder->CreateLoad(type, left);
    }
    if(type::is_reference(expr.right()->get_type())) {
        right = _builder->CreateLoad(type, right);
    }

    if(!type::is_primitive(expr.left()->get_type()) || !type::is_primitive(expr.right()->get_type())) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F015), expr.first_lexeme(),
            "Internal error: '<=' operator has non-primitive operand during code generation; "
            "this should have been rejected during type resolution");
    }

    // For primitives, operand types are supposed to be aligned
    static auto bool_type = _context->from_type(primitive_type::BOOL);
    auto prim_left_le = (type::is_reference(expr.left()->get_type()) || type::is_drain(expr.left()->get_type())) ? expr.left()->get_type()->get_subtype() : expr.left()->get_type();
    auto prim = std::dynamic_pointer_cast<primitive_type>(type::remove_const(prim_left_le));

    if(prim->is_integer_or_bool()) {
        if(prim->is_unsigned()) {
            _value = _builder->CreateICmpULE(left, right);
        } else {
            _value = _builder->CreateICmpSLE(left, right);
        }
    } else if(prim->is_float()) {
        _value = _builder->CreateFCmpOLE(left, right);
    } else {
        // TODO support for other types
    }
}

//
// Greater than or equal expression (>=)
//

void implementation_generator::visit_greater_equal_expression(greater_equal_expression& expr) {
    if (generate_comparison_operator(expr)) return;

    if (expr.is_constant()) {
        _value = _context->get_llvm_constant_from_constant_value(expr.get_constant_value(), expr.get_type());
        if (_value) return;
    }

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F016), expr.first_lexeme(),
            "Internal error: '>=' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    // If operands are references, dereference them.
    llvm::Type* type = _context->get_llvm_type(expr.get_type());
    if(type::is_reference(expr.left()->get_type()) || type::is_drain(expr.left()->get_type())) {
        left = _builder->CreateLoad(type, left);
    }
    if(type::is_reference(expr.right()->get_type())) {
        right = _builder->CreateLoad(type, right);
    }

    if(!type::is_primitive(expr.left()->get_type()) || !type::is_primitive(expr.right()->get_type())) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F016), expr.first_lexeme(),
            "Internal error: '>=' operator has non-primitive operand during code generation; "
            "this should have been rejected during type resolution");
    }

    // For primitives, operand types are supposed to be aligned
    static auto bool_type = _context->from_type(primitive_type::BOOL);
    auto prim_left_ge = (type::is_reference(expr.left()->get_type()) || type::is_drain(expr.left()->get_type())) ? expr.left()->get_type()->get_subtype() : expr.left()->get_type();
    auto prim = std::dynamic_pointer_cast<primitive_type>(type::remove_const(prim_left_ge));

    if(prim->is_integer_or_bool()) {
        if(prim->is_unsigned()) {
            _value = _builder->CreateICmpUGE(left, right);
        } else {
            _value = _builder->CreateICmpSGE(left, right);
        }
    } else if(prim->is_float()) {
        _value = _builder->CreateFCmpOGE(left, right);
    } else {
        // TODO support for other types
    }
}

} // namespace k::model::gen
