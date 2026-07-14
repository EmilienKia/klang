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
#include "../common/operator_names.hpp"
#include "../parse/ast.hpp"

#include "../errors.hpp"
#include "gen_operators_helpers.hpp"

namespace k::model::gen {

std::pair<std::shared_ptr<function>, std::shared_ptr<expression>>
/**
 * Resolve a binary operator overload for an aggregate type using cast-weight scoring.
 *
 * Steps:
 *   1. Collect member operator candidates from the aggregate.
 *   2. Collect non-member operator candidates from enclosing scopes.
 *   3. Score each candidate by cast_weight on the right operand (and left for non-member).
 *   4. Prefer member operators over non-member when scores are equal.
 *   5. Filter by const-this if the left operand is const.
 *
 * @return {best_func, adapted_right} or {nullptr, nullptr} if no viable match.
 */
type_reference_resolver::resolve_binary_operator_overload(
    const binary_expression& expr,
    const std::shared_ptr<aggregate>& left_agg,
    const std::shared_ptr<expression>& left_expr,
    const std::shared_ptr<expression>& right_expr,
    bool is_const_this)
{
    std::string op_name = get_binary_operator_name(expr);
    if (op_name.empty()) return {nullptr, nullptr};

    // Step 1: Collect member operator candidates from the aggregate
    // Collect all candidate functions: member first (with inheritance), then non-member.
    // collect_member_operators_from_hierarchy implements C++-style name hiding: if the left
    // aggregate itself declares any operator with op_name, only those are returned;
    // otherwise the search recurses into base classes (BFS, diamond-safe).
    std::vector<std::shared_ptr<function>> member_funcs =
        collect_member_operators_from_hierarchy(left_agg, op_name);
    std::vector<std::shared_ptr<function>> non_member_funcs = scope_lookup::lookup_functions(left_expr, op_name);
    // Remove any member functions that leaked into non_member_funcs via scope_lookup.
    // (scope_lookup walks up the element parent chain, which includes the enclosing aggregate.)
    non_member_funcs.erase(
        std::remove_if(non_member_funcs.begin(), non_member_funcs.end(),
            [](const std::shared_ptr<function>& f) { return f->is_member(); }),
        non_member_funcs.end());

    // Subscript operator[] is member-only: discard non-member candidates.
    if (k::op::is_subscript_operator(op_name)) {
        non_member_funcs.clear();
    }

    // Filter member operators by constness: on a const object, only const member operators are callable.
    if (is_const_this && !member_funcs.empty()) {
        bool had_mutable = false;
        std::vector<std::shared_ptr<function>> const_members;
        for (auto& func : member_funcs) {
            if (func->is_const_member()) {
                const_members.push_back(func);
            } else {
                had_mutable = true;
            }
        }
        if (const_members.empty() && had_mutable && non_member_funcs.empty()) {
            std::string op_sym = get_operator_symbol(op_name);
            std::string type_str = left_agg->get_struct_type() ? left_agg->get_struct_type()->to_string() : left_agg->get_short_name();
            throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_BINARY_OVERLOAD_BAD_RECEIVER), expr.first_lexeme(),
                "Cannot call mutable member operator '{}' on a const object of type '{}': "
                "only const member operators can be called on const objects; "
                "declare the operator as 'const' to allow calls on const objects",
                {op_sym, type_str});
        }
        member_funcs = std::move(const_members);
    }

    if (member_funcs.empty() && non_member_funcs.empty()) return {nullptr, nullptr};

    struct CandInfo {
        std::shared_ptr<function> func;
        cast_weight score;
        bool is_member;
        bool is_const_func;
        std::shared_ptr<expression> adapted_right;
    };

    std::vector<CandInfo> valid;

    // Score member operator functions: parameter list has 1 param (the right operand)
    // The left operand ('this') is always an exact match since we looked up on the correct aggregate.
    for (auto& func : member_funcs) {
        const auto& params = func->parameters();
        if (params.size() != 1) continue; // Binary member operator should have exactly 1 explicit param
        auto right_param_type = params[0]->get_type();
        auto w = compute_cast_weight(right_expr, right_param_type);
        if (w != CAST_IMPOSSIBLE) {
            auto adapted = adapt_type(right_expr, right_param_type);
            valid.push_back({func, w, true, func->is_const_member(), adapted ? adapted : right_expr});
        }
    }

    // Step 2: Collect non-member operator candidates from enclosing scopes
    // Score non-member operator functions: parameter list has 2 params (left, right)
    // Must validate BOTH left (params[0]) and right (params[1]) parameter compatibility.
    for (auto& func : non_member_funcs) {
        const auto& params = func->parameters();
        if (params.size() != 2) continue; // Binary non-member operator should have exactly 2 params

        // Validate left parameter: must be compatible with the left expression
        auto left_param_type = params[0]->get_type();
        auto wl = compute_cast_weight(left_expr, left_param_type);
        if (wl == CAST_IMPOSSIBLE) continue;

        // Score right parameter
        auto right_param_type = params[1]->get_type();
        auto wr = compute_cast_weight(right_expr, right_param_type);
        if (wr == CAST_IMPOSSIBLE) continue;

        // Step 3: Score each candidate by cast_weight on the right operand (and left for non-member)
        // Overall score = worst of left and right
        cast_weight w = std::max(wl, wr);
        auto adapted = adapt_type(right_expr, right_param_type);
        valid.push_back({func, w, false, false, adapted ? adapted : right_expr});
    }

    // Step 4: Prefer member operators over non-member when scores are equal
    if (valid.empty()) return {nullptr, nullptr};

    // Step 5: Select best candidate.
    // Best = lowest score; among equal scores, prefer member over non-member.
    // Among equal-score members on a mutable object, prefer mutable over const
    // (spec §9: "On a mutable object, the mutable version is preferred").
    cast_weight best_score = CAST_IMPOSSIBLE;
    bool best_is_member = false;

    for (auto& c : valid) {
        if (c.score < best_score
            || (c.score == best_score && c.is_member && !best_is_member)) {
            best_score = c.score;
            best_is_member = c.is_member;
        }
    }

    std::vector<CandInfo*> best;
    for (auto& c : valid) {
        if (c.score == best_score && c.is_member == best_is_member)
            best.push_back(&c);
    }

    // When multiple member candidates tie, prefer mutable over const on a mutable object.
    if (best.size() > 1 && !is_const_this && best_is_member) {
        std::vector<CandInfo*> mutable_best;
        for (auto* c : best) {
            if (!c->is_const_func)
                mutable_best.push_back(c);
        }
        if (!mutable_best.empty()) {
            best = std::move(mutable_best);
        }
    }

    if (best.size() > 1) {
        std::string op_sym = get_operator_symbol(op_name);
        std::string left_type_str = left_expr->get_type() ? left_expr->get_type()->to_string() : "?";
        auto d = k::log::diagnostic::make_error(static_cast<unsigned int>(k::diag::symbol_diag::ERR_BINARY_OVERLOAD_NOT_FOUND),
            "Ambiguous operator '{}' for type '{}': {} equally viable overloads",
            {op_sym, left_type_str, std::to_string(best.size())});
        if (auto pos = expr.first_lexeme()) d.at(*pos);
        for (auto* c : best) {
            std::string sig;
            bool first = true;
            for (auto& p : c->func->parameters()) {
                if (!first) sig += ", ";
                sig += p->get_type() ? p->get_type()->to_string() : "?";
                first = false;
            }
            d.add_note("  candidate: {} {}({})", {c->is_member ? "[member]" : "[non-member]",
                        c->func->get_fq_name(), sig});
        }
        throw resolution_error(std::move(d));
    }

    return {best[0]->func, best[0]->adapted_right};
}

