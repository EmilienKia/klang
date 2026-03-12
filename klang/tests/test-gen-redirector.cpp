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
 * Tests for K language function redirectors (-> target;).
 *
 * Step 2: model-level representation
 * Step 3+: resolution, type checking, codegen, vtable interaction
 */

#include <catch2/catch_all.hpp>

#include "../src/common/logger.hpp"
#include "../src/parse/parser.hpp"
#include "../src/model/model.hpp"
#include "../src/model/model_builder.hpp"
#include "../src/gen/generators.hpp"
#include "../src/gen/resolvers.hpp"
#include "../src/compiler.hpp"

#include "helpers.hpp"

using namespace k::model;
namespace ast = k::parse::ast;

// ─────────────────────────────────────────────────────────────────────────────
// Model building tests — verify that AST redirect is correctly propagated
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Model — free function redirect has correct flags", "[model][redirect]") {
    test_logger log;
    auto src = R"SRC(
        module test;
        bar() : int { return 42; }
        foo() : int -> bar;
    )SRC";

    k::parse::parser parser(log);
    k::source source(src);
    parser.parse(source);
    auto ast_unit = parser.parse_unit();
    REQUIRE(ast_unit);

    auto ctx = context::create();
    auto unit = k::model::unit::create(ctx);
    model_builder::visit(log, ctx, *ast_unit, *unit);

    auto root_ns = unit->get_root_namespace();
    REQUIRE(root_ns);

    // Find the 'foo' function
    auto foo = root_ns->get_function("foo");
    REQUIRE(foo);
    CHECK(foo->is_redirected());
    CHECK(foo->get_aliasing() == function::function_aliasing::REDIRECT);
    CHECK(foo->get_redirect_target_name().size() == 1);
    CHECK(foo->get_redirect_target_name()[0] == "bar");
    // Redirect functions have no explicit body (but get_block() lazily creates one)
    CHECK(foo->is_redirected());
}

TEST_CASE("Model — member function redirect has correct flags", "[model][redirect]") {
    test_logger log;
    auto src = R"SRC(
        module test;
        struct Foo {
            bar() : int { return 42; }
            baz() : int -> bar;
        }
    )SRC";

    k::parse::parser parser(log);
    k::source source(src);
    parser.parse(source);
    auto ast_unit = parser.parse_unit();
    REQUIRE(ast_unit);

    auto ctx = context::create();
    auto unit = k::model::unit::create(ctx);
    model_builder::visit(log, ctx, *ast_unit, *unit);

    auto root_ns = unit->get_root_namespace();
    REQUIRE(root_ns);

    auto foo_st = root_ns->get_structure("Foo");
    REQUIRE(foo_st);

    auto baz = foo_st->get_function("baz");
    REQUIRE(baz);
    CHECK(baz->is_redirected());
    CHECK(baz->get_redirect_target_name()[0] == "bar");
}

TEST_CASE("Model — redirect with qualified target name", "[model][redirect]") {
    test_logger log;
    auto src = R"SRC(
        module test;
        struct Base {
            method() : int { return 10; }
        }
        struct Derived : public Base {
            method() : int -> Base::method;
        }
    )SRC";

    k::parse::parser parser(log);
    k::source source(src);
    parser.parse(source);
    auto ast_unit = parser.parse_unit();
    REQUIRE(ast_unit);

    auto ctx = context::create();
    auto unit = k::model::unit::create(ctx);
    model_builder::visit(log, ctx, *ast_unit, *unit);

    auto root_ns = unit->get_root_namespace();
    auto derived = root_ns->get_structure("Derived");
    REQUIRE(derived);

    auto method = derived->get_function("method");
    REQUIRE(method);
    CHECK(method->is_redirected());
    CHECK(method->get_redirect_target_name().size() == 2);
    CHECK(method->get_redirect_target_name()[0] == "Base");
    CHECK(method->get_redirect_target_name()[1] == "method");
}

TEST_CASE("Model — regular function is not redirected", "[model][redirect]") {
    test_logger log;
    auto src = R"SRC(
        module test;
        bar() : int { return 42; }
    )SRC";

    k::parse::parser parser(log);
    k::source source(src);
    parser.parse(source);
    auto ast_unit = parser.parse_unit();
    REQUIRE(ast_unit);

    auto ctx = context::create();
    auto unit = k::model::unit::create(ctx);
    model_builder::visit(log, ctx, *ast_unit, *unit);

    auto root_ns = unit->get_root_namespace();
    auto bar = root_ns->get_function("bar");
    REQUIRE(bar);
    CHECK_FALSE(bar->is_redirected());
    CHECK(bar->get_aliasing() == function::function_aliasing::NONE);
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 3 — Resolution tests (full pipeline via gen_jit)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Redirect — free function to free function", "[redirect][codegen]") {
    auto result = build_and_exec(R"SRC(
        module test;
        bar() : int { return 42; }
        foo() : int -> bar;
        main() : int {
            return foo();
        }
    )SRC");
    CHECK(result.exit_code == 42);
}

TEST_CASE("Redirect — free function with params", "[redirect][codegen]") {
    auto result = build_and_exec(R"SRC(
        module test;
        add(a: int, b: int) : int { return a + b; }
        sum(a: int, b: int) : int -> add;
        main() : int {
            return sum(3, 7);
        }
    )SRC");
    CHECK(result.exit_code == 10);
}

