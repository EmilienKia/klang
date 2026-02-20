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

/*
 * Expression internal hierarchy:
 * expression
 * +- value_expression
 * +- symbol_expression
 * +- unary_expression
 * | +- load_value_expression
 * | +- address_of_expression
 * | +- dereference_expression
 * | +- cast_expression
 * | ... (unary operators
 * +- binary_expression
 * | ... (binary operators)
 * +- member_of_expression
 * | +- member_of_object_expression
 * | +- member_of_pointer_expression
 * +- subscript_expression
 * +- function_invocation_expression
 * +- constructor_invocation_expression
 */

#ifndef KLANG_MODEL_EXPRESSIONS_HPP
#define KLANG_MODEL_EXPRESSIONS_HPP

#include "model.hpp"

namespace k::model {

namespace gen {
class symbol_resolver;
}


/**
 * Base class for all expressions.
 */
class expression : public element {
protected:
    /** Type of the expression. */
    std::shared_ptr<type> _type = nullptr;

    virtual ~expression() = default;

    expression() = default;
    expression(std::shared_ptr<type> type) : _type(type) {}

    friend class unary_expression;
    friend class binary_expression;
    friend class member_of_expression;
    friend class function_invocation_expression;
    friend class constructor_invocation_expression;

    void set_parent_expression(const std::shared_ptr<expression> &expression) {
        set_parent(expression);
    }

    friend class gen::symbol_resolver;
    friend class gen::type_reference_resolver;

    void set_type(std::shared_ptr<type> type);

public:
    void accept(model_visitor &visitor) override;

    std::shared_ptr<type> get_type() { return _type; }
    std::shared_ptr<const type> get_type() const { return _type; }

    std::shared_ptr<statement> find_statement();
    std::shared_ptr<const statement> find_statement() const;

    std::shared_ptr<expression> get_parent_expression() { return parent<expression>(); };
    std::shared_ptr<const expression> get_parent_expression() const { return parent<expression>(); };
};

class value_expression : public expression {
protected:
    /** Value if constructed directly or already resolved from literal. */
    k::value_type _value;

    /** Source literal, if constructed from. */
    k::lex::any_literal::any_of_opt_t _literal;

    value_expression() = delete;

    value_expression(const k::lex::any_literal &literal);

public:
    void accept(model_visitor &visitor) override;

    template<typename T>
    explicit value_expression(T val) : _value(val) {}
    explicit value_expression(const std::string &str) : _value(str) {}
    explicit value_expression(std::string &&str) : _value(std::move(str)) {}

    bool is_literal() const {
        return _literal.has_value();
    }

    const lex::any_literal::any_of_opt_t &any_literal() const {
        return _literal;
    }

    const lex::literal &get_literal() const {
        return _literal.value();
    }

    void set_value(const k::value_type& value) {
        _value = value;
    }

    const k::value_type& get_value() const {
        return _value;
    }

    static std::shared_ptr<value_expression> from_literal(const k::lex::any_literal &literal);

    template<typename T>
    static std::shared_ptr<value_expression> from_value(T val) {
        return std::make_shared<value_expression>(val);
    }

    static std::shared_ptr<value_expression> from_value(const std::string &str) {
        return std::make_shared<value_expression>(str);
    }

};

class symbol_expression : public expression {
protected:
    // Name of the symbol when not resolved.
    name _name;

    std::variant<
            std::monostate, // Not resolved
            std::shared_ptr<variable_definition>,
            std::shared_ptr<function>
    > _target;

    symbol_expression(const name &name);

    symbol_expression(const std::shared_ptr<variable_definition> &var);

    symbol_expression(const std::shared_ptr<function> &func);

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<symbol_expression> from_string(const std::string &type_name);

    static std::shared_ptr<symbol_expression> from_identifier(const name &type_id);

    static std::shared_ptr<symbol_expression> from_variable(const std::shared_ptr<variable_definition>& var);

    static std::shared_ptr<symbol_expression> from_function(const std::shared_ptr<function>& func);

