/*
 * K Language compiler
 *
 * Copyright 2023 Emilien Kia
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
#include "symbol_type_resolver.hpp"
#include "unit_llvm_ir_gen.hpp"

namespace k::model::gen {

//
// Value expression
//

void symbol_type_resolver::visit_value_expression(value_expression& expr)
{
    // Nothing to view here:
    // - No symbol to resolve
    // - Type is supposed to have already been set at value_expression construction.
    // See value_expression::type_from_literal(const k::lex::any_literal& literal)
}

void unit_llvm_ir_gen::visit_value_expression(value_expression &expr) {
    if(expr.is_literal()) {
        auto lit = expr.any_literal();
        switch(lit.index()) {
            case lex::any_literal_type_index::INTEGER: {
                const auto &i = lit.get<lex::integer>();
                auto val = llvm::APInt((unsigned) i.size, i.int_content(), (uint8_t) i.base);
                _value = llvm::ConstantInt::get(*_context, val);
            } break;
            case lex::any_literal_type_index::FLOAT_NUM: {
                const auto &f = lit.get<lex::float_num>();
                llvm::Type* type = f.size==lex::DOUBLE ? _builder->getDoubleTy() : _builder->getFloatTy() ;
                llvm::APFloat val(type->getScalarType()->getFltSemantics(), f.float_content());
                _value = llvm::ConstantFP::get(type, val);
            } break;
            case lex::any_literal_type_index::CHARACTER:
                // TODO
                break;
            case lex::any_literal_type_index::STRING:
                // TODO
                break;
            case lex::any_literal_type_index::BOOLEAN: {
                const auto& b = lit.get<lex::boolean>();
                if(std::get<bool>(b.value())) {
                    _value = _builder->getTrue();
                } else {
                    _value = _builder->getFalse();
                }
            } break;
            case lex::any_literal_type_index::NUL:
                // TODO
                break;
            default:
                break;
        }
    } else {

    }
}

//
// Symbol expression
//

void symbol_type_resolver::visit_symbol_expression(symbol_expression& symbol)
{
    // TODO: Have to check the variable definition (for block-variable) is done before the expression;
    // TODO: Have to support all symbol types, not only variables
    if(!symbol.is_resolved()) {
        if(auto stmt = symbol.find_statement()) {
            if(auto var_holder = stmt->get_variable_holder()) {
                if(auto def = var_holder->lookup_variable(symbol.get_name())) {
                    symbol.resolve(def);
                    // Note: type is supposed to be applied at resolution
                }
            }
        }
    }
}

void unit_llvm_ir_gen::visit_symbol_expression(symbol_expression &symbol) {
    if(symbol.is_variable_def()) {
        auto var_def = symbol.get_variable_def();
        llvm::Value* ptr = nullptr;
        std::string name;

        // Handle source of symbol
        if (auto param = std::dynamic_pointer_cast<parameter>(var_def)) {
            ptr =  _parameter_variables[param];
            name = param->get_name();
        } else if (auto global_var = std::dynamic_pointer_cast<global_variable_definition>(var_def)) {
            ptr = _global_vars[global_var];
            name = global_var->get_name();
        } else if (auto local_var = std::dynamic_pointer_cast<variable_statement>(var_def)) {
            ptr = _variables[local_var];
            name = local_var->get_name();
        }

        // Handle type of symbol
        llvm::Type* type = get_llvm_type(var_def->get_type());

        if(ptr && type) {
            _value = _builder->CreateLoad(type, ptr, name);
        }

    } else {
        // TODO Support other types of symbols, not only variables
    }
}

//
// Unary expression
//

void symbol_type_resolver::visit_unary_expression(unary_expression& expr)
{
    auto& sub = expr.sub_expr();

    if(!sub) {
        // TODO throw an exception
        // Error 0x0002: unary expression must have non-null sub expresssion
        std::cerr << "Error: unary expression must have non-null sub expresssion" << std::endl;
    }

    sub->accept(*this);

    if(!type::is_resolved(sub->get_type())) {
        // TODO throw an exception
        // Error 0x0003: unary expression must have resolved type for its sub-expression
        std::cerr << "Error: unary expression must have resolved type for its sub-expression" << std::endl;
    }
}

llvm::Value* unit_llvm_ir_gen::process_unary_expression(unary_expression& expr) {
    llvm::Value* res = nullptr;
    _value = nullptr;
    expr.sub_expr()->accept(*this);
    res = _value;
    _value = nullptr;
    return res;
}

//
// Binary expression
//

void symbol_type_resolver::visit_binary_expression(binary_expression& expr)
{
    auto& left = expr.left();
    auto& right = expr.right();

    if(!left || !right) {
        // TODO throw an exception
        // Error 0x0004: binary expression must have non-null left and right expresssion
        std::cerr << "Error: binary expression must have non-null left and right expresssion" << std::endl;
    }

    left->accept(*this);
    right->accept(*this);

    if(!type::is_resolved(left->get_type())) {
        // TODO throw an exception
        // Error 0x0005: Error: left sub-expression of binary expression must have resolved type
        std::cerr << "Error: left sub-expression of binary expression must have resolved type" << std::endl;
    }
    if(!type::is_resolved(right->get_type())) {
        // TODO throw an exception
        // Error 0x0005b: Error: right sub-expression of binary expression must have resolved type
        std::cerr << "Error: right sub-expression of binary expression must have resolved type" << std::endl;
    }
}


std::pair<llvm::Value*,llvm::Value*> unit_llvm_ir_gen::process_binary_expression(binary_expression & expr) {
    std::pair<llvm::Value*,llvm::Value*> res;
    _value = nullptr;
    expr.left()->accept(*this);
    res.first = _value;
    _value = nullptr;
    expr.right()->accept(*this);
    res.second = _value;
    _value = nullptr;
    return res;
}

//
// Arithmetic binary expression
//

void symbol_type_resolver::process_arithmetic(binary_expression& expr) {
    // TODO Rework conversions and promotions
    visit_binary_expression(expr);

    auto& left = expr.left();
    auto& right = expr.right();

    auto type = left->get_type();
    if(!type::is_primitive(type)) {
        // TODO throw an exception
        // Arithmetic for non-primitive types is not supported.
        std::cerr << "Error: Arithmetic for non-primitive types is not supported yet." << std::endl;
    }
    if(type::is_prim_bool(type)) {
        // TODO throw an exception
        // Arithmetic for boolean is not supported.
        std::cerr << "Error: Arithmetic for boolean is not supported." << std::endl;
    }

    expr.set_type(left->get_type());
    // TODO Promote to largest type instread to align to left operand.
    auto cast = adapt_type(right, left->get_type());
    if(!cast) {
        // TODO throw an exception
        // Error: right type is not compatible (cannot be cast).
        std::cerr << "Error: binary arithmetic expression must have resolved type at left and right sub-expression" << std::endl;
    } else if(cast != right ) {
        // Casted, assign casted expression instead of right source.
        expr.assign_right(cast);
    } else {
        // Compatible type, no need to cast.
    }
}

void symbol_type_resolver::visit_arithmetic_binary_expression(arithmetic_binary_expression &expr) {
    process_arithmetic(expr);
}

//
// Addition expression (+)
//

void unit_llvm_ir_gen::visit_addition_expression(addition_expression &expr) {
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
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

void unit_llvm_ir_gen::visit_substraction_expression(substraction_expression &expr) {
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
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

void unit_llvm_ir_gen::visit_multiplication_expression(multiplication_expression &expr) {
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
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

void unit_llvm_ir_gen::visit_division_expression(division_expression &expr) {
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
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

void unit_llvm_ir_gen::visit_modulo_expression(modulo_expression &expr) {
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
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

void unit_llvm_ir_gen::visit_bitwise_and_expression(bitwise_and_expression& expr) {
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    if(auto prim = std::dynamic_pointer_cast<primitive_type>(expr.get_type())) {
        if(prim->is_integer()) {
            _value = _builder->CreateAnd(left, right);
        } else if(prim->is_float()) {
            // TODO throw an exception
            // Error : bitwise operations are not meaningful for float numbers, hence not supported.
            std::cerr << "Error : bitwise operations are not meaningful for float numbers, hence not supported." << std::endl;
        }
    } else {
        // TODO: Support other types
    }
}

//
// Bitwise or expression
//

void unit_llvm_ir_gen::visit_bitwise_or_expression(bitwise_or_expression& expr) {
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    if(auto prim = std::dynamic_pointer_cast<primitive_type>(expr.get_type())) {
        if(prim->is_integer()) {
            _value = _builder->CreateOr(left, right);
        } else if(prim->is_float()) {
            // TODO throw an exception
            // Error : bitwise operations are not meaningful for float numbers, hence not supported.
            std::cerr << "Error : bitwise operations are not meaningful for float numbers, hence not supported." << std::endl;
        }
    } else {
        // TODO: Support other types
    }
}

//
// Bitwise xor expression
//

void unit_llvm_ir_gen::visit_bitwise_xor_expression(bitwise_xor_expression& expr) {
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    if(auto prim = std::dynamic_pointer_cast<primitive_type>(expr.get_type())) {
        if(prim->is_integer()) {
            _value = _builder->CreateXor(left, right);
        } else if(prim->is_float()) {
            // TODO throw an exception
            // Error : bitwise operations are not meaningful for float numbers, hence not supported.
            std::cerr << "Error : bitwise operations are not meaningful for float numbers, hence not supported." << std::endl;
        }
    } else {
        // TODO: Support other types
    }
}

//
// Left shift expression
//

void unit_llvm_ir_gen::visit_left_shift_expression(left_shift_expression& expr) {
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    if(auto prim = std::dynamic_pointer_cast<primitive_type>(expr.get_type())) {
        if(prim->is_integer()) {
            // TODO may it poison when overflow ?
            _value = _builder->CreateShl(left, right);
        } else if(prim->is_float()) {
            // TODO throw an exception
            // Error : shifting operations are not meaningful for float numbers, hence not supported.
            std::cerr << "Error : shifting operations are not meaningful for float numbers, hence not supported." << std::endl;
        }
    } else {
        // TODO: Support other types
    }
}

//
// Right shift expression
//

void unit_llvm_ir_gen::visit_right_shift_expression(right_shift_expression& expr) {
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
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
            // TODO throw an exception
            // Error : shifting operations are not meaningful for float numbers, hence not supported.
            std::cerr << "Error : shifting operations are not meaningful for float numbers, hence not supported." << std::endl;
        }
    } else {
        // TODO: Support other types
    }
}

//
// Assignment expression
//

void unit_llvm_ir_gen::create_assignement(std::shared_ptr<expression> expr, llvm::Value* value) {
    auto var_expr = std::dynamic_pointer_cast<symbol_expression>(expr);
    if(!var_expr) {
        // TODO throw an exception
        // Only support direct variable expression as left operand
        std::cerr << "Assignation supports only direct variable assignment for now." << std::endl;
    }

    auto var = var_expr->get_variable_def();

    if(auto param = std::dynamic_pointer_cast<parameter>(var)) {
        _builder->CreateStore(value, _parameter_variables[param]);
    } else if (auto local_var = std::dynamic_pointer_cast<variable_statement>(var)) {
        _value = _builder->CreateStore(value, _variables[local_var]);
    } else if(auto global_var = std::dynamic_pointer_cast<global_variable_definition>(var)) {
        _value = _builder->CreateStore(value, _global_vars[global_var]);
    } else {
        std::cout << "Assign to something not supported" << std::endl;
    }
}

//
// Simple assignment expression (=)
//

void symbol_type_resolver::visit_assignation_expression(assignation_expression &expr) {
    process_arithmetic(expr);
}

void unit_llvm_ir_gen::visit_simple_assignation_expression(simple_assignation_expression& expr) {
    _value = nullptr;
    expr.right()->accept(*this);
    auto value = _value;
    _value = nullptr;
    if(!value) {
        // TODO throw an exception
        std::cerr << "No value on assignation." << std::endl;
    }

    create_assignement(expr.left(), value);
}

//
// Addition assignment expression (+=)
//

void unit_llvm_ir_gen::visit_addition_assignation_expression(additition_assignation_expression& expr) {
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    if(type::is_prim_integer(expr.get_type())) {
        _value = _builder->CreateAdd(left, right);
    } else if(type::is_prim_float(expr.get_type())) {
        _value = _builder->CreateFAdd(left, right);
    } else {
        // TODO: Support other types
    }
    create_assignement(expr.left(), _value);
}

//
// Substraction assignment expression (-=)
//

void unit_llvm_ir_gen::visit_substraction_assignation_expression(substraction_assignation_expression& expr) {
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    if(type::is_prim_integer(expr.get_type())) {
        _value = _builder->CreateSub(left, right);
    } else if(type::is_prim_float(expr.get_type())) {
        _value = _builder->CreateFSub(left, right);
    } else {
        // TODO: Support other types
    }
    create_assignement(expr.left(), _value);
}

//
// Multiplication assignment expression (*=)
//

void unit_llvm_ir_gen::visit_multiplication_assignation_expression(multiplication_assignation_expression& expr) {
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    if(type::is_prim_integer(expr.get_type())) {
        // TODO Should poison for int/uint multiplication overflow ?
        _value = _builder->CreateMul(left, right);
    } else if(type::is_prim_float(expr.get_type())) {
        _value = _builder->CreateFMul(left, right);
    } else {
        // TODO: Support other types
    }
    create_assignement(expr.left(), _value);
}

//
// Division assignment expression (/=)
//

void unit_llvm_ir_gen::visit_division_assignation_expression(division_assignation_expression& expr) {
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
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
    create_assignement(expr.left(), _value);
}

//
// Modulo assignment expression (%=)
//

void unit_llvm_ir_gen::visit_modulo_assignation_expression(modulo_assignation_expression& expr) {
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
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
    create_assignement(expr.left(), _value);
}

//
// Bitwise and assignment expression
//

void unit_llvm_ir_gen::visit_bitwise_and_assignation_expression(bitwise_and_assignation_expression& expr) {
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    if(auto prim = std::dynamic_pointer_cast<primitive_type>(expr.get_type())) {
        if(prim->is_integer()) {
            _value = _builder->CreateAnd(left, right);
        } else if(prim->is_float()) {
            // TODO throw an exception
            // Error : bitwise operations are not meaningful for float numbers, hence not supported.
            std::cerr << "Error : bitwise operations are not meaningful for float numbers, hence not supported." << std::endl;
        }
    } else {
        // TODO: Support other types
    }
    create_assignement(expr.left(), _value);
}

//
// Bitwise or assignment expression
//

void unit_llvm_ir_gen::visit_bitwise_or_assignation_expression(bitwise_or_assignation_expression& expr) {
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    if(auto prim = std::dynamic_pointer_cast<primitive_type>(expr.get_type())) {
        if(prim->is_integer()) {
            _value = _builder->CreateOr(left, right);
        } else if(prim->is_float()) {
            // TODO throw an exception
            // Error : bitwise operations are not meaningful for float numbers, hence not supported.
            std::cerr << "Error : bitwise operations are not meaningful for float numbers, hence not supported." << std::endl;
        }
    } else {
        // TODO: Support other types
    }
    create_assignement(expr.left(), _value);
}

//
// Bitwise xor assignment expression
//

void unit_llvm_ir_gen::visit_bitwise_xor_assignation_expression(bitwise_xor_assignation_expression& expr) {
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    if(auto prim = std::dynamic_pointer_cast<primitive_type>(expr.get_type())) {
        if(prim->is_integer()) {
            _value = _builder->CreateXor(left, right);
        } else if(prim->is_float()) {
            // TODO throw an exception
            // Error : bitwise operations are not meaningful for float numbers, hence not supported.
            std::cerr << "Error : bitwise operations are not meaningful for float numbers, hence not supported." << std::endl;
        }
    } else {
        // TODO: Support other types
    }
    create_assignement(expr.left(), _value);
}

//
// Left shift assignment expression
//

void unit_llvm_ir_gen::visit_left_shift_assignation_expression(left_shift_assignation_expression& expr) {
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    if(auto prim = std::dynamic_pointer_cast<primitive_type>(expr.get_type())) {
        if(prim->is_integer()) {
            // TODO may it poison when overflow ?
            _value = _builder->CreateShl(left, right);
        } else if(prim->is_float()) {
            // TODO throw an exception
            // Error : shifting operations are not meaningful for float numbers, hence not supported.
            std::cerr << "Error : shifting operations are not meaningful for float numbers, hence not supported." << std::endl;
        }
    } else {
        // TODO: Support other types
    }
    create_assignement(expr.left(), _value);
}

//
// Right shift assignment expression
//

void unit_llvm_ir_gen::visit_right_shift_assignation_expression(right_shift_assignation_expression& expr) {
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
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
            // TODO throw an exception
            // Error : shifting operations are not meaningful for float numbers, hence not supported.
            std::cerr << "Error : shifting operations are not meaningful for float numbers, hence not supported." << std::endl;
        }
    } else {
        // TODO: Support other types
    }
    create_assignement(expr.left(), _value);
}

//
// Arithmetic unary expression
//

void symbol_type_resolver::visit_arithmetic_unary_expression(arithmetic_unary_expression& expr) {
    visit_unary_expression(expr);

    auto& sub = expr.sub_expr();
    auto type = sub->get_type();

    if(!type::is_primitive(type)) {
        // TODO throw an exception
        // Arithmetic for non-primitive types is not supported.
        std::cerr << "Error: Arithmetic for non-primitive types is not supported yet." << std::endl;
    }

    expr.set_type(type);
}

//
// Unary plus expression
//

void unit_llvm_ir_gen::visit_unary_plus_expression(unary_plus_expression& expr) {
    auto val = process_unary_expression(expr);
    if(!val) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    if(type::is_primitive(expr.get_type())) {
        // When primitive, return the value itself
        _value = val;
    } else {
        // TODO: Support other types
    }
}

//
// Unary minus expression
//

void unit_llvm_ir_gen::visit_unary_minus_expression(unary_minus_expression& expr) {
    auto val = process_unary_expression(expr);
    if(!val) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    if(auto prim = std::dynamic_pointer_cast<primitive_type>(expr.get_type())) {
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

void unit_llvm_ir_gen::visit_bitwise_not_expression(bitwise_not_expression& expr) {
    auto val = process_unary_expression(expr);
    if(!val) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    if(auto prim = std::dynamic_pointer_cast<primitive_type>(expr.get_type())) {
        // When primitive, return the value itself
        if(prim->is_integer_or_bool()) {
            _value = _builder->CreateNot(val);
        } else if(prim->is_float()) {
            // TODO throw an exception
            // Error : bitwise operations are not meaningful for float numbers, hence not supported.
            std::cerr << "Error : bitwise operations are not meaningful for float numbers, hence not supported." << std::endl;
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

void symbol_type_resolver::visit_logical_binary_expression(logical_binary_expression& expr) {
    visit_binary_expression(expr);

    auto& left = expr.left();
    auto& right = expr.right();

    if(!type::is_primitive( left->get_type()) || !type::is_primitive(right->get_type())) {
        // TODO throw an exception
        // Logical for non-primitive types is not supported.
        std::cerr << "Error: Arithmetic for non-primitive types is not supported yet." << std::endl;
    }

    auto bool_type = primitive_type::from_type(primitive_type::BOOL);

    auto cast_left = adapt_type(left, bool_type);
    if(!cast_left) {
        // TODO throw an exception
        // Error: left type is not compatible (cannot be cast).
        std::cerr << "Error: Logical binary operand must be casted to boolean" << std::endl;
    } else if(cast_left != left ) {
        // Casted, assign casted expression instead of source.
        expr.assign_left(cast_left);
    } else {
        // Compatible type, no need to cast.
    }

    auto cast_right = adapt_type(right, bool_type);
    if(!cast_right) {
        // TODO throw an exception
        // Error: right type is not compatible (cannot be cast).
        std::cerr << "Error: Logical binary operand must be casted to boolean" << std::endl;
    } else if(cast_right != right ) {
        // Casted, assign casted expression instead of source.
        expr.assign_right(cast_right);
    } else {
        // Compatible type, no need to cast.
    }

    // For primitive type, logical is always returning boolean
    expr.set_type(primitive_type::from_type(primitive_type::BOOL));
}

//
// Logical and expression (&&)
//

void unit_llvm_ir_gen::visit_logical_and_expression(logical_and_expression& expr) {
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    if(!type::is_primitive(expr.left()->get_type()) || !type::is_primitive(expr.right()->get_type())) {
        // TODO throw an exception
        // Logical arithmetic for non-primitive types is not supported.
        std::cerr << "Error: Logical arithmetic for non-primitive types is not supported yet." << std::endl;
    }

    _value = _builder->CreateAnd(left, right);
}

//
// Logical or expression (||)
//

void unit_llvm_ir_gen::visit_logical_or_expression(logical_or_expression& expr) {
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    if(!type::is_primitive(expr.left()->get_type()) || !type::is_primitive(expr.right()->get_type())) {
        // TODO throw an exception
        // Logical arithmetic for non-primitive types is not supported.
        std::cerr << "Error: Logical arithmetic for non-primitive types is not supported yet." << std::endl;
    }

    _value = _builder->CreateOr(left, right);
}

//
// Logical not expression (!)
//

void symbol_type_resolver::visit_logical_not_expression(logical_not_expression& expr) {
    visit_unary_expression(expr);

    auto& sub = expr.sub_expr();
    auto type = sub->get_type();

    if(!type::is_primitive(type)) {
        // TODO throw an exception
        // Logical negation for non-primitive types is not supported.
        std::cerr << "Error: Logical negation for non-primitive types is not supported yet." << std::endl;
    }

    static auto bool_type = primitive_type::from_type(primitive_type::BOOL);
    auto cast = adapt_type(sub, bool_type);
    if(!cast) {
        // TODO throw an exception
        // Error: right type is not compatible (cannot be cast).
        std::cerr << "Error: Logical negation operand must be casted to boolean" << std::endl;
    } else if(cast != sub ) {
        // Casted, assign casted expression instead of source.
        expr.assign(cast);
    } else {
        // Compatible type, no need to cast.
    }

    // For primitive type, logical is always returning boolean
    expr.set_type(bool_type);
}

void unit_llvm_ir_gen::visit_logical_not_expression(logical_not_expression& expr) {
    auto value = process_unary_expression(expr);

    if(!value) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    auto& sub = expr.sub_expr();
    auto type = sub->get_type();

    if(!type::is_primitive(type)) {
        // TODO throw an exception
        // Logical negation for non-primitive types is not supported.
        std::cerr << "Error: Logical negation for non-primitive types is not supported yet." << std::endl;
    }

    _value = _builder->CreateNot(value);
}

//
// Comparison expressions
//


void symbol_type_resolver::visit_comparison_expression(comparison_expression& expr) {
    visit_binary_expression(expr);

    auto& left = expr.left();
    auto& right = expr.right();

    if(!type::is_primitive(left->get_type()) || !type::is_primitive(right->get_type())) {
        // TODO throw an exception
        // Logical for non-primitive types is not supported.
        std::cerr << "Error: Arithmetic for non-primitive types is not supported yet." << std::endl;
    }

    auto left_type = std::dynamic_pointer_cast<primitive_type>(left->get_type());
    auto right_type = std::dynamic_pointer_cast<primitive_type>(right->get_type());

    auto adapted_left = left;
    auto adapted_right = right;

    if(left_type->is_boolean() && !right_type->is_boolean()) {
        // Adapt right to boolean
        adapted_right = adapt_type(right, left_type);
    } else if(!left_type->is_boolean() && right_type->is_boolean()) {
        // Adapt left to boolean
        adapted_left = adapt_type(left, right_type);
    }  else {
        // Adapt right to left type
        // TODO rework to promote to biggest integer of both
        adapted_right = adapt_type(right, left_type);
    }

    if(!adapted_left || !adapted_right) {
        // TODO throw an exception
        // Adaptation is not possible
        std::cerr << "Error: Type alignment for comparison expression is not possible." << std::endl;
    }

    if(adapted_left!=left) {
        expr.assign_left(adapted_left);
    }
    if(adapted_right!=right) {
        expr.assign_right(adapted_right);
    }

    // For primitive type, logical is always returning boolean
    static auto bool_type = primitive_type::from_type(primitive_type::BOOL);
    expr.set_type(bool_type);
}

//
// Equal expression (==)
//

void unit_llvm_ir_gen::visit_equal_expression(equal_expression& expr) {
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    if(!type::is_primitive(expr.left()->get_type()) || !type::is_primitive(expr.right()->get_type())) {
        // TODO throw an exception
        // Comparison for non-primitive types is not supported.
        std::cerr << "Error: Comparison for non-primitive types is not supported yet." << std::endl;
    }

    // For primitives, operand types are supposed to be aligned
    static auto bool_type = primitive_type::from_type(primitive_type::BOOL);
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

void unit_llvm_ir_gen::visit_different_expression(different_expression& expr) {
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    if(!type::is_primitive(expr.left()->get_type()) || !type::is_primitive(expr.right()->get_type())) {
        // TODO throw an exception
        // Comparison for non-primitive types is not supported.
        std::cerr << "Error: Comparison for non-primitive types is not supported yet." << std::endl;
    }

    // For primitives, operand types are supposed to be aligned
    static auto bool_type = primitive_type::from_type(primitive_type::BOOL);
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

void unit_llvm_ir_gen::visit_lesser_expression(lesser_expression& expr) {
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    if(!type::is_primitive(expr.left()->get_type()) || !type::is_primitive(expr.right()->get_type())) {
        // TODO throw an exception
        // Comparison for non-primitive types is not supported.
        std::cerr << "Error: Comparison for non-primitive types is not supported yet." << std::endl;
    }

    // For primitives, operand types are supposed to be aligned
    static auto bool_type = primitive_type::from_type(primitive_type::BOOL);
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

void unit_llvm_ir_gen::visit_greater_expression(greater_expression& expr) {
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    if(!type::is_primitive(expr.left()->get_type()) || !type::is_primitive(expr.right()->get_type())) {
        // TODO throw an exception
        // Comparison for non-primitive types is not supported.
        std::cerr << "Error: Comparison for non-primitive types is not supported yet." << std::endl;
    }

    // For primitives, operand types are supposed to be aligned
    static auto bool_type = primitive_type::from_type(primitive_type::BOOL);
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

void unit_llvm_ir_gen::visit_lesser_equal_expression(lesser_equal_expression& expr) {
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    if(!type::is_primitive(expr.left()->get_type()) || !type::is_primitive(expr.right()->get_type())) {
        // TODO throw an exception
        // Comparison for non-primitive types is not supported.
        std::cerr << "Error: Comparison for non-primitive types is not supported yet." << std::endl;
    }

    // For primitives, operand types are supposed to be aligned
    static auto bool_type = primitive_type::from_type(primitive_type::BOOL);
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

void unit_llvm_ir_gen::visit_greater_equal_expression(greater_equal_expression& expr) {
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    if(!type::is_primitive(expr.left()->get_type()) || !type::is_primitive(expr.right()->get_type())) {
        // TODO throw an exception
        // Comparison for non-primitive types is not supported.
        std::cerr << "Error: Comparison for non-primitive types is not supported yet." << std::endl;
    }

    // For primitives, operand types are supposed to be aligned
    static auto bool_type = primitive_type::from_type(primitive_type::BOOL);
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

//
// Function invocation expression
//

void symbol_type_resolver::visit_function_invocation_expression(function_invocation_expression &expr) {
    auto callee = std::dynamic_pointer_cast<symbol_expression>(expr.callee_expr());
    if(!callee) {
        // TODO Support only variable expr as function name for now.
        std::cerr << "Error : only support symbol expr as function name for now" << std::endl;
    }

    for(auto& arg : expr.arguments()) {
        arg->accept(*this);
    }

    if(auto stmt = callee->find_statement()) {
        if(auto block = stmt->get_block()) {
            if(auto func = block->get_function()) {
                if(auto ns = func->parent_ns()) {
                    if(auto function = ns->lookup_function(callee->get_name())) {
                        // TODO support overloading
                        // TODO enforce prototype matching
                        // Function prototype and expression type are set at resolution
                        callee->resolve(function);
                        expr.set_type(function->return_type());
                    }
                }
            }
        }
    }

    if(!callee->is_resolved() || !callee->is_function()) {
        // Error: cannot resolve function.
        // TODO throw an exception
        std::cerr << "Cannot resolve function '" << callee->get_name().to_string() << "'" << std::endl;
    }

    auto params = callee->get_function()->parameters();
    if(expr.arguments().size() != params.size()) {
        // Error : callee and function have not the same argument count.
        // TODO throw an exception
        std::cerr << "Error : callee and function '" << callee->get_name().to_string() << "' have not the same argument count" << std::endl;
    }

    for(size_t n=0; n<expr.arguments().size(); ++n) {
        auto arg = expr.arguments().at(n);
        auto param = callee->get_function()->parameters().at(n);

        if(!param->get_type() || !param->get_type()->is_resolved() ||
           !arg->get_type() || !arg->get_type()->is_resolved()) {
            // TODO throw an exception
            // Error: function argument or function invocation parameter have undefined type
            std::cerr << "Error: function invocation must have defined types" << std::endl;
        }

        auto cast = adapt_type(arg, param->get_type());
        if(!cast) {
            // TODO throw an exception
            // Error: function parameter is not compatible (cannot be cast).
            std::cerr << "Error: function argument must be compatible to parameter" << std::endl;
        } else if(cast != arg ) {
            // Casted, assign casted expression instead of right source.
            expr.assign_argument(n, cast);
        } else {
            // Compatible type, no need to cast.
        }
    }
}

void unit_llvm_ir_gen::visit_function_invocation_expression(function_invocation_expression &expr) {
    auto callee = std::dynamic_pointer_cast<symbol_expression>(expr.callee_expr());
    if(!callee || !callee->is_function()) {
        // Function invocation is supported only for function symbol yet.
        // TODO throw exception
        std::cerr << "Function invocation is supported only for symbol yet." << std::endl;
    }

    std::vector<llvm::Value*> args;
    for(auto arg : expr.arguments()) {
        _value = nullptr;
        arg->accept(*this);
        if(!_value) {
            // Problem with argument generation
            // TODO throw exception
            std::cerr << "Problem with generation of an argument of a function call." << std::endl;
        }
        args.push_back(_value);
    }

    // TODO Check function argument count

    auto function = callee->get_function();
    auto it = _functions.find(function);
    if(it==_functions.end()) {
        // Error: function definition is not found.
        // TODO throw exception
        std::cerr << "Error: function definition is not found." << std::endl;
    }
    llvm::Function* llvm_func = it->second;
    if(!llvm_func) {
        // Error: function definition is not found.
        // TODO throw exception
        std::cerr << "Error: llvm function definition is not found." << std::endl;
    }
    // TODO look for external functions.

    _value = _builder->CreateCall(llvm_func, args);
}

//
// Cast expression
//

void symbol_type_resolver::visit_cast_expression(cast_expression& expr) {
    expr.sub_expr()->accept(*this);

    // TODO check if cast is possible (expr.expr().get_type() && expr.get_cast_type() compatibility)

    expr.set_type(expr.get_cast_type());
}


void unit_llvm_ir_gen::visit_cast_expression(cast_expression& expr) {
    auto source_type = expr.sub_expr()->get_type();
    auto target_type = expr.get_cast_type();

    if(!source_type->is_resolved() || !target_type->is_resolved()) {
        // Error: source and target types must be both resolved.
        // TODO throw exception
        std::cerr << "Error: in casting expression, both source and target types must be resolved." << std::endl;
    }

    if(!type::is_primitive(source_type) || !type::is_primitive(target_type)) {
        // TODO Support also non primitive type
        std::cerr << "Error: in casting expression, only primitive types are supported yet." << std::endl;
    }
    auto src = std::dynamic_pointer_cast<primitive_type>(source_type);
    auto tgt = std::dynamic_pointer_cast<primitive_type>(target_type);

    _value = nullptr;
    expr.sub_expr()->accept(*this);
    if(!_value) {
        // TODO throw exception
        // Sub expression is not reporting any value.
        std::cerr << "Error: in casting expression, expression to cast is not returning any value." << std::endl;
    }

    if(src->is_boolean()) {
        if(tgt->is_integer()) {
            if(tgt->is_unsigned()) {
                _value = _builder->CreateZExt(_value, _builder->getIntNTy(tgt->type_size()));
            } else {
                _value = _builder->CreateSExt(_value, _builder->getIntNTy(tgt->type_size()));
            }
        } else if (tgt->is_float()) {
            if(*tgt == primitive_type::FLOAT) {
                auto ftype = get_llvm_type(tgt);
                auto ftrue = llvm::ConstantFP::get(ftype, llvm::APFloat(1.0f));
                auto ffalse = llvm::ConstantFP::get(ftype, llvm::APFloat(0.0f));
                _value = _builder->CreateSelect(_value, ftrue, ffalse);
            } else if(*tgt == primitive_type::DOUBLE) {
                auto dtype = get_llvm_type(tgt);
                auto dtrue = llvm::ConstantFP::get(dtype, llvm::APFloat(1.0));
                auto dfalse = llvm::ConstantFP::get(dtype, llvm::APFloat(0.0));
                _value = _builder->CreateSelect(_value, dtrue, dfalse);
            } // else must not happen
        } else {
            // Support other types
        }
    } else if(src->is_integer()) {
        if(tgt->is_boolean()) {
            _value = _builder->CreateICmpNE(_value, _builder->getIntN(src->type_size(), 0));
        } else if (tgt->is_integer()) {
            if (tgt->is_signed()) {
                if (src->is_unsigned()) {
                    // TODO Add "Unsigned to signed" overflow warning
                    std::cerr << "Cast unsigned integer to signed integer may result on overflow" << std::endl;
                }
                // SExt or trunc for signed integers
                _value = _builder->CreateSExtOrTrunc(_value, get_llvm_type(tgt));
            } else /* if (tgt->is_unsigned())*/  {
                if (src->is_unsigned()) {
                    // TODO Add "Signed to unsigned" truncation/misunderstanding warning
                    std::cerr
                            << "Cast signed integer to unsigned integer may result on truncating/misinterpreting of integers"
                            << std::endl;
                }
                // SExt or trunc for signed integers
                _value = _builder->CreateZExtOrTrunc(_value, get_llvm_type(tgt));
            }
        } else if (tgt->is_float()) {
            if(src->is_unsigned()) {
                if(*tgt == primitive_type::FLOAT) {
                    _value = _builder->CreateUIToFP(_value, _builder->getFloatTy());
                } else if(*tgt == primitive_type::FLOAT) {
                    _value = _builder->CreateUIToFP(_value, _builder->getDoubleTy());
                } /* else must not happen */
            } else {
                if(*tgt == primitive_type::FLOAT) {
                    _value = _builder->CreateSIToFP(_value, _builder->getFloatTy());
                } else if(*tgt == primitive_type::FLOAT) {
                    _value = _builder->CreateSIToFP(_value, _builder->getDoubleTy());
                } /* else must not happen */
            }
        } else {
            // Support other types
        }
    } else if(src->is_float()) {
        if(tgt->is_boolean()) {
            _value = _builder->CreateFCmpUNE(_value, llvm::ConstantFP::get(get_llvm_type(tgt), 0.0));
        } else if(tgt->is_integer()) {
            if(tgt->is_unsigned()) {
                _value = _builder->CreateFPToUI(_value, get_llvm_type(tgt));
            } else {
                _value = _builder->CreateFPToSI(_value, get_llvm_type(tgt));
            }
        } else if(tgt->is_float()) {
            if(*src == primitive_type::FLOAT && *tgt == primitive_type::DOUBLE) {
                _value = _builder->CreateFPExt(_value, get_llvm_type(tgt));
            } else if(*src == primitive_type::DOUBLE && *tgt == primitive_type::FLOAT) {
                _value = _builder->CreateFPTrunc(_value, get_llvm_type(tgt));
            } else {
                // Do nothing, float type is already aligned
            }
        } else{
            // Support other types
        }
    } else {
        // Support other types
    }
}




} // namespace k::model::gen
