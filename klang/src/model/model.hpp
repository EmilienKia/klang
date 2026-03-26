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
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

#include "../lex/lexer.hpp"
#include "../common/common.hpp"
#include "type.hpp"
#include "import.hpp"

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>

namespace k::parse::ast {
struct ast_node;
struct function_decl;
struct parameter_spec;
struct aggregate_decl;
struct enum_decl;
}

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
class aggregate;
class structure;
class enumeration;
class klass;
class interface;
class ns;
class unit;

class global_tool_function;
class global_constructor_function;
class global_destructor_function;
// Forward declarations for imported model nodes (defined in imported.hpp).
class imported_function;
class imported_constructor;
class imported_destructor;
class imported_method;
class imported_variable;
class imported_aggregate;
class imported_structure;
class imported_klass;
class imported_interface;


namespace gen {
class symbol_resolver;
class aggregate_type_resolver;
class model_materializer;
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

/**
 * A single entry in the vtable of a class.
 * Represents a virtual function slot: the function that occupies this slot
 * and the index within the vtable array (after the RTTI placeholder at index 0).
 */
struct vtable_entry {
    /** Slot index in the vtable (0 = first function slot, i.e. vtable[1] after RTTI). */
    size_t slot_index = 0;
    /**
     * The most-derived override of this virtual function in the class that owns the vtable.
     * After symbol resolution, this always points to the concrete implementation.
     */
    std::shared_ptr<function> func;
    /**
     * The "introducing" function (the first declaration of this virtual slot
     * in the inheritance hierarchy). Used for signature matching.
     */
    std::shared_ptr<function> introducing_func;
};

/**
 * This-adjustment thunk descriptor for a secondary vtable slot.
 *
 * When a derived class D inherits from multiple bases (B, C) each with their own
 * vtable, the secondary vtable for base C (embedded at a non-zero byte offset in D)
 * needs "thunk" function pointers that adjust 'this' from C* back to D* before
 * calling the real override.
 *
 * This struct describes one such thunk: the slot index, the real override function,
 * and the byte offset to subtract from 'this'.
 *
 * No LLVM types are stored here; they are computed by the generators from these
 * pure model values.
 */
struct thunk_info {
    /** Vtable slot index (0-based within the vtable entries, NOT counting the RTTI slot). */
    size_t slot_index = 0;
    /** The concrete (most-derived) override function to call after this-adjustment. */
    std::shared_ptr<function> real_func;
    /**
     * Byte offset to subtract from 'this' (a Base* pointing into D's layout) to
     * obtain the D* pointing to the start of D. Always positive when the base
     * subobject is at a non-zero offset.
     */
    ptrdiff_t this_adjustment = 0;
    /** True if a thunk is needed (this_adjustment != 0 AND real_func overrides an ancestor). */
    bool needs_thunk = false;
};

/**
 * Descriptor for a secondary vtable that a derived class must emit for one of its
 * non-primary base subobjects.
 *
 * A "secondary vtable" points to the vtable entries as seen from the perspective of
 * a base class embedded at a non-zero offset inside the derived class.  Its function
 * pointers may be this-adjustment thunks when the slot was overridden in the derived class.
 */
struct secondary_vtable_spec {
    /** The base class whose embedded subobject needs a secondary vtable. */
    std::shared_ptr<klass> base_class;
    /**
     * Byte offset of the base subobject within the derived class layout.
     * 0 means the base is at the start of the object — no adjustment needed (skip).
     */
    ptrdiff_t base_offset = 0;
    /** Per-slot thunk descriptors (indexed identically to base_class->get_vtable()->entries). */
    std::vector<thunk_info> slot_thunks;
};

/**
 * Annotation attached to a function_invocation_expression by type_reference_resolver
 * (Phase 3). Describes how the call should be dispatched at the call-site level.
 *
 * This is pure model data — no llvm::* types.  The code generator reads it to
 * decide between a direct LLVM call and a vtable-indirect dispatch.
 */
struct virtual_dispatch_info {
    /** Dispatch strategy chosen at resolution time. */
    enum class dispatch_kind {
        /** Direct (non-virtual) call: call the LLVM function directly. */
        DIRECT,
        /** Vtable dispatch through the static receiver type's vtable. */
        VTABLE,
        /** Indirect call through a function-reference variable (fp(args)). */
        INDIRECT,
        /** Indirect call through a member function pointer (obj.*mfp(args) or ptr->*mfp(args)). */
        INDIRECT_MEMBER,
    };

    dispatch_kind kind = dispatch_kind::DIRECT;

    /**
     * Vtable slot index (0-based within vtable entries, not counting the RTTI slot).
     * Valid only when kind == VTABLE; -1 for DIRECT.
     */
    int slot_index = -1;

    /**
     * The klass whose vtable should be used for the dispatch lookup.
     * This is the *static* receiver type at the call site (e.g. the type of `b` in
     * `b.speak()` where b : Animal&). Non-null when kind == VTABLE and the receiver
     * is a locally-defined klass.
     */
    std::shared_ptr<klass> dispatch_class;

    /**
     * Imported aggregate (imported_klass / imported_interface) to use for vtable
     * dispatch when the receiver is an imported type (neither inherits from klass).
     * Non-null when kind == VTABLE and dispatch_class is null.
     */
    std::shared_ptr<aggregate> imported_dispatch_agg;

    /**
     * Optional this-adjustment offset (bytes) to apply BEFORE loading the vptr.
     * Non-zero when the receiver is a secondary-base reference (e.g. a C& pointing
     * into a D object that embeds C at offset > 0).
     * 0 for primary-base dispatch (no adjustment needed before vptr load).
     */
    ptrdiff_t this_adjustment = 0;
};

/**
 * Complete vtable layout for a class.
 * Each class (or virtual base) has its own vtable descriptor.
 * For single inheritance, there is one vtable_layout per class.
 * For multiple / diamond inheritance, a derived class may have multiple
 * vtable_layouts (one per primary vtable + one per each non-primary base path).
 */
struct vtable_layout {
    /** All virtual function slots in declaration order. */
    std::vector<vtable_entry> entries;

