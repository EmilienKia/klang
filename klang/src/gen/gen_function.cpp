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
// gen_function.cpp — Code generation for K language functions (including constructor/destructors).
//
// This file contains all visitor method overrides and helper functions
// related to the 'function' and derivated feature:

#include "resolvers.hpp"
#include "generators.hpp"
#include "gen_helpers.hpp"

#include "../model/expressions.hpp"
#include "../model/statements.hpp"
#include "../model/imported.hpp"
#include "../model/mangler.hpp"

#include <llvm/IR/Verifier.h>
#include <llvm/Transforms/Utils/ModuleUtils.h>

#include <algorithm>
#include <queue>
#include <unordered_map>
#include <unordered_set>


namespace k::model::gen {



void emit_vptr_store(llvm::IRBuilder<>& builder,
                     klass& st,
                     llvm::Value* this_ptr,
                     std::shared_ptr<context> ctx) {
    if (!st.has_vtable()) return;

    auto vt = st.get_vtable();
    if (!vt->llvm_global) return;

    auto struct_llvm_type = st.get_struct_type()->get_llvm_type();
    if (!struct_llvm_type) return;

    llvm::LLVMContext& llvm_ctx = **ctx;
    llvm::Type* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);

    // Set the primary vptr (field 0) of the current object to point to this class's vtable.
    llvm::Value* vptr_addr = builder.CreateStructGEP(struct_llvm_type, this_ptr, 0, "__vptr_addr__");
    builder.CreateStore(vt->llvm_global, vptr_addr);

    // For classes with base sub-objects that have vtables, also update the base sub-object's
    // vptr to point to this class's vtable (K uses single virtual inheritance: one vtable per
    // complete object).  We walk all direct base sub-objects recursively.
    // Note: interfaces are class-like (they have vtables) even though is_class() returns false.
    // imported_aggregate variants are covered via is_class() overrides.
    auto is_class_like = [](const aggregate& a) {
        return a.is_class() || std::dynamic_pointer_cast<const interface>(a.shared_as<const element>()) != nullptr;
    };

    // Helper: look up the secondary vtable for `target` in `st`'s secondary vtable list.
    // Also searches the LLVM module for secondary vtable globals for imported bases
    // (named "__vtable_<ConcreteClass>_for_<Base>__" by the declaration pass).
    auto find_secondary_vtable = [&](const aggregate& target) -> llvm::GlobalVariable* {
        // First: check local secondary vtables (for local klass/interface bases)
        for (auto& [base_agg, sec_vt] : st.get_secondary_vtables()) {
            if (base_agg.get() == &target && sec_vt && sec_vt->llvm_global) {
                return sec_vt->llvm_global;
            }
        }
        // Second: check the LLVM module for secondary vtable globals created by the
        // declaration pass for imported bases.
        // Name convention: mangler::mangle_vtable(st) + "_for_" + target.get_short_name()
        std::string sec_name = mangler::mangle_vtable(st.get_name()) + "_for_" + target.get_short_name();
        return ctx->module().getNamedGlobal(sec_name);
    };

    std::function<void(aggregate& base_st, llvm::Value* base_ptr)> update_base_vptrs;
    update_base_vptrs = [&](aggregate& base_st, llvm::Value* base_ptr) {
        if (!is_class_like(base_st) || !base_st.has_vtable()) return;
        auto base_llvm_type = base_st.get_struct_type()->get_llvm_type();
        if (!base_llvm_type) return;

        // Determine which vtable to write: prefer the secondary vtable for this
        // base type as registered in the most-derived class (`st`).  Fall back to
        // the primary vtable only if no secondary spec exists.
        llvm::GlobalVariable* vtable_to_write = find_secondary_vtable(base_st);
        if (!vtable_to_write) vtable_to_write = vt->llvm_global;

        // Overwrite the base's vptr (field 0)
        llvm::Value* base_vptr_addr = builder.CreateStructGEP(base_llvm_type, base_ptr, 0,
                                                               base_st.get_short_name() + "_vptr_addr");
        builder.CreateStore(vtable_to_write, base_vptr_addr);

        // Recurse into base's embedded non-virtual sub-objects
        for (auto& bs : base_st.get_bases()) {
            if (!bs.base || !is_class_like(*bs.base) || bs.is_virtual) continue;
            std::string subobj_name = "__base_" + bs.sanitised_name() + "__";
            auto field_opt = base_st.get_struct_type()->get_member(subobj_name);
            if (!field_opt) continue;
            llvm::Value* sub_ptr = builder.CreateStructGEP(base_llvm_type, base_ptr,
                (unsigned)field_opt->index, subobj_name + "_ptr");
            update_base_vptrs(*bs.base, sub_ptr);
        }
    };

    for (auto& bs : st.get_bases()) {
        if (!bs.base || !is_class_like(*bs.base)) continue;
        std::string subobj_name = bs.is_virtual
            ? "__vbase_" + bs.sanitised_name() + "__"
            : "__base_" + bs.sanitised_name() + "__";
        auto field_opt = st.get_struct_type()->get_member(subobj_name);
        if (!field_opt) continue;
        llvm::Value* sub_ptr = builder.CreateStructGEP(struct_llvm_type, this_ptr,
            (unsigned)field_opt->index, subobj_name + "_ptr");
        update_base_vptrs(*bs.base, sub_ptr);
    }

    // ── Update vptrs of transitively-collected virtual bases ─────────────────
    {
        auto kl_ptr = dynamic_cast<klass*>(&st);
        if (kl_ptr) {
            auto vbases = st.get_all_virtual_base_structs();
            for (auto& vbase : vbases) {
                if (!is_class_like(*vbase)) continue;
                std::string vbase_field_name = "__vbase_" + vbase->get_short_name() + "__";
                auto vbase_field = st.get_struct_type()->get_member(vbase_field_name);
                if (!vbase_field) continue;  // This class is not the collector

                llvm::Value* vbase_ptr = builder.CreateStructGEP(
                    struct_llvm_type, this_ptr,
                    (unsigned)vbase_field->index,
                    "vbase_" + vbase->get_short_name() + "_for_vptr");
                update_base_vptrs(*vbase, vbase_ptr);
            }
        }
    }
}

//
// Function parameter
//
void symbol_resolver::visit_parameter(parameter& param) {
    visit_named_element(param);

    if(auto expr = param.get_init_expr()) {
        expr->accept(*this);
    }
    if(auto expr = param.get_default_expr()) {
        expr->accept(*this);
    }
}

void type_reference_resolver::visit_parameter(parameter& param) {

    if (auto var_type = param.get_type(); !type::is_resolved(var_type)) {
        // Handle unresolved_function_ref_type (function pointer/pin/link type)
        if (auto ufrt = std::dynamic_pointer_cast<unresolved_function_ref_type>(var_type)) {
            auto resolved = resolve_function_ref_type(ufrt, param);
            if (resolved && type::is_resolved(resolved)) {
                param.set_type(resolved);
            } else {
                throw_error(0x0001, std::nullopt,
                    "Cannot resolve function reference type for parameter '{}'",
                    {param.get_short_name()});
            }
        } else {
        std::shared_ptr<type> res_type = _context->resolve_type(var_type);
        if (!type::is_resolved(res_type)) {
            // Fallback for composite types wrapping an imported aggregate
            // (e.g. reference_type(unresolved("ns::Type"))).
            // Peel wrappers, resolve the inner aggregate from imports, then re-wrap.
            enum class WrapKind { Ref, Ptr, Link, Pin, Const, Owner };
            std::vector<WrapKind> wrappers;
            auto inner = var_type;
            while (inner && !std::dynamic_pointer_cast<unresolved_type>(inner)) {
                if      (type::is_reference(inner))  wrappers.push_back(WrapKind::Ref);
                else if (type::is_pointer(inner))    wrappers.push_back(WrapKind::Ptr);
                else if (type::is_link(inner))       wrappers.push_back(WrapKind::Link);
                else if (type::is_pinned(inner))     wrappers.push_back(WrapKind::Pin);
                else if (type::is_const(inner))      wrappers.push_back(WrapKind::Const);
                else if (type::is_owner(inner))      wrappers.push_back(WrapKind::Owner);
                else break;
                inner = inner->get_subtype();
            }
            if (auto unres = std::dynamic_pointer_cast<unresolved_type>(inner);
                unres && !unres->type_id().empty())
            {
                if (auto imp_agg = _unit.get_or_create_imported_aggregate(unres->type_id(), _context)) {
                    res_type = imp_agg->get_struct_type();
                    for (auto it = wrappers.rbegin(); it != wrappers.rend(); ++it) {
                        switch (*it) {
                            case WrapKind::Ref:   res_type = res_type->get_reference(); break;
                            case WrapKind::Ptr:   res_type = res_type->get_pointer();   break;
                            case WrapKind::Link:  res_type = res_type->get_link();      break;
                            case WrapKind::Pin:   res_type = res_type->get_pinned();    break;
                            case WrapKind::Const: res_type = res_type->get_const();     break;
                            case WrapKind::Owner: res_type = res_type->get_owner();     break;
                        }
                    }
                }
            }
        }
        if (!type::is_resolved(res_type)) {
            throw_error(0x0001, std::nullopt,
                "Cannot resolve type for parameter '{}': the type name is unknown",
                {param.get_short_name()});
        }
        param.set_type(res_type);
        } // end else (not unresolved_function_ref_type)
    }

    if(auto expr = param.get_init_expr()) {
        expr->accept(*this);
    }

    if(auto expr = param.get_default_expr()) {
        expr->accept(*this);
        // Adapt type of default expression to parameter type
        auto cast = adapt_type(expr, param.get_type());
        if(cast && cast != expr) {
            param.set_default_expr(cast);
        }
    }
}

