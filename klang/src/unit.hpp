//
// Created by Emilien Kia <emilien.kia+dev@gmail.com>.
//

#ifndef KLANG_UNIT_HPP
#define KLANG_UNIT_HPP

#include <map>
#include <memory>
#include <string>
#include <utility>

#include "lexer.hpp"
#include "ast.hpp"
#include "parser.hpp"
#include "common.hpp"
#include "type.hpp"


namespace k::unit {

class expression;
class statement;
class variable_statement;
class block;

class parameter;
class function;
class ns;
class unit;


namespace gen {
    class unit_llvm_ir_gen;
}

enum visibility {
    DEFAULT,
    PUBLIC,
    PROTECTED,
    PRIVATE
};


class element_visitor;

/**
 * Base class for all language construction.
 */
class element : public std::enable_shared_from_this<element>
{
protected:
    virtual ~element() = default;

public:
    template<typename T>
    std::shared_ptr<T> shared_as() {
        return std::dynamic_pointer_cast<T>(shared_from_this());
    }

    template<typename T>
    std::shared_ptr<const T> shared_as() const {
        return std::dynamic_pointer_cast<T>(shared_from_this());
    }

    virtual void accept(element_visitor& visitor) =0;

};


/**
 * Interface for variables
 */
class variable_declaration
{
public:
    virtual const std::string& get_name() const = 0;
    virtual std::shared_ptr<type> get_type() const = 0;

    virtual std::shared_ptr<expression> get_init_expr() const = 0;
};


class variable_definition : public variable_declaration
{
protected:
    /** Variable name.*/
    std::string _name;

    /** Type of the variable */
    std::shared_ptr<type> _type;

    /** Optional initialization statement. */
    std::shared_ptr<expression> _expression;

    variable_definition() = default;
    variable_definition(const variable_definition&) = default;
    variable_definition(variable_definition&&) = default;
    variable_definition(const std::string& name) : _name(name) {}
    variable_definition(const std::string& name, const std::shared_ptr<type> &type) : _name(name), _type(type) {}

public:
    virtual const std::string& get_name() const override {
        return _name;
    }

    virtual std::shared_ptr<type> get_type() const override {
        return _type;
    }

    virtual std::shared_ptr<expression> get_init_expr() const override {
        return _expression;
    }

    virtual variable_definition& set_type(std::shared_ptr<type> type) {
        _type = type;
        return *this;
    }

    virtual variable_definition& set_init_expr(std::shared_ptr<expression> init_expr) {
        _expression = init_expr;
        return *this;
    }

};



/**
* Interface for holding variables (like ns and blocks)
*/
class variable_holder
{
public:
    virtual std::shared_ptr<variable_definition> append_variable(const std::string& name) =0;

    virtual std::shared_ptr<variable_definition> get_variable(const std::string& name) =0;

    virtual std::shared_ptr<variable_definition> lookup_variable(const std::string& name) =0;
};


/**
 * Base class for all expressions.
 */
class expression : public element
{
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

    friend class symbol_type_resolver;
    void set_type(std::shared_ptr<type> type) {
        _type = type;
    }

public:
    void accept(element_visitor& visitor) override;

    std::shared_ptr<type> get_type() {
        return _type;
    }

    std::shared_ptr<const type> get_type() const{
        return _type;
    }

    std::shared_ptr<statement> get_statement() { return _statement; };
    std::shared_ptr<const statement> get_statement() const { return _statement; };

    std::shared_ptr<statement> find_statement() { return _statement ? _statement : _parent_expression ? _parent_expression->find_statement() : nullptr; };
    std::shared_ptr<const statement> find_statement() const { return _statement ? _statement : _parent_expression ? _parent_expression->find_statement() : nullptr; };

    std::shared_ptr<expression> get_parent_expression() { return _parent_expression; };
    std::shared_ptr<const expression> get_parent_expression() const { return _parent_expression; };

};


class value_expression : public expression
{
protected:
    /** Value if constructed directly or already resolved from literal. */
    k::value_type _value;

    /** Source literal, if constructed from. */
    k::lex::any_literal::any_of_opt_t _literal;

    value_expression() = delete;

    value_expression(const k::lex::any_literal& literal);

    static std::shared_ptr<type> type_from_literal(const k::lex::any_literal& literal);

public:
    void accept(element_visitor& visitor) override;

    template<typename T>
    explicit value_expression(T val) : _value(val) {}

