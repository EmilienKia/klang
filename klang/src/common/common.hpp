/*
 * K Language compiler
 *
 * Copyright 2023 Emilien Kia
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

#ifndef KLANG_COMMON_HPP
#define KLANG_COMMON_HPP

#include <string>
#include <vector>
#include <variant>

namespace k {


class name
{
protected:
    bool _root_prefix = false;
    std::vector<std::string> _identifiers;

public:
    name() =default;

    name(const std::string& name) :
            _root_prefix(false), _identifiers({name}) {}

    name(bool root_prefix, const std::string& name) :
            _root_prefix(root_prefix), _identifiers({name}) {}

    name(bool root_prefix, const std::vector<std::string>& identifiers) :
            _root_prefix(root_prefix), _identifiers(identifiers) {}

    name(bool root_prefix, std::vector<std::string>&& identifiers) :
            _root_prefix(root_prefix), _identifiers(identifiers) {}

    name(bool root_prefix, const std::initializer_list<std::string>& identifiers) :
            _root_prefix(root_prefix), _identifiers(identifiers) {}

    name(const name&) = default;
    name(name&&) = default;

    name& operator=(const name&) = default;
    name& operator=(name&&) = default;

    bool has_root_prefix()const {
        return _root_prefix;
    }

    size_t size() const {
        return _identifiers.size();
    }

    const std::string& at(size_t index) const {
        return _identifiers.at(index);
    }

    const std::string& operator[] (size_t index) const {
        return _identifiers[index];
    }

    bool operator == (const name& other) const;

    std::string to_string()const;

    operator std::string () const{
        return to_string();
    }
};

/**
 * Facility for holding some value.
 */
typedef std::variant<std::monostate,
        std::nullptr_t, bool,
        char, unsigned char,
        short, unsigned short,
        int, unsigned int,
        long, unsigned long,
        long long, unsigned long long,
        float, double,
        std::string> value_type;


} // namespace k
#endif //KLANG_COMMON_HPP