//
// Function
//

void symbol_resolver::visit_function(function& fn) {
    visit_named_element(fn);

    if (fn.is_member() && !fn.is_static()) {
        fn.create_this_parameter();
    }

    for(auto param : fn.parameters()) {
        param->accept(*this);
    }

    _function_stack.push_back(fn.shared_as<function>());
    if(auto block = fn.get_block()) {
        visit_block(*block);
    }
    _function_stack.pop_back();
}

void type_reference_resolver::visit_function(function& fn) {

    if (fn.is_member() && !fn.is_static()) {
        fn.get_this_parameter()->accept(*this);
    }

    for(auto param : fn.parameters()) {
        param->accept(*this);
    }

    _function_stack.push_back(fn.shared_as<function>());
    if(auto block = fn.get_block()) {
        visit_block(*block);
    }
    _function_stack.pop_back();
}

void declaration_generator::visit_function(function &function) {
    // Deleted constructors must never be called; do not emit any LLVM declaration for them.
    if (function.is_deleted()) {
        return;
    }
    // Abstract functions have no body and must not be materialized (similar to deleted).
    if (function.is_abstract_func()) {
        return;
    }

    // Parameter types:
    std::vector<llvm::Type*> param_types;
    if (function.is_member()  && !function.is_static()) {
        // First parameter is the 'this' pointer
        auto this_param = function.get_this_parameter();
        if (!this_param || !this_param->get_type()) {
            // Cannot emit declaration without a valid 'this' parameter type.
            // This can happen for imported methods whose struct type is not yet resolved.
            return;
        }
        param_types.push_back(_context->get_llvm_type(this_param->get_type()));
    }
    for(const auto& param : function.parameters()) {
        auto ptype = _context->get_llvm_type(param->get_type());
        if (!ptype) {
            // Skip declaration if any parameter type is unresolved.
            return;
        }
        param_types.push_back(ptype);
    }

    // Return type, if any:
    llvm::Type* ret_type = nullptr;
    if(const auto& ret = function.get_return_type()) {
        ret_type = _context->get_llvm_type(ret);
    } else {
        ret_type = llvm::Type::getVoidTy(**_context);
    }

    // create the function:
    llvm::FunctionType *func_type = llvm::FunctionType::get(ret_type, param_types, false);
    llvm::Function *func = llvm::Function::Create(func_type, llvm::Function::ExternalLinkage, function.get_mangled_name(), *_context->_module);

    _context->_functions.insert({function.shared_as<k::model::function>(), func});

    // Declare content (only if there is a block — defaulted constructors may have no body yet)
    if (auto blck = function.get_block()) {
        blck->accept(*this);
    }
}

