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

#include "../model/expressions.hpp"
#include "../model/statements.hpp"
#include "../model/operators.hpp"
#include "../model/mangler.hpp"
#include "../model/imported.hpp"
#include "../../../libkdi/src/kdi_aggregates.hpp"

#include "llvm/Support/raw_os_ostream.h"
#include <llvm/IR/DataLayout.h>

#include <unordered_set>

namespace k::model::gen {
// Forward declarations for class-related helpers defined in gen_class.cpp
void emit_vptr_store(llvm::IRBuilder<>& builder, klass& st, llvm::Value* this_ptr, std::shared_ptr<context> ctx);
llvm::Value* emit_virtual_dispatch_call(llvm::IRBuilder<>& builder, klass& st, llvm::Value* this_ptr,
    int slot_index, llvm::FunctionType* fn_type, const std::vector<llvm::Value*>& args,
    std::shared_ptr<context> ctx, const std::string& result_name);
} // k::model::gen
template<typename STM>
inline STM& operator << (STM& stm, const llvm::Type& type) {
    llvm::raw_os_ostream ross(stm);
    type.print(ross, true);
    return stm;
}

template<typename STM>
inline STM& operator << (STM& stm, const llvm::Value& value) {
    llvm::raw_os_ostream ross(stm);
    value.print(ross, true);
    return stm;
}

namespace k::model::gen {

//
// Value expression
//

void symbol_resolver::visit_value_expression(value_expression& expr)
{
}

void type_reference_resolver::visit_value_expression(value_expression& expr)
{
    auto type = _context->from_literal(expr.any_literal());
    expr.set_type(type);
}

llvm::Constant* implementation_generator::get_llvm_constant_from_value_expr(const value_expression& expr) const {
    if(expr.is_literal()) {
        return _context->get_llvm_constant_from_literal(expr.any_literal());
    } else {
        return _context->get_llvm_constant_from_value(expr.get_value());
    }
}

void implementation_generator::visit_value_expression(value_expression &expr) {
    _value = get_llvm_constant_from_value_expr(expr);
}

//
// Symbol expression:
//
// Symbol expression could be found in two cases:
// - In the symbol part (right hand) of a member-of expression, like b in "a.b".
//   In this case, the symbol is supposed to be already resolved when visiting the member-of expression.
//   It must not be resolved again here.
// - As a standalone symbol expression, like a or b in "a + b".
//   In this case, the symbol must be resolved here.
//   It could be only:
//   - a non-member variable: local variable, parameter, global variable.
//      - the returned value is the reference (address) of the variable.
//      - the return value type is always a reference to the variable type.
//   - a non-member function: function name used as a function pointer. It returns the reference of the function.
//      - the return value is always a function reference address.
//      - the return value type is always a function reference type.
//

void symbol_resolver::visit_symbol_expression(symbol_expression& symbol)
{
    auto found_symbol = resolve_symbol(symbol);
    if (std::holds_alternative<std::shared_ptr<variable_definition>>(found_symbol)) {
        auto var_def = std::get<std::shared_ptr<variable_definition>>(found_symbol);
        symbol.set_target(var_def);
        // Check visibility of member and global variables at the access site
        check_variable_visibility(*var_def, symbol);
    } else if (std::holds_alternative<std::shared_ptr<function>>(found_symbol)) {
        auto func =  std::get<std::shared_ptr<function>>(found_symbol);
        symbol.set_target(func);
    } else {
        // Symbol not found at this phase.
        // If this symbol is the callee of a function invocation, defer resolution to
        // type_reference_resolver which handles unified call syntax and member lookups.
        // Otherwise throw immediately, since there is nothing further that can resolve it.
        auto parent_expr = symbol.get_parent_expression();
        bool is_function_callee = false;
        if (auto parent_invoc = std::dynamic_pointer_cast<function_invocation_expression>(parent_expr)) {
            is_function_callee = (parent_invoc->callee_expr().get() == &symbol);
        }
        if (!is_function_callee) {
            throw_error(0x0003, std::nullopt,
                "Undefined symbol '{}': no variable, parameter or function with this name is visible in the current scope",
                {symbol.get_name().to_string()});
        }
        // else: leave unresolved; type_reference_resolver will report the error if still not found
    }
}

void type_reference_resolver::visit_symbol_expression(symbol_expression& symbol)
{
    if(!symbol.is_resolved()) {
        throw_internal_error(0x0001, std::nullopt,
            "Internal error: symbol '{}' reached type-resolution phase without being resolved; "
            "symbol resolution must be run before type resolution",
            {symbol.get_name().to_string()});
    }
    if (symbol.is_variable_def()) {
        auto var_def = symbol.get_variable_def();
        auto var_type = var_def->get_type();
        // If the variable is declared const (flag), propagate const-ness through the type system.
        // For primitive/struct types: wrap in const_type → "const int", "const MyStruct".
        // For indirection types (link, pointer, pinned): apply const to the pointed-at subtype
        // → "link(const int)", "pointer(const int)", "pinned(const int)".
        // References are immutable by design; const on a reference applies to its subtype too.
        // This ensures that "const x : int~" and "x : const int~" are truly identical.
        if (var_def->is_const() && !type::is_const(var_type)) {
            if (type::is_any_indirection(var_type)) {
                // Apply const to the subtype of the indirection.
                auto sub = var_type->get_subtype();
                auto const_sub = sub->get_const();
                // Reconstruct the same indirection kind with const subtype.
                if (type::is_link(var_type)) {
                    var_type = const_sub->get_link();
                } else if (type::is_pointer(var_type)) {
                    var_type = const_sub->get_pointer();
                } else if (type::is_pinned(var_type)) {
                    var_type = const_sub->get_pinned();
                } else { // reference
                    var_type = const_sub->get_reference();
                }
            } else {
                var_type = var_type->get_const();
            }
        }
        // Variable symbol will always be a reference to the variable type.
        if (type::is_reference(var_type)) {
            // Variable is already a reference, so symbol type is the variable type.
            symbol.set_type(var_type);
        } else {
            // Variable is not a reference, so symbol type is a reference to the variable type.
            symbol.set_type(var_type->get_reference());
        }
    } else if (symbol.is_function()) {
        // A symbol resolved to a function (without call parentheses) yields the address
        // of the function.  The type is a function_reference_type with ref_kind::link
        // (non-null, immutable — same semantics as a reference in K).
        // It can be freely assigned to a pointer (*), pin (^) or link (~) variable.
        auto func = symbol.get_function();
        if (func) {
            // Build the function_reference_type for this function.
            function_reference_type_builder builder(_context);
            builder.ref_kind(function_reference_type::ref_kind::link);
            // Return type
            auto ret_type = func->get_return_type();
            if (ret_type) builder.return_type(ret_type);
            // Parameter types
            for (size_t i = 0; i < func->get_parameter_size(); ++i) {
                auto p = func->get_parameter(i);
                if (p && p->get_type()) {
                    builder.append_parameter_type(p->get_type());
                }
            }
            // Owner struct (member function)
            auto owner_st = func->parent<aggregate>();
            if (owner_st && !func->is_static()) {
                auto owner_struct = std::dynamic_pointer_cast<structure>(owner_st);
                if (owner_struct) builder.member_of(owner_struct);
            }
            auto fn_ref_type = builder.build();
            // Keep fn_ref_type alive for the duration of type resolution.
            // Without this, fn_ref_type is a temporary local; the only other strong
            // reference is fn_ref_type->reference (the cached ref_type), which does NOT
            // own fn_ref_type.  When fn_ref_type expires, reference_type::subtype (a
            // weak_ptr) becomes dangling, causing is_resolved() to crash.
            _ephemeral_types.push_back(fn_ref_type);
            // The symbol gives a reference to the function ref type (non-null, like K's ~).
            // Wrap in a reference so that it can be assigned to any indirection kind.
            symbol.set_type(fn_ref_type->get_reference());
        }
    }
    // (symbol type resolution complete)
}

void implementation_generator::visit_symbol_expression(symbol_expression &symbol) {
    if(symbol.is_variable_def()) {
        auto var_def = symbol.get_variable_def();
        llvm::Value* ptr = nullptr;
        std::string name;

        // Handle source of symbol
        if (auto param = std::dynamic_pointer_cast<parameter>(var_def)) {
            ptr =  _context->_parameter_variables[param];
            name = param->get_short_name();
        } else if (auto global_var = std::dynamic_pointer_cast<global_variable_definition>(var_def)) {
            ptr = _context->_global_vars[global_var];
            name = global_var->get_short_name();
        } else if (auto local_var = std::dynamic_pointer_cast<variable_statement>(var_def)) {
            ptr = _context->_variables[local_var];
            name = local_var->get_short_name();
        } else if (auto member_var = std::dynamic_pointer_cast<member_variable_definition>(var_def)) {
            name = member_var->get_short_name();

            // Get 'this' pointer
            llvm::Value* this_value_ref = nullptr;
            auto func = std::dynamic_pointer_cast<function>(symbol.find_statement()->get_function());
            if(!func) {
                throw_internal_error(0x0001, std::nullopt,
                    "Internal error: cannot find enclosing function context for member variable '{}' access; "
                    "member variables can only be accessed from inside a method",
                    {member_var->get_fq_name()});
            }
            this_value_ref = _context->_function_this_variables[func];
            if (!this_value_ref) {
                throw_internal_error(0x0002, std::nullopt,
                    "Internal error: no 'this' pointer found in function '{}' for member variable '{}' access; "
                    "the function may be static or have no associated struct instance",
                    {func->get_fq_name(), member_var->get_fq_name()});
            }

            // Get member variable — potentially from an ancestor struct via __parent__ chain
            if(_struct_stack.empty()) {
                throw_internal_error(0x0003, std::nullopt,
                    "Internal error: no struct context on the code-generation stack when accessing member variable '{}'; "
                    "member access code generation must be performed inside a struct method",
                    {name});
            }

            // Determine if the member belongs to the current struct or an ancestor struct.
            // Build the chain of structs from the current (innermost) up to the owning struct.
            auto member_owner = member_var->parent<aggregate>();
            auto current_struct = _struct_stack.top();

            // Walk the __parent__ chain to reach the owning struct
            auto current_struct_type = current_struct->get_struct_type();
            llvm::Value* this_ptr = _builder->CreateLoad(
                    _context->get_llvm_type(current_struct_type->get_reference()),
                    this_value_ref,
                    "this_ref"
            );

            // Navigate __parent__ pointers until we reach the struct that owns member_var
            auto walk_struct = current_struct;
            while (walk_struct && walk_struct != member_owner) {
                if (!walk_struct->is_inner()) {
                    throw_internal_error(0x0004, std::nullopt,
                        "Internal error: could not reach owning struct '{}' for member '{}' via __parent__ chain; "
                        "the struct hierarchy is inconsistent",
                        {member_owner ? member_owner->get_short_name() : "?", name});
                }
                // GEP to __parent__ field (always index 0 for inner structs)
                auto walk_st_type = walk_struct->get_struct_type();
                this_ptr = _builder->CreateStructGEP(
                        _context->get_llvm_type(walk_st_type),
                        this_ptr,
                        0, // __parent__ is always field 0
                        "parent_field_ptr"
                );
                // Load the parent reference (stored as opaque pointer, same as 'this')
                auto outer_struct = walk_struct->get_enclosing_structure();
                auto outer_ref_type = outer_struct->get_struct_type()->get_reference();
                this_ptr = _builder->CreateLoad(
                        _context->get_llvm_type(outer_ref_type),
                        this_ptr,
                        "parent_ref"
                );
                walk_struct = outer_struct;
            }

            if (!walk_struct) {
                throw_internal_error(0x0005, std::nullopt,
                    "Internal error: could not find owning struct for member variable '{}' in __parent__ chain",
                    {name});
            }

            auto struct_type = walk_struct->get_struct_type();
            if(struct_type) {
                if(auto field = struct_type->get_member(name); field) {
                    ptr = _builder->CreateStructGEP(
                            _context->get_llvm_type(struct_type),
                            this_ptr,
                            (unsigned)field->index,
                            "this_" + walk_struct->get_short_name() + "_" + name + "_ptr"
                    );
                } else {
                    throw_internal_error(0x0004, std::nullopt,
                        "Internal error: struct '{}' has no member named '{}'; "
                        "the model is inconsistent — the member was not found during code generation",
                        {struct_type->name(), name});
                }
            } else { // TODO add here the method resolution
                throw_internal_error(0x0005, std::nullopt,
                    "Internal error: struct has no LLVM type information when accessing member '{}'; "
                    "the declaration pass must be run before the implementation pass",
                    {name});
            }

        } else {
            throw_internal_error(0x0006, std::nullopt,
                "Internal error: unsupported variable definition kind encountered while generating code for symbol '{}'; "
                "only parameters, global variables, local variables and member variables are supported",
                {var_def->get_fq_name()});
        }

        // Handle type of symbol
        auto var_type = var_def->get_type();
        llvm::Type* type = _context->get_llvm_type(var_type);

        if(ptr && type) {
            if (type::is_reference(var_type)) {
                // Type is a reference (pointer), so value is loaded from the pointer
                _value = _builder->CreateLoad(type, ptr, name + "_ref");
            } else {
                // Value of a symbol (as a reference) is always its address.
                // This includes function_reference_type variables: the caller that needs
                // the actual function pointer (e.g. indirect call) must load from this address.
                _value = ptr;
            }
        }

    } else if (symbol.is_function()) {
        auto func = symbol.get_function();

        // Find the function definition
        auto it = _context->_functions.find(func);
        if(it==_context->_functions.end()) {
            throw_internal_error(0x0007, std::nullopt,
                "Internal error: LLVM declaration not found for function '{}'; "
                "the declaration pass must be run before the implementation pass",
                {func ? func->get_fq_name() : "<null>"});
        }
        llvm::Function* llvm_func = it->second;
        if(!llvm_func) {
            throw_internal_error(0x0008, std::nullopt,
                "Internal error: LLVM function object is null for '{}'; "
                "this indicates a compiler bug in the declaration pass",
                {func ? func->get_fq_name() : "<null>"});
        }
        _value = llvm_func;
    }
    // TODO Support other types of symbols, not only variables and functions
}

//
// Unary expression
//

void symbol_resolver::visit_unary_expression(unary_expression& expr)
{
    auto& sub = expr.sub_expr();
    if(!sub) {
        throw_internal_error(0x0001, std::nullopt,
            "Internal error: unary expression has a null sub-expression; "
            "this indicates a malformed AST or a compiler bug");
    }
    sub->accept(*this);
}

void type_reference_resolver::visit_unary_expression(unary_expression& expr)
{
    auto& sub = expr.sub_expr();

    if(!sub) {
        throw_internal_error(0x0002, std::nullopt,
            "Internal error: unary expression has a null sub-expression; "
            "this indicates a malformed AST or a compiler bug");
    }

    sub->accept(*this);

    if(!type::is_resolved(sub->get_type())) {
        throw_internal_error(0x0003, std::nullopt,
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
        throw_internal_error(0x0002, std::nullopt,
            "Internal error: binary expression has a null left or right operand; "
            "this indicates a malformed AST or a compiler bug");
    }

    left->accept(*this);
    right->accept(*this);

}

void type_reference_resolver::visit_binary_expression(binary_expression& expr)
{
    auto& left = expr.left();
    auto& right = expr.right();

    if(!left || !right) {
        throw_internal_error(0x0004, std::nullopt,
            "Internal error: binary expression has a null left or right operand; "
            "this indicates a malformed AST or a compiler bug");
    }

    left->accept(*this);
    right->accept(*this);

    if(!type::is_resolved(left->get_type())) {
        throw_internal_error(0x0005, std::nullopt,
            "Internal error: the left operand of a binary operator could not be type-resolved; "
            "the type of each operand must be known before the binary expression can be typed");
    }
    if(!type::is_resolved(right->get_type())) {
        throw_internal_error(0x0006, std::nullopt,
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
        throw_error(0x0018, std::nullopt,
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
    _value = nullptr;
    expr.sub_expr()->accept(*this);

    if(!_value) {
        throw_internal_error(0x0009, std::nullopt,
            "Internal error: the sub-expression of an address-of ('&') operator produced no LLVM value; "
            "this indicates a code-generation bug");
    }
    // The value returned by the sub expression is the desired value
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
    } else {
        throw_error(0x0019, std::nullopt,
            "Cannot dereference a non-pointer/non-reference expression: "
            "load ('*') requires a reference or pointer operand, "
            "but the operand has type '{}'",
            {type ? type->to_string() : "?"});
    }
}

void implementation_generator::visit_load_value_expression(load_value_expression& expr) {
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
        }
    }
    if (load_type) {
        _value = _builder->CreateLoad(_context->get_llvm_type(load_type), _value);
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
           std::dynamic_pointer_cast<pinned_type>(sub)) {
            type = sub;
        } else {
            throw_error(0x001A, std::nullopt,
                "Cannot dereference a reference to a non-pointer type: "
                "the dereference operator ('*') requires pointer (*), link (~) or pinned (^), "
                "but '{}' is not a pointer-like type",
                {sub ? sub->to_string() : "?"});
        }
    }

    if(auto ptr_type = std::dynamic_pointer_cast<pointer_type>(type)) {
        expr.set_type(ptr_type->get_subtype()->get_reference());
    } else if(auto lnk_type = std::dynamic_pointer_cast<link_type>(type)) {
        expr.set_type(lnk_type->get_linked_type()->get_reference());
    } else if(auto pin_type = std::dynamic_pointer_cast<pinned_type>(type)) {
        expr.set_type(pin_type->get_pinned_type()->get_reference());
    } else {
        throw_error(0x001B, std::nullopt,
            "Cannot dereference a non-pointer expression: "
            "the dereference operator ('*') requires a pointer (*), link (~) or pinned (^), "
            "but the operand has type '{}'",
            {type ? type->to_string() : "?"});
    }
}

void implementation_generator::visit_dereference_expression(dereference_expression& expr) {
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
        std::dynamic_pointer_cast<pinned_type>(inner_type)) {
        auto* fatal = get_or_declare_fatal_null_function("__fatal_null_dereference");
        emit_null_check(_value, fatal, "deref");
    }
    // _value now holds the raw pointer — acts as a reference to the pointed object
}

//
// Member of object expression
//
void symbol_resolver::visit_member_of_expression(member_of_expression& expr) {
    // Explicitly only resolve sub expression.
    // Symbol can only be resolved afterward, cause it will depend on the type of subexpression.
    visit_unary_expression(expr);
}

