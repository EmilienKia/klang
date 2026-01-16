/*
 * K Language compiler
 *
 * Copyright 2023-2024 Emilien Kia
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


#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>


namespace k::model {

class context;

class expression;
class statement;
class variable_statement;
class block;

class parameter;
class function;
class structure;
class ns;
class unit;


namespace gen {
class type_reference_resolver;
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
    std::shared_ptr<element> _parent = nullptr;

    element(std::shared_ptr<element> parent = nullptr) : _parent(parent) {}

    friend statement;
    void set_parent(const std::shared_ptr<element> &parent_element) {
        _parent = parent_element;
    }

    static void set_parent(const std::shared_ptr<element> &parent, const std::shared_ptr<element> &child) {
        if(child && parent) {
            child->set_parent(parent);
        }
    }

public:
    virtual ~element() = default;

    std::shared_ptr<context> get_context();

    template<typename T>
    inline std::shared_ptr<T> shared_as() {
        return std::dynamic_pointer_cast<T>(shared_from_this());
    }

    template<typename T>
    inline std::shared_ptr<const T> shared_as() const {
        return std::dynamic_pointer_cast<T>(shared_from_this());
    }

    template<typename T>
    inline std::shared_ptr<T> parent() {
        return std::dynamic_pointer_cast<T>(_parent);
    }

    template<typename T>
    inline std::shared_ptr<const T> parent() const {
        return std::dynamic_pointer_cast<T>(_parent);
    }

    template<typename T>
    inline std::shared_ptr<T> ancestor() {
        std::shared_ptr<element> current = _parent;
        while(current) {
            if(auto ancestor = std::dynamic_pointer_cast<T>(current)) {
                return ancestor;
            }
            current = current->_parent;
        }
        return {};
    }

    template<typename T>
    inline std::shared_ptr<const T> ancestor() const {
        std::shared_ptr<const element> current = _parent;
        while(current) {
            if(auto ancestor = std::dynamic_pointer_cast<const T>(current)) {
                return ancestor;
            }
            current = current->_parent;
        }
        return {};
    }

    virtual void accept(model_visitor& visitor) =0;

};


template<>
inline std::shared_ptr<element> element::parent<element>() {
    return _parent;
}

template<>
inline std::shared_ptr<const element> element::parent<element>() const {
    return _parent;
}


class named_element
{
protected:
    name _name;
    std::string _short_name;
    std::string _fq_name;
    std::string _mangled_name;

    virtual void update_names();
    virtual void update_mangled_name() = 0;

public:
    named_element() = default;
    named_element(const named_element&) = default;
    named_element(named_element&&) = default;

    void assign_name(const std::string& name) {
        _name = name;
        update_names();
    }

    void assign_name(const name& name) {
        _name = name;
        update_names();
    }

    named_element& operator=(const std::string& name) {
        assign_name(name);
        return *this;
    }

    named_element& operator=(const name& name) {
        assign_name(name);
        return *this;
    }

    const name& get_name() const {
        return _name;
    }

    const std::string& get_short_name() const {
        return _short_name;
    }

    const std::string& get_fq_name() const {
        return _fq_name;
    }

    const std::string& get_mangled_name() const {
        return _mangled_name;
    }
};

/**
 * Interface for variables
 */
class variable_definition : public named_element
{
protected:
    /** Type of the variable */
    std::shared_ptr<type> _type;

    /** Optional initialization statement. */
    std::shared_ptr<expression> _expression;

    variable_definition() = default;
    variable_definition(const variable_definition&) = default;
    variable_definition(variable_definition&&) = default;

public:
    virtual void init(const std::string &name, const std::shared_ptr<type> &type = nullptr);

    virtual std::shared_ptr<type> get_type() const;
    virtual std::shared_ptr<expression> get_init_expr() const;
    virtual variable_definition& set_type(std::shared_ptr<type> type);
    virtual variable_definition& set_init_expr(std::shared_ptr<expression> init_expr);
};



/**
* Interface for holding variables (like ns, structs and blocks)
*/
class variable_holder
{
public:
    virtual std::shared_ptr<variable_definition> append_variable(const std::string& name);
    virtual std::shared_ptr<variable_definition> get_variable(const std::string& name) const;
    virtual std::shared_ptr<variable_definition> lookup_variable(const std::string& name) const;

    typedef std::map<std::string, std::shared_ptr<variable_definition>> variable_map_t;
        
protected:
    /** Map of all defined vars. */
    variable_map_t _vars;

