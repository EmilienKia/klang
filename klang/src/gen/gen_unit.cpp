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

#include <llvm/IR/Verifier.h>

namespace k::model::gen {


//
// Unit
//

void symbol_type_resolver::visit_unit(unit& unit)
{
    visit_namespace(*_unit.get_root_namespace());
}

void unit_llvm_ir_gen::visit_unit(unit &unit) {
    visit_namespace(*_unit.get_root_namespace());
}

//
// Namespace
//

void symbol_type_resolver::visit_namespace(ns& ns)
{
    bool has_name = false;
    if(!ns.get_name().empty()) {
        has_name = true;
        _naming_context.push_back(ns.get_name());
    }

    for(auto& child : ns.get_children()) {
        child->accept(*this);
    }

    if(has_name) {
        _naming_context.pop_back();
    }
}

void unit_llvm_ir_gen::visit_namespace(ns &ns) {
    for(auto child : ns.get_children()) {
        child->accept(*this);
    }
}

//
// Global variable definition
//

void symbol_type_resolver::visit_global_variable_definition(global_variable_definition& var)
{
    // TODO visit parameter definition (just in case default init is referencing a variable).
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

//
// Function
//

void symbol_type_resolver::visit_function(function& fn)
{
    _naming_context.push_back(fn.name());

    // TODO visit parameter definition (just in case default init is referencing a variable).

    if(auto block = fn.get_block()) {
        visit_block(*block);
    }

    _naming_context.pop_back();
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

    _functions.insert({function.shared_as<k::model::function>(), func});

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


} // namespace k::model::gen
