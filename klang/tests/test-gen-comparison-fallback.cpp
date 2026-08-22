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
 * Tests for comparison operator fallback / synthesis.
 *
 * When an aggregate (struct/class/interface) does not declare the exact
 * comparison operator used in an expression, the compiler synthesizes it
 * from another declared comparison operator via boolean algebra:
 *
 *   - NEGATE:       wanted = !source            (== <-> !=, < <-> >=, > <-> <=)
 *   - SWAP:         wanted = source(b, a)        (== <-> ==, != <-> !=, < <-> >, <= <-> >=)
 *   - SWAP_NEGATE:  wanted = !source(b, a)        (< <-> <=, > <-> >=)
 *   - COMPOSITE:    wanted (==/!=) synthesized from TWO calls to a single
 *                   relational operator (</>/<=/>=), each operand evaluated
 *                   exactly once.
 *
 * Selection order is lexicographic on (cast_weight, tier): the least amount
 * of type relaxation wins first; synthesis complexity only breaks ties.
 *
 * See doc/spec/language/functions/operators.md for the full specification.
 */

#include "helpers.hpp"
#include <catch2/catch_all.hpp>

// ═════════════════════════════════════════════════════════════════════════════
// 1. Single-source synthesis: NEGATE (equality group)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Synthesize != from == via NEGATE", "[gen][operator][comparison-fallback]") {
    auto jit = gen_jit(R"SRC(
module gen_comparison_fallback_01;
struct Box {
    v: int;
    Box(av: int) : v(av) {}
    operator ==(other: Box&) : bool { return v == other.v; }
}
test_same() : bool { a: Box(5); b: Box(5); return a != b; }
test_diff() : bool { a: Box(5); b: Box(6); return a != b; }
)SRC");
    REQUIRE(jit);
    auto test_same = jit->lookup_symbol<bool(*)()>("_KFN26gen_comparison_fallback_019test_sameEv");
    auto test_diff = jit->lookup_symbol<bool(*)()>("_KFN26gen_comparison_fallback_019test_diffEv");
    REQUIRE(test_same); REQUIRE(test_diff);
    CHECK(test_same() == false);
    CHECK(test_diff() == true);
}

TEST_CASE("Synthesize == from != via NEGATE", "[gen][operator][comparison-fallback]") {
    auto jit = gen_jit(R"SRC(
module gen_comparison_fallback_02;
struct Box {
    v: int;
    Box(av: int) : v(av) {}
    operator !=(other: Box&) : bool { return v != other.v; }
}
test_same() : bool { a: Box(5); b: Box(5); return a == b; }
test_diff() : bool { a: Box(5); b: Box(6); return a == b; }
)SRC");
    REQUIRE(jit);
    auto test_same = jit->lookup_symbol<bool(*)()>("_KFN26gen_comparison_fallback_029test_sameEv");
    auto test_diff = jit->lookup_symbol<bool(*)()>("_KFN26gen_comparison_fallback_029test_diffEv");
    REQUIRE(test_same); REQUIRE(test_diff);
    CHECK(test_same() == true);
    CHECK(test_diff() == false);
}

// ═════════════════════════════════════════════════════════════════════════════
// 2. Single-source synthesis: relational group, one operator declared
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Synthesize >, <=, >= from < via SWAP/SWAP_NEGATE/NEGATE", "[gen][operator][comparison-fallback]") {
    auto jit = gen_jit(R"SRC(
module gen_comparison_fallback_03;
struct Ord {
    v: int;
    Ord(av: int) : v(av) {}
    operator <(other: Ord&) : bool { return v < other.v; }
}
test_gt_true()  : bool { a: Ord(5); b: Ord(3); return a > b; }
test_gt_false() : bool { a: Ord(3); b: Ord(5); return a > b; }
test_le_true()  : bool { a: Ord(3); b: Ord(3); return a <= b; }
test_le_false() : bool { a: Ord(5); b: Ord(3); return a <= b; }
test_ge_true()  : bool { a: Ord(3); b: Ord(3); return a >= b; }
test_ge_false() : bool { a: Ord(3); b: Ord(5); return a >= b; }
)SRC");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<bool(*)()>("_KFN26gen_comparison_fallback_0312test_gt_trueEv")());
    CHECK_FALSE(jit->lookup_symbol<bool(*)()>("_KFN26gen_comparison_fallback_0313test_gt_falseEv")());
    CHECK(jit->lookup_symbol<bool(*)()>("_KFN26gen_comparison_fallback_0312test_le_trueEv")());
    CHECK_FALSE(jit->lookup_symbol<bool(*)()>("_KFN26gen_comparison_fallback_0313test_le_falseEv")());
    CHECK(jit->lookup_symbol<bool(*)()>("_KFN26gen_comparison_fallback_0312test_ge_trueEv")());
    CHECK_FALSE(jit->lookup_symbol<bool(*)()>("_KFN26gen_comparison_fallback_0313test_ge_falseEv")());
}