    virtual std::shared_ptr<variable_definition> do_create_variable(const std::string &name) =0;
    virtual void on_variable_defined(std::shared_ptr<variable_definition>) =0;

public:
    const variable_map_t& variables() const {return _vars;}
    variable_map_t::const_iterator variable_begin() const;
    variable_map_t::const_iterator variable_end() const;
};


/**
* Interface for holding functions (like ns and structs)
*/
class function_holder
{
public:
    virtual std::shared_ptr<function> define_function(const std::string& name);
    virtual std::shared_ptr<function> get_function(const std::string& name) const;
    virtual std::shared_ptr<function> lookup_function(const std::string& name) const;

    std::vector<std::shared_ptr<function>> functions() {return _functions;}

protected:
    /** List of all defined functions. */
    std::vector<std::shared_ptr<function>> _functions;

    virtual std::shared_ptr<function> do_create_function(const std::string &name) =0;
    virtual void on_function_defined(std::shared_ptr<function>) =0;
};

/**
* Interface for holding structures (like ns and structs)
*/
class structure_holder
{
public:
    virtual std::shared_ptr<structure> define_structure(const std::string& name);
    virtual std::shared_ptr<structure> get_structure(const std::string& name);
    virtual std::shared_ptr<structure> lookup_structure(const std::string& name);

protected:
    /** Map of all defined structures. */
    std::map<std::string, std::shared_ptr<structure>> _structs;

    virtual std::shared_ptr<structure> do_create_structure(const std::string &name) =0;
    virtual void on_structure_defined(std::shared_ptr<structure>) =0;
};


class member_variable_definition : public element, public variable_definition {
protected:

    friend class structure;
    friend class gen::unit_llvm_ir_gen;

    member_variable_definition(std::shared_ptr<structure> st);

    static std::shared_ptr<member_variable_definition> make_shared(std::shared_ptr<structure> st);
    static std::shared_ptr<member_variable_definition> make_shared(std::shared_ptr<structure> st, const std::string &name);

    void update_mangled_name() override;

public:
    void accept(model_visitor& visitor) override;

};


class structure : public element, public named_element, public variable_holder, public function_holder {
protected:
    friend class ns;
    friend class gen::unit_llvm_ir_gen;
    friend class gen::symbol_resolver;

    /** Collection of all children of this namespace. */
    std::vector<std::shared_ptr<element>> _children;

    std::shared_ptr<struct_type> _type;

    structure(std::shared_ptr<element> parent) :
        element(parent) {}

    static std::shared_ptr<structure> make_shared(std::shared_ptr<element> parent, const std::string &name);

    std::shared_ptr<variable_definition> do_create_variable(const std::string &name) override;
    void on_variable_defined(std::shared_ptr<variable_definition>) override;

    std::shared_ptr<function> do_create_function(const std::string &name) override;
    void on_function_defined(std::shared_ptr<function>) override;

    void set_struct_type(const std::shared_ptr<struct_type>& st_type) {
        _type = st_type;
    }

    void update_mangled_name() override;

public:

    void accept(model_visitor& visitor) override;

    std::shared_ptr<struct_type> get_struct_type() const {
        return _type;
    }

    //
    // Children functions
    //

    const std::vector<std::shared_ptr<element>>& get_children() const {
        return _children;
    }

    std::shared_ptr<function> lookup_function(const std::string& name) const override;

    std::shared_ptr<variable_definition> lookup_variable(const std::string& name) const override;
};

class parameter : public element, public variable_definition {
protected:

    friend class function;
    friend class gen::unit_llvm_ir_gen;

    std::shared_ptr<function> _function;

    size_t _pos;

    parameter(std::shared_ptr<function> func, size_t pos);

    static std::shared_ptr<parameter> make_shared(std::shared_ptr<function> func, size_t pos);
    static std::shared_ptr<parameter> make_shared(std::shared_ptr<function> func, const std::string &name, size_t pos);
    static std::shared_ptr<parameter> make_shared(std::shared_ptr<function> func, const std::string &name, const std::shared_ptr<type> &type, size_t pos);

    void update_mangled_name() override;

public:
    void accept(model_visitor& visitor) override;

    size_t get_pos() const {
        return _pos;
    }

    std::shared_ptr<function> get_function() {return _function;}
    std::shared_ptr<const function> get_function() const {return _function;}
};

class function : public element, public named_element, public variable_holder {
protected:
    friend class ns;
    friend class structure;
    friend class gen::unit_llvm_ir_gen;
    friend class gen::symbol_resolver;
    friend class gen::type_reference_resolver;

