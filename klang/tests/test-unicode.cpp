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

/**
 * Unit tests for the UTF-8/16/32 helpers in common/unicode.hpp.
 */

#include <catch2/catch_all.hpp>

#include "../src/common/unicode.hpp"

using namespace k::unicode;

namespace {

std::vector<char32_t> decode_all(std::string_view s, bool& ok) {
    std::vector<char32_t> out;
    ok = decode_utf8(s, out);
    return out;
}

std::string encode_all_utf8(const std::vector<char32_t>& cps) {
    std::string out;
    for (char32_t cp : cps) encode_utf8(cp, out);
    return out;
}

} // namespace

TEST_CASE("unicode — code point classification", "[unicode]") {
    REQUIRE(is_valid_code_point(0x00));
    REQUIRE(is_valid_code_point(0x41));
    REQUIRE(is_valid_code_point(0xE9));       // é
    REQUIRE(is_valid_code_point(0x20AC));     // €
    REQUIRE(is_valid_code_point(0x1F600));    // 😀
    REQUIRE(is_valid_code_point(MAX_CODE_POINT));

    REQUIRE_FALSE(is_valid_code_point(0xD800)); // lone surrogate
    REQUIRE_FALSE(is_valid_code_point(0xDFFF));
    REQUIRE_FALSE(is_valid_code_point(MAX_CODE_POINT + 1));

    REQUIRE(is_surrogate(0xD800));
    REQUIRE(is_surrogate(0xDC00));
    REQUIRE_FALSE(is_surrogate(0xE000));
}

TEST_CASE("unicode — UTF-8 decode well-formed input", "[unicode]") {
    bool ok = false;

    SECTION("ASCII") {
        auto cps = decode_all("ABC", ok);
        REQUIRE(ok);
        REQUIRE(cps == std::vector<char32_t>{'A', 'B', 'C'});
    }

    SECTION("2-byte (é = U+00E9)") {
        auto cps = decode_all("\xC3\xA9", ok);
        REQUIRE(ok);
        REQUIRE(cps == std::vector<char32_t>{0xE9});
    }

    SECTION("3-byte (€ = U+20AC)") {
        auto cps = decode_all("\xE2\x82\xAC", ok);
        REQUIRE(ok);
        REQUIRE(cps == std::vector<char32_t>{0x20AC});
    }

    SECTION("4-byte (😀 = U+1F600)") {
        auto cps = decode_all("\xF0\x9F\x98\x80", ok);
        REQUIRE(ok);
        REQUIRE(cps == std::vector<char32_t>{0x1F600});
    }

    SECTION("mixed") {
        auto cps = decode_all("a\xC3\xA9\xE2\x82\xAC", ok);
        REQUIRE(ok);
        REQUIRE(cps == std::vector<char32_t>{'a', 0xE9, 0x20AC});
    }
}

TEST_CASE("unicode — UTF-8 decode rejects malformed input", "[unicode]") {
    REQUIRE_FALSE(validate_utf8("\xC3"));            // truncated 2-byte
    REQUIRE_FALSE(validate_utf8("\xE2\x82"));        // truncated 3-byte
    REQUIRE_FALSE(validate_utf8("\x80"));            // stray continuation
    REQUIRE_FALSE(validate_utf8("\xC0\xAF"));        // overlong '/'
    REQUIRE_FALSE(validate_utf8("\xED\xA0\x80"));    // surrogate U+D800
    REQUIRE_FALSE(validate_utf8("\xF5\x80\x80\x80")); // > U+10FFFF

    REQUIRE(validate_utf8("plain ascii"));
    REQUIRE(validate_utf8("\xC3\xA9\xE2\x82\xAC"));
}

TEST_CASE("unicode — decode yields replacement char on error", "[unicode]") {
    bool ok = true;
    auto cps = decode_all("\x80", ok);
    REQUIRE_FALSE(ok);
    REQUIRE(cps == std::vector<char32_t>{REPLACEMENT_CHARACTER});
}

TEST_CASE("unicode — UTF-8 encode round-trips", "[unicode]") {
    const std::vector<char32_t> cps{'A', 0xE9, 0x20AC, 0x1F600};
    std::string encoded = encode_all_utf8(cps);

    bool ok = false;
    auto back = decode_all(encoded, ok);
    REQUIRE(ok);
    REQUIRE(back == cps);
}

TEST_CASE("unicode — UTF-16 encode and surrogate pairs", "[unicode]") {
    std::vector<std::uint16_t> out;

    encode_utf16(0x41, out);
    REQUIRE(out == std::vector<std::uint16_t>{0x41});

    out.clear();
    encode_utf16(0x20AC, out);
    REQUIRE(out == std::vector<std::uint16_t>{0x20AC});

    out.clear();
    encode_utf16(0x1F600, out); // supplementary plane → surrogate pair
    REQUIRE(out == std::vector<std::uint16_t>{0xD83D, 0xDE00});
}

TEST_CASE("unicode — UTF-32 encode is identity", "[unicode]") {
    std::vector<std::uint32_t> out;
    encode_utf32(0x1F600, out);
    REQUIRE(out == std::vector<std::uint32_t>{0x1F600});
}

TEST_CASE("unicode — encoded length computations", "[unicode]") {
    REQUIRE(utf8_length(char32_t{0x41}) == 1);
    REQUIRE(utf8_length(char32_t{0xE9}) == 2);
    REQUIRE(utf8_length(char32_t{0x20AC}) == 3);
    REQUIRE(utf8_length(char32_t{0x1F600}) == 4);

    REQUIRE(utf16_length(char32_t{0x41}) == 1);
    REQUIRE(utf16_length(char32_t{0xFFFF}) == 1);
    REQUIRE(utf16_length(char32_t{0x1F600}) == 2);

    const std::vector<char32_t> cps{'A', 0xE9, 0x20AC, 0x1F600};
    REQUIRE(utf8_length(cps) == 1 + 2 + 3 + 4);
    REQUIRE(utf16_length(cps) == 1 + 1 + 1 + 2);
    REQUIRE(utf32_length(cps) == 4);
}
