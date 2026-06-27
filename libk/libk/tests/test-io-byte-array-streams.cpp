/*
 * K Language standard library — I/O Array stream tests
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
 * Tests for k::io::ArrayInputStream<byte> and k::io::ArrayOutputStream<byte>.
 *
 * Exercises write, read, size, toArray, reset, skip, available and
 * round-trip (write to BAOS → extract → read from BAIS).
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
// ArrayOutputStream — default construction
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("ArrayOutputStream default ctor — size is 0", "[libk][io][baos]") {
    auto jit = jit_k(R"SRC(
        module __baos_default__;

        test_size() : int {
            baos : k::io::ArrayOutputStream<byte>;
            return baos.size();
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_size");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// ArrayOutputStream — write single bytes
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("ArrayOutputStream write single bytes", "[libk][io][baos]") {
    auto jit = jit_k(R"SRC(
        module __baos_write_single__;

        test_size() : int {
            baos : k::io::ArrayOutputStream<byte>;
            baos.write(65);
            baos.write(66);
            baos.write(67);
            return baos.size();
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_size");
    REQUIRE(fn);
    CHECK(fn() == 3);
}

// ═════════════════════════════════════════════════════════════════════════════
// ArrayOutputStream — toArray
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("ArrayOutputStream toArray content", "[libk][io][baos]") {
    auto jit = jit_k(R"SRC(
        module __baos_toarray__;

        test_content() : int {
            baos : k::io::ArrayOutputStream<byte>;
            baos.write(10);
            baos.write(20);
            baos.write(30);
            arr : byte[]* = baos.toArray();
            if (arr[0] != (byte) 10) return 1;
            if (arr[1] != (byte) 20) return 2;
            if (arr[2] != (byte) 30) return 3;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_content");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// ArrayOutputStream — reset
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("ArrayOutputStream reset clears size", "[libk][io][baos]") {
    auto jit = jit_k(R"SRC(
        module __baos_reset__;

        test_reset() : int {
            baos : k::io::ArrayOutputStream<byte>;
            baos.write(1);
            baos.write(2);
            baos.reset();
            return baos.size();
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_reset");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// ArrayInputStream — read single bytes and EOF
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("ArrayInputStream read returns bytes then -1", "[libk][io][bais]") {
    auto jit = jit_k(R"SRC(
        module __bais_read__;

        test_read() : int {
            sz : int = 3;
            buf : byte[]! = new byte[sz];
            buf[0] = (byte) 10;
            buf[1] = (byte) 20;
            buf[2] = (byte) 30;
            bais : k::io::ArrayInputStream<byte>(buf, 3);
            v0 : int = (int)(unsigned byte) bais.read().getOr((byte) 0);
            v1 : int = (int)(unsigned byte) bais.read().getOr((byte) 0);
            v2 : int = (int)(unsigned byte) bais.read().getOr((byte) 0);
            atEof : bool = bais.read().hasValue();
            if (v0 != 10) return 1;
            if (v1 != 20) return 2;
            if (v2 != 30) return 3;
            if (atEof) return 4;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_read");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// ArrayInputStream — available
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("ArrayInputStream available", "[libk][io][bais]") {
    auto jit = jit_k(R"SRC(
        module __bais_available__;

        test_available() : int {
            sz : int = 5;
            buf : byte[]! = new byte[sz];
            buf[0] = (byte) 1; buf[1] = (byte) 2; buf[2] = (byte) 3;
            buf[3] = (byte) 4; buf[4] = (byte) 5;
            bais : k::io::ArrayInputStream<byte>(buf, 5);
            a0 : int = (int) bais.available().getResultOr((unsigned int) 0);
            bais.read();
            bais.read();
            a1 : int = (int) bais.available().getResultOr((unsigned int) 0);
            if (a0 != 5) return 1;
            if (a1 != 3) return 2;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_available");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// ArrayInputStream — skip
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("ArrayInputStream skip", "[libk][io][bais]") {
    auto jit = jit_k(R"SRC(
        module __bais_skip__;

        test_skip() : int {
            sz : int = 5;
            buf : byte[]! = new byte[sz];
            buf[0] = (byte) 10; buf[1] = (byte) 20; buf[2] = (byte) 30;
            buf[3] = (byte) 40; buf[4] = (byte) 50;
            bais : k::io::ArrayInputStream<byte>(buf, 5);
            skipped : long = bais.skip(3);
            val : int = (int)(unsigned byte) bais.read().getOr((byte) 0);
            if (skipped != 3) return 1;
            if (val != 40) return 2;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_skip");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// ArrayInputStream — bulk read
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("ArrayInputStream bulk read", "[libk][io][bais]") {
    auto jit = jit_k(R"SRC(
        module __bais_bulk_read__;

        test_bulk_read() : int {
            sz : int = 4;
            src : byte[]! = new byte[sz];
            src[0] = (byte) 1; src[1] = (byte) 2;
            src[2] = (byte) 3; src[3] = (byte) 4;
            bais : k::io::ArrayInputStream<byte>(src, 4);

            dsz : int = 4;
            dst : byte[]! = new byte[dsz];
            n : int = (int) bais.read(dst, 0, 4).getResultOr((unsigned int) 0);
            if (n != 4) return 1;
            if (dst[0] != (byte) 1) return 2;
            if (dst[1] != (byte) 2) return 3;
            if (dst[2] != (byte) 3) return 4;
            if (dst[3] != (byte) 4) return 5;
            // Next read at EOS should return 0 (no bytes read, stream still open)
            n2 : int = (int) bais.read(dst, 0, 4).getResultOr((unsigned int) 0);
            if (n2 != 0) return 6;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_bulk_read");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// Round-trip: BAOS → toArray → BAIS
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Round-trip BAOS to BAIS", "[libk][io][baos][bais]") {
    auto jit = jit_k(R"SRC(
        module __baos_bais_roundtrip__;

        test_roundtrip() : int {
            baos : k::io::ArrayOutputStream<byte>;
            baos.write(100);
            baos.write(200);
            baos.write(42);

            arr : byte[]* = baos.toArray();
            bais : k::io::ArrayInputStream<byte>(arr, baos.size());

            v0 : int = (int)(unsigned byte) bais.read().getOr((byte) 0);
            v1 : int = (int)(unsigned byte) bais.read().getOr((byte) 0);
            v2 : int = (int)(unsigned byte) bais.read().getOr((byte) 0);
            atEof : bool = bais.read().hasValue();

            if (v0 != 100) return 1;
            if (v1 != 200) return 2;
            if (v2 != 42) return 3;
            if (atEof) return 4;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_roundtrip");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// ArrayOutputStream — bulk write
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("ArrayOutputStream bulk write", "[libk][io][baos]") {
    auto jit = jit_k(R"SRC(
        module __baos_bulk_write__;

        test_bulk_write() : int {
            baos : k::io::ArrayOutputStream<byte>;
            sz : int = 3;
            src : byte[]! = new byte[sz];
            src[0] = (byte) 11; src[1] = (byte) 22; src[2] = (byte) 33;
            baos.write(src, 0, 3);
            if (baos.size() != 3) return 1;
            arr : byte[]* = baos.toArray();
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
// Copy-initialising an Optional<byte> from an imported method's rvalue return
// value.
//
// Storing the result of an imported method that returns a template instantiation
// (here k::io::ArrayInputStream<byte>::read() -> Optional<byte>) into a local
// variable of the same type:
//
//     o : Optional<byte> = bais.read();
//
// This used to fail (errors 0x000D9 / 0x000FF) because the consumer module ended
// up with TWO distinct struct_type objects for the same instantiation
// `Optional__byte`: one created by the KDI importer (used as read()'s return
// type) and one synthesised locally so the consumer could construct the value
// with a real body. The pointer-based struct-identity checks then rejected
// binding the imported rvalue to the local instantiation's copy constructor.
//
// Fixed by unifying both into a single struct_type via a registry on
// k::model::unit keyed by the mangled short instantiation name (see
// unit::_instantiation_struct_types, consulted by get_or_create_imported_aggregate
// and try_instantiate_template_type).
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("ArrayInputStream read() rvalue copy-init into Optional<byte>",
          "[libk][io][bais]") {
    auto jit = jit_k(R"SRC(
        module __bais_opt_copy_init__;

        test_copy_init() : int {
            sz : int = 1;
            buf : byte[]! = new byte[sz];
            buf[0] = (byte) 42;
            bais : k::io::ArrayInputStream<byte>(buf, 1);
            // Copy-initialise from an imported method's rvalue of the same
            // template instantiation (formerly unsupported).
            o : Optional<byte> = bais.read();
            if (!o.hasValue()) return 1;
            if ((int)(unsigned byte) o.get() != 42) return 2;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_copy_init");
    REQUIRE(fn);
    CHECK(fn() == 0);
}
