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
 * Named Return Variable tests for the K language.
 *
 * Syntax: func(params) retVarName : RetType [ Initialiser ] { body }
 *
 * Validates that:
 * - Named return variables are correctly parsed and code-generated
 * - Primitive types return the correct value via implicit return
 * - Aggregate types get guaranteed NRVO (1 ctor, 1 dtor)
 * - Bare return; works as intermediate return
 * - return expr; emits a warning and works as assignment + return
 * - Interaction with owners, pointers
 * - Non-regression of existing return syntax
 *
 * Categories:
 *   NR-1: Primitive types — implicit return at end of function
 *   NR-2: Primitive types — modification in body
 *   NR-3: Primitive types — bare return; in branches
 *   NR-4: Aggregate types — guaranteed NRVO
 *   NR-5: Aggregate types — modification + NRVO
 *   NR-6: Aggregate types — conditional bare return
 *   NR-7: Default-constructed aggregate
 *   NR-8: Named return + other locals destruction
 *   NR-9: Named return in member function
 *   NR-10: Return expr with named return (warning, assignment semantics)
 *   NR-11: Non-regression — classic return unchanged
 *   NR-12: Owner type named return
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

// =============================================================================
// Category NR-1: Primitive — implicit return at end of function
// =============================================================================

TEST_CASE("NR-1: Named return int — implicit return", "[gen][named-return][nr1]") {
    auto jit = gen_jit(R"SRC(
        module __nr1_int__;
        foo() r : int = 42 { }
        test() : int { return foo(); }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 42);
}

TEST_CASE("NR-1: Named return bool — implicit return", "[gen][named-return][nr1]") {
    auto jit = gen_jit(R"SRC(
        module __nr1_bool__;
        foo() r : bool = true { }
        test() : int { if(foo()) return 1; return 0; }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 1);
}

TEST_CASE("NR-1: Named return double — implicit return", "[gen][named-return][nr1]") {
    auto jit = gen_jit(R"SRC(
        module __nr1_double__;
        foo() r : double = 3.14d { }
        test() : double { return foo(); }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<double(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == Catch::Approx(3.14));
}

// =============================================================================
// Category NR-2: Primitive — modification in body
// =============================================================================

TEST_CASE("NR-2: Named return int — modified in body", "[gen][named-return][nr2]") {
    auto jit = gen_jit(R"SRC(
        module __nr2_mod__;
        foo() r : int = 0 { r = 42; }
        test() : int { return foo(); }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 42);
}

TEST_CASE("NR-2: Named return int — init from parameter", "[gen][named-return][nr2]") {
    auto jit = gen_jit(R"SRC(
        module __nr2_param__;
        foo(x : int) r : int = x { ++r; }
        test() : int { return foo(41); }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 42);
}

// =============================================================================
// Category NR-3: Primitive — bare return; in branches
// =============================================================================

TEST_CASE("NR-3: Named return int — bare return in if/else", "[gen][named-return][nr3]") {
    auto jit = gen_jit(R"SRC(
        module __nr3_branch__;
        foo(b : bool) r : int = 0 {
            if (b) {
                r = 1;
                return;
            }
            r = 2;
        }
        test_true() : int { return foo(true); }
        test_false() : int { return foo(false); }
    )SRC");
    REQUIRE(jit);
    auto test_true = jit->lookup_symbol<int(*)()>("test_true");
    auto test_false = jit->lookup_symbol<int(*)()>("test_false");
    REQUIRE(test_true != nullptr);
    REQUIRE(test_false != nullptr);
    CHECK(test_true() == 1);
    CHECK(test_false() == 2);
}

TEST_CASE("NR-3: Named return int — bare return in loop", "[gen][named-return][nr3]") {
    auto jit = gen_jit(R"SRC(
        module __nr3_loop__;
        find_first_positive(a : int, b : int, c : int) r : int = 0 {
            if (a > 0) { r = a; return; }
            if (b > 0) { r = b; return; }
            if (c > 0) { r = c; return; }
        }
        test() : int { return find_first_positive(0 - 1, 42, 99); }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 42);
}

// =============================================================================
// Category NR-4: Aggregate — guaranteed NRVO
// =============================================================================

TEST_CASE("NR-4: Named return struct — guaranteed NRVO (1 ctor, 1 dtor)", "[gen][named-return][nr4]") {
    auto jit = gen_jit(R"SRC(
        module __nr4_nrvo__;

        g_ctors : int = 0;
        g_dtors : int = 0;

        struct Obj {
            val : int;
            Obj(v : int) : val(v) { ++g_ctors; }
            ~Obj() { ++g_dtors; }
        }

        make(v : int) r : Obj(v) { }

        test() : int {
            o : Obj = make(42);
            return o.val;
        }

        get_ctors() : int { return g_ctors; }
        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 42);

    auto get_ctors = jit->lookup_symbol<int(*)()>("get_ctors");
    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_ctors != nullptr);
    REQUIRE(get_dtors != nullptr);
    // Guaranteed NRVO: exactly 1 ctor (directly into caller's destination), 1 dtor
    CHECK(get_ctors() == 1);
    CHECK(get_dtors() == 1);
}

