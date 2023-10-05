//
// Created by emilien on 01/06/23.
//

#include "unit_llvm_ir_gen.hpp"

#include <llvm/IR/Verifier.h>

#include "llvm/Transforms/InstCombine/InstCombine.h"
#include "llvm/Transforms/Scalar.h"
#include "llvm/Transforms/Scalar/GVN.h"

#include "llvm/Target/TargetMachine.h"

namespace k::unit::gen {

//
// Exceptions
//
generation_error::generation_error(const std::string &arg) :
        runtime_error(arg)
{}

generation_error::generation_error(const char *string) :
        runtime_error(string)
{}

//
// LLVM unit generator
//

unit_llvm_ir_gen::unit_llvm_ir_gen(k::log::logger& logger, unit& unit):
lexeme_logger(logger, 0x40000),
_unit(unit)
{
    // TODO initialize them only once
    LLVMInitializeNativeTarget();
    LLVMInitializeNativeAsmPrinter();
    LLVMInitializeNativeAsmParser();

    _context = std::make_unique<llvm::LLVMContext>();
    _builder = std::make_unique<llvm::IRBuilder<>>(*_context);
    _module = std::make_unique<llvm::Module>(unit.get_unit_name().to_string(), *_context);
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

llvm::Type* unit_llvm_ir_gen::get_llvm_type(const std::shared_ptr<type>& type) {
    llvm::Type *llvm_type;
    if(!type::is_resolved(type)) {
        // TODO cannot translate unresolved type.
        return nullptr;
    }
    auto res_type = std::dynamic_pointer_cast<resolved_type>(type);

    if(res_type->is_primitive()) {
        auto prim = std::dynamic_pointer_cast<primitive_type>(res_type);
        if(prim->is_integer()) {
            // LLVM looks to use same type descriptor for signed and unsigned integers
            llvm_type = _builder->getIntNTy(prim->type_size());
        } else if (prim->is_boolean()) {
            llvm_type = _builder->getInt1Ty();
        } else if (*prim == primitive_type::FLOAT) {
            return _builder->getFloatTy();
        } else if (*prim == primitive_type::DOUBLE) {
            return _builder->getDoubleTy();
        } else {
            // TODO support float types
        }
    }
    return llvm_type;
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

llvm::Value* unit_llvm_ir_gen::process_unary_expression(unary_expression& expr) {
    llvm::Value* res = nullptr;
    _value = nullptr;
    expr.sub_expr()->accept(*this);
    res = _value;
    _value = nullptr;
    return res;
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


void unit_llvm_ir_gen::visit_unit(unit &unit) {
    visit_namespace(*_unit.get_root_namespace());
}

void unit_llvm_ir_gen::visit_namespace(ns &ns) {
    for(auto child : ns.get_children()) {
        child->accept(*this);
    }
}

void unit_llvm_ir_gen::visit_global_variable_definition(global_variable_definition &var) {
    llvm::Type *type = get_llvm_type(var.get_type());

    // TODO initialize the variable with the expression
    llvm::Constant *value;
    if(type::is_prim_integer(var.get_type())) {
        value = llvm::ConstantInt::get(type, 0);
    } else if(type::is_prim_bool(var.get_type())) {
        value = llvm::ConstantInt::getFalse(type);
    } else if(type::is_prim_float(var.get_type())) {
        value = llvm::ConstantFP::get(type, 0.0);
    }

    // TODO use the real mangled name
    //std::string mangledName;
    //Mangler::getNameWithPrefix(mangledName, "test::toto", Mangler::ManglingMode::Default);

    auto variable = new llvm::GlobalVariable(*_module, type, false, llvm::GlobalValue::ExternalLinkage, value, var.get_name());
    _global_vars.insert({var.shared_as<global_variable_definition>(), variable});
}

void unit_llvm_ir_gen::visit_function(function &function) {
    // Parameter types:
    std::vector<llvm::Type*> param_types;
    for(const auto& param : function.parameters()) {
        param_types.push_back(get_llvm_type(param->get_type()));
    }

    // Return type, if any:
    llvm::Type* ret_type = nullptr;
    if(const auto& ret = function.return_type()) {
        ret_type = get_llvm_type(ret);
    }

    // create the function:
    llvm::FunctionType *func_type = llvm::FunctionType::get(ret_type, param_types, false);
    llvm::Function *func = llvm::Function::Create(func_type, llvm::Function::ExternalLinkage, function.name(), *_module);

    _functions.insert({function.shared_as<k::unit::function>(), func});

    // create the function content:
    llvm::BasicBlock *block = llvm::BasicBlock::Create(*_context, "entry", func);
    _builder->SetInsertPoint(block);

    // Capture arguments
    auto arg_it = func->arg_begin();
    for(const auto& param : function.parameters()) {
        llvm::Argument *arg = &*(arg_it++);
        arg->setName(param->get_name());
        _parameters.insert({param, arg});

        llvm::AllocaInst* alloca = _builder->CreateAlloca(get_llvm_type(param->get_type()), nullptr, param->get_name());
        _parameter_variables.insert({param, alloca});

        // Read param value and store it in dedicated local var
        //auto val = _builder->CreateLoad(int32Type, arg);
        _builder->CreateStore(arg, alloca);
    }

    // Produce content
    function.get_block()->accept(*this);

    // Force adding a return void as last instruction.
    _builder->CreateRetVoid();

    // Pre-optimize function
    optimize_function_dead_inst_elimination(*func);

    // Verify function
    llvm::verifyFunction(*func);
}

void unit_llvm_ir_gen::optimize_function_dead_inst_elimination(llvm::Function& func) {
    for(auto& block : func) {
        llvm::BasicBlock *bb;
        // Find first terminator instruction
        auto term = std::find_if(block.begin(), block.end(), [](auto& inst)->bool{return inst.isTerminator();});
        if(term!=block.end()) {
            if(++term!=block.end()) {
                auto& inst_list = block.getInstList();
                inst_list.erase(term, block.end());
            }
        }
    }
}

void unit_llvm_ir_gen::visit_block(block& block) {
    for(auto stmt : block.get_statements()) {
        stmt->accept(*this);
    }
}

void unit_llvm_ir_gen::visit_return_statement(return_statement& stmt) {

    if(auto expr = stmt.get_expression()) {
        _value = nullptr;
        expr->accept(*this);

        if (_value) {
            _builder->CreateRet(_value);
        } else {
            // TODO Must be an error.
            _builder->CreateRetVoid();
        }
    } else {
        _builder->CreateRetVoid();
    }
}

void unit_llvm_ir_gen::visit_if_else_statement(if_else_statement& stmt) {

    // Condition expression
    _value = nullptr;
    stmt.get_test_expr()->accept(*this);
    auto test_value = _value;
    _value = nullptr;

    bool has_else = (bool)stmt.get_else_stmt();

    // Retrieve current block and create then, else and continue blocks
    llvm::Function* func = _builder->GetInsertBlock()->getParent();
    llvm::BasicBlock* then_block = llvm::BasicBlock::Create(*_context, "then", func);
    llvm::BasicBlock* else_block = has_else ? llvm::BasicBlock::Create(*_context, "else") : nullptr;
    llvm::BasicBlock* cont_block = llvm::BasicBlock::Create(*_context, "cont");

    // Do branching
    if(has_else) {
        _builder->CreateCondBr(test_value, then_block, else_block);
    } else {
        _builder->CreateCondBr(test_value, then_block, cont_block);
    }

    // Generate "then" branch
    _builder->SetInsertPoint(then_block);
    stmt.get_then_stmt()->accept(*this);
    _builder->CreateBr(cont_block);

    // Generate "else" branch, if any
    if(has_else) {
        func->getBasicBlockList().push_back(else_block);
        _builder->SetInsertPoint(else_block);
        stmt.get_else_stmt()->accept(*this);
        _builder->CreateBr(cont_block);
    }

    // Generate "continuation" block
    func->getBasicBlockList().push_back(cont_block);
    _builder->SetInsertPoint(cont_block);
}

void unit_llvm_ir_gen::visit_expression_statement(expression_statement& stmt) {
    if(auto expr = stmt.get_expression()) {
        expr->accept(*this);
    }
}

void unit_llvm_ir_gen::visit_variable_statement(variable_statement& var) {
    // TODO process initialization

    // create the alloca at begining of the function
    // TODO rework it to do so at right place (or begining of the block ?)
    auto func = _functions[var.get_block()->get_function()];
    llvm::IRBuilder<> build(&func->getEntryBlock(),func->getEntryBlock().begin());

    llvm::Type *  type = get_llvm_type(var.get_type());
    llvm::AllocaInst* alloca = build.CreateAlloca(type, nullptr, var.get_name());
    _variables.insert({var.shared_as<variable_statement>(), alloca});

    // TODO initialize the variable with the expression
    llvm::Constant *value;
    if(type::is_prim_integer(var.get_type())) {
        value = llvm::ConstantInt::get(type, 0);
    } else if(type::is_prim_bool(var.get_type())) {
        value = llvm::ConstantInt::getFalse(type);
    } else if(type::is_prim_float(var.get_type())) {
        value = llvm::ConstantFP::get(type, 0.0);
    }

    build.CreateStore(value, alloca);
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

void unit_llvm_ir_gen::dump() {
    _module->print(llvm::outs(), nullptr);
}

void unit_llvm_ir_gen::verify() {
    llvm::verifyModule(*_module, &llvm::outs());
}


void unit_llvm_ir_gen::optimize_functions() {
    // TODO switch to new pass manager
    std::shared_ptr<llvm::legacy::FunctionPassManager> passes;

    // Initialize Function pass manager
    passes = std::make_shared<llvm::legacy::FunctionPassManager>(_module.get());
    // Do simple "peephole" optimizations and bit-twiddling optzns.
    passes->add(llvm::createInstructionCombiningPass());
    // Re-associate expressions.
    passes->add(llvm::createReassociatePass());
    // Eliminate Common SubExpressions.
    passes->add(llvm::createGVNPass());
    // Eliminate chains of dead computations.
    passes->add(llvm::createDeadCodeEliminationPass());
    // Simplify the control flow graph (deleting unreachable blocks, etc).
    passes->add(llvm::createCFGSimplificationPass());

    passes->doInitialization();

    for(auto& func : *_module) {
        passes->run(func);
    }
}


std::unique_ptr<unit_llvm_jit> unit_llvm_ir_gen::to_jit() {
    auto jit = unit_llvm_jit::create();
    if(jit) {
        jit.get()->add_module(llvm::orc::ThreadSafeModule(std::move(_module), std::move(_context)));
    }
    return jit;
}



//
// LLVM JIT
//

unit_llvm_jit::unit_llvm_jit(std::unique_ptr<llvm::orc::ExecutionSession> session, llvm::orc::JITTargetMachineBuilder jtmb, llvm::DataLayout layout) :
        _session(std::move(session)),
        _layout(std::move(layout)),
        _mangle(*this->_session, this->_layout),
        _object_layer(*this->_session, []() { return std::make_unique<llvm::SectionMemoryManager>(); }),
        _compile_layer(*this->_session, _object_layer, std::make_unique<llvm::orc::ConcurrentIRCompiler>(std::move(jtmb))),
        _main_dynlib(this->_session->createBareJITDylib("<main>")) {
    _main_dynlib.addGenerator(llvm::cantFail(llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(layout.getGlobalPrefix())));
    if (jtmb.getTargetTriple().isOSBinFormatCOFF()) {
        _object_layer.setOverrideObjectFlagsWithResponsibilityFlags(true);
        _object_layer.setAutoClaimResponsibilityForObjectSymbols(true);
    }
}

unit_llvm_jit::~unit_llvm_jit() {
    if (auto Err = _session->endSession())
        _session->reportError(std::move(Err));
}

std::unique_ptr<unit_llvm_jit> unit_llvm_jit::create() {
    auto epc = llvm::orc::SelfExecutorProcessControl::Create();
    if (!epc) {
        // TODO throw an exception.
        std::cerr << "Failed to instantiate ORC SelfExecutorProcessControl" << std::endl;
        return nullptr;
    }

    auto session = std::make_unique<llvm::orc::ExecutionSession>(std::move(*epc));

    llvm::orc::JITTargetMachineBuilder jtmb(session->getExecutorProcessControl().getTargetTriple());

    auto layout = jtmb.getDefaultDataLayoutForTarget();
    if (!layout) {
        // TODO throw an exception.
        std::cerr << "Failed to retrieve default data layout for current target." << std::endl;
        return nullptr;
    }

    return std::make_unique<unit_llvm_jit>(std::move(session), std::move(jtmb), std::move(*layout));
}

void unit_llvm_jit::add_module(llvm::orc::ThreadSafeModule module, llvm::orc::ResourceTrackerSP res_tracker) {
    if (!res_tracker)
        res_tracker = _main_dynlib.getDefaultResourceTracker();
    if(llvm::Error err = _compile_layer.add(res_tracker, std::move(module))) {
        std::cerr << "Failed to register module to jit." << std::endl;
    }
}

llvm::Expected<llvm::JITEvaluatedSymbol> unit_llvm_jit::lookup(llvm::StringRef name) {
    return _session->lookup({&_main_dynlib}, _mangle(name.str()));
}

} // k::unit::gen