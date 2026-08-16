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
 * Functional interface binding to callables (phase B.6).
 *
 * THE RULES:
 *   - An interface — or, per design decision D10, an abstract class — is
 *     *functional* when its vtable layout holds exactly **one** abstract virtual
 *     slot, the universal destructor slot excluded.
 *   - Binding a functional receiver (`I&`, `I*`, `I+`, `I?`) to a callable whose
 *     prototype matches the single abstract method produces a
 *     `callable_bind_expression(kind::functional_interface)`: the `fn` field is
 *     the vtable slot load and the `ctx` field is the *unadjusted* subobject
 *     pointer the vptr was loaded from.
 *   - The slot count is computed on the vtable *layout*, so an abstract method
 *     inherited from a base interface — or re-declared several times along a
 *     diamond — counts exactly once.
 *   - Default methods are concrete and never counted.
 *   - A receiver that is an interface or an abstract class but does not have
 *     exactly one abstract slot is rejected with ERR_CALLABLE_NOT_FUNCTIONAL_IFACE
 *     (0x01D7).
 *   - A functional receiver whose single abstract method does not match the
 *     destination prototype is rejected with
 *     ERR_CALLABLE_IFACE_SIGNATURE_MISMATCH (0x01D8).
 *   - A concrete aggregate implementing exactly one abstract slot inherited from a
 *     functional base is bindable too (spec §6.6); it stays silent when it does
 *     not fit, so the generic conversion diagnostics keep their place.
 *   - A nullable receiver (`*` / `?`) bound to a nullable callable propagates the
 *     null instead of trapping; bound to a `+` / `&` callable it raises a
 *     FatalError.
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"
#include "../src/errors.hpp"

namespace {

/** Warnings emitted by the deliberately redundant re-declarations of the diamond tests. */
const IgnoredDiagCodes REDECL_WARNINGS = {0x0176, 0x017F};

} // anonymous namespace

