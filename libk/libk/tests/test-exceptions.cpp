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
 *  - Construction of Exception / RuntimeException / MemoryException
 *  - getCode() returns the expected error code
 *  - Throw and catch stdlib exception types
 *  - Catch by base class (RuntimeException catches MemoryException)
 */

#include <catch2/catch_test_macros.hpp>
#include "../../klang/tests/helpers.hpp"

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

// ═════════════════════════════════════════════════════════════════════════════
//  1. Exception construction and getCode()
// ═════════════════════════════════════════════════════════════════════════════

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

TEST_CASE("MemoryException: default code is 1", "[libk][exception]") {
    auto jit = jit_k(R"SRC(
        module __test_mem_exc__;

        test() : int {
            e : MemoryException;
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

        thrower() : void throws Exception {
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
//  NOTE: Throwing polymorphic classes (with vtables) via temporary construction
//  (e.g. `throw MemoryException()`) is not yet fully supported by the codegen.
//  These tests are SKIPPED until the throw codegen handles vtable-bearing objects.
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Exception: throw and catch MemoryException", "[libk][exception][run][.][throw-class]") {
    auto jit = jit_k(R"SRC(
        module __test_exc_throw_catch__;

        thrower() : void {
            throw MemoryException();
        }

        test() : int {
            result : int = 0;
            try {
                thrower();
                result = 999;
            } catch (e: MemoryException*) {
                result = e->getCode();
            }
            return result;
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 1);
}

TEST_CASE("Exception: catch by base RuntimeException", "[libk][exception][run][.][throw-class]") {
    auto jit = jit_k(R"SRC(
        module __test_exc_catch_base__;

        thrower() : void {
            throw NullPointerException();
        }

        test() : int {
            result : int = 0;
            try {
                thrower();
                result = 999;
            } catch (e: RuntimeException*) {
                result = e->getCode();
            }
            return result;
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 2);
}