    std::shared_ptr<type> _return_type;
    std::vector<std::shared_ptr<parameter>> _parameters;
    std::shared_ptr<parameter> _this_param;
    std::shared_ptr<block> _block;

    function(std::shared_ptr<element> parent) :
        element(parent) {}

    static std::shared_ptr<function> make_shared(std::shared_ptr<element> parent, const std::string& name);

    std::shared_ptr<variable_definition> do_create_variable(const std::string &name) override;
    void on_variable_defined(std::shared_ptr<variable_definition>) override;

    void update_mangled_name() override;

    void create_this_parameter();

public:
    void accept(model_visitor& visitor) override;

    void set_return_type(std::shared_ptr<type> return_type);
    std::shared_ptr<type> get_return_type() {return _return_type;}
    std::shared_ptr<const type> get_return_type() const {return _return_type;}

    const std::vector<std::shared_ptr<parameter>>& parameters() const {
        return _parameters;
    }

    std::shared_ptr<variable_definition> append_variable(const std::string& name) override;

    std::shared_ptr<parameter> append_parameter(const std::string& name, std::shared_ptr<type> type);
    std::shared_ptr<parameter> insert_parameter(const std::string& name, std::shared_ptr<type> type, size_t pos);

    size_t get_parameter_size() const {return _parameters.size();}
    std::shared_ptr<parameter> get_parameter(size_t index);
    std::shared_ptr<const parameter> get_parameter(size_t index)const;

    std::shared_ptr<parameter> get_parameter(const std::string& name);
    std::shared_ptr<const parameter> get_parameter(const std::string& name)const;

    std::shared_ptr<parameter> get_this_parameter() const {
        return _this_param;
    }

    void set_block(const std::shared_ptr<block>& block);
    std::shared_ptr<block> get_block();

    bool is_member() const;
    std::shared_ptr<const structure> get_owner() const;
    std::shared_ptr<structure> get_owner();
};


class global_variable_definition : public element, public variable_definition {
protected:

    friend class ns;
    friend class gen::unit_llvm_ir_gen;

    global_variable_definition(std::shared_ptr<ns> ns);

    static std::shared_ptr<global_variable_definition> make_shared(std::shared_ptr<ns> ns);
    static std::shared_ptr<global_variable_definition> make_shared(std::shared_ptr<ns> ns, const std::string& name);

    void update_mangled_name() override;

public:
    void accept(model_visitor& visitor) override;

};


class ns : public element, public named_element, public variable_holder, public function_holder, public structure_holder {
protected:

    friend class unit;

    /** Collection of all children of this namespace. */
    std::vector<std::shared_ptr</*ns_element*/element>> _children;

    /** Map of direct child namespaces. */
    std::map<std::string, std::shared_ptr<ns>> _ns;

    /** Map of all structures defined in this namespace. */
    std::map<std::string, std::shared_ptr<structure>> _structs;

    ns(std::shared_ptr<element> parent):
        element(parent) {}

    static std::shared_ptr<ns> make_shared(std::shared_ptr<element> parent, const std::string& name);

    std::shared_ptr<variable_definition> do_create_variable(const std::string &name) override;
    void on_variable_defined(std::shared_ptr<variable_definition>) override;

    std::shared_ptr<function> do_create_function(const std::string &name) override;
    void on_function_defined(std::shared_ptr<function> func) override;

    std::shared_ptr<structure> do_create_structure(const std::string &name) override;
    void on_structure_defined(std::shared_ptr<structure>) override;

    void update_mangled_name() override;
public:

    void accept(model_visitor& visitor) override;

    //
    // This namespace manipulations
    //

    /**
     * Test if this namespace is the root namespace.
     * @return True if root namespace, false otherwise.
     */
    bool is_root() const { return !!parent<unit>(); }

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

    const std::vector<std::shared_ptr</*ns_element*/element>>& get_children() const {
        return _children;
    }

    std::shared_ptr<function> lookup_function(const std::string& name) const override;

    std::shared_ptr<variable_definition> lookup_variable(const std::string& name) const override;

    std::shared_ptr<structure> lookup_structure(const std::string& name) override;
};



class unit : public element {
protected:
    friend class element;
    /** Analysis context */
    std::shared_ptr<context> _context;

    /** Unit name */
    name _unit_name;

    /** Root namespace.*/
    std::shared_ptr<ns> _root_ns;

    unit() = delete;
    unit(std::shared_ptr<context> context);
public:

    static std::shared_ptr<unit> create(std::shared_ptr<context> context);

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
    void set_unit_name(const name& unit_name);

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
    std::shared_ptr<ns> get_root_namespace();
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
