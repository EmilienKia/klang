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
 * Tests for the three-way "spaceship" comparison operator `<=>` (Phase 1).
 *
 * Phase 1 scope: `<=>` is a binary, const, member/global/virtual/static/template
 * operator returning a signed integer or floating-point primitive. Semantics:
 * negative if left < right, positive if left > right, zero if equal.
 *
 * `<=>` acts as the FIRST fallback source for the six comparison operators
 * (== != < > <= >=) when the exact operator is not declared, ranking between
 * the exact (DIRECT) operator and the older NEGATE/SWAP/SWAP_NEGATE/COMPOSITE
 * fallback tiers (see k::model::cmp_synthesis::SPACESHIP / SPACESHIP_SWAP and
 * resolve_comparison_with_fallback() in gen_operators_overload.cpp).
 *
 * See doc/spec/language/functions/operators.md for the full specification.
 */

#include "helpers.hpp"
#include <catch2/catch_all.hpp>

// ═════════════════════════════════════════════════════════════════════════════
// 1. Builtin primitive `<=>`
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Builtin primitive <=> on int", "[gen][spaceship][primitive]") {
    auto jit = gen_jit(R"SRC(
module __ss_prim_int__;
lt() : int { return 3 <=> 5; }
eq() : int { return 5 <=> 5; }
gt() : int { return 7 <=> 5; }
)SRC");
    REQUIRE(jit);
    auto lt = jit->lookup_symbol<int(*)()>("_KFN15__ss_prim_int__2ltEv");
    auto eq = jit->lookup_symbol<int(*)()>("_KFN15__ss_prim_int__2eqEv");
    auto gt = jit->lookup_symbol<int(*)()>("_KFN15__ss_prim_int__2gtEv");
    REQUIRE(lt); REQUIRE(eq); REQUIRE(gt);
    CHECK(lt() < 0);
    CHECK(eq() == 0);
    CHECK(gt() > 0);
}

TEST_CASE("Builtin primitive <=> on double", "[gen][spaceship][primitive]") {
    auto jit = gen_jit(R"SRC(
module __ss_prim_dbl__;
lt() : double { return 1.5 <=> 2.5; }
eq() : double { return 2.5 <=> 2.5; }
gt() : double { return 3.5 <=> 2.5; }
)SRC");
    REQUIRE(jit);
    auto lt = jit->lookup_symbol<double(*)()>("_KFN15__ss_prim_dbl__2ltEv");
    auto eq = jit->lookup_symbol<double(*)()>("_KFN15__ss_prim_dbl__2eqEv");
    auto gt = jit->lookup_symbol<double(*)()>("_KFN15__ss_prim_dbl__2gtEv");
    REQUIRE(lt); REQUIRE(eq); REQUIRE(gt);
    CHECK(lt() < 0.0);
    CHECK(eq() == 0.0);
    CHECK(gt() > 0.0);
}

