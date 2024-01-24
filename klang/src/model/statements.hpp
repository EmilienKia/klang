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


#ifndef KLANG_MODEL_STATEMENTS_HPP
#define KLANG_MODEL_STATEMENTS_HPP

#include "model.hpp"

namespace k::model {


/**
 * Base statement class
 */
class statement : public element
{
protected:
    /** Statement owning the statement. */
    std::shared_ptr<statement> _parent_stmt;

    statement() = default;
    statement(const std::shared_ptr<statement>& parent_stmt) : _parent_stmt(parent_stmt) {}
    virtual ~statement() = default;

    void set_this_as_parent_to(std::shared_ptr<expression> expr);
    void set_this_as_parent_to(std::shared_ptr<statement> stmt);

public:
    void accept(model_visitor& visitor) override;

    std::shared_ptr<statement> get_parent_stmt() { return _parent_stmt; };
    std::shared_ptr<const statement> get_parent_stmt() const { return _parent_stmt; };

    virtual std::shared_ptr<variable_holder> get_variable_holder();
    virtual std::shared_ptr<const variable_holder> get_variable_holder() const;

    std::shared_ptr<block> get_block();
    std::shared_ptr<const block> get_block() const;

    virtual std::shared_ptr<function> get_function();
    virtual std::shared_ptr<const function> get_function() const;
};

/**
 * Return expression statement
 */
class return_statement : public statement
{
protected:
    friend class block;

    std::shared_ptr<expression> _expression;

    std::shared_ptr<k::parse::ast::return_statement> _ast_return_stmt;

    explicit return_statement(const std::shared_ptr<statement>& parent) :
            statement(parent) {}

public:
    return_statement() = default;
    return_statement(const std::shared_ptr<k::parse::ast::return_statement>& ast) :
            _ast_return_stmt(ast) {}

    void accept(model_visitor& visitor) override;

    void set_ast_return_statement(std::shared_ptr<k::parse::ast::return_statement> _ast_return_stmt) {
        _ast_return_stmt = _ast_return_stmt;
    }

    std::shared_ptr<const k::parse::ast::return_statement> get_ast_return_statement() const {
        return _ast_return_stmt;
    }

    std::shared_ptr<expression> get_expression() { return _expression; };
    std::shared_ptr<const expression> get_expression() const { return _expression; };

    return_statement& set_expression(std::shared_ptr<expression> expr) {
        _expression = expr;
        set_this_as_parent_to(_expression);
        return *this;
    }
};

/**
 * If then else statement
 */
class if_else_statement : public statement
{
protected:
    std::shared_ptr<k::parse::ast::if_else_statement> _ast_if_else_stmt;

    std::shared_ptr<expression> _test_expr;
    std::shared_ptr<statement> _then_stmt;
    std::shared_ptr<statement> _else_stmt;

public:
    if_else_statement() = default;
    if_else_statement(const std::shared_ptr<k::parse::ast::if_else_statement>& ast) :
            _ast_if_else_stmt(ast) {}

    void accept(model_visitor& visitor) override;

    void set_ast_if_else_stmt(const std::shared_ptr<k::parse::ast::if_else_statement> &ast_if_else_stmt) {
        _ast_if_else_stmt = ast_if_else_stmt;
    }

    const std::shared_ptr<k::parse::ast::if_else_statement> &get_ast_if_else_stmt() const {
        return _ast_if_else_stmt;
    }

    void set_test_expr(const std::shared_ptr<expression> &test_expr) {
        _test_expr = test_expr;
        set_this_as_parent_to(_test_expr);
    }

    const std::shared_ptr<expression> &get_test_expr() const {
        return _test_expr;
    }

    void set_then_stmt(const std::shared_ptr<statement> &then_stmt) {
        _then_stmt = then_stmt;
        set_this_as_parent_to(_then_stmt);
    }

    const std::shared_ptr<statement> &get_then_stmt() const {
        return _then_stmt;
    }

    void set_else_stmt(const std::shared_ptr<statement> &else_stmt) {
        _else_stmt = else_stmt;
        if(_else_stmt) {
            set_this_as_parent_to(_else_stmt);
        }
    }

    const std::shared_ptr<statement> &get_else_stmt() const {
        return _else_stmt;
    }

};


/**
 * While statement
 */
class while_statement : public statement
{
protected:
    std::shared_ptr<k::parse::ast::while_statement> _ast_while_stmt;

    std::shared_ptr<expression> _test_expr;
    std::shared_ptr<statement> _nested_stmt;

public:
    while_statement() = default;
    while_statement(const std::shared_ptr<k::parse::ast::while_statement>& ast) :
            _ast_while_stmt(ast) {}

    void accept(model_visitor& visitor) override;

    void set_ast_while_stmt(const std::shared_ptr<k::parse::ast::while_statement> &ast_while_stmt) {
        _ast_while_stmt = ast_while_stmt;
    }

    const std::shared_ptr<k::parse::ast::while_statement> &get_ast_while_stmt() const {
        return _ast_while_stmt;
    }

    void set_test_expr(const std::shared_ptr<expression> &test_expr) {
        _test_expr = test_expr;
        set_this_as_parent_to(_test_expr);
    }

    const std::shared_ptr<expression> &get_test_expr() const {
        return _test_expr;
    }

    void set_nested_stmt(const std::shared_ptr<statement> &nested_stmt) {
        _nested_stmt = nested_stmt;
        set_this_as_parent_to(_nested_stmt);
    }

