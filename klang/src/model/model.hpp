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
class constructor_invocation_expression;
class global_variable_definition;

class context;

class expression;
class statement;
class variable_statement;
class block;

class parameter;
class function;
class constructor;
class destructor;
class static_constructor;
class static_destructor;
class structure;
class ns;
class unit;

class global_tool_function;
class global_constructor_function;
class global_destructor_function;


namespace gen {
class type_reference_resolver;
class declaration_generator;
class implementation_generator;
class init_order_resolver;
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

    friend class statement;
    friend class variable_definition;
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

    /** Optional initialization statement */
    std::shared_ptr<constructor_invocation_expression> _init_expr;

    // Not sure useful here :
    // Real constructor is already stored in the init expression, but we need to store it here for the case of a variable definition without initialization (like "var x: MyStruct;")
    // If no init expr, only default constructor should be stored here.
    std::shared_ptr<constructor> _var_constructor;

    friend class k::model::gen::type_reference_resolver;
    void set_var_constructor(const std::shared_ptr<constructor>& var_constructor) { _var_constructor = var_constructor; }

    variable_definition() = default;
    variable_definition(const variable_definition&) = default;
    variable_definition(variable_definition&&) = default;

public:
    virtual void init(const std::string &name, const std::shared_ptr<type> &type = nullptr);

    virtual std::shared_ptr<type> get_type() const;
    virtual variable_definition& set_type(std::shared_ptr<type> type);

    virtual std::shared_ptr<constructor_invocation_expression> get_init_expr() const;
    virtual variable_definition& set_init_expr(std::shared_ptr<constructor_invocation_expression> init_expr);
};



/**
* Interface for holding variables (like ns, structs and blocks)
*/
class variable_holder
{
public:
    virtual std::shared_ptr<variable_definition> append_variable(const std::string& name, bool is_static = false);
    virtual std::shared_ptr<variable_definition> get_variable(const std::string& name) const;

    typedef std::map<std::string, std::shared_ptr<variable_definition>> variable_map_t;
        
protected:
    /** Map of all defined vars. */
    variable_map_t _vars;

    virtual std::shared_ptr<variable_definition> do_create_variable(const std::string &name, bool is_static) =0;
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
    virtual std::shared_ptr<function> define_function(const std::string& name, bool is_static);
    /** Return the first function matching name (legacy, single-overload). */
    virtual std::shared_ptr<function> get_function(const std::string& name) const;
    /** Return ALL functions matching name (for overload resolution). */
    virtual std::vector<std::shared_ptr<function>> get_functions(const std::string& name) const;

    std::vector<std::shared_ptr<function>> functions() {return _functions;}

protected:
    /** List of all defined functions. */
    std::vector<std::shared_ptr<function>> _functions;

    virtual std::shared_ptr<function> do_create_function(const std::string &name, bool is_static) =0;
    virtual void on_function_defined(std::shared_ptr<function>) =0;
};

/**
* Interface for holding structures (like ns and structs)
*/
class structure_holder
{
public:
    virtual std::shared_ptr<structure> define_structure(const std::string& name);
    virtual std::shared_ptr<structure> get_structure(const std::string& name) const;

protected:
    /** Map of all defined structures. */
    std::map<std::string, std::shared_ptr<structure>> _structs;

    virtual std::shared_ptr<structure> do_create_structure(const std::string &name) =0;
    virtual void on_structure_defined(std::shared_ptr<structure>) =0;
};


class member_variable_definition : public element, public variable_definition {
protected:

    friend class structure;
    friend class gen::implementation_generator;
    friend class gen::symbol_resolver;

    /** Declared visibility of this member variable. PUBLIC by default. */
    visibility _visibility = PUBLIC;

    member_variable_definition(std::shared_ptr<structure> st);

    static std::shared_ptr<member_variable_definition> make_shared(std::shared_ptr<structure> st, const std::string &name);

    void update_mangled_name() override;

public:
    void accept(model_visitor& visitor) override;

    visibility get_visibility() const { return _visibility; }
    void set_visibility(visibility v) { _visibility = v; }
};

/**
 * Specifies a single base class in an inheritance clause.
 * E.g. "struct D : public B1, private B2"
 */
struct base_spec {
    /** Inheritance visibility (PUBLIC by default, as in C++ struct). */
    visibility vis = PUBLIC;
    /** Raw name as written in source (before resolution). */
    std::string raw_name;
    /** Resolved base structure (set during symbol resolution). */
    std::shared_ptr<structure> base;