/**
 * Resolve a unary operator overload for an aggregate type, using cast-weight scoring
 * to select the best match among multiple candidates.
 */
std::shared_ptr<function>
/**
 * Resolve a unary operator overload for an aggregate type using cast-weight scoring.
 *
 * Steps:
 *   1. Collect member operator candidates from the aggregate.
 *   2. Collect non-member operator candidates from enclosing scopes.
 *   3. Score each candidate by cast_weight on the operand.
 *   4. Prefer member operators over non-member when scores are equal.
 *
 * @return The best matching function, or nullptr if no viable match.
 */
type_reference_resolver::resolve_unary_operator_overload(
    const unary_expression& expr,
    const std::shared_ptr<aggregate>& operand_agg,
    const std::shared_ptr<expression>& operand_expr,
    bool is_const_this)
{
    std::string op_name = get_unary_operator_name(expr);
    if (op_name.empty()) return nullptr;

    // Step 1: Collect member operator candidates from the aggregate
    // Collect all candidate functions: member first (with inheritance), then non-member.
    std::vector<std::shared_ptr<function>> member_funcs =
        collect_member_operators_from_hierarchy(operand_agg, op_name);
    std::vector<std::shared_ptr<function>> non_member_funcs = scope_lookup::lookup_functions(operand_expr, op_name);
    // Remove any member functions that leaked into non_member_funcs via scope_lookup.
    non_member_funcs.erase(
        std::remove_if(non_member_funcs.begin(), non_member_funcs.end(),
            [](const std::shared_ptr<function>& f) { return f->is_member(); }),
        non_member_funcs.end());

    // Filter member operators by constness: on a const object, only const member operators are callable.
    if (is_const_this && !member_funcs.empty()) {
        bool had_mutable = false;
        std::vector<std::shared_ptr<function>> const_members;
        for (auto& func : member_funcs) {
            if (func->is_const_member()) {
                const_members.push_back(func);
            } else {
                had_mutable = true;
            }
        }
        if (const_members.empty() && had_mutable && non_member_funcs.empty()) {
            std::string op_sym = get_operator_symbol(op_name);
            std::string type_str = operand_agg->get_struct_type() ? operand_agg->get_struct_type()->to_string() : operand_agg->get_short_name();
            throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_UNARY_OVERLOAD_BAD_RECEIVER), expr.first_lexeme(),
                "Cannot call mutable member operator '{}' on a const object of type '{}': "
                "only const member operators can be called on const objects; "
                "declare the operator as 'const' to allow calls on const objects",
                {op_sym, type_str});
        }
        member_funcs = std::move(const_members);
    }

    if (member_funcs.empty() && non_member_funcs.empty()) return nullptr;

    struct CandInfo {
        std::shared_ptr<function> func;
        cast_weight score;
        bool is_member;
        bool is_const_func;
    };

    std::vector<CandInfo> valid;

    // Score member operator functions: no explicit parameters for unary member operators
    // The operand ('this') is always an exact match since we looked up on the correct aggregate.
    for (auto& func : member_funcs) {
        const auto& params = func->parameters();
        if (!params.empty()) continue; // Unary member operator should have no explicit param
        valid.push_back({func, CAST_NONE, true, func->is_const_member()});
    }

    // Step 2: Collect non-member operator candidates from enclosing scopes
    // Score non-member operator functions: parameter list has 1 param (the operand)
    for (auto& func : non_member_funcs) {
        const auto& params = func->parameters();
        if (params.size() != 1) continue; // Unary non-member operator should have exactly 1 param

        // Step 3: Score each candidate by cast_weight on the operand
        auto operand_param_type = params[0]->get_type();
        auto w = compute_cast_weight(operand_expr, operand_param_type);
        if (w != CAST_IMPOSSIBLE) {
            valid.push_back({func, w, false, false});
        }
    }

    // Step 4: Prefer member operators over non-member when scores are equal
    if (valid.empty()) return nullptr;

    // Best = lowest score; among equal scores, prefer member over non-member.
    // Among equal-score members on a mutable object, prefer mutable over const
    // (spec §9: "On a mutable object, the mutable version is preferred").
    cast_weight best_score = CAST_IMPOSSIBLE;
    bool best_is_member = false;

    for (auto& c : valid) {
        if (c.score < best_score
            || (c.score == best_score && c.is_member && !best_is_member)) {
            best_score = c.score;
            best_is_member = c.is_member;
        }
    }

    std::vector<CandInfo*> best;
    for (auto& c : valid) {
        if (c.score == best_score && c.is_member == best_is_member)
            best.push_back(&c);
    }

    // When multiple member candidates tie, prefer mutable over const on a mutable object.
    if (best.size() > 1 && !is_const_this && best_is_member) {
        std::vector<CandInfo*> mutable_best;
        for (auto* c : best) {
            if (!c->is_const_func)
                mutable_best.push_back(c);
        }
        if (!mutable_best.empty()) {
            best = std::move(mutable_best);
        }
    }

    if (best.size() > 1) {
        std::string op_sym = get_operator_symbol(op_name);
        std::string operand_type_str = operand_expr->get_type() ? operand_expr->get_type()->to_string() : "?";
        auto d = k::log::diagnostic::make_error(static_cast<unsigned int>(k::diag::symbol_diag::ERR_UNARY_OVERLOAD_NOT_FOUND),
            "Ambiguous unary operator '{}' for type '{}': {} equally viable overloads",
            {op_sym, operand_type_str, std::to_string(best.size())});
        for (auto* c : best) {
            std::string sig;
            bool first = true;
            for (auto& p : c->func->parameters()) {
                if (!first) sig += ", ";
                sig += p->get_type() ? p->get_type()->to_string() : "?";
                first = false;
            }
            d.add_note("  candidate: {} {}({})", {c->is_member ? "[member]" : "[non-member]",
                        c->func->get_fq_name(), sig});
        }
        throw resolution_error(std::move(d));
    }

    return best[0]->func;
}

