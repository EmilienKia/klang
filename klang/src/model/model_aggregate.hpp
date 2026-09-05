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
#ifndef KLANG_MODEL_AGGREGATE_HPP
#define KLANG_MODEL_AGGREGATE_HPP
#include "model_enum.hpp"
namespace k::model {

class member_variable_definition : public element, public variable_definition {
protected:

    friend class aggregate;
    friend class structure;
    friend class klass;
    friend class annotation_type;
    friend class gen::implementation_generator;
    friend class gen::symbol_resolver;
    friend class gen::declaration_generator;
    friend class gen::aggregate_type_resolver;
    friend class gen::type_reference_resolver;
    friend class template_instantiator;

    /** Declared visibility of this member variable. PUBLIC by default. */
    visibility _visibility = PUBLIC;

    member_variable_definition(std::shared_ptr<aggregate> st);

    static std::shared_ptr<member_variable_definition> make_shared(std::shared_ptr<aggregate> st, const std::string &name);

    void update_mangled_name() override;

public:
    void accept(model_visitor& visitor) override;

    visibility get_visibility() const { return _visibility; }
    void set_visibility(visibility v) { _visibility = v; }

    void set_ast_variable_decl(std::shared_ptr<k::parse::ast::variable_decl> ast);
    std::shared_ptr<k::parse::ast::variable_decl> get_ast_variable_decl() const;
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
class aggregate : public element, public named_element, public variable_holder, public function_holder, public aggregate_holder, public enum_holder, public union_holder, public using_holder, public alias_holder, public friend_holder, public annotation_holder {
protected:
    friend class ns;
    friend class model_builder;
    friend class gen::implementation_generator;
    friend class gen::symbol_resolver;
    friend class gen::type_reference_resolver;
    friend class gen::aggregate_type_resolver;
    friend class template_instantiator;

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

    /**
     * Template information for this aggregate (non-null only if this is a template definition).
     * Holds parameter descriptors, the original AST, and the instantiation cache.
     */
    std::unique_ptr<tpl_info> _tpl_info;

    /**
     * Template instantiation metadata (set only on concrete instantiations, NOT on template definitions).
     * _tpl_base_name is the original template name (e.g. "Box"), _tpl_args are the concrete arguments.
     * These are used by the mangler to produce the correct I…E encoding.
     */
    std::string _tpl_base_name;
    std::vector<template_argument> _tpl_args;

    /**
     * True when this aggregate is a template instantiation synthesised by
     * template_instantiator (either the type-erased "generic" model — which keeps
     * the base name and has no tpl args — or the per-arg "concrete" model). Used by
     * the code generator to apply linkonce_odr + COMDAT linkage so identical
     * instantiations are merged across translation units / modules.
     */
    bool _is_instantiation = false;

    aggregate(std::shared_ptr<element> parent) :
        element(parent) {}

    std::shared_ptr<variable_definition> do_create_variable(const std::string &name, bool is_static) override;
    void on_variable_defined(std::shared_ptr<variable_definition>) override;

    std::shared_ptr<function> do_create_function(const std::string &name, bool is_static) override;
    void on_function_defined(std::shared_ptr<function>) override;
    void on_function_removed(const std::shared_ptr<function>&) override;

    std::shared_ptr<structure> do_create_structure(const std::string &name) override;
    std::shared_ptr<klass> do_create_class(const std::string &name) override;
    std::shared_ptr<interface> do_create_interface(const std::string &name) override;
    std::shared_ptr<annotation_type> do_create_annotation(const std::string &name) override;
    void on_aggregate_defined(std::shared_ptr<aggregate>) override;

    std::shared_ptr<enumeration> do_create_enum(const std::string &name) override;
    void on_enum_defined(std::shared_ptr<enumeration>) override;

    std::shared_ptr<union_type_def> do_create_union(const std::string &name) override;
    void on_union_defined(std::shared_ptr<union_type_def>) override;

    void set_struct_type(const std::shared_ptr<struct_type>& st_type) {
        _type = st_type;
    }

public:

    void update_mangled_name() override;

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

    /** True if this is a struct (keyword 'struct'). */
    virtual bool is_struct() const { return false; }

