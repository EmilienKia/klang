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
