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
#include "gen_helpers.hpp"

#include "../model/imported.hpp"

namespace k::model::gen {

using namespace k::model;

//
// Expression temporaries cleanup
//

void implementation_generator::emit_expression_temporaries_cleanup() {
    if (_expression_temporaries.empty()) return;
    // Destroy in reverse creation order
    for (auto it = _expression_temporaries.rbegin(); it != _expression_temporaries.rend(); ++it) {
        _builder->CreateCall(it->second, {it->first});
    }
    _expression_temporaries.clear();
}


//
// Block
//

void symbol_resolver::visit_block(block& block)
{
    // Look at static/global var definitions
    for (auto var_entry : block.variables()) {
        if (auto global_var = std::dynamic_pointer_cast<global_variable_definition>(var_entry.second)) {
            global_var->accept(*this);
        }
    }
    // Statements
    for(auto& stmt : block.get_statements()) {
        stmt->accept(*this);
    }
}

void type_reference_resolver::visit_block(block& block)
{
    // Look at static/global var definitions
    for (auto var_entry : block.variables()) {
        if (auto global_var = std::dynamic_pointer_cast<global_variable_definition>(var_entry.second)) {
            global_var->accept(*this);
        }
    }
    // Statements
    for(auto& stmt : block.get_statements()) {
        stmt->accept(*this);
    }
}

void declaration_generator::visit_block(block& block) {
    // Look at static/global var definitions
    for (auto var_entry : block.variables()) {
        if (auto global_var = std::dynamic_pointer_cast<global_variable_definition>(var_entry.second)) {
            global_var->accept(*this);
        }
    }
    // Statements
    for(auto stmt : block.get_statements()) {
        stmt->accept(*this);
    }
}

void implementation_generator::visit_block(block& blk) {
    // Look at static/global var definitions
    for (auto var_entry : blk.variables()) {
        if (auto global_var = std::dynamic_pointer_cast<global_variable_definition>(var_entry.second)) {
            global_var->accept(*this);
        }
    }

    // Collect local variable_statements whose type is a struct with a destructor,
    // or an owner type (owner variables must be freed at scope exit), in declaration order.
    std::vector<std::shared_ptr<variable_statement>> dtor_vars;
    for (auto& stmt : blk.get_statements()) {
        if (auto var_stmt = std::dynamic_pointer_cast<variable_statement>(stmt)) {
            auto vt = var_stmt->get_type();
            // Struct with destructor
            if (auto st_type = std::dynamic_pointer_cast<struct_type>(vt)) {
                if (st_type->get_struct() && st_type->get_struct()->get_destructor()) {
                    dtor_vars.push_back(var_stmt);
                }
            }
            // Owner type — always needs cleanup (destroy + free on scope exit)
            if (type::is_owner(vt)) {
                dtor_vars.push_back(var_stmt);
            }
        }
    }

    const bool needs_cleanup = !dtor_vars.empty();

    llvm::Function* func = _builder->GetInsertBlock()->getParent();
    llvm::BasicBlock* cleanup_block = nullptr;
    llvm::BasicBlock* continue_block = nullptr;

    if (needs_cleanup) {
        cleanup_block  = llvm::BasicBlock::Create(**_context, "block-cleanup");
        continue_block = llvm::BasicBlock::Create(**_context, "block-continue");
        // Push cleanup block and variable list so visit_return_statement can use them
        _cleanup_blocks.push(cleanup_block);
        _cleanup_vars_stack.push(dtor_vars);
    }

    // Generate statements normally
    for (auto& stmt : blk.get_statements()) {
        stmt->accept(*this);
    }

    if (needs_cleanup) {
        // On the normal exit path, branch to cleanup
        _builder->CreateBr(cleanup_block);

        // Emit cleanup block: call destructors in REVERSE declaration order
        func->insert(func->end(), cleanup_block);
        _builder->SetInsertPoint(cleanup_block);

        for (auto it = dtor_vars.rbegin(); it != dtor_vars.rend(); ++it) {
            auto& var_stmt = *it;
            auto vt = var_stmt->get_type();

            auto var_it = _context->_variables.find(var_stmt);
            if (var_it == _context->_variables.end()) continue;
            llvm::AllocaInst* alloca = var_it->second;

            if (auto st_type = std::dynamic_pointer_cast<struct_type>(vt)) {
                // Struct destructor
                auto dtor = st_type->get_struct()->get_destructor();
                if (!dtor) continue;
                auto dtor_it = _context->_functions.find(dtor->shared_as<function>());
                if (dtor_it == _context->_functions.end()) continue;
                _builder->CreateCall(dtor_it->second, {alloca});
            } else if (auto own_type = std::dynamic_pointer_cast<owner_type>(vt)) {
                // Owner: emit conditional destroy+free (only if non-null)
                emit_owner_cleanup_if_nonnull(_builder.get(), get_module(), _context->_functions,
                    alloca, own_type->get_owned_type(), "owner_cleanup");
            }
        }

        // On normal path, branch to continue
        _builder->CreateBr(continue_block);

        // Pop cleanup entries
        _cleanup_blocks.pop();
        _cleanup_vars_stack.pop();

        // Emit continue block and set insert point there
        func->insert(func->end(), continue_block);
        _builder->SetInsertPoint(continue_block);
    }
}

//
// Return
//

void symbol_resolver::visit_return_statement(return_statement& stmt)
{
    if(auto expr = stmt.get_expression()) {
        expr->accept(*this);
    }
}

void type_reference_resolver::visit_return_statement(return_statement& stmt)
{
    auto func = stmt.get_block()->get_function();
    auto ret_type = func->get_return_type();
    // TODO check if return type is void to prevent to return sometinhg

    if(auto expr = stmt.get_expression()) {
        expr->accept(*this);
        auto cast = adapt_type(expr, ret_type);
        if(!cast) {
            throw_error(0x000E, stmt.get_ast_return_statement()->ret, "Return expression type must be compatible to the expected function return type");
        } else if(cast != expr ) {
            // Casted, assign casted expression as return expr.
            stmt.set_expression(cast);
        } else {
            // Compatible type, no need to cast.
        }
    }
}

void declaration_generator::visit_return_statement(return_statement& stmt) {
    // Nothing to do here (nothing in expressions)
}

void implementation_generator::visit_return_statement(return_statement& stmt) {

    // Evaluate the return expression first (before any destructor calls)
    llvm::Value* ret_value = nullptr;
    if (auto expr = stmt.get_expression()) {
        _value = nullptr;
        expr->accept(*this);
        ret_value = _value;
        _value = nullptr;

        if (ret_value && _retval_alloca) {
            // Store the return value so we can load it after destructor calls
            _builder->CreateStore(ret_value, _retval_alloca);
        }

        // Destroy any struct temporaries created during the return expression evaluation
        emit_expression_temporaries_cleanup();
    }

    // Emit destructor calls for all active scopes, from innermost to outermost.
    // We use a copy of the cleanup vars stack to iterate without modifying the live stack.
    if (!_cleanup_vars_stack.empty() || !_owner_params_stack.empty() || !_struct_params_stack.empty()) {
        // Collect all scope variable lists from innermost to outermost
        std::vector<std::vector<std::shared_ptr<variable_statement>>> all_scopes;
        std::stack<std::vector<std::shared_ptr<variable_statement>>> tmp = _cleanup_vars_stack;
        while (!tmp.empty()) {
            all_scopes.push_back(tmp.top());
            tmp.pop();
        }
        // Each scope: emit destructor calls in reverse declaration order
        for (auto& scope_vars : all_scopes) {
            for (auto it = scope_vars.rbegin(); it != scope_vars.rend(); ++it) {
                auto& var_stmt = *it;
                auto vt = var_stmt->get_type();

                auto var_it = _context->_variables.find(var_stmt);
                if (var_it == _context->_variables.end()) continue;
                llvm::AllocaInst* alloca = var_it->second;

                if (auto st_type = std::dynamic_pointer_cast<struct_type>(vt)) {
                    auto dtor = st_type->get_struct()->get_destructor();
                    if (!dtor) continue;
                    auto dtor_it = _context->_functions.find(dtor->shared_as<function>());
                    if (dtor_it == _context->_functions.end()) continue;
                    _builder->CreateCall(dtor_it->second, {alloca});
                } else if (auto own_type = std::dynamic_pointer_cast<owner_type>(vt)) {
                    emit_owner_cleanup_if_nonnull(_builder.get(), get_module(), _context->_functions,
                        alloca, own_type->get_owned_type(), "ret_owner");
                }
            }
        }
        // Also clean up owner-typed parameters (outermost scope, reverse order)
        if (!_owner_params_stack.empty()) {
            auto params_copy = _owner_params_stack.top(); // only outermost (function body) scope
            for (auto it = params_copy.rbegin(); it != params_copy.rend(); ++it) {
                auto& param = *it;
                auto own_type = std::dynamic_pointer_cast<owner_type>(param->get_type());
                if (!own_type) continue;
                auto param_it = _context->_parameter_variables.find(param);
                if (param_it == _context->_parameter_variables.end()) continue;
                llvm::AllocaInst* alloca = param_it->second;
                emit_owner_cleanup_if_nonnull(_builder.get(), get_module(), _context->_functions,
                    alloca, own_type->get_owned_type(), "ret_param");
            }
        }
        // Also clean up struct-typed by-value parameters (reverse order)
        if (!_struct_params_stack.empty()) {
            auto params_copy = _struct_params_stack.top();
            for (auto it = params_copy.rbegin(); it != params_copy.rend(); ++it) {
                auto& param = *it;
                auto st_type = std::dynamic_pointer_cast<struct_type>(param->get_type());
                if (!st_type || !st_type->get_struct() || !st_type->get_struct()->get_destructor()) continue;
                auto dtor = st_type->get_struct()->get_destructor();
                auto dtor_it = _context->_functions.find(dtor->shared_as<function>());
                if (dtor_it == _context->_functions.end()) continue;
                auto param_it = _context->_parameter_variables.find(param);
                if (param_it == _context->_parameter_variables.end()) continue;
                _builder->CreateCall(dtor_it->second, {param_it->second});
            }
        }
    }

    // Emit the actual ret instruction
    if (stmt.get_expression()) {
        if (_retval_alloca && ret_value) {
            // Load the stored return value (after destructors)
            llvm::Value* loaded = _builder->CreateLoad(
                _retval_alloca->getAllocatedType(), _retval_alloca, "ret_loaded");
            _builder->CreateRet(loaded);
        } else if (ret_value) {
            // No cleanup was needed, we have the value directly
            _builder->CreateRet(ret_value);
        } else {
            _builder->CreateRetVoid();
        }
    } else {
        _builder->CreateRetVoid();
    }
}

//
// If-then-else
//

void symbol_resolver::visit_if_else_statement(if_else_statement& stmt)
{
    stmt.get_test_expr()->accept(*this);

    stmt.get_then_stmt()->accept(*this);

    // Resolve else statement
    if(auto expr = stmt.get_else_stmt()) {
        expr->accept(*this);
    }
}

void type_reference_resolver::visit_if_else_statement(if_else_statement& stmt)
{
    // Resolve and cast test
    {
        auto expr = stmt.get_test_expr();
        expr->accept(*this);
        auto cast = adapt_type(expr, _context->from_type(primitive_type::BOOL));
        if(!cast) {
            throw_error(0x000F, stmt.get_ast_if_else_stmt()->if_kw, "If test expression type must be convertible to bool");
        } else if(cast != expr ) {
            // Casted, assign casted expression as return expr.
            stmt.set_test_expr(cast);
        } else {
            // Compatible type, no need to cast.
        }
    }

    // Resolve then statement
    stmt.get_then_stmt()->accept(*this);

    // Resolve else statement
    if(auto expr = stmt.get_else_stmt()) {
        expr->accept(*this);
    }
}

void declaration_generator::visit_if_else_statement(if_else_statement& stmt) {
    stmt.get_then_stmt()->accept(*this);
    if(stmt.get_else_stmt()) {
        stmt.get_else_stmt()->accept(*this);
    }
}

void implementation_generator::visit_if_else_statement(if_else_statement& stmt) {

    // Condition expression
    _value = nullptr;
    stmt.get_test_expr()->accept(*this);
    auto test_value = _value;
    _value = nullptr;

    // Destroy any struct temporaries created during condition evaluation
    emit_expression_temporaries_cleanup();

    bool has_else = (bool)stmt.get_else_stmt();

    // Retrieve current block and create then, else and continue blocks
    llvm::Function* func = _builder->GetInsertBlock()->getParent();
    llvm::BasicBlock* then_block = llvm::BasicBlock::Create(**_context, "if-then", func);
    llvm::BasicBlock* else_block = has_else ? llvm::BasicBlock::Create(**_context, "if-else") : nullptr;
    llvm::BasicBlock* cont_block = llvm::BasicBlock::Create(**_context, "if-continue");

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
        func->insert(func->end(), else_block);
        _builder->SetInsertPoint(else_block);
        stmt.get_else_stmt()->accept(*this);
        _builder->CreateBr(cont_block);
    }

