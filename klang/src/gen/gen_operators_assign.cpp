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

#include "resolvers.hpp"
#include "generators.hpp"
#include "gen_helpers.hpp"
#include "resolvers_scope_lookup.hpp"
#include "../common/operator_names.hpp"
#include "../parse/ast.hpp"

#include "../errors.hpp"
#include "gen_operators_helpers.hpp"

namespace k::model::gen {

// ─────────────────────────────────────────────────────────────────────────────
// Value semantics for aggregates (IN-PROGRESS.md, phases F1/F3/F4)
// ─────────────────────────────────────────────────────────────────────────────

bool implementation_generator::is_trivially_copyable(const std::shared_ptr<type>& t) {
    auto nt = type::remove_const(t);
    if (!nt) return true;

    // An owner directly owns heap memory — never bytewise copyable.
    if (std::dynamic_pointer_cast<owner_type>(nt)) return false;

    // A sized array is trivially copyable iff its element type is.
    if (auto arr = std::dynamic_pointer_cast<sized_array_type>(nt)) {
        return is_trivially_copyable(arr->get_subtype());
    }

    auto st_type = std::dynamic_pointer_cast<struct_type>(nt);
    if (!st_type) return true; // primitives, pointers, references, ...

    auto st = st_type->get_struct();
    if (!st) return true; // union / unresolved struct type: handled elsewhere

    // A user or intrinsic destructor, or a copy constructor, signals that the
    // type manages its own resources and must not be copied bytewise.
    if (st->get_destructor()) return false;
    if (st->get_copy_constructor()) return false;

    // Recurse into base sub-objects.
    for (auto& bs : st->get_bases()) {
        if (bs.base && bs.base->get_struct_type()) {
            if (!is_trivially_copyable(bs.base->get_struct_type())) return false;
        }
    }

    // Recurse into member variables.
    for (auto& entry : st->variables()) {
        auto mv = std::dynamic_pointer_cast<member_variable_definition>(entry.second);
        if (!mv) continue;
        if (!is_trivially_copyable(mv->get_type())) return false;
    }

    return true;
}

bool implementation_generator::cancel_temporary_cleanup(llvm::Value* ptr) {
    for (auto it = _expression_temporaries.rbegin(); it != _expression_temporaries.rend(); ++it) {
        if (it->alloca == ptr) {
            // Convert reverse iterator to forward iterator for erase.
            _expression_temporaries.erase(std::next(it).base());
            return true;
        }
    }
    return false;
}

void implementation_generator::emit_value_copy_or_move(llvm::Value* dest, llvm::Value* src,
                                                       const std::shared_ptr<type>& t,
                                                       bool destroy_dest_first) {
    auto nt = type::remove_const(t);
    llvm::Type* struct_llvm = _context->get_llvm_type(nt);
    if (!struct_llvm || struct_llvm->isPointerTy()) {
        // Not a real struct payload — fall back to a scalar store.
        _builder->CreateStore(src, dest);
        return;
    }
    const auto& dl = _context->module().getDataLayout();
    uint64_t sz = dl.getTypeAllocSize(struct_llvm);

    auto st_type   = std::dynamic_pointer_cast<struct_type>(nt);
    auto agg       = st_type ? st_type->get_struct() : nullptr;
    bool trivially = is_trivially_copyable(nt);

    // Is `src` a materialised temporary currently scheduled for destruction?
    // If so, it is a prvalue whose contents we may steal (move).
    bool src_is_tracked_temp = false;
    for (auto& e : _expression_temporaries) {
        if (e.alloca == src) { src_is_tracked_temp = true; break; }
    }

    // Destroy the previous contents of the destination first (assignment onto an
    // existing, already-constructed object owning resources).
    if (destroy_dest_first && !trivially && agg && agg->get_destructor()) {
        auto dtor = agg->get_destructor();
        auto dtor_it = _context->_functions.find(dtor->shared_as<k::model::function>());
        if (dtor_it != _context->_functions.end()) {
            _builder->CreateCall(dtor_it->second, {dest});
        }
    }

    if (trivially) {
        _builder->CreateMemCpy(dest, llvm::MaybeAlign(), src, llvm::MaybeAlign(),
                               _builder->getInt64(sz));
        return;
    }

    if (src_is_tracked_temp) {
        // MOVE: transfer the bytes, then cancel the source temporary's destruction
        // so the destination becomes the sole owner (no double free).
        _builder->CreateMemCpy(dest, llvm::MaybeAlign(), src, llvm::MaybeAlign(),
                               _builder->getInt64(sz));
        cancel_temporary_cleanup(src);
        return;
    }

    // Non-trivial lvalue copy: prefer a user-provided copy constructor.
    if (agg) {
        if (auto cc = agg->get_copy_constructor()) {
            auto cc_it = _context->_functions.find(cc->shared_as<k::model::function>());
            if (cc_it != _context->_functions.end()) {
                _builder->CreateCall(cc_it->second, {dest, src});
                return;
            }
        }
    }

    // Fallback: shallow copy. This is unsafe for owning types without a copy
    // constructor; a dedicated "type-not-copyable" diagnostic (phase F6) will be
    // raised here once the lvalue-copy contract is enforced.
    _builder->CreateMemCpy(dest, llvm::MaybeAlign(), src, llvm::MaybeAlign(),
                           _builder->getInt64(sz));
}

void type_reference_resolver::visit_assignation_expression(assignation_expression &expr) {
    // TODO Rework conversions and promotions and mutualize with symbol_type_resolver::process_arithmetic(...)
    visit_binary_expression(expr);

    auto left = expr.left();
    auto right = expr.right();

    auto left_type = left->get_type();

    // ── Assignment to a struct rvalue via an operator= overload ──────────────
    // K normally requires the LHS of '=' to be an lvalue (a reference). However a
    // value-returning subscript proxy — e.g. StringBuilder.operator[](i) returning a
    // CharRef whose operator=(char) writes back into the builder — is a common idiom
    // ("sb[i] = c"). When the LHS is a (non-reference) struct that provides an
    // operator= overload, dispatch to it instead of rejecting the assignment.
    if (!type::is_reference(left_type) && type::is_struct(left_type)) {
        auto nc_left = type::remove_const(left_type);
        if (auto st_type = std::dynamic_pointer_cast<struct_type>(nc_left)) {
            if (auto agg = st_type->get_struct()) {
                std::string op_name = get_binary_operator_name(expr);
                if (!op_name.empty() &&
                    !collect_member_operators_from_hierarchy(agg, op_name).empty()) {
                    bool is_const_left = type::is_const(left_type);
                    auto [op_func, adapted_right] =
                        resolve_binary_operator_overload(expr, agg, left, right, is_const_left);
                    if (op_func) {
                        expr.set_operator_func(op_func);
                        if (adapted_right && adapted_right != right) {
                            expr.assign_right(adapted_right);
                        }
                        if (op_func->has_return_type()) {
                            expr.set_type(op_func->get_return_type());
                        } else {
                            expr.set_type(left_type);
                        }
                        if (op_func->is_member()) {
                            auto di = compute_operator_dispatch_info(op_func, left_type);
                            expr.set_operator_dispatch_info(std::move(di));
                        } else {
                            virtual_dispatch_info di;
                            di.kind = virtual_dispatch_info::dispatch_kind::DIRECT;
                            expr.set_operator_dispatch_info(std::move(di));
                        }
                        return;
                    }
                }
            }
        }
    }

    if(!type::is_reference(left_type)) {
        throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_ASSIGN_INCOMPATIBLE), expr.first_lexeme(),
            "The left operand of an assignment must be assignable (an lvalue): "
            "the left-hand side has type '{}' which is not a reference; "
            "you can only assign to a variable, parameter, or array element",
            {left_type ? left_type->to_string() : "?"});
    }
    auto ref_target_type = std::dynamic_pointer_cast<reference_type>(left_type);
    auto target_type = ref_target_type->get_subtype();

    // ── Const-check ──────────────────────────────────────────────────────────
    // If the target type is const-qualified (ref<const T>), assignment is forbidden.
    if (type::is_const(target_type)) {
        throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_OVERLOAD_CALL_NO_MATCH), expr.first_lexeme(),
            "Cannot assign to a const variable: "
            "the left-hand side has type '{}' which is const; "
            "const variables cannot be modified after initialisation",
            {target_type ? target_type->to_string() : "?"});
    }
    // ─────────────────────────────────────────────────────────────────────────

    // Step 1: Resolve left and right sub-expressions
    if(type::is_reference(target_type)) {
        // Left hand is ref-to-ref: assignment acts on the underlying object.
        left = load_value_expression::make_shared(left);
        left->set_type(target_type);
        expr.assign_left(left);
        target_type = std::dynamic_pointer_cast<reference_type>(target_type)->get_subtype();
    } else if (type::is_link(target_type)) {
        // Left hand is ref-to-link.
        // Determine if this is a rebind (RHS is an indirection) or
        // an assignment to the pointed object (RHS is a value).
        auto link_subtype = std::dynamic_pointer_cast<link_type>(target_type)->get_linked_type();
        auto rhs_type = right->get_type();
        // Unwrap ref<indirection> from rhs_type
        auto rhs_effective = rhs_type;
        if (type::is_reference(rhs_type)) {
            auto inner = std::dynamic_pointer_cast<reference_type>(rhs_type)->get_subtype();
            if (type::is_link(inner) || type::is_pointer(inner) || type::is_view(inner)) {
                rhs_effective = inner;
            }
        }
        // Helper lambda: check if rhs_effective is an indirection compatible with link_subtype (same, static upcast, or dynamic downcast)
        auto is_rebind_compatible = [&]() -> bool {
            if (!type::is_any_indirection(rhs_effective) || !rhs_effective->get_subtype() || !link_subtype)
                return false;
            auto rhs_sub_nc = type::remove_const(rhs_effective->get_subtype());
            auto lnk_sub_nc = type::remove_const(link_subtype);
            if (type::are_equal(rhs_sub_nc, lnk_sub_nc)) return true;
            auto src_st = std::dynamic_pointer_cast<struct_type>(rhs_sub_nc);
            auto tgt_st = std::dynamic_pointer_cast<struct_type>(lnk_sub_nc);
            if (!src_st || !tgt_st || !src_st->get_struct() || !tgt_st->get_struct()) return false;
            // Static upcast: rhs points to Derived, link points to Base
            if (src_st->get_struct()->is_derived_from(tgt_st->get_struct())) return true;
            // Dynamic downcast: rhs points to Base, link points to Derived (klass/interface only)
            if (tgt_st->get_struct()->is_derived_from(src_st->get_struct()) &&
                tgt_st->get_struct()->has_rtti() &&
                std::dynamic_pointer_cast<klass>(tgt_st->get_struct()) != nullptr) return true;
            return false;
        };
        // If RHS is an indirection compatible (same or upcast) with link_subtype: REBIND
        if (is_rebind_compatible()) {
            // Rebind: check const compatibility (const T~ ← T~ is OK; T~ ← const T~ is not)
            if (type::is_const(rhs_effective->get_subtype()) && !type::is_const(link_subtype)) {
                throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_OVERLOAD_ARG_TYPE_MISMATCH), expr.first_lexeme(),
                    "Cannot rebind a link-to-mutable ('{}') from a link-to-const ('{}'): "
                    "this would allow modification of a const object",
                    {target_type ? target_type->to_string() : "?",
                     rhs_type ? rhs_type->to_string() : "?"});
            }
            // Rebind: load the source address and store into the link alloca.
            // If source is nullable, warn — null-check at IR level.
            if (type::is_nullable_indirection(rhs_effective)) {
                auto diag = k::log::diagnostic::make_warning(static_cast<unsigned int>(k::diag::operator_diag::WARN_IMPLICIT_LOSSY_CAST),
                    "Rebinding a link from a nullable indirection (type '{}'): "
                    "a runtime null-check will be inserted",
                    {rhs_type ? rhs_type->to_string() : "?"});
                logger_relay::report(diag);
            }
            // Unwrap the ref wrapper from rhs if needed
            if (type::is_reference(rhs_type)) {
                right = load_value_expression::make_shared(right);
                rhs_type = rhs_effective;
                right->set_type(rhs_type);
                expr.assign_right(right);
            }
            // Determine whether to use static upcast or dynamic downcast
            {
                auto rhs_sub_nc = type::remove_const(right->get_type()->get_subtype());
                auto lnk_sub_nc = type::remove_const(link_subtype);
                if (!type::are_equal(rhs_sub_nc, lnk_sub_nc)) {
                    auto src_st = std::dynamic_pointer_cast<struct_type>(rhs_sub_nc);
                    auto tgt_st = std::dynamic_pointer_cast<struct_type>(lnk_sub_nc);
                    bool is_static_upcast = src_st && tgt_st &&
                        src_st->get_struct() && tgt_st->get_struct() &&
                        src_st->get_struct()->is_derived_from(tgt_st->get_struct());
                    if (is_static_upcast) {
                        auto upcast = cast_expression::make_shared(right, target_type);
                        upcast->set_type(target_type);
                        expr.assign_right(upcast);
                    } else {
                        // Dynamic downcast — lien is non-null, so fatal on null result
                        auto dc = cast_expression::make_shared(right, target_type, /*null_is_fatal=*/true);
                        expr.assign_right(dc);
                    }
                }
            }
            // The assignment stores a new address into the link alloca.
            expr.set_type(ref_target_type);
            return;
        }
        // Otherwise: transparent reference — assignment to the pointed object.
        left = load_value_expression::make_shared(left);
        left->set_type(target_type);
        auto ref_to_target = link_subtype->get_reference();
        left->set_type(ref_to_target);
        expr.assign_left(left);
        target_type = link_subtype;
        ref_target_type = ref_to_target;
    } else if (type::is_view(target_type)) {
        throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_LOGICAL_NOT_BOOL), expr.first_lexeme(),
            "Cannot assign to a view indirection (type '{}'): "
            "a view ('?') is immutable after initialisation",
            {target_type ? target_type->to_string() : "?"});
    }

    auto source_type = right->get_type();

    // Unwrap ref<link/ptr/pin/owner> for source-side checks
    auto effective_source_type = source_type;
    if (type::is_reference(source_type)) {
        auto inner = std::dynamic_pointer_cast<reference_type>(source_type)->get_subtype();
        // The referenced type may be const-qualified (e.g. a `const T&` parameter
        // where T is itself a pointer/link/view/owner, as produced by the const
        // Collection API for `Vector<Object*>`).  Strip the const before the
        // indirection-kind check: copying a const *pointer value* into a mutable
        // pointer is legal — only the pointer is read, the pointee's const-ness is
        // unaffected (like `Object* const` → `Object*` in C++).
        auto inner_nc = type::remove_const(inner);
        if (type::is_link(inner_nc) || type::is_pointer(inner_nc) || type::is_view(inner_nc) || type::is_owner(inner_nc)) {
            effective_source_type = inner_nc;
        }
    }

    if(type::is_pointer(target_type)) {
        // Null literal: always compatible with any pointer type.
        if(type::is_null(effective_source_type) || type::is_null(source_type)) {
            expr.set_type(ref_target_type);
            return;
        }
        if(type::is_pointer(effective_source_type) || type::is_link(effective_source_type)
           || type::is_view(effective_source_type) || type::is_owner(effective_source_type)) {
            auto src_sub = effective_source_type->get_subtype();
            auto tgt_sub = target_type->get_subtype();
            // Strip const from both sides for structural comparison
            auto src_sub_nc = type::remove_const(src_sub);
            auto tgt_sub_nc = type::remove_const(tgt_sub);
            if (src_sub_nc != tgt_sub_nc) {
                // Check static upcast: ptr<Derived>→ptr<Base>
                auto src_st = std::dynamic_pointer_cast<struct_type>(src_sub_nc);
                auto tgt_st = std::dynamic_pointer_cast<struct_type>(tgt_sub_nc);
                bool is_static_upcast = src_st && tgt_st &&
                                 src_st->get_struct() && tgt_st->get_struct() &&
                                 src_st->get_struct()->is_derived_from(tgt_st->get_struct());
                bool is_dynamic_downcast = !is_static_upcast && src_st && tgt_st &&
                                 src_st->get_struct() && tgt_st->get_struct() &&
                                 tgt_st->get_struct()->has_rtti();
                                 std::dynamic_pointer_cast<klass>(tgt_st->get_struct()) != nullptr;
                if (!is_static_upcast && !is_dynamic_downcast) {
                    throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_ASSIGN_TO_CONST), expr.first_lexeme(),
                        "Pointer assignment type mismatch: "
                        "cannot assign a '{}' to a '{}'; pointer subtypes must match "
                        "or source must be a derived type of the target",
                        {source_type ? source_type->to_string() : "?",
                         target_type ? target_type->to_string() : "?"});
                }
                // Forbid const T* → T* (would lose const-ness on pointed object)
                if (type::is_const(src_sub) && !type::is_const(tgt_sub)) {
                    throw_error(static_cast<unsigned int>(k::diag::codegen_diag::ERR_GEN_FUNC_OVERLOAD_AMBIGUOUS), expr.first_lexeme(),
                        "Cannot assign a pointer-to-const ('{}') to a pointer-to-mutable ('{}'): "
                        "this would allow modification of a const object through the mutable pointer",
                        {source_type ? source_type->to_string() : "?",
                         target_type ? target_type->to_string() : "?"});
                }
                // Unwrap ref wrapper if needed
                if (type::is_reference(source_type)) {
                    right = load_value_expression::make_shared(right);
                    source_type = std::dynamic_pointer_cast<reference_type>(source_type)->get_subtype();
                    right->set_type(source_type);
                    expr.assign_right(right);
                }
                if (is_static_upcast) {
                    auto upcast = cast_expression::make_shared(right, target_type);
                    upcast->set_type(target_type);
                    expr.assign_right(upcast);
                } else {
                    // Dynamic downcast — ptr can be null, not fatal
                    auto dc = cast_expression::make_shared(right, target_type, /*null_is_fatal=*/false);
                    expr.assign_right(dc);
                }
                expr.set_type(ref_target_type);
                return;
            }
            // Forbid const T* → T* (would lose const-ness on pointed object)
            if (type::is_const(src_sub) && !type::is_const(tgt_sub)) {
                throw_error(static_cast<unsigned int>(k::diag::codegen_diag::ERR_GEN_FUNC_OVERLOAD_AMBIGUOUS), expr.first_lexeme(),
                    "Cannot assign a pointer-to-const ('{}') to a pointer-to-mutable ('{}'): "
                    "this would allow modification of a const object through the mutable pointer",
                    {source_type ? source_type->to_string() : "?",
                     target_type ? target_type->to_string() : "?"});
            }
            if (type::is_reference(source_type)) {
                right = load_value_expression::make_shared(right);
                source_type = std::dynamic_pointer_cast<reference_type>(source_type)->get_subtype();
                right->set_type(source_type);
                expr.assign_right(right);
            }
            expr.set_type(ref_target_type);
            return;
        } else {
            throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_MAIN_WRONG_RETURN_TYPE), expr.first_lexeme(),
                "Pointer assignment requires a pointer or link on the right-hand side: "
                "cannot assign a value of type '{}' to a pointer of type '{}'",
                {source_type ? source_type->to_string() : "?",
                 target_type ? target_type->to_string() : "?"});
        }
    } else if (type::is_link(target_type)) {
        // Direct link rebind (reached after link-to-link case not matched above).
        if (!type::is_any_indirection(effective_source_type)) {
            throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_LOGICAL_AND_INCOMPATIBLE), expr.first_lexeme(),
                "Link assignment requires an indirection on the right-hand side, "
                "but got type '{}'",
                {source_type ? source_type->to_string() : "?"});
        }
        if (type::is_nullable_indirection(effective_source_type)) {
            auto diag = k::log::diagnostic::make_warning(static_cast<unsigned int>(k::diag::operator_diag::WARN_IMPLICIT_LOSSY_CAST),
                "Assigning a nullable indirection (type '{}') to a link: "
                "a runtime null-check will be inserted",
                {source_type ? source_type->to_string() : "?"});
            logger_relay::report(diag);
        }
        if (type::is_reference(source_type)) {
            right = load_value_expression::make_shared(right);
            source_type = std::dynamic_pointer_cast<reference_type>(source_type)->get_subtype();
            right->set_type(source_type);
            expr.assign_right(right);
        }
        expr.set_type(ref_target_type);
        return;
    } else if (type::is_sized_array(target_type)) {
        // Array = array : element-wise copy (see spec).
        // Source must be a reference to a sized array of the same element type.
        auto dest_arr = std::dynamic_pointer_cast<sized_array_type>(target_type);
        std::shared_ptr<type> src_inner_type = source_type;
        if (type::is_reference(source_type)) {
            src_inner_type = std::dynamic_pointer_cast<reference_type>(source_type)->get_subtype();
        }
        if (!type::is_sized_array(src_inner_type)) {
            throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_SHIFT_NOT_INT), expr.first_lexeme(),
                "Array assignment: the right-hand side must be an array of the same element type, "
                "but '{}' is not a sized array",
                {source_type ? source_type->to_string() : "?"});
        }
        auto src_arr = std::dynamic_pointer_cast<sized_array_type>(src_inner_type);
        if (!type::are_equal(dest_arr->get_subtype(), src_arr->get_subtype())) {
            throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_SHIFT_INCOMPATIBLE), expr.first_lexeme(),
                "Array assignment: element type mismatch — cannot copy from '{}' to '{}'",
                {source_type ? source_type->to_string() : "?",
                 target_type ? target_type->to_string() : "?"});
        }
        // Type of the assignment expression is ref<dest array>
        expr.set_type(ref_target_type);
        // Ensure the source is referenced (if it isn't already)
        if (!type::is_reference(source_type)) {
            right = load_value_expression::make_shared(right);
            right->set_type(source_type->get_reference());
            expr.assign_right(right);
        }
        return; // code generation handled in visit_simple_assignation_expression
    } else if (type::is_function_reference(target_type)) {
        // Assigning a function address (or another frt variable) to a function-pointer variable.
        // target_type is a function_reference_type; source should be ref<frt> (function symbol)
        // or frt itself (another variable). The ref wrapper is stripped below if present.
        //
        // Check: only pointer (*) frt is rebindable; view (?) and link (+) are immutable.
        auto frt_target = std::dynamic_pointer_cast<function_reference_type>(target_type);
        if (frt_target && frt_target->get_ref_kind() != function_reference_type::ref_kind::pointer) {
            throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_PM_EXPR_BAD_TYPE), expr.first_lexeme(),
                "Cannot assign to an immutable function reference (type '{}'): "
                "only pointer (*) function references are rebindable",
                {target_type ? target_type->to_string() : "?"});
        }
        // Unwrap ref<frt> on the source side if needed.
        // For a direct function symbol (is_function()), impl_gen returns the Function* directly —
        // no load needed. For a frt variable, impl_gen returns the alloca address — needs a load.
        if (type::is_reference(source_type)) {
            auto inner = std::dynamic_pointer_cast<reference_type>(source_type)->get_subtype();
            if (type::is_function_reference(inner)) {
                auto rhs_sym = std::dynamic_pointer_cast<symbol_expression>(right);
                if (!rhs_sym || !rhs_sym->is_function()) {
                    // Variable of frt type: load the stored function pointer from the alloca
                    right = load_value_expression::make_shared(right);
                    source_type = inner;
                    right->set_type(source_type);
                    expr.assign_right(right);
                }
                // else: direct function symbol → keep ref<frt>; impl_gen produces Function* directly
            }
        }
        expr.set_type(ref_target_type);
        return;
    } else if (type::is_owner(target_type)) {
        // ── Owner assignment: destroy old object (if any), transfer ownership ─────
        //   - null literal    → destroy current + set null
        //   - ref<owner<T>>  → move (load + null source), same or compatible subtype
        //   - owner<T>       → direct (from new_expression or already an owner value)

        // Detect null literal (both parsed 'null' and programmatic nullptr)
        bool rhs_is_null = type::is_null(source_type);
        if (!rhs_is_null) {
            if (auto ve = std::dynamic_pointer_cast<value_expression>(right)) {
                if (ve->is_literal() && ve->any_literal().has_value()) {
                    rhs_is_null = std::holds_alternative<lex::null>(ve->any_literal());
                } else {
                    rhs_is_null = std::holds_alternative<std::nullptr_t>(ve->get_value());
                }
            }
        }
        if (rhs_is_null) {
            // Assign null: destroy current owned object and store null
            expr.set_type(ref_target_type);
            return;
        }

        if (type::is_reference(source_type)) {
            auto inner = std::dynamic_pointer_cast<reference_type>(source_type)->get_subtype();
            // The referenced owner may be const-qualified (e.g. a `const T&` parameter
            // where T is an owner, as produced by the const Collection API for
            // `LinkedList<Object!>`).  Strip the const for the owner-kind check: the
            // owner is still moved (source consumed / nulled) — the const only guards
            // the pointee, not the ownership transfer of the reference binding.
            auto inner_nc = type::remove_const(inner);
            if (type::is_owner(inner_nc)) {
                // Move: wrap source in owner_move_expression (load + null source alloca)
                auto own_src_nc = type::remove_const(inner_nc->get_subtype());
                auto own_tgt_nc = type::remove_const(target_type->get_subtype());
                auto move = owner_move_expression::make_shared(right);
                move->set_type(inner_nc);
                std::shared_ptr<expression> new_right = move;
                if (!type::are_equal(own_src_nc, own_tgt_nc)) {
                    // Check upcast: owner<Derived> → owner<Base>
                    auto src_st = std::dynamic_pointer_cast<struct_type>(own_src_nc);
                    auto tgt_st = std::dynamic_pointer_cast<struct_type>(own_tgt_nc);
                    if (!src_st || !tgt_st || !src_st->get_struct() || !tgt_st->get_struct() ||
                        !src_st->get_struct()->is_derived_from(tgt_st->get_struct())) {
                        throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_SUBSCRIPT_OVERLOAD_CONST), expr.first_lexeme(),
                            "Owner assignment type mismatch: cannot move '{}' into '{}'",
                            {source_type->to_string(), target_type->to_string()});
                    }
                    auto upcast = cast_expression::make_shared(move, target_type);
                    upcast->set_type(target_type);
                    new_right = upcast;
                }
                expr.assign_right(new_right);
                expr.set_type(ref_target_type);
                return;
            }
        }
        if (type::is_owner(source_type)) {
            // Direct owner value (e.g. from new_expression or already an owner_move_expression)
            expr.set_type(ref_target_type);
            return;
        }
        throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_SUBSCRIPT_OVERLOAD_NOT_FOUND), expr.first_lexeme(),
            "Owner assignment: right-hand side must be an owner value, "
            "another owner variable (move), or null; got type '{}'",
            {source_type ? source_type->to_string() : "?"});
    } else if(type::is_struct(target_type)) {
        // ── Struct assignment: try operator overload ──
        auto nc_target = type::remove_const(target_type);
        auto st_type = std::dynamic_pointer_cast<struct_type>(nc_target);
        if (st_type) {
            auto agg = st_type->get_struct();
            if (agg) {
                // Check if the operator is explicitly deleted
                std::string op_name = get_binary_operator_name(expr);
                if (!op_name.empty()) {
                    auto member_funcs = collect_member_operators_from_hierarchy(agg, op_name);
                    for (auto& f : member_funcs) {
                        if (f->is_deleted()) {
                            throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_SUBSCRIPT_OVERLOAD_BAD_RETURN), expr.first_lexeme(),
                                "Use of deleted operator '{}' on type '{}': "
                                "this operator was explicitly deleted with '-> delete'",
                                {get_operator_symbol(op_name),
                                 target_type ? target_type->to_string() : "?"});
                        }
                    }
                }
                bool is_const_left = type::is_const(target_type);
                auto [op_func, adapted_right] = resolve_binary_operator_overload(expr, agg, left, right, is_const_left);
                if (op_func) {
                    // Store the resolved operator function on the expression
                    expr.set_operator_func(op_func);
                    // Apply the adapted right operand (implicit cast if needed)
                    if (adapted_right && adapted_right != right) {
                        expr.assign_right(adapted_right);
                    }
                    // Set the expression type to the return type of the operator function
                    if (op_func->has_return_type()) {
                        expr.set_type(op_func->get_return_type());
                    } else {
                        expr.set_type(ref_target_type);
                    }
                    // Compute dispatch info for virtual calls
                    if (op_func->is_member()) {
                        auto di = compute_operator_dispatch_info(op_func, left_type);
                        expr.set_operator_dispatch_info(std::move(di));
                    } else {
                        virtual_dispatch_info di;
                        di.kind = virtual_dispatch_info::dispatch_kind::DIRECT;
                        expr.set_operator_dispatch_info(std::move(di));
                    }
                    return;
                }
            } else {
                // No owning aggregate → this is a union struct type.
                // Check if the source is also a union type and if there is an inheritance
                // relationship (upcast: derived→base, or downcast: base→derived).
                auto src_inner = source_type;
                if (type::is_reference(src_inner)) {
                    src_inner = std::dynamic_pointer_cast<reference_type>(src_inner)->get_subtype();
                }
                if (auto src_st = std::dynamic_pointer_cast<struct_type>(src_inner)) {
                    if (!src_st->get_struct() && src_st != st_type) {
                        // Both are union struct types and they differ: check inheritance.
                        auto root_ns = _unit.get_root_namespace();
                        std::shared_ptr<union_type_def> lhs_udef, rhs_udef;
                        if (root_ns) {
                            lhs_udef = find_union_by_struct_type(root_ns, st_type);
                            rhs_udef = find_union_by_struct_type(root_ns, src_st);
                        }
                        if (lhs_udef && rhs_udef) {
                            bool upcast   = scope_lookup::is_base_union_of(*lhs_udef, *rhs_udef);
                            bool downcast = scope_lookup::is_base_union_of(*rhs_udef, *lhs_udef);
                            if (!upcast && !downcast) {
                                throw_error(static_cast<unsigned int>(k::diag::union_diag::ERR_UNION_ASSIGN_TYPE_MISMATCH),
                                    expr.first_lexeme(),
                                    "Cannot assign union of type '{}' to union of type '{}': "
                                    "the two union types are unrelated (no inheritance relationship)",
                                    {src_inner->to_string(), target_type->to_string()});
                            }
                            // Allow the assignment; do NOT wrap the source in load_value_expression
                            // so that codegen sees both as pointers (needed for field-by-field copy).
                            // Keep source_type as ref<union> so codegen receives the pointer.
                            expr.set_type(ref_target_type);
                            return;
                        }
                    }
                }
            }
        }
        // No operator overload found — fall through to default struct assignment (memcpy/store).
        expr.set_type(ref_target_type);
        // If source type is reference, deref it
        if(type::is_reference(source_type)) {
            right = load_value_expression::make_shared(right);
            source_type = std::dynamic_pointer_cast<reference_type>(source_type)->get_subtype();
            right->set_type(source_type);
            expr.assign_right(right);
        }
        return;
    } else if(!type::is_primitive(target_type) && !type::is_enum(target_type)) {
        throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_MAIN_WRONG_PARAMS), expr.first_lexeme(),
            "Assignment to a non-primitive, non-pointer type is not yet supported: "
            "the target has type '{}'; only assignments to primitive types, pointers and arrays are supported",
            {target_type ? target_type->to_string() : "?"});
    }

    // Type of an assignation is a reference
    expr.set_type(ref_target_type);

    // Step 2: Validate that the left operand is assignable (reference, not const)
    // If source type is reference, deref it
    if(type::is_reference(source_type)) {
        // Source type must be de-referenced
        right = load_value_expression::make_shared(right);
        source_type = std::dynamic_pointer_cast<reference_type>(source_type)->get_subtype();
        right->set_type(source_type);
        expr.assign_right(right);
    }

    // Step 3: For struct types: check for operator= overload or direct copy
    // Step 6: For primitives: adapt right operand type to match left
    // TODO Promote to largest target_type instead to align to left operand.
    auto cast = adapt_type(right, target_type);
    if(!cast) {
        throw_error(static_cast<unsigned int>(k::diag::statement_diag::ERR_IF_COND_NOT_BOOL), expr.first_lexeme(),
            "Incompatible types in assignment: "
            "the right-hand side of type '{}' cannot be implicitly converted to the target type '{}'; "
            "use an explicit cast if a narrowing conversion is intended",
            {right->get_type() ? right->get_type()->to_string() : "?",
             target_type ? target_type->to_string() : "?"});
    } else if(cast != right) {
        // Casted, assign casted expression instead of right source.
        expr.assign_right(cast);
    } else {
        // Compatible target_type, no need to cast.
    }
}

