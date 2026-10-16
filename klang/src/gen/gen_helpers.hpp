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
#include <llvm/IR/DerivedTypes.h>
#include "llvm/Support/raw_os_ostream.h"

#include "../model/model.hpp"
#include "../model/context.hpp"
#include "../model/imported.hpp"
#include "../parse/ast.hpp"
#include "../../../libkdi/src/kdi_aggregates.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <unordered_set>
#include <vector>

namespace k::model::gen {

inline bool is_enum_backing_const_reference(const std::shared_ptr<enum_type>& source,
                                            const std::shared_ptr<type>& target) {
    if (!source || !source->is_object_backed()) return false;
    auto ref = std::dynamic_pointer_cast<reference_type>(type::remove_const(target));
    if (!ref || !type::is_const(ref->get_subtype())) return false;
    auto object = source->get_object_type();
    return object && type::remove_const(ref->get_subtype()) == object;
}

/** Remove resolved/const wrappers without erasing strong typedef identity. */
inline std::shared_ptr<type> unqualified_nominal_type(std::shared_ptr<type> value_type) {
    while (value_type) {
        auto next = type::remove_const(value_type);
        if (auto unresolved = std::dynamic_pointer_cast<unresolved_type>(next);
            unresolved && unresolved->is_resolved()) {
            next = unresolved->get_resolved();
        } else if (auto unresolved = std::dynamic_pointer_cast<unresolved_callable_type>(next);
                   unresolved && unresolved->is_resolved()) {
            next = unresolved->get_resolved();
        }
        if (next == value_type) break;
        value_type = next;
    }
    return value_type;
}

inline const union_alternative* find_union_alternative_for_argument(
    const union_type_def& definition, const std::shared_ptr<type>& argument_type) {
    auto argument = unqualified_nominal_type(argument_type);
    for (const auto* alternative : definition.all_alternatives_ptrs()) {
        if (!alternative || !alternative->resolved_type) continue;
        if (type::are_equal(unqualified_nominal_type(alternative->resolved_type), argument))
            return alternative;
    }
    if (type::is_reference(argument) || type::is_drain(argument)) {
        argument = unqualified_nominal_type(argument->get_subtype());
        for (const auto* alternative : definition.all_alternatives_ptrs()) {
            if (!alternative || !alternative->resolved_type) continue;
            if (type::are_equal(unqualified_nominal_type(alternative->resolved_type), argument))
                return alternative;
        }
    }
    return nullptr;
}

inline bool same_union_declaration(const std::shared_ptr<type>& left,
                                   const std::shared_ptr<type>& right) {
    // A typedef may convert to its underlying union, but layout equality never
    // establishes compatibility between distinct union declarations.
    auto left_union = std::dynamic_pointer_cast<struct_type>(
        type::remove_const(type::canonical(left)));
    auto right_union = std::dynamic_pointer_cast<struct_type>(
        type::remove_const(type::canonical(right)));
    return left_union && right_union && left_union->get_union()
        && left_union->get_union() == right_union->get_union();
}

inline bool callee_cleans_owner_parameter(const std::shared_ptr<type>& parameter_type) {
    if (type::is_owner(parameter_type)) return true;
    auto callable = std::dynamic_pointer_cast<callable_type>(parameter_type);
    return callable && callable->is_owner();
}

/** Resolve a named ABI field against the final layout, not LLVM type identity. */
inline std::optional<uint64_t> aggregate_field_offset(
    const aggregate& owner, const std::string& field_name,
    const llvm::DataLayout& layout, llvm::Type** field_type = nullptr) {
    auto st = owner.get_struct_type();
    auto* llvm_st = llvm::dyn_cast_or_null<llvm::StructType>(
        st ? st->get_llvm_type() : nullptr);
    auto field = st ? st->get_member(field_name) : std::nullopt;
    if (!field || !llvm_st || llvm_st->isOpaque() || !llvm_st->isSized()
        || field->index >= llvm_st->getNumElements()) return std::nullopt;
    if (field_type) *field_type = llvm_st->getElementType(field->index);
    return layout.getStructLayout(llvm_st)->getElementOffset(
        static_cast<unsigned>(field->index));
}

/** Field schema shared by local RTTI definitions and imported RTTI declarations. */
inline std::vector<llvm::Type*> aggregate_rtti_fields(llvm::LLVMContext& context) {
    auto* ptr_ty = llvm::PointerType::get(context, 0);
    auto* i32_ty = llvm::Type::getInt32Ty(context);
    return {
        ptr_ty, ptr_ty, ptr_ty, ptr_ty, // vptrs (fields 0-3)
        ptr_ty, ptr_ty,                 // name and fullName (fields 4-5)
        ptr_ty, ptr_ty, ptr_ty,         // bases, nested, enclosing (fields 6-8)
        i32_ty,                         // flags (field 9)
        ptr_ty,                         // annotations (field 10)
        ptr_ty,                         // cast map (field 11)
        ptr_ty,                         // functions (field 12)
        ptr_ty,                         // constructors (field 13)
        ptr_ty                          // exact semantic type (field 14)
    };
}

inline llvm::StructType* aggregate_rtti_value_type(llvm::LLVMContext& context) {
    return llvm::StructType::get(context, aggregate_rtti_fields(context), false);
}

inline std::vector<llvm::Type*> annotation_rtti_fields(llvm::LLVMContext& context) {
    auto* ptr_ty = llvm::PointerType::get(context, 0);
    auto* i32_ty = llvm::Type::getInt32Ty(context);
    return {
        ptr_ty, ptr_ty, ptr_ty, ptr_ty, // vptrs (fields 0-3)
        ptr_ty, ptr_ty,                 // name and fullName (fields 4-5)
        ptr_ty, ptr_ty, ptr_ty,         // bases, nested, enclosing (fields 6-8)
        i32_ty,                         // flags (field 9)
        ptr_ty,                         // annotations (field 10)
        ptr_ty,                         // cast map (field 11)
        ptr_ty                          // functions (field 12)
    };
}

inline llvm::StructType* annotation_rtti_value_type(llvm::LLVMContext& context) {
    return llvm::StructType::get(context, annotation_rtti_fields(context), false);
}

inline bool is_annotation_aggregate(const std::shared_ptr<aggregate>& agg) {
    return std::dynamic_pointer_cast<annotation_type>(agg) != nullptr
        || std::dynamic_pointer_cast<imported_annotation_type>(agg) != nullptr;
}

llvm::Constant* materialize_annotation_array(
    annotation_holder& holder,
    const std::shared_ptr<context>& ctx,
    unit& owner,
    const std::string& symbol_prefix,
    aggregate* parent_aggregate = nullptr);

/**
 * Emitter for calls that must follow the active cleanup/catch context.
 * Code generated inside a function supplies create_call_or_invoke.
 */
using virtual_call_emitter = std::function<llvm::Value*(llvm::FunctionType*, llvm::Value*,
    const std::vector<llvm::Value*>&, const std::string&)>;

inline void emit_value_cleanup(
    llvm::IRBuilder<>* builder, llvm::Module& mod,
    const std::map<std::shared_ptr<function>, llvm::Function*>& functions,
    llvm::Value* storage, const std::shared_ptr<type>& value_type,
    const virtual_call_emitter& emit_call = {});

inline void emit_array_elements_cleanup(
    llvm::IRBuilder<>* builder, llvm::Module& mod,
    const std::map<std::shared_ptr<function>, llvm::Function*>& functions,
    llvm::Value* storage, const std::shared_ptr<array_type>& array,
    const virtual_call_emitter& emit_call);

inline llvm::Function* get_or_create_owned_container_cleanup_function(
    llvm::Module& mod,
    const std::map<std::shared_ptr<function>, llvm::Function*>& functions,
    const std::shared_ptr<type>& value_type);

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

inline bool same_conversion_type(const std::shared_ptr<type>& lhs,
                                const std::shared_ptr<type>& rhs) {
    if (type::are_equal(lhs, rhs)) return true;
    auto left_struct = std::dynamic_pointer_cast<struct_type>(type::remove_const(lhs));
    auto right_struct = std::dynamic_pointer_cast<struct_type>(type::remove_const(rhs));
    if (!left_struct || !right_struct) return false;
    auto left_aggregate = left_struct->get_struct();
    auto right_aggregate = right_struct->get_struct();
    return left_aggregate && right_aggregate
        && !left_aggregate->get_fq_name().empty()
        && left_aggregate->get_fq_name() == right_aggregate->get_fq_name();
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
inline llvm::Value* emit_virtual_destructor_call(
    llvm::IRBuilder<>* builder,
    aggregate& st,
    llvm::Value* this_ptr,
    const virtual_call_emitter& emit_call = {})
{
    if (!st.has_vtable()) return this_ptr;

    llvm::LLVMContext& llvm_ctx = builder->getContext();
    llvm::Type* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);

    auto struct_type = st.get_struct_type();
    auto* struct_llvm_type = struct_type ? struct_type->get_llvm_type() : nullptr;
    if (!struct_llvm_type) return this_ptr;

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

    auto* fn_type = llvm::FunctionType::get(ptr_ty, {ptr_ty}, false);
    if (emit_call) return emit_call(fn_type, fn_ptr, {this_ptr}, "");
    return builder->CreateCall(fn_type, fn_ptr, {this_ptr});
}

inline llvm::Function* get_or_create_destroy_and_free_function(
    llvm::Module& mod, llvm::Function* destructor_fn)
{
    const std::string name = destructor_fn->getName().str() + "_destroy_and_free";
    if (auto* existing = mod.getFunction(name)) return existing;
    auto& ctx = mod.getContext();
    auto* ptr_ty = llvm::PointerType::get(ctx, 0);
    auto* fn_ty = llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), {ptr_ty}, false);
    auto* fn = llvm::Function::Create(fn_ty, llvm::Function::InternalLinkage, name, mod);
    auto personality = mod.getOrInsertFunction("__gxx_personality_v0",
        llvm::FunctionType::get(llvm::Type::getInt32Ty(ctx), true));
    fn->setPersonalityFn(llvm::cast<llvm::Constant>(personality.getCallee()));
    auto free_fn = mod.getOrInsertFunction("free", fn_ty);
    auto* entry = llvm::BasicBlock::Create(ctx, "entry", fn);
    auto* normal = llvm::BasicBlock::Create(ctx, "destroyed", fn);
    auto* unwind = llvm::BasicBlock::Create(ctx, "destroy_unwind", fn);
    llvm::IRBuilder<> builder(entry);
    auto* ptr = fn->getArg(0);
    builder.CreateInvoke(destructor_fn, normal, unwind, {ptr});
    builder.SetInsertPoint(normal);
    builder.CreateCall(free_fn, {ptr});
    builder.CreateRetVoid();
    builder.SetInsertPoint(unwind);
    auto* exception = builder.CreateLandingPad(
        llvm::StructType::get(ctx, {ptr_ty, llvm::Type::getInt32Ty(ctx)}), 0);
    exception->setCleanup(true);
    builder.CreateCall(free_fn, {ptr});
    builder.CreateResume(exception);
    return fn;
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
    const std::shared_ptr<type>& alloc_type,
    const virtual_call_emitter& emit_call = {})
{
    auto& llvm_ctx = builder->getContext();
    auto* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);
    llvm::Value* complete_ptr = ptr_value;
    // Const qualifies access, not destruction; aliases may also hide it.
    auto object_type = type::remove_const(type::canonical(alloc_type));

    if (type::is_array(object_type)
        || (type::is_struct(object_type) && std::dynamic_pointer_cast<struct_type>(object_type)->get_union())) {
        auto* cleanup = get_or_create_owned_container_cleanup_function(mod, functions, object_type);
        if (emit_call) emit_call(cleanup->getFunctionType(), cleanup, {ptr_value}, "");
        else builder->CreateCall(cleanup, {ptr_value});
        return;
    }

    if (auto st_type = std::dynamic_pointer_cast<struct_type>(object_type)) {
        // Call destructor if struct (single object). If the static type has a
        // vtable (class/interface reaching ::k::Object), dispatch virtually so
        // the most-derived override runs even though the owner is statically
        // typed as a base (e.g. deleting/releasing through an interface owner).
        auto st = st_type->get_struct();
        if (st && st->has_vtable()) {
            complete_ptr = emit_virtual_destructor_call(builder, *st, ptr_value, emit_call);
        } else {
            auto dtor = st ? st->get_destructor() : nullptr;
            if (dtor) {
                auto dtor_it = functions.find(dtor->shared_as<function>());
                if (dtor_it != functions.end()) {
                    // A non-virtual struct's receiver is the allocation address.
                    // Release it on both exits, before propagating a destructor failure.
                    auto* destroy_fn = get_or_create_destroy_and_free_function(mod, dtor_it->second);
                    if (emit_call) emit_call(destroy_fn->getFunctionType(), destroy_fn, {ptr_value}, "");
                    else builder->CreateCall(destroy_fn, {ptr_value});
                    return;
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
    builder->CreateCall(free_fn, {complete_ptr});
}

/**
 * Emit the full conditional owner cleanup sequence:
 *   if (owner_ptr != null) { owner_slot = null; destroy+free; }
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
 */
inline void emit_owner_cleanup_if_nonnull(
    llvm::IRBuilder<>* builder,
    llvm::Module& mod,
    const std::map<std::shared_ptr<function>, llvm::Function*>& functions,
    llvm::Value* owner_alloca,
    const std::shared_ptr<type>& alloc_type,
    const std::string& label_prefix = "owner_cleanup",
    const virtual_call_emitter& emit_call = {})
{
    auto& llvm_ctx = builder->getContext();
    auto* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);
    auto* cur_ptr = builder->CreateLoad(ptr_ty, owner_alloca, label_prefix + "_ptr");
    auto* fn = builder->GetInsertBlock()->getParent();
    auto* nonnull_bb = llvm::BasicBlock::Create(llvm_ctx, label_prefix + "_nonnull", fn);
    auto* done_bb    = llvm::BasicBlock::Create(llvm_ctx, label_prefix + "_done",    fn);
    auto* is_null = builder->CreateICmpEQ(
        cur_ptr, llvm::ConstantPointerNull::get(ptr_ty), label_prefix + "_null");
    builder->CreateCondBr(is_null, done_bb, nonnull_bb);
    builder->SetInsertPoint(nonnull_bb);
    builder->CreateStore(llvm::ConstantPointerNull::get(ptr_ty), owner_alloca);
    emit_owner_object_destroy(builder, mod, functions, cur_ptr, alloc_type, emit_call);
    builder->CreateBr(done_bb);
    builder->SetInsertPoint(done_bb);
}

/**
 * Emit the full conditional cleanup sequence for an owned callable (`!(...)`):
 *   ctx = callable_slot.ctx; callable_slot = { null, null };
 *   if (ctx != null) { drop(ctx); }
 *
 * An owned callable fat pointer has type `%__k.callable = { ptr fn, ptr ctx }`.
 * When ctx is non-null, ctx points to a heap closure whose first field is
 * `void (*drop)(void* ctx)`. Capture destruction can throw.
 *
 * @param builder              The IRBuilder positioned at the insertion point.
 * @param mod                  The LLVM module.
 * @param callable_alloca      The alloca holding the %__k.callable value.
 * @param callable_llvm_type   The %__k.callable llvm::StructType*.
 * @param label_prefix         A label prefix for the generated basic blocks.
 */
inline void emit_owned_callable_cleanup_if_nonnull(
    llvm::IRBuilder<>* builder,
    llvm::Module& mod,
    llvm::Value* callable_alloca,
    llvm::StructType* callable_llvm_type,
    const std::string& label_prefix = "owned_callable_cleanup",
    const virtual_call_emitter& emit_call = {})
{
    auto& llvm_ctx = builder->getContext();
    auto* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);

    auto* ctx_addr = builder->CreateStructGEP(
        callable_llvm_type, callable_alloca, 1, label_prefix + "_ctx_addr");
    llvm::Value* ctx_ptr = builder->CreateLoad(ptr_ty, ctx_addr, label_prefix + "_ctx");
    builder->CreateStore(llvm::ConstantAggregateZero::get(callable_llvm_type), callable_alloca);

    auto* fn = builder->GetInsertBlock()->getParent();
    auto* nonnull_bb = llvm::BasicBlock::Create(llvm_ctx, label_prefix + "_nonnull", fn);
    auto* done_bb    = llvm::BasicBlock::Create(llvm_ctx, label_prefix + "_done",    fn);

    auto* is_null = builder->CreateICmpEQ(
        ctx_ptr, llvm::ConstantPointerNull::get(ptr_ty), label_prefix + "_null");
    builder->CreateCondBr(is_null, done_bb, nonnull_bb);

    builder->SetInsertPoint(nonnull_bb);
    // ctx_ptr holds __k_owned_callable_header at offset 0: void (*drop)(void* ctx)
    llvm::Value* drop_fn_ptr = builder->CreateLoad(ptr_ty, ctx_ptr, label_prefix + "_drop_fn");
    auto* drop_fn_type = llvm::FunctionType::get(llvm::Type::getVoidTy(llvm_ctx), {ptr_ty}, false);
    if (emit_call) emit_call(drop_fn_type, drop_fn_ptr, {ctx_ptr}, "");
    else builder->CreateCall(drop_fn_type, drop_fn_ptr, {ctx_ptr});
    builder->CreateBr(done_bb);

    builder->SetInsertPoint(done_bb);
}

/**
 * Emit cleanup for all elements of a stack-allocated sized array.
 *
 * For struct elements with a destructor: calls the destructor on each element
 * in reverse order.
 * For owner elements: calls emit_owner_cleanup_if_nonnull on each element
 * in reverse order (clear the slot, then conditional destroy+free).
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
    const std::shared_ptr<sized_array_type>& arr_type,
    const virtual_call_emitter& emit_call = {})
{
    emit_array_elements_cleanup(builder, mod, functions, arr_alloca, arr_type, emit_call);
}

inline bool value_cleanup_has_pending_children(const std::shared_ptr<type>& value_type) {
    auto bare = type::remove_const(type::canonical(value_type));
    if (type::is_sized_array(bare)) return true;
    auto st = std::dynamic_pointer_cast<struct_type>(bare);
    return st && st->get_union();
}

inline void emit_array_elements_cleanup(
    llvm::IRBuilder<>* builder, llvm::Module& mod,
    const std::map<std::shared_ptr<function>, llvm::Function*>& functions,
    llvm::Value* storage, const std::shared_ptr<array_type>& array,
    const virtual_call_emitter& emit_call)
{
    auto element = type::remove_const(type::canonical(array->get_subtype()));
    if (!value_type_needs_cleanup(element)) return;
    auto* layout = array->get_llvm_struct_type();
    auto* data_type = array->get_llvm_data_array_type();
    auto* count_ptr = builder->CreateStructGEP(layout, storage, array_type::FIELD_SIZE);
    auto* data = builder->CreateStructGEP(layout, storage, array_type::FIELD_DATA);
    auto* fn = builder->GetInsertBlock()->getParent();
    auto* test = llvm::BasicBlock::Create(builder->getContext(), "arr_cleanup_test", fn);
    auto* destroy = llvm::BasicBlock::Create(builder->getContext(), "arr_cleanup_destroy", fn);
    auto* done = llvm::BasicBlock::Create(builder->getContext(), "arr_cleanup_done", fn);
    builder->CreateBr(test);
    builder->SetInsertPoint(test);
    auto* count = builder->CreateLoad(builder->getInt32Ty(), count_ptr);
    builder->CreateCondBr(builder->CreateICmpNE(count, builder->getInt32(0)), destroy, done);
    builder->SetInsertPoint(destroy);
    auto* index = builder->CreateSub(count, builder->getInt32(1));
    // Nested containers retain their own progress. Keep them reachable if a
    // child's destructor throws; leaf slots are disarmed before destruction.
    const bool pending_children = value_cleanup_has_pending_children(element);
    if (!pending_children) builder->CreateStore(index, count_ptr);
    auto* child = builder->CreateInBoundsGEP(data_type, data,
        {builder->getInt32(0), index}, "arr_cleanup_element");
    emit_value_cleanup(builder, mod, functions, child, element, emit_call);
    if (pending_children) builder->CreateStore(index, count_ptr);
    builder->CreateBr(test);
    builder->SetInsertPoint(done);
}

// The live word of a union lives at byte 4 of its { i64, [N x i8] } header.
inline llvm::Value* union_live_state_slot(llvm::IRBuilder<>* builder, llvm::Value* storage) {
    return builder->CreateConstGEP1_64(builder->getInt8Ty(), storage, 4, "union_live_state");
}

inline void emit_union_value_cleanup(
    llvm::IRBuilder<>* builder, llvm::Module& mod,
    const std::map<std::shared_ptr<function>, llvm::Function*>& functions,
    llvm::Value* storage, union_type_def& definition,
    const virtual_call_emitter& emit_call = {});
inline void emit_value_cleanup(
    llvm::IRBuilder<>* builder, llvm::Module& mod,
    const std::map<std::shared_ptr<function>, llvm::Function*>& functions,
    llvm::Value* storage, const std::shared_ptr<type>& value_type,
    const virtual_call_emitter& emit_call)
{
    auto bare = type::remove_const(type::canonical(value_type));
    if (auto owner = std::dynamic_pointer_cast<owner_type>(bare)) {
        emit_owner_cleanup_if_nonnull(builder, mod, functions, storage,
            owner->get_owned_type(), "value_owner_cleanup", emit_call);
    } else if (auto callable = std::dynamic_pointer_cast<callable_type>(bare)) {
        if (callable->is_owner()) {
            emit_owned_callable_cleanup_if_nonnull(builder, mod, storage,
                llvm::cast<llvm::StructType>(callable->get_llvm_type()), "value_callable_cleanup", emit_call);
        }
    } else if (auto array = std::dynamic_pointer_cast<sized_array_type>(bare)) {
        emit_array_elements_cleanup(builder, mod, functions, storage, array, emit_call);
    } else if (auto st = std::dynamic_pointer_cast<struct_type>(bare)) {
        if (auto un = st->get_union()) {
            emit_union_value_cleanup(builder, mod, functions, storage, *un, emit_call);
        } else if (auto agg = st->get_struct(); agg && agg->get_destructor()) {
            auto dtor = functions.find(agg->get_destructor()->shared_as<function>());
            if (dtor == functions.end())
                throw std::runtime_error("Missing destructor declaration during value cleanup");
            if (agg->has_vtable()) {
                auto* fn = builder->GetInsertBlock()->getParent();
                auto* destroy = llvm::BasicBlock::Create(builder->getContext(), "value_destroy", fn);
                auto* done = llvm::BasicBlock::Create(builder->getContext(), "value_destroy_done", fn);
                auto* ptr_type = llvm::PointerType::get(builder->getContext(), 0);
                auto* vptr = builder->CreateLoad(ptr_type, storage);
                builder->CreateCondBr(builder->CreateIsNotNull(vptr), destroy, done);
                builder->SetInsertPoint(destroy);
                emit_virtual_destructor_call(builder, *agg, storage, emit_call);
                builder->CreateBr(done);
                builder->SetInsertPoint(done);
            } else if (emit_call) {
                emit_call(dtor->second->getFunctionType(), dtor->second, {storage}, "");
            } else {
                builder->CreateCall(dtor->second, {storage});
            }
        } else if (auto agg = st->get_struct(); agg && struct_releases_fields_without_destructor(*agg)) {
            // Owners of a destructor-less struct are released field by field, in reverse order.
            std::vector<std::pair<unsigned, std::shared_ptr<type>>> fields;
            for (const auto& entry : agg->variables()) {
                auto member = std::dynamic_pointer_cast<member_variable_definition>(entry.second);
                if (!member || !type_releases_fields_without_destructor(member->get_type())) continue;
                if (auto field = st->get_member(member->get_short_name()))
                    fields.emplace_back(static_cast<unsigned>(field->index), member->get_type());
            }
            std::sort(fields.begin(), fields.end(),
                [](const auto& a, const auto& b) { return a.first < b.first; });
            for (auto it = fields.rbegin(); it != fields.rend(); ++it) {
                auto* field_ptr = builder->CreateStructGEP(st->get_llvm_type(), storage, it->first);
                emit_value_cleanup(builder, mod, functions, field_ptr, it->second, emit_call);
            }
        }
    }
}

inline llvm::Function* get_or_create_union_cleanup_function(
    llvm::Module& mod,
    const std::map<std::shared_ptr<function>, llvm::Function*>& functions,
    union_type_def& definition)
{
    const auto name = "__klang_union_cleanup_" + definition.get_mangled_name();
    if (auto* existing = mod.getFunction(name)) return existing;
    auto& ctx = mod.getContext();
    auto* ptr = llvm::PointerType::get(ctx, 0);
    auto* signature = llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), {ptr}, false);
    auto* cleanup = llvm::Function::Create(signature, llvm::Function::InternalLinkage, name, mod);
    auto personality = mod.getOrInsertFunction("__gxx_personality_v0",
        llvm::FunctionType::get(llvm::Type::getInt32Ty(ctx), true));
    cleanup->setPersonalityFn(llvm::cast<llvm::Constant>(personality.getCallee()));
    auto* entry = llvm::BasicBlock::Create(ctx, "entry", cleanup);
    auto* unwind = llvm::BasicBlock::Create(ctx, "cleanup_unwind", cleanup);
    llvm::IRBuilder<> builder(entry);
    virtual_call_emitter invoke = [&](llvm::FunctionType* type, llvm::Value* callee,
                                     const std::vector<llvm::Value*>& args, const std::string&) -> llvm::Value* {
        auto* next = llvm::BasicBlock::Create(ctx, "cleanup_continue", cleanup);
        auto* call = builder.CreateInvoke(type, callee, next, unwind, args);
        builder.SetInsertPoint(next);
        return call;
    };
    emit_union_value_cleanup(&builder, mod, functions, cleanup->getArg(0), definition, invoke);
    builder.CreateRetVoid();
    builder.SetInsertPoint(unwind);
    auto* exception = builder.CreateLandingPad(llvm::StructType::get(ctx, {ptr, builder.getInt32Ty()}), 0);
    exception->setCleanup(true);
    // Finish a retained array/nested-union prefix without revisiting disarmed leaves.
    emit_union_value_cleanup(&builder, mod, functions, cleanup->getArg(0), definition);
    builder.CreateResume(exception);
    return cleanup;
}

inline std::string value_cleanup_symbol_key(const std::shared_ptr<type>& value_type) {
    auto bare = type::remove_const(type::canonical(value_type));
    if (auto st = std::dynamic_pointer_cast<struct_type>(bare)) {
        if (auto un = st->get_union()) return un->get_mangled_name();
        if (auto aggregate = st->get_struct()) return aggregate->get_mangled_name();
    }
    if (auto array = std::dynamic_pointer_cast<array_type>(bare)) {
        auto sized = std::dynamic_pointer_cast<sized_array_type>(array);
        return "array_" + (sized ? std::to_string(sized->get_size()) : "dynamic")
            + "_" + value_cleanup_symbol_key(array->get_subtype());
    }
    if (auto owner = std::dynamic_pointer_cast<owner_type>(bare))
        return "owner_" + value_cleanup_symbol_key(owner->get_owned_type());
    if (auto callable = std::dynamic_pointer_cast<callable_type>(bare)) {
        if (callable->is_unbound_member()) return "unbound_callable";
        return callable->is_owner() ? "owned_callable" : "borrowed_callable";
    }
    return bare->to_string();
}

inline llvm::Function* get_or_create_owned_container_cleanup_function(
    llvm::Module& mod,
    const std::map<std::shared_ptr<function>, llvm::Function*>& functions,
    const std::shared_ptr<type>& value_type)
{
    const auto name = "__klang_owned_cleanup_" + value_cleanup_symbol_key(value_type);
    if (auto* existing = mod.getFunction(name)) return existing;
    auto& ctx = mod.getContext();
    auto* ptr = llvm::PointerType::get(ctx, 0);
    auto* signature = llvm::FunctionType::get(llvm::Type::getVoidTy(ctx), {ptr}, false);
    auto* cleanup = llvm::Function::Create(signature, llvm::Function::InternalLinkage, name, mod);
    auto personality = mod.getOrInsertFunction("__gxx_personality_v0",
        llvm::FunctionType::get(llvm::Type::getInt32Ty(ctx), true));
    cleanup->setPersonalityFn(llvm::cast<llvm::Constant>(personality.getCallee()));
    auto free = mod.getOrInsertFunction("free", signature);
    auto* entry = llvm::BasicBlock::Create(ctx, "entry", cleanup);
    auto* unwind = llvm::BasicBlock::Create(ctx, "cleanup_unwind", cleanup);
    llvm::IRBuilder<> builder(entry);
    virtual_call_emitter invoke = [&](llvm::FunctionType* type, llvm::Value* callee,
                                     const std::vector<llvm::Value*>& args, const std::string&) -> llvm::Value* {
        auto* next = llvm::BasicBlock::Create(ctx, "cleanup_continue", cleanup);
        auto* call = builder.CreateInvoke(type, callee, next, unwind, args);
        builder.SetInsertPoint(next);
        return call;
    };
    auto destroy = [&](const virtual_call_emitter& emit_call) {
        if (auto array = std::dynamic_pointer_cast<array_type>(value_type))
            emit_array_elements_cleanup(&builder, mod, functions, cleanup->getArg(0), array, emit_call);
        else
            emit_value_cleanup(&builder, mod, functions, cleanup->getArg(0), value_type, emit_call);
    };
    destroy(invoke);
    builder.CreateCall(free, {cleanup->getArg(0)});
    builder.CreateRetVoid();
    builder.SetInsertPoint(unwind);
    auto* exception = builder.CreateLandingPad(llvm::StructType::get(ctx, {ptr, builder.getInt32Ty()}), 0);
    exception->setCleanup(true);
    destroy({});
    builder.CreateCall(free, {cleanup->getArg(0)});
    builder.CreateResume(exception);
    return cleanup;
}

inline void emit_transferred_value_reset(
    llvm::IRBuilder<>* builder, llvm::Value* storage, const std::shared_ptr<type>& value_type)
{
    auto bare = type::remove_const(type::canonical(value_type));
    if (auto st = std::dynamic_pointer_cast<struct_type>(bare); st && st->get_union()) {
        auto* disc = builder->CreateStructGEP(st->get_llvm_type(), storage, 0);
        builder->CreateStore(builder->getInt32(-1), disc);
        builder->CreateStore(builder->getInt32(0), union_live_state_slot(builder, storage));
    } else if (type::is_sized_array(bare)) {
        builder->CreateStore(llvm::Constant::getNullValue(bare->get_llvm_type()), storage);
    }
}

inline void emit_union_value_transfer(
    llvm::IRBuilder<>* builder, llvm::Value* destination, union_type_def& destination_type,
    llvm::Value* source, union_type_def& source_type, bool move,
    const std::function<void(llvm::Value*, llvm::Value*, const std::shared_ptr<type>&)>& copy_value)
{
    auto* source_layout = source_type.get_struct_type()->get_llvm_type();
    auto* destination_layout = destination_type.get_struct_type()->get_llvm_type();
    auto* source_disc = builder->CreateStructGEP(source_layout, source, 0);
    auto* destination_disc = builder->CreateStructGEP(destination_layout, destination, 0);
    auto* source_live = union_live_state_slot(builder, source);
    auto* destination_live = union_live_state_slot(builder, destination);
    auto* disc = builder->CreateLoad(builder->getInt32Ty(), source_disc);
    llvm::Value* live = builder->CreateLoad(builder->getInt32Ty(), source_live);
    if (!move) {
        // A copied escaped default is a constructed value in the destination.
        live = builder->CreateSelect(builder->CreateICmpEQ(live, builder->getInt32(2)),
            builder->getInt32(1), live);
    }
    builder->CreateStore(builder->getInt32(-1), destination_disc);
    builder->CreateStore(builder->getInt32(0), destination_live);
    auto* fn = builder->GetInsertBlock()->getParent();
    auto* done = llvm::BasicBlock::Create(builder->getContext(), "union_transfer_done", fn);
    auto* dispatch = builder->CreateSwitch(disc, done);
    for (const auto* alternative : destination_type.all_alternatives_ptrs()) {
        if (!source_type.get_alternative_by_global_index(alternative->index)) continue;
        auto* branch = llvm::BasicBlock::Create(builder->getContext(),
            "union_transfer_" + alternative->name, fn);
        dispatch->addCase(builder->getInt32(alternative->index), branch);
        builder->SetInsertPoint(branch);
        auto* source_payload = builder->CreateStructGEP(source_layout, source, 1);
        auto* destination_payload = builder->CreateStructGEP(destination_layout, destination, 1);
        if (move) {
            auto* payload_type = alternative->resolved_type->get_llvm_type();
            builder->CreateStore(builder->CreateLoad(payload_type, source_payload), destination_payload);
        } else {
            copy_value(destination_payload, source_payload, alternative->resolved_type);
        }
        builder->CreateStore(disc, destination_disc);
        builder->CreateStore(live, destination_live);
        builder->CreateBr(done);
    }
    builder->SetInsertPoint(done);
    if (move) {
        builder->CreateStore(builder->getInt32(-1), source_disc);
        builder->CreateStore(builder->getInt32(0), source_live);
    }
}

/**
 * Return true if a value of type `t` can be safely duplicated with a raw
 * bytewise copy (memcpy): no destructor and no user copy constructor exist
 * anywhere in its layout (recursively through base classes, member variables,
 * and sized-array elements). An owner type is never trivially copyable.
 *
 * Shared between the aggregate resolver (which decides whether to auto-
 * generate a memcpy-based default copy constructor) and the code generator
 * (which decides whether a plain memcpy is a safe fallback for a struct
 * copy that has no copy constructor at all).
 */
inline bool aggregate_type_is_trivially_copyable(const std::shared_ptr<type>& t) {
    auto nt = type::remove_const(type::canonical(t));
    if (!nt) return true;

    // An owner directly owns heap memory — never bytewise copyable.
    if (std::dynamic_pointer_cast<owner_type>(nt)) return false;
    if (auto callable = std::dynamic_pointer_cast<callable_type>(nt); callable && callable->is_owner())
        return false;

    // A sized array is trivially copyable iff its element type is.
    if (auto arr = std::dynamic_pointer_cast<sized_array_type>(nt)) {
        return aggregate_type_is_trivially_copyable(arr->get_subtype());
    }

    auto st_type = std::dynamic_pointer_cast<struct_type>(nt);
    if (!st_type) return true; // primitives, pointers, references, ...

    auto st = st_type->get_struct();
    if (!st) {
        if (auto un = st_type->get_union())
            for (const auto* alternative : un->all_alternatives_ptrs())
                if (!aggregate_type_is_trivially_copyable(alternative->resolved_type)) return false;
        return true;
    }

    // A user or intrinsic destructor, or a copy constructor, signals that the
    // type manages its own resources and must not be copied bytewise.
    if (st->get_destructor()) return false;
    if (st->get_copy_constructor()) return false;

    // Recurse into base sub-objects.
    for (auto& bs : st->get_bases()) {
        if (bs.base && bs.base->get_struct_type()) {
            if (!aggregate_type_is_trivially_copyable(bs.base->get_struct_type())) return false;
        }
    }

    // Recurse into member variables.
    for (auto& entry : st->variables()) {
        auto mv = std::dynamic_pointer_cast<member_variable_definition>(entry.second);
        if (!mv) continue;
        if (!aggregate_type_is_trivially_copyable(mv->get_type())) return false;
    }

    return true;
}

/**
 * Return true if a value of type `t` holds (recursively, through base
 * classes and member variables) a field that references or owns an external
 * resource: an owner, pointer, link or view addresser, or a nested struct
 * that itself has such a field. Such a field makes a bytewise copy unsafe
 * (aliasing / double-free risk) even when the struct has no owner-typed
 * field directly and even when it only has a destructor with no otherwise
 * unsafe field (e.g. a destructor that merely updates a counter is safe to
 * bytewise-copy; a destructor that frees a raw pointer member is not).
 */
inline bool aggregate_type_has_resource_field(const std::shared_ptr<type>& t) {
    auto nt = type::remove_const(type::canonical(t));
    if (!nt) return false;

    if (type::is_owner(nt) || type::is_pointer(nt) || type::is_link(nt) || type::is_view(nt)) {
        return true;
    }
    if (auto callable = std::dynamic_pointer_cast<callable_type>(nt); callable && callable->is_owner())
        return true;

    if (auto arr = std::dynamic_pointer_cast<sized_array_type>(nt)) {
        return aggregate_type_has_resource_field(arr->get_subtype());
    }

    auto st_type = std::dynamic_pointer_cast<struct_type>(nt);
    if (!st_type) return false;

    auto st = st_type->get_struct();
    if (!st) {
        // Union resources follow the union copy policy: alternative 0 (also the default
        // state) is checked at runtime, owners in other alternatives make the union
        // statically move-only. Other noncopyable payloads are rejected by the runtime copy.
        if (st_type->get_union()) return value_type_is_move_only(nt);
        return false;
    }

    // An explicitly deleted copy constructor makes every aggregate embedding the type
    // non-copyable: a bytewise copy would bypass the deletion.
    if (auto copy = st->get_copy_constructor(); copy && copy->is_deleted()) return true;

    // An imported aggregate exposes only its public members; its private fields (owners,
    // unions holding owners, ...) are opaque. A compiler-generated destructor exists only
    // because some hidden member needs cleanup, so a bytewise copy would duplicate it.
    if (st->is_imported()) {
        auto imported_dtor = std::dynamic_pointer_cast<imported_destructor>(st->get_destructor());
        if (imported_dtor && imported_dtor->get_kdi_destructor()
            && imported_dtor->get_kdi_destructor()->is_compiler_generated) {
            return true;
        }
    }

    for (auto& bs : st->get_bases()) {
        if (bs.base && bs.base->get_struct_type()) {
            if (aggregate_type_has_resource_field(bs.base->get_struct_type())) return true;
        }
    }

    for (auto& entry : st->variables()) {
        auto mv = std::dynamic_pointer_cast<member_variable_definition>(entry.second);
        if (!mv) continue;
        if (aggregate_type_has_resource_field(mv->get_type())) return true;
    }

    return false;
}

/** How a member or base sub-object is copied by a synthesized memberwise copy constructor. */
enum class member_copy_kind {
    plain,        ///< copied bytewise
    constructed,  ///< copied by its own copy constructor
    blocked       ///< owns or aliases a resource: cannot be copied implicitly
};

inline bool aggregate_needs_memberwise_copy(const aggregate& st);

inline member_copy_kind member_copy_kind_of(const std::shared_ptr<type>& t) {
    auto nt = type::canonical(type::remove_const(t));
    if (!nt) return member_copy_kind::plain;
    if (type::contains_unresolved(nt)) return member_copy_kind::blocked;
    if (type::is_owner(nt) || type::is_pointer(nt) || type::is_link(nt) || type::is_view(nt)) {
        return member_copy_kind::blocked;
    }
    if (auto callable = std::dynamic_pointer_cast<callable_type>(nt); callable && callable->is_owner()) {
        return member_copy_kind::blocked;
    }
    if (auto arr = std::dynamic_pointer_cast<sized_array_type>(nt)) {
        return member_copy_kind_of(arr->get_subtype());
    }
    auto st_type = std::dynamic_pointer_cast<struct_type>(nt);
    if (!st_type) return member_copy_kind::plain;
    auto agg = st_type->get_struct();
    if (!agg) return member_copy_kind::blocked; // unions are handled by their own lifecycle
    if (auto copy = agg->get_copy_constructor()) {
        if (copy->is_deleted()) return member_copy_kind::blocked;
        if (!copy->is_compiler_generated() || copy->is_memberwise_copy()) return member_copy_kind::constructed;
    }
    if (aggregate_needs_memberwise_copy(*agg)) return member_copy_kind::constructed;
    return aggregate_type_has_resource_field(nt) ? member_copy_kind::blocked : member_copy_kind::plain;
}

/**
 * True when a local struct or class must copy its sub-objects through their own copy
 * constructors: at least one base or member needs one (for instance a k::String), and
 * every other base or member can be copied bytewise. A bytewise copy of such a type would
 * duplicate the owned resources. It holds when the type has no copy constructor, or only
 * a compiler-generated bytewise one, which is then upgraded to a memberwise copy.
 */
inline bool aggregate_needs_memberwise_copy(const aggregate& st) {
    if (st.is_imported() || !(st.is_struct() || st.is_class()) || st.has_virtual_bases()) return false;
    if (auto copy = st.get_copy_constructor()) {
        if (copy->is_deleted() || !copy->is_compiler_generated() || copy->is_memberwise_copy()) return false;
    }
    bool constructed = false;
    auto classify = [&](const std::shared_ptr<type>& t) {
        auto kind = member_copy_kind_of(t);
        if (kind == member_copy_kind::constructed) constructed = true;
        return kind != member_copy_kind::blocked;
    };
    for (const auto& bs : st.get_bases()) {
        if (bs.base && bs.base->get_struct_type() && !classify(bs.base->get_struct_type())) return false;
    }
    for (const auto& entry : st.variables()) {
        auto mv = std::dynamic_pointer_cast<member_variable_definition>(entry.second);
        if (!mv || entry.first.rfind("__", 0) == 0) continue;
        if (!classify(mv->get_type())) return false;
    }
    return constructed;
}

} // namespace k::model::gen

