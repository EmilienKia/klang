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
// This file is intentionally empty.
// Resolver implementations have been split into:
//   resolvers_scope_lookup.cpp  — scope_lookup + resolve_using_target
//   resolvers_symbol.cpp        — symbol_resolver
//   resolvers_aggregate.cpp     — aggregate_type_resolver
//   resolvers_materializer.cpp  — model_materializer
//   resolvers_type_ref.cpp      — type_reference_resolver + signature_resolver (bodies)
//   resolvers_init_order.cpp    — init_order_resolver
// The visitor method bodies for symbol_resolver and signature_resolver are
// co-located with the generation code in gen_unit.cpp, gen_struct.cpp, etc.