//
// Simple assignment expression (=)
//

/**
 * Generate LLVM IR for simple assignment (=).
 *
 * Steps:
 *   1. Evaluate left and right operands.
 *   2. For operator= overload: delegate to generate_binary_operator_overload.
 *   3. For owner assignment: emit owner_move + null the source.
 *   4. For struct copy: emit memcpy or copy constructor call.
 *   5. For primitives/pointers: emit store instruction.
 */
void implementation_generator::visit_simple_assignation_expression(simple_assignation_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    // ── Skip the union discriminant check when evaluating the LHS of an assignment
    //    to a union alternative (we are about to switch the active alternative). ──
    {
        auto lhs_e = std::shared_ptr<expression>(expr.left());
        while (auto lve = std::dynamic_pointer_cast<load_value_expression>(lhs_e)) {
            lhs_e = lve->sub_expr();
        }
        if (auto moe = std::dynamic_pointer_cast<member_of_object_expression>(lhs_e)) {
            auto sub_type = moe->sub_expr()->get_type();
            if (type::is_reference(sub_type))
                sub_type = std::dynamic_pointer_cast<reference_type>(sub_type)->get_subtype();
            if (auto st = std::dynamic_pointer_cast<struct_type>(sub_type)) {
                if (!st->get_struct())
                    _skip_union_disc_check = true;
            }
        }
    }

    // Step 1: Evaluate left and right operands
    auto [left, right] = process_binary_expression(expr);
    _skip_union_disc_check = false;
    if(!left || !right) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F035), expr.first_lexeme(),
            "Internal error: assignment expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    // Step 2: For operator= overload: delegate to generate_binary_operator_overload
    // left is a pointer to the storage.
    // Determine what the target type really is after one level of ref-unwrap.
    auto expr_left_type = expr.left()->get_type();
    auto left_ref_type  = std::dynamic_pointer_cast<reference_type>(expr_left_type);
    auto target_type    = left_ref_type ? left_ref_type->get_subtype() : nullptr;

    // If target is ref-to-ref, unwrap one more level (variable access pattern).
    if (target_type && type::is_reference(target_type)) {
        target_type = std::dynamic_pointer_cast<reference_type>(target_type)->get_subtype();
    }

    // ── Union member assignment ─────────────────────────────────────────────
    // If the LHS is a member access on a union, destroy the previously-active
    // alternative, store the new value, and update the discriminant. This must
    // run *before* the owner/array special cases below, otherwise an owner or
    // array alternative would short-circuit (return) without updating the
    // discriminant — leaving the union mistagged and crashing on the next read.
    {
        auto lhs_expr = expr.left();
        while (auto lve = std::dynamic_pointer_cast<load_value_expression>(lhs_expr)) {
            lhs_expr = lve->sub_expr();
        }
        if (auto moe = std::dynamic_pointer_cast<member_of_object_expression>(lhs_expr)) {
            auto sub_type = moe->sub_expr()->get_type();
            if (type::is_reference(sub_type)) {
                sub_type = std::dynamic_pointer_cast<reference_type>(sub_type)->get_subtype();
            }
            if (auto st_type = std::dynamic_pointer_cast<struct_type>(sub_type)) {
                if (!st_type->get_struct()) {
                    // This is a union type — find the union_type_def
                    std::shared_ptr<union_type_def> union_def;
                    auto root_ns = _unit.get_root_namespace();
                    if (root_ns) {
                        union_def = find_union_by_struct_type(root_ns, st_type);
                    }
                    if (union_def) {
                        const k::name& sym_name = moe->symbol().get_name();
                        std::string alt_name = sym_name.size() > 1 ? sym_name.back() : sym_name.to_string();
                        auto* alt = union_def->get_alternative_by_name(alt_name);
                        if (alt) {
                            // Re-evaluate the sub-expression to get the union base pointer
                            _value = nullptr;
                            moe->sub_expr()->accept(*this);
                            llvm::Value* union_base = _value;
                            if (union_base) {
                                // Destroy the previously-active alternative (handles
                                // owner alternatives — frees their buffer).
                                emit_union_cleanup_on_reassign(union_base, *union_def, alt->index);
                                // Store the new value. For owner/pointer alternatives a
                                // null RHS yields a null LLVM value: store an explicit
                                // null pointer instead.
                                llvm::Value* store_val = right;
                                if (!store_val) {
                                    store_val = llvm::ConstantPointerNull::get(
                                        llvm::PointerType::get(_builder->getContext(), 0));
                                }
                                _builder->CreateStore(store_val, left);
                                // Update the discriminant.
                                auto* union_llvm_type = st_type->get_llvm_type();
                                auto* disc_ptr = _builder->CreateStructGEP(union_llvm_type, union_base, 0, "union_disc_upd");
                                _builder->CreateStore(
                                    llvm::ConstantInt::get(llvm::Type::getInt32Ty(_builder->getContext()), alt->index),
                                    disc_ptr);
                            }
                            _value = left;
                            return;
                        }
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------------
    // Array assignment: element-wise copy (spec: partial copy, no resize)
    // ------------------------------------------------------------------
    if (target_type && type::is_sized_array(target_type)) {
        auto dest_arr  = std::dynamic_pointer_cast<sized_array_type>(target_type);
        auto* struct_llvm    = dest_arr->get_llvm_struct_type();
        auto* data_arr_llvm  = dest_arr->get_llvm_data_array_type();
        auto* elem_llvm      = _context->get_llvm_type(dest_arr->get_subtype());
        auto  dest_n         = static_cast<uint64_t>(dest_arr->get_size());

        // right is the pointer to the source struct { i32, [N x T] }
        auto* i32_t = llvm::Type::getInt32Ty(_builder->getContext());

        // Source capacity (runtime value from field 0)
        llvm::Value* src_size_ptr = _builder->CreateStructGEP(struct_llvm, right,
            sized_array_type::FIELD_SIZE, "src_sz_ptr");
        llvm::Value* src_n = _builder->CreateLoad(i32_t, src_size_ptr, "src_n");

        // Data pointers
        llvm::Value* src_data  = _builder->CreateStructGEP(struct_llvm, right,
            sized_array_type::FIELD_DATA, "src_data");
        llvm::Value* dest_data = _builder->CreateStructGEP(struct_llvm, left,
            sized_array_type::FIELD_DATA, "dst_data");

        // copy_n = min(dest_n, src_n)
        auto* dest_n_val = llvm::ConstantInt::get(i32_t, dest_n, false);
        llvm::Value* copy_n = _builder->CreateSelect(
            _builder->CreateICmpULT(src_n, dest_n_val), src_n, dest_n_val, "copy_n");

        // Emit copy loop
        auto* fn = _builder->GetInsertBlock()->getParent();
        auto* pre_bb   = _builder->GetInsertBlock();
        auto* loop_bb  = llvm::BasicBlock::Create(_builder->getContext(), "arr_asgn_loop", fn);
        auto* done_bb  = llvm::BasicBlock::Create(_builder->getContext(), "arr_asgn_done", fn);

        _builder->CreateCondBr(
            _builder->CreateICmpUGT(copy_n, llvm::ConstantInt::get(i32_t, 0, false)),
            loop_bb, done_bb);

        _builder->SetInsertPoint(loop_bb);
        auto* idx = _builder->CreatePHI(i32_t, 2, "asgn_idx");
        idx->addIncoming(llvm::ConstantInt::get(i32_t, 0, false), pre_bb);

        llvm::Value* s = _builder->CreateGEP(data_arr_llvm, src_data,
            {_builder->getInt32(0), idx}, "s_elem");
        llvm::Value* d = _builder->CreateGEP(data_arr_llvm, dest_data,
            {_builder->getInt32(0), idx}, "d_elem");
        _builder->CreateStore(_builder->CreateLoad(elem_llvm, s, "ev"), d);

        auto* nxt = _builder->CreateAdd(idx, llvm::ConstantInt::get(i32_t, 1), "nxt");
        idx->addIncoming(nxt, loop_bb);
        _builder->CreateCondBr(_builder->CreateICmpULT(nxt, copy_n), loop_bb, done_bb);

        _builder->SetInsertPoint(done_bb);
        _value = left;
        return;
    }

    // ------------------------------------------------------------------
    // Owner assignment: delete old object (if any), store new pointer
    // ------------------------------------------------------------------
    if (target_type && type::is_owner(target_type)) {
        auto& llvm_ctx = _builder->getContext();
        auto* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);

        // Step 3: For owner assignment: emit owner_move + null the source
        // If non-null, destroy + free the existing object (don't null-out, we're about to store the new value)
        auto own_type = std::dynamic_pointer_cast<owner_type>(target_type);
        emit_owner_cleanup_if_nonnull(_builder.get(), get_module(), _context->_functions,
            left, own_type->get_owned_type(), "owner_asgn", /*null_out=*/false);

        // Determine the new pointer value to store:
        // - right may be null (from null literal → visit_value_expression returns nullptr LLVM val)
        // - right may be an owner ptr (from owner_move_expression)
        llvm::Value* new_ptr = right;
        if (!new_ptr) {
            // null literal case
            new_ptr = llvm::ConstantPointerNull::get(ptr_ty);
        }
        _builder->CreateStore(new_ptr, left);
        _value = left;
        return;
    }

    // ------------------------------------------------------------------
    // Scalar / pointer assignment (existing behaviour)
    // ------------------------------------------------------------------

    // Step 4: For struct copy: emit memcpy or copy constructor call
    // Link rebind from nullable source: emit null-check before store.
    // (The resolver emits warning 0x0072 at compile-time; we add the runtime guard here.)
    if (target_type && type::is_link(target_type)) {
        // Pierce cast_expression to find the real source nullability
        auto rhs_model = expr.right();
        auto rhs_type = rhs_model ? rhs_model->get_type() : nullptr;
        // Also check original type through a cast (upcast Derived→Base wraps nullable ptr)
        if (auto cast_e = std::dynamic_pointer_cast<cast_expression>(rhs_model)) {
            auto inner_type = cast_e->sub_expr()->get_type();
            if (inner_type && type::is_nullable_indirection(inner_type)) {
                rhs_type = inner_type;
            }
        }
        if (rhs_type && type::is_nullable_indirection(rhs_type)) {
            set_debug_location(expr.first_lexeme());
            auto* fatal = get_or_declare_fatal_null_function("__k_fatal_null_assignation");
            // In an if-condition, soft-fail to _null_failure_bb instead of fatal trap.
            emit_null_check(right, fatal, "link_rebind", _null_failure_bb);
        }
    }

    // ── Union inherited assignment (upcast: derived→base, downcast: base→derived) ──
    // Detected when both LHS and RHS are union struct types with different struct types.
    // In this case `right` is still a pointer (the ref wrapper was NOT stripped by the type
    // resolver), so we can use it directly for field-by-field copy.
    {
        auto lhs_type = target_type;
        auto rhs_model_type = expr.right()->get_type();
        // source is still ref<union_T> (the type resolver kept it as a reference)
        auto rhs_inner = rhs_model_type;
        if (type::is_reference(rhs_inner)) {
            rhs_inner = std::dynamic_pointer_cast<reference_type>(rhs_inner)->get_subtype();
        }
        if (auto lhs_st = std::dynamic_pointer_cast<struct_type>(lhs_type)) {
            if (auto rhs_st = std::dynamic_pointer_cast<struct_type>(rhs_inner)) {
                if (!lhs_st->get_struct() && !rhs_st->get_struct() && lhs_st != rhs_st) {
                    // Both are union struct types and they differ — do inherited copy
                    auto root_ns = _unit.get_root_namespace();
                    std::shared_ptr<union_type_def> lhs_udef, rhs_udef;
                    if (root_ns) {
                        lhs_udef = find_union_by_struct_type(root_ns, lhs_st);
                        rhs_udef = find_union_by_struct_type(root_ns, rhs_st);
                    }
                    if (lhs_udef && rhs_udef) {
                        bool upcast   = scope_lookup::is_base_union_of(*lhs_udef, *rhs_udef);
                        bool downcast = scope_lookup::is_base_union_of(*rhs_udef, *lhs_udef);

                        auto& llvm_ctx = _builder->getContext();
                        auto* i32_ty   = llvm::Type::getInt32Ty(llvm_ctx);
                        auto* i64_ty   = llvm::Type::getInt64Ty(llvm_ctx);
                        auto& dl       = _context->module().getDataLayout();

                        // `right` is the pointer to the source union (kept as ref by type resolver)
                        llvm::Value* src_ptr = right;
                        auto* lhs_llvm = lhs_st->get_llvm_type();
                        auto* rhs_llvm = rhs_st->get_llvm_type();

                        // Compute source storage size (field 1 of the rhs struct)
                        uint64_t rhs_storage_size = 0;
                        if (auto* rhs_arr_ty = llvm::dyn_cast<llvm::ArrayType>(
                                rhs_llvm->getStructElementType(1))) {
                            rhs_storage_size = rhs_arr_ty->getNumElements();
                        }
                        uint64_t lhs_storage_size = 0;
                        if (auto* lhs_arr_ty = llvm::dyn_cast<llvm::ArrayType>(
                                lhs_llvm->getStructElementType(1))) {
                            lhs_storage_size = lhs_arr_ty->getNumElements();
                        }
                        uint64_t copy_storage = std::min(lhs_storage_size, rhs_storage_size);

                        if (upcast) {
                            // derived→base: runtime check that discriminant is in base's range
                            auto* disc_src_ptr = _builder->CreateStructGEP(rhs_llvm, src_ptr, 0, "upcast_disc_ptr");
                            auto* disc_val     = _builder->CreateLoad(i32_ty, disc_src_ptr, "upcast_disc");
                            auto* limit        = llvm::ConstantInt::get(i32_ty,
                                                    lhs_udef->total_alternative_count());
                            auto* in_range     = _builder->CreateICmpULT(disc_val, limit, "upcast_in_range");

                            auto* cur_fn  = _builder->GetInsertBlock()->getParent();
                            auto* fail_bb = llvm::BasicBlock::Create(llvm_ctx, "union_upcast_fail", cur_fn);
                            auto* ok_bb   = llvm::BasicBlock::Create(llvm_ctx, "union_upcast_ok",   cur_fn);
                            _builder->CreateCondBr(in_range, ok_bb, fail_bb);

                            // Fail: discriminant is out of range — call trap
                            _builder->SetInsertPoint(fail_bb);
                            auto* trap_fn = llvm::Intrinsic::getDeclaration(
                                &_context->module(), llvm::Intrinsic::trap);
                            _builder->CreateCall(trap_fn);
                            _builder->CreateUnreachable();

                            _builder->SetInsertPoint(ok_bb);
                            // Copy discriminant
                            auto* disc_dst_ptr = _builder->CreateStructGEP(lhs_llvm, left, 0, "upcast_dst_disc");
                            _builder->CreateStore(disc_val, disc_dst_ptr);
                        } else {
                            // base→derived (downcast): always valid — copy discriminant
                            auto* disc_src_ptr = _builder->CreateStructGEP(rhs_llvm, src_ptr, 0, "downcast_disc_ptr");
                            auto* disc_val     = _builder->CreateLoad(i32_ty, disc_src_ptr, "downcast_disc");
                            auto* disc_dst_ptr = _builder->CreateStructGEP(lhs_llvm, left, 0, "downcast_dst_disc");
                            _builder->CreateStore(disc_val, disc_dst_ptr);
                        }

                        // Copy storage bytes (min of both sides)
                        if (copy_storage > 0) {
                            auto* src_storage = _builder->CreateStructGEP(rhs_llvm, src_ptr, 1, "union_inh_src_storage");
                            auto* dst_storage = _builder->CreateStructGEP(lhs_llvm, left,    1, "union_inh_dst_storage");
                            _builder->CreateMemCpy(dst_storage, llvm::MaybeAlign(1),
                                                   src_storage, llvm::MaybeAlign(1),
                                                   llvm::ConstantInt::get(i64_ty, copy_storage));
                        }
                        _value = left;
                        return;
                    }
                }
            }
        }
    }
    // ───────────────────────────────────────────────────────────────────────

     // ── Struct value copy/move (no operator= overload) ───────────────────────
    // K does not provide user-defined copy-assignment operators, so a plain
    // struct assignment (e.g. `a = b;` or `current = _in->read();`) falls through
    // to here.  When the right-hand side is a *pointer* to the source struct
    // (e.g. an sret alloca returned by a value-returning call, or the address of
    // a struct variable), delegate to the value-semantics routine, which:
    //   - memcpy's trivially-copyable structs (fast path);
    //   - MOVES a prvalue temporary (memcpy + cancel its destruction) so owning
    //     aggregates such as Vector<T> are not double-freed;
    //   - COPIES an lvalue via the copy constructor when available.
    // The old destination contents are destroyed first for non-trivial types.
    if (target_type && type::is_struct(target_type) && right->getType()->isPointerTy()) {
        auto* struct_llvm = _context->get_llvm_type(type::remove_const(target_type));
        if (struct_llvm && !struct_llvm->isPointerTy()) {
            emit_value_copy_or_move(left, right, target_type, /*destroy_dest_first=*/true);
            _value = left;
            return;
        }
    }

    // Step 5: For primitives/pointers: emit store instruction
    _value = right;
    _value = _builder->CreateStore(_value, left);
    _value = left;

}

//
// Arithmetic assignation expression
//

void symbol_resolver::visit_arithmetic_assignation_expression(arithmetic_assignation_expression &expr) {
    visit_assignation_expression(expr);
}

void type_reference_resolver::visit_arithmetic_assignation_expression(arithmetic_assignation_expression &expr) {
    visit_assignation_expression(expr);

    // If an operator overload was resolved, no further checks needed.
    if (expr.has_operator_overload()) return;

    auto left = expr.left();
    auto right = expr.right();

    auto left_type = left->get_type();
    auto ref_target_type = std::dynamic_pointer_cast<reference_type>(left_type);
    auto target_type = ref_target_type->get_subtype();
    if(type::is_pointer(target_type)) {
        throw_error(static_cast<unsigned int>(k::diag::statement_diag::ERR_FOR_COND_NOT_BOOL), expr.first_lexeme(),
            "Arithmetic-assignment operators (e.g. '+=', '-=') cannot be applied to pointer types: "
            "the target has type '{}'; pointer arithmetic is not supported",
            {target_type ? target_type->to_string() : "?"});
    }
}

//
// Addition assignment expression (+=)
//

void implementation_generator::visit_addition_assignation_expression(additition_assignation_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F036), expr.first_lexeme(),
            "Internal error: '+=' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    auto left_ref_type = std::dynamic_pointer_cast<reference_type>(expr.left()->get_type());
    auto left_type = left_ref_type->get_subtype();
    auto llvm_type = _context->get_llvm_type(left_type);

    auto left_val = _builder->CreateLoad(llvm_type, left);
    if(type::is_prim_integer(left_type)) {
        _value = _builder->CreateAdd(left_val, right);
    } else if(type::is_prim_float(left_type)) {
        _value = _builder->CreateFAdd(left_val, right);
    } else {
        // TODO: Support other types
    }

    // Store the value, return the left ref
    _value = _builder->CreateStore(_value, left);
    _value = left;
}

//
// Substraction assignment expression (-=)
//

void implementation_generator::visit_substraction_assignation_expression(substraction_assignation_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F037), expr.first_lexeme(),
            "Internal error: '-=' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    auto left_ref_type = std::dynamic_pointer_cast<reference_type>(expr.left()->get_type());
    auto left_type = left_ref_type->get_subtype();
    auto llvm_type = _context->get_llvm_type(left_type);

    auto left_val = _builder->CreateLoad(llvm_type, left);
    if(type::is_prim_integer(left_type)) {
        _value = _builder->CreateSub(left_val, right);
    } else if(type::is_prim_float(left_type)) {
        _value = _builder->CreateFSub(left_val, right);
    } else {
        // TODO: Support other types
    }

    // Store the value, return the left ref
    _value = _builder->CreateStore(_value, left);
    _value = left;
}

//
// Multiplication assignment expression (*=)
//

void implementation_generator::visit_multiplication_assignation_expression(multiplication_assignation_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F038), expr.first_lexeme(),
            "Internal error: '*=' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    auto left_ref_type = std::dynamic_pointer_cast<reference_type>(expr.left()->get_type());
    auto left_type = left_ref_type->get_subtype();
    auto llvm_type = _context->get_llvm_type(left_type);

    auto left_val = _builder->CreateLoad(llvm_type, left);
    if(type::is_prim_integer(left_type)) {
        _value = _builder->CreateMul(left_val, right);
    } else if(type::is_prim_float(left_type)) {
        _value = _builder->CreateFMul(left_val, right);
    } else {
        // TODO: Support other types
    }

    // Store the value, return the left ref
    _value = _builder->CreateStore(_value, left);
    _value = left;
}

