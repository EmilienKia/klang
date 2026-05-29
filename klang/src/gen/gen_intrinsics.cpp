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
        // Primitive type — handle zero-init or direct store
        auto& uni_params = function.parameters();
        if (!uni_params.empty()) {
            // Has constructor arg: store first arg into _slot
            auto param_it = _context->_parameter_variables.find(uni_params[0]);
            if (param_it != _context->_parameter_variables.end()) {
                auto* param_ty = _context->get_llvm_type(uni_params[0]->get_type());
                auto loaded = _builder->CreateLoad(param_ty, param_it->second, "arg_val");
                _builder->CreateStore(loaded, slot_ptr);
            }
        } else {
            // No args: zero-initialize
            auto* llvm_slot_type = _context->get_llvm_type(slot_type);
            _builder->CreateStore(llvm::Constant::getNullValue(llvm_slot_type), slot_ptr);
        }
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
            emit_ctor_invoke_with_construction_exception_wrap(
                ctor_it->second, ctor_args, func);
        }
    } else if (!arg_types.empty()) {
        // No matching constructor found — try aggregate initialization:
        // store each argument directly into the corresponding struct field.
        auto st_type = slot_st_type;
        if (st_type && arg_types.size() <= st_type->fields_size()) {
            auto* llvm_st = _context->get_llvm_type(slot_type);
            auto field_it = st_type->fields_begin();
            for (size_t i = 0; i < arg_types.size(); ++i, ++field_it) {
                auto field_ptr = _builder->CreateStructGEP(llvm_st, slot_ptr, (unsigned)field_it->index, field_it->name + "_ptr");
                // ctor_args[0] is slot_ptr (this), ctor_args[1..] are the values
                _builder->CreateStore(ctor_args[i + 1], field_ptr);
            }
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

    // Null-check: throw OutOfMemory if allocation failed
    emit_alloc_null_check(new_ptr, "multislot_alloc");

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

    // Null-check: throw OutOfMemory if reallocation failed
    emit_alloc_null_check(new_ptr, "multislot_realloc");

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
        // Primitive type — handle zero-init or direct store
        if (params.size() > 1) {
            // Has constructor arg(s): store the first one into _data[index]
            auto param_it = _context->_parameter_variables.find(params[1]);
            if (param_it != _context->_parameter_variables.end()) {
                auto* param_ty = _context->get_llvm_type(params[1]->get_type());
                auto loaded = _builder->CreateLoad(param_ty, param_it->second, "arg_val");
                _builder->CreateStore(loaded, elem_ptr);
            }
        } else {
            // No args: zero-initialize
            _builder->CreateStore(llvm::Constant::getNullValue(llvm_elem_type), elem_ptr);
        }
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
            emit_ctor_invoke_with_construction_exception_wrap(
                ctor_it->second, ctor_args, func);
        }
    } else if (!arg_types.empty()) {
        // No matching constructor found — try aggregate initialization:
        // store each argument directly into the corresponding struct field.
        auto st_type = slot_st_type;
        if (st_type && arg_types.size() <= st_type->fields_size()) {
            auto field_it = st_type->fields_begin();
            for (size_t i = 0; i < arg_types.size(); ++i, ++field_it) {
                auto field_ptr = _builder->CreateStructGEP(llvm_elem_type, elem_ptr, (unsigned)field_it->index, field_it->name + "_ptr");
                // ctor_args[0] is elem_ptr (this), ctor_args[1..] are the values
                _builder->CreateStore(ctor_args[i + 1], field_ptr);
            }
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

// ─────────────────────────────────────────────────────────────────────────────
// emit_ctor_invoke_with_construction_exception_wrap
//
// Invokes a constructor, wrapping any checked exception (Exception-derived)
// into a ConstructionException. FatalError-derived exceptions propagate unchanged.
//
// Generated IR pattern:
//   invoke ctor(...) to %normal unwind to %lpad
// %normal:
//   <continue>
// %lpad:
//   landingpad catch-all
//   load _k_thrown_typeinfo_chain
//   walk chain entries: if any matches FatalError typeinfo → resume
//   otherwise → allocate + throw ConstructionException
// ─────────────────────────────────────────────────────────────────────────────

void implementation_generator::emit_ctor_invoke_with_construction_exception_wrap(
    llvm::Function* ctor_func, llvm::ArrayRef<llvm::Value*> ctor_args,
    llvm::Function* current_func)
{
    auto& llvm_ctx = _context->llvm_context();
    auto& mod = _context->module();
    auto* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);
    auto* i32_ty = llvm::Type::getInt32Ty(llvm_ctx);
    auto* i64_ty = llvm::Type::getInt64Ty(llvm_ctx);
    auto* void_ty = llvm::Type::getVoidTy(llvm_ctx);

    // Set personality function for exception handling
    if (!current_func->hasPersonalityFn()) {
        auto personality = mod.getOrInsertFunction("__gxx_personality_v0",
            llvm::FunctionType::get(i32_ty, true));
        current_func->setPersonalityFn(llvm::cast<llvm::Constant>(personality.getCallee()));
    }

    // Create basic blocks
    auto* normal_bb = llvm::BasicBlock::Create(llvm_ctx, "ctor_ok", current_func);
    auto* lpad_bb = llvm::BasicBlock::Create(llvm_ctx, "ctor_lpad", current_func);

    // Invoke the constructor
    _builder->CreateInvoke(ctor_func, normal_bb, lpad_bb, ctor_args);

    // ── Landing pad: catch all exceptions ──
    _builder->SetInsertPoint(lpad_bb);
    auto* lpad_type = llvm::StructType::get(llvm_ctx, {ptr_ty, i32_ty});
    auto* lpad = _builder->CreateLandingPad(lpad_type, 1, "ctor_lpad_val");
    lpad->addClause(llvm::ConstantPointerNull::get(ptr_ty)); // catch-all

    auto* exc_ptr = _builder->CreateExtractValue(lpad, 0, "ctor_exc_ptr");

    // Load the typeinfo chain of the thrown exception
    auto* ti_chain_global = mod.getNamedGlobal("_k_thrown_typeinfo_chain");
    if (!ti_chain_global) {
        ti_chain_global = new llvm::GlobalVariable(
            mod, ptr_ty, /*isConstant=*/false,
            llvm::GlobalValue::ExternalLinkage,
            llvm::ConstantPointerNull::get(ptr_ty),
            "_k_thrown_typeinfo_chain");
    }
    auto* thrown_chain_ptr = _builder->CreateLoad(ptr_ty, ti_chain_global, "thrown_chain");

    // Get or create the FatalError typeinfo global
    k::name fatal_error_name(false, {"k", "FatalError"});
    std::string fatal_ti_name = mangler::mangle_rtti(fatal_error_name);
    auto* fatal_ti_gv = mod.getNamedGlobal(fatal_ti_name);
    if (!fatal_ti_gv) {
        fatal_ti_gv = new llvm::GlobalVariable(
            mod, ptr_ty, /*isConstant=*/true,
            llvm::GlobalValue::LinkOnceODRLinkage,
            llvm::ConstantPointerNull::get(ptr_ty),
            fatal_ti_name);
    }

    // Walk the typeinfo chain: if any entry matches FatalError → resume
    // Chain entry struct: { ptr typeinfo, i32 offset }
    auto* chain_entry_ty = llvm::StructType::get(llvm_ctx, {ptr_ty, i32_ty});

    auto* loop_bb = llvm::BasicBlock::Create(llvm_ctx, "fatal_check_loop", current_func);
    auto* is_fatal_bb = llvm::BasicBlock::Create(llvm_ctx, "is_fatal", current_func);
    auto* not_fatal_bb = llvm::BasicBlock::Create(llvm_ctx, "not_fatal", current_func);

    _builder->CreateBr(loop_bb);
    _builder->SetInsertPoint(loop_bb);

    // PHI for current chain pointer
    auto* phi_ptr = _builder->CreatePHI(ptr_ty, 2, "chain_ptr");
    phi_ptr->addIncoming(thrown_chain_ptr, lpad_bb);

    // Load typeinfo field (index 0)
    auto* ti_field_ptr = _builder->CreateStructGEP(chain_entry_ty, phi_ptr, 0, "entry_ti_ptr");
    auto* entry_ti = _builder->CreateLoad(ptr_ty, ti_field_ptr, "chain_entry_ti");

    // Check if null (end of chain → not a FatalError)
    auto* is_null = _builder->CreateICmpEQ(entry_ti,
        llvm::ConstantPointerNull::get(ptr_ty), "chain_end");
    auto* check_fatal_bb = llvm::BasicBlock::Create(llvm_ctx, "check_fatal", current_func);
    _builder->CreateCondBr(is_null, not_fatal_bb, check_fatal_bb);

    // Compare with FatalError typeinfo
    _builder->SetInsertPoint(check_fatal_bb);
    auto* is_fatal = _builder->CreateICmpEQ(entry_ti, fatal_ti_gv, "is_fatal_match");
    auto* next_bb = llvm::BasicBlock::Create(llvm_ctx, "chain_next", current_func);
    _builder->CreateCondBr(is_fatal, is_fatal_bb, next_bb);

    // Advance to next entry
    _builder->SetInsertPoint(next_bb);
    auto* next_entry = _builder->CreateGEP(chain_entry_ty, phi_ptr,
        {llvm::ConstantInt::get(i32_ty, 1)}, "next_entry");
    phi_ptr->addIncoming(next_entry, next_bb);
    _builder->CreateBr(loop_bb);

    // ── is_fatal: resume unwinding (let FatalError propagate) ──
    _builder->SetInsertPoint(is_fatal_bb);
    auto* resume_val = llvm::UndefValue::get(lpad_type);
    auto* resume_val2 = _builder->CreateInsertValue(resume_val, exc_ptr, 0);
    auto* resume_val3 = _builder->CreateInsertValue(resume_val2,
        llvm::ConstantInt::get(i32_ty, 0), 1);
    _builder->CreateResume(resume_val3);

    // ── not_fatal: throw ConstructionException ──
    _builder->SetInsertPoint(not_fatal_bb);

    // End the current catch (release the caught exception)
    auto cxa_end = mod.getOrInsertFunction("__cxa_end_catch",
        llvm::FunctionType::get(void_ty, false));
    _builder->CreateCall(cxa_end);

    // Get ConstructionException type info
    k::name ce_name(false, {"k", "ConstructionException"});
    std::string ce_ti_name = mangler::mangle_rtti(ce_name);
    auto* ce_ti_gv = mod.getNamedGlobal(ce_ti_name);
    if (!ce_ti_gv) {
        ce_ti_gv = new llvm::GlobalVariable(
            mod, ptr_ty, /*isConstant=*/true,
            llvm::GlobalValue::LinkOnceODRLinkage,
            llvm::ConstantPointerNull::get(ptr_ty),
            ce_ti_name);
    }

    // Find ConstructionException in the model to get its LLVM type and constructor
    std::shared_ptr<aggregate> ce_agg;
    // Look up in the unit's namespace tree
    auto root_ns = _unit.get_root_namespace();
    if (root_ns) {
        // Check if ConstructionException is in the k namespace
        auto k_ns = root_ns->get_child_namespace("k");
        if (k_ns) {
            ce_agg = k_ns->get_aggregate("ConstructionException");
        }
        if (!ce_agg) {
            ce_agg = root_ns->get_aggregate("ConstructionException");
        }
    }
    // Also check imported aggregates
    if (!ce_agg) {
        k::name ce_lookup(false, {"k", "ConstructionException"});
        ce_agg = _unit.get_or_create_imported_aggregate(ce_lookup, _context);
    }

    // Determine ConstructionException struct size
    llvm::Type* ce_llvm_type = nullptr;
    uint64_t ce_size = 0;
    if (ce_agg && ce_agg->get_struct_type()) {
        ce_llvm_type = _context->get_llvm_type(ce_agg->get_struct_type());
        if (ce_llvm_type) {
            ce_size = mod.getDataLayout().getTypeAllocSize(ce_llvm_type);
        }
    }

    if (ce_size == 0) {
        // Fallback: use a reasonable size (Throwable has _code:int + vptr ~ 16 bytes)
        ce_size = 16;
    }

    // Allocate exception memory
    auto cxa_alloc = mod.getOrInsertFunction("__cxa_allocate_exception",
        llvm::FunctionType::get(ptr_ty, {i64_ty}, false));
    auto* ce_mem = _builder->CreateCall(cxa_alloc,
        {llvm::ConstantInt::get(i64_ty, ce_size)}, "ce_mem");

    // Construct ConstructionException: call its constructor with code=6
    if (ce_agg) {
        // Find the constructor that takes (int)
        std::shared_ptr<constructor> ce_ctor;
        for (auto& ctor : ce_agg->constructors()) {
            if (ctor->is_deleted()) continue;
            if (ctor->get_parameter_size() == 1) {
                auto p_type = ctor->parameters()[0]->get_type();
                if (auto pt = std::dynamic_pointer_cast<primitive_type>(p_type)) {
                    if (pt->get_type() == primitive_type::INT) {
                        ce_ctor = ctor;
                        break;
                    }
                }
            }
        }
        if (ce_ctor) {
            auto ctor_it = _context->_functions.find(ce_ctor->shared_as<model::function>());
            if (ctor_it != _context->_functions.end()) {
                // Call ConstructionException(6)
                _builder->CreateCall(ctor_it->second,
                    {ce_mem, llvm::ConstantInt::get(i32_ty, 6)});
            }
        } else {
            // Try default constructor
            for (auto& ctor : ce_agg->constructors()) {
                if (ctor->is_deleted()) continue;
                if (ctor->get_parameter_size() == 0) {
                    auto ctor_it = _context->_functions.find(ctor->shared_as<model::function>());
                    if (ctor_it != _context->_functions.end()) {
                        _builder->CreateCall(ctor_it->second, {ce_mem});
                    }
                    break;
                }
            }
        }
    }

    // Build typeinfo chain for ConstructionException
    // Chain: ConstructionException → Exception → Throwable → null
    k::name exc_name(false, {"k", "Exception"});
    k::name thr_name(false, {"k", "Throwable"});
    std::string exc_ti_name = mangler::mangle_rtti(exc_name);
    std::string thr_ti_name = mangler::mangle_rtti(thr_name);

    auto* exc_ti_gv = mod.getNamedGlobal(exc_ti_name);
    if (!exc_ti_gv) {
        exc_ti_gv = new llvm::GlobalVariable(mod, ptr_ty, true,
            llvm::GlobalValue::LinkOnceODRLinkage,
            llvm::ConstantPointerNull::get(ptr_ty), exc_ti_name);
    }
    auto* thr_ti_gv = mod.getNamedGlobal(thr_ti_name);
    if (!thr_ti_gv) {
        thr_ti_gv = new llvm::GlobalVariable(mod, ptr_ty, true,
            llvm::GlobalValue::LinkOnceODRLinkage,
            llvm::ConstantPointerNull::get(ptr_ty), thr_ti_name);
    }

    // Build chain constant array: [CE, Exception, Throwable, null-terminator]
    // Each entry is { ptr typeinfo, i32 offset }
    // For linear single-inheritance, offsets are cumulative base sub-object offsets.
    // We need the actual byte offsets. For a simple class hierarchy:
    //   ConstructionException → Exception → Throwable
    // The offsets depend on the struct layout. Compute them if possible.
    uint32_t ce_to_exc_offset = 0;
    uint32_t ce_to_thr_offset = 0;
    if (ce_llvm_type && ce_llvm_type->isStructTy()) {
        auto* ce_struct_ty = llvm::cast<llvm::StructType>(ce_llvm_type);
        auto* sl = mod.getDataLayout().getStructLayout(ce_struct_ty);
        // In K's layout, base sub-object is typically at field index 0 or 1
        // For classes: field 0 = vptr, field 1 = base sub-object
        // For structs: field 0 = base sub-object
        unsigned num_elems = ce_struct_ty->getNumElements();
        // Find the Exception base sub-object field
        std::shared_ptr<aggregate> exc_agg;
        if (root_ns) {
            auto k_ns = root_ns->get_child_namespace("k");
            if (k_ns) exc_agg = k_ns->get_aggregate("Exception");
            if (!exc_agg) exc_agg = root_ns->get_aggregate("Exception");
        }
        if (!exc_agg) {
            exc_agg = _unit.get_or_create_imported_aggregate(exc_name, _context);
        }
        llvm::Type* exc_llvm_type = exc_agg && exc_agg->get_struct_type()
            ? _context->get_llvm_type(exc_agg->get_struct_type()) : nullptr;
        if (exc_llvm_type) {
            for (unsigned fi = 0; fi < num_elems; ++fi) {
                if (ce_struct_ty->getElementType(fi) == exc_llvm_type) {
                    ce_to_exc_offset = (uint32_t)sl->getElementOffset(fi);
                    break;
                }
            }
        }
        // Find Throwable offset within Exception
        if (exc_llvm_type && exc_llvm_type->isStructTy()) {
            auto* exc_struct_ty = llvm::cast<llvm::StructType>(exc_llvm_type);
            auto* exc_sl = mod.getDataLayout().getStructLayout(exc_struct_ty);
            std::shared_ptr<aggregate> thr_agg;
            if (root_ns) {
                auto k_ns = root_ns->get_child_namespace("k");
                if (k_ns) thr_agg = k_ns->get_aggregate("Throwable");
                if (!thr_agg) thr_agg = root_ns->get_aggregate("Throwable");
            }
            if (!thr_agg) {
                thr_agg = _unit.get_or_create_imported_aggregate(thr_name, _context);
            }
            llvm::Type* thr_llvm_type = thr_agg && thr_agg->get_struct_type()
                ? _context->get_llvm_type(thr_agg->get_struct_type()) : nullptr;
            if (thr_llvm_type) {
                unsigned exc_num = exc_struct_ty->getNumElements();
                for (unsigned fi = 0; fi < exc_num; ++fi) {
                    if (exc_struct_ty->getElementType(fi) == thr_llvm_type) {
                        ce_to_thr_offset = ce_to_exc_offset + (uint32_t)exc_sl->getElementOffset(fi);
                        break;
                    }
                }
            }
        }
    }

    // Build chain entries
    std::vector<llvm::Constant*> chain_entries;
    chain_entries.push_back(llvm::ConstantStruct::get(chain_entry_ty,
        {ce_ti_gv, llvm::ConstantInt::get(i32_ty, 0)}));
    chain_entries.push_back(llvm::ConstantStruct::get(chain_entry_ty,
        {exc_ti_gv, llvm::ConstantInt::get(i32_ty, ce_to_exc_offset)}));
    chain_entries.push_back(llvm::ConstantStruct::get(chain_entry_ty,
        {thr_ti_gv, llvm::ConstantInt::get(i32_ty, ce_to_thr_offset)}));
    // Null terminator
    chain_entries.push_back(llvm::ConstantStruct::get(chain_entry_ty,
        {llvm::ConstantPointerNull::get(ptr_ty), llvm::ConstantInt::get(i32_ty, 0)}));

    auto* chain_arr_ty = llvm::ArrayType::get(chain_entry_ty, chain_entries.size());
    auto* chain_initializer = llvm::ConstantArray::get(chain_arr_ty, chain_entries);
    std::string chain_global_name = ce_ti_name + "_chain";
    auto* chain_arr_gv = mod.getNamedGlobal(chain_global_name);
    if (!chain_arr_gv) {
        chain_arr_gv = new llvm::GlobalVariable(
            mod, chain_arr_ty, /*isConstant=*/true,
            llvm::GlobalValue::LinkOnceODRLinkage,
            chain_initializer, chain_global_name);
    }

    // Store typeinfo chain
    auto* ti_chain_store = mod.getNamedGlobal("_k_thrown_typeinfo_chain");
    if (!ti_chain_store) {
        ti_chain_store = new llvm::GlobalVariable(
            mod, ptr_ty, false, llvm::GlobalValue::ExternalLinkage,
            llvm::ConstantPointerNull::get(ptr_ty), "_k_thrown_typeinfo_chain");
    }
    _builder->CreateStore(chain_arr_gv, ti_chain_store);

    // Store primary typeinfo
    auto* ti_global = mod.getNamedGlobal("_k_thrown_typeinfo");
    if (!ti_global) {
        ti_global = new llvm::GlobalVariable(
            mod, ptr_ty, false, llvm::GlobalValue::ExternalLinkage,
            llvm::ConstantPointerNull::get(ptr_ty), "_k_thrown_typeinfo");
    }
    _builder->CreateStore(ce_ti_gv, ti_global);

    // __cxa_throw
    auto cxa_throw = mod.getOrInsertFunction("__cxa_throw",
        llvm::FunctionType::get(void_ty, {ptr_ty, ptr_ty, ptr_ty}, false));
    auto* throw_call = _builder->CreateCall(cxa_throw,
        {ce_mem, ce_ti_gv, llvm::ConstantPointerNull::get(ptr_ty)});
    throw_call->setDoesNotReturn();
    _builder->CreateUnreachable();

    // ── Continue normal execution after successful construction ──
    _builder->SetInsertPoint(normal_bb);
}

} // namespace k::model::gen







