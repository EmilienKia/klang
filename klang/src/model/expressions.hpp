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

#ifndef KLANG_MODEL_EXPRESSIONS_HPP
#define KLANG_MODEL_EXPRESSIONS_HPP

#include "model.hpp"

namespace k::model {

namespace gen {
class symbol_type_resolver;
}


/**
 * Base class for all expressions.
 */
class expression : public element {
protected:
    /** Statement owning the expression. */
    std::shared_ptr<statement> _statement;
    /** Parent expression */
    std::shared_ptr<expression> _parent_expression;
    /** Type of the expression. */
    std::shared_ptr<type> _type;

    virtual ~expression() = default;

    expression() = default;

    expression(std::shared_ptr<type> type) : _type(type) {}

    friend class statement;

    void set_statement(const std::shared_ptr<statement> &statement) {
        _statement = statement;
    }

    friend class unary_expression;

    friend class binary_expression;

    friend class function_invocation_expression;

    void set_parent_expression(const std::shared_ptr<expression> &expression) {
        _parent_expression = expression;
    }

    friend class gen::symbol_type_resolver;

    void set_type(std::shared_ptr<type> type) {
        _type = type;
    }

public:
    void accept(model_visitor &visitor) override;

    std::shared_ptr<type> get_type() {
        return _type;
    }

    std::shared_ptr<const type> get_type() const {
        return _type;
    }

    std::shared_ptr<statement> get_statement() { return _statement; };

    std::shared_ptr<const statement> get_statement() const { return _statement; };

    std::shared_ptr<statement> find_statement() {
        return _statement ? _statement : _parent_expression ? _parent_expression->find_statement() : nullptr;
    };

    std::shared_ptr<const statement> find_statement() const {
        return _statement ? _statement : _parent_expression ? _parent_expression->find_statement() : nullptr;
    };

    std::shared_ptr<expression> get_parent_expression() { return _parent_expression; };

    std::shared_ptr<const expression> get_parent_expression() const { return _parent_expression; };

};


class value_expression : public expression {
protected:
    /** Value if constructed directly or already resolved from literal. */
    k::value_type _value;

    /** Source literal, if constructed from. */
    k::lex::any_literal::any_of_opt_t _literal;

    value_expression() = delete;

    value_expression(const k::lex::any_literal &literal);

    static std::shared_ptr<type> type_from_literal(const k::lex::any_literal &literal);

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
    > _symbol;

    symbol_expression(const name &name);

    symbol_expression(const std::shared_ptr<variable_definition> &var);

    symbol_expression(const std::shared_ptr<function> &func);

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<symbol_expression> from_string(const std::string &type_name);

    static std::shared_ptr<symbol_expression> from_identifier(const name &type_id);

    const name &get_name() const {
        return _name;
    }

    bool is_variable_def() const {
        return std::holds_alternative<std::shared_ptr<variable_definition>>(_symbol);
    }

    bool is_function() const {
        return std::holds_alternative<std::shared_ptr<function>>(_symbol);
    }

    std::shared_ptr<variable_definition> get_variable_def() const {
        if (is_variable_def()) {
            return std::get<std::shared_ptr<variable_definition>>(_symbol);
        } else {
            return nullptr;
        }
    }

    std::shared_ptr<function> get_function() const {
        if (is_function()) {
            return std::get<std::shared_ptr<function>>(_symbol);
        } else {
            return nullptr;
        }
    }

    bool is_resolved() const {
        return _symbol.index() != 0;
    }

    void resolve(std::shared_ptr<variable_definition> var);

    void resolve(std::shared_ptr<function> func);
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

    friend class gen::symbol_type_resolver;

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

