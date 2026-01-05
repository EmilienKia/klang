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

/*
 * Expression internal hierarchy:
 * expression
 * +- value_expression
 * +- symbol_expression
 * +- unary_expression
 * | +- arithmetic_unary_expression
 * | | +- unary_plus_expression
 * | | +- unary_minus_expression
 * | | +- bitwise_not_expression
 * | +- load_value_expression
 * | +- address_of_expression
 * | +- dereference_expression
 * | +- cast_expression
 * +- binary_expression
 * | +- arithmetic_binary_expression
 * | | +- addition_expression
 * | | +- substraction_expression
 * | | +- multiplication_expression
 * | | +- division_expression
 * | | +- modulo_expression
 * | | +- bitwise_and_expression
 * | | +- bitwise_or_expression
 * | | +- bitwise_xor_expression
 * | | +- left_shift_expression
 * | | +- right_shift_expression
 * | +- assignation_expression << TODO add pointer support here
 * | | +- simple_assignation_expression
 * | | +- arithmetic_assignation_expression
 * | | | +- additition_assignation_expression
 * | | | +- substraction_assignation_expression
 * | | | +- multiplication_assignation_expression
 * | | | +- division_assignation_expression
 * | | | +- modulo_assignation_expression
 * | | | +- bitwise_and_assignation_expression
 * | | | +- bitwise_or_assignation_expression
 * | | | +- bitwise_xor_assignation_expression
 * | | | +- left_shift_assignation_expression
 * | | | +- right_shift_assignation_expression
 * | +- logical_binary_expression
 * | | +- logical_and_expression
 * | | +- logical_or_expression
 * | | +- logical_not_expression
 * | +- comparison_expression
 * | | +- equal_expression
 * | | +- different_expression
 * | | +- lesser_expression
 * | | +- greater_expression
 * | | +- lesser_equal_expression
 * | | +- greater_equal_expression
 * +- member_of_expression
 * | +- member_of_object_expression
 * | +- member_of_pointer_expression
 * +- subscript_expression
 * +- function_invocation_expression
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
    /** Type of the expression. */
    std::shared_ptr<type> _type;

    virtual ~expression() = default;

    expression() = delete;
    expression(std::shared_ptr<context> context) : element(context) {}

    expression(std::shared_ptr<context> context, std::shared_ptr<type> type) : element(context), _type(type) {}

    friend class unary_expression;
    friend class binary_expression;
    friend class member_of_expression;
    friend class function_invocation_expression;

    void set_parent_expression(const std::shared_ptr<expression> &expression) {
        set_parent(expression);
    }

    friend class gen::symbol_type_resolver;

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

    value_expression(std::shared_ptr<context> context, const k::lex::any_literal &literal);