//
// Division assignment expression (/=)
//

void implementation_generator::visit_division_assignation_expression(division_assignation_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F039), expr.first_lexeme(),
            "Internal error: '/=' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    auto left_ref_type = std::dynamic_pointer_cast<reference_type>(expr.left()->get_type());
    auto left_type = type::remove_const(left_ref_type->get_subtype());
    auto llvm_type = _context->get_llvm_type(left_type);

    auto left_val = _builder->CreateLoad(llvm_type, left);
    if(auto prim = std::dynamic_pointer_cast<primitive_type>(left_type)) {
        if(prim->is_integer()) {
            if(prim->is_unsigned()) {
                _value = _builder->CreateUDiv(left_val, right);
            } else {
                _value = _builder->CreateSDiv(left_val, right);
            }
        } else if(prim->is_float()) {
            _value = _builder->CreateFDiv(left_val, right);
        }
    } else {
        // TODO: Support other types
    }

    // Store the value, return the left ref
    _value = _builder->CreateStore(_value, left);
    _value = left;
}

//
// Modulo assignment expression (%=)
//

void implementation_generator::visit_modulo_assignation_expression(modulo_assignation_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F03A), expr.first_lexeme(),
            "Internal error: '%=' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    auto left_ref_type = std::dynamic_pointer_cast<reference_type>(expr.left()->get_type());
    auto left_type = type::remove_const(left_ref_type->get_subtype());
    auto llvm_type = _context->get_llvm_type(left_type);

    auto left_val = _builder->CreateLoad(llvm_type, left);
    if(auto prim = std::dynamic_pointer_cast<primitive_type>(left_type)) {
        if(prim->is_integer()) {
            if(prim->is_unsigned()) {
                _value = _builder->CreateURem(left_val, right);
            } else {
                _value = _builder->CreateSRem(left_val, right);
            }
        } else if(prim->is_float()) {
            _value = _builder->CreateFRem(left_val, right);
        }
    } else {
        // TODO: Support other types
    }

    // Store the value, return the left ref
    _value = _builder->CreateStore(_value, left);
    _value = left;
}

