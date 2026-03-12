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
 * Tests for K language dynamic array allocation: new T[N]{...} / delete.
 *
 * Phase 1: Parser tests for new T[N]{...} syntax
 * Phase 2-5: Model, resolver, codegen, end-to-end JIT tests
 * Bounds-check tests: runtime bounds checking for array subscript
 * Error code coverage:
 *   - Error 0x4221: array size expression not convertible to unsigned int
 *   - Error 0x4222: too many initializers
 *   - Error 0x4224: cannot convert init list element to array element type
 *   - Error 0x4226: abstract class element type
 *   - Error 0x4227: no matching explicit constructor for element
 *   - Error 0x4228: no matching single-param constructor for implicit element
 *   - Error 0x4229: cannot infer array size
 *   - Error 0x422A: brace init forbidden for dynamic-sized arrays
 */

#include <catch2/catch_all.hpp>

#include "../src/common/logger.hpp"
#include "../src/parse/parser.hpp"
#include "../src/model/model.hpp"
#include "../src/gen/generators.hpp"
#include "../src/gen/resolvers.hpp"
#include "../src/compiler.hpp"

#include "helpers.hpp"

// ─────────────────────────────────────────────────────────────────────────────
// Phase 1: Parser tests for new T[N]{...} syntax
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Parse new array — int[5] without init", "[parser][new-array]") {
    test_logger log;
    k::source src{"new int[5]"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_unary_expr();
    REQUIRE(expr);
    auto ne = std::dynamic_pointer_cast<k::parse::ast::new_expr>(expr);
    REQUIRE(ne);
    REQUIRE(ne->is_array == true);
    REQUIRE(ne->array_size_expr != nullptr);
    REQUIRE(ne->brace_init == nullptr);
    REQUIRE(ne->args.empty());
}

