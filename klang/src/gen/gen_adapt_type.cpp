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

//
// Extracted helpers for type_reference_resolver::adapt_type.
// Contains per-type-category adaptation sub-methods called from
// the main adapt_type dispatcher in resolvers.cpp.
//

#include "resolvers.hpp"

#include "../model/expressions.hpp"

namespace k::model::gen {


// ── adapt_function_ref_type ──────────────────────────────────────────────────

std::shared_ptr<expression>
type_reference_resolver::adapt_function_ref_type(
    std::shared_ptr<expression> expr,
    const std::shared_ptr<type>& type_src,
    const std::shared_ptr<type>& type_nc)
{
    // Case 1: target is a bare function_reference_type
    if (auto tgt_frt = std::dynamic_pointer_cast<function_reference_type>(type_nc)) {
        if (std::dynamic_pointer_cast<function_reference_type>(type_src)) {
            // Source is already a bare frt — no load needed.
            return expr;
        }
        if (type::is_reference(type_src)) {
            auto src_inner = type::remove_const(std::dynamic_pointer_cast<reference_type>(type_src)->get_subtype());
            if (std::dynamic_pointer_cast<function_reference_type>(src_inner)) {
                // Check if this is a direct function symbol (impl_gen returns Function* directly).
                auto sym = std::dynamic_pointer_cast<symbol_expression>(expr);
                if (sym && sym->is_function()) {
                    // Direct function address: no load needed, impl_gen produces the ptr directly.
                    return expr;
                }
                // Variable holding a function pointer: load the stored function pointer from the alloca.
                return adapt_reference_load_value(expr);
            }
        }
        return {};
    }
    // Case 2: source is ref<frt> and target is also ref<frt> — pass through unchanged.
    if (type::is_reference(type_nc)) {
        auto tgt_sub_nc = type::remove_const(std::dynamic_pointer_cast<reference_type>(type_nc)->get_subtype());
        if (std::dynamic_pointer_cast<function_reference_type>(tgt_sub_nc)) {
            if (type_src == type_nc) return expr;
            auto src_sub = type::is_reference(type_src)
                ? std::dynamic_pointer_cast<reference_type>(type_src)->get_subtype()
                : type_src;
            if (std::dynamic_pointer_cast<function_reference_type>(type::remove_const(src_sub))) {
                return expr; // compatible frt ref
            }
        }
    }
    return nullptr; // not handled — let caller continue
}


// ── adapt_from_pointer ───────────────────────────────────────────────────────

std::shared_ptr<expression>
type_reference_resolver::adapt_from_pointer(
    std::shared_ptr<expression> expr,
    const std::shared_ptr<type>& type_src,
    const std::shared_ptr<type>& type_nc)
{
    if (type::is_pointer(type_nc) || type::is_link(type_nc)) {
        if (type_nc == type_src) {
            // Pointers to same type, return the expression
            return expr;
        }
        // Check struct upcast: ptr<Derived> → ptr<Base> or ptr<Derived> → lien<Base>
        auto src_sub = type_src->get_subtype();
        auto tgt_sub = type_nc->get_subtype();
        auto src_sub_nc = type::remove_const(src_sub);
        auto tgt_sub_nc = type::remove_const(tgt_sub);
        if (src_sub_nc != tgt_sub_nc) {
            auto src_st_type = std::dynamic_pointer_cast<struct_type>(src_sub_nc);
            auto tgt_st_type = std::dynamic_pointer_cast<struct_type>(tgt_sub_nc);
            if (src_st_type && tgt_st_type) {
                auto src_st = src_st_type->get_struct();
                auto tgt_st = tgt_st_type->get_struct();
                if (src_st && tgt_st && src_st->is_derived_from(tgt_st)) {
                    auto upcast = cast_expression::make_shared(expr, type_nc);
                    upcast->set_type(type_nc);
                    return upcast;
                }
            }
            return {};
        }
        return expr;
    } else {
        // ptr<T> → ref<T>: borrow pointer target as reference (LLVM-level identical)
        if (type::is_reference(type_nc)) {
            auto tgt_sub_nc = type::remove_const(std::dynamic_pointer_cast<reference_type>(type_nc)->get_subtype());
            auto src_sub_nc = type::remove_const(type_src->get_subtype());
            if (src_sub_nc == tgt_sub_nc) {
                auto cast = cast_expression::make_shared(expr, type_nc);
                cast->set_type(type_nc);
                return cast;
            }
        }
        // Error: Source is a pointer, and asked to be cast to an object.
        return {};
    }
}


// ── adapt_from_link ──────────────────────────────────────────────────────────

std::shared_ptr<expression>
type_reference_resolver::adapt_from_link(
    std::shared_ptr<expression> expr,
    const std::shared_ptr<type>& type_src,
    const std::shared_ptr<type>& type_nc)
{
    if (type::is_link(type_nc) || type::is_pointer(type_nc) || type::is_view(type_nc)) {
        if (type_nc == type_src) {
            return expr;
        }
        // Check struct upcast: lien<Derived> → lien<Base> or lien<Derived> → ptr<Base>
        auto src_sub = type_src->get_subtype();
        auto tgt_sub = type_nc->get_subtype();
        auto src_sub_nc = type::remove_const(src_sub);
        auto tgt_sub_nc = type::remove_const(tgt_sub);
        if (src_sub_nc != tgt_sub_nc) {
            auto src_st_type = std::dynamic_pointer_cast<struct_type>(src_sub_nc);
            auto tgt_st_type = std::dynamic_pointer_cast<struct_type>(tgt_sub_nc);
            if (src_st_type && tgt_st_type) {
                auto src_st = src_st_type->get_struct();
                auto tgt_st = tgt_st_type->get_struct();
                if (src_st && tgt_st && src_st->is_derived_from(tgt_st)) {
                    auto upcast = cast_expression::make_shared(expr, type_nc);
                    upcast->set_type(type_nc);
                    return upcast;
                }
            }
            return {};
        }
        return expr;
    } else {
        // lnk<T> → ref<T>: borrow link target as reference
        if (type::is_reference(type_nc)) {
            auto tgt_sub_nc = type::remove_const(std::dynamic_pointer_cast<reference_type>(type_nc)->get_subtype());
            auto src_sub_nc = type::remove_const(type_src->get_subtype());
            if (src_sub_nc == tgt_sub_nc) {
                auto cast = cast_expression::make_shared(expr, type_nc);
                cast->set_type(type_nc);
                return cast;
            }
        }
        return {};
    }
}


// ── adapt_from_view ──────────────────────────────────────────────────────────

std::shared_ptr<expression>
type_reference_resolver::adapt_from_view(
    std::shared_ptr<expression> expr,
    const std::shared_ptr<type>& type_src,
    const std::shared_ptr<type>& type_nc)
{
    if (type::is_view(type_nc) || type::is_pointer(type_nc)) {
        if (type_nc == type_src) {
            return expr;
        }
        // Check struct upcast: pin<Derived> → pin<Base> or pin<Derived> → ptr<Base>
        auto src_sub = type_src->get_subtype();
        auto tgt_sub = type_nc->get_subtype();
        auto src_sub_nc = type::remove_const(src_sub);
        auto tgt_sub_nc = type::remove_const(tgt_sub);
        if (src_sub_nc != tgt_sub_nc) {
            auto src_st_type = std::dynamic_pointer_cast<struct_type>(src_sub_nc);
            auto tgt_st_type = std::dynamic_pointer_cast<struct_type>(tgt_sub_nc);
            if (src_st_type && tgt_st_type) {
                auto src_st = src_st_type->get_struct();
                auto tgt_st = tgt_st_type->get_struct();
                if (src_st && tgt_st && src_st->is_derived_from(tgt_st)) {
                    auto upcast = cast_expression::make_shared(expr, type_nc);
                    upcast->set_type(type_nc);
                    return upcast;
                }
            }
            return {};
        }
        return expr;
    } else {
        // view<T> → ref<T>: borrow view target as reference
        if (type::is_reference(type_nc)) {
            auto tgt_sub_nc = type::remove_const(std::dynamic_pointer_cast<reference_type>(type_nc)->get_subtype());
            auto src_sub_nc = type::remove_const(type_src->get_subtype());
            if (src_sub_nc == tgt_sub_nc) {
                auto cast = cast_expression::make_shared(expr, type_nc);
                cast->set_type(type_nc);
                return cast;
            }
        }
        return {};
    }
}


// ── adapt_from_owner ─────────────────────────────────────────────────────────

std::shared_ptr<expression>
type_reference_resolver::adapt_from_owner(
    std::shared_ptr<expression> expr,
    const std::shared_ptr<type>& type_src,
    const std::shared_ptr<type>& type_nc)
{
    auto src_sub = type_src->get_subtype();
    auto src_sub_nc = type::remove_const(src_sub);
    if (type::is_owner(type_nc)) {
        if (type_nc == type_src) return expr;
        auto tgt_sub_nc = type::remove_const(type_nc->get_subtype());
        if (src_sub_nc == tgt_sub_nc) return expr;
        // Upcast owner<Derived> → owner<Base>
        auto src_st = std::dynamic_pointer_cast<struct_type>(src_sub_nc);
        auto tgt_st = std::dynamic_pointer_cast<struct_type>(tgt_sub_nc);
        if (src_st && tgt_st && src_st->get_struct() && tgt_st->get_struct() &&
            src_st->get_struct()->is_derived_from(tgt_st->get_struct())) {
            auto upcast = cast_expression::make_shared(expr, type_nc);
            upcast->set_type(type_nc);
            return upcast;
        }
        return {};
    }
    if (type::is_pointer(type_nc)) {
        // Borrow as pointer observer — address is used, ownership stays in the owner
        auto tgt_sub_nc = type::remove_const(type_nc->get_subtype());
        if (src_sub_nc == tgt_sub_nc) {
            // Same subtype: reinterpret owner value as pointer (same LLVM representation)
            auto cast = cast_expression::make_shared(expr, type_nc);
            cast->set_type(type_nc);
            return cast;
        }
        // Upcast: owner<Derived> → ptr<Base>
        auto src_st = std::dynamic_pointer_cast<struct_type>(src_sub_nc);
        auto tgt_st = std::dynamic_pointer_cast<struct_type>(tgt_sub_nc);
        if (src_st && tgt_st && src_st->get_struct() && tgt_st->get_struct() &&
            src_st->get_struct()->is_derived_from(tgt_st->get_struct())) {
            auto upcast = cast_expression::make_shared(expr, type_nc);
            upcast->set_type(type_nc);
            return upcast;
        }
        return {};
    }
    // owner<T> → lnk<T> / view<T>: borrow as link or view (same LLVM representation)
    if (type::is_link(type_nc) || type::is_view(type_nc)) {
        auto tgt_sub_nc = type::remove_const(type_nc->get_subtype());
        if (src_sub_nc == tgt_sub_nc) {
            auto cast = cast_expression::make_shared(expr, type_nc);
            cast->set_type(type_nc);
            return cast;
        }
    }
    // owner<T> → ref<T>: borrow owned object as reference (same LLVM representation)
    if (type::is_reference(type_nc)) {
        auto tgt_sub_nc = type::remove_const(std::dynamic_pointer_cast<reference_type>(type_nc)->get_subtype());
        if (src_sub_nc == tgt_sub_nc) {
            auto cast = cast_expression::make_shared(expr, type_nc);
            cast->set_type(type_nc);
            return cast;
        }
    }
    return {};
}


// ── adapt_from_drain ─────────────────────────────────────────────────────────

std::shared_ptr<expression>
type_reference_resolver::adapt_from_drain(
    std::shared_ptr<expression> expr,
    const std::shared_ptr<type>& type_src,
    const std::shared_ptr<type>& type_nc)
{
    auto drn = std::dynamic_pointer_cast<drain_type>(type_src);
    auto src_sub = drn->get_drained_type();
    auto src_sub_nc = type::remove_const(src_sub);

    // drain<T> → drain<T>: identity
    if (type::is_drain(type_nc)) {
        auto tgt_sub_nc = type::remove_const(type_nc->get_subtype());
        if (src_sub_nc == tgt_sub_nc) return expr;
        // Struct upcast: drain<Derived> → drain<Base>
        auto src_st = std::dynamic_pointer_cast<struct_type>(src_sub_nc);
        auto tgt_st = std::dynamic_pointer_cast<struct_type>(tgt_sub_nc);
        if (src_st && tgt_st && src_st->get_struct() && tgt_st->get_struct() &&
            src_st->get_struct()->is_derived_from(tgt_st->get_struct())) {
            auto upcast = cast_expression::make_shared(expr, type_nc);
            upcast->set_type(type_nc);
            return upcast;
        }
        return {};
    }
    // drain<T> → ref<T>: implicit cast (drain is a superset of reference)
    if (type::is_reference(type_nc)) {
        auto tgt_sub_nc = type::remove_const(std::dynamic_pointer_cast<reference_type>(type_nc)->get_subtype());
        if (src_sub_nc == tgt_sub_nc || types_match_array_const_compatible(src_sub_nc, tgt_sub_nc)) {
            auto cast = cast_expression::make_shared(expr, type_nc);
            cast->set_type(type_nc);
            return cast;
        }
        // Struct upcast: drain<Derived> → ref<Base>
        auto src_st = std::dynamic_pointer_cast<struct_type>(src_sub_nc);
        auto tgt_st = std::dynamic_pointer_cast<struct_type>(tgt_sub_nc);
        if (src_st && tgt_st && src_st->get_struct() && tgt_st->get_struct() &&
            src_st->get_struct()->is_derived_from(tgt_st->get_struct())) {
            auto upcast = cast_expression::make_shared(expr, type_nc);
            upcast->set_type(type_nc);
            return upcast;
        }
        return {};
    }
    // drain<T> → link<T>, view<T>, ptr<T>: implicit borrow
    if (type::is_link(type_nc) || type::is_view(type_nc) || type::is_pointer(type_nc)) {
        auto tgt_sub_nc = type::remove_const(type_nc->get_subtype());
        if (src_sub_nc == tgt_sub_nc) {
            auto cast = cast_expression::make_shared(expr, type_nc);
            cast->set_type(type_nc);
            return cast;
        }
        return {};
    }
    // drain<T> → value T: load through drain (like ref → value)
    if (src_sub_nc == type_nc) {
        auto loaded = load_value_expression::make_shared(expr);
        loaded->set_type(src_sub_nc);
        return loaded;
    }
    // drain<primA> → primB: load first, then cast
    auto prim_drn_sub = std::dynamic_pointer_cast<primitive_type>(src_sub_nc);
    auto prim_drn_tgt = std::dynamic_pointer_cast<primitive_type>(type_nc);
    if (prim_drn_sub && prim_drn_tgt) {
        auto loaded = load_value_expression::make_shared(expr);
        loaded->set_type(src_sub_nc);
        if (*prim_drn_sub == *prim_drn_tgt) return loaded;
        auto cast = cast_expression::make_shared(loaded, prim_drn_tgt);
        cast->set_type(prim_drn_tgt);
        return cast;
    }
    return {};
}


// ── adapt_from_ref_owner ─────────────────────────────────────────────────────

std::shared_ptr<expression>
type_reference_resolver::adapt_from_ref_owner(
    std::shared_ptr<expression> expr,
    const std::shared_ptr<type>& type_src,
    const std::shared_ptr<type>& type_nc)
{
    auto ref_src = std::dynamic_pointer_cast<reference_type>(type_src);
    auto inner = ref_src->get_subtype();
    auto inner_nc = type::remove_const(inner);
    auto own_sub_nc = type::remove_const(inner_nc->get_subtype());

    // ref<owner<T>> → owner<T>: move ownership (load + null source)
    if (type::is_owner(type_nc)) {
        auto tgt_sub_nc = type::remove_const(type_nc->get_subtype());
        if (type::are_equal(own_sub_nc, tgt_sub_nc)) {
            auto move = owner_move_expression::make_shared(expr);
            move->set_type(inner);  // owner<T>
            return move;
        }
        // Upcast: ref<owner<Derived>> → owner<Base>
        auto src_st = std::dynamic_pointer_cast<struct_type>(own_sub_nc);
        auto tgt_st = std::dynamic_pointer_cast<struct_type>(tgt_sub_nc);
        if (src_st && tgt_st && src_st->get_struct() && tgt_st->get_struct() &&
            src_st->get_struct()->is_derived_from(tgt_st->get_struct())) {
            auto move = owner_move_expression::make_shared(expr);
            move->set_type(inner);  // owner<Derived>
            auto upcast = cast_expression::make_shared(move, type_nc);
            upcast->set_type(type_nc);
            return upcast;
        }
    }
    if (type::is_pointer(type_nc)) {
        auto tgt_sub_nc = type::remove_const(type_nc->get_subtype());
        if (own_sub_nc == tgt_sub_nc || types_match_array_const_compatible(own_sub_nc, tgt_sub_nc)) {
            // Load the stored pointer from the owner slot
            auto loaded = load_value_expression::make_shared(expr);
            loaded->set_type(inner_nc);
            // Reinterpret as pointer
            auto cast = cast_expression::make_shared(loaded, type_nc);
            cast->set_type(type_nc);
            return cast;
        }
        // Upcast variant
        auto src_st = std::dynamic_pointer_cast<struct_type>(own_sub_nc);
        auto tgt_st = std::dynamic_pointer_cast<struct_type>(tgt_sub_nc);
        if (src_st && tgt_st && src_st->get_struct() && tgt_st->get_struct() &&
            src_st->get_struct()->is_derived_from(tgt_st->get_struct())) {
            auto loaded = load_value_expression::make_shared(expr);
            loaded->set_type(inner_nc);
            auto upcast = cast_expression::make_shared(loaded, type_nc);
            upcast->set_type(type_nc);
            return upcast;
        }
    }
    // ref<owner<T>> → lnk<T> / view<T>: load owner, borrow as link or view
    if (type::is_link(type_nc) || type::is_view(type_nc)) {
        auto tgt_sub_nc = type::remove_const(type_nc->get_subtype());
        if (own_sub_nc == tgt_sub_nc || types_match_array_const_compatible(own_sub_nc, tgt_sub_nc)) {
            auto loaded = load_value_expression::make_shared(expr);
            loaded->set_type(inner_nc);
            auto cast = cast_expression::make_shared(loaded, type_nc);
            cast->set_type(type_nc);
            return cast;
        }
    }
    // ref<owner<T>> → ref<T>: load owner pointer value, borrow as reference
    if (type::is_reference(type_nc)) {
        auto tgt_sub_nc = type::remove_const(std::dynamic_pointer_cast<reference_type>(type_nc)->get_subtype());
        if (own_sub_nc == tgt_sub_nc || types_match_array_const_compatible(own_sub_nc, tgt_sub_nc)) {
            auto loaded = load_value_expression::make_shared(expr);
            loaded->set_type(inner_nc);  // owner<T>
            auto cast = cast_expression::make_shared(loaded, type_nc);
            cast->set_type(type_nc);
            return cast;
        }
    }
    return {};
}


// ── adapt_from_reference ─────────────────────────────────────────────────────

std::shared_ptr<expression>
type_reference_resolver::adapt_from_reference(
    std::shared_ptr<expression> expr,
    const std::shared_ptr<type>& type_src,
    const std::shared_ptr<type>& type_nc,
    const std::shared_ptr<type>& type_orig)
{
    // ── ref<ptr/lnk/view<T>> → ptr/lnk/pin<Base>: load the stored pointer then upcast ──
    // ── ref<ptr/lnk/view<T>> → ref<T>: load the indirection value, use as reference ──
    if (!type::is_reference(type_nc)) {
        auto ref_src = std::dynamic_pointer_cast<reference_type>(type_src);
        auto inner = ref_src->get_subtype();
        if ((type::is_pointer(inner) || type::is_link(inner) || type::is_view(inner)) &&
            (type::is_pointer(type_nc) || type::is_link(type_nc) || type::is_view(type_nc))) {
            // Load the pointer value stored in the ref slot
            auto loaded = load_value_expression::make_shared(expr);
            loaded->set_type(inner);
            // Now adapt the loaded indirection to the target indirection type
            auto adapted = adapt_type(loaded, type_nc);
            return adapted ? adapted : loaded;
        }
    } else {
        // ref<ptr/lnk/view<T>> → ref<T>: load indirection value, reinterpret as reference
        auto ref_src = std::dynamic_pointer_cast<reference_type>(type_src);
        auto inner = ref_src->get_subtype();
        auto inner_nc = type::remove_const(inner);
        if (type::is_pointer(inner_nc) || type::is_link(inner_nc) || type::is_view(inner_nc)) {
            auto tgt_ref = std::dynamic_pointer_cast<reference_type>(type_nc);
            auto tgt_sub_nc = type::remove_const(tgt_ref->get_subtype());
            auto src_sub_nc = type::remove_const(inner_nc->get_subtype());
            if (src_sub_nc == tgt_sub_nc || types_match_array_const_compatible(src_sub_nc, tgt_sub_nc)) {
                auto loaded = load_value_expression::make_shared(expr);
                loaded->set_type(inner_nc);
                auto cast = cast_expression::make_shared(loaded, type_nc);
                cast->set_type(type_nc);
                return cast;
            }
        }
    }

    if (type::is_reference(type_nc)) {
        if (type_nc == type_src) {
            // Reference to same type, return the expression
            return expr;
        }
        auto src_ref = std::dynamic_pointer_cast<reference_type>(type_src);
        auto tgt_ref = std::dynamic_pointer_cast<reference_type>(type_nc);
        auto src_sub = src_ref->get_referenced_type();
        auto tgt_sub = tgt_ref->get_referenced_type();
        auto src_sub_nc = type::remove_const(src_sub);
        auto tgt_sub_nc = type::remove_const(tgt_sub);
        // Exact match: ref<T> → ref<T> (structural equality, not just pointer identity)
        if (type::are_equal(src_sub_nc, tgt_sub_nc) && type::is_const(src_sub) == type::is_const(tgt_sub)) {
            return expr;
        }
        // Mutable → const widening: ref<T> → ref<const T>
        if (src_sub_nc == tgt_sub_nc && type::is_const(tgt_sub) && !type::is_const(src_sub)) {
            // Bitwise-identical at IR level (just a different type annotation).
            // Wrap in a cast so implementation_generator passes the right LLVM type.
            auto cast = cast_expression::make_shared(expr, type_nc);
            cast->set_type(type_nc);
            return cast;
        }
        // Array element const-widening: ref<array<T>> → ref<array<const<T>>>
        // At IR level this is a no-op (same pointer).
        if (types_match_array_const_compatible(src_sub_nc, tgt_sub_nc)) {
            auto cast = cast_expression::make_shared(expr, type_nc);
            cast->set_type(type_nc);
            return cast;
        }
        // Upcast: ref<Derived> → ref<Base> (also handles const variants on both sides)
        auto src_st_type = std::dynamic_pointer_cast<struct_type>(src_sub_nc);
        auto tgt_st_type = std::dynamic_pointer_cast<struct_type>(tgt_sub_nc);
        if (src_st_type && tgt_st_type) {
            auto src_st = src_st_type->get_struct();
            auto tgt_st = tgt_st_type->get_struct();
            if (src_st && tgt_st && src_st->is_derived_from(tgt_st)) {
                auto upcast = cast_expression::make_shared(expr, type_nc);
                upcast->set_type(type_nc);
                return upcast;
            }
        }
        // Sized→unsized array widening: ref<T[N]> → ref<T[]>
        // At LLVM IR level both are opaque pointers, so this is a no-op cast.
        if (auto src_sized = std::dynamic_pointer_cast<sized_array_type>(src_sub_nc)) {
            auto tgt_arr = std::dynamic_pointer_cast<array_type>(tgt_sub_nc);
            if (tgt_arr && !tgt_arr->is_sized()) {
                auto src_elem = type::remove_const(src_sized->get_subtype());
                auto tgt_elem = type::remove_const(tgt_arr->get_subtype());
                if (src_elem == tgt_elem) {
                    auto cast = cast_expression::make_shared(expr, type_nc);
                    cast->set_type(type_nc);
                    return cast;
                }
            }
        }
        return {};
    }
    auto ref_src = std::dynamic_pointer_cast<reference_type>(type_src);
    auto ref_subtype = type::remove_const(ref_src->get_subtype());
    // ── Owner move: ref<owner<T>> → owner<T> ─────────────────────────────
    // Transfer of ownership: load raw ptr AND null out the source alloca.
    if (type::is_owner(ref_subtype) && type::is_owner(type_nc)) {
        auto own_src_nc = type::remove_const(ref_subtype->get_subtype());
        auto own_tgt_nc = type::remove_const(type_nc->get_subtype());
        if (type::are_equal(own_src_nc, own_tgt_nc)) {
            auto move = owner_move_expression::make_shared(expr);
            move->set_type(type_nc);
            return move;
        }
        // Upcast owner: ref<owner<Derived>> → owner<Base>
        auto src_st = std::dynamic_pointer_cast<struct_type>(own_src_nc);
        auto tgt_st = std::dynamic_pointer_cast<struct_type>(own_tgt_nc);
        if (src_st && tgt_st && src_st->get_struct() && tgt_st->get_struct() &&
            src_st->get_struct()->is_derived_from(tgt_st->get_struct())) {
            auto move = owner_move_expression::make_shared(expr);
            move->set_type(ref_subtype); // owner<Derived>
            auto upcast = cast_expression::make_shared(move, type_nc);
            upcast->set_type(type_nc);   // owner<Base>
            return upcast;
        }
        return {}; // incompatible owner types
    }
    // ─────────────────────────────────────────────────────────────────────
    if (ref_subtype == type_nc) {
        // ref<T> -> T : simple load
        return adapt_reference_load_value(expr);
    }
    // ref<drain<T>> → T/ref<T>/link<T>/...: load the drain, then adapt it
    if (type::is_drain(ref_subtype)) {
        auto loaded = load_value_expression::make_shared(expr);
        loaded->set_type(ref_subtype);
        return adapt_type(loaded, type_nc);
    }
    // ref<T> → link<T> or ref<T> → view<T>: pass the address directly (LLVM ptr is compatible)
    if (type::is_link(type_nc) || type::is_view(type_nc)) {
        auto tgt_sub_nc = type::remove_const(type_nc->get_subtype());
        if (ref_subtype == tgt_sub_nc) {
            // Same underlying type: the ref address IS the link address — no conversion needed.
            // Wrap in cast to change the K type annotation without emitting any IR cast.
            auto cast = cast_expression::make_shared(expr, type_nc);
            cast->set_type(type_nc);
            return cast;
        }
        // Struct upcast: ref<Derived> → link<Base>
        auto src_st = std::dynamic_pointer_cast<struct_type>(ref_subtype);
        auto tgt_st = std::dynamic_pointer_cast<struct_type>(tgt_sub_nc);
        if (src_st && tgt_st && src_st->get_struct() && tgt_st->get_struct() &&
            src_st->get_struct()->is_derived_from(tgt_st->get_struct())) {
            auto cast = cast_expression::make_shared(expr, type_nc);
            cast->set_type(type_nc);
            return cast;
        }
    }
    // ref<primA> -> primB : load first, then cast between primitives
    auto prim_sub = std::dynamic_pointer_cast<primitive_type>(ref_subtype);
    auto prim_tgt = std::dynamic_pointer_cast<primitive_type>(type_nc);
    if (prim_sub && prim_tgt) {
        auto loaded = adapt_reference_load_value(expr);
        if (!loaded) return {};
        if (*prim_sub == *prim_tgt) return loaded;
        auto cast = cast_expression::make_shared(loaded, prim_tgt);
        cast->set_type(prim_tgt);
        return cast;
    }
    // ref<enum> → enum or ref<enum> → primitive: load first, then adapt
    auto ref_enum_sub = std::dynamic_pointer_cast<enum_type>(ref_subtype);
    if (ref_enum_sub) {
        auto loaded = adapt_reference_load_value(expr);
        if (!loaded) return {};
        return adapt_type(loaded, type_nc);
    }
    // ref<indirection> → bool: load the pointer then compare to null.
    if (type::is_prim_bool(type_nc)) {
        if (type::is_pointer(ref_subtype) || type::is_link(ref_subtype) ||
            type::is_view(ref_subtype) || type::is_owner(ref_subtype)) {
            auto loaded = adapt_reference_load_value(expr);
            if (!loaded) return {};
            auto bool_type = _context->from_type(primitive_type::BOOL);
            auto cast = cast_expression::make_shared(loaded, bool_type);
            cast->set_type(bool_type);
            return cast;
        }
    }
    return {};
}


// ── adapt_enum_type ──────────────────────────────────────────────────────────

std::shared_ptr<expression>
type_reference_resolver::adapt_enum_type(
    std::shared_ptr<expression> expr,
    const std::shared_ptr<type>& type_nc)
{
    auto enum_src = std::dynamic_pointer_cast<enum_type>(type::remove_const(expr->get_type()));
    auto enum_tgt = std::dynamic_pointer_cast<enum_type>(type_nc);

    // enum → enum (same enum): identity
    if (enum_src && enum_tgt && enum_src->get_enumeration() == enum_tgt->get_enumeration()) {
        return expr;
    }

    // enum → enum (different enums): allowed with warning (both primitive-backed)
    if (enum_src && enum_tgt && enum_src->get_enumeration() != enum_tgt->get_enumeration()) {
        // Implicit conversion between different enum types — emit a warning
        // TODO: emit a warning diagnostic here
        auto cast = cast_expression::make_shared(expr, enum_tgt);
        cast->set_type(enum_tgt);
        return cast;
    }

    // enum → primitive int: implicit (use underlying type)
    if (enum_src && !enum_tgt) {
        auto prim_tgt = std::dynamic_pointer_cast<primitive_type>(type_nc);
        if (prim_tgt) {
            auto underlying = enum_src->get_underlying_type();
            if (*underlying == *prim_tgt) {
                // Same underlying type: just reinterpret
                auto cast = cast_expression::make_shared(expr, prim_tgt);
                cast->set_type(prim_tgt);
                return cast;
            }
            // Different primitive widths: cast through underlying
            auto cast = cast_expression::make_shared(expr, prim_tgt);
            cast->set_type(prim_tgt);
            return cast;
        }
    }

    // primitive int → enum: implicit
    if (!enum_src && enum_tgt) {
        auto prim_src = std::dynamic_pointer_cast<primitive_type>(type::remove_const(expr->get_type()));
        if (prim_src) {
            auto cast = cast_expression::make_shared(expr, enum_tgt);
            cast->set_type(enum_tgt);
            return cast;
        }
    }

    return nullptr; // not an enum conversion — let caller continue
}


// ── adapt_primitive_or_struct_type ───────────────────────────────────────────

std::shared_ptr<expression>
type_reference_resolver::adapt_primitive_or_struct_type(
    std::shared_ptr<expression> expr,
    const std::shared_ptr<type>& type_nc)
{
    auto prim_src = std::dynamic_pointer_cast<primitive_type>(type::remove_const(expr->get_type()));
    auto prim_tgt = std::dynamic_pointer_cast<primitive_type>(type_nc);

    if (!prim_src || !prim_tgt) {
        // For non-primitive (struct/class) value types: accept if they are the same type object.
        auto src_nc = type::remove_const(expr->get_type());
        if (src_nc == type_nc) return expr;
        // Also accept struct-type upcast by value (same struct_type ptr = same type).
        if (auto src_st = std::dynamic_pointer_cast<struct_type>(src_nc)) {
            if (auto tgt_st = std::dynamic_pointer_cast<struct_type>(type_nc)) {
                if (src_st.get() == tgt_st.get()) return expr;
            }
        }
        return {};
    }

    if (*prim_src == *prim_tgt) {
        return expr;
    }

    auto cast = cast_expression::make_shared(expr, prim_tgt);
    cast->set_type(prim_tgt);
    return cast;
}


} // namespace k::model::gen

