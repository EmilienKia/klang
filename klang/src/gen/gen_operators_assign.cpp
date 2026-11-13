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

#include "resolvers.hpp"
#include "generators.hpp"
#include "gen_helpers.hpp"
#include "resolvers_scope_lookup.hpp"
#include "../common/operator_names.hpp"
#include "../parse/ast.hpp"

#include "../errors.hpp"
#include "gen_operators_helpers.hpp"

#include "../common/llvm_scope_exit.hpp"

#include <tuple>

namespace k::model::gen {

// ─────────────────────────────────────────────────────────────────────────────
// Value semantics for aggregates (IN-PROGRESS.md, phases F1/F3/F4)
// ─────────────────────────────────────────────────────────────────────────────

bool implementation_generator::is_trivially_copyable(const std::shared_ptr<type>& t) {
    return aggregate_type_is_trivially_copyable(t);
}

bool implementation_generator::cancel_temporary_cleanup(llvm::Value* ptr) {
    for (auto it = _expression_temporaries.rbegin(); it != _expression_temporaries.rend(); ++it) {
        if (it->alloca == ptr) {
            // Convert reverse iterator to forward iterator for erase.
            _expression_temporaries.erase(std::next(it).base());
            return true;
        }
    }
    return false;
}

bool implementation_generator::is_expression_temporary(llvm::Value* ptr) const {
    for (auto& e : _expression_temporaries) {
        if (e.alloca == ptr) return true;
    }
    return false;
}

bool implementation_generator::emit_scalar_union_wrapper_copy(
    llvm::Value* dest, llvm::Value* source, const std::shared_ptr<aggregate>& wrapper,
    bool destroy_dest_first, bool source_const, bool move) {
    auto un = scalar_union_wrapper(wrapper, _unit.get_root_namespace());
    if (!un) return false;
    auto model_type = wrapper->get_struct_type();
    auto* layout = llvm::dyn_cast_or_null<llvm::StructType>(_context->get_llvm_type(model_type));
    std::vector<std::pair<size_t, std::shared_ptr<member_variable_definition>>> fields;
    for (const auto& [name, var] : wrapper->variables()) {
        auto member = std::dynamic_pointer_cast<member_variable_definition>(var);
        if (!member) continue;
        auto field = model_type->get_member(name);
        if (!field) {
            throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F024), {},
                "Missing field layout for union wrapper '{}'", {wrapper->get_fq_name()});
        }
        fields.emplace_back(field->index, member);
    }
    if (!layout || layout->isOpaque() || layout->getNumElements() != fields.size()) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F024), {},
            "Incomplete field layout for union wrapper '{}'", {wrapper->get_fq_name()});
    }
    std::sort(fields.begin(), fields.end(),
        [](const auto& a, const auto& b) { return a.first < b.first; });
    auto* fn = _builder->GetInsertBlock()->getParent();
    auto* copy = llvm::BasicBlock::Create(**_context, "wrapper_copy_fields", fn);
    auto* done = llvm::BasicBlock::Create(**_context, "wrapper_copy_done", fn);
    _builder->CreateCondBr(_builder->CreateICmpEQ(dest, source), done, copy);
    _builder->SetInsertPoint(copy);
    for (size_t i = 0; i < fields.size(); ++i) {
        if (fields[i].first != i) {
            throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F024), {},
                "Invalid field layout for union wrapper '{}'", {wrapper->get_fq_name()});
        }
        auto* dst = _builder->CreateStructGEP(layout, dest, i, "wrapper_copy_dst");
        auto* src = _builder->CreateStructGEP(layout, source, i, "wrapper_copy_src");
        auto field_type = type::canonical(type::remove_const(fields[i].second->get_type()));
        if (type::are_equal(field_type, un->get_struct_type())) {
            if (move) {
                if (destroy_dest_first) emit_union_cleanup(dst, *un);
                uint64_t size = _context->module().getDataLayout().getTypeAllocSize(
                    _context->get_llvm_type(field_type));
                _builder->CreateMemCpy(dst, llvm::MaybeAlign(1), src, llvm::MaybeAlign(1),
                                       _builder->getInt64(size));
            } else {
                emit_union_copy(dst, src, *un, *un, destroy_dest_first, source_const);
            }
        } else if (auto nested = std::dynamic_pointer_cast<struct_type>(field_type)) {
            if (!emit_scalar_union_wrapper_copy(dst, src, nested->get_struct(),
                                                destroy_dest_first, source_const, move)) {
                throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F024), {},
                    "Invalid nested union wrapper '{}'", {wrapper->get_fq_name()});
            }
        } else {
            auto* llvm_type = _context->get_llvm_type(field_type);
            auto* value = _builder->CreateLoad(llvm_type, src);
            value->setAlignment(llvm::Align(1));
            auto* store = _builder->CreateStore(value, dst);
            store->setAlignment(llvm::Align(1));
        }
    }
    _builder->CreateBr(done);
    _builder->SetInsertPoint(done);
    return true;
}

bool implementation_generator::emit_union_payload_copy(
    llvm::Value* dest, llvm::Value* source, const std::shared_ptr<type>& payload_type,
    bool source_const, llvm::Value* allow_empty_default_owner) {
    auto bare = type::canonical(type::remove_const(type::canonical(payload_type)));
    if (type::is_owner(bare)) {
        emit_union_copy_failure();
        return false;
    }
    if (auto callable = std::dynamic_pointer_cast<callable_type>(bare)) {
        if (callable->is_owner()) {
            emit_union_copy_failure();
            return false;
        }
    }
    if (auto array = std::dynamic_pointer_cast<sized_array_type>(bare)) {
        auto* array_type = array->get_llvm_struct_type();
        auto* data_type = array->get_llvm_data_array_type();
        _builder->CreateStore(llvm::ConstantAggregateZero::get(array_type), dest);
        call_argument_cleanup_scope partial(*this, true);
        partial.track_storage(bare, dest);
        auto* src_count_ptr = _builder->CreateStructGEP(
            array_type, source, sized_array_type::FIELD_SIZE, "union_copy_array_src_count");
        auto* dst_count_ptr = _builder->CreateStructGEP(
            array_type, dest, sized_array_type::FIELD_SIZE, "union_copy_array_dst_count");
        auto* src_data = _builder->CreateStructGEP(
            array_type, source, sized_array_type::FIELD_DATA, "union_copy_array_src_data");
        auto* dst_data = _builder->CreateStructGEP(
            array_type, dest, sized_array_type::FIELD_DATA, "union_copy_array_dst_data");
        auto* count = _builder->CreateLoad(_builder->getInt32Ty(), src_count_ptr);
        auto* fn = _builder->GetInsertBlock()->getParent();
        auto* test = llvm::BasicBlock::Create(**_context, "union_copy_array_test", fn);
        auto* copy = llvm::BasicBlock::Create(**_context, "union_copy_array_element", fn);
        auto* done = llvm::BasicBlock::Create(**_context, "union_copy_array_done", fn);
        _builder->CreateBr(test);
        _builder->SetInsertPoint(test);
        auto* index = _builder->CreateLoad(_builder->getInt32Ty(), dst_count_ptr);
        _builder->CreateCondBr(_builder->CreateICmpULT(index, count), copy, done);
        _builder->SetInsertPoint(copy);
        auto* src_element = _builder->CreateInBoundsGEP(data_type, src_data,
            {_builder->getInt32(0), index}, "union_copy_src_element");
        auto* dst_element = _builder->CreateInBoundsGEP(data_type, dst_data,
            {_builder->getInt32(0), index}, "union_copy_dst_element");
        if (emit_union_payload_copy(dst_element, src_element, array->get_subtype(), source_const,
                                    allow_empty_default_owner)) {
            _builder->CreateStore(_builder->CreateAdd(index, _builder->getInt32(1)), dst_count_ptr);
            _builder->CreateBr(test);
        }
        _builder->SetInsertPoint(done);
        partial.transfer();
        return true;
    }
    if (auto st = std::dynamic_pointer_cast<struct_type>(bare)) {
        auto agg = st->get_struct();
        if (!agg) {
            auto un = find_union_by_struct_type(_unit.get_root_namespace(), st);
            if (!un) {
                throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F024), {},
                    "Cannot find union metadata while copying '{}'", {st->to_string()});
            }
            emit_union_copy(dest, source, *un, *un, false, source_const, allow_empty_default_owner);
            return true;
        }

        auto wrapper_copy = agg->get_copy_constructor();
        if ((!wrapper_copy || wrapper_copy->is_compiler_generated())
            && emit_scalar_union_wrapper_copy(dest, source, agg, false, source_const)) return true;

        std::shared_ptr<constructor> selected;
        bool has_copy = false;
        bool selected_const = true;
        for (const auto& ctor : agg->constructors()) {
            if (!ctor || !agg->is_copy_constructor(*ctor)) continue;
            has_copy = true;
            if (ctor->get_parameter_size() != 1 || !ctor->get_parameter(0)) continue;
            auto param = type::canonical(ctor->get_parameter(0)->get_type());
            if (!type::is_reference(param)) continue;
            const bool param_const = type::is_const(param->get_subtype());
            if (source_const && !param_const) continue;
            if (!selected || (selected_const && !param_const)) {
                selected = ctor;
                selected_const = param_const;
            }
        }
        std::vector<std::shared_ptr<function>> access_stack;
        if (_current_function) access_stack.push_back(_current_function);
        const bool accessible = selected &&
            (scope_lookup::is_struct_member_accessible(selected->get_visibility(), *agg, agg, access_stack)
             || scope_lookup::is_friend_of(*agg, access_stack, _unit));
        if ((has_copy && (!selected || selected->is_deleted() || !accessible))
            || (!has_copy && (aggregate_type_has_resource_field(bare)
                              || (agg->is_imported() && agg->get_destructor())))) {
            emit_union_copy_failure();
            return false;
        }
        if (selected) {
            if (selected->is_throws_spec_unknown()) {
                throw_error(static_cast<unsigned int>(k::diag::exception_diag::ERR_UNCAUGHT_EXCEPTION), {},
                    "Union lifecycle constructor '{}' has an unknown exception contract; regenerate its KDI descriptor",
                    {selected->get_fq_name()});
            }
            auto* llvm_ctor = _context->lookup_llvm_function(selected);
            if (!llvm_ctor) {
                throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F031), {},
                    "LLVM declaration not found for union payload copy constructor of '{}'",
                    {st->to_string()});
            }
            create_call_or_invoke(llvm_ctor->getFunctionType(), llvm_ctor, {dest, source});
            return true;
        }
    }
    auto* llvm_type = _context->get_llvm_type(bare);
    uint64_t size = _context->module().getDataLayout().getTypeAllocSize(llvm_type);
    _builder->CreateMemCpy(dest, llvm::MaybeAlign(1), source, llvm::MaybeAlign(1),
                           _builder->getInt64(size));
    return true;
}

