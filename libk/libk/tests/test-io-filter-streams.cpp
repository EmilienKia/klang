/*
 * K Language standard library — I/O Filter stream tests
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
 * Tests for k::io::FilterInputStream and k::io::FilterOutputStream.
 *
 * Verifies that filter streams correctly delegate all operations to
 * the underlying ByteArray streams.
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
// FilterInputStream — delegates read()
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("FilterInputStream delegates read()", "[libk][io][filter]") {
    auto jit = jit_k(R"SRC(
        module __fis_read__;

        test_read() : int {
            sz : int = 3;
            buf : byte[]! = new byte[sz];
            buf[0] = (byte) 10; buf[1] = (byte) 20; buf[2] = (byte) 30;
            bais : k::io::ByteArrayInputStream(buf, 3);
            fis : k::io::FilterInputStream(&bais);

            v0 : int = fis.read();
            v1 : int = fis.read();
            v2 : int = fis.read();
            eof : int = fis.read();
            if (v0 != 10) return 1;
            if (v1 != 20) return 2;
            if (v2 != 30) return 3;
            if (eof != -1) return 4;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_read");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// FilterInputStream — delegates bulk read
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("FilterInputStream delegates bulk read", "[libk][io][filter]") {
    auto jit = jit_k(R"SRC(
        module __fis_bulk_read__;

        test_bulk_read() : int {
            sz : int = 3;
            src : byte[]! = new byte[sz];
            src[0] = (byte) 5; src[1] = (byte) 10; src[2] = (byte) 15;
            bais : k::io::ByteArrayInputStream(src, 3);
            fis : k::io::FilterInputStream(&bais);

            dsz : int = 3;
            dst : byte[]! = new byte[dsz];
            n : int = fis.read(dst, 0, 3);
            if (n != 3) return 1;
            if (dst[0] != (byte) 5) return 2;
            if (dst[1] != (byte) 10) return 3;
            if (dst[2] != (byte) 15) return 4;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_bulk_read");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// FilterInputStream — delegates available()
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("FilterInputStream delegates available()", "[libk][io][filter]") {
    auto jit = jit_k(R"SRC(
        module __fis_available__;

        test_available() : int {
            sz : int = 5;
            buf : byte[]! = new byte[sz];
            buf[0] = (byte)1; buf[1] = (byte)2; buf[2] = (byte)3;
            buf[3] = (byte)4; buf[4] = (byte)5;
            bais : k::io::ByteArrayInputStream(buf, 5);
            fis : k::io::FilterInputStream(&bais);

            a : int = fis.available();
            if (a != 5) return 1;
            fis.read();
            a = fis.available();
            if (a != 4) return 2;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_available");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// FilterOutputStream — delegates write()
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("FilterOutputStream delegates write()", "[libk][io][filter]") {
    auto jit = jit_k(R"SRC(
        module __fos_write__;

        test_write() : int {
            baos : k::io::ByteArrayOutputStream;
            fos : k::io::FilterOutputStream(&baos);
            fos.write(65);
            fos.write(66);
            if (baos.size() != 2) return 1;
            arr : byte[]* = baos.toByteArray();
            if (arr[0] != (byte) 65) return 2;
            if (arr[1] != (byte) 66) return 3;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_write");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// FilterOutputStream — delegates bulk write
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("FilterOutputStream delegates bulk write", "[libk][io][filter]") {
    auto jit = jit_k(R"SRC(
        module __fos_bulk_write__;

        test_bulk_write() : int {
            baos : k::io::ByteArrayOutputStream;
            fos : k::io::FilterOutputStream(&baos);
            sz : int = 3;
            src : byte[]! = new byte[sz];
            src[0] = (byte) 11; src[1] = (byte) 22; src[2] = (byte) 33;
            fos.write(src, 0, 3);
            if (baos.size() != 3) return 1;
            arr : byte[]* = baos.toByteArray();
            if (arr[0] != (byte) 11) return 2;
            if (arr[1] != (byte) 22) return 3;
            if (arr[2] != (byte) 33) return 4;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_bulk_write");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// FilterInputStream — delegates skip()
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("FilterInputStream delegates skip()", "[libk][io][filter]") {
    auto jit = jit_k(R"SRC(
        module __fis_skip__;

        test_skip() : int {
            sz : int = 5;
            buf : byte[]! = new byte[sz];
            buf[0] = (byte) 10; buf[1] = (byte) 20; buf[2] = (byte) 30;
            buf[3] = (byte) 40; buf[4] = (byte) 50;
            bais : k::io::ByteArrayInputStream(buf, 5);
            fis : k::io::FilterInputStream(&bais);

            skipped : unsigned long = fis.skip(2uL);
            if (skipped != 2uL) return 1;
            v : int = fis.read();
            if (v != 30) return 2;
            // skip beyond remaining
            skipped = fis.skip(10uL);
            if (skipped != 2uL) return 3;
            eof : int = fis.read();
            if (eof != -1) return 4;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_skip");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// FilterInputStream — delegates close()
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("FilterInputStream delegates close()", "[libk][io][filter]") {
    auto jit = jit_k(R"SRC(
        module __fis_close__;

        test_close() : int {
            sz : int = 2;
            buf : byte[]! = new byte[sz];
            buf[0] = (byte) 1; buf[1] = (byte) 2;
            bais : k::io::ByteArrayInputStream(buf, 2);
            fis : k::io::FilterInputStream(&bais);

            v : int = fis.read();
            if (v != 1) return 1;
            fis.close();
            // After close on ByteArrayInputStream, nothing crashes
            // (BAIS close is a no-op)
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_close");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// FilterOutputStream — delegates flush()
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("FilterOutputStream delegates flush()", "[libk][io][filter]") {
    auto jit = jit_k(R"SRC(
        module __fos_flush__;

        test_flush() : int {
            baos : k::io::ByteArrayOutputStream;
            fos : k::io::FilterOutputStream(&baos);
            fos.write(42);
            fos.flush();
            if (baos.size() != 1) return 1;
            arr : byte[]* = baos.toByteArray();
            if (arr[0] != (byte) 42) return 2;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_flush");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// FilterOutputStream — delegates close()
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("FilterOutputStream delegates close()", "[libk][io][filter]") {
    auto jit = jit_k(R"SRC(
        module __fos_close__;

        test_fos_close() : int {
            baos : k::io::ByteArrayOutputStream;
            fos : k::io::FilterOutputStream(&baos);
            fos.write(99);
            fos.close();
            // Data still in baos after close (BAOS close is no-op)
            if (baos.size() != 1) return 1;
            arr : byte[]* = baos.toByteArray();
            if (arr[0] != (byte) 99) return 2;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_fos_close");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Filter streams round-trip", "[libk][io][filter]") {
    auto jit = jit_k(R"SRC(
        module __filter_roundtrip__;

        test_roundtrip() : int {
            baos : k::io::ByteArrayOutputStream;
            fos : k::io::FilterOutputStream(&baos);
            fos.write(42);
            fos.write(99);
            fos.flush();

            arr : byte[]* = baos.toByteArray();
            bais : k::io::ByteArrayInputStream(arr, baos.size());
            fis : k::io::FilterInputStream(&bais);

            v0 : int = fis.read();
            v1 : int = fis.read();
            eof : int = fis.read();
            if (v0 != 42) return 1;
            if (v1 != 99) return 2;
            if (eof != -1) return 3;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_roundtrip");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

