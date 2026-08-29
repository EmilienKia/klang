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
 * Callable compatibility rules (phase B.7).
 *
 * Every callable binding site — a free function symbol, a bound member function,
 * a functor, a functional interface or another callable — funnels through
 * `callable_signature_compatible()` (klang/src/gen/gen_callable_compat.cpp).
 *
 * The rules it enforces:
 *
 *  - **arity** must match exactly;
 *  - the **return type is covariant**: what the target produces must be usable
 *    where the declared callable promises a value;
 *  - the **parameter types are contravariant**: what the declared callable will
 *    pass must be usable where the target expects a value;
 *  - `throws(target) ⊆ throws(callable)`;
 *  - identity is decided **nominally first**, so a `typedef` never silently
 *    collapses into the type it renames (a soft `alias` does, by definition);
 *  - an aggregate derivation is accepted **only when the base sub-object sits at
 *    offset 0** — an indirect call has no opportunity to adjust the address.
 *    In practice a K *class* stores its vptr at field 0 and therefore never has
 *    a base at offset 0; a *struct* does, but only for its first base;
 *  - **no primitive widening or narrowing at all** — `int` is not `long`, and
 *    `float` is not `double`, in either direction;
 *  - the addresser of an indirection must match exactly (a `Base+` return is not
 *    a `Base*` return).
 *
 * Binding a nullable (`*` / `?`) callable into a non-null (`+` / `&`) callable is
 * legal but guarded by a runtime null-check, exactly like a link rebind.
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

#include "../src/errors.hpp"
#include "../src/common/logger.hpp"

namespace {

/**
 * Compile @p src expecting a resolution failure carrying the diagnostic code @p code.
 */
template<typename Code>
void expect_diag(const char* src, Code code)
{
    const auto expected = static_cast<unsigned int>(code);
    try {
        gen_jit_throws(src);
        FAIL("Expected a compiler_error with code " << std::hex << expected);
    } catch (const k::log::compiler_error& e) {
        CHECK(e.get_diagnostic().code == expected);
    }
}

} // anonymous namespace