//
// Bitwise and assignment expression
//

void implementation_generator::visit_bitwise_and_assignation_expression(bitwise_and_assignation_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F03B), expr.first_lexeme(),
            "Internal error: '&=' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    auto left_ref_type = std::dynamic_pointer_cast<reference_type>(expr.left()->get_type());
    auto left_type = type::remove_const(left_ref_type->get_subtype());
    auto llvm_type = _context->get_llvm_type(left_type);

    auto left_val = _builder->CreateLoad(llvm_type, left);
    if(auto prim = std::dynamic_pointer_cast<primitive_type>(left_type)) {
        if(prim->is_integer()) {
            _value = _builder->CreateAnd(left_val, right);
        } else if(prim->is_float()) {
            throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_ADD_ASSIGN_INCOMPATIBLE), expr.first_lexeme(),
                "Bitwise AND-assignment ('&=') cannot be applied to floating-point values: "
                "bitwise operations are only defined for integer types; "
                "the operand has type '{}'",
                {prim->to_string()});
        }
    } else {
        // TODO: Support other types
    }

    // Store the value, return the left ref
    _value = _builder->CreateStore(_value, left);
    _value = left;
}

//
// Bitwise or assignment expression
//

void implementation_generator::visit_bitwise_or_assignation_expression(bitwise_or_assignation_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F03C), expr.first_lexeme(),
            "Internal error: '|=' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    auto left_ref_type = std::dynamic_pointer_cast<reference_type>(expr.left()->get_type());
    auto left_type = type::remove_const(left_ref_type->get_subtype());
    auto llvm_type = _context->get_llvm_type(left_type);

    auto left_val = _builder->CreateLoad(llvm_type, left);
    if(auto prim = std::dynamic_pointer_cast<primitive_type>(left_type)) {
        if(prim->is_integer()) {
            _value = _builder->CreateOr(left_val, right);
        } else if(prim->is_float()) {
            throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_CAST_NOT_SUPPORTED), expr.first_lexeme(),
                "Bitwise OR-assignment ('|=') cannot be applied to floating-point values: "
                "bitwise operations are only defined for integer types; "
                "the operand has type '{}'",
                {prim->to_string()});
        }
    } else {
        // TODO: Support other types
    }

    // Store the value, return the left ref
    _value = _builder->CreateStore(_value, left);
    _value = left;
}

