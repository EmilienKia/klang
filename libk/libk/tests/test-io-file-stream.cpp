/*
 * K Language standard library - retained FileChannel lifecycle regressions
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
 * The obsolete async stream adapter cases were removed. Preserve the channel
 * lifecycle coverage independently of synchronous FileInput/OutputStream tests:
 *  - a channel operation started on an already-interrupted thread completes
 *    or reports interruption with the actual drained transfer count
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

} // anonymous namespace

TEST_CASE("FileChannel: an already-interrupted thread reports the interruption",
          "[libk][io][file-channel][interruption]") {
    const std::string path = "/tmp/klang_afs_interrupt.bin";
    write_file(path, std::string(65536, 'Q'));
    auto jit = jit_k(R"SRC(
        module __afs_interrupt__;

        class Worker : public k::Runnable {
            _res : int;
        public:
            Worker() : _res(0) {}
            override run() : void {
                p : k::io::file::Path("/tmp/klang_afs_interrupt.bin");
                c : k::io::file::FileChannel! = k::io::file::FileChannel::open(p, k::io::file::OPEN_READ);
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
                    _res = got == 65536 && b->position() == 65536u ? 1 : -1;
                } catch (e: k::ThreadInterruptionException&) {
                    _res = b->position() == 0u ? 2 : -2;
                } catch (e: k::io::InterruptedIOException&) {
                    transferred : long = e.getBytesTransferred();
                    _res = transferred > 0L && transferred <= 65536L &&
                        (long) b->position() == transferred &&
                        b->get(0u) == (byte) 81 &&
                        b->get((unsigned int) transferred - 1u) == (byte) 81 ? 4 : -4;
                } catch (e2: k::io::IOException&) {
                    _res = 3;
                }
                if (b->hasLease()) _res = -5;
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
    int r = 0;
    REQUIRE_NOTHROW(r = fn());
    INFO("completion/interruption result=" << r);
    REQUIRE((r == 1 || r == 2 || r == 4));
    std::filesystem::remove(path);
}

TEST_CASE("FileChannel: closing from another thread is observed by users",
          "[libk][io][file-channel][interruption]") {
    const std::string path = "/tmp/klang_afs_closer.bin";
    write_file(path, std::string(4096, 'W'));
    auto jit = jit_k(R"SRC(
        module __afs_closer__;

        class Closer : public k::Runnable {
            _chan : k::io::file::FileChannel*;
        public:
            Closer(chan: k::io::file::FileChannel*) : _chan(chan) {}
            override run() : void {
                k::Thread::sleep(k::Duration::ofMillis(30L));
                _chan->close();
            }
        }

        test() : int {
            p : k::io::file::Path("/tmp/klang_afs_closer.bin");
            c : k::io::file::FileChannel! = k::io::file::FileChannel::open(p, k::io::file::OPEN_READ);
            res : int = 0;

            cl : Closer! = new Closer(c);
            t : k::Thread! = new k::Thread(cl);
            t->start();
            t->join();

            if (c->isOpen() == false) { ++res; }
            b : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(64u);
            try {
                c->read(b, 0L);
            } catch (e: k::io::ClosedChannelException&) {
                res += 2;
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