void implementation_generator::visit_function(function &function) {
    // Deleted functions have no LLVM declaration and must never be implemented.
    if (function.is_deleted()) {
        return;
    }
    // Abstract functions have no body and must not be materialized.
    if (function.is_abstract_func()) {
        return;
    }

    auto func_it = _context->_functions.find(function.shared_as<k::model::function>());
    if (func_it==_context->_functions.end()) {
        throw_internal_error(0x0001, std::nullopt,
            "Internal error: LLVM function declaration not found for '{}'; "
            "the declaration pass must be run before the implementation pass",
            {function.get_fq_name()});
    }

    llvm::Function* func = func_it->second;

    // create the function content:
    llvm::BasicBlock *block = llvm::BasicBlock::Create(**_context, "entry", func);
    _builder->SetInsertPoint(block);

    // Reset per-function state
    _retval_alloca = nullptr;
    while (!_cleanup_blocks.empty()) _cleanup_blocks.pop();
    while (!_cleanup_vars_stack.empty()) _cleanup_vars_stack.pop();
    while (!_owner_params_stack.empty()) _owner_params_stack.pop();

    // If function has a non-void return type, pre-create an alloca for the return value
    // so that destructor calls can happen before the actual ret instruction.
    if (function.has_return_type()) {
        llvm::IRBuilder<> alloca_builder(&func->getEntryBlock(), func->getEntryBlock().begin());
        _retval_alloca = alloca_builder.CreateAlloca(
            _context->get_llvm_type(function.get_return_type()), nullptr, "retval");
    }

    // Capture arguments
    auto arg_it = func->arg_begin();
    if (function.is_member() && !function.is_static()) {
        // First parameter is the 'this' pointer
        llvm::Argument *arg = &*(arg_it++);
        arg->setName("this");
        // Create dedicated local storage for "this" argument
        llvm::AllocaInst* alloca = _builder->CreateAlloca(llvm::PointerType::get(_context->llvm_context(), 0), nullptr, "this");
        _context->_function_this_variables.insert({function.shared_as<model::function>(), alloca});
        _context->_parameter_variables.insert({function.get_this_parameter(), alloca});
        // Read "this" param value and store it in dedicated local var
        _builder->CreateStore(arg, alloca);
    }
    for(const auto& param : function.parameters()) {
        // Iterate to get all explicit parameters
        llvm::Argument *arg = &*(arg_it++);
        arg->setName(param->get_short_name());
        // Create dedicated local storage for argument
        llvm::AllocaInst* alloca = _builder->CreateAlloca(_context->get_llvm_type(param->get_type()), nullptr, param->get_short_name());
        _context->_parameter_variables.insert({param, alloca});
        // Read param value and store it in dedicated local var
        _builder->CreateStore(arg, alloca);
    }

    // Collect owner-typed parameters for end-of-function cleanup (destroy + free on scope exit).
    // This enables RAII semantics for owner parameters: when a function receives an owner,
    // it takes ownership, and the object is destroyed when the function returns.
    {
        std::vector<std::shared_ptr<parameter>> owner_params;
        for (const auto& param : function.parameters()) {
            if (type::is_owner(param->get_type())) {
                owner_params.push_back(param);
            }
        }
        if (!owner_params.empty()) {
            _owner_params_stack.push(owner_params);
        }
    }

    if (auto ctor = function.shared_as<constructor>()) {
        // For constructor, start by initializing all members
        auto this_param_it = _context->_function_this_variables.find(function.shared_as<model::function>());
        auto this_param = this_param_it->second;
        auto st = ctor->get_owner();
        auto type = st->get_struct_type()->get_llvm_type();
        auto zero_init = llvm::ConstantAggregateZero::get(type);
        auto this_ptr = _builder->CreateLoad(st->get_struct_type()->get_reference()->get_llvm_type(), this_param);
        _builder->CreateStore(zero_init, this_ptr);

        // NOTE: vptr initialization (emit_vptr_store) is deferred to AFTER the block
        // (i.e., after base constructors run) so that the most-derived class's vtable
        // pointer wins. See below, after function.get_block()->accept(*this).

        // For non-static inner struct constructors: store the __parent__ parameter
        // (first explicit parameter, type Outer&) into the __parent__ field (LLVM struct field index 0).
        if (st->is_inner()) {
            auto parent_param_model = ctor->get_parameter("__parent__");
            if (parent_param_model) {
                auto parent_param_alloca_it = _context->_parameter_variables.find(
                    std::const_pointer_cast<parameter>(parent_param_model));
                if (parent_param_alloca_it != _context->_parameter_variables.end()) {
                    auto parent_param_alloca = parent_param_alloca_it->second;
                    // Load the outer struct pointer (ref = opaque ptr at LLVM level)
                    auto outer_ref_llvm_type = _context->get_llvm_type(
                        st->get_enclosing_structure()->get_struct_type()->get_reference());
                    auto parent_ptr_val = _builder->CreateLoad(outer_ref_llvm_type, parent_param_alloca, "parent_ref_val");
                    // GEP to __parent__ field (field index 0)
                    auto parent_field_ptr = _builder->CreateStructGEP(
                        _context->get_llvm_type(st->get_struct_type()),
                        this_ptr,
                        0,
                        "this_parent_field_ptr"
                    );
                    _builder->CreateStore(parent_ptr_val, parent_field_ptr);
                }
            }
        }

        // ── Generated copy constructor: emit memberwise copy at IR level ──────
        if (ctor->is_copy_constructor() && ctor->is_compiler_generated()) {
            // Load the 'other' parameter (first explicit param, type Struct&)
            auto other_param = ctor->get_parameter("other");
            if (other_param) {
                auto other_alloca_it = _context->_parameter_variables.find(
                    std::const_pointer_cast<parameter>(other_param));
                if (other_alloca_it != _context->_parameter_variables.end()) {
                    auto other_ref_type = _context->get_llvm_type(st->get_struct_type()->get_reference());
                    auto other_ptr = _builder->CreateLoad(other_ref_type, other_alloca_it->second, "other_ref");
                    auto st_llvm_type = _context->get_llvm_type(st->get_struct_type());

                    // Copy each field by field index using GEP + memcpy approach:
                    // We use a simple aggregate load/store (only valid for simple types).
                    // For structs with nested struct members, we'd need to call their copy ctors —
                    // but since we only generate this for trivially-copyable cases,
                    // a bitwise copy (memcpy semantics) is correct.
                    // Use llvm.memcpy intrinsic: copy sizeof(Struct) bytes from other to this.
                    auto& dl = _context->_module->getDataLayout();
                    uint64_t size = dl.getTypeAllocSize(st_llvm_type);
                    _builder->CreateMemCpy(
                        this_ptr, llvm::MaybeAlign(),
                        other_ptr, llvm::MaybeAlign(),
                        _builder->getInt64(size)
                    );
                }
            }
            // No user block to visit for a generated copy constructor — return immediately.
            // Add terminator and finalize.
            _builder->CreateRetVoid();
            optimize_function_dead_inst_elimination(*func);
            llvm::verifyFunction(*func);
            return;
        }

        // ── Standalone virtual base initialization (BEFORE block) ─────────────
        // For classes that directly declare "virtual B" (have __vbptr_X__ but no __vbase_X__
        // in their struct), create a stack alloca for each virtual base sub-object and
        // set the vbptr to it. This handles the standalone case (e.g. `B b;`).
        // When B is a sub-object of D, D's post-block code will overwrite these vbptrs
        // with the address of D's embedded __vbase_X__.
        // We place the allocas at the top of the entry block (before zero-init of 'this')
        // using a separate builder pointing to the entry block start.
        {
            auto vbases = st->get_all_virtual_base_structs();
            for (auto& vbase : vbases) {
                std::string vbase_name = "__vbase_" + vbase->get_short_name() + "__";
                std::string vbptr_name = "__vbptr_" + vbase->get_short_name() + "__";

                // Only handle classes that have __vbptr_X__ but NOT __vbase_X__
                auto vbptr_field = st->get_struct_type()->get_member(vbptr_name);
                auto vbase_field_opt = st->get_struct_type()->get_member(vbase_name);
                if (!vbptr_field || vbase_field_opt) continue;

                // Create an alloca for the virtual base at the function entry
                auto vbase_llvm_type = _context->get_llvm_type(vbase->get_struct_type());
                if (!vbase_llvm_type) continue;

                // Use a separate builder at the entry block for the alloca
                llvm::IRBuilder<> alloca_builder(&func->getEntryBlock(), func->getEntryBlock().begin());
                llvm::AllocaInst* vbase_alloca = alloca_builder.CreateAlloca(
                    vbase_llvm_type, nullptr, "vbase_" + vbase->get_short_name() + "_standalone");

                // Zero-initialize the alloca
                _builder->CreateStore(llvm::ConstantAggregateZero::get(vbase_llvm_type), vbase_alloca);

                // Store its address into the __vbptr_X__ field of 'this'
                auto st_llvm_type = _context->get_llvm_type(st->get_struct_type());
                llvm::Value* vbptr_addr = _builder->CreateStructGEP(
                    st_llvm_type, this_ptr, (unsigned)vbptr_field->index,
                    "vbptr_" + vbase->get_short_name() + "_standalone_addr");
                _builder->CreateStore(vbase_alloca, vbptr_addr);

                // Call the virtual base's default constructor on the alloca
                // (this handles A() : x(10) {} etc.)
                if (auto vbase_ctor_list = &vbase->constructors(); !vbase_ctor_list->empty()) {
                    // Find the default (0-arg) constructor
                    for (auto& vbase_ctor : *vbase_ctor_list) {
                        if (vbase_ctor->get_parameter_size() == 0 && !vbase_ctor->is_copy_constructor()) {
                            auto vbase_ctor_it = _context->_functions.find(vbase_ctor->shared_as<k::model::function>());
                            if (vbase_ctor_it != _context->_functions.end()) {
                                _builder->CreateCall(vbase_ctor_it->second, {vbase_alloca});
                            }
                            break;
                        }
                    }
                }

                // Store the alloca in context for potential use by constructor_invocation_expression
                _context->_vbase_standalone_allocas[st->shared_as<aggregate>()][vbase->get_short_name()] = vbase_alloca;
            }
        }
    }

    // Produce content
    function.get_block()->accept(*this);

    // ── Class vptr initialization (after base ctors, so our vtable wins) ─────
    // This must be done AFTER the block (which calls base constructors that also
    // set their own vptrs). By setting the vptr last, we ensure the most-derived
    // class's vtable pointer is what remains in the object.
    if (auto ctor = function.shared_as<constructor>()) {
        auto st = ctor->get_owner();
        if (auto kl = std::dynamic_pointer_cast<klass>(st)) {
            if (kl->has_vtable()) {
            auto this_param_it = _context->_function_this_variables.find(function.shared_as<model::function>());
            if (this_param_it != _context->_function_this_variables.end()) {
                auto this_ptr = _builder->CreateLoad(
                    kl->get_struct_type()->get_reference()->get_llvm_type(),
                    this_param_it->second, "this_for_vptr");
                emit_vptr_store(*_builder, *kl, this_ptr, _context);
            }
            }
        }

        // ── Virtual base pointer initialization ───────────────────────────────
        // For each transitively-declared virtual base, find the __vbase_X__ sub-object
        // in the most-derived class (this class) and write its address into all
        // __vbptr_X__ fields found in all non-virtual base sub-objects.
        if (st) {
            auto vbases = st->get_all_virtual_base_structs();
            if (!vbases.empty()) {
                auto this_param_it2 = _context->_function_this_variables.find(function.shared_as<model::function>());
                if (this_param_it2 != _context->_function_this_variables.end()) {
                    auto this_ptr = _builder->CreateLoad(
                        st->get_struct_type()->get_reference()->get_llvm_type(),
                        this_param_it2->second, "this_for_vbptr");
                    auto st_llvm_type = _context->get_llvm_type(st->get_struct_type());

                    for (auto& vbase : vbases) {
                        std::string vbase_field_name = "__vbase_" + vbase->get_short_name() + "__";
                        auto vbase_field = st->get_struct_type()->get_member(vbase_field_name);
                        if (!vbase_field) continue;

                        // Get address of the virtual base sub-object
                        llvm::Value* vbase_ptr = _builder->CreateStructGEP(
                            st_llvm_type, this_ptr, (unsigned)vbase_field->index,
                            "vbase_" + vbase->get_short_name() + "_addr");

                        std::string vbptr_field_name = "__vbptr_" + vbase->get_short_name() + "__";

                        // Walk all base sub-objects (both embedded struct-bases and vbase class-bases)
                        // and set their __vbptr_X__ to point to vbase_ptr.
                        std::function<void(aggregate&, llvm::Value*)> set_vbptrs;
                        set_vbptrs = [&](aggregate& base_st, llvm::Value* base_sub_ptr) {
                            auto base_llvm_type = _context->get_llvm_type(base_st.get_struct_type());
                            if (!base_llvm_type) return;

                            // Check if this base_st has a direct __vbptr_X__ field
                            auto vbptr_field = base_st.get_struct_type()->get_member(vbptr_field_name);
                            if (vbptr_field) {
                                // Store the vbase address into this vbptr field
                                llvm::Value* vbptr_addr = _builder->CreateStructGEP(
                                    base_llvm_type, base_sub_ptr,
                                    (unsigned)vbptr_field->index,
                                    "vbptr_" + vbase->get_short_name() + "_addr");
                                _builder->CreateStore(vbase_ptr, vbptr_addr);
                            }

                            // Recurse into embedded (non-virtual) sub-bases
                            for (auto& bs : base_st.get_bases()) {
                                if (!bs.base) continue;
                                if (!bs.is_virtual) {
                                    // Non-virtual base: embedded as __base_X__
                                    std::string sub_name = "__base_" + bs.sanitised_name() + "__";
                                    auto sub_field = base_st.get_struct_type()->get_member(sub_name);
                                    if (!sub_field) continue;
                                    llvm::Value* sub_ptr = _builder->CreateStructGEP(
                                        base_llvm_type, base_sub_ptr,
                                        (unsigned)sub_field->index,
                                        "sub_" + bs.sanitised_name() + "_ptr");
                                    set_vbptrs(*bs.base, sub_ptr);
                                } else {
                                    // Virtual base: stored as __vbase_X__ in the containing object
                                    std::string vbase_sub_name = "__vbase_" + bs.sanitised_name() + "__";
                                    auto vbase_sub_field = base_st.get_struct_type()->get_member(vbase_sub_name);
                                    if (vbase_sub_field) {
                                        llvm::Value* vbase_sub_ptr = _builder->CreateStructGEP(
                                            base_llvm_type, base_sub_ptr,
                                            (unsigned)vbase_sub_field->index,
                                            "vbase_" + bs.sanitised_name() + "_sub_ptr");
                                        set_vbptrs(*bs.base, vbase_sub_ptr);
                                    }
                                }
                            }
                        };

                        // Walk direct bases of 'st' to find and update __vbptr_X__
                        for (auto& bs : st->get_bases()) {
                            if (!bs.base) continue;
                            if (!bs.is_virtual) {
                                // Non-virtual base: embedded as __base_X__
                                std::string sub_name = "__base_" + bs.sanitised_name() + "__";
                                auto sub_field = st->get_struct_type()->get_member(sub_name);
                                if (!sub_field) continue;
                                llvm::Value* sub_ptr = _builder->CreateStructGEP(
                                    st_llvm_type, this_ptr, (unsigned)sub_field->index,
                                    "sub_" + bs.sanitised_name() + "_ptr");
                                set_vbptrs(*bs.base, sub_ptr);
                            } else {
                                // Virtual base: stored as __vbase_X__ in st
                                std::string vbase_sub_name = "__vbase_" + bs.sanitised_name() + "__";
                                auto vbase_sub_field = st->get_struct_type()->get_member(vbase_sub_name);
                                if (vbase_sub_field) {
                                    llvm::Value* vbase_sub_ptr = _builder->CreateStructGEP(
                                        st_llvm_type, this_ptr, (unsigned)vbase_sub_field->index,
                                        "vbase_" + bs.sanitised_name() + "_sub_ptr");
                                    set_vbptrs(*bs.base, vbase_sub_ptr);
                                }
                            }
                        }
                        // Also check if 'st' itself directly holds vbptr (direct virtual base)
                        auto direct_vbptr = st->get_struct_type()->get_member(vbptr_field_name);
                        if (direct_vbptr) {
                            llvm::Value* vbptr_addr = _builder->CreateStructGEP(
                                st_llvm_type, this_ptr, (unsigned)direct_vbptr->index,
                                "direct_vbptr_" + vbase->get_short_name() + "_addr");
                            _builder->CreateStore(vbase_ptr, vbptr_addr);
                        }
                    }
                }
            }
        }
    }

    // ── For destructors: call base destructors in reverse base-declaration order ──
    // (own members are handled by visit_block cleanup; bases are handled here)
    if (auto dtor = function.shared_as<destructor>()) {
        auto st = dtor->get_owner();
        if (st && (st->has_bases() || st->has_virtual_bases())) {
            auto this_param_it = _context->_function_this_variables.find(function.shared_as<model::function>());
            if (this_param_it != _context->_function_this_variables.end()) {
                auto this_param = this_param_it->second;
                auto this_ptr = _builder->CreateLoad(
                    st->get_struct_type()->get_reference()->get_llvm_type(),
                    this_param, "this_ptr");

                const auto& bases = st->get_bases();
                // Iterate in reverse base-declaration order
                // Skip virtual bases (handled separately via vbases below)
                for (auto bit = bases.rbegin(); bit != bases.rend(); ++bit) {
                    auto& bs = *bit;
                    if (!bs.base) continue;
                    if (bs.is_virtual) continue; // virtual bases handled below
                    auto base_dtor = bs.base->get_destructor();
                    if (!base_dtor) continue;
                    auto dtor_it = _context->_functions.find(base_dtor->shared_as<k::model::function>());
                    if (dtor_it == _context->_functions.end()) continue;

                    // GEP to base subobject field
                    std::string subobj_name = "__base_" + bs.sanitised_name() + "__";
                    auto base_field = st->get_struct_type()->get_member(subobj_name);
                    if (!base_field) continue;

                    auto base_ptr = _builder->CreateStructGEP(
                        _context->get_llvm_type(st->get_struct_type()),
                        this_ptr,
                        (unsigned)base_field->index,
                        "base_" + bs.sanitised_name() + "_ptr"
                    );
                    _builder->CreateCall(dtor_it->second, {base_ptr});
                }

                // ── Virtual base sub-object destructors (most-derived class only) ──
                // Only the class that owns __vbase_X__ should call X's destructor.
                // Collect virtual bases transitively
                auto vbases = st->get_all_virtual_base_structs();
                for (auto it = vbases.rbegin(); it != vbases.rend(); ++it) {
                    auto& vbase = *it;
                    std::string vbase_field_name = "__vbase_" + vbase->get_short_name() + "__";
                    auto vbase_field = st->get_struct_type()->get_member(vbase_field_name);
                    if (!vbase_field) continue; // not in this class's layout (not the collector)

                    auto vbase_dtor = vbase->get_destructor();
                    if (!vbase_dtor) continue;
                    auto dtor_it = _context->_functions.find(vbase_dtor->shared_as<k::model::function>());
                    if (dtor_it == _context->_functions.end()) continue;

                    auto vbase_ptr = _builder->CreateStructGEP(
                        _context->get_llvm_type(st->get_struct_type()),
                        this_ptr,
                        (unsigned)vbase_field->index,
                        "vbase_" + vbase->get_short_name() + "_ptr"
                    );
                    _builder->CreateCall(dtor_it->second, {vbase_ptr});
                }
                // NOTE: Classes with only __vbptr_X__ (not __vbase_X__) do NOT call virtual
                // base destructors — that is the responsibility of the most-derived class (the
                // one owning __vbase_X__). This matches C++ sub-object destructor semantics.
            }
        }
    }

    // Force adding a terminator as last instruction guard (will be eliminated if unreachable).
    // Before that, emit cleanup for owner-typed parameters (fall-through exit path).
    // For functions with an explicit return statement, this code is unreachable and will be
    // removed by optimize_function_dead_inst_elimination.
    if (!_owner_params_stack.empty()) {
        auto params = _owner_params_stack.top();
        _owner_params_stack.pop();
        for (auto it = params.rbegin(); it != params.rend(); ++it) {
            auto& param = *it;
            auto own_type = std::dynamic_pointer_cast<owner_type>(param->get_type());
            if (!own_type) continue;
            auto param_it = _context->_parameter_variables.find(param);
            if (param_it == _context->_parameter_variables.end()) continue;
            llvm::AllocaInst* alloca = param_it->second;
            emit_owner_cleanup_if_nonnull(_builder.get(), get_module(), _context->_functions,
                alloca, own_type->get_owned_type(), "exit_param");
        }
    }

    if (function.has_return_type()) {
        llvm::Type* ret_type = _context->get_llvm_type(function.get_return_type());
        _builder->CreateRet(llvm::UndefValue::get(ret_type));
    } else {
        _builder->CreateRetVoid();
    }

    // Pre-optimize function
    optimize_function_dead_inst_elimination(*func);

    // Verify function
    llvm::verifyFunction(*func);
}