TEST_CASE("Synthesize <, <=, >= from > via SWAP/NEGATE/SWAP_NEGATE", "[gen][operator][comparison-fallback]") {
    auto jit = gen_jit(R"SRC(
module gen_comparison_fallback_04;
struct Ord {
    v: int;
    Ord(av: int) : v(av) {}
    operator >(other: Ord&) : bool { return v > other.v; }
}
test_lt_true()  : bool { a: Ord(3); b: Ord(5); return a < b; }
test_lt_false() : bool { a: Ord(5); b: Ord(3); return a < b; }
test_le_true()  : bool { a: Ord(3); b: Ord(3); return a <= b; }
test_le_false() : bool { a: Ord(5); b: Ord(3); return a <= b; }
test_ge_true()  : bool { a: Ord(3); b: Ord(3); return a >= b; }
test_ge_false() : bool { a: Ord(3); b: Ord(5); return a >= b; }
)SRC");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<bool(*)()>("_KFN26gen_comparison_fallback_0412test_lt_trueEv")());
    CHECK_FALSE(jit->lookup_symbol<bool(*)()>("_KFN26gen_comparison_fallback_0413test_lt_falseEv")());
    CHECK(jit->lookup_symbol<bool(*)()>("_KFN26gen_comparison_fallback_0412test_le_trueEv")());
    CHECK_FALSE(jit->lookup_symbol<bool(*)()>("_KFN26gen_comparison_fallback_0413test_le_falseEv")());
    CHECK(jit->lookup_symbol<bool(*)()>("_KFN26gen_comparison_fallback_0412test_ge_trueEv")());
    CHECK_FALSE(jit->lookup_symbol<bool(*)()>("_KFN26gen_comparison_fallback_0413test_ge_falseEv")());
}

TEST_CASE("Synthesize <, >, >= from <= via NEGATE/SWAP/SWAP_NEGATE", "[gen][operator][comparison-fallback]") {
    auto jit = gen_jit(R"SRC(
module gen_comparison_fallback_05;
struct Ord {
    v: int;
    Ord(av: int) : v(av) {}
    operator <=(other: Ord&) : bool { return v <= other.v; }
}
test_lt_true()  : bool { a: Ord(3); b: Ord(5); return a < b; }
test_lt_false() : bool { a: Ord(3); b: Ord(3); return a < b; }
test_gt_true()  : bool { a: Ord(5); b: Ord(3); return a > b; }
test_gt_false() : bool { a: Ord(3); b: Ord(3); return a > b; }
test_ge_true()  : bool { a: Ord(3); b: Ord(3); return a >= b; }
test_ge_false() : bool { a: Ord(3); b: Ord(5); return a >= b; }
)SRC");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<bool(*)()>("_KFN26gen_comparison_fallback_0512test_lt_trueEv")());
    CHECK_FALSE(jit->lookup_symbol<bool(*)()>("_KFN26gen_comparison_fallback_0513test_lt_falseEv")());
    CHECK(jit->lookup_symbol<bool(*)()>("_KFN26gen_comparison_fallback_0512test_gt_trueEv")());
    CHECK_FALSE(jit->lookup_symbol<bool(*)()>("_KFN26gen_comparison_fallback_0513test_gt_falseEv")());
    CHECK(jit->lookup_symbol<bool(*)()>("_KFN26gen_comparison_fallback_0512test_ge_trueEv")());
    CHECK_FALSE(jit->lookup_symbol<bool(*)()>("_KFN26gen_comparison_fallback_0513test_ge_falseEv")());
}

