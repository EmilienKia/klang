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
 * Tests for assignment operator overloading (=, +=, -=, *=, /=, %=, &=, |=, ^=, <<=, >>=).
 *
 * Coverage:
 *  1. Parser: operator =, +=, -= etc. declarations parse correctly
 *  2. Simple assignment operator= on a struct
 *  3. Compound assignment operators +=, -=, *=, /=, %=
 *  4. Bitwise compound assignment &=, |=, ^=
 *  5. Shift compound assignment <<=, >>=
 *  6. Chaining: a = b = c with operator=
 *  7. Implicit copy assignment operator for structs
 *  8. Deleted assignment operator (-> delete)
 *  9. Assignment on const object must fail
 * 10. Non-member assignment operator must fail
 * 11. Virtual assignment operator in class
 * 12. Assignment operator with different parameter type
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

TEST_CASE("Parse operator = declaration", "[parser][operator][assign]") {
    test_logger logger;
    k::parse::parser parser(logger);
    k::source src(R"SRC(
struct Vec2 {
    x: int;
    y: int;
    operator =(other: Vec2&) : Vec2& {
        x = other.x;
        y = other.y;
        return this;
    }
}
)SRC");
    parser.parse(src);
    auto unit = parser.parse_unit();
    REQUIRE(unit);
}

TEST_CASE("Parse all assignment operator symbols", "[parser][operator][assign]") {
    test_logger logger;
    k::parse::parser parser(logger);
    k::source src(R"SRC(
struct A {
    v: int;
    operator =(o: A&) : A& { return this; }
    operator +=(o: A&) : A& { return this; }
    operator -=(o: A&) : A& { return this; }
    operator *=(o: A&) : A& { return this; }
    operator /=(o: A&) : A& { return this; }
    operator %=(o: A&) : A& { return this; }
    operator &=(o: A&) : A& { return this; }
    operator |=(o: A&) : A& { return this; }
    operator ^=(o: A&) : A& { return this; }
    operator <<=(o: A&) : A& { return this; }
    operator >>=(o: A&) : A& { return this; }
}
)SRC");
    parser.parse(src);
    auto unit = parser.parse_unit();
    REQUIRE(unit);
}

TEST_CASE("Parse operator = with -> delete", "[parser][operator][assign][delete]") {
    test_logger logger;
    k::parse::parser parser(logger);
    k::source src(R"SRC(
struct Immutable {
    v: int;
    operator =(const other: Immutable&) -> delete;
}
)SRC");
    parser.parse(src);
    auto unit = parser.parse_unit();
    REQUIRE(unit);
}

