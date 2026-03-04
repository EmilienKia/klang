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
//      slot 0  : ptr  RTTI placeholder (nullptr for now)
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

    // Inherit slots from primary base (first direct class base with a vtable)
    std::shared_ptr<klass> primary_base;
    for (auto& bs : st.get_bases()) {
        if (auto kl = std::dynamic_pointer_cast<klass>(bs.base)) {
            if (kl->has_vtable()) {
                primary_base = kl;
                break;
            }
        }
    }

    size_t next_slot = 0;
    if (primary_base) {
        for (auto& entry : primary_base->get_vtable()->entries) {
            vtable_entry inherited;
            inherited.slot_index = entry.slot_index;
            inherited.introducing_func = entry.introducing_func;
            // Inherit the abstract-slot state: if the parent's slot is still abstract,
            // this class inherits it as abstract (until a concrete override is provided).
            inherited.func = entry.func;
            vt->entries.push_back(inherited);
            next_slot = std::max(next_slot, entry.slot_index + 1);
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
                    // Only replace the slot if the new function is concrete (non-abstract).
                    // If the new function is also abstract, keep the slot abstract but update it.
                    entry.func = func;
                    found_override = true;
                }
                break;
            }
        }

        if (!found_override) {
            if (func->is_final_func() && !func->is_abstract_func()) {
                // A final non-abstract new function gets no vtable slot
                func->set_virtual(false);
                func->set_vtable_slot(-1);
            } else {
                // Abstract functions always get a vtable slot (they must be overridable)
                func->set_virtual(true);
                func->set_vtable_slot((int)next_slot);
                vtable_entry new_entry;
                new_entry.slot_index = next_slot++;
                new_entry.introducing_func = func;
                new_entry.func = func;
                vt->entries.push_back(new_entry);

                // Check if this function overrides a method from a secondary (non-primary) base.
                // If so, record the override chain so that compute_secondary_vtable_specs can
                // identify it as an override when building the secondary vtable specs.
                for (auto& bs : st.get_bases()) {
                    if (bs.is_virtual) continue;
                    // Skip primary base (already processed above via inherited slots)
                    if (auto pk = std::dynamic_pointer_cast<klass>(bs.base)) {
                        if (pk.get() == (primary_base ? primary_base.get() : nullptr)) continue;
                        if (!pk->has_vtable()) continue;
                        for (auto& sec_entry : pk->get_vtable()->entries) {
                            if (sec_entry.introducing_func
                                && have_same_virtual_signature(*func, *sec_entry.introducing_func)) {
                                // func is an override of a secondary base method
                                if (!func->get_overrides()) {
                                    func->set_overrides(sec_entry.func ? sec_entry.func
                                                                       : sec_entry.introducing_func);
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

    return builder.CreateCall(fn_type, fn_ptr, args, result_name);
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
//     - Slot 0: opaque pointer (RTTI placeholder, currently nullptr).
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
// Extends aggregate declaration generation by emitting the vtable global variable
// for a class that has virtual functions.
//
// Steps:
//  1. Delegate to visit_aggregate to emit declarations for all member functions,
//     variables, nested aggregates, constructors, and destructor.
//  2. Early-exit if the class has no vtable or if the vtable LLVM type was not built
//     (type_reference_resolver must have run first).
//  3. Build the initial vtable initializer:
//     - Slot 0: null pointer (RTTI placeholder).
//     - Slots 1..N: null pointers for each virtual function entry (will be filled
//       in by implementation_generator::visit_klass once function bodies are emitted).
//  4. Emit a GlobalVariable named after the mangled vtable name with the null initializer
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

        std::unordered_set<const k::model::klass*> already_created;

        // Create secondary vtable globals for ALL base sub-objects reachable from
        // klass (transitively, non-virtual), including the "primary" base chain.
        // In K's layout every base sub-object starts at a non-zero offset (klass's
        // own __vptr__ is at field 0), so every base needs its own secondary vtable.
        std::function<void(const aggregate&)> collect_all_bases;
        collect_all_bases = [&](const aggregate& cur) {
            for (auto& bs : cur.get_bases()) {
                if (!bs.base || bs.is_virtual) continue;
                auto base_klass = std::dynamic_pointer_cast<k::model::klass>(bs.base);
                if (!base_klass) continue;

                if (!already_created.count(base_klass.get()) && base_klass->has_vtable()) {
                    already_created.insert(base_klass.get());
                    auto base_vt = base_klass->get_vtable();
                    if (base_vt && base_vt->llvm_type) {
                        std::string sec_vtable_name =
                            mangler::mangle_vtable(klass.get_name())
                            + "_for_" + base_klass->get_short_name();

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
                        klass.add_secondary_vtable(base_klass, sec_vt_layout);
                    }
                }
                collect_all_bases(*base_klass);
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

    // Abstract classes cannot be instantiated directly; their vtable is never used at runtime.
    // Do not emit a vtable global for abstract classes.
    if (klass.is_abstract()) return;

    auto vt = klass.get_vtable();
    if (!vt->llvm_type) return;

    llvm::LLVMContext& llvm_ctx = **_context;
    llvm::Type* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);

    std::string vtable_name = mangler::mangle_vtable(klass.get_name());

    std::vector<llvm::Constant*> vtable_init;
    vtable_init.push_back(llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptr_ty)));
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
//     - Slot 0: null pointer (RTTI placeholder, not yet implemented).
//     - Slots 1..N: for each vtable entry, look up the LLVM function corresponding
//       to the most-derived override (entry.func).  If the function was successfully
//       declared, use its pointer; otherwise fall back to null.
//  4. Replace the GlobalVariable's initializer (previously all-null from declaration
//     pass) with the now-populated constant struct, completing the vtable.
void implementation_generator::visit_klass(klass& klass) {
    visit_aggregate(klass);

    if (!klass.has_vtable()) return;

    // Abstract classes have no vtable global to fill (not emitted in declaration pass).
    if (klass.is_abstract()) return;

    auto vt = klass.get_vtable();
    if (!vt->llvm_global || !vt->llvm_type) return;

    llvm::LLVMContext& llvm_ctx = **_context;
    llvm::Type* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);

    // ── 1. Fill the primary vtable ─────────────────────────────────────────────
    {
        std::vector<llvm::Constant*> vtable_init;
        vtable_init.push_back(llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptr_ty)));
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
        sec_init.push_back(llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptr_ty)));

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

                llvm::Argument* this_arg = thunk->arg_begin();
                llvm::Type* i64_ty = llvm::Type::getInt64Ty(llvm_ctx);
                llvm::Value* this_as_int = tb.CreatePtrToInt(this_arg, i64_ty, "this_int");
                llvm::Value* adj_int = tb.CreateSub(
                    this_as_int,
                    llvm::ConstantInt::get(i64_ty, (uint64_t)offset),
                    "this_adj_int");
                llvm::Value* this_adj = tb.CreateIntToPtr(adj_int, ptr_ty, "this_adj");

                std::vector<llvm::Value*> fwd_args;
                fwd_args.push_back(this_adj);
                for (auto it = std::next(thunk->arg_begin()); it != thunk->arg_end(); ++it)
                    fwd_args.push_back(&*it);

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




