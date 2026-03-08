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

#ifndef LIBKDI_CBOR_HPP
#define LIBKDI_CBOR_HPP

/**
 * @file kdi_cbor.hpp
 *
 * KDI CBOR serialisation / deserialisation.
 *
 * File format: a single CBOR map item written to a binary stream.
 * Extension: .kdi
 *
 * kdi_write_cbor() — serialise a kdi_file to a byte stream.
 * kdi_read_cbor()  — deserialise a kdi_file from a byte stream.
 *
 * On error, kdi_read_cbor() throws kdi_parse_error.
 */

#include "kdi_file.hpp"

#include <istream>
#include <ostream>
#include <stdexcept>
#include <string>

namespace kdi {

/** Exception thrown by kdi_read_cbor() when the input is malformed. */
struct kdi_parse_error : std::runtime_error {
    explicit kdi_parse_error(const std::string& what) : std::runtime_error(what) {}
};

/**
 * Write @p file as CBOR to @p out.
 * @throws std::runtime_error on I/O failure.
 */
void kdi_write_cbor(const kdi_file& file, std::ostream& out);

/**
 * Read a CBOR-encoded kdi_file from @p in.
 * @throws kdi_parse_error on schema or format errors.
 * @throws std::runtime_error on I/O failure.
 */
kdi_file kdi_read_cbor(std::istream& in);

/**
 * Convenience: write to a file path.
 * @return true on success.
 */
bool kdi_write_cbor_file(const kdi_file& file, const std::string& path);

/**
 * Convenience: read from a file path.
 * @throws kdi_parse_error, std::runtime_error.
 */
kdi_file kdi_read_cbor_file(const std::string& path);

} // namespace kdi

#endif // LIBKDI_CBOR_HPP

