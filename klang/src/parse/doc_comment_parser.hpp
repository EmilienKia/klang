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

#ifndef KLANG_PARSE_DOC_COMMENT_PARSER_HPP
#define KLANG_PARSE_DOC_COMMENT_PARSER_HPP

#include <optional>
#include <vector>

#include "../lex/lexemes.hpp"
#include "ast.hpp"

namespace k::parse {

/**
 * Parse a list of raw doc_comment lexemes into a fully structured
 * ast::documentation node.
 *
 * The function:
 *   1. Cleans the raw text of each lexeme (strips opening/closing markers,
 *      per-line '*'/'!' decoration, pure-decoration border lines).
 *   2. Concatenates the cleaned texts in source order.
 *   3. Splits the result into brief, description, and tagged sections
 *      (@param, @return/@returns, @throws/@throw/@exception,
 *       @tparam/@typeparam, and any other generic @tag).
 *
 * Returns std::nullopt when the resulting documentation is empty (all
 * fields would be empty after cleaning).
 *
 * @param raw_comments  Ordered list of raw doc_comment lexemes (forward
 *                      and/or backward) to merge and parse.
 */
std::optional<ast::documentation> parse_documentation(
    const std::vector<lex::doc_comment>& raw_comments);

} // namespace k::parse

#endif // KLANG_PARSE_DOC_COMMENT_PARSER_HPP