void implementation_generator::optimize_function_dead_inst_elimination(llvm::Function& func) {
    for(auto& block : func) {
        llvm::BasicBlock *bb;
        // Find first terminator instruction
        auto term = std::find_if(block.begin(), block.end(), [](auto& inst)->bool{return inst.isTerminator();});
        if(term!=block.end()) {
            if(++term!=block.end()) {
                block.erase(term, block.end());
            }
        }
    }
}

//
// Constructor
//

void symbol_resolver::visit_constructor(constructor& ctor) {
    // Deleted constructors have no body: skip body resolution and member-init injection.
    // The constructor already appears in the overload set because it was added to
    // _constructors by model_builder. Naming and 'this' setup are not needed here
    // because get_best_matching_constructor only looks at parameter count / types,
    // and symbol_resolver has not yet resolved the parameter types anyway.
    if (ctor.is_deleted()) {
        return;
    }

    // For non-static inner structs, inject the implicit 'parent' parameter
    // as the first explicit parameter (position 0, after the implicit 'this').
    // Type is Outer& (reference), consistent with 'this' parameter semantics.
    auto st = ctor.get_owner();
    if (st && st->is_inner()) {
        auto outer_st = st->get_enclosing_structure();
        auto outer_ref_type = outer_st->get_struct_type()->get_reference();
        // Only inject if not already present (avoid double-injection if revisited)
        if (!ctor.get_parameter("__parent__")) {
            ctor.insert_parameter("__parent__", outer_ref_type, 0);
        }
    }

    // ── Mark base-class member_inits and detect copy constructor ──────────────
    if (st) {
        // Build set of base names (direct + transitively-declared virtual bases)
        std::unordered_map<std::string, std::shared_ptr<aggregate>> base_by_name;
        for (auto& bs : st->get_bases()) {
            if (bs.base) base_by_name[bs.raw_name] = bs.base;
        }
        // Also include transitively-collected virtual bases (e.g., A in D : B,C where B,C : virtual A)
        for (auto& vbase : st->get_all_virtual_base_structs()) {
            base_by_name.emplace(vbase->get_short_name(), vbase);
        }

        // Mark each explicit mem-init as base-init or member-init
        for (auto& mi : const_cast<std::vector<constructor::member_init_spec>&>(ctor.member_inits())) {
            auto it = base_by_name.find(mi.member_name);
            if (it != base_by_name.end()) {
                mi.is_base_init = true;
                mi.base_struct = it->second;
            }
        }

        // Detect copy constructor: single non-this param whose type is a ref to this struct
        if (ctor.get_parameter_size() == 1 && !ctor.is_compiler_generated()) {
            auto p0 = ctor.get_parameter(0);
            if (p0) {
                auto ptype = p0->get_type();
                if (auto ref = std::dynamic_pointer_cast<reference_type>(ptype)) {
                    if (auto sub_st = std::dynamic_pointer_cast<struct_type>(ref->get_referenced_type())) {
                        if (sub_st->get_struct() && sub_st->get_struct().get() == st.get()) {
                            ctor.set_copy_constructor(true);
                        }
                    }
                }
            }
        }
    }

    // Before resolving the block, inject expression_statements for each explicit member
    // initializer into the beginning of the constructor block. This ensures that when
    // visit_function → visit_block visits the block, the symbol expressions inside the
    // mem-init args have a proper parent in the element hierarchy and can resolve
    // parameter references correctly.
    // Injected in struct member declaration order (as in C++), not in the list order.
    //
    // For base inits: we'll inject a constructor_invocation_expression targeting the
    // synthetic __base_X__ subobject field.

    auto blck = ctor.get_block();
    // Note: 'st' already declared above for inner-struct check
    if (blck && st) {
        // Track actual number of base ctor stmts injected in Step 1
        // (used by Step 1b and Step 2 to find the correct insert position)
        size_t insert_idx1 = 0;

        // ── Step 1: inject base constructor calls (in base declaration order) ──
        if (st->has_bases()) {
            // Build lookup: base raw_name → member_init_spec for this constructor
            std::unordered_map<std::string, const constructor::member_init_spec*> base_init_by_name;
            for (auto& mi : ctor.member_inits()) {
                if (mi.is_base_init) {
                    base_init_by_name[mi.member_name] = &mi;
                }
            }

            for (auto& bs : st->get_bases()) {
                if (!bs.base) continue;
                if (bs.is_virtual) {
                    // Virtual base: sub-object is __vbase_X__ (only constructed in most-derived class)
                    std::string vbase_name = "__vbase_" + bs.sanitised_name() + "__";
                    auto vbase_var_it = st->variables().find(vbase_name);
                    if (vbase_var_it == st->variables().end()) continue; // not the most-derived class
                    auto vbase_var = std::dynamic_pointer_cast<member_variable_definition>(vbase_var_it->second);
                    if (!vbase_var) continue;

                    std::vector<std::shared_ptr<expression>> args;
                    auto it = base_init_by_name.find(bs.raw_name);
                    if (it != base_init_by_name.end()) {
                        for (auto& arg : it->second->args) args.push_back(arg->clone());
                    }
                    auto init_expr = constructor_invocation_expression::make_shared(vbase_var, args);
                    auto stmt = std::make_shared<expression_statement>(blck);
                    stmt->set_expression(init_expr);
                    auto fresh_pos = blck->begin();
                    std::advance(fresh_pos, insert_idx1);
                    blck->insert_statement(fresh_pos, stmt);
                    ++insert_idx1;
                } else {
                    // Non-virtual base: embedded as __base_X__
                    std::string subobj_name = "__base_" + bs.sanitised_name() + "__";
                    auto subobj_var_it = st->variables().find(subobj_name);
                    if (subobj_var_it == st->variables().end()) continue;
                    auto subobj_var = std::dynamic_pointer_cast<member_variable_definition>(subobj_var_it->second);
                    if (!subobj_var) continue;

                    std::vector<std::shared_ptr<expression>> args;
                    auto it = base_init_by_name.find(bs.raw_name);
                    if (it != base_init_by_name.end()) {
                        for (auto& arg : it->second->args) args.push_back(arg->clone());
                    }
                    auto init_expr = constructor_invocation_expression::make_shared(subobj_var, args);
                    auto stmt = std::make_shared<expression_statement>(blck);
                    stmt->set_expression(init_expr);
                    auto fresh_pos = blck->begin();
                    std::advance(fresh_pos, insert_idx1);
                    blck->insert_statement(fresh_pos, stmt);
                    ++insert_idx1;
                }
            }
        }

        // ── Step 1b: inject transitively-collected virtual base constructor calls ──
        // For classes like D that collect virtual bases through non-virtual bases (e.g., D has
        // B,C as non-virtual bases where B,C each declare virtual A), D gets __vbase_A__ in its
        // layout. D must construct A. This is NOT handled by the direct-bases loop above (A is
        // not in D's direct base list). We inject these after all direct-base ctor calls.
        {
            auto vbases = st->get_all_virtual_base_structs();
            if (!vbases.empty()) {
                // Build lookup: virtual base short_name → member_init_spec for this constructor
                std::unordered_map<std::string, const constructor::member_init_spec*> vbase_init_by_name;
                for (auto& mi : ctor.member_inits()) {
                    if (mi.is_base_init) {
                        vbase_init_by_name[mi.member_name] = &mi;
                    }
                }

                // Insert position: after all ACTUALLY-injected direct-base ctor calls.
                // We use insert_idx1 which was incremented for each actual injection in Step 1.
                size_t insert_idx = insert_idx1;

                for (auto& vbase : vbases) {
                    std::string vbase_name = "__vbase_" + vbase->get_short_name() + "__";
                    // Only inject if this class actually owns the __vbase_X__ field
                    auto vbase_var_it = st->variables().find(vbase_name);
                    if (vbase_var_it == st->variables().end()) continue;
                    auto vbase_var = std::dynamic_pointer_cast<member_variable_definition>(vbase_var_it->second);
                    if (!vbase_var) continue;

                    std::vector<std::shared_ptr<expression>> args;
                    auto it = vbase_init_by_name.find(vbase->get_short_name());
                    if (it != vbase_init_by_name.end()) {
                        for (auto& arg : it->second->args) {
                            args.push_back(arg->clone());
                        }
                    }
                    auto init_expr = constructor_invocation_expression::make_shared(vbase_var, args);
                    auto stmt = std::make_shared<expression_statement>(blck);
                    stmt->set_expression(init_expr);
                    // Use index-based position to get a fresh iterator (avoids invalidation)
                    auto fresh_pos = blck->begin();
                    std::advance(fresh_pos, insert_idx);
                    blck->insert_statement(fresh_pos, stmt);
                    ++insert_idx;
                }
            }
        }

        // ── Step 2: inject member initializers (in member declaration order) ──
        if (!ctor.member_inits().empty()) {
            // Build a lookup map from member name to mem_init_spec
            std::unordered_map<std::string, const constructor::member_init_spec*> init_by_name;
            for (auto& mi : ctor.member_inits()) {
                if (!mi.is_base_init) init_by_name[mi.member_name] = &mi;
            }

            // Insert after the base-init calls (direct bases + collected virtual bases from step 1b)
            // Use insert_idx1 (actual injected count from Step 1) plus vbase count from Step 1b.
            size_t base_count = insert_idx1;
            // Add count of step 1b injected vbase stmts
            for (auto& vbase : st->get_all_virtual_base_structs()) {
                std::string vn = "__vbase_" + vbase->get_short_name() + "__";
                if (st->variables().count(vn)) ++base_count;
            }
            size_t insert_idx_step2 = base_count;

            for (auto& var_entry : st->variables()) {
                if (auto var = std::dynamic_pointer_cast<member_variable_definition>(var_entry.second)) {
                    // Skip synthetic fields
                    if (var->get_short_name() == "__parent__") continue;
                    if (var->get_short_name().rfind("__base_", 0) == 0) continue;
                    if (var->get_short_name().rfind("__vbptr_", 0) == 0) continue;
                    if (var->get_short_name().rfind("__vbase_", 0) == 0) continue;
                    if (var->get_short_name().rfind("__vptr", 0) == 0) continue;

                    auto it = init_by_name.find(var->get_short_name());
                    if (it == init_by_name.end()) continue;
                    const auto& mi = *it->second;

                    // Clone the args so each constructor gets its own independent copy
                    std::vector<std::shared_ptr<expression>> args;
                    args.reserve(mi.args.size());
                    for (auto& arg : mi.args) {
                        args.push_back(arg->clone());
                    }
                    auto init_expr = constructor_invocation_expression::make_shared(var, args);
                    auto stmt = std::make_shared<expression_statement>(blck);
                    stmt->set_expression(init_expr);
                    auto fresh_pos = blck->begin();
                    std::advance(fresh_pos, insert_idx_step2);
                    blck->insert_statement(fresh_pos, stmt);
                    ++insert_idx_step2;
                }
            }
        }
    }

    // For non-static inner struct constructors, the __parent__ field is stored
    // directly at IR level in implementation_generator::visit_function (constructor prologue).
    // No model-level injection needed here.

    visit_function(ctor);
}

