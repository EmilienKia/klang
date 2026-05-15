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
 * Tests for member template functions:
 *   - template methods inside template structs (Step 1)
 *   - member template invocation with explicit template args (Step 2)
 *   - member template with parameter packs (Step 3)
 *   - UniSlot construct with variadic forwarding (Step 4)
 */

#include <catch2/catch_all.hpp>
#include "helpers.hpp"


// ════════════════════════════════════════════════════════════════════════════
//  1. Basic member template method on a non-template struct
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Member template method on non-template struct", "[gen][member-template]") {
    std::string src = R"SRC(
        module __mt_01__;

        struct Converter {
            template<typename T>
            identity(x : T) : T { return x; }
        }

        test_member_tpl() : int {
            c : Converter;
            return c.identity<int>(42);
        }
    )SRC";
    auto jit = gen_jit(src);
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test_member_tpl");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 42);
}


// ════════════════════════════════════════════════════════════════════════════
//  2. Member template method on a template struct
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Member template method on template struct", "[gen][member-template]") {
    std::string src = R"SRC(
        module __mt_02__;

        template<typename T>
        struct Container {
            _val : T;

            template<typename U>
            setFrom(u : U) : U { _val = u; return u; }
        }

        test_member_tpl_struct() : int {
            c : Container<int>;
            c.setFrom<int>(55);
            return c._val;
        }
    )SRC";
    auto jit = gen_jit(src);
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test_member_tpl_struct");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 55);
}


// ════════════════════════════════════════════════════════════════════════════
//  3. Member template with parameter pack
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Member template with parameter pack", "[gen][member-template][pack]") {
    std::string src = R"SRC(
        module __mt_03__;

        struct Adder {
            template<typename...Args>
            add(Args...args) : int { return sum(args...); }
        }

        sum(a : int, b : int) : int { return a + b; }

        test_member_pack() : int {
            adder : Adder;
            return adder.add<int, int>(17, 25);
        }
    )SRC";
    auto jit = gen_jit(src);
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test_member_pack");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 42);
}


// ════════════════════════════════════════════════════════════════════════════
//  4. UniSlot construct with explicit template args (variadic forwarding)
// ════════════════════════════════════════════════════════════════════════════

// Preamble with UniSlot using member template construct
static constexpr const char* UNISLOT_TPL_PREAMBLE = R"SRC(
        namespace annotations {
            annotation Intrinsic {
                name : int;
            }
        }

        template<typename T>
        struct UniSlot {
            private:
            _slot : T;

            public:
            @annotations::Intrinsic(0)
            UniSlot();

            @annotations::Intrinsic(0)
            ~UniSlot();

            @annotations::Intrinsic(1)
            template<typename...Args>
            construct(Args...args);

            @annotations::Intrinsic(2)
            destruct();

            get() : T& { return _slot; }
        }
)SRC";

TEST_CASE("UniSlot member template construct with args", "[gen][member-template][intrinsic]") {
    std::string src = std::string("module __mt_04__;\n") + UNISLOT_TPL_PREAMBLE + R"SRC(

        struct Point {
            x : int;
            y : int;
            Point(px : int, py : int) { x = px; y = py; }
        }

        test_unislot_tpl_construct() : int {
            slot : UniSlot<Point>;
            slot.construct<int, int>(10, 20);
            return slot.get().x + slot.get().y;
        }
    )SRC";
    auto jit = gen_jit(src);
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test_unislot_tpl_construct");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 30);
}


// ════════════════════════════════════════════════════════════════════════════
//  5. UniSlot member template construct zero-arg still works
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("UniSlot member template construct zero-arg", "[gen][member-template][intrinsic]") {
    std::string src = std::string("module __mt_05__;\n") + UNISLOT_TPL_PREAMBLE + R"SRC(

        struct Widget {
            value : int;
            Widget() { value = 99; }
        }

        test_unislot_tpl_zero() : int {
            slot : UniSlot<Widget>;
            slot.construct();
            return slot.get().value;
        }
    )SRC";
    auto jit = gen_jit(src);
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test_unislot_tpl_zero");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 99);
}


// ════════════════════════════════════════════════════════════════════════════
//  6. Member template deduction (implicit template args)
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Member template with implicit deduction", "[gen][member-template][deduction]") {
    std::string src = R"SRC(
        module __mt_06__;

        struct Wrapper {
            template<typename T>
            echo(x : T) : T { return x; }
        }

        test_member_deduction() : int {
            w : Wrapper;
            return w.echo(123);
        }
    )SRC";
    auto jit = gen_jit(src);
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test_member_deduction");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 123);
}