// ═════════════════════════════════════════════════════════════════════════════
// 2. Direct member `<=>` usage and fallback synthesis of all six comparisons
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Member <=> used directly and as fallback for all six comparisons", "[gen][spaceship][member][comparison-fallback]") {
    auto jit = gen_jit(R"SRC(
module __ss_member_all__;
struct Point {
    x: int;
    y: int;
    Point(ax: int, ay: int) : x(ax), y(ay) {}
    const operator <=>(o: Point&) : int {
        if (x != o.x) return x - o.x;
        return y - o.y;
    }
}
direct() : int { a: Point(1, 2); b: Point(1, 5); return a <=> b; }
lt() : bool { a: Point(1, 2); b: Point(1, 5); return a < b; }
gt() : bool { a: Point(1, 2); b: Point(1, 5); return a > b; }
le() : bool { a: Point(1, 2); b: Point(1, 5); return a <= b; }
ge() : bool { a: Point(1, 2); b: Point(1, 5); return a >= b; }
eq_diff() : bool { a: Point(1, 2); b: Point(1, 5); return a == b; }
ne_diff() : bool { a: Point(1, 2); b: Point(1, 5); return a != b; }
eq_same() : bool { a: Point(1, 2); b: Point(1, 2); return a == b; }
ne_same() : bool { a: Point(1, 2); b: Point(1, 2); return a != b; }
le_same() : bool { a: Point(1, 2); b: Point(1, 2); return a <= b; }
ge_same() : bool { a: Point(1, 2); b: Point(1, 2); return a >= b; }
)SRC");
    REQUIRE(jit);
    auto direct = jit->lookup_symbol<int(*)()>("_KFN17__ss_member_all__6directEv");
    auto lt = jit->lookup_symbol<bool(*)()>("_KFN17__ss_member_all__2ltEv");
    auto gt = jit->lookup_symbol<bool(*)()>("_KFN17__ss_member_all__2gtEv");
    auto le = jit->lookup_symbol<bool(*)()>("_KFN17__ss_member_all__2leEv");
    auto ge = jit->lookup_symbol<bool(*)()>("_KFN17__ss_member_all__2geEv");
    auto eq_diff = jit->lookup_symbol<bool(*)()>("_KFN17__ss_member_all__7eq_diffEv");
    auto ne_diff = jit->lookup_symbol<bool(*)()>("_KFN17__ss_member_all__7ne_diffEv");
    auto eq_same = jit->lookup_symbol<bool(*)()>("_KFN17__ss_member_all__7eq_sameEv");
    auto ne_same = jit->lookup_symbol<bool(*)()>("_KFN17__ss_member_all__7ne_sameEv");
    auto le_same = jit->lookup_symbol<bool(*)()>("_KFN17__ss_member_all__7le_sameEv");
    auto ge_same = jit->lookup_symbol<bool(*)()>("_KFN17__ss_member_all__7ge_sameEv");
    REQUIRE(direct); REQUIRE(lt); REQUIRE(gt); REQUIRE(le); REQUIRE(ge);
    REQUIRE(eq_diff); REQUIRE(ne_diff); REQUIRE(eq_same); REQUIRE(ne_same);
    REQUIRE(le_same); REQUIRE(ge_same);

    CHECK(direct() < 0);
    CHECK(lt() == true);
    CHECK(gt() == false);
    CHECK(le() == true);
    CHECK(ge() == false);
    CHECK(eq_diff() == false);
    CHECK(ne_diff() == true);

    CHECK(eq_same() == true);
    CHECK(ne_same() == false);
    CHECK(le_same() == true);
    CHECK(ge_same() == true);
}

// ═════════════════════════════════════════════════════════════════════════════
// 3. Non-member (global) `<=>`
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Non-member (global) <=> used directly and as comparison fallback", "[gen][spaceship][non-member]") {
    auto jit = gen_jit(R"SRC(
module __ss_global__;
struct Pt { x: int; Pt(ax: int) : x(ax) {} }
operator <=>(a: Pt&, b: Pt&) : int { return a.x - b.x; }
direct() : int { a: Pt(3); b: Pt(5); return a <=> b; }
lt_true() : bool { a: Pt(3); b: Pt(5); return a < b; }
lt_false() : bool { a: Pt(5); b: Pt(3); return a < b; }
)SRC");
    REQUIRE(jit);
    auto direct = jit->lookup_symbol<int(*)()>("_KFN13__ss_global__6directEv");
    auto lt_true = jit->lookup_symbol<bool(*)()>("_KFN13__ss_global__7lt_trueEv");
    auto lt_false = jit->lookup_symbol<bool(*)()>("_KFN13__ss_global__8lt_falseEv");
    REQUIRE(direct); REQUIRE(lt_true); REQUIRE(lt_false);
    CHECK(direct() < 0);
    CHECK(lt_true() == true);
    CHECK(lt_false() == false);
}

