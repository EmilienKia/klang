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
#include "../model/constant_evaluator.hpp"
#include "gen_operators_helpers.hpp"

namespace k::model::gen {

void type_reference_resolver::visit_arithmetic_unary_expression(arithmetic_unary_expression& expr) {
    visit_unary_expression(expr);

    auto& sub = expr.sub_expr();
    auto type = sub->get_type();

    if(type::is_pointer(type)) {
        throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_SUBSCRIPT_INDEX_TYPE), expr.first_lexeme(),
            "Unary arithmetic operators cannot be applied to pointer types: "
            "the operand has type '{}'; only numeric primitive types are supported",
            {type ? type->to_string() : "?"});
    }

    auto orig_type = type;
    if(type::is_reference(type)) {
        // Dereference type, if needed
        type = type->get_subtype();
    }

    // ── Operator overload for aggregate types ──
    bool is_const_operand = type::is_const(type);
    auto check_type = type::remove_const(type);
    if(type::is_struct(check_type)) {
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
                        expr.set_type(type);
                    }
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

    if(!type::is_primitive(type)) {
        throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_INVOKE_NOT_CALLABLE), expr.first_lexeme(),
            "Unary arithmetic operators are not supported for non-primitive types: "
            "the operand has type '{}'; only numeric primitive types are supported",
            {type ? type->to_string() : "?"});
    }

    expr.set_type(type);

    if (!expr.has_operator_overload() && expr.sub_expr()->is_constant()) {
        unary_op op = unary_op::PLUS;
        if (dynamic_cast<unary_minus_expression*>(&expr)) op = unary_op::MINUS;
        else if (dynamic_cast<unary_plus_expression*>(&expr)) op = unary_op::PLUS;
        else if (dynamic_cast<bitwise_not_expression*>(&expr)) op = unary_op::BITWISE_NOT;

        auto res = constant_evaluator::eval_unary(op, expr.sub_expr()->get_constant_value(), expr.get_type());
        if (res) {
            expr.set_constant_value(*res);
        }
    }
}

//
// Prefix increment expression (++expr)
//

void type_reference_resolver::visit_prefix_increment_expression(prefix_increment_expression& expr) {
    visit_unary_expression(expr);

    auto& sub = expr.sub_expr();
    auto type = sub->get_type();

    if(!type::is_reference(type)) {
        throw_error(static_cast<unsigned int>(k::diag::function_diag::ERR_FUNC_CTOR_ACCESS_DENIED), expr.first_lexeme(),
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
        throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_OVERLOAD_RETURN_TYPE_MISMATCH), expr.first_lexeme(),
            "Cannot apply prefix '++' to a const variable of type '{}'",
            {value_type ? value_type->to_string() : "?"});
    }

    // ── Operator overload for aggregate types ──
    auto check_type = type::remove_const(value_type);
    if(type::is_struct(check_type)) {
        auto st_type = std::dynamic_pointer_cast<struct_type>(check_type);
        if (st_type) {
            auto agg = st_type->get_struct();
            if (agg) {
                auto op_func = resolve_unary_operator_overload(expr, agg, sub);
                if (op_func) {
                    expr.set_operator_func(op_func);
                    if (op_func->has_return_type()) {
                        expr.set_type(op_func->get_return_type());
                    } else {
                        expr.set_type(ref_type);
                    }
                    if (op_func->is_member()) {
                        auto di = compute_operator_dispatch_info(op_func, type);
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

    if(!type::is_primitive(value_type)) {
        throw_error(static_cast<unsigned int>(k::diag::function_diag::ERR_FUNC_CTOR_VISIBILITY_MISMATCH), expr.first_lexeme(),
            "Prefix '++' requires a numeric primitive operand, but got type '{}'",
            {value_type ? value_type->to_string() : "?"});
    }
    if(type::is_prim_bool(value_type)) {
        throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_MEMBER_DEREF_NOT_POINTER), expr.first_lexeme(),
            "Prefix '++' cannot be applied to a boolean operand");
    }

    // Prefix increment returns a reference to the (now updated) variable
    expr.set_type(ref_type);
}

void implementation_generator::visit_prefix_increment_expression(prefix_increment_expression& expr) {
    if (generate_unary_operator_overload(expr)) return;

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
        throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_MEMBER_FUNC_NO_MATCH), expr.first_lexeme(),
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
        throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_OVERLOAD_CONST_MISMATCH), expr.first_lexeme(),
            "Cannot apply prefix '--' to a const variable of type '{}'",
            {value_type ? value_type->to_string() : "?"});
    }

    // ── Operator overload for aggregate types ──
    auto check_type = type::remove_const(value_type);
    if(type::is_struct(check_type)) {
        auto st_type = std::dynamic_pointer_cast<struct_type>(check_type);
        if (st_type) {
            auto agg = st_type->get_struct();
            if (agg) {
                auto op_func = resolve_unary_operator_overload(expr, agg, sub);
                if (op_func) {
                    expr.set_operator_func(op_func);
                    if (op_func->has_return_type()) {
                        expr.set_type(op_func->get_return_type());
                    } else {
                        expr.set_type(ref_type);
                    }
                    if (op_func->is_member()) {
                        auto di = compute_operator_dispatch_info(op_func, type);
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

    if(!type::is_primitive(value_type)) {
        throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_CAST_INCOMPATIBLE), expr.first_lexeme(),
            "Prefix '--' requires a numeric primitive operand, but got type '{}'",
            {value_type ? value_type->to_string() : "?"});
    }
    if(type::is_prim_bool(value_type)) {
        throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_CAST_UNSUPPORTED), expr.first_lexeme(),
            "Prefix '--' cannot be applied to a boolean operand");
    }

    // Prefix decrement returns a reference to the (now updated) variable
    expr.set_type(ref_type);
}

