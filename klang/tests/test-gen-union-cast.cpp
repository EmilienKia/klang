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
#include <catch2/catch_all.hpp>
#include "helpers.hpp"

TEST_CASE("Union cast: explicit cast by value and reference", "[gen][union][cast]") {
    auto jit = gen_jit(R"SRC(
        module gen_union_cast_01;
        union NumVal {
            i: int;
            d: double;
        }

        test_cast_val() : int {
            u : NumVal;
            u.i = 42;
            x : int = (int) u;
            return x;
        }

        test_cast_ref() : int {
            u : NumVal;
            u.i = 50;
            (int&) u = 100;
            return u.i;
        }

        test_cast_const_ref() : int {
            u : NumVal;
            u.i = 77;
            cr : const int& = (const int&) u;
            return cr;
        }
    )SRC");
    REQUIRE(jit);
    auto f_val = jit->lookup_symbol<int(*)()>("test_cast_val");
    auto f_ref = jit->lookup_symbol<int(*)()>("test_cast_ref");
    auto f_cr  = jit->lookup_symbol<int(*)()>("test_cast_const_ref");
    REQUIRE(f_val); REQUIRE(f_ref); REQUIRE(f_cr);
    REQUIRE(f_val() == 42);
    REQUIRE(f_ref() == 100);
    REQUIRE(f_cr() == 77);
}

TEST_CASE("Union cast: implicit adaptation in var init, assign, and param passing", "[gen][union][cast]") {
    auto jit = gen_jit(R"SRC(
        module gen_union_cast_02;
        union Value {
            i: int;
            d: double;
        }

        take_int(x: int) : int {
            return x * 2;
        }

        take_ref(r: int&) : int {
            r = r + 5;
            return r;
        }

        test_init() : int {
            u : Value;
            u.i = 10;
            x : int = u;
            return x;
        }

        test_assign() : int {
            u : Value;
            u.i = 20;
            x : int = 0;
            x = u;
            return x;
        }

        test_param_val() : int {
            u : Value;
            u.i = 15;
            return take_int(u);
        }

        test_param_ref() : int {
            u : Value;
            u.i = 30;
            res : int = take_ref(u);
            return u.i;
        }
    )SRC");
    REQUIRE(jit);
    auto f_init = jit->lookup_symbol<int(*)()>("test_init");
    auto f_asgn = jit->lookup_symbol<int(*)()>("test_assign");
    auto f_pval = jit->lookup_symbol<int(*)()>("test_param_val");
    auto f_pref = jit->lookup_symbol<int(*)()>("test_param_ref");
    REQUIRE(f_init); REQUIRE(f_asgn); REQUIRE(f_pval); REQUIRE(f_pref);
    REQUIRE(f_init() == 10);
    REQUIRE(f_asgn() == 20);
    REQUIRE(f_pval() == 30);
    REQUIRE(f_pref() == 35);
}

TEST_CASE("Union cast: mismatch traps in normal execution", "[gen][union][cast]") {
    auto result = build_and_exec(R"SRC(
        module gen_union_cast_03;
        union Value {
            i: int;
            d: double;
        }

        main() : int {
            u : Value;
            u.d = 3.14;
            x : int = (int) u; // should trap on mismatch
            return x;
        }
    )SRC");
    REQUIRE(result.exit_code != 0);
}

TEST_CASE("Union cast: soft-fail in if conditions", "[gen][union][cast][if]") {
    auto jit = gen_jit(R"SRC(
        module gen_union_cast_04;
        union Value {
            i: int;
            d: double;
        }

        test_if_hit() : int {
            u : Value;
            u.i = 99;
            if (x : int = u) {
                return x;
            } else {
                return -1;
            }
        }

        test_if_miss() : int {
            u : Value;
            u.d = 2.71;
            if (x : int = u) {
                return x;
            } else {
                return 42;
            }
        }

        test_if_ref_miss() : int {
            u : Value;
            u.d = 2.71;
            if (r : int& = u) {
                return r;
            } else {
                return 88;
            }
        }
    )SRC");
    REQUIRE(jit);
    auto f_hit  = jit->lookup_symbol<int(*)()>("test_if_hit");
    auto f_miss = jit->lookup_symbol<int(*)()>("test_if_miss");
    auto f_rmis = jit->lookup_symbol<int(*)()>("test_if_ref_miss");
    REQUIRE(f_hit); REQUIRE(f_miss); REQUIRE(f_rmis);
    REQUIRE(f_hit() == 99);
    REQUIRE(f_miss() == 42);
    REQUIRE(f_rmis() == 88);
}