// ════════════════════════════════════════════════════════════════════════════
//  [1] A single-abstract-method interface binds and dispatches
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Callable interface: a single-abstract-method interface binds to a callable",
    "[gen][callable][interface]")
{
    auto jit = gen_jit(R"SRC(
        module test;
        interface Transformer {
            transform(x : int) : int;
        }
        class Doubler : public Transformer {
            public:
            override transform(x : int) : int { return x * 2; }
        }
        bind_it(t : Transformer&) : int {
            f : &(int):int = t;
            return f(21);
        }
        test() : int {
            d : Doubler;
            return bind_it(d);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable interface: the bound callable dispatches through the vtable",
    "[gen][callable][interface][virtual]")
{
    // Two implementations of the same functional interface bound through the same
    // interface reference must reach their own override.
    auto jit = gen_jit(R"SRC(
        module test;
        interface Transformer {
            transform(x : int) : int;
        }
        class Doubler : public Transformer {
            public:
            override transform(x : int) : int { return x * 2; }
        }
        class Incrementer : public Transformer {
            public:
            override transform(x : int) : int { return x + 1; }
        }
        bind_it(t : Transformer&, x : int) : int {
            f : &(int):int = t;
            return f(x);
        }
        test() : int {
            d : Doubler;
            i : Incrementer;
            return bind_it(d, 20) + bind_it(i, 1);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable interface: a functional interface returning nothing binds to '*(int)'",
    "[gen][callable][interface]")
{
    auto jit = gen_jit(R"SRC(
        module test;
        interface Sink {
            accept(x : int);
        }
        counter : int = 0;
        class Accumulator : public Sink {
            public:
            override accept(x : int) { counter += x; }
        }
        test() : int {
            a : Accumulator;
            s : Sink+ = &a;
            f : *(int) = s;
            f(42);
            return counter;
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

// ════════════════════════════════════════════════════════════════════════════
//  [2] Default methods do not count as abstract slots
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Callable interface: one default method plus one abstract method is still functional",
    "[gen][callable][interface][default]")
{
    auto jit = gen_jit(R"SRC(
        module test;
        interface Transformer {
            transform(x : int) : int;
            default twice(x : int) : int { return transform(transform(x)); }
        }
        class Doubler : public Transformer {
            public:
            override transform(x : int) : int { return x * 2; }
        }
        bind_it(t : Transformer&) : int {
            f : &(int):int = t;
            return f(21);
        }
        test() : int {
            d : Doubler;
            return bind_it(d);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

// ════════════════════════════════════════════════════════════════════════════
//  [3] 0 or 2 abstract methods → ERR_CALLABLE_NOT_FUNCTIONAL_IFACE
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Callable interface: an interface with no abstract method is not functional",
    "[gen][callable][interface][error]")
{
    try {
        gen_jit_throws(R"SRC(
            module test;
            interface AllDefault {
                default apply(x : int) : int { return x; }
            }
            bind_it(t : AllDefault&) : int {
                f : &(int):int = t;
                return f(21);
            }
        )SRC");
        FAIL("Expected a resolution_error to be thrown");
    } catch (const k::model::gen::resolution_error& e) {
        CHECK(e.get_diagnostic().code ==
              static_cast<unsigned int>(k::diag::callable_diag::ERR_CALLABLE_NOT_FUNCTIONAL_IFACE));
    }
}

TEST_CASE("Callable interface: an interface with two abstract methods is not functional",
    "[gen][callable][interface][error]")
{
    try {
        gen_jit_throws(R"SRC(
            module test;
            interface TwoOps {
                first(x : int) : int;
                second(x : int) : int;
            }
            bind_it(t : TwoOps&) : int {
                f : &(int):int = t;
                return f(21);
            }
        )SRC");
        FAIL("Expected a resolution_error to be thrown");
    } catch (const k::model::gen::resolution_error& e) {
        CHECK(e.get_diagnostic().code ==
              static_cast<unsigned int>(k::diag::callable_diag::ERR_CALLABLE_NOT_FUNCTIONAL_IFACE));
    }
}

TEST_CASE("Callable interface: an abstract class with two abstract methods is not functional",
    "[gen][callable][interface][error][abstract]")
{
    try {
        gen_jit_throws(R"SRC(
            module test;
            abstract class TwoOps {
                public:
                abstract first(x : int) : int;
                abstract second(x : int) : int;
            }
            bind_it(t : TwoOps&) : int {
                f : &(int):int = t;
                return f(21);
            }
        )SRC");
        FAIL("Expected a resolution_error to be thrown");
    } catch (const k::model::gen::resolution_error& e) {
        CHECK(e.get_diagnostic().code ==
              static_cast<unsigned int>(k::diag::callable_diag::ERR_CALLABLE_NOT_FUNCTIONAL_IFACE));
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  [4] Signature mismatch → ERR_CALLABLE_IFACE_SIGNATURE_MISMATCH
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Callable interface: a mismatching parameter list is rejected",
    "[gen][callable][interface][error]")
{
    try {
        gen_jit_throws(R"SRC(
            module test;
            interface Transformer {
                transform(x : int) : int;
            }
            bind_it(t : Transformer&) : int {
                f : &(double):int = t;
                return f(21.0);
            }
        )SRC");
        FAIL("Expected a resolution_error to be thrown");
    } catch (const k::model::gen::resolution_error& e) {
        CHECK(e.get_diagnostic().code ==
              static_cast<unsigned int>(k::diag::callable_diag::ERR_CALLABLE_IFACE_SIGNATURE_MISMATCH));
    }
}

TEST_CASE("Callable interface: a mismatching arity is rejected",
    "[gen][callable][interface][error]")
{
    try {
        gen_jit_throws(R"SRC(
            module test;
            interface Transformer {
                transform(x : int) : int;
            }
            bind_it(t : Transformer&) : int {
                f : &(int, int):int = t;
                return f(21, 21);
            }
        )SRC");
        FAIL("Expected a resolution_error to be thrown");
    } catch (const k::model::gen::resolution_error& e) {
        CHECK(e.get_diagnostic().code ==
              static_cast<unsigned int>(k::diag::callable_diag::ERR_CALLABLE_IFACE_SIGNATURE_MISMATCH));
    }
}

TEST_CASE("Callable interface: a mismatching return type is rejected",
    "[gen][callable][interface][error]")
{
    try {
        gen_jit_throws(R"SRC(
            module test;
            interface Transformer {
                transform(x : int) : int;
            }
            bind_it(t : Transformer&) : long {
                f : &(int):long = t;
                return f(21);
            }
        )SRC");
        FAIL("Expected a resolution_error to be thrown");
    } catch (const k::model::gen::resolution_error& e) {
        CHECK(e.get_diagnostic().code ==
              static_cast<unsigned int>(k::diag::callable_diag::ERR_CALLABLE_IFACE_SIGNATURE_MISMATCH));
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  [5] Abstract class with one abstract method (decision D10)
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Callable interface: an abstract class with a single abstract method is functional",
    "[gen][callable][interface][abstract]")
{
    auto jit = gen_jit(R"SRC(
        module test;
        abstract class AbstractOp {
            public:
            abstract apply(x : int) : int;
            helper(x : int) : int { return x + 1; }
        }
        class Impl : public AbstractOp {
            public:
            override apply(x : int) : int { return x * 2; }
        }
        bind_it(a : AbstractOp&) : int {
            f : &(int):int = a;
            return f(21);
        }
        test() : int {
            i : Impl;
            return bind_it(i);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

// ════════════════════════════════════════════════════════════════════════════
//  [6] Every addresser: I&, I*, I+, I? — and the null I* case
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Callable interface: bind through a pointer receiver", "[gen][callable][interface]")
{
    auto jit = gen_jit(R"SRC(
        module test;
        interface Transformer {
            transform(x : int) : int;
        }
        class Doubler : public Transformer {
            public:
            override transform(x : int) : int { return x * 2; }
        }
        test() : int {
            d : Doubler;
            p : Transformer* = &d;
            f : *(int):int = p;
            return f(21);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable interface: bind through a link receiver", "[gen][callable][interface]")
{
    auto jit = gen_jit(R"SRC(
        module test;
        interface Transformer {
            transform(x : int) : int;
        }
        class Doubler : public Transformer {
            public:
            override transform(x : int) : int { return x * 2; }
        }
        test() : int {
            d : Doubler;
            l : Transformer+ = &d;
            f : +(int):int = l;
            return f(21);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable interface: bind through a view receiver", "[gen][callable][interface]")
{
    auto jit = gen_jit(R"SRC(
        module test;
        interface Transformer {
            const transform(x : int) : int;
        }
        class Doubler : public Transformer {
            public:
            override const transform(x : int) : int { return x * 2; }
        }
        test() : int {
            d : Doubler;
            v : Transformer? = &d;
            f : *(int):int = v;
            return f(21);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable interface: bind through an owner receiver",
    "[gen][callable][interface][owner]")
{
    auto jit = gen_jit(R"SRC(
        module test;
        interface Transformer {
            transform(x : int) : int;
        }
        class Doubler : public Transformer {
            public:
            override transform(x : int) : int { return x * 2; }
        }
        test() : int {
            o : Transformer! = new Doubler();
            f : *(int):int = o;
            return f(21);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable interface: a const receiver binds a const abstract method",
    "[gen][callable][interface][const]")
{
    auto jit = gen_jit(R"SRC(
        module test;
        interface Transformer {
            const transform(x : int) : int;
        }
        class Doubler : public Transformer {
            public:
            override const transform(x : int) : int { return x * 2; }
        }
        bind_it(t : const Transformer&) : int {
            f : &(int):int = t;
            return f(21);
        }
        test() : int {
            d : Doubler;
            return bind_it(d);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable interface: a null pointer receiver bound to a nullable callable yields null",
    "[gen][callable][interface][null]")
{
    auto jit = gen_jit(R"SRC(
        module test;
        interface Transformer {
            transform(x : int) : int;
        }
        test() : int {
            p : Transformer* = null;
            f : *(int):int = p;
            if (f == null) { return 42; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable interface: a non-null pointer receiver bound to a nullable callable still binds",
    "[gen][callable][interface][null]")
{
    auto jit = gen_jit(R"SRC(
        module test;
        interface Transformer {
            transform(x : int) : int;
        }
        class Doubler : public Transformer {
            public:
            override transform(x : int) : int { return x * 2; }
        }
        test() : int {
            d : Doubler;
            p : Transformer* = &d;
            f : *(int):int = p;
            if (f == null) { return 0; }
            return f(21);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable interface: a null pointer receiver bound to a non-null callable is fatal",
    "[gen][callable][interface][null]")
{
    // A '&' callable cannot hold null, so the bind dereferences the receiver and the
    // null check raises a FatalError (non-zero exit status).
    auto res = build_and_exec(R"SRC(
        module test;
        interface Transformer {
            transform(x : int) : int;
        }
        main() : int {
            p : Transformer* = null;
            f : &(int):int = p;
            return f(21);
        }
    )SRC");
    REQUIRE(res.exit_code != 0);
}

// ════════════════════════════════════════════════════════════════════════════
//  [7] An inherited abstract method counts once
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Callable interface: an abstract method inherited from a base interface counts once",
    "[gen][callable][interface][inheritance]")
{
    auto jit = gen_jit(R"SRC(
        module test;
        interface BaseOp {
            apply(x : int) : int;
        }
        interface DerivedOp : public BaseOp {
            default helper(x : int) : int { return x + 1; }
        }
        class Impl : public DerivedOp {
            public:
            override apply(x : int) : int { return x * 2; }
        }
        bind_it(d : DerivedOp&) : int {
            f : &(int):int = d;
            return f(21);
        }
        test() : int {
            i : Impl;
            return bind_it(i);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable interface: a derived interface adding a second abstract method is not functional",
    "[gen][callable][interface][inheritance][error]")
{
    try {
        gen_jit_throws(R"SRC(
            module test;
            interface BaseOp {
                apply(x : int) : int;
            }
            interface DerivedOp : public BaseOp {
                other(x : int) : int;
            }
            bind_it(d : DerivedOp&) : int {
                f : &(int):int = d;
                return f(21);
            }
        )SRC");
        FAIL("Expected a resolution_error to be thrown");
    } catch (const k::model::gen::resolution_error& e) {
        CHECK(e.get_diagnostic().code ==
              static_cast<unsigned int>(k::diag::callable_diag::ERR_CALLABLE_NOT_FUNCTIONAL_IFACE));
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  [8] A diamond re-declaring the same abstract method counts once
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Callable interface: a diamond re-declaring the same abstract method counts once",
    "[gen][callable][interface][diamond]")
{
    auto jit = gen_jit(R"SRC(
        module test;
        interface Root {
            apply(x : int) : int;
        }
        interface Left : public Root {
            apply(x : int) : int;
        }
        interface Right : public Root {
            apply(x : int) : int;
        }
        interface Bottom : public Left, public Right {
        }
        class Impl : public Bottom {
            public:
            override apply(x : int) : int { return x * 2; }
        }
        bind_it(b : Bottom&) : int {
            f : &(int):int = b;
            return f(21);
        }
        test() : int {
            i : Impl;
            return bind_it(i);
        }
    )SRC", false, false, REDECL_WARNINGS);
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

// ════════════════════════════════════════════════════════════════════════════
//  Concrete receivers, storage and rebinding
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Callable interface: a concrete class implementing one abstract slot binds too",
    "[gen][callable][interface][concrete]")
{
    // Spec §6.6: `fp : *(int):int = d;` where `d` is the concrete implementation.
    auto jit = gen_jit(R"SRC(
        module test;
        interface Transformer {
            transform(x : int) : int;
        }
        class Doubler : public Transformer {
            public:
            override transform(x : int) : int { return x * 2; }
        }
        test() : int {
            d : Doubler;
            f : *(int):int = d;
            return f(21);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable interface: a plain class implementing nothing abstract is not bindable",
    "[gen][callable][interface][error]")
{
    // No abstract slot at all: the functional-interface rule declines silently and
    // the generic conversion diagnostics reject the assignment.
    REQUIRE(compile_should_fail(R"SRC(
        module test;
        class Plain {
            public:
            value(x : int) : int { return x; }
        }
        test() : int {
            p : Plain;
            f : *(int):int = p;
            return f(21);
        }
    )SRC", nullptr));
}

TEST_CASE("Callable interface: store a bound interface callable in a struct member",
    "[gen][callable][interface][store]")
{
    auto jit = gen_jit(R"SRC(
        module test;
        interface Transformer {
            transform(x : int) : int;
        }
        class Doubler : public Transformer {
            public:
            override transform(x : int) : int { return x * 2; }
        }
        struct Holder {
            fn : *(int):int;
        }
        test() : int {
            d : Doubler;
            t : Transformer+ = &d;
            h : Holder;
            h.fn = t;
            return h.fn(21);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable interface: rebind a pointer callable from an interface receiver",
    "[gen][callable][interface][rebind]")
{
    auto jit = gen_jit(R"SRC(
        module test;
        interface Transformer {
            transform(x : int) : int;
        }
        class Doubler : public Transformer {
            public:
            override transform(x : int) : int { return x * 2; }
        }
        class Incrementer : public Transformer {
            public:
            override transform(x : int) : int { return x + 1; }
        }
        test() : int {
            d : Doubler;
            i : Incrementer;
            td : Transformer+ = &d;
            ti : Transformer+ = &i;
            f : *(int):int = td;
            r : int = f(20);
            f = ti;
            return r + f(1);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable interface: a functor operator() wins over the functional-interface rule",
    "[gen][callable][interface][functor]")
{
    // An abstract class declaring `operator()` binds as a functor (phase B.5), not
    // as a functional interface: the functor rule is tried first.
    auto jit = gen_jit(R"SRC(
        module test;
        abstract class Callable {
            public:
            abstract other(x : int) : int;
            operator()(x : int) : int { return x * 2; }
        }
        class Impl : public Callable {
            public:
            override other(x : int) : int { return 0; }
        }
        bind_it(c : Callable&) : int {
            f : &(int):int = c;
            return f(21);
        }
        test() : int {
            i : Impl;
            return bind_it(i);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}
