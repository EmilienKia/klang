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

#include "constant_value.hpp"
#include "aggregate_value.hpp"
#include "model_aggregate.hpp"
#include "model_enum.hpp"
#include "model_union.hpp"
#include "../parse/ast.hpp"

#include <sstream>

namespace k::model {

// ════════════════════════════════════════════════════════════════════════════
// enum_value
// ════════════════════════════════════════════════════════════════════════════

bool enum_value::operator==(const enum_value& other) const {
    if (enum_def != other.enum_def) return false;
    return raw_value == other.raw_value && entry_index == other.entry_index;
}

std::string enum_value::dump() const {
    std::ostringstream ss;
    if (enum_def) {
        ss << enum_def->get_short_name() << "::";
    }
    ss << name << "(" << raw_value << ")";
    return ss.str();
}

// ════════════════════════════════════════════════════════════════════════════
// struct_value
// ════════════════════════════════════════════════════════════════════════════

struct_value::struct_value(std::shared_ptr<aggregate> type, std::map<std::string, constant_value> fields)
    : _type(std::move(type)), _fields(std::move(fields)) {}

std::optional<constant_value> struct_value::get_field(const std::string& field_name) const {
    auto it = _fields.find(field_name);
    if (it != _fields.end()) {
        return it->second;
    }
    return std::nullopt;
}

bool struct_value::has_field(const std::string& field_name) const {
    return _fields.find(field_name) != _fields.end();
}

bool struct_value::operator==(const struct_value& other) const {
    if (_type != other._type) return false;
    if (_fields.size() != other._fields.size()) return false;
    for (const auto& [name, val] : _fields) {
        auto it = other._fields.find(name);
        if (it == other._fields.end()) return false;
        if (val != it->second) return false;
    }
    return true;
}

std::string struct_value::dump() const {
    std::ostringstream ss;
    if (_type) {
        ss << _type->get_short_name();
    } else {
        ss << "struct";
    }
    ss << "{";
    bool first = true;
    for (const auto& [name, val] : _fields) {
        if (!first) ss << ", ";
        first = false;
        ss << "." << name << " = " << val.dump();
    }
    ss << "}";
    return ss.str();
}

// ════════════════════════════════════════════════════════════════════════════
// union_value
// ════════════════════════════════════════════════════════════════════════════

union_value::union_value(std::shared_ptr<union_type_def> type, size_t active_index, std::string alt_name, constant_value active_val)
    : _type(std::move(type)), _active_index(active_index), _alternative_name(std::move(alt_name)),
      _active_value(std::make_shared<constant_value>(std::move(active_val))) {}

const constant_value& union_value::get_active_value() const {
    static const constant_value invalid_val;
    return _active_value ? *_active_value : invalid_val;
}

bool union_value::operator==(const union_value& other) const {
    if (_type != other._type) return false;
    if (_active_index != other._active_index) return false;
    if (_alternative_name != other._alternative_name) return false;
    return get_active_value() == other.get_active_value();
}

std::string union_value::dump() const {
    std::ostringstream ss;
    if (_type) {
        ss << _type->get_short_name();
    } else {
        ss << "union";
    }
    ss << "{." << _alternative_name << " = " << get_active_value().dump() << "}";
    return ss.str();
}

// ════════════════════════════════════════════════════════════════════════════
// array_value
// ════════════════════════════════════════════════════════════════════════════

array_value::array_value(std::shared_ptr<array_type> type, std::vector<constant_value> elements)
    : _type(std::move(type)), _elements(std::move(elements)) {}

std::optional<constant_value> array_value::get_element(size_t index) const {
    if (index < _elements.size()) {
        return _elements[index];
    }
    return std::nullopt;
}

bool array_value::has_element(size_t index) const {
    return index < _elements.size();
}

bool array_value::operator==(const array_value& other) const {
    if (_elements.size() != other._elements.size()) return false;
    for (size_t i = 0; i < _elements.size(); ++i) {
        if (_elements[i] != other._elements[i]) return false;
    }
    return true;
}

std::string array_value::dump() const {
    std::ostringstream ss;
    ss << "[";
    for (size_t i = 0; i < _elements.size(); ++i) {
        if (i > 0) ss << ", ";
        ss << _elements[i].dump();
    }
    ss << "]";
    return ss.str();
}

// ════════════════════════════════════════════════════════════════════════════
// constant_value
// ════════════════════════════════════════════════════════════════════════════

constant_value::constant_value(const k::value_type& vt) {
    std::visit([this](auto&& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
            _storage = std::monostate{};
        } else if constexpr (std::is_same_v<T, std::shared_ptr<k::model::aggregate_value>>) {
            if (v) {
                std::map<std::string, constant_value> field_map;
                for (const auto& [fname, fval] : v->get_fields()) {
                    field_map[fname] = constant_value(fval);
                }
                _storage = std::make_shared<struct_value>(v->get_type(), std::move(field_map));
            } else {
                _storage = std::monostate{};
            }
        } else {
            _storage = scalar_t(v);
        }
    }, vt);
}

