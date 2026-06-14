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
// UniSlot::construct mirrors the real stdlib signature: a variadic member template
// `template<typename...Args> construct(Args...args)`. A non-variadic `construct()` mock
// would diverge from the real intrinsic and exercise an unsupported declaration shape.
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
            template<typename...Args>
            construct(Args...args);

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

// ════════════════════════════════════════════════════════════════════════════
//  MultiSlot<T> tests
// ════════════════════════════════════════════════════════════════════════════

static constexpr const char* MULTISLOT_PREAMBLE = R"SRC(
        namespace annotations {
            annotation Intrinsic {
                name : int;
            }
        }

        template<typename T>
        struct MultiSlot {
            private:
            _data : T*;
            _capacity : int;

            public:
            @annotations::Intrinsic(0)
            MultiSlot();

            @annotations::Intrinsic(0)
            ~MultiSlot();

            @annotations::Intrinsic(1)
            allocate(capacity : int);

            @annotations::Intrinsic(2)
            reallocate(newCapacity : int);

            @annotations::Intrinsic(3)
            deallocate();

            @annotations::Intrinsic(4)
            template<typename...Args>
            construct(index : int, Args...args);

            @annotations::Intrinsic(5)
            destruct(index : int);

            @annotations::Intrinsic(6)
            get(index : int) : T&;

            const getCapacity() : int { return _capacity; }
        }
)SRC";


TEST_CASE("MultiSlot<int> — allocate, construct, get, destruct, deallocate", "[gen][intrinsic][multislot]") {
    std::string src = std::string("module __ms_01__;\n") + MULTISLOT_PREAMBLE + R"SRC(

        test_multislot_int() : int {
            slots : MultiSlot<int>;
            slots.allocate(4);

            slots.get(0) = 10;
            slots.get(1) = 20;
            slots.get(2) = 30;
            slots.get(3) = 40;

            result : int = slots.get(0) + slots.get(1) + slots.get(2) + slots.get(3);
            slots.deallocate();
            return result;
        }
    )SRC";
    auto jit = gen_jit(src);
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test_multislot_int");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 100);
}

TEST_CASE("MultiSlot<Point> — construct with args, get, destruct", "[gen][intrinsic][multislot]") {
    std::string src = std::string("module __ms_02__;\n") + MULTISLOT_PREAMBLE + R"SRC(

        struct Point {
            x : int;
            y : int;
            Point() { x = 0; y = 0; }
            Point(ax : int, ay : int) { x = ax; y = ay; }
        }

        test_multislot_struct() : int {
            slots : MultiSlot<Point>;
            slots.allocate(3);

            slots.construct<int, int>(0, 10, 20);
            slots.construct<int, int>(1, 30, 40);
            slots.construct(2);

            result : int = slots.get(0).x + slots.get(0).y
                         + slots.get(1).x + slots.get(1).y
                         + slots.get(2).x;

            slots.destruct(0);
            slots.destruct(1);
            slots.destruct(2);
            slots.deallocate();
            return result;
        }
    )SRC";
    auto jit = gen_jit(src);
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test_multislot_struct");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 100);
}

TEST_CASE("MultiSlot — reallocate preserves existing data", "[gen][intrinsic][multislot]") {
    std::string src = std::string("module __ms_03__;\n") + MULTISLOT_PREAMBLE + R"SRC(

        test_multislot_realloc() : int {
            slots : MultiSlot<int>;
            slots.allocate(2);

            slots.get(0) = 42;
            slots.get(1) = 77;

            slots.reallocate(10);

            // Old data should still be accessible after realloc
            result : int = slots.get(0) + slots.get(1);
            slots.deallocate();
            return result;
        }
    )SRC";
    auto jit = gen_jit(src);
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test_multislot_realloc");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 119);
}

TEST_CASE("MultiSlot — getCapacity tracks allocations", "[gen][intrinsic][multislot]") {
    std::string src = std::string("module __ms_04__;\n") + MULTISLOT_PREAMBLE + R"SRC(

        test_multislot_capacity() : int {
            slots : MultiSlot<int>;
            result : int = 0;

            if (slots.getCapacity() == 0) result = result + 1;

            slots.allocate(5);
            if (slots.getCapacity() == 5) result = result + 10;

            slots.reallocate(20);
            if (slots.getCapacity() == 20) result = result + 100;

            slots.deallocate();
            if (slots.getCapacity() == 0) result = result + 1000;

            return result;
        }
    )SRC";
    auto jit = gen_jit(src);
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test_multislot_capacity");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 1111);
}

TEST_CASE("MultiSlot — multiple construct/destruct cycles", "[gen][intrinsic][multislot]") {
    std::string src = std::string("module __ms_05__;\n") + MULTISLOT_PREAMBLE + R"SRC(

        struct Counter {
            value : int;
            Counter() { value = 1; }
            ~Counter() { }
        }

        test_multislot_cycles() : int {
            slots : MultiSlot<Counter>;
            slots.allocate(3);

            // First cycle
            slots.construct(0);
            slots.construct(1);
            slots.construct(2);
            r1 : int = slots.get(0).value + slots.get(1).value + slots.get(2).value;
            slots.destruct(0);
            slots.destruct(1);
            slots.destruct(2);

            // Second cycle — re-use same indices
            slots.construct(0);
            slots.construct(1);
            r2 : int = slots.get(0).value + slots.get(1).value;
            slots.destruct(0);
            slots.destruct(1);

            slots.deallocate();
            return r1 + r2;
        }
    )SRC";
    auto jit = gen_jit(src);
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test_multislot_cycles");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 5);
}

TEST_CASE("MultiSlot — destructor frees memory (no crash)", "[gen][intrinsic][multislot]") {
    std::string src = std::string("module __ms_06__;\n") + MULTISLOT_PREAMBLE + R"SRC(

        test_multislot_dtor() : int {
            slots : MultiSlot<int>;
            slots.allocate(100);
            slots.get(0) = 42;
            // Let destructor handle free via scope exit
            return slots.get(0);
        }
    )SRC";
    auto jit = gen_jit(src);
    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("test_multislot_dtor");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 42);
}

