/*
 * K Language standard library — Set tests (ListSet, TreeSet, HashSet)
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

#ifndef LIBK_KDI_DIR
#error "LIBK_KDI_DIR must be defined — check CMakeLists.txt"
#endif
#ifndef LIBK_LIB_DIR
#error "LIBK_LIB_DIR must be defined — check CMakeLists.txt"
#endif

namespace {

std::unique_ptr<k::model::gen::jit> jit_k(std::string_view src, bool dump = false) {
    return gen_jit_with_stdlib(src, LIBK_KDI_DIR, LIBK_LIB_DIR, dump);
}

} // anonymous namespace


// ═══════════════════════════════════════════════════════════════════════════════
// 1 : Sequence::forEach
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Vector<int> — for each", "[libk][vector][int][forEach]") {
    auto j = jit_k(R"SRC(
        module __vector_foreach__;

        struct Accumulator {
            sum : int = 0;
            add(v : const int&) { sum += v; }
        }

        test() : bool {
            v : Vector<int>(int[]{1, 2, 3, 4, 5});
            acc : Accumulator;
            v.forEach(acc.add);
            return acc.sum == 15;  // 1+2+3+4+5 = 15
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<bool(*)()>("test");
    REQUIRE(fn);
    CHECK(fn());
}


// ═══════════════════════════════════════════════════════════════════════════════
// 2 : MutableSequence::forEach
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Vector<int> — mutable for each", "[libk][vector][int][forEach]") {
    auto j = jit_k(R"SRC(
        module __vector_foreach__;

        test() : bool {
            v : Vector<int>(int[]{1, 2, 3, 4, 5});
            v.forEach([](v : int&) { v *= 2; });  // Double each element
            return v.size() == 5 && v[0] == 2 && v[4] == 10;  // Check first and last elements
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<bool(*)()>("test");
    REQUIRE(fn);
    CHECK(fn());
}

// ═══════════════════════════════════════════════════════════════════════════════
// 3 : Sequence::collect
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Vector<int> — collect by appending into an appendable collection", "[libk][vector][int][collect]") {
    auto j = jit_k(R"SRC(
        module __vector_collect__;
        test() : bool {
            v : Vector<int>(int[]{1, 2, 3, 4, 5});
            v2 : Vector<int>;

            v.collect(v2);

            return v2.size() == v.size() && v2[0] == v[0] && v2[4] == v[4];  // Check first and last elements
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<bool(*)()>("test");
    REQUIRE(fn);
    CHECK(fn());
}

TEST_CASE("Vector<int> — collect through a consumer collector", "[libk][vector][int][collect]") {
    auto j = jit_k(R"SRC(
        module __vector_collect__;

        struct Accumulator {
            sum : int = 0;
            add(v : const int&) { sum += v; }
        }

        test() : bool {
            v : Vector<int>(int[]{1, 2, 3, 4, 5});
            acc : Accumulator;

            v.collect(acc.add);

            return acc.sum == 15;  // 1+2+3+4+5 = 15
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<bool(*)()>("test");
    REQUIRE(fn);
    CHECK(fn());
}

// ═══════════════════════════════════════════════════════════════════════════════
// 4 : Sequence::accumulate
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Vector<int> — accumulate into a single value - deduced member form", "[libk][vector][int][accumulate]") {
    auto j = jit_k(R"SRC(
        module __vector_accumulate__;
        test() : bool {
            v : Vector<int>(int[]{1, 2, 3, 4, 5});
            result : int = v.accumulate(0, [](acc : int, v : const int&) { return acc + v; });

            return result == 15;  // 1+2+3+4+5 = 15
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<bool(*)()>("test");
    REQUIRE(fn);
    CHECK(fn());
}

TEST_CASE("Vector<int> — accumulate into a single value - uniform member form", "[libk][vector][int][accumulate]") {
    auto j = jit_k(R"SRC(
        module __vector_accumulate__;
        test() : bool {
            v : Vector<int>(int[]{1, 2, 3, 4, 5});
            result : int = v.accumulate<int>(0, [](acc : int, v : const int&) { return acc + v; });

            return result == 15;  // 1+2+3+4+5 = 15
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<bool(*)()>("test");
    REQUIRE(fn);
    CHECK(fn());
}

// ═══════════════════════════════════════════════════════════════════════════════
// 5 : Sequence::filter
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Vector<int> — filter elements", "[libk][vector][int][filter]") {
    auto j = jit_k(R"SRC(
        module __vector_filter__;
        test() : bool {
            v : Vector<int>(int[]{1, 2, 3, 4, 5, 6, 7, 8, 9, 10});
            result : int = v.filter([](v : const int&) { return v % 2 == 0; })
                            ->accumulate<int>(0, [](acc : int, v : const int&) { return acc + v; });

            return result == 30;  // 2+4+6+8+10 = 30
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<bool(*)()>("test");
    REQUIRE(fn);
    CHECK(fn());
}

// ═══════════════════════════════════════════════════════════════════════════════
// 6 : Sequence::map
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Vector<int> — map elements", "[libk][vector][int][map]") {
    auto j = jit_k(R"SRC(
        module __vector_map__;

        i_to_f(v : const int&) : float {
            return 1.5f * v;
        }

        test() : bool {
            v : Vector<int>(int[]{1, 2, 3, 4, 5});
            result : float = v.map<float>(i_to_f)
                            ->accumulate<float>(0.0f, [](acc : float, v : const float&) { return acc + v; });

            return result == 22.5f;  // (1*1.5)+(2*1.5)+(3*1.5)+(4*1.5)+(5*1.5) = 22.5
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<bool(*)()>("test");
    REQUIRE(fn);
    CHECK(fn());
}

TEST_CASE("Vector<int> — map elements with lambda", "[libk][vector][int][map]") {
    auto j = jit_k(R"SRC(
        module __vector_map_lambda__;

        test() : bool {
            v : Vector<int>(int[]{1, 2, 3, 4, 5});
            result : float = v.map<float>([](v : const int&) : float { return 1.5f * v; })
                            ->accumulate<float>(0.0f, [](acc : float, v : const float&) { return acc + v; });

            return result == 22.5f;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<bool(*)()>("test");
    REQUIRE(fn);
    CHECK(fn());
}

// ═══════════════════════════════════════════════════════════════════════════════
// 7 : Sequence::flatMap
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Vector<int> — flatten elements", "[libk][vector][int][flatten]") {
    auto j = jit_k(R"SRC(
        module __vector_flatten__;

        test() : bool {
            v : Vector<Vector<int> >;
            v.append(Vector<int>(int[]{1, 2}));
            v.append(Vector<int>(int[]{2, 4}));
            v.append(Vector<int>(int[]{3, 6}));
            result : int = v.flatten<int>()
                            ->accumulate<int>(0, [](acc : int, x : const int&) { return acc + x; });

            return result == 18;  // (1+2)+(2+4)+(3+6) = 18
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<bool(*)()>("test");
    REQUIRE(fn);
    CHECK(fn());
}

TEST_CASE("Vector<int> — flatMap elements without args", "[libk][vector][int][flatMap][noargs]") {
    auto j = jit_k(R"SRC(
        module __vector_flatmap_noargs__;

        test() : bool {
            v : Vector<Vector<int> >;
            v.append(Vector<int>(int[]{1, 2}));
            v.append(Vector<int>(int[]{2, 4}));
            v.append(Vector<int>(int[]{3, 6}));
            result : int = v.flatMap<int>()
                            ->accumulate<int>(0, [](acc : int, x : const int&) { return acc + x; });

            return result == 18;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<bool(*)()>("test");
    REQUIRE(fn);
    CHECK(fn());
}

TEST_CASE("Vector<int> — flatMap with mapping function", "[libk][vector][int][flatMap]") {
    auto j = jit_k(R"SRC(
        module __vector_flatmap__;

        duplicate(x : const int&) : Sequence<int>! {
            return new Vector<int>(int[]{x, x * 10});
        }

        test() : bool {
            v : Vector<int>(int[]{1, 2, 3});
            result : int = v.flatMap<int>(duplicate)
                            ->accumulate<int>(0, [](acc : int, x : const int&) { return acc + x; });

            return result == 66;  // (1+10)+(2+20)+(3+30) = 66
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<bool(*)()>("test");
    REQUIRE(fn);
    CHECK(fn());
}

TEST_CASE("Vector<int> — flatMap with lambda", "[libk][vector][int][flatMap]") {
    auto j = jit_k(R"SRC(
        module __vector_flatmap_lambda__;

        test() : bool {
            v : Vector<int>(int[]{1, 2, 3});
            result : int = v.flatMap<int>([](x : const int&) : Sequence<int>! {
                                return new Vector<int>(int[]{x, x * 10});
                            })
                            ->accumulate<int>(0, [](acc : int, x : const int&) { return acc + x; });

            return result == 66;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<bool(*)()>("test");
    REQUIRE(fn);
    CHECK(fn());
}
