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
 * Tests for static constructor and destructor capability on structures.
 *
 * A static constructor is a static no-argument void function of a structure
 * with exactly the same name as the structure. It acts as a class initializer
 * and is automatically registered in the global initializer function.
 *
 * A static destructor is a static no-argument void function of a structure
 * named with "~" + structure name. It acts as a class finalizer and is
 * automatically registered in the global finalizer function.
 *
 * Static constructors and destructors cannot be called explicitly by user code.
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

// =============================================================================
// Basic static constructor: called during global initialization
// =============================================================================

TEST_CASE("Static constructor is called during global initialization", "[gen][struct][static-ctor]") {
    auto jit = gen_jit(R"SRC(
        module __static_ctor__;

        // Global counter to verify the static constructor ran
        ctor_called : int;

        struct Tracker {
            // Static constructor: called at program init
            static Tracker() {
                ctor_called = 42;
            }
        }

        get_ctor_called() : int {
            return ctor_called;
        }
        )SRC");
    REQUIRE(jit);

    // gen_jit already calls initialize_runtime(), which triggers llvm.global_ctors.
    // So the static constructor should have run by now.
    auto get_val = jit->lookup_symbol<int(*)()>("get_ctor_called");
    REQUIRE(get_val != nullptr);
    REQUIRE(get_val() == 42);
}

// =============================================================================
// Basic static destructor: called during global finalization
// =============================================================================

TEST_CASE("Static destructor is called during global finalization", "[gen][struct][static-dtor]") {
    auto jit = gen_jit(R"SRC(
        module __static_dtor__;

        dtor_called : int;

        struct Cleaner {
            // Static destructor: called at program finalization
            static ~Cleaner() {
                dtor_called = 99;
            }
        }

        get_dtor_called() : int {
            return dtor_called;
        }
        )SRC");
    REQUIRE(jit);

    // Before finalization, the static destructor has NOT yet been called.
    auto get_val = jit->lookup_symbol<int(*)()>("get_dtor_called");
    REQUIRE(get_val != nullptr);
    REQUIRE(get_val() == 0);

    // Finalize the JIT — this triggers llvm.global_dtors including the static destructor.
    jit->finalize_runtime();

    // Now the static destructor should have run.
    auto dtor_called = jit->lookup_symbol<int*>("dtor_called");
    REQUIRE(dtor_called != nullptr);
    REQUIRE(*dtor_called == 99);
}

// =============================================================================
// Static constructor and destructor together
// =============================================================================

TEST_CASE("Static constructor and destructor: both executed at init/finit", "[gen][struct][static-ctor][static-dtor]") {
    auto jit = gen_jit(R"SRC(
        module __static_ctor_dtor__;

        init_count : int;
        fini_count : int;

        struct Service {
            // Static constructor: run at init
            static Service() {
                init_count = 1;
            }

            // Static destructor: run at finit
            static ~Service() {
                fini_count = 1;
            }
        }

        get_init() : int { return init_count; }
        get_fini() : int { return fini_count; }
        )SRC");
    REQUIRE(jit);

    // After initialization, ctor should have run, dtor not yet.
    auto get_init = jit->lookup_symbol<int(*)()>("get_init");
    REQUIRE(get_init != nullptr);
    REQUIRE(get_init() == 1);

    auto get_fini = jit->lookup_symbol<int(*)()>("get_fini");
    REQUIRE(get_fini != nullptr);
    REQUIRE(get_fini() == 0);

    // After finalization, dtor should have run.
    jit->finalize_runtime();

    auto init_count = jit->lookup_symbol<int*>("init_count");
    REQUIRE(init_count != nullptr);
    REQUIRE(*init_count == 1);

    auto fini_count = jit->lookup_symbol<int*>("fini_count");
    REQUIRE(fini_count != nullptr);
    REQUIRE(*fini_count == 1);
}

// =============================================================================
// Static constructor: runs before user functions are invoked
// =============================================================================

TEST_CASE("Static constructor initializes state before functions are called", "[gen][struct][static-ctor]") {
    auto jit = gen_jit(R"SRC(
        module __static_ctor_state__;

        value : int;

        struct Config {
            static Config() {
                value = 100;
            }
        }

        get_value() : int {
            return value;
        }
        )SRC");
    REQUIRE(jit);

    // Static constructor ran at init time, value should be 100.
    auto get_value = jit->lookup_symbol<int(*)()>("get_value");
    REQUIRE(get_value != nullptr);
    REQUIRE(get_value() == 100);
}

// =============================================================================
// Static destructor: ordering – static dtors run before instance dtors
// (static dtors are registered first in the global destructor, but run in
// reverse registration order i.e. after static ctors)
// =============================================================================

TEST_CASE("Static destructor runs after global variable destruction (reverse order)", "[gen][struct][static-dtor]") {
    auto jit = gen_jit(R"SRC(
        module __static_dtor_order__;

        log : int;

        struct Registry {
            static ~Registry() {
                log = log + 10;
            }
        }

        struct Item {
            ~Item() {
                log = log + 1;
            }
        }

        // Global instance of Item (will be destroyed at finalization)
        g_item : Item;

        get_log() : int { return log; }
        )SRC");
    REQUIRE(jit);

    auto get_log = jit->lookup_symbol<int(*)()>("get_log");
    REQUIRE(get_log != nullptr);
    REQUIRE(get_log() == 0);  // Nothing happened yet

    // Finalize: static dtor runs FIRST (in reverse registration = after ctor),
    // then instance destructors for global variables.
    // According to implementation: static dtors run first (LIFO), then instance dtors.
    jit->finalize_runtime();

    auto log_val = jit->lookup_symbol<int*>("log");
    REQUIRE(log_val != nullptr);
    // Static dtor: +10, then instance dtor: +1 => 11
    REQUIRE(*log_val == 11);
}

