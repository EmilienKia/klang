/*
 * K Language standard library — ByteBuffer tests (Phase 4)
 *
 * Copyright 2023-2026 Emilien Kia
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
 * Tests for k::io::ByteBuffer.
 *
 * Coverage:
 *  - geometry invariants after allocate / flip / clear / rewind / compact
 *  - relative and absolute get/put
 *  - bulk transfers and toArray()
 *  - wrap() produces a drainable buffer
 *  - overflow and underflow raise IndexOutOfBoundsError
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

TEST_CASE("ByteBuffer: a freshly allocated buffer is in fill mode", "[libk][io][bytebuffer]") {
    auto jit = jit_k(R"SRC(
        module __bb_alloc__;
        test() : int {
            b : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(8u);
            res : int = 0;
            if (b->capacity() == 8u)   { ++res; }
            if (b->position() == 0u)   { res += 2; }
            if (b->limit() == 8u)      { res += 4; }
            if (b->remaining() == 8u)  { res += 8; }
            if (b->hasRemaining())     { res += 16; }
            delete b;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 31);
}

TEST_CASE("ByteBuffer: put then flip switches to drain mode", "[libk][io][bytebuffer]") {
    auto jit = jit_k(R"SRC(
        module __bb_flip__;
        test() : int {
            b : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(8u);
            b->put((byte)10);
            b->put((byte)20);
            b->put((byte)30);
            res : int = 0;
            if (b->position() == 3u) { ++res; }
            b->flip();
            if (b->position() == 0u) { res += 2; }
            if (b->limit() == 3u)    { res += 4; }
            if (b->get() == (byte)10) { res += 8; }
            if (b->get() == (byte)20) { res += 16; }
            if (b->get() == (byte)30) { res += 32; }
            if (b->hasRemaining() == false) { res += 64; }
            delete b;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 127);
}

TEST_CASE("ByteBuffer: clear and rewind reset the cursor", "[libk][io][bytebuffer]") {
    auto jit = jit_k(R"SRC(
        module __bb_clear__;
        test() : int {
            b : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(4u);
            b->put((byte)1);
            b->put((byte)2);
            b->flip();
            b->get();
            res : int = 0;
            if (b->position() == 1u) { ++res; }
            b->rewind();
            if (b->position() == 0u && b->limit() == 2u) { res += 2; }
            b->clear();
            if (b->position() == 0u && b->limit() == 4u) { res += 4; }
            delete b;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 7);
}

TEST_CASE("ByteBuffer: compact preserves the unread bytes", "[libk][io][bytebuffer]") {
    auto jit = jit_k(R"SRC(
        module __bb_compact__;
        test() : int {
            b : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(8u);
            b->put((byte)1);
            b->put((byte)2);
            b->put((byte)3);
            b->put((byte)4);
            b->flip();
            b->get();
            b->get();
            b->compact();
            res : int = 0;
            if (b->position() == 2u) { ++res; }
            if (b->limit() == 8u)    { res += 2; }
            b->flip();
            if (b->get() == (byte)3) { res += 4; }
            if (b->get() == (byte)4) { res += 8; }
            delete b;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 15);
}

TEST_CASE("ByteBuffer: absolute get and put leave the cursor untouched", "[libk][io][bytebuffer]") {
    auto jit = jit_k(R"SRC(
        module __bb_absolute__;
        test() : int {
            b : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(4u);
            b->put(0u, (byte)7);
            b->put(3u, (byte)9);
            res : int = 0;
            if (b->position() == 0u)   { ++res; }
            if (b->get(0u) == (byte)7) { res += 2; }
            if (b->get(3u) == (byte)9) { res += 4; }
            delete b;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 7);
}

TEST_CASE("ByteBuffer: wrap builds a drainable buffer", "[libk][io][bytebuffer]") {
    auto jit = jit_k(R"SRC(
        module __bb_wrap__;
        test() : int {
            src : byte[]! = new byte[3];
            src[0] = (byte)5;
            src[1] = (byte)6;
            src[2] = (byte)7;
            b : k::io::ByteBuffer! = k::io::ByteBuffer::wrap(src);
            res : int = 0;
            if (b->position() == 0u) { ++res; }
            if (b->limit() == 3u)    { res += 2; }
            if (b->get() == (byte)5) { res += 4; }
            if (b->get() == (byte)6) { res += 8; }
            if (b->get() == (byte)7) { res += 16; }
            delete b;
            delete src;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 31);
}

TEST_CASE("ByteBuffer: bulk transfers and toArray", "[libk][io][bytebuffer]") {
    auto jit = jit_k(R"SRC(
        module __bb_bulk__;
        test() : int {
            src : byte[]! = new byte[4];
            src[0] = (byte)1;
            src[1] = (byte)2;
            src[2] = (byte)3;
            src[3] = (byte)4;

            b : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(4u);
            written : unsigned int = b->put(src, 0u, 4u);
            res : int = 0;
            if (written == 4u) { ++res; }
            b->flip();

            dst : byte[]! = new byte[4];
            read : unsigned int = b->get(dst, 0u, 4u);
            if (read == 4u) { res += 2; }
            if (dst[0] == (byte)1 && dst[3] == (byte)4) { res += 4; }

            b->rewind();
            copy : byte[]! = b->toArray();
            if (copy.size == 4) { res += 8; }
            if (copy[2] == (byte)3) { res += 16; }
            // toArray() must not move the cursor.
            if (b->position() == 0u) { res += 32; }

            delete copy;
            delete dst;
            delete b;
            delete src;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 63);
}

TEST_CASE("ByteBuffer: overflow and underflow are reported", "[libk][io][bytebuffer]") {
    auto jit = jit_k(R"SRC(
        module __bb_bounds__;
        test() : int {
            res : int = 0;

            b : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(1u);
            b->put((byte)1);
            try {
                b->put((byte)2);
            } catch (e: IndexOutOfBoundsError&) {
                ++res;
            }

            b->flip();
            b->get();
            try {
                b->get();
            } catch (e: IndexOutOfBoundsError&) {
                res += 2;
            }

            try {
                b->get(9u);
            } catch (e: IndexOutOfBoundsError&) {
                res += 4;
            }

            try {
                b->limit(9u);
            } catch (e: IndexOutOfBoundsError&) {
                res += 8;
            }

            delete b;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 15);
}
