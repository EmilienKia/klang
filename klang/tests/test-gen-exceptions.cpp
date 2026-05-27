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
 * Tests for exception handling: throw, try-catch, throws clause, contract verification.
 *
 * All throwable types must derive from ::k::Exception (auto-imported from the stdlib).
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"


// ════════════════════════════════════════════════════════════════════════════
//  1. Basic try-catch syntax acceptance
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Exception: try-catch parses without error", "[gen][exceptions]") {
    // Verify that the pipeline accepts try-catch syntax and executes the try body
    // normally when no exception is thrown.
    auto jit = gen_jit(R"(
        module __test_exc_1__;

        try_but_no_throw() : int {
            result : int = 0;
            try {
                result = 42;
            } catch (e: Exception*) {
                result = -1;
            }
            return result;
        }
    )");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("try_but_no_throw");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

TEST_CASE("Exception: multiple catch clauses parse", "[gen][exceptions]") {
    auto jit = gen_jit(R"(
        module __test_exc_2__;

        class ErrA : public Exception { }
        class ErrB : public Exception { }

        multi_catch() : int {
            result : int = 0;
            try {
                result = 10;
            } catch (e: ErrA*) {
                result = -1;
            } catch (e: ErrB*) {
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

        class MyError : public Exception { }

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

        class ErrorA : public Exception { }
        class ErrorB : public Exception { }

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
    auto comp = compile_model_with_stdlib(R"(
        module __test_exc_res_1__;

        class MyException : public Exception { }

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
//  3. Throw statement codegen
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Exception: throw statement compiles", "[gen][exceptions]") {
    // Verify that a throw of an Exception-derived class compiles correctly.
    auto jit = gen_jit(R"(
        module __test_exc_throw_1__;

        class Err : public Exception { }

        will_throw() : void throws Err {
            e : Err;
            throw e;
        }

        safe_path() : int {
            return 100;
        }
    )");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("safe_path");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 100);
}

TEST_CASE("Exception: try-catch with throw in try body compiles", "[gen][exceptions]") {
    auto jit = gen_jit(R"(
        module __test_exc_trycatch_1__;

        class Problem : public Exception { }

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
    auto jit = gen_jit(R"(
        module __test_exc_runtime_1__;

        class MyErr : public Exception { }

        test_throw_catch() : int {
            result : int = 0;
            try {
                e : MyErr;
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
    auto jit = gen_jit(R"(
        module __test_exc_invoke_1__;

        class AppError : public Exception { }

        thrower() : void {
            e : AppError;
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
    auto jit = gen_jit(R"(
        module __test_exc_dispatch_1__;

        class ErrA : public Exception { }
        class ErrB : public Exception { }

        throw_b() : void {
            e : ErrB;
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
    auto jit = gen_jit(R"(
        module __test_exc_dispatch_2__;

        class ErrA : public Exception { }
        class ErrB : public Exception { }

        throw_a() : void {
            e : ErrA;
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
    auto jit = gen_jit(R"(
        module __test_exc_dispatch_3__;

        class ErrA : public Exception { }
        class ErrC : public Exception { }

        throw_c() : void {
            e : ErrC;
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

        class ErrA : public Exception { }

        test_nested() : int {
            result : int = 0;
            try {
                try {
                    e : ErrA;
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

// ════════════════════════════════════════════════════════════════════════════
//  8. Exception contract checker
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Exception contract: throw undeclared type in function with throws clause fails",
          "[gen][exceptions][contract]") {
    // Function declares 'throws ErrA' but throws ErrB → should fail
    REQUIRE_THROWS_AS(gen_jit_throws(R"(
        module __test_exc_contract_1__;

        class ErrA : public Exception { }
        class ErrB : public Exception { }

        risky() : void throws ErrA {
            e : ErrB;
            throw e;
        }
    )"), k::log::compiler_error);
}

TEST_CASE("Exception contract: throw declared type in function with throws clause passes",
          "[gen][exceptions][contract]") {
    auto jit = gen_jit(R"(
        module __test_exc_contract_2__;

        class ErrA : public Exception { }

        risky() : void throws ErrA {
            e : ErrA;
            throw e;
        }

        safe() : int { return 42; }
    )");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("safe");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

TEST_CASE("Exception contract: throw inside try-catch does not require throws clause",
          "[gen][exceptions][contract]") {
    auto jit = gen_jit(R"(
        module __test_exc_contract_3__;

        class ErrA : public Exception { }
        class ErrB : public Exception { }

        guarded() : int throws ErrA {
            result : int = 0;
            try {
                e : ErrB;
                throw e;
            } catch (b: ErrB*) {
                result = 99;
            }
            return result;
        }
    )");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("guarded");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 99);
}

TEST_CASE("Exception contract: call to throwing function not handled fails",
          "[gen][exceptions][contract]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"(
        module __test_exc_contract_4__;

        class ErrA : public Exception { }
        class ErrB : public Exception { }

        thrower() : void throws ErrB {
            e : ErrB;
            throw e;
        }

        caller() : void throws ErrA {
            thrower();
        }
    )"), k::log::compiler_error);
}

TEST_CASE("Exception contract: call to throwing function propagated via throws clause passes",
          "[gen][exceptions][contract]") {
    auto jit = gen_jit(R"(
        module __test_exc_contract_5__;

        class ErrB : public Exception { }

        thrower() : void throws ErrB {
            e : ErrB;
            throw e;
        }

        caller() : void throws ErrB {
            thrower();
        }

        safe() : int { return 7; }
    )");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("safe");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 7);
}

TEST_CASE("Exception contract: call to throwing function caught in try-catch passes",
          "[gen][exceptions][contract]") {
    auto jit = gen_jit(R"(
        module __test_exc_contract_6__;

        class ErrA : public Exception { }
        class ErrB : public Exception { }

        thrower() : void throws ErrB {
            e : ErrB;
            throw e;
        }

        caller() : int throws ErrA {
            result : int = 0;
            try {
                thrower();
            } catch (b: ErrB*) {
                result = 55;
            }
            return result;
        }
    )");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("caller");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 55);
}

TEST_CASE("Exception contract: function without throws clause can throw freely",
          "[gen][exceptions][contract]") {
    auto jit = gen_jit(R"(
        module __test_exc_contract_7__;

        class AnyErr : public Exception { }

        unspec_thrower() : void {
            e : AnyErr;
            throw e;
        }

        safe() : int { return 123; }
    )");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("safe");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 123);
}

TEST_CASE("Exception contract: calling unspec function from throws function needs no handling",
          "[gen][exceptions][contract]") {
    auto jit = gen_jit(R"(
        module __test_exc_contract_8__;

        class ErrA : public Exception { }

        unspec() : void {
        }

        caller() : void throws ErrA {
            unspec();
        }

        safe() : int { return 77; }
    )");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("safe");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 77);
}

// ════════════════════════════════════════════════════════════════════════════
//  9. Compile-time rejection of non-Exception types
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Exception: throwing a non-Exception struct fails compilation",
          "[gen][exceptions][contract]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"(
        module __test_exc_reject_struct__;

        struct NotAnException {
            code : int;
        }

        thrower() : void {
            e : NotAnException;
            throw e;
        }
    )"), k::log::compiler_error);
}

TEST_CASE("Exception: throwing a non-Exception class fails compilation",
          "[gen][exceptions][contract]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"(
        module __test_exc_reject_class__;

        class NotAnException {
            public:
            getVal() : int { return 0; }
        }

        thrower() : void {
            e : NotAnException;
            throw e;
        }
    )"), k::log::compiler_error);
}

