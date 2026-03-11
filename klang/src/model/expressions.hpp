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

namespace k::parse::ast {
struct unary_expression;
}

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
    // Copy constructor: copies type only, parent is NOT copied (clone is orphan).
    expression(const expression& other) : _type(other._type) {}

    friend class unary_expression;
    friend class binary_expression;
    friend class member_of_expression;
    friend class function_invocation_expression;
    friend class constructor_invocation_expression;
    friend class new_expression;
    friend class delete_expression;
    friend class array_init_expression;

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

    /** Return a deep copy of this expression, without parent (orphan). */
    virtual std::shared_ptr<expression> clone() const = 0;
};

class value_expression : public expression {
protected:
    /** Value if constructed directly or already resolved from literal. */
    k::value_type _value;

    /** Source literal, if constructed from. */
    k::lex::any_literal::any_of_opt_t _literal;

    value_expression() = delete;

    value_expression(const k::lex::any_literal &literal);

    // Copy constructor
    value_expression(const value_expression& other) : expression(other), _value(other._value), _literal(other._literal) {}

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

    std::shared_ptr<expression> clone() const override {
        return std::shared_ptr<value_expression>(new value_expression(*this));
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

    // Copy constructor
    symbol_expression(const symbol_expression& other) : expression(other), _name(other._name), _target(other._target) {}

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

    std::shared_ptr<expression> clone() const override {
        return std::shared_ptr<symbol_expression>(new symbol_expression(*this));
    }
};

class unary_expression : public expression {
protected:
    /** Sub expression. */
    std::shared_ptr<expression> _sub_expr;
    std::shared_ptr<k::parse::ast::unary_expression> _ast_unary_expr;

    unary_expression() = default;
    unary_expression(const unary_expression&) = delete;
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

    std::shared_ptr<expression> clone() const override = 0;
};

class binary_expression : public expression {
protected:
    /** Left hand sub expression. */
    std::shared_ptr<expression> _left_expr;
    /** Right hand sub expression. */
    std::shared_ptr<expression> _right_expr;

    binary_expression() = default;
    binary_expression(const binary_expression&) = delete;
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

    std::shared_ptr<expression> clone() const override = 0;
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
    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<load_value_expression> c{new load_value_expression()};
        c->_type = _type;
        c->_ast_unary_expr = _ast_unary_expr;
        if (_sub_expr) c->assign(_sub_expr->clone());
        return c;
    }
};

/**
 * Owner move expression — transfers ownership from a ref<owner<T>> source.
 * Loads the raw pointer from the source alloca AND nulls out the source alloca.
 * Source expression type: ref<owner<T>>.  Result type: owner<T>.
 * Used internally during type resolution for ownership-transferring contexts:
 *   - return p;  (when function return type is owner<T>)
 *   - a = b;     (when both a and b are owner<T>)
 *   - foo(p);    (when the parameter type is owner<T>)
 *   - q : T! = p;  (owner variable initialisation from another owner variable)
 */
class owner_move_expression : public unary_expression {
protected:
    owner_move_expression() = default;
public:
    void accept(model_visitor& visitor) override;
    static std::shared_ptr<owner_move_expression> make_shared(const std::shared_ptr<expression>& src) {
        std::shared_ptr<owner_move_expression> expr{new owner_move_expression()};
        if (src) expr->assign(src);
        return expr;
    }
    std::shared_ptr<expression> clone() const override {
        auto c = make_shared(_sub_expr ? _sub_expr->clone() : nullptr);
        c->_type = _type;
        return c;
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
    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<address_of_expression> c{new address_of_expression()};
        c->_type = _type;
        c->_ast_unary_expr = _ast_unary_expr;
        if (_sub_expr) c->assign(_sub_expr->clone());
        return c;
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
    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<dereference_expression> c{new dereference_expression()};
        c->_type = _type;
        c->_ast_unary_expr = _ast_unary_expr;
        if (_sub_expr) c->assign(_sub_expr->clone());
        return c;
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

    std::shared_ptr<expression> clone() const override = 0;
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
    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<member_of_object_expression> c{new member_of_object_expression()};
        c->_type = _type;
        c->_ast_unary_expr = _ast_unary_expr;
        auto sym = _symbol ? std::dynamic_pointer_cast<symbol_expression>(_symbol->clone()) : nullptr;
        if (_sub_expr && sym) c->assign(_sub_expr->clone(), sym);
        return c;
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
    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<member_of_pointer_expression> c{new member_of_pointer_expression()};
        c->_type = _type;
        c->_ast_unary_expr = _ast_unary_expr;
        auto sym = _symbol ? std::dynamic_pointer_cast<symbol_expression>(_symbol->clone()) : nullptr;
        if (_sub_expr && sym) c->assign(_sub_expr->clone(), sym);
        return c;
    }
};

