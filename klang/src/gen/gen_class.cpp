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
// gen_class.cpp — Code generation for K language classes
//
// This file contains all visitor method overrides and helper functions
// related to the 'class' feature:
//
//  1. symbol_resolver helpers: vtable layout computation, virtual function
//     marking, vptr injection, diamond virtual inheritance.
//  2. type_reference_resolver helpers: resolve vptr types, build LLVM
//     vtable struct types.
//  3. declaration_generator helpers: emit vtable globals, emit dispatch
//     thunks (virtual dispatch wrappers).
//  4. implementation_generator helpers: emit C1/C2 constructors,
//     D1/D2 destructors, vtable initialisation code.
//
// ─── Virtual inheritance model ───────────────────────────────────────────────
//
//  For single inheritance:
//    class B { virtual void f(); }
//    class D : B { void f() override; }
//
//    Vtable of D (primary):
//      slot 0  : ptr  RTTI global (@_KTRINmoduleDDE) — self-pointer typeid, always non-null for non-abstract
//      slot 1  : ptr  @_KFMvN...fE  (D::f dispatch thunk)
//
//    Layout of D:
//      field 0 : ptr  __vptr__ → points to D's vtable
//      field 1 : B sub-object (inlined, contains its own vptr from B)
//      ...
//
//  For diamond inheritance:
//    class A { int x; virtual void f(); }
//    class B : A  { void f() override; }
//    class C : A  { void f() override; }
//    class D : B, C {}
//
//    D has two vptrs:
//      __vptr__     → primary vtable (B path)
//      __vptr_C__   → secondary vtable (C path, with vbase offset)
//
//    Each base sub-object's vptr is set to the appropriate vtable on
//    construction via C1/C2 constructor variants.
//
// ─── Constructor variants ────────────────────────────────────────────────────
//
//    C1 (complete object constructor):
//      - Sets all vptrs to the most-derived vtables.
//      - Calls C2 variants of direct base constructors.
//      - Executes the user-defined constructor body.
//      - Used when constructing the most-derived object.
//
//    C2 (base subobject constructor):
//      - Sets vptrs to intermediate vtables (as seen from the base's path).
//      - Calls C2 variants of its own bases.
//      - Executes the user-defined constructor body.
//      - Used when a class is used as a base in a derived object.
//
// ─── Destructor variants ─────────────────────────────────────────────────────
//
//    D1 (complete object destructor):
//      - Resets vptrs to the most-derived vtables.
//      - Executes the user-defined destructor body.
//      - Calls D2 variants of direct base destructors (reverse order).
//
//    D2 (base subobject destructor):
//      - Resets vptrs to intermediate vtables.
//      - Executes the user-defined destructor body.
//      - Calls D2 variants of its own base destructors (reverse order).
//
// Note: D0 (deleting destructor) is NOT emitted — there is no new/delete yet.
//

#include "resolvers.hpp"
#include "generators.hpp"

#include "../model/mangler.hpp"
#include "../model/context.hpp"
#include "../model/imported.hpp"

#include <llvm/IR/Verifier.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>

#include <unordered_set>
#include <functional>

#include <algorithm>
#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace k::model::gen {

// ─────────────────────────────────────────────────────────────────────────────
//  Helpers (file-local)
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/**
 * True if two non-static member functions have the same virtual signature.
 * The const-qualification of 'this' is part of the signature: a mutable
 * function does NOT override a const function and vice-versa.
 */
bool have_same_virtual_signature(const function& a, const function& b) {
    if (a.get_short_name() != b.get_short_name()) return false;
    if (a.is_const_member() != b.is_const_member()) return false;
    if (a.get_parameter_size() != b.get_parameter_size()) return false;
    for (size_t i = 0; i < a.get_parameter_size(); ++i) {
        auto ta = std::const_pointer_cast<type>(a.get_parameter(i)->get_type());
        auto tb = std::const_pointer_cast<type>(b.get_parameter(i)->get_type());
        if (!type::are_equal(ta, tb)) return false;
    }
    auto ra = std::const_pointer_cast<type>(a.get_return_type());
    auto rb = std::const_pointer_cast<type>(b.get_return_type());
    if (bool(ra) != bool(rb)) return false;
    if (ra && rb && !type::are_equal(ra, rb)) return false;
    return true;
}

/**
 * Collect the linearised list of all distinct base classes in BFS order.
 */
std::vector<std::shared_ptr<aggregate>>
collect_virtual_bases_bfs(const aggregate& st) {
    std::vector<std::shared_ptr<aggregate>> result;
    std::unordered_set<const aggregate*> seen;
    std::queue<std::shared_ptr<aggregate>> q;

    for (auto& bs : st.get_bases()) {
        if (bs.base && !seen.count(bs.base.get())) {
            seen.insert(bs.base.get());
            q.push(bs.base);
            result.push_back(bs.base);
        }
    }
    while (!q.empty()) {
        auto cur = q.front(); q.pop();
        for (auto& bs : cur->get_bases()) {
            if (bs.base && !seen.count(bs.base.get())) {
                seen.insert(bs.base.get());
                q.push(bs.base);
                result.push_back(bs.base);
            }
        }
    }
    return result;
}

/**
 * Build the vtable layout for a class.
 */
