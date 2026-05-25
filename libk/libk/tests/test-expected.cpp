/*
 * K Language standard library — Expected<R,E> tests
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
 * Tests for ::k::Expected<R,E>.
 *
 * These tests exercise the static factory methods (expected(), unexpected(),
 * error()) and the copy constructor of Expected<R,E> by JIT-compiling small
 * K programs that use the stdlib type.
 *
 * The base standard library (module "k") is implicitly imported by the
 * compiler — no explicit "import k;" is needed in the K sources.
 */

#include <catch2/catch_all.hpp>
#include "helpers.hpp"

#ifndef LIBK_KDI_DIR
#error "LIBK_KDI_DIR must be defined — check CMakeLists.txt"
#endif
#ifndef LIBK_LIB_DIR
#error "LIBK_LIB_DIR must be defined — check CMakeLists.txt"
#endif

namespace {

/// Shorthand: compile K source that uses the base stdlib and JIT it.
std::unique_ptr<k::model::gen::jit> jit_k(std::string_view src) {
    return gen_jit_with_stdlib(src, LIBK_KDI_DIR, LIBK_LIB_DIR);
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════════
//  Static factory methods — direct call syntax
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Expected::expected() — static factory for result", "[libk][expected][factory]") {
    auto j = jit_k(R"SRC(
        module __expected_factory_result__;
        test() : int {
            e : Expected<int, int> = Expected<int, int>::expected(42);
            if (!e.hasResult()) return 0;
            if (e.getResult() != 42) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

TEST_CASE("Expected::unexpected() — static factory for error", "[libk][expected][factory]") {
    auto j = jit_k(R"SRC(
        module __expected_factory_unexpected__;
        test() : int {
            e : Expected<int, int> = Expected<int, int>::unexpected(-7);
            if (!e.hasError()) return 0;
            if (e.getError() != -7) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

TEST_CASE("Expected::error() — alias for unexpected()", "[libk][expected][factory]") {
    auto j = jit_k(R"SRC(
        module __expected_factory_error__;
        test() : int {
            e : Expected<int, int> = Expected<int, int>::error(-99);
            if (!e.hasError()) return 0;
            if (e.getError() != -99) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

TEST_CASE("k::Expected::expected() — namespace-qualified static factory", "[libk][expected][factory]") {
    auto j = jit_k(R"SRC(
        module __expected_factory_ns__;
        test() : int {
            e : k::Expected<int, int> = k::Expected<int, int>::expected(42);
            if (!e.hasResult()) return 0;
            if (e.getResult() != 42) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

TEST_CASE("::k::Expected::expected() — absolute-prefix static factory", "[libk][expected][factory]") {
    auto j = jit_k(R"SRC(
        module __expected_factory_abs__;
        test() : int {
            e : ::k::Expected<int, int> = ::k::Expected<int, int>::expected(42);
            if (!e.hasResult()) return 0;
            if (e.getResult() != 42) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Copy constructor — copy of a result-holding Expected
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Expected copy constructor — copy result", "[libk][expected][copy]") {
    auto j = jit_k(R"SRC(
        module __expected_copy_result__;
        test() : int {
            src : Expected<int, int> = Expected<int, int>::expected(42);
            dst : Expected<int, int>(src);
            if (!dst.hasResult()) return 0;
            if (dst.getResult() != 42) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Copy constructor — copy of an error-holding Expected
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Expected copy constructor — copy error", "[libk][expected][copy]") {
    auto j = jit_k(R"SRC(
        module __expected_copy_error__;
        test() : int {
            src : Expected<int, int> = Expected<int, int>::unexpected(-1);
            dst : Expected<int, int>(src);
            if (!dst.hasError()) return 0;
            if (dst.getError() != -1) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Copy constructor — copy does not share state with original (result)
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Expected copy constructor — modifying copy does not affect original (result)", "[libk][expected][copy]") {
    auto j = jit_k(R"SRC(
        module __expected_copy_independence_result__;
        test() : int {
            src : Expected<int, int> = Expected<int, int>::expected(10);
            dst : Expected<int, int>(src);
            // Overwrite dst with an error; src must keep its result
            dst.setError(99);
            result : int = 0;
            if (src.hasResult()) { result = result + 1; }
            if (src.getResult() == 10) { result = result + 10; }
            if (dst.hasError()) { result = result + 100; }
            if (dst.getError() == 99) { result = result + 1000; }
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1111);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Copy constructor — copy does not share state with original (error)
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Expected copy constructor — modifying copy does not affect original (error)", "[libk][expected][copy]") {
    auto j = jit_k(R"SRC(
        module __expected_copy_independence_error__;
        test() : int {
            src : Expected<int, int> = Expected<int, int>::error(-5);
            dst : Expected<int, int>(src);
            // Overwrite dst with a result; src must keep its error
            dst.setResult(77);
            result : int = 0;
            if (src.hasError()) { result = result + 1; }
            if (src.getError() == -5) { result = result + 10; }
            if (dst.hasResult()) { result = result + 100; }
            if (dst.getResult() == 77) { result = result + 1000; }
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1111);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Copy constructor — chained copies preserve state
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Expected copy constructor — chained copies preserve result", "[libk][expected][copy]") {
    auto j = jit_k(R"SRC(
        module __expected_copy_chain__;
        test() : int {
            a : Expected<int, int> = ::k::Expected<int, int>::expected(7);
            b : Expected<int, int>(a);
            c : Expected<int, int>(b);
            result : int = 0;
            if (a.hasResult() && a.getResult() == 7) { result = result + 1; }
            if (b.hasResult() && b.getResult() == 7) { result = result + 10; }
            if (c.hasResult() && c.getResult() == 7) { result = result + 100; }
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 111);
}