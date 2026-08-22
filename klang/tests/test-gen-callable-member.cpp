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
 * Bound member function callables (phase B.4).
 *
 * A member function designated *without* call parentheses and used where a
 * callable is expected produces a `callable_bind_expression`:
 *
 *  - a non-virtual method binds `{ @method, receiver }`, the receiver being
 *    upcast to the aggregate that *declares* the method;
 *  - a virtual method binds `{ vtable[slot], receiver }`, the receiver being the
 *    *unadjusted* subobject pointer the vptr was loaded from — the vtable slot
 *    (and its thunk for a secondary base) already performs the adjustment.
 *
 * A nullable receiver (`*` / `?`) bound to a nullable callable propagates the
 * null instead of trapping; bound to a `+` / `&` callable it raises a FatalError.
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

TEST_CASE("Callable member: bind a method through an object", "[gen][callable][member]")
{
    auto jit = gen_jit(R"SRC(
        module gen_callable_member_01;
        struct Counter {
            base : int;
            add(x : int) : int { return base + x; }
        }
        test() : int {
            c : Counter;
            c.base = 2;
            f : *(int):int = c.add;
            return f(40);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable member: bind through a reference receiver", "[gen][callable][member]")
{
    auto jit = gen_jit(R"SRC(
        module gen_callable_member_02;
        struct Counter {
            base : int;
            add(x : int) : int { return base + x; }
        }
        bind_it(c : Counter&) : int {
            f : &(int):int = c.add;
            return f(40);
        }
        test() : int {
            c : Counter;
            c.base = 2;
            return bind_it(c);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable member: bind through a pointer receiver", "[gen][callable][member]")
{
    auto jit = gen_jit(R"SRC(
        module gen_callable_member_03;
        struct Counter {
            base : int;
            add(x : int) : int { return base + x; }
        }
        test() : int {
            c : Counter;
            c.base = 2;
            p : Counter* = &c;
            f : *(int):int = p->add;
            return f(40);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable member: bind through a link receiver", "[gen][callable][member]")
{
    auto jit = gen_jit(R"SRC(
        module gen_callable_member_04;
        struct Counter {
            base : int;
            add(x : int) : int { return base + x; }
        }
        test() : int {
            c : Counter;
            c.base = 2;
            l : Counter+ = &c;
            f : +(int):int = l->add;
            return f(40);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable member: bind 'this.method' inside a member function", "[gen][callable][member]")
{
    auto jit = gen_jit(R"SRC(
        module gen_callable_member_05;
        struct Counter {
            base : int;
            add(x : int) : int { return base + x; }
            run(x : int) : int {
                f : *(int):int = this.add;
                return f(x);
            }
        }
        test() : int {
            c : Counter;
            c.base = 2;
            return c.run(40);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable member: bind a bare member name inside a member function", "[gen][callable][member]")
{
    // A bare `add` in a callable position inside a non-static member function
    // implicitly binds the enclosing `this`.
    auto jit = gen_jit(R"SRC(
        module gen_callable_member_06;
        struct Counter {
            base : int;
            add(x : int) : int { return base + x; }
            run(x : int) : int {
                f : *(int):int = add;
                return f(x);
            }
        }
        test() : int {
            c : Counter;
            c.base = 2;
            return c.run(40);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable member: a virtual method bound through a base reference dispatches to the override",
    "[gen][callable][member][virtual]")
{
    auto jit = gen_jit(R"SRC(
        module gen_callable_member_07;
        class Base {
            public:
            value(x : int) : int { return x; }
        }
        class Derived : public Base {
            public:
            override value(x : int) : int { return x + 2; }
        }
        bind_it(b : Base&) : int {
            f : &(int):int = b.value;
            return f(40);
        }
        test() : int {
            d : Derived;
            return bind_it(d);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable member: an inherited virtual method bound through a derived object",
    "[gen][callable][member][virtual]")
{
    // `d.value` resolves to Base::value: the receiver is upcast to the Base
    // subobject, which is also the pointer the vptr is loaded from.
    auto jit = gen_jit(R"SRC(
        module gen_callable_member_08;
        class Base {
            public:
            base : int;
            value(x : int) : int { return base + x; }
        }
        class Derived : public Base {
        }
        test() : int {
            d : Derived;
            d.base = 2;
            f : *(int):int = d.value;
            return f(40);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable member: a non-virtual method of a secondary base upcasts the receiver",
    "[gen][callable][member][upcast]")
{
    // `A` is not the first base of `C`, so binding `c.getA` must adjust the
    // receiver to the A subobject or the wrong field is read.
    auto jit = gen_jit(R"SRC(
        module gen_callable_member_09;
        struct First {
            pad0 : long;
            pad1 : long;
        }
        struct Second {
            val : int;
            get(x : int) : int { return val + x; }
        }
        struct Both : public First, public Second {
        }
        test() : int {
            b : Both;
            b.val = 2;
            f : *(int):int = b.get;
            return f(40);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable member: a const object binds a const member function",
    "[gen][callable][member][const]")
{
    auto jit = gen_jit(R"SRC(
        module gen_callable_member_10;
        struct Counter {
            base : int;
            const add(x : int) : int { return base + x; }
        }
        bind_it(c : const Counter&) : int {
            f : &(int):int = c.add;
            return f(40);
        }
        test() : int {
            c : Counter;
            c.base = 2;
            return bind_it(c);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable member: a const object cannot bind a non-const member function",
    "[gen][callable][member][const]")
{
    REQUIRE(compile_should_fail(R"SRC(
        module gen_callable_member_11;
        struct Counter {
            base : int;
            add(x : int) : int { return base + x; }
        }
        bind_it(c : const Counter&) : int {
            f : &(int):int = c.add;
            return f(40);
        }
        test() : int {
            c : Counter;
            return bind_it(c);
        }
    )SRC", nullptr));
}

TEST_CASE("Callable member: naming a non-static member without an object is an error",
    "[gen][callable][member][error]")
{
    // ERR_CALLABLE_MEMBER_BIND_REQUIRES_OBJECT
    REQUIRE(compile_should_fail(R"SRC(
        module gen_callable_member_12;
        struct Counter {
            base : int;
            add(x : int) : int { return base + x; }
        }
        test() : int {
            f : *(int):int = Counter::add;
            return f(40);
        }
    )SRC", nullptr));
}

TEST_CASE("Callable member: overloaded methods disambiguated by the callable prototype",
    "[gen][callable][member][overload]")
{
    auto jit = gen_jit(R"SRC(
        module gen_callable_member_13;
        struct Counter {
            base : int;
            compute(x : double) : int { return 99; }
            compute(x : int) : int { return base + x; }
        }
        test() : int {
            c : Counter;
            c.base = 2;
            f : *(int):int = c.compute;
            return f(40);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable member: store a bound callable in a struct member and call it later",
    "[gen][callable][member][store]")
{
    auto jit = gen_jit(R"SRC(
        module gen_callable_member_14;
        struct Counter {
            base : int;
            add(x : int) : int { return base + x; }
        }
        struct Holder {
            fn : *(int):int;
        }
        test() : int {
            c : Counter;
            c.base = 2;
            h : Holder;
            h.fn = c.add;
            return h.fn(40);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable member: pass a bound callable as an argument", "[gen][callable][member]")
{
    auto jit = gen_jit(R"SRC(
        module gen_callable_member_15;
        struct Counter {
            base : int;
            add(x : int) : int { return base + x; }
        }
        apply(f : &(int):int, x : int) : int { return f(x); }
        test() : int {
            c : Counter;
            c.base = 2;
            return apply(c.add, 40);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable member: a bound callable called inside a try catches the thrown value",
    "[gen][callable][member][throw]")
{
    // The bound callable is called through create_call_or_invoke(), so the indirect
    // call carries an unwind edge and the enclosing try catches the exception.
    auto jit = gen_jit(R"SRC(
        module gen_callable_member_16;
        class Boom : public Exception {
            public:
            Boom(code : int) : Exception(code) {}
        }
        struct Thrower {
            base : int;
            go(x : int) : int throws Boom {
                throw Boom(base + x);
            }
        }
        test() : int {
            t : Thrower;
            t.base = 2;
            f : *(int):int throws Boom = t.go;
            try {
                f(40);
            } catch (e : Boom&) {
                return e.getCode();
            }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable member: a null pointer receiver bound to a nullable callable yields null",
    "[gen][callable][member][null]")
{
    auto jit = gen_jit(R"SRC(
        module gen_callable_member_17;
        struct Counter {
            base : int;
            add(x : int) : int { return base + x; }
        }
        test() : int {
            p : Counter* = null;
            f : *(int):int = p->add;
            if (f == null) { return 42; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable member: a non-null pointer receiver bound to a nullable callable still binds",
    "[gen][callable][member][null]")
{
    auto jit = gen_jit(R"SRC(
        module gen_callable_member_18;
        struct Counter {
            base : int;
            add(x : int) : int { return base + x; }
        }
        test() : int {
            c : Counter;
            c.base = 2;
            p : Counter* = &c;
            f : *(int):int = p->add;
            if (f == null) { return 0; }
            return f(40);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable member: a null pointer receiver bound to a non-null callable is fatal",
    "[gen][callable][member][null]")
{
    // A '&' callable cannot hold null, so the bind dereferences the receiver and
    // the null check raises a FatalError (non-zero exit status).
    auto res = build_and_exec(R"SRC(
        module gen_callable_member_19;
        struct Counter {
            base : int;
            add(x : int) : int { return base + x; }
        }
        main() : int {
            p : Counter* = null;
            f : &(int):int = p->add;
            return f(40);
        }
    )SRC");
    REQUIRE(res.exit_code != 0);
}

TEST_CASE("Callable member: bind a virtual method through an owner receiver",
    "[gen][callable][member][virtual][owner]")
{
    auto jit = gen_jit(R"SRC(
        module gen_callable_member_20;
        class Base {
            public:
            value(x : int) : int { return x; }
        }
        class Derived : public Base {
            public:
            override value(x : int) : int { return x + 2; }
        }
        test() : int {
            o : Base! = new Derived();
            f : *(int):int = o->value;
            return f(40);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}