// ═════════════════════════════════════════════════════════════════════════════
// 4. SPACESHIP_SWAP: <=> declared only on the right operand's aggregate
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Heterogeneous <=> declared on right operand triggers SPACESHIP_SWAP", "[gen][spaceship][comparison-fallback][swap]") {
    auto jit = gen_jit(R"SRC(
module __ss_swap__;
struct A { v: int; A(av: int) : v(av) {} }
struct B {
    v: int;
    B(av: int) : v(av) {}
    const operator <=>(other: A&) : int { return v - other.v; }
}
lt() : bool { a: A(3); b: B(5); return a < b; }
gt() : bool { a: A(3); b: B(5); return a > b; }
le() : bool { a: A(3); b: B(5); return a <= b; }
ge() : bool { a: A(3); b: B(5); return a >= b; }
ne() : bool { a: A(3); b: B(5); return a != b; }
eq_diff() : bool { a: A(3); b: B(5); return a == b; }
eq_same() : bool { a: A(5); b: B(5); return a == b; }
ne_same() : bool { a: A(5); b: B(5); return a != b; }
le_same() : bool { a: A(5); b: B(5); return a <= b; }
ge_same() : bool { a: A(5); b: B(5); return a >= b; }
)SRC");
    REQUIRE(jit);
    auto lt = jit->lookup_symbol<bool(*)()>("_KFN11__ss_swap__2ltEv");
    auto gt = jit->lookup_symbol<bool(*)()>("_KFN11__ss_swap__2gtEv");
    auto le = jit->lookup_symbol<bool(*)()>("_KFN11__ss_swap__2leEv");
    auto ge = jit->lookup_symbol<bool(*)()>("_KFN11__ss_swap__2geEv");
    auto ne = jit->lookup_symbol<bool(*)()>("_KFN11__ss_swap__2neEv");
    auto eq_diff = jit->lookup_symbol<bool(*)()>("_KFN11__ss_swap__7eq_diffEv");
    auto eq_same = jit->lookup_symbol<bool(*)()>("_KFN11__ss_swap__7eq_sameEv");
    auto ne_same = jit->lookup_symbol<bool(*)()>("_KFN11__ss_swap__7ne_sameEv");
    auto le_same = jit->lookup_symbol<bool(*)()>("_KFN11__ss_swap__7le_sameEv");
    auto ge_same = jit->lookup_symbol<bool(*)()>("_KFN11__ss_swap__7ge_sameEv");
    REQUIRE(lt); REQUIRE(gt); REQUIRE(le); REQUIRE(ge); REQUIRE(ne);
    REQUIRE(eq_diff); REQUIRE(eq_same); REQUIRE(ne_same); REQUIRE(le_same); REQUIRE(ge_same);

    CHECK(lt() == true);
    CHECK(gt() == false);
    CHECK(le() == true);
    CHECK(ge() == false);
    CHECK(ne() == true);
    CHECK(eq_diff() == false);

    CHECK(eq_same() == true);
    CHECK(ne_same() == false);
    CHECK(le_same() == true);
    CHECK(ge_same() == true);
}

// ═════════════════════════════════════════════════════════════════════════════
// 5. Priority: exact operator always wins over spaceship fallback
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Exact operator takes precedence over spaceship fallback", "[gen][spaceship][comparison-fallback][priority]") {
    // operator< is deliberately "wrong" (always true) to detect whether the
    // exact DIRECT operator (must win) or the correct SPACESHIP fallback
    // (must NOT be used here) was selected.
    auto jit = gen_jit(R"SRC(
module __ss_prio_direct__;
struct P {
    v: int;
    P(av: int) : v(av) {}
    const operator <(other: P&) : bool { return true; }
    const operator <=>(other: P&) : int { return v - other.v; }
}
lt() : bool { a: P(10); b: P(5); return a < b; }
)SRC");
    REQUIRE(jit);
    auto lt = jit->lookup_symbol<bool(*)()>("_KFN18__ss_prio_direct__2ltEv");
    REQUIRE(lt);
    // Real "<" (10 < 5) is false, but the always-true exact operator< must be
    // the one used (DIRECT beats SPACESHIP), so the result must be true.
    CHECK(lt() == true);
}

