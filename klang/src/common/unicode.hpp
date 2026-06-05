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

#ifndef KLANG_UNICODE_HPP
#define KLANG_UNICODE_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace k::unicode {

/** Unicode replacement character (U+FFFD), emitted on decoding errors. */
constexpr char32_t REPLACEMENT_CHARACTER = 0xFFFD;

/** Highest valid Unicode scalar value. */
constexpr char32_t MAX_CODE_POINT = 0x10FFFF;

/** True if the code point lies in the UTF-16 surrogate range (U+D800–U+DFFF). */
constexpr bool is_surrogate(char32_t cp) {
    return cp >= 0xD800 && cp <= 0xDFFF;
}

/** True if the code point is a valid Unicode scalar value (≤ U+10FFFF, not a surrogate). */
constexpr bool is_valid_code_point(char32_t cp) {
    return cp <= MAX_CODE_POINT && !is_surrogate(cp);
}

/**
 * Decode a single UTF-8 sequence from @p s starting at index @p i.
 * On success, @p i is advanced past the consumed bytes and @p out receives the
 * decoded code point. On an invalid/truncated/overlong sequence, @p out is set
 * to REPLACEMENT_CHARACTER, @p i is advanced by exactly one byte, and the
 * function returns false.
 */
bool decode_utf8(std::string_view s, std::size_t& i, char32_t& out);

/**
 * Decode the whole UTF-8 buffer @p s into a sequence of code points.
 * Invalid sequences are replaced by REPLACEMENT_CHARACTER. Returns true when the
 * entire input was well-formed UTF-8, false if any error was encountered.
 */
bool decode_utf8(std::string_view s, std::vector<char32_t>& out);

/** Validate that @p s is well-formed UTF-8 (no decoding output kept). */
bool validate_utf8(std::string_view s);

/** Append the UTF-8 encoding of @p cp (assumed valid) to @p out. */
void encode_utf8(char32_t cp, std::string& out);

/** Append the UTF-16 encoding of @p cp (assumed valid) to @p out, using a
 *  surrogate pair for supplementary-plane code points. */
void encode_utf16(char32_t cp, std::vector<std::uint16_t>& out);

/** Append the UTF-32 encoding of @p cp (assumed valid) to @p out. */
void encode_utf32(char32_t cp, std::vector<std::uint32_t>& out);

/** Number of UTF-8 code units (bytes) needed to encode @p cp. */
std::size_t utf8_length(char32_t cp);

/** Number of UTF-16 code units (1 or 2) needed to encode @p cp. */
std::size_t utf16_length(char32_t cp);

/** Total UTF-8 code-unit count for the code-point sequence @p cps (no terminator). */
std::size_t utf8_length(const std::vector<char32_t>& cps);

/** Total UTF-16 code-unit count for the code-point sequence @p cps (no terminator). */
std::size_t utf16_length(const std::vector<char32_t>& cps);

/** Total UTF-32 code-unit count for the code-point sequence @p cps (== cps.size()). */
std::size_t utf32_length(const std::vector<char32_t>& cps);

} // namespace k::unicode

#endif // KLANG_UNICODE_HPP
