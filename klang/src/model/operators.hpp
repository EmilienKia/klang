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
 * Operator expression internal hierarchy:
 * expression
 * +- unary_expression
 * | +- arithmetic_unary_expression
 * | | +- unary_plus_expression
 * | | +- unary_minus_expression
 * | | +- bitwise_not_expression
 * | | +- prefix_increment_expression
 * | | +- prefix_decrement_expression
 * | | +- postfix_increment_expression
 * | | +- postfix_decrement_expression
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
 * | +- ternary_expression
 * | | +- conditional_expression
 * | +- comparison_expression
 * | | +- equal_expression
 * | | +- different_expression
 * | | +- lesser_expression
 * | | +- greater_expression
 * | | +- lesser_equal_expression
 * | | +- greater_equal_expression
 * | +- spaceship_expression
 */

#ifndef KLANG_MODEL_OPERATORS_HPP
#define KLANG_MODEL_OPERATORS_HPP

#include "model.hpp"
#include "expressions.hpp"

namespace k::model {

namespace gen {
class symbol_resolver;
}



class arithmetic_binary_expression : public binary_expression {
protected:
    arithmetic_binary_expression() = default;
public:
    void accept(model_visitor &visitor) override;
    std::shared_ptr<expression> clone() const override = 0;
};

class addition_expression : public arithmetic_binary_expression {
protected:
    addition_expression() = default;
public:
    void accept(model_visitor &visitor) override;
    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<addition_expression> expr{new addition_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<addition_expression> c{new addition_expression()};
        c->_type = _type;
        c->_ast_node = _ast_node;
        if (_left_expr && _right_expr) c->assign(_left_expr->clone(), _right_expr->clone());
        return c;
    }
};

class substraction_expression : public arithmetic_binary_expression {
protected:
    substraction_expression() = default;
public:
    void accept(model_visitor &visitor) override;
    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<substraction_expression> expr{new substraction_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<substraction_expression> c{new substraction_expression()};
        c->_type = _type;
        c->_ast_node = _ast_node;
        if (_left_expr && _right_expr) c->assign(_left_expr->clone(), _right_expr->clone());
        return c;
    }
};

class multiplication_expression : public arithmetic_binary_expression {
protected:
    multiplication_expression() = default;
public:
    void accept(model_visitor &visitor) override;
    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<multiplication_expression> expr{new multiplication_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<multiplication_expression> c{new multiplication_expression()};
        c->_type = _type;
        c->_ast_node = _ast_node;
        if (_left_expr && _right_expr) c->assign(_left_expr->clone(), _right_expr->clone());
        return c;
    }
};

class division_expression : public arithmetic_binary_expression {
protected:
    division_expression() = default;
public:
    void accept(model_visitor &visitor) override;
    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<division_expression> expr{new division_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<division_expression> c{new division_expression()};
        c->_type = _type;
        c->_ast_node = _ast_node;
        if (_left_expr && _right_expr) c->assign(_left_expr->clone(), _right_expr->clone());
        return c;
    }
};

class modulo_expression : public arithmetic_binary_expression {
protected:
    modulo_expression() = default;
public:
    void accept(model_visitor &visitor) override;
    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<modulo_expression> expr{new modulo_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<modulo_expression> c{new modulo_expression()};
        c->_type = _type;
        c->_ast_node = _ast_node;
        if (_left_expr && _right_expr) c->assign(_left_expr->clone(), _right_expr->clone());
        return c;
    }
};

class bitwise_and_expression : public arithmetic_binary_expression {
protected:
    bitwise_and_expression() = default;
public:
    void accept(model_visitor &visitor) override;
    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<bitwise_and_expression> expr{new bitwise_and_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<bitwise_and_expression> c{new bitwise_and_expression()};
        c->_type = _type;
        c->_ast_node = _ast_node;
        if (_left_expr && _right_expr) c->assign(_left_expr->clone(), _right_expr->clone());
        return c;
    }
};

class bitwise_or_expression : public arithmetic_binary_expression {
protected:
    bitwise_or_expression() = default;
public:
    void accept(model_visitor &visitor) override;
    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<bitwise_or_expression> expr{new bitwise_or_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<bitwise_or_expression> c{new bitwise_or_expression()};
        c->_type = _type;
        c->_ast_node = _ast_node;
        if (_left_expr && _right_expr) c->assign(_left_expr->clone(), _right_expr->clone());
        return c;
    }
};

class bitwise_xor_expression : public arithmetic_binary_expression {
protected:
    bitwise_xor_expression() = default;
public:
    void accept(model_visitor &visitor) override;
    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<bitwise_xor_expression> expr{new bitwise_xor_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<bitwise_xor_expression> c{new bitwise_xor_expression()};
        c->_type = _type;
        c->_ast_node = _ast_node;
        if (_left_expr && _right_expr) c->assign(_left_expr->clone(), _right_expr->clone());
        return c;
    }
};

class left_shift_expression : public arithmetic_binary_expression {
protected:
    left_shift_expression() = default;
public:
    void accept(model_visitor &visitor) override;
    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<left_shift_expression> expr{new left_shift_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<left_shift_expression> c{new left_shift_expression()};
        c->_type = _type;
        c->_ast_node = _ast_node;
        if (_left_expr && _right_expr) c->assign(_left_expr->clone(), _right_expr->clone());
        return c;
    }
};

class right_shift_expression : public arithmetic_binary_expression {
protected:
    right_shift_expression() = default;
public:
    void accept(model_visitor &visitor) override;
    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<right_shift_expression> expr{new right_shift_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<right_shift_expression> c{new right_shift_expression()};
        c->_type = _type;
        c->_ast_node = _ast_node;
        if (_left_expr && _right_expr) c->assign(_left_expr->clone(), _right_expr->clone());
        return c;
    }
};

class assignation_expression : public binary_expression {
protected:
    assignation_expression() = default;
public:
    void accept(model_visitor &visitor) override;
    std::shared_ptr<expression> clone() const override = 0;
};

class simple_assignation_expression : public assignation_expression {
protected:
    simple_assignation_expression() = default;
public:
    void accept(model_visitor &visitor) override;
    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<simple_assignation_expression> expr{new simple_assignation_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<simple_assignation_expression> c{new simple_assignation_expression()};
        c->_type = _type;
        c->_ast_node = _ast_node;
        if (_left_expr && _right_expr) c->assign(_left_expr->clone(), _right_expr->clone());
        return c;
    }
};

class arithmetic_assignation_expression : public assignation_expression {
protected:
    arithmetic_assignation_expression() = default;
public:
    void accept(model_visitor &visitor) override;
    std::shared_ptr<expression> clone() const override = 0;
};

class additition_assignation_expression : public arithmetic_assignation_expression {
protected:
    additition_assignation_expression() = default;
public:
    void accept(model_visitor &visitor) override;
    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<additition_assignation_expression> expr{new additition_assignation_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<additition_assignation_expression> c{new additition_assignation_expression()};
        c->_type = _type;
        c->_ast_node = _ast_node;
        if (_left_expr && _right_expr) c->assign(_left_expr->clone(), _right_expr->clone());
        return c;
    }
};

class substraction_assignation_expression : public arithmetic_assignation_expression {
protected:
    substraction_assignation_expression() = default;
public:
    void accept(model_visitor &visitor) override;
    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<substraction_assignation_expression> expr{new substraction_assignation_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<substraction_assignation_expression> c{new substraction_assignation_expression()};
        c->_type = _type;
        c->_ast_node = _ast_node;
        if (_left_expr && _right_expr) c->assign(_left_expr->clone(), _right_expr->clone());
        return c;
    }
};

class multiplication_assignation_expression : public arithmetic_assignation_expression {
protected:
    multiplication_assignation_expression() = default;
public:
    void accept(model_visitor &visitor) override;
    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<multiplication_assignation_expression> expr{new multiplication_assignation_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<multiplication_assignation_expression> c{new multiplication_assignation_expression()};
        c->_type = _type;
        c->_ast_node = _ast_node;
        if (_left_expr && _right_expr) c->assign(_left_expr->clone(), _right_expr->clone());
        return c;
    }
};

class division_assignation_expression : public arithmetic_assignation_expression {
protected:
    division_assignation_expression() = default;
public:
    void accept(model_visitor &visitor) override;
    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<division_assignation_expression> expr{new division_assignation_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<division_assignation_expression> c{new division_assignation_expression()};
        c->_type = _type;
        c->_ast_node = _ast_node;
        if (_left_expr && _right_expr) c->assign(_left_expr->clone(), _right_expr->clone());
        return c;
    }
};

class modulo_assignation_expression : public arithmetic_assignation_expression {
protected:
    modulo_assignation_expression() = default;
public:
    void accept(model_visitor &visitor) override;
    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<modulo_assignation_expression> expr{new modulo_assignation_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<modulo_assignation_expression> c{new modulo_assignation_expression()};
        c->_type = _type;
        c->_ast_node = _ast_node;
        if (_left_expr && _right_expr) c->assign(_left_expr->clone(), _right_expr->clone());
        return c;
    }
};

class bitwise_and_assignation_expression : public arithmetic_assignation_expression {
protected:
    bitwise_and_assignation_expression() = default;
public:
    void accept(model_visitor &visitor) override;
    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<bitwise_and_assignation_expression> expr{new bitwise_and_assignation_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<bitwise_and_assignation_expression> c{new bitwise_and_assignation_expression()};
        c->_type = _type;
        c->_ast_node = _ast_node;
        if (_left_expr && _right_expr) c->assign(_left_expr->clone(), _right_expr->clone());
        return c;
    }
};

class bitwise_or_assignation_expression : public arithmetic_assignation_expression {
protected:
    bitwise_or_assignation_expression() = default;
public:
    void accept(model_visitor &visitor) override;
    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<bitwise_or_assignation_expression> expr{new bitwise_or_assignation_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<bitwise_or_assignation_expression> c{new bitwise_or_assignation_expression()};
        c->_type = _type;
        c->_ast_node = _ast_node;
        if (_left_expr && _right_expr) c->assign(_left_expr->clone(), _right_expr->clone());
        return c;
    }
};

class bitwise_xor_assignation_expression : public arithmetic_assignation_expression {
protected:
    bitwise_xor_assignation_expression() = default;
public:
    void accept(model_visitor &visitor) override;
    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<bitwise_xor_assignation_expression> expr{new bitwise_xor_assignation_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<bitwise_xor_assignation_expression> c{new bitwise_xor_assignation_expression()};
        c->_type = _type;
        c->_ast_node = _ast_node;
        if (_left_expr && _right_expr) c->assign(_left_expr->clone(), _right_expr->clone());
        return c;
    }
};

class left_shift_assignation_expression : public arithmetic_assignation_expression {
protected:
    left_shift_assignation_expression() = default;
public:
    void accept(model_visitor &visitor) override;
    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<left_shift_assignation_expression> expr{new left_shift_assignation_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<left_shift_assignation_expression> c{new left_shift_assignation_expression()};
        c->_type = _type;
        c->_ast_node = _ast_node;
        if (_left_expr && _right_expr) c->assign(_left_expr->clone(), _right_expr->clone());
        return c;
    }
};

class right_shift_assignation_expression : public arithmetic_assignation_expression {
protected:
    right_shift_assignation_expression() = default;
public:
    void accept(model_visitor &visitor) override;
    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<right_shift_assignation_expression> expr{new right_shift_assignation_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<right_shift_assignation_expression> c{new right_shift_assignation_expression()};
        c->_type = _type;
        c->_ast_node = _ast_node;
        if (_left_expr && _right_expr) c->assign(_left_expr->clone(), _right_expr->clone());
        return c;
    }
};

class arithmetic_unary_expression : public unary_expression {
protected:
    arithmetic_unary_expression() = default;
public:
    void accept(model_visitor &visitor) override;
    std::shared_ptr<expression> clone() const override = 0;
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
    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<unary_plus_expression> c{new unary_plus_expression()};
        c->_type = _type;
        c->_ast_node = _ast_node;
        if (_sub_expr) c->assign(_sub_expr->clone());
        return c;
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
    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<unary_minus_expression> c{new unary_minus_expression()};
        c->_type = _type;
        c->_ast_node = _ast_node;
        if (_sub_expr) c->assign(_sub_expr->clone());
        return c;
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
    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<bitwise_not_expression> c{new bitwise_not_expression()};
        c->_type = _type;
        c->_ast_node = _ast_node;
        if (_sub_expr) c->assign(_sub_expr->clone());
        return c;
    }
};

