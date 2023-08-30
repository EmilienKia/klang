//
// Created by Emilien Kia <emilien.kia+dev@gmail.com>.
//

#include "symbol_type_resolver.hpp"

namespace k::unit {

void symbol_type_resolver::resolve()
{
    visit_unit(_unit);
}

void symbol_type_resolver::visit_unit(unit& unit)
{
    visit_namespace(*_unit.get_root_namespace());
}

void symbol_type_resolver::visit_ns_element(ns_element& elem)
{
    if(auto ns = dynamic_cast<k::unit::ns*>(&elem)) {
        visit_namespace(*ns);
    } else if(auto var = dynamic_cast<k::unit::global_variable_definition*>(&elem)) {
        visit_global_variable_definition(*var);
    } else if(auto fn = dynamic_cast<k::unit::function*>(&elem)) {
        visit_function(*fn);
    } // else // Must not occur
}

void symbol_type_resolver::visit_namespace(ns& ns)
{
    bool has_name = false;
    if(!ns.get_name().empty()) {
        has_name = true;
        _naming_context.push_back(ns.get_name());
    }

    for(auto& child : ns.get_children()) {
        visit_ns_element(*child);
    }

    if(has_name) {
        _naming_context.pop_back();
    }
}
void symbol_type_resolver::visit_global_variable_definition(global_variable_definition& var)
{
    // TODO visit parameter definition (just in case default init is referencing a variable).

}

void symbol_type_resolver::visit_function(function& fn)
{
    _naming_context.push_back(fn.name());

    // TODO visit parameter definition (just in case default init is referencing a variable).

    if(auto block = fn.get_block()) {
        visit_block(*block);
    }

    _naming_context.pop_back();
}

void symbol_type_resolver::visit_statement(statement& stmt)
{
    if(auto block = dynamic_cast<k::unit::block*>(&stmt)) {
        visit_block(*block);
    } else if(auto ret = dynamic_cast<k::unit::return_statement*>(&stmt)) {
        visit_return_statement(*ret);
    } else if(auto expr = dynamic_cast<k::unit::expression_statement*>(&stmt)) {
        visit_expression_statement(*expr);
    } else if(auto var = dynamic_cast<k::unit::variable_statement*>(&stmt)) {
        visit_variable_statement(*var);
    } // else // Must not occur
}

void symbol_type_resolver::visit_block(block& block)
{
    for(auto& stmt : block.get_statements()) {
        visit_statement(*stmt);
    }
}

void symbol_type_resolver::visit_return_statement(return_statement& stmt)
{
    if(auto expr = stmt.get_expression()) {
        visit_expression(*expr);
    }
}

void symbol_type_resolver::visit_expression_statement(expression_statement& stmt)
{
    if(auto expr = stmt.get_expression()) {
        visit_expression(*expr);
    }
}

void symbol_type_resolver::visit_variable_statement(variable_statement& var)
{
    if(auto expr = var.get_init_expr()) {
        visit_expression(*expr);
    }
}

void symbol_type_resolver::visit_expression(expression& expr)
{
    if(auto val = dynamic_cast<k::unit::value_expression*>(&expr)) {
        visit_value_expression(*val);
    } else if(auto var = dynamic_cast<k::unit::symbol_expression*>(&expr)) {
        visit_symbol_expression(*var);
    } else if(auto func = dynamic_cast<k::unit::function_invocation_expression*>(&expr)) {
        visit_function_invocation_expression(*func);
    } else if(auto bin = dynamic_cast<k::unit::binary_expression*>(&expr)) {
        visit_binary_expression(*bin);
    } else if(auto cast = dynamic_cast<k::unit::cast_expression*>(&expr)) {
        visit_cast_expression(*cast);
    } // else // Must not occur
}

void symbol_type_resolver::visit_value_expression(value_expression& expr)
{
    // Nothing to view here:
    // - No symbol to resolve
    // - Type is supposed to have already been set at value_expression construction.
}

void symbol_type_resolver::visit_symbol_expression(symbol_expression& symbol)
{
    // TODO: Have to check the variable definition (for block-variable) is done before the expression;
    // TODO: Have to support all symbol types, not only variables
    if(!symbol.is_resolved()) {
        if(auto stmt = symbol.find_statement()) {
            if(auto block = stmt->get_block()) {
                if(auto def = block->lookup_variable(symbol.get_name())) {
                    symbol.resolve(def);
                    // Note: type is supposed to be applyed at resolution
                }
            }
        }
    }
}

void symbol_type_resolver::visit_binary_expression(binary_expression& expr)
{
    auto& left = expr.left();
    auto& right = expr.right();

    if(!left || !right) {
        // TODO throw an exception
        // Error: binary expression must have non-null left and right expresssion
        std::cerr << "Error: binary expression must have non-null left and right expresssion" << std::endl;
    }

    visit_expression(*left);
    visit_expression(*right);

    if(!left->get_type() || !left->get_type()->is_resolved() ||
            !right->get_type() || !right->get_type()->is_resolved()) {
        // TODO throw an exception
        // Error: binary expression must have resolved type at left and right sub-expression
        std::cerr << "Error: binary expression must have resolved type at left and right sub-expression" << std::endl;
    }

    expr.set_type(left->get_type());
    auto cast = adapt_type(right, left->get_type());
    if(!cast) {
        // TODO throw an exception
        // Error: right type is not compatible (cannot be cast).
        std::cerr << "Error: binary expression must have resolved type at left and right sub-expression" << std::endl;
    } else if(cast != right ) {
        // Casted, assign casted expression instead of right source.
        expr.assign_right(cast);
    } else {
        // Compatible type, no need to cast.
    }
}

void symbol_type_resolver::visit_function_invocation_expression(function_invocation_expression &expr) {
    auto callee = std::dynamic_pointer_cast<symbol_expression>(expr.callee_expr());
    if(!callee) {
        // TODO Support only variable expr as function name for now.
        std::cerr << "Error : only support symbol expr as function name for now" << std::endl;
    }

    for(auto& arg : expr.arguments()) {
        visit_expression(*arg);
    }

    if(auto stmt = callee->find_statement()) {
        if(auto block = stmt->get_block()) {
            if(auto func = block->get_function()) {
                if(auto ns = func->parent_ns()) {
                    if(auto function = ns->lookup_function(callee->get_name())) {
                        // TODO support overloading
                        // TODO enforce prototype matching
                        // Function prototype and expression type are set at resolution
                        callee->resolve(function);
                        expr.set_type(function->return_type());
                    }
                }
            }
        }
    }

    if(!callee->is_resolved() || !callee->is_function()) {
        // Error: cannot resolve function.
        // TODO throw an exception
        std::cerr << "Cannot resolve function '" << callee->get_name().to_string() << "'" << std::endl;
    }

    auto params = callee->get_function()->parameters();
    if(expr.arguments().size() != params.size()) {
        // Error : callee and function have not the same argument count.
        // TODO throw an exception
        std::cerr << "Error : callee and function '" << callee->get_name().to_string() << "' have not the same argument count" << std::endl;
    }

    for(size_t n=0; n<expr.arguments().size(); ++n) {
        auto arg = expr.arguments().at(n);
        auto param = callee->get_function()->parameters().at(n);

        if(!param->get_type() || !param->get_type()->is_resolved() ||
           !arg->get_type() || !arg->get_type()->is_resolved()) {
            // TODO throw an exception
            // Error: function argument or function invocation parameter have undefined type
            std::cerr << "Error: function invocation must have defined types" << std::endl;
        }

        auto cast = adapt_type(arg, param->get_type());
        if(!cast) {
            // TODO throw an exception
            // Error: function parameter is not compatible (cannot be cast).
            std::cerr << "Error: function argument must be compatible to parameter" << std::endl;
        } else if(cast != arg ) {
            // Casted, assign casted expression instead of right source.
            expr.assign_argument(n, cast);
        } else {
            // Compatible type, no need to cast.
        }
    }
}

void symbol_type_resolver::visit_cast_expression(cast_expression& expr) {
    visit_expression(*expr.expr());

    // TODO check if cast is possible (expr.expr().get_type() && expr.get_cast_type() compatibility)

    expr.set_type(expr.get_cast_type());
}

std::shared_ptr<expression> symbol_type_resolver::adapt_type(const std::shared_ptr<expression>& expr, const std::shared_ptr<type>& type) {
    if(!expr || !type || !type->is_resolved() || !expr->get_type() || !expr->get_type()->is_resolved()) {
        // Arguments must not be null, expr must have a type and types (expr and target) must be resolved.
        return nullptr;
    }

    auto prim_src = std::dynamic_pointer_cast<primitive_type>(expr->get_type());
    auto prim_tgt = std::dynamic_pointer_cast<primitive_type>(type);

    if(!prim_src || !prim_tgt) {
        // Support only primitive types for now.
        // TODO support not-primitive type casting
        return {};
    }

    if(prim_src==prim_tgt) {
        // Trivially agree for same types
        return expr;
    }

    if(prim_src->is_signed() && prim_tgt->is_signed() && !prim_src->is_float() && !prim_tgt->is_float()) {
        // NOTE only signed integer types are supported
        if (prim_src->type_size() != prim_tgt->type_size()) {
            // TODO inject intermediate casting expr
            if (prim_src->type_size() > prim_tgt->type_size()) {
                // Just warn for casting
                // TODO Replace by a warning log with more context
                std::cerr << "Warning: integer implicit downcast may loose data." << std::endl;
            }
            auto cast = cast_expression::make_shared(expr, prim_tgt);
            cast->set_type(prim_tgt);
            return cast;
        } else {
            // Types are the same, shall not happen
            return expr;
        }
    } else {
        // TODO support unsigned integers and float types
        return nullptr;
    }

}


} // k::unit
