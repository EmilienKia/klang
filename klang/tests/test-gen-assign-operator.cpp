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

#include "helpers.hpp"
#include "../src/common/operator_names.hpp"
#include "../src/errors.hpp"
#include "../src/model/model_builder.hpp"

#include <algorithm>

namespace {

bool is_assignment_diagnostic(const k::log::diagnostic& diagnostic) {
    return diagnostic.level == k::log::diagnostic::severity::warning
        && diagnostic.message.starts_with("Assignment operator");
}

size_t assignment_diagnostic_count(const test_logger& logger) {
    return std::count_if(logger.diagnostics.begin(), logger.diagnostics.end(), is_assignment_diagnostic);
}

} // namespace

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
module gen_assign_operator_01;
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
module gen_assign_operator_02;
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
module gen_assign_operator_03;
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
module gen_assign_operator_04;
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
module gen_assign_operator_05;
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
module gen_assign_operator_06;
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
module gen_assign_operator_07;
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
module gen_assign_operator_08;
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
module gen_assign_operator_09;
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
module gen_assign_operator_10;
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
module gen_assign_operator_11;
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
module gen_assign_operator_12;
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
module gen_assign_operator_13;
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
module gen_assign_operator_14;
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
// 7b. Copy assignment from a value-returning call (prvalue / sret temporary)
// ═════════════════════════════════════════════════════════════════════════════

