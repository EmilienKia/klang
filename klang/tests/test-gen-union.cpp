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

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

using namespace k::model;
using namespace k::model::gen;
using namespace k::parse;
using namespace k::parse::ast;

// ============================================================
// Phase 1: Basic union declaration — parsing and type creation
// ============================================================

TEST_CASE("Union basic declaration compiles", "[gen][union]") {
    auto jit = gen_jit(R"(
        module test;
        union MyUnion {
            first: int;
            second: long;
        }
        fun get_size() : int {
            return 1;
        }
    )");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("get_size");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}

TEST_CASE("Union variable declaration and default construction", "[gen][union]") {
    auto jit = gen_jit(R"(
        module test;
        union MyUnion {
            first: int;
            second: long;
        }
        fun test_default() : int {
            u : MyUnion;
            return 0;
        }
    )", false);
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_default");
    REQUIRE(fn);
    REQUIRE(fn() == 0);
}

TEST_CASE("Union explicit member write and read", "[gen][union]") {
    auto jit = gen_jit(R"(
        module test;
        union MyUnion {
            first: int;
            second: long;
        }
        fun test_write_read() : int {
            u : MyUnion;
            u.first = 42;
            return u.first;
        }
    )");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_write_read");
    REQUIRE(fn);
    REQUIRE(fn() == 42);
}

TEST_CASE("Union typed construction with initializer", "[gen][union]") {
    auto jit = gen_jit(R"(
        module test;
        union MyUnion {
            first: int;
            second: long;
        }
        fun test_init() : int {
            u : MyUnion = 25;
            return u.first;
        }
    )");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_init");
    REQUIRE(fn);
    REQUIRE(fn() == 25);
}

TEST_CASE("Union second alternative access", "[gen][union]") {
    auto jit = gen_jit(R"(
        module test;
        union MyUnion {
            first: int;
            second: long;
        }
        fun test_second() : long {
            u : MyUnion;
            u.second = 100;
            return u.second;
        }
    )", false, false);
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<long(*)()>("test_second");
    REQUIRE(fn);
    REQUIRE(fn() == 100L);
}

TEST_CASE("Union drain addresser rejected", "[gen][union]") {
    REQUIRE(compile_should_fail(R"(
        module test;
        union MyUnion {
            first: int;
            second: int#;
        }
        fun dummy() : int { return 0; }
    )", nullptr));
}

TEST_CASE("Union multiple alternatives different sizes", "[gen][union]") {
    auto jit = gen_jit(R"(
        module test;
        union NumUnion {
            i: int;
            l: long;
            b: byte;
        }
        fun test_int() : int {
            u : NumUnion;
            u.i = 42;
            return u.i;
        }
        fun test_long() : long {
            u : NumUnion;
            u.l = 1000000;
            return u.l;
        }
        fun test_byte() : byte {
            u : NumUnion;
            u.b = 7;
            return u.b;
        }
    )");
    REQUIRE(jit);
    auto fn_int = jit->lookup_symbol<int(*)()>("test_int");
    REQUIRE(fn_int);
    REQUIRE(fn_int() == 42);
    auto fn_long = jit->lookup_symbol<long(*)()>("test_long");
    REQUIRE(fn_long);
    REQUIRE(fn_long() == 1000000L);
    auto fn_byte = jit->lookup_symbol<int8_t(*)()>("test_byte");
    REQUIRE(fn_byte);
    REQUIRE(fn_byte() == 7);
}

TEST_CASE("Union passed to function by reference", "[gen][union]") {
    auto jit = gen_jit(R"(
        module test;
        union MyUnion {
            first: int;
            second: long;
        }
        fun read_first(u: MyUnion&) : int {
            return u.first;
        }
        fun test_ref() : int {
            u : MyUnion;
            u.first = 99;
            return read_first(u);
        }
    )");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_ref");
    REQUIRE(fn);
    REQUIRE(fn() == 99);
}

// ============================================================
// Phase 2: Union with struct alternatives
// ============================================================

TEST_CASE("Union with struct alternative — write and read fields", "[gen][union][structs]") {
    auto jit = gen_jit(R"(
        module test;
        struct Point {
            x: int = 0;
            y: int = 0;
        }
        union ShapeData {
            point: Point;
            radius: int;
        }
        fun test_struct_alt() : int {
            u : ShapeData;
            p : Point;
            p.x = 10;
            p.y = 20;
            u.point = p;
            return u.point.x + u.point.y;
        }
    )");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_struct_alt");
    REQUIRE(fn);
    REQUIRE(fn() == 30);
}