/**
 * Pointer-to-member dereference expression.
 * Represents `obj.*mfp` (DOT_STAR) or `ptr->*mfp` (ARROW_STAR).
 * - _left_expr  : the object (or pointer) expression
 * - _right_expr : the member-function-pointer variable expression
 * - _is_arrow   : true for `->*`, false for `.*`
 */
class pm_expression : public binary_expression {
protected:
    bool _is_arrow = false; ///< true for ->*, false for .*

    pm_expression() = default;
public:
    void accept(model_visitor &visitor) override;

    bool is_arrow() const { return _is_arrow; }

    static std::shared_ptr<pm_expression> make_shared(
        const std::shared_ptr<expression>& obj_expr,
        const std::shared_ptr<expression>& mfp_expr,
        bool is_arrow) {
        std::shared_ptr<pm_expression> e{new pm_expression()};
        e->_is_arrow = is_arrow;
        e->assign(obj_expr, mfp_expr);
        return e;
    }

    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<pm_expression> c{new pm_expression()};
        c->_type = _type;
        c->_is_arrow = _is_arrow;
        if (_left_expr && _right_expr) c->assign(_left_expr->clone(), _right_expr->clone());
        return c;
    }
};

class cast_expression : public unary_expression {
protected:
    std::shared_ptr<type> _cast_type;
    /** True when the cast is dynamic and a null result is fatal (target is lnk or ref). */
    bool _null_is_fatal = false;
    cast_expression() = default;
public:
    void accept(model_visitor &visitor) override;
    static std::shared_ptr<expression> make_shared(
            const std::shared_ptr<expression>& expr,
            const std::shared_ptr<type>& type,
            bool null_is_fatal = false) {
        std::shared_ptr<cast_expression> rexpr{new cast_expression()};
        rexpr->assign(expr);
        rexpr->_cast_type = type;
        rexpr->_null_is_fatal = null_is_fatal;
        rexpr->_type = type;
        return std::shared_ptr<expression>{rexpr};
    }
    std::shared_ptr<type> get_cast_type() { return _cast_type; }
    std::shared_ptr<const type> get_cast_type() const { return _cast_type; }
    /** Update the cast target type (used by type_reference_resolver to resolve unresolved types). */
    void set_cast_type(const std::shared_ptr<type>& t) { _cast_type = t; _type = t; }
    /** True if a null result from a dynamic cast is fatal (target is lnk or ref). */
    bool null_is_fatal() const { return _null_is_fatal; }
    /** Set whether a null result from a dynamic cast should trigger a fatal trap. */
    void set_null_is_fatal(bool v) { _null_is_fatal = v; }
    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<cast_expression> c{new cast_expression()};
        c->_type = _type;
        c->_ast_unary_expr = _ast_unary_expr;
        c->_cast_type = _cast_type;
        c->_null_is_fatal = _null_is_fatal;
        if (_sub_expr) c->assign(_sub_expr->clone());
        return c;
    }
};

class subscript_expression : public binary_expression {
protected:
    subscript_expression() = default;
    subscript_expression(const std::shared_ptr<expression> &callee_expr,
                         const std::shared_ptr<expression> &index_expr) :
                         binary_expression(callee_expr, index_expr) {}
public:
    void accept(model_visitor &visitor) override;
    static std::shared_ptr<expression> make_shared(const std::shared_ptr<expression> &left_expr, const std::shared_ptr<expression> &right_expr) {
        std::shared_ptr<subscript_expression> expr{new subscript_expression()};
        expr->assign(left_expr, right_expr);
        return std::shared_ptr<expression>{expr};
    }
    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<subscript_expression> c{new subscript_expression()};
        c->_type = _type;
        if (_left_expr && _right_expr) c->assign(_left_expr->clone(), _right_expr->clone());
        return c;
    }
};

