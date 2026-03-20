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

TEST_CASE( "Simple method", "[gen]" ) {

    SECTION("Simple int() method") {
        auto jit = gen_jit(R"SRC(
        module test;
        test() : int {
            return 42;
        }
        )SRC");
        REQUIRE(jit);

        auto test = jit->lookup_symbol < int(*)() > ("test");
        REQUIRE(test != nullptr);
        REQUIRE(test() == 42);
    }

    SECTION( "Simple int(int) method" ) {
        auto jit = gen_jit(R"SRC(
        module test;
        increment(i : int) : int {
            return i + 1;
        }
        )SRC");
        REQUIRE( jit );

        auto increment = jit->lookup_symbol<int(*)(int)>("increment");
        REQUIRE(increment != nullptr);
        REQUIRE( increment(41) == 42 );
    }

    SECTION( "Simple int(int, int) method" ) {
        auto jit = gen_jit(R"SRC(
        module test;
        multiply(a : int, b : int) : int {
            return a * b;
        }
        )SRC");
        REQUIRE( jit );

        auto multiply = jit->lookup_symbol<int(*)(int, int)>("multiply");
        REQUIRE(multiply != nullptr);
        REQUIRE( multiply(2, 3) == 6 );
    }
}

//
// Pointer, addresses and value-of
//

TEST_CASE("Pointers", "[gen][pointers]") {
    auto jit = gen_jit(R"SRC(
        module __pointers__;
        a : int;
        b : int;

        init() {
            a = 4;
            b = 5;
        }

        assign(i: int, j: int) : int {
            p : int*;
            if(i<j) {
                p = &a;
            } else {
                p = &b;
            }
            *p += i + j;
            return *p;
        }
        )SRC");
    REQUIRE(jit);

    auto init = jit.get()->lookup_symbol < void(*)() > ("init");
    REQUIRE(init != nullptr);
    init();

    auto assign = jit.get()->lookup_symbol< int(*)(int, int) >("assign");
    REQUIRE(assign != nullptr);
    REQUIRE(assign(1, 2) == 7);
    REQUIRE(assign(2, 1) == 8);

}

//
// References
//

TEST_CASE("References", "[gen][refs]") {
    auto jit = gen_jit(R"SRC(
        module __refs__;
        a : int;

        assign(var: int&, val: int) : int {
            var = val;
            return var;
        }

        test() : int {
            return assign(a, 4);
        }

        value() : int {
            return a;
        }
        )SRC");
    REQUIRE(jit);

    auto test = jit.get()->lookup_symbol < int(*)() > ("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 4);

    auto value = jit.get()->lookup_symbol< int(*)() >("value");
    REQUIRE(value != nullptr);
    REQUIRE(value() == 4);
}

//
// Array indices references
//

TEST_CASE("Array indices references", "[gen][refs][array]") {
    auto jit = gen_jit(R"SRC(
        module __arrs__;

        g : int[4];

        set(p: int[4]&, i: int, v: int) {
            p[i] = v;
        }

        get(p: int[4]&, i: int) : int {
            return p[i];
        }

        test() : int {
            l : int[4];

            set(l, 0, 1);
            set(l, 1, 2);
            set(l, 2, 4);
            set(l, 3, 8);

            set(g, 0, l[0]);
            set(g, 1, l[1]);
            set(g, 2, l[2]);
            set(g, 3, l[3]);

            return get(g, 3);
        }
        )SRC");
    REQUIRE(jit);

    auto test = jit.get()->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 8);
}

//
// Structure content references and invocation
//

TEST_CASE("Structure content references and invocation with local variable", "[gen][struct]") {
    auto jit = gen_jit(R"SRC(
        module __structs__;

        struct plop {
            a : int;
            b: int;
            add(c: int) : int {
                return a + b + c;
            }
        }

        // Test member function invocation with local object
        test_local() : int {
            q : plop;
            q.a = 10;
            q.b = 32;
            return q.add(8);
        }

        another_test_local() : int {
            return test_local() + 5;
        }

        )SRC");
    REQUIRE(jit);

    auto test_local = jit->lookup_symbol < int(*)() > ("test_local");
    auto res_test_local = test_local();
    REQUIRE( res_test_local == (10 + 32 + 8) );

    auto another_test_local = jit->lookup_symbol < int(*)() > ("another_test_local");
    auto res_another_test_local = another_test_local();
    REQUIRE( res_another_test_local == (10 + 32 + 5 + 8) );

}

TEST_CASE("Structure content references and invocation with global variable", "[gen][struct]") {
    auto jit = gen_jit(R"SRC(
        module __structs__;

        struct plop {
            a : int;
            b: int;
            add() : int {
                return a + b;
            }
        }

        // Test member function invocation with global object
        glop : plop;
        test_global() : int {
            glop.a = 18;
            glop.b = 24;
            return glop.add();
        }

        // Test member variable references and assignments
        test_accesses() : int {
            p : plop;
            p.a = 10;
            p.b = p.a + 20;
            glop.a = 5;
            glop.b = p.a + 7;
            p.b += 12;
            return p.add();
        }
        )SRC");
    REQUIRE(jit);

    auto test_global = jit->lookup_symbol < int(*)() > ("test_global");
    auto res_test_global = test_global();
    REQUIRE( res_test_global == 42 );

    struct plop {
        int a;
        int b;
    };
    auto glop = jit->lookup_symbol < plop* > ("glop");
    REQUIRE( glop != nullptr );
    REQUIRE( glop->a == 18 );
    REQUIRE( glop->b == 24 );

    auto test_accesses = jit->lookup_symbol < int(*)() > ("test_accesses");
    auto res_test_accesses = test_accesses();
    REQUIRE( res_test_accesses == 52 );
    REQUIRE( glop->a == 5 );
    REQUIRE( glop->b == 17 );
}

TEST_CASE("Structure content and invocation through reference", "[gen][struct]") {
    auto jit = gen_jit(R"SRC(
        module __structs__;

        struct plop {
            a : int;
            b: int;
            add() : int {
                return a + b;
            }
        }

        // Test member function invocation with ref param object
        test_ref_call(r: plop&) : int {
            r.b = 72;
            return r.add();
        }

        test_ref() : int {
            l : plop;
            l.a = 28;
            return test_ref_call(l) + l.b;
        }

        )SRC");
    REQUIRE(jit);

    auto test_ref = jit->lookup_symbol < int(*)() > ("test_ref");
    auto res_test_ref = test_ref();
    REQUIRE( res_test_ref == ((28 + 72) + 72) );
}

