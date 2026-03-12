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
 * Tests for subscript operator '[]' through all indirection types.
 *
 * The subscript operator must work uniformly on arrays accessed through
 * any indirection: reference (&), pointer (*), link (~), pinned (^),
 * and owner (!).
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
// Regression: subscript through reference (&) — already worked
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Subscript through reference — read", "[gen][subscript-indirection]") {
    auto jit = gen_jit(R"SRC(
        module test;

        get(a : int[3]&, i : int) : int {
            return a[i];
        }

        test() : int {
            arr : int[3]{10, 20, 30};
            return get(arr, 1);
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 20);
}

TEST_CASE("Subscript through reference — write", "[gen][subscript-indirection]") {
    auto jit = gen_jit(R"SRC(
        module test;

        set(a : int[3]&, i : int, v : int) {
            a[i] = v;
        }

        test() : int {
            arr : int[3]{10, 20, 30};
            set(arr, 1, 99);
            return arr[1];
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 99);
}

// ─────────────────────────────────────────────────────────────────────────────
// Regression: subscript through owner (!) — already worked
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Subscript through owner — read", "[gen][subscript-indirection]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : int {
            arr : int[3]! = new int[3]{10, 20, 30};
            r : int = arr[2];
            delete arr;
            return r;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 30);
}

TEST_CASE("Subscript through owner — write", "[gen][subscript-indirection]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : int {
            arr : int[3]! = new int[3]{10, 20, 30};
            arr[1] = 99;
            r : int = arr[1];
            delete arr;
            return r;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 99);
}

// ─────────────────────────────────────────────────────────────────────────────
// NEW: subscript through pointer (*)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Subscript through pointer — read", "[gen][subscript-indirection]") {
    auto jit = gen_jit(R"SRC(
        module test;

        get(p : int[3]*, i : int) : int {
            return p[i];
        }

        test() : int {
            arr : int[3]{10, 20, 30};
            p : int[3]* = &arr;
            return get(p, 2);
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 30);
}

TEST_CASE("Subscript through pointer — write modifies original", "[gen][subscript-indirection]") {
    auto jit = gen_jit(R"SRC(
        module test;

        set(p : int[3]*, i : int, v : int) {
            p[i] = v;
        }

        test() : int {
            arr : int[3]{10, 20, 30};
            p : int[3]* = &arr;
            set(p, 0, 77);
            return arr[0];
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 77);
}

// ─────────────────────────────────────────────────────────────────────────────
// NEW: subscript through link (~)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Subscript through link — read", "[gen][subscript-indirection]") {
    auto jit = gen_jit(R"SRC(
        module test;

        get(l : int[3]~, i : int) : int {
            return l[i];
        }

        test() : int {
            arr : int[3]{10, 20, 30};
            l : int[3]~ = &arr;
            return get(l, 1);
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 20);
}

TEST_CASE("Subscript through link — write modifies original", "[gen][subscript-indirection]") {
    auto jit = gen_jit(R"SRC(
        module test;

        set(l : int[3]~, i : int, v : int) {
            l[i] = v;
        }

        test() : int {
            arr : int[3]{10, 20, 30};
            l : int[3]~ = &arr;
            set(l, 0, 55);
            return arr[0];
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 55);
}

// ─────────────────────────────────────────────────────────────────────────────
// NEW: subscript through pinned (^)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Subscript through pinned — read", "[gen][subscript-indirection]") {
    auto jit = gen_jit(R"SRC(
        module test;

        get(q : int[3]^, i : int) : int {
            return q[i];
        }

        test() : int {
            arr : int[3]{10, 20, 30};
            q : int[3]^ = &arr;
            return get(q, 2);
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 30);
}

// ─────────────────────────────────────────────────────────────────────────────
// Struct arrays through indirection
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Subscript through pointer — struct member access", "[gen][subscript-indirection]") {
    auto jit = gen_jit(R"SRC(
        module test;

        struct Point {
            x : int = 0;
            y : int = 0;
        }

        test() : int {
            arr : Point[2];
            arr[0].x = 10;
            arr[0].y = 20;
            arr[1].x = 30;
            arr[1].y = 40;
            p : Point[2]* = &arr;
            return p[0].x + p[1].y;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 50); // 10 + 40
}

TEST_CASE("Subscript through link — struct member access", "[gen][subscript-indirection]") {
    auto jit = gen_jit(R"SRC(
        module test;

        struct Point {
            x : int = 0;
            y : int = 0;
        }

        test() : int {
            arr : Point[2];
            arr[0].x = 5;
            arr[1].x = 15;
            l : Point[2]~ = &arr;
            return l[0].x + l[1].x;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 20); // 5 + 15
}

// ─────────────────────────────────────────────────────────────────────────────
// Dynamic owner array subscript (regression)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Subscript through owner — dynamic size", "[gen][subscript-indirection]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test(n : unsigned int) : int {
            arr : int[]! = new int(42)[n];
            r : int = arr[0];
            delete arr;
            return r;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)(unsigned int)>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test(5) == 42);
}

// ─────────────────────────────────────────────────────────────────────────────
// Error: subscript on non-array indirection should be an error
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Subscript on non-array pointer — error", "[gen][subscript-indirection][error]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module test;

        test() : int {
            x : int = 42;
            p : int* = &x;
            return p[0];
        }
    )SRC"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Arrays of links (~)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Array of links — read", "[gen][subscript-indirection][array-of-indir]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : int {
            a : int = 3;
            b : int = 5;
            c : int = 7;
            arr : int~[]{&a, &b, &c};
            return *arr[0] + *arr[1] + *arr[2];
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == (3+5+7));
}

TEST_CASE("Array of links — write-through", "[gen][subscript-indirection][array-of-indir]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : int {
            a : int = 1;
            b : int = 2;
            arr : int~[]{&a, &b};
            *arr[0] = 10;
            *arr[1] = 20;
            return a + b;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 30);
}

// ─────────────────────────────────────────────────────────────────────────────
// Arrays of pointers (*)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Array of pointers — read", "[gen][subscript-indirection][array-of-indir]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : int {
            a : int = 10;
            b : int = 20;
            c : int = 30;
            arr : int*[]{&a, &b, &c};
            return *arr[0] + *arr[1] + *arr[2];
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 60);
}

TEST_CASE("Array of pointers — write-through", "[gen][subscript-indirection][array-of-indir]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : int {
            a : int = 1;
            b : int = 2;
            arr : int*[]{&a, &b};
            *arr[0] = 100;
            *arr[1] = 200;
            return a + b;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 300);
}

// ─────────────────────────────────────────────────────────────────────────────
// Arrays of pinned (^)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Array of pinned — read", "[gen][subscript-indirection][array-of-indir]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : int {
            a : int = 4;
            b : int = 5;
            c : int = 6;
            arr : int^[]{&a, &b, &c};
            return *arr[0] + *arr[1] + *arr[2];
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 15);
}

// ─────────────────────────────────────────────────────────────────────────────
// Arrays of owners (!)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Array of owners — read", "[gen][subscript-indirection][array-of-indir]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : int {
            arr : int![]{new int(10), new int(20), new int(30)};
            r : int = *arr[0] + *arr[1] + *arr[2];
            delete arr[0];
            delete arr[1];
            delete arr[2];
            return r;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 60);
}

TEST_CASE("Array of owners — write-through", "[gen][subscript-indirection][array-of-indir]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : int {
            arr : int![]{new int(1), new int(2)};
            *arr[0] = 50;
            *arr[1] = 60;
            r : int = *arr[0] + *arr[1];
            delete arr[0];
            delete arr[1];
            return r;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 110);
}

