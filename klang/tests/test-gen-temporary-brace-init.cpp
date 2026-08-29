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
 * Tests for temporary anonymous construction using brace initializers.
 *
 * These tests verify that `S{.x = val, .y = val}` can be used as
 * sub-expressions (temporary struct construction with designated init):
 *  - As a function argument
 *  - In a return statement
 *  - With partial init (remaining fields zero-initialized)
 *  - With inherited members
 *  - Destructor cleanup of temporaries
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// Phase 1: Parser tests for brace postfix expression
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Parse brace postfix — designated init S{.x=1, .y=2}", "[parser][brace-postfix]") {
    test_logger log;
    k::source src{"f(Point{.x = 1, .y = 2});"};
    k::parse::parser parser(log, src);
    auto stmt = parser.parse_expression_statement();
    REQUIRE(stmt);
}

TEST_CASE("Parse brace postfix — qualified type name Mod::S{.x=1}", "[parser][brace-postfix]") {
    test_logger log;
    k::source src{"f(Mod::Point{.x = 1});"};
    k::parse::parser parser(log, src);
    auto stmt = parser.parse_expression_statement();
    REQUIRE(stmt);
}

TEST_CASE("Parse brace postfix — array temporary Point[]{...}", "[parser][brace-postfix]") {
    test_logger log;
    k::source src{"f(Point[]{Point{.x = 1}, Point{.x = 2}});"};
    k::parse::parser parser(log, src);
    auto stmt = parser.parse_expression_statement();
    REQUIRE(stmt);
}

// ─────────────────────────────────────────────────────────────────────────────
// Phase 2: End-to-end (JIT) tests for struct designated init temporaries
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Temporary brace init: struct as function argument", "[gen][temporary_brace_init]") {
    auto jit = gen_jit(R"SRC(
module gen_temporary_brace_init_01;

struct Point {
    x : int;
    y : int;
}

sum_point(p : Point&) : int {
    return p.x + p.y;
}

test() : int {
    return sum_point(Point{.x = 3, .y = 4});
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn);
    CHECK(test_fn() == 7);
}