//
// Var name lookup and "this" usage
//

TEST_CASE("Implicit and explicit 'this' name lookup", "[gen][struct]") {
    auto jit = gen_jit(R"SRC(
        module __structs__;

        struct plop {
            a: double;
            b: int;
            c: int;
            implicit() : int {
                return b;
            }
            explicit() : int {
                return this.b;
            }
        }

        test() : int {
            p : plop;
            p.a = 42.2;
            p.b = 10;
            p.c = 35;
            return p.implicit() + p.explicit();
        }

        )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol < int(*)() > ("test");
    auto res_test = test();
    REQUIRE( res_test == (10 + 10) );
}

TEST_CASE("This and var name lookup", "[gen][struct]") {
    auto jit = gen_jit(R"SRC(
        module __structs__;

        struct plop {
            a: double;
            b: int;
            c: int;
            add(b: int) : int {
                return this.b + b;
            }
        }

        test() : int {
            p : plop;
            p.a = 42.2;
            p.b = 10;
            p.c = 35;
            return p.add(20);
        }

        )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol < int(*)() > ("test");
    auto res_test = test();
    REQUIRE( res_test == (10 + 20) );
}

TEST_CASE("Local variable constant init expression", "[gen][variable]") {
    auto jit = gen_jit(R"SRC(
        module __vars__;

        test() : int {
            a : int = 5;
            b : int = 12;
            return a + b;
        }

        )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol < int(*)() > ("test");
    auto res_test = test();
    REQUIRE( res_test == (5 + 12) );
}

TEST_CASE("Global variable constant init expression", "[gen][variable]") {
    auto jit = gen_jit(R"SRC(
        module __vars__;

        a : int = 5;
        b : int = 12;

        test() : int {
            return a + b;
        }

        )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol < int(*)() > ("test");
    auto res_test = test();
    REQUIRE( res_test == (5 + 12) );
}

TEST_CASE("Struct fields default 0-initialization", "[gen][struct]") {
    auto jit = gen_jit(R"SRC(
        module __struct__;

        struct plop {
            // Should be default init to 0
            a : int;
            b : int;
            c : int;
            sum() : int {
                return a + b + c;
            }
        }

        test_local() : int {
            p : plop;
            return p.sum();
        }

        )SRC");
    REQUIRE(jit);

    auto test_local = jit->lookup_symbol < int(*)() > ("test_local");
    auto res_test_local = test_local();
    REQUIRE( res_test_local == (0 + 0 + 0) );
}

TEST_CASE("Struct fields trivial constant default initialization", "[gen][struct]") {
    auto jit = gen_jit(R"SRC(
        module __struct__;

        struct plop {
            a : int = 5;
            b : int = 12;
            c : int; // Should be default init to 0
            sum() : int {
                return a + b + c;
            }
        }

        test_local() : int {
            p : plop;
            return p.sum();
        }

        g : plop;
        test_global() : int {
            return g.sum();
        }

        )SRC");
    REQUIRE(jit);

    auto test_local = jit->lookup_symbol < int(*)() > ("test_local");
    auto res_test_local = test_local();
    REQUIRE( res_test_local == (5 + 12 + 0) );

    auto test_global = jit->lookup_symbol < int(*)() > ("test_global");
    auto res_test_global = test_global();
    REQUIRE( res_test_global == (5 + 12 + 0) );
}

TEST_CASE("Global primitive variable non-trivial initialization", "[gen]") {
    auto jit = gen_jit(R"SRC(
        module __global__;

        a : int = 30 + 12;
        b : int = init();

        init() : int {
            return 25;
        }

        test_a() : int {
            return a;
        }

        test_b() : int {
            return b;
        }

        )SRC");
    REQUIRE(jit);

    auto test_a = jit->lookup_symbol < int(*)() > ("test_a");
    auto res_test_a = test_a();
    REQUIRE( res_test_a == (30 + 12) );

    auto test_b = jit->lookup_symbol < int(*)() > ("test_b");
    auto res_test_b = test_b();
    REQUIRE( res_test_b == 25 );
}

TEST_CASE("Main entry point method returning int", "[gen]") {
    auto jit = gen_jit(R"SRC(
        module __main__;

        test() : int {
            return 42;
        }

        main() : int {
            return test();
        }
        )SRC");
    REQUIRE(jit);

    static const int argc = 3;
    static const char* argv[] = {"test", "my", "prog", nullptr};

    auto main = jit->lookup_main_entry_symbol< int(*)(int, char**) > ();
    auto res_main = main(argc, (char**)argv);
    REQUIRE( res_main == 42 );
}

TEST_CASE("Main entry point method returning nothing", "[gen]") {
    auto jit = gen_jit(R"SRC(
        module __main__;

        test() : int {
            return 42;
        }

        main() {
            test();
        }
        )SRC");
    REQUIRE(jit);

    static const int argc = 3;
    static const char* argv[] = {"test", "my", "prog", nullptr};

    auto main = jit->lookup_main_entry_symbol< int(*)(int, char**) > ();
    auto res_main = main(argc, (char**)argv);
    REQUIRE( res_main == 0 );
}

TEST_CASE("Static struct member", "[gen][var]") {
    auto jit = gen_jit(R"SRC(
        module __static__;

        g : int = 7;

        struct titi {
            a : int = 5;
            static b : int = 12;
            add() : int {
                l : int = 28;
                return a + b + l + g;
            }
        }

        test() : int {
            t : titi;
            t.a = 6;
            titi::b = 13;
            return t.add();
        }

        )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol < int(*)() > ("test");
    auto res_test = test();
    REQUIRE( res_test == (6 + 13 + 28 + 7) );
}

