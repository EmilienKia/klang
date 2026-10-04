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

TEST_CASE("EventLoop: FIFO execution order", "[libk][io][event-loop]") {
    auto jit = jit_k(R"SRC(
        module __eventloop_fifo__;

        result : int = 0;

        class LoopRunner : public Runnable {
            _loop : k::io::EventLoop*;
        public:
            LoopRunner(loop: k::io::EventLoop*) : _loop(loop) {}
            override run() : void { _loop->run(); }
        }

        class OrderTask : public Runnable {
            _val : int;
            _loop : k::io::EventLoop*;
        public:
            OrderTask(val: int, loop: k::io::EventLoop*) : _val(val), _loop(loop) {}
            override run() : void {
                result = result * 10 + _val;
                if (_val == 3) {
                    _loop->stop();
                }
            }
        }

        test() : int {
            loop : k::io::EventLoop! = new k::io::EventLoop();
            task1 : OrderTask! = new OrderTask(1, loop);
            task2 : OrderTask! = new OrderTask(2, loop);
            task3 : OrderTask! = new OrderTask(3, loop);

            loop->execute(task1);
            loop->execute(task2);
            loop->execute(task3);

            runner : LoopRunner! = new LoopRunner(loop);
            tr : Thread! = new Thread(runner);
            tr->start();
            tr->join();

            res : int = result;
            delete task1;
            delete task2;
            delete task3;
            delete tr;
            delete runner;
            delete loop;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 123);
}

TEST_CASE("EventLoop: runOnce processes task", "[libk][io][event-loop]") {
    auto jit = jit_k(R"SRC(
        module __eventloop_run_once__;

        step : int = 0;

        class StepTask : public Runnable {
            _val : int;
        public:
            StepTask(val: int) : _val(val) {}
            override run() : void {
                step = _val;
            }
        }

        test() : int {
            loop : k::io::EventLoop! = new k::io::EventLoop();
            task : StepTask! = new StepTask(42);
            loop->execute(task);

            ran : bool = loop->runOnce(Duration::ofMillis(50L));
            res : int = 0;
            if (ran && step == 42) {
                res = 1;
            }

            delete task;
            delete loop;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}

TEST_CASE("EventLoop: timers ordered in min-heap", "[libk][io][event-loop]") {
    auto jit = jit_k(R"SRC(
        module __eventloop_timer_min_heap__;

        order : int = 0;

        class LoopRunner : public Runnable {
            _loop : k::io::EventLoop*;
        public:
            LoopRunner(loop: k::io::EventLoop*) : _loop(loop) {}
            override run() : void { _loop->run(); }
        }

        class TimerOrderTask : public Runnable {
            _id : int;
            _loop : k::io::EventLoop*;
        public:
            TimerOrderTask(id: int, loop: k::io::EventLoop*) : _id(id), _loop(loop) {}
            override run() : void {
                order = order * 10 + _id;
                if (_id == 3) {
                    _loop->stop();
                }
            }
        }

        test() : int {
            loop : k::io::EventLoop! = new k::io::EventLoop();

            runner : LoopRunner! = new LoopRunner(loop);
            tr : Thread! = new Thread(runner);
            tr->start();

            t3 : TimerOrderTask! = new TimerOrderTask(3, loop);
            t1 : TimerOrderTask! = new TimerOrderTask(1, loop);
            t2 : TimerOrderTask! = new TimerOrderTask(2, loop);

            // Schedule t3 with largest delay, t1 with smallest delay, t2 in middle
            loop->scheduleAfter(Duration::ofMillis(120L), t3);
            loop->scheduleAfter(Duration::ofMillis(30L), t1);
            loop->scheduleAfter(Duration::ofMillis(70L), t2);

            tr->join();

            res : int = order;
            delete t3;
            delete t2;
            delete t1;
            delete tr;
            delete runner;
            delete loop;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 123);
}

TEST_CASE("EventLoop: acquireLease executes tasks through lease", "[libk][io][event-loop]") {
    auto jit = jit_k(R"SRC(
        module __eventloop_lease__;

        count : int = 0;

        class LoopRunner : public Runnable {
            _loop : k::io::EventLoop*;
        public:
            LoopRunner(loop: k::io::EventLoop*) : _loop(loop) {}
            override run() : void { _loop->run(); }
        }

        class LeaseTask : public Runnable {
            _loop : k::io::EventLoop*;
        public:
            LeaseTask(loop: k::io::EventLoop*) : _loop(loop) {}
            override run() : void {
                count = count + 1;
                _loop->stop();
            }
        }

        test() : int {
            loop : k::io::EventLoop! = new k::io::EventLoop();
            lease : ExecutorLease! = loop->acquireLease();

            runner : LoopRunner! = new LoopRunner(loop);
            tr : Thread! = new Thread(runner);
            tr->start();

            task : LeaseTask! = new LeaseTask(loop);
            lease->execute(task);

            tr->join();
            res : int = count;

            delete task;
            delete lease;
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

TEST_CASE("EventLoop: rejection and task cleanup after stop", "[libk][io][event-loop]") {
    auto jit = jit_k(R"SRC(
        module __eventloop_rejection__;

        cleanedUp : int = 0;

        cleanupHandler() : void {
            cleanedUp = cleanedUp + 1;
        }

        class DummyTask : public Runnable {
        public:
            DummyTask() {}
            override run() : void {}
        }

        test() : int {
            loop : k::io::EventLoop! = new k::io::EventLoop();
            loop->stop();

            task : DummyTask! = new DummyTask();
            rejected : int = 0;
            try {
                loop->execute(task, cleanupHandler);
            } catch (e: RejectedExecutionException&) {
                rejected = 1;
            }

            res : int = 0;
            if (rejected == 1 && cleanedUp == 1 && loop->isShutdown()) {
                res = 1;
            }

            delete task;
            delete loop;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}
