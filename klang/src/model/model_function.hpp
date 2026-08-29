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
#ifndef KLANG_MODEL_FUNCTION_HPP
#define KLANG_MODEL_FUNCTION_HPP
#include "model_aggregate.hpp"
namespace k::model {

class parameter : public element, public variable_definition, public annotation_holder {
protected:

    friend class function;
    friend class gen::implementation_generator;

    std::shared_ptr<function> _function;

    size_t _pos;

    /** Optional default value expression for this parameter (may be nullptr). */
    std::shared_ptr<expression> _default_expr;

    /**
     * True if this parameter carries @k::ffi::CString and should be treated
     * as a C `char*` (null-terminated string pointer) in FFI calls.
     * Set by symbol_resolver when processing @ffi::CString annotations.
     */
    bool _is_ffi_cstring = false;

    /** True if this parameter was declared with '...' (varargs). Informational flag. */
    bool _is_varargs = false;

    /** True if this parameter is a pack expansion (e.g. Ts... args in a template function). */
    bool _is_pack_expansion = false;

    /** Name of the template parameter pack this expansion refers to (e.g. "Ts"). */
    std::string _pack_param_name;

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

    /** True if this parameter is marked @ffi::CString for C FFI. */
    bool is_ffi_cstring() const { return _is_ffi_cstring; }
    /** Mark this parameter as @ffi::CString. */
    void set_ffi_cstring(bool v) { _is_ffi_cstring = v; }

    /** True if this parameter was declared with '...' (varargs). */
    bool is_varargs() const { return _is_varargs; }
    /** Mark this parameter as varargs. */
    void set_varargs(bool v) { _is_varargs = v; }

    /** True if this parameter is a pack expansion (Ts... args). */
    bool is_pack_expansion() const { return _is_pack_expansion; }
    /** Mark this parameter as a pack expansion. */
    void set_pack_expansion(bool v) { _is_pack_expansion = v; }

    /** Get the name of the template parameter pack this refers to. */
    const std::string& pack_param_name() const { return _pack_param_name; }
    /** Set the name of the template parameter pack. */
    void set_pack_param_name(const std::string& name) { _pack_param_name = name; }

    /** Set the AST parameter_spec node this parameter was built from. */
    void set_ast_parameter_spec(std::shared_ptr<k::parse::ast::parameter_spec> ast) {
        _ast_node = std::static_pointer_cast<k::parse::ast::ast_node>(std::move(ast));
    }
    /** Get the AST parameter_spec node (may be null). */
    std::shared_ptr<k::parse::ast::parameter_spec> get_ast_parameter_spec() const {
        return get_ast_node_as<k::parse::ast::parameter_spec>();
    }
};

class function : public element, public named_element, public variable_holder, public annotation_holder {
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
    friend class gen::declaration_generator;
    friend class gen::implementation_generator;
    friend class gen::symbol_resolver;
    friend class gen::type_reference_resolver;
    friend class gen::aggregate_type_resolver;
    friend class gen::signature_resolver;
    friend class template_instantiator;

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
     * True if this function is declared 'override' by the user.
     * An override specifier asserts that this function overrides an inherited
     * virtual slot. If no inherited slot exists, it is a compilation error.
     * Conversely, overriding without this specifier emits a warning.
     */
    bool _is_override_specifier = false;

    /**
     * True if this function is an interface default method: a concrete member
     * function declared with the 'default' prefix specifier inside an interface.
     * Such a method has a body, is virtual (participates in the vtable) and is
     * NOT abstract. Derived classes that do not override it inherit its slot.
     */
    bool _is_default_method = false;

    /**
     * True if this function is an FFI extern function: it has no body, no K mangling,
     * and is resolved at link time from an external library.
     * Set automatically by set_extern_c_symbol().
     */
    bool _is_extern = false;

    /**
     * The C symbol name to use for this extern function.
     * When set (via @k::ffi::Extern annotation), _is_extern is also set to true.
     * If empty, the function's short name is used.
     */
    std::optional<std::string> _extern_c_symbol;

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
    bool _has_explicit_return_type = false;
    bool _is_lambda = false;
    bool _has_bare_return = false;
    std::vector<std::shared_ptr<parameter>> _parameters;
    std::shared_ptr<parameter> _this_param;
    std::shared_ptr<block> _block;

    /** Named return variable — when set, NRVO is guaranteed and implicit return is enabled.
     *  The variable_statement is inserted as the first statement in the function's block. */
    std::shared_ptr<variable_statement> _named_return_var;

    /**
     * Template information for this function (non-null only if this is a template definition).
     * Holds parameter descriptors, the original AST, and the instantiation cache.
     */
    std::unique_ptr<tpl_info> _tpl_info;

