/*
 * K Language standard library — Thread stress tests (Phase 7)
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

TEST_CASE("Thread stress: repeated lifecycle is stable", "[libk][thread][stress]") {
    auto jit = jit_k(R"SRC(
        module __thread_stress__;

        class Worker : public Runnable {
            _loops : int;
            _done  : int;
        public:
            Worker(loops: int) : _loops(loops), _done(0) {}
            override run() : void {
                i : int = 0;
                while (i < _loops) {
                    Thread::yield();
                    ++i;
                }
                _done = 1;
            }
            const done() : int { return _done; }
        }

        test() : int {
            rounds : int = 0;
            completed : int = 0;
            while (rounds < 16) {
                a : Worker! = new Worker(32);
                b : Worker! = new Worker(32);
                c : Worker! = new Worker(32);
                d : Worker! = new Worker(32);
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
                completed += a->done() + b->done() + c->done() + d->done();
                delete ta; delete tb; delete tc; delete td;
                delete a; delete b; delete c; delete d;
                ++rounds;
            }
            return completed;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 64);
}