bool constant_value::is_valid() const {
    return !std::holds_alternative<std::monostate>(_storage);
}

bool constant_value::is_null() const {
    if (auto* sc = std::get_if<scalar_t>(&_storage)) {
        return std::holds_alternative<std::nullptr_t>(*sc);
    }
    return false;
}

bool constant_value::is_bool() const {
    if (auto* sc = std::get_if<scalar_t>(&_storage)) {
        return std::holds_alternative<bool>(*sc);
    }
    return false;
}

bool constant_value::is_integer() const {
    if (auto* sc = std::get_if<scalar_t>(&_storage)) {
        return std::holds_alternative<char>(*sc) ||
               std::holds_alternative<unsigned char>(*sc) ||
               std::holds_alternative<short>(*sc) ||
               std::holds_alternative<unsigned short>(*sc) ||
               std::holds_alternative<int>(*sc) ||
               std::holds_alternative<unsigned int>(*sc) ||
               std::holds_alternative<long>(*sc) ||
               std::holds_alternative<unsigned long>(*sc) ||
               std::holds_alternative<long long>(*sc) ||
               std::holds_alternative<unsigned long long>(*sc);
    }
    return false;
}

bool constant_value::is_signed_integer() const {
    if (auto* sc = std::get_if<scalar_t>(&_storage)) {
        return std::holds_alternative<char>(*sc) ||
               std::holds_alternative<short>(*sc) ||
               std::holds_alternative<int>(*sc) ||
               std::holds_alternative<long>(*sc) ||
               std::holds_alternative<long long>(*sc);
    }
    return false;
}

bool constant_value::is_unsigned_integer() const {
    if (auto* sc = std::get_if<scalar_t>(&_storage)) {
        return std::holds_alternative<unsigned char>(*sc) ||
               std::holds_alternative<unsigned short>(*sc) ||
               std::holds_alternative<unsigned int>(*sc) ||
               std::holds_alternative<unsigned long>(*sc) ||
               std::holds_alternative<unsigned long long>(*sc);
    }
    return false;
}

bool constant_value::is_float() const {
    if (auto* sc = std::get_if<scalar_t>(&_storage)) {
        return std::holds_alternative<float>(*sc) ||
               std::holds_alternative<double>(*sc);
    }
    return false;
}

bool constant_value::is_numeric() const {
    return is_integer() || is_float() || is_bool();
}

bool constant_value::is_string() const {
    if (auto* sc = std::get_if<scalar_t>(&_storage)) {
        return std::holds_alternative<std::string>(*sc);
    }
    return false;
}

bool constant_value::is_enum() const {
    return std::holds_alternative<enum_value>(_storage);
}

bool constant_value::is_struct() const {
    return std::holds_alternative<std::shared_ptr<struct_value>>(_storage) &&
           std::get<std::shared_ptr<struct_value>>(_storage) != nullptr;
}

bool constant_value::is_union() const {
    return std::holds_alternative<std::shared_ptr<union_value>>(_storage) &&
           std::get<std::shared_ptr<union_value>>(_storage) != nullptr;
}

