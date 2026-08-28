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
 * Template argument deduction test suite.
 *
 * Tests that template function arguments can be deduced from call-site
 * argument types without explicit template argument specification.
 */
#include <catch2/catch_all.hpp>
#include "helpers.hpp"
TEST_CASE("template deduction - simple single param", "[gen][template-deduction]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_deduction_01;
        template<typename T>
        fun identity(a: T) : T {
            return a;
        }
        fun test_deduction() : int {
            return identity(42);
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_deduction");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}
TEST_CASE("template deduction - two different params", "[gen][template-deduction]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_deduction_02;
        template<typename T, typename U>
        fun first_of(a: T, b: U) : T {
            return a;
        }
        fun test_two() : int {
            return first_of(99, 3l);
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_two");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 99);
}
TEST_CASE("template deduction - same param used twice", "[gen][template-deduction]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_deduction_03;
        template<typename T>
        fun add_same(a: T, b: T) : T {
            return a + b;
        }
        fun test_same() : int {
            return add_same(20, 22);
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_same");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}
TEST_CASE("template deduction - pack deduction", "[gen][template-deduction]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_deduction_04;
        fun sum2(a: int, b: int) : int {
            return a + b;
        }
        template<typename... Ts>
        fun forward_sum(Ts... args) : int {
            return sum2(args...);
        }
        fun test_pack() : int {
            return forward_sum(20, 22);
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_pack");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}
TEST_CASE("template deduction - mixed param + pack", "[gen][template-deduction]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_deduction_05;
        fun combine(a: int, b: int, c: int) : int {
            return a + b + c;
        }
        template<typename T, typename... Ts>
        fun fwd_mixed(first: T, Ts... rest) : int {
            return combine(first, rest...);
        }
        fun test_mixed() : int {
            return fwd_mixed(10, 20, 12);
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_mixed");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}
TEST_CASE("template deduction - forwarding chain", "[gen][template-deduction]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_deduction_06;
        fun target(a: int, b: int) : int {
            return a * b;
        }
        template<typename... Ts>
        fun wrapper(Ts... args) : int {
            return target(args...);
        }
        template<typename... Ts>
        fun outer(Ts... args) : int {
            return wrapper(args...);
        }
        fun test_chain() : int {
            return outer(6, 7);
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_chain");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}
TEST_CASE("template deduction - empty pack", "[gen][template-deduction]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_deduction_07;
        fun no_args() : int {
            return 77;
        }
        template<typename... Ts>
        fun fwd_empty(Ts... args) : int {
            return no_args(args...);
        }
        fun test_empty() : int {
            return fwd_empty();
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_empty");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 77);
}
TEST_CASE("template deduction - prefers non-template exact match", "[gen][template-deduction]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_deduction_08;
        fun process(a: int) : int {
            return a + 1;
        }
        template<typename T>
        fun process(a: T) : T {
            return a;
        }
        fun test_prefer() : int {
            return process(41);
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_prefer");
    REQUIRE(fn != nullptr);
    // Non-template should be preferred: 41 + 1 = 42
    REQUIRE(fn() == 42);
}

TEST_CASE("template deduction - deduces when non-template has different arity", "[gen][template-deduction]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_deduction_09;
        // Non-template with 2 params (different arity than the call)
        fun work(a: int, b: int) : int {
            return a + b;
        }
        // Template with 1 param — should be deduced for single-arg calls
        template<typename T>
        fun work(a: T) : T {
            return a * 2;
        }
        fun test_deduced_different_arity() : int {
            return work(21);
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_deduced_different_arity");
    REQUIRE(fn != nullptr);
    // Template deduced with T=int: 21*2 = 42
    REQUIRE(fn() == 42);
}

TEST_CASE("template deduction - fails on type inconsistency", "[gen][template-deduction]") {
    // Same T deduced to int from first arg and long from second arg
    // should fail deduction, leaving no viable candidate → compilation error
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module gen_template_deduction_10;
        template<typename T>
        fun same_type(a: T, b: T) : T {
            return a + b;
        }
        fun test_fail() : int {
            return same_type(42, 99l);
        }
    )SRC"));
}


TEST_CASE("template deduction - overloaded targets with single type param", "[gen][template-deduction]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_deduction_11;

        // Overloaded target functions with different parameter types
        fun compute(a: int) : int {
            return a * 2;
        }

        fun compute(a: long) : long {
            return a * 3l;
        }

        fun compute(a: int, b: int) : int {
            return a + b;
        }

        fun compute(a: int, b: long) : long {
            return ((long)a) + b;
        }

        fun compute(a: long, b: long) : long {
            return a * b;
        }

        fun compute(a: int, b: int, c: int) : int {
            return a + b + c;
        }

        // Template forwarder with a single type param
        template<typename T>
        fun forward_one(x: T) : T {
            return compute(x);
        }

        // Template forwarder with two type params
        template<typename T, typename U>
        fun forward_two(x: T, y: U) : U {
            return compute(x, y);
        }

        // Template forwarder with three params (same type)
        template<typename T>
        fun forward_three_same(a: T, b: T, c: T) : T {
            return compute(a, b, c);
        }

        // --- Test functions ---

        // Deduce T=int, calls compute(int) -> 21*2 = 42
        fun test_one_int() : int {
            return forward_one(21);
        }

        // Deduce T=long, calls compute(long) -> 14*3 = 42
        fun test_one_long() : long {
            return forward_one(14l);
        }

        // Deduce T=int, U=int, calls compute(int,int) -> 20+22 = 42
        fun test_two_int_int() : int {
            return forward_two(20, 22);
        }

        // Deduce T=int, U=long, calls compute(int,long) -> 10+32 = 42
        fun test_two_int_long() : long {
            return forward_two(10, 32l);
        }

        // Deduce T=long, U=long, calls compute(long,long) -> 6*7 = 42
        fun test_two_long_long() : long {
            return forward_two(6l, 7l);
        }

        // Deduce T=int, calls compute(int,int,int) -> 10+20+12 = 42
        fun test_three_same() : int {
            return forward_three_same(10, 20, 12);
        }
    )SRC");

    SECTION("forward_one with int - deduces T=int") {
        auto fn = jit->lookup_symbol<int(*)()>("test_one_int");
        REQUIRE(fn != nullptr);
        REQUIRE(fn() == 42);
    }

    SECTION("forward_one with long - deduces T=long") {
        auto fn = jit->lookup_symbol<long(*)()>("test_one_long");
        REQUIRE(fn != nullptr);
        REQUIRE(fn() == 42l);
    }

    SECTION("forward_two with int,int - deduces T=int, U=int") {
        auto fn = jit->lookup_symbol<int(*)()>("test_two_int_int");
        REQUIRE(fn != nullptr);
        REQUIRE(fn() == 42);
    }

    SECTION("forward_two with int,long - deduces T=int, U=long") {
        auto fn = jit->lookup_symbol<long(*)()>("test_two_int_long");
        REQUIRE(fn != nullptr);
        REQUIRE(fn() == 42l);
    }

    SECTION("forward_two with long,long - deduces T=long, U=long") {
        auto fn = jit->lookup_symbol<long(*)()>("test_two_long_long");
        REQUIRE(fn != nullptr);
        REQUIRE(fn() == 42l);
    }

    SECTION("forward_three_same with int,int,int - deduces T=int") {
        auto fn = jit->lookup_symbol<int(*)()>("test_three_same");
        REQUIRE(fn != nullptr);
        REQUIRE(fn() == 42);
    }
}

TEST_CASE("template deduction - pack forwarding to distinct targets", "[gen][template-deduction]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_deduction_12;

        // Distinct target functions (no overloading)
        fun target_one(a: int) : int {
            return a * 2;
        }

        fun target_two(a: int, b: int) : int {
            return a + b;
        }

        fun target_three(a: int, b: int, c: int) : int {
            return a + b + c;
        }

        // Pack forwarders to each distinct target
        template<typename... Ts>
        fun fwd_one(Ts... args) : int {
            return target_one(args...);
        }

        template<typename... Ts>
        fun fwd_two(Ts... args) : int {
            return target_two(args...);
        }

        template<typename... Ts>
        fun fwd_three(Ts... args) : int {
            return target_three(args...);
        }

        // Mixed: one fixed param + pack
        template<typename T, typename... Ts>
        fun fwd_first_rest(first: T, Ts... rest) : int {
            return target_two(first, rest...);
        }

        template<typename T, typename... Ts>
        fun fwd_first_rest3(first: T, Ts... rest) : int {
            return target_three(first, rest...);
        }

        // --- Test functions ---

        fun test_fwd_one() : int {
            return fwd_one(21);
        }

        fun test_fwd_two() : int {
            return fwd_two(20, 22);
        }

        fun test_fwd_three() : int {
            return fwd_three(10, 20, 12);
        }

        fun test_fwd_first_rest() : int {
            return fwd_first_rest(30, 12);
        }

        fun test_fwd_first_rest3() : int {
            return fwd_first_rest3(10, 20, 12);
        }
    )SRC");

    SECTION("pack deduction with 1 arg") {
        auto fn = jit->lookup_symbol<int(*)()>("test_fwd_one");
        REQUIRE(fn != nullptr);
        REQUIRE(fn() == 42);
    }

    SECTION("pack deduction with 2 args") {
        auto fn = jit->lookup_symbol<int(*)()>("test_fwd_two");
        REQUIRE(fn != nullptr);
        REQUIRE(fn() == 42);
    }

    SECTION("pack deduction with 3 args") {
        auto fn = jit->lookup_symbol<int(*)()>("test_fwd_three");
        REQUIRE(fn != nullptr);
        REQUIRE(fn() == 42);
    }

    SECTION("fixed param + pack deduction with 2 args") {
        auto fn = jit->lookup_symbol<int(*)()>("test_fwd_first_rest");
        REQUIRE(fn != nullptr);
        REQUIRE(fn() == 42);
    }

    SECTION("fixed param + pack deduction with 3 args") {
        auto fn = jit->lookup_symbol<int(*)()>("test_fwd_first_rest3");
        REQUIRE(fn != nullptr);
        REQUIRE(fn() == 42);
    }
}

TEST_CASE("template deduction - composite template argument single type param", "[gen][template-deduction][composite]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_deduction_composite_01;
        template<typename T>
        struct Box {
            val: T;
        }
        template<typename T>
        unbox(b: Box<T>&) : T {
            return b.val;
        }
        test_composite() : int {
            b : Box<int>;
            b.val = 42;
            return unbox(b);
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_composite");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

TEST_CASE("template deduction - composite template argument multiple type params", "[gen][template-deduction][composite]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_deduction_composite_02;
        template<typename T, typename U>
        struct Pair {
            first: T;
            second: U;
        }
        template<typename T, typename U>
        get_second(p: Pair<T, U>&) : U {
            return p.second;
        }
        test_pair() : int {
            p : Pair<int, int>;
            p.first = 10;
            p.second = 42;
            return get_second(p);
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_pair");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

TEST_CASE("template deduction - nested composite template arguments", "[gen][template-deduction][composite]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_deduction_composite_03;
        template<typename T>
        struct Box {
            val: T;
        }
        template<typename T>
        unbox_nested(b: Box<Box<T>>&) : T {
            return b.val.val;
        }
        test_nested() : int {
            b : Box<Box<int>>;
            b.val.val = 42;
            return unbox_nested(b);
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_nested");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

TEST_CASE("template deduction - dependent composite return type", "[gen][template-deduction][composite]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_deduction_composite_04;
        template<typename T>
        struct Box {
            val: T;
        }
        template<typename T>
        make_box(val: T) : Box<T> {
            b : Box<T>;
            b.val = val;
            return b;
        }
        test_make_box() : int {
            b : Box<int> = make_box(42);
            return b.val;
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_make_box");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

TEST_CASE("template deduction - pointer indirection qualifier", "[gen][template-deduction][indirection]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_deduction_ptr_01;
        template<typename T>
        deref(p: T*) : T {
            return *p;
        }
        test_ptr() : int {
            x : int = 42;
            return deref(&x);
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_ptr");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

TEST_CASE("template deduction - member template method on non-template class", "[gen][template-deduction][member]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_deduction_member_01;
        class Helper {
        public:
            template<typename T>
            echo(x: T) : T {
                return x;
            }
        }
        test_member() : int {
            h : Helper;
            return h.echo(42);
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_member");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

TEST_CASE("template deduction - member template method on template class", "[gen][template-deduction][member]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_deduction_member_02;
        template<typename U>
        class Container {
        public:
            item: U;
            template<typename T>
            combine(x: T) : int {
                return (int)item + (int)x;
            }
        }
        test_container() : int {
            c : Container<int>;
            c.item = 20;
            return c.combine(22);
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_container");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

TEST_CASE("template deduction - static member template method", "[gen][template-deduction][static]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_deduction_static_01;
        class MathUtils {
        public:
            template<typename T>
            static double_it(x: T) : T {
                return x + x;
            }
        }
        test_static() : int {
            return MathUtils::double_it(21);
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_static");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

TEST_CASE("template deduction - callable argument deduction", "[gen][template-deduction][callable]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_deduction_callable_01;
        template<typename T, typename R>
        apply(x: T, fn: *(T):R) : R {
            return fn(x);
        }
        square(n: int) : int {
            return n * n;
        }
        test_callable() : int {
            fn : *(int):int = square;
            return apply(6, fn);
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_callable");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 36);
}

TEST_CASE("template deduction - fallback to template when non-template has incompatible type", "[gen][template-deduction][overload]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_deduction_overload_01;
        process(x: int) : int {
            return 1;
        }
        template<typename T>
        process(x: T) : int {
            return 2;
        }
        test_overload() : int {
            b : bool = true;
            return process(b);
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_overload");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 2);
}

TEST_CASE("template deduction - sized array deduction", "[gen][template-deduction][array]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_deduction_array_01;
        template<typename T>
        first_elem(arr: T[3]) : T {
            return arr[0];
        }
        test_arr() : int {
            a : int[3];
            a[0] = 42;
            return first_elem(a);
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_arr");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

TEST_CASE("template deduction - chained member method calls", "[gen][template-deduction][chained]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_deduction_chained_01;
        class Accumulator {
        public:
            sum: int;
            template<typename T>
            add(val: T) : Accumulator& {
                sum = sum + (int)val;
                return this;
            }
        }
        test_chain() : int {
            acc : Accumulator;
            acc.sum = 0;
            acc.add(10).add(20).add(12);
            return acc.sum;
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_chain");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

TEST_CASE("template deduction - const member template on const object", "[gen][template-deduction][const]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_deduction_const_01;
        class Inspector {
        public:
            base: int;
            template<typename T>
            const inspect(val: T) : int {
                return base + (int)val;
            }
        }
        test_const() : int {
            ins : Inspector;
            ins.base = 20;
            c_ins : const Inspector& = ins;
            return c_ins.inspect(22);
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_const");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

TEST_CASE("template deduction - type constraints on deduced argument", "[gen][template-deduction][constraints]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_deduction_constraints_01;
        class Animal {
        public:
            age: int;
        }
        class Dog : Animal {
        public:
            Dog() { age = 7; }
        }
        template<class T : Animal>
        get_age(pet: T&) : int {
            return pet.age;
        }
        test_constraint() : int {
            d : Dog;
            return get_age(d);
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_constraint");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 7);
}

// ═══════════════════════════════════════════════════════════════════════════
// Return-type-only template argument deduction (target-type inference / context deduction)
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("template deduction - return-type in variable definition", "[gen][template-deduction][return-type]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_deduction_ret_01;
        template<typename T>
        make_default() : T {
            x : T = (T) 42;
            return x;
        }
        test_var_def() : int {
            val : int = make_default();
            return val;
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_var_def");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

TEST_CASE("template deduction - return-type in return statement", "[gen][template-deduction][return-type]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_deduction_ret_02;
        template<typename T>
        make_val() : T {
            x : T = (T) 42;
            return x;
        }
        test_return_stmt() : int {
            return make_val();
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_return_stmt");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

TEST_CASE("template deduction - return-type in assignment", "[gen][template-deduction][return-type]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_deduction_ret_03;
        template<typename T>
        make_val() : T {
            x : T = (T) 42;
            return x;
        }
        test_assignment() : int {
            res : int = 100;
            res = make_val();
            return res;
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_assignment");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

TEST_CASE("template deduction - return-type in explicit cast", "[gen][template-deduction][return-type]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_deduction_ret_04;
        template<typename T>
        make_val() : T {
            x : T = (T) 42;
            return x;
        }
        test_cast() : int {
            return (int) make_val();
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_cast");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

TEST_CASE("template deduction - return-type in ternary branches", "[gen][template-deduction][return-type]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_deduction_ret_05;
        template<typename T>
        make_val() : T {
            x : T = (T) 42;
            return x;
        }
        test_ternary() : int {
            flag : bool = true;
            res : int = flag ? make_val() : 0;
            return res;
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_ternary");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

TEST_CASE("template deduction - mixed argument and return-type deduction", "[gen][template-deduction][return-type]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_deduction_ret_06;
        template<typename In, typename Out>
        convert_val(v: In) : Out {
            return (Out) v;
        }
        test_mixed() : long {
            res : long = convert_val(42);
            return res;
        }
    )SRC");
    auto fn = jit->lookup_symbol<long(*)()>("test_mixed");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42l);
}

TEST_CASE("template deduction - return-type pointer wrapper", "[gen][template-deduction][return-type]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_deduction_ret_07;
        g_val : int = 42;
        template<typename T>
        get_global_ptr() : T* {
            return &g_val;
        }
        test_ptr() : int {
            p : int* = get_global_ptr();
            return *p;
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_ptr");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

TEST_CASE("template deduction - return-type composite template", "[gen][template-deduction][return-type]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_deduction_ret_08;
        template<typename T>
        struct Box {
            val: T;
        }
        template<typename T>
        make_box() : Box<T> {
            b : Box<T>;
            b.val = (T) 42;
            return b;
        }
        test_box() : int {
            b : Box<int> = make_box();
            return b.val;
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_box");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

TEST_CASE("template deduction - return-type composite with value param", "[gen][template-deduction][return-type]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_deduction_ret_09;
        template<typename T, int N>
        struct FixedBuffer {
            val: T;
            get_size() : int { return N; }
        }
        template<typename T, int N>
        make_buf() : FixedBuffer<T, N> {
            buf : FixedBuffer<T, N>;
            buf.val = (T) 10;
            return buf;
        }
        test_buf() : int {
            b : FixedBuffer<int, 32> = make_buf();
            return b.val + b.get_size();
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_buf");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

TEST_CASE("template deduction - member template return-type deduction", "[gen][template-deduction][return-type]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_deduction_ret_10;
        struct Factory {
            template<typename T>
            create() : T {
                x : T = (T) 42;
                return x;
            }
        }
        test_member() : int {
            f : Factory;
            res : int = f.create();
            return res;
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_member");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

TEST_CASE("template deduction - return-type in function argument", "[gen][template-deduction][return-type]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_deduction_ret_11;
        template<typename T>
        make_zero() : T {
            x : T = (T) 0;
            return x;
        }
        consume(x: int) : int {
            return x + 42;
        }
        test_arg_context() : int {
            return consume(make_zero());
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_arg_context");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

TEST_CASE("template deduction - return-type in array element initialization", "[gen][template-deduction][return-type]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_deduction_ret_12;
        template<typename T>
        make_val() : T {
            x : T = (T) 21;
            return x;
        }
        test_array_init() : int {
            arr : int[2] { make_val(), make_val() };
            return arr[0] + arr[1];
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_array_init");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

TEST_CASE("template deduction - return-type in designated struct initialization", "[gen][template-deduction][return-type]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_deduction_ret_13;
        struct Point {
            x: int;
            y: int;
        }
        template<typename T>
        make_val() : T {
            val : T = (T) 21;
            return val;
        }
        test_struct_init() : int {
            pt : Point = Point{ .x = make_val(), .y = make_val() };
            return pt.x + pt.y;
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_struct_init");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

TEST_CASE("template deduction - return-type nested composite template", "[gen][template-deduction][return-type]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_deduction_ret_14;
        template<typename T>
        struct Box {
            val: T;
        }
        template<typename T, typename U>
        struct Pair {
            first: Box<T>;
            second: U;
        }
        template<typename T, typename U>
        make_pair() : Pair<T, U> {
            p : Pair<T, U>;
            p.first.val = (T) 10;
            p.second = (U) 32;
            return p;
        }
        test_nested() : int {
            p : Pair<int, int> = make_pair();
            return p.first.val + p.second;
        }
    )SRC");
    auto fn = jit->lookup_symbol<int(*)()>("test_nested");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42);
}

TEST_CASE("template deduction - fails when no target type context is available", "[gen][template-deduction][return-type]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module gen_template_deduction_ret_err_01;
        template<typename T>
        make_val() : T {
            x : T;
            return x;
        }
        test_no_context() {
            make_val();
        }
    )SRC"));
}


