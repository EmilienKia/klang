/*
 * K Language compiler
 *
 * Copyright 2023 Emilien Kia
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

#ifndef KLANG_MODEL_HPP
#define KLANG_MODEL_HPP

#include <map>
#include <memory>
#include <string>
#include <utility>

#include "../lex/lexer.hpp"
#include "../parse/ast.hpp"
#include "../parse/parser.hpp"
#include "../common/common.hpp"
#include "type.hpp"


namespace k::model {

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


class model_visitor;

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

    virtual void accept(model_visitor& visitor) =0;

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

    void accept(model_visitor& visitor) override;

    /**
     * Retrieve the model this element is declared in.
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
    void accept(model_visitor& visitor) override;

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
    void accept(model_visitor& visitor) override;

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

    void accept(model_visitor& visitor) override;

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

    void accept(model_visitor& visitor) override;

    /**
     * Get the model name.
     * @return Unit name identifier
     */
    name get_unit_name() const {
        return _unit_name;
    }

    /**
     * Set the model name
     * @param unit_name New model name
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
     * Retrieve the root namespace of this model.
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



} // namespace k::model
#endif //KLANG_MODEL_HPP
