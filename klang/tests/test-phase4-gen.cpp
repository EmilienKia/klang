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
 * Tests for Phase 4: code-generation adaptations.
 *
 * Phase 4 goals:
 *  1. Remove the legacy fallback in implementation_generator::visit_function_invocation_expression
 *     — dispatch is now driven exclusively by virtual_dispatch_info (set in Phase 3).
 *  2. Verify that secondary-vtable thunk LLVM functions are generated correctly
 *     (naming convention, this-adjustment arithmetic).
 *  3. Confirm that the full pipeline (model_materializer → thunk generation →
 *     virtual dispatch via dispatch_info) produces correct runtime results.
 *
 * Test catalogue:
 *  [A] Direct call through dispatch_info DIRECT path — still works (non-regression)
 *  [B] VTABLE dispatch: override in single-inheritance via base ref → correct value
 *  [C] Thunk function generated for secondary base override (multiple inheritance)
 *  [D] Runtime: dispatch via primary-base ref → correct value (single inheritance)
 *  [E] Runtime: dispatch via secondary-base ref → correct value (multiple inheritance)
 *  [F] Runtime: qualified call (bypass vtable) → calls exact base method
 *  [G] Runtime: abstract method via base ref → concrete override reached
 *  [H] Runtime: multiple virtual methods — both slots dispatch to correct override
 *  [I] Non-regression: fibo recursive call still works (direct call, not virtual)
 *  [J] Non-regression: main() wrapper (global_main_function) still works
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// [A] Direct call via dispatch_info DIRECT — non-regression
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("[A] Phase4: direct (non-virtual) call still works", "[phase4][gen][direct]") {
    auto jit = gen_jit(R"SRC(
        module __p4_a__;
        add(x: int, y: int) : int { return x + y; }
        test() : int { return add(3, 4); }
    )SRC");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 7);
}

