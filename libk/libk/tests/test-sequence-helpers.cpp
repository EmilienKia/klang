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

TEST_CASE("Array and sequence ordering compares contents in both directions",
          "[libk][array][sequence][comparison]") {
    auto j = jit_k(R"SRC(
        module __array_sequence_comparison__;

        arrayFirst(left : const int[]&, right : const Sequence<int>&) : int {
            return left <=> right;
        }
        sequenceFirst(left : const Sequence<int>&, right : const int[]&) : int {
            return left <=> right;
        }

        test() : int {
            empty : int[0]{};
            const array : int[3]{1, 2, 3};
            prefix : int[2]{1, 2};
            longer : int[4]{1, 2, 3, 0};
            lower : int[3]{1, 1, 9};
            higher : int[3]{1, 4, 0};
            emptyVector : Vector<int>;
            vector : Vector<int>(int[]{1, 2, 3});
            list : LinkedList<int>;
            list.append(1);
            list.append(2);
            list.append(3);

            if (arrayFirst(empty, emptyVector) != 0 || sequenceFirst(emptyVector, empty) != 0) return 1;
            if (arrayFirst(empty, vector) != -1 || sequenceFirst(vector, empty) != 1) return 2;
            if (arrayFirst(array, emptyVector) != 1 || sequenceFirst(emptyVector, array) != -1) return 3;
            if (arrayFirst(array, vector) != 0 || sequenceFirst(vector, array) != 0) return 4;
            if (arrayFirst(array, list) != 0 || sequenceFirst(list, array) != 0) return 5;
            if ((array <=> vector) != 0 || (vector <=> array) != 0 ||
                (array <=> list) != 0 || (list <=> array) != 0) return 6;
            if (arrayFirst(lower, list) != -1 || sequenceFirst(list, lower) != 1) return 7;
            if (arrayFirst(higher, vector) != 1 || sequenceFirst(vector, higher) != -1) return 8;
            if (arrayFirst(prefix, list) != -1 || sequenceFirst(list, prefix) != 1) return 9;
            if (arrayFirst(longer, vector) != 1 || sequenceFirst(vector, longer) != -1) return 10;
            return 0;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

TEST_CASE("Array and sequence ordering consumes only the necessary elements",
          "[libk][array][sequence][comparison]") {
    auto j = jit_k(R"SRC(
        module __array_sequence_comparison_consumption__;

        calls : int = 0;
        nextCalls : int = 0;
        destroyed : int = 0;
        struct Element {
            value : int = 0;
            Element() -> default;
            Element(v : int) : value(v) {}
            const operator <=>(other : const Element&) : double {
                ++calls;
                if (value == other.value) return 0.0d;
                // Both operand orders are negative, so swapping and negating is observable.
                if (value == 2) return -0.25d;
                return -0.5d;
            }
        }

        class ScratchIterator : public ConstIterator<Element> {
            length : int;
            index : int = 0;
            current : Element;
            ScratchIterator(n : int) : length(n), index(0), current() {}
            ~ScratchIterator() { ++destroyed; }
            next() : OptionalConstRef<Element> {
                ++nextCalls;
                if (index >= length) return OptionalConstRef<Element>();
                current.value = index == 1 ? 4 : index + 1;
                ++index;
                return OptionalConstRef<Element>(current);
            }
        }
        class ScratchSequence : public Sequence<Element> {
            length : int;
            ScratchSequence(n : int) : length(n) {}
            const constIterator() : ConstIterator<Element>! {
                return new ScratchIterator(length);
            }
        }

        arrayFirst(left : const Element[]&, right : const Sequence<Element>&) : int {
            return left <=> right;
        }
        sequenceFirst(left : const Sequence<Element>&, right : const Element[]&) : int {
            return left <=> right;
        }

        test() : int {
            array : Element[3]{Element(1), Element(2), Element(3)};
            equal : Element[3]{Element(1), Element(4), Element(3)};
            prefix : Element[1]{Element(1)};
            empty : Element[0]{};
            sequence : ScratchSequence(3);
            shorter : ScratchSequence(1);

            if (arrayFirst(array, sequence) != -1 || calls != 2 || nextCalls != 2 || destroyed != 1) return 1;
            calls = 0;
            nextCalls = 0;
            if (sequenceFirst(sequence, array) != -1 || calls != 2 || nextCalls != 2 || destroyed != 2) return 2;
            calls = 0;
            nextCalls = 0;
            if (arrayFirst(prefix, sequence) != -1 || calls != 1 || nextCalls != 2 || destroyed != 3) return 3;
            calls = 0;
            nextCalls = 0;
            if (sequenceFirst(sequence, prefix) != 1 || calls != 1 || nextCalls != 2 || destroyed != 4) return 4;
            calls = 0;
            nextCalls = 0;
            if (arrayFirst(array, shorter) != 1 || calls != 1 || nextCalls != 2 || destroyed != 5) return 5;
            calls = 0;
            nextCalls = 0;
            if (sequenceFirst(shorter, array) != -1 || calls != 1 || nextCalls != 2 || destroyed != 6) return 6;
            calls = 0;
            nextCalls = 0;
            if (arrayFirst(equal, sequence) != 0 || calls != 3 || nextCalls != 4 || destroyed != 7) return 7;
            calls = 0;
            nextCalls = 0;
            if (sequenceFirst(sequence, equal) != 0 || calls != 3 || nextCalls != 4 || destroyed != 8) return 8;
            calls = 0;
            nextCalls = 0;
            if (arrayFirst(empty, sequence) != -1 || calls != 0 || nextCalls != 1 || destroyed != 9) return 9;
            calls = 0;
            nextCalls = 0;
            if (sequenceFirst(sequence, empty) != 1 || calls != 0 || nextCalls != 1 || destroyed != 10) return 10;
            return 0;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

TEST_CASE("Array and sequence ordering requires the same element type",
          "[libk][array][sequence][comparison][diagnostic]") {
    std::string_view source;
    SECTION("array first") {
        source = R"SRC(
            module __array_sequence_comparison_types_array_first__;
            reject(left : const int[]&, right : const Sequence<unsigned int>&) : int {
                return left <=> right;
            }
        )SRC";
    }
    SECTION("sequence first") {
        source = R"SRC(
            module __array_sequence_comparison_types_sequence_first__;
            reject(left : const Sequence<unsigned int>&, right : const int[]&) : int {
                return left <=> right;
            }
        )SRC";
    }
    REQUIRE_THROWS_AS(gen_jit_throws(source), k::log::compiler_error);
}

TEST_CASE("Sequence ordering compares iterator contents lexicographically",
          "[libk][sequence][comparison]") {
    auto j = jit_k(R"SRC(
        module __sequence_comparison__;

        compare(left : const Sequence<int>&, right : const Sequence<int>&) : int {
            return left <=> right;
        }

        compareMutable(left : Sequence<int>&, right : Sequence<int>&) : int {
            return left <=> right;
        }

        test() : int {
            empty : Vector<int>;
            otherEmpty : LinkedList<int>;
            a : Vector<int>(int[]{1, 2, 3});
            equal : LinkedList<int>;
            equal.append(1);
            equal.append(2);
            equal.append(3);
            first : Vector<int>(int[]{0, 9, 9});
            later : Vector<int>(int[]{1, 4, 0});
            prefix : Vector<int>(int[]{1, 2});
            longer : Vector<int>(int[]{1, 2, 3, 0});

            if (compare(empty, otherEmpty) != 0) return 1;
            if (compare(empty, a) != -1 || compare(a, empty) != 1) return 2;
            if (compare(a, equal) != 0 || compare(equal, a) != 0 ||
                compareMutable(a, equal) != 0) return 3;
            if (compare(first, a) != -1 || compare(a, first) != 1) return 4;
            if ((a <=> first) != 1) return 8;
            if (compare(a, later) != -1 || compare(later, a) != 1) return 5;
            if (compare(prefix, a) != -1 || compare(a, prefix) != 1) return 6;
            if (compare(a, longer) != -1 || compare(longer, a) != 1) return 7;
            return 0;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

TEST_CASE("Sequence ordering consumes iterators and compares each pair once",
          "[libk][sequence][comparison]") {
    auto j = jit_k(R"SRC(
        module __sequence_comparison_consumption__;

        calls : int = 0;
        struct Element {
            value : int = 0;
            Element() -> default;
            Element(v : int) : value(v) {}
            const operator <=>(other : const Element&) : double {
                ++calls;
                if (value < other.value) return -0.25d;
                if (value > other.value) return 0.25d;
                return 0.0d;
            }
        }

        compare(left : const Sequence<Element>&, right : const Sequence<Element>&) : int {
            return left <=> right;
        }

        test() : int {
            a : Vector<Element>(Element[]{Element(1), Element(2), Element(3)});
            b : Vector<Element>(Element[]{Element(1), Element(4), Element(0)});
            shorterSource : Vector<Element>(Element[]{Element(1)});
            leftCount : int = 0;
            rightCount : int = 0;
            left : Sequence<Element>! = a.count<int>(leftCount);
            right : Sequence<Element>! = b.count<int>(rightCount);
            shorter : Sequence<Element>! = shorterSource.count<int>(rightCount);

            calls = 0;
            if (compare(*left, *right) != -1 || calls != 2) return 1;
            if (leftCount != 2 || rightCount != 2) return 2;
            calls = 0;
            leftCount = 0;
            rightCount = 0;
            if (compare(*right, *left) != 1 || calls != 2) return 3;
            if (leftCount != 2 || rightCount != 2) return 4;
            calls = 0;
            leftCount = 0;
            if (compare(*left, *left) != 0 || calls != 3 || leftCount != 6) return 5;
            calls = 0;
            leftCount = 0;
            rightCount = 0;
            if (compare(*left, *shorter) != 1 || calls != 1) return 6;
            if (leftCount != 2 || rightCount != 1) return 7;
            return 0;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

TEST_CASE("Sequence ordering advances single-use sources through the mismatch",
          "[libk][sequence][comparison]") {
    auto j = jit_k(R"SRC(
        module __sequence_comparison_single_use__;

        leftPosition : int = 0;
        rightPosition : int = 0;
        created : int = 0;
        destroyed : int = 0;

        class SingleUseIterator : public ConstIterator<int> {
            side : int;
            current : int = 0;

            SingleUseIterator(s : int) : side(s), current(0) { ++created; }
            ~SingleUseIterator() { ++destroyed; }

            next() : OptionalConstRef<int> {
                position : int = side == 0 ? leftPosition : rightPosition;
                if (position >= 4) return OptionalConstRef<int>();
                current = position == 3 ? 7 : position + 1;
                if (side == 1 && position == 2) current = 4;
                if (side == 0) ++leftPosition; else ++rightPosition;
                return OptionalConstRef<int>(current);
            }
        }

        class SingleUseSequence : public Sequence<int> {
            side : int;
            SingleUseSequence(s : int) : side(s) {}
            const constIterator() : ConstIterator<int>! {
                return new SingleUseIterator(side);
            }
        }

        compare(left : const Sequence<int>&, right : const Sequence<int>&) : int {
            return left <=> right;
        }

        test() : int {
            left : SingleUseSequence(0);
            right : SingleUseSequence(1);
            if (compare(left, right) != -1) return 1;
            if (leftPosition != 3 || rightPosition != 3 || created != 2 || destroyed != 2) return 2;
            if (compare(left, right) != 0) return 3;
            if (leftPosition != 4 || rightPosition != 4 || created != 4 || destroyed != 4) return 4;
            return 0;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

TEST_CASE("Sequence ordering keeps iterator-owned elements alive through each comparison",
          "[libk][sequence][comparison]") {
    auto j = jit_k(R"SRC(
        module __sequence_comparison_iterator_storage__;

        leftNext : int = 0;
        rightNext : int = 0;
        destroyed : int = 0;
        comparisons : int = 0;

        struct Element {
            value : int = 0;
            const operator <=>(other : const Element&) : int {
                ++comparisons;
                return value <=> other.value;
            }
        }

        class ScratchIterator : public ConstIterator<Element> {
            side : int;
            length : int;
            second : int;
            index : int = 0;
            current : Element;

            ScratchIterator(s : int, n : int, v : int)
                : side(s), length(n), second(v), index(0), current() {}
            ~ScratchIterator() { ++destroyed; }

            next() : OptionalConstRef<Element> {
                if (side == 0) ++leftNext; else ++rightNext;
                if (index >= length) return OptionalConstRef<Element>();
                current.value = index == 1 ? second : index;
                ++index;
                return OptionalConstRef<Element>(current);
            }
        }

        class ScratchSequence : public Sequence<Element> {
            side : int;
            length : int;
            second : int;

            ScratchSequence(s : int, n : int, v : int)
                : side(s), length(n), second(v) {}
            const constIterator() : ConstIterator<Element>! {
                return new ScratchIterator(side, length, second);
            }
        }

        compare(left : const Sequence<Element>&, right : const Sequence<Element>&) : int {
            return left <=> right;
        }

        test() : int {
            left : ScratchSequence(0, 3, 1);
            equal : ScratchSequence(1, 3, 1);
            different : ScratchSequence(1, 3, 5);
            shorter : ScratchSequence(1, 1, 1);
            empty : ScratchSequence(1, 0, 1);

            if (compare(left, equal) != 0) return 1;
            if (leftNext != 4 || rightNext != 4 || comparisons != 3 || destroyed != 2) return 2;
            leftNext = 0;
            rightNext = 0;
            comparisons = 0;
            if (compare(left, different) != -1) return 3;
            if (leftNext != 2 || rightNext != 2 || comparisons != 2 || destroyed != 4) return 4;
            leftNext = 0;
            rightNext = 0;
            comparisons = 0;
            if (compare(left, shorter) != 1) return 5;
            if (leftNext != 2 || rightNext != 2 || comparisons != 1 || destroyed != 6) return 6;
            leftNext = 0;
            rightNext = 0;
            comparisons = 0;
            if (compare(left, empty) != 1) return 7;
            if (leftNext != 1 || rightNext != 1 || comparisons != 0 || destroyed != 8) return 8;
            return 0;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 0);
}


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
