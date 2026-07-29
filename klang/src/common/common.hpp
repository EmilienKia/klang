/*
 * K Language compiler
 *
 * Copyright 2023-2024 Emilien Kia
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

#include <limits>
#include <string>
#include <vector>
#include <variant>
#include <memory>

namespace k::model {
    class aggregate_value;  // Forward declaration for k::model::aggregate_value
}

namespace k {


class name
{
protected:
    bool _root_prefix = false;
    std::vector<std::string> _identifiers;

    template<typename IT>
    name(bool root_prefix, IT first, IT last ) : _root_prefix(root_prefix), _identifiers(first, last) {}

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

    bool empty() const {
        return _identifiers.empty();
    }

    const std::vector<std::string>& parts() const {
        return _identifiers;
    }

    const std::string& at(size_t index) const {
        return _identifiers.at(index);
    }

    const std::string& operator[] (size_t index) const {
        return _identifiers[index];
    }

    const std::string& front() const {
        return _identifiers.front();
    }

    const std::string& back() const {
        return _identifiers.back();
    }

    bool operator == (const name& other) const;

    bool start_with(const name& prefix) const;

    std::string to_string()const;

    operator std::string () const{
        return to_string();
    }

    name with_root_prefix() const {
        return name(true, _identifiers);
    }

    name without_root_prefix() const {
        return name(false, _identifiers);
    }

    name without_back(size_t count = 1) const;
    name without_front(size_t count = 1) const;

    std::pair<std::string, name> pop_front() const;
    std::pair<name, std::string> pop_back() const;

    name with_back(const std::string& part) const;

    static name from(const std::string& str);
};

/**
 * Facility for holding some value.
 *
 * Holds either a primitive value (monostate, nullptr, bool, numeric type, string)
 * or a compile-time constant aggregate value (see k::model::aggregate_value).
 */
typedef std::variant<std::monostate,
        std::nullptr_t, bool,
        char, unsigned char,
        short, unsigned short,
        int, unsigned int,
        long, unsigned long,
        long long, unsigned long long,
        float, double,
        std::string,
        std::shared_ptr<k::model::aggregate_value>> value_type;



struct char_pos
{
    const char* pos = nullptr;
};

struct char_coord {
    unsigned int line;
    unsigned int col;

    operator bool() const {
        return line!=std::numeric_limits<unsigned int>().max() || col!=std::numeric_limits<unsigned int>().max();
    }

    static constexpr char_coord INVALID() {return {std::numeric_limits<unsigned int>::max(), std::numeric_limits<unsigned int>::max()};}
};

struct source {
    /// Path from which the text content come
    std::string path;
    /// Content text
    std::string content;
    /// Index of the first character of each line in the 'content' text
    std::vector<unsigned int> lines;

    source() = default;
    source(const source&) = default;
    source(source&&) = default;

    explicit source(const std::string_view& content) : path(""), content(content) {}
    source(const std::string_view& path, const std::string_view& content) : path(path), content(content) {}

    /**
     * Return a view of the expected line in the content text.
     * @param line Line index (0-based)
     * @return The view of the line, empty view if line is not found.
     */
    std::string_view get_line(unsigned int line) const;

    /**
     * Compute the coordinates (line;col) of the given character.
     * @param pos Absolute index of the character in the text content of which compute the coordinates.
     * @return Coordinates of the given character, invalid coordinates (MAX;MAX) if out of range.
     */
    char_coord get_coordinates(unsigned int pos) const;

    /**
     * Compute the coordinates (line;col) of the given character.
     * @param c Pointer to the character to look for. The pointer must look into the valid data range of the content data.
     * @return Coordinates of the given character, invalid coordinates (MAX;MAX) if out of range.
     */
    char_coord get_coordinates(const char_pos& c) const;
};


} // namespace k
#endif //KLANG_COMMON_HPP
