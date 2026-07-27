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
// Break statement
//
void break_statement::accept(model_visitor &visitor) {
    visitor.visit_break_statement(*this);
}

//
// Continue statement
//
void continue_statement::accept(model_visitor &visitor) {
    visitor.visit_continue_statement(*this);
}


//
// Throw statement
//
void throw_statement::accept(model_visitor &visitor) {
    visitor.visit_throw_statement(*this);
}


//
// Catch clause
//
void catch_clause::accept(model_visitor &visitor) {
    visitor.visit_catch_clause(*this);
}

std::shared_ptr<variable_definition> catch_clause::do_create_variable(const std::string &name, bool is_static) {
    auto var = variable_statement::make_shared(shared_as<statement>(), name);
    _exception_var = var;
    return var;
}

void catch_clause::on_variable_defined(std::shared_ptr<variable_definition>) {
}

std::shared_ptr<variable_holder> catch_clause::get_variable_holder() {
    return shared_as<variable_holder>();
}

std::shared_ptr<const variable_holder> catch_clause::get_variable_holder() const {
    return shared_as<const variable_holder>();
}


//
// Try-catch statement
//
void try_catch_statement::accept(model_visitor &visitor) {
    visitor.visit_try_catch_statement(*this);
}


//
// If then else statement
//
void if_else_statement::accept(model_visitor &visitor) {
    visitor.visit_if_else_statement(*this);
}

std::shared_ptr<variable_holder> if_else_statement::get_variable_holder() {
    return shared_as<variable_holder>();
}

std::shared_ptr<const variable_holder> if_else_statement::get_variable_holder() const {
    return shared_as<const variable_holder>();
}

std::shared_ptr<variable_definition> if_else_statement::do_create_variable(const std::string &name, bool is_static) {
    if (is_static) {
        std::clog << "An if-declared variable cannot be declared static : " << name << ", ignore it" << std::endl;
    }
    return std::shared_ptr<variable_definition>(variable_statement::make_shared(shared_as<statement>(), name));
}

