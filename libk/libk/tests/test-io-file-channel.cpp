/*
 * K Language standard library — FileChannel tests (Phase 4)
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
 * Tests for k::io::FileChannel — the asynchronous file channel built on the
 * Phase 4 substrate (io_uring when available, synchronous POSIX otherwise).
 *
 * Coverage:
 *  - opening a missing file raises FileNotFoundException
 *  - create / write / read round-trip through ByteBuffer
 *  - positional reads and writes leave the channel position untouched
 *  - sequential reads and writes advance the channel position
 *  - readFully raises EndOfStreamException past the end of the file
 *  - writeFully transfers every remaining byte
 *  - size / truncate / force
 *  - operating on a closed channel raises ClosedChannelException
 *  - concurrent positional reads from several threads
 */

#include <catch2/catch_all.hpp>

#include "../../klang/tests/helpers.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>

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

void write_file(const std::string& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)),
                       std::istreambuf_iterator<char>());
}

} // anonymous namespace

TEST_CASE("FileChannel: opening a missing file raises FileNotFoundException",
          "[libk][io][file-channel]") {
    std::filesystem::remove("/tmp/klang_fc_absent.bin");
    auto jit = jit_k(R"SRC(
        module __fc_missing__;
        test() : int {
            p : k::io::Path("/tmp/klang_fc_absent.bin");
            try {
                c : k::io::FileChannel! = k::io::FileChannel::open(p);
                delete c;
                return 0;
            } catch (e: k::io::FileNotFoundException&) {
                return 1;
            } catch (e2: k::io::IOException&) {
                return 2;
            }
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}

TEST_CASE("FileChannel: create, write and read back", "[libk][io][file-channel]") {
    const std::string path = "/tmp/klang_fc_roundtrip.bin";
    std::filesystem::remove(path);
    auto jit = jit_k(R"SRC(
        module __fc_roundtrip__;
        test() : int {
            p : k::io::Path("/tmp/klang_fc_roundtrip.bin");
            res : int = 0;

            opts : int = k::io::OPEN_WRITE | k::io::OPEN_CREATE | k::io::OPEN_TRUNCATE;
            out : k::io::FileChannel! = k::io::FileChannel::open(p, opts);
            src : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(5u);
            src->put((byte) 75);
            src->put((byte) 76);
            src->put((byte) 65);
            src->put((byte) 78);
            src->put((byte) 71);
            src->flip();
            if (out->write(src) == 5) { ++res; }
            if (out->position() == 5L) { res += 2; }
            out->force();
            if (out->size() == 5L) { res += 4; }
            out->close();
            if (out->isOpen() == false) { res += 8; }
            delete out;
            delete src;

            in : k::io::FileChannel! = k::io::FileChannel::open(p);
            dst : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(16u);
            n : int = in->read(dst);
            if (n == 5) { res += 16; }
            dst->flip();
            if (dst->get() == (byte) 75) { res += 32; }
            if (dst->get(4u) == (byte) 71) { res += 64; }
            if (in->read(dst, 5L) == 0) { res += 128; }
            in->close();
            delete in;
            delete dst;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 255);
    REQUIRE(read_file(path) == "KLANG");
    std::filesystem::remove(path);
}

TEST_CASE("FileChannel: positional access leaves the position untouched",
          "[libk][io][file-channel]") {
    const std::string path = "/tmp/klang_fc_positional.bin";
    write_file(path, "0123456789");
    auto jit = jit_k(R"SRC(
        module __fc_positional__;
        test() : int {
            p : k::io::Path("/tmp/klang_fc_positional.bin");
            res : int = 0;
            c : k::io::FileChannel! = k::io::FileChannel::open(p, k::io::OPEN_READ);

            b : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(3u);
            if (c->read(b, 4L) == 3) { ++res; }
            if (c->position() == 0L) { res += 2; }
            b->flip();
            if (b->get() == (byte) 52) { res += 4; }
            if (b->get() == (byte) 53) { res += 8; }

            b->clear();
            if (c->read(b, 9L) == 1) { res += 16; }

            b->clear();
            if (c->read(b, 10L) == 0) { res += 32; }

            c->close();
            delete c;
            delete b;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 63);
    REQUIRE(read_file(path) == "0123456789");
    std::filesystem::remove(path);
}

TEST_CASE("FileChannel: readFully raises EndOfStreamException past the end",
          "[libk][io][file-channel]") {
    const std::string path = "/tmp/klang_fc_readfully.bin";
    write_file(path, "abcd");
    auto jit = jit_k(R"SRC(
        module __fc_readfully__;
        test() : int {
            p : k::io::Path("/tmp/klang_fc_readfully.bin");
            res : int = 0;
            c : k::io::FileChannel! = k::io::FileChannel::open(p, k::io::OPEN_READ);

            small : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(4u);
            c->readFully(small, 0L);
            if (small->hasRemaining() == false) { ++res; }
            small->flip();
            if (small->get(3u) == (byte) 100) { res += 2; }

            big : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(8u);
            try {
                c->readFully(big, 0L);
            } catch (e: k::io::EndOfStreamException&) {
                res += 4;
                if (e.getBytesTransferred() == 4L) { res += 8; }
            }

            c->close();
            delete c;
            delete small;
            delete big;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 15);
    std::filesystem::remove(path);
}

TEST_CASE("FileChannel: writeFully transfers every remaining byte",
          "[libk][io][file-channel]") {
    const std::string path = "/tmp/klang_fc_writefully.bin";
    std::filesystem::remove(path);
    auto jit = jit_k(R"SRC(
        module __fc_writefully__;
        test() : int {
            p : k::io::Path("/tmp/klang_fc_writefully.bin");
            res : int = 0;
            opts : int = k::io::OPEN_WRITE | k::io::OPEN_CREATE | k::io::OPEN_TRUNCATE;
            c : k::io::FileChannel! = k::io::FileChannel::open(p, opts);

            b : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(64u);
            i : unsigned int = 0u;
            while (i < 64u) {
                b->put((byte) 65);
                ++i;
            }
            b->flip();
            c->writeFully(b, 0L);
            if (b->hasRemaining() == false) { ++res; }
            c->force();
            if (c->size() == 64L) { res += 2; }

            c->truncate(10L);
            if (c->size() == 10L) { res += 4; }

            c->close();
            delete c;
            delete b;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 7);
    REQUIRE(read_file(path) == "AAAAAAAAAA");
    std::filesystem::remove(path);
}

TEST_CASE("FileChannel: a closed channel raises ClosedChannelException",
          "[libk][io][file-channel]") {
    const std::string path = "/tmp/klang_fc_closed.bin";
    write_file(path, "xyz");
    auto jit = jit_k(R"SRC(
        module __fc_closed__;
        test() : int {
            p : k::io::Path("/tmp/klang_fc_closed.bin");
            res : int = 0;
            c : k::io::FileChannel! = k::io::FileChannel::open(p, k::io::OPEN_READ);
            if (c->isOpen()) { ++res; }
            if (c->nativeDescriptor() >= 0) { res += 2; }
            c->close();
            if (c->nativeDescriptor() == -1) { res += 4; }

            b : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(4u);
            try {
                c->read(b, 0L);
            } catch (e: k::io::ClosedChannelException&) {
                res += 8;
            }
            try {
                c->size();
            } catch (e2: k::io::ClosedChannelException&) {
                res += 16;
            }
            // Closing twice must stay harmless.
            c->close();
            res += 32;

            delete c;
            delete b;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 63);
    std::filesystem::remove(path);
}

TEST_CASE("FileChannel: append mode writes at the end", "[libk][io][file-channel]") {
    const std::string path = "/tmp/klang_fc_append.bin";
    write_file(path, "head-");
    auto jit = jit_k(R"SRC(
        module __fc_append__;
        test() : int {
            p : k::io::Path("/tmp/klang_fc_append.bin");
            opts : int = k::io::OPEN_WRITE | k::io::OPEN_APPEND;
            c : k::io::FileChannel! = k::io::FileChannel::open(p, opts);
            b : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(4u);
            b->put((byte) 116);
            b->put((byte) 97);
            b->put((byte) 105);
            b->put((byte) 108);
            b->flip();
            c->writeFully(b, c->size());
            c->force();
            c->close();
            delete c;
            delete b;
            return 1;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
    REQUIRE(read_file(path) == "head-tail");
    std::filesystem::remove(path);
}

TEST_CASE("FileChannel: exclusive create fails on an existing file",
          "[libk][io][file-channel]") {
    const std::string path = "/tmp/klang_fc_excl.bin";
    write_file(path, "here");
    auto jit = jit_k(R"SRC(
        module __fc_excl__;
        test() : int {
            p : k::io::Path("/tmp/klang_fc_excl.bin");
            opts : int = k::io::OPEN_WRITE | k::io::OPEN_CREATE | k::io::OPEN_EXCLUSIVE;
            try {
                c : k::io::FileChannel! = k::io::FileChannel::open(p, opts);
                delete c;
                return 0;
            } catch (e: k::io::IOException&) {
                return 1;
            }
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
    REQUIRE(read_file(path) == "here");
    std::filesystem::remove(path);
}

TEST_CASE("FileChannel: concurrent positional reads from several threads",
          "[libk][io][file-channel]") {
    const std::string path = "/tmp/klang_fc_concurrent.bin";
    write_file(path, std::string(4096, 'Z'));
    auto jit = jit_k(R"SRC(
        module __fc_concurrent__;

        class Reader : public k::Runnable {
            _chan : k::io::FileChannel*;
            _from : long;
            _ok   : int;
        public:
            Reader(chan: k::io::FileChannel*, from: long)
                : _chan(chan), _from(from), _ok(0) {}
            override run() : void {
                b : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(512u);
                _chan->readFully(b, _from);
                b->flip();
                good : int = 1;
                i : unsigned int = 0u;
                while (i < 512u) {
                    if (b->get(i) != (byte) 90) { good = 0; }
                    ++i;
                }
                _ok = good;
                delete b;
            }
            const ok() : int { return _ok; }
        }

        test() : int {
            p : k::io::Path("/tmp/klang_fc_concurrent.bin");
            c : k::io::FileChannel! = k::io::FileChannel::open(p, k::io::OPEN_READ);

            r0 : Reader! = new Reader(c, 0L);
            r1 : Reader! = new Reader(c, 1024L);
            r2 : Reader! = new Reader(c, 2048L);
            r3 : Reader! = new Reader(c, 3072L);

            t0 : k::Thread! = new k::Thread(r0);
            t1 : k::Thread! = new k::Thread(r1);
            t2 : k::Thread! = new k::Thread(r2);
            t3 : k::Thread! = new k::Thread(r3);
            t0->start();
            t1->start();
            t2->start();
            t3->start();
            t0->join();
            t1->join();
            t2->join();
            t3->join();

            res : int = r0->ok() + r1->ok() + r2->ok() + r3->ok();
            // The channel position must be untouched by positional reads.
            if (c->position() == 0L) { res += 8; }

            delete t0; delete t1; delete t2; delete t3;
            delete r0; delete r1; delete r2; delete r3;
            c->close();
            delete c;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 12);
    std::filesystem::remove(path);
}
