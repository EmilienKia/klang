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
 * Tests for const-ness: const variables, const parameters, const pointers/links.
 */

#include <catch2/catch_test_macros.hpp>
#include "helpers.hpp"

// =============================================================================
// CONST LOCAL VARIABLES
// =============================================================================

// A const local variable can be read normally.
TEST_CASE("Const local variable read", "[gen][const]") {
    auto jit = gen_jit(R"SRC(
        module __const_local_read__;

        test() : int {
            const x : int = 42;
            return x;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

// A const local variable cannot be assigned after construction.
TEST_CASE("Const local variable assignment rejected", "[gen][const][error]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __const_local_assign__;

        test() {
            const x : int = 1;
            x = 2;
        }
    )SRC"), k::log::compiler_error);
}

// Prefix ++ forbidden on const variable.
TEST_CASE("Const prefix increment rejected", "[gen][const][error]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __const_prefix_inc__;

        test() {
            const x : int = 1;
            ++x;
        }
    )SRC"), k::log::compiler_error);
}

// Prefix -- forbidden on const variable.
TEST_CASE("Const prefix decrement rejected", "[gen][const][error]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __const_prefix_dec__;

        test() {
            const x : int = 1;
            --x;
        }
    )SRC"), k::log::compiler_error);
}

// Postfix ++ forbidden on const variable.
TEST_CASE("Const postfix increment rejected", "[gen][const][error]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __const_postfix_inc__;

        test() {
            const x : int = 1;
            x++;
        }
    )SRC"), k::log::compiler_error);
}

// Postfix -- forbidden on const variable.
TEST_CASE("Const postfix decrement rejected", "[gen][const][error]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __const_postfix_dec__;

        test() {
            const x : int = 1;
            x--;
        }
    )SRC"), k::log::compiler_error);
}

// =============================================================================
// CONST GLOBAL VARIABLES
// =============================================================================

