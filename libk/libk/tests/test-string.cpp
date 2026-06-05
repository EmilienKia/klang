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
            return s.at(0u);
        }

        test_at1() : char {
            sz : unsigned int = 4u;
            buf : char[]! = new char[sz];
            buf[0] = 'A'; buf[1] = 'B'; buf[2] = 'C'; buf[3] = '\0';
            s : k::String(buf, 3u);
            return s.at(1u);
        }

        test_at2() : char {
            sz : unsigned int = 4u;
            buf : char[]! = new char[sz];
            buf[0] = 'A'; buf[1] = 'B'; buf[2] = 'C'; buf[3] = '\0';
            s : k::String(buf, 3u);
            return s.at(2u);
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
// 5. StringBuilder — appendChar and size
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("StringBuilder appendChar and size", "[libk][string][builder]") {
    auto jit = jit_k(R"SRC(
        module __sb_appendChar__;

        test_append_size() : unsigned int {
            sb : k::StringBuilder;
            sb.appendChar('A');
            sb.appendChar('B');
            sb.appendChar('C');
            return sb.size();
        }

        test_append_content() : int {
            sb : k::StringBuilder;
            sb.appendChar('X');
            sb.appendChar('Y');
            if (sb.charAt(0) != 'X') return 1;
            if (sb.charAt(1) != 'Y') return 2;
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
            sb.appendChar('A');
            sb.appendChar('B');
            sb.clear();
            return sb.size();
        }

        test_clear_empty() : int {
            sb : k::StringBuilder;
            sb.appendChar('X');
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

// ═════════════════════════════════════════════════════════════════════════════
// 8. StringBuilder — drain constructor from StringBuilder
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("StringBuilder drain constructor — content", "[libk][string][builder][drain]") {
    auto jit = jit_k(R"SRC(
        module __sb_drain_content__;

        test_size() : unsigned int {
            src : k::StringBuilder;
            src.appendChar('A');
            src.appendChar('B');
            src.appendChar('C');
            dst : k::StringBuilder(#src);
            return dst.size();
        }

        test_char0() : char {
            src : k::StringBuilder;
            src.appendChar('A');
            src.appendChar('B');
            src.appendChar('C');
            dst : k::StringBuilder(#src);
            return dst.charAt(0);
        }

        test_char2() : char {
            src : k::StringBuilder;
            src.appendChar('A');
            src.appendChar('B');
            src.appendChar('C');
            dst : k::StringBuilder(#src);
            return dst.charAt(2);
        }
    )SRC");
    REQUIRE(jit);

    auto test_size = jit->lookup_symbol<unsigned(*)()>("test_size");
    REQUIRE(test_size);
    CHECK(test_size() == 3);

    auto test_char0 = jit->lookup_symbol<char(*)()>("test_char0");
    REQUIRE(test_char0);
    CHECK(test_char0() == 'A');

    auto test_char2 = jit->lookup_symbol<char(*)()>("test_char2");
    REQUIRE(test_char2);
    CHECK(test_char2() == 'C');
}

TEST_CASE("StringBuilder drain constructor — source is drained", "[libk][string][builder][drain]") {
    auto jit = jit_k(R"SRC(
        module __sb_drain_source__;

        test_src_empty_after_drain() : int {
            src : k::StringBuilder;
            src.appendChar('X');
            src.appendChar('Y');
            dst : k::StringBuilder(#src);
            // After drain, source should be empty
            if (src.empty()) return 1;
            return 0;
        }

        test_src_size_after_drain() : unsigned int {
            src : k::StringBuilder;
            src.appendChar('X');
            src.appendChar('Y');
            dst : k::StringBuilder(#src);
            return src.size();
        }
    )SRC");
    REQUIRE(jit);

    auto test_src_empty = jit->lookup_symbol<int(*)()>("test_src_empty_after_drain");
    REQUIRE(test_src_empty);
    CHECK(test_src_empty() == 1);

    auto test_src_size = jit->lookup_symbol<unsigned(*)()>("test_src_size_after_drain");
    REQUIRE(test_src_size);
    CHECK(test_src_size() == 0);
}

TEST_CASE("StringBuilder drain constructor from empty source", "[libk][string][builder][drain]") {
    auto jit = jit_k(R"SRC(
        module __sb_drain_empty__;

        test_empty() : int {
            src : k::StringBuilder;
            dst : k::StringBuilder(#src);
            if (dst.empty()) return 1;
            return 0;
        }

        test_size() : unsigned int {
            src : k::StringBuilder;
            dst : k::StringBuilder(#src);
            return dst.size();
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

TEST_CASE("StringBuilder drain then reuse source", "[libk][string][builder][drain]") {
    auto jit = jit_k(R"SRC(
        module __sb_drain_reuse__;

        test_reuse_after_drain() : unsigned int {
            src : k::StringBuilder;
            src.appendChar('A');
            src.appendChar('B');
            dst : k::StringBuilder(#src);
            // Source is now empty; reuse it
            src.appendChar('X');
            src.appendChar('Y');
            src.appendChar('Z');
            return src.size();
        }

        test_reuse_content() : char {
            src : k::StringBuilder;
            src.appendChar('A');
            src.appendChar('B');
            dst : k::StringBuilder(#src);
            // Source is now empty; reuse it
            src.appendChar('Q');
            return src.charAt(0);
        }
    )SRC");
    REQUIRE(jit);

    auto test_reuse = jit->lookup_symbol<unsigned(*)()>("test_reuse_after_drain");
    REQUIRE(test_reuse);
    CHECK(test_reuse() == 3);

    auto test_content = jit->lookup_symbol<char(*)()>("test_reuse_content");
    REQUIRE(test_content);
    CHECK(test_content() == 'Q');
}


// ═════════════════════════════════════════════════════════════════════════════
// 9. StringBuilder — copy constructor
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("StringBuilder copy constructor — size and content", "[libk][string][builder][copy]") {
    auto jit = jit_k(R"SRC(
        module __sb_copy_ctor__;

        test_copy_size() : unsigned int {
            src : k::StringBuilder("hello");
            dst : k::StringBuilder(src);
            return dst.size();
        }

        test_copy_content() : int {
            src : k::StringBuilder("hello");
            dst : k::StringBuilder(src);
            if (dst.charAt(0u) != 'h') return 1;
            if (dst.charAt(4u) != 'o') return 2;
            return 0;
        }

        test_copy_equality() : int {
            src : k::StringBuilder("hello");
            dst : k::StringBuilder(src);
            if (src == dst) return 1;
            return 0;
        }

        test_copy_independence() : int {
            src : k::StringBuilder("hello");
            dst : k::StringBuilder(src);
            src.append(" world");
            // dst should still be "hello"
            if (dst.size() == 5u) return 1;
            return 0;
        }

        test_copy_empty() : int {
            src : k::StringBuilder;
            dst : k::StringBuilder(src);
            if (dst.empty()) return 1;
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

    auto test_copy_independence = jit->lookup_symbol<int(*)()>("test_copy_independence");
    REQUIRE(test_copy_independence);
    CHECK(test_copy_independence() == 1);

    auto test_copy_empty = jit->lookup_symbol<int(*)()>("test_copy_empty");
    REQUIRE(test_copy_empty);
    CHECK(test_copy_empty() == 1);
}


// ═════════════════════════════════════════════════════════════════════════════
// 10. StringBuilder — append(String)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("StringBuilder append(String)", "[libk][string][builder]") {
    auto jit = jit_k(R"SRC(
        module __sb_append_str__;

        test_append_str_size() : unsigned int {
            sb : k::StringBuilder("hello");
            s : k::String(" world");
            sb.append(s);
            return sb.size();
        }

        test_append_str_content() : int {
            sb : k::StringBuilder("hello");
            s : k::String(" world");
            sb.append(s);
            result : k::String(sb);
            expected : k::String("hello world");
            if (result == expected) return 1;
            return 0;
        }

        test_append_empty_str() : int {
            sb : k::StringBuilder("hello");
            s : k::String;
            sb.append(s);
            if (sb.size() == 5u) return 1;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test_append_str_size = jit->lookup_symbol<unsigned(*)()>("test_append_str_size");
    REQUIRE(test_append_str_size);
    CHECK(test_append_str_size() == 11);

    auto test_append_str_content = jit->lookup_symbol<int(*)()>("test_append_str_content");
    REQUIRE(test_append_str_content);
    CHECK(test_append_str_content() == 1);

    auto test_append_empty_str = jit->lookup_symbol<int(*)()>("test_append_empty_str");
    REQUIRE(test_append_empty_str);
    CHECK(test_append_empty_str() == 1);
}


// ═════════════════════════════════════════════════════════════════════════════
// 11. String — construction from multi-fragment StringBuilder
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("String from multi-fragment StringBuilder", "[libk][string][builder]") {
    auto jit = jit_k(R"SRC(
        module __str_from_multifrag_sb__;

        test_size() : unsigned int {
            sb : k::StringBuilder("hello");
            sb.append(" ");
            sb.append("beautiful");
            sb.append(" ");
            sb.append("world");
            s : k::String(sb);
            return s.size();
        }

        test_content() : int {
            sb : k::StringBuilder("hello");
            sb.append(" ");
            sb.append("beautiful");
            sb.append(" ");
            sb.append("world");
            s : k::String(sb);
            expected : k::String("hello beautiful world");
            if (s == expected) return 1;
            return 0;
        }

        test_at_boundary() : int {
            sb : k::StringBuilder("AB");
            sb.append("CD");
            sb.append("EF");
            s : k::String(sb);
            if (s.at(0u) != 'A') return 1;
            if (s.at(1u) != 'B') return 2;
            if (s.at(2u) != 'C') return 3;
            if (s.at(3u) != 'D') return 4;
            if (s.at(4u) != 'E') return 5;
            if (s.at(5u) != 'F') return 6;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test_size = jit->lookup_symbol<unsigned(*)()>("test_size");
    REQUIRE(test_size);
    CHECK(test_size() == 21);

    auto test_content = jit->lookup_symbol<int(*)()>("test_content");
    REQUIRE(test_content);
    CHECK(test_content() == 1);

    auto test_at_boundary = jit->lookup_symbol<int(*)()>("test_at_boundary");
    REQUIRE(test_at_boundary);
    CHECK(test_at_boundary() == 0);
}




// ═════════════════════════════════════════════════════════════════════════════
// String / StringBuilder — encoding conversions (toUtf8 / toUtf16 / toUtf32)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("String encoding conversions — ASCII", "[libk][string][encoding]") {
    auto jit = jit_k(R"SRC(
        module __str_enc_ascii__;

        utf8_size()  : unsigned int   { s : k::String("AB"); a : unsigned byte[]!  = s.toUtf8();  return a.size; }
        utf8_b0()    : unsigned byte  { s : k::String("AB"); a : unsigned byte[]!  = s.toUtf8();  return a[0]; }
        utf16_size() : unsigned int   { s : k::String("AB"); a : unsigned short[]! = s.toUtf16(); return a.size; }
        utf16_u1()   : unsigned short { s : k::String("AB"); a : unsigned short[]! = s.toUtf16(); return a[1]; }
        utf32_size() : unsigned int   { s : k::String("AB"); a : char[]!           = s.toUtf32(); return a.size; }
    )SRC");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<unsigned(*)()>("utf8_size")() == 3);          // A,B,\0
    CHECK(jit->lookup_symbol<unsigned char(*)()>("utf8_b0")() == 'A');
    CHECK(jit->lookup_symbol<unsigned(*)()>("utf16_size")() == 3);
    CHECK(jit->lookup_symbol<unsigned short(*)()>("utf16_u1")() == 'B');
    CHECK(jit->lookup_symbol<unsigned(*)()>("utf32_size")() == 3);
}

TEST_CASE("String encoding conversions — non-ASCII U+00E9", "[libk][string][encoding]") {
    auto jit = jit_k(R"SRC(
        module __str_enc_nonascii__;

        // '\u00E9' (é) is a single code point requiring 2 UTF-8 bytes.
        len()       : unsigned int   { s : k::String("\u00E9"); return s.size(); }
        utf8_size() : unsigned int   { s : k::String("\u00E9"); a : unsigned byte[]!  = s.toUtf8();  return a.size; }
        utf8_b0()   : unsigned byte  { s : k::String("\u00E9"); a : unsigned byte[]!  = s.toUtf8();  return a[0]; }
        utf8_b1()   : unsigned byte  { s : k::String("\u00E9"); a : unsigned byte[]!  = s.toUtf8();  return a[1]; }
        utf16_u0()  : unsigned short { s : k::String("\u00E9"); a : unsigned short[]! = s.toUtf16(); return a[0]; }
        utf32_c0()  : unsigned int   { s : k::String("\u00E9"); a : char[]!           = s.toUtf32(); return a[0]; }
    )SRC");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<unsigned(*)()>("len")() == 1);
    CHECK(jit->lookup_symbol<unsigned(*)()>("utf8_size")() == 3);          // 2 bytes + \0
    CHECK(jit->lookup_symbol<unsigned char(*)()>("utf8_b0")() == 0xC3);
    CHECK(jit->lookup_symbol<unsigned char(*)()>("utf8_b1")() == 0xA9);
    CHECK(jit->lookup_symbol<unsigned short(*)()>("utf16_u0")() == 0xE9);
    CHECK(jit->lookup_symbol<unsigned(*)()>("utf32_c0")() == 0xE9);
}

TEST_CASE("StringBuilder encoding conversions", "[libk][string][stringbuilder][encoding]") {
    auto jit = jit_k(R"SRC(
        module __sb_enc__;

        utf8_size() : unsigned int {
            sb : k::StringBuilder("Hi");
            sb.append("!");
            a : unsigned byte[]! = sb.toUtf8();
            return a.size;
        }
        utf32_c0() : unsigned int {
            sb : k::StringBuilder("Hi");
            a : char[]! = sb.toUtf32();
            return a[0];
        }
    )SRC");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<unsigned(*)()>("utf8_size")() == 4);          // H,i,!,\0
    CHECK(jit->lookup_symbol<unsigned(*)()>("utf32_c0")() == 'H');
}


// ═════════════════════════════════════════════════════════════════════════════
// String / StringBuilder — multi-encoding construction & mixed fragments
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("String from UTF-8 source", "[libk][string][encoding]") {
    auto jit = jit_k(R"SRC(
        module __str_from_u8__;

        size_ascii()    : unsigned int { s : k::String(u8"AB"); return s.size(); }
        at0_ascii()     : unsigned int { s : k::String(u8"AB"); return s.at(0u); }
        size_nonascii() : unsigned int { s : k::String(u8"\u00E9"); return s.size(); }
        cp_nonascii()   : unsigned int { s : k::String(u8"\u00E9"); return s.at(0u); }
    )SRC");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<unsigned(*)()>("size_ascii")() == 2);
    CHECK(jit->lookup_symbol<unsigned(*)()>("at0_ascii")() == 'A');
    CHECK(jit->lookup_symbol<unsigned(*)()>("size_nonascii")() == 1);
    CHECK(jit->lookup_symbol<unsigned(*)()>("cp_nonascii")() == 0xE9);
}

TEST_CASE("String from UTF-16 source", "[libk][string][encoding]") {
    auto jit = jit_k(R"SRC(
        module __str_from_u16__;

        size_ascii()  : unsigned int { s : k::String(u"AB"); return s.size(); }
        cp_nonascii() : unsigned int { s : k::String(u"\u20AC"); return s.at(0u); }   // €
    )SRC");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<unsigned(*)()>("size_ascii")() == 2);
    CHECK(jit->lookup_symbol<unsigned(*)()>("cp_nonascii")() == 0x20AC);
}

TEST_CASE("StringBuilder mixed-encoding fragments", "[libk][string][stringbuilder][encoding]") {
    auto jit = jit_k(R"SRC(
        module __sb_mixed__;

        // Append fragments stored in three different source encodings.
        frag_size() : unsigned int {
            sb : k::StringBuilder("A");   // char[]  (UTF-32)
            sb.append(u8"B");             // unsigned byte[]  (UTF-8)
            sb.append(u"C");              // unsigned short[] (UTF-16)
            return sb.size();
        }
        frag_third() : unsigned int {
            sb : k::StringBuilder("A");
            sb.append(u8"B");
            sb.append(u"C");
            return sb.charAt(2u);
        }
        frag_nonascii() : unsigned int {
            sb : k::StringBuilder("x");
            sb.append(u8"\u00E9");        // 'é' as UTF-8
            return sb.charAt(1u);
        }
    )SRC");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<unsigned(*)()>("frag_size")() == 3);
    CHECK(jit->lookup_symbol<unsigned(*)()>("frag_third")() == 'C');
    CHECK(jit->lookup_symbol<unsigned(*)()>("frag_nonascii")() == 0xE9);
}

TEST_CASE("String UTF-8 round-trip", "[libk][string][encoding]") {
    auto jit = jit_k(R"SRC(
        module __str_roundtrip__;

        test() : int {
            orig : k::String("\u00E9");
            bytes : unsigned byte[]! = orig.toUtf8();
            back : k::String(bytes);
            if (back.size() != orig.size()) return 1;
            if (back.at(0u) != orig.at(0u)) return 2;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<int(*)()>("test")() == 0);
}