    base_spec() = default;
    base_spec(const std::string& raw_name, visibility vis = PUBLIC)
        : vis(vis), raw_name(raw_name) {}
};

class structure : public element, public named_element, public variable_holder, public function_holder, public structure_holder {
protected:
    friend class ns;
    friend class gen::implementation_generator;
    friend class gen::symbol_resolver;
    friend class gen::type_reference_resolver;

    /** Collection of all children of this namespace. */
    std::vector<std::shared_ptr<element>> _children;

    std::vector<std::shared_ptr<constructor>> _constructors;

    std::shared_ptr<destructor> _destructor;

    /** Optional static constructor (class initializer), named with the struct name and static. */
    std::shared_ptr<static_constructor> _static_constructor;

    /** Optional static destructor (class finalizer), named with ~struct_name and static. */
    std::shared_ptr<static_destructor> _static_destructor;

    std::shared_ptr<struct_type> _type;

    /** True if this structure is a static nested struct (no implicit parent reference). */
    bool _is_static_nested = false;

    /** True if this structure is final (cannot be used as a base class). */
    bool _is_final = false;

    /** Declared visibility of this structure. PUBLIC by default. */
    visibility _visibility = PUBLIC;

    /** Synthetic member variable for the implicit parent pointer (non-static nested structs only). */
    std::shared_ptr<member_variable_definition> _parent_field;

    /** Base classes declared in the inheritance clause (in declaration order). */
    std::vector<base_spec> _bases;

    structure(std::shared_ptr<element> parent) :
        element(parent) {}

    static std::shared_ptr<structure> make_shared(std::shared_ptr<element> parent, const std::string &name);

    std::shared_ptr<variable_definition> do_create_variable(const std::string &name, bool is_static) override;
    void on_variable_defined(std::shared_ptr<variable_definition>) override;

    std::shared_ptr<function> do_create_function(const std::string &name, bool is_static) override;
    void on_function_defined(std::shared_ptr<function>) override;

    std::shared_ptr<structure> do_create_structure(const std::string &name) override;
    void on_structure_defined(std::shared_ptr<structure>) override;

    void set_struct_type(const std::shared_ptr<struct_type>& st_type) {
        _type = st_type;
    }

    void update_mangled_name() override;

public:

    void accept(model_visitor& visitor) override;

    std::shared_ptr<struct_type> get_struct_type() const {
        return _type;
    }

    visibility get_visibility() const { return _visibility; }
    void set_visibility(visibility v) { _visibility = v; }

    /** True if this structure is declared inside another structure (static or non-static). */
    bool is_nested() const { return !!parent<structure>(); }

    /** True if this structure is a static nested struct (no implicit parent reference). */
    bool is_static_nested() const { return _is_static_nested; }

    /** Set whether this is a static nested struct. */
    void set_static_nested(bool v) { _is_static_nested = v; }

    /** True if this structure is final (cannot be used as a base class). */
    bool is_final() const { return _is_final; }

    /** Set whether this structure is final. */
    void set_final(bool v) { _is_final = v; }

    /** True if this is a non-static inner struct (has an implicit parent reference). */
    bool is_inner() const { return is_nested() && !_is_static_nested; }

    /** Returns the direct enclosing structure, or nullptr if not nested. */
    std::shared_ptr<structure> get_enclosing_structure() const {
        return std::const_pointer_cast<structure>(parent<structure>());
    }

    /** Returns the synthetic __parent__ member variable (non-static inner structs only, set during symbol resolution). */
    std::shared_ptr<member_variable_definition> get_parent_field() const { return _parent_field; }

    //
    // Children functions
    //

    std::shared_ptr<function> define_function(const std::string& name, bool is_static) override;

    const std::vector<std::shared_ptr<element>>& get_children() const {
        return _children;
    }

    const std::vector<std::shared_ptr<constructor>>& constructors() const { return _constructors; }

    std::shared_ptr<destructor> get_destructor() const { return _destructor; }

    /** Returns the static constructor (class initializer) if defined, nullptr otherwise. */
    std::shared_ptr<static_constructor> get_static_constructor() const { return _static_constructor; }

    /** Returns the static destructor (class finalizer) if defined, nullptr otherwise. */
    std::shared_ptr<static_destructor> get_static_destructor() const { return _static_destructor; }