/**
 * Resolve a casting operator overload for an aggregate type.
 * Looks for a member function named "__operator_cv_<encoded_type>" matching the
 * target type of the cast, searching the aggregate's hierarchy.
 */
std::shared_ptr<function>
type_reference_resolver::resolve_cast_operator_overload(
    const std::shared_ptr<aggregate>& source_agg,
    const std::shared_ptr<type>& target_type,
    bool is_const_this)
{
    if (!source_agg || !target_type) return nullptr;

    // Build the canonical operator name from the target type
    std::string encoded = encode_type_for_cast_operator(target_type);
    std::string op_name = "__operator_cv_" + encoded;

    // Collect member operator functions from the hierarchy
    std::vector<std::shared_ptr<function>> member_funcs =
        collect_member_operators_from_hierarchy(source_agg, op_name);

    if (member_funcs.empty()) return nullptr;

    // Filter by constness: on a const object, only const member operators are callable.
    if (is_const_this) {
        std::vector<std::shared_ptr<function>> const_members;
        for (auto& func : member_funcs) {
            if (func->is_const_member()) {
                const_members.push_back(func);
            }
        }
        if (const_members.empty()) return nullptr;
        member_funcs = std::move(const_members);
    }

    // Casting operators have no parameters (other than 'this'), so there's no
    // scoring to do — just return the first (and should be only) match.
    for (auto& func : member_funcs) {
        // Verify it has no explicit parameters
        if (func->parameters().empty()) {
            return func;
        }
    }

    return nullptr;
}

namespace {
} // anonymous namespace

/**
 * Compute virtual dispatch info for an operator overload call on a member function.
 * Similar to annotate_dispatch_info for function_invocation_expression.
 */
virtual_dispatch_info compute_operator_dispatch_info(
    const std::shared_ptr<function>& func,
    const std::shared_ptr<type>& receiver_type)
{
    virtual_dispatch_info di;
    di.kind = virtual_dispatch_info::dispatch_kind::DIRECT;

    if (!func || !func->is_virtual() || func->get_vtable_slot() < 0) {
        return di;
    }

    if (!type::is_reference(receiver_type)) {
        return di;
    }

    auto bare_subtype = type::remove_const(receiver_type->get_subtype());
    auto st_type = std::dynamic_pointer_cast<struct_type>(bare_subtype);
    if (!st_type) {
        return di;
    }

    auto kl = std::dynamic_pointer_cast<klass>(st_type->get_struct());
    if (!kl || !kl->has_vtable()) {
        // Check imported aggregates
        auto imp = std::dynamic_pointer_cast<aggregate>(st_type->get_struct());
        if (imp && imp->has_vtable()) {
            di.kind = virtual_dispatch_info::dispatch_kind::VTABLE;
            di.slot_index = func->get_vtable_slot();
            di.imported_dispatch_agg = imp;
            di.this_adjustment = 0;
            return di;
        }
        return di;
    }

    di.kind = virtual_dispatch_info::dispatch_kind::VTABLE;
    di.slot_index = func->get_vtable_slot();
    di.dispatch_class = kl;
    di.this_adjustment = 0;
    return di;
}


//
// Operator overload code generation helpers
//

/**
 * Generate LLVM IR for a binary operator overload function call.
 *
 * Steps:
 *   1. Evaluate left and right operand expressions.
 *   2. Resolve the operator function (member or non-member).
 *   3. For member operators: load 'this' from left operand, call with right as arg.
 *   4. For non-member operators: call with both operands as args.
 *   5. Handle virtual dispatch if the operator function is virtual.
 *   6. Handle sret return for aggregate return types.
 *
 * @return true if an overload was handled, false if not an overload.
 */
