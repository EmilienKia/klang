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

#include "llvm/Support/raw_os_ostream.h"
template<typename STM>
inline STM& operator << (STM& stm, const llvm::Type& type) {
    llvm::raw_os_ostream ross(stm);
    type.print(ross, true);
    return stm;
}

template<typename STM>
inline STM& operator << (STM& stm, const llvm::Value& value) {
    llvm::raw_os_ostream ross(stm);
    value.print(ross, true);
    return stm;
}

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

    auto target_type = left->get_type();
    if(type::is_reference(target_type)) {
        // Target type must be de-referenced
        target_type = std::dynamic_pointer_cast<reference_type>(target_type)->get_subtype();
    }
    if(!type::is_primitive(target_type)) {
        throw_error(0x0001, std::nullopt,
            "Arithmetic operators are not supported for non-primitive types: "
            "the left operand has type '{}'; only numeric primitive types are supported",
            {target_type ? target_type->to_string() : "?"});
    }
    if(type::is_prim_bool(target_type)) {
        throw_error(0x0002, std::nullopt,
            "Arithmetic operators cannot be applied to boolean operands: "
            "use logical operators ('&&', '||', '!') instead of arithmetic operators for boolean values");
    }

    expr.set_type(target_type);

    auto source_type = right->get_type();
    if(type::is_pointer(source_type)) {
        throw_error(0x0003, std::nullopt,
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
    }

    // TODO Promote to largest target_type instead to align to left operand.
    auto cast = adapt_type(right, target_type);
    if(!cast) {
        throw_error(0x0004, std::nullopt,
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
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    // If left operand is a reference, dereference it.
    // Right is supposed to be already dereferenced
    if(type::is_reference(expr.left()->get_type())) {
        auto ref_type = std::dynamic_pointer_cast<reference_type>(expr.left()->get_type());
        llvm::Type* type = _context->get_llvm_type(ref_type->get_subtype());
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
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    // If left operand is a reference, dereference it.
    // Right is supposed to be already dereferenced
    if(type::is_reference(expr.left()->get_type())) {
        auto ref_type = std::dynamic_pointer_cast<reference_type>(expr.left()->get_type());
        llvm::Type* type = _context->get_llvm_type(ref_type->get_subtype());
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
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    // If left operand is a reference, dereference it.
    // Right is supposed to be already dereferenced
    if(type::is_reference(expr.left()->get_type())) {
        auto ref_type = std::dynamic_pointer_cast<reference_type>(expr.left()->get_type());
        llvm::Type* type = _context->get_llvm_type(ref_type->get_subtype());
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
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    // If left operand is a reference, dereference it.
    // Right is supposed to be already dereferenced
    if(type::is_reference(expr.left()->get_type())) {
        auto ref_type = std::dynamic_pointer_cast<reference_type>(expr.left()->get_type());
        llvm::Type* type = _context->get_llvm_type(ref_type->get_subtype());
        left = _builder->CreateLoad(type, left);
    }

    if(auto prim = std::dynamic_pointer_cast<primitive_type>(expr.get_type())) {
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
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    // If left operand is a reference, dereference it.
    // Right is supposed to be already dereferenced
    if(type::is_reference(expr.left()->get_type())) {
        auto ref_type = std::dynamic_pointer_cast<reference_type>(expr.left()->get_type());
        llvm::Type* type = _context->get_llvm_type(ref_type->get_subtype());
        left = _builder->CreateLoad(type, left);
    }

    if(auto prim = std::dynamic_pointer_cast<primitive_type>(expr.get_type())) {
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
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    // If left operand is a reference, dereference it.
    // Right is supposed to be already dereferenced
    if(type::is_reference(expr.left()->get_type())) {
        auto ref_type = std::dynamic_pointer_cast<reference_type>(expr.left()->get_type());
        llvm::Type* type = _context->get_llvm_type(ref_type->get_subtype());
        left = _builder->CreateLoad(type, left);
    }

    if(auto prim = std::dynamic_pointer_cast<primitive_type>(expr.get_type())) {
        if(prim->is_integer()) {
            _value = _builder->CreateAnd(left, right);
        } else if(prim->is_float()) {
            throw_error(0x0005, std::nullopt,
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
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    // If left operand is a reference, dereference it.
    // Right is supposed to be already dereferenced
    if(type::is_reference(expr.left()->get_type())) {
        auto ref_type = std::dynamic_pointer_cast<reference_type>(expr.left()->get_type());
        llvm::Type* type = _context->get_llvm_type(ref_type->get_subtype());
        left = _builder->CreateLoad(type, left);
    }

    if(auto prim = std::dynamic_pointer_cast<primitive_type>(expr.get_type())) {
        if(prim->is_integer()) {
            _value = _builder->CreateOr(left, right);
        } else if(prim->is_float()) {
            throw_error(0x0006, std::nullopt,
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
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    // If left operand is a reference, dereference it.
    // Right is supposed to be already dereferenced
    if(type::is_reference(expr.left()->get_type())) {
        auto ref_type = std::dynamic_pointer_cast<reference_type>(expr.left()->get_type());
        llvm::Type* type = _context->get_llvm_type(ref_type->get_subtype());
        left = _builder->CreateLoad(type, left);
    }

    if(auto prim = std::dynamic_pointer_cast<primitive_type>(expr.get_type())) {
        if(prim->is_integer()) {
            _value = _builder->CreateXor(left, right);
        } else if(prim->is_float()) {
            throw_error(0x0007, std::nullopt,
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
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    // If left operand is a reference, dereference it.
    // Right is supposed to be already dereferenced
    if(type::is_reference(expr.left()->get_type())) {
        auto ref_type = std::dynamic_pointer_cast<reference_type>(expr.left()->get_type());
        llvm::Type* type = _context->get_llvm_type(ref_type->get_subtype());
        left = _builder->CreateLoad(type, left);
    }

    if(auto prim = std::dynamic_pointer_cast<primitive_type>(expr.get_type())) {
        if(prim->is_integer()) {
            // TODO may it poison when overflow ?
            _value = _builder->CreateShl(left, right);
        } else if(prim->is_float()) {
            throw_error(0x0008, std::nullopt,
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
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    // If left operand is a reference, dereference it.
    // Right is supposed to be already dereferenced
    if(type::is_reference(expr.left()->get_type())) {
        auto ref_type = std::dynamic_pointer_cast<reference_type>(expr.left()->get_type());
        llvm::Type* type = _context->get_llvm_type(ref_type->get_subtype());
        left = _builder->CreateLoad(type, left);
    }

    if(auto prim = std::dynamic_pointer_cast<primitive_type>(expr.get_type())) {
        if(prim->is_integer()) {
            if(prim->is_unsigned()) {
                // TODO may it poison when overflow ?
                _value = _builder->CreateLShr(left, right);
            } else {
                // TODO may it poison when overflow ?
                _value = _builder->CreateAShr(left, right);
            }
        } else if(prim->is_float()) {
            throw_error(0x0009, std::nullopt,
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

void type_reference_resolver::visit_assignation_expression(assignation_expression &expr) {
    // TODO Rework conversions and promotions and mutualize with symbol_type_resolver::process_arithmetic(...)
    visit_binary_expression(expr);

    auto left = expr.left();
    auto right = expr.right();

    auto left_type = left->get_type();

    if(!type::is_reference(left_type)) {
        throw_error(0x000A, std::nullopt,
            "The left operand of an assignment must be assignable (an lvalue): "
            "the left-hand side has type '{}' which is not a reference; "
            "you can only assign to a variable, parameter, or array element",
            {left_type ? left_type->to_string() : "?"});
    }
    auto ref_target_type = std::dynamic_pointer_cast<reference_type>(left_type);
    auto target_type = ref_target_type->get_subtype();

    if(type::is_reference(target_type)) {
        // Left hand is a ref-to-ref-to-something, i.e. a ref-something variable.
        // Deref again target type
        left = load_value_expression::make_shared(left);
        left->set_type(target_type);
        expr.assign_left(left);
        target_type = std::dynamic_pointer_cast<reference_type>(target_type)->get_subtype();
    }

    auto source_type = right->get_type();

    if(type::is_pointer(target_type)) {
        if(type::is_pointer(source_type)) {
            if(target_type->get_subtype() != source_type->get_subtype()) {
                throw_error(0x000B, std::nullopt,
                    "Pointer assignment type mismatch: "
                    "cannot assign a '{}' to a '{}'; pointer types must match exactly",
                    {source_type ? source_type->to_string() : "?",
                     target_type ? target_type->to_string() : "?"});
            }
        } else {
            throw_error(0x000C, std::nullopt,
                "Pointer assignment requires a pointer on the right-hand side: "
                "cannot assign a value of type '{}' to a pointer of type '{}'",
                {source_type ? source_type->to_string() : "?",
                 target_type ? target_type->to_string() : "?"});
        }
    } else if(!type::is_primitive(target_type)) {
        throw_error(0x000D, std::nullopt,
            "Assignment to a non-primitive, non-pointer type is not yet supported: "
            "the target has type '{}'; only assignments to primitive types and pointers are supported",
            {target_type ? target_type->to_string() : "?"});
    } else if(type::is_prim_bool(target_type)) {
        throw_error(0x000E, std::nullopt,
            "Direct arithmetic assignment to a boolean variable is not supported: "
            "use a comparison or logical expression to produce a boolean value for '{}'",
            {target_type ? target_type->to_string() : "?"});
    }

    // Type of an assignation is a reference
    expr.set_type(ref_target_type);

    // If source type is reference, deref it
    if(type::is_reference(source_type)) {
        // Source type must be de-referenced
        right = load_value_expression::make_shared(right);
        source_type = std::dynamic_pointer_cast<reference_type>(source_type)->get_subtype();
        right->set_type(source_type);
        expr.assign_right(right);
    }

    // TODO Promote to largest target_type instead to align to left operand.
    auto cast = adapt_type(right, target_type);
    if(!cast) {
        throw_error(0x000F, std::nullopt,
            "Incompatible types in assignment: "
            "the right-hand side of type '{}' cannot be implicitly converted to the target type '{}'; "
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

//
// Simple assignment expression (=)
//

void implementation_generator::visit_simple_assignation_expression(simple_assignation_expression& expr) {
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        throw_internal_error(0x001B, std::nullopt,
            "Internal error: assignment expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    auto left_ref_type = std::dynamic_pointer_cast<reference_type>(expr.left()->get_type())->get_subtype();
    auto left_type = left_ref_type->get_subtype();
    auto llvm_type = _context->get_llvm_type(left_type);

    _value = right;

    // Store the value, return the left ref
    _value = _builder->CreateStore(_value, left);
    _value = left;

}

//
// Arithmetic assignation expression
//

void symbol_resolver::visit_arithmetic_assignation_expression(arithmetic_assignation_expression &expr) {
    visit_assignation_expression(expr);
}

void type_reference_resolver::visit_arithmetic_assignation_expression(arithmetic_assignation_expression &expr) {
    visit_assignation_expression(expr);

    auto left = expr.left();
    auto right = expr.right();

    auto left_type = left->get_type();
    auto ref_target_type = std::dynamic_pointer_cast<reference_type>(left_type);
    auto target_type = ref_target_type->get_subtype();
    if(type::is_pointer(target_type)) {
        throw_error(0x0011, std::nullopt,
            "Arithmetic-assignment operators (e.g. '+=', '-=') cannot be applied to pointer types: "
            "the target has type '{}'; pointer arithmetic is not supported",
            {target_type ? target_type->to_string() : "?"});
    }
}

//
// Addition assignment expression (+=)
//

void implementation_generator::visit_addition_assignation_expression(additition_assignation_expression& expr) {
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        throw_internal_error(0x001C, std::nullopt,
            "Internal error: '+=' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    auto left_ref_type = std::dynamic_pointer_cast<reference_type>(expr.left()->get_type());
    auto left_type = left_ref_type->get_subtype();
    auto llvm_type = _context->get_llvm_type(left_type);

    auto left_val = _builder->CreateLoad(llvm_type, left);
    if(type::is_prim_integer(left_type)) {
        _value = _builder->CreateAdd(left_val, right);
    } else if(type::is_prim_float(left_type)) {
        _value = _builder->CreateFAdd(left_val, right);
    } else {
        // TODO: Support other types
    }

    // Store the value, return the left ref
    _value = _builder->CreateStore(_value, left);
    _value = left;
}

//
// Substraction assignment expression (-=)
//

void implementation_generator::visit_substraction_assignation_expression(substraction_assignation_expression& expr) {
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        throw_internal_error(0x001D, std::nullopt,
            "Internal error: '-=' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    auto left_ref_type = std::dynamic_pointer_cast<reference_type>(expr.left()->get_type());
    auto left_type = left_ref_type->get_subtype();
    auto llvm_type = _context->get_llvm_type(left_type);

    auto left_val = _builder->CreateLoad(llvm_type, left);
    if(type::is_prim_integer(left_type)) {
        _value = _builder->CreateSub(left_val, right);
    } else if(type::is_prim_float(left_type)) {
        _value = _builder->CreateFSub(left_val, right);
    } else {
        // TODO: Support other types
    }

    // Store the value, return the left ref
    _value = _builder->CreateStore(_value, left);
    _value = left;
}

//
// Multiplication assignment expression (*=)
//

void implementation_generator::visit_multiplication_assignation_expression(multiplication_assignation_expression& expr) {
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        throw_internal_error(0x001E, std::nullopt,
            "Internal error: '*=' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    auto left_ref_type = std::dynamic_pointer_cast<reference_type>(expr.left()->get_type());
    auto left_type = left_ref_type->get_subtype();
    auto llvm_type = _context->get_llvm_type(left_type);

    auto left_val = _builder->CreateLoad(llvm_type, left);
    if(type::is_prim_integer(left_type)) {
        _value = _builder->CreateMul(left_val, right);
    } else if(type::is_prim_float(left_type)) {
        _value = _builder->CreateFMul(left_val, right);
    } else {
        // TODO: Support other types
    }

    // Store the value, return the left ref
    _value = _builder->CreateStore(_value, left);
    _value = left;
}

//
// Division assignment expression (/=)
//

void implementation_generator::visit_division_assignation_expression(division_assignation_expression& expr) {
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        throw_internal_error(0x001F, std::nullopt,
            "Internal error: '/=' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    auto left_ref_type = std::dynamic_pointer_cast<reference_type>(expr.left()->get_type());
    auto left_type = left_ref_type->get_subtype();
    auto llvm_type = _context->get_llvm_type(left_type);

    auto left_val = _builder->CreateLoad(llvm_type, left);
    if(auto prim = std::dynamic_pointer_cast<primitive_type>(left_type)) {
        if(prim->is_integer()) {
            if(prim->is_unsigned()) {
                _value = _builder->CreateUDiv(left_val, right);
            } else {
                _value = _builder->CreateSDiv(left_val, right);
            }
        } else if(prim->is_float()) {
            _value = _builder->CreateFDiv(left_val, right);
        }
    } else {
        // TODO: Support other types
    }

    // Store the value, return the left ref
    _value = _builder->CreateStore(_value, left);
    _value = left;
}

//
// Modulo assignment expression (%=)
//

void implementation_generator::visit_modulo_assignation_expression(modulo_assignation_expression& expr) {
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        throw_internal_error(0x0020, std::nullopt,
            "Internal error: '%=' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    auto left_ref_type = std::dynamic_pointer_cast<reference_type>(expr.left()->get_type());
    auto left_type = left_ref_type->get_subtype();
    auto llvm_type = _context->get_llvm_type(left_type);

    auto left_val = _builder->CreateLoad(llvm_type, left);
    if(auto prim = std::dynamic_pointer_cast<primitive_type>(left_type)) {
        if(prim->is_integer()) {
            if(prim->is_unsigned()) {
                _value = _builder->CreateURem(left_val, right);
            } else {
                _value = _builder->CreateSRem(left_val, right);
            }
        } else if(prim->is_float()) {
            _value = _builder->CreateFRem(left_val, right);
        }
    } else {
        // TODO: Support other types
    }

    // Store the value, return the left ref
    _value = _builder->CreateStore(_value, left);
    _value = left;
}

//
// Bitwise and assignment expression
//

void implementation_generator::visit_bitwise_and_assignation_expression(bitwise_and_assignation_expression& expr) {
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        throw_internal_error(0x0021, std::nullopt,
            "Internal error: '&=' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    auto left_ref_type = std::dynamic_pointer_cast<reference_type>(expr.left()->get_type());
    auto left_type = left_ref_type->get_subtype();
    auto llvm_type = _context->get_llvm_type(left_type);

    auto left_val = _builder->CreateLoad(llvm_type, left);
    if(auto prim = std::dynamic_pointer_cast<primitive_type>(left_type)) {
        if(prim->is_integer()) {
            _value = _builder->CreateAnd(left_val, right);
        } else if(prim->is_float()) {
            throw_error(0x0018, std::nullopt,
                "Bitwise AND-assignment ('&=') cannot be applied to floating-point values: "
                "bitwise operations are only defined for integer types; "
                "the operand has type '{}'",
                {prim->to_string()});
        }
    } else {
        // TODO: Support other types
    }

    // Store the value, return the left ref
    _value = _builder->CreateStore(_value, left);
    _value = left;
}

//
// Bitwise or assignment expression
//

void implementation_generator::visit_bitwise_or_assignation_expression(bitwise_or_assignation_expression& expr) {
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        throw_internal_error(0x0022, std::nullopt,
            "Internal error: '|=' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    auto left_ref_type = std::dynamic_pointer_cast<reference_type>(expr.left()->get_type());
    auto left_type = left_ref_type->get_subtype();
    auto llvm_type = _context->get_llvm_type(left_type);

    auto left_val = _builder->CreateLoad(llvm_type, left);
    if(auto prim = std::dynamic_pointer_cast<primitive_type>(left_type)) {
        if(prim->is_integer()) {
            _value = _builder->CreateOr(left_val, right);
        } else if(prim->is_float()) {
            throw_error(0x001A, std::nullopt,
                "Bitwise OR-assignment ('|=') cannot be applied to floating-point values: "
                "bitwise operations are only defined for integer types; "
                "the operand has type '{}'",
                {prim->to_string()});
        }
    } else {
        // TODO: Support other types
    }

    // Store the value, return the left ref
    _value = _builder->CreateStore(_value, left);
    _value = left;
}

//
// Bitwise xor assignment expression
//

void implementation_generator::visit_bitwise_xor_assignation_expression(bitwise_xor_assignation_expression& expr) {
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        throw_internal_error(0x0023, std::nullopt,
            "Internal error: '^=' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    auto left_ref_type = std::dynamic_pointer_cast<reference_type>(expr.left()->get_type());
    auto left_type = left_ref_type->get_subtype();
    auto llvm_type = _context->get_llvm_type(left_type);

    auto left_val = _builder->CreateLoad(llvm_type, left);
    if(auto prim = std::dynamic_pointer_cast<primitive_type>(left_type)) {
        if(prim->is_integer()) {
            _value = _builder->CreateXor(left_val, right);
        } else if(prim->is_float()) {
            throw_error(0x001C, std::nullopt,
                "Bitwise XOR-assignment ('^=') cannot be applied to floating-point values: "
                "bitwise operations are only defined for integer types; "
                "the operand has type '{}'",
                {prim->to_string()});
        }
    } else {
        // TODO: Support other types
    }

    // Store the value, return the left ref
    _value = _builder->CreateStore(_value, left);
    _value = left;
}

//
// Left shift assignment expression
//

void implementation_generator::visit_left_shift_assignation_expression(left_shift_assignation_expression& expr) {
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        throw_internal_error(0x0024, std::nullopt,
            "Internal error: '<<=' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    auto left_ref_type = std::dynamic_pointer_cast<reference_type>(expr.left()->get_type());
    auto left_type = left_ref_type->get_subtype();
    auto llvm_type = _context->get_llvm_type(left_type);

    auto left_val = _builder->CreateLoad(llvm_type, left);
    if(auto prim = std::dynamic_pointer_cast<primitive_type>(left_type)) {
        if(prim->is_integer()) {
            // TODO may it poison when overflow ?
            _value = _builder->CreateShl(left_val, right);
        } else if(prim->is_float()) {
            throw_error(0x001E, std::nullopt,
                "Left shift-assignment ('<<=') cannot be applied to floating-point values: "
                "shift operations are only defined for integer types; "
                "the operand has type '{}'",
                {prim->to_string()});
        }
    } else {
        // TODO: Support other types
    }

    // Store the value, return the left ref
    _value = _builder->CreateStore(_value, left);
    _value = left;
}

//
// Right shift assignment expression
//

void implementation_generator::visit_right_shift_assignation_expression(right_shift_assignation_expression& expr) {
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        throw_internal_error(0x0025, std::nullopt,
            "Internal error: '>>=' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    auto left_ref_type = std::dynamic_pointer_cast<reference_type>(expr.left()->get_type());
    auto left_type = left_ref_type->get_subtype();
    auto llvm_type = _context->get_llvm_type(left_type);

    auto left_val = _builder->CreateLoad(llvm_type, left);
    if(auto prim = std::dynamic_pointer_cast<primitive_type>(left_type)) {
        if(prim->is_integer()) {
            if(prim->is_unsigned()) {
                // TODO may it poison when overflow ?
                _value = _builder->CreateLShr(left_val, right);
            } else {
                // TODO may it poison when overflow ?
                _value = _builder->CreateAShr(left_val, right);
            }
        } else if(prim->is_float()) {
            throw_error(0x0020, std::nullopt,
                "Right shift-assignment ('>>=') cannot be applied to floating-point values: "
                "shift operations are only defined for integer types; "
                "the operand has type '{}'",
                {prim->to_string()});
        }
    } else {
        // TODO: Support other types
    }

    // Store the value, return the left ref
    _value = _builder->CreateStore(_value, left);
    _value = left;
}

//
// Arithmetic unary expression
//

void type_reference_resolver::visit_arithmetic_unary_expression(arithmetic_unary_expression& expr) {
    visit_unary_expression(expr);

    auto& sub = expr.sub_expr();
    auto type = sub->get_type();

    if(type::is_pointer(type)) {
        throw_error(0x0021, std::nullopt,
            "Unary arithmetic operators cannot be applied to pointer types: "
            "the operand has type '{}'; only numeric primitive types are supported",
            {type ? type->to_string() : "?"});
    }

    if(type::is_reference(type)) {
        // Dereference type, if needed
        type = type->get_subtype();
    }

    if(!type::is_primitive(type)) {
        throw_error(0x0022, std::nullopt,
            "Unary arithmetic operators are not supported for non-primitive types: "
            "the operand has type '{}'; only numeric primitive types are supported",
            {type ? type->to_string() : "?"});
    }

    expr.set_type(type);
}

//
// Prefix increment expression (++expr)
//

void type_reference_resolver::visit_prefix_increment_expression(prefix_increment_expression& expr) {
    visit_unary_expression(expr);

    auto& sub = expr.sub_expr();
    auto type = sub->get_type();

    if(!type::is_reference(type)) {
        throw_error(0x002F, std::nullopt,
            "The operand of prefix '++' must be an assignable lvalue (a variable or dereferenced pointer), "
            "but got a non-reference type '{}'",
            {type ? type->to_string() : "?"});
    }

    auto ref_type = std::dynamic_pointer_cast<reference_type>(type);
    auto value_type = ref_type->get_subtype();
    if(type::is_reference(value_type)) {
        value_type = value_type->get_subtype();
    }

    if(!type::is_primitive(value_type)) {
        throw_error(0x0030, std::nullopt,
            "Prefix '++' requires a numeric primitive operand, but got type '{}'",
            {value_type ? value_type->to_string() : "?"});
    }
    if(type::is_prim_bool(value_type)) {
        throw_error(0x0031, std::nullopt,
            "Prefix '++' cannot be applied to a boolean operand");
    }

    // Prefix increment returns a reference to the (now updated) variable
    expr.set_type(ref_type);
}

void implementation_generator::visit_prefix_increment_expression(prefix_increment_expression& expr) {
    // Get the pointer (alloca) to the variable
    auto ptr = process_unary_expression(expr);
    if(!ptr) {
        _value = nullptr;
        return;
    }

    auto sub_type = expr.sub_expr()->get_type();
    // sub_type is a reference; get the underlying value type
    auto ref_type = std::dynamic_pointer_cast<reference_type>(sub_type);
    auto value_type = ref_type->get_subtype();
    if(type::is_reference(value_type)) {
        // ref-to-ref: load once more to get the actual pointer
        ptr = _builder->CreateLoad(_context->get_llvm_type(value_type), ptr);
        value_type = std::dynamic_pointer_cast<reference_type>(value_type)->get_subtype();
    }

    auto llvm_type = _context->get_llvm_type(value_type);
    auto old_val = _builder->CreateLoad(llvm_type, ptr);

    llvm::Value* new_val = nullptr;
    if(auto prim = std::dynamic_pointer_cast<primitive_type>(value_type)) {
        if(prim->is_integer()) {
            new_val = _builder->CreateAdd(old_val, llvm::ConstantInt::get(llvm_type, 1));
        } else if(prim->is_float()) {
            new_val = _builder->CreateFAdd(old_val, llvm::ConstantFP::get(llvm_type, 1.0));
        }
    }
    if(new_val) {
        _builder->CreateStore(new_val, ptr);
    }
    // Return the pointer (reference) to the updated variable
    _value = ptr;
}


//
// Prefix decrement expression (--expr)
//

void type_reference_resolver::visit_prefix_decrement_expression(prefix_decrement_expression& expr) {
    visit_unary_expression(expr);

    auto& sub = expr.sub_expr();
    auto type = sub->get_type();

    if(!type::is_reference(type)) {
        throw_error(0x0032, std::nullopt,
            "The operand of prefix '--' must be an assignable lvalue (a variable or dereferenced pointer), "
            "but got a non-reference type '{}'",
            {type ? type->to_string() : "?"});
    }

    auto ref_type = std::dynamic_pointer_cast<reference_type>(type);
    auto value_type = ref_type->get_subtype();
    if(type::is_reference(value_type)) {
        value_type = value_type->get_subtype();
    }

    if(!type::is_primitive(value_type)) {
        throw_error(0x0033, std::nullopt,
            "Prefix '--' requires a numeric primitive operand, but got type '{}'",
            {value_type ? value_type->to_string() : "?"});
    }
    if(type::is_prim_bool(value_type)) {
        throw_error(0x0034, std::nullopt,
            "Prefix '--' cannot be applied to a boolean operand");
    }

    // Prefix decrement returns a reference to the (now updated) variable
    expr.set_type(ref_type);
}

void implementation_generator::visit_prefix_decrement_expression(prefix_decrement_expression& expr) {
    auto ptr = process_unary_expression(expr);
    if(!ptr) {
        _value = nullptr;
        return;
    }

    auto sub_type = expr.sub_expr()->get_type();
    auto ref_type = std::dynamic_pointer_cast<reference_type>(sub_type);
    auto value_type = ref_type->get_subtype();
    if(type::is_reference(value_type)) {
        ptr = _builder->CreateLoad(_context->get_llvm_type(value_type), ptr);
        value_type = std::dynamic_pointer_cast<reference_type>(value_type)->get_subtype();
    }

    auto llvm_type = _context->get_llvm_type(value_type);
    auto old_val = _builder->CreateLoad(llvm_type, ptr);

    llvm::Value* new_val = nullptr;
    if(auto prim = std::dynamic_pointer_cast<primitive_type>(value_type)) {
        if(prim->is_integer()) {
            new_val = _builder->CreateSub(old_val, llvm::ConstantInt::get(llvm_type, 1));
        } else if(prim->is_float()) {
            new_val = _builder->CreateFSub(old_val, llvm::ConstantFP::get(llvm_type, 1.0));
        }
    }
    if(new_val) {
        _builder->CreateStore(new_val, ptr);
    }
    // Return the pointer (reference) to the updated variable
    _value = ptr;
}


//
// Postfix increment expression (expr++)
//

void type_reference_resolver::visit_postfix_increment_expression(postfix_increment_expression& expr) {
    visit_unary_expression(expr);

    auto& sub = expr.sub_expr();
    auto type = sub->get_type();

    if(!type::is_reference(type)) {
        throw_error(0x0035, std::nullopt,
            "The operand of postfix '++' must be an assignable lvalue (a variable or dereferenced pointer), "
            "but got a non-reference type '{}'",
            {type ? type->to_string() : "?"});
    }

    auto ref_type = std::dynamic_pointer_cast<reference_type>(type);
    auto value_type = ref_type->get_subtype();
    if(type::is_reference(value_type)) {
        value_type = value_type->get_subtype();
    }

    if(!type::is_primitive(value_type)) {
        throw_error(0x0036, std::nullopt,
            "Postfix '++' requires a numeric primitive operand, but got type '{}'",
            {value_type ? value_type->to_string() : "?"});
    }
    if(type::is_prim_bool(value_type)) {
        throw_error(0x0037, std::nullopt,
            "Postfix '++' cannot be applied to a boolean operand");
    }

    // Postfix increment returns the old value (not a reference)
    expr.set_type(value_type);
}

void implementation_generator::visit_postfix_increment_expression(postfix_increment_expression& expr) {
    auto ptr = process_unary_expression(expr);
    if(!ptr) {
        _value = nullptr;
        return;
    }

    auto sub_type = expr.sub_expr()->get_type();
    auto ref_type = std::dynamic_pointer_cast<reference_type>(sub_type);
    auto value_type = ref_type->get_subtype();
    if(type::is_reference(value_type)) {
        ptr = _builder->CreateLoad(_context->get_llvm_type(value_type), ptr);
        value_type = std::dynamic_pointer_cast<reference_type>(value_type)->get_subtype();
    }

    auto llvm_type = _context->get_llvm_type(value_type);
    // Save old value before increment
    auto old_val = _builder->CreateLoad(llvm_type, ptr);

    llvm::Value* new_val = nullptr;
    if(auto prim = std::dynamic_pointer_cast<primitive_type>(value_type)) {
        if(prim->is_integer()) {
            new_val = _builder->CreateAdd(old_val, llvm::ConstantInt::get(llvm_type, 1));
        } else if(prim->is_float()) {
            new_val = _builder->CreateFAdd(old_val, llvm::ConstantFP::get(llvm_type, 1.0));
        }
    }
    if(new_val) {
        _builder->CreateStore(new_val, ptr);
    }
    // Return the old (pre-increment) value
    _value = old_val;
}


//
// Postfix decrement expression (expr--)
//

void type_reference_resolver::visit_postfix_decrement_expression(postfix_decrement_expression& expr) {
    visit_unary_expression(expr);

    auto& sub = expr.sub_expr();
    auto type = sub->get_type();

    if(!type::is_reference(type)) {
        throw_error(0x0038, std::nullopt,
            "The operand of postfix '--' must be an assignable lvalue (a variable or dereferenced pointer), "
            "but got a non-reference type '{}'",
            {type ? type->to_string() : "?"});
    }

    auto ref_type = std::dynamic_pointer_cast<reference_type>(type);
    auto value_type = ref_type->get_subtype();
    if(type::is_reference(value_type)) {
        value_type = value_type->get_subtype();
    }

    if(!type::is_primitive(value_type)) {
        throw_error(0x0039, std::nullopt,
            "Postfix '--' requires a numeric primitive operand, but got type '{}'",
            {value_type ? value_type->to_string() : "?"});
    }
    if(type::is_prim_bool(value_type)) {
        throw_error(0x003A, std::nullopt,
            "Postfix '--' cannot be applied to a boolean operand");
    }

    // Postfix decrement returns the old value (not a reference)
    expr.set_type(value_type);
}

void implementation_generator::visit_postfix_decrement_expression(postfix_decrement_expression& expr) {
    auto ptr = process_unary_expression(expr);
    if(!ptr) {
        _value = nullptr;
        return;
    }

    auto sub_type = expr.sub_expr()->get_type();
    auto ref_type = std::dynamic_pointer_cast<reference_type>(sub_type);
    auto value_type = ref_type->get_subtype();
    if(type::is_reference(value_type)) {
        ptr = _builder->CreateLoad(_context->get_llvm_type(value_type), ptr);
        value_type = std::dynamic_pointer_cast<reference_type>(value_type)->get_subtype();
    }

    auto llvm_type = _context->get_llvm_type(value_type);
    // Save old value before decrement
    auto old_val = _builder->CreateLoad(llvm_type, ptr);

    llvm::Value* new_val = nullptr;
    if(auto prim = std::dynamic_pointer_cast<primitive_type>(value_type)) {
        if(prim->is_integer()) {
            new_val = _builder->CreateSub(old_val, llvm::ConstantInt::get(llvm_type, 1));
        } else if(prim->is_float()) {
            new_val = _builder->CreateFSub(old_val, llvm::ConstantFP::get(llvm_type, 1.0));
        }
    }
    if(new_val) {
        _builder->CreateStore(new_val, ptr);
    }
    // Return the old (pre-decrement) value
    _value = old_val;
}

//
// Unary plus expression
//

void implementation_generator::visit_unary_plus_expression(unary_plus_expression& expr) {
    auto val = process_unary_expression(expr);
    if(!val) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    auto type = expr.sub_expr()->get_type();
    if(type::is_reference(type)) {
        type = type->get_subtype();
        // If reference, dereference it.
        val = _builder->CreateLoad(_context->get_llvm_type(type), val);
    }

    if(type::is_primitive(type)) {
        // When primitive, return the value itself
        _value = val;
    } else {
        // TODO: Support other types
    }
}

//
// Unary minus expression
//

void implementation_generator::visit_unary_minus_expression(unary_minus_expression& expr) {
    auto val = process_unary_expression(expr);
    if(!val) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    auto type = expr.sub_expr()->get_type();
    if(type::is_reference(type)) {
        type = type->get_subtype();
        // If reference, dereference it.
        val = _builder->CreateLoad(_context->get_llvm_type(type), val);
    }

    if(auto prim = std::dynamic_pointer_cast<primitive_type>(type)) {
        // When primitive, return the value itself
        if(prim->is_integer_or_bool()) {
            // TODO may it poison when overflow ?
            //_value = _builder->CreateSub(_builder->getIntN(prim->type_size(), 0), val);
            _value = _builder->CreateNeg(val);
        } else if(prim->is_float()) {
            _value = _builder->CreateFNeg(val);
        } else {
            // TODO: Support other types
        };
    } else {
        // TODO: Support other types
    }
}

//
// Bitwise not expression
//

void implementation_generator::visit_bitwise_not_expression(bitwise_not_expression& expr) {
    auto val = process_unary_expression(expr);
    if(!val) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    auto type = expr.sub_expr()->get_type();
    if(type::is_reference(type)) {
        type = type->get_subtype();
        // If reference, dereference it.
        val = _builder->CreateLoad(_context->get_llvm_type(type), val);
    }

    if(auto prim = std::dynamic_pointer_cast<primitive_type>(type)) {
        // When primitive, return the value itself
        if(prim->is_integer_or_bool()) {
            _value = _builder->CreateNot(val);
        } else if(prim->is_float()) {
            throw_error(0x0023, std::nullopt,
                "Bitwise NOT ('~') cannot be applied to floating-point values: "
                "bitwise operations are only defined for integer and boolean types; "
                "the operand has type '{}'",
                {prim->to_string()});
        } else {
            // TODO: Support other types
        };
    } else {
        // TODO: Support other types
    }
}

//
// Logical binary expression
//

void type_reference_resolver::visit_logical_binary_expression(logical_binary_expression& expr) {
    visit_binary_expression(expr);

    auto left = expr.left();
    auto right = expr.right();

    auto left_type = left->get_type();
    auto right_type = right->get_type();

    if(type::is_reference(left_type)) {
        left = adapt_reference_load_value(left);
        expr.assign_left(left);
        left_type = left_type->get_subtype();
    }

    if(type::is_reference(right_type)) {
        right = adapt_reference_load_value(right);
        expr.assign_right(right);
        right_type = right_type->get_subtype();
    }

    if(!type::is_primitive( left->get_type()) || !type::is_primitive(right->get_type())) {
        throw_error(0x0024, std::nullopt,
            "Logical operators ('&&', '||') are not supported for non-primitive types: "
            "operands must be of a primitive type convertible to boolean, "
            "but found '{}' and '{}'",
            {left->get_type() ? left->get_type()->to_string() : "?",
             right->get_type() ? right->get_type()->to_string() : "?"});
    }

    auto bool_type = _context->from_type(primitive_type::BOOL);

    auto cast_left = adapt_type(left, bool_type);
    if(!cast_left) {
        throw_error(0x0025, std::nullopt,
            "The left operand of a logical operator cannot be implicitly converted to bool: "
            "the operand has type '{}'; logical operators require boolean-compatible operands",
            {left->get_type() ? left->get_type()->to_string() : "?"});
    } else if(cast_left != left ) {
        // Casted, assign casted expression instead of source.
        expr.assign_left(cast_left);
    } else {
        // Compatible type, no need to cast.
    }

    auto cast_right = adapt_type(right, bool_type);
    if(!cast_right) {
        throw_error(0x0026, std::nullopt,
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
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    // If left operand is a reference, dereference it.
    // Right is supposed to be already dereferenced
    if(type::is_reference(expr.left()->get_type())) {
        llvm::Type* type = _context->get_llvm_type(expr.left()->get_type());
        left = _builder->CreateLoad(type, left);
    }

    if(!type::is_primitive(expr.left()->get_type()) || !type::is_primitive(expr.right()->get_type())) {
        throw_internal_error(0x0026, std::nullopt,
            "Internal error: '&&' operator has non-primitive operand during code generation; "
            "this should have been rejected during type resolution");
    }

    _value = _builder->CreateAnd(left, right);
}


void implementation_generator::visit_logical_or_expression(logical_or_expression& expr) {
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    // If left operand is a reference, dereference it.
    // Right is supposed to be already dereferenced
    if(type::is_reference(expr.left()->get_type())) {
        llvm::Type* type = _context->get_llvm_type(expr.left()->get_type());
        left = _builder->CreateLoad(type, left);
    }

    if(!type::is_primitive(expr.left()->get_type()) || !type::is_primitive(expr.right()->get_type())) {
        throw_internal_error(0x0027, std::nullopt,
            "Internal error: '||' operator has non-primitive operand during code generation; "
            "this should have been rejected during type resolution");
    }

    _value = _builder->CreateOr(left, right);
}

//
// Logical not expression (!)
//

void type_reference_resolver::visit_logical_not_expression(logical_not_expression& expr) {
    visit_unary_expression(expr);

    auto& sub = expr.sub_expr();
    auto type = sub->get_type();

    if(type::is_reference(type)) {
        // Dereference type
        type = type->get_subtype();
    }

    if(!type::is_primitive(type)) {
        throw_error(0x0029, std::nullopt,
            "Logical NOT ('!') is not supported for non-primitive types: "
            "the operand has type '{}'; only primitive types convertible to boolean are supported",
            {type ? type->to_string() : "?"});
    }

    static auto bool_type = _context->from_type(primitive_type::BOOL);
    auto cast = adapt_type(sub, bool_type);
    if(!cast) {
        throw_error(0x002A, std::nullopt,
            "The operand of logical NOT ('!') cannot be implicitly converted to bool: "
            "the operand has type '{}'; logical NOT requires a boolean-compatible operand",
            {type ? type->to_string() : "?"});
    } else if(cast != sub ) {
        // Casted, assign casted expression instead of source.
        expr.assign(cast);
    } else {
        // Compatible type, no need to cast.
    }

    // For primitive type, logical is always returning boolean
    expr.set_type(bool_type);
}

void implementation_generator::visit_logical_not_expression(logical_not_expression& expr) {
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
        throw_internal_error(0x0028, std::nullopt,
            "Internal error: '!' operator has a non-primitive operand during code generation; "
            "this should have been rejected during type resolution");
    }

    _value = _builder->CreateNot(value);
}

//
// Comparison expressions
//
void type_reference_resolver::visit_comparison_expression(comparison_expression& expr) {
    visit_binary_expression(expr);

    auto& left = expr.left();
    auto& right = expr.right();

    auto left_type = left->get_type();
    auto right_type = right->get_type();

    if(type::is_reference(left_type)) {
        left = adapt_reference_load_value(left);
        expr.assign_left(left);
        left_type = left_type->get_subtype();
    }

    if(type::is_reference(right_type)) {
        right = adapt_reference_load_value(right);
        expr.assign_right(right);
        right_type = right_type->get_subtype();
    }

    if(!type::is_primitive(left_type) || !type::is_primitive(right_type)) {
        throw_error(0x002C, std::nullopt,
            "Comparison operators are not supported for non-primitive types: "
            "operands must be primitive types, but found '{}' and '{}'",
            {left_type ? left_type->to_string() : "?",
             right_type ? right_type->to_string() : "?"});
    }

    auto left_prim_type = std::dynamic_pointer_cast<primitive_type>(left_type);
    auto right_prim_type = std::dynamic_pointer_cast<primitive_type>(right_type);

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

    if(!adapted_left || !adapted_right) {
        throw_error(0x002D, std::nullopt,
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

    // For primitive type, logical is always returning boolean
    static auto bool_type = _context->from_type(primitive_type::BOOL);
    expr.set_type(bool_type);
}

//
// Equal expression (==)
//

void implementation_generator::visit_equal_expression(equal_expression& expr) {
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        throw_internal_error(0x0029, std::nullopt,
            "Internal error: '==' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    // If operands are references, dereference them.
    llvm::Type* type = _context->get_llvm_type(expr.get_type());
    if(type::is_reference(expr.left()->get_type())) {
        left = _builder->CreateLoad(type, left);
    }
    if(type::is_reference(expr.right()->get_type())) {
        right = _builder->CreateLoad(type, right);
    }

    if(!type::is_primitive(expr.left()->get_type()) || !type::is_primitive(expr.right()->get_type())) {
        throw_internal_error(0x002A, std::nullopt,
            "Internal error: '==' operator has non-primitive operand during code generation; "
            "this should have been rejected during type resolution");
    }

    // For primitives, operand types are supposed to be aligned
    static auto bool_type = _context->from_type(primitive_type::BOOL);
    auto prim = std::dynamic_pointer_cast<primitive_type>(expr.left()->get_type());

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
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        throw_internal_error(0x002B, std::nullopt,
            "Internal error: '!=' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    // If operands are references, dereference them.
    llvm::Type* type = _context->get_llvm_type(expr.get_type());
    if(type::is_reference(expr.left()->get_type())) {
        left = _builder->CreateLoad(type, left);
    }
    if(type::is_reference(expr.right()->get_type())) {
        right = _builder->CreateLoad(type, right);
    }

    if(!type::is_primitive(expr.left()->get_type()) || !type::is_primitive(expr.right()->get_type())) {
        throw_internal_error(0x002C, std::nullopt,
            "Internal error: '!=' operator has non-primitive operand during code generation; "
            "this should have been rejected during type resolution");
    }

    // For primitives, operand types are supposed to be aligned
    static auto bool_type = _context->from_type(primitive_type::BOOL);
    auto prim = std::dynamic_pointer_cast<primitive_type>(expr.left()->get_type());

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
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        throw_internal_error(0x002D, std::nullopt,
            "Internal error: '<' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    // If operands are references, dereference them.
    llvm::Type* type = _context->get_llvm_type(expr.get_type());
    if(type::is_reference(expr.left()->get_type())) {
        left = _builder->CreateLoad(type, left);
    }
    if(type::is_reference(expr.right()->get_type())) {
        right = _builder->CreateLoad(type, right);
    }

    if(!type::is_primitive(expr.left()->get_type()) || !type::is_primitive(expr.right()->get_type())) {
        throw_internal_error(0x002E, std::nullopt,
            "Internal error: '<' operator has non-primitive operand during code generation; "
            "this should have been rejected during type resolution");
    }

    // For primitives, operand types are supposed to be aligned
    static auto bool_type = _context->from_type(primitive_type::BOOL);
    auto prim = std::dynamic_pointer_cast<primitive_type>(expr.left()->get_type());

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
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        throw_internal_error(0x002F, std::nullopt,
            "Internal error: '>' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    // If operands are references, dereference them.
    llvm::Type* type = _context->get_llvm_type(expr.get_type());
    if(type::is_reference(expr.left()->get_type())) {
        left = _builder->CreateLoad(type, left);
    }
    if(type::is_reference(expr.right()->get_type())) {
        right = _builder->CreateLoad(type, right);
    }

    if(!type::is_primitive(expr.left()->get_type()) || !type::is_primitive(expr.right()->get_type())) {
        throw_internal_error(0x0030, std::nullopt,
            "Internal error: '>' operator has non-primitive operand during code generation; "
            "this should have been rejected during type resolution");
    }

    // For primitives, operand types are supposed to be aligned
    static auto bool_type = _context->from_type(primitive_type::BOOL);
    auto prim = std::dynamic_pointer_cast<primitive_type>(expr.left()->get_type());

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
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        throw_internal_error(0x0031, std::nullopt,
            "Internal error: '<=' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    // If operands are references, dereference them.
    llvm::Type* type = _context->get_llvm_type(expr.get_type());
    if(type::is_reference(expr.left()->get_type())) {
        left = _builder->CreateLoad(type, left);
    }
    if(type::is_reference(expr.right()->get_type())) {
        right = _builder->CreateLoad(type, right);
    }

    if(!type::is_primitive(expr.left()->get_type()) || !type::is_primitive(expr.right()->get_type())) {
        throw_internal_error(0x0032, std::nullopt,
            "Internal error: '<=' operator has non-primitive operand during code generation; "
            "this should have been rejected during type resolution");
    }

    // For primitives, operand types are supposed to be aligned
    static auto bool_type = _context->from_type(primitive_type::BOOL);
    auto prim = std::dynamic_pointer_cast<primitive_type>(expr.left()->get_type());

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
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        throw_internal_error(0x0033, std::nullopt,
            "Internal error: '>=' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    // If operands are references, dereference them.
    llvm::Type* type = _context->get_llvm_type(expr.get_type());
    if(type::is_reference(expr.left()->get_type())) {
        left = _builder->CreateLoad(type, left);
    }
    if(type::is_reference(expr.right()->get_type())) {
        right = _builder->CreateLoad(type, right);
    }

    if(!type::is_primitive(expr.left()->get_type()) || !type::is_primitive(expr.right()->get_type())) {
        throw_internal_error(0x0034, std::nullopt,
            "Internal error: '>=' operator has non-primitive operand during code generation; "
            "this should have been rejected during type resolution");
    }

    // For primitives, operand types are supposed to be aligned
    static auto bool_type = _context->from_type(primitive_type::BOOL);
    auto prim = std::dynamic_pointer_cast<primitive_type>(expr.left()->get_type());

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
