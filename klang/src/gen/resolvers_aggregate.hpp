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

#ifndef KLANG_RESOLVERS_AGGREGATE_HPP
#define KLANG_RESOLVERS_AGGREGATE_HPP

#include "resolvers_common.hpp"

namespace k::model::gen {

/**
 * Aggregate type resolver — Phase 1.a
 *
 * Resolves the types of all unit-level declarations: aggregates (structs, classes,
 * interfaces), functions (signatures only — no bodies), constructors, destructors,
 * and their parameters and member variables.
 *
 * It does NOT visit blocks, expressions, or statements. Its sole purpose is to
 * guarantee that, when type_reference_resolver runs, every aggregate and function
 * signature type is already resolved — including the LLVM vtable struct types for
 * polymorphic classes.
 *
 * Must be run AFTER symbol_resolver and BEFORE type_reference_resolver.
 */
class aggregate_type_resolver : public default_model_visitor, protected k::log::logger_relay {
protected:
    std::shared_ptr<context> _context;
    unit& _unit;

public:
    aggregate_type_resolver(k::log::logger& logger, std::shared_ptr<context> context, unit& unit)
        : k::log::logger_relay(logger),
          _context(context),
          _unit(unit) {}

    void resolve();

    // Type resolution helpers (public so they can be used by free helpers)
    std::shared_ptr<type> resolve_type_by_name(const k::name& type_name, const element& context_elem);
    static std::shared_ptr<aggregate> resolve_struct_from(const element& elem, const k::name& qualified_name);
    std::shared_ptr<type> resolve_type_from_root(const k::name& name_without_prefix);

    /**
     * Try to resolve an unresolved_type that carries AST template arguments
     * (e.g. Box<int>) by finding the template definition, converting the AST
     * args to model template_argument values, and triggering instantiation.
     *
     * Returns the struct_type of the concrete instantiated aggregate, or
     * nullptr if the base name is not a known template.
     */
    std::shared_ptr<type> try_instantiate_template_type(
        const std::shared_ptr<unresolved_type>& unres,
        const element& context_elem);

    /**
     * Best-effort early resolution of an unresolved callable type (`*(int):int`,
     * `Counter::&(int)`, …) during phase 1.a, using @p scope for name lookup.
     *
     * @return The resolved callable_type, or nullptr when some component of the
     *         signature cannot be resolved yet.
     */
    std::shared_ptr<type> resolve_unresolved_callable_type(
        const std::shared_ptr<unresolved_callable_type>& ufrt,
        const element& scope);

protected:

    [[noreturn]] void throw_error(unsigned int code, const lex::opt_any_lexeme& lexeme,
                                  const std::string& message, const std::vector<std::string>& args = {}) {
        auto diag = k::log::diagnostic::make_error(code, message, args);
        if (lexeme) diag.at(*lexeme);
        logger_relay::report(diag);
        throw resolution_error(std::move(diag));
    }


    void visit_unit(unit&) override;
    void visit_namespace(ns&) override;
    void visit_aggregate(aggregate&) override;
    void visit_klass(klass&) override;
    void visit_interface(interface&) override;
    void visit_annotation_type(annotation_type&) override;
    void visit_union(union_type_def&) override;
    void visit_member_variable_definition(member_variable_definition&) override;
    void visit_global_variable_definition(global_variable_definition&) override;
    void visit_parameter(parameter&) override;
    void visit_function(function&) override;
    void visit_constructor(constructor&) override;
    void visit_destructor(destructor&) override;
    void visit_static_constructor(static_constructor&) override;
    void visit_static_destructor(static_destructor&) override;
    void visit_global_constructor_function(global_constructor_function&) override;
    void visit_global_destructor_function(global_destructor_function&) override;
    void visit_global_main_function(global_main_function&) override;
};

} // namespace k::model::gen

#endif //KLANG_RESOLVERS_AGGREGATE_HPP

