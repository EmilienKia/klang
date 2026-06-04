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

#ifndef KLANG_MODEL_EXPRESSIONS_INIT_HPP
#define KLANG_MODEL_EXPRESSIONS_INIT_HPP
#include "expressions_invocation.hpp"

namespace k::model {

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

    /** Temporary mode: true when used as expression-level temporary array construction. */
    bool _is_temporary = false;
    /** Temporary mode unresolved element type name (e.g. "int" in int[]{...}). */
    k::name _temporary_type_name;

    /** Per-element initializer expressions. nullptr = default-init. */
    std::vector<std::shared_ptr<expression>> _elements;

    /** True when this is a uniform array init: var : T(args)[N]; */
    bool _is_uniform = false;
    /** Constructor arguments for uniform array init (applied to every element). */
    std::vector<std::shared_ptr<expression>> _uniform_ctor_args;
    /** Resolved constructor for uniform array init (struct element types). */
    std::shared_ptr<constructor> _uniform_constructor;
    /** Resolved array size (set during type resolution for uniform mode). */
    size_t _array_size = 0;

    array_init_expression() = default;
    array_init_expression(const array_init_expression&) = delete;

    friend class gen::type_reference_resolver;
    friend class gen::implementation_generator;

public:
    const std::shared_ptr<symbol_expression>& constructed_symbol() const { return _constructed_symbol; }
    bool is_temporary() const { return _is_temporary; }
    const k::name& temporary_type_name() const { return _temporary_type_name; }

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

    // --- Uniform mode accessors ---

    bool is_uniform() const { return _is_uniform; }

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

    size_t array_size() const { return _array_size; }

    // --- Factory methods ---

    static std::shared_ptr<array_init_expression> make_shared(
        const std::shared_ptr<symbol_expression>& constructed_symbol,
        const std::vector<std::shared_ptr<expression>>& elements);

    static std::shared_ptr<array_init_expression> make_shared(
        const std::shared_ptr<variable_definition>& variable,
        const std::vector<std::shared_ptr<expression>>& elements);

    /** Create a temporary array-init expression: T[]{...}. */
    static std::shared_ptr<array_init_expression> make_temporary_shared(
        const k::name& temporary_type_name,
        const std::vector<std::shared_ptr<expression>>& elements);

    /** Create a uniform array init expression: var : T(args)[N]; */
    static std::shared_ptr<array_init_expression> make_uniform_shared(
        const std::shared_ptr<symbol_expression>& constructed_symbol,
        const std::vector<std::shared_ptr<expression>>& uniform_ctor_args,
        size_t array_size);

    /** Create a uniform array init expression from a variable definition. */
    static std::shared_ptr<array_init_expression> make_uniform_shared(
        const std::shared_ptr<variable_definition>& variable,
        const std::vector<std::shared_ptr<expression>>& uniform_ctor_args,
        size_t array_size);

    void accept(model_visitor& visitor) override;

    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<array_init_expression> c{new array_init_expression()};
        c->_type = _type;
        c->_is_temporary = _is_temporary;
        c->_temporary_type_name = _temporary_type_name;
        c->_is_uniform = _is_uniform;
        c->_uniform_constructor = _uniform_constructor;
        c->_array_size = _array_size;
        auto sym = _constructed_symbol
            ? std::dynamic_pointer_cast<symbol_expression>(_constructed_symbol->clone())
            : nullptr;
        if (sym) {
            c->_constructed_symbol = sym;
            sym->set_parent_expression(c);
        }
        if (_is_uniform) {
            std::vector<std::shared_ptr<expression>> uargs;
            for (auto& a : _uniform_ctor_args) uargs.push_back(a ? a->clone() : nullptr);
            c->set_uniform_ctor_args(uargs);
        } else {
            std::vector<std::shared_ptr<expression>> elems;
            for (auto& e : _elements) elems.push_back(e ? e->clone() : nullptr);
            c->set_elements(elems);
        }
        return c;
    }
};

/**
 * Designated struct initializer expression.
 * Represents brace-init with named member designators: s : S { .x = 1, .y(2, 3) };
 *
 * Each member initializer is stored as a (name, expression) pair.
 * Members not listed are default-initialized.
 * This expression is only valid for struct types (no virtual inheritance).
 *
 * If a member name is qualified (e.g. ".Base::x"), the qualifier is stored
 * alongside the member name for disambiguation of inherited members.
 */
