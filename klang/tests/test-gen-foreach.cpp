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
 * Tests for the ARRAY variant of the `foreach` statement:
 *   for ( [specifiers] name : type = source_expr ) nested_stmt
 *
 * Where source_expr is an array (or reference to an array), sized or unsized,
 * including a primitive-type array temporary literal built directly in the
 * init expression (e.g. `int[]{1, 2, 3}`). The loop variable is local to the
 * foreach statement: constructed then destroyed at every iteration.
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// Basic iteration: copy semantics
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Foreach array — sum elements via copy", "[gen][foreach][array]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : int {
            arr : int[5]{1, 2, 3, 4, 5};
            sum : int = 0;
            for(x : int = arr) {
                sum = sum + x;
            }
            return sum;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 15);
}

TEST_CASE("Foreach array — copy does not mutate source array", "[gen][foreach][array]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : int {
            arr : int[3]{1, 2, 3};
            for(x : int = arr) {
                x = x * 100;
            }
            return arr[0] + arr[1] + arr[2];
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 6); // unchanged: 1 + 2 + 3
}

// ─────────────────────────────────────────────────────────────────────────────
// Reference semantics: mutation in place
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Foreach array — mutate elements via reference", "[gen][foreach][array]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : int {
            arr : int[4]{1, 2, 3, 4};
            for(x : int& = arr) {
                x = x * 10;
            }
            return arr[0] + arr[1] + arr[2] + arr[3];
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 100); // (1+2+3+4)*10
}

TEST_CASE("Foreach array — const array requires const reference", "[gen][foreach][array]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : int {
            const arr : int[3]{5, 6, 7};
            sum : int = 0;
            for(x : const int& = arr) {
                sum = sum + x;
            }
            return sum;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 18);
}

// ─────────────────────────────────────────────────────────────────────────────
// Primitive-type array temporary literal as the source expression
//
// `int[]{...}` (and other primitive-type keywords: bool, byte, char, short,
// long, float, double) is recognised as a temporary array literal directly in
// the foreach init expression, exactly like a struct/class type name already
// was (e.g. `Point[]{...}`). Only single-keyword (non-compound) primitive
// types are supported this way: 'unsigned int' / 'long long' still require an
// identifier-driven form and are unaffected by this feature.
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Foreach array — iterates over a primitive array temporary literal by copy",
          "[gen][foreach][array][temporary]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : int {
            sum : int = 0;
            for(x : int = int[]{1, 2, 30}) {
                sum = sum + x;
            }
            return sum;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 33);
}

TEST_CASE("Foreach array — reference loop variable can bind to a primitive array temporary literal",
          "[gen][foreach][array][temporary]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : int {
            sum : int = 0;
            for(x : int& = int[]{1, 2, 30}) {
                sum = sum + x;
            }
            return sum;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 33);
}

TEST_CASE("Foreach array — owner loop variable is still forbidden with a temporary array literal source",
          "[gen][foreach][array][temporary][errors]") {
    REQUIRE(compile_should_fail(R"SRC(
        module __foreach_temp_owner_forbidden__;

        test() : void {
            for(x : int! = int[]{1, 2, 3}) {
            }
        }
    )SRC", nullptr));
}

// ─────────────────────────────────────────────────────────────────────────────
// Sized vs unsized array parameters
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Known-limitation: foreach over an unsized array reference parameter",
          "[.][foreach][known-limitation]") {
    // LIMITATION: passing a sized array `int[4]` to an `int[]&` (or `+`/`*`/`?`)
    // parameter relies on the sized→unsized array implicit widening conversion,
    // which is broken for all indirection kinds except `!` (owner) — see
    // `validate_reference_variable` / `validate_link_variable` /
    // `validate_pointer_variable` / `validate_view_variable`
    // (`gen/gen_variable_definition.cpp`). The bare `int[]` (no addresser) and
    // `int[]!` (owner) forms compile but crash at runtime when indexed (a
    // pre-existing ABI/codegen bug, unrelated to `foreach`).
    // Tracked in TODO.md; see also the pre-existing (currently unregistered)
    // `klang/tests/test-gen-array-unsized-conv.cpp`.
    SKIP("Sized->unsized array implicit conversion is broken for reference/link/"
         "pointer/view parameters, and unusable at runtime for the bare/owner forms; "
         "foreach itself works correctly once a valid unsized array reference exists.");
}

