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
#include "../common/llvm_scope_exit.hpp"
#include "../model/expressions.hpp"
#include "../model/statements.hpp"
#include "../model/operators.hpp"
#include "../model/mangler.hpp"
#include "../model/imported.hpp"
#include "../model/template.hpp"
#include "../model/template_instantiator.hpp"
#include "../parse/ast.hpp"
#include "../../../libkdi/src/kdi_aggregates.hpp"
#include "llvm/Support/raw_os_ostream.h"
#include <llvm/IR/Constants.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Intrinsics.h>
#include <type_traits>
#include <unordered_set>
#include "../errors.hpp"
namespace k::model::gen {
// unary/binary base + address_of, drain, load_value, dereference

implementation_generator::value_result implementation_generator::load_place(
    place_result place, llvm::Type* type, const llvm::Twine& name) {
    return {_builder->CreateLoad(type, place.address, name)};
}

implementation_generator::value_result implementation_generator::address_of_place(place_result place) {
    return {place.address};
}

implementation_generator::place_result implementation_generator::dereference_address(value_result address) {
    return {address.value};
}

implementation_generator::expression_result implementation_generator::evaluate_expression(expression& expr) {
    expression_result_frame frame{&expr, std::nullopt};
    auto* previous = _expression_result_frame;
    _expression_result_frame = &frame;
    auto restore = k::make_scope_exit([&] { _expression_result_frame = previous; });
    _value = nullptr;
    expr.accept(*this);
    if (frame.result) return std::move(*frame.result);
    return legacy_result{_value};
}

void implementation_generator::publish_expression_result(expression& expr, expression_result result) {
    if (_expression_result_frame && _expression_result_frame->target == &expr) {
        _expression_result_frame->result = std::move(result);
        _value = nullptr;
    } else {
        _value = checked_legacy_result(result);
    }
}

llvm::Value* implementation_generator::checked_legacy_result(const expression_result& result) {
    return std::visit([&](const auto& item) -> llvm::Value* {
        using result_type = std::decay_t<decltype(item)>;
        if constexpr (std::is_same_v<result_type, place_result>) {
            if (item.address && !item.address->getType()->isPointerTy()) {
                throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F023), {},
                    "Internal error: a place cannot be passed to legacy codegen without an address");
            }
            return item.address;
        } else if constexpr (std::is_same_v<result_type, value_result> ||
                             std::is_same_v<result_type, legacy_result>) {
            return item.value;
        } else {
            throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F023), {},
                "Internal error: expression result cannot be passed to legacy codegen");
        }
    }, result);
}

implementation_generator::place_result implementation_generator::require_place(
    const expression_result& result, expression& source) {
    if (auto* place = std::get_if<place_result>(&result)) return *place;
    if (auto* legacy = std::get_if<legacy_result>(&result)) {
        auto source_type = source.get_type();
        if ((type::is_reference(source_type) || type::is_drain(source_type))
            && (!legacy->value || legacy->value->getType()->isPointerTy())) {
            return {legacy->value};
        }
    }
    throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F023), source.first_lexeme(),
        "Internal error: expression did not produce an addressable place");
}

implementation_generator::value_result implementation_generator::require_value(
    const expression_result& result, expression& source) {
    if (auto* value = std::get_if<value_result>(&result)) return *value;
    if (auto* legacy = std::get_if<legacy_result>(&result)) {
        auto source_type = source.get_type();
        if (!type::is_reference(source_type) && !type::is_drain(source_type)) {
            return {legacy->value};
        }
    }
    throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F023), source.first_lexeme(),
        "Internal error: expression did not produce a value");
}

void symbol_resolver::visit_unary_expression(unary_expression& expr)
{
    auto& sub = expr.sub_expr();
    if(!sub) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F001), expr.first_lexeme(),
            "Internal error: unary expression has a null sub-expression; "
            "this indicates a malformed AST or a compiler bug");
    }
    sub->accept(*this);
}

