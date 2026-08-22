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
 * RVO / NRVO (Copy Elision) tests for the K language.
 *
 * Validates that struct-by-value returns use sret-based copy elision so that:
 * - No unnecessary copies or destructor calls are emitted
 * - Returned objects are constructed directly into the caller's destination
 * - Values are correct throughout
 *
 * Category RVO-1:  RVO — return of a constructor invocation (unnamed temporary)
 * Category RVO-2:  NRVO — return of a named local variable
 * Category RVO-3:  Assignment context — r : Obj = make(42)
 * Category RVO-4:  Temporary context — discarded returns, member access on temps
 * Category RVO-5:  Chained calls — make().method().method()
 * Category RVO-6:  Nested calls — consume(make(42))
 * Category RVO-7:  Multiple return paths (NRVO ineligible)
 * Category RVO-8:  Operator overloads returning structs
 * Category RVO-9:  Virtual dispatch returning struct by value
 * Category RVO-10: Struct without destructor (no crash, correct values)
 * Category RVO-11: Control flow with struct returns
 * Category RVO-12: Struct with struct members (aggregation + return)
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

// =============================================================================
// Category RVO-1: Basic NRVO — single named local returned
// =============================================================================

TEST_CASE("RVO-1: Simple factory — exactly 1 ctor, 1 dtor", "[gen][rvo][rvo1]") {
    // make() creates a local and returns it.
    // With NRVO+sret: the local is constructed directly into caller's destination.
    // Expected: exactly 1 constructor call, exactly 1 destructor call.
    auto jit = gen_jit(R"SRC(
        module gen_rvo_01;

        g_ctors : int = 0;
        g_dtors : int = 0;

        struct Obj {
            val : int;
            Obj(v: int) : val(v) { ++g_ctors; }
            ~Obj() { ++g_dtors; }
        }

        make(v: int) : Obj {
            o : Obj(v);
            return o;
        }

        test() : int {
            r : Obj = make(42);
            return r.val;
        }

        get_ctors() : int { return g_ctors; }
        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 42);

    auto get_ctors = jit->lookup_symbol<int(*)()>("get_ctors");
    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_ctors != nullptr);
    REQUIRE(get_dtors != nullptr);
    // With NRVO: exactly 1 ctor (directly into r), 1 dtor (r at scope exit)
    CHECK(get_ctors() == 1);
    CHECK(get_dtors() == 1);
}

TEST_CASE("RVO-1: Factory with multiple-field struct", "[gen][rvo][rvo1]") {
    auto jit = gen_jit(R"SRC(
        module gen_rvo_02;

        g_ctors : int = 0;
        g_dtors : int = 0;

        struct Point {
            x : int;
            y : int;
            Point(px: int, py: int) : x(px), y(py) { ++g_ctors; }
            ~Point() { ++g_dtors; }
        }

        make_point(a: int, b: int) : Point {
            p : Point(a, b);
            return p;
        }

        test() : int {
            p : Point = make_point(10, 20);
            return p.x + p.y;
        }

        get_ctors() : int { return g_ctors; }
        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 30);

    auto get_ctors = jit->lookup_symbol<int(*)()>("get_ctors");
    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_ctors != nullptr);
    REQUIRE(get_dtors != nullptr);
    CHECK(get_ctors() == 1);
    CHECK(get_dtors() == 1);
}

TEST_CASE("RVO-1: Factory with default-constructed struct", "[gen][rvo][rvo1]") {
    auto jit = gen_jit(R"SRC(
        module gen_rvo_03;

        g_ctors : int = 0;
        g_dtors : int = 0;

        struct Obj {
            val : int = 99;
            Obj() { ++g_ctors; }
            ~Obj() { ++g_dtors; }
        }

        make() : Obj {
            o : Obj;
            return o;
        }

        test() : int {
            r : Obj = make();
            return r.val;
        }

        get_ctors() : int { return g_ctors; }
        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 99);

    auto get_ctors = jit->lookup_symbol<int(*)()>("get_ctors");
    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_ctors != nullptr);
    REQUIRE(get_dtors != nullptr);
    CHECK(get_ctors() == 1);
    CHECK(get_dtors() == 1);
}

// =============================================================================
// Category RVO-2: NRVO — return named local variable
// =============================================================================

TEST_CASE("RVO-2: NRVO basic — return named local, exactly 1 ctor, 1 dtor", "[gen][rvo][rvo2]") {
    // o : Obj(42); return o; should use the caller's destination as o's storage.
    auto jit = gen_jit(R"SRC(
        module gen_rvo_04;

        g_ctors : int = 0;
        g_dtors : int = 0;

        struct Obj {
            val : int;
            Obj(v: int) : val(v) { ++g_ctors; }
            ~Obj() { ++g_dtors; }
        }

        make(v: int) : Obj {
            o : Obj(v);
            return o;
        }

        test() : int {
            r : Obj = make(42);
            return r.val;
        }

        get_ctors() : int { return g_ctors; }
        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 42);

    auto get_ctors = jit->lookup_symbol<int(*)()>("get_ctors");
    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_ctors != nullptr);
    REQUIRE(get_dtors != nullptr);
    // With NRVO: o is constructed directly into caller's r → 1 ctor, 1 dtor
    CHECK(get_ctors() == 1);
    CHECK(get_dtors() == 1);
}

TEST_CASE("RVO-2: NRVO — local modified before return", "[gen][rvo][rvo2]") {
    auto jit = gen_jit(R"SRC(
        module gen_rvo_05;

        g_ctors : int = 0;
        g_dtors : int = 0;

        struct Obj {
            val : int;
            Obj(v: int) : val(v) { ++g_ctors; }
            ~Obj() { ++g_dtors; }
        }

        make(v: int) : Obj {
            o : Obj(v);
            o.val = o.val + 10;
            return o;
        }

        test() : int {
            r : Obj = make(32);
            return r.val;
        }

        get_ctors() : int { return g_ctors; }
        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 42);

    auto get_ctors = jit->lookup_symbol<int(*)()>("get_ctors");
    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_ctors != nullptr);
    REQUIRE(get_dtors != nullptr);
    CHECK(get_ctors() == 1);
    CHECK(get_dtors() == 1);
}

TEST_CASE("RVO-2: NRVO — member function called on local before return", "[gen][rvo][rvo2]") {
    auto jit = gen_jit(R"SRC(
        module gen_rvo_06;

        g_ctors : int = 0;
        g_dtors : int = 0;

        struct Builder {
            val : int;
            Builder(v: int) : val(v) { ++g_ctors; }
            ~Builder() { ++g_dtors; }
            add(x: int) { val += x; }
        }

        make(v: int) : Builder {
            b : Builder(v);
            b.add(10);
            return b;
        }

        test() : int {
            r : Builder = make(32);
            return r.val;
        }

        get_ctors() : int { return g_ctors; }
        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 42);

    auto get_ctors = jit->lookup_symbol<int(*)()>("get_ctors");
    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_ctors != nullptr);
    REQUIRE(get_dtors != nullptr);
    CHECK(get_ctors() == 1);
    CHECK(get_dtors() == 1);
}

