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
#ifndef KLANG_KDI_TYPE_CONVERTER_HPP
#define KLANG_KDI_TYPE_CONVERTER_HPP

/**
 * @file kdi_type_converter.hpp
 *
 * Convert a kdi::kdi_type (KDI type descriptor) into the corresponding
 * k::model::type (K model type).
 *
 * The converter is a free function that is intentionally kept stateless so
 * that it can be called during any phase of compilation.  The two parameters
 * give it the necessary context:
 *
 *   - @p ctx   : the compilation context, used to resolve primitive type names
 *                and to look up already-known struct_type entries.
 *   - @p owner : the unit being compiled, used to resolve kdi_aggregate_ref
 *                names through unit::get_or_create_imported_aggregate().
 *
 * Returns nullptr for any unrecognised or currently unsupported kdi_type.
 */

#include "../type.hpp"

#include <kdi.hpp>

#include <memory>

namespace k::model {
class unit;
class context;
} // namespace k::model

namespace k::model {

/**
 * Convert @p kdi_t into the matching k::model::type.
 *
 * @param kdi_t   KDI type to convert.
 * @param owner   Compilation unit (needed for aggregate-ref resolution).
 * @param ctx     Compilation context (needed for primitive lookup and
 *                struct_type registration).
 * @return        Resolved k::model::type, or nullptr if conversion is not
 *                possible (e.g. unsupported form, forward reference that
 *                could not be resolved yet).
 */
std::shared_ptr<type>
kdi_type_to_model_type(const kdi::kdi_type& kdi_t,
                       unit& owner,
                       std::shared_ptr<context> ctx);

} // namespace k::model

#endif // KLANG_KDI_TYPE_CONVERTER_HPP

