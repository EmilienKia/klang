/*
 * K Language standard library — I/O PrintStream tests
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
 * Tests for k::io::PrintStream.
 *
 * Verifies textual output of all primitive-type print/println overloads,
 * fluent chaining, and flush/close delegation.
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
std::unique_ptr<k::model::gen::jit> jit_k(std::string_view src) {
    return gen_jit_with_stdlib(src, LIBK_KDI_DIR, LIBK_LIB_DIR);
}
} // anonymous namespace
// ═════════════════════════════════════════════════════════════════════════════
// print(bool)
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("PrintStream print(bool)", "[libk][io][print]") {
    auto jit = jit_k(R"SRC(
        module __ps_bool__;
        test_print_bool() : int {
            baos : k::io::ByteArrayOutputStream;
            ps : k::io::PrintStream(&baos);
            ps.print(true);
            ps.print(false);
            if (baos.size() != 9) return 1;
            arr : byte[]* = baos.toByteArray();
            if (arr[0] != (byte) 't') return 2;
            if (arr[1] != (byte) 'r') return 3;
            if (arr[2] != (byte) 'u') return 4;
            if (arr[3] != (byte) 'e') return 5;
            if (arr[4] != (byte) 'f') return 6;
            if (arr[5] != (byte) 'a') return 7;
            if (arr[6] != (byte) 'l') return 8;
            if (arr[7] != (byte) 's') return 9;
            if (arr[8] != (byte) 'e') return 10;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_print_bool");
    REQUIRE(fn);
    CHECK(fn() == 0);
}
// ═════════════════════════════════════════════════════════════════════════════
// print(char)
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("PrintStream print(char)", "[libk][io][print]") {
    auto jit = jit_k(R"SRC(
        module __ps_char__;
        test_print_char() : int {
            baos : k::io::ByteArrayOutputStream;
            ps : k::io::PrintStream(&baos);
            ps.print('A');
            ps.print('Z');
            if (baos.size() != 2) return 1;
            arr : byte[]* = baos.toByteArray();
            if (arr[0] != (byte) 65) return 2;
            if (arr[1] != (byte) 90) return 3;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_print_char");
    REQUIRE(fn);
    CHECK(fn() == 0);
}
// ═════════════════════════════════════════════════════════════════════════════
// print(int) — positive, negative, zero
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("PrintStream print(int)", "[libk][io][print]") {
    auto jit = jit_k(R"SRC(
        module __ps_int__;
        test_print_int() : int {
            baos : k::io::ByteArrayOutputStream;
            ps : k::io::PrintStream(&baos);
            ps.print(42);
            ps.print(-7);
            ps.print(0);
            // Expected: "42-70" = 5 bytes
            if (baos.size() != 5) return 1;
            arr : byte[]* = baos.toByteArray();
            if (arr[0] != (byte) '4') return 2;
            if (arr[1] != (byte) '2') return 3;
            if (arr[2] != (byte) '-') return 4;
            if (arr[3] != (byte) '7') return 5;
            if (arr[4] != (byte) '0') return 6;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_print_int");
    REQUIRE(fn);
    CHECK(fn() == 0);
}
// ═════════════════════════════════════════════════════════════════════════════
// print(long) — large value
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("PrintStream print(long)", "[libk][io][print]") {
    auto jit = jit_k(R"SRC(
        module __ps_long__;
        test_print_long() : int {
            baos : k::io::ByteArrayOutputStream;
            ps : k::io::PrintStream(&baos);
            ps.print(1234567890123L);
            // Expected: "1234567890123" = 13 bytes
            if (baos.size() != 13) return 1;
            arr : byte[]* = baos.toByteArray();
            if (arr[0] != (byte) '1') return 2;
            if (arr[4] != (byte) '5') return 3;
            if (arr[12] != (byte) '3') return 4;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_print_long");
    REQUIRE(fn);
    CHECK(fn() == 0);
}
// ═════════════════════════════════════════════════════════════════════════════
// print(float)
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("PrintStream print(float)", "[libk][io][print]") {
    auto jit = jit_k(R"SRC(
        module __ps_float__;
        test_print_float() : int {
            baos : k::io::ByteArrayOutputStream;
            ps : k::io::PrintStream(&baos);
            ps.print(2.5f);
            // %g of 2.5f -> "2.5" = 3 bytes
            if (baos.size() != 3) return 1;
            arr : byte[]* = baos.toByteArray();
            if (arr[0] != (byte) '2') return 2;
            if (arr[1] != (byte) '.') return 3;
            if (arr[2] != (byte) '5') return 4;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_print_float");
    REQUIRE(fn);
    CHECK(fn() == 0);
}
// ═════════════════════════════════════════════════════════════════════════════
// print(double)
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("PrintStream print(double)", "[libk][io][print]") {
    auto jit = jit_k(R"SRC(
        module __ps_double__;
        test_print_double() : int {
            baos : k::io::ByteArrayOutputStream;
            ps : k::io::PrintStream(&baos);
            ps.print(2.5);
            // %g of 2.5 -> "2.5" = 3 bytes
            if (baos.size() != 3) return 1;
            arr : byte[]* = baos.toByteArray();
            if (arr[0] != (byte) '2') return 2;
            if (arr[1] != (byte) '.') return 3;
            if (arr[2] != (byte) '5') return 4;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_print_double");
    REQUIRE(fn);
    CHECK(fn() == 0);
}
// ═════════════════════════════════════════════════════════════════════════════
// print(const char[]) — string literal
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("PrintStream print(const char[])", "[libk][io][print]") {
    auto jit = jit_k(R"SRC(
        module __ps_chararray__;
        test_print_str() : int {
            baos : k::io::ByteArrayOutputStream;
            ps : k::io::PrintStream(&baos);
            ps.print("Hello");
            // "Hello" literal has trailing '\0'; printed as 5 chars
            if (baos.size() != 5) return 1;
            arr : byte[]* = baos.toByteArray();
            if (arr[0] != (byte) 'H') return 2;
            if (arr[1] != (byte) 'e') return 3;
            if (arr[2] != (byte) 'l') return 4;
            if (arr[3] != (byte) 'l') return 5;
            if (arr[4] != (byte) 'o') return 6;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_print_str");
    REQUIRE(fn);
    CHECK(fn() == 0);
}
// ═════════════════════════════════════════════════════════════════════════════
// print with String — via toUtf32() returning char[]!, implicit cast to const char[]
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("PrintStream print(String.toUtf32())", "[libk][io][print]") {
    auto jit = jit_k(R"SRC(
        module __ps_string__;
        test_print_string() : int {
            baos : k::io::ByteArrayOutputStream;
            ps : k::io::PrintStream(&baos);
            s : k::String("World");
            ps.print(s.toUtf32());
            if (baos.size() != 5) return 1;
            arr : byte[]* = baos.toByteArray();
            if (arr[0] != (byte) 'W') return 2;
            if (arr[1] != (byte) 'o') return 3;
            if (arr[2] != (byte) 'r') return 4;
            if (arr[3] != (byte) 'l') return 5;
            if (arr[4] != (byte) 'd') return 6;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_print_string");
    REQUIRE(fn);
    CHECK(fn() == 0);
}
// ═════════════════════════════════════════════════════════════════════════════
// println() — bare newline
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("PrintStream println()", "[libk][io][print]") {
    auto jit = jit_k(R"SRC(
        module __ps_println_empty__;
        test_println_empty() : int {
            baos : k::io::ByteArrayOutputStream;
            ps : k::io::PrintStream(&baos);
            ps.println();
            if (baos.size() != 1) return 1;
            arr : byte[]* = baos.toByteArray();
            if (arr[0] != (byte) 10) return 2;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_println_empty");
    REQUIRE(fn);
    CHECK(fn() == 0);
}
// ═════════════════════════════════════════════════════════════════════════════
// println(int) — value followed by newline
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("PrintStream println(int)", "[libk][io][print]") {
    auto jit = jit_k(R"SRC(
        module __ps_println_int__;
        test_println_int() : int {
            baos : k::io::ByteArrayOutputStream;
            ps : k::io::PrintStream(&baos);
            ps.println(42);
            // "42\n" = 3 bytes
            if (baos.size() != 3) return 1;
            arr : byte[]* = baos.toByteArray();
            if (arr[0] != (byte) '4') return 2;
            if (arr[1] != (byte) '2') return 3;
            if (arr[2] != (byte) 10) return 4;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_println_int");
    REQUIRE(fn);
    CHECK(fn() == 0);
}
// ═════════════════════════════════════════════════════════════════════════════
// println(const char[]) — string literal followed by newline
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("PrintStream println(const char[])", "[libk][io][print]") {
    auto jit = jit_k(R"SRC(
        module __ps_println_str__;
        test_println_str() : int {
            baos : k::io::ByteArrayOutputStream;
            ps : k::io::PrintStream(&baos);
            ps.println("Hi");
            // "Hi\n" = 3 bytes
            if (baos.size() != 3) return 1;
            arr : byte[]* = baos.toByteArray();
            if (arr[0] != (byte) 'H') return 2;
            if (arr[1] != (byte) 'i') return 3;
            if (arr[2] != (byte) 10) return 4;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_println_str");
    REQUIRE(fn);
    CHECK(fn() == 0);
}
// ═════════════════════════════════════════════════════════════════════════════
// println(String.toUtf32()) — String via toUtf32(), followed by newline
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("PrintStream println(String.toUtf32())", "[libk][io][print]") {
    auto jit = jit_k(R"SRC(
        module __ps_println_string__;
        test_println_string() : int {
            baos : k::io::ByteArrayOutputStream;
            ps : k::io::PrintStream(&baos);
            s : k::String("Ok");
            ps.println(s.toUtf32());
            // "Ok\n" = 3 bytes
            if (baos.size() != 3) return 1;
            arr : byte[]* = baos.toByteArray();
            if (arr[0] != (byte) 'O') return 2;
            if (arr[1] != (byte) 'k') return 3;
            if (arr[2] != (byte) 10) return 4;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_println_string");
    REQUIRE(fn);
    CHECK(fn() == 0);
}
// ═════════════════════════════════════════════════════════════════════════════
// Fluent chaining — print().print().println()
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("PrintStream fluent chaining", "[libk][io][print]") {
    auto jit = jit_k(R"SRC(
        module __ps_fluent__;
        test_fluent() : int {
            baos : k::io::ByteArrayOutputStream;
            ps : k::io::PrintStream(&baos);
            ps.print(1).print(2).println(3);
            // "123\n" = 4 bytes
            if (baos.size() != 4) return 1;
            arr : byte[]* = baos.toByteArray();
            if (arr[0] != (byte) '1') return 2;
            if (arr[1] != (byte) '2') return 3;
            if (arr[2] != (byte) '3') return 4;
            if (arr[3] != (byte) 10) return 5;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_fluent");
    REQUIRE(fn);
    CHECK(fn() == 0);
}
// ═════════════════════════════════════════════════════════════════════════════
// Multi-type mixed output
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("PrintStream multi-type output", "[libk][io][print]") {
    auto jit = jit_k(R"SRC(
        module __ps_multi__;
        test_multi() : int {
            baos : k::io::ByteArrayOutputStream;
            ps : k::io::PrintStream(&baos);
            ps.print(true);       // "true"  4
            ps.print(' ');        // " "     1
            ps.print(42);         // "42"    2
            ps.print(' ');        // " "     1
            ps.print(2.5);        // "2.5"   3
            ps.println();         // "\n"    1
            // Total = 12
            if (baos.size() != 12) return baos.size();
            arr : byte[]* = baos.toByteArray();
            if (arr[0] != (byte) 't') return 100;
            if (arr[1] != (byte) 'r') return 101;
            if (arr[2] != (byte) 'u') return 102;
            if (arr[3] != (byte) 'e') return 103;
            if (arr[4] != (byte) ' ') return 104;
            if (arr[5] != (byte) '4') return 105;
            if (arr[6] != (byte) '2') return 106;
            if (arr[7] != (byte) ' ') return 107;
            if (arr[8] != (byte) '2') return 108;
            if (arr[9] != (byte) '.') return 109;
            if (arr[10] != (byte) '5') return 110;
            if (arr[11] != (byte) 10) return 111;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_multi");
    REQUIRE(fn);
    CHECK(fn() == 0);
}
// ═════════════════════════════════════════════════════════════════════════════
// flush() delegation
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("PrintStream flush delegates", "[libk][io][print]") {
    auto jit = jit_k(R"SRC(
        module __ps_flush__;
        test_flush() : int {
            baos : k::io::ByteArrayOutputStream;
            ps : k::io::PrintStream(&baos);
            ps.print(99);
            ps.flush();
            if (baos.size() != 2) return 1;
            arr : byte[]* = baos.toByteArray();
            if (arr[0] != (byte) '9') return 2;
            if (arr[1] != (byte) '9') return 3;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_flush");
    REQUIRE(fn);
    CHECK(fn() == 0);
}
// ═════════════════════════════════════════════════════════════════════════════
// print(byte)
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("PrintStream print(byte)", "[libk][io][print]") {
    auto jit = jit_k(R"SRC(
        module __ps_byte__;
        test_print_byte() : int {
            baos : k::io::ByteArrayOutputStream;
            ps : k::io::PrintStream(&baos);
            ps.print((byte) 100);
            // 100 as decimal = "100" = 3 bytes
            if (baos.size() != 3) return 1;
            arr : byte[]* = baos.toByteArray();
            if (arr[0] != (byte) '1') return 2;
            if (arr[1] != (byte) '0') return 3;
            if (arr[2] != (byte) '0') return 4;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_print_byte");
    REQUIRE(fn);
    CHECK(fn() == 0);
}
// ═════════════════════════════════════════════════════════════════════════════
// print(short)
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("PrintStream print(short)", "[libk][io][print]") {
    auto jit = jit_k(R"SRC(
        module __ps_short__;
        test_print_short() : int {
            baos : k::io::ByteArrayOutputStream;
            ps : k::io::PrintStream(&baos);
            ps.print((short) -1234);
            // "-1234" = 5 bytes
            if (baos.size() != 5) return 1;
            arr : byte[]* = baos.toByteArray();
            if (arr[0] != (byte) '-') return 2;
            if (arr[1] != (byte) '1') return 3;
            if (arr[2] != (byte) '2') return 4;
            if (arr[3] != (byte) '3') return 5;
            if (arr[4] != (byte) '4') return 6;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_print_short");
    REQUIRE(fn);
    CHECK(fn() == 0);
}
// ═════════════════════════════════════════════════════════════════════════════
// print(unsigned short)
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("PrintStream print(unsigned short)", "[libk][io][print]") {
    auto jit = jit_k(R"SRC(
        module __ps_ushort__;
        test_print_ushort() : int {
            baos : k::io::ByteArrayOutputStream;
            ps : k::io::PrintStream(&baos);
            ps.print((unsigned short) 65535);
            // "65535" = 5 bytes
            if (baos.size() != 5) return 1;
            arr : byte[]* = baos.toByteArray();
            if (arr[0] != (byte) '6') return 2;
            if (arr[4] != (byte) '5') return 3;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_print_ushort");
    REQUIRE(fn);
    CHECK(fn() == 0);
}
// ═════════════════════════════════════════════════════════════════════════════
// print(unsigned int)
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("PrintStream print(unsigned int)", "[libk][io][print]") {
    auto jit = jit_k(R"SRC(
        module __ps_uint__;
        test_print_uint() : int {
            baos : k::io::ByteArrayOutputStream;
            ps : k::io::PrintStream(&baos);
            ps.print(4000000000u);
            // "4000000000" = 10 bytes
            if (baos.size() != 10) return 1;
            arr : byte[]* = baos.toByteArray();
            if (arr[0] != (byte) '4') return 2;
            if (arr[9] != (byte) '0') return 3;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_print_uint");
    REQUIRE(fn);
    CHECK(fn() == 0);
}
// ═════════════════════════════════════════════════════════════════════════════
// print(unsigned long)
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("PrintStream print(unsigned long)", "[libk][io][print]") {
    auto jit = jit_k(R"SRC(
        module __ps_ulong__;
        test_print_ulong() : int {
            baos : k::io::ByteArrayOutputStream;
            ps : k::io::PrintStream(&baos);
            ps.print(9999999999uL);
            // "9999999999" = 10 bytes
            if (baos.size() != 10) return 1;
            arr : byte[]* = baos.toByteArray();
            if (arr[0] != (byte) '9') return 2;
            if (arr[9] != (byte) '9') return 3;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_print_ulong");
    REQUIRE(fn);
    CHECK(fn() == 0);
}
// ═════════════════════════════════════════════════════════════════════════════
// println(bool) — "true\n"
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("PrintStream println(bool)", "[libk][io][print]") {
    auto jit = jit_k(R"SRC(
        module __ps_println_bool__;
        test_println_bool() : int {
            baos : k::io::ByteArrayOutputStream;
            ps : k::io::PrintStream(&baos);
            ps.println(true);
            // "true\n" = 5 bytes
            if (baos.size() != 5) return 1;
            arr : byte[]* = baos.toByteArray();
            if (arr[0] != (byte) 't') return 2;
            if (arr[3] != (byte) 'e') return 3;
            if (arr[4] != (byte) 10) return 4;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_println_bool");
    REQUIRE(fn);
    CHECK(fn() == 0);
}
// ═════════════════════════════════════════════════════════════════════════════
// println(char) — "A\n"
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("PrintStream println(char)", "[libk][io][print]") {
    auto jit = jit_k(R"SRC(
        module __ps_println_char__;
        test_println_char() : int {
            baos : k::io::ByteArrayOutputStream;
            ps : k::io::PrintStream(&baos);
            ps.println('A');
            // "A\n" = 2 bytes
            if (baos.size() != 2) return 1;
            arr : byte[]* = baos.toByteArray();
            if (arr[0] != (byte) 'A') return 2;
            if (arr[1] != (byte) 10) return 3;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_println_char");
    REQUIRE(fn);
    CHECK(fn() == 0);
}
// ═════════════════════════════════════════════════════════════════════════════
// println(byte) — "42\n"
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("PrintStream println(byte)", "[libk][io][print]") {
    auto jit = jit_k(R"SRC(
        module __ps_println_byte__;
        test_println_byte() : int {
            baos : k::io::ByteArrayOutputStream;
            ps : k::io::PrintStream(&baos);
            ps.println((byte) 42);
            // "42\n" = 3 bytes
            if (baos.size() != 3) return 1;
            arr : byte[]* = baos.toByteArray();
            if (arr[0] != (byte) '4') return 2;
            if (arr[1] != (byte) '2') return 3;
            if (arr[2] != (byte) 10) return 4;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_println_byte");
    REQUIRE(fn);
    CHECK(fn() == 0);
}
// ═════════════════════════════════════════════════════════════════════════════
// println(short) — "-99\n"
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("PrintStream println(short)", "[libk][io][print]") {
    auto jit = jit_k(R"SRC(
        module __ps_println_short__;
        test_println_short() : int {
            baos : k::io::ByteArrayOutputStream;
            ps : k::io::PrintStream(&baos);
            ps.println((short) -99);
            // "-99\n" = 4 bytes
            if (baos.size() != 4) return 1;
            arr : byte[]* = baos.toByteArray();
            if (arr[0] != (byte) '-') return 2;
            if (arr[1] != (byte) '9') return 3;
            if (arr[2] != (byte) '9') return 4;
            if (arr[3] != (byte) 10) return 5;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_println_short");
    REQUIRE(fn);
    CHECK(fn() == 0);
}
// ═════════════════════════════════════════════════════════════════════════════
// println(long) — "999\n"
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("PrintStream println(long)", "[libk][io][print]") {
    auto jit = jit_k(R"SRC(
        module __ps_println_long__;
        test_println_long() : int {
            baos : k::io::ByteArrayOutputStream;
            ps : k::io::PrintStream(&baos);
            ps.println(999L);
            // "999\n" = 4 bytes
            if (baos.size() != 4) return 1;
            arr : byte[]* = baos.toByteArray();
            if (arr[0] != (byte) '9') return 2;
            if (arr[3] != (byte) 10) return 3;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_println_long");
    REQUIRE(fn);
    CHECK(fn() == 0);
}
// ═════════════════════════════════════════════════════════════════════════════
// println(unsigned short)
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("PrintStream println(unsigned short)", "[libk][io][print]") {
    auto jit = jit_k(R"SRC(
        module __ps_println_ushort__;
        test_println_ushort() : int {
            baos : k::io::ByteArrayOutputStream;
            ps : k::io::PrintStream(&baos);
            ps.println((unsigned short) 100);
            // "100\n" = 4 bytes
            if (baos.size() != 4) return 1;
            arr : byte[]* = baos.toByteArray();
            if (arr[0] != (byte) '1') return 2;
            if (arr[3] != (byte) 10) return 3;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_println_ushort");
    REQUIRE(fn);
    CHECK(fn() == 0);
}
// ═════════════════════════════════════════════════════════════════════════════
// println(unsigned int)
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("PrintStream println(unsigned int)", "[libk][io][print]") {
    auto jit = jit_k(R"SRC(
        module __ps_println_uint__;
        test_println_uint() : int {
            baos : k::io::ByteArrayOutputStream;
            ps : k::io::PrintStream(&baos);
            ps.println(500u);
            // "500\n" = 4 bytes
            if (baos.size() != 4) return 1;
            arr : byte[]* = baos.toByteArray();
            if (arr[0] != (byte) '5') return 2;
            if (arr[3] != (byte) 10) return 3;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_println_uint");
    REQUIRE(fn);
    CHECK(fn() == 0);
}
// ═════════════════════════════════════════════════════════════════════════════
// println(unsigned long)
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("PrintStream println(unsigned long)", "[libk][io][print]") {
    auto jit = jit_k(R"SRC(
        module __ps_println_ulong__;
        test_println_ulong() : int {
            baos : k::io::ByteArrayOutputStream;
            ps : k::io::PrintStream(&baos);
            ps.println(77uL);
            // "77\n" = 3 bytes
            if (baos.size() != 3) return 1;
            arr : byte[]* = baos.toByteArray();
            if (arr[0] != (byte) '7') return 2;
            if (arr[2] != (byte) 10) return 3;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_println_ulong");
    REQUIRE(fn);
    CHECK(fn() == 0);
}
// ═════════════════════════════════════════════════════════════════════════════
// println(float)
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("PrintStream println(float)", "[libk][io][print]") {
    auto jit = jit_k(R"SRC(
        module __ps_println_float__;
        test_println_float() : int {
            baos : k::io::ByteArrayOutputStream;
            ps : k::io::PrintStream(&baos);
            ps.println(1.5f);
            // "1.5\n" = 4 bytes
            if (baos.size() != 4) return 1;
            arr : byte[]* = baos.toByteArray();
            if (arr[0] != (byte) '1') return 2;
            if (arr[1] != (byte) '.') return 3;
            if (arr[2] != (byte) '5') return 4;
            if (arr[3] != (byte) 10) return 5;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_println_float");
    REQUIRE(fn);
    CHECK(fn() == 0);
}
// ═════════════════════════════════════════════════════════════════════════════
// println(double)
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("PrintStream println(double)", "[libk][io][print]") {
    auto jit = jit_k(R"SRC(
        module __ps_println_double__;
        test_println_double() : int {
            baos : k::io::ByteArrayOutputStream;
            ps : k::io::PrintStream(&baos);
            ps.println(3.14);
            // "3.14\n" = 5 bytes
            if (baos.size() != 5) return 1;
            arr : byte[]* = baos.toByteArray();
            if (arr[0] != (byte) '3') return 2;
            if (arr[1] != (byte) '.') return 3;
            if (arr[4] != (byte) 10) return 4;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_println_double");
    REQUIRE(fn);
    CHECK(fn() == 0);
}
// ═════════════════════════════════════════════════════════════════════════════
// print(const char[]) — empty string literal
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("PrintStream print empty string", "[libk][io][print]") {
    auto jit = jit_k(R"SRC(
        module __ps_empty_str__;
        test_print_empty() : int {
            baos : k::io::ByteArrayOutputStream;
            ps : k::io::PrintStream(&baos);
            ps.print("");
            // empty string literal is just '\0' → nothing printed
            if (baos.size() != 0) return 1;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_print_empty");
    REQUIRE(fn);
    CHECK(fn() == 0);
}
// ═════════════════════════════════════════════════════════════════════════════
// println(const char[]) — empty string literal gives just "\n"
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("PrintStream println empty string", "[libk][io][print]") {
    auto jit = jit_k(R"SRC(
        module __ps_println_empty_str__;
        test_println_empty_str() : int {
            baos : k::io::ByteArrayOutputStream;
            ps : k::io::PrintStream(&baos);
            ps.println("");
            // empty string → just newline = 1 byte
            if (baos.size() != 1) return 1;
            arr : byte[]* = baos.toByteArray();
            if (arr[0] != (byte) 10) return 2;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_println_empty_str");
    REQUIRE(fn);
    CHECK(fn() == 0);
}
// ═════════════════════════════════════════════════════════════════════════════
// close() delegation
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("PrintStream close delegates", "[libk][io][print]") {
    auto jit = jit_k(R"SRC(
        module __ps_close__;
        test_close() : int {
            baos : k::io::ByteArrayOutputStream;
            ps : k::io::PrintStream(&baos);
            ps.print(42);
            ps.close();
            // Data should still be in baos after close
            if (baos.size() != 2) return 1;
            arr : byte[]* = baos.toByteArray();
            if (arr[0] != (byte) '4') return 2;
            if (arr[1] != (byte) '2') return 3;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_close");
    REQUIRE(fn);
    CHECK(fn() == 0);
}
// ═════════════════════════════════════════════════════════════════════════════
// print(int) — zero
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("PrintStream print(int) zero", "[libk][io][print]") {
    auto jit = jit_k(R"SRC(
        module __ps_int_zero__;
        test_print_zero() : int {
            baos : k::io::ByteArrayOutputStream;
            ps : k::io::PrintStream(&baos);
            ps.print(0);
            // "0" = 1 byte
            if (baos.size() != 1) return 1;
            arr : byte[]* = baos.toByteArray();
            if (arr[0] != (byte) '0') return 2;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_print_zero");
    REQUIRE(fn);
    CHECK(fn() == 0);
}
// ═════════════════════════════════════════════════════════════════════════════
// print(bool) — false only
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("PrintStream print(bool) false", "[libk][io][print]") {
    auto jit = jit_k(R"SRC(
        module __ps_bool_false__;
        test_print_false() : int {
            baos : k::io::ByteArrayOutputStream;
            ps : k::io::PrintStream(&baos);
            ps.print(false);
            if (baos.size() != 5) return 1;
            arr : byte[]* = baos.toByteArray();
            if (arr[0] != (byte) 'f') return 2;
            if (arr[4] != (byte) 'e') return 3;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_print_false");
    REQUIRE(fn);
    CHECK(fn() == 0);
}
// ═════════════════════════════════════════════════════════════════════════════
// print(int) — negative number
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("PrintStream print(int) negative", "[libk][io][print]") {
    auto jit = jit_k(R"SRC(
        module __ps_int_neg__;
        test_print_neg() : int {
            baos : k::io::ByteArrayOutputStream;
            ps : k::io::PrintStream(&baos);
            ps.print(-12345);
            // "-12345" = 6 bytes
            if (baos.size() != 6) return 1;
            arr : byte[]* = baos.toByteArray();
            if (arr[0] != (byte) '-') return 2;
            if (arr[5] != (byte) '5') return 3;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_print_neg");
    REQUIRE(fn);
    CHECK(fn() == 0);
}
// ═════════════════════════════════════════════════════════════════════════════
// Fluent chaining with const char[] — print("a").print("b").println("c")
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("PrintStream fluent chaining with strings", "[libk][io][print]") {
    auto jit = jit_k(R"SRC(
        module __ps_fluent_str__;
        test_fluent_str() : int {
            baos : k::io::ByteArrayOutputStream;
            ps : k::io::PrintStream(&baos);
            ps.print("Hello").print(' ').println("World");
            // "Hello World\n" = 12 bytes
            if (baos.size() != 12) return 1;
            arr : byte[]* = baos.toByteArray();
            if (arr[0] != (byte) 'H') return 2;
            if (arr[5] != (byte) ' ') return 3;
            if (arr[6] != (byte) 'W') return 4;
            if (arr[11] != (byte) 10) return 5;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_fluent_str");
    REQUIRE(fn);
    CHECK(fn() == 0);
}
// ═════════════════════════════════════════════════════════════════════════════
// Multiple println calls — accumulates correctly
// ═════════════════════════════════════════════════════════════════════════════
TEST_CASE("PrintStream multiple println", "[libk][io][print]") {
    auto jit = jit_k(R"SRC(
        module __ps_multi_println__;
        test_multi_println() : int {
            baos : k::io::ByteArrayOutputStream;
            ps : k::io::PrintStream(&baos);
            ps.println(1);
            ps.println(2);
            ps.println(3);
            // "1\n2\n3\n" = 6 bytes
            if (baos.size() != 6) return 1;
            arr : byte[]* = baos.toByteArray();
            if (arr[0] != (byte) '1') return 2;
            if (arr[1] != (byte) 10) return 3;
            if (arr[2] != (byte) '2') return 4;
            if (arr[3] != (byte) 10) return 5;
            if (arr[4] != (byte) '3') return 6;
            if (arr[5] != (byte) 10) return 7;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_multi_println");
    REQUIRE(fn);
    CHECK(fn() == 0);
}