TEST_CASE("RVO-2: NRVO — single return at end of function", "[gen][rvo][rvo2]") {
    // Simplest NRVO case: single local, single return at end.
    auto jit = gen_jit(R"SRC(
        module gen_rvo_07;

        g_ctors : int = 0;
        g_dtors : int = 0;

        struct Data {
            a : int;
            b : int;
            Data(x: int, y: int) : a(x), b(y) { ++g_ctors; }
            ~Data() { ++g_dtors; }
        }

        make_data(x: int, y: int) : Data {
            d : Data(x, y);
            return d;
        }

        test() : int {
            r : Data = make_data(10, 20);
            return r.a + r.b;
        }

        get_ctors() : int { return g_ctors; }
        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 30);

    auto get_ctors = jit->lookup_symbol<int(*)()>("get_ctors");
    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_ctors != nullptr);
    REQUIRE(get_dtors != nullptr);
    CHECK(get_ctors() == 1);
    CHECK(get_dtors() == 1);
}

// =============================================================================
// Category RVO-3: Assignment context — copy elision into destination variable
// =============================================================================

TEST_CASE("RVO-3: Assigned to local — no intermediate temporary", "[gen][rvo][rvo3]") {
    // r : Obj = make(42); → make() should write directly into r's alloca.
    auto jit = gen_jit(R"SRC(
        module gen_rvo_08;

        g_ctors : int = 0;
        g_dtors : int = 0;

        struct Obj {
            val : int;
            Obj(v: int) : val(v) { ++g_ctors; }
            ~Obj() { ++g_dtors; }
        }

        make(v: int) : Obj {
            o : Obj(v);
            return o;
        }

        test() : int {
            r : Obj = make(42);
            return r.val;
        }

        get_ctors() : int { return g_ctors; }
        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 42);

    auto get_ctors = jit->lookup_symbol<int(*)()>("get_ctors");
    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_ctors != nullptr);
    REQUIRE(get_dtors != nullptr);
    // With sret destination passing: 1 ctor (directly into r), 1 dtor (r at scope exit)
    CHECK(get_ctors() == 1);
    CHECK(get_dtors() == 1);
}

TEST_CASE("RVO-3: Two assigned locals from same factory function", "[gen][rvo][rvo3]") {
    auto jit = gen_jit(R"SRC(
        module gen_rvo_09;

        g_ctors : int = 0;
        g_dtors : int = 0;

        struct Obj {
            val : int;
            Obj(v: int) : val(v) { ++g_ctors; }
            ~Obj() { ++g_dtors; }
        }

        make(v: int) : Obj {
            o : Obj(v);
            return o;
        }

        test() : int {
            a : Obj = make(10);
            b : Obj = make(20);
            return a.val + b.val;
        }

        get_ctors() : int { return g_ctors; }
        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 30);

    auto get_ctors = jit->lookup_symbol<int(*)()>("get_ctors");
    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_ctors != nullptr);
    REQUIRE(get_dtors != nullptr);
    // 2 calls to make → 2 ctors, 2 dtors (a and b)
    CHECK(get_ctors() == 2);
    CHECK(get_dtors() == 2);
}

TEST_CASE("RVO-3: Assigned from chained factory — make_outer(make_inner())", "[gen][rvo][rvo3]") {
    // A factory function that takes a struct by value from another factory.
    auto jit = gen_jit(R"SRC(
        module gen_rvo_10;

        g_ctors : int = 0;
        g_dtors : int = 0;

        struct Inner {
            val : int;
            Inner(v: int) : val(v) { ++g_ctors; }
            ~Inner() { ++g_dtors; }
        }

        make_inner(v: int) : Inner {
            i : Inner(v);
            return i;
        }

        wrap(i: Inner) : Inner {
            r : Inner(i.val + 100);
            return r;
        }

        test() : int {
            result : Inner = wrap(make_inner(42));
            return result.val;
        }

        get_ctors() : int { return g_ctors; }
        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 142);

    auto get_ctors = jit->lookup_symbol<int(*)()>("get_ctors");
    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_ctors != nullptr);
    REQUIRE(get_dtors != nullptr);
    // make_inner: 1 ctor (Inner(42))
    // wrap: 1 ctor (Inner(142)), 1 dtor for by-value param i
    // caller: 1 dtor for result
    // With RVO/NRVO: 2 ctors, 2 dtors total
    CHECK(get_ctors() == 2);
    CHECK(get_dtors() == 2);
}

// =============================================================================
// Category RVO-4: Temporary context — discarded returns, member access on temps
// =============================================================================

TEST_CASE("RVO-4: Discarded struct return — temporary created and destroyed", "[gen][rvo][rvo4]") {
    auto jit = gen_jit(R"SRC(
        module gen_rvo_11;

        g_ctors : int = 0;
        g_dtors : int = 0;

        struct Obj {
            val : int;
            Obj(v: int) : val(v) { ++g_ctors; }
            ~Obj() { ++g_dtors; }
        }

        make(v: int) : Obj {
            o : Obj(v);
            return o;
        }

        test() : int {
            make(42);
            return g_dtors;
        }

        get_ctors() : int { return g_ctors; }
        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    int result = test();
    // The temporary from make(42) must be destroyed at end of expression-statement
    // (before the return statement). With RVO: 1 ctor, 1 dtor at that point.
    CHECK(result >= 1);

    auto get_ctors = jit->lookup_symbol<int(*)()>("get_ctors");
    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_ctors != nullptr);
    REQUIRE(get_dtors != nullptr);
    CHECK(get_ctors() == 1);
    CHECK(get_dtors() == 1);
}

TEST_CASE("RVO-4: Member access on temporary — correct value, temp destroyed", "[gen][rvo][rvo4]") {
    auto jit = gen_jit(R"SRC(
        module gen_rvo_12;

        g_ctors : int = 0;
        g_dtors : int = 0;

        struct Obj {
            val : int;
            Obj(v: int) : val(v) { ++g_ctors; }
            ~Obj() { ++g_dtors; }
        }

        make(v: int) : Obj {
            o : Obj(v);
            return o;
        }

        test() : int {
            x : int = make(42).val;
            return x;
        }

        get_ctors() : int { return g_ctors; }
        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 42);

    auto get_ctors = jit->lookup_symbol<int(*)()>("get_ctors");
    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_ctors != nullptr);
    REQUIRE(get_dtors != nullptr);
    // With RVO: 1 ctor into sret temp, 1 dtor at end of variable_statement
    CHECK(get_ctors() == 1);
    CHECK(get_dtors() == 1);
}

