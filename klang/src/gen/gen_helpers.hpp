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

#ifndef KLANG_GEN_HELPERS_HPP
#define KLANG_GEN_HELPERS_HPP

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include "llvm/Support/raw_os_ostream.h"

#include "../model/model.hpp"
#include "../model/context.hpp"
#include "../model/imported.hpp"
#include "../parse/ast.hpp"
#include "../../../libkdi/src/kdi_aggregates.hpp"

#include <map>
#include <memory>

namespace k::model::gen {

/**
 * Return true if the given aggregate is ::k::Object (the root base class).
 * The root namespace is named after the module (e.g. "k" for module k),
 * so Object's parent namespace IS the root when compiling module k.
 *
 * Shared between gen_unit.cpp (implicit-base injection) and gen_class.cpp
 * (universal destructor vtable slot seeding).
 */
inline bool is_k_object(const aggregate& agg) {
    if (agg.get_short_name() != "Object") return false;
    auto parent_ns = agg.parent<ns>();
    if (!parent_ns) return false;
    if (parent_ns->get_short_name() != "k") return false;
    return true;
}

/**
 * Apply the linkage policy for a **template instantiation** symbol.
 *
 * Template instantiations follow the C++ ODR model: every translation unit /
 * module that uses an instantiation emits its own definition, marked
 * `linkonce_odr` and placed in a COMDAT group keyed by its mangled name. The
 * static linker then keeps a single copy (and the dynamic linker interposes a
 * single copy across shared libraries, thanks to default visibility), instead of
 * emitting strong `external` symbols that would collide at link time.
 *
 * Applies to instantiation methods, constructors, destructors, and the
 * associated vtable / RTTI / static globals (any llvm::GlobalObject).
 *
 * @param mod           The LLVM module owning the symbol.
 * @param gv            The global object (function or global variable) to mark.
 * @param mangled_name  The instantiation symbol's mangled name (the COMDAT key).
 */
inline void apply_instantiation_linkage(llvm::Module& mod,
                                        llvm::GlobalObject* gv,
                                        const std::string& mangled_name)
{
    if (!gv || mangled_name.empty()) return;
    gv->setLinkage(llvm::GlobalValue::LinkOnceODRLinkage);
    // Default visibility is intentional: it lets the dynamic linker interpose a
    // single copy across shared libraries (required for cross-module type
    // identity / RTTI). Do NOT set hidden visibility here.
    llvm::Comdat* comdat = mod.getOrInsertComdat(mangled_name);
    comdat->setSelectionKind(llvm::Comdat::Any);
    gv->setComdat(comdat);
}

/**
 * True when an aggregate's externally-visible code-gen artifacts (vtable, RTTI)
 * should be merged across translation units / modules via linkonce_odr + COMDAT.
 *
 * Covers both template-related forms that every consumer re-synthesises:
 *   - concrete instantiations (e.g. ::k::Optional<int>), is_instantiation();
 *   - any template definition / type-erased generic template (is_template()):
 *     these are re-emitted under a deterministic, identical name in every module
 *     that defines or imports them, so without COMDAT they would collide as
 *     duplicate strong symbols in a static link.
 */
inline bool should_merge_aggregate_symbols(const k::model::aggregate& agg)
{
    return agg.is_instantiation() || agg.is_template();
}

/**
 * Emit a virtual (vtable-dispatched) call to the destructor of the object
 * pointed to by `this_ptr`, whose static type is `st`.
 *
 * The destructor always occupies the universal vtable slot 0 (see
 * build_vtable_layout() in gen_class.cpp): ::k::Object declares the first-ever
 * virtual destructor at slot 0, and every derived class/interface that reaches
 * Object through its *primary* vtable base chain inherits and overrides that
 * same slot — never introducing a new one. This makes the destructor
 * reachable at a fixed vtable offset from any class/interface-typed reference.
 *
 * Handles both:
 *  - locally-declared aggregates (klass/interface compiled in this unit), whose
 *    vptr always lives at struct field 0;
 *  - KDI-imported aggregates (imported_klass/imported_interface), whose vptr
 *    field index is read from the imported KDI layout metadata (usually also
 *    field 0, but looked up generically for correctness).
 *
 * The vtable memory layout is `{ RTTI ptr, slot0 fn ptr, slot1 fn ptr, … }`,
 * so the destructor (logical slot 0) sits at byte offset `ptr_size` (one
 * pointer past the RTTI slot).
 *
 * No-op if `st` has no vtable (non-virtual / struct types never reach here —
 * callers are expected to only invoke this when st.has_vtable() is true, but
 * it's safe to call unconditionally).
 *
 * @param builder   The IRBuilder positioned at the insertion point.
 * @param st        The static K model type of the object being destroyed.
 * @param this_ptr  The (non-null) pointer to the object.
 */
inline void emit_virtual_destructor_call(
    llvm::IRBuilder<>* builder,
    aggregate& st,
    llvm::Value* this_ptr)
{
    if (!st.has_vtable()) return;

    llvm::LLVMContext& llvm_ctx = builder->getContext();
    llvm::Type* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);