// ─────────────────────────────────────────────────────────────────────────────
// break / continue
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Foreach array — break stops iteration early", "[gen][foreach][array]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : int {
            arr : int[5]{1, 2, 3, 4, 5};
            sum : int = 0;
            for(x : int = arr) {
                if(x == 4) {
                    break;
                }
                sum = sum + x;
            }
            return sum;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 6); // 1 + 2 + 3
}

TEST_CASE("Foreach array — continue skips one element", "[gen][foreach][array]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : int {
            arr : int[5]{1, 2, 3, 4, 5};
            sum : int = 0;
            for(x : int = arr) {
                if(x == 3) {
                    continue;
                }
                sum = sum + x;
            }
            return sum;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 12); // 1 + 2 + 4 + 5
}

// ─────────────────────────────────────────────────────────────────────────────
// Nullable addresser content
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Foreach array — nullable pointer elements can be null", "[gen][foreach][array]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : int {
            a : int = 1;
            b : int = 2;
            arr : int*[3]{ &a, null, &b };
            count : int = 0;
            for(p : int* = arr) {
                if(p == null) {
                    count = count + 100;
                } else {
                    count = count + *p;
                }
            }
            return count;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 103); // 1 + 100 + 2
}

// ─────────────────────────────────────────────────────────────────────────────
// Regression: classic for-loop must still parse and work (non-regression)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Foreach array — classic for loop is unaffected", "[gen][foreach][array][regression]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : int {
            sum : int = 0;
            for(i : int = 0; i < 5; i++) {
                sum = sum + i;
            }
            return sum;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 10); // 0+1+2+3+4
}

// ─────────────────────────────────────────────────────────────────────────────
// Construction / destruction per iteration
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Foreach array — loop variable constructed/destructed each iteration", "[gen][foreach][array]") {
    auto jit = gen_jit(R"SRC(
        module test;

        struct Counter {
            static ctor_count : int = 0;
            static dtor_count : int = 0;
            val : int;
            Counter(v : int) {
                val = v;
                ctor_count = ctor_count + 1;
            }
            ~Counter() {
                dtor_count = dtor_count + 1;
            }
        }

        run_loop() : void {
            arr : int[3]{1, 2, 3};
            for(x : Counter = arr) {
            }
        }

        get_ctor_count() : int {
            return Counter::ctor_count;
        }

        get_dtor_count() : int {
            return Counter::dtor_count;
        }
    )SRC");
    REQUIRE(jit);
    auto run_loop = jit->lookup_symbol<void(*)()>("run_loop");
    auto get_ctor_count = jit->lookup_symbol<int(*)()>("get_ctor_count");
    auto get_dtor_count = jit->lookup_symbol<int(*)()>("get_dtor_count");
    REQUIRE(run_loop != nullptr);
    REQUIRE(get_ctor_count != nullptr);
    REQUIRE(get_dtor_count != nullptr);
    // Both counters must be 0 before the loop runs (each construct/destruct
    // happens strictly once per iteration when the loop actually executes).
    REQUIRE(get_ctor_count() == 0);
    REQUIRE(get_dtor_count() == 0);
    run_loop();
    REQUIRE(get_ctor_count() == 3);
    REQUIRE(get_dtor_count() == 3);
}

// ─────────────────────────────────────────────────────────────────────────────
// Error cases
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Foreach array — owner loop variable is forbidden", "[gen][foreach][array][errors]") {
    REQUIRE(compile_should_fail(R"SRC(
        module __foreach_owner_forbidden__;
        struct Widget {
            v : int;
            Widget(x : int) { v = x; }
        }
        test() : int {
            arr : Widget[2]{ Widget(1), Widget(2) };
            for(w : Widget! = arr) {
            }
            return 0;
        }
    )SRC", nullptr));
}

TEST_CASE("Foreach array — drain loop variable is forbidden", "[gen][foreach][array][errors]") {
    REQUIRE(compile_should_fail(R"SRC(
        module __foreach_drain_forbidden__;
        struct Widget {
            v : int;
            Widget(x : int) { v = x; }
        }
        test() : int {
            arr : Widget[2]{ Widget(1), Widget(2) };
            for(w : Widget# = arr) {
            }
            return 0;
        }
    )SRC", nullptr));
}

TEST_CASE("Foreach array — non-iterable source is rejected", "[gen][foreach][array][errors]") {
    REQUIRE(compile_should_fail(R"SRC(
        module __foreach_not_iterable__;
        test() : int {
            v : int = 42;
            for(x : int = v) {
            }
            return 0;
        }
    )SRC", nullptr));
}

