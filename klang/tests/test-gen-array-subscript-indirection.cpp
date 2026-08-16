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
 * any indirection: reference (&), pointer (*), link (+), view (?),
 * and owner (!).
 */

#include <catch2/catch_all.hpp>

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
// NEW: subscript through link (+)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Subscript through link — read", "[gen][subscript-indirection]") {
    auto jit = gen_jit(R"SRC(
        module test;

        get(l : int[3]+, i : int) : int {
            return l[i];
        }

        test() : int {
            arr : int[3]{10, 20, 30};
            l : int[3]+ = &arr;
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

        set(l : int[3]+, i : int, v : int) {
            l[i] = v;
        }

        test() : int {
            arr : int[3]{10, 20, 30};
            l : int[3]+ = &arr;
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
// NEW: subscript through view (?)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Subscript through view — read", "[gen][subscript-indirection]") {
    auto jit = gen_jit(R"SRC(
        module test;

        get(q : int[3]?, i : int) : int {
            return q[i];
        }

        test() : int {
            arr : int[3]{10, 20, 30};
            q : int[3]? = &arr;
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
            l : Point[2]+ = &arr;
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
            arr : int+[]{&a, &b, &c};
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
            arr : int+[]{&a, &b};
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
// Arrays of view (?)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Array of view — read", "[gen][subscript-indirection][array-of-indir]") {
    auto jit = gen_jit(R"SRC(
        module test;

        test() : int {
            a : int = 4;
            b : int = 5;
            c : int = 6;
            arr : int?[]{&a, &b, &c};
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

// ─────────────────────────────────────────────────────────────────────────────
// Subscript on owner array member of struct (regression)
//
// These tests exercise owner array member subscript in different access modes:
//   1) External: h._buf[0]  (member accessed from outside the struct)
//   2) Via method using implicit 'this': _buf[i]
//   3) Via const method
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Subscript on owner array member — external access", "[gen][subscript-indirection][owner-member]") {
    auto jit = gen_jit(R"SRC(
        module __ext_owner_subscript__;

        struct Holder {
            _buf : char[]!;
            Holder(buf : char[]!) : _buf(buf) {}
            ~Holder() { delete _buf; }
        }

        test() : int {
            sz : unsigned int = 3u;
            buf : char[]! = new char[sz];
            buf[0] = 'A';
            buf[1] = 'B';
            buf[2] = 'C';
            h : Holder(buf);
            result : int = 0;
            if (h._buf[0] == 'A') ++result;
            if (h._buf[1] == 'B') result += 10;
            if (h._buf[2] == 'C') result += 100;
            return result;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 111);
}

TEST_CASE("Subscript on owner array member via non-const method (implicit this)", "[gen][subscript-indirection][owner-member]") {
    auto jit = gen_jit(R"SRC(
        module __const_owner_subscript__;

        struct Holder {
            _buf : char[]!;
            _size : int;
            Holder(buf : char[]!, sz : int) : _buf(buf), _size(sz) {}
            ~Holder() { delete _buf; }
            const at(i : int) : char { return _buf[i]; }
        }

        test() : int {
            sz : unsigned int = 3u;
            buf : char[]! = new char[sz];
            buf[0] = 'A';
            buf[1] = 'B';
            buf[2] = 'C';
            h : Holder(buf, 3);
            result : int = 0;
            if (h.at(0) == 'A') ++result;
            if (h.at(1) == 'B') result += 10;
            if (h.at(2) == 'C') result += 100;
            return result;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 111);
}

TEST_CASE("Subscript on owner int array member via const method", "[gen][subscript-indirection][owner-member]") {
    auto jit = gen_jit(R"SRC(
        module __const_owner_int_subscript__;

        struct IntArray {
            _data : int[]!;
            _len  : int;
            IntArray(d : int[]!, n : int) : _data(d), _len(n) {}
            ~IntArray() { delete _data; }
            const get(i : int) : int { return _data[i]; }
        }

        test() : int {
            sz : unsigned int = 3u;
            d : int[]! = new int[sz];
            d[0] = 10;
            d[1] = 20;
            d[2] = 30;
            a : IntArray(d, 3);
            return a.get(0) + a.get(1) + a.get(2);
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 60);
}

TEST_CASE("Subscript on owner array member via non-const method (mutable write)", "[gen][subscript-indirection][owner-member]") {
    auto jit = gen_jit(R"SRC(
        module __mut_owner_subscript__;

        struct Buffer {
            _buf : char[]!;
            _size : int;
            Buffer(buf : char[]!, sz : int) : _buf(buf), _size(sz) {}
            ~Buffer() { delete _buf; }
            set(i : int, c : char) { _buf[i] = c; }
            const get(i : int) : char { return _buf[i]; }
        }

        test() : int {
            sz : unsigned int = 2u;
            buf : char[]! = new char[sz];
            buf[0] = 'X';
            buf[1] = 'Y';
            b : Buffer(buf, 2);
            b.set(0, 'Z');
            result : int = 0;
            if (b.get(0) == 'Z') ++result;
            if (b.get(1) == 'Y') result += 10;
            return result;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test);
    CHECK(test() == 11);
}

