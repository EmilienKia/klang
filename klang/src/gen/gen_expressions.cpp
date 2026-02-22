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
        // Symbol not found
        // TODO throw an exception
        std::cerr << "Error: Unable to resolve symbol '" << symbol.get_name().to_string() << "'." << std::endl;
    }
}

void type_reference_resolver::visit_symbol_expression(symbol_expression& symbol)
{
    if(!symbol.is_resolved()) {
        // TODO throw an exception
        std::cerr << "Error: symbol expression is not resolved." << std::endl;
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
                // TODO throw exception : no function found in context for member variable access
                std::cerr << "Error: no function context available for member variable '" << member_var->get_fq_name() << "' access." << std::endl;
            }
            this_value_ref = _context->_function_this_variables[func];
            if (!this_value_ref) {
                // TODO throw exception : no 'this' pointer found in function for member variable access
                std::cerr << "Error: no 'this' pointer available in function context for member variable '" << member_var->get_fq_name() << "' access." << std::endl;
            }

            // Get member variable
            if(_struct_stack.empty()) {
                // TODO throw exception : no 'this' pointer available for member variable access
                std::cerr << "Error: no 'this' context available for member variable '" << name << "' access." << std::endl;
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
                    // TODO throw an exception
                    std::cerr << "Error: Struct type '" << struct_type->name() << "' has no member named '" << name << "'. (1)" << std::endl;
                }
            } else { // TODO add here the method resolution
                // TODO throw an exception
                std::cout << "Error: Struct type has no type information for member variable '" << name << "' access." << std::endl;
            }

        } else {
            // TODO Support other types of variable definitions
            std::cout << "Error: Unsupported variable definition type in symbol expression." << std::endl;
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
            // Error: function definition is not found.
            // TODO throw exception
            std::cerr << "Error: function definition is not found." << std::endl;
        }
        llvm::Function* llvm_func = it->second;
        if(!llvm_func) {
            // Error: function definition is not found.
            // TODO throw exception
            std::cerr << "Error: llvm function definition is not found." << std::endl;
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
        // TODO throw an exception
        // Error 0x0002: unary expression must have non-null sub expresssion
        std::cerr << "Error: unary expression must have non-null sub expresssion" << std::endl;
    }
    sub->accept(*this);
}

void type_reference_resolver::visit_unary_expression(unary_expression& expr)
{
    auto& sub = expr.sub_expr();

    if(!sub) {
        // TODO throw an exception
        // Error 0x0002: unary expression must have non-null sub expresssion
        std::cerr << "Error: unary expression must have non-null sub expresssion" << std::endl;
    }

    sub->accept(*this);

    if(!type::is_resolved(sub->get_type())) {
        // TODO throw an exception
        // Error 0x0003: unary expression must have resolved type for its sub-expression
        std::cerr << "Error: unary expression must have resolved type for its sub-expression" << std::endl;
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
        // TODO throw an exception
        // Error 0x0004: binary expression must have non-null left and right expresssion
        std::cerr << "Error: binary expression must have non-null left and right expresssion" << std::endl;
    }

    left->accept(*this);
    right->accept(*this);

}

void type_reference_resolver::visit_binary_expression(binary_expression& expr)
{
    auto& left = expr.left();
    auto& right = expr.right();

    if(!left || !right) {
        // TODO throw an exception
        // Error 0x0004: binary expression must have non-null left and right expresssion
        std::cerr << "Error: binary expression must have non-null left and right expresssion" << std::endl;
    }

    left->accept(*this);
    right->accept(*this);

    if(!type::is_resolved(left->get_type())) {
        // TODO throw an exception
        // Error 0x0005: Error: left sub-expression of binary expression must have resolved type
        std::cerr << "Error: left sub-expression of binary expression must have resolved type" << std::endl;
    }
    if(!type::is_resolved(right->get_type())) {
        // TODO throw an exception
        // Error 0x0005b: Error: right sub-expression of binary expression must have resolved type
        std::cerr << "Error: right sub-expression of binary expression must have resolved type" << std::endl;
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
        // TODO throw an exception
        std::cerr << "Error: Address-of expression can be applied only to reference types." << std::endl;
    }

    expr.set_type(sub_type->get_subtype()->get_pointer());
}