void implementation_generator::emit_union_copy(
    llvm::Value* dest, llvm::Value* source, union_type_def& dest_def, union_type_def& source_def,
    bool destroy_dest_first, bool source_const, llvm::Value* allow_empty_default_owner) {
    auto* fn = _builder->GetInsertBlock()->getParent();
    auto* done = llvm::BasicBlock::Create(**_context, "union_copy_done", fn);
    auto* dispatch = llvm::BasicBlock::Create(**_context, "union_copy_dispatch", fn);
    _builder->CreateCondBr(_builder->CreateICmpEQ(dest, source), done, dispatch);
    _builder->SetInsertPoint(dispatch);
    auto* src_type = source_def.get_struct_type()->get_llvm_type();
    auto* dst_type = dest_def.get_struct_type()->get_llvm_type();
    auto* src_disc_ptr = _builder->CreateStructGEP(src_type, source, 0, "union_copy_src_disc");
    auto* dst_disc_ptr = _builder->CreateStructGEP(dst_type, dest, 0, "union_copy_dst_disc");
    auto alternatives = source_def.all_alternatives_ptrs();
    if (destroy_dest_first) {
        auto* prepare = llvm::BasicBlock::Create(**_context, "union_copy_prepare", fn);
        auto* invalid_initial = llvm::BasicBlock::Create(**_context, "union_copy_invalid_initial", fn);
        auto* initial_disc = _builder->CreateLoad(_builder->getInt32Ty(), src_disc_ptr);
        auto* validate = _builder->CreateSwitch(initial_disc, invalid_initial, alternatives.size() + 1);
        validate->addCase(_builder->getInt32(-1), prepare);
        for (const auto* alt : alternatives) {
            if (dest_def.get_alternative_by_global_index(alt->index))
                validate->addCase(_builder->getInt32(alt->index), prepare);
        }
        _builder->SetInsertPoint(invalid_initial);
        _builder->CreateIntrinsic(llvm::Intrinsic::trap, llvm::ArrayRef<llvm::Type*>{}, {});
        _builder->CreateUnreachable();
        _builder->SetInsertPoint(prepare);
        emit_union_cleanup(dest, dest_def);
        _builder->CreateStore(_builder->getInt32(-1), dst_disc_ptr);
        set_union_live_state(dest, dest_def, false);
    }
    // Cleanup can call user destructors that mutate the already evaluated source.
    auto* disc = _builder->CreateLoad(_builder->getInt32Ty(), src_disc_ptr);
    auto* src_state = _builder->CreateLoad(_builder->getInt32Ty(),
        union_live_state_ptr(source, source_def));
    auto* src_payload = _builder->CreateStructGEP(src_type, source, 1, "union_copy_src_payload");
    auto* dst_payload = _builder->CreateStructGEP(dst_type, dest, 1, "union_copy_dst_payload");
    auto* invalid = llvm::BasicBlock::Create(**_context, "union_copy_invalid", fn);
    auto* sw = _builder->CreateSwitch(disc, invalid, alternatives.size());

    for (const auto* alt : alternatives) {
        auto* branch = llvm::BasicBlock::Create(**_context,
            "union_copy_alt" + std::to_string(alt->index), fn);
        sw->addCase(_builder->getInt32(alt->index), branch);
        _builder->SetInsertPoint(branch);
        if (!dest_def.get_alternative_by_global_index(alt->index)) {
            _builder->CreateIntrinsic(llvm::Intrinsic::trap, llvm::ArrayRef<llvm::Type*>{}, {});
            _builder->CreateUnreachable();
            continue;
        }
        auto bare = type::canonical(type::remove_const(type::canonical(alt->resolved_type)));
        if (alt->index == 0 && (type::is_struct(bare) || type::is_sized_array(bare))
            && !has_default_nested_union(source_def)) {
            auto* default_copy = llvm::BasicBlock::Create(**_context, "union_copy_default", fn);
            auto* live_copy = llvm::BasicBlock::Create(**_context, "union_copy_live", fn);
            auto src_struct = std::dynamic_pointer_cast<struct_type>(bare);
            auto src_agg = src_struct ? src_struct->get_struct() : nullptr;
            auto src_copy_ctor = src_agg ? src_agg->get_copy_constructor() : nullptr;
            // An escaped default aggregate without a user copy constructor is copied by value
            // only while none of its resource slots holds an owner.
            const bool escaped_by_value = src_agg
                && union_first_alternative_tracks_default(source_def, _unit.get_root_namespace())
                && (!src_copy_ctor || src_copy_ctor->is_compiler_generated());
            auto* not_default = live_copy;
            if (escaped_by_value) {
                not_default = llvm::BasicBlock::Create(**_context, "union_copy_escaped", fn);
            }
            _builder->CreateCondBr(_builder->CreateICmpEQ(src_state, _builder->getInt32(0)),
                                   default_copy, not_default);
            if (escaped_by_value) {
                _builder->SetInsertPoint(not_default);
                auto* escaped_check = llvm::BasicBlock::Create(**_context, "union_copy_escaped_check", fn);
                _builder->CreateCondBr(_builder->CreateICmpEQ(src_state, _builder->getInt32(2)),
                                       escaped_check, live_copy);
                _builder->SetInsertPoint(escaped_check);
                auto* escaped_failure = llvm::BasicBlock::Create(**_context, "union_copy_escaped_failure", fn);
                _builder->CreateCondBr(emit_default_aggregate_empty(src_payload, src_struct),
                                       default_copy, escaped_failure);
                _builder->SetInsertPoint(escaped_failure);
                emit_union_copy_failure();
            }
            _builder->SetInsertPoint(default_copy);
            _builder->CreateStore(llvm::ConstantAggregateZero::get(dst_type), dest);
            uint64_t size = _context->module().getDataLayout().getTypeAllocSize(_context->get_llvm_type(bare));
            _builder->CreateMemCpy(dst_payload, llvm::MaybeAlign(1), src_payload, llvm::MaybeAlign(1),
                                   _builder->getInt64(size));
            _builder->CreateBr(done);
            _builder->SetInsertPoint(live_copy);
        }
        _builder->CreateStore(_builder->getInt32(-1), dst_disc_ptr);
        set_union_live_state(dest, dest_def, false);
        auto callable = std::dynamic_pointer_cast<callable_type>(bare);
        const bool owning_callable = callable && callable->is_owner();
        if (alt->index == 0 && allow_empty_default_owner
            && (type::is_owner(bare) || owning_callable)) {
            auto* ptr_type = llvm::PointerType::get(_builder->getContext(), 0);
            llvm::Value* is_empty = nullptr;
            if (owning_callable) {
                auto* callable_type = _context->get_or_create_callable_llvm_type();
                auto* fn_ptr = _builder->CreateStructGEP(callable_type, src_payload, 0);
                auto* ctx_ptr = _builder->CreateStructGEP(callable_type, src_payload, 1);
                auto* fn_value = _builder->CreateLoad(ptr_type, fn_ptr);
                auto* ctx_value = _builder->CreateLoad(ptr_type, ctx_ptr);
                fn_value->setAlignment(llvm::Align(1));
                ctx_value->setAlignment(llvm::Align(1));
                is_empty = _builder->CreateAnd(
                    _builder->CreateIsNull(fn_value), _builder->CreateIsNull(ctx_value));
            } else {
                auto* owner_value = _builder->CreateLoad(ptr_type, src_payload);
                owner_value->setAlignment(llvm::Align(1));
                is_empty = _builder->CreateIsNull(owner_value);
            }
            auto* empty_copy = llvm::BasicBlock::Create(**_context, "union_copy_empty_owner", fn);
            auto* failure = llvm::BasicBlock::Create(**_context, "union_copy_owned_failure", fn);
            auto* allowed = _builder->CreateAnd(allow_empty_default_owner,
                _builder->CreateICmpEQ(src_state, _builder->getInt32(0)));
            _builder->CreateCondBr(_builder->CreateAnd(allowed, is_empty), empty_copy, failure);
            _builder->SetInsertPoint(failure);
            emit_union_copy_failure();
            _builder->SetInsertPoint(empty_copy);
            auto* value_type = owning_callable
                ? static_cast<llvm::Type*>(_context->get_or_create_callable_llvm_type())
                : static_cast<llvm::Type*>(ptr_type);
            auto* store = _builder->CreateStore(llvm::Constant::getNullValue(value_type), dst_payload);
            store->setAlignment(llvm::Align(1));
            _builder->CreateStore(disc, dst_disc_ptr);
            _builder->CreateBr(done);
            continue;
        }
        const bool alternative_source_const = source_const || alt->is_const
            || type::is_const(type::canonical(alt->resolved_type));
        auto* allow_nested_empty = allow_empty_default_owner;
        if (alt->index == 0 && has_default_nested_union(source_def)) {
            auto* is_default = _builder->CreateICmpEQ(src_state, _builder->getInt32(0));
            allow_nested_empty = allow_nested_empty
                ? _builder->CreateOr(allow_nested_empty, is_default) : is_default;
        }
        if (emit_union_payload_copy(dst_payload, src_payload, bare, alternative_source_const,
                                    allow_nested_empty)) {
            _builder->CreateStore(disc, dst_disc_ptr);
            // An escaped default source yields a constructed (live) destination.
            _builder->CreateStore(
                _builder->CreateSelect(_builder->CreateICmpEQ(src_state, _builder->getInt32(2)),
                                       _builder->getInt32(1), src_state),
                union_live_state_ptr(dest, dest_def));
            _builder->CreateBr(done);
        }
    }
    _builder->SetInsertPoint(invalid);
    auto* empty = llvm::BasicBlock::Create(**_context, "union_copy_empty", fn);
    auto* invalid_tag = llvm::BasicBlock::Create(**_context, "union_copy_invalid_tag", fn);
    _builder->CreateCondBr(_builder->CreateICmpEQ(disc, _builder->getInt32(-1)), empty, invalid_tag);
    _builder->SetInsertPoint(empty);
    _builder->CreateStore(_builder->getInt32(-1), dst_disc_ptr);
    set_union_live_state(dest, dest_def, false);
    _builder->CreateBr(done);
    _builder->SetInsertPoint(invalid_tag);
    _builder->CreateIntrinsic(llvm::Intrinsic::trap, llvm::ArrayRef<llvm::Type*>{}, {});
    _builder->CreateUnreachable();
    _builder->SetInsertPoint(done);
}

void implementation_generator::emit_value_copy_or_move(llvm::Value* dest, llvm::Value* src,
                                                       const std::shared_ptr<type>& t,
                                                       bool destroy_dest_first,
                                                       const lex::opt_any_lexeme& lexeme,
                                                       const char* copy_context,
                                                       bool named_result_assignment,
                                                       bool allow_move,
                                                       const std::shared_ptr<constructor>& selected_copy,
                                                       bool source_const) {
    auto nt = type::remove_const(type::canonical(t));
    llvm::Type* struct_llvm = _context->get_llvm_type(nt);
    if (!struct_llvm || struct_llvm->isPointerTy()) {
        // Not a real struct payload — fall back to a scalar store.
        _builder->CreateStore(src, dest);
        return;
    }
    const auto& dl = _context->module().getDataLayout();
    uint64_t sz = dl.getTypeAllocSize(struct_llvm);

    auto st_type   = std::dynamic_pointer_cast<struct_type>(nt);
    auto agg       = st_type ? st_type->get_struct() : nullptr;
    if (st_type && !agg) {
        auto un = find_union_by_struct_type(_unit.get_root_namespace(), st_type);
        if (!un) {
            throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F024), {},
                "Cannot find union metadata while assigning '{}'", {st_type->to_string()});
        }
        if (!src->getType()->isPointerTy()) {
            if (destroy_dest_first) emit_union_cleanup(dest, *un);
            _builder->CreateStore(src, dest);
        } else if (allow_move && is_expression_temporary(src)) {
            auto* fn = _builder->GetInsertBlock()->getParent();
            auto* move = llvm::BasicBlock::Create(**_context, "union_move", fn);
            auto* done = llvm::BasicBlock::Create(**_context, "union_move_done", fn);
            _builder->CreateCondBr(_builder->CreateICmpEQ(dest, src), done, move);
            _builder->SetInsertPoint(move);
            if (destroy_dest_first) emit_union_cleanup(dest, *un);
            _builder->CreateMemCpy(dest, llvm::MaybeAlign(), src, llvm::MaybeAlign(),
                                   _builder->getInt64(sz));
            _builder->CreateBr(done);
            _builder->SetInsertPoint(done);
            cancel_temporary_cleanup(src);
        } else {
            emit_union_copy(dest, src, *un, *un, destroy_dest_first, source_const);
        }
        return;
    }
    bool trivially = !selected_copy && is_trivially_copyable(nt);

    const bool src_is_return_value = named_result_assignment && !src->getType()->isPointerTy();
    if (src_is_return_value) {
        auto* func = _builder->GetInsertBlock()->getParent();
        llvm::IRBuilder<> entry_builder(&func->getEntryBlock(), func->getEntryBlock().begin());
        auto* storage = entry_builder.CreateAlloca(struct_llvm, nullptr, "named_result_value");
        _builder->CreateStore(src, storage);
        src = storage;
    }

    llvm::BasicBlock* assignment_done = nullptr;
    llvm::AllocaInst* constructed_flag = nullptr;
    if (named_result_assignment) {
        if (dest == src) return;
        auto* func = _builder->GetInsertBlock()->getParent();
        auto* assignment_body = llvm::BasicBlock::Create(_builder->getContext(), "named_result_assign", func);
        assignment_done = llvm::BasicBlock::Create(_builder->getContext(), "named_result_assigned", func);
        _builder->CreateCondBr(_builder->CreateICmpEQ(dest, src), assignment_done, assignment_body);
        _builder->SetInsertPoint(assignment_body);
        if (auto it = _dtor_flags.find(_nrvo_candidate); it != _dtor_flags.end()) {
            constructed_flag = it->second;
        }
    }
    auto assignment_guard = k::make_scope_exit([&]() {
        if (constructed_flag) {
            _builder->CreateStore(llvm::ConstantInt::getTrue(_builder->getContext()), constructed_flag);
        }
        if (assignment_done) {
            _builder->CreateBr(assignment_done);
            _builder->SetInsertPoint(assignment_done);
        }
    });

    // Is `src` a materialised temporary currently scheduled for destruction?
    // If so, it is a prvalue whose contents we may steal (move).
    bool src_is_tracked_temp = allow_move && src_is_return_value;
    for (auto& e : _expression_temporaries) {
        if (allow_move && e.alloca == src) { src_is_tracked_temp = true; break; }
    }

    auto wrapper_copy = agg ? (selected_copy ? selected_copy : agg->get_copy_constructor()) : nullptr;
    if (agg && (!wrapper_copy || wrapper_copy->is_compiler_generated())
        && src->getType()->isPointerTy()
        && scalar_union_wrapper(agg, _unit.get_root_namespace())) {
        call_argument_cleanup_scope incoming(*this, src_is_tracked_temp && destroy_dest_first);
        if (src_is_tracked_temp && destroy_dest_first) incoming.track_storage(nt, src);
        if (!emit_scalar_union_wrapper_copy(dest, src, agg, destroy_dest_first, source_const,
                                            src_is_tracked_temp)) {
            throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F024), lexeme,
                "Invalid union wrapper '{}'", {agg->get_fq_name()});
        }
        incoming.transfer();
        if (src_is_tracked_temp) cancel_temporary_cleanup(src);
        return;
    }

    // Destroy the previous contents of the destination first (assignment onto an
    // existing, already-constructed object owning resources).
    if (destroy_dest_first && !trivially && agg && agg->get_destructor()) {
        auto dtor = agg->get_destructor();
        auto dtor_it = _context->_functions.find(dtor->shared_as<k::model::function>());
        if (dtor_it != _context->_functions.end()) {
            if (constructed_flag) {
                _builder->CreateStore(llvm::ConstantInt::getFalse(_builder->getContext()), constructed_flag);
            }
            create_call_or_invoke(dtor_it->second->getFunctionType(), dtor_it->second, {dest});
        }
    }

    if (src_is_tracked_temp) {
        // MOVE: transfer the bytes, then cancel the source temporary's destruction
        // so the destination becomes the sole owner (no double free).
        //
        // Checked BEFORE the trivially-copyable fast path: a tracked temporary was
        // registered precisely because its type has a destructor, so it always needs
        // its cleanup cancelled here — even when `is_trivially_copyable` cannot see the
        // destructor (e.g. this particular struct_type instance's weak aggregate link
        // is not populated, so get_struct() returns null and triviality is misjudged).
        _builder->CreateMemCpy(dest, llvm::MaybeAlign(), src, llvm::MaybeAlign(),
                               _builder->getInt64(sz));
        cancel_temporary_cleanup(src);
        return;
    }

    if (agg) {
        auto copy_ctor = selected_copy ? selected_copy : agg->get_copy_constructor();
        if (copy_ctor && copy_ctor->is_deleted()) {
            throw_error(static_cast<unsigned int>(k::diag::codegen_diag::ERR_TYPE_NOT_COPYABLE), lexeme,
                "Type '{}' is not copyable: its copy constructor is deleted",
                {nt ? nt->to_string() : "?"});
        }
    }

    if (trivially) {
        _builder->CreateMemCpy(dest, llvm::MaybeAlign(), src, llvm::MaybeAlign(),
                               _builder->getInt64(sz));
        return;
    }

    // Non-trivial lvalue copy: prefer a user-provided copy constructor.
    if (agg) {
        if (auto cc = selected_copy ? selected_copy : agg->get_copy_constructor()) {
            auto cc_it = _context->_functions.find(cc->shared_as<k::model::function>());
            if (cc_it != _context->_functions.end()) {
                create_call_or_invoke(cc_it->second->getFunctionType(), cc_it->second, {dest, src});
                return;
            }
            throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F031), lexeme,
                "Internal error: LLVM declaration not found for copy constructor of type '{}'; "
                "the declaration pass must register every emitted constructor",
                {nt ? nt->to_string() : "?"});
        }
        // No copy constructor: only reject if the struct holds a resource-referencing
        // field (owner/pointer/link/view, recursively through nested struct members),
        // which cannot be safely shallow-copied.  A struct with a destructor but no
        // such field is copyable via a plain bitwise memcpy — the programmer chose not
        // to provide a copy constructor, implying shallow copy is safe for their
        // semantics (e.g. a destructor that merely updates a counter).
        if (aggregate_type_has_resource_field(nt)) {
            throw_error(static_cast<unsigned int>(k::diag::codegen_diag::ERR_TYPE_NOT_COPYABLE), lexeme,
                "Type '{}' is not copyable during {}: a non-trivial lvalue copy requires a copy constructor",
                {nt ? nt->to_string() : "?", copy_context ? copy_context : "copy"});
        }
        // Fall through to memcpy for non-owner non-trivial structs (destructor exists but
        // shallow copy is safe: no owned heap resources).
    }
    // Shallow bitwise copy for structs with no copy constructor and no resource fields.
    _builder->CreateMemCpy(dest, llvm::MaybeAlign(), src, llvm::MaybeAlign(),
                           _builder->getInt64(sz));
}

