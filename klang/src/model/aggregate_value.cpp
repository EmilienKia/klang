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

#include "aggregate_value.hpp"
#include "model.hpp"  // for complete aggregate definition

namespace k::model {

aggregate_value::aggregate_value(
    std::shared_ptr<aggregate> agg_type,
    std::map<std::string, k::value_type> fields
)
    : _type(std::move(agg_type)), _fields(std::move(fields)) {
    // Invariant check: type must not be nullptr
    // (validation of field presence/types happens during constexpr evaluation)
}

std::optional<k::value_type> aggregate_value::get_field(
    const std::string& field_name) const {
    auto it = _fields.find(field_name);
    if (it != _fields.end()) {
        return it->second;
    }
    return std::nullopt;
}

bool aggregate_value::has_field(const std::string& field_name) const {
    return _fields.find(field_name) != _fields.end();
}

bool aggregate_value::operator==(const aggregate_value& other) const {
    // Compare types first (pointer equality is sufficient since aggregates
    // are interned in the model)
    if (_type != other._type) {
        return false;
    }

    // Compare field count
    if (_fields.size() != other._fields.size()) {
        return false;
    }

    // Compare each field value
    for (const auto& [name, value] : _fields) {
        auto it = other._fields.find(name);
        if (it == other._fields.end() || it->second != value) {
            return false;
        }
    }

    return true;
}

std::string aggregate_value::dump() const {
    std::string result;

    // Append type name if available
    if (_type) {
        result += _type->get_short_name();
    } else {
        result += "<?unknown>";
    }

    result += "{";

    bool first = true;
    for (const auto& [name, value] : _fields) {
        if (!first) {
            result += ", ";
        }
        first = false;

        result += name;
        result += "=";

        // Format the value
        std::visit(
            [&result](auto&& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    result += "{}";
                } else if constexpr (std::is_same_v<T, std::nullptr_t>) {
                    result += "null";
                } else if constexpr (std::is_same_v<T, bool>) {
                    result += v ? "true" : "false";
                } else if constexpr (std::is_same_v<T, std::string>) {
                    result += "\"";
                    result += v;
                    result += "\"";
                } else if constexpr (std::is_same_v<T, std::shared_ptr<k::model::aggregate_value>>) {
                    // Recursively dump nested aggregate values
                    if (v) {
                        result += v->dump();
                    } else {
                        result += "<?null>";
                    }
                } else {
                    result += std::to_string(v);
                }
            },
            value
        );
    }

    result += "}";
    return result;
}

} // namespace k::model
