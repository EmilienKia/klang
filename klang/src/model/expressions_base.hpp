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
 * +- temporary_construction_expression
 * +- designated_struct_init_expression
 */

#ifndef KLANG_MODEL_EXPRESSIONS_BASE_HPP
#define KLANG_MODEL_EXPRESSIONS_BASE_HPP

#include "model.hpp"

namespace k::parse::ast {
struct expression;
struct unary_expression;
struct template_arg;
using template_arg_list = std::vector<std::shared_ptr<template_arg>>;
}

namespace k::model {

namespace gen {
class symbol_resolver;
}

class template_instantiator;


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
    // Copy constructor: copies type and AST node, parent is NOT copied (clone is orphan).
    expression(const expression& other) : _type(other._type) { _ast_node = other._ast_node; }

    friend class unary_expression;
    friend class binary_expression;
    friend class member_of_expression;
    friend class function_invocation_expression;
    friend class constructor_invocation_expression;
    friend class new_expression;
    friend class delete_expression;
    friend class array_init_expression;
    friend class designated_struct_init_expression;
    friend class temporary_construction_expression;

    void set_parent_expression(const std::shared_ptr<expression> &expression) {
        set_parent(expression);
    }

    friend class gen::symbol_resolver;
    friend class gen::type_reference_resolver;
    friend class template_instantiator;

    void set_type(std::shared_ptr<type> type);

public:
    void accept(model_visitor &visitor) override;

    std::shared_ptr<type> get_type() { return _type; }
    std::shared_ptr<const type> get_type() const { return _type; }

    /** Set the AST expression node associated with this model expression. */
    void set_ast_expression(std::shared_ptr<k::parse::ast::expression> ast) {
        _ast_node = std::static_pointer_cast<k::parse::ast::ast_node>(std::move(ast));
    }

    /** Get the AST expression node (typed) associated with this model expression (may be null). */
    std::shared_ptr<k::parse::ast::expression> get_ast_expression() const {
        return get_ast_node_as<k::parse::ast::expression>();
    }

    std::shared_ptr<statement> find_statement();
    std::shared_ptr<const statement> find_statement() const;

    std::shared_ptr<expression> get_parent_expression() { return parent<expression>(); };
    std::shared_ptr<const expression> get_parent_expression() const { return parent<expression>(); };

    /** Return a deep copy of this expression, without parent (orphan). */
    virtual std::shared_ptr<expression> clone() const = 0;

    /**
     * Recursively retrieve the first (leftmost) lexeme covered by this expression's AST subtree.
     * Returns std::nullopt if no AST node is attached or no lexeme can be found.
     */
    virtual std::optional<k::lex::any_lexeme> first_lexeme() const;

    /**
     * Recursively retrieve the last (rightmost) lexeme covered by this expression's AST subtree.
     * Returns std::nullopt if no AST node is attached or no lexeme can be found.
     */
    virtual std::optional<k::lex::any_lexeme> last_lexeme() const;

    /**
     * Convenience: returns the source range (first, last) lexemes of this expression.
     * Either or both may be std::nullopt if not available.
     */
    std::pair<std::optional<k::lex::any_lexeme>, std::optional<k::lex::any_lexeme>> source_range() const {
        return { first_lexeme(), last_lexeme() };
    }
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

    /**
     * Force the encoding of a held (string or character) literal. Used by the
     * type resolver to apply context-driven element-type selection to an
     * unprefixed string literal (e.g. when it initialises an `unsigned byte[]`
     * variable or is passed to an `unsigned short[]` parameter).
     */
    void set_literal_encoding(k::lex::literal_encoding enc);

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
public:
    /** Resolved target for enum entries: holds the enumeration and the entry index. */
    struct enum_entry_target {
        std::shared_ptr<enumeration> enum_def;
        size_t entry_index; // index into enumeration->entries()
    };

    /** Resolved target for annotation type RTTI descriptors (e.g. MyAnnotation::annotation). */
    struct annotation_type_rtti_target {
        std::shared_ptr<aggregate> ann_type;
    };

protected:
    // Name of the symbol when not resolved.
    name _name;

    std::variant<
            std::monostate, // Not resolved
            std::shared_ptr<variable_definition>,
            std::shared_ptr<function>,
            enum_entry_target,
            annotation_type_rtti_target
    > _target;