//
// Bitwise xor assignment expression
//

void implementation_generator::visit_bitwise_xor_assignation_expression(bitwise_xor_assignation_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F03D), expr.first_lexeme(),
            "Internal error: '^=' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    auto left_ref_type = std::dynamic_pointer_cast<reference_type>(expr.left()->get_type());
    auto left_type = type::remove_const(left_ref_type->get_subtype());
    auto llvm_type = _context->get_llvm_type(left_type);

    auto left_val = _builder->CreateLoad(llvm_type, left);
    if(auto prim = std::dynamic_pointer_cast<primitive_type>(left_type)) {
        if(prim->is_integer()) {
            _value = _builder->CreateXor(left_val, right);
        } else if(prim->is_float()) {
            throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_MUL_ASSIGN_INCOMPATIBLE), expr.first_lexeme(),
                "Bitwise XOR-assignment ('^=') cannot be applied to floating-point values: "
                "bitwise operations are only defined for integer types; "
                "the operand has type '{}'",
                {prim->to_string()});
        }
    } else {
        // TODO: Support other types
    }

    // Store the value, return the left ref
    _value = _builder->CreateStore(_value, left);
    _value = left;
}

//
// Left shift assignment expression
//

void implementation_generator::visit_left_shift_assignation_expression(left_shift_assignation_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F03E), expr.first_lexeme(),
            "Internal error: '<<=' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    auto left_ref_type = std::dynamic_pointer_cast<reference_type>(expr.left()->get_type());
    auto left_type = type::remove_const(left_ref_type->get_subtype());
    auto llvm_type = _context->get_llvm_type(left_type);

    auto left_val = _builder->CreateLoad(llvm_type, left);
    if(auto prim = std::dynamic_pointer_cast<primitive_type>(left_type)) {
        if(prim->is_integer()) {
            // TODO may it poison when overflow ?
            _value = _builder->CreateShl(left_val, right);
        } else if(prim->is_float()) {
            throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_SUB_ASSIGN_INCOMPATIBLE), expr.first_lexeme(),
                "Left shift-assignment ('<<=') cannot be applied to floating-point values: "
                "shift operations are only defined for integer types; "
                "the operand has type '{}'",
                {prim->to_string()});
        }
    } else {
        // TODO: Support other types
    }

    // Store the value, return the left ref
    _value = _builder->CreateStore(_value, left);
    _value = left;
}

