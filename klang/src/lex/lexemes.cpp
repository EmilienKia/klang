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

#include "lexemes.hpp"

#include <charconv>

namespace k::lex {

//
// Integer literal
//
k::value_type integer::value()const {
    // TODO
    return {};
}

unsigned int integer::to_unsigned_int() const {
    unsigned int res;
    auto view = int_content();
    std::from_chars(view.data(), view.data() + view.size(), res, base);
    return res;
}

//
// Floating point number litteral
//
k::value_type float_num::value() const {
    return {};
}

//
// Character literal
//
k::value_type character::value()const {
    // TODO Decode unicode escape
    return content.at(1);
}

//
// String literal
//
k::value_type string::value()const {
    // TODO Decode unicode escape
    return {content.substr(1, content.size()-2)};
}

//
// Boolean literal
//
k::value_type boolean::value()const {
    if(content=="true") {
        return {true};
    } else {
        return {false};
    }
}

//
// Null literal
//
k::value_type null::value()const {
    return {nullptr};
}


} // k::lex
