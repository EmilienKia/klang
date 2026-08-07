/*
 * K Language compiler
 *
 * Copyright 2026 Emilien Kia
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

#ifndef KLANG_KDI_EXPORTER_HPP
#define KLANG_KDI_EXPORTER_HPP

/**
 * @file kdi_exporter.hpp
 *
 * kdi_builder — a default_model_visitor that traverses a fully-compiled K
 * model unit (post process_generation) and accumulates a kdi_file DTO, which
 * can then be serialised as a .kdi CBOR file by libkdi.
 *
 * The visitor caches a reference to the compilation context so that all LLVM
 * DataLayout queries, mangled names and vtable layouts are available during
 * traversal without being passed as parameters.
 *
 * Private member variables are obfuscated: consecutive private/synthetic
 * fields are grouped into kdi_layout_opaque_block entries that only expose
 * their cumulative bit-size (queried from the LLVM DataLayout).
 *
 * Usage:
 *   kdi_builder builder(context, lib_path, compiler_ver);
 *   unit.accept(builder);
 *   kdi::kdi_file file = builder.take();
 */

#include "../model_visitor.hpp"
#include "../context.hpp"

#include <kdi.hpp>

#include <functional>
#include <string>
#include <vector>

namespace k::model {

/**
 * Visitor that builds a kdi_file from the fully-compiled K model.
 *
 * Derives from default_model_visitor so that any new model node added in the
 * future will compile without error (the default no-op is inherited), but will
 * be visible as a missing override if it should be exported.
 *
 * Must be called AFTER process_generation() so that all LLVM types, vtable
 * layouts and mangled names are fully resolved.
 */
class kdi_builder : public default_model_visitor {
public:
    /**
     * @param ctx           The compilation context (post generation).
     * @param lib_path      Path to the produced binary (.so or .a).
     * @param compiler_ver  Compiler version string (e.g. "klangc 0.0.1").
     */
    explicit kdi_builder(context& ctx,
                         const std::string& lib_path = "",
                         const std::string& compiler_ver = "");

    // Non-copyable, non-movable (holds stack references)
    kdi_builder(const kdi_builder&) = delete;
    kdi_builder& operator=(const kdi_builder&) = delete;

    /** Move the accumulated kdi_file out of the builder (call once, after accept()). */
    kdi::kdi_file take() { return std::move(_file); }

    // ── Overridden visit methods ──────────────────────────────────────────────

    void visit_unit(unit& u)                                         override;
    void visit_namespace(ns& n)                                      override;
    void visit_structure(structure& s)                               override;
    void visit_klass(klass& k)                                       override;
    void visit_interface(interface& i)                               override;
    void visit_annotation_type(annotation_type& a)                   override;
    void visit_function(function& fn)                                override;
    void visit_constructor(constructor& ctor)                        override;
    void visit_destructor(destructor& dtor)                          override;
    void visit_global_variable_definition(global_variable_definition& var) override;
    void visit_member_variable_definition(member_variable_definition& var) override;

    // Enum declarations
    void visit_enumeration(enumeration&) override;

    // Union declarations
    void visit_union(union_type_def&) override;

    // Compiler-internal nodes — silently ignored
    void visit_static_constructor(static_constructor&)               override {}
    void visit_static_destructor(static_destructor&)                 override {}
    void visit_global_tool_function(global_tool_function&)           override {}
    void visit_global_constructor_function(global_constructor_function&) override {}
    void visit_global_destructor_function(global_destructor_function&)   override {}
    void visit_global_main_function(global_main_function&)           override {}
    void visit_parameter(parameter&)                                 override {}

private:
    // ── Cached compilation context ────────────────────────────────────────────
    context&    _ctx;
    std::string _lib_path;
    std::string _compiler_ver;

    // ── Output ────────────────────────────────────────────────────────────────
    kdi::kdi_file _file;

    // ── Traversal state stacks ────────────────────────────────────────────────
    // Each push/pop pair wraps the visit of a namespace or aggregate.
    std::vector<kdi::kdi_namespace*>  _ns_stack;   ///< current namespace output target
    std::vector<kdi::kdi_aggregate*>  _agg_stack;  ///< current aggregate output target

    // ── Helpers ───────────────────────────────────────────────────────────────

    /** True if we are currently inside an aggregate (method/ctor/dtor context). */
    bool in_aggregate() const { return !_agg_stack.empty(); }