    /**
     * Template instantiation metadata (set only on concrete instantiations, NOT on template definitions).
     * _tpl_base_name is the original template name (e.g. "identity"), _tpl_args are the concrete arguments.
     * These are used by the mangler to produce the correct I…E encoding.
     */
    std::string _tpl_base_name;
    std::vector<template_argument> _tpl_args;

    /**
     * True when this function is part of a synthesised template instantiation
     * (free function-template instantiation, or a method/ctor/dtor of an
     * instantiated aggregate). Drives linkonce_odr + COMDAT linkage in codegen.
     */
    bool _is_instantiation = false;

     /**
      * Type substitution map used when this function was instantiated from a template
      * (e.g. {"R"→int, "E"→int} for expected__int_int).  Set by
      * template_instantiator::populate_function_from_template() on the concrete function.
      * Used by type_reference_resolver::try_instantiate_template_type() to resolve
      * template-parameter names embedded in AST template arg lists (e.g. "R" in
      * Expected<R,E>) that are no longer in scope in the concrete function body.
      */
    std::unordered_map<std::string, std::shared_ptr<type>> _tpl_instantiation_subst;

    /** Raw unresolved exception type names from parser (consumed during resolution). */
    std::vector<std::string> _throws_spec_raw;

    /** Resolved exception types that this function may throw. Empty = noexcept. */
    std::vector<std::shared_ptr<type>> _throws_spec;

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

    bool has_explicit_return_type() const { return _has_explicit_return_type; }
    void set_has_explicit_return_type(bool v) { _has_explicit_return_type = v; }

    bool is_lambda() const { return _is_lambda; }
    void set_lambda(bool v) { _is_lambda = v; }

    bool has_bare_return() const { return _has_bare_return; }
    void set_has_bare_return(bool v) { _has_bare_return = v; }

    // ── Exception specification ──────────────────────────────────────────────
    /** True if this function has an explicit throws clause (even if empty after resolution). */
    bool has_throws_spec() const { return !_throws_spec_raw.empty() || !_throws_spec.empty(); }
    /** True if this function is guaranteed not to throw (no throws clause). */
    bool is_noexcept() const { return _throws_spec_raw.empty() && _throws_spec.empty(); }
    /** Raw unresolved exception type names (set by model builder, consumed by resolver). */
    const std::vector<std::string>& get_throws_spec_raw() const { return _throws_spec_raw; }
    void add_throws_spec_raw(const std::string& name) { _throws_spec_raw.push_back(name); }
    /** Resolved exception types. */
    const std::vector<std::shared_ptr<type>>& get_throws_spec() const { return _throws_spec; }
    void add_throws_type(std::shared_ptr<type> t) { _throws_spec.push_back(std::move(t)); }

    const std::vector<std::shared_ptr<parameter>>& parameters() const {
        return _parameters;
    }

    std::shared_ptr<variable_definition> append_variable(const std::string& name, bool is_static) override;

    std::shared_ptr<parameter> append_parameter(const std::string& name, std::shared_ptr<type> type);
    std::shared_ptr<parameter> insert_parameter(const std::string& name, std::shared_ptr<type> type, size_t pos);

    size_t get_parameter_size() const {return _parameters.size();}
    bool has_parameter()const {return !_parameters.empty();}
    /** True if the last parameter is a varargs parameter. */
    bool has_varargs() const {
        return !_parameters.empty() && _parameters.back()->is_varargs();
    }
    std::shared_ptr<parameter> get_parameter(size_t index);
    std::shared_ptr<const parameter> get_parameter(size_t index)const;

    std::shared_ptr<parameter> get_parameter(const std::string& name);
    std::shared_ptr<const parameter> get_parameter(const std::string& name)const;

    std::shared_ptr<parameter> get_this_parameter() const {
        return _this_param;
    }

    void set_block(const std::shared_ptr<block>& block);
    std::shared_ptr<block> get_block();
    std::shared_ptr<block> get_existing_block();
    std::shared_ptr<const block> get_existing_block() const;

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

    /** True if this function is declared 'override' by the user. */
    bool is_override_specifier() const { return _is_override_specifier; }
    /** Set whether this function has the 'override' specifier. */
    void set_override_specifier(bool v) { _is_override_specifier = v; }

    /**
     * True if this function is an interface default method (declared with the
     * 'default' prefix specifier inside an interface). It is concrete and virtual.
     */
    bool is_default_method() const { return _is_default_method; }
    /** Set whether this function is an interface default method. */
    void set_default_method(bool v) { _is_default_method = v; }

    /**
     * True if this function is an FFI extern function (resolved at link time
     * from an external library; no body, no K mangling).
     */
    bool is_extern() const { return _is_extern; }

