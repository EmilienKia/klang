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

TEST_CASE("Template union basic instantiation", "[gen][union][template]") {
    auto jit = gen_jit(R"(
        module gen_union_template_01;
        template<typename T>
        union MaybeVal {
            value: T;
            none: byte;
        }
        get_value() : int {
            o : MaybeVal<int>;
            o.value = 42;
            return o.value;
        }
    )");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("get_value");
    REQUIRE(fn);
    REQUIRE(fn() == 42);
}

TEST_CASE("Template union multiple type params", "[gen][union][template]") {
    auto jit = gen_jit(R"(
        module gen_union_template_02;
        template<typename T, typename U>
        union Either {
            left: T;
            right: U;
        }
        get_left() : int {
            e : Either<int, long>;
            e.left = 10;
            return e.left;
        }
        get_right() : long {
            e : Either<int, long>;
            e.right = 99;
            return e.right;
        }
    )");
    REQUIRE(jit);
    auto fn_left = jit->lookup_symbol<int(*)()>("get_left");
    REQUIRE(fn_left);
    REQUIRE(fn_left() == 10);
    auto fn_right = jit->lookup_symbol<long(*)()>("get_right");
    REQUIRE(fn_right);
    REQUIRE(fn_right() == 99);
}

TEST_CASE("Template union distinct instantiations are different types", "[gen][union][template]") {
    auto jit = gen_jit(R"(
        module gen_union_template_03;
        template<typename T>
        union Opt {
            value: T;
            none: byte;
        }
        test_int() : int {
            o : Opt<int>;
            o.value = 7;
            return o.value;
        }
        test_long() : long {
            o : Opt<long>;
            o.value = 123;
            return o.value;
        }
    )");
    REQUIRE(jit);
    auto fn_int = jit->lookup_symbol<int(*)()>("test_int");
    REQUIRE(fn_int);
    REQUIRE(fn_int() == 7);
    auto fn_long = jit->lookup_symbol<long(*)()>("test_long");
    REQUIRE(fn_long);
    REQUIRE(fn_long() == 123);
}

TEST_CASE("Template union discriminant tracking", "[gen][union][template]") {
    auto jit = gen_jit(R"(
        module gen_union_template_04;
        template<typename T>
        union Opt {
            value: T;
            none: byte;
        }
        test_disc() : int {
            o : Opt<int>;
            o.value = 42;
            val : int = o.value;
            o.none = 0;
            return val;
        }
    )");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_disc");
    REQUIRE(fn);
    REQUIRE(fn() == 42);
}

TEST_CASE("Template union pass by reference", "[gen][union][template]") {
    auto jit = gen_jit(R"(
        module gen_union_template_05;
        template<typename T>
        union Opt {
            value: T;
            none: byte;
        }
        read_opt(o: Opt<int>&) : int {
            return o.value;
        }
        test_ref() : int {
            o : Opt<int>;
            o.value = 55;
            return read_opt(o);
        }
    )");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_ref");
    REQUIRE(fn);
    REQUIRE(fn() == 55);
}

// ============================================================
// Cross-module template union import
// ============================================================

TEST_CASE("Template union exported and instantiated cross-module", "[gen][union][template][import]") {
    auto result = build_exec_with_lib(R"(
        module gen_union_template_06;
        public:
        template<typename T>
        union MaybeVal {
            value: T;
            none: byte;
        }
        get_opt_value() : int {
            o : MaybeVal<int>;
            o.value = 77;
            return o.value;
        }
    )", R"(
        module gen_union_template_07;
        import gen_union_template_06;
        main() : int {
            return gen_union_template_06::get_opt_value();
        }
    )");
    REQUIRE(result.exit_code == 77);
}

TEST_CASE("Template union definition imported and instantiated by consumer", "[gen][union][template][import]") {
    auto result = build_exec_with_lib(R"(
        module gen_union_template_08;
        public:
        template<typename T>
        union Wrapper {
            val: T;
            empty: byte;
        }
        dummy() : int { return 0; }
    )", R"(
        module gen_union_template_09;
        import gen_union_template_08;
        main() : int {
            w : Wrapper<int>;
            w.val = 33;
            return w.val;
        }
    )");
    REQUIRE(result.exit_code == 33);
}

