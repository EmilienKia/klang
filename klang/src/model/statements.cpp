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

#include "statements.hpp"
#include "expressions.hpp"

#include "model_visitor.hpp"

#include "../common/tools.hpp"

namespace k::model {


//
// Statement
//

void statement::set_this_as_parent_to(std::shared_ptr<expression> expr) {
    expr->set_parent(shared_as<statement>());
}

void statement::set_this_as_parent_to(std::shared_ptr<statement> stmt) {
    stmt->set_parent(shared_as<statement>());
}

void statement::accept(model_visitor &visitor) {
    visitor.visit_statement(*this);
}

std::shared_ptr<variable_holder> statement::get_variable_holder() {
    return parent<variable_holder>();
}

std::shared_ptr<const variable_holder> statement::get_variable_holder() const {
    return parent<variable_holder>();
}

std::shared_ptr<block> statement::get_block() {
    return ancestor<block>();
}

std::shared_ptr<const block> statement::get_block() const {
    return ancestor<block>();
}

std::shared_ptr<function> statement::get_function() {
    auto blk = get_block();
    return blk ? blk->get_function() : nullptr;
}

std::shared_ptr<const function> statement::get_function() const {
    auto blk = get_block();
    return blk ? blk->get_function() : nullptr;
}


//
// Return statement
//
void return_statement::accept(model_visitor &visitor) {
    visitor.visit_return_statement(*this);
}


//
// If then else statement
//
void if_else_statement::accept(model_visitor &visitor) {
    visitor.visit_if_else_statement(*this);
}

//
// While statement
//
void while_statement::accept(model_visitor &visitor) {
    visitor.visit_while_statement(*this);
}

//
// For statement
//
void for_statement::accept(model_visitor &visitor) {
    visitor.visit_for_statement(*this);
}

const std::shared_ptr<k::parse::ast::for_statement>& for_statement::get_ast_for_stmt() const {
    return _ast_for_stmt;
}

void for_statement::set_ast_for_stmt(const std::shared_ptr<k::parse::ast::for_statement> &ast_for_stmt) {
    _ast_for_stmt = ast_for_stmt;
}

const std::shared_ptr<variable_statement>& for_statement::get_decl_stmt() const {
    return _decl_stmt;
}

void for_statement::set_decl_stmt(const std::shared_ptr<variable_statement> &decl_stmt) {
    _decl_stmt = decl_stmt;
    set_this_as_parent_to(_decl_stmt);
}

const std::shared_ptr<expression>& for_statement::get_test_expr() const {
    return _test_expr;
}

void for_statement::set_test_expr(const std::shared_ptr<expression> &test_expr) {
    _test_expr = test_expr;
    set_this_as_parent_to(_test_expr);
}

const std::shared_ptr<expression>& for_statement::get_step_expr() const {
    return _step_expr;
}

void for_statement::set_step_expr(const std::shared_ptr<expression> &step_expr) {
    _step_expr = step_expr;
    set_this_as_parent_to(_step_expr);
}

const std::shared_ptr<statement>& for_statement::get_nested_stmt() const {
    return _nested_stmt;
}

void for_statement::set_nested_stmt(const std::shared_ptr<statement> &nested_stmt) {
    _nested_stmt = nested_stmt;
    set_this_as_parent_to(_nested_stmt);
}


std::shared_ptr<variable_holder> for_statement::get_variable_holder() {
    return shared_as<variable_holder>();
}

std::shared_ptr<const variable_holder> for_statement::get_variable_holder() const {
    return shared_as<const variable_holder>();
}

std::shared_ptr<variable_definition> for_statement::do_create_variable(const std::string &name) {
    return std::shared_ptr<variable_definition>(new variable_statement(_context, shared_as<statement>(), name));
}

void for_statement::on_variable_defined(std::shared_ptr<variable_definition> var) {
    _decl_stmt = std::dynamic_pointer_cast<variable_statement>(var); // Supposed to have only one var declaration for now
}

std::shared_ptr<variable_definition> for_statement::lookup_variable(const std::string &name) const {
    // TODO add qualified name lookup
    if (auto var = variable_holder::lookup_variable(name)) {
        return var;
    } else if (auto parent = get_parent_stmt()->get_variable_holder()) {
        // Has a parent variable holder, look at it
        return parent->lookup_variable(name);
    } else {
        // For statement is necessarily on a block (direct or indirect)
        return {};
    }
}


//
// Expression statement
//
void expression_statement::accept(model_visitor &visitor) {
    visitor.visit_expression_statement(*this);
}

//
// Variable statement
//
void variable_statement::accept(model_visitor &visitor) {
    visitor.visit_variable_statement(*this);
}

variable_definition& variable_statement::set_init_expr(std::shared_ptr<expression> init_expr) {
    variable_definition::set_init_expr(init_expr);
    set_this_as_parent_to(init_expr);
    return *this;
}

//
// Block
//

void block::accept(model_visitor &visitor) {
    visitor.visit_block(*this);
}

std::shared_ptr<variable_holder> block::get_variable_holder() {
    return shared_as<variable_holder>();
}

std::shared_ptr<const variable_holder> block::get_variable_holder() const {
    return shared_as<const variable_holder>();
}

std::shared_ptr<function> block::get_function() {
    if(_function) {
        return _function;
    } else if(auto parent = get_block()) {
        return parent->get_function();
    } else {
        return nullptr;
    }
}

std::shared_ptr<const function> block::get_function() const {
    if(_function) {
        return _function;
    } else if(auto parent = get_block()) {
        return parent->get_function();
    } else {
        return nullptr;
    }
}

void block::append_statement(std::shared_ptr<statement> stmt) {
    _statements.push_back(stmt);
    set_this_as_parent_to(stmt);
    // TODO add specific process for variables
}

std::shared_ptr<variable_definition> block::do_create_variable(const std::string &name) {
    return std::shared_ptr<variable_definition>(new variable_statement(_context, shared_as<block>(), name));
}

void block::on_variable_defined(std::shared_ptr<variable_definition> var) {
    _statements.push_back(std::dynamic_pointer_cast<variable_statement>(var));
}

std::shared_ptr<variable_definition> block::lookup_variable(const std::string &name) const {
    // TODO add qualified name lookup
    if (auto var = variable_holder::lookup_variable(name)) {
        return var;
    }

    if (auto parent = get_parent_stmt()) {
        if(auto var_holder = parent->get_variable_holder()) {
            // Has a parent variable holder, look at it
            return var_holder->lookup_variable(name);
        }
    }
    if(_function) {
        if (auto param = _function->get_parameter(name)) {
            // Has a parameter of same name
            return param;
        } else if (auto ns = _function->parent<variable_holder>()) {
            // Else base block of a function, look at the enclosing scope (ns)
            return ns->lookup_variable(name);
        }
    }
    return {}; // Must not happen
}


} // namespace k::model
