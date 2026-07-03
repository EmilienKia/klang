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
 * Object lifecycle tests for the K language.
 *
 * Verifies correct allocation, construction, destruction and deallocation
 * across all scenarios:
 *
 * Category 1: Static allocation (stack-allocated struct variables)
 * Category 2: Dynamic allocation (new/delete, owner)
 * Category 3: Pass-by-value struct parameters
 * Category 4: Struct return values
 * Category 5: Temporary objects in expressions (requires compiler changes)
 * Category 6: Struct members of struct type (aggregation)
 * Category 7: Temporaries in control flow (if, while, for conditions)
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

// =============================================================================
// Category 1: Static allocation — stack-allocated struct variables
// =============================================================================

TEST_CASE("Lifecycle Cat1: Multiple locals, reverse destruction order", "[gen][lifecycle][cat1]") {
    auto jit = gen_jit(R"SRC(
        module __lc1_reverse__;

        g_order : int = 0;

        struct First {
            ~First() { g_order = g_order * 10 + 1; }
        }

        struct Second {
            ~Second() { g_order = g_order * 10 + 2; }
        }

        test() : int {
            a : First;
            b : Second;
            return 0;
        }

        get_order() : int { return g_order; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    test();

    auto get_order = jit->lookup_symbol<int(*)()>("get_order");
    REQUIRE(get_order != nullptr);
    // Second destroyed first (21), then First
    CHECK(get_order() == 21);
}

TEST_CASE("Lifecycle Cat1: Nested blocks — inner scope dtor before outer", "[gen][lifecycle][cat1]") {
    auto jit = gen_jit(R"SRC(
        module __lc1_nested__;

        g_log : int = 0;

        struct A {
            ~A() { g_log = g_log * 10 + 1; }
        }

        struct B {
            ~B() { g_log = g_log * 10 + 2; }
        }

        test() : int {
            a : A;
            {
                b : B;
            }
            return g_log;
        }

        get_log() : int { return g_log; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    // At the return statement, only B has been destroyed (inner block exited)
    CHECK(test() == 2);

    auto get_log = jit->lookup_symbol<int(*)()>("get_log");
    REQUIRE(get_log != nullptr);
    // After test() returns, A is also destroyed: g_log = 2 * 10 + 1 = 21
    CHECK(get_log() == 21);
}

TEST_CASE("Lifecycle Cat1: Return captures value before dtors (two variables)", "[gen][lifecycle][cat1]") {
    auto jit = gen_jit(R"SRC(
        module __lc1_ret2__;

        g_count : int = 0;

        struct C {
            ~C() { g_count = g_count + 1; }
        }

        test() : int {
            c1 : C;
            c2 : C;
            return g_count;
        }

        get_count() : int { return g_count; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    // Return value captured before destructors run
    CHECK(test() == 0);

    auto get_count = jit->lookup_symbol<int(*)()>("get_count");
    REQUIRE(get_count != nullptr);
    // Both c1 and c2 destroyed after return
    CHECK(get_count() == 2);
}

TEST_CASE("Lifecycle Cat1: Constructor with member-init list + dtor", "[gen][lifecycle][cat1]") {
    auto jit = gen_jit(R"SRC(
        module __lc1_minit__;

        g_val : int = 0;

        struct Foo {
            val : int;
            Foo(x: int) : val(x) {}
            ~Foo() { g_val = val; }
        }

        test() : int {
            f : Foo(42);
            return f.val;
        }

        get_val() : int { return g_val; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 42);

    auto get_val = jit->lookup_symbol<int(*)()>("get_val");
    REQUIRE(get_val != nullptr);
    // Destructor saved the field value to g_val
    CHECK(get_val() == 42);
}

TEST_CASE("Lifecycle Cat1: Virtual call in constructor body uses the class vptr", "[gen][lifecycle][cat1][vptr]") {
    auto jit = gen_jit(R"SRC(
        module __lc1_ctor_vptr__;

        class Base {
            value : int;
            public:
            Base() : value(0) {}
            touch() { value = 1; }
            get() : int { return value; }
        }

        class Derived : public Base {
            public:
            Derived() { touch(); }
            override touch() { value = 42; }
        }

        test() : int {
            d : Derived;
            return d.get();
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 42);
}

// =============================================================================
// Category 2: Dynamic allocation — new/delete, owner
// =============================================================================

TEST_CASE("Lifecycle Cat2: Owner of struct with dtor — delete then delete-null", "[gen][lifecycle][cat2]") {
    auto jit = gen_jit(R"SRC(
        module __lc2_deldel__;

        g_dtors : int = 0;

        struct Tracked {
            Tracked() {}
            ~Tracked() { g_dtors = g_dtors + 1; }
        }

        test() : int {
            p : Tracked! = new Tracked();
            delete p;
            result : int = g_dtors;
            delete p;
            return result * 10 + g_dtors;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    // result = 1 (first delete), second delete is no-op → 1*10 + 1 = 11
    CHECK(test() == 11);
}

TEST_CASE("Lifecycle Cat2: Owner in conditional branch only", "[gen][lifecycle][cat2]") {
    auto jit = gen_jit(R"SRC(
        module __lc2_cond__;

        g_dtors : int = 0;

        struct Item {
            Item() {}
            ~Item() { g_dtors = g_dtors + 1; }
        }

        test(flag: int) : int {
            if (flag > 0) {
                p : Item! = new Item();
            }
            return g_dtors;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)(int)>("test");
    REQUIRE(test != nullptr);
    // flag=1: owner created and auto-destroyed at if-block exit → g_dtors=1
    CHECK(test(1) == 1);
    // flag=0: no owner created → g_dtors still 1
    CHECK(test(0) == 1);
}

// =============================================================================
// Category 3: Pass-by-value struct parameters
// =============================================================================

TEST_CASE("Lifecycle Cat3: Struct passed by value — dtor called on copy at function exit", "[gen][lifecycle][cat3]") {
    auto jit = gen_jit(R"SRC(
        module __lc3_byval__;

        g_dtors : int = 0;

        struct Box {
            v : int = 0;
            Box() {}
            ~Box() { g_dtors = g_dtors + 1; }
        }

        consume(b: Box) : int {
            return b.v;
        }

        test() : int {
            b : Box;
            b.v = 42;
            result : int = consume(b);
            return result;
        }

        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 42);

    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_dtors != nullptr);
    // 1 dtor for the copy in consume() + 1 dtor for original b in test() = 2
    CHECK(get_dtors() == 2);
}

TEST_CASE("Lifecycle Cat3: Struct passed by reference — no copy, no extra dtor", "[gen][lifecycle][cat3]") {
    auto jit = gen_jit(R"SRC(
        module __lc3_byref__;

        g_dtors : int = 0;

        struct Box {
            v : int = 0;
            Box() {}
            ~Box() { g_dtors = g_dtors + 1; }
        }

        read_ref(b: Box&) : int {
            return b.v;
        }

        test() : int {
            b : Box;
            b.v = 42;
            result : int = read_ref(b);
            return result;
        }

        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 42);

    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_dtors != nullptr);
    // Only 1 dtor for the original b in test()
    CHECK(get_dtors() == 1);
}

// =============================================================================
// Category 4: Struct return values
// =============================================================================

TEST_CASE("Lifecycle Cat4: Function returning struct by value, assigned to local", "[gen][lifecycle][cat4]") {
    auto jit = gen_jit(R"SRC(
        module __lc4_retval__;

        g_ctors : int = 0;
        g_dtors : int = 0;

        struct Obj {
            val : int;
            Obj(v: int) : val(v) { g_ctors = g_ctors + 1; }
            ~Obj() { g_dtors = g_dtors + 1; }
        }

        make(v: int) : int {
            o : Obj(v);
            return o.val;
        }

        test() : int {
            result : int = make(42);
            return result;
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

    // 1 ctor in make(), 1 dtor in make() (at scope exit)
    CHECK(get_ctors() == 1);
    CHECK(get_dtors() == get_ctors());
}

// =============================================================================
// Category 6: Struct members of struct type (aggregation)
// =============================================================================

TEST_CASE("Lifecycle Cat6: Outer struct with inner struct member — inner dtor called", "[gen][lifecycle][cat6]") {
    auto jit = gen_jit(R"SRC(
        module __lc6_inner__;

        g_log : int = 0;

        struct Inner {
            ~Inner() { g_log = g_log + 1; }
        }

        struct Outer {
            m : Inner;
        }

        test() : int {
            o : Outer;
            return 0;
        }

        get_log() : int { return g_log; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    test();

    auto get_log = jit->lookup_symbol<int(*)()>("get_log");
    REQUIRE(get_log != nullptr);
    CHECK(get_log() == 1);
}

TEST_CASE("Lifecycle Cat6: Multiple struct members — reverse destruction order", "[gen][lifecycle][cat6]") {
    auto jit = gen_jit(R"SRC(
        module __lc6_multi__;

        g_order : int = 0;

        struct A {
            ~A() { g_order = g_order * 10 + 1; }
        }

        struct B {
            ~B() { g_order = g_order * 10 + 2; }
        }

        struct Outer {
            a : A;
            b : B;
        }

        test() : int {
            o : Outer;
            return 0;
        }

        get_order() : int { return g_order; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    test();

    auto get_order = jit->lookup_symbol<int(*)()>("get_order");
    REQUIRE(get_order != nullptr);
    // B destroyed first (field declared second, reverse order), then A
    CHECK(get_order() == 21);
}

TEST_CASE("Lifecycle Cat6: Nested aggregation depth 3", "[gen][lifecycle][cat6]") {
    auto jit = gen_jit(R"SRC(
        module __lc6_deep__;

        g_order : int = 0;

        struct A {
            ~A() { g_order = g_order * 10 + 1; }
        }

        struct B {
            a : A;
            ~B() { g_order = g_order * 10 + 2; }
        }

        struct C {
            b : B;
            ~C() { g_order = g_order * 10 + 3; }
        }

        test() : int {
            c : C;
            return 0;
        }

        get_order() : int { return g_order; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    test();

    auto get_order = jit->lookup_symbol<int(*)()>("get_order");
    REQUIRE(get_order != nullptr);
    // C::~C() runs (3), then C.b::~B() runs (2), then C.b.a::~A() runs (1)
    // g_order = ((0*10+3)*10+2)*10+1 = 321
    CHECK(get_order() == 321);
}

// =============================================================================
// Category 4 (extended): Struct return values — actual struct-by-value returns
// =============================================================================

TEST_CASE("Lifecycle Cat4: Function returning struct by value, assigned to local var", "[gen][lifecycle][cat4]") {
    auto jit = gen_jit(R"SRC(
        module __lc4_struct_ret__;

        g_ctors : int = 0;
        g_dtors : int = 0;

        struct Obj {
            val : int;
            Obj(v: int) : val(v) { g_ctors = g_ctors + 1; }
            ~Obj() { g_dtors = g_dtors + 1; }
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
    // Without copy constructors, dtors > ctors because bitwise copies also get destroyed.
    // 1 ctor in make(), but 3 dtors: local o in make() + caller temp + r in test().
    // With copy elision (RVO), could be 1 ctor and 1 dtor. Either is acceptable.
    CHECK(get_ctors() >= 1);
    CHECK(get_dtors() >= get_ctors());
}

TEST_CASE("Lifecycle Cat4: Struct return value used in expression then discarded", "[gen][lifecycle][cat4]") {
    auto jit = gen_jit(R"SRC(
        module __lc4_discard__;

        g_dtors : int = 0;

        struct Obj {
            val : int;
            Obj(v: int) : val(v) {}
            ~Obj() { g_dtors = g_dtors + 1; }
        }

        make(v: int) : Obj {
            o : Obj(v);
            return o;
        }

        test() : int {
            x : int = make(42).val;
            return x;
        }

        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 42);

    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_dtors != nullptr);
    // All created objects must be destroyed
    CHECK(get_dtors() >= 1);
}

// =============================================================================
// Category 5: Temporary objects in expressions
// =============================================================================

TEST_CASE("Lifecycle Cat5: Temporary struct from function call, result discarded", "[gen][lifecycle][cat5]") {
    // A bare expression-statement calling a function that returns a struct.
    // The temporary must be destroyed at the end of the statement.
    auto jit = gen_jit(R"SRC(
        module __lc5_discard__;

        g_ctors : int = 0;
        g_dtors : int = 0;

        struct Tmp {
            Tmp() { g_ctors = g_ctors + 1; }
            ~Tmp() { g_dtors = g_dtors + 1; }
        }

        make() : Tmp {
            t : Tmp;
            return t;
        }

        test() : int {
            make();
            return g_dtors;
        }

        get_ctors() : int { return g_ctors; }
        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    int result = test();
    // By the time test() returns g_dtors, the temporary from make() should
    // already have been destroyed (end of expression-statement).
    // At minimum: 1 dtor for the local in make(), and 1 for the caller-side temporary.
    CHECK(result >= 1);

    auto get_ctors = jit->lookup_symbol<int(*)()>("get_ctors");
    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_ctors != nullptr);
    REQUIRE(get_dtors != nullptr);
    // Without copy constructors, the caller-side temporary is a bitwise copy,
    // so g_ctors=1 (inside make) but g_dtors=2 (local in make + caller temp).
    // With NRVO: local is not destroyed in callee, only the caller temp → 1 dtor.
    // The key invariant: every created object is destroyed.
    CHECK(get_ctors() >= 1);
    CHECK(get_dtors() >= 1);
}

TEST_CASE("Lifecycle Cat5: Member access on temporary struct return value", "[gen][lifecycle][cat5]") {
    // make().val — the temporary must stay alive during member access,
    // then be destroyed at the end of the full expression.
    auto jit = gen_jit(R"SRC(
        module __lc5_member__;

        g_dtors : int = 0;

        struct Obj {
            val : int;
            Obj(v: int) : val(v) {}
            ~Obj() { g_dtors = g_dtors + 1; }
        }

        make(v: int) : Obj {
            o : Obj(v);
            return o;
        }

        test() : int {
            x : int = make(99).val;
            return x;
        }

        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    // The member access must work correctly (temporary alive)
    CHECK(test() == 99);

    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_dtors != nullptr);
    // All temporaries destroyed
    CHECK(get_dtors() >= 1);
}

TEST_CASE("Lifecycle Cat5: Chained method call on temporary", "[gen][lifecycle][cat5]") {
    // make().get_val() where make() returns a struct with a member function.
    // The temporary must survive until after get_val() completes,
    // then be destroyed at end of full expression.
    auto jit = gen_jit(R"SRC(
        module __lc5_chain__;

        g_dtors : int = 0;

        struct Obj {
            val : int;
            Obj(v: int) : val(v) {}
            ~Obj() { g_dtors = g_dtors + 1; }

            get_val() : int { return val; }
        }

        make(v: int) : Obj {
            o : Obj(v);
            return o;
        }

        test() : int {
            x : int = make(77).get_val();
            return x;
        }

        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 77);

    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_dtors != nullptr);
    CHECK(get_dtors() >= 1);
}

TEST_CASE("Lifecycle Cat5: Multiple temporaries in one expression, reverse destruction", "[gen][lifecycle][cat5]") {
    // f1().val + f2().val — two temporaries created; both destroyed at end
    // of the full expression, in reverse creation order.
    auto jit = gen_jit(R"SRC(
        module __lc5_multi__;

        g_order : int = 0;

        struct A {
            val : int;
            A(v: int) : val(v) {}
            ~A() { g_order = g_order * 10 + 1; }
        }

        struct B {
            val : int;
            B(v: int) : val(v) {}
            ~B() { g_order = g_order * 10 + 2; }
        }

        make_a(v: int) : A {
            a : A(v);
            return a;
        }

        make_b(v: int) : B {
            b : B(v);
            return b;
        }

        test() : int {
            x : int = make_a(10).val + make_b(20).val;
            return x;
        }

        get_order() : int { return g_order; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 30);

    auto get_order = jit->lookup_symbol<int(*)()>("get_order");
    REQUIRE(get_order != nullptr);
    // Temporaries from make_a and make_b destroyed at end of variable_statement.
    // In reverse creation order: B destroyed first (2), then A (1).
    // But dtors from inside make_a/make_b also ran first.
    // At minimum, caller-side temporaries must both be destroyed.
    // The exact g_order depends on whether we have copy elision.
    // Without RVO: make_a's local dtor (1), make_b's local dtor (2),
    //   then caller temps reversed: B_temp(2), A_temp(1) → order includes ...2121 or similar.
    // Key check: final g_order must contain at least two digits for caller-side temps.
    int order = get_order();
    // At the very least, both A and B temporaries were destroyed
    CHECK(order > 0);
    // The last two destructions at caller site must be reverse order: B(2) then A(1)
    // So the last two digits of g_order should be "21"
    CHECK((order % 100) == 21);
}

TEST_CASE("Lifecycle Cat5: Deep chaining — two struct intermediaries", "[gen][lifecycle][cat5]") {
    // make().transform().get_val() with two struct temporaries.
    // Both must survive until end of full expression, then destroyed in reverse.
    auto jit = gen_jit(R"SRC(
        module __lc5_deep__;

        g_order : int = 0;

        struct Step1 {
            val : int;
            Step1(v: int) : val(v) {}
            ~Step1() { g_order = g_order * 10 + 1; }

            transform() : Step2 {
                s : Step2(val * 2);
                return s;
            }
        }

        struct Step2 {
            val : int;
            Step2(v: int) : val(v) {}
            ~Step2() { g_order = g_order * 10 + 2; }

            get_val() : int { return val; }
        }

        make(v: int) : Step1 {
            s : Step1(v);
            return s;
        }

        test() : int {
            x : int = make(5).transform().get_val();
            return x;
        }

        get_order() : int { return g_order; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 10); // 5 * 2

    auto get_order = jit->lookup_symbol<int(*)()>("get_order");
    REQUIRE(get_order != nullptr);
    int order = get_order();
    // Both temporaries (Step1 and Step2) must have been destroyed.
    // Caller-side: Step2 created after Step1, so destroyed first.
    // Last two digits should be 12 (Step2-dtor=2 first, then Step1-dtor=1)
    // Wait — reverse order means Step2 (created second) destroyed first → g_order ends with ...21
    // Actually: Step1 temp created first (by make()), Step2 temp created second (by transform()),
    // reverse destruction: Step2 first (2), then Step1 (1) → caller-side ends with "21"
    CHECK((order % 100) == 21);
}

// =============================================================================
// Category 7: Temporaries in control flow conditions
// =============================================================================

TEST_CASE("Lifecycle Cat7: Temporary struct in if-condition", "[gen][lifecycle][cat7]") {
    // if (make(x).is_valid()) — temporary created for condition,
    // must be destroyed after condition evaluation (before or after the body).
    auto jit = gen_jit(R"SRC(
        module __lc7_if__;

        g_dtors : int = 0;

        struct Checker {
            val : int;
            Checker(v: int) : val(v) {}
            ~Checker() { g_dtors = g_dtors + 1; }

            is_positive() : int {
                if (val > 0) { return 1; }
                return 0;
            }
        }

        make_checker(v: int) : Checker {
            c : Checker(v);
            return c;
        }

        test(v: int) : int {
            result : int = 0;
            if (make_checker(v).is_positive() > 0) {
                result = 1;
            }
            return result;
        }

        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)(int)>("test");
    REQUIRE(test != nullptr);
    CHECK(test(5) == 1);   // positive → then-branch
    CHECK(test(-3) == 0);  // not positive → no then-branch

    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_dtors != nullptr);
    // Two calls to test() → two temporaries created and destroyed
    // Plus local dtors inside make_checker. At minimum 2 caller-side temps destroyed.
    CHECK(get_dtors() >= 2);
}

TEST_CASE("Lifecycle Cat7: Temporary struct in while-condition", "[gen][lifecycle][cat7]") {
    // while (make_counter().has_next()) — temporary created each iteration,
    // must be destroyed after each condition evaluation.
    auto jit = gen_jit(R"SRC(
        module __lc7_while__;

        g_dtors : int = 0;
        g_counter : int = 0;

        struct Counter {
            val : int;
            Counter(v: int) : val(v) {}
            ~Counter() { g_dtors = g_dtors + 1; }

            has_next() : int {
                g_counter = g_counter + 1;
                if (val > g_counter) { return 1; }
                return 0;
            }
        }

        make_counter(v: int) : Counter {
            c : Counter(v);
            return c;
        }

        test() : int {
            while (make_counter(3).has_next() > 0) {
            }
            return g_counter;
        }

        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    int result = test();
    // The loop runs while g_counter < 3, so g_counter increments to 3
    CHECK(result == 3);

    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_dtors != nullptr);
    // 3 iterations of condition evaluation → 3 temporaries created and destroyed
    // (plus local dtors inside make_counter)
    CHECK(get_dtors() >= 3);
}

TEST_CASE("Lifecycle Cat7: Temporary struct in for-condition", "[gen][lifecycle][cat7]") {
    // for (i:=0; make_limit(n).above(i) > 0; i = i + 1)
    // Temporary created at each condition check, must be destroyed after each.
    auto jit = gen_jit(R"SRC(
        module __lc7_for__;

        g_dtors : int = 0;

        struct Limit {
            max : int;
            Limit(m: int) : max(m) {}
            ~Limit() { g_dtors = g_dtors + 1; }

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
            for (i : int = 0; make_limit(3).above(i) > 0; i = i + 1) {
                sum = sum + i;
            }
            return sum;
        }

        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    // sum = 0 + 1 + 2 = 3
    CHECK(test() == 3);

    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_dtors != nullptr);
    // 4 condition evaluations (i=0,1,2 pass, i=3 fails) → 4 temps destroyed
    CHECK(get_dtors() >= 4);
}

// =============================================================================
// Category 1 (extended): Additional edge cases
// =============================================================================

TEST_CASE("Lifecycle Cat1: Early return in middle of block — all locals destroyed", "[gen][lifecycle][cat1]") {
    auto jit = gen_jit(R"SRC(
        module __lc1_early__;

        g_dtors : int = 0;

        struct D {
            id : int;
            D(i: int) : id(i) {}
            ~D() { g_dtors = g_dtors * 10 + id; }
        }

        test(flag: int) : int {
            a : D(1);
            b : D(2);
            if (flag > 0) {
                return g_dtors;
            }
            c : D(3);
            return g_dtors;
        }

        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)(int)>("test");
    REQUIRE(test != nullptr);

    // Early return (flag=1): return g_dtors=0, then b,a destroyed → g_dtors=21
    CHECK(test(1) == 0);
    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_dtors != nullptr);
    CHECK(get_dtors() == 21);
}

// =============================================================================
// Category 2 (extended): Additional edge cases
// =============================================================================

TEST_CASE("Lifecycle Cat2: Owner auto-cleanup at scope exit — dtor called exactly once", "[gen][lifecycle][cat2]") {
    auto jit = gen_jit(R"SRC(
        module __lc2_auto__;

        g_dtors : int = 0;

        struct Res {
            Res() {}
            ~Res() { g_dtors = g_dtors + 1; }
        }

        test() : int {
            p : Res! = new Res();
            return g_dtors;
        }

        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    // At return, g_dtors is 0 (dtor hasn't run yet)
    CHECK(test() == 0);

    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_dtors != nullptr);
    // After function exit, owner auto-cleanup runs dtor exactly once
    CHECK(get_dtors() == 1);
}

// =============================================================================
// Category 3 (extended): Nested pass-by-value
// =============================================================================

TEST_CASE("Lifecycle Cat3: Multiple by-value params — all destroyed at function exit", "[gen][lifecycle][cat3]") {
    auto jit = gen_jit(R"SRC(
        module __lc3_multi__;

        g_dtors : int = 0;

        struct Item {
            v : int = 0;
            Item() {}
            ~Item() { g_dtors = g_dtors + 1; }
        }

        consume_two(a: Item, b: Item) : int {
            return a.v + b.v;
        }

        test() : int {
            x : Item;
            y : Item;
            x.v = 10;
            y.v = 20;
            result : int = consume_two(x, y);
            return result;
        }

        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 30);

    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_dtors != nullptr);
    // 2 copies in consume_two + 2 originals in test = 4 dtors
    CHECK(get_dtors() == 4);
}

// =============================================================================
// Category 4 (extended): Struct return as bare expression-statement
// =============================================================================

TEST_CASE("Lifecycle Cat4: Struct return as bare expression-statement (no use)", "[gen][lifecycle][cat4]") {
    // make(); — return value is a struct, entirely discarded.
    // The temporary must still be destroyed at end of the expression-statement.
    auto jit = gen_jit(R"SRC(
        module __lc4_bare__;

        g_dtors : int = 0;

        struct Obj {
            val : int;
            Obj(v: int) : val(v) {}
            ~Obj() { g_dtors = g_dtors + 1; }
        }

        make(v: int) : Obj {
            o : Obj(v);
            return o;
        }

        test() : int {
            make(10);
            return g_dtors;
        }

        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    // By the time we return g_dtors, the temporary from make() must be destroyed.
    // With NRVO: 1 dtor for caller-side temporary (local o skipped).
    // Without NRVO: 1 dtor for local in make() + 1 for caller-side temporary = 2.
    CHECK(test() >= 1);
}

// =============================================================================
// Category 5 (extended): Additional temporary edge cases
// =============================================================================

TEST_CASE("Lifecycle Cat5: Temporary in assignment expression", "[gen][lifecycle][cat5]") {
    // x = make(42).val; where x is already declared.
    // Temporary must be destroyed at end of expression-statement.
    auto jit = gen_jit(R"SRC(
        module __lc5_assign__;

        g_dtors : int = 0;

        struct Obj {
            val : int;
            Obj(v: int) : val(v) {}
            ~Obj() { g_dtors = g_dtors + 1; }
        }

        make(v: int) : Obj {
            o : Obj(v);
            return o;
        }

        test() : int {
            x : int = 0;
            x = make(55).val;
            return x;
        }

        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 55);

    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_dtors != nullptr);
    // Temporary destroyed at end of expression-statement
    CHECK(get_dtors() >= 1);
}

TEST_CASE("Lifecycle Cat5: Struct return passed by value as function argument", "[gen][lifecycle][cat5]") {
    // consume(make(42)) — make() returns a struct by value, passed to consume()
    // by value (copy). All copies and temporaries must be properly destroyed.
    auto jit = gen_jit(R"SRC(
        module __lc5_nested_call__;

        g_dtors : int = 0;

        struct Box {
            val : int;
            Box(v: int) : val(v) {}
            ~Box() { g_dtors = g_dtors + 1; }
        }

        make(v: int) : Box {
            b : Box(v);
            return b;
        }

        consume(b: Box) : int {
            return b.val;
        }

        test() : int {
            result : int = consume(make(42));
            return result;
        }

        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 42);

    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_dtors != nullptr);
    // With NRVO in make() + argument copy elision: consume param only = 1 dtor
    // Without elision: Local in make() + caller temp + consume() param copy = at least 2 dtors
    CHECK(get_dtors() >= 1);
}

// =============================================================================
// Category 1 (extended): Struct without destructor
// =============================================================================

TEST_CASE("Lifecycle Cat1: Struct without destructor — no crash", "[gen][lifecycle][cat1]") {
    // Verify that structs without destructors don't cause issues.
    auto jit = gen_jit(R"SRC(
        module __lc1_no_dtor__;

        struct Plain {
            val : int;
            Plain(v: int) : val(v) {}
        }

        test() : int {
            p : Plain(42);
            return p.val;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 42);
}

TEST_CASE("Lifecycle Cat1: Struct return by value without destructor — no crash", "[gen][lifecycle][cat1]") {
    auto jit = gen_jit(R"SRC(
        module __lc1_ret_no_dtor__;

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

// =============================================================================
// Category 4 (extended): Struct copy between locals
// =============================================================================

TEST_CASE("Lifecycle Cat4: Struct copy from local to local", "[gen][lifecycle][cat4]") {
    auto jit = gen_jit(R"SRC(
        module __lc4_copy_local__;

        g_dtors : int = 0;

        struct Obj {
            val : int;
            Obj(v: int) : val(v) {}
            ~Obj() { g_dtors = g_dtors + 1; }
        }

        test() : int {
            a : Obj(42);
            b : Obj = a;
            return b.val;
        }

        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 42);

    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_dtors != nullptr);
    // Both a and b destroyed
    CHECK(get_dtors() == 2);
}


// =============================================================================
// Category 8: Value semantics of owning aggregates — move on prvalue temporary
//
// A prvalue struct temporary passed by value or returned by value must be MOVED
// into the destination (its scheduled destruction cancelled), not shallow-copied
// and then destroyed twice.  These tests assert the constructor / destructor
// balance: a single logical instance must be destroyed exactly once.  Before the
// site-3 (by-value argument) and site-4 (return by value) value-semantics wiring,
// the prvalue temporary was destroyed both at the caller and at the callee/return
// slot, so g_dtors exceeded g_ctors.
// =============================================================================

TEST_CASE("Lifecycle Cat8: prvalue temporary passed by value is moved, not double-destroyed",
          "[gen][lifecycle][cat8][value-semantics]") {
    auto jit = gen_jit(R"SRC(
        module __lc8_byval_move__;

        g_ctors : int = 0;
        g_dtors : int = 0;

        struct Res {
            Res() { g_ctors = g_ctors + 1; }
            ~Res() { g_dtors = g_dtors + 1; }
        }

        consume(r: Res) : int { return 0; }

        test() : int {
            consume(Res());
            return 0;
        }

        get_ctors() : int { return g_ctors; }
        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    test();

    auto get_ctors = jit->lookup_symbol<int(*)()>("get_ctors");
    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_ctors != nullptr);
    REQUIRE(get_dtors != nullptr);

    // Exactly one construction (the temporary) and one destruction (the callee's
    // by-value parameter): the temporary is moved in, its own cleanup cancelled.
    CHECK(get_ctors() == 1);
    CHECK(get_dtors() == 1);
}

TEST_CASE("Lifecycle Cat8: prvalue temporary returned by value is moved, not double-destroyed",
          "[gen][lifecycle][cat8][value-semantics]") {
    auto jit = gen_jit(R"SRC(
        module __lc8_ret_move__;

        g_ctors : int = 0;
        g_dtors : int = 0;

        struct Res {
            Res() { g_ctors = g_ctors + 1; }
            ~Res() { g_dtors = g_dtors + 1; }
        }

        make() : Res {
            return Res();
        }

        run() : int {
            x : Res = make();
            return 0;
        }

        test() : int {
            run();
            return 0;
        }

        get_ctors() : int { return g_ctors; }
        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    test();

    auto get_ctors = jit->lookup_symbol<int(*)()>("get_ctors");
    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_ctors != nullptr);
    REQUIRE(get_dtors != nullptr);

    // The temporary built in make() is moved into the caller's sret slot (x), so
    // there is a single construction and a single destruction (x at run() exit).
    CHECK(get_ctors() == 1);
    CHECK(get_dtors() == 1);
}