// ═════════════════════════════════════════════════════════════════════════════
// Nested union inside a template aggregate: per-instantiation layout
//
// Regression tests for the `DataStream round-trip long` bug. Sibling instantiations of a
// template holding a *nested* union used to share a single llvm::StructType (created for
// the template *definition* and reused by template_instantiator::clone_nested_union). Since
// declaration_generator::visit_union only sizes the first instantiation it finalises and
// skips the others, Expected<long,E> could end up with the 4-byte payload computed for
// Expected<int,E>, silently truncating the stored value.
//
// The declaration order below matters: the small payload is instantiated first, so a shared
// union type would be sized to 4 bytes and the 8-byte round-trip would fail.
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Nested union in a template: sibling instantiations keep their own layout",
          "[gen][union][template][nested-layout]") {
    auto jit = gen_jit(R"(
        module gen_union_template_10;
        template<typename R, typename E>
        struct Holder {
            private:
            union Storage {
                result: R;
                error: E;
            }
            _storage : Storage;
            public:
            Holder() {}
            setResult(value : R&) { _storage.result = value; }
            const getResult() : R { return _storage.result; }
        }

        // Instantiated FIRST: a shared union type would be sized from this one.
        small_first(v : int) : int {
            h : Holder<int, bool>;
            h.setResult(v);
            return h.getResult();
        }

        // Copying by value is what makes a too-small layout observable: the copy is a
        // memcpy of sizeof(Holder<long,bool>), which drops the high bytes.
        big_round_trip(v : long) : long {
            h : Holder<long, bool>;
            h.setResult(v);
            copy : Holder<long, bool> = h;
            return copy.getResult();
        }
    )");
    REQUIRE(jit);

    auto small = jit->lookup_symbol<int(*)(int)>("small_first");
    REQUIRE(small != nullptr);
    CHECK(small(7) == 7);

    auto big = jit->lookup_symbol<long(*)(long)>("big_round_trip");
    REQUIRE(big != nullptr);
    CHECK(big(1234567890123L) == 1234567890123L);
    CHECK(big(-1L) == -1L);
}

TEST_CASE("Nested union in a template: three payload sizes coexist",
          "[gen][union][template][nested-layout]") {
    auto jit = gen_jit(R"(
        module gen_union_template_11;
        template<typename R>
        struct Box {
            private:
            union Store {
                v: R;
                tag: byte;
            }
            _s : Store;
            public:
            Box() {}
            set(value : R&) { _s.v = value; }
            const get() : R { return _s.v; }
        }
        run_byte(v : byte) : byte { b : Box<byte>; b.set(v); c : Box<byte> = b; return c.get(); }
        run_int(v : int)   : int  { b : Box<int>;  b.set(v); c : Box<int>  = b; return c.get(); }
        run_long(v : long) : long { b : Box<long>; b.set(v); c : Box<long> = b; return c.get(); }
    )");
    REQUIRE(jit);

    auto rb = jit->lookup_symbol<signed char(*)(signed char)>("run_byte");
    auto ri = jit->lookup_symbol<int(*)(int)>("run_int");
    auto rl = jit->lookup_symbol<long(*)(long)>("run_long");
    REQUIRE(rb != nullptr);
    REQUIRE(ri != nullptr);
    REQUIRE(rl != nullptr);

    CHECK(rb(-42) == -42);
    CHECK(ri(-123456789) == -123456789);
    CHECK(rl(-1L) == -1L);
}

TEST_CASE("Nested union in a template survives a KDI export/import round-trip",
          "[gen][union][template][nested-layout][import][run]") {
    // The producing module instantiates the small payload first and the consumer reads back
    // both. Before the fix the KDI importer deduplicated LLVM type definitions by name and,
    // because every instantiation's union was the anonymous `%_union`, all of them collapsed
    // onto a single 4-byte layout in the consumer.
    auto result = build_exec_with_lib(R"(
        module gen_union_template_12;
        public:
        template<typename R, typename E>
        struct Holder {
            private:
            union Storage {
                result: R;
                error: E;
            }
            _storage : Storage;
            public:
            Holder() {}
            setResult(value : R&) { _storage.result = value; }
            const getResult() : R { return _storage.result; }
        }
        make_small(v : int) : int {
            h : Holder<int, bool>;
            h.setResult(v);
            return h.getResult();
        }
        make_big(v : long) : long {
            h : Holder<long, bool>;
            h.setResult(v);
            return h.getResult();
        }
    )", R"(
        module gen_union_template_13;
        import gen_union_template_12;
        main() : int {
            if (gen_union_template_12::make_small(7) != 7) return 1;
            if (gen_union_template_12::make_big(-1) != -1) return 2;
            if (gen_union_template_12::make_big(1234567890123) != 1234567890123) return 3;
            return 0;
        }
    )");
    REQUIRE(result.exit_code == 0);
}
