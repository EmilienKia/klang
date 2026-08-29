/*
 * K Language standard library — Future/Promise tests (Phase 2)
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
 * Tests for k::Future<T> and k::Promise<T>.
 *
 * Phase 2 coverage:
 *  - trySuccess() publishes a value observed by get()
 *  - a second publication attempt is rejected
 *  - tryFailure() makes get() throw ExecutionException carrying the cause
 *  - cancel() / tryCancel() make get() throw CancellationException
 *  - get(Duration) throws TimeoutException when the deadline expires
 *  - get() blocks until another thread publishes the value
 *  - a blocking get() is interruptible
 *  - completion wins over a concurrent interruption
 *  - several Future handles observe the same result
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
// Completion — success
// =============================================================================

TEST_CASE("Future: a fresh promise is pending", "[libk][future]") {
    auto jit = jit_k(R"SRC(
        module __fut_pending__;
        test() : int {
            p : Promise<int>;
            f : Future<int>! = p.future();
            r : int = 0;
            if (f->isDone()) { ++r; }
            if (f->isSuccess()) { r += 2; }
            if (f->isFailed()) { r += 4; }
            if (f->isCancelled()) { r += 8; }
            delete f;
            return r;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 0);
}

TEST_CASE("Future: trySuccess publishes a value", "[libk][future]") {
    auto jit = jit_k(R"SRC(
        module __fut_success__;
        test() : int {
            p : Promise<int>;
            f : Future<int>! = p.future();
            if (!p.trySuccess(42)) { delete f; return -1; }
            if (!f->isDone()) { delete f; return -2; }
            if (!f->isSuccess()) { delete f; return -3; }
            v : int = -4;
            try { v = f->get(); } catch (e: Throwable&) { v = -5; }
            delete f;
            return v;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 42);
}

TEST_CASE("Future: a second completion attempt is rejected", "[libk][future]") {
    auto jit = jit_k(R"SRC(
        module __fut_once__;
        test() : int {
            p : Promise<int>;
            f : Future<int>! = p.future();
            r : int = 0;
            if (p.trySuccess(1)) { ++r; }
            if (p.trySuccess(2)) { r += 10; }
            if (p.tryCancel()) { r += 100; }
            v : int = 0;
            try { v = f->get(); } catch (e: Throwable&) { v = -1; }
            delete f;
            return r * 1000 + v;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    // only the first trySuccess succeeds (r == 1) and the value stays 1
    REQUIRE(fn() == 1001);
}

TEST_CASE("Future: get() on a completed future does not block", "[libk][future]") {
    auto jit = jit_k(R"SRC(
        module __fut_nonblock__;
        test() : int {
            p : Promise<int>;
            f : Future<int>! = p.future();
            p.trySuccess(7);
            v : int = 0;
            try { v = f->get(Duration::ofSeconds(10L)); } catch (e: Throwable&) { v = -1; }
            delete f;
            return v;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    auto start = std::chrono::steady_clock::now();
    REQUIRE(fn() == 7);
    auto elapsed = std::chrono::steady_clock::now() - start;
    REQUIRE(elapsed < std::chrono::seconds(2));
}

// =============================================================================
// Completion — failure and cancellation
// =============================================================================

TEST_CASE("Future: tryFailure makes get() throw ExecutionException", "[libk][future]") {
    auto jit = jit_k(R"SRC(
        module __fut_failure__;
        test() : int {
            p : Promise<int>;
            f : Future<int>! = p.future();
            if (!p.tryFailure(new Exception(77))) { delete f; return -1; }
            if (!f->isFailed()) { delete f; return -2; }
            r : int = -3;
            try {
                v : int = f->get();
                r = -4;
            } catch (e: ExecutionException&) {
                c : Throwable* = e.getCause();
                if (c == null) { r = -5; } else { r = c->getCode(); }
            } catch (other: Throwable&) {
                r = -6;
            }
            delete f;
            return r;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 77);
}

TEST_CASE("Future: cancel makes get() throw CancellationException", "[libk][future]") {
    auto jit = jit_k(R"SRC(
        module __fut_cancel__;
        test() : int {
            p : Promise<int>;
            f : Future<int>! = p.future();
            if (!f->cancel()) { delete f; return -1; }
            if (!f->isCancelled()) { delete f; return -2; }
            if (!f->isDone()) { delete f; return -3; }
            r : int = -4;
            try {
                v : int = f->get();
                r = -5;
            } catch (e: CancellationException&) {
                r = 1;
            } catch (other: Throwable&) {
                r = -6;
            }
            delete f;
            return r;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}

TEST_CASE("Future: tryCancel from the promise side", "[libk][future]") {
    auto jit = jit_k(R"SRC(
        module __fut_promise_cancel__;
        test() : int {
            p : Promise<int>;
            f : Future<int>! = p.future();
            if (!p.tryCancel()) { delete f; return -1; }
            if (!f->isCancelled()) { delete f; return -2; }
            if (p.trySuccess(3)) { delete f; return -3; }
            delete f;
            return 1;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}

// =============================================================================
// Timed wait
// =============================================================================

TEST_CASE("Future: get(Duration) times out on a pending future", "[libk][future]") {
    auto jit = jit_k(R"SRC(
        module __fut_timeout__;
        test() : int {
            p : Promise<int>;
            f : Future<int>! = p.future();
            r : int = -1;
            try {
                v : int = f->get(Duration::ofMillis(50L));
                r = -2;
            } catch (e: TimeoutException&) {
                r = 1;
            } catch (other: Throwable&) {
                r = -3;
            }
            delete f;
            return r;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    auto start = std::chrono::steady_clock::now();
    REQUIRE(fn() == 1);
    auto elapsed = std::chrono::steady_clock::now() - start;
    REQUIRE(elapsed >= std::chrono::milliseconds(40));
    REQUIRE(elapsed < std::chrono::seconds(5));
}

// =============================================================================
// Multiple handles
// =============================================================================

TEST_CASE("Future: several handles observe the same result", "[libk][future]") {
    auto jit = jit_k(R"SRC(
        module __fut_share__;
        test() : int {
            p : Promise<int>;
            f1 : Future<int>! = p.future();
            f2 : Future<int>! = p.future();
            f3 : Future<int>! = f1->share();
            p.trySuccess(11);
            r : int = 0;
            try {
                r = f1->get() + f2->get() + f3->get();
            } catch (e: Throwable&) {
                r = -1;
            }
            delete f1;
            delete f2;
            delete f3;
            return r;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 33);
}

TEST_CASE("Future: a handle outliving its promise still reads the value", "[libk][future]") {
    auto jit = jit_k(R"SRC(
        module __fut_outlive__;
        makeFuture() : Future<int>! {
            p : Promise<int>;
            f : Future<int>! = p.future();
            p.trySuccess(5);
            return f;
        }
        test() : int {
            f : Future<int>! = makeFuture();
            v : int = 0;
            try { v = f->get(); } catch (e: Throwable&) { v = -1; }
            delete f;
            return v;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 5);
}

// =============================================================================
// Cross-thread completion
// =============================================================================

TEST_CASE("Future: get() blocks until another thread publishes", "[libk][future][thread]") {
    auto jit = jit_k(R"SRC(
        module __fut_cross__;

        class Completer : public Runnable {
            private:
            _p : Promise<int>*;
            _delayMillis : long;
            public:
            Completer(p: Promise<int>*, delayMillis: long) {
                _p = p;
                _delayMillis = delayMillis;
            }
            override run() : void throws(Throwable) {
                Thread::sleep(Duration::ofMillis(_delayMillis));
                _p->trySuccess(99);
            }
        }

        test() : int {
            p : Promise<int>;
            f : Future<int>! = p.future();
            c : Runnable! = new Completer(&p, 60L);
            t : Thread! = new Thread(c);
            t->start();
            v : int = 0;
            try { v = f->get(); } catch (e: Throwable&) { v = -1; }
            try { t->join(); } catch (e2: Throwable&) { }
            delete t;
            delete c;
            delete f;
            return v;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    auto start = std::chrono::steady_clock::now();
    REQUIRE(fn() == 99);
    auto elapsed = std::chrono::steady_clock::now() - start;
    REQUIRE(elapsed >= std::chrono::milliseconds(40));
}

TEST_CASE("Future: a failure published from another thread reaches get()", "[libk][future][thread]") {
    auto jit = jit_k(R"SRC(
        module __fut_cross_fail__;

        class Failer : public Runnable {
            private:
            _p : Promise<int>*;
            public:
            Failer(p: Promise<int>*) { _p = p; }
            override run() : void throws(Throwable) {
                Thread::sleep(Duration::ofMillis(40L));
                _p->tryFailure(new Exception(31));
            }
        }

        test() : int {
            p : Promise<int>;
            f : Future<int>! = p.future();
            c : Runnable! = new Failer(&p);
            t : Thread! = new Thread(c);
            t->start();
            r : int = -1;
            try {
                v : int = f->get();
                r = -2;
            } catch (e: ExecutionException&) {
                cause : Throwable* = e.getCause();
                if (cause == null) { r = -3; } else { r = cause->getCode(); }
            } catch (other: Throwable&) {
                r = -4;
            }
            try { t->join(); } catch (e2: Throwable&) { }
            delete t;
            delete c;
            delete f;
            return r;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 31);
}

// =============================================================================
// Interruption
// =============================================================================

TEST_CASE("Future: a blocking get() is interruptible", "[libk][future][thread]") {
    auto jit = jit_k(R"SRC(
        module __fut_interrupt__;

        class Waiter : public Runnable {
            private:
            _f : Future<int>*;
            public:
            _outcome : int = 0;
            Waiter(f: Future<int>*) { _f = f; }
            override run() : void throws(Throwable) {
                try {
                    v : int = _f->get();
                    _outcome = 1;
                } catch (e: ThreadInterruptionException&) {
                    _outcome = 2;
                } catch (other: Throwable&) {
                    _outcome = 3;
                }
            }
        }

        test() : int {
            p : Promise<int>;
            f : Future<int>! = p.future();
            w : Waiter! = new Waiter(f);
            r : Runnable* = w;
            t : Thread! = new Thread(r);
            t->start();
            Thread::sleep(Duration::ofMillis(60L));
            t->interrupt();
            try { t->join(); } catch (e: Throwable&) { }
            outcome : int = w->_outcome;
            delete t;
            delete w;
            delete f;
            return outcome;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 2);
}

TEST_CASE("Future: completion wins over a concurrent interruption", "[libk][future][thread]") {
    auto jit = jit_k(R"SRC(
        module __fut_race__;

        class Waiter : public Runnable {
            private:
            _f : Future<int>*;
            public:
            _outcome : int = 0;
            Waiter(f: Future<int>*) { _f = f; }
            override run() : void throws(Throwable) {
                try {
                    _outcome = _f->get();
                } catch (e: ThreadInterruptionException&) {
                    _outcome = -2;
                } catch (other: Throwable&) {
                    _outcome = -3;
                }
            }
        }

        test() : int {
            p : Promise<int>;
            f : Future<int>! = p.future();
            w : Waiter! = new Waiter(f);
            r : Runnable* = w;
            t : Thread! = new Thread(r);
            t->start();
            Thread::sleep(Duration::ofMillis(50L));
            // Publish the value, then interrupt: a value that arrived first
            // must never be reported as an interruption.
            p.trySuccess(64);
            t->interrupt();
            try { t->join(); } catch (e: Throwable&) { }
            outcome : int = w->_outcome;
            delete t;
            delete w;
            delete f;
            return outcome;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 64);
}

TEST_CASE("Future: an already-interrupted thread fails a blocking get()", "[libk][future][thread]") {
    auto jit = jit_k(R"SRC(
        module __fut_pre_interrupt__;

        class Waiter : public Runnable {
            private:
            _f : Future<int>*;
            public:
            _outcome : int = 0;
            Waiter(f: Future<int>*) { _f = f; }
            override run() : void throws(Throwable) {
                self : Thread! = Thread::current();
                self->interrupt();
                try {
                    v : int = _f->get();
                    _outcome = 1;
                } catch (e: ThreadInterruptionException&) {
                    _outcome = 2;
                } catch (other: Throwable&) {
                    _outcome = 3;
                }
            }
        }

        test() : int {
            p : Promise<int>;
            f : Future<int>! = p.future();
            w : Waiter! = new Waiter(f);
            r : Runnable* = w;
            t : Thread! = new Thread(r);
            t->start();
            try { t->join(); } catch (e: Throwable&) { }
            outcome : int = w->_outcome;
            delete t;
            delete w;
            delete f;
            return outcome;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 2);
}
