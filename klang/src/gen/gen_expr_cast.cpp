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
#include "../model/expressions.hpp"
#include "../model/statements.hpp"
#include "../model/operators.hpp"
#include "../model/mangler.hpp"
#include "../model/imported.hpp"
#include "../model/template.hpp"
#include "../model/template_instantiator.hpp"
#include "../parse/ast.hpp"
#include "../../../libkdi/src/kdi_aggregates.hpp"
#include "llvm/Support/raw_os_ostream.h"
#include <llvm/IR/Constants.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Intrinsics.h>
#include <unordered_set>
#include "../errors.hpp"
namespace k::model::gen {
// cast_expression, dynamic_cast, null_check

void type_reference_resolver::visit_cast_expression(cast_expression& expr) {
    auto sub_expr = expr.sub_expr();
    sub_expr->accept(*this);

    auto source_type = sub_expr->get_type();
    auto target_type = expr.get_cast_type();

    // Step 1: Resolve the sub-expression and the target type
    // ── Resolve the target type if it is not yet resolved ────────────────────
    // A `(Type)expr` cast produces an unresolved type for named types (struct/class/interface).
    // We must resolve it here before any validation.
    if (target_type && !type::is_resolved(target_type)) {
        // Step 1: try context::resolve_type — handles composite types (ptr<unresolved>, etc.)
        // and looks up structs in the context's _struct_types registry.
        auto resolved = _context->resolve_type(target_type);
        if (resolved && type::is_resolved(resolved)) {
            target_type = resolved;
            expr.set_cast_type(target_type);
        } else {
            // Step 2: for types not in context registry, use name-based resolution from root ns.
            // This handles types in namespaces or types that haven't been registered yet.
            auto resolve_by_name_composite = [&](const auto& self, const std::shared_ptr<type>& t) -> std::shared_ptr<type> {
                if (!t) return nullptr;
                if (type::is_resolved(t)) return t;
                if (auto unres = std::dynamic_pointer_cast<unresolved_type>(t)) {
                    if (auto already = unres->get_resolved()) return already;
                    auto root_ns = _unit.get_root_namespace();
                    if (root_ns) return resolve_type_by_name(unres->type_id(), *root_ns);
                    return nullptr;
                }
                auto sub = self(self, t->get_subtype());
                if (!sub || !type::is_resolved(sub)) return nullptr;
                if (type::is_pointer(t))   return sub->get_pointer();
                if (type::is_link(t))      return sub->get_link();
                if (type::is_view(t))    return sub->get_view();
                if (type::is_reference(t)) return sub->get_reference();
                if (type::is_const(t))     return sub->get_const();
                return nullptr;
            };
            auto resolved2 = resolve_by_name_composite(resolve_by_name_composite, target_type);
            if (resolved2 && type::is_resolved(resolved2)) {
                target_type = resolved2;
                expr.set_cast_type(target_type);
            } else {
                throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_CAST_OPERATOR_NOT_FOUND), expr.first_lexeme(),
                    "Cannot resolve target type of explicit cast: '{}' is unknown in this scope",
                    {target_type ? target_type->to_string() : "?"});
            }
        }
    }

    if(source_type==target_type) {
        // TODO warn about useless casting
    } else {
        // ── Helper: extract struct_type from an indirection (lnk/pin/ptr) ────
        auto get_indir_struct = [](const std::shared_ptr<type>& t) -> std::shared_ptr<struct_type> {
            if (auto lnk = std::dynamic_pointer_cast<link_type>(t))
                return std::dynamic_pointer_cast<struct_type>(type::remove_const(lnk->get_linked_type()));
            if (auto view_var = std::dynamic_pointer_cast<view_type>(t))
                return std::dynamic_pointer_cast<struct_type>(type::remove_const(view_var->get_viewed_type()));
            if (auto ptr = std::dynamic_pointer_cast<pointer_type>(t))
                return std::dynamic_pointer_cast<struct_type>(type::remove_const(ptr->get_pointed_type()));
            return nullptr;
        };

        // ── Helper: is target a non-null indirection (lnk or ref) ────────────
        auto target_is_nonnull = [](const std::shared_ptr<type>& t) -> bool {
            return type::is_link(t) || type::is_reference(t);
        };

        // ── Unwrap ref<indir> source once ─────────────────────────────────────
        // Allows explicit casts like (Base*)(ref<ptr<Derived>>)
        auto effective_source = source_type;
        bool source_unwrapped_ref = false;
        if (type::is_reference(source_type)) {
            auto inner = std::dynamic_pointer_cast<reference_type>(source_type)->get_subtype();
            if (type::is_link(inner) || type::is_view(inner) || type::is_pointer(inner)) {
                effective_source = inner;
                source_unwrapped_ref = true;
            }
        }

        // ── Case: ptr/lnk/pin source → ptr/lnk/pin target ────────────────────
        if ((type::is_pointer(effective_source) || type::is_link(effective_source) || type::is_view(effective_source)) &&
            (type::is_pointer(target_type)       || type::is_link(target_type)       || type::is_view(target_type))) {

            auto src_st = get_indir_struct(effective_source);
            auto tgt_st_type = get_indir_struct(target_type);
            if (src_st && tgt_st_type) {
                auto src_agg = src_st->get_struct();
                auto tgt_agg = tgt_st_type->get_struct();
                if (src_agg && tgt_agg && src_agg != tgt_agg) {
                    if (src_agg->is_derived_from(tgt_agg)) {
                        // Static upcast: ptr/lnk/pin<Derived> → ptr/lnk/pin<Base>
                        // IR handles GEP; model-level: no load_value wrapping needed.
                        // null_is_fatal not needed for static upcast.
                    } else if (tgt_agg->is_derived_from(src_agg) &&
                               tgt_agg->has_rtti()) {
                        // Set null_is_fatal on the cast_expression for non-null targets.
                        expr.set_null_is_fatal(target_is_nonnull(target_type));
                    } else {
                        throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_CAST_INCOMPATIBLE), expr.first_lexeme(),
                            "Explicit cast: cannot cast from '{}' to '{}': "
                            "the pointed types have no inheritance relationship",
                            {source_type->to_string(), target_type->to_string()});
                    }
                }
                // Same struct type: allowed (e.g. ptr<T>→lnk<T>).
            }
            // If source or target does not point to a struct/class: allowed (opaque ptr reinterpret).
        }

        // ── Case: indirection/null source → bool target ───────────────────────
        else if ((type::is_pointer(effective_source) || type::is_link(effective_source) ||
                  type::is_view(effective_source) || type::is_owner(effective_source) ||
                  type::is_null(effective_source)) && type::is_prim_bool(target_type)) {
            // Indirection-to-bool or null-to-bool: valid (null check). No model transformation needed.
        }

        // ── Case: ref<Struct> → ref<Struct> (same handling as implicit upcast/downcast) ──
        else if (type::is_reference(source_type) && type::is_reference(target_type)) {
            // Struct reference upcast: ref<Derived> → ref<Base>
            // or dynamic downcast: ref<Base> → ref<Derived> (fatal if RTTI mismatch)
            auto src_ref = std::dynamic_pointer_cast<reference_type>(source_type);
            auto tgt_ref = std::dynamic_pointer_cast<reference_type>(target_type);
            auto src_sub_nc = type::remove_const(src_ref->get_referenced_type());
            auto tgt_sub_nc = type::remove_const(tgt_ref->get_referenced_type());
            auto src_st_type = std::dynamic_pointer_cast<struct_type>(src_sub_nc);
            auto tgt_st_type = std::dynamic_pointer_cast<struct_type>(tgt_sub_nc);
            if (src_st_type && tgt_st_type && src_st_type != tgt_st_type) {
                auto src_agg = src_st_type->get_struct();
                auto tgt_agg = tgt_st_type->get_struct();
                if (src_agg && tgt_agg) {
                    if (src_agg->is_derived_from(tgt_agg)) {
                        // Static upcast ref<Derived>→ref<Base>: handled by IR GEP.
                    } else if (tgt_agg->is_derived_from(src_agg) &&
                               tgt_agg->has_rtti()) {
                        // Dynamic downcast ref<Base>→ref<Derived>: ref is non-null → fatal.
                        expr.set_null_is_fatal(true);
                    } else {
                        throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_CAST_UNSUPPORTED), expr.first_lexeme(),
                            "Explicit cast: cannot cast reference from '{}' to '{}': "
                            "the referenced types have no inheritance relationship",
                            {source_type->to_string(), target_type->to_string()});
                    }
                }
            }
            // Keep as-is (no load_value replacement).
        }

        // Step 2: Check for casting operator overload on the source struct type
        // ── Casting operator overload: (TargetType)struct_value ──────────────
        // Check BEFORE ref→value unwrapping so that the reference is preserved
        // as the 'this' parameter for the casting operator call.
        else if(type::is_reference(source_type)) {
            auto get_source_aggregate = [](const std::shared_ptr<type>& src) -> std::shared_ptr<aggregate> {
                auto effective = src;
                if (type::is_reference(effective)) {
                    effective = std::dynamic_pointer_cast<reference_type>(effective)->get_referenced_type();
                }
                effective = type::remove_const(effective);
                if (auto st = std::dynamic_pointer_cast<struct_type>(effective)) {
                    return st->get_struct();
                }
                return nullptr;
            };

            // Step 3: Validate primitive casts (widening, narrowing, int↔float)
            auto source_agg = get_source_aggregate(source_type);
            if (source_agg) {
                bool is_const_this = false;
                auto ref_sub = std::dynamic_pointer_cast<reference_type>(source_type)->get_referenced_type();
                is_const_this = type::is_const(ref_sub);

                // Step 4: Validate pointer/link/view/owner casts (same indirection kind or cross-kind)
                auto cast_func = resolve_cast_operator_overload(source_agg, target_type, is_const_this);
                if (cast_func) {
                    expr.set_operator_func(cast_func);

                    // Step 5: Validate struct upcast (static) and downcast (dynamic, requires RTTI)
                    // Compute virtual dispatch info if the function is virtual
                    if (cast_func->is_virtual() && cast_func->get_vtable_slot() >= 0) {
                        auto receiver_type = source_type;
                        auto di = compute_operator_dispatch_info(cast_func, receiver_type);
                        expr.set_operator_dispatch_info(std::move(di));
                    }

                    // Step 6: Set the result type to the cast target type
                    expr.set_type(target_type);
                    return;
                }
            }

            // No casting operator found: fall back to ref<T> → T load
            auto deref = load_value_expression::make_shared(sub_expr->shared_as<expression>());
            expr.assign(deref);
            deref->set_type(source_type->get_subtype());
        }
    }

    expr.set_type(expr.get_cast_type());
}

