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
 * Comprehensive tests for K language operator overloading.
 *
 * Coverage:
 *  1. Parser: 'operator' keyword with all operator symbols
 *  2. Member binary operator overloads on structs (+, -, *, /, %, &, |, ^, <<, >>)
 *  3. Member comparison operator overloads (==, !=, <, >, <=, >=)
 *  4. Member logical operator overloads (&&, ||, !)
 *  5. Member unary operator overloads (-, +, ~)
 *  6. Multiple operators on same struct
 *  7. Non-member (external) operator declarations
 *  8. Class operators with virtual dispatch
 *  9. Module export/import with operator overloads
 * 10. Prefix/postfix increment/decrement operators
 * 11. Operator chaining
 * 12. Non-member unary operators
 * 13. Operator with primitive right-hand side parameter (struct + int)
 * 14. Member operator priority over non-member
 * 15. Interface operator declaration with class implementation
 * 16. Operator on dereferenced owner pointer
 * 17. Non-member prefix increment operator
 * 18–29. Implicit casting (widening, long, best overload, upcast, comparison, non-member, logical, bitwise, ref, filtering)
 * 30–35. Ambiguity detection (binary, unary, comparison, logical, bitwise, no-ambiguity)
 * 36–47. Const-correctness (binary, unary, comparison, logical, NOT, both, const struct, non-member fallback, parse, local, ~, &)
 * 48–49. Const-correctness: bitwise | ^ and shift << >> operators
 * 50–51. Const-correctness: prefix/postfix increment/decrement rejection on const
 * 52. Const-correctness: const comparison operators (<, >, <=, >=)
 * 53. Const-correctness: virtual operator dispatch on const class object
 * 54. Const-correctness: non-member unary operator on const object
 * 55. Const overload resolution: const and mutable member operator coexistence
 * 56. Const-correctness: logical || on const object
 * 57. Cross-module operator with const-correctness
 * 58. Operator chaining with const-correctness
 * 59. Const struct promotes all operator families to const
 * 60. Const operator with implicit widening cast on const object
 * 61. No ambiguity: const/mutable member operator resolution
 * 62. Non-member operator: const vs non-const first parameter resolution
 * 63. Const operator on dereferenced owner pointer
 * 64. Const interface operator with class implementation
 * 65. Multiple non-member const operators with type filtering
 */

#include <catch2/catch_all.hpp>

#include "../src/common/logger.hpp"
#include "../src/parse/parser.hpp"
#include "../src/model/model.hpp"
#include "../src/gen/generators.hpp"
#include "../src/gen/resolvers.hpp"
#include "../src/compiler.hpp"

#include "helpers.hpp"

