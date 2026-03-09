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

#include "expressions.hpp"

#include "context.hpp"
#include "model_visitor.hpp"

namespace k::model {



//
// Expression
//

void expression::set_type(std::shared_ptr<type> type) {
    _type = type;
}

void expression::accept(model_visitor &visitor) {
    visitor.visit_expression(*this);
}

std::shared_ptr<statement> expression::find_statement() {
    return ancestor<statement>();
}

std::shared_ptr<const statement> expression::find_statement() const {
    return ancestor<statement>();
};

//
// Value expression
//

value_expression::value_expression(const k::lex::any_literal &literal) :
        _literal(literal) {
}

void value_expression::accept(model_visitor &visitor) {
    visitor.visit_value_expression(*this);
}

std::shared_ptr<value_expression> value_expression::from_literal(const k::lex::any_literal &literal) {
    return std::shared_ptr<value_expression>(new value_expression(literal));
}

//
// Symbol expression
//

symbol_expression::symbol_expression(const name &name) :
        _name(name) {}

symbol_expression::symbol_expression(const std::shared_ptr<variable_definition> &var) :
        _name(var->get_short_name()),
        _target(var) {}

symbol_expression::symbol_expression(const std::shared_ptr<function> &func) :
        _name(func->get_short_name()),
        _target(func) {}


void symbol_expression::accept(model_visitor &visitor) {
    visitor.visit_symbol_expression(*this);
}

std::shared_ptr<symbol_expression> symbol_expression::from_string(const std::string &name) {
    return std::shared_ptr<symbol_expression>(new symbol_expression(name));
}

std::shared_ptr<symbol_expression> symbol_expression::from_identifier(const name &name) {
    return std::shared_ptr<symbol_expression>(new symbol_expression(name));
}

std::shared_ptr<symbol_expression> symbol_expression::from_variable(const std::shared_ptr<variable_definition>& var) {
    return std::shared_ptr<symbol_expression>(new symbol_expression(var));
}

std::shared_ptr<symbol_expression> symbol_expression::from_function(const std::shared_ptr<function>& func) {
    return std::shared_ptr<symbol_expression>(new symbol_expression(func));
}

void symbol_expression::set_target(std::shared_ptr<variable_definition> var) {
    _target = var;
}

void symbol_expression::set_target(std::shared_ptr<function> func) {
    _target = func;
}

//
// Unary expression
//
void unary_expression::accept(model_visitor &visitor) {
    visitor.visit_unary_expression(*this);
}

//
// Binary expression
//
void binary_expression::accept(model_visitor &visitor) {
    visitor.visit_binary_expression(*this);
}

//
// Load value expression
//
void load_value_expression::accept(model_visitor &visitor) {
    visitor.visit_load_value_expression(*this);
}

//
// Address of expression
//
void address_of_expression::accept(model_visitor &visitor) {
    visitor.visit_address_of_expression(*this);
}

//
// Dereference expression
//
void dereference_expression::accept(model_visitor &visitor) {
    visitor.visit_dereference_expression(*this);
}

//
// Member of expression
//
void member_of_expression::accept(model_visitor &visitor) {
    visitor.visit_member_of_expression(*this);
}

//
// Member of object expression
//
void member_of_object_expression::accept(model_visitor &visitor) {
    visitor.visit_member_of_object_expression(*this);
}

//
// Member of pointer expression
//
void member_of_pointer_expression::accept(model_visitor &visitor) {
    visitor.visit_member_of_pointer_expression(*this);
}

//
// PM expression (.* and ->*)
//
void pm_expression::accept(model_visitor &visitor) {
    visitor.visit_pm_expression(*this);
}

//
// Cast expression
//
void cast_expression::accept(model_visitor &visitor) {
    visitor.visit_cast_expression(*this);
}


//
// Subscript expression
//
void subscript_expression::accept(model_visitor &visitor) {
    visitor.visit_subscript_expression(*this);
}

//
// Function invocation expression
//
void function_invocation_expression::accept(model_visitor &visitor) {
    visitor.visit_function_invocation_expression(*this);
}

void function_invocation_expression::assign(const std::shared_ptr<expression> &callee_expr, const std::vector<std::shared_ptr<expression>> &args) {
    _callee_expr = callee_expr;
    _arguments = args;
    _callee_expr->set_parent_expression(shared_as<expression>());
    for (auto &arg: _arguments) {
        arg->set_parent_expression(shared_as<expression>());
    }
}

void function_invocation_expression::assign_argument(size_t index, const std::shared_ptr<expression> &arg) {
    if (index >= _arguments.size()) {
        // Cannot assign aan argument out of existing arguments bound.
    } else {
        _arguments[index] = arg;
        arg->set_parent_expression(shared_as<expression>());
    }
}

std::shared_ptr<function_invocation_expression> function_invocation_expression::make_shared(const std::shared_ptr<expression> &callee_expr, const std::vector<std::shared_ptr<expression>> &args) {
    std::shared_ptr<function_invocation_expression> expr{new function_invocation_expression()};
    expr->assign(callee_expr, args);
    return std::shared_ptr<function_invocation_expression>{expr};
}

std::shared_ptr<function_invocation_expression> function_invocation_expression::make_shared(const std::shared_ptr<function> &callee_func, const std::vector<std::shared_ptr<expression>> &args) {
    auto func = make_shared(symbol_expression::from_function(callee_func), args);
    func->set_type(callee_func->get_return_type());
    return func;
}

//
// Constructor invocation
//

void constructor_invocation_expression::accept(model_visitor &visitor) {
    visitor.visit_constructor_invocation_expression(*this);
}

void constructor_invocation_expression::assign(const std::shared_ptr<symbol_expression> &constructed_symbol, const std::vector<std::shared_ptr<expression>> &args) {
    _constructed_symbol = constructed_symbol;
    _arguments = args;
    _constructed_symbol->set_parent_expression(shared_as<expression>());
    for (auto &arg: _arguments) {
        arg->set_parent_expression(shared_as<expression>());
    }
}

void constructor_invocation_expression::assign_argument(size_t index, const std::shared_ptr<expression> &arg) {
    if (index >= _arguments.size()) {
        // Cannot assign aan argument out of existing arguments bound.
    } else {
        _arguments[index] = arg;
        arg->set_parent_expression(shared_as<expression>());
    }
}

std::shared_ptr<constructor_invocation_expression> constructor_invocation_expression::make_shared(const std::shared_ptr<symbol_expression> &constructed_symbol, const std::vector<std::shared_ptr<expression>> &args) {
    std::shared_ptr<constructor_invocation_expression> expr{new constructor_invocation_expression()};
    expr->assign(constructed_symbol, args);
    return std::shared_ptr<constructor_invocation_expression>{expr};
}

std::shared_ptr<constructor_invocation_expression> constructor_invocation_expression::make_shared(const std::shared_ptr<variable_definition> &variable, const std::vector<std::shared_ptr<expression>> &args) {
    auto expr = make_shared(symbol_expression::from_variable(variable), args);
    if (variable->get_type()) {
        expr->set_type(variable->get_type());
    }
    return expr;
}




} // namespace k::model