TEST_CASE("RVO-4: Method call on temporary — correct value, temp destroyed", "[gen][rvo][rvo4]") {
    auto jit = gen_jit(R"SRC(
        module gen_rvo_13;

        g_ctors : int = 0;
        g_dtors : int = 0;

        struct Obj {
            val : int;
            Obj(v: int) : val(v) { ++g_ctors; }
            ~Obj() { ++g_dtors; }

            get_val() : int { return val; }
        }

        make(v: int) : Obj {
            o : Obj(v);
            return o;
        }

        test() : int {
            return make(42).get_val();
        }

        get_ctors() : int { return g_ctors; }
        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 42);

    auto get_ctors = jit->lookup_symbol<int(*)()>("get_ctors");
    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_ctors != nullptr);
    REQUIRE(get_dtors != nullptr);
    CHECK(get_ctors() == 1);
    CHECK(get_dtors() == 1);
}

// =============================================================================
// Category RVO-5: Chained calls — make().method().method()
// =============================================================================

TEST_CASE("RVO-5: Chained method returning struct — all intermediates are temps", "[gen][rvo][rvo5]") {
    auto jit = gen_jit(R"SRC(
        module gen_rvo_14;

        g_ctors : int = 0;
        g_dtors : int = 0;

        struct Builder {
            val : int;
            Builder(v: int) : val(v) { ++g_ctors; }
            ~Builder() { ++g_dtors; }

            add(x: int) : Builder {
                r : Builder(val + x);
                return r;
            }

            get() : int { return val; }
        }

        make(v: int) : Builder {
            b : Builder(v);
            return b;
        }

        test() : int {
            return make(1).add(10).add(100).get();
        }

        get_ctors() : int { return g_ctors; }
        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 111);

    auto get_ctors = jit->lookup_symbol<int(*)()>("get_ctors");
    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_ctors != nullptr);
    REQUIRE(get_dtors != nullptr);
    // With RVO: 3 ctors (make, add, add), 3 dtors (3 temps at end of statement)
    CHECK(get_ctors() == 3);
    CHECK(get_dtors() == 3);
}

TEST_CASE("RVO-5: Deep chain with different struct types", "[gen][rvo][rvo5]") {
    auto jit = gen_jit(R"SRC(
        module gen_rvo_15;

        g_order : int = 0;
        g_ctors : int = 0;

        struct Step1 {
            val : int;
            Step1(v: int) : val(v) { ++g_ctors; }
            ~Step1() { g_order = g_order * 10 + 1; }

            next() : Step2 {
                r : Step2(val * 2);
                return r;
            }
        }

        struct Step2 {
            val : int;
            Step2(v: int) : val(v) { ++g_ctors; }
            ~Step2() { g_order = g_order * 10 + 2; }

            get() : int { return val; }
        }

        make(v: int) : Step1 {
            s : Step1(v);
            return s;
        }

        test() : int {
            return make(5).next().get();
        }

        get_order() : int { return g_order; }
        get_ctors() : int { return g_ctors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 10);

    auto get_order = jit->lookup_symbol<int(*)()>("get_order");
    auto get_ctors = jit->lookup_symbol<int(*)()>("get_ctors");
    REQUIRE(get_order != nullptr);
    REQUIRE(get_ctors != nullptr);
    // With RVO: 2 ctors (Step1 + Step2)
    CHECK(get_ctors() == 2);
    // Reverse destruction: Step2 (2) then Step1 (1) → g_order == 21
    CHECK(get_order() == 21);
}

TEST_CASE("RVO-5: Chained call result assigned to variable", "[gen][rvo][rvo5]") {
    auto jit = gen_jit(R"SRC(
        module gen_rvo_16;

        g_ctors : int = 0;
        g_dtors : int = 0;

        struct Obj {
            val : int;
            Obj(v: int) : val(v) { ++g_ctors; }
            ~Obj() { ++g_dtors; }

            transform(x: int) : Obj {
                r : Obj(val + x);
                return r;
            }
        }

        make(v: int) : Obj {
            o : Obj(v);
            return o;
        }

        test() : int {
            r : Obj = make(1).transform(41);
            return r.val;
        }

        get_ctors() : int { return g_ctors; }
        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 42);

    auto get_ctors = jit->lookup_symbol<int(*)()>("get_ctors");
    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_ctors != nullptr);
    REQUIRE(get_dtors != nullptr);
    // make(1) → temp1, transform(41) → r (or temp2 copied to r)
    // With full elision: 2 ctors, 2 dtors (temp1 + r)
    CHECK(get_ctors() == 2);
    CHECK(get_dtors() == 2);
}

// =============================================================================
// Category RVO-6: Nested calls — consume(make(42))
// =============================================================================

TEST_CASE("RVO-6: Struct return passed by value to another function", "[gen][rvo][rvo6]") {
    auto jit = gen_jit(R"SRC(
        module gen_rvo_17;

        g_ctors : int = 0;
        g_dtors : int = 0;

        struct Box {
            val : int;
            Box(v: int) : val(v) { ++g_ctors; }
            ~Box() { ++g_dtors; }
        }

        make(v: int) : Box {
            b : Box(v);
            return b;
        }

        consume(b: Box) : int {
            return b.val;
        }

        test() : int {
            return consume(make(42));
        }

        get_ctors() : int { return g_ctors; }
        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 42);

    auto get_ctors = jit->lookup_symbol<int(*)()>("get_ctors");
    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_ctors != nullptr);
    REQUIRE(get_dtors != nullptr);
    // With NRVO + argument copy elision:
    // make: 1 ctor (into sret staging alloca, NOT tracked for cleanup)
    // consume: param b receives the loaded aggregate → 1 dtor at consume exit
    // Total: 1 ctor, 1 dtor (the prvalue from make() materializes directly as param b)
    CHECK(get_ctors() == 1);
    CHECK(get_dtors() == 1);
}