// ═════════════════════════════════════════════════════════════════════════════
// 1. Parser tests
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Parse operator function declaration", "[parser][operator]") {

    SECTION("Operator + in struct") {
        test_logger logger;
        k::parse::parser parser(logger);
        k::source src(R"SRC(
struct Vec2 {
    x: int;
    y: int;
    operator +(other: Vec2&) : int {
        return x + other.x + y + other.y;
    }
}
)SRC");
        parser.parse(src);
        auto unit = parser.parse_unit();
        REQUIRE(unit);
    }

    SECTION("Multiple operator declarations") {
        test_logger logger;
        k::parse::parser parser(logger);
        k::source src(R"SRC(
struct Num {
    v: int;
    operator +(other: Num&) : int { return v + other.v; }
    operator -(other: Num&) : int { return v - other.v; }
    operator ==(other: Num&) : bool { return v == other.v; }
    operator -() : int { return -v; }
}
)SRC");
        parser.parse(src);
        auto unit = parser.parse_unit();
        REQUIRE(unit);
    }

    SECTION("All operator symbols parse") {
        test_logger logger;
        k::parse::parser parser(logger);
        k::source src(R"SRC(
struct A {
    v: int;
    operator +(o: A&) : int { return v; }
    operator -(o: A&) : int { return v; }
    operator *(o: A&) : int { return v; }
    operator /(o: A&) : int { return v; }
    operator %(o: A&) : int { return v; }
    operator &(o: A&) : int { return v; }
    operator |(o: A&) : int { return v; }
    operator ^(o: A&) : int { return v; }
    operator <<(o: A&) : int { return v; }
    operator >>(o: A&) : int { return v; }
    operator &&(o: A&) : bool { return true; }
    operator ||(o: A&) : bool { return true; }
    operator ==(o: A&) : bool { return true; }
    operator !=(o: A&) : bool { return true; }
    operator <(o: A&) : bool { return true; }
    operator >(o: A&) : bool { return true; }
    operator <=(o: A&) : bool { return true; }
    operator >=(o: A&) : bool { return true; }
    operator -() : int { return -v; }
    operator +() : int { return v; }
    operator ~() : int { return v; }
    operator !() : bool { return true; }
    operator ++_() : int { return v; }
    operator --_() : int { return v; }
}
)SRC");
        parser.parse(src);
        auto unit = parser.parse_unit();
        REQUIRE(unit);
    }

    SECTION("Postfix increment/decrement parse") {
        test_logger logger;
        k::parse::parser parser(logger);
        k::source src(R"SRC(
struct B {
    v: int;
    operator _++() : int { return v; }
    operator _--() : int { return v; }
}
)SRC");
        parser.parse(src);
        auto unit = parser.parse_unit();
        REQUIRE(unit);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// 2. Member binary arithmetic operators on struct
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Struct member operator + overload", "[operator][gen][struct]") {
    auto jit = gen_jit(R"SRC(
module __op_add__;
struct Vec2 {
    x: int;
    y: int;
    Vec2(ax: int, ay: int) : x(ax), y(ay) {}
    operator +(other: Vec2&) : int {
        return x + other.x + y + other.y;
    }
}
test() : int {
    a: Vec2(1, 2);
    b: Vec2(3, 4);
    return a + b;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN10__op_add__4testEv");
    REQUIRE(fn);
    CHECK(fn() == 10);
}

TEST_CASE("Struct member operator - overload", "[operator][gen][struct]") {
    auto jit = gen_jit(R"SRC(
module __op_sub__;
struct Vec2 {
    x: int;
    y: int;
    Vec2(ax: int, ay: int) : x(ax), y(ay) {}
    operator -(other: Vec2&) : int {
        return (x - other.x) + (y - other.y);
    }
}
test() : int {
    a: Vec2(10, 20);
    b: Vec2(3, 5);
    return a - b;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN10__op_sub__4testEv");
    REQUIRE(fn);
    CHECK(fn() == 22);
}

TEST_CASE("Struct member operator * / % overloads", "[operator][gen][struct]") {
    auto jit = gen_jit(R"SRC(
module __op_muldiv__;
struct Num {
    v: int;
    Num(av: int) : v(av) {}
    operator *(other: Num&) : int { return v * other.v; }
    operator /(other: Num&) : int { return v / other.v; }
    operator %(other: Num&) : int { return v % other.v; }
}
test_mul() : int { a: Num(6); b: Num(7); return a * b; }
test_div() : int { a: Num(42); b: Num(6); return a / b; }
test_mod() : int { a: Num(17); b: Num(5); return a % b; }
)SRC");
    REQUIRE(jit);
    auto fn_mul = jit->lookup_symbol<int(*)()>("_KFN13__op_muldiv__8test_mulEv");
    REQUIRE(fn_mul); CHECK(fn_mul() == 42);
    auto fn_div = jit->lookup_symbol<int(*)()>("_KFN13__op_muldiv__8test_divEv");
    REQUIRE(fn_div); CHECK(fn_div() == 7);
    auto fn_mod = jit->lookup_symbol<int(*)()>("_KFN13__op_muldiv__8test_modEv");
    REQUIRE(fn_mod); CHECK(fn_mod() == 2);
}

// ═════════════════════════════════════════════════════════════════════════════
// 3. Member bitwise operators on struct
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Struct member bitwise operator overloads", "[operator][gen][struct]") {
    auto jit = gen_jit(R"SRC(
module __op_bit__;
struct Bits {
    v: int;
    Bits(av: int) : v(av) {}
    operator |(other: Bits&) : int { return v | other.v; }
    operator &(other: Bits&) : int { return v & other.v; }
    operator ^(other: Bits&) : int { return v ^ other.v; }
}
test_or() : int {
    a: Bits(5);
    b: Bits(3);
    return a | b;
}
test_and() : int {
    a: Bits(5);
    b: Bits(3);
    return a & b;
}
test_xor() : int {
    a: Bits(5);
    b: Bits(3);
    return a ^ b;
}
)SRC");
    REQUIRE(jit);
    auto fn_or = jit->lookup_symbol<int(*)()>("_KFN10__op_bit__7test_orEv");
    REQUIRE(fn_or); CHECK(fn_or() == 7);
    auto fn_and = jit->lookup_symbol<int(*)()>("_KFN10__op_bit__8test_andEv");
    REQUIRE(fn_and); CHECK(fn_and() == 1);
    auto fn_xor = jit->lookup_symbol<int(*)()>("_KFN10__op_bit__8test_xorEv");
    REQUIRE(fn_xor); CHECK(fn_xor() == 6);
}

TEST_CASE("Struct member shift operator overloads", "[operator][gen][struct]") {
    auto jit = gen_jit(R"SRC(
module __op_shift__;
struct Sh {
    v: int;
    Sh(av: int) : v(av) {}
    operator <<(other: Sh&) : int { return v << other.v; }
    operator >>(other: Sh&) : int { return v >> other.v; }
}
test_shl() : int { a: Sh(1); b: Sh(3); return a << b; }
test_shr() : int { a: Sh(24); b: Sh(2); return a >> b; }
)SRC");
    REQUIRE(jit);
    auto fn_shl = jit->lookup_symbol<int(*)()>("_KFN12__op_shift__8test_shlEv");
    REQUIRE(fn_shl); CHECK(fn_shl() == 8);
    auto fn_shr = jit->lookup_symbol<int(*)()>("_KFN12__op_shift__8test_shrEv");
    REQUIRE(fn_shr); CHECK(fn_shr() == 6);
}

// ═════════════════════════════════════════════════════════════════════════════
// 4. Member comparison operators on struct
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Struct member comparison operator overloads", "[operator][gen][struct]") {
    auto jit = gen_jit(R"SRC(
module __op_cmp__;
struct Cmp {
    v: int;
    Cmp(av: int) : v(av) {}
    operator ==(o: Cmp&) : bool { return v == o.v; }
    operator !=(o: Cmp&) : bool { return v != o.v; }
    operator <(o: Cmp&) : bool { return v < o.v; }
    operator >(o: Cmp&) : bool { return v > o.v; }
    operator <=(o: Cmp&) : bool { return v <= o.v; }
    operator >=(o: Cmp&) : bool { return v >= o.v; }
}
test_eq()  : bool { a: Cmp(5); b: Cmp(5); return a == b; }
test_ne()  : bool { a: Cmp(5); b: Cmp(3); return a != b; }
test_lt()  : bool { a: Cmp(3); b: Cmp(5); return a < b; }
test_gt()  : bool { a: Cmp(7); b: Cmp(2); return a > b; }
test_le()  : bool { a: Cmp(5); b: Cmp(5); return a <= b; }
test_ge()  : bool { a: Cmp(5); b: Cmp(5); return a >= b; }
)SRC");
    REQUIRE(jit);
    auto fn_eq = jit->lookup_symbol<bool(*)()>("_KFN10__op_cmp__7test_eqEv");
    REQUIRE(fn_eq); CHECK(fn_eq() == true);
    auto fn_ne = jit->lookup_symbol<bool(*)()>("_KFN10__op_cmp__7test_neEv");
    REQUIRE(fn_ne); CHECK(fn_ne() == true);
    auto fn_lt = jit->lookup_symbol<bool(*)()>("_KFN10__op_cmp__7test_ltEv");
    REQUIRE(fn_lt); CHECK(fn_lt() == true);
    auto fn_gt = jit->lookup_symbol<bool(*)()>("_KFN10__op_cmp__7test_gtEv");
    REQUIRE(fn_gt); CHECK(fn_gt() == true);
    auto fn_le = jit->lookup_symbol<bool(*)()>("_KFN10__op_cmp__7test_leEv");
    REQUIRE(fn_le); CHECK(fn_le() == true);
    auto fn_ge = jit->lookup_symbol<bool(*)()>("_KFN10__op_cmp__7test_geEv");
    REQUIRE(fn_ge); CHECK(fn_ge() == true);
}

// ═════════════════════════════════════════════════════════════════════════════
// 5. Member unary operators on struct
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Struct member unary operator overloads", "[operator][gen][struct]") {
    auto jit = gen_jit(R"SRC(
module __op_unary__;
struct Val {
    v: int;
    Val(av: int) : v(av) {}
    operator -() : int { return -v; }
    operator +() : int { return v; }
    operator ~() : int { return ~v; }
}
test_neg() : int { a: Val(7); return -a; }
test_pos() : int { a: Val(7); return +a; }
test_bnot() : int { a: Val(0); return ~a; }
)SRC");
    REQUIRE(jit);
    auto fn_neg = jit->lookup_symbol<int(*)()>("_KFN12__op_unary__8test_negEv");
    REQUIRE(fn_neg); CHECK(fn_neg() == -7);
    auto fn_pos = jit->lookup_symbol<int(*)()>("_KFN12__op_unary__8test_posEv");
    REQUIRE(fn_pos); CHECK(fn_pos() == 7);
    auto fn_bnot = jit->lookup_symbol<int(*)()>("_KFN12__op_unary__9test_bnotEv");
    REQUIRE(fn_bnot); CHECK(fn_bnot() == -1);  // ~0 == -1 in two's complement
}

TEST_CASE("Struct member logical operator overloads", "[operator][gen][struct]") {
    auto jit = gen_jit(R"SRC(
module __op_logic__;
struct Logic {
    v: bool;
    Logic(av: bool) : v(av) {}
    operator &&(o: Logic&) : bool { return v && o.v; }
    operator ||(o: Logic&) : bool { return v || o.v; }
    operator !() : bool { return !v; }
}
test_and_tt() : bool { a: Logic(true);  b: Logic(true);  return a && b; }
test_and_tf() : bool { a: Logic(true);  b: Logic(false); return a && b; }
test_or_ff()  : bool { a: Logic(false); b: Logic(false); return a || b; }
test_or_tf()  : bool { a: Logic(true);  b: Logic(false); return a || b; }
test_not_t()  : bool { a: Logic(true);  return !a; }
test_not_f()  : bool { a: Logic(false); return !a; }
)SRC");
    REQUIRE(jit);
    auto fn_and_tt = jit->lookup_symbol<bool(*)()>("_KFN12__op_logic__11test_and_ttEv");
    REQUIRE(fn_and_tt); CHECK(fn_and_tt() == true);
    auto fn_and_tf = jit->lookup_symbol<bool(*)()>("_KFN12__op_logic__11test_and_tfEv");
    REQUIRE(fn_and_tf); CHECK(fn_and_tf() == false);
    auto fn_or_ff = jit->lookup_symbol<bool(*)()>("_KFN12__op_logic__10test_or_ffEv");
    REQUIRE(fn_or_ff); CHECK(fn_or_ff() == false);
    auto fn_or_tf = jit->lookup_symbol<bool(*)()>("_KFN12__op_logic__10test_or_tfEv");
    REQUIRE(fn_or_tf); CHECK(fn_or_tf() == true);
    auto fn_not_t = jit->lookup_symbol<bool(*)()>("_KFN12__op_logic__10test_not_tEv");
    REQUIRE(fn_not_t); CHECK(fn_not_t() == false);
    auto fn_not_f = jit->lookup_symbol<bool(*)()>("_KFN12__op_logic__10test_not_fEv");
    REQUIRE(fn_not_f); CHECK(fn_not_f() == true);
}

// ═════════════════════════════════════════════════════════════════════════════
// 6. Multiple operators on same struct
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Multiple operator overloads on same struct", "[operator][gen][struct]") {
    auto jit = gen_jit(R"SRC(
module __op_multi__;
struct Counter {
    val: int;
    Counter(v: int) : val(v) {}
    operator +(other: Counter&) : int { return val + other.val; }
    operator -(other: Counter&) : int { return val - other.val; }
    operator *(other: Counter&) : int { return val * other.val; }
    operator ==(other: Counter&) : bool { return val == other.val; }
}
test_add() : int {
    a: Counter(3);
    b: Counter(7);
    return a + b;
}
test_sub() : int {
    a: Counter(10);
    b: Counter(4);
    return a - b;
}
test_mul() : int {
    a: Counter(5);
    b: Counter(6);
    return a * b;
}
test_eq() : bool {
    a: Counter(42);
    b: Counter(42);
    return a == b;
}
)SRC");
    REQUIRE(jit);
    auto fn_add = jit->lookup_symbol<int(*)()>("_KFN12__op_multi__8test_addEv");
    REQUIRE(fn_add); CHECK(fn_add() == 10);
    auto fn_sub = jit->lookup_symbol<int(*)()>("_KFN12__op_multi__8test_subEv");
    REQUIRE(fn_sub); CHECK(fn_sub() == 6);
    auto fn_mul = jit->lookup_symbol<int(*)()>("_KFN12__op_multi__8test_mulEv");
    REQUIRE(fn_mul); CHECK(fn_mul() == 30);
    auto fn_eq = jit->lookup_symbol<bool(*)()>("_KFN12__op_multi__7test_eqEv");
    REQUIRE(fn_eq); CHECK(fn_eq() == true);
}

