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
#include "../common/operator_names.hpp"

#include "../model/expressions.hpp"
#include "../model/imported.hpp"
#include "../errors.hpp"

namespace k::model::gen {

namespace {

/**
 * Unwrap the indirection layers of a receiver type down to the aggregate it
 * designates. Handles `ref`, `drain`, `owner`, `ptr`, `link`, `view` and the
 * `ref<owner<T>>` shape produced by an lvalue access to an owner variable.
 */
std::shared_ptr<aggregate> receiver_aggregate(std::shared_ptr<type> t) {
    for (unsigned int guard = 0; t && guard < 8u; ++guard) {
        t = type::canonical(type::remove_const(t));
        if (auto st = std::dynamic_pointer_cast<struct_type>(t)) {
            return st->get_struct();
        }
        if (type::is_reference(t) || type::is_drain(t) || type::is_owner(t)
            || type::is_pointer(t) || type::is_link(t) || type::is_view(t)) {
            t = t->get_subtype();
            continue;
        }
        break;
    }
    return nullptr;
}

/** True when the receiver designates a `const` object. */
bool receiver_is_const(std::shared_ptr<type> t) {
    for (unsigned int guard = 0; t && guard < 8u; ++guard) {
        if (type::is_const(t)) return true;
        if (type::is_reference(t) || type::is_drain(t) || type::is_owner(t)
            || type::is_pointer(t) || type::is_link(t) || type::is_view(t)) {
            t = t->get_subtype();
            continue;
        }
        break;
    }
    return false;
}

/** True when the receiver may hold a null target (`*`, `?` — and `!`). */
bool receiver_is_nullable(std::shared_ptr<type> t) {
    t = type::remove_const(t);
    if (type::is_reference(t)) t = type::remove_const(t->get_subtype());
    return type::is_pointer(t) || type::is_view(t) || type::is_owner(t);
}

} // anonymous namespace

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

// ── Member function binding (phase B.4) ────────────────────────────────────

std::vector<std::shared_ptr<function>> type_reference_resolver::collect_member_overloads(
    const std::shared_ptr<aggregate>& agg, const std::string& name)
{
    std::vector<std::shared_ptr<function>> out;
    std::unordered_set<const aggregate*> visited;
    std::function<void(const std::shared_ptr<aggregate>&)> walk =
        [&](const std::shared_ptr<aggregate>& a) {
            if (!a || !visited.insert(a.get()).second) return;
            for (const auto& fn : a->get_functions(name)) {
                if (!fn) continue;
                if (std::find(out.begin(), out.end(), fn) == out.end()) out.push_back(fn);
            }
            for (const auto& bs : a->get_bases()) walk(bs.base);
        };
    walk(agg);
    return out;
}

std::shared_ptr<callable_type> type_reference_resolver::build_member_callable_marker(
    const std::shared_ptr<aggregate>& agg, const std::string& name)
{
    auto candidates = collect_member_overloads(agg, name);
    if (candidates.empty()) return nullptr;
    // Any candidate does: the marker only carries a *shape*, the real overload is
    // chosen against the destination prototype by try_bind_member_callable().
    const auto& fn = candidates.front();
    callable_type_builder builder(_context);
    builder.addresser(callable_type::addresser::link);
    if (fn->get_return_type()) builder.return_type(fn->get_return_type());
    for (size_t i = 0; i < fn->get_parameter_size(); ++i) {
        auto p = fn->get_parameter(i);
        if (!p || !p->get_type()) return nullptr;
        builder.append_parameter_type(p->get_type());
    }
    return builder.build();
}

std::shared_ptr<expression> type_reference_resolver::make_member_bind(
    const std::shared_ptr<function>& fn,
    const std::shared_ptr<expression>& receiver,
    const std::shared_ptr<callable_type>& tgt,
    bool nullable_receiver,
    const lex::opt_any_lexeme& where)
{
    auto owner = fn->parent<aggregate>();
    if (!owner || !owner->get_struct_type()) {
        throw_error(static_cast<unsigned int>(k::diag::callable_diag::ERR_CALLABLE_MEMBER_BIND_REQUIRES_OBJECT),
            where, "'{}' is not a member function and cannot be bound to a receiver",
            {fn->get_fq_name()});
    }

    // A `*`/`?` receiver bound to a nullable destination propagates the null instead
    // of trapping; every other combination goes through the normal dereference, which
    // raises a FatalError on a null receiver.
    const bool propagate = nullable_receiver && tgt->is_nullable();

    std::shared_ptr<expression> ctx;
    if (propagate) {
        // Keep the raw pointer: no dereference is emitted, so no null trap.
        ctx = adapt_type(receiver, owner->get_struct_type()->get_pointer());
    } else {
        auto this_param = fn->get_this_parameter();
        if (!this_param || !this_param->get_type()) {
            throw_error(static_cast<unsigned int>(k::diag::callable_diag::ERR_CALLABLE_MEMBER_BIND_REQUIRES_OBJECT),
                where, "Member function '{}' has no implicit 'this' parameter", {fn->get_fq_name()});
        }
        ctx = adapt_type(receiver, this_param->get_type());
    }
    if (!ctx) {
        throw_error(static_cast<unsigned int>(k::diag::callable_diag::ERR_CALLABLE_INCOMPATIBLE_SIGNATURE),
            where,
            "The receiver of type '{}' cannot be bound to member function '{}'; "
            "a const object can only bind a const member function",
            {receiver->get_type() ? receiver->get_type()->to_string() : "?", fn->get_fq_name()});
    }

    const bool use_vtable = fn->is_virtual() && fn->get_vtable_slot() >= 0 && owner->has_vtable();

    auto bind = callable_bind_expression::make_shared(
        use_vtable ? callable_bind_expression::kind::virtual_method
                   : callable_bind_expression::kind::bound_method,
        fn, ctx);
    if (use_vtable) {
        bind->set_vtable_slot(fn->get_vtable_slot());
        // The vptr is loaded from the *unadjusted* subobject held in ctx: the vtable
        // slot (and its thunk for a secondary base) already performs the adjustment.
        bind->set_dispatch_base(owner);
    }
    bind->set_null_propagating(propagate);
    bind->set_type(tgt);
    return bind;
}

std::shared_ptr<expression> type_reference_resolver::try_bind_member_callable(
    const std::shared_ptr<expression>& expr,
    const std::shared_ptr<callable_type>& tgt)
{
    if (!expr || !tgt || tgt->is_unbound_member() || tgt->is_prototype()) return nullptr;

    std::shared_ptr<expression> receiver;
    std::string member_name;
    lex::opt_any_lexeme where = expr->first_lexeme();
    bool nullable_receiver = false;

    if (auto moe = std::dynamic_pointer_cast<member_of_object_expression>(expr)) {
        // `obj.method` — the receiver has already been upcast to the aggregate that
        // declares the member by visit_member_of_object_expression().
        receiver = moe->sub_expr();
        const auto& nm = moe->symbol().get_name();
        member_name = nm.size() > 1 ? nm.back() : nm.to_string();
    } else if (auto mop = std::dynamic_pointer_cast<member_of_pointer_expression>(expr)) {
        // `ptr->method` — reuse the '.' machinery on a dereference of the pointer so
        // that base lookup and upcasting behave identically. When the destination is
        // nullable the raw pointer is used instead (see make_member_bind).
        const auto& nm = mop->symbol().get_name();
        member_name = nm.size() > 1 ? nm.back() : nm.to_string();
        nullable_receiver = receiver_is_nullable(mop->sub_expr()->get_type());
        if (nullable_receiver && tgt->is_nullable()) {
            receiver = mop->sub_expr();
        } else {
            auto deref = dereference_expression::make_shared(mop->sub_expr());
            auto sym = std::dynamic_pointer_cast<symbol_expression>(mop->symbol().clone());
            auto dot = member_of_object_expression::make_shared(deref, sym);
            dot->accept(*this);
            receiver = dot->sub_expr();
        }
    } else if (auto sym = std::dynamic_pointer_cast<symbol_expression>(expr)) {
        auto fn = sym->get_function();
        if (!fn || fn->is_static() || !fn->parent<aggregate>()) return nullptr;
        // A bare `method` or a qualified `Type::method` naming a non-static member:
        // only bindable from inside a non-static member function of a compatible type.
        std::shared_ptr<parameter> this_param;
        for (auto it = _function_stack.rbegin(); it != _function_stack.rend(); ++it) {
            if (*it && (*it)->is_member() && !(*it)->is_static() && (*it)->get_this_parameter()) {
                this_param = std::const_pointer_cast<parameter>((*it)->get_this_parameter());
                break;
            }
        }
        if (!this_param) {
            throw_error(static_cast<unsigned int>(k::diag::callable_diag::ERR_CALLABLE_MEMBER_BIND_REQUIRES_OBJECT),
                where,
                "Binding the non-static member function '{}' to a callable requires a receiver "
                "object; write 'obj.{}' instead",
                {fn->get_fq_name(), fn->get_short_name()});
        }
        auto this_sym = symbol_expression::from_identifier(k::name("this"));
        this_sym->set_target(this_param);
        this_sym->set_type(this_param->get_type());
        receiver = this_sym;
        member_name = fn->get_short_name();
    } else {
        return nullptr;
    }

    if (!receiver || !receiver->get_type()) return nullptr;
    auto agg = receiver_aggregate(receiver->get_type());
    if (!agg) return nullptr;

    auto candidates = collect_member_overloads(agg, member_name);
    if (candidates.empty()) return nullptr;

    // A const receiver may only bind a const member function.
    const bool const_receiver = receiver_is_const(receiver->get_type());
    std::vector<std::shared_ptr<function>> viable;
    for (const auto& c : candidates) {
        if (!c || c->is_static()) continue;
        if (const_receiver && !c->is_const_member()) continue;
        viable.push_back(c);
    }
    if (viable.empty()) {
        throw_error(static_cast<unsigned int>(k::diag::callable_diag::ERR_CALLABLE_NO_MATCHING_OVERLOAD),
            where,
            "No non-static member function '{}' of '{}' can be bound to a callable of type '{}'{}",
            {member_name, agg->get_short_name(), tgt->to_string(),
             const_receiver ? "; the receiver is const, so only a const member function qualifies" : ""});
    }

    std::shared_ptr<function> fn;
    if (viable.size() == 1) {
        fn = viable.front();
    } else {
        bool ambiguous = false;
        fn = select_overload_for_prototype(viable, *tgt, &ambiguous);
        if (ambiguous) {
            throw_error(static_cast<unsigned int>(k::diag::callable_diag::ERR_CALLABLE_AMBIGUOUS_OVERLOAD),
                where, "Several overloads of '{}' match the callable type '{}'",
                {member_name, tgt->to_string()});
        }
        if (!fn) {
            throw_error(static_cast<unsigned int>(k::diag::callable_diag::ERR_CALLABLE_NO_MATCHING_OVERLOAD),
                where, "No overload of '{}' matches the callable type '{}'",
                {member_name, tgt->to_string()});
        }
    }

    return make_member_bind(fn, receiver, tgt, nullable_receiver, where);
}

/**
 * Bind a functor object (an aggregate declaring `operator()`) to a callable.
 *
 * Produces `callable_bind_expression(kind::functor)` whose context is the receiver
 * adapted to the selected `operator()`'s implicit `this` parameter, so a const object
 * can only bind a `const operator()`.
 */
std::shared_ptr<expression> type_reference_resolver::try_bind_functor_callable(
    const std::shared_ptr<expression>& expr,
    const std::shared_ptr<callable_type>& tgt)
{
    if (!expr || !tgt || tgt->is_unbound_member() || tgt->is_prototype()) return nullptr;
    auto src = expr->get_type();
    if (!src) return nullptr;

    auto agg = receiver_aggregate(src);
    if (!agg) return nullptr;

    auto candidates = collect_member_overloads(agg, std::string(k::op::OP_CALL));
    if (candidates.empty()) return nullptr;

    lex::opt_any_lexeme where = expr->first_lexeme();
    const bool const_receiver = receiver_is_const(src);
    std::vector<std::shared_ptr<function>> viable;
    for (const auto& c : candidates) {
        if (!c || c->is_static()) continue;
        if (const_receiver && !c->is_const_member()) continue;
        viable.push_back(c);
    }
    if (viable.empty()) {
        throw_error(static_cast<unsigned int>(k::diag::callable_diag::ERR_CALLABLE_NO_MATCHING_OVERLOAD),
            where,
            "No 'operator()' of '{}' can be bound to a callable of type '{}'{}",
            {agg->get_short_name(), tgt->to_string(),
             const_receiver ? "; the receiver is const, so only a const operator() qualifies" : ""});
    }

    bool ambiguous = false;
    auto fn = select_overload_for_prototype(viable, *tgt, &ambiguous);
    if (ambiguous) {
        throw_error(static_cast<unsigned int>(k::diag::callable_diag::ERR_CALLABLE_AMBIGUOUS_OPERATOR_CALL),
            where, "Several 'operator()' overloads of '{}' match the callable type '{}'",
            {agg->get_short_name(), tgt->to_string()});
    }
    if (!fn) {
        throw_error(static_cast<unsigned int>(k::diag::callable_diag::ERR_CALLABLE_NO_MATCHING_OVERLOAD),
            where, "No 'operator()' overload of '{}' matches the callable type '{}'",
            {agg->get_short_name(), tgt->to_string()});
    }

    const bool nullable_receiver = receiver_is_nullable(src);
    auto bind = make_member_bind(fn, expr, tgt, nullable_receiver, where);
    if (auto cbe = std::dynamic_pointer_cast<callable_bind_expression>(bind)) {
        // A virtual operator() keeps its vtable dispatch; a direct one is a functor bind.
        if (cbe->get_kind() == callable_bind_expression::kind::bound_method) {
            cbe->set_kind(callable_bind_expression::kind::functor);
        }
    }
    return bind;
}

bool type_reference_resolver::try_resolve_callable_member_invocation(
    function_invocation_expression& expr,
    const std::shared_ptr<member_of_object_expression>& member_callee,
    const std::string& member_name,
    const std::shared_ptr<aggregate>& agg)
{
    if (!member_callee || !agg) return false;

    // Look for a data member of callable type, in the aggregate and its bases.
    std::shared_ptr<callable_type> ct;
    std::unordered_set<const aggregate*> visited;
    std::function<void(const std::shared_ptr<aggregate>&)> walk =
        [&](const std::shared_ptr<aggregate>& a) {
            if (!a || ct || !visited.insert(a.get()).second) return;
            if (auto mv = a->get_variable(member_name)) {
                if (auto found = peel_to_callable(mv->get_type())) {
                    ct = found;
                    return;
                }
            }
            for (const auto& bs : a->get_bases()) walk(bs.base);
        };
    walk(agg);
    if (!ct) return false;

    // Resolve the member access itself: it yields the address of the callable slot.
    member_callee->accept(*this);

    const auto& params = ct->get_parameter_types();
    if (params.size() != expr.arguments().size()) {
        throw_error(static_cast<unsigned int>(k::diag::callable_diag::ERR_CALLABLE_ARG_COUNT_MISMATCH),
            expr.first_lexeme(),
            "Callable member '{}' of type '{}' expects {} argument(s) but {} were supplied",
            {member_name, ct->to_string(),
             std::to_string(params.size()), std::to_string(expr.arguments().size())});
    }
    for (size_t n = 0; n < expr.arguments().size(); ++n) {
        auto arg = expr.arguments().at(n);
        auto cast = adapt_type(arg, params[n]);
        if (cast && cast != arg) expr.assign_argument(n, cast);
    }
    expr.set_type(ct->get_return_type());

    virtual_dispatch_info di;
    di.kind = virtual_dispatch_info::dispatch_kind::INDIRECT;
    expr.set_dispatch_info(std::move(di));
    return true;
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
        case callable_bind_expression::kind::functor:
        case callable_bind_expression::kind::virtual_method:
            if (!ctx_ptr) {
                throw_error(static_cast<unsigned int>(k::diag::callable_diag::ERR_CALLABLE_MEMBER_BIND_REQUIRES_OBJECT),
                    expr.first_lexeme(),
                    "Binding the non-static member function '{}' to a callable requires a receiver object",
                    {expr.get_target() ? expr.get_target()->get_fq_name() : "<null>"});
            }
            _value = build_bound_callable(expr, ctx_ptr);
            return;
        default:
            break;
    }
    throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F05C), expr.first_lexeme(),
        "Internal error: this callable binding source is not supported yet");
}

