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

#ifndef KLANG_MODEL_EXPRESSIONS_UNARY_HPP
#define KLANG_MODEL_EXPRESSIONS_UNARY_HPP
#include "expressions_base.hpp"

namespace k::model {

class unary_expression : public expression {
protected:
    /** Sub expression. */
    std::shared_ptr<expression> _sub_expr;

    /**
     * Resolved operator overload function, set during type resolution.
     * When non-null, the code generator should produce a function call to this operator
     * function instead of a primitive operation.
     */
    std::shared_ptr<function> _operator_func;

    /**
     * Dispatch info for operator overload virtual dispatch.
     * Only meaningful when _operator_func is set and virtual.
     */
    std::optional<virtual_dispatch_info> _operator_dispatch_info;

    unary_expression() = default;
    unary_expression(const unary_expression&) = delete;
    unary_expression(const std::shared_ptr<expression> &sub_expr)
            : _sub_expr(sub_expr) {
        _sub_expr->set_parent_expression(shared_as<expression>());
    }

    friend class gen::symbol_resolver;
    friend class gen::type_reference_resolver;
    friend class template_instantiator;

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
        _ast_node = std::static_pointer_cast<k::parse::ast::ast_node>(expr);
    }

    std::shared_ptr<k::parse::ast::unary_expression> get_ast_unary_expr() const {
        return get_ast_node_as<k::parse::ast::unary_expression>();
    }

    /** Resolved operator overload function (nullptr if primitive operation). */
    std::shared_ptr<function> get_operator_func() const { return _operator_func; }
    void set_operator_func(std::shared_ptr<function> f) { _operator_func = std::move(f); }
    bool has_operator_overload() const { return _operator_func != nullptr; }

    /** Dispatch info for virtual operator calls. */
    bool has_operator_dispatch_info() const { return _operator_dispatch_info.has_value(); }
    const virtual_dispatch_info& get_operator_dispatch_info() const { return _operator_dispatch_info.value(); }
    void set_operator_dispatch_info(virtual_dispatch_info info) { _operator_dispatch_info = std::move(info); }

    std::shared_ptr<expression> clone() const override = 0;

    std::optional<k::lex::any_lexeme> first_lexeme() const override;
    std::optional<k::lex::any_lexeme> last_lexeme() const override;
};

class binary_expression : public expression {
protected:
    /** Left hand sub expression. */
    std::shared_ptr<expression> _left_expr;
    /** Right hand sub expression. */
    std::shared_ptr<expression> _right_expr;

    /**
     * Resolved operator overload function, set during type resolution.
     * When non-null, the code generator should produce a function call to this operator
     * function instead of a primitive operation.
     */
    std::shared_ptr<function> _operator_func;

    /**
     * Dispatch info for operator overload virtual dispatch.
     * Only meaningful when _operator_func is set and virtual.
     */
    std::optional<virtual_dispatch_info> _operator_dispatch_info;

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
    friend class template_instantiator;

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

    /** Resolved operator overload function (nullptr if primitive operation). */
    std::shared_ptr<function> get_operator_func() const { return _operator_func; }
    void set_operator_func(std::shared_ptr<function> f) { _operator_func = std::move(f); }
    bool has_operator_overload() const { return _operator_func != nullptr; }

    /** Dispatch info for virtual operator calls. */
    bool has_operator_dispatch_info() const { return _operator_dispatch_info.has_value(); }
    const virtual_dispatch_info& get_operator_dispatch_info() const { return _operator_dispatch_info.value(); }
    void set_operator_dispatch_info(virtual_dispatch_info info) { _operator_dispatch_info = std::move(info); }

    std::shared_ptr<expression> clone() const override = 0;

    std::optional<k::lex::any_lexeme> first_lexeme() const override;
    std::optional<k::lex::any_lexeme> last_lexeme() const override;
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
        c->_ast_node = _ast_node;
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
        c->_ast_node = _ast_node;
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
        c->_ast_node = _ast_node;
        if (_sub_expr) c->assign(_sub_expr->clone());
        return c;
    }
};

/**
 * Drain expression (#expr).
 * Produces a drain indirection (T#) from an lvalue, granting the consumer
 * the permission to steal the internal resources of the referenced object.
 */
class drain_expression : public unary_expression {
protected:
    drain_expression() = default;
public:
    void accept(model_visitor &visitor) override;
    static std::shared_ptr<unary_expression> make_shared(const std::shared_ptr<expression> &sub_expr) {
        std::shared_ptr<drain_expression> expr{new drain_expression()};
        expr->assign(sub_expr);
        return std::shared_ptr<unary_expression>{expr};
    }
    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<drain_expression> c{new drain_expression()};
        c->_type = _type;
        c->_ast_node = _ast_node;
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
        c->_ast_node = _ast_node;
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
        c->_ast_node = _ast_node;
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
        c->_ast_node = _ast_node;
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
        c->_ast_node = _ast_node;
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

} // namespace k::model
#endif //KLANG_MODEL_EXPRESSIONS_UNARY_HPP