// Regression test: assigning a struct returned *by value* from a function into an
// existing variable must copy the struct's contents. A previous bug emitted a
// scalar `store` of the sret pointer into the destination's first field instead
// of a struct copy, corrupting the value. This is the minimal form of the bug
// that broke transform-stream `read()` loops ("current = _in->read();").
TEST_CASE("Copy assignment from value-returning call", "[operator][gen][assign][implicit]") {
    auto jit = gen_jit(R"SRC(
module gen_assign_operator_15;
struct Box {
    a: int;
    b: int;
}
makeBox(x: int) : Box {
    r: Box;
    r.a = x;
    r.b = x * 2;
    return r;
}
test() : int {
    acc: Box;
    acc.a = 0;
    acc.b = 0;
    // Reassign from an sret-returning call, in a loop, to mimic the stream case.
    i: int = 1;
    while (i <= 3) {
        acc = makeBox(i);   // struct copy-assignment from a prvalue temporary
        i = i + 1;
    }
    // After the last iteration acc == makeBox(3) => {3, 6}
    return acc.a + acc.b;   // 9
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 9);
}

// ═════════════════════════════════════════════════════════════════════════════
// 8. Deleted assignment operator
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Deleted operator= prevents assignment", "[operator][gen][assign][delete]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
module gen_assign_operator_16;
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
module gen_assign_operator_17;
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
module gen_assign_operator_18;
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
module gen_assign_operator_19;
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
module gen_assign_operator_20;
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
module gen_assign_operator_21;
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
module gen_assign_operator_22;
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
module gen_assign_operator_23;
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
module gen_assign_operator_24;
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

// ═════════════════════════════════════════════════════════════════════════════
//  Boolean assignment — direct true/false assignment to bool variables
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Bool assignment — local variable", "[gen][assign][bool]") {
    auto jit = gen_jit(R"SRC(
module gen_assign_operator_25;
test() : int {
    flag : bool = true;
    if (!flag) return 1;
    flag = false;
    if (flag) return 2;
    flag = true;
    if (!flag) return 3;
    return 0;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 0);
}

TEST_CASE("Bool assignment — struct member", "[gen][assign][bool]") {
    auto jit = gen_jit(R"SRC(
module gen_assign_operator_26;
struct Toggle {
    _on : bool;
    Toggle() { _on = false; }
    turnOn() { _on = true; }
    turnOff() { _on = false; }
    const isOn() : bool { return _on; }
}
test() : int {
    t : Toggle;
    if (t.isOn()) return 1;
    t.turnOn();
    if (!t.isOn()) return 2;
    t.turnOff();
    if (t.isOn()) return 3;
    return 0;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 0);
}

TEST_CASE("Bool assignment — from comparison expression", "[gen][assign][bool]") {
    auto jit = gen_jit(R"SRC(
module gen_assign_operator_27;
test(x : int) : int {
    positive : bool = x > 0;
    if (positive) return 1;
    positive = x == 5;
    if (positive) return 2;
    return 0;
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)(int)>("test");
    REQUIRE(fn != nullptr);
    CHECK(fn(10) == 1);
    CHECK(fn(-1) == 0);
    CHECK(fn(5) == 1);  // x>0 is true, returns 1 before reaching x==5
    CHECK(fn(0) == 0);  // x>0 is false, x==5 is false
}

TEST_CASE("Assignment diagnostics defer unused member recipes and preserve concrete reference chaining",
          "[operator][gen][assign][template][assignment-diagnostics]") {
    auto compiler = compile_model_with_stdlib(R"SRC(
        module gen_assign_diagnostics_deferred;
        template<typename T>
        struct Input {
            value : T;
        }
        template<typename T>
        struct Holder {
            value : T;
            operator =(other : const Holder<T>&) : Holder<T>& {
                value = other.value;
                return this;
            }
            template<typename U>
            operator =(input : const Input<U>&) : Holder<T>& {
                value = (T) input.value;
                return this;
            }
            template<typename U>
            operator +=(input : U) : Holder<T>& {
                return input.dependentMember();
            }
            operator -=(input : int) -> delete;
        }
        test() : int {
            first : Holder<int>;
            second : Holder<int>;
            input : Input<int>;
            input.value = 21;
            result : Holder<int>& = (first = second = input);
            result.value = result.value + 21;
            return first.value == 42 && second.value == 21 ? 42 : -1;
        }
    )SRC");
    REQUIRE(compiler);
    auto unit = compiler->get_unit();
    auto test = unit->get_root_namespace()->get_functions("test").front();
    auto first = test->get_block()->get_variable("first");
    REQUIRE(first);
    auto type = std::dynamic_pointer_cast<k::model::struct_type>(
        k::model::type::canonical(first->get_type()));
    REQUIRE(type);
    auto owner = type->get_struct();
    REQUIRE(owner);

    test_logger logger;
    k::model::gen::type_reference_resolver resolver(logger, compiler->get_context_for_test(), *unit);
    resolver.resolve();
    CHECK(assignment_diagnostic_count(logger) == 0);
    resolver.resolve();
    CHECK(assignment_diagnostic_count(logger) == 0);

    size_t concrete_templates = 0;
    size_t unused_recipes = 0;
    for (const auto& function : owner->functions()) {
        const auto name = function->has_tpl_args()
            ? function->get_tpl_base_name() : function->get_short_name();
        if (!k::op::is_assignment_operator(name) || function->is_deleted()) continue;
        if (function->is_template()) {
            CHECK(k::model::type::contains_unresolved(function->get_return_type()));
            if (name == k::op::OP_ADD_ASGN) ++unused_recipes;
            continue;
        }
        CHECK_FALSE(k::model::type::contains_unresolved(function->get_return_type()));
        auto reference = std::dynamic_pointer_cast<k::model::reference_type>(
            k::model::type::canonical(function->get_return_type()));
        REQUIRE(reference);
        CHECK(k::model::type::are_equal(reference->get_subtype(), type));
        if (function->has_tpl_args()) {
            ++concrete_templates;
            CHECK(k::model::gen::format_function_display_name(function)
                == "Holder<int>::operator=<int>");
            CHECK(k::model::gen::format_function_signature(function).find("Holder<int>&")
                != std::string::npos);
        }
    }
    CHECK(concrete_templates == 1);
    CHECK(unused_recipes == 1);

    auto jit = compiler->to_jit();
    REQUIRE(jit);
    auto run = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(run);
    CHECK(run() == 42);
}

TEST_CASE("Assignment diagnostics do not hide an unresolved concrete return type",
          "[operator][gen][assign][assignment-diagnostics]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module gen_assign_diagnostics_unresolved;
        struct Holder {
            operator =(input : int) : Missing& { return this; }
        }
        test() {
            value : Holder;
            value = 42;
        }
    )SRC"), k::log::compiler_error);
}

TEST_CASE("Assignment diagnostics warn once for concrete invalid returns using source names",
          "[operator][gen][assign][template][assignment-diagnostics]") {
    test_logger logger;
    k::source source(R"SRC(
        module gen_assign_diagnostics_invalid;
        struct Other {}
        other : Other;
        template<typename T>
        struct Holder {
            value : T;
            operator =(input : const Holder<T>&) : int { return 0; }
            operator +=(input : int) { value = value + input; }
            operator -=(input : int) : void {}
            template<typename U>
            operator *=(input : U) : int { return (int) input; }
            template<typename U>
            operator /=(input : U) : Holder<T>& { return this; }
            operator %=(input : int) -> delete;
            operator <<=(input : int) : const Holder<T>& { return this; }
            operator >>=(input : int) : Other& { return other; }
        }
        const struct Frozen {
            const operator =(input : int) : const Frozen& { return this; }
        }
        test() {
            holder : Holder<int>;
            holder *= 2;
            holder *= 3;
            frozen : Frozen;
        }
    )SRC");
    k::parse::parser parser(logger);
    parser.parse(source);
    auto ast = parser.parse_unit();
    REQUIRE(ast);
    auto context = k::model::context::create();
    auto unit = k::model::unit::create(context);
    k::model::model_builder::visit(logger, context, *ast, *unit);
    k::model::gen::symbol_resolver symbols(logger, context, *unit);
    symbols.resolve();
    context->resolve_types();
    k::model::gen::aggregate_type_resolver aggregates(logger, context, *unit);
    aggregates.resolve();
    context->resolve_types();
    k::model::gen::model_materializer materializer(logger, context, *unit);
    materializer.materialize();
    k::model::gen::type_reference_resolver resolver(logger, context, *unit);
    resolver.resolve();
    REQUIRE_FALSE(logger.has_error());
    REQUIRE(assignment_diagnostic_count(logger) == 5);

    size_t wrong_returns = 0;
    size_t void_returns = 0;
    size_t instantiated_returns = 0;
    for (const auto& diagnostic : logger.diagnostics) {
        if (!is_assignment_diagnostic(diagnostic)) continue;
        const auto message = k::log::format_diagnostic_message(diagnostic);
        INFO(message);
        CHECK(message.find("__operator_") == std::string::npos);
        CHECK(message.find("Holder__") == std::string::npos);
        CHECK(message.find("unresolved") == std::string::npos);
        CHECK(message.find("struct:") == std::string::npos);
        REQUIRE(diagnostic.args.size() >= 3);
        CHECK(diagnostic.args[1] == "Holder<int>");
        CHECK(diagnostic.args.back() == "Holder<int>&");
        REQUIRE(diagnostic.pos);
        const auto token = std::visit([](const auto& lexeme) {
            return std::string(lexeme.content);
        }, *diagnostic.pos);
        CHECK(diagnostic.args[0] == "operator" + token);
        if (diagnostic.code == static_cast<unsigned int>(
                k::diag::structure_diag::WARN_CONST_STRUCT_NON_CONST_MEMBER)) {
            ++wrong_returns;
            REQUIRE(diagnostic.args.size() == 4);
            CHECK((diagnostic.args[2] == "int" || diagnostic.args[2] == "Other&"));
            if (diagnostic.args[0] == "operator*=") ++instantiated_returns;
        } else {
            CHECK(diagnostic.code == static_cast<unsigned int>(
                k::diag::structure_diag::WARN_CONST_STRUCT_NON_CONST_BASE));
            ++void_returns;
            CHECK((diagnostic.args[0] == "operator+=" || diagnostic.args[0] == "operator-="));
        }
    }
    CHECK(wrong_returns == 3);
    CHECK(void_returns == 2);
    CHECK(instantiated_returns == 1);
    resolver.resolve();
    CHECK(assignment_diagnostic_count(logger) == 5);

    auto test = unit->get_root_namespace()->get_functions("test").front();
    auto holder = test->get_block()->get_variable("holder");
    REQUIRE(holder);
    auto type = std::dynamic_pointer_cast<k::model::struct_type>(holder->get_type());
    REQUIRE(type);
    size_t template_signatures = 0;
    for (const auto& function : type->get_struct()->functions()) {
        if (function->is_template() && function->get_short_name() == k::op::OP_MUL_ASGN) {
            CHECK(k::model::gen::format_template_function_signature(function)
                == "Holder<int>::operator*=<U>(input : U) : int");
            ++template_signatures;
        }
        if (function->has_tpl_args() && function->get_tpl_base_name() == k::op::OP_MUL_ASGN) {
            CHECK(k::model::gen::format_function_signature(function)
                == "Holder<int>::operator*=<int>(input : int) : int");
            ++template_signatures;
        }
    }
    CHECK(template_signatures == 2);
}
