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
#include "gen_operators_helpers.hpp"

namespace k::model::gen {


//
// Arithmetic binary expression
//

void symbol_resolver::process_arithmetic(binary_expression& expr) {
    visit_binary_expression(expr);
}

void type_reference_resolver::process_arithmetic(binary_expression& expr) {
    // TODO Rework conversions and promotions
    visit_binary_expression(expr);

    auto left = expr.left();
    auto right = expr.right();

    auto left_type = left->get_type();
    auto target_type = left_type;
    if(type::is_reference(target_type)) {
        // Target type must be de-referenced
        target_type = std::dynamic_pointer_cast<reference_type>(target_type)->get_subtype();
    } else if(type::is_drain(target_type)) {
        // Drain type must be unwrapped like a reference
        target_type = std::dynamic_pointer_cast<drain_type>(target_type)->get_drained_type();
    }
    // Detect constness before stripping const qualifier
    bool is_const_left = type::is_const(target_type);
    // Strip const qualifier for arithmetic type checks (const is compile-time only)
    target_type = type::remove_const(target_type);

    // ── Enum → underlying primitive conversion for both operands ──
    if (auto left_enum = std::dynamic_pointer_cast<enum_type>(target_type)) {
        target_type = left_enum->get_underlying_type();
        left = adapt_type(left, target_type);
        if (left) expr.assign_left(left);
    }

    // ── Operator overload for aggregate (struct/class/interface) references ──
    if(type::is_struct(target_type)) {
        auto st_type = std::dynamic_pointer_cast<struct_type>(type::remove_const(target_type));
        if (st_type) {
            auto agg = st_type->get_struct();
            if (agg) {
                auto [op_func, adapted_right] = resolve_binary_operator_overload(expr, agg, left, right, is_const_left);
                if (op_func) {
                    // Store the resolved operator function on the expression
                    expr.set_operator_func(op_func);
                    // Apply the adapted right operand (implicit cast if needed)
                    if (adapted_right && adapted_right != right) {
                        expr.assign_right(adapted_right);
                    }
                    // Set the expression type to the return type of the operator function
                    if (op_func->has_return_type()) {
                        expr.set_type(op_func->get_return_type());
                    } else {
                        expr.set_type(target_type);
                    }
                    // Compute dispatch info for virtual calls
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
        throw_error(static_cast<unsigned int>(k::diag::function_diag::ERR_PARAM_VOID_NOT_ALLOWED), expr.first_lexeme(),
            "No matching operator overload found for non-primitive type: "
            "the left operand has type '{}'; define an operator function or use primitive types",
            {target_type ? target_type->to_string() : "?"});
    }

    if(!type::is_primitive(target_type)) {
        throw_error(static_cast<unsigned int>(k::diag::function_diag::ERR_PARAM_VOID_NOT_ALLOWED), expr.first_lexeme(),
            "Arithmetic operators are not supported for non-primitive types: "
            "the left operand has type '{}'; only numeric primitive types are supported",
            {target_type ? target_type->to_string() : "?"});
    }
    if(type::is_prim_bool(target_type)) {
        throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_ARITH_TYPE_MISMATCH), expr.first_lexeme(),
            "Arithmetic operators cannot be applied to boolean operands: "
            "use logical operators ('&&', '||', '!') instead of arithmetic operators for boolean values");
    }

    expr.set_type(target_type);

    auto source_type = right->get_type();
    if(type::is_pointer(source_type)) {
        throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_ARITH_NO_COMMON_TYPE), expr.first_lexeme(),
            "Arithmetic operators are not supported for pointer types: "
            "the right operand has a pointer type '{}'; pointer arithmetic is not allowed",
            {source_type ? source_type->to_string() : "?"});
    }
    // If source type is reference, deref it
    if(type::is_reference(source_type)) {
        // Source type must be de-referenced
        right = load_value_expression::make_shared(right);
        source_type = std::dynamic_pointer_cast<reference_type>(source_type)->get_subtype();
        right->set_type(source_type);
        expr.assign_right(right);
    } else if(type::is_drain(source_type)) {
        // Drain type must be dereferenced like a reference
        right = load_value_expression::make_shared(right);
        source_type = std::dynamic_pointer_cast<drain_type>(source_type)->get_drained_type();
        right->set_type(source_type);
        expr.assign_right(right);
    }
    // Convert right enum to underlying primitive too
    source_type = type::remove_const(source_type);
    if (auto right_enum = std::dynamic_pointer_cast<enum_type>(source_type)) {
        source_type = right_enum->get_underlying_type();
        right = adapt_type(right, source_type);
        if (right) {
            expr.assign_right(right);
        }
    }

