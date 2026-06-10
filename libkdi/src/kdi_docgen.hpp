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

#ifndef LIBKDI_DOCGEN_HPP
#define LIBKDI_DOCGEN_HPP

/**
 * @file kdi_docgen.hpp
 *
 * Documentation generators for KDI files.
 * Supports Markdown and static HTML output formats.
 */

#include "kdi_file.hpp"

#include <string>

namespace kdi {

/**
 * Generate a Markdown documentation tree from a parsed KDI model.
 *
 * Output layout:
 * - <destination_dir>/<module_name>/index.md
 * - namespace sub-directories with index.md
 * - one .md file per type
 * - name-references.md and typed-references.md at module root
 *
 * @param file KDI in-memory model.
 * @param destination_dir Destination root directory (created if needed).
 * @param error_message Optional detailed error message when generation fails.
 * @return true on success, false on error.
 */
bool kdi_generate_markdown_doc(const kdi_file& file,
                               const std::string& destination_dir,
                               std::string* error_message = nullptr);

/**
 * Generate a static HTML documentation tree from a parsed KDI model.
 *
 * Produces a self-contained, modern-styled documentation site:
 * - <destination_dir>/<module_name>/index.html
 * - <destination_dir>/<module_name>/kdoc.css  (shared stylesheet)
 * - namespace sub-directories with index.html
 * - one .html file per type
 * - name-references.html and typed-references.html at module root
 *
 * @param file KDI in-memory model.
 * @param destination_dir Destination root directory (created if needed).
 * @param error_message Optional detailed error message when generation fails.
 * @return true on success, false on error.
 */
bool kdi_generate_html_doc(const kdi_file& file,
                           const std::string& destination_dir,
                           std::string* error_message = nullptr);

} // namespace kdi

#endif // LIBKDI_DOCGEN_HPP

