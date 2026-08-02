/*
 * K Language standard library — Cancellation storm tests (Phase 7)
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

TEST_CASE("Thread cancellation storm remains stable", "[libk][thread][stress][interrupt]") {
    auto jit = jit_k(R"SRC(
        module __cancel_storm__;

        class Sleeper : public Runnable {
            _caught : int;
        public:
            Sleeper() : _caught(0) {}
            override run() : void {
                try {
                    Thread::sleep(Duration::ofSeconds(30L));
                } catch (e: ThreadInterruptionException&) {
                    _caught = 1;
                }
            }
            const caught() : int { return _caught; }
        }

        test() : int {
            round : int = 0;
            ok : int = 0;
            while (round < 16) {
                worker : Sleeper! = new Sleeper();
                thr : Thread! = new Thread(worker);
                thr->start();
                Thread::sleep(Duration::ofMillis(5L));
                thr->interrupt();
                thr->join();
                ok = ok + worker->caught();
                delete thr;
                delete worker;
                round = round + 1;
            }
            return ok;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 16);
}

