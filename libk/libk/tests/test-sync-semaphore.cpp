/*
 * K Language standard library — Semaphore tests (Phase 3)
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
 * Tests for k::Semaphore.
 *
 * Phase 3 coverage:
 *  - permit accounting: acquire/release/availablePermits
 *  - tryAcquire() fails when no permit is left, multi-permit acquisition
 *  - drainPermits() takes everything atomically
 *  - tryAcquire(Duration) times out when no permit arrives
 *  - acquire() blocks until another thread releases a permit
 *  - a semaphore bounds the number of threads inside a section
 *  - a blocking acquire() is interruptible and consumes no permit
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
// Permit accounting
// =============================================================================

TEST_CASE("Semaphore: acquire and release track available permits", "[libk][sync][semaphore]") {
    auto jit = jit_k(R"SRC(
        module __sync_sem_basic__;
        test() : int {
            s : Semaphore(2);
            res : int = 0;
            if (s.availablePermits() == 2) { res = res + 1; }
            try {
                s.acquire();
                if (s.availablePermits() == 1) { res = res + 2; }
                s.acquire();
                if (s.availablePermits() == 0) { res = res + 4; }
                s.release();
                s.release();
                if (s.availablePermits() == 2) { res = res + 8; }
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

TEST_CASE("Semaphore: tryAcquire fails when exhausted", "[libk][sync][semaphore]") {
    auto jit = jit_k(R"SRC(
        module __sync_sem_try__;
        test() : int {
            s : Semaphore(1);
            res : int = 0;
            if (s.tryAcquire()) { res = res + 1; }
            if (!s.tryAcquire()) { res = res + 2; }
            s.release();
            // Multi-permit acquisition is all-or-nothing.
            if (!s.tryAcquire(2)) { res = res + 4; }
            if (s.availablePermits() == 1) { res = res + 8; }
            s.release(3);
            if (s.tryAcquire(4)) { res = res + 16; }
            if (s.availablePermits() == 0) { res = res + 32; }
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 63);
}

TEST_CASE("Semaphore: drainPermits empties the semaphore", "[libk][sync][semaphore]") {
    auto jit = jit_k(R"SRC(
        module __sync_sem_drain__;
        test() : int {
            s : Semaphore(5);
            res : int = 0;
            if (s.drainPermits() == 5) { res = res + 1; }
            if (s.availablePermits() == 0) { res = res + 2; }
            if (s.drainPermits() == 0) { res = res + 4; }
            if (!s.tryAcquire()) { res = res + 8; }
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 15);
}

TEST_CASE("Semaphore: a zero-permit semaphore blocks a timed acquisition", "[libk][sync][semaphore]") {
    auto jit = jit_k(R"SRC(
        module __sync_sem_timeout__;
        test() : int {
            s : Semaphore;
            res : int = 0;
            try {
                if (!s.tryAcquire(Duration::ofMillis(60L))) { res = res + 1; }
                if (s.availablePermits() == 0) { res = res + 2; }
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
// Cross-thread hand-off
// =============================================================================

TEST_CASE("Semaphore: acquire blocks until another thread releases", "[libk][sync][semaphore][thread]") {
    auto jit = jit_k(R"SRC(
        module __sync_sem_handoff__;

        class Releaser : public Runnable {
            private:
            _s : Semaphore*;
            public:
            Releaser(s: Semaphore*) { _s = s; }
            override run() : void throws Throwable {
                Thread::sleep(Duration::ofMillis(50L));
                _s->release();
            }
        }

        test() : int {
            s : Semaphore;
            r : Releaser! = new Releaser(&s);
            run : Runnable* = r;
            t : Thread! = new Thread(run);
            t->start();

            res : int = 0;
            try {
                s.acquire();
                res = 1;
                if (s.availablePermits() == 0) { res = res + 2; }
                t->join();
            } catch (e: Throwable&) {
                res = -1;
            }
            delete t;
            delete r;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    auto start = std::chrono::steady_clock::now();
    REQUIRE(fn() == 3);
    REQUIRE(std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(35));
}

TEST_CASE("Semaphore: bounds the number of threads in a section", "[libk][sync][semaphore][thread]") {
    auto jit = jit_k(R"SRC(
        module __sync_sem_bound__;

        public struct Shared {
            sem : Semaphore*;
            lock : Mutex;
            inside : int;
            maxInside : int;
            total : int;
        }

        class Worker : public Runnable {
            private:
            _s : Shared*;
            public:
            Worker(s: Shared*) { _s = s; }
            override run() : void throws Throwable {
                i : int = 0;
                while (i < 40) {
                    _s->sem->acquire();
                    _s->lock.lock();
                    _s->inside = _s->inside + 1;
                    if (_s->inside > _s->maxInside) { _s->maxInside = _s->inside; }
                    _s->total = _s->total + 1;
                    _s->lock.unlock();

                    Thread::sleep(Duration::ofMillis(1L));

                    _s->lock.lock();
                    _s->inside = _s->inside - 1;
                    _s->lock.unlock();
                    _s->sem->release();
                    i = i + 1;
                }
            }
        }

        test() : int {
            s : Shared;
            sem : Semaphore! = new Semaphore(2);
            s.sem = sem;
            s.inside = 0;
            s.maxInside = 0;
            s.total = 0;

            w1 : Worker! = new Worker(&s);
            w2 : Worker! = new Worker(&s);
            w3 : Worker! = new Worker(&s);
            w4 : Worker! = new Worker(&s);
            r1 : Runnable* = w1;
            r2 : Runnable* = w2;
            r3 : Runnable* = w3;
            r4 : Runnable* = w4;
            t1 : Thread! = new Thread(r1);
            t2 : Thread! = new Thread(r2);
            t3 : Thread! = new Thread(r3);
            t4 : Thread! = new Thread(r4);
            t1->start(); t2->start(); t3->start(); t4->start();
            try { t1->join(); t2->join(); t3->join(); t4->join(); } catch (e: Throwable&) { }

            res : int = 0;
            if (s.maxInside <= 2) { res = res + 1; }
            if (s.maxInside >= 1) { res = res + 2; }
            if (s.total == 160) { res = res + 4; }
            if (sem->availablePermits() == 2) { res = res + 8; }
            delete t1; delete t2; delete t3; delete t4;
            delete w1; delete w2; delete w3; delete w4;
            delete sem;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 15);
}

// =============================================================================
// Interruption
// =============================================================================

TEST_CASE("Semaphore: a blocked acquire is interruptible", "[libk][sync][semaphore][thread]") {
    auto jit = jit_k(R"SRC(
        module __sync_sem_interrupt__;

        public struct Shared {
            sem : Semaphore*;
            ready : CountDownLatch*;
        }

        class Blocker : public Runnable {
            private:
            _s : Shared*;
            public:
            _outcome : int = 0;
            Blocker(s: Shared*) { _s = s; }
            override run() : void throws Throwable {
                try {
                    _s->ready->countDown();
                    _s->sem->acquire();
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
            sem : Semaphore! = new Semaphore(0);
            ready : CountDownLatch! = new CountDownLatch(1L);
            s.sem = sem;
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
            // No permit may have been consumed by the interrupted acquisition.
            if (sem->availablePermits() == 0) { res = res + 10; }
            delete t;
            delete b;
            delete sem;
            delete ready;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 12);
}