    const std::shared_ptr<statement> &get_nested_stmt() const {
        return _nested_stmt;
    }

};


/**
 * For statement
 */
class for_statement : public statement , public variable_holder
{
protected:
    std::shared_ptr<k::parse::ast::for_statement> _ast_for_stmt;

    std::shared_ptr<variable_statement> _decl_stmt;
    std::shared_ptr<expression> _test_expr;
    std::shared_ptr<expression> _step_expr;
    std::shared_ptr<statement> _nested_stmt;


    /** Map of all vars defined in this block. */
    std::map<std::string, std::shared_ptr<variable_statement>> _vars;

public:
    for_statement() = default;
    for_statement(const std::shared_ptr<k::parse::ast::for_statement>& ast) :
            _ast_for_stmt(ast) {}

    void accept(model_visitor& visitor) override;

    const std::shared_ptr<k::parse::ast::for_statement> &get_ast_for_stmt() const;

    void set_ast_for_stmt(const std::shared_ptr<k::parse::ast::for_statement> &ast_for_stmt);

    const std::shared_ptr<variable_statement> &get_decl_stmt() const;

    void set_decl_stmt(const std::shared_ptr<variable_statement> &decl_stmt);

    const std::shared_ptr<expression> &get_test_expr() const;

    void set_test_expr(const std::shared_ptr<expression> &test_expr);

    const std::shared_ptr<expression> &get_step_expr() const;

    void set_step_expr(const std::shared_ptr<expression> &step_expr);

    const std::shared_ptr<statement> &get_nested_stmt() const;

    void set_nested_stmt(const std::shared_ptr<statement> &nested_stmt);

    std::shared_ptr<variable_holder> get_variable_holder() override;
    std::shared_ptr<const variable_holder> get_variable_holder() const override;

    std::shared_ptr<variable_definition> append_variable(const std::string& name) override;
    std::shared_ptr<variable_definition> get_variable(const std::string& name) override;
    std::shared_ptr<variable_definition> lookup_variable(const std::string& name) override;
};


/**
 * Expression statement
 */
class expression_statement : public statement
{
private:
    expression_statement(const std::shared_ptr<statement>& parent, const std::shared_ptr<expression>& expr) :
            statement(parent), _expression(expr) {}

    explicit expression_statement(const std::shared_ptr<statement>& parent) :
            statement(parent) {}

    std::shared_ptr<expression> _expression;
    std::shared_ptr<k::parse::ast::expression_statement> _ast_expr_stmt;


    friend class block;
    static std::shared_ptr<expression_statement> make_shared(const std::shared_ptr<statement>& parent) {
        return std::shared_ptr<expression_statement>(new expression_statement(parent));
    }

    static std::shared_ptr<expression_statement> make_shared(const std::shared_ptr<statement>& parent, std::shared_ptr<expression> expr) {
        std::shared_ptr<expression_statement> res(new expression_statement(parent, expr));
        res->set_this_as_parent_to(expr);
        return res;
    }

public:
    expression_statement() = default;
    expression_statement(const std::shared_ptr<k::parse::ast::expression_statement>& ast):
            _ast_expr_stmt(ast) {}

    void accept(model_visitor& visitor) override;

    std::shared_ptr<expression> get_expression() { return _expression; };
    std::shared_ptr<const expression> get_expression() const { return _expression; };

    expression_statement& set_expression(std::shared_ptr<expression> expr) {
        _expression = std::move(expr);
        set_this_as_parent_to(_expression);
        return *this;
    }
};

/**
 * Variable declaration statement
 */
class variable_statement : public statement, public variable_definition
{
protected:
    friend class block;
    friend class for_statement;
    friend class gen::unit_llvm_ir_gen;

    std::shared_ptr<parameter> _func_param;

    variable_statement(const std::shared_ptr<statement> &parent) :
            statement(parent) {}

    variable_statement(const std::shared_ptr<statement> &parent, const std::string& name) :
            statement(parent), variable_definition(name) {}

public:
    void accept(model_visitor& visitor) override;

    void set_as_parameter(std::shared_ptr<parameter> func_param) {
        _func_param = func_param;
    }

    std::shared_ptr<parameter> get_as_parameter() const {
        return _func_param;
    }

    bool is_parameter() const {
        return (bool)_func_param;
    }

    virtual variable_definition& set_init_expr(std::shared_ptr<expression> init_expr) override;
};


/**
 * Statement block.
 */
class block : public statement , public variable_holder {
protected:

    friend class function;

    /** Function directly holding this block, if any. */
    std::shared_ptr<function> _function;

    /** List of statements of this block. */
    std::vector<std::shared_ptr<statement>> _statements;

    /** Map of all vars defined in this block. */
    std::map<std::string, std::shared_ptr<variable_statement>> _vars;

    void set_as_parent(std::shared_ptr<function> func) {
        _function = func;
    }

public:
    block() = default;

    void accept(model_visitor& visitor) override;

    const std::vector<std::shared_ptr<statement>>& get_statements() const {
        return _statements;
    }

    std::vector<std::shared_ptr<statement>>& get_statements() {
        return _statements;
    }

    void append_statement(std::shared_ptr<statement> stmt);

    std::shared_ptr<variable_holder> get_variable_holder() override;
    std::shared_ptr<const variable_holder> get_variable_holder() const override;

    std::shared_ptr<function> get_function() override;
    std::shared_ptr<const function> get_function() const override;

    std::shared_ptr<variable_definition> append_variable(const std::string& name) override;
    std::shared_ptr<variable_definition> get_variable(const std::string& name) override;
    std::shared_ptr<variable_definition> lookup_variable(const std::string& name) override;
};


} // namespace k::model
#endif //KLANG_MODEL_STATEMENTS_HPP
