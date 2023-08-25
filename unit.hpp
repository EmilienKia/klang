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


namespace k::unit {

class type;
class expression;
class statement;
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

class name
{
protected:
    bool _root_prefix = false;
    std::vector<std::string> _identifiers;

public:
    name() =default;

    name(const std::string& name) :
            _root_prefix(false), _identifiers({name}) {}

    name(bool root_prefix, const std::string& name) :
            _root_prefix(root_prefix), _identifiers({name}) {}

    name(bool root_prefix, const std::vector<std::string>& identifiers) :
            _root_prefix(root_prefix), _identifiers(identifiers) {}

    name(bool root_prefix, std::vector<std::string>&& identifiers) :
            _root_prefix(root_prefix), _identifiers(identifiers) {}

    name(bool root_prefix, const std::initializer_list<std::string>& identifiers) :
            _root_prefix(root_prefix), _identifiers(identifiers) {}

    name(const name&) = default;
    name(name&&) = default;

    name& operator=(const name&) = default;
    name& operator=(name&&) = default;

    bool has_root_prefix()const {
        return _root_prefix;
    }

    size_t size() const {
        return _identifiers.size();
    }

    const std::string& at(size_t index) const {
        return _identifiers.at(index);
    }

    const std::string& operator[] (size_t index) const {
        return _identifiers[index];
    }

    bool operator == (const name& other) const {
        if( _root_prefix != other._root_prefix )
            return false;
        if( _identifiers.size() != other._identifiers.size() )
            return false;
        for(size_t i = 0; i < _identifiers.size(); ++i) {
            if(_identifiers[i] != other._identifiers[i])
                return false;
        }
        return true;
    }

    std::string to_string()const {
        std::ostringstream stm;
        stm << (_root_prefix ? "::" : "");
        if(_identifiers.empty()) {
            stm << "<<noidentifier>>";
        } else {
            stm << _identifiers.front();
            for(size_t i = 1; i < _identifiers.size(); ++i) {
                stm << "::" << _identifiers[i];
            }
        }
        return stm.str();
    }

     operator std::string () const{
        return to_string();
    }
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
    variable_definition(const std::string& name, const std::shared_ptr<type> &type) : _name(name) {}

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
 * Base type class
 */
class type : public std::enable_shared_from_this<type>{
public:
    virtual ~type() = default;
};

/**
 * Unresolved type
 */
class unresolved_type : public type {
protected:
    name _type_id;

    unresolved_type(const name& type_id): _type_id(type_id) {}
    unresolved_type(name&& type_id): _type_id(type_id) {}

public:
    static std::shared_ptr<type> from_string(const std::string& type_name);
    static std::shared_ptr<type> from_identifier(const name& type_id);
    static std::shared_ptr<type> from_type_specifier(const k::parse::ast::type_specifier& type_spec);

    const name& type_id() const {return _type_id;}
};

/**
 * Base class for resolved types.
 */
class resolved_type : public type {
public:
};

/**
 * Primitive type
 */
class primitive_type : public resolved_type {
protected:
    enum PRIMITIVE_TYPE {
        BYTE,
        CHAR,
        SHORT,
        INT,
        LONG,
        FLOAT,
        DOUBLE
    }_type;

    primitive_type(const primitive_type&) = default;
    primitive_type(primitive_type&&) = default;

    primitive_type(PRIMITIVE_TYPE type):_type(type){}

private:
    static std::shared_ptr<primitive_type> make_shared(PRIMITIVE_TYPE type);

public:

    const std::string& to_string()const;

    static std::shared_ptr<type> from_string(const std::string& type_name);
    static std::shared_ptr<type> from_keyword(const lex::keyword& kw);

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

    virtual ~expression() = default;


    friend class expression_statement;
    friend class return_statement;
    void set_statement(const std::shared_ptr<statement> &statement) {
        _statement = statement;
    }

    friend class binary_expression;
    friend class function_invocation_expression;
    void set_parent_expression(const std::shared_ptr<expression> &expression) {
        _parent_expression = expression;
    }

public:
    void accept(element_visitor& visitor) override;

    std::shared_ptr<statement> get_statement() { return _statement; };
    std::shared_ptr<const statement> get_statement() const { return _statement; };

    std::shared_ptr<statement> find_statement() { return _statement ? _statement : _parent_expression ? _parent_expression->find_statement() : nullptr; };
    std::shared_ptr<const statement> find_statement() const { return _statement ? _statement : _parent_expression ? _parent_expression->find_statement() : nullptr; };