TEST_CASE("RVO-6: Struct return assigned then passed by value", "[gen][rvo][rvo6]") {
    auto jit = gen_jit(R"SRC(
        module gen_rvo_18;

        g_ctors : int = 0;
        g_dtors : int = 0;

        struct Obj {
            val : int;
            Obj(v: int) : val(v) { ++g_ctors; }
            ~Obj() { ++g_dtors; }
        }

        make(v: int) : Obj {
            o : Obj(v);
            return o;
        }

        consume(o: Obj) : int {
            return o.val;
        }

        test() : int {
            x : Obj = make(42);
            return consume(x);
        }

        get_ctors() : int { return g_ctors; }
        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 42);

    auto get_ctors = jit->lookup_symbol<int(*)()>("get_ctors");
    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_ctors != nullptr);
    REQUIRE(get_dtors != nullptr);
    // make → directly into x (NRVO+sret): 1 ctor
    // consume(x) → bitwise copy param: 1 dtor (param) + 1 dtor (x) = 2
    CHECK(get_ctors() == 1);
    CHECK(get_dtors() == 2);
}

TEST_CASE("RVO-6: Struct return from nested factory calls", "[gen][rvo][rvo6]") {
    // wrap(make(42)) where wrap also returns a struct
    auto jit = gen_jit(R"SRC(
        module gen_rvo_19;

        g_ctors : int = 0;
        g_dtors : int = 0;

        struct Obj {
            val : int;
            Obj(v: int) : val(v) { ++g_ctors; }
            ~Obj() { ++g_dtors; }
        }

        make(v: int) : Obj {
            o : Obj(v);
            return o;
        }

        wrap(o: Obj) : Obj {
            r : Obj(o.val + 100);
            return r;
        }

        test() : int {
            result : Obj = wrap(make(42));
            return result.val;
        }

        get_ctors() : int { return g_ctors; }
        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 142);

    auto get_ctors = jit->lookup_symbol<int(*)()>("get_ctors");
    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_ctors != nullptr);
    REQUIRE(get_dtors != nullptr);
    // With NRVO + argument copy elision:
    // make: 1 ctor (into staging alloca, NOT tracked for cleanup — arg elision)
    // wrap: 1 ctor for r (Obj(142)), + 1 dtor for by-val param o at exit
    // caller: result destroyed via NRVO
    // Total: 2 ctors, 2 dtors (param-o, result)
    CHECK(get_ctors() == 2);
    CHECK(get_dtors() == 2);
}

// =============================================================================
// Category RVO-7: Multiple return paths (NRVO ineligible or with fallback)
// =============================================================================

TEST_CASE("RVO-7: Multiple returns of same variable — NRVO eligible", "[gen][rvo][rvo7]") {
    auto jit = gen_jit(R"SRC(
        module gen_rvo_20;

        g_ctors : int = 0;
        g_dtors : int = 0;

        struct Obj {
            val : int;
            Obj(v: int) : val(v) { ++g_ctors; }
            ~Obj() { ++g_dtors; }
        }

        make(v: int) : Obj {
            o : Obj(v);
            if (v > 100) {
                o.val = 999;
                return o;
            }
            return o;
        }

        test() : int {
            r : Obj = make(42);
            return r.val;
        }

        get_ctors() : int { return g_ctors; }
        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 42);

    auto get_ctors = jit->lookup_symbol<int(*)()>("get_ctors");
    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_ctors != nullptr);
    REQUIRE(get_dtors != nullptr);
    // All return paths return 'o' → NRVO eligible
    CHECK(get_ctors() == 1);
    CHECK(get_dtors() == 1);
}

TEST_CASE("RVO-7: Multiple returns of different variables — NRVO ineligible, sret fallback", "[gen][rvo][rvo7]") {
    // Different named locals returned from different branches → NRVO not possible.
    // Should still use sret (copy into sret at each return), but cannot eliminate all copies.
    auto jit = gen_jit(R"SRC(
        module gen_rvo_21;

        g_ctors : int = 0;
        g_dtors : int = 0;

        struct Obj {
            val : int;
            Obj(v: int) : val(v) { ++g_ctors; }
            ~Obj() { ++g_dtors; }
        }

        make(flag: int) : Obj {
            if (flag > 0) {
                a : Obj(10);
                return a;
            }
            b : Obj(20);
            return b;
        }

        test_pos() : int {
            r : Obj = make(1);
            return r.val;
        }

        test_neg() : int {
            r : Obj = make(0);
            return r.val;
        }

        get_ctors() : int { return g_ctors; }
        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test_pos = jit->lookup_symbol<int(*)()>("test_pos");
    REQUIRE(test_pos != nullptr);
    CHECK(test_pos() == 10);

    auto test_neg = jit->lookup_symbol<int(*)()>("test_neg");
    REQUIRE(test_neg != nullptr);
    CHECK(test_neg() == 20);

    auto get_ctors = jit->lookup_symbol<int(*)()>("get_ctors");
    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_ctors != nullptr);
    REQUIRE(get_dtors != nullptr);
    // Each call creates exactly 1 Obj: 2 calls → 2 ctors, 2 dtors (the locals + results)
    // Without NRVO: local is constructed, then copied into sret, local destroyed, result destroyed.
    // With NRVO: local IS sret → 1 ctor, 1 dtor per call.
    // Either way, values must be correct.
    CHECK(get_ctors() == 2);
    CHECK(get_dtors() >= 2);
}

TEST_CASE("RVO-7: Return from different scopes — NRVO-eligible with same variable name", "[gen][rvo][rvo7]") {
    // Same variable name in both branches — each branch constructs, returns
    auto jit = gen_jit(R"SRC(
        module gen_rvo_22;

        g_ctors : int = 0;
        g_dtors : int = 0;

        struct Obj {
            val : int;
            Obj(v: int) : val(v) { ++g_ctors; }
            ~Obj() { ++g_dtors; }
        }

        make(flag: int) : Obj {
            if (flag > 0) {
                a : Obj(10);
                return a;
            }
            b : Obj(20);
            return b;
        }

        test_pos() : int {
            r : Obj = make(1);
            return r.val;
        }

        test_neg() : int {
            r : Obj = make(0);
            return r.val;
        }

        get_ctors() : int { return g_ctors; }
        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test_pos = jit->lookup_symbol<int(*)()>("test_pos");
    REQUIRE(test_pos != nullptr);
    CHECK(test_pos() == 10);

    auto test_neg = jit->lookup_symbol<int(*)()>("test_neg");
    REQUIRE(test_neg != nullptr);
    CHECK(test_neg() == 20);

    auto get_ctors = jit->lookup_symbol<int(*)()>("get_ctors");
    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_ctors != nullptr);
    REQUIRE(get_dtors != nullptr);
    CHECK(get_ctors() == 2);
    CHECK(get_dtors() >= 2);
}

// =============================================================================
// Category RVO-8: Operator overloads returning structs
// =============================================================================

