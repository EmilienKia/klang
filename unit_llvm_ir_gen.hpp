//
// Created by emilien on 01/06/23.
//

#ifndef KLANG_UNIT_LLVM_IR_GEN_HPP
#define KLANG_UNIT_LLVM_IR_GEN_HPP

#include <llvm/ADT/STLExtras.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>

#include "unit.hpp"

namespace k::unit::gen {

class unit_llvm_ir_gen : public default_element_visitor {
protected:
    unit& _unit;

    std::unique_ptr<llvm::LLVMContext> _context;
    std::unique_ptr<llvm::IRBuilder<>> _builder;
    std::unique_ptr<llvm::Module> _module;


    llvm::Value* _value;

public:
    unit_llvm_ir_gen(unit& unit);


    void visit_value_expression(value_expression&) override;
    void visit_variable_expression(variable_expression&) override;

    std::pair<llvm::Value*,llvm::Value*> process_binary_expression(binary_expression&);
    void visit_addition_expression(addition_expression&) override;
    void visit_substraction_expression(substraction_expression&) override;
    void visit_multiplication_expression(multiplication_expression&) override;
    void visit_division_expression(division_expression&) override;
    void visit_modulo_expression(modulo_expression&) override;
};


} // k::unit::gen

#endif //KLANG_UNIT_LLVM_IR_GEN_HPP
