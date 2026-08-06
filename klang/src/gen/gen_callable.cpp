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
// Callable binding and invocation.
//
// A callable is the fat reference `%__k.callable = type { ptr fn, ptr ctx }`.
// A null `ctx` marks a free function or a static method; a non-null one is passed
// as the implicit first argument (`this`) of the target method.
//

#include "generators.hpp"
#include "resolvers.hpp"
#include "gen_helpers.hpp"
#include "gen_callable_helpers.hpp"

#include "../model/expressions.hpp"
#include "../errors.hpp"

namespace k::model::gen {

// ═══════════════════════════════════════════════════════════════════════════
// symbol_resolver
// ═══════════════════════════════════════════════════════════════════════════

void symbol_resolver::visit_callable_bind_expression(callable_bind_expression& expr) {
    if (expr.get_context()) expr.get_context()->accept(*this);
}

void symbol_resolver::visit_callable_invocation_expression(callable_invocation_expression& expr) {
    if (expr.get_callee()) expr.get_callee()->accept(*this);
    for (const auto& arg : expr.arguments()) {
        if (arg) arg->accept(*this);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// type_reference_resolver
// ═══════════════════════════════════════════════════════════════════════════

void type_reference_resolver::visit_callable_bind_expression(callable_bind_expression& expr) {
    if (expr.get_context()) expr.get_context()->accept(*this);
    // The bind type is installed by whoever created the node (adapt_callable_type),
    // because only the target declaration knows the requested addresser.
}

void type_reference_resolver::visit_callable_invocation_expression(callable_invocation_expression& expr) {
    if (expr.get_callee()) expr.get_callee()->accept(*this);
    for (const auto& arg : expr.arguments()) {
        if (arg) arg->accept(*this);
    }
    auto ct = peel_to_callable(expr.get_callee() ? expr.get_callee()->get_type() : nullptr);
    if (!ct) {
        throw_error(static_cast<unsigned int>(k::diag::callable_diag::ERR_CALLABLE_NOT_INVOCABLE),
            expr.first_lexeme(), "The callee expression is not a callable value");
    }
    if (ct->get_parameter_types().size() != expr.arguments().size()) {
        throw_error(static_cast<unsigned int>(k::diag::callable_diag::ERR_CALLABLE_ARG_COUNT_MISMATCH),
            expr.first_lexeme(),
            "Callable of type '{}' expects {} argument(s) but {} were supplied",
            {ct->to_string(),
             std::to_string(ct->get_parameter_types().size()),
             std::to_string(expr.arguments().size())});
    }
    expr.set_type(ct->get_return_type());
}

// ═══════════════════════════════════════════════════════════════════════════
// implementation_generator
// ═══════════════════════════════════════════════════════════════════════════

llvm::Value* implementation_generator::build_callable_from_function(
    const std::shared_ptr<function>& func,
    llvm::Value* ctx_ptr,
    const std::optional<k::lex::any_lexeme>& where)
{
    auto it = _context->_functions.find(func);
    if (it == _context->_functions.end() || !it->second) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F05B), where,
            "Internal error: LLVM declaration not found for function '{}' while binding a callable",
            {func ? func->get_fq_name() : "<null>"});
    }
    auto* callable_ty = _context->get_or_create_callable_llvm_type();
    llvm::Value* ctx = ctx_ptr
        ? ctx_ptr
        : static_cast<llvm::Value*>(llvm::ConstantPointerNull::get(llvm::PointerType::get(**_context, 0)));
    return build_callable_value(*_builder, callable_ty, it->second, ctx);
}

void implementation_generator::visit_callable_bind_expression(callable_bind_expression& expr) {
    llvm::Value* ctx_ptr = nullptr;
    if (expr.get_context()) {
        _value = nullptr;
        expr.get_context()->accept(*this);
        ctx_ptr = _value;
        _value = nullptr;
    }

    switch (expr.get_kind()) {
        case callable_bind_expression::kind::free_function:
        case callable_bind_expression::kind::static_method:
            // Context-free target: the callable is { @fn, null }.
            _value = build_callable_from_function(expr.get_target(), nullptr, expr.first_lexeme());
            return;
        case callable_bind_expression::kind::bound_method:
            if (!ctx_ptr) {
                throw_error(static_cast<unsigned int>(k::diag::callable_diag::ERR_CALLABLE_MEMBER_BIND_REQUIRES_OBJECT),
                    expr.first_lexeme(),
                    "Binding the non-static member function '{}' to a callable requires a receiver object",
                    {expr.get_target() ? expr.get_target()->get_fq_name() : "<null>"});
            }
            _value = build_callable_from_function(expr.get_target(), ctx_ptr, expr.first_lexeme());
            return;
        default:
            break;
    }
    throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F05C), expr.first_lexeme(),
        "Internal error: this callable binding source is not supported yet");
}