    /**
     * Secondary vtable specifications computed by model_materializer.
     * One entry per non-primary base class with a vtable embedded at non-zero offset.
     * Empty for classes with no multiple inheritance.
     */
    std::vector<secondary_vtable_spec> secondary_vtables;

    /** LLVM global variable holding the vtable constant (set during declaration generation). */
    llvm::GlobalVariable* llvm_global = nullptr;

    /** LLVM struct type for the vtable: { ptr (RTTI), [N x ptr] } (set during type resolution). */
    llvm::StructType* llvm_type = nullptr;

    /**
     * LLVM global variable holding the RTTI constant for this class.
     * The RTTI global is a genuine ::k::Class instance with layout:
     *   { ptr __vptr__ (Class vtable), ptr __vptr_TypeInfo__ (Class secondary vtable), ptr name (short name) }
     * The 'typeid' is the address of this global, valid across dynamic modules.
     * Set during declaration generation. May be non-null even if llvm_global is null
     * (e.g. for abstract classes that have no emitted vtable global).
     */
    llvm::GlobalVariable* llvm_rtti_global = nullptr;

    /** Total number of slots (entries.size()). */
    size_t slot_count() const { return entries.size(); }
};



class model_visitor;

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

    /** Get the AST node associated with this model element (may be null). */
    std::shared_ptr<k::parse::ast::ast_node> get_ast_node() const { return _ast_node; }

