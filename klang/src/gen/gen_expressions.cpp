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
#include "../parse/ast.hpp"
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
// Forward declaration for operator dispatch helper defined in gen_operators.cpp
virtual_dispatch_info compute_operator_dispatch_info(
    const std::shared_ptr<function>& func,
    const std::shared_ptr<type>& receiver_type);
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
        // Symbol not found as variable/function.
        // Try enum entry resolution: EnumName::entryName or module::EnumName::entryName
        bool resolved_as_enum = false;
        const auto& sym_name = symbol.get_name();
        if (sym_name.size() >= 2 && !sym_name.has_root_prefix()) {
            // The entry name is the last component; the enum name is everything before it
            const std::string& entry_name = sym_name.back();
            std::shared_ptr<enumeration> found_enum;

            if (sym_name.size() == 2) {
                // Simple: EnumName::entryName — walk up the scope chain
                for (auto current = symbol.shared_as<element>(); current; current = current->parent<element>()) {
                    if (auto eh = std::dynamic_pointer_cast<enum_holder>(current)) {
                        if (auto en = eh->get_enum(sym_name.front())) {
                            found_enum = en;
                            break;
                        }
                    }
                }
            }

            // If not found locally, try imported enums (works for both 2-part and N-part names)
            if (!found_enum) {
                // Build a k::name from all parts except the last (the entry name)
                std::vector<std::string> enum_parts(sym_name.parts().begin(),
                                                     sym_name.parts().end() - 1);
                k::name enum_name{false, std::move(enum_parts)};
                found_enum = _unit.get_or_create_imported_enum(enum_name, _context);
            }

            if (found_enum) {
                auto entry = found_enum->get_entry_by_name(entry_name);
                if (entry.has_value()) {
                    size_t idx = 0;
                    for (auto& e : found_enum->entries()) {
                        if (e.name == entry_name) break;
                        idx++;
                    }
                    symbol.set_target(symbol_expression::enum_entry_target{found_enum, idx});
                    resolved_as_enum = true;
                } else {
                    throw_error(0x0080, symbol.first_lexeme(),
                        "Enum '{}' has no entry named '{}'",
                        {found_enum->get_short_name(), entry_name});
                }
            }
        }

        if (!resolved_as_enum) {
            // If this symbol is the callee of a function invocation, defer resolution to
            // type_reference_resolver which handles unified call syntax and member lookups.
            // Also defer if the symbol is an argument of a constructor_invocation_expression:
            // the name may be an enum entry that can only be resolved once the target type is known.
            // Otherwise throw immediately, since there is nothing further that can resolve it.
            auto parent_expr = symbol.get_parent_expression();
            bool is_function_callee = false;
            if (auto parent_invoc = std::dynamic_pointer_cast<function_invocation_expression>(parent_expr)) {
                is_function_callee = (parent_invoc->callee_expr().get() == &symbol);
            }
            bool is_ctor_arg = std::dynamic_pointer_cast<constructor_invocation_expression>(parent_expr) != nullptr;
            if (!is_function_callee && !is_ctor_arg) {
                throw_error(0x0003, symbol.first_lexeme(),
                    "Undefined symbol '{}': no variable, parameter or function with this name is visible in the current scope",
                    {symbol.get_name().to_string()});
            }
            // else: leave unresolved; type_reference_resolver will report the error if still not found
        }
    }
}