class prefix_increment_expression : public arithmetic_unary_expression {
protected:
    prefix_increment_expression() = default;
public:
    void accept(model_visitor &visitor) override;
    static std::shared_ptr<unary_expression> make_shared(const std::shared_ptr<expression> &sub_expr) {
        std::shared_ptr<prefix_increment_expression> expr{new prefix_increment_expression()};
        expr->assign(sub_expr);
        return std::shared_ptr<unary_expression>{expr};
    }
    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<prefix_increment_expression> c{new prefix_increment_expression()};
        c->_type = _type;
        c->_ast_node = _ast_node;
        if (_sub_expr) c->assign(_sub_expr->clone());
        return c;
    }
};

class prefix_decrement_expression : public arithmetic_unary_expression {
protected:
    prefix_decrement_expression() = default;
public:
    void accept(model_visitor &visitor) override;
    static std::shared_ptr<unary_expression> make_shared(const std::shared_ptr<expression> &sub_expr) {
        std::shared_ptr<prefix_decrement_expression> expr{new prefix_decrement_expression()};
        expr->assign(sub_expr);
        return std::shared_ptr<unary_expression>{expr};
    }
    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<prefix_decrement_expression> c{new prefix_decrement_expression()};
        c->_type = _type;
        c->_ast_node = _ast_node;
        if (_sub_expr) c->assign(_sub_expr->clone());
        return c;
    }
};