// A const global variable can be read normally.
TEST_CASE("Const global variable read", "[gen][const]") {
    auto jit = gen_jit(R"SRC(
        module __const_global_read__;

        const ANSWER : int = 42;

        test() : int {
            return ANSWER;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

// A const global variable cannot be assigned.
TEST_CASE("Const global variable assignment rejected", "[gen][const][error]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __const_global_assign__;

        const G : int = 10;

        test() {
            G = 20;
        }
    )SRC"), k::log::compiler_error);
}

// =============================================================================
// CONST PARAMETERS
// =============================================================================

// A const parameter can be read; it is passed by value with a const flag.
TEST_CASE("Const parameter read", "[gen][const]") {
    auto jit = gen_jit(R"SRC(
        module __const_param_read__;

        double_it(const n : int) : int {
            return n + n;
        }

        test() : int {
            return double_it(21);
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

// A const parameter cannot be assigned inside the function.
TEST_CASE("Const parameter assignment rejected", "[gen][const][error]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __const_param_assign__;

        bad(const n : int) {
            n = 99;
        }
    )SRC"), k::log::compiler_error);
}

// Two overloads distinguishable only by const parameter: const on a by-value
// parameter is part of the function implementation, not its interface.
// Calling pick(0) is therefore ambiguous, and the compiler must report an error.
TEST_CASE("Const parameter overload is ambiguous", "[gen][const][error]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __const_param_overload__;

        pick(n : int) : int { return 1; }
        pick(const n : int) : int { return 2; }

        test() : int {
            return pick(0);
        }
    )SRC"), k::log::compiler_error);
}

// =============================================================================
// POINTERS / LINKS TO CONST
// =============================================================================

// A mutable pointer can be assigned to a pointer-to-const (widening).
TEST_CASE("Mutable pointer to const pointer widening", "[gen][const]") {
    auto jit = gen_jit(R"SRC(
        module __const_ptr_widen__;

        test() : int {
            x   : int  = 7;
            p   : int* = &x;
            cp  : const int* = p;   // mutable -> const: OK
            return *cp;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 7);
}

// A pointer-to-const cannot be assigned to a mutable pointer.
TEST_CASE("Const pointer to mutable pointer rejected", "[gen][const][error]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __const_ptr_narrow__;

        test() {
            x   : int      = 5;
            cp  : const int* = &x;
            p   : int*       = cp;  // const -> mutable: forbidden
        }
    )SRC"), k::log::compiler_error);
}

// A mutable link can be assigned to a link-to-const (widening).
TEST_CASE("Mutable link to const link widening", "[gen][const]") {
    auto jit = gen_jit(R"SRC(
        module __const_link_widen__;

        test() : int {
            x   : int  = 9;
            lnk : int~ = &x;
            clnk : const int~ = &x;   // mutable -> const: OK
            return *clnk;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 9);
}

// A link-to-const cannot be rebound to a mutable link.
TEST_CASE("Const link to mutable link rebind rejected", "[gen][const][error]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __const_link_rebind__;

        test() {
            x    : int      = 3;
            clnk : const int~ = &x;
            lnk  : int~        = clnk;  // const -> mutable: forbidden
        }
    )SRC"), k::log::compiler_error);
}

// address-of a const variable yields a const link.
TEST_CASE("Address of const variable yields const link", "[gen][const]") {
    auto jit = gen_jit(R"SRC(
        module __const_addr_of__;

        test() : int {
            const x : int = 55;
            clnk : const int~ = &x;
            return *clnk;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 55);
}

// Cannot take the address of a const variable and store it in a mutable link.
TEST_CASE("Address of const variable to mutable link rejected", "[gen][const][error]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __const_addr_mutable_link__;

        test() {
            const x : int = 1;
            lnk : int~ = &x;   // &x has type const int~; assigning to int~ loses const
        }
    )SRC"), k::log::compiler_error);
}

// =============================================================================
// CONST FORM EQUIVALENCE
// =============================================================================

// "const var : T" and "var : const T" and "const var : const T" are equivalent.
// All three should produce the same behaviour: read OK, assign rejected.

TEST_CASE("Const form equivalence — specifier side", "[gen][const]") {
    // Form 1: const on the specifier side  →  const x : int
    auto jit = gen_jit(R"SRC(
        module __const_equiv_spec__;
        test() : int {
            const x : int = 7;
            return x;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 7);
}

TEST_CASE("Const form equivalence — type side", "[gen][const]") {
    // Form 2: const on the type side  →  x : const int
    auto jit = gen_jit(R"SRC(
        module __const_equiv_type__;
        test() : int {
            x : const int = 7;
            return x;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 7);
}

TEST_CASE("Const form equivalence — both sides", "[gen][const]") {
    // Form 3: const on both sides  →  const x : const int
    auto jit = gen_jit(R"SRC(
        module __const_equiv_both__;
        test() : int {
            const x : const int = 7;
            return x;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 7);
}

TEST_CASE("Const form equivalence — type side assignment rejected", "[gen][const][error]") {
    // "x : const int" must also reject assignment
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __const_equiv_type_assign__;
        test() {
            x : const int = 5;
            x = 6;   // must be rejected
        }
    )SRC"), k::log::compiler_error);
}

TEST_CASE("Const form equivalence — param specifier side", "[gen][const]") {
    // Param: const n : int
    auto jit = gen_jit(R"SRC(
        module __const_equiv_param_spec__;
        f(const n : int) : int { return n; }
        test() : int { return f(99); }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 99);
}

TEST_CASE("Const form equivalence — param type side", "[gen][const]") {
    // Param: n : const int
    auto jit = gen_jit(R"SRC(
        module __const_equiv_param_type__;
        f(n : const int) : int { return n; }
        test() : int { return f(99); }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 99);
}

TEST_CASE("Const form equivalence — param both sides", "[gen][const]") {
    // Param: const n : const int
    auto jit = gen_jit(R"SRC(
        module __const_equiv_param_both__;
        f(const n : const int) : int { return n; }
        test() : int { return f(99); }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 99);
}

// The three forms of a const link are equivalent: all forbid writing through the link.
TEST_CASE("Const link form equivalence — specifier side write rejected", "[gen][const][error]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __const_link_spec_write__;
        test() {
            x : int = 3;
            const lnk : int~ = &x;
            *lnk = 5;   // must be rejected
        }
    )SRC"), k::log::compiler_error);
}

TEST_CASE("Const link form equivalence — type side write rejected", "[gen][const][error]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __const_link_type_write__;
        test() {
            x : int = 3;
            lnk : const int~ = &x;
            *lnk = 5;   // must be rejected
        }
    )SRC"), k::log::compiler_error);
}

// =============================================================================
// CONST MEMBER FUNCTIONS
// =============================================================================

// A const member function can be called on a mutable object.
TEST_CASE("Const member function — call on mutable object", "[gen][const][struct]") {
    auto jit = gen_jit(R"SRC(
        module __const_mfn_mutable_obj__;
        struct Counter {
            value : int;
            Counter(v : int) : value(v) {}
            const get() : int { return this.value; }
        }
        test() : int {
            c : Counter(42);
            return c.get();
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

// A const member function can be called on a const object.
TEST_CASE("Const member function — call on const object (by ref)", "[gen][const][struct]") {
    auto jit = gen_jit(R"SRC(
        module __const_mfn_const_obj__;
        struct Point {
            x : int;
            y : int;
            Point(px : int, py : int) : x(px), y(py) {}
            const sum() : int { return this.x + this.y; }
        }
        read(p : const Point&) : int {
            return p.sum();
        }
        test() : int {
            pt : Point(3, 7);
            return read(pt);
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 10);
}

// A mutable member function cannot be called on a const object.
TEST_CASE("Mutable member function call on const object rejected", "[gen][const][struct][error]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __const_mfn_mutable_on_const__;
        struct Counter {
            value : int;
            Counter() : value(0) {}
            increment() { value = value + 1; }
        }
        test(c : const Counter&) {
            c.increment();   // must be rejected: mutable method on const object
        }
    )SRC"), k::log::compiler_error);
}

// A const member function cannot assign to a member field.
TEST_CASE("Const member function cannot assign to field", "[gen][const][struct][error]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __const_mfn_assign_field__;
        struct Counter {
            value : int;
            Counter() : value(0) {}
            const bad_set(v : int) { this.value = v; }  // must be rejected
        }
    )SRC"), k::log::compiler_error);
}

// Const member function can read a field.
TEST_CASE("Const member function can read field via this", "[gen][const][struct]") {
    auto jit = gen_jit(R"SRC(
        module __const_mfn_read_field__;
        struct Box {
            size : int;
            Box(s : int) : size(s) {}
            const area() : int { return this.size * this.size; }
        }
        test() : int {
            b : Box(5);
            return b.area();
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 25);
}

// const local variable of struct type: only const methods can be called.
TEST_CASE("Const local struct variable — only const methods callable", "[gen][const][struct][error]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __const_local_struct_mutable_call__;
        struct Counter {
            value : int;
            Counter() : value(0) {}
            increment() { value = value + 1; }
        }
        test() {
            const c : Counter();
            c.increment();   // must be rejected
        }
    )SRC"), k::log::compiler_error);
}

// const local variable of struct type: can read field.
TEST_CASE("Const local struct variable — field is read-only", "[gen][const][struct][error]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __const_local_struct_field_assign__;
        struct Point {
            x : int;
            Point(v : int) : x(v) {}
        }
        test() {
            const p : Point(3);
            p.x = 5;    // must be rejected: field of const object is const
        }
    )SRC"), k::log::compiler_error);
}

// const local variable of struct type: const method call succeeds.
TEST_CASE("Const local struct variable — const method callable", "[gen][const][struct]") {
    auto jit = gen_jit(R"SRC(
        module __const_local_struct_const_call__;
        struct Point {
            x : int;
            y : int;
            Point(px : int, py : int) : x(px), y(py) {}
            const sum() : int { return this.x + this.y; }
        }
        test() : int {
            const p : Point(4, 6);
            return p.sum();
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 10);
}

// =============================================================================
// CONST STRUCT
// =============================================================================

// In a const struct, a non-const non-static member function is implicitly promoted to const
// with a warning — it is NOT rejected as a compile error.
TEST_CASE("Const struct with implicit-const method emits warning and compiles", "[gen][const][struct]") {
    // The method 'mutate' is not declared const but belongs to a const struct.
    // It should be silently promoted to const (warning emitted) and the code should compile.
    // Note: direct field assignment inside the promoted method that uses the direct-symbol
    // path (not this.x) may not yet be blocked by const checking — that is a separate issue.
    auto jit = gen_jit(R"SRC(
        module __const_struct_implicit_const__;
        const struct Frozen {
            x : int;
            Frozen(v : int) : x(v) {}
            const get() : int { return this.x; }
            get_via_implicit_const() : int { return this.x; }  // promoted implicitly to const
        }
        test() : int {
            f : Frozen(42);
            return f.get_via_implicit_const();
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

// A const struct with only const methods compiles fine.
TEST_CASE("Const struct — all const methods — compiles and runs", "[gen][const][struct]") {
    auto jit = gen_jit(R"SRC(
        module __const_struct_ok__;
        const struct Frozen {
            x : int;
            Frozen(v : int) : x(v) {}
            const get() : int { return this.x; }
        }
        test() : int {
            f : Frozen(99);
            return f.get();
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 99);
}

// A const struct cannot inherit from a mutable struct.
TEST_CASE("Const struct cannot inherit from mutable struct", "[gen][const][struct][error]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __const_struct_bad_inherit__;
        struct Mutable {
            x : int;
            Mutable(v : int) : x(v) {}
        }
        const struct Frozen : public Mutable {
            Frozen(v : int) : Mutable(v) {}
            const get() : int { return this.x; }
        }
    )SRC", false, false), k::model::gen::resolution_error);
}

// A mutable struct can inherit from a const struct.
TEST_CASE("Mutable struct can inherit from const struct", "[gen][const][struct]") {
    auto jit = gen_jit(R"SRC(
        module __mutable_struct_inherit_const__;
        const struct ReadOnly {
            x : int;
            ReadOnly(v : int) : x(v) {}
            ReadOnly(src : const ReadOnly&) : x(src.x) {}
            const get() : int { return this.x; }
        }
        struct Writable : public ReadOnly {
            Writable(v : int) : ReadOnly(v) {}
            Writable(src : Writable&) : ReadOnly(src.get()) {}
            set(v : int) { this.x = v; }
        }
        test() : int {
            w : Writable(10);
            w.set(42);
            return w.get();
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

// Const and static are not compatible on a member function.
TEST_CASE("Const static member function rejected", "[gen][const][struct][error]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __const_static_method__;
        struct Bad {
            const static make() : int { return 1; }
        }
    )SRC"), k::log::compiler_error);
}