void type_reference_resolver::visit_symbol_expression(symbol_expression& symbol)
{
    if(!symbol.is_resolved()) {
        // Allow unresolved symbols that are arguments of a constructor_invocation_expression.
        // These may be enum entry names that will be resolved during visit_constructor_invocation_expression.
        auto parent = symbol.get_parent_expression();
        if (std::dynamic_pointer_cast<constructor_invocation_expression>(parent)) {
            return; // defer to visit_constructor_invocation_expression
        }
        throw_internal_error(0x0001, symbol.first_lexeme(),
            "Internal error: symbol '{}' reached type-resolution phase without being resolved; "
            "symbol resolution must be run before type resolution",
            {symbol.get_name().to_string()});
    }
    if (symbol.is_variable_def()) {
        auto var_def = symbol.get_variable_def();
        auto var_type = var_def->get_type();
        // If the variable is declared const (flag), propagate const-ness through the type system.
        // For primitive/struct types: wrap in const_type → "const int", "const MyStruct".
        // For indirection types (link, pointer, view): apply const to the pointed-at subtype
        // → "link(const int)", "pointer(const int)", "view(const int)".
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
                } else if (type::is_view(var_type)) {
                    var_type = const_sub->get_view();
                } else { // reference
                    var_type = const_sub->get_reference();
                }
            } else {
                var_type = var_type->get_const();
            }
        }
        // Variable symbol will always be a reference to the variable type.
        if (type::is_reference(var_type) || type::is_drain(var_type)) {
            // Variable is already a reference/drain (indirection), so symbol type is the variable type.
            symbol.set_type(var_type);
        } else {
            // Variable is not a reference, so symbol type is a reference to the variable type.
            symbol.set_type(var_type->get_reference());
        }
    } else if (symbol.is_function()) {
        // A symbol resolved to a function (without call parentheses) yields the address
        // of the function.  The type is a function_reference_type with ref_kind::link
        // (non-null, immutable — same semantics as a reference in K).
        // It can be freely assigned to a pointer (*), pin (^) or link (+) variable.
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
    } else if (symbol.is_enum_entry()) {
        // Enum entry: the type is the enum_type itself (not a reference — it's an rvalue constant).
        auto& target = symbol.get_enum_entry();
        auto en = target.enum_def;
        if (en && en->get_enum_type()) {
            symbol.set_type(en->get_enum_type());
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
            auto enclosing_stmt = symbol.find_statement();
            auto func = enclosing_stmt
                ? std::dynamic_pointer_cast<function>(enclosing_stmt->get_function())
                : nullptr;
            if(!func) {
                throw_internal_error(0x0001, symbol.first_lexeme(),
                    "Internal error: cannot find enclosing function context for member variable '{}' access; "
                    "member variables can only be accessed from inside a method",
                    {member_var->get_fq_name()});
            }
            this_value_ref = _context->_function_this_variables[func];
            if (!this_value_ref) {
                throw_internal_error(0x0002, symbol.first_lexeme(),
                    "Internal error: no 'this' pointer found in function '{}' for member variable '{}' access; "
                    "the function may be static or have no associated struct instance",
                    {func->get_fq_name(), member_var->get_fq_name()});
            }

            // Get member variable — potentially from an ancestor struct via __parent__ chain
            if(_struct_stack.empty()) {
                throw_internal_error(0x0003, symbol.first_lexeme(),
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
                    throw_internal_error(0x0004, symbol.first_lexeme(),
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
                throw_internal_error(0x0005, symbol.first_lexeme(),
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
                    throw_internal_error(0x0004, symbol.first_lexeme(),
                        "Internal error: struct '{}' has no member named '{}'; "
                        "the model is inconsistent — the member was not found during code generation",
                        {struct_type->name(), name});
                }
            } else { // TODO add here the method resolution
                throw_internal_error(0x0005, symbol.first_lexeme(),
                    "Internal error: struct has no LLVM type information when accessing member '{}'; "
                    "the declaration pass must be run before the implementation pass",
                    {name});
            }

        } else {
            throw_internal_error(0x0006, symbol.first_lexeme(),
                "Internal error: unsupported variable definition kind encountered while generating code for symbol '{}'; "
                "only parameters, global variables, local variables and member variables are supported",
                {var_def->get_fq_name()});
        }

        // Handle type of symbol
        auto var_type = var_def->get_type();
        llvm::Type* type = _context->get_llvm_type(var_type);

        if(ptr && type) {
            if (type::is_reference(var_type) || type::is_drain(var_type)) {
                // Type is a reference/drain (indirection), so value is loaded from the alloca
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
            throw_internal_error(0x0007, symbol.first_lexeme(),
                "Internal error: LLVM declaration not found for function '{}'; "
                "the declaration pass must be run before the implementation pass",
                {func ? func->get_fq_name() : "<null>"});
        }
        llvm::Function* llvm_func = it->second;
        if(!llvm_func) {
            throw_internal_error(0x0008, symbol.first_lexeme(),
                "Internal error: LLVM function object is null for '{}'; "
                "this indicates a compiler bug in the declaration pass",
                {func ? func->get_fq_name() : "<null>"});
        }
        _value = llvm_func;
    } else if (symbol.is_enum_entry()) {
        // Enum entry: generate a constant integer value with the enum's underlying LLVM type.
        auto& target = symbol.get_enum_entry();
        auto en = target.enum_def;
        auto& entry = en->entries()[target.entry_index];
        auto et = en->get_enum_type();
        llvm::Type* llvm_ty = et->get_llvm_type();
        _value = llvm::ConstantInt::get(llvm_ty, static_cast<uint64_t>(entry.value), /*isSigned=*/entry.value < 0);
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
        throw_internal_error(0x0001, expr.first_lexeme(),
            "Internal error: unary expression has a null sub-expression; "
            "this indicates a malformed AST or a compiler bug");
    }
    sub->accept(*this);
}

void type_reference_resolver::visit_unary_expression(unary_expression& expr)
{
    auto& sub = expr.sub_expr();

    if(!sub) {
        throw_internal_error(0x0002, expr.first_lexeme(),
            "Internal error: unary expression has a null sub-expression; "
            "this indicates a malformed AST or a compiler bug");
    }

    sub->accept(*this);

    if(!type::is_resolved(sub->get_type())) {
        throw_internal_error(0x0003, expr.first_lexeme(),
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
        throw_internal_error(0x0002, expr.first_lexeme(),
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
        throw_internal_error(0x0004, expr.first_lexeme(),
            "Internal error: binary expression has a null left or right operand; "
            "this indicates a malformed AST or a compiler bug");
    }

    left->accept(*this);
    right->accept(*this);

    if(!type::is_resolved(left->get_type())) {
        throw_internal_error(0x0005, expr.first_lexeme(),
            "Internal error: the left operand of a binary operator could not be type-resolved; "
            "the type of each operand must be known before the binary expression can be typed");
    }
    if(!type::is_resolved(right->get_type())) {
        throw_internal_error(0x0006, expr.first_lexeme(),
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
        throw_error(0x0018, expr.first_lexeme(),
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
        throw_internal_error(0x0009, expr.first_lexeme(),
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
        throw_error(0x0090, expr.first_lexeme(),
            "Cannot drain a non-reference expression: "
            "the '#' operator requires a reference (i.e. an addressable location) as its operand, "
            "but the operand has type '{}'",
            {sub_type ? sub_type->to_string() : "?"});
    }

    // Cannot drain a const object
    if (type::is_const(inner)) {
        throw_error(0x0091, expr.first_lexeme(),
            "Cannot drain a const object: "
            "the '#' operator requires a mutable object, "
            "but the operand has type '{}'",
            {sub_type ? sub_type->to_string() : "?"});
    }

    // Produce drain<T> from ref<T>
    expr.set_type(inner->get_drain());
}

void implementation_generator::visit_drain_expression(drain_expression& expr) {
    _value = nullptr;
    expr.sub_expr()->accept(*this);

    if(!_value) {
        throw_internal_error(0x000E, expr.first_lexeme(),
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
        throw_error(0x0019, expr.first_lexeme(),
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
        } else if (auto drn_t = std::dynamic_pointer_cast<drain_type>(sub_t)) {
            load_type = k::model::type::remove_const(drn_t->get_drained_type());
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
           std::dynamic_pointer_cast<view_type>(sub) ||
           std::dynamic_pointer_cast<owner_type>(sub)) {
            type = sub;
        } else {
            throw_error(0x001A, expr.first_lexeme(),
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
        throw_error(0x001B, expr.first_lexeme(),
            "Cannot dereference a non-pointer expression: "
            "the dereference operator ('*') requires a pointer (*), link (+), view (?) or owner (!), "
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
        std::dynamic_pointer_cast<view_type>(inner_type) ||
        std::dynamic_pointer_cast<owner_type>(inner_type)) {
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

    if(!type::is_reference(type) && !type::is_drain(type)) {
        // ── Bare struct rvalue (e.g. function return value) ──────────────────
        // Allow member access on struct-typed rvalues (temporaries).
        // The implementation generator will materialize the temporary into an alloca.
        if (type::is_struct(type)) {
            auto bare_subtype = type::remove_const(type);
            bool is_const_access = type::is_const(type);
            if (auto struct_subtype = std::dynamic_pointer_cast<struct_type>(bare_subtype)) {
                const auto& member_name = expr.symbol();
                const k::name& sym_name = member_name.get_name();
                const std::string& name_str = sym_name.to_string();
                std::string simple_name = sym_name.size() > 1 ? sym_name.back() : name_str;

                // Look up the member in the struct type (field or method)
                if (auto field = struct_subtype->get_member(simple_name)) {
                    auto field_type = field->field_type.lock();
                    if (is_const_access) {
                        field_type = type::remove_const(field_type)->get_const();
                    }
                    expr.set_type(field_type->get_reference());
                }
                // For method: leave type unset, handled by function_invocation_expression
            }
            return;
        }
        throw_error(0x001C, expr.first_lexeme(),
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

    // ── Virtual member: array.size ──────────────────────────────────────────
    // Arrays (sized or unsized) expose a virtual read-only member "size"
    // that returns the element count (unsigned int, stored in LLVM struct field 0).
    if (auto arr_subtype = std::dynamic_pointer_cast<array_type>(bare_subtype)) {
        const std::string& name_str = expr.symbol().get_name().to_string();
        if (name_str == "size") {
            // "size" is an unsigned int value (not a reference — it is loaded, not addressable)
            expr.set_type(_context->from_type(primitive_type::UNSIGNED_INT));
            return;
        }
        throw_error(0x001D, expr.first_lexeme(),
            "Arrays have no member named '{}'; only 'size' is available",
            {name_str});
    }

    if(auto struct_subtype = std::dynamic_pointer_cast<struct_type>(bare_subtype)) {
        const auto& member_name = expr.symbol();
        const k::name& sym_name = member_name.get_name();
        const std::string& name_str = sym_name.to_string();

        // ── Detect qualified member access (e.g. d.A::v where sym_name has parts ["A", "v"]) ──
        // If the name has more than one part, the last part is the member name and
        // the preceding parts form the qualifier (base class name).
        bool is_qualified = sym_name.size() > 1;
        std::string simple_name = is_qualified ? sym_name.back() : name_str;
        std::string qualifier_name;
        if (is_qualified) {
            // Build qualifier from all parts except the last
            for (size_t i = 0; i + 1 < sym_name.size(); ++i) {
                if (i > 0) qualifier_name += "::";
                qualifier_name += sym_name[i];
            }
        }

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

        auto hits = search_member(struct_subtype, simple_name, PUBLIC, true, false);
        if (hits.size() > 1) {
            std::cerr << "[DEBUG] Ambiguous: " << hits.size() << " hits for '" << simple_name << "' in '" << struct_subtype->name() << "'\n" << std::flush;
        }

        // If qualified, filter hits to only those in the named base
        if (is_qualified && !hits.empty()) {
            std::vector<MemberHit> filtered;
            for (auto& h : hits) {
                auto h_agg = h.in_struct_type->get_struct();
                if (h_agg && h_agg->get_short_name() == qualifier_name) {
                    filtered.push_back(h);
                }
            }
            if (filtered.empty()) {
                // If this is a function callee (e.g. this.Base::method()), defer
                // to function_invocation_expression which handles qualified calls.
                auto parent_expr = expr.get_parent_expression();
                if (auto parent_invoc = std::dynamic_pointer_cast<function_invocation_expression>(parent_expr)) {
                    if (parent_invoc->callee_expr().get() == &expr) {
                        return; // defer to function_invocation_expression
                    }
                }
                throw_error(0x001D, expr.first_lexeme(),
                    "No member named '{}' in struct '{}' or any of its bases",
                    {name_str, struct_subtype->name()});
                return;
            }
            hits = std::move(filtered);
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
            throw_error(0x001D, expr.first_lexeme(),
                "No member named '{}' in struct '{}' or any of its bases",
                {name_str, struct_subtype->name()});
        }

        if (hits.size() > 1) {
            throw_error(0x0031, expr.first_lexeme(),
                "Ambiguous access to member '{}' in struct '{}': "
                "the member is found in multiple base classes; use Base::member to disambiguate",
                {name_str, struct_subtype->name()});
        }

        auto& hit = hits[0];

        // Check visibility of the accessed member
        if (auto st_model = hit.in_struct_type->get_struct()) {
            auto mv = st_model->get_variable(simple_name);
            if (auto member_var = std::dynamic_pointer_cast<member_variable_definition>(mv)) {
                auto vis = member_var->get_visibility();
                if (vis != PUBLIC) {
                    if (!scope_lookup::is_struct_member_accessible(vis, *st_model, st_model, _function_stack)) {
                        if (vis != PROTECTED || !scope_lookup::is_friend_of(*st_model, _function_stack, _unit)) {
                            throw_error(0x0030, expr.first_lexeme(),
                                "{} member variable '{}' of struct '{}' is not accessible here; "
                                "it can only be accessed from member functions of '{}'{}",
                                {vis == PROTECTED ? "protected" : "private",
                                 member_var->get_short_name(), st_model->get_short_name(), st_model->get_short_name(),
                                 vis == PROTECTED ? " or its subclasses or friends" : ""});
                        }
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
        throw_error(0x001E, expr.first_lexeme(),
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

    // ── Bare struct rvalue (e.g. function return materialized into alloca) ──
    // _value is already a pointer (alloca) from struct return materialization.
    if (type::is_struct(type)) {
        auto bare_subtype = type::remove_const(type);
        if (auto struct_subtype = std::dynamic_pointer_cast<struct_type>(bare_subtype)) {
            const auto& member_name = expr.symbol();
            const k::name& sym_name = member_name.get_name();
            std::string simple_name = sym_name.size() > 1 ? sym_name.back() : sym_name.to_string();
            if (auto field = struct_subtype->get_member(simple_name); field) {
                _value = _builder->CreateStructGEP(bare_subtype->get_llvm_type(), _value, field->index);
            } else if (auto method = struct_subtype->get_struct()->get_function(simple_name)) {
                // Leave _value as the struct alloca pointer (will be used as 'this')
            } else {
                throw_internal_error(0x000A, expr.first_lexeme(),
                    "Internal error: struct '{}' has no member named '{}' during code generation; "
                    "the model is inconsistent — type resolution should have caught this earlier",
                    {struct_subtype->name(), simple_name});
            }
        }
        return;
    }

    // Strip const from the subtype to get the bare struct_type for GEP/method lookup.
    auto bare_subtype = type::remove_const(type->get_subtype());

    // ── Virtual member: array.size ──────────────────────────────────────────
    if (auto arr_subtype = std::dynamic_pointer_cast<array_type>(bare_subtype)) {
        const std::string& name_str = expr.symbol().get_name().to_string();
        if (name_str == "size") {
            // _value is a pointer to the array struct { i32, [N x T] }.
            // GEP into field 0 (size), then load.
            auto* struct_ty = arr_subtype->get_llvm_struct_type();
            auto* size_ptr = _builder->CreateStructGEP(struct_ty, _value, array_type::FIELD_SIZE, "arr_size_ptr");
            _value = _builder->CreateLoad(llvm::Type::getInt32Ty(_builder->getContext()), size_ptr, "arr_size");
            return;
        }
    }

    if(auto struct_subtype = std::dynamic_pointer_cast<struct_type>(bare_subtype)) {
        const auto& member_name =  expr.symbol();
        // For qualified names like A::v, use only the last part (the field name)
        const k::name& sym_name = member_name.get_name();
        std::string simple_name = sym_name.size() > 1 ? sym_name.back() : sym_name.to_string();
        if(auto field = struct_subtype->get_member(simple_name); field) {
            _value = _builder->CreateStructGEP(bare_subtype->get_llvm_type(), _value, field->index);
        } else if(auto method = struct_subtype->get_struct()->get_function(simple_name)) {
            // Note return the already-assigned address of the struct onto which the function is applied to
        } else {
            throw_internal_error(0x000A, expr.first_lexeme(),
                "Internal error: struct '{}' has no member named '{}' during code generation; "
                "the model is inconsistent — type resolution should have caught this earlier",
                {struct_subtype->name(), simple_name});
        }
    } else {
        throw_internal_error(0x000B, expr.first_lexeme(),
            "Internal error: the '.' operator is applied to a non-struct type during code generation; "
            "the operand type is '{}' — type resolution should have caught this earlier",
            {type && type->get_subtype() ? type->get_subtype()->to_string() : "?"});
    }
}

//
// Member of pointer expression (->)
// Acts as (*expr).member. Supported LHS: pointer (*), link (+), view (?).
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
    } else if (auto view_t = std::dynamic_pointer_cast<view_type>(type)) {
        pointed_type = view_t->get_viewed_type();
    } else if (auto own_t = std::dynamic_pointer_cast<owner_type>(type)) {
        // Allow -> on owner: syntactic sugar for (*owner).member
        pointed_type = own_t->get_owned_type();
    } else {
        throw_error(0x0080, expr.first_lexeme(),
            "The '->' operator requires a pointer (*), link (+), view (?) or owner (!) on the LHS, "
            "but got '{}'", {type ? type->to_string() : "?"});
    }

    // ── Virtual member: array->size ─────────────────────────────────────────
    // pointed_type is array_type directly for sized arrays (e.g. int[3]*),
    // or reference<array_type> for unsized arrays (e.g. int[]* since int[] = int[]&).
    auto arr_pointed = pointed_type;
    if (auto ref_wrap = std::dynamic_pointer_cast<reference_type>(arr_pointed)) {
        arr_pointed = ref_wrap->get_subtype();
    }
    if (auto arr_subtype = std::dynamic_pointer_cast<array_type>(arr_pointed)) {
        const std::string& name_str = expr.symbol().get_name().to_string();
        if (name_str == "size") {
            expr.set_type(_context->from_type(primitive_type::UNSIGNED_INT));
            return;
        }
        throw_error(0x001D, expr.first_lexeme(),
            "Arrays have no member named '{}'; only 'size' is available",
            {name_str});
    }

    auto struct_subtype = std::dynamic_pointer_cast<struct_type>(pointed_type);
    if (!struct_subtype) {
        throw_error(0x0081, expr.first_lexeme(),
            "The '->' operator requires a pointer to a struct or array, "
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
                        if (vis != PROTECTED || !scope_lookup::is_friend_of(*st_model, _function_stack, _unit)) {
                            throw_error(0x0083, expr.first_lexeme(),
                                "{} member variable '{}' of struct '{}' is not accessible here via '->'; "
                                "it can only be accessed from member functions of '{}'{}",
                                {vis == PROTECTED ? "protected" : "private",
                                 mv->get_short_name(), st_model->get_short_name(), st_model->get_short_name(),
                                 vis == PROTECTED ? " or its subclasses or friends" : ""});
                        }
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
                        if (vis != PROTECTED || !scope_lookup::is_friend_of(*st_model, _function_stack, _unit)) {
                            throw_error(0x0084, expr.first_lexeme(),
                                "{} member function '{}' of struct '{}' is not accessible here via '->'; "
                                "it can only be called from member functions of '{}'{}",
                                {vis == PROTECTED ? "protected" : "private",
                                 fn->get_short_name(), st_model->get_short_name(), st_model->get_short_name(),
                                 vis == PROTECTED ? " or its subclasses or friends" : ""});
                        }
                    }
                }
            }
        }
        expr.set_type(pointed_type->get_reference());
    } else {
        throw_error(0x0082, expr.first_lexeme(),
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

    // Null-check for nullable indirections (pointer, pin, owner)
    if (std::dynamic_pointer_cast<pointer_type>(inner_type) ||
        std::dynamic_pointer_cast<view_type>(inner_type) ||
        std::dynamic_pointer_cast<owner_type>(inner_type)) {
        auto* fatal = get_or_declare_fatal_null_function("__fatal_null_dereference");
        emit_null_check(_value, fatal, "arrow");
    }

    std::shared_ptr<k::model::type> pointed_type;
    if (auto ptr_t = std::dynamic_pointer_cast<pointer_type>(inner_type)) pointed_type = ptr_t->get_pointed_type();
    else if (auto lnk_t = std::dynamic_pointer_cast<link_type>(inner_type)) pointed_type = lnk_t->get_linked_type();
    else if (auto view_t = std::dynamic_pointer_cast<view_type>(inner_type)) pointed_type = view_t->get_viewed_type();
    else if (auto own_t = std::dynamic_pointer_cast<owner_type>(inner_type)) pointed_type = own_t->get_owned_type();
    if (!pointed_type) return;

    // ── Virtual member: array->size ─────────────────────────────────────────
    // For unsized arrays (int[] = int[]&), pointed_type is reference<array_type>.
    // We must unwrap the reference and add an extra load to follow the double
    // indirection (ptr → ref → array struct).
    auto arr_pointed = pointed_type;
    bool arr_needs_ref_load = false;
    if (auto ref_wrap = std::dynamic_pointer_cast<reference_type>(arr_pointed)) {
        arr_pointed = ref_wrap->get_subtype();
        arr_needs_ref_load = true;
    }
    if (auto arr_subtype = std::dynamic_pointer_cast<array_type>(arr_pointed)) {
        const std::string& name_str = expr.symbol().get_name().to_string();
        if (name_str == "size") {
            if (arr_needs_ref_load) {
                // Extra load: dereference the reference to get the actual array struct pointer
                _value = _builder->CreateLoad(
                    llvm::PointerType::get(_builder->getContext(), 0), _value, "arr_ref_load");
            }
            auto* struct_ty = arr_subtype->get_llvm_struct_type();
            auto* size_ptr = _builder->CreateStructGEP(struct_ty, _value, array_type::FIELD_SIZE, "arr_size_ptr");
            _value = _builder->CreateLoad(llvm::Type::getInt32Ty(_builder->getContext()), size_ptr, "arr_size");
            return;
        }
    }

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
        } else if (auto view_var = std::dynamic_pointer_cast<view_type>(obj_type)) {
            obj_type = view_var->get_viewed_type();
        } else {
            throw_error(0x0090, expr.first_lexeme(),
                "The '->*' operator requires a pointer (*), link (+) or view (?) on the LHS, "
                "but got '{}'", {obj_type ? obj_type->to_string() : "?"});
        }
    }

    auto struct_t = std::dynamic_pointer_cast<struct_type>(obj_type);
    if (!struct_t) {
        throw_error(0x0091, expr.first_lexeme(),
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
            throw_error(0x0092, expr.first_lexeme(),
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
        throw_error(0x001F, expr.first_lexeme(),
            "Subscript operator '[]' requires a reference to an array as left operand, "
            "but the left operand has type '{}' which is not a reference",
            {left_type ? left_type->to_string() : "?"});
    }
    if(type::is_double_reference(left_type)) {
        // Deref first ref
        left_type = left_type->get_subtype();
    }
    left_type = std::dynamic_pointer_cast<reference_type>(left_type)->get_subtype();

    // Strip const qualifier before checking for indirection / array
    left_type = type::remove_const(left_type);

    // Unwrap any indirection type (owner, pointer, link, view) to reach the inner array
    if (type::is_owner(left_type)) {
        left_type = std::dynamic_pointer_cast<owner_type>(left_type)->get_owned_type();
    } else if (type::is_pointer(left_type)) {
        left_type = std::dynamic_pointer_cast<pointer_type>(left_type)->get_pointed_type();
    } else if (type::is_link(left_type)) {
        left_type = std::dynamic_pointer_cast<link_type>(left_type)->get_linked_type();
    } else if (type::is_view(left_type)) {
        left_type = std::dynamic_pointer_cast<view_type>(left_type)->get_viewed_type();
    }

    // Unsized arrays (e.g. char[]) are canonicalized to ref<array<T>>.
    // After unwrapping an indirection such as pointer<ref<array<T>>> we
    // may still have a reference wrapper — strip it to reach the array.
    if (type::is_reference(left_type)) {
        left_type = std::dynamic_pointer_cast<reference_type>(left_type)->get_subtype();
    }

    // Strip any remaining const wrapper (e.g. pointer<const<array<T>>> → const<array<T>>)
    left_type = type::remove_const(left_type);

    if(!type::is_array(left_type)) {
        throw_error(0x0020, expr.first_lexeme(),
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
        throw_error(0x0021, expr.first_lexeme(),
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

    // At this point left_type is ref<X> where X can be:
    //   - sized_array_type or array_type (direct array)
    //   - owner_type, pointer_type, link_type, view_type wrapping an array
    //   - const_type wrapping any of the above
    auto arr_type_inner = left_type->get_subtype();

    // Strip const qualifier before checking for indirection / array
    arr_type_inner = type::remove_const(arr_type_inner);

    // Handle any indirection wrapping an array: load the raw pointer, then treat it as ptr to array struct
    // All indirection types (owner, pointer, link, view) are opaque pointers in LLVM IR.
    if (auto own_type = std::dynamic_pointer_cast<owner_type>(arr_type_inner)) {
        auto* ptr_ty = llvm::PointerType::get(_builder->getContext(), 0);
        left = _builder->CreateLoad(ptr_ty, left, "own_arr_ptr");
        arr_type_inner = own_type->get_owned_type();
    } else if (auto ptr_type = std::dynamic_pointer_cast<pointer_type>(arr_type_inner)) {
        auto* ptr_ty = llvm::PointerType::get(_builder->getContext(), 0);
        left = _builder->CreateLoad(ptr_ty, left, "ptr_arr_ptr");
        arr_type_inner = ptr_type->get_pointed_type();
    } else if (auto lnk_type = std::dynamic_pointer_cast<link_type>(arr_type_inner)) {
        auto* ptr_ty = llvm::PointerType::get(_builder->getContext(), 0);
        left = _builder->CreateLoad(ptr_ty, left, "lnk_arr_ptr");
        arr_type_inner = lnk_type->get_linked_type();
    } else if (auto view_type_var = std::dynamic_pointer_cast<view_type>(arr_type_inner)) {
        auto* ptr_ty = llvm::PointerType::get(_builder->getContext(), 0);
        left = _builder->CreateLoad(ptr_ty, left, "view_arr_ptr");
        arr_type_inner = view_type_var->get_viewed_type();
    }

    // Unsized arrays (e.g. char[]) are canonicalized to ref<array<T>>.
    // After unwrapping an indirection such as pointer<ref<array<T>>> we
    // may still have a reference wrapper — strip it and load the actual
    // array struct pointer through the reference.
    if (auto ref_inner = std::dynamic_pointer_cast<reference_type>(arr_type_inner)) {
        arr_type_inner = ref_inner->get_subtype();
        auto* ptr_ty = llvm::PointerType::get(_builder->getContext(), 0);
        left = _builder->CreateLoad(ptr_ty, left, "ref_arr_ptr");
    }

    // Strip any remaining const wrapper (e.g. pointer<const<array<T>>> → const<array<T>>)
    arr_type_inner = type::remove_const(arr_type_inner);

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
            throw_internal_error(0x000C, expr.first_lexeme(),
                "Internal error: sized array has no LLVM struct type during subscript code generation");
        }

        // ── Runtime bounds check ─────────────────────────────────────────
        // Load the element count from field 0 and verify index < count.
        llvm::Value* count_ptr = _builder->CreateStructGEP(struct_llvm, left,
            sized_array_type::FIELD_SIZE, "arr_count_ptr");
        llvm::Value* count_val = _builder->CreateLoad(
            _builder->getInt32Ty(), count_ptr, "arr_count");
        emit_array_bounds_check(_builder.get(), get_module(), right, count_val, "subscript");

        // Two-step GEP: first into the struct field 1, then into the array element
        llvm::Value* field_data_ptr = _builder->CreateStructGEP(struct_llvm, left,
            sized_array_type::FIELD_DATA, "arr_data_ptr");
        llvm::Value* indices[] = {_builder->getInt32(0), right};
        _value = _builder->CreateGEP(data_arr_llvm, field_data_ptr, indices, "elem_ptr");
    } else if (auto unsized_arr = std::dynamic_pointer_cast<array_type>(arr_type_inner)) {
        // Unsized (dynamic) array: layout { i32, [0 x T] } — same GEP pattern as sized.
        auto* struct_llvm = unsized_arr->get_llvm_struct_type();
        auto* data_arr_llvm = unsized_arr->get_llvm_data_array_type();
        if (!struct_llvm || !data_arr_llvm) {
            throw_internal_error(0x000C, expr.first_lexeme(),
                "Internal error: unsized array has no LLVM struct type during subscript code generation");
        }

        // Runtime bounds check: load count from field 0, verify index < count
        llvm::Value* count_ptr = _builder->CreateStructGEP(struct_llvm, left,
            array_type::FIELD_SIZE, "dynarr_count_ptr");
        llvm::Value* count_val = _builder->CreateLoad(
            _builder->getInt32Ty(), count_ptr, "dynarr_count");
        emit_array_bounds_check(_builder.get(), get_module(), right, count_val, "subscript");

        // GEP into field 1 (data), then index into the [0 x T] trailing array
        llvm::Value* field_data_ptr = _builder->CreateStructGEP(struct_llvm, left,
            array_type::FIELD_DATA, "dynarr_data_ptr");
        llvm::Value* indices[] = {_builder->getInt32(0), right};
        _value = _builder->CreateGEP(data_arr_llvm, field_data_ptr, indices, "elem_ptr");
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
    if (!type::is_reference(this_type) && !type::is_drain(this_type)) {
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
    auto ptr_member_callee = std::dynamic_pointer_cast<member_of_pointer_expression>(expr.callee_expr());

    if(!callee && !member_callee && !pm_callee && !ptr_member_callee) {
        throw_error(0x0022, expr.first_lexeme(),
            "Unsupported call expression form: only direct function calls ('func(args)'), "
            "member function calls ('obj.method(args)') and pointer-to-member calls "
            "('obj.*mfp(args)') are supported");
    }

    // Resolve and type-check all arguments first
    for(auto& arg : expr.arguments()) {
        arg->accept(*this);
    }

    // ── Pre-process: ptr->method(args) → (*ptr).method(args) ─────────────────
    // When the callee is a member_of_pointer_expression (ptr->method), transform it
    // into member_of_object_expression(dereference(ptr), method) so that the existing
    // member_callee path handles dispatch uniformly (including vtable dispatch).
    if (ptr_member_callee) {
        auto sym = ptr_member_callee->symbol().shared_as<symbol_expression>();
        auto sub = ptr_member_callee->sub_expr();
        auto deref = dereference_expression::make_shared(sub);
        auto obj_member = member_of_object_expression::make_shared(deref, sym);
        expr.callee_expr(obj_member);
        member_callee = obj_member;
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
            throw_error(0x0093, expr.first_lexeme(),
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
            throw_error(0x0023, expr.first_lexeme(),
                "Unsupported member call form: the right-hand side of '.' must be a simple name, "
                "not a complex expression");
        }

        // sub_expr of member_callee gives the object reference (possibly upcast)
        auto this_expr = member_callee->sub_expr();
        auto this_type = this_expr->get_type(); // should be ref<struct> (possibly base)

        if (!type::is_reference(this_type) && !type::is_struct(this_type) && !type::is_drain(this_type)) {
            throw_error(0x0024, expr.first_lexeme(),
                "The '.' operator requires the left-hand side to have a reference type, "
                "but '{}' is not a reference; did you mean to use a reference parameter?",
                {this_type ? this_type->to_string() : "?"});
        }
        auto subtype = (type::is_reference(this_type) || type::is_drain(this_type)) ? this_type->get_subtype() : this_type;
        // Detect if the object is accessed through a const reference (ref<const S>)
        bool is_const_this = type::is_const(subtype);
        auto bare_subtype = type::remove_const(subtype);
        auto struct_subtype = std::dynamic_pointer_cast<struct_type>(bare_subtype);
        if (!struct_subtype) {
            throw_error(0x0025, expr.first_lexeme(),
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
                throw_error(0x0040, expr.first_lexeme(),
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
                throw_error(0x0041, expr.first_lexeme(),
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
                throw_error(0x0034, expr.first_lexeme(),
                    "Cannot call mutable member function '{}' on a const object of type '{}': "
                    "only const member functions can be called on const objects",
                    {func_short_name, struct_subtype->name()});
            }
            candidates = std::move(const_candidates);
        }

        if (candidates.empty()) {
            throw_error(0x0026, expr.first_lexeme(),
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
            throw_error(0x002B, expr.first_lexeme(),
                "Static constructor '{}' cannot be called explicitly; "
                "it is automatically invoked during program initialization",
                {best.func->get_short_name()});
        }
        if (std::dynamic_pointer_cast<static_destructor>(best.func)) {
            throw_error(0x002C, expr.first_lexeme(),
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
                                   type::is_pointer(inner_type) || type::is_view(inner_type))) {
                inner_type = inner_type->get_subtype();
            }
            // Also unwrap an unresolved_function_ref_type that has been resolved
            if (auto ufrt = std::dynamic_pointer_cast<unresolved_function_ref_type>(inner_type)) {
                if (ufrt->is_resolved()) {
                    inner_type = ufrt->get_resolved();
                    while (inner_type && (type::is_reference(inner_type) || type::is_link(inner_type) ||
                                           type::is_pointer(inner_type) || type::is_view(inner_type))) {
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
        //
        // Also applies to unqualified member-function calls: when calling method() from
        // within another member function, inject 'this' so that the member function
        // candidate can match via Mode A in get_best_matching_function.
        if (!this_candidate && !_function_stack.empty()) {
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
            throw_error(0x0027, expr.first_lexeme(),
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
            throw_error(0x002D, expr.first_lexeme(),
                "Static constructor '{}' cannot be called explicitly; "
                "it is automatically invoked during program initialization",
                {best.func->get_short_name()});
        }
        if (std::dynamic_pointer_cast<static_destructor>(best.func)) {
            throw_error(0x002E, expr.first_lexeme(),
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
        throw_internal_error(0x000C, expr.first_lexeme(),
            "Internal error: unsupported call expression form during code generation; "
            "only direct, member and pointer-to-member function calls are supported");
    }

    // ── Helper state: track whether sret destination was consumed ──────────
    bool _sret_dest_was_consumed = false;

    // ── Helper lambda: handle sret call result ─────────────────────────────
    // For functions that return non-primitive types via sret, _value after the call
    // is the sret alloca pointer (already written by the callee).
    // If the struct has a destructor and this is a temporary, track it for cleanup.
    auto handle_sret_result = [&](llvm::Value* sret_ptr_val) {
        _value = sret_ptr_val;

        // If the sret destination was consumed from _sret_destination (variable init),
        // it's NOT a temporary — don't track it for cleanup (the variable owns it).
        if (_sret_dest_was_consumed) {
            _sret_dest_was_consumed = false;
            return;
        }

        // Track for temporary cleanup if the struct has a destructor
        if (!expr.get_type()) return;
        auto ret_type_nc = type::remove_const(expr.get_type());
        auto ret_st = std::dynamic_pointer_cast<struct_type>(ret_type_nc);
        if (!ret_st) return;
        auto st = ret_st->get_struct();
        if (st) {
            auto dtor = st->get_destructor();
            if (dtor) {
                auto dtor_fn = dtor->shared_as<k::model::function>();
                auto dtor_it = _context->_functions.find(dtor_fn);
                if (dtor_it != _context->_functions.end()) {
                    // Only track if the value is an AllocaInst (temporary)
                    if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(sret_ptr_val)) {
                        _expression_temporaries.push_back(std::make_pair(alloca, dtor_it->second));
                    }
                }
            }
        }
    };

    // ── Helper lambda: create or get sret destination for a call ──────────────
    // If _sret_destination is set (from variable_statement or return), use it directly.
    // Otherwise create a new temporary alloca.
    auto get_sret_ptr_for_call = [&]() -> llvm::Value* {
        if (_sret_destination) {
            // Caller provided a destination — use it directly (no temporary)
            llvm::Value* dest = _sret_destination;
            _sret_destination = nullptr; // consume it
            _sret_dest_was_consumed = true;
            return dest;
        }
        _sret_dest_was_consumed = false;
        // Create a temporary alloca for the sret result
        auto ret_type_nc = type::remove_const(expr.get_type());
        llvm::Type* llvm_ret = _context->get_llvm_type(ret_type_nc);
        llvm::Function* cur_fn = _builder->GetInsertBlock()->getParent();
        llvm::IRBuilder<> entry_builder(&cur_fn->getEntryBlock(), cur_fn->getEntryBlock().begin());
        return entry_builder.CreateAlloca(llvm_ret, nullptr, "sret_tmp");
    };

    // ── Helper lambda: emit a call with sret if needed ──────────────────────
    // Wraps CreateCall: if the callee uses sret ABI, prepend the sret pointer.
    // Returns the sret pointer (or nullptr if not sret).
    auto emit_sret_call = [&](llvm::FunctionType* fn_type, llvm::Value* callee_val,
                               std::vector<llvm::Value*>& call_args,
                               const std::string& name) -> llvm::Value* {
        bool callee_is_sret = fn_type->getReturnType()->isVoidTy()
            && expr.get_type() && needs_sret_return(expr.get_type());
        if (callee_is_sret) {
            llvm::Value* sret_ptr = get_sret_ptr_for_call();
            call_args.insert(call_args.begin(), sret_ptr);

            // Rebuild fn_type with the sret param prepended
            std::vector<llvm::Type*> param_types;
            param_types.push_back(llvm::PointerType::get(**_context, 0));
            for (auto* pt : fn_type->params())
                param_types.push_back(pt);
            auto* sret_fn_type = llvm::FunctionType::get(
                llvm::Type::getVoidTy(**_context), param_types, false);

            _builder->CreateCall(sret_fn_type, callee_val, call_args);
            return sret_ptr;
        }
        // Non-sret call
        _value = _builder->CreateCall(fn_type, callee_val, call_args,
            fn_type->getReturnType()->isVoidTy() ? "" : name);
        return nullptr;
    };

    // ── INDIRECT_MEMBER call via pointer-to-member  obj.*mfp(args) ────────────
    if (expr.has_dispatch_info() &&
        expr.get_dispatch_info().kind == virtual_dispatch_info::dispatch_kind::INDIRECT_MEMBER) {
        // pm_callee->left() = object expression (this), pm_callee->right() = mfp variable
        if (!pm_callee) {
            throw_internal_error(0x0048, expr.first_lexeme(),
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
                    std::dynamic_pointer_cast<view_type>(inner)) {
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
            throw_internal_error(0x0049, expr.first_lexeme(),
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

        auto* sret_result = emit_sret_call(llvm_fn_type, fn_ptr, call_args, "mfp_call");
        if (sret_result) {
            handle_sret_result(sret_result);
        }
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
            throw_internal_error(0x0041, expr.first_lexeme(),
                "Internal error: indirect call through function reference produced no LLVM value");
        }

        // Build the LLVM function type from the function_reference_type in the call expression.
        auto callee_type = callee ? callee->get_type() : nullptr;
        auto inner_type = callee_type;
        while (inner_type && (type::is_reference(inner_type) || type::is_link(inner_type) ||
                               type::is_pointer(inner_type) || type::is_view(inner_type))) {
            inner_type = inner_type->get_subtype();
        }
        auto frt = std::dynamic_pointer_cast<function_reference_type>(inner_type);
        if (!frt) {
            throw_internal_error(0x0042, expr.first_lexeme(),
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
                throw_internal_error(0x0043, expr.first_lexeme(),
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
                throw_internal_error(0x0044, expr.first_lexeme(),
                    "Internal error: an argument for an indirect call produced no LLVM value");
            }
            call_args.push_back(_value);
        }

        auto* sret_result = emit_sret_call(llvm_fn_type, fn_ptr, call_args, "ind_call");
        if (sret_result) {
            handle_sret_result(sret_result);
        }
        return;
    }

    // Generate arguments and add them to the args list (for non-indirect calls)
    std::vector<llvm::Value*> args;
    if (member_callee) {
        callee = std::dynamic_pointer_cast<symbol_expression>(member_callee->symbol().shared_as<symbol_expression>());
        if (!callee) {
            throw_internal_error(0x000D, expr.first_lexeme(),
                "Internal error: member function call has a non-symbol callee; "
                "this should have been rejected during type resolution");
        }

        // First argument is the object pointer (this)
        member_callee->sub_expr()->accept(*this);
        if(!_value) {
            throw_internal_error(0x000E, expr.first_lexeme(),
                "Internal error: failed to generate the 'this' argument for member function call '{}'; "
                "the object expression produced no LLVM value",
                {callee ? callee->get_name().to_string() : "<unknown>"});
        }

        args.push_back(_value);
    }
    // Save outer _sret_destination — it's meant for the call result, not for arguments
    llvm::Value* saved_sret_destination = _sret_destination;
    _sret_destination = nullptr;

    for(auto arg : expr.arguments()) {
        _value = nullptr;

        // ── Argument copy elision for by-value struct parameters ──────────
        // When a by-value struct argument is the direct result of a sret-
        // returning function call (prvalue), set _sret_destination so the
        // inner call writes directly into a staging alloca without tracking
        // it as a temporary. This avoids an extra destructor call.
        bool arg_elision_set = false;
        bool arg_is_struct = arg->get_type() && type::is_struct(arg->get_type())
            && !type::is_reference(arg->get_type())
            && !type::is_any_indirection(arg->get_type());
        bool arg_is_fn_call = std::dynamic_pointer_cast<function_invocation_expression>(arg) != nullptr;
        if (arg_is_struct
            && needs_sret_return(arg->get_type())
            && !_sret_destination
            && arg_is_fn_call)
        {
            auto st_type_nc = type::remove_const(arg->get_type());
            llvm::Type* llvm_st = _context->get_llvm_type(st_type_nc);
            llvm::Function* cur_fn = _builder->GetInsertBlock()->getParent();
            llvm::IRBuilder<> entry_builder(&cur_fn->getEntryBlock(), cur_fn->getEntryBlock().begin());
            auto* staging_alloca = entry_builder.CreateAlloca(llvm_st, nullptr, "arg_staging");
            _sret_destination = staging_alloca;
            arg_elision_set = true;
        }

        arg->accept(*this);

        // Only clear _sret_destination if WE set it (and it wasn't consumed)
        if (arg_elision_set && _sret_destination) {
            _sret_destination = nullptr;
        }

        if(!_value) {
            throw_internal_error(0x000F, expr.first_lexeme(),
                "Internal error: a call argument for '{}' produced no LLVM value during code generation; "
                "this indicates a bug in expression code generation",
                {callee ? callee->get_name().to_string() : "<unknown>"});
        }
        // If the argument is a struct rvalue (bare struct type, not ref) and _value is
        // an alloca (pointer), we need to load the aggregate to pass it by value.
        // This happens when a function return value is materialized into an alloca.
        if (arg->get_type() && type::is_struct(arg->get_type())) {
            if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(_value)) {
                auto st_type_nc = type::remove_const(arg->get_type());
                llvm::Type* llvm_st = _context->get_llvm_type(st_type_nc);
                _value = _builder->CreateLoad(llvm_st, alloca, "struct_arg_load");
            }
        }
        args.push_back(_value);
    }

    // Restore outer _sret_destination for the call result
    _sret_destination = saved_sret_destination;

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
            throw_internal_error(0x0010, expr.first_lexeme(),
                "Internal error: LLVM declaration not found for function '{}' during code generation; "
                "the declaration pass must be run before the implementation pass",
                {function ? function->get_fq_name() : "<null>"});
        }
    }
    llvm::Function* llvm_func = (it != _context->_functions.end()) ? it->second : nullptr;
    if(llvm_func == nullptr &&
       !(function && (function->is_abstract_func() ||
                      (function->is_virtual() && function->is_external())))) {
        throw_internal_error(0x0011, expr.first_lexeme(),
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
                // For sret: prepend sret pointer parameter
                if (function->has_return_type() && needs_sret_return(function->get_return_type()))
                    param_types.push_back(llvm::PointerType::get(**_context, 0));
                if (function->is_member() && !function->is_static())
                    param_types.push_back(_context->get_llvm_type(function->get_this_parameter()->get_type()));
                for (const auto& param : function->parameters())
                    param_types.push_back(_context->get_llvm_type(param->get_type()));
                llvm::Type* ret_type_llvm = llvm::Type::getVoidTy(**_context);
                if (function->has_return_type() && !needs_sret_return(function->get_return_type()))
                    ret_type_llvm = _context->get_llvm_type(function->get_return_type());
                fn_type = llvm::FunctionType::get(ret_type_llvm, param_types, false);
            }
            // Check if sret ABI is used
            bool call_uses_sret = fn_type->getReturnType()->isVoidTy()
                && expr.get_type() && needs_sret_return(expr.get_type());
            if (call_uses_sret) {
                llvm::Value* sret_alloca = get_sret_ptr_for_call();
                args.insert(args.begin(), sret_alloca);
                _value = emit_virtual_dispatch_call(*_builder, *kl, args[1], di.slot_index, fn_type, args, _context, "");
                handle_sret_result(sret_alloca);
            } else {
                _value = emit_virtual_dispatch_call(*_builder, *kl, args[0], di.slot_index, fn_type, args, _context, "");
            }
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
                if (function->has_return_type() && needs_sret_return(function->get_return_type()))
                    param_types.push_back(llvm::PointerType::get(**_context, 0));
                if (function->is_member() && !function->is_static() && function->get_this_parameter())
                    param_types.push_back(_context->get_llvm_type(function->get_this_parameter()->get_type()));
                for (const auto& param : function->parameters())
                    param_types.push_back(_context->get_llvm_type(param->get_type()));
                llvm::Type* ret_type_llvm = llvm::Type::getVoidTy(**_context);
                if (function->has_return_type() && !needs_sret_return(function->get_return_type()))
                    ret_type_llvm = _context->get_llvm_type(function->get_return_type());
                fn_type = llvm::FunctionType::get(ret_type_llvm, param_types, false);
            }
            if (!fn_type) {
                throw_internal_error(0x0015, expr.first_lexeme(),
                    "Internal error: cannot build FunctionType for imported virtual dispatch of '{}'",
                    {function ? function->get_fq_name() : "<null>"});
            }

            auto* struct_llvm_type = imp_agg->get_struct_type()
                                     ? imp_agg->get_struct_type()->get_llvm_type() : nullptr;
            if (!struct_llvm_type) {
                throw_internal_error(0x0016, expr.first_lexeme(),
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
            bool call_uses_sret = fn_type->getReturnType()->isVoidTy()
                && expr.get_type() && needs_sret_return(expr.get_type());
            if (call_uses_sret) {
                llvm::Value* sret_alloca = get_sret_ptr_for_call();
                args.insert(args.begin(), sret_alloca);
                _builder->CreateCall(fn_type, fn_ptr, args);
                handle_sret_result(sret_alloca);
            } else {
                _value = _builder->CreateCall(fn_type, fn_ptr, args,
                    fn_type->getReturnType()->isVoidTy() ? "" : "imp_vcall");
            }
            return;
        }
    }
    // ── Direct call (non-virtual, or qualified, or free function) ────────────
    bool call_uses_sret = llvm_func->getReturnType()->isVoidTy()
        && expr.get_type() && needs_sret_return(expr.get_type());
    if (call_uses_sret) {
        llvm::Value* sret_alloca = get_sret_ptr_for_call();
        args.insert(args.begin(), sret_alloca);
        _builder->CreateCall(llvm_func, args);
        handle_sret_result(sret_alloca);
    } else {
        _value = _builder->CreateCall(llvm_func, args);
    }
}

//
// Constructor invocation
//

void symbol_resolver::visit_constructor_invocation_expression(constructor_invocation_expression& expr) {
    for (auto arg : expr.arguments()) {
        arg->accept(*this);
    }
}

//
// New expression
//

void symbol_resolver::visit_new_expression(new_expression& expr) {
    if (expr.is_uniform_array()) {
        if (expr.array_size_expr()) expr.array_size_expr()->accept(*this);
        for (auto& a : expr.uniform_ctor_args()) {
            if (a) a->accept(*this);
        }
    } else if (expr.is_array()) {
        if (expr.array_size_expr()) expr.array_size_expr()->accept(*this);
        for (auto& e : expr.array_init_elements()) {
            if (e) e->accept(*this);
        }
    } else {
        for (auto& arg : expr.arguments()) {
            arg->accept(*this);
        }
    }
}

void type_reference_resolver::visit_new_expression(new_expression& expr) {
    if (expr.is_uniform_array()) {
        // ── Uniform array form: new T(args)[N] ──

        // Resolve uniform ctor args
        for (auto& a : expr._uniform_ctor_args) {
            if (a) a->accept(*this);
        }

        // Resolve the array size expression
        if (expr._array_size_expr) {
            expr._array_size_expr->accept(*this);
        }

        // Resolve the element type
        auto elem_type = expr.allocated_type();
        if (!type::is_resolved(elem_type)) {
            if (auto unres = std::dynamic_pointer_cast<unresolved_type>(elem_type)) {
                auto resolved = resolve_type_by_name(unres->type_id(), static_cast<const element&>(expr));
                if (!resolved || !type::is_resolved(resolved)) {
                    auto imported_agg = _unit.get_or_create_imported_aggregate(unres->type_id(), _context);
                    if (imported_agg) resolved = imported_agg->get_struct_type();
                }
                if (resolved && type::is_resolved(resolved)) {
                    expr.allocated_type(resolved);
                    elem_type = resolved;
                }
            }
        }
        if (!type::is_resolved(elem_type)) {
            throw_error(0x0055, expr.first_lexeme(),
                "Cannot resolve element type of 'new' uniform array expression: type '{}' is unknown",
                {elem_type ? elem_type->to_string() : "<null>"});
            return;
        }

        // Determine the array size (static or dynamic)
        size_t arr_size = 0;
        bool is_dynamic = false;

        if (expr._array_size_expr) {
            auto size_val = std::dynamic_pointer_cast<value_expression>(expr._array_size_expr);
            if (size_val && size_val->is_literal()
                && std::holds_alternative<lex::integer>(size_val->any_literal())) {
                auto& int_lit = size_val->any_literal().get<lex::integer>();
                arr_size = int_lit.to_unsigned_int();
            } else {
                // Dynamic size
                is_dynamic = true;
                auto uint_type = _context->from_type(primitive_type::UNSIGNED_INT);
                auto adapted_size = adapt_type(expr._array_size_expr, uint_type);
                if (!adapted_size) {
                    throw_error(0x4233, expr.first_lexeme(),
                        "Uniform array size expression must be convertible to an unsigned integer; "
                        "expression has type '{}'",
                        {expr._array_size_expr->get_type() ? expr._array_size_expr->get_type()->to_string() : "?"});
                    return;
                }
                if (adapted_size != expr._array_size_expr) {
                    expr._array_size_expr = adapted_size;
                    adapted_size->set_parent_expression(expr.shared_as<expression>());
                }
            }
        }

        // Check for abstract types
        if (auto st_type = std::dynamic_pointer_cast<struct_type>(elem_type)) {
            auto struct_model = st_type->get_struct();
            if (struct_model && struct_model->is_abstract()) {
                throw_error(0x4230, expr.first_lexeme(),
                    "Cannot create uniform array of abstract class '{}'",
                    {struct_model->get_short_name()});
                return;
            }
        }

        // Resolve the constructor / type-check for the uniform args
        if (auto st_type = std::dynamic_pointer_cast<struct_type>(elem_type)) {
            auto struct_model = st_type->get_struct();
            if (!struct_model) {
                throw_error(0x4225, expr.first_lexeme(),
                    "Cannot resolve struct for uniform 'new {}(...)[]': aggregate not resolved",
                    {st_type->to_string()});
                return;
            }
            auto [best_ctor, adapted_args] = get_best_matching_constructor(
                struct_model->constructors(), expr._uniform_ctor_args);
            if (!best_ctor) {
                throw_error(0x4231, expr.first_lexeme(),
                    "No matching constructor for uniform array init of type '{}'",
                    {st_type->to_string()});
                return;
            }
            check_constructor_visibility(*best_ctor, expr);
            expr._uniform_constructor = best_ctor;
            expr.set_uniform_ctor_args(adapted_args);
        } else if (type::is_primitive(elem_type)) {
            // Primitive: must have exactly one arg convertible to the element type
            if (expr._uniform_ctor_args.size() > 1) {
                throw_error(0x4232, expr.first_lexeme(),
                    "Uniform array init for primitive type '{}' expects at most one argument, got {}",
                    {elem_type->to_string(), std::to_string(expr._uniform_ctor_args.size())});
                return;
            }
            if (!expr._uniform_ctor_args.empty() && expr._uniform_ctor_args[0]) {
                auto cast = adapt_type(expr._uniform_ctor_args[0], elem_type);
                if (!cast) {
                    throw_error(0x4232, expr.first_lexeme(),
                        "Cannot convert uniform init value to primitive element type '{}'",
                        {elem_type->to_string()});
                    return;
                }
                if (cast != expr._uniform_ctor_args[0]) {
                    expr.assign_uniform_ctor_arg(0, cast);
                }
            }
        }

        if (is_dynamic) {
            expr._is_dynamic_size = true;
            expr._array_size = 0;
            auto arr_type_unsized = elem_type->get_array();
            expr.set_type(arr_type_unsized->get_owner());
        } else {
            expr._array_size = arr_size;
            auto arr_type_unsized = elem_type->get_array();
            auto sized_arr_type = arr_type_unsized->with_size(arr_size);
            expr.set_type(sized_arr_type->get_owner());
        }
        return;
    }

    if (expr.is_array()) {
        // ── Array form: new T[N]{e0, e1, ...} ──

        // Resolve array size expression
        if (expr._array_size_expr) {
            expr._array_size_expr->accept(*this);
        }

        // Resolve element init expressions
        // For function_invocation_expression elements, only resolve their arguments
        // (the callee is a struct name, not a function — constructor resolution happens below)
        for (size_t i = 0; i < expr._array_init_elements.size(); ++i) {
            if (auto& e = expr._array_init_elements[i]) {
                auto func_inv = std::dynamic_pointer_cast<function_invocation_expression>(e);
                if (func_inv) {
                    // Only resolve the arguments, not the callee
                    for (auto& arg : func_inv->arguments()) {
                        if (arg) arg->accept(*this);
                    }
                } else {
                    e->accept(*this);
                }
            }
        }

        // Resolve the element type
        auto elem_type = expr.allocated_type();
        if (!type::is_resolved(elem_type)) {
            if (auto unres = std::dynamic_pointer_cast<unresolved_type>(elem_type)) {
                auto resolved = resolve_type_by_name(unres->type_id(), static_cast<const element&>(expr));
                if (!resolved || !type::is_resolved(resolved)) {
                    auto imported_agg = _unit.get_or_create_imported_aggregate(unres->type_id(), _context);
                    if (imported_agg) resolved = imported_agg->get_struct_type();
                }
                if (resolved && type::is_resolved(resolved)) {
                    expr.allocated_type(resolved);
                    elem_type = resolved;
                }
            }
        }
        if (!type::is_resolved(elem_type)) {
            throw_error(0x0055, expr.first_lexeme(),
                "Cannot resolve element type of 'new[]' expression: type '{}' is unknown",
                {elem_type ? elem_type->to_string() : "<null>"});
            return;
        }

        // Determine the array size
        size_t init_count = expr._array_init_elements.size();
        size_t arr_size = 0;
        bool has_explicit_size = (expr._array_size_expr != nullptr);
        bool is_dynamic = false;

        if (has_explicit_size) {
            // Try to evaluate the size expression as a compile-time constant
            auto size_val = std::dynamic_pointer_cast<value_expression>(expr._array_size_expr);
            if (size_val && size_val->is_literal()
                && std::holds_alternative<lex::integer>(size_val->any_literal())) {
                auto& int_lit = size_val->any_literal().get<lex::integer>();
                arr_size = int_lit.to_unsigned_int();
            } else {
                // ── Dynamic size: runtime expression ──
                is_dynamic = true;

                // The size expression must be convertible to unsigned int
                auto uint_type = _context->from_type(primitive_type::UNSIGNED_INT);
                auto adapted_size = adapt_type(expr._array_size_expr, uint_type);
                if (!adapted_size) {
                    throw_error(0x4221, expr.first_lexeme(),
                        "Array size expression for 'new[]' must be convertible to an unsigned integer; "
                        "expression has type '{}'",
                        {expr._array_size_expr->get_type() ? expr._array_size_expr->get_type()->to_string() : "?"});
                    return;
                }
                if (adapted_size != expr._array_size_expr) {
                    expr._array_size_expr = adapted_size;
                    adapted_size->set_parent_expression(expr.shared_as<expression>());
                }

                // Brace initializers are not allowed for dynamic-sized arrays
                if (expr._has_brace_init) {
                    throw_error(0x422A, expr.first_lexeme(),
                        "Brace initializer lists are not allowed for dynamically-sized 'new[]' arrays; "
                        "all elements will be default-initialized");
                    return;
                }
            }
        } else {
            // Size inferred from init list (or empty brace init → 0 elements)
            arr_size = init_count;
            if (arr_size == 0 && !expr._has_brace_init) {
                // new T[] with no brace init at all → cannot infer the size
                throw_error(0x4229, expr.first_lexeme(),
                    "Cannot infer array size for 'new[]': "
                    "either provide an explicit size or a brace initializer list");
                return;
            }
            // new T[]{} → arr_size == 0 is valid (empty array)
        }

        if (is_dynamic) {
            // ── Dynamic-sized array: new T[expr] ──
            // No brace init, no static size. All elements default-initialized.
            expr._is_dynamic_size = true;
            expr._array_size = 0; // not meaningful for dynamic

            // For struct element types, resolve the default constructor
            if (auto st_type = std::dynamic_pointer_cast<struct_type>(elem_type)) {
                auto struct_model = st_type->get_struct();
                if (!struct_model) {
                    throw_error(0x4225, expr.first_lexeme(),
                        "Cannot resolve struct for 'new {}[]': aggregate not resolved",
                        {st_type->to_string()});
                    return;
                }
                if (struct_model->is_abstract()) {
                    throw_error(0x4226, expr.first_lexeme(),
                        "Cannot 'new' array of abstract class '{}'",
                        {struct_model->get_short_name()});
                    return;
                }
                // Resolve default constructor (needed for each element)
                auto [default_ctor, default_args] = get_best_matching_constructor(
                    struct_model->constructors(), std::vector<std::shared_ptr<expression>>{});
                // Store in element_constructors[0] as the single default ctor to use
                expr._element_constructors.resize(1, nullptr);
                expr._element_constructors[0] = default_ctor;
            }

            // Result type: owner<array_type> (unsized) → T[]!
            auto arr_type_unsized = elem_type->get_array();
            expr.set_type(arr_type_unsized->get_owner());
            return;
        }

        // ── Static-sized array: new T[N]{...} ──

        // Validate init count vs array size
        if (init_count > arr_size) {
            throw_error(0x4222, expr.first_lexeme(),
                "Array initializer list for 'new {}[{}]' has {} elements: too many initializers",
                {elem_type->to_string(), std::to_string(arr_size), std::to_string(init_count)});
            return;
        }
        if (init_count < arr_size && init_count > 0) {
            warn(0x4223,
                "Array initializer list for 'new {}[{}]' has only {} elements: "
                "remaining {} elements will be default-initialized",
                {elem_type->to_string(), std::to_string(arr_size), std::to_string(init_count),
                 std::to_string(arr_size - init_count)});
        }

        expr._array_size = arr_size;

        // Type-check and adapt each element + resolve constructors
        expr._element_constructors.resize(arr_size, nullptr);

        if (type::is_primitive(elem_type)) {
            for (size_t i = 0; i < init_count; ++i) {
                auto e = expr._array_init_elements[i];
                if (!e) continue;
                auto cast = adapt_type(e, elem_type);
                if (!cast) {
                    throw_error(0x4224, expr.first_lexeme(),
                        "Cannot convert element {} to type '{}' in 'new[]' initializer",
                        {std::to_string(i), elem_type->to_string()});
                } else if (cast != e) {
                    expr.assign_array_init_element(i, cast);
                }
            }
        } else if (auto st_type = std::dynamic_pointer_cast<struct_type>(elem_type)) {
            auto struct_model = st_type->get_struct();
            if (!struct_model) {
                throw_error(0x4225, expr.first_lexeme(),
                    "Cannot resolve struct for 'new {}[]': aggregate not resolved",
                    {st_type->to_string()});
                return;
            }
            if (struct_model->is_abstract()) {
                throw_error(0x4226, expr.first_lexeme(),
                    "Cannot 'new' array of abstract class '{}'",
                    {struct_model->get_short_name()});
                return;
            }
            for (size_t i = 0; i < init_count; ++i) {
                auto e = expr._array_init_elements[i];
                if (!e) continue; // default-init

                auto func_inv = std::dynamic_pointer_cast<function_invocation_expression>(e);
                if (func_inv) {
                    // Explicit constructor call
                    std::vector<std::shared_ptr<expression>> ctor_args;
                    for (auto& arg : func_inv->arguments()) ctor_args.push_back(arg);
                    auto [best_ctor, adapted_args] = get_best_matching_constructor(struct_model->constructors(), ctor_args);
                    if (!best_ctor) {
                        throw_error(0x4227, expr.first_lexeme(),
                            "No matching constructor for element {} of type '{}' in 'new[]'",
                            {std::to_string(i), st_type->to_string()});
                    }
                    check_constructor_visibility(*best_ctor, expr);
                    expr._element_constructors[i] = best_ctor;
                    func_inv->assign_arguments(adapted_args);
                } else {
                    // Implicit single-param constructor
                    std::vector<std::shared_ptr<expression>> ctor_args = {e};
                    auto [best_ctor, adapted_args] = get_best_matching_constructor(struct_model->constructors(), ctor_args);
                    if (!best_ctor) {
                        throw_error(0x4228, expr.first_lexeme(),
                            "No matching single-parameter constructor for element {} of type '{}' "
                            "with argument type '{}' in 'new[]'",
                            {std::to_string(i), st_type->to_string(),
                             e->get_type() ? e->get_type()->to_string() : "?"});
                    }
                    check_constructor_visibility(*best_ctor, expr);
                    expr._element_constructors[i] = best_ctor;
                    if (!adapted_args.empty() && adapted_args[0] != e) {
                        expr.assign_array_init_element(i, adapted_args[0]);
                    }
                }
            }
            // For uninitialized elements, find default constructor
            // Only search if there are actually elements that need default-init
            bool needs_default_ctor = false;
            for (size_t i = 0; i < arr_size; ++i) {
                if (i >= init_count || !expr._array_init_elements[i]) {
                    needs_default_ctor = true;
                    break;
                }
            }
            if (needs_default_ctor) {
                auto [default_ctor, default_args] = get_best_matching_constructor(
                    struct_model->constructors(), std::vector<std::shared_ptr<expression>>{});
                for (size_t i = 0; i < arr_size; ++i) {
                    if (i >= init_count || !expr._array_init_elements[i]) {
                        expr._element_constructors[i] = default_ctor;
                    }
                }
            }
        }

        // Build the sized_array_type and set the expression type to owner<sized_array_type>
        auto arr_type_unsized = elem_type->get_array();
        auto sized_arr_type = arr_type_unsized->with_size(arr_size);
        expr.set_type(sized_arr_type->get_owner());

        return;
    }

    // ── Single-object form (unchanged) ──

    // Resolve arguments first
    for (auto& arg : expr.arguments()) {
        arg->accept(*this);
    }

    auto alloc_type = expr.allocated_type();
    if (!type::is_resolved(alloc_type)) {
        // Try to resolve unresolved type
        if (auto unres = std::dynamic_pointer_cast<unresolved_type>(alloc_type)) {
            auto resolved = resolve_type_by_name(unres->type_id(), static_cast<const element&>(expr));
            if (!resolved || !type::is_resolved(resolved)) {
                auto imported_agg = _unit.get_or_create_imported_aggregate(unres->type_id(), _context);
                if (imported_agg) resolved = imported_agg->get_struct_type();
            }
            if (resolved && type::is_resolved(resolved)) {
                expr.allocated_type(resolved);
                alloc_type = resolved;
            }
        }
    }

    if (!type::is_resolved(alloc_type)) {
        throw_error(0x0055, expr.first_lexeme(),
            "Cannot resolve the type of 'new' expression: type '{}' is unknown",
            {alloc_type ? alloc_type->to_string() : "<null>"});
    }

    // Set the expression type to owner<allocated_type>
    expr.set_type(alloc_type->get_owner());

    // If the allocated type is a struct, find the best matching constructor
    if (auto st_type = std::dynamic_pointer_cast<struct_type>(alloc_type)) {
        auto st = st_type->get_struct();
        if (!st) {
            throw_error(0x0056, expr.first_lexeme(),
                "Cannot 'new' a struct type '{}': the aggregate is not resolved",
                {st_type->to_string()});
        }
        if (st->is_abstract()) {
            throw_error(0x0057, expr.first_lexeme(),
                "Cannot 'new' abstract class '{}': abstract classes cannot be directly instantiated",
                {st->get_short_name()});
        }
        auto [best_ctor, adapted_args] = get_best_matching_constructor(st->constructors(), expr.arguments());
        if (!best_ctor) {
            throw_error(0x0058, expr.first_lexeme(),
                "No matching constructor found for 'new {}': none of the available constructors "
                "can be called with the provided arguments",
                {st_type->to_string()});
        }
        check_constructor_visibility(*best_ctor, expr);
        expr.set_constructor(best_ctor);
        expr.assign_arguments(adapted_args);
    } else if (type::is_primitive(alloc_type)) {
        // Primitive type: adapt the single argument if any
        if (!expr.arguments().empty()) {
            auto cast = adapt_type(expr.arguments()[0], alloc_type);
            if (cast && cast != expr.arguments()[0]) {
                expr.assign_argument(0, cast);
            }
        }
    }
}

void symbol_resolver::visit_delete_expression(delete_expression& expr) {
    if (expr.sub_expr()) expr.sub_expr()->accept(*this);
}

void type_reference_resolver::visit_delete_expression(delete_expression& expr) {
    if (expr.sub_expr()) {
        expr.sub_expr()->accept(*this);
    }
    // The sub-expression must be an owner (directly or via reference-to-owner)
    auto sub = expr.sub_expr();
    if (!sub) {
        throw_error(0x0059, expr.first_lexeme(), "'delete' requires an expression");
    }
    auto sub_type = sub->get_type();
    // Unwrap reference-to-owner if needed
    if (type::is_reference(sub_type)) {
        auto ref = std::dynamic_pointer_cast<reference_type>(sub_type);
        sub_type = ref->get_subtype();
    }
    if (!type::is_owner(sub_type)) {
        throw_error(0x005A, expr.first_lexeme(),
            "'delete' can only be applied to an owner ('!') type, got '{}'",
            {sub->get_type() ? sub->get_type()->to_string() : "<null>"});
    }
    // Result type is void
    expr.set_type(nullptr); // void
}

//
// Owner move expression
//

void symbol_resolver::visit_owner_move_expression(owner_move_expression& expr) {
    if (expr.sub_expr()) expr.sub_expr()->accept(*this);
}

void type_reference_resolver::visit_owner_move_expression(owner_move_expression& expr) {
    if (expr.sub_expr()) expr.sub_expr()->accept(*this);
    auto src_type = expr.sub_expr() ? expr.sub_expr()->get_type() : nullptr;
    if (!src_type) return;
    // Source must be ref<owner<T>> → result type is owner<T>
    if (type::is_reference(src_type)) {
        auto inner = std::dynamic_pointer_cast<reference_type>(src_type)->get_subtype();
        if (type::is_owner(inner)) {
            expr.set_type(inner);
            return;
        }
    }
    // If already owner<T> (e.g., wrapping a new_expression rvalue), pass through
    if (type::is_owner(src_type)) {
        expr.set_type(src_type);
    }
}

void implementation_generator::visit_owner_move_expression(owner_move_expression& expr) {
    _value = nullptr;
    if (expr.sub_expr()) expr.sub_expr()->accept(*this);
    if (!_value) return;

    auto& llvm_ctx = _builder->getContext();
    auto* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);

    auto src_type = expr.sub_expr() ? expr.sub_expr()->get_type() : nullptr;
    if (src_type && type::is_reference(src_type)) {
        // Source is ref<owner<T>>: _value is the alloca (pointer to owner slot)
        llvm::Value* alloca_ptr = _value;
        // Load the raw owner pointer from the alloca
        _value = _builder->CreateLoad(ptr_ty, alloca_ptr, "own_move_val");
        // Null out the source alloca (ownership transferred)
        _builder->CreateStore(llvm::ConstantPointerNull::get(ptr_ty), alloca_ptr);
    }
    // else: _value is already the raw owner pointer (e.g., from new_expression)
}

//
// Array init expression
//

void symbol_resolver::visit_array_init_expression(array_init_expression& expr) {
    if (expr.constructed_symbol()) expr.constructed_symbol()->accept(*this);
    if (expr.is_uniform()) {
        for (auto& a : expr.uniform_ctor_args()) {
            if (a) a->accept(*this);
        }
    } else {
        for (size_t i = 0; i < expr.size(); ++i) {
            if (auto e = expr.element(i)) e->accept(*this);
        }
    }
}

void symbol_resolver::visit_designated_struct_init_expression(designated_struct_init_expression& expr) {
    if (expr.constructed_symbol()) expr.constructed_symbol()->accept(*this);
    for (auto& m : expr.members_mutable()) {
        if (m.value) m.value->accept(*this);
        for (auto& a : m.args) {
            if (a) a->accept(*this);
        }
    }
}

void type_reference_resolver::visit_array_init_expression(array_init_expression& expr) {
    if (expr.is_uniform()) {
        // ── Uniform array init: var : T(args)[N]; ──
        // Resolve uniform ctor args
        for (auto& a : expr._uniform_ctor_args) {
            if (a) a->accept(*this);
        }

        auto var_def = expr.constructed_symbol() ? expr.constructed_symbol()->get_variable_def() : nullptr;
        if (!var_def) return;
        auto var_type = var_def->get_type();
        if (!type::is_sized_array(var_type)) return;

        auto arr_type = std::dynamic_pointer_cast<sized_array_type>(var_type);
        auto elem_type = arr_type->get_subtype();
        size_t arr_size = arr_type->get_size();

        // Update stored array size
        expr._array_size = arr_size;

        // Check for abstract types
        if (auto st_type = std::dynamic_pointer_cast<struct_type>(elem_type)) {
            auto struct_model = st_type->get_struct();
            if (struct_model && struct_model->is_abstract()) {
                throw_error(0x4230, expr.first_lexeme(),
                    "Cannot create uniform array of abstract class '{}'",
                    {struct_model->get_short_name()});
                return;
            }
        }

        // Resolve the constructor / type-check for the uniform args
        if (auto st_type = std::dynamic_pointer_cast<struct_type>(elem_type)) {
            auto struct_model = st_type->get_struct();
            if (!struct_model) return;
            auto [best_ctor, adapted_args] = get_best_matching_constructor(
                struct_model->constructors(), expr._uniform_ctor_args);
            if (!best_ctor) {
                throw_error(0x4231, expr.first_lexeme(),
                    "No matching constructor for uniform array init of type '{}'",
                    {st_type->to_string()});
                return;
            }
            expr._uniform_constructor = best_ctor;
            expr.set_uniform_ctor_args(adapted_args);
        } else if (type::is_primitive(elem_type)) {
            // Primitive: must have exactly one arg convertible to the element type
            if (expr._uniform_ctor_args.size() > 1) {
                throw_error(0x4232, expr.first_lexeme(),
                    "Uniform array init for primitive type '{}' expects at most one argument, got {}",
                    {elem_type->to_string(), std::to_string(expr._uniform_ctor_args.size())});
                return;
            }
            if (!expr._uniform_ctor_args.empty() && expr._uniform_ctor_args[0]) {
                auto cast = adapt_type(expr._uniform_ctor_args[0], elem_type);
                if (!cast) {
                    throw_error(0x4232, expr.first_lexeme(),
                        "Cannot convert uniform init value to primitive element type '{}'",
                        {elem_type->to_string()});
                    return;
                }
                if (cast != expr._uniform_ctor_args[0]) {
                    expr.assign_uniform_ctor_arg(0, cast);
                }
            }
        }
        return;
    }

    // Resolve sub-expressions
    for (size_t i = 0; i < expr.size(); ++i) {
        if (auto e = expr.element(i)) e->accept(*this);
    }

    // Get the array variable's type
    auto var_def = expr.constructed_symbol() ? expr.constructed_symbol()->get_variable_def() : nullptr;
    if (!var_def) return;
    auto var_type = var_def->get_type();
    if (!type::is_sized_array(var_type)) return;

    auto arr_type = std::dynamic_pointer_cast<sized_array_type>(var_type);
    auto elem_type = arr_type->get_subtype();

    // Validate element count
    size_t arr_size = arr_type->get_size();
    size_t init_count = expr.size();

    if (init_count > arr_size) {
        throw_error(0x4210, expr.first_lexeme(),
            "Array initializer list has {} elements, but the array '{}' has size {}: too many initializers",
            {std::to_string(init_count), var_def->get_fq_name(), std::to_string(arr_size)});
        return;
    }
    if (init_count < arr_size && init_count > 0) {
        warn(0x4211,
            "Array initializer list has {} elements, but the array '{}' has size {}: "
            "remaining {} elements will be default-initialized",
            {std::to_string(init_count), var_def->get_fq_name(), std::to_string(arr_size),
             std::to_string(arr_size - init_count)});
    }

    // Type-check and adapt each element
    if (type::is_primitive(elem_type)) {
        for (size_t i = 0; i < init_count; ++i) {
            auto e = expr.element(i);
            if (!e) continue; // default-init slot
            auto cast = adapt_type(e, elem_type);
            if (!cast) {
                throw_error(0x4212, expr.first_lexeme(),
                    "Cannot convert array element {} to type '{}' for array '{}'",
                    {std::to_string(i), elem_type->to_string(), var_def->get_fq_name()});
            } else if (cast != e) {
                expr.assign_element(i, cast);
            }
        }
    } else if (type::is_any_indirection(elem_type)) {
        // Indirection element types (link, pointer, view, owner):
        // each init expression must be adaptable to the element indirection type.
        for (size_t i = 0; i < init_count; ++i) {
            auto e = expr.element(i);
            if (!e) continue;
            auto cast = adapt_type(e, elem_type);
            if (!cast) {
                throw_error(0x4212, expr.first_lexeme(),
                    "Cannot convert array element {} to indirection type '{}' for array '{}'",
                    {std::to_string(i), elem_type->to_string(), var_def->get_fq_name()});
            } else if (cast != e) {
                expr.assign_element(i, cast);
            }
        }
    } else if (auto st_type = std::dynamic_pointer_cast<struct_type>(elem_type)) {
        auto struct_model = st_type->get_struct();
        for (size_t i = 0; i < init_count; ++i) {
            auto e = expr.element(i);
            if (!e) continue; // default-init slot, will use default ctor

            // Check if element is a function invocation (explicit constructor call)
            // The model_builder creates function_invocation_expression for Name(args...) patterns
            auto func_inv = std::dynamic_pointer_cast<function_invocation_expression>(e);
            if (func_inv) {
                // Explicit constructor call — resolve constructor
                std::vector<std::shared_ptr<expression>> ctor_args;
                for (auto& arg : func_inv->arguments()) {
                    ctor_args.push_back(arg);
                }
                auto [best_ctor, adapted_args] = get_best_matching_constructor(struct_model->constructors(), ctor_args);
                if (!best_ctor) {
                    throw_error(0x4213, expr.first_lexeme(),
                        "No matching constructor for array element {} of type '{}'",
                        {std::to_string(i), st_type->to_string()});
                }
                // Store ctor info in the expression for later use by codegen
                // For now, adapt the arguments
                func_inv->assign_arguments(adapted_args);
            } else {
                // Implicit single-param constructor
                std::vector<std::shared_ptr<expression>> ctor_args = {e};
                auto [best_ctor, adapted_args] = get_best_matching_constructor(struct_model->constructors(), ctor_args);
                if (!best_ctor) {
                    throw_error(0x4214, expr.first_lexeme(),
                        "No matching single-parameter constructor for array element {} of type '{}' "
                        "with argument type '{}'",
                        {std::to_string(i), st_type->to_string(),
                         e->get_type() ? e->get_type()->to_string() : "?"});
                }
                if (!adapted_args.empty() && adapted_args[0] != e) {
                    expr.assign_element(i, adapted_args[0]);
                }
             }
        }
    }
}

void type_reference_resolver::visit_designated_struct_init_expression(designated_struct_init_expression& expr) {
    // Resolve sub-expressions in each member initializer
    for (auto& m : expr.members_mutable()) {
        if (m.value) m.value->accept(*this);
        for (auto& a : m.args) {
            if (a) a->accept(*this);
        }
    }

    // Determine the target struct type.
    // For top-level designated inits, derive from the constructed variable's type.
    // For nested designated inits (no constructed_symbol), _target_aggregate is pre-set by the parent.
    std::shared_ptr<struct_type> st_type;
    std::shared_ptr<aggregate> target_struct = expr._target_aggregate;

    if (!target_struct) {
        auto var_def = expr.constructed_symbol() ? expr.constructed_symbol()->get_variable_def() : nullptr;
        if (!var_def) return;
        auto var_type = var_def->get_type();

        // Unwrap reference if needed
        if (type::is_reference(var_type)) {
            var_type = std::dynamic_pointer_cast<reference_type>(var_type)->get_subtype();
        }

        st_type = std::dynamic_pointer_cast<struct_type>(var_type);
        if (!st_type) {
            throw_error(0x4250, expr.first_lexeme(),
                "Designated initializer can only be used with struct types, but '{}' has type '{}'",
                {var_def->get_fq_name(), var_type ? var_type->to_string() : "?"});
            return;
        }
        target_struct = st_type->get_struct();
        if (!target_struct) return;

        // Only valid for structs, not classes with virtual inheritance
        if (target_struct->is_class() && target_struct->has_virtual_bases()) {
            throw_error(0x4251, expr.first_lexeme(),
                "Designated initializer cannot be used with class '{}' which has virtual bases",
                {target_struct->get_short_name()});
            return;
        }

        // Store the resolved aggregate in the expression
        expr._target_aggregate = target_struct;
    } else {
        // Nested designated init: _target_aggregate already set
        st_type = target_struct->get_struct_type();
    }

    // Collect all accessible member variables from the struct and its bases
    // Map: member_name -> (member_var, owning_aggregate)
    struct member_info {
        std::shared_ptr<member_variable_definition> var;
        std::shared_ptr<aggregate> owner;
    };
    std::map<std::string, std::vector<member_info>> all_members;

    // Helper: skip synthetic members (__base_X__, __vbptr_X__, __vbase_X__, __parent__)
    auto is_synthetic = [](const std::string& name) {
        return name.size() >= 4 && name[0] == '_' && name[1] == '_'
            && name[name.size()-1] == '_' && name[name.size()-2] == '_';
    };

    // Gather members from the struct itself
    for (auto& [name, var] : target_struct->variables()) {
        auto mem = std::dynamic_pointer_cast<member_variable_definition>(var);
        if (!mem) continue;
        if (is_synthetic(name)) continue;
        all_members[name].push_back({mem, target_struct});
    }

    // Gather inherited members from base classes
    auto all_bases = target_struct->get_all_bases();
    for (auto& base : all_bases) {
        if (!base.base) continue;
        for (auto& [name, var] : base.base->variables()) {
            auto mem = std::dynamic_pointer_cast<member_variable_definition>(var);
            if (!mem) continue;
            if (is_synthetic(name)) continue;
            all_members[name].push_back({mem, base.base});
        }
    }

    // Validate and resolve each designated member
    std::set<std::string> seen_members;
    for (auto& m : expr.members_mutable()) {
        std::string full_name = m.qualifier.empty()
            ? m.member_name
            : m.qualifier + "::" + m.member_name;

        // Check for duplicate designators
        if (seen_members.count(full_name)) {
            throw_error(0x4252, expr.first_lexeme(),
                "Duplicate designated initializer for member '.{}'",
                {full_name});
            continue;
        }
        seen_members.insert(full_name);

        // Find the member
        auto it = all_members.find(m.member_name);
        if (it == all_members.end()) {
            throw_error(0x4253, expr.first_lexeme(),
                "No member '{}' in struct '{}' for designated initializer",
                {m.member_name, target_struct->get_short_name()});
            continue;
        }

        auto& candidates = it->second;

        // Resolve ambiguity using qualifier if needed
        std::shared_ptr<member_variable_definition> resolved_mem;
        std::shared_ptr<aggregate> resolved_owner;
        if (candidates.size() > 1 && m.qualifier.empty()) {
            throw_error(0x4254, expr.first_lexeme(),
                "Ambiguous member '{}' in struct '{}': "
                "use a qualified name (e.g. '.Base::{}') to disambiguate",
                {m.member_name, target_struct->get_short_name(), m.member_name});
            continue;
        } else if (!m.qualifier.empty()) {
            // Find the candidate matching the qualifier
            bool found = false;
            for (auto& cand : candidates) {
                if (cand.owner->get_short_name() == m.qualifier) {
                    resolved_mem = cand.var;
                    resolved_owner = cand.owner;
                    found = true;
                    break;
                }
            }
            if (!found) {
                throw_error(0x4255, expr.first_lexeme(),
                    "No member '{}::{}' found in struct '{}'",
                    {m.qualifier, m.member_name, target_struct->get_short_name()});
                continue;
            }
        } else {
            resolved_mem = candidates[0].var;
            resolved_owner = candidates[0].owner;
        }

        // Check accessibility
        if (resolved_mem->get_visibility() == PRIVATE || resolved_mem->get_visibility() == PROTECTED) {
            throw_error(0x4256, expr.first_lexeme(),
                "Member '{}' is {} in struct '{}' and cannot be used in a designated initializer",
                {full_name,
                 resolved_mem->get_visibility() == PRIVATE ? "private" : "protected",
                 target_struct->get_short_name()});
            continue;
        }

        m.resolved_member = resolved_mem;
        m.resolved_owner = resolved_owner;

        // Type-check the initializer value
        auto member_type = resolved_mem->get_type();
        if (m.is_call_form) {
            // Constructor form: .member(args...)
            if (auto mem_st_type = std::dynamic_pointer_cast<struct_type>(member_type)) {
                auto mem_struct = mem_st_type->get_struct();
                if (mem_struct) {
                    auto [best_ctor, adapted_args] = get_best_matching_constructor(
                        mem_struct->constructors(), m.args);
                    if (!best_ctor) {
                        throw_error(0x4257, expr.first_lexeme(),
                            "No matching constructor for member '{}' of type '{}'",
                            {full_name, mem_st_type->to_string()});
                    } else {
                        m.resolved_constructor = best_ctor;
                        m.args = adapted_args;
                    }
                }
            } else {
                // Primitive or indirection: constructor form is like a single-arg init
                if (m.args.size() != 1) {
                    throw_error(0x4258, expr.first_lexeme(),
                        "Constructor form for non-aggregate member '{}' of type '{}' expects exactly one argument",
                        {full_name, member_type ? member_type->to_string() : "?"});
                } else if (m.args[0]) {
                    auto cast = adapt_type(m.args[0], member_type);
                    if (!cast) {
                        throw_error(0x4259, expr.first_lexeme(),
                            "Cannot convert argument to type '{}' for member '{}'",
                            {member_type->to_string(), full_name});
                    } else if (cast != m.args[0]) {
                        m.args[0] = cast;
                    }
                }
            }
        } else {
            // Assignment form: .member = expr
            if (m.value) {
                // Check if value is a brace_init_list (nested designated init) — handled as sub-struct
                if (auto desig_sub = std::dynamic_pointer_cast<designated_struct_init_expression>(m.value)) {
                    // Pre-set target aggregate from the member type for nested resolution
                    if (auto mem_st_type = std::dynamic_pointer_cast<struct_type>(member_type)) {
                        desig_sub->_target_aggregate = mem_st_type->get_struct();
                    }
                    // Resolve recursively
                    desig_sub->accept(*this);
                } else {
                    auto cast = adapt_type(m.value, member_type);
                    if (!cast) {
                        throw_error(0x425A, expr.first_lexeme(),
                            "Cannot convert initializer value to type '{}' for member '{}'",
                            {member_type ? member_type->to_string() : "?", full_name});
                    } else if (cast != m.value) {
                        m.value = cast;
                    }
                }
            }
        }
    }
}

void implementation_generator::visit_array_init_expression(array_init_expression& expr) {
    auto var_def = expr.constructed_symbol() ? expr.constructed_symbol()->get_variable_def() : nullptr;
    if (!var_def) return;

    auto var_type = var_def->get_type();
    auto arr_type = std::dynamic_pointer_cast<sized_array_type>(var_type);
    if (!arr_type) return;

    // Get the alloca for the array variable
    _value = nullptr;
    expr.constructed_symbol()->accept(*this);
    llvm::Value* arr_alloca = _value;
    _value = nullptr;
    if (!arr_alloca) return;

    auto* struct_llvm = arr_type->get_llvm_struct_type();
    auto elem_type = arr_type->get_subtype();
    llvm::Type* llvm_elem_type = _context->get_llvm_type(elem_type);
    size_t arr_size = arr_type->get_size();

    // Zero-init the entire array struct first
    _builder->CreateStore(llvm::ConstantAggregateZero::get(struct_llvm), arr_alloca);

    // Write the element count into field 0
    llvm::Value* size_ptr = _builder->CreateStructGEP(struct_llvm, arr_alloca,
        sized_array_type::FIELD_SIZE, "arr_size");
    _builder->CreateStore(
        llvm::ConstantInt::get(llvm::Type::getInt32Ty(_builder->getContext()),
            arr_size, false),
        size_ptr);

    // Get pointer to the data array (field 1)
    llvm::Value* data_ptr = _builder->CreateStructGEP(struct_llvm, arr_alloca,
        sized_array_type::FIELD_DATA, "arr_data");
    auto* llvm_arr_type = arr_type->get_llvm_data_array_type();

    if (expr.is_uniform()) {
        // ── Uniform mode: initialize all elements with the same ctor args ──
        for (size_t i = 0; i < arr_size; ++i) {
            llvm::Value* elem_ptr = _builder->CreateConstInBoundsGEP2_32(
                llvm_arr_type, data_ptr, 0, i, "uarr_elem_" + std::to_string(i));

            if (auto st_type = std::dynamic_pointer_cast<struct_type>(elem_type)) {
                auto ctor = expr.uniform_constructor();
                if (ctor) {
                    auto ctor_it = _context->_functions.find(ctor->shared_as<function>());
                    if (ctor_it != _context->_functions.end()) {
                        std::vector<llvm::Value*> args;
                        args.push_back(elem_ptr);
                        for (auto& arg : expr.uniform_ctor_args()) {
                            _value = nullptr;
                            arg->accept(*this);
                            if (_value) args.push_back(_value);
                        }
                        _builder->CreateCall(ctor_it->second, args);
                    }
                }
            } else if (type::is_primitive(elem_type)) {
                if (!expr.uniform_ctor_args().empty() && expr.uniform_ctor_args()[0]) {
                    _value = nullptr;
                    expr.uniform_ctor_args()[0]->accept(*this);
                    if (_value) _builder->CreateStore(_value, elem_ptr);
                }
                // else: already zero-inited
            }
        }
    } else {
        // ── Per-element mode (existing behavior) ──
        // Initialize each element
        for (size_t i = 0; i < expr.size() && i < arr_size; ++i) {
            auto elem_expr = expr.element(i);
            if (!elem_expr) continue; // default-init (already zeroed)

            llvm::Value* elem_ptr = _builder->CreateConstInBoundsGEP2_32(
                llvm_arr_type, data_ptr, 0, i, "arr_elem_" + std::to_string(i));

            if (type::is_primitive(elem_type) || type::is_any_indirection(elem_type)) {
                _value = nullptr;
                elem_expr->accept(*this);
                if (_value) {
                    _builder->CreateStore(_value, elem_ptr);
                }
            } else if (auto st_type = std::dynamic_pointer_cast<struct_type>(elem_type)) {
                // For struct elements, we need to call the constructor
                _value = nullptr;
                elem_expr->accept(*this);
                if (_value) {
                    _builder->CreateStore(_value, elem_ptr);
                }
            }
        }
    }
    _value = arr_alloca;
}

void implementation_generator::visit_designated_struct_init_expression(designated_struct_init_expression& expr) {
    auto target_struct = expr.target_aggregate();
    if (!target_struct) return;

    std::shared_ptr<struct_type> st_type;
    llvm::Value* struct_alloca = nullptr;

    if (expr.constructed_symbol()) {
        // Top-level designated init: get alloca from variable
        auto var_def = expr.constructed_symbol()->get_variable_def();
        if (!var_def) return;
        st_type = std::dynamic_pointer_cast<struct_type>(var_def->get_type());
        if (!st_type) return;

        _value = nullptr;
        expr.constructed_symbol()->accept(*this);
        struct_alloca = _value;
        _value = nullptr;
        if (!struct_alloca) return;

        // Zero-init the entire struct first (default initialization for all members)
        auto* llvm_type = st_type->get_llvm_type();
        if (llvm_type) {
            _builder->CreateStore(llvm::ConstantAggregateZero::get(llvm_type), struct_alloca);
        }
    } else {
        // Nested designated init: _value is the pre-computed mem_ptr from outer caller
        struct_alloca = _value;
        if (!struct_alloca) return;
        st_type = target_struct->get_struct_type();
        if (!st_type) return;
        _value = nullptr;

        // Zero-init the nested struct sub-object
        auto* llvm_type = st_type->get_llvm_type();
        if (llvm_type) {
            _builder->CreateStore(llvm::ConstantAggregateZero::get(llvm_type), struct_alloca);
        }
    }

    // ── Helper: DFS to find GEP path from a source aggregate to a target aggregate
    //    through __base_X__ sub-objects. Returns the sequence of (struct_type, field_index)
    //    pairs to GEP through.
    struct GepStep {
        llvm::Type* llvm_type;
        unsigned field_index;
        std::string name;
    };
    std::function<bool(aggregate*, struct_type*, aggregate*, std::vector<GepStep>&)> find_base_path;
    find_base_path = [&](aggregate* cur_agg, struct_type* cur_st, aggregate* target_agg,
                          std::vector<GepStep>& path) -> bool {
        if (cur_agg == target_agg) return true;
        for (auto& bs : cur_agg->get_bases()) {
            if (!bs.base || bs.is_virtual) continue;
            std::string field_name = "__base_" + bs.sanitised_name() + "__";
            auto field = cur_st->get_member(field_name);
            if (!field) continue;
            auto base_st = bs.base->get_struct_type();
            if (!base_st) continue;
            path.push_back(GepStep{cur_st->get_llvm_type(), (unsigned)field->index, field_name});
            if (find_base_path(bs.base.get(), base_st.get(), target_agg, path)) {
                return true;
            }
            path.pop_back();
        }
        return false;
    };

    // ── Helper: Get a pointer to a member, navigating through base sub-objects if needed.
    //    Returns nullptr if the path cannot be found.
    auto get_member_ptr = [&](const std::string& member_name,
                               aggregate* member_owner,
                               const std::string& label_prefix) -> llvm::Value* {
        if (member_owner == target_struct.get()) {
            // Direct member of the target struct
            auto field = st_type->get_member(member_name);
            if (!field) return nullptr;
            return _builder->CreateStructGEP(st_type->get_llvm_type(), struct_alloca, field->index,
                                              label_prefix + member_name);
        }
        // Inherited member: navigate __base_X__ chain
        std::vector<GepStep> path;
        if (!find_base_path(target_struct.get(), st_type.get(), member_owner, path)) {
            return nullptr;
        }
        // Walk the GEP path to reach the base sub-object
        llvm::Value* ptr = struct_alloca;
        for (auto& step : path) {
            ptr = _builder->CreateStructGEP(step.llvm_type, ptr, step.field_index,
                                             label_prefix + step.name);
        }
        // Now GEP to the field within the base sub-object
        auto owner_st_type = member_owner->get_struct_type();
        if (!owner_st_type) return nullptr;
        auto field = owner_st_type->get_member(member_name);
        if (!field) return nullptr;
        return _builder->CreateStructGEP(owner_st_type->get_llvm_type(), ptr, field->index,
                                          label_prefix + member_name);
    };

    // Build set of designated member keys for quick lookup
    // Use "qualifier::name" as key to handle qualified members correctly
    std::set<std::string> designated_keys;
    for (auto& m : expr.members()) {
        std::string key = m.qualifier.empty() ? m.member_name : m.qualifier + "::" + m.member_name;
        designated_keys.insert(key);
    }

    // ── Helper: call default constructor for an aggregate-typed member
    auto call_default_ctor = [&](aggregate* mem_struct, llvm::Value* mem_ptr) {
        for (auto& ctor : mem_struct->constructors()) {
            if (ctor->parameters().empty() ||
                (ctor->parameters().size() == 1 /* this */)) {
                auto ctor_it = _context->_functions.find(ctor->shared_as<function>());
                if (ctor_it != _context->_functions.end()) {
                    _builder->CreateCall(ctor_it->second, {mem_ptr});
                }
                break;
            }
        }
    };

    // Helper: skip synthetic members (__base_X__, __vbptr_X__, __vbase_X__, __parent__)
    auto is_synthetic = [](const std::string& name) {
        return name.size() >= 4 && name[0] == '_' && name[1] == '_'
            && name[name.size()-1] == '_' && name[name.size()-2] == '_';
    };

    // For each member in the struct (and its bases) that has a default constructor
    // and is NOT designated, call its default constructor.
    // Process direct members first
    for (auto& [name, var] : target_struct->variables()) {
        auto mem = std::dynamic_pointer_cast<member_variable_definition>(var);
        if (!mem) continue;
        if (is_synthetic(name)) continue;
        if (designated_keys.count(name)) continue;

        auto mem_type = mem->get_type();
        if (auto mem_st_type = std::dynamic_pointer_cast<struct_type>(mem_type)) {
            auto mem_struct = mem_st_type->get_struct();
            if (mem_struct) {
                auto field = st_type->get_member(name);
                if (field) {
                    llvm::Value* mem_ptr = _builder->CreateStructGEP(
                        st_type->get_llvm_type(), struct_alloca, field->index,
                        "desig_default_" + name);
                    call_default_ctor(mem_struct.get(), mem_ptr);
                }
            }
        }
    }

    // Process inherited members from all bases
    auto all_bases = target_struct->get_all_bases();
    for (auto& base : all_bases) {
        if (!base.base) continue;
        for (auto& [name, var] : base.base->variables()) {
            auto mem = std::dynamic_pointer_cast<member_variable_definition>(var);
            if (!mem) continue;
            if (is_synthetic(name)) continue;
            // Check if this member is designated (with or without qualifier)
            bool is_designated = designated_keys.count(name)
                || designated_keys.count(base.base->get_short_name() + "::" + name);
            if (is_designated) continue;

            auto mem_type = mem->get_type();
            if (auto mem_st_type = std::dynamic_pointer_cast<struct_type>(mem_type)) {
                auto mem_struct = mem_st_type->get_struct();
                if (mem_struct) {
                    llvm::Value* mem_ptr = get_member_ptr(name, base.base.get(), "desig_default_");
                    if (mem_ptr) {
                        call_default_ctor(mem_struct.get(), mem_ptr);
                    }
                }
            }
        }
    }

    // Now initialize each designated member
    for (auto& m : expr.members()) {
        auto resolved_mem = m.resolved_member;
        if (!resolved_mem) continue;

        // Determine the owning aggregate for this member
        aggregate* owner = m.resolved_owner ? m.resolved_owner.get() : target_struct.get();

        llvm::Value* mem_ptr = get_member_ptr(m.member_name, owner, "desig_");
        if (!mem_ptr) continue;

        auto mem_type = resolved_mem->get_type();

        if (m.is_call_form) {
            // Constructor form: .member(args...)
            if (m.resolved_constructor) {
                auto ctor_it = _context->_functions.find(m.resolved_constructor->shared_as<function>());
                if (ctor_it != _context->_functions.end()) {
                    std::vector<llvm::Value*> args;
                    args.push_back(mem_ptr); // 'this' pointer
                    for (auto& a : m.args) {
                        _value = nullptr;
                        if (a) a->accept(*this);
                        if (_value) args.push_back(_value);
                    }
                    _builder->CreateCall(ctor_it->second, args);
                }
            } else if (type::is_primitive(mem_type) && !m.args.empty() && m.args[0]) {
                _value = nullptr;
                m.args[0]->accept(*this);
                if (_value) _builder->CreateStore(_value, mem_ptr);
            }
        } else {
            // Assignment form: .member = expr
            if (m.value) {
                if (auto desig_sub = std::dynamic_pointer_cast<designated_struct_init_expression>(m.value)) {
                    // Nested designated init — pass mem_ptr as the alloca via _value
                    _value = mem_ptr;
                    desig_sub->accept(*this);
                } else {
                    _value = nullptr;
                    m.value->accept(*this);
                    if (_value) _builder->CreateStore(_value, mem_ptr);
                }
            }
        }
    }

    _value = struct_alloca;
}

void implementation_generator::visit_new_expression(new_expression& expr) {
    auto alloc_type = expr.allocated_type();
    if (!alloc_type) { _value = nullptr; return; }

    auto& llvm_ctx = _builder->getContext();
    auto* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);

    // Ensure malloc is declared
    llvm::Module& mod = get_module();
    llvm::Function* malloc_fn = mod.getFunction("malloc");
    if (!malloc_fn) {
        auto* malloc_type = llvm::FunctionType::get(
            ptr_ty,
            {llvm::Type::getInt64Ty(llvm_ctx)},
            false);
        malloc_fn = llvm::Function::Create(
            malloc_type, llvm::Function::ExternalLinkage, "malloc", mod);
    }

    if (expr.is_array() && expr.is_dynamic_size() && !expr.is_uniform_array()) {
        // ── Dynamic array form: new T[expr] ──
        // Size is a runtime value. No brace initializers. All elements default-initialized.
        auto elem_type = alloc_type;
        llvm::Type* llvm_elem_type = _context->get_llvm_type(elem_type);

        // Get the unsized array_type from the expression type (owner<array_type>)
        auto own_type = std::dynamic_pointer_cast<owner_type>(expr.get_type());
        auto unsized_arr_type = own_type
            ? std::dynamic_pointer_cast<array_type>(own_type->get_owned_type())
            : nullptr;
        if (!unsized_arr_type) { _value = nullptr; return; }

        auto* struct_llvm = unsized_arr_type->get_llvm_struct_type();
        auto* llvm_arr_type = unsized_arr_type->get_llvm_data_array_type();
        if (!struct_llvm || !llvm_arr_type) { _value = nullptr; return; }

        // Evaluate the size expression → i32 runtime value
        _value = nullptr;
        expr.array_size_expr()->accept(*this);
        llvm::Value* n_val = _value; // i32
        if (!n_val) { _value = nullptr; return; }

        // Compute allocation size: header_size + sizeof(T) * n
        // header_size = offset of field 1 in { i32, [0 x T] }
        auto* i64_ty = llvm::Type::getInt64Ty(llvm_ctx);
        auto* i32_ty = llvm::Type::getInt32Ty(llvm_ctx);
        uint64_t header_size = mod.getDataLayout().getStructLayout(struct_llvm)->getElementOffset(array_type::FIELD_DATA);
        uint64_t elem_size = mod.getDataLayout().getTypeAllocSize(llvm_elem_type);

        llvm::Value* n_i64 = _builder->CreateZExt(n_val, i64_ty, "n_i64");
        llvm::Value* data_bytes = _builder->CreateMul(
            n_i64,
            llvm::ConstantInt::get(i64_ty, elem_size),
            "data_bytes");
        llvm::Value* alloc_size = _builder->CreateAdd(
            data_bytes,
            llvm::ConstantInt::get(i64_ty, header_size),
            "alloc_size");

        // malloc
        llvm::Value* raw_ptr = _builder->CreateCall(
            malloc_fn->getFunctionType(), malloc_fn, {alloc_size}, "new_dynarr_raw");

        // memset to zero
        _builder->CreateMemSet(raw_ptr,
            llvm::ConstantInt::get(llvm::Type::getInt8Ty(llvm_ctx), 0),
            alloc_size, llvm::MaybeAlign(1));

        // Store the element count in field 0
        llvm::Value* size_ptr = _builder->CreateStructGEP(struct_llvm, raw_ptr,
            array_type::FIELD_SIZE, "dynarr_size");
        _builder->CreateStore(n_val, size_ptr);

        // Get pointer to the data area (field 1)
        llvm::Value* data_ptr = _builder->CreateStructGEP(struct_llvm, raw_ptr,
            array_type::FIELD_DATA, "dynarr_data");

        // For struct element types with a constructor, emit an IR loop to call
        // the default constructor on each element.
        if (auto st_type = std::dynamic_pointer_cast<struct_type>(elem_type)) {
            auto default_ctor = (!expr.element_constructors().empty())
                ? expr.element_constructors()[0] : nullptr;
            if (default_ctor) {
                auto ctor_it = _context->_functions.find(default_ctor->shared_as<function>());
                if (ctor_it != _context->_functions.end()) {
                    // Emit IR loop: for (i = 0; i < n; ++i) ctor(&data[i])
                    auto* fn = _builder->GetInsertBlock()->getParent();
                    auto* loop_header = llvm::BasicBlock::Create(llvm_ctx, "dynarr_init_hdr", fn);
                    auto* loop_body   = llvm::BasicBlock::Create(llvm_ctx, "dynarr_init_body", fn);
                    auto* loop_end    = llvm::BasicBlock::Create(llvm_ctx, "dynarr_init_end", fn);

                    _builder->CreateBr(loop_header);

                    // Loop header: %i = phi [0, entry], [%i_next, body]; if i < n goto body else end
                    _builder->SetInsertPoint(loop_header);
                    auto* entry_bb = loop_header->getSinglePredecessor();
                    llvm::PHINode* i_phi = _builder->CreatePHI(i32_ty, 2, "dynarr_i");
                    i_phi->addIncoming(llvm::ConstantInt::get(i32_ty, 0), entry_bb);
                    llvm::Value* cmp = _builder->CreateICmpULT(i_phi, n_val, "dynarr_cmp");
                    _builder->CreateCondBr(cmp, loop_body, loop_end);

                    // Loop body: GEP to element, call ctor, increment
                    _builder->SetInsertPoint(loop_body);
                    llvm::Value* indices[] = {llvm::ConstantInt::get(i32_ty, 0), i_phi};
                    llvm::Value* elem_ptr = _builder->CreateGEP(
                        llvm_arr_type, data_ptr, indices, "dynarr_elem");
                    _builder->CreateCall(ctor_it->second, {elem_ptr});
                    llvm::Value* i_next = _builder->CreateAdd(
                        i_phi, llvm::ConstantInt::get(i32_ty, 1), "dynarr_i_next");
                    i_phi->addIncoming(i_next, loop_body);
                    _builder->CreateBr(loop_header);

                    _builder->SetInsertPoint(loop_end);
                }
            }
        }
        // For primitive types: memset already zero-initialized everything.

        _value = raw_ptr;
        return;
    }

    if (expr.is_uniform_array()) {
        // ── Uniform array form: new T(args)[N] ──
        auto elem_type = alloc_type;
        llvm::Type* llvm_elem_type = _context->get_llvm_type(elem_type);

        if (expr.is_dynamic_size()) {
            // ── Dynamic uniform array: new T(args)[expr] ──
            auto own_type = std::dynamic_pointer_cast<owner_type>(expr.get_type());
            auto unsized_arr_type = own_type
                ? std::dynamic_pointer_cast<array_type>(own_type->get_owned_type())
                : nullptr;
            if (!unsized_arr_type) { _value = nullptr; return; }

            auto* struct_llvm = unsized_arr_type->get_llvm_struct_type();
            auto* llvm_arr_type = unsized_arr_type->get_llvm_data_array_type();
            if (!struct_llvm || !llvm_arr_type) { _value = nullptr; return; }

            // Evaluate the size expression → i32 runtime value
            _value = nullptr;
            expr.array_size_expr()->accept(*this);
            llvm::Value* n_val = _value;
            if (!n_val) { _value = nullptr; return; }

            // Compute allocation size: header_size + sizeof(T) * n
            auto* i64_ty = llvm::Type::getInt64Ty(llvm_ctx);
            auto* i32_ty = llvm::Type::getInt32Ty(llvm_ctx);
            uint64_t header_size = mod.getDataLayout().getStructLayout(struct_llvm)->getElementOffset(array_type::FIELD_DATA);
            uint64_t elem_size = mod.getDataLayout().getTypeAllocSize(llvm_elem_type);

            llvm::Value* n_i64 = _builder->CreateZExt(n_val, i64_ty, "n_i64");
            llvm::Value* data_bytes = _builder->CreateMul(
                n_i64,
                llvm::ConstantInt::get(i64_ty, elem_size),
                "data_bytes");
            llvm::Value* alloc_size = _builder->CreateAdd(
                data_bytes,
                llvm::ConstantInt::get(i64_ty, header_size),
                "alloc_size");

            // malloc
            llvm::Value* raw_ptr = _builder->CreateCall(
                malloc_fn->getFunctionType(), malloc_fn, {alloc_size}, "new_uarr_raw");

            // memset to zero
            _builder->CreateMemSet(raw_ptr,
                llvm::ConstantInt::get(llvm::Type::getInt8Ty(llvm_ctx), 0),
                alloc_size, llvm::MaybeAlign(1));

            // Store the element count in field 0
            llvm::Value* size_ptr = _builder->CreateStructGEP(struct_llvm, raw_ptr,
                array_type::FIELD_SIZE, "uarr_size");
            _builder->CreateStore(n_val, size_ptr);

            // Get pointer to the data area (field 1)
            llvm::Value* data_ptr = _builder->CreateStructGEP(struct_llvm, raw_ptr,
                array_type::FIELD_DATA, "uarr_data");

            // Emit IR loop: for (i = 0; i < n; ++i) init element i with ctor args
            auto* fn = _builder->GetInsertBlock()->getParent();
            auto* loop_header = llvm::BasicBlock::Create(llvm_ctx, "uarr_init_hdr", fn);
            auto* loop_body   = llvm::BasicBlock::Create(llvm_ctx, "uarr_init_body", fn);
            auto* loop_end    = llvm::BasicBlock::Create(llvm_ctx, "uarr_init_end", fn);

            _builder->CreateBr(loop_header);

            _builder->SetInsertPoint(loop_header);
            auto* entry_bb = loop_header->getSinglePredecessor();
            llvm::PHINode* i_phi = _builder->CreatePHI(i32_ty, 2, "uarr_i");
            i_phi->addIncoming(llvm::ConstantInt::get(i32_ty, 0), entry_bb);
            llvm::Value* cmp = _builder->CreateICmpULT(i_phi, n_val, "uarr_cmp");
            _builder->CreateCondBr(cmp, loop_body, loop_end);

            _builder->SetInsertPoint(loop_body);
            llvm::Value* indices[] = {llvm::ConstantInt::get(i32_ty, 0), i_phi};
            llvm::Value* elem_ptr = _builder->CreateGEP(
                llvm_arr_type, data_ptr, indices, "uarr_elem");

            if (auto st_type = std::dynamic_pointer_cast<struct_type>(elem_type)) {
                auto ctor = expr.uniform_constructor();
                if (ctor) {
                    auto ctor_it = _context->_functions.find(ctor->shared_as<function>());
                    if (ctor_it != _context->_functions.end()) {
                        std::vector<llvm::Value*> args;
                        args.push_back(elem_ptr);
                        for (auto& arg : expr.uniform_ctor_args()) {
                            _value = nullptr;
                            arg->accept(*this);
                            if (_value) args.push_back(_value);
                        }
                        _builder->CreateCall(ctor_it->second, args);
                    }
                }
            } else if (type::is_primitive(elem_type)) {
                if (!expr.uniform_ctor_args().empty() && expr.uniform_ctor_args()[0]) {
                    _value = nullptr;
                    expr.uniform_ctor_args()[0]->accept(*this);
                    if (_value) _builder->CreateStore(_value, elem_ptr);
                }
            }

            llvm::Value* i_next = _builder->CreateAdd(
                i_phi, llvm::ConstantInt::get(i32_ty, 1), "uarr_i_next");
            i_phi->addIncoming(i_next, loop_body);
            _builder->CreateBr(loop_header);

            _builder->SetInsertPoint(loop_end);
            _value = raw_ptr;
            return;
        } else {
            // ── Static uniform array: new T(args)[N] ──
            size_t arr_size = expr.array_size();

            auto own_type = std::dynamic_pointer_cast<owner_type>(expr.get_type());
            auto sized_arr_type = own_type
                ? std::dynamic_pointer_cast<sized_array_type>(own_type->get_owned_type())
                : nullptr;
            if (!sized_arr_type) { _value = nullptr; return; }

            auto* struct_llvm = sized_arr_type->get_llvm_struct_type();
            if (!struct_llvm) { _value = nullptr; return; }

            // malloc(sizeof(struct { i32, [N x T] }))
            auto* size_val = llvm::ConstantInt::get(
                llvm::Type::getInt64Ty(llvm_ctx),
                mod.getDataLayout().getTypeAllocSize(struct_llvm));
            llvm::Value* raw_ptr = _builder->CreateCall(
                malloc_fn->getFunctionType(), malloc_fn, {size_val}, "new_uarr_raw");

            // Zero-init the entire struct
            auto* zero_init = llvm::ConstantAggregateZero::get(struct_llvm);
            _builder->CreateStore(zero_init, raw_ptr);

            // Store the element count in field 0
            llvm::Value* size_ptr2 = _builder->CreateStructGEP(struct_llvm, raw_ptr,
                sized_array_type::FIELD_SIZE, "uarr_size");
            _builder->CreateStore(
                llvm::ConstantInt::get(llvm::Type::getInt32Ty(llvm_ctx), arr_size, false),
                size_ptr2);

            // Get pointer to the data array (field 1)
            llvm::Value* data_ptr = _builder->CreateStructGEP(struct_llvm, raw_ptr,
                sized_array_type::FIELD_DATA, "uarr_data");
            auto* llvm_arr_type2 = sized_arr_type->get_llvm_data_array_type();

            // Initialize each element
            for (size_t i = 0; i < arr_size; ++i) {
                llvm::Value* elem_ptr = _builder->CreateConstInBoundsGEP2_32(
                    llvm_arr_type2, data_ptr, 0, i, "uarr_elem_" + std::to_string(i));

                if (auto st_type = std::dynamic_pointer_cast<struct_type>(elem_type)) {
                    auto ctor = expr.uniform_constructor();
                    if (ctor) {
                        auto ctor_it = _context->_functions.find(ctor->shared_as<function>());
                        if (ctor_it != _context->_functions.end()) {
                            std::vector<llvm::Value*> args;
                            args.push_back(elem_ptr);
                            for (auto& arg : expr.uniform_ctor_args()) {
                                _value = nullptr;
                                arg->accept(*this);
                                if (_value) args.push_back(_value);
                            }
                            _builder->CreateCall(ctor_it->second, args);
                        }
                    }
                } else if (type::is_primitive(elem_type)) {
                    if (!expr.uniform_ctor_args().empty() && expr.uniform_ctor_args()[0]) {
                        _value = nullptr;
                        expr.uniform_ctor_args()[0]->accept(*this);
                        if (_value) _builder->CreateStore(_value, elem_ptr);
                    }
                }
            }

            _value = raw_ptr;
            return;
        }
    }

    if (expr.is_array()) {
        // ── Static array form: new T[N]{e0, e1, ...} ──
        size_t arr_size = expr.array_size();
        auto elem_type = alloc_type;

        // Get the sized_array_type from the expression type (owner<sized_array_type>)
        auto own_type = std::dynamic_pointer_cast<owner_type>(expr.get_type());
        auto sized_arr_type = own_type
            ? std::dynamic_pointer_cast<sized_array_type>(own_type->get_owned_type())
            : nullptr;
        if (!sized_arr_type) { _value = nullptr; return; }

        // Get LLVM types
        auto* struct_llvm = sized_arr_type->get_llvm_struct_type();
        if (!struct_llvm) { _value = nullptr; return; }
        llvm::Type* llvm_elem_type = _context->get_llvm_type(elem_type);

        // malloc(sizeof(struct { i32, [N x T] }))
        auto* size_val = llvm::ConstantInt::get(
            llvm::Type::getInt64Ty(llvm_ctx),
            mod.getDataLayout().getTypeAllocSize(struct_llvm));
        llvm::Value* raw_ptr = _builder->CreateCall(
            malloc_fn->getFunctionType(), malloc_fn, {size_val}, "new_arr_raw");

        // Zero-init the entire struct
        auto* zero_init = llvm::ConstantAggregateZero::get(struct_llvm);
        _builder->CreateStore(zero_init, raw_ptr);

        // Store the element count in field 0
        llvm::Value* size_ptr = _builder->CreateStructGEP(struct_llvm, raw_ptr,
            sized_array_type::FIELD_SIZE, "arr_size");
        _builder->CreateStore(
            llvm::ConstantInt::get(llvm::Type::getInt32Ty(llvm_ctx), arr_size, false),
            size_ptr);

        // Get pointer to the data array (field 1)
        llvm::Value* data_ptr = _builder->CreateStructGEP(struct_llvm, raw_ptr,
            sized_array_type::FIELD_DATA, "arr_data");
        auto* llvm_arr_type = sized_arr_type->get_llvm_data_array_type();

        // Initialize each element
        size_t init_count = expr.array_init_elements().size();
        for (size_t i = 0; i < arr_size; ++i) {
            llvm::Value* elem_ptr = _builder->CreateConstInBoundsGEP2_32(
                llvm_arr_type, data_ptr, 0, i, "arr_elem_" + std::to_string(i));

            std::shared_ptr<expression> elem_expr = (i < init_count) ? expr.array_init_elements()[i] : nullptr;

            if (type::is_primitive(elem_type)) {
                if (elem_expr) {
                    _value = nullptr;
                    elem_expr->accept(*this);
                    if (_value) _builder->CreateStore(_value, elem_ptr);
                }
                // else: already zero-inited
            } else if (auto st_type = std::dynamic_pointer_cast<struct_type>(elem_type)) {
                auto ctor = (i < expr.element_constructors().size())
                    ? expr.element_constructors()[i] : nullptr;
                if (ctor) {
                    auto ctor_it = _context->_functions.find(ctor->shared_as<function>());
                    if (ctor_it != _context->_functions.end()) {
                        std::vector<llvm::Value*> args;
                        args.push_back(elem_ptr); // 'this' pointer for the element

                        if (elem_expr) {
                            auto func_inv = std::dynamic_pointer_cast<function_invocation_expression>(elem_expr);
                            if (func_inv) {
                                // Explicit constructor call: pass all arguments
                                for (auto& arg : func_inv->arguments()) {
                                    _value = nullptr;
                                    arg->accept(*this);
                                    if (_value) args.push_back(_value);
                                }
                            } else {
                                // Implicit single-param constructor
                                _value = nullptr;
                                elem_expr->accept(*this);
                                if (_value) args.push_back(_value);
                            }
                        }
                        // else: default constructor (no extra args)

                        _builder->CreateCall(ctor_it->second, args);
                    }
                }
            }
        }

        _value = raw_ptr;
        return;
    }

    // ── Single-object form (unchanged) ──

    llvm::Type* llvm_type = _context->get_llvm_type(alloc_type);
    if (!llvm_type) { _value = nullptr; return; }

    auto* size_val = llvm::ConstantInt::get(
        llvm::Type::getInt64Ty(llvm_ctx),
        mod.getDataLayout().getTypeAllocSize(llvm_type));
    std::vector<llvm::Value*> malloc_args = {size_val};
    llvm::Value* raw_ptr = _builder->CreateCall(
        malloc_fn->getFunctionType(), malloc_fn, malloc_args, "new_raw");

    // Call constructor if struct
    if (auto st_type = std::dynamic_pointer_cast<struct_type>(alloc_type)) {
        auto ctor = expr.get_constructor();
        if (ctor) {
            auto ctor_it = _context->_functions.find(ctor->shared_as<function>());
            if (ctor_it != _context->_functions.end()) {
                std::vector<llvm::Value*> args;
                args.push_back(raw_ptr);
                for (auto& arg : expr.arguments()) {
                    _value = nullptr;
                    arg->accept(*this);
                    if (_value) args.push_back(_value);
                }
                _builder->CreateCall(ctor_it->second, args);
            }
        }
    } else if (type::is_primitive(alloc_type)) {
        // Store the argument value if provided
        if (!expr.arguments().empty()) {
            _value = nullptr;
            expr.arguments()[0]->accept(*this);
            if (_value) _builder->CreateStore(_value, raw_ptr);
        } else {
            // Zero-init
            _builder->CreateStore(llvm::Constant::getNullValue(llvm_type), raw_ptr);
        }
    }

    _value = raw_ptr;
}


void implementation_generator::visit_delete_expression(delete_expression& expr) {
    auto sub = expr.sub_expr();
    if (!sub) { _value = nullptr; return; }

    auto sub_type = sub->get_type();

    // Determine the owner type and the alloca holding it
    std::shared_ptr<owner_type> own_type;
    llvm::Value* owner_alloca = nullptr; // alloca of the owner variable (opaque ptr to owner slot)

    bool is_ref_to_owner = false;
    if (type::is_reference(sub_type)) {
        auto ref = std::dynamic_pointer_cast<reference_type>(sub_type);
        own_type = std::dynamic_pointer_cast<owner_type>(ref->get_subtype());
        is_ref_to_owner = (own_type != nullptr);
    } else {
        own_type = std::dynamic_pointer_cast<owner_type>(sub_type);
    }

    if (!own_type) { _value = nullptr; return; }
    auto alloc_type = own_type->get_owned_type();

    // Get the alloca (address of the owner slot)
    _value = nullptr;
    // We need the address (reference), not the value — so we evaluate the sub_expr
    // as an lvalue (address). For a symbol_expression this gives us the alloca directly.
    // Note: for both direct owner and reference-to-owner, accept() on a
    // symbol_expression produces the alloca (address of the owner slot).
    sub->accept(*this);
    owner_alloca = _value;

    if (!owner_alloca) { _value = nullptr; return; }

    emit_owner_cleanup_if_nonnull(_builder.get(), get_module(), _context->_functions,
        owner_alloca, alloc_type, "delete");
    _value = nullptr;
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
        throw_internal_error(0x0007, expr.first_lexeme(),
            "Internal error: constructor invocation expression does not refer to a variable definition; "
            "the constructed symbol must be a variable — this indicates a compiler bug");
    }
    expr.set_type(var_def->get_type()->get_reference());

    // Check if constructor is explicitly needed
    auto var_type = var_def->get_type();
    if (!var_type) {
        throw_internal_error(0x0008, expr.first_lexeme(),
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
    } else if (auto et = std::dynamic_pointer_cast<enum_type>(var_type)) {
        // Enum type construction:
        //   MonEnum(entree)   — enum entry by name (resolved relative to the enum)
        //   MonEnum(3)        — construction from numeric literal
        //   MonEnum           — default construction (uses default entry)
        auto en = et->get_enumeration();
        if (!en) {
            throw_internal_error(0x0081, expr.first_lexeme(),
                "Internal error: enum_type has no associated enumeration model object");
        }
        if (expr.empty()) {
            // Default construction: use the default entry value
            auto def_entry = en->get_default_entry();
            auto val = value_expression::from_value(static_cast<long long>(def_entry.value));
            val->set_type(et);
            expr.arguments({val});
        } else if (expr.size() == 1) {
            auto arg = expr.argument(0);
            // Check if the argument is an unresolved symbol (enum entry name)
            auto sym = std::dynamic_pointer_cast<symbol_expression>(arg);
            if (sym && !sym->is_resolved()) {
                // Try to resolve as an enum entry name
                auto entry_name = sym->get_name().to_string();
                auto entry = en->get_entry_by_name(entry_name);
                if (entry.has_value()) {
                    auto val = value_expression::from_value(static_cast<long long>(entry->value));
                    val->set_type(et);
                    expr.assign_argument(0, val);
                } else {
                    throw_error(0x0082, expr.first_lexeme(),
                        "Enum '{}' has no entry named '{}'",
                        {en->get_short_name(), entry_name});
                }
            } else {
                // Argument is an already-resolved expression (e.g. numeric literal, variable, qualified enum entry)
                // Visit it to resolve types if needed
                arg->accept(*this);
                auto cast = adapt_type(expr.argument(0), et);
                if (cast && cast != expr.argument(0)) {
                    expr.assign_argument(0, cast);
                }
            }
        } else {
            throw_error(0x0083, expr.first_lexeme(),
                "Enum constructor takes at most one argument, but {} were provided",
                {std::to_string(expr.size())});
        }
    } else if (type::is_owner(var_type)) {
        // Owner type member init: _buf(buf) — move the owner pointer.
        // Adapt the argument type if needed (e.g. ref<owner> → owner).
        if (!expr.empty()) {
            auto cast = adapt_type(expr.argument(0), var_type);
            if (cast && cast != expr.argument(0)) {
                expr.assign_argument(0, cast);
            }
        }
    } else if (type::is_drain(var_type)) {
        // Drain type init: adapt argument to drain type.
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

        // ── Direct struct copy: if single arg has the same struct type (by value or by ref),
        //    allow direct aggregate copy without a constructor.
        if (ctor_args.size() == 1) {
            auto arg_type = ctor_args[0]->get_type();
            auto arg_type_nc = type::remove_const(arg_type);
            bool is_direct_copy = false;
            // Check bare struct type (rvalue from function return)
            if (arg_type_nc == st_type) {
                is_direct_copy = true;
            }
            // Check ref<struct> (lvalue variable)
            if (!is_direct_copy && type::is_reference(arg_type_nc)) {
                auto ref_sub = type::remove_const(std::dynamic_pointer_cast<reference_type>(arg_type_nc)->get_subtype());
                if (ref_sub == st_type) {
                    is_direct_copy = true;
                }
            }
            if (is_direct_copy) {
                // Direct copy: null constructor signals aggregate store in impl_gen
                expr.set_constructor(nullptr);
                expr.arguments(ctor_args);
                return;
            }
        }

        auto [best_constructor, adapted_args] = get_best_matching_constructor(st->constructors(), ctor_args);
        if (!best_constructor) {
            throw_error(0x002A, expr.first_lexeme(),
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
                throw_error(0x0032, expr.first_lexeme(),
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
        throw_internal_error(0x0012, expr.first_lexeme(),
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
        throw_internal_error(0x0013, expr.first_lexeme(),
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
                        throw_internal_error(0x0014, expr.first_lexeme(),
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
                    throw_internal_error(0x0015, expr.first_lexeme(),
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
    } else if (auto et = std::dynamic_pointer_cast<enum_type>(var_type)) {
        // Enum type: treat like a primitive — store the integer value.
        llvm::Value* value = nullptr;
        if (!expr.empty()) {
            auto first_arg = expr.argument(0);
            _value = nullptr;
            first_arg->accept(*this);
            if (!_value) {
                throw_internal_error(0x0084, expr.first_lexeme(),
                    "Internal error: failed to generate an LLVM value for enum initialisation "
                    "of variable '{}'; the argument expression produced no result",
                    {var_def->get_fq_name()});
            }
            value = _value;
        } else {
            // Default construction: use default value initializer
            value = et->generate_default_value_initializer();
        }
        if (value != nullptr) {
            _builder->CreateStore(value, object_ref);
        }
    } else if (auto st_type = std::dynamic_pointer_cast<struct_type>(var_type)) {
        auto st = st_type->get_struct();
        auto function = expr.get_constructor();

        // ── Direct struct copy (no constructor): aggregate load+store ──
        if (!function && expr.size() == 1) {
            _value = nullptr;
            expr.argument(0)->accept(*this);
            if (_value) {
                auto arg_type = expr.argument(0)->get_type();
                llvm::Type* llvm_struct_ty = _context->get_llvm_type(st_type);
                llvm::Value* src_val = _value;
                // If the argument is a reference (lvalue) OR a bare struct type
                // (rvalue materialized into an alloca), load the aggregate from the pointer.
                if (type::is_reference(arg_type) || type::is_struct(arg_type)) {
                    src_val = _builder->CreateLoad(llvm_struct_ty, _value, "copy_load");
                }
                // src_val is now the aggregate value; store into the destination alloca
                _builder->CreateStore(src_val, object_ref);
            }
            _value = object_ref;
            return;
        }

        std::vector<llvm::Value*> args;
        args.push_back(object_ref);
        for(auto arg : expr.arguments()) {
            _value = nullptr;
            arg->accept(*this);
            if(!_value) {
                throw_internal_error(0x0016, expr.first_lexeme(),
                    "Internal error: a constructor argument for type '{}' produced no LLVM value; "
                    "this indicates a code-generation bug",
                    {st_type->to_string()});
            }
            args.push_back(_value);
        }
        auto it = _context->_functions.find(function);
        if(it==_context->_functions.end()) {
            throw_internal_error(0x0017, expr.first_lexeme(),
                "Internal error: LLVM declaration not found for constructor of type '{}'; "
                "the declaration pass must be run before the implementation pass",
                {st_type->to_string()});
        }
        llvm::Function* llvm_func = it->second;
        if(!llvm_func) {
            throw_internal_error(0x0018, expr.first_lexeme(),
                "Internal error: LLVM constructor function object is null for type '{}'; "
                "this indicates a compiler bug in the declaration pass",
                {st_type->to_string()});
        }
        _value = _builder->CreateCall(llvm_func, args);

    } else if (type::is_owner(var_type)) {
        // Owner type member init: _buf(buf) — move the owner pointer.
        // Load the pointer from the source (parameter alloca), store into the member,
        // then null out the source so the exit-param cleanup doesn't free it.
        if (!expr.empty()) {
            _value = nullptr;
            expr.argument(0)->accept(*this);
            if (_value) {
                auto arg_type = expr.argument(0)->get_type();
                llvm::Value* ptr_val = _value;
                // If arg is ref<owner<T>>, load the owner pointer from the alloca
                if (arg_type && type::is_reference(arg_type)) {
                    auto inner = std::dynamic_pointer_cast<reference_type>(arg_type)->get_subtype();
                    inner = type::remove_const(inner);
                    if (type::is_owner(inner)) {
                        auto* ptr_ty = llvm::PointerType::get(_builder->getContext(), 0);
                        // _value is the alloca of the source owner variable
                        llvm::Value* src_alloca = _value;
                        ptr_val = _builder->CreateLoad(ptr_ty, src_alloca, "owner_move_load");
                        // Store into the destination member
                        _builder->CreateStore(ptr_val, object_ref);
                        // Null out the source to prevent double-free
                        _builder->CreateStore(
                            llvm::ConstantPointerNull::get(llvm::PointerType::get(_builder->getContext(), 0)),
                            src_alloca);
                    } else {
                        _builder->CreateStore(ptr_val, object_ref);
                    }
                } else {
                    // Direct owner value (e.g. from a move expression)
                    _builder->CreateStore(ptr_val, object_ref);
                }
            }
        }
        _value = object_ref;

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

    } else if (type::is_link(var_type) || type::is_view(var_type)) {
        // Link (+) or view (?) variable: store the address of the linked object.
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
            throw_internal_error(0x001A, expr.first_lexeme(),
                "Internal error: could not obtain the storage location for reference variable '{}'; "
                "the variable must have been allocated before constructor code generation",
                {var_def->get_fq_name()});
        }

        if (!expr.empty()) {
            auto first_arg = expr.argument(0);
            _value = nullptr;
            first_arg->accept(*this);
            if (!_value) {
                throw_internal_error(0x001B, expr.first_lexeme(),
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
            throw_internal_error(0x001C, expr.first_lexeme(),
                "Internal error: reference variable '{}' has no initialisation argument; "
                "the resolver should have rejected this earlier",
                {var_def->get_fq_name()});
        }

    } else if (type::is_drain(var_type)) {
        // Drain variable — store the drain address into the alloca.
        // Similar to reference init: get the raw alloca and store the address.
        llvm::Value* alloca_ptr = nullptr;
        if (auto local_var = std::dynamic_pointer_cast<variable_statement>(var_def)) {
            auto it = _context->_variables.find(local_var);
            if (it != _context->_variables.end()) alloca_ptr = it->second;
        } else if (auto global_var = std::dynamic_pointer_cast<global_variable_definition>(var_def)) {
            auto it = _context->_global_vars.find(global_var);
            if (it != _context->_global_vars.end()) alloca_ptr = it->second;
        }
        if (!alloca_ptr) {
            throw_internal_error(0x001A, expr.first_lexeme(),
                "Internal error: could not obtain the storage location for drain variable '{}'; "
                "the variable must have been allocated before constructor code generation",
                {var_def->get_fq_name()});
        }
        if (!expr.empty()) {
            _value = nullptr;
            expr.argument(0)->accept(*this);
            if (_value) {
                _builder->CreateStore(_value, alloca_ptr);
            }
        }
        object_ref = alloca_ptr;

    } else if (auto sized_arr_type = std::dynamic_pointer_cast<sized_array_type>(var_type)) {
        // Sized array value variable: int[N]
        // No explicit init — zero-initialise the entire struct, then set the count field.
        auto* struct_llvm = sized_arr_type->get_llvm_struct_type();
        if (!struct_llvm) {
            throw_internal_error(0x001D, expr.first_lexeme(),
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
                if (type::is_view(t))    return sub->get_view();
                if (type::is_reference(t)) return sub->get_reference();
                if (type::is_const(t))     return sub->get_const();
                return nullptr;
            };
            auto resolved2 = resolve_by_name_composite(resolve_by_name_composite, target_type);
            if (resolved2 && type::is_resolved(resolved2)) {
                target_type = resolved2;
                expr.set_cast_type(target_type);
            } else {
                throw_error(0x40035, expr.first_lexeme(),
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
            if (auto view_var = std::dynamic_pointer_cast<view_type>(t))
                return std::dynamic_pointer_cast<struct_type>(type::remove_const(view_var->get_viewed_type()));
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
            if (type::is_link(inner) || type::is_view(inner) || type::is_pointer(inner)) {
                effective_source = inner;
                source_unwrapped_ref = true;
            }
        }

        // ── Case: ptr/lnk/pin source → ptr/lnk/pin target ────────────────────
        if ((type::is_pointer(effective_source) || type::is_link(effective_source) || type::is_view(effective_source)) &&
            (type::is_pointer(target_type)       || type::is_link(target_type)       || type::is_view(target_type))) {

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
                        throw_error(0x40033, expr.first_lexeme(),
                            "Explicit cast: cannot cast from '{}' to '{}': "
                            "the pointed types have no inheritance relationship",
                            {source_type->to_string(), target_type->to_string()});
                    }
                }
                // Same struct type: allowed (e.g. ptr<T>→lnk<T>).
            }
            // If source or target does not point to a struct/class: allowed (opaque ptr reinterpret).
        }

        // ── Case: indirection/null source → bool target ───────────────────────
        else if ((type::is_pointer(effective_source) || type::is_link(effective_source) ||
                  type::is_view(effective_source) || type::is_owner(effective_source) ||
                  type::is_null(effective_source)) && type::is_prim_bool(target_type)) {
            // Indirection-to-bool or null-to-bool: valid (null check). No model transformation needed.
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
                        throw_error(0x40034, expr.first_lexeme(),
                            "Explicit cast: cannot cast reference from '{}' to '{}': "
                            "the referenced types have no inheritance relationship",
                            {source_type->to_string(), target_type->to_string()});
                    }
                }
            }
            // Keep as-is (no load_value replacement).
        }

        // ── Casting operator overload: (TargetType)struct_value ──────────────
        // Check BEFORE ref→value unwrapping so that the reference is preserved
        // as the 'this' parameter for the casting operator call.
        else if(type::is_reference(source_type)) {
            auto get_source_aggregate = [](const std::shared_ptr<type>& src) -> std::shared_ptr<aggregate> {
                auto effective = src;
                if (type::is_reference(effective)) {
                    effective = std::dynamic_pointer_cast<reference_type>(effective)->get_referenced_type();
                }
                effective = type::remove_const(effective);
                if (auto st = std::dynamic_pointer_cast<struct_type>(effective)) {
                    return st->get_struct();
                }
                return nullptr;
            };

            auto source_agg = get_source_aggregate(source_type);
            if (source_agg) {
                bool is_const_this = false;
                auto ref_sub = std::dynamic_pointer_cast<reference_type>(source_type)->get_referenced_type();
                is_const_this = type::is_const(ref_sub);

                auto cast_func = resolve_cast_operator_overload(source_agg, target_type, is_const_this);
                if (cast_func) {
                    expr.set_operator_func(cast_func);

                    // Compute virtual dispatch info if the function is virtual
                    if (cast_func->is_virtual() && cast_func->get_vtable_slot() >= 0) {
                        auto receiver_type = source_type;
                        auto di = compute_operator_dispatch_info(cast_func, receiver_type);
                        expr.set_operator_dispatch_info(std::move(di));
                    }

                    expr.set_type(target_type);
                    return;
                }
            }

            // No casting operator found: fall back to ref<T> → T load
            auto deref = load_value_expression::make_shared(sub_expr->shared_as<expression>());
            expr.assign(deref);
            deref->set_type(source_type->get_subtype());
        }
    }

    expr.set_type(expr.get_cast_type());
}

void implementation_generator::visit_cast_expression(cast_expression& expr) {
    // ── Casting operator overload: call __operator_cv_<type>() ───────────────
    if (generate_cast_operator_overload(expr)) return;

    auto source_type = expr.sub_expr()->get_type();
    auto target_type = expr.get_cast_type();

    if(!source_type->is_resolved() || !target_type->is_resolved()) {
        throw_internal_error(0x0019, expr.first_lexeme(),
            "Internal error: cast expression has an unresolved source or target type; "
            "type resolution must complete before code generation");
    }

    // ── Enum ↔ primitive / enum ↔ enum casts ─────────────────────────────────
    // At LLVM IR level, enums are just integers. The cast is a no-op if the
    // underlying types match, or an integer truncation/extension otherwise.
    {
        auto src_nc = type::remove_const(source_type);
        auto tgt_nc = type::remove_const(target_type);
        auto enum_src = std::dynamic_pointer_cast<enum_type>(src_nc);
        auto enum_tgt = std::dynamic_pointer_cast<enum_type>(tgt_nc);
        if (enum_src || enum_tgt) {
            // Evaluate source expression
            _value = nullptr;
            expr.sub_expr()->accept(*this);
            if (!_value) return;

            llvm::Type* src_llvm = enum_src ? enum_src->get_llvm_type()
                : std::dynamic_pointer_cast<primitive_type>(src_nc)->get_llvm_type();
            llvm::Type* tgt_llvm = enum_tgt ? enum_tgt->get_llvm_type()
                : std::dynamic_pointer_cast<primitive_type>(tgt_nc)->get_llvm_type();

            if (src_llvm == tgt_llvm) {
                // Same LLVM type: no-op cast
                return;
            }
            // Integer widening/narrowing
            auto src_int = llvm::dyn_cast<llvm::IntegerType>(src_llvm);
            auto tgt_int = llvm::dyn_cast<llvm::IntegerType>(tgt_llvm);
            if (src_int && tgt_int) {
                if (tgt_int->getBitWidth() > src_int->getBitWidth()) {
                    // Determine signedness from enum's underlying type or primitive
                    bool is_signed = false;
                    if (enum_src) {
                        is_signed = !enum_src->get_underlying_type()->is_unsigned();
                    } else if (auto ps = std::dynamic_pointer_cast<primitive_type>(src_nc)) {
                        is_signed = !ps->is_unsigned();
                    }
                    _value = is_signed
                        ? _builder->CreateSExt(_value, tgt_llvm, "enum_sext")
                        : _builder->CreateZExt(_value, tgt_llvm, "enum_zext");
                } else {
                    _value = _builder->CreateTrunc(_value, tgt_llvm, "enum_trunc");
                }
                return;
            }
            return;
        }
    }

    // ── ref<T> → link<T> or ref<T> → pin<T>: no-op (same LLVM ptr) ────────────
    if (type::is_reference(source_type) &&
        (type::is_link(target_type) || type::is_view(target_type))) {
        auto src_sub = type::remove_const(std::dynamic_pointer_cast<reference_type>(source_type)->get_subtype());
        auto tgt_sub = type::remove_const(target_type->get_subtype());
        if (src_sub == tgt_sub) {
            // ref<T> and link<T>/pin<T> are both LLVM pointers — no IR conversion needed.
            _value = nullptr;
            expr.sub_expr()->accept(*this);
            return;
        }
    }

    // ── drain<T> ↔ ref<T> / drain<T> ↔ drain<T>: no-op (same LLVM opaque ptr) ──
    if ((type::is_drain(source_type) || type::is_reference(source_type)) &&
        (type::is_drain(target_type) || type::is_reference(target_type) ||
         type::is_link(target_type) || type::is_view(target_type))) {
        auto src_sub = type::remove_const(source_type->get_subtype());
        auto tgt_sub = type::remove_const(target_type->get_subtype());
        if (src_sub == tgt_sub) {
            // drain<T> and ref<T> are both LLVM opaque pointers — no IR conversion needed.
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
            if (auto view_var = std::dynamic_pointer_cast<view_type>(t))
                return std::dynamic_pointer_cast<struct_type>(type::remove_const(view_var->get_viewed_type()));
            if (auto ptr = std::dynamic_pointer_cast<pointer_type>(t))
                return std::dynamic_pointer_cast<struct_type>(type::remove_const(ptr->get_pointed_type()));
            return nullptr;
        };

        // Effective source: if ref<indirection>, unwrap ref for type checks (load needed)
        auto effective_source = source_type;
        if (type::is_reference(source_type)) {
            auto inner = std::dynamic_pointer_cast<reference_type>(source_type)->get_subtype();
            if (type::is_link(inner) || type::is_view(inner) || type::is_pointer(inner)) {
                effective_source = inner;
                src_needs_load = true;
            }
        }

        bool src_is_indir = type::is_link(effective_source) || type::is_view(effective_source) || type::is_pointer(effective_source);
        bool tgt_is_indir = type::is_link(target_type) || type::is_view(target_type) || type::is_pointer(target_type);
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

    // ── Indirection → bool: emit ICmpNE(ptr, null) ─────────────────────────
    if((type::is_pointer(source_type) || type::is_link(source_type) ||
        type::is_view(source_type) || type::is_owner(source_type)) &&
       type::is_prim_bool(target_type)) {
        _value = nullptr;
        expr.sub_expr()->accept(*this);
        if (!_value) return;
        auto null_ptr = llvm::ConstantPointerNull::get(
            llvm::PointerType::get(_builder->getContext(), 0));
        _value = _builder->CreateICmpNE(_value, null_ptr, "ind_to_bool");
        return;
    }
    // ── null → bool: always false ────────────────────────────────────────────
    if(type::is_null(source_type) && type::is_prim_bool(target_type)) {
        _value = _builder->getFalse();
        return;
    }

    // ── Indirection reinterpret: owner ↔ pointer ↔ link ↔ view ────────────
    // All indirection types share the same LLVM opaque-pointer representation,
    // so an owner-to-pointer borrow (or any other combination) is a no-op cast
    // when the inner types match.  We must NOT short-circuit when inner types
    // differ (e.g. Base* → Derived~ requires a dynamic cast).
    {
        auto is_heap_indirection = [](const std::shared_ptr<type>& t) {
            return type::is_owner(t) || type::is_pointer(t) ||
                   type::is_link(t) || type::is_view(t);
        };
        if (is_heap_indirection(source_type) && is_heap_indirection(target_type)) {
            auto src_inner = type::remove_const(source_type->get_subtype());
            auto tgt_inner = type::remove_const(target_type->get_subtype());
            if (src_inner == tgt_inner) {
                _value = nullptr;
                expr.sub_expr()->accept(*this);
                return;
            }
        }
        // indirection → reference: owner/ptr/lnk/pin<T> → ref<T>
        // Both are opaque pointers at LLVM IR level — no-op cast.
        // Also handles array element const-widening: array<T> → array<const<T>>.
        if (is_heap_indirection(source_type) && type::is_reference(target_type)) {
            auto src_inner = type::remove_const(source_type->get_subtype());
            auto tgt_inner = type::remove_const(std::dynamic_pointer_cast<reference_type>(target_type)->get_subtype());
            bool match = (src_inner == tgt_inner);
            if (!match) {
                // Check array element const-widening: array<T> matches array<const<T>>
                auto sa = std::dynamic_pointer_cast<array_type>(src_inner);
                auto ta = std::dynamic_pointer_cast<array_type>(tgt_inner);
                if (sa && ta && !sa->is_sized() && !ta->is_sized()) {
                    match = (type::remove_const(sa->get_subtype()) == type::remove_const(ta->get_subtype()));
                }
            }
            if (match) {
                _value = nullptr;
                expr.sub_expr()->accept(*this);
                return;
            }
        }
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
            if (auto view_var = std::dynamic_pointer_cast<view_type>(effective))
                return std::dynamic_pointer_cast<struct_type>(type::remove_const(view_var->get_viewed_type()));
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
        throw_error(0x001A, expr.first_lexeme(),
            "Casting between non-primitive types is not yet supported: "
            "cannot cast from '{}' to '{}'; only casts between primitive types are currently implemented",
            {source_type->to_string(), target_type->to_string()});
    }
    auto src = std::dynamic_pointer_cast<primitive_type>(source_type);
    auto tgt = std::dynamic_pointer_cast<primitive_type>(target_type);

    _value = nullptr;
    expr.sub_expr()->accept(*this);
    if(!_value) {
        throw_internal_error(0x001A, expr.first_lexeme(),
            "Internal error: the expression being cast produced no LLVM value; "
            "this indicates a code-generation bug in the sub-expression");
    }

    if(src->is_boolean()) {
        if(tgt->is_integer()) {
            // Bool is logically 0 or 1: always zero-extend, regardless of target signedness.
            _value = _builder->CreateZExt(_value, _builder->getIntNTy(tgt->type_size()));
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
            } else /* if (tgt->is_unsigned())*/  {
                if (src->is_signed()) {
                    auto d = k::log::diagnostic::make_warning(with_flag(0x001D),
                        "Casting a signed integer to an unsigned integer may reinterpret negative values "
                        "as large positive values (two's complement wrap-around)");
                    report(d);
                }
            }
            // Extension type depends on source signedness:
            // unsigned source → ZExt, signed source → SExt. Truncation is the same either way.
            if (src->is_unsigned()) {
                _value = _builder->CreateZExtOrTrunc(_value, _context->get_llvm_type(tgt));
            } else {
                _value = _builder->CreateSExtOrTrunc(_value, _context->get_llvm_type(tgt));
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
            _value = _builder->CreateFCmpUNE(_value, llvm::ConstantFP::get(_context->get_llvm_type(src), 0.0));
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
        throw_internal_error(0x0026, expr.first_lexeme(),
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
        throw_internal_error(0x0027, expr.first_lexeme(),
            "emit_dynamic_cast: RTTI global '{}' not found in module",
            {rtti_name});
    }

    // ── 3. Load the vptr from the source object (field 0 of the klass) ───────
    auto src_klass = std::dynamic_pointer_cast<klass>(src_st);
    if (!src_klass || !src_klass->has_vtable()) {
        throw_internal_error(0x0028, expr.first_lexeme(),
            "emit_dynamic_cast: source class '{}' has no vtable/vptr",
            {src_st->get_short_name()});
    }
    auto src_vt = src_klass->get_vtable();
    auto* src_llvm_type = src_st_type->get_llvm_type();
    if (!src_llvm_type || !src_vt->llvm_type) {
        throw_internal_error(0x0029, expr.first_lexeme(),
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