    //
    // Inheritance
    //

    /** Add a base class to the inheritance clause. */
    void add_base(const std::string& raw_name, visibility vis = PUBLIC) {
        _bases.push_back({raw_name, vis});
    }

    /** Returns the list of base class specs (in declaration order). */
    const std::vector<base_spec>& get_bases() const { return _bases; }
    std::vector<base_spec>& get_bases_mutable() { return _bases; }

    /** True if this structure has at least one base class. */
    bool has_bases() const { return !_bases.empty(); }

    /**
     * Return true if this struct (directly or transitively) derives from `base_st`.
     * Only works after symbol resolution (base_spec::base must be set).
     */
    bool is_derived_from(const std::shared_ptr<structure>& base_st) const;

    /**
     * Return the list of ALL base specs in depth-first BFS order
     * (direct bases first, then their bases, etc.).
     * Only works after symbol resolution.
     */
    std::vector<base_spec> get_all_bases() const;

    /**
     * Returns the copy constructor if one exists, nullptr otherwise.
     * A copy constructor is one whose first (and only non-this) parameter is of type
     * `const Struct&` or `Struct&`.
     */
    std::shared_ptr<constructor> get_copy_constructor() const;
};

class parameter : public element, public variable_definition {
protected:

    friend class function;
    friend class gen::implementation_generator;

    std::shared_ptr<function> _function;

    size_t _pos;

    /** Optional default value expression for this parameter (may be nullptr). */
    std::shared_ptr<expression> _default_expr;

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

    /** Returns the optional default expression for this parameter (nullptr if none). */
    std::shared_ptr<expression> get_default_expr() const { return _default_expr; }
    /** Sets the default expression for this parameter. */
    void set_default_expr(std::shared_ptr<expression> expr) { _default_expr = std::move(expr); }
    /** True if this parameter has a default value expression. */
    bool has_default_expr() const { return _default_expr != nullptr; }
};

class function : public element, public named_element, public variable_holder {
public:
    /**
     * Aliasing specifier set by '-> default' or '-> delete' on constructor declarations.
     * NONE means a regular user-defined body is present.
     */
    enum class function_aliasing { NONE, DEFAULT, DELETE };

protected:
    friend class ns;
    friend class structure;
    friend class gen::implementation_generator;
    friend class gen::symbol_resolver;
    friend class gen::type_reference_resolver;

    bool _is_static = false;

    /** True if this function was auto-generated by the compiler (e.g. default constructor), false if defined by the user. */
    bool _compiler_generated = false;

    /** Declared visibility of this function. PUBLIC by default. */
    visibility _visibility = PUBLIC;

    /** Backing storage for the aliasing specifier. */
    function_aliasing _aliasing = function_aliasing::NONE;

    std::shared_ptr<type> _return_type;
    std::vector<std::shared_ptr<parameter>> _parameters;
    std::shared_ptr<parameter> _this_param;
    std::shared_ptr<block> _block;

    function(std::shared_ptr<element> parent, bool is_static = false) :
        element(parent), _is_static(is_static) {}

    static std::shared_ptr<function> make_shared(std::shared_ptr<element> parent, const std::string& name, bool is_static = false);

    std::shared_ptr<variable_definition> do_create_variable(const std::string &name, bool is_static) override;
    void on_variable_defined(std::shared_ptr<variable_definition>) override;

    void update_mangled_name() override;

    void create_this_parameter();

public:
    void accept(model_visitor& visitor) override;

    void set_return_type(std::shared_ptr<type> return_type);
    bool has_return_type() const {return _return_type != nullptr;}
    std::shared_ptr<type> get_return_type() {return _return_type;}
    std::shared_ptr<const type> get_return_type() const {return _return_type;}

    const std::vector<std::shared_ptr<parameter>>& parameters() const {
        return _parameters;
    }

    std::shared_ptr<variable_definition> append_variable(const std::string& name, bool is_static) override;

    std::shared_ptr<parameter> append_parameter(const std::string& name, std::shared_ptr<type> type);
    std::shared_ptr<parameter> insert_parameter(const std::string& name, std::shared_ptr<type> type, size_t pos);

    size_t get_parameter_size() const {return _parameters.size();}
    bool has_parameter()const {return !_parameters.empty();}
    std::shared_ptr<parameter> get_parameter(size_t index);
    std::shared_ptr<const parameter> get_parameter(size_t index)const;