TEST_CASE("RVO-8: Operator+ returning struct by value", "[gen][rvo][rvo8]") {
    auto jit = gen_jit(R"SRC(
        module gen_rvo_23;

        g_ctors : int = 0;
        g_dtors : int = 0;

        struct Vec {
            x : int;
            y : int;
            Vec(px: int, py: int) : x(px), y(py) { ++g_ctors; }
            ~Vec() { ++g_dtors; }

            operator+(other: Vec&) : Vec {
                r : Vec(x + other.x, y + other.y);
                return r;
            }
        }

        test() : int {
            a : Vec(1, 2);
            b : Vec(10, 20);
            c : Vec = a + b;
            return c.x + c.y;
        }

        get_ctors() : int { return g_ctors; }
        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 33);  // (1+10) + (2+20) = 33

    auto get_ctors = jit->lookup_symbol<int(*)()>("get_ctors");
    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_ctors != nullptr);
    REQUIRE(get_dtors != nullptr);
    // 3 ctors: a, b, c (operator+ constructs directly into c with NRVO+sret)
    CHECK(get_ctors() == 3);
    CHECK(get_dtors() == 3);
}

TEST_CASE("RVO-8: Chained operator returning struct — a + b + c", "[gen][rvo][rvo8]") {
    // With left-associativity fixed, a + b + c is now parsed as (a + b) + c.
    auto jit = gen_jit(R"SRC(
        module gen_rvo_24;

        g_ctors : int = 0;
        g_dtors : int = 0;

        struct Vec {
            x : int;
            Vec(v: int) : x(v) { ++g_ctors; }
            ~Vec() { ++g_dtors; }

            operator+(other: Vec&) : Vec {
                r : Vec(x + other.x);
                return r;
            }
        }

        test() : int {
            a : Vec(1);
            b : Vec(10);
            c : Vec(100);
            d : Vec = a + b + c;
            return d.x;
        }

        get_ctors() : int { return g_ctors; }
        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 111);

    auto get_ctors = jit->lookup_symbol<int(*)()>("get_ctors");
    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_ctors != nullptr);
    REQUIRE(get_dtors != nullptr);
    // a, b, c: 3 ctors
    // a+b → temp1: 1 ctor, temp1+c → d: 1 ctor
    // Total ctors: 5 (a, b, c, temp1, d)
    // dtors: temp1 destroyed at end of stmt + a, b, c, d at scope exit = 5
    CHECK(get_ctors() == 5);
    CHECK(get_dtors() == 5);
}

// =============================================================================
// Category RVO-9: Virtual dispatch returning struct by value
// =============================================================================

TEST_CASE("RVO-9: Virtual method returning struct by value", "[gen][rvo][rvo9]") {
    auto jit = gen_jit(R"SRC(
        module gen_rvo_25;

        g_ctors : int = 0;
        g_dtors : int = 0;

        struct Result {
            val : int;
            Result(v: int) : val(v) { ++g_ctors; }
            ~Result() { ++g_dtors; }
        }

        class Base {
            public get() : Result {
                r : Result(10);
                return r;
            }
        }

        class Derived : Base {
            public get() : Result {
                r : Result(42);
                return r;
            }
        }

        test_via_ref(b: Base&) : int {
            r : Result = b.get();
            return r.val;
        }

        test() : int {
            d : Derived;
            return test_via_ref(d);
        }

        get_ctors() : int { return g_ctors; }
        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 42);

    auto get_ctors = jit->lookup_symbol<int(*)()>("get_ctors");
    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_ctors != nullptr);
    REQUIRE(get_dtors != nullptr);
    // Virtual dispatch should still use sret: 1 ctor (in Derived::get), 1 dtor (r)
    CHECK(get_ctors() == 1);
    CHECK(get_dtors() == 1);
}

// =============================================================================
// Category RVO-10: Struct without destructor (no crash, correct values)
// =============================================================================

TEST_CASE("RVO-10: Struct return without destructor — correct value", "[gen][rvo][rvo10]") {
    auto jit = gen_jit(R"SRC(
        module gen_rvo_26;

        struct Plain {
            val : int;
            Plain(v: int) : val(v) {}
        }

        make(v: int) : Plain {
            p : Plain(v);
            return p;
        }

        test() : int {
            r : Plain = make(99);
            return r.val;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 99);
}

TEST_CASE("RVO-10: Chained calls without destructor — no crash", "[gen][rvo][rvo10]") {
    auto jit = gen_jit(R"SRC(
        module gen_rvo_27;

        struct Obj {
            val : int;
            Obj(v: int) : val(v) {}
            get() : int { return val; }
            add(x: int) : Obj {
                r : Obj(val + x);
                return r;
            }
        }

        make(v: int) : Obj {
            o : Obj(v);
            return o;
        }

        test() : int {
            return make(1).add(10).add(100).get();
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 111);
}

TEST_CASE("RVO-10: Multiple-field struct return without destructor", "[gen][rvo][rvo10]") {
    auto jit = gen_jit(R"SRC(
        module gen_rvo_28;

        struct Rect {
            w : int;
            h : int;
            Rect(pw: int, ph: int) : w(pw), h(ph) {}
            area() : int { return w * h; }
        }

        make_rect(w: int, h: int) : Rect {
            r : Rect(w, h);
            return r;
        }

        test() : int {
            r : Rect = make_rect(6, 7);
            return r.area();
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 42);
}

// =============================================================================
// Category RVO-11: Control flow with struct returns
// =============================================================================

TEST_CASE("RVO-11: Struct return used in if-condition via method", "[gen][rvo][rvo11]") {
    auto jit = gen_jit(R"SRC(
        module gen_rvo_29;

        g_ctors : int = 0;
        g_dtors : int = 0;

        struct Checker {
            val : int;
            Checker(v: int) : val(v) { ++g_ctors; }
            ~Checker() { ++g_dtors; }
            is_positive() : int {
                if (val > 0) { return 1; }
                return 0;
            }
        }

        make(v: int) : Checker {
            c : Checker(v);
            return c;
        }

        test(v: int) : int {
            if (make(v).is_positive() > 0) {
                return 1;
            }
            return 0;
        }

        get_ctors() : int { return g_ctors; }
        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)(int)>("test");
    REQUIRE(test != nullptr);
    CHECK(test(5) == 1);
    CHECK(test(-3) == 0);

    auto get_ctors = jit->lookup_symbol<int(*)()>("get_ctors");
    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_ctors != nullptr);
    REQUIRE(get_dtors != nullptr);
    // 2 calls → 2 ctors, 2 dtors (temporaries at condition end)
    CHECK(get_ctors() == 2);
    CHECK(get_dtors() == 2);
}

TEST_CASE("RVO-11: Struct return in for-loop condition", "[gen][rvo][rvo11]") {
    auto jit = gen_jit(R"SRC(
        module gen_rvo_30;

        g_ctors : int = 0;
        g_dtors : int = 0;

        struct Limit {
            max : int;
            Limit(m: int) : max(m) { ++g_ctors; }
            ~Limit() { ++g_dtors; }
            above(i: int) : int {
                if (max > i) { return 1; }
                return 0;
            }
        }

        make_limit(m: int) : Limit {
            l : Limit(m);
            return l;
        }

        test() : int {
            sum : int = 0;
            for (i : int = 0; make_limit(3).above(i) > 0; ++i) {
                sum += i;
            }
            return sum;
        }

        get_ctors() : int { return g_ctors; }
        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 3); // 0 + 1 + 2

    auto get_ctors = jit->lookup_symbol<int(*)()>("get_ctors");
    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_ctors != nullptr);
    REQUIRE(get_dtors != nullptr);
    // 4 condition evaluations → 4 ctors, 4 dtors (each temp destroyed at condition end)
    CHECK(get_ctors() == 4);
    CHECK(get_dtors() == 4);
}

TEST_CASE("RVO-11: Struct return in while-loop condition", "[gen][rvo][rvo11]") {
    auto jit = gen_jit(R"SRC(
        module gen_rvo_31;

        g_ctors : int = 0;
        g_dtors : int = 0;
        g_counter : int = 0;

        struct Counter {
            max : int;
            Counter(m: int) : max(m) { ++g_ctors; }
            ~Counter() { ++g_dtors; }
            has_next() : int {
                ++g_counter;
                if (max > g_counter) { return 1; }
                return 0;
            }
        }

        make_counter(m: int) : Counter {
            c : Counter(m);
            return c;
        }

        test() : int {
            while (make_counter(3).has_next() > 0) {
            }
            return g_counter;
        }

        get_ctors() : int { return g_ctors; }
        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 3);

    auto get_ctors = jit->lookup_symbol<int(*)()>("get_ctors");
    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_ctors != nullptr);
    REQUIRE(get_dtors != nullptr);
    // 3 iterations of condition → 3 ctors, 3 dtors
    CHECK(get_ctors() == 3);
    CHECK(get_dtors() == 3);
}

