/*
 * K Language standard library — Map tests (ListMap, TreeMap, HashMap)
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
//  1. ListMap<int, int>
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("ListMap<int,int> — empty map", "[libk][map][listmap]") {
    auto j = jit_k(R"SRC(
        module __lmap_empty__;
        test() : bool {
            m : ListMap<int, int>;
            return m.isEmpty() && m.size() == 0 && !m.first().hasValue() && !m.last().hasValue();
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<bool(*)()>("test");
    REQUIRE(fn);
    CHECK(fn());
}

TEST_CASE("ListMap<int,int> — put/get/containsKey/containsValue", "[libk][map][listmap]") {
    auto j = jit_k(R"SRC(
        module __lmap_basic__;
        test() : int {
            m : ListMap<int, int>;
            m.put(1, 10);
            m.put(2, 20);
            result : int = 0;
            if (m.size() == 2) result = result + 1;
            if (m.containsKey(1)) result = result + 10;
            if (!m.containsKey(3)) result = result + 100;
            if (m.containsValue(20)) result = result + 1000;
            if (!m.containsValue(99)) result = result + 10000;
            g : OptionalConstRef<int> = m.get(1);
            if (g.hasValue() && g.get() == 10) result = result + 100000;
            g2 : OptionalConstRef<int> = m.get(42);
            if (!g2.hasValue()) result = result + 1000000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1111111);
}

TEST_CASE("ListMap<int,int> — put updates value in place without moving order", "[libk][map][listmap]") {
    auto j = jit_k(R"SRC(
        module __lmap_update__;
        test() : int {
            m : ListMap<int, int>;
            m.put(1, 10);
            m.put(2, 20);
            m.put(1, 99);
            result : int = 0;
            if (m.size() == 2) result = result + 1;
            g : OptionalConstRef<int> = m.get(1);
            if (g.get() == 99) result = result + 10;
            if (m.first().get().key() == 1) result = result + 100;
            if (m.last().get().key() == 2) result = result + 1000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1111);
}

TEST_CASE("ListMap<int,int> — mutable get() allows in-place value mutation", "[libk][map][listmap]") {
    auto j = jit_k(R"SRC(
        module __lmap_mutget__;
        test() : int {
            m : ListMap<int, int>;
            m.put(1, 10);
            g : OptionalRef<int> = m.get(1);
            g.get() = 55;
            return m.get(1).get();
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 55);
}

TEST_CASE("ListMap<int,int> — putIfAbsent leaves existing value untouched", "[libk][map][listmap]") {
    auto j = jit_k(R"SRC(
        module __lmap_putifabsent__;
        test() : int {
            m : ListMap<int, int>;
            m.put(1, 10);
            r1 : OptionalRef<int> = m.putIfAbsent(1, 999);
            r2 : OptionalRef<int> = m.putIfAbsent(2, 20);
            result : int = 0;
            if (r1.hasValue() && r1.get() == 10) result = result + 1;
            if (!r2.hasValue()) result = result + 10;
            if (m.get(1).get() == 10) result = result + 100;
            if (m.get(2).get() == 20) result = result + 1000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1111);
}

TEST_CASE("ListMap<int,int> — replace with insertIfAbsent true/false", "[libk][map][listmap]") {
    auto j = jit_k(R"SRC(
        module __lmap_replace__;
        test() : int {
            m : ListMap<int, int>;
            m.put(1, 10);
            result : int = 0;
            prev : Optional<int> = m.replace(1, 11);
            if (prev.hasValue() && prev.get() == 10) result = result + 1;
            if (m.get(1).get() == 11) result = result + 10;

            absentNoInsert : Optional<int> = m.replace(2, 20, false);
            if (!absentNoInsert.hasValue()) result = result + 100;
            if (!m.containsKey(2)) result = result + 1000;

            absentInsert : Optional<int> = m.replace(3, 30);
            if (!absentInsert.hasValue()) result = result + 10000;
            if (m.get(3).get() == 30) result = result + 100000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 111111);
}

TEST_CASE("ListMap<int,int> — remove and clear", "[libk][map][listmap]") {
    auto j = jit_k(R"SRC(
        module __lmap_remove__;
        test() : int {
            m : ListMap<int, int>;
            m.put(1, 10);
            m.put(2, 20);
            result : int = 0;
            if (m.remove(1)) result = result + 1;
            if (!m.containsKey(1)) result = result + 10;
            if (!m.remove(99)) result = result + 100;
            if (m.size() == 1) result = result + 1000;
            m.clear();
            if (m.isEmpty() && m.size() == 0) result = result + 10000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 11111);
}

TEST_CASE("ListMap<int,int> — const iteration visits entries in insertion order", "[libk][map][listmap]") {
    auto j = jit_k(R"SRC(
        module __lmap_iter__;
        test() : int {
            m : ListMap<int, int>;
            m.put(1, 10);
            m.put(2, 20);
            m.put(3, 30);
            it : ConstIterator<Entry<int,int>>! = m.constIterator();
            sum : int = 0;
            cur : OptionalConstRef<Entry<int,int>> = it.next();
            order : int = 1;
            while (cur.hasValue()) {
                sum = sum + cur.get().key() * order + cur.get().value();
                order = order * 10;
                cur = it.next();
            }
            return sum;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    // order=1: 1*1+10=11 ; order=10: 2*10+20=40 -> running sum 51 ; order=100: 3*100+30=330 -> 381
    CHECK(fn() == 381);
}

TEST_CASE("ListMap<int,int> — mutable iteration updates values in place", "[libk][map][listmap]") {
    auto j = jit_k(R"SRC(
        module __lmap_mutiter__;
        test() : int {
            m : ListMap<int, int>;
            m.put(1, 10);
            m.put(2, 20);
            it : Iterator<MutableEntry<int,int>>! = m.iterator();
            cur : OptionalRef<MutableEntry<int,int>> = it.next();
            while (cur.hasValue()) {
                cur.get().value(cur.get().value() + 1);
                cur = it.next();
            }
            return m.get(1).get() + m.get(2).get();
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 32);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  2. TreeMap<int, int>
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("TreeMap<int,int> — empty map", "[libk][map][treemap]") {
    auto j = jit_k(R"SRC(
        module __tmap_empty__;
        test() : bool {
            m : TreeMap<int, int>;
            return m.isEmpty() && m.size() == 0 && !m.first().hasValue() && !m.last().hasValue();
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<bool(*)()>("test");
    REQUIRE(fn);
    CHECK(fn());
}

TEST_CASE("TreeMap<int,int> — put updates existing key and keeps sorted order", "[libk][map][treemap]") {
    auto j = jit_k(R"SRC(
        module __tmap_basic__;
        test() : int {
            m : TreeMap<int, int>;
            m.put(5, 50);
            m.put(1, 10);
            m.put(3, 30);
            m.put(1, 99);
            result : int = 0;
            if (m.size() == 3) result = result + 1;
            if (m.get(1).get() == 99) result = result + 10;
            if (m.first().get().key() == 1) result = result + 100;
            if (m.last().get().key() == 5) result = result + 1000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1111);
}

TEST_CASE("TreeMap<int,int> — ascending iteration is sorted by key", "[libk][map][treemap]") {
    auto j = jit_k(R"SRC(
        module __tmap_iter__;
        test() : int {
            m : TreeMap<int, int>;
            m.put(5, 50);
            m.put(1, 10);
            m.put(4, 40);
            m.put(2, 20);
            m.put(3, 30);
            it : ConstIterator<Entry<int,int>>! = m.constIterator();
            cur : OptionalConstRef<Entry<int,int>> = it.next();
            lastKey : int = -1;
            ascending : bool = true;
            count : int = 0;
            while (cur.hasValue()) {
                if (cur.get().key() <= lastKey) {
                    ascending = false;
                }
                lastKey = cur.get().key();
                count = count + 1;
                cur = it.next();
            }
            if (ascending && count == 5) {
                return 1;
            }
            return 0;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

TEST_CASE("TreeMap<int,int> — mutable iteration updates values in place", "[libk][map][treemap]") {
    auto j = jit_k(R"SRC(
        module __tmap_mutiter__;
        test() : int {
            m : TreeMap<int, int>;
            m.put(1, 10);
            m.put(2, 20);
            m.put(3, 30);
            it : Iterator<MutableEntry<int,int>>! = m.iterator();
            cur : OptionalRef<MutableEntry<int,int>> = it.next();
            while (cur.hasValue()) {
                cur.get().value(cur.get().value() * 2);
                cur = it.next();
            }
            return m.get(1).get() + m.get(2).get() + m.get(3).get();
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 120);
}

TEST_CASE("TreeMap<int,int> — remove leaf, single-child and two-children nodes, stays balanced", "[libk][map][treemap]") {
    auto j = jit_k(R"SRC(
        module __tmap_remove__;
        test() : int {
            m : TreeMap<int, int>;
            i : int = 1;
            while (i <= 15) {
                m.put(i, i * 100);
                i = i + 1;
            }
            result : int = 0;
            if (m.remove(1)) result = result + 1;      // leaf-ish
            if (m.remove(8)) result = result + 10;      // root-ish, two children
            if (m.remove(15)) result = result + 100;    // far leaf
            if (!m.remove(1)) result = result + 1000;   // already removed
            if (m.size() == 12) result = result + 10000;
            // Remaining entries must still be sorted and complete.
            it : ConstIterator<Entry<int,int>>! = m.constIterator();
            cur : OptionalConstRef<Entry<int,int>> = it.next();
            lastKey : int = -1;
            count : int = 0;
            ascending : bool = true;
            while (cur.hasValue()) {
                if (cur.get().key() <= lastKey) { ascending = false; }
                lastKey = cur.get().key();
                count = count + 1;
                cur = it.next();
            }
            if (ascending && count == 12) result = result + 100000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 111111);
}

TEST_CASE("TreeMap<int,int> — putIfAbsent and replace semantics", "[libk][map][treemap]") {
    auto j = jit_k(R"SRC(
        module __tmap_putifabsent__;
        test() : int {
            m : TreeMap<int, int>;
            m.put(1, 10);
            result : int = 0;
            r1 : OptionalRef<int> = m.putIfAbsent(1, 999);
            if (r1.hasValue() && r1.get() == 10) result = result + 1;
            if (m.get(1).get() == 10) result = result + 10;

            r2 : OptionalRef<int> = m.putIfAbsent(2, 20);
            if (!r2.hasValue()) result = result + 100;
            if (m.get(2).get() == 20) result = result + 1000;

            prev : Optional<int> = m.replace(2, 25);
            if (prev.hasValue() && prev.get() == 20) result = result + 10000;

            noInsert : Optional<int> = m.replace(3, 30, false);
            if (!noInsert.hasValue() && !m.containsKey(3)) result = result + 100000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 111111);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  3. HashMap<K,V> — requires K::hash() and K::operator==, so a small user class
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

TEST_CASE("HashMap<Id,int> — empty map", "[libk][map][hashmap]") {
    auto j = jit_k(std::string(R"SRC(
        module __hmap_empty__;
)SRC") + HASHABLE_ID_DECL + R"SRC(
        test() : bool {
            m : HashMap<Id, int>;
            return m.isEmpty() && m.size() == 0;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<bool(*)()>("test");
    REQUIRE(fn);
    CHECK(fn());
}

TEST_CASE("HashMap<Id,int> — put/get/containsKey/containsValue and value update", "[libk][map][hashmap]") {
    auto j = jit_k(std::string(R"SRC(
        module __hmap_basic__;
)SRC") + HASHABLE_ID_DECL + R"SRC(
        test() : int {
            m : HashMap<Id, int>;
            m.put(Id(1), 10);
            m.put(Id(2), 20);
            m.put(Id(1), 99);
            result : int = 0;
            if (m.size() == 2) result = result + 1;
            if (m.containsKey(Id(1))) result = result + 10;
            if (!m.containsKey(Id(3))) result = result + 100;
            if (m.containsValue(20)) result = result + 1000;
            if (m.get(Id(1)).get() == 99) result = result + 10000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 11111);
}

TEST_CASE("HashMap<Id,int> — putIfAbsent, replace and remove", "[libk][map][hashmap]") {
    auto j = jit_k(std::string(R"SRC(
        module __hmap_mutations__;
)SRC") + HASHABLE_ID_DECL + R"SRC(
        test() : int {
            m : HashMap<Id, int>;
            m.put(Id(1), 10);
            result : int = 0;

            r1 : OptionalRef<int> = m.putIfAbsent(Id(1), 999);
            if (r1.hasValue() && r1.get() == 10) result = result + 1;

            r2 : OptionalRef<int> = m.putIfAbsent(Id(2), 20);
            if (!r2.hasValue() && m.get(Id(2)).get() == 20) result = result + 10;

            prev : Optional<int> = m.replace(Id(2), 25);
            if (prev.hasValue() && prev.get() == 20 && m.get(Id(2)).get() == 25) result = result + 100;

            noInsert : Optional<int> = m.replace(Id(3), 30, false);
            if (!noInsert.hasValue() && !m.containsKey(Id(3))) result = result + 1000;

            if (m.remove(Id(1)) && !m.containsKey(Id(1))) result = result + 10000;
            if (!m.remove(Id(42))) result = result + 100000;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 111111);
}

TEST_CASE("HashMap<Id,int> — clear", "[libk][map][hashmap]") {
    auto j = jit_k(std::string(R"SRC(
        module __hmap_clear__;
)SRC") + HASHABLE_ID_DECL + R"SRC(
        test() : bool {
            m : HashMap<Id, int>;
            m.put(Id(1), 10);
            m.put(Id(2), 20);
            m.clear();
            return m.isEmpty() && m.size() == 0 && !m.containsKey(Id(1));
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<bool(*)()>("test");
    REQUIRE(fn);
    CHECK(fn());
}

TEST_CASE("HashMap<Id,int> — iteration visits every distinct entry exactly once", "[libk][map][hashmap]") {
    auto j = jit_k(std::string(R"SRC(
        module __hmap_iter__;
)SRC") + HASHABLE_ID_DECL + R"SRC(
        test() : int {
            m : HashMap<Id, int>;
            i : int = 0;
            while (i < 20) {
                m.put(Id(i), i * 10);
                i = i + 1;
            }
            it : ConstIterator<Entry<Id,int>>! = m.constIterator();
            seen : bool[20];
            j : int = 0;
            while (j < 20) { seen[j] = false; j = j + 1; }
            count : int = 0;
            valid : bool = true;
            cur : OptionalConstRef<Entry<Id,int>> = it.next();
            while (cur.hasValue()) {
                k : int = cur.get().key().value;
                if (k < 0 || k >= 20 || seen[k] || cur.get().value() != k * 10) {
                    valid = false;
                }
                seen[k] = true;
                count = count + 1;
                cur = it.next();
            }
            if (valid && count == 20) {
                return 1;
            }
            return 0;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

TEST_CASE("HashMap<Id,int> — mutable iteration updates values in place", "[libk][map][hashmap]") {
    auto j = jit_k(std::string(R"SRC(
        module __hmap_mutiter__;
)SRC") + HASHABLE_ID_DECL + R"SRC(
        test() : int {
            m : HashMap<Id, int>;
            m.put(Id(1), 10);
            m.put(Id(2), 20);
            it : Iterator<MutableEntry<Id,int>>! = m.iterator();
            cur : OptionalRef<MutableEntry<Id,int>> = it.next();
            while (cur.hasValue()) {
                cur.get().value(cur.get().value() + 1);
                cur = it.next();
            }
            return m.get(Id(1)).get() + m.get(Id(2)).get();
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 32);
}

TEST_CASE("HashMap<Id,int> — grows (rehashes) past default capacity without losing entries", "[libk][map][hashmap]") {
    auto j = jit_k(std::string(R"SRC(
        module __hmap_rehash__;
)SRC") + HASHABLE_ID_DECL + R"SRC(
        test() : int {
            m : HashMap<Id, int>;
            i : int = 0;
            while (i < 100) {
                m.put(Id(i), i);
                i = i + 1;
            }
            result : int = 0;
            if (m.size() == 100) result = result + 1;
            allFound : bool = true;
            j : int = 0;
            while (j < 100) {
                if (m.get(Id(j)).get() != j) { allFound = false; }
                j = j + 1;
            }
            if (allFound) result = result + 10;
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 11);
}