    // TODO Promote to largest target_type instead to align to left operand.
    auto cast = adapt_type(right, target_type);
    if(!cast) {
        throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_ARITH_MODULO_NOT_INT), expr.first_lexeme(),
            "Incompatible types in arithmetic expression: "
            "the right operand of type '{}' cannot be implicitly converted to the left operand type '{}'; "
            "use an explicit cast if a narrowing conversion is intended",
            {right->get_type() ? right->get_type()->to_string() : "?",
             target_type ? target_type->to_string() : "?"});
    } else if(cast != right) {
        // Casted, assign casted expression instead of right source.
        expr.assign_right(cast);
    } else {
        // Compatible target_type, no need to cast.
    }
}

void symbol_resolver::visit_arithmetic_binary_expression(arithmetic_binary_expression &expr) {
    process_arithmetic(expr);
}

void type_reference_resolver::visit_arithmetic_binary_expression(arithmetic_binary_expression &expr) {
    process_arithmetic(expr);
}

//
// Addition expression (+)
//

void implementation_generator::visit_addition_expression(addition_expression &expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    // If left operand is a reference, dereference it.
    // Right is supposed to be already dereferenced
    if(type::is_reference(expr.left()->get_type()) || type::is_drain(expr.left()->get_type())) {
        llvm::Type* type = _context->get_llvm_type(expr.left()->get_type()->get_subtype());
        left = _builder->CreateLoad(type, left);
    }

    if(type::is_prim_integer(expr.get_type())) {
        _value = _builder->CreateAdd(left, right);
    } else if(type::is_prim_float(expr.get_type())) {
        _value = _builder->CreateFAdd(left, right);
    } else {
        // TODO: Support other types
    }
}

//
// Substraction expression (-)
//

void implementation_generator::visit_substraction_expression(substraction_expression &expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    // If left operand is a reference, dereference it.
    // Right is supposed to be already dereferenced
    if(type::is_reference(expr.left()->get_type()) || type::is_drain(expr.left()->get_type())) {
        llvm::Type* type = _context->get_llvm_type(expr.left()->get_type()->get_subtype());
        left = _builder->CreateLoad(type, left);
    }

    if(type::is_prim_integer(expr.get_type())) {
        _value = _builder->CreateSub(left, right);
    } else if(type::is_prim_float(expr.get_type())) {
        _value = _builder->CreateFSub(left, right);
    } else {
        // TODO: Support other types
    }
}

//
// Multiplication expression (*)
//

void implementation_generator::visit_multiplication_expression(multiplication_expression &expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    // If left operand is a reference, dereference it.
    // Right is supposed to be already dereferenced
    if(type::is_reference(expr.left()->get_type()) || type::is_drain(expr.left()->get_type())) {
        llvm::Type* type = _context->get_llvm_type(expr.left()->get_type()->get_subtype());
        left = _builder->CreateLoad(type, left);
    }

    // TODO: Check for type alignement
    if(type::is_prim_integer(expr.get_type())) {
        // TODO Should poison for int/uint multiplication overflow ?
        _value = _builder->CreateMul(left, right);
    } else if(type::is_prim_float(expr.get_type())) {
        _value = _builder->CreateFMul(left, right);
    } else {
        // TODO: Support other types
    }
}