TEST_CASE("Synthesize <, >, <= from >= via NEGATE/SWAP/SWAP_NEGATE", "[gen][operator][comparison-fallback]") {
    auto jit = gen_jit(R"SRC(
module gen_comparison_fallback_06;
struct Ord {
    v: int;
    Ord(av: int) : v(av) {}
    operator >=(other: Ord&) : bool { return v >= other.v; }
}
test_lt_true()  : bool { a: Ord(3); b: Ord(5); return a < b; }
test_lt_false() : bool { a: Ord(3); b: Ord(3); return a < b; }
test_gt_true()  : bool { a: Ord(5); b: Ord(3); return a > b; }
test_gt_false() : bool { a: Ord(3); b: Ord(3); return a > b; }
test_le_true()  : bool { a: Ord(3); b: Ord(3); return a <= b; }
test_le_false() : bool { a: Ord(5); b: Ord(3); return a <= b; }
)SRC");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<bool(*)()>("_KFN26gen_comparison_fallback_0612test_lt_trueEv")());
    CHECK_FALSE(jit->lookup_symbol<bool(*)()>("_KFN26gen_comparison_fallback_0613test_lt_falseEv")());
    CHECK(jit->lookup_symbol<bool(*)()>("_KFN26gen_comparison_fallback_0612test_gt_trueEv")());
    CHECK_FALSE(jit->lookup_symbol<bool(*)()>("_KFN26gen_comparison_fallback_0613test_gt_falseEv")());
    CHECK(jit->lookup_symbol<bool(*)()>("_KFN26gen_comparison_fallback_0612test_le_trueEv")());
    CHECK_FALSE(jit->lookup_symbol<bool(*)()>("_KFN26gen_comparison_fallback_0613test_le_falseEv")());
}

// ═════════════════════════════════════════════════════════════════════════════
// 3. Member vs non-member source operator
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Synthesize from a non-member source comparison operator", "[gen][operator][comparison-fallback]") {
    auto jit = gen_jit(R"SRC(
module gen_comparison_fallback_07;
struct Pt {
    x: int;
    Pt(ax: int) : x(ax) {}
}
operator <(a: Pt&, b: Pt&) : bool { return a.x < b.x; }
test_gt_true()  : bool { a: Pt(5); b: Pt(3); return a > b; }
test_gt_false() : bool { a: Pt(3); b: Pt(5); return a > b; }
)SRC");
    REQUIRE(jit);
    auto gt_true = jit->lookup_symbol<bool(*)()>("_KFN26gen_comparison_fallback_0712test_gt_trueEv");
    auto gt_false = jit->lookup_symbol<bool(*)()>("_KFN26gen_comparison_fallback_0713test_gt_falseEv");
    REQUIRE(gt_true); REQUIRE(gt_false);
    CHECK(gt_true() == true);
    CHECK(gt_false() == false);
}

// ═════════════════════════════════════════════════════════════════════════════
// 4. Const-correctness
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Synthesize from a const member source operator", "[gen][operator][comparison-fallback][const]") {
    auto jit = gen_jit(R"SRC(
module gen_comparison_fallback_08;
struct Box {
    v: int;
    Box(av: int) : v(av) {}
    const operator ==(other: const Box&) : bool { return v == other.v; }
}
check(a: const Box&, b: const Box&) : bool { return a != b; }
test_same() : bool { a: Box(5); b: Box(5); return check(a, b); }
test_diff() : bool { a: Box(5); b: Box(6); return check(a, b); }
)SRC");
    REQUIRE(jit);
    auto test_same = jit->lookup_symbol<bool(*)()>("_KFN26gen_comparison_fallback_089test_sameEv");
    auto test_diff = jit->lookup_symbol<bool(*)()>("_KFN26gen_comparison_fallback_089test_diffEv");
    REQUIRE(test_same); REQUIRE(test_diff);
    CHECK(test_same() == false);
    CHECK(test_diff() == true);
}

TEST_CASE("Mutable-only source operator on const object is rejected (no fallback)", "[gen][operator][comparison-fallback][const][error]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module gen_comparison_fallback_09;
        struct Box {
            v: int;
            Box(av: int) : v(av) {}
            operator ==(other: Box&) : bool { return v == other.v; }
        }
        check(a: const Box&, b: const Box&) : bool { return a != b; }
    )SRC"), k::log::compiler_error);
}

// ═════════════════════════════════════════════════════════════════════════════
// 5. Priority: type relaxation (cast_weight) beats synthesis tier
// ═════════════════════════════════════════════════════════════════════════════

