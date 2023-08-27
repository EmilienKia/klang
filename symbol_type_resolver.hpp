//
// Created by Emilien Kia <emilien.kia+dev@gmail.com>.// Created by Emilien Kia <emilien.kia+dev@gmail.com>.
//

#ifndef KLANG_SYMBOL_TYPE_RESOLVER_HPP
#define KLANG_SYMBOL_TYPE_RESOLVER_HPP

#include "unit.hpp"

namespace k::unit {

/**
 * Unit symbol resolver
 * This helper class will resolve method and variable usages to their definitions.
 */
class symbol_type_resolver : public default_element_visitor {
protected:
    unit& _unit;

    std::vector<std::string> _naming_context;

public:

    symbol_type_resolver(unit& unit) : _unit(unit) {
    }

    void resolve();

protected:
    void visit_unit(unit&) override;

    void visit_ns_element(ns_element&) override;
    void visit_namespace(ns&) override;
    void visit_global_variable_definition(global_variable_definition&) override;
    void visit_function(function&) override;

    void visit_statement(statement&) override;
    void visit_block(block&) override;
    void visit_return_statement(return_statement&) override;
    void visit_expression_statement(expression_statement&) override;
    void visit_variable_statement(variable_statement&) override;

    void visit_expression(expression&) override;
    void visit_value_expression(value_expression&) override;
    void visit_symbol_expression(symbol_expression&) override;
    void visit_binary_expression(binary_expression&) override;

    void visit_function_invocation_expression(function_invocation_expression &) override;

};


} // k::unit

#endif //KLANG_SYMBOL_TYPE_RESOLVER_HPP