void if_else_statement::on_variable_defined(std::shared_ptr<variable_definition> var) {
    _cond_vars.push_back(std::dynamic_pointer_cast<variable_statement>(var));
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

std::shared_ptr<k::parse::ast::for_statement> for_statement::get_ast_for_stmt() const {
    return get_ast_node_as<k::parse::ast::for_statement>();
}

void for_statement::set_ast_for_stmt(const std::shared_ptr<k::parse::ast::for_statement> &ast_for_stmt) {
    _ast_node = ast_for_stmt;
}

const std::shared_ptr<variable_statement>& for_statement::get_decl_stmt() const {
    return _decl_stmt;
}

void for_statement::set_decl_stmt(const std::shared_ptr<variable_statement> &decl_stmt) {
    _decl_stmt = decl_stmt;
    set_this_as_parent_to(_decl_stmt);
    // Also register the variable in _vars so that symbol lookup from the test,
    // step, and body expressions can find it by walking up the parent chain.
    // This is needed when set_decl_stmt is called directly (e.g. during template
    // cloning) instead of going through append_variable+on_variable_defined.
    if (decl_stmt && !decl_stmt->get_short_name().empty()) {
        const std::string& name = decl_stmt->get_short_name();
        if (!_vars.contains(name)) {
            _vars[name] = decl_stmt;
        }
    }
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

std::shared_ptr<variable_definition> for_statement::do_create_variable(const std::string &name, bool is_static) {
    if (is_static) {
        std::clog << "A for-declared variable cannot be declared static : " << name << ", ignore it" << std::endl;
    }
    return std::shared_ptr<variable_definition>(variable_statement::make_shared(shared_as<statement>(), name));
}

void for_statement::on_variable_defined(std::shared_ptr<variable_definition> var) {
    _decl_stmt = std::dynamic_pointer_cast<variable_statement>(var);
}


//
// Foreach statement
//
void foreach_statement::accept(model_visitor &visitor) {
    visitor.visit_foreach_statement(*this);
}

std::shared_ptr<k::parse::ast::foreach_statement> foreach_statement::get_ast_foreach_stmt() const {
    return get_ast_node_as<k::parse::ast::foreach_statement>();
}

void foreach_statement::set_ast_foreach_stmt(const std::shared_ptr<k::parse::ast::foreach_statement> &ast_foreach_stmt) {
    _ast_node = ast_foreach_stmt;
}

const std::shared_ptr<variable_statement>& foreach_statement::get_loop_var() const {
    return _loop_var;
}

const std::shared_ptr<expression>& foreach_statement::get_source_expr() const {
    return _source_expr;
}

void foreach_statement::set_source_expr(const std::shared_ptr<expression> &source_expr) {
    _source_expr = source_expr;
    set_this_as_parent_to(_source_expr);
}

const std::shared_ptr<statement>& foreach_statement::get_nested_stmt() const {
    return _nested_stmt;
}

void foreach_statement::set_nested_stmt(const std::shared_ptr<statement> &nested_stmt) {
    _nested_stmt = nested_stmt;
    set_this_as_parent_to(_nested_stmt);
}

const std::shared_ptr<variable_statement>& foreach_statement::get_iterator_var() const {
    return _iterator_var;
}

void foreach_statement::set_iterator_var(const std::shared_ptr<variable_statement> &iterator_var) {
    _iterator_var = iterator_var;
}

const std::shared_ptr<variable_statement>& foreach_statement::get_source_var() const {
    return _source_var;
}

void foreach_statement::set_source_var(const std::shared_ptr<variable_statement> &source_var) {
    _source_var = source_var;
}

const std::shared_ptr<variable_statement>& foreach_statement::get_index_var() const {
    return _index_var;
}

void foreach_statement::set_index_var(const std::shared_ptr<variable_statement> &index_var) {
    _index_var = index_var;
}

const std::shared_ptr<expression>& foreach_statement::get_test_expr() const {
    return _test_expr;
}

void foreach_statement::set_test_expr(const std::shared_ptr<expression> &test_expr) {
    _test_expr = test_expr;
    set_this_as_parent_to(_test_expr);
}

const std::shared_ptr<expression>& foreach_statement::get_step_expr() const {
    return _step_expr;
}

void foreach_statement::set_step_expr(const std::shared_ptr<expression> &step_expr) {
    _step_expr = step_expr;
    set_this_as_parent_to(_step_expr);
}

const std::shared_ptr<expression>& foreach_statement::get_current_expr() const {
    return _current_expr;
}

void foreach_statement::set_current_expr(const std::shared_ptr<expression> &current_expr) {
    _current_expr = current_expr;
    set_this_as_parent_to(_current_expr);
}

std::shared_ptr<variable_holder> foreach_statement::get_variable_holder() {
    return shared_as<variable_holder>();
}

std::shared_ptr<const variable_holder> foreach_statement::get_variable_holder() const {
    return shared_as<const variable_holder>();
}

std::shared_ptr<variable_definition> foreach_statement::do_create_variable(const std::string &name, bool is_static) {
    if (is_static) {
        std::clog << "A foreach-declared variable cannot be declared static : " << name << ", ignore it" << std::endl;
    }
    return std::shared_ptr<variable_definition>(variable_statement::make_shared(shared_as<statement>(), name));
}

void foreach_statement::on_variable_defined(std::shared_ptr<variable_definition> var) {
    // The user-declared loop variable is always created first (during model
    // building). Any subsequent hidden variable created later by
    // type_reference_resolver is routed as follows:
    //   - SEQUENCE: the first hidden variable is the owned iterator (_iterator_var,
    //     created before the driver variable, since the driver's init_expr calls
    //     next() on it); the second is the driver variable (_index_var).
    //   - ARRAY: the first hidden variable is the once-bound source reference
    //     (_source_var, created before the index variable, since the test/current
    //     expressions read the array through it); the second is the driver
    //     variable (_index_var).
    //   - ITERATOR: the (only) hidden variable is the driver (_index_var).
    // This relies on set_kind(...) having already been called before the first
    // hidden variable is appended.
    auto var_stmt = std::dynamic_pointer_cast<variable_statement>(var);
    if (!_loop_var) {
        _loop_var = var_stmt;
    } else if (_kind == foreach_kind::SEQUENCE && !_iterator_var) {
        _iterator_var = var_stmt;
    } else if (_kind == foreach_kind::ARRAY && !_source_var) {
        _source_var = var_stmt;
    } else {
        _index_var = var_stmt;
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

void variable_statement::update_mangled_name() {
    // TODO Implement mangling scheme
}

void variable_statement::accept(model_visitor &visitor) {
    visitor.visit_variable_statement(*this);
}

variable_definition& variable_statement::set_init_expr(std::shared_ptr<constructor_invocation_expression> init_expr) {
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
}

block::iterator block::insert_statement(block::const_iterator pos, std::shared_ptr<statement> stmt) {
    auto it = _statements.insert(pos, stmt);
    set_this_as_parent_to(stmt);
    return it;
}

std::shared_ptr<variable_definition> block::do_create_variable(const std::string &name, bool is_static) {
    if (is_static) {
        return std::shared_ptr<variable_definition>(global_variable_definition::make_shared(shared_as<block>(), name));
    } else {
        return std::shared_ptr<variable_definition>(variable_statement::make_shared(shared_as<block>(), name));
    }
}

void block::on_variable_defined(std::shared_ptr<variable_definition> var) {
    if (auto stmt = std::dynamic_pointer_cast<variable_statement>(var)) {
        // Only save variable_statement in list of statements, static variables are not referenced here
        _statements.push_back(stmt);
    }
}



} // namespace k::model