    /**
     * Returns the C symbol name for this extern function.
     * If set, this is the exact symbol name to use.
     * If not set but is_extern() is true, the short name should be used.
     */
    const std::optional<std::string>& get_extern_c_symbol() const { return _extern_c_symbol; }

    /**
     * Mark this function as FFI extern with the given C symbol name.
     * Also sets _is_extern = true.
     */
    void set_extern_c_symbol(const std::string& sym) {
        _extern_c_symbol = sym;
        _is_extern = true;
    }

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

    //
    // Template support
    //

    /** True if this function is a template definition (has template parameters). */
    bool is_template() const { return _tpl_info != nullptr; }

    /**
     * True if this function is a generic definition (declared with 'generic' keyword).
     * Implies is_template() == true.
     */
    bool is_generic() const { return _tpl_info != nullptr && _tpl_info->is_generic; }

    /** Returns the template info (nullptr if not a template). */
    tpl_info* get_tpl_info() const { return _tpl_info.get(); }

    /** Set the template info (takes ownership). */
    void set_tpl_info(std::unique_ptr<tpl_info> ti) { _tpl_info = std::move(ti); }

    /** True if this function is a concrete template instantiation (has template args). */
    bool has_tpl_args() const { return !_tpl_base_name.empty(); }

    /** Returns the original template base name (e.g. "identity" for identity<int>). Empty if not an instantiation. */
    const std::string& get_tpl_base_name() const { return _tpl_base_name; }

    /** Returns the concrete template arguments used to instantiate this function. */
    const std::vector<template_argument>& get_tpl_args() const { return _tpl_args; }

    /**
     * True if this function was instantiated from a template and carries a type substitution map.
     * Used by type_reference_resolver to resolve template-param names embedded in
     * inner AST template arg type specs (e.g. "R" in Expected<R,E>).
     */
    bool has_tpl_instantiation_subst() const { return !_tpl_instantiation_subst.empty(); }

    /** Returns the type substitution map for this template instantiation (may be empty). */
    const std::unordered_map<std::string, std::shared_ptr<type>>& get_tpl_instantiation_subst() const {
        return _tpl_instantiation_subst;
    }

    /** Set the type substitution map (called by template_instantiator). */
    void set_tpl_instantiation_subst(std::unordered_map<std::string, std::shared_ptr<type>> s) {
        _tpl_instantiation_subst = std::move(s);
    }

    /** Set the template instantiation metadata (base name + concrete args). */
    void set_tpl_instantiation_info(const std::string& base_name, std::vector<template_argument> args) {
        _tpl_base_name = base_name;
        _tpl_args = std::move(args);
    }

    /**
     * True if this function belongs to (or is) a synthesised template instantiation
     * — covers free function-template instantiations and methods of instantiated
     * aggregates (generic type-erased or concrete). Used by the code generator to
     * apply linkonce_odr + COMDAT linkage. Set by template_instantiator.
     */
    bool is_instantiation() const { return _is_instantiation; }

    /** Mark this function as belonging to a synthesised template instantiation. */
    void mark_instantiation() { _is_instantiation = true; }

};

class constructor : public function {
protected:
    friend class aggregate;
    friend class model_builder;
    friend class gen::symbol_resolver;
    friend class gen::type_reference_resolver;
    friend class template_instantiator;

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
    // Guards template_instantiator::inject_constructor_member_inits() against
    // being invoked more than once for the same (freshly-instantiated)
    // constructor. Template instantiation can be triggered from multiple,
    // independent resolver entry points (aggregate_type_resolver's own
    // instantiation path and type_reference_resolver's on-demand path) that
    // both resolve to the SAME cached concrete aggregate/constructor; without
    // this guard, each trigger would re-inject a duplicate set of base/vbase
    // constructor-call statements into the constructor's body. If one such
    // duplicate injection happens after the aggregate's one-shot
    // resolve_instantiated_aggregate() visit already ran, its statements are
    // never type-resolved, leaving a null constructor at codegen time (crashes
    // with "LLVM declaration not found for constructor of type ...").
    bool _base_inits_injected = false;

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

    /** True once template_instantiator::inject_constructor_member_inits() has run for this constructor. */
    bool are_base_inits_injected() const { return _base_inits_injected; }
    void set_base_inits_injected(bool v) { _base_inits_injected = v; }
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
    /** True when the user's main() has a `const String[]` args parameter. */
    bool _has_args = false;

    friend class unit;
    global_main_function(std::shared_ptr<element> parent, std::shared_ptr<function> real_main_func);

public:
    void accept(model_visitor& visitor) override;

    void update_mangled_name() override;

    function& get_real_func() {return *_real_main_func;}

    bool has_args() const { return _has_args; }
    void set_has_args(bool v) { _has_args = v; }
};


} // namespace k::model

#endif //KLANG_MODEL_FUNCTION_HPP