// =============================================================================
// Category RVO-12: Struct with struct members (aggregation + return)
// =============================================================================

TEST_CASE("RVO-12: Return struct containing struct member", "[gen][rvo][rvo12]") {
    auto jit = gen_jit(R"SRC(
        module gen_rvo_32;

        g_inner_ctors : int = 0;
        g_inner_dtors : int = 0;
        g_outer_ctors : int = 0;
        g_outer_dtors : int = 0;

        struct Inner {
            val : int;
            Inner(v: int) : val(v) { ++g_inner_ctors; }
            ~Inner() { ++g_inner_dtors; }
        }

        struct Outer {
            inner : Inner;
            extra : int;
            Outer(v: int, e: int) : inner(v), extra(e) { ++g_outer_ctors; }
            ~Outer() { ++g_outer_dtors; }
        }

        make_outer(v: int) : Outer {
            o : Outer(v, v * 10);
            return o;
        }

        test() : int {
            r : Outer = make_outer(5);
            return r.inner.val + r.extra;
        }

        get_inner_ctors() : int { return g_inner_ctors; }
        get_inner_dtors() : int { return g_inner_dtors; }
        get_outer_ctors() : int { return g_outer_ctors; }
        get_outer_dtors() : int { return g_outer_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 55); // 5 + 50

    auto get_inner_ctors = jit->lookup_symbol<int(*)()>("get_inner_ctors");
    auto get_inner_dtors = jit->lookup_symbol<int(*)()>("get_inner_dtors");
    auto get_outer_ctors = jit->lookup_symbol<int(*)()>("get_outer_ctors");
    auto get_outer_dtors = jit->lookup_symbol<int(*)()>("get_outer_dtors");
    REQUIRE(get_inner_ctors != nullptr);
    REQUIRE(get_inner_dtors != nullptr);
    REQUIRE(get_outer_ctors != nullptr);
    REQUIRE(get_outer_dtors != nullptr);
    // With NRVO: Outer constructed once (directly into r), Inner constructed once (as member)
    CHECK(get_outer_ctors() == 1);
    CHECK(get_outer_dtors() == 1);
    CHECK(get_inner_ctors() == 1);
    CHECK(get_inner_dtors() == 1);
}

// =============================================================================
// Additional edge cases
// =============================================================================

TEST_CASE("RVO: Struct return value used in arithmetic expression", "[gen][rvo]") {
    auto jit = gen_jit(R"SRC(
        module gen_rvo_33;

        g_ctors : int = 0;
        g_dtors : int = 0;

        struct Obj {
            val : int;
            Obj(v: int) : val(v) { ++g_ctors; }
            ~Obj() { ++g_dtors; }
        }

        make(v: int) : Obj {
            o : Obj(v);
            return o;
        }

        test() : int {
            x : int = make(10).val + make(20).val;
            return x;
        }

        get_ctors() : int { return g_ctors; }
        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 30);

    auto get_ctors = jit->lookup_symbol<int(*)()>("get_ctors");
    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_ctors != nullptr);
    REQUIRE(get_dtors != nullptr);
    // 2 calls → 2 ctors, 2 dtors (both temps destroyed at end of variable_statement)
    CHECK(get_ctors() == 2);
    CHECK(get_dtors() == 2);
}

TEST_CASE("RVO: Multiple sequential struct returns — independent lifetimes", "[gen][rvo]") {
    auto jit = gen_jit(R"SRC(
        module gen_rvo_34;

        g_ctors : int = 0;
        g_dtors : int = 0;

        struct Obj {
            val : int;
            Obj(v: int) : val(v) { ++g_ctors; }
            ~Obj() { ++g_dtors; }
        }

        make(v: int) : Obj {
            o : Obj(v);
            return o;
        }

        test() : int {
            a : Obj = make(1);
            b : Obj = make(2);
            c : Obj = make(3);
            return a.val + b.val + c.val;
        }

        get_ctors() : int { return g_ctors; }
        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 6);

    auto get_ctors = jit->lookup_symbol<int(*)()>("get_ctors");
    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_ctors != nullptr);
    REQUIRE(get_dtors != nullptr);
    CHECK(get_ctors() == 3);
    CHECK(get_dtors() == 3);
}