    // Generate "continuation" block
    func->insert(func->end(), cont_block);
    _builder->SetInsertPoint(cont_block);
}

//
// While
//

void symbol_resolver::visit_while_statement(while_statement& stmt)
{
    stmt.get_test_expr()->accept(*this);
    stmt.get_nested_stmt()->accept(*this);
}

void type_reference_resolver::visit_while_statement(while_statement& stmt)
{
    // Resolve and cast test
    {
        auto expr = stmt.get_test_expr();
        expr->accept(*this);
        auto cast = adapt_type(expr, _context->from_type(primitive_type::BOOL));
        if(!cast) {
            throw_error(0x0010, stmt.get_ast_while_stmt()->while_kw, "While test expression type must be convertible to bool");
        } else if(cast != expr ) {
            // Casted, assign casted expression as return expr.
            stmt.set_test_expr(cast);
        } else {
            // Compatible type, no need to cast.
        }
    }

    // Resolve nested statement
    stmt.get_nested_stmt()->accept(*this);
}

void declaration_generator::visit_while_statement(while_statement& stmt) {
    stmt.get_nested_stmt()->accept(*this);
}

void implementation_generator::visit_while_statement(while_statement& stmt) {

    // Retrieve current block and create nested and continue blocks
    llvm::Function* func = _builder->GetInsertBlock()->getParent();
    llvm::BasicBlock* while_block = llvm::BasicBlock::Create(**_context, "while-condition");
    llvm::BasicBlock* nested_block = llvm::BasicBlock::Create(**_context, "while-nested");
    llvm::BasicBlock* cont_block = llvm::BasicBlock::Create(**_context, "while-continue");

    // While test block
    _builder->CreateBr(while_block);
    func->insert(func->end(), while_block);
    _builder->SetInsertPoint(while_block);

    // Condition expression
    _value = nullptr;
    stmt.get_test_expr()->accept(*this);
    auto test_value = _value;
    _value = nullptr;

    // Destroy any struct temporaries created during condition evaluation
    emit_expression_temporaries_cleanup();

    // Do branching
    _builder->CreateCondBr(test_value, nested_block, cont_block);

    // Nest block
    func->insert(func->end(), nested_block);
    _builder->SetInsertPoint(nested_block);
    stmt.get_nested_stmt()->accept(*this);

    // Go back to test
    _builder->CreateBr(while_block);

    // Generate "continuation" block
    func->insert(func->end(), cont_block);
    _builder->SetInsertPoint(cont_block);
}