namespace k::model::gen {

// ── Forward declarations for helpers defined in specific gen_*.cpp files ────

/** Emit vptr store (defined in gen_class.cpp). */
void emit_vptr_store(llvm::IRBuilder<>& builder, klass& st, llvm::Value* this_ptr, std::shared_ptr<context> ctx);

/** Emit virtual dispatch call (defined in gen_class.cpp). */
llvm::Value* emit_virtual_dispatch_call(llvm::IRBuilder<>& builder, klass& st, llvm::Value* this_ptr,
    int slot_index, llvm::FunctionType* fn_type, const std::vector<llvm::Value*>& args,
    std::shared_ptr<context> ctx, const std::string& result_name,
    const virtual_call_emitter& emit_call = {});

/** Compute operator dispatch info (defined in gen_operators.cpp). */
virtual_dispatch_info compute_operator_dispatch_info(
    const std::shared_ptr<function>& func,
    const std::shared_ptr<type>& receiver_type);

// ── Shared static helpers ──────────────────────────────────────────────────

inline llvm::ArrayType* enum_object_table_type(
    const std::shared_ptr<enumeration>& en,
    const std::shared_ptr<context>& ctx)
{
    if (!en || !ctx || !en->is_object_backed()) return nullptr;
    if (auto* table = en->get_table_global())
        return llvm::dyn_cast<llvm::ArrayType>(table->getValueType());

    auto obj_type = en->get_object_type();
    if (!obj_type || !obj_type->is_resolved()) return nullptr;
    auto* llvm_st = llvm::dyn_cast_or_null<llvm::StructType>(ctx->get_llvm_type(obj_type));
    if (!llvm_st) return nullptr;

    size_t table_size = 0;
    for (const auto& entry : en->entries()) {
        if (entry.value >= 0)
            table_size = std::max(table_size, static_cast<size_t>(entry.value) + 1);
    }
    return llvm::ArrayType::get(llvm_st, table_size);
}

// Runtime tables export the accessor name in the KDI symbol field.
inline llvm::Function* ensure_enum_object_table_accessor_reference(
    const std::shared_ptr<enumeration>& en,
    const std::shared_ptr<context>& ctx)
{
    if (!en || !ctx) return nullptr;
    if (auto* accessor = en->get_table_accessor()) return accessor;
    const auto& symbol = en->get_table_symbol();
    if (!en->has_runtime_table() || symbol.empty()) return nullptr;

    auto* accessor = ctx->module().getFunction(symbol);
    if (!accessor) {
        auto* fn_type = llvm::FunctionType::get(llvm::PointerType::get(**ctx, 0), false);
        accessor = llvm::Function::Create(fn_type, llvm::GlobalValue::ExternalLinkage,
            symbol, ctx->module());
    }
    en->set_table_accessor(accessor);
    return accessor;
}

/** Ensure an LLVM global reference for a constant enum backing table. */
inline llvm::GlobalVariable* ensure_enum_object_table_reference(
    const std::shared_ptr<enumeration>& en,
    const std::shared_ptr<context>& ctx)
{
    if (!en || !ctx || !en->is_object_backed()) return nullptr;
    if (auto* existing = en->get_table_global()) return existing;

    auto* arr_ty = enum_object_table_type(en, ctx);
    if (!arr_ty) return nullptr;

    const std::string table_name = !en->get_table_symbol().empty()
                                   ? en->get_table_symbol()
                                   : ("__klang_enum_table_" + en->get_mangled_name() + "__");
    if (auto* gv = ctx->module().getNamedGlobal(table_name)) {
        en->set_table_global(gv);
        return gv;
    }

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

// A zero-filled nested union has its own valid default tag and lifecycle state.
inline bool has_default_nested_union(const union_type_def& outer) {
    const auto* first = outer.get_alternative_by_global_index(0);
    if (!first || !first->resolved_type) return false;
    auto bare = type::canonical(type::remove_const(type::canonical(first->resolved_type)));
    auto st = std::dynamic_pointer_cast<struct_type>(bare);
    return st && !st->get_struct();
}

// A resource slot reachable from an unconstructed (zero-filled) aggregate: an
// owner, an owning callable or a nested union with resource-bearing alternatives.
// `steps` is the chain of (struct type, field index) leading to it.
struct zero_state_leaf {
    std::vector<std::pair<std::shared_ptr<struct_type>, unsigned>> steps;
    std::shared_ptr<type> leaf_type;
};

// An unconstructed object is "valid" when its resources can be released field by
// field, without running user code, reading a vptr or an element counter.
struct zero_state_profile {
    bool valid = true;
    std::vector<zero_state_leaf> leaves;
};

using zero_state_steps = std::vector<std::pair<std::shared_ptr<struct_type>, unsigned>>;

inline bool is_root_object_aggregate(const aggregate& agg) {
    return is_k_object(agg)
        || (agg.get_short_name() == "Object"
            && (agg.get_fq_name() == "k::Object" || agg.get_fq_name() == "::k::Object"));
}

// `top` is the union alternative itself: only there the activation code fixes up
// an array count.
inline void collect_zero_state_leaves(const std::shared_ptr<type>& t, const std::shared_ptr<ns>& root,
                                      zero_state_profile& profile, zero_state_steps& steps, bool top) {
    if (!profile.valid) return;
    auto bare = type::canonical(type::remove_const(type::canonical(t)));
    if (!bare) { profile.valid = false; return; }
    if (type::is_owner(bare)) { profile.leaves.push_back({steps, bare}); return; }
    if (auto callable = std::dynamic_pointer_cast<callable_type>(bare)) {
        if (callable->is_owner()) profile.leaves.push_back({steps, bare});
        return;
    }
    if (auto enumeration = std::dynamic_pointer_cast<enum_type>(bare)) {
        profile.valid = !enumeration->is_object_backed();
        return;
    }
    if (type::is_primitive(bare) || type::is_pointer(bare) || type::is_link(bare)
        || type::is_view(bare)) return;
    if (auto array = std::dynamic_pointer_cast<sized_array_type>(bare)) {
        if (!top) { profile.valid = false; return; }
        // Elements are released by the array loop; only their resources matter here.
        zero_state_steps element_steps;
        collect_zero_state_leaves(array->get_subtype(), root, profile, element_steps, false);
        return;
    }
    auto st = std::dynamic_pointer_cast<struct_type>(bare);
    if (!st) { profile.valid = false; return; }
    auto agg = st->get_struct();
    if (!agg) {
        auto nested = st->get_union();
        if (!nested) nested = find_union_by_struct_type(root, st);
        if (!nested) profile.valid = false;
        else if (nested->has_nontrivial_destructor_alternative()) profile.leaves.push_back({steps, bare});
        return;
    }
    if (agg->is_imported() || agg->has_virtual_bases() || agg->is_inner()) {
        profile.valid = false;
        return;
    }
    std::vector<std::pair<unsigned, std::shared_ptr<member_variable_definition>>> fields;
    for (const auto& entry : agg->variables()) {
        auto member = std::dynamic_pointer_cast<member_variable_definition>(entry.second);
        if (!member || member->get_short_name() == "__vptr__") continue;
        auto field = st->get_member(member->get_short_name());
        if (!field) { profile.valid = false; return; }
        fields.emplace_back(static_cast<unsigned>(field->index), member);
    }
    std::sort(fields.begin(), fields.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [index, member] : fields) {
        auto member_type = type::canonical(type::remove_const(type::canonical(member->get_type())));
        if (member->get_short_name().rfind("__base_", 0) == 0) {
            auto base_struct = std::dynamic_pointer_cast<struct_type>(member_type);
            if (!base_struct || !base_struct->get_struct()) { profile.valid = false; return; }
            if (is_root_object_aggregate(*base_struct->get_struct())) continue;
        }
        steps.emplace_back(st, index);
        collect_zero_state_leaves(member_type, root, profile, steps, false);
        steps.pop_back();
        if (!profile.valid) return;
    }
}

inline zero_state_profile zero_state_of(const std::shared_ptr<type>& t, const std::shared_ptr<ns>& root,
                                        bool top) {
    zero_state_profile profile;
    zero_state_steps steps;
    collect_zero_state_leaves(t, root, profile, steps, top);
    return profile;
}

// The default (zero-filled, unconstructed) state of a first aggregate or array
// alternative cannot stay invisible when its fields may own resources written
// through a reference: the union is then marked as "default, escaped" (live
// state 2) on every mutable access. Cleanup then releases the resource slots
// field by field, without running the never-executed constructor or destructor.
inline bool union_first_alternative_tracks_default(
    const union_type_def& udef, const std::shared_ptr<ns>& root) {
    const auto* first = udef.get_alternative_by_global_index(0);
    if (!first || !first->resolved_type || first->is_const) return false;
    auto bare = type::canonical(type::remove_const(type::canonical(first->resolved_type)));
    auto st = std::dynamic_pointer_cast<struct_type>(bare);
    if (!type::is_sized_array(bare) && !(st && st->get_struct())) return false;
    auto profile = zero_state_of(bare, root, true);
    return profile.valid && !profile.leaves.empty();
}

// Releases the resource slots of an unconstructed (zero-filled but escaped) union
// alternative field by field, like the generator's default-state cleanup: neither the
// constructor nor the destructor of the alternative ever ran.
inline void emit_default_payload_cleanup(
    llvm::IRBuilder<>* builder, llvm::Module& mod,
    const std::map<std::shared_ptr<function>, llvm::Function*>& functions,
    llvm::Value* storage, const std::shared_ptr<type>& payload_type,
    const virtual_call_emitter& emit_call)
{
    auto bare = type::canonical(type::remove_const(type::canonical(payload_type)));
    if (auto array = std::dynamic_pointer_cast<sized_array_type>(bare)) {
        auto element = type::remove_const(type::canonical(array->get_subtype()));
        if (!value_type_needs_cleanup(element)) return;
        auto* layout = array->get_llvm_struct_type();
        auto* data_type = array->get_llvm_data_array_type();
        auto* count_ptr = builder->CreateStructGEP(layout, storage, array_type::FIELD_SIZE);
        auto* data = builder->CreateStructGEP(layout, storage, array_type::FIELD_DATA);
        auto* fn = builder->GetInsertBlock()->getParent();
        auto* test = llvm::BasicBlock::Create(builder->getContext(), "default_arr_test", fn);
        auto* destroy = llvm::BasicBlock::Create(builder->getContext(), "default_arr_destroy", fn);
        auto* done = llvm::BasicBlock::Create(builder->getContext(), "default_arr_done", fn);
        builder->CreateBr(test);
        builder->SetInsertPoint(test);
        auto* count = builder->CreateLoad(builder->getInt32Ty(), count_ptr);
        builder->CreateCondBr(builder->CreateICmpNE(count, builder->getInt32(0)), destroy, done);
        builder->SetInsertPoint(destroy);
        auto* index = builder->CreateSub(count, builder->getInt32(1));
        builder->CreateStore(index, count_ptr);
        auto* child = builder->CreateInBoundsGEP(data_type, data,
            {builder->getInt32(0), index}, "default_arr_element");
        emit_default_payload_cleanup(builder, mod, functions, child, element, emit_call);
        builder->CreateBr(test);
        builder->SetInsertPoint(done);
        return;
    }
    auto st = std::dynamic_pointer_cast<struct_type>(bare);
    if (st && st->get_struct()) {
        auto profile = zero_state_of(st, nullptr, false);
        if (!profile.valid)
            throw std::runtime_error("Cannot release the default state of '" + st->to_string()
                + "' field by field");
        for (auto leaf = profile.leaves.rbegin(); leaf != profile.leaves.rend(); ++leaf) {
            llvm::Value* slot = storage;
            for (const auto& [step_type, index] : leaf->steps)
                slot = builder->CreateStructGEP(step_type->get_llvm_type(), slot, index, "default_field");
            emit_value_cleanup(builder, mod, functions, slot, leaf->leaf_type, emit_call);
        }
        return;
    }
    emit_value_cleanup(builder, mod, functions, storage, payload_type, emit_call);
}

// Mirrors the generator's union cleanup: a default (zero-filled) first alternative is not
// an object, an escaped default one releases its owned slots field by field, and the
// union is left inactive.
inline void emit_union_value_cleanup(
    llvm::IRBuilder<>* builder, llvm::Module& mod,
    const std::map<std::shared_ptr<function>, llvm::Function*>& functions,
    llvm::Value* storage, union_type_def& definition,
    const virtual_call_emitter& emit_call)
{
    auto* layout = definition.get_struct_type()->get_llvm_type();
    auto* disc_ptr = builder->CreateStructGEP(layout, storage, 0);
    auto* live_ptr = union_live_state_slot(builder, storage);
    auto* disc = builder->CreateLoad(builder->getInt32Ty(), disc_ptr);
    auto* fn = builder->GetInsertBlock()->getParent();
    auto* done = llvm::BasicBlock::Create(builder->getContext(), "union_cleanup_done", fn);
    const auto* first = definition.get_alternative_by_global_index(0);
    auto first_type = first && first->resolved_type
        ? type::canonical(type::remove_const(type::canonical(first->resolved_type))) : nullptr;
    auto first_callable = std::dynamic_pointer_cast<callable_type>(first_type);
    const bool first_is_resource = has_default_nested_union(definition) || type::is_owner(first_type)
        || (first_callable && first_callable->is_owner());
    const bool tracks_default = union_first_alternative_tracks_default(definition, nullptr);
    auto* dispatch = builder->CreateSwitch(disc, done);
    for (const auto* alternative : definition.all_alternatives_ptrs()) {
        if (!value_type_needs_cleanup(alternative->resolved_type)) continue;
        auto* branch = llvm::BasicBlock::Create(builder->getContext(),
            "union_cleanup_" + alternative->name, fn);
        dispatch->addCase(builder->getInt32(alternative->index), branch);
        builder->SetInsertPoint(branch);
        auto* payload = builder->CreateStructGEP(layout, storage, 1);
        const bool pending_children = value_cleanup_has_pending_children(alternative->resolved_type);
        if (alternative->index == 0 && !first_is_resource) {
            auto* state = builder->CreateLoad(builder->getInt32Ty(), live_ptr);
            if (tracks_default) {
                auto* escaped = llvm::BasicBlock::Create(builder->getContext(), "union_cleanup_escaped", fn);
                auto* regular = llvm::BasicBlock::Create(builder->getContext(), "union_cleanup_regular", fn);
                builder->CreateCondBr(builder->CreateICmpEQ(state, builder->getInt32(2)), escaped, regular);
                builder->SetInsertPoint(escaped);
                emit_default_payload_cleanup(builder, mod, functions, payload,
                    alternative->resolved_type, emit_call);
                builder->CreateBr(done);
                builder->SetInsertPoint(regular);
            }
            // A default, unconstructed first alternative owns nothing.
            auto* live = llvm::BasicBlock::Create(builder->getContext(), "union_cleanup_live", fn);
            builder->CreateCondBr(builder->CreateICmpEQ(state, builder->getInt32(0)), done, live);
            builder->SetInsertPoint(live);
        }
        if (!pending_children) builder->CreateStore(builder->getInt32(-1), disc_ptr);
        emit_value_cleanup(builder, mod, functions, payload, alternative->resolved_type, emit_call);
        builder->CreateBr(done);
    }
    builder->SetInsertPoint(done);
    builder->CreateStore(builder->getInt32(-1), disc_ptr);
    builder->CreateStore(builder->getInt32(0), live_ptr);
}

// Copy-initialising an aggregate from an lvalue of its own type chooses among its
// copy constructors only: a converting overload such as `C(base : Base&)` must
// never outrank `C(other : const C&)` because of a cheaper upcast weight.
inline std::vector<std::shared_ptr<constructor>> copy_constructors_of(const aggregate& agg) {
    std::vector<std::shared_ptr<constructor>> copies;
    for (const auto& ctor : agg.constructors()) {
        if (ctor && agg.is_copy_constructor(*ctor)) copies.push_back(ctor);
    }
    return copies;
}

// Only one union leaf and otherwise trivial scalar fields can be copied without
// bookkeeping for previously completed resource-bearing subobjects.
inline std::shared_ptr<union_type_def> scalar_union_wrapper(
    const std::shared_ptr<aggregate>& agg, const std::shared_ptr<ns>& root) {
    std::unordered_set<const aggregate*> active;
    std::function<std::shared_ptr<union_type_def>(const std::shared_ptr<aggregate>&, bool)> classify;
    classify = [&](const std::shared_ptr<aggregate>& current, bool nested)
            -> std::shared_ptr<union_type_def> {
        if (!current || current->is_imported() || !current->is_struct() || current->is_inner()
            || current->has_bases() || current->has_virtual_bases()
            || !active.insert(current.get()).second) return nullptr;
        auto finish = [&](std::shared_ptr<union_type_def> result) {
            active.erase(current.get());
            return result;
        };
        if (nested) {
            for (const auto& ctor : current->constructors()) {
                if (ctor && current->is_copy_constructor(*ctor) && !ctor->is_compiler_generated())
                    return finish(nullptr);
            }
            for (const auto& op : current->get_functions("__operator_aS_")) {
                if (op && !op->is_compiler_generated()) return finish(nullptr);
            }
        }
        std::shared_ptr<union_type_def> result;
        for (const auto& [name, var] : current->variables()) {
            auto member = std::dynamic_pointer_cast<member_variable_definition>(var);
            if (!member) continue;
            if (name.rfind("__", 0) == 0) return finish(nullptr);
            auto field_type = type::canonical(type::remove_const(member->get_type()));
            auto field_struct = std::dynamic_pointer_cast<struct_type>(field_type);
            if (field_struct) {
                if (result) return finish(nullptr);
                auto child = field_struct->get_struct();
                if (child && (current->is_instantiation() || child->is_instantiation()))
                    return finish(nullptr);
                result = child ? classify(child, true) : find_union_by_struct_type(root, field_struct);
                if (!result) return finish(nullptr);
            } else if (auto enumeration = std::dynamic_pointer_cast<enum_type>(field_type)) {
                if (enumeration->is_object_backed()) return finish(nullptr);
            } else if (!type::is_primitive(field_type)) return finish(nullptr);
        }
        return finish(result);
    };
    return classify(agg, false);
}

/**
 * Upcast a derived class pointer to a base class or interface pointer at LLVM IR level.
 */
inline llvm::Value* emit_class_upcast(
    llvm::IRBuilder<>* builder,
    context* ctx,
    aggregate* src_st,
    struct_type* src_st_type,
    aggregate* tgt_st,
    llvm::Value* src_ptr)
{
    if (!src_st || !tgt_st || src_st == tgt_st || !src_ptr) {
        return src_ptr;
    }
    if (!src_st_type) {
        src_st_type = src_st->get_struct_type().get();
    }
    if (!src_st_type) return src_ptr;

    auto& llvm_ctx = ctx->llvm_context();
    llvm::Type* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);

    // Direct base
    std::string subobj_name;
    for (auto& bs : src_st->get_bases()) {
        if (bs.base && bs.base.get() == tgt_st) {
            if (bs.is_virtual) {
                std::string vbptr_name = "__vbptr_" + bs.base->get_short_name() + "__";
                if (auto vbptr_field = src_st_type->get_member(vbptr_name)) {
                    auto src_llvm_type = ctx->get_llvm_type(src_st_type->shared_from_this());
                    llvm::Value* vbptr_addr = builder->CreateStructGEP(
                        src_llvm_type, src_ptr, (unsigned)vbptr_field->index,
                        "vbptr_" + tgt_st->get_short_name() + "_addr");
                    return builder->CreateLoad(ptr_ty, vbptr_addr,
                        "vbase_" + tgt_st->get_short_name() + "_ptr");
                }
                subobj_name = "__vbase_" + bs.sanitised_name() + "__";
            } else {
                subobj_name = "__base_" + bs.sanitised_name() + "__";
            }
            break;
        }
    }
    if (!subobj_name.empty()) {
        auto src_llvm_type = ctx->get_llvm_type(src_st_type->shared_from_this());
        if (auto field = src_st_type->get_member(subobj_name)) {
            return builder->CreateStructGEP(
                src_llvm_type,
                src_ptr,
                (unsigned)field->index,
                "base_" + tgt_st->get_short_name() + "_ptr"
            );
        }
    }

    // Transitive upcast via DFS
    std::function<llvm::Value*(aggregate*, struct_type*, llvm::Value*)> dfs_gep;
    dfs_gep = [&](aggregate* cur_agg, struct_type* cur_st_type, llvm::Value* cur_ptr) -> llvm::Value* {
        for (auto& bs : cur_agg->get_bases()) {
            if (!bs.base) continue;

            auto base_agg = bs.base;
            auto base_st_type = base_agg->get_struct_type();
            if (!base_st_type) continue;
            llvm::Type* cur_llvm_type = cur_st_type->get_llvm_type();
            if (!cur_llvm_type) continue;

            llvm::Value* base_ptr = nullptr;
            if (bs.is_virtual) {
                std::string vbptr_name = "__vbptr_" + bs.base->get_short_name() + "__";
                auto vbptr_field = cur_st_type->get_member(vbptr_name);
                if (!vbptr_field) continue;
                llvm::Value* vbptr_addr = builder->CreateStructGEP(
                    cur_llvm_type, cur_ptr, (unsigned)vbptr_field->index,
                    "trans_vbptr_" + bs.sanitised_name() + "_addr");
                base_ptr = builder->CreateLoad(ptr_ty, vbptr_addr,
                    "trans_vbase_" + bs.sanitised_name() + "_ptr");
            } else {
                std::string field_name = "__base_" + bs.sanitised_name() + "__";
                auto field = cur_st_type->get_member(field_name);
                if (!field) continue;
                base_ptr = builder->CreateStructGEP(
                    cur_llvm_type, cur_ptr, (unsigned)field->index,
                    "trans_base_" + bs.sanitised_name() + "_ptr");
            }

            if (bs.base.get() == tgt_st) {
                return base_ptr;
            }

            std::string vbptr_name2 = "__vbptr_" + tgt_st->get_short_name() + "__";
            if (auto vbptr_field2 = base_st_type->get_member(vbptr_name2)) {
                llvm::Type* inter_llvm_type = base_st_type->get_llvm_type();
                if (inter_llvm_type) {
                    llvm::Value* vbptr_addr = builder->CreateStructGEP(
                        inter_llvm_type, base_ptr, (unsigned)vbptr_field2->index,
                        "trans_vbptr_" + tgt_st->get_short_name() + "_addr");
                    return builder->CreateLoad(ptr_ty, vbptr_addr,
                        "trans_vbase_" + tgt_st->get_short_name() + "_ptr");
                }
            }
            if (auto res = dfs_gep(bs.base.get(), base_st_type.get(), base_ptr)) {
                return res;
            }
            std::string vbase_name2 = "__vbase_" + tgt_st->get_short_name() + "__";
            if (auto vbase_field2 = base_st_type->get_member(vbase_name2)) {
                llvm::Type* inter_llvm_type = base_st_type->get_llvm_type();
                if (!inter_llvm_type) continue;
                return builder->CreateStructGEP(
                    inter_llvm_type, base_ptr, (unsigned)vbase_field2->index,
                    "trans_vbase_" + tgt_st->get_short_name() + "_ptr");
            }
        }
        return nullptr;
    };
    if (auto res = dfs_gep(src_st, src_st_type, src_ptr)) {
        return res;
    }

    // Direct vbptr
    {
        std::string vbptr_name = "__vbptr_" + tgt_st->get_short_name() + "__";
        auto src_llvm_type = ctx->get_llvm_type(src_st_type->shared_from_this());
        if (auto vbptr_field = src_st_type->get_member(vbptr_name)) {
            llvm::Value* vbptr_addr = builder->CreateStructGEP(
                src_llvm_type, src_ptr, (unsigned)vbptr_field->index,
                "vbptr_" + tgt_st->get_short_name() + "_addr");
            return builder->CreateLoad(ptr_ty, vbptr_addr,
                "vbase_" + tgt_st->get_short_name() + "_ptr");
        }
    }
    // Direct vbase
    {
        std::string vbase_name = "__vbase_" + tgt_st->get_short_name() + "__";
        auto src_llvm_type = ctx->get_llvm_type(src_st_type->shared_from_this());
        if (auto vbase_field = src_st_type->get_member(vbase_name)) {
            return builder->CreateStructGEP(
                src_llvm_type, src_ptr, (unsigned)vbase_field->index,
                "vbase_" + tgt_st->get_short_name() + "_ptr");
        }
    }

    return src_ptr;
}

/**
 * Switch on a polymorphic union's active discriminant and return a pointer to the common base class/interface.
 */
inline llvm::Value* emit_polymorphic_union_to_base_ptr(
    llvm::IRBuilder<>* builder,
    context* ctx,
    const union_type_def& union_def,
    aggregate* poly_base,
    llvm::Value* union_ptr)
{
    auto& llvm_ctx = ctx->llvm_context();
    auto* cur_fn = builder->GetInsertBlock()->getParent();
    auto* i32_ty = llvm::Type::getInt32Ty(llvm_ctx);
    auto* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);

