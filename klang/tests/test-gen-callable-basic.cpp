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
 * Basic tests of the first-class callable type (phase B.1-B.3).
 *
 * A callable is the fat reference `{ ptr fn, ptr ctx }`. These tests only exercise
 * context-free targets (free functions), whose `ctx` field is null; the call-site
 * lowering nevertheless branches on `ctx`, so they also validate the dispatch shape.
 *
 * Covered here:
 *  - Callable type syntax with an explicit return type: `*(int):int`, `?(int):int`, `+(int):int`
 *  - Binding a free function to a callable variable, a global, a parameter, a return value
 *  - Calling through a callable
 *  - Rebinding a `*` callable
 *  - Overload disambiguation from the callable parameter list
 *  - Void return (omitted `: TypeSpec`) — K has no `void` keyword
 *  - `f == null` / `f != null` and boolean conversion
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

TEST_CASE("Callable: local variable of pointer callable type", "[gen][callable]")
{
    auto jit = gen_jit(R"SRC(
        module test;
        add_one(x : int) : int { return x + 1; }
        test() : int {
            fp : *(int):int = add_one;
            return fp(41);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable: local variable of link callable type", "[gen][callable]")
{
    auto jit = gen_jit(R"SRC(
        module test;
        double_it(x : int) : int { return x * 2; }
        test() : int {
            fp : +(int):int = double_it;
            return fp(21);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable: local variable of view callable type", "[gen][callable]")
{
    auto jit = gen_jit(R"SRC(
        module test;
        triple_it(x : int) : int { return x * 3; }
        test() : int {
            fp : ?(int):int = triple_it;
            return fp(14);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable: local variable of reference callable type", "[gen][callable]")
{
    // '&' is the new non-null, non-rebindable callable addresser.
    auto jit = gen_jit(R"SRC(
        module test;
        quad_it(x : int) : int { return x * 4; }
        test() : int {
            fp : &(int):int = quad_it;
            return fp(10) + 2;
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable: global variable of callable type", "[gen][callable]")
{
    auto jit = gen_jit(R"SRC(
        module test;
        add_one(x : int) : int { return x + 1; }
        gfp : *(int):int = add_one;
        test() : int { return gfp(41); }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable: rebind a pointer callable", "[gen][callable]")
{
    auto jit = gen_jit(R"SRC(
        module test;
        add_one(x : int) : int { return x + 1; }
        add_two(x : int) : int { return x + 2; }
        test() : int {
            fp : *(int):int = add_one;
            fp = add_two;
            return fp(40);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable: pass a callable as a parameter", "[gen][callable]")
{
    auto jit = gen_jit(R"SRC(
        module test;
        add_one(x : int) : int { return x + 1; }
        apply(f : *(int):int, x : int) : int { return f(x); }
        test() : int { return apply(add_one, 41); }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable: return a callable from a function", "[gen][callable]")
{
    auto jit = gen_jit(R"SRC(
        module test;
        add_one(x : int) : int { return x + 1; }
        get_fn() : *(int):int { return add_one; }
        test() : int {
            fp : *(int):int = get_fn();
            return fp(41);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable: several parameters", "[gen][callable]")
{
    auto jit = gen_jit(R"SRC(
        module test;
        add(a : int, b : int) : int { return a + b; }
        test() : int {
            fp : *(int, int):int = add;
            return fp(20, 22);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable: no parameter", "[gen][callable]")
{
    auto jit = gen_jit(R"SRC(
        module test;
        answer() : int { return 42; }
        test() : int {
            fp : *():int = answer;
            return fp();
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable: void return type is an omitted return type", "[gen][callable]")
{
    // K has no 'void' keyword: a callable without ': TypeSpec' returns nothing.
    auto jit = gen_jit(R"SRC(
        module test;
        counter : int = 0;
        bump(x : int) { counter += x; }
        test() : int {
            fp : *(int) = bump;
            fp(42);
            return counter;
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable: overload disambiguated by the callable parameter list",
    "[gen][callable][overload]")
{
    auto jit = gen_jit(R"SRC(
        module test;
        compute(x : int) : int { return x + 1; }
        compute(x : double) : int { return 99; }
        test() : int {
            fp : *(int):int = compute;
            return fp(41);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable: comparison against null and boolean conversion", "[gen][callable][null]")
{
    auto jit = gen_jit(R"SRC(
        module test;
        add_one(x : int) : int { return x + 1; }
        test() : int {
            fp : *(int):int = add_one;
            res : int = 0;
            if (fp != null) { res += 2; }
            if (fp) { res += 40; }
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable: 'void' is not a keyword, so ':void' does not parse", "[gen][callable][parse]")
{
    // 'void' is parsed as an ordinary (unknown) type name, so resolution fails.
    REQUIRE(compile_should_fail(R"SRC(
        module test;
        bump(x : int) { }
        test() : int {
            fp : *(int):void = bump;
            return 0;
        }
    )SRC", nullptr));
}

TEST_CASE("Callable: a bare prototype is not an instantiable value type", "[gen][callable][proto]")
{
    // '(int):int' names a signature; a variable needs an addresser.
    REQUIRE(compile_should_fail(R"SRC(
        module test;
        add_one(x : int) : int { return x + 1; }
        test() : int {
            fp : (int):int = add_one;
            return fp(41);
        }
    )SRC", nullptr));
}

TEST_CASE("Callable: capture-free lambda infers the callable prototype from the destination", "[gen][callable][lambda]")
{
    auto jit = gen_jit(R"SRC(
        module test;
        test() : int {
            fp : &(int):int = [](x : int) { return x + 1; };
            return fp(41);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable: a non-null callable must be initialised", "[gen][callable][null]")
{
    REQUIRE(compile_should_fail(R"SRC(
        module test;
        test() : int {
            fp : &(int):int;
            return 0;
        }
    )SRC", nullptr));
}

TEST_CASE("Callable: a reference callable is not rebindable", "[gen][callable][rebind]")
{
    REQUIRE(compile_should_fail(R"SRC(
        module test;
        add_one(x : int) : int { return x + 1; }
        add_two(x : int) : int { return x + 2; }
        test() : int {
            fp : &(int):int = add_one;
            fp = add_two;
            return fp(40);
        }
    )SRC", nullptr));
}

TEST_CASE("Callable: a view callable is not rebindable", "[gen][callable][rebind]")
{
    REQUIRE(compile_should_fail(R"SRC(
        module test;
        add_one(x : int) : int { return x + 1; }
        add_two(x : int) : int { return x + 2; }
        test() : int {
            fp : ?(int):int = add_one;
            fp = add_two;
            return fp(40);
        }
    )SRC", nullptr));
}

TEST_CASE("Callable: relational operators are forbidden on callables", "[gen][callable][operators]")
{
    REQUIRE(compile_should_fail(R"SRC(
        module test;
        add_one(x : int) : int { return x + 1; }
        add_two(x : int) : int { return x + 2; }
        test() : int {
            a : *(int):int = add_one;
            b : *(int):int = add_two;
            if (a < b) { return 1; }
            return 0;
        }
    )SRC", nullptr));
}
