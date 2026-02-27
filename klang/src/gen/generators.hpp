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

#ifndef KLANG_GENERATORS_HPP
#define KLANG_GENERATORS_HPP

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


#include <llvm/ExecutionEngine/JITSymbol.h>
#include <llvm/ExecutionEngine/Orc/CompileUtils.h>
#include <llvm/ExecutionEngine/Orc/Core.h>
#include <llvm/ExecutionEngine/Orc/ExecutionUtils.h>
#include <llvm/ExecutionEngine/Orc/ExecutorProcessControl.h>
#include <llvm/ExecutionEngine/Orc/IRCompileLayer.h>
#include <llvm/ExecutionEngine/Orc/JITTargetMachineBuilder.h>
#include <llvm/ExecutionEngine/Orc/RTDyldObjectLinkingLayer.h>
#include <llvm/ExecutionEngine/SectionMemoryManager.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>


#include "../model/model.hpp"
#include "../model/model_visitor.hpp"

#include "../common/logger.hpp"
#include "../lex/lexer.hpp"

#include "../compiler.hpp"


namespace k {
class compiler;
}

namespace k::model::gen {

class jit;

class generation_error : public k::log::compiler_error {
public:
    explicit generation_error(k::log::diagnostic diag)
        : k::log::compiler_error(std::move(diag)) {}
};


/**
 * First pass of IR generation.
 * Will generate global variables and function declarations.
 * Function implementation and global variable initialization will be done later with implementation_generator.
 * Used to ensure all declarations are done and accessible, useful for nesting and out-of-order declaration.
 */
class declaration_generator : public default_model_visitor, protected k::log::logger_relay {
protected:
    unit& _unit;

    std::shared_ptr<context> _context;

    std::unique_ptr<llvm::IRBuilder<>> _builder;

    llvm::Value* _value;

    std::stack<std::shared_ptr<structure>> _struct_stack;

    static constexpr unsigned int INTERNAL_ERROR_BASE = 0xA000;

    [[noreturn]] void throw_error(unsigned int code, const lex::opt_ref_any_lexeme& lexeme, const std::string& message, const std::vector<std::string>& args = {}) {
        k::lex::opt_any_lexeme opt = lexeme ? k::lex::opt_any_lexeme{lexeme->get()} : std::nullopt;
        auto diag = k::log::diagnostic::make_error(with_flag(code), message, args);
        if (opt) diag.at(*opt);
        logger_relay::report(diag);
        throw generation_error(std::move(diag));
    }

    /** Throw an internal-compiler-error (should never be reachable via any K source input). */
    [[noreturn]] void throw_internal_error(unsigned int code, const lex::opt_ref_any_lexeme& lexeme, const std::string& message, const std::vector<std::string>& args = {}) {
        throw_error(INTERNAL_ERROR_BASE + code, lexeme, message, args);
    }

public:
    declaration_generator(k::log::logger& logger, std::shared_ptr<context> context, unit& unit);

    llvm::Module& get_module();

    void visit_unit(unit &) override;

    void visit_namespace(ns &) override;
    void visit_function(function &) override;
    void visit_structure(structure&) override;
    void visit_member_variable_definition(member_variable_definition&) override;
    void visit_global_variable_definition(global_variable_definition &) override;

    void visit_block(block&) override;
    void visit_return_statement(return_statement&) override;
    void visit_if_else_statement(if_else_statement&) override;
    void visit_while_statement(while_statement&) override;
    void visit_for_statement(for_statement&) override;
    void visit_expression_statement(expression_statement&) override;
    void visit_variable_statement(variable_statement&) override;

    void generate();
};

/**
 * Really achieve function implementations and global variable initialization.
 * Must be run after declaration_generator.
 */
class implementation_generator : public default_model_visitor, protected k::log::logger_relay {
protected:
    unit& _unit;

    std::shared_ptr<context> _context;

    std::unique_ptr<llvm::IRBuilder<>> _builder;

    llvm::Value* _value;

    std::stack<std::shared_ptr<structure>> _struct_stack;

    /** Stack of active cleanup BasicBlocks, one per block with destructible local variables. */
    std::stack<llvm::BasicBlock*> _cleanup_blocks;

    /** Parallel stack: for each cleanup block, the list of variable_statements to destroy (declaration order). */
    std::stack<std::vector<std::shared_ptr<variable_statement>>> _cleanup_vars_stack;

    /** Per-function alloca for return value (used when destructions must happen before a return). */
    llvm::AllocaInst* _retval_alloca = nullptr;

    static constexpr unsigned int INTERNAL_ERROR_BASE = 0xA000;

    [[noreturn]] void throw_error(unsigned int code, const lex::opt_ref_any_lexeme& lexeme, const std::string& message, const std::vector<std::string>& args = {}) {
        k::lex::opt_any_lexeme opt = lexeme ? k::lex::opt_any_lexeme{lexeme->get()} : std::nullopt;
        auto diag = k::log::diagnostic::make_error(with_flag(code), message, args);
        if (opt) diag.at(*opt);
        logger_relay::report(diag);
        throw generation_error(std::move(diag));
    }