void type_reference_resolver::visit_member_of_object_expression(member_of_object_expression& expr) {
    expr.sub_expr()->accept(*this);
    auto type = expr.sub_expr()->get_type();

    // Handle vbptr path: sub_expr was cast to pointer<VirtualBase> by the type resolver (this visit)
    // on a previous recursive call. Accept pointer type and resolve the member type.
    if (type::is_pointer(type)) {
        auto ptr_type = std::dynamic_pointer_cast<pointer_type>(type);
        auto bare_subtype = type::remove_const(ptr_type->get_pointed_type());
        if (auto struct_subtype = std::dynamic_pointer_cast<struct_type>(bare_subtype)) {
            const auto& member_name = expr.symbol();
            const std::string& name_str = member_name.get_name().to_string();
            if (auto field = struct_subtype->get_member(name_str)) {
                auto field_type = field->field_type.lock();
                expr.set_type(field_type->get_reference());
            }
            // For method: leave type unset, handled by function_invocation_expression
        }
        return;
    }

    if(!type::is_reference(type)) {
        throw_error(0x001C, std::nullopt,
            "Cannot access a member on a non-reference expression: "
            "the '.' operator requires the left-hand side to be a reference to a struct, "
            "but the left-hand side has type '{}'",
            {type ? type->to_string() : "?"});
    }
    auto subtype = type->get_subtype();
    // Detect if we are accessing through a const reference (ref<const S> or ref<S> where S is const struct)
    bool is_const_access = type::is_const(subtype);
    // Strip const to get the actual struct_type for member lookup
    auto bare_subtype = type::remove_const(subtype);

    if(auto struct_subtype = std::dynamic_pointer_cast<struct_type>(bare_subtype)) {
        const auto& member_name = expr.symbol();
        const std::string& name_str = member_name.get_name().to_string();

        // ── Helper: search a struct and its bases for a named field or function,
        //    returning (struct_type*, field) or (struct_type*, nullptr=function).
        //    Returns empty vector if not found, multiple items if ambiguous.
        //    'via_virtual' is true when the path to the member crossed at least one virtual link.
        struct MemberHit {
            std::shared_ptr<struct_type> in_struct_type;
            std::optional<struct_type::field> field;
            bool is_function = false;
            bool via_virtual = false; ///< true if the path to this member crosses a virtual base link
        };

        // Track visited structs to deduplicate diamond paths.
        // When traversing via a virtual base path, all transitively-visited structs
        // are deduplicated (even non-virtual ones), because the virtual base already
        // ensures a single shared copy.
        std::unordered_set<const aggregate*> visited_virtual_bases;

        std::function<std::vector<MemberHit>(const std::shared_ptr<struct_type>&, const std::string&, visibility, bool, bool)> search_member;
        search_member = [&](const std::shared_ptr<struct_type>& stype, const std::string& mname,
                             visibility inherit_vis, bool /*top_level*/, bool via_virt) -> std::vector<MemberHit> {
            std::vector<MemberHit> hits;
            // Check direct field
            if (auto field = stype->get_member(mname)) {
                hits.push_back({stype, field, false, via_virt});
                return hits;
            }
            // Check direct method
            if (auto st = stype->get_struct()) {
                if (st->get_function(mname)) {
                    hits.push_back({stype, std::nullopt, true, via_virt});
                    return hits;
                }
                // Search bases
                for (auto& bs : st->get_bases()) {
                    if (!bs.base || !bs.base->get_struct_type()) continue;
                    visibility eff_vis = (inherit_vis == PRIVATE || bs.vis == PRIVATE) ? PRIVATE :
                                         (inherit_vis == PROTECTED || bs.vis == PROTECTED) ? PROTECTED :
                                         PUBLIC;
                    bool next_via_virt = via_virt || bs.is_virtual;
                    // Deduplicate: if this base is virtual (explicitly), or if we're already
                    // traversing through a virtual-base path, track visited structs to avoid
                    // finding the same member multiple times (diamond disambiguation).
                    if (bs.is_virtual || via_virt) {
                        if (visited_virtual_bases.count(bs.base.get())) continue;
                        visited_virtual_bases.insert(bs.base.get());
                    }
                    auto sub_hits = search_member(bs.base->get_struct_type(), mname, eff_vis, false, next_via_virt);
                    hits.insert(hits.end(), sub_hits.begin(), sub_hits.end());
                }
            }
            return hits;
        };

        auto hits = search_member(struct_subtype, name_str, PUBLIC, true, false);
        if (hits.size() > 1) {
            std::cerr << "[DEBUG] Ambiguous: " << hits.size() << " hits for '" << name_str << "' in '" << struct_subtype->name() << "'\n" << std::flush;
        }

        if (hits.empty()) {
            // If this member_of_object_expression is the callee of a function_invocation_expression,
            // the name may be a free function callable via unified-call syntax (e.g. pt.sum() where
            // sum(p: point&) is a free function). Let visit_function_invocation_expression handle it.
            auto parent_expr = expr.get_parent_expression();
            bool is_function_callee = false;
            if (auto parent_invoc = std::dynamic_pointer_cast<function_invocation_expression>(parent_expr)) {
                is_function_callee = (parent_invoc->callee_expr().get() == &expr);
            }
            if (is_function_callee) return; // defer to function_invocation_expression
            throw_error(0x001D, std::nullopt,
                "No member named '{}' in struct '{}' or any of its bases",
                {name_str, struct_subtype->name()});
        }

        if (hits.size() > 1) {
            throw_error(0x0031, std::nullopt,
                "Ambiguous access to member '{}' in struct '{}': "
                "the member is found in multiple base classes; use Base::member to disambiguate",
                {name_str, struct_subtype->name()});
        }

        auto& hit = hits[0];

        // Check visibility of the accessed member
        if (auto st_model = hit.in_struct_type->get_struct()) {
            auto mv = st_model->get_variable(name_str);
            if (auto member_var = std::dynamic_pointer_cast<member_variable_definition>(mv)) {
                auto vis = member_var->get_visibility();
                if (vis != PUBLIC) {
                    if (!scope_lookup::is_struct_member_accessible(vis, *st_model, st_model, _function_stack)) {
                        throw_error(0x0030, std::nullopt,
                            "{} member variable '{}' of struct '{}' is not accessible here; "
                            "it can only be accessed from member functions of '{}'{}",
                            {vis == PROTECTED ? "protected" : "private",
                             member_var->get_short_name(), st_model->get_short_name(), st_model->get_short_name(),
                             vis == PROTECTED ? " or its subclasses" : ""});
                    }
                }
            }
        }

        if (!hit.is_function && hit.field.has_value()) {
            auto field_type = hit.field->field_type.lock();
            // If accessing through a const reference, the field is also const.
            if (is_const_access) {
                field_type = type::remove_const(field_type)->get_const();
            }
            expr.set_type(field_type->get_reference());
            // If the field is in a base, wrap sub_expr in a cast_expression to base ref
            // so that implementation_generator will compute the correct GEP offset.
            if (hit.in_struct_type != struct_subtype) {
                if (hit.via_virtual) {
                    // Virtual base access: find the direct path to the virtual base.
                    // The most-derived class has a __vbase_X__ embedded sub-object;
                    // an intermediate class has a __vbptr_X__ pointer field.
                    auto vbase_st = hit.in_struct_type->get_struct();
                    std::string vbase_short_name = vbase_st ? vbase_st->get_short_name() : "";
                    std::string vbase_field_name = "__vbase_" + vbase_short_name + "__";
                    std::string vbptr_field_name = "__vbptr_" + vbase_short_name + "__";

                    if (struct_subtype->get_member(vbase_field_name)) {
                        // Most-derived class: has the actual __vbase_X__ embedded sub-object.
                        // Upcast to that sub-object via GEP (no load needed).
                        auto base_ref_type = is_const_access
                            ? hit.in_struct_type->get_const()->get_reference()
                            : hit.in_struct_type->get_reference();
                        auto upcast = cast_expression::make_shared(expr.sub_expr(), base_ref_type);
                        upcast->set_type(base_ref_type);
                        expr.sub_expr() = upcast;
                    } else if (struct_subtype->get_member(vbptr_field_name)) {
                        // Intermediate class: has a __vbptr_X__ pointer.
                        // Use ref<A> (not A*) — the cast implementation will load the vbptr.
                        auto base_ref_type = is_const_access
                            ? hit.in_struct_type->get_const()->get_reference()
                            : hit.in_struct_type->get_reference();
                        auto vbptr_cast = cast_expression::make_shared(expr.sub_expr(), base_ref_type);
                        vbptr_cast->set_type(base_ref_type);
                        expr.sub_expr() = vbptr_cast;
                    } else {
                        // Fallback: the member is in a base X that is not directly a virtual base
                        // of struct_subtype, but is reachable through a virtual base path.
                        // Example: D->B(virtual)->A(non-virtual): x is in A, path is D.__vbase_B__.__base_A__.x
                        // Find one direct base of D that contains or leads to hit.in_struct_type.
                        bool found = false;
                        std::string base_A_name = "__base_" + vbase_short_name + "__";
                        for (auto& bs2 : struct_subtype->get_struct()->get_bases()) {
                            if (!bs2.base) continue;
                            auto base_sub = bs2.base->get_struct_type();
                            if (!base_sub) continue;
                            bool this_base_has_path = base_sub->get_member(vbase_field_name)
                                || base_sub->get_member(vbptr_field_name)
                                || base_sub->get_member(base_A_name);
                            if (!this_base_has_path) continue;
                            // Found a direct base of D that can reach hit.in_struct_type
                            // Build the cast: D → (via vbptr/vbase) bs2.base → (via __base_X__) hit.in_struct_type
                            auto inter_ref_type = is_const_access
                                ? base_sub->get_const()->get_reference()
                                : base_sub->get_reference();
                            auto inter_upcast = cast_expression::make_shared(expr.sub_expr(), inter_ref_type);
                            inter_upcast->set_type(inter_ref_type);
                            auto vbase_ref_type = is_const_access
                                ? hit.in_struct_type->get_const()->get_reference()
                                : hit.in_struct_type->get_reference();
                            auto vbase_upcast = cast_expression::make_shared(inter_upcast, vbase_ref_type);
                            vbase_upcast->set_type(vbase_ref_type);
                            expr.sub_expr() = vbase_upcast;
                            found = true;
                            break;
                        }
                        if (!found) {
                            // Simple fallback: treat as regular cast
                            auto base_ref_type = is_const_access
                                ? hit.in_struct_type->get_const()->get_reference()
                                : hit.in_struct_type->get_reference();
                            auto upcast = cast_expression::make_shared(expr.sub_expr(), base_ref_type);
                            upcast->set_type(base_ref_type);
                            expr.sub_expr() = upcast;
                        }
                    }
                } else {
                    // Non-virtual base: normal GEP-based upcast
                    auto base_ref_type = is_const_access
                        ? hit.in_struct_type->get_const()->get_reference()
                        : hit.in_struct_type->get_reference();
                    auto upcast = cast_expression::make_shared(expr.sub_expr(), base_ref_type);
                    upcast->set_type(base_ref_type);
                    expr.sub_expr() = upcast;
                }
            }
        } else if (hit.is_function) {
            // Member function: update the sub_expr type to point to the struct that owns the method.
            // This is needed so that implementation_generator finds the method in the correct struct,
            // and so that the 'this' pointer is correctly adjusted via upcast if needed.
            if (hit.in_struct_type != struct_subtype) {
                if (hit.via_virtual) {
                    // Virtual base: function is in a virtual base. Same path-finding as field access.
                    auto vbase_st = hit.in_struct_type->get_struct();
                    std::string vbase_short_name = vbase_st ? vbase_st->get_short_name() : "";
                    std::string vbase_field_name = "__vbase_" + vbase_short_name + "__";
                    std::string vbptr_field_name = "__vbptr_" + vbase_short_name + "__";

                    if (struct_subtype->get_member(vbase_field_name)) {
                        auto base_ref_type = is_const_access
                            ? hit.in_struct_type->get_const()->get_reference()
                            : hit.in_struct_type->get_reference();
                        auto upcast = cast_expression::make_shared(expr.sub_expr(), base_ref_type);
                        upcast->set_type(base_ref_type);
                        expr.sub_expr() = upcast;
                    } else if (struct_subtype->get_member(vbptr_field_name)) {
                        // Intermediate class has __vbptr_X__: load vbptr.
                        // Use ref<A> type (not A*) so function invocations accept it as reference.
                        auto base_ref_type = is_const_access
                            ? hit.in_struct_type->get_const()->get_reference()
                            : hit.in_struct_type->get_reference();
                        auto vbptr_cast = cast_expression::make_shared(expr.sub_expr(), base_ref_type);
                        vbptr_cast->set_type(base_ref_type);
                        expr.sub_expr() = vbptr_cast;
                    } else {
                        // Fallback: member function in a transitively reachable base
                        // (e.g., D->B(virtual)->A(non-virtual): method is in A)
                        std::string base_A_name2 = "__base_" + vbase_short_name + "__";
                        bool found2 = false;
                        for (auto& bs : struct_subtype->get_struct()->get_bases()) {
                            if (!bs.base) continue;
                            auto base_sub = bs.base->get_struct_type();
                            if (!base_sub) continue;
                            if (base_sub->get_member(vbase_field_name) || base_sub->get_member(vbptr_field_name) || base_sub->get_member(base_A_name2)) {
                                auto inter_ref_type = is_const_access
                                    ? base_sub->get_const()->get_reference()
                                    : base_sub->get_reference();
                                auto inter_upcast = cast_expression::make_shared(expr.sub_expr(), inter_ref_type);
                                inter_upcast->set_type(inter_ref_type);
                                auto vbase_ref_type = is_const_access
                                    ? hit.in_struct_type->get_const()->get_reference()
                                    : hit.in_struct_type->get_reference();
                                auto vbase_upcast = cast_expression::make_shared(inter_upcast, vbase_ref_type);
                                vbase_upcast->set_type(vbase_ref_type);
                                expr.sub_expr() = vbase_upcast;
                                found2 = true;
                                break;
                            }
                        }
                        if (!found2) {
                            // Simple fallback
                            auto base_ref_type = is_const_access
                                ? hit.in_struct_type->get_const()->get_reference()
                                : hit.in_struct_type->get_reference();
                            auto upcast = cast_expression::make_shared(expr.sub_expr(), base_ref_type);
                            upcast->set_type(base_ref_type);
                            expr.sub_expr() = upcast;
                        }
                    }
                } else {
                    auto base_ref_type = is_const_access
                        ? hit.in_struct_type->get_const()->get_reference()
                        : hit.in_struct_type->get_reference();
                    auto upcast = cast_expression::make_shared(expr.sub_expr(), base_ref_type);
                    upcast->set_type(base_ref_type);
                    expr.sub_expr() = upcast;
                }
            }
            // Type of member_of_object_expression for functions is the struct ref (for 'this')
            // — leave expr type unset; function_invocation_expression will handle it.
        }
    } else {
        throw_error(0x001E, std::nullopt,
            "The '.' operator can only be applied to a reference to a struct type, "
            "but the left-hand side is a reference to '{}' which is not a struct",
            {bare_subtype ? bare_subtype->to_string() : "?"});
    }
}

//
// Member of object expression
// If the member is a field, return the address of the field within the struct.
// If the member is a function, return the address of the object onto which the function will be called (the future this ref).
//
void implementation_generator::visit_member_of_object_expression(member_of_object_expression& expr) {
    _value = nullptr;
    expr.sub_expr()->accept(*this);

    auto type = expr.sub_expr()->get_type(); // Is a reference or (for vbptr path) a pointer

    // Handle vbptr path: sub_expr was cast to pointer<VirtualBase> by the type resolver
    if (type::is_pointer(type)) {
        auto ptr_type = std::dynamic_pointer_cast<pointer_type>(type);
        auto bare_subtype = type::remove_const(ptr_type->get_pointed_type());
        if (auto struct_subtype = std::dynamic_pointer_cast<struct_type>(bare_subtype)) {
            const auto& member_name = expr.symbol();
            if (auto field = struct_subtype->get_member(member_name.get_name()); field) {
                _value = _builder->CreateStructGEP(bare_subtype->get_llvm_type(), _value, field->index);
            }
            // For method calls: leave _value as the A* pointer (treated as A ref at LLVM level)
        }
        return;
    }

    // Strip const from the subtype to get the bare struct_type for GEP/method lookup.
    auto bare_subtype = type::remove_const(type->get_subtype());
    if(auto struct_subtype = std::dynamic_pointer_cast<struct_type>(bare_subtype)) {
        const auto& member_name =  expr.symbol();
        if(auto field = struct_subtype->get_member(member_name.get_name()); field) {
            _value = _builder->CreateStructGEP(bare_subtype->get_llvm_type(), _value, field->index);
        } else if(auto method = struct_subtype->get_struct()->get_function(member_name.get_name())) {
            // Note return the already-assigned address of the struct onto which the function is applied to
        } else {
            throw_internal_error(0x000A, std::nullopt,
                "Internal error: struct '{}' has no member named '{}' during code generation; "
                "the model is inconsistent — type resolution should have caught this earlier",
                {struct_subtype->name(), member_name.get_name().to_string()});
        }
    } else {
        throw_internal_error(0x000B, std::nullopt,
            "Internal error: the '.' operator is applied to a non-struct type during code generation; "
            "the operand type is '{}' — type resolution should have caught this earlier",
            {type && type->get_subtype() ? type->get_subtype()->to_string() : "?"});
    }
}

//
// Member of pointer expression (->)
// Acts as (*expr).member. Supported LHS: pointer (*), link (~), pinned (^).
//
void type_reference_resolver::visit_member_of_pointer_expression(member_of_pointer_expression& expr) {
    expr.sub_expr()->accept(*this);
    auto type = expr.sub_expr()->get_type();

    // Unwrap ref-to-indirection
    if (auto ref_type = std::dynamic_pointer_cast<reference_type>(type)) {
        type = ref_type->get_subtype();
    }

    std::shared_ptr<k::model::type> pointed_type;
    if (auto ptr_t = std::dynamic_pointer_cast<pointer_type>(type)) {
        pointed_type = ptr_t->get_pointed_type();
    } else if (auto lnk_t = std::dynamic_pointer_cast<link_type>(type)) {
        pointed_type = lnk_t->get_linked_type();
    } else if (auto pin_t = std::dynamic_pointer_cast<pinned_type>(type)) {
        pointed_type = pin_t->get_pinned_type();
    } else {
        throw_error(0x0080, std::nullopt,
            "The '->' operator requires a pointer (*), link (~) or pinned (^) on the LHS, "
            "but got '{}'", {type ? type->to_string() : "?"});
    }

    auto struct_subtype = std::dynamic_pointer_cast<struct_type>(pointed_type);
    if (!struct_subtype) {
        throw_error(0x0081, std::nullopt,
            "The '->' operator requires a pointer to a struct, "
            "but the pointed-to type is '{}'",
            {pointed_type ? pointed_type->to_string() : "?"});
    }

    const auto& member_name = expr.symbol();
    const std::string& name_str = member_name.get_name().to_string();
    if (auto field = struct_subtype->get_member(name_str)) {
        // Check visibility of the accessed field
        if (auto st_model = struct_subtype->get_struct()) {
            if (auto mv = std::dynamic_pointer_cast<member_variable_definition>(st_model->get_variable(name_str))) {
                auto vis = mv->get_visibility();
                if (vis != PUBLIC) {
                    if (!scope_lookup::is_struct_member_accessible(vis, *st_model, st_model, _function_stack)) {
                        throw_error(0x0083, std::nullopt,
                            "{} member variable '{}' of struct '{}' is not accessible here via '->'; "
                            "it can only be accessed from member functions of '{}'{}",
                            {vis == PROTECTED ? "protected" : "private",
                             mv->get_short_name(), st_model->get_short_name(), st_model->get_short_name(),
                             vis == PROTECTED ? " or its subclasses" : ""});
                    }
                }
            }
        }
        auto field_type = field->field_type.lock();
        expr.set_type(field_type ? field_type->get_reference() : nullptr);
    } else if (struct_subtype->get_struct() && struct_subtype->get_struct()->get_function(name_str)) {
        // Check visibility of the accessed method
        if (auto st_model = struct_subtype->get_struct()) {
            if (auto fn = st_model->get_function(name_str)) {
                auto vis = fn->get_visibility();
                if (vis != PUBLIC) {
                    if (!scope_lookup::is_struct_member_accessible(vis, *st_model, st_model, _function_stack)) {
                        throw_error(0x0084, std::nullopt,
                            "{} member function '{}' of struct '{}' is not accessible here via '->'; "
                            "it can only be called from member functions of '{}'{}",
                            {vis == PROTECTED ? "protected" : "private",
                             fn->get_short_name(), st_model->get_short_name(), st_model->get_short_name(),
                             vis == PROTECTED ? " or its subclasses" : ""});
                    }
                }
            }
        }
        expr.set_type(pointed_type->get_reference());
    } else {
        throw_error(0x0082, std::nullopt,
            "Struct '{}' has no member named '{}'",
            {struct_subtype->name(), name_str});
    }
}

