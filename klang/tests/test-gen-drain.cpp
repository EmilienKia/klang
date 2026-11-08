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

#include <catch2/catch_all.hpp>
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
        module gen_drain_01;

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
        module gen_drain_02;

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
        module gen_drain_03;

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
        module gen_drain_04;

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
        module gen_drain_05;

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
        module gen_drain_06;

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

// A mutable local variable can be explicitly drained and passed to a drain parameter.
// Drain (#) binds to mutable objects; primitives are copied on drain,
// so the call is semantically correct.
// Note: implicit ref→drain conversion is NOT allowed (spec says explicit # is required).
TEST_CASE("Mutable local can be passed to drain parameter", "[gen][drain]") {
    auto jit = gen_jit(R"SRC(
        module gen_drain_07;

        consume(v : int#) : int {
            return v;
        }

        test() : int {
            x : int = 10;
            return consume(#x);
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
        module gen_drain_08;

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
        module gen_drain_09;

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
        module gen_drain_10;

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
        module gen_drain_11;

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

TEST_CASE("Drain to value rejects a deleted copy in every value context",
          "[gen][drain][copy][delete][regression]") {
    for (const auto* body : {
             "test(value: Key&): int { consume(#value); return 0; }",
             "test(value: Key#): int { consume(value); return 0; }",
             "test(value: Key&): Key { return #value; }",
             "test(value: Key&): int { copy: Key = (Key) #value; return 0; }",
             "test(value: Key&): int { slot: Slot(#value); return 0; }",
             "test(value: Key&): int { d: Key# = #value; slot: Slot(d); return 0; }",
             "test(value: Key&): int { useSlot(Slot(#value)); return 0; }"}) {
        std::string source = R"K(
            module drain_deleted_copy;
            class Key {
            private:
                handle: int* = null;
                Key(other: const Key&) -> delete;
            public:
                Key() {}
                ~Key() {}
            }
            union Slot { empty: byte; key: Key; }
            consume(value: Key) {}
            useSlot(value: Slot) {}
        )K";
        source += body;
        INFO(body);
        REQUIRE_THROWS_MATCHES(gen_jit_throws(source), k::log::compiler_error,
            Catch::Matchers::Predicate<k::log::compiler_error>(
                [](const auto& error) {
                    return error.get_diagnostic().message.find("deleted") != std::string::npos;
                }, "selected deleted copy constructor"));
    }
}

TEST_CASE("Drain to value selects a resource transfer constructor",
          "[gen][drain][move][regression]") {
    auto jit = gen_jit(R"K(
        module drain_value_move;
        releases: int = 0;
        drops: int = 0;
        moves: int = 0;
        class Resource {
        public:
            owns: int = 1;
            Resource() {}
            Resource(other: const Resource&) -> delete;
            Resource(other: Resource#) : owns(other.owns) { other.owns = 0; ++moves; }
            ~Resource() { if (owns != 0) ++releases; ++drops; }
        }
        consume(value: Resource): int { return value.owns; }
        forward(value: Resource#): int { return consume(value); }
        test(): int {
            releases = 0; drops = 0; moves = 0;
            {
                first: Resource;
                if (consume(#first) != 1 || first.owns != 0) return -1;
                second: Resource;
                if (forward(#second) != 1 || second.owns != 0) return -2;
            }
            if (moves != 2 || releases != 2 || drops != 4) return -3;
            return 42;
        }
    )K");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("Drain to value uses the selected copy without draining the source",
          "[gen][drain][copy][overload][regression]") {
    auto jit = gen_jit(R"K(
        module drain_value_copy;
        copies: int = 0;
        class Value {
        public:
            number: int = 42;
            Value() {}
            Value(other: const Value&) -> delete;
            Value(other: Value&) : number(other.number) { ++copies; }
        }
        consume(value: Value): int { return value.number; }
        test(): int {
            source: Value;
            first: int = consume(#source);
            second: int = consume((Value) #source);
            if (copies != 2 || source.number != 42) return -1;
            return first + second - 42;
        }
    )K");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("Drain to value does not bypass a private transfer constructor",
          "[gen][drain][move][visibility][regression]") {
    REQUIRE_THROWS_MATCHES(gen_jit_throws(R"K(
        module drain_private_move;
        class Resource {
        private:
            Resource(other: Resource#) {}
        public:
            Resource() {}
            Resource(other: const Resource&) {}
        }
        consume(value: Resource) {}
        test(value: Resource&): int { consume(#value); return 0; }
    )K"), k::log::compiler_error,
        Catch::Matchers::Predicate<k::log::compiler_error>(
            [](const auto& error) {
                const auto& diagnostic = error.get_diagnostic();
                return diagnostic.code == 0x080Cu
                    && diagnostic.args.size() >= 2
                    && diagnostic.args[0] == "private"
                    && diagnostic.args[1] == "Resource";
            }, "private selected drain constructor"));
}

TEST_CASE("Drain to value transfers an owned array exactly once",
          "[gen][drain][move][owner][regression]") {
    auto jit = gen_jit(R"K(
        module drain_owned_value;
        moves: int = 0;
        class Resource {
            data: int[]!;
        public:
            Resource() : data(new int[1]) { data[0] = 42; }
            Resource(other: const Resource&) -> delete;
            Resource(other: Resource#) : data(other.data) { ++moves; }
            ~Resource() {}
            const empty(): bool { return !data; }
            const get(): int { return data[0]; }
        }
        consume(value: Resource): int { return value.get(); }
        test(): int {
            source: Resource;
            result: int = consume(#source);
            if (!source.empty() || moves != 1) return -1;
            return result;
        }
    )K");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("Drain moves a class into a union alternative",
          "[gen][drain][union][move][regression]") {
    auto jit = gen_jit(R"K(
        module drain_union_move;
        releases: int = 0;
        moves: int = 0;
        class Resource {
        public:
            owns: int = 1;
            Resource() {}
            Resource(other: const Resource&) -> delete;
            Resource(other: Resource#) : owns(other.owns) { other.owns = 0; ++moves; }
            ~Resource() { if (owns != 0) ++releases; }
        }
        union Slot { empty: byte; value: Resource; }
        test(): int {
            releases = 0; moves = 0;
            {
                source: Resource;
                {
                    slot: Slot(#source);
                    if (source.owns != 0 || moves != 1 || releases != 0) return -1;
                }
                if (releases != 1) return -2;
            }
            return releases == 1 ? 42 : -3;
        }
    )K");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}