    auto st_type = union_def.get_struct_type();
    auto* union_llvm_type = st_type ? st_type->get_llvm_type() : nullptr;

    // Load discriminant from field 0
    auto* disc_ptr = builder->CreateStructGEP(union_llvm_type, union_ptr, 0, "poly_union_disc_ptr");
    auto* disc_val = builder->CreateLoad(i32_ty, disc_ptr, "poly_union_disc");

    auto alts = union_def.all_alternatives_ptrs();

    auto* merge_bb = llvm::BasicBlock::Create(llvm_ctx, "poly_union_merge", cur_fn);
    auto* fail_bb = llvm::BasicBlock::Create(llvm_ctx, "poly_union_fail", cur_fn);

    auto* switch_inst = builder->CreateSwitch(disc_val, fail_bb, alts.size());

    std::vector<std::pair<llvm::Value*, llvm::BasicBlock*>> incoming;

    for (const auto* alt : alts) {
        auto* case_bb = llvm::BasicBlock::Create(llvm_ctx, "poly_union_case_" + alt->name, cur_fn);
        switch_inst->addCase(llvm::ConstantInt::get(i32_ty, alt->index), case_bb);

        builder->SetInsertPoint(case_bb);
        // GEP to storage (field 1)
        auto* storage_ptr = builder->CreateStructGEP(union_llvm_type, union_ptr, 1, "union_storage");
        llvm::Value* base_ptr = storage_ptr;
        auto bare_alt = type::remove_const(alt->resolved_type);
        if (auto alt_st_type = std::dynamic_pointer_cast<struct_type>(bare_alt)) {
            if (auto alt_agg = alt_st_type->get_struct()) {
                base_ptr = emit_class_upcast(builder, ctx, alt_agg.get(), alt_st_type.get(), poly_base, storage_ptr);
            }
        }
        auto* end_case_bb = builder->GetInsertBlock();
        incoming.push_back({base_ptr, end_case_bb});
        builder->CreateBr(merge_bb);
    }

