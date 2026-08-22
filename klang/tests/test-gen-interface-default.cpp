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
 * Tests for interface default methods (Java-style).
 *
 * THE RULES:
 *   - A member function of an interface declared with the 'default' prefix
 *     specifier and a body is a *default method*: a concrete, virtual method.
 *   - A class implementing the interface that does NOT override the method
 *     inherits the default implementation through the vtable (no need to be
 *     declared abstract).
 *   - A class MAY override a default method; the override is then dispatched.
 *   - A default method body may call other (abstract or default) methods of the
 *     same interface, which dispatch dynamically to the concrete implementation.
 *   - 'default' is only allowed on interface member functions, with a body, and
 *     is incompatible with 'static', 'final', 'abstract', 'private', ctor/dtor
 *     and '-> default/delete/redirect'.
 *   - For a template interface, default methods are synthesised per
 *     instantiation (linkonce_odr), like every other template member.
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

// ════════════════════════════════════════════════════════════════════════════
//  Model-level checks
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[default][model] default method is concrete (non-abstract) and flagged", "[interface][default][model]") {
    auto comp = k::compiler::create();
    comp->parse_source("", R"SRC(
module gen_interface_default_01;
interface Greeter {
    name() : int;
    default greet() : int { return 1; }
}
)SRC");

    auto elems = comp->find_elements("Greeter");
    REQUIRE(!elems.empty());
    auto iface = std::dynamic_pointer_cast<k::model::interface>(elems[0]);
    REQUIRE(iface);

    auto name_fn = iface->get_function("name");
    REQUIRE(name_fn);
    CHECK(name_fn->is_abstract_func());
    CHECK_FALSE(name_fn->is_default_method());

    auto greet_fn = iface->get_function("greet");
    REQUIRE(greet_fn);
    CHECK(greet_fn->is_default_method());
    CHECK_FALSE(greet_fn->is_abstract_func());
}

// ════════════════════════════════════════════════════════════════════════════
//  Functional: class inherits the default implementation (no override)
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[default] class inherits the default implementation", "[interface][default][dispatch]") {

    SECTION("Direct call on concrete instance") {
        auto jit = gen_jit(R"SRC(
module gen_interface_default_02;
interface Greeter {
    default greet() : int { return 7; }
}
class Hello : public Greeter {
    Hello() {}
}
test() : int {
    h: Hello;
    return h.greet();
}
)SRC");
        REQUIRE(jit);
        auto fn = jit->lookup_symbol<int(*)()>("test");
        REQUIRE(fn);
        CHECK(fn() == 7);
    }

    SECTION("Dispatch through interface reference") {
        auto jit = gen_jit(R"SRC(
module gen_interface_default_03;
interface Greeter {
    default greet() : int { return 7; }
}
class Hello : public Greeter {
    Hello() {}
}
via(g: Greeter&) : int { return g.greet(); }
test() : int {
    h: Hello;
    return via(h);
}
)SRC");
        REQUIRE(jit);
        auto fn = jit->lookup_symbol<int(*)()>("test");
        REQUIRE(fn);
        CHECK(fn() == 7);
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  Functional: class overrides the default implementation
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[default] class override wins over the default", "[interface][default][dispatch]") {
    auto jit = gen_jit(R"SRC(
module gen_interface_default_04;
interface Greeter {
    default greet() : int { return 7; }
}
class Custom : public Greeter {
    Custom() {}
    override greet() : int { return 42; }
}
class Fallback : public Greeter {
    Fallback() {}
}
via(g: Greeter&) : int { return g.greet(); }
test_custom()   : int { c: Custom;   return via(c); }
test_fallback() : int { f: Fallback; return via(f); }
)SRC");
    REQUIRE(jit);
    auto fc = jit->lookup_symbol<int(*)()>("test_custom");
    auto ff = jit->lookup_symbol<int(*)()>("test_fallback");
    REQUIRE(fc); REQUIRE(ff);
    CHECK(fc() == 42);
    CHECK(ff() == 7);
}

// ════════════════════════════════════════════════════════════════════════════
//  Functional: default method calls an abstract method (dynamic dispatch)
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[default] default body calls abstract method of same interface", "[interface][default][dispatch]") {
    auto jit = gen_jit(R"SRC(
module gen_interface_default_05;
interface Doubler {
    base() : int;
    default doubled() : int { return this.base() * 2; }
}
class Five : public Doubler {
    Five() {}
    base() : int { return 5; }
}
class Ten : public Doubler {
    Ten() {}
    base() : int { return 10; }
}
via(d: Doubler&) : int { return d.doubled(); }
test_five() : int { f: Five; return via(f); }
test_ten()  : int { t: Ten;  return via(t); }
)SRC");
    REQUIRE(jit);
    auto ff = jit->lookup_symbol<int(*)()>("test_five");
    auto ft = jit->lookup_symbol<int(*)()>("test_ten");
    REQUIRE(ff); REQUIRE(ft);
    CHECK(ff() == 10);
    CHECK(ft() == 20);
}

