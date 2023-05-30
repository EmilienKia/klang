//
// Created by Emilien Kia <emilien.kia+dev@gmail.com>.// Created by Emilien Kia <emilien.kia+dev@gmail.com>.
//

#ifndef KLANG_UNIT_VAR_RESOLVER_HPP
#define KLANG_UNIT_VAR_RESOLVER_HPP

#include "unit.hpp"

namespace k::unit::resolvers {

/**
 * Unit variable resolver
 * This helper class will resolve variable usage to their definitions.
 */
class variable_resolver {
protected:
    unit& _unit;

    std::vector<std::string> _naming_context;

public:

    variable_resolver(unit& unit) : _unit(unit) {
    }

    void resolve();

protected:
    void visit_unit(unit&);

    void visit_ns_element(ns_element&);
    void visit_namespace(ns&);
    void visit_global_variable_definition(global_variable_definition&);
    void visit_function(function&);

    void visit_statement(statement&);
    void visit_block(block&);
    void visit_return_statement(return_statement&);
    void visit_expression_statement(expression_statement&);
    void visit_variable_statement(variable_statement&);

    void visit_expression(expression&);
    void visit_value_expression(value_expression&);
    void visit_variable_expression(variable_expression&);
    void visit_binary_expression(binary_expression&);


};


} // k::unit::resolvers

#endif //KLANG_UNIT_VAR_RESOLVER_HPP
