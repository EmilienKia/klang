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

#ifndef KLANG_MODEL_EXPRESSIONS_INVOCATION_HPP
#define KLANG_MODEL_EXPRESSIONS_INVOCATION_HPP
#include "expressions_unary.hpp"

namespace k::model {

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
        if (_callee_expr) _callee_expr->set_parent_expression(shared_as<expression>());
    }

    const std::vector<std::shared_ptr<expression>> &arguments() const {
        return _arguments;
    }

    void arguments(const std::vector<std::shared_ptr<expression>> &arguments) {
        _arguments = arguments;
        for (auto &arg : _arguments) {
            if (arg) arg->set_parent_expression(shared_as<expression>());
        }
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
        for (auto &arg : _arguments) {
            if (arg) arg->set_parent_expression(shared_as<expression>());
        }
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
 * Temporary anonymous object construction expression.
 * Represents `S(args...)` used as a sub-expression (not a variable initialization).
 * Allocates a stack temporary, calls the constructor, and registers the object for
 * destructor cleanup at the end of the enclosing full-expression.
 *
 * Unlike constructor_invocation_expression, this expression does NOT reference a
 * named variable — the temporary is anonymous and its lifetime is the enclosing
 * full-expression.
 *
 * Result type: struct_type (the aggregate type being constructed).
 */
class temporary_construction_expression : public expression {
protected:
    /** The type being constructed (must be a struct_type once resolved). */
    std::shared_ptr<type> _constructed_type;

    /** Construction argument expressions. */
    std::vector<std::shared_ptr<expression>> _arguments;

    /** Resolved constructor to call (set during type resolution). */
    std::shared_ptr<constructor> _constructor;

    temporary_construction_expression() = default;
    temporary_construction_expression(const temporary_construction_expression&) = delete;

    friend class gen::type_reference_resolver;
    friend class gen::implementation_generator;

public:

    const std::shared_ptr<type>& constructed_type() const { return _constructed_type; }

    const std::vector<std::shared_ptr<expression>>& arguments() const { return _arguments; }

    size_t size() const { return _arguments.size(); }

    bool empty() const { return _arguments.empty(); }

    std::shared_ptr<expression> argument(size_t index) const { return _arguments[index]; }

    void assign_arguments(const std::vector<std::shared_ptr<expression>>& args) {
        _arguments = args;
        for (auto& a : _arguments) if (a) a->set_parent_expression(shared_as<expression>());
    }

    void assign_argument(size_t index, const std::shared_ptr<expression>& arg) {
        if (index < _arguments.size()) {
            _arguments[index] = arg;
            if (arg) arg->set_parent_expression(shared_as<expression>());
        }
    }

    std::shared_ptr<constructor> get_constructor() const { return _constructor; }

    void set_constructor(const std::shared_ptr<constructor>& ctor) { _constructor = ctor; }

    static std::shared_ptr<temporary_construction_expression> make_shared(
        const std::shared_ptr<type>& constructed_type,
        const std::vector<std::shared_ptr<expression>>& args);

    void accept(model_visitor& visitor) override;

    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<temporary_construction_expression> c{new temporary_construction_expression()};
        c->_type = _type;
        c->_constructed_type = _constructed_type;
        c->_constructor = _constructor;
        std::vector<std::shared_ptr<expression>> args;
        for (auto& a : _arguments) args.push_back(a->clone());
        c->assign_arguments(args);
        return c;
    }
};

/**
 * New expression — allocates a heap object (or array) and returns an owner.
 * Corresponds to AST new_expr.
 *
 * Single-object form:  new T(args...)      → owner_type<T>
 * Array form:          new T[N]{e0,e1,...}  → owner_type<sized_array_type<T,N>>
 *
 * For the array form:
 *   - _is_array is true
 *   - _array_size_expr is the expression for the array size (may be nullptr if inferred)
 *   - _array_init_elements holds the per-element initializer expressions (nullptr = default-init)
 *   - _array_size is the resolved integer array size (set during type resolution)
 *   - _element_constructors[i] is the constructor chosen for element i (for struct elements)
 */
class new_expression : public expression {
protected:
    /** The type to allocate (resolved element type for arrays, object type for single). */
    std::shared_ptr<type> _allocated_type;
    /** Constructor arguments for single-object new (resolved). */
    std::vector<std::shared_ptr<expression>> _arguments;
    /** The selected constructor for single-object new (resolved in phase 4). */
    std::shared_ptr<constructor> _constructor;

    /** True when this is an array allocation: new T[N]{...} */
    bool _is_array = false;
    /** True when the array size is a runtime expression (not a compile-time constant). */
    bool _is_dynamic_size = false;
    /** Array size expression (from brackets), nullptr if inferred from init list. */
    std::shared_ptr<expression> _array_size_expr;
    /** Per-element initializer expressions for array form. nullptr = default-init. */
    std::vector<std::shared_ptr<expression>> _array_init_elements;
    /** True when a brace initializer was explicitly provided (even if empty: new T[]{}). */
    bool _has_brace_init = false;
    /** Resolved array size (set during type resolution, only valid when !_is_dynamic_size). */
    size_t _array_size = 0;
    /** Per-element constructor (for struct element types). Index matches _array_init_elements. */
    std::vector<std::shared_ptr<constructor>> _element_constructors;