    const name &get_name() const {
        return _name;
    }

    bool is_variable_def() const {
        return std::holds_alternative<std::shared_ptr<variable_definition>>(_target);
    }

    bool is_function() const {
        return std::holds_alternative<std::shared_ptr<function>>(_target);
    }

    std::shared_ptr<variable_definition> get_variable_def() const {
        if (is_variable_def()) {
            return std::get<std::shared_ptr<variable_definition>>(_target);
        } else {
            return nullptr;
        }
    }

    std::shared_ptr<function> get_function() const {
        if (is_function()) {
            return std::get<std::shared_ptr<function>>(_target);
        } else {
            return nullptr;
        }
    }

    bool is_resolved() const {
        return _target.index() != 0;
    }

    void set_target(std::shared_ptr<variable_definition> var);

    void set_target(std::shared_ptr<function> func);
};

class unary_expression : public expression {
protected:
    /** Sub expression. */
    std::shared_ptr<expression> _sub_expr;
    std::shared_ptr<k::parse::ast::unary_expression> _ast_unary_expr;

    unary_expression() = default;
    unary_expression(const std::shared_ptr<expression> &sub_expr)
            : _sub_expr(sub_expr) {
        _sub_expr->set_parent_expression(shared_as<expression>());
    }

    friend class gen::symbol_resolver;
    friend class gen::type_reference_resolver;

    void assign(const std::shared_ptr<expression> &sub_expr) {
        _sub_expr = sub_expr;
        _sub_expr->set_parent_expression(shared_as<expression>());
    }

public:
    void accept(model_visitor &visitor) override;

    const std::shared_ptr<expression> &sub_expr() const {
        return _sub_expr;
    }

    std::shared_ptr<expression> &sub_expr() {
        return _sub_expr;
    }

    void set_ast_unary_expr(const std::shared_ptr<k::parse::ast::unary_expression> &expr) {
        _ast_unary_expr = expr;
    }

    const std::shared_ptr<k::parse::ast::unary_expression> &get_ast_unary_expr() const {
        return _ast_unary_expr;
    }


};

class binary_expression : public expression {
protected:
    /** Left hand sub expression. */
    std::shared_ptr<expression> _left_expr;
    /** Right hand sub expression. */
    std::shared_ptr<expression> _right_expr;

    binary_expression() = default;
    binary_expression(const std::shared_ptr<expression> &leftExpr, const std::shared_ptr<expression> &rightExpr)
            : _left_expr(leftExpr), _right_expr(rightExpr) {
        _left_expr->set_parent_expression(shared_as<expression>());
        _right_expr->set_parent_expression(shared_as<expression>());
    }

    void assign(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        _left_expr = left_expr;
        _right_expr = right_expr;
        _left_expr->set_parent_expression(shared_as<expression>());
        _right_expr->set_parent_expression(shared_as<expression>());
    }

    void assign_left(const std::shared_ptr<expression> &left_expr) {
        _left_expr = left_expr;
        _left_expr->set_parent_expression(shared_as<expression>());
    }

    friend class gen::type_reference_resolver;

    void assign_right(const std::shared_ptr<expression> &right_expr) {
        _right_expr = right_expr;
        _right_expr->set_parent_expression(shared_as<expression>());
    }

public:
    void accept(model_visitor &visitor) override;

    const std::shared_ptr<expression> &left() const {
        return _left_expr;
    }

    std::shared_ptr<expression> &left() {
        return _left_expr;
    }

    const std::shared_ptr<expression> &right() const {
        return _right_expr;
    }

