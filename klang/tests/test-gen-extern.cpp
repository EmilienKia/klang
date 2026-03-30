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
 * Tests for K language FFI @k::ffi::Extern annotation.
 *
 * Tests covered:
 *  - 'extern' keyword no longer exists (now an identifier)
 *  - Bodyless @Extern("C") function compiles
 *  - @Extern("C") function with body is rejected
 *  - @Extern on non-static member method is rejected
 *  - Missing / empty / unsupported language parameter is rejected
 *  - Case-insensitive language matching ("c" and "C" both work)
 *  - 'library' parameter accepted with warning
 *  - Explicit 'symbol' override via positional and designated forms
 *  - @Extern + abstract combination rejected
 *  - Static @Extern member function accepted
 *  - Runtime: @Extern function can call a C function resolved at link time
 *  - @Extern function used from within a struct method
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

// ── Common K source preamble: inline definition of the Extern annotation ──
// Tests use gen_jit() (no stdlib KDI import resolution), so we define a local
// annotation type whose FQ name matches k::ffi::Extern.  The symbol_resolver
// matches by FQ name of the resolved annotation type.
// We omit @Retention/@Target since the stdlib annotations namespace is not
// available in the test context — the compiler defaults work fine.
static const std::string FFI_PREAMBLE = R"K(
namespace ffi {
    annotation Extern {
        language : const char[];
        library : const char[]? = null;
        symbol : const char[]? = null;
    }
}
)K";