class function_invocation_expression : public expression {
protected:
    /** Callee function to call. */
    std::shared_ptr<expression> _callee_expr;
    /** Right hand sub expression. */
    std::vector<std::shared_ptr<expression>> _arguments;


    function_invocation_expression() = default;
    function_invocation_expression(const function_invocation_expression&) = delete;

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

    /** Replace all arguments, properly setting their parent expression. */
    void assign_arguments(const std::vector<std::shared_ptr<expression>> &args) {
        _arguments = args;
        for (auto& arg : _arguments) {
            if (arg) arg->set_parent_expression(shared_as<expression>());
        }
    }

    void assign(const std::shared_ptr<expression> &callee_expr, const std::vector<std::shared_ptr<expression>> &args);

    void assign_argument(size_t index, const std::shared_ptr<expression> &arg);

    /**
     * True when this call was written with a qualified name, e.g. Base::method(obj).
     * In that case the virtual dispatch mechanism must be bypassed and the exact
     * function named in the callee must be invoked directly (non-virtual call).
     */
    bool _non_virtual_qualified_call = false;

    bool is_non_virtual_qualified_call() const { return _non_virtual_qualified_call; }
    void set_non_virtual_qualified_call(bool v) { _non_virtual_qualified_call = v; }

    /**
     * Phase-3 dispatch annotation set by type_reference_resolver.
     * Describes exactly how this call should be dispatched (direct or vtable).
     * Empty (nullopt) if the resolver has not yet annotated this node.
     */
    std::optional<virtual_dispatch_info> _dispatch_info;

    bool has_dispatch_info() const { return _dispatch_info.has_value(); }
    const virtual_dispatch_info& get_dispatch_info() const { return _dispatch_info.value(); }
    void set_dispatch_info(virtual_dispatch_info info) { _dispatch_info = std::move(info); }
    void clear_dispatch_info() { _dispatch_info.reset(); }

    static std::shared_ptr<function_invocation_expression> make_shared(const std::shared_ptr<expression> &callee_expr, const std::vector<std::shared_ptr<expression>> &args);

    static std::shared_ptr<function_invocation_expression> make_shared(const std::shared_ptr<function> &callee_func, const std::vector<std::shared_ptr<expression>> &args);

public:
    void accept(model_visitor &visitor) override;

    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<function_invocation_expression> c{new function_invocation_expression()};
        c->_type = _type;
        c->_non_virtual_qualified_call = _non_virtual_qualified_call;
        c->_dispatch_info = _dispatch_info;
        std::vector<std::shared_ptr<expression>> args;
        for (auto& a : _arguments) args.push_back(a->clone());
        auto callee = _callee_expr ? _callee_expr->clone() : nullptr;
        if (callee) c->assign(callee, args);
        return c;
    }
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
    constructor_invocation_expression(const constructor_invocation_expression&) = delete;

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

    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<constructor_invocation_expression> c{new constructor_invocation_expression()};
        c->_type = _type;
        c->_constructor = _constructor;
        auto sym = _constructed_symbol
            ? std::dynamic_pointer_cast<symbol_expression>(_constructed_symbol->clone())
            : nullptr;
        std::vector<std::shared_ptr<expression>> args;
        for (auto& a : _arguments) args.push_back(a->clone());
        if (sym) c->assign(sym, args);
        return c;
    }
};

/**
 * New expression — allocates a heap object and returns an owner.
 * Corresponds to AST new_expr.
 * Type of this expression: owner_type<T>.
 */
class new_expression : public expression {
protected:
    /** The type to allocate (resolved). */
    std::shared_ptr<type> _allocated_type;
    /** Constructor arguments (resolved). */
    std::vector<std::shared_ptr<expression>> _arguments;
    /** The selected constructor (resolved in phase 4). */
    std::shared_ptr<constructor> _constructor;

    new_expression() = default;
    new_expression(const new_expression&) = delete;

    friend class gen::symbol_resolver;
    friend class gen::type_reference_resolver;

    void set_constructor(const std::shared_ptr<constructor>& ctor) { _constructor = ctor; }

public:
    void accept(model_visitor& visitor) override;

    const std::shared_ptr<type>& allocated_type() const { return _allocated_type; }
    void allocated_type(const std::shared_ptr<type>& t) { _allocated_type = t; }