void type_reference_resolver::visit_assignation_expression(assignation_expression &expr) {
    // Resolve left operand first to determine the target assignment type
    _replacement_expr = nullptr;
    expr.left()->accept(*this);
    if (_replacement_expr) {
        expr.assign_left(_replacement_expr);
        _replacement_expr = nullptr;
    }

    auto left = expr.left();
    auto left_type = left->get_type();
    std::shared_ptr<type> target_type = nullptr;
    if (left_type) {
        if (type::is_reference(left_type) || type::is_drain(left_type) || type::is_owner(left_type)) {
            target_type = left_type->get_subtype();
        } else {
            target_type = left_type;
        }
    }

    // Resolve right operand with the expected target type context
    _replacement_expr = nullptr;
    {
        target_scope scope(*this, target_type, target_context_kind::ASSIGNMENT_RHS);
        expr.right()->accept(*this);
    }
    if (_replacement_expr) {
        expr.assign_right(_replacement_expr);
        _replacement_expr = nullptr;
    }

    auto right = expr.right();
    auto return_stmt = std::dynamic_pointer_cast<return_statement>(expr.find_statement());
    const bool named_result_assignment = return_stmt && return_stmt->is_named_result_assignment()
        && return_stmt->get_expression().get() == &expr;

    // ── Assignment to a struct rvalue via an operator= overload ──────────────
    // K normally requires the LHS of '=' to be an lvalue (a reference). However a
    // value-returning subscript proxy — e.g. StringBuilder.operator[](i) returning a
    // CharRef whose operator=(char) writes back into the builder — is a common idiom
    // ("sb[i] = c"). When the LHS is a (non-reference) struct that provides an
    // operator= overload, dispatch to it instead of rejecting the assignment.
    if (!type::is_reference(left_type) && type::is_struct(left_type)) {
        auto nc_left = type::remove_const(left_type);
        if (auto st_type = std::dynamic_pointer_cast<struct_type>(nc_left)) {
            if (auto agg = st_type->get_struct()) {
                std::string op_name = get_binary_operator_name(expr);
                if (!op_name.empty() &&
                    !collect_member_operators_from_hierarchy(agg, op_name).empty()) {
                    bool is_const_left = type::is_const(left_type);
                    auto [op_func, adapted_right] =
                        resolve_binary_operator_overload(expr, agg, left, right, is_const_left);
                    if (op_func) {
                        expr.set_operator_func(op_func);
                        if (adapted_right && adapted_right != right) {
                            expr.assign_right(adapted_right);
                        }
                        if (op_func->has_return_type()) {
                            expr.set_type(op_func->get_return_type());
                        } else {
                            expr.set_type(left_type);
                        }
                        if (op_func->is_member()) {
                            auto di = compute_operator_dispatch_info(op_func, left_type);
                            expr.set_operator_dispatch_info(std::move(di));
                        } else {
                            virtual_dispatch_info di;
                            di.kind = virtual_dispatch_info::dispatch_kind::DIRECT;
                            expr.set_operator_dispatch_info(std::move(di));
                        }
                        return;
                    }
                }
            }
        }
    }

    if(!type::is_reference(left_type)) {
        throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_ASSIGN_INCOMPATIBLE), expr.first_lexeme(),
            "The left operand of an assignment must be assignable (an lvalue): "
            "the left-hand side has type '{}' which is not a reference; "
            "you can only assign to a variable, parameter, or array element",
            {left_type ? left_type->to_string() : "?"});
    }
    auto ref_target_type = std::dynamic_pointer_cast<reference_type>(left_type);
    target_type = ref_target_type->get_subtype();

    // ── Const-check ──────────────────────────────────────────────────────────
    // If the target type is const-qualified (ref<const T>), assignment is forbidden.
    if (type::is_const(target_type)) {
        throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_OVERLOAD_CALL_NO_MATCH), expr.first_lexeme(),
            "Cannot assign to a const variable: "
            "the left-hand side has type '{}' which is const; "
            "const variables cannot be modified after initialisation",
            {target_type ? target_type->to_string() : "?"});
    }
    // ─────────────────────────────────────────────────────────────────────────

    // A typedef is a distinct type: assigning a base-typed expression to it
    // requires an explicit cast (unless the expression is a literal or is
    // already tainted by the very same typedef).
    check_strong_alias_conversion(right, target_type,
                                  alias_conv_site::ASSIGNMENT, expr.first_lexeme());

    // Aliases never influence assignment semantics: from here on the canonical
    // (alias-free) target type is used.
    target_type = type::canonical(target_type);

    // Step 1: Resolve left and right sub-expressions
    if(type::is_reference(target_type)) {
        // Left hand is ref-to-ref: assignment acts on the underlying object.
        left = load_value_expression::make_shared(left);
        left->set_type(target_type);
        expr.assign_left(left);
        target_type = std::dynamic_pointer_cast<reference_type>(target_type)->get_subtype();
    } else if (type::is_link(target_type)) {
        // Left hand is ref-to-link.
        // Determine if this is a rebind (RHS is an indirection) or
        // an assignment to the pointed object (RHS is a value).
        auto link_subtype = std::dynamic_pointer_cast<link_type>(target_type)->get_linked_type();
        auto rhs_type = right->get_type();
        // Unwrap ref<indirection> from rhs_type
        auto rhs_effective = rhs_type;
        if (type::is_reference(rhs_type)) {
            auto inner = std::dynamic_pointer_cast<reference_type>(rhs_type)->get_subtype();
            if (type::is_link(inner) || type::is_pointer(inner) || type::is_view(inner)) {
                rhs_effective = inner;
            }
        }
        // Helper lambda: check if rhs_effective is an indirection compatible with link_subtype (same, static upcast, or dynamic downcast)
        auto is_rebind_compatible = [&]() -> bool {
            if (!type::is_any_indirection(rhs_effective) || !rhs_effective->get_subtype() || !link_subtype)
                return false;
            auto rhs_sub_nc = type::remove_const(rhs_effective->get_subtype());
            auto lnk_sub_nc = type::remove_const(link_subtype);
            if (type::are_equal(rhs_sub_nc, lnk_sub_nc)) return true;
            auto src_st = std::dynamic_pointer_cast<struct_type>(rhs_sub_nc);
            auto tgt_st = std::dynamic_pointer_cast<struct_type>(lnk_sub_nc);
            if (!src_st || !tgt_st || !src_st->get_struct() || !tgt_st->get_struct()) return false;
            // Static upcast: rhs points to Derived, link points to Base
            if (src_st->get_struct()->is_derived_from(tgt_st->get_struct())) return true;
            // Dynamic downcast: rhs points to Base, link points to Derived (klass/interface only)
            if (tgt_st->get_struct()->is_derived_from(src_st->get_struct()) &&
                tgt_st->get_struct()->has_rtti() &&
                std::dynamic_pointer_cast<klass>(tgt_st->get_struct()) != nullptr) return true;
            return false;
        };
        // If RHS is an indirection compatible (same or upcast) with link_subtype: REBIND
        if (is_rebind_compatible()) {
            // Rebind: check const compatibility (const T~ ← T~ is OK; T~ ← const T~ is not)
            if (type::is_const(rhs_effective->get_subtype()) && !type::is_const(link_subtype)) {
                throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_OVERLOAD_ARG_TYPE_MISMATCH), expr.first_lexeme(),
                    "Cannot rebind a link-to-mutable ('{}') from a link-to-const ('{}'): "
                    "this would allow modification of a const object",
                    {target_type ? target_type->to_string() : "?",
                     rhs_type ? rhs_type->to_string() : "?"});
            }
            // Rebind: load the source address and store into the link alloca.
            // If source is nullable, warn — null-check at IR level.
            if (type::is_nullable_indirection(rhs_effective)) {
                auto diag = k::log::diagnostic::make_warning(static_cast<unsigned int>(k::diag::operator_diag::WARN_IMPLICIT_LOSSY_CAST),
                    "Rebinding a link from a nullable indirection (type '{}'): "
                    "a runtime null-check will be inserted",
                    {rhs_type ? rhs_type->to_string() : "?"});
                logger_relay::report(diag);
            }
            // Unwrap the ref wrapper from rhs if needed
            if (type::is_reference(rhs_type)) {
                right = load_value_expression::make_shared(right);
                rhs_type = rhs_effective;
                right->set_type(rhs_type);
                expr.assign_right(right);
            }
            // Determine whether to use static upcast or dynamic downcast
            {
                auto rhs_sub_nc = type::remove_const(right->get_type()->get_subtype());
                auto lnk_sub_nc = type::remove_const(link_subtype);
                if (!type::are_equal(rhs_sub_nc, lnk_sub_nc)) {
                    auto src_st = std::dynamic_pointer_cast<struct_type>(rhs_sub_nc);
                    auto tgt_st = std::dynamic_pointer_cast<struct_type>(lnk_sub_nc);
                    bool is_static_upcast = src_st && tgt_st &&
                        src_st->get_struct() && tgt_st->get_struct() &&
                        src_st->get_struct()->is_derived_from(tgt_st->get_struct());
                    if (is_static_upcast) {
                        auto upcast = cast_expression::make_shared(right, target_type);
                        upcast->set_type(target_type);
                        expr.assign_right(upcast);
                    } else {
                        // Dynamic downcast — lien is non-null, so fatal on null result
                        auto dc = cast_expression::make_shared(right, target_type, /*null_is_fatal=*/true);
                        expr.assign_right(dc);
                    }
                }
            }
            // The assignment stores a new address into the link alloca.
            expr.set_type(ref_target_type);
            return;
        }
        // Otherwise: transparent reference — assignment to the pointed object.
        left = load_value_expression::make_shared(left);
        left->set_type(target_type);
        auto ref_to_target = link_subtype->get_reference();
        left->set_type(ref_to_target);
        expr.assign_left(left);
        target_type = link_subtype;
        ref_target_type = ref_to_target;
    } else if (type::is_view(target_type)) {
        throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_LOGICAL_NOT_BOOL), expr.first_lexeme(),
            "Cannot assign to a view indirection (type '{}'): "
            "a view ('?') is immutable after initialisation",
            {target_type ? target_type->to_string() : "?"});
    }

    auto source_type = right->get_type();

    // Unwrap ref<link/ptr/pin/owner> for source-side checks
    auto effective_source_type = source_type;
    if (type::is_reference(source_type)) {
        auto inner = std::dynamic_pointer_cast<reference_type>(source_type)->get_subtype();
        // The referenced type may be const-qualified (e.g. a `const T&` parameter
        // where T is itself a pointer/link/view/owner, as produced by the const
        // Collection API for `Vector<Object*>`).  Strip the const before the
        // indirection-kind check: copying a const *pointer value* into a mutable
        // pointer is legal — only the pointer is read, the pointee's const-ness is
        // unaffected (like `Object* const` → `Object*` in C++).
        auto inner_nc = type::remove_const(inner);
        if (type::is_link(inner_nc) || type::is_pointer(inner_nc) || type::is_view(inner_nc) || type::is_owner(inner_nc)) {
            effective_source_type = inner_nc;
        }
    }

    if(type::is_pointer(target_type)) {
        // Null literal: always compatible with any pointer type.
        if(type::is_null(effective_source_type) || type::is_null(source_type)) {
            expr.set_type(ref_target_type);
            return;
        }
        if(type::is_pointer(effective_source_type) || type::is_link(effective_source_type)
           || type::is_view(effective_source_type) || type::is_owner(effective_source_type)) {
            auto src_sub = effective_source_type->get_subtype();
            auto tgt_sub = target_type->get_subtype();
            // Strip const from both sides for structural comparison
            auto src_sub_nc = type::remove_const(src_sub);
            auto tgt_sub_nc = type::remove_const(tgt_sub);
            if (!type::are_equal(src_sub_nc, tgt_sub_nc)) {
                // Check static upcast: ptr<Derived>→ptr<Base>
                auto src_st = std::dynamic_pointer_cast<struct_type>(src_sub_nc);
                auto tgt_st = std::dynamic_pointer_cast<struct_type>(tgt_sub_nc);
                bool is_static_upcast = src_st && tgt_st &&
                                 src_st->get_struct() && tgt_st->get_struct() &&
                                 src_st->get_struct()->is_derived_from(tgt_st->get_struct());
                bool is_dynamic_downcast = !is_static_upcast && src_st && tgt_st &&
                                 src_st->get_struct() && tgt_st->get_struct() &&
                                 tgt_st->get_struct()->has_rtti();
                                 std::dynamic_pointer_cast<klass>(tgt_st->get_struct()) != nullptr;
                if (!is_static_upcast && !is_dynamic_downcast) {
                    throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_ASSIGN_TO_CONST), expr.first_lexeme(),
                        "Pointer assignment type mismatch: "
                        "cannot assign a '{}' to a '{}'; pointer subtypes must match "
                        "or source must be a derived type of the target",
                        {source_type ? source_type->to_string() : "?",
                         target_type ? target_type->to_string() : "?"});
                }
                // Forbid const T* → T* (would lose const-ness on pointed object)
                if (type::is_const(src_sub) && !type::is_const(tgt_sub)) {
                    throw_error(static_cast<unsigned int>(k::diag::codegen_diag::ERR_GEN_FUNC_OVERLOAD_AMBIGUOUS), expr.first_lexeme(),
                        "Cannot assign a pointer-to-const ('{}') to a pointer-to-mutable ('{}'): "
                        "this would allow modification of a const object through the mutable pointer",
                        {source_type ? source_type->to_string() : "?",
                         target_type ? target_type->to_string() : "?"});
                }
                // Unwrap ref wrapper if needed
                if (type::is_reference(source_type)) {
                    right = load_value_expression::make_shared(right);
                    source_type = std::dynamic_pointer_cast<reference_type>(source_type)->get_subtype();
                    right->set_type(source_type);
                    expr.assign_right(right);
                }
                if (is_static_upcast) {
                    auto upcast = cast_expression::make_shared(right, target_type);
                    upcast->set_type(target_type);
                    expr.assign_right(upcast);
                } else {
                    // Dynamic downcast — ptr can be null, not fatal
                    auto dc = cast_expression::make_shared(right, target_type, /*null_is_fatal=*/false);
                    expr.assign_right(dc);
                }
                expr.set_type(ref_target_type);
                return;
            }
            // Forbid const T* → T* (would lose const-ness on pointed object)
            if (type::is_const(src_sub) && !type::is_const(tgt_sub)) {
                throw_error(static_cast<unsigned int>(k::diag::codegen_diag::ERR_GEN_FUNC_OVERLOAD_AMBIGUOUS), expr.first_lexeme(),
                    "Cannot assign a pointer-to-const ('{}') to a pointer-to-mutable ('{}'): "
                    "this would allow modification of a const object through the mutable pointer",
                    {source_type ? source_type->to_string() : "?",
                     target_type ? target_type->to_string() : "?"});
            }
            if (type::is_reference(source_type)) {
                right = load_value_expression::make_shared(right);
                source_type = std::dynamic_pointer_cast<reference_type>(source_type)->get_subtype();
                right->set_type(source_type);
                expr.assign_right(right);
            }
            expr.set_type(ref_target_type);
            return;
        } else {
            throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_MAIN_WRONG_RETURN_TYPE), expr.first_lexeme(),
                "Pointer assignment requires a pointer or link on the right-hand side: "
                "cannot assign a value of type '{}' to a pointer of type '{}'",
                {source_type ? source_type->to_string() : "?",
                 target_type ? target_type->to_string() : "?"});
        }
    } else if (type::is_link(target_type)) {
        // Direct link rebind (reached after link-to-link case not matched above).
        if (!type::is_any_indirection(effective_source_type)) {
            throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_LOGICAL_AND_INCOMPATIBLE), expr.first_lexeme(),
                "Link assignment requires an indirection on the right-hand side, "
                "but got type '{}'",
                {source_type ? source_type->to_string() : "?"});
        }
        if (type::is_nullable_indirection(effective_source_type)) {
            auto diag = k::log::diagnostic::make_warning(static_cast<unsigned int>(k::diag::operator_diag::WARN_IMPLICIT_LOSSY_CAST),
                "Assigning a nullable indirection (type '{}') to a link: "
                "a runtime null-check will be inserted",
                {source_type ? source_type->to_string() : "?"});
            logger_relay::report(diag);
        }
        if (type::is_reference(source_type)) {
            right = load_value_expression::make_shared(right);
            source_type = std::dynamic_pointer_cast<reference_type>(source_type)->get_subtype();
            right->set_type(source_type);
            expr.assign_right(right);
        }
        expr.set_type(ref_target_type);
        return;
    } else if (type::is_sized_array(target_type)) {
        // Array = array : element-wise copy (see spec).
        // Source must be a reference to a sized array of the same element type.
        auto dest_arr = std::dynamic_pointer_cast<sized_array_type>(target_type);
        std::shared_ptr<type> src_inner_type = source_type;
        if (type::is_reference(source_type)) {
            src_inner_type = std::dynamic_pointer_cast<reference_type>(source_type)->get_subtype();
        }
        if (!type::is_sized_array(src_inner_type)) {
            throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_SHIFT_NOT_INT), expr.first_lexeme(),
                "Array assignment: the right-hand side must be an array of the same element type, "
                "but '{}' is not a sized array",
                {source_type ? source_type->to_string() : "?"});
        }
        auto src_arr = std::dynamic_pointer_cast<sized_array_type>(src_inner_type);
        if (!type::are_equal(dest_arr->get_subtype(), src_arr->get_subtype())) {
            throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_SHIFT_INCOMPATIBLE), expr.first_lexeme(),
                "Array assignment: element type mismatch — cannot copy from '{}' to '{}'",
                {source_type ? source_type->to_string() : "?",
                 target_type ? target_type->to_string() : "?"});
        }
        auto member = std::dynamic_pointer_cast<member_of_object_expression>(left);
        auto parent_type = member && member->sub_expr() ? member->sub_expr()->get_type() : nullptr;
        if (type::is_reference(parent_type)) parent_type = parent_type->get_subtype();
        auto parent_struct = std::dynamic_pointer_cast<struct_type>(
            type::canonical(type::remove_const(parent_type)));
        if (parent_struct && !parent_struct->get_struct()
            && find_union_by_struct_type(_unit.get_root_namespace(), parent_struct)
            && !std::dynamic_pointer_cast<temporary_construction_expression>(right)) {
            const bool source_const = type::is_reference(source_type)
                && type::is_const(source_type->get_subtype());
            check_union_payload_copy_contract(dest_arr, source_const, expr.first_lexeme());
        }
        // Type of the assignment expression is ref<dest array>
        expr.set_type(ref_target_type);
        // Ensure the source is referenced (if it isn't already)
        if (!type::is_reference(source_type)) {
            right = load_value_expression::make_shared(right);
            right->set_type(source_type->get_reference());
            expr.assign_right(right);
        }
        return; // code generation handled in visit_simple_assignation_expression
    } else if (type::is_callable(target_type)) {
        // Assigning a function address (or another frt variable) to a function-pointer variable.
        // target_type is a callable_type; source should be ref<frt> (function symbol)
        // or frt itself (another variable). The ref wrapper is stripped below if present.
        //
        // Check: only mutable addressers are rebindable. `*` and `+` may be rebound;
        // `?` (view) and `&` (reference) are immutable bindings, and a bare prototype
        // is not an instantiable value type at all.
        auto frt_target = std::dynamic_pointer_cast<callable_type>(target_type);
        if (frt_target && !frt_target->is_rebindable()) {
            throw_error(static_cast<unsigned int>(k::diag::callable_diag::ERR_CALLABLE_NOT_REBINDABLE), expr.first_lexeme(),
                "Cannot assign to an immutable callable (type '{}'): "
                "only pointer (*) and link (+) callables are rebindable",
                {target_type ? target_type->to_string() : "?"});
        }
        // Everything else (member/functor/functional-interface binding, free
        // function symbol selection, callable→callable conversion and `null`)
        // goes through the single adaptation funnel, which enforces the phase B.7
        // co/contravariance and `throws`-subset rules.
        if (frt_target && !frt_target->is_unbound_member()) {
            // A nullable source rebound into a non-null (`+`) callable is guarded
            // by a runtime null-check emitted by the implementation generator.
            if (!frt_target->is_nullable()) {
                auto eff_src = source_type;
                if (type::is_reference(eff_src)) {
                    eff_src = type::remove_const(
                        std::dynamic_pointer_cast<reference_type>(eff_src)->get_subtype());
                }
                if (auto src_ct = std::dynamic_pointer_cast<callable_type>(eff_src)) {
                    if (src_ct->is_nullable()) {
                        auto diag = k::log::diagnostic::make_warning(static_cast<unsigned int>(k::diag::operator_diag::WARN_IMPLICIT_LOSSY_CAST),
                            "Rebinding a non-null callable from a nullable callable (type '{}'): "
                            "a runtime null-check will be inserted",
                            {src_ct->to_string()});
                        logger_relay::report(diag);
                    }
                }
            }
            auto adapted = adapt_type(right, frt_target);
            if (!adapted) {
                throw_error(static_cast<unsigned int>(k::diag::callable_diag::ERR_CALLABLE_INCOMPATIBLE_SIGNATURE), expr.first_lexeme(),
                    "Cannot assign an expression of type '{}' to the callable type '{}'",
                    {source_type ? source_type->to_string() : "?",
                     target_type ? target_type->to_string() : "?"});
            }
            expr.assign_right(adapted);
            expr.set_type(ref_target_type);
            return;
        }
        // Unbound member function reference: keeps the historical bare
        // function-pointer representation produced directly by impl_gen.
        if (type::is_reference(source_type)) {
            auto inner = std::dynamic_pointer_cast<reference_type>(source_type)->get_subtype();
            if (type::is_callable(inner)) {
                auto rhs_sym = std::dynamic_pointer_cast<symbol_expression>(right);
                if (!rhs_sym || !rhs_sym->is_function()) {
                    // Variable of frt type: load the stored function pointer from the alloca
                    right = load_value_expression::make_shared(right);
                    source_type = inner;
                    right->set_type(source_type);
                    expr.assign_right(right);
                }
            }
        }
        expr.set_type(ref_target_type);
        return;
    } else if (type::is_owner(target_type)) {
        // ── Owner assignment: destroy old object (if any), transfer ownership ─────
        //   - null literal    → destroy current + set null
        //   - ref<owner<T>>  → move (load + null source), same or compatible subtype
        //   - owner<T>       → direct (from new_expression or already an owner value)

        // Detect null literal (both parsed 'null' and programmatic nullptr)
        bool rhs_is_null = type::is_null(source_type);
        if (!rhs_is_null) {
            if (auto ve = std::dynamic_pointer_cast<value_expression>(right)) {
                if (ve->is_literal() && ve->any_literal().has_value()) {
                    rhs_is_null = std::holds_alternative<lex::null>(ve->any_literal());
                } else {
                    rhs_is_null = std::holds_alternative<std::nullptr_t>(ve->get_value());
                }
            }
        }
        if (rhs_is_null) {
            // Assign null: destroy current owned object and store null
            expr.set_type(ref_target_type);
            return;
        }

        if (type::is_reference(source_type)) {
            auto inner = std::dynamic_pointer_cast<reference_type>(source_type)->get_subtype();
            // The referenced owner may be const-qualified (e.g. a `const T&` parameter
            // where T is an owner, as produced by the const Collection API for
            // `LinkedList<Object!>`).  Strip the const for the owner-kind check: the
            // owner is still moved (source consumed / nulled) — the const only guards
            // the pointee, not the ownership transfer of the reference binding.
            auto inner_nc = type::remove_const(inner);
            if (type::is_owner(inner_nc)) {
                // Move: wrap source in owner_move_expression (load + null source alloca)
                auto own_src_nc = type::remove_const(inner_nc->get_subtype());
                auto own_tgt_nc = type::remove_const(target_type->get_subtype());
                auto move = owner_move_expression::make_shared(right);
                move->set_type(inner_nc);
                std::shared_ptr<expression> new_right = move;
                if (!same_conversion_type(own_src_nc, own_tgt_nc)) {
                    // Check upcast: owner<Derived> → owner<Base>
                    auto src_st = std::dynamic_pointer_cast<struct_type>(own_src_nc);
                    auto tgt_st = std::dynamic_pointer_cast<struct_type>(own_tgt_nc);
                    if (!src_st || !tgt_st || !src_st->get_struct() || !tgt_st->get_struct() ||
                        !src_st->get_struct()->is_derived_from(tgt_st->get_struct())) {
                        throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_SUBSCRIPT_OVERLOAD_CONST), expr.first_lexeme(),
                            "Owner assignment type mismatch: cannot move '{}' into '{}'",
                            {source_type->to_string(), target_type->to_string()});
                    }
                    auto upcast = cast_expression::make_shared(move, target_type);
                    upcast->set_type(target_type);
                    new_right = upcast;
                }
                expr.assign_right(new_right);
                expr.set_type(ref_target_type);
                return;
            }
        }
        if (type::is_owner(source_type)) {
            // Direct owner value (e.g. from new_expression or already an owner_move_expression)
            expr.set_type(ref_target_type);
            return;
        }
        throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_SUBSCRIPT_OVERLOAD_NOT_FOUND), expr.first_lexeme(),
            "Owner assignment: right-hand side must be an owner value, "
            "another owner variable (move), or null; got type '{}'",
            {source_type ? source_type->to_string() : "?"});
    } else if(type::is_struct(target_type)) {
        // ── Struct assignment: try operator overload ──
        auto nc_target = type::remove_const(target_type);
        auto st_type = std::dynamic_pointer_cast<struct_type>(nc_target);
        if (st_type) {
            auto agg = st_type->get_struct();
            if (agg) {
                bool is_const_left = type::is_const(target_type);
                auto [op_func, adapted_right] = resolve_binary_operator_overload(expr, agg, left, right, is_const_left);
                auto rhs_value_type = source_type;
                if (type::is_reference(rhs_value_type)) rhs_value_type = rhs_value_type->get_subtype();
                auto wrapper_copy = agg->get_copy_constructor();
                if (op_func && op_func->is_compiler_generated()
                    && op_func->get_short_name() == "__operator_aS_"
                    && (!wrapper_copy || wrapper_copy->is_compiler_generated())
                    && type::is_reference(source_type)
                    && scalar_union_wrapper(agg, _unit.get_root_namespace())
                    && type::are_equal(type::canonical(type::remove_const(rhs_value_type)),
                                       type::canonical(type::remove_const(target_type)))) {
                    if (!std::dynamic_pointer_cast<temporary_construction_expression>(right)) {
                        check_union_payload_copy_contract(st_type,
                            type::is_const(source_type->get_subtype()), expr.first_lexeme());
                    }
                    expr.set_type(ref_target_type);
                    return;
                }
                if (op_func && !(named_result_assignment && op_func->is_compiler_generated()
                                 && op_func->get_short_name() == "__operator_aS_")) {
                    if (op_func->is_compiler_generated()
                        && op_func->get_short_name() == "__operator_aS_") {
                        if (auto un = scalar_union_wrapper(agg, _unit.get_root_namespace())) {
                            // The synthesized assignment takes a const reference.
                            check_union_payload_copy_contract(un->get_struct_type(), true,
                                                              expr.first_lexeme());
                        }
                    }
                    // An inactive union alternative must be constructed even
                    // when assignment to an already-active one uses operator=.
                    auto member = std::dynamic_pointer_cast<member_of_object_expression>(left);
                    auto union_type = member && member->sub_expr() ? member->sub_expr()->get_type() : nullptr;
                    if (type::is_reference(union_type)) union_type = union_type->get_subtype();
                    auto union_st = std::dynamic_pointer_cast<struct_type>(type::remove_const(union_type));
                    if (union_st && !union_st->get_struct()
                        && find_union_by_struct_type(_unit.get_root_namespace(), union_st)
                        && type::is_reference(source_type)
                        && !std::dynamic_pointer_cast<temporary_construction_expression>(right)) {
                        auto first_copy = agg->get_copy_constructor();
                        if (first_copy && !first_copy->is_generated_bytewise_copy()) {
                            std::vector<std::shared_ptr<constructor>> copies;
                            for (const auto& ctor : agg->constructors()) {
                                if (ctor && agg->is_copy_constructor(*ctor)) copies.push_back(ctor);
                            }
                            auto [selected, adapted] = get_best_matching_constructor(
                                copies, {right}, true, expr.first_lexeme());
                            check_constructor_visibility(*selected, expr);
                            check_union_lifecycle_constructor_contract(*selected, expr.first_lexeme());
                            check_call_contract(*selected, expr.first_lexeme());
                            expr.set_copy_constructor(selected);
                        }
                    }
                    // Store the resolved operator function on the expression
                    expr.set_operator_func(op_func);
                    // Apply the adapted right operand (implicit cast if needed)
                    if (adapted_right && adapted_right != right) {
                        expr.assign_right(adapted_right);
                    }
                    // Set the expression type to the return type of the operator function
                    if (op_func->has_return_type()) {
                        expr.set_type(op_func->get_return_type());
                    } else {
                        expr.set_type(ref_target_type);
                    }
                    // Compute dispatch info for virtual calls
                    if (op_func->is_member()) {
                        auto di = compute_operator_dispatch_info(op_func, left_type);
                        expr.set_operator_dispatch_info(std::move(di));
                    } else {
                        virtual_dispatch_info di;
                        di.kind = virtual_dispatch_info::dispatch_kind::DIRECT;
                        expr.set_operator_dispatch_info(std::move(di));
                    }
                    return;
                }
            } else {
                // No owning aggregate → this is a union struct type.
                // Check if the source is also a union type and if there is an inheritance
                // relationship (upcast: derived→base, or downcast: base→derived).
                auto src_inner = source_type;
                if (type::is_reference(src_inner)) {
                    src_inner = std::dynamic_pointer_cast<reference_type>(src_inner)->get_subtype();
                }
                src_inner = type::canonical(type::remove_const(src_inner));
                if (auto src_st = std::dynamic_pointer_cast<struct_type>(src_inner)) {
                    if (!src_st->get_struct()
                        && type::are_equal(type::canonical(type::remove_const(src_st)),
                                           type::canonical(type::remove_const(st_type)))) {
                        if (!std::dynamic_pointer_cast<temporary_construction_expression>(right)) {
                            const bool source_const = type::is_reference(source_type)
                                && type::is_const(source_type->get_subtype());
                            check_union_payload_copy_contract(src_st, source_const, expr.first_lexeme());
                        }
                        expr.set_type(ref_target_type);
                        return;
                    }
                    if (!src_st->get_struct() && src_st != st_type) {
                        // Both are union struct types and they differ: check inheritance.
                        auto root_ns = _unit.get_root_namespace();
                        std::shared_ptr<union_type_def> lhs_udef, rhs_udef;
                        if (root_ns) {
                            lhs_udef = find_union_by_struct_type(root_ns, st_type);
                            rhs_udef = find_union_by_struct_type(root_ns, src_st);
                        }
                        if (lhs_udef && rhs_udef) {
                            bool upcast   = scope_lookup::is_base_union_of(*lhs_udef, *rhs_udef);
                            bool downcast = scope_lookup::is_base_union_of(*rhs_udef, *lhs_udef);
                            if (!upcast && !downcast) {
                                throw_error(static_cast<unsigned int>(k::diag::union_diag::ERR_UNION_ASSIGN_TYPE_MISMATCH),
                                    expr.first_lexeme(),
                                    "Cannot assign union of type '{}' to union of type '{}': "
                                    "the two union types are unrelated (no inheritance relationship)",
                                    {src_inner->to_string(), target_type->to_string()});
                            }
                            // Allow the assignment; do NOT wrap the source in load_value_expression
                            // so that codegen sees both as pointers (needed for field-by-field copy).
                            // Keep source_type as ref<union> so codegen receives the pointer.
                            if (!std::dynamic_pointer_cast<temporary_construction_expression>(right)) {
                                const bool source_const = type::is_reference(source_type)
                                    && type::is_const(source_type->get_subtype());
                                check_union_payload_copy_contract(src_st, source_const, expr.first_lexeme());
                            }
                            expr.set_type(ref_target_type);
                            return;
                        }
                    }
                }
            }
        }
        // No operator overload found — fall through to default struct assignment (memcpy/store).
        expr.set_type(ref_target_type);
        if (named_result_assignment) {
            auto src_value_type = source_type;
            if (type::is_reference(src_value_type)) {
                src_value_type = src_value_type->get_subtype();
            }
            if (!type::are_equal(type::canonical(type::remove_const(src_value_type)),
                                 type::canonical(type::remove_const(target_type)))) {
                throw_error(static_cast<unsigned int>(k::diag::statement_diag::ERR_RETURN_TYPE_MISMATCH),
                            expr.first_lexeme(), "Return expression type must be compatible to the expected function return type");
            }
            if (type::is_reference(source_type) && st_type && st_type->get_struct()) {
                auto owner = st_type->get_struct();
                auto first_copy = owner->get_copy_constructor();
                if ((!first_copy || first_copy->is_compiler_generated())
                    && !std::dynamic_pointer_cast<temporary_construction_expression>(right)
                    && scalar_union_wrapper(owner, _unit.get_root_namespace())) {
                    check_union_payload_copy_contract(st_type,
                        type::is_const(source_type->get_subtype()), expr.first_lexeme());
                }
                if (first_copy && !first_copy->is_generated_bytewise_copy()) {
                    std::vector<std::shared_ptr<constructor>> copies;
                    for (const auto& ctor : owner->constructors()) {
                        if (ctor && owner->is_copy_constructor(*ctor)) copies.push_back(ctor);
                    }
                    auto [selected, adapted] = get_best_matching_constructor(
                        copies, {right}, true, expr.first_lexeme());
                    check_constructor_visibility(*selected, expr);
                    check_call_contract(*selected, expr.first_lexeme());
                    expr.set_copy_constructor(selected);
                    if (adapted.front() != right) expr.assign_right(adapted.front());
                }
            }
            // Keep an lvalue's address: loading it by value would lose both
            // self-alias detection and copy-constructor lifetime semantics.
            return;
        }
        // If source type is reference, deref it
        if(type::is_reference(source_type)) {
            right = adapt_reference_load_value(right);
            if (auto member = std::dynamic_pointer_cast<member_of_object_expression>(left)) {
                auto parent_type = member->sub_expr()->get_type();
                if (type::is_reference(parent_type)) parent_type = parent_type->get_subtype();
                auto parent_struct = std::dynamic_pointer_cast<struct_type>(
                    type::canonical(type::remove_const(parent_type)));
                if (parent_struct && !parent_struct->get_struct()
                    && find_union_by_struct_type(_unit.get_root_namespace(), parent_struct)) {
                    if (auto load = std::dynamic_pointer_cast<load_value_expression>(right)) {
                        if (auto selected = load->get_value_constructor())
                            check_union_lifecycle_constructor_contract(*selected, expr.first_lexeme());
                    }
                }
            }
            source_type = right->get_type();
            expr.assign_right(right);
        }
        return;
    } else if(!type::is_primitive(target_type) && !type::is_enum(target_type)) {
        throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_MAIN_WRONG_PARAMS), expr.first_lexeme(),
            "Assignment to a non-primitive, non-pointer type is not yet supported: "
            "the target has type '{}'; only assignments to primitive types, pointers and arrays are supported",
            {target_type ? target_type->to_string() : "?"});
    }

    // Type of an assignation is a reference
    expr.set_type(ref_target_type);

    // Step 2: Validate that the left operand is assignable (reference, not const)
    // If source type is reference, deref it
    if(type::is_reference(source_type)) {
        // Source type must be de-referenced
        right = load_value_expression::make_shared(right);
        source_type = std::dynamic_pointer_cast<reference_type>(source_type)->get_subtype();
        right->set_type(source_type);
        expr.assign_right(right);
    }

    // Step 3: For struct types: check for operator= overload or direct copy
    // Step 6: For primitives: adapt right operand type to match left
    // TODO Promote to largest target_type instead to align to left operand.
    auto cast = adapt_type(right, target_type);
    if(!cast) {
        throw_error(static_cast<unsigned int>(k::diag::statement_diag::ERR_IF_COND_NOT_BOOL), expr.first_lexeme(),
            "Incompatible types in assignment: "
            "the right-hand side of type '{}' cannot be implicitly converted to the target type '{}'; "
            "use an explicit cast if a narrowing conversion is intended",
            {right->get_type() ? right->get_type()->to_string() : "?",
             target_type ? target_type->to_string() : "?"});
    } else if(cast != right) {
        // Casted, assign casted expression instead of right source.
        expr.assign_right(cast);
    } else {
        // Compatible target_type, no need to cast.
    }
}