void implementation_generator::emit_callable_invocation(
    const std::shared_ptr<callable_type>& ct,
    llvm::Value* callable_val,
    const std::vector<llvm::Value*>& args,
    const std::shared_ptr<type>& result_type,
    const std::optional<k::lex::any_lexeme>& where)
{
    if (!ct || !callable_val) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F05D), where,
            "Internal error: indirect call without a callable type annotation");
    }

    auto& llvm_ctx = _context->llvm_context();
    auto* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);

    llvm::Value* fn_ptr = extract_fn(*_builder, callable_val);
    llvm::Value* ctx_ptr = extract_ctx(*_builder, callable_val);

    // A nullable callable (* or ?) may hold a null target: trap before dispatching.
    // A + or & callable is non-null by construction, so no check is emitted.
    if (ct->is_nullable()) {
        set_debug_location(where);
        auto* fatal = get_or_declare_fatal_null_function("__k_fatal_null_dereference");
        emit_null_check(fn_ptr, fatal, "callable");
    }

    // Build the LLVM prototype of the target.
    std::vector<llvm::Type*> param_types;
    for (const auto& pt : ct->get_parameter_types()) {
        auto* llt = _context->get_llvm_type(pt);
        if (!llt) {
            throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F05E), where,
                "Internal error: could not map K parameter type to LLVM type for a callable call");
        }
        param_types.push_back(llt);
    }

    const bool uses_sret = needs_sret_return(result_type);
    llvm::Type* llvm_ret = (!result_type || uses_sret)
        ? llvm::Type::getVoidTy(llvm_ctx)
        : _context->get_llvm_type(result_type);
    if (!llvm_ret) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F05E), where,
            "Internal error: could not map the callable return type to LLVM");
    }

    // The sret pointer is allocated once, before the branch, and passed to both
    // branches — the sret argument comes FIRST, before the context (ABI rule).
    llvm::Value* sret_ptr = nullptr;
    if (uses_sret) {
        if (_sret_destination) {
            sret_ptr = _sret_destination;
            _sret_destination = nullptr;
        } else {
            llvm::Function* cur_fn = _builder->GetInsertBlock()->getParent();
            llvm::IRBuilder<> entry_builder(&cur_fn->getEntryBlock(), cur_fn->getEntryBlock().begin());
            sret_ptr = entry_builder.CreateAlloca(_context->get_llvm_type(type::remove_const(result_type)),
                                                  nullptr, "callable_sret");
        }
    }

    std::vector<llvm::Type*> free_params;
    if (uses_sret) free_params.push_back(ptr_ty);
    free_params.insert(free_params.end(), param_types.begin(), param_types.end());
    auto* free_fn_type = llvm::FunctionType::get(llvm_ret, free_params, false);

    std::vector<llvm::Type*> bound_params;
    if (uses_sret) bound_params.push_back(ptr_ty);
    bound_params.push_back(ptr_ty);
    bound_params.insert(bound_params.end(), param_types.begin(), param_types.end());
    auto* bound_fn_type = llvm::FunctionType::get(llvm_ret, bound_params, false);

    llvm::Function* cur_fn = _builder->GetInsertBlock()->getParent();
    auto* free_bb  = llvm::BasicBlock::Create(llvm_ctx, "callable.free", cur_fn);
    auto* bound_bb = llvm::BasicBlock::Create(llvm_ctx, "callable.bound", cur_fn);
    auto* join_bb  = llvm::BasicBlock::Create(llvm_ctx, "callable.join", cur_fn);

    llvm::Value* is_free = _builder->CreateICmpEQ(
        ctx_ptr, llvm::ConstantPointerNull::get(ptr_ty), "callable.isfree");
    _builder->CreateCondBr(is_free, free_bb, bound_bb);

    // ── free branch: fn([sret], args…) ────────────────────────────────────────
    _builder->SetInsertPoint(free_bb);
    std::vector<llvm::Value*> free_args;
    if (uses_sret) free_args.push_back(sret_ptr);
    free_args.insert(free_args.end(), args.begin(), args.end());
    llvm::Value* free_res = create_call_or_invoke(free_fn_type, fn_ptr, free_args,
        llvm_ret->isVoidTy() ? "" : "callable.free.res");
    llvm::BasicBlock* free_end_bb = _builder->GetInsertBlock();
    _builder->CreateBr(join_bb);

    // ── bound branch: fn([sret], ctx, args…) ──────────────────────────────────
    _builder->SetInsertPoint(bound_bb);
    std::vector<llvm::Value*> bound_args;
    if (uses_sret) bound_args.push_back(sret_ptr);
    bound_args.push_back(ctx_ptr);
    bound_args.insert(bound_args.end(), args.begin(), args.end());
    llvm::Value* bound_res = create_call_or_invoke(bound_fn_type, fn_ptr, bound_args,
        llvm_ret->isVoidTy() ? "" : "callable.bound.res");
    llvm::BasicBlock* bound_end_bb = _builder->GetInsertBlock();
    _builder->CreateBr(join_bb);

    // ── join ──────────────────────────────────────────────────────────────────
    _builder->SetInsertPoint(join_bb);
    if (uses_sret) {
        _value = sret_ptr;
    } else if (llvm_ret->isVoidTy()) {
        _value = nullptr;
    } else {
        auto* phi = _builder->CreatePHI(llvm_ret, 2, "callable.res");
        phi->addIncoming(free_res, free_end_bb);
        phi->addIncoming(bound_res, bound_end_bb);
        _value = phi;
    }
}

void implementation_generator::visit_callable_invocation_expression(callable_invocation_expression& expr) {
    auto ct = peel_to_callable(expr.get_callee() ? expr.get_callee()->get_type() : nullptr);
    if (!ct) {
        throw_error(static_cast<unsigned int>(k::diag::callable_diag::ERR_CALLABLE_NOT_INVOCABLE),
            expr.first_lexeme(), "The callee expression is not a callable value");
    }

    // Argument evaluation happens once, before the dispatch branch.
    llvm::Value* saved_sret_dest = _sret_destination;
    _sret_destination = nullptr;

    _value = nullptr;
    expr.get_callee()->accept(*this);
    llvm::Value* callable_val = _value;

    std::vector<llvm::Value*> args;
    for (const auto& arg : expr.arguments()) {
        _value = nullptr;
        if (arg) arg->accept(*this);
        if (!_value) {
            throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F05F), expr.first_lexeme(),
                "Internal error: an argument of a callable call produced no LLVM value");
        }
        args.push_back(_value);
    }

    _sret_destination = saved_sret_dest;
    emit_callable_invocation(ct, callable_val, args, expr.get_type(), expr.first_lexeme());
}

} // namespace k::model::gen
