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

#include "../model/model.hpp"

#include <map>
#include <memory>

namespace k::model::gen {

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
        // Call destructor if struct (single object)
        auto st = st_type->get_struct();
        auto dtor = st ? st->get_destructor() : nullptr;
        if (dtor) {
            auto dtor_it = functions.find(dtor->shared_as<function>());
            if (dtor_it != functions.end()) {
                builder->CreateCall(dtor_it->second, {ptr_value});
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

/**
 * Get or create the __k_array_bounds_check_failed(index, size) function
 * in the given module. The function body is emitted directly in LLVM IR so
 * the generated code is self-contained (works for both JIT and AOT).
 *
 * The body calls fprintf(stderr, ...) then abort().
 */
inline llvm::Function* get_or_create_bounds_check_failed_fn(
    llvm::Module& mod)
{
    if (auto* existing = mod.getFunction("__k_array_bounds_check_failed"))
        return existing;

    auto& ctx = mod.getContext();
    auto* i32_ty  = llvm::Type::getInt32Ty(ctx);
    auto* void_ty = llvm::Type::getVoidTy(ctx);
    auto* ptr_ty  = llvm::PointerType::get(ctx, 0);

    // Declare fprintf(ptr, ptr, ...) -> i32
    llvm::Function* fprintf_fn = mod.getFunction("fprintf");
    if (!fprintf_fn) {
        auto* fprintf_ty = llvm::FunctionType::get(i32_ty, {ptr_ty, ptr_ty}, /*isVarArg=*/true);
        fprintf_fn = llvm::Function::Create(fprintf_ty, llvm::Function::ExternalLinkage, "fprintf", mod);
    }

    // Declare abort() -> void
    llvm::Function* abort_fn = mod.getFunction("abort");
    if (!abort_fn) {
        auto* abort_ty = llvm::FunctionType::get(void_ty, false);
        abort_fn = llvm::Function::Create(abort_ty, llvm::Function::ExternalLinkage, "abort", mod);
        abort_fn->addFnAttr(llvm::Attribute::NoReturn);
        abort_fn->addFnAttr(llvm::Attribute::NoUnwind);
    }

    // Declare stderr as an external global (FILE* — opaque pointer)
    llvm::GlobalVariable* stderr_var = mod.getGlobalVariable("stderr");
    if (!stderr_var) {
        stderr_var = new llvm::GlobalVariable(
            mod, ptr_ty, false, llvm::GlobalValue::ExternalLinkage, nullptr, "stderr");
    }

    // Define __k_array_bounds_check_failed(i32 index, i32 size) -> void
    auto* fn_ty = llvm::FunctionType::get(void_ty, {i32_ty, i32_ty}, false);
    auto* fn = llvm::Function::Create(fn_ty, llvm::Function::InternalLinkage,
                                       "__k_array_bounds_check_failed", mod);
    fn->addFnAttr(llvm::Attribute::NoReturn);
    fn->addFnAttr(llvm::Attribute::NoUnwind);

    auto* entry_bb = llvm::BasicBlock::Create(ctx, "entry", fn);
    llvm::IRBuilder<> b(entry_bb);

    // Create the format string as a global constant
    auto* fmt_str = b.CreateGlobalStringPtr(
        "runtime error: array index out of bounds (index=%u, size=%u)\n",
        "__k_bounds_fmt");

    // Load stderr
    llvm::Value* stderr_val = b.CreateLoad(ptr_ty, stderr_var, "stderr_val");

    // fprintf(stderr, fmt, index, size)
    auto* idx_arg = fn->getArg(0);
    auto* sz_arg  = fn->getArg(1);
    b.CreateCall(fprintf_fn, {stderr_val, fmt_str, idx_arg, sz_arg});

    // abort()
    b.CreateCall(abort_fn);
    b.CreateUnreachable();

    return fn;
}

/**
 * Emit a runtime bounds check: if (index >= count) __k_array_bounds_check_failed(index, count).
 *
 * @param builder     The IRBuilder positioned at the insertion point.
 * @param mod         The LLVM module.
 * @param index_val   The i32 index value to check.
 * @param count_val   The i32 element count (loaded from the array header).
 * @param label_prefix  Label prefix for generated basic blocks.
 */
inline void emit_array_bounds_check(
    llvm::IRBuilder<>* builder,
    llvm::Module& mod,
    llvm::Value* index_val,
    llvm::Value* count_val,
    const std::string& label_prefix = "bounds")
{
    llvm::Function* fail_fn = get_or_create_bounds_check_failed_fn(mod);

    // if (index >= count) { __k_array_bounds_check_failed(index, count); unreachable; }
    auto& llvm_ctx = builder->getContext();
    auto* fn = builder->GetInsertBlock()->getParent();
    auto* oob_bb  = llvm::BasicBlock::Create(llvm_ctx, label_prefix + "_oob",    fn);
    auto* ok_bb   = llvm::BasicBlock::Create(llvm_ctx, label_prefix + "_ok",     fn);
    auto* cmp = builder->CreateICmpUGE(index_val, count_val, label_prefix + "_cmp");
    builder->CreateCondBr(cmp, oob_bb, ok_bb);

    // Out-of-bounds branch: call fatal + unreachable
    builder->SetInsertPoint(oob_bb);
    builder->CreateCall(fail_fn, {index_val, count_val});
    builder->CreateUnreachable();

    // In-bounds branch: continue here
    builder->SetInsertPoint(ok_bb);
}

} // namespace k::model::gen

#endif // KLANG_GEN_HELPERS_HPP