class postfix_increment_expression : public arithmetic_unary_expression {
protected:
    postfix_increment_expression() = default;
public:
    void accept(model_visitor &visitor) override;
    static std::shared_ptr<unary_expression> make_shared(const std::shared_ptr<expression> &sub_expr) {
        std::shared_ptr<postfix_increment_expression> expr{new postfix_increment_expression()};
        expr->assign(sub_expr);
        return std::shared_ptr<unary_expression>{expr};
    }
    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<postfix_increment_expression> c{new postfix_increment_expression()};
        c->_type = _type;
        c->_ast_node = _ast_node;
        if (_sub_expr) c->assign(_sub_expr->clone());
        return c;
    }
};

class postfix_decrement_expression : public arithmetic_unary_expression {
protected:
    postfix_decrement_expression() = default;
public:
    void accept(model_visitor &visitor) override;
    static std::shared_ptr<unary_expression> make_shared(const std::shared_ptr<expression> &sub_expr) {
        std::shared_ptr<postfix_decrement_expression> expr{new postfix_decrement_expression()};
        expr->assign(sub_expr);
        return std::shared_ptr<unary_expression>{expr};
    }
    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<postfix_decrement_expression> c{new postfix_decrement_expression()};
        c->_type = _type;
        c->_ast_node = _ast_node;
        if (_sub_expr) c->assign(_sub_expr->clone());
        return c;
    }
};