    std::shared_ptr<parameter> get_parameter(const std::string& name);
    std::shared_ptr<const parameter> get_parameter(const std::string& name)const;

    std::shared_ptr<parameter> get_this_parameter() const {
        return _this_param;
    }

    void set_block(const std::shared_ptr<block>& block);
    std::shared_ptr<block> get_block();

    bool is_static() const { return _is_static; }
    bool is_compiler_generated() const { return _compiler_generated; }
    void set_compiler_generated(bool v) { _compiler_generated = v; }
    bool is_member() const;
    std::shared_ptr<const structure> get_owner() const;
    std::shared_ptr<structure> get_owner();

    visibility get_visibility() const { return _visibility; }
    void set_visibility(visibility v) { _visibility = v; }

    /** Returns the aliasing specifier (NONE / DEFAULT / DELETE). */
    function_aliasing get_aliasing() const { return _aliasing; }
    /** Set the aliasing specifier. */
    void set_aliasing(function_aliasing a) { _aliasing = a; }
    /** True if the constructor was declared with '-> default ;'. */
    bool is_defaulted() const { return _aliasing == function_aliasing::DEFAULT; }
    /** True if the constructor was declared with '-> delete ;'. */
    bool is_deleted() const { return _aliasing == function_aliasing::DELETE; }
};


class constructor : public function {
protected:
    friend class structure;
    friend class gen::symbol_resolver;
    friend class gen::type_reference_resolver;

public:
    /**
     * A single explicit member initializer as provided in the constructor's mem-initializer-list.
     */
    struct member_init_spec {
        std::string member_name;
        std::vector<std::shared_ptr<expression>> args;
        /** True if this initializer refers to a base class (not a member variable). */
        bool is_base_init = false;
        /** Resolved base structure (set during symbol resolution, only when is_base_init=true). */
        std::shared_ptr<structure> base_struct;
    };

protected:
    /** Explicit member initializers from the source mem-initializer-list (in declaration order). */
    std::vector<member_init_spec> _member_inits;

    /** True if this constructor is a copy constructor (detected/marked during symbol resolution). */
    bool _is_copy_constructor = false;

    constructor(std::shared_ptr<structure> parent) :
        function(parent) {}

    void update_mangled_name() override;

    static std::shared_ptr<constructor> make_shared(std::shared_ptr<structure> parent);

public:
    void accept(model_visitor& visitor) override;

    void add_member_init(const std::string& name, std::vector<std::shared_ptr<expression>> args, bool is_base_init = false) {
        _member_inits.push_back({name, std::move(args), is_base_init});
    }

    const std::vector<member_init_spec>& member_inits() const { return _member_inits; }

    bool is_copy_constructor() const { return _is_copy_constructor; }
    void set_copy_constructor(bool v) { _is_copy_constructor = v; }
};


class destructor : public function {
protected:
    friend class structure;
    friend class gen::symbol_resolver;

    destructor(std::shared_ptr<structure> parent) :
        function(parent) {}

    void update_mangled_name() override;

    static std::shared_ptr<destructor> make_shared(std::shared_ptr<structure> parent);

public:
    void accept(model_visitor& visitor) override;

};


/**
 * Static constructor: a static no-argument void function named exactly with the structure name.
 * Acts as a class initializer. Its execution is registered in the global initializer function.
 */
class static_constructor : public function {
public:
    /**
     * A dependency declared in the static constructor's mem-init list.
     * Syntax: `static S() : A(), gvar() {}`
     *
     * Each entry is initially created with only a raw name (from the parser/model_builder).
     * During symbol resolution (symbol_resolver::visit_static_constructor), the name is
     * resolved to either a `structure` (whose static constructor must run first) or a
     * `global_variable_definition` (which must be initialized first).
     * After resolution the `resolved` variant holds the concrete model element; the raw
     * `name` string is kept for diagnostics only.
     *
     * Resolution is performed exclusively by gen::symbol_resolver — the model itself
     * contains no resolution logic.
     */
    struct static_dep_spec {
        /// Raw name as written in source — kept for error messages only.
        std::string name;

        /// Resolved target: monostate = not yet resolved (or resolution failed),
        /// shared_ptr<structure> = depends on that struct's static constructor,
        /// shared_ptr<global_variable_definition> = depends on that global variable.
        std::variant<
            std::monostate,
            std::shared_ptr<structure>,
            std::shared_ptr<global_variable_definition>
        > resolved;