void type_reference_resolver::visit_constructor(constructor& ctor) {
    auto st = ctor.get_owner();
    if (!st) {
        throw_internal_error(0x0001, std::nullopt,
            "Internal error: constructor has no owner structure; "
            "every constructor must belong to a struct — this indicates a compiler bug");
    }

    // Deleted constructors have no body and must never be called (enforced at resolution time).
    // Ensure _this_param exists (needed by check_constructor_visibility and type queries)
    // and resolve the formal parameter types so overload resolution can match argument types.
    if (ctor.is_deleted()) {
        if (ctor.is_member() && !ctor.is_static() && !ctor.get_this_parameter()) {
            ctor.create_this_parameter();
        }
        for (auto param : ctor.parameters()) {
            param->accept(*this);
        }
        return;
    }

    auto blck = ctor.get_block();

    // For compiler-generated copy constructor: do NOT inject model-level statements.
    // The memberwise copy will be emitted directly at IR level in implementation_generator::visit_function.
    if (ctor.is_copy_constructor() && ctor.is_compiler_generated()) {
        visit_function(ctor);
        return;
    }

    // Defaulted (-> default ;) constructors: they are compiler-generated and have no
    // user-provided body, but they still need to have member default-value initialisations
    // injected (same as any compiler-generated or user-written constructor without a
    // mem-initializer-list). Fall through to the standard injection logic below.
    // (do NOT return early here)

    // Note : the statements for explicit member_inits and base inits were already injected by
    // symbol_resolver::visit_constructor (in struct member declaration order).
    // Here we insert fallback initialization statements for members NOT listed in the
    // mem-initializer-list, interleaved in declaration order.

    // Build the set of member names and base names with an explicit initializer
    std::unordered_set<std::string> explicit_init_names;
    for (auto& mi : ctor.member_inits()) {
        explicit_init_names.insert(mi.member_name);
    }

    // Walk member declaration order and insert fallback init for each unlisted member
    // at the correct position (interleaved with the already-injected explicit ones).
    // We maintain insert_pos which advances past each already-injected or newly-injected stmt.
    // Use an index counter to avoid iterator invalidation from vector reallocation.
    size_t insert_idx2 = 0;

    // Skip already-injected base init stmts
    for (auto& bs : st->get_bases()) {
        if (!bs.base) continue;
        if (bs.is_virtual) {
            std::string vbase_name = "__vbase_" + bs.sanitised_name() + "__";
            if (st->variables().count(vbase_name)) ++insert_idx2;
        } else {
            std::string sub_name = "__base_" + bs.sanitised_name() + "__";
            if (st->variables().count(sub_name)) ++insert_idx2;
        }
    }
    // Also skip vbase stmts injected in step 1b for transitively-collected virtual bases
    {
        auto vbases = st->get_all_virtual_base_structs();
        for (auto& vbase : vbases) {
            std::string vbase_name = "__vbase_" + vbase->get_short_name() + "__";
            if (st->variables().count(vbase_name)) {
                bool already_counted = false;
                for (auto& bs : st->get_bases()) {
                    if (bs.base && bs.is_virtual && bs.raw_name == vbase->get_short_name()) {
                        already_counted = true; break;
                    }
                }
                if (!already_counted) ++insert_idx2;
            }
        }
    }

    for (auto& var_entry : st->variables()) {
        if (auto var = std::dynamic_pointer_cast<member_variable_definition>(var_entry.second)) {
            // Skip __parent__ field — stored directly at IR level in constructor prologue
            if (var->get_short_name() == "__parent__") continue;
            // Skip base subobject fields — already handled above
            if (var->get_short_name().rfind("__base_", 0) == 0) continue;
            // Skip virtual base pointer fields — set at IR level
            if (var->get_short_name().rfind("__vbptr_", 0) == 0) continue;
            // Skip virtual base sub-object fields — handled in base injection loop above
            if (var->get_short_name().rfind("__vbase_", 0) == 0) continue;
            // Skip vptr fields — set at IR level
            if (var->get_short_name().rfind("__vptr", 0) == 0) continue;

            if (explicit_init_names.count(var->get_short_name()) > 0) {
                // This member has an explicit initializer already in the block: skip past it
                ++insert_idx2;
            } else {
                // Not in the explicit list: use its own init_expr (if any)
                auto init_expr = var->get_init_expr();
                if (init_expr) {
                    // Clone so each constructor gets its own independent copy.
                    auto stmt = std::make_shared<expression_statement>(blck);
                    stmt->set_expression(init_expr->clone());
                    auto fresh_pos = blck->begin();
                    std::advance(fresh_pos, insert_idx2);
                    blck->insert_statement(fresh_pos, stmt);
                    ++insert_idx2;
                }
                // If no init_expr, zero-initialization covers it (done at IR level).
            }
        }
    }

    visit_function(ctor);
}

