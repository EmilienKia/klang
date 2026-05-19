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

#ifndef LIBKDI_FILE_HPP
#define LIBKDI_FILE_HPP

/**
 * @file kdi_file.hpp
 *
 * Top-level KDI file DTO: header, type table, namespace tree, and the root
 * unit that assembles everything.
 */

#include "kdi_aggregates.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace kdi {

// ─────────────────────────────────────────────────────────────────────────────
// Schema version constants
// ─────────────────────────────────────────────────────────────────────────────

inline constexpr uint32_t KDI_SCHEMA_MAJOR = 0;
inline constexpr uint32_t KDI_SCHEMA_MINOR = 1;

// ─────────────────────────────────────────────────────────────────────────────
// KdiHeader
// ─────────────────────────────────────────────────────────────────────────────

struct kdi_header {
    uint32_t    schema_major  = KDI_SCHEMA_MAJOR;
    uint32_t    schema_minor  = KDI_SCHEMA_MINOR;
    std::string module_name;       ///< e.g. "math::utils"
    std::string lib_base;          ///< e.g. "math.utils"
    std::string lib_path;          ///< path to the .so/.a (informational)
    std::string target_triple;     ///< e.g. "x86_64-pc-linux-gnu"
    std::string compiler_ver;      ///< klangc version string
    /// List of module names (canonical form) that this module directly imports.
    /// Used by consumers to perform transitive KDI loading.
    std::vector<std::string> dependencies;
};

// ─────────────────────────────────────────────────────────────────────────────
// KdiTypeTable
// ─────────────────────────────────────────────────────────────────────────────

/** Entry in the global aggregate-type registry. */
struct kdi_aggregate_type_entry {
    std::string fq_name;           ///< fully-qualified K name
    std::string mangled_name;      ///< LLVM struct type name
};

/** Entry in the global enum-type registry. */
struct kdi_enum_type_entry {
    std::string fq_name;           ///< fully-qualified K name
};

/** Flat table of all aggregate and enum types referenced in this KDI. */
struct kdi_type_table {
    std::vector<kdi_aggregate_type_entry> aggregates;
    std::vector<kdi_enum_type_entry>      enums;
};

// ─────────────────────────────────────────────────────────────────────────────
// KdiNamespace
// ─────────────────────────────────────────────────────────────────────────────

struct kdi_namespace {
    std::string                  name;          ///< short name ("" for root)
    std::string                  fq_name;
    std::vector<kdi_namespace>   namespaces;    ///< nested namespaces
    std::vector<kdi_aggregate>   aggregates;    ///< struct / class / interface
    std::vector<kdi_enum>        enums;         ///< enumerations
    std::vector<kdi_union>       unions;        ///< discriminated unions
    std::vector<kdi_function>    functions;     ///< global/static functions (PUBLIC)
    std::vector<kdi_variable>    variables;     ///< global/static variables (PUBLIC)
    std::vector<kdi_template_def> template_defs; ///< template definitions (for cross-module instantiation)
};

// ─────────────────────────────────────────────────────────────────────────────
// KdiUnit
// ─────────────────────────────────────────────────────────────────────────────

struct kdi_unit {
    std::string   name;            ///< module name (same as header.module_name)
    kdi_namespace root_ns;         ///< root namespace (name = "")
};

// ─────────────────────────────────────────────────────────────────────────────
// KdiFile — top-level DTO
// ─────────────────────────────────────────────────────────────────────────────

struct kdi_file {
    kdi_header     header;
    kdi_type_table types;
    kdi_unit       unit;
};

} // namespace kdi

#endif // LIBKDI_FILE_HPP