llvm::Value* implementation_generator::load_vtable_slot(
    const std::shared_ptr<aggregate>& dispatch_base,
    llvm::Value* obj_ptr,
    int slot_index,
    const std::optional<k::lex::any_lexeme>& where)
{
    auto& llvm_ctx = _context->llvm_context();
    auto* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);

    auto* struct_llvm_type = dispatch_base->get_struct_type()
        ? dispatch_base->get_struct_type()->get_llvm_type() : nullptr;
    if (!struct_llvm_type) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F05B), where,
            "Internal error: aggregate '{}' has no LLVM struct type while binding a virtual callable",
            {dispatch_base->get_fq_name()});
    }

    // The vptr is field 0 for a locally-compiled class; an imported aggregate carries
    // its own index in the KDI layout.
    unsigned vptr_field_index = 0;
    if (auto imp = std::dynamic_pointer_cast<imported_aggregate>(dispatch_base)) {
        if (const auto* kdi_agg = imp->get_kdi_aggregate()) {
            for (const auto& lf : kdi_agg->layout) {
                if (auto* vp = std::get_if<kdi::kdi_layout_vptr>(&lf)) {
                    vptr_field_index = vp->llvm_field_index;
                    break;
                }
            }
        }
    }

    llvm::Value* vptr_addr = _builder->CreateStructGEP(struct_llvm_type, obj_ptr, vptr_field_index, "vptr_addr");
    llvm::Value* vptr = _builder->CreateLoad(ptr_ty, vptr_addr, "vptr");

    // Vtable layout is { RTTI, slot0, slot1, … }, hence the +1.
    auto vt = dispatch_base->get_vtable();
    if (vt && vt->llvm_type) {
        llvm::Value* slot_addr = _builder->CreateStructGEP(
            vt->llvm_type, vptr, static_cast<unsigned>(slot_index + 1), "vtable_slot_addr");
        return _builder->CreateLoad(ptr_ty, slot_addr, "vtable_fn");
    }
    // Imported aggregate: the vtable StructType is unknown, index by byte offset.
    const uint64_t ptr_size = 8;
    llvm::Value* slot_offset = llvm::ConstantInt::get(
        llvm::Type::getInt64Ty(llvm_ctx), (slot_index + 1) * ptr_size);
    llvm::Value* slot_addr = _builder->CreateInBoundsGEP(
        llvm::Type::getInt8Ty(llvm_ctx), vptr, slot_offset, "vtable_slot_addr");
    return _builder->CreateLoad(ptr_ty, slot_addr, "vtable_fn");
}