    std::shared_ptr<expression> get_parent_expression() { return _parent_expression; };
    std::shared_ptr<const expression> get_parent_expression() const { return _parent_expression; };

};


class value_expression : public expression
{
public:
    typedef std::variant<std::monostate,
        std::nullptr_t, bool,
        short, unsigned short,
        int, unsigned int,
        long, unsigned long,
        long long, unsigned long long,
        float, double,
        char, std::string> value_type;

protected:
    /** Value if constructed directly or already resolved from literal. */
    value_type _value;

    /** Source literal, if constructed from. */
    k::lex::any_literal::any_of_opt_t _literal;

    value_expression() = delete;

    value_expression(const k::lex::any_literal& literal) : _literal(literal) {}

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

    void resolve(std::shared_ptr<variable_definition> var) {
        _symbol = var;
    }

    void resolve(std::shared_ptr<function> func) {
        _symbol = func;
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

class addition_expression : public binary_expression
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

class substraction_expression : public binary_expression
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

class multiplication_expression : public binary_expression
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

class division_expression : public binary_expression
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

class modulo_expression : public binary_expression
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

class assignation_expression : public binary_expression
{
protected:
    assignation_expression() = default;
public:
    void accept(element_visitor& visitor) override;

    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<assignation_expression> expr{ new assignation_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
};

class additition_assignation_expression : public binary_expression
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

class substraction_assignation_expression : public binary_expression
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

class multiplication_assignation_expression : public binary_expression
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

class division_assignation_expression : public binary_expression
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

class modulo_assignation_expression : public binary_expression
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
    /** Block owning the statement. */
    std::shared_ptr<block> _block;

    statement(const std::shared_ptr<block>& block) : _block(block) {}
    virtual ~statement() = default;

public:
    void accept(element_visitor& visitor) override;

    std::shared_ptr<block> get_block() { return _block; };
    std::shared_ptr<const block> get_block() const { return _block; };

};

/**
 * Return expression statement
 */
class return_statement : public statement
{
protected:
    friend class block;

    std::shared_ptr<expression> _expression;

    explicit return_statement(const std::shared_ptr<block>& block) :
            statement(block) {}

public:
    void accept(element_visitor& visitor) override;

    std::shared_ptr<expression> get_expression() { return _expression; };
    std::shared_ptr<const expression> get_expression() const { return _expression; };

    return_statement& set_expression(std::shared_ptr<expression> expr) {
        _expression = expr;
        _expression->set_statement(shared_as<statement>());
        return *this;
    }
};


/**
 * Expression statement
 */
class expression_statement : public statement
{
private:
    expression_statement(const std::shared_ptr<block>& block, const std::shared_ptr<expression>& expr) :
            statement(block), _expression(expr) {}

    explicit expression_statement(const std::shared_ptr<block>& block) :
            statement(block) {}

    std::shared_ptr<expression> _expression;

protected:

    friend class block;
    static std::shared_ptr<expression_statement> make_shared(const std::shared_ptr<block>& block) {
        return std::shared_ptr<expression_statement>(new expression_statement(block));
    }

    static std::shared_ptr<expression_statement> make_shared(const std::shared_ptr<block>& block, const std::shared_ptr<expression>& expr) {
        std::shared_ptr<expression_statement> res(new expression_statement(block, expr));
        expr->set_statement(res);
        return res;
    }

public:
    void accept(element_visitor& visitor) override;

    std::shared_ptr<expression> get_expression() { return _expression; };
    std::shared_ptr<const expression> get_expression() const { return _expression; };

    expression_statement& set_expression(std::shared_ptr<expression> expr) {
        _expression = std::move(expr);
        _expression->set_statement(shared_as<statement>());
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
    friend class gen::unit_llvm_ir_gen;

    std::shared_ptr<parameter> _func_param;

    variable_statement(const std::shared_ptr<block> &block) :
            statement(block) {}

    variable_statement(const std::shared_ptr<block> &block, const std::string& name) :
            statement(block), variable_definition(name) {}

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

    /** Function holding this block. */
    std::shared_ptr<function> _function;

    /** Map of all vars defined in this block. */
    std::map<std::string, std::shared_ptr<variable_statement>> _vars;

    /** List of statements of this block. */
    std::vector<std::shared_ptr<statement>> _statements;


    block(const std::shared_ptr<block>& parent) : statement(parent), _function(parent->_function) {}

    block(const std::shared_ptr<function>& function) : statement(nullptr), _function(function) {}


    /**
     * create a block as main block of given function.
     * This new block will be attached to the given function but will not have any parent block.
     * @param func Function to which the block will be attached
     * @return New block
     */
    static std::shared_ptr<block> for_function(std::shared_ptr<function> func);
    /**
     * create a block as sub block of given block.
     * This new block will be attached to the same function than its parent.
     * @param parent Parent of the new block
     * @return New block
     */
    static std::shared_ptr<block> for_block(std::shared_ptr<block> parent);

public:

    void accept(element_visitor& visitor) override;

    const std::vector<std::shared_ptr<statement>>& get_statements() const {
        return _statements;
    }

    std::vector<std::shared_ptr<statement>>& get_statements() {
        return _statements;
    }

    std::shared_ptr<function> get_function() {
        return _function;
    }

    std::shared_ptr<const function> get_function() const {
        return _function;
    }


    std::shared_ptr<return_statement> append_return_statement();

    std::shared_ptr<expression_statement> append_expression_statement();

    std::shared_ptr<expression_statement> append_expression_statement(const std::shared_ptr<expression>& expr);

    std::shared_ptr<block> append_block_statement();

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

    std::vector<std::shared_ptr<parameter>> parameters() const {
        return _parameters;
    }

    std::shared_ptr<parameter> append_parameter(const std::string& name, std::shared_ptr<type> type);
    std::shared_ptr<parameter> insert_parameter(const std::string& name, std::shared_ptr<type> type, size_t pos);

    std::shared_ptr<parameter> get_parameter(size_t index);
    std::shared_ptr<const parameter> get_parameter(size_t index)const;

    std::shared_ptr<parameter> get_parameter(const std::string& name);
    std::shared_ptr<const parameter> get_parameter(const std::string& name)const;

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
    virtual void visit_expression_statement(expression_statement&) =0;
    virtual void visit_variable_statement(variable_statement&) =0;

    virtual void visit_expression(expression&) =0;
    virtual void visit_value_expression(value_expression&) =0;
    virtual void visit_symbol_expression(symbol_expression&) =0;

    virtual void visit_binary_expression(binary_expression&) =0;
    virtual void visit_addition_expression(addition_expression&) =0;
    virtual void visit_substraction_expression(substraction_expression&) =0;
    virtual void visit_multiplication_expression(multiplication_expression&) =0;
    virtual void visit_division_expression(division_expression&) =0;
    virtual void visit_modulo_expression(modulo_expression&) =0;
    virtual void visit_assignation_expression(assignation_expression&) =0;
    virtual void visit_addition_assignation_expression(additition_assignation_expression&) =0;
    virtual void visit_substraction_assignation_expression(substraction_assignation_expression&) =0;
    virtual void visit_multiplication_assignation_expression(multiplication_assignation_expression&) =0;
    virtual void visit_division_assignation_expression(division_assignation_expression&) =0;
    virtual void visit_modulo_assignation_expression(modulo_assignation_expression&) =0;
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
    virtual void visit_expression_statement(expression_statement&) override;
    virtual void visit_variable_statement(variable_statement&) override;

    virtual void visit_expression(expression&) override;
    virtual void visit_value_expression(value_expression&) override;
    virtual void visit_symbol_expression(symbol_expression&) override;

    virtual void visit_binary_expression(binary_expression&) override;
    virtual void visit_addition_expression(addition_expression&) override;
    virtual void visit_substraction_expression(substraction_expression&) override;
    virtual void visit_multiplication_expression(multiplication_expression&) override;
    virtual void visit_division_expression(division_expression&) override;
    virtual void visit_modulo_expression(modulo_expression&) override;
    virtual void visit_assignation_expression(assignation_expression&) override;
    virtual void visit_addition_assignation_expression(additition_assignation_expression&) override;
    virtual void visit_substraction_assignation_expression(substraction_assignation_expression&) override;
    virtual void visit_multiplication_assignation_expression(multiplication_assignation_expression&) override;
    virtual void visit_division_assignation_expression(division_assignation_expression&) override;
    virtual void visit_modulo_assignation_expression(modulo_assignation_expression&) override;
    virtual void visit_function_invocation_expression(function_invocation_expression&) override;
};


} // namespace k::unit

#endif //KLANG_UNIT_HPP