/**
 * Generate LLVM IR for a cast expression.
 *
 * Steps:
 *   1. If casting operator overload: delegate to generate_cast_operator_overload.
 *   2. Primitive-to-primitive: emit trunc/zext/sext/fptrunc/fpext/sitofp/fptosi.
 *   3. Pointer/link/view/owner casts: bitcast or GEP for struct upcast.
 *   4. Struct upcast: GEP to base sub-object at known offset.
 *   5. Dynamic downcast: emit RTTI-based dynamic cast IR.
 *   6. Null → pointer/link/view: emit null constant.
 *   7. Enum ↔ underlying: emit zext/trunc as needed.
 */
void implementation_generator::visit_cast_expression(cast_expression& expr) {
    // Step 1: If casting operator overload: delegate to generate_cast_operator_overload
    // ── Casting operator overload: call __operator_cv_<type>() ───────────────
    if (generate_cast_operator_overload(expr)) return;

    auto source_type = expr.sub_expr()->get_type();
    auto target_type = expr.get_cast_type();

    if(!source_type->is_resolved() || !target_type->is_resolved()) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F033), expr.first_lexeme(),
            "Internal error: cast expression has an unresolved source or target type; "
            "type resolution must complete before code generation");
    }

    // ── Enum ↔ primitive / enum ↔ enum casts ─────────────────────────────────
    // At LLVM IR level, enums are just integers. The cast is a no-op if the
    // underlying types match, or an integer truncation/extension otherwise.
    {
        auto src_nc = type::remove_const(source_type);
        auto tgt_nc = type::remove_const(target_type);
        auto enum_src = std::dynamic_pointer_cast<enum_type>(src_nc);
        auto enum_tgt = std::dynamic_pointer_cast<enum_type>(tgt_nc);
        if (enum_src || enum_tgt) {
            // ── Object value/ref -> object-backed enum: linear lookup in backing table ──
            if (!enum_src && enum_tgt && enum_tgt->is_object_backed()) {
                auto obj_type = enum_tgt->get_object_type();
                auto src_st = std::dynamic_pointer_cast<struct_type>(src_nc);
                if (!src_st && type::is_reference(src_nc)) {
                    auto src_ref = std::dynamic_pointer_cast<reference_type>(src_nc);
                    src_st = std::dynamic_pointer_cast<struct_type>(type::remove_const(src_ref->get_subtype()));
                }

                if (obj_type && src_st && src_st == obj_type) {
                    _value = nullptr;
                    expr.sub_expr()->accept(*this);
                    if (!_value) return;

                    auto en = enum_tgt->get_enumeration();
                    if (!en) return;
                    auto* table_gv = ensure_enum_object_table_reference(en, _context);
                    if (!table_gv) return;

                    auto* arr_ty = llvm::dyn_cast<llvm::ArrayType>(table_gv->getValueType());
                    auto* llvm_st = arr_ty ? llvm::dyn_cast<llvm::StructType>(arr_ty->getElementType()) : nullptr;
                    auto* tgt_int_ty = llvm::dyn_cast_or_null<llvm::IntegerType>(enum_tgt->get_llvm_type());
                    if (!arr_ty || !llvm_st || !tgt_int_ty) return;

                    llvm::Value* src_ptr = _value;
                    // Symbol values for non-reference structs are addresses already; cast expressions may also provide pointers.
                    if (!src_ptr->getType()->isPointerTy()) return;

                    auto* cur_fn = _builder->GetInsertBlock()->getParent();
                    auto* i1_ty = llvm::Type::getInt1Ty(_builder->getContext());

                    llvm::Function* equals_fn = nullptr;
                    if (auto obj_agg = obj_type->get_struct()) {
                        std::shared_ptr<function> eq_model = obj_agg->get_function("equals");
                        if (!eq_model) eq_model = obj_agg->get_function("__operator_eq_");
                        if (eq_model) {
                            equals_fn = get_module().getFunction(eq_model->get_mangled_name());
                        }
                    }
                    if (!equals_fn) {
                        throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_CAST_UNSUPPORTED), expr.first_lexeme(),
                            "Object-backed enum cast '{}' -> '{}' requires underlying type '{}' to define equality (equals/==)",
                            {source_type->to_string(), target_type->to_string(), obj_type->to_string()});
                    }

                    auto default_entry = en->get_default_entry();
                    llvm::Value* found = llvm::ConstantInt::getFalse(i1_ty);
                    llvm::Value* out_idx = llvm::ConstantInt::get(
                        tgt_int_ty,
                        static_cast<uint64_t>(default_entry.value),
                        default_entry.value < 0);

                    for (size_t i = 0; i < en->entries().size(); ++i) {
                        llvm::Value* indices[] = {
                            llvm::ConstantInt::get(llvm::Type::getInt64Ty(_builder->getContext()), 0),
                            llvm::ConstantInt::get(llvm::Type::getInt64Ty(_builder->getContext()), i)
                        };
                        auto* table_elem_ptr = _builder->CreateInBoundsGEP(
                            arr_ty,
                            table_gv,
                            llvm::ArrayRef<llvm::Value*>(indices),
                            "enum_obj_tbl_elem");

                        llvm::Value* is_match = nullptr;
                        auto* eq_call = _builder->CreateCall(equals_fn, {src_ptr, table_elem_ptr}, "enum_obj_equals");
                        if (eq_call->getType()->isIntegerTy(1)) {
                            is_match = eq_call;
                        } else if (eq_call->getType()->isIntegerTy()) {
                            is_match = _builder->CreateICmpNE(eq_call,
                                llvm::ConstantInt::get(eq_call->getType(), 0),
                                "enum_obj_equals_bool");
                        }

                        if (!is_match) {
                            is_match = llvm::ConstantInt::getTrue(i1_ty);
                            unsigned field_index = 0;
                            for (auto it = obj_type->fields_begin(); it != obj_type->fields_end(); ++it, ++field_index) {
                                auto ft = it->field_type.lock();
                                if (!ft) continue;
                                auto* llvm_ft = _context->get_llvm_type(ft);
                                if (!llvm_ft) continue;

                                auto* src_field_ptr = _builder->CreateStructGEP(
                                    llvm_st, src_ptr, field_index, "enum_obj_src_field_ptr");
                                auto* tbl_field_ptr = _builder->CreateStructGEP(
                                    llvm_st, table_elem_ptr, field_index, "enum_obj_tbl_field_ptr");
                                auto* src_field = _builder->CreateLoad(llvm_ft, src_field_ptr, "enum_obj_src_field");
                                auto* tbl_field = _builder->CreateLoad(llvm_ft, tbl_field_ptr, "enum_obj_tbl_field");

                                llvm::Value* field_eq = nullptr;
                                if (llvm_ft->isIntegerTy() || llvm_ft->isPointerTy()) {
                                    field_eq = _builder->CreateICmpEQ(src_field, tbl_field, "enum_obj_field_eq");
                                } else if (llvm_ft->isFloatingPointTy()) {
                                    field_eq = _builder->CreateFCmpOEQ(src_field, tbl_field, "enum_obj_field_eq");
                                } else {
                                    field_eq = llvm::ConstantInt::getFalse(i1_ty);
                                }
                                is_match = _builder->CreateAnd(is_match, field_eq, "enum_obj_entry_match");
                            }
                        }

                        auto* not_found_yet = _builder->CreateNot(found, "enum_obj_not_found_yet");
                        auto* take_this = _builder->CreateAnd(not_found_yet, is_match, "enum_obj_take_this");
                        auto* idx_const = llvm::ConstantInt::get(tgt_int_ty, static_cast<uint64_t>(i), false);
                        out_idx = _builder->CreateSelect(take_this, idx_const, out_idx, "enum_obj_idx_sel");
                        found = _builder->CreateOr(found, is_match, "enum_obj_found");
                    }

                    if (_null_failure_bb) {
                        auto* ok_bb = llvm::BasicBlock::Create(_builder->getContext(), "enum_obj_cast_ok", cur_fn);
                        auto* should_fail = _builder->CreateNot(found, "enum_obj_cast_fail");
                        _builder->CreateCondBr(should_fail, _null_failure_bb, ok_bb);
                        _builder->SetInsertPoint(ok_bb);
                    } else {
                        auto* ok_bb = llvm::BasicBlock::Create(_builder->getContext(), "enum_obj_cast_ok", cur_fn);
                        auto* fail_bb = llvm::BasicBlock::Create(_builder->getContext(), "enum_obj_cast_fail", cur_fn);
                        _builder->CreateCondBr(found, ok_bb, fail_bb);
                        _builder->SetInsertPoint(fail_bb);
                        auto* trap_fn = llvm::Intrinsic::getDeclaration(&get_module(), llvm::Intrinsic::trap);
                        _builder->CreateCall(trap_fn, {});
                        _builder->CreateUnreachable();
                        _builder->SetInsertPoint(ok_bb);
                    }

                    _value = out_idx;
                    return;
                }
            }

            // ── Object-backed enum → const T& (reference to backing table element) ──
            if (enum_src && enum_src->is_object_backed() && !enum_tgt) {
                // Check target is a reference to the object type
                auto obj_type = enum_src->get_object_type();
                auto tgt_ref = std::dynamic_pointer_cast<reference_type>(tgt_nc);
                if (!tgt_ref) {
                    if (auto tgt_const = std::dynamic_pointer_cast<const_type>(tgt_nc)) {
                        tgt_ref = std::dynamic_pointer_cast<reference_type>(tgt_const->get_subtype());
                    }
                }
                bool is_table_gep = tgt_ref && obj_type &&
                    type::remove_const(tgt_ref->get_subtype()) == obj_type;

                if (is_table_gep) {
                    // Get the enum index value
                    _value = nullptr;
                    expr.sub_expr()->accept(*this);
                    if (!_value) return;

                    // Get the backing table global
                    auto en = enum_src->get_enumeration();
                    if (!en) return;
                    auto* table_gv = ensure_enum_object_table_reference(en, _context);
                    if (!table_gv) return;
                    auto* arr_ty = llvm::dyn_cast<llvm::ArrayType>(
                        table_gv->getValueType());
                    if (!arr_ty) return;

                    // Zero-extend the index to i64 for GEP
                    auto* idx_i64 = _builder->CreateZExt(_value,
                        llvm::Type::getInt64Ty(_builder->getContext()),
                        "enum_tbl_idx");

                    // GEP: &table[0][enum_index] → ptr to the struct element
                    llvm::Value* indices[] = {
                        llvm::ConstantInt::get(llvm::Type::getInt64Ty(_builder->getContext()), 0),
                        idx_i64
                    };
                    _value = _builder->CreateInBoundsGEP(
                        arr_ty,
                        table_gv,
                        llvm::ArrayRef<llvm::Value*>(indices),
                        "enum_tbl_elem");

                    // _value is now a pointer to the struct element (= the reference value)
                    return;
                }
            }

            // Evaluate source expression
            _value = nullptr;
            expr.sub_expr()->accept(*this);
            if (!_value) return;

            llvm::Type* src_llvm = enum_src ? enum_src->get_llvm_type()
                : std::dynamic_pointer_cast<primitive_type>(src_nc)->get_llvm_type();
            llvm::Type* tgt_llvm = enum_tgt ? enum_tgt->get_llvm_type()
                : std::dynamic_pointer_cast<primitive_type>(tgt_nc)->get_llvm_type();

            if (src_llvm == tgt_llvm) {
                // Same LLVM type: no-op cast
                return;
            }
            // Integer widening/narrowing
            auto src_int = llvm::dyn_cast<llvm::IntegerType>(src_llvm);
            auto tgt_int = llvm::dyn_cast<llvm::IntegerType>(tgt_llvm);
            if (src_int && tgt_int) {
                if (tgt_int->getBitWidth() > src_int->getBitWidth()) {
                    // Determine signedness from enum's underlying type or primitive
                    bool is_signed = false;
                    if (enum_src) {
                        is_signed = !enum_src->get_underlying_type()->is_unsigned();
                    } else if (auto ps = std::dynamic_pointer_cast<primitive_type>(src_nc)) {
                        is_signed = !ps->is_unsigned();
                    }
                    _value = is_signed
                        ? _builder->CreateSExt(_value, tgt_llvm, "enum_sext")
                        : _builder->CreateZExt(_value, tgt_llvm, "enum_zext");
                } else {
                    _value = _builder->CreateTrunc(_value, tgt_llvm, "enum_trunc");
                }
                return;
            }
            return;
        }
    }

    // ── ref<T> → link<T> or ref<T> → pin<T>: no-op (same LLVM ptr) ────────────
    if (type::is_reference(source_type) &&
        (type::is_link(target_type) || type::is_view(target_type))) {
        auto src_sub = type::remove_const(std::dynamic_pointer_cast<reference_type>(source_type)->get_subtype());
        auto tgt_sub = type::remove_const(target_type->get_subtype());
        if (src_sub == tgt_sub) {
            // ref<T> and link<T>/pin<T> are both LLVM pointers — no IR conversion needed.
            _value = nullptr;
            expr.sub_expr()->accept(*this);
            return;
        }
    }

    // ── drain<T> ↔ ref<T> / drain<T> ↔ drain<T>: no-op (same LLVM opaque ptr) ──
    if ((type::is_drain(source_type) || type::is_reference(source_type)) &&
        (type::is_drain(target_type) || type::is_reference(target_type) ||
         type::is_link(target_type) || type::is_view(target_type))) {
        auto src_sub = type::remove_const(source_type->get_subtype());
        auto tgt_sub = type::remove_const(target_type->get_subtype());
        if (src_sub == tgt_sub) {
            // drain<T> and ref<T> are both LLVM opaque pointers — no IR conversion needed.
            _value = nullptr;
            expr.sub_expr()->accept(*this);
            return;
        }
    }

    // ── Struct reference upcast: ref<Derived> → ref<Base> ────────────────────
    // Both source and target are references to struct types. We need to GEP to the
    // base subobject field within the derived struct.
    if (type::is_reference(source_type) && type::is_reference(target_type)) {
        auto src_ref = std::dynamic_pointer_cast<reference_type>(source_type);
        auto tgt_ref = std::dynamic_pointer_cast<reference_type>(target_type);
        auto src_st_type = std::dynamic_pointer_cast<struct_type>(type::remove_const(src_ref->get_referenced_type()));
        auto tgt_st_type = std::dynamic_pointer_cast<struct_type>(type::remove_const(tgt_ref->get_referenced_type()));
        if (src_st_type && tgt_st_type && src_st_type != tgt_st_type) {
            auto src_st = src_st_type->get_struct();
            auto tgt_st = tgt_st_type->get_struct();
            if (src_st && tgt_st && src_st->is_derived_from(tgt_st)) {
                // ── Static upcast: ref<Derived> → ref<Base> — GEP to base subobject ──
                _value = nullptr;
                expr.sub_expr()->accept(*this);
                if (!_value) return;

                // Find the base subobject field index in the derived struct
                // The base subobject is stored as "__base_<name>__" or "__vbase_<name>__" member
                std::string subobj_name;
                for (auto& bs : src_st->get_bases()) {
                    if (bs.base && bs.base.get() == tgt_st.get()) {
                        subobj_name = bs.is_virtual
                            ? "__vbase_" + bs.sanitised_name() + "__"
                            : "__base_" + bs.sanitised_name() + "__";
                        break;
                    }
                }
                // Also check for __vbase_ in the most-derived class (transitively virtual)
                if (subobj_name.empty()) {
                    std::string vbase_name = "__vbase_" + tgt_st->get_short_name() + "__";
                    if (src_st_type->get_member(vbase_name)) {
                        subobj_name = vbase_name;
                    }
                }
                if (!subobj_name.empty()) {
                    auto src_llvm_type = _context->get_llvm_type(src_st_type);
                    if (auto field = src_st_type->get_member(subobj_name)) {
                        _value = _builder->CreateStructGEP(
                            src_llvm_type,
                            _value,
                            (unsigned)field->index,
                            "base_" + tgt_st->get_short_name() + "_ptr"
                        );
                        return;
                    }
                }

                // ── Transitive upcast: tgt_st is a transitive (non-direct) base of src_st.
                if (subobj_name.empty()) {
                    std::function<bool(aggregate*, struct_type*, llvm::Value*)> dfs_gep;
                    dfs_gep = [&](aggregate* cur_agg, struct_type* cur_st_type, llvm::Value* cur_ptr) -> bool {
                        for (auto& bs : cur_agg->get_bases()) {
                            if (!bs.base || bs.is_virtual) continue;
                            std::string field_name = "__base_" + bs.sanitised_name() + "__";
                            auto field = cur_st_type->get_member(field_name);
                            if (!field) continue;

                            auto base_agg = bs.base;
                            auto base_st_type = base_agg->get_struct_type();
                            if (!base_st_type) continue;

                            llvm::Type* cur_llvm_type = cur_st_type->get_llvm_type();
                            if (!cur_llvm_type) continue;
                            llvm::Value* base_ptr = _builder->CreateStructGEP(
                                cur_llvm_type, cur_ptr, (unsigned)field->index,
                                "trans_base_" + bs.sanitised_name() + "_ptr");

                            if (bs.base.get() == tgt_st.get()) {
                                _value = base_ptr;
                                return true;
                            }

                            // Check if tgt_st is a direct __vbase_ of this intermediate
                            std::string vbase_name2 = "__vbase_" + tgt_st->get_short_name() + "__";
                            if (auto vbase_field2 = base_st_type->get_member(vbase_name2)) {
                                llvm::Type* inter_llvm_type = base_st_type->get_llvm_type();
                                if (!inter_llvm_type) continue;
                                _value = _builder->CreateStructGEP(
                                    inter_llvm_type, base_ptr, (unsigned)vbase_field2->index,
                                    "trans_vbase_" + tgt_st->get_short_name() + "_ptr");
                                return true;
                            }

                            if (dfs_gep(bs.base.get(), base_st_type.get(), base_ptr)) {
                                return true;
                            }
                        }
                        return false;
                    };
                    if (dfs_gep(src_st.get(), src_st_type.get(), _value)) return;
                }

                // Virtual base via vbptr
                {
                    std::string vbptr_name = "__vbptr_" + tgt_st->get_short_name() + "__";
                    auto src_llvm_type = _context->get_llvm_type(src_st_type);
                    if (auto vbptr_field = src_st_type->get_member(vbptr_name)) {
                        llvm::Type* ptr_ty = llvm::PointerType::get(_context->llvm_context(), 0);
                        llvm::Value* vbptr_addr = _builder->CreateStructGEP(
                            src_llvm_type, _value, (unsigned)vbptr_field->index,
                            "vbptr_" + tgt_st->get_short_name() + "_addr");
                        _value = _builder->CreateLoad(ptr_ty, vbptr_addr,
                            "vbase_" + tgt_st->get_short_name() + "_ptr");
                        return;
                    }
                }
                // Fallback: return as-is (pointer reinterpret for same-layout case)
                return;
            }
            // If tgt_st is derived from src_st (dynamic downcast ref<Base>→ref<Derived>),
            // fall through to the dynamic cast block below — do NOT handle here.
            // Only do a no-op for truly same/unrelated types.
            bool is_dynamic_downcast = src_st && tgt_st &&
                tgt_st->is_derived_from(src_st) &&
                tgt_st->has_rtti();
            if (!is_dynamic_downcast) {
                // Same type or unrelated: no-op
                _value = nullptr;
                expr.sub_expr()->accept(*this);
                return;
            }
            // else: fall through to dynamic cast block
        } else {
            // Same type, no-op
            _value = nullptr;
            expr.sub_expr()->accept(*this);
            return;
        }
    }
    // For lien~, pin^, ptr* pointing to Derived → lien~, pin^, ptr* pointing to Base.
    // All are LLVM opaque pointers; same GEP strategy applies.
    // Also handles ref<ptr<Derived>>→lien<Base> etc. (load through ref first).
    {
        // Determine source and target struct_type from indirection kind
        std::shared_ptr<struct_type> indir_src_st_type, indir_tgt_st_type;
        bool is_indir_upcast = false;
        bool src_needs_load = false; // source is ref<indirection>

        auto get_indir_pointed = [](const std::shared_ptr<type>& t) -> std::shared_ptr<struct_type> {
            if (auto lnk = std::dynamic_pointer_cast<link_type>(t))
                return std::dynamic_pointer_cast<struct_type>(type::remove_const(lnk->get_linked_type()));
            if (auto view_var = std::dynamic_pointer_cast<view_type>(t))
                return std::dynamic_pointer_cast<struct_type>(type::remove_const(view_var->get_viewed_type()));
            if (auto ptr = std::dynamic_pointer_cast<pointer_type>(t))
                return std::dynamic_pointer_cast<struct_type>(type::remove_const(ptr->get_pointed_type()));
            return nullptr;
        };

        // Effective source: if ref<indirection>, unwrap ref for type checks (load needed)
        auto effective_source = source_type;
        if (type::is_reference(source_type)) {
            auto inner = std::dynamic_pointer_cast<reference_type>(source_type)->get_subtype();
            if (type::is_link(inner) || type::is_view(inner) || type::is_pointer(inner)) {
                effective_source = inner;
                src_needs_load = true;
            }
        }

        bool src_is_indir = type::is_link(effective_source) || type::is_view(effective_source) || type::is_pointer(effective_source);
        bool tgt_is_indir = type::is_link(target_type) || type::is_view(target_type) || type::is_pointer(target_type);
        if (src_is_indir && tgt_is_indir) {
            indir_src_st_type = get_indir_pointed(effective_source);
            indir_tgt_st_type = get_indir_pointed(target_type);
            if (indir_src_st_type && indir_tgt_st_type && indir_src_st_type != indir_tgt_st_type) {
                auto src_st = indir_src_st_type->get_struct();
                auto tgt_st = indir_tgt_st_type->get_struct();
                if (src_st && tgt_st && src_st->is_derived_from(tgt_st)) {
                    is_indir_upcast = true;
                }
            }
        }

        if (is_indir_upcast) {
            auto src_st_type = indir_src_st_type;
            auto tgt_st = indir_tgt_st_type->get_struct();
            auto src_st = src_st_type->get_struct();

            _value = nullptr;
            expr.sub_expr()->accept(*this);
            if (!_value) return;

            // If source was ref<indirection>, load the pointer value first
            if (src_needs_load) {
                _value = _builder->CreateLoad(
                    _context->get_llvm_type(effective_source), _value, "indir_upcast_load");
            }

            // Same GEP strategy as ref<Derived>→ref<Base>
            std::string subobj_name;
            for (auto& bs : src_st->get_bases()) {
                if (bs.base && bs.base.get() == tgt_st.get()) {
                    subobj_name = bs.is_virtual
                        ? "__vbase_" + bs.sanitised_name() + "__"
                        : "__base_" + bs.sanitised_name() + "__";
                    break;
                }
            }
            if (subobj_name.empty()) {
                std::string vbase_name = "__vbase_" + tgt_st->get_short_name() + "__";
                if (src_st_type->get_member(vbase_name)) {
                    subobj_name = vbase_name;
                }
            }
            if (!subobj_name.empty()) {
                auto src_llvm_type = _context->get_llvm_type(src_st_type);
                if (auto field = src_st_type->get_member(subobj_name)) {
                    _value = _builder->CreateStructGEP(
                        src_llvm_type, _value, (unsigned)field->index,
                        "base_" + tgt_st->get_short_name() + "_ptr");
                    return;
                }
            }
            // Transitive upcast via DFS
            std::function<bool(aggregate*, struct_type*, llvm::Value*)> dfs_gep;
            dfs_gep = [&](aggregate* cur_agg, struct_type* cur_st_type, llvm::Value* cur_ptr) -> bool {
                for (auto& bs : cur_agg->get_bases()) {
                    if (!bs.base || bs.is_virtual) continue;
                    std::string field_name = "__base_" + bs.sanitised_name() + "__";
                    auto field = cur_st_type->get_member(field_name);
                    if (!field) continue;
                    // Use aggregate directly (works for both structure and klass/interface)
                    auto base_agg = bs.base;
                    auto base_st_type = base_agg->get_struct_type();
                    if (!base_st_type) continue;
                    llvm::Type* cur_llvm_type = cur_st_type->get_llvm_type();
                    if (!cur_llvm_type) continue;
                    llvm::Value* base_ptr = _builder->CreateStructGEP(
                        cur_llvm_type, cur_ptr, (unsigned)field->index,
                        "trans_base_" + bs.sanitised_name() + "_ptr");
                    if (bs.base.get() == tgt_st.get()) {
                        _value = base_ptr;
                        return true;
                    }
                    std::string vbase_name2 = "__vbase_" + tgt_st->get_short_name() + "__";
                    if (auto vbase_field2 = base_st_type->get_member(vbase_name2)) {
                        llvm::Type* inter_llvm_type = base_st_type->get_llvm_type();
                        if (!inter_llvm_type) continue;
                        _value = _builder->CreateStructGEP(
                            inter_llvm_type, base_ptr, (unsigned)vbase_field2->index,
                            "trans_vbase_" + tgt_st->get_short_name() + "_ptr");
                        return true;
                    }
                    if (dfs_gep(bs.base.get(), base_st_type.get(), base_ptr)) return true;
                }
                return false;
            };
            if (dfs_gep(src_st.get(), src_st_type.get(), _value)) return;

            // Virtual base via vbptr
            {
                std::string vbptr_name = "__vbptr_" + tgt_st->get_short_name() + "__";
                auto src_llvm_type = _context->get_llvm_type(src_st_type);
                if (auto vbptr_field = src_st_type->get_member(vbptr_name)) {
                    llvm::Type* ptr_ty = llvm::PointerType::get(_context->llvm_context(), 0);
                    llvm::Value* vbptr_addr = _builder->CreateStructGEP(
                        src_llvm_type, _value, (unsigned)vbptr_field->index,
                        "vbptr_" + tgt_st->get_short_name() + "_addr");
                    _value = _builder->CreateLoad(ptr_ty, vbptr_addr,
                        "vbase_" + tgt_st->get_short_name() + "_ptr");
                    return;
                }
            }
            // Fallback: return as-is
            return;
        }
    }

    // ── Struct reference → pointer upcast for virtual base vbptr deref ────────
    // When the type resolver sets target type to pointer<VirtualBase>, it means we need
    // to load the __vbptr_<name>__ field and use it as a pointer (which the subsequent
    // GEP in member_of_object_expression will use).
    // ── Struct reference → pointer for virtual base vbptr deref ─────────────
    // Only applies when source is ref<StructType> (not ref<ptr/lnk/pin>).
    // When source referenced type is itself an indirection (e.g. ref<ptr<Base>>→ptr<Derived>),
    // fall through to the dynamic cast block below.
    if (type::is_reference(source_type) && type::is_pointer(target_type)) {
        auto src_ref = std::dynamic_pointer_cast<reference_type>(source_type);
        auto src_inner = src_ref->get_referenced_type();
        // Only handle this block when source references a struct directly (not a ptr/lnk/pin).
        // If source is ref<ptr/lnk/pin>, fall through to dynamic cast.
        if (!type::is_any_indirection(src_inner)) {
            auto tgt_ptr = std::dynamic_pointer_cast<pointer_type>(target_type);
            auto src_st_type = std::dynamic_pointer_cast<struct_type>(type::remove_const(src_inner));
            auto tgt_st_type = std::dynamic_pointer_cast<struct_type>(tgt_ptr->get_pointed_type());
            if (src_st_type && tgt_st_type) {
                // Check if this is a dynamic downcast (Base→Derived): if so, fall through
                auto src_agg = src_st_type->get_struct();
                auto tgt_agg = tgt_st_type->get_struct();
                bool is_dynamic_downcast = src_agg && tgt_agg &&
                    tgt_agg->is_derived_from(src_agg) &&
                    tgt_agg->has_rtti();
                if (!is_dynamic_downcast) {
                    _value = nullptr;
                    expr.sub_expr()->accept(*this);
                    if (!_value) return;

                    std::string vbptr_name = "__vbptr_" + tgt_st_type->name() + "__";
                    auto src_llvm_type = _context->get_llvm_type(src_st_type);
                    if (auto field = src_st_type->get_member(vbptr_name)) {
                        // Load the vbptr field (it's an opaque ptr to the virtual base)
                        llvm::Type* ptr_ty = llvm::PointerType::get(_context->llvm_context(), 0);
                        llvm::Value* vbptr_field_addr = _builder->CreateStructGEP(
                            src_llvm_type, _value, (unsigned)field->index, "vbptr_" + tgt_st_type->name() + "_addr");
                        _value = _builder->CreateLoad(ptr_ty, vbptr_field_addr, "vbptr_" + tgt_st_type->name());
                        return;
                    }
                    // Fallback for non-dynamic ref<Struct>→ptr<Struct> (no vbptr found)
                    return;
                }
                // is_dynamic_downcast → fall through to dynamic cast block
            }
            // src_st_type or tgt_st_type null → fall through
        }
        // ref<ptr/lnk/pin> → fall through to dynamic cast block
    }

    // ── Indirection → bool: emit ICmpNE(ptr, null) ─────────────────────────
    if((type::is_pointer(source_type) || type::is_link(source_type) ||
        type::is_view(source_type) || type::is_owner(source_type)) &&
       type::is_prim_bool(target_type)) {
        _value = nullptr;
        expr.sub_expr()->accept(*this);
        if (!_value) return;
        auto null_ptr = llvm::ConstantPointerNull::get(
            llvm::PointerType::get(_builder->getContext(), 0));
        _value = _builder->CreateICmpNE(_value, null_ptr, "ind_to_bool");
        return;
    }
    // ── null → bool: always false ────────────────────────────────────────────
    if(type::is_null(source_type) && type::is_prim_bool(target_type)) {
        _value = _builder->getFalse();
        return;
    }

    // ── Indirection reinterpret: owner ↔ pointer ↔ link ↔ view ────────────
    // All indirection types share the same LLVM opaque-pointer representation,
    // so an owner-to-pointer borrow (or any other combination) is a no-op cast
    // when the inner types match.  We must NOT short-circuit when inner types
    // differ (e.g. Base* → Derived~ requires a dynamic cast).
    {
        auto is_heap_indirection = [](const std::shared_ptr<type>& t) {
            return type::is_owner(t) || type::is_pointer(t) ||
                   type::is_link(t) || type::is_view(t);
        };
        if (is_heap_indirection(source_type) && is_heap_indirection(target_type)) {
            auto src_inner = type::remove_const(source_type->get_subtype());
            auto tgt_inner = type::remove_const(target_type->get_subtype());
            bool match = (src_inner == tgt_inner);
            if (!match) {
                // Check array element const-widening: array<T> matches array<const<T>>
                auto sa = std::dynamic_pointer_cast<array_type>(src_inner);
                auto ta = std::dynamic_pointer_cast<array_type>(tgt_inner);
                if (sa && ta && !sa->is_sized() && !ta->is_sized()) {
                    match = (type::remove_const(sa->get_subtype()) == type::remove_const(ta->get_subtype()));
                }
            }
            if (match) {
                _value = nullptr;
                expr.sub_expr()->accept(*this);
                return;
            }
        }
        // indirection → reference: owner/ptr/lnk/pin<T> → ref<T>
        // Both are opaque pointers at LLVM IR level — no-op cast.
        // Also handles array element const-widening: array<T> → array<const<T>>.
        if (is_heap_indirection(source_type) && type::is_reference(target_type)) {
            auto src_inner = type::remove_const(source_type->get_subtype());
            auto tgt_inner = type::remove_const(std::dynamic_pointer_cast<reference_type>(target_type)->get_subtype());
            bool match = (src_inner == tgt_inner);
            if (!match) {
                // Check array element const-widening: array<T> matches array<const<T>>
                auto sa = std::dynamic_pointer_cast<array_type>(src_inner);
                auto ta = std::dynamic_pointer_cast<array_type>(tgt_inner);
                if (sa && ta && !sa->is_sized() && !ta->is_sized()) {
                    match = (type::remove_const(sa->get_subtype()) == type::remove_const(ta->get_subtype()));
                }
            }
            if (match) {
                _value = nullptr;
                expr.sub_expr()->accept(*this);
                return;
            }
        }
    }

    // ── Dynamic cast (RTTI-based): Base→Derived for klass/interface indirections ──
    // Triggered when target_st is derived from source_st (i.e. going "upward" in the
    // type hierarchy from a base pointer to a more-derived pointer).
    // This is the inverse of the static upcast handled above.
    {
        // Helper: extract struct_type from any indirection or ref<indirection> or ref<struct>
        auto get_pointed_struct = [](const std::shared_ptr<type>& t) -> std::shared_ptr<struct_type> {
            auto effective = t;
            if (auto ref = std::dynamic_pointer_cast<reference_type>(t)) {
                auto inner = ref->get_referenced_type();
                if (type::is_any_indirection(inner)) effective = inner;
                else return std::dynamic_pointer_cast<struct_type>(type::remove_const(inner));
            }
            if (auto lnk = std::dynamic_pointer_cast<link_type>(effective))
                return std::dynamic_pointer_cast<struct_type>(type::remove_const(lnk->get_linked_type()));
            if (auto view_var = std::dynamic_pointer_cast<view_type>(effective))
                return std::dynamic_pointer_cast<struct_type>(type::remove_const(view_var->get_viewed_type()));
            if (auto ptr = std::dynamic_pointer_cast<pointer_type>(effective))
                return std::dynamic_pointer_cast<struct_type>(type::remove_const(ptr->get_pointed_type()));
            return nullptr;
        };
        auto src_st_type = get_pointed_struct(source_type);
        auto tgt_st_type = get_pointed_struct(target_type);
        if (src_st_type && tgt_st_type) {
            auto src_st = src_st_type->get_struct();
            auto tgt_st = tgt_st_type->get_struct();
            bool is_dynamic = src_st && tgt_st &&
                tgt_st->is_derived_from(src_st) &&
                tgt_st->has_rtti();
            if (is_dynamic) {
                emit_dynamic_cast(expr, src_st_type, tgt_st_type);
                return;
            }
        }
    }

    // Step 2: Primitive-to-primitive: emit trunc/zext/sext/fptrunc/fpext/sitofp/fptosi
    if(!type::is_primitive(source_type) || !type::is_primitive(target_type)) {
        // ── Generic erasure: both sides are indirection types (all `ptr` in LLVM IR) ──
        // When generic synthesis maps T to byte*, we get casts like byte** → IntBox*
        // which are no-ops at LLVM IR level (opaque pointers).
        bool both_indirections =
            (type::is_any_indirection(source_type) || type::is_reference(source_type)) &&
            (type::is_any_indirection(target_type) || type::is_reference(target_type));
        if (both_indirections) {
            // All indirections are `ptr` at LLVM IR level — just emit the sub-expression.
            _value = nullptr;
            expr.sub_expr()->accept(*this);
            return;
        }
        throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_CAST_NOT_SUPPORTED), expr.first_lexeme(),
            "Casting between non-primitive types is not yet supported: "
            "cannot cast from '{}' to '{}'; only casts between primitive types are currently implemented",
            {source_type->to_string(), target_type->to_string()});
    }
    auto src = std::dynamic_pointer_cast<primitive_type>(source_type);
    auto tgt = std::dynamic_pointer_cast<primitive_type>(target_type);

    // Step 3: Pointer/link/view/owner casts: bitcast or GEP for struct upcast
    _value = nullptr;
    expr.sub_expr()->accept(*this);
    if(!_value) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F034), expr.first_lexeme(),
            "Internal error: the expression being cast produced no LLVM value; "
            "this indicates a code-generation bug in the sub-expression");
    }

    // Step 4: Struct upcast: GEP to base sub-object at known offset
    if(src->is_boolean()) {
        if(tgt->is_integer()) {
            // Bool is logically 0 or 1: always zero-extend, regardless of target signedness.
            _value = _builder->CreateZExt(_value, _builder->getIntNTy(tgt->type_size()));
        } else if (tgt->is_float()) {
            if(*tgt == primitive_type::FLOAT) {
                auto ftype = _context->get_llvm_type(tgt);
                auto ftrue = llvm::ConstantFP::get(ftype, llvm::APFloat(1.0f));
                auto ffalse = llvm::ConstantFP::get(ftype, llvm::APFloat(0.0f));
                _value = _builder->CreateSelect(_value, ftrue, ffalse);
            } else if(*tgt == primitive_type::DOUBLE) {
                auto dtype = _context->get_llvm_type(tgt);
                auto dtrue = llvm::ConstantFP::get(dtype, llvm::APFloat(1.0));
                auto dfalse = llvm::ConstantFP::get(dtype, llvm::APFloat(0.0));
                _value = _builder->CreateSelect(_value, dtrue, dfalse);
            } // else must not happen
        } else {
            // Support other types
        }
    } else if(src->is_integer()) {
        if(tgt->is_boolean()) {
            _value = _builder->CreateICmpNE(_value, _builder->getIntN(src->type_size(), 0));
        } else if (tgt->is_integer()) {
            if (tgt->is_signed()) {
                if (src->is_unsigned()) {
                    auto d = k::log::diagnostic::make_warning(static_cast<unsigned int>(k::diag::operator_diag::ERR_MUL_ASSIGN_INCOMPATIBLE),
                        "Casting an unsigned integer to a signed integer of the same size may produce "
                        "unexpected results if the value exceeds the signed range (overflow is implementation-defined)");
                    report(d);
                }
            } else /* if (tgt->is_unsigned())*/  {
                if (src->is_signed()) {
                    auto d = k::log::diagnostic::make_warning(static_cast<unsigned int>(k::diag::type_diag::WARN_CAST_SIGN_CHANGE),
                        "Casting a signed integer to an unsigned integer may reinterpret negative values "
                        "as large positive values (two's complement wrap-around)");
                    report(d);
                }
            }
            // Extension type depends on source signedness:
            // Step 7: Enum ↔ underlying: emit zext/trunc as needed
            // unsigned source → ZExt, signed source → SExt. Truncation is the same either way.
            if (src->is_unsigned()) {
                _value = _builder->CreateZExtOrTrunc(_value, _context->get_llvm_type(tgt));
            } else {
                _value = _builder->CreateSExtOrTrunc(_value, _context->get_llvm_type(tgt));
            }
        } else if (tgt->is_float()) {
            if(src->is_unsigned()) {
                if(*tgt == primitive_type::FLOAT) {
                    _value = _builder->CreateUIToFP(_value, _builder->getFloatTy());
                } else if(*tgt == primitive_type::DOUBLE) {
                    _value = _builder->CreateUIToFP(_value, _builder->getDoubleTy());
                } /* else must not happen */
            } else {
                if(*tgt == primitive_type::FLOAT) {
                    _value = _builder->CreateSIToFP(_value, _builder->getFloatTy());
                } else if(*tgt == primitive_type::DOUBLE) {
                    _value = _builder->CreateSIToFP(_value, _builder->getDoubleTy());
                } /* else must not happen */
            }
        } else {
            // Support other types
        }
    } else if(src->is_float()) {
        if(tgt->is_boolean()) {
            _value = _builder->CreateFCmpUNE(_value, llvm::ConstantFP::get(_context->get_llvm_type(src), 0.0));
        } else if(tgt->is_integer()) {
            if(tgt->is_unsigned()) {
                _value = _builder->CreateFPToUI(_value, _context->get_llvm_type(tgt));
            } else {
                _value = _builder->CreateFPToSI(_value, _context->get_llvm_type(tgt));
            }
        } else if(tgt->is_float()) {
            if(*src == primitive_type::FLOAT && *tgt == primitive_type::DOUBLE) {
                _value = _builder->CreateFPExt(_value, _context->get_llvm_type(tgt));
            } else if(*src == primitive_type::DOUBLE && *tgt == primitive_type::FLOAT) {
                _value = _builder->CreateFPTrunc(_value, _context->get_llvm_type(tgt));
            } else {
                // Do nothing, float type is already aligned
            }
        } else{
            // Support other types
        }
    } else {
        // Support other types
    }
}