TEST_CASE("Union with struct alternative — default construction", "[gen][union][structs]") {
    auto jit = gen_jit(R"(
        module test;
        struct Pair {
            a: int = 5;
            b: int = 7;
        }
        union PairOrInt {
            pair: Pair;
            value: int;
        }
        fun test_default_struct() : int {
            u : PairOrInt;
            return u.pair.a + u.pair.b;
        }
    )");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_default_struct");
    REQUIRE(fn);
    // Default-initialized union has discriminant=0, first alt (Pair) is zero-initialized memory
    // struct fields are zero because storage is zero-inited, not default-constructed
    REQUIRE(fn() == 0);
}

TEST_CASE("Union with struct — switch between struct and primitive", "[gen][union][structs]") {
    auto jit = gen_jit(R"(
        module test;
        struct Vec2 {
            x: int = 0;
            y: int = 0;
        }
        union VecOrScalar {
            vec: Vec2;
            scalar: int;
        }
        fun test_switch() : int {
            u : VecOrScalar;
            u.scalar = 42;
            return u.scalar;
        }
    )");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_switch");
    REQUIRE(fn);
    REQUIRE(fn() == 42);
}

TEST_CASE("Union with struct — struct field modification via reference", "[gen][union][structs]") {
    auto jit = gen_jit(R"(
        module test;
        struct Rect {
            w: int = 0;
            h: int = 0;
        }
        union Shape {
            rect: Rect;
            radius: int;
        }
        fun test_struct_ref() : int {
            u : Shape;
            r : Rect;
            r.w = 3;
            r.h = 4;
            u.rect = r;
            return u.rect.w * u.rect.h;
        }
    )");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_struct_ref");
    REQUIRE(fn);
    REQUIRE(fn() == 12);
}



// ============================================================
// Phase 3: Union with class alternatives
// ============================================================

TEST_CASE("Union with class alternative — basic instantiation", "[gen][union][class]") {
    auto jit = gen_jit(R"(
        module test;
        class Counter {
            count: int;
            Counter() : count(0) {}
            increment() { ++count; }
            get() : int { return count; }
        }
        union ValueOrCounter {
            value: int;
            counter: Counter;
        }
        fun test_class_alt() : int {
            u : ValueOrCounter;
            u.value = 77;
            return u.value;
        }
    )");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_class_alt");
    REQUIRE(fn);
    REQUIRE(fn() == 77);
}

TEST_CASE("Union with struct and class mixed — access different alts", "[gen][union][class][structs]") {
    auto jit = gen_jit(R"(
        module test;
        struct Coords {
            x: int = 0;
            y: int = 0;
        }
        class Label {
            id: int;
            Label() : id(0) {}
            Label(v: int) : id(v) {}
            get_id() : int { return id; }
        }
        union Element {
            coords: Coords;
            label: Label;
            tag: int;
        }
        fun test_mixed_tag() : int {
            u : Element;
            u.tag = 123;
            return u.tag;
        }
        fun test_mixed_coords() : int {
            u : Element;
            c : Coords;
            c.x = 5;
            c.y = 8;
            u.coords = c;
            return u.coords.x + u.coords.y;
        }
    )");
    REQUIRE(jit);
    auto fn_tag = jit->lookup_symbol<int(*)()>("test_mixed_tag");
    REQUIRE(fn_tag);
    REQUIRE(fn_tag() == 123);
    auto fn_coords = jit->lookup_symbol<int(*)()>("test_mixed_coords");
    REQUIRE(fn_coords);
    REQUIRE(fn_coords() == 13);
}

TEST_CASE("Union with pointer alternative", "[gen][union][structs]") {
    auto jit = gen_jit(R"(
        module test;
        struct Data {
            val: int = 0;
        }
        union PtrOrVal {
            ptr: Data*;
            val: int;
        }
        fun test_ptr_alt() : int {
            u : PtrOrVal;
            u.val = 55;
            return u.val;
        }
    )");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_ptr_alt");
    REQUIRE(fn);
    REQUIRE(fn() == 55);
}

// ============================================================
// Phase 4: Discriminant update on alternative assignment
// ============================================================

