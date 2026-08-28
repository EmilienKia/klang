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
        module gen_callable_basic_01;
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
        module gen_callable_basic_02;
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
        module gen_callable_basic_03;
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
        module gen_callable_basic_04;
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
        module gen_callable_basic_05;
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
        module gen_callable_basic_06;
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
        module gen_callable_basic_07;
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
        module gen_callable_basic_08;
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
        module gen_callable_basic_09;
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
        module gen_callable_basic_10;
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
        module gen_callable_basic_11;
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
        module gen_callable_basic_12;
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
        module gen_callable_basic_13;
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
        module gen_callable_basic_14;
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
        module gen_callable_basic_15;
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
        module gen_callable_basic_16;
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
        module gen_callable_basic_17;
        test() : int {
            fp : &(int):int;
            return 0;
        }
    )SRC", nullptr));
}

TEST_CASE("Callable: a reference callable is not rebindable", "[gen][callable][rebind]")
{
    REQUIRE(compile_should_fail(R"SRC(
        module gen_callable_basic_18;
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
        module gen_callable_basic_19;
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
        module gen_callable_basic_20;
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

TEST_CASE("Callable: local variable of owned callable type", "[gen][callable][owner]")
{
    auto jit = gen_jit(R"SRC(
        module gen_callable_basic_21;
        add_one(x : int) : int { return x + 1; }
        test() : int {
            fp : !(int):int = add_one;
            return fp(41);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable: owned callable can be default-initialized to null", "[gen][callable][owner][null]")
{
    auto jit = gen_jit(R"SRC(
        module gen_callable_basic_22;
        test() : int {
            fp : !(int):int;
            if (fp == null) { return 42; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable: borrow callable from owned callable", "[gen][callable][owner][borrow]")
{
    auto jit = gen_jit(R"SRC(
        module gen_callable_basic_23;
        add_one(x : int) : int { return x + 1; }
        test() : int {
            owned : !(int):int = add_one;
            borrowed : &(int):int = owned;
            return borrowed(41);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable: owned callable variable initialization moves and nulls source", "[gen][callable][owner][move]")
{
    auto jit = gen_jit(R"SRC(
        module gen_callable_basic_24;
        add_one(x : int) : int { return x + 1; }
        test() : int {
            src : !(int):int = add_one;
            dst : !(int):int = src;
            if (src == null && dst != null) {
                return dst(41);
            }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable: owned callable assignment moves and nulls source", "[gen][callable][owner][assign]")
{
    auto jit = gen_jit(R"SRC(
        module gen_callable_basic_25;
        add_one(x : int) : int { return x + 1; }
        test() : int {
            src : !(int):int = add_one;
            dst : !(int):int;
            dst = src;
            if (src == null && dst != null) {
                return dst(41);
            }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable: pass owned callable as parameter moves ownership", "[gen][callable][owner][param]")
{
    auto jit = gen_jit(R"SRC(
        module gen_callable_basic_26;
        add_one(x : int) : int { return x + 1; }
        consume(f : !(int):int) : int {
            if (f != null) {
                return f(41);
            }
            return 0;
        }
        test() : int {
            src : !(int):int = add_one;
            res : int = consume(src);
            if (src == null && res == 42) {
                return 42;
            }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable: return owned callable transfers ownership to caller", "[gen][callable][owner][return]")
{
    auto jit = gen_jit(R"SRC(
        module gen_callable_basic_27;
        add_one(x : int) : int { return x + 1; }
        make_fn() : !(int):int {
            f : !(int):int = add_one;
            return f;
        }
        test() : int {
            res : !(int):int = make_fn();
            if (res != null) {
                return res(41);
            }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}