    std::shared_ptr<expression> &right() {
        return _right_expr;
    }
};

/**
 * The load-value expression is an internal tool to get the real value from a reference.
 * Supposed to be injected to simplify code generation.
 * Not supposed to be used by external code.
 */
class load_value_expression : public unary_expression {
protected:
    load_value_expression() = default;

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<unary_expression> make_shared(const std::shared_ptr<expression> &sub_expr) {
        std::shared_ptr<load_value_expression> expr{new load_value_expression()};
        expr->assign(sub_expr);
        return std::shared_ptr<unary_expression>{expr};
    }
};

class address_of_expression : public unary_expression {
protected:
    address_of_expression() = default;

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<unary_expression> make_shared(const std::shared_ptr<expression> &sub_expr) {
        std::shared_ptr<address_of_expression> expr{new address_of_expression()};
        expr->assign(sub_expr);
        return std::shared_ptr<unary_expression>{expr};
    }
};

class dereference_expression : public unary_expression {
protected:
    dereference_expression() = default;

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<unary_expression> make_shared(const std::shared_ptr<expression> &sub_expr) {
        std::shared_ptr<dereference_expression> expr{new dereference_expression()};
        expr->assign(sub_expr);
        return std::shared_ptr<unary_expression>{expr};
    }
};

class member_of_expression : public unary_expression {
protected:
    std::shared_ptr<symbol_expression> _symbol;

    member_of_expression() = default;

    friend class gen::symbol_resolver;

    void assign(const std::shared_ptr<expression> &sub_expr, const std::shared_ptr<symbol_expression> &symbol_expr)
    {
        unary_expression::assign(sub_expr);
        _symbol = symbol_expr;
        _symbol->set_parent_expression(shared_as<expression>());
    }
public:
    void accept(model_visitor &visitor) override;

    const symbol_expression& symbol() const {
        return *_symbol;
    }

    symbol_expression& symbol() {
        return *_symbol;
    }
};

class member_of_object_expression : public member_of_expression {
protected:
    member_of_object_expression() = default;

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<member_of_object_expression> make_shared(const std::shared_ptr<expression> &sub_expr, const std::shared_ptr<symbol_expression>& symbol) {
        std::shared_ptr<member_of_object_expression> expr{new member_of_object_expression()};
        expr->assign(sub_expr, symbol);
        return std::shared_ptr<member_of_object_expression>{expr};
    }
};

class member_of_pointer_expression : public member_of_expression {
protected:
    member_of_pointer_expression() = default;

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<member_of_pointer_expression> make_shared(const std::shared_ptr<expression> &sub_expr, const std::shared_ptr<symbol_expression>& symbol) {
        std::shared_ptr<member_of_pointer_expression> expr{new member_of_pointer_expression()};
        expr->assign(sub_expr, symbol);
        return std::shared_ptr<member_of_pointer_expression>{expr};
    }
};

class cast_expression : public unary_expression {
protected:
    // Casting type
    std::shared_ptr<type> _cast_type;

    cast_expression() = default;

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(const std::shared_ptr<expression> &expr, const std::shared_ptr<type> &type) {
        std::shared_ptr<cast_expression> rexpr{new cast_expression()};
        rexpr->assign(expr);
        rexpr->_cast_type = type;
        return std::shared_ptr<expression>{rexpr};
    }

    std::shared_ptr<type> get_cast_type() {
        return _cast_type;
    }

    std::shared_ptr<const type> get_cast_type() const {
        return _cast_type;
    }
};

class subscript_expression : public binary_expression {
protected:
    subscript_expression() = default;