TEST_CASE("Union discriminant updated on alternative switch", "[gen][union]") {
    auto jit = gen_jit(R"(
        module test;
        union MyUnion {
            first: int;
            second: long;
            third: byte;
        }
        fun test_switch_disc() : int {
            u : MyUnion;
            u.first = 10;
            u.second = 20;
            u.third = 30;
            // After assigning to third (index 2), accessing first (index 0) should fail
            // but we can verify the last assigned alternative is readable
            return u.third;
        }
    )", false, false);
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int8_t(*)()>("test_switch_disc");
    REQUIRE(fn);
    REQUIRE(fn() == 30);
}

TEST_CASE("Union discriminant tracks last assigned alternative", "[gen][union]") {
    auto jit = gen_jit(R"(
        module test;
        union NumVal {
            i: int;
            l: long;
        }
        fun get_disc(u: NumVal&) : int {
            // Assign to l (index 1) then read back l
            u.l = 99;
            return 1;
        }
        fun test_disc_track() : long {
            u : NumVal;
            u.i = 5;
            get_disc(u);
            return u.l;
        }
    )");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<long(*)()>("test_disc_track");
    REQUIRE(fn);
    REQUIRE(fn() == 99L);
}

TEST_CASE("Union struct assignment updates discriminant", "[gen][union][structs]") {
    auto jit = gen_jit(R"(
        module test;
        struct Vec2 {
            x: int = 0;
            y: int = 0;
        }
        union VecOrInt {
            vec: Vec2;
            val: int;
        }
        fun test_struct_disc() : int {
            u : VecOrInt;
            v : Vec2;
            v.x = 3;
            v.y = 4;
            u.vec = v;
            // Now switch to the int alternative
            u.val = 42;
            return u.val;
        }
    )");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_struct_disc");
    REQUIRE(fn);
    REQUIRE(fn() == 42);
}

// ============================================================
// Phase 5: Mangling — unions in function signatures
// ============================================================

TEST_CASE("Union passed by value to function", "[gen][union]") {
    auto jit = gen_jit(R"(
        module test;
        union IntOrLong {
            i: int;
            l: long;
        }
        fun use_val(u: IntOrLong) : int {
            return u.i;
        }
        fun test_by_val() : int {
            u : IntOrLong;
            u.i = 77;
            return use_val(u);
        }
    )");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_by_val");
    REQUIRE(fn);
    REQUIRE(fn() == 77);
}

// ============================================================
// Phase 6: KDI export/import — unions across modules
// ============================================================

TEST_CASE("Union exported to shared library and used from another module", "[gen][union][import]") {
    auto result = build_exec_with_lib(R"(
        module mylib;
        public:
        union IntOrLong {
            i: int;
            l: long;
        }
        fun get_forty_two() : int {
            u : IntOrLong;
            u.i = 42;
            return u.i;
        }
    )", R"(
        module main;
        import mylib;
        fun main() : int {
            return mylib::get_forty_two();
        }
    )");
    REQUIRE(result.exit_code == 42);
}

TEST_CASE("Union passed by reference across modules", "[gen][union][import]") {
    auto result = build_exec_with_lib(R"(
        module mylib;
        public:
        union IntOrLong {
            i: int;
            l: long;
        }
        fun read_int(u: IntOrLong&) : int {
            return u.i;
        }
    )", R"(
        module main;
        import mylib;
        fun main() : int {
            u : mylib::IntOrLong;
            u.i = 99;
            return mylib::read_int(u);
        }
    )");
    REQUIRE(result.exit_code == 99);
}

// ============================================================
// Nested unions inside aggregates
// ============================================================

TEST_CASE("Nested union inside struct - basic declaration and usage", "[gen][union][nested]") {
    auto jit = gen_jit(R"(
        module test;
        struct Container {
            union Value {
                i: int;
                l: long;
            }
            v: Value;
            tag: int;
        }
        test_nested_union() : int {
            c : Container;
            c.v.i = 42;
            c.tag = 1;
            return c.v.i;
        }
    )");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_nested_union");
    REQUIRE(fn);
    REQUIRE(fn() == 42);
}