TEST_CASE("Spaceship fallback takes precedence over NEGATE/SWAP fallback tiers", "[gen][spaceship][comparison-fallback][priority]") {
    SECTION("SPACESHIP beats NEGATE") {
        // operator== is deliberately "wrong" (always true); if NEGATE (!= via
        // !==) were (incorrectly) preferred over SPACESHIP, the wrong answer
        // would come out for differing values.
        auto jit = gen_jit(R"SRC(
module __ss_prio_negate__;
struct P {
    v: int;
    P(av: int) : v(av) {}
    const operator ==(other: P&) : bool { return true; }
    const operator <=>(other: P&) : int { return v - other.v; }
}
ne() : bool { a: P(3); b: P(5); return a != b; }
)SRC");
        REQUIRE(jit);
        auto ne = jit->lookup_symbol<bool(*)()>("_KFN18__ss_prio_negate__2neEv");
        REQUIRE(ne);
        // Real 3 != 5 is true; NEGATE(always-true ==) would (wrongly) give false.
        CHECK(ne() == true);
    }

    SECTION("SPACESHIP beats SWAP") {
        // operator> is deliberately "wrong" (always true); if SWAP (< via
        // swapped >) were (incorrectly) preferred over SPACESHIP, the wrong
        // answer would come out.
        auto jit = gen_jit(R"SRC(
module __ss_prio_swap__;
struct P {
    v: int;
    P(av: int) : v(av) {}
    const operator >(other: P&) : bool { return true; }
    const operator <=>(other: P&) : int { return v - other.v; }
}
lt() : bool { a: P(5); b: P(3); return a < b; }
)SRC");
        REQUIRE(jit);
        auto lt = jit->lookup_symbol<bool(*)()>("_KFN16__ss_prio_swap__2ltEv");
        REQUIRE(lt);
        // Real 5 < 3 is false; SWAP(always-true >) would (wrongly) give true.
        CHECK(lt() == false);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// 6. Virtual dispatch
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Virtual <=> is dispatched polymorphically through comparison fallback", "[gen][spaceship][virtual]") {
    auto jit = gen_jit(R"SRC(
module __ss_virtual__;
class Base {
    public v: int;
    Base() : v(0) {}
    Base(av: int) : v(av) {}
    const operator <=>(other: Base&) : int { return v - other.v; }
}
class Derived : public Base {
    Derived() : Base(0) {}
    Derived(av: int) : Base(av) {}
    override const operator <=>(other: Base&) : int { return (v - other.v) + 1000; }
}
call_lt(a: Base&, b: Base&) : bool { return a < b; }
test_base() : bool {
    a: Base(3);
    b: Base(5);
    return call_lt(a, b);
}
test_derived() : bool {
    d: Derived(3);
    b: Base(5);
    return call_lt(d, b);
}
)SRC");
    REQUIRE(jit);
    auto test_base = jit->lookup_symbol<bool(*)()>("_KFN14__ss_virtual__9test_baseEv");
    auto test_derived = jit->lookup_symbol<bool(*)()>("_KFN14__ss_virtual__12test_derivedEv");
    REQUIRE(test_base); REQUIRE(test_derived);
    CHECK(test_base() == true);     // 3 - 5 = -2 < 0 -> true
    CHECK(test_derived() == false); // (3 - 5) + 1000 = 998, not < 0 -> false
}

// ═════════════════════════════════════════════════════════════════════════════
// 7. Template `<=>`
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Template struct with member <=>", "[gen][spaceship][template]") {
    auto jit = gen_jit(R"SRC(
module __ss_template__;
template<typename T>
struct Box {
    v: T;
    const operator <=>(other: Box<T>&) : int {
        if (v < other.v) return -1;
        if (v > other.v) return 1;
        return 0;
    }
}
test_lt() : bool {
    a: Box<int>;
    a.v = 3;
    b: Box<int>;
    b.v = 5;
    return a < b;
}
test_eq() : bool {
    a: Box<int>;
    a.v = 5;
    b: Box<int>;
    b.v = 5;
    return a == b;
}
)SRC");
    REQUIRE(jit);
    auto test_lt = jit->lookup_symbol<bool(*)()>("_KFN15__ss_template__7test_ltEv");
    auto test_eq = jit->lookup_symbol<bool(*)()>("_KFN15__ss_template__7test_eqEv");
    REQUIRE(test_lt); REQUIRE(test_eq);
    CHECK(test_lt() == true);
    CHECK(test_eq() == true);
}

