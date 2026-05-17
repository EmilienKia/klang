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
 * Tests for compiler intrinsics:
 *   - @annotations::Intrinsic annotation detection
 *   - UniSlot<T> construct/destruct semantics
 */

#include <catch2/catch_all.hpp>
#include "helpers.hpp"

// Common K source fragment for the Intrinsic annotation + UniSlot template
// The annotation uses 'int' for the name field to avoid char[] RTTI layout issues in tests.
// The real stdlib version uses const char[]; the intrinsic matching is done by raw_name only.
static constexpr const char* UNISLOT_PREAMBLE = R"SRC(
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
            construct();

            @annotations::Intrinsic(2)
            destruct();

            get() : T& { return _slot; }
        }
)SRC";


// ════════════════════════════════════════════════════════════════════════════
//  1. Basic UniSlot with primitive type
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("UniSlot with primitive type — construct and get", "[gen][intrinsic]") {
    std::string src = std::string("module __intrinsic_01__;\n") + UNISLOT_PREAMBLE + R"SRC(

        test_int_slot() : int {
            slot : UniSlot<int>;
            slot.construct();
            slot.get() = 42;
            val : int = slot.get();
            slot.destruct();
            return val;
        }
    )SRC";
    auto jit = gen_jit(src);
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test_int_slot");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 42);
}


// ════════════════════════════════════════════════════════════════════════════
//  2. UniSlot does NOT auto-construct T
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("UniSlot does not auto-construct T member", "[gen][intrinsic]") {
    std::string src = std::string("module __intrinsic_02__;\n") + UNISLOT_PREAMBLE + R"SRC(

        gConstructed : int = 0;

        struct Widget {
            value : int;
            Widget() { gConstructed = 1; value = 99; }
        }

        test_no_auto_construct() : int {
            slot : UniSlot<Widget>;
            // Widget constructor should NOT have been called
            return gConstructed;
        }
    )SRC";
    auto jit = gen_jit(src);
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test_no_auto_construct");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 0);
}


// ════════════════════════════════════════════════════════════════════════════
//  3. UniSlot::construct invokes T's constructor
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("UniSlot construct invokes T constructor", "[gen][intrinsic]") {
    std::string src = std::string("module __intrinsic_03__;\n") + UNISLOT_PREAMBLE + R"SRC(

        gConstructed : int = 0;

        struct Widget {
            value : int;
            Widget() { gConstructed = gConstructed + 1; value = 77; }
        }

        test_explicit_construct() : int {
            slot : UniSlot<Widget>;
            slot.construct();
            return gConstructed;
        }
    )SRC";
    auto jit = gen_jit(src);
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test_explicit_construct");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 1);
}


// ════════════════════════════════════════════════════════════════════════════
//  4. UniSlot does NOT auto-destruct T
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("UniSlot does not auto-destruct T member", "[gen][intrinsic]") {
    std::string src = std::string("module __intrinsic_04__;\n") + UNISLOT_PREAMBLE + R"SRC(

        gDestructed : int = 0;

        struct Widget {
            value : int;
            Widget() { value = 55; }
            ~Widget() { gDestructed = gDestructed + 1; }
        }

        test_no_auto_destruct() : int {
            slot : UniSlot<Widget>;
            slot.construct();
            // Let slot go out of scope WITHOUT calling destruct()
            // Widget destructor should NOT be called by UniSlot's destructor
            return gDestructed;
        }
    )SRC";
    auto jit = gen_jit(src);
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test_no_auto_destruct");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 0);
}


// ════════════════════════════════════════════════════════════════════════════
//  5. UniSlot::destruct invokes T's destructor
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("UniSlot destruct invokes T destructor", "[gen][intrinsic]") {
    std::string src = std::string("module __intrinsic_05__;\n") + UNISLOT_PREAMBLE + R"SRC(

        gDestructed : int = 0;

        struct Widget {
            value : int;
            Widget() { value = 55; }
            ~Widget() { gDestructed = gDestructed + 1; }
        }

        test_explicit_destruct() : int {
            slot : UniSlot<Widget>;
            slot.construct();
            slot.destruct();
            return gDestructed;
        }
    )SRC";
    auto jit = gen_jit(src);
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test_explicit_destruct");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 1);
}


// ════════════════════════════════════════════════════════════════════════════
//  6. Full lifecycle: construct + use + destruct
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("UniSlot full lifecycle", "[gen][intrinsic]") {
    std::string src = std::string("module __intrinsic_06__;\n") + UNISLOT_PREAMBLE + R"SRC(

        struct Counter {
            count : int;
            Counter() { count = 10; }
            ~Counter() { count = 0; }
            increment() { count = count + 1; }
        }

        test_lifecycle() : int {
            slot : UniSlot<Counter>;
            slot.construct();
            slot.get().increment();
            slot.get().increment();
            slot.get().increment();
            result : int = slot.get().count;
            slot.destruct();
            return result;
        }
    )SRC";
    auto jit = gen_jit(src);
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test_lifecycle");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 13);
}


// ════════════════════════════════════════════════════════════════════════════
//  7. UniSlot construct with arguments (perfect forwarding fallback)
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("UniSlot construct with arguments", "[gen][intrinsic][forwarding]") {
    std::string src = std::string("module __intrinsic_07__;\n") + UNISLOT_PREAMBLE + R"SRC(

        struct Point {
            x : int;
            y : int;
            Point(px : int, py : int) { x = px; y = py; }
        }

        test_construct_args() : int {
            slot : UniSlot<Point>;
            slot.construct(10, 20);
            return slot.get().x + slot.get().y;
        }
    )SRC";
    auto jit = gen_jit(src);
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test_construct_args");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 30);
}


// ════════════════════════════════════════════════════════════════════════════
//  8. UniSlot construct with single argument
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("UniSlot construct with single argument", "[gen][intrinsic][forwarding]") {
    std::string src = std::string("module __intrinsic_08__;\n") + UNISLOT_PREAMBLE + R"SRC(

        struct Value {
            n : int;
            Value(v : int) { n = v; }
        }

        test_construct_one_arg() : int {
            slot : UniSlot<Value>;
            slot.construct(77);
            return slot.get().n;
        }
    )SRC";
    auto jit = gen_jit(src);
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test_construct_one_arg");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 77);
}