bool constant_value::is_array() const {
    return std::holds_alternative<std::shared_ptr<array_value>>(_storage) &&
           std::get<std::shared_ptr<array_value>>(_storage) != nullptr;
}

bool constant_value::get_bool() const {
    if (auto* sc = std::get_if<scalar_t>(&_storage)) {
        if (auto* b = std::get_if<bool>(sc)) return *b;
    }
    return false;
}

int64_t constant_value::get_int64() const {
    if (auto* sc = std::get_if<scalar_t>(&_storage)) {
        return std::visit([](auto&& x) -> int64_t {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
                return static_cast<int64_t>(x);
            } else if constexpr (std::is_same_v<T, bool>) {
                return x ? 1 : 0;
            }
            return 0;
        }, *sc);
    }
    if (auto* ev = std::get_if<enum_value>(&_storage)) {
        return ev->raw_value;
    }
    return 0;
}

uint64_t constant_value::get_uint64() const {
    if (auto* sc = std::get_if<scalar_t>(&_storage)) {
        return std::visit([](auto&& x) -> uint64_t {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
                return static_cast<uint64_t>(x);
            } else if constexpr (std::is_same_v<T, bool>) {
                return x ? 1 : 0;
            }
            return 0;
        }, *sc);
    }
    if (auto* ev = std::get_if<enum_value>(&_storage)) {
        return static_cast<uint64_t>(ev->raw_value);
    }
    return 0;
}

double constant_value::get_double() const {
    if (auto* sc = std::get_if<scalar_t>(&_storage)) {
        return std::visit([](auto&& x) -> double {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_arithmetic_v<T>) {
                return static_cast<double>(x);
            }
            return 0.0;
        }, *sc);
    }
    if (auto* ev = std::get_if<enum_value>(&_storage)) {
        return static_cast<double>(ev->raw_value);
    }
    return 0.0;
}

const std::string& constant_value::get_string() const {
    static const std::string empty;
    if (auto* sc = std::get_if<scalar_t>(&_storage)) {
        if (auto* s = std::get_if<std::string>(sc)) return *s;
    }
    return empty;
}

const enum_value& constant_value::get_enum() const {
    static const enum_value empty;
    if (auto* ev = std::get_if<enum_value>(&_storage)) {
        return *ev;
    }
    return empty;
}

std::shared_ptr<struct_value> constant_value::get_struct() const {
    if (auto* sv = std::get_if<std::shared_ptr<struct_value>>(&_storage)) {
        return *sv;
    }
    return nullptr;
}

std::shared_ptr<union_value> constant_value::get_union() const {
    if (auto* uv = std::get_if<std::shared_ptr<union_value>>(&_storage)) {
        return *uv;
    }
    return nullptr;
}

std::shared_ptr<array_value> constant_value::get_array() const {
    if (auto* av = std::get_if<std::shared_ptr<array_value>>(&_storage)) {
        return *av;
    }
    return nullptr;
}

bool constant_value::as_numeric(bool& is_float, int64_t& ival, double& fval) const {
    if (auto* sc = std::get_if<scalar_t>(&_storage)) {
        return std::visit([&](auto&& x) -> bool {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, std::monostate> || std::is_same_v<T, std::nullptr_t>
                          || std::is_same_v<T, std::string>) {
                return false;
            } else if constexpr (std::is_floating_point_v<T>) {
                is_float = true;
                fval = static_cast<double>(x);
                ival = static_cast<int64_t>(x);
                return true;
            } else if constexpr (std::is_same_v<T, bool>) {
                is_float = false;
                ival = x ? 1 : 0;
                fval = x ? 1.0 : 0.0;
                return true;
            } else if constexpr (std::is_integral_v<T>) {
                is_float = false;
                ival = static_cast<int64_t>(x);
                fval = static_cast<double>(x);
                return true;
            } else {
                return false;
            }
        }, *sc);
    }
    if (auto* ev = std::get_if<enum_value>(&_storage)) {
        is_float = false;
        ival = ev->raw_value;
        fval = static_cast<double>(ev->raw_value);
        return true;
    }
    return false;
}