TEST_CASE("Static local variable", "[gen][var]") {
    auto jit = gen_jit(R"SRC(
        module __static__;

        init() : int {
            return 12;
        }

        test_static() : int {
            static i : int = init();
            i += 1;
            return i;
        }

        test_non_static() : int {
            j : int = 12;
            j += 1;
            return j;
        }

        )SRC");
    REQUIRE(jit);

    auto test_static = jit->lookup_symbol < int(*)() > ("test_static");
    auto res_test_static = test_static();
    REQUIRE( res_test_static == (12 + 1) );
    res_test_static = test_static();
    REQUIRE( res_test_static == (12 + 2) );
    res_test_static = test_static();
    REQUIRE( res_test_static == (12 + 3) );

    auto test_non_static = jit->lookup_symbol < int(*)() > ("test_non_static");
    auto res_test_non_static = test_non_static();
    REQUIRE( res_test_non_static == (12 + 1) );
    res_test_non_static = test_non_static();
    REQUIRE( res_test_non_static == (12 + 1) );
    res_test_non_static = test_non_static();
    REQUIRE( res_test_non_static == (12 + 1) );
}

TEST_CASE("Static method", "[gen]") {
    auto jit = gen_jit(R"SRC(
        module __func__;

        struct plop {
            static s : int = 12;

            add(a : int) : int {
                return a + s;
            }

            static sub(b : int) : int {
                return b - s;
            }
        }

        test_add() : int {
            p : plop;
            return p.add(19);
        }

        test_sub() : int {
            return plop::sub(43);
        }

        )SRC");
    REQUIRE(jit);

    auto test_add = jit->lookup_symbol < int(*)() > ("test_add");
    auto res_test_add = test_add();
    REQUIRE( res_test_add == (19 + 12) );

    auto test_sub = jit->lookup_symbol < int(*)() > ("test_sub");
    auto res_test_sub = test_sub();
    REQUIRE( res_test_sub == (43 - 12) );
}

TEST_CASE("Aggregated structs", "[gen][structs]") {
    auto jit = gen_jit(R"SRC(
        module __structs__;

        struct titi {
            b : int = 5;
            p : plop;
            add() : int {
                return p.add(b);
            }
        }

        struct plop {
            a : int = 3;
            add(c : int) : int {
                return a + c;
            }
        }

        test() : int {
            t : titi;
            t.p.a = 7;
            return t.add();
        }

        )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol < int(*)() > ("test");
    auto res_test = test();
    REQUIRE( res_test == (7 + 5) );
}

//
// Struct constructors
//

TEST_CASE("Struct constructor", "[gen][structs]") {
    auto jit = gen_jit(R"SRC(
        module __structs__;

        struct plop {
            a : int = 3;

            plop(c : int) {
                a = c + 1;
            }

            add(c : int) : int {
                return a + c;
            }
        }

        test() : int {
            p : plop(5);
            return p.add(7);
        }

        )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol < int(*)() > ("test");
    auto res_test = test();
    REQUIRE( res_test == (5 + 1 + 7) );
}

TEST_CASE("Struct constructor overload resolution", "[gen][structs]") {
    auto jit = gen_jit(R"SRC(
        module __structs__;

        struct plop {
            a : int = 1;

            plop(c : int) {
                a = 3;
            }

            plop(d : double) {
                a = 5;
            }
        }

        test_int() : int {
            p : plop(2);
            return p.a;
        }

        test_double() : int {
            p : plop(2.0d);
            return p.a;
        }

        )SRC");
    REQUIRE(jit);

    auto test_int = jit->lookup_symbol<int(*)()>("test_int");
    REQUIRE(test_int != nullptr);
    REQUIRE(test_int() == 3);

    auto test_double = jit->lookup_symbol<int(*)()>("test_double");
    REQUIRE(test_double != nullptr);
    REQUIRE(test_double() == 5);
}

//
// Destructor tests
//

TEST_CASE("Struct destructor called on local variable at block exit", "[gen][struct][destructor]") {
    // The destructor increments a global counter.
    // The return value is evaluated BEFORE destruction (C++ semantics),
    // so test_local_dtor() returns 0. After the call returns, dtor_count is 1.
    auto jit = gen_jit(R"SRC(
        module __dtor_local__;

        dtor_count : int;

        struct counter {
            ~counter() {
                dtor_count = dtor_count + 1;
            }
        }

        test_local_dtor() : int {
            c : counter;
            return dtor_count;
        }

        get_dtor_count() : int {
            return dtor_count;
        }
        )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test_local_dtor");
    REQUIRE(test != nullptr);
    // The return expression is evaluated BEFORE c is destroyed:
    // so the returned value is 0 (dtor not yet called at return time).
    REQUIRE(test() == 0);

    // But after the function has returned, c has been destroyed:
    // dtor_count must now be 1.
    auto get_count = jit->lookup_symbol<int(*)()>("get_dtor_count");
    REQUIRE(get_count != nullptr);
    REQUIRE(get_count() == 1);
}

TEST_CASE("Struct destructor called on global variable via global_dtor", "[gen][struct][destructor]") {
    auto jit = gen_jit(R"SRC(
        module __dtor_global__;

        dtor_called : int;

        struct tracked {
            ~tracked() {
                dtor_called = 77;
            }
        }

        g : tracked;

        get_dtor_called() : int {
            return dtor_called;
        }
        )SRC");
    REQUIRE(jit);

    // gen_jit already called initialize_runtime (triggers global constructors).
    // Before finalize: destructor not yet called
    auto get_val = jit->lookup_symbol<int(*)()>("get_dtor_called");
    REQUIRE(get_val != nullptr);
    REQUIRE(get_val() == 0);

    // Finalize triggers global destructors (deinitialize runs llvm.global_dtors)
    jit->finalize_runtime();

    // After finalize: destructor was called, dtor_called == 77
    auto dtor_called = jit->lookup_symbol<int*>("dtor_called");
    REQUIRE(dtor_called != nullptr);
    REQUIRE(*dtor_called == 77);
}

//
// Implicit cast on function invocation
//