void type_reference_resolver::visit_unary_expression(unary_expression& expr)
{
    auto& sub = expr.sub_expr();

    if(!sub) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F004), expr.first_lexeme(),
            "Internal error: unary expression has a null sub-expression; "
            "this indicates a malformed AST or a compiler bug");
    }

    sub->accept(*this);

    if(!type::is_resolved(sub->get_type())) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F005), expr.first_lexeme(),
            "Internal error: sub-expression of a unary operator could not be type-resolved; "
            "the type of the operand must be known before the unary expression can be typed");
    }
}

llvm::Value* implementation_generator::process_unary_expression(unary_expression& expr) {
    llvm::Value* res = nullptr;
    _value = nullptr;
    expr.sub_expr()->accept(*this);
    res = _value;
    _value = nullptr;
    return res;
}

//
// Binary expression
//

void symbol_resolver::visit_binary_expression(binary_expression& expr)
{
    auto& left = expr.left();
    auto& right = expr.right();

    if(!left || !right) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F002), expr.first_lexeme(),
            "Internal error: binary expression has a null left or right operand; "
            "this indicates a malformed AST or a compiler bug");
    }

    left->accept(*this);
    right->accept(*this);

}

void type_reference_resolver::visit_binary_expression(binary_expression& expr)
{
    if(!expr.left() || !expr.right()) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F006), expr.first_lexeme(),
            "Internal error: binary expression has a null left or right operand; "
            "this indicates a malformed AST or a compiler bug");
    }

    // Visit the left operand and consume any replacement produced by inline
    // temporary construction (e.g. S(args) as an operand).
    _replacement_expr = nullptr;
    expr.left()->accept(*this);
    if (_replacement_expr) {
        expr.assign_left(_replacement_expr);
        _replacement_expr = nullptr;
    }

    // Visit the right operand similarly.
    _replacement_expr = nullptr;
    expr.right()->accept(*this);
    if (_replacement_expr) {
        expr.assign_right(_replacement_expr);
        _replacement_expr = nullptr;
    }

    if(!type::is_resolved(expr.left()->get_type())) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F007), expr.first_lexeme(),
            "Internal error: the left operand of a binary operator could not be type-resolved; "
            "the type of each operand must be known before the binary expression can be typed");
    }
    if(!type::is_resolved(expr.right()->get_type())) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F008), expr.first_lexeme(),
            "Internal error: the right operand of a binary operator could not be type-resolved; "
            "the type of each operand must be known before the binary expression can be typed");
    }
}


std::pair<llvm::Value*,llvm::Value*> implementation_generator::process_binary_expression(binary_expression & expr) {
    std::pair<llvm::Value*,llvm::Value*> res;
    _value = nullptr;
    expr.left()->accept(*this);
    res.first = _value;
    _value = nullptr;
    expr.right()->accept(*this);
    res.second = _value;
    _value = nullptr;
    return res;
}

//
// Address of expression
//

void type_reference_resolver::visit_address_of_expression(address_of_expression& expr) {
    default_model_visitor::visit_address_of_expression(expr);

    auto sub_expr = expr.sub_expr();
    auto sub_type = sub_expr->get_type();

    if(!type::is_reference(sub_type)) {
        throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_UNRESOLVED_TYPE_EXPR), expr.first_lexeme(),
            "Cannot take the address of a non-reference expression: "
            "the '&' operator requires a reference (i.e. an addressable location) as its operand, "
            "but the operand has type '{}'",
            {sub_type ? sub_type->to_string() : "?"});
    }

    // &ref<T> produces a link_type (mutable, non-null address).
    // &ref<const T> produces a link_type to const T: const T~
    auto inner = sub_type->get_subtype(); // T or const T
    expr.set_type(inner->get_link());
}

