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
#include <catch2/generators/catch_generators.hpp>
#include "helpers.hpp"

TEST_CASE("if-local-var: NRVO condition survives only the returned iteration",
          "[gen][if-local-var][nrvo][regression]") {
    const bool optimize = GENERATE(false, true);
    CAPTURE(optimize);
    auto jit = gen_jit(R"K(
        module if_nrvo_iteration_cleanup;
        made: int = 0;
        drops: int = 0;
        struct Result {
            id: int;
            Result(value: int): id(value) { ++made; }
            ~Result() { ++drops; }
        }
        fallthrough(): Result {
            i: int = 0;
            while (true) {
                ++i;
                if (r: Result = Result(i); true) {
                    if (i == 2) { return r; }
                }
            }
        }
        continuing(): Result {
            i: int = 0;
            while (true) {
                ++i;
                if (r: Result = Result(i); true) {
                    if (i == 1) { continue; }
                    return r;
                }
            }
        }
        breaking(): Result {
            i: int = 0;
            while (true) {
                ++i;
                while (true) {
                    if (r: Result = Result(i); true) {
                        if (i == 1) { break; }
                        return r;
                    }
                }
            }
        }
        ordinary(): Result { return Result(9); }
        testFallthrough(): int {
            made = 0;
            drops = 0;
            if (true) {
                r: Result = fallthrough();
                if (r.id != 2 || made != 2 || drops != 1) return -1;
            }
            return drops == 2 ? 42 : -2;
        }
        testContinue(): int {
            made = 0;
            drops = 0;
            if (true) {
                r: Result = continuing();
                if (r.id != 2 || made != 2 || drops != 1) return -1;
            }
            return drops == 2 ? 42 : -2;
        }
        testBreak(): int {
            made = 0;
            drops = 0;
            if (true) {
                r: Result = breaking();
                if (r.id != 2 || made != 2 || drops != 1) return -1;
            }
            return drops == 2 ? 42 : -2;
        }
        testOrdinary(): int {
            made = 0;
            drops = 0;
            if (true) {
                r: Result = ordinary();
                if (r.id != 9 || made != 1 || drops != 0) return -1;
            }
            return drops == 1 ? 42 : -2;
        }
    )K", false, optimize);
    REQUIRE(jit);
    for (const auto* name : {"testFallthrough", "testContinue", "testBreak", "testOrdinary"}) {
        INFO(name);
        auto fn = jit->lookup_symbol<int (*)()>(name);
        REQUIRE(fn);
        CHECK(fn() == 42);
    }
}