class logical_binary_expression : public binary_expression {
protected:
    logical_binary_expression() = default;
public:
    void accept(model_visitor &visitor) override;
    std::shared_ptr<expression> clone() const override = 0;
};

class logical_and_expression : public logical_binary_expression {
protected:
    logical_and_expression() = default;
public:
    void accept(model_visitor &visitor) override;
    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<logical_and_expression> expr{new logical_and_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<logical_and_expression> c{new logical_and_expression()};
        c->_type = _type;
        c->_ast_node = _ast_node;
        if (_left_expr && _right_expr) c->assign(_left_expr->clone(), _right_expr->clone());
        return c;
    }
};

class logical_or_expression : public logical_binary_expression {
protected:
    logical_or_expression() = default;
public:
    void accept(model_visitor &visitor) override;
    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<logical_or_expression> expr{new logical_or_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<logical_or_expression> c{new logical_or_expression()};
        c->_type = _type;
        c->_ast_node = _ast_node;
        if (_left_expr && _right_expr) c->assign(_left_expr->clone(), _right_expr->clone());
        return c;
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
    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<logical_not_expression> c{new logical_not_expression()};
        c->_type = _type;
        c->_ast_node = _ast_node;
        if (_sub_expr) c->assign(_sub_expr->clone());
        return c;
    }
};

