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

#ifndef LIBKDI_VALIDATE_HPP
#define LIBKDI_VALIDATE_HPP

/**
 * @file kdi_validate.hpp
 *
 * KDI schema validation.
 *
 * kdi_validate() checks that a kdi_file conforms to schema v0.1:
 *   - correct schema version numbers,
 *   - all aggregate references resolvable within the type table,
 *   - no duplicate fq_names within a scope,
 *   - vtable slot indices contiguous and starting at 0,
 *   - layout field indices strictly increasing.
 *
 * Returns a kdi_validation_result that aggregates all errors found.
 * An empty errors list means the file is valid.
 */

#include "kdi_file.hpp"

#include <string>
#include <vector>

namespace kdi {

struct kdi_validation_error {
    std::string path;     ///< dotted path to the offending field, e.g. "unit.root_ns.aggregates[0].vtable"
    std::string message;
};

struct kdi_validation_result {
    std::vector<kdi_validation_error> errors;

    bool is_valid() const { return errors.empty(); }

    void add(std::string path, std::string message) {
        errors.push_back({std::move(path), std::move(message)});
    }
};

/**
 * Validate a kdi_file against schema v0.1.
 * @param file   The KDI file to validate.
 * @return       A result object; call is_valid() to check overall status.
 */
kdi_validation_result kdi_validate(const kdi_file& file);

} // namespace kdi

#endif // LIBKDI_VALIDATE_HPP