//
// Simple assignment expression (=)
//

implementation_generator::place_result implementation_generator::evaluate_assignment_place(expression& expr) {
    auto result = evaluate_expression(expr);
    // The resolver uses a load to follow a reference or link field on the LHS.
    // Its loaded pointer is the destination place, not the value being assigned.
    if (dynamic_cast<load_value_expression*>(&expr) && type::is_reference(expr.get_type())) {
        if (auto* address = std::get_if<value_result>(&result)) {
            return dereference_address(*address);
        }
    }
    return require_place(result, expr);
}

std::pair<implementation_generator::place_result, llvm::Value*>
implementation_generator::process_assignment_operands(assignation_expression& expr) {
    auto destination = evaluate_assignment_place(*expr.left());
    auto source = evaluate_expression(*expr.right());
    auto* right = checked_legacy_result(source);
    _value = nullptr;
    return {destination, right};
}

/**
 * Generate LLVM IR for simple assignment (=).
 *
 * Steps:
 *   1. Evaluate left and right operands.
 *   2. For operator= overload: delegate to generate_binary_operator_overload.
 *   3. For owner assignment: emit owner_move + null the source.
 *   4. For struct copy: emit memcpy or copy constructor call.
 *   5. For primitives/pointers: emit store instruction.
 */
void implementation_generator::visit_simple_assignation_expression(simple_assignation_expression& expr) {
    // ── Check if LHS is a union alternative assignment ───────────────────────
    std::shared_ptr<member_of_object_expression> union_moe;
    std::shared_ptr<union_type_def> union_def_for_assign;
    const union_alternative* union_alt_for_assign = nullptr;
    {
        auto lhs_e = std::shared_ptr<expression>(expr.left());
        while (auto lve = std::dynamic_pointer_cast<load_value_expression>(lhs_e)) {
            lhs_e = lve->sub_expr();
        }
        if (auto moe = std::dynamic_pointer_cast<member_of_object_expression>(lhs_e)) {
            auto sub_type = moe->sub_expr()->get_type();
            if (type::is_reference(sub_type))
                sub_type = std::dynamic_pointer_cast<reference_type>(sub_type)->get_subtype();
            if (auto st = std::dynamic_pointer_cast<struct_type>(sub_type)) {
                if (!st->get_struct()) {
                    _skip_union_disc_check = true;
                    auto root_ns = _unit.get_root_namespace();
                    if (root_ns) {
                        union_def_for_assign = find_union_by_struct_type(root_ns, st);
                    }
                    if (union_def_for_assign) {
                        union_moe = moe;
                        const k::name& sym_name = moe->symbol().get_name();
                        std::string alt_name = sym_name.size() > 1 ? sym_name.back() : sym_name.to_string();
                        union_alt_for_assign = union_def_for_assign->get_alternative_by_name(alt_name);
                    }
                }
            }
        }
    }

    // A union alternative is an object only while its discriminant is active.
    // Inactive storage must be copy-constructed, never passed to operator=.
    if (union_moe && union_def_for_assign && union_alt_for_assign &&
        type::is_struct(type::remove_const(union_alt_for_assign->resolved_type))) {
        _skip_union_disc_check = false;
        union_moe->sub_expr()->accept(*this);
        auto* union_base = _value;
        auto* union_llvm_type = union_def_for_assign->get_struct_type()->get_llvm_type();
        auto* disc_ptr = _builder->CreateStructGEP(union_llvm_type, union_base, 0, "union_assign_disc");
        place_result destination{
            _builder->CreateStructGEP(union_llvm_type, union_base, 1, "union_assign_storage")};
        auto alt_type = type::remove_const(union_alt_for_assign->resolved_type);
        auto* new_disc = _builder->getInt32(union_alt_for_assign->index);
        call_argument_cleanup_scope incoming(*this, true);

        auto* saved_sret_destination = _sret_destination;
        _sret_destination = nullptr;
        expr.right()->accept(*this);
        _sret_destination = saved_sret_destination;
        auto* source = _value;
        auto* dest = checked_legacy_result(destination);
        auto source_type = expr.right()->get_type();
        const bool source_const = type::is_reference(source_type)
            && type::is_const(source_type->get_subtype());
        if (!source) {
            throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F035),
                expr.first_lexeme(), "Internal error: union assignment produced no source value");
        }
        if (is_expression_temporary(source)) {
            incoming.track_storage(alt_type, source);
        } else if (!source->getType()->isPointerTy()) {
            incoming.track(alt_type, source);
        }
        auto alt_struct = std::dynamic_pointer_cast<struct_type>(type::canonical(alt_type));
        std::shared_ptr<union_type_def> temporary_union;
        if (alt_struct && !alt_struct->get_struct() && !source_const
            && !expr.has_operator_overload() && is_expression_temporary(source)) {
            temporary_union = find_union_by_struct_type(_unit.get_root_namespace(), alt_struct);
            if (!temporary_union) {
                throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F024), {},
                    "Cannot find union metadata while moving '{}'", {alt_struct->to_string()});
            }
        }
        auto move_temporary_union = [&](bool destroy_dest) {
            if (destroy_dest) emit_union_cleanup(dest, *temporary_union);
            auto* llvm_type = temporary_union->get_struct_type()->get_llvm_type();
            uint64_t size = _context->module().getDataLayout().getTypeAllocSize(llvm_type);
            _builder->CreateMemCpy(dest, llvm::MaybeAlign(1), source, llvm::MaybeAlign(1),
                                   _builder->getInt64(size));
            auto* source_disc = _builder->CreateStructGEP(llvm_type, source, 0, "union_move_source_disc");
            _builder->CreateStore(_builder->getInt32(-1), source_disc);
            set_union_live_state(source, *temporary_union, false);
        };

        auto* func = _builder->GetInsertBlock()->getParent();
        auto* active_bb = llvm::BasicBlock::Create(**_context, "union_assign_active", func);
        auto* construct_bb = llvm::BasicBlock::Create(**_context, "union_assign_construct", func);
        auto* done_bb = llvm::BasicBlock::Create(**_context, "union_assign_done", func);
        auto* old_disc = _builder->CreateLoad(_builder->getInt32Ty(), disc_ptr, "union_assign_old_disc");
        llvm::Value* is_active = _builder->CreateICmpEQ(old_disc, new_disc);
        auto* live = _builder->CreateLoad(_builder->getInt32Ty(),
            union_live_state_ptr(union_base, *union_def_for_assign));
        llvm::Value* has_payload = _builder->CreateOr(
            _builder->CreateICmpNE(old_disc, _builder->getInt32(0)),
            _builder->CreateICmpNE(live, _builder->getInt32(0)));
        if (has_default_nested_union(*union_def_for_assign)) {
            has_payload = _builder->getTrue();
        }
        is_active = _builder->CreateAnd(is_active, has_payload);
        if (union_alt_for_assign->index == 0
            && union_first_alternative_tracks_default(*union_def_for_assign, _unit.get_root_namespace())) {
            // An escaped default alternative is not constructed: it is released field by field
            // and then copy-constructed, never assigned in place.
            auto* escaped = _builder->CreateICmpEQ(live, _builder->getInt32(2));
            is_active = _builder->CreateAnd(is_active, _builder->CreateNot(escaped));
            if (source->getType()->isPointerTy()) {
                auto* dispatch_bb = llvm::BasicBlock::Create(**_context, "union_assign_dispatch", func);
                auto* alias = _builder->CreateAnd(escaped, _builder->CreateICmpEQ(dest, source));
                _builder->CreateCondBr(alias, done_bb, dispatch_bb);
                _builder->SetInsertPoint(dispatch_bb);
            }
        }
        _builder->CreateCondBr(is_active, active_bb, construct_bb);

        auto construct_value = [&]() {
            if (temporary_union) {
                move_temporary_union(false);
            } else if (source->getType()->isPointerTy()) {
                // Both runtime branches must preserve a reference-bound RHS
                // temporary's cleanup; only one branch activates new storage.
                emit_value_copy_or_move(dest, source, alt_type, false, expr.first_lexeme(),
                    "union activation", false, false, expr.get_copy_constructor(), source_const);
            } else {
                _builder->CreateStore(source, dest);
            }
            // A throwing constructor must leave no alternative eligible for cleanup.
            _builder->CreateStore(new_disc, disc_ptr);
            set_union_live_state(union_base, *union_def_for_assign, true);
        };

        _builder->SetInsertPoint(construct_bb);
        emit_union_cleanup_on_reassign(union_base, *union_def_for_assign, union_alt_for_assign->index);
        _builder->CreateStore(_builder->getInt32(-1), disc_ptr);
        set_union_live_state(union_base, *union_def_for_assign, false);
        construct_value();
        _builder->CreateBr(done_bb);

        _builder->SetInsertPoint(active_bb);
        if (!generate_binary_operator_overload(expr, dest, source)) {
            if (source->getType()->isPointerTy()) {
                auto* copy_bb = llvm::BasicBlock::Create(**_context, "union_assign_replace", func);
                // Default self-assignment must not destroy the object being copied.
                _builder->CreateCondBr(_builder->CreateICmpEQ(dest, source), done_bb, copy_bb);
                _builder->SetInsertPoint(copy_bb);
                _builder->CreateStore(_builder->getInt32(-1), disc_ptr);
                set_union_live_state(union_base, *union_def_for_assign, false);
                if (temporary_union) {
                    move_temporary_union(true);
                } else {
                    emit_value_copy_or_move(dest, source, alt_type, true, expr.first_lexeme(),
                        "union assignment", false, false, expr.get_copy_constructor(), source_const);
                }
                _builder->CreateStore(new_disc, disc_ptr);
                set_union_live_state(union_base, *union_def_for_assign, true);
            } else {
                // A loaded aggregate already owns its by-value copy.
                _builder->CreateStore(_builder->getInt32(-1), disc_ptr);
                set_union_live_state(union_base, *union_def_for_assign, false);
                auto agg = std::dynamic_pointer_cast<struct_type>(alt_type)->get_struct();
                if (agg && agg->get_destructor()) {
                    auto dtor = _context->_functions.find(agg->get_destructor()->shared_as<function>());
                    if (dtor != _context->_functions.end()) {
                        create_call_or_invoke(dtor->second->getFunctionType(), dtor->second, {dest});
                    }
                } else if (!agg) {
                    auto st = std::dynamic_pointer_cast<struct_type>(alt_type);
                    auto nested = find_union_by_struct_type(_unit.get_root_namespace(), st);
                    if (!nested) {
                        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F024), {},
                            "Cannot find union metadata while replacing '{}'", {st->to_string()});
                    }
                    emit_union_cleanup(dest, *nested);
                }
                _builder->CreateStore(source, dest);
                _builder->CreateStore(new_disc, disc_ptr);
                set_union_live_state(union_base, *union_def_for_assign, true);
            }
        }
        _builder->CreateBr(done_bb);
        _builder->SetInsertPoint(done_bb);
        if (temporary_union) cancel_temporary_cleanup(source);
        incoming.transfer();
        if (type::is_reference(expr.get_type()) || type::is_drain(expr.get_type())) {
            publish_expression_result(expr, destination);
        } else {
            _value = checked_legacy_result(destination);
        }
        return;
    }

    if (generate_binary_operator_overload(expr)) {
        _skip_union_disc_check = false;
        if (union_moe && union_def_for_assign && union_alt_for_assign) {
            _value = nullptr;
            union_moe->sub_expr()->accept(*this);
            llvm::Value* union_base = _value;
            if (union_base) {
                auto* union_llvm_type = union_def_for_assign->get_struct_type()->get_llvm_type();
                auto* disc_ptr = _builder->CreateStructGEP(union_llvm_type, union_base, 0, "union_disc_upd");
                _builder->CreateStore(
                    llvm::ConstantInt::get(llvm::Type::getInt32Ty(_builder->getContext()), union_alt_for_assign->index),
                    disc_ptr);
                set_union_live_state(union_base, *union_def_for_assign, true);
            }
        }
        return;
    }

    // Step 1: Evaluate left and right operands
    llvm::Value* union_base_once = nullptr;
    place_result destination{nullptr};
    llvm::Value* right = nullptr;
    if (union_moe && union_def_for_assign && union_alt_for_assign) {
        _skip_union_disc_check = false;
        _value = nullptr;
        union_moe->sub_expr()->accept(*this);
        union_base_once = _value;
        if (union_base_once) {
            destination = place_result{_builder->CreateStructGEP(
                union_def_for_assign->get_struct_type()->get_llvm_type(), union_base_once, 1, "union_assign_storage")};
        }
        _value = nullptr;
        expr.right()->accept(*this);
        right = _value;
        _value = nullptr;
    } else {
        std::tie(destination, right) = process_assignment_operands(expr);
    }
    _skip_union_disc_check = false;
    auto* left = checked_legacy_result(destination);
    if(!left || !right) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F035), expr.first_lexeme(),
            "Internal error: assignment expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    // Step 2: For operator= overload: delegate to generate_binary_operator_overload
    // left is a pointer to the storage.
    // Determine what the target type really is after one level of ref-unwrap.
    auto expr_left_type = expr.left()->get_type();
    auto left_ref_type  = std::dynamic_pointer_cast<reference_type>(expr_left_type);
    auto target_type    = left_ref_type ? left_ref_type->get_subtype() : nullptr;

    // If target is ref-to-ref, unwrap one more level (variable access pattern).
    if (target_type && type::is_reference(target_type)) {
        target_type = std::dynamic_pointer_cast<reference_type>(target_type)->get_subtype();
    }

    // ── Union member assignment ─────────────────────────────────────────────
    // If the LHS is a member access on a union, destroy the previously-active
    // alternative, store the new value, and update the discriminant. This must
    // run *before* the owner/array special cases below, otherwise an owner or
    // array alternative would short-circuit (return) without updating the
    // discriminant — leaving the union mistagged and crashing on the next read.
    {
        auto lhs_expr = expr.left();
        while (auto lve = std::dynamic_pointer_cast<load_value_expression>(lhs_expr)) {
            lhs_expr = lve->sub_expr();
        }
        if (auto moe = std::dynamic_pointer_cast<member_of_object_expression>(lhs_expr)) {
            auto sub_type = moe->sub_expr()->get_type();
            if (type::is_reference(sub_type)) {
                sub_type = std::dynamic_pointer_cast<reference_type>(sub_type)->get_subtype();
            }
            if (auto st_type = std::dynamic_pointer_cast<struct_type>(sub_type)) {
                if (!st_type->get_struct()) {
                    // This is a union type — find the union_type_def
                    std::shared_ptr<union_type_def> union_def;
                    auto root_ns = _unit.get_root_namespace();
                    if (root_ns) {
                        union_def = find_union_by_struct_type(root_ns, st_type);
                    }
                    if (union_def) {
                        const k::name& sym_name = moe->symbol().get_name();
                        std::string alt_name = sym_name.size() > 1 ? sym_name.back() : sym_name.to_string();
                        auto* alt = union_def->get_alternative_by_name(alt_name);
                        if (alt) {
                            llvm::Value* union_base = union_base_once;
                            if (union_base) {
                                auto alt_type = alt->resolved_type;
                                auto bare_alt = alt_type ? type::remove_const(alt_type) : nullptr;
                                auto callable = std::dynamic_pointer_cast<callable_type>(bare_alt);
                                const bool owning = type::is_owner(bare_alt)
                                    || (callable && callable->is_owner());
                                const bool array_temporary = type::is_sized_array(bare_alt)
                                    && is_expression_temporary(right);
                                call_argument_cleanup_scope incoming(*this, owning || array_temporary);
                                if (owning) right = incoming.track(bare_alt, right);
                                if (array_temporary) incoming.track_storage(bare_alt, right);
                                if (type::is_sized_array(bare_alt) && right->getType()->isPointerTy()) {
                                    auto* fn = _builder->GetInsertBlock()->getParent();
                                    auto* copy = llvm::BasicBlock::Create(**_context, "union_array_assign", fn);
                                    auto* done = llvm::BasicBlock::Create(**_context, "union_array_assigned", fn);
                                    _builder->CreateCondBr(_builder->CreateICmpEQ(left, right), done, copy);
                                    _builder->SetInsertPoint(copy);
                                    emit_union_cleanup(union_base, *union_def);
                                    auto* union_llvm_type = st_type->get_llvm_type();
                                    auto* disc_ptr = _builder->CreateStructGEP(
                                        union_llvm_type, union_base, 0, "union_array_disc");
                                    _builder->CreateStore(_builder->getInt32(-1), disc_ptr);
                                    set_union_live_state(union_base, *union_def, false);
                                    auto source_type = expr.right()->get_type();
                                    const bool source_const = type::is_reference(source_type)
                                        && type::is_const(source_type->get_subtype());
                                    if (emit_union_payload_copy(left, right, bare_alt, source_const)) {
                                        _builder->CreateStore(_builder->getInt32(alt->index), disc_ptr);
                                        set_union_live_state(union_base, *union_def, true);
                                        _builder->CreateBr(done);
                                    }
                                    _builder->SetInsertPoint(done);
                                    incoming.transfer();
                                    publish_expression_result(expr, destination);
                                    return;
                                }
                                // Destroy the previously-active alternative (handles
                                // owner alternatives — frees their buffer).
                                emit_union_cleanup_on_reassign(union_base, *union_def, alt->index);
                                // Store the new value.
                                auto alt_st = std::dynamic_pointer_cast<struct_type>(bare_alt);
                                if (alt_st && right->getType()->isPointerTy()) {
                                    emit_value_copy_or_move(left, right, alt_st, /*destroy_dest_first=*/false, expr.first_lexeme(), "=");
                                } else {
                                    llvm::Value* store_val = right;
                                    if (!store_val) {
                                        store_val = llvm::ConstantPointerNull::get(
                                            llvm::PointerType::get(_builder->getContext(), 0));
                                    }
                                    _builder->CreateStore(store_val, left);
                                }
                                // Update the discriminant.
                                auto* union_llvm_type = st_type->get_llvm_type();
                                auto* disc_ptr = _builder->CreateStructGEP(union_llvm_type, union_base, 0, "union_disc_upd");
                                _builder->CreateStore(
                                    llvm::ConstantInt::get(llvm::Type::getInt32Ty(_builder->getContext()), alt->index),
                                    disc_ptr);
                                set_union_live_state(union_base, *union_def, true);
                                incoming.transfer();
                            }
                            publish_expression_result(expr, destination);
                            return;
                        }
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------------
    // Array assignment: element-wise copy (spec: partial copy, no resize)
    // ------------------------------------------------------------------
    if (target_type && type::is_sized_array(target_type)) {
        auto dest_arr  = std::dynamic_pointer_cast<sized_array_type>(target_type);
        auto* struct_llvm    = dest_arr->get_llvm_struct_type();
        auto* data_arr_llvm  = dest_arr->get_llvm_data_array_type();
        auto* elem_llvm      = _context->get_llvm_type(dest_arr->get_subtype());
        auto  dest_n         = static_cast<uint64_t>(dest_arr->get_size());

        // right is the pointer to the source struct { i32, [N x T] }
        auto* i32_t = llvm::Type::getInt32Ty(_builder->getContext());

        // Source capacity (runtime value from field 0)
        llvm::Value* src_size_ptr = _builder->CreateStructGEP(struct_llvm, right,
            sized_array_type::FIELD_SIZE, "src_sz_ptr");
        llvm::Value* src_n = _builder->CreateLoad(i32_t, src_size_ptr, "src_n");

        // Data pointers
        llvm::Value* src_data  = _builder->CreateStructGEP(struct_llvm, right,
            sized_array_type::FIELD_DATA, "src_data");
        llvm::Value* dest_data = _builder->CreateStructGEP(struct_llvm, left,
            sized_array_type::FIELD_DATA, "dst_data");

        // copy_n = min(dest_n, src_n)
        auto* dest_n_val = llvm::ConstantInt::get(i32_t, dest_n, false);
        llvm::Value* copy_n = _builder->CreateSelect(
            _builder->CreateICmpULT(src_n, dest_n_val), src_n, dest_n_val, "copy_n");

        // Emit copy loop
        auto* fn = _builder->GetInsertBlock()->getParent();
        auto* pre_bb   = _builder->GetInsertBlock();
        auto* loop_bb  = llvm::BasicBlock::Create(_builder->getContext(), "arr_asgn_loop", fn);
        auto* done_bb  = llvm::BasicBlock::Create(_builder->getContext(), "arr_asgn_done", fn);

        _builder->CreateCondBr(
            _builder->CreateICmpUGT(copy_n, llvm::ConstantInt::get(i32_t, 0, false)),
            loop_bb, done_bb);

        _builder->SetInsertPoint(loop_bb);
        auto* idx = _builder->CreatePHI(i32_t, 2, "asgn_idx");
        idx->addIncoming(llvm::ConstantInt::get(i32_t, 0, false), pre_bb);

        llvm::Value* s = _builder->CreateGEP(data_arr_llvm, src_data,
            {_builder->getInt32(0), idx}, "s_elem");
        llvm::Value* d = _builder->CreateGEP(data_arr_llvm, dest_data,
            {_builder->getInt32(0), idx}, "d_elem");
        _builder->CreateStore(_builder->CreateLoad(elem_llvm, s, "ev"), d);

        auto* nxt = _builder->CreateAdd(idx, llvm::ConstantInt::get(i32_t, 1), "nxt");
        idx->addIncoming(nxt, loop_bb);
        _builder->CreateCondBr(_builder->CreateICmpULT(nxt, copy_n), loop_bb, done_bb);

        _builder->SetInsertPoint(done_bb);
        publish_expression_result(expr, destination);
        return;
    }

    // ------------------------------------------------------------------
    // Owner assignment: delete old object (if any), store new pointer
    // ------------------------------------------------------------------
    if (target_type && type::is_owner(target_type)) {
        auto& llvm_ctx = _builder->getContext();
        auto* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);

        call_argument_cleanup_scope incoming(*this, true);
        right = incoming.track(target_type, right);

        // Step 3: For owner assignment: emit owner_move + null the source
        // Disarm the old owner before destruction, which can throw.
        auto own_type = std::dynamic_pointer_cast<owner_type>(target_type);
        emit_owner_cleanup_if_nonnull(_builder.get(), get_module(), _context->_functions,
            left, own_type->get_owned_type(), "owner_asgn", make_virtual_call_emitter());

        // Determine the new pointer value to store:
        // - right may be null (from null literal → visit_value_expression returns nullptr LLVM val)
        // - right may be an owner ptr (from owner_move_expression)
        llvm::Value* new_ptr = right;
        if (!new_ptr) {
            // null literal case
            new_ptr = llvm::ConstantPointerNull::get(ptr_ty);
        }
        _builder->CreateStore(new_ptr, left);
        incoming.transfer();
        publish_expression_result(expr, destination);
        return;
    }

    // ------------------------------------------------------------------
    // Callable assignment: cleanup old closure if owner, store fat callable
    // ------------------------------------------------------------------
    if (auto ct = std::dynamic_pointer_cast<callable_type>(target_type)) {
        if (!ct->is_unbound_member()) {
            auto* callable_llvm_type = _context->get_or_create_callable_llvm_type();
            if (ct->is_owner()) {
                emit_owned_callable_cleanup_if_nonnull(_builder.get(), get_module(),
                    left, callable_llvm_type, "owned_callable_asgn", make_virtual_call_emitter());
            }
            llvm::Value* new_callable = right;
            if (!new_callable) {
                new_callable = llvm::ConstantAggregateZero::get(callable_llvm_type);
            } else if (new_callable->getType()->isPointerTy()) {
                new_callable = _builder->CreateLoad(callable_llvm_type, new_callable, "callable_asgn_val");
            }
            // Rebinding a non-null ('+') callable from a nullable one: trap before the store.
            // The adaptation of the right-hand side to the target callable type wraps the
            // original expression in a cast whose type is the (non-null) target type, so the
            // nullability of the *source* has to be read through those wrappers.
            if (new_callable && !ct->is_nullable()) {
                auto rhs_model = expr.right();
                auto source_type = rhs_model ? rhs_model->get_type() : nullptr;
                for (auto inner = rhs_model; inner; ) {
                    auto unary = std::dynamic_pointer_cast<unary_expression>(inner);
                    if (!unary || !(std::dynamic_pointer_cast<cast_expression>(inner) ||
                                    std::dynamic_pointer_cast<load_value_expression>(inner))) break;
                    inner = unary->sub_expr();
                    if (inner && inner->get_type()) source_type = inner->get_type();
                }
                emit_callable_nonnull_check(target_type, source_type, new_callable, expr.first_lexeme());
            }
            _builder->CreateStore(new_callable, left);
            publish_expression_result(expr, destination);
            return;
        }
    }

    // ------------------------------------------------------------------
    // Scalar / pointer assignment (existing behaviour)
    // ------------------------------------------------------------------

    // Step 4: For struct copy: emit memcpy or copy constructor call
    // Link rebind from nullable source: emit null-check before store.
    // (The resolver emits warning 0x0072 at compile-time; we add the runtime guard here.)
    if (target_type && type::is_link(target_type)) {
        // Pierce cast_expression to find the real source nullability
        auto rhs_model = expr.right();
        auto rhs_type = rhs_model ? rhs_model->get_type() : nullptr;
        // Also check original type through a cast (upcast Derived→Base wraps nullable ptr)
        if (auto cast_e = std::dynamic_pointer_cast<cast_expression>(rhs_model)) {
            auto inner_type = cast_e->sub_expr()->get_type();
            if (inner_type && type::is_nullable_indirection(inner_type)) {
                rhs_type = inner_type;
            }
        }
        if (rhs_type && type::is_nullable_indirection(rhs_type)) {
            set_debug_location(expr.first_lexeme());
            auto* fatal = get_or_declare_fatal_null_function("__k_fatal_null_assignation");
            // In an if-condition, soft-fail to _null_failure_bb instead of fatal trap.
            emit_null_check(right, fatal, "link_rebind", _null_failure_bb);
        }
    }

    // ── Union inherited assignment (upcast: derived→base, downcast: base→derived) ──
    // Detected when both LHS and RHS are union struct types with different struct types.
    // In this case `right` is still a pointer (the ref wrapper was NOT stripped by the type
    // resolver), so we can use it directly for field-by-field copy.
    {
        auto lhs_type = target_type;
        auto rhs_model_type = expr.right()->get_type();
        // source is still ref<union_T> (the type resolver kept it as a reference)
        auto rhs_inner = rhs_model_type;
        if (type::is_reference(rhs_inner)) {
            rhs_inner = std::dynamic_pointer_cast<reference_type>(rhs_inner)->get_subtype();
        }
        const bool source_const = type::is_const(rhs_inner);
        rhs_inner = type::canonical(type::remove_const(rhs_inner));
        if (auto lhs_st = std::dynamic_pointer_cast<struct_type>(lhs_type)) {
            if (auto rhs_st = std::dynamic_pointer_cast<struct_type>(rhs_inner)) {
                if (!lhs_st->get_struct() && !rhs_st->get_struct() && lhs_st != rhs_st) {
                    // Both are union struct types and they differ — do inherited copy
                    auto root_ns = _unit.get_root_namespace();
                    std::shared_ptr<union_type_def> lhs_udef, rhs_udef;
                    if (root_ns) {
                        lhs_udef = find_union_by_struct_type(root_ns, lhs_st);
                        rhs_udef = find_union_by_struct_type(root_ns, rhs_st);
                    }
                    if (lhs_udef && rhs_udef) {
                        emit_union_copy(left, right, *lhs_udef, *rhs_udef, true, source_const);
                        publish_expression_result(expr, destination);
                        return;
                    }
                }
            }
        }
    }
    // ───────────────────────────────────────────────────────────────────────

     // ── Struct value copy/move (no operator= overload) ───────────────────────
    // K does not provide user-defined copy-assignment operators, so a plain
    // struct assignment (e.g. `a = b;` or `current = _in->read();`) falls through
    // to here.  When the right-hand side is a *pointer* to the source struct
    // (e.g. an sret alloca returned by a value-returning call, or the address of
    // a struct variable), delegate to the value-semantics routine, which:
    //   - memcpy's trivially-copyable structs (fast path);
    //   - MOVES a prvalue temporary (memcpy + cancel its destruction) so owning
    //     aggregates such as Vector<T> are not double-freed;
    //   - COPIES an lvalue via the copy constructor when available.
    // The old destination contents are destroyed first for non-trivial types.
    auto ret_stmt = std::dynamic_pointer_cast<return_statement>(expr.find_statement());
    const bool named_result_assignment = ret_stmt && ret_stmt->is_named_result_assignment()
        && ret_stmt->get_expression().get() == &expr;
    if (target_type && type::is_struct(target_type) && (right->getType()->isPointerTy() || named_result_assignment)) {
        auto* struct_llvm = _context->get_llvm_type(type::remove_const(target_type));
        if (struct_llvm && !struct_llvm->isPointerTy()) {
            auto source_type = expr.right() ? expr.right()->get_type() : nullptr;
            const bool source_const = source_type && type::is_reference(source_type)
                && type::is_const(source_type->get_subtype());
            emit_value_copy_or_move(left, right, target_type, /*destroy_dest_first=*/true,
                                    expr.first_lexeme(), "assignment",
                                    named_result_assignment, true, expr.get_copy_constructor(), source_const);
            publish_expression_result(expr, destination);
            return;
        }
    }

    // Step 5: For primitives/pointers: emit store instruction
    _value = right;
    _value = _builder->CreateStore(_value, left);
    publish_expression_result(expr, destination);

}

//
// Arithmetic assignation expression
//

void symbol_resolver::visit_arithmetic_assignation_expression(arithmetic_assignation_expression &expr) {
    visit_assignation_expression(expr);
}

void type_reference_resolver::visit_arithmetic_assignation_expression(arithmetic_assignation_expression &expr) {
    visit_assignation_expression(expr);

    // If an operator overload was resolved, no further checks needed.
    if (expr.has_operator_overload()) return;

    auto left = expr.left();
    auto right = expr.right();

    auto left_type = left->get_type();
    auto ref_target_type = std::dynamic_pointer_cast<reference_type>(left_type);
    auto target_type = ref_target_type->get_subtype();
    if(type::is_pointer(target_type)) {
        throw_error(static_cast<unsigned int>(k::diag::statement_diag::ERR_FOR_COND_NOT_BOOL), expr.first_lexeme(),
            "Arithmetic-assignment operators (e.g. '+=', '-=') cannot be applied to pointer types: "
            "the target has type '{}'; pointer arithmetic is not supported",
            {target_type ? target_type->to_string() : "?"});
    }
}

//
// Addition assignment expression (+=)
//

void implementation_generator::visit_addition_assignation_expression(additition_assignation_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [destination, right] = process_assignment_operands(expr);
    auto* left = checked_legacy_result(destination);
    if(!left || !right) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F036), expr.first_lexeme(),
            "Internal error: '+=' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    auto left_ref_type = std::dynamic_pointer_cast<reference_type>(expr.left()->get_type());
    auto left_type = left_ref_type->get_subtype();
    auto llvm_type = _context->get_llvm_type(left_type);

    auto left_val = _builder->CreateLoad(llvm_type, left);
    if(type::is_prim_integer(left_type)) {
        _value = _builder->CreateAdd(left_val, right);
    } else if(type::is_prim_float(left_type)) {
        _value = _builder->CreateFAdd(left_val, right);
    } else {
        // TODO: Support other types
    }

    // Store the value, return the left ref
    _value = _builder->CreateStore(_value, left);
    publish_expression_result(expr, destination);
}

//
// Substraction assignment expression (-=)
//

void implementation_generator::visit_substraction_assignation_expression(substraction_assignation_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [destination, right] = process_assignment_operands(expr);
    auto* left = checked_legacy_result(destination);
    if(!left || !right) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F037), expr.first_lexeme(),
            "Internal error: '-=' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    auto left_ref_type = std::dynamic_pointer_cast<reference_type>(expr.left()->get_type());
    auto left_type = left_ref_type->get_subtype();
    auto llvm_type = _context->get_llvm_type(left_type);

    auto left_val = _builder->CreateLoad(llvm_type, left);
    if(type::is_prim_integer(left_type)) {
        _value = _builder->CreateSub(left_val, right);
    } else if(type::is_prim_float(left_type)) {
        _value = _builder->CreateFSub(left_val, right);
    } else {
        // TODO: Support other types
    }

    // Store the value, return the left ref
    _value = _builder->CreateStore(_value, left);
    publish_expression_result(expr, destination);
}

//
// Multiplication assignment expression (*=)
//

void implementation_generator::visit_multiplication_assignation_expression(multiplication_assignation_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [destination, right] = process_assignment_operands(expr);
    auto* left = checked_legacy_result(destination);
    if(!left || !right) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F038), expr.first_lexeme(),
            "Internal error: '*=' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    auto left_ref_type = std::dynamic_pointer_cast<reference_type>(expr.left()->get_type());
    auto left_type = left_ref_type->get_subtype();
    auto llvm_type = _context->get_llvm_type(left_type);

    auto left_val = _builder->CreateLoad(llvm_type, left);
    if(type::is_prim_integer(left_type)) {
        _value = _builder->CreateMul(left_val, right);
    } else if(type::is_prim_float(left_type)) {
        _value = _builder->CreateFMul(left_val, right);
    } else {
        // TODO: Support other types
    }

    // Store the value, return the left ref
    _value = _builder->CreateStore(_value, left);
    publish_expression_result(expr, destination);
}

