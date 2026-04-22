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
// This file has been split into focused implementation files:
//   gen_operators_helpers.hpp    — anonymous-namespace helper functions (type encoding, operator names)
//   gen_operators_overload.cpp   — resolve/generate binary, unary, cast operator overloads
//   gen_operators_arithmetic.cpp — arithmetic binary expression visitors
//   gen_operators_assign.cpp     — assignment and compound-assignment expression visitors
//   gen_operators_unary.cpp      — arithmetic unary, prefix/postfix inc/dec + bitwise_not visitors
//   gen_operators_logical.cpp    — logical binary/unary + comparison expression visitors
