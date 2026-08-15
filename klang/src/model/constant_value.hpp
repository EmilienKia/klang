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

#ifndef KLANG_MODEL_CONSTANT_VALUE_HPP
#define KLANG_MODEL_CONSTANT_VALUE_HPP

#include "../common/common.hpp"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace k::model {

class aggregate;
class enumeration;
class union_type_def;
class array_type;
class constant_value;

/**
 * Compile-time constant representation of an enum entry.
 */
struct enum_value {
    std::shared_ptr<enumeration> enum_def;
    size_t entry_index = 0;
    int64_t raw_value = 0;
    std::string name;

    bool operator==(const enum_value& other) const;
    bool operator!=(const enum_value& other) const { return !(*this == other); }
    std::string dump() const;
};

/**
 * Compile-time constant representation of a struct value.
 */
class struct_value {
public:
    struct_value(std::shared_ptr<aggregate> type, std::map<std::string, constant_value> fields);

    const std::shared_ptr<aggregate>& get_type() const { return _type; }
    const std::map<std::string, constant_value>& get_fields() const { return _fields; }

    std::optional<constant_value> get_field(const std::string& field_name) const;
    bool has_field(const std::string& field_name) const;

    bool operator==(const struct_value& other) const;
    bool operator!=(const struct_value& other) const { return !(*this == other); }
    std::string dump() const;

private:
    std::shared_ptr<aggregate> _type;
    std::map<std::string, constant_value> _fields;
};

/**
 * Compile-time constant representation of a union value.
 */
class union_value {
public:
    union_value(std::shared_ptr<union_type_def> type, size_t active_index, std::string alt_name, constant_value active_val);

    const std::shared_ptr<union_type_def>& get_type() const { return _type; }
    size_t get_active_index() const { return _active_index; }
    const std::string& get_alternative_name() const { return _alternative_name; }
    const constant_value& get_active_value() const;

    bool operator==(const union_value& other) const;
    bool operator!=(const union_value& other) const { return !(*this == other); }
    std::string dump() const;

private:
    std::shared_ptr<union_type_def> _type;
    size_t _active_index = 0;
    std::string _alternative_name;
    std::shared_ptr<constant_value> _active_value;
};

/**
 * Compile-time constant representation of an array value.
 */
class array_value {
public:
    array_value(std::shared_ptr<array_type> type, std::vector<constant_value> elements);

    const std::shared_ptr<array_type>& get_type() const { return _type; }
    const std::vector<constant_value>& get_elements() const { return _elements; }
    size_t size() const { return _elements.size(); }

    std::optional<constant_value> get_element(size_t index) const;
    bool has_element(size_t index) const;

    bool operator==(const array_value& other) const;
    bool operator!=(const array_value& other) const { return !(*this == other); }
    std::string dump() const;

private:
    std::shared_ptr<array_type> _type;
    std::vector<constant_value> _elements;
};

/**
 * General compile-time constant value held by model expressions.
 */
class constant_value {
public:
    using scalar_t = std::variant<
        std::monostate,
        std::nullptr_t,
        bool,
        char, unsigned char,
        short, unsigned short,
        int, unsigned int,
        long, unsigned long,
        long long, unsigned long long,
        float, double,
        std::string
    >;

    using storage_t = std::variant<
        std::monostate,
        scalar_t,
        enum_value,
        std::shared_ptr<struct_value>,
        std::shared_ptr<union_value>,
        std::shared_ptr<array_value>
    >;

    constant_value() = default;
    constant_value(std::monostate) : _storage(std::monostate{}) {}
    constant_value(std::nullptr_t) : _storage(scalar_t(nullptr)) {}
    constant_value(bool v) : _storage(scalar_t(v)) {}
    constant_value(char v) : _storage(scalar_t(v)) {}
    constant_value(unsigned char v) : _storage(scalar_t(v)) {}
    constant_value(short v) : _storage(scalar_t(v)) {}
    constant_value(unsigned short v) : _storage(scalar_t(v)) {}
    constant_value(int v) : _storage(scalar_t(v)) {}
    constant_value(unsigned int v) : _storage(scalar_t(v)) {}
    constant_value(long v) : _storage(scalar_t(v)) {}
    constant_value(unsigned long v) : _storage(scalar_t(v)) {}
    constant_value(long long v) : _storage(scalar_t(v)) {}
    constant_value(unsigned long long v) : _storage(scalar_t(v)) {}
    constant_value(float v) : _storage(scalar_t(v)) {}
    constant_value(double v) : _storage(scalar_t(v)) {}
    constant_value(const std::string& v) : _storage(scalar_t(v)) {}
    constant_value(std::string&& v) : _storage(scalar_t(std::move(v))) {}
    constant_value(const char* v) : _storage(scalar_t(std::string(v))) {}
    constant_value(enum_value ev) : _storage(std::move(ev)) {}
    constant_value(std::shared_ptr<struct_value> sv) : _storage(std::move(sv)) {}
    constant_value(std::shared_ptr<union_value> uv) : _storage(std::move(uv)) {}
    constant_value(std::shared_ptr<array_value> av) : _storage(std::move(av)) {}
    constant_value(const k::value_type& vt);

    bool is_valid() const;
    bool is_null() const;
    bool is_bool() const;
    bool is_integer() const;
    bool is_signed_integer() const;
    bool is_unsigned_integer() const;
    bool is_float() const;
    bool is_numeric() const;
    bool is_string() const;
    bool is_enum() const;
    bool is_struct() const;
    bool is_union() const;
    bool is_array() const;

    bool get_bool() const;
    int64_t get_int64() const;
    uint64_t get_uint64() const;
    double get_double() const;
    const std::string& get_string() const;
    const enum_value& get_enum() const;
    std::shared_ptr<struct_value> get_struct() const;
    std::shared_ptr<union_value> get_union() const;
    std::shared_ptr<array_value> get_array() const;

    bool as_numeric(bool& is_float, int64_t& ival, double& fval) const;

    bool operator==(const constant_value& other) const;
    bool operator!=(const constant_value& other) const { return !(*this == other); }

    std::string dump() const;

    const storage_t& storage() const { return _storage; }

    /** Convert to legacy k::value_type if possible */
    std::optional<k::value_type> to_value_type() const;

private:
    storage_t _storage;
};

} // namespace k::model

#endif // KLANG_MODEL_CONSTANT_VALUE_HPP