std::shared_ptr<vtable_layout>
build_vtable_layout(aggregate& st,
                    std::vector<std::shared_ptr<function>>& warning_override_final,
                    std::vector<std::shared_ptr<function>>& error_private_overrides) {
    auto vt = std::make_shared<vtable_layout>();

    // Inherit slots from primary base (first direct class base with a vtable).
    // The primary base can be either a local klass or an imported aggregate.
    std::shared_ptr<klass>           primary_base;
    std::shared_ptr<imported_aggregate> primary_base_imp;
    for (auto& bs : st.get_bases()) {
        if (auto kl = std::dynamic_pointer_cast<klass>(bs.base)) {
            if (kl->has_vtable()) {
                primary_base = kl;
                break;
            }
        } else if (auto imp = std::dynamic_pointer_cast<imported_aggregate>(bs.base)) {
            if (imp->has_vtable() && !primary_base_imp) {
                primary_base_imp = imp;
                // Don't break — prefer local klass if one comes first, so keep looking
            }
        }
    }
    // If we found both, prefer the first one in declaration order (already done above)
    // If only imported found, use it.

    size_t next_slot = 0;
    if (primary_base) {
        // Local primary base: inherit vtable entries directly
        for (auto& entry : primary_base->get_vtable()->entries) {
            vtable_entry inherited;
            inherited.slot_index = entry.slot_index;
            inherited.introducing_func = entry.introducing_func;
            inherited.func = entry.func;
            vt->entries.push_back(inherited);
            next_slot = std::max(next_slot, entry.slot_index + 1);
        }
    } else if (primary_base_imp) {
        // Imported primary base: build vtable entries from KDI slot descriptors.
        // Each slot's introducing_func is the imported_method with that slot_index.
        const auto* kdi_agg = primary_base_imp->get_kdi_aggregate();
        if (kdi_agg && kdi_agg->vtable.has_value()) {
            const auto& kdi_vt = kdi_agg->vtable.value();
            for (const auto& slot : kdi_vt.slots) {
                // Find the imported_method that introduces this slot
                std::shared_ptr<function> intro_func;
                for (auto& child : primary_base_imp->get_children()) {
                    auto im = std::dynamic_pointer_cast<imported_method>(child);
                    if (im && im->get_vtable_slot() == (int)slot.slot_index) {
                        intro_func = im;
                        break;
                    }
                }
                if (!intro_func) continue; // skip if not found
                vtable_entry inherited;
                inherited.slot_index = slot.slot_index;
                inherited.introducing_func = intro_func;
                inherited.func = intro_func;  // initially: the imported method
                vt->entries.push_back(inherited);
                next_slot = std::max(next_slot, (size_t)slot.slot_index + 1);
            }
        }
    }

    // Process own functions
    for (auto& child : st.get_children()) {
        auto func = std::dynamic_pointer_cast<function>(child);
        if (!func) continue;
        if (func->is_static()) continue;
        if (std::dynamic_pointer_cast<constructor>(func)) continue;
        if (std::dynamic_pointer_cast<destructor>(func)) continue;

        auto vis = func->get_visibility();

        if (vis == PRIVATE) {
            for (auto& entry : vt->entries) {
                if (have_same_virtual_signature(*func, *entry.introducing_func)) {
                    error_private_overrides.push_back(func);
                }
            }
            func->set_virtual(false);
            func->set_vtable_slot(-1);
            continue;
        }

        bool found_override = false;
        for (auto& entry : vt->entries) {
            if (have_same_virtual_signature(*func, *entry.introducing_func)) {
                // Check if the current slot occupant (entry.func) is declared 'final'.
                if (entry.func->is_final_func()) {
                    warning_override_final.push_back(func);
                } else {
                    func->set_virtual(true);
                    func->set_vtable_slot((int)entry.slot_index);
                    func->set_overrides(entry.func);
                    entry.func = func;
                    found_override = true;
                }
                break;
            }
        }

        if (!found_override) {
            if (func->is_final_func() && !func->is_abstract_func()) {
                func->set_virtual(false);
                func->set_vtable_slot(-1);
            } else {
                func->set_virtual(true);
                func->set_vtable_slot((int)next_slot);
                vtable_entry new_entry;
                new_entry.slot_index = next_slot++;
                new_entry.introducing_func = func;
                new_entry.func = func;
                vt->entries.push_back(new_entry);

                // Check if this function overrides a method from a secondary (non-primary) base.
                // Handle both local klass bases and imported aggregate bases.
                for (auto& bs : st.get_bases()) {
                    if (bs.is_virtual) continue;
                    if (auto pk = std::dynamic_pointer_cast<klass>(bs.base)) {
                        if (pk.get() == (primary_base ? primary_base.get() : nullptr)) continue;
                        if (!pk->has_vtable()) continue;
                        for (auto& sec_entry : pk->get_vtable()->entries) {
                            if (sec_entry.introducing_func
                                && have_same_virtual_signature(*func, *sec_entry.introducing_func)) {
                                if (!func->get_overrides()) {
                                    func->set_overrides(sec_entry.func ? sec_entry.func
                                                                       : sec_entry.introducing_func);
                                }
                                break;
                            }
                        }
                    } else if (auto imp = std::dynamic_pointer_cast<imported_aggregate>(bs.base)) {
                        if (imp.get() == (primary_base_imp ? primary_base_imp.get() : nullptr)) continue;
                        if (!imp->has_vtable()) continue;
                        for (auto& child2 : imp->get_children()) {
                            auto im = std::dynamic_pointer_cast<imported_method>(child2);
                            if (im && have_same_virtual_signature(*func, *im)) {
                                if (!func->get_overrides()) {
                                    func->set_overrides(im);
                                }
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    return vt;
}


} // anonymous namespace


llvm::Value* emit_virtual_dispatch_call(
    llvm::IRBuilder<>& builder,
    klass& st,
    llvm::Value* this_ptr,
    int slot_index,
    llvm::FunctionType* fn_type,
    const std::vector<llvm::Value*>& args,
    std::shared_ptr<context> ctx,
    const std::string& result_name) {

    if (!st.has_vtable() || !st.get_vtable()->llvm_type) return nullptr;

    auto vt = st.get_vtable();
    llvm::LLVMContext& llvm_ctx = **ctx;
    llvm::Type* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);

    auto struct_llvm_type = st.get_struct_type()->get_llvm_type();
    if (!struct_llvm_type) return nullptr;

    // Load vptr from field 0
    llvm::Value* vptr_addr = builder.CreateStructGEP(struct_llvm_type, this_ptr, 0, "vptr_addr");
    llvm::Value* vptr = builder.CreateLoad(ptr_ty, vptr_addr, "vptr");

    // Load fn ptr from vtable slot (slot_index + 1 because slot 0 = RTTI)
    llvm::Value* fn_ptr_addr = builder.CreateStructGEP(
        vt->llvm_type, vptr,
        (unsigned)(slot_index + 1),
        "vtable_slot_addr");
    llvm::Value* fn_ptr = builder.CreateLoad(ptr_ty, fn_ptr_addr, "fn_ptr");

    // Void calls (including sret) cannot have a name in LLVM IR.
    std::string call_name = fn_type->getReturnType()->isVoidTy() ? "" : result_name;
    return builder.CreateCall(fn_type, fn_ptr, args, call_name);
}



// symbol_resolver::visit_klass
// ------------------------------
// Extends aggregate symbol resolution with virtual-dispatch infrastructure for classes.
//
// Steps:
//  1. Delegate to visit_aggregate to perform all aggregate-level symbol resolution
//     (base resolution, member injection, implicit ctor/dtor generation, etc.).
//  2. Build the vtable layout for this class:
//     a. Inherit vtable slots from the primary base class (first direct class base
//        that has a vtable), copying all existing entries.
//     b. For each non-static, non-ctor/dtor member function:
//        - PRIVATE functions: cannot override virtual slots → record as error and
//          clear their virtual flag.
//        - If a matching signature exists in the inherited vtable:
//          - If the current slot occupant is 'final', record a warning (the new
//            function will become a new virtual entry instead).
//          - Otherwise, override the slot: mark the function as virtual, assign
//            its slot index, record the overrides link, and update the entry.
//        - If no matching slot: introduce a new vtable slot (unless the function
//          itself is 'final', in which case it is non-virtual).
//  3. Emit diagnostics:
//     - Warnings for functions that attempted to override a 'final' slot → they
//       are promoted to new non-overriding virtual entries instead.
//     - Errors for private functions that shadow virtual slots.
//  4. If the vtable is non-empty, attach it to the class and inject a synthetic
//     __vptr__ field as the first member via klass::inject_vptr_field.
void symbol_resolver::visit_klass(klass& klass) {
    visit_aggregate(klass);

    // Build vtable layout
    std::vector<std::shared_ptr<function>> warning_override_final;
    std::vector<std::shared_ptr<function>> error_private_overrides;
    auto vt = build_vtable_layout(klass, warning_override_final, error_private_overrides);

    for (auto& f : warning_override_final) {
        // Warning: attempting to override a 'final' virtual function → new branch
        std::clog << "Warning: function '" << f->get_short_name()
                  << "' in class '" << klass.get_short_name()
                  << "' attempts to override a 'final' virtual function; "
                     "it will be treated as a new (non-overriding) virtual function." << std::endl;
        if (!f->is_final_func()) {
            size_t next_slot = 0;
            for (auto& e : vt->entries) next_slot = std::max(next_slot, e.slot_index + 1);
            f->set_virtual(true);
            f->set_vtable_slot((int)next_slot);
            vtable_entry new_entry;
            new_entry.slot_index = next_slot;
            new_entry.introducing_func = f;
            new_entry.func = f;
            vt->entries.push_back(new_entry);
        }
    }

    for (auto& f : error_private_overrides) {
        auto diag = k::log::diagnostic::make_error(
            0x30037,
            "private function '{}' in class '{}' cannot override a virtual function",
            {f->get_short_name(), klass.get_short_name()});
        throw resolution_error(std::move(diag));
    }

    // ── Abstract consistency checks ────────────────────────────────────────
    // 1. If any direct member function is abstract, the class itself must be abstract.
    for (auto& child : klass.get_children()) {
        auto func = std::dynamic_pointer_cast<function>(child);
        if (func && func->is_abstract_func() && !klass.is_abstract()) {
            auto diag = k::log::diagnostic::make_error(
                with_flag(0x0038),
                "class '{}' has abstract method '{}' but is not declared 'abstract'; "
                "add the 'abstract' specifier to the class declaration",
                {klass.get_short_name(), func->get_short_name()});
            logger_relay::report(diag);
            throw resolution_error(std::move(diag));
        }
    }
    // 2. If any inherited vtable slot remains abstract (unimplemented), the class must be abstract.
    for (auto& entry : vt->entries) {
        if (entry.func && entry.func->is_abstract_func() && !klass.is_abstract()) {
            auto diag = k::log::diagnostic::make_error(
                with_flag(0x0039),
                "class '{}' inherits unimplemented abstract method '{}' from '{}' but is not declared 'abstract'; "
                "either override '{}' with a concrete implementation or add 'abstract' to the class declaration",
                {klass.get_short_name(), entry.func->get_short_name(),
                 entry.introducing_func->get_owner() ? entry.introducing_func->get_owner()->get_short_name() : "?",
                 entry.func->get_short_name()});
            logger_relay::report(diag);
            throw resolution_error(std::move(diag));
        }
    }

    if (!vt->entries.empty()) {
        klass.set_vtable(vt);
        // Inject __vptr__ as first synthetic member using the public API
        klass.inject_vptr_field("__vptr__");
    }
}

void signature_resolver::visit_klass(klass& klass) {
    visit_aggregate(klass);
}

// type_reference_resolver::visit_klass
// --------------------------------------
// Extends aggregate type-reference resolution by building the LLVM struct type
// that represents the vtable layout of a class.
//
// Steps:
//  1. Delegate to visit_aggregate to resolve all type references in the class body
//     (member types, function parameter/return types, overload collision checks).
//  2. Early-exit if the class has no vtable (no virtual functions declared or inherited).
//  3. Construct the LLVM struct type for the vtable:
//     - Slot 0: opaque pointer (RTTI pointer — filled during declaration generation).
//     - Slots 1..N: one opaque pointer per virtual function entry.
//     The struct is named "__vtable_<ClassName>__" for debuggability.
//  4. Store the resulting llvm::StructType* on the vtable_layout object so that
//     declaration_generator and implementation_generator can reference it.
void type_reference_resolver::visit_klass(klass& klass) {
    visit_aggregate(klass);

    if (!klass.has_vtable()) return;

    auto vt = klass.get_vtable();

    // If aggregate_type_resolver already built the LLVM vtable struct type (Phase 1.a),
    // there is nothing left to do here. This avoids a duplicate-type-name error in LLVM.
    if (vt->llvm_type) return;

    size_t num_slots = vt->slot_count();

    llvm::LLVMContext& llvm_ctx = **_context;
    llvm::Type* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);

    std::vector<llvm::Type*> vtable_fields;
    vtable_fields.push_back(ptr_ty); // RTTI placeholder
    for (size_t i = 0; i < num_slots; ++i) {
        vtable_fields.push_back(ptr_ty);
    }
    std::string vtable_struct_name = "__vtable_" + klass.get_short_name() + "__";
    llvm::StructType* vtable_llvm_type = llvm::StructType::create(llvm_ctx, vtable_fields, vtable_struct_name);
    vt->llvm_type = vtable_llvm_type;
}

// declaration_generator::visit_klass
// ------------------------------------
// Extends aggregate declaration generation by emitting the RTTI global and the
// vtable global variable for a class that has virtual functions.
//
// Steps:
//  1. Delegate to visit_aggregate to emit declarations for all member functions,
//     variables, nested aggregates, constructors, and destructor.
//  2. Early-exit if the class has no vtable or if the vtable LLVM type was not built
//     (type_reference_resolver must have run first).
//  3. Emit the RTTI global variable for ALL classes and interfaces (including abstract):
//     - Struct layout: { ptr self_rtti_ptr, ptr name_cstr, ptr null_introspection }
//     - 'typeid' = address of this global (self-pointer), unique cross-module.
//     - ExternalLinkage so the linker merges duplicates across DSOs (ODR).
//  4. For non-abstract classes only, build the initial vtable initializer:
//     - Slot 0: pointer to the RTTI global (from step 3).
//     - Slots 1..N: null pointers for each virtual function entry (will be filled
//       in by implementation_generator::visit_klass once function bodies are emitted).
//  5. Emit a GlobalVariable named after the mangled vtable name with the null initializer
//     and ExternalLinkage, and store its pointer on the vtable_layout object.
void declaration_generator::visit_klass(klass& klass) {
    // ── Pre-create secondary vtable globals BEFORE visit_aggregate ─────────────
    // Secondary vtable globals must exist before constructors are generated
    // (emit_vptr_store references them). We create null-initialised placeholders here
    // and fill them in implementation_generator::visit_klass after the thunks are built.
    //
    // Create secondary vtable globals for ALL secondary sub-objects transitively
    // reachable in klass's layout (not only direct bases).  This covers cases like
    //   Child : Base (abstract) where Base : Ping (primary), Pong (secondary)
    // — Child needs a secondary vtable for Pong even though Pong is not a direct base.
    //
    // We skip the "primary chain" (first base at each level) and virtual bases here;
    // virtual bases are handled separately below.
    {
        llvm::LLVMContext& llvm_ctx = **_context;
        llvm::Type* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);

        std::unordered_set<const k::model::aggregate*> already_created;

        // Create secondary vtable globals for ALL base sub-objects reachable from
        // klass (transitively, non-virtual), including the "primary" base chain.
        // In K's layout every base sub-object starts at a non-zero offset (klass's
        // own __vptr__ is at field 0), so every base needs its own secondary vtable.
        // This also covers imported bases (imported_klass / imported_interface) which
        // have a vtable layout stored in their KDI.
        std::function<void(const aggregate&)> collect_all_bases;
        collect_all_bases = [&](const aggregate& cur) {
            for (auto& bs : cur.get_bases()) {
                if (!bs.base || bs.is_virtual) continue;
                auto& base_agg = bs.base;
                if (!base_agg->has_vtable()) continue;

                // Obtain the vtable llvm_type for this base.
                // For local klass/interface: from base_agg->get_vtable()->llvm_type.
                // For imported bases: from the kdi vtable llvm_def / already-created vtable struct.
                llvm::StructType* base_vtable_llvm_type = nullptr;
                std::size_t base_vtable_slot_count = 0;

                if (auto base_klass = std::dynamic_pointer_cast<k::model::klass>(base_agg)) {
                    auto base_vt = base_klass->get_vtable();
                    if (!base_vt || !base_vt->llvm_type) {
                        collect_all_bases(*base_agg);
                        continue;
                    }
                    base_vtable_llvm_type = base_vt->llvm_type;
                    base_vtable_slot_count = base_vt->slot_count();
                } else if (auto base_imp = std::dynamic_pointer_cast<imported_aggregate>(base_agg)) {
                    // Imported base: build vtable LLVM type from KDI data
                    const auto* kdi_agg = base_imp->get_kdi_aggregate();
                    if (!kdi_agg || !kdi_agg->vtable.has_value()) {
                        collect_all_bases(*base_agg);
                        continue;
                    }
                    const auto& kdi_vt = kdi_agg->vtable.value();
                    base_vtable_slot_count = kdi_vt.slots.size();
                    // Try to look up or build the vtable llvm type
                    std::string vt_struct_name = "__vtable_" + base_imp->get_short_name() + "__";
                    base_vtable_llvm_type = llvm::StructType::getTypeByName(**_context, vt_struct_name);
                    if (!base_vtable_llvm_type) {
                        // Build it: { ptr(RTTI), ptr*slot_count }
                        std::vector<llvm::Type*> vt_fields;
                        vt_fields.push_back(ptr_ty);  // RTTI
                        for (std::size_t i = 0; i < base_vtable_slot_count; ++i)
                            vt_fields.push_back(ptr_ty);
                        base_vtable_llvm_type = llvm::StructType::create(**_context, vt_fields, vt_struct_name);
                    }
                } else {
                    collect_all_bases(*base_agg);
                    continue;
                }

                if (!already_created.count(base_agg.get())) {
                    already_created.insert(base_agg.get());
                    std::string sec_vtable_name =
                        mangler::mangle_vtable(klass.get_name())
                        + "_for_" + base_agg->get_short_name();

                    std::vector<llvm::Constant*> null_init;
                    null_init.push_back(llvm::ConstantPointerNull::get(
                        llvm::cast<llvm::PointerType>(ptr_ty)));
                    for (size_t i = 0; i < base_vtable_slot_count; ++i)
                        null_init.push_back(llvm::ConstantPointerNull::get(
                            llvm::cast<llvm::PointerType>(ptr_ty)));
                    llvm::Constant* null_struct =
                        llvm::ConstantStruct::get(base_vtable_llvm_type, null_init);
                    auto sec_gv = new llvm::GlobalVariable(
                        _context->module(), base_vtable_llvm_type,
                        true, llvm::GlobalValue::InternalLinkage,
                        null_struct, sec_vtable_name);

                    auto sec_vt_layout = std::make_shared<vtable_layout>();
                    sec_vt_layout->llvm_global = sec_gv;
                    sec_vt_layout->llvm_type = base_vtable_llvm_type;
                    klass.add_secondary_vtable(base_agg, sec_vt_layout);
                }
                collect_all_bases(*base_agg);
            }
        };
        collect_all_bases(klass);

        // Also create secondary vtable globals for virtual bases (__vbase_X__)
        for (auto& vbase_agg : klass.get_all_virtual_base_structs()) {
            auto vbase_klass = std::dynamic_pointer_cast<k::model::klass>(vbase_agg);
            if (!vbase_klass || !vbase_klass->has_vtable()) continue;
            if (already_created.count(vbase_klass.get())) continue;
            already_created.insert(vbase_klass.get());

            // Only if klass owns the __vbase_X__ field
            if (!klass.get_struct_type()) continue;
            std::string vbase_field = "__vbase_" + vbase_klass->get_short_name() + "__";
            if (!klass.get_struct_type()->get_member(vbase_field)) continue;

            auto base_vt = vbase_klass->get_vtable();
            if (!base_vt || !base_vt->llvm_type) continue;

            std::string sec_vtable_name =
                mangler::mangle_vtable(klass.get_name())
                + "_for_" + vbase_klass->get_short_name();

            std::vector<llvm::Constant*> null_init;
            null_init.push_back(llvm::ConstantPointerNull::get(
                llvm::cast<llvm::PointerType>(ptr_ty)));
            for (size_t i = 0; i < base_vt->slot_count(); ++i)
                null_init.push_back(llvm::ConstantPointerNull::get(
                    llvm::cast<llvm::PointerType>(ptr_ty)));
            llvm::Constant* null_struct =
                llvm::ConstantStruct::get(base_vt->llvm_type, null_init);
            auto sec_gv = new llvm::GlobalVariable(
                _context->module(), base_vt->llvm_type,
                true, llvm::GlobalValue::InternalLinkage,
                null_struct, sec_vtable_name);

            auto sec_vt_layout = std::make_shared<vtable_layout>();
            sec_vt_layout->llvm_global = sec_gv;
            sec_vt_layout->llvm_type = base_vt->llvm_type;
            klass.add_secondary_vtable(vbase_klass, sec_vt_layout);
        }
    }

    visit_aggregate(klass);

    if (!klass.has_vtable()) return;

    // ── Emit RTTI global for all classes and interfaces (including abstract) ────
    //
    // Each RTTI global is a genuine ::k::Class or ::k::Interface instance.
    //
    // Both classes and interfaces share the same layout (6 fields):
    //   { ptr __vptr__, ptr __vptr_TypeInfo__, ptr name, ptr bases,
    //     ptr nested, ptr enclosing }
    //
    // The __vptr__ and __vptr_TypeInfo__ point to the Class/Interface vtable and
    // its secondary vtable for the TypeInfo interface.  These vtables are defined
    // in the libk module; we look them up at implementation time.
    //
    // The 'name' field is a view (char[]?) pointing to a private global string
    // containing the SHORT (unqualified) class/interface name.
    //
    // The 'bases' field is a view (TypeInfo?[]?) pointing to a K-array of RTTI
    // pointers for the direct public bases (both interfaces and classes, merged).
    //
    // The 'nested' field is a view (TypeInfo?[]?) pointing to a K-array of RTTI
    // pointers for the nested types (classes/interfaces) declared inside this type.
    //
    // The 'enclosing' field is a view (TypeInfo?) pointing to the RTTI global of
    // the enclosing aggregate, or null if this type is not nested.
    //
    // All fields except 'name' are null-initialised here and patched in
    // implementation_generator::visit_klass.
    //
    // The mangled RTTI symbol name (_KTRI...) is used as the GlobalVariable name
    // so that the linker can merge duplicates across DSOs (ODR rule).
    //
    // The unique 'typeid' of a class/interface is the address of its RTTI global.
    //
    // During the declaration pass we use null placeholders for the vtable pointers
    // (fields 0 and 1), the bases, nested and enclosing fields.  The
    // implementation pass patches them once the vtable globals and RTTI globals
    // are available.
    {
        llvm::LLVMContext& llvm_ctx = **_context;
        llvm::Type* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);

        // ── Build the RTTI struct type ──────────────────────────────────────────
        // Both interfaces and classes:
        //   { ptr __vptr__, ptr __vptr_AggregateType__, ptr __vptr_TypeInfo__,
        //     ptr name, ptr bases, ptr nested, ptr enclosing, i32 flags }
        //   (7 ptr fields + 1 i32 field = 8 fields)
        std::string rtti_struct_name = "__rtti_" + klass.get_short_name() + "__";
        llvm::Type* i32_ty = llvm::Type::getInt32Ty(llvm_ctx);
        std::vector<llvm::Type*> rtti_fields = {
            ptr_ty, ptr_ty, ptr_ty,     // __vptr__, __vptr_AggregateType__, __vptr_TypeInfo__
            ptr_ty,                      // name
            ptr_ty, ptr_ty, ptr_ty,     // bases, nested, enclosing
            i32_ty                       // flags
        };
        llvm::StructType* rtti_llvm_type = llvm::StructType::create(
            llvm_ctx, rtti_fields, rtti_struct_name);

        std::string rtti_name = mangler::mangle_rtti(klass.get_name());

        // Emit the SHORT (unqualified) class name as a K sized-array constant.
        // K's array layout is { i32 size, [N x i8] data } where 'size' includes
        // the null terminator.  The Class.name field (const char[]?) is a view
        // pointing to this struct, so code like name.size reads the i32 correctly.
        const std::string& short_name = klass.get_short_name();
        uint32_t name_len = static_cast<uint32_t>(short_name.size() + 1); // includes '\0'
        llvm::Constant* name_str_data = llvm::ConstantDataArray::getString(llvm_ctx, short_name, /*AddNull=*/true);
        llvm::StructType* name_struct_ty = llvm::StructType::get(
            llvm_ctx, {i32_ty, name_str_data->getType()}, /*isPacked=*/false);
        llvm::Constant* name_struct_init = llvm::ConstantStruct::get(
            name_struct_ty,
            {llvm::ConstantInt::get(i32_ty, name_len), name_str_data});
        auto name_gv = new llvm::GlobalVariable(
            _context->module(), name_struct_ty,
            true, llvm::GlobalValue::PrivateLinkage,
            name_struct_init, rtti_name + "_name");
        name_gv->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
        llvm::Constant* name_cstr = name_gv;

        // ── Build the RTTI global ──────────────────────────────────────────────
        // All fields except 'name' are null/zero for now; patched in implementation pass.
        llvm::Constant* null_ptr = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptr_ty));
        std::vector<llvm::Constant*> rtti_init = {
            null_ptr,   // field 0: __vptr__                → patched later
            null_ptr,   // field 1: __vptr_AggregateType__  → patched later
            null_ptr,   // field 2: __vptr_TypeInfo__       → patched later
            name_cstr,  // field 3: name                    → short name (char[]?)
            null_ptr,   // field 4: bases                   → patched later
            null_ptr,   // field 5: nested                  → patched later
            null_ptr,   // field 6: enclosing               → patched later
            llvm::ConstantInt::get(i32_ty, 0)  // field 7: flags → patched later
        };
        llvm::Constant* rtti_const = llvm::ConstantStruct::get(rtti_llvm_type, rtti_init);
        auto rtti_gv = new llvm::GlobalVariable(
            _context->module(), rtti_llvm_type,
            /*isConstant=*/false,  // mutable during compilation for patching, made constant after
            llvm::GlobalValue::ExternalLinkage,
            rtti_const, rtti_name);


        // Store on vtable_layout so implementation_generator can use it for vtable slot 0
        klass.get_vtable()->llvm_rtti_global = rtti_gv;
    }

    // Abstract classes cannot be instantiated directly; their vtable is never used at runtime.
    // Do not emit a vtable global for abstract classes.
    if (klass.is_abstract()) return;

    auto vt = klass.get_vtable();
    if (!vt->llvm_type) return;

    llvm::LLVMContext& llvm_ctx = **_context;
    llvm::Type* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);

    std::string vtable_name = mangler::mangle_vtable(klass.get_name());

    // Slot 0 points to the RTTI global (already created above)
    llvm::Constant* rtti_slot = vt->llvm_rtti_global
        ? llvm::cast<llvm::Constant>(vt->llvm_rtti_global)
        : llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptr_ty));

    std::vector<llvm::Constant*> vtable_init;
    vtable_init.push_back(rtti_slot);
    for (size_t i = 0; i < vt->slot_count(); ++i) {
        vtable_init.push_back(llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptr_ty)));
    }

    llvm::Constant* vtable_const = llvm::ConstantStruct::get(vt->llvm_type, vtable_init);
    auto vtable_gv = new llvm::GlobalVariable(
        _context->module(), vt->llvm_type,
        true, llvm::GlobalValue::ExternalLinkage,
        vtable_const, vtable_name);
    vt->llvm_global = vtable_gv;
}

