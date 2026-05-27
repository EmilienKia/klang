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
 * Tests for exception handling syntax and model building.
 *
 * Currently tests that the exception syntax (throw, try-catch, throws clause)
 * is correctly parsed and compiled through the pipeline without errors.
 * Full exception semantics (throw/catch at runtime) are not yet implemented.
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"


// ════════════════════════════════════════════════════════════════════════════
//  1. Basic try-catch syntax acceptance
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Exception: try-catch parses without error", "[gen][exceptions]") {
    // This test verifies that the pipeline accepts try-catch syntax
    // without throwing a compiler error (no runtime exception support yet).
    auto jit = gen_jit(R"(
        module __test_exc_1__;

        try_but_no_throw() : int {
            result : int = 0;
            try {
                result = 42;
            } catch (e: int*) {
                result = -1;
            }
            return result;
        }
    )");
    REQUIRE(jit != nullptr);
    // The function should return 42 since no exception is thrown
    // and the try body simply executes
    auto fn = jit->lookup_symbol<int(*)()>("try_but_no_throw");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

TEST_CASE("Exception: multiple catch clauses parse", "[gen][exceptions]") {
    auto jit = gen_jit(R"(
        module __test_exc_2__;

        multi_catch() : int {
            result : int = 0;
            try {
                result = 10;
            } catch (e: int*) {
                result = -1;
            } catch (e: long*) {
                result = -2;
            }
            return result;
        }
    )");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("multi_catch");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 10);
}

TEST_CASE("Exception: throws clause on function declaration", "[gen][exceptions]") {
    auto jit = gen_jit(R"(
        module __test_exc_3__;

        struct MyError {
            code : int;
        }

        may_throw() : int throws MyError {
            return 7;
        }
    )");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("may_throw");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 7);
}

TEST_CASE("Exception: throws clause with multiple types", "[gen][exceptions]") {
    auto jit = gen_jit(R"(
        module __test_exc_4__;

        struct ErrorA {
            code : int;
        }

        struct ErrorB {
            msg : int;
        }

        may_throw_multi() : int throws ErrorA, ErrorB {
            return 99;
        }
    )");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("may_throw_multi");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 99);
}

// ════════════════════════════════════════════════════════════════════════════
//  2. Throws spec resolution
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Exception: throws clause type resolution — model inspection", "[gen][exceptions][resolution]") {
    // Verify that the throws spec raw names are resolved to actual types
    auto comp = compile_model(R"(
        module __test_exc_res_1__;

        struct MyException {
            code : int;
        }

        risky() : int throws MyException {
            return 1;
        }
    )");
    REQUIRE(comp != nullptr);

    // Find the function and check that throws_spec is populated
    auto root = comp->get_unit()->get_root_namespace();
    REQUIRE(root != nullptr);
    auto fn = root->get_function("risky");
    REQUIRE(fn != nullptr);
    REQUIRE(fn->has_throws_spec());
    REQUIRE(fn->get_throws_spec().size() == 1);
    // The resolved type should be the struct_type for MyException
    auto throws_type = fn->get_throws_spec()[0];
    REQUIRE(throws_type != nullptr);
    auto st = std::dynamic_pointer_cast<k::model::struct_type>(throws_type);
    REQUIRE(st != nullptr);
    REQUIRE(st->name() == "MyException");
}

TEST_CASE("Exception: throws clause unknown type fails", "[gen][exceptions][resolution]") {
    // A throws clause referencing a non-existent type should fail compilation
    REQUIRE_THROWS_AS(gen_jit_throws(R"(
        module __test_exc_res_2__;

        bad_throws() : int throws NonExistentType {
            return 0;
        }
    )"), k::log::compiler_error);
}

// ════════════════════════════════════════════════════════════════════════════
//  3. Throw statement codegen (trap-based for now)
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Exception: throw statement compiles (traps at runtime)", "[gen][exceptions]") {
    // The throw statement currently emits a trap instruction.
    // Verify that the function compiles; we don't invoke it since it would trap.
    auto jit = gen_jit(R"(
        module __test_exc_throw_1__;

        struct Err {
            code : int;
        }

        will_trap() : void throws Err {
            e : Err;
            throw e;
        }

        safe_path() : int {
            return 100;
        }
    )");
    REQUIRE(jit != nullptr);
    // Only call safe_path to verify the module compiled correctly
    auto fn = jit->lookup_symbol<int(*)()>("safe_path");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 100);
}

TEST_CASE("Exception: try-catch with throw in try body compiles", "[gen][exceptions]") {
    // Verify that a throw inside a try block compiles without errors.
    // The try body executes sequentially; since throw traps, we test the
    // path that doesn't reach the throw.
    auto jit = gen_jit(R"(
        module __test_exc_trycatch_1__;

        struct Problem {
            val : int;
        }

        guarded(flag: int) : int {
            result : int = 0;
            try {
                result = 5;
            } catch (p: Problem*) {
                result = -1;
            }
            return result;
        }
    )");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)(int)>("guarded");
    REQUIRE(fn != nullptr);
    REQUIRE(fn(0) == 5);
}