    /** True if the element's visibility qualifies for export. */
    static bool is_exported(visibility v) {
        return v == PUBLIC || v == PROTECTED;
    }

    /** Convert a model visibility to a kdi visibility. */
    static kdi::kdi_visibility to_kdi_vis(visibility v);

    /** Convert a model type to a kdi_type DTO (recursive). */
    kdi::kdi_type to_kdi_type(const std::shared_ptr<type>& t) const;

    /** Map a model callable addresser to its KDI counterpart. */
    static kdi::kdi_callable_addresser to_kdi_addresser(callable_type::addresser a);

    /**
     * Convert a callable_type (or an unbound member function reference) to its
     * KDI DTO, converting every component through @p convert so that both the
     * concrete and the template-signature conversions can share the code.
     */
    kdi::kdi_callable_type to_kdi_callable(
        const callable_type& ct,
        const std::function<kdi::kdi_type(const std::shared_ptr<type>&)>& convert) const;

    /** Convert a template-signature type to KDI, preserving template parameter placeholders. */
    kdi::kdi_type to_kdi_signature_type(const std::shared_ptr<type>& t,
                                        const tpl_info& ti) const;

    /** Build the parameter list for a function, skipping synthetic params. */
    std::vector<kdi::kdi_param> to_kdi_params(const function& fn) const;

    /** Build the parameter list for a template signature, preserving template parameter placeholders. */
    std::vector<kdi::kdi_param> to_kdi_signature_params(const function& fn,
                                                        const tpl_info& ti) const;

    /** Register an aggregate in the type table (idempotent). */
    void register_aggregate_type(const aggregate& agg);

    /** Build the physical layout (LLVM-order fields) for an aggregate. */
    std::vector<kdi::kdi_layout_field> build_layout(const aggregate& agg) const;

    /** Build the vtable descriptor for a class/interface, or nullopt. */
    std::optional<kdi::kdi_vtable> build_vtable(const aggregate& agg) const;

    /** Fill the common fields of a kdi_aggregate from a model aggregate. */
    kdi::kdi_aggregate begin_aggregate(const aggregate& agg);

    /** Dispatch aggregate visit body (shared between structure/klass/interface). */
    void visit_aggregate_body(aggregate& agg, kdi::kdi_aggregate& kagg);

    /** Export the exported alias/typedef declarations of a scope. */
    void export_aliases(const alias_holder& holder, std::vector<kdi::kdi_alias>& out,
                        const std::string& scope_fq_name);

    /** Build a kdi_template_origin from an entity's tpl_base_name and tpl_args. */
    kdi::kdi_template_origin build_template_origin(const std::string& base_name,
                                                    const std::string& fq_name,
                                                    const std::vector<template_argument>& args) const;

    /** Build a kdi_template_def from a template definition (aggregate or function).
     *  Uses k_source_emitter to reconstruct the source with resolved types,
     *  falling back to ti.source_text if the emitter produces empty output.
     *  @param entity  The model element (aggregate or function) to emit source for. May be null for fallback-only. */
    kdi::kdi_template_def build_template_def(const std::string& name,
                                              const std::string& fq_name,
                                              const std::string& entity_kind,
                                              visibility vis,
                                              const tpl_info& ti,
                                              const element* entity) const;

    /** Build the declaration-only aggregate signature embedded in a generic template definition. */
    kdi::kdi_aggregate build_generic_template_aggregate_signature(const aggregate& agg,
                                                                  const tpl_info& ti,
                                                                  const std::string& fq_name) const;

    /** Build the declaration-only free-function signature embedded in a generic template definition. */
    kdi::kdi_function build_generic_template_function_signature(const function& fn,
                                                                const tpl_info& ti,
                                                                const std::string& fq_name) const;
};

/**
 * Convenience free function: construct a kdi_builder, run it over @p unit,
 * and return the resulting kdi_file.
 *
 * @param ctx           The compilation context (post generation).
 * @param lib_path      Path to the produced binary (.so or .a).
 * @param compiler_ver  Compiler version string.
 */
kdi::kdi_file build_kdi(context& ctx,
                         const unit& u,
                         const std::string& lib_path = "",
                         const std::string& compiler_ver = "");

} // namespace k::model

#endif // KLANG_KDI_EXPORTER_HPP