//
// Division assignment expression (/=)
//

void implementation_generator::visit_division_assignation_expression(division_assignation_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [destination, right] = process_assignment_operands(expr);
    auto* left = checked_legacy_result(destination);
    if(!left || !right) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F039), expr.first_lexeme(),
            "Internal error: '/=' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    auto left_ref_type = std::dynamic_pointer_cast<reference_type>(expr.left()->get_type());
    auto left_type = type::remove_const(left_ref_type->get_subtype());
    auto llvm_type = _context->get_llvm_type(left_type);

    auto left_val = _builder->CreateLoad(llvm_type, left);
    if(auto prim = std::dynamic_pointer_cast<primitive_type>(left_type)) {
        if(prim->is_integer()) {
            if(prim->is_unsigned()) {
                _value = _builder->CreateUDiv(left_val, right);
            } else {
                _value = _builder->CreateSDiv(left_val, right);
            }
        } else if(prim->is_float()) {
            _value = _builder->CreateFDiv(left_val, right);
        }
    } else {
        // TODO: Support other types
    }

    // Store the value, return the left ref
    _value = _builder->CreateStore(_value, left);
    publish_expression_result(expr, destination);
}

//
// Modulo assignment expression (%=)
//

void implementation_generator::visit_modulo_assignation_expression(modulo_assignation_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [destination, right] = process_assignment_operands(expr);
    auto* left = checked_legacy_result(destination);
    if(!left || !right) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F03A), expr.first_lexeme(),
            "Internal error: '%=' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    auto left_ref_type = std::dynamic_pointer_cast<reference_type>(expr.left()->get_type());
    auto left_type = type::remove_const(left_ref_type->get_subtype());
    auto llvm_type = _context->get_llvm_type(left_type);

    auto left_val = _builder->CreateLoad(llvm_type, left);
    if(auto prim = std::dynamic_pointer_cast<primitive_type>(left_type)) {
        if(prim->is_integer()) {
            if(prim->is_unsigned()) {
                _value = _builder->CreateURem(left_val, right);
            } else {
                _value = _builder->CreateSRem(left_val, right);
            }
        } else if(prim->is_float()) {
            _value = _builder->CreateFRem(left_val, right);
        }
    } else {
        // TODO: Support other types
    }

    // Store the value, return the left ref
    _value = _builder->CreateStore(_value, left);
    publish_expression_result(expr, destination);
}

