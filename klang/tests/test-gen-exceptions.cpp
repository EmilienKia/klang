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

// ════════════════════════════════════════════════════════════════════════════
//  10. Throw via temporary construction (polymorphic classes)
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Exception: throw temporary construction of Exception-derived class",
          "[gen][exceptions][run][temp-throw]") {
    // First verify that calling getCode() on a caught derived exception works
    // even with a local variable (non-temporary) throw
    auto jit = gen_jit(R"(
        module __test_exc_temp_throw_1__;

        class MyErr : public Exception {
            public:
            MyErr(code: int) : Exception(code) { }
        }

        test_throw_local_getcode() : int {
            result : int = 0;
            try {
                e : MyErr(42);
                throw e;
                result = 999;
            } catch (e: MyErr&) {
                result = e.getCode();
            }
            return result;
        }

        test_throw_temp() : int {
            result : int = 0;
            try {
                throw MyErr(42);
                result = 999;
            } catch (e: MyErr&) {
                result = e.getCode();
            }
            return result;
        }
    )");
    REQUIRE(jit != nullptr);
    // Test local variable throw + getCode
    auto fn_local = jit->lookup_symbol<int(*)()>("test_throw_local_getcode");
    REQUIRE(fn_local != nullptr);
    REQUIRE(fn_local() == 42);
    // Test temporary throw + getCode
    auto fn_temp = jit->lookup_symbol<int(*)()>("test_throw_temp");
    REQUIRE(fn_temp != nullptr);
    REQUIRE(fn_temp() == 42);
}

TEST_CASE("Exception: throw temporary construction of base Exception",
          "[gen][exceptions][run][temp-throw]") {
    auto jit = gen_jit(R"(
        module __test_exc_temp_throw_2__;

        test_throw_base() : int {
            result : int = 0;
            try {
                throw Exception(99);
                result = 999;
            } catch (e: Exception&) {
                result = e.getCode();
            }
            return result;
        }
    )");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test_throw_base");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 99);
}

TEST_CASE("Exception: throw temporary caught by base class reference",
          "[gen][exceptions][run][temp-throw]") {
    auto jit = gen_jit(R"(
        module __test_exc_temp_throw_3__;

        class AppError : public Exception {
            public:
            AppError() : Exception(77) { }
        }

        test_throw_temp_base_catch() : int {
            result : int = 0;
            try {
                throw AppError();
                result = 999;
            } catch (e: Exception&) {
                result = e.getCode();
            }
            return result;
        }
    )");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test_throw_temp_base_catch");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 77);
}

TEST_CASE("Exception: throw temporary in called function",
          "[gen][exceptions][run][temp-throw]") {
    auto jit = gen_jit(R"(
        module __test_exc_temp_throw_4__;

        class NetErr : public Exception {
            public:
            NetErr(code: int) : Exception(code) { }
        }

        thrower() : void {
            throw NetErr(55);
        }

        test_throw_temp_callee() : int {
            result : int = 0;
            try {
                thrower();
                result = 999;
            } catch (e: NetErr&) {
                result = e.getCode();
            }
            return result;
        }
    )");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test_throw_temp_callee");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 55);
}

// ════════════════════════════════════════════════════════════════════════════
//  11. Constructor throws clause — contract enforcement and runtime behaviour
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Exception contract: constructor with throws clause — new caught in try-catch",
          "[gen][exceptions][contract][constructor]") {
    // A class whose constructor declares 'throws ErrA'.
    // Calling new inside a try-catch that catches ErrA should compile and run.
    auto jit = gen_jit(R"(
        module __test_exc_ctor_1__;

        class ErrA : public Exception {
            public:
            ErrA(code: int) : Exception(code) { }
        }

        class Widget {
            val : int;
            public:
            Widget(v: int) throws ErrA {
                if (v < 0) {
                    throw ErrA(v);
                }
                val = v;
            }
            getVal() : int { return val; }
        }

        test_ctor_no_throw() : int {
            result : int = 0;
            try {
                w : Widget! = new Widget(42);
                result = w->getVal();
                delete w;
            } catch (e: ErrA&) {
                result = -1;
            }
            return result;
        }

        test_ctor_throws() : int {
            result : int = 0;
            try {
                w : Widget! = new Widget(-5);
                result = w->getVal();
                delete w;
            } catch (e: ErrA&) {
                result = e.getCode();
            }
            return result;
        }
    )");
    REQUIRE(jit != nullptr);
    auto fn_ok = jit->lookup_symbol<int(*)()>("test_ctor_no_throw");
    REQUIRE(fn_ok != nullptr);
    REQUIRE(fn_ok() == 42);

    auto fn_throw = jit->lookup_symbol<int(*)()>("test_ctor_throws");
    REQUIRE(fn_throw != nullptr);
    REQUIRE(fn_throw() == -5);
}