void implementation_generator::visit_address_of_expression(address_of_expression& expr) {
    set_debug_location(expr.first_lexeme());
    auto operand_result = evaluate_expression(*expr.sub_expr());
    auto operand = require_place(operand_result, *expr.sub_expr());
    if(!operand.address) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F023), expr.first_lexeme(),
            "Internal error: the sub-expression of an address-of ('&') operator produced no LLVM value; "
            "this indicates a code-generation bug");
    }
    publish_expression_result(expr, address_of_place(operand));
}

//
// Drain expression (#expr)
//

void type_reference_resolver::visit_drain_expression(drain_expression& expr) {
    default_model_visitor::visit_drain_expression(expr);

    auto sub_expr = expr.sub_expr();
    auto sub_type = sub_expr->get_type();

    // Unwrap reference to get to the actual type
    std::shared_ptr<type> inner;
    if (auto ref_type = std::dynamic_pointer_cast<reference_type>(sub_type)) {
        inner = ref_type->get_subtype();
    } else if (type::is_drain(sub_type)) {
        // #(drain) is a no-op: already a drain
        expr.set_type(sub_type);
        return;
    } else {
        throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_PM_EXPR_BAD_TYPE), expr.first_lexeme(),
            "Cannot drain a non-reference expression: "
            "the '#' operator requires a reference (i.e. an addressable location) as its operand, "
            "but the operand has type '{}'",
            {sub_type ? sub_type->to_string() : "?"});
    }

    // Cannot drain a const object
    if (type::is_const(inner)) {
        throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_PM_EXPR_NOT_MEMBER_PTR), expr.first_lexeme(),
            "Cannot drain a const object: "
            "the '#' operator requires a mutable object, "
            "but the operand has type '{}'",
            {sub_type ? sub_type->to_string() : "?"});
    }

    // Produce drain<T> from ref<T>
    expr.set_type(inner->get_drain());
}

void implementation_generator::visit_drain_expression(drain_expression& expr) {
    set_debug_location(expr.first_lexeme());
    _value = nullptr;
    expr.sub_expr()->accept(*this);

    if(!_value) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F028), expr.first_lexeme(),
            "Internal error: the sub-expression of a drain ('#') operator produced no LLVM value; "
            "this indicates a code-generation bug");
    }
    // Drain is semantically identical to a reference at LLVM level — just an address.
    // _value = _value;
}

//
// Load value expression
//

void type_reference_resolver::visit_load_value_expression(load_value_expression& expr) {
    auto type = expr.sub_expr()->get_type();

    if(auto ref_type = std::dynamic_pointer_cast<reference_type>(type)) {
        // Strip const when loading a value: const int& → int (the loaded value is not const itself)
        expr.set_type(k::model::type::remove_const(ref_type->get_subtype()));
    } else if(auto ptr_type = std::dynamic_pointer_cast<pointer_type>(type)) {
        expr.set_type(k::model::type::remove_const(ptr_type->get_subtype()));
    } else if(auto drn_type = std::dynamic_pointer_cast<drain_type>(type)) {
        // Loading through a drain is the same as loading through a reference
        expr.set_type(k::model::type::remove_const(drn_type->get_drained_type()));
    } else {
        throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_DEREF_NOT_POINTER), expr.first_lexeme(),
            "Cannot dereference a non-pointer/non-reference expression: "
            "load ('*') requires a reference or pointer operand, "
            "but the operand has type '{}'",
            {type ? type->to_string() : "?"});
    }

    if (expr.sub_expr()->is_constant()) {
        expr.set_constant_value(expr.sub_expr()->get_constant_value());
    }
}

