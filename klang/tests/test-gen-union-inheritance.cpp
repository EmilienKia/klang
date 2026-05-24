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
TEST_CASE("Union inheritance: basic derived union compiles", "[gen][union][inheritance]") {
    auto jit = gen_jit(R"SRC(
        module test;
        union Base { i: int; d: double; }
        union Derived : Base { b: bool; }
        make_base_alt() : int {
            v : Derived;
            v.i = 42;
            return v.i;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("make_base_alt");
    REQUIRE(fn);
    REQUIRE(fn() == 42);
}
TEST_CASE("Union inheritance: discriminant continuity", "[gen][union][inheritance]") {
    auto jit = gen_jit(R"SRC(
        module test;
        union Base { a: int; b: int; }
        union Derived : Base { c: int; }
        disc_a() : int { v : Derived; v.a = 1; return v.index(); }
        disc_b() : int { v : Derived; v.b = 2; return v.index(); }
        disc_c() : int { v : Derived; v.c = 3; return v.index(); }
    )SRC");
    REQUIRE(jit);
    auto fa = jit->lookup_symbol<int(*)()>("disc_a");
    auto fb = jit->lookup_symbol<int(*)()>("disc_b");
    auto fc = jit->lookup_symbol<int(*)()>("disc_c");
    REQUIRE(fa); REQUIRE(fb); REQUIRE(fc);
    REQUIRE(fa() == 0);
    REQUIRE(fb() == 1);
    REQUIRE(fc() == 2);
}
TEST_CASE("Union inheritance: derived own alternative accessible", "[gen][union][inheritance]") {
    auto jit = gen_jit(R"SRC(
        module test;
        union Base { i: int; }
        union Derived : Base { d: double; }
        get_d() : double { v : Derived; v.d = 3.14; return v.d; }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<double(*)()>("get_d");
    REQUIRE(fn);
    REQUIRE(fn() == Catch::Approx(3.14));
}
TEST_CASE("Union inheritance: LLVM size at least as large as parent", "[gen][union][inheritance]") {
    auto jit = gen_jit(R"SRC(
        module test;
        union Small { i: int; }
        union Big : Small { d: double; }
        roundtrip(x: double) : double { v : Big; v.d = x; return v.d; }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<double(*)(double)>("roundtrip");
    REQUIRE(fn);
    REQUIRE(fn(1234.5) == Catch::Approx(1234.5));
}
TEST_CASE("Union inheritance: parent to derived assignment (downcast)", "[gen][union][inheritance]") {
    auto jit = gen_jit(R"SRC(
        module test;
        union Base { i: int; d: double; }
        union Derived : Base { b: bool; }
        downcast_test() : int {
            p : Base; p.i = 99;
            der : Derived; der = p;
            return der.i;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("downcast_test");
    REQUIRE(fn);
    REQUIRE(fn() == 99);
}
TEST_CASE("Union inheritance: derived to parent upcast, valid alt", "[gen][union][inheritance]") {
    auto jit = gen_jit(R"SRC(
        module test;
        union Base { i: int; d: double; }
        union Derived : Base { b: bool; }
        upcast_valid() : int {
            der : Derived; der.i = 77;
            p : Base; p = der;
            return p.i;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("upcast_valid");
    REQUIRE(fn);
    REQUIRE(fn() == 77);
}
TEST_CASE("Union inheritance: derived to parent upcast, invalid alt traps", "[gen][union][inheritance]") {
    auto result = build_and_exec(R"SRC(
        module __uinh_trap__;
        union Base { i: int; }
        union Derived : Base { x: long; }
        main() : int {
            der : Derived; der.x = 999;
            p : Base; p = der;
            return p.i;
        }
    )SRC");
    REQUIRE(result.exit_code != 0);
}
TEST_CASE("Union inheritance: three-level chain", "[gen][union][inheritance]") {
    auto jit = gen_jit(R"SRC(
        module test;
        union GP { x: int; }
        union P : GP { y: int; }
        union C : P  { z: int; }
        dx() : int { v : C; v.x = 1; return v.index(); }
        dy() : int { v : C; v.y = 2; return v.index(); }
        dz() : int { v : C; v.z = 3; return v.index(); }
    )SRC");
    REQUIRE(jit);
    auto fx = jit->lookup_symbol<int(*)()>("dx");
    auto fy = jit->lookup_symbol<int(*)()>("dy");
    auto fz = jit->lookup_symbol<int(*)()>("dz");
    REQUIRE(fx); REQUIRE(fy); REQUIRE(fz);
    REQUIRE(fx() == 0);
    REQUIRE(fy() == 1);
    REQUIRE(fz() == 2);
}
TEST_CASE("Union inheritance: multiple bases rejected", "[gen][union][inheritance]") {
    REQUIRE(compile_should_fail(R"SRC(
        module test;
        union A { i: int; } union B { d: double; }
        union C : A, B { b: bool; }
    )SRC", nullptr));
}
TEST_CASE("Union inheritance: base must be union not struct", "[gen][union][inheritance]") {
    REQUIRE(compile_should_fail(R"SRC(
        module test;
        struct S { x: int; }
        union U : S { y: int; }
    )SRC", nullptr));
}
TEST_CASE("Union inheritance: circular inheritance rejected", "[gen][union][inheritance]") {
    REQUIRE(compile_should_fail(R"SRC(
        module test;
        union A : B { x: int; }
        union B : A { y: int; }
    )SRC", nullptr));
}
TEST_CASE("Union inheritance: unrelated union assignment rejected", "[gen][union][inheritance]") {
    REQUIRE(compile_should_fail(R"SRC(
        module test;
        union A { i: int; } union B { d: double; }
        bad() : void { a : A; b : B; b = a; }
    )SRC", nullptr));
}
TEST_CASE("Union inheritance: cross-module base union import", "[gen][union][inheritance][import]") {
    auto result = build_exec_with_lib(R"SRC(
        module base_mod;
        public:
        union BaseVal { i: int; d: double; }
        dummy() : int { return 0; }
    )SRC", R"SRC(
        module main;
        import base_mod;
        union ExtVal : BaseVal { b: bool; }
        main() : int {
            v : ExtVal; v.i = 55;
            if (v.i == 55) { return 0; }
            return 1;
        }
    )SRC");
    REQUIRE(result.exit_code == 0);
}
