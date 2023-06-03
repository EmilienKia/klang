//
// Created by Emilien Kia <emilien.kia+dev@gmail.com>.
//

#include "unit_var_resolver.hpp"

namespace k::unit::resolvers {

void variable_resolver::resolve()
{
    visit_unit(_unit);
}

void variable_resolver::visit_unit(unit& unit)
{
    visit_namespace(*_unit.get_root_namespace());
}

void variable_resolver::visit_ns_element(ns_element& elem)
{
    if(auto ns = dynamic_cast<k::unit::ns*>(&elem)) {
        visit_namespace(*ns);
    } else if(auto var = dynamic_cast<k::unit::global_variable_definition*>(&elem)) {
        visit_global_variable_definition(*var);
    } else if(auto fn = dynamic_cast<k::unit::function*>(&elem)) {
        visit_function(*fn);
    } // else // Must not occur
}

void variable_resolver::visit_namespace(ns& ns)
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
void variable_resolver::visit_global_variable_definition(global_variable_definition& var)
{
    // TODO visit parameter definition (just in case default init is referencing a variable).

}

void variable_resolver::visit_function(function& fn)
{
    _naming_context.push_back(fn.name());

    // TODO visit parameter definition (just in case default init is referencing a variable).

    if(auto block = fn.get_block()) {
        visit_block(*block);
    }

    _naming_context.pop_back();
}

void variable_resolver::visit_statement(statement& stmt)
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

void variable_resolver::visit_block(block& block)
{
    for(auto& stmt : block.get_statements()) {
        visit_statement(*stmt);
    }
}

void variable_resolver::visit_return_statement(return_statement& stmt)
{
    if(auto expr = stmt.get_expression()) {
        visit_expression(*expr);
    }
}

void variable_resolver::visit_expression_statement(expression_statement& stmt)
{
    if(auto expr = stmt.get_expression()) {
        visit_expression(*expr);
    }
}

void variable_resolver::visit_variable_statement(variable_statement& var)
{
    if(auto expr = var.get_init_expr()) {
        visit_expression(*expr);
    }
}

void variable_resolver::visit_expression(expression& expr)
{
    if(auto val = dynamic_cast<k::unit::value_expression*>(&expr)) {
        visit_value_expression(*val);
    } else if(auto var = dynamic_cast<k::unit::variable_expression*>(&expr)) {
        visit_variable_expression(*var);
    } else if(auto bin = dynamic_cast<k::unit::binary_expression*>(&expr)) {
        visit_binary_expression(*bin);
    } // else // Must not occur
}

void variable_resolver::visit_value_expression(value_expression&)
{
    // Nothing to view here
}

void variable_resolver::visit_variable_expression(variable_expression& var)
{
    // TODO: Have to check the variable definition (for block-variable) is done before the expression;
    if(!var.is_resolved()) {
        if(auto stmt = var.find_statement()) {
            if(auto block = stmt->get_block()) {
                if(auto def = block->lookup_variable(var.get_var_name())) {
                    var.resolve(def);
                }
            }
        }
    }
}

void variable_resolver::visit_binary_expression(binary_expression& expr)
{
    if(auto& left = expr.left()) {
        visit_expression(*left);
    }
    if(auto& right = expr.right()) {
        visit_expression(*right);
    }
}



} // k::unit::resolvers