class ternary_expression : public expression {
protected:
    std::shared_ptr<expression> _lexpr;
    std::shared_ptr<expression> _mexpr;
    std::shared_ptr<expression> _rexpr;

public:
    const std::shared_ptr<expression>& lexpr() const { return _lexpr; }
    std::shared_ptr<expression>& lexpr() { return _lexpr; }

    const std::shared_ptr<expression>& mexpr() const { return _mexpr; }
    std::shared_ptr<expression>& mexpr() { return _mexpr; }

    const std::shared_ptr<expression>& rexpr() const { return _rexpr; }
    std::shared_ptr<expression>& rexpr() { return _rexpr; }

    void assign(std::shared_ptr<expression> lexpr, std::shared_ptr<expression> mexpr, std::shared_ptr<expression> rexpr) {
        _lexpr = lexpr;
        _mexpr = mexpr;
        _rexpr = rexpr;
        _lexpr->set_parent_expression(shared_as<expression>());
        _mexpr->set_parent_expression(shared_as<expression>());
        _rexpr->set_parent_expression(shared_as<expression>());
    }

    lex::opt_any_lexeme get_first_lexeme() const override;
    lex::opt_any_lexeme get_last_lexeme() const override;
    lex::opt_any_lexeme get_interest_lexeme() const override;

protected:
    ternary_expression() = default;
    ternary_expression(const ternary_expression&) = default;
    ternary_expression(ternary_expression&&) = default;
    ternary_expression(std::shared_ptr<expression> lexpr, std::shared_ptr<expression> mexpr, std::shared_ptr<expression> rexpr)
        : _lexpr(lexpr), _mexpr(mexpr), _rexpr(rexpr) {
        _lexpr->set_parent_expression(shared_as<expression>());
        _mexpr->set_parent_expression(shared_as<expression>());
        _rexpr->set_parent_expression(shared_as<expression>());
    }
};

class conditional_expression : public ternary_expression {
protected:
    conditional_expression() = default;
public:
    void accept(model_visitor &visitor) override;
    static std::shared_ptr<expression> make_shared(
        const std::shared_ptr<expression>& condition,
        const std::shared_ptr<expression>& then_expr,
        const std::shared_ptr<expression>& else_expr)
    {
        std::shared_ptr<conditional_expression> expr{new conditional_expression()};
        expr->assign(condition, then_expr, else_expr);
        return std::shared_ptr<expression>{expr};
    }
    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<conditional_expression> c{new conditional_expression()};
        c->_type = _type;
        c->_ast_node = _ast_node;
        if (_lexpr && _mexpr && _rexpr) c->assign(_lexpr->clone(), _mexpr->clone(), _rexpr->clone());
        return c;
    }
};