void implementation_generator::visit_member_of_pointer_expression(member_of_pointer_expression& expr) {
    _value = nullptr;
    expr.sub_expr()->accept(*this);

    auto sub_type = expr.sub_expr()->get_type();
    std::shared_ptr<k::model::type> inner_type;
    if (auto ref_type = std::dynamic_pointer_cast<reference_type>(sub_type)) {
        inner_type = ref_type->get_subtype();
        _value = _builder->CreateLoad(_context->get_llvm_type(inner_type), _value, "arrow_load");
    } else {
        inner_type = sub_type;
    }

    // Null-check for nullable indirections
    if (std::dynamic_pointer_cast<pointer_type>(inner_type) ||
        std::dynamic_pointer_cast<pinned_type>(inner_type)) {
        auto* fatal = get_or_declare_fatal_null_function("__fatal_null_dereference");
        emit_null_check(_value, fatal, "arrow");
    }

    std::shared_ptr<k::model::type> pointed_type;
    if (auto ptr_t = std::dynamic_pointer_cast<pointer_type>(inner_type)) pointed_type = ptr_t->get_pointed_type();
    else if (auto lnk_t = std::dynamic_pointer_cast<link_type>(inner_type)) pointed_type = lnk_t->get_linked_type();
    else if (auto pin_t = std::dynamic_pointer_cast<pinned_type>(inner_type)) pointed_type = pin_t->get_pinned_type();
    if (!pointed_type) return;

    auto struct_subtype = std::dynamic_pointer_cast<struct_type>(pointed_type);
    if (!struct_subtype) return;
    const auto& member_name = expr.symbol();
    if (auto field = struct_subtype->get_member(member_name.get_name())) {
        _value = _builder->CreateStructGEP(
            _context->get_llvm_type(pointed_type), _value,
            (unsigned)field->index, member_name.get_name().to_string() + "_ptr");
    }
    // For method: _value is already the struct ptr (this)
}

//
// PM expression (.* and ->*)
//

void type_reference_resolver::visit_pm_expression(pm_expression& expr) {
    // Resolve both sub-expressions
    expr.left()->accept(*this);
    expr.right()->accept(*this);

    // ── LHS: get the struct type ──────────────────────────────────────────────
    auto obj_type = expr.left()->get_type();

    // Unwrap ref if needed
    if (auto ref = std::dynamic_pointer_cast<reference_type>(obj_type)) {
        obj_type = ref->get_subtype();
    }

    // For ->*, unwrap the pointer/link/pin layer
    if (expr.is_arrow()) {
        if (auto ptr = std::dynamic_pointer_cast<pointer_type>(obj_type)) {
            obj_type = ptr->get_pointed_type();
        } else if (auto lnk = std::dynamic_pointer_cast<link_type>(obj_type)) {
            obj_type = lnk->get_linked_type();
        } else if (auto pin = std::dynamic_pointer_cast<pinned_type>(obj_type)) {
            obj_type = pin->get_pinned_type();
        } else {
            throw_error(0x0090, std::nullopt,
                "The '->*' operator requires a pointer (*), link (~) or pinned (^) on the LHS, "
                "but got '{}'", {obj_type ? obj_type->to_string() : "?"});
        }
    }

    auto struct_t = std::dynamic_pointer_cast<struct_type>(obj_type);
    if (!struct_t) {
        throw_error(0x0091, std::nullopt,
            "The '{}' operator requires a struct on the LHS, but got '{}'",
            {expr.is_arrow() ? "->*" : ".*", obj_type ? obj_type->to_string() : "?"});
    }

    // ── RHS: must be a member_function_reference_type ────────────────────────
    auto mfp_type = expr.right()->get_type();
    // Unwrap ref wrapper if present
    if (auto ref = std::dynamic_pointer_cast<reference_type>(mfp_type)) {
        mfp_type = ref->get_subtype();
    }
    auto mfrt = std::dynamic_pointer_cast<member_function_reference_type>(mfp_type);
    if (!mfrt) {
        // Also accept plain function_reference_type (for free-function pointers used in pm context)
        if (!std::dynamic_pointer_cast<function_reference_type>(mfp_type)) {
            throw_error(0x0092, std::nullopt,
                "The '{}' operator requires a member function reference type on the RHS, "
                "but got '{}'",
                {expr.is_arrow() ? "->*" : ".*", mfp_type ? mfp_type->to_string() : "?"});
        }
    }

    // ── Result type: return type of the member function reference ────────────
    auto frt = std::dynamic_pointer_cast<function_reference_type>(mfp_type);
    expr.set_type(frt ? frt->get_return_type() : nullptr);
}

void implementation_generator::visit_pm_expression(pm_expression& expr) {
    // pm_expression is used as the callee of a function_invocation_expression.
    // The invocation handler calls this to get the (this_ptr, fn_ptr) pair it needs.
    // Here we just produce the function pointer value; the object pointer is obtained
    // separately by the caller via expr.left().
    //
    // However, if visit_pm_expression is reached standalone (e.g. result unused),
    // produce nullptr.
    _value = nullptr;

    // Evaluate the RHS (member function pointer variable)
    expr.right()->accept(*this);
    auto fn_alloca = _value;
    if (!fn_alloca) return;

    // Load the actual function pointer from the variable alloca
    auto mfp_type = expr.right()->get_type();
    if (auto ref = std::dynamic_pointer_cast<reference_type>(mfp_type)) {
        mfp_type = ref->get_subtype();
    }
    if (auto frt = std::dynamic_pointer_cast<function_reference_type>(mfp_type)) {
        auto* llvm_fn_type = frt->get_llvm_type();
        if (llvm_fn_type) {
            _value = _builder->CreateLoad(llvm_fn_type, fn_alloca, "mfp_load");
        }
    }
}

//
// Subscript expression
//

void type_reference_resolver::visit_subscript_expression(subscript_expression& expr) {
    visit_binary_expression(expr);

    auto left = expr.left();
    auto right = expr.right();

    auto left_type = left->get_type();

//  TODO dereference for double references

    // Dereference if needed
    if(!type::is_reference(left_type)) {
        throw_error(0x001F, std::nullopt,
            "Subscript operator '[]' requires a reference to an array as left operand, "
            "but the left operand has type '{}' which is not a reference",
            {left_type ? left_type->to_string() : "?"});
    }
    if(type::is_double_reference(left_type)) {
        // Deref first ref
        left_type = left_type->get_subtype();
    }
    left_type = std::dynamic_pointer_cast<reference_type>(left_type)->get_subtype();

    if(!type::is_array(left_type)) {
        throw_error(0x0020, std::nullopt,
            "Subscript operator '[]' can only be applied to an array type, "
            "but the dereferenced left operand has type '{}' which is not an array",
            {left_type ? left_type->to_string() : "?"});
    }
    auto arr_type = std::dynamic_pointer_cast<array_type>(left_type);
    expr.set_type(arr_type->get_subtype()->get_reference());

    // Check the right hand can be cast to unsigned integer
    // TODO adapt to the really right index type.
    // TODO is array really indexed by uint ?
    auto adapted_right = adapt_type(right, _context->from_type(primitive_type::UNSIGNED_INT));
    if(!adapted_right) {
        throw_error(0x0021, std::nullopt,
            "Subscript index expression cannot be implicitly converted to an unsigned integer index type; "
            "the index operand has type '{}' — use an explicit cast if needed",
            {right->get_type() ? right->get_type()->to_string() : "?"});
    } else if(adapted_right!=right) {
        right = adapted_right;
        expr.assign_right(right);
    }
}

void implementation_generator::visit_subscript_expression(subscript_expression& expr) {
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        _value = nullptr;
        return;
    }

    auto left_type = expr.left()->get_type();

    // Dereference if double ref (ref<ref<array>>)
    if(type::is_double_reference(left_type)) {
        left_type = left_type->get_subtype();
        left = _builder->CreateLoad(_context->get_llvm_type(left_type), left, "arr_ref");
    }

    // At this point left_type is ref<array<T>> or ref<sized_array<T>>.
    // left is the pointer to the { i32, [N x T] } struct.
    auto arr_type_inner = left_type->get_subtype(); // sized_array_type or array_type

    // Dereference index if needed
    auto right_type = expr.right()->get_type();
    if(type::is_reference(right_type)) {
        right_type = std::dynamic_pointer_cast<reference_type>(right_type)->get_subtype();
        right = _builder->CreateLoad(_context->get_llvm_type(right_type), right, "idx");
    }

    if (auto sized_arr = std::dynamic_pointer_cast<sized_array_type>(arr_type_inner)) {
        // Layout: { i32, [N x T] }
        // GEP: struct_ptr -> field 1 (data array) -> element index
        auto* struct_llvm = sized_arr->get_llvm_struct_type();
        auto* data_arr_llvm = sized_arr->get_llvm_data_array_type();
        if (!struct_llvm || !data_arr_llvm) {
            throw_internal_error(0x000C, std::nullopt,
                "Internal error: sized array has no LLVM struct type during subscript code generation");
        }
        // Two-step GEP: first into the struct field 1, then into the array element
        llvm::Value* field_data_ptr = _builder->CreateStructGEP(struct_llvm, left,
            sized_array_type::FIELD_DATA, "arr_data_ptr");
        llvm::Value* indices[] = {_builder->getInt32(0), right};
        _value = _builder->CreateGEP(data_arr_llvm, field_data_ptr, indices, "elem_ptr");
    } else {
        // Unsized array ref (int[]) — ptr to opaque struct; use i8* arithmetic for now
        // Fall back to generic GEP via element type
        auto elem_type = arr_type_inner->get_subtype();
        auto* elem_llvm = _context->get_llvm_type(elem_type);
        llvm::Value* indices[] = {right};
        _value = _builder->CreateGEP(elem_llvm, left, indices, "elem_ptr");
    }
}

//
// Function invocation expression
//

void symbol_resolver::visit_function_invocation_expression(function_invocation_expression &expr) {
    expr.callee_expr()->accept(*this);
    for (auto arg : expr.arguments()) {
        arg->accept(*this);
    }
    // TODO Add more pre process here ?!?
}

namespace {
/**
 * Phase-3 helper: compute and store a virtual_dispatch_info annotation on a
 * function_invocation_expression after the callee and its 'this' type have been resolved.
 *
 * @param expr          The call expression being annotated.
 * @param func          The resolved function (callee).
 * @param member_callee Non-null when the call is of the form obj.method(...).
 *
 * Rules:
 *  - Qualified call (expr.is_non_virtual_qualified_call()) → DIRECT
 *  - Non-member call / no receiver              → DIRECT
 *  - Function not virtual                       → DIRECT
 *  - Virtual function through a class reference → VTABLE
 *    The dispatch_class is the *static* receiver type (the base class as written
 *    in the source).  The slot_index comes from the vtable layout.
 *    If the receiver is a secondary-base reference (embedded at non-zero offset),
 *    this_adjustment is set from secondary_vtable_spec::base_offset so the generator
 *    can use it if needed (Phase 4).
 */
void annotate_dispatch_info(function_invocation_expression& expr,
                            const std::shared_ptr<function>& func,
                            const std::shared_ptr<member_of_object_expression>& member_callee)
{
    // ── DIRECT cases ─────────────────────────────────────────────────────────
    if (expr.is_non_virtual_qualified_call() || !member_callee || !func) {
        virtual_dispatch_info di;
        di.kind = virtual_dispatch_info::dispatch_kind::DIRECT;
        expr.set_dispatch_info(std::move(di));
        return;
    }

    if (!func->is_virtual() || func->get_vtable_slot() < 0) {
        virtual_dispatch_info di;
        di.kind = virtual_dispatch_info::dispatch_kind::DIRECT;
        expr.set_dispatch_info(std::move(di));
        return;
    }

    // ── Determine the static receiver type ───────────────────────────────────
    auto this_type = member_callee->sub_expr()->get_type();
    if (!type::is_reference(this_type)) {
        virtual_dispatch_info di;
        di.kind = virtual_dispatch_info::dispatch_kind::DIRECT;
        expr.set_dispatch_info(std::move(di));
        return;
    }

    auto bare_subtype = type::remove_const(this_type->get_subtype());
    auto st_type = std::dynamic_pointer_cast<struct_type>(bare_subtype);
    if (!st_type) {
        virtual_dispatch_info di;
        di.kind = virtual_dispatch_info::dispatch_kind::DIRECT;
        expr.set_dispatch_info(std::move(di));
        return;
    }

    auto kl = std::dynamic_pointer_cast<klass>(st_type->get_struct());
    if (!kl || !kl->has_vtable()) {
        // Check if it is an imported aggregate with a vtable
        // (imported_klass / imported_interface — neither derives from klass).
        auto imp = std::dynamic_pointer_cast<aggregate>(st_type->get_struct());
        if (imp && imp->has_vtable()) {
            virtual_dispatch_info di;
            di.kind                = virtual_dispatch_info::dispatch_kind::VTABLE;
            di.slot_index          = func->get_vtable_slot();
            di.imported_dispatch_agg = imp;
            di.this_adjustment     = 0;
            expr.set_dispatch_info(std::move(di));
            return;
        }
        virtual_dispatch_info di;
        di.kind = virtual_dispatch_info::dispatch_kind::DIRECT;
        expr.set_dispatch_info(std::move(di));
        return;
    }

    // ── Build VTABLE annotation ───────────────────────────────────────────────
    virtual_dispatch_info di;
    di.kind           = virtual_dispatch_info::dispatch_kind::VTABLE;
    di.slot_index     = func->get_vtable_slot();
    di.dispatch_class = kl;
    di.this_adjustment = 0;

    // Check if this receiver class is a secondary base somewhere (for information;
    // the generator may use this in Phase 4 to apply this-adjustment before vptr load).
    // We look for kl in the secondary_vtable_specs of its owner classes.
    // For now, for static dispatch we just store 0: the vptr in the object already
    // points to the right secondary vtable (set up by the constructor via emit_vptr_store).
    // The slot_index here is the index within kl's own vtable.

    expr.set_dispatch_info(std::move(di));
}
} // anonymous namespace