    explicit value_expression(const std::string& str) : _value(str) {}
    explicit value_expression(std::string&& str) : _value(std::move(str)) {}

    bool is_literal() const {
        return _literal.has_value();
    }

    const lex::any_literal::any_of_opt_t&  any_literal() const {
        return _literal;
    }

    const lex::literal& get_literal()const {
        return _literal.value();
    }

    static std::shared_ptr<value_expression> from_literal(const k::lex::any_literal& literal);

    template<typename T>
    static std::shared_ptr<value_expression> from_value(T val) {
        return std::make_shared<value_expression>(val);
    }

    static std::shared_ptr<value_expression> from_value(const std::string& str) {
        return std::make_shared<value_expression>(str);
    }

};



class symbol_expression : public expression
{
protected:
    // Name of the symbol when not resolved.
    name _name;

    std::variant<
            std::monostate, // Not resolved
            std::shared_ptr<variable_definition>,
            std::shared_ptr<function>
                    > _symbol;

    symbol_expression(const name& name);
    symbol_expression(const std::shared_ptr<variable_definition>& var);
    symbol_expression(const std::shared_ptr<function>& func);

public:
    void accept(element_visitor& visitor) override;

    static std::shared_ptr<symbol_expression> from_string(const std::string& type_name);
    static std::shared_ptr<symbol_expression> from_identifier(const name& type_id);

    const name& get_name() const {
        return _name;
    }

    bool is_variable_def() const {
        return std::holds_alternative<std::shared_ptr<variable_definition>>(_symbol);
    }

    bool is_function() const {
        return std::holds_alternative<std::shared_ptr<function>>(_symbol);
    }

    std::shared_ptr<variable_definition> get_variable_def() const {
        if(is_variable_def()) {
            return std::get<std::shared_ptr<variable_definition>>(_symbol);
        } else {
            return nullptr;
        }
    }

    std::shared_ptr<function> get_function() const {
        if(is_function()) {
            return std::get<std::shared_ptr<function>>(_symbol);
        } else {
            return nullptr;
        }
    }

    bool is_resolved() const {
        return _symbol.index()!=0;
    }

    void resolve(std::shared_ptr<variable_definition> var);
    void resolve(std::shared_ptr<function> func);
};

class unary_expression  : public expression {
protected:
    /** Sub expression. */
    std::shared_ptr<expression> _sub_expr;
    std::shared_ptr<k::parse::ast::unary_expression> _ast_unary_expr;

    unary_expression() = default;

    unary_expression(const std::shared_ptr<expression> &sub_expr)
            : _sub_expr(sub_expr)
    {
        _sub_expr->set_parent_expression(shared_as<expression>());
    }

    friend class symbol_type_resolver;
    void assign(const std::shared_ptr<expression> &sub_expr) {
        _sub_expr = sub_expr;
        _sub_expr->set_parent_expression(shared_as<expression>());
    }

public:
    void accept(element_visitor& visitor) override;

    const std::shared_ptr<expression>& sub_expr() const {
        return _sub_expr;
    }

    std::shared_ptr<expression>& sub_expr() {
        return _sub_expr;
    }

    void set_ast_unary_expr(const std::shared_ptr<k::parse::ast::unary_expression>& expr) {
        _ast_unary_expr = expr;
    }

    const std::shared_ptr<k::parse::ast::unary_expression>& get_ast_unary_expr() const {
        return _ast_unary_expr;
    }


};

class binary_expression : public expression
{
protected:
    /** Left hand sub expression. */
    std::shared_ptr<expression> _left_expr;
    /** Right hand sub expression. */
    std::shared_ptr<expression> _right_expr;

    binary_expression() = default;