// ─── Dynamic cast (RTTI-based): emit_dynamic_cast ────────────────────────────
//
// Called from visit_cast_expression when types require a runtime RTTI check.
// source is a base-typed indirection; target is a derived-typed indirection.
// Algorithm:
//  1. Evaluate source → raw base pointer.
//  2. Load the vptr (field 0 of the base klass layout).
//  3. Load vtable[0] → actual RTTI pointer of the most-derived object.
//  4. Compare with the RTTI global of the target klass.
//  5. On match: subtract compile-time byte-offset → Derived* result.
//  6. On mismatch: null.
//  7. If expr.null_is_fatal(): emit debugtrap on null (target is lnk or ref).
//
/**
 * Emit RTTI-based dynamic cast IR for a cast_expression.
 *
 * Steps:
 *   1. Load the vptr from the source object.
 *   2. Load the RTTI pointer from the vtable (slot 0).
 *   3. Call the runtime dynamic_cast function with source RTTI, target RTTI, and object ptr.
 *   4. If null_is_fatal: emit a null check + fatal trap.
 *   5. Set _value to the cast result pointer.
 */
void implementation_generator::emit_dynamic_cast(
        cast_expression& expr,
        std::shared_ptr<struct_type> src_st_type,
        std::shared_ptr<struct_type> tgt_st_type)
{
    auto& llvm_ctx  = _builder->getContext();
    auto* ptr_ty    = llvm::PointerType::get(llvm_ctx, 0);
    auto* i64_ty    = llvm::Type::getInt64Ty(llvm_ctx);

    auto source_type = expr.sub_expr()->get_type();

    auto src_st = src_st_type->get_struct();
    auto tgt_st = tgt_st_type->get_struct();

    if (!src_st || !tgt_st || !tgt_st->has_rtti()) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F040), expr.first_lexeme(),
            "emit_dynamic_cast: source or target is not a class/interface/annotation aggregate");
    }

    // ── 1. Evaluate sub-expression → raw base pointer ────────────────────────
    _value = nullptr;
    expr.sub_expr()->accept(*this);
    if (!_value) return;
    llvm::Value* base_raw = _value;

    // If source is ref<lnk/pin/ptr>, load the stored pointer value.
    {
        if (auto ref_t = std::dynamic_pointer_cast<reference_type>(source_type)) {
            auto inner = ref_t->get_referenced_type();
            if (type::is_any_indirection(inner)) {
                base_raw = _builder->CreateLoad(ptr_ty, base_raw, "dyncast_load_indir");
            }
        }
    }

    // ── 1b. Null-source guard for nullable indirections ─────────────────────
    // If the source is a nullable indirection (ptr, view, owner), the raw
    // pointer may be null.  Loading the vptr from null would segfault, so we
    // must check before proceeding with the RTTI comparison.
    {
        bool src_nullable = type::is_nullable_indirection(source_type);
        if (!src_nullable) {
            if (auto ref_t = std::dynamic_pointer_cast<reference_type>(source_type)) {
                src_nullable = type::is_nullable_indirection(ref_t->get_referenced_type());
            }
        }
        if (src_nullable) {
            auto target_type = expr.get_cast_type();
            if (_null_failure_bb && type::is_link(target_type)) {
                // Soft-fail: branch to else/continue if source is null.
                auto* cur_fn = _builder->GetInsertBlock()->getParent();
                auto* ok_bb = llvm::BasicBlock::Create(llvm_ctx, "dyncast_src_ok", cur_fn);
                auto* is_null = _builder->CreateICmpEQ(
                    base_raw,
                    llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptr_ty)),
                    "dyncast_src_null");
                _builder->CreateCondBr(is_null, _null_failure_bb, ok_bb);
                _builder->SetInsertPoint(ok_bb);
            } else if (expr.null_is_fatal()) {
                // Fatal: trap if source is null.
                auto* fatal_fn = get_or_declare_fatal_null_function("__k_fatal_null_dyncast");
                emit_null_check(base_raw, fatal_fn, "dyncast_src");
            }
            // For non-fatal targets (ptr/view) with null source, the vptr load
            // would still segfault.  This is a known limitation.
            // TODO: guard the vptr load for non-fatal nullable targets too.
        }
    }

    // ── 2. Find the RTTI global for the target class/annotation ─────────────
    std::string rtti_name = mangler::mangle_rtti(tgt_st->get_name());
    llvm::GlobalVariable* tgt_rtti_gv = _context->module().getNamedGlobal(rtti_name);
    if (!tgt_rtti_gv) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F041), expr.first_lexeme(),
            "emit_dynamic_cast: RTTI global '{}' not found in module",
            {rtti_name});
    }

    // Step 1: Load the vptr from the source object
    // ── 3. Load the vptr from the source object (field 0 of the aggregate) ──
    if (!src_st->has_rtti()) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F042), expr.first_lexeme(),
            "emit_dynamic_cast: source aggregate '{}' has no vtable/vptr",
            {src_st->get_short_name()});
    }
    auto src_vt = src_st->get_vtable();
    auto* src_llvm_type = src_st_type->get_llvm_type();
    if (!src_llvm_type) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F043), expr.first_lexeme(),
            "emit_dynamic_cast: source class LLVM type not built");
    }
    llvm::Value* vptr_addr = _builder->CreateStructGEP(
        src_llvm_type, base_raw, 0, "dyncast_vptr_addr");
    llvm::Value* vptr = _builder->CreateLoad(ptr_ty, vptr_addr, "dyncast_vptr");

    // Step 2: Load the RTTI pointer from the vtable (slot 0)
    // ── 4. Load vtable[0] → actual RTTI pointer ──────────────────────────────
    // For imported classes, no vtable_layout exists — synthesise a minimal
    // vtable struct { ptr } since RTTI is always at slot 0.
    llvm::Type* vt_llvm_type = nullptr;
    if (src_vt && src_vt->llvm_type) {
        vt_llvm_type = src_vt->llvm_type;
    } else {
        std::vector<llvm::Type*> vt_fields_vec;
        vt_fields_vec.push_back(ptr_ty);
        vt_llvm_type = llvm::StructType::get(llvm_ctx, vt_fields_vec);
    }
    llvm::Value* rtti_slot_addr = _builder->CreateStructGEP(
        vt_llvm_type, vptr, 0, "dyncast_rtti_slot_addr");
    llvm::Value* actual_rtti = _builder->CreateLoad(ptr_ty, rtti_slot_addr, "dyncast_actual_rtti");

    // ── 5. Compare RTTI pointers ──────────────────────────────────────────────
    llvm::Value* rtti_match = _builder->CreateICmpEQ(
        actual_rtti, tgt_rtti_gv, "dyncast_rtti_match");

    // ── 6. Compute adjusted pointer (Derived* = Base* − byte_offset) ─────────
    llvm::Value* derived_ptr = nullptr;
    {
        llvm::DataLayout dl(&_context->module());

        // Try direct base first
        std::string subobj_name;
        for (auto& bs : tgt_st->get_bases()) {
            if (!bs.base) continue;
            if (bs.base.get() == src_st.get()) {
                subobj_name = bs.is_virtual
                    ? "__vbase_" + bs.sanitised_name() + "__"
                    : "__base_" + bs.sanitised_name() + "__";
                break;
            }
        }
        if (subobj_name.empty()) {
            std::string vbase_name = "__vbase_" + src_st->get_short_name() + "__";
            if (tgt_st_type->get_member(vbase_name)) subobj_name = vbase_name;
        }
        if (!subobj_name.empty()) {
            if (auto field = tgt_st_type->get_member(subobj_name)) {
                auto* tgt_llvm_type = llvm::dyn_cast_or_null<llvm::StructType>(tgt_st_type->get_llvm_type());
                if (tgt_llvm_type) {
                    uint64_t off = dl.getStructLayout(tgt_llvm_type)->getElementOffset((unsigned)field->index);
                    llvm::Value* bi = _builder->CreatePtrToInt(base_raw, i64_ty, "dyncast_base_int");
                    llvm::Value* di = _builder->CreateSub(bi, llvm::ConstantInt::get(i64_ty, off), "dyncast_derived_int");
                    derived_ptr = _builder->CreateIntToPtr(di, ptr_ty, "dyncast_derived_ptr");
                }
            }
        }
        if (!derived_ptr) {
            // Transitive DFS: accumulate byte offsets through hierarchy
            std::function<int64_t(aggregate*, struct_type*)> dfs_offset;
            dfs_offset = [&](aggregate* cur_agg, struct_type* cur_st_type) -> int64_t {
                for (auto& bs : cur_agg->get_bases()) {
                    if (!bs.base || bs.is_virtual) continue;
                    std::string fname = "__base_" + bs.sanitised_name() + "__";
                    auto field = cur_st_type->get_member(fname);
                    if (!field) continue;
                    auto* cur_llvm = llvm::dyn_cast_or_null<llvm::StructType>(cur_st_type->get_llvm_type());
                    if (!cur_llvm) continue;
                    uint64_t this_off = dl.getStructLayout(cur_llvm)->getElementOffset((unsigned)field->index);
                    if (bs.base.get() == src_st.get()) return (int64_t)this_off;
                    auto base_st_type = bs.base->get_struct_type();
                    if (!base_st_type) continue;
                    int64_t inner = dfs_offset(bs.base.get(), base_st_type.get());
                    if (inner >= 0) return (int64_t)this_off + inner;
                }
                return -1;
            };
            int64_t total = dfs_offset(tgt_st.get(), tgt_st_type.get());
            if (total >= 0) {
                llvm::Value* bi2 = _builder->CreatePtrToInt(base_raw, i64_ty);
                llvm::Value* di2 = _builder->CreateSub(bi2, llvm::ConstantInt::get(i64_ty, (uint64_t)total), "dyncast_trans_int");
                derived_ptr = _builder->CreateIntToPtr(di2, ptr_ty, "dyncast_trans_ptr");
            }
        }
    }
    if (!derived_ptr) derived_ptr = base_raw; // degenerate: same address

    // Step 3: Call the runtime dynamic_cast function with source RTTI, target RTTI, and object ptr
    // ── 7. Select result: derived_ptr on match, null on mismatch ─────────────
    auto* null_val = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptr_ty));
    llvm::Value* result = _builder->CreateSelect(rtti_match, derived_ptr, null_val, "dyncast_result");

    // Step 4: If null_is_fatal: emit a null check + fatal trap
    // ── 8. Fatal-null check for lnk/ref targets ───────────────────────────────
    if (expr.null_is_fatal()) {
        auto* fatal_fn = get_or_declare_fatal_null_function("__k_fatal_null_dyncast");
        // For link targets inside an if-condition, soft-fail to _null_failure_bb
        // instead of trapping.  Ref targets remain unconditionally fatal.
        auto target_type = expr.get_cast_type();
        llvm::BasicBlock* soft_bb = (_null_failure_bb && type::is_link(target_type))
                                     ? _null_failure_bb : nullptr;
        emit_null_check(result, fatal_fn, "dyncast", soft_bb);
    }

    // Step 5: Set _value to the cast result pointer
    _value = result;
}

