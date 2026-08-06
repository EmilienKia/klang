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

#ifndef LIBKDI_TYPES_HPP
#define LIBKDI_TYPES_HPP

/**
 * @file kdi_types.hpp
 *
 * KDI type system DTOs.
 *
 * Types are described as a tagged variant (kdi_type).  The encoding mirrors the
 * K type system: primitives, five indirections (&, *, +, ?, #), const, arrays,
 * function references, and named aggregate references.
 */

#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace kdi {

// ─────────────────────────────────────────────────────────────────────────────
// kdi_type — tagged union
// ─────────────────────────────────────────────────────────────────────────────

struct kdi_type;  // forward

// ── Primitives ────────────────────────────────────────────────────────────────

struct kdi_void_type {};

struct kdi_bool_type {};

struct kdi_char_type {};

/** Integer type: signed or unsigned, 8/16/32/64 bits. */
struct kdi_int_type {
    uint32_t bits   = 32;
    bool     is_signed = true;
};

/** Floating-point type: 32 or 64 bits. */
struct kdi_float_type {
    uint32_t bits = 64;
};

// ── Indirections ─────────────────────────────────────────────────────────────

/** Reference type (&inner). */
struct kdi_ref_type {
    std::shared_ptr<kdi_type> inner;
};

/** Pointer type (*inner). */
struct kdi_ptr_type {
    std::shared_ptr<kdi_type> inner;
};

/** Link type (+inner) — mutable, non-null, rebindable. */
struct kdi_link_type {
    std::shared_ptr<kdi_type> inner;
};

/** View type (?inner) — immutable, nullable. */
struct kdi_view_type {
    std::shared_ptr<kdi_type> inner;
};

/** Owner type (!inner) — owning, nullable. */
struct kdi_owner_type {
    std::shared_ptr<kdi_type> inner;
};

/** Drain type (#inner) — immutable binding, non-null, drain permission. */
struct kdi_drain_type {
    std::shared_ptr<kdi_type> inner;
};

// ── Qualifiers ────────────────────────────────────────────────────────────────

/** Const-qualified type. */
struct kdi_const_type {
    std::shared_ptr<kdi_type> inner;
};

// ── Arrays ────────────────────────────────────────────────────────────────────

/** Unsized array type. */
struct kdi_array_type {
    std::shared_ptr<kdi_type> elem;
};

/** Sized array type. */
struct kdi_sized_array_type {
    std::shared_ptr<kdi_type> elem;
    uint64_t                  size = 0;
};

// ── Function reference ────────────────────────────────────────────────────────

/** Function reference type. */
struct kdi_fn_ref_type {
    std::shared_ptr<kdi_type>              ret;
    std::vector<std::shared_ptr<kdi_type>> params;
};

// ── Named aggregate reference ─────────────────────────────────────────────────

/** Reference to an aggregate type by its fully-qualified K name. */
struct kdi_aggregate_ref {
    std::string fq_name;      ///< e.g. "math::Vec3"
};

/** Reference to an enum type by its fully-qualified K name. */
struct kdi_enum_ref {
    std::string fq_name;      ///< e.g. "color::Color"
};

/**
 * Reference to an exported alias / typedef by its fully-qualified K name.
 *
 * Only a strong alias (typedef) is referenced this way: it is nominally
 * distinct from the type it renames, so the distinction has to survive the
 * round trip through the KDI. A soft alias is fully transparent and is always
 * exported as the type it renames.
 */
struct kdi_alias_ref {
    std::string fq_name;      ///< e.g. "app::Identifier"
};

/** Reference to a template parameter by name inside a template signature. */
struct kdi_template_param_ref {
    std::string name;         ///< e.g. "T"
};

/**
 * Reference to a named type applied with template (type) arguments, as it
 * appears inside an uninstantiated template's own declaration (e.g. the
 * member type "MultiSlot<T>" inside `template<typename T> class Vector`).
 *
 * Unlike kdi_aggregate_ref, the referenced name may not resolve to a concrete
 * aggregate outside of an instantiation context — it can name another
 * template (own or imported) that has not been (and may never be)
 * instantiated with concrete arguments. Each argument is itself a full
 * kdi_type so that nested template-parameter references (e.g. "Node<T>"
 * inside "List<T>") are preserved structurally instead of degrading to
 * opaque source text.
 *
 * Only type arguments are represented (the common case for libk-style
 * collections). A non-type (value) argument used in such a nested reference
 * falls back to a single unresolved kdi_void_type placeholder entry.
 */
struct kdi_generic_ref_type {
    std::string name;                                ///< base name as written (e.g. "MultiSlot"), unqualified
    std::vector<std::shared_ptr<kdi_type>> args;      ///< template type arguments
};

// ── Tagged union ─────────────────────────────────────────────────────────────

using kdi_type_variant = std::variant<
    kdi_void_type,
    kdi_bool_type,
    kdi_char_type,
    kdi_int_type,
    kdi_float_type,
    kdi_ref_type,
    kdi_ptr_type,
    kdi_link_type,
    kdi_view_type,
    kdi_owner_type,
    kdi_drain_type,
    kdi_const_type,
    kdi_array_type,
    kdi_sized_array_type,
    kdi_fn_ref_type,
    kdi_aggregate_ref,
    kdi_enum_ref,
    kdi_alias_ref,
    kdi_template_param_ref,
    kdi_generic_ref_type
>;

/** A complete K type, encoded as a tagged union. */
struct kdi_type {
    kdi_type_variant value;

    // Convenience constructors
    static kdi_type make_void()  { return {kdi_void_type{}}; }
    static kdi_type make_bool()  { return {kdi_bool_type{}}; }
    static kdi_type make_char()  { return {kdi_char_type{}}; }
    static kdi_type make_int(uint32_t bits, bool is_signed = true) {
        return {kdi_int_type{bits, is_signed}};
    }
    static kdi_type make_float(uint32_t bits) {
        return {kdi_float_type{bits}};
    }
    static kdi_type make_aggregate(std::string fq_name) {
        return {kdi_aggregate_ref{std::move(fq_name)}};
    }
    static kdi_type make_enum(std::string fq_name) {
        return {kdi_enum_ref{std::move(fq_name)}};
    }
    static kdi_type make_alias(std::string fq_name) {
        return {kdi_alias_ref{std::move(fq_name)}};
    }
    static kdi_type make_template_param(std::string name) {
        return {kdi_template_param_ref{std::move(name)}};
    }
    static kdi_type make_owner(kdi_type inner) {
        return {kdi_owner_type{std::make_shared<kdi_type>(std::move(inner))}};
    }
};

} // namespace kdi

#endif // LIBKDI_TYPES_HPP

