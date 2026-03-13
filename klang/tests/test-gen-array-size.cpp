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
 * Tests for virtual array member "size".
 *
 * Arrays expose a virtual read-only member `size` that returns the element
 * count as an unsigned int (i32), stored in field 0 of the LLVM struct.
 * Access is via `.` (direct / reference) or `->` (pointer / link / pinned / owner).
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
// Direct / reference access (.)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Array .size — sized array direct access", "[gen][array-size]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : unsigned int {
            arr : int[5]{10, 20, 30, 40, 50};
            return arr.size;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<unsigned(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 5);
}

TEST_CASE("Array .size — sized array via reference parameter", "[gen][array-size]") {
    auto jit = gen_jit(R"SRC(
        module test;

        get_size(a : int[3]&) : unsigned int {
            return a.size;
        }

        test() : unsigned int {
            arr : int[3]{1, 2, 3};
            return get_size(arr);
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<unsigned(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 3);
}

// NOTE: int[] (unsized) parameter and indirection tests (int[]*, int[]~, int[]^)
// are not included here because sized→unsized array conversion (e.g. int[4] → int[])
// is not yet supported. The .size feature itself handles unsized arrays correctly;
// only the conversion path is missing.

TEST_CASE("Array .size — empty sized array", "[gen][array-size]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : unsigned int {
            arr : int[0]{};
            return arr.size;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<unsigned(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Pointer access (->)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Array ->size — pointer to sized array", "[gen][array-size]") {
    auto jit = gen_jit(R"SRC(
        module test;

        get_size(p : int[4]*) : unsigned int {
            return p->size;
        }

        test() : unsigned int {
            arr : int[4]{10, 20, 30, 40};
            p : int[4]* = &arr;
            return get_size(p);
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<unsigned(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 4);
}

// ─────────────────────────────────────────────────────────────────────────────
// Link access (->)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Array ->size — link to sized array", "[gen][array-size]") {
    auto jit = gen_jit(R"SRC(
        module test;

        get_size(l : int[3]~) : unsigned int {
            return l->size;
        }

        test() : unsigned int {
            arr : int[3]{1, 2, 3};
            l : int[3]~ = &arr;
            return get_size(l);
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<unsigned(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 3);
}

// ─────────────────────────────────────────────────────────────────────────────
// Pinned access (->)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Array ->size — pinned to sized array", "[gen][array-size]") {
    auto jit = gen_jit(R"SRC(
        module test;

        get_size(p : int[2]^) : unsigned int {
            return p->size;
        }

        test() : unsigned int {
            arr : int[2]{7, 8};
            p : int[2]^ = &arr;
            return get_size(p);
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<unsigned(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 2);
}

// ─────────────────────────────────────────────────────────────────────────────
// Owner access (->)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Array ->size — owner to sized array (new)", "[gen][array-size]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : unsigned int {
            o : int[5]! = new int[]{10, 20, 30, 40, 50};
            return o->size;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<unsigned(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 5);
}

// ─────────────────────────────────────────────────────────────────────────────
// Size in expressions
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Array .size — used in arithmetic expression", "[gen][array-size]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : unsigned int {
            arr : int[4]{1, 2, 3, 4};
            return arr.size * 2u;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<unsigned(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 8);
}

TEST_CASE("Array .size — used as loop bound", "[gen][array-size]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : int {
            arr : int[4]{10, 20, 30, 40};
            sum : int = 0;
            i : unsigned int = 0u;
            while(i < arr.size) {
                sum = sum + arr[i];
                i = i + 1u;
            }
            return sum;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 100);
}

// ─────────────────────────────────────────────────────────────────────────────
// Array of arrays: .size on outer and inner
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Array .size — outer size of array of links to sized arrays", "[gen][array-size]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : unsigned int {
            a : int[3]{1, 2, 3};
            b : int[3]{4, 5, 6};
            outer : int[3]~[]{&a, &b};
            return outer.size;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<unsigned(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 2);
}

TEST_CASE("Array ->size — inner array via link (same size)", "[gen][array-size]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : unsigned int {
            a : int[3]{1, 2, 3};
            b : int[3]{4, 5, 6};
            outer : int[3]~[]{&a, &b};
            lnk : int[3]~ = outer[1];
            return lnk->size;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<unsigned(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 3);
}

// ─────────────────────────────────────────────────────────────────────────────
// Array of owners to sized arrays
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Array ->size — array of owners to sized arrays (int[3]![])", "[gen][array-size]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : unsigned int {
            arr : int[3]![]{new int[]{1, 2, 3}, new int[]{4, 5, 6}};
            return arr[1]->size;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<unsigned(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 3);
}

TEST_CASE("Array .size — outer size of array of owners", "[gen][array-size]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : unsigned int {
            arr : int[3]![]{new int[]{1, 2, 3}, new int[]{4, 5, 6}};
            return arr.size;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<unsigned(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 2);
}

// ─────────────────────────────────────────────────────────────────────────────
// Size as function argument
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Array .size — passed as function argument", "[gen][array-size]") {
    auto jit = gen_jit(R"SRC(
        module test;

        identity(n : unsigned int) : unsigned int {
            return n;
        }

        test() : unsigned int {
            arr : int[7]{1, 2, 3, 4, 5, 6, 7};
            return identity(arr.size);
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<unsigned(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 7);
}





