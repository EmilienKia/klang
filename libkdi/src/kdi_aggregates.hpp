/*
 * K Language compiler — libkdi
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

#ifndef LIBKDI_AGGREGATES_HPP
#define LIBKDI_AGGREGATES_HPP

/**
 * @file kdi_aggregates.hpp
 *
 * KDI aggregate (struct / class / interface) DTOs.
 *
 * The layout section of each aggregate exports ALL LLVM fields in index order,
 * including synthetic compiler fields (vptrs, base sub-objects, …).
 * Private member variables are collapsed into opaque_block entries that only
 * expose their cumulative bit-size — implementation details remain hidden.
 */

#include "kdi_types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace kdi {

struct kdi_function;
struct kdi_aggregate;

// ─────────────────────────────────────────────────────────────────────────────
// Template data DTOs
// ─────────────────────────────────────────────────────────────────────────────

/**
 * A single template parameter descriptor (for KDI export of template definitions).
 *
 * For type parameters:  kind is "typename"/"struct"/"class"/"interface".
 * For value parameters: kind is "value", and value_type holds the explicit type.
 */
struct kdi_template_param {
    std::string kind;          ///< "typename", "struct", "class", "interface", "value"
    std::string name;          ///< parameter name (e.g. "T", "N")
    std::optional<kdi_type> constraint_type;  ///< base-type constraint (type params)
    std::optional<kdi_type> default_type;     ///< default type (type params)
    std::optional<kdi_type> value_type;       ///< explicit type for value params
    // Note: default_value for value params is stored as a string representation
    // to avoid complex variant serialization.  The importing compiler re-parses it.
    std::optional<std::string> default_value; ///< default value literal (value params)
};

/**
 * A concrete template argument used at an instantiation site.
 *
 * Exactly one of type_arg / value_arg is set:
 * - type_arg for type arguments (e.g. "int" in Pair<int>)
 * - value_arg for value arguments (e.g. "10" in Array<int, 10>), stored as string
 */
struct kdi_template_arg {
    std::optional<kdi_type>   type_arg;   ///< type argument (if type param)
    std::optional<std::string> value_arg;  ///< value argument as string (if value param)
    std::optional<kdi_type>   value_type;  ///< type of the value argument (if value param)
};

/**
 * Template origin metadata for a concrete instantiation.
 * Records which template definition produced this entity and with which arguments.
 */
struct kdi_template_origin {
    std::string base_name;    ///< original template short name (e.g. "Pair")
    std::string base_fq_name; ///< original template fully-qualified name (e.g. "containers::Pair")
    std::vector<kdi_template_arg> args; ///< concrete arguments used for this instantiation
};

/**
 * A complete template definition exported for cross-module re-instantiation.
 *
 * Contains the template parameter descriptors and the raw K source text of the
 * declaration so that an importing compiler can re-parse and re-instantiate it
 * locally with new type arguments.
 */
struct kdi_template_def {
    std::string name;          ///< short name (e.g. "Pair")
    std::string fq_name;       ///< fully-qualified name (e.g. "containers::Pair")
    std::string entity_kind;   ///< "struct", "class", "interface", "function"
    std::string visibility;    ///< "public" or "protected"
    bool is_generic = false;   ///< true for `generic<...>` declarations
    std::vector<kdi_template_param> params; ///< template parameter descriptors
    std::string source;        ///< raw K source text (full declaration + body)
    std::shared_ptr<kdi_aggregate> aggregate_signature; ///< template declaration signature for aggregates
    std::shared_ptr<kdi_function>  function_signature;  ///< template declaration signature for free functions
};

// ─────────────────────────────────────────────────────────────────────────────
// Visibility
// ─────────────────────────────────────────────────────────────────────────────

enum class kdi_visibility : uint8_t {
    public_,
    protected_,
};

// ─────────────────────────────────────────────────────────────────────────────
// Parameter & variable
// ─────────────────────────────────────────────────────────────────────────────

struct kdi_param {
    std::string name;
    kdi_type    type;
};

struct kdi_variable {
    std::string    name;
    std::string    fq_name;
    kdi_visibility visibility   = kdi_visibility::public_;
    kdi_type       type;
    bool           is_const     = false;
    std::string    mangled_name;
};

// ─────────────────────────────────────────────────────────────────────────────
// Function / method
// ─────────────────────────────────────────────────────────────────────────────

/** Global or namespace-level function. */
struct kdi_function {
    std::string              name;
    std::string              fq_name;
    kdi_visibility           visibility   = kdi_visibility::public_;
    bool                     is_static    = false;
    bool                     is_operator  = false;
    kdi_type                 return_type;
    std::vector<kdi_param>   params;
    std::string              mangled_name;
    /// LLVM IR prototype for this function, e.g. "declare i32 @_ZN3foo3barEi(i32)".
    /// Used by importing compilers to reconstruct the exact LLVM Function declaration
    /// without re-deriving the ABI from the KDI type descriptors.
    std::string              llvm_def;
    /// Template origin metadata (set only for concrete template instantiations).
    std::optional<kdi_template_origin> template_origin;
};

