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

#ifndef LIBKDI_SYMBOLS_HPP
#define LIBKDI_SYMBOLS_HPP

/**
 * @file kdi_symbols.hpp
 *
 * KDI symbol cross-check: compare the mangled names declared in a kdi_file
 * against the symbols actually exported by the accompanying binary (.so or .a).
 *
 * kdi_collect_binary_symbols()
 *   Read the exported (defined, global) symbols from a binary file using `nm`.
 *   Returns a sorted set of mangled symbol names.
 *
 * kdi_check_symbols()
 *   Walk every mangled name declared in a kdi_file and verify that it exists
 *   in the provided symbol set.  Returns a kdi_symbol_check_result which
 *   lists every missing symbol.
 *
 * kdi_check_symbols(file, binary_path)
 *   Convenience overload: reads the binary symbols internally, then checks.
 *
 * On failure to invoke `nm`, kdi_collect_binary_symbols() throws
 * kdi_symbol_error.
 */

#include "kdi_file.hpp"

#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace kdi {

/** Exception thrown when the binary cannot be read (nm failure, missing file). */
struct kdi_symbol_error : std::runtime_error {
    explicit kdi_symbol_error(const std::string& w) : std::runtime_error(w) {}
};

// ─────────────────────────────────────────────────────────────────────────────
// Symbol check result
// ─────────────────────────────────────────────────────────────────────────────

struct kdi_missing_symbol {
    std::string mangled_name;   ///< The mangled symbol that was not found
    std::string context;        ///< Human-readable path (e.g. "ns::Foo::bar C1")
};

struct kdi_symbol_check_result {
    std::vector<kdi_missing_symbol> missing;

    bool is_ok()   const { return missing.empty(); }
    bool has_missing() const { return !missing.empty(); }
};

// ─────────────────────────────────────────────────────────────────────────────
// API
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Read the exported defined-global symbols from @p binary_path (.so or .a)
 * using `nm --defined-only --extern-only`.
 *
 * @returns A sorted set of mangled symbol names.
 * @throws  kdi_symbol_error if nm cannot be invoked or the file cannot be read.
 */
std::set<std::string> kdi_collect_binary_symbols(const std::string& binary_path);

/**
 * Compare all mangled names declared in @p file against @p binary_symbols.
 * Every mangled name that is absent from @p binary_symbols is reported as
 * missing.
 *
 * Symbols that are intentionally empty (abstract functions, compiler-generated
 * destructors marked is_compiler_generated) are silently skipped.
 */
kdi_symbol_check_result kdi_check_symbols(const kdi_file& file,
                                          const std::set<std::string>& binary_symbols);

/**
 * Convenience overload: read symbols from @p binary_path, then check.
 * @throws kdi_symbol_error on nm failure.
 */
kdi_symbol_check_result kdi_check_symbols(const kdi_file& file,
                                          const std::string& binary_path);

} // namespace kdi

#endif // LIBKDI_SYMBOLS_HPP