bool implementation_generator::generate_binary_operator_overload(binary_expression& expr) {
    if (!expr.has_operator_overload()) return false;

    auto op_func = expr.get_operator_func();

    // Find the LLVM function (may be null for abstract or external virtual operators)
    auto it = _context->_functions.find(op_func);
    if (it == _context->_functions.end()) {
        if (op_func->is_virtual() &&
            (op_func->is_abstract_func() || op_func->is_external())) {
            // Abstract/external virtual operator: no LLVM definition, dispatch via vtable below.
        } else {
            throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F04A), expr.first_lexeme(),
                "Internal error: operator overload function '{}' has no LLVM definition",
                {op_func->get_short_name()});
        }
    }
    llvm::Function* llvm_func = (it != _context->_functions.end()) ? it->second : nullptr;

    // Build the LLVM FunctionType (from llvm_func if available, or from the model).
    // When llvm_func is non-null its FunctionType already includes the sret parameter
    // (if any).  When reconstructing from the model we must mirror what
    // declaration_generator::visit_function does: prepend a ptr param and use void
    // return when the return type needs sret ABI.
    auto build_fn_type = [&]() -> llvm::FunctionType* {
        if (llvm_func) return llvm_func->getFunctionType();
        // Reconstruct from model
        std::vector<llvm::Type*> param_types;
        // sret parameter comes first (before this)
        bool model_sret = op_func->has_return_type() && needs_sret_return(op_func->get_return_type());
        if (model_sret)
            param_types.push_back(llvm::PointerType::get(**_context, 0));
        if (op_func->is_member() && !op_func->is_static() && op_func->get_this_parameter())
            param_types.push_back(_context->get_llvm_type(op_func->get_this_parameter()->get_type()));
        for (const auto& param : op_func->parameters())
            param_types.push_back(_context->get_llvm_type(param->get_type()));
        llvm::Type* ret_type = llvm::Type::getVoidTy(**_context);
        if (op_func->has_return_type() && !model_sret)
            ret_type = _context->get_llvm_type(op_func->get_return_type());
        return llvm::FunctionType::get(ret_type, param_types, false);
    };

    // Helper: detect whether an operator call uses sret ABI.
    auto op_needs_sret = [&]() -> bool {
        return op_func->has_return_type() && needs_sret_return(op_func->get_return_type());
    };

    // Helper: allocate an sret temporary, insert it at the front of `args`,
    // and track it for destructor cleanup.  Returns the sret alloca pointer.
    auto prepare_sret_for_op = [&](std::vector<llvm::Value*>& args, bool use_sret_destination) -> llvm::AllocaInst* {
        auto ret_type_nc = type::remove_const(op_func->get_return_type());
        llvm::Type* llvm_ret = _context->get_llvm_type(ret_type_nc);
        llvm::AllocaInst* sret_dest = nullptr;
        bool consumed_sret_dest = false;

        if (use_sret_destination && _sret_destination) {
            sret_dest = llvm::dyn_cast<llvm::AllocaInst>(_sret_destination);
            if (!sret_dest) {
                // _sret_destination is not an alloca — create a temp instead
                llvm::Function* cur_fn = _builder->GetInsertBlock()->getParent();
                llvm::IRBuilder<> entry_builder(&cur_fn->getEntryBlock(), cur_fn->getEntryBlock().begin());
                sret_dest = entry_builder.CreateAlloca(llvm_ret, nullptr, "op_sret_tmp");
            } else {
                _sret_destination = nullptr;
                consumed_sret_dest = true;
            }
        } else {
            llvm::Function* cur_fn = _builder->GetInsertBlock()->getParent();
            llvm::IRBuilder<> entry_builder(&cur_fn->getEntryBlock(), cur_fn->getEntryBlock().begin());
            sret_dest = entry_builder.CreateAlloca(llvm_ret, nullptr, "op_sret_tmp");
        }

        args.insert(args.begin(), sret_dest);

        // Track for temporary cleanup only when not consumed from _sret_destination
        if (!consumed_sret_dest) {
            auto ret_st = std::dynamic_pointer_cast<struct_type>(ret_type_nc);
            if (ret_st && ret_st->get_struct()) {
                auto dtor = ret_st->get_struct()->get_destructor();
                if (dtor) {
                    auto dtor_fn = dtor->shared_as<k::model::function>();
                    auto dtor_it = _context->_functions.find(dtor_fn);
                    if (dtor_it != _context->_functions.end())
                        _expression_temporaries.push_back({sret_dest, dtor_it->second, nullptr});
                }
            }
        }
        return sret_dest;
    };

    // Build arguments
    std::vector<llvm::Value*> args;

    if (op_func->is_member()) {
        // Member operator: 'this' is the left operand (a reference/pointer to the struct)
        expr.left()->accept(*this);
        if (!_value) {
            throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F04B), expr.first_lexeme(),
                "Internal error: left operand for operator overload produced no LLVM value");
        }
        args.push_back(_value);

        // Step 1: Evaluate left and right operand expressions
        // Right operand is the argument
        expr.right()->accept(*this);
        if (!_value) {
            throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F04C), expr.first_lexeme(),
                "Internal error: right operand for operator overload produced no LLVM value");
        }
        args.push_back(_value);
    } else {
        // Non-member operator: both operands are arguments
        expr.left()->accept(*this);
        if (!_value) {
            throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F04D), expr.first_lexeme(),
                "Internal error: left operand for non-member operator overload produced no LLVM value");
        }
        args.push_back(_value);

        // Step 2: Resolve the operator function (member or non-member)
        expr.right()->accept(*this);
        if (!_value) {
            throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F04E), expr.first_lexeme(),
                "Internal error: right operand for non-member operator overload produced no LLVM value");
        }
        args.push_back(_value);
    }

    // Check for virtual dispatch
    if (expr.has_operator_dispatch_info()) {
        auto& di = expr.get_operator_dispatch_info();
        if (di.kind == virtual_dispatch_info::dispatch_kind::VTABLE) {
            llvm::FunctionType* fn_type = build_fn_type();
            bool is_sret = fn_type->getReturnType()->isVoidTy() && op_needs_sret();
            if (di.dispatch_class) {
                // Local class: use the standard virtual dispatch helper
                if (is_sret) {
                    auto* sret_tmp = prepare_sret_for_op(args, false);
                    emit_virtual_dispatch_call(*_builder, *di.dispatch_class, args[1],
                        di.slot_index, fn_type, args, _context, "op_vcall");
                    _value = sret_tmp;
                    return true;
                }
                auto result = emit_virtual_dispatch_call(*_builder, *di.dispatch_class, args[0],
                    di.slot_index, fn_type, args, _context, "op_vcall");
                if (result) {
                    _value = result;
                    return true;
                }
                // Fallback: emit_virtual_dispatch_call returned nullptr (vtable not ready?)
            }
            if (di.imported_dispatch_agg) {
                // Imported class: use byte-offset GEP (same as function invocation for imports)
                auto imp_agg = di.imported_dispatch_agg;
                auto* struct_llvm_type = imp_agg->get_struct_type()
                                         ? imp_agg->get_struct_type()->get_llvm_type() : nullptr;
                if (struct_llvm_type) {
                    llvm::LLVMContext& llvm_ctx = **_context;
                    llvm::Type* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);
                    // When sret is used, this_ptr is at args[1] (after sret); GEP on args[0] is wrong
                    if (is_sret) {
                        auto* sret_tmp = prepare_sret_for_op(args, false);
                        // this_ptr is now at args[1]
                        llvm::Value* vptr_addr = _builder->CreateStructGEP(
                            struct_llvm_type, args[1], 0, "op_imp_vptr_addr");
                        llvm::Value* vptr = _builder->CreateLoad(ptr_ty, vptr_addr, "op_imp_vptr");
                        const uint64_t ptr_size = 8;
                        llvm::Value* slot_offset = llvm::ConstantInt::get(
                            llvm::Type::getInt64Ty(llvm_ctx),
                            (di.slot_index + 1) * ptr_size);
                        llvm::Value* fn_ptr_addr = _builder->CreateInBoundsGEP(
                            llvm::Type::getInt8Ty(llvm_ctx), vptr, slot_offset, "op_imp_vtbl_slot");
                        llvm::Value* fn_ptr = _builder->CreateLoad(ptr_ty, fn_ptr_addr, "op_imp_fn_ptr");
                        _builder->CreateCall(fn_type, fn_ptr, args);
                        _value = sret_tmp;
                        return true;
                    }
                    llvm::Value* vptr_addr = _builder->CreateStructGEP(
                        struct_llvm_type, args[0], 0, "op_imp_vptr_addr");
                    llvm::Value* vptr = _builder->CreateLoad(ptr_ty, vptr_addr, "op_imp_vptr");
                    const uint64_t ptr_size = 8;
                    llvm::Value* slot_offset = llvm::ConstantInt::get(
                        llvm::Type::getInt64Ty(llvm_ctx),
                        (di.slot_index + 1) * ptr_size);
                    llvm::Value* fn_ptr_addr = _builder->CreateInBoundsGEP(
                        llvm::Type::getInt8Ty(llvm_ctx), vptr, slot_offset, "op_imp_vtbl_slot");
                    llvm::Value* fn_ptr = _builder->CreateLoad(ptr_ty, fn_ptr_addr, "op_imp_fn_ptr");
                    _value = _builder->CreateCall(fn_type, fn_ptr, args,
                        fn_type->getReturnType()->isVoidTy() ? "" : "op_imp_vcall");
                    return true;
                }
            }
        }
    }

    if (!llvm_func) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F04A), expr.first_lexeme(),
            "Internal error: operator overload function '{}' has no LLVM definition and is not dispatched virtually",
            {op_func->get_short_name()});
    }

    // Step 3: For member operators: load 'this' from left operand, call with right as arg
    // Direct call
    bool op_uses_sret = llvm_func->getReturnType()->isVoidTy() && op_needs_sret();
    if (op_uses_sret) {
        auto* sret_dest = prepare_sret_for_op(args, true);
        _builder->CreateCall(llvm_func, args);
        _value = sret_dest;
    } else {
        _value = _builder->CreateCall(llvm_func, args,
            llvm_func->getReturnType()->isVoidTy() ? "" : "op_call");
    }
    return true;
}

