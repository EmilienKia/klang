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
#include "gen_callable_helpers.hpp"
#include "gen_helpers.hpp"

#include "../model/expressions.hpp"
#include "../model/constant_evaluator.hpp"
#include "../errors.hpp"

namespace k::model::gen {

namespace {

/**
 * Detect the generic opaque pointer type: pointer<byte>.
 * This is what type parameter T maps to in synthesized generic code.
 */
bool is_generic_opaque_ptr(const std::shared_ptr<type>& t) {
    auto ptr = std::dynamic_pointer_cast<pointer_type>(type::remove_const(t));
    if (!ptr) return false;
    auto inner = type::remove_const(ptr->get_subtype());
    auto prim = std::dynamic_pointer_cast<primitive_type>(inner);
    return prim && prim->get_type() == primitive_type::BYTE;
}

/**
 * Check if a type conversion represents generic erasure:
 * ConcreteClass ↔ pointer<byte> (the opaque representation of T).
 * Used when comparing sub-types inside addressers (owner, ptr, link, view).
 */
bool is_generic_erasure_pair(const std::shared_ptr<type>& a, const std::shared_ptr<type>& b) {
    auto a_nc = type::remove_const(a);
    auto b_nc = type::remove_const(b);
    // a is concrete class, b is the opaque byte*
    if (std::dynamic_pointer_cast<struct_type>(a_nc) && is_generic_opaque_ptr(b_nc))
        return true;
    // a is the opaque byte*, b is concrete class
    if (is_generic_opaque_ptr(a_nc) && std::dynamic_pointer_cast<struct_type>(b_nc))
        return true;
    return false;
}

} // anonymous namespace


// ── adapt_callable_type ──────────────────────────────────────────────────

std::shared_ptr<expression>
/**
 * Adapt function reference types (frt → frt, ref<frt> → frt, etc.).
 *
 * Steps:
 *   1. frt → frt with different addresser: cast_expression to reinterpret.
 *   2. ref<frt> → frt: load_value_expression to dereference.
 *   3. frt → ref<frt>: no direct conversion possible.
 *
 * @return The adapted expression, or nullptr if not a function reference case.
 */
type_reference_resolver::adapt_callable_type(
    std::shared_ptr<expression> expr,
    const std::shared_ptr<type>& type_src,
    const std::shared_ptr<type>& type_nc)
{
    // ── null → callable ──────────────────────────────────────────────────────
    // `null` is a valid target only for a nullable callable (`*` or `?`); it
    // materialises the zeroed `{ null, null }` fat value.
    if (type::is_null(type_src)) {
        auto dest_ct = std::dynamic_pointer_cast<callable_type>(type_nc);
        if (!dest_ct && type::is_reference(type_nc)) {
            dest_ct = std::dynamic_pointer_cast<callable_type>(
                type::remove_const(std::dynamic_pointer_cast<reference_type>(type_nc)->get_subtype()));
        }
        if (dest_ct && !dest_ct->is_unbound_member()) {
            if (!dest_ct->is_nullable()) {
                throw_error(static_cast<unsigned int>(k::diag::callable_diag::ERR_CALLABLE_NULL_TO_NONNULL),
                    expr ? expr->first_lexeme() : lex::opt_any_lexeme{},
                    "'null' cannot be assigned to the non-null callable type '{}'; "
                    "only a pointer (*) or view (?) callable may hold null",
                    {dest_ct->to_string()});
            }
            auto cast = cast_expression::make_shared(expr, dest_ct);
            cast->set_type(dest_ct);
            return cast;
        }
    }

    // Step 1: frt → frt with different addresser: cast_expression to reinterpret
    // Case 1: target is a bare callable_type
    // Step 2: ref<frt> → frt: load_value_expression to dereference
    // A member function designated without call parentheses (`obj.method`,
    // `ptr->method`, a bare `method` inside a member function) binds its receiver.
    // The destination may be the callable itself or a reference to it (assignment
    // to a callable-typed variable or data member).
    {
        auto dest_ct = std::dynamic_pointer_cast<callable_type>(type_nc);
        if (!dest_ct && type::is_reference(type_nc)) {
            dest_ct = std::dynamic_pointer_cast<callable_type>(
                type::remove_const(std::dynamic_pointer_cast<reference_type>(type_nc)->get_subtype()));
        }
        if (dest_ct && !dest_ct->is_unbound_member() && !dest_ct->is_prototype()) {
            if (auto lambda = std::dynamic_pointer_cast<lambda_expression>(expr)) {
                auto bind = lambda->bind();
                if (bind) {
                    if (auto target = bind->get_target()) {
                        if (!target->has_return_type() && dest_ct->get_return_type()) {
                            target->set_return_type(dest_ct->get_return_type());
                        }
                    }
                    bind->set_type(dest_ct);
                    lambda->set_type(dest_ct);
                    if (dest_ct->is_owner() && bind->get_context()) {
                        if (auto ast_lambda = lambda->get_ast_node_as<parse::ast::lambda_expression>()) {
                            for (const auto& cap : ast_lambda->captures) {
                                if (cap.is_reference && cap.name && !cap.is_this) {
                                    throw_error(static_cast<unsigned int>(k::diag::callable_model_diag::ERR_LAMBDA_OWNED_CAPTURE_LOCAL_REF),
                                        expr ? expr->first_lexeme() : lex::opt_any_lexeme{},
                                        "An owned lambda ('!') cannot capture local variable '{}' by reference; capture it by value instead",
                                        {std::string{cap.name->content}});
                                }
                            }
                        }
                        if (auto tmp = std::dynamic_pointer_cast<temporary_construction_expression>(bind->get_context())) {
                            auto new_expr = new_expression::make_shared(tmp->constructed_type(), tmp->arguments());
                            new_expr->set_type(tmp->constructed_type()->get_owner());
                            new_expr->accept(*this);
                            bind->set_context(new_expr);
                        }
                    } else if (bind->get_context()) {
                        bind->get_context()->accept(*this);
                    }
                    return bind;
                }
            }
            if (auto bound = try_bind_member_callable(expr, dest_ct)) return bound;
            if (auto bound = try_bind_functor_callable(expr, dest_ct)) return bound;
            if (auto bound = try_bind_functional_interface_callable(expr, dest_ct)) return bound;
        }
    }
    if (auto tgt_frt = std::dynamic_pointer_cast<callable_type>(type_nc)) {
        if (auto src_frt = std::dynamic_pointer_cast<callable_type>(type_src)) {
            // Source is already a bare frt — no load needed, but the prototypes must
            // still satisfy the co/contravariance rules (phase B.7).
            if (!tgt_frt->is_unbound_member() && !src_frt->is_unbound_member()) {
                check_callable_conversion(src_frt, tgt_frt, expr ? expr->first_lexeme() : lex::opt_any_lexeme{});
            }
            return expr;
        }
        if (type::is_reference(type_src)) {
            auto src_inner = type::remove_const(std::dynamic_pointer_cast<reference_type>(type_src)->get_subtype());
            if (auto src_frt = std::dynamic_pointer_cast<callable_type>(src_inner)) {
                // Direct function symbol: bind it into a callable value.
                auto sym = std::dynamic_pointer_cast<symbol_expression>(expr);
                if (sym && sym->is_function()) {
                    if (tgt_frt->is_unbound_member()) {
                        // Unbound member function reference: keeps the historical bare
                        // function-pointer representation, produced directly by impl_gen.
                        return expr;
                    }
                    auto fn = sym->get_function();
                    if (!fn) return expr;
                    // Symbol resolution picks the first declaration carrying that name,
                    // which is declaration-order dependent: re-select the overload whose
                    // prototype is compatible with the destination, and reject the
                    // binding outright when none is.
                    std::vector<std::shared_ptr<function>> cands{fn};
                    if (auto holder = fn->parent<element>()) {
                        if (auto* fh = dynamic_cast<function_holder*>(holder.get())) {
                            auto found = fh->get_functions(fn->get_short_name());
                            if (!found.empty()) cands = found;
                        }
                    }
                    fn = select_callable_target(cands, tgt_frt, fn->get_short_name(),
                                                expr->first_lexeme());
                    auto bind = callable_bind_expression::make_shared(
                        fn->is_static() || !fn->parent<aggregate>()
                            ? (fn->parent<aggregate>() ? callable_bind_expression::kind::static_method
                                                       : callable_bind_expression::kind::free_function)
                            : callable_bind_expression::kind::bound_method,
                        fn);
                    bind->set_type(tgt_frt);
                    return bind;
                }
                // Variable holding a callable: load or move the stored value from the alloca.
                if (!tgt_frt->is_unbound_member() && !src_frt->is_unbound_member()) {
                    check_callable_conversion(src_frt, tgt_frt, expr->first_lexeme());
                }
                if (tgt_frt->is_owner() && src_frt->is_owner()) {
                    auto move = owner_move_expression::make_shared(expr);
                    move->set_type(tgt_frt);
                    return move;
                }
                return adapt_reference_load_value(expr);
            }
        }
        return {};
    }
    // Case 2: source is ref<frt> and target is also ref<frt> — pass through unchanged.
    if (type::is_reference(type_nc)) {
        auto tgt_sub_nc = type::remove_const(std::dynamic_pointer_cast<reference_type>(type_nc)->get_subtype());
        if (std::dynamic_pointer_cast<callable_type>(tgt_sub_nc)) {
            if (type_src == type_nc) return expr;
            auto src_sub = type::is_reference(type_src)
                ? std::dynamic_pointer_cast<reference_type>(type_src)->get_subtype()
                : type_src;
            if (std::dynamic_pointer_cast<callable_type>(type::remove_const(src_sub))) {
                // Step 3: frt → ref<frt>: no direct conversion possible
                return expr; // compatible frt ref
            }
        }
    }
    return nullptr; // not handled — let caller continue
}


// ── adapt_from_pointer ───────────────────────────────────────────────────────

std::shared_ptr<expression>
/**
 * Adapt when source is a pointer type (ptr<T> → ptr/lnk/view/ref).
 *
 * Steps:
 *   1. ptr<T> → ptr<T>/lnk<T>: identity or const-widening, struct upcast if needed.
 *   2. ptr<T> → ref<T>: dereference pointer to obtain reference to pointed object.
 *
 * @return The adapted expression, or nullptr if conversion is impossible.
 */
type_reference_resolver::adapt_from_pointer(
    std::shared_ptr<expression> expr,
    const std::shared_ptr<type>& type_src,
    const std::shared_ptr<type>& type_nc)
{
    // Step 1: ptr<T> → ptr<T>/lnk<T>: identity or const-widening, struct upcast if needed
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
                auto src_agg = src_st_type->get_struct();
                auto tgt_agg = tgt_st_type->get_struct();
                if (src_agg && tgt_agg && src_agg->is_derived_from(tgt_agg)) {
                    auto upcast = cast_expression::make_shared(expr, type_nc);
                    upcast->set_type(type_nc);
                    return upcast;
                }
            }
            // Union pointer to alternative or polymorphic base
            if (src_st_type && !src_st_type->get_struct()) {
                auto root_ns = _unit.get_root_namespace();
                if (auto udef = find_union_by_struct_type(root_ns, src_st_type)) {
                    auto tgt_st = std::dynamic_pointer_cast<struct_type>(tgt_sub_nc);
                    if (udef->is_polymorphic() && udef->get_polymorphic_base()) {
                        auto poly_base = udef->get_polymorphic_base();
                        if (poly_base->get_struct_type() && (tgt_st == poly_base->get_struct_type() ||
                            (tgt_st && tgt_st->get_struct() && poly_base->is_derived_from(tgt_st->get_struct())))) {
                            auto cast = cast_expression::make_shared(expr, type_nc, type::is_link(type_nc) || type::is_reference(type_nc));
                            cast->set_type(type_nc);
                            return cast;
                        }
                    }
                    std::vector<const union_alternative*> matches;
                    for (const auto* alt : udef->all_alternatives_ptrs()) {
                        if (alt->resolved_type && type::are_equal(type::remove_const(alt->resolved_type), tgt_sub_nc)) {
                            matches.push_back(alt);
                        }
                    }
                    if (matches.size() == 1) {
                        auto cast = cast_expression::make_shared(expr, type_nc, type::is_link(type_nc) || type::is_reference(type_nc));
                        cast->set_type(type_nc);
                        return cast;
                    }
                }
            }
            // Generic erasure: ptr<Concrete> ↔ ptr<byte*> or ptr<byte*> ↔ ptr<Concrete>
            if (is_generic_erasure_pair(src_sub_nc, tgt_sub_nc)) {
                auto cast = cast_expression::make_shared(expr, type_nc);
                cast->set_type(type_nc);
                return cast;
            }
            // Sized→unsized array widening: ptr<T[N]> → ptr<T[]>/lnk<T[]>
            if (types_match_array_const_compatible(src_sub_nc, tgt_sub_nc)) {
                auto cast = cast_expression::make_shared(expr, type_nc);
                cast->set_type(type_nc);
                return cast;
            }
            return {};
        }
        return expr;
    } else {
        // Step 2: ptr<T> → ref<T>: dereference pointer to obtain reference to pointed object
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
/**
 * Adapt when source is a link type (lnk<T> → lnk/ptr/view/ref).
 *
 * Steps:
 *   1. lnk<T> → lnk<T>/ptr<T>/view<T>: cast with const-check and struct upcast.
 *   2. lnk<T> → ref<T>: borrow link target as reference.
 *
 * @return The adapted expression, or nullptr if conversion is impossible.
 */
type_reference_resolver::adapt_from_link(
    std::shared_ptr<expression> expr,
    const std::shared_ptr<type>& type_src,
    const std::shared_ptr<type>& type_nc)
{
    // Step 1: lnk<T> → lnk<T>/ptr<T>/view<T>: cast with const-check and struct upcast
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
            // Union link to alternative or polymorphic base
            if (src_st_type && !src_st_type->get_struct()) {
                auto root_ns = _unit.get_root_namespace();
                if (auto udef = find_union_by_struct_type(root_ns, src_st_type)) {
                    auto tgt_st = std::dynamic_pointer_cast<struct_type>(tgt_sub_nc);
                    if (udef->is_polymorphic() && udef->get_polymorphic_base()) {
                        auto poly_base = udef->get_polymorphic_base();
                        if (poly_base->get_struct_type() && (tgt_st == poly_base->get_struct_type() ||
                            (tgt_st && tgt_st->get_struct() && poly_base->is_derived_from(tgt_st->get_struct())))) {
                            auto cast = cast_expression::make_shared(expr, type_nc, type::is_link(type_nc) || type::is_reference(type_nc));
                            cast->set_type(type_nc);
                            return cast;
                        }
                    }
                    std::vector<const union_alternative*> matches;
                    for (const auto* alt : udef->all_alternatives_ptrs()) {
                        if (alt->resolved_type && type::are_equal(type::remove_const(alt->resolved_type), tgt_sub_nc)) {
                            matches.push_back(alt);
                        }
                    }
                    if (matches.size() == 1) {
                        auto cast = cast_expression::make_shared(expr, type_nc, type::is_link(type_nc) || type::is_reference(type_nc));
                        cast->set_type(type_nc);
                        return cast;
                    }
                }
            }
            // Sized→unsized array widening: lnk<T[N]> → lnk/ptr/view<T[]>
            if (types_match_array_const_compatible(src_sub_nc, tgt_sub_nc)) {
                auto cast = cast_expression::make_shared(expr, type_nc);
                cast->set_type(type_nc);
                return cast;
            }
            return {};
        }
        return expr;
    } else {
        // Step 2: lnk<T> → ref<T>: borrow link target as reference
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
/**
 * Adapt when source is a view type (view<T> → view/ptr/ref).
 *
 * Steps:
 *   1. view<T> → view<T>/ptr<T>: cast with const-check and struct upcast.
 *   2. view<T> → ref<T>: borrow view target as reference.
 *
 * @return The adapted expression, or nullptr if conversion is impossible.
 */
type_reference_resolver::adapt_from_view(
    std::shared_ptr<expression> expr,
    const std::shared_ptr<type>& type_src,
    const std::shared_ptr<type>& type_nc)
{
    // Step 1: view<T> → view<T>/ptr<T>: cast with const-check and struct upcast
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
            // Union view to alternative or polymorphic base
            if (src_st_type && !src_st_type->get_struct()) {
                auto root_ns = _unit.get_root_namespace();
                if (auto udef = find_union_by_struct_type(root_ns, src_st_type)) {
                    auto tgt_st = std::dynamic_pointer_cast<struct_type>(tgt_sub_nc);
                    if (udef->is_polymorphic() && udef->get_polymorphic_base()) {
                        auto poly_base = udef->get_polymorphic_base();
                        if (poly_base->get_struct_type() && (tgt_st == poly_base->get_struct_type() ||
                            (tgt_st && tgt_st->get_struct() && poly_base->is_derived_from(tgt_st->get_struct())))) {
                            auto cast = cast_expression::make_shared(expr, type_nc, type::is_link(type_nc) || type::is_reference(type_nc));
                            cast->set_type(type_nc);
                            return cast;
                        }
                    }
                    std::vector<const union_alternative*> matches;
                    for (const auto* alt : udef->all_alternatives_ptrs()) {
                        if (alt->resolved_type && type::are_equal(type::remove_const(alt->resolved_type), tgt_sub_nc)) {
                            matches.push_back(alt);
                        }
                    }
                    if (matches.size() == 1) {
                        auto cast = cast_expression::make_shared(expr, type_nc, type::is_link(type_nc) || type::is_reference(type_nc));
                        cast->set_type(type_nc);
                        return cast;
                    }
                }
            }
            // Sized→unsized array widening: view<T[N]> → view/ptr<T[]>
            if (types_match_array_const_compatible(src_sub_nc, tgt_sub_nc)) {
                auto cast = cast_expression::make_shared(expr, type_nc);
                cast->set_type(type_nc);
                return cast;
            }
            return {};
        }
        return expr;
    } else {
        // Step 2: view<T> → ref<T>: borrow view target as reference
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
/**
 * Adapt when source is an owner type (owner<T> → owner/ptr/lnk/view/ref).
 *
 * Steps:
 *   1. owner<T> → owner<T>: identity.
 *   2. owner<T> → ptr<T>/lnk<T>/view<T>: borrow as observer pointer/link/view.
 *   3. owner<T> → ref<T>: borrow owned object as reference.
 *   4. Struct upcast (owner<Derived> → ptr<Base>) where applicable.
 *
 * @return The adapted expression, or nullptr if conversion is impossible.
 */
type_reference_resolver::adapt_from_owner(
    std::shared_ptr<expression> expr,
    const std::shared_ptr<type>& type_src,
    const std::shared_ptr<type>& type_nc)
{
    // Step 1: owner<T> → owner<T>: identity
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
        // Generic erasure: owner<Concrete> ↔ owner<byte*> (no-op at IR level)
        if (is_generic_erasure_pair(src_sub_nc, tgt_sub_nc)) {
            auto cast = cast_expression::make_shared(expr, type_nc);
            cast->set_type(type_nc);
            return cast;
        }
        // Sized→unsized array widening: owner<T[N]> → owner<T[]>
        if (types_match_array_const_compatible(src_sub_nc, tgt_sub_nc)) {
            auto cast = cast_expression::make_shared(expr, type_nc);
            cast->set_type(type_nc);
            return cast;
        }
        return {};
    }
    if (type::is_pointer(type_nc)) {
        // Borrow as pointer observer — address is used, ownership stays in the owner
        auto tgt_sub_nc = type::remove_const(type_nc->get_subtype());
        if (src_sub_nc == tgt_sub_nc || types_match_array_const_compatible(src_sub_nc, tgt_sub_nc)) {
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
    // Step 2: owner<T> → ptr<T>/lnk<T>/view<T>: borrow as observer pointer/link/view
    // owner<T> → lnk<T> / view<T>: borrow as link or view (same LLVM representation)
    if (type::is_link(type_nc) || type::is_view(type_nc)) {
        auto tgt_sub_nc = type::remove_const(type_nc->get_subtype());
        if (src_sub_nc == tgt_sub_nc || types_match_array_const_compatible(src_sub_nc, tgt_sub_nc)) {
            auto cast = cast_expression::make_shared(expr, type_nc);
            cast->set_type(type_nc);
            return cast;
        }
    }
    // Step 3: owner<T> → ref<T>: borrow owned object as reference
    // owner<T> → ref<T>: borrow owned object as reference (same LLVM representation)
    if (type::is_reference(type_nc)) {
        auto tgt_sub_nc = type::remove_const(std::dynamic_pointer_cast<reference_type>(type_nc)->get_subtype());
        if (src_sub_nc == tgt_sub_nc || types_match_array_const_compatible(src_sub_nc, tgt_sub_nc)) {
            auto cast = cast_expression::make_shared(expr, type_nc);
            cast->set_type(type_nc);
            return cast;
        }
    }
    return {};
}


// ── adapt_from_drain ─────────────────────────────────────────────────────────

std::shared_ptr<expression>
/**
 * Adapt when source is a drain type (drain<T> → drain/ref/lnk/view/ptr/value).
 *
 * Steps:
 *   1. drain<T> → drain<T>: identity or struct upcast.
 *   2. drain<T> → ref<T>: implicit borrow (drain can always be used as a reference).
 *   3. drain<T> → link/view/ptr<T>: implicit borrow.
 *   4. drain<T> → T: load through drain.
 *   5. drain<T> → different primitive: load + cast.
 *
 * @return The adapted expression, or nullptr if conversion is impossible.
 */
type_reference_resolver::adapt_from_drain(
    std::shared_ptr<expression> expr,
    const std::shared_ptr<type>& type_src,
    const std::shared_ptr<type>& type_nc)
{
    auto drn = std::dynamic_pointer_cast<drain_type>(type_src);
    auto src_sub = drn->get_drained_type();
    auto src_sub_nc = type::remove_const(src_sub);

    // Step 1: drain<T> → drain<T>: identity or struct upcast
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
    // Step 2: drain<T> → ref<T>: implicit borrow (drain can always be used as a reference)
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
    // Step 3: drain<T> → link/view/ptr<T>: implicit borrow
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
    // Step 4: drain<T> → T: load through drain
    // drain<T> → value T: load through drain (like ref → value)
    if (src_sub_nc == type_nc) {
        auto loaded = load_value_expression::make_shared(expr);
        loaded->set_type(src_sub_nc);
        return loaded;
    }
    // Step 5: drain<T> → different primitive: load + cast
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
/**
 * Adapt ref<owner<T>> to owner/ptr/lnk/view/ref (owner borrow and move patterns).
 *
 * Steps:
 *   1. ref<owner<T>> → owner<T>: owner_move_expression (ownership transfer).
 *   2. ref<owner<T>> → ptr<T>/lnk<T>/view<T>: load owner, borrow as observer.
 *   3. ref<owner<T>> → ref<T>: load owner pointer, borrow as reference.
 *   4. Struct upcast variants for all of the above.
 *
 * @return The adapted expression, or nullptr if conversion is impossible.
 */
type_reference_resolver::adapt_from_ref_owner(
    std::shared_ptr<expression> expr,
    const std::shared_ptr<type>& type_src,
    const std::shared_ptr<type>& type_nc)
{
    auto ref_src = std::dynamic_pointer_cast<reference_type>(type_src);
    auto inner = ref_src->get_subtype();
    auto inner_nc = type::remove_const(inner);
    auto own_sub_nc = type::remove_const(inner_nc->get_subtype());

    // Step 1: ref<owner<T>> → owner<T>: owner_move_expression (ownership transfer)
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
        // Generic erasure: ref<owner<Concrete>> → owner<byte*> (move + bitcast)
        if (is_generic_erasure_pair(own_sub_nc, tgt_sub_nc)) {
            auto move = owner_move_expression::make_shared(expr);
            move->set_type(inner);  // owner<Concrete>
            auto cast = cast_expression::make_shared(move, type_nc);
            cast->set_type(type_nc);
            return cast;
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
    // Step 2: ref<owner<T>> → ptr<T>/lnk<T>/view<T>: load owner, borrow as observer
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
    // Step 3: ref<owner<T>> → ref<T>: load owner pointer, borrow as reference
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
/**
 * Adapt when source is a reference type (ref<T> → ref/value/lnk/view/owner + loads and casts).
 *
 * Steps:
 *   1. ref<T> → ref<T>: identity, const-widening, struct upcast.
 *   2. ref<T> → T: load_value_expression.
 *   3. ref<T> → lnk<T>/view<T>: borrow as link or view.
 *   4. ref<ptr/lnk/view<T>> → ptr/lnk/view<U>: unwrap ref, then delegate.
 *   5. ref<drain<T>> → various: unwrap drain, then convert.
 *   6. ref<Struct> → value Struct: load.
 *   7. ref<enum> → enum or underlying primitive: load + enum conversion.
 *   8. ref<primitive> → different primitive: load + widening/narrowing cast.
 *   9. ref<Derived> → ref<Base>: struct upcast.
 *   10. ref<T[N]> → ref<T[]>: sized array to unsized array widening.
 *
 * @return The adapted expression, or nullptr if conversion is impossible.
 */
type_reference_resolver::adapt_from_reference(
    std::shared_ptr<expression> expr,
    const std::shared_ptr<type>& type_src,
    const std::shared_ptr<type>& type_nc,
    const std::shared_ptr<type>& type_orig)
{
    auto ref_src_early = std::dynamic_pointer_cast<reference_type>(type_src);
    auto ref_subtype_early = ref_src_early ? type::remove_const(ref_src_early->get_subtype()) : nullptr;

    // ── ref<Union> → alternative T / ref<T> / ref<const T> / polymorphic Base& ──
    if (auto ref_src_st = std::dynamic_pointer_cast<struct_type>(ref_subtype_early)) {
        if (!ref_src_st->get_struct()) { // union type
            auto root_ns = _unit.get_root_namespace();
            if (auto udef = find_union_by_struct_type(root_ns, ref_src_st)) {
                auto bare_tgt = type::remove_const(type_nc);
                if (auto tgt_ref = std::dynamic_pointer_cast<reference_type>(bare_tgt)) {
                    bare_tgt = type::remove_const(tgt_ref->get_subtype());
                }

                // 1. Polymorphic union base target
                if (udef->is_polymorphic()) {
                    auto poly_base = udef->get_polymorphic_base();
                    if (poly_base && poly_base->get_struct_type()) {
                        auto poly_st = poly_base->get_struct_type();
                        auto tgt_st = std::dynamic_pointer_cast<struct_type>(bare_tgt);
                        if (tgt_st == poly_st || (tgt_st && tgt_st->get_struct() && poly_base->is_derived_from(tgt_st->get_struct()))) {
                            auto cast = cast_expression::make_shared(expr, type_nc, true);
                            cast->set_type(type_nc);
                            return cast;
                        }
                    }
                }

                // 2. Alternative target
                std::vector<const union_alternative*> matches;
                for (const auto* alt : udef->all_alternatives_ptrs()) {
                    if (!alt->resolved_type) continue;
                    auto alt_bare = type::remove_const(alt->resolved_type);
                    if (type::are_equal(alt_bare, bare_tgt)) {
                        matches.push_back(alt);
                    }
                }
                if (matches.size() == 1) {
                    auto cast = cast_expression::make_shared(expr, type_nc, true);
                    cast->set_type(type_nc);
                    return cast;
                } else if (matches.size() > 1) {
                    throw_error(static_cast<unsigned int>(k::diag::union_diag::ERR_UNION_AMBIGUOUS_IMPLICIT_ASSIGN), expr->first_lexeme(),
                        "Implicit conversion of union '{}' to '{}' is ambiguous: multiple alternatives have this type",
                        {udef->get_short_name(), type_nc->to_string()});
                }
            }
        }
    }

    // Step 1: ref<T> → ref<T>: identity, const-widening, struct upcast
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

    // Step 2: ref<T> → T: load_value_expression
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
    // Step 3: ref<T> → lnk<T>/view<T>: borrow as link or view
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
        if (loaded->is_constant()) {
            auto res = constant_evaluator::cast_to_type(loaded->get_constant_value(), prim_tgt);
            if (res) cast->set_constant_value(*res);
        }
        return cast;
    }
    // ref<enum> → enum or ref<enum> → primitive: load first, then adapt
    auto ref_enum_sub = std::dynamic_pointer_cast<enum_type>(ref_subtype);
    if (ref_enum_sub) {
        auto loaded = adapt_reference_load_value(expr);
        if (!loaded) return {};
        return adapt_type(loaded, type_nc);
    }

    // ref<struct> -> object-backed enum: allow direct cast without forcing a value load
    if (auto ref_struct_sub = std::dynamic_pointer_cast<struct_type>(type::remove_const(ref_subtype))) {
        if (auto enum_tgt = std::dynamic_pointer_cast<enum_type>(type_nc)) {
            if (enum_tgt->is_object_backed()) {
                auto obj_type = enum_tgt->get_object_type();
                if (obj_type && ref_struct_sub == obj_type) {
                    auto cast = cast_expression::make_shared(expr, enum_tgt);
                    cast->set_type(enum_tgt);
                    return cast;
                }
            }
        }
    }
    // ref<indirection> → bool: load the pointer then compare to null.
    // A callable behaves like an indirection here: it converts to `fn != null`.
    if (type::is_prim_bool(type_nc)) {
        if (type::is_pointer(ref_subtype) || type::is_link(ref_subtype) ||
            // Step 4: ref<ptr/lnk/view<T>> → ptr/lnk/view<U>: unwrap ref, then delegate
            type::is_view(ref_subtype) || type::is_owner(ref_subtype) ||
            type::is_fat_callable(ref_subtype)) {
            // Step 6: ref<Struct> → value Struct: load
            auto loaded = adapt_reference_load_value(expr);
            if (!loaded) return {};
            auto bool_type = _context->from_type(primitive_type::BOOL);
            // Step 8: ref<primitive> → different primitive: load + widening/narrowing cast
            auto cast = cast_expression::make_shared(loaded, bool_type);
            cast->set_type(bool_type);
            return cast;
        }
    }
    // ref<Struct> -> T via user-defined cast operator
    if (auto src_st = std::dynamic_pointer_cast<struct_type>(ref_subtype)) {
        auto src_agg = src_st->get_struct();
        if (src_agg) {
            bool is_const_this = type::is_const(ref_src->get_referenced_type());
            auto cast_func = resolve_cast_operator_overload(src_agg, type_nc, is_const_this);
            if (cast_func) {
                auto cast = std::dynamic_pointer_cast<cast_expression>(
                    cast_expression::make_shared(expr, type_nc));
                cast->set_operator_func(cast_func);
                if (cast_func->is_virtual() && cast_func->get_vtable_slot() >= 0) {
                    cast->set_operator_dispatch_info(compute_operator_dispatch_info(cast_func, type_src));
                }
                cast->set_type(type_nc);
                return cast;
            }
        }
    }
    return {};
}


// ── adapt_enum_type ──────────────────────────────────────────────────────────

std::shared_ptr<expression>
/**
 * Adapt enum conversions (enum ↔ enum, enum ↔ primitive).
 *
 * Steps:
 *   1. enum → same enum: identity.
 *   2. enum → derived enum (upcast): cast_expression.
 *   3. enum → underlying primitive or vice versa: cast_expression.
 *
 * @return The adapted expression, or nullptr if not an enum case.
 */
type_reference_resolver::adapt_enum_type(
    std::shared_ptr<expression> expr,
    const std::shared_ptr<type>& type_nc)
{
    // Step 1: enum → same enum: identity
    auto enum_src = std::dynamic_pointer_cast<enum_type>(type::remove_const(expr->get_type()));
    auto enum_tgt = std::dynamic_pointer_cast<enum_type>(type_nc);

    // enum → enum (same enum): identity
    if (enum_src && enum_tgt && enum_src->get_enumeration() == enum_tgt->get_enumeration()) {
        return expr;
    }

    // Step 2: enum → derived enum (upcast): cast_expression
    // enum → enum (different enums): allowed with warning (both primitive-backed)
    if (enum_src && enum_tgt && enum_src->get_enumeration() != enum_tgt->get_enumeration()) {
        // Implicit conversion between different enum types — emit a warning
        // TODO: emit a warning diagnostic here
        auto cast = cast_expression::make_shared(expr, enum_tgt);
        cast->set_type(enum_tgt);
        return cast;
    }

    // Step 3: enum → underlying primitive or vice versa: cast_expression
    // enum → primitive int: implicit (use underlying type)
    if (enum_src && !enum_tgt) {
        // ── Object-backed enum → const struct reference: GEP into backing table ──
        // Check if target is a reference (or const reference) to the backing object type
        if (enum_src->is_object_backed()) {
            auto obj_type = enum_src->get_object_type();
            auto tgt_ref = std::dynamic_pointer_cast<reference_type>(type_nc);
            if (!tgt_ref) {
                // Also check const_type wrapping a reference (const E &)
                if (auto tgt_const = std::dynamic_pointer_cast<const_type>(type_nc)) {
                    tgt_ref = std::dynamic_pointer_cast<reference_type>(tgt_const->get_subtype());
                }
            }
            if (tgt_ref && obj_type) {
                auto ref_inner_nc = type::remove_const(tgt_ref->get_subtype());
                if (ref_inner_nc == obj_type) {
                    // Cast: object-backed enum → const T& (backed by table GEP)
                    auto cast = cast_expression::make_shared(expr, type_nc);
                    cast->set_type(type_nc);
                    return cast;
                }
            }
        }

        auto prim_tgt = std::dynamic_pointer_cast<primitive_type>(type_nc);
        if (prim_tgt) {
            auto cast = cast_expression::make_shared(expr, prim_tgt);
            cast->set_type(prim_tgt);
            if (expr->is_constant()) {
                auto res = constant_evaluator::cast_to_type(expr->get_constant_value(), prim_tgt);
                if (res) cast->set_constant_value(*res);
            }
            return cast;
        }
    }

    // primitive int / object value → enum: implicit
    if (!enum_src && enum_tgt) {
        auto prim_src = std::dynamic_pointer_cast<primitive_type>(type::remove_const(expr->get_type()));
        if (prim_src) {
            auto cast = cast_expression::make_shared(expr, enum_tgt);
            cast->set_type(enum_tgt);
            if (expr->is_constant()) {
                auto res = constant_evaluator::cast_to_type(expr->get_constant_value(), enum_tgt);
                if (res) cast->set_constant_value(*res);
            }
            return cast;
        }

        // Object-backed enum: allow T (or const T&) -> E
        if (enum_tgt->is_object_backed()) {
            auto obj_type = enum_tgt->get_object_type();
            auto src_nc = type::remove_const(expr->get_type());

            auto src_st = std::dynamic_pointer_cast<struct_type>(src_nc);
            if (!src_st && type::is_reference(src_nc)) {
                auto src_ref = std::dynamic_pointer_cast<reference_type>(src_nc);
                src_st = std::dynamic_pointer_cast<struct_type>(
                    type::remove_const(src_ref->get_subtype()));
            }

            if (obj_type && src_st && src_st == obj_type) {
                auto agg = obj_type->get_struct();
                bool has_equality = false;
                if (agg) {
                    has_equality = (agg->get_function("equals") != nullptr)
                        || (agg->get_function("__operator_eq_") != nullptr)
                        || (agg->get_function("__operator_ne_") != nullptr);
                }
                if (!has_equality) {
                    throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_CAST_UNSUPPORTED), expr->first_lexeme(),
                        "Object-backed enum cast '{}' -> '{}' requires underlying type '{}' to define equality (equals/==/!=)",
                        {expr->get_type()->to_string(), enum_tgt->to_string(), obj_type->to_string()});
                }

                auto cast = cast_expression::make_shared(expr, enum_tgt);
                cast->set_type(enum_tgt);
                return cast;
            }
        }
    }

    return nullptr; // not an enum conversion — let caller continue
}


// ── adapt_primitive_or_struct_type ───────────────────────────────────────────

std::shared_ptr<expression>
/**
 * Adapt primitive-to-primitive or struct identity conversions (terminal fallback).
 *
 * Steps:
 *   1. Struct identity: same struct type → return as-is.
 *   2. Struct upcast: Derived → Base (value) → cast_expression.
 *   3. Same primitive: return as-is.
 *   4. Different primitives: insert cast_expression (widening or narrowing).
 *   5. 1-arg constructor: construct target type from source via CAST_CONSTRUCT.
 */
type_reference_resolver::adapt_primitive_or_struct_type(
    std::shared_ptr<expression> expr,
    const std::shared_ptr<type>& type_nc)
{
    auto prim_src = std::dynamic_pointer_cast<primitive_type>(type::remove_const(expr->get_type()));
    auto prim_tgt = std::dynamic_pointer_cast<primitive_type>(type_nc);

    // Step 1: Struct identity: same struct type → return as-is
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
        // Struct value -> T via user-defined cast operator. Member operators need a receiver
        // reference, so materialize a temporary for value sources.
        if (auto src_st = std::dynamic_pointer_cast<struct_type>(src_nc)) {
            auto src_agg = src_st->get_struct();
            if (src_agg) {
                auto cast_func = resolve_cast_operator_overload(src_agg, type_nc, type::is_const(expr->get_type()));
                if (cast_func) {
                    auto temp = temporary_construction_expression::make_shared(src_st, {expr});
                    temp->set_type(src_st->get_reference());
                    auto cast = std::dynamic_pointer_cast<cast_expression>(
                        cast_expression::make_shared(temp, type_nc));
                    cast->set_operator_func(cast_func);
                    if (cast_func->is_virtual() && cast_func->get_vtable_slot() >= 0) {
                        cast->set_operator_dispatch_info(
                            compute_operator_dispatch_info(cast_func, src_st->get_reference()));
                    }
                    cast->set_type(type_nc);
                    return cast;
                }
            } else {
                // Union value -> alternative or polymorphic base
                auto root_ns = _unit.get_root_namespace();
                if (auto udef = find_union_by_struct_type(root_ns, src_st)) {
                    auto bare_tgt = type::remove_const(type_nc);
                    auto tgt_st = std::dynamic_pointer_cast<struct_type>(bare_tgt);
                    if (udef->is_polymorphic() && udef->get_polymorphic_base()) {
                        auto poly_base = udef->get_polymorphic_base();
                        if (poly_base->get_struct_type() && (tgt_st == poly_base->get_struct_type() ||
                            (tgt_st && tgt_st->get_struct() && poly_base->is_derived_from(tgt_st->get_struct())))) {
                            auto cast = cast_expression::make_shared(expr, type_nc, true);
                            cast->set_type(type_nc);
                            return cast;
                        }
                    }
                    std::vector<const union_alternative*> matches;
                    for (const auto* alt : udef->all_alternatives_ptrs()) {
                        if (alt->resolved_type && type::are_equal(type::remove_const(alt->resolved_type), bare_tgt)) {
                            matches.push_back(alt);
                        }
                    }
                    if (matches.size() == 1) {
                        auto cast = cast_expression::make_shared(expr, type_nc, true);
                        cast->set_type(type_nc);
                        return cast;
                    }
                }
            }
        }
        // Value construction through a temporary for struct/union targets:
        // target T, source x  =>  load( T(x) ).
        if (auto tgt_st = std::dynamic_pointer_cast<struct_type>(type_nc)) {
            auto temp = temporary_construction_expression::make_shared(tgt_st, {expr});
            temp->accept(*this);
            auto temp_ref_type = temp->get_type();
            if (temp_ref_type && type::is_reference(temp_ref_type)) {
                auto loaded = load_value_expression::make_shared(temp);
                loaded->set_type(tgt_st);
                if (temp->is_constant()) {
                    loaded->set_constant_value(temp->get_constant_value());
                }
                return loaded;
            }
        }
        return {};
    }

    // Step 2: Struct upcast: Derived → Base (value) → cast_expression
    if (*prim_src == *prim_tgt) {
        return expr;
    }

    // Step 3: Same primitive: return as-is
    auto cast = cast_expression::make_shared(expr, prim_tgt);
    cast->set_type(prim_tgt);
    if (expr->is_constant()) {
        auto res = constant_evaluator::cast_to_type(expr->get_constant_value(), prim_tgt);
        if (res) cast->set_constant_value(*res);
    }
    return cast;
}


} // namespace k::model::gen