// =============================================================================
// Category NR-5: Aggregate — modification + NRVO
// =============================================================================

TEST_CASE("NR-5: Named return struct — modified in body, NRVO", "[gen][named-return][nr5]") {
    auto jit = gen_jit(R"SRC(
        module __nr5_mod_nrvo__;

        g_ctors : int = 0;
        g_dtors : int = 0;

        struct Obj {
            val : int;
            Obj(v : int) : val(v) { ++g_ctors; }
            ~Obj() { ++g_dtors; }
        }

        make(v : int) r : Obj(v) {
            r.val = r.val + 1;
        }

        test() : int {
            o : Obj = make(41);
            return o.val;
        }

        get_ctors() : int { return g_ctors; }
        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 42);

    auto get_ctors = jit->lookup_symbol<int(*)()>("get_ctors");
    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_ctors != nullptr);
    REQUIRE(get_dtors != nullptr);
    CHECK(get_ctors() == 1);
    CHECK(get_dtors() == 1);
}

// =============================================================================
// Category NR-6: Aggregate — conditional bare return + NRVO
// =============================================================================

TEST_CASE("NR-6: Named return struct — conditional bare return, NRVO", "[gen][named-return][nr6]") {
    auto jit = gen_jit(R"SRC(
        module __nr6_cond_nrvo__;

        g_ctors : int = 0;
        g_dtors : int = 0;

        struct Obj {
            val : int;
            Obj(v : int) : val(v) { ++g_ctors; }
            ~Obj() { ++g_dtors; }
        }

        factory(b : bool) r : Obj(0) {
            if (b) {
                r.val = 1;
                return;
            }
            r.val = 2;
        }

        test_true() : int {
            o : Obj = factory(true);
            return o.val;
        }

        test_false() : int {
            o : Obj = factory(false);
            return o.val;
        }

        get_ctors() : int { return g_ctors; }
        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test_true = jit->lookup_symbol<int(*)()>("test_true");
    auto test_false = jit->lookup_symbol<int(*)()>("test_false");
    REQUIRE(test_true != nullptr);
    REQUIRE(test_false != nullptr);
    CHECK(test_true() == 1);
    CHECK(test_false() == 2);

    auto get_ctors = jit->lookup_symbol<int(*)()>("get_ctors");
    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_ctors != nullptr);
    REQUIRE(get_dtors != nullptr);
    // 2 calls total: each should be 1 ctor + 1 dtor = 2+2
    CHECK(get_ctors() == 2);
    CHECK(get_dtors() == 2);
}

// =============================================================================
// Category NR-7: Default-constructed aggregate
// =============================================================================