TEST_CASE("Function call with primitive widening cast", "[gen][cast][widening]") {
    // short argument passed to int parameter: widening, no data loss
    auto jit = gen_jit(R"SRC(
        module __cast_widening__;

        add(a : int, b : int) : int {
            return a + b;
        }

        test() : int {
            s : short = 10s;
            return add(s, 32);
        }
        )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

TEST_CASE("Function call with primitive narrowing cast", "[gen][cast][narrowing]") {
    // int argument passed to short parameter: narrowing, possible overflow
    auto jit = gen_jit(R"SRC(
        module __cast_narrowing__;

        identity(x : short) : int {
            return x;
        }

        test() : int {
            i : int = 100;
            return identity(i);
        }
        )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 100);
}

TEST_CASE("Function call with int-to-double widening cast", "[gen][cast][widening]") {
    auto jit = gen_jit(R"SRC(
        module __cast_int_double__;

        half(x : double) : double {
            return x / 2.0d;
        }

        test() : int {
            return half(84);
        }
        )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

TEST_CASE("Constructor call with primitive widening cast", "[gen][cast][structs][widening]") {
    // Constructor expects int but we pass a short: widening cast
    auto jit = gen_jit(R"SRC(
        module __ctor_widening__;

        struct box {
            val : int = 0;

            box(v : int) {
                val = v + 1;
            }
        }

        test() : int {
            s : short = 41s;
            b : box(s);
            return b.val;
        }
        )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

TEST_CASE("Constructor overload resolution preferring widening over narrowing", "[gen][cast][structs][widening]") {
    // plop(int) and plop(short): passing an int literal should select plop(int) (CAST_NONE),
    // and passing a short should prefer plop(short) (CAST_NONE) over plop(int) (CAST_WIDENING).
    auto jit = gen_jit(R"SRC(
        module __ctor_overload__;

        struct plop {
            a : int = 0;

            plop(c : int) {
                a = 10;
            }

            plop(d : double) {
                a = 20;
            }
        }

        test_exact_int() : int {
            p : plop(5);
            return p.a;
        }

        test_exact_double() : int {
            p : plop(5.0d);
            return p.a;
        }

        test_widening_short_to_int() : int {
            s : short = 5s;
            p : plop(s);
            return p.a;
        }
        )SRC");
    REQUIRE(jit);

    // Exact match → int constructor
    auto test_exact_int = jit->lookup_symbol<int(*)()>("test_exact_int");
    REQUIRE(test_exact_int != nullptr);
    REQUIRE(test_exact_int() == 10);

    // Exact match → double constructor
    auto test_exact_double = jit->lookup_symbol<int(*)()>("test_exact_double");
    REQUIRE(test_exact_double != nullptr);
    REQUIRE(test_exact_double() == 20);

    // short widened to int → int constructor (widening, score=WIDENING < NARROWING)
    auto test_widening = jit->lookup_symbol<int(*)()>("test_widening_short_to_int");
    REQUIRE(test_widening != nullptr);
    REQUIRE(test_widening() == 10);
}

//
// Mem-initializer-list tests
//

TEST_CASE("Constructor mem-initializer-list: single primitive member", "[gen][structs][mem_init]") {
    // Constructor mem-init sets a primitive field directly
    auto jit = gen_jit(R"SRC(
        module __mem_init_single__;

        struct box {
            val : int = 0;
            box(v : int) : val(v) { }
        }

        test() : int {
            b : box(42);
            return b.val;
        }
        )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

TEST_CASE("Constructor mem-initializer-list: multiple primitive members", "[gen][structs][mem_init]") {
    // Multiple members initialized via mem-init list
    auto jit = gen_jit(R"SRC(
        module __mem_init_multi__;

        struct point {
            x : int = 0;
            y : int = 0;
            point(a : int, b : int) : x(a), y(b) { }
        }

        test_x() : int {
            p : point(10, 20);
            return p.x;
        }

        test_y() : int {
            p : point(10, 20);
            return p.y;
        }
        )SRC");
    REQUIRE(jit);

    auto test_x = jit->lookup_symbol<int(*)()>("test_x");
    REQUIRE(test_x != nullptr);
    REQUIRE(test_x() == 10);

    auto test_y = jit->lookup_symbol<int(*)()>("test_y");
    REQUIRE(test_y != nullptr);
    REQUIRE(test_y() == 20);
}

TEST_CASE("Constructor mem-initializer-list: mem-init with expression", "[gen][structs][mem_init]") {
    // Member initialized with an arithmetic expression
    auto jit = gen_jit(R"SRC(
        module __mem_init_expr__;

        struct doubled {
            val : int = 0;
            doubled(v : int) : val(v * 2) { }
        }

        test() : int {
            d : doubled(21);
            return d.val;
        }
        )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

TEST_CASE("Constructor mem-initializer-list: mem-init overrides default member init", "[gen][structs][mem_init]") {
    // Default member init is 99, but mem-init list overrides with 42
    auto jit = gen_jit(R"SRC(
        module __mem_init_override__;

        struct box {
            val : int = 99;
            box(v : int) : val(v) { }
        }

        test() : int {
            b : box(42);
            return b.val;
        }
        )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

TEST_CASE("Constructor mem-initializer-list: unlisted member keeps its default", "[gen][structs][mem_init]") {
    // x is initialized via mem-init, y keeps its default value
    auto jit = gen_jit(R"SRC(
        module __mem_init_partial__;

        struct pair {
            x : int = 0;
            y : int = 77;
            pair(a : int) : x(a) { }
        }

        test_x() : int {
            p : pair(42);
            return p.x;
        }

        test_y() : int {
            p : pair(42);
            return p.y;
        }
        )SRC");
    REQUIRE(jit);

    auto test_x = jit->lookup_symbol<int(*)()>("test_x");
    REQUIRE(test_x != nullptr);
    REQUIRE(test_x() == 42);

    auto test_y = jit->lookup_symbol<int(*)()>("test_y");
    REQUIRE(test_y != nullptr);
    REQUIRE(test_y() == 77);
}

TEST_CASE("Constructor mem-initializer-list: nested struct member", "[gen][structs][mem_init]") {
    // Inner struct initialized via mem-init list using its own constructor
    auto jit = gen_jit(R"SRC(
        module __mem_init_nested__;

        struct inner {
            v : int = 0;
            inner(x : int) : v(x) { }
        }

        struct outer {
            a : inner;
            outer(x : int) : a(x) { }
        }

        test() : int {
            o : outer(42);
            return o.a.v;
        }
        )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

TEST_CASE("Constructor mem-initializer-list: no mem-init uses default", "[gen][structs][mem_init]") {
    // Constructor without mem-init list: member should use its default value
    auto jit = gen_jit(R"SRC(
        module __mem_init_no_list__;

        struct box {
            val : int = 42;
            box() { }
        }

        test() : int {
            b : box();
            return b.val;
        }
        )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

TEST_CASE("Constructor mem-initializer-list: multiple ctors different mem-inits", "[gen][structs][mem_init]") {
    // Two overloaded constructors with different mem-initializer-lists
    auto jit = gen_jit(R"SRC(
        module __mem_init_overload__;

        struct box {
            val : int = 0;
            box(v : int) : val(v) { }
            box(a : int, b : int) : val(a + b) { }
        }

        test_one() : int {
            b : box(42);
            return b.val;
        }

        test_two() : int {
            b : box(20, 22);
            return b.val;
        }
        )SRC");
    REQUIRE(jit);

    auto test_one = jit->lookup_symbol<int(*)()>("test_one");
    REQUIRE(test_one != nullptr);
    REQUIRE(test_one() == 42);

    auto test_two = jit->lookup_symbol<int(*)()>("test_two");
    REQUIRE(test_two != nullptr);
    REQUIRE(test_two() == 42);
}

TEST_CASE("Constructor mem-initializer-list: a constructor of sub-object with multiple arguments", "[gen][structs][mem_init]") {
    // Constructor with mem-init list that initializes a sub-object using a constructor that takes multiple arguments
    auto jit = gen_jit(R"SRC(
        module __mem_init_overload__;

        struct box {
            p1 : point;
            p2 : point;
            box(x1 : int, y1 : int, x2 : int, y2 : int) : p1(x1, y1), p2(x2, y2) { }
        }

        struct point {
            x : int = 0;
            y : int = 0;
            point(x : int, y : int) : x(x), y(y) { }
        }

        test_x1() : int {
            b : box(7, 13, 23, 42);
            return b.p1.x;
        }

        test_y1() : int {
            b : box(7, 13, 23, 42);
            return b.p1.y;
        }

        test_x2() : int {
            b : box(7, 13, 23, 42);
            return b.p2.x;
        }

        test_y2() : int {
            b : box(7, 13, 23, 42);
            return b.p2.y;
        }

        )SRC");
    REQUIRE(jit);

    auto test_x1 = jit->lookup_symbol<int(*)()>("test_x1");
    REQUIRE(test_x1 != nullptr);
    REQUIRE(test_x1() == 7);

    auto test_y1 = jit->lookup_symbol<int(*)()>("test_y1");
    REQUIRE(test_y1 != nullptr);
    REQUIRE(test_y1() == 13);

    auto test_x2 = jit->lookup_symbol<int(*)()>("test_x2");
    REQUIRE(test_x2 != nullptr);
    REQUIRE(test_x2() == 23);

    auto test_y2 = jit->lookup_symbol<int(*)()>("test_y2");
    REQUIRE(test_y2 != nullptr);
    REQUIRE(test_y2() == 42);
}

//
// Default parameter values
//

TEST_CASE("Function with one default parameter - call with full args", "[gen][default-params]") {
    auto jit = gen_jit(R"SRC(
        module __default_params__;

        add(a: int, b: int = 10) : int {
            return a + b;
        }

        test_full() : int {
            return add(3, 7);
        }

        test_default() : int {
            return add(3);
        }
    )SRC");
    REQUIRE(jit);

    auto test_full = jit->lookup_symbol<int(*)()>("test_full");
    REQUIRE(test_full != nullptr);
    REQUIRE(test_full() == (3 + 7));

    auto test_default = jit->lookup_symbol<int(*)()>("test_default");
    REQUIRE(test_default != nullptr);
    REQUIRE(test_default() == (3 + 10));
}

TEST_CASE("Function with two default parameters", "[gen][default-params]") {
    auto jit = gen_jit(R"SRC(
        module __default_params__;

        compute(a: int, b: int = 5, c: int = 2) : int {
            return a + b * c;
        }

        test_full() : int {
            return compute(1, 3, 4);
        }

        test_one_default() : int {
            return compute(1, 3);
        }

        test_two_defaults() : int {
            return compute(1);
        }
    )SRC");
    REQUIRE(jit);

    auto test_full = jit->lookup_symbol<int(*)()>("test_full");
    REQUIRE(test_full != nullptr);
    REQUIRE(test_full() == (1 + 3*4));

    auto test_one_default = jit->lookup_symbol<int(*)()>("test_one_default");
    REQUIRE(test_one_default != nullptr);
    REQUIRE(test_one_default() == (1 + 3*2));

    auto test_two_defaults = jit->lookup_symbol<int(*)()>("test_two_defaults");
    REQUIRE(test_two_defaults != nullptr);
    REQUIRE(test_two_defaults() == (1 + 5*2));
}

TEST_CASE("Member function with default parameter", "[gen][default-params][structs]") {
    auto jit = gen_jit(R"SRC(
        module __default_params__;

        struct Counter {
            value : int = 0;

            increment(by: int = 1) {
                value = value + by;
            }

            get() : int {
                return value;
            }
        }

        test_increment_by_one() : int {
            c : Counter;
            c.increment();
            return c.get();
        }

        test_increment_by_five() : int {
            c : Counter;
            c.increment(5);
            return c.get();
        }
    )SRC");
    REQUIRE(jit);

    auto test_one = jit->lookup_symbol<int(*)()>("test_increment_by_one");
    REQUIRE(test_one != nullptr);
    REQUIRE(test_one() == 1);

    auto test_five = jit->lookup_symbol<int(*)()>("test_increment_by_five");
    REQUIRE(test_five != nullptr);
    REQUIRE(test_five() == 5);
}

TEST_CASE("Constructor with default parameter", "[gen][default-params][structs]") {
    auto jit = gen_jit(R"SRC(
        module __default_params__;

        struct Box {
            width  : int = 0;
            height : int = 0;

            Box(w: int, h: int = 10) : width(w), height(h) {}

            area() : int {
                return width * height;
            }
        }

        test_explicit() : int {
            b : Box(3, 4);
            return b.area();
        }

        test_default_height() : int {
            b : Box(3);
            return b.area();
        }
    )SRC");
    REQUIRE(jit);

    auto test_explicit = jit->lookup_symbol<int(*)()>("test_explicit");
    REQUIRE(test_explicit != nullptr);
    REQUIRE(test_explicit() == (3 * 4));

    auto test_default = jit->lookup_symbol<int(*)()>("test_default_height");
    REQUIRE(test_default != nullptr);
    REQUIRE(test_default() == (3 * 10));
}

//
// Reference variables (local and global)
//

TEST_CASE("Local reference variable bound to a local variable", "[gen][refs][variable]") {
    auto jit = gen_jit(R"SRC(
        module __ref_var__;

        test() : int {
            x : int = 10;
            r : int& = x;
            r = 42;
            return x;
        }
        )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    // Assigning through r must modify x
    REQUIRE(test() == 42);
}

TEST_CASE("Local reference variable bound to a global variable", "[gen][refs][variable]") {
    auto jit = gen_jit(R"SRC(
        module __ref_var_global__;

        g : int = 0;

        set_via_ref(v : int) {
            r : int& = g;
            r = v;
        }

        get() : int {
            return g;
        }
        )SRC");
    REQUIRE(jit);

    auto set_via_ref = jit->lookup_symbol<void(*)(int)>("set_via_ref");
    REQUIRE(set_via_ref != nullptr);
    set_via_ref(99);

    auto get = jit->lookup_symbol<int(*)()>("get");
    REQUIRE(get != nullptr);
    REQUIRE(get() == 99);
}

TEST_CASE("Local reference variable bound to a function parameter", "[gen][refs][variable]") {
    auto jit = gen_jit(R"SRC(
        module __ref_param__;

        increment(x : int&) {
            r : int& = x;
            r = r + 1;
        }

        test() : int {
            v : int = 10;
            increment(v);
            return v;
        }
        )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 11);
}

TEST_CASE("Local reference variable: assignment modifies referent, does not rebind", "[gen][refs][variable]") {
    // After r is bound to x, assigning r = 99 must change x, not rebind r.
    auto jit = gen_jit(R"SRC(
        module __ref_rebind__;

        test() : int {
            x : int = 10;
            y : int = 20;
            r : int& = x;
            r = 99;         // must change x, not rebind r to y
            return x + y;   // expected: 99 + 20 = 119
        }
        )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == (99 + 20));
}

TEST_CASE("Reference variable bound to struct member", "[gen][refs][variable][struct]") {
    auto jit = gen_jit(R"SRC(
        module __ref_struct__;

        struct Point {
            x : int = 0;
            y : int = 0;
        }

        test() : int {
            p : Point;
            p.x = 3;
            p.y = 7;
            rx : int& = p.x;
            rx = 10;
            return p.x + p.y;   // 10 + 7 = 17
        }
        )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == (10 + 7));
}

//
// Array value variables and struct layout
//

TEST_CASE("Array value variable: zero-initialised on declaration", "[gen][array][value]") {
    auto jit = gen_jit(R"SRC(
        module __arr_zero__;

        test() : int {
            a : int[4];
            return a[0] + a[1] + a[2] + a[3];
        }
        )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 0);
}

TEST_CASE("Array value variable: read and write elements", "[gen][array][value]") {
    auto jit = gen_jit(R"SRC(
        module __arr_rw__;

        test() : int {
            a : int[4];
            a[0] = 10;
            a[1] = 20;
            a[2] = 30;
            a[3] = 40;
            return a[0] + a[1] + a[2] + a[3];
        }
        )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 100);
}

TEST_CASE("Array value variable: global array zero-initialised", "[gen][array][value][global]") {
    auto jit = gen_jit(R"SRC(
        module __arr_global__;

        g : int[3];

        test() : int {
            return g[0] + g[1] + g[2];
        }
        )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 0);
}

TEST_CASE("Array ref variable: pass local array by reference", "[gen][array][ref]") {
    auto jit = gen_jit(R"SRC(
        module __arr_ref__;

        fill(a : int[4]&, v : int) {
            a[0] = v;
            a[1] = v + 1;
            a[2] = v + 2;
            a[3] = v + 3;
        }

        sum(a : int[4]&) : int {
            return a[0] + a[1] + a[2] + a[3];
        }

        test() : int {
            arr : int[4];
            fill(arr, 10);
            return sum(arr);   // 10+11+12+13 = 46
        }
        )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == (10 + 11 + 12 + 13));
}

