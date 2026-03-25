/*
 * K Language compiler
 *
 * Copyright 2026 Emilien Kia
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
 * Tests for K language 'extern' function declarations.
 *
 * Tests covered:
 *  - Parser: 'extern' keyword recognized as a specifier
 *  - Bodyless extern function parses and compiles
 *  - Extern function with body is rejected
 *  - Extern + abstract combination is rejected
 *  - Runtime: extern function can call a C function resolved at link time
 *  - Extern function inside a struct (static extern method)
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

// ═══════════════════════════════════════════════════════════════════════════
//  Parser: 'extern' keyword recognized
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Extern: keyword is lexed", "[extern][lexer]") {
    test_logger logger;
    k::lex::lexer lexer(logger);
    k::source src("extern");
    auto lexemes = lexer.parse(src);
    REQUIRE(lexemes.size() == 1);
    REQUIRE(std::holds_alternative<k::lex::keyword>(lexemes[0]));
    CHECK(std::get<k::lex::keyword>(lexemes[0]).type == k::lex::keyword::EXTERN);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Parser + Model: extern function declaration (no body) compiles
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Extern: bodyless extern function compiles", "[extern][gen]") {
    auto jit = gen_jit(R"SRC(
        module __extern_basic__;
        extern some_c_function(x : int) : int;
    )SRC");
    REQUIRE(jit);
}

TEST_CASE("Extern: extern function with no return type compiles", "[extern][gen]") {
    auto jit = gen_jit(R"SRC(
        module __extern_void__;
        extern some_c_proc(x : int);
    )SRC");
    REQUIRE(jit);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Error: extern function with body is rejected
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Extern: extern function with body is rejected", "[extern][error]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __extern_body__;
        extern bad(x : int) : int { return x; }
    )SRC"), k::log::compiler_error);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Error: extern + abstract combination rejected
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Extern: extern + abstract combination rejected", "[extern][error]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __extern_abstract__;
        abstract class Foo {
            extern abstract bad() : int;
        }
    )SRC"), k::log::compiler_error);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Runtime: extern function resolves a C symbol and executes correctly
// ═══════════════════════════════════════════════════════════════════════════

// A plain C function that will be visible to the JIT linker because it
// lives in the test executable (loaded into the process address space).
extern "C" {
    int __k_test_extern_add(int a, int b) {
        return a + b;
    }
    int __k_test_extern_mul(int a, int b) {
        return a * b;
    }
}

TEST_CASE("Extern: call to C function via extern declaration", "[extern][gen][runtime]") {
    auto jit = gen_jit(R"SRC(
        module __extern_call__;
        extern __k_test_extern_add(a : int, b : int) : int;
        test() : int {
            return __k_test_extern_add(20, 22);
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

TEST_CASE("Extern: multiple extern calls in one expression", "[extern][gen][runtime]") {
    auto jit = gen_jit(R"SRC(
        module __extern_multi__;
        extern __k_test_extern_add(a : int, b : int) : int;
        extern __k_test_extern_mul(a : int, b : int) : int;
        test() : int {
            return __k_test_extern_add(__k_test_extern_mul(6, 7), 0);
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

TEST_CASE("Extern: extern function used from within a struct method", "[extern][gen][runtime]") {
    auto jit = gen_jit(R"SRC(
        module __extern_struct__;
        extern __k_test_extern_add(a : int, b : int) : int;
        struct Calculator {
            base : int;
            Calculator(b : int) : base(b) {}
            add(x : int) : int {
                return __k_test_extern_add(base, x);
            }
        }
        test() : int {
            c : Calculator(40);
            return c.add(2);
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}