void type_reference_resolver::visit_function_invocation_expression(function_invocation_expression &expr) {
    auto callee = std::dynamic_pointer_cast<symbol_expression>(expr.callee_expr());
    auto member_callee = std::dynamic_pointer_cast<member_of_object_expression>(expr.callee_expr());
    auto pm_callee = std::dynamic_pointer_cast<pm_expression>(expr.callee_expr());

    if(!callee && !member_callee && !pm_callee) {
        throw_error(0x0022, std::nullopt,
            "Unsupported call expression form: only direct function calls ('func(args)'), "
            "member function calls ('obj.method(args)') and pointer-to-member calls "
            "('obj.*mfp(args)') are supported");
    }

    // Resolve and type-check all arguments first
    for(auto& arg : expr.arguments()) {
        arg->accept(*this);
    }


    // ----------------------------------------------------------------
    // Case 0 : pointer-to-member call  "obj.*mfp(args)" or "ptr->*mfp(args)"
    // ----------------------------------------------------------------
    if (pm_callee) {
        // Visit the pm_expression to resolve types of both LHS and RHS
        pm_callee->accept(*this);

        // Retrieve the member function reference type from the RHS
        auto mfp_type = pm_callee->right()->get_type();
        if (auto ref = std::dynamic_pointer_cast<reference_type>(mfp_type)) {
            mfp_type = ref->get_subtype();
        }
        auto frt = std::dynamic_pointer_cast<function_reference_type>(mfp_type);
        if (!frt) {
            throw_error(0x0093, std::nullopt,
                "The '{}' call requires a member function reference type, but got '{}'",
                {pm_callee->is_arrow() ? "->*" : ".*", mfp_type ? mfp_type->to_string() : "?"});
        }

        // Set return type of the invocation expression
        // If the frt has no return type (e.g. it's a parameter with inferred type),
        // propagate from the enclosing function's return type.
        auto ret_type = frt->get_return_type();
        if (!ret_type && !_function_stack.empty()) {
            ret_type = _function_stack.back()->get_return_type();
            if (ret_type) {
                // Cache on the frt so later uses (e.g. in impl_gen) see it
                frt->set_return_type(ret_type);
            }
        }
        expr.set_type(ret_type);

        // Adapt arguments against the frt's parameter types.
        // NOTE: for member_function_reference_type, get_parameter_types() returns ONLY the
        // explicit parameters — the implicit 'this' pointer is NOT in _parameter_types
        // (it appears only in the LLVM FunctionType built by function_reference_type_builder).
        // Therefore param_offset is always 0 here.
        const auto& frt_params = frt->get_parameter_types();
        const auto& call_args = expr.arguments();
        for (size_t i = 0; i < call_args.size() && i < frt_params.size(); ++i) {
            auto adapted = adapt_type(call_args[i], frt_params[i]);
            if (adapted && adapted != call_args[i]) {
                expr.assign_argument(i, adapted);
            }
        }

        // Annotate dispatch info as INDIRECT_MEMBER
        virtual_dispatch_info di;
        di.kind = virtual_dispatch_info::dispatch_kind::INDIRECT_MEMBER;
        expr.set_dispatch_info(std::move(di));
        return;
    }

    // ----------------------------------------------------------------
    // Case 1 : member-of-object call  "obj.method(args)"
    // ----------------------------------------------------------------
    if (member_callee) {
        // Visit the full member_of_object_expression so that upcast injection for inherited
        // methods is triggered (visit_member_of_object_expression injects a cast_expression
        // into sub_expr when the method belongs to a base struct).
        member_callee->accept(*this);

        callee = std::dynamic_pointer_cast<symbol_expression>(
                member_callee->symbol().shared_as<symbol_expression>());
        if (!callee) {
            throw_error(0x0023, std::nullopt,
                "Unsupported member call form: the right-hand side of '.' must be a simple name, "
                "not a complex expression");
        }

        // sub_expr of member_callee gives the object reference (possibly upcast)
        auto this_expr = member_callee->sub_expr();
        auto this_type = this_expr->get_type(); // should be ref<struct> (possibly base)

        if (!type::is_reference(this_type)) {
            throw_error(0x0024, std::nullopt,
                "The '.' operator requires the left-hand side to have a reference type, "
                "but '{}' is not a reference; did you mean to use a reference parameter?",
                {this_type ? this_type->to_string() : "?"});
        }
        auto subtype = type::is_reference(this_type) ? this_type->get_subtype() : this_type;
        // Detect if the object is accessed through a const reference (ref<const S>)
        bool is_const_this = type::is_const(subtype);
        auto bare_subtype = type::remove_const(subtype);
        auto struct_subtype = std::dynamic_pointer_cast<struct_type>(bare_subtype);
        if (!struct_subtype) {
            throw_error(0x0025, std::nullopt,
                "The '.' operator can only be applied to a struct type, "
                "but the left-hand side has type '{}' which is not a struct",
                {bare_subtype ? bare_subtype->to_string() : "?"});
        }
        auto st = struct_subtype->get_struct();

        // Use the short (unqualified) name for function lookup
        std::string func_short_name = callee->get_name().back();

        // ── Qualified member call: obj.Base::method(args) or this->Base::method(args) ──
        // If the callee symbol has more than one name component (e.g. Base::method), it is
        // an explicit qualification: bypass virtual dispatch and call the exact named class.
        const bool is_qualified_member_call = (callee->get_name().size() > 1);
        if (is_qualified_member_call) {
            // The qualifying class name is the second-to-last component (e.g. "Base" in Base::method).
            const std::string& qualifying_class_name = callee->get_name()[callee->get_name().size() - 2];

            // Find the qualifying aggregate — it must be the class itself or one of its bases.
            std::shared_ptr<aggregate> qualifying_agg;
            std::function<void(const std::shared_ptr<aggregate>&)> find_class;
            find_class = [&](const std::shared_ptr<aggregate>& agg) {
                if (!agg || qualifying_agg) return;
                if (agg->get_short_name() == qualifying_class_name) {
                    qualifying_agg = agg;
                    return;
                }
                for (auto& bs : agg->get_bases()) {
                    if (bs.base) find_class(bs.base);
                }
            };
            find_class(st);

            if (!qualifying_agg) {
                throw_error(0x0040, std::nullopt,
                    "Qualified member call '{}': '{}' is not a base class of '{}'; "
                    "the qualifying class must be the class itself or one of its base classes",
                    {callee->get_name().to_string(), qualifying_class_name, st->get_short_name()});
            }

            // Collect overloads of func_short_name directly in the qualifying class
            std::vector<std::shared_ptr<function>> qual_candidates;
            for (auto& fn : qualifying_agg->functions()) {
                if (fn && fn->get_short_name() == func_short_name) {
                    qual_candidates.push_back(fn);
                }
            }
            if (qual_candidates.empty()) {
                throw_error(0x0041, std::nullopt,
                    "No function named '{}' found in class '{}'",
                    {func_short_name, qualifying_class_name});
            }

            // Upcast this_expr to the qualifying class reference
            auto qual_ref_type = is_const_this
                ? qualifying_agg->get_struct_type()->get_const()->get_reference()
                : qualifying_agg->get_struct_type()->get_reference();
            auto upcast_this = adapt_type(this_expr, qual_ref_type);
            if (upcast_this) this_expr = upcast_this;
            // Update the sub_expr of member_callee so the IR generator uses the upcast
            member_callee->sub_expr() = this_expr;

            auto best = get_best_matching_function(qual_candidates, expr.arguments(), this_expr);
            if (!best.func) return;

            check_function_visibility(*best.func, expr);

            callee->set_target(best.func);
            expr.set_type(best.func->get_return_type());
            expr.assign_arguments(best.adapted_args);
            // Bypass virtual dispatch — this is an explicit base-class call
            expr.set_non_virtual_qualified_call(true);
            // Phase 3: annotate dispatch info
            annotate_dispatch_info(expr, best.func, member_callee);
            return;
        }

        // Collect all candidate functions (member + free/static from parent scopes)
        std::vector<std::shared_ptr<function>> candidates = scope_lookup::lookup_functions(st, func_short_name);

        // If calling on a const object, only const member functions are callable.
        if (is_const_this) {
            std::vector<std::shared_ptr<function>> const_candidates;
            for (auto& f : candidates) {
                if (!f->is_member() || f->is_static() || f->is_const_member()) {
                    const_candidates.push_back(f);
                }
            }
            if (const_candidates.empty() && !candidates.empty()) {
                throw_error(0x0034, std::nullopt,
                    "Cannot call mutable member function '{}' on a const object of type '{}': "
                    "only const member functions can be called on const objects",
                    {func_short_name, struct_subtype->name()});
            }
            candidates = std::move(const_candidates);
        }

        if (candidates.empty()) {
            throw_error(0x0026, std::nullopt,
                "No function named '{}' found in struct '{}' or its enclosing scopes; "
                "check the spelling or verify that '{}' is declared as a method or free function",
                {callee->get_name().to_string(), st->get_short_name(),
                 callee->get_name().to_string()});
        }

        auto best = get_best_matching_function(candidates, expr.arguments(), this_expr);
        if (!best.func) {
            // get_best_matching_function already reported/threw an error
            return;
        }

        // Static constructors and destructors cannot be called explicitly
        if (std::dynamic_pointer_cast<static_constructor>(best.func)) {
            throw_error(0x002B, std::nullopt,
                "Static constructor '{}' cannot be called explicitly; "
                "it is automatically invoked during program initialization",
                {best.func->get_short_name()});
        }
        if (std::dynamic_pointer_cast<static_destructor>(best.func)) {
            throw_error(0x002C, std::nullopt,
                "Static destructor '~{}' cannot be called explicitly; "
                "it is automatically invoked during program finalization",
                {best.func->get_short_name()});
        }

        // Check visibility of the resolved function
        check_function_visibility(*best.func, expr);

        callee->set_target(best.func);
        expr.set_type(best.func->get_return_type());

        // Apply adapted arguments (may include cloned defaults for trailing params)
        expr.assign_arguments(best.adapted_args);
        // Note: if best.is_unified_call, the callee stays as member_of_object_expression
        // but the resolved function is free/static. impl_gen handles this by passing
        // sub_expr() value as first argument when the function is not a member.
        // Phase 3: annotate dispatch info
        annotate_dispatch_info(expr, best.func, member_callee);
        return;
    }

    // ----------------------------------------------------------------
    // Case 1.5 : indirect call via a function-reference variable "fp(args)"
    //   callee may be unresolved (symbol_resolver deferred it as a potential function call).
    //   Try to resolve it as a variable by walking the scope chain manually.
    //   If a variable with a function_reference_type is found, treat this as indirect.
    // ----------------------------------------------------------------
    if (callee && !callee->is_resolved()) {
        // Walk up the scope chain from the callee expression to find a variable with this name.
        const k::name& sym_name = callee->get_name();
        if (sym_name.size() == 1) {
            const std::string& simple_name = sym_name.back();
            // Walk up: callee → function_invocation → ... → block → function
            std::shared_ptr<element> cur = callee->shared_as<element>();
            while (cur) {
                if (auto vh = std::dynamic_pointer_cast<variable_holder>(cur)) {
                    if (auto vdef = vh->get_variable(sym_name)) {
                        callee->set_target(vdef);
                        break;
                    }
                }
                cur = cur->parent<element>();
            }
        }
    }
    if (callee && callee->is_variable_def()) {
        // Make sure the callee symbol has its type resolved
        callee->accept(*this);
        auto callee_type = callee->get_type();
        if (callee_type) {
            // Unwrap reference / indirection wrapper
            auto inner_type = callee_type;
            while (inner_type && (type::is_reference(inner_type) || type::is_link(inner_type) ||
                                   type::is_pointer(inner_type) || type::is_pinned(inner_type))) {
                inner_type = inner_type->get_subtype();
            }
            // Also unwrap an unresolved_function_ref_type that has been resolved
            if (auto ufrt = std::dynamic_pointer_cast<unresolved_function_ref_type>(inner_type)) {
                if (ufrt->is_resolved()) {
                    inner_type = ufrt->get_resolved();
                    while (inner_type && (type::is_reference(inner_type) || type::is_link(inner_type) ||
                                           type::is_pointer(inner_type) || type::is_pinned(inner_type))) {
                        inner_type = inner_type->get_subtype();
                    }
                }
            }
            // If the inner type is a function_reference_type, this is an indirect call
            auto frt = std::dynamic_pointer_cast<function_reference_type>(inner_type);
            if (frt) {
                // Set the return type of the call expression.
                // If the frt has no return type yet (e.g. parameter with no init expression),
                // try to propagate the return type from the enclosing function's context.
                auto ret_type = frt->get_return_type();
                if (!ret_type && !_function_stack.empty()) {
                    // Propagate from the enclosing function's return type
                    ret_type = _function_stack.back()->get_return_type();
                    if (ret_type) {
                        // Mutate the frt in-place to record the inferred return type.
                        frt->set_return_type(ret_type);
                    }
                }
                expr.set_type(ret_type);
                // Type-adapt arguments against the function_reference_type's parameter types
                const auto& params = frt->get_parameter_types();
                for (size_t n = 0; n < expr.arguments().size() && n < params.size(); ++n) {
                    auto arg = expr.arguments().at(n);
                    auto cast = adapt_type(arg, params[n]);
                    if (cast && cast != arg) expr.assign_argument(n, cast);
                }
                // Mark as indirect call — no dispatch annotation needed
                virtual_dispatch_info di;
                di.kind = virtual_dispatch_info::dispatch_kind::INDIRECT;
                expr.set_dispatch_info(std::move(di));
                return;
            }
        }
    }

    // ----------------------------------------------------------------
    // Case 2 : plain symbol call  "func(args)"
    // ----------------------------------------------------------------
    {
        std::string func_name = callee->get_name().back();
        const auto& args = expr.arguments();

        // A qualified name (e.g. Base::value) means the call is non-virtual and targets
        // exactly the named function.  Do NOT collect additional member-function candidates
        // from the first argument's struct type — that would create false ambiguities and
        // would defeat the purpose of the explicit qualification.
        const bool is_qualified_call = (callee->get_name().size() > 1);

        std::vector<std::shared_ptr<function>> all_candidates;
        if (is_qualified_call) {
            // For a qualified name (e.g. Base::value or point::get), collect ALL overloads
            // of the short name within the qualifying context (struct / namespace), not the
            // entire scope chain.  This prevents false ambiguity with functions of the same
            // name in outer scopes while still supporting overload resolution.
            if (callee->is_function() && callee->get_function()) {
                auto resolved_fn = callee->get_function();
                auto owner = resolved_fn->parent<element>();
                if (owner) {
                    // Collect all overloads of func_name directly in the owner element.
                    if (auto fh = dynamic_cast<function_holder*>(owner.get())) {
                        for (auto& fn : fh->functions()) {
                            if (fn && fn->get_short_name() == func_name) {
                                all_candidates.push_back(fn);
                            }
                        }
                    }
                }
                // Fallback: only the resolved function
                if (all_candidates.empty()) {
                    all_candidates.push_back(resolved_fn);
                }
            }
        } else {
            all_candidates = scope_lookup::lookup_functions(callee, func_name);
        }

        std::shared_ptr<expression> this_candidate;
        std::vector<std::shared_ptr<expression>> rest_args;

        // For a qualified call (e.g. Base::value(d) or point::get(pt, 6f)):
        // If the first argument is a reference to a struct, treat it as the potential 'this'
        // for member functions (Mode A) while also allowing Mode B matching for static functions.
        // We do NOT restrict to all_are_member because the candidates may be a mix of member
        // and static overloads.
        if (is_qualified_call && !all_candidates.empty() && !args.empty()) {
            auto first_arg_type = args[0]->get_type();
            if (type::is_reference(first_arg_type)) {
                auto bare_sub = type::remove_const(first_arg_type->get_subtype());
                if (std::dynamic_pointer_cast<struct_type>(bare_sub)) {
                    this_candidate = args[0];
                    rest_args = std::vector<std::shared_ptr<expression>>(args.begin() + 1, args.end());
                }
            }
        }

        // ── Implicit 'this' injection for Base::method() from inside a member function ──
        // If we have a qualified call (Base::method) with no explicit 'this' argument yet,
        // and we are inside a non-static member function whose owning class is derived from
        // the qualifying base class, inject 'this' automatically.
        // This enables the pattern:  Base::method()  inside an override instead of
        // the more verbose:  Base::method(this)
        if (is_qualified_call && !this_candidate && !_function_stack.empty()) {
            auto enclosing_fn = _function_stack.back();
            if (enclosing_fn && enclosing_fn->is_member() && !enclosing_fn->is_static()) {
                auto this_param = enclosing_fn->get_this_parameter();
                if (this_param && this_param->get_type()) {
                    // Check that at least one candidate is a member function (not static)
                    bool any_member = std::any_of(all_candidates.begin(), all_candidates.end(),
                        [](const std::shared_ptr<function>& f){ return f && f->is_member() && !f->is_static(); });
                    if (any_member) {
                        // Build a symbol_expression for 'this'
                        auto this_sym = symbol_expression::from_identifier(k::name("this"));
                        this_sym->set_target(std::const_pointer_cast<parameter>(this_param));
                        this_sym->set_type(this_param->get_type());
                        this_candidate = this_sym;
                        rest_args = args; // all explicit args remain as-is (no args consumed)
                    }
                }
            }
        }

        // Unified-call-syntax: only when the call is NOT a qualified name.
        // For a qualified call "Base::method(d)", d is already handled above.
        if (!is_qualified_call && !args.empty()) {
            auto first_arg_type = args[0]->get_type();
            if (type::is_reference(first_arg_type)) {
                if (auto first_struct = std::dynamic_pointer_cast<struct_type>(first_arg_type->get_subtype())) {
                    auto st = first_struct->get_struct();
                    this_candidate = args[0];
                    rest_args = std::vector<std::shared_ptr<expression>>(args.begin() + 1, args.end());
                    // Collect member functions from the aggregate and all its bases (recursively)
                    std::function<void(const std::shared_ptr<aggregate>&)> collect_member_fns;
                    collect_member_fns = [&](const std::shared_ptr<aggregate>& s) {
                        if (!s) return;
                        for (auto& f : scope_lookup::lookup_functions(s, func_name)) {
                            if (std::find(all_candidates.begin(), all_candidates.end(), f) == all_candidates.end()) {
                                all_candidates.push_back(f);
                            }
                        }
                        for (auto& bs : s->get_bases()) {
                            if (bs.base) collect_member_fns(bs.base);
                        }
                    };
                    collect_member_fns(st);
                }
            }
        }

        if (all_candidates.empty()) {
            if (callee->is_function()) {
                auto already_func = callee->get_function();
                expr.set_type(already_func->get_return_type());
                const auto& params = already_func->parameters();
                for (size_t n = 0; n < expr.arguments().size() && n < params.size(); ++n) {
                    auto arg = expr.arguments().at(n);
                    auto w = compute_cast_weight(arg, params[n]->get_type());
                    if (w != CAST_IMPOSSIBLE) {
                        auto cast = adapt_type(arg, params[n]->get_type());
                        if (cast && cast != arg) expr.assign_argument(n, cast);
                    }
                }
                // Phase 3: annotate — already_func is resolved but we have no member_callee here
                {
                    auto mc = std::dynamic_pointer_cast<member_of_object_expression>(expr.callee_expr());
                    annotate_dispatch_info(expr, already_func, mc);
                }
                return;
            }
            throw_error(0x0027, std::nullopt,
                "No function named '{}' found in the current scope; "
                "check the spelling or add the appropriate declaration",
                {func_name});
        }

        FunctionCandidate best = get_best_matching_function(all_candidates,
                                                            this_candidate ? rest_args : args,
                                                            this_candidate,
                                                            this_candidate ? &args : nullptr);
        bool is_free_to_member_call = false;

        if (!best.func) {
            // get_best_matching_function already reported/threw an error
            return;
        }

        // Static constructors and destructors cannot be called explicitly
        if (std::dynamic_pointer_cast<static_constructor>(best.func)) {
            throw_error(0x002D, std::nullopt,
                "Static constructor '{}' cannot be called explicitly; "
                "it is automatically invoked during program initialization",
                {best.func->get_short_name()});
        }
        if (std::dynamic_pointer_cast<static_destructor>(best.func)) {
            throw_error(0x002E, std::nullopt,
                "Static destructor '~{}' cannot be called explicitly; "
                "it is automatically invoked during program finalization",
                {best.func->get_short_name()});
        }

        // Check visibility of the resolved function
        check_function_visibility(*best.func, expr);

        if (this_candidate && best.func->is_member() && !best.func->is_static() && !best.is_unified_call) {
            is_free_to_member_call = true;
        }

        callee->set_target(best.func);
        expr.set_type(best.func->get_return_type());

        if (is_free_to_member_call) {
            // Member function found via free-function syntax: func(obj, args...)
            // (also covers qualified calls like Base::method(d))
            auto obj_expr = this_candidate; // first arg is the object

            // For a qualified call targeting a base-class method (e.g. Base::value(d)),
            // adapt the object expression to the expected 'this' type (upcast Derived→Base).
            if (is_qualified_call && best.func->is_member() && !best.func->is_static()) {
                // The implicit 'this' type is ref<OwningClass>.
                auto owner_st = best.func->parent<aggregate>();
                if (owner_st) {
                    auto owner_ref_type = owner_st->get_struct_type()
                        ? owner_st->get_struct_type()->get_reference()
                        : nullptr;
                    if (owner_ref_type) {
                        auto adapted_obj = adapt_type(obj_expr, owner_ref_type);
                        if (adapted_obj) obj_expr = adapted_obj;
                    }
                }
            }

            auto sym_for_member = symbol_expression::from_function(best.func);
            sym_for_member->set_target(best.func);
            auto member_expr = member_of_object_expression::make_shared(obj_expr, sym_for_member);
            expr.assign(member_expr, best.adapted_args);

            // A qualified call (e.g. Base::method(d)) must bypass virtual dispatch
            // and invoke the exact named function directly.
            if (is_qualified_call) {
                expr.set_non_virtual_qualified_call(true);
            }
        } else {
            // Regular/unified call — may include default values for trailing params
            expr.assign_arguments(best.adapted_args);
        }

        // Phase 3: annotate dispatch info
        // After the potential rewrite above, re-read member_callee from the (possibly updated) callee.
        {
            auto updated_member_callee = std::dynamic_pointer_cast<member_of_object_expression>(expr.callee_expr());
            annotate_dispatch_info(expr, best.func, updated_member_callee);
        }
        return;
    }
}