    auto struct_type = st.get_struct_type();
    auto* struct_llvm_type = struct_type ? struct_type->get_llvm_type() : nullptr;
    if (!struct_llvm_type) return;

    // Determine the vptr field index. Locally-declared aggregates always put
    // the (primary) vptr at field 0. Imported aggregates carry the actual
    // index in their KDI layout metadata (still field 0 in every case seen so
    // far, but resolved properly rather than assumed).
    uint32_t vptr_field_index = 0;
    if (auto* imp_agg = dynamic_cast<imported_aggregate*>(&st)) {
        if (const auto* kdi_agg = imp_agg->get_kdi_aggregate()) {
            for (const auto& lf : kdi_agg->layout) {
                if (auto* vp = std::get_if<kdi::kdi_layout_vptr>(&lf)) {
                    vptr_field_index = vp->llvm_field_index;
                    break;
                }
            }
        }
    }

    llvm::Value* vptr_addr = builder->CreateStructGEP(
        struct_llvm_type, this_ptr, vptr_field_index, "dtor_vptr_addr");
    llvm::Value* vptr = builder->CreateLoad(ptr_ty, vptr_addr, "dtor_vptr");

    // Vtable layout is { RTTI ptr, slot0 (dtor) fn ptr, slot1 fn ptr, … }:
    // the destructor is always at byte offset `ptr_size` (one pointer past RTTI).
    const uint64_t ptr_size = 8;
    llvm::Value* slot_offset = llvm::ConstantInt::get(
        llvm::Type::getInt64Ty(llvm_ctx), ptr_size);
    llvm::Value* fn_ptr_addr = builder->CreateInBoundsGEP(
        llvm::Type::getInt8Ty(llvm_ctx), vptr, slot_offset, "dtor_slot_addr");
    llvm::Value* fn_ptr = builder->CreateLoad(ptr_ty, fn_ptr_addr, "dtor_fn_ptr");

    auto* fn_type = llvm::FunctionType::get(llvm::Type::getVoidTy(llvm_ctx), {ptr_ty}, false);
    builder->CreateCall(fn_type, fn_ptr, {this_ptr});
}

/**
 * Emit the destroy+free sequence for an owner pointer value.
 *
 * For array types (owner<T[N]>), calls destructors on each element in
 * reverse order, then frees the whole allocation.
 * For struct types, calls the destructor, then frees.
 * For primitive types, just frees.
 *
 * Precondition: ptr_value is non-null (the caller must emit a null check).
 *
 * @param builder    The IRBuilder positioned at the insertion point.
 * @param mod        The LLVM module (used to look up / declare free()).
 * @param functions  Map from K model functions to LLVM functions (for dtor lookup).
 * @param ptr_value  The raw pointer (LLVM opaque ptr) to the allocated object.
 * @param alloc_type The K model type of the pointed-to object (may be a sized_array_type).
 */