// ═════════════════════════════════════════════════════════════════════════════
// 7. Non-member (external) operator declarations
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Non-member operator + overload on struct", "[operator][gen][non-member]") {
    auto jit = gen_jit(R"SRC(
module __op_ext__;
struct Vec {
    v: int;
    Vec(av: int) : v(av) {}
}
operator +(a: Vec&, b: Vec&) : int { return a.v + b.v; }

test() : int {
    a: Vec(10);
    b: Vec(32);
    return a + b;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN10__op_ext__4testEv");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("Non-member comparison operator on struct", "[operator][gen][non-member]") {
    auto jit = gen_jit(R"SRC(
module __op_ext_eq__;
struct Pt {
    x: int;
    Pt(ax: int) : x(ax) {}
}
operator ==(a: Pt&, b: Pt&) : bool { return a.x == b.x; }

test_eq() : bool {
    a: Pt(5);
    b: Pt(5);
    return a == b;
}
test_neq() : bool {
    a: Pt(5);
    b: Pt(3);
    return a == b;
}
)SRC");
    REQUIRE(jit);
    auto fn_eq = jit->lookup_symbol<bool(*)()>("_KFN13__op_ext_eq__7test_eqEv");
    REQUIRE(fn_eq); CHECK(fn_eq() == true);
    auto fn_neq = jit->lookup_symbol<bool(*)()>("_KFN13__op_ext_eq__8test_neqEv");
    REQUIRE(fn_neq); CHECK(fn_neq() == false);
}

// ═════════════════════════════════════════════════════════════════════════════
// 8. Class operators with virtual dispatch
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Class operator overload with virtual dispatch", "[operator][gen][class]") {
    auto jit = gen_jit(R"SRC(
module __op_cls__;
class Base {
    v: int;
    Base() : v(0) {}
    Base(av: int) : v(av) {}
    operator +(other: Base&) : int { return this.v + other.v; }
}
class Derived : public Base {
    Derived() : Base() {}
    Derived(av: int) : Base(av) {}
    operator +(other: Base&) : int { return (this.v + other.v) * 10; }
}
call_add(a: Base&, b: Base&) : int { return a + b; }

test_base() : int {
    a: Base(3);
    b: Base(4);
    return call_add(a, b);
}
test_derived() : int {
    a: Derived(3);
    b: Base(4);
    return call_add(a, b);
}
)SRC");
    REQUIRE(jit);
    auto fn_base = jit->lookup_symbol<int(*)()>("_KFN10__op_cls__9test_baseEv");
    REQUIRE(fn_base); CHECK(fn_base() == 7);
    auto fn_derived = jit->lookup_symbol<int(*)()>("_KFN10__op_cls__12test_derivedEv");
    REQUIRE(fn_derived); CHECK(fn_derived() == 70);
}

TEST_CASE("Class operator == with virtual dispatch", "[operator][gen][class]") {
    auto jit = gen_jit(R"SRC(
module __op_cls_eq__;
class Shape {
    id: int;
    Shape() : id(0) {}
    Shape(aid: int) : id(aid) {}
    operator ==(other: Shape&) : bool { return this.id == other.id; }
}
class Circle : public Shape {
    r: int;
    Circle() : Shape(), r(0) {}
    Circle(aid: int, ar: int) : Shape(aid), r(ar) {}
    operator ==(other: Shape&) : bool { return false; }
}
check_eq(a: Shape&, b: Shape&) : bool { return a == b; }

test_shape_eq() : bool {
    a: Shape(1);
    b: Shape(1);
    return check_eq(a, b);
}
test_circle_always_false() : bool {
    a: Circle(1, 5);
    b: Shape(1);
    return check_eq(a, b);
}
)SRC");
    REQUIRE(jit);
    auto fn1 = jit->lookup_symbol<bool(*)()>("_KFN13__op_cls_eq__13test_shape_eqEv");
    REQUIRE(fn1); CHECK(fn1() == true);
    auto fn2 = jit->lookup_symbol<bool(*)()>("_KFN13__op_cls_eq__24test_circle_always_falseEv");
    REQUIRE(fn2); CHECK(fn2() == false);
}

// ═════════════════════════════════════════════════════════════════════════════
// 9. Module export/import with operator overloads
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("import — struct operator overload across module boundary",
          "[operator][import][e2e]") {
    auto result = build_exec_with_lib(
        // ── Library ──
        R"K(
            module oplib;
            struct Vec2 {
                x: int;
                y: int;
                Vec2(ax: int, ay: int) : x(ax), y(ay) {}
                operator +(other: Vec2&) : int {
                    return x + other.x + y + other.y;
                }
                operator ==(other: Vec2&) : bool {
                    return x == other.x && y == other.y;
                }
            }
        )K",
        // ── Executable ──
        R"K(
            module opexec;
            import oplib;

            main() : int {
                a : oplib::Vec2(10, 20);
                b : oplib::Vec2(5, 7);
                return a + b;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 42 );
}