    /** True if this is a class (keyword 'class'), false if it is a struct (keyword 'struct'). */
    virtual bool is_class() const { return false; }

    /**
     * True if this is an interface (keyword 'interface'). Interfaces are stateless
     * (vtable-only, no data members), so diamond convergence purely within an
     * interface hierarchy carries no ABI ambiguity: any two paths to the same
     * interface base always refer to the very same (shared, virtual-like) slot.
     */
    virtual bool is_interface() const { return false; }

    /** True if this is an annotation type (keyword 'annotation'). */
    virtual bool is_annotation() const { return false; }

    /**
     * True if this aggregate was materialised from an imported KDI module
     * (as opposed to being declared in the current compilation unit).
     *
     * Imported aggregates have an ALREADY-COMPILED LLVM struct layout and
     * machine code (in their originating shared/static library), frozen at
     * that library's own compile time. Diamond (virtual-base) detection must
     * never retroactively mark one of an imported aggregate's OWN base_spec
     * edges as virtual purely because some later, unrelated compilation
     * combines it into a new diamond: doing so would silently change how
     * this compilation lays out/accesses that imported aggregate's members
     * relative to what its originating library actually compiled, causing
     * an ABI mismatch (wrong offsets, corrupted vtable pointers, crashes).
     * See aggregate_type_resolver/klass::compute_virtual_bases_for().
     */
    virtual bool is_imported() const { return false; }

    /**
     * Check whether this annotation type has @Retention(Policy::SOURCE).
     * Returns true if the type explicitly specifies SOURCE retention.
     * Returns false (RUNTIME) if @Retention is absent or set to RUNTIME.
     * For non-annotation aggregates, always returns false.
     */
    virtual bool is_source_retention() const { return false; }

    /**
     * True if this aggregate has at least one virtual function (needs a vtable).
     * Kept virtual on aggregate for generic call sites (e.g. virtual dispatch check
     * in gen_expressions.cpp) that hold a shared_ptr<aggregate> without knowing
     * the concrete type.
     */
    /**
     * True if this aggregate carries RTTI (vtable slot 0) and therefore supports
     * dynamic downcast.  Currently classes, interfaces, and annotation types have
     * RTTI; structs do not.
     */
    virtual bool has_rtti() const { return false; }

    /**
     * Return the primary vtable layout (RTTI + virtual slots).
     * Only valid for aggregates with has_rtti() == true.
     * Default returns nullptr.
     */
    virtual std::shared_ptr<vtable_layout> get_vtable() const { return nullptr; }

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
    std::shared_ptr<destructor> create_destructor();

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

    //
    // Template support
    //

    /** True if this aggregate is a template definition (has template parameters). */
    bool is_template() const { return _tpl_info != nullptr; }

    /**
     * True if this aggregate is a generic definition (declared with 'generic' keyword).
     * Implies is_template() == true.
     */
    bool is_generic() const { return _tpl_info != nullptr && _tpl_info->is_generic; }

    /** Returns the template info (nullptr if not a template). */
    tpl_info* get_tpl_info() const { return _tpl_info.get(); }

    /** Set the template info (takes ownership). */
    void set_tpl_info(std::unique_ptr<tpl_info> ti) { _tpl_info = std::move(ti); }

    /** True if this aggregate is a concrete template instantiation (has template args). */
    bool has_tpl_args() const { return !_tpl_base_name.empty(); }

    /** Returns the original template base name (e.g. "Box" for Box<int>). Empty if not an instantiation. */
    const std::string& get_tpl_base_name() const { return _tpl_base_name; }

    /** Returns the concrete template arguments used to instantiate this aggregate. */
    const std::vector<template_argument>& get_tpl_args() const { return _tpl_args; }

    /** Set the template instantiation metadata (base name + concrete args). */
    void set_tpl_instantiation_info(const std::string& base_name, std::vector<template_argument> args) {
        _tpl_base_name = base_name;
        _tpl_args = std::move(args);
    }