//
// Bitwise and assignment expression
//

void implementation_generator::visit_bitwise_and_assignation_expression(bitwise_and_assignation_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [destination, right] = process_assignment_operands(expr);
    auto* left = checked_legacy_result(destination);
    if(!left || !right) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F03B), expr.first_lexeme(),
            "Internal error: '&=' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    auto left_ref_type = std::dynamic_pointer_cast<reference_type>(expr.left()->get_type());
    auto left_type = type::remove_const(left_ref_type->get_subtype());
    auto llvm_type = _context->get_llvm_type(left_type);

    auto left_val = _builder->CreateLoad(llvm_type, left);
    if(auto prim = std::dynamic_pointer_cast<primitive_type>(left_type)) {
        if(prim->is_integer()) {
            _value = _builder->CreateAnd(left_val, right);
        } else if(prim->is_float()) {
            throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_ADD_ASSIGN_INCOMPATIBLE), expr.first_lexeme(),
                "Bitwise AND-assignment ('&=') cannot be applied to floating-point values: "
                "bitwise operations are only defined for integer types; "
                "the operand has type '{}'",
                {prim->to_string()});
        }
    } else {
        // TODO: Support other types
    }

    // Store the value, return the left ref
    _value = _builder->CreateStore(_value, left);
    publish_expression_result(expr, destination);
}