inline void emit_owner_object_destroy(
    llvm::IRBuilder<>* builder,
    llvm::Module& mod,
    const std::map<std::shared_ptr<function>, llvm::Function*>& functions,
    llvm::Value* ptr_value,
    const std::shared_ptr<type>& alloc_type)
{
    auto& llvm_ctx = builder->getContext();
    auto* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);

    // Handle array types: call destructors on each element in reverse order
    if (auto sized_arr = std::dynamic_pointer_cast<sized_array_type>(alloc_type)) {
        // ── Sized (static) array: unrolled destructor loop ──
        auto elem_type = sized_arr->get_subtype();
        if (auto st_type = std::dynamic_pointer_cast<struct_type>(elem_type)) {
            auto st = st_type->get_struct();
            auto dtor = st ? st->get_destructor() : nullptr;
            if (dtor) {
                auto dtor_it = functions.find(dtor->shared_as<function>());
                if (dtor_it != functions.end()) {
                    auto* struct_llvm = sized_arr->get_llvm_struct_type();
                    auto* llvm_arr_type = sized_arr->get_llvm_data_array_type();
                    llvm::Value* data_ptr = builder->CreateStructGEP(struct_llvm, ptr_value,
                        sized_array_type::FIELD_DATA, "arr_dtor_data");
                    size_t arr_size = sized_arr->get_size();
                    // Call destructors in REVERSE order
                    for (size_t ri = arr_size; ri > 0; --ri) {
                        size_t i = ri - 1;
                        llvm::Value* elem_ptr = builder->CreateConstInBoundsGEP2_32(
                            llvm_arr_type, data_ptr, 0, i, "arr_dtor_elem_" + std::to_string(i));
                        builder->CreateCall(dtor_it->second, {elem_ptr});
                    }
                }
            }
        }
    } else if (auto unsized_arr = std::dynamic_pointer_cast<array_type>(alloc_type)) {
        // ── Unsized (dynamic) array: IR loop for destructors ──
        auto elem_type = unsized_arr->get_subtype();
        if (auto st_type = std::dynamic_pointer_cast<struct_type>(elem_type)) {
            auto st = st_type->get_struct();
            auto dtor = st ? st->get_destructor() : nullptr;
            if (dtor) {
                auto dtor_it = functions.find(dtor->shared_as<function>());
                if (dtor_it != functions.end()) {
                    auto* struct_llvm = unsized_arr->get_llvm_struct_type();
                    auto* llvm_arr_type = unsized_arr->get_llvm_data_array_type();
                    auto* i32_ty = llvm::Type::getInt32Ty(llvm_ctx);

                    // Load count from field 0
                    llvm::Value* count_ptr = builder->CreateStructGEP(struct_llvm, ptr_value,
                        array_type::FIELD_SIZE, "dynarr_dtor_count_ptr");
                    llvm::Value* count_val = builder->CreateLoad(i32_ty, count_ptr, "dynarr_dtor_count");

                    llvm::Value* data_ptr = builder->CreateStructGEP(struct_llvm, ptr_value,
                        array_type::FIELD_DATA, "dynarr_dtor_data");

                    // Emit reverse-order IR loop: for (i = count; i > 0; --i) dtor(&data[i-1])
                    auto* fn = builder->GetInsertBlock()->getParent();
                    auto* loop_header = llvm::BasicBlock::Create(llvm_ctx, "dynarr_dtor_hdr", fn);
                    auto* loop_body   = llvm::BasicBlock::Create(llvm_ctx, "dynarr_dtor_body", fn);
                    auto* loop_end    = llvm::BasicBlock::Create(llvm_ctx, "dynarr_dtor_end", fn);

                    auto* pre_bb = builder->GetInsertBlock();
                    builder->CreateBr(loop_header);

                    // Header: %ri = phi [count, pre], [%ri_next, body]; if ri > 0 goto body else end
                    builder->SetInsertPoint(loop_header);
                    llvm::PHINode* ri_phi = builder->CreatePHI(i32_ty, 2, "dynarr_dtor_ri");
                    ri_phi->addIncoming(count_val, pre_bb);
                    llvm::Value* cmp = builder->CreateICmpUGT(ri_phi,
                        llvm::ConstantInt::get(i32_ty, 0), "dynarr_dtor_cmp");
                    builder->CreateCondBr(cmp, loop_body, loop_end);

                    // Body: index = ri - 1; dtor(&data[index]); ri_next = index
                    builder->SetInsertPoint(loop_body);
                    llvm::Value* idx = builder->CreateSub(ri_phi,
                        llvm::ConstantInt::get(i32_ty, 1), "dynarr_dtor_idx");
                    llvm::Value* indices[] = {llvm::ConstantInt::get(i32_ty, 0), idx};
                    llvm::Value* elem_ptr = builder->CreateGEP(
                        llvm_arr_type, data_ptr, indices, "dynarr_dtor_elem");
                    builder->CreateCall(dtor_it->second, {elem_ptr});
                    ri_phi->addIncoming(idx, loop_body);
                    builder->CreateBr(loop_header);

                    builder->SetInsertPoint(loop_end);
                }
            }
        }
    } else if (auto st_type = std::dynamic_pointer_cast<struct_type>(alloc_type)) {
        // Call destructor if struct (single object). If the static type has a
        // vtable (class/interface reaching ::k::Object), dispatch virtually so
        // the most-derived override runs even though the owner is statically
        // typed as a base (e.g. deleting/releasing through an interface owner).
        auto st = st_type->get_struct();
        if (st && st->has_vtable()) {
            emit_virtual_destructor_call(builder, *st, ptr_value);
        } else {
            auto dtor = st ? st->get_destructor() : nullptr;
            if (dtor) {
                auto dtor_it = functions.find(dtor->shared_as<function>());
                if (dtor_it != functions.end()) {
                    builder->CreateCall(dtor_it->second, {ptr_value});
                }
            }
        }
    }

    // Call free(ptr)
    llvm::Function* free_fn = mod.getFunction("free");
    if (!free_fn) {
        auto* free_type = llvm::FunctionType::get(
            llvm::Type::getVoidTy(llvm_ctx), {ptr_ty}, false);
        free_fn = llvm::Function::Create(
            free_type, llvm::Function::ExternalLinkage, "free", mod);
    }
    builder->CreateCall(free_fn, {ptr_value});
}