/** Member method. */
struct kdi_method {
    std::string              name;
    std::string              fq_name;
    kdi_visibility           visibility      = kdi_visibility::public_;
    bool                     is_static       = false;
    bool                     is_const_member = false;
    bool                     is_virtual      = false;
    bool                     is_abstract     = false;
    bool                     is_final        = false;
    bool                     is_operator     = false;
    int32_t                  vtable_slot     = -1;   ///< -1 = not virtual
    kdi_type                 return_type;
    std::vector<kdi_param>   params;                 ///< excluding 'this'
    std::string              mangled_name;
    /// LLVM IR prototype (with implicit 'this' pointer as first arg), e.g.
    /// "declare i32 @_ZN3ns5Adder3addEi(%struct.ns.Adder* %this, i32)".
    std::string              llvm_def;
    /// Template origin metadata (set only for methods of template instantiations).
    std::optional<kdi_template_origin> template_origin;
};

struct kdi_constructor {
    kdi_visibility           visibility          = kdi_visibility::public_;
    bool                     is_copy_constructor = false;
    bool                     is_defaulted        = false;
    bool                     is_deleted          = false;
    std::vector<kdi_param>   params;
    std::string              mangled_name;         ///< C1 variant
    std::string              mangled_name_c2;      ///< C2 (base-subobject) variant
    /// LLVM IR prototype of the C1 constructor variant, e.g.
    /// "declare void @_ZN3ns7CounterC1Ev(%struct.ns.Counter* %this)".
    std::string              llvm_def;
};

struct kdi_destructor {
    kdi_visibility visibility             = kdi_visibility::public_;
    bool           is_virtual             = false;
    bool           is_compiler_generated  = false;
    std::string    mangled_name;           ///< D1 variant
    std::string    mangled_name_d2;        ///< D2 (base-subobject) variant
    /// LLVM IR prototype of the D1 destructor variant, e.g.
    /// "declare void @_ZN3ns7CounterD1Ev(%struct.ns.Counter* %this)".
    std::string    llvm_def;
};

// ─────────────────────────────────────────────────────────────────────────────
// Vtable
// ─────────────────────────────────────────────────────────────────────────────

struct kdi_vtable_slot {
    uint32_t    slot_index       = 0;
    std::string introducing_func;   ///< fq_name of the introducing function
    std::string override_symbol;    ///< mangled name; empty if abstract
    bool        is_abstract      = false;
};

struct kdi_thunk {
    uint32_t    slot_index       = 0;
    std::string real_func_symbol;   ///< mangled name of concrete override
    int32_t     this_adjustment  = 0;
    bool        needs_thunk      = false;
};

struct kdi_secondary_vtable {
    std::string            base_fq_name;
    uint64_t               base_offset   = 0;  ///< byte offset in derived layout
    std::string            vtable_symbol;       ///< mangled name (generated by derived compiler)
    std::vector<kdi_thunk> thunks;
};

struct kdi_vtable {
    std::string                       vtable_symbol;   ///< mangled name of primary vtable global
    std::string                       rtti_symbol;     ///< mangled name of RTTI global
    std::vector<kdi_vtable_slot>      slots;
    std::vector<kdi_secondary_vtable> secondary;
    /// LLVM IR type declaration for the vtable struct, e.g.
    /// "%vtable.ns.Counter = type { i8*, i8*, void (%struct.ns.Counter*)*, ... }".
    std::string                       llvm_def;
};

// ─────────────────────────────────────────────────────────────────────────────
// Layout fields
// ─────────────────────────────────────────────────────────────────────────────

/** Named, accessible member variable (public or protected). */
struct kdi_layout_member {
    std::string    name;
    std::string    fq_name;
    kdi_visibility visibility       = kdi_visibility::public_;
    uint32_t       llvm_field_index = 0;
    kdi_type       type;
    bool           is_const         = false;
    std::string    mangled_name;
};

/** Primary vptr (first field in classes/interfaces with a vtable). */
struct kdi_layout_vptr {
    uint32_t    llvm_field_index = 0;
    std::string vtable_symbol;     ///< mangled name of the vtable global
};

/** Secondary vptr for a non-primary base sub-object. */
struct kdi_layout_vptr_secondary {
    uint32_t    llvm_field_index = 0;
    std::string base_fq_name;
    std::string vtable_symbol;
};

/** Embedded non-virtual base sub-object (__base_X__). */
struct kdi_layout_base_subobject {
    uint32_t    llvm_field_index = 0;
    std::string base_fq_name;
};

/** Pointer-to-virtual-base slot (__vbptr_X__). */
struct kdi_layout_vbptr {
    uint32_t    llvm_field_index = 0;
    std::string vbase_fq_name;
};

