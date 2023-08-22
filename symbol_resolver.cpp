//
// Created by Emilien Kia <emilien.kia+dev@gmail.com>.
//

#include "symbol_resolver.hpp"

namespace k::unit {

void symbol_resolver::resolve()
{
    visit_unit(_unit);
}

void symbol_resolver::visit_unit(unit& unit)
{
    visit_namespace(*_unit.get_root_namespace());
}

void symbol_resolver::visit_ns_element(ns_element& elem)
{
    if(auto ns = dynamic_cast<k::unit::ns*>(&elem)) {
        visit_namespace(*ns);
    } else if(auto var = dynamic_cast<k::unit::global_variable_definition*>(&elem)) {
        visit_global_variable_definition(*var);
    } else if(auto fn = dynamic_cast<k::unit::function*>(&elem)) {
        visit_function(*fn);
    } // else // Must not occur
}

void symbol_resolver::visit_namespace(ns& ns)
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
void symbol_resolver::visit_global_variable_definition(global_variable_definition& var)
{
    // TODO visit parameter definition (just in case default init is referencing a variable).

}

void symbol_resolver::visit_function(function& fn)
{
    _naming_context.push_back(fn.name());

    // TODO visit parameter definition (just in case default init is referencing a variable).

    if(auto block = fn.get_block()) {
        visit_block(*block);
    }

    _naming_context.pop_back();
}

void symbol_resolver::visit_statement(statement& stmt)
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

void symbol_resolver::visit_block(block& block)
{
    for(auto& stmt : block.get_statements()) {
        visit_statement(*stmt);
    }
}

void symbol_resolver::visit_return_statement(return_statement& stmt)
{
    if(auto expr = stmt.get_expression()) {
        visit_expression(*expr);
    }
}

void symbol_resolver::visit_expression_statement(expression_statement& stmt)
{
    if(auto expr = stmt.get_expression()) {
        visit_expression(*expr);
    }
}

void symbol_resolver::visit_variable_statement(variable_statement& var)
{
    if(auto expr = var.get_init_expr()) {
        visit_expression(*expr);
    }
}

void symbol_resolver::visit_expression(expression& expr)
{
    if(auto val = dynamic_cast<k::unit::value_expression*>(&expr)) {
        visit_value_expression(*val);
    } else if(auto var = dynamic_cast<k::unit::symbol_expression*>(&expr)) {
        visit_symbol_expression(*var);
    } else if(auto func = dynamic_cast<k::unit::function_invocation_expression*>(&expr)) {
        visit_function_invocation_expression(*func);
    } else if(auto bin = dynamic_cast<k::unit::binary_expression*>(&expr)) {
        visit_binary_expression(*bin);
    } // else // Must not occur
}

void symbol_resolver::visit_value_expression(value_expression&)
{
    // Nothing to view here
}

void symbol_resolver::visit_symbol_expression(symbol_expression& symbol)
{
    // TODO: Have to check the variable definition (for block-variable) is done before the expression;
    // TODO: Have to sypport all symbol types, not only variables
    if(!symbol.is_resolved()) {
        if(auto stmt = symbol.find_statement()) {
            if(auto block = stmt->get_block()) {
                if(auto def = block->lookup_variable(symbol.get_name())) {
                    symbol.resolve(def);
                }
            }
        }
    }
}

void symbol_resolver::visit_binary_expression(binary_expression& expr)
{
    if(auto& left = expr.left()) {
        visit_expression(*left);
    }
    if(auto& right = expr.right()) {
        visit_expression(*right);
    }
}

void symbol_resolver::visit_function_invocation_expression(function_invocation_expression &expr) {
    auto callee = std::dynamic_pointer_cast<symbol_expression>(expr.callee_expr());
    if(!callee) {
        // TODO Support only variable expr as function name for now.
        std::cerr << "Error : only support symbol expr as function name for now" << std::endl;
    }

    if(auto stmt = callee->find_statement()) {
        if(auto block = stmt->get_block()) {
            if(auto func = block->get_function()) {
                if(auto ns = func->parent_ns()) {
                    if(auto function = ns->lookup_function(callee->get_name())) {
                        callee->resolve(function);
                    }
                }
            }
        }
    }

    if(!callee->is_resolved()) {
        // Error: cannot resolve function.
        // TODO throw an exception
        std::cerr << "Cannot resolve function '" << callee->get_name().to_string() << "'" << std::endl;
    }

    for(auto& arg : expr.arguments()) {
        visit_expression(*arg);
    }
}


} // k::unit