void implementation_generator::visit_load_value_expression(load_value_expression& expr) {
    set_debug_location(expr.first_lexeme());

    if (expr.is_constant()) {
        auto t = type::remove_const(expr.get_type());
        if (t && (type::is_primitive(t) || type::is_enum(t) || type::is_null(t))) {
            auto* constant = _context->get_llvm_constant_from_constant_value(expr.get_constant_value(), expr.get_type());
            if (constant) {
                publish_expression_result(expr, value_result{constant});
                return;
            }
        }
    }

    auto* construction_destination = _sret_destination;
    auto operand = evaluate_expression(*expr.sub_expr());
    // Use the expression's own type if set; fall back to the sub-expression's referenced type.
    auto load_type = expr.get_type();
    if (!load_type) {
        auto sub_t = expr.sub_expr()->get_type();
        if (auto ref_t = std::dynamic_pointer_cast<reference_type>(sub_t)) {
            load_type = k::model::type::remove_const(ref_t->get_subtype());
        } else if (auto ptr_t = std::dynamic_pointer_cast<pointer_type>(sub_t)) {
            load_type = k::model::type::remove_const(ptr_t->get_subtype());
        } else if (auto drn_t = std::dynamic_pointer_cast<drain_type>(sub_t)) {
            load_type = k::model::type::remove_const(drn_t->get_drained_type());
        }
    }
    if (load_type) {
        auto sub_type = expr.sub_expr()->get_type();
        auto source = type::is_reference(sub_type) || type::is_drain(sub_type)
            ? require_place(operand, *expr.sub_expr())
            : dereference_address(require_value(operand, *expr.sub_expr()));
        llvm::Value* src_ptr = source.address;

        // Construct a value from an lvalue or drain in staging storage. A drain
        // constructor may transfer resources; a copy constructor must not alias them.
        auto st_type_nc = std::dynamic_pointer_cast<struct_type>(
            type::remove_const(type::canonical(load_type)));
        if (st_type_nc) {
            auto sub_type = expr.sub_expr()->get_type();
            bool src_is_lvalue_ref = type::is_reference(sub_type) || type::is_drain(sub_type);
            // A prvalue constructed directly into its destination is deliberately
            // untracked: the destination owns it, and copying it would strand the
            // original object's resources when that same destination is overwritten.
            bool src_is_prvalue_temp = std::dynamic_pointer_cast<temporary_construction_expression>(expr.sub_expr())
                                       && (is_expression_temporary(src_ptr)
                                           || (construction_destination && src_ptr == construction_destination));
            if (src_is_lvalue_ref && !src_is_prvalue_temp) {
                auto agg = st_type_nc->get_struct();
                if (!agg) {
                    if (auto un = find_union_by_struct_type(_unit.get_root_namespace(), st_type_nc)) {
                        auto* fn = _builder->GetInsertBlock()->getParent();
                        llvm::IRBuilder<> entry(&fn->getEntryBlock(), fn->getEntryBlock().begin());
                        auto* llvm_type = _context->get_llvm_type(st_type_nc);
                        auto* staging = entry.CreateAlloca(llvm_type, nullptr, "union_value_copy");
                        const bool source_const = sub_type->get_subtype()
                            && type::is_const(sub_type->get_subtype());
                        emit_union_copy(staging, src_ptr, *un, *un, false, source_const);
                        publish_expression_result(expr, value_result{
                            _builder->CreateLoad(llvm_type, staging, "union_copy_value")});
                        return;
                    }
                }
                if (agg && (expr.get_value_constructor() || agg->get_copy_constructor()
                            || scalar_union_wrapper(agg, _unit.get_root_namespace()))) {
                    // Call copy constructor: dest = new staging alloca, src = lvalue ptr
                    llvm::Type* llvm_st = _context->get_llvm_type(st_type_nc);
                    llvm::Function* cur_fn = _builder->GetInsertBlock()->getParent();
                    llvm::IRBuilder<> entry_bld(&cur_fn->getEntryBlock(),
                                                cur_fn->getEntryBlock().begin());
                    auto* staging = entry_bld.CreateAlloca(llvm_st, nullptr, "lval_copy");
                    if (auto ctor = expr.get_value_constructor()) {
                        auto it = _context->_functions.find(ctor->shared_as<k::model::function>());
                        if (it == _context->_functions.end()) {
                            throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F031),
                                expr.first_lexeme(), "LLVM declaration not found for selected copy constructor of type '{}'",
                                {st_type_nc->to_string()});
                        }
                        create_call_or_invoke(it->second->getFunctionType(), it->second, {staging, src_ptr});
                    } else {
                        emit_value_copy_or_move(staging, src_ptr, st_type_nc,
                            /*destroy_dest_first=*/false, expr.first_lexeme(), "by-value copy",
                            false, true, nullptr,
                            sub_type->get_subtype() && type::is_const(sub_type->get_subtype()));
                    }
                    // Load the struct value from staging so it can be passed by value.
                    // A call's argument cleanup scope owns successful copies until
                    // entry into the callee. A failed constructor never registers
                    // this staging storage as a live argument.
                    publish_expression_result(expr, value_result{
                        _builder->CreateLoad(llvm_st, staging, "lval_copy_load")});
                    return;
                }
            }
        }

        auto result = load_place(source, _context->get_llvm_type(load_type));
        // Value semantics: when a whole struct aggregate is loaded by value directly out
        // of a materialised prvalue temporary (e.g. `return Res();` or `consume(Res())`,
        // both lowered to a load of a temporary_construction_expression), the loaded
        // aggregate now carries the temporary's owned resources. Cancel the temporary's
        // scheduled destruction so an owning aggregate (e.g. Vector<T>) is MOVED into the
        // single destination (return slot / by-value parameter) instead of shallow-copied
        // and then freed twice.
        //
        // The move is restricted to a *directly* temporary-constructed sub-expression:
        // an lvalue (symbol, member access) keeps its own lifetime, and an intermediate
        // temporary that is the receiver ('this') of a chained call must still be
        // destroyed after the full expression (it is used by address, not consumed).
        if (std::dynamic_pointer_cast<temporary_construction_expression>(expr.sub_expr())
            && type::is_struct(load_type) && is_expression_temporary(src_ptr)) {
            cancel_temporary_cleanup(src_ptr);
        }
        publish_expression_result(expr, result);
    } else {
        // The unresolved fallback historically passed the operand through unchanged.
        publish_expression_result(expr, legacy_result{checked_legacy_result(operand)});
    }
}


