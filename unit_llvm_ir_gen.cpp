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
// LLVM unit generator
//

unit_llvm_ir_gen::unit_llvm_ir_gen(unit& unit):
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

    if(auto param = std::dynamic_pointer_cast<parameter>(var_def)) {
        // TODO use the right type, only int32 supported for now
        llvm::Type *int32Type = llvm::Type::getInt32Ty(*_context);
        _value = _builder->CreateLoad(int32Type, _parameter_variables[param], param->get_name());
    } else if(auto global_var = std::dynamic_pointer_cast<global_variable_definition>(var_def)) {
        // TODO use the right type, only int32 supported for now
        llvm::Type *int32Type = llvm::Type::getInt32Ty(*_context);
        llvm::GlobalVariable* gv = _global_vars[global_var];
        _value = _builder->CreateLoad(int32Type, gv, global_var->get_name());
    } else if (auto local_var = std::dynamic_pointer_cast<variable_statement>(var_def)) {
        // TODO use the right type, only int32 supported for now
        llvm::Type *int32Type = llvm::Type::getInt32Ty(*_context);
        _value = _builder->CreateLoad(int32Type, _variables[local_var], local_var->get_name());
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

    // TODO: Check for type alignment

    _value = _builder->CreateSRem(left, right);
}

void unit_llvm_ir_gen::create_assignement(std::shared_ptr<expression> expr, llvm::Value* value) {
    auto var_expr = std::dynamic_pointer_cast<variable_expression>(expr);
    if(!var_expr) {
        // TODO throw an exception
        // Only support direct variable expression as left operand
        std::cerr << "Assignation supports only direct variable assignment for now." << std::endl;
    }

    auto var = var_expr->get_variable_def();

    if(auto param = std::dynamic_pointer_cast<parameter>(var)) {
        // TODO seems not working correctly
        _builder->CreateStore(value, _parameter_variables[param]);
    } else if (auto local_var = std::dynamic_pointer_cast<variable_statement>(var)) {
        // TODO use the right type, only int32 supported for now
        _value = _builder->CreateStore(value, _variables[local_var]);
    } else if(auto global_var = std::dynamic_pointer_cast<global_variable_definition>(var)) {
        // TODO use the right type, only int32 supported for now
        llvm::GlobalVariable* gv = _global_vars[global_var];
        _value = _builder->CreateStore(value, gv);
    } else {
        std::cout << "Assign to something not supported" << std::endl;
    }
}

void unit_llvm_ir_gen::visit_assignation_expression(assignation_expression& expr) {
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

    // TODO: Check for type alignement
    _value = _builder->CreateAdd(left, right);
    create_assignement(expr.left(), _value);
}

void unit_llvm_ir_gen::visit_substraction_assignation_expression(substraction_assignation_expression& expr) {
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    // TODO: Check for type alignement
    _value = _builder->CreateSub(left, right);
    create_assignement(expr.left(), _value);
}

void unit_llvm_ir_gen::visit_multiplication_assignation_expression(multiplication_assignation_expression& expr) {
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    // TODO: Check for type alignement
    _value = _builder->CreateMul(left, right);
    create_assignement(expr.left(), _value);
}

void unit_llvm_ir_gen::visit_division_assignation_expression(division_assignation_expression& expr) {
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    // TODO: Check for type alignement
    _value = _builder->CreateSDiv(left, right);
    create_assignement(expr.left(), _value);
}

void unit_llvm_ir_gen::visit_modulo_assignation_expression(modulo_assignation_expression& expr) {
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    // TODO: Check for type alignement
    _value = _builder->CreateSRem(left, right);
    create_assignement(expr.left(), _value);
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
    // TODO use the right type, only int32 supported for now
    // TODO initialize the variable with the expression

    //std::string mangledName;
    //Mangler::getNameWithPrefix(mangledName, "test::toto", Mangler::ManglingMode::Default);
    llvm::Type *int32Type = llvm::Type::getInt32Ty(*_context);
    llvm::Constant *testValue = llvm::ConstantInt::get(int32Type, 0);
    llvm::GlobalVariable * variable = new llvm::GlobalVariable(*_module, int32Type, false, llvm::GlobalValue::ExternalLinkage, testValue, var.get_name());

    _global_vars.insert({var.shared_as<global_variable_definition>(), variable});
}