//
// Bitwise or assignment expression
//

void implementation_generator::visit_bitwise_or_assignation_expression(bitwise_or_assignation_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [destination, right] = process_assignment_operands(expr);
    auto* left = checked_legacy_result(destination);
    if(!left || !right) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F03C), expr.first_lexeme(),
            "Internal error: '|=' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    auto left_ref_type = std::dynamic_pointer_cast<reference_type>(expr.left()->get_type());
    auto left_type = type::remove_const(left_ref_type->get_subtype());
    auto llvm_type = _context->get_llvm_type(left_type);

    auto left_val = _builder->CreateLoad(llvm_type, left);
    if(auto prim = std::dynamic_pointer_cast<primitive_type>(left_type)) {
        if(prim->is_integer()) {
            _value = _builder->CreateOr(left_val, right);
        } else if(prim->is_float()) {
            throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_CAST_NOT_SUPPORTED), expr.first_lexeme(),
                "Bitwise OR-assignment ('|=') cannot be applied to floating-point values: "
                "bitwise operations are only defined for integer types; "
                "the operand has type '{}'",
                {prim->to_string()});
        }
    } else {
        // TODO: Support other types
    }

    // Store the value, return the left ref
    _value = _builder->CreateStore(_value, left);
    publish_expression_result(expr, destination);
}