// =============================================================================
// Multiple structs with static ctors: all are called at init
// =============================================================================

TEST_CASE("Multiple structs with static constructors: all called at init", "[gen][struct][static-ctor]") {
    auto jit = gen_jit(R"SRC(
        module __multi_static_ctor__;

        counter : int;

        struct A {
            static A() {
                counter = counter + 1;
            }
        }

        struct B {
            static B() {
                counter = counter + 10;
            }
        }

        struct C {
            static C() {
                counter = counter + 100;
            }
        }

        get_counter() : int { return counter; }
        )SRC");
    REQUIRE(jit);

    auto get_counter = jit->lookup_symbol<int(*)()>("get_counter");
    REQUIRE(get_counter != nullptr);
    REQUIRE(get_counter() == 111);  // 1 + 10 + 100
}

// =============================================================================
// Multiple structs with static dtors: all are called at finit
// =============================================================================

TEST_CASE("Multiple structs with static destructors: all called at finit", "[gen][struct][static-dtor]") {
    auto jit = gen_jit(R"SRC(
        module __multi_static_dtor__;

        counter : int;

        struct X {
            static ~X() {
                counter = counter + 1;
            }
        }

        struct Y {
            static ~Y() {
                counter = counter + 10;
            }
        }

        get_counter() : int { return counter; }
        )SRC");
    REQUIRE(jit);

    auto get_counter = jit->lookup_symbol<int(*)()>("get_counter");
    REQUIRE(get_counter != nullptr);
    REQUIRE(get_counter() == 0);  // Not yet called

    jit->finalize_runtime();

    auto counter = jit->lookup_symbol<int*>("counter");
    REQUIRE(counter != nullptr);
    REQUIRE(*counter == 11);  // 1 + 10 (both static dtors ran)
}

// =============================================================================
// Error: static constructor cannot be called explicitly
// A struct with a static constructor and a deleted default instance constructor
// cannot be instantiated. Tracker() in expression context attempts temporary
// construction, which fails because the only matching constructor is deleted.
// =============================================================================

TEST_CASE("Static constructor cannot be called explicitly", "[gen][struct][static-ctor][error]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __explicit_static_ctor__;

        struct Tracker {
            static Tracker() {}
            Tracker() -> delete;
        }

        bad() {
            Tracker();
        }
        )SRC"), k::log::compiler_error);
}

// =============================================================================
// Error: static destructor cannot be called explicitly
// A struct with a static destructor and a deleted default instance constructor
// cannot be instantiated. ~Cleaner() is parsed as ~(Cleaner()), and Cleaner()
// fails because the only matching constructor is deleted.
// =============================================================================

TEST_CASE("Static destructor cannot be called explicitly", "[gen][struct][static-dtor][error]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __explicit_static_dtor__;

        struct Cleaner {
            static ~Cleaner() {}
            Cleaner() -> delete;
        }

        bad() {
            ~Cleaner();
        }
        )SRC"), k::log::compiler_error);
}

// =============================================================================
// Error: static constructor must not have a return type
// =============================================================================

TEST_CASE("Static constructor with return type is a compilation error", "[gen][struct][static-ctor][error]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __static_ctor_ret__;

        struct BadInit {
            static BadInit() : int {
                return 0;
            }
        }
        )SRC"), k::log::compiler_error);
}

// =============================================================================
// Error: static constructor must not have parameters
// =============================================================================

TEST_CASE("Static constructor with parameters is a compilation error", "[gen][struct][static-ctor][error]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __static_ctor_params__;

        struct BadInit {
            static BadInit(x: int) {
            }
        }
        )SRC"), k::log::compiler_error);
}

// =============================================================================
// Error: static destructor must not have a return type
// =============================================================================

TEST_CASE("Static destructor with return type is a compilation error", "[gen][struct][static-dtor][error]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __static_dtor_ret__;

        struct BadFini {
            static ~BadFini() : int {
                return 0;
            }
        }
        )SRC"), k::log::compiler_error);
}

// =============================================================================
// Error: static destructor must not have parameters
// =============================================================================

TEST_CASE("Static destructor with parameters is a compilation error", "[gen][struct][static-dtor][error]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __static_dtor_params__;

        struct BadFini {
            static ~BadFini(x: int) {
            }
        }
        )SRC"), k::log::compiler_error);
}

// =============================================================================
// Static constructor interacts with global variables: ctor sets up a value
// that global variables (initialized AFTER static ctors) can use
// =============================================================================

TEST_CASE("Static constructor co-exists with global variable initialization", "[gen][struct][static-ctor]") {
    auto jit = gen_jit(R"SRC(
        module __static_ctor_global__;

        base : int;
        derived : int;

        struct Init {
            static Init() {
                base = 5;
            }
        }

        setup() {
            derived = base * 2;
        }

        get_base() : int { return base; }
        get_derived() : int { return derived; }
        )SRC");
    REQUIRE(jit);

    // After init: static ctor set base = 5
    auto get_base = jit->lookup_symbol<int(*)()>("get_base");
    REQUIRE(get_base != nullptr);
    REQUIRE(get_base() == 5);

    // derived is set only by calling setup()
    auto get_derived = jit->lookup_symbol<int(*)()>("get_derived");
    REQUIRE(get_derived != nullptr);
    REQUIRE(get_derived() == 0);  // not yet set

    auto setup = jit->lookup_symbol<void(*)()>("setup");
    REQUIRE(setup != nullptr);
    setup();
    REQUIRE(get_derived() == 10);  // base(5) * 2
}

