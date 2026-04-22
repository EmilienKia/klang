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
#ifndef KLANG_ERRORS_HPP
#define KLANG_ERRORS_HPP
// Umbrella header — includes all diagnostic code sub-headers in dependency order.
// Prefer including the specific sub-header when only one domain is needed:
//   errors_lex_parse.hpp — compiler_diag, lexer_diag, parser_diag
//   errors_model.hpp     — model_diag, symbol_diag, structure_diag, function_diag, type_diag
//   errors_gen.hpp       — operator_diag, variable_diag, statement_diag, codegen_diag, template_diag
#include "errors_gen.hpp"
#endif // KLANG_ERRORS_HPP
