// SPDX-License-Identifier: Apache-2.0
// Copyright 2023-2026 Emilien Kia
//
// Comprehensive ternary expression tests covering:
// - All basic types: primitives, enums, unions, aggregates, references, pointers
// - All expression contexts: sub-expressions, assignments, function arguments, returns
// - Edge cases and regressions

#include <catch2/catch_all.hpp>
#include "helpers.hpp"

//
// ============================================================================
// PRIMITIVE TERNARY TESTS (int, bool, double, char)
// ============================================================================
//

TEST_CASE("Ternary: primitive in arithmetic sub-expression", "[gen][ternary][primitives][sub-expression]") {
    auto jit = gen_jit(R"SRC(
module gen_ternary_comprehensive_01;

test() : int {
    // Ternary as operand in arithmetic
    return (10 > 5 ? 100 : 200) + (3 < 7 ? 1 : 2);
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn);
    CHECK(test_fn() == 101);  // 100 + 1
}

TEST_CASE("Ternary: primitive in logical sub-expression", "[gen][ternary][primitives][sub-expression]") {
    auto jit = gen_jit(R"SRC(
module gen_ternary_comprehensive_02;

test() : bool {
    // Ternary as operand in logical expression
    return (5 > 3 ? true : false) && (10 < 20 ? true : false);
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<bool(*)()>("test");
    REQUIRE(test_fn);
    CHECK(test_fn() == true);
}

TEST_CASE("Ternary: primitive in assignment", "[gen][ternary][primitives][assignment]") {
    auto jit = gen_jit(R"SRC(
module gen_ternary_comprehensive_03;

test() : int {
    x : int = 5 > 3 ? 42 : 24;
    y : int = 1 == 1 ? 10 : 20;
    return x + y;
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn);
    CHECK(test_fn() == 52);  // 42 + 10
}

TEST_CASE("Ternary: primitive as function argument", "[gen][ternary][primitives][function-arg]") {
    auto jit = gen_jit(R"SRC(
module gen_ternary_comprehensive_04;

add(a : int, b : int) : int {
    return a + b;
}

test() : int {
    // Ternary directly as function argument
    return add(10 > 5 ? 100 : 50, 20 < 30 ? 5 : 15);
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn);
    CHECK(test_fn() == 105);  // 100 + 5
}

TEST_CASE("Ternary: primitive in return statement", "[gen][ternary][primitives][return]") {
    auto jit = gen_jit(R"SRC(
module gen_ternary_comprehensive_05;

get_value(flag : bool) : int {
    return flag ? 777 : 888;
}

test() : int {
    return get_value(true) + get_value(false);
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn);
    CHECK(test_fn() == 1665);  // 777 + 888
}

TEST_CASE("Ternary: double in arithmetic", "[gen][ternary][primitives][floating-point]") {
    auto jit = gen_jit(R"SRC(
module gen_ternary_comprehensive_06;

test() : double {
    x : double = true ? 3.14 : 2.71;
    y : double = false ? 1.41 : 1.73;
    return x + y;
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<double(*)()>("test");
    REQUIRE(test_fn);
    double result = test_fn();
    CHECK(result > 4.86);  // 3.14 + 1.73 ≈ 4.87
    CHECK(result < 4.88);
}

TEST_CASE("Ternary: char type", "[gen][ternary][primitives][char]") {
    auto jit = gen_jit(R"SRC(
module gen_ternary_comprehensive_07;

test() : int {
    c : char = 5 > 3 ? 'A' : 'B';
    // char is an int in K, so we can return it as int
    return (int) c;
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn);
    CHECK(test_fn() == (int)'A');
}

//
// ============================================================================
// ENUM TERNARY TESTS
// ============================================================================
//

TEST_CASE("Ternary: enum in assignment", "[gen][ternary][enums][assignment]") {
    auto jit = gen_jit(R"SRC(
module gen_ternary_comprehensive_08;

enum Color { Red; Green; Blue; }

get_color(pick : bool) : Color {
    return pick ? Color::Red : Color::Blue;
}

test() : int {
    // Ternary in assignment
    c : Color = 5 > 3 ? Color::Green : Color::Red;
    // Convert to int for testing (enums are zero-indexed)
    return (int) c;
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn);
    CHECK(test_fn() == 1);  // Green is at index 1
}

TEST_CASE("Ternary: enum as function argument", "[gen][ternary][enums][function-arg]") {
    auto jit = gen_jit(R"SRC(
module gen_ternary_comprehensive_09;

enum Status { Idle; Running; Done; }

get_status_value(s : Status) : int {
    return (int) s;
}

test() : int {
    // Ternary directly as function argument
    return get_status_value(true ? Status::Running : Status::Idle);
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn);
    CHECK(test_fn() == 1);  // Running is at index 1
}

TEST_CASE("Ternary: enum in return statement", "[gen][ternary][enums][return]") {
    auto jit = gen_jit(R"SRC(
module gen_ternary_comprehensive_10;

enum Priority { Low; Medium; High; }

choose_priority(urgent : bool) : Priority {
    return urgent ? Priority::High : Priority::Low;
}

test() : int {
    return (int) choose_priority(false);
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn);
    CHECK(test_fn() == 0);  // Low is at index 0
}

//
// ============================================================================
// UNION TERNARY TESTS
// ============================================================================
//

TEST_CASE("Ternary: union in assignment", "[gen][ternary][unions][assignment]") {
    auto jit = gen_jit(R"SRC(
module gen_ternary_comprehensive_11;

union Value {
    i : int;
    d : double;
}

test() : int {
    // Ternary result is converted into union via typed initialization.
    v : Value = true ? 42 : -1;
    // Unions are discriminated, read the int
    return v.i;
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn);
    CHECK(test_fn() == 42);
}

TEST_CASE("Ternary: union as function argument", "[gen][ternary][unions][function-arg]") {
    auto jit = gen_jit(R"SRC(
module gen_ternary_comprehensive_12;

union Number {
    i : int;
    f : double;
}

get_int_value(n : Number) : int {
    return n.i;
}

test() : int {
    // Ternary is used directly as function argument and converted to union type.
    return get_int_value(false ? 2 : 99);
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn);
    CHECK(test_fn() == 99);
}

//
// ============================================================================
// AGGREGATE (STRUCT/CLASS) TERNARY TESTS
//
// ============================================================================
//

TEST_CASE("Ternary: struct in assignment", "[gen][ternary][aggregates][assignment]") {
    auto jit = gen_jit(R"SRC(
module gen_ternary_comprehensive_13;

struct Point {
    x : int;
    y : int;
}

test() : int {
    // This should work but crashes at runtime
    a : Point = 1 == 1 ? Point{.x = 10, .y = 20} : Point{.x = 5, .y = 15};
    return a.x + a.y;
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn);
    CHECK(test_fn() == 30);
}

TEST_CASE("Ternary: struct in return", "[gen][ternary][aggregates][return]") {
    auto jit = gen_jit(R"SRC(
module gen_ternary_comprehensive_14;

struct Pair {
    first : int;
    second : int;
}

make_pair(cond : bool) : Pair {
    return cond ? Pair{.first = 100, .second = 200} : Pair{.first = 1, .second = 2};
}

test() : int {
    p : Pair = make_pair(true);
    return p.first + p.second;
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn);
    // Expected: 100 + 200 = 300
    CHECK(test_fn() == 300);
}

TEST_CASE("Ternary: struct as function argument",
          "[gen][ternary][aggregates][function-arg]") {
    auto jit = gen_jit(R"SRC(
module gen_ternary_comprehensive_15;

struct Coord {
    row : int;
    col : int;
}

sum_coord(c : Coord) : int {
    return c.row + c.col;
}

test() : int {
    return sum_coord(false ? Coord{.row = 1, .col = 2} : Coord{.row = 50, .col = 60});
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn);
    // Expected: 50 + 60 = 110
    CHECK(test_fn() == 110);
}

//
// ============================================================================
// REFERENCE AND POINTER TERNARY TESTS
// ============================================================================
//

TEST_CASE("Ternary: reference in assignment", "[gen][ternary][references][assignment]") {
    auto jit = gen_jit(R"SRC(
module gen_ternary_comprehensive_16;

test() : int {
    x : int = 10;
    y : int = 20;
    // Select reference based on condition
    ref : int& = true ? x : y;
    return ref + 5;
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn);
    CHECK(test_fn() == 15);  // ref points to x=10, so 10+5=15
}

TEST_CASE("Ternary: pointer in assignment", "[gen][ternary][pointers][assignment]") {
    auto jit = gen_jit(R"SRC(
module gen_ternary_comprehensive_17;

test() : int {
    x : int = 42;
    y : int = 99;
    // Select pointer based on condition
    ptr : int* = false ? &x : &y;
    return *ptr;
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn);
    CHECK(test_fn() == 99);  // ptr points to y=99
}

TEST_CASE("Ternary: reference through ternary-selected pointer",
          "[gen][ternary][references][indirect]") {
    auto jit = gen_jit(R"SRC(
module gen_ternary_comprehensive_18;

test() : int {
    x : int = 10;
    y : int = 20;
    ptr : int* = true ? &x : &y;
    ref : int& = *ptr;
    return ref + 5;
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn);
    CHECK(test_fn() == 15);
}

//
// ============================================================================
// NESTED AND COMPLEX TERNARY TESTS
// ============================================================================
//

TEST_CASE("Ternary: nested ternaries (primitive)", "[gen][ternary][primitives][nested]") {
    auto jit = gen_jit(R"SRC(
module gen_ternary_comprehensive_19;

test() : int {
    // Nested ternaries in both branches
    x : int = 10;
    result : int = x > 20 ? (x > 50 ? 1 : 2) : (x > 5 ? 3 : 4);
    return result;
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn);
    CHECK(test_fn() == 3);  // x=10: not >20, so right branch; 10>5 so 3
}

TEST_CASE("Ternary: deeply nested ternaries", "[gen][ternary][primitives][nested]") {
    auto jit = gen_jit(R"SRC(
module gen_ternary_comprehensive_20;

test() : int {
    // Triple-nested ternary
    v : int = 15;
    return v < 10 ? 1 : (v < 20 ? (v < 15 ? 2 : 3) : 4);
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn);
    CHECK(test_fn() == 3);  // v=15: not <10; <20 yes; not <15; so 3
}

TEST_CASE("Ternary: ternary with complex condition", "[gen][ternary][primitives][complex-condition]") {
    auto jit = gen_jit(R"SRC(
module gen_ternary_comprehensive_21;

test() : int {
    a : int = 5;
    b : int = 10;
    c : int = 15;
    // Complex boolean condition
    result : int = (a < b && b < c) || (c == 0) ? 100 : 200;
    return result;
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn);
    CHECK(test_fn() == 100);  // (5<10 && 10<15) is true, so 100
}

TEST_CASE("Ternary: ternary in chain of operations", "[gen][ternary][primitives][chain]") {
    auto jit = gen_jit(R"SRC(
module gen_ternary_comprehensive_22;

test() : int {
    // Ternary result used in multiple operations
    selector : bool = 7 < 10;
    value : int = (selector ? 50 : 25) + 10 * (selector ? 2 : 3);
    return value;
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn);
    // selector = true: (50) + 10*2 = 50 + 20 = 70
    CHECK(test_fn() == 70);
}

//
// ============================================================================
// TYPE ADAPTATION AND COERCION TESTS
// ============================================================================
//

TEST_CASE("Ternary: type adaptation in branches", "[gen][ternary][type-adaptation]") {
    auto jit = gen_jit(R"SRC(
module gen_ternary_comprehensive_23;

test() : long {
    // Branches have different primitive types that need adaptation
    result : long = true ? 42 : 100L;  // int vs long -> result is long
    return result;
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<long(*)()>("test");
    REQUIRE(test_fn);
    CHECK(test_fn() == 42L);
}

TEST_CASE("Ternary: bool result from primitive comparison", "[gen][ternary][type-coercion]") {
    auto jit = gen_jit(R"SRC(
module gen_ternary_comprehensive_24;

test() : bool {
    x : int = 10;
    y : int = 20;
    // Ternary branches are bool values
    return x > y ? true : false;
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<bool(*)()>("test");
    REQUIRE(test_fn);
    CHECK(test_fn() == false);  // 10 > 20 is false
}

//
// ============================================================================
// EDGE CASES
// ============================================================================
//

TEST_CASE("Ternary: zero and negative values", "[gen][ternary][primitives][edge-cases]") {
    auto jit = gen_jit(R"SRC(
module gen_ternary_comprehensive_25;

test() : int {
    zero_or_neg : int = true ? 0 : -42;
    return zero_or_neg + 50;
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn);
    CHECK(test_fn() == 50);  // 0 + 50 = 50
}

TEST_CASE("Ternary: same value in both branches", "[gen][ternary][primitives][edge-cases]") {
    auto jit = gen_jit(R"SRC(
module gen_ternary_comprehensive_26;

test() : int {
    // Both branches return same value (dead code in else)
    result : int = (5 > 3) ? 777 : 777;
    return result;
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn);
    CHECK(test_fn() == 777);
}

TEST_CASE("Ternary: boundary values", "[gen][ternary][primitives][edge-cases]") {
    auto jit = gen_jit(R"SRC(
module gen_ternary_comprehensive_27;

test() : int {
    // Maximum int value
    max_val : int = true ? 2147483647 : -2147483648;
    // Just verify it compiles and runs
    return 1;
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn);
    CHECK(test_fn() == 1);
}