//
// Division expression (/)
//

void implementation_generator::visit_division_expression(division_expression &expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    // If left operand is a reference, dereference it.
    // Right is supposed to be already dereferenced
    if(type::is_reference(expr.left()->get_type()) || type::is_drain(expr.left()->get_type())) {
        llvm::Type* type = _context->get_llvm_type(expr.left()->get_type()->get_subtype());
        left = _builder->CreateLoad(type, left);
    }

    if(auto prim = std::dynamic_pointer_cast<primitive_type>(type::remove_const(expr.get_type()))) {
        if(prim->is_integer()) {
            if(prim->is_unsigned()) {
                _value = _builder->CreateUDiv(left, right);
            } else {
                _value = _builder->CreateSDiv(left, right);
            }
        } else if(prim->is_float()) {
            _value = _builder->CreateFDiv(left, right);
        }
    } else {
        // TODO: Support other types
    }
}

//
// Modulo expression (%)
//

void implementation_generator::visit_modulo_expression(modulo_expression &expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    // If left operand is a reference, dereference it.
    // Right is supposed to be already dereferenced
    if(type::is_reference(expr.left()->get_type()) || type::is_drain(expr.left()->get_type())) {
        llvm::Type* type = _context->get_llvm_type(expr.left()->get_type()->get_subtype());
        left = _builder->CreateLoad(type, left);
    }

    if(auto prim = std::dynamic_pointer_cast<primitive_type>(type::remove_const(expr.get_type()))) {
        if(prim->is_integer()) {
            if(prim->is_unsigned()) {
                _value = _builder->CreateURem(left, right);
            } else {
                _value = _builder->CreateSRem(left, right);
            }
        } else if(prim->is_float()) {
            _value = _builder->CreateFRem(left, right);
        }
    } else {
        // TODO: Support other types
    }
}

//
// Bitwise and expression
//

void implementation_generator::visit_bitwise_and_expression(bitwise_and_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    // If left operand is a reference, dereference it.
    // Right is supposed to be already dereferenced
    if(type::is_reference(expr.left()->get_type()) || type::is_drain(expr.left()->get_type())) {
        llvm::Type* type = _context->get_llvm_type(expr.left()->get_type()->get_subtype());
        left = _builder->CreateLoad(type, left);
    }

    if(auto prim = std::dynamic_pointer_cast<primitive_type>(type::remove_const(expr.get_type()))) {
        if(prim->is_integer()) {
            _value = _builder->CreateAnd(left, right);
        } else if(prim->is_float()) {
            throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_BITWISE_AND_INCOMPATIBLE), expr.first_lexeme(),
                "Bitwise AND ('&') cannot be applied to floating-point values: "
                "bitwise operations are only defined for integer types; "
                "the operand has type '{}'",
                {prim->to_string()});
        }
    } else {
        // TODO: Support other types
    }
}

//
// Bitwise or expression
//

void implementation_generator::visit_bitwise_or_expression(bitwise_or_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    // If left operand is a reference, dereference it.
    // Right is supposed to be already dereferenced
    if(type::is_reference(expr.left()->get_type()) || type::is_drain(expr.left()->get_type())) {
        llvm::Type* type = _context->get_llvm_type(expr.left()->get_type()->get_subtype());
        left = _builder->CreateLoad(type, left);
    }

    if(auto prim = std::dynamic_pointer_cast<primitive_type>(type::remove_const(expr.get_type()))) {
        if(prim->is_integer()) {
            _value = _builder->CreateOr(left, right);
        } else if(prim->is_float()) {
            throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_BITWISE_OR_INCOMPATIBLE), expr.first_lexeme(),
                "Bitwise OR ('|') cannot be applied to floating-point values: "
                "bitwise operations are only defined for integer types; "
                "the operand has type '{}'",
                {prim->to_string()});
        }
    } else {
        // TODO: Support other types
    }
}