    /**
     * True if this aggregate is a template instantiation synthesised by the
     * template_instantiator (covers both the generic type-erased model and the
     * concrete per-arg model). Distinct from has_tpl_args(), which is false for
     * generic synthesised aggregates.
     */
    bool is_instantiation() const { return _is_instantiation; }

    /** Mark this aggregate as a synthesised template instantiation. */
    void mark_instantiation() { _is_instantiation = true; }
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
    bool is_struct() const override { return true; }
    bool is_class() const override { return false; }

    void accept(model_visitor& visitor) override;

    /** Returns the direct enclosing structure, or nullptr if not nested in a struct. */
    std::shared_ptr<structure> get_enclosing_structure() const {
        return std::dynamic_pointer_cast<structure>(get_enclosing_aggregate());
    }
};

/**
 * Annotation type: concrete aggregate declared with the 'annotation' keyword.
 *
 * Annotations inherit directly from aggregate (not from klass or structure).
 * They have their own vtable/vptr infrastructure for RTTI type resolution
 * (like classes), but their member functions are never virtual.
 * Members are public by default (like structs), inheritance is by aggregation
 * (non-virtual). Instances are synthesised at compilation time and attached
 * to model elements (classes and interfaces only).
 */
class annotation_type : public aggregate {
protected:
    friend class ns;
    friend class aggregate;
    friend class gen::implementation_generator;
    friend class gen::symbol_resolver;
    friend class gen::type_reference_resolver;
    friend class gen::declaration_generator;

    /**
     * Primary vtable layout for this annotation type.
     * Contains only the RTTI slot (slot 0) — no user-level virtual functions.
     */
    std::shared_ptr<vtable_layout> _vtable;

    /**
     * Synthetic vptr member variable (__vptr__).
     */
    std::shared_ptr<member_variable_definition> _vptr;

    annotation_type(std::shared_ptr<element> parent) :
        aggregate(parent) {}

    static std::shared_ptr<annotation_type> make_shared(std::shared_ptr<element> parent, const std::string &name);

public:
    bool has_rtti() const override { return _vtable != nullptr; }
    bool is_annotation() const override { return true; }
    bool has_vtable() const override { return _vtable != nullptr; }
    std::shared_ptr<vtable_layout> get_vtable() const override { return _vtable; }

    /**
     * Check whether this annotation type has @Retention(Policy::SOURCE).
     * Returns true if the type explicitly specifies SOURCE retention.
     * Returns false (RUNTIME) if @Retention is absent or set to RUNTIME.
     */
    bool is_source_retention() const override;

    void set_vtable(std::shared_ptr<vtable_layout> vt) { _vtable = std::move(vt); }

    std::shared_ptr<member_variable_definition> get_vptr() const { return _vptr; }

    std::shared_ptr<member_variable_definition> inject_vptr_field(const std::string& field_name) {
        auto vptr_field = member_variable_definition::make_shared(shared_as<aggregate>(), field_name);
        _vptr = vptr_field;
        _vars.insert({field_name, vptr_field});
        _children.insert(_children.begin(), vptr_field);
        return vptr_field;
    }

    void accept(model_visitor& visitor) override;
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
    bool has_rtti() const override { return _vtable != nullptr; }
    bool is_class() const override { return true; }
    bool has_vtable() const override { return _vtable != nullptr; }

    void accept(model_visitor& visitor) override;

    std::shared_ptr<vtable_layout> get_vtable() const override { return _vtable; }

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

    /**
     * Single-aggregate variant of compute_virtual_bases(): detects diamond
     * patterns reachable from 'agg' only and marks the relevant base_spec
     * entries as virtual. Used right after an aggregate's own bases have been
     * resolved (gen_struct.cpp, symbol_resolver::visit_aggregate), so that
     * diamonds only reachable through template-instantiated bases (whose
     * concrete aggregate did not exist yet when the early global
     * compute_virtual_bases() prepass ran) are still correctly detected.
     * Idempotent: safe to call multiple times / in addition to the global pass.
     */
    static void compute_virtual_bases_single(aggregate& agg);

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
    bool is_interface() const override { return true; }

    void accept(model_visitor& visitor) override;
};


} // namespace k::model

#endif //KLANG_MODEL_AGGREGATE_HPP
