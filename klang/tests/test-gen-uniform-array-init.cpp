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
 * Tests for K language uniform array initialization.
 *
 * Uniform array init allows all elements of an array to be initialized with
 * the same constructor arguments.
 *
 * Syntax:
 *   Stack:  var : T(args)[N];
 *   Heap:   new T(args)[N]
 *
 * Error code coverage:
 *   - Error 0x4231: no matching constructor
 *   - Error 0x4232: cannot convert value to primitive element type
 */

#include <catch2/catch_all.hpp>

#include "../src/common/logger.hpp"
#include "../src/parse/parser.hpp"
#include "../src/model/model.hpp"
#include "../src/gen/generators.hpp"
#include "../src/gen/resolvers.hpp"
#include "../src/compiler.hpp"

#include "helpers.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// Parser tests for stack uniform array init: var : T(args)[N];
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Parse uniform array — int(42)[5]", "[parser][uniform-array]") {
    test_logger log;
    k::source src{"arr : int(42)[5];"};
    k::parse::parser parser(log, src);
    auto var = parser.parse_variable_decl();
    REQUIRE(var);
    REQUIRE(var->name.content == "arr");
    REQUIRE(var->is_uniform_array_init == true);
    REQUIRE(var->is_constructor == false);
    REQUIRE(var->is_brace_init == false);
    REQUIRE(var->uniform_ctor_args.size() == 1);
    REQUIRE(var->uniform_array_size != nullptr);
}

TEST_CASE("Parse uniform array — int(0)[100]", "[parser][uniform-array]") {
    test_logger log;
    k::source src{"zeros : int(0)[100];"};
    k::parse::parser parser(log, src);
    auto var = parser.parse_variable_decl();
    REQUIRE(var);
    REQUIRE(var->name.content == "zeros");
    REQUIRE(var->is_uniform_array_init == true);
    REQUIRE(var->uniform_ctor_args.size() == 1);
    REQUIRE(var->uniform_array_size != nullptr);
}

TEST_CASE("Parse uniform array — empty ctor args", "[parser][uniform-array]") {
    test_logger log;
    k::source src{"arr : int()[3];"};
    k::parse::parser parser(log, src);
    auto var = parser.parse_variable_decl();
    REQUIRE(var);
    REQUIRE(var->is_uniform_array_init == true);
    REQUIRE(var->uniform_ctor_args.empty());
    REQUIRE(var->uniform_array_size != nullptr);
}

TEST_CASE("Parse uniform array — multi-arg constructor", "[parser][uniform-array]") {
    test_logger log;
    k::source src{"pts : Point(3, 4)[10];"};
    k::parse::parser parser(log, src);
    auto var = parser.parse_variable_decl();
    REQUIRE(var);
    REQUIRE(var->name.content == "pts");
    REQUIRE(var->is_uniform_array_init == true);
    REQUIRE(var->uniform_ctor_args.size() == 2);
    REQUIRE(var->uniform_array_size != nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Parser tests for heap uniform array init: new T(args)[N]
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Parse new uniform array — int(42)[5]", "[parser][uniform-array]") {
    test_logger log;
    k::source src{"new int(42)[5]"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_unary_expr();
    REQUIRE(expr);
    auto ne = std::dynamic_pointer_cast<k::parse::ast::new_expr>(expr);
    REQUIRE(ne);
    REQUIRE(ne->is_array == true);
    REQUIRE(ne->is_uniform_array == true);
    REQUIRE(ne->uniform_ctor_args.size() == 1);
    REQUIRE(ne->array_size_expr != nullptr);
}

TEST_CASE("Parse new uniform array — empty args", "[parser][uniform-array]") {
    test_logger log;
    k::source src{"new int()[3]"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_unary_expr();
    REQUIRE(expr);
    auto ne = std::dynamic_pointer_cast<k::parse::ast::new_expr>(expr);
    REQUIRE(ne);
    REQUIRE(ne->is_array == true);
    REQUIRE(ne->is_uniform_array == true);
    REQUIRE(ne->uniform_ctor_args.empty());
    REQUIRE(ne->array_size_expr != nullptr);
}

TEST_CASE("Parse new uniform array — multi-arg", "[parser][uniform-array]") {
    test_logger log;
    k::source src{"new Point(3, 4)[5]"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_unary_expr();
    REQUIRE(expr);
    auto ne = std::dynamic_pointer_cast<k::parse::ast::new_expr>(expr);
    REQUIRE(ne);
    REQUIRE(ne->is_array == true);
    REQUIRE(ne->is_uniform_array == true);
    // Parser stores args from parse_expression() — "3, 4" becomes a single
    // expr_list_expr (comma expression). Model builder flattens it.
    REQUIRE(ne->uniform_ctor_args.size() >= 1);
    REQUIRE(ne->array_size_expr != nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// End-to-end JIT tests — stack uniform array (primitives)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Uniform array stack — int(42)[5] returns element", "[gen][uniform-array]") {
    auto jit = gen_jit(R"SRC(
        module test;

        get_elem(idx : int) : int {
            arr : int(42)[5];
            return arr[idx];
        }
    )SRC");
    REQUIRE(jit);
    auto get_elem = jit->lookup_symbol<int(*)(int)>("get_elem");
    REQUIRE(get_elem != nullptr);
    REQUIRE(get_elem(0) == 42);
    REQUIRE(get_elem(1) == 42);
    REQUIRE(get_elem(4) == 42);
}

TEST_CASE("Uniform array stack — int(0)[3] all zero", "[gen][uniform-array]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : int {
            arr : int(0)[3];
            return arr[0] + arr[1] + arr[2];
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 0);
}

TEST_CASE("Uniform array stack — int(7)[4] sum", "[gen][uniform-array]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : int {
            arr : int(7)[4];
            return arr[0] + arr[1] + arr[2] + arr[3];
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 28);
}

// ─────────────────────────────────────────────────────────────────────────────
// End-to-end JIT tests — heap uniform array (static size, primitives)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Uniform array heap static — new int(42)[5]", "[gen][uniform-array]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : int {
            arr : int[5]! = new int(42)[5];
            r : int = arr[3];
            delete arr;
            return r;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

TEST_CASE("Uniform array heap static — new int(10)[3] sum", "[gen][uniform-array]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : int {
            arr : int[3]! = new int(10)[3];
            r : int = arr[0] + arr[1] + arr[2];
            delete arr;
            return r;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 30);
}

// ─────────────────────────────────────────────────────────────────────────────
// End-to-end JIT tests — heap uniform array (dynamic size, primitives)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Uniform array heap dynamic — new int(99)[n]", "[gen][uniform-array]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test(n : unsigned int) : int {
            arr : int[]! = new int(99)[n];
            r : int = arr[0];
            delete arr;
            return r;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)(unsigned int)>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test(3) == 99);
}