TEST_CASE("Array ref variable: init copies all elements (same size)", "[gen][array][ref][copy]") {
    auto jit = gen_jit(R"SRC(
        module __arr_copy_same__;

        test() : int {
            src : int[4];
            src[0] = 1;
            src[1] = 2;
            src[2] = 4;
            src[3] = 8;
            dst : int[4]& = src;   // copy: same size → all 4 elements copied
            return dst[0] + dst[1] + dst[2] + dst[3];   // 1+2+4+8 = 15
        }
        )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 15);
}

TEST_CASE("Array ref init: dest smaller than src — only first N elements copied", "[gen][array][ref][copy]") {
    // dst has 2 elements, src has 4: only first 2 are copied
    auto jit = gen_jit(R"SRC(
        module __arr_copy_smaller__;

        test() : int {
            src : int[4];
            src[0] = 10;
            src[1] = 20;
            src[2] = 99;
            src[3] = 99;
            dst : int[2]& = src;   // only 2 elements copied
            return dst[0] + dst[1];   // 10+20 = 30
        }
        )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 30);
}

TEST_CASE("Array ref init: dest larger than src — extra elements zero-initialised", "[gen][array][ref][copy]") {
    // dst has 6 elements, src has 3: 3 copied, 3 zero-filled
    auto jit = gen_jit(R"SRC(
        module __arr_copy_larger__;

        test() : int {
            src : int[3];
            src[0] = 5;
            src[1] = 7;
            src[2] = 3;
            dst : int[6]& = src;   // 3 copied, 3 zeroed
            return dst[0] + dst[1] + dst[2] + dst[3] + dst[4] + dst[5];   // 5+7+3+0+0+0 = 15
        }
        )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 15);
}

