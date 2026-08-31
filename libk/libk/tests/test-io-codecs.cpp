/*
 * K Language standard library — Granular Stream Codec tests
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


// ═════════════════════════════════════════════════════════════════════════════
// 1. Base64 Codec Tests (Fine-grained Cases)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Base64: Standard Encoding vectors with and without padding", "[libk][io][codec][base64]") {
    auto jit = jit_k(R"SRC(
        module __test_b64_enc__;

        test_enc_empty() : int {
            inStream : k::io::ArrayInputStream<byte>;
            b64In : k::io::Base64InputStream(&inStream, true);
            buf : byte[]! = new byte[8];
            res : Expected<unsigned int, ::k::io::StreamOutOfData> = b64In.read(buf, 0u, 8u);
            if (res.hasError() || res.getResult() != 0u) return 1;
            return 0;
        }

        test_enc_1_byte() : int { // "f" -> "Zg=="
            raw : byte[]! = new byte[1];
            raw[0] = (byte) 'f';
            inStream : k::io::ArrayInputStream<byte>(raw, 1);
            b64In : k::io::Base64InputStream(&inStream, true);
            buf : byte[]! = new byte[8];
            res : Expected<unsigned int, ::k::io::StreamOutOfData> = b64In.read(buf, 0u, 8u);
            if (res.hasError() || res.getResult() != 4u) return 1;
            if (buf[0] != (byte) 'Z' || buf[1] != (byte) 'g' || buf[2] != (byte) '=' || buf[3] != (byte) '=') return 2;
            return 0;
        }

        test_enc_2_bytes() : int { // "fo" -> "Zm8="
            raw : byte[]! = new byte[2];
            raw[0] = (byte) 'f'; raw[1] = (byte) 'o';
            inStream : k::io::ArrayInputStream<byte>(raw, 2);
            b64In : k::io::Base64InputStream(&inStream, true);
            buf : byte[]! = new byte[8];
            res : Expected<unsigned int, ::k::io::StreamOutOfData> = b64In.read(buf, 0u, 8u);
            if (res.hasError() || res.getResult() != 4u) return 1;
            if (buf[0] != (byte) 'Z' || buf[1] != (byte) 'm' || buf[2] != (byte) '8' || buf[3] != (byte) '=') return 2;
            return 0;
        }

        test_enc_3_bytes() : int { // "foo" -> "Zm9v"
            raw : byte[]! = new byte[3];
            raw[0] = (byte) 'f'; raw[1] = (byte) 'o'; raw[2] = (byte) 'o';
            inStream : k::io::ArrayInputStream<byte>(raw, 3);
            b64In : k::io::Base64InputStream(&inStream, true);
            buf : byte[]! = new byte[8];
            res : Expected<unsigned int, ::k::io::StreamOutOfData> = b64In.read(buf, 0u, 8u);
            if (res.hasError() || res.getResult() != 4u) return 1;
            if (buf[0] != (byte) 'Z' || buf[1] != (byte) 'm' || buf[2] != (byte) '9' || buf[3] != (byte) 'v') return 2;
            return 0;
        }

        test_enc_6_bytes() : int { // "foobar" -> "Zm9vYmFy"
            raw : byte[]! = new byte[6];
            raw[0] = (byte) 'f'; raw[1] = (byte) 'o'; raw[2] = (byte) 'o';
            raw[3] = (byte) 'b'; raw[4] = (byte) 'a'; raw[5] = (byte) 'r';
            inStream : k::io::ArrayInputStream<byte>(raw, 6);
            b64In : k::io::Base64InputStream(&inStream, true);
            buf : byte[]! = new byte[16];
            res : Expected<unsigned int, ::k::io::StreamOutOfData> = b64In.read(buf, 0u, 16u);
            if (res.hasError() || res.getResult() != 8u) return 1;
            if (buf[0] != (byte) 'Z' || buf[1] != (byte) 'm' || buf[2] != (byte) '9' || buf[3] != (byte) 'v') return 2;
            if (buf[4] != (byte) 'Y' || buf[5] != (byte) 'm' || buf[6] != (byte) 'F' || buf[7] != (byte) 'y') return 3;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto f0 = jit->lookup_symbol<int(*)()>("test_enc_empty");
    REQUIRE(f0);
    CHECK(f0() == 0);

    auto f1 = jit->lookup_symbol<int(*)()>("test_enc_1_byte");
    REQUIRE(f1);
    CHECK(f1() == 0);

    auto f2 = jit->lookup_symbol<int(*)()>("test_enc_2_bytes");
    REQUIRE(f2);
    CHECK(f2() == 0);

    auto f3 = jit->lookup_symbol<int(*)()>("test_enc_3_bytes");
    REQUIRE(f3);
    CHECK(f3() == 0);

    auto f6 = jit->lookup_symbol<int(*)()>("test_enc_6_bytes");
    REQUIRE(f6);
    CHECK(f6() == 0);
}

TEST_CASE("Base64: Standard Decoding vectors with padding and full blocks", "[libk][io][codec][base64]") {
    auto jit = jit_k(R"SRC(
        module __test_b64_dec_vectors__;

        test_dec_padded_1_byte() : int { // "Zg==" -> "f"
            enc : byte[]! = new byte[4];
            enc[0] = (byte) 'Z'; enc[1] = (byte) 'g'; enc[2] = (byte) '='; enc[3] = (byte) '=';
            inStream : k::io::ArrayInputStream<byte>(enc, 4);
            b64Dec : k::io::Base64InputStream(&inStream);
            buf : byte[]! = new byte[8];
            res : Expected<unsigned int, ::k::io::StreamOutOfData> = b64Dec.read(buf, 0u, 8u);
            if (res.hasError() || res.getResult() != 1u) return 1;
            if (buf[0] != (byte) 'f') return 2;
            return 0;
        }

        test_dec_padded_2_bytes() : int { // "Zm8=" -> "fo"
            enc : byte[]! = new byte[4];
            enc[0] = (byte) 'Z'; enc[1] = (byte) 'm'; enc[2] = (byte) '8'; enc[3] = (byte) '=';
            inStream : k::io::ArrayInputStream<byte>(enc, 4);
            b64Dec : k::io::Base64InputStream(&inStream);
            buf : byte[]! = new byte[8];
            res : Expected<unsigned int, ::k::io::StreamOutOfData> = b64Dec.read(buf, 0u, 8u);
            if (res.hasError() || res.getResult() != 2u) return 1;
            if (buf[0] != (byte) 'f' || buf[1] != (byte) 'o') return 2;
            return 0;
        }

        test_dec_full_block() : int { // "Zm9vYmFy" -> "foobar"
            enc : byte[]! = new byte[8];
            enc[0] = (byte) 'Z'; enc[1] = (byte) 'm'; enc[2] = (byte) '9'; enc[3] = (byte) 'v';
            enc[4] = (byte) 'Y'; enc[5] = (byte) 'm'; enc[6] = (byte) 'F'; enc[7] = (byte) 'y';
            inStream : k::io::ArrayInputStream<byte>(enc, 8);
            b64Dec : k::io::Base64InputStream(&inStream);
            buf : byte[]! = new byte[8];
            res : Expected<unsigned int, ::k::io::StreamOutOfData> = b64Dec.read(buf, 0u, 8u);
            if (res.hasError() || res.getResult() != 6u) return 1;
            if (buf[0] != (byte) 'f' || buf[1] != (byte) 'o' || buf[2] != (byte) 'o') return 2;
            if (buf[3] != (byte) 'b' || buf[4] != (byte) 'a' || buf[5] != (byte) 'r') return 3;
            return 0;
        }
    )SRC");
    REQUIRE(jit);

    auto f1 = jit->lookup_symbol<int(*)()>("test_dec_padded_1_byte");
    REQUIRE(f1);
    CHECK(f1() == 0);

    auto f2 = jit->lookup_symbol<int(*)()>("test_dec_padded_2_bytes");
    REQUIRE(f2);
    CHECK(f2() == 0);

    auto f6 = jit->lookup_symbol<int(*)()>("test_dec_full_block");
    REQUIRE(f6);
    CHECK(f6() == 0);
}

TEST_CASE("Base64: Decoding ignores whitespace characters", "[libk][io][codec][base64]") {
    auto jit = jit_k(R"SRC(
        module __test_b64_ws__;

        test_dec_whitespace() : int { // "Zm9v\r\n YmFy" -> "foobar"
            enc : byte[]! = new byte[11];
            enc[0] = (byte) 'Z'; enc[1] = (byte) 'm'; enc[2] = (byte) '9'; enc[3] = (byte) 'v';
            enc[4] = (byte) '\r'; enc[5] = (byte) '\n'; enc[6] = (byte) ' ';
            enc[7] = (byte) 'Y'; enc[8] = (byte) 'm'; enc[9] = (byte) 'F'; enc[10] = (byte) 'y';
            inStream : k::io::ArrayInputStream<byte>(enc, 11);
            b64Dec : k::io::Base64InputStream(&inStream);
            buf : byte[]! = new byte[8];
            res : Expected<unsigned int, ::k::io::StreamOutOfData> = b64Dec.read(buf, 0u, 8u);
            if (res.hasError() || res.getResult() != 6u) return 1;
            if (buf[0] != (byte) 'f' || buf[5] != (byte) 'r') return 2;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_dec_whitespace");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

TEST_CASE("Base64: URL-Safe encoding and decoding without padding", "[libk][io][codec][base64][urlsafe]") {
    auto jit = jit_k(R"SRC(
        module __test_b64_urlsafe_case__;

        test_urlsafe_enc_dec() : int {
            raw : byte[]! = new byte[3];
            raw[0] = (byte) 0xFB; raw[1] = (byte) 0xEF; raw[2] = (byte) 0xFE;

            inStream : k::io::ArrayInputStream<byte>(raw, 3);
            b64In : k::io::Base64InputStream(&inStream, true, true, 0u, null);
            encBuf : byte[]! = new byte[8];
            res : Expected<unsigned int, ::k::io::StreamOutOfData> = b64In.read(encBuf, 0u, 8u);
            if (res.hasError() || res.getResult() != 4u) return 1;

            if (encBuf[0] != (byte) '-' || encBuf[1] != (byte) '-' || encBuf[2] != (byte) '_' || encBuf[3] != (byte) '-') return 2;

            encStream : k::io::ArrayInputStream<byte>(encBuf, 4);
            b64Dec : k::io::Base64InputStream(&encStream);
            decBuf : byte[]! = new byte[4];
            decRes : Expected<unsigned int, ::k::io::StreamOutOfData> = b64Dec.read(decBuf, 0u, 4u);
            if (decRes.hasError() || decRes.getResult() != 3u) return 3;

            if (decBuf[0] != (byte) 0xFB || decBuf[1] != (byte) 0xEF || decBuf[2] != (byte) 0xFE) return 4;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_urlsafe_enc_dec");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

TEST_CASE("Base64: Line chunking with custom line length and separator", "[libk][io][codec][base64][chunking]") {
    auto jit = jit_k(R"SRC(
        module __test_b64_chunk_case__;

        test_b64_chunking() : int {
            sep : byte[]! = new byte[1];
            sep[0] = (byte) '\n';

            raw : byte[]! = new byte[6];
            raw[0] = (byte) 'f'; raw[1] = (byte) 'o'; raw[2] = (byte) 'o';
            raw[3] = (byte) 'b'; raw[4] = (byte) 'a'; raw[5] = (byte) 'r';

            inStream : k::io::ArrayInputStream<byte>(raw, 6);
            b64In : k::io::Base64InputStream(&inStream, true, false, 4u, sep);

            buf : byte[]! = new byte[16];
            res : Expected<unsigned int, ::k::io::StreamOutOfData> = b64In.read(buf, 0u, 16u);
            if (res.hasError() || res.getResult() != 10u) return 1;
            if (buf[4] != (byte) '\n' || buf[9] != (byte) '\n') return 2;

            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_b64_chunking");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

TEST_CASE("Base64: OutputStream write and readback round-trip", "[libk][io][codec][base64][stream]") {
    auto jit = jit_k(R"SRC(
        module __test_b64_out_case__;

        test_b64_output_stream() : int {
            outArray : k::io::ArrayOutputStream<byte>;
            b64Out : k::io::Base64OutputStream(&outArray);

            raw : byte[]! = new byte[6];
            raw[0] = (byte) 'f'; raw[1] = (byte) 'o'; raw[2] = (byte) 'o';
            raw[3] = (byte) 'b'; raw[4] = (byte) 'a'; raw[5] = (byte) 'r';
            b64Out.write(raw, 0u, 6u);
            b64Out.close();

            enc : byte[]! = outArray.toArray();
            if (enc.size != 8) return 1;
            if (enc[0] != (byte) 'Z' || enc[7] != (byte) 'y') return 2;

            decArray : k::io::ArrayOutputStream<byte>;
            b64DecOut : k::io::Base64OutputStream(&decArray, false);
            b64DecOut.write(enc, 0u, (unsigned int) enc.size);
            b64DecOut.close();

            dec : byte[]! = decArray.toArray();
            if (dec.size != 6) return 3;
            if (dec[0] != (byte) 'f' || dec[5] != (byte) 'r') return 4;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_b64_output_stream");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// 2. Hex (Base16) Codec Tests (Fine-grained Cases)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Hex: Uppercase encoding", "[libk][io][codec][hex]") {
    auto jit = jit_k(R"SRC(
        module __test_hex_upper__;

        test_upper() : int {
            raw : byte[]! = new byte[3];
            raw[0] = (byte) 0x1A; raw[1] = (byte) 0x2B; raw[2] = (byte) 0x3C;
            inStream : k::io::ArrayInputStream<byte>(raw, 3);
            hexIn : k::io::HexInputStream(&inStream, true, true, 0u, null);
            buf : byte[]! = new byte[8];
            res : Expected<unsigned int, ::k::io::StreamOutOfData> = hexIn.read(buf, 0u, 8u);
            if (res.hasError() || res.getResult() != 6u) return 1;
            if (buf[0] != (byte) '1' || buf[1] != (byte) 'A' || buf[2] != (byte) '2' || buf[3] != (byte) 'B') return 2;
            if (buf[4] != (byte) '3' || buf[5] != (byte) 'C') return 3;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_upper");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

TEST_CASE("Hex: Lowercase encoding", "[libk][io][codec][hex]") {
    auto jit = jit_k(R"SRC(
        module __test_hex_lower__;

        test_lower() : int {
            raw : byte[]! = new byte[2];
            raw[0] = (byte) 0xAB; raw[1] = (byte) 0xCD;
            inStream : k::io::ArrayInputStream<byte>(raw, 2);
            hexIn : k::io::HexInputStream(&inStream, true, false, 0u, null);
            buf : byte[]! = new byte[8];
            res : Expected<unsigned int, ::k::io::StreamOutOfData> = hexIn.read(buf, 0u, 8u);
            if (res.hasError() || res.getResult() != 4u) return 1;
            if (buf[0] != (byte) 'a' || buf[1] != (byte) 'b' || buf[2] != (byte) 'c' || buf[3] != (byte) 'd') return 2;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_lower");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

TEST_CASE("Hex: Decoding uppercase, lowercase and mixed-case", "[libk][io][codec][hex]") {
    auto jit = jit_k(R"SRC(
        module __test_hex_dec__;

        test_decode() : int {
            enc : byte[]! = new byte[6];
            enc[0] = (byte) '1'; enc[1] = (byte) 'a'; enc[2] = (byte) '2'; enc[3] = (byte) 'B'; enc[4] = (byte) '3'; enc[5] = (byte) 'c';
            inStream : k::io::ArrayInputStream<byte>(enc, 6);
            hexDec : k::io::HexInputStream(&inStream);
            buf : byte[]! = new byte[4];
            res : Expected<unsigned int, ::k::io::StreamOutOfData> = hexDec.read(buf, 0u, 4u);
            if (res.hasError() || res.getResult() != 3u) return 1;
            if (buf[0] != (byte) 0x1A || buf[1] != (byte) 0x2B || buf[2] != (byte) 0x3C) return 2;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_decode");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

TEST_CASE("Hex: OutputStream encoding and decoding", "[libk][io][codec][hex][stream]") {
    auto jit = jit_k(R"SRC(
        module __test_hex_stream__;

        test_out_stream() : int {
            outArray : k::io::ArrayOutputStream<byte>;
            hexOut : k::io::HexOutputStream(&outArray, true, false, 0u, null);
            raw : byte[]! = new byte[2];
            raw[0] = (byte) 0xCA; raw[1] = (byte) 0xFE;
            hexOut.write(raw, 0u, 2u);
            hexOut.close();

            enc : byte[]! = outArray.toArray();
            if (enc.size != 4) return 1;
            if (enc[0] != (byte) 'c' || enc[1] != (byte) 'a' || enc[2] != (byte) 'f' || enc[3] != (byte) 'e') return 2;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_out_stream");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// 3. Base2 (Binary) Codec Tests (Fine-grained Cases)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Base2: Binary bitwise encoding", "[libk][io][codec][base2]") {
    auto jit = jit_k(R"SRC(
        module __test_base2_enc__;

        test_enc() : int {
            raw : byte[]! = new byte[2];
            raw[0] = (byte) 0xA5; // 10100101
            raw[1] = (byte) 0x3C; // 00111100
            inStream : k::io::ArrayInputStream<byte>(raw, 2);
            b2In : k::io::Base2InputStream(&inStream, true);
            buf : byte[]! = new byte[20];
            res : Expected<unsigned int, ::k::io::StreamOutOfData> = b2In.read(buf, 0u, 20u);
            if (res.hasError() || res.getResult() != 16u) return 1;

            if (buf[0] != (byte) '1' || buf[1] != (byte) '0' || buf[2] != (byte) '1' || buf[3] != (byte) '0') return 2;
            if (buf[4] != (byte) '0' || buf[5] != (byte) '1' || buf[6] != (byte) '0' || buf[7] != (byte) '1') return 3;
            if (buf[8] != (byte) '0' || buf[9] != (byte) '0' || buf[10] != (byte) '1' || buf[11] != (byte) '1') return 4;
            if (buf[12] != (byte) '1' || buf[13] != (byte) '1' || buf[14] != (byte) '0' || buf[15] != (byte) '0') return 5;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_enc");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

TEST_CASE("Base2: Binary bitwise decoding", "[libk][io][codec][base2]") {
    auto jit = jit_k(R"SRC(
        module __test_base2_dec__;

        test_dec() : int {
            enc : byte[]! = new byte[16];
            enc[0] = (byte) '1'; enc[1] = (byte) '0'; enc[2] = (byte) '1'; enc[3] = (byte) '0';
            enc[4] = (byte) '0'; enc[5] = (byte) '1'; enc[6] = (byte) '0'; enc[7] = (byte) '1';
            enc[8] = (byte) '0'; enc[9] = (byte) '0'; enc[10] = (byte) '1'; enc[11] = (byte) '1';
            enc[12] = (byte) '1'; enc[13] = (byte) '1'; enc[14] = (byte) '0'; enc[15] = (byte) '0';

            inStream : k::io::ArrayInputStream<byte>(enc, 16);
            b2Dec : k::io::Base2InputStream(&inStream);
            buf : byte[]! = new byte[4];
            res : Expected<unsigned int, ::k::io::StreamOutOfData> = b2Dec.read(buf, 0u, 4u);
            if (res.hasError() || res.getResult() != 2u) return 1;
            if (buf[0] != (byte) 0xA5 || buf[1] != (byte) 0x3C) return 2;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_dec");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

TEST_CASE("Base2: OutputStream encoding and decoding", "[libk][io][codec][base2][stream]") {
    auto jit = jit_k(R"SRC(
        module __test_base2_stream__;

        test_stream() : int {
            outArray : k::io::ArrayOutputStream<byte>;
            b2Out : k::io::Base2OutputStream(&outArray);
            raw : byte[]! = new byte[1];
            raw[0] = (byte) 0xFF;
            b2Out.write(raw, 0u, 1u);
            b2Out.close();

            enc : byte[]! = outArray.toArray();
            if (enc.size != 8) return 1;
            i : int = 0;
            while (i < 8) {
                if (enc[i] != (byte) '1') return 2;
                ++i;
            }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_stream");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// 4. Base32 Codec Tests (Fine-grained Cases)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Base32: Standard alphabet encoding and padding", "[libk][io][codec][base32]") {
    auto jit = jit_k(R"SRC(
        module __test_b32_enc__;

        test_enc_standard() : int {
            // "fooba" -> "MZXW6YTB"
            raw : byte[]! = new byte[5];
            raw[0] = (byte) 'f'; raw[1] = (byte) 'o'; raw[2] = (byte) 'o';
            raw[3] = (byte) 'b'; raw[4] = (byte) 'a';

            inStream : k::io::ArrayInputStream<byte>(raw, 5);
            b32In : k::io::Base32InputStream(&inStream, true);
            buf : byte[]! = new byte[16];
            res : Expected<unsigned int, ::k::io::StreamOutOfData> = b32In.read(buf, 0u, 16u);
            if (res.hasError() || res.getResult() != 8u) return 1;

            if (buf[0] != (byte) 'M' || buf[1] != (byte) 'Z' || buf[2] != (byte) 'X' || buf[3] != (byte) 'W') return 2;
            if (buf[4] != (byte) '6' || buf[5] != (byte) 'Y' || buf[6] != (byte) 'T' || buf[7] != (byte) 'B') return 3;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_enc_standard");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

TEST_CASE("Base32: Standard alphabet decoding", "[libk][io][codec][base32]") {
    auto jit = jit_k(R"SRC(
        module __test_b32_dec__;

        test_dec_standard() : int {
            enc : byte[]! = new byte[8];
            enc[0] = (byte) 'M'; enc[1] = (byte) 'Z'; enc[2] = (byte) 'X'; enc[3] = (byte) 'W';
            enc[4] = (byte) '6'; enc[5] = (byte) 'Y'; enc[6] = (byte) 'T'; enc[7] = (byte) 'B';

            inStream : k::io::ArrayInputStream<byte>(enc, 8);
            b32Dec : k::io::Base32InputStream(&inStream);
            decBuf : byte[]! = new byte[8];
            res : Expected<unsigned int, ::k::io::StreamOutOfData> = b32Dec.read(decBuf, 0u, 8u);
            if (res.hasError() || res.getResult() != 5u) return 1;
            if (decBuf[0] != (byte) 'f' || decBuf[4] != (byte) 'a') return 2;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_dec_standard");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

TEST_CASE("Base32: Extended Hex alphabet encoding and decoding", "[libk][io][codec][base32][hex]") {
    auto jit = jit_k(R"SRC(
        module __test_b32_hex__;

        test_hex_alphabet() : int {
            // RFC 4648 Base32Hex: "foo" -> "CPNGU==="
            raw : byte[]! = new byte[3];
            raw[0] = (byte) 'f'; raw[1] = (byte) 'o'; raw[2] = (byte) 'o';

            inStream : k::io::ArrayInputStream<byte>(raw, 3);
            b32In : k::io::Base32InputStream(&inStream, true, true);
            buf : byte[]! = new byte[16];
            res : Expected<unsigned int, ::k::io::StreamOutOfData> = b32In.read(buf, 0u, 16u);
            if (res.hasError() || res.getResult() != 8u) return 1;

            if (buf[0] != (byte) 'C' || buf[1] != (byte) 'P' || buf[2] != (byte) 'N' || buf[3] != (byte) 'M' || buf[4] != (byte) 'U') return 2;
            if (buf[5] != (byte) '=' || buf[6] != (byte) '=' || buf[7] != (byte) '=') return 3;

            encStream : k::io::ArrayInputStream<byte>(buf, 8);
            b32Dec : k::io::Base32InputStream(&encStream, false, true);
            decBuf : byte[]! = new byte[8];
            decRes : Expected<unsigned int, ::k::io::StreamOutOfData> = b32Dec.read(decBuf, 0u, 8u);
            if (decRes.hasError() || decRes.getResult() != 3u) return 4;
            if (decBuf[0] != (byte) 'f' || decBuf[1] != (byte) 'o' || decBuf[2] != (byte) 'o') return 5;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_hex_alphabet");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

TEST_CASE("Base32: OutputStream encoding and decoding", "[libk][io][codec][base32][stream]") {
    auto jit = jit_k(R"SRC(
        module __test_b32_stream__;

        test_output_stream() : int {
            outArray : k::io::ArrayOutputStream<byte>;
            b32Out : k::io::Base32OutputStream(&outArray);
            raw : byte[]! = new byte[5];
            raw[0] = (byte) 'f'; raw[1] = (byte) 'o'; raw[2] = (byte) 'o';
            raw[3] = (byte) 'b'; raw[4] = (byte) 'a';
            b32Out.write(raw, 0u, 5u);
            b32Out.close();

            enc : byte[]! = outArray.toArray();
            if (enc.size != 8) return 1;
            if (enc[0] != (byte) 'M' || enc[7] != (byte) 'B') return 2;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_output_stream");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// 5. Percent and Form-URL Codec Tests (Fine-grained Cases)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Percent: Unreserved characters passthrough", "[libk][io][codec][percent]") {
    auto jit = jit_k(R"SRC(
        module __test_p_pass__;

        test_passthrough() : int {
            raw : byte[]! = new byte[8];
            raw[0] = (byte) 'A'; raw[1] = (byte) 'z'; raw[2] = (byte) '9'; raw[3] = (byte) '-';
            raw[4] = (byte) '_'; raw[5] = (byte) '.'; raw[6] = (byte) '~'; raw[7] = (byte) 'k';

            inStream : k::io::ArrayInputStream<byte>(raw, 8);
            pIn : k::io::PercentInputStream(&inStream, true);
            buf : byte[]! = new byte[16];
            res : Expected<unsigned int, ::k::io::StreamOutOfData> = pIn.read(buf, 0u, 16u);
            if (res.hasError() || res.getResult() != 8u) return 1;

            i : int = 0;
            while (i < 8) {
                if (buf[i] != raw[i]) return 2;
                ++i;
            }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_passthrough");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

TEST_CASE("Percent: Reserved and special characters percent-encoding", "[libk][io][codec][percent]") {
    auto jit = jit_k(R"SRC(
        module __test_p_enc__;

        test_reserved() : int {
            raw : byte[]! = new byte[3];
            raw[0] = (byte) ' '; raw[1] = (byte) '!'; raw[2] = (byte) '/';

            inStream : k::io::ArrayInputStream<byte>(raw, 3);
            pIn : k::io::PercentInputStream(&inStream, true);
            buf : byte[]! = new byte[16];
            res : Expected<unsigned int, ::k::io::StreamOutOfData> = pIn.read(buf, 0u, 16u);
            if (res.hasError() || res.getResult() != 9u) return 1;

            if (buf[0] != (byte) '%' || buf[1] != (byte) '2' || buf[2] != (byte) '0') return 2;
            if (buf[3] != (byte) '%' || buf[4] != (byte) '2' || buf[5] != (byte) '1') return 3;
            if (buf[6] != (byte) '%' || buf[7] != (byte) '2' || buf[8] != (byte) 'F') return 4;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_reserved");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

TEST_CASE("Percent: Percent-decoding hexadecimal sequences", "[libk][io][codec][percent]") {
    auto jit = jit_k(R"SRC(
        module __test_p_dec__;

        test_decode() : int {
            enc : byte[]! = new byte[9];
            enc[0] = (byte) '%'; enc[1] = (byte) '2'; enc[2] = (byte) '0';
            enc[3] = (byte) '%'; enc[4] = (byte) '2'; enc[5] = (byte) '1';
            enc[6] = (byte) '%'; enc[7] = (byte) '2'; enc[8] = (byte) 'F';

            inStream : k::io::ArrayInputStream<byte>(enc, 9);
            pDec : k::io::PercentInputStream(&inStream);
            decBuf : byte[]! = new byte[8];
            decRes : Expected<unsigned int, ::k::io::StreamOutOfData> = pDec.read(decBuf, 0u, 8u);
            if (decRes.hasError() || decRes.getResult() != 3u) return 1;
            if (decBuf[0] != (byte) ' ' || decBuf[1] != (byte) '!' || decBuf[2] != (byte) '/') return 2;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_decode");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

TEST_CASE("Percent: OutputStream encoding and decoding", "[libk][io][codec][percent][stream]") {
    auto jit = jit_k(R"SRC(
        module __test_p_stream__;

        test_stream() : int {
            outArray : k::io::ArrayOutputStream<byte>;
            pOut : k::io::PercentOutputStream(&outArray, true);
            raw : byte[]! = new byte[3];
            raw[0] = (byte) 'k'; raw[1] = (byte) '='; raw[2] = (byte) '1';
            pOut.write(raw, 0u, 3u);
            pOut.close();

            enc : byte[]! = outArray.toArray();
            if (enc.size != 5) return 1; // "k%3D1"
            if (enc[0] != (byte) 'k' || enc[1] != (byte) '%' || enc[2] != (byte) '3' || enc[3] != (byte) 'D' || enc[4] != (byte) '1') return 2;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_stream");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

TEST_CASE("FormUrl: Space to plus encoding and decoding", "[libk][io][codec][formurl]") {
    auto jit = jit_k(R"SRC(
        module __test_fu_space__;

        test_space() : int {
            raw : byte[]! = new byte[5];
            raw[0] = (byte) 'a'; raw[1] = (byte) ' '; raw[2] = (byte) 'b'; raw[3] = (byte) ' '; raw[4] = (byte) 'c';

            inStream : k::io::ArrayInputStream<byte>(raw, 5);
            fIn : k::io::FormUrlInputStream(&inStream, true);
            buf : byte[]! = new byte[16];
            res : Expected<unsigned int, ::k::io::StreamOutOfData> = fIn.read(buf, 0u, 16u);
            if (res.hasError() || res.getResult() != 5u) return 1;

            // "a+b+c"
            if (buf[0] != (byte) 'a' || buf[1] != (byte) '+' || buf[2] != (byte) 'b') return 2;
            if (buf[3] != (byte) '+' || buf[4] != (byte) 'c') return 3;

            encStream : k::io::ArrayInputStream<byte>(buf, 5);
            fDec : k::io::FormUrlInputStream(&encStream);
            decBuf : byte[]! = new byte[8];
            decRes : Expected<unsigned int, ::k::io::StreamOutOfData> = fDec.read(decBuf, 0u, 8u);
            if (decRes.hasError() || decRes.getResult() != 5u) return 4;
            if (decBuf[1] != (byte) ' ' || decBuf[3] != (byte) ' ') return 5;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_space");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

TEST_CASE("FormUrl: Special characters and percent-sequences", "[libk][io][codec][formurl]") {
    auto jit = jit_k(R"SRC(
        module __test_fu_special__;

        test_special() : int {
            raw : byte[]! = new byte[3];
            raw[0] = (byte) 'x'; raw[1] = (byte) '='; raw[2] = (byte) 'y';

            inStream : k::io::ArrayInputStream<byte>(raw, 3);
            fIn : k::io::FormUrlInputStream(&inStream, true);
            buf : byte[]! = new byte[16];
            res : Expected<unsigned int, ::k::io::StreamOutOfData> = fIn.read(buf, 0u, 16u);
            if (res.hasError() || res.getResult() != 5u) return 1;

            // "x%3Dy"
            if (buf[0] != (byte) 'x' || buf[1] != (byte) '%' || buf[2] != (byte) '3' || buf[3] != (byte) 'D' || buf[4] != (byte) 'y') return 2;

            encStream : k::io::ArrayInputStream<byte>(buf, 5);
            fDec : k::io::FormUrlInputStream(&encStream);
            decBuf : byte[]! = new byte[8];
            decRes : Expected<unsigned int, ::k::io::StreamOutOfData> = fDec.read(decBuf, 0u, 8u);
            if (decRes.hasError() || decRes.getResult() != 3u) return 3;
            if (decBuf[0] != (byte) 'x' || decBuf[1] != (byte) '=' || decBuf[2] != (byte) 'y') return 4;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_special");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

TEST_CASE("FormUrl: OutputStream encoding and decoding", "[libk][io][codec][formurl][stream]") {
    auto jit = jit_k(R"SRC(
        module __test_fu_stream__;

        test_out_stream() : int {
            outArray : k::io::ArrayOutputStream<byte>;
            fOut : k::io::FormUrlOutputStream(&outArray, true);
            raw : byte[]! = new byte[4];
            raw[0] = (byte) 'a'; raw[1] = (byte) ' '; raw[2] = (byte) 'b'; raw[3] = (byte) '!';
            fOut.write(raw, 0u, 4u);
            fOut.close();

            enc : byte[]! = outArray.toArray();
            if (enc.size != 6) return 1; // "a+b%21"
            if (enc[0] != (byte) 'a' || enc[1] != (byte) '+' || enc[2] != (byte) 'b') return 2;
            if (enc[3] != (byte) '%' || enc[4] != (byte) '2' || enc[5] != (byte) '1') return 3;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_out_stream");
    REQUIRE(fn);
    CHECK(fn() == 0);
}
