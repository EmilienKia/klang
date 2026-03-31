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

// ── Common K source preamble: inline definition of the FFI annotations ────
// Tests use gen_jit() (no stdlib KDI import resolution), so we define local
// annotation types whose FQ names match k::ffi::Extern and k::ffi::CString.
// The symbol_resolver matches by FQ name of the resolved annotation type.
// We omit @Retention/@Target since the stdlib annotations namespace is not
// available in the test context — the compiler defaults work fine.
static const std::string FFI_PREAMBLE = R"K(
namespace ffi {
    annotation Extern {
        language : const char[];
        library : const char[]? = null;
        symbol : const char[]? = null;
    }
    annotation CString {
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

// ═══════════════════════════════════════════════════════════════════════════
//  @ffi::CString — error cases
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("FFI CString: @CString on non-Extern function is rejected", "[ffi][cstring][error]") {
    REQUIRE_THROWS_AS(gen_jit_throws(ffi_src(R"K(
        not_extern(@ffi::CString s : char&) : int { return 0; }
    )K")), k::log::compiler_error);
}

TEST_CASE("FFI CString: @CString on non-addresser type is rejected", "[ffi][cstring][error]") {
    REQUIRE_THROWS_AS(gen_jit_throws(ffi_src(R"K(
        @ffi::Extern("C") bad(@ffi::CString s : char) : int;
    )K")), k::log::compiler_error);
}

TEST_CASE("FFI CString: @CString on non-char addresser is rejected (int&)", "[ffi][cstring][error]") {
    REQUIRE_THROWS_AS(gen_jit_throws(ffi_src(R"K(
        @ffi::Extern("C") bad(@ffi::CString s : int&) : int;
    )K")), k::log::compiler_error);
}

TEST_CASE("FFI CString: @CString on non-char addresser is rejected (short*)", "[ffi][cstring][error]") {
    REQUIRE_THROWS_AS(gen_jit_throws(ffi_src(R"K(
        @ffi::Extern("C") bad(@ffi::CString s : short*) : int;
    )K")), k::log::compiler_error);
}

// ═══════════════════════════════════════════════════════════════════════════
//  @ffi::CString — valid addresser combinations
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("FFI CString: char pointer compiles", "[ffi][cstring][gen]") {
    auto jit = gen_jit(ffi_src(R"K(
        @ffi::Extern("C") ok(@ffi::CString s : char*) : int;
    )K"));
    REQUIRE(jit);
}

TEST_CASE("FFI CString: char reference compiles", "[ffi][cstring][gen]") {
    auto jit = gen_jit(ffi_src(R"K(
        @ffi::Extern("C") ok(@ffi::CString s : char&) : int;
    )K"));
    REQUIRE(jit);
}

TEST_CASE("FFI CString: const char reference compiles", "[ffi][cstring][gen]") {
    auto jit = gen_jit(ffi_src(R"K(
        @ffi::Extern("C") ok(@ffi::CString s : const char&) : int;
    )K"));
    REQUIRE(jit);
}

TEST_CASE("FFI CString: const char pointer compiles", "[ffi][cstring][gen]") {
    auto jit = gen_jit(ffi_src(R"K(
        @ffi::Extern("C") ok(@ffi::CString s : const char*) : int;
    )K"));
    REQUIRE(jit);
}

TEST_CASE("FFI CString: char view compiles", "[ffi][cstring][gen]") {
    auto jit = gen_jit(ffi_src(R"K(
        @ffi::Extern("C") ok(@ffi::CString s : char?) : int;
    )K"));
    REQUIRE(jit);
}

TEST_CASE("FFI CString: char link compiles", "[ffi][cstring][gen]") {
    auto jit = gen_jit(ffi_src(R"K(
        @ffi::Extern("C") ok(@ffi::CString s : char+) : int;
    )K"));
    REQUIRE(jit);
}

TEST_CASE("FFI CString: char owner compiles", "[ffi][cstring][gen]") {
    auto jit = gen_jit(ffi_src(R"K(
        @ffi::Extern("C") ok(@ffi::CString s : char!) : int;
    )K"));
    REQUIRE(jit);
}

// ═══════════════════════════════════════════════════════════════════════════
//  @ffi::CString — warning cases (compile OK, but emit diagnostic)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("FFI CString: unsigned char reference compiles with warning", "[ffi][cstring][gen]") {
    // byte == unsigned char — should compile but warn
    auto jit = gen_jit(ffi_src(R"K(
        @ffi::Extern("C") ok(@ffi::CString s : byte&) : int;
    )K"));
    REQUIRE(jit);
}

TEST_CASE("FFI CString: char drain compiles with warning", "[ffi][cstring][gen]") {
    // drain is not meaningful for C FFI — should compile but warn
    auto jit = gen_jit(ffi_src(R"K(
        @ffi::Extern("C") ok(@ffi::CString s : char#) : int;
    )K"));
    REQUIRE(jit);
}

// ═══════════════════════════════════════════════════════════════════════════
//  @ffi::CString — model flag
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("FFI CString: parameter model flag is_ffi_cstring()", "[ffi][cstring][model]") {
    auto comp = compile_model(ffi_src(R"K(
        @ffi::Extern("C") my_strlen(@ffi::CString s : const char*) : int;
    )K"));
    REQUIRE(comp != nullptr);

    // Find the function via the root namespace
    auto root = comp->get_unit()->get_root_namespace();
    REQUIRE(root != nullptr);
    auto fn = root->get_function("my_strlen");
    REQUIRE(fn != nullptr);
    REQUIRE(fn->parameters().size() == 1);
    CHECK(fn->parameters()[0]->is_ffi_cstring());
}

TEST_CASE("FFI CString: non-CString parameter is_ffi_cstring() is false", "[ffi][cstring][model]") {
    auto comp = compile_model(ffi_src(R"K(
        @ffi::Extern("C") my_add(a : int, b : int) : int;
    )K"));
    REQUIRE(comp != nullptr);

    auto root = comp->get_unit()->get_root_namespace();
    REQUIRE(root != nullptr);
    auto fn = root->get_function("my_add");
    REQUIRE(fn != nullptr);
    REQUIRE(fn->parameters().size() == 2);
    CHECK_FALSE(fn->parameters()[0]->is_ffi_cstring());
    CHECK_FALSE(fn->parameters()[1]->is_ffi_cstring());
}

// ═══════════════════════════════════════════════════════════════════════════
//  @ffi::CString — runtime: calling a C function with char* parameter
// ═══════════════════════════════════════════════════════════════════════════

// C functions visible to the JIT linker (in the test executable).
extern "C" {
    int __k_test_cstring_len(const char* s) {
        int len = 0;
        while (s[len]) ++len;
        return len;
    }
    int __k_test_cstring_first_char(const char* s) {
        return s ? static_cast<int>(s[0]) : -1;
    }
    int __k_test_cstring_and_int(const char* s, int n) {
        int len = 0;
        while (s[len]) ++len;
        return len + n;
    }
}

TEST_CASE("FFI CString: runtime — pass char pointer to C strlen", "[ffi][cstring][runtime]") {
    auto jit = gen_jit(ffi_src(R"K(
        @ffi::Extern("C") __k_test_cstring_len(@ffi::CString s : const char*) : int;
        call_len(s : const char[]) : int {
            p : const char* = &s[0];
            return __k_test_cstring_len(p);
        }
        test() : int {
            return call_len("hello");
        }
    )K"));
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 5);
}

TEST_CASE("FFI CString: runtime — @CString alongside normal parameter", "[ffi][cstring][runtime]") {
    auto jit = gen_jit(ffi_src(R"K(
        @ffi::Extern("C") __k_test_cstring_and_int(@ffi::CString s : const char*, n : int) : int;
        call_and_int(s : const char[], n : int) : int {
            p : const char* = &s[0];
            return __k_test_cstring_and_int(p, n);
        }
        test() : int {
            return call_and_int("hi", 100);
        }
    )K"));
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 102);
}

TEST_CASE("FFI CString: runtime — mixed CString and non-CString params", "[ffi][cstring][runtime]") {
    auto jit = gen_jit(ffi_src(R"K(
        @ffi::Extern("C") __k_test_cstring_len(@ffi::CString s : const char*) : int;
        @ffi::Extern("C") __k_test_extern_add(a : int, b : int) : int;
        call_len(s : const char[]) : int {
            p : const char* = &s[0];
            return __k_test_cstring_len(p);
        }
        test() : int {
            return __k_test_extern_add(call_len("abc"), 39);
        }
    )K"));
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

// ═══════════════════════════════════════════════════════════════════════════
//  @ffi::CString — additional edge-case tests
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("FFI CString: multiple @CString parameters", "[ffi][cstring][model]") {
    auto comp = compile_model(ffi_src(R"K(
        @ffi::Extern("C") cmp(@ffi::CString a : const char*, @ffi::CString b : const char*) : int;
    )K"));
    REQUIRE(comp != nullptr);

    auto root = comp->get_unit()->get_root_namespace();
    REQUIRE(root != nullptr);
    auto fn = root->get_function("cmp");
    REQUIRE(fn != nullptr);
    REQUIRE(fn->parameters().size() == 2);
    CHECK(fn->parameters()[0]->is_ffi_cstring());
    CHECK(fn->parameters()[1]->is_ffi_cstring());
}

TEST_CASE("FFI CString: @CString on second parameter only", "[ffi][cstring][model]") {
    auto comp = compile_model(ffi_src(R"K(
        @ffi::Extern("C") write(fd : int, @ffi::CString buf : const char*) : int;
    )K"));
    REQUIRE(comp != nullptr);

    auto root = comp->get_unit()->get_root_namespace();
    REQUIRE(root != nullptr);
    auto fn = root->get_function("write");
    REQUIRE(fn != nullptr);
    REQUIRE(fn->parameters().size() == 2);
    CHECK_FALSE(fn->parameters()[0]->is_ffi_cstring());
    CHECK(fn->parameters()[1]->is_ffi_cstring());
}

TEST_CASE("FFI CString: const byte pointer compiles with warning", "[ffi][cstring][gen]") {
    auto jit = gen_jit(ffi_src(R"K(
        @ffi::Extern("C") ok(@ffi::CString s : const byte*) : int;
    )K"));
    REQUIRE(jit);
}

TEST_CASE("FFI CString: static member extern with @CString", "[ffi][cstring][gen]") {
    auto jit = gen_jit(ffi_src(R"K(
        class Util {
            public Util() {}
            @ffi::Extern("C") static __k_test_cstring_len(@ffi::CString s : const char*) : int;
        }
        call_len(s : const char[]) : int {
            p : const char* = &s[0];
            return Util::__k_test_cstring_len(p);
        }
        test() : int {
            return call_len("abcde");
        }
    )K"));
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 5);
}

TEST_CASE("FFI CString: runtime — pass char reference to C function", "[ffi][cstring][runtime]") {
    auto jit = gen_jit(ffi_src(R"K(
        @ffi::Extern("C") __k_test_cstring_first_char(@ffi::CString s : const char&) : int;
        call_first(s : const char[]) : int {
            return __k_test_cstring_first_char(s[0]);
        }
        test() : int {
            return call_first("A");
        }
    )K"));
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 65);  // ASCII 'A'
}

TEST_CASE("FFI CString: runtime — multiple @CString params called", "[ffi][cstring][runtime]") {
    auto jit = gen_jit(ffi_src(R"K(
        @ffi::Extern("C") __k_test_cstring_len(@ffi::CString s : const char*) : int;
        @ffi::Extern("C") __k_test_cstring_and_int(@ffi::CString s : const char*, n : int) : int;
        get_len(s : const char[]) : int {
            p : const char* = &s[0];
            return __k_test_cstring_len(p);
        }
        call_and_int(s : const char[], n : int) : int {
            p : const char* = &s[0];
            return __k_test_cstring_and_int(p, n);
        }
        test() : int {
            la : int = get_len("ab");
            return call_and_int("xyz", la);
        }
    )K"));
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 5);  // len("xyz")=3 + len("ab")=2 = 5
}
