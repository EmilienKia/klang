/*
 * K Language standard library — runtime shutdown tests (Phase 7)
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

TEST_CASE("Runtime shutdown survives repeated JIT teardown", "[libk][runtime][shutdown][stress]") {
    for (int i = 0; i < 8; ++i) {
        auto jit = jit_k(R"SRC(
            module __runtime_shutdown__;

            class Worker : public Runnable {
                _done : int;
            public:
                Worker() : _done(0) {}
                override run() : void {
                    Thread::yield();
                    _done = 1;
                }
                const done() : int { return _done; }
            }

            test() : int {
                worker : Worker! = new Worker();
                thr : Thread! = new Thread(worker);
                thr->start();
                thr->join();
                res : int = worker->done();
                delete thr;
                delete worker;
                return res;
            }
        )SRC");
        REQUIRE(jit);
        auto fn = jit->lookup_symbol<int(*)()>("test");
        REQUIRE(fn);
        REQUIRE(fn() == 1);
    }
}

