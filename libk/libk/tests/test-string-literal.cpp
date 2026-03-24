/*
 * K Language standard library — String / StringBuilder literal tests
 *
 * Copyright 2026 Emilien Kia
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
 * Tests for ::k::String and ::k::StringBuilder construction and usage
 * with character and string literals.
 *
 * Character literals produce constant char (i8) values.
 * String literals produce static constant { i32, [N x i8] } globals
 * typed as const char[N]& in the model, converted to const char[] on use.
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

#ifndef LIBK_KDI_DIR
#error "LIBK_KDI_DIR must be defined — check CMakeLists.txt"
#endif
#ifndef LIBK_LIB_DIR
#error "LIBK_LIB_DIR must be defined — check CMakeLists.txt"
#endif

namespace {

/// Shorthand: compile K source that uses the base stdlib and JIT it.
std::unique_ptr<k::model::gen::jit> jit_k(std::string_view src) {
    return gen_jit_with_stdlib(src, LIBK_KDI_DIR, LIBK_LIB_DIR);
}

} // anonymous namespace


// ═════════════════════════════════════════════════════════════════════════════
// 1. String — construction from string literal
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("String from string literal — explicit construction", "[libk][string][literal]") {
    auto jit = jit_k(R"SRC(
        module __str_lit_explicit__;

        test_size() : unsigned int {
            s : k::String("hello");
            return s.size();
        }

        test_empty() : int {
            s : k::String("hello");
            if (s.empty()) return 0;
            return 1;
        }

        test_at() : char {
            s : k::String("hello");
            return s.at(0);
        }
    )SRC");
    REQUIRE(jit);

    auto test_size = jit->lookup_symbol<unsigned(*)()>("test_size");
    REQUIRE(test_size);
    CHECK(test_size() == 5);

    auto test_empty = jit->lookup_symbol<int(*)()>("test_empty");
    REQUIRE(test_empty);
    CHECK(test_empty() == 1);

    auto test_at = jit->lookup_symbol<char(*)()>("test_at");
    REQUIRE(test_at);
    CHECK(test_at() == 'h');
}

TEST_CASE("String from string literal — implicit construction (= expr)", "[libk][string][literal]") {
    auto jit = jit_k(R"SRC(
        module __str_lit_implicit__;

        test_size() : unsigned int {
            s : k::String = "world";
            return s.size();
        }

        test_at() : char {
            s : k::String = "world";
            return s.at(2);
        }
    )SRC");
    REQUIRE(jit);

    auto test_size = jit->lookup_symbol<unsigned(*)()>("test_size");
    REQUIRE(test_size);
    CHECK(test_size() == 5);

    auto test_at = jit->lookup_symbol<char(*)()>("test_at");
    REQUIRE(test_at);
    CHECK(test_at() == 'r');
}

TEST_CASE("String from empty string literal", "[libk][string][literal]") {
    auto jit = jit_k(R"SRC(
        module __str_lit_empty__;

        test_size() : unsigned int {
            s : k::String("");
            return s.size();
        }

        test_empty() : int {
            s : k::String("");
            if (s.empty()) return 1;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test_size = jit->lookup_symbol<unsigned(*)()>("test_size");
    REQUIRE(test_size);
    CHECK(test_size() == 0);

    auto test_empty = jit->lookup_symbol<int(*)()>("test_empty");
    REQUIRE(test_empty);
    CHECK(test_empty() == 1);
}

TEST_CASE("String from string literal — character access", "[libk][string][literal]") {
    auto jit = jit_k(R"SRC(
        module __str_lit_at__;

        test_first() : char {
            s : k::String("ABCDE");
            return s.at(0);
        }

        test_mid() : char {
            s : k::String("ABCDE");
            return s.at(2);
        }

        test_last() : char {
            s : k::String("ABCDE");
            return s.at(4);
        }
    )SRC");
    REQUIRE(jit);

    auto test_first = jit->lookup_symbol<char(*)()>("test_first");
    REQUIRE(test_first);
    CHECK(test_first() == 'A');

    auto test_mid = jit->lookup_symbol<char(*)()>("test_mid");
    REQUIRE(test_mid);
    CHECK(test_mid() == 'C');

    auto test_last = jit->lookup_symbol<char(*)()>("test_last");
    REQUIRE(test_last);
    CHECK(test_last() == 'E');
}

TEST_CASE("String from string literal — equality", "[libk][string][literal]") {
    auto jit = jit_k(R"SRC(
        module __str_lit_eq__;

        test_equal() : int {
            s1 : k::String("hello");
            s2 : k::String("hello");
            if (s1 == s2) return 1;
            return 0;
        }

        test_not_equal() : int {
            s1 : k::String("hello");
            s2 : k::String("world");
            if (s1 != s2) return 1;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test_equal = jit->lookup_symbol<int(*)()>("test_equal");
    REQUIRE(test_equal);
    CHECK(test_equal() == 1);

    auto test_not_equal = jit->lookup_symbol<int(*)()>("test_not_equal");
    REQUIRE(test_not_equal);
    CHECK(test_not_equal() == 1);
}

// ═════════════════════════════════════════════════════════════════════════════
// 2. StringBuilder — construction from string literal
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("StringBuilder from string literal — explicit construction", "[libk][string][builder][literal]") {
    auto jit = jit_k(R"SRC(
        module __sb_lit_explicit__;

        test_size() : unsigned int {
            sb : k::StringBuilder("hello");
            return sb.size();
        }

        test_empty() : int {
            sb : k::StringBuilder("hello");
            if (sb.empty()) return 0;
            return 1;
        }

        test_charAt() : char {
            sb : k::StringBuilder("hello");
            return sb.charAt(1);
        }
    )SRC");
    REQUIRE(jit);

    auto test_size = jit->lookup_symbol<unsigned(*)()>("test_size");
    REQUIRE(test_size);
    CHECK(test_size() == 5);

    auto test_empty = jit->lookup_symbol<int(*)()>("test_empty");
    REQUIRE(test_empty);
    CHECK(test_empty() == 1);

    auto test_charAt = jit->lookup_symbol<char(*)()>("test_charAt");
    REQUIRE(test_charAt);
    CHECK(test_charAt() == 'e');
}

TEST_CASE("StringBuilder from string literal — implicit construction", "[libk][string][builder][literal]") {
    auto jit = jit_k(R"SRC(
        module __sb_lit_implicit__;

        test_size() : unsigned int {
            sb : k::StringBuilder = "test";
            return sb.size();
        }

        test_charAt() : char {
            sb : k::StringBuilder = "test";
            return sb.charAt(0);
        }
    )SRC");
    REQUIRE(jit);

    auto test_size = jit->lookup_symbol<unsigned(*)()>("test_size");
    REQUIRE(test_size);
    CHECK(test_size() == 4);

    auto test_charAt = jit->lookup_symbol<char(*)()>("test_charAt");
    REQUIRE(test_charAt);
    CHECK(test_charAt() == 't');
}

TEST_CASE("StringBuilder from empty string literal", "[libk][string][builder][literal]") {
    auto jit = jit_k(R"SRC(
        module __sb_lit_empty__;

        test_size() : unsigned int {
            sb : k::StringBuilder("");
            return sb.size();
        }

        test_empty() : int {
            sb : k::StringBuilder("");
            if (sb.empty()) return 1;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test_size = jit->lookup_symbol<unsigned(*)()>("test_size");
    REQUIRE(test_size);
    CHECK(test_size() == 0);

    auto test_empty = jit->lookup_symbol<int(*)()>("test_empty");
    REQUIRE(test_empty);
    CHECK(test_empty() == 1);
}

// ═════════════════════════════════════════════════════════════════════════════
// 3. StringBuilder — append from string literal
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("StringBuilder append string literal", "[libk][string][builder][literal]") {
    auto jit = jit_k(R"SRC(
        module __sb_append_lit__;

        test_append_size() : unsigned int {
            sb : k::StringBuilder;
            sb.append("abc");
            return sb.size();
        }

        test_append_content() : int {
            sb : k::StringBuilder;
            sb.append("XY");
            if (sb.charAt(0) != 'X') return 1;
            if (sb.charAt(1) != 'Y') return 2;
            return 0;
        }

        test_append_multiple() : unsigned int {
            sb : k::StringBuilder;
            sb.append("hello");
            sb.append(" ");
            sb.append("world");
            return sb.size();
        }

        test_append_multiple_content() : int {
            sb : k::StringBuilder;
            sb.append("AB");
            sb.append("CD");
            if (sb.charAt(0) != 'A') return 1;
            if (sb.charAt(1) != 'B') return 2;
            if (sb.charAt(2) != 'C') return 3;
            if (sb.charAt(3) != 'D') return 4;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test_append_size = jit->lookup_symbol<unsigned(*)()>("test_append_size");
    REQUIRE(test_append_size);
    CHECK(test_append_size() == 3);

    auto test_append_content = jit->lookup_symbol<int(*)()>("test_append_content");
    REQUIRE(test_append_content);
    CHECK(test_append_content() == 0);

    auto test_append_multiple = jit->lookup_symbol<unsigned(*)()>("test_append_multiple");
    REQUIRE(test_append_multiple);
    CHECK(test_append_multiple() == 11);

    auto test_append_multiple_content = jit->lookup_symbol<int(*)()>("test_append_multiple_content");
    REQUIRE(test_append_multiple_content);
    CHECK(test_append_multiple_content() == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// 4. Character literals with StringBuilder
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("StringBuilder appendChar with char literals", "[libk][string][builder][literal][char]") {
    auto jit = jit_k(R"SRC(
        module __sb_charlit__;

        test_appendChars() : unsigned int {
            sb : k::StringBuilder;
            sb.appendChar('H');
            sb.appendChar('i');
            return sb.size();
        }

        test_appendChars_content() : int {
            sb : k::StringBuilder;
            sb.appendChar('H');
            sb.appendChar('i');
            if (sb.charAt(0) != 'H') return 1;
            if (sb.charAt(1) != 'i') return 2;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test_appendChars = jit->lookup_symbol<unsigned(*)()>("test_appendChars");
    REQUIRE(test_appendChars);
    CHECK(test_appendChars() == 2);

    auto test_appendChars_content = jit->lookup_symbol<int(*)()>("test_appendChars_content");
    REQUIRE(test_appendChars_content);
    CHECK(test_appendChars_content() == 0);
}

TEST_CASE("StringBuilder mix string literal and char literal", "[libk][string][builder][literal]") {
    auto jit = jit_k(R"SRC(
        module __sb_mix_lit__;

        test_mix() : int {
            sb : k::StringBuilder("hello");
            sb.appendChar('!');
            if (sb.size() != 6) return 1;
            if (sb.charAt(0) != 'h') return 2;
            if (sb.charAt(4) != 'o') return 3;
            if (sb.charAt(5) != '!') return 4;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test_mix = jit->lookup_symbol<int(*)()>("test_mix");
    REQUIRE(test_mix);
    CHECK(test_mix() == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// 5. Character literal comparison with String.at()
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("String.at() compared with char literal", "[libk][string][literal][char]") {
    auto jit = jit_k(R"SRC(
        module __str_at_charlit__;

        test_compare() : int {
            s : k::String("ABC");
            if (s.at(0) != 'A') return 1;
            if (s.at(1) != 'B') return 2;
            if (s.at(2) != 'C') return 3;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test_compare = jit->lookup_symbol<int(*)()>("test_compare");
    REQUIRE(test_compare);
    CHECK(test_compare() == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// 6. String copy from literal-constructed String
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("String copy constructor from literal-constructed String", "[libk][string][literal]") {
    auto jit = jit_k(R"SRC(
        module __str_lit_copy__;

        test_copy_size() : unsigned int {
            s1 : k::String("hello");
            s2 : k::String(s1);
            return s2.size();
        }

        test_copy_content() : int {
            s1 : k::String("ABC");
            s2 : k::String(s1);
            if (s2.at(0) != 'A') return 1;
            if (s2.at(1) != 'B') return 2;
            if (s2.at(2) != 'C') return 3;
            return 0;
        }

        test_copy_equality() : int {
            s1 : k::String("hello");
            s2 : k::String(s1);
            if (s1 == s2) return 1;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test_copy_size = jit->lookup_symbol<unsigned(*)()>("test_copy_size");
    REQUIRE(test_copy_size);
    CHECK(test_copy_size() == 5);

    auto test_copy_content = jit->lookup_symbol<int(*)()>("test_copy_content");
    REQUIRE(test_copy_content);
    CHECK(test_copy_content() == 0);

    auto test_copy_equality = jit->lookup_symbol<int(*)()>("test_copy_equality");
    REQUIRE(test_copy_equality);
    CHECK(test_copy_equality() == 1);
}

// ═════════════════════════════════════════════════════════════════════════════
// 7. StringBuilder to String conversion from literal
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("StringBuilder to String conversion from literal", "[libk][string][builder][literal]") {
    auto jit = jit_k(R"SRC(
        module __sb_to_str_lit__;

        test_convert_size() : unsigned int {
            sb : k::StringBuilder("hello");
            sb.appendChar('!');
            s : k::String(sb);
            return s.size();
        }

        test_convert_content() : int {
            sb : k::StringBuilder("AB");
            sb.appendChar('C');
            s : k::String(sb);
            if (s.at(0) != 'A') return 1;
            if (s.at(1) != 'B') return 2;
            if (s.at(2) != 'C') return 3;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test_convert_size = jit->lookup_symbol<unsigned(*)()>("test_convert_size");
    REQUIRE(test_convert_size);
    CHECK(test_convert_size() == 6);

    auto test_convert_content = jit->lookup_symbol<int(*)()>("test_convert_content");
    REQUIRE(test_convert_content);
    CHECK(test_convert_content() == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// 8. String constructed from StringBuilder built from literals
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("String from StringBuilder built with string and char literals", "[libk][string][builder][literal]") {
    auto jit = jit_k(R"SRC(
        module __str_from_sb_lit__;

        test_size() : unsigned int {
            sb : k::StringBuilder("Hello");
            sb.append(" ");
            sb.appendChar('W');
            s : k::String(sb);
            return s.size();
        }

        test_content() : int {
            sb : k::StringBuilder("Hi");
            sb.appendChar('!');
            s : k::String(sb);
            if (s.at(0) != 'H') return 1;
            if (s.at(1) != 'i') return 2;
            if (s.at(2) != '!') return 3;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test_size = jit->lookup_symbol<unsigned(*)()>("test_size");
    REQUIRE(test_size);
    CHECK(test_size() == 7);

    auto test_content = jit->lookup_symbol<int(*)()>("test_content");
    REQUIRE(test_content);
    CHECK(test_content() == 0);
}