void implementation_generator::visit_function_invocation_expression(function_invocation_expression &expr) {
    auto callee = std::dynamic_pointer_cast<symbol_expression>(expr.callee_expr());
    auto member_callee = std::dynamic_pointer_cast<member_of_object_expression>(expr.callee_expr());
    auto pm_callee = std::dynamic_pointer_cast<pm_expression>(expr.callee_expr());

    if(!callee && !member_callee && !pm_callee) {
        throw_internal_error(0x000C, std::nullopt,
            "Internal error: unsupported call expression form during code generation; "
            "only direct, member and pointer-to-member function calls are supported");
    }

    // ── INDIRECT_MEMBER call via pointer-to-member  obj.*mfp(args) ────────────
    if (expr.has_dispatch_info() &&
        expr.get_dispatch_info().kind == virtual_dispatch_info::dispatch_kind::INDIRECT_MEMBER) {
        // pm_callee->left() = object expression (this), pm_callee->right() = mfp variable
        if (!pm_callee) {
            throw_internal_error(0x0048, std::nullopt,
                "Internal error: INDIRECT_MEMBER dispatch without a pm_expression callee");
        }

        // 1. Evaluate the object (this) pointer
        _value = nullptr;
        pm_callee->left()->accept(*this);
        llvm::Value* this_val = _value;

        // If the object is a ref/indirection, load the actual pointer
        auto obj_type = pm_callee->left()->get_type();
        if (auto ref = std::dynamic_pointer_cast<reference_type>(obj_type)) {
            auto inner = ref->get_subtype();
            if (pm_callee->is_arrow()) {
                // ->*: load the pointer value from the ref, then we have a ptr-to-struct
                this_val = _builder->CreateLoad(_context->get_llvm_type(inner), this_val, "pm_ptr_load");
                // Null-check for nullable indirections
                if (std::dynamic_pointer_cast<pointer_type>(inner) ||
                    std::dynamic_pointer_cast<pinned_type>(inner)) {
                    auto* fatal = get_or_declare_fatal_null_function("__fatal_null_dereference");
                    emit_null_check(this_val, fatal, "pm_arrow");
                }
            }
            // For .*, this_val is already the struct alloca address (which is what we want as `this`)
        }

        // 2. Evaluate the member function pointer variable (load fn pointer)
        _value = nullptr;
        pm_callee->right()->accept(*this);
        llvm::Value* mfp_alloca = _value;

        auto mfp_type = pm_callee->right()->get_type();
        if (auto ref = std::dynamic_pointer_cast<reference_type>(mfp_type)) {
            mfp_type = ref->get_subtype();
        }
        auto frt = std::dynamic_pointer_cast<function_reference_type>(mfp_type);
        if (!frt || !mfp_alloca) {
            throw_internal_error(0x0049, std::nullopt,
                "Internal error: INDIRECT_MEMBER call: could not obtain function pointer");
        }

        // Load the actual function pointer from the alloca
        llvm::Type* frt_llvm = frt->get_llvm_type();
        llvm::Value* fn_ptr = _builder->CreateLoad(frt_llvm, mfp_alloca, "mfp_fn_ptr");

        // 3. Build LLVM function type from frt parameter types
        //    For member_function_reference_type the first param is implicit `this` (ptr)
        std::vector<llvm::Type*> param_llvm_types;
        // Always prepend `this` as opaque ptr
        param_llvm_types.push_back(llvm::PointerType::getUnqual(**_context));
        for (const auto& pt : frt->get_parameter_types()) {
            // Skip the implicit this param if it's already in get_parameter_types()
            auto llt = _context->get_llvm_type(pt);
            if (!llt) continue;
            param_llvm_types.push_back(llt);
        }
        llvm::Type* ret_llvm = frt->get_return_type()
            ? _context->get_llvm_type(frt->get_return_type())
            : llvm::Type::getVoidTy(**_context);
        auto llvm_fn_type = llvm::FunctionType::get(ret_llvm, param_llvm_types, false);

        // 4. Build call arguments: this_val first, then expression arguments
        std::vector<llvm::Value*> call_args;
        call_args.push_back(this_val);
        for (auto& arg : expr.arguments()) {
            _value = nullptr;
            arg->accept(*this);
            if (_value) call_args.push_back(_value);
        }

        _value = _builder->CreateCall(llvm_fn_type, fn_ptr, call_args, "mfp_call");
        return;
    }

    // ── INDIRECT call via function-reference variable ─────────────────────────
    if (expr.has_dispatch_info() &&
        expr.get_dispatch_info().kind == virtual_dispatch_info::dispatch_kind::INDIRECT) {
        // callee is a symbol_expression that holds a variable of function_reference_type.
        // We already visited the callee in type_reference_resolver, so its type is set.
        // In impl_gen, visiting a variable symbol gives us the *address* of the variable (alloca).
        // For function-reference variables, we must load the function pointer from that address.
        _value = nullptr;
        if (callee) callee->accept(*this);
        llvm::Value* var_addr = _value;
        if (!var_addr) {
            throw_internal_error(0x0041, std::nullopt,
                "Internal error: indirect call through function reference produced no LLVM value");
        }

        // Build the LLVM function type from the function_reference_type in the call expression.
        auto callee_type = callee ? callee->get_type() : nullptr;
        auto inner_type = callee_type;
        while (inner_type && (type::is_reference(inner_type) || type::is_link(inner_type) ||
                               type::is_pointer(inner_type) || type::is_pinned(inner_type))) {
            inner_type = inner_type->get_subtype();
        }
        auto frt = std::dynamic_pointer_cast<function_reference_type>(inner_type);
        if (!frt) {
            throw_internal_error(0x0042, std::nullopt,
                "Internal error: indirect call without a function_reference_type annotation");
        }

        // The function_reference_type has a ptr (opaque pointer) as its LLVM type.
        // Load the actual function pointer from the variable's address.
        llvm::Type* frt_llvm = _context->get_llvm_type(inner_type); // = opaque ptr
        llvm::Value* fn_ptr = _builder->CreateLoad(frt_llvm, var_addr, "fn_ptr_load");

        // Build LLVM parameter types from the function_reference_type
        std::vector<llvm::Type*> param_llvm_types;
        for (const auto& pt : frt->get_parameter_types()) {
            auto llt = _context->get_llvm_type(pt);
            if (!llt) {
                throw_internal_error(0x0043, std::nullopt,
                    "Internal error: could not map K parameter type to LLVM type for indirect call");
            }
            param_llvm_types.push_back(llt);
        }
        llvm::Type* ret_llvm_type = frt->get_return_type()
            ? _context->get_llvm_type(frt->get_return_type())
            : llvm::Type::getVoidTy(**_context);
        auto llvm_fn_type = llvm::FunctionType::get(ret_llvm_type, param_llvm_types, false);

        // Generate arguments
        std::vector<llvm::Value*> call_args;
        for (auto& arg : expr.arguments()) {
            _value = nullptr;
            arg->accept(*this);
            if (!_value) {
                throw_internal_error(0x0044, std::nullopt,
                    "Internal error: an argument for an indirect call produced no LLVM value");
            }
            call_args.push_back(_value);
        }

        _value = _builder->CreateCall(llvm_fn_type, fn_ptr, call_args, "ind_call");
        return;
    }

    // Generate arguments and add them to the args list (for non-indirect calls)
    std::vector<llvm::Value*> args;
    if (member_callee) {
        callee = std::dynamic_pointer_cast<symbol_expression>(member_callee->symbol().shared_as<symbol_expression>());
        if (!callee) {
            throw_internal_error(0x000D, std::nullopt,
                "Internal error: member function call has a non-symbol callee; "
                "this should have been rejected during type resolution");
        }

        // First argument is the object pointer (this)
        member_callee->sub_expr()->accept(*this);
        if(!_value) {
            throw_internal_error(0x000E, std::nullopt,
                "Internal error: failed to generate the 'this' argument for member function call '{}'; "
                "the object expression produced no LLVM value",
                {callee ? callee->get_name().to_string() : "<unknown>"});
        }

        args.push_back(_value);
    }
    for(auto arg : expr.arguments()) {
        _value = nullptr;
        arg->accept(*this);
        if(!_value) {
            throw_internal_error(0x000F, std::nullopt,
                "Internal error: a call argument for '{}' produced no LLVM value during code generation; "
                "this indicates a bug in expression code generation",
                {callee ? callee->get_name().to_string() : "<unknown>"});
        }
        args.push_back(_value);
    }

    // Find the function definition
    auto function = callee->get_function();
    auto it = _context->_functions.find(function);
    if(it==_context->_functions.end()) {
        // Abstract virtual functions and imported virtual functions without a
        // concrete LLVM declaration can still be dispatched through the vtable.
        if (function && function->is_virtual() &&
            (function->is_abstract_func() || function->is_external())) {
            // Fall through: llvm_func will remain null; virtual dispatch handles it below.
        } else {
            throw_internal_error(0x0010, std::nullopt,
                "Internal error: LLVM declaration not found for function '{}' during code generation; "
                "the declaration pass must be run before the implementation pass",
                {function ? function->get_fq_name() : "<null>"});
        }
    }
    llvm::Function* llvm_func = (it != _context->_functions.end()) ? it->second : nullptr;
    if(llvm_func == nullptr &&
       !(function && (function->is_abstract_func() ||
                      (function->is_virtual() && function->is_external())))) {
        throw_internal_error(0x0011, std::nullopt,
            "Internal error: LLVM function object is null for '{}'; "
            "this indicates a compiler bug in the declaration pass",
            {function ? function->get_fq_name() : "<null>"});
    }

    // ── Virtual dispatch ─────────────────────────────────────────────────────
    // Phase 3/4: dispatch_info is normally set by type_reference_resolver.
    // If absent (e.g. a synthetic node that bypassed the resolver), treat as DIRECT.
    const bool is_vtable_dispatch =
        expr.has_dispatch_info()
        && expr.get_dispatch_info().kind == virtual_dispatch_info::dispatch_kind::VTABLE
        && (expr.get_dispatch_info().dispatch_class != nullptr
            || expr.get_dispatch_info().imported_dispatch_agg != nullptr);

    if (is_vtable_dispatch) {
        const auto& di = expr.get_dispatch_info();

        // ── Local klass dispatch ──────────────────────────────────────────
        auto kl = di.dispatch_class;
        if (kl && kl->has_vtable() && !args.empty()) {
            llvm::FunctionType* fn_type = nullptr;
            if (llvm_func) {
                fn_type = llvm_func->getFunctionType();
            } else {
                std::vector<llvm::Type*> param_types;
                if (function->is_member() && !function->is_static())
                    param_types.push_back(_context->get_llvm_type(function->get_this_parameter()->get_type()));
                for (const auto& param : function->parameters())
                    param_types.push_back(_context->get_llvm_type(param->get_type()));
                llvm::Type* ret_type = function->has_return_type()
                    ? _context->get_llvm_type(function->get_return_type())
                    : llvm::Type::getVoidTy(**_context);
                fn_type = llvm::FunctionType::get(ret_type, param_types, false);
            }
            _value = emit_virtual_dispatch_call(*_builder, *kl, args[0], di.slot_index, fn_type, args, _context, "");
            return;
        }

        // ── Imported aggregate dispatch (imported_klass / imported_interface) ──
        // The LLVM struct type was interned from llvm_def — field 0 is always the
        // primary vptr.  The vtable layout is:  { RTTI ptr, slot0 ptr, slot1 ptr, … }
        // so the function pointer is at index  (slot_index + 1).
        auto imp_agg = di.imported_dispatch_agg;
        if (imp_agg && imp_agg->has_vtable() && !args.empty()) {
            // Build the callee FunctionType from the LLVM declaration if we have it,
            // or reconstruct from K model types as fallback.
            llvm::FunctionType* fn_type = nullptr;
            if (llvm_func) {
                fn_type = llvm_func->getFunctionType();
            } else if (function) {
                std::vector<llvm::Type*> param_types;
                if (function->is_member() && !function->is_static() && function->get_this_parameter())
                    param_types.push_back(_context->get_llvm_type(function->get_this_parameter()->get_type()));
                for (const auto& param : function->parameters())
                    param_types.push_back(_context->get_llvm_type(param->get_type()));
                llvm::Type* ret_type = function->has_return_type()
                    ? _context->get_llvm_type(function->get_return_type())
                    : llvm::Type::getVoidTy(**_context);
                fn_type = llvm::FunctionType::get(ret_type, param_types, false);
            }
            if (!fn_type) {
                throw_internal_error(0x0015, std::nullopt,
                    "Internal error: cannot build FunctionType for imported virtual dispatch of '{}'",
                    {function ? function->get_fq_name() : "<null>"});
            }

            auto* struct_llvm_type = imp_agg->get_struct_type()
                                     ? imp_agg->get_struct_type()->get_llvm_type() : nullptr;
            if (!struct_llvm_type) {
                throw_internal_error(0x0016, std::nullopt,
                    "Internal error: imported aggregate '{}' has no LLVM struct type",
                    {imp_agg->get_fq_name()});
            }

            llvm::LLVMContext& llvm_ctx = **_context;
            llvm::Type* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);

            // Find the vptr field index from the KDI layout (first kdi_layout_vptr).
            uint32_t vptr_field_index = 0; // default: field 0 (primary vptr)
            auto imp_agg_cast = std::dynamic_pointer_cast<imported_aggregate>(imp_agg);
            if (imp_agg_cast) {
                const auto* kdi_agg = imp_agg_cast->get_kdi_aggregate();
                if (kdi_agg) {
                    for (const auto& lf : kdi_agg->layout) {
                        if (auto* vp = std::get_if<kdi::kdi_layout_vptr>(&lf)) {
                            vptr_field_index = vp->llvm_field_index;
                            break;
                        }
                    }
                }
            }

            // Load the vptr
            llvm::Value* vptr_addr = _builder->CreateStructGEP(
                struct_llvm_type, args[0], vptr_field_index, "imp_vptr_addr");
            llvm::Value* vptr = _builder->CreateLoad(ptr_ty, vptr_addr, "imp_vptr");

            // The vtable layout is { RTTI, fn0, fn1, … } so slot i → index i+1.
            // We use a byte-offset GEP because we don't have the vtable's StructType.
            // On all supported 64-bit targets a pointer is 8 bytes.
            const uint64_t ptr_size = 8;
            llvm::Value* slot_offset = llvm::ConstantInt::get(
                llvm::Type::getInt64Ty(llvm_ctx),
                (di.slot_index + 1) * ptr_size);
            llvm::Value* fn_ptr_addr = _builder->CreateInBoundsGEP(
                llvm::Type::getInt8Ty(llvm_ctx), vptr, slot_offset, "imp_vtbl_slot");
            llvm::Value* fn_ptr = _builder->CreateLoad(ptr_ty, fn_ptr_addr, "imp_fn_ptr");
            _value = _builder->CreateCall(fn_type, fn_ptr, args,
                fn_type->getReturnType()->isVoidTy() ? "" : "imp_vcall");
            return;
        }
    }
    // ── Direct call (non-virtual, or qualified, or free function) ────────────
    _value = _builder->CreateCall(llvm_func, args);
}

//
// Constructor invocation
//

void symbol_resolver::visit_constructor_invocation_expression(constructor_invocation_expression& expr) {
    for (auto arg : expr.arguments()) {
        arg->accept(*this);
    }
}

void type_reference_resolver::visit_constructor_invocation_expression(constructor_invocation_expression& expr) {
    // Just accept arguments,
    // the rest of resolution will be done (just after) in variable definition caller
    for(auto& arg : expr.arguments()) {
        arg->accept(*this);
    }

    // This expression is always returning the reference to the constructed object
    // so type is set to the reference of the constructed symbol type.
    auto var_def = expr.constructed_symbol()->get_variable_def();
    if(!var_def) {
        throw_internal_error(0x0007, std::nullopt,
            "Internal error: constructor invocation expression does not refer to a variable definition; "
            "the constructed symbol must be a variable — this indicates a compiler bug");
    }
    expr.set_type(var_def->get_type()->get_reference());

    // Check if constructor is explicitly needed
    auto var_type = var_def->get_type();
    if (!var_type) {
        throw_internal_error(0x0008, std::nullopt,
            "Internal error: constructor invocation refers to a variable '{}' with no type; "
            "the type must be resolved before constructor invocation can be typed",
            {var_def->get_fq_name()});
    }
    if (type::is_primitive(var_type)) {
        if (!expr.empty()) {
            auto cast = adapt_type(expr.argument(0), var_type);
            if (cast && cast != expr.argument(0)) {
                expr.assign_argument(0, cast);
            }
        }
    } else if (auto st_type = std::dynamic_pointer_cast<struct_type>(var_type)) {
        auto st = st_type->get_struct();
        std::vector<std::shared_ptr<expression>> ctor_args = expr.arguments();

        // For non-static inner structs: auto-inject __parent__ if constructing from the outer struct context
        if (st && st->is_inner()) {
            auto outer_struct = st->get_enclosing_structure();
            auto var_elem = dynamic_cast<const element*>(var_def.get());
            bool in_outer_method = false;
            if (var_elem) {
                auto enclosing_func = var_elem->ancestor<function>();
                if (enclosing_func && enclosing_func->is_member() && !enclosing_func->is_static()) {
                    if (enclosing_func->get_owner() == outer_struct) in_outer_method = true;
                }
            }
            if (in_outer_method) {
                bool needs_inject = true;
                if (!st->constructors().empty()) {
                    if (ctor_args.size() == st->constructors()[0]->parameters().size()) needs_inject = false;
                }
                if (needs_inject) {
                    auto this_sym = symbol_expression::from_identifier(k::name("this"));
                    auto enclosing_func = var_elem->ancestor<function>();
                    if (enclosing_func && enclosing_func->get_this_parameter()) {
                        this_sym->set_target(std::const_pointer_cast<parameter>(enclosing_func->get_this_parameter()));
                        this_sym->set_type(enclosing_func->get_this_parameter()->get_type());
                    }
                    ctor_args.insert(ctor_args.begin(), this_sym);
                    expr.arguments(ctor_args);
                }
            }
        }

        auto [best_constructor, adapted_args] = get_best_matching_constructor(st->constructors(), ctor_args);
        if (!best_constructor) {
            throw_error(0x002A, std::nullopt,
                "No matching constructor found for member initialisation of type '{}': "
                "none of the available constructors can be called with the provided arguments",
                {st_type->to_string()});
        }
        // Cannot instantiate an abstract class directly.
        // Skip this check for synthetic base sub-object fields (e.g. __base_X__ / __vbase_X__)
        // which are compiler-generated during base class construction.
        if (st->is_abstract()) {
            auto var_name = var_def->get_short_name();
            bool is_subobject_init = (var_name.rfind("__base_", 0) == 0)
                                  || (var_name.rfind("__vbase_", 0) == 0);
            if (!is_subobject_init) {
                throw_error(0x0032, std::nullopt,
                    "Cannot instantiate abstract class '{}'; abstract classes cannot be directly instantiated",
                    {st->get_short_name()});
            }
        }
        // Check constructor visibility
        check_constructor_visibility(*best_constructor, expr);
        expr.set_constructor(best_constructor);
        expr.arguments(adapted_args);
    }
}