//
// For
//

void symbol_resolver::visit_for_statement(for_statement& stmt)
{
    // Resolve variable decl, if any
    if(auto decl = stmt.get_decl_stmt()) {
        decl->accept(*this);
    }

    // Resolve and cast test
    if(auto expr = stmt.get_test_expr()) {
        expr->accept(*this);
    }

    // Resolve step
    if(auto step = stmt.get_step_expr()) {
        step->accept(*this);
    }

    // Resolve nested statement
    stmt.get_nested_stmt()->accept(*this);
}

void type_reference_resolver::visit_for_statement(for_statement& stmt)
{
    // Resolve variable decl, if any
    if(auto decl = stmt.get_decl_stmt()) {
        decl->accept(*this);
    }

    // Resolve and cast test
    if(auto expr = stmt.get_test_expr()) {
        expr->accept(*this);
        auto cast = adapt_type(expr, _context->from_type(primitive_type::BOOL));
        if(!cast) {
            throw_error(0x0011, stmt.get_ast_for_stmt()->for_kw, "For test expression type must be convertible to bool");
        } else if(cast != expr ) {
            // Casted, assign casted expression as return expr.
            stmt.set_test_expr(cast);
        } else {
            // Compatible type, no need to cast.
        }
    }

    // Resolve step
    if(auto step = stmt.get_step_expr()) {
        step->accept(*this);
    }

    // Resolve nested statement
    stmt.get_nested_stmt()->accept(*this);
}