TEST_CASE("NR-7: Named return struct — default-constructed", "[gen][named-return][nr7]") {
    auto jit = gen_jit(R"SRC(
        module __nr7_default__;

        g_ctors : int = 0;
        g_dtors : int = 0;

        struct Obj {
            val : int;
            Obj() : val(99) { ++g_ctors; }
            ~Obj() { ++g_dtors; }
        }

        make() r : Obj { }

        test() : int {
            o : Obj = make();
            return o.val;
        }

        get_ctors() : int { return g_ctors; }
        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 99);

    auto get_ctors = jit->lookup_symbol<int(*)()>("get_ctors");
    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_ctors != nullptr);
    REQUIRE(get_dtors != nullptr);
    CHECK(get_ctors() == 1);
    CHECK(get_dtors() == 1);
}

// =============================================================================
// Category NR-8: Named return + other locals destruction
// =============================================================================

TEST_CASE("NR-8: Named return struct — other locals are destroyed", "[gen][named-return][nr8]") {
    auto jit = gen_jit(R"SRC(
        module __nr8_locals__;

        g_ctors : int = 0;
        g_dtors : int = 0;

        struct Obj {
            val : int;
            Obj(v : int) : val(v) { ++g_ctors; }
            ~Obj() { ++g_dtors; }
        }

        make(v : int) r : Obj(v) {
            other : Obj(100);
            r.val = r.val + other.val;
        }

        test() : int {
            o : Obj = make(1);
            return o.val;
        }

        get_ctors() : int { return g_ctors; }
        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 101);

    auto get_ctors = jit->lookup_symbol<int(*)()>("get_ctors");
    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_ctors != nullptr);
    REQUIRE(get_dtors != nullptr);
    // r: 1 ctor, 1 dtor; other: 1 ctor, 1 dtor; result o: same as r via NRVO
    // Total: 2 ctors (r + other), 2 dtors (other + o/r)
    CHECK(get_ctors() == 2);
    CHECK(get_dtors() == 2);
}

// =============================================================================
// Category NR-9: Named return in member function
// =============================================================================

TEST_CASE("NR-9: Named return in member function", "[gen][named-return][nr9]") {
    auto jit = gen_jit(R"SRC(
        module __nr9_member__;

        struct Helper {
            base : int;
            Helper(b : int) : base(b) {}
            compute(x : int) r : int = base {
                r += x;
            }
        }

        test() : int {
            h : Helper(10);
            return h.compute(32);
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 42);
}

// =============================================================================
// Category NR-10: Return expr with named return (warning, assignment)
// =============================================================================

TEST_CASE("NR-10: Named return int — return expr assigns to named var", "[gen][named-return][nr10]") {
    // return expr; with named return = assigns to r, then returns
    auto jit = gen_jit(R"SRC(
        module __nr10_ret_expr__;
        foo() r : int = 0 {
            return 42;
        }
        test() : int { return foo(); }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 42);
}

// =============================================================================
// Category NR-11: Non-regression — classic return unchanged
// =============================================================================

TEST_CASE("NR-11: Classic return — no named return, unchanged", "[gen][named-return][nr11]") {
    auto jit = gen_jit(R"SRC(
        module __nr11_classic__;
        foo(x : int) : int { return x + 1; }
        test() : int { return foo(41); }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 42);
}

TEST_CASE("NR-11: Classic struct return — NRVO still works", "[gen][named-return][nr11]") {
    auto jit = gen_jit(R"SRC(
        module __nr11_classic_nrvo__;

        g_ctors : int = 0;
        g_dtors : int = 0;

        struct Obj {
            val : int;
            Obj(v : int) : val(v) { ++g_ctors; }
            ~Obj() { ++g_dtors; }
        }

        make(v : int) : Obj {
            o : Obj(v);
            return o;
        }

        test() : int {
            r : Obj = make(42);
            return r.val;
        }

        get_ctors() : int { return g_ctors; }
        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 42);

    auto get_ctors = jit->lookup_symbol<int(*)()>("get_ctors");
    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_ctors != nullptr);
    REQUIRE(get_dtors != nullptr);
    CHECK(get_ctors() == 1);
    CHECK(get_dtors() == 1);
}

// =============================================================================
// Category NR-12: Named return with chained member access
// =============================================================================

