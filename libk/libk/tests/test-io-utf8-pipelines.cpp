/*
 * K Language standard library — UTF-8 Native Pipelines tests
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

TEST_CASE("UTF-8 Pipelines: Utf8Validator stream validation", "[libk][io][utf8][pipeline]") {
    auto jit = jit_k(R"SRC(
        module __test_utf8_pipeline__;

        test_utf8_validator_streaming() : int {
            // "Hello " (6) + "é" (2) + " " (1) + rocket (4) = 13 bytes
            bytes : byte[]! = new byte[13];
            bytes[0] = (byte) 'H';
            bytes[1] = (byte) 'e';
            bytes[2] = (byte) 'l';
            bytes[3] = (byte) 'l';
            bytes[4] = (byte) 'o';
            bytes[5] = (byte) ' ';
            bytes[6] = (byte) 0xC3;
            bytes[7] = (byte) 0xA9;
            bytes[8] = (byte) ' ';
            bytes[9] = (byte) 0xF0;
            bytes[10] = (byte) 0x9F;
            bytes[11] = (byte) 0x9A;
            bytes[12] = (byte) 0x80;

            bais : k::io::ArrayInputStream<byte>(bytes, 13);
            validator : k::io::Utf8Validator(&bais);

            // Read units directly through Utf8Validator without UTF-32 conversion
            outBuf : unsigned byte[]! = new unsigned byte[13];
            n : int = (int) validator.read(outBuf, 0u, 13u).getResultOr(0u);
            if (n != 13) return 1;

            if (outBuf[0] != (unsigned byte) 'H') return 2;
            if (outBuf[6] != (unsigned byte) 0xC3 || outBuf[7] != (unsigned byte) 0xA9) return 3;
            if (outBuf[9] != (unsigned byte) 0xF0 || outBuf[12] != (unsigned byte) 0x80) return 4;

            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_utf8_validator_streaming");
    REQUIRE(fn);
    CHECK(fn() == 0);
}
