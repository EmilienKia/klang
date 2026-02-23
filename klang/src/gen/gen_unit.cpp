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
#include "resolvers.hpp"
#include "generators.hpp"

#include <llvm/IR/Verifier.h>
#include <llvm/Transforms/Utils/ModuleUtils.h>

#include <unordered_map>
#include <unordered_set>

namespace k::model::gen {

//
// Named element
//

void symbol_resolver::visit_named_element(named_element& named) {
    // Assign fully qualified name if not already assigned, and compute the mangled name accordingly
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
// Note : Global constructor and destructor method objects are always created but generated and registered only if needed.
// But Global main method is
//

void symbol_resolver::visit_unit(unit& unit)
{
    visit_namespace(*_unit.get_root_namespace());

    visit_global_constructor_function(_unit.get_global_constructor_function());
    visit_global_destructor_function(_unit.get_global_destructor_function());
}

void type_reference_resolver::visit_unit(unit& unit)
{
    visit_namespace(*_unit.get_root_namespace());

    visit_global_constructor_function(_unit.get_global_constructor_function());
    visit_global_destructor_function(_unit.get_global_destructor_function());

    if (auto func = unit.get_root_namespace()->get_function("main")) {
        if (auto main_func = unit.generate_main_function(func)) {
            visit_global_main_function(*main_func);
        }
    }
}

void declaration_generator::visit_unit(unit &unit) {
    visit_namespace(*_unit.get_root_namespace());

    visit_global_constructor_function(_unit.get_global_constructor_function());
    visit_global_destructor_function(_unit.get_global_destructor_function());

    if (unit._global_main_func) {
        visit_global_main_function(*unit._global_main_func);
    }
}

void implementation_generator::visit_unit(unit &unit) {
    visit_namespace(*_unit.get_root_namespace());

    visit_global_constructor_function(_unit.get_global_constructor_function());
    visit_global_destructor_function(_unit.get_global_destructor_function());

    if (unit._global_main_func) {
        visit_global_main_function(*unit._global_main_func);
    }
}

//
// Namespace
//

void symbol_resolver::visit_namespace(ns& ns)
{
    if (ns.get_fq_name().empty()) {
        if (ns.is_root()) {
            // Root namespace
            // Should not happen, supposed to be handled at model construction level
            if (ns.get_name().empty()) {
                throw_error(0x0001, std::nullopt,
                    "Internal error: root namespace has no name at code generation stage; "
                    "this should not happen and indicates a compiler bug");
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

void type_reference_resolver::visit_namespace(ns& ns)
{
    for(auto& child : ns.get_children()) {
        child->accept(*this);
    }
    // After all children are resolved, check for overload collisions among free functions.
    check_overload_collisions(ns);
}

void declaration_generator::visit_namespace(ns &ns) {
    for(auto child : ns.get_children()) {
        child->accept(*this);
    }
}

void implementation_generator::visit_namespace(ns &ns) {
    for(auto child : ns.get_children()) {
        child->accept(*this);
    }
}

//
// Structure
//
void symbol_resolver::visit_structure(structure& st) {
    visit_named_element(st);

    // Pre declare type
    // TODO Mangle struct name to avoid collisions
    std::shared_ptr<struct_type> st_type{new struct_type(st.get_short_name()/*st.get_mangled_name()*/, st.shared_as<structure>())};
    _context->add_struct(st_type);
    st.set_struct_type(st_type);

    // Visit member variable children
    for(auto& child : st.get_children()) {
        if(auto var = std::dynamic_pointer_cast<member_variable_definition>(child)) {
            var->accept(*this);
        }
    }

    // Visit global/static variable children
    for(auto& child : st.get_children()) {
        if(auto var = std::dynamic_pointer_cast<global_variable_definition>(child)) {
            var->accept(*this);
        }
    }

    // Visit function children (includes constructors and destructor)
    for(auto& child : st.get_children()) {
        if(auto func = std::dynamic_pointer_cast<function>(child)) {
            func->accept(*this);
        }
    }

    // Add a compiler-generated default constructor if none was defined by the user.
    // It is added to both _constructors and _children so all visitors find it uniformly.
    if (st.constructors().empty()) {
        auto default_constructor = constructor::make_shared(st.shared_as<structure>());
        default_constructor->set_compiler_generated(true);
        st._constructors.push_back(default_constructor);
        st._children.push_back(default_constructor);
        default_constructor->accept(*this);
    }
}

void type_reference_resolver::visit_structure(structure& st) {
    // Visit all functions (including constructors and destructors).
    for(auto& child : st.get_children()) {
        child->accept(*this);
    }
    // After all members are resolved, check for overload collisions.
    check_overload_collisions(st);
    check_constructor_overload_collisions(st);
}

void declaration_generator::visit_structure(structure& st) {
    _struct_stack.push(st.shared_as<structure>());

    // Visit all children (variables, methods, constructors, destructor).
    for(auto& child : st.get_children()) {
        child->accept(*this);
    }

    _struct_stack.pop();
}

void implementation_generator::visit_structure(structure& st) {
    _struct_stack.push(st.shared_as<structure>());

    // Visit all children (variables, methods, constructors, destructor).
    for(auto& child : st.get_children()) {
        child->accept(*this);
    }

    _struct_stack.pop();
}


//
// Member variable definition
//
void symbol_resolver::visit_member_variable_definition(member_variable_definition& var) {
    visit_named_element(var);
    // No symbol resolution today, because only primitive types are supported today.
    // TODO Add complex member resolution.
    // TODO visit the initialization expression if any
}

void type_reference_resolver::visit_member_variable_definition(member_variable_definition& var) {
    // Do nothing for now
    // Everything is done at structure level
}

void declaration_generator::visit_member_variable_definition(member_variable_definition&) {
    // Do nothing for now
    // Everything is done at structure level
}

void implementation_generator::visit_member_variable_definition(member_variable_definition&) {
    // Do nothing for now
    // Everything is done at structure level
}


//
// Global variable definition
//

void symbol_resolver::visit_global_variable_definition(global_variable_definition& var)
{
    visit_named_element(var);

    if (auto expr = var.get_init_expr()) {
        expr->accept(*this);
    }
}

void type_reference_resolver::visit_global_variable_definition(global_variable_definition& var)
{
    visit_variable_definition(var);

    // Unconditionnally register global variable to global constructor for now, because we need to be sure it is registered before any possible use in other variable initialization expression.
    // TODO Add registering condition for trivial primitive initialization
    var.ancestor<unit>()->get_global_constructor_function().add_global_variable_definition(var.shared_as<global_variable_definition>());

    // If the variable's type is a struct with a destructor, also register it for global destruction.
    if (auto st_type = std::dynamic_pointer_cast<struct_type>(var.get_type())) {
        if (st_type->get_struct() && st_type->get_struct()->get_destructor()) {
            var.ancestor<unit>()->get_global_destructor_function().add_global_variable_definition(var.shared_as<global_variable_definition>());
        }
    }
}

void declaration_generator::visit_global_variable_definition(global_variable_definition& var) {
    auto type = var.get_type();
    llvm::Type *llvm_type = _context->get_llvm_type(type);

    auto variable = new llvm::GlobalVariable(*_context->_module, llvm_type, false, llvm::GlobalValue::ExternalLinkage, nullptr, var.get_mangled_name());
    _context->_global_vars.insert({var.shared_as<global_variable_definition>(), variable});
}


void implementation_generator::visit_global_variable_definition(global_variable_definition& var) {
    auto type = var.get_type();
    llvm::Type *llvm_type = _context->get_llvm_type(type);

    // Generate initialization
    llvm::Constant* constInitValue = nullptr;

    if (type::is_primitive(var.get_type()) && var.get_init_expr() && var.get_init_expr()->size() == 1) {
        if (auto value = std::dynamic_pointer_cast<value_expression>(var.get_init_expr()->argument(0))) {
            // Constant init expression
            if (auto constant = get_llvm_constant_from_value_expr(*value)) {
                // TODO Implement type conversion
                constInitValue = constant;
            }
        }
    }

    if (!constInitValue) {
        // If no explicit initialization, or complex initialization, lets have 0-filled initialization:
        constInitValue = type->generate_default_value_initializer();
    }

    auto variable_it = _context->_global_vars.find(var.shared_as<global_variable_definition>());
    if (variable_it == _context->_global_vars.end()) {
        // Not declared yet, should not append, but let's create it lazily anyway
        auto variable = new llvm::GlobalVariable(*_context->_module, llvm_type, false, llvm::GlobalValue::ExternalLinkage, constInitValue, var.get_mangled_name());
        _context->_global_vars.insert({var.shared_as<global_variable_definition>(), variable});
    } else {
        // Already declared, just add initializer
        variable_it->second->setInitializer(constInitValue);
    }
}

//
// Function parameter
//
void symbol_resolver::visit_parameter(parameter& param) {
    visit_named_element(param);

    if(auto expr = param.get_init_expr()) {
        expr->accept(*this);
    }
    if(auto expr = param.get_default_expr()) {
        expr->accept(*this);
    }
}

void type_reference_resolver::visit_parameter(parameter& param) {

    if (auto var_type = param.get_type(); !type::is_resolved(var_type)) {
        std::shared_ptr<type> res_type = _context->resolve_type(var_type);
        if (!type::is_resolved(res_type)) {
            throw_error(0x0001, std::nullopt,
                "Cannot resolve type for parameter '{}': the type name is unknown",
                {param.get_short_name()});
        }
        param.set_type(res_type);
    }

    if(auto expr = param.get_init_expr()) {
        expr->accept(*this);
    }

    if(auto expr = param.get_default_expr()) {
        expr->accept(*this);
        // Adapt type of default expression to parameter type
        auto cast = adapt_type(expr, param.get_type());
        if(cast && cast != expr) {
            param.set_default_expr(cast);
        }
    }
}

//
// Function
//

void symbol_resolver::visit_function(function& fn) {
    visit_named_element(fn);

    if (fn.is_member() && !fn.is_static()) {
        fn.create_this_parameter();
    }

    for(auto param : fn.parameters()) {
        param->accept(*this);
    }
    // TODO visit parameter definition (just in case default init is referencing a variable).

    if(auto block = fn.get_block()) {
        visit_block(*block);
    }
}

void symbol_resolver::visit_constructor(constructor& ctor) {
    // Before resolving the block, inject expression_statements for each explicit member
    // initializer into the beginning of the constructor block. This ensures that when
    // visit_function → visit_block visits the block, the symbol expressions inside the
    // mem-init args have a proper parent in the element hierarchy and can resolve
    // parameter references correctly.
    // Injected in struct member declaration order (as in C++), not in the list order.

    auto blck = ctor.get_block();
    auto st = ctor.get_owner();
    if (blck && st && !ctor.member_inits().empty()) {
        // Build a lookup map from member name to mem_init_spec
        std::unordered_map<std::string, const constructor::member_init_spec*> init_by_name;
        for (auto& mi : ctor.member_inits()) {
            init_by_name[mi.member_name] = &mi;
        }

        auto insert_pos = blck->begin();
        for (auto& var_entry : st->variables()) {
            if (auto var = std::dynamic_pointer_cast<member_variable_definition>(var_entry.second)) {
                auto it = init_by_name.find(var->get_short_name());
                if (it == init_by_name.end()) continue;
                const auto& mi = *it->second;

                // Clone the args so each constructor gets its own independent copy
                std::vector<std::shared_ptr<expression>> args;
                args.reserve(mi.args.size());
                for (auto& arg : mi.args) {
                    args.push_back(arg->clone());
                }
                auto init_expr = constructor_invocation_expression::make_shared(var, args);
                auto stmt = std::make_shared<expression_statement>(blck);
                stmt->set_expression(init_expr);
                insert_pos = blck->insert_statement(insert_pos, stmt);
                ++insert_pos;
            }
        }
    }

    visit_function(ctor);
}

void type_reference_resolver::visit_function(function& fn) {

    if (fn.is_member() && !fn.is_static()) {
        fn.get_this_parameter()->accept(*this);
    }

    for(auto param : fn.parameters()) {
        param->accept(*this);
    }
    // TODO visit parameter definition (just in case default init is referencing a variable).

    if(auto block = fn.get_block()) {
        visit_block(*block);
    }
}

void declaration_generator::visit_function(function &function) {
    // Parameter types:
    std::vector<llvm::Type*> param_types;
    if (function.is_member()  && !function.is_static()) {
        // First parameter is the 'this' pointer
        param_types.push_back(_context->get_llvm_type(function.get_this_parameter()->get_type()));
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
    llvm::Function *func = llvm::Function::Create(func_type, llvm::Function::ExternalLinkage, function.get_mangled_name(), *_context->_module);

    _context->_functions.insert({function.shared_as<k::model::function>(), func});

    // Declare content
    function.get_block()->accept(*this);
}

void implementation_generator::visit_function(function &function) {

    auto func_it = _context->_functions.find(function.shared_as<k::model::function>());
    if (func_it==_context->_functions.end()) {
        throw_error(0x0001, std::nullopt,
            "Internal error: LLVM function declaration not found for '{}'; "
            "the declaration pass must be run before the implementation pass",
            {function.get_fq_name()});
    }

    llvm::Function* func = func_it->second;

    // create the function content:
    llvm::BasicBlock *block = llvm::BasicBlock::Create(**_context, "entry", func);
    _builder->SetInsertPoint(block);

    // Reset per-function state
    _retval_alloca = nullptr;
    while (!_cleanup_blocks.empty()) _cleanup_blocks.pop();
    while (!_cleanup_vars_stack.empty()) _cleanup_vars_stack.pop();

    // If function has a non-void return type, pre-create an alloca for the return value
    // so that destructor calls can happen before the actual ret instruction.
    if (function.has_return_type()) {
        llvm::IRBuilder<> alloca_builder(&func->getEntryBlock(), func->getEntryBlock().begin());
        _retval_alloca = alloca_builder.CreateAlloca(
            _context->get_llvm_type(function.get_return_type()), nullptr, "retval");
    }

    // Capture arguments
    auto arg_it = func->arg_begin();
    if (function.is_member() && !function.is_static()) {
        // First parameter is the 'this' pointer
        llvm::Argument *arg = &*(arg_it++);
        arg->setName("this");
        // Create dedicated local storage for "this" argument
        llvm::AllocaInst* alloca = _builder->CreateAlloca(llvm::PointerType::get(_context->llvm_context(), 0), nullptr, "this");
        _context->_function_this_variables.insert({function.shared_as<model::function>(), alloca});
        _context->_parameter_variables.insert({function.get_this_parameter(), alloca});
        // Read "this" param value and store it in dedicated local var
        _builder->CreateStore(arg, alloca);
    }
    for(const auto& param : function.parameters()) {
        // Iterate to get all explicit parameters
        llvm::Argument *arg = &*(arg_it++);
        arg->setName(param->get_short_name());
        // Create dedicated local storage for argument
        llvm::AllocaInst* alloca = _builder->CreateAlloca(_context->get_llvm_type(param->get_type()), nullptr, param->get_short_name());
        _context->_parameter_variables.insert({param, alloca});
        // Read param value and store it in dedicated local var
        _builder->CreateStore(arg, alloca);
    }

    if (auto ctor = function.shared_as<constructor>()) {
        // For constructor, start by initializing all members
        auto this_param = _context->_function_this_variables.find(function.shared_as<model::function>())->second;
        auto st = ctor->get_owner();
        auto type = st->get_struct_type()->get_llvm_type();
        auto zero_init = llvm::ConstantAggregateZero::get(type);
        _builder->CreateStore(zero_init, _builder->CreateLoad(st->get_struct_type()->get_reference()->get_llvm_type(), this_param));
    }

    // Produce content
    function.get_block()->accept(*this);

    // Force adding a terminator as last instruction guard (will be eliminated if unreachable).
    if (function.has_return_type()) {
        llvm::Type* ret_type = _context->get_llvm_type(function.get_return_type());
        _builder->CreateRet(llvm::UndefValue::get(ret_type));
    } else {
        _builder->CreateRetVoid();
    }

    // Pre-optimize function
    optimize_function_dead_inst_elimination(*func);

    // Verify function
    llvm::verifyFunction(*func);
}

void implementation_generator::optimize_function_dead_inst_elimination(llvm::Function& func) {
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

//
// Constructor
//
void type_reference_resolver::visit_constructor(constructor& ctor) {
    auto st = ctor.get_owner();
    if (!st) {
        throw_error(0x000A, std::nullopt,
            "Internal error: constructor has no owner structure; "
            "every constructor must belong to a struct — this indicates a compiler bug");
    }

    auto blck = ctor.get_block();
    // Note : the statements for explicit member_inits were already injected by
    // symbol_resolver::visit_constructor (in struct member declaration order).
    // Here we insert fallback initialization statements for members NOT listed in the
    // mem-initializer-list, interleaved in declaration order.
    //
    // The first N statements in the block (where N = number of explicit mem-inits listed in
    // declaration order) are the already-injected ones. We walk declaration order and insert
    // missing members at the right position.

    // Build the set of member names with an explicit initializer
    std::unordered_set<std::string> explicit_init_names;
    for (auto& mi : ctor.member_inits()) {
        explicit_init_names.insert(mi.member_name);
    }

    // Walk member declaration order and insert fallback init for each unlisted member
    // at the correct position (interleaved with the already-injected explicit ones).
    // We maintain insert_pos which advances past each already-injected or newly-injected stmt.
    block::iterator insert_pos = blck->begin();
    for (auto& var_entry : st->variables()) {
        if (auto var = std::dynamic_pointer_cast<member_variable_definition>(var_entry.second)) {
            if (explicit_init_names.count(var->get_short_name()) > 0) {
                // This member has an explicit initializer already in the block: skip past it
                ++insert_pos;
            } else {
                // Not in the explicit list: use its own init_expr (if any)
                auto init_expr = var->get_init_expr();
                if (init_expr) {
                    // Clone so each constructor gets its own independent copy.
                    auto stmt = std::make_shared<expression_statement>(blck);
                    stmt->set_expression(init_expr->clone());
                    insert_pos = blck->insert_statement(insert_pos, stmt);
                    ++insert_pos;
                }
                // If no init_expr, zero-initialization covers it (done at IR level).
            }
        }
    }

    visit_function(ctor);
}

//
// Destructor
//

void type_reference_resolver::visit_destructor(destructor& dtor) {
    auto st = dtor.get_owner();
    if (!st) {
        throw_error(0x000B, std::nullopt,
            "Internal error: destructor has no owner structure; "
            "every destructor must belong to a struct — this indicates a compiler bug");
    }

    auto blck = dtor.get_block();
    // Insert calls to members' destructors at the END of the destructor block, in reverse declaration order.
    // Collect member variables that have a destructor
    std::vector<std::shared_ptr<member_variable_definition>> dtor_members;
    for (auto& var_entry : st->variables()) {
        if (auto var = std::dynamic_pointer_cast<member_variable_definition>(var_entry.second)) {
            if (auto st_type = std::dynamic_pointer_cast<struct_type>(var->get_type())) {
                if (st_type->get_struct() && st_type->get_struct()->get_destructor()) {
                    dtor_members.push_back(var);
                }
            }
        }
    }
    // Insert destructor calls in reverse order at end of block
    // (they will be appended and processed after user code — the block visitor handles ordering)
    for (auto it = dtor_members.rbegin(); it != dtor_members.rend(); ++it) {
        // Member destructor calls will be generated by implementation_generator::visit_block
        // via the destructor_invocation mechanism — for now, mark via a model expression.
        // The actual call generation happens at IR level in visit_block/visit_destructor.
        (void)*it; // placeholder – IR generation handles this
    }

    visit_function(dtor);
}

//
// Global constructor function
// This generate the unique global constructor function (if needed) and register it to llvm.global_ctors
// Note: Global constructor is processed at the end of the unit (but before global destructor)
//
void type_reference_resolver::visit_global_constructor_function(global_constructor_function& func) {
    auto vars = func.get_sorted_global_variables();
    if (!vars.empty()) {

        auto blck = func.get_block();
        for (auto var : vars) {
            auto init_expr = var->get_init_expr();
            if (init_expr) {
                auto stmt = std::make_shared<expression_statement>(blck);
                stmt->set_expression(init_expr);
                blck->append_statement(stmt);
            }
        }
        visit_function(func);
    }
}

void implementation_generator::visit_global_constructor_function(global_constructor_function& func) {
    auto vars = func.get_sorted_global_variables();
    if (!vars.empty()) {
        // Really generate the function
        visit_function(func);

        auto it_func = _context->_functions.find(func.shared_as<function>());
        if (it_func==_context->_functions.end()) {
            throw_error(0x0002, std::nullopt,
                "Internal error: global constructor function not found in LLVM function table; "
                "the declaration pass may not have run");
        }

        // Register the function
        llvm::appendToGlobalCtors(get_module(), it_func->second, 65535);
    }
}


//
// Global destructor function
// This generates the unique global destructor function (if needed) and registers it to llvm.global_dtors
// Note: Global destructor is processed at the end of the unit and after global constructor
//
void type_reference_resolver::visit_global_destructor_function(global_destructor_function& func) {
    auto vars = func.get_sorted_global_variables();
    if (!vars.empty()) {
        auto blck = func.get_block();
        // Insert destructor invocation expression_statements in REVERSE construction order.
        // We create function_invocation_expression nodes pointing to each variable's destructor.
        for (auto it = vars.rbegin(); it != vars.rend(); ++it) {
            auto& var = *it;
            if (auto st_type = std::dynamic_pointer_cast<struct_type>(var->get_type())) {
                if (auto dtor = st_type->get_struct() ? st_type->get_struct()->get_destructor() : nullptr) {
                    // Build a function_invocation_expression for: dtor(&var)
                    // The address of the global var acts as 'this'.
                    // We emit this directly at IR level in implementation_generator.
                    // For type_reference_resolver, just call visit_function to resolve types.
                }
            }
        }
        visit_function(func);
    }
}

void implementation_generator::visit_global_destructor_function(global_destructor_function& func) {
    auto vars = func.get_sorted_global_variables();

    // Collect variables with struct types that have a destructor, in reverse construction order
    std::vector<std::shared_ptr<global_variable_definition>> dtor_vars;
    for (auto it = vars.rbegin(); it != vars.rend(); ++it) {
        auto& var = *it;
        if (auto st_type = std::dynamic_pointer_cast<struct_type>(var->get_type())) {
            if (st_type->get_struct() && st_type->get_struct()->get_destructor()) {
                dtor_vars.push_back(var);
            }
        }
    }

    if (dtor_vars.empty()) {
        return;
    }

    // Generate a void() function for the global destructor
    llvm::FunctionType* func_type = llvm::FunctionType::get(llvm::Type::getVoidTy(**_context), false);
    llvm::Function* llvm_func = llvm::Function::Create(func_type, llvm::Function::ExternalLinkage,
                                                        func.get_mangled_name(), *_context->_module);
    _context->_functions.insert({func.shared_as<function>(), llvm_func});

    llvm::BasicBlock* entry = llvm::BasicBlock::Create(**_context, "entry", llvm_func);
    llvm::IRBuilder<> dtor_builder(entry);

    for (auto& var : dtor_vars) {
        auto var_it = _context->_global_vars.find(var);
        if (var_it == _context->_global_vars.end()) continue;
        llvm::GlobalVariable* global_var = var_it->second;

        auto st_type = std::dynamic_pointer_cast<struct_type>(var->get_type());
        auto dtor = st_type->get_struct()->get_destructor();
        auto dtor_it = _context->_functions.find(dtor->shared_as<function>());
        if (dtor_it == _context->_functions.end()) continue;

        // Call destructor: pass address of the global variable as 'this'
        dtor_builder.CreateCall(dtor_it->second, {global_var});
    }

    dtor_builder.CreateRetVoid();

    llvm::verifyFunction(*llvm_func);

    // Register the function with the global destructor table
    llvm::appendToGlobalDtors(get_module(), llvm_func, 65535);
}

//
// Global main function
// This generate the main entry point proxy code
//
void type_reference_resolver::visit_global_main_function(global_main_function& main_func) {

    std::vector<std::shared_ptr<expression>> args;

    // Look at the compatible prototypes
    // TODO Add a better method prototype compatibility checking/searching
    if (main_func.get_real_func().has_parameter()) {
        throw_error(0x000C, std::nullopt,
            "'main' function does not support parameters yet; "
            "declare it as 'func main() : int' or 'func main() : void'");
    }

    auto int_type = _context->from_type(primitive_type::INT);

    main_func.assign_name(name(true, "main"));
    main_func.set_return_type(int_type);
    main_func.append_parameter("argc", int_type);
    main_func.append_parameter("argv", _context->from_type(primitive_type::UNSIGNED_CHAR)->get_pointer()->get_pointer());

    auto main_block = main_func.get_block();
    auto ret_stmt = std::make_shared<model::return_statement>(main_block);

    std::shared_ptr<expression> invoke = function_invocation_expression::make_shared(main_func.get_real_func().shared_as<function>(), args);

    if (main_func.get_real_func().has_return_type()) {
        // Cast invocation result to int
        auto cast = adapt_type(invoke, int_type);
        if(!cast) {
            throw_error(0x000D, std::nullopt,
                "'main' function return type '{}' cannot be implicitly cast to 'int'; "
                "the return type must be 'int', 'void', or a type castable to 'int'",
                {main_func.get_real_func().get_return_type() ? main_func.get_real_func().get_return_type()->to_string() : "?"});
        } else if(cast != invoke) {
            // Casted, assign casted expression as return expr.
            invoke = cast;
        } else {
            // Compatible type, no need to cast.
        }
        // Return casted result
        ret_stmt->set_expression(invoke);
        main_func.get_block()->append_statement(ret_stmt);
    } else {
        // Create statement for this invocation
        auto call_stmt = std::make_shared<model::expression_statement>(main_block);
        call_stmt->set_expression(invoke);
        main_func.get_block()->append_statement(call_stmt);
        // Create return statement with returning 0
        ret_stmt->set_expression(value_expression::from_value(0));
        main_func.get_block()->append_statement(ret_stmt);
    }
}


} // namespace k::model::gen
