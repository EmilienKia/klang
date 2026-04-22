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

#ifndef KLANG_RESOLVERS_SIGNATURE_HPP
#define KLANG_RESOLVERS_SIGNATURE_HPP

#include "resolvers_common.hpp"

namespace k::model::gen {

/**
 * Signature resolver — pre-pass within type_reference_resolver
 *
 * Resolves function parameter and return types for all aggregates within a
 * namespace WITHOUT processing function bodies, expressions, or statements.
 *
 * This is used as a pre-pass inside type_reference_resolver::visit_namespace
 * to ensure that when function bodies reference types from sibling classes
 * (e.g. String's operator+ returning StringBuilder, where StringBuilder is
 * declared later), those constructor/function parameter types are already
 * resolved.
 *
 * Must be run AFTER symbol_resolver / aggregate_type_resolver / model_materializer
 * and BEFORE the full type_reference_resolver pass over function bodies.
 */
class signature_resolver : public default_model_visitor, protected k::log::logger_relay {
protected:
    std::shared_ptr<context> _context;
    unit& _unit;

public:
    signature_resolver(k::log::logger& logger, std::shared_ptr<context> context, unit& unit)
        : k::log::logger_relay(logger),
          _context(context),
          _unit(unit) {}

    /**
     * Pre-resolve all function signatures (parameter and return types) in the
     * aggregates of the given namespace.  Does NOT process function bodies.
     */
    void resolve_signatures(ns& ns);

protected:
    void visit_namespace(ns&) override;
    void visit_aggregate(aggregate&) override;
    void visit_klass(klass&) override;
    void visit_interface(interface&) override;
    void visit_function(function&) override;
    void visit_constructor(constructor&) override;
    void visit_destructor(destructor&) override;
    void visit_static_constructor(static_constructor&) override;
    void visit_static_destructor(static_destructor&) override;
    void visit_parameter(parameter&) override;
};

} // namespace k::model::gen

#endif //KLANG_RESOLVERS_SIGNATURE_HPP

