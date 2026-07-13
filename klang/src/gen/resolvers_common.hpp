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

#ifndef KLANG_RESOLVERS_COMMON_HPP
#define KLANG_RESOLVERS_COMMON_HPP

#include <functional>
#include <limits>
#include <unordered_map>
#include <unordered_set>

#include "../model/model.hpp"
#include "../model/model_visitor.hpp"
#include "../model/statements.hpp"

#include "../model/context.hpp"

#include "../common/logger.hpp"
#include "../lex/lexer.hpp"

namespace k::model::gen {

/**
 * Helper: resolve the model element targeted by a using directive's target_name.
 * Returns the element (as ns or aggregate) or nullptr if not found.
 */
std::shared_ptr<const element>
resolve_using_target(const k::name& target_name, const unit& unit);

/**
 * Returns true if `arg_name` matches a template parameter name of the nearest
 * enclosing template aggregate/union of `context_elem`. Unlike
 * unresolved_type::is_template_param_placeholder() (which is only set at AST
 * parse time, while the pushed template-param scope is active), this check
 * works during later resolution passes (aggregate_type_resolver,
 * type_reference_resolver), which run long after that scope has been popped.
 * This is needed to avoid resolving a bare template-parameter identifier (e.g.
 * 'I'/'O' in `template<typename I, typename O> ... Vector<I> ...`) against an
 * unrelated same-named user type (e.g. `interface I { ... }`) when merely
 * scanning an un-instantiated template's own body (member variable types,
 * base classes, etc.).
 */
bool is_enclosing_template_param_name(const element& context_elem, const std::string& arg_name);

/**
 * Ensure that `kl` has a fully-built vtable, constructing one on demand if
 * missing.
 *
 * Template instantiations (classes/interfaces created by
 * template_instantiator::instantiate_aggregate) bypass symbol_resolver's
 * normal build_vtable_layout() pass entirely — they don't exist yet when
 * symbol_resolver walks the tree (Pass A), so their vtable must be built
 * lazily, on demand, the first time it's needed (e.g. when the instantiation
 * is used as a base of another instantiation, or when code generation needs
 * its vtable layout).
 *
 * Unlike a naive "flatten this class's own methods into a fresh vtable"
 * approach, this recursively ensures each of `kl`'s bases has its own vtable
 * built FIRST, so multi-level template interface hierarchies (e.g.
 * `MutableIndexedCollection<T> : IndexedCollection<T>, MutableCollection<T>`,
 * each itself deriving further from `Collection<T>`, `Sequence<T>`, etc.) get
 * the correct, fully-inherited slot list rather than only their own directly
 * declared methods.
 *
 * Also links `overrides` for methods that override a slot introduced by a
 * *secondary* (non-primary) base, transitively at any depth — mirroring the
 * fix applied to gen_class.cpp's build_vtable_layout() for the same class of
 * bug (a class implementing `interface C : A, B` must have its `B`-overriding
 * method's `overrides` chain reach `B`'s slot even though `B` is not `C`'s
 * primary base). Without this, compute_secondary_vtable_specs() cannot find
 * a concrete function for the secondary vtable slot and leaves it null,
 * causing a runtime segfault when dispatching through that secondary base.
 *
 * No-op if `kl` already has a vtable.
 */
void ensure_klass_vtable_built(klass& kl);


class resolution_error : public k::log::compiler_error {
public:
    explicit resolution_error(k::log::diagnostic diag)
        : k::log::compiler_error(std::move(diag)) {}
};

} // namespace k::model::gen

#endif //KLANG_RESOLVERS_COMMON_HPP

