/*
 * K Language standard library — I/O Data stream tests
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
 * Tests for k::io::DataInputStream and k::io::DataOutputStream.
 *
 * Verifies round-trip encoding/decoding of all primitive types in native
 * byte order, and readFully.
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
// Round-trip: byte
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("DataStream round-trip byte", "[libk][io][data]") {
    auto jit = jit_k(R"SRC(
        module __ds_byte__;

        test_byte() : int {
            baos : k::io::ByteArrayOutputStream;
            dos : k::io::DataOutputStream(&baos);
            dos.writeByte((byte) 42);
            dos.writeByte((byte) -1);

            arr : byte[]* = baos.toByteArray();
            bais : k::io::ByteArrayInputStream(arr, baos.size());
            dis : k::io::DataInputStream(&bais);

            v0 : byte = dis.readByte();
            v1 : byte = dis.readByte();
            if (v0 != (byte) 42) return 1;
            if (v1 != (byte) -1) return 2;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_byte");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// Round-trip: bool
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("DataStream round-trip bool", "[libk][io][data]") {
    auto jit = jit_k(R"SRC(
        module __ds_bool__;

        test_bool() : int {
            baos : k::io::ByteArrayOutputStream;
            dos : k::io::DataOutputStream(&baos);
            dos.writeBool(true);
            dos.writeBool(false);

            arr : byte[]* = baos.toByteArray();
            bais : k::io::ByteArrayInputStream(arr, baos.size());
            dis : k::io::DataInputStream(&bais);

            v0 : bool = dis.readBool();
            v1 : bool = dis.readBool();
            if (!v0) return 1;
            if (v1) return 2;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_bool");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// Round-trip: short
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("DataStream round-trip short", "[libk][io][data]") {
    auto jit = jit_k(R"SRC(
        module __ds_short__;

        test_short() : int {
            baos : k::io::ByteArrayOutputStream;
            dos : k::io::DataOutputStream(&baos);
            dos.writeShort((short) 12345);
            dos.writeShort((short) -1);

            arr : byte[]* = baos.toByteArray();
            bais : k::io::ByteArrayInputStream(arr, baos.size());
            dis : k::io::DataInputStream(&bais);

            v0 : short = dis.readShort();
            v1 : short = dis.readShort();
            if (v0 != (short) 12345) return 1;
            if (v1 != (short) -1) return 2;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_short");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// Round-trip: int (positive, negative, zero)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("DataStream round-trip int", "[libk][io][data]") {
    auto jit = jit_k(R"SRC(
        module __ds_int__;

        test_int() : int {
            baos : k::io::ByteArrayOutputStream;
            dos : k::io::DataOutputStream(&baos);
            dos.writeInt(305419896);
            dos.writeInt(-1);
            dos.writeInt(0);

            arr : byte[]* = baos.toByteArray();
            bais : k::io::ByteArrayInputStream(arr, baos.size());
            dis : k::io::DataInputStream(&bais);

            v0 : int = dis.readInt();
            v1 : int = dis.readInt();
            v2 : int = dis.readInt();
            if (v0 != 305419896) return 1;
            if (v1 != -1) return 2;
            if (v2 != 0) return 3;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_int");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// Round-trip: long
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("DataStream round-trip long", "[libk][io][data]") {
    auto jit = jit_k(R"SRC(
        module __ds_long__;

        test_long() : int {
            baos : k::io::ByteArrayOutputStream;
            dos : k::io::DataOutputStream(&baos);
            dos.writeLong(1234567890123);
            dos.writeLong(-1);

            arr : byte[]* = baos.toByteArray();
            bais : k::io::ByteArrayInputStream(arr, baos.size());
            dis : k::io::DataInputStream(&bais);

            v0 : long = dis.readLong();
            v1 : long = dis.readLong();
            if (v0 != 1234567890123) return 1;
            if (v1 != -1) return 2;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_long");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// Round-trip: byte (unsigned, value > 127)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("DataStream round-trip byte high value", "[libk][io][data]") {
    auto jit = jit_k(R"SRC(
        module __ds_ubyte__;

        test_ubyte() : int {
            baos : k::io::ByteArrayOutputStream;
            dos : k::io::DataOutputStream(&baos);
            dos.writeByte((byte) 200);

            arr : byte[]* = baos.toByteArray();
            bais : k::io::ByteArrayInputStream(arr, baos.size());
            dis : k::io::DataInputStream(&bais);

            v : byte = dis.readByte();
            if (v != (byte) 200) return 1;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_ubyte");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// Round-trip: float
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("DataStream round-trip float", "[libk][io][data]") {
    auto jit = jit_k(R"SRC(
        module __ds_float__;

        test_float() : int {
            baos : k::io::ByteArrayOutputStream;
            dos : k::io::DataOutputStream(&baos);
            dos.writeFloat(3.14f);

            arr : byte[]* = baos.toByteArray();
            bais : k::io::ByteArrayInputStream(arr, baos.size());
            dis : k::io::DataInputStream(&bais);

            v : float = dis.readFloat();
            // Check approximate equality (should be exact for IEEE 754 round-trip)
            diff : float = v - 3.14f;
            if (diff < -0.001f) return 1;
            if (diff > 0.001f) return 2;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_float");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// Round-trip: double
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("DataStream round-trip double", "[libk][io][data]") {
    auto jit = jit_k(R"SRC(
        module __ds_double__;

        test_double() : int {
            baos : k::io::ByteArrayOutputStream;
            dos : k::io::DataOutputStream(&baos);
            dos.writeDouble(2.718281828);

            arr : byte[]* = baos.toByteArray();
            bais : k::io::ByteArrayInputStream(arr, baos.size());
            dis : k::io::DataInputStream(&bais);

            v : double = dis.readDouble();
            diff : double = v - 2.718281828;
            if (diff < -0.000001) return 1;
            if (diff > 0.000001) return 2;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_double");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// DataOutputStream — size() tracks bytes written
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("DataOutputStream size() tracks bytes written", "[libk][io][data]") {
    auto jit = jit_k(R"SRC(
        module __ds_size__;

        test_size() : int {
            baos : k::io::ByteArrayOutputStream;
            dos : k::io::DataOutputStream(&baos);
            dos.writeByte((byte) 1);
            dos.writeInt(42);
            dos.writeLong(100);
            // 1 + 4 + 8 = 13
            return dos.size();
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_size");
    REQUIRE(fn);
    CHECK(fn() == 13);
}

// ═════════════════════════════════════════════════════════════════════════════
// readFully
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("DataInputStream readFully", "[libk][io][data]") {
    auto jit = jit_k(R"SRC(
        module __ds_readfully__;

        test_readfully() : int {
            baos : k::io::ByteArrayOutputStream;
            dos : k::io::DataOutputStream(&baos);
            dos.writeByte((byte) 10);
            dos.writeByte((byte) 20);
            dos.writeByte((byte) 30);
            dos.writeByte((byte) 40);

            arr : byte[]* = baos.toByteArray();
            bais : k::io::ByteArrayInputStream(arr, baos.size());
            dis : k::io::DataInputStream(&bais);

            dsz : int = 4;
            dst : byte[]! = new byte[dsz];
            dis.readFully(dst, 0, 4);
            if (dst[0] != (byte) 10) return 1;
            if (dst[1] != (byte) 20) return 2;
            if (dst[2] != (byte) 30) return 3;
            if (dst[3] != (byte) 40) return 4;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_readfully");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// Mixed primitives round-trip
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("DataStream mixed primitives round-trip", "[libk][io][data]") {
    auto jit = jit_k(R"SRC(
        module __ds_mixed__;

        test_mixed() : int {
            baos : k::io::ByteArrayOutputStream;
            dos : k::io::DataOutputStream(&baos);
            dos.writeBool(true);
            dos.writeByte((byte) 42);
            dos.writeShort((short) 1000);
            dos.writeInt(123456);
            dos.writeLong(9876543210);

            arr : byte[]* = baos.toByteArray();
            bais : k::io::ByteArrayInputStream(arr, baos.size());
            dis : k::io::DataInputStream(&bais);

            vBool : bool = dis.readBool();
            vByte : byte = dis.readByte();
            vShort : short = dis.readShort();
            vInt : int = dis.readInt();
            vLong : long = dis.readLong();

            if (!vBool) return 1;
            if (vByte != (byte) 42) return 2;
            if (vShort != (short) 1000) return 3;
            if (vInt != 123456) return 4;
            if (vLong != 9876543210) return 5;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_mixed");
    REQUIRE(fn);
    CHECK(fn() == 0);
}