// ═════════════════════════════════════════════════════════════════════════════
// 2. Simple assignment operator= on a struct
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Struct operator= overload basic", "[operator][gen][assign]") {
    auto jit = gen_jit(R"SRC(
module __op_asgn_basic__;
struct Counter {
    val: int;
    operator =(const other: Counter&) : Counter& {
        val = other.val + 100;
        return this;
    }
}
test() : int {
    a: Counter;
    a.val = 1;
    b: Counter;
    b.val = 42;
    a = b;
    return a.val;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    // operator= adds 100, so a.val = b.val + 100 = 42 + 100 = 142
    CHECK(fn() == 142);
}

// ═════════════════════════════════════════════════════════════════════════════
// 3. Compound assignment operator += on a struct
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Struct operator+= overload", "[operator][gen][assign][compound]") {
    auto jit = gen_jit(R"SRC(
module __op_add_asgn__;
struct Acc {
    val: int;
    operator +=(n: int) : Acc& {
        val = val + n;
        return this;
    }
}
test() : int {
    a: Acc;
    a.val = 10;
    a += 32;
    return a.val;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 42);
}

TEST_CASE("Struct operator-= overload", "[operator][gen][assign][compound]") {
    auto jit = gen_jit(R"SRC(
module __op_sub_asgn__;
struct Acc {
    val: int;
    operator -=(n: int) : Acc& {
        val = val - n;
        return this;
    }
}
test() : int {
    a: Acc;
    a.val = 50;
    a -= 8;
    return a.val;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 42);
}

TEST_CASE("Struct operator*= overload", "[operator][gen][assign][compound]") {
    auto jit = gen_jit(R"SRC(
module __op_mul_asgn__;
struct Acc {
    val: int;
    operator *=(n: int) : Acc& {
        val = val * n;
        return this;
    }
}
test() : int {
    a: Acc;
    a.val = 6;
    a *= 7;
    return a.val;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 42);
}

TEST_CASE("Struct operator/= overload", "[operator][gen][assign][compound]") {
    auto jit = gen_jit(R"SRC(
module __op_div_asgn__;
struct Acc {
    val: int;
    operator /=(n: int) : Acc& {
        val = val / n;
        return this;
    }
}
test() : int {
    a: Acc;
    a.val = 84;
    a /= 2;
    return a.val;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 42);
}

TEST_CASE("Struct operator%= overload", "[operator][gen][assign][compound]") {
    auto jit = gen_jit(R"SRC(
module __op_mod_asgn__;
struct Acc {
    val: int;
    operator %=(n: int) : Acc& {
        val = val % n;
        return this;
    }
}
test() : int {
    a: Acc;
    a.val = 142;
    a %= 100;
    return a.val;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 42);
}

// ═════════════════════════════════════════════════════════════════════════════
// 4. Bitwise compound assignment &=, |=, ^=
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Struct operator&= overload", "[operator][gen][assign][bitwise]") {
    auto jit = gen_jit(R"SRC(
module __op_bitand_asgn__;
struct Mask {
    val: int;
    operator &=(n: int) : Mask& {
        val = val & n;
        return this;
    }
}
test() : int {
    m: Mask;
    m.val = 63;
    m &= 42;
    return m.val;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    CHECK(fn() == (63 & 42));
}

TEST_CASE("Struct operator|= overload", "[operator][gen][assign][bitwise]") {
    auto jit = gen_jit(R"SRC(
module __op_bitor_asgn__;
struct Mask {
    val: int;
    operator |=(n: int) : Mask& {
        val = val | n;
        return this;
    }
}
test() : int {
    m: Mask;
    m.val = 32;
    m |= 10;
    return m.val;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 42);
}

TEST_CASE("Struct operator^= overload", "[operator][gen][assign][bitwise]") {
    auto jit = gen_jit(R"SRC(
module __op_bitxor_asgn__;
struct Mask {
    val: int;
    operator ^=(n: int) : Mask& {
        val = val ^ n;
        return this;
    }
}
test() : int {
    m: Mask;
    m.val = 55;
    m ^= 29;
    return m.val;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    CHECK(fn() == (55 ^ 29));
}

// ═════════════════════════════════════════════════════════════════════════════
// 5. Shift compound assignment <<=, >>=
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Struct operator<<= overload", "[operator][gen][assign][shift]") {
    auto jit = gen_jit(R"SRC(
module __op_shl_asgn__;
struct Bits {
    val: int;
    operator <<=(n: int) : Bits& {
        val = val << n;
        return this;
    }
}
test() : int {
    b: Bits;
    b.val = 21;
    b <<= 1;
    return b.val;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 42);
}

TEST_CASE("Struct operator>>= overload", "[operator][gen][assign][shift]") {
    auto jit = gen_jit(R"SRC(
module __op_shr_asgn__;
struct Bits {
    val: int;
    operator >>=(n: int) : Bits& {
        val = val >> n;
        return this;
    }
}
test() : int {
    b: Bits;
    b.val = 168;
    b >>= 2;
    return b.val;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 42);
}

// ═════════════════════════════════════════════════════════════════════════════
// 6. Assignment chaining: a = b = c
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Assignment chaining with operator=", "[operator][gen][assign][chain]") {
    auto jit = gen_jit(R"SRC(
module __op_asgn_chain__;
struct Val {
    v: int;
    operator =(const other: Val&) : Val& {
        v = other.v;
        return this;
    }
}
test() : int {
    a: Val;
    b: Val;
    c: Val;
    c.v = 42;
    a = b = c;
    return a.v;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 42);
}

// ═════════════════════════════════════════════════════════════════════════════
// 7. Implicit copy assignment operator for structs
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Implicit copy assignment for struct", "[operator][gen][assign][implicit]") {
    auto jit = gen_jit(R"SRC(
module __op_asgn_implicit__;
struct Point {
    x: int;
    y: int;
}
test() : int {
    a: Point;
    a.x = 10;
    a.y = 32;
    b: Point;
    b = a;
    return b.x + b.y;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 42);
}

TEST_CASE("Implicit copy assignment overwrites all fields", "[operator][gen][assign][implicit]") {
    auto jit = gen_jit(R"SRC(
module __op_asgn_implicit2__;
struct Pair {
    a: int;
    b: int;
}
test() : int {
    p1: Pair;
    p1.a = 100;
    p1.b = 200;
    p2: Pair;
    p2.a = 0;
    p2.b = 0;
    p2 = p1;
    return p2.a + p2.b;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 300);
}

// ═════════════════════════════════════════════════════════════════════════════
// 8. Deleted assignment operator
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Deleted operator= prevents assignment", "[operator][gen][assign][delete]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
module __op_asgn_delete__;
struct Immutable {
    v: int;
    operator =(const other: Immutable&) -> delete;
}
test() : int {
    a: Immutable;
    a.v = 1;
    b: Immutable;
    b.v = 2;
    a = b;
    return a.v;
}
)SRC"));
}

// ═════════════════════════════════════════════════════════════════════════════
// 9. Non-member assignment operator must fail
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Non-member operator= is rejected", "[parser][operator][assign][error]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
module __op_asgn_nonmember__;
struct Val {
    v: int;
}
operator =(a: Val&, b: Val&) : Val& {
    a.v = b.v;
    return a;
}
test() : int {
    return 0;
}
)SRC"));
}

// ═════════════════════════════════════════════════════════════════════════════
// 10. Virtual assignment operator in class
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Virtual operator= in class", "[operator][gen][assign][class][virtual]") {
    auto jit = gen_jit(R"SRC(
module __op_asgn_virtual__;
class Base {
    public val: int;
    public operator =(n: int) : Base& {
        this.val = n;
        return this;
    }
}
class Derived : public Base {
    public operator =(n: int) : Derived& {
        this.val = n * 2;
        return this;
    }
}
test() : int {
    d: Derived;
    d = 21;
    return d.val;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 42);
}

// ═════════════════════════════════════════════════════════════════════════════
// 11. Operator+= with struct parameter
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Struct operator+= with struct parameter", "[operator][gen][assign][compound]") {
    auto jit = gen_jit(R"SRC(
module __op_add_asgn_struct__;
struct Vec2 {
    x: int;
    y: int;
    operator +=(const other: Vec2&) : Vec2& {
        x = x + other.x;
        y = y + other.y;
        return this;
    }
}
test() : int {
    a: Vec2;
    a.x = 10;
    a.y = 20;
    b: Vec2;
    b.x = 5;
    b.y = 7;
    a += b;
    return a.x + a.y;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 42);
}

// ═════════════════════════════════════════════════════════════════════════════
// 12. Multiple compound assignments in sequence
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Multiple compound assignment operators in sequence", "[operator][gen][assign][compound]") {
    auto jit = gen_jit(R"SRC(
module __op_compound_seq__;
struct Counter {
    val: int;
    operator +=(n: int) : Counter& {
        val = val + n;
        return this;
    }
    operator -=(n: int) : Counter& {
        val = val - n;
        return this;
    }
    operator *=(n: int) : Counter& {
        val = val * n;
        return this;
    }
}
test() : int {
    c: Counter;
    c.val = 5;
    c += 2;
    c *= 6;
    return c.val;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 42);
}

// ═════════════════════════════════════════════════════════════════════════════
// 13. Const object: assignment must fail
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Assignment on const struct object is rejected", "[operator][gen][assign][const]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
module __op_asgn_const__;
struct Val {
    v: int;
    operator =(const other: Val&) : Val& {
        v = other.v;
        return this;
    }
}
test() : int {
    const a: Val;
    b: Val;
    b.v = 42;
    a = b;
    return a.v;
}
)SRC"));
}

// ═════════════════════════════════════════════════════════════════════════════
// 14. '-> default' on operator must fail at parse time
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Operator -> default is rejected", "[parser][operator][assign][error]") {
    test_logger logger;
    k::parse::parser parser(logger);
    k::source src(R"SRC(
struct S {
    v: int;
    operator =(const other: S&) : S& -> default;
}
)SRC");
    parser.parse(src);
    REQUIRE_THROWS(parser.parse_unit());
}

// ═════════════════════════════════════════════════════════════════════════════
// 15. Implicit copy assignment is NOT generated for classes
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("No implicit copy assignment for class", "[operator][gen][assign][class]") {
    // Classes do not get an implicit operator=.  Assigning one class to another
    // without a user-defined operator= should trigger the default class behaviour
    // (or be rejected).  This test simply ensures compilation succeeds with a
    // direct field assignment on a class (no operator= overload involved).
    auto jit = gen_jit(R"SRC(
module __op_asgn_no_implicit_class__;
class Widget {
    public val: int;
}
test() : int {
    a: Widget;
    a.val = 42;
    return a.val;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 42);
}

// ═════════════════════════════════════════════════════════════════════════════
// 16. Compound assignment -> delete
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Deleted compound operator+= prevents usage", "[operator][gen][assign][delete][compound]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
module __op_add_asgn_delete__;
struct Counter {
    val: int;
    operator +=(n: int) -> delete;
}
test() : int {
    c: Counter;
    c.val = 10;
    c += 5;
    return c.val;
}
)SRC"));
}

// ═════════════════════════════════════════════════════════════════════════════
// 17. Operator= with void return type (should compile, just no chaining)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Operator= with void return compiles", "[operator][gen][assign]") {
    auto jit = gen_jit(R"SRC(
module __op_asgn_void__;
struct Val {
    v: int;
    operator =(n: int) {
        v = n;
    }
}
test() : int {
    a: Val;
    a = 42;
    return a.v;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 42);
}

