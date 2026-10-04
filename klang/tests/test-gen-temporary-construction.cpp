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
 * Tests for temporary anonymous object construction in expressions.
 *
 * These tests verify that `S(args...)` can be used as a sub-expression:
 *  - As a function argument: `consume(Point(1,2))`
 *  - In a return statement: `return Pair(x, y);`
 *  - Default construction (no args): `consume(Counter())`
 *  - Destructor cleanup of temporaries
 *  - Multiple temporaries in one expression (reverse destruction order)
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

TEST_CASE("Return prvalue: adopting resources preserve one destination lifetime",
          "[gen][temporary_ctor][return-prvalue][rvo][lifecycle][regression]") {
    const bool is_class = GENERATE(false, true);
    const bool templated = GENERATE(false, true);
    const bool optimize = GENERATE(false, true);
    INFO("class=" << is_class << " template=" << templated << " optimize=" << optimize);
    std::string source = R"K(
        module return_prvalue_resource;
        struct State {
            refs : int = 1;
            adoptions : int = 0;
            copies : int = 0;
            drops : int = 0;
            invalid : int = 0;
        }
        @DECL@ Resource {
        private:
            state : State*;
        public:
            Resource(value : State*) : state(value) { ++state->adoptions; }
            Resource(other : const @SELF@&) : state(other.state) {
                ++state->refs;
                ++state->copies;
            }
            ~Resource() {
                if (state->refs <= 0) ++state->invalid;
                --state->refs;
                ++state->drops;
            }
            const refs() : int { return state->refs; }
            static decodeOpen(value : State*) : @SELF@ {
                return @SELF@(value);
            }
        }
        make(value : State*) : @RESOURCE@ { return @RESOURCE@(value); }
        forward(value : State*) : @RESOURCE@ { return make(value); }
        consume(value : @RESOURCE@) : int { return value.refs(); }
        struct Carrier {
            stored : @RESOURCE@;
            Carrier(value : const @RESOURCE@&) : stored(value) {}
            get() : @RESOURCE@ { return stored; }
        }
        counts(state : const State&) : int {
            if (state.refs != 0 || state.adoptions != 1 || state.invalid != 0) return -10;
            return state.refs * 10000 + state.copies * 1000 + state.drops * 100
                + state.adoptions * 10 + state.invalid;
        }
        direct() : int {
            state : State;
            {
                value : @RESOURCE@ = make(&state);
                if (state.refs != 1 || state.copies != 0 || state.drops != 0) return -1;
            }
            return counts(state);
        }
        decoder() : int {
            state : State;
            {
                value : @RESOURCE@ = @RESOURCE@::decodeOpen(&state);
                if (state.refs != 1 || state.copies != 0 || state.drops != 0) return -2;
            }
            return counts(state);
        }
        forwarded() : int {
            state : State;
            {
                value : @RESOURCE@ = forward(&state);
                if (state.refs != 1 || state.copies != 0 || state.drops != 0) return -3;
            }
            return counts(state);
        }
        copied() : int {
            state : State;
            {
                value : @RESOURCE@ = make(&state);
                if (consume(value) != 2 || state.refs != 1 || state.copies != 1) return -4;
                {
                    carrier : Carrier(value);
                    if (state.refs != 2 || state.copies != 2) return -5;
                    {
                        later : @RESOURCE@ = carrier.get();
                        if (later.refs() != 3 || state.copies != 3) return -6;
                    }
                    if (state.refs != 2) return -7;
                }
                if (state.refs != 1) return -8;
            }
            return counts(state);
        }
        intermediate() : int {
            state : State;
            {
                value : @RESOURCE@ = Carrier(make(&state)).get();
                if (state.refs != 1 || state.copies != 2 || state.drops != 2) return -9;
            }
            return counts(state);
        }
    )K";
    const auto replace = [&](const std::string& token, const std::string& value) {
        size_t pos = 0;
        while ((pos = source.find(token, pos)) != std::string::npos) {
            source.replace(pos, token.size(), value);
            pos += value.size();
        }
    };
    replace("@DECL@", std::string(templated ? "template<typename T> " : "")
        + (is_class ? "class" : "struct"));
    replace("@SELF@", templated ? "Resource<T>" : "Resource");
    replace("@RESOURCE@", templated ? "Resource<int>" : "Resource");
    auto jit = gen_jit(source, false, optimize);
    REQUIRE(jit);
    for (const auto* name : {"direct", "decoder", "forwarded", "copied", "intermediate"}) {
        INFO(name);
        auto run = jit->lookup_symbol<int (*)()>(name);
        REQUIRE(run);
        int result = -100;
        REQUIRE_NOTHROW(result = run());
        CHECK(result == (std::string_view(name) == "copied" ? 3410
            : std::string_view(name) == "intermediate" ? 2310 : 110));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Basic: temporary struct as function argument
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Temporary construction: struct as function argument", "[gen][temporary_ctor]") {
    auto jit = gen_jit(R"SRC(
module gen_temporary_construction_01;

struct Point {
    x : int;
    y : int;
    Point(ax : int, ay : int) : x(ax), y(ay) {}
}

sum_point(p : Point&) : int {
    return p.x + p.y;
}

test() : int {
    return sum_point(Point(3, 4));
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn);
    CHECK(test_fn() == 7);
}

// ─────────────────────────────────────────────────────────────────────────────
// Temporary struct in return statement
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Temporary construction: struct in return statement", "[gen][temporary_ctor]") {
    auto jit = gen_jit(R"SRC(
module gen_temporary_construction_02;

struct Pair {
    a : int;
    b : int;
    Pair(x : int, y : int) : a(x), b(y) {}
}

make_pair(x : int, y : int) : Pair {
    return Pair(x, y);
}

test() : int {
    p : Pair = make_pair(10, 20);
    return p.a + p.b;
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn);
    CHECK(test_fn() == 30);
}

// ─────────────────────────────────────────────────────────────────────────────
// Default construction (no args)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Temporary construction: default ctor (no args)", "[gen][temporary_ctor]") {
    auto jit = gen_jit(R"SRC(
module gen_temporary_construction_03;

struct Counter {
    val : int;
    Counter() : val(42) {}
}

get_val(c : Counter&) : int {
    return c.val;
}

test() : int {
    return get_val(Counter());
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn);
    CHECK(test_fn() == 42);
}

// ─────────────────────────────────────────────────────────────────────────────
// Destructor cleanup of temporaries
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Temporary construction: destructor called", "[gen][temporary_ctor]") {
    auto jit = gen_jit(R"SRC(
module gen_temporary_construction_04;

g_dtors : int = 0;

struct Tracked {
    val : int;
    Tracked(v : int) : val(v) {}
    ~Tracked() {
        ++g_dtors;
    }
}

consume(t : Tracked&) : int {
    return t.val;
}

test() : int {
    g_dtors = 0;
    r : int = consume(Tracked(7));
    // After the full expression, the temporary should have been destroyed.
    return g_dtors;
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn);
    CHECK(test_fn() == 1);
}

// ─────────────────────────────────────────────────────────────────────────────
// Multiple temporaries in one expression (two args)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Temporary construction: two temporaries as args", "[gen][temporary_ctor]") {
    auto jit = gen_jit(R"SRC(
module gen_temporary_construction_05;

struct Val {
    n : int;
    Val(v : int) : n(v) {}
}

add_vals(a : Val&, b : Val&) : int {
    return a.n + b.n;
}

test() : int {
    return add_vals(Val(10), Val(20));
}
)SRC");
    REQUIRE(jit);
    auto test_fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test_fn);
    CHECK(test_fn() == 30);
}

// ─────────────────────────────────────────────────────────────────────────────
// Temporary construction must fail for abstract classes
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Temporary construction: abstract class rejected", "[gen][temporary_ctor]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
module gen_temporary_construction_06;

abstract class Base {
    Base() {}
    abstract value() : int;
}

consume(b : Base&) : int {
    return b.value();
}

test() : int {
    return consume(Base());
}
)SRC"));
}