    /** True when this is a uniform array init: new T(args)[N] — all elements same ctor. */
    bool _is_uniform_array = false;
    /** Constructor arguments for uniform array init (applied to every element). */
    std::vector<std::shared_ptr<expression>> _uniform_ctor_args;
    /** Resolved constructor for uniform array init (struct element types). */
    std::shared_ptr<constructor> _uniform_constructor;

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

    // --- Array accessors ---

    bool is_array() const { return _is_array; }

    /** True when the array size is a runtime expression (dynamic allocation). */
    bool is_dynamic_size() const { return _is_dynamic_size; }

    const std::shared_ptr<expression>& array_size_expr() const { return _array_size_expr; }

    /** True when a brace initializer was explicitly provided (even if empty). */
    bool has_brace_init() const { return _has_brace_init; }

    const std::vector<std::shared_ptr<expression>>& array_init_elements() const { return _array_init_elements; }

    void set_array_init_elements(const std::vector<std::shared_ptr<expression>>& elems) {
        _array_init_elements = elems;
        for (auto& e : _array_init_elements) {
            if (e) e->set_parent_expression(shared_as<expression>());
        }
    }

    void assign_array_init_element(size_t index, const std::shared_ptr<expression>& elem) {
        _array_init_elements[index] = elem;
        if (elem) elem->set_parent_expression(shared_as<expression>());
    }

    size_t array_size() const { return _array_size; }

    const std::vector<std::shared_ptr<constructor>>& element_constructors() const { return _element_constructors; }

    // --- Factory methods ---

    static std::shared_ptr<new_expression> make_shared(
        const std::shared_ptr<type>& allocated_type,
        const std::vector<std::shared_ptr<expression>>& args);

    /** Create a new-array expression. */
    static std::shared_ptr<new_expression> make_array_shared(
        const std::shared_ptr<type>& element_type,
        const std::shared_ptr<expression>& array_size_expr,
        const std::vector<std::shared_ptr<expression>>& init_elements,
        bool has_brace_init);

    /** Create a new-uniform-array expression: new T(args)[size]. */
    static std::shared_ptr<new_expression> make_uniform_array_shared(
        const std::shared_ptr<type>& element_type,
        const std::shared_ptr<expression>& array_size_expr,
        const std::vector<std::shared_ptr<expression>>& uniform_ctor_args);

    // --- Uniform array accessors ---

    bool is_uniform_array() const { return _is_uniform_array; }

    const std::vector<std::shared_ptr<expression>>& uniform_ctor_args() const { return _uniform_ctor_args; }

    void set_uniform_ctor_args(const std::vector<std::shared_ptr<expression>>& args) {
        _uniform_ctor_args = args;
        for (auto& a : _uniform_ctor_args) if (a) a->set_parent_expression(shared_as<expression>());
    }

    void assign_uniform_ctor_arg(size_t index, const std::shared_ptr<expression>& arg) {
        _uniform_ctor_args[index] = arg;
        if (arg) arg->set_parent_expression(shared_as<expression>());
    }

    std::shared_ptr<constructor> uniform_constructor() const { return _uniform_constructor; }

    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<new_expression> c{new new_expression()};
        c->_type = _type;
        c->_allocated_type = _allocated_type;
        c->_constructor = _constructor;
        c->_is_array = _is_array;
        c->_is_dynamic_size = _is_dynamic_size;
        c->_array_size = _array_size;
        c->_has_brace_init = _has_brace_init;
        c->_is_uniform_array = _is_uniform_array;
        c->_uniform_constructor = _uniform_constructor;
        if (!_is_array) {
            std::vector<std::shared_ptr<expression>> args;
            for (auto& a : _arguments) args.push_back(a->clone());
            c->assign_arguments(args);
        } else if (_is_uniform_array) {
            if (_array_size_expr) {
                c->_array_size_expr = _array_size_expr->clone();
                c->_array_size_expr->set_parent_expression(c);
            }
            std::vector<std::shared_ptr<expression>> uargs;
            for (auto& a : _uniform_ctor_args) uargs.push_back(a ? a->clone() : nullptr);
            c->set_uniform_ctor_args(uargs);
        } else {
            if (_array_size_expr) {
                c->_array_size_expr = _array_size_expr->clone();
                c->_array_size_expr->set_parent_expression(c);
            }
            std::vector<std::shared_ptr<expression>> elems;
            for (auto& e : _array_init_elements) elems.push_back(e ? e->clone() : nullptr);
            c->set_array_init_elements(elems);
            c->_element_constructors = _element_constructors;
        }
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

} // namespace k::model
#endif //KLANG_MODEL_EXPRESSIONS_INVOCATION_HPP
