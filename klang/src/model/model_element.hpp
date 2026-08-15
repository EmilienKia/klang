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
#ifndef KLANG_MODEL_ELEMENT_HPP
#define KLANG_MODEL_ELEMENT_HPP
#include "model_fwd.hpp"
#include "constant_value.hpp"
namespace k::model {
/**
 * Base class for all language construction.
 */
class element : public std::enable_shared_from_this<element>
{
protected:
    std::shared_ptr<element> _parent = nullptr;

    /** Optional AST node that this model element was built from.
     *  Set during model building; nullptr when the model is built without parsing. */
    std::shared_ptr<k::parse::ast::ast_node> _ast_node;
    /** Optional documentation attached to this model element. */
    std::shared_ptr<k::model::doc::doc_entity> _documentation;

    element(std::shared_ptr<element> parent = nullptr) : _parent(parent) {}

    friend class statement;
    friend class variable_definition;
    friend class template_instantiator;
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

    /** Get the AST node associated with this model element (may be null). */
    std::shared_ptr<k::parse::ast::ast_node> get_ast_node() const { return _ast_node; }

    /** Get the AST node cast to a specific AST type (returns null if not set or wrong type). */
    template<typename T>
    std::shared_ptr<T> get_ast_node_as() const {
        return std::dynamic_pointer_cast<T>(_ast_node);
    }

    /** Get documentation associated with this element (may be null). */
    std::shared_ptr<k::model::doc::doc_entity> get_documentation() const { return _documentation; }
    /** Attach documentation to this element. */
    void set_documentation(std::shared_ptr<k::model::doc::doc_entity> documentation);
    /** Get attached documentation cast to a specific doc type (may be null). */
    template<typename T>
    std::shared_ptr<T> get_documentation_as() const {
        return std::dynamic_pointer_cast<T>(_documentation);
    }

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

    /** True if this variable is declared const (immutable after construction). */
    bool _is_const = false;

    /** Optional initialization statement */
    std::shared_ptr<expression> _init_expr;

    /** Compile-time constant value of this variable if declared const and initialized with a constant expression. */
    std::optional<constant_value> _constant_value;

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

    bool is_const() const { return _is_const; }
    variable_definition& set_const(bool c) { _is_const = c; return *this; }

    bool is_constant() const { return _constant_value.has_value() && _constant_value->is_valid(); }
    const constant_value& get_constant_value() const { return *_constant_value; }
    variable_definition& set_constant_value(constant_value val) { _constant_value = std::move(val); return *this; }
    void clear_constant_value() { _constant_value.reset(); }

    virtual std::shared_ptr<expression> get_init_expr() const;
    virtual variable_definition& set_init_expr(std::shared_ptr<expression> init_expr);
    // Convenience overload for constructor-invocation init (most common case)
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
    friend class template_instantiator;
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

    /**
     * Register an already-constructed function in this holder without creating
     * a new one.  Used by the compiler to move a free function into a class
     * during the Application synthesis pass.
     */
    void add_existing_function(std::shared_ptr<function> func);

    /**
     * Remove the given function from this holder's list (does nothing if not
     * present).  Used to move a free function from a namespace into a class.
     */
    void remove_function(const std::shared_ptr<function>& func);

protected:
    /** List of all defined functions. */
    std::vector<std::shared_ptr<function>> _functions;

    virtual std::shared_ptr<function> do_create_function(const std::string &name, bool is_static) =0;
    virtual void on_function_defined(std::shared_ptr<function>) =0;
    /** Called by remove_function() after erasing from _functions. Override to remove from _children etc. */
    virtual void on_function_removed(const std::shared_ptr<function>&) {}
};

/**
* Interface for holding aggregates (structures and classes) (like ns and aggregates)
*/
class aggregate_holder
{
public:
    virtual std::shared_ptr<aggregate> define_aggregate(const std::string& name, bool is_class = false);
    virtual std::shared_ptr<structure> define_structure(const std::string& name);
    virtual std::shared_ptr<klass> define_class(const std::string& name);
    virtual std::shared_ptr<interface> define_interface(const std::string& name);
    virtual std::shared_ptr<annotation_type> define_annotation(const std::string& name);
    virtual std::shared_ptr<aggregate> get_aggregate(const std::string& name) const;
    /** Legacy: get by name as structure pointer (returns nullptr if not an aggregate or not found). */
    virtual std::shared_ptr<structure> get_structure(const std::string& name) const;

protected:
    /** Map of all defined aggregates (structures and classes). */
    std::map<std::string, std::shared_ptr<aggregate>> _structs;