/**
 * Generate LLVM IR for a unary operator overload function call.
 *
 * Steps:
 *   1. Evaluate the operand expression.
 *   2. Resolve the operator function (member or non-member).
 *   3. For member operators: load 'this' from operand, call with no additional args.
 *   4. For non-member operators: call with operand as arg.
 *   5. Handle virtual dispatch and sret return.
 *
 * @return true if an overload was handled, false if not an overload.
 */
bool implementation_generator::generate_unary_operator_overload(unary_expression& expr) {
    // Step 1: Evaluate the operand expression
    if (!expr.has_operator_overload()) return false;

    // Step 2: Resolve the operator function (member or non-member)
    auto op_func = expr.get_operator_func();

    // Find the LLVM function (may be null for abstract or external virtual operators)
    auto it = _context->_functions.find(op_func);
    if (it == _context->_functions.end()) {
        if (op_func->is_virtual() &&
            (op_func->is_abstract_func() || op_func->is_external())) {
            // Abstract/external virtual operator: no LLVM definition, dispatch via vtable below.
        } else {
            throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F04F), expr.first_lexeme(),
                "Internal error: operator overload function '{}' has no LLVM definition",
                {op_func->get_short_name()});
        }
    }
    llvm::Function* llvm_func = (it != _context->_functions.end()) ? it->second : nullptr;

    // Build the LLVM FunctionType (from llvm_func if available, or from the model).
    // Must match declaration_generator::visit_function: sret param first, void return.
    auto build_fn_type = [&]() -> llvm::FunctionType* {
        if (llvm_func) return llvm_func->getFunctionType();
        // Reconstruct from model
        std::vector<llvm::Type*> param_types;
        bool model_sret = op_func->has_return_type() && needs_sret_return(op_func->get_return_type());
        if (model_sret)
            param_types.push_back(llvm::PointerType::get(**_context, 0));
        if (op_func->is_member() && !op_func->is_static() && op_func->get_this_parameter())
            param_types.push_back(_context->get_llvm_type(op_func->get_this_parameter()->get_type()));
        for (const auto& param : op_func->parameters())
            param_types.push_back(_context->get_llvm_type(param->get_type()));
        llvm::Type* ret_type = llvm::Type::getVoidTy(**_context);
        if (op_func->has_return_type() && !model_sret)
            ret_type = _context->get_llvm_type(op_func->get_return_type());
        return llvm::FunctionType::get(ret_type, param_types, false);
    };

    auto op_needs_sret = [&]() -> bool {
        return op_func->has_return_type() && needs_sret_return(op_func->get_return_type());
    };

    // Helper: allocate sret temp, insert at front of args, track cleanup. Returns alloca.
    auto prepare_sret_for_uop = [&](std::vector<llvm::Value*>& args) -> llvm::AllocaInst* {
        auto ret_type_nc = type::remove_const(op_func->get_return_type());
        llvm::Type* llvm_ret = _context->get_llvm_type(ret_type_nc);
        llvm::Function* cur_fn = _builder->GetInsertBlock()->getParent();
        llvm::IRBuilder<> entry_builder(&cur_fn->getEntryBlock(), cur_fn->getEntryBlock().begin());
        auto* sret_tmp = entry_builder.CreateAlloca(llvm_ret, nullptr, "uop_sret_tmp");
        args.insert(args.begin(), sret_tmp);
        // Track for cleanup
        auto ret_st = std::dynamic_pointer_cast<struct_type>(ret_type_nc);
        if (ret_st && ret_st->get_struct()) {
            auto dtor = ret_st->get_struct()->get_destructor();
            if (dtor) {
                auto dtor_fn = dtor->shared_as<k::model::function>();
                auto dtor_it = _context->_functions.find(dtor_fn);
                if (dtor_it != _context->_functions.end())
                    _expression_temporaries.push_back({sret_tmp, dtor_it->second, nullptr});
            }
        }
        return sret_tmp;
    };

    // Step 3: For member operators: load 'this' from operand, call with no additional args
    // Build arguments
    std::vector<llvm::Value*> args;

    // Step 4: For non-member operators: call with operand as arg
    if (op_func->is_member()) {
        // Member operator: 'this' is the operand (a reference/pointer to the struct)
        expr.sub_expr()->accept(*this);
        if (!_value) {
            throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F050), expr.first_lexeme(),
                "Internal error: operand for unary operator overload produced no LLVM value");
        }
        args.push_back(_value);
    } else {
        // Non-member operator: operand is the argument
        expr.sub_expr()->accept(*this);
        if (!_value) {
            throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F051), expr.first_lexeme(),
                "Internal error: operand for non-member unary operator overload produced no LLVM value");
        }
        args.push_back(_value);
    }

    // Step 5: Handle virtual dispatch and sret return
    // Check for virtual dispatch
    if (expr.has_operator_dispatch_info()) {
        auto& di = expr.get_operator_dispatch_info();
        if (di.kind == virtual_dispatch_info::dispatch_kind::VTABLE) {
            llvm::FunctionType* fn_type = build_fn_type();
            bool is_sret = fn_type->getReturnType()->isVoidTy() && op_needs_sret();
            if (di.dispatch_class) {
                if (is_sret) {
                    auto* sret_tmp = prepare_sret_for_uop(args);
                    emit_virtual_dispatch_call(*_builder, *di.dispatch_class, args[1],
                        di.slot_index, fn_type, args, _context, "uop_vcall");
                    _value = sret_tmp;
                    return true;
                }
                auto result = emit_virtual_dispatch_call(*_builder, *di.dispatch_class, args[0],
                    di.slot_index, fn_type, args, _context, "uop_vcall");
                if (result) {
                    _value = result;
                    return true;
                }
            }
            if (di.imported_dispatch_agg) {
                auto imp_agg = di.imported_dispatch_agg;
                auto* struct_llvm_type = imp_agg->get_struct_type()
                                         ? imp_agg->get_struct_type()->get_llvm_type() : nullptr;
                if (struct_llvm_type) {
                    llvm::LLVMContext& llvm_ctx = **_context;
                    llvm::Type* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);
                    if (is_sret) {
                        auto* sret_tmp = prepare_sret_for_uop(args);
                        llvm::Value* vptr_addr = _builder->CreateStructGEP(
                            struct_llvm_type, args[1], 0, "uop_imp_vptr_addr");
                        llvm::Value* vptr = _builder->CreateLoad(ptr_ty, vptr_addr, "uop_imp_vptr");
                        const uint64_t ptr_size = 8;
                        llvm::Value* slot_offset = llvm::ConstantInt::get(
                            llvm::Type::getInt64Ty(llvm_ctx),
                            (di.slot_index + 1) * ptr_size);
                        llvm::Value* fn_ptr_addr = _builder->CreateInBoundsGEP(
                            llvm::Type::getInt8Ty(llvm_ctx), vptr, slot_offset, "uop_imp_vtbl_slot");
                        llvm::Value* fn_ptr = _builder->CreateLoad(ptr_ty, fn_ptr_addr, "uop_imp_fn_ptr");
                        _builder->CreateCall(fn_type, fn_ptr, args);
                        _value = sret_tmp;
                        return true;
                    }
                    llvm::Value* vptr_addr = _builder->CreateStructGEP(
                        struct_llvm_type, args[0], 0, "uop_imp_vptr_addr");
                    llvm::Value* vptr = _builder->CreateLoad(ptr_ty, vptr_addr, "uop_imp_vptr");
                    const uint64_t ptr_size = 8;
                    llvm::Value* slot_offset = llvm::ConstantInt::get(
                        llvm::Type::getInt64Ty(llvm_ctx),
                        (di.slot_index + 1) * ptr_size);
                    llvm::Value* fn_ptr_addr = _builder->CreateInBoundsGEP(
                        llvm::Type::getInt8Ty(llvm_ctx), vptr, slot_offset, "uop_imp_vtbl_slot");
                    llvm::Value* fn_ptr = _builder->CreateLoad(ptr_ty, fn_ptr_addr, "uop_imp_fn_ptr");
                    _value = _builder->CreateCall(fn_type, fn_ptr, args,
                        fn_type->getReturnType()->isVoidTy() ? "" : "uop_imp_vcall");
                    return true;
                }
            }
        }
    }

    if (!llvm_func) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F04F), expr.first_lexeme(),
            "Internal error: operator overload function '{}' has no LLVM definition and is not dispatched virtually",
            {op_func->get_short_name()});
    }

    // Direct call
    bool op_uses_sret = llvm_func->getReturnType()->isVoidTy() && op_needs_sret();
    if (op_uses_sret) {
        auto* sret_tmp = prepare_sret_for_uop(args);
        _builder->CreateCall(llvm_func, args);
        _value = sret_tmp;
    } else {
        _value = _builder->CreateCall(llvm_func, args,
            llvm_func->getReturnType()->isVoidTy() ? "" : "uop_call");
    }
    return true;
}