bool constant_value::operator==(const constant_value& other) const {
    if (_storage.index() != other._storage.index()) return false;
    return std::visit([&](auto&& lhs) -> bool {
        using T = std::decay_t<decltype(lhs)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
            return true;
        } else if constexpr (std::is_same_v<T, scalar_t>) {
            const auto& rhs = std::get<scalar_t>(other._storage);
            return lhs == rhs;
        } else if constexpr (std::is_same_v<T, enum_value>) {
            const auto& rhs = std::get<enum_value>(other._storage);
            return lhs == rhs;
        } else if constexpr (std::is_same_v<T, std::shared_ptr<struct_value>>) {
            const auto& rhs = std::get<std::shared_ptr<struct_value>>(other._storage);
            if (!lhs && !rhs) return true;
            if (!lhs || !rhs) return false;
            return *lhs == *rhs;
        } else if constexpr (std::is_same_v<T, std::shared_ptr<union_value>>) {
            const auto& rhs = std::get<std::shared_ptr<union_value>>(other._storage);
            if (!lhs && !rhs) return true;
            if (!lhs || !rhs) return false;
            return *lhs == *rhs;
        } else if constexpr (std::is_same_v<T, std::shared_ptr<array_value>>) {
            const auto& rhs = std::get<std::shared_ptr<array_value>>(other._storage);
            if (!lhs && !rhs) return true;
            if (!lhs || !rhs) return false;
            return *lhs == *rhs;
        }
        return false;
    }, _storage);
}

std::string constant_value::dump() const {
    if (!is_valid()) return "<invalid>";
    if (auto* sc = std::get_if<scalar_t>(&_storage)) {
        return std::visit([](auto&& x) -> std::string {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, std::monostate>) return "<none>";
            else if constexpr (std::is_same_v<T, std::nullptr_t>) return "null";
            else if constexpr (std::is_same_v<T, bool>) return x ? "true" : "false";
            else if constexpr (std::is_same_v<T, std::string>) return "\"" + x + "\"";
            else if constexpr (std::is_same_v<T, char>) return std::string("'") + x + "'";
            else return std::to_string(x);
        }, *sc);
    }
    if (auto* ev = std::get_if<enum_value>(&_storage)) {
        return ev->dump();
    }
    if (auto* sv = std::get_if<std::shared_ptr<struct_value>>(&_storage)) {
        return (*sv) ? (*sv)->dump() : "struct<null>";
    }
    if (auto* uv = std::get_if<std::shared_ptr<union_value>>(&_storage)) {
        return (*uv) ? (*uv)->dump() : "union<null>";
    }
    if (auto* av = std::get_if<std::shared_ptr<array_value>>(&_storage)) {
        return (*av) ? (*av)->dump() : "array<null>";
    }
    return "<unknown>";
}

std::optional<k::value_type> constant_value::to_value_type() const {
    if (!is_valid()) return std::nullopt;
    if (auto* sc = std::get_if<scalar_t>(&_storage)) {
        return std::visit([](auto&& x) -> std::optional<k::value_type> {
            using T = std::decay_t<decltype(x)>;
            if constexpr (std::is_same_v<T, std::monostate>) return std::nullopt;
            else return k::value_type(x);
        }, *sc);
    }
    if (auto* ev = std::get_if<enum_value>(&_storage)) {
        return k::value_type(ev->raw_value);
    }
    if (auto* sv = std::get_if<std::shared_ptr<struct_value>>(&_storage)) {
        if (!*sv) return std::nullopt;
        std::map<std::string, k::value_type> legacy_fields;
        for (const auto& [fname, fval] : (*sv)->get_fields()) {
            auto legacy_fval = fval.to_value_type();
            if (!legacy_fval) return std::nullopt;
            legacy_fields[fname] = *legacy_fval;
        }
        return k::value_type(std::make_shared<k::model::aggregate_value>((*sv)->get_type(), std::move(legacy_fields)));
    }
    // union_value has no legacy aggregate_value representation
    return std::nullopt;
}

} // namespace k::model