    virtual std::shared_ptr<structure> do_create_structure(const std::string &name) =0;
    virtual std::shared_ptr<klass> do_create_class(const std::string &name) =0;
    virtual std::shared_ptr<interface> do_create_interface(const std::string &name) =0;
    virtual std::shared_ptr<annotation_type> do_create_annotation(const std::string &name) =0;
    virtual void on_aggregate_defined(std::shared_ptr<aggregate>) =0;

public:
    const std::map<std::string, std::shared_ptr<aggregate>>& aggregates() const {return _structs;}
};


/**
 * Interface for holding enumerations (like ns and aggregates).
 */
class enum_holder
{
public:
    virtual std::shared_ptr<enumeration> define_enum(const std::string& name);
    virtual std::shared_ptr<enumeration> get_enum(const std::string& name) const;

    const std::map<std::string, std::shared_ptr<enumeration>>& enums() const { return _enums; }

protected:
    /** Map of all defined enumerations. */
    std::map<std::string, std::shared_ptr<enumeration>> _enums;

    virtual std::shared_ptr<enumeration> do_create_enum(const std::string& name) = 0;
    virtual void on_enum_defined(std::shared_ptr<enumeration>) = 0;
};


/**
 * Interface for holding union type definitions (like ns and aggregates).
 */
class union_holder
{
public:
    virtual std::shared_ptr<union_type_def> define_union(const std::string& name);
    virtual std::shared_ptr<union_type_def> get_union(const std::string& name) const;

    const std::map<std::string, std::shared_ptr<union_type_def>>& unions() const { return _unions; }

protected:
    /** Map of all defined unions. */
    std::map<std::string, std::shared_ptr<union_type_def>> _unions;

    virtual std::shared_ptr<union_type_def> do_create_union(const std::string& name) = 0;
    virtual void on_union_defined(std::shared_ptr<union_type_def>) = 0;
};


/**
 * Describes a single 'using' directive in a scope.
 *
 * A using directive makes elements from another scope resolvable as if they
 * were direct members of the enclosing scope.  It does NOT create or
 * materialise new symbols — it only affects name-lookup priority.
 *
 * Three forms:
 *  - Namespace using:   'using namespace X::Y;'        — all members of X::Y are injected.
 *  - Specific using:    'using X::Y::foo;'             — only 'foo' is injected.
 *  - Aliased using:     'using Alias = X::Y::foo;'     — 'foo' is accessible as 'Alias'.
 *                       'using NS = namespace X::Y;'   — X::Y is accessible via NS::member.
 *
 * Aliasing is purely local: exports/imports always use the real (de-aliased) names.
 */
struct using_directive {
    /// What kind of element is targeted (NONE = any).
    enum class filter_t { NONE, NAMESPACE, STRUCT, INTERFACE, CLASS };

    filter_t filter = filter_t::NONE;

    /// The fully-qualified name being imported.
    k::name target_name;

    /// Optional alias name. When set, the target is accessible under this
    /// name instead of its original short name.
    std::optional<std::string> alias_name;

    /// Whether this is a namespace using (using namespace X) vs specific element using (using X::foo).
    bool is_namespace() const { return filter == filter_t::NAMESPACE; }

    /// Whether this directive carries a local alias.
    bool has_alias() const { return alias_name.has_value(); }

    /// AST node for error reporting (may be null).
    std::shared_ptr<k::parse::ast::ast_node> ast_node;
};


/**
 * Interface for scopes that can hold 'using' directives.
 *
 * Mixed into ns, aggregate, block, and for_statement — any scope where a
 * using declaration may appear or whose name lookup should honour inherited
 * using directives.
 */
class using_holder
{
public:
    void add_using_directive(using_directive directive) {
        _using_directives.push_back(std::move(directive));
    }

