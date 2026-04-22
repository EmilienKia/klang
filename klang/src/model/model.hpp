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
#ifndef KLANG_MODEL_HPP
#define KLANG_MODEL_HPP
// Umbrella header — includes all model sub-headers in dependency order.
// Prefer including the specific sub-header when only part of the model is needed:
//   model_fwd.hpp       — includes, forward declarations, visibility enum, vtable structs
//   model_element.hpp   — element, named_element, variable_definition, holder mixins
//   model_enum.hpp      — enum_entry_def, enum_raw_entry_def, enumeration
//   model_aggregate.hpp — member_variable_definition, base_spec, aggregate hierarchy
//   model_function.hpp  — parameter, function, constructors/destructors, global functions
//   model_ns.hpp        — global_variable_definition, ns, unit
#include "model_ns.hpp"
#endif //KLANG_MODEL_HPP
