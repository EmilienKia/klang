/*
 * K Language standard library — CountDownLatch / CyclicBarrier tests (Phase 3)
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
 * Tests for k::CountDownLatch and k::CyclicBarrier.
 *
 * Phase 3 coverage:
 *  - a zero-count latch is already open; countDown() never goes negative
 *  - await(Duration) times out on a latch that stays closed
 *  - await() returns once N threads have counted down
 *  - a blocked latch await() is interruptible
 *  - a barrier releases every party together and hands out arrival indices
 *  - a barrier is reusable across generations
 *  - reset() breaks the current generation (BrokenBarrierException)
 *  - a timed barrier await() reports the timeout and breaks the barrier
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

// =============================================================================
// CountDownLatch — single thread
// =============================================================================

TEST_CASE("CountDownLatch: a zero-count latch is already open", "[libk][sync][latch]") {
    auto jit = jit_k(R"SRC(
        module __sync_latch_open__;
        test() : int {
            l : CountDownLatch(0L);
            res : int = 0;
            if (l.count() == 0L) { ++res; }
            try {
                l.await();
                res += 2;
                if (l.await(Duration::ofMillis(1L))) { res += 4; }
            } catch (e: Throwable&) {
                res = -1;
            }
            // Counting down an open latch is a no-op, never negative.
            l.countDown();
            if (l.count() == 0L) { res += 8; }
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 15);
}

TEST_CASE("CountDownLatch: countDown opens the latch", "[libk][sync][latch]") {
    auto jit = jit_k(R"SRC(
        module __sync_latch_countdown__;
        test() : int {
            l : CountDownLatch(3L);
            res : int = 0;
            if (l.count() == 3L) { ++res; }
            l.countDown();
            if (l.count() == 2L) { res += 2; }
            l.countDown();
            l.countDown();
            if (l.count() == 0L) { res += 4; }
            try {
                l.await();
                res += 8;
            } catch (e: Throwable&) {
                res = -1;
            }
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 15);
}

TEST_CASE("CountDownLatch: await(Duration) times out on a closed latch", "[libk][sync][latch]") {
    auto jit = jit_k(R"SRC(
        module __sync_latch_timeout__;
        test() : int {
            l : CountDownLatch(1L);
            res : int = 0;
            try {
                if (!l.await(Duration::ofMillis(60L))) { ++res; }
                if (l.count() == 1L) { res += 2; }
            } catch (e: Throwable&) {
                res = -1;
            }
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    auto start = std::chrono::steady_clock::now();
    REQUIRE(fn() == 3);
    REQUIRE(std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(45));
}

// =============================================================================
// CountDownLatch — cross-thread
// =============================================================================

TEST_CASE("CountDownLatch: await returns once every worker counted down", "[libk][sync][latch][thread]") {
    auto jit = jit_k(R"SRC(
        module __sync_latch_workers__;

        class Worker : public Runnable {
            private:
            _latch : CountDownLatch*;
            _delayMillis : long;
            public:
            Worker(l: CountDownLatch*, delayMillis: long) {
                _latch = l;
                _delayMillis = delayMillis;
            }
            override run() : void throws(Throwable) {
                Thread::sleep(Duration::ofMillis(_delayMillis));
                _latch->countDown();
            }
        }

        test() : int {
            l : CountDownLatch(3L);
            w1 : Worker! = new Worker(&l, 20L);
            w2 : Worker! = new Worker(&l, 40L);
            w3 : Worker! = new Worker(&l, 60L);
            r1 : Runnable* = w1;
            r2 : Runnable* = w2;
            r3 : Runnable* = w3;
            t1 : Thread! = new Thread(r1);
            t2 : Thread! = new Thread(r2);
            t3 : Thread! = new Thread(r3);
            t1->start(); t2->start(); t3->start();

            res : int = 0;
            try {
                l.await();
                if (l.count() == 0L) { res = 1; }
                t1->join(); t2->join(); t3->join();
            } catch (e: Throwable&) {
                res = -1;
            }
            delete t1; delete t2; delete t3;
            delete w1; delete w2; delete w3;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    auto start = std::chrono::steady_clock::now();
    REQUIRE(fn() == 1);
    REQUIRE(std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(45));
}

TEST_CASE("CountDownLatch: a blocked await is interruptible", "[libk][sync][latch][thread]") {
    auto jit = jit_k(R"SRC(
        module __sync_latch_interrupt__;

        public struct Shared {
            gate : CountDownLatch*;
            ready : CountDownLatch*;
        }

        class Blocker : public Runnable {
            private:
            _s : Shared*;
            public:
            _outcome : int = 0;
            Blocker(s: Shared*) { _s = s; }
            override run() : void throws(Throwable) {
                try {
                    _s->ready->countDown();
                    _s->gate->await();
                    _outcome = 1;
                } catch (e: ThreadInterruptionException&) {
                    _outcome = 2;
                } catch (o: Throwable&) {
                    _outcome = 3;
                }
            }
        }

        test() : int {
            s : Shared;
            gate : CountDownLatch! = new CountDownLatch(1L);
            ready : CountDownLatch! = new CountDownLatch(1L);
            s.gate = gate;
            s.ready = ready;

            b : Blocker! = new Blocker(&s);
            r : Runnable* = b;
            t : Thread! = new Thread(r);
            t->start();
            try {
                ready->await();
                Thread::sleep(Duration::ofMillis(50L));
                t->interrupt();
                t->join();
            } catch (e: Throwable&) { }

            res : int = b->_outcome;
            if (gate->count() == 1L) { res += 10; }
            delete t;
            delete b;
            delete gate;
            delete ready;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 12);
}

// =============================================================================
// CyclicBarrier
// =============================================================================

TEST_CASE("CyclicBarrier: a single-party barrier trips immediately", "[libk][sync][barrier]") {
    auto jit = jit_k(R"SRC(
        module __sync_barrier_single__;
        test() : int {
            b : CyclicBarrier(1);
            res : int = 0;
            if (b.parties() == 1) { ++res; }
            if (b.numberWaiting() == 0) { res += 2; }
            try {
                if (b.await() == 0) { res += 4; }
                // The barrier is reusable.
                if (b.await() == 0) { res += 8; }
            } catch (e: Throwable&) {
                res = -1;
            }
            if (!b.isBroken()) { res += 16; }
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 31);
}

TEST_CASE("CyclicBarrier: a timed await times out and breaks the barrier", "[libk][sync][barrier]") {
    auto jit = jit_k(R"SRC(
        module __sync_barrier_timeout__;
        test() : int {
            b : CyclicBarrier(2);
            res : int = 0;
            try {
                if (b.await(Duration::ofMillis(60L)) == -1) { ++res; }
            } catch (e: Throwable&) {
                res = -10;
            }
            if (b.isBroken()) { res += 2; }
            // Every subsequent arrival fails until the barrier is reset.
            try {
                b.await(Duration::ofMillis(20L));
                res += 100;
            } catch (e2: BrokenBarrierException&) {
                res += 4;
            } catch (o: Throwable&) {
                res += 200;
            }
            b.reset();
            if (!b.isBroken()) { res += 8; }
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    auto start = std::chrono::steady_clock::now();
    REQUIRE(fn() == 15);
    REQUIRE(std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(45));
}

TEST_CASE("CyclicBarrier: every party crosses together", "[libk][sync][barrier][thread]") {
    auto jit = jit_k(R"SRC(
        module __sync_barrier_cross__;

        public struct Shared {
            barrier : CyclicBarrier*;
            lock : Mutex;
            beforeCount : int;
            crossedWithAllArrived : int;
        }

        class Party : public Runnable {
            private:
            _s : Shared*;
            _delayMillis : long;
            public:
            _index : int = -99;
            Party(s: Shared*, delayMillis: long) {
                _s = s;
                _delayMillis = delayMillis;
            }
            override run() : void throws(Throwable) {
                Thread::sleep(Duration::ofMillis(_delayMillis));
                _s->lock.lock();
                _s->beforeCount = _s->beforeCount + 1;
                _s->lock.unlock();
                _index = _s->barrier->await();
                // Every party must observe all arrivals once released.
                _s->lock.lock();
                if (_s->beforeCount == 3) {
                    _s->crossedWithAllArrived = _s->crossedWithAllArrived + 1;
                }
                _s->lock.unlock();
            }
        }

        test() : int {
            s : Shared;
            barrier : CyclicBarrier! = new CyclicBarrier(3);
            s.barrier = barrier;
            s.beforeCount = 0;
            s.crossedWithAllArrived = 0;

            p1 : Party! = new Party(&s, 10L);
            p2 : Party! = new Party(&s, 40L);
            p3 : Party! = new Party(&s, 70L);
            r1 : Runnable* = p1;
            r2 : Runnable* = p2;
            r3 : Runnable* = p3;
            t1 : Thread! = new Thread(r1);
            t2 : Thread! = new Thread(r2);
            t3 : Thread! = new Thread(r3);
            t1->start(); t2->start(); t3->start();
            try { t1->join(); t2->join(); t3->join(); } catch (e: Throwable&) { }

            res : int = 0;
            if (s.crossedWithAllArrived == 3) { ++res; }
            if (!barrier->isBroken()) { res += 2; }
            // Arrival indices are a permutation of {0, 1, 2}.
            sum : int = p1->_index + p2->_index + p3->_index;
            if (sum == 3) { res += 4; }
            if (p3->_index == 0) { res += 8; }
            delete t1; delete t2; delete t3;
            delete p1; delete p2; delete p3;
            delete barrier;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 15);
}

TEST_CASE("CyclicBarrier: an interrupted party breaks the barrier", "[libk][sync][barrier][thread]") {
    auto jit = jit_k(R"SRC(
        module __sync_barrier_broken__;

        public struct Shared {
            barrier : CyclicBarrier*;
            ready : CountDownLatch*;
        }

        class Party : public Runnable {
            private:
            _s : Shared*;
            public:
            _outcome : int = 0;
            Party(s: Shared*) { _s = s; }
            override run() : void throws(Throwable) {
                try {
                    _s->ready->countDown();
                    _s->barrier->await();
                    _outcome = 1;
                } catch (e: ThreadInterruptionException&) {
                    _outcome = 2;
                } catch (b: BrokenBarrierException&) {
                    _outcome = 3;
                } catch (o: Throwable&) {
                    _outcome = 4;
                }
            }
        }

        test() : int {
            s : Shared;
            barrier : CyclicBarrier! = new CyclicBarrier(3);
            ready : CountDownLatch! = new CountDownLatch(2L);
            s.barrier = barrier;
            s.ready = ready;

            p1 : Party! = new Party(&s);
            p2 : Party! = new Party(&s);
            r1 : Runnable* = p1;
            r2 : Runnable* = p2;
            t1 : Thread! = new Thread(r1);
            t2 : Thread! = new Thread(r2);
            t1->start(); t2->start();
            try {
                ready->await();
                Thread::sleep(Duration::ofMillis(60L));
                // Only two of the three parties arrived: interrupting one must
                // break the generation for the other one too.
                t1->interrupt();
                t1->join();
                t2->join();
            } catch (e: Throwable&) { }

            res : int = 0;
            if (p1->_outcome == 2) { ++res; }
            if (p2->_outcome == 3) { res += 2; }
            if (barrier->isBroken()) { res += 4; }
            delete t1; delete t2;
            delete p1; delete p2;
            delete barrier;
            delete ready;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 7);
}