TEST_CASE("Exception contract: constructor with throws clause — new not handled fails",
          "[gen][exceptions][contract][constructor]") {
    // A function with a throws clause that does NOT cover the constructor's exception.
    // Calling new without try-catch should produce a compile error.
    REQUIRE_THROWS_AS(gen_jit_throws(R"(
        module __test_exc_ctor_2__;

        class ErrA : public Exception { }
        class ErrB : public Exception { }

        class Widget {
            val : int;
            public:
            Widget(v: int) throws ErrA {
                val = v;
            }
        }

        caller() : int throws ErrB {
            w : Widget! = new Widget(10);
            delete w;
            return 0;
        }
    )"), k::log::compiler_error);
}

TEST_CASE("Exception contract: constructor with throws clause — propagated via caller throws",
          "[gen][exceptions][contract][constructor]") {
    // A function that declares the same exception as the constructor should compile fine.
    auto jit = gen_jit(R"(
        module __test_exc_ctor_3__;

        class ErrA : public Exception {
            public:
            ErrA(code: int) : Exception(code) { }
        }

        class Widget {
            val : int;
            public:
            Widget(v: int) throws ErrA {
                if (v < 0) {
                    throw ErrA(v);
                }
                val = v;
            }
            getVal() : int { return val; }
        }

        make_widget(v: int) : Widget! throws ErrA {
            return new Widget(v);
        }

        safe() : int { return 99; }
    )");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("safe");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 99);
}

TEST_CASE("Exception contract: struct constructor with throws clause — local var caught in try-catch",
          "[gen][exceptions][contract][constructor]") {
    // A struct whose constructor declares 'throws ErrA'.
    // Constructing a local variable inside a try-catch should compile and work.
    auto jit = gen_jit(R"(
        module __test_exc_ctor_4__;

        class ErrA : public Exception {
            public:
            ErrA(code: int) : Exception(code) { }
        }

        struct Point {
            x : int;
            y : int;
            Point(px: int, py: int) throws ErrA {
                if (px < 0) {
                    throw ErrA(px);
                }
                x = px;
                y = py;
            }
        }

        test_struct_ctor_ok() : int {
            result : int = 0;
            try {
                p : Point(5, 10);
                result = p.x + p.y;
            } catch (e: ErrA&) {
                result = -1;
            }
            return result;
        }

        test_struct_ctor_throws() : int {
            result : int = 0;
            try {
                p : Point(-3, 10);
                result = p.x + p.y;
            } catch (e: ErrA&) {
                result = e.getCode();
            }
            return result;
        }
    )");
    REQUIRE(jit != nullptr);
    auto fn_ok = jit->lookup_symbol<int(*)()>("test_struct_ctor_ok");
    REQUIRE(fn_ok != nullptr);
    REQUIRE(fn_ok() == 15);

    auto fn_throw = jit->lookup_symbol<int(*)()>("test_struct_ctor_throws");
    REQUIRE(fn_throw != nullptr);
    REQUIRE(fn_throw() == -3);
}

TEST_CASE("Exception contract: struct constructor with throws clause — local var not handled fails",
          "[gen][exceptions][contract][constructor]") {
    // A struct constructor with throws clause, and the caller declares a different exception.
    REQUIRE_THROWS_AS(gen_jit_throws(R"(
        module __test_exc_ctor_5__;

        class ErrA : public Exception { }
        class ErrB : public Exception { }

        struct Point {
            x : int;
            y : int;
            Point(px: int, py: int) throws ErrA {
                x = px;
                y = py;
            }
        }

        caller() : int throws ErrB {
            p : Point(5, 10);
            return p.x + p.y;
        }
    )"), k::log::compiler_error);
}

TEST_CASE("Exception contract: temporary construction with throws clause — caught in try-catch",
          "[gen][exceptions][contract][constructor]") {
    // Throwing constructor via temporary construction inside try-catch should work.
    auto jit = gen_jit(R"(
        module __test_exc_ctor_6__;

        class ErrA : public Exception {
            public:
            ErrA(code: int) : Exception(code) { }
        }

        class Gadget : public Exception {
            public:
            Gadget(v: int) throws ErrA {
                if (v < 0) {
                    throw ErrA(v);
                }
            }
        }

        test_temp_ctor() : int {
            result : int = 0;
            try {
                throw Gadget(10);
            } catch (g: Gadget&) {
                result = 1;
            } catch (e: ErrA&) {
                result = e.getCode();
            }
            return result;
        }

        test_temp_ctor_throws() : int {
            result : int = 0;
            try {
                throw Gadget(-7);
            } catch (g: Gadget&) {
                result = 1;
            } catch (e: ErrA&) {
                result = e.getCode();
            }
            return result;
        }
    )");
    REQUIRE(jit != nullptr);
    auto fn_ok = jit->lookup_symbol<int(*)()>("test_temp_ctor");
    REQUIRE(fn_ok != nullptr);
    REQUIRE(fn_ok() == 1);

    auto fn_throw = jit->lookup_symbol<int(*)()>("test_temp_ctor_throws");
    REQUIRE(fn_throw != nullptr);
    REQUIRE(fn_throw() == -7);
}

TEST_CASE("Exception contract: constructor without throws clause — no enforcement",
          "[gen][exceptions][contract][constructor]") {
    // A constructor without a throws clause should not trigger any contract check.
    auto jit = gen_jit(R"(
        module __test_exc_ctor_7__;

        class ErrA : public Exception { }

        class Simple {
            val : int;
            public:
            Simple(v: int) {
                val = v;
            }
            getVal() : int { return val; }
        }

        caller() : int throws ErrA {
            s : Simple! = new Simple(42);
            result : int = s->getVal();
            delete s;
            return result;
        }
    )");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("caller");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

// ════════════════════════════════════════════════════════════════════════════
//  12. Cross-module constructor throws — import library with throwing ctor
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Exception contract: cross-module constructor throws — local and new",
          "[gen][exceptions][contract][constructor][import][e2e]") {
    // Module A defines an exception and a class whose constructor throws it.
    // Module B imports A, constructs instances both locally and via new,
    // and catches the exception in both cases.
    auto result = build_exec_with_lib(
        // ── Library module ──
        R"K(
            module exc_lib;

            public:

            class InitError : public Exception {
                public:
                InitError(code: int) : Exception(code) { }
            }

            class Sensor {
                value : int;
                public:
                Sensor(v: int) throws InitError {
                    if (v < 0) {
                        throw InitError(v);
                    }
                    value = v;
                }
                getValue() : int { return value; }
            }
        )K",
        // ── Executable module ──
        R"K(
            module exc_app;
            import exc_lib;

            test_local_ok() : int {
                result : int = 0;
                try {
                    s : exc_lib::Sensor(10);
                    result = s.getValue();
                } catch (e: exc_lib::InitError&) {
                    result = -1;
                }
                return result;
            }

            test_local_throws() : int {
                result : int = 0;
                try {
                    s : exc_lib::Sensor(-7);
                    result = s.getValue();
                } catch (e: exc_lib::InitError&) {
                    result = e.getCode();
                }
                return result;
            }

            test_new_ok() : int {
                result : int = 0;
                try {
                    s : exc_lib::Sensor! = new exc_lib::Sensor(20);
                    result = s->getValue();
                    delete s;
                } catch (e: exc_lib::InitError&) {
                    result = -1;
                }
                return result;
            }

            test_new_throws() : int {
                result : int = 0;
                try {
                    s : exc_lib::Sensor! = new exc_lib::Sensor(-3);
                    result = s->getValue();
                    delete s;
                } catch (e: exc_lib::InitError&) {
                    result = e.getCode();
                }
                return result;
            }

            main() : int {
                r1 : int = test_local_ok();
                if (r1 != 10) { return 1; }

                r2 : int = test_local_throws();
                if (r2 != -7) { return 2; }

                r3 : int = test_new_ok();
                if (r3 != 20) { return 3; }

                r4 : int = test_new_throws();
                if (r4 != -3) { return 4; }

                return 42;
            }
        )K"
    );

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);

    REQUIRE(result.exit_code == 42);
}