TEST_CASE("if-local-var: pending NRVO result is destroyed when outer cleanup throws",
          "[gen][if-local-var][nrvo][exceptions][regression]") {
    const bool optimize = GENERATE(false, true);
    CAPTURE(optimize);
    auto jit = gen_jit(R"K(
        module if_nrvo_pending_result_cleanup;
        made: int = 0;
        drops: int = 0;
        badDrops: int = 0;
        catchInside: bool = false;
        innerCode: int = 0;
        struct Result {
            id: int;
            Result(value: int): id(value) { ++made; }
            ~Result() {
                ++drops;
                if (catchInside) {
                    try { throw FatalError(932); }
                    catch (e: FatalError&) { innerCode = e.getCode(); }
                }
            }
        }
        struct BadGuard {
            BadGuard() {}
            ~BadGuard() { ++badDrops; throw FatalError(930); }
        }
        class BadOwner {
        public:
            BadOwner() {}
            ~BadOwner() { ++badDrops; throw FatalError(931); }
        }
        outer(): Result {
            bad: BadGuard = BadGuard();
            while (true) {
                if (r: Result = Result(42); true) { return r; }
            }
        }
        sameScope(): Result {
            while (true) {
                if (bad: BadGuard = BadGuard(); r: Result = Result(42); true) {
                    return r;
                }
            }
        }
        parameter(bad: BadOwner!): Result {
            while (true) {
                if (r: Result = Result(42); true) { return r; }
            }
        }
        named(bad: BadOwner!) r: Result(42) {}
        namedFinally(bad: BadOwner!) r: Result(42) {
            try { return; }
            finally {
                try { throw FatalError(934); }
                catch (e: FatalError&) { innerCode = e.getCode(); }
            }
        }
        testOuter(): int {
            made = 0;
            drops = 0;
            badDrops = 0;
            innerCode = 0;
            catchInside = true;
            code: int = 0;
            try { value: Result = outer(); }
            catch (e: FatalError&) { code = e.getCode(); }
            catchInside = false;
            return code == 930 && innerCode == 932 && made == 1 &&
                drops == 1 && badDrops == 1 ? 42 : -1;
        }
        testParameter(): int {
            made = 0;
            drops = 0;
            badDrops = 0;
            code: int = 0;
            try { value: Result = parameter(new BadOwner()); }
            catch (e: FatalError&) { code = e.getCode(); }
            return code == 931 && made == 1 && drops == 1 && badDrops == 1 ? 42 : -2;
        }
        testSameScope(): int {
            made = 0;
            drops = 0;
            badDrops = 0;
            code: int = 0;
            try { value: Result = sameScope(); }
            catch (e: FatalError&) { code = e.getCode(); }
            return code == 930 && made == 1 && drops == 1 && badDrops == 1 ? 42 : -5;
        }
        testNamed(): int {
            made = 0;
            drops = 0;
            badDrops = 0;
            code: int = 0;
            try { value: Result = named(new BadOwner()); }
            catch (e: FatalError&) { code = e.getCode(); }
            return code == 931 && made == 1 && drops == 1 && badDrops == 1 ? 42 : -3;
        }
        testNamedFinally(): int {
            made = 0;
            drops = 0;
            badDrops = 0;
            innerCode = 0;
            code: int = 0;
            try { value: Result = namedFinally(new BadOwner()); }
            catch (e: FatalError&) { code = e.getCode(); }
            return code == 931 && innerCode == 934 && made == 1 &&
                drops == 1 && badDrops == 1 ? 42 : -4;
        }
    )K", false, optimize);
    REQUIRE(jit);
    for (const auto* name : {"testOuter", "testSameScope", "testParameter",
                             "testNamed", "testNamedFinally"}) {
        INFO(name);
        auto fn = jit->lookup_symbol<int (*)()>(name);
        REQUIRE(fn);
        CHECK(fn() == 42);
    }
}