    // Fail BB: emit trap
    builder->SetInsertPoint(fail_bb);
    builder->CreateIntrinsic(llvm::Intrinsic::trap, llvm::ArrayRef<llvm::Type*>{}, {});
    builder->CreateUnreachable();

    // Merge BB
    builder->SetInsertPoint(merge_bb);
    auto* phi = builder->CreatePHI(ptr_ty, incoming.size(), "poly_union_base_ptr");
    for (auto& [val, bb] : incoming) {
        phi->addIncoming(val, bb);
    }
    return phi;
}

/**
 * Tell whether an aggregate's RTTI global is defined by another module.
 *
 * Only aggregates carrying a vtable get an RTTI global emitted, so an imported
 * aggregate without a vtable has no definition anywhere and must not be referred
 * to through an external declaration.
 */
inline bool has_external_rtti_definition(const std::shared_ptr<aggregate>& agg)
{
    auto imp = std::dynamic_pointer_cast<imported_aggregate>(agg);
    if (!imp) return false;
    auto* kdi = imp->get_kdi_aggregate();
    return kdi != nullptr && kdi->vtable.has_value();
}

/**
 * Get, or lazily introduce, the RTTI (typeinfo) global for an aggregate.
 *
 * The declaration pass emits a full RTTI definition for every aggregate owned by
 * the current unit, so if the global is already present in the module it is the
 * real definition and is returned as-is.
 *
 * When it is absent the aggregate comes from another module (a KDI import): the
 * global must then be introduced as a layout-compatible *external declaration*
 * so that both AOT linking and the ORC JIT bind it to the single definition
 * exported by the defining library. Emitting a `linkonce_odr null` definition
 * instead would give the importing module its own copy of the symbol; exception
 * dispatch compares typeinfo pointers by identity, so a duplicate silently
 * breaks every `catch` of an imported exception type across the module boundary.
 *
 * @param mod             Module being generated.
 * @param name            Mangled RTTI symbol name.
 * @param external_if_absent  True only when another module is known to define the
 *                        symbol (see has_external_rtti_definition). Synthetic
 *                        `_KTI_<type>` placeholders and vtable-less aggregates
 *                        are defined nowhere and keep a local weak definition so
 *                        that they stay linkable.
 */
inline llvm::GlobalVariable* get_or_declare_typeinfo_global(
    llvm::Module& mod, const std::string& name, bool external_if_absent,
    bool annotation_rtti = false)
{
    if (auto* existing = mod.getNamedGlobal(name)) {
        return existing;
    }
    auto* ptr_ty = llvm::PointerType::get(mod.getContext(), 0);
    if (external_if_absent) {
        return new llvm::GlobalVariable(
            mod, annotation_rtti
                ? annotation_rtti_value_type(mod.getContext())
                : aggregate_rtti_value_type(mod.getContext()),
            /*isConstant=*/true,
            llvm::GlobalValue::ExternalLinkage,
            /*Initializer=*/nullptr, name);
    }
    return new llvm::GlobalVariable(
        mod, ptr_ty, /*isConstant=*/true,
        llvm::GlobalValue::LinkOnceODRLinkage,
        llvm::ConstantPointerNull::get(ptr_ty), name);
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