        bool is_resolved() const { return resolved.index() != 0; }

        /// True when the dep resolved to a structure.
        bool is_structure() const {
            return std::holds_alternative<std::shared_ptr<structure>>(resolved);
        }
        /// True when the dep resolved to a global variable.
        bool is_global_variable() const {
            return std::holds_alternative<std::shared_ptr<global_variable_definition>>(resolved);
        }

        /// Returns the resolved structure (nullptr if not a structure dep).
        std::shared_ptr<structure> get_structure() const {
            auto* p = std::get_if<std::shared_ptr<structure>>(&resolved);
            return p ? *p : nullptr;
        }
        /// Returns the resolved global variable (nullptr if not a global-var dep).
        std::shared_ptr<global_variable_definition> get_global_variable() const {
            auto* p = std::get_if<std::shared_ptr<global_variable_definition>>(&resolved);
            return p ? *p : nullptr;
        }
    };

protected:
    friend class structure;
    friend class gen::symbol_resolver;
    friend class gen::init_order_resolver;

    std::vector<static_dep_spec> _static_deps;

    static_constructor(std::shared_ptr<structure> parent) :
        function(parent, true) {}

    void update_mangled_name() override;

    static std::shared_ptr<static_constructor> make_shared(std::shared_ptr<structure> parent);

public:
    void accept(model_visitor& visitor) override;

    /// Called by model_builder: adds an unresolved dep with the given raw name.
    void add_static_dep(const std::string& raw_name) {
        _static_deps.push_back({raw_name, std::monostate{}});
    }

    /// Returns the full list of dependency specs (resolved or not).
    const std::vector<static_dep_spec>& member_inits() const { return _static_deps; }

    /// Returns a mutable reference used by gen::symbol_resolver to fill in resolved targets.
    std::vector<static_dep_spec>& mutable_member_inits() { return _static_deps; }
};


/**
 * Static destructor: a static no-argument void function named with "~" + structure name.
 * Acts as a class finalizer. Its execution is registered in the global finalizer function.
 */
class static_destructor : public function {
protected:
    friend class structure;
    friend class gen::symbol_resolver;

    static_destructor(std::shared_ptr<structure> parent) :
        function(parent, true) {}

    void update_mangled_name() override;

    static std::shared_ptr<static_destructor> make_shared(std::shared_ptr<structure> parent);

public:
    void accept(model_visitor& visitor) override;
};


/**
 * An "init item" is one node in the global initialization/finalization graph.
 * It is either:
 *   - a static_constructor (class-level initializer for a structure), or
 *   - a global_variable_definition (global or static struct member variable).
 *
 * This variant type is used in the unified ordered init/finit sequence produced
 * by init_order_resolver.
 */
using init_item = std::variant<
    std::shared_ptr<static_constructor>,
    std::shared_ptr<global_variable_definition>
>;


class global_tool_function : public function {
protected:
    /**
     * Raw (unordered) set of global variables registered for initialization.
     * Populated by type_reference_resolver::visit_global_variable_definition.
     * Key = global variable, Value = set of explicit global-variable dependencies
     * (filled later by init_order_resolver).
     */
    std::vector<std::shared_ptr<global_variable_definition>> _global_vars;

    /**
     * Raw (unordered) set of static constructors registered for initialization.
     * Populated by type_reference_resolver::visit_static_constructor.
     */
    std::vector<std::shared_ptr<static_constructor>> _static_ctors;

    /**
     * Unified ordered init sequence produced by init_order_resolver::resolve().
     * For the constructor function this is construction order (dependencies first).
     * For the destructor function this is the exact reverse.
     * Each element is either a static_constructor or a global_variable_definition.
     */
    std::vector<init_item> _ordered_items;

    /**
     * Standalone static destructors: structs that have a static ~S() but no static S().
     * These are only meaningful for the destructor function; stored separately since
     * they have no corresponding init_item in the construction order.
     */
    std::vector<std::shared_ptr<static_destructor>> _standalone_sdtors;

    global_tool_function(std::shared_ptr<element> parent) : function(parent) {
    }

public:

    void accept(model_visitor& visitor) override;

    void update_mangled_name() override;

    /** Register a global variable for initialization (called during type resolution). */
    void add_global_variable_definition(const std::shared_ptr<global_variable_definition>& gv);

