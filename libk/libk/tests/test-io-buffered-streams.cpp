/*
 * K Language standard library — I/O Buffered stream tests
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
 * Tests for k::io::BufferedInputStream and k::io::BufferedOutputStream.
 *
 * Verifies buffered reading and writing with various sizes relative to the
 * internal buffer, and that flush() / close() force buffered data through.
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
// BufferedInputStream — single byte reads through small buffer
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("BufferedInputStream single byte reads", "[libk][io][buffered]") {
    auto jit = jit_k(R"SRC(
        module __bis_single__;

        test_read() : int {
            sz : int = 5;
            data : byte[]! = new byte[sz];
            data[0] = (byte) 10; data[1] = (byte) 20; data[2] = (byte) 30;
            data[3] = (byte) 40; data[4] = (byte) 50;
            bais : k::io::ArrayInputStream<byte>(data, 5);
            bis : k::io::BufferedInputStream(&bais, 3);

            v0 : int = (int)(unsigned byte) bis.read().getOr((byte) 0);
            v1 : int = (int)(unsigned byte) bis.read().getOr((byte) 0);
            v2 : int = (int)(unsigned byte) bis.read().getOr((byte) 0);
            v3 : int = (int)(unsigned byte) bis.read().getOr((byte) 0);
            v4 : int = (int)(unsigned byte) bis.read().getOr((byte) 0);
            atEof : bool = bis.read().hasValue();

            if (v0 != 10) return 1;
            if (v1 != 20) return 2;
            if (v2 != 30) return 3;
            if (v3 != 40) return 4;
            if (v4 != 50) return 5;
            if (atEof) return 6;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_read");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// BufferedInputStream — bulk read smaller than buffer
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("BufferedInputStream bulk read smaller than buffer", "[libk][io][buffered]") {
    auto jit = jit_k(R"SRC(
        module __bis_bulk_small__;

        test_bulk() : int {
            sz : int = 6;
            data : byte[]! = new byte[sz];
            data[0] = (byte) 1; data[1] = (byte) 2; data[2] = (byte) 3;
            data[3] = (byte) 4; data[4] = (byte) 5; data[5] = (byte) 6;
            bais : k::io::ArrayInputStream<byte>(data, 6);
            bis : k::io::BufferedInputStream(&bais, 8);

            dsz : int = 4;
            dst : byte[]! = new byte[dsz];
            n : int = (int) bis.read(dst, 0, 4).getResultOr((unsigned int) 0);
            if (n != 4) return 1;
            if (dst[0] != (byte) 1) return 2;
            if (dst[3] != (byte) 4) return 3;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_bulk");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// BufferedInputStream — available includes buffered + underlying
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("BufferedInputStream available", "[libk][io][buffered]") {
    auto jit = jit_k(R"SRC(
        module __bis_available__;

        test_available() : int {
            sz : int = 10;
            data : byte[]! = new byte[sz];
            i : int = 0;
            while (i < 10) {
                data[i] = (byte) i;
                ++i;
            }
            bais : k::io::ArrayInputStream<byte>(data, 10);
            bis : k::io::BufferedInputStream(&bais, 4);
            // Before first read, available = underlying (10)
            a0 : int = (int) bis.available().getResultOr((unsigned int) 0);
            // Read one byte to trigger fill (fills 4 bytes)
            bis.read();
            // After fill: 3 in buffer + 6 in underlying = 9
            a1 : int = (int) bis.available().getResultOr((unsigned int) 0);
            if (a0 != 10) return 1;
            if (a1 != 9) return 2;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_available");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// BufferedOutputStream — flush forces data through
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("BufferedOutputStream flush forces data through", "[libk][io][buffered]") {
    auto jit = jit_k(R"SRC(
        module __bos_flush__;

        test_flush() : int {
            baos : k::io::ArrayOutputStream<byte>;
            bos : k::io::BufferedOutputStream(&baos, 8);

            bos.write(10);
            bos.write(20);
            // Before flush, underlying may or may not have data (buffered)
            sizeBefore : int = baos.size();
            bos.flush();
            sizeAfter : int = baos.size();
            if (sizeAfter != 2) return 1;
            arr : byte[]* = baos.toArray();
            if (arr[0] != (byte) 10) return 2;
            if (arr[1] != (byte) 20) return 3;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_flush");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// BufferedOutputStream — auto-flush when buffer is full
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("BufferedOutputStream auto-flush on full buffer", "[libk][io][buffered]") {
    auto jit = jit_k(R"SRC(
        module __bos_autof__;

        test_auto_flush() : int {
            baos : k::io::ArrayOutputStream<byte>;
            bos : k::io::BufferedOutputStream(&baos, 4);

            // Write 4 bytes to fill buffer
            bos.write(1);
            bos.write(2);
            bos.write(3);
            bos.write(4);
            // Buffer is now full but not yet flushed
            // Write one more — triggers flush of the 4, then buffers the 5th
            bos.write(5);
            sizeAfterOverflow : int = baos.size();
            // After auto-flush, the first 4 bytes should be in baos
            if (sizeAfterOverflow != 4) return 1;
            // Flush the remaining buffered byte
            bos.flush();
            if (baos.size() != 5) return 2;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_auto_flush");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// BufferedOutputStream — close flushes remaining data
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("BufferedOutputStream close flushes", "[libk][io][buffered]") {
    auto jit = jit_k(R"SRC(
        module __bos_close__;

        test_close() : int {
            baos : k::io::ArrayOutputStream<byte>;
            bos : k::io::BufferedOutputStream(&baos, 16);
            bos.write(42);
            bos.write(99);
            bos.close();
            if (baos.size() != 2) return 1;
            arr : byte[]* = baos.toArray();
            if (arr[0] != (byte) 42) return 2;
            if (arr[1] != (byte) 99) return 3;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_close");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// Buffered stream round-trip
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Buffered streams round-trip", "[libk][io][buffered]") {
    auto jit = jit_k(R"SRC(
        module __buffered_roundtrip__;

        test_roundtrip() : int {
            baos : k::io::ArrayOutputStream<byte>;
            bos : k::io::BufferedOutputStream(&baos, 4);

            i : int = 0;
            while (i < 10) {
                bos.write(i + 100);
                ++i;
            }
            bos.flush();

            arr : byte[]* = baos.toArray();
            bais : k::io::ArrayInputStream<byte>(arr, baos.size());
            bis : k::io::BufferedInputStream(&bais, 3);

            i = 0;
            while (i < 10) {
                val : int = (int)(unsigned byte) bis.read().getOr((byte) 0);
                expected : int = i + 100;
                if (val != expected) return i + 1;
                ++i;
            }
            atEof : bool = bis.read().hasValue();
            if (atEof) return 99;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_roundtrip");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

