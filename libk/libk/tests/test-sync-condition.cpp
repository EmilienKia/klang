/*
 * K Language standard library — Condition variable tests (Phase 3)
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
 * Tests for k::Condition, obtained through k::Mutex.newCondition().
 *
 * Phase 3 coverage:
 *  - await() returns once another thread signals, with the lock re-acquired
 *  - signalAll() releases every waiter
 *  - await(Duration) reports a timeout and re-acquires the lock
 *  - await() outside the lock raises IllegalMonitorStateException
 *  - signal()/signalAll() outside the lock raise IllegalMonitorStateException
 *  - a blocking await() is interruptible and still re-acquires the lock
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
// Monitor state checks
// =============================================================================

TEST_CASE("Condition: await without the lock throws", "[libk][sync][condition]") {
    auto jit = jit_k(R"SRC(
        module __sync_cond_illegal_await__;
        test() : int {
            m : Mutex;
            c : Condition! = m.newCondition();
            res : int = 0;
            try {
                c->await();
                res = 1;
            } catch (e: IllegalMonitorStateException&) {
                res = 2;
            } catch (o: Throwable&) {
                res = 3;
            }
            delete c;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 2);
}

TEST_CASE("Condition: signal without the lock throws", "[libk][sync][condition]") {
    auto jit = jit_k(R"SRC(
        module __sync_cond_illegal_signal__;
        test() : int {
            m : Mutex;
            c : Condition! = m.newCondition();
            res : int = 0;
            try { c->signal(); } catch (e: IllegalMonitorStateException&) { ++res; }
            try { c->signalAll(); } catch (e2: IllegalMonitorStateException&) { res += 2; }
            // Holding the lock makes both legal, even with no waiter.
            m.lock();
            try { c->signal(); c->signalAll(); res += 4; } catch (o: Throwable&) { }
            m.unlock();
            delete c;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 7);
}

// =============================================================================
// Timed wait
// =============================================================================

TEST_CASE("Condition: await(Duration) times out and keeps the lock", "[libk][sync][condition]") {
    auto jit = jit_k(R"SRC(
        module __sync_cond_timeout__;
        test() : int {
            m : Mutex;
            c : Condition! = m.newCondition();
            res : int = 0;
            m.lock();
            try {
                if (!c->await(Duration::ofMillis(60L))) { ++res; }
                if (m.isHeldByCurrentThread()) { res += 2; }
            } catch (o: Throwable&) {
                res = -1;
            }
            m.unlock();
            delete c;
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
// Cross-thread signalling
// =============================================================================

TEST_CASE("Condition: await returns once another thread signals", "[libk][sync][condition][thread]") {
    auto jit = jit_k(R"SRC(
        module __sync_cond_signal__;

        public struct Shared {
            lock : Mutex;
            cond : Condition*;
            value : int;
        }

        class Producer : public Runnable {
            private:
            _s : Shared*;
            public:
            Producer(s: Shared*) { _s = s; }
            override run() : void throws(Throwable) {
                Thread::sleep(Duration::ofMillis(50L));
                _s->lock.lock();
                _s->value = 42;
                _s->cond->signal();
                _s->lock.unlock();
            }
        }

        test() : int {
            s : Shared;
            s.value = 0;
            c : Condition! = s.lock.newCondition();
            s.cond = c;

            p : Producer! = new Producer(&s);
            r : Runnable* = p;
            t : Thread! = new Thread(r);
            t->start();

            res : int = 0;
            s.lock.lock();
            try {
                while (s.value == 0) {
                    c->await();
                }
                res = s.value;
                if (!s.lock.isHeldByCurrentThread()) { res = -2; }
            } catch (o: Throwable&) {
                res = -1;
            }
            s.lock.unlock();
            try { t->join(); } catch (e: Throwable&) { }
            delete t;
            delete p;
            delete c;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    auto start = std::chrono::steady_clock::now();
    REQUIRE(fn() == 42);
    REQUIRE(std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(35));
}

TEST_CASE("Condition: signalAll releases every waiter", "[libk][sync][condition][thread]") {
    auto jit = jit_k(R"SRC(
        module __sync_cond_signal_all__;

        public struct Shared {
            lock : Mutex;
            cond : Condition*;
            open : bool;
            woken : int;
        }

        class Waiter : public Runnable {
            private:
            _s : Shared*;
            public:
            Waiter(s: Shared*) { _s = s; }
            override run() : void throws(Throwable) {
                _s->lock.lock();
                while (!_s->open) {
                    _s->cond->await();
                }
                _s->woken = _s->woken + 1;
                _s->lock.unlock();
            }
        }

        test() : int {
            s : Shared;
            s.open = false;
            s.woken = 0;
            c : Condition! = s.lock.newCondition();
            s.cond = c;

            w1 : Waiter! = new Waiter(&s);
            w2 : Waiter! = new Waiter(&s);
            w3 : Waiter! = new Waiter(&s);
            r1 : Runnable* = w1;
            r2 : Runnable* = w2;
            r3 : Runnable* = w3;
            t1 : Thread! = new Thread(r1);
            t2 : Thread! = new Thread(r2);
            t3 : Thread! = new Thread(r3);
            t1->start(); t2->start(); t3->start();

            try { Thread::sleep(Duration::ofMillis(60L)); } catch (e: Throwable&) { }
            s.lock.lock();
            s.open = true;
            try { c->signalAll(); } catch (o: Throwable&) { }
            s.lock.unlock();

            try { t1->join(); t2->join(); t3->join(); } catch (e2: Throwable&) { }
            res : int = s.woken;
            delete t1; delete t2; delete t3;
            delete w1; delete w2; delete w3;
            delete c;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 3);
}

// =============================================================================
// Interruption
// =============================================================================

TEST_CASE("Condition: a blocked await is interruptible", "[libk][sync][condition][thread]") {
    auto jit = jit_k(R"SRC(
        module __sync_cond_interrupt__;

        public struct Shared {
            lock : Mutex;
            cond : Condition*;
            ready : CountDownLatch*;
        }

        class Waiter : public Runnable {
            private:
            _s : Shared*;
            public:
            _outcome : int = 0;
            _heldOnExit : bool = false;
            Waiter(s: Shared*) { _s = s; }
            override run() : void throws(Throwable) {
                _s->lock.lock();
                try {
                    _s->ready->countDown();
                    _s->cond->await();
                    _outcome = 1;
                } catch (e: ThreadInterruptionException&) {
                    _outcome = 2;
                    // The lock must have been re-acquired before unwinding.
                    _heldOnExit = _s->lock.isHeldByCurrentThread();
                } catch (o: Throwable&) {
                    _outcome = 3;
                }
                _s->lock.unlock();
            }
        }

        test() : int {
            s : Shared;
            c : Condition! = s.lock.newCondition();
            ready : CountDownLatch! = new CountDownLatch(1L);
            s.cond = c;
            s.ready = ready;

            w : Waiter! = new Waiter(&s);
            r : Runnable* = w;
            t : Thread! = new Thread(r);
            t->start();
            try {
                ready->await();
                Thread::sleep(Duration::ofMillis(50L));
                t->interrupt();
                t->join();
            } catch (e: Throwable&) { }

            res : int = w->_outcome;
            if (w->_heldOnExit) { res += 10; }
            delete t;
            delete w;
            delete ready;
            delete c;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 12);
}
