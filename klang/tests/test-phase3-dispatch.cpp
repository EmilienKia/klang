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
 * Tests for Phase 3: virtual_dispatch_info annotation in function_invocation_expression.
 *
 * type_reference_resolver now annotates every function_invocation_expression with a
 * virtual_dispatch_info descriptor that records:
 *  - dispatch_kind::DIRECT  — non-virtual call, static call, or qualified bypass
 *  - dispatch_kind::VTABLE  — vtable dispatch, with slot_index and dispatch_class
 *
 * Tests inspect the AST model directly (no JIT) by traversing the unit after a full
 * compile_model() run.
 *
 * Test catalogue:
 *  [A] Free (non-member) function call → DIRECT
 *  [B] Non-virtual struct method call   → DIRECT
 *  [C] Virtual class method call (single inheritance, base ref) → VTABLE, slot_index=0
 *  [D] Qualified call Base::method(d) → DIRECT (bypasses vtable)
 *  [E] Abstract method call via abstract class ref → VTABLE, slot_index correct
 *  [F] Virtual method call, slot_index > 0 (second virtual method in vtable)
 *  [G] Virtual call via secondary base reference (multiple inheritance) → VTABLE
 *  [H] Runtime: virtual dispatch via dispatch_info still produces correct results (JIT)
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

using dispatch_kind = k::model::virtual_dispatch_info::dispatch_kind;

// ════════════════════════════════════════════════════════════════════════════
//  [A] Free function call → DIRECT
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[A] Phase3: free function call is annotated DIRECT", "[phase3][dispatch][direct]") {
    auto comp = compile_model(R"SRC(
        module __p3_a__;
        helper() : int { return 42; }
        caller() : int { return helper(); }
    )SRC");
    REQUIRE(comp != nullptr);

    auto invocations = collect_invocations_in(comp, "caller");
    REQUIRE(!invocations.empty());

    // The call to helper() is a free function — must be DIRECT
    bool found = false;
    for (auto* inv : invocations) {
        REQUIRE(inv->has_dispatch_info());
        CHECK(inv->get_dispatch_info().kind == dispatch_kind::DIRECT);
        found = true;
    }
    CHECK(found);
}

// ════════════════════════════════════════════════════════════════════════════
//  [B] Non-virtual struct method call → DIRECT
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[B] Phase3: non-virtual struct method call is annotated DIRECT", "[phase3][dispatch][direct]") {
    auto comp = compile_model(R"SRC(
        module __p3_b__;
        struct Point {
            public x : int;
            get_x() : int { return this.x; }
        }
        test(p : Point&) : int { return p.get_x(); }
    )SRC");
    REQUIRE(comp != nullptr);

    auto invocations = collect_invocations_in(comp, "test");
    REQUIRE(!invocations.empty());

    bool found = false;
    for (auto* inv : invocations) {
        REQUIRE(inv->has_dispatch_info());
        CHECK(inv->get_dispatch_info().kind == dispatch_kind::DIRECT);
        found = true;
    }
    CHECK(found);
}

// ════════════════════════════════════════════════════════════════════════════
//  [C] Virtual class method call via base reference → VTABLE, slot_index=0
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[C] Phase3: virtual call via base ref → VTABLE with slot_index=0", "[phase3][dispatch][vtable]") {
    auto comp = compile_model(R"SRC(
        module __p3_c__;
        class Animal {
            speak() : int { return 0; }
        }
        class Dog : Animal {
            speak() : int { return 7; }
        }
        dispatch(a : Animal&) : int { return a.speak(); }
    )SRC");
    REQUIRE(comp != nullptr);

    auto invocations = collect_invocations_in(comp, "dispatch");
    REQUIRE(!invocations.empty());

    bool found_vtable = false;
    for (auto* inv : invocations) {
        REQUIRE(inv->has_dispatch_info());
        const auto& di = inv->get_dispatch_info();
        if (di.kind == dispatch_kind::VTABLE) {
            found_vtable = true;
            CHECK(di.slot_index == 0);
            REQUIRE(di.dispatch_class != nullptr);
            CHECK(di.dispatch_class->get_short_name() == "Animal");
        }
    }
    CHECK(found_vtable);
}