TEST_CASE("Temporary brace init: struct in return statement", "[gen][temporary_brace_init]") {
    auto jit = gen_jit(R"SRC(
module gen_temporary_brace_init_02;

struct Pair {
    a : int;
    b : int;
}

make_pair(x : int, y : int) : Pair {
    return Pair{.a = x, .b = y};
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

TEST_CASE("Temporary brace init: ternary aggregate result", "[gen][temporary_brace_init][ternary]") {
    auto jit = gen_jit(R"SRC(
module gen_temporary_brace_init_03;

struct Point {
    x : int;
    y : int;
}

test() : int {
    a : Point = 1 == 1 ? Point{.x = 1, .y = 2} : Point{.x = 3, .y = 4};
    b : Point = 1 == 0 ? Point{.x = 1, .y = 2} : Point{.x = 3, .y = 4};
    return a.x + a.y * 10 + b.x * 100 + b.y * 1000;
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn);
    CHECK(test_fn() == 4321);
}

TEST_CASE("Temporary brace init: partial init, remaining defaults to zero", "[gen][temporary_brace_init]") {
    auto jit = gen_jit(R"SRC(
module gen_temporary_brace_init_04;

struct Trio {
    a : int;
    b : int;
    c : int;
}

get_fields(t : Trio&) : int {
    return t.a + t.b * 10 + t.c * 100;
}

test() : int {
    return get_fields(Trio{.b = 42});
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn);
    // a=0, b=42, c=0 → 0 + 420 + 0 = 420
    CHECK(test_fn() == 420);
}

TEST_CASE("Temporary brace init: order independent", "[gen][temporary_brace_init]") {
    auto jit = gen_jit(R"SRC(
module gen_temporary_brace_init_05;

struct Point {
    x : int;
    y : int;
}

get_sum(p : Point&) : int {
    return p.x * 10 + p.y;
}

test() : int {
    return get_sum(Point{.y = 20, .x = 10});
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn);
    CHECK(test_fn() == 120);  // 10*10 + 20 = 120
}

TEST_CASE("Temporary brace init: float members", "[gen][temporary_brace_init]") {
    auto jit = gen_jit(R"SRC(
module gen_temporary_brace_init_06;

struct Vec {
    x : float;
    y : float;
}

sum_vec(v : Vec&) : float {
    return v.x + v.y;
}

test() : float {
    return sum_vec(Vec{.x = 1.5f, .y = 2.5f});
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<float(*)()>("test");
    REQUIRE(test_fn);
    CHECK(test_fn() == Catch::Approx(4.0f));
}

TEST_CASE("Temporary brace init: expression values", "[gen][temporary_brace_init]") {
    auto jit = gen_jit(R"SRC(
module gen_temporary_brace_init_07;

struct Point {
    x : int;
    y : int;
}

sum_point(p : Point&) : int {
    return p.x + p.y;
}

make(a : int, b : int) : int {
    return sum_point(Point{.x = a + 1, .y = b * 2});
}
)SRC");
    REQUIRE(jit);
    auto make = jit->lookup_symbol<int(*)(int, int)>("make");
    REQUIRE(make);
    CHECK(make(5, 3) == 12);  // (5+1) + (3*2) = 6 + 6 = 12
}

TEST_CASE("Temporary brace init: inherited member from base struct", "[gen][temporary_brace_init]") {
    auto jit = gen_jit(R"SRC(
module gen_temporary_brace_init_08;

struct Base {
    x : int;
}

struct Derived : public Base {
    y : int;
}

sum(d : Derived&) : int {
    return d.x + d.y;
}

test() : int {
    return sum(Derived{.x = 10, .y = 20});
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn);
    CHECK(test_fn() == 30);
}

TEST_CASE("Temporary brace init: constructor form (.member(args))", "[gen][temporary_brace_init]") {
    auto jit = gen_jit(R"SRC(
module gen_temporary_brace_init_09;

struct Pair {
    x : int;
    y : int;
}

sum(p : Pair&) : int {
    return p.x + p.y;
}

test() : int {
    return sum(Pair{.x = 10, .y(20)});
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn);
    CHECK(test_fn() == 30);
}

TEST_CASE("Temporary brace init: destructor called", "[gen][temporary_brace_init]") {
    auto jit = gen_jit(R"SRC(
module gen_temporary_brace_init_10;

g_dtors : int = 0;

struct Tracked {
    val : int;
    ~Tracked() {
        ++g_dtors;
    }
}

consume(t : Tracked&) : int {
    return t.val;
}

test() : int {
    g_dtors = 0;
    r : int = consume(Tracked{.val = 7});
    return g_dtors;
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn);
    CHECK(test_fn() == 1);
}

TEST_CASE("Temporary brace init: two temporaries as args", "[gen][temporary_brace_init]") {
    auto jit = gen_jit(R"SRC(
module gen_temporary_brace_init_11;

struct Val {
    n : int;
}

add_vals(a : Val&, b : Val&) : int {
    return a.n + b.n;
}

test() : int {
    return add_vals(Val{.n = 10}, Val{.n = 20});
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn);
    CHECK(test_fn() == 30);
}

TEST_CASE("Temporary brace init: error on non-struct type", "[gen][temporary_brace_init]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
module gen_temporary_brace_init_12;

get() : int {
    x : int = NotAType{.a = 1};
    return x;
}
)SRC"));
}

TEST_CASE("Temporary brace init: nested struct designated init", "[gen][temporary_brace_init]") {
    auto jit = gen_jit(R"SRC(
module gen_temporary_brace_init_13;

struct Inner {
    a : int;
    b : int;
}

struct Outer {
    inner : Inner;
    c : int;
}

get_sum(o : Outer&) : int {
    return o.inner.a + o.inner.b + o.c;
}

test() : int {
    return get_sum(Outer{.inner = { .a = 10, .b = 20 }, .c = 30});
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn);
    CHECK(test_fn() == 60);
}

// ─────────────────────────────────────────────────────────────────────────────
// Phase 3: Array brace init in variable declarations (verification tests)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Array brace init verification: local int array as func arg", "[gen][brace-init][verification]") {
    auto jit = gen_jit(R"SRC(
module gen_temporary_brace_init_14;

sum3(arr : int[3]&) : int {
    return arr[0] + arr[1] + arr[2];
}

test() : int {
    a : int[3] {10, 20, 30};
    return sum3(a);
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn);
    CHECK(test_fn() == 60);
}

TEST_CASE("Array brace init verification: inferred size", "[gen][brace-init][verification]") {
    auto jit = gen_jit(R"SRC(
module gen_temporary_brace_init_15;

test() : int {
    arr : int[] {100, 200, 300, 400};
    return arr[0] + arr[1] + arr[2] + arr[3];
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn);
    CHECK(test_fn() == 1000);
}

// ─────────────────────────────────────────────────────────────────────────────
// Phase 4: Temporary array brace init in expression context
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Temporary brace init: array temporary as function argument", "[gen][temporary_brace_init][array]") {
    auto jit = gen_jit(R"SRC(
module gen_temporary_brace_init_16;

struct Point {
    x : int;
}

sum_points(p : Point[2]&) : int {
    return p[0].x + p[1].x;
}

test() : int {
    return sum_points(Point[]{Point{.x = 10}, Point{.x = 20}});
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn);
    CHECK(test_fn() == 30);
}

TEST_CASE("Temporary brace init: array temporary in return expression", "[gen][temporary_brace_init][array]") {
    auto jit = gen_jit(R"SRC(
module gen_temporary_brace_init_17;

struct Point {
    x : int;
}

make_points() : Point[2] {
    return Point[]{Point{.x = 7}, Point{.x = 8}};
}

test() : int {
    make_points();
    return 78;
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn);
    CHECK(test_fn() == 78);
}

TEST_CASE("Temporary brace init: array temporary partial init keeps default values", "[gen][temporary_brace_init][array]") {
    auto jit = gen_jit(R"SRC(
module gen_temporary_brace_init_18;

struct Point {
    x : int;
}

sum3(p : Point[3]&) : int {
    return p[0].x + p[1].x + p[2].x;
}

test() : int {
    return sum3(Point[]{Point{.x = 11}, , Point{.x = 33}});
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn);
    CHECK(test_fn() == 44);
}

TEST_CASE("Temporary brace init: object array temporary with direct primitive values", "[gen][temporary_brace_init][array]") {
    auto jit = gen_jit(R"SRC(
module gen_temporary_brace_init_19;

struct Box {
    v : int;
    Box(x : int) : v(x) {}
}

sum3(a : Box[3]&) : int {
    return a[0].v + a[1].v + a[2].v;
}

test() : int {
    // Direct primitive values must be adapted through Box(int) constructor.
    return sum3(Box[]{2, 4, 6});
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn);
    CHECK(test_fn() == 12);
}

TEST_CASE("Temporary brace init: object array temporary partial primitive direct values", "[gen][temporary_brace_init][array]") {
    auto jit = gen_jit(R"SRC(
module gen_temporary_brace_init_20;

struct Box {
    v : int;
    Box(x : int) : v(x) {}
}

sum3(a : Box[3]&) : int {
    return a[0].v + a[1].v + a[2].v;
}

test() : int {
    return sum3(Box[]{5, , 7});
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn);
    CHECK(test_fn() == 12); // middle slot default-initialized to Box(0)
}

TEST_CASE("Temporary brace init: array temporary with constructor-style elements", "[gen][temporary_brace_init][array]") {
    auto jit = gen_jit(R"SRC(
module gen_temporary_brace_init_21;

struct Box {
    v : int;
    Box(x : int) : v(x) {}
}

sum2(a : Box[2]&) : int {
    return a[0].v + a[1].v;
}

test() : int {
    return sum2(Box[]{9, 11});
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn);
    CHECK(test_fn() == 20);
}

TEST_CASE("Temporary brace init: array temporary destructors are called", "[gen][temporary_brace_init][array]") {
    auto jit = gen_jit(R"SRC(
module gen_temporary_brace_init_22;

g_dtors : int = 0;

struct Tracked {
    n : int;
    ~Tracked() {
        ++g_dtors;
    }
}

consume(a : Tracked[2]&) : int {
    return a[0].n + a[1].n;
}

test() : int {
    g_dtors = 0;
    r : int = consume(Tracked[]{Tracked{.n = 1}, Tracked{.n = 2}});
    return g_dtors;
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn);
    CHECK(test_fn() == 2);
}

TEST_CASE("Temporary brace init: array temporary rejects unknown type", "[gen][temporary_brace_init][array]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
module gen_temporary_brace_init_23;

test() : int {
    x : int = NotAType[]{1};
    return x;
}
)SRC"));
}

TEST_CASE("Temporary brace init: array temporary rejects designated members", "[gen][temporary_brace_init][array]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
module gen_temporary_brace_init_24;

struct Point {
    x : int;
}

test() : int {
    arr : Point[1] = Point[]{.x = 42};
    return arr[0].x;
}
)SRC"));
}
