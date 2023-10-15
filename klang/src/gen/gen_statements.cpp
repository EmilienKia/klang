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

namespace k::model::gen {

using namespace k::model;

//
// Block
//

void symbol_type_resolver::visit_block(block& block)
{
    for(auto& stmt : block.get_statements()) {
        stmt->accept(*this);
    }
}

void unit_llvm_ir_gen::visit_block(block& block) {
    for(auto stmt : block.get_statements()) {
        stmt->accept(*this);
    }
}

//
// Return
//

void symbol_type_resolver::visit_return_statement(return_statement& stmt)
{
    auto func = stmt.get_block()->get_function();
    auto ret_type = func->return_type();
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

//
// If-then-else
//

void symbol_type_resolver::visit_if_else_statement(if_else_statement& stmt)
{
    // Resolve and cast test
    {
        auto expr = stmt.get_test_expr();
        expr->accept(*this);
        auto cast = adapt_type(expr, primitive_type::from_type(primitive_type::BOOL));
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

void unit_llvm_ir_gen::visit_if_else_statement(if_else_statement& stmt) {

    // Condition expression
    _value = nullptr;
    stmt.get_test_expr()->accept(*this);
    auto test_value = _value;
    _value = nullptr;

    bool has_else = (bool)stmt.get_else_stmt();

    // Retrieve current block and create then, else and continue blocks
    llvm::Function* func = _builder->GetInsertBlock()->getParent();
    llvm::BasicBlock* then_block = llvm::BasicBlock::Create(*_context, "if-then", func);
    llvm::BasicBlock* else_block = has_else ? llvm::BasicBlock::Create(*_context, "if-else") : nullptr;
    llvm::BasicBlock* cont_block = llvm::BasicBlock::Create(*_context, "if-continue");

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
        func->getBasicBlockList().push_back(else_block);
        _builder->SetInsertPoint(else_block);
        stmt.get_else_stmt()->accept(*this);
        _builder->CreateBr(cont_block);
    }

    // Generate "continuation" block
    func->getBasicBlockList().push_back(cont_block);
    _builder->SetInsertPoint(cont_block);
}

//
// While
//

void symbol_type_resolver::visit_while_statement(while_statement& stmt)
{
    // Resolve and cast test
    {
        auto expr = stmt.get_test_expr();
        expr->accept(*this);
        auto cast = adapt_type(expr, primitive_type::from_type(primitive_type::BOOL));
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

void unit_llvm_ir_gen::visit_while_statement(while_statement& stmt) {

    // Retrieve current block and create nested and continue blocks
    llvm::Function* func = _builder->GetInsertBlock()->getParent();
    llvm::BasicBlock* while_block = llvm::BasicBlock::Create(*_context, "while-condition");
    llvm::BasicBlock* nested_block = llvm::BasicBlock::Create(*_context, "while-nested");
    llvm::BasicBlock* cont_block = llvm::BasicBlock::Create(*_context, "while-continue");

    // While test block
    _builder->CreateBr(while_block);
    func->getBasicBlockList().push_back(while_block);
    _builder->SetInsertPoint(while_block);

    // Condition expression
    _value = nullptr;
    stmt.get_test_expr()->accept(*this);
    auto test_value = _value;
    _value = nullptr;

    // Do branching
    _builder->CreateCondBr(test_value, nested_block, cont_block);

    // Nest block
    func->getBasicBlockList().push_back(nested_block);
    _builder->SetInsertPoint(nested_block);
    stmt.get_nested_stmt()->accept(*this);

    // Go back to test
    _builder->CreateBr(while_block);

    // Generate "continuation" block
    func->getBasicBlockList().push_back(cont_block);
    _builder->SetInsertPoint(cont_block);
}

//
// For
//

void symbol_type_resolver::visit_for_statement(for_statement& stmt)
{
    // Resolve variable decl, if any
    if(auto decl = stmt.get_decl_stmt()) {
        decl->accept(*this);
    }

    // Resolve and cast test
    if(auto expr = stmt.get_test_expr()) {
        expr->accept(*this);
        auto cast = adapt_type(expr, primitive_type::from_type(primitive_type::BOOL));
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

void unit_llvm_ir_gen::visit_for_statement(for_statement& stmt) {

    // Retrieve current block and create nested and continue blocks
    llvm::Function* func = _builder->GetInsertBlock()->getParent();
    llvm::BasicBlock* for_block = llvm::BasicBlock::Create(*_context, "for-condition");
    llvm::BasicBlock* nested_block = llvm::BasicBlock::Create(*_context, "for-nested");
    llvm::BasicBlock* cont_block = llvm::BasicBlock::Create(*_context, "for-continue");

    // Generate variable decl, if any
    if(auto decl = stmt.get_decl_stmt()) {
        decl->accept(*this);
    }

    // If test block
    _builder->CreateBr(for_block);
    func->getBasicBlockList().push_back(for_block);
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
    func->getBasicBlockList().push_back(nested_block);
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
    func->getBasicBlockList().push_back(cont_block);
    _builder->SetInsertPoint(cont_block);
}

//
// Expression statement
//

void symbol_type_resolver::visit_expression_statement(expression_statement& stmt)
{
    if(auto expr = stmt.get_expression()) {
        expr->accept(*this);
    }
}

void unit_llvm_ir_gen::visit_expression_statement(expression_statement& stmt) {
    if(auto expr = stmt.get_expression()) {
        expr->accept(*this);
    }
}

//
// Variable statement
//

void symbol_type_resolver::visit_variable_statement(variable_statement& var)
{
    if(auto expr = var.get_init_expr()) {
        expr->accept(*this);

        auto cast = adapt_type(expr, var.get_type());
        if(!cast) {
// TODO            throw_error(0x0004, var.get_ast_for_stmt()->for_kw, "For test expression type must be convertible to bool");
        } else if(cast != expr) {
            // Casted, assign casted expression as return expr.
            var.set_init_expr(cast);
        } else {
            // Compatible type, no need to cast.
        }
    }
}

void unit_llvm_ir_gen::visit_variable_statement(variable_statement& var) {
    // Create the alloca at begining of the function ...
    auto var_func = var.get_function();
    auto func = _functions[var_func];
    llvm::IRBuilder<> build(&func->getEntryBlock(),func->getEntryBlock().begin());

    llvm::Type *  type = get_llvm_type(var.get_type());
    llvm::AllocaInst* alloca = build.CreateAlloca(type, nullptr, var.get_name());
    _variables.insert({var.shared_as<variable_statement>(), alloca});

    // But initialize at the decl place
    llvm::Value *value = nullptr;
    if(auto init = var.get_init_expr()) {
        _value = nullptr;
        init->accept(*this);
        value = _value;
        _value = nullptr;
    }

    // If no explicit initialization, init to 0.
    if(value == nullptr) {
        if(type::is_prim_integer(var.get_type())) {
            value = llvm::ConstantInt::get(type, 0);
        } else if(type::is_prim_bool(var.get_type())) {
            value = llvm::ConstantInt::getFalse(type);
        } else if(type::is_prim_float(var.get_type())) {
            value = llvm::ConstantFP::get(type, 0.0);
        } /* TODO add init of other types. */
    }

    if(value) {
        _builder->CreateStore(value, alloca);
    }
}


} // namespace k::model::gen