TEST_CASE("Nested union inside struct - multiple alternatives", "[gen][union][nested]") {
    auto jit = gen_jit(R"(
        module test;
        struct Wrapper {
            union Data {
                x: int;
                y: long;
                z: double;
            }
            data: Data;
        }
        test_int() : int {
            w : Wrapper;
            w.data.x = 7;
            return w.data.x;
        }
        test_long() : long {
            w : Wrapper;
            w.data.y = 123L;
            return w.data.y;
        }
    )");
    REQUIRE(jit);
    auto fn_int = jit->lookup_symbol<int(*)()>("test_int");
    REQUIRE(fn_int);
    REQUIRE(fn_int() == 7);
    auto fn_long = jit->lookup_symbol<long(*)()>("test_long");
    REQUIRE(fn_long);
    REQUIRE(fn_long() == 123L);
}

TEST_CASE("Nested union inside class", "[gen][union][nested]") {
    auto jit = gen_jit(R"(
        module test;
        class MyClass {
            public:
            union Result {
                value: int;
                error: long;
            }
            result: Result;
            getResult() : int {
                return result.value;
            }
        }
        test_class_nested_union() : int {
            obj : MyClass;
            obj.result.value = 55;
            return obj.getResult();
        }
    )");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_class_nested_union");
    REQUIRE(fn);
    REQUIRE(fn() == 55);
}

TEST_CASE("Nested union type used as function parameter type", "[gen][union][nested]") {
    auto jit = gen_jit(R"(
        module test;
        struct Outer {
            union Inner {
                a: int;
                b: long;
            }
        }
        read_inner(u: Outer::Inner&) : int {
            return u.a;
        }
        test_param() : int {
            o : Outer;
            inner : Outer::Inner;
            inner.a = 33;
            return read_inner(inner);
        }
    )");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_param");
    REQUIRE(fn);
    REQUIRE(fn() == 33);
}


// ============================================================
// Phase 7: Union Kind enum and index() intrinsic
// ============================================================

TEST_CASE("union Kind enum basic values", "[gen][union][kind]") {
    auto jit = gen_jit(R"(
        module test;
        union Value {
            i: int;
            d: double;
            s: byte;
        }
        get_first_kind() : int {
            k : int = Value::Kind::i;
            return k;
        }
        get_second_kind() : int {
            k : int = Value::Kind::d;
            return k;
        }
        get_third_kind() : int {
            k : int = Value::Kind::s;
            return k;
        }
    )");
    REQUIRE(jit);
    auto fn0 = jit->lookup_symbol<int(*)()>("get_first_kind");
    auto fn1 = jit->lookup_symbol<int(*)()>("get_second_kind");
    auto fn2 = jit->lookup_symbol<int(*)()>("get_third_kind");
    REQUIRE(fn0);
    REQUIRE(fn1);
    REQUIRE(fn2);
    REQUIRE(fn0() == 0);
    REQUIRE(fn1() == 1);
    REQUIRE(fn2() == 2);
}

TEST_CASE("union index() basic", "[gen][union][kind]") {
    auto jit = gen_jit(R"(
        module test;
        union Value {
            i: int;
            d: double;
        }
        test_index_int() : int {
            v : Value;
            v.i = 42;
            idx : int = v.index();
            return idx;
        }
        test_index_double() : int {
            v : Value;
            v.d = 3.14;
            idx : int = v.index();
            return idx;
        }
    )");
    REQUIRE(jit);
    auto fn_i = jit->lookup_symbol<int(*)()>("test_index_int");
    auto fn_d = jit->lookup_symbol<int(*)()>("test_index_double");
    REQUIRE(fn_i);
    REQUIRE(fn_d);
    REQUIRE(fn_i() == 0);
    REQUIRE(fn_d() == 1);
}

TEST_CASE("union index() after reassignment", "[gen][union][kind]") {
    auto jit = gen_jit(R"(
        module test;
        union Value {
            i: int;
            d: double;
        }
        test_reassign() : int {
            v : Value;
            v.i = 10;
            v.d = 2.5;
            idx : int = v.index();
            return idx;
        }
    )");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_reassign");
    REQUIRE(fn);
    REQUIRE(fn() == 1); // last assignment was .d (index 1)
}

TEST_CASE("union index() compared with Kind enum", "[gen][union][kind]") {
    auto jit = gen_jit(R"(
        module test;
        union Value {
            i: int;
            d: double;
            b: bool;
        }
        test_compare() : int {
            v : Value;
            v.d = 1.0;
            if(v.index() == Value::Kind::d) {
                return 1;
            }
            return 0;
        }
    )");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_compare");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}