/**
 * Generate LLVM IR for a casting operator overload function call.
 *
 * Steps:
 *   1. Evaluate the source expression.
 *   2. Resolve the operator_cast function on the source aggregate type.
 *   3. Call the casting operator with 'this' pointer.
 *   4. Handle virtual dispatch and sret return.
 *
 * @return true if an overload was handled, false if not an overload.
 */
bool implementation_generator::generate_cast_operator_overload(cast_expression& expr) {
    if (!expr.has_operator_overload()) return false;

    auto op_func = expr.get_operator_func();

    // Find the LLVM function (may be null for abstract or external virtual operators)
    auto it = _context->_functions.find(op_func);
    if (it == _context->_functions.end()) {
        if (op_func->is_virtual() &&
            (op_func->is_abstract_func() || op_func->is_external())) {
            // Abstract/external virtual operator: no LLVM definition, dispatch via vtable below.
        } else {
            throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F052), expr.first_lexeme(),
                "Internal error: casting operator function '{}' has no LLVM definition",
                {op_func->get_short_name()});
        }
    }
    llvm::Function* llvm_func = (it != _context->_functions.end()) ? it->second : nullptr;

    // Build the LLVM FunctionType (from llvm_func if available, or from the model).
    // Must match declaration_generator::visit_function: sret param first, void return.
    auto build_fn_type = [&]() -> llvm::FunctionType* {
        if (llvm_func) return llvm_func->getFunctionType();
        // Reconstruct from model
        std::vector<llvm::Type*> param_types;
        bool model_sret = op_func->has_return_type() && needs_sret_return(op_func->get_return_type());
        if (model_sret)
            param_types.push_back(llvm::PointerType::get(**_context, 0));
        if (op_func->is_member() && !op_func->is_static() && op_func->get_this_parameter())
            param_types.push_back(_context->get_llvm_type(op_func->get_this_parameter()->get_type()));
        for (const auto& param : op_func->parameters())
            param_types.push_back(_context->get_llvm_type(param->get_type()));
        llvm::Type* ret_type = llvm::Type::getVoidTy(**_context);
        if (op_func->has_return_type() && !model_sret)
            ret_type = _context->get_llvm_type(op_func->get_return_type());
        return llvm::FunctionType::get(ret_type, param_types, false);
    };

    auto op_needs_sret = [&]() -> bool {
        return op_func->has_return_type() && needs_sret_return(op_func->get_return_type());
    };

    // Helper: allocate sret temp, insert at front of args, track cleanup. Returns alloca.
    auto prepare_sret_for_cast = [&](std::vector<llvm::Value*>& args, bool use_sret_destination) -> llvm::AllocaInst* {
        auto ret_type_nc = type::remove_const(op_func->get_return_type());
        llvm::Type* llvm_ret = _context->get_llvm_type(ret_type_nc);
        llvm::AllocaInst* sret_dest = nullptr;
        bool consumed_sret_dest = false;

        if (use_sret_destination && _sret_destination) {
            sret_dest = llvm::dyn_cast<llvm::AllocaInst>(_sret_destination);
            if (!sret_dest) {
                llvm::Function* cur_fn = _builder->GetInsertBlock()->getParent();
                llvm::IRBuilder<> entry_builder(&cur_fn->getEntryBlock(), cur_fn->getEntryBlock().begin());
                sret_dest = entry_builder.CreateAlloca(llvm_ret, nullptr, "cast_sret_tmp");
            } else {
                _sret_destination = nullptr;
                consumed_sret_dest = true;
            }
        } else {
            llvm::Function* cur_fn = _builder->GetInsertBlock()->getParent();
            llvm::IRBuilder<> entry_builder(&cur_fn->getEntryBlock(), cur_fn->getEntryBlock().begin());
            sret_dest = entry_builder.CreateAlloca(llvm_ret, nullptr, "cast_sret_tmp");
        }

        args.insert(args.begin(), sret_dest);

        if (!consumed_sret_dest) {
            auto ret_st = std::dynamic_pointer_cast<struct_type>(ret_type_nc);
            if (ret_st && ret_st->get_struct()) {
                auto dtor = ret_st->get_struct()->get_destructor();
                if (dtor) {
                    auto dtor_fn = dtor->shared_as<k::model::function>();
                    auto dtor_it = _context->_functions.find(dtor_fn);
                    if (dtor_it != _context->_functions.end())
                        _expression_temporaries.push_back({sret_dest, dtor_it->second, nullptr});
                }
            }
        }
        return sret_dest;
    };

    // Step 1: Evaluate the source expression
    // Build arguments: only 'this' (the source object being cast)
    std::vector<llvm::Value*> args;

    // Step 2: Resolve the operator_cast function on the source aggregate type
    // Member casting operator: 'this' is the source operand (a reference/pointer to the struct)
    expr.sub_expr()->accept(*this);
    if (!_value) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F053), expr.first_lexeme(),
            "Internal error: source operand for casting operator overload produced no LLVM value");
    }
    args.push_back(_value);

    // Step 3: Call the casting operator with 'this' pointer
    // Check for virtual dispatch
    if (expr.has_operator_dispatch_info()) {
        auto& di = expr.get_operator_dispatch_info();
        if (di.kind == virtual_dispatch_info::dispatch_kind::VTABLE) {
            llvm::FunctionType* fn_type = build_fn_type();
            bool is_sret = fn_type->getReturnType()->isVoidTy() && op_needs_sret();
            if (di.dispatch_class) {
                if (is_sret) {
                    auto* sret_tmp = prepare_sret_for_cast(args, false);
                    emit_virtual_dispatch_call(*_builder, *di.dispatch_class, args[1],
                        di.slot_index, fn_type, args, _context, "cast_vcall");
                    _value = sret_tmp;
                    return true;
                }
                auto result = emit_virtual_dispatch_call(*_builder, *di.dispatch_class, args[0],
                    di.slot_index, fn_type, args, _context, "cast_vcall");
                if (result) {
                    _value = result;
                    return true;
                }
            }
            if (di.imported_dispatch_agg) {
                auto imp_agg = di.imported_dispatch_agg;
                auto* struct_llvm_type = imp_agg->get_struct_type()
                                         ? imp_agg->get_struct_type()->get_llvm_type() : nullptr;
                if (struct_llvm_type) {
                    llvm::LLVMContext& llvm_ctx = **_context;
                    llvm::Type* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);
                    if (is_sret) {
                        auto* sret_tmp = prepare_sret_for_cast(args, false);
                        llvm::Value* vptr_addr = _builder->CreateStructGEP(
                            struct_llvm_type, args[1], 0, "cast_imp_vptr_addr");
                        llvm::Value* vptr = _builder->CreateLoad(ptr_ty, vptr_addr, "cast_imp_vptr");
                        const uint64_t ptr_size = 8;
                        llvm::Value* slot_offset = llvm::ConstantInt::get(
                            llvm::Type::getInt64Ty(llvm_ctx),
                            (di.slot_index + 1) * ptr_size);
                        llvm::Value* fn_ptr_addr = _builder->CreateInBoundsGEP(
                            llvm::Type::getInt8Ty(llvm_ctx), vptr, slot_offset, "cast_imp_vtbl_slot");
                        llvm::Value* fn_ptr = _builder->CreateLoad(ptr_ty, fn_ptr_addr, "cast_imp_fn_ptr");
                        _builder->CreateCall(fn_type, fn_ptr, args);
                        _value = sret_tmp;
                        return true;
                    }
                    llvm::Value* vptr_addr = _builder->CreateStructGEP(
                        struct_llvm_type, args[0], 0, "cast_imp_vptr_addr");
                    llvm::Value* vptr = _builder->CreateLoad(ptr_ty, vptr_addr, "cast_imp_vptr");
                    const uint64_t ptr_size = 8;
                    llvm::Value* slot_offset = llvm::ConstantInt::get(
                        llvm::Type::getInt64Ty(llvm_ctx),
                        (di.slot_index + 1) * ptr_size);
                    llvm::Value* fn_ptr_addr = _builder->CreateInBoundsGEP(
                        llvm::Type::getInt8Ty(llvm_ctx), vptr, slot_offset, "cast_imp_vtbl_slot");
                    llvm::Value* fn_ptr = _builder->CreateLoad(ptr_ty, fn_ptr_addr, "cast_imp_fn_ptr");
                    _value = _builder->CreateCall(fn_type, fn_ptr, args,
                        fn_type->getReturnType()->isVoidTy() ? "" : "cast_imp_vcall");
                    return true;
                }
            }
        }
    }

    if (!llvm_func) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F052), expr.first_lexeme(),
            "Internal error: casting operator function '{}' has no LLVM definition and is not dispatched virtually",
            {op_func->get_short_name()});
    }

    // Step 4: Handle virtual dispatch and sret return
    // Direct call
    bool op_uses_sret = llvm_func->getReturnType()->isVoidTy() && op_needs_sret();
    if (op_uses_sret) {
        auto* sret_dest = prepare_sret_for_cast(args, true);
        _builder->CreateCall(llvm_func, args);
        _value = sret_dest;
    } else {
        _value = _builder->CreateCall(llvm_func, args,
            llvm_func->getReturnType()->isVoidTy() ? "" : "cast_call");
    }
    return true;
}

} // namespace k::model::gen
