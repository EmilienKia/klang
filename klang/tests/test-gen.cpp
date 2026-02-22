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

#include "../src/common/logger.hpp"
#include "../src/parse/parser.hpp"
#include "../src/model/model.hpp"
#include "../src/gen/generators.hpp"
#include "../src/compiler.hpp"

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
        set(p: int[4]&, i: int, v: int) {
                p[i] = v;
        }

        get(p: int[4]&, i: int) : int {
                return p[i];
        }
		
        test(p: int[4]&) : int {
		l: int[4];

                set(l, 0, 1);
                set(l, 1, 2);
                set(l, 2, 4);
                set(l, 3, 8);

                set(p, 0, l[0]);
                set(p, 1, l[1]);
                set(p, 2, l[2]);
                set(p, 3, l[3]);

                return p[3];
        }
        )SRC");
    REQUIRE(jit);

    int arr[4] = {0, 0, 0, 0};
    auto ptr = &arr;

    auto test = jit.get()->lookup_symbol<int(*)(int(*)[4]) >("test");
    REQUIRE(test != nullptr);
    REQUIRE(test(ptr) == 8);
    REQUIRE(arr[0] == 1);
    REQUIRE(arr[1] == 2);
    REQUIRE(arr[2] == 4);
    REQUIRE(arr[3] == 8);
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

        )SRC", true);
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

        )SRC", true);
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
        module __ctor_overload_weight__;

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
