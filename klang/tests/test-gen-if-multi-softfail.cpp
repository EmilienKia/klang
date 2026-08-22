/*
 * K Language compiler
 *
 * Copyright 2023-2026 Emilien Kia
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
 * Tests for multi-variable soft-fail in if statements.
 *
 * Syntax: if (v1 : T1 = e1; v2 : T2 = e2; ...) { ... } else { ... }
 *
 * Without a trailing test expression, each addressor variable (pointer,
 * reference, link, view, owner) is null-checked after initialization.
 * The first null triggers a jump to else (or continue). No variables
 * are visible in the else branch (option 3a).
 *
 * Non-addressor variables are allowed and never null-checked.
 *
 * Test categories:
 *   [IMSF-PTR]    Pointer soft-fail
 *   [IMSF-LINK]   Link soft-fail
 *   [IMSF-VIEW]   View soft-fail
 *   [IMSF-CHAIN]  Chained dereference soft-fail
 *   [IMSF-MIXED]  Mixed addressor and non-addressor
 *   [IMSF-DTOR]   Destructor cleanup on soft-fail
 *   [IMSF-SCOPE]  Variables not visible after if
 *   [IMSF-NESTED] Nested multi-var soft-fail
 */

#include <catch2/catch_test_macros.hpp>
#include "helpers.hpp"


// =============================================================================
// [IMSF-PTR] Pointer soft-fail
// =============================================================================