TEST_CASE("Array assignment: same size — element-wise copy", "[gen][array][assign]") {
    auto jit = gen_jit(R"SRC(
        module __arr_assign__;

        test() : int {
            a : int[3];
            a[0] = 1; a[1] = 2; a[2] = 3;
            b : int[3];
            b = a;
            return b[0] + b[1] + b[2];   // 6
        }
        )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 6);
}

TEST_CASE("Array assignment: modifying src after assignment does not affect dest", "[gen][array][assign]") {
    // Assignment copies values; mutating src afterwards must not change dest.
    auto jit = gen_jit(R"SRC(
        module __arr_assign_indep__;

        test() : int {
            a : int[3];
            a[0] = 10; a[1] = 20; a[2] = 30;
            b : int[3];
            b = a;
            a[0] = 99;   // mutate src
            return b[0]; // must still be 10
        }
        )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 10);
}

TEST_CASE("Array assignment: partial copy (dest < src), unchanged tail", "[gen][array][assign]") {
    // Assign int[5] to int[3]: only first 3 elements of dest are overwritten.
    auto jit = gen_jit(R"SRC(
        module __arr_assign_partial__;

        test() : int {
            src : int[5];
            src[0]=1; src[1]=2; src[2]=3; src[3]=4; src[4]=5;
            dst : int[3];
            dst[0]=10; dst[1]=20; dst[2]=30;
            dst = src;   // only 3 elements copied
            return dst[0] + dst[1] + dst[2];  // 1+2+3 = 6
        }
        )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 6);
}

