/*
 * K Language standard library — Exception tests
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
 * Tests for the K standard library exception hierarchy.
 *
 * Exercises:
 *  - Construction of Throwable / Exception / FatalError / OutOfMemory
 *  - getCode() returns the expected error code
 *  - Throw and catch stdlib exception types
 *  - Catch by base class (Throwable catches everything)
 */

#include <catch2/catch_message.hpp>
#include <catch2/catch_test_macros.hpp>
#include <cstddef>
#include <llvm/IR/DataLayout.h>
#include "../../klang/tests/helpers.hpp"
#include "../src/fatal_abi.h"

#ifndef LIBK_KDI_DIR
#error "LIBK_KDI_DIR not defined — set via CMake target_compile_definitions"
#endif
#ifndef LIBK_LIB_DIR
#error "LIBK_LIB_DIR not defined — set via CMake target_compile_definitions"
#endif

namespace {

/// Shorthand: compile K source that uses the base stdlib and JIT it.
std::unique_ptr<k::model::gen::jit> jit_k(std::string_view src) {
    return gen_jit_with_stdlib(src, LIBK_KDI_DIR, LIBK_LIB_DIR);
}

} // anonymous namespace

TEST_CASE("Fatal helpers: allocation layouts match imported exception types",
          "[libk][exception][fatal][abi]") {
    auto comp = compile_model_with_stdlib(R"SRC(
        module fatal_exception_allocation_layouts;
        probe(a: OutOfMemory&, b: IndexOutOfBoundsError&,
              c: NullDereferenceError&, d: NullAssignationError&, e: NullCastError&) {}
    )SRC");
    REQUIRE(comp != nullptr);
    auto context = comp->get_context_for_test();
    const auto& layout = context->module().getDataLayout();
    const struct {
        const char* name;
        std::size_t size;
    } exceptions[] = {
        {"Throwable", sizeof(k_throwable_layout)},
        {"FatalError", sizeof(k_fatal_error_layout)},
        {"NullPointerError", sizeof(k_null_pointer_error_layout)},
        {"OutOfMemory", sizeof(k_direct_fatal_error_layout)},
        {"IndexOutOfBoundsError", sizeof(k_direct_fatal_error_layout)},
        {"NullDereferenceError", sizeof(k_null_fatal_error_layout)},
        {"NullAssignationError", sizeof(k_null_fatal_error_layout)},
        {"NullCastError", sizeof(k_null_fatal_error_layout)},
    };
    for (const auto& exception : exceptions) {
        INFO(exception.name);
        auto aggregate = comp->get_unit()->get_or_create_imported_aggregate(
            k::name{false, {"k", exception.name}}, context);
        REQUIRE(aggregate != nullptr);
        auto* llvm_type = context->get_llvm_type(aggregate->get_struct_type());
        REQUIRE(llvm_type != nullptr);
        CHECK(layout.getTypeAllocSize(llvm_type).getFixedValue() == exception.size);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
//  1. Throwable and Exception construction and getCode()
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Throwable: default constructor gives code 0", "[libk][exception]") {
    auto jit = jit_k(R"SRC(
        module __test_throwable_default__;

        test() : int {
            e : Throwable;
            return e.getCode();
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 0);
}

TEST_CASE("Exception: default constructor gives code 0", "[libk][exception]") {
    auto jit = jit_k(R"SRC(
        module __test_exc_default__;

        test() : int {
            e : Exception;
            return e.getCode();
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 0);
}

TEST_CASE("Exception: constructor with code", "[libk][exception]") {
    auto jit = jit_k(R"SRC(
        module __test_exc_code__;

        test() : int {
            e : Exception(42);
            return e.getCode();
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

TEST_CASE("FatalError: default constructor gives code 0", "[libk][exception]") {
    auto jit = jit_k(R"SRC(
        module __test_fatal_default__;

        test() : int {
            e : FatalError;
            return e.getCode();
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 0);
}

TEST_CASE("OutOfMemory: default code is 1", "[libk][exception]") {
    auto jit = jit_k(R"SRC(
        module __test_oom__;

        test() : int {
            e : OutOfMemory;
            return e.getCode();
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 1);
}

TEST_CASE("NullPointerException: default code is 2", "[libk][exception]") {
    auto jit = jit_k(R"SRC(
        module __test_npe__;

        test() : int {
            e : NullPointerException;
            return e.getCode();
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 2);
}

TEST_CASE("IndexOutOfBoundsException: default code is 3", "[libk][exception]") {
    auto jit = jit_k(R"SRC(
        module __test_ioobe__;

        test() : int {
            e : IndexOutOfBoundsException;
            return e.getCode();
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 3);
}

// ═════════════════════════════════════════════════════════════════════════════
//  2. Throw Exception base class
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Exception: throwing Exception itself compiles", "[libk][exception]") {
    // Exception itself should be throwable
    auto jit = jit_k(R"SRC(
        module __test_exc_throw_base__;

        thrower() : void throws(Exception) {
            e : Exception(99);
            throw e;
        }

        safe() : int { return 1; }
    )SRC");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("safe");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 1);
}

// ═════════════════════════════════════════════════════════════════════════════
//  3. Throw and catch stdlib exception types
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Exception: throw and catch OutOfMemory", "[libk][exception][run][throw-class]") {
    auto jit = jit_k(R"SRC(
        module __test_exc_throw_catch__;

        thrower() : void {
            throw OutOfMemory();
        }

        test() : int {
            result : int = 0;
            try {
                thrower();
                result = 999;
            } catch (e: OutOfMemory&) {
                result = e.getCode();
            }
            return result;
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 1);
}

TEST_CASE("Exception: catch by base Throwable", "[libk][exception][run][throw-class]") {
    auto jit = jit_k(R"SRC(
        module __test_exc_catch_base__;

        thrower() : void throws(NullPointerException) {
            throw NullPointerException();
        }

        test() : int {
            result : int = 0;
            try {
                thrower();
                result = 999;
            } catch (e: Throwable&) {
                result = e.getCode();
            }
            return result;
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 2);
}