/**
 * Emit the full conditional owner cleanup sequence:
 *   if (owner_ptr != null) { destroy+free; owner_slot = null; }
 *
 * This eliminates the repeated null-check + emit_owner_object_destroy + null-out
 * pattern used at scope exit, return, assignment, and delete sites.
 *
 * @param builder       The IRBuilder positioned at the insertion point.
 * @param mod           The LLVM module.
 * @param functions     Map from K model functions to LLVM functions (for dtor lookup).
 * @param owner_alloca  The alloca (address of the owner variable slot).
 * @param alloc_type    The K model type of the pointed-to object.
 * @param label_prefix  A label prefix for the generated basic blocks (e.g. "owner_cleanup").
 * @param null_out      If true (default), writes null to the owner alloca after destroy.
 */
inline void emit_owner_cleanup_if_nonnull(
    llvm::IRBuilder<>* builder,
    llvm::Module& mod,
    const std::map<std::shared_ptr<function>, llvm::Function*>& functions,
    llvm::Value* owner_alloca,
    const std::shared_ptr<type>& alloc_type,
    const std::string& label_prefix = "owner_cleanup",
    bool null_out = true)
{
    auto& llvm_ctx = builder->getContext();
    auto* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);
    llvm::Value* cur_ptr = builder->CreateLoad(ptr_ty, owner_alloca, label_prefix + "_ptr");
    auto* fn = builder->GetInsertBlock()->getParent();
    auto* nonnull_bb = llvm::BasicBlock::Create(llvm_ctx, label_prefix + "_nonnull", fn);
    auto* done_bb    = llvm::BasicBlock::Create(llvm_ctx, label_prefix + "_done",    fn);
    auto* is_null = builder->CreateICmpEQ(
        cur_ptr, llvm::ConstantPointerNull::get(ptr_ty), label_prefix + "_null");
    builder->CreateCondBr(is_null, done_bb, nonnull_bb);
    builder->SetInsertPoint(nonnull_bb);
    emit_owner_object_destroy(builder, mod, functions, cur_ptr, alloc_type);
    if (null_out) {
        builder->CreateStore(llvm::ConstantPointerNull::get(ptr_ty), owner_alloca);
    }
    builder->CreateBr(done_bb);
    builder->SetInsertPoint(done_bb);
}

