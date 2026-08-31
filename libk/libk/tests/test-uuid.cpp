/*
 * K Language standard library — UUID and Cryptographic Hash tests
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

#include "../../klang/tests/helpers.hpp"

#ifndef LIBK_KDI_DIR
#error "LIBK_KDI_DIR not defined — set via CMake target_compile_definitions"
#endif
#ifndef LIBK_LIB_DIR
#error "LIBK_LIB_DIR not defined — set via CMake target_compile_definitions"
#endif

namespace {

std::unique_ptr<k::model::gen::jit> jit_k(std::string_view src) {
    return gen_jit_with_stdlib(src, LIBK_KDI_DIR, LIBK_LIB_DIR);
}

} // anonymous namespace

TEST_CASE("MD5: RFC 1321 test vectors", "[libk][crypto][md5]") {
    auto jit = jit_k(R"SRC(
        module __test_md5__;

        hexNibble(n: unsigned int) : byte {
            v : unsigned int = n & 0x0Fu;
            if (v < 10u) return (byte) (((unsigned int) '0') + v);
            return (byte) (((unsigned int) 'a') + (v - 10u));
        }

        bytesToHex(b: const byte[]) : k::String {
            buf : unsigned byte[]! = new unsigned byte[b.size * 2u + 1u];
            i : unsigned int = 0u;
            while (i < b.size) {
                ub : unsigned int = ((unsigned int) b[i]) & 0xFFu;
                buf[i * 2u] = (unsigned byte) hexNibble(ub >> 4u);
                buf[i * 2u + 1u] = (unsigned byte) hexNibble(ub);
                ++i;
            }
            buf[b.size * 2u] = (unsigned byte) 0;
            s : k::String(buf);
            delete buf;
            return s;
        }

        test() : int {
            res : int = 0;

            // Vector 1: "" -> d41d8cd98f00b204e9800998ecf8427e
            emptyBytes : byte[]! = new byte[0u];
            d1 : byte[]! = k::crypto::Md5::digest(emptyBytes);
            delete emptyBytes;
            if (bytesToHex(d1) == k::String("d41d8cd98f00b204e9800998ecf8427e")) { res += 1; }
            delete d1;

            // Vector 2: "a" -> 0cc175b9c0f1b6a831c399e269772661
            aBytes : byte[]! = new byte[1u];
            aBytes[0] = (byte) 'a';
            d2 : byte[]! = k::crypto::Md5::digest(aBytes);
            delete aBytes;
            if (bytesToHex(d2) == k::String("0cc175b9c0f1b6a831c399e269772661")) { res += 2; }
            delete d2;

            // Vector 3: "abc" -> 900150983cd24fb0d6963f7d28e17f72
            abcBytes : byte[]! = new byte[3u];
            abcBytes[0] = (byte) 'a';
            abcBytes[1] = (byte) 'b';
            abcBytes[2] = (byte) 'c';
            d3 : byte[]! = k::crypto::Md5::digest(abcBytes);
            delete abcBytes;
            if (bytesToHex(d3) == k::String("900150983cd24fb0d6963f7d28e17f72")) { res += 4; }
            delete d3;

            // Vector 4: "message digest" -> f96b697d7cb7938d525a2f31aaf161d0
            msg : k::String("message digest");
            u8 : unsigned byte[]! = msg.toUtf8();
            n : unsigned int = (u8.size > 1u) ? (u8.size - 1u) : 0u;
            rawMsg : byte[]! = new byte[n];
            idx : unsigned int = 0u;
            while (idx < n) {
                rawMsg[idx] = (byte) u8[idx];
                ++idx;
            }
            delete u8;
            d4 : byte[]! = k::crypto::Md5::digest(rawMsg);
            delete rawMsg;
            if (bytesToHex(d4) == k::String("f96b697d7cb7938d525a2f31aaf161d0")) { res += 8; }
            delete d4;

            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == (1 + 2 + 4 + 8));
}

TEST_CASE("SHA-1: RFC 3174 test vectors", "[libk][crypto][sha1]") {
    auto jit = jit_k(R"SRC(
        module __test_sha1__;

        hexNibble(n: unsigned int) : byte {
            v : unsigned int = n & 0x0Fu;
            if (v < 10u) return (byte) (((unsigned int) '0') + v);
            return (byte) (((unsigned int) 'a') + (v - 10u));
        }

        bytesToHex(b: const byte[]) : k::String {
            buf : unsigned byte[]! = new unsigned byte[b.size * 2u + 1u];
            i : unsigned int = 0u;
            while (i < b.size) {
                ub : unsigned int = ((unsigned int) b[i]) & 0xFFu;
                buf[i * 2u] = (unsigned byte) hexNibble(ub >> 4u);
                buf[i * 2u + 1u] = (unsigned byte) hexNibble(ub);
                ++i;
            }
            buf[b.size * 2u] = (unsigned byte) 0;
            s : k::String(buf);
            delete buf;
            return s;
        }

        test() : int {
            res : int = 0;

            // Vector 1: "" -> da39a3ee5e6b4b0d3255bfef95601890afd80709
            emptyBytes : byte[]! = new byte[0u];
            d1 : byte[]! = k::crypto::Sha1::digest(emptyBytes);
            delete emptyBytes;
            if (bytesToHex(d1) == k::String("da39a3ee5e6b4b0d3255bfef95601890afd80709")) { res += 1; }
            delete d1;

            // Vector 2: "abc" -> a9993e364706816aba3e25717850c26c9cd0d89d
            abcBytes : byte[]! = new byte[3u];
            abcBytes[0] = (byte) 'a';
            abcBytes[1] = (byte) 'b';
            abcBytes[2] = (byte) 'c';
            d2 : byte[]! = k::crypto::Sha1::digest(abcBytes);
            delete abcBytes;
            if (bytesToHex(d2) == k::String("a9993e364706816aba3e25717850c26c9cd0d89d")) { res += 2; }
            delete d2;

            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == (1 + 2));
}

TEST_CASE("Uuid: Nil, Max, and Predefined Namespaces", "[libk][uuid]") {
    auto jit = jit_k(R"SRC(
        module __test_uuid_constants__;

        test() : int {
            res : int = 0;

            nilId : k::Uuid = k::Uuid::nil();
            if (nilId.isNil()) { res += 1; }
            if (nilId.version() == 0) { res += 2; }
            if (nilId.variant() == 0) { res += 4; }
            if (nilId.toString() == k::String("00000000-0000-0000-0000-000000000000")) { res += 8; }

            maxId : k::Uuid = k::Uuid::max();
            if (maxId.isMax()) { res += 16; }
            if (maxId.toString() == k::String("ffffffff-ffff-ffff-ffff-ffffffffffff")) { res += 32; }

            dns : k::Uuid = k::Uuid::namespaceDns();
            if (dns.toString() == k::String("6ba7b810-9dad-11d1-80b4-00c04fd430c8")) { res += 64; }
            if (dns.version() == 1) { res += 128; }
            if (dns.variant() == 2) { res += 256; }

            url : k::Uuid = k::Uuid::namespaceUrl();
            if (url.toString() == k::String("6ba7b811-9dad-11d1-80b4-00c04fd430c8")) { res += 512; }

            oid : k::Uuid = k::Uuid::namespaceOid();
            if (oid.toString() == k::String("6ba7b812-9dad-11d1-80b4-00c04fd430c8")) { res += 1024; }

            x500 : k::Uuid = k::Uuid::namespaceX500();
            if (x500.toString() == k::String("6ba7b814-9dad-11d1-80b4-00c04fd430c8")) { res += 2048; }

            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == (1 + 2 + 4 + 8 + 16 + 32 + 64 + 128 + 256 + 512 + 1024 + 2048));
}

TEST_CASE("Uuid: Version 1 and Version 6 (Time-based)", "[libk][uuid]") {
    auto jit = jit_k(R"SRC(
        module __test_uuid_v1_v6__;

        test() : int {
            res : int = 0;

            node : unsigned long = 0x00c04fd430c8uL;
            cs : int = 0x1234;
            ts : unsigned long = 0x1d19dad6ba7b810uL;

            // v1 test
            u1 : k::Uuid = k::Uuid::v1(node, cs, ts);
            if (u1.version() == 1) { res += 1; }
            if (u1.variant() == 2) { res += 2; }
            if (u1.timestamp() == ts) { res += 4; }
            if (u1.clockSequence() == (cs & 0x3FFF)) { res += 8; }
            if (u1.node() == node) { res += 16; }

            // v6 test (RFC 9562 reordered timestamp)
            u6 : k::Uuid = k::Uuid::v6(node, cs, ts);
            if (u6.version() == 6) { res += 32; }
            if (u6.variant() == 2) { res += 64; }
            if (u6.timestamp() == ts) { res += 128; }
            if (u6.clockSequence() == (cs & 0x3FFF)) { res += 256; }
            if (u6.node() == node) { res += 512; }

            // Dynamic v1 and v6
            dyn1 : k::Uuid = k::Uuid::v1();
            if (dyn1.version() == 1) { res += 1024; }
            if (dyn1.variant() == 2) { res += 2048; }

            dyn6 : k::Uuid = k::Uuid::v6();
            if (dyn6.version() == 6) { res += 4096; }
            if (dyn6.variant() == 2) { res += 8192; }

            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == (1 + 2 + 4 + 8 + 16 + 32 + 64 + 128 + 256 + 512 + 1024 + 2048 + 4096 + 8192));
}

TEST_CASE("Uuid: Version 2 (DCE Security)", "[libk][uuid]") {
    auto jit = jit_k(R"SRC(
        module __test_uuid_v2__;

        test() : int {
            res : int = 0;

            node : unsigned long = 0x00c04fd430c8uL;
            cs : int = 0x2A;
            ts : unsigned long = 0x1d19dad6ba7b810uL;
            domain : byte = (byte) 1;
            localId : unsigned int = 1000u;

            u2 : k::Uuid = k::Uuid::v2(domain, localId, node, cs, ts);
            if (u2.version() == 2) { res += 1; }
            if (u2.variant() == 2) { res += 2; }
            if (u2.localDomain() == domain) { res += 4; }
            if (u2.localIdentifier() == localId) { res += 8; }
            if (u2.node() == node) { res += 16; }

            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == (1 + 2 + 4 + 8 + 16));
}

TEST_CASE("Uuid: Version 3 (MD5) and Version 5 (SHA-1) RFC 4122 Vectors", "[libk][uuid]") {
    auto jit = jit_k(R"SRC(
        module __test_uuid_v3_v5__;

        test() : int {
            res : int = 0;

            dns : k::Uuid = k::Uuid::namespaceDns();

            // Vector 1: "www.widgets.com"
            v3_widgets : k::Uuid = k::Uuid::v3(dns, k::String("www.widgets.com"));
            if (v3_widgets.version() == 3) { res += 1; }
            if (v3_widgets.variant() == 2) { res += 2; }
            if (v3_widgets.toString() == k::String("3d813cbb-47fb-32ba-91df-831e1593ac29")) { res += 4; }

            v5_widgets : k::Uuid = k::Uuid::v5(dns, k::String("www.widgets.com"));
            if (v5_widgets.version() == 5) { res += 8; }
            if (v5_widgets.variant() == 2) { res += 16; }
            if (v5_widgets.toString() == k::String("21f7f8de-8051-5b89-8680-0195ef798b6a")) { res += 32; }

            // Vector 2: "python.org"
            v3_python : k::Uuid = k::Uuid::v3(dns, k::String("python.org"));
            if (v3_python.toString() == k::String("6fa459ea-ee8a-3ca4-894e-db77e160355e")) { res += 64; }

            v5_python : k::Uuid = k::Uuid::v5(dns, k::String("python.org"));
            if (v5_python.toString() == k::String("886313e1-3b8a-5372-9b90-0c9aee199e5d")) { res += 128; }

            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == (1 + 2 + 4 + 8 + 16 + 32 + 64 + 128));
}

TEST_CASE("Uuid: Version 4 (Random) and Version 7 (Unix Millis) and Version 8 (Custom)", "[libk][uuid]") {
    auto jit = jit_k(R"SRC(
        module __test_uuid_v4_v7_v8__;

        test() : int {
            res : int = 0;

            // v4
            r1 : k::Uuid = k::Uuid::v4();
            r2 : k::Uuid = k::Uuid::random();
            if (r1.version() == 4) { res += 1; }
            if (r1.variant() == 2) { res += 2; }
            if (r1 != r2) { res += 4; }

            // v7
            ts : unsigned long = 1700000000000uL;
            u7 : k::Uuid = k::Uuid::v7(ts, 0x123u, 0x1234567890123456uL);
            if (u7.version() == 7) { res += 8; }
            if (u7.variant() == 2) { res += 16; }
            if (u7.unixTimestampMillis() == ts) { res += 32; }
            if (u7.subSequence() == 0x123u) { res += 64; }

            u7_now : k::Uuid = k::Uuid::v7();
            if (u7_now.version() == 7) { res += 128; }
            if (u7_now.unixTimestampMillis() > 0uL) { res += 256; }

            // v8
            u8 : k::Uuid = k::Uuid::v8(0x1234567890abcdefuL, 0xfedcba0987654321uL);
            if (u8.version() == 8) { res += 512; }
            if (u8.variant() == 2) { res += 1024; }

            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == (1 + 2 + 4 + 8 + 16 + 32 + 64 + 128 + 256 + 512 + 1024));
}

TEST_CASE("Uuid: Parsing, Formatting and Operators", "[libk][uuid]") {
    auto jit = jit_k(R"SRC(
        module __test_uuid_parse_ops__;

        test() : int {
            res : int = 0;

            orig : k::Uuid = k::Uuid::namespaceDns();

            // Format standard and URN
            s : k::String = orig.toString();
            if (s == k::String("6ba7b810-9dad-11d1-80b4-00c04fd430c8")) { res += 1; }
            urn : k::String = orig.toUrn();
            if (urn == k::String("urn:uuid:6ba7b810-9dad-11d1-80b4-00c04fd430c8")) { res += 2; }

            // Parse hyphenated
            p1 : k::Uuid = k::Uuid::fromString(s);
            if (p1 == orig) { res += 4; }

            // Parse uppercase
            upper : k::String("6BA7B810-9DAD-11D1-80B4-00C04FD430C8");
            p2 : k::Uuid = k::Uuid::fromString(upper);
            if (p2 == orig) { res += 8; }

            // Parse compact 32-char
            compact : k::String("6ba7b8109dad11d180b400c04fd430c8");
            p3 : k::Uuid = k::Uuid::fromString(compact);
            if (p3 == orig) { res += 16; }

            // Parse URN
            p4 : k::Uuid = k::Uuid::fromString(urn);
            if (p4 == orig) { res += 32; }

            // Rejection of invalid
            bad1 : k::Optional<k::Uuid> = k::Uuid::tryParse(k::String("not-a-uuid"));
            if (!bad1.hasValue()) { res += 64; }

            bad2 : k::Optional<k::Uuid> = k::Uuid::tryParse(k::String("6ba7b810-9dad-11d1-80b4-00c04fd430cZ"));
            if (!bad2.hasValue()) { res += 128; }

            // Byte array round-trip
            bytes : byte[]! = orig.toByteArray();
            fromBytes : k::Uuid = k::Uuid::fromBytes(bytes);
            delete bytes;
            if (fromBytes == orig) { res += 256; }

            // Comparison operators
            uSmall : k::Uuid = k::Uuid::fromBits(1uL, 2uL);
            uBig : k::Uuid = k::Uuid::fromBits(1uL, 3uL);
            uBigger : k::Uuid = k::Uuid::fromBits(2uL, 1uL);

            if (uSmall < uBig) { res += 512; }
            if (uBig < uBigger) { res += 1024; }
            if (uSmall.compareTo(uBig) == -1) { res += 2048; }
            if (uSmall.compareTo(uSmall) == 0) { res += 4096; }
            if (uBig.compareTo(uSmall) == 1) { res += 8192; }

            // Hash code
            if (orig.hashCode() != 0uL) { res += 16384; }

            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == (1 + 2 + 4 + 8 + 16 + 32 + 64 + 128 + 256 + 512 + 1024 + 2048 + 4096 + 8192 + 16384));
}
