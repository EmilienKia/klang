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

namespace k::model::gen {

using namespace k::model;

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

    // Collect local variable_statements whose type is a struct with a destructor, in declaration order.
    std::vector<std::shared_ptr<variable_statement>> dtor_vars;
    for (auto& stmt : blk.get_statements()) {
        if (auto var_stmt = std::dynamic_pointer_cast<variable_statement>(stmt)) {
            if (auto st_type = std::dynamic_pointer_cast<struct_type>(var_stmt->get_type())) {
                if (st_type->get_struct() && st_type->get_struct()->get_destructor()) {
                    dtor_vars.push_back(var_stmt);
                }
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
            auto st_type = std::dynamic_pointer_cast<struct_type>(var_stmt->get_type());
            auto dtor = st_type->get_struct()->get_destructor();

            auto var_it = _context->_variables.find(var_stmt);
            if (var_it == _context->_variables.end()) continue;
            llvm::AllocaInst* alloca = var_it->second;

            auto dtor_it = _context->_functions.find(dtor->shared_as<function>());
            if (dtor_it == _context->_functions.end()) continue;

            _builder->CreateCall(dtor_it->second, {alloca});
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
            throw_error(0x0001, stmt.get_ast_return_statement()->ret, "Return expression type must be compatible to the expected function return type");
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
    }

    // Emit destructor calls for all active scopes, from innermost to outermost.
    // We use a copy of the cleanup vars stack to iterate without modifying the live stack.
    if (!_cleanup_vars_stack.empty()) {
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
                auto st_type = std::dynamic_pointer_cast<struct_type>(var_stmt->get_type());
                if (!st_type) continue;
                auto dtor = st_type->get_struct()->get_destructor();
                if (!dtor) continue;

                auto var_it = _context->_variables.find(var_stmt);
                if (var_it == _context->_variables.end()) continue;
                llvm::AllocaInst* alloca = var_it->second;

                auto dtor_it = _context->_functions.find(dtor->shared_as<function>());
                if (dtor_it == _context->_functions.end()) continue;

                _builder->CreateCall(dtor_it->second, {alloca});
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
            throw_error(0x0002, stmt.get_ast_if_else_stmt()->if_kw, "If test expression type must be convertible to bool");
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
            throw_error(0x0003, stmt.get_ast_while_stmt()->while_kw, "While test expression type must be convertible to bool");
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
            throw_error(0x0004, stmt.get_ast_for_stmt()->for_kw, "For test expression type must be convertible to bool");
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
    }
}

void declaration_generator::visit_expression_statement(expression_statement& stmt) {
    // Nothing to do here (nothing in expressions)
}

void implementation_generator::visit_expression_statement(expression_statement& stmt) {
    if(auto expr = stmt.get_expression()) {
        expr->accept(*this);
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
    if (init != nullptr) {
        _value = nullptr;
        init->accept(*this);
    } else {
        // TODO throw exception
        std::cerr << "Variable declaration without initialization is not supported for now : " << var.get_fq_name() << std::endl;
    }

}


} // namespace k::model::gen
