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
 * callables, and named aggregate references.
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

// ── Callable ──────────────────────────────────────────────────────────────────

/** Addresser applied to a callable prototype. */
enum class kdi_callable_addresser {
    none,       ///< bare prototype `(int):bool` — alias / typedef target only
    ptr,        ///< `*(int):bool` — nullable, rebindable
    view,       ///< `?(int):bool` — nullable, not rebindable
    link,       ///< `+(int):bool` — non-null, rebindable
    ref,        ///< `&(int):bool` — non-null, not rebindable
    owner       ///< `!(int):bool` — nullable, rebindable, owning closure environment
};

/**
 * Callable type — a first-class invocable with a fixed prototype.
 *
 * Mirrors k::model::callable_type: an addresser, a return type (a
 * kdi_void_type return means "returns nothing"), a parameter list and a
 * declared checked-exception set.
 *
 * When @c member_of is non-empty the type is an *unbound member function
 * reference* (`Counter::*(int):int`) rather than a fat callable: it carries no
 * bound receiver and names the owner aggregate by its fully-qualified K name.
 */
struct kdi_callable_type {
    kdi_callable_addresser                 addresser = kdi_callable_addresser::ptr;
    std::shared_ptr<kdi_type>              ret;
    std::vector<std::shared_ptr<kdi_type>> params;
    std::vector<std::shared_ptr<kdi_type>> throws;
    std::string                            member_of;  ///< empty unless unbound member fn ref
};

/** Textual encoding of a callable addresser, as stored in the KDI. */
inline const char* to_string(kdi_callable_addresser a) {
    switch (a) {
        case kdi_callable_addresser::none:  return "none";
        case kdi_callable_addresser::view:  return "view";
        case kdi_callable_addresser::link:  return "link";
        case kdi_callable_addresser::ref:   return "ref";
        case kdi_callable_addresser::owner: return "owner";
        case kdi_callable_addresser::ptr:   break;
    }
    return "ptr";
}

/** Decode a callable addresser; unknown spellings degrade to `ptr`. */
inline kdi_callable_addresser callable_addresser_from_string(const std::string& s) {
    if (s == "none")  return kdi_callable_addresser::none;
    if (s == "view")  return kdi_callable_addresser::view;
    if (s == "link")  return kdi_callable_addresser::link;
    if (s == "ref")   return kdi_callable_addresser::ref;
    if (s == "owner") return kdi_callable_addresser::owner;
    return kdi_callable_addresser::ptr;
}

/** K source-level symbol of a callable addresser (empty for a bare prototype). */
inline const char* callable_addresser_symbol(kdi_callable_addresser a) {
    switch (a) {
        case kdi_callable_addresser::none:  return "";
        case kdi_callable_addresser::view:  return "?";
        case kdi_callable_addresser::link:  return "+";
        case kdi_callable_addresser::ref:   return "&";
        case kdi_callable_addresser::owner: return "!";
        case kdi_callable_addresser::ptr:   break;
    }
    return "*";
}

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
    kdi_callable_type,
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
    static kdi_type make_callable(kdi_callable_addresser addresser,
                                  kdi_type ret,
                                  std::vector<std::shared_ptr<kdi_type>> params = {}) {
        kdi_callable_type c;
        c.addresser = addresser;
        c.ret       = std::make_shared<kdi_type>(std::move(ret));
        c.params    = std::move(params);
        return {std::move(c)};
    }
};

} // namespace kdi

#endif // LIBKDI_TYPES_HPP