//
// Bitwise xor assignment expression
//

void implementation_generator::visit_bitwise_xor_assignation_expression(bitwise_xor_assignation_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [destination, right] = process_assignment_operands(expr);
    auto* left = checked_legacy_result(destination);
    if(!left || !right) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F03D), expr.first_lexeme(),
            "Internal error: '^=' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    auto left_ref_type = std::dynamic_pointer_cast<reference_type>(expr.left()->get_type());
    auto left_type = type::remove_const(left_ref_type->get_subtype());
    auto llvm_type = _context->get_llvm_type(left_type);

    auto left_val = _builder->CreateLoad(llvm_type, left);
    if(auto prim = std::dynamic_pointer_cast<primitive_type>(left_type)) {
        if(prim->is_integer()) {
            _value = _builder->CreateXor(left_val, right);
        } else if(prim->is_float()) {
            throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_MUL_ASSIGN_INCOMPATIBLE), expr.first_lexeme(),
                "Bitwise XOR-assignment ('^=') cannot be applied to floating-point values: "
                "bitwise operations are only defined for integer types; "
                "the operand has type '{}'",
                {prim->to_string()});
        }
    } else {
        // TODO: Support other types
    }

    // Store the value, return the left ref
    _value = _builder->CreateStore(_value, left);
    publish_expression_result(expr, destination);
}

//
// Left shift assignment expression
//

void implementation_generator::visit_left_shift_assignation_expression(left_shift_assignation_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [destination, right] = process_assignment_operands(expr);
    auto* left = checked_legacy_result(destination);
    if(!left || !right) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F03E), expr.first_lexeme(),
            "Internal error: '<<=' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    auto left_ref_type = std::dynamic_pointer_cast<reference_type>(expr.left()->get_type());
    auto left_type = type::remove_const(left_ref_type->get_subtype());
    auto llvm_type = _context->get_llvm_type(left_type);

    auto left_val = _builder->CreateLoad(llvm_type, left);
    if(auto prim = std::dynamic_pointer_cast<primitive_type>(left_type)) {
        if(prim->is_integer()) {
            // TODO may it poison when overflow ?
            _value = _builder->CreateShl(left_val, right);
        } else if(prim->is_float()) {
            throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_SUB_ASSIGN_INCOMPATIBLE), expr.first_lexeme(),
                "Left shift-assignment ('<<=') cannot be applied to floating-point values: "
                "shift operations are only defined for integer types; "
                "the operand has type '{}'",
                {prim->to_string()});
        }
    } else {
        // TODO: Support other types
    }

    // Store the value, return the left ref
    _value = _builder->CreateStore(_value, left);
    publish_expression_result(expr, destination);
}

//
// Right shift assignment expression
//

void implementation_generator::visit_right_shift_assignation_expression(right_shift_assignation_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [destination, right] = process_assignment_operands(expr);
    auto* left = checked_legacy_result(destination);
    if(!left || !right) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F03F), expr.first_lexeme(),
            "Internal error: '>>=' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    auto left_ref_type = std::dynamic_pointer_cast<reference_type>(expr.left()->get_type());
    auto left_type = type::remove_const(left_ref_type->get_subtype());
    auto llvm_type = _context->get_llvm_type(left_type);

    auto left_val = _builder->CreateLoad(llvm_type, left);
    if(auto prim = std::dynamic_pointer_cast<primitive_type>(left_type)) {
        if(prim->is_integer()) {
            if(prim->is_unsigned()) {
                // TODO may it poison when overflow ?
                _value = _builder->CreateLShr(left_val, right);
            } else {
                // TODO may it poison when overflow ?
                _value = _builder->CreateAShr(left_val, right);
            }
        } else if(prim->is_float()) {
            throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_DIV_ASSIGN_INCOMPATIBLE), expr.first_lexeme(),
                "Right shift-assignment ('>>=') cannot be applied to floating-point values: "
                "shift operations are only defined for integer types; "
                "the operand has type '{}'",
                {prim->to_string()});
        }
    } else {
        // TODO: Support other types
    }

    // Store the value, return the left ref
    _value = _builder->CreateStore(_value, left);
    publish_expression_result(expr, destination);
}

//
// Arithmetic unary expression
//

} // namespace k::model::gen