void implementation_generator::visit_prefix_decrement_expression(prefix_decrement_expression& expr) {
    if (generate_unary_operator_overload(expr)) return;

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
        throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_CAST_OPERATOR_NOT_FOUND), expr.first_lexeme(),
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
        throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_OVERLOAD_VISIBILITY_DENIED), expr.first_lexeme(),
            "Cannot apply postfix '++' to a const variable of type '{}'",
            {value_type ? value_type->to_string() : "?"});
    }

    // ── Operator overload for aggregate types ──
    auto check_type = type::remove_const(value_type);
    if(type::is_struct(check_type)) {
        auto st_type = std::dynamic_pointer_cast<struct_type>(check_type);
        if (st_type) {
            auto agg = st_type->get_struct();
            if (agg) {
                auto op_func = resolve_unary_operator_overload(expr, agg, sub);
                if (op_func) {
                    expr.set_operator_func(op_func);
                    if (op_func->has_return_type()) {
                        expr.set_type(op_func->get_return_type());
                    } else {
                        expr.set_type(value_type);
                    }
                    if (op_func->is_member()) {
                        auto di = compute_operator_dispatch_info(op_func, type);
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

    if(!type::is_primitive(value_type)) {
        throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_PREINC_NOT_REF), expr.first_lexeme(),
            "Postfix '++' requires a numeric primitive operand, but got type '{}'",
            {value_type ? value_type->to_string() : "?"});
    }
    if(type::is_prim_bool(value_type)) {
        throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_PREINC_NOT_NUMERIC), expr.first_lexeme(),
            "Postfix '++' cannot be applied to a boolean operand");
    }

    // Postfix increment returns the old value (not a reference)
    expr.set_type(value_type);
}

void implementation_generator::visit_postfix_increment_expression(postfix_increment_expression& expr) {
    if (generate_unary_operator_overload(expr)) return;

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
        throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_PREDEC_NOT_REF), expr.first_lexeme(),
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
        throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_OVERLOAD_NOT_FOUND), expr.first_lexeme(),
            "Cannot apply postfix '--' to a const variable of type '{}'",
            {value_type ? value_type->to_string() : "?"});
    }

    // ── Operator overload for aggregate types ──
    auto check_type = type::remove_const(value_type);
    if(type::is_struct(check_type)) {
        auto st_type = std::dynamic_pointer_cast<struct_type>(check_type);
        if (st_type) {
            auto agg = st_type->get_struct();
            if (agg) {
                auto op_func = resolve_unary_operator_overload(expr, agg, sub);
                if (op_func) {
                    expr.set_operator_func(op_func);
                    if (op_func->has_return_type()) {
                        expr.set_type(op_func->get_return_type());
                    } else {
                        expr.set_type(value_type);
                    }
                    if (op_func->is_member()) {
                        auto di = compute_operator_dispatch_info(op_func, type);
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

    if(!type::is_primitive(value_type)) {
        throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_POSTINC_NOT_REF), expr.first_lexeme(),
            "Postfix '--' requires a numeric primitive operand, but got type '{}'",
            {value_type ? value_type->to_string() : "?"});
    }
    if(type::is_prim_bool(value_type)) {
        throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_POSTINC_NOT_NUMERIC), expr.first_lexeme(),
            "Postfix '--' cannot be applied to a boolean operand");
    }

    // Postfix decrement returns the old value (not a reference)
    expr.set_type(value_type);
}

void implementation_generator::visit_postfix_decrement_expression(postfix_decrement_expression& expr) {
    if (generate_unary_operator_overload(expr)) return;

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
    if (generate_unary_operator_overload(expr)) return;

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
    if (generate_unary_operator_overload(expr)) return;

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

    if(auto prim = std::dynamic_pointer_cast<primitive_type>(type::remove_const(type))) {
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
    if (generate_unary_operator_overload(expr)) return;

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

    if(auto prim = std::dynamic_pointer_cast<primitive_type>(type::remove_const(type))) {
        // When primitive, return the value itself
        if(prim->is_integer_or_bool()) {
            _value = _builder->CreateNot(val);
        } else if(prim->is_float()) {
            throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_INVOKE_ARG_TYPE_MISMATCH), expr.first_lexeme(),
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

} // namespace k::model::gen