/**
 * Describes how a comparison expression's result is actually produced when the exact
 * comparison operator (==, !=, <, >, <=, >=) is not declared on the operand's aggregate
 * type, but can be synthesized from another declared comparison operator via boolean
 * algebra, or from a declared spaceship (`<=>`) operator via a sign test against zero.
 * See doc/spec/language/functions/operators.md, "Comparison operator fallback".
 *
 * A single "source" operator function is resolved and stored in the inherited
 * binary_expression::_operator_func (with binary_expression::_operator_dispatch_info
 * describing its dispatch). All synthesis kinds reuse that single source operator,
 * calling it once (single-source kinds) or twice with swapped operands (composite
 * kinds) — never two *different* operator functions.
 *
 * Operand order / combination fed to the source operator (`src`), where a/b are the
 * expression's original left/right operands:
 *   DIRECT         : src(a, b)                       — exact operator, kept as declared.
 *   SPACESHIP      : wanted_test(src(a, b), 0)        — src is `operator <=>` on a's type.
 *   SPACESHIP_SWAP : swap_of(wanted)_test(src(b, a), 0) — src is `operator <=>` on b's type.
 *   NEGATE         : !src(a, b)
 *   SWAP           : src(b, a)
 *   SWAP_NEGATE    : !src(b, a)
 *   COMPOSITE_AND  : src(a, b) && src(b, a)           — wanted is == or !=.
 *   COMPOSITE_OR   : src(a, b) || src(b, a)
 * When composite_negate_terms() is true, each term above is additionally negated
 * before combination (i.e. `!src(a,b) && !src(b,a)` / `!src(a,b) || !src(b,a)`).
 *
 * For SPACESHIP/SPACESHIP_SWAP, `wanted_test`/`swap_of(wanted)_test` denotes comparing the
 * source spaceship call's signed-integer or floating-point result against the integer
 * literal `0` using the wanted comparison operator (or its swap pairing for
 * SPACESHIP_SWAP, since `b <=> a` has operands reversed relative to `a <=> b`) — e.g. for
 * wanted `<`, SPACESHIP tests `src(a,b) < 0`. See k::op operator swap pairings.
 */
enum class cmp_synthesis {
    /** Exact operator declared and used as-is; declared return type is kept. */
    DIRECT,
    /**
     * Wanted = wanted_test(source(left, right), 0), where source is a declared
     * `operator <=>` found on the left operand's aggregate type. See class docs.
     */
    SPACESHIP,
    /**
     * Wanted = swap_of(wanted)_test(source(right, left), 0), where source is a declared
     * `operator <=>` found on the right operand's aggregate type. See class docs.
     */
    SPACESHIP_SWAP,
    /** Wanted = !source(left, right). */
    NEGATE,
    /** Wanted = source(right, left). */
    SWAP,
    /** Wanted = !source(right, left). */
    SWAP_NEGATE,
    /**
     * Wanted (== or !=) = source(left, right) && source(right, left) [terms optionally
     * negated]. Restriction: only chosen when BOTH calls need zero operand adaptation
     * (cast_weight == CAST_NONE), so the two already-evaluated operand LLVM values can be
     * reused verbatim in both calls without re-evaluating (and therefore without risking
     * duplicate side effects) or needing separate adapted expression trees.
     */
    COMPOSITE_AND,
    /** Wanted (== or !=) = source(left, right) || source(right, left) [terms optionally
     * negated]. Same CAST_NONE restriction as COMPOSITE_AND. */
    COMPOSITE_OR,
};

class comparison_expression : public binary_expression {
protected:
    comparison_expression() = default;

    /** Synthesis strategy chosen at resolution time (DIRECT if the exact operator was found). */
    cmp_synthesis _cmp_synthesis = cmp_synthesis::DIRECT;