TEST_CASE("Redirect — member function in struct", "[redirect][codegen]") {
    auto result = build_and_exec(R"SRC(
        module test;
        struct Foo {
            bar() : int { return 99; }
            baz() : int -> bar;
        }
        main() : int {
            f : Foo;
            return f.baz();
        }
    )SRC");
    CHECK(result.exit_code == 99);
}

TEST_CASE("Redirect — member to parent struct method", "[redirect][codegen]") {
    auto result = build_and_exec(R"SRC(
        module test;
        struct Base {
            value() : int { return 7; }
        }
        struct Derived : public Base {
            value() : int -> Base::value;
        }
        main() : int {
            d : Derived;
            return d.value();
        }
    )SRC");
    CHECK(result.exit_code == 7);
}

TEST_CASE("Redirect — chained redirects", "[redirect][codegen]") {
    auto result = build_and_exec(R"SRC(
        module test;
        impl() : int { return 55; }
        mid() : int -> impl;
        top() : int -> mid;
        main() : int {
            return top();
        }
    )SRC");
    CHECK(result.exit_code == 55);
}

TEST_CASE("Redirect — error: unknown target", "[redirect][error]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module test;
        foo() : int -> nonexistent;
    )SRC"));
}

TEST_CASE("Redirect — error: circular redirect", "[redirect][error]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module test;
        foo() : int -> bar;
        bar() : int -> foo;
    )SRC"));
}

TEST_CASE("Redirect — error: target is abstract", "[redirect][error]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module test;
        abstract class Base {
            abstract speak() : int;
        }
        class Derived : public Base {
            speak() : int -> Base::speak;
        }
    )SRC"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Step 6 — Virtual dispatch with redirects
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Redirect — virtual method redirect to parent", "[redirect][codegen][virtual]") {
    auto result = build_and_exec(R"SRC(
        module test;
        class Base {
            speak() : int { return 10; }
        }
        class Derived : public Base {
            speak() : int -> Base::speak;
        }
        dispatch(b: Base&) : int {
            return b.speak();
        }
        main() : int {
            d : Derived;
            return dispatch(d);
        }
    )SRC");
    CHECK(result.exit_code == 10);
}

TEST_CASE("Redirect — override a redirected virtual method", "[redirect][codegen][virtual]") {
    auto result = build_and_exec(R"SRC(
        module test;
        class Base {
            speak() : int { return 10; }
        }
        class Mid : public Base {
            speak() : int -> Base::speak;
        }
        class Final : public Mid {
            speak() : int { return 99; }
        }
        dispatch(b: Base&) : int {
            return b.speak();
        }
        main() : int {
            f : Final;
            return dispatch(f);
        }
    )SRC");
    CHECK(result.exit_code == 99);
}

TEST_CASE("Redirect — redirect with return type void", "[redirect][codegen]") {
    auto result = build_and_exec(R"SRC(
        module test;
        g : int = 0;
        impl() { g = 77; }
        alias() -> impl;
        main() : int {
            alias();
            return g;
        }
    )SRC");
    CHECK(result.exit_code == 77);
}

// ─────────────────────────────────────────────────────────────────────────────
// Forward reference and ordering
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Redirect — forward reference (target defined after redirector)", "[redirect][codegen]") {
    auto result = build_and_exec(R"SRC(
        module test;
        alias() : int -> impl;
        impl() : int { return 33; }
        main() : int {
            return alias();
        }
    )SRC");
    CHECK(result.exit_code == 33);
}

// ─────────────────────────────────────────────────────────────────────────────
// Static member redirect to free function (cross-kind)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Redirect — static member to free function", "[redirect][codegen]") {
    auto result = build_and_exec(R"SRC(
        module test;
        helper(a: int) : int { return a * 3; }
        struct Foo {
            static compute(a: int) : int -> helper;
        }
        main() : int {
            return Foo::compute(5);
        }
    )SRC");
    CHECK(result.exit_code == 15);
}

// ─────────────────────────────────────────────────────────────────────────────
// Triple chained redirect
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Redirect — triple chain a -> b -> c -> impl", "[redirect][codegen]") {
    auto result = build_and_exec(R"SRC(
        module test;
        impl() : int { return 88; }
        c() : int -> impl;
        b() : int -> c;
        a() : int -> b;
        main() : int {
            return a();
        }
    )SRC");
    CHECK(result.exit_code == 88);
}

// ─────────────────────────────────────────────────────────────────────────────
// ELF symbol presence via shared library
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Redirect — alias symbol present in shared library", "[redirect][codegen][lib]") {
    auto so_path = build_shared_library(R"SRC(
        module test;
        bar() : int { return 42; }
        foo() : int -> bar;
    )SRC");

    auto nm_res = k::tools::lookup_run_process("nm", {"--dynamic", "--defined-only", so_path});
    REQUIRE(nm_res.exit_code == 0);

    // Both the target and the alias should appear in the symbol table
    CHECK(nm_res.out.find("bar") != std::string::npos);
    CHECK(nm_res.out.find("foo") != std::string::npos);

    std::remove(so_path.c_str());
}