// ═══════════════════════════════════════════════════════════════════════════
// 1. Identical signatures
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Callable variance: identical signatures bind and call",
    "[gen][callable][variance]")
{
    auto jit = gen_jit(R"SRC(
        module gen_callable_variance_01;
        add_one(x : int) : int { return x + 1; }
        test() : int {
            f : *(int):int = add_one;
            return f(41);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable variance: the compatible overload is selected, not the first declared",
    "[gen][callable][variance][overload]")
{
    // Symbol resolution finds the *first* declaration carrying the name; the
    // callable binding must re-select the overload that actually fits.
    auto jit = gen_jit(R"SRC(
        module gen_callable_variance_02;
        pick(x : long) : long { return x + 100; }
        pick(x : int) : int { return x + 1; }
        test() : int {
            f : *(int):int = pick;
            return f(41);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}


// ═══════════════════════════════════════════════════════════════════════════
// 2. Return covariance at offset 0
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Callable variance: return covariance succeeds when the base is at offset 0",
    "[gen][callable][variance][covariance][run]")
{
    // A struct lays its *first* base out at field index 0, so a `Derived+` and the
    // `Base+` view of it share the very same address: the indirect call needs no
    // adjustment and the covariant binding is safe.
    auto res = build_and_exec(R"SRC(
        module gen_callable_variance_03;
        struct Base { a : int; }
        struct Derived : public Base { b : int; }
        identity(d : Derived+) : Derived+ { return d; }
        main() : int {
            d : Derived;
            d.a = 42;
            d.b = 7;
            f : *(Derived+):Base+ = identity;
            r : Base+ = f(d);
            return r->a;
        }
    )SRC");
    REQUIRE(res.exit_code == 42);
}


// ═══════════════════════════════════════════════════════════════════════════
// 3. Return covariance needing a non-zero adjustment
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Callable variance: return covariance through a second base is rejected",
    "[gen][callable][variance][covariance]")
{
    // 'B' is the *second* base of 'C': its sub-object starts after 'A', so a
    // 'C+' address is not a valid 'B+' address without an adjustment that an
    // indirect call cannot perform.
    expect_diag(R"SRC(
        module gen_callable_variance_04;
        struct A { a : int; }
        struct B { b : int; }
        struct C : public A, public B { c : int; }
        identity(x : C+) : C+ { return x; }
        test() : int {
            f : *(C+):B+ = identity;
            return 0;
        }
    )SRC", k::diag::callable_diag::ERR_CALLABLE_COVARIANCE_NEEDS_ADJUSTMENT);
}

TEST_CASE("Callable variance: return covariance through a class base is rejected",
    "[gen][callable][variance][covariance]")
{
    // A K class stores its vptr at field 0, so *no* base sub-object of a class is
    // ever at offset 0 — class covariance always needs an adjustment.
    expect_diag(R"SRC(
        module gen_callable_variance_05;
        class Shape { public: Shape() {} area() : int { return 1; } }
        class Circle : public Shape { public: Circle() {} area() : int { return 2; } }
        identity(c : Circle+) : Circle+ { return c; }
        test() : int {
            f : *(Circle+):Shape+ = identity;
            return 0;
        }
    )SRC", k::diag::callable_diag::ERR_CALLABLE_COVARIANCE_NEEDS_ADJUSTMENT);
}

TEST_CASE("Callable variance: parameter contravariance through a second base is rejected",
    "[gen][callable][variance][contravariance]")
{
    expect_diag(R"SRC(
        module gen_callable_variance_06;
        struct A { a : int; }
        struct B { b : int; }
        struct C : public A, public B { c : int; }
        takeB(x : B+) : int { return x->b; }
        test() : int {
            f : *(C+):int = takeB;
            return 0;
        }
    )SRC", k::diag::callable_diag::ERR_CALLABLE_COVARIANCE_NEEDS_ADJUSTMENT);
}


// ═══════════════════════════════════════════════════════════════════════════
// 4. Parameter contravariance
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Callable variance: parameter contravariance succeeds at offset 0",
    "[gen][callable][variance][contravariance][run]")
{
    // The callable promises to pass a 'Derived+'; the target only needs a 'Base+',
    // which the very same address already is.
    auto res = build_and_exec(R"SRC(
        module gen_callable_variance_07;
        struct Base { a : int; }
        struct Derived : public Base { b : int; }
        takeBase(x : Base+) : int { return x->a; }
        main() : int {
            d : Derived;
            d.a = 42;
            f : *(Derived+):int = takeBase;
            return f(d);
        }
    )SRC");
    REQUIRE(res.exit_code == 42);
}

TEST_CASE("Callable variance: the wrong contravariance direction is rejected",
    "[gen][callable][variance][contravariance]")
{
    // The callable only promises a 'Base+', but the target dereferences it as a
    // 'Derived+' — the substitution is unsound in that direction.
    expect_diag(R"SRC(
        module gen_callable_variance_08;
        struct Base { a : int; }
        struct Derived : public Base { b : int; }
        takeDerived(x : Derived+) : int { return x->b; }
        test() : int {
            f : *(Base+):int = takeDerived;
            return 0;
        }
    )SRC", k::diag::callable_diag::ERR_CALLABLE_INCOMPATIBLE_SIGNATURE);
}


// ═══════════════════════════════════════════════════════════════════════════
// 5. No primitive covariance
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Callable variance: an int return bound to a long callable is rejected",
    "[gen][callable][variance][primitive]")
{
    expect_diag(R"SRC(
        module gen_callable_variance_09;
        narrow(x : int) : int { return x; }
        test() : int {
            f : *(int):long = narrow;
            return 0;
        }
    )SRC", k::diag::callable_diag::ERR_CALLABLE_INCOMPATIBLE_SIGNATURE);
}

