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

// =============================================================================
// Name lookup — model-level (compiler::find_elements / get_element_mangled_name)
// =============================================================================

TEST_CASE("Relative name lookup", "[gen][name_lookup]") {
    auto comp = k::compiler::create();
    comp->parse_source("", R"SRC(
        module the::test;

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

    // Global function:
    auto test_local_elems = comp->find_elements("test_local");
    REQUIRE( test_local_elems.size() == 1 );
    auto test_local_elem = std::dynamic_pointer_cast<k::model::function>(test_local_elems.front());
    REQUIRE( test_local_elem != nullptr );
    REQUIRE( test_local_elem->get_short_name() == "test_local" );
    REQUIRE( test_local_elem->get_fq_name() == "::the::test::test_local" );
    REQUIRE( test_local_elem->get_mangled_name() == "_KFN3the4test10test_localEv" );
    REQUIRE( test_local_elem->parent<k::model::ns>() == comp->get_unit()->get_root_namespace() );

    // Global variable:
    auto g_elems = comp->find_elements("g");
    REQUIRE( g_elems.size() == 1 );
    auto g_elem = std::dynamic_pointer_cast<k::model::global_variable_definition>(g_elems.front());
    REQUIRE( g_elem != nullptr );
    REQUIRE( g_elem->get_short_name() == "g" );
    REQUIRE( g_elem->get_fq_name() == "::the::test::g" );
    REQUIRE( g_elem->get_mangled_name() == "_KN3the4test1gE" );
    REQUIRE( g_elem->parent<k::model::ns>() == comp->get_unit()->get_root_namespace() );

    // Structure:
    auto plop_elems = comp->find_elements("plop");
    REQUIRE( plop_elems.size() == 1 );
    auto plop_elem = std::dynamic_pointer_cast<k::model::structure>(plop_elems.front());
    REQUIRE( plop_elem != nullptr );
    REQUIRE( plop_elem->get_short_name() == "plop" );
    REQUIRE( plop_elem->get_fq_name() == "::the::test::plop" );
    REQUIRE( plop_elem->get_mangled_name() == "_KN3the4test4plopE" );
    REQUIRE( plop_elem->parent<k::model::ns>() == comp->get_unit()->get_root_namespace() );

    // Member function:
    auto plop_sum_elems = comp->find_elements("plop::sum");
    REQUIRE( plop_sum_elems.size() == 1 );
    auto plop_sum_elem = std::dynamic_pointer_cast<k::model::function>(plop_sum_elems.front());
    REQUIRE( plop_sum_elem != nullptr );
    REQUIRE( plop_sum_elem->get_short_name() == "sum" );
    REQUIRE( plop_sum_elem->get_fq_name() == "::the::test::plop::sum" );
    REQUIRE( plop_sum_elem->get_mangled_name() == "_KFMN3the4test4plop3sumEv" );
    REQUIRE( plop_sum_elem->parent<k::model::structure>() == plop_elem );

}

TEST_CASE("Relative mangled name lookup", "[gen][name_lookup]") {
    auto comp = k::compiler::create();
    comp->parse_source("", R"SRC(
        module the::test;

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

    // Global function:
    REQUIRE( comp->get_element_mangled_name("test_local") == "_KFN3the4test10test_localEv" );

    // Global variable:
    REQUIRE( comp->get_element_mangled_name("g") == "_KN3the4test1gE" );

    // Member function:
    REQUIRE( comp->get_element_mangled_name("plop::sum") == "_KFMN3the4test4plop3sumEv" );

    // Structure (cannot be mangled):
    REQUIRE_THROWS_AS(comp->get_element_mangled_name("plop"), std::runtime_error);

    // No such element:
    REQUIRE_THROWS_AS(comp->get_element_mangled_name("blahblah::blah"), std::runtime_error);
}

TEST_CASE("Relative to root namespace name lookup", "[gen][name_lookup]") {
    auto comp = k::compiler::create();
    comp->parse_source("", R"SRC(
        module the::test;

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

    // Global function:
    auto test_local_elems = comp->find_elements("the::test::test_local");
    REQUIRE( test_local_elems.size() == 1 );
    auto test_local_elem = std::dynamic_pointer_cast<k::model::function>(test_local_elems.front());
    REQUIRE( test_local_elem != nullptr );
    REQUIRE( test_local_elem->get_short_name() == "test_local" );
    REQUIRE( test_local_elem->get_fq_name() == "::the::test::test_local" );
    REQUIRE( test_local_elem->get_mangled_name() == "_KFN3the4test10test_localEv" );
    REQUIRE( test_local_elem->parent<k::model::ns>() == comp->get_unit()->get_root_namespace() );

    // Global variable:
    auto g_elems = comp->find_elements("the::test::g");
    REQUIRE( g_elems.size() == 1 );
    auto g_elem = std::dynamic_pointer_cast<k::model::global_variable_definition>(g_elems.front());
    REQUIRE( g_elem != nullptr );
    REQUIRE( g_elem->get_short_name() == "g" );
    REQUIRE( g_elem->get_fq_name() == "::the::test::g" );
    REQUIRE( g_elem->get_mangled_name() == "_KN3the4test1gE" );
    REQUIRE( g_elem->parent<k::model::ns>() == comp->get_unit()->get_root_namespace() );

    // Structure:
    auto plop_elems = comp->find_elements("the::test::plop");
    REQUIRE( plop_elems.size() == 1 );
    auto plop_elem = std::dynamic_pointer_cast<k::model::structure>(plop_elems.front());
    REQUIRE( plop_elem != nullptr );
    REQUIRE( plop_elem->get_short_name() == "plop" );
    REQUIRE( plop_elem->get_fq_name() == "::the::test::plop" );
    REQUIRE( plop_elem->get_mangled_name() == "_KN3the4test4plopE" );
    REQUIRE( plop_elem->parent<k::model::ns>() == comp->get_unit()->get_root_namespace() );

    // Member function:
    auto plop_sum_elems = comp->find_elements("the::test::plop::sum");
    REQUIRE( plop_sum_elems.size() == 1 );
    auto plop_sum_elem = std::dynamic_pointer_cast<k::model::function>(plop_sum_elems.front());
    REQUIRE( plop_sum_elem != nullptr );
    REQUIRE( plop_sum_elem->get_short_name() == "sum" );
    REQUIRE( plop_sum_elem->get_fq_name() == "::the::test::plop::sum" );
    REQUIRE( plop_sum_elem->get_mangled_name() == "_KFMN3the4test4plop3sumEv" );
    REQUIRE( plop_sum_elem->parent<k::model::structure>() == plop_elem );

}

TEST_CASE("Relative to root namespace mangled name lookup", "[gen][name_lookup]") {
    auto comp = k::compiler::create();
    comp->parse_source("", R"SRC(
        module the::test;

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

    // Global function:
    REQUIRE( comp->get_element_mangled_name("the::test::test_local") == "_KFN3the4test10test_localEv" );

    // Global variable:
    REQUIRE( comp->get_element_mangled_name("the::test::g") == "_KN3the4test1gE" );

    // Member function:
    REQUIRE( comp->get_element_mangled_name("the::test::plop::sum") == "_KFMN3the4test4plop3sumEv" );

    // Structure (cannot be mangled):
    REQUIRE_THROWS_AS(comp->get_element_mangled_name("the::test::plop"), std::runtime_error);

    // No such element:
    REQUIRE_THROWS_AS(comp->get_element_mangled_name("the::test::blahblah::blah"), std::runtime_error);
}

TEST_CASE("Absolute name lookup", "[gen][name_lookup]") {
    auto comp = k::compiler::create();
    comp->parse_source("", R"SRC(
        module the::test;

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

    // Global function:
    auto test_local_elems = comp->find_elements("::the::test::test_local");
    REQUIRE( test_local_elems.size() == 1 );
    auto test_local_elem = std::dynamic_pointer_cast<k::model::function>(test_local_elems.front());
    REQUIRE( test_local_elem != nullptr );
    REQUIRE( test_local_elem->get_short_name() == "test_local" );
    REQUIRE( test_local_elem->get_fq_name() == "::the::test::test_local" );
    REQUIRE( test_local_elem->get_mangled_name() == "_KFN3the4test10test_localEv" );
    REQUIRE( test_local_elem->parent<k::model::ns>() == comp->get_unit()->get_root_namespace() );

    // Global variable:
    auto g_elems = comp->find_elements("::the::test::g");
    REQUIRE( g_elems.size() == 1 );
    auto g_elem = std::dynamic_pointer_cast<k::model::global_variable_definition>(g_elems.front());
    REQUIRE( g_elem != nullptr );
    REQUIRE( g_elem->get_short_name() == "g" );
    REQUIRE( g_elem->get_fq_name() == "::the::test::g" );
    REQUIRE( g_elem->get_mangled_name() == "_KN3the4test1gE" );
    REQUIRE( g_elem->parent<k::model::ns>() == comp->get_unit()->get_root_namespace() );

    // Structure:
    auto plop_elems = comp->find_elements("::the::test::plop");
    REQUIRE( plop_elems.size() == 1 );
    auto plop_elem = std::dynamic_pointer_cast<k::model::structure>(plop_elems.front());
    REQUIRE( plop_elem != nullptr );
    REQUIRE( plop_elem->get_short_name() == "plop" );
    REQUIRE( plop_elem->get_fq_name() == "::the::test::plop" );
    REQUIRE( plop_elem->get_mangled_name() == "_KN3the4test4plopE" );
    REQUIRE( plop_elem->parent<k::model::ns>() == comp->get_unit()->get_root_namespace() );

    // Member function:
    auto plop_sum_elems = comp->find_elements("::the::test::plop::sum");
    REQUIRE( plop_sum_elems.size() == 1 );
    auto plop_sum_elem = std::dynamic_pointer_cast<k::model::function>(plop_sum_elems.front());
    REQUIRE( plop_sum_elem != nullptr );
    REQUIRE( plop_sum_elem->get_short_name() == "sum" );
    REQUIRE( plop_sum_elem->get_fq_name() == "::the::test::plop::sum" );
    REQUIRE( plop_sum_elem->get_mangled_name() == "_KFMN3the4test4plop3sumEv" );
    REQUIRE( plop_sum_elem->parent<k::model::structure>() == plop_elem );
}

TEST_CASE("Absolute mangled name lookup", "[gen][name_lookup]") {
    auto comp = k::compiler::create();
    comp->parse_source("", R"SRC(
        module the::test;

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

    // Global function:
    REQUIRE( comp->get_element_mangled_name("::the::test::test_local") == "_KFN3the4test10test_localEv" );

    // Global variable:
    REQUIRE( comp->get_element_mangled_name("::the::test::g") == "_KN3the4test1gE" );

    // Member function:
    REQUIRE( comp->get_element_mangled_name("::the::test::plop::sum") == "_KFMN3the4test4plop3sumEv" );

    // Structure (cannot be mangled):
    REQUIRE_THROWS_AS(comp->get_element_mangled_name("::the::test::plop"), std::runtime_error);

    // No such element:
    REQUIRE_THROWS_AS(comp->get_element_mangled_name("::the::test::blahblah::blah"), std::runtime_error);
}

// =============================================================================
// Function overloading
// =============================================================================

TEST_CASE("Global function overloading", "[gen][overload]") {
    // Two free functions with the same name but different parameter types
    auto jit = gen_jit(R"SRC(
        module __global_overload__;

        compute(a : int) : int {
            return a * 2;
        }

        compute(a : double) : int {
            return a * 3;
        }

        test_int() : int {
            return compute(7);
        }

        test_double() : int {
            return compute(4.0d);
        }
        )SRC");
    REQUIRE(jit);

    auto test_int = jit->lookup_symbol<int(*)()>("test_int");
    REQUIRE(test_int != nullptr);
    REQUIRE(test_int() == 14);

    auto test_double = jit->lookup_symbol<int(*)()>("test_double");
    REQUIRE(test_double != nullptr);
    REQUIRE(test_double() == 12);
}

TEST_CASE("Member method overloading", "[gen][overload]") {
    // Two member functions with the same name but different parameter types
    auto jit = gen_jit(R"SRC(
        module __member_overload__;

        struct calc {
            base : int = 10;

            add(x : int) : int {
                return base + x;
            }

            add(x : double) : int {
                return base + x * 2;
            }
        }

        test_int() : int {
            c : calc;
            return c.add(5);
        }

        test_double() : int {
            c : calc;
            return c.add(3.0d);
        }
        )SRC");
    REQUIRE(jit);

    auto test_int = jit->lookup_symbol<int(*)()>("test_int");
    REQUIRE(test_int != nullptr);
    REQUIRE(test_int() == 15);

    auto test_double = jit->lookup_symbol<int(*)()>("test_double");
    REQUIRE(test_double != nullptr);
    REQUIRE(test_double() == 16);
}

TEST_CASE("Function overloading with widening cast resolution", "[gen][overload][cast]") {
    // Three overloads: func(int), func(double), func(short).
    // Passing an int literal should select func(int).
    // Passing a double should select func(double).
    // Passing a short should select func(short) (exact match).
    auto jit = gen_jit(R"SRC(
        module __overload_cast__;

        tag(x : int) : int    { return 1; }
        tag(x : double) : int { return 2; }
        tag(x : short) : int  { return 3; }

        test_int()    : int { return tag(42);     }
        test_double() : int { return tag(1.0d);   }
        test_short()  : int {
            s : short = 5s;
            return tag(s);
        }
        )SRC");
    REQUIRE(jit);

    auto test_int = jit->lookup_symbol<int(*)()>("test_int");
    REQUIRE(test_int != nullptr);
    REQUIRE(test_int() == 1);

    auto test_double = jit->lookup_symbol<int(*)()>("test_double");
    REQUIRE(test_double != nullptr);
    REQUIRE(test_double() == 2);

    auto test_short = jit->lookup_symbol<int(*)()>("test_short");
    REQUIRE(test_short != nullptr);
    REQUIRE(test_short() == 3);
}

// =============================================================================
// Unified call syntax
// =============================================================================

TEST_CASE("Unified call syntax: free function called as member method", "[gen][overload][unified_call]") {
    // A free function whose first param is ref<struct> can be called as obj.method()
    auto jit = gen_jit(R"SRC(
        module __unified__;

        struct point {
            x : int = 0;
            y : int = 0;

            static difference(p : point&) : int {
                return p.y - p.x;
            }
        }

        // Free function, first param is ref to point
        sum(p : point&) : int {
            return p.x + p.y;
        }

        scale(p : point&, factor : int) : int {
            return (p.x + p.y) * factor;
        }

        test_sum() : int {
            pt : point;
            pt.x = 3;
            pt.y = 7;
            return pt.sum();
        }

        test_scale() : int {
            pt : point;
            pt.x = 4;
            pt.y = 6;
            return pt.scale(3);
        }

        test_difference() : int {
            pt : point;
            pt.x = 8;
            pt.y = 20;
            return pt.difference();
        }

        )SRC");
    REQUIRE(jit);

    auto test_sum = jit->lookup_symbol<int(*)()>("test_sum");
    REQUIRE(test_sum != nullptr);
    REQUIRE(test_sum() == 10);

    auto test_scale = jit->lookup_symbol<int(*)()>("test_scale");
    REQUIRE(test_scale != nullptr);
    REQUIRE(test_scale() == 30);

    auto test_difference = jit->lookup_symbol<int(*)()>("test_difference");
    REQUIRE(test_difference != nullptr);
    REQUIRE(test_difference() == 12);
}

TEST_CASE("Unified call syntax: member method called with free-function syntax", "[gen][overload][unified_call]") {
    // A member function can be called as func(obj, args...)
    auto jit = gen_jit(R"SRC(
        module __unified__;

        struct counter {
            val : int = 0;

            increment(n : int) : int {
                val = val + n;
                return val;
            }
        }

        test() : int {
            c : counter;
            r1 : int = increment(c, 5);
            r2 : int = increment(c, 3);
            return r1 + r2;
        }
        )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == (5 + 8));  // r1=5, r2=5+3=8, total=13
}

TEST_CASE("Unified call: prefer member method over free function for obj.method()", "[gen][overload][unified_call]") {
    // When both a member function and a free function with first param ref<struct> exist,
    // the member function should be preferred (lower score: exact match vs unified call).
    auto jit = gen_jit(R"SRC(
        module __unified_pref__;

        struct box {
            val : int = 0;

            // Member function: returns 10
            get() : int {
                return 10;
            }
        }

        // Free function with box& as first param: returns 20
        get(b : box&) : int {
            return 20;
        }

        test() : int {
            b : box;
            return b.get();
        }
    )SRC");
    REQUIRE(jit);

    // The member function should win (it's an exact match as member, not unified)
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 10);
}

TEST_CASE("Unified call syntax: recursive function resolution with member of call form", "[gen][overload][unified_call]") {
    // Look recursively for compatible methods
    auto jit = gen_jit(R"SRC(
        module __unified__;

        struct point {
            x : int = 0;
            y : int = 0;

            // Member function with int param, should be preferred for point.get(int)
            get(i : int) : int {
                return 1;
            }

            // Static member function with float param, should be preferred for point.get(float)
            static get(p : point&, f : float) : int {
                return 2;
            }
        }

        // Static member function with double param, should be preferred for point.get(double)
        get(p : point&, d: double) : int {
            return 3;
        }

        test_int() : int {
            pt : point;
            return pt.get(5);
        }

        test_float() : int {
            pt : point;
            return pt.get(6f);
        }

        test_double() : int {
            pt : point;
            return pt.get(7d);
        }

        )SRC");
    REQUIRE(jit);

    auto test_int = jit->lookup_symbol<int(*)()>("test_int");
    REQUIRE(test_int != nullptr);
    REQUIRE(test_int() == 1);

    auto test_float = jit->lookup_symbol<int(*)()>("test_float");
    REQUIRE(test_float != nullptr);
    REQUIRE(test_float() == 2);

    auto test_double = jit->lookup_symbol<int(*)()>("test_double");
    REQUIRE(test_double != nullptr);
    REQUIRE(test_double() == 3);
}

TEST_CASE("Unified call syntax: recursive function resolution with free function call form", "[gen][overload][unified_call]") {
    // Look recursively for compatible methods
    auto jit = gen_jit(R"SRC(
        module __unified__;

        struct point {
            x : int = 0;
            y : int = 0;

            // Member function with int param, should be preferred for point.get(int)
            get(i : int) : int {
                return 1;
            }

            // Static member function with float param, should be preferred for point.get(float)
            static get(p : point&, f : float) : int {
                return 2;
            }
        }

        // Static member function with double param, should be preferred for point.get(double)
        get(p : point&, d: double) : int {
            return 3;
        }

        test_int() : int {
            pt : point;
            return point::get(pt, 5);
        }

        test_float() : int {
            pt : point;
            return point::get(pt, 6f);
        }

        test_double() : int {
            pt : point;
            return get(pt, 7d);
        }

        )SRC");
    REQUIRE(jit);

    auto test_int = jit->lookup_symbol<int(*)()>("test_int");
    REQUIRE(test_int != nullptr);
    REQUIRE(test_int() == 1);

    auto test_float = jit->lookup_symbol<int(*)()>("test_float");
    REQUIRE(test_float != nullptr);
    REQUIRE(test_float() == 2);

    auto test_double = jit->lookup_symbol<int(*)()>("test_double");
    REQUIRE(test_double != nullptr);
    REQUIRE(test_double() == 3);
}

// =============================================================================
// Qualified name lookup tests
// =============================================================================

// -----------------------------------------------------------------------------
// 1. Simple qualified call : struct_name::static_func(args) from same namespace
// -----------------------------------------------------------------------------
TEST_CASE("Qualified name lookup: struct-qualified static function call", "[gen][name_lookup][qualified]") {
    // Calling a static member function via the qualified form StructName::func(args)
    // from within the same module (no explicit namespace block).
    auto jit = gen_jit(R"SRC(
        module __qname_struct__;

        struct math {
            static add(a : int, b : int) : int {
                return a + b;
            }
            static mul(a : int, b : int) : int {
                return a * b;
            }
        }

        test_add() : int {
            return math::add(3, 4);
        }

        test_mul() : int {
            return math::mul(3, 4);
        }
    )SRC");
    REQUIRE(jit);

    auto test_add = jit->lookup_symbol<int(*)()>("test_add");
    REQUIRE(test_add != nullptr);
    REQUIRE(test_add() == 7);

    auto test_mul = jit->lookup_symbol<int(*)()>("test_mul");
    REQUIRE(test_mul != nullptr);
    REQUIRE(test_mul() == 12);
}

// -----------------------------------------------------------------------------
// 2. Namespace-qualified call : ns::func(args) without root prefix
// -----------------------------------------------------------------------------
TEST_CASE("Qualified name lookup: namespace-qualified free function call", "[gen][name_lookup][qualified]") {
    // A function defined in an explicit child namespace is called via ns::func()
    // without the :: root prefix.
    auto jit = gen_jit(R"SRC(
        module __qname_ns__;

        namespace math {
            square(x : int) : int {
                return x * x;
            }
            cube(x : int) : int {
                return x * x * x;
            }
        }

        test_square() : int {
            return math::square(5);
        }

        test_cube() : int {
            return math::cube(3);
        }
    )SRC");
    REQUIRE(jit);

    auto test_square = jit->lookup_symbol<int(*)()>("test_square");
    REQUIRE(test_square != nullptr);
    REQUIRE(test_square() == 25);

    auto test_cube = jit->lookup_symbol<int(*)()>("test_cube");
    REQUIRE(test_cube != nullptr);
    REQUIRE(test_cube() == 27);
}

// -----------------------------------------------------------------------------
// 3. Deeply nested namespace-qualified call : ns1::ns2::func(args)
// -----------------------------------------------------------------------------
TEST_CASE("Qualified name lookup: deeply nested namespace-qualified call", "[gen][name_lookup][qualified]") {
    // A function defined two levels deep in namespaces is called with a
    // two-part qualified name (without root prefix).
    auto jit = gen_jit(R"SRC(
        module __qname_deep__;

        namespace geo {
            namespace d2 {
                area(w : int, h : int) : int {
                    return w * h;
                }
            }
        }

        test_area() : int {
            return geo::d2::area(6, 7);
        }
    )SRC");
    REQUIRE(jit);

    auto test_area = jit->lookup_symbol<int(*)()>("test_area");
    REQUIRE(test_area != nullptr);
    REQUIRE(test_area() == 42);
}

// -----------------------------------------------------------------------------
// 4. Root-prefix qualified call : ::module::func(args)
// -----------------------------------------------------------------------------
TEST_CASE("Qualified name lookup: root-prefix qualified function call", "[gen][name_lookup][qualified][root_prefix]") {
    // Calling a free function using the absolute qualified name (with :: prefix).
    auto jit = gen_jit(R"SRC(
        module __qname_root__;

        answer() : int {
            return 42;
        }

        test() : int {
            return ::__qname_root__::answer();
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

// -----------------------------------------------------------------------------
// 5. Root-prefix qualified call into a child namespace : ::module::ns::func()
// -----------------------------------------------------------------------------
TEST_CASE("Qualified name lookup: root-prefix into child namespace", "[gen][name_lookup][qualified][root_prefix]") {
    // Calling a function in a nested namespace using the fully-absolute name.
    auto jit = gen_jit(R"SRC(
        module __qname_root_ns__;

        namespace utils {
            double_it(x : int) : int {
                return x * 2;
            }
        }

        test() : int {
            return ::__qname_root_ns__::utils::double_it(21);
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

// -----------------------------------------------------------------------------
// 6. Qualified call to a member function via struct name + root prefix
// -----------------------------------------------------------------------------
TEST_CASE("Qualified name lookup: root-prefix struct-qualified static member call", "[gen][name_lookup][qualified][root_prefix]") {
    // Call a static member function using the fully-qualified absolute name.
    auto jit = gen_jit(R"SRC(
        module __qname_root_struct__;

        struct calc {
            static negate(x : int) : int {
                return 0 - x;
            }
        }

        test() : int {
            return ::__qname_root_struct__::calc::negate(21);
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == -21);
}

// -----------------------------------------------------------------------------
// 7. Qualified name shadowing: local unqualified vs. sibling-namespace qualified
// -----------------------------------------------------------------------------
TEST_CASE("Qualified name lookup: unqualified shadows sibling namespace function", "[gen][name_lookup][qualified]") {
    // There is a local function named 'value' in the root namespace AND
    // a function with the same short name in a child namespace.
    // An unqualified call must resolve to the root-level one;
    // a qualified call must reach the one in the child namespace.
    auto jit = gen_jit(R"SRC(
        module __qname_shadow__;

        namespace inner {
            value() : int { return 2; }
        }

        value() : int { return 1; }

        test_unqualified() : int {
            return value();
        }

        test_qualified() : int {
            return inner::value();
        }
    )SRC");
    REQUIRE(jit);

    auto test_unqualified = jit->lookup_symbol<int(*)()>("test_unqualified");
    REQUIRE(test_unqualified != nullptr);
    REQUIRE(test_unqualified() == 1);   // root-level value()

    auto test_qualified = jit->lookup_symbol<int(*)()>("test_qualified");
    REQUIRE(test_qualified != nullptr);
    REQUIRE(test_qualified() == 2);     // inner::value()
}

// -----------------------------------------------------------------------------
// 8. Cross-namespace call: function in ns A calls function in sibling ns B
// -----------------------------------------------------------------------------
TEST_CASE("Qualified name lookup: cross-namespace qualified call", "[gen][name_lookup][qualified]") {
    // A function defined inside namespace 'a' calls a function in sibling
    // namespace 'b' using the qualified name b::func().
    auto jit = gen_jit(R"SRC(
        module __qname_cross__;

        namespace b {
            triple(x : int) : int { return x * 3; }
        }

        namespace a {
            compute(x : int) : int {
                return b::triple(x);
            }
        }

        test() : int {
            return a::compute(7);
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 21);
}

// -----------------------------------------------------------------------------
// 9. Qualified struct instantiation + member call
// -----------------------------------------------------------------------------
TEST_CASE("Qualified name lookup: qualified struct type used for variable declaration", "[gen][name_lookup][qualified]") {
    // A struct defined in a child namespace is used as a type for a variable
    // declared in the root namespace, and its member function is called.
    auto jit = gen_jit(R"SRC(
        module __qname_struct_type__;

        namespace shapes {
            struct rect {
                w : int = 3;
                h : int = 4;
                area() : int { return w * h; }
            }
        }

        test() : int {
            r : shapes::rect;
            return r.area();
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 12);
}


// =============================================================================
// Absolute name lookup with omitted module namespace prefix
// i.e. ::func, ::struct::method, ::subns::func should resolve from the root
// of the current unit without requiring the module name.
// =============================================================================

// -----------------------------------------------------------------------------
// A. ::func() — absolute call to a free function, no module prefix
// -----------------------------------------------------------------------------
TEST_CASE("Absolute name lookup: ::func() without module prefix", "[gen][name_lookup][qualified][root_prefix]") {
    // ::answer() should resolve to the free function defined in __qname_abs__,
    // even though the call omits the module namespace prefix.
    auto jit = gen_jit(R"SRC(
        module __qname_abs__;

        answer() : int {
            return 42;
        }

        test() : int {
            return ::answer();
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

// -----------------------------------------------------------------------------
// B. ::struct::method() — absolute call to a static struct method, no module prefix
// -----------------------------------------------------------------------------
TEST_CASE("Absolute name lookup: ::struct::static_method() without module prefix", "[gen][name_lookup][qualified][root_prefix]") {
    // ::calc::negate() resolves to the static member function of struct calc
    // defined in the current module, without the module namespace in the path.
    auto jit = gen_jit(R"SRC(
        module __qname_abs_struct__;

        struct calc {
            static negate(x : int) : int {
                return 0 - x;
            }
        }

        test() : int {
            return ::calc::negate(21);
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == -21);
}

// -----------------------------------------------------------------------------
// C. ::subns::func() — absolute call into a child namespace, no module prefix
// -----------------------------------------------------------------------------
TEST_CASE("Absolute name lookup: ::subns::func() without module prefix", "[gen][name_lookup][qualified][root_prefix]") {
    // ::utils::double_it() resolves to the function in child namespace utils
    // without the module namespace in the path.
    auto jit = gen_jit(R"SRC(
        module __qname_abs_ns__;

        namespace utils {
            double_it(x : int) : int {
                return x * 2;
            }
        }

        test() : int {
            return ::utils::double_it(21);
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

// -----------------------------------------------------------------------------
// D. ::subns::struct::method() — absolute, namespace + struct, no module prefix
// -----------------------------------------------------------------------------
TEST_CASE("Absolute name lookup: ::subns::struct::static_method() without module prefix", "[gen][name_lookup][qualified][root_prefix]") {
    // ::shapes::rect::make() resolves through child namespace + struct,
    // without the module namespace in the path.
    auto jit = gen_jit(R"SRC(
        module __qname_abs_ns_struct__;

        namespace shapes {
            struct rect {
                static make() : int {
                    return 42;
                }
            }
        }

        test() : int {
            return ::shapes::rect::make();
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

// -----------------------------------------------------------------------------
// E. ::func() vs local func — absolute form must bypass shadowing
// -----------------------------------------------------------------------------
TEST_CASE("Absolute name lookup: ::func() bypasses local variable shadowing", "[gen][name_lookup][qualified][root_prefix]") {
    // Inside a member function where a local param 'value' exists,
    // ::value() must still resolve to the module-level free function,
    // not the local variable.
    auto jit = gen_jit(R"SRC(
        module __qname_abs_shadow__;

        value() : int { return 42; }

        test(value : int) : int {
            return ::value();
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)(int)>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test(99) == 42);
}

// -----------------------------------------------------------------------------
// F. ::subns1::subns2::func() — two-level child namespace, no module prefix
// -----------------------------------------------------------------------------
TEST_CASE("Absolute name lookup: ::subns1::subns2::func() two-level without module prefix", "[gen][name_lookup][qualified][root_prefix]") {
    // ::geo::d2::area() resolves into a two-level nested namespace
    // without needing the module name at the front.
    auto jit = gen_jit(R"SRC(
        module __qname_abs_deep__;

        namespace geo {
            namespace d2 {
                area(w : int, h : int) : int {
                    return w * h;
                }
            }
        }

        test() : int {
            return ::geo::d2::area(6, 7);
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

