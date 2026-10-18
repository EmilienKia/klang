/*
 * K Language compiler — libkdi
 *
 * Copyright 2026 Emilien Kia
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

#ifndef LIBKDI_DOC_MARKDOWN_HPP
#define LIBKDI_DOC_MARKDOWN_HPP

#include <cstddef>
#include <string>

// Private documentation-fragment adapter; not part of the KDI data model.
namespace kdi::doc_markdown {

enum class format { markdown, html };
enum class inline_context { normal, table };

// Brief blocks are flattened in document order, with spaces between blocks.
// Truncation counts visible UTF-8 characters and keeps inline markup balanced.
std::string inline_fragment(const std::string& source, format output,
                            inline_context context = inline_context::normal,
                            size_t max_length = 0);

// Heading levels are relative to the containing section, capped at h6.
std::string block_fragment(const std::string& source, format output,
                           int containing_heading = 3);

// Literal identifiers are never parsed as documentation.
std::string literal_code(const std::string& value);

} // namespace kdi::doc_markdown

#endif
