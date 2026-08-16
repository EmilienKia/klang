/*
 * K Language standard library — EventLoop tests (Phase 6)
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

#include <catch2/catch_all.hpp>

#include "../../klang/tests/helpers.hpp"

#include <chrono>

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

TEST_CASE("EventLoop: submit executes task and stop exits loop", "[libk][io][event-loop]") {
    auto jit = jit_k(R"SRC(
        module __eventloop_submit__;

        hits : int = 0;

        class LoopRunner : public Runnable {
            _loop : k::io::EventLoop*;
        public:
            LoopRunner(loop: k::io::EventLoop*) : _loop(loop) {}
            override run() : void { _loop->run(); }
        }

        class HitTask : public Runnable {
            _loop : k::io::EventLoop*;
        public:
            HitTask(loop: k::io::EventLoop*) : _loop(loop) {}
            override run() : void {
                ++hits;
                _loop->stop();
            }
        }

        test() : int {
            loop : k::io::EventLoop! = new k::io::EventLoop();
            runner : LoopRunner! = new LoopRunner(loop);
            tr : Thread! = new Thread(runner);
            tr->start();

            task : HitTask! = new HitTask(loop);
            loop->submit(task);
            tr->join();

            res : int = hits;
            delete task;
            delete tr;
            delete runner;
            delete loop;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}

TEST_CASE("EventLoop: scheduleAfter executes delayed task", "[libk][io][event-loop]") {
    auto jit = jit_k(R"SRC(
        module __eventloop_schedule__;

        done : int = 0;

        class LoopRunner : public Runnable {
            _loop : k::io::EventLoop*;
        public:
            LoopRunner(loop: k::io::EventLoop*) : _loop(loop) {}
            override run() : void { _loop->run(); }
        }

        class DelayedTask : public Runnable {
            _loop : k::io::EventLoop*;
        public:
            DelayedTask(loop: k::io::EventLoop*) : _loop(loop) {}
            override run() : void {
                done = 1;
                _loop->stop();
            }
        }

        test() : int {
            loop : k::io::EventLoop! = new k::io::EventLoop();
            runner : LoopRunner! = new LoopRunner(loop);
            tr : Thread! = new Thread(runner);
            tr->start();

            task : DelayedTask! = new DelayedTask(loop);
            loop->scheduleAfter(Duration::ofMillis(60L), task);
            tr->join();

            res : int = done;
            delete task;
            delete tr;
            delete runner;
            delete loop;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    const auto start = std::chrono::steady_clock::now();
    REQUIRE(fn() == 1);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    REQUIRE(elapsed >= std::chrono::milliseconds(40));
    REQUIRE(elapsed < std::chrono::seconds(5));
}

TEST_CASE("EventLoop: stop wakes idle loop", "[libk][io][event-loop]") {
    auto jit = jit_k(R"SRC(
        module __eventloop_stop_idle__;

        class LoopRunner : public Runnable {
            _loop : k::io::EventLoop*;
        public:
            LoopRunner(loop: k::io::EventLoop*) : _loop(loop) {}
            override run() : void { _loop->run(); }
        }

        test() : int {
            loop : k::io::EventLoop! = new k::io::EventLoop();
            runner : LoopRunner! = new LoopRunner(loop);
            tr : Thread! = new Thread(runner);
            tr->start();
            Thread::sleep(Duration::ofMillis(40L));
            loop->stop();
            tr->join();

            res : int = loop->isRunning() ? 0 : 1;
            delete tr;
            delete runner;
            delete loop;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}