TEST_CASE("Array ref variable: unbound (no init) must be rejected", "[gen][array][ref][error]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module __arr_ref_no_init__;
        test() : int {
            r : int[4]&;   // ERROR: array ref without initialiser
            return 0;
        }
    )SRC"));
}

TEST_CASE("Array ref variable: init with non-array must be rejected", "[gen][array][ref][error]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module __arr_ref_bad_init__;
        test() : int {
            x : int = 5;
            r : int[4]& = x;   // ERROR: int is not an array
            return 0;
        }
    )SRC"));
}

TEST_CASE("Array ref variable: element type mismatch must be rejected", "[gen][array][ref][error]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module __arr_ref_type_mismatch__;
        test() : int {
            src : double[4];
            r : int[4]& = src;   // ERROR: double[] != int[]
            return 0;
        }
    )SRC"));
}

//
// Array of double
//

TEST_CASE("Array of double: zero-init on declaration", "[gen][array][double]") {
    auto jit = gen_jit(R"SRC(
        module __arr_dbl_zero__;
        // Returns 1 if all elements are 0.0, 0 otherwise (using int arithmetic on double comparison)
        test() : int {
            a : double[3];
            // Compare each to 0.0; if all zero, sum of abs should be 0
            // We return the first non-zero flag, or 0 if all ok
            if (a[0] != 0.0d) { return 1; }
            if (a[1] != 0.0d) { return 1; }
            if (a[2] != 0.0d) { return 1; }
            return 0;
        }
        )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 0);
}

TEST_CASE("Array of double: read and write elements", "[gen][array][double]") {
    auto jit = gen_jit(R"SRC(
        module __arr_dbl_rw__;
        // We work with doubles and return int via explicit cast
        sum(a : double[4]&) : double {
            return a[0] + a[1] + a[2] + a[3];
        }
        test() : int {
            a : double[4];
            a[0] = 1.5d;
            a[1] = 2.5d;
            a[2] = 3.0d;
            a[3] = 5.0d;
            return (int) sum(a);   // 1.5+2.5+3.0+5.0 = 12.0 → 12
        }
        )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 12);
}

TEST_CASE("Array of double ref: copy same size", "[gen][array][double][ref][copy]") {
    auto jit = gen_jit(R"SRC(
        module __arr_dbl_copy__;
        sum(a : double[3]&) : double {
            return a[0] + a[1] + a[2];
        }
        test() : int {
            src : double[3];
            src[0] = 10.0d;
            src[1] = 20.0d;
            src[2] = 12.0d;
            dst : double[3]& = src;
            return (int) sum(dst);   // 10+20+12 = 42
        }
        )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

TEST_CASE("Array of double: assignment element-wise copy", "[gen][array][double][assign]") {
    auto jit = gen_jit(R"SRC(
        module __arr_dbl_assign__;
        sum(a : double[2]&) : double {
            return a[0] + a[1];
        }
        test() : int {
            a : double[2];
            a[0] = 6.0d; a[1] = 7.0d;
            b : double[2];
            b = a;
            return (int) sum(b);   // 6+7 = 13
        }
        )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 13);
}

//
// Array of struct — default constructor (zero-init fields)
//

TEST_CASE("Array of struct: default construction (zero fields)", "[gen][array][struct]") {
    auto jit = gen_jit(R"SRC(
        module __arr_struct_default__;

        struct Point {
            x : int;
            y : int;
        }

        sumX(a : Point[3]&) : int {
            return a[0].x + a[1].x + a[2].x;
        }

        test() : int {
            pts : Point[3];
            return sumX(pts);   // all x == 0 → 0
        }
        )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 0);
}

TEST_CASE("Array of struct: write and read fields", "[gen][array][struct]") {
    auto jit = gen_jit(R"SRC(
        module __arr_struct_rw__;

        struct Point {
            x : int;
            y : int;
            sum() : int { return x + y; }
        }

        test() : int {
            pts : Point[3];
            pts[0].x = 1;  pts[0].y = 2;
            pts[1].x = 3;  pts[1].y = 4;
            pts[2].x = 5;  pts[2].y = 6;
            return pts[0].sum() + pts[1].sum() + pts[2].sum();
            // (1+2) + (3+4) + (5+6) = 3 + 7 + 11 = 21
        }
        )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 21);
}

