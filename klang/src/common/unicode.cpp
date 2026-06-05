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

#include "unicode.hpp"

namespace k::unicode {

bool decode_utf8(std::string_view s, std::size_t& i, char32_t& out) {
    if (i >= s.size()) {
        out = REPLACEMENT_CHARACTER;
        return false;
    }

    const auto byte = static_cast<unsigned char>(s[i]);

    // Single-byte ASCII.
    if (byte < 0x80) {
        out = byte;
        ++i;
        return true;
    }

    int extra;        // number of continuation bytes
    char32_t cp;      // accumulator
    char32_t min_cp;  // smallest legal value for this length (overlong check)

    if ((byte & 0xE0) == 0xC0) {        // 110xxxxx
        extra = 1;
        cp = byte & 0x1F;
        min_cp = 0x80;
    } else if ((byte & 0xF0) == 0xE0) { // 1110xxxx
        extra = 2;
        cp = byte & 0x0F;
        min_cp = 0x800;
    } else if ((byte & 0xF8) == 0xF0) { // 11110xxx
        extra = 3;
        cp = byte & 0x07;
        min_cp = 0x10000;
    } else {
        // Continuation byte as leader, or 0xF8..0xFF: invalid.
        out = REPLACEMENT_CHARACTER;
        ++i;
        return false;
    }

    // Need `extra` continuation bytes following.
    if (i + static_cast<std::size_t>(extra) >= s.size()) {
        out = REPLACEMENT_CHARACTER;
        ++i;
        return false;
    }

    for (int k = 1; k <= extra; ++k) {
        const auto cont = static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]);
        if ((cont & 0xC0) != 0x80) { // not 10xxxxxx
            out = REPLACEMENT_CHARACTER;
            ++i;
            return false;
        }
        cp = (cp << 6) | (cont & 0x3F);
    }

    // Reject overlong encodings, surrogates and out-of-range values.
    if (cp < min_cp || !is_valid_code_point(cp)) {
        out = REPLACEMENT_CHARACTER;
        ++i;
        return false;
    }

    out = cp;
    i += static_cast<std::size_t>(extra) + 1;
    return true;
}

bool decode_utf8(std::string_view s, std::vector<char32_t>& out) {
    bool ok = true;
    std::size_t i = 0;
    while (i < s.size()) {
        char32_t cp;
        if (!decode_utf8(s, i, cp)) {
            ok = false;
        }
        out.push_back(cp);
    }
    return ok;
}

bool validate_utf8(std::string_view s) {
    std::size_t i = 0;
    while (i < s.size()) {
        char32_t cp;
        if (!decode_utf8(s, i, cp)) {
            return false;
        }
    }
    return true;
}

void encode_utf8(char32_t cp, std::string& out) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

void encode_utf16(char32_t cp, std::vector<std::uint16_t>& out) {
    if (cp < 0x10000) {
        out.push_back(static_cast<std::uint16_t>(cp));
    } else {
        const char32_t v = cp - 0x10000;
        out.push_back(static_cast<std::uint16_t>(0xD800 + (v >> 10)));
        out.push_back(static_cast<std::uint16_t>(0xDC00 + (v & 0x3FF)));
    }
}

void encode_utf32(char32_t cp, std::vector<std::uint32_t>& out) {
    out.push_back(static_cast<std::uint32_t>(cp));
}

std::size_t utf8_length(char32_t cp) {
    if (cp < 0x80)    return 1;
    if (cp < 0x800)   return 2;
    if (cp < 0x10000) return 3;
    return 4;
}

std::size_t utf16_length(char32_t cp) {
    return cp < 0x10000 ? 1 : 2;
}

std::size_t utf8_length(const std::vector<char32_t>& cps) {
    std::size_t total = 0;
    for (char32_t cp : cps) {
        total += utf8_length(cp);
    }
    return total;
}

std::size_t utf16_length(const std::vector<char32_t>& cps) {
    std::size_t total = 0;
    for (char32_t cp : cps) {
        total += utf16_length(cp);
    }
    return total;
}

std::size_t utf32_length(const std::vector<char32_t>& cps) {
    return cps.size();
}

} // namespace k::unicode
