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
 * Tests for temporary anonymous object construction in expressions.
 *
 * These tests verify that `S(args...)` can be used as a sub-expression:
 *  - As a function argument: `consume(Point(1,2))`
 *  - In a return statement: `return Pair(x, y);`
 *  - Default construction (no args): `consume(Counter())`
 *  - Destructor cleanup of temporaries
 *  - Multiple temporaries in one expression (reverse destruction order)
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// Basic: temporary struct as function argument
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Temporary construction: struct as function argument", "[gen][temporary_ctor]") {
    auto jit = gen_jit(R"SRC(
module __test_tmp_ctor_arg__;

struct Point {
    x : int;
    y : int;
    Point(ax : int, ay : int) : x(ax), y(ay) {}
}

sum_point(p : Point&) : int {
    return p.x + p.y;
}

test() : int {
    return sum_point(Point(3, 4));
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn);
    CHECK(test_fn() == 7);
}

// ─────────────────────────────────────────────────────────────────────────────
// Temporary struct in return statement
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Temporary construction: struct in return statement", "[gen][temporary_ctor]") {
    auto jit = gen_jit(R"SRC(
module __test_tmp_ctor_return__;

struct Pair {
    a : int;
    b : int;
    Pair(x : int, y : int) : a(x), b(y) {}
}

make_pair(x : int, y : int) : Pair {
    return Pair(x, y);
}

test() : int {
    p : Pair = make_pair(10, 20);
    return p.a + p.b;
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn);
    CHECK(test_fn() == 30);
}

// ─────────────────────────────────────────────────────────────────────────────
// Default construction (no args)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Temporary construction: default ctor (no args)", "[gen][temporary_ctor]") {
    auto jit = gen_jit(R"SRC(
module __test_tmp_ctor_default__;

struct Counter {
    val : int;
    Counter() : val(42) {}
}

get_val(c : Counter&) : int {
    return c.val;
}

test() : int {
    return get_val(Counter());
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn);
    CHECK(test_fn() == 42);
}

// ─────────────────────────────────────────────────────────────────────────────
// Destructor cleanup of temporaries
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Temporary construction: destructor called", "[gen][temporary_ctor]") {
    auto jit = gen_jit(R"SRC(
module __test_tmp_ctor_dtor__;

g_dtors : int = 0;

struct Tracked {
    val : int;
    Tracked(v : int) : val(v) {}
    ~Tracked() {
        g_dtors = g_dtors + 1;
    }
}

consume(t : Tracked&) : int {
    return t.val;
}

test() : int {
    g_dtors = 0;
    r : int = consume(Tracked(7));
    // After the full expression, the temporary should have been destroyed.
    return g_dtors;
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn);
    CHECK(test_fn() == 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Multiple temporaries in one expression (two args)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Temporary construction: two temporaries as args", "[gen][temporary_ctor]") {
    auto jit = gen_jit(R"SRC(
module __test_tmp_ctor_two__;

struct Val {
    n : int;
    Val(v : int) : n(v) {}
}

add_vals(a : Val&, b : Val&) : int {
    return a.n + b.n;
}

test() : int {
    return add_vals(Val(10), Val(20));
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn);
    CHECK(test_fn() == 30);
}

// ─────────────────────────────────────────────────────────────────────────────
// Temporary construction must fail for abstract classes
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Temporary construction: abstract class rejected", "[gen][temporary_ctor]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
module __test_tmp_ctor_abstract__;

abstract class Base {
    Base() {}
    abstract value() : int;
}

consume(b : Base&) : int {
    return b.value();
}

test() : int {
    return consume(Base());
}
)SRC"));
}

