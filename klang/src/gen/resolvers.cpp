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
// Note: Last resolver log number: 0x30004
//

#include "resolvers.hpp"

namespace k::model::gen {


//
// Exceptions
//
resolution_error::resolution_error(const std::string &arg) :
        runtime_error(arg)
{}

resolution_error::resolution_error(const char *string) :
        runtime_error(string)
{}

//
// Symbol resolver
//

void symbol_resolver::resolve()
{
    visit_unit(_unit);
}

std::variant<std::monostate, std::shared_ptr<variable_definition>, std::shared_ptr<function>> // TODO Add traversal direction flag
symbol_resolver::resolve_symbol(const element& elem, const name& name) {

    // Specifically look at the "this" symbol (non-static function specific parameter)
    if (name.size() == 1 && name.to_string() == "this") {
        auto func = elem.ancestor<function>();
        while (func) {
            if (func->is_member() && func->get_this_parameter()) {
                return std::const_pointer_cast<parameter>(func->get_this_parameter());
            }
            func = func->ancestor<function>();
        }
        // TODO throw exception Symbol 'this' used outside a non-static member function.
        std::clog << "Symbol 'this' used outside a non-static member function." << std::endl;
        return std::monostate{};
    }

    if (name.has_root_prefix()) {
        // TODO if name has root prefix, look at the unit directly.
        std::clog << "Try to resolve symbol with root prefix: " << name.to_string() << std::endl;
    } else if (name.empty()) {
        // Invalid name, must have at least one part
        return std::monostate{};
    } else if(name.size() > 1) {
        // Qualified name

        // Look at structures
        if (auto st_holder = dynamic_cast<const structure_holder*>(&elem)) {
            if (auto st = st_holder->get_structure(name.front())) {
                if (auto res = resolve_symbol(*st, name.without_front()); res.index()!=0) {
                    return res;
                }
            }
        }

        // Look at namespace
        if (auto nspc = dynamic_cast<const ns*>(&elem)) {
            if (auto child = nspc->get_child_namespace(name.front())) {
                if (auto res = resolve_symbol(*child, name.without_front()); res.index()!=0) {
                    return res;
                }
            }
        }

    } else /*(name.size() == 1)*/ {
        // Simple name, try to resolve it directly

        // Look at a variable
        if (auto var_holder = dynamic_cast<const variable_holder*>(&elem)) {
            if (auto def = var_holder->get_variable(name)) {
                return def;
            }
        }

        // Look at a function
        if (auto func_holder = dynamic_cast<const function_holder*>(&elem)) {
            if (auto func = func_holder->lookup_function(name.to_string())) {
                return func;
            }
        }

        // TODO: Workaround, remove it when function will be a (parameter) variable_holder
        if (auto blck = dynamic_cast<const block*>(&elem)) {
            if (auto func = blck->get_direct_function()) {
                if (auto param = func->get_parameter(name.to_string())) {
                    return std::const_pointer_cast<parameter>(param);
                }
            }
        }
    }

    if (auto parent_elem = elem.parent<element>()) {
        // Try to find the symbol in the parent element context
        return resolve_symbol(*parent_elem, name);
    } else {
        // No parent element, cannot resolve symbol here
        return std::monostate{};
    }
}

std::shared_ptr<expression> symbol_resolver::adapt_reference_load_value(const std::shared_ptr<expression>& expr) {
    auto type = expr->get_type();

    if(!expr || !type::is_resolved(type)) {
        // Arguments must not be null, expr must have a type and this must be resolved.
        return nullptr;
    }

    if(type::is_reference(type)) {
        auto deref = load_value_expression::make_shared(expr);
        deref->set_type(type->get_subtype());
        return deref;
    } else {
        return expr;
    }
}


std::shared_ptr<expression> symbol_resolver::adapt_type(std::shared_ptr<expression> expr, const std::shared_ptr<type>& type) {
    if(!expr || !type::is_resolved(type) || !type::is_resolved(expr->get_type())) {
        // Arguments must not be null, expr must have a type and types (expr and target) must be resolved.
        return nullptr;
    }

    auto type_src = expr->get_type();

    if(type::is_pointer(type_src)) {
        if(type::is_pointer(type)) {
            if (type == type_src) {
                // Pointers to same type, return the expression
                return expr;
            } else {
                // Pointers to different types
                // TODO verify casting
                return {};
            }
        } else {
            // Error : Source is a pointer, and asked to be cast to an object.
            return {};
        }
    }

    if(type::is_double_reference(type_src)) {
        auto ref_src = std::dynamic_pointer_cast<reference_type>(type_src);
        auto deref = load_value_expression::make_shared(expr);
        deref->set_type(ref_src->get_subtype());
        expr = deref;
        type_src = ref_src->get_subtype();
    }

    if(type::is_reference(type_src)) {
        if(type::is_reference(type)) {
            if (type == type_src) {
                // Reference to same type, return the expression
                return expr;
            } else {
                // Reference to different types
                // TODO verify casting
                return {};
            }
        }
        auto ref_src = std::dynamic_pointer_cast<reference_type>(type_src);
        if(ref_src->get_subtype() == type) {
            return adapt_reference_load_value(expr);
        }
    }

    auto prim_src = std::dynamic_pointer_cast<primitive_type>(expr->get_type());
    auto prim_tgt = std::dynamic_pointer_cast<primitive_type>(type);

    if(!prim_src || !prim_tgt) {
        // Support only primitive types for now.
        // TODO support not-primitive type casting
        return {};
    }

    if(prim_src && prim_tgt && *prim_src==*prim_tgt) {
        // Trivially agree for same types
        return expr;
    }

    auto cast = cast_expression::make_shared(expr, prim_tgt);
    cast->set_type(prim_tgt);
    return cast;
}

//
// Type resolver
//

void type_reference_resolver::resolve()
{
    visit_unit(_unit);
}

void type_reference_resolver::visit_variable_definition(variable_definition& var)
{
    if(!type::is_resolved(var.get_type())) {
        auto unres_type = std::dynamic_pointer_cast<unresolved_type>(var.get_type());
        if(!unres_type) {
            // TODO throw an exception
            std::cerr << "Error: global variable definition has an unresolvable type." << std::endl;
        }
        auto type = _context->from_string(unres_type->type_id());
        if(!type || !type::is_resolved(type)) {
            // TODO throw an exception
            std::cerr << "Error: global variable definition has an unresolvable type." << std::endl;
        } else {
            var.set_type(type);
        }
    }

    // Resolve init expressions if any
    auto init_expr = var.get_init_expr();
    if (init_expr) {
        init_expr->accept(*this);
    }

    auto var_type = var.get_type();

    if (type::is_primitive(var_type)) {
        // Primitive type supports only one init expression, always try to cast it, if any.
        if (!init_expr || init_expr->empty()) {
            // If no explicit initialization, let's have 0-filled initialization:
        } else if (init_expr->size() > 1) {
            // TODO throw an exception
            std::cerr << "Error: global variable of primitive type can only have one initialization expression" << std::endl;
        } else if (auto expr = init_expr->argument(0)) {

            // Align init expr type to variable type
            auto cast = adapt_type(expr, var_type);
            if(!cast) {
                // TODO throw_error(0x0004, var.get_ast_for_stmt()->for_kw, "For test expression type must be convertible to bool");
            } else if(cast != expr) {
                // Casted, assign casted expression as return expr.
                init_expr->assign_argument(0, cast);
            } else {
                // Compatible type, no need to cast.
            }

        } else {
            // TODO throw an exception
            std::cerr << "Error: global variable of primitive type has empty initialization expression" << std::endl;
        }

    } else if (auto st_type = std::dynamic_pointer_cast<struct_type>(var.get_type())) {
        // Structure, try to find the right constructor
        auto [best_constructor, adapted_args] = get_best_matching_constructor(st_type->get_struct()->constructors(), init_expr ? init_expr->arguments() : std::vector<std::shared_ptr<expression>>{});
        if (!best_constructor) {
            // TODO throw an exception
            std::cerr << "Error: no matching constructor found for global variable initialization" << std::endl;
        }
        var.set_var_constructor(best_constructor);
        if (init_expr) {
            init_expr->set_constructor(best_constructor);
            init_expr->arguments(adapted_args);
        }

    } else {
        // Unsupported construction for other types for now
        // TODO Support construction for other types (ref, array, etc.)
    }
}

std::pair<std::shared_ptr<constructor>/*best_constructor*/, std::vector<std::shared_ptr<expression>>/*adapted_args*/>
type_reference_resolver::get_best_matching_constructor(const std::vector<std::shared_ptr<constructor>>& constructors, const std::vector<std::shared_ptr<expression>>& args) {
    std::shared_ptr<constructor> best_constructor;
    std::vector<std::shared_ptr<expression>> adapted_args;
    int best_cost = std::numeric_limits<int>::max();

    for (auto& constructor : constructors) {
        if (constructor->parameters().size() == args.size()) {
            bool all_args_compatible = true;
            int cost = 0;
            std::vector<std::shared_ptr<expression>> current_adapted_args;
            for (size_t i = 0; i < args.size(); ++i) {
                auto param_type = constructor->parameters()[i]->get_type();
                auto arg_expr = args[i];
                auto adapted_arg = adapt_type(arg_expr, param_type);
                if (!adapted_arg) {
                    // Not compatible, don't need to look up further
                    all_args_compatible = false;
                    break;
                }
                if (adapted_arg!=arg_expr) {
                    cost++;
                    if (cost>=best_cost) {
                        // Cost is higher than best cost, don't need to look up further
                        break;
                    }
                }
                current_adapted_args.push_back(adapted_arg);
            }
            if (all_args_compatible && cost < best_cost) {
                if (cost==0) {
                    // Perfect match, no need to continue checking other constructors
                    return {std::move(constructor), std::move(current_adapted_args)};
                }
                best_constructor = constructor;
                adapted_args = std::move(current_adapted_args);
                best_cost = cost;
            }
        }
    }

    return {std::move(best_constructor), std::move(adapted_args)};
}


std::shared_ptr<expression> type_reference_resolver::adapt_reference_load_value(const std::shared_ptr<expression>& expr) {
    auto type = expr->get_type();

    if(!expr || !type::is_resolved(type)) {
        // Arguments must not be null, expr must have a type and this must be resolved.
        return nullptr;
    }

    if(type::is_reference(type)) {
        auto deref = load_value_expression::make_shared(expr);
        deref->set_type(type->get_subtype());
        return deref;
    } else {
        return expr;
    }
}

std::shared_ptr<expression> type_reference_resolver::adapt_type(std::shared_ptr<expression> expr, const std::shared_ptr<type>& type) {
    if(!expr || !type::is_resolved(type) || !type::is_resolved(expr->get_type())) {
        // Arguments must not be null, expr must have a type and types (expr and target) must be resolved.
        return nullptr;
    }

    auto type_src = expr->get_type();

    if(type::is_pointer(type_src)) {
        if(type::is_pointer(type)) {
            if (type == type_src) {
                // Pointers to same type, return the expression
                return expr;
            } else {
                // Pointers to different types
                // TODO verify casting
                return {};
            }
        } else {
            // Error : Source is a pointer, and asked to be cast to an object.
            return {};
        }
    }

    if(type::is_double_reference(type_src)) {
        auto ref_src = std::dynamic_pointer_cast<reference_type>(type_src);
        auto deref = load_value_expression::make_shared(expr);
        deref->set_type(ref_src->get_subtype());
        expr = deref;
        type_src = ref_src->get_subtype();
    }

    if(type::is_reference(type_src)) {
        if(type::is_reference(type)) {
            if (type == type_src) {
                // Reference to same type, return the expression
                return expr;
            } else {
                // Reference to different types
                // TODO verify casting
                return {};
            }
        }
        auto ref_src = std::dynamic_pointer_cast<reference_type>(type_src);
        if(ref_src->get_subtype() == type) {
            return adapt_reference_load_value(expr);
        }
    }

    auto prim_src = std::dynamic_pointer_cast<primitive_type>(expr->get_type());
    auto prim_tgt = std::dynamic_pointer_cast<primitive_type>(type);

    if(!prim_src || !prim_tgt) {
        // Support only primitive types for now.
        // TODO support not-primitive type casting
        return {};
    }

    if(*prim_src==*prim_tgt) {
        // Trivially agree for same types
        return expr;
    }

    auto cast = cast_expression::make_shared(expr, prim_tgt);
    cast->set_type(prim_tgt);
    return cast;
}



} // k::model::gen
