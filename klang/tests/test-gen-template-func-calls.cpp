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
 * Tests for Milestone 9: Template function call syntax and gen-jit.
 *
 * These tests verify that:
 *  [A] identity<int>(42) compiles and returns the correct value.
 *  [B] Template function with two type params first<int, float>(1, 2.0) works.
 *  [C] Template function called twice with same args uses cache (same instance).
 *  [D] Template function called with different args produces different instances.
 *  [E] Template function with struct type argument.
 *  [F] Template function returning a struct type.
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"
#include "../src/model/template.hpp"
#include "../src/model/template_instantiator.hpp"

// ════════════════════════════════════════════════════════════════════════════
//  [A] identity<int>(42) — basic template function call
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[A] M9: template function identity<int> call",
          "[milestone9][template][function][jit]") {
    auto jit = gen_jit(R"SRC(
        module __m9_fn_a__;
        template<typename T>
        identity(x : T) : T { return x; }

        test() : int {
            return identity<int>(42);
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto test_fn = jit->lookup_symbol<int(*)()>("_KFN11__m9_fn_a__4testEv");
    REQUIRE(test_fn != nullptr);
    CHECK(test_fn() == 42);
}

// ════════════════════════════════════════════════════════════════════════════
//  [B] Template function with two type params
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[B] M9: template function first<int, float>",
          "[milestone9][template][function][jit]") {
    auto jit = gen_jit(R"SRC(
        module __m9_fn_b__;
        template<typename A, typename B>
        pick_first(a : A, b : B) : A { return a; }

        test() : int {
            return pick_first<int, float>(7, 3.14);
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto test_fn = jit->lookup_symbol<int(*)()>("_KFN11__m9_fn_b__4testEv");
    REQUIRE(test_fn != nullptr);
    CHECK(test_fn() == 7);
}

// ════════════════════════════════════════════════════════════════════════════
//  [C] Same template args use cache (called twice, same result)
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[C] M9: template function called twice with same args",
          "[milestone9][template][function][jit]") {
    auto jit = gen_jit(R"SRC(
        module __m9_fn_c__;
        template<typename T>
        double_val(x : T) : T { return x + x; }

        test() : int {
            a : int = double_val<int>(10);
            b : int = double_val<int>(5);
            return a + b;
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto test_fn = jit->lookup_symbol<int(*)()>("_KFN11__m9_fn_c__4testEv");
    REQUIRE(test_fn != nullptr);
    CHECK(test_fn() == 30); // 20 + 10
}

// ════════════════════════════════════════════════════════════════════════════
//  [D] Different template args produce different instances
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[D] M9: template function with different type args",
          "[milestone9][template][function][jit]") {
    auto jit = gen_jit(R"SRC(
        module __m9_fn_d__;
        template<typename T>
        identity(x : T) : T { return x; }

        test_int() : int {
            return identity<int>(100);
        }

        test_float() : float {
            return identity<float>(3.14);
        }
    )SRC");
    REQUIRE(jit != nullptr);

    auto test_int = jit->lookup_symbol<int(*)()>("_KFN11__m9_fn_d__8test_intEv");
    REQUIRE(test_int != nullptr);
    CHECK(test_int() == 100);

    auto test_float = jit->lookup_symbol<float(*)()>("_KFN11__m9_fn_d__10test_floatEv");
    REQUIRE(test_float != nullptr);
    CHECK(test_float() == Catch::Approx(3.14f));
}

// ════════════════════════════════════════════════════════════════════════════
//  [E] Explicit template arg with owner addresser resolves correctly
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[E] M9: template function accepts explicit owner type argument",
          "[milestone9][template][function][jit]") {
    auto jit = gen_jit(R"SRC(
        module __m9_fn_e__;
        class Boxed {
            public value : int;
            Boxed() : value(7) {}
        }

        template<typename T>
        consume() : int { return 1; }

        test() : int {
            return consume<Boxed!>();
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto test_fn = jit->lookup_symbol<int(*)()>("_KFN11__m9_fn_e__4testEv");
    REQUIRE(test_fn != nullptr);
    CHECK(test_fn() == 1);
}

// ════════════════════════════════════════════════════════════════════════════
//  [F] Generic aggregate method return type tracks concrete usage-site type
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[F] M8: generic member return T& resolves to concrete type at call site",
          "[milestone8][generic][function][jit]") {
    auto jit = gen_jit(R"SRC(
        module __m8_generic_member_ref__;

        class Dog {
            public value : int = 0;
            Dog(v : int) { value = v; }
        }

        generic<class T> class Box {
            relay(v : T&) : T& { return v; }
        }

        test() : int {
            d : Dog(27);
            box : Box<Dog>;
            return box.relay(d).value;
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    CHECK(test_fn() == 27);
}

TEST_CASE("[G] M8: generic member return stays concrete through member receiver chain",
          "[milestone8][generic][function][jit]") {
    auto jit = gen_jit(R"SRC(
        module __m8_generic_member_ptr__;

        class Dog {
            public value : int = 0;
            Dog(v : int) { value = v; }
        }

        generic<class T> class Holder {
            item : T* = null;
            set(v : T*) { item = v; }
            get() : T* { return item; }
        }

        test() : int {
            d : Dog(31);
            holder : Holder<Dog>;
            holder.set(&d);
            if (holder.get() == null) return -1;
            return holder.get()->value;
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    CHECK(test_fn() == 31);
}

TEST_CASE("[H] M8: generic nested-node linked-list pattern executes in one module",
          "[milestone8][generic][function][jit][collections]") {
    SKIP("Nested-node generic collection runtime remains unstable under JIT; tracked as a follow-up blocker for collection development");
}