/**
 * Emit cleanup for all elements of a stack-allocated sized array.
 *
 * For struct elements with a destructor: calls the destructor on each element
 * in reverse order.
 * For owner elements: calls emit_owner_cleanup_if_nonnull on each element
 * in reverse order (conditional destroy+free, then null out the slot).
 *
 * @param builder     The IRBuilder positioned at the insertion point.
 * @param mod         The LLVM module.
 * @param functions   Map from K model functions to LLVM functions (for dtor lookup).
 * @param arr_alloca  The alloca for the sized array variable (points to { i32, [N x T] }).
 * @param arr_type    The K model sized_array_type.
 */
inline void emit_sized_array_elements_cleanup(
    llvm::IRBuilder<>* builder,
    llvm::Module& mod,
    const std::map<std::shared_ptr<function>, llvm::Function*>& functions,
    llvm::Value* arr_alloca,
    const std::shared_ptr<sized_array_type>& arr_type)
{
    auto elem_type = arr_type->get_subtype();
    auto* struct_llvm = arr_type->get_llvm_struct_type();
    auto* llvm_arr_type = arr_type->get_llvm_data_array_type();
    if (!struct_llvm || !llvm_arr_type) return;
    size_t arr_size = arr_type->get_size();

    llvm::Value* data_ptr = builder->CreateStructGEP(struct_llvm, arr_alloca,
        sized_array_type::FIELD_DATA, "arr_cleanup_data");

    if (auto st_type = std::dynamic_pointer_cast<struct_type>(elem_type)) {
        // Struct elements: call destructors in reverse order
        auto st = st_type->get_struct();
        auto dtor = st ? st->get_destructor() : nullptr;
        if (!dtor) return;
        auto dtor_it = functions.find(dtor->shared_as<function>());
        if (dtor_it == functions.end()) return;
        for (size_t ri = arr_size; ri > 0; --ri) {
            size_t i = ri - 1;
            llvm::Value* elem_ptr = builder->CreateConstInBoundsGEP2_32(
                llvm_arr_type, data_ptr, 0, i, "arr_dtor_" + std::to_string(i));
            builder->CreateCall(dtor_it->second, {elem_ptr});
        }
    } else if (auto own_elem = std::dynamic_pointer_cast<owner_type>(elem_type)) {
        // Owner elements: conditional destroy+free each in reverse order
        for (size_t ri = arr_size; ri > 0; --ri) {
            size_t i = ri - 1;
            llvm::Value* elem_ptr = builder->CreateConstInBoundsGEP2_32(
                llvm_arr_type, data_ptr, 0, i, "arr_own_" + std::to_string(i));
            emit_owner_cleanup_if_nonnull(builder, mod, functions,
                elem_ptr, own_elem->get_owned_type(),
                "arr_own_cleanup_" + std::to_string(i));
        }
    }
}

} // namespace k::model::gen

namespace k::model::gen {

// ── Forward declarations for helpers defined in specific gen_*.cpp files ────

/** Emit vptr store (defined in gen_class.cpp). */
void emit_vptr_store(llvm::IRBuilder<>& builder, klass& st, llvm::Value* this_ptr, std::shared_ptr<context> ctx);

/** Emit virtual dispatch call (defined in gen_class.cpp). */
llvm::Value* emit_virtual_dispatch_call(llvm::IRBuilder<>& builder, klass& st, llvm::Value* this_ptr,
    int slot_index, llvm::FunctionType* fn_type, const std::vector<llvm::Value*>& args,
    std::shared_ptr<context> ctx, const std::string& result_name);

/** Compute operator dispatch info (defined in gen_operators.cpp). */
virtual_dispatch_info compute_operator_dispatch_info(
    const std::shared_ptr<function>& func,
    const std::shared_ptr<type>& receiver_type);

// ── Shared static helpers ──────────────────────────────────────────────────

/**
 * Extract a concrete k::value_type from an AST expression node.
 * Only literal expressions are supported (compile-time constants).
 */
inline bool extract_value_from_ast_expr(
    const k::parse::ast::expression* expr,
    k::value_type& out_value)
{
    auto lit = dynamic_cast<const k::parse::ast::literal_expr*>(expr);
    if (!lit) return false;
    auto val = lit->literal.value().value();
    return std::visit([&out_value](auto&& v) -> bool {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::monostate> || std::is_same_v<T, std::nullptr_t>) {
            return false;
        } else {
            out_value = k::value_type{v};
            return true;
        }
    }, val);
}