void implementation_generator::visit_constructor_invocation_expression(constructor_invocation_expression& expr) {
    auto var_def = expr.constructed_symbol()->get_variable_def();
    if (!var_def) {
        throw_internal_error(0x0012, std::nullopt,
            "Internal error: constructor invocation expression does not refer to a variable definition; "
            "this indicates a compiler bug — the constructed symbol must be a variable");
    }

    auto var_type = var_def->get_type();

    llvm::Value* object_ref = nullptr;
    _value = nullptr;
    expr.constructed_symbol()->accept(*this);
    object_ref = _value;
    _value = nullptr;

    if(!object_ref) {
        throw_internal_error(0x0013, std::nullopt,
            "Internal error: failed to obtain an LLVM reference for the object being constructed ('{}'); "
            "the variable must have been allocated before constructor code generation",
            {var_def->get_fq_name()});
    }

    if (auto prim_type = std::dynamic_pointer_cast<primitive_type>(var_type)) {
        llvm::Value* value = nullptr;
        if (!expr.empty()) {
            auto first_arg = expr.argument(0);
            if (auto value_expr = std::dynamic_pointer_cast<value_expression>(first_arg)) {
                if (!std::dynamic_pointer_cast<global_variable_definition>(value_expr)) {
                    llvm::Constant* constant = _context->get_llvm_constant_from_value_expression(*value_expr);
                    if (constant == nullptr) {
                        throw_internal_error(0x0014, std::nullopt,
                            "Internal error: failed to generate an LLVM constant from a literal value expression "
                            "during primitive constructor invocation for variable '{}'; "
                            "this indicates a compiler bug",
                            {var_def->get_fq_name()});
                    } else {
                        value = constant;
                    }
                }
            }
            if (value == nullptr) {
                _value = nullptr;
                first_arg->accept(*this);
                if (!_value) {
                    throw_internal_error(0x0015, std::nullopt,
                        "Internal error: failed to generate an LLVM value for the initialisation argument "
                        "of variable '{}'; the argument expression produced no result",
                        {var_def->get_fq_name()});
                } else {
                    value = _value;
                }
            }
        }
        if (value != nullptr) {
            _builder->CreateStore(value, object_ref);
        }
    } else if (auto st_type = std::dynamic_pointer_cast<struct_type>(var_type)) {
        auto st = st_type->get_struct();
        std::vector<llvm::Value*> args;
        args.push_back(object_ref);
        for(auto arg : expr.arguments()) {
            _value = nullptr;
            arg->accept(*this);
            if(!_value) {
                throw_internal_error(0x0016, std::nullopt,
                    "Internal error: a constructor argument for type '{}' produced no LLVM value; "
                    "this indicates a code-generation bug",
                    {st_type->to_string()});
            }
            args.push_back(_value);
        }
        auto function = expr.get_constructor();
        auto it = _context->_functions.find(function);
        if(it==_context->_functions.end()) {
            throw_internal_error(0x0017, std::nullopt,
                "Internal error: LLVM declaration not found for constructor of type '{}'; "
                "the declaration pass must be run before the implementation pass",
                {st_type->to_string()});
        }
        llvm::Function* llvm_func = it->second;
        if(!llvm_func) {
            throw_internal_error(0x0018, std::nullopt,
                "Internal error: LLVM constructor function object is null for type '{}'; "
                "this indicates a compiler bug in the declaration pass",
                {st_type->to_string()});
        }
        _value = _builder->CreateCall(llvm_func, args);

    } else if (auto ptr_var_type = std::dynamic_pointer_cast<pointer_type>(var_type)) {
        // Pointer (*) variable: store the address of the pointed-to object.
        // For ref<indirection> source, load the indirection value; for ref<struct>, the alloca address IS the pointer.
        if (!expr.empty()) {
            _value = nullptr;
            expr.argument(0)->accept(*this);
            if (_value) {
                auto arg_type = expr.argument(0)->get_type();
                if (arg_type && type::is_reference(arg_type)) {
                    auto inner = std::dynamic_pointer_cast<reference_type>(arg_type)->get_subtype();
                    // Only load if inner is an indirection (ptr/link/pin); for a plain struct the alloca IS the pointer.
                    if (type::is_any_indirection(inner)) {
                        _value = _builder->CreateLoad(_context->get_llvm_type(inner), _value, "ptr_init_load");
                    }
                }
                _builder->CreateStore(_value, object_ref);
            }
        }
        _value = object_ref;

    } else if (type::is_link(var_type) || type::is_pinned(var_type)) {
        // Link (~) or pinned (^) variable: store the address of the linked object.
        if (!expr.empty()) {
            _value = nullptr;
            expr.argument(0)->accept(*this);
            if (_value) {
                // If the argument type is ref<link/pin/ptr<T>>, load the stored indirection value.
                // If the argument type is ref<struct_T> (a direct object reference), the alloca
                // address IS the link value — no load needed.
                auto arg_type = expr.argument(0)->get_type();
                if (arg_type && type::is_reference(arg_type)) {
                    auto inner = std::dynamic_pointer_cast<reference_type>(arg_type)->get_subtype();
                    if (type::is_any_indirection(inner)) {
                        // ref<link/pin/ptr<T>>: load the stored pointer value
                        _value = _builder->CreateLoad(_context->get_llvm_type(inner), _value, "ind_init_load");
                    }
                    // else: ref<struct T> — _value is already the address of the object (= the link)
                }
                if (type::is_link(var_type)) {
                    // Non-null required: emit null-check if source is nullable.
                    // If the argument is a cast_expression (e.g. upcast Derived→Base), pierce through to
                    // the original source type to check its nullability.
                    auto effective_type = arg_type;
                    if (effective_type && type::is_reference(effective_type)) {
                        effective_type = std::dynamic_pointer_cast<reference_type>(effective_type)->get_subtype();
                    }
                    // Pierce cast_expression to find the real source nullability
                    if (auto cast_arg = std::dynamic_pointer_cast<cast_expression>(expr.argument(0))) {
                        auto inner_type = cast_arg->sub_expr()->get_type();
                        if (inner_type && type::is_reference(inner_type)) {
                            inner_type = std::dynamic_pointer_cast<reference_type>(inner_type)->get_subtype();
                        }
                        if (inner_type && type::is_nullable_indirection(inner_type)) {
                            effective_type = inner_type;
                        }
                    }
                    if (effective_type && type::is_nullable_indirection(effective_type)) {
                        auto* fatal = get_or_declare_fatal_null_function("__fatal_null_assignation");
                        emit_null_check(_value, fatal, "link_ctor");
                    }
                }
                _builder->CreateStore(_value, object_ref);
            }
        }
        _value = object_ref;

    } else if (auto ref_type = std::dynamic_pointer_cast<reference_type>(var_type)) {
        // Reference variable — could be a plain ref (int&) or an array ref (int[N]&).
        // In both cases we need the raw alloca, not the loaded pointer.

        llvm::Value* alloca_ptr = nullptr;
        if (auto local_var = std::dynamic_pointer_cast<variable_statement>(var_def)) {
            auto it = _context->_variables.find(local_var);
            if (it != _context->_variables.end()) alloca_ptr = it->second;
        } else if (auto global_var = std::dynamic_pointer_cast<global_variable_definition>(var_def)) {
            auto it = _context->_global_vars.find(global_var);
            if (it != _context->_global_vars.end()) alloca_ptr = it->second;
        }

        if (!alloca_ptr) {
            throw_internal_error(0x001A, std::nullopt,
                "Internal error: could not obtain the storage location for reference variable '{}'; "
                "the variable must have been allocated before constructor code generation",
                {var_def->get_fq_name()});
        }

        if (!expr.empty()) {
            auto first_arg = expr.argument(0);
            _value = nullptr;
            first_arg->accept(*this);
            if (!_value) {
                throw_internal_error(0x001B, std::nullopt,
                    "Internal error: reference variable '{}' initialisation argument produced no LLVM value; "
                    "this indicates a code-generation bug",
                    {var_def->get_fq_name()});
            }

            auto ref_sub = ref_type->get_subtype();
            if (type::is_sized_array(ref_sub)) {
                // int[N]& : copy-initialise.
                // alloca_ptr is the alloca of the reference variable — it holds a POINTER (ptr),
                // not the struct itself.  We must:
                //   1. Allocate a fresh { i32, [N x T] } struct in the entry block.
                //   2. Copy elements from the source into that fresh struct.
                //   3. Store the address of the fresh struct into alloca_ptr.
                auto dest_arr = std::dynamic_pointer_cast<sized_array_type>(ref_sub);
                auto src_ptr  = _value; // ptr to source struct { i32, [M x T] }

                auto* struct_llvm    = dest_arr->get_llvm_struct_type();
                auto* data_arr_llvm  = dest_arr->get_llvm_data_array_type();
                auto* elem_llvm      = _context->get_llvm_type(dest_arr->get_subtype());
                auto  dest_n         = static_cast<uint64_t>(dest_arr->get_size());

                // Allocate the destination struct in the entry block
                auto* fn = _builder->GetInsertBlock()->getParent();
                llvm::IRBuilder<> entry_build(&fn->getEntryBlock(), fn->getEntryBlock().begin());
                llvm::AllocaInst* dest_struct_alloca = entry_build.CreateAlloca(
                    struct_llvm, nullptr, var_def->get_short_name() + "_arr_storage");

                // Zero-fill destination struct, then set its size field
                _builder->CreateStore(llvm::ConstantAggregateZero::get(struct_llvm), dest_struct_alloca);
                llvm::Value* dest_size_field = _builder->CreateStructGEP(struct_llvm, dest_struct_alloca,
                    sized_array_type::FIELD_SIZE, "dest_size_fld");
                _builder->CreateStore(
                    llvm::ConstantInt::get(llvm::Type::getInt32Ty(_builder->getContext()), dest_n, false),
                    dest_size_field);

                // Determine the source struct type (may differ in array size from dest_arr)
                // We read the source capacity from field 0.
                // Use the source's struct type by reading src_n from field 0.
                // Since field 0 is always i32 at offset 0, we can use dest_arr's struct type for GEP.
                llvm::Value* src_size_field = _builder->CreateStructGEP(struct_llvm, src_ptr,
                    sized_array_type::FIELD_SIZE, "src_size_fld");
                llvm::Value* src_n = _builder->CreateLoad(
                    llvm::Type::getInt32Ty(_builder->getContext()), src_size_field, "src_n");

                // Data pointers
                llvm::Value* src_data_ptr  = _builder->CreateStructGEP(struct_llvm, src_ptr,
                    sized_array_type::FIELD_DATA, "src_data_ptr");
                llvm::Value* dest_data_ptr = _builder->CreateStructGEP(struct_llvm, dest_struct_alloca,
                    sized_array_type::FIELD_DATA, "dest_data_ptr");

                // copy_n = min(dest_n, src_n)
                auto* i32_t = llvm::Type::getInt32Ty(_builder->getContext());
                auto* dest_n_val = llvm::ConstantInt::get(i32_t, dest_n, false);
                llvm::Value* copy_n_val = _builder->CreateSelect(
                    _builder->CreateICmpULT(src_n, dest_n_val), src_n, dest_n_val, "copy_n");

                // --- copy loop ---
                auto* copy_entry_bb = _builder->GetInsertBlock();
                auto* copy_loop_bb  = llvm::BasicBlock::Create(_builder->getContext(), "arr_ref_copy_loop", fn);
                auto* copy_done_bb  = llvm::BasicBlock::Create(_builder->getContext(), "arr_ref_copy_done", fn);
                auto* zero_loop_bb  = llvm::BasicBlock::Create(_builder->getContext(), "arr_ref_zero_loop", fn);
                auto* init_done_bb  = llvm::BasicBlock::Create(_builder->getContext(), "arr_ref_init_done", fn);

                _builder->CreateCondBr(
                    _builder->CreateICmpUGT(copy_n_val, llvm::ConstantInt::get(i32_t, 0, false)),
                    copy_loop_bb, copy_done_bb);

                _builder->SetInsertPoint(copy_loop_bb);
                auto* copy_idx = _builder->CreatePHI(i32_t, 2, "copy_idx");
                copy_idx->addIncoming(llvm::ConstantInt::get(i32_t, 0, false), copy_entry_bb);

                llvm::Value* s_elem = _builder->CreateGEP(data_arr_llvm, src_data_ptr,
                    {_builder->getInt32(0), copy_idx}, "s_elem");
                llvm::Value* d_elem = _builder->CreateGEP(data_arr_llvm, dest_data_ptr,
                    {_builder->getInt32(0), copy_idx}, "d_elem");
                _builder->CreateStore(_builder->CreateLoad(elem_llvm, s_elem, "ev"), d_elem);

                auto* copy_next = _builder->CreateAdd(copy_idx,
                    llvm::ConstantInt::get(i32_t, 1, false), "copy_next");
                copy_idx->addIncoming(copy_next, copy_loop_bb);
                _builder->CreateCondBr(
                    _builder->CreateICmpULT(copy_next, copy_n_val),
                    copy_loop_bb, copy_done_bb);

                _builder->SetInsertPoint(copy_done_bb);
                _builder->CreateCondBr(
                    _builder->CreateICmpULT(copy_n_val, dest_n_val),
                    zero_loop_bb, init_done_bb);

                _builder->SetInsertPoint(zero_loop_bb);
                auto* zero_idx = _builder->CreatePHI(i32_t, 2, "zero_idx");
                zero_idx->addIncoming(copy_n_val, copy_done_bb);

                llvm::Value* z_elem = _builder->CreateGEP(data_arr_llvm, dest_data_ptr,
                    {_builder->getInt32(0), zero_idx}, "z_elem");
                _builder->CreateStore(llvm::Constant::getNullValue(elem_llvm), z_elem);

                auto* zero_next = _builder->CreateAdd(zero_idx,
                    llvm::ConstantInt::get(i32_t, 1, false), "zero_next");
                zero_idx->addIncoming(zero_next, zero_loop_bb);
                _builder->CreateCondBr(
                    _builder->CreateICmpULT(zero_next, dest_n_val),
                    zero_loop_bb, init_done_bb);

                _builder->SetInsertPoint(init_done_bb);

                // Store the address of our local struct into the reference variable's alloca
                _builder->CreateStore(dest_struct_alloca, alloca_ptr);
                object_ref = alloca_ptr;
            } else {
                // Plain reference (int&, struct&, etc.) — store the address of the referent
                _builder->CreateStore(_value, alloca_ptr);
                object_ref = alloca_ptr;
            }
        } else {
            throw_internal_error(0x001C, std::nullopt,
                "Internal error: reference variable '{}' has no initialisation argument; "
                "the resolver should have rejected this earlier",
                {var_def->get_fq_name()});
        }

    } else if (auto sized_arr_type = std::dynamic_pointer_cast<sized_array_type>(var_type)) {
        // Sized array value variable: int[N]
        // No explicit init — zero-initialise the entire struct, then set the count field.
        auto* struct_llvm = sized_arr_type->get_llvm_struct_type();
        if (!struct_llvm) {
            throw_internal_error(0x001D, std::nullopt,
                "Internal error: sized array variable '{}' has no LLVM struct type",
                {var_def->get_fq_name()});
        }
        // Zero-fill the entire struct { i32, [N x T] }
        _builder->CreateStore(llvm::ConstantAggregateZero::get(struct_llvm), object_ref);
        // Then write the element count N into field 0
        llvm::Value* size_field_ptr = _builder->CreateStructGEP(struct_llvm, object_ref,
            sized_array_type::FIELD_SIZE, "arr_size_ptr");
        _builder->CreateStore(
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(_builder->getContext()),
                sized_arr_type->get_size(), false),
            size_field_ptr);
        // Field 1 (data) is now zeroed — primitives are ready.
        // TODO: call default constructors for struct element types.
    } else if (type::is_function_reference(var_type)) {
        // Function-reference variable (*(T), ^(T), ~(T)): store the function pointer value.
        // The variable is an alloca of type ptr (opaque pointer).
        // After visiting the argument, _value is already the raw function pointer (ptr):
        // - if the arg is a symbol_expression resolving to a function, impl_gen sets _value=llvm_func directly.
        // - if the arg is a symbol_expression resolving to a function_reference_type variable,
        //   our modified visit_symbol_expression already loads the value from the alloca.
        if (!expr.empty()) {
            _value = nullptr;
            expr.argument(0)->accept(*this);
            if (_value) {
                _builder->CreateStore(_value, object_ref);
            }
        }
        _value = object_ref;

    } else {
        // TODO This is probably a non-primitive primary type, so direct construction will be done
    }

    // The result of a constructor invocation is the reference to the constructed object
    _value = object_ref;
}

//
// Cast expression
//

