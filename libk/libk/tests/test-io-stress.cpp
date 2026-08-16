/*
 * K Language standard library — I/O stress tests (Phase 7)
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

#include <cstdio>
#include <fstream>
#include <string>

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

void write_pattern_file(const std::string& path, std::size_t size) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    for (std::size_t i = 0; i < size; ++i) {
        unsigned char c = static_cast<unsigned char>(i & 0xFFu);
        out.put(static_cast<char>(c));
    }
}

} // anonymous namespace

TEST_CASE("FileChannel stress: concurrent positional reads are stable", "[libk][io][stress]") {
    const std::string path = "/tmp/klang_io_stress.bin";
    write_pattern_file(path, 4096);

    std::string src = R"SRC(
        module __io_stress__;

        class Reader : public Runnable {
            _path  : k::io::Path*;
            _base  : long;
            _done  : int;
        public:
            Reader(path: k::io::Path*, base: long) : _path(path), _base(base), _done(0) {}
            override run() : void {
                file : k::io::FileChannel! = k::io::FileChannel::open(_path);
                buf : k::io::ByteBuffer! = k::io::ByteBuffer::allocate(128u);
                ok : int = 0;
                i : int = 0;
                while (i < 8) {
                    buf->clear();
                    n : int = file->read(buf, _base + (long) (i * 128), 0L);
                    if (n == 128) {
                        ++ok;
                    }
                    ++i;
                }
                _done = ok;
                delete buf;
                delete file;
            }
            const done() : int { return _done; }
        }

        test() : int {
            path : k::io::Path! = new k::io::Path(
        )SRC";
    src += "\"" + path + "\"";
    src += R"SRC(
            );

            a : Reader! = new Reader(path, 0L);
            b : Reader! = new Reader(path, 512L);
            c : Reader! = new Reader(path, 1024L);
            d : Reader! = new Reader(path, 1536L);
            ta : Thread! = new Thread(a);
            tb : Thread! = new Thread(b);
            tc : Thread! = new Thread(c);
            td : Thread! = new Thread(d);
            ta->start();
            tb->start();
            tc->start();
            td->start();
            ta->join();
            tb->join();
            tc->join();
            td->join();
            res : int = a->done() + b->done() + c->done() + d->done();
            delete ta; delete tb; delete tc; delete td;
            delete a; delete b; delete c; delete d;
            delete path;
            return res;
        }
    )SRC";

    auto jit = jit_k(src);
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 32);

    std::remove(path.c_str());
}