/** Embedded virtual base sub-object (__vbase_X__, in the "collector" class). */
struct kdi_layout_vbase_subobject {
    uint32_t    llvm_field_index = 0;
    std::string vbase_fq_name;
};

/** Implicit parent reference (__parent__, non-static inner aggregates). */
struct kdi_layout_parent_ref {
    uint32_t    llvm_field_index = 0;
    std::string parent_fq_name;
};

/**
 * One or more consecutive private/hidden fields collapsed into an opaque block.
 * The consumer must reserve exactly size_bits in the struct layout but cannot
 * access individual fields by name.
 */
struct kdi_layout_opaque_block {
    uint32_t llvm_field_index = 0;  ///< index of first field in this block
    uint32_t field_count      = 0;  ///< number of LLVM fields in the block
    uint64_t size_bits        = 0;  ///< total bit-size of all fields
};

using kdi_layout_field = std::variant<
    kdi_layout_member,
    kdi_layout_vptr,
    kdi_layout_vptr_secondary,
    kdi_layout_base_subobject,
    kdi_layout_vbptr,
    kdi_layout_vbase_subobject,
    kdi_layout_parent_ref,
    kdi_layout_opaque_block
>;

// ─────────────────────────────────────────────────────────────────────────────
// Base spec
// ─────────────────────────────────────────────────────────────────────────────

struct kdi_base {
    std::string    fq_name;
    kdi_visibility visibility       = kdi_visibility::public_;
    bool           is_virtual       = false;
    int32_t        base_field_index = -1;   ///< LLVM field index of __base_X__; -1 for virtual bases
    uint64_t       byte_offset      = 0;    ///< byte offset in derived layout
};

// ─────────────────────────────────────────────────────────────────────────────
// Aggregate
// ─────────────────────────────────────────────────────────────────────────────

enum class kdi_aggregate_kind : uint8_t {
    struct_,
    class_,
    interface_,
    annotation_,
};

struct kdi_aggregate; // forward for nested aggregates

struct kdi_aggregate {
    // Identity
    kdi_aggregate_kind   kind         = kdi_aggregate_kind::struct_;
    std::string          name;
    std::string          fq_name;
    std::string          mangled_name;

    // Modifiers
    kdi_visibility       visibility   = kdi_visibility::public_;
    bool                 is_abstract  = false;
    bool                 is_final     = false;
    bool                 is_const_struct = false;
    bool                 is_static_nested = false;

    /// Fully-qualified name of the enclosing aggregate, or empty if top-level.
    std::string          enclosing_fq_name;

    // Inheritance
    std::vector<kdi_base> bases;

    // Physical layout (complete LLVM field list in index order)
    std::vector<kdi_layout_field> layout;

    // API
    std::vector<kdi_constructor>  constructors;
    std::optional<kdi_destructor> destructor;
    std::vector<kdi_method>       methods;
    std::vector<kdi_variable>     static_vars;

    // Vtable (class/interface only)
    std::optional<kdi_vtable>     vtable;

    /// Mangled name of the default (0-param) constructor, if one exists.
    /// Empty string if the aggregate has no public default constructor.
    /// Used by importing compilers to call the default constructor for
    /// designated struct initialization of inaccessible/unspecified members.
    std::string                   default_constructor_mangled_name;

    // Nested aggregates (public/protected)
    std::vector<kdi_aggregate>    nested;

    /// LLVM IR struct type definition, e.g.
    /// "%struct.ns.Counter = type { i32*, i32 }".
    /// Used by importing compilers to reconstruct the exact LLVM StructType
    /// without re-deriving the layout from the KDI layout fields.
    std::string                   llvm_def;

    /// Template origin metadata (set only for concrete template instantiations).
    std::optional<kdi_template_origin> template_origin;
};

// ─────────────────────────────────────────────────────────────────────────────
// Enumeration
// ─────────────────────────────────────────────────────────────────────────────

/** A single entry in an exported enumeration. */
struct kdi_enum_entry {
    std::string name;
    int64_t     value      = 0;
    bool        is_default = false;
    /// Optional designated-init payload for object-backed enum entries.
    /// Keys are member names, values are integer literal constants.
    std::vector<std::pair<std::string, int64_t>> object_init_members;
};

/** An exported enumeration. */
struct kdi_enum {
    std::string                   name;            ///< short name
    std::string                   fq_name;         ///< fully-qualified K name
    kdi_visibility                visibility = kdi_visibility::public_;
    kdi_type                      underlying_type; ///< backing primitive int type
    /// Backing object type for typed object-backed enums (must be aggregate ref when set).
    std::optional<kdi_type>       object_type;
    /// Symbol name of the backing global table (array of object_type entries).
    std::optional<std::string>    object_table_symbol;
    std::optional<std::string>    base_fq_name;    ///< base enum fq_name (derivation)
    std::vector<kdi_enum_entry>   entries;
};

} // namespace kdi

#endif // LIBKDI_AGGREGATES_HPP