// ════════════════════════════════════════════════════════════════════════════
//  Functional: default method calls another default method
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[default] default body calls another default method", "[interface][default][dispatch]") {
    auto jit = gen_jit(R"SRC(
module gen_interface_default_06;
interface Calc {
    seed() : int;
    default plus_one() : int { return this.seed() + 1; }
    default plus_two() : int { return this.plus_one() + 1; }
}
class C : public Calc {
    C() {}
    seed() : int { return 40; }
}
test() : int { c: C; return c.plus_two(); }
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

// ════════════════════════════════════════════════════════════════════════════
//  Functional: sub-interface provides a default for a super-interface method
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[default] sub-interface provides default for parent method", "[interface][default][inheritance]") {
    auto jit = gen_jit(R"SRC(
module gen_interface_default_07;
interface A {
    value() : int;
}
interface B : public A {
    override default value() : int { return 3; }
}
class Impl : public B {
    Impl() {}
}
via(a: A&) : int { return a.value(); }
test() : int { i: Impl; return via(i); }
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 3);
}

TEST_CASE("[default] default method resolves inherited member call", "[interface][default][inheritance]") {
    auto jit = gen_jit(R"SRC(
module gen_interface_default_08;
interface Sized {
    size() : int;
}
interface Indexed : public Sized {
    insert(index: int, value: int);
}
interface Appendable {
    append(value: int);
}
interface MutableIndexed : public Indexed, public Appendable {
    default append(value: int) { insert(size(), value); }
}
class Vec : public MutableIndexed {
    _n: int;
    Vec() { _n = 0; }
    size() : int { return _n; }
    insert(index: int, value: int) { ++_n; }
}
test() : int {
    v: Vec;
    v.append(1);
    v.append(2);
    return v.size();
}
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 2);
}

// ════════════════════════════════════════════════════════════════════════════
//  Template interfaces: default method synthesised per instantiation
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[default][template] template interface default method (independent of T)", "[interface][default][template]") {
    auto jit = gen_jit(R"SRC(
module gen_interface_default_09;
template<typename T>
interface Container {
    size() : int;
    default isEmptyValue() : int { return this.size(); }
}
class IntList : public Container<int> {
    IntList() {}
    size() : int { return 0; }
}
test() : int { l: IntList; return l.isEmptyValue(); }
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

TEST_CASE("[default][template] template interface default method using T", "[interface][default][template]") {
    auto jit = gen_jit(R"SRC(
module gen_interface_default_10;
template<typename T>
interface Box {
    get() : T;
    default getOrTwice() : T { return this.get() + this.get(); }
}
class IntBox : public Box<int> {
    IntBox() {}
    get() : int { return 21; }
}
test() : int { b: IntBox; return b.getOrTwice(); }
)SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

// ════════════════════════════════════════════════════════════════════════════
//  Negative: 'default' misuse is rejected
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[default][error] default outside interface is rejected", "[interface][default][error]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
module gen_interface_default_11;
class C {
    C() {}
    default foo() : int { return 1; }
}
)SRC"));

    REQUIRE_THROWS(gen_jit_throws(R"SRC(
module gen_interface_default_12;
struct S {
    default foo() : int { return 1; }
}
)SRC"));
}

TEST_CASE("[default][error] default without a body is rejected", "[interface][default][error]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
module gen_interface_default_13;
interface I {
    default foo() : int;
}
)SRC"));
}

TEST_CASE("[default][error] default combined with static/final/abstract is rejected", "[interface][default][error]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
module gen_interface_default_14;
interface I {
    default static foo() : int { return 1; }
}
)SRC"));

    REQUIRE_THROWS(gen_jit_throws(R"SRC(
module gen_interface_default_15;
interface I {
    default final foo() : int { return 1; }
}
)SRC"));

    REQUIRE_THROWS(gen_jit_throws(R"SRC(
module gen_interface_default_16;
interface I {
    default abstract foo() : int { return 1; }
}
)SRC"));
}

TEST_CASE("[default][error] private default is rejected", "[interface][default][error]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
module gen_interface_default_17;
interface I {
private:
    default foo() : int { return 1; }
}
)SRC"));
}

// ════════════════════════════════════════════════════════════════════════════
//  Cross-module: default method inherited through an imported interface (KDI)
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[default][import] default method inherited across modules", "[interface][default][import]") {
    // A class in the executable module implements an imported interface but does
    // NOT override its default method. The vtable slot must reference the default
    // method symbol from the library, and the class must be instantiable.
    auto result = build_exec_with_lib(
        R"K(
            module gen_interface_default_18;
            interface Greeter {
                base() : int;
                default greet() : int { return this.base() + 5; }
            }
        )K",
        R"K(
            module gen_interface_default_19;
            import gen_interface_default_18;
            class Hello : public gen_interface_default_18::Greeter {
                Hello() {}
                base() : int { return 37; }
            }
            main() : int {
                h: Hello;
                return h.greet();
            }
        )K");
    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);
    REQUIRE(result.exit_code == 42);
}

// ════════════════════════════════════════════════════════════════════════════
//  Regression: interface method without body remains abstract
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[default][regression] bodyless interface method stays abstract", "[interface][default][regression]") {
    // A non-default interface method with a body is still an error, and a class
    // that leaves an abstract method unimplemented must be declared abstract.
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
module gen_interface_default_20;
interface I {
    foo() : int { return 1; }
}
)SRC"));

    REQUIRE_THROWS(gen_jit_throws(R"SRC(
module gen_interface_default_21;
interface I {
    foo() : int;
}
class C : public I {
    C() {}
}
)SRC"));
}