void implementation_generator::visit_address_of_expression(address_of_expression& expr) {
    _value = nullptr;
    expr.sub_expr()->accept(*this);

    if(!_value) {
        // TODO throw an exception
        std::cerr << "Error: Sub-expression of address-of expression must return a value." << std::endl;
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
        expr.set_type(ref_type->get_subtype());
    } else {
        // TODO throw an exception
        std::cerr << "Error: Load-expression can be applied only to pointer and reference types." << std::endl;
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
            // Error : If subtype is a reference, it must ref a pointer.
            // TODO throw an exception
            std::cerr << "Error: Dereference can be applied only to pointer types or references to pointer types." << std::endl;
        }
    }

    if(auto ptr_type = std::dynamic_pointer_cast<pointer_type>(type)) {
        expr.set_type(ptr_type->get_subtype()->get_reference());
    } else {
        // TODO throw an exception
        std::cerr << "Error: Dereference can be applied only to pointer types." << std::endl;
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
        // TODO throw an exception
        std::cerr << "Error: Member-of-object expression can be applied only to reference types." << std::endl;
    }
    auto subtype = type->get_subtype();
    /*if(type::is_reference(subtype)) {
        // Dereference the subreference to handle (double reference) for case like reference parameter (like 'this')
        subtype = subtype->get_subtype();
    }*/
    if(auto struct_subtype = std::dynamic_pointer_cast<struct_type>(subtype)) {
        const auto& member_name =  expr.symbol();
        if(auto field = struct_subtype->get_member(member_name.get_name()); field) {
            expr.set_type(field->field_type.lock()->get_reference());
        } else if(auto method = struct_subtype->get_struct()->get_function(member_name.get_name())) {
            // TODO Refactor to return the function type
            std::clog << "Info: Looking for member of object for " << struct_subtype->name() << "::" << method->get_name().to_string() << " (1)" << std::endl;
        } else {
            // TODO throw an exception
            std::cerr << "Error: Struct type '" << struct_subtype->name() << "' has no member named '" << member_name.get_name().to_string() << "'. (2)" << std::endl;
        }
    } else { // TODO add here other types of objects
        // TODO throw an exception
        std::cerr << "Error: Member-of-object expression can be applied only to struct types. (1)" << std::endl;
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
            std::clog << "Trace: Looking for member of object for " << struct_subtype->name() << "::" << method->get_name().to_string() << " (2)"  << std::endl;
            // Note return the already-assigned address of the struct onto which the function is applied to
        } else {
            // TODO throw an exception
            std::cerr << "Error: Struct type '" << struct_subtype->name() << "' has no member named '" << member_name.get_name().to_string() << "'. (3)" << std::endl;
        }
    } else { // TODO add here the method resolution
        // TODO throw an exception
        std::cerr << "Error: Member-of-object expression can be applied only to struct types. (2)" << std::endl;
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
        // TODO throw an exception
        // Subscript expression is supported only for references to arrays.
        std::cerr << "Error: Subscript expression is supported only for reference to arrays." << std::endl;
    }
    if(type::is_double_reference(left_type)) {
        // Deref first ref
        left_type = left_type->get_subtype();
    }
    left_type = std::dynamic_pointer_cast<reference_type>(left_type)->get_subtype();

    if(!type::is_array(left_type)) {
        // TODO throw an exception
        // Subscript expression is supported only for arrays.
        std::cerr << "Error: Subscript expression is supported only for arrays." << std::endl;
    }
    auto arr_type = std::dynamic_pointer_cast<array_type>(left_type);
    expr.set_type(arr_type->get_subtype()->get_reference());

    // Check the right hand can be cast to unsigned integer
    // TODO adapt to the really right index type.
    // TODO is array really indexed by uint ?
    auto adapted_right = adapt_type(right, _context->from_type(primitive_type::UNSIGNED_INT));
    if(!adapted_right) {
        // TODO thrown an exception
        // Error: cannot cast index expression to index type
        std::cerr << "Error: Cannot cast index expression to index type in subscript expression." << std::endl;
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
        std::cerr << "Error : only support global or object-member method call" << std::endl;
        return;
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
            std::cerr << "Error : only support object-member method call with symbol expression" << std::endl;
            return;
        }

        // sub_expr of member_callee gives the object reference
        auto this_expr = member_callee->sub_expr();
        auto this_type = this_expr->get_type(); // should be ref<struct>

        if (!type::is_reference(this_type)) {
            std::cerr << "Error : member-of-object call requires a reference type." << std::endl;
            return;
        }
        auto subtype = type::is_reference(this_type) ? this_type->get_subtype() : this_type;
        auto struct_subtype = std::dynamic_pointer_cast<struct_type>(subtype);
        if (!struct_subtype) {
            std::cerr << "Error : member-of-object call only supports struct types." << std::endl;
            return;
        }
        auto st = struct_subtype->get_struct();

        // Use the short (unqualified) name for function lookup
        std::string func_short_name = callee->get_name().back();

        // Collect all candidate functions (member + free/static from parent scopes)
        std::vector<std::shared_ptr<function>> candidates = scope_lookup::lookup_functions(st, func_short_name);

        if (candidates.empty()) {
            std::cerr << "Error : cannot find any function named '"
                      << callee->get_name().to_string()
                      << "' accessible from struct '" << st->get_short_name() << "'" << std::endl;
            return;
        }

        auto best = get_best_matching_function(candidates, expr.arguments(), this_expr);
        if (!best.func) {
            std::cerr << "Error : no viable overload for '"
                      << callee->get_name().to_string() << "'." << std::endl;
            return;
        }

        callee->set_target(best.func);
        expr.set_type(best.func->get_return_type());

        // Apply adapted arguments
        for (size_t i = 0; i < best.adapted_args.size(); ++i) {
            expr.assign_argument(i, best.adapted_args[i]);
        }
        // Note: if best.is_unified_call, the callee stays as member_of_object_expression
        // but the resolved function is free/static. impl_gen handles this by passing
        // sub_expr() value as first argument when the function is not a member.
        return;
    }

    // ----------------------------------------------------------------
    // Case 2 : plain symbol call  "func(args)"
    // ----------------------------------------------------------------
    // Always perform overload resolution even if symbol_resolver already resolved
    // a candidate (it may have picked the wrong overload without type info).
    // We collect ALL candidates (from scope chain + struct members of first arg)
    // and score them in a single pass that handles:
    //   Mode A: member call with this_expr + rest_args
    //   Mode B: free/static direct call with all args (even when this_expr is set)
    //   Mode C: unified call (free/static with first param = ref<struct of this_expr>)
    {
        // Use the short (unqualified) name for function lookup
        std::string func_name = callee->get_name().back();
        const auto& args = expr.arguments();

        // --- Collect candidates from the scope chain ---
        std::vector<std::shared_ptr<function>> all_candidates = scope_lookup::lookup_functions(callee, func_name);

        // --- If first arg is ref<struct>, also collect candidates from that struct ---
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
            // Fallback: if callee was already resolved by symbol_resolver, use it directly.
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
            std::cerr << "Error : no viable overload for '" << func_name << "'." << std::endl;
            return;
        }

        // --- Single scoring pass: Mode A/C use rest_args+this_candidate; Mode B uses full args ---
        // Mode A: member func, this_candidate provides 'this', rest_args are the explicit params
        // Mode B: free/static, full args passed directly via direct_args (may include obj as first)
        // Mode C: free/static, this_candidate + rest_args as the rest (params.size()==rest_args.size()+1)
        FunctionCandidate best = get_best_matching_function(all_candidates,
                                                            this_candidate ? rest_args : args,
                                                            this_candidate,
                                                            this_candidate ? &args : nullptr);
        bool is_free_to_member_call = false;

        if (!best.func) {
            std::cerr << "Error : no viable overload for '" << func_name << "'." << std::endl;
            return;
        }

        // Determine if this is a free-function-called-as-member transformation
        if (this_candidate && best.func->is_member() && !best.func->is_static() && !best.is_unified_call) {
            is_free_to_member_call = true;
        }

        callee->set_target(best.func);
        expr.set_type(best.func->get_return_type());

        if (best.is_unified_call) {
            for (size_t i = 0; i < best.adapted_args.size(); ++i) {
                expr.assign_argument(i, best.adapted_args[i]);
            }
        } else if (is_free_to_member_call) {
            // Member function found via free-function syntax: func(obj, args...)
            auto obj_expr = expr.arguments()[0];
            auto sym_for_member = symbol_expression::from_function(best.func);
            sym_for_member->set_target(best.func);
            auto member_expr = member_of_object_expression::make_shared(obj_expr, sym_for_member);
            expr.assign(member_expr, best.adapted_args);
        } else if (this_candidate && best.adapted_args.size() == args.size()) {
            // Mode B was selected (free/static direct with all args including first)
            for (size_t i = 0; i < best.adapted_args.size(); ++i) {
                expr.assign_argument(i, best.adapted_args[i]);
            }
        } else {
            // Regular free/static function call (no this_expr involved)
            for (size_t i = 0; i < best.adapted_args.size(); ++i) {
                expr.assign_argument(i, best.adapted_args[i]);
            }
        }
        return;
    }
}

