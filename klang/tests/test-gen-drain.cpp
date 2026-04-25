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
 * Tests for the drain (#) indirection type:
 *   drain (#) — immutable binding, non-null, drainable indirection.
 *   Similar to a reference (&) but grants the consumer the permission
 *   to steal the internal resources of the referenced object.
 */

#include <catch2/catch_test_macros.hpp>
#include "helpers.hpp"

// =============================================================================
// PARSER TESTS
// =============================================================================

TEST_CASE("Parse int# type spec", "[parser][type][drain]") {
    test_logger log;
    k::source src{"int#"};
    k::parse::parser parser(log, src);
    auto spec = parser.parse_type_spec();
    REQUIRE(spec);

    auto ptr = std::dynamic_pointer_cast<k::parse::ast::pointer_type_specifier>(spec);
    REQUIRE(ptr);
    REQUIRE(ptr->pointer_type.type == k::lex::operator_::HASH);

    auto sub = std::dynamic_pointer_cast<k::parse::ast::keyword_type_specifier>(ptr->subtype);
    REQUIRE(sub);
    REQUIRE(sub->keyword.type == k::lex::keyword::INT);
}

TEST_CASE("Parse #expr unary prefix", "[parser][expression][drain]") {
    test_logger log;
    k::source src{"#ident"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_unary_expr();
    REQUIRE(expr);

    auto prefix = std::dynamic_pointer_cast<k::parse::ast::unary_prefix_expr>(expr);
    REQUIRE(prefix);
    REQUIRE(prefix->op.type == k::lex::operator_::HASH);

    auto ident = std::dynamic_pointer_cast<k::parse::ast::identifier_expr>(prefix->expr());
    REQUIRE(ident);
}

// =============================================================================
// BASIC DRAIN — PASS BY DRAIN TO A FUNCTION
// =============================================================================

// Drain parameter: function takes int# and just reads the value (no actual drain)
TEST_CASE("Drain basic read through drain parameter", "[gen][drain]") {
    auto jit = gen_jit(R"SRC(
        module __drain_basic_read__;

        read_via_drain(v : int#) : int {
            return v;
        }

        test() : int {
            x : int = 42;
            return read_via_drain(#x);
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

// Drain parameter with struct: pass a struct by drain, read its member
TEST_CASE("Drain struct member read", "[gen][drain]") {
    auto jit = gen_jit(R"SRC(
        module __drain_struct_read__;

        struct Pt {
            x : int = 0;
            y : int = 0;
            Pt(ax : int, ay : int) { x = ax; y = ay; }
        }

        sum_via_drain(p : Pt#) : int {
            return p.x + p.y;
        }

        test() : int {
            pt : Pt(10, 20);
            return sum_via_drain(#pt);
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 30);
}

// =============================================================================
// DRAIN IMPLICIT CONVERSION TO REFERENCE
// =============================================================================

// When only a reference overload exists, a drain should be implicitly castable
// But per user requirement #3: reference is NOT implicitly cast to drain.
// The drain operator # must be explicit. But drain → reference is OK.
TEST_CASE("Drain implicit conversion to reference parameter", "[gen][drain]") {
    auto jit = gen_jit(R"SRC(
        module __drain_to_ref__;

        // Only a reference-taking function exists
        read_ref(v : int&) : int {
            return v;
        }

        test() : int {
            x : int = 55;
            d : int# = #x;
            return read_ref(d);
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 55);
}

// =============================================================================
// DRAIN OVERLOAD RESOLUTION
// =============================================================================

// When both ref and drain overloads exist, #x selects the drain overload
TEST_CASE("Drain overload resolution prefers drain overload", "[gen][drain]") {
    auto jit = gen_jit(R"SRC(
        module __drain_overload__;

        choose(v : int&) : int {
            return 1;
        }

        choose(v : int#) : int {
            return 2;
        }

        test_ref() : int {
            x : int = 10;
            return choose(x);
        }

        test_drain() : int {
            x : int = 10;
            return choose(#x);
        }
    )SRC");
    REQUIRE(jit);
    auto fn_ref = jit->lookup_symbol<int(*)()>("test_ref");
    REQUIRE(fn_ref != nullptr);
    REQUIRE(fn_ref() == 1);  // reference overload

    auto fn_drain = jit->lookup_symbol<int(*)()>("test_drain");
    REQUIRE(fn_drain != nullptr);
    REQUIRE(fn_drain() == 2);  // drain overload
}

// =============================================================================
// DRAIN VARIABLE DECLARATION
// =============================================================================

// A local variable of drain type can be declared and used
TEST_CASE("Drain variable declaration and usage", "[gen][drain]") {
    auto jit = gen_jit(R"SRC(
        module __drain_var__;

        test() : int {
            x : int = 77;
            d : int# = #x;
            return d;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 77);
}

// =============================================================================
// DRAIN ON CONST — MUST FAIL
// =============================================================================

TEST_CASE("Drain on const object is rejected", "[gen][drain][error]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __drain_const_error__;

        test() : int {
            const x : int = 42;
            d : int# = #x;
            return d;
        }
    )SRC"), k::model::gen::resolution_error);
}

// =============================================================================
// MUTABLE LOCAL CAN BE PASSED TO DRAIN PARAMETER
// =============================================================================

// A mutable local variable is allowed to be passed to a drain parameter.
// Drain (#) binds to mutable objects; primitives are copied on drain,
// so the call is semantically correct.
TEST_CASE("Mutable local can be passed to drain parameter", "[gen][drain]") {
    auto jit = gen_jit(R"SRC(
        module __mutable_to_drain__;

        consume(v : int#) : int {
            return v;
        }

        test() : int {
            x : int = 10;
            return consume(x);
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 10);
}

// =============================================================================
// DRAIN MANGLING
// =============================================================================

TEST_CASE("Drain type mangling", "[gen][drain][mangling]") {
    auto jit = gen_jit(R"SRC(
        module __drain_mangle__;

        drain_fn(v : int#) : int {
            return v;
        }

        test() : int {
            x : int = 99;
            return drain_fn(#x);
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 99);
}

// =============================================================================
// DRAIN VALUE LOAD — DRAIN ACTS LIKE REFERENCE FOR READING
// =============================================================================

TEST_CASE("Drain value load — arithmetic through drain", "[gen][drain]") {
    auto jit = gen_jit(R"SRC(
        module __drain_arith__;

        add_via_drain(a : int#, b : int#) : int {
            return a + b;
        }

        test() : int {
            x : int = 10;
            y : int = 20;
            return add_via_drain(#x, #y);
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 30);
}

// =============================================================================
// DRAIN TYPE PRESERVED THROUGH FUNCTIONS
// =============================================================================

// Passing a drain through another function preserving the drain type
TEST_CASE("Drain parameter forwarding to drain parameter", "[gen][drain]") {
    auto jit = gen_jit(R"SRC(
        module __drain_forward__;

        inner(v : int#) : int {
            return v;
        }

        outer(v : int#) : int {
            return inner(v);
        }

        test() : int {
            x : int = 123;
            return outer(#x);
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 123);
}

// =============================================================================
// DRAIN WITH STRUCT — SIMULATING MOVE CONSTRUCTOR PATTERN
// =============================================================================

TEST_CASE("Drain struct — simulated move constructor pattern", "[gen][drain]") {
    auto jit = gen_jit(R"SRC(
        module __drain_move_ctor__;

        struct Resource {
            value : int = 0;

            Resource() {}
            Resource(v : int) { value = v; }

            // "Drain constructor" — takes a drain parameter
            Resource(other : Resource#) {
                value = other.value;
                other.value = 0;   // reset source (drain semantics)
            }
        }

        test_value() : int {
            a : Resource(42);
            b : Resource(#a);
            return b.value;
        }

        test_drained() : int {
            a : Resource(42);
            b : Resource(#a);
            return a.value;   // should be 0 after drain
        }
    )SRC");
    REQUIRE(jit);

    auto fn_value = jit->lookup_symbol<int(*)()>("test_value");
    REQUIRE(fn_value != nullptr);
    REQUIRE(fn_value() == 42);

    auto fn_drained = jit->lookup_symbol<int(*)()>("test_drained");
    REQUIRE(fn_drained != nullptr);
    REQUIRE(fn_drained() == 0);  // source was drained
}