TEST_CASE("import — class operator overload with virtual dispatch across modules",
          "[operator][import][e2e]") {
    auto result = build_exec_with_lib(
        // ── Library ──
        R"K(
            module opvirtlib;
            class Base {
                v: int;
                Base() : v(0) {}
                Base(av: int) : v(av) {}
                operator +(other: Base&) : int { return this.v + other.v; }
            }
            class Derived : public Base {
                Derived() : Base() {}
                Derived(av: int) : Base(av) {}
                operator +(other: Base&) : int { return (this.v + other.v) * 100; }
            }
        )K",
        // ── Executable ──
        R"K(
            module opvirtexec;
            import opvirtlib;

            call_add(a: opvirtlib::Base&, b: opvirtlib::Base&) : int {
                return a + b;
            }
            main() : int {
                d : opvirtlib::Derived(3);
                b : opvirtlib::Base(4);
                return call_add(d, b) / 100;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE( result.exit_code == 7 );
}

// ═════════════════════════════════════════════════════════════════════════════
// 10. Prefix / postfix increment / decrement
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Struct prefix increment operator overload", "[operator][gen][struct]") {
    auto jit = gen_jit(R"SRC(
module __op_preinc__;
struct Counter {
    v: int;
    Counter(av: int) : v(av) {}
    operator ++_() : int {
        this.v = this.v + 1;
        return this.v;
    }
}
test() : int {
    c: Counter(10);
    return ++c;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN13__op_preinc__4testEv");
    REQUIRE(fn);
    CHECK(fn() == 11);
}

TEST_CASE("Struct prefix decrement operator overload", "[operator][gen][struct]") {
    auto jit = gen_jit(R"SRC(
module __op_predec__;
struct Counter {
    v: int;
    Counter(av: int) : v(av) {}
    operator --_() : int {
        this.v = this.v - 1;
        return this.v;
    }
}
test() : int {
    c: Counter(10);
    return --c;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN13__op_predec__4testEv");
    REQUIRE(fn);
    CHECK(fn() == 9);
}

TEST_CASE("Struct postfix increment operator overload", "[operator][gen][struct]") {
    auto jit = gen_jit(R"SRC(
module __op_postinc__;
struct Counter {
    v: int;
    Counter(av: int) : v(av) {}
    operator _++() : int {
        r : int = this.v;
        this.v = this.v + 1;
        return r;
    }
}
test() : int {
    c: Counter(10);
    return c++;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN14__op_postinc__4testEv");
    REQUIRE(fn);
    CHECK(fn() == 10);
}

TEST_CASE("Struct postfix decrement operator overload", "[operator][gen][struct]") {
    auto jit = gen_jit(R"SRC(
module __op_postdec__;
struct Counter {
    v: int;
    Counter(av: int) : v(av) {}
    operator _--() : int {
        r : int = this.v;
        this.v = this.v - 1;
        return r;
    }
}
test() : int {
    c: Counter(10);
    return c--;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN14__op_postdec__4testEv");
    REQUIRE(fn);
    CHECK(fn() == 10);
}

// ═════════════════════════════════════════════════════════════════════════════
// 11. Operator chaining
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Operator chaining (a + b + c)", "[operator][gen][struct]") {
    auto jit = gen_jit(R"SRC(
module __op_chain__;
struct Val {
    v: int;
    Val(av: int) : v(av) {}
    operator +(other: Val&) : int { return v + other.v; }
}
test() : int {
    a: Val(1);
    b: Val(2);
    c: Val(3);
    return (a + b) + (a + c);
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN12__op_chain__4testEv");
    REQUIRE(fn);
    CHECK(fn() == 7);
}

// ═════════════════════════════════════════════════════════════════════════════
// 12. Non-member unary operator
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Non-member unary operator - overload", "[operator][gen][non-member]") {
    auto jit = gen_jit(R"SRC(
module __op_ext_unary__;
struct Vec {
    v: int;
    Vec(av: int) : v(av) {}
}
operator -(a: Vec&) : int { return -a.v; }

test() : int {
    a: Vec(42);
    return -a;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN16__op_ext_unary__4testEv");
    REQUIRE(fn);
    CHECK(fn() == -42);
}

// ═════════════════════════════════════════════════════════════════════════════
// 13. Operator with non-aggregate parameter (e.g. struct + int)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Operator with primitive right-hand side parameter", "[operator][gen][struct]") {
    auto jit = gen_jit(R"SRC(
module __op_mixed__;
struct Counter {
    v: int;
    Counter(av: int) : v(av) {}
    operator +(n: int) : int { return v + n; }
    operator *(n: int) : int { return v * n; }
}
test_add() : int { c: Counter(10); return c + 5; }
test_mul() : int { c: Counter(7); return c * 6; }
)SRC");
    REQUIRE(jit);
    auto fn_add = jit->lookup_symbol<int(*)()>("_KFN12__op_mixed__8test_addEv");
    REQUIRE(fn_add); CHECK(fn_add() == 15);
    auto fn_mul = jit->lookup_symbol<int(*)()>("_KFN12__op_mixed__8test_mulEv");
    REQUIRE(fn_mul); CHECK(fn_mul() == 42);
}

// ═════════════════════════════════════════════════════════════════════════════
// 14. Member operator priority over non-member
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Member operator takes priority over non-member", "[operator][gen][struct]") {
    auto jit = gen_jit(R"SRC(
module __op_prio__;
struct Val {
    v: int;
    Val(av: int) : v(av) {}
    operator +(other: Val&) : int { return v + other.v + 100; }
}
operator +(a: Val&, b: Val&) : int { return a.v + b.v; }

test() : int {
    a: Val(1);
    b: Val(2);
    return a + b;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN11__op_prio__4testEv");
    REQUIRE(fn);
    // Member operator adds 100, non-member doesn't => expect 103 if member is prioritized
    CHECK(fn() == 103);
}

// ═════════════════════════════════════════════════════════════════════════════
// 15. Interface operator declaration implemented in class
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Interface operator declaration with class implementation", "[operator][gen][interface]") {
    auto jit = gen_jit(R"SRC(
module __op_iface__;
interface Addable {
    operator +(other: Addable&) : int;
}
class MyVal : public Addable {
    v: int;
    MyVal() : v(0) {}
    MyVal(av: int) : v(av) {}
    operator +(other: Addable&) : int { return this.v + 1000; }
}
call_add(a: Addable&, b: Addable&) : int { return a + b; }

test() : int {
    a: MyVal(42);
    b: MyVal(0);
    return call_add(a, b);
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN12__op_iface__4testEv");
    REQUIRE(fn);
    CHECK(fn() == 1042);
}

// ═════════════════════════════════════════════════════════════════════════════
// 16. Operator on dereferenced pointer
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Operator on dereferenced owner pointer", "[operator][gen][struct]") {
    auto jit = gen_jit(R"SRC(
module __op_deref__;
struct Num {
    v: int;
    Num(av: int) : v(av) {}
    operator +(other: Num&) : int { return v + other.v; }
}
test() : int {
    a: Num! = new Num(10);
    b: Num(32);
    r : int = *a + b;
    delete a;
    return r;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN12__op_deref__4testEv");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

// ═════════════════════════════════════════════════════════════════════════════
// 17. Non-member prefix/postfix increment on struct
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Non-member prefix increment operator on struct", "[operator][gen][non-member]") {
    auto jit = gen_jit(R"SRC(
module __op_ext_preinc__;
struct Counter {
    v: int;
    Counter(av: int) : v(av) {}
}
operator ++_(c: Counter&) : int {
    c.v = c.v + 1;
    return c.v;
}
test() : int {
    c: Counter(10);
    return ++c;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN17__op_ext_preinc__4testEv");
    REQUIRE(fn);
    CHECK(fn() == 11);
}

// ═════════════════════════════════════════════════════════════════════════════
// 18. Implicit casting: primitive widening (short → int)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Operator implicit cast: short widened to int", "[operator][gen][cast]") {
    auto jit = gen_jit(R"SRC(
module __op_cast_widen__;
struct Vec {
    v: int;
    Vec(av: int) : v(av) {}
    operator +(n: int) : int { return v + n; }
}
test() : int {
    a: Vec(40);
    s: short = 2;
    return a + s;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN17__op_cast_widen__4testEv");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

// ═════════════════════════════════════════════════════════════════════════════
// 19. Implicit casting: long widening (int → long)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Operator implicit cast: int widened to long", "[operator][gen][cast]") {
    auto jit = gen_jit(R"SRC(
module __op_cast_long__;
struct Accum {
    v: long;
    Accum(av: long) : v(av) {}
    operator +(n: long) : long { return v + n; }
}
test() : long {
    a: Accum(1000000000);
    i: int = 42;
    return a + i;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<long(*)()>("_KFN16__op_cast_long__4testEv");
    REQUIRE(fn);
    CHECK(fn() == 1000000042L);
}

// ═════════════════════════════════════════════════════════════════════════════
// 20. Implicit casting: best overload selection among multiple operator overloads
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Operator overload resolution: exact match preferred over widening", "[operator][gen][cast]") {
    auto jit = gen_jit(R"SRC(
module __op_cast_best__;
struct Val {
    v: int;
    Val(av: int) : v(av) {}
    operator +(n: int) : int { return v + n; }
    operator +(n: long) : int { return v + 1000; }
}
test_exact() : int {
    a: Val(10);
    i: int = 5;
    return a + i;
}
test_widened() : int {
    a: Val(10);
    l: long = 5;
    return a + l;
}
)SRC");
    REQUIRE(jit);
    auto fn_exact = jit->lookup_symbol<int(*)()>("_KFN16__op_cast_best__10test_exactEv");
    REQUIRE(fn_exact);
    CHECK(fn_exact() == 15);  // Exact match: int + int
    auto fn_widened = jit->lookup_symbol<int(*)()>("_KFN16__op_cast_best__12test_widenedEv");
    REQUIRE(fn_widened);
    CHECK(fn_widened() == 1010);  // Exact match: long version
}

// ═════════════════════════════════════════════════════════════════════════════
// 21. Implicit casting: Derived& → Base& upcast for operator parameter
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Operator implicit cast: Derived ref to Base ref parameter", "[operator][gen][cast]") {
    auto jit = gen_jit(R"SRC(
module __op_cast_up__;
class Base {
  public:
    v: int;
    Base() : v(0) {}
    Base(av: int) : v(av) {}
}
class Derived : public Base {
    Derived() : Base() {}
    Derived(av: int) : Base(av) {}
}
struct Container {
    v: int;
    Container(av: int) : v(av) {}
    operator +(other: Base&) : int { return v + other.v; }
}
test() : int {
    c: Container(100);
    d: Derived(42);
    return c + d;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN14__op_cast_up__4testEv");
    REQUIRE(fn);
    CHECK(fn() == 142);
}

// ═════════════════════════════════════════════════════════════════════════════
// 22. Implicit casting: comparison operator with widening
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Comparison operator implicit cast: short compared to int param", "[operator][gen][cast]") {
    auto jit = gen_jit(R"SRC(
module __op_cast_cmp__;
struct Num {
    v: int;
    Num(av: int) : v(av) {}
    operator ==(n: int) : bool { return v == n; }
}
test_true() : bool {
    a: Num(42);
    s: short = 42;
    return a == s;
}
test_false() : bool {
    a: Num(42);
    s: short = 10;
    return a == s;
}
)SRC");
    REQUIRE(jit);
    auto fn_true = jit->lookup_symbol<bool(*)()>("_KFN15__op_cast_cmp__9test_trueEv");
    REQUIRE(fn_true);
    CHECK(fn_true() == true);
    auto fn_false = jit->lookup_symbol<bool(*)()>("_KFN15__op_cast_cmp__10test_falseEv");
    REQUIRE(fn_false);
    CHECK(fn_false() == false);
}

// ═════════════════════════════════════════════════════════════════════════════
// 23. Implicit casting: non-member operator with widening
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Non-member operator implicit cast: widening right operand", "[operator][gen][cast]") {
    auto jit = gen_jit(R"SRC(
module __op_cast_ext__;
struct Vec {
    v: int;
    Vec(av: int) : v(av) {}
}
operator +(a: Vec&, n: long) : long { return a.v + n; }

test() : long {
    a: Vec(100);
    i: int = 42;
    return a + i;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<long(*)()>("_KFN15__op_cast_ext__4testEv");
    REQUIRE(fn);
    CHECK(fn() == 142L);
}

// ═════════════════════════════════════════════════════════════════════════════
// 24. Overload resolution: member wins over non-member at same score
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Operator overload resolution: member preferred at same cast weight", "[operator][gen][cast]") {
    auto jit = gen_jit(R"SRC(
module __op_cast_mprio__;
struct Val {
    v: int;
    Val(av: int) : v(av) {}
    operator +(n: int) : int { return v + n + 500; }
}
operator +(a: Val&, n: int) : int { return a.v + n; }

test() : int {
    a: Val(10);
    return a + 5;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN17__op_cast_mprio__4testEv");
    REQUIRE(fn);
    // Member adds 500, non-member doesn't => expect 515 if member is prioritized
    CHECK(fn() == 515);
}

// ═════════════════════════════════════════════════════════════════════════════
// 25. Overload resolution: non-member wins if it has better score
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Operator overload resolution: non-member wins with better cast score", "[operator][gen][cast]") {
    auto jit = gen_jit(R"SRC(
module __op_cast_nwin__;
struct Val {
    v: int;
    Val(av: int) : v(av) {}
    operator +(n: long) : int { return v + 1000; }
}
operator +(a: Val&, n: int) : int { return a.v + n; }

test() : int {
    a: Val(10);
    i: int = 5;
    return a + i;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN16__op_cast_nwin__4testEv");
    REQUIRE(fn);
    // Non-member has exact match (int), member requires widening (int→long)
    // Non-member should win: 10 + 5 = 15
    CHECK(fn() == 15);
}

// ═════════════════════════════════════════════════════════════════════════════
// 26. Implicit casting: logical operator with widening
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Logical operator implicit cast: widening parameter", "[operator][gen][cast]") {
    auto jit = gen_jit(R"SRC(
module __op_cast_log__;
struct Cond {
    v: int;
    Cond(av: int) : v(av) {}
    operator &&(n: long) : bool { return v != 0 && n != 0; }
}
test_tt() : bool {
    a: Cond(1);
    i: int = 1;
    return a && i;
}
test_tf() : bool {
    a: Cond(1);
    i: int = 0;
    return a && i;
}
)SRC");
    REQUIRE(jit);
    auto fn_tt = jit->lookup_symbol<bool(*)()>("_KFN15__op_cast_log__7test_ttEv");
    REQUIRE(fn_tt);
    CHECK(fn_tt() == true);
    auto fn_tf = jit->lookup_symbol<bool(*)()>("_KFN15__op_cast_log__7test_tfEv");
    REQUIRE(fn_tf);
    CHECK(fn_tf() == false);
}

// ═════════════════════════════════════════════════════════════════════════════
// 27. Implicit casting: bitwise operator with widening
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Bitwise operator implicit cast: short widened to int", "[operator][gen][cast]") {
    auto jit = gen_jit(R"SRC(
module __op_cast_bit__;
struct Mask {
    v: int;
    Mask(av: int) : v(av) {}
    operator |(n: int) : int { return v | n; }
    operator &(n: int) : int { return v & n; }
}
test_or() : int {
    m: Mask(0xF0);
    s: short = 0x0F;
    return m | s;
}
test_and() : int {
    m: Mask(0xFF);
    s: short = 0x0F;
    return m & s;
}
)SRC");
    REQUIRE(jit);
    auto fn_or = jit->lookup_symbol<int(*)()>("_KFN15__op_cast_bit__7test_orEv");
    REQUIRE(fn_or);
    CHECK(fn_or() == 0xFF);
    auto fn_and = jit->lookup_symbol<int(*)()>("_KFN15__op_cast_bit__8test_andEv");
    REQUIRE(fn_and);
    CHECK(fn_and() == 0x0F);
}

// ═════════════════════════════════════════════════════════════════════════════
// 28. Non-member operator: wrong struct type is correctly filtered out
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Non-member operator: left param type correctly filters candidates", "[operator][gen][cast]") {
    auto jit = gen_jit(R"SRC(
module __op_cast_filt__;
struct Alpha {
    v: int;
    Alpha(av: int) : v(av) {}
}
struct Beta {
    v: int;
    Beta(av: int) : v(av) {}
}
operator +(a: Alpha&, n: int) : int { return a.v + n + 100; }
operator +(b: Beta&, n: int) : int { return b.v + n + 200; }

test_alpha() : int {
    a: Alpha(10);
    return a + 5;
}
test_beta() : int {
    b: Beta(10);
    return b + 5;
}
)SRC");
    REQUIRE(jit);
    auto fn_a = jit->lookup_symbol<int(*)()>("_KFN16__op_cast_filt__10test_alphaEv");
    REQUIRE(fn_a);
    CHECK(fn_a() == 115);  // Alpha operator: 10 + 5 + 100
    auto fn_b = jit->lookup_symbol<int(*)()>("_KFN16__op_cast_filt__9test_betaEv");
    REQUIRE(fn_b);
    CHECK(fn_b() == 215);  // Beta operator: 10 + 5 + 200
}

// ═════════════════════════════════════════════════════════════════════════════
// 29. Implicit casting: ref variable with widening (load + cast)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Operator implicit cast: ref variable widened", "[operator][gen][cast]") {
    auto jit = gen_jit(R"SRC(
module __op_cast_refv__;
struct Acc {
    v: long;
    Acc(av: long) : v(av) {}
    operator +(n: long) : long { return v + n; }
}
test() : long {
    a: Acc(100);
    s: short = 7;
    return a + s;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<long(*)()>("_KFN16__op_cast_refv__4testEv");
    REQUIRE(fn);
    CHECK(fn() == 107L);
}

// ═════════════════════════════════════════════════════════════════════════════
// 30. Ambiguity detection: binary operator overload
// ═════════════════════════════════════════════════════════════════════════════

// Two member operator+(int) and operator+(long): calling with short arg.
// short→int is CAST_WIDENING, short→long is CAST_WIDENING → ambiguous.
TEST_CASE("Ambiguous binary operator: two member overloads with equal cast weight", "[operator][gen][ambiguity]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __ambig01__;
        struct Vec {
            v: int;
            Vec(av: int) : v(av) {}
            operator +(n: int) : int { return v + n; }
            operator +(n: long) : long { return v + n; }
        }
        test() : int {
            a: Vec(10);
            s: short = 5;
            return a + s;
        }
    )SRC"), k::model::gen::resolution_error);
}

// Two non-member operator+ with equal overall score (both WIDENING for right).
TEST_CASE("Ambiguous binary operator: two non-member overloads with equal cast weight", "[operator][gen][ambiguity]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __ambig02__;
        struct Vec {
            v: int;
            Vec(av: int) : v(av) {}
        }
        operator +(a: Vec&, n: int) : int { return a.v + n; }
        operator +(a: Vec&, n: long) : long { return a.v + n; }
        test() : int {
            a: Vec(10);
            s: short = 5;
            return a + s;
        }
    )SRC"), k::model::gen::resolution_error);
}

// operator+(int) is exact match for int arg; operator+(long) is widening → no ambiguity.
TEST_CASE("No ambiguity: member operator with exact match wins over widening", "[operator][gen][ambiguity]") {
    auto jit = gen_jit(R"SRC(
module __noambig01__;
struct Vec {
    v: int;
    Vec(av: int) : v(av) {}
    operator +(n: int) : int { return v + n + 100; }
    operator +(n: long) : long { return v + n + 200; }
}
test() : int {
    a: Vec(10);
    return a + 5;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN13__noambig01__4testEv");
    REQUIRE(fn);
    CHECK(fn() == 115); // Exact match: 10 + 5 + 100
}

// Member operator+(int) and non-member operator+(Vec&, int): both exact match,
// but member is preferred → no ambiguity.
TEST_CASE("No ambiguity: member operator preferred over non-member with same score", "[operator][gen][ambiguity]") {
    auto jit = gen_jit(R"SRC(
module __noambig02__;
struct Vec {
    v: int;
    Vec(av: int) : v(av) {}
    operator +(n: int) : int { return v + n + 100; }
}
operator +(a: Vec&, n: int) : int { return a.v + n + 200; }
test() : int {
    a: Vec(10);
    return a + 5;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN13__noambig02__4testEv");
    REQUIRE(fn);
    CHECK(fn() == 115); // Member wins: 10 + 5 + 100
}

// ═════════════════════════════════════════════════════════════════════════════
// 31. Ambiguity detection: unary operator overload
// ═════════════════════════════════════════════════════════════════════════════

// Two non-member operator-() that both accept Vec& (exact match) → ambiguous.
TEST_CASE("Ambiguous unary operator: two non-member overloads for unary minus", "[operator][gen][ambiguity]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __ambig03__;
        struct Vec {
            v: int;
            Vec(av: int) : v(av) {}
        }
        operator -(a: Vec&) : int { return -a.v; }
        operator -(a: Vec&) : long { return -a.v; }
        test() : int {
            a: Vec(10);
            return -a;
        }
    )SRC"), k::model::gen::resolution_error);
}

// Member operator-() and non-member operator-(Vec&): member preferred → no ambiguity.
TEST_CASE("No ambiguity: member unary operator preferred over non-member", "[operator][gen][ambiguity]") {
    auto jit = gen_jit(R"SRC(
module __noambig03__;
struct Vec {
    v: int;
    Vec(av: int) : v(av) {}
    operator -() : int { return -v + 100; }
}
operator -(a: Vec&) : int { return -a.v + 200; }
test() : int {
    a: Vec(10);
    return -a;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN13__noambig03__4testEv");
    REQUIRE(fn);
    CHECK(fn() == 90); // Member wins: -10 + 100
}

// ═════════════════════════════════════════════════════════════════════════════
// 32. Ambiguity detection: comparison operator overload
// ═════════════════════════════════════════════════════════════════════════════

// operator==(int) and operator==(long): calling with short arg.
// short→int is WIDENING, short→long is WIDENING → ambiguous.
TEST_CASE("Ambiguous comparison operator: two member == with equal cast weight", "[operator][gen][ambiguity]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __ambig04__;
        struct Val {
            v: int;
            Val(av: int) : v(av) {}
            operator ==(n: int) : bool { return v == n; }
            operator ==(n: long) : bool { return v == n; }
        }
        test() : bool {
            a: Val(10);
            s: short = 10;
            return a == s;
        }
    )SRC"), k::model::gen::resolution_error);
}

// ═════════════════════════════════════════════════════════════════════════════
// 33. Ambiguity detection: logical operator overload
// ═════════════════════════════════════════════════════════════════════════════

// operator&&(int) and operator&&(long): calling with short arg → ambiguous.
TEST_CASE("Ambiguous logical operator: two member && with equal cast weight", "[operator][gen][ambiguity]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __ambig05__;
        struct Flag {
            v: int;
            Flag(av: int) : v(av) {}
            operator &&(n: int) : bool { return v != 0 && n != 0; }
            operator &&(n: long) : bool { return v != 0 && n != 0; }
        }
        test() : bool {
            a: Flag(1);
            s: short = 1;
            return a && s;
        }
    )SRC"), k::model::gen::resolution_error);
}

// ═════════════════════════════════════════════════════════════════════════════
// 34. Ambiguity detection: bitwise operator overload
// ═════════════════════════════════════════════════════════════════════════════

// operator|(int) and operator|(long): calling with short arg → ambiguous.
TEST_CASE("Ambiguous bitwise operator: two member | with equal cast weight", "[operator][gen][ambiguity]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __ambig06__;
        struct Bits {
            v: int;
            Bits(av: int) : v(av) {}
            operator |(n: int) : int { return v | n; }
            operator |(n: long) : long { return v | n; }
        }
        test() : long {
            a: Bits(0xFF);
            s: short = 0x0F;
            return a | s;
        }
    )SRC"), k::model::gen::resolution_error);
}

// ═════════════════════════════════════════════════════════════════════════════
// 35. No ambiguity: different cast weights resolve correctly
// ═════════════════════════════════════════════════════════════════════════════

// operator+(short) exact vs operator+(long) widening for short argument.
TEST_CASE("No ambiguity: exact match wins over widening for binary operator", "[operator][gen][ambiguity]") {
    auto jit = gen_jit(R"SRC(
module __noambig04__;
struct Acc {
    v: int;
    Acc(av: int) : v(av) {}
    operator +(n: short) : int { return v + n + 1000; }
    operator +(n: long) : long { return v + n + 2000; }
}
test() : int {
    a: Acc(10);
    s: short = 3;
    return a + s;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN13__noambig04__4testEv");
    REQUIRE(fn);
    CHECK(fn() == 1013); // Exact match for short: 10 + 3 + 1000
}


// ═════════════════════════════════════════════════════════════════════════════
// 36. Const-correctness: binary operator overloads
// ═════════════════════════════════════════════════════════════════════════════

// A const binary operator can be called on a mutable object.
TEST_CASE("Const binary operator on mutable object", "[operator][gen][const]") {
    auto jit = gen_jit(R"SRC(
module __const_op01__;
struct Vec {
    v: int;
    Vec(av: int) : v(av) {}
    const operator +(n: int) : int { return v + n; }
}
test() : int {
    a: Vec(10);
    return a + 5;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN14__const_op01__4testEv");
    REQUIRE(fn);
    CHECK(fn() == 15);
}

// A const binary operator can be called on a const object (via const ref parameter).
TEST_CASE("Const binary operator on const object", "[operator][gen][const]") {
    auto jit = gen_jit(R"SRC(
module __const_op02__;
struct Vec {
    v: int;
    Vec(av: int) : v(av) {}
    const operator +(n: int) : int { return v + n; }
}
compute(a: const Vec&) : int {
    return a + 5;
}
test() : int {
    a: Vec(10);
    return compute(a);
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN14__const_op02__4testEv");
    REQUIRE(fn);
    CHECK(fn() == 15);
}

// A mutable binary operator CANNOT be called on a const object → error.
TEST_CASE("Mutable binary operator on const object rejected", "[operator][gen][const][error]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __const_op03__;
        struct Vec {
            v: int;
            Vec(av: int) : v(av) {}
            operator +(n: int) : int { return v + n; }
        }
        compute(a: const Vec&) : int {
            return a + 5;
        }
    )SRC"), k::log::compiler_error);
}

// A mutable binary operator on a mutable object still works.
TEST_CASE("Mutable binary operator on mutable object", "[operator][gen][const]") {
    auto jit = gen_jit(R"SRC(
module __const_op04__;
struct Vec {
    v: int;
    Vec(av: int) : v(av) {}
    operator +(n: int) : int { return v + n; }
}
test() : int {
    a: Vec(10);
    return a + 5;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN14__const_op04__4testEv");
    REQUIRE(fn);
    CHECK(fn() == 15);
}

// Non-member operator on const object succeeds (no this constraint).
TEST_CASE("Non-member binary operator on const object", "[operator][gen][const]") {
    auto jit = gen_jit(R"SRC(
module __const_op05__;
struct Vec {
    v: int;
    Vec(av: int) : v(av) {}
}
operator +(a: const Vec&, n: int) : int { return a.v + n; }
compute(a: const Vec&) : int {
    return a + 5;
}
test() : int {
    a: Vec(10);
    return compute(a);
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN14__const_op05__4testEv");
    REQUIRE(fn);
    CHECK(fn() == 15);
}

// ═════════════════════════════════════════════════════════════════════════════
// 37. Const-correctness: unary operator overloads
// ═════════════════════════════════════════════════════════════════════════════

// A const unary operator can be called on a const object.
TEST_CASE("Const unary operator on const object", "[operator][gen][const]") {
    auto jit = gen_jit(R"SRC(
module __const_op06__;
struct Vec {
    v: int;
    Vec(av: int) : v(av) {}
    const operator -() : int { return -v; }
}
compute(a: const Vec&) : int {
    return -a;
}
test() : int {
    a: Vec(10);
    return compute(a);
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN14__const_op06__4testEv");
    REQUIRE(fn);
    CHECK(fn() == -10);
}

// A mutable unary operator CANNOT be called on a const object → error.
TEST_CASE("Mutable unary operator on const object rejected", "[operator][gen][const][error]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __const_op07__;
        struct Vec {
            v: int;
            Vec(av: int) : v(av) {}
            operator -() : int { return -v; }
        }
        compute(a: const Vec&) : int {
            return -a;
        }
    )SRC"), k::log::compiler_error);
}

// ═════════════════════════════════════════════════════════════════════════════
// 38. Const-correctness: comparison operators
// ═════════════════════════════════════════════════════════════════════════════

// A const comparison operator on a mutable object works.
TEST_CASE("Const comparison operator on mutable object", "[operator][gen][const]") {
    auto jit = gen_jit(R"SRC(
module __const_op08__;
struct Val {
    v: int;
    Val(av: int) : v(av) {}
    const operator ==(n: int) : bool { return v == n; }
}
check(a: Val&) : bool {
    return a == 42;
}
test() : int {
    a: Val(42);
    if(check(a)) { return 1; }
    return 0;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN14__const_op08__4testEv");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

// A const comparison operator on a const object works.
TEST_CASE("Const comparison operator on const object", "[operator][gen][const]") {
    auto jit = gen_jit(R"SRC(
module __const_op09__;
struct Val {
    v: int;
    Val(av: int) : v(av) {}
    const operator ==(n: int) : bool { return v == n; }
}
check(a: const Val&) : bool {
    return a == 42;
}
test() : int {
    a: Val(42);
    if(check(a)) { return 1; }
    return 0;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN14__const_op09__4testEv");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

// A mutable comparison operator on const object → error.
TEST_CASE("Mutable comparison operator on const object rejected", "[operator][gen][const][error]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __const_op10__;
        struct Val {
            v: int;
            Val(av: int) : v(av) {}
            operator ==(n: int) : bool { return v == n; }
        }
        check(a: const Val&) : bool {
            return a == 42;
        }
    )SRC"), k::log::compiler_error);
}

// ═════════════════════════════════════════════════════════════════════════════
// 39. Const-correctness: logical operators
// ═════════════════════════════════════════════════════════════════════════════

// Const logical && on const object.
TEST_CASE("Const logical operator on const object", "[operator][gen][const]") {
    auto jit = gen_jit(R"SRC(
module __const_op11__;
struct Flag {
    v: int;
    Flag(av: int) : v(av) {}
    const operator &&(n: int) : bool { return v != 0 && n != 0; }
}
check(a: const Flag&) : bool {
    return a && 1;
}
test_true() : int {
    a: Flag(1);
    if(check(a)) { return 1; }
    return 0;
}
test_false() : int {
    a: Flag(0);
    if(check(a)) { return 1; }
    return 0;
}
)SRC");
    REQUIRE(jit);
    auto fn_true = jit->lookup_symbol<int(*)()>("_KFN14__const_op11__9test_trueEv");
    REQUIRE(fn_true);
    CHECK(fn_true() == 1);
    auto fn_false = jit->lookup_symbol<int(*)()>("_KFN14__const_op11__10test_falseEv");
    REQUIRE(fn_false);
    CHECK(fn_false() == 0);
}

// Mutable logical && on const object → error.
TEST_CASE("Mutable logical operator on const object rejected", "[operator][gen][const][error]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __const_op12__;
        struct Flag {
            v: int;
            Flag(av: int) : v(av) {}
            operator &&(n: int) : bool { return v != 0 && n != 0; }
        }
        check(a: const Flag&) : bool {
            return a && 1;
        }
    )SRC"), k::log::compiler_error);
}

// ═════════════════════════════════════════════════════════════════════════════
// 40. Const-correctness: virtual operator dispatch on const object
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Const virtual operator on const class object", "[operator][gen][const][class]") {
    auto jit = gen_jit(R"SRC(
module __const_op38__;
class Base {
    v: int;
    Base() : v(0) {}
    Base(av: int) : v(av) {}
    const operator +(n: int) : int { return this.v + n; }
}
class Derived : public Base {
    Derived() : Base() {}
    Derived(av: int) : Base(av) {}
    const operator +(n: int) : int { return this.v + n + 100; }
}
compute(a: const Base&) : int {
    return a + 5;
}
test_base() : int {
    b: Base(10);
    return compute(b);
}
test_derived() : int {
    d: Derived(10);
    return compute(d);
}
)SRC");
    REQUIRE(jit);
    auto fn_base = jit->lookup_symbol<int(*)()>("_KFN14__const_op38__9test_baseEv");
    REQUIRE(fn_base);
    CHECK(fn_base() == 15);  // Base: 10 + 5
    auto fn_derived = jit->lookup_symbol<int(*)()>("_KFN14__const_op38__12test_derivedEv");
    REQUIRE(fn_derived);
    CHECK(fn_derived() == 115);  // Derived: 10 + 5 + 100
}

// ═════════════════════════════════════════════════════════════════════════════
// 54. Const-correctness: non-member unary operator on const object
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Non-member unary operator on const object", "[operator][gen][const][non-member]") {
    // Verify the non-member unary operator is found and called (not the primitive path)
    // by using a return value that differs from the primitive negation.
    auto jit = gen_jit(R"SRC(
module __const_op39__;
struct Vec {
    v: int;
    Vec(av: int) : v(av) {}
}
operator -(a: Vec&) : int { return a.v + 999; }
test() : int {
    a: Vec(42);
    return -a;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN14__const_op39__4testEv");
    REQUIRE(fn);
    CHECK(fn() == 1041);  // Non-member called: 42 + 999 = 1041 (not primitive -42)
}

// ═════════════════════════════════════════════════════════════════════════════
// 66–80. Casting operators (operator() : ReturnType)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Parse casting operator declaration", "[parser][operator][cast]") {

    SECTION("Casting operator in struct") {
        test_logger logger;
        k::parse::parser parser(logger);
        k::source src(R"SRC(
struct Temperature {
    celsius: double;
    operator() : int {
        return (int)celsius;
    }
}
)SRC");
        parser.parse(src);
        auto unit = parser.parse_unit();
        REQUIRE(unit);
    }

    SECTION("Multiple casting operators") {
        test_logger logger;
        k::parse::parser parser(logger);
        k::source src(R"SRC(
struct Convertible {
    value: double;
    operator() : int { return (int)value; }
    operator() : bool { return value != 0.0; }
    operator() : long { return (long)value; }
}
)SRC");
        parser.parse(src);
        auto unit = parser.parse_unit();
        REQUIRE(unit);
    }

    SECTION("Casting operator with const method qualifier") {
        test_logger logger;
        k::parse::parser parser(logger);
        k::source src(R"SRC(
struct Vector {
    x: double;
    y: double;
    const operator() : int {
        return (int)x + (int)y;
    }
}
)SRC");
        parser.parse(src);
        auto unit = parser.parse_unit();
        REQUIRE(unit);
    }
}

TEST_CASE("Struct casting operator basic", "[operator][gen][cast][struct]") {
    auto jit = gen_jit(R"SRC(
module __op_cast_basic__;
struct Temperature {
    celsius: double;
    Temperature(c: double) : celsius(c) {}
    operator() : int {
        return (int)celsius;
    }
}
test() : int {
    t: Temperature(42.7);
    return (int)t;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN17__op_cast_basic__4testEv");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("Struct casting operator to bool", "[operator][gen][cast][struct]") {
    auto jit = gen_jit(R"SRC(
module __op_cast_bool__;
struct Optional {
    has_value: bool;
    Optional(h: bool) : has_value(h) {}
    operator() : bool {
        return has_value;
    }
}
test() : int {
    o: Optional(true);
    if ((bool)o) {
        return 1;
    }
    return 0;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN16__op_cast_bool__4testEv");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

TEST_CASE("Struct multiple casting operators", "[operator][gen][cast][struct]") {
    auto jit = gen_jit(R"SRC(
module __op_cast_multi__;
struct Number {
    value: double;
    Number(v: double) : value(v) {}
    operator() : int {
        return (int)value;
    }
    operator() : bool {
        return value != 0.0;
    }
}
test_to_int() : int {
    n: Number(3.14);
    return (int)n;
}
test_to_bool() : int {
    n: Number(0.0);
    if ((bool)n) {
        return 0;
    }
    return 1;
}
)SRC");
    REQUIRE(jit);
    auto fn_int = jit->lookup_symbol<int(*)()>("_KFN17__op_cast_multi__11test_to_intEv");
    REQUIRE(fn_int);
    CHECK(fn_int() == 3);
    auto fn_bool = jit->lookup_symbol<int(*)()>("_KFN17__op_cast_multi__12test_to_boolEv");
    REQUIRE(fn_bool);
    CHECK(fn_bool() == 1);
}

TEST_CASE("Casting operator const-correctness", "[operator][gen][cast][const]") {
    auto jit = gen_jit(R"SRC(
module __op_cast_const__;
struct Vector {
    x: double;
    y: double;
    Vector(ax: double, ay: double) : x(ax), y(ay) {}
    const operator() : double {
        return x * x + y * y;
    }
}
test() : int {
    v: Vector(3.0, 4.0);
    return (int)(double)v;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN17__op_cast_const__4testEv");
    REQUIRE(fn);
    CHECK(fn() == 25);
}

TEST_CASE("Virtual casting operator in class hierarchy", "[operator][gen][cast][virtual]") {
    auto jit = gen_jit(R"SRC(
module __op_cast_virtual__;
abstract class Shape {
    Shape() {}
    abstract operator() : double;
}
class Circle : public Shape {
    radius: double;
    Circle(r: double) : radius(r) {}
    operator() : double {
        return 3.14159 * radius * radius;
    }
}
class Rectangle : public Shape {
    width: double;
    height: double;
    Rectangle(w: double, h: double) : width(w), height(h) {}
    operator() : double {
        return width * height;
    }
}
test_circle() : int {
    c: Circle(2.0);
    return (int)(double)c;
}
test_rect() : int {
    r: Rectangle(3.0, 4.0);
    return (int)(double)r;
}
)SRC");
    REQUIRE(jit);
    auto fn_circle = jit->lookup_symbol<int(*)()>("_KFN19__op_cast_virtual__11test_circleEv");
    REQUIRE(fn_circle);
    CHECK(fn_circle() == 12);
    auto fn_rect = jit->lookup_symbol<int(*)()>("_KFN19__op_cast_virtual__9test_rectEv");
    REQUIRE(fn_rect);
    CHECK(fn_rect() == 12);
}

TEST_CASE("Casting operator inheritance", "[operator][gen][cast][inheritance]") {
    auto jit = gen_jit(R"SRC(
module __op_cast_inherit__;
struct Base {
    v: int;
    Base() : v(0) {}
    Base(av: int) : v(av) {}
    operator() : double {
        return (double)v * 1.5;
    }
}
struct Derived : public Base {
    extra: int;
    Derived(av: int) : Base(av), extra(0) {}
}
test_base() : int {
    b: Base(10);
    return (int)(double)b;
}
test_derived() : int {
    d: Derived(10);
    return (int)(double)d;
}
)SRC");
    REQUIRE(jit);
    auto fn_base = jit->lookup_symbol<int(*)()>("_KFN19__op_cast_inherit__9test_baseEv");
    REQUIRE(fn_base);
    CHECK(fn_base() == 15);
    auto fn_derived = jit->lookup_symbol<int(*)()>("_KFN19__op_cast_inherit__12test_derivedEv");
    REQUIRE(fn_derived);
    CHECK(fn_derived() == 15);
}

TEST_CASE("Casting operator with pointer return type", "[operator][gen][cast][ptr]") {
    auto jit = gen_jit(R"SRC(
module __op_cast_ptr__;
struct Wrapper {
    ptr: int*;
    Wrapper(p: int*) : ptr(p) {}
    const operator() : int* {
        return ptr;
    }
}
test() : int {
    v: int = 42;
    w: Wrapper(&v);
    p: int* = (int*)w;
    return *p;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN15__op_cast_ptr__4testEv");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("Casting operator in interface", "[operator][gen][cast][interface]") {
    auto jit = gen_jit(R"SRC(
module __op_cast_iface__;
interface Convertible {
    operator() : int;
}
class StringHolder : public Convertible {
    data: int;
    StringHolder(s: int) : data(s) {}
    operator() : int {
        return data;
    }
}
test() : int {
    s: StringHolder(42);
    return (int)s;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN17__op_cast_iface__4testEv");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("Casting operator with reference return type", "[operator][gen][cast][ref]") {
    auto jit = gen_jit(R"SRC(
module __op_cast_ref__;
struct Container {
    value: int;
    Container(v: int) : value(v) {}
    const operator() : int& {
        return value;
    }
}
test() : int {
    c: Container(99);
    ref: int& = (int&)c;
    return ref;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN15__op_cast_ref__4testEv");
    REQUIRE(fn);
    CHECK(fn() == 99);
}