llvm::Value* implementation_generator::build_bound_callable(
    callable_bind_expression& expr, llvm::Value* ctx_ptr)
{
    auto& llvm_ctx = _context->llvm_context();
    auto* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);
    auto* callable_ty = _context->get_or_create_callable_llvm_type();
    const bool is_virtual = expr.get_kind() == callable_bind_expression::kind::virtual_method;

    // ── Null-propagating bind: `{null,null}` when the receiver is null ────────
    if (expr.is_null_propagating()) {
        llvm::Function* cur_fn = _builder->GetInsertBlock()->getParent();
        auto* null_bb  = llvm::BasicBlock::Create(llvm_ctx, "bind.null", cur_fn);
        auto* bind_bb  = llvm::BasicBlock::Create(llvm_ctx, "bind.obj", cur_fn);
        auto* join_bb  = llvm::BasicBlock::Create(llvm_ctx, "bind.join", cur_fn);

        llvm::Value* is_null = _builder->CreateICmpEQ(
            ctx_ptr, llvm::ConstantPointerNull::get(ptr_ty), "bind.isnull");
        _builder->CreateCondBr(is_null, null_bb, bind_bb);

        _builder->SetInsertPoint(null_bb);
        auto* null_ptr = llvm::ConstantPointerNull::get(ptr_ty);
        llvm::Value* null_callable = build_callable_value(*_builder, callable_ty, null_ptr, null_ptr);
        llvm::BasicBlock* null_end_bb = _builder->GetInsertBlock();
        _builder->CreateBr(join_bb);

        _builder->SetInsertPoint(bind_bb);
        llvm::Value* bound = is_virtual
            ? build_callable_value(*_builder, callable_ty,
                  load_vtable_slot(expr.get_dispatch_base(), ctx_ptr, expr.get_vtable_slot(), expr.first_lexeme()),
                  ctx_ptr)
            : build_callable_from_function(expr.get_target(), ctx_ptr, expr.first_lexeme());
        llvm::BasicBlock* bind_end_bb = _builder->GetInsertBlock();
        _builder->CreateBr(join_bb);

        _builder->SetInsertPoint(join_bb);
        auto* phi = _builder->CreatePHI(callable_ty, 2, "bind.res");
        phi->addIncoming(null_callable, null_end_bb);
        phi->addIncoming(bound, bind_end_bb);
        return phi;
    }

    if (is_virtual) {
        llvm::Value* fn_ptr = load_vtable_slot(expr.get_dispatch_base(), ctx_ptr,
                                               expr.get_vtable_slot(), expr.first_lexeme());
        return build_callable_value(*_builder, callable_ty, fn_ptr, ctx_ptr);
    }
    return build_callable_from_function(expr.get_target(), ctx_ptr, expr.first_lexeme());
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