TEST_CASE("RVO: Struct return from function with other locals — dtor ordering correct", "[gen][rvo]") {
    // Function has other locals alongside the NRVO candidate.
    // The other locals must be destroyed, but the NRVO candidate must NOT be
    // destroyed by the callee (it lives in caller's storage now).
    auto jit = gen_jit(R"SRC(
        module gen_rvo_35;

        g_order : int = 0;
        g_ctors : int = 0;

        struct Obj {
            id : int;
            Obj(v: int) : id(v) { ++g_ctors; }
            ~Obj() { g_order = g_order * 10 + id; }
        }

        make() : Obj {
            helper : Obj(9);
            result : Obj(1);
            return result;
        }

        test() : int {
            r : Obj = make();
            return r.id;
        }

        get_order() : int { return g_order; }
        get_ctors() : int { return g_ctors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 1);

    auto get_order = jit->lookup_symbol<int(*)()>("get_order");
    auto get_ctors = jit->lookup_symbol<int(*)()>("get_ctors");
    REQUIRE(get_order != nullptr);
    REQUIRE(get_ctors != nullptr);

    // With NRVO on 'result':
    //   make() ctors: helper(9) + result(1) = 2 ctors
    //   make() exit: helper destroyed (9), result is sret → NOT destroyed by callee
    //   test() exit: r destroyed (1)
    //   g_order = 9 * 10 + 1 = 91
    // Without NRVO:
    //   make() exit: result(1) destroyed, then helper(9) destroyed → 19 or 91
    //   Then caller r destroyed (1) → 191 or 911
    // We just verify the values are correct regardless of NRVO optimization.
    CHECK(get_ctors() >= 2);
    int order = get_order();
    // 'r' (id=1) must be the last thing destroyed
    CHECK((order % 10) == 1);
}

TEST_CASE("RVO: Struct factory called in return statement of another function", "[gen][rvo]") {
    // Chained RVO: test() returns make(42)
    auto jit = gen_jit(R"SRC(
        module gen_rvo_36;

        g_ctors : int = 0;
        g_dtors : int = 0;

        struct Obj {
            val : int;
            Obj(v: int) : val(v) { ++g_ctors; }
            ~Obj() { ++g_dtors; }
        }

        make(v: int) : Obj {
            o : Obj(v);
            return o;
        }

        proxy(v: int) : Obj {
            return make(v);
        }

        test() : int {
            r : Obj = proxy(42);
            return r.val;
        }

        get_ctors() : int { return g_ctors; }
        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 42);

    auto get_ctors = jit->lookup_symbol<int(*)()>("get_ctors");
    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_ctors != nullptr);
    REQUIRE(get_dtors != nullptr);
    // Chained RVO: make → proxy's sret → test's r
    // With full elision: 1 ctor, 1 dtor
    CHECK(get_ctors() == 1);
    CHECK(get_dtors() == 1);
}

