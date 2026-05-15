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
            return std::string("UniSlot::constructor");
        }
        if (auto dtor = std::dynamic_pointer_cast<destructor>(
                const_cast<function&>(fn).shared_as<function>())) {
            return std::string("UniSlot::destructor");
        }
        if (fn.get_short_name() == "construct") {
            return std::string("UniSlot::construct");
        }
        if (fn.get_short_name() == "destruct") {
            return std::string("UniSlot::destruct");
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

} // namespace k::model::gen