    /** Throw an internal-compiler-error (should never be reachable via any K source input). */
    [[noreturn]] void throw_internal_error(unsigned int code, const lex::opt_ref_any_lexeme& lexeme, const std::string& message, const std::vector<std::string>& args = {}) {
        throw_error(INTERNAL_ERROR_BASE + code, lexeme, message, args);
    }

public:
    implementation_generator(k::log::logger& logger, std::shared_ptr<context> context, unit& unit);

    llvm::Module& get_module();

    void visit_unit(unit &) override;

    void visit_namespace(ns &) override;
    void visit_function(function &) override;
    void visit_global_constructor_function(global_constructor_function&) override;
    void visit_global_destructor_function(global_destructor_function&) override;
    void visit_structure(structure&) override;
    void visit_member_variable_definition(member_variable_definition&) override;
    void visit_global_variable_definition(global_variable_definition &) override;

    void visit_block(block&) override;
    void visit_return_statement(return_statement&) override;
    void visit_if_else_statement(if_else_statement&) override;
    void visit_while_statement(while_statement&) override;
    void visit_for_statement(for_statement&) override;
    void visit_expression_statement(expression_statement&) override;
    void visit_variable_statement(variable_statement&) override;

    llvm::Constant* get_llvm_constant_from_value_expr(const value_expression&) const;
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

    void visit_prefix_increment_expression(prefix_increment_expression&) override;
    void visit_prefix_decrement_expression(prefix_decrement_expression&) override;
    void visit_postfix_increment_expression(postfix_increment_expression&) override;
    void visit_postfix_decrement_expression(postfix_decrement_expression&) override;

    void visit_logical_and_expression(logical_and_expression&) override;
    void visit_logical_or_expression(logical_or_expression&) override;
    void visit_logical_not_expression(logical_not_expression&) override;

    void visit_load_value_expression(load_value_expression&) override;
    void visit_address_of_expression(address_of_expression&) override;
    void visit_dereference_expression(dereference_expression&) override;
    void visit_member_of_object_expression(member_of_object_expression&) override;
    void visit_member_of_pointer_expression(member_of_pointer_expression&) override;

    void visit_equal_expression(equal_expression&) override;
    void visit_different_expression(different_expression&) override;
    void visit_lesser_expression(lesser_expression&) override;
    void visit_greater_expression(greater_expression&) override;
    void visit_lesser_equal_expression(lesser_equal_expression&) override;
    void visit_greater_equal_expression(greater_equal_expression&) override;

    void visit_subscript_expression(subscript_expression&) override;
    void visit_function_invocation_expression(function_invocation_expression&) override;
    void visit_constructor_invocation_expression(constructor_invocation_expression&) override;

    void visit_cast_expression(cast_expression&) override;

    void generate();

protected:
    void optimize_function_dead_inst_elimination(llvm::Function& func);

    /**
     * Emit or retrieve the declaration of @__fatal_null_assignation or @__fatal_null_dereference.
     * The function is declared as: void() noreturn nounwind cold, calling llvm.trap or llvm.debugtrap.
     */
    llvm::Function* get_or_declare_fatal_null_function(const std::string& name);

    /**
     * Emit a runtime null-check for `ptr_value`. If null, calls fatal_fn and then unreachable.
     * `ok_bb` is the block to continue in if not null. The function creates the check+branch.
     */
    void emit_null_check(llvm::Value* ptr_value, llvm::Function* fatal_fn, const std::string& label = "");
};



class jit {
protected:
    std::shared_ptr<compiler> _compiler;
    std::unique_ptr<llvm::orc::LLJIT> _lljit;
    llvm::orc::JITDylib &_main_dynlib;

    enum {
        DEFAULT,
        INITIALIZED,
        FINALIZED
    } _state = DEFAULT;

    jit(std::shared_ptr<compiler> compiler);

    friend class k::compiler;
    static std::unique_ptr<jit> create(std::shared_ptr<compiler> compiler);

    llvm::Expected<llvm::orc::ExecutorAddr> lookup_symbol_address(const std::string& name);
    llvm::Expected<llvm::orc::ExecutorAddr> lookup_main_entry_symbol_address();

    void add_module(llvm::orc::ThreadSafeModule module);

public:
    ~jit();

    void initialize_runtime();
    void finalize_runtime();

    template<typename T>
    T lookup_symbol(const std::string& name) {
        auto symb = lookup_symbol_address(name);
        if (symb) {
            return lookup_symbol_address(name)->toPtr<T>();
        } else {
            return nullptr;
        }
    }

    template<typename T>
    T lookup_main_entry_symbol() {
        return lookup_main_entry_symbol_address()->toPtr<T>();
    }

};

} // k::model::gen

#endif //KLANG_GENERATORS_HPP