//
// Bitwise xor expression
//

void implementation_generator::visit_bitwise_xor_expression(bitwise_xor_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    // If left operand is a reference, dereference it.
    // Right is supposed to be already dereferenced
    if(type::is_reference(expr.left()->get_type()) || type::is_drain(expr.left()->get_type())) {
        llvm::Type* type = _context->get_llvm_type(expr.left()->get_type()->get_subtype());
        left = _builder->CreateLoad(type, left);
    }

    if(auto prim = std::dynamic_pointer_cast<primitive_type>(type::remove_const(expr.get_type()))) {
        if(prim->is_integer()) {
            _value = _builder->CreateXor(left, right);
        } else if(prim->is_float()) {
            throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_BITWISE_XOR_INCOMPATIBLE), expr.first_lexeme(),
                "Bitwise XOR ('^') cannot be applied to floating-point values: "
                "bitwise operations are only defined for integer types; "
                "the operand has type '{}'",
                {prim->to_string()});
        }
    } else {
        // TODO: Support other types
    }
}

//
// Left shift expression
//

void implementation_generator::visit_left_shift_expression(left_shift_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    // If left operand is a reference, dereference it.
    // Right is supposed to be already dereferenced
    if(type::is_reference(expr.left()->get_type()) || type::is_drain(expr.left()->get_type())) {
        llvm::Type* type = _context->get_llvm_type(expr.left()->get_type()->get_subtype());
        left = _builder->CreateLoad(type, left);
    }

    if(auto prim = std::dynamic_pointer_cast<primitive_type>(type::remove_const(expr.get_type()))) {
        if(prim->is_integer()) {
            // TODO may it poison when overflow ?
            _value = _builder->CreateShl(left, right);
        } else if(prim->is_float()) {
            throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_SHIFT_ASSIGN_INCOMPATIBLE), expr.first_lexeme(),
                "Left shift ('<<') cannot be applied to floating-point values: "
                "shift operations are only defined for integer types; "
                "the operand has type '{}'",
                {prim->to_string()});
        }
    } else {
        // TODO: Support other types
    }
}

//
// Right shift expression
//

void implementation_generator::visit_right_shift_expression(right_shift_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    // If left operand is a reference, dereference it.
    // Right is supposed to be already dereferenced
    if(type::is_reference(expr.left()->get_type()) || type::is_drain(expr.left()->get_type())) {
        llvm::Type* type = _context->get_llvm_type(expr.left()->get_type()->get_subtype());
        left = _builder->CreateLoad(type, left);
    }

    if(auto prim = std::dynamic_pointer_cast<primitive_type>(type::remove_const(expr.get_type()))) {
        if(prim->is_integer()) {
            if(prim->is_unsigned()) {
                // TODO may it poison when overflow ?
                _value = _builder->CreateLShr(left, right);
            } else {
                // TODO may it poison when overflow ?
                _value = _builder->CreateAShr(left, right);
            }
        } else if(prim->is_float()) {
            throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_BITWISE_ASSIGN_INCOMPATIBLE), expr.first_lexeme(),
                "Right shift ('>>') cannot be applied to floating-point values: "
                "shift operations are only defined for integer types; "
                "the operand has type '{}'",
                {prim->to_string()});
        }
    } else {
        // TODO: Support other types
    }
}

//
// Assignation expression
//

/**
 * Resolve an assignment expression (=, +=, -=, etc.): validate target, type-check operands.
 *
 * Steps:
 *   1. Resolve left and right sub-expressions.
 *   2. Validate that the left operand is assignable (reference, not const).
 *   3. For struct types: check for operator= overload or direct copy.
 *   4. For owner types: validate move semantics (right must be owner or new).
 *   5. For pointer/link/view: validate type compatibility and const-correctness.
 *   6. For primitives: adapt right operand type to match left.
 *   7. Set result type.
 */

} // namespace k::model::gen
