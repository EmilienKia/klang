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

#ifndef LIBKDI_JSON_HPP
#define LIBKDI_JSON_HPP

/**
 * @file kdi_json.hpp
 *
 * KDI JSON serialisation / deserialisation.
 *
 * File format: a single UTF-8 JSON object.
 * Extension: .kdi.json
 *
 * The JSON encoding is a human-readable equivalent of the CBOR (.kdi) format
 * and uses the same schema version fields (schema_major / schema_minor).
 *
 * kdi_write_json() — serialise a kdi_file to a JSON stream.
 * kdi_read_json()  — deserialise a kdi_file from a JSON stream.
 *
 * On error, kdi_read_json() throws kdi_json_error.
 *
 * Convenience file helpers:
 *   kdi_write_json_file(file, path)   →  bool
 *   kdi_read_json_file(path)          →  kdi_file   (throws on error)
 */

#include "kdi_file.hpp"

#include <istream>
#include <ostream>
#include <stdexcept>
#include <string>

namespace kdi {

/** Exception thrown by kdi_read_json() when the input is malformed. */
struct kdi_json_error : std::runtime_error {
    explicit kdi_json_error(const std::string& what) : std::runtime_error(what) {}
};

/**
 * Write @p file as JSON to @p out (pretty-printed, 2-space indent).
 * @throws std::runtime_error on I/O failure.
 */
void kdi_write_json(const kdi_file& file, std::ostream& out);

/**
 * Read a JSON-encoded kdi_file from @p in.
 * @throws kdi_json_error on schema or format errors.
 * @throws std::runtime_error on I/O failure.
 */
kdi_file kdi_read_json(std::istream& in);

/**
 * Write @p file as JSON to the file at @p path.
 * @returns true on success, false on I/O failure.
 */
bool kdi_write_json_file(const kdi_file& file, const std::string& path);

/**
 * Read a JSON-encoded kdi_file from the file at @p path.
 * @throws kdi_json_error on schema or parse errors.
 * @throws std::runtime_error on I/O failure.
 */
kdi_file kdi_read_json_file(const std::string& path);

} // namespace kdi

#endif // LIBKDI_JSON_HPP