//
// Destructor
//

void type_reference_resolver::visit_destructor(destructor& dtor) {
    auto st = dtor.get_owner();
    if (!st) {
        throw_internal_error(0x0002, std::nullopt,
            "Internal error: destructor has no owner structure; "
            "every destructor must belong to a struct — this indicates a compiler bug");
    }

    auto blck = dtor.get_block();
    // Insert calls to members' destructors at the END of the destructor block, in reverse declaration order.
    // Collect member variables that have a destructor (own members, not base subobjs)
    std::vector<std::shared_ptr<member_variable_definition>> dtor_members;
    for (auto& var_entry : st->variables()) {
        if (auto var = std::dynamic_pointer_cast<member_variable_definition>(var_entry.second)) {
            if (var->get_short_name() == "__parent__") continue;
            if (var->get_short_name().rfind("__base_", 0) == 0) continue;
            if (var->get_short_name().rfind("__vbptr_", 0) == 0) continue;
            if (var->get_short_name().rfind("__vbase_", 0) == 0) continue;
            if (var->get_short_name().rfind("__vptr", 0) == 0) continue;
            if (auto st_type = std::dynamic_pointer_cast<struct_type>(var->get_type())) {
                if (st_type->get_struct() && st_type->get_struct()->get_destructor()) {
                    dtor_members.push_back(var);
                }
            }
        }
    }
    // Insert destructor calls for own members in reverse order at end of block
    for (auto it = dtor_members.rbegin(); it != dtor_members.rend(); ++it) {
        (void)*it; // placeholder – IR generation handles this
    }

    // Insert base destructor calls in reverse base-declaration order
    // (bases are destroyed after own members, in reverse order of construction)
    // Placeholder: actual IR generation happens in implementation_generator.
    // We just record the intent; implementation_generator::visit_function handles it.

    visit_function(dtor);
}