    binary_expression(const std::shared_ptr<expression> &leftExpr, const std::shared_ptr<expression> &rightExpr)
            : _left_expr(leftExpr), _right_expr(rightExpr)
    {
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

    friend class symbol_type_resolver;
    void assign_right(const std::shared_ptr<expression> &right_expr) {
        _right_expr = right_expr;
        _right_expr->set_parent_expression(shared_as<expression>());
    }

public:
    void accept(element_visitor& visitor) override;

    const std::shared_ptr<expression>& left() const {
        return _left_expr;
    }

    std::shared_ptr<expression>& left() {
        return _left_expr;
    }

    const std::shared_ptr<expression>& right() const {
        return _right_expr;
    }

    std::shared_ptr<expression>& right() {
        return _right_expr;
    }
};

class arithmetic_binary_expression : public binary_expression
{
protected:
    arithmetic_binary_expression() = default;

public:
    void accept(element_visitor& visitor) override;
};

class addition_expression : public arithmetic_binary_expression
{
protected:
    addition_expression() = default;

public:
    void accept(element_visitor& visitor) override;

    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<addition_expression> expr{ new addition_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class substraction_expression : public arithmetic_binary_expression
{
protected:
    substraction_expression() = default;
public:
    void accept(element_visitor& visitor) override;


    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<substraction_expression> expr{ new substraction_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class multiplication_expression : public arithmetic_binary_expression
{
protected:
    multiplication_expression() = default;
public:
    void accept(element_visitor& visitor) override;

    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<multiplication_expression> expr{ new multiplication_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class division_expression : public arithmetic_binary_expression
{
protected:
    division_expression() = default;
public:
    void accept(element_visitor& visitor) override;

    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<division_expression> expr{ new division_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class modulo_expression : public arithmetic_binary_expression
{
protected:
    modulo_expression() = default;
public:
    void accept(element_visitor& visitor) override;

    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<modulo_expression> expr{ new modulo_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class bitwise_and_expression : public arithmetic_binary_expression
{
protected:
    bitwise_and_expression() = default;

public:
    void accept(element_visitor& visitor) override;

    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<bitwise_and_expression> expr{ new bitwise_and_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class bitwise_or_expression : public arithmetic_binary_expression
{
protected:
    bitwise_or_expression() = default;

public:
    void accept(element_visitor& visitor) override;

    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<bitwise_or_expression> expr{ new bitwise_or_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class bitwise_xor_expression : public arithmetic_binary_expression
{
protected:
    bitwise_xor_expression() = default;

public:
    void accept(element_visitor& visitor) override;

    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<bitwise_xor_expression> expr{ new bitwise_xor_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class left_shift_expression : public arithmetic_binary_expression
{
protected:
    left_shift_expression() = default;

public:
    void accept(element_visitor& visitor) override;

    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<left_shift_expression> expr{ new left_shift_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class right_shift_expression : public arithmetic_binary_expression
{
protected:
    right_shift_expression() = default;

public:
    void accept(element_visitor& visitor) override;

    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<right_shift_expression> expr{ new right_shift_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};


class assignation_expression : public binary_expression {
protected:
    assignation_expression() = default;

public:
    void accept(element_visitor &visitor) override;
};


class simple_assignation_expression : public assignation_expression
{
protected:
    simple_assignation_expression() = default;
public:
    void accept(element_visitor& visitor) override;

    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<simple_assignation_expression> expr{new simple_assignation_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class additition_assignation_expression : public assignation_expression
{
protected:
    additition_assignation_expression() = default;
public:
    void accept(element_visitor& visitor) override;

    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<additition_assignation_expression> expr{ new additition_assignation_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class substraction_assignation_expression : public assignation_expression
{
protected:
    substraction_assignation_expression() = default;
public:
    void accept(element_visitor& visitor) override;

    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<substraction_assignation_expression> expr{ new substraction_assignation_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class multiplication_assignation_expression : public assignation_expression
{
protected:
    multiplication_assignation_expression() = default;
public:
    void accept(element_visitor& visitor) override;

    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<multiplication_assignation_expression> expr{ new multiplication_assignation_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class division_assignation_expression : public assignation_expression
{
protected:
    division_assignation_expression() = default;
public:
    void accept(element_visitor& visitor) override;

    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<division_assignation_expression> expr{ new division_assignation_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class modulo_assignation_expression : public assignation_expression
{
protected:
    modulo_assignation_expression() = default;
public:
    void accept(element_visitor& visitor) override;

    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<modulo_assignation_expression> expr{ new modulo_assignation_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class bitwise_and_assignation_expression : public assignation_expression
{
protected:
    bitwise_and_assignation_expression() = default;
public:
    void accept(element_visitor& visitor) override;

    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<bitwise_and_assignation_expression> expr{ new bitwise_and_assignation_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class bitwise_or_assignation_expression : public assignation_expression
{
protected:
    bitwise_or_assignation_expression() = default;
public:
    void accept(element_visitor& visitor) override;

    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<bitwise_or_assignation_expression> expr{ new bitwise_or_assignation_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class bitwise_xor_assignation_expression : public assignation_expression
{
protected:
    bitwise_xor_assignation_expression() = default;
public:
    void accept(element_visitor& visitor) override;

    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<bitwise_xor_assignation_expression> expr{ new bitwise_xor_assignation_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class left_shift_assignation_expression : public assignation_expression
{
protected:
    left_shift_assignation_expression() = default;
public:
    void accept(element_visitor& visitor) override;

    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<left_shift_assignation_expression> expr{ new left_shift_assignation_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class right_shift_assignation_expression : public assignation_expression
{
protected:
    right_shift_assignation_expression() = default;
public:
    void accept(element_visitor& visitor) override;

    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<right_shift_assignation_expression> expr{ new right_shift_assignation_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class arithmetic_unary_expression : public unary_expression
{
protected:
    arithmetic_unary_expression() = default;

public:
    void accept(element_visitor& visitor) override;
};

class unary_plus_expression : public arithmetic_unary_expression
{
protected:
    unary_plus_expression() = default;

public:
    void accept(element_visitor& visitor) override;

    static std::shared_ptr<unary_expression> make_shared(const std::shared_ptr<expression> &sub_expr) {
        std::shared_ptr<unary_plus_expression> expr{ new unary_plus_expression()};
        expr->assign(sub_expr);
        return std::shared_ptr<unary_expression>{expr};
    }
};

class unary_minus_expression : public arithmetic_unary_expression
{
protected:
    unary_minus_expression() = default;

public:
    void accept(element_visitor& visitor) override;

    static std::shared_ptr<unary_expression> make_shared(const std::shared_ptr<expression> &sub_expr) {
        std::shared_ptr<unary_minus_expression> expr{ new unary_minus_expression()};
        expr->assign(sub_expr);
        return std::shared_ptr<unary_expression>{expr};
    }
};

class bitwise_not_expression : public arithmetic_unary_expression
{
protected:
    bitwise_not_expression() = default;

public:
    void accept(element_visitor& visitor) override;

    static std::shared_ptr<unary_expression> make_shared(const std::shared_ptr<expression> &sub_expr) {
        std::shared_ptr<bitwise_not_expression> expr{ new bitwise_not_expression()};
        expr->assign(sub_expr);
        return std::shared_ptr<unary_expression>{expr};
    }
};


class logical_binary_expression : public binary_expression
{
protected:
    logical_binary_expression() = default;

public:
    void accept(element_visitor& visitor) override;
};

class logical_and_expression : public logical_binary_expression
{
protected:
    logical_and_expression() = default;

public:
    void accept(element_visitor& visitor) override;

    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<logical_and_expression> expr{ new logical_and_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class logical_or_expression : public logical_binary_expression
{
protected:
    logical_or_expression() = default;

public:
    void accept(element_visitor& visitor) override;

    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<logical_or_expression> expr{ new logical_or_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class logical_not_expression : public unary_expression
{
protected:
    logical_not_expression() = default;

public:
    void accept(element_visitor& visitor) override;

    static std::shared_ptr<unary_expression> make_shared(const std::shared_ptr<expression> &sub_expr) {
        std::shared_ptr<logical_not_expression> expr{ new logical_not_expression()};
        expr->assign(sub_expr);
        return std::shared_ptr<unary_expression>{expr};
    }
};

class comparison_expression : public binary_expression
{
protected:
    comparison_expression() = default;

public:
    void accept(element_visitor& visitor) override;
};

class equal_expression : public comparison_expression
{
protected:
    equal_expression() = default;

public:
    void accept(element_visitor& visitor) override;

    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<equal_expression> expr{ new equal_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class different_expression : public comparison_expression
{
protected:
    different_expression() = default;

public:
    void accept(element_visitor& visitor) override;

    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<different_expression> expr{ new different_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class lesser_expression : public comparison_expression
{
protected:
    lesser_expression() = default;

public:
    void accept(element_visitor& visitor) override;

    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<lesser_expression> expr{ new lesser_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class greater_expression : public comparison_expression
{
protected:
    greater_expression() = default;

public:
    void accept(element_visitor& visitor) override;

    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<greater_expression> expr{ new greater_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};


class lesser_equal_expression : public comparison_expression
{
protected:
    lesser_equal_expression() = default;

public:
    void accept(element_visitor& visitor) override;

    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<lesser_equal_expression> expr{ new lesser_equal_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class greater_equal_expression : public comparison_expression
{
protected:
    greater_equal_expression() = default;

public:
    void accept(element_visitor& visitor) override;

    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<greater_equal_expression> expr{ new greater_equal_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class cast_expression : public unary_expression
{
protected:
    // Casting type
    std::shared_ptr<type> _cast_type;

    cast_expression() = default;
public:
    void accept(element_visitor& visitor) override;

    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &expr, const std::shared_ptr<type> &type) {
        std::shared_ptr<cast_expression> rexpr{ new cast_expression()};
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


class function_invocation_expression : public expression
{
protected:
    /** Callee function to call. */
    std::shared_ptr<expression> _callee_expr;
    /** Right hand sub expression. */
    std::vector<std::shared_ptr<expression>> _arguments;


    function_invocation_expression() = default;

    function_invocation_expression(const std::shared_ptr<expression> &callee_expr)
            : _callee_expr(callee_expr)
    {
        _callee_expr->set_parent_expression(shared_as<expression>());
    }

    function_invocation_expression(const std::shared_ptr<expression> &callee_expr, const std::shared_ptr<expression> &arg_expr)
            : _callee_expr(callee_expr)
    {
        _callee_expr->set_parent_expression(shared_as<expression>());
        arg_expr->set_parent_expression(shared_as<expression>());
        _arguments.push_back(arg_expr);
    }

    function_invocation_expression(const std::shared_ptr<expression> &callee_expr, const std::vector<std::shared_ptr<expression>> &args)
            : _callee_expr(callee_expr)
    {
        _callee_expr->set_parent_expression(shared_as<expression>());
        _arguments = args;
        for(auto& arg : args) {
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
        for(auto& arg : _arguments) {
            arg->set_parent_expression(shared_as<expression>());
        }
    }

    void assign_argument(size_t index, const std::shared_ptr<expression>& arg) {
        if(index >= _arguments.size()) {
            // Cannot assign aan argument out of existing arguments bound.
        } else {
            _arguments[index] = arg;
            arg->set_parent_expression(shared_as<expression>());
        }
    }

    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &callee_expr, const std::vector<std::shared_ptr<expression>> &args) {
        std::shared_ptr<function_invocation_expression> expr{ new function_invocation_expression()};
        expr->assign(callee_expr, args);
        return std::shared_ptr<expression>{expr};
    }

public:
    void accept(element_visitor& visitor) override;

};




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
    void accept(element_visitor& visitor) override;

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

    void accept(element_visitor& visitor) override;

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

    void accept(element_visitor& visitor) override;

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

    void accept(element_visitor& visitor) override;

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

    void accept(element_visitor& visitor) override;

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

    void accept(element_visitor& visitor) override;

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
    void accept(element_visitor& visitor) override;

    void set_as_parameter(std::shared_ptr<parameter> func_param) {
        _func_param = func_param;
    }

    std::shared_ptr<parameter> get_as_parameter() const {
        return _func_param;
    }

    bool is_parameter() const {
        return (bool)_func_param;
    }

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

    void accept(element_visitor& visitor) override;

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



/**
 * Element part of a namespace.
 */
class ns_element : public element {
protected:
    /** Unit this element is declared in. */
    unit& _unit;

    /** Parent namespace, null if no parent (root ns only). */
    std::shared_ptr<ns> _parent_ns;

    ns_element() = delete;
    virtual ~ns_element() = default;
    ns_element(const ns_element& elem) = default;
    ns_element(ns_element&& elem) = default;

    ns_element(unit& unit, std::shared_ptr<ns> parent) : _unit(unit), _parent_ns(std::move(parent)) {}

public:

    void accept(element_visitor& visitor) override;

    /**
     * Retrieve the unit this element is declared in.
     * @return Unit reference
     */
    unit& get_unit() {return _unit;}
    const unit& get_unit() const {return _unit;}

    /**
     * Retrieve the namespace this element is declared in.
     * @return Parent namespace, null if no parent (root ns only).
     */
    std::shared_ptr<ns> parent_ns() {return _parent_ns;}
    std::shared_ptr<const ns> parent_ns() const {return _parent_ns;}

};


class parameter : public variable_definition {
protected:

    friend class function;
    friend class gen::unit_llvm_ir_gen;

    std::shared_ptr<function> _function;

    size_t _pos;

    parameter(std::shared_ptr<function> func, size_t pos);
    parameter(std::shared_ptr<function> func, const std::string &name, const std::shared_ptr<type> &type, size_t pos);
public:

    size_t get_pos() const {
        return _pos;
    }

    std::shared_ptr<function> get_function() {return _function;}
    std::shared_ptr<const function> get_function() const {return _function;}
};

class function : public ns_element {
protected:

    friend class ns;
    friend class gen::unit_llvm_ir_gen;

    std::string _name;

    std::shared_ptr<type> _return_type;
    std::vector<std::shared_ptr<parameter>> _parameters;

    std::shared_ptr<block> _block;

    function(std::shared_ptr<ns> ns, const std::string& name);

public:
    void accept(element_visitor& visitor) override;

    const std::string& name() const {return _name;}

    void return_type(std::shared_ptr<type> return_type);
    std::shared_ptr<type> return_type() {return _return_type;}
    std::shared_ptr<const type> return_type() const {return _return_type;}

    const std::vector<std::shared_ptr<parameter>>& parameters() const {
        return _parameters;
    }

    std::shared_ptr<parameter> append_parameter(const std::string& name, std::shared_ptr<type> type);
    std::shared_ptr<parameter> insert_parameter(const std::string& name, std::shared_ptr<type> type, size_t pos);

    std::shared_ptr<parameter> get_parameter(size_t index);
    std::shared_ptr<const parameter> get_parameter(size_t index)const;

    std::shared_ptr<parameter> get_parameter(const std::string& name);
    std::shared_ptr<const parameter> get_parameter(const std::string& name)const;

    void set_block(const std::shared_ptr<block>& block);
    std::shared_ptr<block> get_block();
};


class global_variable_definition : public ns_element, public variable_definition {
protected:

    friend class ns;
    friend class gen::unit_llvm_ir_gen;

    global_variable_definition(std::shared_ptr<ns> ns);

    global_variable_definition(std::shared_ptr<ns> ns, const std::string& name);

public:
    void accept(element_visitor& visitor) override;

};


class ns : public ns_element, public variable_holder {
private:
    ns(unit& unit, std::shared_ptr<ns> parent, const std::string& name);

protected:

    friend class unit;

    /** Name of the namespace. */
    std::string _name;

    /** Collection of all children of this namespace. */
    std::vector<std::shared_ptr<ns_element>> _children;

    /** Map of direct child namespaces. */
    std::map<std::string, std::shared_ptr<ns>> _ns;

    /** Map of all vars defined in this namespace. */
    std::map<std::string, std::shared_ptr<global_variable_definition>> _vars;

    static std::shared_ptr<ns> create(unit& unit, std::shared_ptr<ns> parent, const std::string& name);

public:

    void accept(element_visitor& visitor) override;

    const std::string& get_name() const {
        return _name;
    }


    //
    // This namespace manipulations
    //

    /**
     * Test if this namespace is the root namespace.
     * @return True if root namespace, false otherwise.
     */
    bool is_root() const { return !_parent_ns; }

    //
    // Children namespace manipulations
    //

    /**
     * Retrieve the direct child namespace of given name, creating it if not found.
     * @param child_name Child namespace name to look for.
     * @return The child namespace.
     */
    std::shared_ptr<ns> get_child_namespace(const std::string& child_name);

    /**
     * Retrieve the direct child namespace of given name.
     * @param child_name Child namespace name to look for.
     * @return The child namespace, null if not found.
     */
    std::shared_ptr<const ns> get_child_namespace(const std::string& child_name)const;


    //
    // Children functions
    //

    const std::vector<std::shared_ptr<ns_element>>& get_children() const {
        return _children;
    }

    std::shared_ptr<function> define_function(const std::string& name);
    std::shared_ptr<function> get_function(const std::string& name);
    std::shared_ptr<function> lookup_function(const std::string& name);

    std::shared_ptr<variable_definition> append_variable(const std::string &name) override;
    std::shared_ptr<variable_definition> get_variable(const std::string& name) override;
    std::shared_ptr<variable_definition> lookup_variable(const std::string& name) override;
};



class unit : public element {
protected:

    /** Unit name */
    name _unit_name;

    /** Root namespace.*/
    std::shared_ptr<ns> _root_ns;

public:

    unit();

    void accept(element_visitor& visitor) override;

    /**
     * Get the unit name.
     * @return Unit name identifier
     */
    name get_unit_name() const {
        return _unit_name;
    }

    /**
     * Set the unit name
     * @param unit_name New unit name
     */
    void set_unit_name(const name& unit_name) {
        _unit_name = unit_name;
    }


    //
    // Imports
    //
    //void add_import(const std::string& import_name);


    //
    // Namespaces
    //

    /**
     * Retrieve the root namespace of this unit.
     * @return The root namespace.
     */
    std::shared_ptr<ns> get_root_namespace() {
        return _root_ns;
    }
    std::shared_ptr<const ns> get_root_namespace() const {
        return _root_ns;
    }

    /**
     * Find a namespace, declaring it if needed.
     * @param name Full name of the namespace. Empty or '::' will return the global namespace.
     * @return Namespace.
     */
    std::shared_ptr<ns> find_namespace(std::string_view name);

    /**
     * Find a namespace.
     * @param name Full name of the namespace. Empty or '::' will return the global namespace.
     * @return Namespace, null if not found
     */
    std::shared_ptr<const ns> find_namespace(std::string_view name) const;

};



class element_visitor {
public:
    virtual void visit_element(element&) =0;

    virtual void visit_unit(unit&) =0;

    virtual void visit_ns_element(ns_element&) =0;
    virtual void visit_namespace(ns&) =0;
    virtual void visit_function(function&) =0;
    virtual void visit_global_variable_definition(global_variable_definition&) =0;

    virtual void visit_statement(statement&) =0;
    virtual void visit_block(block&) =0;
    virtual void visit_return_statement(return_statement&) =0;
    virtual void visit_if_else_statement(if_else_statement&) =0;
    virtual void visit_while_statement(while_statement&) =0;
    virtual void visit_for_statement(for_statement&) =0;
    virtual void visit_expression_statement(expression_statement&) =0;
    virtual void visit_variable_statement(variable_statement&) =0;

    virtual void visit_expression(expression&) =0;
    virtual void visit_value_expression(value_expression&) =0;
    virtual void visit_symbol_expression(symbol_expression&) =0;

    virtual void visit_unary_expression(unary_expression&) =0;
    virtual void visit_cast_expression(cast_expression&) =0;

    virtual void visit_binary_expression(binary_expression&) =0;
    virtual void visit_arithmetic_binary_expression(arithmetic_binary_expression&) =0;
    virtual void visit_addition_expression(addition_expression&) =0;
    virtual void visit_substraction_expression(substraction_expression&) =0;
    virtual void visit_multiplication_expression(multiplication_expression&) =0;
    virtual void visit_division_expression(division_expression&) =0;
    virtual void visit_modulo_expression(modulo_expression&) =0;
    virtual void visit_bitwise_and_expression(bitwise_and_expression&) =0;
    virtual void visit_bitwise_or_expression(bitwise_or_expression&) =0;
    virtual void visit_bitwise_xor_expression(bitwise_xor_expression&) =0;
    virtual void visit_left_shift_expression(left_shift_expression&) =0;
    virtual void visit_right_shift_expression(right_shift_expression&) =0;

    virtual void visit_assignation_expression(assignation_expression&) =0;
    virtual void visit_simple_assignation_expression(simple_assignation_expression&) =0;
    virtual void visit_addition_assignation_expression(additition_assignation_expression&) =0;
    virtual void visit_substraction_assignation_expression(substraction_assignation_expression&) =0;
    virtual void visit_multiplication_assignation_expression(multiplication_assignation_expression&) =0;
    virtual void visit_division_assignation_expression(division_assignation_expression&) =0;
    virtual void visit_modulo_assignation_expression(modulo_assignation_expression&) =0;
    virtual void visit_bitwise_and_assignation_expression(bitwise_and_assignation_expression&) =0;
    virtual void visit_bitwise_or_assignation_expression(bitwise_or_assignation_expression&) =0;
    virtual void visit_bitwise_xor_assignation_expression(bitwise_xor_assignation_expression&) =0;
    virtual void visit_left_shift_assignation_expression(left_shift_assignation_expression&) =0;
    virtual void visit_right_shift_assignation_expression(right_shift_assignation_expression&) =0;

    virtual void visit_arithmetic_unary_expression(arithmetic_unary_expression&) =0;
    virtual void visit_unary_plus_expression(unary_plus_expression&) =0;
    virtual void visit_unary_minus_expression(unary_minus_expression&) =0;
    virtual void visit_bitwise_not_expression(bitwise_not_expression&) =0;

    virtual void visit_logical_binary_expression(logical_binary_expression&) =0;
    virtual void visit_logical_and_expression(logical_and_expression&) =0;
    virtual void visit_logical_or_expression(logical_or_expression&) =0;
    virtual void visit_logical_not_expression(logical_not_expression&) =0;

    virtual void visit_comparison_expression(comparison_expression&) =0;
    virtual void visit_equal_expression(equal_expression&) =0;
    virtual void visit_different_expression(different_expression&) =0;
    virtual void visit_lesser_expression(lesser_expression&) =0;
    virtual void visit_greater_expression(greater_expression&) =0;
    virtual void visit_lesser_equal_expression(lesser_equal_expression&) =0;
    virtual void visit_greater_equal_expression(greater_equal_expression&) =0;

    virtual void visit_function_invocation_expression(function_invocation_expression&) =0;
};


class default_element_visitor : public element_visitor {
public:
    virtual void visit_element(element&) override;

    virtual void visit_unit(unit&) override;

    virtual void visit_ns_element(ns_element&) override;
    virtual void visit_namespace(ns&) override;
    virtual void visit_function(function&) override;
    virtual void visit_global_variable_definition(global_variable_definition&) override;

    virtual void visit_statement(statement&) override;
    virtual void visit_block(block&) override;
    virtual void visit_return_statement(return_statement&) override;
    virtual void visit_if_else_statement(if_else_statement&) override;
    virtual void visit_while_statement(while_statement&) override;
    virtual void visit_for_statement(for_statement&) override;
    virtual void visit_expression_statement(expression_statement&) override;
    virtual void visit_variable_statement(variable_statement&) override;

    virtual void visit_expression(expression&) override;
    virtual void visit_value_expression(value_expression&) override;
    virtual void visit_symbol_expression(symbol_expression&) override;

    virtual void visit_unary_expression(unary_expression&) override;
    virtual void visit_cast_expression(cast_expression&) override;

    virtual void visit_binary_expression(binary_expression&) override;

    virtual void visit_arithmetic_binary_expression(arithmetic_binary_expression&) override;
    virtual void visit_addition_expression(addition_expression&) override;
    virtual void visit_substraction_expression(substraction_expression&) override;
    virtual void visit_multiplication_expression(multiplication_expression&) override;
    virtual void visit_division_expression(division_expression&) override;
    virtual void visit_modulo_expression(modulo_expression&) override;
    virtual void visit_bitwise_and_expression(bitwise_and_expression&) override;
    virtual void visit_bitwise_or_expression(bitwise_or_expression&) override;
    virtual void visit_bitwise_xor_expression(bitwise_xor_expression&) override;
    virtual void visit_left_shift_expression(left_shift_expression&) override;
    virtual void visit_right_shift_expression(right_shift_expression&) override;

    virtual void visit_assignation_expression(assignation_expression&) override;
    virtual void visit_simple_assignation_expression(simple_assignation_expression&) override;
    virtual void visit_addition_assignation_expression(additition_assignation_expression&) override;
    virtual void visit_substraction_assignation_expression(substraction_assignation_expression&) override;
    virtual void visit_multiplication_assignation_expression(multiplication_assignation_expression&) override;
    virtual void visit_division_assignation_expression(division_assignation_expression&) override;
    virtual void visit_modulo_assignation_expression(modulo_assignation_expression&) override;
    virtual void visit_bitwise_and_assignation_expression(bitwise_and_assignation_expression&) override;
    virtual void visit_bitwise_or_assignation_expression(bitwise_or_assignation_expression&) override;
    virtual void visit_bitwise_xor_assignation_expression(bitwise_xor_assignation_expression&) override;
    virtual void visit_left_shift_assignation_expression(left_shift_assignation_expression&) override;
    virtual void visit_right_shift_assignation_expression(right_shift_assignation_expression&) override;

    virtual void visit_arithmetic_unary_expression(arithmetic_unary_expression&) override;
    virtual void visit_unary_plus_expression(unary_plus_expression&) override;
    virtual void visit_unary_minus_expression(unary_minus_expression&) override;
    virtual void visit_bitwise_not_expression(bitwise_not_expression&) override;

    virtual void visit_logical_binary_expression(logical_binary_expression&) override;
    virtual void visit_logical_and_expression(logical_and_expression&) override;
    virtual void visit_logical_or_expression(logical_or_expression&) override;
    virtual void visit_logical_not_expression(logical_not_expression&) override;

    virtual void visit_comparison_expression(comparison_expression&) override;
    virtual void visit_equal_expression(equal_expression&) override;
    virtual void visit_different_expression(different_expression&) override;
    virtual void visit_lesser_expression(lesser_expression&) override;
    virtual void visit_greater_expression(greater_expression&) override;
    virtual void visit_lesser_equal_expression(lesser_equal_expression&) override;
    virtual void visit_greater_equal_expression(greater_equal_expression&) override;

    virtual void visit_function_invocation_expression(function_invocation_expression&) override;
};


} // namespace k::unit

#endif //KLANG_UNIT_HPP