TEST_CASE("if-local-var: owner cleanup on control-flow exits",
          "[gen][if-local-var][owner][regression]") {
    const bool optimize = GENERATE(false, true);
    auto jit = gen_jit(R"K(
        module if_owner_control_flow_cleanup;
        drops: int = 0;
        class Resource {
        public:
            Resource() {}
            ~Resource() { ++drops; }
        }
        earlyReturn(): int {
            if (value: Resource! = new Resource()) {
                return 42;
            }
            return -1;
        }
        multiReturn(): int {
            if (first: Resource! = new Resource(); second: Resource! = new Resource()) {
                return 42;
            }
            return -1;
        }
        test(): int {
            drops = 0;
            if (earlyReturn() != 42 || drops != 1) return -1;
            if (multiReturn() != 42 || drops != 3) return -2;
            while (true) {
                if (value: Resource! = new Resource()) {
                    break;
                }
                return -3;
            }
            if (drops != 4) return -4;
            iterations: int = 0;
            while (iterations < 2) {
                ++iterations;
                if (value: Resource! = new Resource()) {
                    continue;
                }
                return -5;
            }
            return drops == 6 ? 42 : -6;
        }
    )K", false, optimize);
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("if-local-var: constructed conditions unwind in reverse order",
          "[gen][if-local-var][exceptions][regression]") {
    const bool optimize = GENERATE(false, true);
    auto jit = gen_jit(R"K(
        module if_condition_unwind_cleanup;
        order: int = 0;
        struct Guard {
            id: int;
            Guard(value: int) : id(value) {}
            ~Guard() { order = order * 10 + id; }
        }
        class Resource {
        public:
            id: int;
            Resource(value: int) : id(value) {}
            ~Resource() { order = order * 10 + id; }
        }
        test(): int {
            order = 0;
            code: int = 0;
            try {
                if (first: Resource! = new Resource(1);
                    guard: Guard = Guard(2);
                    last: Resource! = new Resource(3)) {
                    nested: Guard(4);
                    throw Exception(923);
                }
            } catch (e: Throwable&) {
                code = e.getCode();
            }
            if (code != 923 || order != 4321) return -1;

            order = 0;
            code = 0;
            if (value: Resource! = new Resource(1)) {
                try { throw Exception(924); }
                catch (e: Throwable&) { code = e.getCode(); }
                if (order != 0) return -2;
            }
            return code == 924 && order == 1 ? 42 : -3;
        }
    )K", false, optimize);
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

static const std::string throwing_if_guard_declarations = R"K(
    module if_condition_throwing_destructor;
    drops: int = 0;
    inner: int = 0;
    struct Guard {
        Guard() {}
        ~Guard() {
            ++drops;
            if (drops == 1) throw FatalError(915);
        }
    }
)K";

TEST_CASE("if-local-var: throwing condition destructor on normal exit",
          "[gen][if-local-var][exceptions][regression]") {
    const bool optimize = GENERATE(false, true);
    auto jit = gen_jit(throwing_if_guard_declarations + R"K(
        test(): int {
            drops = 0;
            inner = 0;
            outer: int = 0;
            try {
                if (guard: Guard = Guard(); true) {
                    try { throw FatalError(914); }
                    catch (e: FatalError&) { inner = e.getCode(); }
                }
            } catch (e: FatalError&) { outer = e.getCode(); }
            if (inner != 914) return -1;
            if (outer != 915) return -2;
            return drops == 1 ? 42 : -10 - drops;
        }
    )K", false, optimize);
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("if-local-var: throwing condition destructor on return",
          "[gen][if-local-var][exceptions][regression]") {
    const bool optimize = GENERATE(false, true);
    auto jit = gen_jit(throwing_if_guard_declarations + R"K(
        early(): int {
            if (guard: Guard = Guard(); true) {
                try { return 7; }
                catch (e: FatalError&) { ++inner; }
            }
            return 17;
        }
        test(): int {
            drops = 0;
            inner = 0;
            outer: int = 0;
            try { early(); }
            catch (e: FatalError&) { outer = e.getCode(); }
            if (inner != 0) return -10 - inner;
            if (outer != 915) return -2;
            return drops == 1 ? 42 : -20 - drops;
        }
    )K", false, optimize);
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("if-local-var: throwing later condition cleans earlier owner",
          "[gen][if-local-var][exceptions][regression]") {
    const bool optimize = GENERATE(false, true);
    auto jit = gen_jit(R"K(
        module if_condition_partial_unwind;
        drops: int = 0;
        class Resource {
        public:
            Resource() {}
            ~Resource() { ++drops; }
        }
        fail(): Resource! { throw FatalError(916); }
        test(): int {
            drops = 0;
            code: int = 0;
            try {
                if (first: Resource! = new Resource(); second: Resource! = fail()) {}
            } catch (e: FatalError&) { code = e.getCode(); }
            if (code != 916) return -1;
            return drops == 1 ? 42 : -10 - drops;
        }
    )K", false, optimize);
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("if-local-var: partial initialization and soft-fail unwind",
          "[gen][if-local-var][exceptions][regression]") {
    const bool optimize = GENERATE(false, true);
    auto jit = gen_jit(R"K(
        module if_partial_condition_cleanup;
        order: int = 0;
        throwGuard: bool = false;
        catchInside: bool = false;
        struct Guard {
            Guard() {}
            ~Guard() {
                order = order * 10 + 2;
                if (catchInside) {
                    try { throw FatalError(925); }
                    catch (e: FatalError&) { if (e.getCode() != 925) order = -1; }
                }
                if (throwGuard) throw FatalError(918);
            }
        }
        class Resource {
        public:
            Resource() {}
            ~Resource() { order = order * 10 + 1; }
        }
        failOwner(): Resource! { throw FatalError(916); }
        failTest(): bool { throw FatalError(917); }
        test(): int {
            order = 0;
            code: int = 0;
            try {
                if (first: Resource! = new Resource();
                    second: Guard = Guard();
                    third: Resource! = failOwner()) {}
            } catch (e: FatalError&) { code = e.getCode(); }
            if (code != 916 || order != 21) return -1;

            order = 0;
            code = 0;
            try {
                if (first: Resource! = new Resource();
                    second: Guard = Guard(); failTest()) {}
            } catch (e: FatalError&) { code = e.getCode(); }
            if (code != 917 || order != 21) return -2;

            order = 0;
            code = 0;
            try {
                if (first: Resource! = failOwner();
                    second: Guard = Guard(); true) {}
            } catch (e: FatalError&) { code = e.getCode(); }
            if (code != 916 || order != 0) return -5;

            order = 0;
            code = 0;
            catchInside = true;
            try {
                if (first: Resource! = new Resource();
                    second: Guard = Guard();
                    third: Resource! = failOwner()) {}
            } catch (e: FatalError&) { code = e.getCode(); }
            catchInside = false;
            if (code != 916 || order != 21) return -6;

            iterations: int = 0;
            while (iterations < 2) {
                ++iterations;
                order = 0;
                code = 0;
                throwGuard = true;
                try {
                    if (first: Resource! = new Resource();
                        second: Guard = Guard();
                        third: Resource! = null) { return -3; }
                } catch (e: FatalError&) { code = e.getCode(); }
                throwGuard = false;
                if (code != 918 || order != 21) return -4;
            }
            return 42;
        }
    )K", false, optimize);
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("if-local-var: early exits leave inner catch after finally",
          "[gen][if-local-var][exceptions][regression]") {
    const bool optimize = GENERATE(false, true);
    auto jit = gen_jit(R"K(
        module if_cleanup_exit_handler_scope;
        order: int = 0;
        inner: int = 0;
        struct Guard {
            Guard() {}
            ~Guard() { order = order * 10 + 1; throw FatalError(919); }
        }
        testBreak(): int {
            order = 0;
            inner = 0;
            code: int = 0;
            iterations: int = 0;
            try {
                while (iterations < 1) {
                    ++iterations;
                    if (guard: Guard = Guard(); true) {
                        try { break; }
                        catch (e: FatalError&) { ++inner; }
                        finally { order = order * 10 + 2; }
                    }
                }
            } catch (e: FatalError&) { code = e.getCode(); }
            return code == 919 && inner == 0 && order == 21 ? 42 : -1;
        }
        testContinue(): int {
            order = 0;
            inner = 0;
            code: int = 0;
            iterations: int = 0;
            try {
                while (iterations < 1) {
                    ++iterations;
                    if (guard: Guard = Guard(); true) {
                        try { continue; }
                        catch (e: FatalError&) { ++inner; }
                        finally { order = order * 10 + 2; }
                    }
                }
            } catch (e: FatalError&) { code = e.getCode(); }
            return code == 919 && inner == 0 && order == 21 ? 42 : -2;
        }
        earlyReturn(): int {
            if (guard: Guard = Guard(); true) {
                try { return 7; }
                catch (e: FatalError&) { ++inner; }
                finally { order = order * 10 + 2; }
            }
            return -1;
        }
        testReturn(): int {
            order = 0;
            inner = 0;
            code: int = 0;
            try { earlyReturn(); }
            catch (e: FatalError&) { code = e.getCode(); }
            return code == 919 && inner == 0 && order == 21 ? 42 : -3;
        }
    )K", false, optimize);
    REQUIRE(jit);
    for (const auto* name : {"testBreak", "testContinue", "testReturn"}) {
        INFO(name);
        auto fn = jit->lookup_symbol<int (*)()>(name);
        REQUIRE(fn);
        CHECK(fn() == 42);
    }
}

TEST_CASE("if-local-var: inner cleanup still reaches its enclosing catch",
          "[gen][if-local-var][exceptions][regression]") {
    const bool optimize = GENERATE(false, true);
    auto jit = gen_jit(R"K(
        module if_cleanup_inner_handler_scope;
        outerDrops: int = 0;
        innerDrops: int = 0;
        caught: int = 0;
        struct Outer {
            Outer() {}
            ~Outer() { ++outerDrops; }
        }
        struct Inner {
            Inner() {}
            ~Inner() { ++innerDrops; throw FatalError(920); }
        }
        early(): int {
            if (guard: Outer = Outer(); true) {
                try {
                    local: Inner = Inner();
                    return 7;
                } catch (e: FatalError&) {
                    caught = e.getCode();
                    if (outerDrops != 0) return -1;
                }
            }
            return 42;
        }
        test(): int {
            outerDrops = 0;
            innerDrops = 0;
            caught = 0;
            value: int = early();
            return value == 42 && caught == 920 && innerDrops == 1 &&
                outerDrops == 1 ? 42 : -2;
        }
    )K", false, optimize);
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("if-local-var: boolean condition cleans on false and throw",
          "[gen][if-local-var][exceptions][regression]") {
    const bool optimize = GENERATE(false, true);
    auto jit = gen_jit(R"K(
        module if_bool_condition_cleanup;
        drops: int = 0;
        shouldThrow: bool = false;
        struct Guard {
            id: int;
            Guard(): id(7) {}
            operator(): bool {
                if (shouldThrow) throw FatalError(921);
                return false;
            }
            ~Guard() { ++drops; }
        }
        returnFromElse(): int {
            if (guard: Guard = Guard()) { return -1; }
            else { return guard.id; }
        }
        test(): int {
            drops = 0;
            shouldThrow = false;
            selected: int = 0;
            if (guard: Guard = Guard()) { return -1; }
            else { selected = guard.id + drops; }
            if (selected != 7 || drops != 1) return -2;
            if (returnFromElse() != 7 || drops != 2) return -3;
            code: int = 0;
            try {
                if (guard: Guard = Guard()) { return -4; }
                else { throw FatalError(922); }
            } catch (e: FatalError&) { code = e.getCode(); }
            if (code != 922 || drops != 3) return -5;
            shouldThrow = true;
            code = 0;
            try {
                if (guard: Guard = Guard()) { return -6; }
            } catch (e: FatalError&) { code = e.getCode(); }
            return code == 921 && drops == 4 ? 42 : -7;
        }
    )K", false, optimize);
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}


// =============================================================================
// [ILV-INT] Integer condition variable
// =============================================================================

TEST_CASE("if-local-var: int non-zero enters then",
          "[gen][if-local-var][int]") {
    auto jit = gen_jit(R"SRC(
        module gen_if_local_var_01;

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
        module gen_if_local_var_02;

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
        module gen_if_local_var_03;

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
        module gen_if_local_var_04;

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
        module gen_if_local_var_05;

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
        module gen_if_local_var_06;

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
        module gen_if_local_var_07;

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
        module gen_if_local_var_08;

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
        module gen_if_local_var_09;

        g_dtor_count : int = 0;

        struct S {
            val : int;

            operator() : bool {
                return val != 0;
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
        module gen_if_local_var_10;

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
        module gen_if_local_var_11;

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
        module gen_if_local_var_12;

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
        module gen_if_local_var_13;

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
        module gen_if_local_var_14;

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
        module gen_if_local_var_15;

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