    friend class gen::symbol_type_resolver;

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

class arithmetic_binary_expression : public binary_expression {
protected:
    arithmetic_binary_expression() = default;

public:
    void accept(model_visitor &visitor) override;
};

class addition_expression : public arithmetic_binary_expression {
protected:
    addition_expression() = default;

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<addition_expression> expr{new addition_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class substraction_expression : public arithmetic_binary_expression {
protected:
    substraction_expression() = default;

public:
    void accept(model_visitor &visitor) override;


    static std::shared_ptr<expression>
    make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<substraction_expression> expr{new substraction_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class multiplication_expression : public arithmetic_binary_expression {
protected:
    multiplication_expression() = default;

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<multiplication_expression> expr{new multiplication_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class division_expression : public arithmetic_binary_expression {
protected:
    division_expression() = default;

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<division_expression> expr{new division_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class modulo_expression : public arithmetic_binary_expression {
protected:
    modulo_expression() = default;

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<modulo_expression> expr{new modulo_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class bitwise_and_expression : public arithmetic_binary_expression {
protected:
    bitwise_and_expression() = default;

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<bitwise_and_expression> expr{new bitwise_and_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class bitwise_or_expression : public arithmetic_binary_expression {
protected:
    bitwise_or_expression() = default;

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<bitwise_or_expression> expr{new bitwise_or_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class bitwise_xor_expression : public arithmetic_binary_expression {
protected:
    bitwise_xor_expression() = default;

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<bitwise_xor_expression> expr{new bitwise_xor_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class left_shift_expression : public arithmetic_binary_expression {
protected:
    left_shift_expression() = default;

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<left_shift_expression> expr{new left_shift_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class right_shift_expression : public arithmetic_binary_expression {
protected:
    right_shift_expression() = default;

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<right_shift_expression> expr{new right_shift_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};


class assignation_expression : public binary_expression {
protected:
    assignation_expression() = default;

public:
    void accept(model_visitor &visitor) override;
};


class simple_assignation_expression : public assignation_expression {
protected:
    simple_assignation_expression() = default;

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<simple_assignation_expression> expr{new simple_assignation_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class additition_assignation_expression : public assignation_expression {
protected:
    additition_assignation_expression() = default;

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<additition_assignation_expression> expr{new additition_assignation_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class substraction_assignation_expression : public assignation_expression {
protected:
    substraction_assignation_expression() = default;

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<substraction_assignation_expression> expr{new substraction_assignation_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class multiplication_assignation_expression : public assignation_expression {
protected:
    multiplication_assignation_expression() = default;

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<multiplication_assignation_expression> expr{new multiplication_assignation_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class division_assignation_expression : public assignation_expression {
protected:
    division_assignation_expression() = default;

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<division_assignation_expression> expr{new division_assignation_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class modulo_assignation_expression : public assignation_expression {
protected:
    modulo_assignation_expression() = default;

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<modulo_assignation_expression> expr{new modulo_assignation_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class bitwise_and_assignation_expression : public assignation_expression {
protected:
    bitwise_and_assignation_expression() = default;

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<bitwise_and_assignation_expression> expr{new bitwise_and_assignation_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class bitwise_or_assignation_expression : public assignation_expression {
protected:
    bitwise_or_assignation_expression() = default;

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<bitwise_or_assignation_expression> expr{new bitwise_or_assignation_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class bitwise_xor_assignation_expression : public assignation_expression {
protected:
    bitwise_xor_assignation_expression() = default;

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<bitwise_xor_assignation_expression> expr{new bitwise_xor_assignation_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class left_shift_assignation_expression : public assignation_expression {
protected:
    left_shift_assignation_expression() = default;

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<left_shift_assignation_expression> expr{new left_shift_assignation_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class right_shift_assignation_expression : public assignation_expression {
protected:
    right_shift_assignation_expression() = default;

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<right_shift_assignation_expression> expr{new right_shift_assignation_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class arithmetic_unary_expression : public unary_expression {
protected:
    arithmetic_unary_expression() = default;

public:
    void accept(model_visitor &visitor) override;
};

class unary_plus_expression : public arithmetic_unary_expression {
protected:
    unary_plus_expression() = default;

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<unary_expression> make_shared(const std::shared_ptr<expression> &sub_expr) {
        std::shared_ptr<unary_plus_expression> expr{new unary_plus_expression()};
        expr->assign(sub_expr);
        return std::shared_ptr<unary_expression>{expr};
    }
};

class unary_minus_expression : public arithmetic_unary_expression {
protected:
    unary_minus_expression() = default;

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<unary_expression> make_shared(const std::shared_ptr<expression> &sub_expr) {
        std::shared_ptr<unary_minus_expression> expr{new unary_minus_expression()};
        expr->assign(sub_expr);
        return std::shared_ptr<unary_expression>{expr};
    }
};

class bitwise_not_expression : public arithmetic_unary_expression {
protected:
    bitwise_not_expression() = default;

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<unary_expression> make_shared(const std::shared_ptr<expression> &sub_expr) {
        std::shared_ptr<bitwise_not_expression> expr{new bitwise_not_expression()};
        expr->assign(sub_expr);
        return std::shared_ptr<unary_expression>{expr};
    }
};


class logical_binary_expression : public binary_expression {
protected:
    logical_binary_expression() = default;

public:
    void accept(model_visitor &visitor) override;
};

class logical_and_expression : public logical_binary_expression {
protected:
    logical_and_expression() = default;

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<logical_and_expression> expr{new logical_and_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class logical_or_expression : public logical_binary_expression {
protected:
    logical_or_expression() = default;

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<logical_or_expression> expr{new logical_or_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class logical_not_expression : public unary_expression {
protected:
    logical_not_expression() = default;

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<unary_expression> make_shared(const std::shared_ptr<expression> &sub_expr) {
        std::shared_ptr<logical_not_expression> expr{new logical_not_expression()};
        expr->assign(sub_expr);
        return std::shared_ptr<unary_expression>{expr};
    }
};

class comparison_expression : public binary_expression {
protected:
    comparison_expression() = default;

public:
    void accept(model_visitor &visitor) override;
};

class equal_expression : public comparison_expression {
protected:
    equal_expression() = default;

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<equal_expression> expr{new equal_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class different_expression : public comparison_expression {
protected:
    different_expression() = default;

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<different_expression> expr{new different_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class lesser_expression : public comparison_expression {
protected:
    lesser_expression() = default;

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<lesser_expression> expr{new lesser_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class greater_expression : public comparison_expression {
protected:
    greater_expression() = default;

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<greater_expression> expr{new greater_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};


class lesser_equal_expression : public comparison_expression {
protected:
    lesser_equal_expression() = default;

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<lesser_equal_expression> expr{new lesser_equal_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class greater_equal_expression : public comparison_expression {
protected:
    greater_equal_expression() = default;

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<greater_equal_expression> expr{new greater_equal_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
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

    void assign(const std::shared_ptr<expression> &callee_expr, const std::vector<std::shared_ptr<expression>> &args) {
        _callee_expr = callee_expr;
        _arguments = args;
        _callee_expr->set_parent_expression(shared_as<expression>());
        for (auto &arg: _arguments) {
            arg->set_parent_expression(shared_as<expression>());
        }
    }

    void assign_argument(size_t index, const std::shared_ptr<expression> &arg) {
        if (index >= _arguments.size()) {
            // Cannot assign aan argument out of existing arguments bound.
        } else {
            _arguments[index] = arg;
            arg->set_parent_expression(shared_as<expression>());
        }
    }

    static std::shared_ptr<expression>
    make_shared(const std::shared_ptr<expression> &callee_expr, const std::vector<std::shared_ptr<expression>> &args) {
        std::shared_ptr<function_invocation_expression> expr{new function_invocation_expression()};
        expr->assign(callee_expr, args);
        return std::shared_ptr<expression>{expr};
    }

public:
    void accept(model_visitor &visitor) override;

};


} // namespace k::model
#endif //KLANG_MODEL_EXPRESSIONS_HPP