// ════════════════════════════════════════════════════════════════════════════
//  13. ConstructionException — UniSlot/MultiSlot wrap constructor exceptions
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("ConstructionException: UniSlot construct catches throwing constructor",
          "[gen][exceptions][construction][intrinsic]") {
    // A class whose constructor throws when given a negative value.
    // UniSlot::construct wraps the exception as ConstructionException.
    auto jit = gen_jit(R"(
        module __test_ce_unislot_1__;

        class InitErr : public Exception {
            public:
            InitErr(code: int) : Exception(code) { }
        }

        class Widget {
            val : int;
            public:
            Widget(v: int) throws InitErr {
                if (v < 0) {
                    throw InitErr(v);
                }
                val = v;
            }
            getVal() : int { return val; }
        }

        test_unislot_ok() : int {
            slot : UniSlot<Widget>;
            try {
                slot.construct<int>(42);
            } catch (e: ConstructionException&) {
                return -1;
            }
            result : int = slot.get().getVal();
            slot.destruct();
            return result;
        }

        test_unislot_throws() : int {
            slot : UniSlot<Widget>;
            try {
                slot.construct<int>(-7);
                return 999;
            } catch (e: ConstructionException&) {
                return e.getCode();
            }
            return -999;
        }
    )");
    REQUIRE(jit != nullptr);

    auto fn_ok = jit->lookup_symbol<int(*)()>("test_unislot_ok");
    REQUIRE(fn_ok != nullptr);
    REQUIRE(fn_ok() == 42);

    auto fn_throw = jit->lookup_symbol<int(*)()>("test_unislot_throws");
    REQUIRE(fn_throw != nullptr);
    REQUIRE(fn_throw() == 6);  // ConstructionException default code is 6
}