void declaration_generator::visit_for_statement(for_statement& stmt) {
    stmt.get_nested_stmt()->accept(*this);
}

void implementation_generator::visit_for_statement(for_statement& stmt) {

    // Retrieve current block and create nested and continue blocks
    llvm::Function* func = _builder->GetInsertBlock()->getParent();
    llvm::BasicBlock* for_block = llvm::BasicBlock::Create(**_context, "for-condition");
    llvm::BasicBlock* nested_block = llvm::BasicBlock::Create(**_context, "for-nested");
    llvm::BasicBlock* cont_block = llvm::BasicBlock::Create(**_context, "for-continue");

    // Generate variable decl, if any
    if(auto decl = stmt.get_decl_stmt()) {
        decl->accept(*this);
    }

    // If test block
    _builder->CreateBr(for_block);
    func->insert(func->end(), for_block);
    _builder->SetInsertPoint(for_block);

    // Condition expression
    if(auto test_expr = stmt.get_test_expr()) {
        _value = nullptr;
        stmt.get_test_expr()->accept(*this);
        auto test_value = _value;
        _value = nullptr;

        // Destroy any struct temporaries created during condition evaluation
        emit_expression_temporaries_cleanup();

        // Do branching
        _builder->CreateCondBr(test_value, nested_block, cont_block);
    } else {
        _builder->CreateBr(nested_block);
    }


    // Nest block
    func->insert(func->end(), nested_block);
    _builder->SetInsertPoint(nested_block);
    stmt.get_nested_stmt()->accept(*this);

    // Step, if any
    if(auto step = stmt.get_step_expr()) {
        _value = nullptr;
        step->accept(*this);
        auto step_value = _value;
        _value = nullptr;

        // Destroy any struct temporaries created during step evaluation
        emit_expression_temporaries_cleanup();
    }

    // Go back to test
    _builder->CreateBr(for_block);

    // Generate "continuation" block
    func->insert(func->end(), cont_block);
    _builder->SetInsertPoint(cont_block);
}