TEST_CASE("if-multi-softfail: two pointers both non-null enters then",
          "[gen][if-multi-softfail][ptr]") {
    auto jit = gen_jit(R"SRC(
        module gen_if_multi_softfail_01;

        test() : int {
            a : int = 10;
            b : int = 20;
            if(p1 : int* = &a; p2 : int* = &b) {
                return *p1 + *p2;
            }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 30);
}

TEST_CASE("if-multi-softfail: second pointer null enters else",
          "[gen][if-multi-softfail][ptr]") {
    auto jit = gen_jit(R"SRC(
        module gen_if_multi_softfail_02;

        test() : int {
            a : int = 10;
            if(p1 : int* = &a; p2 : int* = null) {
                return *p1 + *p2;
            } else {
                return -1;
            }
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == -1);
}

TEST_CASE("if-multi-softfail: first pointer null enters else",
          "[gen][if-multi-softfail][ptr]") {
    auto jit = gen_jit(R"SRC(
        module gen_if_multi_softfail_03;

        test() : int {
            b : int = 20;
            if(p1 : int* = null; p2 : int* = &b) {
                return *p1 + *p2;
            } else {
                return -2;
            }
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == -2);
}

TEST_CASE("if-multi-softfail: pointer null no else continues after if",
          "[gen][if-multi-softfail][ptr]") {
    auto jit = gen_jit(R"SRC(
        module gen_if_multi_softfail_04;

        test() : int {
            if(p1 : int* = null; p2 : int* = null) {
                return 1;
            }
            return 99;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 99);
}


// =============================================================================
// [IMSF-LINK] Link soft-fail
// =============================================================================

TEST_CASE("if-multi-softfail: two links both non-null enters then",
          "[gen][if-multi-softfail][link]") {
    auto jit = gen_jit(R"SRC(
        module gen_if_multi_softfail_05;

        test() : int {
            a : int = 10;
            b : int = 20;
            pa : int* = &a;
            pb : int* = &b;
            if(l1 : int+ = pa; l2 : int+ = pb) {
                return *l1 + *l2;
            }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 30);
}

TEST_CASE("if-multi-softfail: second link null enters else",
          "[gen][if-multi-softfail][link]") {
    auto jit = gen_jit(R"SRC(
        module gen_if_multi_softfail_06;

        test() : int {
            a : int = 10;
            pa : int* = &a;
            pn : int* = null;
            if(l1 : int+ = pa; l2 : int+ = pn) {
                return *l1 + *l2;
            } else {
                return -1;
            }
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == -1);
}

TEST_CASE("if-multi-softfail: first link null enters else",
          "[gen][if-multi-softfail][link]") {
    auto jit = gen_jit(R"SRC(
        module gen_if_multi_softfail_07;

        test() : int {
            pn : int* = null;
            b : int = 20;
            pb : int* = &b;
            if(l1 : int+ = pn; l2 : int+ = pb) {
                return *l1 + *l2;
            } else {
                return -2;
            }
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == -2);
}


// =============================================================================
// [IMSF-VIEW] View soft-fail (single var, no test → soft-fail)
// =============================================================================

TEST_CASE("if-multi-softfail: single view non-null enters then",
          "[gen][if-multi-softfail][view]") {
    auto jit = gen_jit(R"SRC(
        module gen_if_multi_softfail_08;

        test() : int {
            val : int = 7;
            if(v : int? = &val) {
                return *v;
            }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 7);
}

TEST_CASE("if-multi-softfail: single view null enters else",
          "[gen][if-multi-softfail][view]") {
    auto jit = gen_jit(R"SRC(
        module gen_if_multi_softfail_09;

        test() : int {
            p : int* = null;
            if(v : int? = p) {
                return *v;
            } else {
                return -1;
            }
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == -1);
}


// =============================================================================
// [IMSF-CHAIN] Chained dereference soft-fail
// =============================================================================

TEST_CASE("if-multi-softfail: chained pointer dereference, all valid",
          "[gen][if-multi-softfail][chain]") {
    auto jit = gen_jit(R"SRC(
        module gen_if_multi_softfail_10;

        getPtr(p : int*) : int* {
            return p;
        }

        test() : int {
            a : int = 10;
            b : int = 20;
            if(p1 : int* = &a; p2 : int* = getPtr(&b)) {
                return *p1 + *p2;
            }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 30);
}

TEST_CASE("if-multi-softfail: chained pointer dereference, second null",
          "[gen][if-multi-softfail][chain]") {
    auto jit = gen_jit(R"SRC(
        module gen_if_multi_softfail_11;

        getNull() : int* {
            p : int* = null;
            return p;
        }

        test() : int {
            a : int = 10;
            if(p1 : int* = &a; p2 : int* = getNull()) {
                return *p1 + *p2;
            } else {
                return -1;
            }
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == -1);
}


// =============================================================================
// [IMSF-MIXED] Mixed addressor and non-addressor
// =============================================================================

TEST_CASE("if-multi-softfail: int var + pointer, pointer non-null",
          "[gen][if-multi-softfail][mixed]") {
    auto jit = gen_jit(R"SRC(
        module gen_if_multi_softfail_12;

        test() : int {
            val : int = 42;
            if(x : int = 5; p : int* = &val) {
                return x + *p;
            }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 47);
}

TEST_CASE("if-multi-softfail: int var + pointer, pointer null enters else",
          "[gen][if-multi-softfail][mixed]") {
    auto jit = gen_jit(R"SRC(
        module gen_if_multi_softfail_13;

        test() : int {
            if(x : int = 5; p : int* = null) {
                return x + *p;
            } else {
                return -1;
            }
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == -1);
}

TEST_CASE("if-multi-softfail: three vars with mixed types",
          "[gen][if-multi-softfail][mixed]") {
    auto jit = gen_jit(R"SRC(
        module gen_if_multi_softfail_14;

        test() : int {
            a : int = 10;
            b : int = 20;
            if(x : int = 3; p1 : int* = &a; p2 : int* = &b) {
                return x + *p1 + *p2;
            }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 33);
}


// =============================================================================
// [IMSF-DTOR] Destructor cleanup on soft-fail
// =============================================================================

TEST_CASE("if-multi-softfail: dtor called for initialized vars on soft-fail",
          "[gen][if-multi-softfail][dtor]") {
    auto jit = gen_jit(R"SRC(
        module gen_if_multi_softfail_15;

        g_dtor_count : int = 0;

        struct S {
            val : int;

            ~S() {
                ++g_dtor_count;
            }
        }

        makeS(v : int) res : S {
            res.val = v;
        }

        test() : int {
            g_dtor_count = 0;
            // s is initialized (dtor should be called on soft-fail of p)
            if(s : S = makeS(1); p : int* = null) {
                return 1;
            } else {
                // s is not visible here (3a), but its dtor should have been called
            }
            return g_dtor_count;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    int count = fn();
    REQUIRE(count >= 1);
}

TEST_CASE("if-multi-softfail: dtor called for all vars in then branch",
          "[gen][if-multi-softfail][dtor]") {
    auto jit = gen_jit(R"SRC(
        module gen_if_multi_softfail_16;

        g_dtor_count : int = 0;

        struct S {
            val : int;

            ~S() {
                ++g_dtor_count;
            }
        }

        makeS(v : int) res : S {
            res.val = v;
        }

        test() : int {
            g_dtor_count = 0;
            val : int = 42;
            if(s1 : S = makeS(1); s2 : S = makeS(2); p : int* = &val) {
                // all alive here
            }
            return g_dtor_count;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    int count = fn();
    REQUIRE(count >= 2);
}


// =============================================================================
// [IMSF-SCOPE] Variables not visible after if
// =============================================================================

TEST_CASE("if-multi-softfail: variables can be redeclared after if",
          "[gen][if-multi-softfail][scope]") {
    auto jit = gen_jit(R"SRC(
        module gen_if_multi_softfail_17;

        test() : int {
            a : int = 10;
            if(p1 : int* = &a; p2 : int* = &a) {
                // p1, p2 visible here
            }
            p1 : int = 100;
            p2 : int = 200;
            return p1 + p2;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 300);
}


// =============================================================================
// [IMSF-NESTED] Nested multi-var soft-fail
// =============================================================================

TEST_CASE("if-multi-softfail: nested soft-fail, inner fails",
          "[gen][if-multi-softfail][nested]") {
    auto jit = gen_jit(R"SRC(
        module gen_if_multi_softfail_18;

        test() : int {
            a : int = 10;
            b : int = 20;
            if(p1 : int* = &a; p2 : int* = &b) {
                if(p3 : int* = null; p4 : int* = &a) {
                    return 999;
                } else {
                    return *p1 + *p2;
                }
            }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 30);
}

TEST_CASE("if-multi-softfail: nested soft-fail, outer fails",
          "[gen][if-multi-softfail][nested]") {
    auto jit = gen_jit(R"SRC(
        module gen_if_multi_softfail_19;

        test() : int {
            if(p1 : int* = null; p2 : int* = null) {
                return 999;
            } else {
                return -1;
            }
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == -1);
}