// ════════════════════════════════════════════════════════════════════════════
//  [D] Qualified call Base::method(d) → DIRECT (bypasses vtable)
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[D] Phase3: qualified call bypasses vtable → DIRECT", "[phase3][dispatch][direct]") {
    auto comp = compile_model(R"SRC(
        module __p3_d__;
        class Base {
            value() : int { return 1; }
        }
        class Derived : Base {
            value() : int { return 2; }
        }
        test(d : Derived&) : int {
            return Base::value(d);
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto invocations = collect_invocations_in(comp, "test");
    REQUIRE(!invocations.empty());

    bool found = false;
    for (auto* inv : invocations) {
        REQUIRE(inv->has_dispatch_info());
        CHECK(inv->get_dispatch_info().kind == dispatch_kind::DIRECT);
        found = true;
    }
    CHECK(found);
}

// ════════════════════════════════════════════════════════════════════════════
//  [E] Abstract method call via abstract class reference → VTABLE
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[E] Phase3: abstract method call via base ref → VTABLE", "[phase3][dispatch][vtable][abstract]") {
    auto comp = compile_model(R"SRC(
        module __p3_e__;
        abstract class Shape {
            abstract area() : int;
        }
        class Circle : Shape {
            area() : int { return 314; }
        }
        measure(s : Shape&) : int { return s.area(); }
    )SRC");
    REQUIRE(comp != nullptr);

    auto invocations = collect_invocations_in(comp, "measure");
    REQUIRE(!invocations.empty());

    bool found_vtable = false;
    for (auto* inv : invocations) {
        REQUIRE(inv->has_dispatch_info());
        const auto& di = inv->get_dispatch_info();
        if (di.kind == dispatch_kind::VTABLE) {
            found_vtable = true;
            CHECK(di.slot_index == 0);
            REQUIRE(di.dispatch_class != nullptr);
            CHECK(di.dispatch_class->get_short_name() == "Shape");
        }
    }
    CHECK(found_vtable);
}

// ════════════════════════════════════════════════════════════════════════════
//  [F] Virtual method with slot_index > 0 (second method in vtable)
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[F] Phase3: second virtual method has slot_index=1", "[phase3][dispatch][vtable]") {
    auto comp = compile_model(R"SRC(
        module __p3_f__;
        class Vehicle {
            start() : int { return 1; }
            stop()  : int { return 0; }
        }
        test_stop(v : Vehicle&) : int { return v.stop(); }
        test_start(v : Vehicle&) : int { return v.start(); }
    )SRC");
    REQUIRE(comp != nullptr);

    {
        auto invocations = collect_invocations_in(comp, "test_stop");
        REQUIRE(!invocations.empty());
        bool found = false;
        for (auto* inv : invocations) {
            REQUIRE(inv->has_dispatch_info());
            const auto& di = inv->get_dispatch_info();
            if (di.kind == dispatch_kind::VTABLE) {
                found = true;
                CHECK(di.slot_index == 1);  // stop() is declared second → slot 1
            }
        }
        CHECK(found);
    }
    {
        auto invocations = collect_invocations_in(comp, "test_start");
        REQUIRE(!invocations.empty());
        bool found = false;
        for (auto* inv : invocations) {
            REQUIRE(inv->has_dispatch_info());
            const auto& di = inv->get_dispatch_info();
            if (di.kind == dispatch_kind::VTABLE) {
                found = true;
                CHECK(di.slot_index == 0);  // start() is declared first → slot 0
            }
        }
        CHECK(found);
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  [G] Virtual call via secondary base reference → VTABLE, dispatch_class = secondary base
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[G] Phase3: virtual call via secondary base ref → VTABLE with correct dispatch_class",
          "[phase3][dispatch][vtable][multiple_inheritance]") {
    auto comp = compile_model(R"SRC(
        module __p3_g__;
        class B {
            b_val() : int { return 10; }
        }
        class C {
            c_val() : int { return 20; }
        }
        class D : B, C {}
        dispatch_c(c : C&) : int { return c.c_val(); }
    )SRC");
    REQUIRE(comp != nullptr);

    auto invocations = collect_invocations_in(comp, "dispatch_c");
    REQUIRE(!invocations.empty());

    bool found_vtable = false;
    for (auto* inv : invocations) {
        REQUIRE(inv->has_dispatch_info());
        const auto& di = inv->get_dispatch_info();
        if (di.kind == dispatch_kind::VTABLE) {
            found_vtable = true;
            CHECK(di.slot_index == 0);
            REQUIRE(di.dispatch_class != nullptr);
            // The call is through C& — dispatch_class must be C
            CHECK(di.dispatch_class->get_short_name() == "C");
        }
    }
    CHECK(found_vtable);
}

// ════════════════════════════════════════════════════════════════════════════
//  [H] Runtime JIT: dispatch_info annotation doesn't break existing dispatch
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[H] Phase3: runtime JIT — virtual dispatch via annotated dispatch_info works",
          "[phase3][dispatch][runtime]") {
    auto jit = gen_jit(R"SRC(
        module __p3_h__;
        abstract class Shape {
            abstract area() : int;
        }
        class Square : Shape {
            public side : int;
            Square(s : int) : side(s) {}
            area() : int { return this.side * this.side; }
        }
        measure(s : Shape&) : int { return s.area(); }
        test() : int {
            sq : Square(5);
            return measure(sq);
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 25);  // 5 * 5
}