//
// Expression statement
//

void symbol_resolver::visit_expression_statement(expression_statement& stmt)
{
    if(auto expr = stmt.get_expression()) {
        expr->accept(*this);
    }
}

void type_reference_resolver::visit_expression_statement(expression_statement& stmt)
{
    if(auto expr = stmt.get_expression()) {
        expr->accept(*this);

        // Warning 0x5010: if the expression produces an owner type and its result
        // is not assigned (bare expression statement), the allocated object will be
        // immediately discarded.  This covers both 'new Foo();' and a function call
        // returning T! used as a statement.
        if (type::is_owner(expr->get_type())) {
            warn(0x5010, std::nullopt,
                "Result of expression producing owner type '{}' is immediately discarded — "
                "the allocated object will be deleted right after construction",
                {expr->get_type()->to_string()});
        }
    }
}

void declaration_generator::visit_expression_statement(expression_statement& stmt) {
    // Nothing to do here (nothing in expressions)
}

void implementation_generator::visit_expression_statement(expression_statement& stmt) {
    if(auto expr = stmt.get_expression()) {
        expr->accept(*this);

        // If the expression produces an owner type (e.g. bare 'new Foo();' or a function
        // call returning T!), the result is unassigned.  Immediately destroy and free
        // the allocated object so it does not leak.
        if (_value && type::is_owner(expr->get_type())) {
            auto own_type = std::dynamic_pointer_cast<owner_type>(expr->get_type());
            if (own_type) {
                auto alloc_type = own_type->get_owned_type();
                emit_owner_object_destroy(_builder.get(), get_module(),
                    _context->_functions, _value, alloc_type);
                _value = nullptr;
            }
        }

        // Destroy any struct temporaries created during expression evaluation
        emit_expression_temporaries_cleanup();
    }
}

//
// Variable statement
//

void symbol_resolver::visit_variable_statement(variable_statement& var)
{
    visit_named_element(var);

    if (auto expr = var.get_init_expr()) {
        expr->accept(*this);
    }
}

void type_reference_resolver::visit_variable_statement(variable_statement& var)
{
    // For local variables, first try to resolve qualified types using the statement's
    // element context (which allows walking up the scope chain).
    if (!type::is_resolved(var.get_type())) {
        if (auto ufrt = std::dynamic_pointer_cast<unresolved_function_ref_type>(var.get_type())) {
            // Function reference type (*(int), ^(int), ~()): resolve using the variable's
            // element context for scope-based type lookup.
            auto resolved = resolve_function_ref_type(ufrt, static_cast<const element&>(var));
            if (resolved && type::is_resolved(resolved)) {
                var.set_type(resolved);
            }
        } else {
        auto unres_type = std::dynamic_pointer_cast<unresolved_type>(var.get_type());
        if (unres_type) {
            std::shared_ptr<type> resolved;
            if (unres_type->type_id().has_root_prefix()) {
                resolved = resolve_type_from_root(unres_type->type_id().without_root_prefix());
            } else {
                // var is a statement (element), so walk up from it
                resolved = resolve_type_by_name(unres_type->type_id(), static_cast<const element&>(var));
            }
            // Fallback: try imported aggregates
            if (!resolved || !type::is_resolved(resolved)) {
                if (auto imported_agg = _unit.get_or_create_imported_aggregate(
                        unres_type->type_id(), _context)) {
                    resolved = imported_agg->get_struct_type();
                }
            }
            // Fallback: try imported enums
            if (!resolved || !type::is_resolved(resolved)) {
                if (auto imported_en = _unit.get_or_create_imported_enum(
                        unres_type->type_id(), _context)) {
                    resolved = imported_en->get_enum_type();
                }
            }
            if (resolved && type::is_resolved(resolved)) {
                var.set_type(resolved);
            }
        }
        } // end else (not unresolved_function_ref_type)
    }
    visit_variable_definition(var);
}