    /** Register a static constructor for initialization (called during type resolution). */
    void add_static_constructor(const std::shared_ptr<static_constructor>& sctor);

    /** (Legacy shim) register any static function — only static_constructor supported here. */
    void add_static_function(const std::shared_ptr<function>& func);

    /** Returns the raw (unordered) list of registered global variables. */
    const std::vector<std::shared_ptr<global_variable_definition>>& get_global_variables() const { return _global_vars; }

    /** Returns the raw (unordered) list of registered static constructors. */
    const std::vector<std::shared_ptr<static_constructor>>& get_static_constructors() const { return _static_ctors; }

    /** Returns the unified ordered init sequence (set by init_order_resolver). */
    const std::vector<init_item>& get_ordered_items() const { return _ordered_items; }

    /** Set the ordered sequence (called by init_order_resolver). */
    void set_ordered_items(std::vector<init_item> items) { _ordered_items = std::move(items); }

    /** Add standalone static destructors (set by init_order_resolver for destructor function). */
    void add_standalone_static_dtors(const std::vector<std::shared_ptr<static_destructor>>& dtors) {
        _standalone_sdtors.insert(_standalone_sdtors.end(), dtors.begin(), dtors.end());
    }

    /** Returns standalone static destructors (structs with ~S() but no S()). */
    const std::vector<std::shared_ptr<static_destructor>>& get_standalone_static_dtors() const {
        return _standalone_sdtors;
    }

    /**
     * Legacy: return sorted global variables in initialization order (from _ordered_items).
     * Only includes global_variable_definition items from the ordered sequence.
     */
    std::vector<std::shared_ptr<global_variable_definition>> get_sorted_global_variables() const;

    /**
     * Legacy: return static constructors in initialization order (from _ordered_items).
     */
    std::vector<std::shared_ptr<function>> get_static_functions() const;

};

class global_constructor_function : public global_tool_function {
protected:
    friend class unit;
    global_constructor_function(std::shared_ptr<element> parent);

public:
    void accept(model_visitor& visitor) override;
};

class global_destructor_function : public global_tool_function {
protected:
    friend class unit;
    global_destructor_function(std::shared_ptr<element> parent);
public:
    void accept(model_visitor& visitor) override;
};

class global_main_function : public function {
protected:

    std::shared_ptr<function> _real_main_func;

    friend class unit;
    global_main_function(std::shared_ptr<element> parent, std::shared_ptr<function> real_main_func);

public:
    void accept(model_visitor& visitor) override;

    void update_mangled_name() override;

    function& get_real_func() {return *_real_main_func;}
};


class global_variable_definition : public element, public variable_definition {
protected:

    friend class ns;
    friend class structure;
    friend class block;
    friend class gen::implementation_generator;

    /** Declared visibility of this global/static variable. PUBLIC by default. */
    visibility _visibility = PUBLIC;

    global_variable_definition(std::shared_ptr<variable_holder> parent);

    static std::shared_ptr<global_variable_definition> make_shared(std::shared_ptr<variable_holder> parent, const std::string& name);

    void update_mangled_name() override;

public:
    void accept(model_visitor& visitor) override;

    visibility get_visibility() const { return _visibility; }
    void set_visibility(visibility v) { _visibility = v; }
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

    std::shared_ptr<variable_definition> do_create_variable(const std::string &name, bool is_static) override;
    void on_variable_defined(std::shared_ptr<variable_definition>) override;

    std::shared_ptr<function> do_create_function(const std::string &name, bool is_static) override;
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

    std::shared_ptr<global_constructor_function> _global_constructor_func;
    std::shared_ptr<global_destructor_function> _global_destructor_func;

    std::shared_ptr<global_main_function> _global_main_func;

    friend class k::model::gen::symbol_resolver;
    friend class k::model::gen::type_reference_resolver;
    friend class k::model::gen::declaration_generator;
    friend class k::model::gen::implementation_generator;
    friend class k::model::gen::init_order_resolver;

    global_constructor_function& get_global_constructor_function() {return *_global_constructor_func;}
    global_destructor_function& get_global_destructor_function() {return *_global_destructor_func;}

    std::shared_ptr<global_main_function> generate_main_function(std::shared_ptr<function> func);


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

    bool has_main_method() const {
        return _global_main_func !=  nullptr;
    }
};


} // namespace k::model
#endif //KLANG_MODEL_HPP