    /**
     * Only meaningful when _cmp_synthesis is COMPOSITE_AND or COMPOSITE_OR: whether each
     * of the two source-operator calls must be logically negated before being combined
     * with && / ||. See class-level documentation for the exact recipes.
     */
    bool _composite_negate_terms = false;

    /**
     * Dispatch info for the *second* source-operator call of a COMPOSITE_AND/COMPOSITE_OR
     * synthesis (receiver = right operand). The first call's dispatch info (receiver =
     * left operand, or the sole call's dispatch info for non-composite kinds) is stored in
     * the inherited binary_expression::_operator_dispatch_info.
     */
    std::optional<virtual_dispatch_info> _composite_dispatch2;

    /**
     * Phase 2 (aggregate `operator <=>` return type): only set for SPACESHIP/SPACESHIP_SWAP
     * synthesis when the spaceship source operator's return type is an aggregate rather
     * than a primitive int/float. Names the resolved, bool-returning comparison operator
     * used to compare that aggregate result against the integer literal 0. Null means the
     * spaceship source's return type is a primitive int/float, and the ordinary
     * sign-test-against-zero codegen (ICmp/FCmp) applies instead.
     */
    std::shared_ptr<function> _spaceship_zero_func;
    /** Dispatch info for `_spaceship_zero_func` (only meaningful if it is virtual). */
    std::optional<virtual_dispatch_info> _spaceship_zero_dispatch;
    /** Numeric primitive type used to build the integer-zero argument for `_spaceship_zero_func`. */
    std::shared_ptr<type> _spaceship_zero_arg_type;

public:
    void accept(model_visitor &visitor) override;
    std::shared_ptr<expression> clone() const override = 0;

    /** Synthesis strategy used to produce this comparison's result. */
    cmp_synthesis get_cmp_synthesis() const { return _cmp_synthesis; }
    void set_cmp_synthesis(cmp_synthesis synth) { _cmp_synthesis = synth; }
    bool is_synthesized() const { return _cmp_synthesis != cmp_synthesis::DIRECT; }

    /** Whether the two composite terms must be negated before combination (COMPOSITE_* only). */
    bool composite_negate_terms() const { return _composite_negate_terms; }
    void set_composite_negate_terms(bool negate) { _composite_negate_terms = negate; }

    /** Dispatch info for the second composite call (COMPOSITE_* only; receiver = right operand). */
    bool has_composite_dispatch_info() const { return _composite_dispatch2.has_value(); }
    const virtual_dispatch_info& get_composite_dispatch_info() const { return _composite_dispatch2.value(); }
    void set_composite_dispatch_info(virtual_dispatch_info info) { _composite_dispatch2 = std::move(info); }

    /** Phase 2: resolved bool-returning comparison of the (aggregate) spaceship result vs 0. */
    bool has_spaceship_zero_func() const { return _spaceship_zero_func != nullptr; }
    std::shared_ptr<function> get_spaceship_zero_func() const { return _spaceship_zero_func; }
    void set_spaceship_zero_func(std::shared_ptr<function> f) { _spaceship_zero_func = std::move(f); }

    bool has_spaceship_zero_dispatch_info() const { return _spaceship_zero_dispatch.has_value(); }
    const virtual_dispatch_info& get_spaceship_zero_dispatch_info() const { return _spaceship_zero_dispatch.value(); }
    void set_spaceship_zero_dispatch_info(virtual_dispatch_info info) { _spaceship_zero_dispatch = std::move(info); }

    std::shared_ptr<type> get_spaceship_zero_arg_type() const { return _spaceship_zero_arg_type; }
    void set_spaceship_zero_arg_type(std::shared_ptr<type> t) { _spaceship_zero_arg_type = std::move(t); }
};

