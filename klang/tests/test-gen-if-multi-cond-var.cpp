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
 * Tests for if(var1; var2; ...; test) form — multiple condition variables.
 *
 * Syntax: if (v1 : T1 = e1; v2 : T2 = e2; ...; test_expr) { ... } else { ... }
 *
 * All variables are declared in order (left to right), later ones can
 * reference earlier ones. The test expression determines branching.
 * No soft-fail behavior. All variables are scoped to the if statement
 * and accessible in both then and else branches.
 *
 * Test categories:
 *   [IMCV-BASIC]   Two int vars + test
 *   [IMCV-THREE]   Three vars with forward references
 *   [IMCV-ELSE]    All vars accessible in else branch
 *   [IMCV-STRUCT]  Struct vars with constructor init
 *   [IMCV-BRACE]   Brace-init form
 *   [IMCV-DTOR]    Destructors called for all vars in both branches
 *   [IMCV-SCOPE]   Variables not visible after if
 *   [IMCV-NESTED]  Nested multi-var if
 *   [IMCV-SINGLE]  Single var backward compatibility
 */

#include <catch2/catch_test_macros.hpp>
#include "helpers.hpp"


// =============================================================================
// [IMCV-BASIC] Two int vars + test
// =============================================================================

TEST_CASE("if-multi-cond-var: two int vars, test true",
          "[gen][if-multi-cond-var][basic]") {
    auto jit = gen_jit(R"SRC(
        module __imcv_basic1__;

        test() : int {
            if(a : int = 3; b : int = a + 1; b > 3) {
                return a + b;
            }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 7);
}

TEST_CASE("if-multi-cond-var: two int vars, test false enters else",
          "[gen][if-multi-cond-var][basic]") {
    auto jit = gen_jit(R"SRC(
        module __imcv_basic2__;

        test() : int {
            if(a : int = 3; b : int = a + 1; b > 10) {
                return a + b;
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
// [IMCV-THREE] Three vars with forward references
// =============================================================================

TEST_CASE("if-multi-cond-var: three vars with chain references",
          "[gen][if-multi-cond-var][three]") {
    auto jit = gen_jit(R"SRC(
        module __imcv_three1__;

        test() : int {
            if(a : int = 1; b : int = a + 1; c : int = b + 1; c == 3) {
                return a + b + c;
            }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 6);
}


// =============================================================================
// [IMCV-ELSE] All vars accessible in else branch
// =============================================================================

TEST_CASE("if-multi-cond-var: all vars accessible in else",
          "[gen][if-multi-cond-var][else]") {
    auto jit = gen_jit(R"SRC(
        module __imcv_else1__;

        test() : int {
            if(a : int = 10; b : int = 20; a + b > 100) {
                return 1;
            } else {
                return a + b;
            }
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 30);
}


// =============================================================================
// [IMCV-STRUCT] Struct vars with constructor init
// =============================================================================

TEST_CASE("if-multi-cond-var: struct vars with constructor",
          "[gen][if-multi-cond-var][struct]") {
    auto jit = gen_jit(R"SRC(
        module __imcv_struct1__;

        struct S {
            val : int;
        }

        makeS(v : int) res : S {
            res.val = v;
        }

        test() : int {
            if(s1 : S = makeS(10); s2 : S = makeS(s1.val + 5); s2.val > 10) {
                return s1.val + s2.val;
            }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 25);
}


// =============================================================================
// [IMCV-BRACE] Brace-init form
// =============================================================================

TEST_CASE("if-multi-cond-var: brace init",
          "[gen][if-multi-cond-var][brace]") {
    auto jit = gen_jit(R"SRC(
        module __imcv_brace1__;

        struct S {
            a : int;
            b : int;
        }

        test() : int {
            if(s : S{.a = 42, .b = 10}; s.a != 28) {
                return s.a + s.b;
            }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 52);
}

TEST_CASE("if-multi-cond-var: multi var with brace init",
          "[gen][if-multi-cond-var][brace]") {
    auto jit = gen_jit(R"SRC(
        module __imcv_brace2__;

        struct S {
            a : int;
            b : int;
        }

        test() : int {
            if(x : int = 5; s : S{.a = x, .b = x + 1}; s.a + s.b > 10) {
                return s.a + s.b;
            }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 11);
}


// =============================================================================
// [IMCV-DTOR] Destructors called for all vars
// =============================================================================

TEST_CASE("if-multi-cond-var: destructors called for all vars in then",
          "[gen][if-multi-cond-var][dtor]") {
    auto jit = gen_jit(R"SRC(
        module __imcv_dtor1__;

        g_dtor_count : int = 0;

        struct S {
            val : int;

            ~S() {
                g_dtor_count = g_dtor_count + 1;
            }
        }

        makeS(v : int) res : S {
            res.val = v;
        }

        test() : int {
            g_dtor_count = 0;
            if(s1 : S = makeS(1); s2 : S = makeS(2); s1.val + s2.val > 0) {
                // both alive here
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

TEST_CASE("if-multi-cond-var: destructors called for all vars in else",
          "[gen][if-multi-cond-var][dtor]") {
    auto jit = gen_jit(R"SRC(
        module __imcv_dtor2__;

        g_dtor_count : int = 0;

        struct S {
            val : int;

            ~S() {
                g_dtor_count = g_dtor_count + 1;
            }
        }

        makeS(v : int) res : S {
            res.val = v;
        }

        test() : int {
            g_dtor_count = 0;
            if(s1 : S = makeS(1); s2 : S = makeS(2); s1.val + s2.val > 100) {
                // not taken
            } else {
                // both alive here
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
// [IMCV-SCOPE] Variables not visible after if
// =============================================================================

TEST_CASE("if-multi-cond-var: variables can be redeclared after if",
          "[gen][if-multi-cond-var][scope]") {
    auto jit = gen_jit(R"SRC(
        module __imcv_scope1__;

        test() : int {
            if(a : int = 1; b : int = 2; a + b > 0) {
                // a, b visible here
            }
            a : int = 100;
            b : int = 200;
            return a + b;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 300);
}


// =============================================================================
// [IMCV-NESTED] Nested multi-var if
// =============================================================================

TEST_CASE("if-multi-cond-var: nested multi-var if",
          "[gen][if-multi-cond-var][nested]") {
    auto jit = gen_jit(R"SRC(
        module __imcv_nested1__;

        test() : int {
            if(a : int = 1; b : int = 2; a + b > 0) {
                if(c : int = a + b; d : int = c * 2; d == 6) {
                    return d;
                }
            }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 6);
}


// =============================================================================
// [IMCV-SINGLE] Single var backward compat (same as if(var; test))
// =============================================================================

TEST_CASE("if-multi-cond-var: single var still works",
          "[gen][if-multi-cond-var][single]") {
    auto jit = gen_jit(R"SRC(
        module __imcv_single1__;

        test() : int {
            if(x : int = 42; x > 0) {
                return x;
            }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