//
// Dereference expression
//

void type_reference_resolver::visit_dereference_expression(dereference_expression& expr) {
    expr.sub_expr()->accept(*this);
    auto type = expr.sub_expr()->get_type();

    // Check if the operand is a polymorphic union (either ref<poly_union> or value poly_union)
    auto check_poly_union = [&](const std::shared_ptr<k::model::type>& t, bool is_const_ctx) -> bool {
        auto bare = type::remove_const(t);
        if (auto ref_t = std::dynamic_pointer_cast<reference_type>(bare)) {
            bare = type::remove_const(ref_t->get_subtype());
        }
        if (auto st = std::dynamic_pointer_cast<struct_type>(bare)) {
            if (!st->get_struct()) {
                auto root_ns = _unit.get_root_namespace();
                if (auto udef = find_union_by_struct_type(root_ns, st)) {
                    if (udef->is_polymorphic()) {
                        auto base_agg = udef->get_polymorphic_base();
                        if (base_agg && base_agg->get_struct_type()) {
                            auto base_st = base_agg->get_struct_type();
                            if (is_const_ctx || type::is_const(t) || type::is_const(bare)) {
                                expr.set_type(base_st->get_const()->get_reference());
                            } else {
                                expr.set_type(base_st->get_reference());
                            }
                            return true;
                        }
                    }
                }
            }
        }
        return false;
    };

    if (check_poly_union(type, type::is_const(type))) {
        return;
    }

    // Unwrap one level of reference if the referred-to type is an indirection
    if(auto ref_type = std::dynamic_pointer_cast<reference_type>(type)) {
        auto sub = ref_type->get_subtype();
        if(std::dynamic_pointer_cast<pointer_type>(sub) ||
           std::dynamic_pointer_cast<link_type>(sub) ||
           std::dynamic_pointer_cast<view_type>(sub) ||
           std::dynamic_pointer_cast<owner_type>(sub)) {
            type = sub;
        } else {
            throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_DEREF_VOID_POINTER), expr.first_lexeme(),
                "Cannot dereference a reference to a non-pointer type: "
                "the dereference operator ('*') requires pointer (*), link (+), view (?) or owner (!), "
                "but '{}' is not a pointer-like type",
                {sub ? sub->to_string() : "?"});
        }
    }

    if(auto ptr_type = std::dynamic_pointer_cast<pointer_type>(type)) {
        expr.set_type(ptr_type->get_subtype()->get_reference());
    } else if(auto lnk_type = std::dynamic_pointer_cast<link_type>(type)) {
        expr.set_type(lnk_type->get_linked_type()->get_reference());
    } else if(auto view_type_var = std::dynamic_pointer_cast<view_type>(type)) {
        expr.set_type(view_type_var->get_viewed_type()->get_reference());
    } else if(auto own_type = std::dynamic_pointer_cast<owner_type>(type)) {
        // Dereferencing an owner gives a reference to the owned object
        expr.set_type(own_type->get_owned_type()->get_reference());
    } else {
        throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_ADDRESS_OF_NOT_REF), expr.first_lexeme(),
            "Cannot dereference a non-pointer expression: "
            "the dereference operator ('*') requires a pointer (*), link (+), view (?), owner (!) or polymorphic union, "
            "but the operand has type '{}'",
            {type ? type->to_string() : "?"});
    }
}

