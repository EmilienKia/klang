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
        c->_ast_unary_expr = _ast_unary_expr;
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
        c->_ast_unary_expr = _ast_unary_expr;
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
        c->_ast_unary_expr = _ast_unary_expr;
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
        c->_ast_unary_expr = _ast_unary_expr;
        if (_sub_expr) c->assign(_sub_expr->clone());
        return c;
    }
};

class comparison_expression : public binary_expression {
protected:
    comparison_expression() = default;
public:
    void accept(model_visitor &visitor) override;
    std::shared_ptr<expression> clone() const override = 0;
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
        if (_left_expr && _right_expr) c->assign(_left_expr->clone(), _right_expr->clone());
        return c;
    }
};

} // namespace k::model
#endif //KLANG_MODEL_OPERATORS_HPP