// ═════════════════════════════════════════════════════════════════════════════
// 8. Errors: invalid `operator <=>` return type
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Direct use of <=> returning bool is rejected (Phase 1 return-type restriction)", "[gen][spaceship][error]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __ss_err_bad_return_direct__;
        struct P {
            v: int;
            const operator <=>(other: P&) : bool { return v < other.v; }
        }
        test(a: P&, b: P&) : int { return a <=> b; }
    )SRC"), k::log::compiler_error);
}

TEST_CASE("Comparison fallback ignores an <=> with an invalid (bool) return type", "[gen][spaceship][error][comparison-fallback]") {
    // The bool-returning `<=>` is not a valid fallback candidate, so no
    // comparison operator can be synthesized for `<` here: this must fail,
    // not silently (and incorrectly) use the invalid spaceship operator.
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __ss_err_bad_return_fallback__;
        struct P {
            v: int;
            const operator <=>(other: P&) : bool { return v < other.v; }
        }
        test(a: P&, b: P&) : bool { return a < b; }
    )SRC"), k::log::compiler_error);
}

// ═════════════════════════════════════════════════════════════════════════════
// 9. Phase 2: `<=>` returning a non-primitive (aggregate) type comparable to 0
// ═════════════════════════════════════════════════════════════════════════════
//
// Phase 2 allows `operator <=>` to return any aggregate/struct type, as long as —
// only when actually needed as a fallback synthesis source for one of the six
// comparison operators — that aggregate declares a direct, bool-returning
// comparison against the integer literal `0` (probed with a non-reference numeric
// primitive parameter). See try_resolve_spaceship_zero_comparison() in
// gen_operators_overload.cpp and doc/spec/language/functions/operators.md §9.

TEST_CASE("Direct use of <=> returning an aggregate works with no extra validation", "[gen][spaceship][phase2][direct]") {
    auto jit = gen_jit(R"SRC(
module __ss2_direct__;
struct Ordering {
    v: int;
    Ordering(av: int) : v(av) {}
}
struct Point {
    x: int;
    y: int;
    Point(ax: int, ay: int) : x(ax), y(ay) {}
    const operator <=>(o: Point&) : Ordering {
        if (x != o.x) return Ordering(x - o.x);
        return Ordering(y - o.y);
    }
}
test() : int {
    a: Point(1, 2);
    b: Point(1, 5);
    r: Ordering = a <=> b;
    return r.v;
}
)SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("_KFN14__ss2_direct__4testEv");
    REQUIRE(test);
    CHECK(test() < 0); // (1,2) vs (1,5): x equal, y: 2 - 5 = -3
}