TEST_CASE("union index() on nested union in struct", "[gen][union][kind]") {
    auto jit = gen_jit(R"(
        module test;
        struct Container {
            union Inner {
                a: int;
                b: double;
            }
            val: Inner;
        }
        test_nested_index() : int {
            c : Container;
            c.val.b = 2.0;
            idx : int = c.val.index();
            return idx;
        }
    )");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_nested_index");
    REQUIRE(fn);
    REQUIRE(fn() == 1);
}

TEST_CASE("template union Kind enum and index()", "[gen][union][kind][template]") {
    auto jit = gen_jit(R"(
        module test;
        template<typename T>
        union MaybeVal {
            some: T;
            none: byte;
        }
        test_optional_some() : int {
            o : MaybeVal<int>;
            o.some = 42;
            idx : int = o.index();
            return idx;
        }
        test_optional_none() : int {
            o : MaybeVal<int>;
            o.none = 0;
            idx : int = o.index();
            return idx;
        }
    )");
    REQUIRE(jit);
    auto fn_some = jit->lookup_symbol<int(*)()>("test_optional_some");
    auto fn_none = jit->lookup_symbol<int(*)()>("test_optional_none");
    REQUIRE(fn_some);
    REQUIRE(fn_none);
    REQUIRE(fn_some() == 0);
    REQUIRE(fn_none() == 1);
}

TEST_CASE("template struct with nested union — index and member access", "[gen][union][template][nested]") {
    auto jit = gen_jit(R"(
        module test;
        template<typename R, typename E>
        struct Expected {
            union Storage {
                result: R;
                error: E;
            }
            _storage : Storage;

            hasResult() : bool {
                return _storage.index() == Storage::Kind::result;
            }
            setResult(value : R&) {
                _storage.result = value;
            }
            getResult() : R {
                return _storage.result;
            }
        }

        test() : int {
            e : Expected<int, int>;
            e.setResult(42);
            if (e.hasResult()) {
                return e.getResult();
            }
            return 0;
        }
    )");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 42);
}

TEST_CASE("template struct with nested union — factory function", "[gen][union][template][nested][factory]") {
    auto jit = gen_jit(R"(
        module test;
        template<typename R, typename E>
        struct Expected {
            union Storage {
                result: R;
                error: E;
            }
            _storage : Storage;

            hasResult() : bool {
                return _storage.index() == Storage::Kind::result;
            }
            setResult(value : R&) {
                _storage.result = value;
            }
            getResult() : R {
                return _storage.result;
            }

            static expected(value : R&) : Expected<R, E> {
                e : Expected<R, E>;
                e.setResult(value);
                return e;
            }
        }

        test() : int {
            e : Expected<int, int> = Expected<int, int>::expected(42);
            if (e.hasResult()) {
                return e.getResult();
            }
            return 0;
        }
    )");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 42);
}



// Regression: assigning an owner-array alternative to a union must update the
// discriminant. Previously the owner-assignment codegen path returned early,
// before the discriminant update, leaving the union mistagged (and crashing on
// the next read).
TEST_CASE("Union owner-array alternative updates discriminant", "[gen][union][owner]") {
    auto jit = gen_jit(R"(
        module test;
        union Store {
            u8  : unsigned byte[]!;
            u16 : unsigned short[]!;
            u32 : char[]!;
        }
        // index() after assigning the third (owner-array) alternative must be 2.
        test_index() : int {
            s : Store;
            s.u32 = new char[3];
            return (int) s.index();
        }
        // The stored content must be readable through the active alternative.
        test_value() : int {
            s : Store;
            s.u32 = new char[3];
            s.u32[0] = 'A';
            return (int) s.u32[0];
        }
        // Switching alternatives must re-tag the discriminant.
        test_switch() : int {
            s : Store;
            s.u32 = new char[2];
            s.u8 = new unsigned byte[4];
            return (int) s.index();   // now UTF-8 alternative → index 0
        }
    )");
    REQUIRE(jit);
    CHECK(jit->lookup_symbol<int(*)()>("test_index")() == 2);
    CHECK(jit->lookup_symbol<int(*)()>("test_value")() == 65);
    CHECK(jit->lookup_symbol<int(*)()>("test_switch")() == 0);
}
