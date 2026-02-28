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

#include "llvm/Support/raw_os_ostream.h"
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
        // TODO set function type
    }
    // TODO resolve other types of symbols
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
            auto member_owner = member_var->parent<structure>();
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
    _value = _builder->CreateLoad(_context->get_llvm_type(expr.get_type()), _value);
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

    if(!type::is_reference(type)) {
        throw_error(0x001C, std::nullopt,
            "Cannot access a member on a non-reference expression: "
            "the '.' operator requires the left-hand side to be a reference to a struct, "
            "but the left-hand side has type '{}'",
            {type ? type->to_string() : "?"});
    }
    auto subtype = type->get_subtype();
    if(auto struct_subtype = std::dynamic_pointer_cast<struct_type>(subtype)) {
        const auto& member_name = expr.symbol();
        const std::string& name_str = member_name.get_name().to_string();

        // ── Helper: search a struct and its bases for a named field or function,
        //    returning (struct_type*, field) or (struct_type*, nullptr=function).
        //    Returns empty vector if not found, multiple items if ambiguous.
        struct MemberHit {
            std::shared_ptr<struct_type> in_struct_type;
            std::optional<struct_type::field> field;
            bool is_function = false;
        };

        std::function<std::vector<MemberHit>(const std::shared_ptr<struct_type>&, const std::string&, visibility, bool)> search_member;
        search_member = [&](const std::shared_ptr<struct_type>& stype, const std::string& mname,
                             visibility inherit_vis, bool /*top_level*/) -> std::vector<MemberHit> {
            std::vector<MemberHit> hits;
            // Check direct field
            if (auto field = stype->get_member(mname)) {
                // Apply inheritance visibility filter (private base → members inaccessible)
                if (inherit_vis == PRIVATE) {
                    // members not accessible via private inheritance from outside
                    // (but we don't check access site here, defer to visibility check below)
                }
                hits.push_back({stype, field, false});
                return hits; // found in direct members — stop, no ambiguity possible at this level
            }
            // Check direct method
            if (auto st = stype->get_struct()) {
                if (st->get_function(mname)) {
                    hits.push_back({stype, std::nullopt, true});
                    return hits;
                }
                // Search bases
                for (auto& bs : st->get_bases()) {
                    if (!bs.base || !bs.base->get_struct_type()) continue;
                    // Combine inheritance visibility: private always wins
                    visibility eff_vis = (inherit_vis == PRIVATE || bs.vis == PRIVATE) ? PRIVATE :
                                         (inherit_vis == PROTECTED || bs.vis == PROTECTED) ? PROTECTED :
                                         PUBLIC;
                    auto sub_hits = search_member(bs.base->get_struct_type(), mname, eff_vis, false);
                    hits.insert(hits.end(), sub_hits.begin(), sub_hits.end());
                }
            }
            return hits;
        };

        auto hits = search_member(struct_subtype, name_str, PUBLIC, true);

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
                if (member_var->get_visibility() != PUBLIC) {
                    bool accessible = false;
                    for (auto it = _function_stack.rbegin(); it != _function_stack.rend(); ++it) {
                        const auto& fn = *it;
                        if (fn->is_member() && !fn->is_static()) {
                            auto check_st = fn->get_owner();
                            while (check_st) {
                                if (check_st.get() == st_model.get()) { accessible = true; break; }
                                check_st = check_st->get_enclosing_structure();
                            }
                        }
                        if (accessible) break;
                    }
                    if (!accessible) {
                        throw_error(0x0030, std::nullopt,
                            "{} member variable '{}' of struct '{}' is not accessible here; "
                            "it can only be accessed from member functions of '{}'",
                            {member_var->get_visibility() == PROTECTED ? "protected" : "private",
                             member_var->get_short_name(), st_model->get_short_name(), st_model->get_short_name()});
                    }
                }
            }
        }

        if (!hit.is_function && hit.field.has_value()) {
            expr.set_type(hit.field->field_type.lock()->get_reference());
            // If the field is in a base, wrap sub_expr in a cast_expression to base ref
            // so that implementation_generator will compute the correct GEP offset.
            if (hit.in_struct_type != struct_subtype) {
                auto base_ref_type = hit.in_struct_type->get_reference();
                // Create cast with base_ref_type as cast_type so visit_cast_expression works
                auto upcast = cast_expression::make_shared(expr.sub_expr(), base_ref_type);
                upcast->set_type(base_ref_type);
                // Directly replace the sub_expr via mutable reference
                expr.sub_expr() = upcast;
            }
        } else if (hit.is_function) {
            // Member function: update the sub_expr type to point to the struct that owns the method.
            // This is needed so that implementation_generator finds the method in the correct struct,
            // and so that the 'this' pointer is correctly adjusted via upcast if needed.
            if (hit.in_struct_type != struct_subtype) {
                auto base_ref_type = hit.in_struct_type->get_reference();
                auto upcast = cast_expression::make_shared(expr.sub_expr(), base_ref_type);
                upcast->set_type(base_ref_type);
                expr.sub_expr() = upcast;
            }
            // Type of member_of_object_expression for functions is the struct ref (for 'this')
            // — leave expr type unset; function_invocation_expression will handle it.
        }
    } else {
        throw_error(0x001E, std::nullopt,
            "The '.' operator can only be applied to a reference to a struct type, "
            "but the left-hand side is a reference to '{}' which is not a struct",
            {subtype ? subtype->to_string() : "?"});
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
    auto struct_ref = _value;

    auto type = expr.sub_expr()->get_type(); // Is a reference
    if(auto struct_subtype = std::dynamic_pointer_cast<struct_type>(type->get_subtype())) {
        const auto& member_name =  expr.symbol();
        if(auto field = struct_subtype->get_member(member_name.get_name()); field) {
            _value = _builder->CreateStructGEP(type->get_subtype()->get_llvm_type(), _value, field->index);
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
        auto field_type = field->field_type.lock();
        expr.set_type(field_type ? field_type->get_reference() : nullptr);
    } else if (struct_subtype->get_struct() && struct_subtype->get_struct()->get_function(name_str)) {
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

void type_reference_resolver::visit_function_invocation_expression(function_invocation_expression &expr) {
    auto callee = std::dynamic_pointer_cast<symbol_expression>(expr.callee_expr());
    auto member_callee = std::dynamic_pointer_cast<member_of_object_expression>(expr.callee_expr());

    if(!callee && !member_callee) {
        throw_error(0x0022, std::nullopt,
            "Unsupported call expression form: only direct function calls ('func(args)') and "
            "member function calls ('obj.method(args)') are supported");
    }

    // Resolve and type-check all arguments first
    for(auto& arg : expr.arguments()) {
        arg->accept(*this);
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
        auto struct_subtype = std::dynamic_pointer_cast<struct_type>(subtype);
        if (!struct_subtype) {
            throw_error(0x0025, std::nullopt,
                "The '.' operator can only be applied to a struct type, "
                "but the left-hand side has type '{}' which is not a struct",
                {subtype ? subtype->to_string() : "?"});
        }
        auto st = struct_subtype->get_struct();

        // Use the short (unqualified) name for function lookup
        std::string func_short_name = callee->get_name().back();

        // Collect all candidate functions (member + free/static from parent scopes)
        std::vector<std::shared_ptr<function>> candidates = scope_lookup::lookup_functions(st, func_short_name);

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
        return;
    }

    // ----------------------------------------------------------------
    // Case 2 : plain symbol call  "func(args)"
    // ----------------------------------------------------------------
    {
        std::string func_name = callee->get_name().back();
        const auto& args = expr.arguments();

        std::vector<std::shared_ptr<function>> all_candidates;
        if (callee->is_function() && callee->get_name().size() > 1) {
            all_candidates.push_back(callee->get_function());
        } else {
            all_candidates = scope_lookup::lookup_functions(callee, func_name);
        }

        std::shared_ptr<expression> this_candidate;
        std::vector<std::shared_ptr<expression>> rest_args;
        if (!args.empty()) {
            auto first_arg_type = args[0]->get_type();
            if (type::is_reference(first_arg_type)) {
                if (auto first_struct = std::dynamic_pointer_cast<struct_type>(first_arg_type->get_subtype())) {
                    auto st = first_struct->get_struct();
                    this_candidate = args[0];
                    rest_args = std::vector<std::shared_ptr<expression>>(args.begin() + 1, args.end());
                    // Collect member functions from the struct and all its bases (recursively)
                    std::function<void(const std::shared_ptr<structure>&)> collect_member_fns;
                    collect_member_fns = [&](const std::shared_ptr<structure>& s) {
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
            auto obj_expr = expr.arguments()[0];
            auto sym_for_member = symbol_expression::from_function(best.func);
            sym_for_member->set_target(best.func);
            auto member_expr = member_of_object_expression::make_shared(obj_expr, sym_for_member);
            expr.assign(member_expr, best.adapted_args);
        } else {
            // Regular/unified call — may include default values for trailing params
            expr.assign_arguments(best.adapted_args);
        }
        return;
    }
}

void implementation_generator::visit_function_invocation_expression(function_invocation_expression &expr) {
    auto callee = std::dynamic_pointer_cast<symbol_expression>(expr.callee_expr());
    auto member_callee = std::dynamic_pointer_cast<member_of_object_expression>(expr.callee_expr());

    if(!callee && !member_callee) {
        throw_internal_error(0x000C, std::nullopt,
            "Internal error: unsupported call expression form during code generation; "
            "only direct and member function calls are supported");
    }

    // Generate arguments and add the to the args list
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
        throw_internal_error(0x0010, std::nullopt,
            "Internal error: LLVM declaration not found for function '{}' during code generation; "
            "the declaration pass must be run before the implementation pass",
            {function ? function->get_fq_name() : "<null>"});
    }
    llvm::Function* llvm_func = it->second;
    if(!llvm_func) {
        throw_internal_error(0x0011, std::nullopt,
            "Internal error: LLVM function object is null for '{}'; "
            "this indicates a compiler bug in the declaration pass",
            {function ? function->get_fq_name() : "<null>"});
    }

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
        // Pointer (*) variable: store the address.
        if (!expr.empty()) {
            _value = nullptr;
            expr.argument(0)->accept(*this);
            if (_value) {
                // Unwrap ref if argument is ref<indirection>
                auto arg_type = expr.argument(0)->get_type();
                if (arg_type && type::is_reference(arg_type)) {
                    auto inner = std::dynamic_pointer_cast<reference_type>(arg_type)->get_subtype();
                    _value = _builder->CreateLoad(_context->get_llvm_type(inner), _value, "ptr_init_load");
                }
                _builder->CreateStore(_value, object_ref);
            }
        }
        _value = object_ref;

    } else if (type::is_link(var_type) || type::is_pinned(var_type)) {
        // Link (~) or pinned (^) variable: store the raw address.
        if (!expr.empty()) {
            _value = nullptr;
            expr.argument(0)->accept(*this);
            if (_value) {
                // Unwrap ref if argument is ref<indirection>
                auto arg_type = expr.argument(0)->get_type();
                if (arg_type && type::is_reference(arg_type)) {
                    auto inner = std::dynamic_pointer_cast<reference_type>(arg_type)->get_subtype();
                    _value = _builder->CreateLoad(_context->get_llvm_type(inner), _value, "ind_init_load");
                }
                if (type::is_link(var_type)) {
                    // Non-null required: emit null-check if source is nullable.
                    auto effective_type = arg_type;
                    if (effective_type && type::is_reference(effective_type)) {
                        effective_type = std::dynamic_pointer_cast<reference_type>(effective_type)->get_subtype();
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

    if(source_type==target_type) {
        // TODO warn about useless casting
    } else {
        if(type::is_pointer(source_type)) {
            if(type::is_prim_bool(target_type)) {
                // TODO add pointer to boolean casting
            } else if(type::is_pointer(target_type)) {
                //  TODO add pointer type casting checking.
            } else {
                // TODO throw an error, other pointer casting are not supported
            }
        } else if(type::is_reference(source_type) && type::is_reference(target_type)) {
            // Struct reference upcast: ref<Derived> → ref<Base>
            // Both are references and types differ — validated during adapt_type / compute_cast_weight.
            // No additional transformation needed at model level; IR generation handles GEP.
            // Keep as-is (no load_value replacement).
        } else if(type::is_reference(source_type)) {
            if(type::is_reference(target_type)) {
                // TODO throw an error, casting references is not supported yet (not for any primitive type)
            }
            auto deref = load_value_expression::make_shared(sub_expr->shared_as<expression>());
            expr.assign(deref);
            deref->set_type(source_type->get_subtype());
        }
    }

    // TODO check if cast is possible (expr.expr().get_type() && expr.get_cast_type() compatibility)

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

    // ── Struct reference upcast: ref<Derived> → ref<Base> ────────────────────
    // Both source and target are references to struct types. We need to GEP to the
    // base subobject field within the derived struct.
    if (type::is_reference(source_type) && type::is_reference(target_type)) {
        auto src_ref = std::dynamic_pointer_cast<reference_type>(source_type);
        auto tgt_ref = std::dynamic_pointer_cast<reference_type>(target_type);
        auto src_st_type = std::dynamic_pointer_cast<struct_type>(src_ref->get_referenced_type());
        auto tgt_st_type = std::dynamic_pointer_cast<struct_type>(tgt_ref->get_referenced_type());
        if (src_st_type && tgt_st_type && src_st_type != tgt_st_type) {
            auto src_st = src_st_type->get_struct();
            auto tgt_st = tgt_st_type->get_struct();
            if (src_st && tgt_st && src_st->is_derived_from(tgt_st)) {
                // Generate GEP to base subobject field
                _value = nullptr;
                expr.sub_expr()->accept(*this);
                if (!_value) return;

                // Find the base subobject field index in the derived struct
                // The base subobject is stored as "__base_<name>__" member
                // We need to find which field index it corresponds to in the LLVM struct type
                std::string subobj_name;
                for (auto& bs : src_st->get_bases()) {
                    if (bs.base && bs.base.get() == tgt_st.get()) {
                        subobj_name = "__base_" + bs.raw_name + "__";
                        break;
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
                // Fallback: return as-is (pointer reinterpret for same-layout case)
                return;
            }
        }
        // Same type, no-op
        _value = nullptr;
        expr.sub_expr()->accept(*this);
        return;
    }

    if(type::is_pointer(source_type) && type::is_prim_bool(target_type)) {
        // TODO add pointer to boolean casting
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