public:
    void accept(model_visitor &visitor) override;

    template<typename T>
    explicit value_expression(std::shared_ptr<context> context, T val) : expression(context),  _value(val) {}

    explicit value_expression(std::shared_ptr<context> context, const std::string &str) : expression(context), _value(str) {}

    explicit value_expression(std::shared_ptr<context> context, std::string &&str) : expression(context), _value(std::move(str)) {}

    bool is_literal() const {
        return _literal.has_value();
    }

    const lex::any_literal::any_of_opt_t &any_literal() const {
        return _literal;
    }

    const lex::literal &get_literal() const {
        return _literal.value();
    }

    static std::shared_ptr<value_expression> from_literal(std::shared_ptr<context> context, const k::lex::any_literal &literal);

    template<typename T>
    static std::shared_ptr<value_expression> from_value(std::shared_ptr<context> context, T val) {
        return std::make_shared<value_expression>(context, val);
    }

    static std::shared_ptr<value_expression> from_value(std::shared_ptr<context> context, const std::string &str) {
        return std::make_shared<value_expression>(context, str);
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

    symbol_expression(std::shared_ptr<context> context, const name &name);

    symbol_expression(std::shared_ptr<context> context, const std::shared_ptr<variable_definition> &var);

    symbol_expression(std::shared_ptr<context> context, const std::shared_ptr<function> &func);

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<symbol_expression> from_string(std::shared_ptr<context> context, const std::string &type_name);

    static std::shared_ptr<symbol_expression> from_identifier(std::shared_ptr<context> context, const name &type_id);

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

    unary_expression() = delete;
    unary_expression(std::shared_ptr<context> context) : expression(context) {}

    unary_expression(std::shared_ptr<context> context, const std::shared_ptr<expression> &sub_expr)
            : expression(context), _sub_expr(sub_expr) {
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

    binary_expression() = delete;
    binary_expression(std::shared_ptr<context> context) : expression(context) {}

    binary_expression(std::shared_ptr<context> context, const std::shared_ptr<expression> &leftExpr, const std::shared_ptr<expression> &rightExpr)
            : expression(context), _left_expr(leftExpr), _right_expr(rightExpr) {
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
    arithmetic_binary_expression() = delete;
    arithmetic_binary_expression(std::shared_ptr<context> context) : binary_expression(context){}

public:
    void accept(model_visitor &visitor) override;
};

class addition_expression : public arithmetic_binary_expression {
protected:
    addition_expression() = delete;
    addition_expression(std::shared_ptr<context> context) : arithmetic_binary_expression(context){}

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(std::shared_ptr<context> context, const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<addition_expression> expr{new addition_expression(context)};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class substraction_expression : public arithmetic_binary_expression {
protected:
    substraction_expression() = delete;
    substraction_expression(std::shared_ptr<context> context) : arithmetic_binary_expression(context){}

public:
    void accept(model_visitor &visitor) override;


    static std::shared_ptr<expression>
    make_shared(std::shared_ptr<context> context, const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<substraction_expression> expr{new substraction_expression(context)};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class multiplication_expression : public arithmetic_binary_expression {
protected:
    multiplication_expression() = delete;
    multiplication_expression(std::shared_ptr<context> context) : arithmetic_binary_expression(context){}

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(std::shared_ptr<context> context, const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<multiplication_expression> expr{new multiplication_expression(context)};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class division_expression : public arithmetic_binary_expression {
protected:
    division_expression() = delete;
    division_expression(std::shared_ptr<context> context) : arithmetic_binary_expression(context) {}

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(std::shared_ptr<context> context, const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<division_expression> expr{new division_expression(context)};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class modulo_expression : public arithmetic_binary_expression {
protected:
    modulo_expression() = default;
    modulo_expression(std::shared_ptr<context> context) : arithmetic_binary_expression(context) {}

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(std::shared_ptr<context> context, const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<modulo_expression> expr{new modulo_expression(context)};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class bitwise_and_expression : public arithmetic_binary_expression {
protected:
    bitwise_and_expression() = delete;
    bitwise_and_expression(std::shared_ptr<context> context) : arithmetic_binary_expression(context) {}

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(std::shared_ptr<context> context, const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<bitwise_and_expression> expr{new bitwise_and_expression(context)};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class bitwise_or_expression : public arithmetic_binary_expression {
protected:
    bitwise_or_expression() = delete;
    bitwise_or_expression(std::shared_ptr<context> context) : arithmetic_binary_expression(context) {}

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(std::shared_ptr<context> context, const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<bitwise_or_expression> expr{new bitwise_or_expression(context)};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class bitwise_xor_expression : public arithmetic_binary_expression {
protected:
    bitwise_xor_expression() = delete;
    bitwise_xor_expression(std::shared_ptr<context> context) : arithmetic_binary_expression(context) {}

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(std::shared_ptr<context> context, const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<bitwise_xor_expression> expr{new bitwise_xor_expression(context)};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class left_shift_expression : public arithmetic_binary_expression {
protected:
    left_shift_expression() = delete;
    left_shift_expression(std::shared_ptr<context> context) : arithmetic_binary_expression(context) {}

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(std::shared_ptr<context> context, const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<left_shift_expression> expr{new left_shift_expression(context)};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class right_shift_expression : public arithmetic_binary_expression {
protected:
    right_shift_expression() = delete;
    right_shift_expression(std::shared_ptr<context> context) : arithmetic_binary_expression(context) {}

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(std::shared_ptr<context> context, const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<right_shift_expression> expr{new right_shift_expression(context)};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};


class assignation_expression : public binary_expression {
protected:
    assignation_expression() = delete;
    assignation_expression(std::shared_ptr<context> context) : binary_expression(context) {}

public:
    void accept(model_visitor &visitor) override;
};


class simple_assignation_expression : public assignation_expression {
protected:
    simple_assignation_expression() = delete;
    simple_assignation_expression(std::shared_ptr<context> context) : assignation_expression(context) {}

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(std::shared_ptr<context> context, const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<simple_assignation_expression> expr{new simple_assignation_expression(context)};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class arithmetic_assignation_expression : public assignation_expression {
protected:
    arithmetic_assignation_expression() = delete;
    arithmetic_assignation_expression(std::shared_ptr<context> context) : assignation_expression(context) {}

public:
    void accept(model_visitor &visitor) override;
};


class additition_assignation_expression : public arithmetic_assignation_expression {
protected:
    additition_assignation_expression() = delete;
    additition_assignation_expression(std::shared_ptr<context> context) : arithmetic_assignation_expression(context) {}

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(std::shared_ptr<context> context, const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<additition_assignation_expression> expr{new additition_assignation_expression(context)};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class substraction_assignation_expression : public arithmetic_assignation_expression {
protected:
    substraction_assignation_expression() = delete;
    substraction_assignation_expression(std::shared_ptr<context> context) : arithmetic_assignation_expression(context) {}

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(std::shared_ptr<context> context, const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<substraction_assignation_expression> expr{new substraction_assignation_expression(context)};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class multiplication_assignation_expression : public arithmetic_assignation_expression {
protected:
    multiplication_assignation_expression() = delete;
    multiplication_assignation_expression(std::shared_ptr<context> context) : arithmetic_assignation_expression(context) {}

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(std::shared_ptr<context> context, const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<multiplication_assignation_expression> expr{new multiplication_assignation_expression(context)};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class division_assignation_expression : public arithmetic_assignation_expression {
protected:
    division_assignation_expression() = delete;
    division_assignation_expression(std::shared_ptr<context> context) : arithmetic_assignation_expression(context) {}

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(std::shared_ptr<context> context, const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<division_assignation_expression> expr{new division_assignation_expression(context)};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class modulo_assignation_expression : public arithmetic_assignation_expression {
protected:
    modulo_assignation_expression() = delete;
    modulo_assignation_expression(std::shared_ptr<context> context) : arithmetic_assignation_expression(context) {}

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(std::shared_ptr<context> context, const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<modulo_assignation_expression> expr{new modulo_assignation_expression(context)};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class bitwise_and_assignation_expression : public arithmetic_assignation_expression {
protected:
    bitwise_and_assignation_expression() = delete;
    bitwise_and_assignation_expression(std::shared_ptr<context> context) : arithmetic_assignation_expression(context) {}

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(std::shared_ptr<context> context, const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<bitwise_and_assignation_expression> expr{new bitwise_and_assignation_expression(context)};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class bitwise_or_assignation_expression : public arithmetic_assignation_expression {
protected:
    bitwise_or_assignation_expression() = delete;
    bitwise_or_assignation_expression(std::shared_ptr<context> context) : arithmetic_assignation_expression(context) {}

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(std::shared_ptr<context> context, const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<bitwise_or_assignation_expression> expr{new bitwise_or_assignation_expression(context)};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class bitwise_xor_assignation_expression : public arithmetic_assignation_expression {
protected:
    bitwise_xor_assignation_expression() = delete;
    bitwise_xor_assignation_expression(std::shared_ptr<context> context) : arithmetic_assignation_expression(context) {}

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(std::shared_ptr<context> context, const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<bitwise_xor_assignation_expression> expr{new bitwise_xor_assignation_expression(context)};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class left_shift_assignation_expression : public arithmetic_assignation_expression {
protected:
    left_shift_assignation_expression() = delete;
    left_shift_assignation_expression(std::shared_ptr<context> context) : arithmetic_assignation_expression(context) {}

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(std::shared_ptr<context> context, const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<left_shift_assignation_expression> expr{new left_shift_assignation_expression(context)};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class right_shift_assignation_expression : public arithmetic_assignation_expression {
protected:
    right_shift_assignation_expression() = default;
    right_shift_assignation_expression(std::shared_ptr<context> context) : arithmetic_assignation_expression(context) {}

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(std::shared_ptr<context> context, const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<right_shift_assignation_expression> expr{new right_shift_assignation_expression(context)};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class arithmetic_unary_expression : public unary_expression {
protected:
    arithmetic_unary_expression() = delete;
    arithmetic_unary_expression(std::shared_ptr<context> context) : unary_expression(context) {}

public:
    void accept(model_visitor &visitor) override;
};

class unary_plus_expression : public arithmetic_unary_expression {
protected:
    unary_plus_expression() = delete;
    unary_plus_expression(std::shared_ptr<context> context) : arithmetic_unary_expression(context) {}

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<unary_expression> make_shared(std::shared_ptr<context> context, const std::shared_ptr<expression> &sub_expr) {
        std::shared_ptr<unary_plus_expression> expr{new unary_plus_expression(context)};
        expr->assign(sub_expr);
        return std::shared_ptr<unary_expression>{expr};
    }
};

class unary_minus_expression : public arithmetic_unary_expression {
protected:
    unary_minus_expression() = delete;
    unary_minus_expression(std::shared_ptr<context> context) : arithmetic_unary_expression(context) {}

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<unary_expression> make_shared(std::shared_ptr<context> context, const std::shared_ptr<expression> &sub_expr) {
        std::shared_ptr<unary_minus_expression> expr{new unary_minus_expression(context)};
        expr->assign(sub_expr);
        return std::shared_ptr<unary_expression>{expr};
    }
};

class bitwise_not_expression : public arithmetic_unary_expression {
protected:
    bitwise_not_expression() = delete;
    bitwise_not_expression(std::shared_ptr<context> context) : arithmetic_unary_expression(context) {}

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<unary_expression> make_shared(std::shared_ptr<context> context, const std::shared_ptr<expression> &sub_expr) {
        std::shared_ptr<bitwise_not_expression> expr{new bitwise_not_expression(context)};
        expr->assign(sub_expr);
        return std::shared_ptr<unary_expression>{expr};
    }
};


class logical_binary_expression : public binary_expression {
protected:
    logical_binary_expression() = delete;
    logical_binary_expression(std::shared_ptr<context> context) : binary_expression(context) {}

public:
    void accept(model_visitor &visitor) override;
};

class logical_and_expression : public logical_binary_expression {
protected:
    logical_and_expression() = delete;
    logical_and_expression(std::shared_ptr<context> context) : logical_binary_expression(context) {}

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(std::shared_ptr<context> context, const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<logical_and_expression> expr{new logical_and_expression(context)};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class logical_or_expression : public logical_binary_expression {
protected:
    logical_or_expression() = delete;
    logical_or_expression(std::shared_ptr<context> context) : logical_binary_expression(context) {}

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(std::shared_ptr<context> context, const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<logical_or_expression> expr{new logical_or_expression(context)};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class logical_not_expression : public unary_expression {
protected:
    logical_not_expression() = delete;
    logical_not_expression(std::shared_ptr<context> context) : unary_expression(context) {}

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<unary_expression> make_shared(std::shared_ptr<context> context, const std::shared_ptr<expression> &sub_expr) {
        std::shared_ptr<logical_not_expression> expr{new logical_not_expression(context)};
        expr->assign(sub_expr);
        return std::shared_ptr<unary_expression>{expr};
    }
};

/**
 * The load-value expression is an internal tool to get the real value from a reference.
 * Supposed to be injected to simplify code generation.
 * Not supposed to be used by external code.
 */
class load_value_expression : public unary_expression {
protected:
    load_value_expression() = delete;
    load_value_expression(std::shared_ptr<context> context) : unary_expression(context) {}

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<unary_expression> make_shared(std::shared_ptr<context> context, const std::shared_ptr<expression> &sub_expr) {
        std::shared_ptr<load_value_expression> expr{new load_value_expression(context)};
        expr->assign(sub_expr);
        return std::shared_ptr<unary_expression>{expr};
    }
};

class address_of_expression : public unary_expression {
protected:
    address_of_expression() = delete;
    address_of_expression(std::shared_ptr<context> context) : unary_expression(context) {}

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<unary_expression> make_shared(std::shared_ptr<context> context, const std::shared_ptr<expression> &sub_expr) {
        std::shared_ptr<address_of_expression> expr{new address_of_expression(context)};
        expr->assign(sub_expr);
        return std::shared_ptr<unary_expression>{expr};
    }
};

class dereference_expression : public unary_expression {
protected:
    dereference_expression() = delete;
    dereference_expression(std::shared_ptr<context> context) : unary_expression(context) {}

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<unary_expression> make_shared(std::shared_ptr<context> context, const std::shared_ptr<expression> &sub_expr) {
        std::shared_ptr<dereference_expression> expr{new dereference_expression(context)};
        expr->assign(sub_expr);
        return std::shared_ptr<unary_expression>{expr};
    }
};

class member_of_expression : public unary_expression {
protected:
    std::shared_ptr<symbol_expression> _symbol;

    member_of_expression() = delete;
    member_of_expression(std::shared_ptr<context> context) : unary_expression(context) {}

    friend class gen::symbol_type_resolver;

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
    member_of_object_expression() = delete;
    member_of_object_expression(std::shared_ptr<context> context) : member_of_expression(context) {}

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<member_of_object_expression> make_shared(std::shared_ptr<context> context, const std::shared_ptr<expression> &sub_expr, const std::shared_ptr<symbol_expression>& symbol) {
        std::shared_ptr<member_of_object_expression> expr{new member_of_object_expression(context)};
        expr->assign(sub_expr, symbol);
        return std::shared_ptr<member_of_object_expression>{expr};
    }
};

class member_of_pointer_expression : public member_of_expression {
protected:
    member_of_pointer_expression() = delete;
    member_of_pointer_expression(std::shared_ptr<context> context) : member_of_expression(context) {}

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<member_of_pointer_expression> make_shared(std::shared_ptr<context> context, const std::shared_ptr<expression> &sub_expr, const std::shared_ptr<symbol_expression>& symbol) {
        std::shared_ptr<member_of_pointer_expression> expr{new member_of_pointer_expression(context)};
        expr->assign(sub_expr, symbol);
        return std::shared_ptr<member_of_pointer_expression>{expr};
    }
};

class comparison_expression : public binary_expression {
protected:
    comparison_expression() = delete;
    comparison_expression(std::shared_ptr<context> context) : binary_expression(context) {}

public:
    void accept(model_visitor &visitor) override;
};

class equal_expression : public comparison_expression {
protected:
    equal_expression() = delete;
    equal_expression(std::shared_ptr<context> context) : comparison_expression(context) {}

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(std::shared_ptr<context> context, const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<equal_expression> expr{new equal_expression(context)};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class different_expression : public comparison_expression {
protected:
    different_expression() = delete;
    different_expression(std::shared_ptr<context> context) : comparison_expression(context) {}

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(std::shared_ptr<context> context, const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<different_expression> expr{new different_expression(context)};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class lesser_expression : public comparison_expression {
protected:
    lesser_expression() = delete;
    lesser_expression(std::shared_ptr<context> context) : comparison_expression(context) {}

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(std::shared_ptr<context> context, const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<lesser_expression> expr{new lesser_expression(context)};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class greater_expression : public comparison_expression {
protected:
    greater_expression() = delete;
    greater_expression(std::shared_ptr<context> context) : comparison_expression(context) {}

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(std::shared_ptr<context> context, const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<greater_expression> expr{new greater_expression(context)};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};


class lesser_equal_expression : public comparison_expression {
protected:
    lesser_equal_expression() = default;
    lesser_equal_expression(std::shared_ptr<context> context) : comparison_expression(context) {}

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(std::shared_ptr<context> context, const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<lesser_equal_expression> expr{new lesser_equal_expression(context)};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class greater_equal_expression : public comparison_expression {
protected:
    greater_equal_expression() = delete;
    greater_equal_expression(std::shared_ptr<context> context) : comparison_expression(context) {}

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(std::shared_ptr<context> context, const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<greater_equal_expression> expr{new greater_equal_expression(context)};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class cast_expression : public unary_expression {
protected:
    // Casting type
    std::shared_ptr<type> _cast_type;

    cast_expression() = delete;
    cast_expression(std::shared_ptr<context> context) : unary_expression(context) {}

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(std::shared_ptr<context> context, const std::shared_ptr<expression> &expr, const std::shared_ptr<type> &type) {
        std::shared_ptr<cast_expression> rexpr{new cast_expression(context)};
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
    subscript_expression() = delete;
    subscript_expression(std::shared_ptr<context> context) : binary_expression(context) {}

    subscript_expression(std::shared_ptr<context> context, 
                                    const std::shared_ptr<expression> &callee_expr,
                                   const std::shared_ptr<expression> &index_expr) :
                                   binary_expression(context, callee_expr, index_expr)
    {
    }

public:
    void accept(model_visitor &visitor) override;

    static std::shared_ptr<expression>
    make_shared(std::shared_ptr<context> context, const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<subscript_expression> expr{new subscript_expression(context)};
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


    function_invocation_expression() = delete;
    function_invocation_expression(std::shared_ptr<context> context) : expression(context) {}

    function_invocation_expression(std::shared_ptr<context> context, const std::shared_ptr<expression> &callee_expr)
            : expression(context), _callee_expr(callee_expr) {
        _callee_expr->set_parent_expression(shared_as<expression>());
    }

    function_invocation_expression(std::shared_ptr<context> context, 
                                   const std::shared_ptr<expression> &callee_expr,
                                   const std::shared_ptr<expression> &arg_expr)
            : expression(context), _callee_expr(callee_expr) {
        _callee_expr->set_parent_expression(shared_as<expression>());
        arg_expr->set_parent_expression(shared_as<expression>());
        _arguments.push_back(arg_expr);
    }

    function_invocation_expression(std::shared_ptr<context> context, 
                                   const std::shared_ptr<expression> &callee_expr,
                                   const std::vector<std::shared_ptr<expression>> &args)
            : expression (context), _callee_expr(callee_expr) {
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
    make_shared(std::shared_ptr<context> context, const std::shared_ptr<expression> &callee_expr, const std::vector<std::shared_ptr<expression>> &args) {
        std::shared_ptr<function_invocation_expression> expr{new function_invocation_expression(context)};
        expr->assign(callee_expr, args);
        return std::shared_ptr<expression>{expr};
    }

public:
    void accept(model_visitor &visitor) override;

};


} // namespace k::model
#endif //KLANG_MODEL_EXPRESSIONS_HPP