    /**
     * Optional AST template arguments carried from the parser.
     * Set when the user writes func<type_args>(...) — the resolver will
     * use these to instantiate a template function before resolving the call.
     */
    k::parse::ast::template_arg_list _ast_template_args;

    /** True when '<>' or '<args>' was explicitly written (even if args is empty). */
    bool _has_explicit_template_args = false;

    /**
     * True when template args qualify the leading type in a qualified symbol
     * (e.g. Type<T>::method), rather than the terminal callable symbol.
     */
    bool _template_args_on_qualifier = false;

    symbol_expression(const name &name);

    symbol_expression(const std::shared_ptr<variable_definition> &var);

    symbol_expression(const std::shared_ptr<function> &func);

    // Copy constructor
    symbol_expression(const symbol_expression& other)
        : expression(other), _name(other._name), _target(other._target),
          _ast_template_args(other._ast_template_args),
          _has_explicit_template_args(other._has_explicit_template_args),
          _template_args_on_qualifier(other._template_args_on_qualifier) {}

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

    bool is_enum_entry() const {
        return std::holds_alternative<enum_entry_target>(_target);
    }

    const enum_entry_target& get_enum_entry() const {
        return std::get<enum_entry_target>(_target);
    }

    bool is_annotation_type_rtti() const {
        return std::holds_alternative<annotation_type_rtti_target>(_target);
    }

    const annotation_type_rtti_target& get_annotation_type_rtti() const {
        return std::get<annotation_type_rtti_target>(_target);
    }

    bool is_resolved() const {
        return _target.index() != 0;
    }

    void set_target(std::shared_ptr<variable_definition> var);

    void set_target(std::shared_ptr<function> func);

    void set_target(enum_entry_target target) { _target = std::move(target); }

    void set_target(annotation_type_rtti_target target) { _target = std::move(target); }

    /** True if this symbol carries explicit template arguments (from func<args>() syntax). */
    bool has_ast_template_args() const { return _has_explicit_template_args || !_ast_template_args.empty(); }

    /** Returns the AST template arguments. */
    const k::parse::ast::template_arg_list& get_ast_template_args() const { return _ast_template_args; }

    /** Set the AST template arguments. */
    void set_ast_template_args(k::parse::ast::template_arg_list args) {
        _ast_template_args = std::move(args);
        _has_explicit_template_args = true;
    }

    /** True when explicit template args apply to the leading qualifier (Type<T>::member). */
    bool has_qualifier_template_args() const {
        return has_ast_template_args() && _template_args_on_qualifier;
    }

    /** Mark whether explicit template args belong to the leading qualifier. */
    void set_template_args_on_qualifier(bool v = true) { _template_args_on_qualifier = v; }

    /** Mark that explicit template args were provided (even if list is empty). */
    void set_has_explicit_template_args(bool v = true) { _has_explicit_template_args = v; }

    std::shared_ptr<expression> clone() const override {
        auto c = std::shared_ptr<symbol_expression>(new symbol_expression(*this));
        c->_ast_template_args = _ast_template_args;
        c->_has_explicit_template_args = _has_explicit_template_args;
        c->_template_args_on_qualifier = _template_args_on_qualifier;
        return c;
    }
};

/**
 * Pack expansion expression — represents `expr...` in a function call.
 * During template instantiation, this is expanded into multiple arguments.
 * After instantiation, no pack_expansion_expression should remain in the model.
 */
class pack_expansion_expression : public expression {
protected:
    /** The inner expression being expanded (typically a symbol referencing a pack param). */
    std::shared_ptr<expression> _inner;

    /** The name of the pack parameter being expanded. */
    std::string _pack_name;

public:
    pack_expansion_expression(std::shared_ptr<expression> inner, std::string pack_name)
        : _inner(std::move(inner)), _pack_name(std::move(pack_name)) {}

    const std::shared_ptr<expression>& inner() const { return _inner; }
    const std::string& pack_name() const { return _pack_name; }

    void accept(model_visitor& visitor) override;

    std::shared_ptr<expression> clone() const override {
        auto cloned_inner = _inner ? _inner->clone() : nullptr;
        return std::make_shared<pack_expansion_expression>(cloned_inner, _pack_name);
    }
};

} // namespace k::model
#endif //KLANG_MODEL_EXPRESSIONS_BASE_HPP
