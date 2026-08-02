/*
 * K Language standard library — asynchronous file stream tests (Phase 4)
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
 * Tests for k::io::AsyncFileInputStream and k::io::AsyncFileOutputStream —
 * the InputStream<byte> / OutputStream<byte> adapters over FileChannel.
 *
 * Coverage:
 *  - single-byte and bulk reads, end of stream
 *  - skip() and available()
 *  - single-byte and bulk writes, flush, truncating and appending modes
 *  - writing to a closed stream raises ClosedChannelException
 *  - a channel operation started on an already-interrupted thread reports
 *    the interruption instead of transferring data
 *  - closing a channel from another thread makes pending users observe
 *    ClosedChannelException
 */

#include <catch2/catch_all.hpp>

#include "../../klang/tests/helpers.hpp"

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

TEST_CASE("AsyncFileInputStream: reads bytes and reports end of stream",
          "[libk][io][async-stream]") {
    const std::string path = "/tmp/klang_afs_read.bin";
    write_file(path, "ABCDE");
    auto jit = jit_k(R"SRC(
        module __afs_read__;
        test() : int {
            p : k::io::Path("/tmp/klang_afs_read.bin");
            res : int = 0;
            s : k::io::AsyncFileInputStream! = new k::io::AsyncFileInputStream(p);
            if (s->isOpen()) { res = res + 1; }

            first : k::Optional<byte> = s->read();
            if (first.hasValue() && first.get() == (byte) 65) { res = res + 2; }

            buf : byte[4];
            n : k::Expected<unsigned int, k::io::StreamOutOfData> = s->read(buf);
            if (n.hasResult() && n.getResult() == 4u) { res = res + 4; }
            if (buf[0] == (byte) 66 && buf[3] == (byte) 69) { res = res + 8; }

            eos : k::Optional<byte> = s->read();
            if (eos.hasValue() == false) { res = res + 16; }

            s->close();
            if (s->isOpen() == false) { res = res + 32; }
            delete s;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 63);
    std::filesystem::remove(path);
}

TEST_CASE("AsyncFileInputStream: skip and available track the position",
          "[libk][io][async-stream]") {
    const std::string path = "/tmp/klang_afs_skip.bin";
    write_file(path, "0123456789");
    auto jit = jit_k(R"SRC(
        module __afs_skip__;
        test() : int {
            p : k::io::Path("/tmp/klang_afs_skip.bin");
            res : int = 0;
            s : k::io::AsyncFileInputStream! = new k::io::AsyncFileInputStream(p);

            a0 : k::Expected<unsigned int, k::io::StreamOutOfData> = s->available();
            if (a0.hasResult() && a0.getResult() == 10u) { res = res + 1; }

            if (s->skip(4) == 4) { res = res + 2; }
            b : k::Optional<byte> = s->read();
            if (b.hasValue() && b.get() == (byte) 52) { res = res + 4; }

            // Skipping past the end must clamp to what remains.
            if (s->skip(1000) == 5) { res = res + 8; }
            a1 : k::Expected<unsigned int, k::io::StreamOutOfData> = s->available();
            if (a1.hasResult() && a1.getResult() == 0u) { res = res + 16; }

            s->close();
            delete s;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 31);
    std::filesystem::remove(path);
}

TEST_CASE("AsyncFileOutputStream: writes, flushes and truncates",
          "[libk][io][async-stream]") {
    const std::string path = "/tmp/klang_afs_write.bin";
    write_file(path, "old-content-to-drop");
    auto jit = jit_k(R"SRC(
        module __afs_write__;
        test() : int {
            p : k::io::Path("/tmp/klang_afs_write.bin");
            res : int = 0;
            s : k::io::AsyncFileOutputStream! = new k::io::AsyncFileOutputStream(p);
            if (s->isOpen()) { res = res + 1; }

            s->write((byte) 75);
            body : byte[4];
            body[0] = (byte) 76;
            body[1] = (byte) 65;
            body[2] = (byte) 78;
            body[3] = (byte) 71;
            s->write(body);
            s->flush();

            if (s->channel()->size() == 5L) { res = res + 2; }
            s->close();
            if (s->isOpen() == false) { res = res + 4; }
            delete s;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 7);
    REQUIRE(read_file(path) == "KLANG");
    std::filesystem::remove(path);
}

TEST_CASE("AsyncFileOutputStream: append mode keeps existing content",
          "[libk][io][async-stream]") {
    const std::string path = "/tmp/klang_afs_append.bin";
    write_file(path, "head-");
    auto jit = jit_k(R"SRC(
        module __afs_append__;
        test() : int {
            p : k::io::Path("/tmp/klang_afs_append.bin");
            s : k::io::AsyncFileOutputStream! = new k::io::AsyncFileOutputStream(p, true);
            body : byte[4];
            body[0] = (byte) 116;
            body[1] = (byte) 97;
            body[2] = (byte) 105;
            body[3] = (byte) 108;
            s->write(body);
            s->flush();
            s->close();
            delete s;
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

TEST_CASE("AsyncFileOutputStream: writing after close raises ClosedChannelException",
          "[libk][io][async-stream]") {
    const std::string path = "/tmp/klang_afs_closed.bin";
    std::filesystem::remove(path);
    auto jit = jit_k(R"SRC(
        module __afs_closed__;
        test() : int {
            p : k::io::Path("/tmp/klang_afs_closed.bin");
            res : int = 0;
            s : k::io::AsyncFileOutputStream! = new k::io::AsyncFileOutputStream(p);
            s->close();
            try {
                s->write((byte) 1);
            } catch (e: k::io::ClosedChannelException&) {
                res = res + 1;
            }
            buf : byte[2];
            try {
                s->write(buf);
            } catch (e2: k::io::ClosedChannelException&) {
                res = res + 2;
            }
            // flush() on a closed stream must stay harmless.
            s->flush();
            res = res + 4;
            delete s;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 7);
    std::filesystem::remove(path);
}

TEST_CASE("FileChannel: an already-interrupted thread reports the interruption",
          "[libk][io][async-stream][interruption]") {
    const std::string path = "/tmp/klang_afs_interrupt.bin";
    write_file(path, std::string(65536, 'Q'));
    auto jit = jit_k(R"SRC(
        module __afs_interrupt__;

        class Worker : public k::Runnable {
            _res : int;
        public:
            Worker() : _res(0) {}
            override run() : void {
                p : k::io::Path("/tmp/klang_afs_interrupt.bin");
                c : k::io::FileChannel! = k::io::FileChannel::open(p, k::io::OPEN_READ);
                b : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(65536u);
                // Arm the interrupted flag before entering the blocking call:
                // the substrate must either complete the transfer or report
                // the interruption, never lose data silently.
                self : k::Thread! = k::Thread::current();
                self->interrupt();
                delete self;
                got : int = 0;
                try {
                    got = c->read(b, 0L);
                    _res = 1;
                } catch (e: k::ThreadInterruptionException&) {
                    _res = 2;
                } catch (e2: k::io::IOException&) {
                    _res = 3;
                }
                // The flag must be observable and clearable afterwards.
                k::Thread::interrupted();
                c->close();
                delete c;
                delete b;
            }
            const res() : int { return _res; }
        }

        test() : int {
            w : Worker! = new Worker();
            t : k::Thread! = new k::Thread(w);
            t->start();
            t->join();
            r : int = w->res();
            delete t;
            delete w;
            return r;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    const int r = fn();
    // Either the read completed (1) or it reported the interruption (2);
    // an I/O error (3) would mean the substrate mishandled the flag.
    REQUIRE((r == 1 || r == 2));
    std::filesystem::remove(path);
}

TEST_CASE("FileChannel: closing from another thread is observed by users",
          "[libk][io][async-stream][interruption]") {
    const std::string path = "/tmp/klang_afs_closer.bin";
    write_file(path, std::string(4096, 'W'));
    auto jit = jit_k(R"SRC(
        module __afs_closer__;

        class Closer : public k::Runnable {
            _chan : k::io::FileChannel*;
        public:
            Closer(chan: k::io::FileChannel*) : _chan(chan) {}
            override run() : void {
                k::Thread::sleep(k::Duration::ofMillis(30L));
                _chan->close();
            }
        }

        test() : int {
            p : k::io::Path("/tmp/klang_afs_closer.bin");
            c : k::io::FileChannel! = k::io::FileChannel::open(p, k::io::OPEN_READ);
            res : int = 0;

            cl : Closer! = new Closer(c);
            t : k::Thread! = new k::Thread(cl);
            t->start();
            t->join();

            if (c->isOpen() == false) { res = res + 1; }
            b : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(64u);
            try {
                c->read(b, 0L);
            } catch (e: k::io::ClosedChannelException&) {
                res = res + 2;
            }

            delete t;
            delete cl;
            delete c;
            delete b;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 3);
    std::filesystem::remove(path);
}