void implementation_generator::visit_dereference_expression(dereference_expression& expr) {
    set_debug_location(expr.first_lexeme());
    auto operand = evaluate_expression(*expr.sub_expr());

    auto sub_type = expr.sub_expr()->get_type();

    // Check if sub_type is a polymorphic union
    auto bare_sub = type::remove_const(sub_type);
    if (auto ref_t = std::dynamic_pointer_cast<reference_type>(bare_sub)) {
        bare_sub = type::remove_const(ref_t->get_subtype());
    }
    if (auto st = std::dynamic_pointer_cast<struct_type>(bare_sub)) {
        if (!st->get_struct()) {
            auto root_ns = _unit.get_root_namespace();
            if (auto udef = find_union_by_struct_type(root_ns, st)) {
                if (udef->is_polymorphic()) {
                    auto base_agg = udef->get_polymorphic_base();
                    if (base_agg) {
                        auto* base = emit_polymorphic_union_to_base_ptr(
                            _builder.get(), _context.get(), *udef, base_agg.get(), checked_legacy_result(operand));
                        publish_expression_result(expr, place_result{base});
                        return;
                    }
                }
            }
        }
    }

    // If sub is ref<indirection>, load the stored address from the place.
    value_result address{nullptr};
    std::shared_ptr<k::model::type> inner_type;
    if(auto ref_type = std::dynamic_pointer_cast<reference_type>(sub_type)) {
        inner_type = ref_type->get_subtype();
        llvm::Type* llvm_inner = _context->get_llvm_type(inner_type);
        address = load_place(require_place(operand, *expr.sub_expr()), llvm_inner, "deref_load");
    } else {
        inner_type = sub_type;
        address = require_value(operand, *expr.sub_expr());
    }

    // For nullable indirections, emit a null-check before use
    if (std::dynamic_pointer_cast<pointer_type>(inner_type) ||
        std::dynamic_pointer_cast<view_type>(inner_type) ||
        std::dynamic_pointer_cast<owner_type>(inner_type)) {
        auto* fatal = get_or_declare_fatal_null_function("__k_fatal_null_dereference");
        emit_null_check(address.value, fatal, "deref");
    }
    publish_expression_result(expr, dereference_address(address));
}

//
// Member of object expression
//

} // namespace k::model::gen
