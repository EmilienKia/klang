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
#include "../common/operator_names.hpp"
#include "../parse/ast.hpp"

#include "../errors.hpp"

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
}

//
// Logical and expression (&&)
//

void implementation_generator::visit_logical_and_expression(logical_and_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

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
}

void implementation_generator::visit_logical_not_expression(logical_not_expression& expr) {
    if (generate_unary_operator_overload(expr)) return;

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
            || type::is_owner(t)   || type::is_null(t);
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
                            static auto bool_type_cached = _context->from_type(primitive_type::BOOL);
                            expr.set_type(bool_type_cached);
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
}

//
// Equal expression (==)
//

void implementation_generator::visit_equal_expression(equal_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

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
        return type::is_pointer(t) || type::is_link(t) || type::is_view(t)
            || type::is_owner(t)   || type::is_null(t);
    };
    if (is_addr(left_type) || is_addr(right_type)) {
        auto* ptr_ty = llvm::PointerType::get(_builder->getContext(), 0);
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
    if (generate_binary_operator_overload(expr)) return;

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
        return type::is_pointer(t) || type::is_link(t) || type::is_view(t)
            || type::is_owner(t)   || type::is_null(t);
    };
    if (is_addr(left_type) || is_addr(right_type)) {
        auto* ptr_ty = llvm::PointerType::get(_builder->getContext(), 0);
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
    if (generate_binary_operator_overload(expr)) return;

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
    if (generate_binary_operator_overload(expr)) return;

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
    if (generate_binary_operator_overload(expr)) return;

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
    if (generate_binary_operator_overload(expr)) return;

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