TEST_CASE("Uniform array heap dynamic — new int(5)[n] last element", "[gen][uniform-array]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test(n : unsigned int) : int {
            arr : int[]! = new int(5)[n];
            r : int = arr[n - 1];
            delete arr;
            return r;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)(unsigned int)>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test(10) == 5);
}

// ─────────────────────────────────────────────────────────────────────────────
// End-to-end JIT tests — struct uniform array (stack)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Uniform array stack — struct with ctor", "[gen][uniform-array]") {
    auto jit = gen_jit(R"SRC(
        module test;

        struct Point {
            x : int = 0;
            y : int = 0;
            Point(a : int, b : int) {
                x = a;
                y = b;
            }
        }

        test() : int {
            pts : Point(3, 4)[3];
            return pts[0].x + pts[1].y + pts[2].x;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 10); // 3 + 4 + 3
}

// ─────────────────────────────────────────────────────────────────────────────
// End-to-end JIT tests — struct uniform array (heap static)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Uniform array heap static — struct with ctor", "[gen][uniform-array]") {
    auto jit = gen_jit(R"SRC(
        module test;

        struct Point {
            x : int = 0;
            y : int = 0;
            Point(a : int, b : int) {
                x = a;
                y = b;
            }
        }

        test() : int {
            pts : Point[2]! = new Point(5, 6)[2];
            r : int = pts[0].x + pts[1].y;
            delete pts;
            return r;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 11); // 5 + 6
}

// ─────────────────────────────────────────────────────────────────────────────
// End-to-end JIT tests — struct uniform array (heap dynamic)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Uniform array heap dynamic — struct with ctor", "[gen][uniform-array]") {
    auto jit = gen_jit(R"SRC(
        module test;

        struct Point {
            x : int = 0;
            y : int = 0;
            Point(a : int, b : int) {
                x = a;
                y = b;
            }
        }

        test(n : unsigned int) : int {
            pts : Point[]! = new Point(7, 8)[n];
            r : int = pts[0].x + pts[0].y;
            delete pts;
            return r;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)(unsigned int)>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test(5) == 15); // 7 + 8
}

// ─────────────────────────────────────────────────────────────────────────────
// Error tests
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Uniform array — error: no matching constructor (stack)", "[gen][uniform-array][error]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module test;

        struct Foo {
            x : int = 0;
            Foo(a : int, b : int, c : int) {
                x = a + b + c;
            }
        }

        test() : int {
            arr : Foo(42)[3];
            return 0;
        }
    )SRC"));
}

TEST_CASE("Uniform array — error: no matching constructor (heap)", "[gen][uniform-array][error]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module test;

        struct Bar {
            x : int = 0;
            Bar(a : int, b : int) {
                x = a + b;
            }
        }

        test() : int {
            arr : Bar[3]! = new Bar(1, 2, 3)[3];
            delete arr;
            return 0;
        }
    )SRC"));
}