TEST_CASE("ConstructionException: MultiSlot construct catches throwing constructor",
          "[gen][exceptions][construction][intrinsic]") {
    auto jit = gen_jit(R"(
        module __test_ce_multislot_1__;

        class InitErr : public Exception {
            public:
            InitErr(code: int) : Exception(code) { }
        }

        class Widget {
            val : int;
            public:
            Widget(v: int) throws InitErr {
                if (v < 0) {
                    throw InitErr(v);
                }
                val = v;
            }
            getVal() : int { return val; }
        }

        test_multislot_ok() : int {
            slots : MultiSlot<Widget>;
            slots.allocate(4);
            try {
                slots.construct<int>(0, 10);
                slots.construct<int>(1, 20);
            } catch (e: ConstructionException&) {
                slots.deallocate();
                return -1;
            }
            result : int = slots.get(0).getVal() + slots.get(1).getVal();
            slots.destruct(0);
            slots.destruct(1);
            slots.deallocate();
            return result;
        }

        test_multislot_throws() : int {
            slots : MultiSlot<Widget>;
            slots.allocate(4);
            try {
                slots.construct<int>(0, 10);
                slots.construct<int>(1, -3);
                slots.destruct(0);
                slots.deallocate();
                return 999;
            } catch (e: ConstructionException&) {
                slots.destruct(0);
                slots.deallocate();
                return e.getCode();
            }
            return -999;
        }
    )");
    REQUIRE(jit != nullptr);

    auto fn_ok = jit->lookup_symbol<int(*)()>("test_multislot_ok");
    REQUIRE(fn_ok != nullptr);
    REQUIRE(fn_ok() == 30);

    auto fn_throw = jit->lookup_symbol<int(*)()>("test_multislot_throws");
    REQUIRE(fn_throw != nullptr);
    REQUIRE(fn_throw() == 6);  // ConstructionException default code is 6
}

TEST_CASE("ConstructionException: UniSlot construct — non-throwing constructor works normally",
          "[gen][exceptions][construction][intrinsic]") {
    // A simple struct without throws clause — construct() still declares throws
    // ConstructionException, so the caller must declare or catch it.
    auto jit = gen_jit(R"(
        module __test_ce_unislot_nothrow__;

        struct Point {
            x : int;
            y : int;
            Point(px: int, py: int) {
                x = px;
                y = py;
            }
        }

        test_nothrow() : int {
            slot : UniSlot<Point>;
            slot.construct<int, int>(3, 7);
            result : int = slot.get().x + slot.get().y;
            slot.destruct();
            return result;
        }
    )");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test_nothrow");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 10);
}