// Helper: build a complete K source with module k + FFI preamble + user body.
static std::string ffi_src(const std::string& body) {
    return "module k;\n" + FFI_PREAMBLE + body;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Lexer: 'extern' keyword no longer recognized
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("FFI: 'extern' is no longer a keyword", "[ffi][lexer]") {
    test_logger logger;
    k::lex::lexer lexer(logger);
    k::source src("extern");
    auto lexemes = lexer.parse(src);
    REQUIRE(lexemes.size() == 1);
    // 'extern' is now lexed as a plain identifier, not a keyword
    CHECK(std::holds_alternative<k::lex::identifier>(lexemes[0]));
}

// ═══════════════════════════════════════════════════════════════════════════
//  Basic: bodyless @Extern("C") function compiles
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("FFI: bodyless @Extern(\"C\") function compiles", "[ffi][gen]") {
    auto jit = gen_jit(ffi_src(R"K(
        @ffi::Extern("C") some_c_function(x : int) : int;
    )K"));
    REQUIRE(jit);
}

TEST_CASE("FFI: @Extern(\"C\") function with no return type compiles", "[ffi][gen]") {
    auto jit = gen_jit(ffi_src(R"K(
        @ffi::Extern("C") some_c_proc(x : int);
    )K"));
    REQUIRE(jit);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Error: @Extern function with body is rejected
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("FFI: @Extern function with body is rejected", "[ffi][error]") {
    REQUIRE_THROWS_AS(gen_jit_throws(ffi_src(R"K(
        @ffi::Extern("C") bad(x : int) : int { return x; }
    )K")), k::log::compiler_error);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Error: @Extern on non-static member method is rejected
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("FFI: @Extern on non-static member method is rejected", "[ffi][error]") {
    REQUIRE_THROWS_AS(gen_jit_throws(ffi_src(R"K(
        struct Foo {
            @ffi::Extern("C") bad() : int;
        }
    )K")), k::log::compiler_error);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Error: missing language parameter
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("FFI: missing language parameter is rejected", "[ffi][error]") {
    REQUIRE_THROWS_AS(gen_jit_throws(ffi_src(R"K(
        @ffi::Extern() bad(x : int) : int;
    )K")), k::log::compiler_error);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Error: empty language string
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("FFI: empty language string is rejected", "[ffi][error]") {
    REQUIRE_THROWS_AS(gen_jit_throws(ffi_src(R"K(
        @ffi::Extern("") bad(x : int) : int;
    )K")), k::log::compiler_error);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Error: unsupported language
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("FFI: unsupported language is rejected", "[ffi][error]") {
    REQUIRE_THROWS_AS(gen_jit_throws(ffi_src(R"K(
        @ffi::Extern("Java") bad(x : int) : int;
    )K")), k::log::compiler_error);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Case-insensitive language matching
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("FFI: lowercase 'c' language is accepted", "[ffi][gen]") {
    auto jit = gen_jit(ffi_src(R"K(
        @ffi::Extern("c") some_c_function(x : int) : int;
    )K"));
    REQUIRE(jit);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Warning: 'library' parameter is not yet used
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("FFI: library parameter compiles with warning", "[ffi][gen]") {
    // Should compile (warning only, not an error)
    auto jit = gen_jit(ffi_src(R"K(
        @ffi::Extern("C", "mylib.so") some_c_function(x : int) : int;
    )K"));
    REQUIRE(jit);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Static @Extern member function is accepted
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("FFI: static @Extern member function is accepted", "[ffi][gen]") {
    auto jit = gen_jit(ffi_src(R"K(
        struct Calculator {
            @ffi::Extern("C") static helper(a : int, b : int) : int;
        }
    )K"));
    REQUIRE(jit);
}

// ═══════════════════════════════════════════════════════════════════════════
//  @Extern + abstract combination rejected
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("FFI: @Extern + abstract combination rejected", "[ffi][error]") {
    REQUIRE_THROWS_AS(gen_jit_throws(ffi_src(R"K(
        abstract class Foo {
            @ffi::Extern("C") abstract bad() : int;
        }
    )K")), k::log::compiler_error);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Runtime: @Extern function resolves a C symbol and executes correctly
// ═══════════════════════════════════════════════════════════════════════════

// Plain C functions visible to the JIT linker (in the test executable).
extern "C" {
    int __k_test_extern_add(int a, int b) {
        return a + b;
    }
    int __k_test_extern_mul(int a, int b) {
        return a * b;
    }
    int __k_test_extern_custom_symbol(int x) {
        return x * 10;
    }
}

TEST_CASE("FFI: call to C function via @Extern declaration", "[ffi][gen][runtime]") {
    auto jit = gen_jit(ffi_src(R"K(
        @ffi::Extern("C") __k_test_extern_add(a : int, b : int) : int;
        test() : int {
            return __k_test_extern_add(20, 22);
        }
    )K"));
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

TEST_CASE("FFI: multiple @Extern calls in one expression", "[ffi][gen][runtime]") {
    auto jit = gen_jit(ffi_src(R"K(
        @ffi::Extern("C") __k_test_extern_add(a : int, b : int) : int;
        @ffi::Extern("C") __k_test_extern_mul(a : int, b : int) : int;
        test() : int {
            return __k_test_extern_add(__k_test_extern_mul(6, 7), 0);
        }
    )K"));
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

TEST_CASE("FFI: @Extern function used from within a struct method", "[ffi][gen][runtime]") {
    auto jit = gen_jit(ffi_src(R"K(
        @ffi::Extern("C") __k_test_extern_add(a : int, b : int) : int;
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
    )K"));
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Explicit symbol override via positional parameter
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("FFI: explicit symbol override via positional argument", "[ffi][gen][runtime]") {
    auto jit = gen_jit(ffi_src(R"K(
        @ffi::Extern("C", null, "__k_test_extern_custom_symbol") my_func(x : int) : int;
        test() : int {
            return my_func(4);
        }
    )K"));
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 40);
}

// ═══════════════════════════════════════════════════════════════════════════
//  Explicit symbol override via designated initializer
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("FFI: explicit symbol override via designated initializer", "[ffi][gen][runtime]") {
    auto jit = gen_jit(ffi_src(R"K(
        @ffi::Extern{.language="C", .symbol="__k_test_extern_custom_symbol"} my_func(x : int) : int;
        test() : int {
            return my_func(4);
        }
    )K"));
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 40);
}