//
// Static constructor
// Registers the static constructor with the global initializer function.
//

void symbol_resolver::visit_static_constructor(static_constructor& sctor) {
    visit_function(sctor);

    // Resolve each dependency name declared in the mem-init list to a concrete model element.
    // Resolution is: name → structure (requires static ctor) OR global_variable_definition.
    // The scope walk starts from the owning structure and climbs to the root namespace.
    // This is the ONLY place where static_dep_spec names are resolved; the model itself
    // holds no resolution logic.
    auto owner = sctor.get_owner();
    if (!owner) return;

    auto start = std::dynamic_pointer_cast<element>(owner);

    for (auto& dep : sctor.mutable_member_inits()) {
        // Try to find a structure with this name in scope
        if (auto st = scope_lookup::lookup_structure(start, dep.name)) {
            dep.resolved = st;
            continue;
        }
        // Try to find a global variable with this name in scope
        if (auto var = scope_lookup::lookup_variable(start, dep.name)) {
            if (auto gv = std::dynamic_pointer_cast<global_variable_definition>(var)) {
                dep.resolved = gv;
                continue;
            }
        }
        // Not found — report error
        throw_error(0x0006, std::nullopt,
            "In static constructor '{}': dependency '{}' in the mem-init list "
            "does not refer to any known struct or global variable in scope",
            {sctor.get_fq_name(), dep.name});
    }
}

void type_reference_resolver::visit_static_constructor(static_constructor& sctor) {
    visit_function(sctor);

    // Register this static constructor with the unit's global constructor function.
    // The actual call order is determined later by init_order_resolver.
    sctor.ancestor<unit>()->get_global_constructor_function().add_static_constructor(sctor.shared_as<static_constructor>());
}

//
// Static destructor
// No direct registration needed: init_order_resolver derives the destruction order
// as the exact reverse of the construction order.
//

void symbol_resolver::visit_static_destructor(static_destructor& sdtor) {
    visit_function(sdtor);
}

void type_reference_resolver::visit_static_destructor(static_destructor& sdtor) {
    visit_function(sdtor);
    // Registration in the global destructor function is handled by init_order_resolver.
}

//
// Global constructor function
// This generate the unique global constructor function (if needed) and register it to llvm.global_ctors
// Note: Global constructor is processed at the end of the unit (but before global destructor)
//
void type_reference_resolver::visit_global_constructor_function(global_constructor_function& func) {
    const auto& items = func.get_ordered_items();
    if (items.empty()) return;

    auto blck = func.get_block();
    // Only global variable initializations need a model-level statement (for type resolution);
    // static constructor calls are emitted directly at IR level.
    for (auto& item : items) {
        if (auto gv = std::get_if<std::shared_ptr<global_variable_definition>>(&item)) {
            auto init_expr = (*gv)->get_init_expr();
            if (init_expr) {
                auto stmt = std::make_shared<expression_statement>(blck);
                stmt->set_expression(init_expr);
                blck->append_statement(stmt);
            }
        }
    }
    visit_function(func);
}

void implementation_generator::visit_global_constructor_function(global_constructor_function& func) {
    const auto& items = func.get_ordered_items();
    if (items.empty()) return;

    // Generate the function body (global variable constructor-invocation statements are in the block).
    visit_function(func);

    auto it_func = _context->_functions.find(func.shared_as<function>());
    if (it_func == _context->_functions.end()) {
        throw_internal_error(0x0002, std::nullopt,
            "Internal error: global constructor function not found in LLVM function table; "
            "the declaration pass may not have run");
    }

    // Emit static constructor calls in order, interleaved with global-variable inits.
    // Global variable init expressions are already emitted by visit_function (from the block).
    // We need to insert static_constructor calls at the right position in the IR.
    // Strategy: build an ordered list of static ctor calls only, then insert them
    // just before the ret terminator (after all variable inits).
    // NOTE: variable inits are already in the block (emitted by visit_function).
    //       Static ctors are emitted in their correct order relative to each other
    //       and relative to variable inits by placing them just before the final ret.
    //       The unified ordering ensures that all dependencies are respected.
    llvm::Function* llvm_func = it_func->second;
    llvm::BasicBlock& last_bb = llvm_func->back();
    llvm::IRBuilder<> ctor_builder(&last_bb, last_bb.getTerminator()->getIterator());

    // Walk ordered items: for each static_constructor, emit a call just before the terminator.
    // Global variable inits are already emitted by visit_function in order from the block.
    // To achieve interleaved ordering (static ctors and var inits mixed), we collect
    // all variable-init instructions from the block and reorder them with the static calls.
    // Simpler approach: since visit_function already emitted var-init calls in the block
    // in the order appended to the block (which matches items order for gv), we only
    // need to insert static ctor calls. But they must appear BETWEEN variable inits if needed.
    // Full interleaving: rebuild the entire function IR in items order.
    // For correctness: emit all var-init calls from the block already (done), then
    // append static ctor calls at the end of the entry block before ret.
    // This is correct IF the unified ordering places all static ctors BEFORE all global vars
    // that depend on them — which init_order_resolver guarantees.
    // The IR order within the function body therefore is:
    //   [var-init calls in block order] then [static ctor calls before ret]
    // Because init_order_resolver ensures the ordering is correct, and the block was built
    // with vars in dependency order, static ctors will logically precede their dependent vars.
    // BUT: to achieve FULL correct interleaving at IR level, we use a different approach:
    // We collect static ctor calls from items in order and insert them AFTER their position
    // in the block by using move-instruction sequencing.
    // For simplicity and correctness (since ordering is resolved), we emit static ctor calls
    // in the order they appear in items, just before the terminator.

    for (auto& item : items) {
        if (auto sc = std::get_if<std::shared_ptr<static_constructor>>(&item)) {
            auto sctor_it = _context->_functions.find(*sc);
            if (sctor_it == _context->_functions.end()) continue;
            ctor_builder.CreateCall(sctor_it->second, {});
        }
    }

    // Register the global constructor function with the runtime
    llvm::appendToGlobalCtors(get_module(), llvm_func, 65535);
}


//
// Global destructor function
// This generates the unique global destructor function (if needed) and registers it to llvm.global_dtors
// Destruction order is the exact REVERSE of construction order.
//

// type_reference_resolver::visit_global_destructor_function
// -----------------------------------------------------------
// Prepares the global destructor function for IR generation.
// Unlike the global constructor function, the destructor body contains no
// model-level statements (all calls are emitted directly at IR level).
//
// Steps:
//  1. Early-exit if there are no ordered items and no standalone static dtors.
//  2. Delegate to visit_function so the function's metadata (name, return type, etc.)
//     is resolved.  The body block is empty at model level; actual calls are
//     inserted directly in implementation_generator::visit_global_destructor_function.
void type_reference_resolver::visit_global_destructor_function(global_destructor_function& func) {
    const auto& items = func.get_ordered_items();
    const auto& standalone = func.get_standalone_static_dtors();
    if (items.empty() && standalone.empty()) return;
    visit_function(func);
}

