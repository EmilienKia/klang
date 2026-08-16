/*
 * K Language standard library — Mutex / ReentrantLock tests (Phase 3)
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
 * Tests for k::Mutex and k::ReentrantLock.
 *
 * Phase 3 coverage:
 *  - uncontended lock/unlock and ownership queries
 *  - tryLock() success on a free lock, failure on a lock held elsewhere
 *  - unlock() without ownership raises IllegalMonitorStateException
 *  - ReentrantLock allows the owner to re-acquire, hold count tracking
 *  - a Mutex serialises increments performed by several threads
 *  - tryLock(Duration) times out while another thread holds the lock
 *  - lockInterruptibly() honours a pending interruption
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
// Basic ownership
// =============================================================================

TEST_CASE("Mutex: lock and unlock on a single thread", "[libk][sync][mutex]") {
    auto jit = jit_k(R"SRC(
        module __sync_mutex_basic__;
        test() : int {
            m : Mutex;
            r : int = 0;
            if (!m.isHeldByCurrentThread()) { ++r; }
            m.lock();
            if (m.isHeldByCurrentThread()) { r += 2; }
            if (m.holdCount() == 1) { r += 4; }
            m.unlock();
            if (!m.isHeldByCurrentThread()) { r += 8; }
            if (m.holdCount() == 0) { r += 16; }
            return r;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 31);
}

TEST_CASE("Mutex: tryLock succeeds on a free lock", "[libk][sync][mutex]") {
    auto jit = jit_k(R"SRC(
        module __sync_mutex_trylock__;
        test() : int {
            m : Mutex;
            r : int = 0;
            if (m.tryLock()) { ++r; }
            if (m.isHeldByCurrentThread()) { r += 2; }
            m.unlock();
            if (m.tryLock()) { r += 4; }
            m.unlock();
            return r;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 7);
}

TEST_CASE("Mutex: unlock without ownership throws", "[libk][sync][mutex]") {
    auto jit = jit_k(R"SRC(
        module __sync_mutex_illegal__;
        test() : int {
            m : Mutex;
            try {
                m.unlock();
                return 1;
            } catch (e: IllegalMonitorStateException&) {
                return 2;
            } catch (o: Throwable&) {
                return 3;
            }
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 2);
}

// =============================================================================
// Reentrancy
// =============================================================================

TEST_CASE("ReentrantLock: the owner may re-acquire the lock", "[libk][sync][mutex]") {
    auto jit = jit_k(R"SRC(
        module __sync_reentrant__;
        test() : int {
            m : ReentrantLock;
            r : int = 0;
            m.lock();
            m.lock();
            m.lock();
            if (m.holdCount() == 3) { ++r; }
            m.unlock();
            if (m.holdCount() == 2) { r += 2; }
            m.unlock();
            m.unlock();
            if (m.holdCount() == 0) { r += 4; }
            if (!m.isHeldByCurrentThread()) { r += 8; }
            // The lock is free again, so it can be taken from scratch.
            if (m.tryLock()) { r += 16; }
            m.unlock();
            return r;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 31);
}

TEST_CASE("ReentrantLock: tryLock also nests", "[libk][sync][mutex]") {
    auto jit = jit_k(R"SRC(
        module __sync_reentrant_try__;
        test() : int {
            m : ReentrantLock;
            r : int = 0;
            if (m.tryLock()) { ++r; }
            if (m.tryLock()) { r += 2; }
            if (m.holdCount() == 2) { r += 4; }
            m.unlock();
            m.unlock();
            if (m.holdCount() == 0) { r += 8; }
            return r;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 15);
}

// =============================================================================
// Contention between threads
// =============================================================================

TEST_CASE("Mutex: concurrent increments are serialised", "[libk][sync][mutex][thread]") {
    auto jit = jit_k(R"SRC(
        module __sync_mutex_contention__;

        public struct Shared {
            lock : Mutex;
            counter : long;
        }

        class Incrementer : public Runnable {
            private:
            _shared : Shared*;
            _rounds : int;
            public:
            Incrementer(s: Shared*, rounds: int) {
                _shared = s;
                _rounds = rounds;
            }
            override run() : void throws Throwable {
                i : int = 0;
                while (i < _rounds) {
                    _shared->lock.lock();
                    _shared->counter = _shared->counter + 1L;
                    _shared->lock.unlock();
                    ++i;
                }
            }
        }

        test() : long {
            s : Shared;
            s.counter = 0L;

            a : Incrementer! = new Incrementer(&s, 2000);
            b : Incrementer! = new Incrementer(&s, 2000);
            c : Incrementer! = new Incrementer(&s, 2000);
            ra : Runnable* = a;
            rb : Runnable* = b;
            rc : Runnable* = c;
            ta : Thread! = new Thread(ra);
            tb : Thread! = new Thread(rb);
            tc : Thread! = new Thread(rc);
            ta->start();
            tb->start();
            tc->start();
            try { ta->join(); tb->join(); tc->join(); } catch (e: Throwable&) { }
            delete ta; delete tb; delete tc;
            delete a; delete b; delete c;
            return s.counter;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<long long(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 6000);
}

TEST_CASE("Mutex: tryLock fails while another thread holds the lock", "[libk][sync][mutex][thread]") {
    auto jit = jit_k(R"SRC(
        module __sync_mutex_held_elsewhere__;

        public struct Shared {
            lock : Mutex;
            ready : CountDownLatch*;
            done : CountDownLatch*;
        }

        class Holder : public Runnable {
            private:
            _s : Shared*;
            public:
            Holder(s: Shared*) { _s = s; }
            override run() : void throws Throwable {
                _s->lock.lock();
                _s->ready->countDown();
                _s->done->await();
                _s->lock.unlock();
            }
        }

        test() : int {
            s : Shared;
            ready : CountDownLatch! = new CountDownLatch(1L);
            done : CountDownLatch! = new CountDownLatch(1L);
            s.ready = ready;
            s.done = done;
            h : Holder! = new Holder(&s);
            r : Runnable* = h;
            t : Thread! = new Thread(r);
            t->start();

            res : int = 0;
            try {
                s.ready->await();
                // The other thread owns the lock: acquisition must fail.
                if (!s.lock.tryLock()) { ++res; }
                if (!s.lock.isHeldByCurrentThread()) { res += 2; }
                if (!s.lock.tryLock(Duration::ofMillis(40L))) { res += 4; }
                s.done->countDown();
                t->join();
                // The lock is free again once the holder returned.
                if (s.lock.tryLock()) { res += 8; }
                s.lock.unlock();
            } catch (e: Throwable&) {
                res = -1;
            }
            delete t;
            delete h;
            delete ready;
            delete done;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    auto start = std::chrono::steady_clock::now();
    REQUIRE(fn() == 15);
    // The timed tryLock must really have waited for its deadline.
    REQUIRE(std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(30));
}

// =============================================================================
// Interruption
// =============================================================================

TEST_CASE("Mutex: lockInterruptibly honours a pending interruption", "[libk][sync][mutex][thread]") {
    auto jit = jit_k(R"SRC(
        module __sync_mutex_interrupt__;

        public struct Shared {
            lock : Mutex;
            ready : CountDownLatch*;
            done : CountDownLatch*;
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
                    _s->lock.lockInterruptibly();
                    _outcome = 1;
                    _s->lock.unlock();
                } catch (e: ThreadInterruptionException&) {
                    _outcome = 2;
                } catch (o: Throwable&) {
                    _outcome = 3;
                }
            }
        }

        test() : int {
            s : Shared;
            ready : CountDownLatch! = new CountDownLatch(1L);
            done : CountDownLatch! = new CountDownLatch(1L);
            s.ready = ready;
            s.done = done;
            s.lock.lock();

            b : Blocker! = new Blocker(&s);
            r : Runnable* = b;
            t : Thread! = new Thread(r);
            t->start();
            try {
                s.ready->await();
                Thread::sleep(Duration::ofMillis(50L));
                t->interrupt();
                t->join();
            } catch (e: Throwable&) { }
            s.lock.unlock();
            outcome : int = b->_outcome;
            delete t;
            delete b;
            delete ready;
            delete done;
            return outcome;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 2);
}
