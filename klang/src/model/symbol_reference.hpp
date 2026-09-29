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

#ifndef KLANG_MODEL_SYMBOL_REFERENCE_HPP
#define KLANG_MODEL_SYMBOL_REFERENCE_HPP

#include <memory>
#include <string>

namespace k::model {

class named_element;

/**
 * Interface implemented by model elements that reference a named declaration.
 * Enables two-way tracking between declarations and their usages.
 */
class symbol_reference {
public:
    virtual ~symbol_reference() = default;

    /** Returns the target declaration referenced by this element, or nullptr if unresolved. */
    virtual std::shared_ptr<named_element> get_target_declaration() const = 0;

    /** Updates the referenced name when the target declaration is renamed. */
    virtual void update_referenced_name(const std::string& new_short_name) = 0;
};

} // namespace k::model

#endif // KLANG_MODEL_SYMBOL_REFERENCE_HPP