/**
 * Ensure an LLVM global reference for an enum's object-backing table.
 */
inline llvm::GlobalVariable* ensure_enum_object_table_reference(
    const std::shared_ptr<enumeration>& en,
    const std::shared_ptr<context>& ctx)
{
    if (!en || !ctx || !en->is_object_backed()) return nullptr;
    if (auto* existing = en->get_table_global()) return existing;

    auto obj_type = en->get_object_type();
    if (!obj_type || !obj_type->is_resolved()) return nullptr;

    auto* llvm_st = llvm::dyn_cast_or_null<llvm::StructType>(ctx->get_llvm_type(obj_type));
    if (!llvm_st) return nullptr;

    const std::string table_name = !en->get_table_symbol().empty()
                                   ? en->get_table_symbol()
                                   : ("__klang_enum_table_" + en->get_mangled_name() + "__");
    if (auto* gv = ctx->module().getNamedGlobal(table_name)) {
        en->set_table_global(gv);
        return gv;
    }

    auto* arr_ty = llvm::ArrayType::get(llvm_st, en->entries().size());
    auto* gv = new llvm::GlobalVariable(
        ctx->module(),
        arr_ty,
        /*isConstant=*/true,
        llvm::GlobalValue::ExternalLinkage,
        nullptr,
        table_name);
    en->set_table_global(gv);
    return gv;
}

// ── LLVM type/value stream helpers ─────────────────────────────────────────

/**
 * Get or declare the __k_fatal_array_bounds_check_failed(index, size) function
 * in the given module.  The actual body is provided by libk (fatal.c),
 * so this only emits an extern declaration.
 */
inline llvm::Function* get_or_create_bounds_check_failed_fn(
    llvm::Module& mod)
{
    if (auto* existing = mod.getFunction("__k_fatal_array_bounds_check_failed"))
        return existing;

    auto& ctx = mod.getContext();
    auto* i32_ty  = llvm::Type::getInt32Ty(ctx);
    auto* void_ty = llvm::Type::getVoidTy(ctx);

    // Declare __k_fatal_array_bounds_check_failed(i32 index, i32 size) -> void
    auto* fn_ty = llvm::FunctionType::get(void_ty, {i32_ty, i32_ty}, false);
    auto* fn = llvm::Function::Create(fn_ty, llvm::Function::ExternalLinkage,
                                       "__k_fatal_array_bounds_check_failed", mod);
    fn->addFnAttr(llvm::Attribute::NoReturn);
    fn->addFnAttr(llvm::Attribute::Cold);
    // NOTE: do NOT add NoUnwind — this function throws IndexOutOfBoundsError!

    return fn;
}

/**
 * Emit a runtime bounds check: if (index >= count) __k_fatal_array_bounds_check_failed(index, count).
 *
 * @param builder     The IRBuilder positioned at the insertion point.
 * @param mod         The LLVM module.
 * @param index_val   The i32 index value to check.
 * @param count_val   The i32 element count (loaded from the array header).
 * @param label_prefix  Label prefix for generated basic blocks.
 * @param lpad_bb     Optional landing pad block for invoke (inside try-catch).
 */