// implementation_generator::visit_klass
// ---------------------------------------
// Extends aggregate implementation generation by filling in the vtable global
// variable with the actual function pointers of the class's virtual methods.
//
// Steps:
//  1. Delegate to visit_aggregate to emit function bodies for all members,
//     constructors, and destructor.
//  2. Early-exit if the class has no vtable, or if the vtable global or LLVM type
//     were not created (declaration pass must run first).
//  3. Build the final vtable initializer:
//     - Slot 0: RTTI pointer (the klass's RTTI global, set in declaration pass).
//     - Slots 1..N: for each vtable entry, look up the LLVM function corresponding
//       to the most-derived override (entry.func).  If the function was successfully
//       declared, use its pointer; otherwise fall back to null.
//  4. Replace the GlobalVariable's initializer (previously all-null from declaration
//     pass) with the now-populated constant struct, completing the vtable.
void implementation_generator::visit_klass(klass& klass) {
    visit_aggregate(klass);

    if (!klass.has_vtable()) return;

    // ── 0. Patch RTTI global with real vtable pointers, base/nested/enclosing lists, and flags ─
    // The declaration pass created the RTTI global with null/zero placeholders.
    // Now that all classes in the module have been declared, we can:
    //   a) Fill in the Class/Interface vtable pointers (fields 0, 1, 2).
    //   b) Generate K-array globals for the bases and nested fields.
    //   c) Set the enclosing field to the RTTI of the enclosing aggregate (if any).
    //   d) Set the flags field (visibility + is_static).
    //
    // Both interfaces and classes share the same layout (8 fields):
    //   { ptr __vptr__, ptr __vptr_AggregateType__, ptr __vptr_TypeInfo__,
    //     ptr name, ptr bases, ptr nested, ptr enclosing, i32 flags }
    //
    // When the vtable symbols are not available (e.g. standalone compilation
    // without libk), the RTTI global keeps null vptrs.  typeid comparison
    // (pointer equality) still works; only getName() via virtual dispatch would fail.
    if (auto* rtti_gv = klass.get_vtable()->llvm_rtti_global) {
        llvm::LLVMContext& llvm_ctx = **_context;
        llvm::Type* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);
        llvm::Type* i32_ty = llvm::Type::getInt32Ty(llvm_ctx);

        const bool is_iface = dynamic_cast<const k::model::interface*>(&klass) != nullptr;

        // ── a) Look up the correct descriptor vtable symbols ───────────────────
        // Interfaces use ::k::Interface vtables, classes use ::k::Class vtables.
        // Each has three vtable levels: primary, AggregateType secondary, TypeInfo secondary.
        std::string desc_vtable_name, desc_at_vtable_name, desc_ti_vtable_name;
        if (is_iface) {
            desc_vtable_name    = "_KTVN1k9InterfaceE";
            desc_at_vtable_name = "_KTVN1k9InterfaceE_for_AggregateType";
            desc_ti_vtable_name = "_KTVN1k9InterfaceE_for_TypeInfo";
        } else {
            desc_vtable_name    = "_KTVN1k5ClassE";
            desc_at_vtable_name = "_KTVN1k5ClassE_for_AggregateType";
            desc_ti_vtable_name = "_KTVN1k5ClassE_for_TypeInfo";
        }
        llvm::Constant* desc_vt    = _context->module().getNamedGlobal(desc_vtable_name);
        llvm::Constant* desc_at_vt = _context->module().getNamedGlobal(desc_at_vtable_name);
        llvm::Constant* desc_ti_vt = _context->module().getNamedGlobal(desc_ti_vtable_name);

        // If the primary vtable symbol is not in this module, declare it as
        // external so that user-defined classes in modules importing libk get
        // valid RTTI with working virtual dispatch on Class/Interface methods.
        // Only create the external declaration if this module (directly or
        // transitively) depends on libk — standalone modules without libk
        // keep null vptrs to avoid unresolvable link-time symbol references.
        // The secondary AggregateType and TypeInfo vtables have internal linkage
        // in libk and cannot be resolved externally — keep them null when
        // unavailable.
        if (!desc_vt) {
            // Check if this module imports k (directly or transitively)
            bool has_libk = _unit.find_import(k::name("k")) != nullptr;
            if (!has_libk) {
                for (const auto& tdep : _unit.get_transitive_kdis()) {
                    if (tdep && tdep->header.module_name == "k") { has_libk = true; break; }
                }
            }
            if (has_libk) {
                desc_vt = new llvm::GlobalVariable(
                    _context->module(), ptr_ty,
                    true, llvm::GlobalValue::ExternalLinkage,
                    nullptr, desc_vtable_name);
            }
        }

        // ── b) Generate K-array globals for base lists ─────────────────────────
        // A K-array is { i32 count, [N x ptr] data }.
        // Only direct public bases are included.
        auto make_base_array = [&](const std::vector<llvm::Constant*>& base_rtti_ptrs,
                                   const std::string& suffix) -> llvm::Constant* {
            if (base_rtti_ptrs.empty()) {
                return llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptr_ty));
            }
            uint32_t count = static_cast<uint32_t>(base_rtti_ptrs.size());
            llvm::ArrayType* arr_ty = llvm::ArrayType::get(ptr_ty, count);
            llvm::StructType* karr_ty = llvm::StructType::get(llvm_ctx, {i32_ty, arr_ty}, /*isPacked=*/false);
            llvm::Constant* arr_data = llvm::ConstantArray::get(arr_ty, base_rtti_ptrs);
            llvm::Constant* karr_init = llvm::ConstantStruct::get(karr_ty, {
                llvm::ConstantInt::get(i32_ty, count), arr_data
            });
            std::string rtti_name = mangler::mangle_rtti(klass.get_name());
            auto* gv = new llvm::GlobalVariable(
                _context->module(), karr_ty,
                true, llvm::GlobalValue::PrivateLinkage,
                karr_init, rtti_name + suffix);
            gv->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
            return gv;
        };

        // Helper: find or declare an extern RTTI global for a base aggregate.
        // Returns nullptr if the base has no vtable (and thus no RTTI global).
        auto get_base_rtti = [&](const std::shared_ptr<aggregate>& base_agg) -> llvm::Constant* {
            // Check if the base has a local vtable_layout with an RTTI global
            if (auto base_kl = std::dynamic_pointer_cast<k::model::klass>(base_agg)) {
                if (!base_kl->has_vtable()) return nullptr;  // no vtable → no RTTI
                if (base_kl->get_vtable()->llvm_rtti_global) {
                    return base_kl->get_vtable()->llvm_rtti_global;
                }
            }
            // For imported bases, check if they have a vtable (via KDI)
            if (auto imp_agg = std::dynamic_pointer_cast<imported_aggregate>(base_agg)) {
                auto* kdi = imp_agg->get_kdi_aggregate();
                if (!kdi || !kdi->vtable.has_value()) return nullptr;  // no vtable → no RTTI
            }
            // Declare an extern reference to the base's RTTI global
            std::string base_rtti_name = mangler::mangle_rtti(base_agg->get_name());
            if (auto* existing = _context->module().getNamedGlobal(base_rtti_name)) {
                return existing;
            }
            auto* extern_gv = new llvm::GlobalVariable(
                _context->module(), ptr_ty,
                true, llvm::GlobalValue::ExternalLinkage,
                nullptr, base_rtti_name);
            return extern_gv;
        };

        // Collect public base RTTI pointers (all kinds merged into one list)
        std::vector<llvm::Constant*> base_rttis;
        for (auto& bs : klass.get_bases()) {
            if (bs.vis != PUBLIC || !bs.base) continue;
            llvm::Constant* base_rtti = get_base_rtti(bs.base);
            if (!base_rtti) continue;
            base_rttis.push_back(base_rtti);
        }

        // Generate the bases array global
        llvm::Constant* null_ptr = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptr_ty));
        llvm::Constant* bases_gv = make_base_array(base_rttis, "_bases");

        // ── b2) Collect nested aggregate RTTI pointers ──────────────────────────
        // Nested aggregates are classes/interfaces defined inside this type.
        // Only classes and interfaces with vtables (and thus RTTI) are included.
        std::vector<llvm::Constant*> nested_rttis;
        for (auto& [name, nested_agg] : klass.aggregates()) {
            if (!nested_agg) continue;
            auto nested_kl = std::dynamic_pointer_cast<k::model::klass>(nested_agg);
            if (!nested_kl || !nested_kl->has_vtable()) continue;
            if (nested_kl->get_vtable()->llvm_rtti_global) {
                nested_rttis.push_back(nested_kl->get_vtable()->llvm_rtti_global);
            }
        }
        llvm::Constant* nested_gv = make_base_array(nested_rttis, "_nested");

        // ── b3) Resolve enclosing aggregate RTTI ────────────────────────────────
        // If this type is nested inside another class/interface, point to its RTTI.
        llvm::Constant* enclosing_rtti = null_ptr;
        if (klass.is_nested()) {
            auto enclosing = klass.get_enclosing_aggregate();
            if (enclosing) {
                llvm::Constant* enc_rtti = get_base_rtti(enclosing);
                if (enc_rtti) {
                    enclosing_rtti = enc_rtti;
                }
            }
        }

        // ── c) Patch the RTTI initializer ──────────────────────────────────────
        auto* rtti_type = llvm::cast<llvm::StructType>(rtti_gv->getValueType());
        auto* old_init = rtti_gv->getInitializer();
        auto* old_struct = llvm::cast<llvm::ConstantStruct>(old_init);

        llvm::Constant* vptr_field    = desc_vt    ? desc_vt    : old_struct->getOperand(0);
        llvm::Constant* at_vptr_field = desc_at_vt ? desc_at_vt : old_struct->getOperand(1);
        llvm::Constant* ti_vptr_field = desc_ti_vt ? desc_ti_vt : old_struct->getOperand(2);

        // ── d) Compute the flags bitfield ──────────────────────────────────────
        // Bits 0-1: visibility (0=PUBLIC, 1=PROTECTED, 2=PRIVATE)
        // Bit 2: is_static (1 if the type is a static nested type)
        uint32_t flags_val = 0;
        switch (klass.get_visibility()) {
            case PUBLIC:  default:   flags_val = 0; break;
            case PROTECTED:          flags_val = 1; break;
            case PRIVATE:            flags_val = 2; break;
        }
        if (klass.is_static_nested()) {
            flags_val |= 4;
        }

        std::vector<llvm::Constant*> new_rtti_init = {
            vptr_field,                        // field 0: __vptr__
            at_vptr_field,                     // field 1: __vptr_AggregateType__
            ti_vptr_field,                     // field 2: __vptr_TypeInfo__
            old_struct->getOperand(3),         // field 3: name (keep as-is)
            bases_gv ? bases_gv : null_ptr,    // field 4: bases
            nested_gv ? nested_gv : null_ptr,  // field 5: nested
            enclosing_rtti,                    // field 6: enclosing
            llvm::ConstantInt::get(i32_ty, flags_val)  // field 7: flags
        };

        rtti_gv->setInitializer(llvm::ConstantStruct::get(rtti_type, new_rtti_init));
        rtti_gv->setConstant(true);  // Fully initialized; mark as constant.
    }

    // Abstract classes have no vtable global to fill (not emitted in declaration pass).
    if (klass.is_abstract()) return;

    auto vt = klass.get_vtable();
    if (!vt->llvm_global || !vt->llvm_type) return;

    llvm::LLVMContext& llvm_ctx = **_context;
    llvm::Type* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);

    // ── 1. Fill the primary vtable ─────────────────────────────────────────────
    {
        std::vector<llvm::Constant*> vtable_init;
        // Slot 0: RTTI pointer (use the RTTI global stored on the vtable layout)
        llvm::Constant* rtti_slot = vt->llvm_rtti_global
            ? llvm::cast<llvm::Constant>(vt->llvm_rtti_global)
            : llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptr_ty));
        vtable_init.push_back(rtti_slot);
        for (auto& entry : vt->entries) {
            llvm::Function* llvm_func = _context->lookup_llvm_function(entry.func);
            if (llvm_func) {
                vtable_init.push_back(llvm_func);
            } else {
                vtable_init.push_back(llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptr_ty)));
            }
        }
        llvm::Constant* new_init = llvm::ConstantStruct::get(vt->llvm_type, vtable_init);
        vt->llvm_global->setInitializer(new_init);
    }

    // ── 2. Build secondary vtables using pre-computed model_materializer data ──────
    // model_materializer::compute_secondary_vtable_specs() has already computed
    // the correct thunk descriptors in vt->secondary_vtables (slot matching by
    // virtual signature, not by slot_index — avoids the slot_index confusion
    // between primary and secondary bases).
    // We just need to generate the LLVM function pointers (and thunks).
    for (auto& spec : vt->secondary_vtables) {
        if (!spec.base_class || !spec.base_class->has_vtable()) continue;

        auto base_vt = spec.base_class->get_vtable();
        if (!base_vt || !base_vt->llvm_type) continue;

        std::string sec_vtable_name =
            mangler::mangle_vtable(klass.get_name()) + "_for_" + spec.base_class->get_short_name();

        llvm::GlobalVariable* sec_gv =
            _context->module().getNamedGlobal(sec_vtable_name);
        if (!sec_gv) continue; // should have been created in declaration pass

        std::vector<llvm::Constant*> sec_init;
        // Slot 0 of secondary vtable: same RTTI global as the primary vtable
        // (the secondary vtable is part of the same concrete object)
        llvm::Constant* rtti_slot = vt->llvm_rtti_global
            ? llvm::cast<llvm::Constant>(vt->llvm_rtti_global)
            : llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptr_ty));
        sec_init.push_back(rtti_slot);

        for (auto& ti : spec.slot_thunks) {
            if (!ti.real_func) {
                sec_init.push_back(llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptr_ty)));
                continue;
            }

            llvm::Function* real_llvm_func = _context->lookup_llvm_function(ti.real_func);
            if (!real_llvm_func) {
                sec_init.push_back(llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptr_ty)));
                continue;
            }

            if (!ti.needs_thunk) {
                // Slot not overridden in klass, or base at offset 0: use function directly.
                sec_init.push_back(real_llvm_func);
                continue;
            }

            // Create a this-adjustment thunk: adjust Base* → Derived* then call the override.
            ptrdiff_t offset = spec.base_offset;
            std::string thunk_name = real_llvm_func->getName().str()
                + "_thunk_adj" + std::to_string(offset);
            llvm::Function* thunk = _context->module().getFunction(thunk_name);
            if (!thunk) {
                llvm::FunctionType* fn_ty = real_llvm_func->getFunctionType();
                thunk = llvm::Function::Create(fn_ty,
                    llvm::Function::InternalLinkage,
                    thunk_name,
                    _context->module());

                llvm::BasicBlock* bb = llvm::BasicBlock::Create(llvm_ctx, "entry", thunk);
                llvm::IRBuilder<> tb(bb);

                // For sret functions, the first arg is the sret pointer (pass through),
                // and the second arg is 'this' (needs adjustment).
                // For non-sret functions, the first arg is 'this'.
                bool thunk_has_sret = real_llvm_func->hasParamAttribute(0, llvm::Attribute::StructRet);
                unsigned this_arg_index = thunk_has_sret ? 1 : 0;

                llvm::Type* i64_ty = llvm::Type::getInt64Ty(llvm_ctx);
                std::vector<llvm::Value*> fwd_args;

                for (unsigned i = 0; i < thunk->arg_size(); ++i) {
                    llvm::Argument* arg = thunk->getArg(i);
                    if (i == this_arg_index) {
                        // Adjust 'this' pointer
                        llvm::Value* this_as_int = tb.CreatePtrToInt(arg, i64_ty, "this_int");
                        llvm::Value* adj_int = tb.CreateSub(
                            this_as_int,
                            llvm::ConstantInt::get(i64_ty, (uint64_t)offset),
                            "this_adj_int");
                        llvm::Value* this_adj = tb.CreateIntToPtr(adj_int, ptr_ty, "this_adj");
                        fwd_args.push_back(this_adj);
                    } else {
                        fwd_args.push_back(arg);
                    }
                }

                if (real_llvm_func->getReturnType()->isVoidTy()) {
                    tb.CreateCall(fn_ty, real_llvm_func, fwd_args);
                    tb.CreateRetVoid();
                } else {
                    llvm::Value* result = tb.CreateCall(fn_ty, real_llvm_func, fwd_args, "res");
                    tb.CreateRet(result);
                }
            }
            sec_init.push_back(thunk);
        }

        llvm::Constant* sec_struct = llvm::ConstantStruct::get(base_vt->llvm_type, sec_init);
        sec_gv->setInitializer(sec_struct);
    }

    // ── 3. Build secondary vtables for imported bases ─────────────────────────
    // For imported bases (imported_klass / imported_interface) that have a vtable,
    // the secondary vtable was created in the declaration pass but not filled here
    // (because they don't appear in vt->secondary_vtables which is limited to
    // local klass bases).  We fill them now, recursively for indirect bases too.
    // Strategy: walk all imported bases transitively (via a recursive lambda),
    // accumulating the byte offset from the start of klass. For each imported base
    // with a vtable, find the matching override in klass's vtable and emit a thunk
    // if the base subobject is at a non-zero offset.
    if (klass.get_struct_type()) {
        const llvm::DataLayout& dl = _context->module().getDataLayout();

        // Helper: normalize fq_name by stripping leading "::"
        auto normalize_fq = [](const std::string& s) -> std::string {
            if (s.size() >= 2 && s[0] == ':' && s[1] == ':') return s.substr(2);
            return s;
        };

        // Helper: extract short name (last component after "::") from a fq_name string.
        auto short_name_from_fq = [](const std::string& fq) -> std::string {
            auto pos = fq.rfind("::");
            return (pos == std::string::npos) ? fq : fq.substr(pos + 2);
        };

        // Recursive lambda that fills the secondary vtable for one imported base.
        // cur_agg_type : the LLVM struct type of the aggregate currently being examined.
        // cur_agg_ptr  : the k::model aggregate currently being examined.
        // cumulative_offset : byte offset of cur_agg inside klass.
        std::function<void(llvm::StructType*, const aggregate*, size_t)> fill_imported_secondary;
        fill_imported_secondary = [&](llvm::StructType* cur_llvm_type,
                                       const aggregate* cur_agg,
                                       size_t cumulative_offset) {
            if (!cur_agg) return;
            for (auto& bs : cur_agg->get_bases()) {
                if (!bs.base || bs.is_virtual) continue;
                auto base_imp = std::dynamic_pointer_cast<imported_aggregate>(bs.base);
                if (!base_imp || !base_imp->has_vtable()) continue;

                const auto* kdi_agg = base_imp->get_kdi_aggregate();
                if (!kdi_agg || !kdi_agg->vtable.has_value()) continue;

                std::string sec_vtable_name =
                    mangler::mangle_vtable(klass.get_name()) + "_for_" + base_imp->get_short_name();
                llvm::GlobalVariable* sec_gv = _context->module().getNamedGlobal(sec_vtable_name);
                if (!sec_gv) {
                    // Recurse into this base's bases even if no sec vtable was created for it
                    auto base_imp_llvm = llvm::dyn_cast_or_null<llvm::StructType>(
                        base_imp->get_struct_type() ? base_imp->get_struct_type()->get_llvm_type() : nullptr);
                    if (base_imp_llvm) {
                        // Compute offset of base_imp within cur_agg
                        std::string subobj_name = "__base_" + bs.sanitised_name() + "__";
                        size_t nested_offset = cumulative_offset;
                        if (cur_llvm_type) {
                            if (auto field = (cur_agg->get_struct_type()
                                    ? cur_agg->get_struct_type()->get_member(subobj_name)
                                    : std::optional<k::model::struct_type::field>{})) {
                                nested_offset += dl.getStructLayout(cur_llvm_type)->getElementOffset(field->index);
                            }
                        }
                        fill_imported_secondary(base_imp_llvm, base_imp.get(), nested_offset);
                    }
                    continue;
                }

                // Compute byte offset of this base sub-object within klass (cumulative)
                std::string subobj_field_name = "__base_" + bs.sanitised_name() + "__";
                size_t base_byte_offset = cumulative_offset;
                if (cur_llvm_type && cur_agg->get_struct_type()) {
                    auto subobj_field = cur_agg->get_struct_type()->get_member(subobj_field_name);
                    if (subobj_field) {
                        base_byte_offset += dl.getStructLayout(cur_llvm_type)
                                             ->getElementOffset((unsigned)subobj_field->index);
                    }
                }

                // Get the vtable LLVM type from the global variable
                llvm::StructType* base_vt_type =
                    llvm::dyn_cast_or_null<llvm::StructType>(sec_gv->getValueType());
                if (!base_vt_type) {
                    // Recurse into this base's own imported bases
                    auto base_imp_llvm = llvm::dyn_cast_or_null<llvm::StructType>(
                        base_imp->get_struct_type() ? base_imp->get_struct_type()->get_llvm_type() : nullptr);
                    if (base_imp_llvm)
                        fill_imported_secondary(base_imp_llvm, base_imp.get(), base_byte_offset);
                    continue;
                }

                std::vector<llvm::Constant*> sec_init;
                // Slot 0: RTTI
                llvm::Constant* rtti_slot = vt->llvm_rtti_global
                    ? llvm::cast<llvm::Constant>(vt->llvm_rtti_global)
                    : llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptr_ty));
                sec_init.push_back(rtti_slot);

                // Each slot in the base vtable maps to a method of klass (or the abstract stub).
                // We match by the introducing_func fq_name stored in the KDI slot.
                //
                // Matching challenges:
                //
                // Case 1 (direct import): IFace { f() } → Klass : IFace
                //   KDI slot introducing_func = "iFaceLib::IFace::f"
                //   Klass vtable entry intro   = imported_method(IFace::f)  ← same owner → Strategy A
                //
                // Case 2 (diamond interfaces): IBase { base_val() } ← IB : IBase { b_val() }
                //   Diamond : IA, IB.  IB vtable slot 0 introducing_func = "ibase_lib::IBase::base_val"
                //   Diamond vtable entry for base_val: intro = IBase::base_val → Strategy B (fq match)
                //
                // Case 3 (chain import): IVal { val() } ← AVal : IVal { val() override } ← ConcreteVal : AVal
                //   IVal vtable slot 0 introducing_func = "ival_lib::IVal::val"
                //   ConcreteVal vtable entry for val:  intro = imported_method(AVal::val)
                //     → fq_name = "::aval_lib::AVal::val" ≠ "ival_lib::IVal::val"  → B fails
                //   get_overrides() of ConcreteVal::val → AVal::val → none (AVal::val has no overrides
                //     stored because imported_methods don't chain through the full hierarchy) → C fails
                //   Solution: Strategy D — match by SHORT NAME of introducing_func.
                //     "IVal::val" short_name = "val" == entry.introducing_func->get_short_name() "val" ✓
                //     (This is safe in vtables: slot names are unique within a vtable, so same short_name
                //      means same method.)
                const auto& kdi_vt = kdi_agg->vtable.value();

                for (std::size_t slot_idx = 0; slot_idx < kdi_vt.slots.size(); ++slot_idx) {
                    const auto& kdi_slot = kdi_vt.slots[slot_idx];
                    const std::string kdi_intro_norm = normalize_fq(kdi_slot.introducing_func);
                    const std::string kdi_intro_short = short_name_from_fq(kdi_intro_norm);

                    // Find the override in klass's vtable entries that corresponds to this slot.
                    // Matching strategy (tried in order):
                    //  A. entry.introducing_func is an imported_method of base_imp with
                    //     the same slot_index (handles non-diamond cases).
                    //  B. entry.introducing_func fq_name matches kdi_slot.introducing_func
                    //     (handles diamond cases where the introducer is a grandparent).
                    //  C. Walk entry.func->get_overrides() chain looking for a function
                    //     whose fq_name matches kdi_slot.introducing_func.
                    //  D. entry.introducing_func short_name matches introducing_func short_name
                    //     (handles chain import: AVal:IVal where IVal's slot has intro=IVal::f
                    //      but ConcreteVal's vtable entry has intro=AVal::f).
                    llvm::Function* override_func = nullptr;
                    for (auto& entry : vt->entries) {
                        if (!entry.func) continue;

                        // Strategy A: intro is an imported_method in base_imp at the right slot
                        if (entry.introducing_func) {
                            auto intro_imp = std::dynamic_pointer_cast<imported_method>(
                                entry.introducing_func);
                            if (intro_imp && intro_imp->get_vtable_slot() == (int)slot_idx
                                && intro_imp->get_owner() &&
                                intro_imp->get_owner().get() == base_imp.get()) {
                                override_func = _context->lookup_llvm_function(entry.func);
                                break;
                            }

                            // Strategy B: match by fq_name of introducing_func (normalized)
                            if (!kdi_intro_norm.empty()
                                && normalize_fq(entry.introducing_func->get_fq_name()) == kdi_intro_norm) {
                                override_func = _context->lookup_llvm_function(entry.func);
                                break;
                            }
                        }

                        // Strategy C: walk get_overrides() chain matching by fq_name
                        if (!kdi_intro_norm.empty()) {
                            auto ov = entry.func->get_overrides();
                            while (ov) {
                                if (normalize_fq(ov->get_fq_name()) == kdi_intro_norm) {
                                    override_func = _context->lookup_llvm_function(entry.func);
                                    break;
                                }
                                // Also check by owner+slot_idx (original strategy)
                                auto ov_imp = std::dynamic_pointer_cast<imported_method>(ov);
                                if (ov_imp && ov_imp->get_vtable_slot() == (int)slot_idx
                                    && ov_imp->get_owner() &&
                                    ov_imp->get_owner().get() == base_imp.get()) {
                                    override_func = _context->lookup_llvm_function(entry.func);
                                    break;
                                }
                                ov = ov->get_overrides();
                            }
                            if (override_func) break;
                        }

                        // Strategy D: match by short name of introducing_func (chain-import fallback).
                        // This handles cases like ConcreteVal:AVal:IVal where IVal slot has
                        // intro="ival_lib::IVal::f" but ConcreteVal's entry has intro=imported(AVal::f).
                        // Since vtable slot names are unique within a vtable hierarchy, matching by
                        // short name is unambiguous.
                        if (!kdi_intro_short.empty() && entry.introducing_func) {
                            if (entry.introducing_func->get_short_name() == kdi_intro_short) {
                                override_func = _context->lookup_llvm_function(entry.func);
                                break;
                            }
                        }
                    }

                    if (!override_func) {
                        sec_init.push_back(llvm::ConstantPointerNull::get(
                            llvm::cast<llvm::PointerType>(ptr_ty)));
                        continue;
                    }

                    // If the base subobject is at a non-zero offset, we need a thunk
                    if (base_byte_offset == 0) {
                        sec_init.push_back(override_func);
                    } else {
                        std::string thunk_name = override_func->getName().str()
                            + "_thunk_adj" + std::to_string(base_byte_offset);
                        llvm::Function* thunk = _context->module().getFunction(thunk_name);
                        if (!thunk) {
                            llvm::FunctionType* fn_ty = override_func->getFunctionType();
                            thunk = llvm::Function::Create(fn_ty,
                                llvm::Function::InternalLinkage,
                                thunk_name,
                                _context->module());
                            llvm::BasicBlock* bb = llvm::BasicBlock::Create(llvm_ctx, "entry", thunk);
                            llvm::IRBuilder<> tb(bb);

                            // For sret functions, arg 0 is sret (pass through), arg 1 is this (adjust)
                            bool thunk_has_sret = override_func->hasParamAttribute(0, llvm::Attribute::StructRet);
                            unsigned this_idx = thunk_has_sret ? 1 : 0;

                            llvm::Type* i64_ty = llvm::Type::getInt64Ty(llvm_ctx);
                            std::vector<llvm::Value*> fwd_args;
                            for (unsigned i = 0; i < thunk->arg_size(); ++i) {
                                llvm::Argument* arg = thunk->getArg(i);
                                if (i == this_idx) {
                                    llvm::Value* this_as_int = tb.CreatePtrToInt(arg, i64_ty, "this_int");
                                    llvm::Value* adj_int = tb.CreateSub(
                                        this_as_int,
                                        llvm::ConstantInt::get(i64_ty, base_byte_offset),
                                        "this_adj_int");
                                    llvm::Value* this_adj = tb.CreateIntToPtr(adj_int, ptr_ty, "this_adj");
                                    fwd_args.push_back(this_adj);
                                } else {
                                    fwd_args.push_back(arg);
                                }
                            }
                            if (override_func->getReturnType()->isVoidTy()) {
                                tb.CreateCall(fn_ty, override_func, fwd_args);
                                tb.CreateRetVoid();
                            } else {
                                llvm::Value* res = tb.CreateCall(fn_ty, override_func, fwd_args, "res");
                                tb.CreateRet(res);
                            }
                        }
                        sec_init.push_back(thunk);
                    }
                }

                if (sec_init.size() == kdi_vt.slots.size() + 1) { // +1 for RTTI
                    llvm::Constant* sec_struct = llvm::ConstantStruct::get(base_vt_type, sec_init);
                    sec_gv->setInitializer(sec_struct);
                }

                // Recurse into this imported base's own imported bases (transitive chain/diamond support)
                auto base_imp_llvm = llvm::dyn_cast_or_null<llvm::StructType>(
                    base_imp->get_struct_type() ? base_imp->get_struct_type()->get_llvm_type() : nullptr);
                if (base_imp_llvm)
                    fill_imported_secondary(base_imp_llvm, base_imp.get(), base_byte_offset);
            }
        };

        // Start the recursive fill from klass's own struct type with offset 0
        auto* kl_llvm_type = llvm::dyn_cast_or_null<llvm::StructType>(
            klass.get_struct_type()->get_llvm_type());
        fill_imported_secondary(kl_llvm_type, &klass, 0);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  visit_interface — delegates to the klass visitors
// ─────────────────────────────────────────────────────────────────────────────
//
// An interface is a special klass with purely-abstract methods and no member
// variables. Its symbol resolution, type resolution, and code generation paths
// are identical to those of a klass (vtable layout, vptr injection, etc.).
// The only difference is the source-level keyword ('interface' vs 'class') and
// the fact that all methods are implicitly abstract. Everything else is handled
// by the klass machinery, so all four generator visitors simply forward to their
// respective klass counterparts.

void symbol_resolver::visit_interface(interface& iface) {
    visit_klass(iface);
}

void signature_resolver::visit_interface(interface& iface) {
    visit_aggregate(iface);
}

void type_reference_resolver::visit_interface(interface& iface) {
    visit_klass(iface);
}

void declaration_generator::visit_interface(interface& iface) {
    visit_klass(iface);
}

void implementation_generator::visit_interface(interface& iface) {
    visit_klass(iface);
}


} // namespace k::model::gen







