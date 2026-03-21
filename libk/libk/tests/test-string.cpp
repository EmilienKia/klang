/*
 * K Language standard library — String / StringBuilder tests
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
 * Tests for ::k::String and ::k::StringBuilder.
 *
 * These tests exercise the behaviour of the libk String and StringBuilder
 * types by JIT-compiling small K programs that use the stdlib types.
 *
 * The base standard library (module "k") is implicitly imported by the
 * compiler — no explicit "import k;" is needed in the K sources.
 *
 * The test executable links against libk.so (loaded via dlopen at test
 * startup) and uses the k.kdi descriptor for import resolution.
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

// Compile-time paths injected by CMake (see libk/libk/CMakeLists.txt).
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
// 1. String — default construction (empty)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("String default construction — empty", "[libk][string]") {
    auto jit = jit_k(R"SRC(
        module __str_default__;

        test_size() : unsigned int {
            s : k::String;
            return s.size();
        }

        test_empty() : int {
            s : k::String;
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

// ═════════════════════════════════════════════════════════════════════════════
// 2. String — construction from owner buffer + size
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("String from owner buffer — size and empty", "[libk][string]") {
    auto jit = jit_k(R"SRC(
        module __str_from_buf__;

        test_size() : unsigned int {
            sz : unsigned int = 6u;
            buf : char[]! = new char[sz];
            buf[0] = 'H'; buf[1] = 'e'; buf[2] = 'l';
            buf[3] = 'l'; buf[4] = 'o'; buf[5] = '\0';
            s : k::String(buf, 5u);
            return s.size();
        }

        test_not_empty() : int {
            sz : unsigned int = 6u;
            buf : char[]! = new char[sz];
            buf[0] = 'H'; buf[1] = 'e'; buf[2] = 'l';
            buf[3] = 'l'; buf[4] = 'o'; buf[5] = '\0';
            s : k::String(buf, 5u);
            if (s.empty()) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(jit);

    auto test_size = jit->lookup_symbol<unsigned(*)()>("test_size");
    REQUIRE(test_size);
    CHECK(test_size() == 5);

    auto test_not_empty = jit->lookup_symbol<int(*)()>("test_not_empty");
    REQUIRE(test_not_empty);
    CHECK(test_not_empty() == 1);
}

// ═════════════════════════════════════════════════════════════════════════════
// 3. StringBuilder — default construction
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("StringBuilder default construction", "[libk][string][builder]") {
    auto jit = jit_k(R"SRC(
        module __sb_default__;

        test_empty() : int {
            sb : k::StringBuilder;
            if (sb.empty()) return 1;
            return 0;
        }

        test_size() : unsigned int {
            sb : k::StringBuilder;
            return sb.size();
        }
    )SRC");
    REQUIRE(jit);

    auto test_empty = jit->lookup_symbol<int(*)()>("test_empty");
    REQUIRE(test_empty);
    CHECK(test_empty() == 1);

    auto test_size = jit->lookup_symbol<unsigned(*)()>("test_size");
    REQUIRE(test_size);
    CHECK(test_size() == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// 4. String.at() — character access
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("String.at() — character access", "[libk][string]") {
    auto jit = jit_k(R"SRC(
        module __str_at__;

        test_at0() : char {
            sz : unsigned int = 4u;
            buf : char[]! = new char[sz];
            buf[0] = 'A'; buf[1] = 'B'; buf[2] = 'C'; buf[3] = '\0';
            s : k::String(buf, 3u);
            return s.at(0);
        }

        test_at1() : char {
            sz : unsigned int = 4u;
            buf : char[]! = new char[sz];
            buf[0] = 'A'; buf[1] = 'B'; buf[2] = 'C'; buf[3] = '\0';
            s : k::String(buf, 3u);
            return s.at(1);
        }

        test_at2() : char {
            sz : unsigned int = 4u;
            buf : char[]! = new char[sz];
            buf[0] = 'A'; buf[1] = 'B'; buf[2] = 'C'; buf[3] = '\0';
            s : k::String(buf, 3u);
            return s.at(2);
        }
    )SRC");
    REQUIRE(jit);

    auto test_at0 = jit->lookup_symbol<char(*)()>("test_at0");
    REQUIRE(test_at0);
    CHECK(test_at0() == 'A');

    auto test_at1 = jit->lookup_symbol<char(*)()>("test_at1");
    REQUIRE(test_at1);
    CHECK(test_at1() == 'B');

    auto test_at2 = jit->lookup_symbol<char(*)()>("test_at2");
    REQUIRE(test_at2);
    CHECK(test_at2() == 'C');
}

// ═════════════════════════════════════════════════════════════════════════════
// 5. StringBuilder — append_char and size
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("StringBuilder append_char and size", "[libk][string][builder]") {
    auto jit = jit_k(R"SRC(
        module __sb_append_char__;

        test_append_size() : unsigned int {
            sb : k::StringBuilder;
            sb.append_char('A');
            sb.append_char('B');
            sb.append_char('C');
            return sb.size();
        }

        test_append_content() : int {
            sb : k::StringBuilder;
            sb.append_char('X');
            sb.append_char('Y');
            if (sb.char_at(0) != 'X') return 1;
            if (sb.char_at(1) != 'Y') return 2;
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
}

// ═════════════════════════════════════════════════════════════════════════════
// 6. StringBuilder — clear resets to empty
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("StringBuilder clear resets to empty", "[libk][string][builder]") {
    auto jit = jit_k(R"SRC(
        module __sb_clear__;

        test_clear_size() : unsigned int {
            sb : k::StringBuilder;
            sb.append_char('A');
            sb.append_char('B');
            sb.clear();
            return sb.size();
        }

        test_clear_empty() : int {
            sb : k::StringBuilder;
            sb.append_char('X');
            sb.clear();
            if (sb.empty()) return 1;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test_clear_size = jit->lookup_symbol<unsigned(*)()>("test_clear_size");
    REQUIRE(test_clear_size);
    CHECK(test_clear_size() == 0);

    auto test_clear_empty = jit->lookup_symbol<int(*)()>("test_clear_empty");
    REQUIRE(test_clear_empty);
    CHECK(test_clear_empty() == 1);
}

// ═════════════════════════════════════════════════════════════════════════════
// 7. String equality operator
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("String equality operator", "[libk][string]") {
    auto jit = jit_k(R"SRC(
        module __str_eq__;

        test_equal() : int {
            sz : unsigned int = 3u;
            buf1 : char[]! = new char[sz];
            buf1[0] = 'A'; buf1[1] = 'B'; buf1[2] = '\0';
            s1 : k::String(buf1, 2u);

            buf2 : char[]! = new char[sz];
            buf2[0] = 'A'; buf2[1] = 'B'; buf2[2] = '\0';
            s2 : k::String(buf2, 2u);

            if (s1 == s2) return 1;
            return 0;
        }

        test_not_equal() : int {
            sz : unsigned int = 3u;
            buf1 : char[]! = new char[sz];
            buf1[0] = 'A'; buf1[1] = 'B'; buf1[2] = '\0';
            s1 : k::String(buf1, 2u);

            buf2 : char[]! = new char[sz];
            buf2[0] = 'X'; buf2[1] = 'Y'; buf2[2] = '\0';
            s2 : k::String(buf2, 2u);

            if (s1 != s2) return 1;
            return 0;
        }

        test_different_sizes() : int {
            sz1 : unsigned int = 3u;
            buf1 : char[]! = new char[sz1];
            buf1[0] = 'A'; buf1[1] = 'B'; buf1[2] = '\0';
            s1 : k::String(buf1, 2u);

            sz2 : unsigned int = 4u;
            buf2 : char[]! = new char[sz2];
            buf2[0] = 'A'; buf2[1] = 'B'; buf2[2] = 'C'; buf2[3] = '\0';
            s2 : k::String(buf2, 3u);

            if (s1 == s2) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(jit);

    auto test_equal = jit->lookup_symbol<int(*)()>("test_equal");
    REQUIRE(test_equal);
    CHECK(test_equal() == 1);

    auto test_not_equal = jit->lookup_symbol<int(*)()>("test_not_equal");
    REQUIRE(test_not_equal);
    CHECK(test_not_equal() == 1);

    auto test_different_sizes = jit->lookup_symbol<int(*)()>("test_different_sizes");
    REQUIRE(test_different_sizes);
    CHECK(test_different_sizes() == 1);
}
