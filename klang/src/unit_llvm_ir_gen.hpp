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
#include <llvm/IR/LegacyPassManager.h>


#include "llvm/ExecutionEngine/JITSymbol.h"
#include "llvm/ExecutionEngine/Orc/CompileUtils.h"
#include "llvm/ExecutionEngine/Orc/Core.h"
#include "llvm/ExecutionEngine/Orc/ExecutionUtils.h"
#include "llvm/ExecutionEngine/Orc/ExecutorProcessControl.h"
#include "llvm/ExecutionEngine/Orc/IRCompileLayer.h"
#include "llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h"
#include "llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h"
#include "llvm/ExecutionEngine/SectionMemoryManager.h"



#include "unit.hpp"

#include "logger.hpp"
#include "lexer.hpp"


namespace k::unit::gen {

class unit_llvm_jit;

class generation_error : public std::runtime_error {
public:
    generation_error(const std::string &arg);
    generation_error(const char *string);
};



class unit_llvm_ir_gen : public default_element_visitor, protected k::lex::lexeme_logger {
protected:
    unit& _unit;

    std::unique_ptr<llvm::LLVMContext> _context;
    std::unique_ptr<llvm::IRBuilder<>> _builder;
    std::unique_ptr<llvm::Module> _module;

    llvm::Value* _value;

    std::map<std::shared_ptr<global_variable_definition>, llvm::GlobalVariable*> _global_vars;
    std::map<std::shared_ptr<function>, llvm::Function*> _functions;
    std::map<std::shared_ptr<parameter>, llvm::Argument*> _parameters;
    std::map<std::shared_ptr<parameter>, llvm::AllocaInst*> _parameter_variables;
    std::map<std::shared_ptr<variable_statement>, llvm::AllocaInst*> _variables;

    llvm::Type* get_llvm_type(const std::shared_ptr<type>& type);


    [[noreturn]] void throw_error(unsigned int code, const lex::opt_ref_any_lexeme& lexeme, const std::string& message, const std::vector<std::string>& args = {}) {
        error(code, lexeme, message, args);
        throw generation_error(message);
    }

public:
    unit_llvm_ir_gen(k::log::logger& logger, unit& unit);

    llvm::Module& get_module() {
        return *_module;
    }

    void visit_unit(unit &) override;

    void visit_namespace(ns &) override;
    void visit_function(function &) override;
    void visit_global_variable_definition(global_variable_definition &) override;

    void visit_block(block&) override;
    void visit_return_statement(return_statement&) override;
    void visit_if_else_statement(if_else_statement&) override;
    void visit_while_statement(while_statement&) override;
    void visit_for_statement(for_statement&) override;
    void visit_expression_statement(expression_statement&) override;
    void visit_variable_statement(variable_statement&) override;

    void visit_value_expression(value_expression&) override;
    void visit_symbol_expression(symbol_expression&) override;

    llvm::Value* process_unary_expression(unary_expression&);
    std::pair<llvm::Value*,llvm::Value*> process_binary_expression(binary_expression&);
    void visit_addition_expression(addition_expression&) override;
    void visit_substraction_expression(substraction_expression&) override;
    void visit_multiplication_expression(multiplication_expression&) override;
    void visit_division_expression(division_expression&) override;
    void visit_modulo_expression(modulo_expression&) override;
    void visit_bitwise_and_expression(bitwise_and_expression&) override;
    void visit_bitwise_or_expression(bitwise_or_expression&) override;
    void visit_bitwise_xor_expression(bitwise_xor_expression&) override;
    void visit_left_shift_expression(left_shift_expression&) override;
    void visit_right_shift_expression(right_shift_expression&) override;

    void create_assignement(std::shared_ptr<expression> expr, llvm::Value* value);
    void visit_simple_assignation_expression(simple_assignation_expression&) override;
    void visit_addition_assignation_expression(additition_assignation_expression&) override;
    void visit_substraction_assignation_expression(substraction_assignation_expression&) override;
    void visit_multiplication_assignation_expression(multiplication_assignation_expression&) override;
    void visit_division_assignation_expression(division_assignation_expression&) override;
    void visit_modulo_assignation_expression(modulo_assignation_expression&) override;
    void visit_bitwise_and_assignation_expression(bitwise_and_assignation_expression&) override;
    void visit_bitwise_or_assignation_expression(bitwise_or_assignation_expression&) override;
    void visit_bitwise_xor_assignation_expression(bitwise_xor_assignation_expression&) override;
    void visit_left_shift_assignation_expression(left_shift_assignation_expression&) override;
    void visit_right_shift_assignation_expression(right_shift_assignation_expression&) override;

    void visit_unary_plus_expression(unary_plus_expression&) override;
    void visit_unary_minus_expression(unary_minus_expression&) override;
    void visit_bitwise_not_expression(bitwise_not_expression&) override;

    void visit_logical_and_expression(logical_and_expression&) override;
    void visit_logical_or_expression(logical_or_expression&) override;
    void visit_logical_not_expression(logical_not_expression&) override;

    void visit_equal_expression(equal_expression&) override;
    void visit_different_expression(different_expression&) override;
    void visit_lesser_expression(lesser_expression&) override;
    void visit_greater_expression(greater_expression&) override;
    void visit_lesser_equal_expression(lesser_equal_expression&) override;
    void visit_greater_equal_expression(greater_equal_expression&) override;


    void visit_function_invocation_expression(function_invocation_expression&) override;

    void visit_cast_expression(cast_expression&) override;

    void dump();
    void verify();
    void optimize_functions();

    std::unique_ptr<unit_llvm_jit> to_jit();

protected:
    void optimize_function_dead_inst_elimination(llvm::Function& func);
};



class unit_llvm_jit {
private:
    std::unique_ptr<llvm::orc::ExecutionSession> _session;
    llvm::DataLayout _layout;
    llvm::orc::MangleAndInterner _mangle;
    llvm::orc::RTDyldObjectLinkingLayer _object_layer;
    llvm::orc::IRCompileLayer _compile_layer;
    llvm::orc::JITDylib &_main_dynlib;

public:

    unit_llvm_jit(std::unique_ptr<llvm::orc::ExecutionSession> session, llvm::orc::JITTargetMachineBuilder jtmb, llvm::DataLayout layout);
    ~unit_llvm_jit();

    static std::unique_ptr<unit_llvm_jit> create();

//    const llvm::DataLayout &get_data_layout() const { return _layout; }
//    llvm::orc::JITDylib &get_main_jit_dynlib() { return _main_dynlib; }

    void add_module(llvm::orc::ThreadSafeModule module, llvm::orc::ResourceTrackerSP res_tracker = nullptr);

    llvm::Expected<llvm::JITEvaluatedSymbol> lookup(llvm::StringRef name);

    template<typename T>
    T lookup_symbol(llvm::StringRef name) {
        return (T) _session->lookup({&_main_dynlib}, _mangle(name.str()))->getAddress();
    }

};



} // k::unit::gen

#endif //KLANG_UNIT_LLVM_IR_GEN_HPP