//
// Fatal null helpers
//

llvm::Function* implementation_generator::get_or_declare_fatal_null_function(const std::string& name) {
    llvm::Module& mod = get_module();
    if (auto* existing = mod.getFunction(name)) {
        return existing;
    }
    auto& llvm_ctx = mod.getContext();
    auto* void_ty  = llvm::Type::getVoidTy(llvm_ctx);
    auto* fn_type  = llvm::FunctionType::get(void_ty, false);
    auto* fn = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage, name, mod);
    fn->addFnAttr(llvm::Attribute::NoReturn);
    fn->addFnAttr(llvm::Attribute::NoUnwind);
    fn->addFnAttr(llvm::Attribute::Cold);
    // Body is provided by libk — only emit the extern declaration.
    return fn;
}

void implementation_generator::emit_null_check(llvm::Value* ptr_value, llvm::Function* fatal_fn, const std::string& label,
                                               llvm::BasicBlock* soft_fail_bb) {
    auto* fn   = _builder->GetInsertBlock()->getParent();
    auto& ctx  = _builder->getContext();
    auto* ptr_ty = llvm::PointerType::get(ctx, 0);
    auto* ok_bb   = llvm::BasicBlock::Create(ctx, label + "_ok",   fn);
    auto* is_null = _builder->CreateICmpEQ(
        ptr_value, llvm::ConstantPointerNull::get(ptr_ty), label + "_is_null");
    if (soft_fail_bb) {
        // Soft-fail mode: branch to the provided block (e.g. if-else or if-continue)
        // instead of trapping. Used for link assignments in if-conditions.
        _builder->CreateCondBr(is_null, soft_fail_bb, ok_bb);
    } else {
        // Normal mode: fatal trap on null.
        auto* null_bb = llvm::BasicBlock::Create(ctx, label + "_null", fn);
        _builder->CreateCondBr(is_null, null_bb, ok_bb);
        _builder->SetInsertPoint(null_bb);
        _builder->CreateCall(fatal_fn, {});
        _builder->CreateUnreachable();
    }
    _builder->SetInsertPoint(ok_bb);
}

