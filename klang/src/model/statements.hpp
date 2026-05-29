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
#include "../parse/ast.hpp"

namespace k::model {

class template_instantiator;


/**
 * Base statement class
 */
class statement : public element
{
protected:
    statement() = delete;
    statement(const std::shared_ptr<statement>& parent_stmt) :
        element(parent_stmt) {}
    statement(const std::shared_ptr<element>& parent) :
        element(parent) {}
    virtual ~statement() = default;

    void set_this_as_parent_to(std::shared_ptr<expression> expr);
    void set_this_as_parent_to(std::shared_ptr<statement> stmt);

public:
    void accept(model_visitor& visitor) override;

    std::shared_ptr<statement> get_parent_stmt() { return parent<statement>(); };
    std::shared_ptr<const statement> get_parent_stmt() const { return parent<statement>(); };

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

public:
    return_statement() = delete;
    return_statement(const std::shared_ptr<statement>& parent) :
            statement(parent) {}
    return_statement(const std::shared_ptr<statement>& parent, const std::shared_ptr<k::parse::ast::return_statement>& ast) :
            statement(parent) { _ast_node = ast; }

    void accept(model_visitor& visitor) override;

    void set_ast_return_statement(std::shared_ptr<k::parse::ast::return_statement> ast) {
        _ast_node = std::move(ast);
    }

