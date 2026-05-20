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
#ifndef KLANG_MODEL_UNION_HPP
#define KLANG_MODEL_UNION_HPP
#include "model_aggregate.hpp"
#include "template.hpp"
namespace k::model {

/**
 * Describes a single alternative (member) within a union definition.
 */
struct union_alternative {
    /** Name of this alternative (e.g. "first", "second"). */
    std::string name;
    /** Resolved type for this alternative (set during type resolution). */
    std::shared_ptr<type> resolved_type;
    /** Raw (unresolved) type name from the AST. */
    std::string raw_type_name;
    /** True if this alternative is declared const. */
    bool is_const = false;
    /** Zero-based index (also the discriminant value). */
    size_t index = 0;
};

/**
 * Union type definition: a discriminated (tagged) union.
 *
 * A union holds one active alternative at a time, identified by a hidden
 * uint32_t discriminant field. Member access performs a runtime check.
 *
 * Memory layout: { uint32_t discriminant, [max_size x i8] storage }
 * (with appropriate alignment padding).
 */
class union_type_def : public element, public named_element {
protected:
    friend class ns;
    friend class aggregate;
    friend class gen::implementation_generator;
    friend class gen::symbol_resolver;
    friend class gen::type_reference_resolver;
    friend class gen::aggregate_type_resolver;
    friend class gen::declaration_generator;
    friend class template_instantiator;

    /** All alternatives in declaration order. */
    std::vector<union_alternative> _alternatives;

    /** The LLVM struct type representing this union (discriminant + storage). */
    std::shared_ptr<struct_type> _type;

    /** Declared visibility. */
    visibility _visibility = PUBLIC;

    /** Template information (nullptr if this is not a template union). */
    std::unique_ptr<tpl_info> _tpl_info;

    /** For concrete instantiations of template unions: original template base name. */
    std::string _tpl_base_name;

    /** For concrete instantiations: the concrete template arguments used. */
    std::vector<template_argument> _tpl_args;

    union_type_def(std::shared_ptr<element> parent)
        : element(parent) {}

    static std::shared_ptr<union_type_def> make_shared(std::shared_ptr<element> parent, const std::string& name);

    void update_mangled_name() override;

public:
    void accept(model_visitor& visitor) override;

    //
    // Template
    //

    /** True if this is a template union definition (not yet instantiated). */
    bool is_template() const { return _tpl_info != nullptr; }

    /** True if this template uses the 'generic' keyword. */
    bool is_generic() const { return _tpl_info && _tpl_info->is_generic; }

    /** Get template info (nullptr if not a template). */
    tpl_info* get_tpl_info() const { return _tpl_info.get(); }

    /** Set template info. */
    void set_tpl_info(std::unique_ptr<tpl_info> ti) { _tpl_info = std::move(ti); }

    /** True if this is a concrete instantiation of a template union. */
    bool has_tpl_args() const { return !_tpl_base_name.empty(); }

    /** Original template base name (e.g. "Optional" for Optional__int). */
    const std::string& get_tpl_base_name() const { return _tpl_base_name; }

    /** Concrete template arguments used to instantiate this union. */
    const std::vector<template_argument>& get_tpl_args() const { return _tpl_args; }

    /** Set template instantiation metadata. */
    void set_tpl_instantiation_info(const std::string& base_name, std::vector<template_argument> args) {
        _tpl_base_name = base_name;
        _tpl_args = std::move(args);
    }

    //
    // Alternatives
    //

    /** Add a new alternative to this union. */
    void add_alternative(const std::string& name, const std::string& raw_type_name, bool is_const) {
        union_alternative alt;
        alt.name = name;
        alt.raw_type_name = raw_type_name;
        alt.is_const = is_const;
        alt.index = _alternatives.size();
        _alternatives.push_back(std::move(alt));
    }

    /** Returns all alternatives in declaration order. */
    const std::vector<union_alternative>& alternatives() const { return _alternatives; }
    std::vector<union_alternative>& alternatives_mutable() { return _alternatives; }

    /** Get an alternative by name. Returns nullptr if not found. */
    const union_alternative* get_alternative_by_name(const std::string& name) const {
        for (auto& alt : _alternatives) {
            if (alt.name == name) return &alt;
        }
        return nullptr;
    }

    /** Get the number of alternatives. */
    size_t alternative_count() const { return _alternatives.size(); }

    //
    // Type
    //

    std::shared_ptr<struct_type> get_struct_type() const { return _type; }
    void set_struct_type(std::shared_ptr<struct_type> t) { _type = std::move(t); }

    /**
     * Returns true if any alternative has a non-trivial destructor
     * (i.e., is a struct/class aggregate with a destructor defined).
     * Used to decide whether cleanup codegen is needed at scope exit.
     */
    bool has_nontrivial_destructor_alternative() const {
        for (auto& alt : _alternatives) {
            if (auto st = std::dynamic_pointer_cast<struct_type>(alt.resolved_type)) {
                if (auto agg = st->get_struct()) {
                    if (agg->get_destructor()) return true;
                }
            }
        }
        return false;
    }

    //
    // Visibility
    //

    visibility get_visibility() const { return _visibility; }
    void set_visibility(visibility v) { _visibility = v; }

    //
    // AST node
    //

    void set_ast_aggregate_decl(std::shared_ptr<k::parse::ast::aggregate_decl> ast) {
        _ast_node = std::static_pointer_cast<k::parse::ast::ast_node>(std::move(ast));
    }
    std::shared_ptr<k::parse::ast::aggregate_decl> get_ast_aggregate_decl() const {
        return get_ast_node_as<k::parse::ast::aggregate_decl>();
    }
};


} // namespace k::model

#endif //KLANG_MODEL_UNION_HPP
