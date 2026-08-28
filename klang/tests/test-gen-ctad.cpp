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
 * Class Template Argument Deduction (CTAD) test suite.
 *
 * Tests that class/struct template arguments can be deduced automatically
 * from constructor arguments for temporary constructions, new expressions,
 * and variable definitions.
 */

#include <catch2/catch_all.hpp>
#include "helpers.hpp"

TEST_CASE("ctad - basic struct temporary construction", "[gen][ctad]") {
    auto jit = gen_jit(R"SRC(
        module gen_ctad_01;

        template<typename T, typename U>
        struct Pair {
            first  : T;
            second : U;

            Pair(first : T, second : U) : first(first), second(second) {}

            sum() : int {
                return (int)first + (int)second;
            }
        }

        test_pair_temp() : int {
            return Pair(10, 32).sum();
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_pair_temp");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

TEST_CASE("ctad - class template with method chaining", "[gen][ctad]") {
    auto jit = gen_jit(R"SRC(
        module gen_ctad_02;

        template<typename T>
        class Box {
            val : T;

            Box(val : T) : val(val) {}

            get() : T {
                return val;
            }
        }

        test_box_temp() : int {
            return Box(123).get();
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_box_temp");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 123);
}

TEST_CASE("ctad - triple with mixed types", "[gen][ctad]") {
    auto jit = gen_jit(R"SRC(
        module gen_ctad_03;

        template<typename T, typename U, typename V>
        struct Triple {
            a : T;
            b : U;
            c : V;

            Triple(a : T, b : U, c : V) : a(a), b(b), c(c) {}

            total() : int {
                return (int)a + (int)b + (int)c;
            }
        }

        test_triple() : int {
            return Triple(10, 20l, 12).total();
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_triple");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

TEST_CASE("ctad - dynamic allocation with new", "[gen][ctad]") {
    auto jit = gen_jit(R"SRC(
        module gen_ctad_04;

        template<typename T, typename U>
        class Pair {
            first  : T;
            second : U;

            Pair(first : T, second : U) : first(first), second(second) {}

            diff() : int {
                return (int)first - (int)second;
            }
        }

        test_new_ctad() : int {
            p : Pair<int, int>! = new Pair(100, 58);
            return p.diff();
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_new_ctad");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

TEST_CASE("ctad - variable definition with constructor syntax", "[gen][ctad]") {
    auto jit = gen_jit(R"SRC(
        module gen_ctad_05;

        template<typename T, typename U>
        struct Pair {
            first  : T;
            second : U;

            Pair(first : T, second : U) : first(first), second(second) {}

            mult() : int {
                return (int)first * (int)second;
            }
        }

        test_var_ctor() : int {
            p : Pair(6, 7);
            return p.mult();
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_var_ctor");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

TEST_CASE("ctad - variable definition with assignment from temporary", "[gen][ctad]") {
    auto jit = gen_jit(R"SRC(
        module gen_ctad_06;

        template<typename T>
        struct Holder {
            val : T;

            Holder(val : T) : val(val) {}
        }

        test_var_assign() : int {
            h : Holder = Holder(77);
            return h.val;
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_var_assign");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 77);
}

TEST_CASE("ctad - owner variable definition with new", "[gen][ctad]") {
    auto jit = gen_jit(R"SRC(
        module gen_ctad_07;

        template<typename T, typename U>
        class Item {
            x : T;
            y : U;

            Item(x : T, y : U) : x(x), y(y) {}

            get_sum() : int {
                return (int)x + (int)y;
            }
        }

        test_owner_var() : int {
            it : Item! = new Item(15, 27);
            return it.get_sum();
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_owner_var");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

TEST_CASE("ctad - implicit copy deduction guide", "[gen][ctad]") {
    auto jit = gen_jit(R"SRC(
        module gen_ctad_08;

        template<typename T, typename U>
        struct Pair {
            first  : T;
            second : U;

            Pair(first : T, second : U) : first(first), second(second) {}
        }

        test_copy_guide() : int {
            p1 : Pair<int, int> = Pair(20, 22);
            p2 : Pair = Pair(p1);
            return p2.first + p2.second;
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_copy_guide");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

TEST_CASE("ctad - overloaded constructors", "[gen][ctad]") {
    auto jit = gen_jit(R"SRC(
        module gen_ctad_09;

        template<typename T>
        struct MultiCtor {
            val   : T;
            extra : int;

            MultiCtor(val : T) : val(val), extra(100) {}
            MultiCtor(val : T, extra : int) : val(val), extra(extra) {}

            calc() : int {
                return (int)val + extra;
            }
        }

        test_overload1() : int {
            return MultiCtor(42).calc();
        }

        test_overload2() : int {
            return MultiCtor(40, 2).calc();
        }
    )SRC");
    auto fn1 = jit->lookup_symbol<int(*)()>("test_overload1");
    REQUIRE(fn1 != nullptr);
    REQUIRE(fn1() == 142);

    auto fn2 = jit->lookup_symbol<int(*)()>("test_overload2");
    REQUIRE(fn2 != nullptr);
    REQUIRE(fn2() == 42);
}

TEST_CASE("ctad - nested composite template deduction", "[gen][ctad]") {
    auto jit = gen_jit(R"SRC(
        module gen_ctad_10;

        template<typename T, typename U>
        struct Pair {
            first  : T;
            second : U;

            Pair(first : T, second : U) : first(first), second(second) {}
        }

        test_nested_ctad() : int {
            nested : Pair = Pair(Pair(10, 20), 12);
            return nested.first.first + nested.first.second + nested.second;
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_nested_ctad");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

TEST_CASE("ctad - namespace-qualified template name", "[gen][ctad]") {
    auto jit = gen_jit(R"SRC(
        module gen_ctad_11;

        namespace geo {
            template<typename T>
            struct Point {
                x : T;
                y : T;

                Point(x : T, y : T) : x(x), y(y) {}

                norm_squared() : int {
                    return (int)(x * x + y * y);
                }
            }
        }

        test_qualified_ctad() : int {
            pt : geo::Point = geo::Point(3, 4);
            return pt.norm_squared();
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_qualified_ctad");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 25);
}

TEST_CASE("ctad - function argument passing", "[gen][ctad]") {
    auto jit = gen_jit(R"SRC(
        module gen_ctad_12;

        template<typename T>
        struct Box {
            val : T;
            Box(val : T) : val(val) {}
        }

        unbox_int(b : const Box<int>&) : int {
            return b.val;
        }

        test_pass_ctad() : int {
            return unbox_int(Box(42));
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_pass_ctad");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

TEST_CASE("ctad - default template arguments", "[gen][ctad]") {
    auto jit = gen_jit(R"SRC(
        module gen_ctad_13;

        template<typename T = int, typename U = int>
        struct DefaultedPair {
            first  : T;
            second : U;

            DefaultedPair(first : T) : first(first), second(100) {}
            DefaultedPair() : first(10), second(20) {}
        }

        test_default_args1() : int {
            p1 : DefaultedPair = DefaultedPair(5);
            return p1.first + p1.second;
        }

        test_default_args2() : int {
            p2 : DefaultedPair = DefaultedPair();
            return p2.first + p2.second;
        }
    )SRC");
    auto fn1 = jit->lookup_symbol<int(*)()>("test_default_args1");
    REQUIRE(fn1 != nullptr);
    REQUIRE(fn1() == 105);

    auto fn2 = jit->lookup_symbol<int(*)()>("test_default_args2");
    REQUIRE(fn2 != nullptr);
    REQUIRE(fn2() == 30);
}

TEST_CASE("ctad - uniform array initialization", "[gen][ctad]") {
    auto jit = gen_jit(R"SRC(
        module gen_ctad_14;

        template<typename T, typename U>
        struct Pair {
            first  : T;
            second : U;

            Pair(first : T, second : U) : first(first), second(second) {}
        }

        test_uniform_array_ctad() : int {
            arr : Pair(10, 20)[3];
            return (int)arr[0].first + (int)arr[2].second;
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_uniform_array_ctad");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 30);
}

TEST_CASE("ctad - map entry struct", "[gen][ctad]") {
    auto jit = gen_jit(R"SRC(
        module gen_ctad_15;

        template<typename K, typename V>
        struct Entry {
            key : K;
            value : V;

            Entry(key : K, value : V) : key(key), value(value) {}

            get_value() : V {
                return value;
            }
        }

        test_entry_ctad() : int {
            e : Entry = Entry(10, 42);
            return e.get_value();
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_entry_ctad");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

TEST_CASE("ctad - negative test deduction failure on arity mismatch", "[gen][ctad]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module gen_ctad_18;

        template<typename T, typename U>
        struct Pair {
            first  : T;
            second : U;

            Pair(first : T, second : U) : first(first), second(second) {}
        }

        test_bad() : int {
            p : Pair = Pair(1, 2, 3);
            return 0;
        }
    )SRC"));
}

TEST_CASE("ctad - composite with value parameter", "[gen][ctad]") {
    auto jit = gen_jit(R"SRC(
        module gen_ctad_16;

        template<typename T, int N>
        struct FixedBuffer {
            val : T;
            FixedBuffer(val : T) : val(val) {}
        }

        template<typename T, int N>
        struct BufferWrapper {
            buf : FixedBuffer<T, N>;
            BufferWrapper(buf : const FixedBuffer<T, N>&) : buf(buf) {}

            get_size_mult() : int {
                return (int)buf.val * N;
            }
        }

        test_val_param_ctad() : int {
            b : FixedBuffer<int, 6> = FixedBuffer<int, 6>(7);
            w : BufferWrapper = BufferWrapper(b);
            return w.get_size_mult();
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_val_param_ctad");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

TEST_CASE("ctad - constraint check positive and negative", "[gen][ctad]") {
    auto jit = gen_jit(R"SRC(
        module gen_ctad_17;

        struct Animal {
            public weight : int;
            Animal(w : int) {
                weight = w;
            }
            Animal() {
                weight = 0;
            }
        }

        struct Dog : public Animal {
            Dog(w : int) : Animal(w) {}
            Dog() : Animal() {}
        }

        template<struct T : Animal>
        struct Cage {
            occupant : T;
            Cage(occupant : T) : occupant(occupant) {}

            get_weight() : int {
                return occupant.weight;
            }
        }

        test_constraint_ctad() : int {
            c : Cage = Cage(Dog(25));
            return c.get_weight();
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_constraint_ctad");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 25);
}

