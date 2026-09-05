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

// ═══════════════════════════════════════════════════════════════════════════════
// 8 : Sequence::count
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Vector<int> — count internal counter", "[libk][vector][int][count]") {
    auto j = jit_k(R"SRC(
        module __vector_count_internal__;

        test() : bool {
            v : Vector<int>(int[]{10, 20, 30, 40, 50});
            seq : SequenceCount<int, unsigned long>! = v.count();
            if (seq.getCount() != 0) {
                return false;
            }

            it : ConstIterator<int>! = seq.constIterator();
            cur : OptionalConstRef<int> = it.next();
            while (cur.hasValue()) {
                cur = it.next();
            }

            if (seq.getCount() != 5) {
                return false;
            }

            // Second iteration: must continue accumulating without resetting to 0
            it2 : ConstIterator<int>! = seq.constIterator();
            cur2 : OptionalConstRef<int> = it2.next();
            while (cur2.hasValue()) {
                cur2 = it2.next();
            }

            return seq.getCount() == 10;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<bool(*)()>("test");
    REQUIRE(fn);
    CHECK(fn());
}

TEST_CASE("Vector<int> — count external primitive accumulator", "[libk][vector][int][count]") {
    auto j = jit_k(R"SRC(
        module __vector_count_external_primitive__;

        test() : bool {
            v : Vector<int>(int[]{1, 2, 3});
            acc : int = 100;

            seq : Sequence<int>! = v.count<int>(acc);
            seq.collect([](x : const int&) {});

            if (acc != 103) {
                return false;
            }

            // Re-iterate: no reset to zero, accumulates on top of previous value
            seq.collect([](x : const int&) {});
            return acc == 106;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<bool(*)()>("test");
    REQUIRE(fn);
    CHECK(fn());
}

TEST_CASE("Vector<int> — count external custom accumulator with operator++", "[libk][vector][int][count]") {
    auto j = jit_k(R"SRC(
        module __vector_count_external_custom__;

        struct StepCounter {
            steps : int = 10;
            operator++_() : StepCounter& {
                steps += 5;
                return this;
            }
        }

        test() : bool {
            v : Vector<int>(int[]{1, 2, 3, 4});
            counter : StepCounter;

            seq : Sequence<int>! = v.count<StepCounter>(counter);
            seq.collect([](x : const int&) {});

            // 10 + 4 * 5 = 30
            return counter.steps == 30;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<bool(*)()>("test");
    REQUIRE(fn);
    CHECK(fn());
}

TEST_CASE("Vector<int> — count pipeline chaining", "[libk][vector][int][count]") {
    auto j = jit_k(R"SRC(
        module __vector_count_pipeline__;

        test() : bool {
            v : Vector<int>(int[]{1, 2, 3, 4, 5, 6, 7, 8});
            counter : unsigned long = 0;

            // Pipeline: filter even numbers -> count -> map(*10) -> accumulate
            total : int = v.filter([](x : const int&) { return x % 2 == 0; })
                           ->count<unsigned long>(counter)
                           ->map<int>([](x : const int&) { return x * 10; })
                           ->accumulate<int>(0, [](acc : int, x : const int&) { return acc + x; });

            // Even numbers: 2, 4, 6, 8 -> count is 4
            // Sum: 20 + 40 + 60 + 80 = 200
            return counter == 4 && total == 200;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<bool(*)()>("test");
    REQUIRE(fn);
    CHECK(fn());
}

// ═══════════════════════════════════════════════════════════════════════════════
// 9 : Sequence::skip
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Vector<int> — skip elements with default after", "[libk][vector][int][skip]") {
    auto j = jit_k(R"SRC(
        module __vector_skip_default_after__;

        test() : bool {
            v : Vector<int>(int[]{10, 20, 30, 40, 50});
            counter : unsigned long = 0;

            sum : int = v.skip(2)
                         ->count<unsigned long>(counter)
                         ->accumulate<int>(0, [](acc : int, x : const int&) { return acc + x; });

            // Skipped 10, 20. Remaining: 30, 40, 50.
            // Sum: 30 + 40 + 50 = 120, count = 3
            return sum == 120 && counter == 3;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<bool(*)()>("test");
    REQUIRE(fn);
    CHECK(fn());
}

TEST_CASE("Vector<int> — skip elements with explicit after", "[libk][vector][int][skip]") {
    auto j = jit_k(R"SRC(
        module __vector_skip_explicit_after__;

        test() : bool {
            v : Vector<int>(int[]{1, 2, 3, 4, 5, 6, 7});
            counter : unsigned long = 0;

            sum : int = v.skip(3, 2)
                         ->count<unsigned long>(counter)
                         ->accumulate<int>(0, [](acc : int, x : const int&) { return acc + x; });

            // Pass 2 elements (1, 2), skip 3 elements (3, 4, 5), pass remaining (6, 7).
            // Elements: 1, 2, 6, 7. Sum = 1 + 2 + 6 + 7 = 16, count = 4.
            return sum == 16 && counter == 4;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<bool(*)()>("test");
    REQUIRE(fn);
    CHECK(fn());
}

TEST_CASE("Vector<int> — skip 0 elements", "[libk][vector][int][skip]") {
    auto j = jit_k(R"SRC(
        module __vector_skip_zero__;

        test() : bool {
            v : Vector<int>(int[]{1, 2, 3, 4});

            sum1 : int = v.skip(0)
                          ->accumulate<int>(0, [](acc : int, x : const int&) { return acc + x; });

            sum2 : int = v.skip(0, 2)
                          ->accumulate<int>(0, [](acc : int, x : const int&) { return acc + x; });

            return sum1 == 10 && sum2 == 10;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<bool(*)()>("test");
    REQUIRE(fn);
    CHECK(fn());
}

TEST_CASE("Vector<int> — skip more elements than available", "[libk][vector][int][skip]") {
    auto j = jit_k(R"SRC(
        module __vector_skip_overflow__;

        test() : bool {
            v : Vector<int>(int[]{1, 2, 3});

            // Skip 10 with after=0: all skipped, empty
            count1 : unsigned long = 0;
            v.skip(10)->count<unsigned long>(count1)->collect([](x : const int&) {});

            // Skip 10 with after=2: 1 and 2 passed, 3 skipped, count = 2, sum = 3
            count2 : unsigned long = 0;
            sum2 : int = v.skip(10, 2)
                          ->count<unsigned long>(count2)
                          ->accumulate<int>(0, [](acc : int, x : const int&) { return acc + x; });

            return count1 == 0 && count2 == 2 && sum2 == 3;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<bool(*)()>("test");
    REQUIRE(fn);
    CHECK(fn());
}

TEST_CASE("Vector<int> — skip when sequence shorter than after", "[libk][vector][int][skip]") {
    auto j = jit_k(R"SRC(
        module __vector_skip_shorter_than_after__;

        test() : bool {
            v : Vector<int>(int[]{1, 2});
            count : unsigned long = 0;

            sum : int = v.skip(5, 10)
                         ->count<unsigned long>(count)
                         ->accumulate<int>(0, [](acc : int, x : const int&) { return acc + x; });

            // Only 2 elements in vector, after=10: both pass, skip phase is never reached
            return count == 2 && sum == 3;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<bool(*)()>("test");
    REQUIRE(fn);
    CHECK(fn());
}

TEST_CASE("Vector<int> — skip pipeline chaining", "[libk][vector][int][skip]") {
    auto j = jit_k(R"SRC(
        module __vector_skip_pipeline__;

        test() : bool {
            v : Vector<int>(int[]{1, 2, 3, 4, 5, 6, 7, 8, 9, 10});

            // Even numbers: 2, 4, 6, 8, 10
            // skip(2, after: 1): pass 1 (2), skip 2 (4, 6), pass rest (8, 10) -> [2, 8, 10]
            // map (*2): [4, 16, 20]
            // sum: 4 + 16 + 20 = 40
            total : int = v.filter([](x : const int&) { return x % 2 == 0; })
                           ->skip(2, 1)
                           ->map<int>([](x : const int&) { return x * 2; })
                           ->accumulate<int>(0, [](acc : int, x : const int&) { return acc + x; });

            return total == 40;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<bool(*)()>("test");
    REQUIRE(fn);
    CHECK(fn());
}

// ═══════════════════════════════════════════════════════════════════════════════
// 10 : Sequence::getFirst
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Vector<int> — getFirst on non-empty sequence", "[libk][vector][int][getFirst]") {
    auto j = jit_k(R"SRC(
        module __vector_get_first_non_empty__;

        test() : bool {
            v : Vector<int>(int[]{42, 10, 20});
            first : Optional<int> = v.getFirst();

            return first.hasValue() && first.get() == 42;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<bool(*)()>("test");
    REQUIRE(fn);
    CHECK(fn());
}

TEST_CASE("Vector<int> — getFirst on empty sequence", "[libk][vector][int][getFirst]") {
    auto j = jit_k(R"SRC(
        module __vector_get_first_empty__;

        test() : bool {
            v : Vector<int>;
            first : Optional<int> = v.getFirst();

            return !first.hasValue();
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<bool(*)()>("test");
    REQUIRE(fn);
    CHECK(fn());
}

TEST_CASE("Vector<int> — getFirst on pipeline", "[libk][vector][int][getFirst]") {
    auto j = jit_k(R"SRC(
        module __vector_get_first_pipeline__;

        test() : bool {
            v : Vector<int>(int[]{1, 2, 3, 4, 5});

            first : Optional<int> = v.filter([](x : const int&) { return x > 2; })
                                    ->skip(1)
                                    ->getFirst();

            // filter (>2) -> [3, 4, 5], skip(1) -> [4, 5], getFirst -> 4
            return first.hasValue() && first.get() == 4;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<bool(*)()>("test");
    REQUIRE(fn);
    CHECK(fn());
}

TEST_CASE("Vector<int> — getFirst on pipeline returning empty", "[libk][vector][int][getFirst]") {
    auto j = jit_k(R"SRC(
        module __vector_get_first_pipeline_empty__;

        test() : bool {
            v : Vector<int>(int[]{1, 2, 3});

            first : Optional<int> = v.filter([](x : const int&) { return x > 10; })
                                    ->getFirst();

            return !first.hasValue();
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<bool(*)()>("test");
    REQUIRE(fn);
    CHECK(fn());
}

TEST_CASE("Vector<int> — getFirst on map", "[libk][vector][int][getFirst]") {
    auto j = jit_k(R"SRC(
        module __vector_get_first_map__;

        test() : bool {
            v : Vector<int>(int[]{5, 10});

            first : Optional<int> = v.map<int>([](x : const int&) { return x * 2; })
                                    ->getFirst();

            return first.hasValue() && first.get() == 10;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<bool(*)()>("test");
    REQUIRE(fn);
    CHECK(fn());
}