TEST_CASE("Parse new array — int[5]{1,2,3,4,5}", "[parser][new-array]") {
    test_logger log;
    k::source src{"new int[5]{1, 2, 3, 4, 5}"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_unary_expr();
    REQUIRE(expr);
    auto ne = std::dynamic_pointer_cast<k::parse::ast::new_expr>(expr);
    REQUIRE(ne);
    REQUIRE(ne->is_array == true);
    REQUIRE(ne->array_size_expr != nullptr);
    REQUIRE(ne->brace_init != nullptr);
    REQUIRE(ne->brace_init->elements.size() == 5);
    for (size_t i = 0; i < 5; ++i) {
        REQUIRE(ne->brace_init->elements[i] != nullptr);
    }
}

TEST_CASE("Parse new array — int[]{1,2,3} inferred size", "[parser][new-array]") {
    test_logger log;
    k::source src{"new int[]{1, 2, 3}"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_unary_expr();
    REQUIRE(expr);
    auto ne = std::dynamic_pointer_cast<k::parse::ast::new_expr>(expr);
    REQUIRE(ne);
    REQUIRE(ne->is_array == true);
    REQUIRE(ne->array_size_expr == nullptr);  // inferred
    REQUIRE(ne->brace_init != nullptr);
    REQUIRE(ne->brace_init->elements.size() == 3);
}

TEST_CASE("Parse new array — int[0]{} empty array", "[parser][new-array]") {
    test_logger log;
    k::source src{"new int[0]{}"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_unary_expr();
    REQUIRE(expr);
    auto ne = std::dynamic_pointer_cast<k::parse::ast::new_expr>(expr);
    REQUIRE(ne);
    REQUIRE(ne->is_array == true);
    REQUIRE(ne->array_size_expr != nullptr);
    REQUIRE(ne->brace_init != nullptr);
    REQUIRE(ne->brace_init->elements.size() == 0);
}

TEST_CASE("Parse new array — with empty slots", "[parser][new-array]") {
    test_logger log;
    k::source src{"new int[3]{1, , 3}"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_unary_expr();
    REQUIRE(expr);
    auto ne = std::dynamic_pointer_cast<k::parse::ast::new_expr>(expr);
    REQUIRE(ne);
    REQUIRE(ne->is_array == true);
    REQUIRE(ne->brace_init != nullptr);
    REQUIRE(ne->brace_init->elements.size() == 3);
    REQUIRE(ne->brace_init->elements[0] != nullptr);
    REQUIRE(ne->brace_init->elements[1] == nullptr);  // empty slot
    REQUIRE(ne->brace_init->elements[2] != nullptr);
}

TEST_CASE("Parse new array — with expressions", "[parser][new-array]") {
    test_logger log;
    k::source src{"new int[3]{1+2, 3*4, 10/2}"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_unary_expr();
    REQUIRE(expr);
    auto ne = std::dynamic_pointer_cast<k::parse::ast::new_expr>(expr);
    REQUIRE(ne);
    REQUIRE(ne->is_array == true);
    REQUIRE(ne->brace_init != nullptr);
    REQUIRE(ne->brace_init->elements.size() == 3);
    for (size_t i = 0; i < 3; ++i) {
        REQUIRE(ne->brace_init->elements[i] != nullptr);
    }
}

TEST_CASE("Parse new single object still works after array support", "[parser][new-array]") {
    test_logger log;
    k::source src{"new int(42)"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_unary_expr();
    REQUIRE(expr);
    auto ne = std::dynamic_pointer_cast<k::parse::ast::new_expr>(expr);
    REQUIRE(ne);
    REQUIRE(ne->is_array == false);
    REQUIRE(ne->args.size() == 1);
}

TEST_CASE("Parse new single object no args still works", "[parser][new-array]") {
    test_logger log;
    k::source src{"new Foo()"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_unary_expr();
    REQUIRE(expr);
    auto ne = std::dynamic_pointer_cast<k::parse::ast::new_expr>(expr);
    REQUIRE(ne);
    REQUIRE(ne->is_array == false);
    REQUIRE(ne->args.empty());
}

TEST_CASE("Parse new array — empty brackets without init", "[parser][new-array]") {
    test_logger log;
    k::source src{"new int[]"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_unary_expr();
    REQUIRE(expr);
    auto ne = std::dynamic_pointer_cast<k::parse::ast::new_expr>(expr);
    REQUIRE(ne);
    REQUIRE(ne->is_array == true);
    REQUIRE(ne->array_size_expr == nullptr);
    REQUIRE(ne->brace_init == nullptr);
}

// ─────────────────────────────────────────────────────────────────────────────
// Phase 2-5: End-to-end JIT tests for new T[N]{...} / delete
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("new int[3]{10,20,30} — read back values via subscript", "[gen][new-array][jit]") {
    auto jit = gen_jit_throws(R"SRC(
        module __na_prim__;
        test() : int {
            arr : int[3]! = new int[3]{10, 20, 30};
            r : int = arr[0] + arr[1] + arr[2];
            delete arr;
            return r;
        }
    )SRC", /*dump=*/false);
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN11__na_prim__4testEv");
    REQUIRE(fn);
    REQUIRE(fn() == 60);
}

TEST_CASE("new int[5]{1,2,3} — fewer inits, rest default-zero", "[gen][new-array][jit]") {
    auto jit = gen_jit(R"SRC(
        module __na_pad__;
        test() : int {
            arr : int[5]! = new int[5]{1, 2, 3};
            r : int = arr[0] + arr[1] + arr[2] + arr[3] + arr[4];
            delete arr;
            return r;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN10__na_pad__4testEv");
    REQUIRE(fn);
    REQUIRE(fn() == 6);  // 1+2+3+0+0
}

TEST_CASE("new int[]{10,20,30} — inferred size from init list", "[gen][new-array][jit]") {
    auto jit = gen_jit_throws(R"SRC(
        module __na_infer__;
        test() : int {
            arr : int[3]! = new int[]{10, 20, 30};
            r : int = arr[0] + arr[1] + arr[2];
            delete arr;
            return r;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN12__na_infer__4testEv");
    REQUIRE(fn);
    REQUIRE(fn() == 60);
}

TEST_CASE("new int[3]{1,,3} — empty slot defaults to 0", "[gen][new-array][jit]") {
    auto jit = gen_jit_throws(R"SRC(
        module __na_empty__;
        test() : int {
            arr : int[3]! = new int[3]{1, , 3};
            r : int = arr[0] + arr[1] + arr[2];
            delete arr;
            return r;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN12__na_empty__4testEv");
    REQUIRE(fn);
    REQUIRE(fn() == 4);  // 1+0+3
}

TEST_CASE("new int[3]{1+2, 3*4, 10/2} — expression elements", "[gen][new-array][jit]") {
    auto jit = gen_jit_throws(R"SRC(
        module __na_expr__;
        test() : int {
            arr : int[3]! = new int[3]{1+2, 3*4, 10/2};
            r : int = arr[0] + arr[1] + arr[2];
            delete arr;
            return r;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN11__na_expr__4testEv");
    REQUIRE(fn);
    REQUIRE(fn() == 20);  // 3+12+5
}

TEST_CASE("new int[0]{} — empty array", "[gen][new-array][jit]") {
    auto jit = gen_jit_throws(R"SRC(
        module __na_zero__;
        test() : int {
            arr : int[0]! = new int[0]{};
            delete arr;
            return 42;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN11__na_zero__4testEv");
    REQUIRE(fn);
    REQUIRE(fn() == 42);
}

TEST_CASE("new int[3]{} — no init, all default-zero", "[gen][new-array][jit]") {
    auto jit = gen_jit(R"SRC(
        module __na_noinit__;
        test() : int {
            arr : int[3]! = new int[3]{};
            r : int = arr[0] + arr[1] + arr[2];
            delete arr;
            return r;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN13__na_noinit__4testEv");
    REQUIRE(fn);
    REQUIRE(fn() == 0);
}

TEST_CASE("new array scope auto-cleanup — no explicit delete", "[gen][new-array][jit]") {
    auto jit = gen_jit(R"SRC(
        module __na_autoclean__;
        test() : int {
            arr : int[3]! = new int[3]{7, 8, 9};
            return arr[0] + arr[1] + arr[2];
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN16__na_autoclean__4testEv");
    REQUIRE(fn);
    REQUIRE(fn() == 24);
}

TEST_CASE("new struct array — ctor called for each element, dtor on delete", "[gen][new-array][jit]") {
    auto jit = gen_jit_throws(R"SRC(
        module __na_struct__;

        g_ctor : int = 0;
        g_dtor : int = 0;

        struct Item {
            val : int = 0;
            Item(v : int) {
                val = v;
                g_ctor = g_ctor + 1;
            }
            ~Item() {
                g_dtor = g_dtor + 1;
            }
        }

        get_ctor_count() : int { return g_ctor; }
        get_dtor_count() : int { return g_dtor; }

        test() : int {
            arr : Item[3]! = new Item[3]{Item(10), Item(20), Item(30)};
            r : int = arr[0].val + arr[1].val + arr[2].val;
            delete arr;
            return r;
        }
    )SRC", /*dump=*/false);
    REQUIRE(jit);

    auto fn = jit->lookup_symbol<int(*)()>("_KFN13__na_struct__4testEv");
    auto get_ctor = jit->lookup_symbol<int(*)()>("_KFN13__na_struct__14get_ctor_countEv");
    auto get_dtor = jit->lookup_symbol<int(*)()>("_KFN13__na_struct__14get_dtor_countEv");
    REQUIRE(fn);
    REQUIRE(get_ctor);
    REQUIRE(get_dtor);

    int r = fn();
    REQUIRE(r == 60);  // 10+20+30
    REQUIRE(get_ctor() == 3);  // 3 constructors called
    REQUIRE(get_dtor() == 3);  // 3 destructors called
}

TEST_CASE("new struct array — scope auto-cleanup calls all dtors", "[gen][new-array][jit]") {
    auto jit = gen_jit(R"SRC(
        module __na_st_auto__;

        g_dtor : int = 0;

        struct Widget {
            id : int = 0;
            Widget(i : int) { id = i; }
            ~Widget() { g_dtor = g_dtor + 1; }
        }

        get_dtor_count() : int { return g_dtor; }

        test() : int {
            arr : Widget[2]! = new Widget[2]{Widget(1), Widget(2)};
            r : int = arr[0].id + arr[1].id;
            return r;
        }
    )SRC");
    REQUIRE(jit);

    auto fn = jit->lookup_symbol<int(*)()>("_KFN14__na_st_auto__4testEv");
    auto get_dtor = jit->lookup_symbol<int(*)()>("_KFN14__na_st_auto__14get_dtor_countEv");
    REQUIRE(fn);
    REQUIRE(get_dtor);

    int r = fn();
    REQUIRE(r == 3);  // 1+2
    REQUIRE(get_dtor() == 2);  // 2 dtors at scope exit
}

TEST_CASE("new array too many inits — error", "[gen][new-array][error]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module __na_toomany__;
        test() : int {
            arr : int[2]! = new int[2]{1, 2, 3};
            return 0;
        }
    )SRC"));
}

TEST_CASE("new int[5] without brace init — default zero", "[gen][new-array][jit]") {
    auto jit = gen_jit(R"SRC(
        module __na_nobraces__;
        test() : int {
            arr : int[5]! = new int[5];
            r : int = arr[0] + arr[1] + arr[2] + arr[3] + arr[4];
            delete arr;
            return r;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN15__na_nobraces__4testEv");
    REQUIRE(fn);
    REQUIRE(fn() == 0);
}

TEST_CASE("Existing single-object new/delete still works", "[gen][new-array][jit]") {
    auto jit = gen_jit_throws(R"SRC(
        module __na_compat__;
        test() : int {
            p : int! = new int(42);
            v : int = *p;
            delete p;
            return v;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN13__na_compat__4testEv");
    REQUIRE(fn);
    REQUIRE(fn() == 42);
}

// ─────────────────────────────────────────────────────────────────────────────
// Additional coverage: error cases
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("new int[] — no size, no init — error", "[gen][new-array][error]") {
    // new T[] with no explicit size and no brace init list is ambiguous
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module __na_nosize__;
        test() : int {
            arr : int[0]! = new int[];
            return 0;
        }
    )SRC"));
}

TEST_CASE("new int[]{} — empty brace init — valid empty array", "[gen][new-array][jit]") {
    // new T[]{} with empty init list → valid empty array (0 elements)
    auto jit = gen_jit_throws(R"SRC(
        module __na_emptyinfer__;
        test() : int {
            arr : int[0]! = new int[]{};
            delete arr;
            return 42;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN17__na_emptyinfer__4testEv");
    REQUIRE(fn);
    REQUIRE(fn() == 42);
}

// ─────────────────────────────────────────────────────────────────────────────
// Additional coverage: struct arrays
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("new struct array — default ctor for all elements", "[gen][new-array][jit]") {
    auto jit = gen_jit_throws(R"SRC(
        module __na_defctor__;

        g_ctor : int = 0;
        g_dtor : int = 0;

        struct Thing {
            val : int = 0;
            Thing() {
                val = 99;
                g_ctor = g_ctor + 1;
            }
            ~Thing() {
                g_dtor = g_dtor + 1;
            }
        }

        get_ctor_count() : int { return g_ctor; }
        get_dtor_count() : int { return g_dtor; }

        test() : int {
            arr : Thing[3]! = new Thing[3]{};
            r : int = arr[0].val + arr[1].val + arr[2].val;
            delete arr;
            return r;
        }
    )SRC", /*dump=*/false);
    REQUIRE(jit);

    auto fn = jit->lookup_symbol<int(*)()>("_KFN14__na_defctor__4testEv");
    auto get_ctor = jit->lookup_symbol<int(*)()>("_KFN14__na_defctor__14get_ctor_countEv");
    auto get_dtor = jit->lookup_symbol<int(*)()>("_KFN14__na_defctor__14get_dtor_countEv");
    REQUIRE(fn);
    REQUIRE(get_ctor);
    REQUIRE(get_dtor);

    int r = fn();
    REQUIRE(r == 297);  // 99+99+99
    REQUIRE(get_ctor() == 3);
    REQUIRE(get_dtor() == 3);
}

TEST_CASE("new struct array — inferred size from init list", "[gen][new-array][jit]") {
    auto jit = gen_jit_throws(R"SRC(
        module __na_stinfer__;

        g_ctor : int = 0;

        struct Pair {
            x : int = 0;
            Pair(v : int) {
                x = v;
                g_ctor = g_ctor + 1;
            }
            ~Pair() {}
        }

        get_ctor_count() : int { return g_ctor; }

        test() : int {
            arr : Pair[2]! = new Pair[]{Pair(10), Pair(20)};
            r : int = arr[0].x + arr[1].x;
            delete arr;
            return r;
        }
    )SRC", /*dump=*/false);
    REQUIRE(jit);

    auto fn = jit->lookup_symbol<int(*)()>("_KFN14__na_stinfer__4testEv");
    auto get_ctor = jit->lookup_symbol<int(*)()>("_KFN14__na_stinfer__14get_ctor_countEv");
    REQUIRE(fn);
    REQUIRE(get_ctor);

    int r = fn();
    REQUIRE(r == 30);  // 10+20
    REQUIRE(get_ctor() == 2);
}

TEST_CASE("new struct array — mixed empty slots with default ctor", "[gen][new-array][jit]") {
    auto jit = gen_jit_throws(R"SRC(
        module __na_stmix__;

        g_ctor : int = 0;
        g_defctor : int = 0;

        struct Elem {
            v : int = 0;
            Elem() {
                v = 0;
                g_defctor = g_defctor + 1;
            }
            Elem(x : int) {
                v = x;
                g_ctor = g_ctor + 1;
            }
            ~Elem() {}
        }

        get_ctor_count() : int { return g_ctor; }
        get_defctor_count() : int { return g_defctor; }

        test() : int {
            arr : Elem[3]! = new Elem[3]{Elem(10), , Elem(30)};
            r : int = arr[0].v + arr[1].v + arr[2].v;
            delete arr;
            return r;
        }
    )SRC", /*dump=*/false);
    REQUIRE(jit);

    auto fn = jit->lookup_symbol<int(*)()>("_KFN12__na_stmix__4testEv");
    auto get_ctor = jit->lookup_symbol<int(*)()>("_KFN12__na_stmix__14get_ctor_countEv");
    auto get_defctor = jit->lookup_symbol<int(*)()>("_KFN12__na_stmix__17get_defctor_countEv");
    REQUIRE(fn);
    REQUIRE(get_ctor);
    REQUIRE(get_defctor);

    int r = fn();
    REQUIRE(r == 40);  // 10+0+30
    REQUIRE(get_ctor() == 2);     // 2 explicit ctors (Elem(10), Elem(30))
    REQUIRE(get_defctor() == 1);  // 1 default ctor for the empty slot
}

TEST_CASE("delete on null owner array — no crash", "[gen][new-array][jit]") {
    auto jit = gen_jit(R"SRC(
        module __na_delnull__;
        test() : int {
            arr : int[3]! = new int[3]{1, 2, 3};
            delete arr;
            delete arr;
            return 42;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN14__na_delnull__4testEv");
    REQUIRE(fn);
    REQUIRE(fn() == 42);  // double delete is a no-op (already null)
}

TEST_CASE("new byte[4]{1,2,3,4} — byte array", "[gen][new-array][jit]") {
    auto jit = gen_jit_throws(R"SRC(
        module __na_byte__;
        test() : int {
            arr : byte[4]! = new byte[4]{1, 2, 3, 4};
            r : int = arr[0] + arr[1] + arr[2] + arr[3];
            delete arr;
            return r;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN11__na_byte__4testEv");
    REQUIRE(fn);
    REQUIRE(fn() == 10);  // 1+2+3+4
}

TEST_CASE("new long[3]{100000, 200000, 300000} — long array", "[gen][new-array][jit]") {
    auto jit = gen_jit_throws(R"SRC(
        module __na_long__;
        test() : long {
            arr : long[3]! = new long[3]{100000, 200000, 300000};
            r : long = arr[0] + arr[1] + arr[2];
            delete arr;
            return r;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<long(*)()>("_KFN11__na_long__4testEv");
    REQUIRE(fn);
    REQUIRE(fn() == 600000L);
}

TEST_CASE("new int[1]{42} — single-element array", "[gen][new-array][jit]") {
    auto jit = gen_jit_throws(R"SRC(
        module __na_single__;
        test() : int {
            arr : int[1]! = new int[1]{42};
            r : int = arr[0];
            delete arr;
            return r;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN13__na_single__4testEv");
    REQUIRE(fn);
    REQUIRE(fn() == 42);
}

TEST_CASE("new struct array — dtors called in reverse order on delete", "[gen][new-array][jit]") {
    // Verify that destructors are called in reverse order (last element first)
    // g_order starts at a value larger than any id; each dtor checks that
    // the current id is not larger than g_order (monotonically non-increasing).
    auto jit = gen_jit_throws(R"SRC(
        module __na_revdtor__;

        g_order : int = 100;
        g_ok : int = 1;

        struct Tracked {
            id : int = 0;
            Tracked(i : int) { id = i; }
            ~Tracked() {
                if (id > g_order) {
                    g_ok = 0;
                }
                g_order = id;
            }
        }

        get_ok() : int { return g_ok; }

        test() : int {
            arr : Tracked[3]! = new Tracked[3]{Tracked(1), Tracked(2), Tracked(3)};
            delete arr;
            return 0;
        }
    )SRC", /*dump=*/false);
    REQUIRE(jit);

    auto fn = jit->lookup_symbol<int(*)()>("_KFN14__na_revdtor__4testEv");
    auto get_ok = jit->lookup_symbol<int(*)()>("_KFN14__na_revdtor__6get_okEv");
    REQUIRE(fn);
    REQUIRE(get_ok);

    fn();
    REQUIRE(get_ok() == 1);  // dtors were called 3→2→1, each time id <= g_order
}

TEST_CASE("new struct array — scope auto-cleanup with default ctors", "[gen][new-array][jit]") {
    auto jit = gen_jit(R"SRC(
        module __na_autodef__;

        g_dtor : int = 0;

        struct AutoItem {
            v : int = 0;
            AutoItem() { v = 7; }
            ~AutoItem() { g_dtor = g_dtor + 1; }
        }

        get_dtor_count() : int { return g_dtor; }

        test() : int {
            arr : AutoItem[2]! = new AutoItem[2]{};
            return arr[0].v + arr[1].v;
        }
    )SRC");
    REQUIRE(jit);

    auto fn = jit->lookup_symbol<int(*)()>("_KFN14__na_autodef__4testEv");
    auto get_dtor = jit->lookup_symbol<int(*)()>("_KFN14__na_autodef__14get_dtor_countEv");
    REQUIRE(fn);
    REQUIRE(get_dtor);

    int r = fn();
    REQUIRE(r == 14);  // 7+7
    REQUIRE(get_dtor() == 2);  // 2 dtors at scope exit
}

// ─────────────────────────────────────────────────────────────────────────────
// new T{} — bare brace init without array brackets
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Parse new int{} — bare brace empty array", "[parser][new-array]") {
    test_logger log;
    k::source src{"new int{}"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_unary_expr();
    REQUIRE(expr);
    auto ne = std::dynamic_pointer_cast<k::parse::ast::new_expr>(expr);
    REQUIRE(ne);
    REQUIRE(ne->is_array == true);
    REQUIRE(ne->array_size_expr == nullptr);  // no explicit size
    REQUIRE(ne->brace_init != nullptr);
    REQUIRE(ne->brace_init->elements.size() == 0);
}

TEST_CASE("Parse new int{1,2,3} — bare brace inferred size", "[parser][new-array]") {
    test_logger log;
    k::source src{"new int{1, 2, 3}"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_unary_expr();
    REQUIRE(expr);
    auto ne = std::dynamic_pointer_cast<k::parse::ast::new_expr>(expr);
    REQUIRE(ne);
    REQUIRE(ne->is_array == true);
    REQUIRE(ne->array_size_expr == nullptr);
    REQUIRE(ne->brace_init != nullptr);
    REQUIRE(ne->brace_init->elements.size() == 3);
    for (size_t i = 0; i < 3; ++i) {
        REQUIRE(ne->brace_init->elements[i] != nullptr);
    }
}

TEST_CASE("Parse new int{1, , 3} — bare brace with empty slots", "[parser][new-array]") {
    test_logger log;
    k::source src{"new int{1, , 3}"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_unary_expr();
    REQUIRE(expr);
    auto ne = std::dynamic_pointer_cast<k::parse::ast::new_expr>(expr);
    REQUIRE(ne);
    REQUIRE(ne->is_array == true);
    REQUIRE(ne->brace_init != nullptr);
    REQUIRE(ne->brace_init->elements.size() == 3);
    REQUIRE(ne->brace_init->elements[0] != nullptr);
    REQUIRE(ne->brace_init->elements[1] == nullptr);
    REQUIRE(ne->brace_init->elements[2] != nullptr);
}

TEST_CASE("new int{} — JIT: empty array, no crash", "[gen][new-array][jit]") {
    auto jit = gen_jit_throws(R"SRC(
        module __na_bare_empty__;
        test() : int {
            arr : int[0]! = new int{};
            delete arr;
            return 42;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN17__na_bare_empty__4testEv");
    REQUIRE(fn);
    REQUIRE(fn() == 42);
}

TEST_CASE("new int{10,20,30} — JIT: inferred size 3 from bare brace", "[gen][new-array][jit]") {
    auto jit = gen_jit_throws(R"SRC(
        module __na_bare_infer__;
        test() : int {
            arr : int[3]! = new int{10, 20, 30};
            r : int = arr[0] + arr[1] + arr[2];
            delete arr;
            return r;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN17__na_bare_infer__4testEv");
    REQUIRE(fn);
    REQUIRE(fn() == 60);
}

TEST_CASE("new int{1, , 3} — JIT: bare brace empty slot defaults to 0", "[gen][new-array][jit]") {
    auto jit = gen_jit_throws(R"SRC(
        module __na_bare_slot__;
        test() : int {
            arr : int[3]! = new int{1, , 3};
            r : int = arr[0] + arr[1] + arr[2];
            delete arr;
            return r;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN16__na_bare_slot__4testEv");
    REQUIRE(fn);
    REQUIRE(fn() == 4);  // 1+0+3
}


// =============================================================================
// Bounds-check tests (Phase 1 of bounds-check feature)
// =============================================================================

TEST_CASE("Bounds check: valid access on stack-allocated array", "[gen][bounds][array]") {
    auto jit = gen_jit_throws(R"SRC(
        module __bc_stack_ok__;
        test() : int {
            a : int[3];
            a[0] = 10;
            a[1] = 20;
            a[2] = 30;
            return a[0] + a[1] + a[2];
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN15__bc_stack_ok__4testEv");
    REQUIRE(fn);
    REQUIRE(fn() == 60);
}

TEST_CASE("Bounds check: valid access on owner array", "[gen][bounds][new-array]") {
    auto jit = gen_jit_throws(R"SRC(
        module __bc_own_ok__;
        test() : int {
            arr : int[3]! = new int[3]{10, 20, 30};
            r : int = arr[0] + arr[1] + arr[2];
            delete arr;
            return r;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN13__bc_own_ok__4testEv");
    REQUIRE(fn);
    REQUIRE(fn() == 60);
}

TEST_CASE("Bounds check: out-of-bounds on stack array aborts", "[gen][bounds][array][oob]") {
    auto res = build_and_exec(R"SRC(
        module __bc_stack_oob__;
        main() : int {
            a : int[3];
            a[0] = 1;
            a[3] = 99;
            return 0;
        }
    )SRC");
    REQUIRE(res.exit_code != 0);
}

TEST_CASE("Bounds check: out-of-bounds on owner array aborts", "[gen][bounds][new-array][oob]") {
    auto res = build_and_exec(R"SRC(
        module __bc_own_oob__;
        main() : int {
            arr : int[3]! = new int[3]{10, 20, 30};
            x : int = arr[5];
            delete arr;
            return x;
        }
    )SRC");
    REQUIRE(res.exit_code != 0);
}

TEST_CASE("Bounds check: negative index (unsigned wrap) on array aborts", "[gen][bounds][array][oob]") {
    auto res = build_and_exec(R"SRC(
        module __bc_neg_oob__;
        main() : int {
            a : int[3];
            i : int = -1;
            a[i] = 42;
            return 0;
        }
    )SRC");
    // -1 cast to unsigned wraps to a huge number > 3 → bounds check fires
    REQUIRE(res.exit_code != 0);
}

// =============================================================================
// Error code coverage for array new expressions
// =============================================================================

TEST_CASE("new array — dynamic size with brace init — error 0x422A", "[gen][new-array][error]") {
    // n is not a compile-time constant → dynamic size; brace init {} is forbidden → error 0x422A
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module __na_err_nonconst__;
        test() : int {
            n : int = 5;
            arr : int[5]! = new int[n]{};
            return 0;
        }
    )SRC"));
}

TEST_CASE("new array — abstract class element — error 0x4226", "[gen][new-array][error]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module __na_err_abstract__;

        abstract class Shape {
            Shape() {}
            abstract area() : int;
        }

        test() : int {
            arr : Shape[2]! = new Shape[2]{};
            return 0;
        }
    )SRC"));
}

TEST_CASE("new struct array — no matching explicit ctor — error 0x4227", "[gen][new-array][error]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module __na_err_noctor__;

        struct Pair {
            x : int = 0;
            y : int = 0;
            Pair(a : int, b : int) { x = a; y = b; }
        }

        test() : int {
            arr : Pair[1]! = new Pair[1]{Pair(1, 2, 3)};
            return 0;
        }
    )SRC"));
}

TEST_CASE("new struct array — no matching single-param ctor — error 0x4228", "[gen][new-array][error]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module __na_err_nodefctor__;

        struct TwoArgs {
            x : int = 0;
            y : int = 0;
            TwoArgs(a : int, b : int) { x = a; y = b; }
        }

        test() : int {
            arr : TwoArgs[1]! = new TwoArgs[1]{42};
            return 0;
        }
    )SRC"));
}

TEST_CASE("new dynamic array — brace init forbidden — error 0x422A", "[gen][new-array][error]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module __na_err_dynbrace__;
        test() : int {
            n : int = 5;
            arr : int[]! = new int[n]{};
            return 0;
        }
    )SRC"));
}

// =============================================================================
// Dynamic-sized array allocation tests
// =============================================================================

TEST_CASE("new int[n] — dynamic primitive array, default-init", "[gen][new-array][dynamic][jit]") {
    auto jit = gen_jit_throws(R"SRC(
        module __na_dyn_prim__;
        test() : int {
            n : unsigned int = 5;
            arr : int[]! = new int[n];
            r : int = arr[0] + arr[1] + arr[2] + arr[3] + arr[4];
            delete arr;
            return r;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN15__na_dyn_prim__4testEv");
    REQUIRE(fn);
    REQUIRE(fn() == 0);  // all default-initialized to 0
}

TEST_CASE("new int[n] — dynamic array, write and read back", "[gen][new-array][dynamic][jit]") {
    auto jit = gen_jit_throws(R"SRC(
        module __na_dyn_rw__;
        test() : int {
            n : unsigned int = 3;
            arr : int[]! = new int[n];
            arr[0] = 10;
            arr[1] = 20;
            arr[2] = 30;
            r : int = arr[0] + arr[1] + arr[2];
            delete arr;
            return r;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN13__na_dyn_rw__4testEv");
    REQUIRE(fn);
    REQUIRE(fn() == 60);
}

TEST_CASE("new int[n] — size from signed int (implicit cast)", "[gen][new-array][dynamic][jit]") {
    auto jit = gen_jit_throws(R"SRC(
        module __na_dyn_signed__;
        test() : int {
            n : int = 4;
            arr : int[]! = new int[n];
            arr[0] = 1;
            arr[1] = 2;
            arr[2] = 3;
            arr[3] = 4;
            r : int = arr[0] + arr[1] + arr[2] + arr[3];
            delete arr;
            return r;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN17__na_dyn_signed__4testEv");
    REQUIRE(fn);
    REQUIRE(fn() == 10);
}

TEST_CASE("new int[expr] — size from expression", "[gen][new-array][dynamic][jit]") {
    auto jit = gen_jit_throws(R"SRC(
        module __na_dyn_expr__;
        test(n : unsigned int) : int {
            arr : int[]! = new int[n];
            i : unsigned int = 0;
            arr[i] = 42;
            r : int = arr[0];
            delete arr;
            return r;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)(unsigned int)>("_KFN15__na_dyn_expr__4testEj");
    REQUIRE(fn);
    REQUIRE(fn(1) == 42);
    REQUIRE(fn(5) == 42);
}

TEST_CASE("new Struct[n] — dynamic struct array with default ctor", "[gen][new-array][dynamic][jit]") {
    auto jit = gen_jit_throws(R"SRC(
        module __na_dyn_struct__;

        struct Point {
            x : int = 0;
            y : int = 0;
            Point() { x = 7; y = 3; }
        }

        test() : int {
            n : unsigned int = 3;
            arr : Point[]! = new Point[n];
            r : int = arr[0].x + arr[1].x + arr[2].y;
            delete arr;
            return r;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN17__na_dyn_struct__4testEv");
    REQUIRE(fn);
    REQUIRE(fn() == 17);  // 7 + 7 + 3
}

TEST_CASE("new Struct[n] — dynamic struct array with destructor", "[gen][new-array][dynamic][jit]") {
    auto jit = gen_jit_throws(R"SRC(
        module __na_dyn_dtor__;

        counter : int = 0;

        struct Tracked {
            Tracked() { counter = counter + 1; }
            ~Tracked() { counter = counter - 1; }
        }

        test() : int {
            n : unsigned int = 5;
            arr : Tracked[]! = new Tracked[n];
            c1 : int = counter;
            delete arr;
            c2 : int = counter;
            return c1 * 10 + c2;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN15__na_dyn_dtor__4testEv");
    REQUIRE(fn);
    REQUIRE(fn() == 50);  // c1=5, c2=0 → 5*10+0 = 50
}

// =============================================================================
// Additional coverage: edge cases
// =============================================================================

TEST_CASE("new int[0] — zero-size static array without braces", "[gen][new-array][jit]") {
    auto jit = gen_jit_throws(R"SRC(
        module __na_zero_nobraces__;
        test() : int {
            arr : int[0]! = new int[0];
            delete arr;
            return 42;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN20__na_zero_nobraces__4testEv");
    REQUIRE(fn);
    REQUIRE(fn() == 42);
}

TEST_CASE("new Struct[n] — dynamic struct array scope auto-cleanup", "[gen][new-array][dynamic][jit]") {
    auto jit = gen_jit(R"SRC(
        module __na_dyn_autoclean__;

        g_dtor : int = 0;

        struct AutoWidget {
            id : int = 0;
            AutoWidget() { id = 7; }
            ~AutoWidget() { g_dtor = g_dtor + 1; }
        }

        get_dtor_count() : int { return g_dtor; }

        test() : int {
            n : unsigned int = 3;
            arr : AutoWidget[]! = new AutoWidget[n];
            return arr[0].id + arr[1].id + arr[2].id;
        }
    )SRC");
    REQUIRE(jit);

    auto fn = jit->lookup_symbol<int(*)()>("_KFN20__na_dyn_autoclean__4testEv");
    auto get_dtor = jit->lookup_symbol<int(*)()>("_KFN20__na_dyn_autoclean__14get_dtor_countEv");
    REQUIRE(fn);
    REQUIRE(get_dtor);

    int r = fn();
    REQUIRE(r == 21);  // 7+7+7
    REQUIRE(get_dtor() == 3);  // 3 dtors at scope exit (implicit delete)
}

TEST_CASE("delete on null owner dynamic array — no crash", "[gen][new-array][dynamic][jit]") {
    auto jit = gen_jit(R"SRC(
        module __na_dyn_delnull__;
        test() : int {
            n : unsigned int = 2;
            arr : int[]! = new int[n];
            arr[0] = 10;
            arr[1] = 20;
            delete arr;
            delete arr;
            return 42;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN18__na_dyn_delnull__4testEv");
    REQUIRE(fn);
    REQUIRE(fn() == 42);  // double delete is a no-op (already null)
}

TEST_CASE("Parse new array — complex expression as size", "[parser][new-array]") {
    test_logger log;
    k::source src{"new int[a + b]"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_unary_expr();
    REQUIRE(expr);
    auto ne = std::dynamic_pointer_cast<k::parse::ast::new_expr>(expr);
    REQUIRE(ne);
    REQUIRE(ne->is_array == true);
    REQUIRE(ne->array_size_expr != nullptr);
    // The size expression should be a binary expression (a + b)
    auto binexpr = std::dynamic_pointer_cast<k::parse::ast::binary_expression>(ne->array_size_expr);
    REQUIRE(binexpr);
    REQUIRE(ne->brace_init == nullptr);
}

TEST_CASE("Bounds check: out-of-bounds on dynamic owner array aborts", "[gen][bounds][new-array][dynamic][oob]") {
    auto res = build_and_exec(R"SRC(
        module __bc_dyn_oob__;
        main() : int {
            n : unsigned int = 3;
            arr : int[]! = new int[n];
            arr[0] = 10;
            x : int = arr[5];
            delete arr;
            return x;
        }
    )SRC");
    REQUIRE(res.exit_code != 0);
}

// =============================================================================
// Additional error code coverage
// =============================================================================

TEST_CASE("new array — struct as size expression — error 0x4221", "[gen][new-array][error]") {
    // A struct value cannot be converted to unsigned int → error 0x4221
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module __na_err_sizetyp__;

        struct Foo {
            x : int = 0;
            Foo() {}
        }

        test() : int {
            f : Foo;
            arr : int[]! = new int[f];
            return 0;
        }
    )SRC"));
}

TEST_CASE("new array — struct value in primitive init list — error 0x4224", "[gen][new-array][error]") {
    // A struct value cannot be converted to the primitive element type → error 0x4224
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module __na_err_elemtyp__;

        struct Bar {
            x : int = 0;
            Bar() {}
        }

        test() : int {
            b : Bar;
            arr : int[1]! = new int[1]{b};
            return 0;
        }
    )SRC"));
}

