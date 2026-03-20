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
 * types.  They self-contain the class definitions inline (same as the real
 * string.k) so they run via JIT without needing to import the stdlib module.
 *
 * NOTE: Some tests are disabled because they depend on compiler features
 * that are not yet implemented (subscript on owner array member through
 * method / this, unsized array parameter, …).  The corresponding compiler
 * bugs are tracked separately in the klang compiler test suite.
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"


// ═════════════════════════════════════════════════════════════════════════════
// 1. String — default construction (empty)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("String default construction — empty", "[libk][string]") {
    auto jit = gen_jit(R"SRC(
        module __str_default__;

        const final class String {
            _buf  : char[]!;
            _size : int;
        public:
            String() : _buf(null), _size(0) {}
            ~String() { delete _buf; }
            const size() : int { return _size; }
            const empty() : bool { return _size == 0; }
        }

        test_size() : int {
            s : String;
            return s.size();
        }

        test_empty() : int {
            s : String;
            if (s.empty()) return 1;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto test_size = jit->lookup_symbol<int(*)()>("test_size");
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
    auto jit = gen_jit(R"SRC(
        module __str_from_buf__;

        const final class String {
            _buf  : char[]!;
            _size : int;
        public:
            String() : _buf(null), _size(0) {}
            String(buf : char[]!, sz : int) : _buf(buf), _size(sz) {}
            ~String() { delete _buf; }
            const size() : int { return _size; }
            const empty() : bool { return _size == 0; }
        }

        test_size() : int {
            sz : unsigned int = 6u;
            buf : char[]! = new char[sz];
            buf[0] = 'H'; buf[1] = 'e'; buf[2] = 'l';
            buf[3] = 'l'; buf[4] = 'o'; buf[5] = '\0';
            s : String(buf, 5);
            return s.size();
        }

        test_not_empty() : int {
            sz : unsigned int = 6u;
            buf : char[]! = new char[sz];
            buf[0] = 'H'; buf[1] = 'e'; buf[2] = 'l';
            buf[3] = 'l'; buf[4] = 'o'; buf[5] = '\0';
            s : String(buf, 5);
            if (s.empty()) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(jit);

    auto test_size = jit->lookup_symbol<int(*)()>("test_size");
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
    auto jit = gen_jit(R"SRC(
        module __sb_default__;

        class StringBuilder {
            _buf      : char[]!;
            _size     : int;
            _capacity : int;
        public:
            StringBuilder() : _buf(null), _size(0), _capacity(0) {}
            const size() : int { return _size; }
            const empty() : bool { return _size == 0; }
        }

        test_empty() : int {
            sb : StringBuilder;
            if (sb.empty()) return 1;
            return 0;
        }

        test_size() : int {
            sb : StringBuilder;
            return sb.size();
        }
    )SRC");
    REQUIRE(jit);

    auto test_empty = jit->lookup_symbol<int(*)()>("test_empty");
    REQUIRE(test_empty);
    CHECK(test_empty() == 1);

    auto test_size = jit->lookup_symbol<int(*)()>("test_size");
    REQUIRE(test_size);
    CHECK(test_size() == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// 4. String.at() — character access
//    DISABLED: depends on subscript on owner array member via method
//    (this._buf[i]) which is a known compiler bug (SIGSEGV).
//    Re-enable once the compiler bug is fixed.
// ═════════════════════════════════════════════════════════════════════════════

// TEST_CASE("String.at() — character access", "[libk][string]") { ... }

// ═════════════════════════════════════════════════════════════════════════════
// 5. StringBuilder — append_char, clear, char_at
//    DISABLED: depends on subscript on owner array member via method.
// ═════════════════════════════════════════════════════════════════════════════

// TEST_CASE("StringBuilder append_char and size", "[libk][string][builder]") { ... }
// TEST_CASE("StringBuilder clear resets to empty", "[libk][string][builder]") { ... }
// TEST_CASE("String from StringBuilder round-trip", "[libk][string][integration]") { ... }

