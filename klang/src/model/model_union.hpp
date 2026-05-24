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

    /** All directly-declared alternatives (own only, not inherited). */
    std::vector<union_alternative> _alternatives;

    /** The LLVM struct type representing this union (discriminant + storage). */
    std::shared_ptr<struct_type> _type;

    /** Synthesized Kind enum (entries cover the full inheritance chain). */
    std::shared_ptr<enumeration> _kind_enum;

    /** Declared visibility. */
    visibility _visibility = PUBLIC;

    /** Template information (nullptr if this is not a template union). */
    std::unique_ptr<tpl_info> _tpl_info;

    // ── Union inheritance ────────────────────────────────────────────────────

    /** Raw name of the parent union as written in source (empty = root union). */
    std::string _base_union_raw_name;

    /** Resolved parent union (nullptr = root union; set during symbol resolution). */
    std::shared_ptr<union_type_def> _base_union;

    /** For concrete instantiations of template unions: original template base name. */
    std::string _tpl_base_name;

    /** For concrete instantiations: the concrete template arguments used. */
    std::vector<template_argument> _tpl_args;

    /** True while this union is being resolved (cycle detection). */
    bool _resolving = false;

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

    /** Add a new alternative to this union (index is set to local position; call
     *  reindex_own_alternatives() after the base union is resolved to get global indices). */
    void add_alternative(const std::string& name, const std::string& raw_type_name, bool is_const) {
        union_alternative alt;
        alt.name = name;
        alt.raw_type_name = raw_type_name;
        alt.is_const = is_const;
        alt.index = _alternatives.size(); // local index; will be adjusted for derived unions
        _alternatives.push_back(std::move(alt));
    }

    /** Returns the directly-declared alternatives for this union (not inherited). */
    const std::vector<union_alternative>& alternatives() const { return _alternatives; }
    std::vector<union_alternative>& alternatives_mutable() { return _alternatives; }

    /** Get an alternative by name; searches own alternatives first, then the base chain. */
    const union_alternative* get_alternative_by_name(const std::string& name) const {
        for (const auto& alt : _alternatives) {
            if (alt.name == name) return &alt;
        }
        if (_base_union) return _base_union->get_alternative_by_name(name);
        return nullptr;
    }

    /** Get an alternative by its global discriminant index; searches the full chain. */
    const union_alternative* get_alternative_by_global_index(size_t global_idx) const {
        for (const auto& alt : _alternatives) {
            if (alt.index == global_idx) return &alt;
        }
        if (_base_union) return _base_union->get_alternative_by_global_index(global_idx);
        return nullptr;
    }

    /** Returns the number of directly-declared alternatives (not inherited). */
    size_t alternative_count() const { return _alternatives.size(); }

    /** Returns the total number of alternatives in the full inheritance chain. */
    size_t total_alternative_count() const {
        return base_alternative_count() + _alternatives.size();
    }

    /** Number of alternatives contributed by the parent chain (0 if root). */
    size_t base_alternative_count() const {
        return _base_union ? _base_union->total_alternative_count() : 0u;
    }

    /** Collect pointers to all alternatives from root to this union (parent first, own last).
     *  Indices in the returned alternatives are global discriminant values. */
    std::vector<const union_alternative*> all_alternatives_ptrs() const {
        std::vector<const union_alternative*> result;
        if (_base_union) {
            auto parent_alts = _base_union->all_alternatives_ptrs();
            result.insert(result.end(), parent_alts.begin(), parent_alts.end());
        }
        for (const auto& alt : _alternatives) {
            result.push_back(&alt);
        }
        return result;
    }

    /** Update own alternatives' index fields using the base chain count as offset.
     *  Must be called after the base union is resolved. Idempotent. */
    void reindex_own_alternatives() {
        size_t offset = base_alternative_count();
        for (size_t i = 0; i < _alternatives.size(); ++i) {
            _alternatives[i].index = offset + i;
        }
    }

    //
    // Inheritance
    //

    /** True if this union has a base union (either raw name or resolved). */
    bool has_base_union() const { return !_base_union_raw_name.empty(); }

    const std::string& get_base_union_raw_name() const { return _base_union_raw_name; }
    void set_base_union_raw_name(const std::string& n) { _base_union_raw_name = n; }

    std::shared_ptr<union_type_def> get_base_union() const { return _base_union; }
    void set_base_union(std::shared_ptr<union_type_def> base) { _base_union = std::move(base); }

    /** Force-reset and re-synthesize the Kind enum (e.g. after the base is resolved
     *  and indices have been updated). */
    void resynthesise_kind_enum() {
        _kind_enum.reset();
        synthesize_kind_enum();
    }

    //
    // Type
    //

    std::shared_ptr<struct_type> get_struct_type() const { return _type; }
    void set_struct_type(std::shared_ptr<struct_type> t) { _type = std::move(t); }

    //
    // Kind enum
    //

    /** Get (or lazily synthesize) the Kind enum for this union. */
    std::shared_ptr<enumeration> get_kind_enum() const { return _kind_enum; }

    /** Synthesize the Kind enum from the current alternatives list.
     *  Should be called after all alternatives are finalized (e.g. during aggregate resolution). */
    void synthesize_kind_enum();

    /**
     * Returns true if any alternative (own or inherited) has a non-trivial destructor.
     * Used to decide whether cleanup codegen is needed at scope exit.
     */
    bool has_nontrivial_destructor_alternative() const {
        for (const auto* alt_ptr : all_alternatives_ptrs()) {
            if (auto st = std::dynamic_pointer_cast<struct_type>(alt_ptr->resolved_type)) {
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
