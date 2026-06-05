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
 * Tests for character and string literal code generation.
 *
 * Character literals produce constant char (i8) values.
 * String literals produce static constant { i32, [N x i8] } globals
 * typed as const char[N]& in the model.
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

// =============================================================================
// CHARACTER LITERALS
// =============================================================================

TEST_CASE("Char literal — basic value", "[gen][literal][char]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : char {
            c : char = 'A';
            return c;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<char(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 'A');
}

TEST_CASE("Char literal — comparison", "[gen][literal][char]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : bool {
            c : char = 'B';
            return c == 'B';
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<bool(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == true);
}

TEST_CASE("Char literal — escape sequences decode correctly", "[gen][literal][char]") {
    auto jit = gen_jit(R"SRC(
        module test;

        newline() : char { return '\n'; }
        tab()     : char { return '\t'; }
        cr()      : char { return '\r'; }
        nul()     : char { return '\0'; }
        backslash() : char { return '\\'; }
        hex()     : char { return '\x41'; }
    )SRC");
    REQUIRE(jit);
    REQUIRE(jit->lookup_symbol<char(*)()>("newline")() == '\n');
    REQUIRE(jit->lookup_symbol<char(*)()>("tab")() == '\t');
    REQUIRE(jit->lookup_symbol<char(*)()>("cr")() == '\r');
    REQUIRE(jit->lookup_symbol<char(*)()>("nul")() == '\0');
    REQUIRE(jit->lookup_symbol<char(*)()>("backslash")() == '\\');
    REQUIRE(jit->lookup_symbol<char(*)()>("hex")() == 'A');
}

// =============================================================================
// ENCODING-PREFIXED LITERALS
// =============================================================================

TEST_CASE("UTF-8 prefixed string literal — element type unsigned byte", "[gen][literal][string][prefix]") {
    auto jit = gen_jit(R"SRC(
        module test;

        get_size(s : const unsigned byte[]) : unsigned int { return s.size; }
        first(s : const unsigned byte[]) : unsigned byte { return s[0]; }

        test_size() : unsigned int { return get_size(u8"hello"); }
        test_first() : unsigned byte { return first(u8"ABC"); }
    )SRC");
    REQUIRE(jit);
    // "hello" UTF-8 is 5 bytes + null terminator = 6
    REQUIRE(jit->lookup_symbol<unsigned(*)()>("test_size")() == 6);
    REQUIRE(jit->lookup_symbol<unsigned char(*)()>("test_first")() == 'A');
}

TEST_CASE("UTF-16 prefixed string literal — element type unsigned short", "[gen][literal][string][prefix]") {
    auto jit = gen_jit(R"SRC(
        module test;

        get_size(s : const unsigned short[]) : unsigned int { return s.size; }
        first(s : const unsigned short[]) : unsigned short { return s[0]; }

        test_size() : unsigned int { return get_size(u16"hi"); }
        test_first() : unsigned short { return first(u"ABC"); }
    )SRC");
    REQUIRE(jit);
    // "hi" UTF-16 is 2 code units + null terminator = 3
    REQUIRE(jit->lookup_symbol<unsigned(*)()>("test_size")() == 3);
    REQUIRE(jit->lookup_symbol<unsigned short(*)()>("test_first")() == 'A');
}

TEST_CASE("UTF-32 prefixed string literal — element type char", "[gen][literal][string][prefix]") {
    auto jit = gen_jit(R"SRC(
        module test;

        get_size(s : const char[]) : unsigned int { return s.size; }
        first(s : const char[]) : char { return s[0]; }

        test_size() : unsigned int { return get_size(U"hi"); }
        test_first() : char { return first(u32"ABC"); }
    )SRC");
    REQUIRE(jit);
    // "hi" UTF-32 is 2 code units + null terminator = 3
    REQUIRE(jit->lookup_symbol<unsigned(*)()>("test_size")() == 3);
    REQUIRE(jit->lookup_symbol<char(*)()>("test_first")() == 'A');
}

TEST_CASE("Prefixed character literals — element types", "[gen][literal][char][prefix]") {
    auto jit = gen_jit(R"SRC(
        module test;

        u8_char()  : unsigned byte  { return u8'A'; }
        u16_char() : unsigned short { return u'A'; }
        u32_char() : char           { return U'A'; }
    )SRC");
    REQUIRE(jit);
    REQUIRE(jit->lookup_symbol<unsigned char(*)()>("u8_char")() == 'A');
    REQUIRE(jit->lookup_symbol<unsigned short(*)()>("u16_char")() == 'A');
    REQUIRE(jit->lookup_symbol<char(*)()>("u32_char")() == 'A');
}


TEST_CASE("String literal — passed to const char[] parameter, read size", "[gen][literal][string]") {
    auto jit = gen_jit(R"SRC(
        module test;

        get_size(s : const char[]) : unsigned int {
            return s.size;
        }

        test() : unsigned int {
            return get_size("hello");
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<unsigned(*)()>("test");
    REQUIRE(fn != nullptr);
    // "hello" + '\0' = 6 bytes in the char[] (size field stores total including \0)
    REQUIRE(fn() == 6);
}

TEST_CASE("String literal — subscript access", "[gen][literal][string]") {
    auto jit = gen_jit(R"SRC(
        module test;

        first_char(s : const char[]) : char {
            return s[0];
        }

        test() : char {
            return first_char("abc");
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<char(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 'a');
}

TEST_CASE("String literal — empty string", "[gen][literal][string]") {
    auto jit = gen_jit(R"SRC(
        module test;

        get_size(s : const char[]) : unsigned int {
            return s.size;
        }

        test() : unsigned int {
            return get_size("");
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<unsigned(*)()>("test");
    REQUIRE(fn != nullptr);
    // "" + '\0' = 1 byte
    REQUIRE(fn() == 1);
}

TEST_CASE("String literal — deduplication (same string used twice)", "[gen][literal][string]") {
    auto jit = gen_jit(R"SRC(
        module test;

        first_char(s : const char[]) : char {
            return s[0];
        }

        test() : int {
            a : char = first_char("dup");
            b : char = first_char("dup");
            if (a == b) return 1;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 1);
}

TEST_CASE("String literal — null terminator present", "[gen][literal][string]") {
    auto jit = gen_jit(R"SRC(
        module test;

        get_size(s : const char[]) : unsigned int {
            return s.size;
        }

        last_char(s : const char[], sz : unsigned int) : char {
            return s[sz - 1u];
        }

        test() : bool {
            sz : unsigned int = get_size("AB");
            // size should be 3 (A, B, \0)
            // last char (index 2) should be the null terminator (value 0)
            c : char = last_char("AB", sz);
            // Compare with integer 0 cast to char (avoid '\0' which needs escape decoding)
            zero : char = 0;
            return c == zero;
        }
    )SRC", false, false);
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<bool(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == true);
}