    const std::vector<using_directive>& get_using_directives() const {
        return _using_directives;
    }

protected:
    std::vector<using_directive> _using_directives;
};


/**
 * A friend directive grants another named entity (aggregate, function, or
 * variable) access to the protected and private members of the declaring aggregate.
 *
 * Syntax: 'friend' ['struct'|'interface'|'class']? qualified_identifier ['<' template-args '>']? ';'
 *
 * Friendship is NOT inherited, and does NOT propagate to nested aggregates.
 * Friends gain access to both protected and private members.
 */
struct friend_directive {
    /// What kind of element is targeted (NONE = any).
    enum class filter_t { NONE, STRUCT, INTERFACE, CLASS };

    filter_t filter = filter_t::NONE;

    /// The fully-qualified name of the friend entity (without template args).
    k::name target_name;

    /// Raw template argument names as written at the declaration site (e.g. {"T"} for
    /// 'friend Foo<T>;'). These are stored as strings before instantiation; after
    /// template instantiation the substituted resolved types are stored in
    /// resolved_tpl_arg_types.
    std::vector<std::string> raw_template_arg_names;

    /// Resolved concrete types for each template argument, filled by
    /// template_instantiator::clone_friend_directives() during aggregate instantiation.
    /// Empty on the template definition itself; non-empty on concrete instantiations.
    std::vector<std::shared_ptr<type>> resolved_tpl_arg_types;

    /// True when '<...>' was explicitly written (even if template_args is empty).
    bool has_explicit_template_args = false;

    /// AST node for error reporting (may be null).
    std::shared_ptr<k::parse::ast::ast_node> ast_node;
};


/**
 * Interface for scopes that can hold 'friend' directives.
 *
 * Mixed into aggregate — friendship is only declared inside aggregate bodies.
 */
class friend_holder
{
public:
    void add_friend_directive(friend_directive directive) {
        _friend_directives.push_back(std::move(directive));
    }

    const std::vector<friend_directive>& get_friend_directives() const {
        return _friend_directives;
    }

protected:
    std::vector<friend_directive> _friend_directives;
};


/**
 * Describes a single annotation instance attached to a model element.
 *
 * At model-building time the annotation type is unresolved: only the raw
 * qualified name (from the AST) is stored. Resolution to a concrete
 * annotation_type will happen in a later compiler phase.
 */
struct annotation_instance {
    /// Raw qualified name of the annotation type (e.g. "my::Deprecated").
    std::string raw_name;

    /// The AST annotation_def node for later resolution and initializer access.
    std::shared_ptr<k::parse::ast::annotation_def> ast_node;

    /// Resolved annotation type (set during symbol resolution phase).
    /// Points to any aggregate with is_annotation() == true (can be
    /// annotation_type for local definitions or imported_annotation_type
    /// for types imported from KDI).
    std::shared_ptr<aggregate> resolved_type;

    /**
     * Resolved compile-time constant values for each member field.
     * Populated during the annotation materialisation phase.
     * Each entry is an LLVM Constant* keyed by the member variable index
     * (following the LLVM struct field order of the annotation type).
     * Empty until materialisation runs.
     */
    std::vector<llvm::Constant*> resolved_field_constants;

    annotation_instance() = default;
    annotation_instance(std::string raw_name,
                        std::shared_ptr<k::parse::ast::annotation_def> ast_node)
        : raw_name(std::move(raw_name)), ast_node(std::move(ast_node)) {}
};


/**
 * Interface for model elements that can carry annotation instances.
 *
 * Mixed into aggregate — annotations are initially only on aggregate
 * declarations. Other element types can gain this mixin later.
 */
class annotation_holder
{
public:
    void add_annotation(annotation_instance ann) {
        _annotations.push_back(std::move(ann));
    }

    bool has_annotations() const {
        return !_annotations.empty();
    }

    const std::vector<annotation_instance>& get_annotations() const {
        return _annotations;
    }

    std::vector<annotation_instance>& get_annotations_mutable() {
        return _annotations;
    }

protected:
    std::vector<annotation_instance> _annotations;
};


/**
 * A single entry in an enumeration definition.
 */

} // namespace k::model

#endif //KLANG_MODEL_ELEMENT_HPP
