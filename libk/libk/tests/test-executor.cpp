/*
 * K Language standard library — Executor and Thread Pool tests
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
 * Tests for k::Executor, k::DirectExecutor, k::SingleThreadExecutor,
 * and k::ThreadPoolExecutor.
 *
 * Coverage:
 *  - DirectExecutor: synchronous execution in caller thread, null safety
 *  - SingleThreadExecutor: asynchronous execution, FIFO sequential order
 *  - SingleThreadExecutor: priority-based task scheduling
 *  - SingleThreadExecutor: exception isolation (worker thread survives thrown exceptions)
 *  - SingleThreadExecutor: shutdown lifecycle and RejectedExecutionException
 *  - ThreadPoolExecutor: concurrent execution across multiple worker threads
 *  - ThreadPoolExecutor: priority task scheduling among workers
 *  - ThreadPoolExecutor: awaitTermination and shutdownNow
 *  - ThreadPoolExecutor: batch processing under load
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
// DirectExecutor tests
// =============================================================================

TEST_CASE("DirectExecutor: synchronous execution in calling thread", "[libk][executor][direct]") {
    auto jit = jit_k(R"SRC(
        module __test_direct_exec_sync__;

        g_value : int = 0;
        task() : void {
            g_value = 42;
        }

        test() : int {
            g_value = 0;
            exec : DirectExecutor;
            exec.execute(task);
            return g_value;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 42);
}

TEST_CASE("DirectExecutor: priority overload executes synchronously", "[libk][executor][direct]") {
    auto jit = jit_k(R"SRC(
        module __test_direct_exec_prio__;

        g_value : int = 0;
        task() : void {
            g_value = 100;
        }

        test() : int {
            g_value = 0;
            exec : DirectExecutor;
            exec.execute(task, 10);
            return g_value;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 100);
}

TEST_CASE("DirectExecutor: null task is safely ignored", "[libk][executor][direct]") {
    auto jit = jit_k(R"SRC(
        module __test_direct_exec_null__;

        test() : int {
            exec : DirectExecutor;
            dummy : int = 0;
            f1 : !() = [dummy]() {};
            f2 : !() = f1; // f1 is now null after move
            exec.execute(f1);
            return 1;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}

// =============================================================================
// SingleThreadExecutor tests
// =============================================================================

TEST_CASE("SingleThreadExecutor: executes task in worker thread", "[libk][executor][single]") {
    auto jit = jit_k(R"SRC(
        module __test_single_exec_basic__;

        g_lock  : Mutex;
        g_latch : CountDownLatch(1L);
        g_value : int = 0;

        task() : void {
            g_lock.lock();
            g_value = 42;
            g_lock.unlock();
            g_latch.countDown();
        }

        test() : int {
            g_value = 0;
            exec : SingleThreadExecutor;
            exec.execute(task);

            g_latch.await();
            g_lock.lock();
            res : int = g_value;
            g_lock.unlock();

            exec.shutdown();
            exec.awaitTermination(Duration::ofSeconds(2L));
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 42);
}

TEST_CASE("SingleThreadExecutor: executes tasks in FIFO order for equal priority", "[libk][executor][single]") {
    auto jit = jit_k(R"SRC(
        module __test_single_exec_fifo__;

        g_lock    : Mutex;
        g_latch   : CountDownLatch(3L);
        g_order   : int = 0;
        g_success : bool = true;

        task1() : void {
            g_lock.lock();
            if (g_order != 0) { g_success = false; }
            g_order = 1;
            g_lock.unlock();
            g_latch.countDown();
        }

        task2() : void {
            g_lock.lock();
            if (g_order != 1) { g_success = false; }
            g_order = 2;
            g_lock.unlock();
            g_latch.countDown();
        }

        task3() : void {
            g_lock.lock();
            if (g_order != 2) { g_success = false; }
            g_order = 3;
            g_lock.unlock();
            g_latch.countDown();
        }

        test() : int {
            exec : SingleThreadExecutor;

            exec.execute(task1);
            exec.execute(task2);
            exec.execute(task3);

            g_latch.await();
            exec.shutdown();
            exec.awaitTermination(Duration::ofSeconds(2L));

            g_lock.lock();
            res : int = (g_success && g_order == 3) ? 1 : 0;
            g_lock.unlock();
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}

TEST_CASE("SingleThreadExecutor: priority-based task scheduling", "[libk][executor][single][priority]") {
    auto jit = jit_k(R"SRC(
        module __test_single_exec_prio__;

        g_blocker : CountDownLatch(1L);
        g_done    : CountDownLatch(4L);
        g_lock    : Mutex;
        g_history : int = 0;

        blockingTask() : void {
            // Block the single worker until all queued tasks are submitted
            g_blocker.await();
            g_done.countDown();
        }

        lowPrioTask() : void {
            g_lock.lock();
            g_history = g_history * 10 + 1; // 1 represents low priority task
            g_lock.unlock();
            g_done.countDown();
        }

        mediumPrioTask() : void {
            g_lock.lock();
            g_history = g_history * 10 + 2; // 2 represents medium priority task
            g_lock.unlock();
            g_done.countDown();
        }

        highPrioTask() : void {
            g_lock.lock();
            g_history = g_history * 10 + 3; // 3 represents high priority task
            g_lock.unlock();
            g_done.countDown();
        }

        test() : int {
            exec : SingleThreadExecutor;

            // First task blocks the worker thread
            exec.execute(blockingTask);

            // Queue up tasks with varying priorities while the worker is blocked
            exec.execute(lowPrioTask, 1);
            exec.execute(highPrioTask, 10);
            exec.execute(mediumPrioTask, 5);

            // Unblock the worker thread: high (3) then medium (2) then low (1) should execute
            g_blocker.countDown();
            g_done.await();

            exec.shutdown();
            exec.awaitTermination(Duration::ofSeconds(2L));

            g_lock.lock();
            res : int = g_history;
            g_lock.unlock();

            // Expected order: 3 (high), then 2 (medium), then 1 (low) => history = 321
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 321);
}

TEST_CASE("SingleThreadExecutor: exception in task does not kill worker thread", "[libk][executor][single][exception]") {
    auto jit = jit_k(R"SRC(
        module __test_single_exec_exc__;

        public class CustomException : public Exception {
        public:
            CustomException() : Exception(999) {}
        }

        g_latch : CountDownLatch(2L);
        g_lock  : Mutex;
        g_stage : int = 0;

        failingTask() : void {
            g_lock.lock();
            g_stage = 1;
            g_lock.unlock();
            g_latch.countDown();
            throw CustomException();
        }

        succeedingTask() : void {
            g_lock.lock();
            g_stage = 2;
            g_lock.unlock();
            g_latch.countDown();
        }

        test() : int {
            exec : SingleThreadExecutor;

            exec.execute(failingTask);
            exec.execute(succeedingTask);

            g_latch.await();
            exec.shutdown();
            exec.awaitTermination(Duration::ofSeconds(2L));

            g_lock.lock();
            res : int = g_stage;
            g_lock.unlock();

            // Stage 2 must have completed despite task 1 throwing
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 2);
}

TEST_CASE("SingleThreadExecutor: RejectedExecutionException after shutdown", "[libk][executor][single][shutdown]") {
    auto jit = jit_k(R"SRC(
        module __test_single_exec_reject__;

        noop() : void {}

        test() : int {
            exec : SingleThreadExecutor;
            exec.shutdown();

            if (!exec.isShutdown()) {
                return 1;
            }

            try {
                exec.execute(noop);
                return 2; // Should not reach here
            } catch (e: RejectedExecutionException&) {
                return 42; // Correctly caught RejectedExecutionException
            } catch (o: Throwable&) {
                return 3;
            }
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 42);
}

// =============================================================================
// ThreadPoolExecutor tests
// =============================================================================

TEST_CASE("ThreadPoolExecutor: concurrent execution with multiple workers", "[libk][executor][pool]") {
    auto jit = jit_k(R"SRC(
        module __test_pool_exec_concurrent__;

        g_startLatch : CountDownLatch(3L);
        g_allReady   : CountDownLatch(1L);
        g_doneLatch  : CountDownLatch(3L);
        g_lock       : Mutex;
        g_activeMax  : int = 0;
        g_current    : int = 0;

        workerTask() : void {
            // Signal that this worker is running
            g_startLatch.countDown();
            // Wait for all 3 workers to reach this point simultaneously
            g_allReady.await();

            g_lock.lock();
            g_current = g_current + 1;
            if (g_current > g_activeMax) {
                g_activeMax = g_current;
            }
            g_lock.unlock();

            Thread::sleep(Duration::ofMillis(20L));

            g_lock.lock();
            g_current = g_current - 1;
            g_lock.unlock();

            g_doneLatch.countDown();
        }

        test() : int {
            exec : ThreadPoolExecutor(3);

            exec.execute(workerTask);
            exec.execute(workerTask);
            exec.execute(workerTask);

            // Wait until all 3 workers started
            g_startLatch.await();
            // Release them simultaneously
            g_allReady.countDown();

            g_doneLatch.await();
            exec.shutdown();
            exec.awaitTermination(Duration::ofSeconds(2L));

            g_lock.lock();
            maxConc : int = g_activeMax;
            g_lock.unlock();

            return maxConc;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 3);
}

TEST_CASE("ThreadPoolExecutor: batch task processing", "[libk][executor][pool][stress]") {
    auto jit = jit_k(R"SRC(
        module __test_pool_exec_batch__;

        g_lock    : Mutex;
        g_counter : int = 0;
        g_latch   : CountDownLatch(60L);

        increment() : void {
            g_lock.lock();
            g_counter = g_counter + 1;
            g_lock.unlock();
            g_latch.countDown();
        }

        test() : int {
            exec : ThreadPoolExecutor(4);

            i : int = 0;
            while (i < 60) {
                exec.execute(increment);
                ++i;
            }

            g_latch.await();
            exec.shutdown();
            exec.awaitTermination(Duration::ofSeconds(5L));

            g_lock.lock();
            res : int = g_counter;
            g_lock.unlock();

            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 60);
}

TEST_CASE("ThreadPoolExecutor: shutdownNow clears pending tasks", "[libk][executor][pool][shutdownNow]") {
    auto jit = jit_k(R"SRC(
        module __test_pool_exec_shutdown_now__;

        g_blocker  : CountDownLatch(1L);
        g_executed : int = 0;
        g_lock     : Mutex;

        blockingTask() : void {
            g_blocker.await();
        }

        pendingTask() : void {
            g_lock.lock();
            g_executed = g_executed + 1;
            g_lock.unlock();
        }

        test() : int {
            exec : SingleThreadExecutor;

            // Worker blocked
            exec.execute(blockingTask);

            // Queue pending tasks
            exec.execute(pendingTask);
            exec.execute(pendingTask);

            // shutdownNow() clears pending tasks and interrupts worker
            exec.shutdownNow();

            // Release blocker in case worker is still waiting
            g_blocker.countDown();

            exec.awaitTermination(Duration::ofSeconds(2L));

            g_lock.lock();
            res : int = g_executed;
            g_lock.unlock();

            // Pending tasks should not have executed
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 0);
}

// =============================================================================
// Executor template execute<R> with Future<R> return tests (UCS)
// =============================================================================

TEST_CASE("Executor: template execute<R> returns Future on DirectExecutor", "[libk][executor][future]") {
    auto jit = jit_k(R"SRC(
        module __test_exec_future_direct__;

        test() : int throws(Throwable) {
            exec : DirectExecutor;
            f : Future<int>! = exec.execute<int>([]() : int { return 42; });
            res : int = 0;
            if (f != null && f->isDone() && f->isSuccess()) {
                try {
                    res = f->get();
                } catch (e: Throwable&) {
                    res = -1;
                }
            }
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 42);
}

TEST_CASE("Executor: template execute<R> returns Future on SingleThreadExecutor", "[libk][executor][future][single]") {
    auto jit = jit_k(R"SRC(
        module __test_exec_future_single__;

        test() : int throws(Throwable) {
            exec : SingleThreadExecutor;
            f : Future<int>! = exec.execute<int>([]() : int {
                Thread::sleep(Duration::ofMillis(20L));
                return 100;
            });
            res : int = 0;
            try {
                res = f->get(Duration::ofSeconds(2L));
            } catch (e: Throwable&) {
                res = -1;
            }
            exec.shutdown();
            exec.awaitTermination(Duration::ofSeconds(2L));
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 100);
}

TEST_CASE("Executor: template execute<R> with priority on SingleThreadExecutor", "[libk][executor][future][priority]") {
    auto jit = jit_k(R"SRC(
        module __test_exec_future_prio__;

        g_blocker : CountDownLatch(1L);

        blockingTask() : void {
            g_blocker.await();
        }

        test() : int throws(Throwable) {
            exec : SingleThreadExecutor;

            // Block worker
            exec.execute(blockingTask);

            // Queue low and high priority value tasks
            fLow : Future<int>! = exec.execute<int>([]() : int { return 1; }, 1);
            fHigh : Future<int>! = exec.execute<int>([]() : int { return 2; }, 10);

            // Unblock worker
            g_blocker.countDown();

            resHigh : int = 0;
            resLow : int = 0;
            try {
                resHigh = fHigh->get(Duration::ofSeconds(2L));
                resLow = fLow->get(Duration::ofSeconds(2L));
            } catch (e: Throwable&) {
            }

            exec.shutdown();
            exec.awaitTermination(Duration::ofSeconds(2L));

            return resHigh * 10 + resLow; // 21
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 21);
}

TEST_CASE("Executor: template execute<R> propagates task exception through Future", "[libk][executor][future][exception]") {
    auto jit = jit_k(R"SRC(
        module __test_exec_future_exc__;

        public class CustomErr : public Exception {
        public:
            CustomErr() : Exception(789) {}
        }

        test() : int throws(Throwable) {
            exec : SingleThreadExecutor;
            f : Future<int>! = exec.execute<int>([]() : int {
                throw CustomErr();
                return 0;
            });
            res : int = 0;
            try {
                v : int = f->get(Duration::ofSeconds(5L));
                res = 1; // Should not reach
            } catch (e: ExecutionException&) {
                c : Throwable* = e.getCause();
                if (c != null && c->getCode() == 789) {
                    res = 42; // Correct exception code
                } else {
                    res = 2;
                }
            } catch (o: Throwable&) {
                res = 3;
            }
            exec.shutdown();
            exec.awaitTermination(Duration::ofSeconds(5L));
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 42);
}

TEST_CASE("Executor: template execute<R> on ThreadPoolExecutor concurrent futures", "[libk][executor][future][pool]") {
    auto jit = jit_k(R"SRC(
        module __test_exec_future_pool__;

        test() : int throws(Throwable) {
            exec : ThreadPoolExecutor(3);

            f1 : Future<int>! = exec.execute<int>([]() : int { Thread::sleep(Duration::ofMillis(10L)); return 10; });
            f2 : Future<int>! = exec.execute<int>([]() : int { Thread::sleep(Duration::ofMillis(10L)); return 20; });
            f3 : Future<int>! = exec.execute<int>([]() : int { Thread::sleep(Duration::ofMillis(10L)); return 30; });

            total : int = 0;
            try {
                total = f1->get(Duration::ofSeconds(2L))
                      + f2->get(Duration::ofSeconds(2L))
                      + f3->get(Duration::ofSeconds(2L));
            } catch (e: Throwable&) {
                total = -1;
            }

            exec.shutdown();
            exec.awaitTermination(Duration::ofSeconds(2L));

            return total; // 60
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 60);
}