    std::shared_ptr<const k::parse::ast::return_statement> get_ast_return_statement() const {
        return get_ast_node_as<k::parse::ast::return_statement>();
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
 * Break statement — exits the innermost enclosing loop.
 */
class break_statement : public statement
{
public:
    break_statement() = delete;
    break_statement(const std::shared_ptr<statement>& parent) :
            statement(parent) {}
    break_statement(const std::shared_ptr<statement>& parent, const std::shared_ptr<k::parse::ast::break_statement>& ast) :
            statement(parent) { _ast_node = ast; }

    void accept(model_visitor& visitor) override;

    void set_ast_break_statement(std::shared_ptr<k::parse::ast::break_statement> ast) {
        _ast_node = std::move(ast);
    }

    std::shared_ptr<const k::parse::ast::break_statement> get_ast_break_statement() const {
        return get_ast_node_as<k::parse::ast::break_statement>();
    }
};

/**
 * Continue statement — jumps to the next iteration of the innermost enclosing loop.
 */
class continue_statement : public statement
{
public:
    continue_statement() = delete;
    continue_statement(const std::shared_ptr<statement>& parent) :
            statement(parent) {}
    continue_statement(const std::shared_ptr<statement>& parent, const std::shared_ptr<k::parse::ast::continue_statement>& ast) :
            statement(parent) { _ast_node = ast; }

    void accept(model_visitor& visitor) override;

    void set_ast_continue_statement(std::shared_ptr<k::parse::ast::continue_statement> ast) {
        _ast_node = std::move(ast);
    }

    std::shared_ptr<const k::parse::ast::continue_statement> get_ast_continue_statement() const {
        return get_ast_node_as<k::parse::ast::continue_statement>();
    }
};

/**
 * If then else statement
 */
class if_else_statement : public statement, public variable_holder
{
protected:
    std::shared_ptr<expression> _test_expr;
    std::shared_ptr<statement> _then_stmt;
    std::shared_ptr<statement> _else_stmt;

    /** Condition variables (if-let / if(vars; test) forms). */
    std::vector<std::shared_ptr<variable_statement>> _cond_vars;

    std::shared_ptr<variable_definition> do_create_variable(const std::string &name, bool is_static) override;
    void on_variable_defined(std::shared_ptr<variable_definition>) override;

public:
    if_else_statement() = delete;
    if_else_statement(const std::shared_ptr<statement>& parent) : statement(parent) {}
    if_else_statement(const std::shared_ptr<statement>& parent, const std::shared_ptr<k::parse::ast::if_else_statement>& ast) :
            statement(parent) { _ast_node = ast; }

    void accept(model_visitor& visitor) override;

    void set_ast_if_else_stmt(const std::shared_ptr<k::parse::ast::if_else_statement> &ast) {
        _ast_node = ast;
    }

    std::shared_ptr<k::parse::ast::if_else_statement> get_ast_if_else_stmt() const {
        return get_ast_node_as<k::parse::ast::if_else_statement>();
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

    /** Get all condition variable statements, or empty vector. */
    const std::vector<std::shared_ptr<variable_statement>> &get_cond_vars() const {
        return _cond_vars;
    }

    /** Get the single condition variable statement (for backward compat), or nullptr. */
    std::shared_ptr<variable_statement> get_cond_var() const {
        return _cond_vars.empty() ? nullptr : _cond_vars[0];
    }

    /** True when this if uses condition variable declaration(s). */
    bool has_cond_var() const { return !_cond_vars.empty(); }

    /** True when this if uses condition variable(s) and a separate test expression. */
    bool has_cond_var_with_test() const { return !_cond_vars.empty() && _test_expr != nullptr; }

    /** True when this if uses multiple condition variables without a test expression (soft-fail mode). */
    bool is_multi_var_softfail() const { return _cond_vars.size() > 1 && _test_expr == nullptr; }

    std::shared_ptr<variable_holder> get_variable_holder() override;
    std::shared_ptr<const variable_holder> get_variable_holder() const override;
};


/**
 * While statement
 */
class while_statement : public statement
{
protected:
    std::shared_ptr<expression> _test_expr;
    std::shared_ptr<statement> _nested_stmt;

public:
    while_statement() = delete;
    while_statement(const std::shared_ptr<statement>& parent) : statement(parent) {}
    while_statement(const std::shared_ptr<statement>& parent, const std::shared_ptr<k::parse::ast::while_statement>& ast) :
            statement(parent) { _ast_node = ast; }

    void accept(model_visitor& visitor) override;

    void set_ast_while_stmt(const std::shared_ptr<k::parse::ast::while_statement> &ast) {
        _ast_node = ast;
    }

    std::shared_ptr<k::parse::ast::while_statement> get_ast_while_stmt() const {
        return get_ast_node_as<k::parse::ast::while_statement>();
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
class for_statement : public statement , public variable_holder, public using_holder
{
protected:
    std::shared_ptr<variable_statement> _decl_stmt;
    std::shared_ptr<expression> _test_expr;
    std::shared_ptr<expression> _step_expr;
    std::shared_ptr<statement> _nested_stmt;


    std::shared_ptr<variable_definition> do_create_variable(const std::string &name, bool is_static) override;
    void on_variable_defined(std::shared_ptr<variable_definition>) override;

public:
    for_statement() = delete;
    for_statement(const std::shared_ptr<statement>& parent) : statement(parent) {}
    for_statement(const std::shared_ptr<statement>& parent, const std::shared_ptr<k::parse::ast::for_statement>& ast) :
            statement(parent) { _ast_node = ast; }

    void accept(model_visitor& visitor) override;

    std::shared_ptr<k::parse::ast::for_statement> get_ast_for_stmt() const;

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
};


/**
 * Expression statement
 */
class expression_statement : public statement
{
private:
    expression_statement(const std::shared_ptr<statement>& parent, const std::shared_ptr<expression>& expr) :
            statement(parent), _expression(expr) {}

    std::shared_ptr<expression> _expression;


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
    expression_statement() = delete;
    expression_statement(const std::shared_ptr<statement>& parent) : statement(parent) {}
    expression_statement(const std::shared_ptr<statement>& parent, const std::shared_ptr<k::parse::ast::expression_statement>& ast):
            statement(parent) { _ast_node = ast; }

    void accept(model_visitor& visitor) override;

    void set_ast_expression_statement(std::shared_ptr<k::parse::ast::expression_statement> ast) {
        _ast_node = std::move(ast);
    }

    std::shared_ptr<k::parse::ast::expression_statement> get_ast_expression_statement() const {
        return get_ast_node_as<k::parse::ast::expression_statement>();
    }

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
    friend class if_else_statement;
    friend class catch_clause;
    friend class gen::implementation_generator;
    friend class template_instantiator;

    std::shared_ptr<parameter> _func_param;

    variable_statement(const std::shared_ptr<statement> &parent) :
            statement(parent) {}

    static std::shared_ptr<variable_statement> make_shared(const std::shared_ptr<statement>& parent) {
        return std::shared_ptr<variable_statement>(new variable_statement(parent));
    }

    static std::shared_ptr<variable_statement> make_shared(const std::shared_ptr<statement>& parent, const std::string& name) {
        auto var_def = std::shared_ptr<variable_statement>(new variable_statement(parent));
        var_def->init(name);
        return var_def;
    }

    void update_mangled_name() override;

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

    void set_ast_variable_decl(std::shared_ptr<k::parse::ast::variable_decl> ast) {
        // variable_decl has non-virtual diamond inheritance on ast_node
        // (declaration + statement both inherit from ast_node).
        // Use aliasing constructor to store the declaration::ast_node subobject.
        if (ast) {
            k::parse::ast::ast_node* base = static_cast<k::parse::ast::declaration*>(ast.get());
            _ast_node = std::shared_ptr<k::parse::ast::ast_node>(std::move(ast), base);
        }
    }

    std::shared_ptr<k::parse::ast::variable_decl> get_ast_variable_decl() const {
        return get_ast_node_as<k::parse::ast::variable_decl>();
    }

    virtual variable_definition& set_init_expr(std::shared_ptr<constructor_invocation_expression> init_expr) override;
};


/**
 * Throw statement — throws an exception object.
 */
class throw_statement : public statement
{
protected:
    std::shared_ptr<expression> _expression;

public:
    throw_statement() = delete;
    throw_statement(const std::shared_ptr<statement>& parent) :
            statement(parent) {}
    throw_statement(const std::shared_ptr<statement>& parent, const std::shared_ptr<k::parse::ast::throw_statement>& ast) :
            statement(parent) { _ast_node = ast; }

    void accept(model_visitor& visitor) override;

    std::shared_ptr<expression> get_expression() { return _expression; }
    std::shared_ptr<const expression> get_expression() const { return _expression; }

    throw_statement& set_expression(std::shared_ptr<expression> expr) {
        _expression = expr;
        set_this_as_parent_to(_expression);
        return *this;
    }
};

/**
 * Catch clause — a single handler in a try-catch statement.
 * Declares a variable holding the caught exception reference.
 */
class catch_clause : public statement, public variable_holder
{
protected:
    friend class block;

    bool _is_const = false;
    std::shared_ptr<variable_statement> _exception_var;
    std::shared_ptr<block> _body;

    std::shared_ptr<variable_definition> do_create_variable(const std::string &name, bool is_static) override;
    void on_variable_defined(std::shared_ptr<variable_definition>) override;

public:
    catch_clause() = delete;
    catch_clause(const std::shared_ptr<statement>& parent) :
            statement(parent) {}

    void accept(model_visitor& visitor) override;

    bool is_const() const { return _is_const; }
    void set_const(bool c) { _is_const = c; }

    std::shared_ptr<variable_statement> get_exception_var() { return _exception_var; }
    std::shared_ptr<const variable_statement> get_exception_var() const { return _exception_var; }
    void set_exception_var(std::shared_ptr<variable_statement> var) { _exception_var = std::move(var); }

    std::shared_ptr<block> get_body() { return _body; }
    std::shared_ptr<const block> get_body() const { return _body; }
    void set_body(std::shared_ptr<block> body) {
        _body = std::move(body);
        if (_body) set_this_as_parent_to(std::static_pointer_cast<statement>(_body));
    }

    std::shared_ptr<variable_holder> get_variable_holder() override;
    std::shared_ptr<const variable_holder> get_variable_holder() const override;
};

/**
 * Try-catch statement — exception handling block.
 */
class try_catch_statement : public statement
{
protected:
    std::shared_ptr<block> _try_body;
    std::vector<std::shared_ptr<catch_clause>> _catch_clauses;
    std::shared_ptr<block> _finally_body;

public:
    try_catch_statement() = delete;
    try_catch_statement(const std::shared_ptr<statement>& parent) :
            statement(parent) {}
    try_catch_statement(const std::shared_ptr<statement>& parent, const std::shared_ptr<k::parse::ast::try_catch_statement>& ast) :
            statement(parent) { _ast_node = ast; }

    void accept(model_visitor& visitor) override;

    std::shared_ptr<block> get_try_body() { return _try_body; }
    std::shared_ptr<const block> get_try_body() const { return _try_body; }
    void set_try_body(std::shared_ptr<block> body) {
        _try_body = std::move(body);
        if (_try_body) set_this_as_parent_to(std::static_pointer_cast<statement>(_try_body));
    }

    const std::vector<std::shared_ptr<catch_clause>>& get_catch_clauses() const { return _catch_clauses; }
    std::vector<std::shared_ptr<catch_clause>>& get_catch_clauses() { return _catch_clauses; }
    void add_catch_clause(std::shared_ptr<catch_clause> clause) {
        _catch_clauses.push_back(std::move(clause));
    }

    std::shared_ptr<block> get_finally_body() { return _finally_body; }
    std::shared_ptr<const block> get_finally_body() const { return _finally_body; }
    void set_finally_body(std::shared_ptr<block> body) {
        _finally_body = std::move(body);
        if (_finally_body) set_this_as_parent_to(std::static_pointer_cast<statement>(_finally_body));
    }
};


/**
 * Statement block.
 */
class block : public statement , public variable_holder, public using_holder {
protected:
    friend class function;

    /** Function directly holding this block, if any. */
    std::shared_ptr<function> _function;

    /** List of statements of this block. */
    std::vector<std::shared_ptr<statement>> _statements;

    void set_as_parent(std::shared_ptr<function> func) {
        _parent = func;
        _function = func;
    }

    std::shared_ptr<variable_definition> do_create_variable(const std::string &name, bool is_static) override;
    void on_variable_defined(std::shared_ptr<variable_definition>) override;

public:
    using iterator = std::vector<std::shared_ptr<statement>>::iterator;
    using const_iterator = std::vector<std::shared_ptr<statement>>::const_iterator;

    block() = delete;
    block(const std::shared_ptr<element>& parent) : statement(parent) {}

    void accept(model_visitor& visitor) override;

    const std::vector<std::shared_ptr<statement>>& get_statements() const {
        return _statements;
    }

    std::vector<std::shared_ptr<statement>>& get_statements() {
        return _statements;
    }

    void append_statement(std::shared_ptr<statement> stmt);

    iterator insert_statement(const_iterator pos, std::shared_ptr<statement> stmt);

    iterator begin() { return _statements.begin(); }
    iterator end() { return _statements.end(); }
    const_iterator begin() const { return _statements.begin(); }
    const_iterator end() const { return _statements.end(); }

    std::shared_ptr<variable_holder> get_variable_holder() override;
    std::shared_ptr<const variable_holder> get_variable_holder() const override;

    std::shared_ptr<function> get_function() override;
    std::shared_ptr<const function> get_function() const override;

    std::shared_ptr<function> get_direct_function() {return _function;}
    std::shared_ptr<const function> get_direct_function() const {return _function;}
};


} // namespace k::model
#endif //KLANG_MODEL_STATEMENTS_HPP
