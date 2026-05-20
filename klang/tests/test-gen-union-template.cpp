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
        module test;
        template<typename T>
        union Optional {
            value: T;
            none: byte;
        }
        fun get_value() : int {
            o : Optional<int>;
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
        module test;
        template<typename T, typename U>
        union Either {
            left: T;
            right: U;
        }
        fun get_left() : int {
            e : Either<int, long>;
            e.left = 10;
            return e.left;
        }
        fun get_right() : long {
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
        module test;
        template<typename T>
        union Opt {
            value: T;
            none: byte;
        }
        fun test_int() : int {
            o : Opt<int>;
            o.value = 7;
            return o.value;
        }
        fun test_long() : long {
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
        module test;
        template<typename T>
        union Opt {
            value: T;
            none: byte;
        }
        fun test_disc() : int {
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
        module test;
        template<typename T>
        union Opt {
            value: T;
            none: byte;
        }
        fun read_opt(o: Opt<int>&) : int {
            return o.value;
        }
        fun test_ref() : int {
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
        module mylib;
        public:
        template<typename T>
        union Optional {
            value: T;
            none: byte;
        }
        fun get_opt_value() : int {
            o : Optional<int>;
            o.value = 77;
            return o.value;
        }
    )", R"(
        module main;
        import mylib;
        fun main() : int {
            return mylib::get_opt_value();
        }
    )");
    REQUIRE(result.exit_code == 77);
}

TEST_CASE("Template union definition imported and instantiated by consumer", "[gen][union][template][import]") {
    auto result = build_exec_with_lib(R"(
        module mylib;
        public:
        template<typename T>
        union Wrapper {
            val: T;
            empty: byte;
        }
        fun dummy() : int { return 0; }
    )", R"(
        module main;
        import mylib;
        fun main() : int {
            w : Wrapper<int>;
            w.val = 33;
            return w.val;
        }
    )");
    REQUIRE(result.exit_code == 33);
}
