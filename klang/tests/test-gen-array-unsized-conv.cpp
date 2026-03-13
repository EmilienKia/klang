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
 * Tests for implicit sized→unsized array conversion.
 *
 * A sized array T[N] can be implicitly converted (widened) to an unsized
 * array reference T[] (= T[]&). This applies to all indirection types:
 * reference (&), link (~), pointer (*), pinned (^), and owner (!).
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
// Reference widening: ref<T[N]> → ref<T[]>  (parameter passing)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Sized→unsized — pass int[4] to int[] parameter", "[gen][array-unsized]") {
    auto jit = gen_jit(R"SRC(
        module test;

        sum(a : int[], n : unsigned int) : int {
            s : int = 0;
            i : unsigned int = 0u;
            while(i < n) {
                s = s + a[i];
                i = i + 1u;
            }
            return s;
        }

        test() : int {
            arr : int[3]{10, 20, 30};
            return sum(arr, 3u);
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 60);
}

TEST_CASE("Sized→unsized — pass int[N] to int[] and read .size", "[gen][array-unsized]") {
    auto jit = gen_jit(R"SRC(
        module test;

        get_size(a : int[]) : unsigned int {
            return a.size;
        }

        test() : unsigned int {
            arr : int[4]{10, 20, 30, 40};
            return get_size(arr);
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<unsigned(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 4);
}

TEST_CASE("Sized→unsized — pass int[5] to int[]& explicit ref", "[gen][array-unsized]") {
    auto jit = gen_jit(R"SRC(
        module test;

        get_size(a : int[]&) : unsigned int {
            return a.size;
        }

        test() : unsigned int {
            arr : int[5]{1, 2, 3, 4, 5};
            return get_size(arr);
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<unsigned(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 5);
}

// ─────────────────────────────────────────────────────────────────────────────
// Link widening: link<T[N]> → link<T[]>
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Sized→unsized — link to unsized array", "[gen][array-unsized]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : unsigned int {
            arr : int[3]{1, 2, 3};
            l : int[]~ = &arr;
            return l->size;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<unsigned(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 3);
}

TEST_CASE("Sized→unsized — link to unsized array, read element", "[gen][array-unsized]") {
    auto jit = gen_jit(R"SRC(
        module test;

        get_first(l : int[]~) : int {
            return l[0];
        }

        test() : int {
            arr : int[3]{42, 99, 7};
            l : int[]~ = &arr;
            return get_first(l);
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

// ─────────────────────────────────────────────────────────────────────────────
// Pointer widening: pointer<T[N]> → pointer<T[]>
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Sized→unsized — pointer to unsized array", "[gen][array-unsized]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : unsigned int {
            arr : int[4]{10, 20, 30, 40};
            p : int[]* = &arr;
            return p->size;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<unsigned(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 4);
}

TEST_CASE("Sized→unsized — pointer to unsized array, subscript", "[gen][array-unsized]") {
    auto jit = gen_jit(R"SRC(
        module test;

        get_elem(p : int[]*, i : unsigned int) : int {
            return p[i];
        }

        test() : int {
            arr : int[3]{100, 200, 300};
            p : int[]* = &arr;
            return get_elem(p, 2u);
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 300);
}

// ─────────────────────────────────────────────────────────────────────────────
// Pinned widening: pinned<T[N]> → pinned<T[]>
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Sized→unsized — pinned to unsized array", "[gen][array-unsized]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : unsigned int {
            arr : int[2]{7, 8};
            p : int[]^ = &arr;
            return p->size;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<unsigned(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 2);
}

// ─────────────────────────────────────────────────────────────────────────────
// Owner widening: owner<T[N]> → owner<T[]>
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Sized→unsized — owner to unsized array", "[gen][array-unsized]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : unsigned int {
            o : int[]! = new int[]{10, 20, 30, 40, 50};
            return o->size;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<unsigned(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 5);
}

TEST_CASE("Sized→unsized — owner to unsized, subscript read", "[gen][array-unsized]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : int {
            o : int[]! = new int[]{10, 20, 30};
            return o[1];
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 20);
}

// ─────────────────────────────────────────────────────────────────────────────
// Array of links to unsized arrays (via sized→unsized element conversion)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Sized→unsized — array of links to unsized, different sizes", "[gen][array-unsized]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : unsigned int {
            a : int[2]{1, 2};
            b : int[4]{3, 4, 5, 6};
            outer : int[]~[]{&a, &b};
            return outer[1]->size;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<unsigned(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 4);
}

// ─────────────────────────────────────────────────────────────────────────────
// Array of owners to unsized arrays
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Sized→unsized — array of owners to unsized arrays", "[gen][array-unsized]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : unsigned int {
            arr : int[]![]{new int[]{1}, new int[]{1, 2}, new int[]{1, 2, 3}};
            return arr[2]->size;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<unsigned(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 3);
}

TEST_CASE("Sized→unsized — array of owners, outer size", "[gen][array-unsized]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : unsigned int {
            arr : int[]![]{new int[]{1}, new int[]{1, 2}, new int[]{1, 2, 3}};
            return arr.size;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<unsigned(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 3);
}

// ─────────────────────────────────────────────────────────────────────────────
// Generic function taking unsized array
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Sized→unsized — sum function with .size loop", "[gen][array-unsized]") {
    auto jit = gen_jit(R"SRC(
        module test;

        array_sum(a : int[]) : int {
            s : int = 0;
            i : unsigned int = 0u;
            while(i < a.size) {
                s = s + a[i];
                i = i + 1u;
            }
            return s;
        }

        test() : int {
            arr : int[5]{1, 2, 3, 4, 5};
            return array_sum(arr);
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 15);
}