void implementation_generator::visit_function_invocation_expression(function_invocation_expression &expr) {
    auto callee = std::dynamic_pointer_cast<symbol_expression>(expr.callee_expr());
    auto member_callee = std::dynamic_pointer_cast<member_of_object_expression>(expr.callee_expr());

    if(!callee && !member_callee) {
        std::cerr << "Error : only support global or object-member method call" << std::endl;
    }

    // Generate arguments and add the to the args list
    std::vector<llvm::Value*> args;
    if (member_callee) {
        callee = std::dynamic_pointer_cast<symbol_expression>(member_callee->symbol().shared_as<symbol_expression>());
        if (!callee) {
            std::cerr << "Error : only support object-member method call with symbol expression" << std::endl;
        }

        // First argument is the object pointer (this)
        member_callee->sub_expr()->accept(*this);
        if(!_value) {
            // Problem with 'this' argument generation
            // TODO throw exception
            std::cerr << "Problem with generation of 'this' argument of a member function call." << std::endl;
        }

        args.push_back(_value);
    }
    for(auto arg : expr.arguments()) {
        _value = nullptr;
        arg->accept(*this);
        if(!_value) {
            // Problem with argument generation
            // TODO throw exception
            std::cerr << "Problem with generation of an argument of a function call." << std::endl;
        }
        args.push_back(_value);
    }

    // TODO Check function argument count

    // Find the function definition
    auto function = callee->get_function();
    auto it = _context->_functions.find(function);
    if(it==_context->_functions.end()) {
        // Error: function definition is not found.
        // TODO throw exception
        std::cerr << "Error: function definition is not found: " << (function ? function->get_fq_name() : "<null>") << std::endl;
        _value = nullptr;
        return;
    }
    llvm::Function* llvm_func = it->second;
    if(!llvm_func) {
        // Error: function definition is not found.
        // TODO throw exception
        std::cerr << "Error: llvm function definition is not found." << std::endl;
        _value = nullptr;
        return;
    }
    // TODO look for external functions.

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
        // Must not happen, should be already checked at resolution phase.
        // TODO throw an exception
        std::cerr << "Error: constructor invocation only support variable definition as constructed symbol." << std::endl;
    }
    expr.set_type(var_def->get_type()->get_reference());

    // Check if constructor is explicitly needed
    auto var_type = var_def->get_type();
    if (!var_type) {
        // Must not happen, should be already checked at resolution phase.
        // TODO throw an exception
        std::cerr << "Error: constructor invocation cannot find type for constructed symbol." << std::endl;
    }
    if (type::is_primitive(var_type)) {
        // Do nothing, direct inline construction, no constructor method, so no need to resolve a constructor function.
    } else if (auto st_type = std::dynamic_pointer_cast<struct_type>(var_type)) {
        auto st = st_type->get_struct();
        auto [best_constructor, adapted_args] = get_best_matching_constructor(st_type->get_struct()->constructors(), expr.arguments());
        if (!best_constructor) {
            // TODO throw an exception
            std::cerr << "Error: no matching constructor found for global variable initialization" << std::endl;
        }
        expr.set_constructor(best_constructor);
    }

}