class equal_expression : public comparison_expression {
protected:
    equal_expression() = default;
public:
    void accept(model_visitor &visitor) override;
    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<equal_expression> expr{new equal_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<equal_expression> c{new equal_expression()};
        c->_type = _type;
        c->_ast_node = _ast_node;
        if (_left_expr && _right_expr) c->assign(_left_expr->clone(), _right_expr->clone());
        return c;
    }
};

class different_expression : public comparison_expression {
protected:
    different_expression() = default;
public:
    void accept(model_visitor &visitor) override;
    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<different_expression> expr{new different_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<different_expression> c{new different_expression()};
        c->_type = _type;
        c->_ast_node = _ast_node;
        if (_left_expr && _right_expr) c->assign(_left_expr->clone(), _right_expr->clone());
        return c;
    }
};

class lesser_expression : public comparison_expression {
protected:
    lesser_expression() = default;
public:
    void accept(model_visitor &visitor) override;
    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<lesser_expression> expr{new lesser_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<lesser_expression> c{new lesser_expression()};
        c->_type = _type;
        c->_ast_node = _ast_node;
        if (_left_expr && _right_expr) c->assign(_left_expr->clone(), _right_expr->clone());
        return c;
    }
};

class greater_expression : public comparison_expression {
protected:
    greater_expression() = default;
public:
    void accept(model_visitor &visitor) override;
    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<greater_expression> expr{new greater_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<greater_expression> c{new greater_expression()};
        c->_type = _type;
        c->_ast_node = _ast_node;
        if (_left_expr && _right_expr) c->assign(_left_expr->clone(), _right_expr->clone());
        return c;
    }
};

class lesser_equal_expression : public comparison_expression {
protected:
    lesser_equal_expression() = default;
public:
    void accept(model_visitor &visitor) override;
    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<lesser_equal_expression> expr{new lesser_equal_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<lesser_equal_expression> c{new lesser_equal_expression()};
        c->_type = _type;
        c->_ast_node = _ast_node;
        if (_left_expr && _right_expr) c->assign(_left_expr->clone(), _right_expr->clone());
        return c;
    }
};

class greater_equal_expression : public comparison_expression {
protected:
    greater_equal_expression() = default;
public:
    void accept(model_visitor &visitor) override;
    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<greater_equal_expression> expr{new greater_equal_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<greater_equal_expression> c{new greater_equal_expression()};
        c->_type = _type;
        c->_ast_node = _ast_node;
        if (_left_expr && _right_expr) c->assign(_left_expr->clone(), _right_expr->clone());
        return c;
    }
};

/**
 * Three-way comparison ("spaceship") expression: `a <=> b`.
 *
 * Unlike comparison_expression (whose six operators always ultimately produce `bool`),
 * spaceship_expression's result type is whatever the resolved `operator <=>` declares as
 * its return type (Phase 1: a signed integer or floating-point primitive; Phase 2: any
 * aggregate type that is itself comparable to the integer literal `0`), or `int` for the
 * builtin primitive-operand spaceship.
 *
 * `<=>` is never itself synthesized from another operator — it is, on the contrary, the
 * first fallback source used to synthesize `==`, `!=`, `<`, `>`, `<=`, `>=` when the exact
 * operator is not declared (see k::model::cmp_synthesis::SPACESHIP / SPACESHIP_SWAP and
 * doc/spec/language/functions/operators.md, "Comparison operator fallback").
 */
class spaceship_expression : public binary_expression {
protected:
    spaceship_expression() = default;
public:
    void accept(model_visitor &visitor) override;
    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<spaceship_expression> expr{new spaceship_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<spaceship_expression> c{new spaceship_expression()};
        c->_type = _type;
        c->_ast_node = _ast_node;
        if (_left_expr && _right_expr) c->assign(_left_expr->clone(), _right_expr->clone());
        return c;
    }
};

} // namespace k::model
#endif //KLANG_MODEL_OPERATORS_HPP
