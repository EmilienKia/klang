/*
 * K Language compiler
 *
 * Copyright 2023-24 Emilien Kia
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


#include "common.hpp"

#include "../lex/lexer.hpp"
#include "../parse/ast.hpp"

#include <algorithm>
#include <iostream>
#include <regex>
#include <sstream>

namespace k {


bool name::operator == (const name& other) const {
    if( _root_prefix != other._root_prefix )
        return false;
    if( _identifiers.size() != other._identifiers.size() )
        return false;
    for(size_t i = 0; i < _identifiers.size(); ++i) {
        if(_identifiers[i] != other._identifiers[i])
            return false;
    }
    return true;
}

bool name::start_with(const name& prefix) const {
    if (prefix.size() > size()) {
        return false;
    }
    for (size_t i = 0; i < prefix.size(); ++i) {
        if (at(i) != prefix.at(i)) {
            return false;
        }
    }
    return true;
}

std::string name::to_string()const {
    std::ostringstream stm;
    stm << (_root_prefix ? "::" : "");
    if(_identifiers.empty()) {
        stm << "<<noidentifier>>";
    } else {
        stm << _identifiers.front();
        for(size_t i = 1; i < _identifiers.size(); ++i) {
            stm << "::" << _identifiers[i];
        }
    }
    return stm.str();
}

name name::without_back(size_t count) const {
    return count < size()
            ? name(_root_prefix, _identifiers.begin(), _identifiers.end()-count)
            : name(_root_prefix, {});
}

name name::without_front(size_t count) const {
    return count < size()
            ? name(_root_prefix, _identifiers.begin()+count, _identifiers.end())
            : name(_root_prefix, {});
}

std::pair<std::string, name> name::pop_front() const {
    std::string front = empty() ? "" : this->front();
    return {front, without_front()};
}

std::pair<name, std::string> name::pop_back() const {
    std::string back = empty() ? "" : this->back();
    return {without_back(), back};
}

name name::with_back(const std::string& part) const {
    std::vector<std::string> new_parts = _identifiers;
    new_parts.push_back(part);
    return name(_root_prefix, std::move(new_parts));
}


name name::from(const std::string& str) {

    static const std::regex qualified_name(
        R"(^(::)?[A-Za-z_][A-Za-z_0-9]*(::[A-Za-z_][A-Za-z_0-9]*)*$)",
        std::regex::ECMAScript
    );

    if (!std::regex_match(str, qualified_name)) {
        throw std::runtime_error("Invalid K qualified name: \"" + str + "\"");
    }

    bool absolute = str.rfind("::", 0) == 0;

    std::vector<std::string> parts;
    parts.reserve(6);

    std::size_t pos = absolute ? 2 : 0;
    while (pos <= str.size()) {
        std::size_t next = str.find("::", pos);
        if (next == std::string::npos) {
            parts.emplace_back(str.substr(pos));
            break;
        } else {
            parts.emplace_back(str.substr(pos, next - pos));
            pos = next + 2;
        }
    }

    return { absolute, std::move(parts) };
}

//
// Source management
//

std::string_view source::get_line(unsigned int line) const {
    unsigned int start = lines.size()>line ? lines.at(line) : content.size();
    unsigned int end   = lines.size()>line+1 ? lines.at(line+1) : content.size();
    return {content.data() + start, content.data() + end};
}

char_coord source::get_coordinates(unsigned int pos) const {
    if (pos > content.size() || lines.empty()) {
        return char_coord::INVALID();
    }
    auto it = std::upper_bound(lines.begin(), lines.end(), pos);
    if (it == lines.begin()) {
        return char_coord::INVALID();
    }
    --it;
    unsigned int line = static_cast<unsigned int>(it - lines.begin());
    unsigned int col  = pos - *it;
    return {line, col};
}

char_coord source::get_coordinates(const char_pos& c) const {
    if (c.pos == nullptr
        || c.pos < content.data()
        || c.pos > content.data() + content.size()) {
        return char_coord::INVALID();
    }
    unsigned int pos = static_cast<unsigned int>(c.pos - content.data());
    return get_coordinates(pos);
}


} // namespace k
