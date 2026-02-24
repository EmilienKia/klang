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

            // Get member variable
            if(_struct_stack.empty()) {
                throw_internal_error(0x0003, std::nullopt,
                    "Internal error: no struct context on the code-generation stack when accessing member variable '{}'; "
                    "member access code generation must be performed inside a struct method",
                    {name});
            }
            auto struct_ref = _struct_stack.top();
            auto struct_type = struct_ref->get_struct_type();
            if(struct_type) {
                if(auto field = struct_type->get_member(name); field) {
                    auto this_ptr = _builder->CreateLoad(
                            _context->get_llvm_type(struct_type->get_reference()),
                            this_value_ref,
                            "this_ref"
                    );
                    ptr = _builder->CreateStructGEP(
                            _context->get_llvm_type(struct_type),
                            this_ptr,
                            (unsigned)field->index,
                            "this_" + struct_ref->get_short_name() + "_" + name + "_ptr"
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

    // TODO support pointer to pointer.

    if(!type::is_reference(sub_type)) {
        throw_error(0x0018, std::nullopt,
            "Cannot take the address of a non-reference expression: "
            "the '&' operator requires a reference (i.e. an addressable location) as its operand, "
            "but the operand has type '{}'",
            {sub_type ? sub_type->to_string() : "?"});
    }

    expr.set_type(sub_type->get_subtype()->get_pointer());
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
        expr.set_type(ref_type->get_subtype());
    } else if(auto ptr_type = std::dynamic_pointer_cast<pointer_type>(type)) {
        expr.set_type(ptr_type->get_subtype());
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

    if(auto ref_type = std::dynamic_pointer_cast<reference_type>(type)) {
        if(auto sub_ref_type = std::dynamic_pointer_cast<pointer_type>(ref_type->get_subtype())) {
            type = sub_ref_type;
        } else {
            throw_error(0x001A, std::nullopt,
                "Cannot dereference a reference to a non-pointer type: "
                "the dereference operator ('*') on a reference requires the referenced type to be a pointer, "
                "but '{}' is not a pointer type",
                {ref_type->get_subtype() ? ref_type->get_subtype()->to_string() : "?"});
        }
    }

    if(auto ptr_type = std::dynamic_pointer_cast<pointer_type>(type)) {
        expr.set_type(ptr_type->get_subtype()->get_reference());
    } else {
        throw_error(0x001B, std::nullopt,
            "Cannot dereference a non-pointer expression: "
            "the dereference operator ('*') requires a pointer or reference-to-pointer operand, "
            "but the operand has type '{}'",
            {type ? type->to_string() : "?"});
    }
}

void implementation_generator::visit_dereference_expression(dereference_expression& expr) {
    _value = nullptr;
    expr.sub_expr()->accept(*this);
    // Just keep the returned address : internally, a reference is a pointer

    if(auto ref_type = std::dynamic_pointer_cast<reference_type>(expr.sub_expr()->get_type())) {
        if(auto sub_ref_type = std::dynamic_pointer_cast<pointer_type>(ref_type->get_subtype())) {
            llvm::Type* type = _context->get_llvm_type(sub_ref_type);
            _value = _builder->CreateLoad(type, _value);
        }
    }
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
        const auto& member_name =  expr.symbol();
        if(auto field = struct_subtype->get_member(member_name.get_name()); field) {
            expr.set_type(field->field_type.lock()->get_reference());
        } else if(auto method = struct_subtype->get_struct()->get_function(member_name.get_name())) {
            // Member function: type resolution deferred to function_invocation_expression
        } else {
            throw_error(0x001D, std::nullopt,
                "No member named '{}' in struct '{}': "
                "check the spelling or verify that '{}' is declared as a field or method of '{}'",
                {member_name.get_name().to_string(), struct_subtype->name(),
                 member_name.get_name().to_string(), struct_subtype->name()});
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
// Member of pointer expression
//
void type_reference_resolver::visit_member_of_pointer_expression(member_of_pointer_expression& expr) {
    // TODO
}

void implementation_generator::visit_member_of_pointer_expression(member_of_pointer_expression& expr) {
    // TODO
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
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    auto left_type = expr.left()->get_type();

    // Dereference if double ref
    if(type::is_double_reference(left_type)) {
        left_type = left_type->get_subtype();
        left = _builder->CreateLoad(_context->get_llvm_type(left_type), left);
    }

    // Dereference index if needed
    auto right_type = expr.right()->get_type();
    if(type::is_reference(right_type)) {
        right_type = std::dynamic_pointer_cast<reference_type>(right_type)->get_subtype();
        right = _builder->CreateLoad(_context->get_llvm_type(right_type), right);
    }

    auto arr_type = _context->get_llvm_type(left_type->get_subtype());

    llvm::Value* indices[] = {_builder->getInt32(0), right};

//    _value = _builder->Insert(llvm::GetElementPtrInst::Create(arr_type, left, indices));
    _value = _builder->CreateGEP(arr_type, left, indices);
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
        member_callee->sub_expr()->accept(*this);

        callee = std::dynamic_pointer_cast<symbol_expression>(
                member_callee->symbol().shared_as<symbol_expression>());
        if (!callee) {
            throw_error(0x0023, std::nullopt,
                "Unsupported member call form: the right-hand side of '.' must be a simple name, "
                "not a complex expression");
        }

        // sub_expr of member_callee gives the object reference
        auto this_expr = member_callee->sub_expr();
        auto this_type = this_expr->get_type(); // should be ref<struct>

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
                    for (auto& f : scope_lookup::lookup_functions(st, func_name)) {
                        if (std::find(all_candidates.begin(), all_candidates.end(), f) == all_candidates.end()) {
                            all_candidates.push_back(f);
                        }
                    }
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
        auto [best_constructor, adapted_args] = get_best_matching_constructor(st_type->get_struct()->constructors(), expr.arguments());
        if (!best_constructor) {
            throw_error(0x002A, std::nullopt,
                "No matching constructor found for member initialisation of type '{}': "
                "none of the available constructors can be called with the provided arguments",
                {st_type->to_string()});
        }
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

} // namespace k::model::gen