// ─────────────────────────────────────────────────────────────────────────────
// [B] VTABLE dispatch: single inheritance override via base ref
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("[B] Phase4: single-inheritance virtual dispatch via base ref", "[phase4][gen][vtable]") {
    auto jit = gen_jit(R"SRC(
        module __p4_b__;
        class Animal {
            speak() : int { return 0; }
        }
        class Cat : Animal {
            speak() : int { return 5; }
        }
        class Dog : Animal {
            speak() : int { return 7; }
        }
        dispatch(a: Animal&) : int { return a.speak(); }
        test() : int {
            d: Dog;
            return dispatch(d);
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 7);
}

// ─────────────────────────────────────────────────────────────────────────────
// [C] Thunk: secondary-base override dispatches to D's override via thunk
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("[C] Phase4: thunk allows secondary-base override to reach D::speak()",
          "[phase4][gen][thunk]") {
    // D : B, C — D overrides speak() declared in C.
    // When dispatching through C&, a this-adjustment thunk must be used to
    // convert the C* into a D* before calling D::speak().
    auto jit = gen_jit(R"SRC(
        module __p4_c__;
        class B {
            val() : int { return 10; }
        }
        class C {
            speak() : int { return 20; }
        }
        class D : B, C {
            speak() : int { return 42; }
        }
        dispatch_c(c: C&) : int { return c.speak(); }
        dispatch_b(b: B&) : int { return b.val(); }
        test() : int {
            d: D;
            return dispatch_c(d) + dispatch_b(d);
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    // dispatch_c(d) → thunk → D::speak() = 42
    // dispatch_b(d) → B::val() = 10  (no override in D, no thunk needed)
    CHECK(fn() == 52);
}

// ─────────────────────────────────────────────────────────────────────────────
// [D] Runtime: dispatch via primary-base ref (single inheritance)
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("[D] Phase4: runtime dispatch via primary-base ref", "[phase4][gen][runtime]") {
    auto jit = gen_jit(R"SRC(
        module __p4_d__;
        class Shape {
            area() : int { return 0; }
        }
        class Rect : Shape {
            public w: int;
            public h: int;
            Rect(w: int, h: int) : w(w), h(h) {}
            area() : int { return this.w * this.h; }
        }
        measure(s: Shape&) : int { return s.area(); }
        test() : int {
            r: Rect(4, 5);
            return measure(r);
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 20);  // 4 * 5
}

// ─────────────────────────────────────────────────────────────────────────────
// [E] Runtime: dispatch via secondary-base ref (multiple inheritance)
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("[E] Phase4: runtime dispatch via secondary-base ref (multiple inheritance)",
          "[phase4][gen][runtime][multiple_inheritance]") {
    // D : B, C — dispatch through C& reaches D::value() = 99
    auto jit = gen_jit(R"SRC(
        module __p4_e__;
        class B {
            b_val() : int { return 10; }
        }
        class C {
            c_val() : int { return 20; }
        }
        class D : B, C {
            c_val() : int { return 99; }
        }
        dispatch_b(b: B&) : int { return b.b_val(); }
        dispatch_c(c: C&) : int { return c.c_val(); }
        test_b() : int { d: D; return dispatch_b(d); }
        test_c() : int { d: D; return dispatch_c(d); }
    )SRC");
    REQUIRE(jit != nullptr);

    {
        auto fn = jit->lookup_symbol<int(*)()>("test_b");
        REQUIRE(fn != nullptr);
        CHECK(fn() == 10);   // B::b_val, no override in D
    }
    {
        auto fn = jit->lookup_symbol<int(*)()>("test_c");
        REQUIRE(fn != nullptr);
        CHECK(fn() == 99);   // D::c_val overrides C::c_val, thunk adjusts this
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// [F] Runtime: qualified call bypasses vtable
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("[F] Phase4: qualified call bypasses vtable — calls exact base method",
          "[phase4][gen][runtime][direct]") {
    auto jit = gen_jit(R"SRC(
        module __p4_f__;
        class Base {
            value() : int { return 1; }
        }
        class Derived : Base {
            value() : int { return 2; }
        }
        test_virtual(d: Base&) : int { return d.value(); }
        test_direct(d: Derived&) : int { return Base::value(d); }
        test() : int {
            d: Derived;
            v: int = test_virtual(d);
            q: int = test_direct(d);
            return v * 10 + q;
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    // test_virtual → vtable → Derived::value() = 2
    // test_direct  → direct → Base::value() = 1
    CHECK(fn() == 21);
}

// ─────────────────────────────────────────────────────────────────────────────
// [G] Runtime: abstract method via base ref → concrete override reached
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("[G] Phase4: abstract method dispatch reaches concrete override",
          "[phase4][gen][runtime][abstract]") {
    auto jit = gen_jit(R"SRC(
        module __p4_g__;
        abstract class Processor {
            abstract process(x: int) : int;
        }
        class Doubler : Processor {
            process(x: int) : int { return x * 2; }
        }
        class Tripler : Processor {
            process(x: int) : int { return x * 3; }
        }
        run(p: Processor&, x: int) : int { return p.process(x); }
        test() : int {
            d: Doubler;
            t: Tripler;
            return run(d, 5) + run(t, 4);
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 22);  // Doubler: 5*2=10, Tripler: 4*3=12, sum=22
}

// ─────────────────────────────────────────────────────────────────────────────
// [H] Runtime: multiple virtual methods — both slots dispatch correctly
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("[H] Phase4: multiple virtual methods dispatch to correct slots",
          "[phase4][gen][runtime]") {
    auto jit = gen_jit(R"SRC(
        module __p4_h__;
        class Vehicle {
            start() : int { return 1; }
            stop()  : int { return 0; }
        }
        class Car : Vehicle {
            start() : int { return 100; }
            stop()  : int { return 200; }
        }
        do_start(v: Vehicle&) : int { return v.start(); }
        do_stop(v: Vehicle&)  : int { return v.stop(); }
        test() : int {
            c: Car;
            return do_start(c) + do_stop(c);
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 300);  // 100 + 200
}

// ─────────────────────────────────────────────────────────────────────────────
// [I] Non-regression: recursive direct call (Fibonacci)
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("[I] Phase4: non-regression — recursive direct call (fibo)",
          "[phase4][gen][direct][nonreg]") {
    auto jit = gen_jit(R"SRC(
        module __p4_i__;
        fibo(n: int) : int {
            if (n <= 1) return n;
            return fibo(n - 1) + fibo(n - 2);
        }
        test() : int { return fibo(10); }
    )SRC");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 55);  // fibo(10) = 55
}

// ─────────────────────────────────────────────────────────────────────────────
// [J] Non-regression: global_main_function wrapper works correctly
// ─────────────────────────────────────────────────────────────────────────────
TEST_CASE("[J] Phase4: non-regression — main() wrapper (global_main_function)",
          "[phase4][gen][direct][nonreg]") {
    auto result = build_and_exec(R"SRC(
        module __p4_j__;
        compute() : int { return 42; }
        main() : int { return compute(); }
    )SRC");
    CHECK(result.exit_code == 42);
}