    /** Get the AST node cast to a specific AST type (returns null if not set or wrong type). */
    template<typename T>
    std::shared_ptr<T> get_ast_node_as() const {
        return std::dynamic_pointer_cast<T>(_ast_node);
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
* Interface for holding aggregates (structures and classes) (like ns and aggregates)
*/
class aggregate_holder
{
public:
    virtual std::shared_ptr<aggregate> define_aggregate(const std::string& name, bool is_class = false);
    virtual std::shared_ptr<structure> define_structure(const std::string& name);
    virtual std::shared_ptr<klass> define_class(const std::string& name);
    virtual std::shared_ptr<interface> define_interface(const std::string& name);
    virtual std::shared_ptr<aggregate> get_aggregate(const std::string& name) const;
    /** Legacy: get by name as structure pointer (returns nullptr if not an aggregate or not found). */
    virtual std::shared_ptr<structure> get_structure(const std::string& name) const;

protected:
    /** Map of all defined aggregates (structures and classes). */
    std::map<std::string, std::shared_ptr<aggregate>> _structs;

    virtual std::shared_ptr<structure> do_create_structure(const std::string &name) =0;
    virtual std::shared_ptr<klass> do_create_class(const std::string &name) =0;
    virtual std::shared_ptr<interface> do_create_interface(const std::string &name) =0;
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
 * variable) access to the protected members of the declaring aggregate.
 *
 * Syntax: 'friend' ['struct'|'interface'|'class']? qualified_identifier ';'
 *
 * Friendship is NOT inherited, and does NOT propagate to nested aggregates.
 * Friends currently gain access to protected members only (not private).
 */
struct friend_directive {
    /// What kind of element is targeted (NONE = any).
    enum class filter_t { NONE, STRUCT, INTERFACE, CLASS };

    filter_t filter = filter_t::NONE;

    /// The fully-qualified name of the friend entity.
    k::name target_name;

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
 * A single entry in an enumeration definition.
 */
struct enum_entry_def {
    std::string name;
    int64_t value = 0;
    bool is_default = false;
};

/**
 * A raw (unresolved) entry in an enumeration definition.
 * Stored during model building; resolved during the symbol resolution phase.
 */
struct enum_raw_entry_def {
    std::string name;
    std::optional<int64_t> explicit_value;  ///< Set if entry has an integer literal value.
    std::string ref_name;                    ///< Set if entry references another entry by name.
    bool is_default = false;
};

/**
 * Enumeration: a named set of integer-valued constants.
 *
 * An enumeration is a nominal type backed by the smallest primitive integer
 * type that can hold all declared values. Each entry maps a name to a
 * compile-time constant integer value.
 *
 * Enumerations support single inheritance: a derived enum inherits all entries
 * from its base and may add new ones. Multi-level inheritance (A : B : C) is
 * supported. Cycles are detected and rejected.
 */
class enumeration : public element, public named_element {
protected:
    friend class ns;
    friend class aggregate;
    friend class unit;
    friend class gen::symbol_resolver;
    friend class gen::type_reference_resolver;

    std::vector<enum_entry_def> _entries;
    std::shared_ptr<enum_type> _type;
    std::shared_ptr<primitive_type> _underlying_type;
    visibility _visibility = PUBLIC;

    /** Optional base enum name (unresolved, from AST). */
    std::optional<std::string> _base_name;
    /** Resolved base enumeration (set during symbol resolution). */
    std::shared_ptr<enumeration> _base;
    /** Raw (unresolved) entries from AST — used for deferred resolution of derived enums. */
    std::vector<enum_raw_entry_def> _raw_entries;
    /** True when entry values, underlying type and enum_type have been fully resolved. */
    bool _resolved = false;
    /** True while this enum is being resolved (for cycle detection). */
    bool _resolving = false;

    enumeration(std::shared_ptr<element> parent)
        : element(parent) {}

    static std::shared_ptr<enumeration> make_shared(std::shared_ptr<element> parent, const std::string& name);

    void update_mangled_name() override;

public:
    void accept(model_visitor& visitor) override;

    const std::vector<enum_entry_def>& entries() const { return _entries; }
    void add_entry(const std::string& name, int64_t value, bool is_default) {
        _entries.push_back({name, value, is_default});
    }

    std::optional<enum_entry_def> get_entry_by_name(const std::string& name) const {
        for (auto& e : _entries) {
            if (e.name == name) return e;
        }
        return std::nullopt;
    }

    enum_entry_def get_default_entry() const {
        for (auto& e : _entries) {
            if (e.is_default) return e;
        }
        // Fallback: first entry (should always exist)
        return _entries.front();
    }

    std::shared_ptr<enum_type> get_enum_type() const { return _type; }
    void set_enum_type(std::shared_ptr<enum_type> t) { _type = t; }

    std::shared_ptr<primitive_type> get_underlying_type() const { return _underlying_type; }
    void set_underlying_type(std::shared_ptr<primitive_type> t) { _underlying_type = t; }

    visibility get_visibility() const { return _visibility; }
    void set_visibility(visibility v) { _visibility = v; }

    // ── Derivation support ──

    void set_base_name(const std::string& name) { _base_name = name; }
    const std::optional<std::string>& get_base_name() const { return _base_name; }

    void set_base(std::shared_ptr<enumeration> base) { _base = base; }
    std::shared_ptr<enumeration> get_base() const { return _base; }
    bool has_base() const { return _base != nullptr; }

    /** Check whether this enum is derived (directly or transitively) from `other`. */
    bool is_derived_from(const std::shared_ptr<enumeration>& other) const {
        for (auto b = _base; b; b = b->_base) {
            if (b == other) return true;
        }
        return false;
    }

    const std::vector<enum_raw_entry_def>& raw_entries() const { return _raw_entries; }
    void add_raw_entry(const enum_raw_entry_def& e) { _raw_entries.push_back(e); }

    bool is_resolved() const { return _resolved; }
    void set_resolved(bool v) { _resolved = v; }

    // ── AST node accessors ──
    void set_ast_enum_decl(std::shared_ptr<k::parse::ast::enum_decl> ast) {
        _ast_node = std::static_pointer_cast<k::parse::ast::ast_node>(std::move(ast));
    }
    std::shared_ptr<k::parse::ast::enum_decl> get_ast_enum_decl() const {
        return get_ast_node_as<k::parse::ast::enum_decl>();
    }
};


class member_variable_definition : public element, public variable_definition {
protected:

    friend class aggregate;
    friend class structure;
    friend class klass;
    friend class gen::implementation_generator;
    friend class gen::symbol_resolver;
    friend class gen::declaration_generator;

    /** Declared visibility of this member variable. PUBLIC by default. */
    visibility _visibility = PUBLIC;

    member_variable_definition(std::shared_ptr<aggregate> st);

    static std::shared_ptr<member_variable_definition> make_shared(std::shared_ptr<aggregate> st, const std::string &name);

    void update_mangled_name() override;

public:
    void accept(model_visitor& visitor) override;

    visibility get_visibility() const { return _visibility; }
    void set_visibility(visibility v) { _visibility = v; }
};

/**
 * Specifies a single base class in an inheritance clause.
 */
struct base_spec {
    /** Inheritance visibility (PUBLIC by default, as in K struct). */
    visibility vis = PUBLIC;
    /** Raw name as written in source (before resolution). */
    std::string raw_name;
    /** Resolved base aggregate (set during symbol resolution). */
    std::shared_ptr<aggregate> base;
    /**
     * True if this base is inherited virtually (diamond-safe).
     * Set automatically by compute_virtual_bases() after all classes are resolved.
     */
    bool is_virtual = false;

    base_spec() = default;
    base_spec(const std::string& raw_name, visibility vis = PUBLIC)
        : vis(vis), raw_name(raw_name) {}

    /**
     * Returns a sanitised version of raw_name suitable for use as a field identifier:
     * replaces all occurrences of "::" with "_".
     * E.g. "my::base::Foo" → "my_base_Foo"
     */
    std::string sanitised_name() const {
        std::string result = raw_name;
        std::string::size_type pos = 0;
        while ((pos = result.find("::", pos)) != std::string::npos) {
            result.replace(pos, 2, "_");
        }
        return result;
    }
};

/**
 * Abstract base class for all aggregate types (struct and class).
 * Holds all common member data: member variables, functions, constructors,
 * destructor, static ctor/dtor, nested aggregates, bases, vtable, vptrs, etc.
 */
class aggregate : public element, public named_element, public variable_holder, public function_holder, public aggregate_holder, public enum_holder, public using_holder, public friend_holder {
protected:
    friend class ns;
    friend class gen::implementation_generator;
    friend class gen::symbol_resolver;
    friend class gen::type_reference_resolver;

    /** Collection of all children of this aggregate. */
    std::vector<std::shared_ptr<element>> _children;

    std::vector<std::shared_ptr<constructor>> _constructors;

    std::shared_ptr<destructor> _destructor;

    /** Optional static constructor (class initializer). */
    std::shared_ptr<static_constructor> _static_constructor;

    /** Optional static destructor (class finalizer). */
    std::shared_ptr<static_destructor> _static_destructor;

    std::shared_ptr<struct_type> _type;

    /** True if this aggregate is a static nested aggregate (no implicit parent reference). */
    bool _is_static_nested = false;

    /** True if this aggregate is final (cannot be used as a base class). */
    bool _is_final = false;

    /**
     * True if this aggregate is abstract (cannot be instantiated directly).
     * A class is abstract if it is explicitly declared abstract, or if it has at
     * least one directly declared or inherited unimplemented abstract method.
     * Only meaningful on klass (not structure).
     */
    bool _is_abstract = false;

    /** True if this aggregate is declared const (all non-static methods are implicitly const). */
    bool _is_const_struct = false;

    /** Declared visibility of this aggregate. PUBLIC by default. */
    visibility _visibility = PUBLIC;

    /** Synthetic member variable for the implicit parent pointer (non-static nested aggregates only). */
    std::shared_ptr<member_variable_definition> _parent_field;

    /** Base classes declared in the inheritance clause (in declaration order). */
    std::vector<base_spec> _bases;


    aggregate(std::shared_ptr<element> parent) :
        element(parent) {}

    std::shared_ptr<variable_definition> do_create_variable(const std::string &name, bool is_static) override;
    void on_variable_defined(std::shared_ptr<variable_definition>) override;

    std::shared_ptr<function> do_create_function(const std::string &name, bool is_static) override;
    void on_function_defined(std::shared_ptr<function>) override;

    std::shared_ptr<structure> do_create_structure(const std::string &name) override;
    std::shared_ptr<klass> do_create_class(const std::string &name) override;
    std::shared_ptr<interface> do_create_interface(const std::string &name) override;
    void on_aggregate_defined(std::shared_ptr<aggregate>) override;

    std::shared_ptr<enumeration> do_create_enum(const std::string &name) override;
    void on_enum_defined(std::shared_ptr<enumeration>) override;

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

    /** Set the AST aggregate_decl node this aggregate was built from. */
    void set_ast_aggregate_decl(std::shared_ptr<k::parse::ast::aggregate_decl> ast) {
        _ast_node = std::static_pointer_cast<k::parse::ast::ast_node>(std::move(ast));
    }
    /** Get the AST aggregate_decl node (may be null). */
    std::shared_ptr<k::parse::ast::aggregate_decl> get_ast_aggregate_decl() const {
        return get_ast_node_as<k::parse::ast::aggregate_decl>();
    }

    /** True if this aggregate is declared inside another aggregate (static or non-static). */
    bool is_nested() const { return !!parent<aggregate>(); }

    /** True if this aggregate is a static nested aggregate (no implicit parent reference). */
    bool is_static_nested() const { return _is_static_nested; }

    /** Set whether this is a static nested aggregate. */
    void set_static_nested(bool v) { _is_static_nested = v; }

    /** True if this aggregate is final (cannot be used as a base class). */
    bool is_final() const { return _is_final; }

    /** Set whether this aggregate is final. */
    void set_final(bool v) { _is_final = v; }

    /**
     * True if this aggregate is abstract (cannot be directly instantiated).
     * Only meaningful on classes (klass); always false for structs.
     */
    bool is_abstract() const { return _is_abstract; }

    /** Set whether this aggregate is abstract. */
    void set_abstract(bool v) { _is_abstract = v; }

    /** True if this aggregate is declared const (all non-static methods are implicitly const). */
    bool is_const_struct() const { return _is_const_struct; }

    /** Set whether this aggregate is a const aggregate. */
    void set_const_struct(bool v) { _is_const_struct = v; }

    /** True if this is a class (keyword 'class'), false if it is a struct (keyword 'struct'). */
    virtual bool is_class() const { return false; }

    /**
     * True if this aggregate has at least one virtual function (needs a vtable).
     * Kept virtual on aggregate for generic call sites (e.g. virtual dispatch check
     * in gen_expressions.cpp) that hold a shared_ptr<aggregate> without knowing
     * the concrete type.
     */
    virtual bool has_vtable() const { return false; }


    /** True if this is a non-static inner aggregate (has an implicit parent reference). */
    bool is_inner() const { return is_nested() && !_is_static_nested; }

    /** Returns the direct enclosing aggregate, or nullptr if not nested. */
    std::shared_ptr<aggregate> get_enclosing_aggregate() const {
        return std::const_pointer_cast<aggregate>(parent<aggregate>());
    }

    /** Alias for backward compatibility — same as get_enclosing_aggregate(). */
    std::shared_ptr<aggregate> get_enclosing_structure() const {
        return get_enclosing_aggregate();
    }

    /** Returns the synthetic __parent__ member variable (non-static inner aggregates only). */
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

    /** Add a base to the inheritance clause. */
    void add_base(const std::string& raw_name, visibility vis = PUBLIC) {
        _bases.push_back({raw_name, vis});
    }

    /** Returns the list of base specs (in declaration order). */
    const std::vector<base_spec>& get_bases() const { return _bases; }
    std::vector<base_spec>& get_bases_mutable() { return _bases; }

    /** True if this aggregate has at least one base. */
    bool has_bases() const { return !_bases.empty(); }

    /** True if this aggregate has any direct virtual bases. */
    bool has_virtual_bases() const {
        for (auto& bs : _bases) {
            if (bs.is_virtual) return true;
        }
        return false;
    }

    /** Collect all transitively-declared virtual base aggregates (unique, BFS order). */
    std::vector<std::shared_ptr<aggregate>> get_all_virtual_base_structs() const;

    /** Return true if this aggregate (directly or transitively) derives from base_st. */
    bool is_derived_from(const std::shared_ptr<aggregate>& base_st) const;

    /** Return the list of ALL base specs in depth-first BFS order. */
    std::vector<base_spec> get_all_bases() const;

    /** Returns the copy constructor if one exists, nullptr otherwise. */
    std::shared_ptr<constructor> get_copy_constructor() const;
};

/**
 * Struct aggregate: concrete aggregate declared with the 'struct' keyword.
 * Constraints: default member visibility PUBLIC, no virtual dispatch,
 * no cross-inheritance with classes.
 */
class structure : public aggregate {
protected:
    friend class ns;
    friend class aggregate;
    friend class gen::implementation_generator;
    friend class gen::symbol_resolver;
    friend class gen::type_reference_resolver;

    structure(std::shared_ptr<element> parent) :
        aggregate(parent) {}

    static std::shared_ptr<structure> make_shared(std::shared_ptr<element> parent, const std::string &name);

public:
    bool is_class() const override { return false; }

    void accept(model_visitor& visitor) override;

    /** Returns the direct enclosing structure, or nullptr if not nested in a struct. */
    std::shared_ptr<structure> get_enclosing_structure() const {
        return std::dynamic_pointer_cast<structure>(get_enclosing_aggregate());
    }
};

/**
 * Class aggregate: concrete aggregate declared with the 'class' keyword.
 * Constraints: default member variable visibility PROTECTED, default function visibility PUBLIC,
 * enables virtual dispatch, no cross-inheritance with structs,
 * no private inheritance.
 */
class klass : public aggregate {
protected:
    friend class ns;
    friend class aggregate;
    friend class gen::implementation_generator;
    friend class gen::symbol_resolver;
    friend class gen::type_reference_resolver;

    /**
     * Primary vtable layout for this class (set during symbol resolution
     * when the class has at least one virtual function).
     */
    std::shared_ptr<vtable_layout> _vtable;

    /**
     * Secondary vtable layouts for non-primary base paths (multiple / diamond inheritance).
     */
    std::vector<std::pair<std::shared_ptr<aggregate>, std::shared_ptr<vtable_layout>>> _secondary_vtables;

    /**
     * Synthetic vptr member variables (one per vtable — primary + secondaries).
     */
    std::vector<std::shared_ptr<member_variable_definition>> _vptrs;

    klass(std::shared_ptr<element> parent) :
        aggregate(parent) {}

    static std::shared_ptr<klass> make_shared(std::shared_ptr<element> parent, const std::string &name);

public:
    bool is_class() const override { return true; }
    bool has_vtable() const override { return _vtable != nullptr; }

    void accept(model_visitor& visitor) override;

    // ── Virtuality accessors (concrete, klass-only — not part of aggregate interface) ──

    std::shared_ptr<vtable_layout> get_vtable() const { return _vtable; }

    void set_vtable(std::shared_ptr<vtable_layout> vt) { _vtable = std::move(vt); }

    const std::vector<std::pair<std::shared_ptr<aggregate>, std::shared_ptr<vtable_layout>>>&
    get_secondary_vtables() const { return _secondary_vtables; }

    void add_secondary_vtable(std::shared_ptr<aggregate> base, std::shared_ptr<vtable_layout> vt) {
        _secondary_vtables.emplace_back(std::move(base), std::move(vt));
    }

    const std::vector<std::shared_ptr<member_variable_definition>>& get_vptrs() const { return _vptrs; }

    std::shared_ptr<member_variable_definition> inject_vptr_field(const std::string& field_name) {
        auto vptr_field = member_variable_definition::make_shared(shared_as<aggregate>(), field_name);
        _vptrs.push_back(vptr_field);
        _vars.insert({field_name, vptr_field});
        _children.insert(_children.begin(), vptr_field);
        return vptr_field;
    }

    // ── Inheritance / diamond detection ──────────────────────────────────────

    /**
     * Automatically detect diamond patterns in class hierarchies and mark
     * the appropriate base_spec entries as virtual (is_virtual = true).
     * Must be called after all base pointers have been resolved.
     */
    static void compute_virtual_bases(const std::vector<std::shared_ptr<aggregate>>& all_aggregates);

    /** Returns the direct enclosing class, or nullptr if not nested in a class. */
    std::shared_ptr<klass> get_enclosing_class() const {
        return std::dynamic_pointer_cast<klass>(get_enclosing_aggregate());
    }

    /**
     * True if this class has at least one vtable slot whose current function is still abstract
     * (i.e. the slot was introduced by an abstract method and was not overridden by a
     * concrete implementation in this class or any ancestor up to and including this class).
     * Requires that the vtable layout has already been built (symbol_resolver pass).
     */
    bool has_abstract_vtable_slots() const;
};

/**
 * Interface aggregate: concrete aggregate declared with the 'interface' keyword.
 * An interface is an abstract, vtable-enabled aggregate where all member functions
 * are implicitly abstract and public. No member variables are allowed (by convention).
 * Semantically similar to a klass but restricted to purely virtual method contracts.
 */
class interface : public klass {
protected:
    friend class ns;
    friend class aggregate;
    friend class gen::implementation_generator;
    friend class gen::symbol_resolver;
    friend class gen::type_reference_resolver;

    interface(std::shared_ptr<element> parent) :
        klass(parent) {}

    static std::shared_ptr<interface> make_shared(std::shared_ptr<element> parent, const std::string &name);

public:
    bool is_class() const override { return false; }
    bool is_interface() const { return true; }

    void accept(model_visitor& visitor) override;
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

    /** Set the AST parameter_spec node this parameter was built from. */
    void set_ast_parameter_spec(std::shared_ptr<k::parse::ast::parameter_spec> ast) {
        _ast_node = std::static_pointer_cast<k::parse::ast::ast_node>(std::move(ast));
    }
    /** Get the AST parameter_spec node (may be null). */
    std::shared_ptr<k::parse::ast::parameter_spec> get_ast_parameter_spec() const {
        return get_ast_node_as<k::parse::ast::parameter_spec>();
    }
};

class function : public element, public named_element, public variable_holder {
public:
    /**
     * Aliasing specifier for function declarations.
     * NONE     = regular user-defined body.
     * DEFAULT  = '-> default ;' on constructors (compiler-generated body).
     * DELETE   = '-> delete ;' on constructors (call is forbidden).
     * REDIRECT = '-> target ;' on any function (alias to another function).
     */
    enum class function_aliasing { NONE, DEFAULT, DELETE, REDIRECT };

protected:
    friend class ns;
    friend class aggregate;
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

    /** True if this member function is declared const (this parameter is ref<const T>). */
    bool _is_const_member = false;

    /** True if this function is an operator overload (e.g. operator_add, operator_eq, etc.). */
    bool _is_operator = false;

    /**
     * True if this function is virtual (dispatch through vtable).
     * Set during symbol resolution for non-static, non-private member functions of classes.
     */
    bool _is_virtual = false;

    /**
     * True if this function is declared 'final' as a specifier.
     * - A final virtual function cannot be overridden.
     * - A NEW function declared final is NOT virtual (no vtable slot).
     */
    bool _is_final_func = false;

    /**
     * True if this function is declared 'abstract': it has no body and its class
     * cannot be instantiated unless a derived class provides a concrete override.
     * Only valid for non-static, non-private, non-final member functions of classes.
     */
    bool _is_abstract_func = false;

    /**
     * True if this function is declared 'extern': it has no body, no K mangling,
     * and is resolved at link time from an external (C) library.
     * The symbol name used in the LLVM module is the function's short name (no mangling).
     */
    bool _is_extern = false;

    /**
     * Index of this function's slot in the vtable of its owning class.
     * -1 means "not in any vtable".
     */
    int _vtable_slot = -1;

    /**
     * The function that this function overrides in the parent class's vtable.
     * nullptr if no override.
     */
    std::shared_ptr<function> _overrides = nullptr;

    /**
     * Unresolved target name for REDIRECT aliasing.
     * Set by model_builder, consumed by symbol_resolver.
     */
    k::name _redirect_target_name;

    /**
     * Resolved redirect target function.
     * After chained resolution, this points to the final concrete implementation.
     * Set by symbol_resolver (redirect resolution phase).
     */
    std::shared_ptr<function> _redirect_target = nullptr;

    std::shared_ptr<type> _return_type;
    std::vector<std::shared_ptr<parameter>> _parameters;
    std::shared_ptr<parameter> _this_param;
    std::shared_ptr<block> _block;

    /** Named return variable — when set, NRVO is guaranteed and implicit return is enabled.
     *  The variable_statement is inserted as the first statement in the function's block. */
    std::shared_ptr<variable_statement> _named_return_var;

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

    /** True if this function has a named return variable. */
    bool has_named_return_var() const { return _named_return_var != nullptr; }
    /** Returns the named return variable (nullptr if none). */
    std::shared_ptr<variable_statement> get_named_return_var() const { return _named_return_var; }
    /** Set the named return variable. */
    void set_named_return_var(std::shared_ptr<variable_statement> v) { _named_return_var = std::move(v); }

    bool is_static() const { return _is_static; }
    bool is_compiler_generated() const { return _compiler_generated; }
    void set_compiler_generated(bool v) { _compiler_generated = v; }

    /** True if this member function is declared const (this parameter is ref<const T>). */
    bool is_const_member() const { return _is_const_member; }
    /** Set whether this member function is const. */
    void set_const_member(bool v) { _is_const_member = v; }

    /** True if this function is an operator overload. */
    bool is_operator() const { return _is_operator; }
    /** Set whether this function is an operator overload. */
    void set_operator(bool v) { _is_operator = v; }

    /**
     * Reset the implicit 'this' parameter so that create_this_parameter() will
     * recreate it with the current _is_const_member flag.
     */
    void reset_this_parameter() { _this_param = nullptr; }

    bool is_member() const;
    std::shared_ptr<const aggregate> get_owner() const;
    std::shared_ptr<aggregate> get_owner();

    visibility get_visibility() const { return _visibility; }
    void set_visibility(visibility v) { _visibility = v; }

    /** Returns the aliasing specifier (NONE / DEFAULT / DELETE / REDIRECT). */
    function_aliasing get_aliasing() const { return _aliasing; }
    /** Set the aliasing specifier. */
    void set_aliasing(function_aliasing a) { _aliasing = a; }
    /** True if the constructor was declared with '-> default ;'. */
    bool is_defaulted() const { return _aliasing == function_aliasing::DEFAULT; }
    /** True if the constructor was declared with '-> delete ;'. */
    bool is_deleted() const { return _aliasing == function_aliasing::DELETE; }

    /** True if this function is a redirect (-> target ;). */
    bool is_redirected() const { return _aliasing == function_aliasing::REDIRECT; }

    /** Returns the unresolved redirect target name (set by model_builder). */
    const k::name& get_redirect_target_name() const { return _redirect_target_name; }
    /** Set the unresolved redirect target name. */
    void set_redirect_target_name(const k::name& n) { _redirect_target_name = n; }

    /** Returns the resolved redirect target function (set by symbol_resolver). */
    std::shared_ptr<function> get_redirect_target() const { return _redirect_target; }
    /** Set the resolved redirect target function. */
    void set_redirect_target(std::shared_ptr<function> f) { _redirect_target = std::move(f); }

    /** True if this function is virtual (dispatched through vtable). Set by symbol_resolver. */
    bool is_virtual() const { return _is_virtual; }
    /** Mark this function as virtual (or non-virtual). */
    void set_virtual(bool v) { _is_virtual = v; }

    /** True if this function is declared 'final'. */
    bool is_final_func() const { return _is_final_func; }
    /** Set whether this function is declared 'final'. */
    void set_final_func(bool v) { _is_final_func = v; }

    /**
     * True if this function is declared 'abstract' (has no body; must not be materialized).
     * The owning class must itself be declared abstract.
     */
    bool is_abstract_func() const { return _is_abstract_func; }
    /** Set whether this function is abstract. */
    void set_abstract_func(bool v) { _is_abstract_func = v; }

    /**
     * True if this function is declared 'extern' (no body; resolved at link time
     * from an external C library; symbol name = short name, no K mangling).
     */
    bool is_extern() const { return _is_extern; }
    /** Set whether this function is extern. */
    void set_extern(bool v) { _is_extern = v; }

    /**
     * True if this function is an external import (no body in this module).
     * Overridden by imported_method, imported_function, etc.
     */
    virtual bool is_external() const { return false; }

    /** Vtable slot index (-1 = not in vtable). Set by symbol_resolver. */
    int get_vtable_slot() const { return _vtable_slot; }
    /** Set the vtable slot index. */
    void set_vtable_slot(int slot) { _vtable_slot = slot; }

    /** Returns the function overridden by this one (nullptr = new virtual or non-virtual). */
    std::shared_ptr<function> get_overrides() const { return _overrides; }
    /** Set the function overridden by this one. */
    void set_overrides(std::shared_ptr<function> f) { _overrides = std::move(f); }

    /** Set the AST function_decl node this function was built from. */
    void set_ast_function_decl(std::shared_ptr<k::parse::ast::function_decl> ast) {
        _ast_node = std::static_pointer_cast<k::parse::ast::ast_node>(std::move(ast));
    }
    /** Get the AST function_decl node (may be null). */
    std::shared_ptr<k::parse::ast::function_decl> get_ast_function_decl() const {
        return get_ast_node_as<k::parse::ast::function_decl>();
    }

};

class constructor : public function {
protected:
    friend class aggregate;
    friend class gen::symbol_resolver;
    friend class gen::type_reference_resolver;

public:
    struct member_init_spec {
        std::string member_name;
        std::vector<std::shared_ptr<expression>> args;
        bool is_base_init = false;
        std::shared_ptr<aggregate> base_struct;
    };

protected:
    std::vector<member_init_spec> _member_inits;
    bool _is_copy_constructor = false;

    constructor(std::shared_ptr<aggregate> parent) :
        function(parent) {}

    void update_mangled_name() override;

    static std::shared_ptr<constructor> make_shared(std::shared_ptr<aggregate> parent);

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
    friend class aggregate;
    friend class gen::symbol_resolver;
    friend class gen::type_reference_resolver;

    destructor(std::shared_ptr<aggregate> parent) :
        function(parent) {}

    void update_mangled_name() override;

    static std::shared_ptr<destructor> make_shared(std::shared_ptr<aggregate> parent);

public:
    void accept(model_visitor& visitor) override;
};

/**
 * Static constructor: a static no-argument void function named exactly with the aggregate name.
 * Acts as a class initializer. Its execution is registered in the global initializer function.
 */
class static_constructor : public function {
public:
    /**
     * A dependency declared in the static constructor's mem-init list.
     */
    struct static_dep_spec {
        /// Raw name as written in source — kept for error messages only.
        std::string name;

        /// Resolved target: monostate = not yet resolved (or resolution failed),
        /// shared_ptr<aggregate> = depends on that aggregate's static constructor,
        /// shared_ptr<global_variable_definition> = depends on that global variable.
        std::variant<
            std::monostate,
            std::shared_ptr<aggregate>,
            std::shared_ptr<global_variable_definition>
        > resolved;

        bool is_resolved() const { return resolved.index() != 0; }

        /// True when the dep resolved to an aggregate.
        bool is_structure() const {
            return std::holds_alternative<std::shared_ptr<aggregate>>(resolved);
        }
        /// True when the dep resolved to a global variable.
        bool is_global_variable() const {
            return std::holds_alternative<std::shared_ptr<global_variable_definition>>(resolved);
        }

        /// Returns the resolved aggregate (nullptr if not an aggregate dep).
        std::shared_ptr<aggregate> get_structure() const {
            auto* p = std::get_if<std::shared_ptr<aggregate>>(&resolved);
            return p ? *p : nullptr;
        }
        /// Returns the resolved global variable (nullptr if not a global-var dep).
        std::shared_ptr<global_variable_definition> get_global_variable() const {
            auto* p = std::get_if<std::shared_ptr<global_variable_definition>>(&resolved);
            return p ? *p : nullptr;
        }
    };

protected:
    friend class aggregate;
    friend class gen::symbol_resolver;
    friend class gen::init_order_resolver;

    std::vector<static_dep_spec> _static_deps;

    static_constructor(std::shared_ptr<aggregate> parent) :
        function(parent, true) {}

    void update_mangled_name() override;

    static std::shared_ptr<static_constructor> make_shared(std::shared_ptr<aggregate> parent);

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
 * Static destructor: a static no-argument void function named with "~" + aggregate name.
 * Acts as a class finalizer. Its execution is registered in the global finalizer function.
 */
class static_destructor : public function {
protected:
    friend class aggregate;
    friend class gen::symbol_resolver;

    static_destructor(std::shared_ptr<aggregate> parent) :
        function(parent, true) {}

    void update_mangled_name() override;

    static std::shared_ptr<static_destructor> make_shared(std::shared_ptr<aggregate> parent);

public:
    void accept(model_visitor& visitor) override;
};


/**
 * An "init item" is one node in the global initialization/finalization graph.
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
    friend class aggregate;
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


class ns : public element, public named_element, public variable_holder, public function_holder, public aggregate_holder, public enum_holder, public using_holder {
protected:

    friend class unit;

    /** Collection of all children of this namespace. */
    std::vector<std::shared_ptr</*ns_element*/element>> _children;

    /** Map of direct child namespaces. */
    std::map<std::string, std::shared_ptr<ns>> _ns;

    ns(std::shared_ptr<element> parent):
        element(parent) {}

    static std::shared_ptr<ns> make_shared(std::shared_ptr<element> parent, const std::string& name);

    std::shared_ptr<variable_definition> do_create_variable(const std::string &name, bool is_static) override;
    void on_variable_defined(std::shared_ptr<variable_definition>) override;

    std::shared_ptr<function> do_create_function(const std::string &name, bool is_static) override;
    void on_function_defined(std::shared_ptr<function> func) override;

    std::shared_ptr<structure> do_create_structure(const std::string &name) override;
    std::shared_ptr<klass> do_create_class(const std::string &name) override;
    std::shared_ptr<interface> do_create_interface(const std::string &name) override;
    void on_aggregate_defined(std::shared_ptr<aggregate>) override;

    std::shared_ptr<enumeration> do_create_enum(const std::string &name) override;
    void on_enum_defined(std::shared_ptr<enumeration>) override;

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

    /** Declared imports (populated by model_builder, resolved by kdi_importer). */
    std::vector<imported_module> _imported_modules;

    /**
     * KDI files loaded as transitive dependencies (not direct imports of this unit).
     * Populated by kdi_importer::import_all() to allow find_imported_type() to
     * resolve types from indirectly-imported modules (e.g. base classes).
     */
    std::vector<std::shared_ptr<kdi::kdi_file>> _transitive_kdis;

    /**
     * Cache of imported_function model nodes keyed by mangled name (C1 for ctors).
     * Created lazily by get_or_create_imported_function().
     */
    std::unordered_map<std::string, std::shared_ptr<imported_function>>  _imported_functions;

    /**
     * Cache of imported_aggregate model nodes keyed by fully-qualified K name.
     * Created lazily by get_or_create_imported_aggregate().
     */
    std::unordered_map<std::string, std::shared_ptr<imported_aggregate>> _imported_aggregates;

    /**
     * Cache of imported_variable model nodes keyed by mangled name.
     * Created lazily by get_or_create_imported_variable().
     */
    std::unordered_map<std::string, std::shared_ptr<imported_variable>>  _imported_variables;

    /**
     * Cache of imported enumeration model nodes keyed by fully-qualified K name.
     * Created lazily by get_or_create_imported_enum().
     */
    std::unordered_map<std::string, std::shared_ptr<enumeration>>        _imported_enums;

    std::shared_ptr<global_main_function> _global_main_func;

    friend class k::model::gen::symbol_resolver;
    friend class k::model::gen::aggregate_type_resolver;
    friend class k::model::gen::model_materializer;
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

    /**
     * Register an import declaration (called by model_builder).
     * @param module_name  Qualified name of the module to import.
     */
    void add_import(const k::name& module_name);

    /**
     * Read-only access to all declared imports.
     */
    const std::vector<imported_module>& get_imports() const {
        return _imported_modules;
    }

    /**
     * Mutable access to all declared imports (used by kdi_importer to fill
     * in resolved_kdi_path, kdi and used fields).
     */
    std::vector<imported_module>& get_imports() {
        return _imported_modules;
    }

    /**
     * Find an import by module name.
     * @return Pointer to the matching entry, or nullptr if not found.
     */
    imported_module* find_import(const k::name& module_name);
    const imported_module* find_import(const k::name& module_name) const;

    /**
     * Register a KDI file loaded as a transitive dependency (not a direct
     * import of this unit).  Called by kdi_importer so that find_imported_type()
     * and friends can resolve symbols from indirectly-imported modules.
     */
    void add_transitive_kdi(std::shared_ptr<kdi::kdi_file> kdi_ptr) {
        if (kdi_ptr) _transitive_kdis.push_back(std::move(kdi_ptr));
    }

    /** Read-only access to the list of transitive KDIs. */
    const std::vector<std::shared_ptr<kdi::kdi_file>>& get_transitive_kdis() const {
        return _transitive_kdis;
    }

    // ── Cross-module symbol lookup ──────────────────────────────────────────
    //
    // These methods search ALL loaded imports for a symbol identified by its
    // qualified K name (e.g. name{"math", "vec", "dot"}).
    //
    // When a match is found, the owning imported_module is marked used=true.
    // The current unit's own namespace is NOT searched here — callers are
    // expected to try local resolution first.
    //
    // Return nullptr when no import contains the requested symbol.

    /**
     * Find a global/namespace-level function in any loaded import.
     * @param name  Qualified name of the function (without root prefix).
     * @return Pointer into the kdi_file's kdi_function entry, or nullptr.
     */
    const kdi::kdi_function*
    find_imported_function(const k::name& name);

    /**
     * Find a global/static variable in any loaded import.
     * @param name  Qualified name of the variable (without root prefix).
     * @return Pointer into the kdi_file's kdi_variable entry, or nullptr.
     */
    const kdi::kdi_variable*
    find_imported_variable(const k::name& name);

    /**
     * Find an aggregate type (struct/class/interface) in any loaded import.
     * @param name  Qualified name of the aggregate (without root prefix).
     * @return Pointer into the kdi_file's kdi_aggregate entry, or nullptr.
     */
    const kdi::kdi_aggregate*
    find_imported_type(const k::name& name);

    /**
     * Find an enum type in any loaded import.
     * @param name  Qualified name of the enum (without root prefix).
     * @return Pointer into the kdi_file's kdi_enum entry, or nullptr.
     */
    const kdi::kdi_enum*
    find_imported_enum(const k::name& name);

    // ── Imported model-node factory methods ─────────────────────────────────
    //
    // Each method returns (or retrieves from cache) a fully-built model node
    // for the corresponding KDI descriptor.  Signatures and types are resolved
    // using kdi_type_to_model_type(); the nodes have no body / initialiser.
    // All side-effects (struct_type registration in context, marking
    // imported_module::used) happen here.

    /**
     * Return (or create) the imported_function model node for @p kdi_fn.
     * Keyed by mangled_name.  Populates return type and parameter list.
     */
    std::shared_ptr<imported_function>
    get_or_create_imported_function(const kdi::kdi_function* kdi_fn,
                                    std::shared_ptr<context> ctx);

    /**
     * Return (or create) the imported_aggregate model node for the aggregate
     * identified by its fully-qualified K name @p fq_name.
     *
     * Searches all loaded imports; builds the LLVM StructType from the KDI
     * layout; materialises all public/protected members, methods (as
     * imported_method), constructors (as imported_constructor) and destructor
     * (as imported_destructor).
     */
    std::shared_ptr<imported_aggregate>
    get_or_create_imported_aggregate(const k::name& fq_name,
                                     std::shared_ptr<context> ctx);

    /**
     * Return (or create) the imported_variable model node for @p kdi_var.
     * Keyed by mangled_name.  Resolves the variable type.
     */
    std::shared_ptr<imported_variable>
    get_or_create_imported_variable(const kdi::kdi_variable* kdi_var,
                                    std::shared_ptr<context> ctx);

    /**
     * Return (or create) an imported enumeration model node for the enum
     * identified by its fully-qualified K name @p fq_name.
     *
     * Searches all loaded imports; builds the underlying type, entries,
     * and registers the enum_type in context::add_enum().
     */
    std::shared_ptr<enumeration>
    get_or_create_imported_enum(const k::name& fq_name,
                                std::shared_ptr<context> ctx);

    // ── Accessors ────────────────────────────────────────────────────────────

    const std::unordered_map<std::string, std::shared_ptr<imported_function>>&
    get_imported_functions() const { return _imported_functions; }

    const std::unordered_map<std::string, std::shared_ptr<imported_aggregate>>&
    get_imported_aggregates() const { return _imported_aggregates; }

    const std::unordered_map<std::string, std::shared_ptr<imported_variable>>&
    get_imported_variables() const { return _imported_variables; }


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
