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
        module gen_null_semantics_01;
        test() : int {
            p : int* = null;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
}

TEST_CASE("Pinned init with null", "[gen][null][construction]") {
    auto jit = gen_jit(R"SRC(
        module gen_null_semantics_02;
        test() : int {
            p : int? = null;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
}

TEST_CASE("Owner init with null", "[gen][null][construction]") {
    auto jit = gen_jit(R"SRC(
        module gen_null_semantics_03;
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
        module gen_null_semantics_04;
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
        module gen_null_semantics_05;
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
        module gen_null_semantics_06;
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
        module gen_null_semantics_07;
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
        module gen_null_semantics_08;
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
        module gen_null_semantics_09;
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
        module gen_null_semantics_10;
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
        module gen_null_semantics_11;
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
        module gen_null_semantics_12;
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
        module gen_null_semantics_13;
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
        module gen_null_semantics_14;
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
        module gen_null_semantics_15;
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
        module gen_null_semantics_16;
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
        module gen_null_semantics_17;
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
        module gen_null_semantics_18;
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
        module gen_null_semantics_19;
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
        module gen_null_semantics_20;
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
        module gen_null_semantics_21;
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
        module gen_null_semantics_22;
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
        module gen_null_semantics_23;
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
        module gen_null_semantics_24;
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
        module gen_null_semantics_25;
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
        module gen_null_semantics_26;
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
        module gen_null_semantics_27;
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
        module gen_null_semantics_28;
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
        module gen_null_semantics_29;
        test() : int {
            x : int = 1;
            p : int* = &x;
            count : int = 0;
            if (p) { ++count; }
            p = null;
            if (p) { ++count; }
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
        module gen_null_semantics_30;
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
        module gen_null_semantics_31;
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
        module gen_null_semantics_32;
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
        module gen_null_semantics_33;
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
        module gen_null_semantics_34;
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
        module gen_null_semantics_35;
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
        module gen_null_semantics_36;
        test() : int {
            l : int+ = null;
            return 0;
        }
    )SRC"));
}

TEST_CASE("Reference init with null is rejected", "[gen][null][error]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module gen_null_semantics_37;
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
        module gen_null_semantics_38;
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
        module gen_null_semantics_39;
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
        module gen_null_semantics_40;
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
        module gen_null_semantics_41;
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
        module gen_null_semantics_42;
        test() : int {
            x : int = 42;
            p : int* = &x;
            result : int = 0;
            if (p) { ++result; }
            p = null;
            if (p) { result += 10; }
            if (!p) { result += 100; }
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
        module gen_null_semantics_43;
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
        module gen_null_semantics_44;
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
        module gen_null_semantics_45;
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
        module gen_null_semantics_46;
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
        module gen_null_semantics_47;
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
        module gen_null_semantics_48;
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
        module gen_null_semantics_49;
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
        module gen_null_semantics_50;
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
        module gen_null_semantics_51;
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
        module gen_null_semantics_52;
        test() : int {
            result : int = 0;
            if (false && false) { ++result; }
            if (false && true)  { result += 10; }
            if (true && false)  { result += 100; }
            if (true && true)   { result += 1000; }
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
        module gen_null_semantics_53;
        test() : int {
            result : int = 0;
            if (false || false) { ++result; }
            if (false || true)  { result += 10; }
            if (true || false)  { result += 100; }
            if (true || true)   { result += 1000; }
            return result;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 1110);  // all except false||false
}