TEST_CASE("Callable variance: a long return bound to an int callable is rejected",
    "[gen][callable][variance][primitive]")
{
    expect_diag(R"SRC(
        module gen_callable_variance_10;
        wide(x : int) : long { return x; }
        test() : int {
            f : *(int):int = wide;
            return 0;
        }
    )SRC", k::diag::callable_diag::ERR_CALLABLE_INCOMPATIBLE_SIGNATURE);
}

TEST_CASE("Callable variance: a float/double parameter mismatch is rejected",
    "[gen][callable][variance][primitive]")
{
    expect_diag(R"SRC(
        module gen_callable_variance_11;
        scale(x : float) : float { return x; }
        test() : int {
            f : *(double):float = scale;
            return 0;
        }
    )SRC", k::diag::callable_diag::ERR_CALLABLE_INCOMPATIBLE_SIGNATURE);
}

TEST_CASE("Callable variance: an int parameter bound to a long callable is rejected",
    "[gen][callable][variance][primitive]")
{
    expect_diag(R"SRC(
        module gen_callable_variance_12;
        takeInt(x : int) : int { return x; }
        test() : int {
            f : *(long):int = takeInt;
            return 0;
        }
    )SRC", k::diag::callable_diag::ERR_CALLABLE_INCOMPATIBLE_SIGNATURE);
}


// ═══════════════════════════════════════════════════════════════════════════
// 6. Addresser mismatch
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Callable variance: a different addresser in the return type is rejected",
    "[gen][callable][variance][addresser]")
{
    expect_diag(R"SRC(
        module gen_callable_variance_13;
        struct Base { a : int; }
        make(b : Base+) : Base+ { return b; }
        test() : int {
            f : *(Base+):Base* = make;
            return 0;
        }
    )SRC", k::diag::callable_diag::ERR_CALLABLE_INCOMPATIBLE_SIGNATURE);
}

TEST_CASE("Callable variance: a different addresser in a parameter is rejected",
    "[gen][callable][variance][addresser]")
{
    expect_diag(R"SRC(
        module gen_callable_variance_14;
        struct Base { a : int; }
        take(b : Base+) : int { return b->a; }
        test() : int {
            f : *(Base*):int = take;
            return 0;
        }
    )SRC", k::diag::callable_diag::ERR_CALLABLE_INCOMPATIBLE_SIGNATURE);
}


// ═══════════════════════════════════════════════════════════════════════════
// 7. Arity mismatch
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Callable variance: too few parameters is rejected",
    "[gen][callable][variance][arity]")
{
    expect_diag(R"SRC(
        module gen_callable_variance_15;
        one(x : int) : int { return x; }
        test() : int {
            f : *(int, int):int = one;
            return 0;
        }
    )SRC", k::diag::callable_diag::ERR_CALLABLE_INCOMPATIBLE_SIGNATURE);
}

TEST_CASE("Callable variance: too many parameters is rejected",
    "[gen][callable][variance][arity]")
{
    expect_diag(R"SRC(
        module gen_callable_variance_16;
        two(x : int, y : int) : int { return x + y; }
        test() : int {
            f : *(int):int = two;
            return 0;
        }
    )SRC", k::diag::callable_diag::ERR_CALLABLE_INCOMPATIBLE_SIGNATURE);
}


// ═══════════════════════════════════════════════════════════════════════════
// 8. Nullable → non-null: runtime null-check
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Callable variance: a live '*' callable initialises a '+' callable",
    "[gen][callable][variance][null][run]")
{
    // The null-check passes: the guarded conversion must be transparent.
    auto res = build_and_exec(R"SRC(
        module gen_callable_variance_17;
        add_one(x : int) : int { return x + 1; }
        main() : int {
            fp : *(int):int = add_one;
            lnk : +(int):int = fp;
            return lnk(41);
        }
    )SRC");
    REQUIRE(res.exit_code == 42);
}

