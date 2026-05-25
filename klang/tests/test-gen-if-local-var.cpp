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
 * Tests for if-local-variable-declaration (if-let form).
 *
 * Syntax: if (name : type = expr) { ... } else { ... }
 *
 * The variable is scoped to the if statement. Its boolean cast determines
 * whether the then or else branch is taken.
 *
 * Test categories:
 *   [ILV-INT]   Integer condition variable (!=0 → then, ==0 → else)
 *   [ILV-PTR]   Pointer condition variable (!=null → then, ==null → else)
 *   [ILV-OWN]   Owner condition variable (!=null → then, destructor called)
 *   [ILV-AGG]   Aggregate with bool cast operator
 *   [ILV-LNK]   Link soft-fail (null → else, variable not visible in else)
 *   [ILV-REF]   Reference soft-fail
 *   [ILV-DTOR]  Destructor called at end of then/else
 *   [ILV-SCOPE] Variable not visible after if
 */

#include <catch2/catch_test_macros.hpp>
#include "helpers.hpp"


// =============================================================================
// [ILV-INT] Integer condition variable
// =============================================================================

TEST_CASE("if-local-var: int non-zero enters then",
          "[gen][if-local-var][int]") {
    auto jit = gen_jit(R"SRC(
        module __ilv_int1__;

        getval() : int {
            return 42;
        }

        test() : int {
            if(x : int = getval()) {
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

TEST_CASE("if-local-var: int zero enters else",
          "[gen][if-local-var][int]") {
    auto jit = gen_jit(R"SRC(
        module __ilv_int2__;

        getval() : int {
            return 0;
        }

        test() : int {
            if(x : int = getval()) {
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

TEST_CASE("if-local-var: int zero no else continues after if",
          "[gen][if-local-var][int]") {
    auto jit = gen_jit(R"SRC(
        module __ilv_int3__;

        getval() : int {
            return 0;
        }

        test() : int {
            if(x : int = getval()) {
                return x;
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
// [ILV-PTR] Pointer condition variable
// =============================================================================

TEST_CASE("if-local-var: pointer non-null enters then",
          "[gen][if-local-var][ptr]") {
    auto jit = gen_jit(R"SRC(
        module __ilv_ptr1__;

        test() : int {
            val : int = 7;
            if(p : int* = &val) {
                return *p;
            }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 7);
}

TEST_CASE("if-local-var: pointer null enters else",
          "[gen][if-local-var][ptr]") {
    auto jit = gen_jit(R"SRC(
        module __ilv_ptr2__;

        test() : int {
            p_null : int* = null;
            if(p : int* = p_null) {
                return *p;
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
// [ILV-LNK] Link soft-fail: null assignment → else, variable not in else
// =============================================================================

TEST_CASE("if-local-var: link from non-null enters then",
          "[gen][if-local-var][link]") {
    auto jit = gen_jit(R"SRC(
        module __ilv_lnk1__;

        test() : int {
            val : int = 55;
            p : int* = &val;
            if(lnk : int+ = p) {
                return *lnk;
            }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 55);
}

TEST_CASE("if-local-var: link from null enters else (soft-fail)",
          "[gen][if-local-var][link][softfail]") {
    auto jit = gen_jit(R"SRC(
        module __ilv_lnk2__;

        test() : int {
            p : int* = null;
            if(lnk : int+ = p) {
                return *lnk;
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

TEST_CASE("if-local-var: link from null no else continues after if",
          "[gen][if-local-var][link][softfail]") {
    auto jit = gen_jit(R"SRC(
        module __ilv_lnk3__;

        test() : int {
            p : int* = null;
            if(lnk : int+ = p) {
                return *lnk;
            }
            return 77;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 77);
}


// =============================================================================
// [ILV-DTOR] Destructor called at end of then and else blocks
// =============================================================================

TEST_CASE("if-local-var: destructor called at end of then block",
          "[gen][if-local-var][dtor]") {
    auto jit = gen_jit(R"SRC(
        module __ilv_dtor1__;

        g_dtor_count : int = 0;

        struct S {
            val : int;

            operator() : bool {
                return val != 0;
            }

            ~S() {
                g_dtor_count = g_dtor_count + 1;
            }
        }

        makeS(v : int) res : S {
            res.val = v;
        }

        test() : int {
            g_dtor_count = 0;
            if(s : S = makeS(1)) {
                // s should be alive here
            }
            // s should be destroyed here
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
// [ILV-SCOPE] Variable not visible after the if statement
// =============================================================================

// This test verifies that the condition variable is scoped to the if statement.
// The variable should not be accessible after the if block.
// We test this indirectly: after the if, we declare a new variable with the same
// name — this should compile without error.
TEST_CASE("if-local-var: variable can be redeclared after if (scope test)",
          "[gen][if-local-var][scope]") {
    auto jit = gen_jit(R"SRC(
        module __ilv_scope1__;

        getval() : int {
            return 10;
        }

        test() : int {
            if(x : int = getval()) {
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
// [ILV-NESTED] Nested if-local-var
// =============================================================================

TEST_CASE("if-local-var: nested if-local-var",
          "[gen][if-local-var][nested]") {
    auto jit = gen_jit(R"SRC(
        module __ilv_nested1__;

        test() : int {
            if(a : int = 3) {
                if(b : int = 5) {
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
// [ILV-UNION] Union sub-type access soft-fail in if-local-var
// =============================================================================

TEST_CASE("if-local-var: union sub-type copy access mismatch enters else",
          "[gen][if-local-var][union][softfail]") {
    auto jit = gen_jit(R"SRC(
        module __ilv_union1__;

        union U {
            first: int;
            second: long;
        }

        test() : int {
            u : U;
            u.second = 9;
            if(v : int = u.first) {
                return v;
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

TEST_CASE("if-local-var: union sub-type with else-if chain and pointer null soft-fail",
          "[gen][if-local-var][union][softfail][else-if]") {
    auto jit = gen_jit(R"SRC(
        module __ilv_union2__;

        struct S {
            x: int;
        }

        union U {
            first: int;
            second: S;
            third: S*;
        }

        test() : int {
            u : U;
            u.third = null;

            if(a : int = u.first) {
                return 10;
            } else if(b : S+ = &u.second) {
                return 20;
            } else if(c : S* = u.third) {
                return 30;
            } else {
                return 40;
            }
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 40);
}

TEST_CASE("if-local-var: multi cond-vars union mismatch on second var soft-fails",
          "[gen][if-local-var][union][softfail][multi]") {
    auto jit = gen_jit(R"SRC(
        module __ilv_union3__;

        union U {
            first: int;
            second: long;
        }

        test() : int {
            u : U;
            u.first = 5;
            if(a : int = u.first; b : long = u.second) {
                return 1;
            } else {
                return 2;
            }
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 2);
}

TEST_CASE("union mismatch remains fatal outside if-local-var",
          "[gen][if-local-var][union][fatal]") {
    auto result = build_and_exec(R"SRC(
        module __ilv_union_fatal__;

        union U {
            first: int;
            second: long;
        }

        main() : int {
            u : U;
            u.second = 9;
            return u.first;
        }
    )SRC");
    REQUIRE(result.exit_code != 0);
}