void type_reference_resolver::visit_cast_expression(cast_expression& expr) {
    auto sub_expr = expr.sub_expr();
    sub_expr->accept(*this);

    auto source_type = sub_expr->get_type();
    auto target_type = expr.get_cast_type();

    // ── Resolve the target type if it is not yet resolved ────────────────────
    // A `(Type)expr` cast produces an unresolved type for named types (struct/class/interface).
    // We must resolve it here before any validation.
    if (target_type && !type::is_resolved(target_type)) {
        // Step 1: try context::resolve_type — handles composite types (ptr<unresolved>, etc.)
        // and looks up structs in the context's _struct_types registry.
        auto resolved = _context->resolve_type(target_type);
        if (resolved && type::is_resolved(resolved)) {
            target_type = resolved;
            expr.set_cast_type(target_type);
        } else {
            // Step 2: for types not in context registry, use name-based resolution from root ns.
            // This handles types in namespaces or types that haven't been registered yet.
            auto resolve_by_name_composite = [&](const auto& self, const std::shared_ptr<type>& t) -> std::shared_ptr<type> {
                if (!t) return nullptr;
                if (type::is_resolved(t)) return t;
                if (auto unres = std::dynamic_pointer_cast<unresolved_type>(t)) {
                    if (auto already = unres->get_resolved()) return already;
                    auto root_ns = _unit.get_root_namespace();
                    if (root_ns) return resolve_type_by_name(unres->type_id(), *root_ns);
                    return nullptr;
                }
                auto sub = self(self, t->get_subtype());
                if (!sub || !type::is_resolved(sub)) return nullptr;
                if (type::is_pointer(t))   return sub->get_pointer();
                if (type::is_link(t))      return sub->get_link();
                if (type::is_pinned(t))    return sub->get_pinned();
                if (type::is_reference(t)) return sub->get_reference();
                if (type::is_const(t))     return sub->get_const();
                return nullptr;
            };
            auto resolved2 = resolve_by_name_composite(resolve_by_name_composite, target_type);
            if (resolved2 && type::is_resolved(resolved2)) {
                target_type = resolved2;
                expr.set_cast_type(target_type);
            } else {
                throw_error(0x40035, std::nullopt,
                    "Cannot resolve target type of explicit cast: '{}' is unknown in this scope",
                    {target_type ? target_type->to_string() : "?"});
            }
        }
    }

    if(source_type==target_type) {
        // TODO warn about useless casting
    } else {
        // ── Helper: extract struct_type from an indirection (lnk/pin/ptr) ────
        auto get_indir_struct = [](const std::shared_ptr<type>& t) -> std::shared_ptr<struct_type> {
            if (auto lnk = std::dynamic_pointer_cast<link_type>(t))
                return std::dynamic_pointer_cast<struct_type>(type::remove_const(lnk->get_linked_type()));
            if (auto pin = std::dynamic_pointer_cast<pinned_type>(t))
                return std::dynamic_pointer_cast<struct_type>(type::remove_const(pin->get_pinned_type()));
            if (auto ptr = std::dynamic_pointer_cast<pointer_type>(t))
                return std::dynamic_pointer_cast<struct_type>(type::remove_const(ptr->get_pointed_type()));
            return nullptr;
        };

        // ── Helper: is target a non-null indirection (lnk or ref) ────────────
        auto target_is_nonnull = [](const std::shared_ptr<type>& t) -> bool {
            return type::is_link(t) || type::is_reference(t);
        };

        // ── Unwrap ref<indir> source once ─────────────────────────────────────
        // Allows explicit casts like (Base*)(ref<ptr<Derived>>)
        auto effective_source = source_type;
        bool source_unwrapped_ref = false;
        if (type::is_reference(source_type)) {
            auto inner = std::dynamic_pointer_cast<reference_type>(source_type)->get_subtype();
            if (type::is_link(inner) || type::is_pinned(inner) || type::is_pointer(inner)) {
                effective_source = inner;
                source_unwrapped_ref = true;
            }
        }

        // ── Case: ptr/lnk/pin source → ptr/lnk/pin target ────────────────────
        if ((type::is_pointer(effective_source) || type::is_link(effective_source) || type::is_pinned(effective_source)) &&
            (type::is_pointer(target_type)       || type::is_link(target_type)       || type::is_pinned(target_type))) {

            auto src_st = get_indir_struct(effective_source);
            auto tgt_st_type = get_indir_struct(target_type);
            if (src_st && tgt_st_type) {
                auto src_agg = src_st->get_struct();
                auto tgt_agg = tgt_st_type->get_struct();
                if (src_agg && tgt_agg && src_agg != tgt_agg) {
                    if (src_agg->is_derived_from(tgt_agg)) {
                        // Static upcast: ptr/lnk/pin<Derived> → ptr/lnk/pin<Base>
                        // IR handles GEP; model-level: no load_value wrapping needed.
                        // null_is_fatal not needed for static upcast.
                    } else if (tgt_agg->is_derived_from(src_agg) &&
                               std::dynamic_pointer_cast<klass>(tgt_agg) != nullptr) {
                        // Dynamic downcast: ptr/lnk/pin<Base> → ptr/lnk/pin<Derived> (RTTI)
                        // Set null_is_fatal on the cast_expression for non-null targets.
                        expr.set_null_is_fatal(target_is_nonnull(target_type));
                    } else {
                        throw_error(0x40033, std::nullopt,
                            "Explicit cast: cannot cast from '{}' to '{}': "
                            "the pointed types have no inheritance relationship",
                            {source_type->to_string(), target_type->to_string()});
                    }
                }
                // Same struct type: allowed (e.g. ptr<T>→lnk<T>).
            }
            // If source or target does not point to a struct/class: allowed (opaque ptr reinterpret).
        }

        // ── Case: ptr/lnk/pin/ref source → bool target ────────────────────────
        else if (type::is_pointer(effective_source) && type::is_prim_bool(target_type)) {
            // Pointer-to-bool: valid (null check). No model transformation needed.
        }

        // ── Case: ref<Struct> → ref<Struct> (same handling as implicit upcast/downcast) ──
        else if (type::is_reference(source_type) && type::is_reference(target_type)) {
            // Struct reference upcast: ref<Derived> → ref<Base>
            // or dynamic downcast: ref<Base> → ref<Derived> (fatal if RTTI mismatch)
            auto src_ref = std::dynamic_pointer_cast<reference_type>(source_type);
            auto tgt_ref = std::dynamic_pointer_cast<reference_type>(target_type);
            auto src_sub_nc = type::remove_const(src_ref->get_referenced_type());
            auto tgt_sub_nc = type::remove_const(tgt_ref->get_referenced_type());
            auto src_st_type = std::dynamic_pointer_cast<struct_type>(src_sub_nc);
            auto tgt_st_type = std::dynamic_pointer_cast<struct_type>(tgt_sub_nc);
            if (src_st_type && tgt_st_type && src_st_type != tgt_st_type) {
                auto src_agg = src_st_type->get_struct();
                auto tgt_agg = tgt_st_type->get_struct();
                if (src_agg && tgt_agg) {
                    if (src_agg->is_derived_from(tgt_agg)) {
                        // Static upcast ref<Derived>→ref<Base>: handled by IR GEP.
                    } else if (tgt_agg->is_derived_from(src_agg) &&
                               std::dynamic_pointer_cast<klass>(tgt_agg) != nullptr) {
                        // Dynamic downcast ref<Base>→ref<Derived>: ref is non-null → fatal.
                        expr.set_null_is_fatal(true);
                    } else {
                        throw_error(0x40034, std::nullopt,
                            "Explicit cast: cannot cast reference from '{}' to '{}': "
                            "the referenced types have no inheritance relationship",
                            {source_type->to_string(), target_type->to_string()});
                    }
                }
            }
            // Keep as-is (no load_value replacement).
        }

        // ── Case: ref<Struct/Prim> → value (load) ──────────────────────────────
        else if(type::is_reference(source_type)) {
            // ref<T> → T : wrap in load_value
            auto deref = load_value_expression::make_shared(sub_expr->shared_as<expression>());
            expr.assign(deref);
            deref->set_type(source_type->get_subtype());
        }
    }


    expr.set_type(expr.get_cast_type());
}

void implementation_generator::visit_cast_expression(cast_expression& expr) {
    auto source_type = expr.sub_expr()->get_type();
    auto target_type = expr.get_cast_type();

    if(!source_type->is_resolved() || !target_type->is_resolved()) {
        throw_internal_error(0x0019, std::nullopt,
            "Internal error: cast expression has an unresolved source or target type; "
            "type resolution must complete before code generation");
    }

    // ── ref<T> → link<T> or ref<T> → pin<T>: no-op (same LLVM ptr) ────────────
    if (type::is_reference(source_type) &&
        (type::is_link(target_type) || type::is_pinned(target_type))) {
        auto src_sub = type::remove_const(std::dynamic_pointer_cast<reference_type>(source_type)->get_subtype());
        auto tgt_sub = type::remove_const(target_type->get_subtype());
        if (src_sub == tgt_sub) {
            // ref<T> and link<T>/pin<T> are both LLVM pointers — no IR conversion needed.
            _value = nullptr;
            expr.sub_expr()->accept(*this);
            return;
        }
    }

    // ── Struct reference upcast: ref<Derived> → ref<Base> ────────────────────
    // Both source and target are references to struct types. We need to GEP to the
    // base subobject field within the derived struct.
    if (type::is_reference(source_type) && type::is_reference(target_type)) {
        auto src_ref = std::dynamic_pointer_cast<reference_type>(source_type);
        auto tgt_ref = std::dynamic_pointer_cast<reference_type>(target_type);
        auto src_st_type = std::dynamic_pointer_cast<struct_type>(type::remove_const(src_ref->get_referenced_type()));
        auto tgt_st_type = std::dynamic_pointer_cast<struct_type>(type::remove_const(tgt_ref->get_referenced_type()));
        if (src_st_type && tgt_st_type && src_st_type != tgt_st_type) {
            auto src_st = src_st_type->get_struct();
            auto tgt_st = tgt_st_type->get_struct();
            if (src_st && tgt_st && src_st->is_derived_from(tgt_st)) {
                // ── Static upcast: ref<Derived> → ref<Base> — GEP to base subobject ──
                _value = nullptr;
                expr.sub_expr()->accept(*this);
                if (!_value) return;

                // Find the base subobject field index in the derived struct
                // The base subobject is stored as "__base_<name>__" or "__vbase_<name>__" member
                std::string subobj_name;
                for (auto& bs : src_st->get_bases()) {
                    if (bs.base && bs.base.get() == tgt_st.get()) {
                        subobj_name = bs.is_virtual
                            ? "__vbase_" + bs.sanitised_name() + "__"
                            : "__base_" + bs.sanitised_name() + "__";
                        break;
                    }
                }
                // Also check for __vbase_ in the most-derived class (transitively virtual)
                if (subobj_name.empty()) {
                    std::string vbase_name = "__vbase_" + tgt_st->get_short_name() + "__";
                    if (src_st_type->get_member(vbase_name)) {
                        subobj_name = vbase_name;
                    }
                }
                if (!subobj_name.empty()) {
                    auto src_llvm_type = _context->get_llvm_type(src_st_type);
                    if (auto field = src_st_type->get_member(subobj_name)) {
                        _value = _builder->CreateStructGEP(
                            src_llvm_type,
                            _value,
                            (unsigned)field->index,
                            "base_" + tgt_st->get_short_name() + "_ptr"
                        );
                        return;
                    }
                }

                // ── Transitive upcast: tgt_st is a transitive (non-direct) base of src_st.
                if (subobj_name.empty()) {
                    std::function<bool(aggregate*, struct_type*, llvm::Value*)> dfs_gep;
                    dfs_gep = [&](aggregate* cur_agg, struct_type* cur_st_type, llvm::Value* cur_ptr) -> bool {
                        for (auto& bs : cur_agg->get_bases()) {
                            if (!bs.base || bs.is_virtual) continue;
                            std::string field_name = "__base_" + bs.sanitised_name() + "__";
                            auto field = cur_st_type->get_member(field_name);
                            if (!field) continue;

                            auto base_agg = bs.base;
                            auto base_st_type = base_agg->get_struct_type();
                            if (!base_st_type) continue;

                            llvm::Type* cur_llvm_type = cur_st_type->get_llvm_type();
                            if (!cur_llvm_type) continue;
                            llvm::Value* base_ptr = _builder->CreateStructGEP(
                                cur_llvm_type, cur_ptr, (unsigned)field->index,
                                "trans_base_" + bs.sanitised_name() + "_ptr");

                            if (bs.base.get() == tgt_st.get()) {
                                _value = base_ptr;
                                return true;
                            }

                            // Check if tgt_st is a direct __vbase_ of this intermediate
                            std::string vbase_name2 = "__vbase_" + tgt_st->get_short_name() + "__";
                            if (auto vbase_field2 = base_st_type->get_member(vbase_name2)) {
                                llvm::Type* inter_llvm_type = base_st_type->get_llvm_type();
                                if (!inter_llvm_type) continue;
                                _value = _builder->CreateStructGEP(
                                    inter_llvm_type, base_ptr, (unsigned)vbase_field2->index,
                                    "trans_vbase_" + tgt_st->get_short_name() + "_ptr");
                                return true;
                            }

                            if (dfs_gep(bs.base.get(), base_st_type.get(), base_ptr)) {
                                return true;
                            }
                        }
                        return false;
                    };
                    if (dfs_gep(src_st.get(), src_st_type.get(), _value)) return;
                }

                // Virtual base via vbptr
                {
                    std::string vbptr_name = "__vbptr_" + tgt_st->get_short_name() + "__";
                    auto src_llvm_type = _context->get_llvm_type(src_st_type);
                    if (auto vbptr_field = src_st_type->get_member(vbptr_name)) {
                        llvm::Type* ptr_ty = llvm::PointerType::get(_context->llvm_context(), 0);
                        llvm::Value* vbptr_addr = _builder->CreateStructGEP(
                            src_llvm_type, _value, (unsigned)vbptr_field->index,
                            "vbptr_" + tgt_st->get_short_name() + "_addr");
                        _value = _builder->CreateLoad(ptr_ty, vbptr_addr,
                            "vbase_" + tgt_st->get_short_name() + "_ptr");
                        return;
                    }
                }
                // Fallback: return as-is (pointer reinterpret for same-layout case)
                return;
            }
            // If tgt_st is derived from src_st (dynamic downcast ref<Base>→ref<Derived>),
            // fall through to the dynamic cast block below — do NOT handle here.
            // Only do a no-op for truly same/unrelated types.
            bool is_dynamic_downcast = src_st && tgt_st &&
                tgt_st->is_derived_from(src_st) &&
                std::dynamic_pointer_cast<klass>(tgt_st) != nullptr;
            if (!is_dynamic_downcast) {
                // Same type or unrelated: no-op
                _value = nullptr;
                expr.sub_expr()->accept(*this);
                return;
            }
            // else: fall through to dynamic cast block
        } else {
            // Same type, no-op
            _value = nullptr;
            expr.sub_expr()->accept(*this);
            return;
        }
    }
    // For lien~, pin^, ptr* pointing to Derived → lien~, pin^, ptr* pointing to Base.
    // All are LLVM opaque pointers; same GEP strategy applies.
    // Also handles ref<ptr<Derived>>→lien<Base> etc. (load through ref first).
    {
        // Determine source and target struct_type from indirection kind
        std::shared_ptr<struct_type> indir_src_st_type, indir_tgt_st_type;
        bool is_indir_upcast = false;
        bool src_needs_load = false; // source is ref<indirection>

        auto get_indir_pointed = [](const std::shared_ptr<type>& t) -> std::shared_ptr<struct_type> {
            if (auto lnk = std::dynamic_pointer_cast<link_type>(t))
                return std::dynamic_pointer_cast<struct_type>(type::remove_const(lnk->get_linked_type()));
            if (auto pin = std::dynamic_pointer_cast<pinned_type>(t))
                return std::dynamic_pointer_cast<struct_type>(type::remove_const(pin->get_pinned_type()));
            if (auto ptr = std::dynamic_pointer_cast<pointer_type>(t))
                return std::dynamic_pointer_cast<struct_type>(type::remove_const(ptr->get_pointed_type()));
            return nullptr;
        };

        // Effective source: if ref<indirection>, unwrap ref for type checks (load needed)
        auto effective_source = source_type;
        if (type::is_reference(source_type)) {
            auto inner = std::dynamic_pointer_cast<reference_type>(source_type)->get_subtype();
            if (type::is_link(inner) || type::is_pinned(inner) || type::is_pointer(inner)) {
                effective_source = inner;
                src_needs_load = true;
            }
        }

        bool src_is_indir = type::is_link(effective_source) || type::is_pinned(effective_source) || type::is_pointer(effective_source);
        bool tgt_is_indir = type::is_link(target_type) || type::is_pinned(target_type) || type::is_pointer(target_type);
        if (src_is_indir && tgt_is_indir) {
            indir_src_st_type = get_indir_pointed(effective_source);
            indir_tgt_st_type = get_indir_pointed(target_type);
            if (indir_src_st_type && indir_tgt_st_type && indir_src_st_type != indir_tgt_st_type) {
                auto src_st = indir_src_st_type->get_struct();
                auto tgt_st = indir_tgt_st_type->get_struct();
                if (src_st && tgt_st && src_st->is_derived_from(tgt_st)) {
                    is_indir_upcast = true;
                }
            }
        }

        if (is_indir_upcast) {
            auto src_st_type = indir_src_st_type;
            auto tgt_st = indir_tgt_st_type->get_struct();
            auto src_st = src_st_type->get_struct();

            _value = nullptr;
            expr.sub_expr()->accept(*this);
            if (!_value) return;

            // If source was ref<indirection>, load the pointer value first
            if (src_needs_load) {
                _value = _builder->CreateLoad(
                    _context->get_llvm_type(effective_source), _value, "indir_upcast_load");
            }

            // Same GEP strategy as ref<Derived>→ref<Base>
            std::string subobj_name;
            for (auto& bs : src_st->get_bases()) {
                if (bs.base && bs.base.get() == tgt_st.get()) {
                    subobj_name = bs.is_virtual
                        ? "__vbase_" + bs.sanitised_name() + "__"
                        : "__base_" + bs.sanitised_name() + "__";
                    break;
                }
            }
            if (subobj_name.empty()) {
                std::string vbase_name = "__vbase_" + tgt_st->get_short_name() + "__";
                if (src_st_type->get_member(vbase_name)) {
                    subobj_name = vbase_name;
                }
            }
            if (!subobj_name.empty()) {
                auto src_llvm_type = _context->get_llvm_type(src_st_type);
                if (auto field = src_st_type->get_member(subobj_name)) {
                    _value = _builder->CreateStructGEP(
                        src_llvm_type, _value, (unsigned)field->index,
                        "base_" + tgt_st->get_short_name() + "_ptr");
                    return;
                }
            }
            // Transitive upcast via DFS
            std::function<bool(aggregate*, struct_type*, llvm::Value*)> dfs_gep;
            dfs_gep = [&](aggregate* cur_agg, struct_type* cur_st_type, llvm::Value* cur_ptr) -> bool {
                for (auto& bs : cur_agg->get_bases()) {
                    if (!bs.base || bs.is_virtual) continue;
                    std::string field_name = "__base_" + bs.sanitised_name() + "__";
                    auto field = cur_st_type->get_member(field_name);
                    if (!field) continue;
                    // Use aggregate directly (works for both structure and klass/interface)
                    auto base_agg = bs.base;
                    auto base_st_type = base_agg->get_struct_type();
                    if (!base_st_type) continue;
                    llvm::Type* cur_llvm_type = cur_st_type->get_llvm_type();
                    if (!cur_llvm_type) continue;
                    llvm::Value* base_ptr = _builder->CreateStructGEP(
                        cur_llvm_type, cur_ptr, (unsigned)field->index,
                        "trans_base_" + bs.sanitised_name() + "_ptr");
                    if (bs.base.get() == tgt_st.get()) {
                        _value = base_ptr;
                        return true;
                    }
                    std::string vbase_name2 = "__vbase_" + tgt_st->get_short_name() + "__";
                    if (auto vbase_field2 = base_st_type->get_member(vbase_name2)) {
                        llvm::Type* inter_llvm_type = base_st_type->get_llvm_type();
                        if (!inter_llvm_type) continue;
                        _value = _builder->CreateStructGEP(
                            inter_llvm_type, base_ptr, (unsigned)vbase_field2->index,
                            "trans_vbase_" + tgt_st->get_short_name() + "_ptr");
                        return true;
                    }
                    if (dfs_gep(bs.base.get(), base_st_type.get(), base_ptr)) return true;
                }
                return false;
            };
            if (dfs_gep(src_st.get(), src_st_type.get(), _value)) return;

            // Virtual base via vbptr
            {
                std::string vbptr_name = "__vbptr_" + tgt_st->get_short_name() + "__";
                auto src_llvm_type = _context->get_llvm_type(src_st_type);
                if (auto vbptr_field = src_st_type->get_member(vbptr_name)) {
                    llvm::Type* ptr_ty = llvm::PointerType::get(_context->llvm_context(), 0);
                    llvm::Value* vbptr_addr = _builder->CreateStructGEP(
                        src_llvm_type, _value, (unsigned)vbptr_field->index,
                        "vbptr_" + tgt_st->get_short_name() + "_addr");
                    _value = _builder->CreateLoad(ptr_ty, vbptr_addr,
                        "vbase_" + tgt_st->get_short_name() + "_ptr");
                    return;
                }
            }
            // Fallback: return as-is
            return;
        }
    }

    // ── Struct reference → pointer upcast for virtual base vbptr deref ────────
    // When the type resolver sets target type to pointer<VirtualBase>, it means we need
    // to load the __vbptr_<name>__ field and use it as a pointer (which the subsequent
    // GEP in member_of_object_expression will use).
    // ── Struct reference → pointer for virtual base vbptr deref ─────────────
    // Only applies when source is ref<StructType> (not ref<ptr/lnk/pin>).
    // When source referenced type is itself an indirection (e.g. ref<ptr<Base>>→ptr<Derived>),
    // fall through to the dynamic cast block below.
    if (type::is_reference(source_type) && type::is_pointer(target_type)) {
        auto src_ref = std::dynamic_pointer_cast<reference_type>(source_type);
        auto src_inner = src_ref->get_referenced_type();
        // Only handle this block when source references a struct directly (not a ptr/lnk/pin).
        // If source is ref<ptr/lnk/pin>, fall through to dynamic cast.
        if (!type::is_any_indirection(src_inner)) {
            auto tgt_ptr = std::dynamic_pointer_cast<pointer_type>(target_type);
            auto src_st_type = std::dynamic_pointer_cast<struct_type>(type::remove_const(src_inner));
            auto tgt_st_type = std::dynamic_pointer_cast<struct_type>(tgt_ptr->get_pointed_type());
            if (src_st_type && tgt_st_type) {
                // Check if this is a dynamic downcast (Base→Derived): if so, fall through
                auto src_agg = src_st_type->get_struct();
                auto tgt_agg = tgt_st_type->get_struct();
                bool is_dynamic_downcast = src_agg && tgt_agg &&
                    tgt_agg->is_derived_from(src_agg) &&
                    std::dynamic_pointer_cast<klass>(tgt_agg) != nullptr;
                if (!is_dynamic_downcast) {
                    _value = nullptr;
                    expr.sub_expr()->accept(*this);
                    if (!_value) return;

                    std::string vbptr_name = "__vbptr_" + tgt_st_type->name() + "__";
                    auto src_llvm_type = _context->get_llvm_type(src_st_type);
                    if (auto field = src_st_type->get_member(vbptr_name)) {
                        // Load the vbptr field (it's an opaque ptr to the virtual base)
                        llvm::Type* ptr_ty = llvm::PointerType::get(_context->llvm_context(), 0);
                        llvm::Value* vbptr_field_addr = _builder->CreateStructGEP(
                            src_llvm_type, _value, (unsigned)field->index, "vbptr_" + tgt_st_type->name() + "_addr");
                        _value = _builder->CreateLoad(ptr_ty, vbptr_field_addr, "vbptr_" + tgt_st_type->name());
                        return;
                    }
                    // Fallback for non-dynamic ref<Struct>→ptr<Struct> (no vbptr found)
                    return;
                }
                // is_dynamic_downcast → fall through to dynamic cast block
            }
            // src_st_type or tgt_st_type null → fall through
        }
        // ref<ptr/lnk/pin> → fall through to dynamic cast block
    }

    if(type::is_pointer(source_type) && type::is_prim_bool(target_type)) {
        // TODO add pointer to boolean casting
    }

    // ── Dynamic cast (RTTI-based): Base→Derived for klass/interface indirections ──
    // Triggered when target_st is derived from source_st (i.e. going "upward" in the
    // type hierarchy from a base pointer to a more-derived pointer).
    // This is the inverse of the static upcast handled above.
    {
        // Helper: extract struct_type from any indirection or ref<indirection> or ref<struct>
        auto get_pointed_struct = [](const std::shared_ptr<type>& t) -> std::shared_ptr<struct_type> {
            auto effective = t;
            if (auto ref = std::dynamic_pointer_cast<reference_type>(t)) {
                auto inner = ref->get_referenced_type();
                if (type::is_any_indirection(inner)) effective = inner;
                else return std::dynamic_pointer_cast<struct_type>(type::remove_const(inner));
            }
            if (auto lnk = std::dynamic_pointer_cast<link_type>(effective))
                return std::dynamic_pointer_cast<struct_type>(type::remove_const(lnk->get_linked_type()));
            if (auto pin = std::dynamic_pointer_cast<pinned_type>(effective))
                return std::dynamic_pointer_cast<struct_type>(type::remove_const(pin->get_pinned_type()));
            if (auto ptr = std::dynamic_pointer_cast<pointer_type>(effective))
                return std::dynamic_pointer_cast<struct_type>(type::remove_const(ptr->get_pointed_type()));
            return nullptr;
        };
        auto src_st_type = get_pointed_struct(source_type);
        auto tgt_st_type = get_pointed_struct(target_type);
        if (src_st_type && tgt_st_type) {
            auto src_st = src_st_type->get_struct();
            auto tgt_st = tgt_st_type->get_struct();
            bool is_dynamic = src_st && tgt_st &&
                tgt_st->is_derived_from(src_st) &&
                std::dynamic_pointer_cast<klass>(tgt_st) != nullptr;
            if (is_dynamic) {
                emit_dynamic_cast(expr, src_st_type, tgt_st_type);
                return;
            }
        }
    }

    if(!type::is_primitive(source_type) || !type::is_primitive(target_type)) {
        throw_error(0x001A, std::nullopt,
            "Casting between non-primitive types is not yet supported: "
            "cannot cast from '{}' to '{}'; only casts between primitive types are currently implemented",
            {source_type->to_string(), target_type->to_string()});
    }
    auto src = std::dynamic_pointer_cast<primitive_type>(source_type);
    auto tgt = std::dynamic_pointer_cast<primitive_type>(target_type);

    _value = nullptr;
    expr.sub_expr()->accept(*this);
    if(!_value) {
        throw_internal_error(0x001A, std::nullopt,
            "Internal error: the expression being cast produced no LLVM value; "
            "this indicates a code-generation bug in the sub-expression");
    }

    if(src->is_boolean()) {
        if(tgt->is_integer()) {
            if(tgt->is_unsigned()) {
                _value = _builder->CreateZExt(_value, _builder->getIntNTy(tgt->type_size()));
            } else {
                _value = _builder->CreateSExt(_value, _builder->getIntNTy(tgt->type_size()));
            }
        } else if (tgt->is_float()) {
            if(*tgt == primitive_type::FLOAT) {
                auto ftype = _context->get_llvm_type(tgt);
                auto ftrue = llvm::ConstantFP::get(ftype, llvm::APFloat(1.0f));
                auto ffalse = llvm::ConstantFP::get(ftype, llvm::APFloat(0.0f));
                _value = _builder->CreateSelect(_value, ftrue, ffalse);
            } else if(*tgt == primitive_type::DOUBLE) {
                auto dtype = _context->get_llvm_type(tgt);
                auto dtrue = llvm::ConstantFP::get(dtype, llvm::APFloat(1.0));
                auto dfalse = llvm::ConstantFP::get(dtype, llvm::APFloat(0.0));
                _value = _builder->CreateSelect(_value, dtrue, dfalse);
            } // else must not happen
        } else {
            // Support other types
        }
    } else if(src->is_integer()) {
        if(tgt->is_boolean()) {
            _value = _builder->CreateICmpNE(_value, _builder->getIntN(src->type_size(), 0));
        } else if (tgt->is_integer()) {
            if (tgt->is_signed()) {
                if (src->is_unsigned()) {
                    auto d = k::log::diagnostic::make_warning(with_flag(0x001C),
                        "Casting an unsigned integer to a signed integer of the same size may produce "
                        "unexpected results if the value exceeds the signed range (overflow is implementation-defined)");
                    report(d);
                }
                // SExt or trunc for signed integers
                _value = _builder->CreateSExtOrTrunc(_value, _context->get_llvm_type(tgt));
            } else /* if (tgt->is_unsigned())*/  {
                if (src->is_signed()) {
                    auto d = k::log::diagnostic::make_warning(with_flag(0x001D),
                        "Casting a signed integer to an unsigned integer may reinterpret negative values "
                        "as large positive values (two's complement wrap-around)");
                    report(d);
                }
                // ZExt or trunc for unsigned integers
                _value = _builder->CreateZExtOrTrunc(_value, _context->get_llvm_type(tgt));
            }
        } else if (tgt->is_float()) {
            if(src->is_unsigned()) {
                if(*tgt == primitive_type::FLOAT) {
                    _value = _builder->CreateUIToFP(_value, _builder->getFloatTy());
                } else if(*tgt == primitive_type::DOUBLE) {
                    _value = _builder->CreateUIToFP(_value, _builder->getDoubleTy());
                } /* else must not happen */
            } else {
                if(*tgt == primitive_type::FLOAT) {
                    _value = _builder->CreateSIToFP(_value, _builder->getFloatTy());
                } else if(*tgt == primitive_type::DOUBLE) {
                    _value = _builder->CreateSIToFP(_value, _builder->getDoubleTy());
                } /* else must not happen */
            }
        } else {
            // Support other types
        }
    } else if(src->is_float()) {
        if(tgt->is_boolean()) {
            _value = _builder->CreateFCmpUNE(_value, llvm::ConstantFP::get(_context->get_llvm_type(tgt), 0.0));
        } else if(tgt->is_integer()) {
            if(tgt->is_unsigned()) {
                _value = _builder->CreateFPToUI(_value, _context->get_llvm_type(tgt));
            } else {
                _value = _builder->CreateFPToSI(_value, _context->get_llvm_type(tgt));
            }
        } else if(tgt->is_float()) {
            if(*src == primitive_type::FLOAT && *tgt == primitive_type::DOUBLE) {
                _value = _builder->CreateFPExt(_value, _context->get_llvm_type(tgt));
            } else if(*src == primitive_type::DOUBLE && *tgt == primitive_type::FLOAT) {
                _value = _builder->CreateFPTrunc(_value, _context->get_llvm_type(tgt));
            } else {
                // Do nothing, float type is already aligned
            }
        } else{
            // Support other types
        }
    } else {
        // Support other types
    }
}