TEST_CASE("Union cast: &u addresser conversions and null policy", "[gen][union][cast][addresser]") {
    auto jit = gen_jit(R"SRC(
        module gen_union_cast_05;
        union Value {
            i: int;
            d: double;
        }

        test_ptr_hit() : int {
            u : Value;
            u.i = 123;
            p : int* = &u;
            if (p != null) {
                return *p;
            }
            return -1;
        }

        test_ptr_miss() : int {
            u : Value;
            u.d = 3.14;
            p : int* = &u;
            if (p == null) {
                return 1;
            }
            return 0;
        }

        test_view_hit() : int {
            u : Value;
            u.i = 456;
            v : int? = &u;
            if (v != null) {
                return *v;
            }
            return -1;
        }

        test_view_miss() : int {
            u : Value;
            u.d = 1.23;
            v : int? = &u;
            if (v == null) {
                return 1;
            }
            return 0;
        }

        test_if_ptr_cond() : int {
            u : Value;
            u.d = 9.99;
            if (p : int* = &u) {
                return *p;
            } else {
                return 777;
            }
        }

        test_link_hit() : int {
            u : Value;
            u.i = 321;
            l : int+ = &u;
            return *l;
        }
    )SRC");
    REQUIRE(jit);
    auto f_phit = jit->lookup_symbol<int(*)()>("test_ptr_hit");
    auto f_pmis = jit->lookup_symbol<int(*)()>("test_ptr_miss");
    auto f_vhit = jit->lookup_symbol<int(*)()>("test_view_hit");
    auto f_vmis = jit->lookup_symbol<int(*)()>("test_view_miss");
    auto f_ifp  = jit->lookup_symbol<int(*)()>("test_if_ptr_cond");
    auto f_lhit = jit->lookup_symbol<int(*)()>("test_link_hit");
    REQUIRE(f_phit); REQUIRE(f_pmis); REQUIRE(f_vhit); REQUIRE(f_vmis); REQUIRE(f_ifp); REQUIRE(f_lhit);
    REQUIRE(f_phit() == 123);
    REQUIRE(f_pmis() == 1);
    REQUIRE(f_vhit() == 456);
    REQUIRE(f_vmis() == 1);
    REQUIRE(f_ifp() == 777);
    REQUIRE(f_lhit() == 321);
}

TEST_CASE("Union cast: polymorphic union to base and ancestor references / addressers", "[gen][union][cast][polymorphic]") {
    auto jit = gen_jit(R"SRC(
        module gen_union_cast_06;
        abstract class Animal {
        public:
            abstract sound() : int;
        }
        class Dog : Animal {
        public:
            override sound() : int { return 10; }
        }
        class Cat : Animal {
        public:
            override sound() : int { return 20; }
        }
        union AnyAnimal : Animal {
            dog: Dog;
            cat: Cat;
        }

        call_animal_ref(a: Animal&) : int {
            return a.sound();
        }

        test_poly_base_ref_dog() : int {
            u : AnyAnimal;
            u.dog = Dog();
            a : Animal& = u;
            return a.sound();
        }

        test_poly_base_ref_cat() : int {
            u : AnyAnimal;
            u.cat = Cat();
            a : Animal& = u;
            return a.sound();
        }

        test_poly_param() : int {
            u : AnyAnimal;
            u.cat = Cat();
            return call_animal_ref(u);
        }

        test_poly_ptr() : int {
            u : AnyAnimal;
            u.dog = Dog();
            p : Animal* = &u;
            if (p != null) {
                return p->sound();
            }
            return -1;
        }

        test_specific_alt_hit() : int {
            u : AnyAnimal;
            u.dog = Dog();
            d : Dog& = u;
            return d.sound();
        }
    )SRC");
    REQUIRE(jit);
    auto f_dog = jit->lookup_symbol<int(*)()>("test_poly_base_ref_dog");
    auto f_cat = jit->lookup_symbol<int(*)()>("test_poly_base_ref_cat");
    auto f_par = jit->lookup_symbol<int(*)()>("test_poly_param");
    auto f_ptr = jit->lookup_symbol<int(*)()>("test_poly_ptr");
    auto f_spe = jit->lookup_symbol<int(*)()>("test_specific_alt_hit");
    REQUIRE(f_dog); REQUIRE(f_cat); REQUIRE(f_par); REQUIRE(f_ptr); REQUIRE(f_spe);
    REQUIRE(f_dog() == 10);
    REQUIRE(f_cat() == 20);
    REQUIRE(f_par() == 20);
    REQUIRE(f_ptr() == 10);
    REQUIRE(f_spe() == 10);
}

TEST_CASE("Union cast: compile errors for invalid casts", "[gen][union][cast][error]") {
    SECTION("Cast to non-existent alternative type") {
        REQUIRE(compile_should_fail(R"SRC(
            module gen_union_cast_err_01;
            union Value {
                i: int;
                d: double;
            }
            bad(u: Value&) : void {
                s : byte = (byte) u;
            }
        )SRC", nullptr));
    }

    SECTION("Cast when multiple alternatives share the same type (ambiguous)") {
        REQUIRE(compile_should_fail(R"SRC(
            module gen_union_cast_err_02;
            union Ambiguous {
                a: int;
                b: int;
            }
            bad(u: Ambiguous&) : void {
                x : int = (int) u;
            }
        )SRC", nullptr));
    }
}