TEST_CASE("ConstructionException: no throws declaration needed — FatalError propagates freely",
          "[gen][exceptions][construction][contract]") {
    // ConstructionException is now a FatalError, so calling UniSlot::construct
    // without declaring it in a throws clause should compile successfully.
    auto jit = gen_jit(R"(
        module __test_ce_contract_1__;

        class InitErr : public Exception { }

        class Widget {
            val : int;
            public:
            Widget(v: int) throws InitErr {
                val = v;
            }
        }

        caller() : int throws InitErr {
            slot : UniSlot<Widget>;
            slot.construct<int>(10);
            return 0;
        }
    )");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("caller");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 0);
}

// =============================================================================
// FatalError runtime exceptions: NullPointerError hierarchy
// =============================================================================

TEST_CASE("NullDereferenceError: catch null dereference in try-catch",
          "[gen][exceptions][fatal][null-deref]") {
    auto res = build_and_exec(R"(
        module __test_null_deref_catch__;

        main() : int {
            p : int* = null;
            try {
                x : int = *p;
                return x;
            } catch(e : NullDereferenceError&) {
                return e.getCode();
            }
        }
    )");
    REQUIRE(res.exit_code == 8);
}

TEST_CASE("NullPointerError: catch base class matches dereference",
          "[gen][exceptions][fatal][null-base]") {
    auto res = build_and_exec(R"(
        module __test_null_base_catch__;

        main() : int {
            p : int* = null;
            try {
                x : int = *p;
                return x;
            } catch(e : NullPointerError&) {
                return e.getCode();
            }
        }
    )");
    REQUIRE(res.exit_code == 8);
}

TEST_CASE("FatalError: catch base class matches null dereference",
          "[gen][exceptions][fatal][fatal-base]") {
    auto res = build_and_exec(R"(
        module __test_fatal_base_catch__;

        main() : int {
            p : int* = null;
            try {
                x : int = *p;
                return x;
            } catch(e : FatalError&) {
                return e.getCode();
            }
        }
    )");
    REQUIRE(res.exit_code == 8);
}

TEST_CASE("NullAssignationError: catch null-to-link rebind in try-catch",
          "[gen][exceptions][fatal][null-assign]") {
    auto res = build_and_exec(R"(
        module __test_null_assign_catch__;

        main() : int {
            v : int = 42;
            lnk : int+ = v;
            p : int* = null;
            try {
                lnk = p;
                return 0;
            } catch(e : NullAssignationError&) {
                return e.getCode();
            }
        }
    )");
    REQUIRE(res.exit_code == 9);
}

TEST_CASE("NullDereferenceError: uncaught terminates process",
          "[gen][exceptions][fatal][null-terminate]") {
    auto res = build_and_exec(R"(
        module __test_null_deref_term__;
        main() : int {
            p : int* = null;
            x : int = *p;
            return x;
        }
    )");
    REQUIRE(res.exit_code != 0);
}

TEST_CASE("IndexOutOfBoundsError: catch array OOB in try-catch",
          "[gen][exceptions][fatal][oob]") {
    auto res = build_and_exec(R"(
        module __test_oob_catch__;

        main() : int {
            a : int[3];
            a[0] = 10;
            a[1] = 20;
            a[2] = 30;
            try {
                x : int = a[5];
                return x;
            } catch(e : IndexOutOfBoundsError&) {
                return e.getCode();
            }
        }
    )");
    REQUIRE(res.exit_code == 11);
}

TEST_CASE("IndexOutOfBoundsError: uncaught terminates process",
          "[gen][exceptions][fatal][oob-terminate]") {
    auto res = build_and_exec(R"(
        module __test_oob_term__;
        main() : int {
            a : int[3];
            a[0] = 1;
            a[3] = 99;
            return 0;
        }
    )");
    REQUIRE(res.exit_code != 0);
}


// ════════════════════════════════════════════════════════════════════════════
//  Rethrow — bare 'throw;' inside catch blocks
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Exception: bare throw rethrows caught exception to outer handler", "[gen][exceptions][rethrow][run]") {
    // A bare 'throw;' inside a catch block should rethrow the current exception
    // to an enclosing try-catch handler.
    auto jit = gen_jit(R"(
        module __test_exc_rethrow_1__;

        class MyErr : public Exception {
            public:
            MyErr() : Exception(42) { }
        }

        test_rethrow() : int {
            result : int = 0;
            try {
                try {
                    throw MyErr();
                } catch (e: MyErr&) {
                    result = 1;
                    throw;
                }
            } catch (e: MyErr&) {
                result = result + e.getCode();
            }
            return result;
        }
    )");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test_rethrow");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 43);  // 1 + 42
}

