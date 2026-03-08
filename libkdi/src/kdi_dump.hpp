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

#ifndef LIBKDI_DUMP_HPP
#define LIBKDI_DUMP_HPP

/**
 * @file kdi_dump.hpp
 *
 * Human-readable text dump of a kdi_file.
 * Used by the `kdi dump` command.
 */

#include "kdi_file.hpp"
#include <ostream>

namespace kdi {

/**
 * Write a human-readable representation of @p file to @p out.
 * The format is a structured text (indented, similar to a language header file).
 */
void kdi_dump(const kdi_file& file, std::ostream& out);

} // namespace kdi

#endif // LIBKDI_DUMP_HPP