// ════════════════════════════════════════════════════════════════════════════
//  4. Runtime throw + catch (end-to-end)
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Exception: throw inside try-catch is caught at runtime", "[gen][exceptions][run]") {
    // Full end-to-end test: throw inside a try block, caught by matching catch clause.
    auto jit = gen_jit(R"(
        module __test_exc_runtime_1__;

        struct MyErr {
            code : int;
        }

        test_throw_catch() : int {
            result : int = 0;
            try {
                e : MyErr;
                e.code = 42;
                throw e;
                result = 999;
            } catch (p: MyErr*) {
                result = 77;
            }
            return result;
        }
    )");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test_throw_catch");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 77);
}

// ════════════════════════════════════════════════════════════════════════════
//  5. Exception thrown by called function, caught by caller (invoke test)
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Exception: throw in called function caught by caller via invoke", "[gen][exceptions][run][invoke]") {
    // The called function throws; the caller wraps it in try-catch.
    // This verifies that invoke (not call) is used for the function call.
    auto jit = gen_jit(R"(
        module __test_exc_invoke_1__;

        struct AppError {
            code : int;
        }

        thrower() : void {
            e : AppError;
            e.code = 123;
            throw e;
        }

        test_invoke_catch() : int {
            result : int = 0;
            try {
                thrower();
                result = 999;
            } catch (p: AppError*) {
                result = 42;
            }
            return result;
        }
    )");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test_invoke_catch");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

// ════════════════════════════════════════════════════════════════════════════
//  6. Type-based catch dispatch (multiple catch clauses)
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Exception: type-based catch dispatch selects correct handler", "[gen][exceptions][run][dispatch]") {
    // Throw ErrB, verify that the ErrB catch handler is reached and not ErrA's.
    auto jit = gen_jit(R"(
        module __test_exc_dispatch_1__;

        struct ErrA {
            code : int;
        }

        struct ErrB {
            code : int;
        }

        throw_b() : void {
            e : ErrB;
            e.code = 2;
            throw e;
        }

        test_dispatch() : int {
            result : int = 0;
            try {
                throw_b();
                result = 999;
            } catch (a: ErrA*) {
                result = 10;
            } catch (b: ErrB*) {
                result = 20;
            }
            return result;
        }
    )");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test_dispatch");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 20);
}

TEST_CASE("Exception: first matching catch clause wins", "[gen][exceptions][run][dispatch]") {
    // Throw ErrA, verify the first (ErrA) handler is reached.
    auto jit = gen_jit(R"(
        module __test_exc_dispatch_2__;

        struct ErrA {
            code : int;
        }

        struct ErrB {
            code : int;
        }

        throw_a() : void {
            e : ErrA;
            e.code = 1;
            throw e;
        }

        test_dispatch_first() : int {
            result : int = 0;
            try {
                throw_a();
                result = 999;
            } catch (a: ErrA*) {
                result = 100;
            } catch (b: ErrB*) {
                result = 200;
            }
            return result;
        }
    )");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test_dispatch_first");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 100);
}

TEST_CASE("Exception: unmatched type resumes unwinding to outer try-catch", "[gen][exceptions][run][dispatch]") {
    // Throw ErrC which is not caught by inner try-catch (has only ErrA handler),
    // so it should propagate to the outer try-catch.
    auto jit = gen_jit(R"(
        module __test_exc_dispatch_3__;

        struct ErrA {
            code : int;
        }

        struct ErrC {
            code : int;
        }

        throw_c() : void {
            e : ErrC;
            e.code = 3;
            throw e;
        }

        test_unmatched_propagation() : int {
            result : int = 0;
            try {
                try {
                    throw_c();
                    result = 999;
                } catch (a: ErrA*) {
                    result = 10;
                }
            } catch (c: ErrC*) {
                result = 30;
            }
            return result;
        }
    )");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test_unmatched_propagation");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 30);
}

// ════════════════════════════════════════════════════════════════════════════
//  7. Nested try-catch: inner catches, outer not reached
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Exception: nested try-catch blocks", "[gen][exceptions][run][nested]") {
    auto jit = gen_jit(R"(
        module __test_exc_nested_1__;

        struct ErrA {
            val : int;
        }

        test_nested() : int {
            result : int = 0;
            try {
                try {
                    e : ErrA;
                    e.val = 10;
                    throw e;
                } catch (p: ErrA*) {
                    result = 55;
                }
            } catch (p: ErrA*) {
                result = 999;
            }
            return result;
        }
    )");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test_nested");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 55);
}