// ─── Dynamic cast (RTTI-based): emit_dynamic_cast ────────────────────────────
//
// Called from visit_cast_expression when types require a runtime RTTI check.
// source is a base-typed indirection; target is a derived-typed indirection.
// Algorithm:
//  1. Evaluate source → raw base pointer.
//  2. Load the vptr (field 0 of the base klass layout).
//  3. Load vtable[0] → actual RTTI pointer of the most-derived object.
//  4. Compare with the RTTI global of the target klass.
//  5. On match: subtract compile-time byte-offset → Derived* result.
//  6. On mismatch: null.
//  7. If expr.null_is_fatal(): emit debugtrap on null (target is lnk or ref).
//
void implementation_generator::emit_dynamic_cast(
        cast_expression& expr,
        std::shared_ptr<struct_type> src_st_type,
        std::shared_ptr<struct_type> tgt_st_type)
{
    auto& llvm_ctx  = _builder->getContext();
    auto* ptr_ty    = llvm::PointerType::get(llvm_ctx, 0);
    auto* i64_ty    = llvm::Type::getInt64Ty(llvm_ctx);

    auto source_type = expr.sub_expr()->get_type();

    auto src_st = src_st_type->get_struct();
    auto tgt_st = tgt_st_type->get_struct();
    auto tgt_klass = std::dynamic_pointer_cast<klass>(tgt_st);

    if (!src_st || !tgt_st || !tgt_klass) {
        throw_internal_error(0x0026, std::nullopt,
            "emit_dynamic_cast: source or target is not a class/interface aggregate");
    }

    // ── 1. Evaluate sub-expression → raw base pointer ────────────────────────
    _value = nullptr;
    expr.sub_expr()->accept(*this);
    if (!_value) return;
    llvm::Value* base_raw = _value;

    // If source is ref<lnk/pin/ptr>, load the stored pointer value.
    {
        if (auto ref_t = std::dynamic_pointer_cast<reference_type>(source_type)) {
            auto inner = ref_t->get_referenced_type();
            if (type::is_any_indirection(inner)) {
                base_raw = _builder->CreateLoad(ptr_ty, base_raw, "dyncast_load_indir");
            }
        }
    }

    // ── 2. Find the RTTI global for the target class ──────────────────────────
    std::string rtti_name = mangler::mangle_rtti(tgt_klass->get_name());
    llvm::GlobalVariable* tgt_rtti_gv = _context->module().getNamedGlobal(rtti_name);
    if (!tgt_rtti_gv) {
        throw_internal_error(0x0027, std::nullopt,
            "emit_dynamic_cast: RTTI global '{}' not found in module",
            {rtti_name});
    }

    // ── 3. Load the vptr from the source object (field 0 of the klass) ───────
    auto src_klass = std::dynamic_pointer_cast<klass>(src_st);
    if (!src_klass || !src_klass->has_vtable()) {
        throw_internal_error(0x0028, std::nullopt,
            "emit_dynamic_cast: source class '{}' has no vtable/vptr",
            {src_st->get_short_name()});
    }
    auto src_vt = src_klass->get_vtable();
    auto* src_llvm_type = src_st_type->get_llvm_type();
    if (!src_llvm_type || !src_vt->llvm_type) {
        throw_internal_error(0x0029, std::nullopt,
            "emit_dynamic_cast: source class LLVM type not built");
    }
    llvm::Value* vptr_addr = _builder->CreateStructGEP(
        src_llvm_type, base_raw, 0, "dyncast_vptr_addr");
    llvm::Value* vptr = _builder->CreateLoad(ptr_ty, vptr_addr, "dyncast_vptr");

    // ── 4. Load vtable[0] → actual RTTI pointer ──────────────────────────────
    llvm::Value* rtti_slot_addr = _builder->CreateStructGEP(
        src_vt->llvm_type, vptr, 0, "dyncast_rtti_slot_addr");
    llvm::Value* actual_rtti = _builder->CreateLoad(ptr_ty, rtti_slot_addr, "dyncast_actual_rtti");

    // ── 5. Compare RTTI pointers ──────────────────────────────────────────────
    llvm::Value* rtti_match = _builder->CreateICmpEQ(
        actual_rtti, tgt_rtti_gv, "dyncast_rtti_match");

    // ── 6. Compute adjusted pointer (Derived* = Base* − byte_offset) ─────────
    llvm::Value* derived_ptr = nullptr;
    {
        llvm::DataLayout dl(&_context->module());

        // Try direct base first
        std::string subobj_name;
        for (auto& bs : tgt_st->get_bases()) {
            if (!bs.base) continue;
            if (bs.base.get() == src_st.get()) {
                subobj_name = bs.is_virtual
                    ? "__vbase_" + bs.sanitised_name() + "__"
                    : "__base_" + bs.sanitised_name() + "__";
                break;
            }
        }
        if (subobj_name.empty()) {
            std::string vbase_name = "__vbase_" + src_st->get_short_name() + "__";
            if (tgt_st_type->get_member(vbase_name)) subobj_name = vbase_name;
        }
        if (!subobj_name.empty()) {
            if (auto field = tgt_st_type->get_member(subobj_name)) {
                auto* tgt_llvm_type = llvm::dyn_cast_or_null<llvm::StructType>(tgt_st_type->get_llvm_type());
                if (tgt_llvm_type) {
                    uint64_t off = dl.getStructLayout(tgt_llvm_type)->getElementOffset((unsigned)field->index);
                    llvm::Value* bi = _builder->CreatePtrToInt(base_raw, i64_ty, "dyncast_base_int");
                    llvm::Value* di = _builder->CreateSub(bi, llvm::ConstantInt::get(i64_ty, off), "dyncast_derived_int");
                    derived_ptr = _builder->CreateIntToPtr(di, ptr_ty, "dyncast_derived_ptr");
                }
            }
        }
        if (!derived_ptr) {
            // Transitive DFS: accumulate byte offsets through hierarchy
            std::function<int64_t(aggregate*, struct_type*)> dfs_offset;
            dfs_offset = [&](aggregate* cur_agg, struct_type* cur_st_type) -> int64_t {
                for (auto& bs : cur_agg->get_bases()) {
                    if (!bs.base || bs.is_virtual) continue;
                    std::string fname = "__base_" + bs.sanitised_name() + "__";
                    auto field = cur_st_type->get_member(fname);
                    if (!field) continue;
                    auto* cur_llvm = llvm::dyn_cast_or_null<llvm::StructType>(cur_st_type->get_llvm_type());
                    if (!cur_llvm) continue;
                    uint64_t this_off = dl.getStructLayout(cur_llvm)->getElementOffset((unsigned)field->index);
                    if (bs.base.get() == src_st.get()) return (int64_t)this_off;
                    auto base_st_type = bs.base->get_struct_type();
                    if (!base_st_type) continue;
                    int64_t inner = dfs_offset(bs.base.get(), base_st_type.get());
                    if (inner >= 0) return (int64_t)this_off + inner;
                }
                return -1;
            };
            int64_t total = dfs_offset(tgt_st.get(), tgt_st_type.get());
            if (total >= 0) {
                llvm::Value* bi2 = _builder->CreatePtrToInt(base_raw, i64_ty);
                llvm::Value* di2 = _builder->CreateSub(bi2, llvm::ConstantInt::get(i64_ty, (uint64_t)total), "dyncast_trans_int");
                derived_ptr = _builder->CreateIntToPtr(di2, ptr_ty, "dyncast_trans_ptr");
            }
        }
    }
    if (!derived_ptr) derived_ptr = base_raw; // degenerate: same address

    // ── 7. Select result: derived_ptr on match, null on mismatch ─────────────
    auto* null_val = llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ptr_ty));
    llvm::Value* result = _builder->CreateSelect(rtti_match, derived_ptr, null_val, "dyncast_result");

    // ── 8. Fatal-null check for lnk/ref targets ───────────────────────────────
    if (expr.null_is_fatal()) {
        auto* fatal_fn = get_or_declare_fatal_null_function("__fatal_null_dyncast");
        emit_null_check(result, fatal_fn, "dyncast");
    }

    _value = result;
}

//
// Fatal null helpers
//

llvm::Function* implementation_generator::get_or_declare_fatal_null_function(const std::string& name) {
    llvm::Module& mod = get_module();
    if (auto* existing = mod.getFunction(name)) {
        return existing;
    }
    auto& llvm_ctx = mod.getContext();
    auto* void_ty  = llvm::Type::getVoidTy(llvm_ctx);
    auto* fn_type  = llvm::FunctionType::get(void_ty, false);
    auto* fn = llvm::Function::Create(fn_type, llvm::Function::ExternalLinkage, name, mod);
    fn->addFnAttr(llvm::Attribute::NoReturn);
    fn->addFnAttr(llvm::Attribute::NoUnwind);
    fn->addFnAttr(llvm::Attribute::Cold);
    auto* entry = llvm::BasicBlock::Create(llvm_ctx, "entry", fn);
    llvm::IRBuilder<> b(entry);
#ifdef NDEBUG
    auto* trap_fn = llvm::Intrinsic::getDeclaration(&mod, llvm::Intrinsic::trap);
#else
    auto* trap_fn = llvm::Intrinsic::getDeclaration(&mod, llvm::Intrinsic::debugtrap);
#endif
    b.CreateCall(trap_fn, {});
    b.CreateUnreachable();
    return fn;
}

void implementation_generator::emit_null_check(llvm::Value* ptr_value, llvm::Function* fatal_fn, const std::string& label) {
    auto* fn   = _builder->GetInsertBlock()->getParent();
    auto& ctx  = _builder->getContext();
    auto* ptr_ty = llvm::PointerType::get(ctx, 0);
    auto* null_bb = llvm::BasicBlock::Create(ctx, label + "_null", fn);
    auto* ok_bb   = llvm::BasicBlock::Create(ctx, label + "_ok",   fn);
    auto* is_null = _builder->CreateICmpEQ(
        ptr_value, llvm::ConstantPointerNull::get(ptr_ty), label + "_is_null");
    _builder->CreateCondBr(is_null, null_bb, ok_bb);
    _builder->SetInsertPoint(null_bb);
    _builder->CreateCall(fatal_fn, {});
    _builder->CreateUnreachable();
    _builder->SetInsertPoint(ok_bb);
}


} // namespace k::model::gen