    const std::vector<std::shared_ptr<expression>>& arguments() const { return _arguments; }

    void assign_arguments(const std::vector<std::shared_ptr<expression>>& args) {
        _arguments = args;
        for (auto& a : _arguments) if (a) a->set_parent_expression(shared_as<expression>());
    }

    void assign_argument(size_t index, const std::shared_ptr<expression>& arg) {
        _arguments[index] = arg;
        if (arg) arg->set_parent_expression(shared_as<expression>());
    }

    std::shared_ptr<constructor> get_constructor() const { return _constructor; }

    static std::shared_ptr<new_expression> make_shared(
        const std::shared_ptr<type>& allocated_type,
        const std::vector<std::shared_ptr<expression>>& args);

    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<new_expression> c{new new_expression()};
        c->_type = _type;
        c->_allocated_type = _allocated_type;
        c->_constructor = _constructor;
        std::vector<std::shared_ptr<expression>> args;
        for (auto& a : _arguments) args.push_back(a->clone());
        c->assign_arguments(args);
        return c;
    }
};

/**
 * Delete expression — explicitly destroys an owner's object.
 * Corresponds to AST delete_expr.
 * Type of this expression: void.
 */
class delete_expression : public unary_expression {
protected:
    delete_expression() = default;
    delete_expression(const delete_expression&) = delete;

public:
    void accept(model_visitor& visitor) override;

    static std::shared_ptr<delete_expression> make_shared(const std::shared_ptr<expression>& target);

    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<delete_expression> c{new delete_expression()};
        c->_type = _type;
        if (_sub_expr) c->assign(_sub_expr->clone());
        return c;
    }
};

/**
 * Array initializer expression.
 * Represents a brace-init list used to initialize an array variable.
 * Each element is an expression (or nullptr for default-init slots).
 * For aggregate element types, each non-null element may be a function_invocation_expression
 * (explicit constructor call) or a plain expression (implicit single-param constructor).
 *
 * The constructed_symbol is set to the variable being initialized (as for constructor_invocation_expression).
 */
class array_init_expression : public expression {
protected:
    /** The variable being initialized. */
    std::shared_ptr<symbol_expression> _constructed_symbol;

    /** Per-element initializer expressions. nullptr = default-init. */
    std::vector<std::shared_ptr<expression>> _elements;

    array_init_expression() = default;
    array_init_expression(const array_init_expression&) = delete;

    friend class gen::type_reference_resolver;
    friend class gen::implementation_generator;

public:
    const std::shared_ptr<symbol_expression>& constructed_symbol() const { return _constructed_symbol; }

    const std::vector<std::shared_ptr<expression>>& elements() const { return _elements; }

    size_t size() const { return _elements.size(); }

    std::shared_ptr<expression> element(size_t index) const {
        return index < _elements.size() ? _elements[index] : nullptr;
    }

    void assign_element(size_t index, const std::shared_ptr<expression>& elem) {
        _elements[index] = elem;
        if (elem) elem->set_parent_expression(shared_as<expression>());
    }

    void set_elements(const std::vector<std::shared_ptr<expression>>& elems) {
        _elements = elems;
        for (auto& e : _elements) {
            if (e) e->set_parent_expression(shared_as<expression>());
        }
    }

    static std::shared_ptr<array_init_expression> make_shared(
        const std::shared_ptr<symbol_expression>& constructed_symbol,
        const std::vector<std::shared_ptr<expression>>& elements);

    static std::shared_ptr<array_init_expression> make_shared(
        const std::shared_ptr<variable_definition>& variable,
        const std::vector<std::shared_ptr<expression>>& elements);

    void accept(model_visitor& visitor) override;

    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<array_init_expression> c{new array_init_expression()};
        c->_type = _type;
        auto sym = _constructed_symbol
            ? std::dynamic_pointer_cast<symbol_expression>(_constructed_symbol->clone())
            : nullptr;
        std::vector<std::shared_ptr<expression>> elems;
        for (auto& e : _elements) elems.push_back(e ? e->clone() : nullptr);
        if (sym) {
            c->_constructed_symbol = sym;
            sym->set_parent_expression(c);
        }
        c->set_elements(elems);
        return c;
    }
};


} // namespace k::model
#endif //KLANG_MODEL_EXPRESSIONS_HPP
