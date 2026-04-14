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
 * Tests for soft-fail link assignment in if-conditions.
 *
 * When a link (+) assignment appears as the condition of an `if` statement and
 * the assignment fails (null source or RTTI mismatch), the behaviour is:
 *   - branch to the `else` clause if present, or
 *   - continue after the `if` statement if there is no `else`.
 *
 * This replaces the fatal trap that would normally fire when a non-null link
 * receives a null value.
 *
 * Test categories:
 *   [SF-T]  Success: link rebind from non-null source → enters then
 *   [SF-E]  Soft-fail: link rebind from null source → enters else / continues
 *   [SF-D]  Dynamic cast to link in if-condition
 *   [SF-N]  Nested if with link soft-fail
 *   [SF-R]  Non-regression: outside of if, behaviour is unchanged
 */

#include <catch2/catch_test_macros.hpp>
#include "helpers.hpp"


// =============================================================================
// [SF-T] Success cases: link rebind from non-null pointer — enters then
// =============================================================================

TEST_CASE("if-link soft-fail: rebind from non-null ptr, no else — enters then",
          "[gen][if-link][softfail][then]") {
    auto jit = gen_jit(R"SRC(
        module __sf_t1__;

        test() : int {
            x : int = 42;
            p : int* = &x;
            lnk : int+ = &x;
            if (lnk = p) {
                return *lnk;
            }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

TEST_CASE("if-link soft-fail: rebind from non-null ptr, with else — enters then",
          "[gen][if-link][softfail][then]") {
    auto jit = gen_jit(R"SRC(
        module __sf_t2__;

        test() : int {
            x : int = 7;
            y : int = 99;
            p : int* = &y;
            lnk : int+ = &x;
            if (lnk = p) {
                return *lnk;
            } else {
                return -1;
            }
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 99);
}


// =============================================================================
// [SF-E] Soft-fail cases: link rebind from null pointer — enters else / continues
// =============================================================================

TEST_CASE("if-link soft-fail: rebind from null ptr, no else — continues after if",
          "[gen][if-link][softfail][else]") {
    auto jit = gen_jit(R"SRC(
        module __sf_e1__;

        test() : int {
            x : int = 10;
            lnk : int+ = &x;
            p : int* = null;
            if (lnk = p) {
                return 1;
            }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 0);
}

TEST_CASE("if-link soft-fail: rebind from null ptr, with else — enters else",
          "[gen][if-link][softfail][else]") {
    auto jit = gen_jit(R"SRC(
        module __sf_e2__;

        test() : int {
            x : int = 10;
            lnk : int+ = &x;
            p : int* = null;
            if (lnk = p) {
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

TEST_CASE("if-link soft-fail: rebind from null, link keeps old value",
          "[gen][if-link][softfail][else]") {
    auto jit = gen_jit(R"SRC(
        module __sf_e3__;

        test() : int {
            x : int = 55;
            lnk : int+ = &x;
            p : int* = null;
            if (lnk = p) {
                return -1;
            }
            // After soft-fail, lnk still holds its old value (&x)
            return *lnk;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 55);
}


// =============================================================================
// [SF-D] Dynamic cast to link in if-condition
// =============================================================================

TEST_CASE("if-link soft-fail: dynamic downcast to link, correct type — enters then",
          "[gen][if-link][softfail][dyncast][then]") {
    auto jit = gen_jit(R"SRC(
        module __sf_d1__;

        class Base {
            public val : int;
            public Base() : val(0) {}
            public Base(v : int) : val(v) {}
            public dummy() : int { return 0; }
        }
        class Derived : public Base {
            public extra : int;
            public Derived(v : int) : Base(v), extra(99) {}
            public get_extra() : int { return extra; }
        }

        get_extra_fn(d : Derived&) : int { return d.get_extra(); }

        test() : int {
            d : Derived(42);
            bp : Base* = &d;
            dlnk : Derived+ = &d;
            x : int = 0;
            if (dlnk = (Derived+) bp) {
                x = get_extra_fn(*dlnk);
            } else {
                x = -1;
            }
            return x;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 99);
}

TEST_CASE("if-link soft-fail: dynamic downcast to link, wrong type — enters else",
          "[gen][if-link][softfail][dyncast][else]") {
    auto jit = gen_jit(R"SRC(
        module __sf_d2__;

        class Base {
            public val : int;
            public Base() : val(0) {}
            public Base(v : int) : val(v) {}
            public dummy() : int { return 0; }
        }
        class Derived : public Base {
            public extra : int;
            public Derived(v : int) : Base(v), extra(99) {}
        }
        class Other : public Base {
            public data : int;
            public Other(v : int) : Base(v), data(77) {}
        }

        test() : int {
            o : Other(5);
            bp : Base* = &o;
            d : Derived(1);
            dlnk : Derived+ = &d;
            if (dlnk = (Derived+) bp) {
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

TEST_CASE("if-link soft-fail: dynamic downcast to link, wrong type, no else — continues",
          "[gen][if-link][softfail][dyncast][else]") {
    auto jit = gen_jit(R"SRC(
        module __sf_d3__;

        class Base {
            public val : int;
            public Base() : val(0) {}
            public Base(v : int) : val(v) {}
            public dummy() : int { return 0; }
        }
        class Derived : public Base {
            public extra : int;
            public Derived(v : int) : Base(v), extra(99) {}
        }
        class Other : public Base {
            public data : int;
            public Other(v : int) : Base(v), data(77) {}
        }

        test() : int {
            o : Other(5);
            bp : Base* = &o;
            d : Derived(1);
            dlnk : Derived+ = &d;
            if (dlnk = (Derived+) bp) {
                return 1;
            }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 0);
}

TEST_CASE("if-link soft-fail: dynamic downcast from null ptr — enters else",
          "[gen][if-link][softfail][dyncast][null]") {
    auto jit = gen_jit(R"SRC(
        module __sf_d4__;

        class Base {
            public val : int;
            public Base() : val(0) {}
            public Base(v : int) : val(v) {}
            public dummy() : int { return 0; }
        }
        class Derived : public Base {
            public extra : int;
            public Derived(v : int) : Base(v), extra(99) {}
        }

        test() : int {
            d : Derived(1);
            bp : Base* = null;
            dlnk : Derived+ = &d;
            if (dlnk = (Derived+) bp) {
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


// =============================================================================
// [SF-N] Nested if with link soft-fail
// =============================================================================

TEST_CASE("if-link soft-fail: nested if, inner soft-fails, outer succeeds",
          "[gen][if-link][softfail][nested]") {
    auto jit = gen_jit(R"SRC(
        module __sf_n1__;

        test() : int {
            x : int = 10;
            y : int = 20;
            p1 : int* = &x;
            p2 : int* = null;
            lnk : int+ = &x;
            result : int = 0;
            if (lnk = p1) {
                // outer succeeds
                result = result + *lnk;  // +10
                if (lnk = p2) {
                    // inner soft-fails
                    result = result + 100;
                } else {
                    result = result + 1;   // +1
                }
            } else {
                result = result + 1000;
            }
            return result;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 11);
}

TEST_CASE("if-link soft-fail: nested if, outer soft-fails",
          "[gen][if-link][softfail][nested]") {
    auto jit = gen_jit(R"SRC(
        module __sf_n2__;

        test() : int {
            x : int = 10;
            p1 : int* = null;
            lnk : int+ = &x;
            result : int = 0;
            if (lnk = p1) {
                // This block should NOT be entered
                if (true) {
                    result = result + 100;
                }
                result = result + 200;
            } else {
                result = result + 1;
            }
            return result;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 1);
}


// =============================================================================
// [SF-R] Non-regression: outside of if, link-from-null remains fatal
// =============================================================================

TEST_CASE("Non-regression: link rebind from null outside if — still fatal",
          "[gen][if-link][softfail][regression]") {
    auto res = build_and_exec(R"SRC(
        module __sf_r1__;

        main() : int {
            x : int = 42;
            lnk : int+ = &x;
            p : int* = null;
            lnk = p;       // outside if — must still be fatal
            return 0;
        }
    )SRC");
    REQUIRE(res.exit_code != 0);
}

TEST_CASE("Non-regression: dynamic downcast link wrong type outside if — still fatal",
          "[gen][if-link][softfail][regression]") {
    auto res = build_and_exec(R"SRC(
        module __sf_r2__;

        class Base {
            public val : int;
            public Base() : val(0) {}
            public Base(v : int) : val(v) {}
            public dummy() : int { return 0; }
        }
        class Derived : public Base {
            public extra : int;
            public Derived(v : int) : Base(v), extra(0) {}
        }
        class Other : public Base {
            public data : int;
            public Other(v : int) : Base(v), data(0) {}
        }

        main() : int {
            o : Other(1);
            bp : Base* = &o;
            d : Derived(1);
            dlnk : Derived+ = &d;
            dlnk = (Derived+) bp;   // outside if — must still be fatal
            return 0;
        }
    )SRC");
    REQUIRE(res.exit_code != 0);
}


// =============================================================================
// [SF-R] Non-regression: pointer null check in if-condition is unaffected
// =============================================================================

TEST_CASE("Non-regression: if(ptr) with non-null pointer — enters then",
          "[gen][if-link][softfail][regression][ptr]") {
    auto jit = gen_jit(R"SRC(
        module __sf_rp1__;

        test() : int {
            x : int = 42;
            p : int* = &x;
            if (p) { return 1; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 1);
}

TEST_CASE("Non-regression: if(ptr) with null pointer — enters else",
          "[gen][if-link][softfail][regression][ptr]") {
    auto jit = gen_jit(R"SRC(
        module __sf_rp2__;

        test() : int {
            p : int* = null;
            if (p) { return 1; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 0);
}

TEST_CASE("Non-regression: pointer dereference null in if-condition — still fatal",
          "[gen][if-link][softfail][regression][deref]") {
    auto res = build_and_exec(R"SRC(
        module __sf_rd__;

        main() : int {
            p : int* = null;
            if (*p > 0) { return 1; }
            return 0;
        }
    )SRC");
    REQUIRE(res.exit_code != 0);
}


