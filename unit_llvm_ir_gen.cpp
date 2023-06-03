//
// Created by emilien on 01/06/23.
//

#include "unit_llvm_ir_gen.hpp"

namespace k::unit::gen {

unit_llvm_ir_gen::unit_llvm_ir_gen(unit& unit):
_unit(unit)
{
    _context = std::make_unique<llvm::LLVMContext>();
    _builder = std::make_unique<llvm::IRBuilder<>>(*_context);
    _module = std::make_unique<llvm::Module>("My module", *_context);
}

void unit_llvm_ir_gen::visit_value_expression(value_expression &expr) {
    if(expr.is_literal()) {
        auto lit = expr.any_literal();
        switch(lit.index()) {
            case lex::any_literal_type_index::INTEGER:
                _value = llvm::ConstantInt::get(*_context, llvm::APInt(32, lit.get<lex::integer>().content, 10));
                break;
            case lex::any_literal_type_index::CHARACTER:
                break;
            case lex::any_literal_type_index::STRING:
                break;
            case lex::any_literal_type_index::BOOLEAN:
                break;
            case lex::any_literal_type_index::NUL:
                break;
            default:
                break;
        }
    } else {

    }
}

void unit_llvm_ir_gen::visit_variable_expression(variable_expression &var) {
    auto var_def = var.get_variable_def();
    // TODO
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

    // TODO: Check for type alignement

    _value = _builder->CreateAdd(left, right);
}

void unit_llvm_ir_gen::visit_substraction_expression(substraction_expression &expr) {
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    // TODO: Check for type alignement

    _value = _builder->CreateSub(left, right);
}

void unit_llvm_ir_gen::visit_multiplication_expression(multiplication_expression &expr) {
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    // TODO: Check for type alignement

    _value = _builder->CreateMul(left, right);
}

void unit_llvm_ir_gen::visit_division_expression(division_expression &expr) {
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    // TODO: Check for type alignement

    _value = _builder->CreateSDiv(left, right);
}

void unit_llvm_ir_gen::visit_modulo_expression(modulo_expression &expr) {
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    // TODO: Check for type alignement

    _value = _builder->CreateSRem(left, right);
}


} // k::unit::gen