TEST_CASE("Callable variance: initialising a '+' callable from a null '*' callable traps",
    "[gen][callable][variance][null][run]")
{
    // A '+' callable is non-null by construction, so its call site skips the null
    // check — the guard has to be at the binding point instead.
    auto res = build_and_exec(R"SRC(
        module gen_callable_variance_18;
        main() : int {
            fp : *(int):int = null;
            lnk : +(int):int = fp;
            return lnk(41);
        }
    )SRC");
    REQUIRE(res.exit_code != 0);
    REQUIRE(res.exit_code != 42);
}

TEST_CASE("Callable variance: rebinding a '+' callable from a null '*' callable traps",
    "[gen][callable][variance][null][run]")
{
    auto res = build_and_exec(R"SRC(
        module gen_callable_variance_19;
        add_one(x : int) : int { return x + 1; }
        main() : int {
            fp : *(int):int = add_one;
            lnk : +(int):int = fp;
            fp = null;
            lnk = fp;
            return lnk(41);
        }
    )SRC");
    REQUIRE(res.exit_code != 0);
    REQUIRE(res.exit_code != 42);
}

// ═══════════════════════════════════════════════════════════════════════════
// 9. Owned callable (!) variance and conversions
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Callable variance: converting a borrowed callable to an owned callable is rejected",
    "[gen][callable][variance][owner]")
{
    expect_diag(R"SRC(
        module gen_callable_variance_20;
        add_one(x : int) : int { return x + 1; }
        test() : int {
            borrowed : *(int):int = add_one;
            owned : !(int):int = borrowed;
            return 0;
        }
    )SRC", k::diag::callable_model_diag::ERR_CALLABLE_OWNER_FROM_BORROW);
}

TEST_CASE("Callable variance: converting a reference callable to an owned callable is rejected",
    "[gen][callable][variance][owner]")
{
    expect_diag(R"SRC(
        module gen_callable_variance_21;
        add_one(x : int) : int { return x + 1; }
        test() : int {
            borrowed : &(int):int = add_one;
            owned : !(int):int = borrowed;
            return 0;
        }
    )SRC", k::diag::callable_model_diag::ERR_CALLABLE_OWNER_FROM_BORROW);
}


TEST_CASE("Callable variance: 'null' cannot initialise a non-null callable",
    "[gen][callable][variance][null]")
{
    expect_diag(R"SRC(
        module gen_callable_variance_20;
        test() : int {
            lnk : +(int):int = null;
            return 0;
        }
    )SRC", k::diag::callable_diag::ERR_CALLABLE_NULL_TO_NONNULL);
}