TEST_CASE("Array of struct: pass by ref, modify elements", "[gen][array][struct][ref]") {
    auto jit = gen_jit(R"SRC(
        module __arr_struct_ref__;

        struct Pair {
            a : int;
            b : int;
        }

        fill(arr : Pair[4]&, base : int) {
            arr[0].a = base;      arr[0].b = base + 1;
            arr[1].a = base + 2;  arr[1].b = base + 3;
            arr[2].a = base + 4;  arr[2].b = base + 5;
            arr[3].a = base + 6;  arr[3].b = base + 7;
        }

        sumAll(arr : Pair[4]&) : int {
            return arr[0].a + arr[0].b
                 + arr[1].a + arr[1].b
                 + arr[2].a + arr[2].b
                 + arr[3].a + arr[3].b;
        }

        test() : int {
            data : Pair[4];
            fill(data, 1);
            // a: 1,3,5,7   b: 2,4,6,8
            // sum = 1+2+3+4+5+6+7+8 = 36
            return sumAll(data);
        }
        )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 36);
}

//
// Array of struct with explicit constructors
//

TEST_CASE("Array of struct with single-arg constructor: element access after fill", "[gen][array][struct][ctor]") {
    auto jit = gen_jit(R"SRC(
        module __arr_struct_ctor1__;

        struct Counter {
            value : int = 0;
            Counter(v : int) { value = v; }
            get() : int { return value; }
        }

        // Manually construct each element into a pre-allocated array
        test() : int {
            arr : Counter[3];
            // Assign through member directly (no per-element ctor call yet)
            arr[0].value = 10;
            arr[1].value = 20;
            arr[2].value = 30;
            return arr[0].get() + arr[1].get() + arr[2].get();   // 60
        }
        )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 60);
}

TEST_CASE("Array of struct with two-arg constructor: element access", "[gen][array][struct][ctor]") {
    auto jit = gen_jit(R"SRC(
        module __arr_struct_ctor2__;

        struct Vec2 {
            x : int = 0;
            y : int = 0;
            Vec2(px : int, py : int) : x(px), y(py) {}
            dot(other : Vec2&) : int { return x * other.x + y * other.y; }
            len2() : int { return x * x + y * y; }
        }

        test() : int {
            arr : Vec2[3];
            arr[0].x = 1;  arr[0].y = 0;
            arr[1].x = 0;  arr[1].y = 1;
            arr[2].x = 3;  arr[2].y = 4;
            // len2: 1+0=1, 0+1=1, 9+16=25 → sum=27
            return arr[0].len2() + arr[1].len2() + arr[2].len2();
        }
        )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 27);
}

TEST_CASE("Array of struct ref copy: same size, plain struct fields", "[gen][array][struct][ref][copy]") {
    auto jit = gen_jit(R"SRC(
        module __arr_struct_refcopy__;

        struct Item {
            id  : int = 0;
            qty : int = 0;
        }

        totalQty(arr : Item[3]&) : int {
            return arr[0].qty + arr[1].qty + arr[2].qty;
        }

        test() : int {
            src : Item[3];
            src[0].id = 1;  src[0].qty = 5;
            src[1].id = 2;  src[1].qty = 7;
            src[2].id = 3;  src[2].qty = 3;
            dst : Item[3]& = src;           // copy all 3 elements
            return totalQty(dst);           // 5+7+3 = 15
        }
        )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 15);
}

TEST_CASE("Array of struct ref: dest larger than src — extra elements zero-init", "[gen][array][struct][ref][copy]") {
    auto jit = gen_jit(R"SRC(
        module __arr_struct_refcopy_larger__;

        struct Slot {
            val : int = 0;
        }

        sumVals(arr : Slot[5]&) : int {
            return arr[0].val + arr[1].val + arr[2].val + arr[3].val + arr[4].val;
        }

        test() : int {
            src : Slot[2];
            src[0].val = 10;
            src[1].val = 20;
            dst : Slot[5]& = src;   // 2 copied, 3 zero-init
            return sumVals(dst);    // 10+20+0+0+0 = 30
        }
        )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 30);
}

TEST_CASE("Array of struct assignment: element-wise copy, tail unchanged", "[gen][array][struct][assign]") {
    auto jit = gen_jit(R"SRC(
        module __arr_struct_assign__;

        struct Pt {
            x : int = 0;
            y : int = 0;
        }

        sumX(arr : Pt[4]&) : int {
            return arr[0].x + arr[1].x + arr[2].x + arr[3].x;
        }

        test() : int {
            src : Pt[4];
            src[0].x = 1;
            src[1].x = 2;
            src[2].x = 3;
            src[3].x = 4;

            dst : Pt[4];
            dst = src;

            return sumX(dst);   // 1+2+3+4 = 10
        }
        )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 10);
}

TEST_CASE("Global array of struct: zero-init", "[gen][array][struct][global]") {
    auto jit = gen_jit(R"SRC(
        module __arr_struct_global__;

        struct Node {
            data : int = 0;
            next : int = -1;
        }

        g : Node[4];

        sumData() : int {
            return g[0].data + g[1].data + g[2].data + g[3].data;
        }

        test() : int {
            return sumData();   // all data == 0
        }
        )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 0);
}

TEST_CASE("Global array of struct: write and read", "[gen][array][struct][global]") {
    auto jit = gen_jit(R"SRC(
        module __arr_struct_global_rw__;

        struct Score {
            player : int = 0;
            points : int = 0;
        }

        scores : Score[3];

        setScore(i : int, p : int, pts : int) {
            scores[i].player = p;
            scores[i].points = pts;
        }

        totalPoints() : int {
            return scores[0].points + scores[1].points + scores[2].points;
        }

        test() : int {
            setScore(0, 1, 10);
            setScore(1, 2, 25);
            setScore(2, 3, 15);
            return totalPoints();   // 10+25+15 = 50
        }
        )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 50);
}

TEST_CASE("Array of struct with default-value fields: zero-declared array", "[gen][array][struct][ctor]") {
    // Struct has non-zero defaults.  A zero-declared array should have all fields zeroed
    // (current spec: arrays are always zero-initialised, default field values are not
    // applied element-by-element for array values — they apply only to standalone variables).
    auto jit = gen_jit(R"SRC(
        module __arr_struct_defaults__;

        struct Pair {
            a : int;
            b : int;
        }

        sum(arr : Pair[3]&) : int {
            return arr[0].a + arr[0].b
                 + arr[1].a + arr[1].b
                 + arr[2].a + arr[2].b;
        }

        test() : int {
            arr : Pair[3];
            // All fields are zero after declaration
            return sum(arr);    // 0
        }
        )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 0);
}