// implementation_generator::visit_global_destructor_function
// ------------------------------------------------------------
// Emits the IR body of the global destructor function and registers it with
// the LLVM global_dtors table.
// Items are processed in the order stored in the model (reverse-construction order,
// set by init_order_resolver).
//
// Steps:
//  1. Early-exit if there are no items and no standalone static destructors.
//  2. Check whether there is any real work: at least one standalone static dtor,
//     a static constructor whose struct has a static destructor, or a global
//     variable whose type has a struct destructor.  Early-exit if nothing to do.
//  3. Create a new void() llvm::Function with ExternalLinkage and the mangled name,
//     register it in the context, and create the entry BasicBlock.
//  4. First: emit calls to standalone static destructors (structs that have a
//     static destructor but no static constructor).
//  5. Then: walk the ordered items (reverse-construction order):
//     - For a static_constructor item: look up the owning struct's static destructor
//       and emit a direct call to it.
//     - For a global_variable_definition item: if the variable's type is a struct
//       with a destructor, GEP to the global variable and emit a destructor call.
//  6. Emit a ret-void terminator.
//  7. Verify the function with llvm::verifyFunction.
//  8. Register the function with the LLVM global_dtors table at priority 65535
//     via llvm::appendToGlobalDtors, ensuring it runs at program shutdown.
void implementation_generator::visit_global_destructor_function(global_destructor_function& func) {
    // The destructor function holds items in REVERSE construction order
    // (set by init_order_resolver). We iterate forward through them.
    const auto& items = func.get_ordered_items();
    const auto& standalone_sdtors = func.get_standalone_static_dtors();
    if (items.empty() && standalone_sdtors.empty()) return;

    // Check if there is anything to do (struct dtors or static dtors)
    bool has_work = !standalone_sdtors.empty();
    if (!has_work) {
        for (auto& item : items) {
            if (auto sc = std::get_if<std::shared_ptr<static_constructor>>(&item)) {
                // Corresponding static destructor
                auto owner = (*sc)->get_owner();
                if (owner && owner->get_static_destructor()) { has_work = true; break; }
            } else if (auto gv = std::get_if<std::shared_ptr<global_variable_definition>>(&item)) {
                if (auto st_type = std::dynamic_pointer_cast<struct_type>((*gv)->get_type())) {
                    if (st_type->get_struct() && st_type->get_struct()->get_destructor()) { has_work = true; break; }
                }
            }
        }
    }
    if (!has_work) return;

    // Generate a void() function for the global destructor
    llvm::FunctionType* func_type = llvm::FunctionType::get(llvm::Type::getVoidTy(**_context), false);
    llvm::Function* llvm_func = llvm::Function::Create(func_type, llvm::Function::ExternalLinkage,
                                                        func.get_mangled_name(), *_context->_module);
    _context->_functions.insert({func.shared_as<function>(), llvm_func});

    llvm::BasicBlock* entry = llvm::BasicBlock::Create(**_context, "entry", llvm_func);
    llvm::IRBuilder<> dtor_builder(entry);

    // First: emit standalone static destructors (structs with ~S() but no S()).
    for (auto& sdtor : standalone_sdtors) {
        auto sdtor_it = _context->_functions.find(sdtor->shared_as<function>());
        if (sdtor_it == _context->_functions.end()) continue;
        dtor_builder.CreateCall(sdtor_it->second, {});
    }

    // Then: emit finalization in the order stored in items (reverse-construction order).
    for (auto& item : items) {
        if (auto sc = std::get_if<std::shared_ptr<static_constructor>>(&item)) {
            auto owner = (*sc)->get_owner();
            if (!owner) continue;
            auto sdtor = owner->get_static_destructor();
            if (!sdtor) continue;
            auto sdtor_it = _context->_functions.find(sdtor->shared_as<function>());
            if (sdtor_it == _context->_functions.end()) continue;
            dtor_builder.CreateCall(sdtor_it->second, {});
        } else if (auto gv = std::get_if<std::shared_ptr<global_variable_definition>>(&item)) {
            auto st_type = std::dynamic_pointer_cast<struct_type>((*gv)->get_type());
            if (!st_type) continue;
            auto st = st_type->get_struct();
            if (!st || !st->get_destructor()) continue;
            auto var_it = _context->_global_vars.find(*gv);
            if (var_it == _context->_global_vars.end()) continue;
            llvm::GlobalVariable* global_var = var_it->second;
            auto dtor_it = _context->_functions.find(st->get_destructor()->shared_as<function>());
            if (dtor_it == _context->_functions.end()) continue;
            dtor_builder.CreateCall(dtor_it->second, {global_var});
        }
    }

    dtor_builder.CreateRetVoid();
    llvm::verifyFunction(*llvm_func);
    llvm::appendToGlobalDtors(get_module(), llvm_func, 65535);
}

//
// Global main function
// This generate the main entry point proxy code
//

// type_reference_resolver::visit_global_main_function
// -----------------------------------------------------
// Synthesizes the C-ABI "main" entry-point proxy that wraps the user-defined
// 'main' function.  This proxy is what the linker and OS call at startup.
//
// Steps:
//  1. Validate that the user's 'main' function takes no parameters (parameters
//     are not yet supported); throw a compile error if parameters are present.
//  2. Resolve the 'int' primitive type from the context.
//  3. Configure the proxy function metadata:
//     - Assign the well-known name "main" (C-ABI entry point).
//     - Set return type to 'int'.
//     - Add 'argc' (int) and 'argv' (unsigned char**) parameters.
//  4. Build the proxy body:
//     - If the user's 'main' returns a value:
//       a. Create a function_invocation_expression calling the user function.
//       b. Adapt/cast the return value to 'int' (implicit cast if needed).
//       c. Wrap in a return_statement.
//     - If the user's 'main' returns void:
//       a. Create an expression_statement for the call (result discarded).
//       b. Append it to the block.
//       c. Append a return_statement returning the integer literal 0.
//  5. The block is now complete; visit_function will be called later in the
//     normal visitor flow to resolve types within the synthesized body.
void type_reference_resolver::visit_global_main_function(global_main_function& main_func) {

    std::vector<std::shared_ptr<expression>> args;

    // Look at the compatible prototypes
    // TODO Add a better method prototype compatibility checking/searching
    if (main_func.get_real_func().has_parameter()) {
        throw_error(0x000C, std::nullopt,
            "'main' function does not support parameters yet; "
            "declare it as 'func main() : int' or 'func main() : void'");
    }

    auto int_type = _context->from_type(primitive_type::INT);

    main_func.assign_name(name(true, "main"));
    main_func.set_return_type(int_type);
    main_func.append_parameter("argc", int_type);
    main_func.append_parameter("argv", _context->from_type(primitive_type::UNSIGNED_CHAR)->get_pointer()->get_pointer());

    auto main_block = main_func.get_block();
    auto ret_stmt = std::make_shared<model::return_statement>(main_block);

    std::shared_ptr<expression> invoke = function_invocation_expression::make_shared(main_func.get_real_func().shared_as<function>(), args);

    // Annotate with DIRECT dispatch_info — the real 'main' function is always
    // a direct call (not virtual).  This synthetic node bypasses the normal
    // type_reference_resolver path, so we set the annotation manually.
    {
        virtual_dispatch_info di;
        di.kind = virtual_dispatch_info::dispatch_kind::DIRECT;
        std::dynamic_pointer_cast<function_invocation_expression>(invoke)->set_dispatch_info(std::move(di));
    }

    if (main_func.get_real_func().has_return_type()) {
        // Cast invocation result to int
        auto cast = adapt_type(invoke, int_type);
        if(!cast) {
            throw_error(0x000D, std::nullopt,
                "'main' function return type '{}' cannot be implicitly cast to 'int'; "
                "the return type must be 'int', 'void', or a type castable to 'int'",
                {main_func.get_real_func().get_return_type() ? main_func.get_real_func().get_return_type()->to_string() : "?"});
        } else if(cast != invoke) {
            // Casted, assign casted expression as return expr.
            invoke = cast;
        } else {
            // Compatible type, no need to cast.
        }
        // Return casted result
        ret_stmt->set_expression(invoke);
        main_func.get_block()->append_statement(ret_stmt);
    } else {
        // Create statement for this invocation
        auto call_stmt = std::make_shared<model::expression_statement>(main_block);
        call_stmt->set_expression(invoke);
        main_func.get_block()->append_statement(call_stmt);
        // Create return statement with returning 0
        ret_stmt->set_expression(value_expression::from_value(0));
        main_func.get_block()->append_statement(ret_stmt);
    }
}

} // namespace k::model::gen
