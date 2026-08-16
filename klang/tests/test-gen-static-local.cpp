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
 * Tests for static local function variables.
 *
 * Static local variables are declared with the 'static' specifier inside
 * a function body.  They behave like global variables (persistent storage,
 * initialized as part of the global initialization sequence) but are only
 * accessible within the enclosing function scope.
 *
 * Their initializers must be constant or global expressions and they
 * participate in the init_order_resolver dependency graph.
 */

#include <catch2/catch_test_macros.hpp>
#include "helpers.hpp"

// =============================================================================
// 1. Static local with constant init — value persists across calls
// =============================================================================

TEST_CASE("Static local variable with constant init persists across calls",
          "[gen][static-local]") {
    auto jit = gen_jit(R"SRC(
        module __sl_const__;

        test() : int {
            static x : int = 42;
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
// 2. Static local with function-call init — init called only once
// =============================================================================

TEST_CASE("Static local variable initialized by function call — called once",
          "[gen][static-local]") {
    auto jit = gen_jit(R"SRC(
        module __sl_func_init__;

        call_count : int;

        make_value() : int {
            ++call_count;
            return 10;
        }

        test() : int {
            static v : int = make_value();
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

    REQUIRE(test() == 11);             // 10 + 1
    REQUIRE(test() == 12);             // persisted 11 + 1
    REQUIRE(get_call_count() == 1);    // make_value called only once (at global init)
}

// =============================================================================
// 3. Two functions with same-named static local — independent storage
// =============================================================================

TEST_CASE("Two functions with identically named static locals are independent",
          "[gen][static-local]") {
    auto jit = gen_jit(R"SRC(
        module __sl_name_unique__;

        foo() : int {
            static i : int = 100;
            ++i;
            return i;
        }

        bar() : int {
            static i : int = 200;
            ++i;
            return i;
        }
    )SRC");
    REQUIRE(jit);

    auto foo = jit->lookup_symbol<int(*)()>("foo");
    auto bar = jit->lookup_symbol<int(*)()>("bar");
    REQUIRE(foo != nullptr);
    REQUIRE(bar != nullptr);

    REQUIRE(foo() == 101);
    REQUIRE(bar() == 201);
    REQUIRE(foo() == 102);   // foo's i is independent
    REQUIRE(bar() == 202);   // bar's i is independent
}

// =============================================================================
// 4. Static local vs non-static local — different behaviour
// =============================================================================

TEST_CASE("Static local persists, non-static local resets each call",
          "[gen][static-local]") {
    auto jit = gen_jit(R"SRC(
        module __sl_vs_nonstatic__;

        test_static() : int {
            static s : int = 0;
            ++s;
            return s;
        }

        test_non_static() : int {
            n : int = 0;
            ++n;
            return n;
        }
    )SRC");
    REQUIRE(jit);

    auto test_static = jit->lookup_symbol<int(*)()>("test_static");
    auto test_non_static = jit->lookup_symbol<int(*)()>("test_non_static");
    REQUIRE(test_static != nullptr);
    REQUIRE(test_non_static != nullptr);

    REQUIRE(test_static() == 1);
    REQUIRE(test_static() == 2);
    REQUIRE(test_static() == 3);

    // Non-static always returns 1 (reset each call)
    REQUIRE(test_non_static() == 1);
    REQUIRE(test_non_static() == 1);
    REQUIRE(test_non_static() == 1);
}

// =============================================================================
// 5. Static local with dependency on a global variable — init order
// =============================================================================

TEST_CASE("Static local depending on global variable — correct init order",
          "[gen][static-local][init-order]") {
    auto jit = gen_jit(R"SRC(
        module __sl_dep_global__;

        base_val : int = 50;

        test() : int {
            static derived : int = base_val * 2;
            return derived;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    // base_val (50) must be initialized before derived (50*2=100)
    REQUIRE(test() == 100);
}

// =============================================================================
// 6. Static local with array brace init
// =============================================================================

TEST_CASE("Static local array with brace init",
          "[gen][static-local][brace-init]") {
    auto jit = gen_jit(R"SRC(
        module __sl_array__;

        get(idx : int) : int {
            static arr : int[3] {10, 20, 30};
            return arr[idx];
        }
    )SRC");
    REQUIRE(jit);

    auto get = jit->lookup_symbol<int(*)(int)>("get");
    REQUIRE(get != nullptr);
    REQUIRE(get(0) == 10);
    REQUIRE(get(1) == 20);
    REQUIRE(get(2) == 30);
}

// =============================================================================
// 7. Static local of struct type with constructor
// =============================================================================

TEST_CASE("Static local of struct type — constructor called at global init",
          "[gen][static-local][struct]") {
    auto jit = gen_jit(R"SRC(
        module __sl_struct__;

        ctor_count : int;

        struct Counter {
            value : int;

            Counter(v : int) {
                value = v;
                ++ctor_count;
            }
        }

        test() : int {
            static c : Counter(42);
            return c.value;
        }

        get_ctor_count() : int {
            return ctor_count;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    auto get_ctor_count = jit->lookup_symbol<int(*)()>("get_ctor_count");
    REQUIRE(test != nullptr);
    REQUIRE(get_ctor_count != nullptr);

    // Constructor was called once at global init time
    REQUIRE(get_ctor_count() == 1);
    REQUIRE(test() == 42);
    // Calling again doesn't re-construct
    REQUIRE(test() == 42);
    REQUIRE(get_ctor_count() == 1);
}

// =============================================================================
// 8. Static local with designated struct init
// =============================================================================

TEST_CASE("Static local with designated struct initializer",
          "[gen][static-local][designated-init]") {
    auto jit = gen_jit(R"SRC(
        module __sl_desig__;

        struct Point {
            x : int;
            y : int;
        }

        get_x() : int {
            static p : Point { .x = 11, .y = 22 };
            return p.x;
        }

        get_y() : int {
            static p : Point { .x = 33, .y = 44 };
            return p.y;
        }
    )SRC");
    REQUIRE(jit);

    auto get_x = jit->lookup_symbol<int(*)()>("get_x");
    auto get_y = jit->lookup_symbol<int(*)()>("get_y");
    REQUIRE(get_x != nullptr);
    REQUIRE(get_y != nullptr);

    REQUIRE(get_x() == 11);
    REQUIRE(get_y() == 44);
}

// =============================================================================
// 9. Static local inside nested block
// =============================================================================

TEST_CASE("Static local inside nested block persists across calls",
          "[gen][static-local]") {
    auto jit = gen_jit(R"SRC(
        module __sl_nested_block__;

        test() : int {
            {
                static inner : int = 5;
                ++inner;
                return inner;
            }
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);

    REQUIRE(test() == 6);    // 5 + 1
    REQUIRE(test() == 7);    // persisted 6 + 1
    REQUIRE(test() == 8);    // persisted 7 + 1
}

