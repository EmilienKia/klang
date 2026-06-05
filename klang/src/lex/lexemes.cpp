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

#include "lexemes.hpp"

#include "../common/unicode.hpp"

#include <charconv>

namespace k::lex {

namespace {

int hex_digit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/**
 * Decode the raw body of a character or string literal (quotes and prefix
 * already stripped) into Unicode code points, interpreting C-style escape
 * sequences and multi-byte UTF-8. Malformed UTF-8 yields U+FFFD.
 */
void decode_literal_body(std::string_view body, std::vector<char32_t>& out) {
    std::size_t i = 0;
    while (i < body.size()) {
        const char c = body[i];
        if (c == '\\' && i + 1 < body.size()) {
            const char e = body[i + 1];
            switch (e) {
                case 'n':  out.push_back(0x0A); i += 2; break;
                case 'r':  out.push_back(0x0D); i += 2; break;
                case 't':  out.push_back(0x09); i += 2; break;
                case 'a':  out.push_back(0x07); i += 2; break;
                case 'b':  out.push_back(0x08); i += 2; break;
                case 'f':  out.push_back(0x0C); i += 2; break;
                case 'v':  out.push_back(0x0B); i += 2; break;
                case '\\': out.push_back(0x5C); i += 2; break;
                case '\'': out.push_back(0x27); i += 2; break;
                case '"':  out.push_back(0x22); i += 2; break;
                case '?':  out.push_back(0x3F); i += 2; break;
                case '0': case '1': case '2': case '3':
                case '4': case '5': case '6': case '7': {
                    // Octal escape: up to 3 octal digits.
                    i += 1;
                    char32_t value = 0;
                    int count = 0;
                    while (i < body.size() && count < 3
                           && body[i] >= '0' && body[i] <= '7') {
                        value = (value << 3) | static_cast<char32_t>(body[i] - '0');
                        ++i;
                        ++count;
                    }
                    out.push_back(value);
                    break;
                }
                case 'x': {
                    // Hexadecimal escape: one or more hex digits.
                    i += 2;
                    char32_t value = 0;
                    while (i < body.size() && hex_digit(body[i]) >= 0) {
                        value = (value << 4) | static_cast<char32_t>(hex_digit(body[i]));
                        ++i;
                    }
                    out.push_back(value);
                    break;
                }
                case 'u': case 'U': {
                    // Universal character name: 4 (u) or 8 (U) hex digits.
                    const int digits = (e == 'u') ? 4 : 8;
                    i += 2;
                    char32_t value = 0;
                    int count = 0;
                    while (i < body.size() && count < digits && hex_digit(body[i]) >= 0) {
                        value = (value << 4) | static_cast<char32_t>(hex_digit(body[i]));
                        ++i;
                        ++count;
                    }
                    out.push_back(value);
                    break;
                }
                default:
                    // Unknown escape: keep the escaped character verbatim.
                    out.push_back(static_cast<unsigned char>(e));
                    i += 2;
                    break;
            }
        } else if (static_cast<unsigned char>(c) < 0x80) {
            out.push_back(static_cast<unsigned char>(c));
            ++i;
        } else {
            char32_t cp;
            k::unicode::decode_utf8(body, i, cp); // advances i past the sequence
            out.push_back(cp);
        }
    }
}

} // namespace

//
// Integer literal
//
k::value_type integer::value()const {
    auto view = int_content();
    const char* first = view.data();
    const char* last  = first + view.size();

    switch (size) {
        case BYTE: {
            // std::from_chars doesn't support char types directly; parse as int then cast.
            int tmp = 0;
            std::from_chars(first, last, tmp, base);
            if (unsigned_num)
                return static_cast<unsigned char>(tmp);
            else
                return static_cast<char>(tmp);
        }
        case SHORT: {
            if (unsigned_num) {
                unsigned short res = 0;
                std::from_chars(first, last, res, base);
                return res;
            } else {
                short res = 0;
                std::from_chars(first, last, res, base);
                return res;
            }
        }
        case INT: {
            if (unsigned_num) {
                unsigned int res = 0;
                std::from_chars(first, last, res, base);
                return res;
            } else {
                int res = 0;
                std::from_chars(first, last, res, base);
                return res;
            }
        }
        case LONG: {
            if (unsigned_num) {
                unsigned long res = 0;
                std::from_chars(first, last, res, base);
                return res;
            } else {
                long res = 0;
                std::from_chars(first, last, res, base);
                return res;
            }
        }
        case LONGLONG: {
            if (unsigned_num) {
                unsigned long long res = 0;
                std::from_chars(first, last, res, base);
                return res;
            } else {
                long long res = 0;
                std::from_chars(first, last, res, base);
                return res;
            }
        }
        default:
            return {};
    }
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
    auto view = float_content();
    if (size == DOUBLE) {
        double res = 0.0;
        std::from_chars(view.data(), view.data() + view.size(), res);
        return res;
    } else {
        float res = 0.0f;
        std::from_chars(view.data(), view.data() + view.size(), res);
        return res;
    }
}

//
// Character literal
//
k::value_type character::value()const {
    // TODO Decode unicode escape
    return content.at(1);
}

char32_t character::code_point() const {
    // Strip the surrounding quotes; the prefix (if any) is not part of content.
    std::string_view body = content.substr(1, content.size() - 2);
    std::vector<char32_t> cps;
    decode_literal_body(body, cps);
    return cps.empty() ? char32_t{0} : cps.front();
}

//
// String literal
//
k::value_type string::value()const {
    // TODO Decode unicode escape
    return {std::string(content.substr(1, content.size()-2))};
}

std::vector<char32_t> string::code_points() const {
    std::string_view body = content.substr(1, content.size() - 2);
    std::vector<char32_t> cps;
    decode_literal_body(body, cps);
    return cps;
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
