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
#include <unordered_set>
#include "../errors.hpp"
namespace k::model::gen {
// unary/binary base + address_of, drain, load_value, dereference

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
    _value = nullptr;
    expr.sub_expr()->accept(*this);

    if(!_value) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F023), expr.first_lexeme(),
            "Internal error: the sub-expression of an address-of ('&') operator produced no LLVM value; "
            "this indicates a code-generation bug");
    }
    // The value returned by the sub expression is the desired value
    // _value = _value;
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
}

void implementation_generator::visit_load_value_expression(load_value_expression& expr) {
    set_debug_location(expr.first_lexeme());
    _value = nullptr;
    expr.sub_expr()->accept(*this);
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
        llvm::Value* src_ptr = _value;

        // ── Lvalue struct copy (Site 3): if the source is a non-temporary lvalue
        //    of a non-trivially copyable struct type with a copy constructor, call
        //    the copy constructor into a staging alloca so the callee receives a
        //    proper copy (not a bitwise alias of the original object).
        auto st_type_nc = std::dynamic_pointer_cast<struct_type>(type::remove_const(load_type));
        if (st_type_nc) {
            auto sub_type = expr.sub_expr()->get_type();
            bool src_is_lvalue_ref = type::is_reference(sub_type);
            bool src_is_prvalue_temp = std::dynamic_pointer_cast<temporary_construction_expression>(expr.sub_expr())
                                       && is_expression_temporary(src_ptr);
            if (src_is_lvalue_ref && !src_is_prvalue_temp) {
                auto agg = st_type_nc->get_struct();
                if (agg && agg->get_copy_constructor()) {
                    // Call copy constructor: dest = new staging alloca, src = lvalue ptr
                    llvm::Type* llvm_st = _context->get_llvm_type(st_type_nc);
                    llvm::Function* cur_fn = _builder->GetInsertBlock()->getParent();
                    llvm::IRBuilder<> entry_bld(&cur_fn->getEntryBlock(),
                                                cur_fn->getEntryBlock().begin());
                    auto* staging = entry_bld.CreateAlloca(llvm_st, nullptr, "lval_copy");
                    emit_value_copy_or_move(staging, src_ptr, st_type_nc,
                        /*destroy_dest_first=*/false, expr.first_lexeme(), "by-value copy");
                    // Load the struct value from staging so it can be passed by value.
                    // Note: staging is intentionally NOT registered as an expression
                    // temporary — the callee takes sole ownership of the copy via its
                    // own stack slot and destructor call. A TODO remains to also emit a
                    // cleanup for staging in the caller for owner-holding structs.
                    _value = _builder->CreateLoad(llvm_st, staging, "lval_copy_load");
                    return;
                }
            }
        }

        _value = _builder->CreateLoad(_context->get_llvm_type(load_type), src_ptr);
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
    }
    // else: leave _value as the alloca ptr (should not happen in correct IR)
}


//
// Dereference expression
//

void type_reference_resolver::visit_dereference_expression(dereference_expression& expr) {
    expr.sub_expr()->accept(*this);
    auto type = expr.sub_expr()->get_type();

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
            "the dereference operator ('*') requires a pointer (*), link (+), view (?) or owner (!), "
            "but the operand has type '{}'",
            {type ? type->to_string() : "?"});
    }
}

void implementation_generator::visit_dereference_expression(dereference_expression& expr) {
    set_debug_location(expr.first_lexeme());
    _value = nullptr;
    expr.sub_expr()->accept(*this);

    auto sub_type = expr.sub_expr()->get_type();

    // If sub is ref<indirection>, load the stored address from the alloca
    std::shared_ptr<k::model::type> inner_type;
    if(auto ref_type = std::dynamic_pointer_cast<reference_type>(sub_type)) {
        inner_type = ref_type->get_subtype();
        llvm::Type* llvm_inner = _context->get_llvm_type(inner_type);
        _value = _builder->CreateLoad(llvm_inner, _value, "deref_load");
    } else {
        inner_type = sub_type;
    }

    // For nullable indirections, emit a null-check before use
    if (std::dynamic_pointer_cast<pointer_type>(inner_type) ||
        std::dynamic_pointer_cast<view_type>(inner_type) ||
        std::dynamic_pointer_cast<owner_type>(inner_type)) {
        auto* fatal = get_or_declare_fatal_null_function("__k_fatal_null_dereference");
        emit_null_check(_value, fatal, "deref");
    }
    // _value now holds the raw pointer — acts as a reference to the pointed object
}

//
// Member of object expression
//

} // namespace k::model::gen
