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

#ifndef KLANG_RESOLVERS_MATERIALIZER_HPP
#define KLANG_RESOLVERS_MATERIALIZER_HPP

#include "resolvers_common.hpp"

namespace k::model::gen {

/**
 * Model materializer — Phase 2
 *
 * Synthesizes and materializes all internal model information needed for virtual
 * dispatch and constructor/destructor variant generation.  Runs AFTER
 * aggregate_type_resolver (Phase 1.a) and BEFORE type_reference_resolver (Phase 1.b).
 *
 * Responsibilities:
 *  1. Validate vtable consistency: every non-abstract class must have all inherited
 *     abstract slots concretely implemented.
 *  2. Compute secondary-vtable thunk descriptors: for each non-primary base class
 *     with a vtable embedded at a non-zero byte offset in a derived class, compute
 *     the this-adjustment offset and build thunk_info records stored in
 *     vtable_layout::secondary_vtables.  No LLVM types are involved — only byte offsets
 *     computed from the LLVM DataLayout.
 *  3. Validate abstract function specifiers on interfaces and classes.
 *
 * All output is pure model data (no llvm::*** in model elements).
 * The generators (declaration_generator / implementation_generator) read these
 * pre-computed descriptors instead of recomputing them on the fly.
 *
 * Must be run AFTER aggregate_type_resolver and BEFORE type_reference_resolver.
 */
class model_materializer : public default_model_visitor, protected k::log::logger_relay {
protected:
    std::shared_ptr<context> _context;
    unit& _unit;

public:
    model_materializer(k::log::logger& logger, std::shared_ptr<context> context, unit& unit)
        : k::log::logger_relay(logger),
          _context(context),
          _unit(unit) {}

    void materialize();

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

    /**
     * Validate vtable consistency for a class:
     * - All inherited abstract slots must be concretely implemented in non-abstract classes.
     * - All abstract methods in an abstract class have a vtable slot.
     * Returns false and emits an error if any inconsistency is found.
     */
    bool validate_vtable(klass& klass);

    /**
     * Compute secondary vtable thunk descriptors for a class with multiple class bases.
     * Populates vtable_layout::secondary_vtables with thunk_info records.
     * The byte offsets are computed from the LLVM StructLayout (requires that
     * context::init_module has NOT yet run; we use DataLayout from the target machine).
     * If no target machine is available, offsets are estimated from field indices.
     */
    void compute_secondary_vtable_specs(klass& klass);
};

} // namespace k::model::gen

#endif //KLANG_RESOLVERS_MATERIALIZER_HPP