//
// Right shift assignment expression
//

void implementation_generator::visit_right_shift_assignation_expression(right_shift_assignation_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F03F), expr.first_lexeme(),
            "Internal error: '>>=' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    auto left_ref_type = std::dynamic_pointer_cast<reference_type>(expr.left()->get_type());
    auto left_type = type::remove_const(left_ref_type->get_subtype());
    auto llvm_type = _context->get_llvm_type(left_type);

    auto left_val = _builder->CreateLoad(llvm_type, left);
    if(auto prim = std::dynamic_pointer_cast<primitive_type>(left_type)) {
        if(prim->is_integer()) {
            if(prim->is_unsigned()) {
                // TODO may it poison when overflow ?
                _value = _builder->CreateLShr(left_val, right);
            } else {
                // TODO may it poison when overflow ?
                _value = _builder->CreateAShr(left_val, right);
            }
        } else if(prim->is_float()) {
            throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_DIV_ASSIGN_INCOMPATIBLE), expr.first_lexeme(),
                "Right shift-assignment ('>>=') cannot be applied to floating-point values: "
                "shift operations are only defined for integer types; "
                "the operand has type '{}'",
                {prim->to_string()});
        }
    } else {
        // TODO: Support other types
    }

    // Store the value, return the left ref
    _value = _builder->CreateStore(_value, left);
    _value = left;
}

//
// Arithmetic unary expression
//

} // namespace k::model::gen
