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

#ifndef KLANG_PLUGIN_NAMING_PASS_HPP
#define KLANG_PLUGIN_NAMING_PASS_HPP

#include "../plugin.hpp"

namespace k::plugin::naming {

/**
 * Model visitor pass that inspects declarations defined in the current module
 * and enforces naming conventions (lower_case namespaces, PascalCase aggregates,
 * SCREAMING_SNAKE_CASE constants, camelCase functions and mutable variables)
 * by calling change_name() on non-conforming declarations.
 */
class naming_pass : public model_pass {
public:
    std::string_view name() const override {
        return "naming_conventions";
    }

    void visit_namespace(model::ns& ns) override;
    void visit_aggregate(model::aggregate& agg) override;
    void visit_structure(model::structure& st) override;
    void visit_klass(model::klass& kl) override;
    void visit_enumeration(model::enumeration& en) override;
    void visit_function(model::function& fn) override;
    void visit_global_variable_definition(model::global_variable_definition& var) override;
    void visit_member_variable_definition(model::member_variable_definition& var) override;

    void visit_block(model::block& blk) override;
    void visit_expression_statement(model::expression_statement& stmt) override;
    void visit_return_statement(model::return_statement& stmt) override;
    void visit_if_else_statement(model::if_else_statement& stmt) override;
    void visit_while_statement(model::while_statement& stmt) override;
    void visit_for_statement(model::for_statement& stmt) override;
    void visit_binary_expression(model::binary_expression& expr) override;
    void visit_unary_expression(model::unary_expression& expr) override;
    void visit_function_invocation_expression(model::function_invocation_expression& expr) override;
    void visit_member_of_expression(model::member_of_expression& expr) override;

private:
    std::map<std::string, std::map<std::string, std::string>> _member_renames_by_name;

    bool is_local(const model::element& elem) const;
    void check_and_rename(model::named_element& decl, const std::string& target_name);
    void check_and_rename_member(model::aggregate& agg, model::named_element& member, const std::string& target_name);
};

} // namespace k::plugin::naming

#endif // KLANG_PLUGIN_NAMING_PASS_HPP
