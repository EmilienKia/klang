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

std::unique_ptr<k::model::gen::jit> jit_k(std::string_view src) {
    return gen_jit_with_stdlib(src, LIBK_KDI_DIR, LIBK_LIB_DIR);
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════════
//  1. ListSet<int>
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("ListSet<int> — empty set", "[libk][set][listset]") {
    auto j = jit_k(R"SRC(
        module __lset_empty__;
        test() : bool {
            s : ListSet<int>;
            return s.isEmpty() && s.size() == 0 && !s.first().hasValue() && !s.last().hasValue();
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<bool(*)()>("test");
    REQUIRE(fn);
    CHECK(fn());
}

TEST_CASE("ListSet<int> — first/last reflect insertion order", "[libk][set][listset]") {
    auto j = jit_k(R"SRC(
        module __lset_order__;
        test() : int {
            s : ListSet<int>;
            s.add(3);
            s.add(1);
            s.add(2);
            result : int = 0;
            if (s.first().get() == 3) result = result + 1;
            if (s.last().get() == 2) result = result + 10;
            if (!s.add(1)) result = result + 100;             // duplicate: no reorder
            if (s.first().get() == 3) result = result + 1000;
            if (s.last().get() == 2) result = result + 10000;
            s.remove(3);
            if (s.first().get() == 1) result = result + 100000;   // oldest remaining
            if (s.last().get() == 2) result = result + 1000000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1111111);
}

TEST_CASE("ListSet<int> — add rejects duplicates", "[libk][set][listset]") {
    auto j = jit_k(R"SRC(
        module __lset_dup__;
        test() : int {
            s : ListSet<int>;
            result : int = 0;
            if (s.add(1)) result = result + 1;
            if (s.add(2)) result = result + 10;
            if (!s.add(1)) result = result + 100;
            if (s.size() == 2) result = result + 1000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1111);
}

TEST_CASE("ListSet<int> — contains and remove", "[libk][set][listset]") {
    auto j = jit_k(R"SRC(
        module __lset_remove__;
        test() : int {
            s : ListSet<int>;
            s.add(1);
            s.add(2);
            s.add(3);
            result : int = 0;
            if (s.contains(2)) result = result + 1;
            if (s.remove(2)) result = result + 10;
            if (!s.contains(2)) result = result + 100;
            if (!s.remove(42)) result = result + 1000;
            if (s.size() == 2) result = result + 10000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 11111);
}

TEST_CASE("ListSet<int> — clear", "[libk][set][listset]") {
    auto j = jit_k(R"SRC(
        module __lset_clear__;
        test() : bool {
            s : ListSet<int>;
            s.add(1);
            s.add(2);
            s.clear();
            return s.isEmpty() && s.size() == 0 && !s.contains(1);
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<bool(*)()>("test");
    REQUIRE(fn);
    CHECK(fn());
}

TEST_CASE("ListSet<int> — forward and reverse iteration", "[libk][set][listset]") {
    auto j = jit_k(R"SRC(
        module __lset_iter__;
        test() : int {
            s : ListSet<int>;
            s.add(1);
            s.add(2);
            s.add(3);

            sum : int = 0;
            count : int = 0;
            it : ConstIterator<int>! = s.constIterator();
            cur : OptionalConstRef<int> = it.next();
            while (cur.hasValue()) {
                sum = sum + cur.get();
                count = count + 1;
                cur = it.next();
            }

            revSum : int = 0;
            revCount : int = 0;
            rit : ConstIterator<int>! = s.constReverseIterator();
            rcur : OptionalConstRef<int> = rit.next();
            while (rcur.hasValue()) {
                revSum = revSum + rcur.get();
                revCount = revCount + 1;
                rcur = rit.next();
            }

            if (sum != 6) return 1;
            if (count != 3) return 2;
            if (revSum != 6) return 3;
            if (revCount != 3) return 4;
            return 0;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

TEST_CASE("ListSet<int> — isSubsetOf / isSupersetOf", "[libk][set][listset]") {
    auto j = jit_k(R"SRC(
        module __lset_subset__;
        test() : int {
            a : ListSet<int>;
            a.add(1);
            a.add(2);

            b : ListSet<int>;
            b.add(1);
            b.add(2);
            b.add(3);

            result : int = 0;
            if (a.isSubsetOf(b)) result = result + 1;
            if (!b.isSubsetOf(a)) result = result + 10;
            if (b.isSupersetOf(a)) result = result + 100;
            if (!a.isSupersetOf(b)) result = result + 1000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1111);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  2. TreeSet<int>
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("TreeSet<int> — empty set", "[libk][set][treeset]") {
    auto j = jit_k(R"SRC(
        module __tset_empty__;
        test() : bool {
            s : TreeSet<int>;
            return s.isEmpty() && s.size() == 0 && !s.first().hasValue() && !s.last().hasValue();
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<bool(*)()>("test");
    REQUIRE(fn);
    CHECK(fn());
}

TEST_CASE("TreeSet<int> — add rejects duplicates and keeps order", "[libk][set][treeset]") {
    auto j = jit_k(R"SRC(
        module __tset_order__;
        test() : int {
            s : TreeSet<int>;
            s.add(5);
            s.add(3);
            s.add(8);
            s.add(1);
            s.add(4);
            result : int = 0;
            if (!s.add(3)) result = result + 1;
            if (s.size() == 5) result = result + 10;
            if (s.first().get() == 1) result = result + 100;
            if (s.last().get() == 8) result = result + 1000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1111);
}

TEST_CASE("TreeSet<int> — ascending iteration is sorted", "[libk][set][treeset]") {
    auto j = jit_k(R"SRC(
        module __tset_asc__;
        test() : int {
            s : TreeSet<int>;
            s.add(50);
            s.add(30);
            s.add(70);
            s.add(20);
            s.add(40);
            s.add(60);
            s.add(80);

            prev : int = -1000;
            count : int = 0;
            ok : bool = true;
            it : ConstIterator<int>! = s.constIterator();
            cur : OptionalConstRef<int> = it.next();
            while (cur.hasValue()) {
                if (cur.get() <= prev) ok = false;
                prev = cur.get();
                count = count + 1;
                cur = it.next();
            }
            if (!ok) return 1;
            if (count != 7) return 2;
            return 0;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

TEST_CASE("TreeSet<int> — descending iteration is sorted", "[libk][set][treeset]") {
    auto j = jit_k(R"SRC(
        module __tset_desc__;
        test() : int {
            s : TreeSet<int>;
            s.add(50);
            s.add(30);
            s.add(70);
            s.add(20);
            s.add(40);
            s.add(60);
            s.add(80);

            prev : int = 1000;
            count : int = 0;
            ok : bool = true;
            it : ConstIterator<int>! = s.constReverseIterator();
            cur : OptionalConstRef<int> = it.next();
            while (cur.hasValue()) {
                if (cur.get() >= prev) ok = false;
                prev = cur.get();
                count = count + 1;
                cur = it.next();
            }
            if (!ok) return 1;
            if (count != 7) return 2;
            return 0;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

TEST_CASE("TreeSet<int> — remove leaf, single-child and two-children nodes", "[libk][set][treeset]") {
    auto j = jit_k(R"SRC(
        module __tset_remove__;
        test() : int {
            s : TreeSet<int>;
            s.add(50);
            s.add(30);
            s.add(70);
            s.add(20);
            s.add(40);
            s.add(60);
            s.add(80);
            s.add(10);

            result : int = 0;
            // leaf removal
            if (s.remove(10)) result = result + 1;
            // single-child removal (20 now has no left child after 10 removed)
            if (s.remove(20)) result = result + 10;
            // two-children removal (root has two children)
            if (s.remove(50)) result = result + 100;
            if (!s.contains(10) && !s.contains(20) && !s.contains(50)) result = result + 1000;
            if (s.size() == 5) result = result + 10000;
            if (!s.remove(999)) result = result + 100000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 111111);
}

TEST_CASE("TreeSet<int> — many sequential inserts stay balanced and sorted", "[libk][set][treeset]") {
    auto j = jit_k(R"SRC(
        module __tset_seq__;
        test() : int {
            s : TreeSet<int>;
            i : int = 0;
            while (i < 200) {
                s.add(i);
                i = i + 1;
            }
            if (s.size() != 200) return 1;

            prev : int = -1;
            count : int = 0;
            ok : bool = true;
            it : ConstIterator<int>! = s.constIterator();
            cur : OptionalConstRef<int> = it.next();
            while (cur.hasValue()) {
                if (cur.get() <= prev) ok = false;
                prev = cur.get();
                count = count + 1;
                cur = it.next();
            }
            if (!ok) return 2;
            if (count != 200) return 3;

            // remove every other element and check size/order still consistent
            i = 0;
            while (i < 200) {
                s.remove(i);
                i = i + 2;
            }
            if (s.size() != 100) return 4;
            if (s.contains(0)) return 5;
            if (!s.contains(1)) return 6;
            return 0;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

TEST_CASE("TreeSet<int> — isSubsetOf / isSupersetOf", "[libk][set][treeset]") {
    auto j = jit_k(R"SRC(
        module __tset_subset__;
        test() : int {
            a : TreeSet<int>;
            a.add(1);
            a.add(2);

            b : TreeSet<int>;
            b.add(1);
            b.add(2);
            b.add(3);

            result : int = 0;
            if (a.isSubsetOf(b)) result = result + 1;
            if (!b.isSubsetOf(a)) result = result + 10;
            if (b.isSupersetOf(a)) result = result + 100;
            if (!a.isSupersetOf(b)) result = result + 1000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1111);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  3. HashSet<T> — requires T::hash() and T::operator==, so a small user class
//     (rather than a primitive) is used, matching the documented contract.
// ═══════════════════════════════════════════════════════════════════════════════

namespace {
constexpr const char* HASHABLE_ID_DECL = R"SRC(
        class Id {
            public:
            value : int;
            Id(v: int) { value = v; }
            const override hash() : int { return value; }
            const operator==(other: const Id&) : bool { return value == other.value; }
            const operator!=(other: const Id&) : bool { return value != other.value; }
        }
)SRC";
}

TEST_CASE("HashSet<Id> — empty set", "[libk][set][hashset]") {
    auto j = jit_k(std::string(R"SRC(
        module __hset_empty__;
)SRC") + HASHABLE_ID_DECL + R"SRC(
        test() : bool {
            s : HashSet<Id>;
            return s.isEmpty() && s.size() == 0;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<bool(*)()>("test");
    REQUIRE(fn);
    CHECK(fn());
}

TEST_CASE("HashSet<Id> — add rejects duplicates, contains and remove", "[libk][set][hashset]") {
    auto j = jit_k(std::string(R"SRC(
        module __hset_basic__;
)SRC") + HASHABLE_ID_DECL + R"SRC(
        test() : int {
            s : HashSet<Id>;
            result : int = 0;
            if (s.add(Id(1))) result = result + 1;
            if (s.add(Id(2))) result = result + 10;
            if (!s.add(Id(1))) result = result + 100;
            if (s.size() == 2) result = result + 1000;
            if (s.contains(Id(2))) result = result + 10000;
            if (s.remove(Id(2))) result = result + 100000;
            if (!s.contains(Id(2))) result = result + 1000000;
            if (!s.remove(Id(42))) result = result + 10000000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 11111111);
}

TEST_CASE("HashSet<Id> — clear", "[libk][set][hashset]") {
    auto j = jit_k(std::string(R"SRC(
        module __hset_clear__;
)SRC") + HASHABLE_ID_DECL + R"SRC(
        test() : bool {
            s : HashSet<Id>;
            s.add(Id(1));
            s.add(Id(2));
            s.clear();
            return s.isEmpty() && s.size() == 0 && !s.contains(Id(1));
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<bool(*)()>("test");
    REQUIRE(fn);
    CHECK(fn());
}

TEST_CASE("HashSet<Id> — iteration visits every distinct element exactly once", "[libk][set][hashset]") {
    auto j = jit_k(std::string(R"SRC(
        module __hset_iter__;
)SRC") + HASHABLE_ID_DECL + R"SRC(
        test() : int {
            s : HashSet<Id>;
            i : int = 0;
            while (i < 50) {
                s.add(Id(i));
                i = i + 1;
            }

            found : bool[50];
            j : int = 0;
            while (j < 50) { found[j] = false; j = j + 1; }

            count : int = 0;
            it : ConstIterator<Id>! = s.constIterator();
            cur : OptionalConstRef<Id> = it.next();
            while (cur.hasValue()) {
                v : int = cur.get().value;
                if (v >= 0 && v < 50) {
                    if (found[v]) return 1000 + v; // duplicate!
                    found[v] = true;
                }
                count = count + 1;
                cur = it.next();
            }
            if (count != 50) return 1;

            j = 0;
            while (j < 50) {
                if (!found[j]) return 2000 + j;
                j = j + 1;
            }
            return 0;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

TEST_CASE("HashSet<Id> — reverse iteration visits every distinct element exactly once", "[libk][set][hashset]") {
    auto j = jit_k(std::string(R"SRC(
        module __hset_reviter__;
)SRC") + HASHABLE_ID_DECL + R"SRC(
        test() : int {
            s : HashSet<Id>;
            i : int = 0;
            while (i < 30) {
                s.add(Id(i));
                i = i + 1;
            }

            count : int = 0;
            it : ConstIterator<Id>! = s.constReverseIterator();
            cur : OptionalConstRef<Id> = it.next();
            while (cur.hasValue()) {
                count = count + 1;
                cur = it.next();
            }
            if (count != 30) return 1;
            return 0;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

TEST_CASE("HashSet<Id> — grows (rehashes) past default capacity without losing elements", "[libk][set][hashset]") {
    auto j = jit_k(std::string(R"SRC(
        module __hset_grow__;
)SRC") + HASHABLE_ID_DECL + R"SRC(
        test() : int {
            s : HashSet<Id>;
            i : int = 0;
            while (i < 500) {
                if (!s.add(Id(i))) return 1;
                i = i + 1;
            }
            if (s.size() != 500) return 2;

            i = 0;
            while (i < 500) {
                if (!s.contains(Id(i))) return 3000 + i;
                i = i + 1;
            }

            // remove half, verify remaining
            i = 0;
            while (i < 500) {
                s.remove(Id(i));
                i = i + 2;
            }
            if (s.size() != 250) return 4;
            if (s.contains(Id(0))) return 5;
            if (!s.contains(Id(1))) return 6;
            return 0;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

TEST_CASE("HashSet<Id> — isSubsetOf / isSupersetOf", "[libk][set][hashset]") {
    auto j = jit_k(std::string(R"SRC(
        module __hset_subset__;
)SRC") + HASHABLE_ID_DECL + R"SRC(
        test() : int {
            a : HashSet<Id>;
            a.add(Id(1));
            a.add(Id(2));

            b : HashSet<Id>;
            b.add(Id(1));
            b.add(Id(2));
            b.add(Id(3));

            result : int = 0;
            if (a.isSubsetOf(b)) result = result + 1;
            if (!b.isSubsetOf(a)) result = result + 10;
            if (b.isSupersetOf(a)) result = result + 100;
            if (!a.isSupersetOf(b)) result = result + 1000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1111);
}