inline void emit_array_bounds_check(
    llvm::IRBuilder<>* builder,
    llvm::Module& mod,
    llvm::Value* index_val,
    llvm::Value* count_val,
    const std::string& label_prefix = "bounds",
    llvm::BasicBlock* lpad_bb = nullptr)
{
    llvm::Function* fail_fn = get_or_create_bounds_check_failed_fn(mod);

    // if (index >= count) { __k_fatal_array_bounds_check_failed(index, count); unreachable; }
    auto& llvm_ctx = builder->getContext();
    auto* fn = builder->GetInsertBlock()->getParent();
    auto* oob_bb  = llvm::BasicBlock::Create(llvm_ctx, label_prefix + "_oob",    fn);
    auto* ok_bb   = llvm::BasicBlock::Create(llvm_ctx, label_prefix + "_ok",     fn);
    auto* cmp = builder->CreateICmpUGE(index_val, count_val, label_prefix + "_cmp");
    builder->CreateCondBr(cmp, oob_bb, ok_bb);

    // Out-of-bounds branch: call/invoke fatal + unreachable
    builder->SetInsertPoint(oob_bb);
    if (lpad_bb) {
        // Inside try-catch: use invoke so the exception unwinds to the landing pad
        auto* unreachable_bb = llvm::BasicBlock::Create(llvm_ctx, label_prefix + "_unreachable", fn);
        builder->CreateInvoke(
            fail_fn->getFunctionType(), fail_fn,
            unreachable_bb, lpad_bb, {index_val, count_val});
        builder->SetInsertPoint(unreachable_bb);
        builder->CreateUnreachable();
    } else {
        // Not inside try-catch: plain call (exception propagates past this frame)
        auto* call = builder->CreateCall(fail_fn, {index_val, count_val});
        call->setDoesNotReturn();
        builder->CreateUnreachable();
    }

    // In-bounds branch: continue here
    builder->SetInsertPoint(ok_bb);
}

/**
 * Find a union_type_def by its struct_type, searching recursively through all
 * namespaces starting from the given namespace.
 */
inline std::shared_ptr<union_type_def> find_union_by_struct_type(
    const std::shared_ptr<ns>& start_ns,
    const std::shared_ptr<struct_type>& st);

// Helper: search a union_holder (namespace or aggregate) and its children recursively
inline std::shared_ptr<union_type_def> find_union_by_struct_type_in_holder(
    const union_holder& holder,
    const std::shared_ptr<struct_type>& st)
{
    for (auto& [uname, udef] : holder.unions()) {
        if (!udef->get_struct_type()) continue;
        if (udef->get_struct_type() == st) return udef;
    }
    return nullptr;
}

inline std::shared_ptr<union_type_def> find_union_by_struct_type_in_aggregate(
    const std::shared_ptr<aggregate>& agg,
    const std::shared_ptr<struct_type>& st)
{
    if (!agg) return nullptr;
    // Check unions directly in this aggregate
    if (auto found = find_union_by_struct_type_in_holder(*agg, st)) return found;
    // Recurse into nested aggregates
    for (auto& child : agg->get_children()) {
        if (auto nested_agg = std::dynamic_pointer_cast<aggregate>(child)) {
            if (auto found = find_union_by_struct_type_in_aggregate(nested_agg, st)) return found;
        }
    }
    return nullptr;
}

inline std::shared_ptr<union_type_def> find_union_by_struct_type(
    const std::shared_ptr<ns>& start_ns,
    const std::shared_ptr<struct_type>& st)
{
    if (!start_ns || !st) return nullptr;
    // Check unions in this namespace
    if (auto found = find_union_by_struct_type_in_holder(*start_ns, st)) return found;
    // Recurse into child namespaces
    for (auto& child : start_ns->get_children()) {
        if (auto child_ns = std::dynamic_pointer_cast<ns>(child)) {
            if (auto found = find_union_by_struct_type(child_ns, st)) return found;
        }
        // Also search inside aggregates for nested unions
        if (auto child_agg = std::dynamic_pointer_cast<aggregate>(child)) {
            if (auto found = find_union_by_struct_type_in_aggregate(child_agg, st)) return found;
        }
    }
    return nullptr;
}

} // namespace k::model::gen

template<typename STM>
inline STM& operator << (STM& stm, const llvm::Type& type) {
    llvm::raw_os_ostream ross(stm);
    type.print(ross, true);
    return stm;
}

template<typename STM>
inline STM& operator << (STM& stm, const llvm::Value& value) {
    llvm::raw_os_ostream ross(stm);
    value.print(ross, true);
    return stm;
}

#endif // KLANG_GEN_HELPERS_HPP