TEST_CASE("Aggregate-returning <=> used as fallback source for all six comparisons", "[gen][spaceship][phase2][comparison-fallback]") {
    auto jit = gen_jit(R"SRC(
module __ss2_fallback__;
struct Ordering {
    v: int;
    Ordering(av: int) : v(av) {}
    const operator ==(o: int) : bool { return v == o; }
    const operator !=(o: int) : bool { return v != o; }
    const operator <(o: int) : bool { return v < o; }
    const operator >(o: int) : bool { return v > o; }
    const operator <=(o: int) : bool { return v <= o; }
    const operator >=(o: int) : bool { return v >= o; }
}
struct Point {
    x: int;
    y: int;
    Point(ax: int, ay: int) : x(ax), y(ay) {}
    const operator <=>(o: Point&) : Ordering {
        if (x != o.x) return Ordering(x - o.x);
        return Ordering(y - o.y);
    }
}
lt() : bool { a: Point(1, 2); b: Point(1, 5); return a < b; }
gt() : bool { a: Point(1, 2); b: Point(1, 5); return a > b; }
le() : bool { a: Point(1, 2); b: Point(1, 5); return a <= b; }
ge() : bool { a: Point(1, 2); b: Point(1, 5); return a >= b; }
eq_diff() : bool { a: Point(1, 2); b: Point(1, 5); return a == b; }
ne_diff() : bool { a: Point(1, 2); b: Point(1, 5); return a != b; }
eq_same() : bool { a: Point(1, 2); b: Point(1, 2); return a == b; }
ne_same() : bool { a: Point(1, 2); b: Point(1, 2); return a != b; }
)SRC");
    REQUIRE(jit);
    auto lt = jit->lookup_symbol<bool(*)()>("_KFN16__ss2_fallback__2ltEv");
    auto gt = jit->lookup_symbol<bool(*)()>("_KFN16__ss2_fallback__2gtEv");
    auto le = jit->lookup_symbol<bool(*)()>("_KFN16__ss2_fallback__2leEv");
    auto ge = jit->lookup_symbol<bool(*)()>("_KFN16__ss2_fallback__2geEv");
    auto eq_diff = jit->lookup_symbol<bool(*)()>("_KFN16__ss2_fallback__7eq_diffEv");
    auto ne_diff = jit->lookup_symbol<bool(*)()>("_KFN16__ss2_fallback__7ne_diffEv");
    auto eq_same = jit->lookup_symbol<bool(*)()>("_KFN16__ss2_fallback__7eq_sameEv");
    auto ne_same = jit->lookup_symbol<bool(*)()>("_KFN16__ss2_fallback__7ne_sameEv");
    REQUIRE(lt); REQUIRE(gt); REQUIRE(le); REQUIRE(ge);
    REQUIRE(eq_diff); REQUIRE(ne_diff); REQUIRE(eq_same); REQUIRE(ne_same);

    CHECK(lt() == true);
    CHECK(gt() == false);
    CHECK(le() == true);
    CHECK(ge() == false);
    CHECK(eq_diff() == false);
    CHECK(ne_diff() == true);

    CHECK(eq_same() == true);
    CHECK(ne_same() == false);
}

TEST_CASE("Aggregate-returning <=> declared only on right operand triggers SPACESHIP_SWAP", "[gen][spaceship][phase2][comparison-fallback][swap]") {
    auto jit = gen_jit(R"SRC(
module __ss2_swap__;
struct Ordering {
    v: int;
    Ordering(av: int) : v(av) {}
    const operator <(o: int) : bool { return v < o; }
    const operator >(o: int) : bool { return v > o; }
}
struct A { v: int; A(av: int) : v(av) {} }
struct B {
    v: int;
    B(av: int) : v(av) {}
    const operator <=>(other: A&) : Ordering { return Ordering(v - other.v); }
}
lt() : bool { a: A(3); b: B(5); return a < b; }
gt() : bool { a: A(3); b: B(5); return a > b; }
)SRC");
    REQUIRE(jit);
    auto lt = jit->lookup_symbol<bool(*)()>("_KFN12__ss2_swap__2ltEv");
    auto gt = jit->lookup_symbol<bool(*)()>("_KFN12__ss2_swap__2gtEv");
    REQUIRE(lt); REQUIRE(gt);
    // b's <=> is declared on B: B(5) <=> A(3) via SPACESHIP_SWAP, so wanted `a < b`
    // probes swap_of(<) = `>` against B's Ordering(5 - 3 = 2): 2 > 0 -> true.
    CHECK(lt() == true);
    CHECK(gt() == false);
}