TEST_CASE("Callable variance: 'null' initialises a nullable callable",
    "[gen][callable][variance][null]")
{
    auto jit = gen_jit(R"SRC(
        module gen_callable_variance_21;
        test() : int {
            fp : *(int):int = null;
            if (fp == null) { return 42; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}


// ═══════════════════════════════════════════════════════════════════════════
// 9. Rebinding rules per addresser
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Callable variance: a '?' callable is initialisable but not rebindable",
    "[gen][callable][variance][rebind]")
{
    // Construction-time initialisation is accepted…
    auto jit = gen_jit(R"SRC(
        module gen_callable_variance_22;
        add_one(x : int) : int { return x + 1; }
        test() : int {
            f : ?(int):int = add_one;
            return f(41);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);

    // …but a later assignment is not.
    expect_diag(R"SRC(
        module gen_callable_variance_23;
        add_one(x : int) : int { return x + 1; }
        add_two(x : int) : int { return x + 2; }
        test() : int {
            f : ?(int):int = add_one;
            f = add_two;
            return f(40);
        }
    )SRC", k::diag::callable_diag::ERR_CALLABLE_NOT_REBINDABLE);
}

TEST_CASE("Callable variance: a '&' callable is initialisable but not rebindable",
    "[gen][callable][variance][rebind]")
{
    auto jit = gen_jit(R"SRC(
        module gen_callable_variance_24;
        add_one(x : int) : int { return x + 1; }
        test() : int {
            f : &(int):int = add_one;
            return f(41);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);

    expect_diag(R"SRC(
        module gen_callable_variance_25;
        add_one(x : int) : int { return x + 1; }
        add_two(x : int) : int { return x + 2; }
        test() : int {
            f : &(int):int = add_one;
            f = add_two;
            return f(40);
        }
    )SRC", k::diag::callable_diag::ERR_CALLABLE_NOT_REBINDABLE);
}

TEST_CASE("Callable variance: '*' and '+' callables are rebindable",
    "[gen][callable][variance][rebind]")
{
    auto jit = gen_jit(R"SRC(
        module gen_callable_variance_26;
        add_one(x : int) : int { return x + 1; }
        add_two(x : int) : int { return x + 2; }
        test() : int {
            p : *(int):int = add_one;
            l : +(int):int = add_one;
            p = add_two;
            l = add_two;
            return p(20) + l(18);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable variance: a const callable cannot be rebound",
    "[gen][callable][variance][rebind][const]")
{
    expect_diag(R"SRC(
        module gen_callable_variance_27;
        add_one(x : int) : int { return x + 1; }
        add_two(x : int) : int { return x + 2; }
        test() : int {
            const f : *(int):int = add_one;
            f = add_two;
            return f(40);
        }
    )SRC", k::diag::operator_diag::ERR_OVERLOAD_CALL_NO_MATCH);
}

TEST_CASE("Callable variance: a const callable is still invocable",
    "[gen][callable][variance][const]")
{
    auto jit = gen_jit(R"SRC(
        module gen_callable_variance_28;
        add_one(x : int) : int { return x + 1; }
        test() : int {
            const f : *(int):int = add_one;
            return f(41);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}


// ═══════════════════════════════════════════════════════════════════════════
// 10. Nominal alias vs typedef
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Callable variance: a soft alias is transparent in a callable prototype",
    "[gen][callable][variance][alias]")
{
    // 'alias' introduces a *name*, not a type: the prototype is identical.
    auto jit = gen_jit(R"SRC(
        module gen_callable_variance_29;
        alias Feet : int;
        f(x : int) : int { return x; }
        test() : int {
            a : *(int):Feet = f;
            return a(42);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}

TEST_CASE("Callable variance: a typedef stays nominally distinct in a callable return",
    "[gen][callable][variance][alias][typedef]")
{
    // 'typedef' introduces a distinct type: an 'int'-returning function does not
    // satisfy a 'Meters'-returning callable, and the callable check must not
    // canonicalise the alias away.
    expect_diag(R"SRC(
        module gen_callable_variance_30;
        typedef Meters : int;
        f(x : int) : int { return x; }
        test() : int {
            a : *(int):Meters = f;
            return 0;
        }
    )SRC", k::diag::callable_diag::ERR_CALLABLE_INCOMPATIBLE_SIGNATURE);
}

TEST_CASE("Callable variance: a typedef stays nominally distinct in a callable parameter",
    "[gen][callable][variance][alias][typedef]")
{
    expect_diag(R"SRC(
        module gen_callable_variance_31;
        typedef Meters : int;
        f(x : int) : int { return x; }
        test() : int {
            a : *(Meters):int = f;
            return 0;
        }
    )SRC", k::diag::callable_diag::ERR_CALLABLE_INCOMPATIBLE_SIGNATURE);
}

TEST_CASE("Callable variance: a typedef binds to a callable declared with the same typedef",
    "[gen][callable][variance][alias][typedef]")
{
    auto jit = gen_jit(R"SRC(
        module gen_callable_variance_32;
        typedef Meters : int;
        f(x : Meters) : Meters { return x; }
        test() : int {
            a : *(Meters):Meters = f;
            return (int) a((Meters) 42);
        }
    )SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn != nullptr);
    REQUIRE(test_fn() == 42);
}


// ═══════════════════════════════════════════════════════════════════════════
// 11. throws variance
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Callable variance: the same throws set is accepted",
    "[gen][callable][variance][throws][run]")
{
    auto res = build_and_exec(R"SRC(
        module gen_callable_variance_33;
        class Boom : public Exception { public: Boom(c : int) : Exception(c) {} }
        may_throw(x : int) : int throws(Boom) {
            if (x < 0) { throw Boom(1); }
            return x;
        }
        main() : int {
            f : *(int):int throws(Boom) = may_throw;
            return f(42);
        }
    )SRC");
    REQUIRE(res.exit_code == 42);
}

TEST_CASE("Callable variance: a throws subset is accepted",
    "[gen][callable][variance][throws][run]")
{
    auto res = build_and_exec(R"SRC(
        module gen_callable_variance_34;
        class Boom : public Exception { public: Boom(c : int) : Exception(c) {} }
        class Bang : public Exception { public: Bang(c : int) : Exception(c) {} }
        may_throw(x : int) : int throws(Boom) {
            if (x < 0) { throw Boom(1); }
            return x;
        }
        main() : int {
            f : *(int):int throws(Boom, Bang) = may_throw;
            return f(42);
        }
    )SRC");
    REQUIRE(res.exit_code == 42);
}

TEST_CASE("Callable variance: a throws base class covers a derived thrown type",
    "[gen][callable][variance][throws][run]")
{
    auto res = build_and_exec(R"SRC(
        module gen_callable_variance_35;
        class Boom : public Exception { public: Boom(c : int) : Exception(c) {} }
        class Bigger : public Boom { public: Bigger(c : int) : Boom(c) {} }
        may_throw(x : int) : int throws(Bigger) {
            if (x < 0) { throw Bigger(1); }
            return x;
        }
        main() : int {
            f : *(int):int throws(Boom) = may_throw;
            return f(42);
        }
    )SRC");
    REQUIRE(res.exit_code == 42);
}

TEST_CASE("Callable variance: a throws superset is rejected",
    "[gen][callable][variance][throws]")
{
    expect_diag(R"SRC(
        module gen_callable_variance_36;
        class Boom : public Exception { public: Boom(c : int) : Exception(c) {} }
        class Bang : public Exception { public: Bang(c : int) : Exception(c) {} }
        may_throw(x : int) : int throws(Boom, Bang) {
            if (x < 0) { throw Boom(1); }
            return x;
        }
        test() : int {
            f : *(int):int throws(Boom) = may_throw;
            return 0;
        }
    )SRC", k::diag::callable_model_diag::ERR_CALLABLE_THROWS_NOT_SUBSET);
}

TEST_CASE("Callable variance: a missing throws clause on the callable is rejected",
    "[gen][callable][variance][throws]")
{
    expect_diag(R"SRC(
        module gen_callable_variance_37;
        class Boom : public Exception { public: Boom(c : int) : Exception(c) {} }
        may_throw(x : int) : int throws(Boom) {
            if (x < 0) { throw Boom(1); }
            return x;
        }
        test() : int {
            f : *(int):int = may_throw;
            return 0;
        }
    )SRC", k::diag::callable_model_diag::ERR_CALLABLE_THROWS_NOT_SUBSET);
}

TEST_CASE("Callable variance: a non-throwing target binds to a throwing callable",
    "[gen][callable][variance][throws][run]")
{
    // The empty set is a subset of every set.
    auto res = build_and_exec(R"SRC(
        module gen_callable_variance_38;
        class Boom : public Exception { public: Boom(c : int) : Exception(c) {} }
        safe(x : int) : int { return x; }
        main() : int {
            f : *(int):int throws(Boom) = safe;
            return f(42);
        }
    )SRC");
    REQUIRE(res.exit_code == 42);
}

TEST_CASE("Callable variance: a callable-to-callable throws superset is rejected",
    "[gen][callable][variance][throws]")
{
    // The conversion check also runs when the source is another callable value.
    expect_diag(R"SRC(
        module gen_callable_variance_39;
        class Boom : public Exception { public: Boom(c : int) : Exception(c) {} }
        may_throw(x : int) : int throws(Boom) {
            if (x < 0) { throw Boom(1); }
            return x;
        }
        test() : int {
            a : *(int):int throws(Boom) = may_throw;
            b : *(int):int = a;
            return 0;
        }
    )SRC", k::diag::callable_model_diag::ERR_CALLABLE_THROWS_NOT_SUBSET);
}


// ═══════════════════════════════════════════════════════════════════════════
// Callable → callable conversions
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Callable variance: a callable-to-callable signature mismatch is rejected",
    "[gen][callable][variance]")
{
    expect_diag(R"SRC(
        module gen_callable_variance_40;
        wide(x : long) : long { return x; }
        test() : int {
            a : *(long):long = wide;
            b : *(int):int = a;
            return 0;
        }
    )SRC", k::diag::callable_diag::ERR_CALLABLE_INCOMPATIBLE_SIGNATURE);
}

TEST_CASE("Callable variance: a callable-to-callable copy keeping the prototype succeeds",
    "[gen][callable][variance][run]")
{
    auto res = build_and_exec(R"SRC(
        module gen_callable_variance_41;
        add_one(x : int) : int { return x + 1; }
        main() : int {
            a : *(int):int = add_one;
            b : *(int):int = a;
            return b(41);
        }
    )SRC");
    REQUIRE(res.exit_code == 42);
}

TEST_CASE("Callable variance: a callable argument is checked at the call site",
    "[gen][callable][variance]")
{
    expect_diag(R"SRC(
        module gen_callable_variance_42;
        wide(x : int) : long { return x; }
        apply(f : *(int):int, x : int) : int { return f(x); }
        test() : int { return apply(wide, 41); }
    )SRC", k::diag::symbol_diag::ERR_OVERLOAD_NO_MATCH);
}


// ═══════════════════════════════════════════════════════════════════════════
// Bound member functions obey the very same rules
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Callable variance: a bound member function must satisfy the throws rule",
    "[gen][callable][variance][member][throws]")
{
    expect_diag(R"SRC(
        module gen_callable_variance_43;
        class Boom : public Exception { public: Boom(c : int) : Exception(c) {} }
        struct Thrower {
            base : int;
            go(x : int) : int throws(Boom) { throw Boom(base + x); }
        }
        test() : int {
            t : Thrower;
            f : *(int):int = t.go;
            return 0;
        }
    )SRC", k::diag::callable_model_diag::ERR_CALLABLE_THROWS_NOT_SUBSET);
}

TEST_CASE("Callable variance: a bound member function must satisfy the primitive rule",
    "[gen][callable][variance][member][primitive]")
{
    expect_diag(R"SRC(
        module gen_callable_variance_44;
        struct Widener {
            base : long;
            go(x : int) : long { return base + x; }
        }
        test() : int {
            w : Widener;
            f : *(int):int = w.go;
            return 0;
        }
    )SRC", k::diag::callable_diag::ERR_CALLABLE_INCOMPATIBLE_SIGNATURE);
}

TEST_CASE("Callable variance: a bound member function benefits from offset-0 covariance",
    "[gen][callable][variance][member][covariance][run]")
{
    auto res = build_and_exec(R"SRC(
        module gen_callable_variance_45;
        struct Base { a : int; }
        struct Derived : public Base { b : int; }
        struct Factory {
            held : Derived;
            get() : Derived+ { return held; }
        }
        main() : int {
            fac : Factory;
            fac.held.a = 42;
            f : *():Base+ = fac.get;
            r : Base+ = f();
            return r->a;
        }
    )SRC");
    REQUIRE(res.exit_code == 42);
}