void declaration_generator::visit_variable_statement(variable_statement& stmt) {
    // Nothing to do here (nothing in expressions)
}

void implementation_generator::visit_variable_statement(variable_statement& var) {
    // Create the alloca at beginning of the function ...
    auto var_func = var.get_function();
    auto func = _context->_functions[var_func];
    llvm::IRBuilder<> build(&func->getEntryBlock(),func->getEntryBlock().begin());

    std::shared_ptr<k::model::type> var_type = var.get_type();
    llvm::Type *  type = _context->get_llvm_type(var_type);
    llvm::AllocaInst* alloca = build.CreateAlloca(type, nullptr, var.get_short_name());
    _context->_variables.insert({var.shared_as<variable_statement>(), alloca});

    // But initialize at the decl place
    auto init = var.get_init_expr();
    if (k::model::type::is_owner(var_type)
        || k::model::type::is_pointer(var_type)
        || k::model::type::is_link(var_type)
        || k::model::type::is_pinned(var_type)) {
        // Indirection variables (owner/pointer/link/pin): null-initialize first, then store
        // the init expression result (an address) if present.
        auto& llvm_ctx = _builder->getContext();
        auto* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);
        _builder->CreateStore(llvm::ConstantPointerNull::get(ptr_ty), alloca);
        if (init != nullptr) {
            _value = nullptr;
            init->accept(*this);
            if (_value) {
                // For link/pin/pointer the init expression is a pointer-valued expression.
                // We need the raw pointer value to store into the alloca.
                auto init_type = init->get_type();
                llvm::Value* addr_val = _value;
                if (k::model::type::is_reference(init_type)) {
                    auto sub = std::dynamic_pointer_cast<k::model::reference_type>(init_type);
                    auto inner = sub->get_subtype();
                    if (k::model::type::is_any_indirection(inner)) {
                        // ref<ptr/link/pin/owner> → load the stored indirection value
                        llvm::Type* inner_llvm = _context->get_llvm_type(inner);
                        addr_val = _builder->CreateLoad(inner_llvm, _value, "indir_init_load");
                    }
                    // else: ref<struct T> or ref<primitive> — _value IS the alloca address,
                    // which is exactly what we want to store as the link/pin/pointer value.
                }
                _builder->CreateStore(addr_val, alloca);
            }
        }
    } else if (init != nullptr) {
        _value = nullptr;
        init->accept(*this);

        // constructor_invocation_expression handles the store for all types.
    } else if (k::model::type::is_sized_array(var_type)) {
        // int[N] without explicit initializer: zero-init the entire struct,
        // then store the capacity N in field 0.
        auto sized_arr = std::dynamic_pointer_cast<k::model::sized_array_type>(var_type);
        auto* struct_llvm = sized_arr->get_llvm_struct_type();
        // Zero-fill the alloca
        _builder->CreateStore(llvm::ConstantAggregateZero::get(struct_llvm), alloca);
        // Write the element count into field 0
        llvm::Value* size_ptr = _builder->CreateStructGEP(struct_llvm, alloca,
            k::model::sized_array_type::FIELD_SIZE, "arr_size");
        _builder->CreateStore(
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(_builder->getContext()),
                sized_arr->get_size(), false),
            size_ptr);
    } else {
        throw_error(0x0003, std::nullopt,
            "Variable '{}' has no initialisation expression; "
            "all variable declarations must have an initialiser (uninitialized variables are not yet supported)",
            {var.get_fq_name()});
    }

    // Destroy any struct temporaries created during the init expression evaluation
    emit_expression_temporaries_cleanup();

}


} // namespace k::model::gen