TEST_CASE("Exception: bare throw rethrows to caller", "[gen][exceptions][rethrow][run]") {
    // A bare 'throw;' inside a catch block rethrows the exception to the calling function.
    auto jit = gen_jit(R"(
        module __test_exc_rethrow_2__;

        class AppErr : public Exception {
            public:
            AppErr() : Exception(7) { }
        }

        rethrower() : void throws AppErr {
            try {
                throw AppErr();
            } catch (e: AppErr&) {
                throw;
            }
        }

        test_rethrow_caller() : int {
            result : int = 0;
            try {
                rethrower();
            } catch (e: AppErr&) {
                result = e.getCode();
            }
            return result;
        }
    )");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test_rethrow_caller");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 7);
}

TEST_CASE("Exception: bare throw outside catch block fails compilation", "[gen][exceptions][rethrow][resolution]") {
    // A bare 'throw;' that is not inside a catch block should produce a compile-time error.
    REQUIRE_THROWS_AS(gen_jit_throws(R"(
        module __test_exc_rethrow_err_1__;

        class MyErr : public Exception { }

        bad_rethrow() : void throws MyErr {
            throw;
        }
    )"), k::log::compiler_error);
}

TEST_CASE("Exception: bare throw outside catch in function body fails", "[gen][exceptions][rethrow][resolution]") {
    // A bare 'throw;' in a try body (not catch) should fail.
    REQUIRE_THROWS_AS(gen_jit_throws(R"(
        module __test_exc_rethrow_err_2__;

        class MyErr : public Exception { }

        bad_rethrow() : void throws MyErr {
            try {
                throw;
            } catch (e: MyErr&) {
            }
        }
    )"), k::log::compiler_error);
}

TEST_CASE("Exception: bare throw rethrows FatalError (unchecked)", "[gen][exceptions][rethrow][run]") {
    // FatalError-derived exceptions are unchecked, but rethrow should still work.
    auto jit = gen_jit(R"(
        module __test_exc_rethrow_fatal_1__;

        class CritErr : public FatalError {
            public:
            CritErr() : FatalError(99) { }
        }

        test_rethrow_fatal() : int {
            result : int = 0;
            try {
                try {
                    throw CritErr();
                } catch (e: CritErr&) {
                    throw;
                }
            } catch (e: CritErr&) {
                result = e.getCode();
            }
            return result;
        }
    )");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test_rethrow_fatal");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 99);
}

TEST_CASE("Exception: bare throw with contract — caught by outer try-catch", "[gen][exceptions][rethrow][contract]") {
    // Rethrow inside a catch, with an enclosing outer try-catch that handles the type.
    // This should compile even if the function has a throws spec that does NOT include the type.
    auto jit = gen_jit(R"(
        module __test_exc_rethrow_contract_1__;

        class ErrA : public Exception {
            public:
            ErrA() : Exception(11) { }
        }

        test_rethrow_contract() : int {
            result : int = 0;
            try {
                try {
                    throw ErrA();
                } catch (e: ErrA&) {
                    throw;
                }
            } catch (e: ErrA&) {
                result = e.getCode();
            }
            return result;
        }
    )");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test_rethrow_contract");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 11);
}

TEST_CASE("Exception: bare throw with contract — declared in throws clause", "[gen][exceptions][rethrow][contract]") {
    // Rethrow inside a catch, function declares exception in throws clause — should compile.
    auto jit = gen_jit(R"(
        module __test_exc_rethrow_contract_2__;

        class ErrB : public Exception {
            public:
            ErrB() : Exception(55) { }
        }

        rethrower_declared() : void throws ErrB {
            try {
                throw ErrB();
            } catch (e: ErrB&) {
                throw;
            }
        }

        test_rethrow_declared() : int {
            result : int = 0;
            try {
                rethrower_declared();
            } catch (e: ErrB&) {
                result = e.getCode();
            }
            return result;
        }
    )");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test_rethrow_declared");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 55);
}