llvm::Function* implementation_generator::get_or_declare_fatal_memory_function() {
    llvm::Module& mod = get_module();
    const char* name = "__k_fatal_memory_allocation";
    if (auto* existing = mod.getFunction(name)) {
        return existing;
    }
    auto& llvm_ctx = mod.getContext();
    auto* void_ty = llvm::Type::getVoidTy(llvm_ctx);
    auto* fn_type = llvm::FunctionType::get(void_ty, false);
    auto* fn = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage, name, mod);
    fn->addFnAttr(llvm::Attribute::NoReturn);
    fn->addFnAttr(llvm::Attribute::Cold);
    // NOTE: do NOT add NoUnwind — this function throws a K OutOfMemory!
    return fn;
}

void implementation_generator::emit_alloc_null_check(llvm::Value* alloc_result, const std::string& label) {
    auto* fn = _builder->GetInsertBlock()->getParent();
    auto& ctx = _builder->getContext();
    auto* ptr_ty = llvm::PointerType::get(ctx, 0);

    auto* ok_bb   = llvm::BasicBlock::Create(ctx, label + "_ok", fn);
    auto* fail_bb = llvm::BasicBlock::Create(ctx, label + "_fail", fn);

    auto* is_null = _builder->CreateICmpEQ(
        alloc_result, llvm::ConstantPointerNull::get(ptr_ty), label + "_is_null");
    _builder->CreateCondBr(is_null, fail_bb, ok_bb);

    // Fail block: call/invoke __k_fatal_memory_allocation (throws OutOfMemory)
    _builder->SetInsertPoint(fail_bb);
    auto* fatal_fn = get_or_declare_fatal_memory_function();
    if (!_landing_pad_stack.empty()) {
        // Inside try-catch: use invoke so the exception unwinds to the landing pad
        auto* unreachable_bb = llvm::BasicBlock::Create(ctx, label + "_unreachable", fn);
        _builder->CreateInvoke(
            fatal_fn->getFunctionType(), fatal_fn,
            unreachable_bb, _landing_pad_stack.top().lpad_bb, {});
        _builder->SetInsertPoint(unreachable_bb);
        _builder->CreateUnreachable();
    } else {
        // Not inside try-catch: plain call (exception propagates past this frame)
        auto* call = _builder->CreateCall(fatal_fn, {});
        call->setDoesNotReturn();
        _builder->CreateUnreachable();
    }

    // Continue in the success block
    _builder->SetInsertPoint(ok_bb);
}


} // namespace k::model::gen