TEST_CASE("NR-12: Named return struct — chained member access on result", "[gen][named-return][nr12]") {
    auto jit = gen_jit(R"SRC(
        module __nr12_chain__;

        struct Point {
            x : int;
            y : int;
            Point(a : int, b : int) : x(a), y(b) {}
        }

        make(v : int) r : Point(v, v * 2) { }

        test() : int {
            return make(21).y;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 42);
}

// =============================================================================
// Category NR-13: Named return struct — bare return with other local cleanup
// =============================================================================

TEST_CASE("NR-13: Named return struct — bare return; destroys locals but not named ret", "[gen][named-return][nr13]") {
    auto jit = gen_jit(R"SRC(
        module __nr13_bare_cleanup__;

        g_ctors : int = 0;
        g_dtors : int = 0;

        struct Obj {
            val : int;
            Obj(v : int) : val(v) { ++g_ctors; }
            ~Obj() { ++g_dtors; }
        }

        make(v : int, early : bool) r : Obj(v) {
            other : Obj(200);
            if (early) {
                r.val = r.val + other.val;
                return;
            }
            r.val = r.val + 1;
        }

        test() : int {
            o : Obj = make(10, true);
            return o.val;
        }

        get_ctors() : int { return g_ctors; }
        get_dtors() : int { return g_dtors; }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 210);

    auto get_ctors = jit->lookup_symbol<int(*)()>("get_ctors");
    auto get_dtors = jit->lookup_symbol<int(*)()>("get_dtors");
    REQUIRE(get_ctors != nullptr);
    REQUIRE(get_dtors != nullptr);
    // r: 1 ctor (NRVO into o); other: 1 ctor, 1 dtor (destroyed at return;)
    // o: 1 dtor (at test() exit)
    CHECK(get_ctors() == 2);
    CHECK(get_dtors() == 2);
}

// =============================================================================
// Category NR-14: Named return with multi-arg constructor
// =============================================================================

TEST_CASE("NR-14: Named return struct — multi-arg constructor init", "[gen][named-return][nr14]") {
    auto jit = gen_jit(R"SRC(
        module __nr14_multi_arg__;

        struct Pair {
            a : int;
            b : int;
            Pair(x : int, y : int) : a(x), b(y) {}
        }

        make() r : Pair(10, 32) { }

        test() : int {
            p : Pair = make();
            return p.a + p.b;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 42);
}

// =============================================================================
// Category NR-15: Named return in operator function
// =============================================================================

TEST_CASE("NR-15: Named return in operator overload", "[gen][named-return][nr15]") {
    auto jit = gen_jit(R"SRC(
        module __nr15_operator__;

        struct Vec {
            x : int;
            y : int;
            Vec(a : int, b : int) : x(a), y(b) {}
            operator +(other : Vec) r : Vec(x + other.x, y + other.y) { }
        }

        test() : int {
            a : Vec(10, 20);
            b : Vec(1, 12);
            c : Vec = a + b;
            return c.x + c.y;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 43);  // (10+1) + (20+12) = 11 + 32 = 43
}

// =============================================================================
// Category NR-16: Void function non-regression
// =============================================================================

TEST_CASE("NR-16: Void function with bare return — non-regression", "[gen][named-return][nr16]") {
    auto jit = gen_jit(R"SRC(
        module __nr16_void__;
        g_val : int = 0;
        set_val() { g_val = 42; return; }
        test() : int { set_val(); return g_val; }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 42);
}

TEST_CASE("NR-16: Void function without return — non-regression", "[gen][named-return][nr16]") {
    auto jit = gen_jit(R"SRC(
        module __nr16_void_no_ret__;
        g_val : int = 0;
        set_val() { g_val = 42; }
        test() : int { set_val(); return g_val; }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 42);
}

// =============================================================================
// Category NR-17: Named return struct with method call on named var
// =============================================================================

TEST_CASE("NR-17: Named return struct — method call on named var", "[gen][named-return][nr17]") {
    auto jit = gen_jit(R"SRC(
        module __nr17_method_call__;

        struct Counter {
            val : int;
            Counter(v : int) : val(v) {}
            increment() { ++val; }
            get() : int { return val; }
        }

        make(v : int) r : Counter(v) {
            r.increment();
            r.increment();
            r.increment();
        }

        test() : int {
            c : Counter = make(39);
            return c.get();
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 42);
}

// =============================================================================
// Parser-level tests (in this file for convenience)
// =============================================================================

TEST_CASE("Parse named return — basic", "[parser][named-return]") {
    test_logger log;
    k::source src{"make() r : int = 42 { }"};
    k::parse::parser parser(log, src);
    auto decl = parser.parse_function_decl();
    REQUIRE(decl);
    REQUIRE(decl->has_named_return == true);
    REQUIRE(decl->return_var_name.has_value());
    REQUIRE(std::string{decl->return_var_name->content} == "r");
    REQUIRE(decl->type); // return type should be int
    REQUIRE(decl->return_var_init_expr); // = 42
    REQUIRE(decl->return_var_is_ctor_init == false);
    REQUIRE(decl->content); // body block
}

TEST_CASE("Parse named return — constructor init", "[parser][named-return]") {
    test_logger log;
    k::source src{"make() r : Obj(42) { }"};
    k::parse::parser parser(log, src);
    auto decl = parser.parse_function_decl();
    REQUIRE(decl);
    REQUIRE(decl->has_named_return == true);
    REQUIRE(decl->return_var_name.has_value());
    REQUIRE(std::string{decl->return_var_name->content} == "r");
    REQUIRE(decl->type); // return type should be Obj
    REQUIRE(decl->return_var_init_expr); // (42)
    REQUIRE(decl->return_var_is_ctor_init == true);
}

TEST_CASE("Parse named return — no init (default constructed)", "[parser][named-return]") {
    test_logger log;
    k::source src{"make() r : Obj { }"};
    k::parse::parser parser(log, src);
    auto decl = parser.parse_function_decl();
    REQUIRE(decl);
    REQUIRE(decl->has_named_return == true);
    REQUIRE(decl->return_var_name.has_value());
    REQUIRE(std::string{decl->return_var_name->content} == "r");
    REQUIRE(decl->type);
    REQUIRE_FALSE(decl->return_var_init_expr); // no init
}

TEST_CASE("Parse classic return type — non-regression", "[parser][named-return]") {
    test_logger log;
    k::source src{"make() : int { return 0; }"};
    k::parse::parser parser(log, src);
    auto decl = parser.parse_function_decl();
    REQUIRE(decl);
    REQUIRE(decl->has_named_return == false);
    REQUIRE_FALSE(decl->return_var_name.has_value());
    REQUIRE(decl->type); // return type should be int
}

TEST_CASE("Parse classic return type — single identifier type", "[parser][named-return]") {
    test_logger log;
    k::source src{"make() : Result { }"};
    k::parse::parser parser(log, src);
    auto decl = parser.parse_function_decl();
    REQUIRE(decl);
    REQUIRE(decl->has_named_return == false);
    REQUIRE(decl->type); // return type should be Result
}

TEST_CASE("Parse named return — with parameters", "[parser][named-return]") {
    test_logger log;
    k::source src{"make(a : int, b : int) r : int = a { }"};
    k::parse::parser parser(log, src);
    auto decl = parser.parse_function_decl();
    REQUIRE(decl);
    REQUIRE(decl->has_named_return == true);
    REQUIRE(decl->params.size() == 2);
    REQUIRE(std::string{decl->return_var_name->content} == "r");
}

TEST_CASE("Parse named return — destructor rejected", "[parser][named-return]") {
    test_logger log;
    k::source src{"~Foo() r : int { }"};
    k::parse::parser parser(log, src);
    REQUIRE_THROWS(parser.parse_function_decl());
}

TEST_CASE("Parse named return — abstract rejected", "[parser][named-return]") {
    test_logger log;
    k::source src{"abstract make() r : int;"};
    k::parse::parser parser(log, src);
    REQUIRE_THROWS(parser.parse_function_decl());
}