TEST_CASE("Virtual aggregate-returning <=> is dispatched polymorphically through comparison fallback", "[gen][spaceship][phase2][virtual]") {
    auto jit = gen_jit(R"SRC(
module __ss2_virtual__;
struct Ordering {
    v: int;
    Ordering(av: int) : v(av) {}
    const operator <(o: int) : bool { return v < o; }
}
class Base {
    public v: int;
    Base() : v(0) {}
    Base(av: int) : v(av) {}
    const operator <=>(other: Base&) : Ordering { return Ordering(v - other.v); }
}
class Derived : public Base {
    Derived() : Base(0) {}
    Derived(av: int) : Base(av) {}
    override const operator <=>(other: Base&) : Ordering { return Ordering((v - other.v) + 1000); }
}
call_lt(a: Base&, b: Base&) : bool { return a < b; }
test_base() : bool {
    a: Base(3);
    b: Base(5);
    return call_lt(a, b);
}
test_derived() : bool {
    d: Derived(3);
    b: Base(5);
    return call_lt(d, b);
}
)SRC");
    REQUIRE(jit);
    auto test_base = jit->lookup_symbol<bool(*)()>("_KFN15__ss2_virtual__9test_baseEv");
    auto test_derived = jit->lookup_symbol<bool(*)()>("_KFN15__ss2_virtual__12test_derivedEv");
    REQUIRE(test_base); REQUIRE(test_derived);
    CHECK(test_base() == true);     // 3 - 5 = -2 < 0 -> true
    CHECK(test_derived() == false); // (3 - 5) + 1000 = 998, not < 0 -> false
}

TEST_CASE("Aggregate-returning <=> with no viable zero-comparison: direct use OK, fallback fails", "[gen][spaceship][phase2][error]") {
    // Ordering2 declares no comparison operator against int at all: direct use of
    // `<=>` never needs it (the result might never be compared to anything), but
    // synthesizing `<` from it is impossible and must be a compile error.
    auto jit = gen_jit(R"SRC(
module __ss2_err_no_zero_direct__;
struct Ordering2 { v: int; Ordering2(av: int) : v(av) {} }
struct P {
    v: int;
    P(av: int) : v(av) {}
    const operator <=>(o: P&) : Ordering2 { return Ordering2(v - o.v); }
}
direct() : int { a: P(1); b: P(2); r: Ordering2 = a <=> b; return r.v; }
)SRC");
    REQUIRE(jit);
    auto direct = jit->lookup_symbol<int(*)()>("_KFN26__ss2_err_no_zero_direct__6directEv");
    REQUIRE(direct);
    CHECK(direct() < 0); // 1 - 2 = -1

    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __ss2_err_no_zero_fallback__;
        struct Ordering2 { v: int; Ordering2(av: int) : v(av) {} }
        struct P {
            v: int;
            P(av: int) : v(av) {}
            const operator <=>(o: P&) : Ordering2 { return Ordering2(v - o.v); }
        }
        test(a: P&, b: P&) : bool { return a < b; }
    )SRC"), k::log::compiler_error);
}

TEST_CASE("Aggregate-returning <=> whose zero-comparison operator takes a reference parameter is rejected", "[gen][spaceship][phase2][error]") {
    // Ordering3's `operator <` takes `int&` (a reference), which is outside the
    // deliberately restricted scope of try_resolve_spaceship_zero_comparison()
    // (by-value numeric parameter only, see docs) — so no fallback candidate is
    // viable and this must be a compile error, not a silent miscompile.
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __ss2_err_ref_param__;
        struct Ordering3 {
            v: int;
            Ordering3(av: int) : v(av) {}
            const operator <(o: int&) : bool { return v < o; }
        }
        struct P {
            v: int;
            P(av: int) : v(av) {}
            const operator <=>(o: P&) : Ordering3 { return Ordering3(v - o.v); }
        }
        test(a: P&, b: P&) : bool { return a < b; }
    )SRC"), k::log::compiler_error);
}
