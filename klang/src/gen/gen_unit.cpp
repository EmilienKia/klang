/*
 * K Language compiler
 *
 * Copyright 2023-2024 Emilien Kia
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
// Named element
//

void symbol_type_resolver::visit_named_element(named_element& named) {
    if (named.get_fq_name().empty()) {
        if (named.get_short_name().empty()) {
            // TODO correctly handle unnamed elements
        } else {
            auto elem = dynamic_cast<element*>(&named);
            if (elem) {
                named.assign_name(elem->ancestor<named_element>()->get_name().with_back(named.get_short_name()));
            }
        }
    }
}


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
    if (ns.get_fq_name().empty()) {
        if (ns.is_root()) {
            // Root namespace
            // Should not happen, supposed to be handled at model construction level
            if (ns.get_name().empty()) {
                // TODO Root namespace cannot be unnamed at this stage
                std::cerr << "Error: Root namespace cannot be unnamed at this stage" << std::endl;
                ns.assign_name(name(true, "unnamed"));
            } else {
                ns.assign_name(ns.get_name().with_root_prefix());
            }
        } else {
            if (ns.get_short_name().empty()) {
                // TODO correctly handle unnamed namespaces
            } else {
                ns.assign_name(ns.parent<model::ns>()->get_name().with_back(ns.get_short_name()));
            }
        }
    }

    for(auto& child : ns.get_children()) {
        child->accept(*this);
    }

}

void unit_llvm_ir_gen::visit_namespace(ns &ns) {
    for(auto child : ns.get_children()) {
        child->accept(*this);
    }
}

//
// Structure
//
void symbol_type_resolver::visit_structure(structure& st) {
    visit_named_element(st);

    for(auto& child : st.get_children()) {
        child->accept(*this);
    }

    // Create type for structure
    struct_type_builder builder(_context);
    builder.name(st.get_short_name());
    builder.structure(st.shared_as<structure>());
    for(auto& var : st.variables()) {
        builder.append_field(var.first, var.second->get_type());
    }
    auto st_type = builder.build();
    st.set_struct_type(st_type);
}

void unit_llvm_ir_gen::visit_structure(structure& st) {
    _struct_stack.push(st.shared_as<structure>());

    // There is nothing yet.
    // TODO
    // Add constructors and destructors.

    // Add methods:
    for(auto& child : st.get_children()) {
        child->accept(*this);
    }

    _struct_stack.pop();
}


//
// Member variable definition
//
void symbol_type_resolver::visit_member_variable_definition(member_variable_definition& var) {
    visit_named_element(var);
    // TODO
}

void unit_llvm_ir_gen::visit_member_variable_definition(member_variable_definition&) {
    // TODO
}


//
// Global variable definition
//

void symbol_type_resolver::visit_global_variable_definition(global_variable_definition& var)
{
    visit_named_element(var);
    if(!type::is_resolved(var.get_type())) {
        auto unres_type = std::dynamic_pointer_cast<unresolved_type>(var.get_type());
        if(!unres_type) {
            // TODO throw an exception
            std::cerr << "Error: global variable definition has an unresolvable type." << std::endl;
        }
        auto type = _context->from_string(unres_type->type_id());
        if(!type || !type::is_resolved(type)) {
            // TODO throw an exception
            std::cerr << "Error: global variable definition has an unresolvable type." << std::endl;
        } else {
            var.set_type(type);
        }
    }
    // TODO visit parameter definition (just in case default init is referencing a variable).
}

void unit_llvm_ir_gen::visit_global_variable_definition(global_variable_definition &var) {
    auto type = var.get_type();
    llvm::Type *llvm_type = _context->get_llvm_type(type);

    // TODO initialize the variable with the expression
    // Here is the 0-filled initialization:
    llvm::Constant *value = type->generate_default_value_initializer();

    // TODO use the real mangled name
    //std::string mangledName;
    //Mangler::getNameWithPrefix(mangledName, "test::toto", Mangler::ManglingMode::Default);

    auto variable = new llvm::GlobalVariable(*_module, llvm_type, false, llvm::GlobalValue::ExternalLinkage, value, var.get_short_name());
    _global_vars.insert({var.shared_as<global_variable_definition>(), variable});
}

//
// Function
//

void symbol_type_resolver::visit_function(function& fn)
{
    visit_named_element(fn);
    // TODO visit parameter definition (just in case default init is referencing a variable).

    if(auto block = fn.get_block()) {
        visit_block(*block);
    }

}

void unit_llvm_ir_gen::visit_function(function &function) {
    // Parameter types:
    std::vector<llvm::Type*> param_types;
    if (function.is_member() /* TODO and is not static */) {
        // First parameter is the 'this' pointer
        auto owner = function.get_owner();
        param_types.push_back(_context->get_llvm_type(owner->get_struct_type()->get_reference()));
    }
    for(const auto& param : function.parameters()) {
        param_types.push_back(_context->get_llvm_type(param->get_type()));
    }

    // Return type, if any:
    llvm::Type* ret_type = nullptr;
    if(const auto& ret = function.get_return_type()) {
        ret_type = _context->get_llvm_type(ret);
    } else {
        ret_type = llvm::Type::getVoidTy(**_context);
    }

    // create the function:
    llvm::FunctionType *func_type = llvm::FunctionType::get(ret_type, param_types, false);
    llvm::Function *func = llvm::Function::Create(func_type, llvm::Function::ExternalLinkage, function.get_short_name(), *_module);

    _functions.insert({function.shared_as<k::model::function>(), func});

    // create the function content:
    llvm::BasicBlock *block = llvm::BasicBlock::Create(**_context, "entry", func);
    _builder->SetInsertPoint(block);

    // Capture arguments
    auto arg_it = func->arg_begin();
    if (function.is_member() /* TODO and is not static */) {
        // First parameter is the 'this' pointer
        llvm::Argument *arg = &*(arg_it++);
        arg->setName("this");
        // Create dedicated local storage for "this" argument
        llvm::AllocaInst* alloca = _builder->CreateAlloca(llvm::PointerType::get(_context->llvm_context(), 0), nullptr, "this");
        _function_this_variables.insert({function.shared_as<model::function>(), alloca});
        // Read "this" param value and store it in dedicated local var
        _builder->CreateStore(arg, alloca);
    }
    for(const auto& param : function.parameters()) {
        // Iterate to get all explicit parameters
        llvm::Argument *arg = &*(arg_it++);
        arg->setName(param->get_short_name());
        // Create dedicated local storage for argument
        llvm::AllocaInst* alloca = _builder->CreateAlloca(_context->get_llvm_type(param->get_type()), nullptr, param->get_short_name());
        _parameter_variables.insert({param, alloca});
        // Read param value and store it in dedicated local var
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
                block.erase(term, block.end());
            }
        }
    }
}


} // namespace k::model::gen