class designated_struct_init_expression : public expression {
public:
    /**
     * A single member initializer entry.
     * Either assignment form (.member = expr) or constructor form (.member(args...)).
     */
    struct member_init_entry {
        std::string member_name;                       ///< Simple member name (e.g. "x")
        std::string qualifier;                         ///< Optional qualifier for disambiguation (e.g. "BaseA")
        bool is_call_form = false;                     ///< true → .m(args), false → .m = expr
        std::shared_ptr<expression> value;             ///< Assignment form: the value expression
        std::vector<std::shared_ptr<expression>> args; ///< Constructor form: argument expressions
        /** Resolved member variable definition (set during type resolution). */
        std::shared_ptr<member_variable_definition> resolved_member;
        /** Resolved owning aggregate for this member (set during type resolution).
         *  Differs from target_aggregate when the member is inherited from a base. */
        std::shared_ptr<aggregate> resolved_owner;
        /** Resolved constructor for this member's type (set during type resolution, if applicable). */
        std::shared_ptr<constructor> resolved_constructor;
    };

protected:
    /** The variable being initialized (null for temporaries and nested inits). */
    std::shared_ptr<symbol_expression> _constructed_symbol;

    /** The target struct type. */
    std::shared_ptr<aggregate> _target_aggregate;

    /** Member initializers in declaration order. */
    std::vector<member_init_entry> _members;

    /** True when this designated init is used as a temporary expression (no variable). */
    bool _is_temporary = false;

    /** Type name for temporary mode (resolved during type resolution). */
    std::string _type_name;

    designated_struct_init_expression() = default;
    designated_struct_init_expression(const designated_struct_init_expression&) = delete;

    friend class gen::type_reference_resolver;
    friend class gen::implementation_generator;

public:
    const std::shared_ptr<symbol_expression>& constructed_symbol() const { return _constructed_symbol; }

    std::shared_ptr<aggregate> target_aggregate() const { return _target_aggregate; }

    const std::vector<member_init_entry>& members() const { return _members; }
    std::vector<member_init_entry>& members_mutable() { return _members; }

    size_t size() const { return _members.size(); }

    bool is_temporary() const { return _is_temporary; }
    const std::string& type_name() const { return _type_name; }

    static std::shared_ptr<designated_struct_init_expression> make_shared(
        const std::shared_ptr<symbol_expression>& constructed_symbol,
        const std::shared_ptr<aggregate>& target_aggregate,
        const std::vector<member_init_entry>& members);

    static std::shared_ptr<designated_struct_init_expression> make_shared(
        const std::shared_ptr<variable_definition>& variable,
        const std::shared_ptr<aggregate>& target_aggregate,
        const std::vector<member_init_entry>& members);

    /** Create a temporary designated init expression (no variable). */
    static std::shared_ptr<designated_struct_init_expression> make_temporary_shared(
        const std::string& type_name,
        const std::vector<member_init_entry>& members);

    void accept(model_visitor& visitor) override;

    std::shared_ptr<expression> clone() const override {
        std::shared_ptr<designated_struct_init_expression> c{new designated_struct_init_expression()};
        c->_type = _type;
        c->_target_aggregate = _target_aggregate;
        c->_is_temporary = _is_temporary;
        c->_type_name = _type_name;
        auto sym = _constructed_symbol
            ? std::dynamic_pointer_cast<symbol_expression>(_constructed_symbol->clone())
            : nullptr;
        if (sym) {
            c->_constructed_symbol = sym;
            sym->set_parent_expression(c);
        }
        for (auto& m : _members) {
            member_init_entry entry;
            entry.member_name = m.member_name;
            entry.qualifier = m.qualifier;
            entry.is_call_form = m.is_call_form;
            entry.resolved_member = m.resolved_member;
            entry.resolved_owner = m.resolved_owner;
            entry.resolved_constructor = m.resolved_constructor;
            if (m.value) {
                entry.value = m.value->clone();
                entry.value->set_parent_expression(c);
            }
            for (auto& a : m.args) {
                auto ac = a ? a->clone() : nullptr;
                if (ac) ac->set_parent_expression(c);
                entry.args.push_back(ac);
            }
            c->_members.push_back(std::move(entry));
        }
        return c;
    }
};

} // namespace k::model
#endif //KLANG_MODEL_EXPRESSIONS_INIT_HPP