    subscript_expression(const std::shared_ptr<expression> &callee_expr,
                         const std::shared_ptr<expression> &index_expr) :
                         binary_expression(callee_expr, index_expr)
    {
    }

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<subscript_expression> expr{new subscript_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class function_invocation_expression : public expression {
protected:
    /** Callee function to call. */
    std::shared_ptr<expression> _callee_expr;
    /** Right hand sub expression. */
    std::vector<std::shared_ptr<expression>> _arguments;


    function_invocation_expression() = default;

    function_invocation_expression(const std::shared_ptr<expression> &callee_expr)
            : _callee_expr(callee_expr) {
        _callee_expr->set_parent_expression(shared_as<expression>());
    }

    function_invocation_expression(const std::shared_ptr<expression> &callee_expr,
                                   const std::shared_ptr<expression> &arg_expr)
            : _callee_expr(callee_expr) {
        _callee_expr->set_parent_expression(shared_as<expression>());
        arg_expr->set_parent_expression(shared_as<expression>());
        _arguments.push_back(arg_expr);
    }

    function_invocation_expression(const std::shared_ptr<expression> &callee_expr,
                                   const std::vector<std::shared_ptr<expression>> &args)
            : _callee_expr(callee_expr) {
        _callee_expr->set_parent_expression(shared_as<expression>());
        _arguments = args;
        for (auto &arg: args) {
            arg->set_parent_expression(shared_as<expression>());
        }
    }

public:
    const std::shared_ptr<expression> &callee_expr() const {
        return _callee_expr;
    }

    void callee_expr(const std::shared_ptr<expression> &callee) {
        _callee_expr = callee;
    }

    const std::vector<std::shared_ptr<expression>> &arguments() const {
        return _arguments;
    }

    void arguments(const std::vector<std::shared_ptr<expression>> &arguments) {
        _arguments = arguments;
    }

    void assign(const std::shared_ptr<expression> &callee_expr, const std::vector<std::shared_ptr<expression>> &args);

    void assign_argument(size_t index, const std::shared_ptr<expression> &arg);

    static std::shared_ptr<function_invocation_expression> make_shared(const std::shared_ptr<expression> &callee_expr, const std::vector<std::shared_ptr<expression>> &args);

    static std::shared_ptr<function_invocation_expression> make_shared(const std::shared_ptr<function> &callee_func, const std::vector<std::shared_ptr<expression>> &args);

public:
    void accept(model_visitor &visitor) override;

};

class constructor_invocation_expression : public expression {
protected:
    /** Object to construct. */
    std::shared_ptr<symbol_expression> _constructed_symbol;

    /** Construction argument expressions */
    std::vector<std::shared_ptr<expression>> _arguments;

    /** Constructor to call */
    std::shared_ptr<constructor> _constructor;

    constructor_invocation_expression() = default;

    constructor_invocation_expression(const std::shared_ptr<symbol_expression> &constructed_symbol,
                                   const std::vector<std::shared_ptr<expression>> &args)
            : _constructed_symbol(constructed_symbol), _arguments(args) {
        _constructed_symbol->set_parent_expression(shared_as<expression>());
        for (auto &arg: args) {
            arg->set_parent_expression(shared_as<expression>());
        }
    }

    friend class gen::type_reference_resolver;
    void set_constructor(const std::shared_ptr<constructor> &constructor) {
        _constructor = constructor;
    }

public:

    const std::shared_ptr<symbol_expression> &constructed_symbol() const {
        return _constructed_symbol;
    }

    void constructed_symbol(const std::shared_ptr<symbol_expression> &constructed_symbol) {
        _constructed_symbol = constructed_symbol;
    }

    const std::vector<std::shared_ptr<expression>> &arguments() const {
        return _arguments;
    }

    void arguments(const std::vector<std::shared_ptr<expression>> &arguments) {
        _arguments = arguments;
    }

    size_t size() const {
        return _arguments.size();
    }

    bool empty() const {
        return _arguments.empty();
    }

    std::shared_ptr<expression> argument(size_t index) {
        return _arguments[index];
    }

    void assign(const std::shared_ptr<symbol_expression> &constructed_symbol, const std::vector<std::shared_ptr<expression>> &args);

    void assign_argument(size_t index, const std::shared_ptr<expression> &arg);

    std::shared_ptr<constructor> get_constructor() const { return _constructor; }

    static std::shared_ptr<constructor_invocation_expression> make_shared(const std::shared_ptr<symbol_expression> &constructed_symbol, const std::vector<std::shared_ptr<expression>> &args);
    static std::shared_ptr<constructor_invocation_expression> make_shared(const std::shared_ptr<variable_definition> &variable, const std::vector<std::shared_ptr<expression>> &args);

    void accept(model_visitor &visitor) override;
};


} // namespace k::model
#endif //KLANG_MODEL_EXPRESSIONS_HPP