void implementation_generator::visit_constructor_invocation_expression(constructor_invocation_expression& expr) {
    // NOTE : The IR builder must be at the right place (in method block for local variables, in global constructor for global variables)
    auto var_def = expr.constructed_symbol()->get_variable_def();
    if (!var_def) {
        // Must not happen, should be already checked at resolution phase.
        // TODO throw an exception
        std::cerr << "Error: constructor invocation only support variable definition as constructed symbol." << std::endl;
    }

    auto var_type = var_def->get_type();

    llvm::Value* object_ref = nullptr;
    _value = nullptr;
    expr.constructed_symbol()->accept(*this);
    object_ref = _value;
    _value = nullptr;

    if(!object_ref) {
        // Must not happen, should be already checked at variable definition codegen.
        // TODO throw an exception
        std::cerr << "Error: constructor invocation cannot find llvm reference for constructed symbol." << std::endl;
    }

    if (auto prim_type = std::dynamic_pointer_cast<primitive_type>(var_type)) {
        // Primitive type has a direct initialization (no constructor function), so just generate the init expression if any, and store the value in the variable address.
        llvm::Value* value = nullptr;
        if (!expr.empty()) {
            auto first_arg = expr.argument(0);
            if (auto value_expr = std::dynamic_pointer_cast<value_expression>(first_arg)) {
                if (!std::dynamic_pointer_cast<global_variable_definition>(value_expr)) {
                    // Primitive constant, just store it
                    llvm::Constant* constant = _context->get_llvm_constant_from_value_expression(*value_expr);
                    if (constant == nullptr) {
                        // TODO throw an exception
                        std::cerr << "Error: cannot generate llvm constant from value expression for constructor invocation." << std::endl;
                    } else {
                        value = constant;
                    }
                }
            }
            if (value == nullptr) {
                // No constant value, generate the expression as usual and store the result.
                _value = nullptr;
                first_arg->accept(*this);
                if (!_value) {
                    // TODO throw an exception
                    std::cerr << "Error: cannot generate value for constructor argument." << std::endl;
                } else {
                    value = _value;
                }
            }
        }
        if (value != nullptr) {
            _builder->CreateStore(value, object_ref);
        }
    } else if (auto st_type = std::dynamic_pointer_cast<struct_type>(var_type)) {
        // For struct type, constructor function is generated, so just call it and store the result
        auto st = st_type->get_struct();

        // Generate arguments and add the to the args list
        std::vector<llvm::Value*> args;

        // First, add the address of the struct to construct as first argument (this)
        args.push_back(object_ref);

        // Then add constructor arguments if any
        for(auto arg : expr.arguments()) {
            _value = nullptr;
            arg->accept(*this);
            if(!_value) {
                // Problem with argument generation
                // TODO throw exception
                std::cerr << "Problem with generation of an argument of a function call." << std::endl;
            }
            args.push_back(_value);
        }

        // Find the function definition
        auto function = expr.get_constructor();
        auto it = _context->_functions.find(function);
        if(it==_context->_functions.end()) {
            // Error: function definition is not found.
            // TODO throw exception
            std::cerr << "Error: constructor function definition is not found." << std::endl;
        }
        llvm::Function* llvm_func = it->second;
        if(!llvm_func) {
            // Error: function definition is not found.
            // TODO throw exception
            std::cerr << "Error: llvm function definition is not found." << std::endl;
        }
        _value = _builder->CreateCall(llvm_func, args);

    } else {
        // TODO This is probably a non-primitive primary type, so direct construction will be done

        /*
        if(auto init = var_def->get_init_expr()) {
            _value = nullptr;
            init->accept(*this);
            if (_value!=nullptr) {
                _builder->CreateStore(_value, alloca);
                _value = nullptr;
            } else {
                // TODO handle error (nullptr) in init expr generation
            }
            */
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
        // Error: source and target types must be both resolved.
        // TODO throw exception
        std::cerr << "Error: in casting expression, both source and target types must be resolved." << std::endl;
    }

    if(type::is_pointer(source_type) && type::is_prim_bool(target_type)) {
        // TODO add pointer to boolean casting
    }

    if(!type::is_primitive(source_type) || !type::is_primitive(target_type)) {
        // TODO Support also non primitive type
        std::cerr << "Error: in casting expression, only primitive types are supported yet." << std::endl;
    }
    auto src = std::dynamic_pointer_cast<primitive_type>(source_type);
    auto tgt = std::dynamic_pointer_cast<primitive_type>(target_type);

    _value = nullptr;
    expr.sub_expr()->accept(*this);
    if(!_value) {
        // TODO throw exception
        // Sub expression is not reporting any value.
        std::cerr << "Error: in casting expression, expression to cast is not returning any value." << std::endl;
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
                    // TODO Add "Unsigned to signed" overflow warning
                    std::cerr << "Cast unsigned integer to signed integer may result on overflow" << std::endl;
                }
                // SExt or trunc for signed integers
                _value = _builder->CreateSExtOrTrunc(_value, _context->get_llvm_type(tgt));
            } else /* if (tgt->is_unsigned())*/  {
                if (src->is_unsigned()) {
                    // TODO Add "Signed to unsigned" truncation/misunderstanding warning
                    std::cerr
                            << "Cast signed integer to unsigned integer may result on truncating/misinterpreting of integers"
                            << std::endl;
                }
                // SExt or trunc for signed integers
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