void unit_llvm_ir_gen::visit_function(function &function) {
    // Parameter types:
    std::vector<llvm::Type*> param_types;
    for(auto param : function.parameters()) {
        // TODO use the right type, only int32 supported for now
        llvm::Type *int32Type = llvm::Type::getInt32Ty(*_context);
        param_types.push_back(int32Type);
    }

    // Return type, if any:
    llvm::Type* ret_type = nullptr;
    if(auto ret = function.return_type()) {
        // TODO use the right type, only int32 supported for now
        llvm::Type *int32Type = llvm::Type::getInt32Ty(*_context);
        ret_type = int32Type;
    }

    // Create the function:
    llvm::FunctionType *func_type = llvm::FunctionType::get(ret_type, param_types, false);
    llvm::Function *func = llvm::Function::Create(func_type, llvm::Function::ExternalLinkage, function.name(), *_module);

    _functions.insert({function.shared_as<k::unit::function>(), func});

    // Create the function content:
    llvm::BasicBlock *block = llvm::BasicBlock::Create(*_context, "entry", func);
    _builder->SetInsertPoint(block);

    // Capture arguments
    auto arg_it = func->arg_begin();
    for(auto param : function.parameters()) {
        llvm::Argument *arg = &*(arg_it++);
        arg->setName(param->get_name());
        _parameters.insert({param, arg});

        // TODO use the right type, only int32 supported for now
        llvm::Type *int32Type = llvm::Type::getInt32Ty(*_context);
        llvm::AllocaInst* alloca = _builder->CreateAlloca(int32Type, nullptr, param->get_name());
        _parameter_variables.insert({param, alloca});

        // Read param value and store it in dedicated local var
        //auto val = _builder->CreateLoad(int32Type, arg);
        _builder->CreateStore(arg, alloca);
    }

    // Produce content
    function.get_block()->accept(*this);

    // Verify function
    llvm::verifyFunction(*func);

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

void unit_llvm_ir_gen::visit_expression_statement(expression_statement& stmt) {
    if(auto expr = stmt.get_expression()) {
        expr->accept(*this);
    }
}

void unit_llvm_ir_gen::visit_variable_statement(variable_statement& var) {
    // TODO process initialization

    // Create the alloca at begining of the function
    // TODO rework it to do so at right place (or begining of the block ?)
    auto func = _functions[var.get_block()->get_function()];
    llvm::IRBuilder<> build(&func->getEntryBlock(),func->getEntryBlock().begin());

    // TODO use the right type, only int32 supported for now
    llvm::Type *int32Type = llvm::Type::getInt32Ty(*_context);
    llvm::AllocaInst* alloca = build.CreateAlloca(int32Type, nullptr, var.get_name());
    _variables.insert({var.shared_as<variable_statement>(), alloca});

    // TODO replace init by 0 with the real init
    llvm::Value* zero = llvm::ConstantInt::get(int32Type, 0);
    build.CreateStore(zero, alloca);
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
    // Reassociate expressions.
    passes->add(llvm::createReassociatePass());
    // Eliminate Common SubExpressions.
    passes->add(llvm::createGVNPass());
    // Simplify the control flow graph (deleting unreachable blocks, etc).
    passes->add(llvm::createCFGSimplificationPass());
    passes->doInitialization();

    for(auto& func : *_module) {
        passes->run(func);
    }

}


llvm::Expected<std::unique_ptr<unit_llvm_jit>> unit_llvm_ir_gen::to_jit() {
    auto jit = unit_llvm_jit::Create();
    if(jit) {
        jit.get()->addModule(llvm::orc::ThreadSafeModule(std::move(_module), std::move(_context)));
    }
    return jit;
}


//
// LLVM JIT
//



} // k::unit::gen