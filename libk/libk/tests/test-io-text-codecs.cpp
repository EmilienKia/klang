/*
 * K Language standard library — Text Codecs tests
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

TEST_CASE("Text Codecs: StringReader and StringWriter", "[libk][io][text][string]") {
    auto jit = jit_k(R"SRC(
        module __test_str_rw__;

        test_string_rw() : int {
            sw : k::io::StringWriter;
            sw.write("Hello ");
            sw.write("World!");
            sw.writeLine();
            sw.writeLine("Second line");

            s : k::String = sw.toString();
            sr : k::io::StringReader(s);

            buf : char[]! = new char[6];
            n : int = (int) sr.read(buf, 0u, 5u).getResultOr(0u);
            if (n != 5) return 1;
            if (buf[0] != 'H' || buf[1] != 'e' || buf[2] != 'l' || buf[3] != 'l' || buf[4] != 'o') return 2;

            c : k::Optional<char> = sr.read();
            if (!c.hasValue() || c.get() != ' ') return 3;

            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_string_rw");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

TEST_CASE("Text Codecs: Utf8Reader decode valid UTF-8 sequences", "[libk][io][text][utf8]") {
    auto jit = jit_k(R"SRC(
        module __test_utf8_reader__;

        test_utf8_decode() : int {
            // "A" (0x41), "é" (0xC3 0xA9), "€" (0xE2 0x82 0xAC), rocket emoji (0xF0 0x9F 0x9A 0x80)
            bytes : byte[]! = new byte[10];
            bytes[0] = (byte) 0x41;
            bytes[1] = (byte) 0xC3;
            bytes[2] = (byte) 0xA9;
            bytes[3] = (byte) 0xE2;
            bytes[4] = (byte) 0x82;
            bytes[5] = (byte) 0xAC;
            bytes[6] = (byte) 0xF0;
            bytes[7] = (byte) 0x9F;
            bytes[8] = (byte) 0x9A;
            bytes[9] = (byte) 0x80;

            bais : k::io::ArrayInputStream<byte>(bytes, 10);
            reader : k::io::Utf8Reader(&bais);

            c1 : k::Optional<char> = reader.read();
            if (!c1.hasValue() || c1.get() != 'A') return 1;

            c2 : k::Optional<char> = reader.read();
            if (!c2.hasValue() || (unsigned int) c2.get() != 0x00E9u) return 2;

            c3 : k::Optional<char> = reader.read();
            if (!c3.hasValue() || (unsigned int) c3.get() != 0x20ACu) return 3;

            c4 : k::Optional<char> = reader.read();
            if (!c4.hasValue() || (unsigned int) c4.get() != 0x1F680u) return 4;

            c5 : k::Optional<char> = reader.read();
            if (c5.hasValue()) return 5;

            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_utf8_decode");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

TEST_CASE("Text Codecs: Utf8Writer encode code points to bytes", "[libk][io][text][utf8]") {
    auto jit = jit_k(R"SRC(
        module __test_utf8_writer__;

        test_utf8_encode() : int {
            baos : k::io::ArrayOutputStream<byte>;
            writer : k::io::Utf8Writer(&baos);

            writer.write('A');
            writer.write((char) 0x00E9u); // é
            writer.write((char) 0x20ACu); // €
            writer.write((char) 0x1F680u); // 🚀

            arr : byte[]* = baos.toArray();
            if (baos.size() != 10) return 1;

            if (arr[0] != (byte) 0x41) return 2;
            if (arr[1] != (byte) 0xC3 || arr[2] != (byte) 0xA9) return 3;
            if (arr[3] != (byte) 0xE2 || arr[4] != (byte) 0x82 || arr[5] != (byte) 0xAC) return 4;
            if (arr[6] != (byte) 0xF0 || arr[7] != (byte) 0x9F || arr[8] != (byte) 0x9A || arr[9] != (byte) 0x80) return 5;

            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_utf8_encode");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

TEST_CASE("Text Codecs: Utf8Reader Malformed Input Exception", "[libk][io][text][utf8]") {
    auto jit = jit_k(R"SRC(
        module __test_utf8_malformed__;

        test_utf8_malformed() : int {
            bytes : byte[]! = new byte[2];
            bytes[0] = (byte) 0xC0; // Overlong sequence
            bytes[1] = (byte) 0xAF;

            bais : k::io::ArrayInputStream<byte>(bytes, 2);
            reader : k::io::Utf8Reader(&bais, k::io::CodingErrorAction::Report);

            try {
                reader.read();
                return 1; // Expected exception
            } catch (e: k::io::MalformedInputException&) {
                return 0; // Success
            } catch (e2: k::Throwable&) {
                return 2;
            }
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_utf8_malformed");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

TEST_CASE("Text Codecs: Utf16Reader and Utf16Writer", "[libk][io][text][utf16]") {
    auto jit = jit_k(R"SRC(
        module __test_utf16__;

        test_utf16_roundtrip() : int {
            baos : k::io::ArrayOutputStream<byte>;
            writer : k::io::Utf16Writer(&baos, k::io::ByteOrder::BigEndian);

            writer.write('A');
            writer.write((char) 0x00E9u); // é
            writer.write((char) 0x1F680u); // surrogate pair

            arr : byte[]* = baos.toArray();
            if (baos.size() != 8) return 1;

            bais : k::io::ArrayInputStream<byte>(arr, baos.size());
            reader : k::io::Utf16Reader(&bais, k::io::ByteOrder::BigEndian);

            c1 : k::Optional<char> = reader.read();
            if (!c1.hasValue() || c1.get() != 'A') return 2;

            c2 : k::Optional<char> = reader.read();
            if (!c2.hasValue() || (unsigned int) c2.get() != 0x00E9u) return 3;

            c3 : k::Optional<char> = reader.read();
            if (!c3.hasValue() || (unsigned int) c3.get() != 0x1F680u) return 4;

            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_utf16_roundtrip");
    REQUIRE(fn);
    CHECK(fn() == 0);
}
