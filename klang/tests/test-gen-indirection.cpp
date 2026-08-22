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
 * Tests for the four indirection types:
 *   reference (&) — immutable, non-null
 *   link      (~) — mutable,   non-null
 *   view      (?) — immutable, nullable
 *   pointer   (*) — mutable,   nullable
 */

#include <catch2/catch_test_macros.hpp>
#include "helpers.hpp"

// =============================================================================
// LINK (~)  — mutable, non-null
// =============================================================================

// -----------------------------------------------------------------------------
// Basic link usage: declare, initialise from a variable, read through it.
// -----------------------------------------------------------------------------
TEST_CASE("Link basic read", "[gen][indirection][link]") {
    auto jit = gen_jit(R"SRC(
        module gen_indirection_01;

        test() : int {
            x : int = 42;
            lnk : int+ = &x;
            return *lnk;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

// -----------------------------------------------------------------------------
// Link write: write through the link modifies the original variable.
// -----------------------------------------------------------------------------
TEST_CASE("Link write modifies original", "[gen][indirection][link]") {
    auto jit = gen_jit(R"SRC(
        module gen_indirection_02;

        a : int;

        set_via_link() {
            lnk : int+ = &a;
            *lnk = 99;
        }

        get() : int { return a; }
    )SRC");
    REQUIRE(jit);
    auto set_fn = jit->lookup_symbol<void(*)()>("set_via_link");
    REQUIRE(set_fn != nullptr);
    auto get_fn = jit->lookup_symbol<int(*)()>("get");
    REQUIRE(get_fn != nullptr);
    set_fn();
    REQUIRE(get_fn() == 99);
}

// -----------------------------------------------------------------------------
// Link rebind: a link can be rebound after initialisation.
// -----------------------------------------------------------------------------
TEST_CASE("Link rebind", "[gen][indirection][link]") {
    auto jit = gen_jit(R"SRC(
        module gen_indirection_03;

        test() : int {
            x : int = 1;
            y : int = 2;
            lnk : int+ = &x;
            lnk = &y;          // rebind to y
            return *lnk;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 2);
}

// -----------------------------------------------------------------------------
// Link initialised from a pointer (runtime null-check emitted).
// If the pointer is non-null the result must be correct.
// -----------------------------------------------------------------------------
TEST_CASE("Link init from non-null pointer succeeds", "[gen][indirection][link]") {
    auto jit = gen_jit(R"SRC(
        module gen_indirection_04;

        test() : int {
            x : int = 7;
            p : int* = &x;
            lnk : int+ = p;    // warning: nullable source, null-check inserted
            return *lnk;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 7);
}

// -----------------------------------------------------------------------------
// ERROR: link without initialiser must be rejected.
// -----------------------------------------------------------------------------
TEST_CASE("Link without initialiser is rejected", "[gen][resolution][link]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module gen_indirection_05;
        test() : int {
            lnk : int+;    // ERROR: link must be initialised at declaration
            return 0;
        }
    )SRC"));
}

// -----------------------------------------------------------------------------
// ERROR: link initialised from a non-indirection (bare value) must be rejected.
// -----------------------------------------------------------------------------
TEST_CASE("Link initialised from a literal is rejected", "[gen][resolution][link]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module gen_indirection_06;
        test() : int {
            lnk : int+ = 42;   // ERROR: 42 is not an address
            return 0;
        }
    )SRC"));
}

// =============================================================================
// VIEW (^) — immutable, nullable
// =============================================================================

// -----------------------------------------------------------------------------
// Pinned basic: declare and read through it.
// -----------------------------------------------------------------------------
TEST_CASE("Pinned basic read", "[gen][indirection][view]") {
    auto jit = gen_jit(R"SRC(
        module gen_indirection_07;

        test() : int {
            x : int = 55;
            pin : int? = &x;
            return *pin;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 55);
}

// -----------------------------------------------------------------------------
// Pinned from pointer: if pointer is non-null, dereference works.
// -----------------------------------------------------------------------------
TEST_CASE("Pinned init from non-null pointer succeeds", "[gen][indirection][view]") {
    auto jit = gen_jit(R"SRC(
        module gen_indirection_08;

        test() : int {
            x : int = 13;
            p : int* = &x;
            pin : int? = p;
            return *pin;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 13);
}

// -----------------------------------------------------------------------------
// ERROR: view without initialiser must be rejected.
// -----------------------------------------------------------------------------
TEST_CASE("Pinned without initialiser is rejected", "[gen][resolution][view]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module gen_indirection_09;
        test() : int {
            pin : int?;    // ERROR: view must be initialised at declaration
            return 0;
        }
    )SRC"));
}

// -----------------------------------------------------------------------------
// ERROR: rebinding a view must be rejected.
// -----------------------------------------------------------------------------
TEST_CASE("Pinned rebind is rejected", "[gen][resolution][view]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module gen_indirection_10;
        test() : int {
            x : int = 1;
            y : int = 2;
            pin : int? = &x;
            pin = &y;        // ERROR: view is immutable
            return 0;
        }
    )SRC"));
}

// =============================================================================
// REFERENCE (&) — immutable, non-null  (existing semantics, regression)
// =============================================================================

// -----------------------------------------------------------------------------
// Reference "rebind": r = y is NOT a rebind but assignment through the ref.
// y is a ref<int>, so r = y copies y's value into x (transparent semantics).
// This must succeed and return 2.
// -----------------------------------------------------------------------------
TEST_CASE("Reference assignment through ref modifies underlying object", "[gen][indirection][refs]") {
    auto jit = gen_jit(R"SRC(
        module gen_indirection_11;
        test() : int {
            x : int = 1;
            y : int = 2;
            r : int& = x;
            r = y;           // assignment to x through r (y's value copied into x)
            return r;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 2);
}

// Positive test: assignment through a reference changes the underlying object.
TEST_CASE("Assignment through reference modifies underlying object", "[gen][indirection][refs]") {
    auto jit = gen_jit(R"SRC(
        module gen_indirection_12;

        a : int;

        test() {
            r : int& = a;
            r = 77;    // assigns 77 to a
        }

        get() : int { return a; }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<void(*)()>("test");
    REQUIRE(test_fn != nullptr);
    auto get_fn = jit->lookup_symbol<int(*)()>("get");
    REQUIRE(get_fn != nullptr);
    test_fn();
    REQUIRE(get_fn() == 77);
}

// =============================================================================
// ADDRESS-OF (&expr) returns a link
// =============================================================================

TEST_CASE("Address-of produces a link", "[gen][indirection][address_of]") {
    auto jit = gen_jit(R"SRC(
        module gen_indirection_13;

        test() : int {
            x : int = 21;
            lnk : int+ = &x;    // &x produces int+
            *lnk = 42;
            return x;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

TEST_CASE("Address-of result can be stored in a pointer", "[gen][indirection][address_of]") {
    auto jit = gen_jit(R"SRC(
        module gen_indirection_14;

        test() : int {
            x : int = 10;
            p : int* = &x;   // &x (link) assigned to pointer
            *p = 20;
            return x;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 20);
}

// =============================================================================
// POINTER (*) — mutable, nullable  (existing regression + new interactions)
// =============================================================================

// -----------------------------------------------------------------------------
// Pointer assigned from a link (link is a non-null address, widening to nullable).
// -----------------------------------------------------------------------------
TEST_CASE("Pointer assigned from a link", "[gen][indirection][pointer]") {
    auto jit = gen_jit(R"SRC(
        module gen_indirection_15;

        test() : int {
            x : int = 33;
            lnk : int+ = &x;
            p : int* = &x;
            p = lnk;          // link -> pointer: safe widening
            return *p;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 33);
}

// =============================================================================
// DEREFERENCE with null-check (pointer)
// Runtime: dereferencing a non-null pointer succeeds.
// =============================================================================

TEST_CASE("Pointer dereference with non-null pointer succeeds", "[gen][indirection][pointer]") {
    auto jit = gen_jit(R"SRC(
        module gen_indirection_16;

        test() : int {
            x : int = 5;
            p : int* = &x;
            return *p;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 5);
}

// =============================================================================
// MEMBER-OF-POINTER (->)
// =============================================================================

TEST_CASE("Member-of-pointer (->) on a pointer to struct", "[gen][indirection][arrow]") {
    auto jit = gen_jit(R"SRC(
        module gen_indirection_17;

        struct Point {
            x : int = 3;
            y : int = 4;
        }

        test() : int {
            pt : Point();
            p : Point* = &pt;
            return p->x + p->y;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 7);
}

TEST_CASE("Member-of-pointer (->) on a link to struct", "[gen][indirection][arrow]") {
    auto jit = gen_jit(R"SRC(
        module gen_indirection_18;

        struct Pair {
            a : int = 10;
            b : int = 20;
        }

        test() : int {
            pr : Pair();
            lnk : Pair+ = &pr;
            return lnk->a + lnk->b;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 30);
}

TEST_CASE("Member access through struct reference member", "[gen][indirection][refs][member-access]") {
    auto jit = gen_jit(R"SRC(
        module gen_indirection_19;

        class Counter {
        public:
            ping() : int { return 1; }
        }

        struct Holder {
            r : Counter&;
        }

        probe(h: Holder*) : int {
            return h->r.ping();
        }

        test() : int {
            // Only checks compilation/type-resolution path for h->r.method().
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 0);
}

// =============================================================================
// INTERACTION: link ~ and reference & have transparent object semantics
// =============================================================================

TEST_CASE("Link passed as function parameter behaves like object", "[gen][indirection][link]") {
    auto jit = gen_jit(R"SRC(
        module gen_indirection_20;

        double_it(r : int&) : int {
            return r * 2;
        }

        test() : int {
            x : int = 6;
            lnk : int+ = &x;
            return double_it(*lnk);
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 12);
}
