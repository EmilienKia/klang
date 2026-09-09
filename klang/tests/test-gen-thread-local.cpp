/*
 * K Language compiler
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
 * Tests for thread-local storage variables ('threadlocal' specifier).
 *
 * 'threadlocal' is accepted on:
 *   - Local variables in functions and methods
 *   - Member variables in structures and classes
 *   - Global variables in namespaces
 *
 * Each thread receives its own storage for the variable.
 * Variables with non-constant initializers or destructors are lazily
 * initialized on first access in each thread, and destroyed in LIFO
 * order upon thread termination.
 */

#include <catch2/catch_test_macros.hpp>
#include "helpers.hpp"
#include <thread>
#include <atomic>

// =============================================================================
// 1. Threadlocal local variable with constant init persists across calls in same thread
// =============================================================================

TEST_CASE("Threadlocal local variable with constant init persists across calls",
          "[gen][threadlocal]") {
    auto jit = gen_jit(R"SRC(
        module gen_threadlocal_01;

        test() : int {
            threadlocal x : int = 42;
            ++x;
            return x;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 43);   // 42 + 1
    REQUIRE(test() == 44);   // persisted 43 + 1
    REQUIRE(test() == 45);   // persisted 44 + 1
}

// =============================================================================
// 2. Threadlocal local variable is independent across threads
// =============================================================================

TEST_CASE("Threadlocal local variable is independent across threads",
          "[gen][threadlocal]") {
    auto jit = gen_jit(R"SRC(
        module gen_threadlocal_02;

        test() : int {
            threadlocal count : int = 100;
            ++count;
            return count;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);

    // Main thread calls
    REQUIRE(test() == 101);
    REQUIRE(test() == 102);

    // Second thread starts from initial value 100
    int t1_res1 = 0, t1_res2 = 0;
    std::thread t1([&]() {
        t1_res1 = test();
        t1_res2 = test();
    });
    t1.join();

    REQUIRE(t1_res1 == 101);
    REQUIRE(t1_res2 == 102);

    // Main thread continues its own counter
    REQUIRE(test() == 103);
}

// =============================================================================
// 3. Threadlocal local with function-call init — called once per thread
// =============================================================================

TEST_CASE("Threadlocal local initialized by function call — called once per thread",
          "[gen][threadlocal]") {
    auto jit = gen_jit(R"SRC(
        module gen_threadlocal_03;

        call_count : int;

        make_value() : int {
            ++call_count;
            return 10;
        }

        test() : int {
            threadlocal v : int = make_value();
            ++v;
            return v;
        }

        get_call_count() : int {
            return call_count;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    auto get_call_count = jit->lookup_symbol<int(*)()>("get_call_count");
    REQUIRE(test != nullptr);
    REQUIRE(get_call_count != nullptr);

    REQUIRE(test() == 11);
    REQUIRE(test() == 12);
    REQUIRE(get_call_count() == 1);

    std::thread t1([&]() {
        REQUIRE(test() == 11);
        REQUIRE(test() == 12);
    });
    t1.join();

    REQUIRE(get_call_count() == 2);
}

// =============================================================================
// 4. Threadlocal global variable in namespace
// =============================================================================

TEST_CASE("Threadlocal global variable in namespace is isolated per thread",
          "[gen][threadlocal]") {
    auto jit = gen_jit(R"SRC(
        module gen_threadlocal_04;

        threadlocal g_val : int = 50;

        inc_and_get() : int {
            ++g_val;
            return g_val;
        }
    )SRC");
    REQUIRE(jit);

    auto inc_and_get = jit->lookup_symbol<int(*)()>("inc_and_get");
    REQUIRE(inc_and_get != nullptr);

    REQUIRE(inc_and_get() == 51);
    REQUIRE(inc_and_get() == 52);

    int thread_val = 0;
    std::thread t([&]() {
        thread_val = inc_and_get();
    });
    t.join();

    REQUIRE(thread_val == 51);
    REQUIRE(inc_and_get() == 53);
}

// =============================================================================
// 5. Threadlocal member variable in struct
// =============================================================================

TEST_CASE("Threadlocal member variable in struct",
          "[gen][threadlocal][struct]") {
    auto jit = gen_jit(R"SRC(
        module gen_threadlocal_05;

        struct Counter {
            threadlocal count : int = 0;

            static increment() : int {
                ++count;
                return count;
            }

            static get() : int {
                return count;
            }
        }

        test() : int {
            return Counter::increment();
        }

        get_direct() : int {
            return Counter::count;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    auto get_direct = jit->lookup_symbol<int(*)()>("get_direct");
    REQUIRE(test != nullptr);
    REQUIRE(get_direct != nullptr);

    REQUIRE(test() == 1);
    REQUIRE(test() == 2);
    REQUIRE(get_direct() == 2);

    int t_val = 0;
    std::thread t([&]() {
        test();
        test();
        t_val = test();
    });
    t.join();

    REQUIRE(t_val == 3);
    REQUIRE(get_direct() == 2);
}

// =============================================================================
// 6. Threadlocal member variable in class
// =============================================================================

TEST_CASE("Threadlocal member variable in class",
          "[gen][threadlocal][class]") {
    auto jit = gen_jit(R"SRC(
        module gen_threadlocal_06;

        class Context {
            threadlocal id : int = 7;

            public static get_id() : int {
                return id;
            }

            public static set_id(v : int) {
                id = v;
            }
        }

        test_get() : int {
            return Context::get_id();
        }

        test_set(v : int) {
            Context::set_id(v);
        }
    )SRC");
    REQUIRE(jit);

    auto test_get = jit->lookup_symbol<int(*)()>("test_get");
    auto test_set = jit->lookup_symbol<void(*)(int)>("test_set");
    REQUIRE(test_get != nullptr);
    REQUIRE(test_set != nullptr);

    REQUIRE(test_get() == 7);
    test_set(42);
    REQUIRE(test_get() == 42);

    int other_val = 0;
    std::thread t([&]() {
        other_val = test_get();
        test_set(99);
        other_val = test_get();
    });
    t.join();

    REQUIRE(other_val == 99);
    REQUIRE(test_get() == 42);
}

// =============================================================================
// 7. Threadlocal variable with destructor called on thread exit
// =============================================================================

TEST_CASE("Threadlocal variable destructor called on thread exit",
          "[gen][threadlocal][dtor]") {
    auto jit = gen_jit(R"SRC(
        module gen_threadlocal_07;

        dtor_count : int;

        struct Tracker {
            val : int;
            Tracker(v : int) { val = v; }
            ~Tracker() {
                ++dtor_count;
            }
        }

        worker() : int {
            threadlocal t : Tracker(10);
            return t.val;
        }

        get_dtor_count() : int {
            return dtor_count;
        }
    )SRC");
    REQUIRE(jit);

    auto worker = jit->lookup_symbol<int(*)()>("worker");
    auto get_dtor_count = jit->lookup_symbol<int(*)()>("get_dtor_count");
    REQUIRE(worker != nullptr);
    REQUIRE(get_dtor_count != nullptr);

    REQUIRE(get_dtor_count() == 0);

    // Run in a separate thread: t must be destroyed when the thread exits
    std::thread t([&]() {
        int v = worker();
        (void)v;
    });
    t.join();

    // After thread exit, Tracker's destructor must have run exactly once!
    REQUIRE(get_dtor_count() == 1);

    // Another thread
    std::thread t2([&]() {
        worker();
        worker();
    });
    t2.join();

    REQUIRE(get_dtor_count() == 2);
}

// =============================================================================
// 8. Error on combining 'threadlocal' and 'static'
// =============================================================================

TEST_CASE("Error: 'threadlocal' and 'static' cannot be combined",
          "[gen][threadlocal][error]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module gen_threadlocal_err_01;

        test() : int {
            threadlocal static x : int = 0;
            return x;
        }
    )SRC"));

    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module gen_threadlocal_err_02;

        test() : int {
            static threadlocal x : int = 0;
            return x;
        }
    )SRC"));
}
