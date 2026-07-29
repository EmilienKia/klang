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

#ifndef KLANG_AGGREGATE_VALUE_HPP
#define KLANG_AGGREGATE_VALUE_HPP

#include "../common/common.hpp"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace k::model {

// Forward declaration
class aggregate;

/**
 * Represents a compile-time constant value of an aggregate type (struct or class).
 *
 * Used exclusively during template instantiation to represent concrete template value
 * arguments for aggregate-typed template parameters.
 *
 * Example: for template<Point P> distance(p: Point), a call distance<Point{1, 2}>(...)
 * would create an aggregate_value representing the struct Point with x=1, y=2.
 *
 * Invariants:
 *   1. Every field declared in `type` must have a corresponding entry in `fields`.
 *   2. The type of each field value must match the declared field type (validated
 *      during constexpr evaluation).
 *   3. aggregate_value instances are immutable (created by constexpr evaluator,
 *      used during instantiation, then discarded).
 *   4. A nullptr type is invalid — every aggregate_value must reference a concrete
 *      aggregate definition.
 */
class aggregate_value {
public:
    /**
     * Construct a compile-time aggregate value.
     *
     * @param agg_type  Shared pointer to the aggregate definition (struct or class).
     *                  Must not be nullptr.
     * @param fields    Map from field name → compile-time constant value.
     *                  Must contain an entry for every field in agg_type.
     */
    aggregate_value(
        std::shared_ptr<aggregate> agg_type,
        std::map<std::string, k::value_type> fields
    );

    /**
     * Get the type (aggregate definition) of this value.
     */
    const std::shared_ptr<aggregate>& get_type() const {
        return _type;
    }

    /**
     * Get the map of field name → value.
     *
     * @return Const reference to the internal field map.
     */
    const std::map<std::string, k::value_type>& get_fields() const {
        return _fields;
    }

    /**
     * Look up the compile-time constant value of a named field.
     *
     * @param field_name  The name of the field to retrieve.
     * @return The constant value, or std::nullopt if the field does not exist.
     */
    std::optional<k::value_type> get_field(const std::string& field_name) const;

    /**
     * Check if this aggregate_value has a field with the given name.
     */
    bool has_field(const std::string& field_name) const;

    /**
     * Check equality with another aggregate_value.
     *
     * Two aggregate_values are equal iff:
     *   1. Their types are the same (same aggregate definition), AND
     *   2. All corresponding field values are equal.
     */
    bool operator==(const aggregate_value& other) const;

    bool operator!=(const aggregate_value& other) const {
        return !(*this == other);
    }

    /**
     * Return a human-readable debug representation of this aggregate value.
     *
     * Example output:
     *   "Point{x=1, y=2}"
     *
     * Note: type name is omitted if the aggregate type is not available.
     */
    std::string dump() const;

private:
    std::shared_ptr<aggregate> _type;
    std::map<std::string, k::value_type> _fields;
};

} // namespace k::model

#endif // KLANG_AGGREGATE_VALUE_HPP
