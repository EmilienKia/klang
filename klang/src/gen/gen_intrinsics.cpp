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
// gen_intrinsics.cpp — Compiler intrinsic dispatch and code generation.
//
// Functions annotated with @annotations::Intrinsic("name") bypass normal codegen
// and instead generate specialised IR determined by the intrinsic name.
//
#include "gen_intrinsics.hpp"
#include "resolvers.hpp"
#include "generators.hpp"
#include "gen_helpers.hpp"
#include "../model/expressions.hpp"
#include "../model/statements.hpp"
#include "../model/imported.hpp"
#include "../model/mangler.hpp"
#include "../parse/ast.hpp"
#include "../errors.hpp"

#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>

namespace k::model::gen {

// ─────────────────────────────────────────────────────────────────────────────
// get_intrinsic_name — extract @Intrinsic("...") from a function's annotations
// ─────────────────────────────────────────────────────────────────────────────

std::optional<std::string> get_intrinsic_name(const function& fn) {
    for (auto& ann_inst : fn.get_annotations()) {
        // Check by resolved type if available
        bool is_intrinsic = false;
        if (ann_inst.resolved_type) {
            std::string fq = ann_inst.resolved_type->get_fq_name();
            is_intrinsic = (fq == "k::annotations::Intrinsic" || fq == "::k::annotations::Intrinsic");
        }
        // Fallback: check by raw name (before resolution)
        if (!is_intrinsic) {
            is_intrinsic = (ann_inst.raw_name == "annotations::Intrinsic"
                         || ann_inst.raw_name == "Intrinsic"
                         || ann_inst.raw_name == "k::annotations::Intrinsic"
                         || ann_inst.raw_name == "::k::annotations::Intrinsic");
        }
        if (!is_intrinsic) continue;

        // Try to extract a string parameter from the annotation AST node.
        auto extract_string = [](const std::shared_ptr<k::parse::ast::expression>& expr)
                -> std::optional<std::string> {
            if (!expr) return std::nullopt;
            if (auto lit = std::dynamic_pointer_cast<k::parse::ast::literal_expr>(expr)) {
                if (auto* s = lit->literal.get_if<lex::string>()) {
                    return std::get<std::string>(s->value());
                }
            }
            return std::nullopt;
        };

        // Try to get explicit intrinsic name from annotation arguments
        if (ann_inst.ast_node && ann_inst.ast_node->has_parens) {
            auto& args = ann_inst.ast_node->args;
            if (!args.empty()) {
                if (auto name = extract_string(args[0])) {
                    return name;
                }
            }
        } else if (ann_inst.ast_node && ann_inst.ast_node->brace_init
                   && ann_inst.ast_node->brace_init->is_designated) {
            for (auto& elem : ann_inst.ast_node->brace_init->elements) {
                auto des = std::dynamic_pointer_cast<k::parse::ast::designated_init_element>(elem);
                if (!des) continue;
                std::string member{des->member_name.content};
                if (member == "name") {
                    if (auto name = extract_string(des->value)) {
                        return name;
                    }
                }
            }
        }

        // Fallback: derive intrinsic name from function context.
        // This handles cases where the annotation field is not a string
        // (e.g. integer discriminator in test code).
        if (auto ctor = std::dynamic_pointer_cast<constructor>(
                const_cast<function&>(fn).shared_as<function>())) {
            // Determine owner struct prefix for intrinsic name
            auto owner = fn.get_owner();
            std::string owner_name = owner ? owner->get_short_name() : "";
            if (owner_name.find("MultiSlot") != std::string::npos) {
                return std::string("MultiSlot::constructor");
            }
            return std::string("UniSlot::constructor");
        }
        if (auto dtor = std::dynamic_pointer_cast<destructor>(
                const_cast<function&>(fn).shared_as<function>())) {
            auto owner = fn.get_owner();
            std::string owner_name = owner ? owner->get_short_name() : "";
            if (owner_name.find("MultiSlot") != std::string::npos) {
                return std::string("MultiSlot::destructor");
            }
            return std::string("UniSlot::destructor");
        }
        // Check both the short name and the template base name (for instantiated member templates)
        std::string check_name = fn.get_short_name();
        if (fn.has_tpl_args() && !fn.get_tpl_base_name().empty()) {
            check_name = fn.get_tpl_base_name();
        }
        // Determine if this is a MultiSlot or UniSlot method
        auto owner = fn.get_owner();
        std::string owner_name = owner ? owner->get_short_name() : "";
        bool is_multislot = owner_name.find("MultiSlot") != std::string::npos;
        std::string prefix = is_multislot ? "MultiSlot" : "UniSlot";
        if (check_name == "construct") {
            return prefix + "::construct";
        }
        if (check_name == "destruct") {
            return prefix + "::destruct";
        }
        if (check_name == "get") {
            return prefix + "::get";
        }
        if (check_name == "allocate") {
            return prefix + "::allocate";
        }
        if (check_name == "reallocate") {
            return prefix + "::reallocate";
        }
        if (check_name == "deallocate") {
            return prefix + "::deallocate";
        }
        if (check_name == "resize") {
            return prefix + "::resize";
        }

        // Unknown intrinsic — return empty name to signal "is intrinsic but no body needed"
        return std::string("unknown");
    }
    return std::nullopt;
}

// ─────────────────────────────────────────────────────────────────────────────
// UniSlot::construct — placement constructor on _slot member
// ─────────────────────────────────────────────────────────────────────────────

void implementation_generator::emit_intrinsic_unislot_construct(function& function, llvm::Function* func) {
    // Get owning struct (the instantiated UniSlot<T>)
    auto owner_st = function.get_owner();
    if (!owner_st) {
        _builder->CreateRetVoid();
        llvm::verifyFunction(*func);
        return;
    }

    // Get 'this' pointer
    auto this_param_it = _context->_function_this_variables.find(function.shared_as<model::function>());
    if (this_param_it == _context->_function_this_variables.end()) {
        _builder->CreateRetVoid();
        llvm::verifyFunction(*func);
        return;
    }
    auto this_alloca = this_param_it->second;
    auto this_ptr = _builder->CreateLoad(
        owner_st->get_struct_type()->get_reference()->get_llvm_type(),
        this_alloca, "this_ptr");

    // GEP to _slot member
    auto slot_field = owner_st->get_struct_type()->get_member("_slot");
    if (!slot_field) {
        _builder->CreateRetVoid();
        llvm::verifyFunction(*func);
        return;
    }

    auto slot_ptr = _builder->CreateStructGEP(
        _context->get_llvm_type(owner_st->get_struct_type()),
        this_ptr, (unsigned)slot_field->index, "slot_ptr");

    // Get the type of _slot and check if it's a struct type with a constructor
    auto slot_var = owner_st->variables().find("_slot");
    if (slot_var == owner_st->variables().end()) {
        _builder->CreateRetVoid();
        llvm::verifyFunction(*func);
        return;
    }
    auto slot_type = slot_var->second->get_type();
    auto slot_st_type = std::dynamic_pointer_cast<struct_type>(slot_type);
    if (!slot_st_type || !slot_st_type->get_struct()) {
        // Primitive type — nothing to construct
        _builder->CreateRetVoid();
        llvm::verifyFunction(*func);
        return;
    }

    auto target_struct = slot_st_type->get_struct();

    // Collect call-site argument values from the function's explicit parameters
    std::vector<llvm::Value*> ctor_args = {slot_ptr};
    std::vector<std::shared_ptr<type>> arg_types;
    for (const auto& param : function.parameters()) {
        auto param_it = _context->_parameter_variables.find(param);
        if (param_it != _context->_parameter_variables.end()) {
            auto* param_ty = _context->get_llvm_type(param->get_type());
            auto loaded = _builder->CreateLoad(param_ty, param_it->second, param->get_short_name());
            ctor_args.push_back(loaded);
            arg_types.push_back(param->get_type());
        }
    }

    // Find the best matching constructor for the argument types
    std::shared_ptr<constructor> best_ctor;
    if (arg_types.empty()) {
        // Zero-arg: find default constructor
        for (auto& ctor : target_struct->constructors()) {
            if (ctor->is_deleted()) continue;
            if (ctor->get_parameter_size() == 0) {
                best_ctor = ctor;
                break;
            }
        }
    } else {
        // Match by parameter count and types (simplified matching)
        for (auto& ctor : target_struct->constructors()) {
            if (ctor->is_deleted()) continue;
            if (ctor->get_parameter_size() != arg_types.size()) continue;
            // Check type compatibility (simple exact-match check)
            bool match = true;
            for (size_t i = 0; i < arg_types.size(); ++i) {
                auto param_type = ctor->parameters()[i]->get_type();
                if (param_type && arg_types[i] && param_type != arg_types[i]) {
                    // Allow if both are the same primitive type
                    auto p1 = std::dynamic_pointer_cast<primitive_type>(param_type);
                    auto p2 = std::dynamic_pointer_cast<primitive_type>(arg_types[i]);
                    if (!p1 || !p2 || p1->get_type() != p2->get_type()) {
                        match = false;
                        break;
                    }
                }
            }
            if (match) {
                best_ctor = ctor;
                break;
            }
        }
    }

    if (best_ctor) {
        auto ctor_it = _context->_functions.find(best_ctor->shared_as<model::function>());
        if (ctor_it != _context->_functions.end()) {
            _builder->CreateCall(ctor_it->second, ctor_args);
        }
    }

    _builder->CreateRetVoid();
    llvm::verifyFunction(*func);
}

// ─────────────────────────────────────────────────────────────────────────────
// UniSlot::destruct — destructor call on _slot member without deallocation
// ─────────────────────────────────────────────────────────────────────────────

void implementation_generator::emit_intrinsic_unislot_destruct(function& function, llvm::Function* func) {
    // Get owning struct (the instantiated UniSlot<T>)
    auto owner_st = function.get_owner();
    if (!owner_st) {
        _builder->CreateRetVoid();
        llvm::verifyFunction(*func);
        return;
    }

    // Get 'this' pointer
    auto this_param_it = _context->_function_this_variables.find(function.shared_as<model::function>());
    if (this_param_it == _context->_function_this_variables.end()) {
        _builder->CreateRetVoid();
        llvm::verifyFunction(*func);
        return;
    }
    auto this_alloca = this_param_it->second;
    auto this_ptr = _builder->CreateLoad(
        owner_st->get_struct_type()->get_reference()->get_llvm_type(),
        this_alloca, "this_ptr");

    // GEP to _slot member
    auto slot_field = owner_st->get_struct_type()->get_member("_slot");
    if (!slot_field) {
        _builder->CreateRetVoid();
        llvm::verifyFunction(*func);
        return;
    }

    auto slot_ptr = _builder->CreateStructGEP(
        _context->get_llvm_type(owner_st->get_struct_type()),
        this_ptr, (unsigned)slot_field->index, "slot_ptr");

    // Get the type of _slot and check if it's a struct type with a destructor
    auto slot_var = owner_st->variables().find("_slot");
    if (slot_var == owner_st->variables().end()) {
        _builder->CreateRetVoid();
        llvm::verifyFunction(*func);
        return;
    }
    auto slot_type = slot_var->second->get_type();
    auto slot_st_type = std::dynamic_pointer_cast<struct_type>(slot_type);
    if (!slot_st_type || !slot_st_type->get_struct()) {
        // Primitive type — nothing to destruct
        _builder->CreateRetVoid();
        llvm::verifyFunction(*func);
        return;
    }

    auto target_struct = slot_st_type->get_struct();
    auto target_dtor = target_struct->get_destructor();

    if (target_dtor) {
        auto dtor_it = _context->_functions.find(target_dtor->shared_as<model::function>());
        if (dtor_it != _context->_functions.end()) {
            _builder->CreateCall(dtor_it->second, {slot_ptr});
        }
    }

    _builder->CreateRetVoid();
    llvm::verifyFunction(*func);
}

// ─────────────────────────────────────────────────────────────────────────────
// MultiSlot::constructor — zero-init _data and _capacity
// ─────────────────────────────────────────────────────────────────────────────

void implementation_generator::emit_intrinsic_multislot_constructor(function& function, llvm::Function* func) {
    // The constructor pre-block already does memset(this, 0, sizeof(struct)),
    // which sets _data = null and _capacity = 0. Nothing extra needed.
    _builder->CreateRetVoid();
    llvm::verifyFunction(*func);
}

// ─────────────────────────────────────────────────────────────────────────────
// MultiSlot::destructor — free(_data) if non-null
// ─────────────────────────────────────────────────────────────────────────────

void implementation_generator::emit_intrinsic_multislot_destructor(function& function, llvm::Function* func) {
    auto owner_st = function.get_owner();
    if (!owner_st) { _builder->CreateRetVoid(); llvm::verifyFunction(*func); return; }

    auto this_param_it = _context->_function_this_variables.find(function.shared_as<model::function>());
    if (this_param_it == _context->_function_this_variables.end()) { _builder->CreateRetVoid(); llvm::verifyFunction(*func); return; }
    auto this_ptr = _builder->CreateLoad(
        owner_st->get_struct_type()->get_reference()->get_llvm_type(),
        this_param_it->second, "this_ptr");
    if (!this_ptr) { _builder->CreateRetVoid(); llvm::verifyFunction(*func); return; }

    // GEP to _data field
    auto data_field = owner_st->get_struct_type()->get_member("_data");
    if (!data_field) { _builder->CreateRetVoid(); llvm::verifyFunction(*func); return; }

    auto data_ptr_ptr = _builder->CreateStructGEP(
        _context->get_llvm_type(owner_st->get_struct_type()),
        this_ptr, (unsigned)data_field->index, "data_ptr_ptr");

    auto ptr_type = llvm::PointerType::get(_context->llvm_context(), 0);
    auto data_ptr = _builder->CreateLoad(ptr_type, data_ptr_ptr, "data_ptr");

    // if (data_ptr != null) free(data_ptr)
    auto null_ptr = llvm::ConstantPointerNull::get(ptr_type);
    auto is_not_null = _builder->CreateICmpNE(data_ptr, null_ptr, "is_not_null");

    auto free_bb = llvm::BasicBlock::Create(_context->llvm_context(), "free_bb", func);
    auto end_bb = llvm::BasicBlock::Create(_context->llvm_context(), "end_bb", func);
    _builder->CreateCondBr(is_not_null, free_bb, end_bb);

    _builder->SetInsertPoint(free_bb);
    // Call free
    auto free_func = _context->module().getOrInsertFunction("free",
        llvm::FunctionType::get(llvm::Type::getVoidTy(_context->llvm_context()), {ptr_type}, false));
    _builder->CreateCall(free_func, {data_ptr});
    _builder->CreateBr(end_bb);

    _builder->SetInsertPoint(end_bb);
    _builder->CreateRetVoid();
    llvm::verifyFunction(*func);
}

// ─────────────────────────────────────────────────────────────────────────────
// MultiSlot::allocate — malloc(capacity * sizeof(T)), store to _data/_capacity
// ─────────────────────────────────────────────────────────────────────────────

void implementation_generator::emit_intrinsic_multislot_allocate(function& function, llvm::Function* func) {
    auto owner_st = function.get_owner();
    if (!owner_st) { _builder->CreateRetVoid(); llvm::verifyFunction(*func); return; }

    auto this_param_it = _context->_function_this_variables.find(function.shared_as<model::function>());
    if (this_param_it == _context->_function_this_variables.end()) { _builder->CreateRetVoid(); llvm::verifyFunction(*func); return; }
    auto this_ptr = _builder->CreateLoad(
        owner_st->get_struct_type()->get_reference()->get_llvm_type(),
        this_param_it->second, "this_ptr");
    if (!this_ptr) { _builder->CreateRetVoid(); llvm::verifyFunction(*func); return; }

    // Get the capacity parameter
    auto& params = function.parameters();
    if (params.empty()) { _builder->CreateRetVoid(); llvm::verifyFunction(*func); return; }
    auto param_it = _context->_parameter_variables.find(params[0]);
    if (param_it == _context->_parameter_variables.end()) { _builder->CreateRetVoid(); llvm::verifyFunction(*func); return; }
    auto capacity_val = _builder->CreateLoad(
        llvm::Type::getInt32Ty(_context->llvm_context()), param_it->second, "capacity");

    // Get T's LLVM type from _data member type
    auto data_field = owner_st->get_struct_type()->get_member("_data");
    auto cap_field = owner_st->get_struct_type()->get_member("_capacity");
    if (!data_field || !cap_field) { _builder->CreateRetVoid(); llvm::verifyFunction(*func); return; }

    // Get element type T from the pointer type of _data
    auto data_var = owner_st->variables().find("_data");
    if (data_var == owner_st->variables().end()) { _builder->CreateRetVoid(); llvm::verifyFunction(*func); return; }
    auto data_type = data_var->second->get_type();
    auto ptr_t = std::dynamic_pointer_cast<pointer_type>(data_type);
    if (!ptr_t) { _builder->CreateRetVoid(); llvm::verifyFunction(*func); return; }
    auto elem_type = ptr_t->get_subtype();
    auto* llvm_elem_type = _context->get_llvm_type(elem_type);

    // sizeof(T)
    auto& dl = _context->module().getDataLayout();
    uint64_t elem_size = dl.getTypeAllocSize(llvm_elem_type);

    // total_size = capacity * sizeof(T)
    auto elem_size_val = llvm::ConstantInt::get(llvm::Type::getInt64Ty(_context->llvm_context()), elem_size);
    auto cap_i64 = _builder->CreateZExt(capacity_val, llvm::Type::getInt64Ty(_context->llvm_context()), "cap_i64");
    auto total_size = _builder->CreateMul(cap_i64, elem_size_val, "total_size");

    // Call malloc
    auto ptr_type = llvm::PointerType::get(_context->llvm_context(), 0);
    auto malloc_func = _context->module().getOrInsertFunction("malloc",
        llvm::FunctionType::get(ptr_type, {llvm::Type::getInt64Ty(_context->llvm_context())}, false));
    auto new_ptr = _builder->CreateCall(malloc_func, {total_size}, "new_data");

    // Store _data
    auto data_ptr_ptr = _builder->CreateStructGEP(
        _context->get_llvm_type(owner_st->get_struct_type()),
        this_ptr, (unsigned)data_field->index, "data_ptr_ptr");
    _builder->CreateStore(new_ptr, data_ptr_ptr);

    // Store _capacity
    auto cap_ptr = _builder->CreateStructGEP(
        _context->get_llvm_type(owner_st->get_struct_type()),
        this_ptr, (unsigned)cap_field->index, "cap_ptr");
    _builder->CreateStore(capacity_val, cap_ptr);

    _builder->CreateRetVoid();
    llvm::verifyFunction(*func);
}

// ─────────────────────────────────────────────────────────────────────────────
// MultiSlot::reallocate — realloc(_data, newCapacity * sizeof(T))
// ─────────────────────────────────────────────────────────────────────────────

void implementation_generator::emit_intrinsic_multislot_reallocate(function& function, llvm::Function* func) {
    auto owner_st = function.get_owner();
    if (!owner_st) { _builder->CreateRetVoid(); llvm::verifyFunction(*func); return; }

    auto this_param_it = _context->_function_this_variables.find(function.shared_as<model::function>());
    if (this_param_it == _context->_function_this_variables.end()) { _builder->CreateRetVoid(); llvm::verifyFunction(*func); return; }
    auto this_ptr = _builder->CreateLoad(
        owner_st->get_struct_type()->get_reference()->get_llvm_type(),
        this_param_it->second, "this_ptr");
    if (!this_ptr) { _builder->CreateRetVoid(); llvm::verifyFunction(*func); return; }

    // Get the newCapacity parameter
    auto& params = function.parameters();
    if (params.empty()) { _builder->CreateRetVoid(); llvm::verifyFunction(*func); return; }
    auto param_it = _context->_parameter_variables.find(params[0]);
    if (param_it == _context->_parameter_variables.end()) { _builder->CreateRetVoid(); llvm::verifyFunction(*func); return; }
    auto new_cap_val = _builder->CreateLoad(
        llvm::Type::getInt32Ty(_context->llvm_context()), param_it->second, "new_capacity");

    auto data_field = owner_st->get_struct_type()->get_member("_data");
    auto cap_field = owner_st->get_struct_type()->get_member("_capacity");
    if (!data_field || !cap_field) { _builder->CreateRetVoid(); llvm::verifyFunction(*func); return; }

    // Get element type T
    auto data_var = owner_st->variables().find("_data");
    if (data_var == owner_st->variables().end()) { _builder->CreateRetVoid(); llvm::verifyFunction(*func); return; }
    auto data_type = data_var->second->get_type();
    auto ptr_t = std::dynamic_pointer_cast<pointer_type>(data_type);
    if (!ptr_t) { _builder->CreateRetVoid(); llvm::verifyFunction(*func); return; }
    auto elem_type = ptr_t->get_subtype();
    auto* llvm_elem_type = _context->get_llvm_type(elem_type);

    // sizeof(T)
    auto& dl = _context->module().getDataLayout();
    uint64_t elem_size = dl.getTypeAllocSize(llvm_elem_type);
    auto elem_size_val = llvm::ConstantInt::get(llvm::Type::getInt64Ty(_context->llvm_context()), elem_size);
    auto cap_i64 = _builder->CreateZExt(new_cap_val, llvm::Type::getInt64Ty(_context->llvm_context()), "cap_i64");
    auto total_size = _builder->CreateMul(cap_i64, elem_size_val, "total_size");

    // Load current _data
    auto ptr_type = llvm::PointerType::get(_context->llvm_context(), 0);
    auto data_ptr_ptr = _builder->CreateStructGEP(
        _context->get_llvm_type(owner_st->get_struct_type()),
        this_ptr, (unsigned)data_field->index, "data_ptr_ptr");
    auto old_ptr = _builder->CreateLoad(ptr_type, data_ptr_ptr, "old_data");

    // Call realloc
    auto realloc_func = _context->module().getOrInsertFunction("realloc",
        llvm::FunctionType::get(ptr_type, {ptr_type, llvm::Type::getInt64Ty(_context->llvm_context())}, false));
    auto new_ptr = _builder->CreateCall(realloc_func, {old_ptr, total_size}, "new_data");

    // Store new _data
    _builder->CreateStore(new_ptr, data_ptr_ptr);

    // Store new _capacity
    auto cap_ptr = _builder->CreateStructGEP(
        _context->get_llvm_type(owner_st->get_struct_type()),
        this_ptr, (unsigned)cap_field->index, "cap_ptr");
    _builder->CreateStore(new_cap_val, cap_ptr);

    _builder->CreateRetVoid();
    llvm::verifyFunction(*func);
}

// ─────────────────────────────────────────────────────────────────────────────
// MultiSlot::deallocate — free(_data), _data = null, _capacity = 0
// ─────────────────────────────────────────────────────────────────────────────

void implementation_generator::emit_intrinsic_multislot_deallocate(function& function, llvm::Function* func) {
    auto owner_st = function.get_owner();
    if (!owner_st) { _builder->CreateRetVoid(); llvm::verifyFunction(*func); return; }

    auto this_param_it = _context->_function_this_variables.find(function.shared_as<model::function>());
    if (this_param_it == _context->_function_this_variables.end()) { _builder->CreateRetVoid(); llvm::verifyFunction(*func); return; }
    auto this_ptr = _builder->CreateLoad(
        owner_st->get_struct_type()->get_reference()->get_llvm_type(),
        this_param_it->second, "this_ptr");
    if (!this_ptr) { _builder->CreateRetVoid(); llvm::verifyFunction(*func); return; }

    auto data_field = owner_st->get_struct_type()->get_member("_data");
    auto cap_field = owner_st->get_struct_type()->get_member("_capacity");
    if (!data_field || !cap_field) { _builder->CreateRetVoid(); llvm::verifyFunction(*func); return; }

    auto ptr_type = llvm::PointerType::get(_context->llvm_context(), 0);
    auto data_ptr_ptr = _builder->CreateStructGEP(
        _context->get_llvm_type(owner_st->get_struct_type()),
        this_ptr, (unsigned)data_field->index, "data_ptr_ptr");
    auto data_ptr = _builder->CreateLoad(ptr_type, data_ptr_ptr, "data_ptr");

    // Call free
    auto free_func = _context->module().getOrInsertFunction("free",
        llvm::FunctionType::get(llvm::Type::getVoidTy(_context->llvm_context()), {ptr_type}, false));
    _builder->CreateCall(free_func, {data_ptr});

    // Store null to _data
    _builder->CreateStore(llvm::ConstantPointerNull::get(ptr_type), data_ptr_ptr);

    // Store 0 to _capacity
    auto cap_ptr = _builder->CreateStructGEP(
        _context->get_llvm_type(owner_st->get_struct_type()),
        this_ptr, (unsigned)cap_field->index, "cap_ptr");
    _builder->CreateStore(llvm::ConstantInt::get(llvm::Type::getInt32Ty(_context->llvm_context()), 0), cap_ptr);

    _builder->CreateRetVoid();
    llvm::verifyFunction(*func);
}

// ─────────────────────────────────────────────────────────────────────────────
// MultiSlot::construct — placement new on _data[index] with forwarded args
// ─────────────────────────────────────────────────────────────────────────────

void implementation_generator::emit_intrinsic_multislot_construct(function& function, llvm::Function* func) {
    auto owner_st = function.get_owner();
    if (!owner_st) { _builder->CreateRetVoid(); llvm::verifyFunction(*func); return; }

    auto this_param_it = _context->_function_this_variables.find(function.shared_as<model::function>());
    if (this_param_it == _context->_function_this_variables.end()) { _builder->CreateRetVoid(); llvm::verifyFunction(*func); return; }
    auto this_ptr = _builder->CreateLoad(
        owner_st->get_struct_type()->get_reference()->get_llvm_type(),
        this_param_it->second, "this_ptr");
    if (!this_ptr) { _builder->CreateRetVoid(); llvm::verifyFunction(*func); return; }

    // Parameters: first is 'index', rest are constructor args
    auto& params = function.parameters();
    if (params.empty()) { _builder->CreateRetVoid(); llvm::verifyFunction(*func); return; }

    // Load index parameter
    auto idx_it = _context->_parameter_variables.find(params[0]);
    if (idx_it == _context->_parameter_variables.end()) { _builder->CreateRetVoid(); llvm::verifyFunction(*func); return; }
    auto index_val = _builder->CreateLoad(
        llvm::Type::getInt32Ty(_context->llvm_context()), idx_it->second, "index");

    // Get _data pointer
    auto data_field = owner_st->get_struct_type()->get_member("_data");
    if (!data_field) { _builder->CreateRetVoid(); llvm::verifyFunction(*func); return; }

    auto data_ptr_ptr = _builder->CreateStructGEP(
        _context->get_llvm_type(owner_st->get_struct_type()),
        this_ptr, (unsigned)data_field->index, "data_ptr_ptr");

    // Get element type T
    auto data_var = owner_st->variables().find("_data");
    if (data_var == owner_st->variables().end()) { _builder->CreateRetVoid(); llvm::verifyFunction(*func); return; }
    auto data_type = data_var->second->get_type();
    auto ptr_t = std::dynamic_pointer_cast<pointer_type>(data_type);
    if (!ptr_t) { _builder->CreateRetVoid(); llvm::verifyFunction(*func); return; }
    auto elem_type = ptr_t->get_subtype();
    auto* llvm_elem_type = _context->get_llvm_type(elem_type);

    auto ptr_type = llvm::PointerType::get(_context->llvm_context(), 0);
    auto data_ptr = _builder->CreateLoad(ptr_type, data_ptr_ptr, "data_ptr");

    // GEP to _data[index]
    auto elem_ptr = _builder->CreateGEP(llvm_elem_type, data_ptr, {index_val}, "elem_ptr");

    // Check if T is a struct type with constructors
    auto slot_st_type = std::dynamic_pointer_cast<struct_type>(elem_type);
    if (!slot_st_type || !slot_st_type->get_struct()) {
        // Primitive type — nothing to construct
        _builder->CreateRetVoid();
        llvm::verifyFunction(*func);
        return;
    }

    auto target_struct = slot_st_type->get_struct();

    // Collect constructor args (parameters after index)
    std::vector<llvm::Value*> ctor_args = {elem_ptr};
    std::vector<std::shared_ptr<type>> arg_types;
    for (size_t i = 1; i < params.size(); ++i) {
        auto param_it = _context->_parameter_variables.find(params[i]);
        if (param_it != _context->_parameter_variables.end()) {
            auto* param_ty = _context->get_llvm_type(params[i]->get_type());
            auto loaded = _builder->CreateLoad(param_ty, param_it->second, params[i]->get_short_name());
            ctor_args.push_back(loaded);
            arg_types.push_back(params[i]->get_type());
        }
    }

    // Find matching constructor
    std::shared_ptr<constructor> best_ctor;
    if (arg_types.empty()) {
        for (auto& ctor : target_struct->constructors()) {
            if (ctor->is_deleted()) continue;
            if (ctor->get_parameter_size() == 0) { best_ctor = ctor; break; }
        }
    } else {
        for (auto& ctor : target_struct->constructors()) {
            if (ctor->is_deleted()) continue;
            if (ctor->get_parameter_size() != arg_types.size()) continue;
            bool match = true;
            for (size_t i = 0; i < arg_types.size(); ++i) {
                auto param_type = ctor->parameters()[i]->get_type();
                if (param_type && arg_types[i] && param_type != arg_types[i]) {
                    auto p1 = std::dynamic_pointer_cast<primitive_type>(param_type);
                    auto p2 = std::dynamic_pointer_cast<primitive_type>(arg_types[i]);
                    if (!p1 || !p2 || p1->get_type() != p2->get_type()) {
                        match = false; break;
                    }
                }
            }
            if (match) { best_ctor = ctor; break; }
        }
    }

    if (best_ctor) {
        auto ctor_it = _context->_functions.find(best_ctor->shared_as<model::function>());
        if (ctor_it != _context->_functions.end()) {
            _builder->CreateCall(ctor_it->second, ctor_args);
        }
    }

    _builder->CreateRetVoid();
    llvm::verifyFunction(*func);
}

// ─────────────────────────────────────────────────────────────────────────────
// MultiSlot::destruct — call destructor on _data[index]
// ─────────────────────────────────────────────────────────────────────────────

void implementation_generator::emit_intrinsic_multislot_destruct(function& function, llvm::Function* func) {
    auto owner_st = function.get_owner();
    if (!owner_st) { _builder->CreateRetVoid(); llvm::verifyFunction(*func); return; }

    auto this_param_it = _context->_function_this_variables.find(function.shared_as<model::function>());
    if (this_param_it == _context->_function_this_variables.end()) { _builder->CreateRetVoid(); llvm::verifyFunction(*func); return; }
    auto this_ptr = _builder->CreateLoad(
        owner_st->get_struct_type()->get_reference()->get_llvm_type(),
        this_param_it->second, "this_ptr");
    if (!this_ptr) { _builder->CreateRetVoid(); llvm::verifyFunction(*func); return; }

    // Get index parameter
    auto& params = function.parameters();
    if (params.empty()) { _builder->CreateRetVoid(); llvm::verifyFunction(*func); return; }
    auto idx_it = _context->_parameter_variables.find(params[0]);
    if (idx_it == _context->_parameter_variables.end()) { _builder->CreateRetVoid(); llvm::verifyFunction(*func); return; }
    auto index_val = _builder->CreateLoad(
        llvm::Type::getInt32Ty(_context->llvm_context()), idx_it->second, "index");

    // Get _data pointer
    auto data_field = owner_st->get_struct_type()->get_member("_data");
    if (!data_field) { _builder->CreateRetVoid(); llvm::verifyFunction(*func); return; }

    auto data_ptr_ptr = _builder->CreateStructGEP(
        _context->get_llvm_type(owner_st->get_struct_type()),
        this_ptr, (unsigned)data_field->index, "data_ptr_ptr");

    // Get element type T
    auto data_var = owner_st->variables().find("_data");
    if (data_var == owner_st->variables().end()) { _builder->CreateRetVoid(); llvm::verifyFunction(*func); return; }
    auto data_type = data_var->second->get_type();
    auto ptr_t = std::dynamic_pointer_cast<pointer_type>(data_type);
    if (!ptr_t) { _builder->CreateRetVoid(); llvm::verifyFunction(*func); return; }
    auto elem_type = ptr_t->get_subtype();
    auto* llvm_elem_type = _context->get_llvm_type(elem_type);

    auto ptr_type = llvm::PointerType::get(_context->llvm_context(), 0);
    auto data_ptr = _builder->CreateLoad(ptr_type, data_ptr_ptr, "data_ptr");

    // GEP to _data[index]
    auto elem_ptr = _builder->CreateGEP(llvm_elem_type, data_ptr, {index_val}, "elem_ptr");

    // Check if T is a struct type with a destructor
    auto slot_st_type = std::dynamic_pointer_cast<struct_type>(elem_type);
    if (!slot_st_type || !slot_st_type->get_struct()) {
        _builder->CreateRetVoid();
        llvm::verifyFunction(*func);
        return;
    }

    auto target_struct = slot_st_type->get_struct();
    auto target_dtor = target_struct->get_destructor();
    if (target_dtor) {
        auto dtor_it = _context->_functions.find(target_dtor->shared_as<model::function>());
        if (dtor_it != _context->_functions.end()) {
            _builder->CreateCall(dtor_it->second, {elem_ptr});
        }
    }

    _builder->CreateRetVoid();
    llvm::verifyFunction(*func);
}

// ─────────────────────────────────────────────────────────────────────────────
// MultiSlot::get — return reference to _data[index]
// ─────────────────────────────────────────────────────────────────────────────

void implementation_generator::emit_intrinsic_multislot_get(function& function, llvm::Function* func) {
    auto owner_st = function.get_owner();
    if (!owner_st) { _builder->CreateRetVoid(); llvm::verifyFunction(*func); return; }

    auto this_param_it = _context->_function_this_variables.find(function.shared_as<model::function>());
    if (this_param_it == _context->_function_this_variables.end()) { _builder->CreateRetVoid(); llvm::verifyFunction(*func); return; }
    auto this_ptr = _builder->CreateLoad(
        owner_st->get_struct_type()->get_reference()->get_llvm_type(),
        this_param_it->second, "this_ptr");
    if (!this_ptr) { _builder->CreateRetVoid(); llvm::verifyFunction(*func); return; }

    // Get index parameter
    auto& params = function.parameters();
    if (params.empty()) { _builder->CreateRetVoid(); llvm::verifyFunction(*func); return; }
    auto idx_it = _context->_parameter_variables.find(params[0]);
    if (idx_it == _context->_parameter_variables.end()) { _builder->CreateRetVoid(); llvm::verifyFunction(*func); return; }
    auto index_val = _builder->CreateLoad(
        llvm::Type::getInt32Ty(_context->llvm_context()), idx_it->second, "index");

    // Get _data pointer
    auto data_field = owner_st->get_struct_type()->get_member("_data");
    if (!data_field) { _builder->CreateRetVoid(); llvm::verifyFunction(*func); return; }

    auto data_ptr_ptr = _builder->CreateStructGEP(
        _context->get_llvm_type(owner_st->get_struct_type()),
        this_ptr, (unsigned)data_field->index, "data_ptr_ptr");

    // Get element type T
    auto data_var = owner_st->variables().find("_data");
    if (data_var == owner_st->variables().end()) { _builder->CreateRetVoid(); llvm::verifyFunction(*func); return; }
    auto data_type = data_var->second->get_type();
    auto ptr_t = std::dynamic_pointer_cast<pointer_type>(data_type);
    if (!ptr_t) { _builder->CreateRetVoid(); llvm::verifyFunction(*func); return; }
    auto elem_type = ptr_t->get_subtype();
    auto* llvm_elem_type = _context->get_llvm_type(elem_type);

    auto ptr_type = llvm::PointerType::get(_context->llvm_context(), 0);
    auto data_ptr = _builder->CreateLoad(ptr_type, data_ptr_ptr, "data_ptr");

    // GEP to _data[index]
    auto elem_ptr = _builder->CreateGEP(llvm_elem_type, data_ptr, {index_val}, "elem_ptr");

    // Return the pointer as reference
    _builder->CreateRet(elem_ptr);
    llvm::verifyFunction(*func);
}

} // namespace k::model::gen