// operator!=(long) requires a widening conversion from an int argument
// (cast_weight > 0), while operator==(int) matches exactly and can be
// negated to synthesize != at cast_weight == 0. The exact-type synthesis
// must win over the named-but-relaxed exact operator.
TEST_CASE("Exact-type synthesis wins over relaxed-type exact operator", "[gen][operator][comparison-fallback][priority]") {
    auto jit = gen_jit(R"SRC(
module gen_comparison_fallback_10;
struct Vec {
    v: int;
    Vec(av: int) : v(av) {}
    operator ==(n: int) : bool { return v == n; }
    operator !=(n: long) : bool { return true; }
}
test_same() : bool { a: Vec(5); return a != 5; }
test_diff() : bool { a: Vec(5); return a != 6; }
)SRC");
    REQUIRE(jit);
    auto test_same = jit->lookup_symbol<bool(*)()>("_KFN26gen_comparison_fallback_109test_sameEv");
    auto test_diff = jit->lookup_symbol<bool(*)()>("_KFN26gen_comparison_fallback_109test_diffEv");
    REQUIRE(test_same); REQUIRE(test_diff);
    // If the (cast_weight=0, tier=NEGATE) synthesis is correctly preferred over
    // the (cast_weight>0, tier=DIRECT) exact `!=(long)` (which always returns
    // true), test_same() must be false.
    CHECK(test_same() == false);
    CHECK(test_diff() == true);
}

// ═════════════════════════════════════════════════════════════════════════════
// 6. Inheritance: source operator declared in a base class
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Synthesize using a comparison operator inherited from a base class", "[gen][operator][comparison-fallback][class]") {
    auto jit = gen_jit(R"SRC(
module gen_comparison_fallback_11;
class Base {
    v: int;
    Base() : v(0) {}
    Base(av: int) : v(av) {}
    operator <(other: Base&) : bool { return v < other.v; }
}
class Derived : public Base {
    Derived() : Base() {}
    Derived(av: int) : Base(av) {}
}
test_gt_true()  : bool { a: Derived(5); b: Derived(3); return a > b; }
test_gt_false() : bool { a: Derived(3); b: Derived(5); return a > b; }
)SRC");
    REQUIRE(jit);
    auto gt_true = jit->lookup_symbol<bool(*)()>("_KFN26gen_comparison_fallback_1112test_gt_trueEv");
    auto gt_false = jit->lookup_symbol<bool(*)()>("_KFN26gen_comparison_fallback_1113test_gt_falseEv");
    REQUIRE(gt_true); REQUIRE(gt_false);
    CHECK(gt_true() == true);
    CHECK(gt_false() == false);
}

// ═════════════════════════════════════════════════════════════════════════════
// 7. Composite tier: == / != synthesized from a single relational operator,
//    called twice (both orders), with operands evaluated exactly once.
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Synthesize == and != from < only (COMPOSITE)", "[gen][operator][comparison-fallback][composite]") {
    auto jit = gen_jit(R"SRC(
module gen_comparison_fallback_12;
struct Ord {
    v: int;
    Ord(av: int) : v(av) {}
    operator <(other: Ord&) : bool { return v < other.v; }
}
test_eq_true()  : bool { a: Ord(5); b: Ord(5); return a == b; }
test_eq_false() : bool { a: Ord(5); b: Ord(6); return a == b; }
test_ne_true()  : bool { a: Ord(5); b: Ord(6); return a != b; }
test_ne_false() : bool { a: Ord(5); b: Ord(5); return a != b; }
)SRC");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<bool(*)()>("_KFN26gen_comparison_fallback_1212test_eq_trueEv")());
    CHECK_FALSE(jit->lookup_symbol<bool(*)()>("_KFN26gen_comparison_fallback_1213test_eq_falseEv")());
    CHECK(jit->lookup_symbol<bool(*)()>("_KFN26gen_comparison_fallback_1212test_ne_trueEv")());
    CHECK_FALSE(jit->lookup_symbol<bool(*)()>("_KFN26gen_comparison_fallback_1213test_ne_falseEv")());
}

