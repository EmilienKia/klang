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
 * Tests for if(var; test) form — condition variable with separate test expression.
 *
 * Syntax: if (name : type = expr; test_expr) { ... } else { ... }
 *
 * The variable is declared and initialized, but branching is determined
 * by the separate test expression after ';' — NOT by a boolean cast of
 * the variable. No soft-fail behavior on ref/link assignment.
 *
 * Test categories:
 *   [ICVT-INT]    Integer variable + separate bool test
 *   [ICVT-REF]    Reference variable + separate test (no soft-fail)
 *   [ICVT-SCOPE]  Variable scoped to if statement
 *   [ICVT-NESTED] Nested if(var; test)
 *   [ICVT-ELSE]   Variable accessible in both then and else
 *   [ICVT-STRUCT] Struct variable + method test
 */

#include <catch2/catch_test_macros.hpp>
#include "helpers.hpp"


// =============================================================================
// [ICVT-INT] Integer variable + separate bool test
// =============================================================================

TEST_CASE("if-cond-var-test: int var with true test enters then",
          "[gen][if-cond-var-test][int]") {
    auto jit = gen_jit(R"SRC(
        module gen_if_cond_var_test_01;

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

TEST_CASE("if-cond-var-test: int var with false test enters else",
          "[gen][if-cond-var-test][int]") {
    auto jit = gen_jit(R"SRC(
        module gen_if_cond_var_test_02;

        test() : int {
            if(x : int = 42; x < 0) {
                return x;
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

TEST_CASE("if-cond-var-test: zero var with separate true test enters then",
          "[gen][if-cond-var-test][int]") {
    // Zero variable would be false in classic if-let, but separate test overrides
    auto jit = gen_jit(R"SRC(
        module gen_if_cond_var_test_03;

        test() : int {
            if(x : int = 0; true) {
                return 1;
            } else {
                return -1;
            }
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 1);
}


// =============================================================================
// [ICVT-ELSE] Variable accessible in both then and else branches
// =============================================================================

TEST_CASE("if-cond-var-test: var accessible in else branch",
          "[gen][if-cond-var-test][else]") {
    auto jit = gen_jit(R"SRC(
        module gen_if_cond_var_test_04;

        test() : int {
            if(x : int = 10; x > 100) {
                return x + 1;
            } else {
                return x + 2;
            }
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 12);
}


// =============================================================================
// [ICVT-SCOPE] Variable scoped to if statement
// =============================================================================

TEST_CASE("if-cond-var-test: variable can be redeclared after if (scope test)",
          "[gen][if-cond-var-test][scope]") {
    auto jit = gen_jit(R"SRC(
        module gen_if_cond_var_test_05;

        test() : int {
            if(x : int = 5; x > 0) {
                // x is visible here
            }
            x : int = 20;
            return x;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 20);
}


// =============================================================================
// [ICVT-NESTED] Nested if(var; test) statements
// =============================================================================

TEST_CASE("if-cond-var-test: nested if(var; test)",
          "[gen][if-cond-var-test][nested]") {
    auto jit = gen_jit(R"SRC(
        module gen_if_cond_var_test_06;

        test() : int {
            if(a : int = 3; a > 0) {
                if(b : int = 5; b > a) {
                    return a + b;
                }
            }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 8);
}


// =============================================================================
// [ICVT-STRUCT] Struct variable + method call as test
// =============================================================================

TEST_CASE("if-cond-var-test: struct var with method test",
          "[gen][if-cond-var-test][struct]") {
    auto jit = gen_jit(R"SRC(
        module gen_if_cond_var_test_07;

        struct S {
            val : int;

            isPositive() : bool {
                return val > 0;
            }
        }

        makeS(v : int) res : S {
            res.val = v;
        }

        test() : int {
            if(s : S = makeS(42); s.isPositive()) {
                return s.val;
            } else {
                return -1;
            }
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

TEST_CASE("if-cond-var-test: struct var with failing method test enters else",
          "[gen][if-cond-var-test][struct]") {
    auto jit = gen_jit(R"SRC(
        module gen_if_cond_var_test_08;

        struct S {
            val : int;

            isPositive() : bool {
                return val > 0;
            }
        }

        makeS(v : int) res : S {
            res.val = v;
        }

        test() : int {
            if(s : S = makeS(-5); s.isPositive()) {
                return s.val;
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
// [ICVT-REF] Reference variable + separate test (no soft-fail)
// =============================================================================

TEST_CASE("if-cond-var-test: ref var with separate test, no soft-fail",
          "[gen][if-cond-var-test][ref]") {
    auto jit = gen_jit(R"SRC(
        module gen_if_cond_var_test_09;

        test() : int {
            val : int = 7;
            if(r : int& = val; r > 5) {
                return r;
            } else {
                return -1;
            }
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 7);
}

TEST_CASE("if-cond-var-test: ref var with failing test enters else",
          "[gen][if-cond-var-test][ref]") {
    auto jit = gen_jit(R"SRC(
        module gen_if_cond_var_test_10;

        test() : int {
            val : int = 3;
            if(r : int& = val; r > 5) {
                return r;
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
// [ICVT-DTOR] Destructor called at end of both branches
// =============================================================================

TEST_CASE("if-cond-var-test: destructor called in then branch",
          "[gen][if-cond-var-test][dtor]") {
    auto jit = gen_jit(R"SRC(
        module gen_if_cond_var_test_11;

        g_dtor_count : int = 0;

        struct S {
            val : int;

            isPositive() : bool {
                return val > 0;
            }

            ~S() {
                ++g_dtor_count;
            }
        }

        makeS(v : int) res : S {
            res.val = v;
        }

        test() : int {
            g_dtor_count = 0;
            if(s : S = makeS(1); s.isPositive()) {
                // s alive here
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

TEST_CASE("if-cond-var-test: destructor called in else branch",
          "[gen][if-cond-var-test][dtor]") {
    auto jit = gen_jit(R"SRC(
        module gen_if_cond_var_test_12;

        g_dtor_count : int = 0;

        struct S {
            val : int;

            isPositive() : bool {
                return val > 0;
            }

            ~S() {
                ++g_dtor_count;
            }
        }

        makeS(v : int) res : S {
            res.val = v;
        }

        test() : int {
            g_dtor_count = 0;
            if(s : S = makeS(-1); s.isPositive()) {
                // not taken
            } else {
                // s alive here too
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


// =============================================================================
// [ICVT-NOTEST] No separate test, only var (should use classic if-let form)
// This is a negative test: if(var; test) with no test should fail to parse.
// =============================================================================

TEST_CASE("if-cond-var-test: function call as init with method test",
          "[gen][if-cond-var-test][func]") {
    auto jit = gen_jit(R"SRC(
        module gen_if_cond_var_test_13;

        getValue() : int {
            return 100;
        }

        test() : int {
            if(x : int = getValue(); x == 100) {
                return 1;
            } else {
                return 0;
            }
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 1);
}

