/*
 * K Language compiler
 *
 * Copyright 2023 Emilien Kia
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

#ifndef KLANG_SYMBOL_TYPE_RESOLVER_HPP
#define KLANG_SYMBOL_TYPE_RESOLVER_HPP

#include "../unit/unit.hpp"

#include "../common/logger.hpp"
#include "../lex/lexer.hpp"

namespace k::unit {


class resolution_error : public std::runtime_error {
public:
    resolution_error(const std::string &arg);
    resolution_error(const char *string);
};



/**
 * Unit symbol resolver
 * This helper class will resolve method and variable usages to their definitions.
 */
class symbol_type_resolver : public default_element_visitor, protected k::lex::lexeme_logger {
protected:
    unit& _unit;

    std::vector<std::string> _naming_context;

public:

    symbol_type_resolver(k::log::logger& logger, unit& unit) :
    lexeme_logger(logger, 0x30000),
    _unit(unit)  {
    }

    void resolve();

protected:

    [[noreturn]] void throw_error(unsigned int code, const lex::lexeme& lexeme, const std::string& message, const std::vector<std::string>& args = {}) {
        error(code, lexeme, message, args);
        throw resolution_error(message);
    }

    [[noreturn]] void throw_error(unsigned int code, const lex::opt_ref_any_lexeme& lexeme, const std::string& message, const std::vector<std::string>& args = {}) {
        error(code, lexeme, message, args);
        throw resolution_error(message);
    }


    void visit_unit(unit&) override;

    void visit_namespace(ns&) override;
    void visit_global_variable_definition(global_variable_definition&) override;
    void visit_function(function&) override;

    void visit_block(block&) override;
    void visit_return_statement(return_statement&) override;
    void visit_if_else_statement(if_else_statement&) override;
    void visit_while_statement(while_statement&) override;
    void visit_for_statement(for_statement&) override;
    void visit_expression_statement(expression_statement&) override;
    void visit_variable_statement(variable_statement&) override;

    void visit_value_expression(value_expression&) override;
    void visit_symbol_expression(symbol_expression&) override;
    void visit_unary_expression(unary_expression&) override;
    void visit_binary_expression(binary_expression&) override;

    void process_arithmetic(binary_expression&);

    void visit_arithmetic_binary_expression(arithmetic_binary_expression &expression) override;
    void visit_assignation_expression(assignation_expression &expression) override;

    void visit_arithmetic_unary_expression(arithmetic_unary_expression&) override;

    void visit_logical_binary_expression(logical_binary_expression&) override;
    void visit_logical_not_expression(logical_not_expression&) override;

    void visit_comparison_expression(comparison_expression&) override;

protected:

    void visit_function_invocation_expression(function_invocation_expression &) override;

    void visit_cast_expression(cast_expression&)override;

    /**
     * Adapt an expression to ensure iit maps to a given type, by casting it.
     * @param expr Expression to map.
     * @param type Type to target
     * @return The given arg expression if already compatible, the new wrapping casting expr if mapping, nullptr if not possible.
     */
    std::shared_ptr<expression> adapt_type(const std::shared_ptr<expression>& expr, const std::shared_ptr<type>& type);
};


} // k::unit

#endif //KLANG_SYMBOL_TYPE_RESOLVER_HPP