TEST_CASE("Composite synthesis calls the base operator exactly twice, no operand re-evaluation", "[gen][operator][comparison-fallback][composite]") {
    auto jit = gen_jit(R"SRC(
module gen_comparison_fallback_13;
op_calls : int;
ctor_calls : int;
struct Ord {
    v: int;
    Ord(av: int) : v(av) { ++ctor_calls; }
    operator <(other: Ord&) : bool {
        ++op_calls;
        return v < other.v;
    }
}
get_op_calls() : int { return op_calls; }
get_ctor_calls() : int { return ctor_calls; }
test_eq() : bool { a: Ord(3); b: Ord(3); return a == b; }
test_ne() : bool { a: Ord(3); b: Ord(4); return a != b; }
)SRC");
    REQUIRE(jit);
    auto test_eq = jit->lookup_symbol<bool(*)()>("_KFN26gen_comparison_fallback_137test_eqEv");
    auto test_ne = jit->lookup_symbol<bool(*)()>("_KFN26gen_comparison_fallback_137test_neEv");
    auto get_op_calls = jit->lookup_symbol<int(*)()>("get_op_calls");
    auto get_ctor_calls = jit->lookup_symbol<int(*)()>("get_ctor_calls");
    REQUIRE(test_eq); REQUIRE(test_ne); REQUIRE(get_op_calls); REQUIRE(get_ctor_calls);

    CHECK(test_eq() == true);
    // Both operands constructed exactly once each (no re-evaluation of the
    // operand sub-expressions), and the base operator< called exactly twice
    // (once per operand order) to synthesize ==.
    CHECK(get_ctor_calls() == 2);
    CHECK(get_op_calls() == 2);

    CHECK(test_ne() == true);
    CHECK(get_ctor_calls() == 4); // two more operands constructed, still once each
    CHECK(get_op_calls() == 4);   // two more operator< calls for the != composite
}

// ═════════════════════════════════════════════════════════════════════════════
// 8. Return-type guard: a non-bool-returning operator cannot be used as a
//    synthesis source (only as a DIRECT, exact-name match).
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Non-bool-returning operator is not used as a synthesis source", "[gen][operator][comparison-fallback][error]") {
    // operator==() returns int, not bool: it can still be called directly as
    // `==`, but must not be used to synthesize `!=` (no bool-returning source
    // exists), so this must fail to compile.
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module gen_comparison_fallback_14;
        struct Box {
            v: int;
            Box(av: int) : v(av) {}
            operator ==(other: Box&) : int { return v == other.v; }
        }
        test() : bool {
            a: Box(5);
            b: Box(5);
            return a != b;
        }
    )SRC"), k::log::compiler_error);
}

TEST_CASE("Non-bool-returning operator still usable directly by exact name", "[gen][operator][comparison-fallback]") {
    auto jit = gen_jit(R"SRC(
module gen_comparison_fallback_15;
struct Box {
    v: int;
    Box(av: int) : v(av) {}
    operator ==(other: Box&) : int { return v == other.v; }
}
test() : int { a: Box(5); b: Box(5); return a == b; }
)SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("_KFN26gen_comparison_fallback_154testEv");
    REQUIRE(test);
    CHECK(test() == 1);
}

// ═════════════════════════════════════════════════════════════════════════════
// 9. Negative: no comparison operator at all → clear compile error
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("No comparison operator declared at all yields a compile error", "[gen][operator][comparison-fallback][error]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module gen_comparison_fallback_16;
        struct Plain {
            v: int;
            Plain(av: int) : v(av) {}
        }
        test() : bool {
            a: Plain(5);
            b: Plain(5);
            return a == b;
        }
    )SRC"), k::log::compiler_error);
}

// ═════════════════════════════════════════════════════════════════════════════
// 10. End-to-end sanity check via full compile + link + run pipeline
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Comparison fallback end-to-end via build_and_exec", "[gen][operator][comparison-fallback][e2e]") {
    auto result = build_and_exec(R"SRC(
module gen_comparison_fallback_17;
struct Ord {
    v: int;
    Ord(av: int) : v(av) {}
    operator <(other: Ord&) : bool { return v < other.v; }
}
main() : int {
    a: Ord(3);
    b: Ord(5);
    r : int = 0;
    if (a < b) ++r;       // DIRECT
    if (a != b) r += 2;      // COMPOSITE
    if (b > a) r += 4;       // SWAP
    if (a <= b) r += 8;      // SWAP_NEGATE
    if (!(b <= a)) r += 16;  // SWAP_NEGATE, false case
    if (!(a >= b)) r += 32;  // NEGATE, false case
    return r;
}
)SRC");
    CHECK(result.exit_code == (1 + 2 + 4 + 8 + 16 + 32));
}
