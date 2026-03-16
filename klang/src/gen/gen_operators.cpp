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
    // Strip const qualifier for arithmetic type checks (const is compile-time only)
    target_type = type::remove_const(target_type);
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

    // ── Const-check ──────────────────────────────────────────────────────────
    // If the target type is const-qualified (ref<const T>), assignment is forbidden.
    if (type::is_const(target_type)) {
        throw_error(0x0080, std::nullopt,
            "Cannot assign to a const variable: "
            "the left-hand side has type '{}' which is const; "
            "const variables cannot be modified after initialisation",
            {target_type ? target_type->to_string() : "?"});
    }
    // ─────────────────────────────────────────────────────────────────────────

    if(type::is_reference(target_type)) {
        // Left hand is ref-to-ref: assignment acts on the underlying object.
        left = load_value_expression::make_shared(left);
        left->set_type(target_type);
        expr.assign_left(left);
        target_type = std::dynamic_pointer_cast<reference_type>(target_type)->get_subtype();
    } else if (type::is_link(target_type)) {
        // Left hand is ref-to-link.
        // Determine if this is a rebind (RHS is an indirection) or
        // an assignment to the pointed object (RHS is a value).
        auto link_subtype = std::dynamic_pointer_cast<link_type>(target_type)->get_linked_type();
        auto rhs_type = right->get_type();
        // Unwrap ref<indirection> from rhs_type
        auto rhs_effective = rhs_type;
        if (type::is_reference(rhs_type)) {
            auto inner = std::dynamic_pointer_cast<reference_type>(rhs_type)->get_subtype();
            if (type::is_link(inner) || type::is_pointer(inner) || type::is_pinned(inner)) {
                rhs_effective = inner;
            }
        }
        // Helper lambda: check if rhs_effective is an indirection compatible with link_subtype (same, static upcast, or dynamic downcast)
        auto is_rebind_compatible = [&]() -> bool {
            if (!type::is_any_indirection(rhs_effective) || !rhs_effective->get_subtype() || !link_subtype)
                return false;
            auto rhs_sub_nc = type::remove_const(rhs_effective->get_subtype());
            auto lnk_sub_nc = type::remove_const(link_subtype);
            if (type::are_equal(rhs_sub_nc, lnk_sub_nc)) return true;
            auto src_st = std::dynamic_pointer_cast<struct_type>(rhs_sub_nc);
            auto tgt_st = std::dynamic_pointer_cast<struct_type>(lnk_sub_nc);
            if (!src_st || !tgt_st || !src_st->get_struct() || !tgt_st->get_struct()) return false;
            // Static upcast: rhs points to Derived, link points to Base
            if (src_st->get_struct()->is_derived_from(tgt_st->get_struct())) return true;
            // Dynamic downcast: rhs points to Base, link points to Derived (klass/interface only)
            if (tgt_st->get_struct()->is_derived_from(src_st->get_struct()) &&
                std::dynamic_pointer_cast<klass>(tgt_st->get_struct()) != nullptr) return true;
            return false;
        };
        // If RHS is an indirection compatible (same or upcast) with link_subtype: REBIND
        if (is_rebind_compatible()) {
            // Rebind: check const compatibility (const T~ ← T~ is OK; T~ ← const T~ is not)
            if (type::is_const(rhs_effective->get_subtype()) && !type::is_const(link_subtype)) {
                throw_error(0x0082, std::nullopt,
                    "Cannot rebind a link-to-mutable ('{}') from a link-to-const ('{}'): "
                    "this would allow modification of a const object",
                    {target_type ? target_type->to_string() : "?",
                     rhs_type ? rhs_type->to_string() : "?"});
            }
            // Rebind: load the source address and store into the link alloca.
            // If source is nullable, warn — null-check at IR level.
            if (type::is_nullable_indirection(rhs_effective)) {
                auto diag = k::log::diagnostic::make_warning(with_flag(0x0072),
                    "Rebinding a link from a nullable indirection (type '{}'): "
                    "a runtime null-check will be inserted",
                    {rhs_type ? rhs_type->to_string() : "?"});
                logger_relay::report(diag);
            }
            // Unwrap the ref wrapper from rhs if needed
            if (type::is_reference(rhs_type)) {
                right = load_value_expression::make_shared(right);
                rhs_type = rhs_effective;
                right->set_type(rhs_type);
                expr.assign_right(right);
            }
            // Determine whether to use static upcast or dynamic downcast
            {
                auto rhs_sub_nc = type::remove_const(right->get_type()->get_subtype());
                auto lnk_sub_nc = type::remove_const(link_subtype);
                if (!type::are_equal(rhs_sub_nc, lnk_sub_nc)) {
                    auto src_st = std::dynamic_pointer_cast<struct_type>(rhs_sub_nc);
                    auto tgt_st = std::dynamic_pointer_cast<struct_type>(lnk_sub_nc);
                    bool is_static_upcast = src_st && tgt_st &&
                        src_st->get_struct() && tgt_st->get_struct() &&
                        src_st->get_struct()->is_derived_from(tgt_st->get_struct());
                    if (is_static_upcast) {
                        auto upcast = cast_expression::make_shared(right, target_type);
                        upcast->set_type(target_type);
                        expr.assign_right(upcast);
                    } else {
                        // Dynamic downcast — lien is non-null, so fatal on null result
                        auto dc = cast_expression::make_shared(right, target_type, /*null_is_fatal=*/true);
                        expr.assign_right(dc);
                    }
                }
            }
            // The assignment stores a new address into the link alloca.
            expr.set_type(ref_target_type);
            return;
        }
        // Otherwise: transparent reference — assignment to the pointed object.
        left = load_value_expression::make_shared(left);
        left->set_type(target_type);
        auto ref_to_target = link_subtype->get_reference();
        left->set_type(ref_to_target);
        expr.assign_left(left);
        target_type = link_subtype;
        ref_target_type = ref_to_target;
    } else if (type::is_pinned(target_type)) {
        throw_error(0x0070, std::nullopt,
            "Cannot assign to a pinned indirection (type '{}'): "
            "a pinned ('^') is immutable after initialisation",
            {target_type ? target_type->to_string() : "?"});
    }

    auto source_type = right->get_type();

    // Unwrap ref<link/ptr/pin> for source-side checks
    auto effective_source_type = source_type;
    if (type::is_reference(source_type)) {
        auto inner = std::dynamic_pointer_cast<reference_type>(source_type)->get_subtype();
        if (type::is_link(inner) || type::is_pointer(inner) || type::is_pinned(inner)) {
            effective_source_type = inner;
        }
    }

    if(type::is_pointer(target_type)) {
        // Null literal: always compatible with any pointer type.
        if(type::is_null(effective_source_type) || type::is_null(source_type)) {
            expr.set_type(ref_target_type);
            return;
        }
        if(type::is_pointer(effective_source_type) || type::is_link(effective_source_type)
           || type::is_pinned(effective_source_type)) {
            auto src_sub = effective_source_type->get_subtype();
            auto tgt_sub = target_type->get_subtype();
            // Strip const from both sides for structural comparison
            auto src_sub_nc = type::remove_const(src_sub);
            auto tgt_sub_nc = type::remove_const(tgt_sub);
            if (src_sub_nc != tgt_sub_nc) {
                // Check static upcast: ptr<Derived>→ptr<Base>
                auto src_st = std::dynamic_pointer_cast<struct_type>(src_sub_nc);
                auto tgt_st = std::dynamic_pointer_cast<struct_type>(tgt_sub_nc);
                bool is_static_upcast = src_st && tgt_st &&
                                 src_st->get_struct() && tgt_st->get_struct() &&
                                 src_st->get_struct()->is_derived_from(tgt_st->get_struct());
                bool is_dynamic_downcast = !is_static_upcast && src_st && tgt_st &&
                                 src_st->get_struct() && tgt_st->get_struct() &&
                                 tgt_st->get_struct()->is_derived_from(src_st->get_struct()) &&
                                 std::dynamic_pointer_cast<klass>(tgt_st->get_struct()) != nullptr;
                if (!is_static_upcast && !is_dynamic_downcast) {
                    throw_error(0x000B, std::nullopt,
                        "Pointer assignment type mismatch: "
                        "cannot assign a '{}' to a '{}'; pointer subtypes must match "
                        "or source must be a derived type of the target",
                        {source_type ? source_type->to_string() : "?",
                         target_type ? target_type->to_string() : "?"});
                }
                // Forbid const T* → T* (would lose const-ness on pointed object)
                if (type::is_const(src_sub) && !type::is_const(tgt_sub)) {
                    throw_error(0x0081, std::nullopt,
                        "Cannot assign a pointer-to-const ('{}') to a pointer-to-mutable ('{}'): "
                        "this would allow modification of a const object through the mutable pointer",
                        {source_type ? source_type->to_string() : "?",
                         target_type ? target_type->to_string() : "?"});
                }
                // Unwrap ref wrapper if needed
                if (type::is_reference(source_type)) {
                    right = load_value_expression::make_shared(right);
                    source_type = std::dynamic_pointer_cast<reference_type>(source_type)->get_subtype();
                    right->set_type(source_type);
                    expr.assign_right(right);
                }
                if (is_static_upcast) {
                    auto upcast = cast_expression::make_shared(right, target_type);
                    upcast->set_type(target_type);
                    expr.assign_right(upcast);
                } else {
                    // Dynamic downcast — ptr can be null, not fatal
                    auto dc = cast_expression::make_shared(right, target_type, /*null_is_fatal=*/false);
                    expr.assign_right(dc);
                }
                expr.set_type(ref_target_type);
                return;
            }
            // Forbid const T* → T* (would lose const-ness on pointed object)
            if (type::is_const(src_sub) && !type::is_const(tgt_sub)) {
                throw_error(0x0081, std::nullopt,
                    "Cannot assign a pointer-to-const ('{}') to a pointer-to-mutable ('{}'): "
                    "this would allow modification of a const object through the mutable pointer",
                    {source_type ? source_type->to_string() : "?",
                     target_type ? target_type->to_string() : "?"});
            }
            if (type::is_reference(source_type)) {
                right = load_value_expression::make_shared(right);
                source_type = std::dynamic_pointer_cast<reference_type>(source_type)->get_subtype();
                right->set_type(source_type);
                expr.assign_right(right);
            }
            expr.set_type(ref_target_type);
            return;
        } else {
            throw_error(0x000C, std::nullopt,
                "Pointer assignment requires a pointer or link on the right-hand side: "
                "cannot assign a value of type '{}' to a pointer of type '{}'",
                {source_type ? source_type->to_string() : "?",
                 target_type ? target_type->to_string() : "?"});
        }
    } else if (type::is_link(target_type)) {
        // Direct link rebind (reached after link-to-link case not matched above).
        if (!type::is_any_indirection(effective_source_type)) {
            throw_error(0x0071, std::nullopt,
                "Link assignment requires an indirection on the right-hand side, "
                "but got type '{}'",
                {source_type ? source_type->to_string() : "?"});
        }
        if (type::is_nullable_indirection(effective_source_type)) {
            auto diag = k::log::diagnostic::make_warning(with_flag(0x0072),
                "Assigning a nullable indirection (type '{}') to a link: "
                "a runtime null-check will be inserted",
                {source_type ? source_type->to_string() : "?"});
            logger_relay::report(diag);
        }
        if (type::is_reference(source_type)) {
            right = load_value_expression::make_shared(right);
            source_type = std::dynamic_pointer_cast<reference_type>(source_type)->get_subtype();
            right->set_type(source_type);
            expr.assign_right(right);
        }
        expr.set_type(ref_target_type);
        return;
    } else if (type::is_sized_array(target_type)) {
        // Array = array : element-wise copy (see spec).
        // Source must be a reference to a sized array of the same element type.
        auto dest_arr = std::dynamic_pointer_cast<sized_array_type>(target_type);
        std::shared_ptr<type> src_inner_type = source_type;
        if (type::is_reference(source_type)) {
            src_inner_type = std::dynamic_pointer_cast<reference_type>(source_type)->get_subtype();
        }
        if (!type::is_sized_array(src_inner_type)) {
            throw_error(0x0060, std::nullopt,
                "Array assignment: the right-hand side must be an array of the same element type, "
                "but '{}' is not a sized array",
                {source_type ? source_type->to_string() : "?"});
        }
        auto src_arr = std::dynamic_pointer_cast<sized_array_type>(src_inner_type);
        if (!type::are_equal(dest_arr->get_subtype(), src_arr->get_subtype())) {
            throw_error(0x0061, std::nullopt,
                "Array assignment: element type mismatch — cannot copy from '{}' to '{}'",
                {source_type ? source_type->to_string() : "?",
                 target_type ? target_type->to_string() : "?"});
        }
        // Type of the assignment expression is ref<dest array>
        expr.set_type(ref_target_type);
        // Ensure the source is referenced (if it isn't already)
        if (!type::is_reference(source_type)) {
            right = load_value_expression::make_shared(right);
            right->set_type(source_type->get_reference());
            expr.assign_right(right);
        }
        return; // code generation handled in visit_simple_assignation_expression
    } else if (type::is_function_reference(target_type)) {
        // Assigning a function address (or another frt variable) to a function-pointer variable.
        // target_type is a function_reference_type; source should be ref<frt> (function symbol)
        // or frt itself (another variable). The ref wrapper is stripped below if present.
        //
        // Check: only pointer (*) frt is rebindable; pin (^) and link (~) are immutable.
        auto frt_target = std::dynamic_pointer_cast<function_reference_type>(target_type);
        if (frt_target && frt_target->get_ref_kind() != function_reference_type::ref_kind::pointer) {
            throw_error(0x0090, std::nullopt,
                "Cannot assign to an immutable function reference (type '{}'): "
                "only pointer (*) function references are rebindable",
                {target_type ? target_type->to_string() : "?"});
        }
        // Unwrap ref<frt> on the source side if needed.
        // For a direct function symbol (is_function()), impl_gen returns the Function* directly —
        // no load needed. For a frt variable, impl_gen returns the alloca address — needs a load.
        if (type::is_reference(source_type)) {
            auto inner = std::dynamic_pointer_cast<reference_type>(source_type)->get_subtype();
            if (type::is_function_reference(inner)) {
                auto rhs_sym = std::dynamic_pointer_cast<symbol_expression>(right);
                if (!rhs_sym || !rhs_sym->is_function()) {
                    // Variable of frt type: load the stored function pointer from the alloca
                    right = load_value_expression::make_shared(right);
                    source_type = inner;
                    right->set_type(source_type);
                    expr.assign_right(right);
                }
                // else: direct function symbol → keep ref<frt>; impl_gen produces Function* directly
            }
        }
        expr.set_type(ref_target_type);
        return;
    } else if (type::is_owner(target_type)) {
        // ── Owner assignment: destroy old object (if any), transfer ownership ─────
        //   - null literal    → destroy current + set null
        //   - ref<owner<T>>  → move (load + null source), same or compatible subtype
        //   - owner<T>       → direct (from new_expression or already an owner value)

        // Detect null literal (both parsed 'null' and programmatic nullptr)
        bool rhs_is_null = type::is_null(source_type);
        if (!rhs_is_null) {
            if (auto ve = std::dynamic_pointer_cast<value_expression>(right)) {
                if (ve->is_literal() && ve->any_literal().has_value()) {
                    rhs_is_null = std::holds_alternative<lex::null>(ve->any_literal());
                } else {
                    rhs_is_null = std::holds_alternative<std::nullptr_t>(ve->get_value());
                }
            }
        }
        if (rhs_is_null) {
            // Assign null: destroy current owned object and store null
            expr.set_type(ref_target_type);
            return;
        }

        if (type::is_reference(source_type)) {
            auto inner = std::dynamic_pointer_cast<reference_type>(source_type)->get_subtype();
            if (type::is_owner(inner)) {
                // Move: wrap source in owner_move_expression (load + null source alloca)
                auto own_src_nc = type::remove_const(inner->get_subtype());
                auto own_tgt_nc = type::remove_const(target_type->get_subtype());
                auto move = owner_move_expression::make_shared(right);
                move->set_type(inner);
                std::shared_ptr<expression> new_right = move;
                if (!type::are_equal(own_src_nc, own_tgt_nc)) {
                    // Check upcast: owner<Derived> → owner<Base>
                    auto src_st = std::dynamic_pointer_cast<struct_type>(own_src_nc);
                    auto tgt_st = std::dynamic_pointer_cast<struct_type>(own_tgt_nc);
                    if (!src_st || !tgt_st || !src_st->get_struct() || !tgt_st->get_struct() ||
                        !src_st->get_struct()->is_derived_from(tgt_st->get_struct())) {
                        throw_error(0x00A1, std::nullopt,
                            "Owner assignment type mismatch: cannot move '{}' into '{}'",
                            {source_type->to_string(), target_type->to_string()});
                    }
                    auto upcast = cast_expression::make_shared(move, target_type);
                    upcast->set_type(target_type);
                    new_right = upcast;
                }
                expr.assign_right(new_right);
                expr.set_type(ref_target_type);
                return;
            }
        }
        if (type::is_owner(source_type)) {
            // Direct owner value (e.g. from new_expression or already an owner_move_expression)
            expr.set_type(ref_target_type);
            return;
        }
        throw_error(0x00A0, std::nullopt,
            "Owner assignment: right-hand side must be an owner value, "
            "another owner variable (move), or null; got type '{}'",
            {source_type ? source_type->to_string() : "?"});
    } else if(!type::is_primitive(target_type) && !type::is_struct(target_type)) {
        throw_error(0x000D, std::nullopt,
            "Assignment to a non-primitive, non-pointer type is not yet supported: "
            "the target has type '{}'; only assignments to primitive types, pointers and arrays are supported",
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

    // left is a pointer to the storage.
    // Determine what the target type really is after one level of ref-unwrap.
    auto expr_left_type = expr.left()->get_type();
    auto left_ref_type  = std::dynamic_pointer_cast<reference_type>(expr_left_type);
    auto target_type    = left_ref_type ? left_ref_type->get_subtype() : nullptr;

    // If target is ref-to-ref, unwrap one more level (variable access pattern).
    if (target_type && type::is_reference(target_type)) {
        target_type = std::dynamic_pointer_cast<reference_type>(target_type)->get_subtype();
    }

    // ------------------------------------------------------------------
    // Array assignment: element-wise copy (spec: partial copy, no resize)
    // ------------------------------------------------------------------
    if (target_type && type::is_sized_array(target_type)) {
        auto dest_arr  = std::dynamic_pointer_cast<sized_array_type>(target_type);
        auto* struct_llvm    = dest_arr->get_llvm_struct_type();
        auto* data_arr_llvm  = dest_arr->get_llvm_data_array_type();
        auto* elem_llvm      = _context->get_llvm_type(dest_arr->get_subtype());
        auto  dest_n         = static_cast<uint64_t>(dest_arr->get_size());

        // right is the pointer to the source struct { i32, [N x T] }
        auto* i32_t = llvm::Type::getInt32Ty(_builder->getContext());

        // Source capacity (runtime value from field 0)
        llvm::Value* src_size_ptr = _builder->CreateStructGEP(struct_llvm, right,
            sized_array_type::FIELD_SIZE, "src_sz_ptr");
        llvm::Value* src_n = _builder->CreateLoad(i32_t, src_size_ptr, "src_n");

        // Data pointers
        llvm::Value* src_data  = _builder->CreateStructGEP(struct_llvm, right,
            sized_array_type::FIELD_DATA, "src_data");
        llvm::Value* dest_data = _builder->CreateStructGEP(struct_llvm, left,
            sized_array_type::FIELD_DATA, "dst_data");

        // copy_n = min(dest_n, src_n)
        auto* dest_n_val = llvm::ConstantInt::get(i32_t, dest_n, false);
        llvm::Value* copy_n = _builder->CreateSelect(
            _builder->CreateICmpULT(src_n, dest_n_val), src_n, dest_n_val, "copy_n");

        // Emit copy loop
        auto* fn = _builder->GetInsertBlock()->getParent();
        auto* pre_bb   = _builder->GetInsertBlock();
        auto* loop_bb  = llvm::BasicBlock::Create(_builder->getContext(), "arr_asgn_loop", fn);
        auto* done_bb  = llvm::BasicBlock::Create(_builder->getContext(), "arr_asgn_done", fn);

        _builder->CreateCondBr(
            _builder->CreateICmpUGT(copy_n, llvm::ConstantInt::get(i32_t, 0, false)),
            loop_bb, done_bb);

        _builder->SetInsertPoint(loop_bb);
        auto* idx = _builder->CreatePHI(i32_t, 2, "asgn_idx");
        idx->addIncoming(llvm::ConstantInt::get(i32_t, 0, false), pre_bb);

        llvm::Value* s = _builder->CreateGEP(data_arr_llvm, src_data,
            {_builder->getInt32(0), idx}, "s_elem");
        llvm::Value* d = _builder->CreateGEP(data_arr_llvm, dest_data,
            {_builder->getInt32(0), idx}, "d_elem");
        _builder->CreateStore(_builder->CreateLoad(elem_llvm, s, "ev"), d);

        auto* nxt = _builder->CreateAdd(idx, llvm::ConstantInt::get(i32_t, 1), "nxt");
        idx->addIncoming(nxt, loop_bb);
        _builder->CreateCondBr(_builder->CreateICmpULT(nxt, copy_n), loop_bb, done_bb);

        _builder->SetInsertPoint(done_bb);
        _value = left;
        return;
    }

    // ------------------------------------------------------------------
    // Owner assignment: delete old object (if any), store new pointer
    // ------------------------------------------------------------------
    if (target_type && type::is_owner(target_type)) {
        auto& llvm_ctx = _builder->getContext();
        auto* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);

        // If non-null, destroy + free the existing object (don't null-out, we're about to store the new value)
        auto own_type = std::dynamic_pointer_cast<owner_type>(target_type);
        emit_owner_cleanup_if_nonnull(_builder.get(), get_module(), _context->_functions,
            left, own_type->get_owned_type(), "owner_asgn", /*null_out=*/false);

        // Determine the new pointer value to store:
        // - right may be null (from null literal → visit_value_expression returns nullptr LLVM val)
        // - right may be an owner ptr (from owner_move_expression)
        llvm::Value* new_ptr = right;
        if (!new_ptr) {
            // null literal case
            new_ptr = llvm::ConstantPointerNull::get(ptr_ty);
        }
        _builder->CreateStore(new_ptr, left);
        _value = left;
        return;
    }

    // ------------------------------------------------------------------
    // Scalar / pointer assignment (existing behaviour)
    // ------------------------------------------------------------------

    // Link rebind from nullable source: emit null-check before store.
    // (The resolver emits warning 0x0072 at compile-time; we add the runtime guard here.)
    if (target_type && type::is_link(target_type)) {
        // Pierce cast_expression to find the real source nullability
        auto rhs_model = expr.right();
        auto rhs_type = rhs_model ? rhs_model->get_type() : nullptr;
        // Also check original type through a cast (upcast Derived→Base wraps nullable ptr)
        if (auto cast_e = std::dynamic_pointer_cast<cast_expression>(rhs_model)) {
            auto inner_type = cast_e->sub_expr()->get_type();
            if (inner_type && type::is_nullable_indirection(inner_type)) {
                rhs_type = inner_type;
            }
        }
        if (rhs_type && type::is_nullable_indirection(rhs_type)) {
            auto* fatal = get_or_declare_fatal_null_function("__fatal_null_assignation");
            emit_null_check(right, fatal, "link_rebind");
        }
    }

    _value = right;
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

    if(type::is_const(value_type)) {
        throw_error(0x0083, std::nullopt,
            "Cannot apply prefix '++' to a const variable of type '{}'",
            {value_type ? value_type->to_string() : "?"});
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

    if(type::is_const(value_type)) {
        throw_error(0x0084, std::nullopt,
            "Cannot apply prefix '--' to a const variable of type '{}'",
            {value_type ? value_type->to_string() : "?"});
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

    if(type::is_const(value_type)) {
        throw_error(0x0085, std::nullopt,
            "Cannot apply postfix '++' to a const variable of type '{}'",
            {value_type ? value_type->to_string() : "?"});
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

    if(type::is_const(value_type)) {
        throw_error(0x0086, std::nullopt,
            "Cannot apply postfix '--' to a const variable of type '{}'",
            {value_type ? value_type->to_string() : "?"});
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

    // Helper: is the type boolean-compatible? (primitive or indirection/null → bool via adapt_type)
    auto is_bool_compatible = [](const std::shared_ptr<type>& t) {
        if (type::is_primitive(t)) return true;
        if (type::is_pointer(t) || type::is_link(t) || type::is_pinned(t)
            || type::is_owner(t) || type::is_null(t)) return true;
        // Also accept ref<indirection>
        if (type::is_reference(t)) {
            auto inner = t->get_subtype();
            if (type::is_pointer(inner) || type::is_link(inner) ||
                type::is_pinned(inner) || type::is_owner(inner)) return true;
        }
        return false;
    };

    if(type::is_reference(left_type)) {
        // For ref<indirection>, don't unwrap — adapt_type handles ref<indirection>→bool.
        auto inner = left_type->get_subtype();
        if (type::is_pointer(inner) || type::is_link(inner) || type::is_pinned(inner)
            || type::is_owner(inner)) {
            // Leave as-is; adapt_type will handle ref<indirection>→bool.
        } else {
            left = adapt_reference_load_value(left);
            expr.assign_left(left);
            left_type = left_type->get_subtype();
        }
    }

    if(type::is_reference(right_type)) {
        auto inner = right_type->get_subtype();
        if (type::is_pointer(inner) || type::is_link(inner) || type::is_pinned(inner)
            || type::is_owner(inner)) {
            // Leave as-is; adapt_type will handle ref<indirection>→bool.
        } else {
            right = adapt_reference_load_value(right);
            expr.assign_right(right);
            right_type = right_type->get_subtype();
        }
    }

    if(!is_bool_compatible(left->get_type()) || !is_bool_compatible(right->get_type())) {
        throw_error(0x0024, std::nullopt,
            "Logical operators ('&&', '||') are not supported for non-primitive types: "
            "operands must be of a primitive type or indirection type convertible to boolean, "
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
    if (type::is_reference(expr.left()->get_type())) {
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
    if (type::is_reference(expr.left()->get_type())) {
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

void type_reference_resolver::visit_logical_not_expression(logical_not_expression& expr) {
    visit_unary_expression(expr);

    auto& sub = expr.sub_expr();
    auto type = sub->get_type();

    if(type::is_reference(type)) {
        // For ref<indirection>, don't unwrap — adapt_type handles it.
        auto inner = type->get_subtype();
        if (!type::is_pointer(inner) && !type::is_link(inner) &&
            !type::is_pinned(inner) && !type::is_owner(inner)) {
            type = type->get_subtype();
        }
    }

    // Check bool-compatibility: primitive, indirection, or null.
    auto is_bool_compatible = [](const std::shared_ptr<k::model::type>& t) {
        if (type::is_primitive(t)) return true;
        if (type::is_pointer(t) || type::is_link(t) || type::is_pinned(t)
            || type::is_owner(t) || type::is_null(t)) return true;
        // Also accept ref<indirection>
        if (type::is_reference(t)) {
            auto inner = t->get_subtype();
            if (type::is_pointer(inner) || type::is_link(inner) ||
                type::is_pinned(inner) || type::is_owner(inner)) return true;
        }
        return false;
    };
    if(!is_bool_compatible(type)) {
        throw_error(0x0029, std::nullopt,
            "Logical NOT ('!') is not supported for non-primitive types: "
            "the operand has type '{}'; only primitive or indirection types convertible to boolean are supported",
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

    // ── Helper: is this type address-comparable? ─────────────────────────────
    // Pointer, link, pinned, owner, and the null literal type can all participate
    // in address equality/inequality comparisons.
    auto is_address_comparable = [](const std::shared_ptr<type>& t) -> bool {
        return type::is_pointer(t) || type::is_link(t) || type::is_pinned(t)
            || type::is_owner(t)   || type::is_null(t);
    };

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

    unwrap_ref_indirection(left, left_type);
    unwrap_ref_indirection(right, right_type);

    // ── Address comparison path ──────────────────────────────────────────────
    if (is_address_comparable(left_type) || is_address_comparable(right_type)) {
        // Both sides must be address-comparable.
        if (!is_address_comparable(left_type) || !is_address_comparable(right_type)) {
            throw_error(0x002C, std::nullopt,
                "Address comparison requires both operands to be indirections "
                "(pointer, link, pinned, owner) or null, but found '{}' and '{}'",
                {left_type ? left_type->to_string() : "?",
                 right_type ? right_type->to_string() : "?"});
        }
        // Only == and != are valid for address comparison (not <, >, <=, >=).
        if (!dynamic_cast<equal_expression*>(&expr) &&
            !dynamic_cast<different_expression*>(&expr)) {
            throw_error(0x002E, std::nullopt,
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

    // ── Primitive comparison (existing path) ─────────────────────────────────
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
                throw_error(0x0085, std::nullopt,
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

    auto left_type = expr.left()->get_type();
    auto right_type = expr.right()->get_type();

    // ── Address comparison for indirection types ─────────────────────────────
    auto is_addr = [](const std::shared_ptr<type>& t) {
        return type::is_pointer(t) || type::is_link(t) || type::is_pinned(t)
            || type::is_owner(t)   || type::is_null(t);
    };
    if (is_addr(left_type) || is_addr(right_type)) {
        // Both are pointer-sized values; null is ConstantPointerNull.
        // Ensure both are ptr-typed for ICmpEQ.
        auto* ptr_ty = llvm::PointerType::get(_builder->getContext(), 0);
        if (left->getType() != ptr_ty) left = _builder->CreateBitCast(left, ptr_ty);
        if (right->getType() != ptr_ty) right = _builder->CreateBitCast(right, ptr_ty);
        _value = _builder->CreateICmpEQ(left, right);
        return;
    }
    // ─────────────────────────────────────────────────────────────────────────

    // If operands are references, dereference them.
    llvm::Type* type = _context->get_llvm_type(expr.get_type());
    if(type::is_reference(left_type)) {
        left = _builder->CreateLoad(type, left);
    }
    if(type::is_reference(right_type)) {
        right = _builder->CreateLoad(type, right);
    }

    if(!type::is_primitive(left_type) || !type::is_primitive(right_type)) {
        throw_internal_error(0x002A, std::nullopt,
            "Internal error: '==' operator has a non-primitive operand during code generation; "
            "this should have been rejected during type resolution");
    }

    // For primitives, operand types are supposed to be aligned
    static auto bool_type = _context->from_type(primitive_type::BOOL);
    auto prim = std::dynamic_pointer_cast<primitive_type>(left_type);

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

    auto left_type = expr.left()->get_type();
    auto right_type = expr.right()->get_type();

    // ── Address comparison for indirection types ─────────────────────────────
    auto is_addr = [](const std::shared_ptr<type>& t) {
        return type::is_pointer(t) || type::is_link(t) || type::is_pinned(t)
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

    // If operands are references, dereference them.
    llvm::Type* type = _context->get_llvm_type(expr.get_type());
    if(type::is_reference(left_type)) {
        left = _builder->CreateLoad(type, left);
    }
    if(type::is_reference(right_type)) {
        right = _builder->CreateLoad(type, right);
    }

    if(!type::is_primitive(left_type) || !type::is_primitive(right_type)) {
        throw_internal_error(0x002C, std::nullopt,
            "Internal error: '!=' operator has non-primitive operand during code generation; "
            "this should have been rejected during type resolution");
    }

    // For primitives, operand types are supposed to be aligned
    static auto bool_type = _context->from_type(primitive_type::BOOL);
    auto prim = std::dynamic_pointer_cast<primitive_type>(left_type);

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
