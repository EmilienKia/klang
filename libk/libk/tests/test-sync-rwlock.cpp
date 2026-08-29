/*
 * K Language standard library — ReadWriteLock tests (Phase 3)
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
 * Tests for k::ReadWriteLock.
 *
 * Phase 3 coverage:
 *  - several read holds coexist; readCount() tracks them
 *  - the write lock excludes readers and other writers
 *  - unlocking a free lock raises IllegalMonitorStateException
 *  - a reader blocks while a writer holds the lock, and vice versa
 *  - a timed read acquisition times out under a live writer
 *  - concurrent readers and writers never observe a torn state
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
// Single-thread semantics
// =============================================================================

TEST_CASE("ReadWriteLock: read holds are shared and counted", "[libk][sync][rwlock]") {
    auto jit = jit_k(R"SRC(
        module __sync_rw_shared__;
        test() : int {
            l : ReadWriteLock;
            res : int = 0;
            if (l.readCount() == 0) { ++res; }
            l.readLock();
            if (l.readCount() == 1) { res += 2; }
            // A second shared hold is granted without blocking.
            if (l.tryReadLock()) { res += 4; }
            if (l.readCount() == 2) { res += 8; }
            l.readUnlock();
            l.readUnlock();
            if (l.readCount() == 0) { res += 16; }
            if (!l.isWriteLocked()) { res += 32; }
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 63);
}

TEST_CASE("ReadWriteLock: the write lock is exclusive", "[libk][sync][rwlock]") {
    auto jit = jit_k(R"SRC(
        module __sync_rw_exclusive__;
        test() : int {
            l : ReadWriteLock;
            res : int = 0;
            if (l.tryWriteLock()) { ++res; }
            if (l.isWriteLocked()) { res += 2; }
            // Neither readers nor other writers may enter.
            if (!l.tryReadLock()) { res += 4; }
            if (!l.tryWriteLock()) { res += 8; }
            l.writeUnlock();
            if (!l.isWriteLocked()) { res += 16; }
            // A live reader excludes writers.
            l.readLock();
            if (!l.tryWriteLock()) { res += 32; }
            l.readUnlock();
            if (l.tryWriteLock()) { res += 64; }
            l.writeUnlock();
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 127);
}

TEST_CASE("ReadWriteLock: unlocking a free lock throws", "[libk][sync][rwlock]") {
    auto jit = jit_k(R"SRC(
        module __sync_rw_illegal__;
        test() : int {
            l : ReadWriteLock;
            res : int = 0;
            try { l.readUnlock(); } catch (e: IllegalMonitorStateException&) { ++res; }
            try { l.writeUnlock(); } catch (e2: IllegalMonitorStateException&) { res += 2; }
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 3);
}

// =============================================================================
// Blocking behaviour across threads
// =============================================================================

TEST_CASE("ReadWriteLock: a reader waits for the writer to finish", "[libk][sync][rwlock][thread]") {
    auto jit = jit_k(R"SRC(
        module __sync_rw_reader_waits__;

        public struct Shared {
            lock : ReadWriteLock*;
            ready : CountDownLatch*;
            value : int;
        }

        class Writer : public Runnable {
            private:
            _s : Shared*;
            public:
            Writer(s: Shared*) { _s = s; }
            override run() : void throws(Throwable) {
                _s->lock->writeLock();
                _s->ready->countDown();
                Thread::sleep(Duration::ofMillis(80L));
                _s->value = 7;
                _s->lock->writeUnlock();
            }
        }

        test() : int {
            s : Shared;
            lock : ReadWriteLock! = new ReadWriteLock();
            ready : CountDownLatch! = new CountDownLatch(1L);
            s.lock = lock;
            s.ready = ready;
            s.value = 0;

            w : Writer! = new Writer(&s);
            r : Runnable* = w;
            t : Thread! = new Thread(r);
            t->start();

            res : int = 0;
            try {
                ready->await();
                // The writer holds the lock: a timed read must give up.
                if (!lock->tryReadLock(Duration::ofMillis(20L))) { ++res; }
                if (lock->isWriteLocked()) { res += 2; }
                // Blocking until the writer is done yields the published value.
                lock->readLock();
                if (s.value == 7) { res += 4; }
                lock->readUnlock();
                t->join();
                if (lock->readCount() == 0) { res += 8; }
            } catch (e: Throwable&) {
                res = -1;
            }
            delete t;
            delete w;
            delete lock;
            delete ready;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    auto start = std::chrono::steady_clock::now();
    REQUIRE(fn() == 15);
    REQUIRE(std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(60));
}

TEST_CASE("ReadWriteLock: a writer waits for readers to leave", "[libk][sync][rwlock][thread]") {
    auto jit = jit_k(R"SRC(
        module __sync_rw_writer_waits__;

        public struct Shared {
            lock : ReadWriteLock*;
            ready : CountDownLatch*;
            done : CountDownLatch*;
            wrote : bool;
        }

        class Writer : public Runnable {
            private:
            _s : Shared*;
            public:
            _blockedWhileReading : bool = false;
            Writer(s: Shared*) { _s = s; }
            override run() : void throws(Throwable) {
                _s->ready->await();
                _blockedWhileReading = !_s->lock->tryWriteLock(Duration::ofMillis(20L));
                _s->done->countDown();
                _s->lock->writeLock();
                _s->wrote = true;
                _s->lock->writeUnlock();
            }
        }

        test() : int {
            s : Shared;
            lock : ReadWriteLock! = new ReadWriteLock();
            ready : CountDownLatch! = new CountDownLatch(1L);
            done : CountDownLatch! = new CountDownLatch(1L);
            s.lock = lock;
            s.ready = ready;
            s.done = done;
            s.wrote = false;

            w : Writer! = new Writer(&s);
            r : Runnable* = w;
            t : Thread! = new Thread(r);

            res : int = 0;
            try {
                lock->readLock();
                t->start();
                ready->countDown();
                // Wait for the writer to have failed its timed acquisition.
                done->await();
                if (!s.wrote) { ++res; }
                lock->readUnlock();
                t->join();
                if (s.wrote) { res += 2; }
                if (w->_blockedWhileReading) { res += 4; }
                if (!lock->isWriteLocked()) { res += 8; }
            } catch (e: Throwable&) {
                res = -1;
            }
            delete t;
            delete w;
            delete lock;
            delete ready;
            delete done;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 15);
}

TEST_CASE("ReadWriteLock: readers never observe a torn write", "[libk][sync][rwlock][thread]") {
    auto jit = jit_k(R"SRC(
        module __sync_rw_consistency__;

        public struct Shared {
            lock : ReadWriteLock*;
            a : long;
            b : long;
            torn : int;
            reads : long;
        }

        class Mutator : public Runnable {
            private:
            _s : Shared*;
            public:
            Mutator(s: Shared*) { _s = s; }
            override run() : void throws(Throwable) {
                i : int = 0;
                while (i < 500) {
                    _s->lock->writeLock();
                    _s->a = _s->a + 1L;
                    // A reader entering here would see a != b.
                    _s->b = _s->b + 1L;
                    _s->lock->writeUnlock();
                    ++i;
                }
            }
        }

        class Reader : public Runnable {
            private:
            _s : Shared*;
            public:
            _torn : int = 0;
            _reads : long = 0L;
            Reader(s: Shared*) { _s = s; }
            override run() : void throws(Throwable) {
                i : int = 0;
                while (i < 500) {
                    _s->lock->readLock();
                    if (_s->a != _s->b) { ++_torn; }
                    _reads += 1L;
                    _s->lock->readUnlock();
                    ++i;
                }
            }
        }

        test() : int {
            s : Shared;
            lock : ReadWriteLock! = new ReadWriteLock();
            s.lock = lock;
            s.a = 0L;
            s.b = 0L;

            m1 : Mutator! = new Mutator(&s);
            m2 : Mutator! = new Mutator(&s);
            rd1 : Reader! = new Reader(&s);
            rd2 : Reader! = new Reader(&s);
            r1 : Runnable* = m1;
            r2 : Runnable* = m2;
            r3 : Runnable* = rd1;
            r4 : Runnable* = rd2;
            t1 : Thread! = new Thread(r1);
            t2 : Thread! = new Thread(r2);
            t3 : Thread! = new Thread(r3);
            t4 : Thread! = new Thread(r4);
            t1->start(); t2->start(); t3->start(); t4->start();
            try { t1->join(); t2->join(); t3->join(); t4->join(); } catch (e: Throwable&) { }

            res : int = 0;
            if (rd1->_torn == 0 && rd2->_torn == 0) { ++res; }
            if (rd1->_reads == 500L && rd2->_reads == 500L) { res += 2; }
            if (s.a == 1000L && s.b == 1000L) { res += 4; }
            if (lock->readCount() == 0 && !lock->isWriteLocked()) { res += 8; }
            delete t1; delete t2; delete t3; delete t4;
            delete m1; delete m2; delete rd1; delete rd2;
            delete lock;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 15);
}
