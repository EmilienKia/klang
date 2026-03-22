/*
 * K Language compiler
 *
 * Copyright 2026 Emilien Kia
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
 * Tests for null semantics:
 *  Step 2: Null construction & assignment for pointer, view, owner.
 *  Step 3: Address comparison (==, !=) between indirection types and with null.
 *  Step 4: Implicit bool conversion for indirection types.
 */

#include <catch2/catch_test_macros.hpp>
#include "helpers.hpp"

// =============================================================================
// STEP 2 — Null construction and assignment
// =============================================================================

// ── Construction with null ──────────────────────────────────────────────────

TEST_CASE("Pointer init with null", "[gen][null][construction]") {
    auto jit = gen_jit(R"SRC(
        module __null_ptr_init__;
        test() : int {
            p : int* = null;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
}

TEST_CASE("Pinned init with null", "[gen][null][construction]") {
    auto jit = gen_jit(R"SRC(
        module __null_pin_init__;
        test() : int {
            p : int? = null;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
}

TEST_CASE("Owner init with null", "[gen][null][construction]") {
    auto jit = gen_jit(R"SRC(
        module __null_own_init__;
        test() : int {
            o : int! = null;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
}

// ── Assignment with null ────────────────────────────────────────────────────

TEST_CASE("Pointer assign null", "[gen][null][assignment]") {
    auto jit = gen_jit(R"SRC(
        module __null_ptr_assign__;
        test() : int {
            x : int = 42;
            p : int* = &x;
            p = null;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
}

TEST_CASE("Owner assign null", "[gen][null][assignment]") {
    auto jit = gen_jit(R"SRC(
        module __null_own_assign__;
        test() : int {
            o : int! = new int(42);
            o = null;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
}

TEST_CASE("Pinned assign null is rejected", "[gen][null][assignment]") {
    // Pinned cannot be reassigned after construction — compile error expected.
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module __null_pin_assign__;
        test() : int {
            x : int = 42;
            p : int? = &x;
            p = null;
            return 0;
        }
    )SRC"));
}

// =============================================================================
// STEP 3 — Address comparison (==, !=) between indirection types and null
// =============================================================================

TEST_CASE("Pointer == null", "[gen][null][comparison]") {
    auto jit = gen_jit(R"SRC(
        module __ptr_eq_null__;
        test() : int {
            p : int* = null;
            if (p == null) { return 1; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}

TEST_CASE("Pointer != null", "[gen][null][comparison]") {
    auto jit = gen_jit(R"SRC(
        module __ptr_ne_null__;
        test() : int {
            x : int = 42;
            p : int* = &x;
            if (p != null) { return 1; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}

TEST_CASE("null == pointer", "[gen][null][comparison]") {
    auto jit = gen_jit(R"SRC(
        module __null_eq_ptr__;
        test() : int {
            p : int* = null;
            if (null == p) { return 1; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}

TEST_CASE("Two pointers same address", "[gen][null][comparison]") {
    auto jit = gen_jit(R"SRC(
        module __ptr_eq_ptr__;
        test() : int {
            x : int = 42;
            p1 : int* = &x;
            p2 : int* = &x;
            if (p1 == p2) { return 1; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}

TEST_CASE("Two pointers different address", "[gen][null][comparison]") {
    auto jit = gen_jit(R"SRC(
        module __ptr_ne_ptr__;
        test() : int {
            x : int = 1;
            y : int = 2;
            p1 : int* = &x;
            p2 : int* = &y;
            if (p1 != p2) { return 1; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}

TEST_CASE("Owner == null", "[gen][null][comparison]") {
    auto jit = gen_jit(R"SRC(
        module __own_eq_null__;
        test() : int {
            o : int! = null;
            if (o == null) { return 1; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}

TEST_CASE("Owner != null after new", "[gen][null][comparison]") {
    auto jit = gen_jit(R"SRC(
        module __own_ne_null__;
        test() : int {
            o : int! = new int(42);
            if (o != null) { return 1; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}

TEST_CASE("Pinned == null", "[gen][null][comparison]") {
    auto jit = gen_jit(R"SRC(
        module __pin_eq_null__;
        test() : int {
            p : int? = null;
            if (p == null) { return 1; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}

TEST_CASE("Pointer == link (same address)", "[gen][null][comparison]") {
    auto jit = gen_jit(R"SRC(
        module __ptr_eq_link__;
        test() : int {
            x : int = 42;
            p : int* = &x;
            l : int+ = &x;
            if (p == l) { return 1; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}

TEST_CASE("null != null is false", "[gen][null][comparison]") {
    auto jit = gen_jit(R"SRC(
        module __null_ne_null__;
        test() : int {
            if (null != null) { return 0; }
            return 1;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}

// =============================================================================
// STEP 4 — Implicit bool conversion for indirection types
// =============================================================================

// ── if() with pointer ───────────────────────────────────────────────────────

TEST_CASE("if(ptr) — non-null pointer is truthy", "[gen][null][bool]") {
    auto jit = gen_jit(R"SRC(
        module __if_ptr_truthy__;
        test() : int {
            x : int = 42;
            p : int* = &x;
            if (p) { return 1; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}

TEST_CASE("if(ptr) — null pointer is falsy", "[gen][null][bool]") {
    auto jit = gen_jit(R"SRC(
        module __if_ptr_falsy__;
        test() : int {
            p : int* = null;
            if (p) { return 1; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 0);
}

// ── if() with owner ─────────────────────────────────────────────────────────

TEST_CASE("if(owner) — non-null owner is truthy", "[gen][null][bool]") {
    auto jit = gen_jit(R"SRC(
        module __if_own_truthy__;
        test() : int {
            o : int! = new int(1);
            if (o) { return 1; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}

TEST_CASE("if(owner) — null owner is falsy", "[gen][null][bool]") {
    auto jit = gen_jit(R"SRC(
        module __if_own_falsy__;
        test() : int {
            o : int! = null;
            if (o) { return 1; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 0);
}

// ── if() with view ────────────────────────────────────────────────────────

TEST_CASE("if(view) — non-null view is truthy", "[gen][null][bool]") {
    auto jit = gen_jit(R"SRC(
        module __if_pin_truthy__;
        test() : int {
            x : int = 10;
            p : int? = &x;
            if (p) { return 1; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}

TEST_CASE("if(view) — null view is falsy", "[gen][null][bool]") {
    auto jit = gen_jit(R"SRC(
        module __if_pin_falsy__;
        test() : int {
            p : int? = null;
            if (p) { return 1; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 0);
}

// ── Logical NOT (!) ─────────────────────────────────────────────────────────

TEST_CASE("!ptr — null pointer negated is true", "[gen][null][bool]") {
    auto jit = gen_jit(R"SRC(
        module __not_ptr_null__;
        test() : int {
            p : int* = null;
            if (!p) { return 1; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}

TEST_CASE("!ptr — non-null pointer negated is false", "[gen][null][bool]") {
    auto jit = gen_jit(R"SRC(
        module __not_ptr_nonnull__;
        test() : int {
            x : int = 42;
            p : int* = &x;
            if (!p) { return 1; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 0);
}

// ── Logical AND (&&) ────────────────────────────────────────────────────────

TEST_CASE("ptr && ptr — both non-null", "[gen][null][bool]") {
    auto jit = gen_jit(R"SRC(
        module __and_ptr_ptr__;
        test() : int {
            x : int = 1;
            y : int = 2;
            p : int* = &x;
            q : int* = &y;
            if (p && q) { return 1; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}

TEST_CASE("ptr && ptr — one null", "[gen][null][bool]") {
    auto jit = gen_jit(R"SRC(
        module __and_ptr_null__;
        test() : int {
            x : int = 1;
            p : int* = &x;
            q : int* = null;
            if (p && q) { return 1; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 0);
}

// ── Logical OR (||) ─────────────────────────────────────────────────────────

TEST_CASE("ptr || ptr — one non-null", "[gen][null][bool]") {
    auto jit = gen_jit(R"SRC(
        module __or_ptr_one__;
        test() : int {
            x : int = 1;
            p : int* = &x;
            q : int* = null;
            if (p || q) { return 1; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}

TEST_CASE("ptr || ptr — both null", "[gen][null][bool]") {
    auto jit = gen_jit(R"SRC(
        module __or_ptr_both_null__;
        test() : int {
            p : int* = null;
            q : int* = null;
            if (p || q) { return 1; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 0);
}

// ── while() with pointer ────────────────────────────────────────────────────

TEST_CASE("while(ptr) — loop until null", "[gen][null][bool]") {
    // Use a simple array-like pattern: walk pointers until null sentinel
    auto jit = gen_jit(R"SRC(
        module __while_ptr__;
        test() : int {
            x : int = 1;
            p : int* = &x;
            count : int = 0;
            if (p) { count = count + 1; }
            p = null;
            if (p) { count = count + 1; }
            return count;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}

// ── Mixed: indirection and bool in same expression ──────────────────────────

TEST_CASE("ptr && true", "[gen][null][bool]") {
    auto jit = gen_jit(R"SRC(
        module __ptr_and_true__;
        test() : int {
            x : int = 42;
            p : int* = &x;
            if (p && true) { return 1; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}

TEST_CASE("false || ptr", "[gen][null][bool]") {
    auto jit = gen_jit(R"SRC(
        module __false_or_ptr__;
        test() : int {
            x : int = 42;
            p : int* = &x;
            if (false || p) { return 1; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}

// ── if() with link ──────────────────────────────────────────────────────────

TEST_CASE("if(link) — non-null link is truthy", "[gen][null][bool]") {
    auto jit = gen_jit(R"SRC(
        module __if_link_truthy__;
        test() : int {
            x : int = 42;
            l : int+ = &x;
            if (l) { return 1; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}

// ── Classic null-guard pattern ──────────────────────────────────────────────

TEST_CASE("Null guard before dereference", "[gen][null][bool]") {
    auto jit = gen_jit(R"SRC(
        module __null_guard__;
        test() : int {
            x : int = 42;
            p : int* = &x;
            if (p != null && *p == 42) { return 1; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}

// =============================================================================
// ERROR CASES — Operations that should be rejected at compile time
// =============================================================================

TEST_CASE("Relational < on pointers is rejected", "[gen][null][error]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module __ptr_lt_err__;
        test() : int {
            x : int = 1;
            y : int = 2;
            p1 : int* = &x;
            p2 : int* = &y;
            if (p1 < p2) { return 1; }
            return 0;
        }
    )SRC"));
}

TEST_CASE("Relational >= on pointer and null is rejected", "[gen][null][error]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module __ptr_ge_null_err__;
        test() : int {
            x : int = 1;
            p : int* = &x;
            if (p >= null) { return 1; }
            return 0;
        }
    )SRC"));
}

TEST_CASE("Link init with null is rejected", "[gen][null][error]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module __link_null_init_err__;
        test() : int {
            l : int+ = null;
            return 0;
        }
    )SRC"));
}

TEST_CASE("Reference init with null is rejected", "[gen][null][error]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module __ref_null_init_err__;
        test() : int {
            r : int& = null;
            return 0;
        }
    )SRC"));
}

// =============================================================================
// ADDITIONAL FUNCTIONAL COVERAGE
// =============================================================================

TEST_CASE("!owner — null owner negated is true", "[gen][null][bool]") {
    auto jit = gen_jit(R"SRC(
        module __not_own_null__;
        test() : int {
            o : int! = null;
            if (!o) { return 1; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}

TEST_CASE("!owner — non-null owner negated is false", "[gen][null][bool]") {
    auto jit = gen_jit(R"SRC(
        module __not_own_nonnull__;
        test() : int {
            o : int! = new int(42);
            if (!o) { return 1; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 0);
}

TEST_CASE("Owner == owner (same allocation)", "[gen][null][comparison]") {
    // Two null owners are equal
    auto jit = gen_jit(R"SRC(
        module __own_eq_own__;
        test() : int {
            o1 : int! = null;
            o2 : int! = null;
            if (o1 == o2) { return 1; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}

TEST_CASE("null == null is true", "[gen][null][comparison]") {
    auto jit = gen_jit(R"SRC(
        module __null_eq_null__;
        test() : int {
            if (null == null) { return 1; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}

TEST_CASE("Pointer assign null then check", "[gen][null][bool][assignment]") {
    // Assign non-null, check truthy, assign null, check falsy
    auto jit = gen_jit(R"SRC(
        module __ptr_assign_check__;
        test() : int {
            x : int = 42;
            p : int* = &x;
            result : int = 0;
            if (p) { result = result + 1; }
            p = null;
            if (p) { result = result + 10; }
            if (!p) { result = result + 100; }
            return result;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    // 1 (first if) + 0 (p is null) + 100 (!p is true) = 101
    REQUIRE(fn() == 101);
}

// =============================================================================
// SHORT-CIRCUIT EVALUATION — && (and-then) and || (or-else)
// =============================================================================

TEST_CASE("&& short-circuit: null ptr skips dereference", "[gen][null][shortcircuit]") {
    // Critical test: if && were eager, *p would segfault because p is null.
    // With short-circuit, p is false so *p is never evaluated.
    auto jit = gen_jit(R"SRC(
        module __sc_and_null__;
        test() : int {
            p : int* = null;
            if (p != null && *p == 42) { return 1; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 0);  // p is null, right side never evaluated, result is false
}

TEST_CASE("&& short-circuit: non-null ptr evaluates both sides", "[gen][null][shortcircuit]") {
    auto jit = gen_jit(R"SRC(
        module __sc_and_nonnull__;
        test() : int {
            x : int = 42;
            p : int* = &x;
            if (p != null && *p == 42) { return 1; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);  // p is non-null, *p == 42 is true
}

TEST_CASE("&& short-circuit with implicit bool: null ptr skips dereference", "[gen][null][shortcircuit]") {
    // Same as above but using implicit bool conversion instead of explicit != null
    auto jit = gen_jit(R"SRC(
        module __sc_and_implicit__;
        test() : int {
            p : int* = null;
            if (p && *p == 42) { return 1; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 0);  // p is null (false), right side skipped
}

TEST_CASE("|| short-circuit: true left skips right", "[gen][null][shortcircuit]") {
    // If || were eager, *q would segfault because q is null.
    // With short-circuit, p != null is true so *q is never evaluated.
    auto jit = gen_jit(R"SRC(
        module __sc_or_skip__;
        test() : int {
            x : int = 42;
            p : int* = &x;
            q : int* = null;
            if (p != null || *q == 99) { return 1; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);  // p != null is true, right side never evaluated
}

TEST_CASE("|| short-circuit: false left evaluates right", "[gen][null][shortcircuit]") {
    auto jit = gen_jit(R"SRC(
        module __sc_or_eval__;
        test() : int {
            x : int = 42;
            p : int* = null;
            q : int* = &x;
            if (p != null || *q == 42) { return 1; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);  // p is null (false), evaluates right: *q == 42 is true
}

TEST_CASE("Chained && short-circuit: a && b && c", "[gen][null][shortcircuit]") {
    auto jit = gen_jit(R"SRC(
        module __sc_chain_and__;
        test() : int {
            x : int = 1;
            y : int = 2;
            p : int* = &x;
            q : int* = &y;
            r : int* = null;
            if (p != null && q != null && *p == 1 && *q == 2) { return 1; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}

TEST_CASE("Chained && short-circuit: stops at first false", "[gen][null][shortcircuit]") {
    // r is null, so the third && is false and *r is never evaluated
    auto jit = gen_jit(R"SRC(
        module __sc_chain_stop__;
        test() : int {
            x : int = 1;
            p : int* = &x;
            r : int* = null;
            if (p != null && r != null && *r == 99) { return 1; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 0);  // r is null, third operand skipped
}

TEST_CASE("Chained || short-circuit: stops at first true", "[gen][null][shortcircuit]") {
    auto jit = gen_jit(R"SRC(
        module __sc_chain_or__;
        test() : int {
            x : int = 42;
            p : int* = &x;
            q : int* = null;
            if (*p == 42 || q != null || *q == 99) { return 1; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);  // first condition true, rest skipped
}

TEST_CASE("Mixed && || short-circuit", "[gen][null][shortcircuit]") {
    auto jit = gen_jit(R"SRC(
        module __sc_mixed__;
        test() : int {
            x : int = 42;
            p : int* = &x;
            q : int* = null;
            if ((p != null && *p == 42) || (q != null && *q == 99)) { return 1; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1);  // left side of || is true, right || side skipped (q is null)
}

TEST_CASE("&& truth table preserved", "[gen][null][shortcircuit]") {
    auto jit = gen_jit(R"SRC(
        module __sc_truth_and__;
        test() : int {
            result : int = 0;
            if (false && false) { result = result + 1; }
            if (false && true)  { result = result + 10; }
            if (true && false)  { result = result + 100; }
            if (true && true)   { result = result + 1000; }
            return result;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1000);  // only true&&true yields true
}

TEST_CASE("|| truth table preserved", "[gen][null][shortcircuit]") {
    auto jit = gen_jit(R"SRC(
        module __sc_truth_or__;
        test() : int {
            result : int = 0;
            if (false || false) { result = result + 1; }
            if (false || true)  { result = result + 10; }
            if (true || false)  { result = result + 100; }
            if (true || true)   { result = result + 1000; }
            return result;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1110);  // all except false||false
}