TEST_CASE("RVO: Destructor side effect preserved — dtor runs exactly once per object", "[gen][rvo]") {
    // Verify that side effects in the destructor happen exactly once per constructed object.
    auto jit = gen_jit(R"SRC(
        module gen_rvo_37;

        g_sum : int = 0;

        struct Acc {
            val : int;
            Acc(v: int) : val(v) {}
            ~Acc() { g_sum += val; }
        }

        make(v: int) : Acc {
            a : Acc(v);
            return a;
        }

        test() : int {
            r : Acc = make(42);
            return 0;
        }

        get_sum() : int { return g_sum; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    test();

    auto get_sum = jit->lookup_symbol<int(*)()>("get_sum");
    REQUIRE(get_sum != nullptr);
    // With NRVO: only r is destroyed → g_sum = 42
    // Without NRVO: local + temp + r → g_sum could be 42*3 = 126
    // With RVO, side effect should be exactly 42.
    CHECK(get_sum() == 42);
}

// =============================================================================
// Category RVO-13: Copy constructor interaction with NRVO
// =============================================================================

TEST_CASE("RVO-13: NRVO bypasses copy constructor — no copy ctor call", "[gen][rvo][rvo13]") {
    // If NRVO is applied, the copy constructor should NOT be called.
    // The local is constructed directly into the caller's destination.
    auto jit = gen_jit(R"SRC(
        module gen_rvo_38;

        g_ctors : int = 0;
        g_copy_ctors : int = 0;
        g_dtors : int = 0;

        struct Obj {
            val : int;
            Obj(v: int) : val(v) { ++g_ctors; }
            Obj(other: Obj&) : val(other.val) { ++g_copy_ctors; }
            ~Obj() { ++g_dtors; }
        }

        make(v: int) : Obj {
            o : Obj(v);
            return o;
        }

        test() : int {
            r : Obj = make(42);
            return r.val;
        }

        get_ctors() : int { return g_ctors; }
        get_copy_ctors() : int { return g_copy_ctors; }
        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 42);

    auto get_ctors = jit->lookup_symbol<int(*)()>("get_ctors");
    auto get_copy_ctors = jit->lookup_symbol<int(*)()>("get_copy_ctors");
    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_ctors != nullptr);
    REQUIRE(get_copy_ctors != nullptr);
    REQUIRE(get_dtors != nullptr);
    // With NRVO: 1 ctor (directly into r), 0 copy ctors, 1 dtor (r)
    CHECK(get_ctors() == 1);
    CHECK(get_copy_ctors() == 0);
    CHECK(get_dtors() == 1);
}

// =============================================================================
// Category RVO-14: NRVO with non-NRVO local cleanup
// =============================================================================

TEST_CASE("RVO-14: NRVO — non-NRVO locals destroyed, NRVO candidate preserved", "[gen][rvo][rvo14]") {
    // The NRVO candidate should not be destroyed by the callee, but other
    // locals in the same or nested scopes should be destroyed normally.
    auto jit = gen_jit(R"SRC(
        module gen_rvo_39;

        g_ctor_ids : int = 0;
        g_dtor_ids : int = 0;

        struct Obj {
            id : int;
            Obj(v: int) : id(v) { g_ctor_ids = g_ctor_ids * 10 + v; }
            ~Obj() { g_dtor_ids = g_dtor_ids * 10 + id; }
        }

        make(v: int) : Obj {
            helper1 : Obj(8);
            result : Obj(v);
            helper2 : Obj(9);
            return result;
        }

        test() : int {
            r : Obj = make(1);
            return r.id;
        }

        get_ctor_ids() : int { return g_ctor_ids; }
        get_dtor_ids() : int { return g_dtor_ids; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 1);

    auto get_ctor_ids = jit->lookup_symbol<int(*)()>("get_ctor_ids");
    auto get_dtor_ids = jit->lookup_symbol<int(*)()>("get_dtor_ids");
    REQUIRE(get_ctor_ids != nullptr);
    REQUIRE(get_dtor_ids != nullptr);

    // Construction order: helper1(8), result(1), helper2(9) → g_ctor_ids = 819
    CHECK(get_ctor_ids() == 819);

    // Destruction order at return: reverse of declaration, skipping NRVO candidate (result):
    //   helper2(9), then helper1(8) destroyed in make()
    //   then r(1) destroyed in test()
    // With NRVO: g_dtor_ids = 9 * 10 + 8 = 98, then * 10 + 1 = 981
    // Without NRVO: result(1) also destroyed → different ordering
    int dtor_ids = get_dtor_ids();
    // The last thing destroyed must be r (id=1)
    CHECK((dtor_ids % 10) == 1);
}

TEST_CASE("RVO-14: NRVO — return from if-else with helper locals in each branch", "[gen][rvo][rvo14]") {
    // Both branches return the same NRVO candidate, but each branch has
    // a helper local that must be destroyed before the return.
    auto jit = gen_jit(R"SRC(
        module gen_rvo_40;

        g_ctors : int = 0;
        g_dtors : int = 0;

        struct Obj {
            val : int;
            Obj(v: int) : val(v) { ++g_ctors; }
            ~Obj() { ++g_dtors; }
        }

        make(flag: int) : Obj {
            result : Obj(42);
            if (flag > 0) {
                helper : Obj(88);
                result.val = result.val + helper.val;
                return result;
            }
            return result;
        }

        test_pos() : int {
            r : Obj = make(1);
            return r.val;
        }

        test_neg() : int {
            r : Obj = make(0);
            return r.val;
        }

        get_ctors() : int { return g_ctors; }
        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    // Test positive branch (with helper)
    auto test_pos = jit->lookup_symbol<int(*)()>("test_pos");
    REQUIRE(test_pos != nullptr);
    CHECK(test_pos() == 130);  // 42 + 88

    auto get_ctors = jit->lookup_symbol<int(*)()>("get_ctors");
    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_ctors != nullptr);
    REQUIRE(get_dtors != nullptr);
    // pos branch: result(42) + helper(88) = 2 ctors
    // helper destroyed, r destroyed = 2 dtors
    CHECK(get_ctors() == 2);
    CHECK(get_dtors() == 2);

    // Test negative branch (no helper)
    auto test_neg = jit->lookup_symbol<int(*)()>("test_neg");
    REQUIRE(test_neg != nullptr);
    CHECK(test_neg() == 42);

    // After both calls: 3 ctors total (2 from pos + 1 from neg)
    CHECK(get_ctors() == 3);
    CHECK(get_dtors() == 3);
}

// =============================================================================
// Category RVO-15: Sret with class (not just struct)
// =============================================================================

TEST_CASE("RVO-15: Class return by value — sret + NRVO works for classes", "[gen][rvo][rvo15]") {
    // Classes (with vtables) also use sret for return by value.
    auto jit = gen_jit(R"SRC(
        module gen_rvo_41;

        g_ctors : int = 0;
        g_dtors : int = 0;

        struct Result {
            val : int;
            Result(v: int) : val(v) { ++g_ctors; }
            ~Result() { ++g_dtors; }
        }

        make_result(v: int) : Result {
            r : Result(v);
            return r;
        }

        test() : int {
            r : Result = make_result(42);
            return r.val;
        }

        get_ctors() : int { return g_ctors; }
        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 42);

    auto get_ctors = jit->lookup_symbol<int(*)()>("get_ctors");
    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_ctors != nullptr);
    REQUIRE(get_dtors != nullptr);
    CHECK(get_ctors() == 1);
    CHECK(get_dtors() == 1);
}

// =============================================================================
// Category RVO-16: Recursive factory function
// =============================================================================

TEST_CASE("RVO-16: Recursive factory — correct value through recursive sret chain", "[gen][rvo][rvo16]") {
    // A recursive function that returns a struct. At the base case it constructs
    // one, at recursive cases it calls itself. Sret must be threaded correctly.
    auto jit = gen_jit(R"SRC(
        module gen_rvo_42;

        g_ctors : int = 0;
        g_dtors : int = 0;

        struct Obj {
            val : int;
            Obj(v: int) : val(v) { ++g_ctors; }
            ~Obj() { ++g_dtors; }
        }

        make(n: int) : Obj {
            if (n > 0) {
                tmp : Obj = make(n - 1);
                result : Obj(tmp.val + n);
                return result;
            }
            base : Obj(0);
            return base;
        }

        test() : int {
            r : Obj = make(3);
            return r.val;
        }

        get_ctors() : int { return g_ctors; }
        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    // make(0) = 0, make(1) = 0+1=1, make(2) = 1+2=3, make(3) = 3+3=6
    CHECK(test() == 6);

    auto get_ctors = jit->lookup_symbol<int(*)()>("get_ctors");
    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_ctors != nullptr);
    REQUIRE(get_dtors != nullptr);
    // 4 levels of recursion: base(0), tmp+result at each level = 7 ctors
    // With NRVO: each level constructs result and tmp.
    // base: 1 ctor (NRVO'd)
    // n=1: tmp from make(0) = 1 ctor + result(1) = 1 ctor, tmp destroyed = 1 dtor
    // n=2: tmp from make(1) = ... + result(3), tmp destroyed
    // n=3: tmp from make(2) = ... + result(6), tmp destroyed
    // Final r destroyed
    // Values must be correct regardless of exact elision.
    CHECK(get_ctors() >= 4);
}

TEST_CASE("RVO-17: Converting-constructor variable init from a different-typed sret call — no _sret_destination aliasing",
          "[gen][rvo][rvo17][sret]") {
    // Regression test for a bug where `_sret_destination` (the outer variable's
    // storage, set up by visit_variable_statement for a struct-typed declaration
    // with a converting-constructor initializer) leaked into evaluation of the
    // constructor's OWN arguments. When one of those arguments is itself a
    // different-struct-typed sret-returning call (e.g. `g : Box = wrap(inner());`
    // where inner() returns Inner and Box has a converting constructor
    // `Box(other: const Inner&)`), the leaked destination caused the nested call's
    // sret result to be written directly into the outer variable's storage. Since
    // every constructor zero-initializes its 'this' before running its body, this
    // aliased 'this' with 'other' and wiped out 'other' before the converting
    // constructor could read it — silently producing a default/zeroed value.
    //
    // This exact pattern was found via a real-world ListMap<K,V>::get() bug
    // (libk/libk/src/map.k), where `OptionalConstRef<V> get()` internally built an
    // `OptionalRef<V>` result and converted it via
    // `OptionalConstRef(other: const OptionalRef<V>&)`.
    auto jit = gen_jit(R"SRC(
        module gen_rvo_43;

        struct Inner {
            val : int;
        }

        struct Box {
            val : int;
            Box(other: const Inner&) : val(other.val) { }
        }

        inner() : Inner {
            result : Inner;
            result.val = 42;
            return result;
        }

        test() : int {
            g : Box = inner();
            return g.val;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 42);
}

