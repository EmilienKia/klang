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


TEST_CASE( "float arithmetic", "[gen][float][arithmetic]" ) {

    auto jit = gen_jit(R"SRC(
        module __float__;
        add(a : float, b : float) : float {
            return a + b;
        }
        sub(a : float, b : float) : float {
            return a - b;
        }
        mul(a : float, b : float) : float {
            return a * b;
        }
        div(a : float, b : float) : float {
            return a / b;
        }
        mod(a : float, b : float) : float {
            return a % b;
        }
        plus(a : float) : float {
            return + a;
        }
        minus(a : float) : float {
            return - a;
        }
        prefix_incr_float(a : float) : float {
            return ++a;
        }
        prefix_incr_float_result_is_new(a : float) : float {
            b : float = ++a;
            return b;
        }
        prefix_decr_float(a : float) : float {
            return --a;
        }
        prefix_decr_float_result_is_new(a : float) : float {
            b : float = --a;
            return b;
        }
        postfix_incr_float(a : float) : float {
            return a++;
        }
        postfix_incr_float_mutates(a : float) : float {
            a++;
            return a;
        }
        postfix_incr_float_result_is_old(a : float) : float {
            b : float = a++;
            return b;
        }
        postfix_decr_float(a : float) : float {
            return a--;
        }
        postfix_decr_float_mutates(a : float) : float {
            a--;
            return a;
        }
        postfix_decr_float_result_is_old(a : float) : float {
            b : float = a--;
            return b;
        }
        eq(a:float, b:float) : bool { return a == b; }
        ne(a:float, b:float) : bool { return a != b; }
        lt(a:float, b:float) : bool { return a < b; }
        le(a:float, b:float) : bool { return a <= b; }
        gt(a:float, b:float) : bool { return a > b; }
        ge(a:float, b:float) : bool { return a >= b; }
        )SRC");
    REQUIRE( jit );

    typedef float type_t;

    SECTION( "float addition" ) {
        auto add = jit->lookup_symbol<type_t(*)(type_t, type_t)>("add");
        REQUIRE(add != nullptr);
        REQUIRE( add(0, 0) == 0 );
        REQUIRE( add(2, 3) == 5 );
        REQUIRE( add(-2, -3) == -5 );
        REQUIRE( add(42, -42) == 0 );
    }

    SECTION( "float substraction" ) {
        auto sub = jit->lookup_symbol<type_t(*)(type_t, type_t)>("sub");
        REQUIRE(sub != nullptr);
        REQUIRE( sub(0, 0) == 0 );
        REQUIRE( sub(3, 2) == 1 );
        REQUIRE( sub(2, 3) == -1 );
        REQUIRE( sub(-3, -2) == -1 );
        REQUIRE( sub(-2, -3) == 1 );
        REQUIRE( sub(42, -42) == 84 );
        REQUIRE( sub(-42, 42) == -84 );
        REQUIRE( sub(-42, -42) == 0 );
        REQUIRE( sub(42, 42) == 0 );
    }

    SECTION( "float multiplication" ) {
        auto mul = jit->lookup_symbol<type_t(*)(type_t, type_t)>("mul");
        REQUIRE(mul != nullptr);
        REQUIRE( mul(0, 0) == 0 );
        REQUIRE( mul(2, 3) == 6 );
        REQUIRE( mul(-2, -3) == 6 );
        REQUIRE( mul(2, -3) == -6 );
        REQUIRE( mul(-2, 3) == -6 );
    }

    SECTION( "float division" ) {
        auto div = jit->lookup_symbol<type_t(*)(type_t, type_t)>("div");
        REQUIRE(div != nullptr);
        REQUIRE( div(6, 3) == 2 );
        REQUIRE( div(-6, -2) == 3 );
        REQUIRE( div(6, -3) == -2 );
        REQUIRE( div(-6, 2) == -3 );
    }

    SECTION( "float modulo" ) {
        auto mod = jit->lookup_symbol<type_t(*)(type_t, type_t)>("mod");
        REQUIRE(mod != nullptr);
        REQUIRE( mod(6, 2) == 0 );
        REQUIRE( mod(7, 3) == 1 );
    }

    SECTION( "float plus" ) {
        auto plus = jit->lookup_symbol<type_t(*)(type_t)>("plus");
        REQUIRE(plus != nullptr);
        REQUIRE( plus(42) == 42 );
    }

    SECTION( "float minus" ) {
        auto minus = jit->lookup_symbol<type_t(*)(type_t)>("minus");
        REQUIRE(minus != nullptr);
        REQUIRE( minus(42) == -42 );
    }

    SECTION("prefix increment float") {
        auto f = jit->lookup_symbol<float(*)(float)>("prefix_incr_float");
        REQUIRE(f != nullptr);
        REQUIRE(f(0.0f) == Catch::Approx(1.0f));
        REQUIRE(f(1.5f) == Catch::Approx(2.5f));
        REQUIRE(f(-1.0f) == Catch::Approx(0.0f));
        REQUIRE(f(-0.5f) == Catch::Approx(0.5f));
    }

    SECTION("prefix float increment returns new value") {
        auto f = jit->lookup_symbol<float(*)(float)>("prefix_incr_float_result_is_new");
        REQUIRE(f != nullptr);
        REQUIRE(f(1.5f) == Catch::Approx(2.5f));
        REQUIRE(f(-0.5f) == Catch::Approx(0.5f));
    }

    SECTION("prefix decrement float") {
        auto f = jit->lookup_symbol<float(*)(float)>("prefix_decr_float");
        REQUIRE(f != nullptr);
        REQUIRE(f(1.0f) == Catch::Approx(0.0f));
        REQUIRE(f(2.5f) == Catch::Approx(1.5f));
        REQUIRE(f(0.0f) == Catch::Approx(-1.0f));
        REQUIRE(f(0.5f) == Catch::Approx(-0.5f));
    }

    SECTION("prefix float decrement returns new value") {
        auto f = jit->lookup_symbol<float(*)(float)>("prefix_decr_float_result_is_new");
        REQUIRE(f != nullptr);
        REQUIRE(f(2.5f) == Catch::Approx(1.5f));
        REQUIRE(f(0.5f) == Catch::Approx(-0.5f));
    }

    SECTION("postfix increment float returns old value") {
        auto f = jit->lookup_symbol<float(*)(float)>("postfix_incr_float");
        REQUIRE(f != nullptr);
        REQUIRE(f(0.0f) == Catch::Approx(0.0f));    // old value returned
        REQUIRE(f(1.5f) == Catch::Approx(1.5f));    // old value returned
        REQUIRE(f(-1.0f) == Catch::Approx(-1.0f));
    }

    SECTION("postfix float increment mutates the variable") {
        auto f = jit->lookup_symbol<float(*)(float)>("postfix_incr_float_mutates");
        REQUIRE(f != nullptr);
        REQUIRE(f(1.5f) == Catch::Approx(2.5f));
        REQUIRE(f(-0.5f) == Catch::Approx(0.5f));
    }

    SECTION("postfix float increment result is old value") {
        auto f = jit->lookup_symbol<float(*)(float)>("postfix_incr_float_result_is_old");
        REQUIRE(f != nullptr);
        REQUIRE(f(1.5f) == Catch::Approx(1.5f));
        REQUIRE(f(-0.5f) == Catch::Approx(-0.5f));
    }

    SECTION("postfix decrement float returns old value") {
        auto f = jit->lookup_symbol<float(*)(float)>("postfix_decr_float");
        REQUIRE(f != nullptr);
        REQUIRE(f(1.0f) == Catch::Approx(1.0f));    // old value returned
        REQUIRE(f(2.5f) == Catch::Approx(2.5f));    // old value returned
        REQUIRE(f(0.0f) == Catch::Approx(0.0f));
    }

    SECTION("postfix float decrement mutates the variable") {
        auto f = jit->lookup_symbol<float(*)(float)>("postfix_decr_float_mutates");
        REQUIRE(f != nullptr);
        REQUIRE(f(2.5f) == Catch::Approx(1.5f));
        REQUIRE(f(0.5f) == Catch::Approx(-0.5f));
    }

    SECTION("postfix float decrement result is old value") {
        auto f = jit->lookup_symbol<float(*)(float)>("postfix_decr_float_result_is_old");
        REQUIRE(f != nullptr);
        REQUIRE(f(2.5f) == Catch::Approx(2.5f));
        REQUIRE(f(0.5f) == Catch::Approx(0.5f));
    }

    SECTION("float equal") {
        auto eq = jit->lookup_symbol<bool(*)(type_t, type_t)>("eq");
        REQUIRE(eq != nullptr);
        REQUIRE( eq(42, 42) == true );
        REQUIRE( eq(42, 24) == false );
    }

    SECTION("float not equal") {
        auto ne = jit->lookup_symbol<bool(*)(type_t, type_t)>("ne");
        REQUIRE(ne != nullptr);
        REQUIRE( ne(42, 42) == false );
        REQUIRE( ne(42, 24) == true );
    }

    SECTION("float less than") {
        auto lt = jit->lookup_symbol<bool(*)(type_t, type_t)>("lt");
        REQUIRE(lt != nullptr);
        REQUIRE( lt(42, 42) == false );
        REQUIRE( lt(42, 24) == false );
        REQUIRE( lt(24, 42) == true );
    }

    SECTION("float less or equal") {
        auto le = jit->lookup_symbol<bool(*)(type_t, type_t)>("le");
        REQUIRE(le != nullptr);
        REQUIRE( le(42, 42) == true );
        REQUIRE( le(42, 24) == false );
        REQUIRE( le(24, 42) == true );
    }

    SECTION("float greater than") {
        auto gt = jit->lookup_symbol<bool(*)(type_t, type_t)>("gt");
        REQUIRE(gt != nullptr);
        REQUIRE( gt(42, 42) == false );
        REQUIRE( gt(42, 24) == true );
        REQUIRE( gt(24, 42) == false );
    }

    SECTION("float greater or equal") {
        auto ge = jit->lookup_symbol<bool(*)(type_t, type_t)>("ge");
        REQUIRE(ge != nullptr);
        REQUIRE( ge(42, 42) == true );
        REQUIRE( ge(42, 24) == true );
        REQUIRE( ge(24, 42) == false );
    }
}

TEST_CASE( "double arithmetic", "[gen][double][arithmetic]" ) {

    auto jit = gen_jit(R"SRC(
        module __double__;
        add(a : double, b : double) : double {
            return a + b;
        }
        sub(a : double, b : double) : double {
            return a - b;
        }
        mul(a : double, b : double) : double {
            return a * b;
        }
        div(a : double, b : double) : double {
            return a / b;
        }
        mod(a : double, b : double) : double {
            return a % b;
        }
        plus(a : double) : double {
            return + a;
        }
        minus(a : double) : double {
            return - a;
        }
        prefix_incr_double(a : double) : double {
            return ++a;
        }
        prefix_incr_double_result_is_new(a : double) : double {
            b : double = ++a;
            return b;
        }
        prefix_decr_double(a : double) : double {
            return --a;
        }
        prefix_decr_double_result_is_new(a : double) : double {
            b : double = --a;
            return b;
        }
        postfix_incr_double(a : double) : double {
            return a++;
        }
        postfix_incr_double_mutates(a : double) : double {
            a++;
            return a;
        }
        postfix_incr_double_result_is_old(a : double) : double {
            b : double = a++;
            return b;
        }
        postfix_decr_double(a : double) : double {
            return a--;
        }
        postfix_decr_double_mutates(a : double) : double {
            a--;
            return a;
        }
        postfix_decr_double_result_is_old(a : double) : double {
            b : double = a--;
            return b;
        }
        eq(a:double, b:double) : bool { return a == b; }
        ne(a:double, b:double) : bool { return a != b; }
        lt(a:double, b:double) : bool { return a < b; }
        le(a:double, b:double) : bool { return a <= b; }
        gt(a:double, b:double) : bool { return a > b; }
        ge(a:double, b:double) : bool { return a >= b; }
        )SRC");
    REQUIRE( jit );

    typedef double type_t;

    SECTION( "double addition" ) {
        auto add = jit->lookup_symbol<type_t(*)(type_t, type_t)>("add");
        REQUIRE(add != nullptr);
        REQUIRE( add(0, 0) == 0 );
        REQUIRE( add(2, 3) == 5 );
        REQUIRE( add(-2, -3) == -5 );
        REQUIRE( add(42, -42) == 0 );
    }

    SECTION( "double substraction" ) {
        auto sub = jit->lookup_symbol<type_t(*)(type_t, type_t)>("sub");
        REQUIRE(sub != nullptr);
        REQUIRE( sub(0, 0) == 0 );
        REQUIRE( sub(3, 2) == 1 );
        REQUIRE( sub(2, 3) == -1 );
        REQUIRE( sub(-3, -2) == -1 );
        REQUIRE( sub(-2, -3) == 1 );
        REQUIRE( sub(42, -42) == 84 );
        REQUIRE( sub(-42, 42) == -84 );
        REQUIRE( sub(-42, -42) == 0 );
        REQUIRE( sub(42, 42) == 0 );
    }

    SECTION( "double multiplication" ) {
        auto mul = jit->lookup_symbol<type_t(*)(type_t, type_t)>("mul");
        REQUIRE(mul != nullptr);
        REQUIRE( mul(0, 0) == 0 );
        REQUIRE( mul(2, 3) == 6 );
        REQUIRE( mul(-2, -3) == 6 );
        REQUIRE( mul(2, -3) == -6 );
        REQUIRE( mul(-2, 3) == -6 );
    }

    SECTION( "double division" ) {
        auto div = jit->lookup_symbol<type_t(*)(type_t, type_t)>("div");
        REQUIRE(div != nullptr);
        REQUIRE( div(6, 3) == 2 );
        REQUIRE( div(-6, -2) == 3 );
        REQUIRE( div(6, -3) == -2 );
        REQUIRE( div(-6, 2) == -3 );
    }

    SECTION( "double modulo" ) {
        auto mod = jit->lookup_symbol<type_t(*)(type_t, type_t)>("mod");
        REQUIRE(mod != nullptr);
        REQUIRE( mod(6, 2) == 0 );
        REQUIRE( mod(7, 3) == 1 );
    }

    SECTION( "double plus" ) {
        auto plus = jit->lookup_symbol<type_t(*)(type_t)>("plus");
        REQUIRE(plus != nullptr);
        REQUIRE( plus(42) == 42 );
    }

    SECTION( "double minus" ) {
        auto minus = jit->lookup_symbol<type_t(*)(type_t)>("minus");
        REQUIRE(minus != nullptr);
        REQUIRE( minus(42) == -42 );
    }

    SECTION("prefix increment double") {
        auto f = jit->lookup_symbol<double(*)(double)>("prefix_incr_double");
        REQUIRE(f != nullptr);
        REQUIRE(f(0.0) == Catch::Approx(1.0));
        REQUIRE(f(1.5) == Catch::Approx(2.5));
        REQUIRE(f(-1.0) == Catch::Approx(0.0));
        REQUIRE(f(-0.5) == Catch::Approx(0.5));
    }

    SECTION("prefix double increment returns new value") {
        auto f = jit->lookup_symbol<double(*)(double)>("prefix_incr_double_result_is_new");
        REQUIRE(f != nullptr);
        REQUIRE(f(1.5) == Catch::Approx(2.5));
        REQUIRE(f(-0.5) == Catch::Approx(0.5));
    }

    SECTION("prefix decrement double") {
        auto f = jit->lookup_symbol<double(*)(double)>("prefix_decr_double");
        REQUIRE(f != nullptr);
        REQUIRE(f(1.0) == Catch::Approx(0.0));
        REQUIRE(f(2.5) == Catch::Approx(1.5));
        REQUIRE(f(0.0) == Catch::Approx(-1.0));
        REQUIRE(f(0.5) == Catch::Approx(-0.5));
    }

    SECTION("prefix double decrement returns new value") {
        auto f = jit->lookup_symbol<double(*)(double)>("prefix_decr_double_result_is_new");
        REQUIRE(f != nullptr);
        REQUIRE(f(2.5) == Catch::Approx(1.5));
        REQUIRE(f(0.5) == Catch::Approx(-0.5));
    }

    SECTION("postfix increment double returns old value") {
        auto f = jit->lookup_symbol<double(*)(double)>("postfix_incr_double");
        REQUIRE(f != nullptr);
        REQUIRE(f(0.0) == Catch::Approx(0.0));
        REQUIRE(f(1.5) == Catch::Approx(1.5));
        REQUIRE(f(-1.0) == Catch::Approx(-1.0));
    }

    SECTION("postfix double increment mutates the variable") {
        auto f = jit->lookup_symbol<double(*)(double)>("postfix_incr_double_mutates");
        REQUIRE(f != nullptr);
        REQUIRE(f(1.5) == Catch::Approx(2.5));
        REQUIRE(f(-0.5) == Catch::Approx(0.5));
    }

    SECTION("postfix double increment result is old value") {
        auto f = jit->lookup_symbol<double(*)(double)>("postfix_incr_double_result_is_old");
        REQUIRE(f != nullptr);
        REQUIRE(f(1.5) == Catch::Approx(1.5));
        REQUIRE(f(-0.5) == Catch::Approx(-0.5));
    }

    SECTION("postfix decrement double returns old value") {
        auto f = jit->lookup_symbol<double(*)(double)>("postfix_decr_double");
        REQUIRE(f != nullptr);
        REQUIRE(f(1.0) == Catch::Approx(1.0));
        REQUIRE(f(2.5) == Catch::Approx(2.5));
        REQUIRE(f(0.0) == Catch::Approx(0.0));
    }

    SECTION("postfix double decrement mutates the variable") {
        auto f = jit->lookup_symbol<double(*)(double)>("postfix_decr_double_mutates");
        REQUIRE(f != nullptr);
        REQUIRE(f(2.5) == Catch::Approx(1.5));
        REQUIRE(f(0.5) == Catch::Approx(-0.5));
    }

    SECTION("postfix double decrement result is old value") {
        auto f = jit->lookup_symbol<double(*)(double)>("postfix_decr_double_result_is_old");
        REQUIRE(f != nullptr);
        REQUIRE(f(2.5) == Catch::Approx(2.5));
        REQUIRE(f(0.5) == Catch::Approx(0.5));
    }

    SECTION("double equal") {
        auto eq = jit->lookup_symbol<bool(*)(type_t, type_t)>("eq");
        REQUIRE(eq != nullptr);
        REQUIRE( eq(42, 42) == true );
        REQUIRE( eq(42, 24) == false );
    }

    SECTION("double not equal") {
        auto ne = jit->lookup_symbol<bool(*)(type_t, type_t)>("ne");
        REQUIRE(ne != nullptr);
        REQUIRE( ne(42, 42) == false );
        REQUIRE( ne(42, 24) == true );
    }

    SECTION("double less than") {
        auto lt = jit->lookup_symbol<bool(*)(type_t, type_t)>("lt");
        REQUIRE(lt != nullptr);
        REQUIRE( lt(42, 42) == false );
        REQUIRE( lt(42, 24) == false );
        REQUIRE( lt(24, 42) == true );
    }

    SECTION("double less or equal") {
        auto le = jit->lookup_symbol<bool(*)(type_t, type_t)>("le");
        REQUIRE(le != nullptr);
        REQUIRE( le(42, 42) == true );
        REQUIRE( le(42, 24) == false );
        REQUIRE( le(24, 42) == true );
    }

    SECTION("double greater than") {
        auto gt = jit->lookup_symbol<bool(*)(type_t, type_t)>("gt");
        REQUIRE(gt != nullptr);
        REQUIRE( gt(42, 42) == false );
        REQUIRE( gt(42, 24) == true );
        REQUIRE( gt(24, 42) == false );
    }

    SECTION("double greater or equal") {
        auto ge = jit->lookup_symbol<bool(*)(type_t, type_t)>("ge");
        REQUIRE(ge != nullptr);
        REQUIRE( ge(42, 42) == true );
        REQUIRE( ge(42, 24) == true );
        REQUIRE( ge(24, 42) == false );
    }
}

TEST_CASE("Boolean values and casting", "[gen][bool]") {
    auto jit = gen_jit(R"SRC(
        module __bool__;
        ret_true() : bool {
            return true;
        }
        ret_false() : bool {
            return false;
        }
        cast_char_to_bool(c : char) : bool {
            return (bool)c;
        }
        cast_byte_to_bool(b : byte) : bool {
            return (bool)b;
        }
        cast_int32_to_bool(i : int) : bool {
            return (bool)i;
        }
        cast_uint64_to_bool(u : unsigned long) : bool {
            return (bool)u;
        }
        cast_bool_to_char(b : bool) : char {
            return (char)b;
        }
        cast_bool_to_byte(b : bool) : byte {
            return (byte)b;
        }
        cast_bool_to_int32(b : bool) : int {
            return (int)b;
        }
        cast_bool_to_uint64(b : bool) : unsigned long {
            return (unsigned long)b;
        }
        )SRC");
    REQUIRE( jit );

    typedef bool type_t;

    SECTION( "boolean true basic value" ) {
        auto ret_true = jit->lookup_symbol<bool(*)()>("ret_true");
        REQUIRE( ret_true != nullptr );
        REQUIRE( ret_true() == true );
    }

    SECTION( "boolean false basic value" ) {
        auto ret_false = jit->lookup_symbol<bool(*)()>("ret_false");
        REQUIRE( ret_false != nullptr );
        REQUIRE( ret_false() == false );
    }

    SECTION( "cast char to boolean" ) {
        auto cast_char_to_bool = jit->lookup_symbol<bool(*)(char)>("cast_char_to_bool");
        REQUIRE( cast_char_to_bool != nullptr );
        REQUIRE( cast_char_to_bool( 42 ) == true );
        REQUIRE( cast_char_to_bool( -42 ) == true );
        REQUIRE( cast_char_to_bool( 0 ) == false );
    }

    SECTION( "cast byte to boolean" ) {
        auto cast_byte_to_bool = jit->lookup_symbol<bool(*)(char)>("cast_byte_to_bool");
        REQUIRE( cast_byte_to_bool != nullptr );
        REQUIRE( cast_byte_to_bool( 42 ) == true );
        REQUIRE( cast_byte_to_bool( 0 ) == false );
    }

    SECTION( "cast int32 to boolean" ) {
        auto cast_int32_to_bool = jit->lookup_symbol<bool(*)(int)>("cast_int32_to_bool");
        REQUIRE( cast_int32_to_bool != nullptr );
        REQUIRE( cast_int32_to_bool( 42 ) == true );
        REQUIRE( cast_int32_to_bool( -42 ) == true );
        REQUIRE( cast_int32_to_bool( 0 ) == false );
    }

    SECTION( "cast uint64 to boolean" ) {
        auto cast_uint64_to_bool = jit->lookup_symbol<bool(*)(uint64_t)>("cast_uint64_to_bool");
        REQUIRE( cast_uint64_to_bool != nullptr );
        REQUIRE( cast_uint64_to_bool( 42 ) == true );
        REQUIRE( cast_uint64_to_bool( 0 ) == false );
    }

    SECTION( "cast boolean to char" ) {
        auto cast_bool_to_char = jit->lookup_symbol<char(*)(bool)>("cast_bool_to_char");
        REQUIRE( cast_bool_to_char != nullptr );
        REQUIRE( cast_bool_to_char( false ) == 0 );
        REQUIRE( cast_bool_to_char( true ) != 0 );
    }

    SECTION( "cast boolean to byte" ) {
        auto cast_bool_to_byte = jit->lookup_symbol<unsigned char(*)(bool)>("cast_bool_to_byte");
        REQUIRE( cast_bool_to_byte != nullptr );
        REQUIRE( cast_bool_to_byte( false ) == 0 );
        REQUIRE( cast_bool_to_byte( true ) != 0 );
    }

    SECTION( "cast boolean to int32" ) {
        auto cast_bool_to_int32 = jit->lookup_symbol<int(*)(bool)>("cast_bool_to_int32");
        REQUIRE( cast_bool_to_int32 != nullptr );
        REQUIRE( cast_bool_to_int32( false ) == 0 );
        REQUIRE( cast_bool_to_int32( true ) != 0 );
    }

    SECTION( "cast boolean to uint64" ) {
        auto cast_bool_to_uint64 = jit->lookup_symbol<uint64_t(*)(bool)>("cast_bool_to_uint64");
        REQUIRE( cast_bool_to_uint64 != nullptr );
        REQUIRE( cast_bool_to_uint64( false ) == 0 );
        REQUIRE( cast_bool_to_uint64( true ) != 0 );
    }
}

TEST_CASE("Boolean arithmetic", "[gen][bool][arithmetic]") {
    auto jit = gen_jit(R"SRC(
        module __bool__;
        not(b : bool) : bool {
            return !b;
        }
        and(a : bool, b: bool) : bool {
            return a && b;
        }
        and_int(a : bool, b: int) : bool {
            return a && b;
        }
        or(a : bool, b: bool) : bool {
            return a || b;
        }
        or_int(a : bool, b: int) : bool {
            return a || b;
        }
        eq(a:bool, b:bool) : bool { return a == b; }
        ne(a:bool, b:bool) : bool { return a != b; }
        lt(a:bool, b:bool) : bool { return a < b; }
        le(a:bool, b:bool) : bool { return a <= b; }
        gt(a:bool, b:bool) : bool { return a > b; }
        ge(a:bool, b:bool) : bool { return a >= b; }
        )SRC");
    REQUIRE( jit );

    typedef bool type_t;

    SECTION( "boolean not" ) {
        auto _not = jit->lookup_symbol<bool(*)(bool)>("not");
        REQUIRE( _not != nullptr );
        REQUIRE( _not(false) == true );
        REQUIRE( _not(true) == false );
    }

    SECTION( "boolean and" ) {
        auto _and = jit->lookup_symbol<bool(*)(bool, bool)>("and");
        REQUIRE( _and != nullptr );
        REQUIRE( _and(false, false) == false );
        REQUIRE( _and(false, true) == false );
        REQUIRE( _and(true, false) == false );
        REQUIRE( _and(true, true) == true );
    }

    SECTION( "boolean and with cast" ) {
        auto _and = jit->lookup_symbol<bool(*)(bool, int)>("and_int");
        REQUIRE( _and != nullptr );
        REQUIRE( _and(false, 0) == false );
        REQUIRE( _and(false, 25) == false );
        REQUIRE( _and(true, 0) == false );
        REQUIRE( _and(true, 42) == true );
    }

    SECTION( "boolean or" ) {
        auto _or = jit->lookup_symbol<bool(*)(bool, bool)>("or");
        REQUIRE( _or != nullptr );
        REQUIRE( _or(false, false) == false );
        REQUIRE( _or(false, true) == true );
        REQUIRE( _or(true, false) == true );
        REQUIRE( _or(true, true) == true );
    }

    SECTION( "boolean or with cast" ) {
        auto _or = jit->lookup_symbol<bool(*)(bool, int)>("or_int");
        REQUIRE( _or != nullptr );
        REQUIRE( _or(false, 0) == false );
        REQUIRE( _or(false, 25) == true );
        REQUIRE( _or(true, 0) == true );
        REQUIRE( _or(true, 42) == true );
    }

    SECTION("bool equal") {
        auto eq = jit->lookup_symbol<bool(*)(type_t, type_t)>("eq");
        REQUIRE(eq != nullptr);
        REQUIRE( eq(true, true) == true );
        REQUIRE( eq(false, false) == true );
        REQUIRE( eq(true, false) == false );
        REQUIRE( eq(false, true) == false );
    }

    SECTION("bool not equal") {
        auto ne = jit->lookup_symbol<bool(*)(type_t, type_t)>("ne");
        REQUIRE(ne != nullptr);
        REQUIRE( ne(true, true) == false );
        REQUIRE( ne(false, false) == false );
        REQUIRE( ne(true, false) == true );
        REQUIRE( ne(false, true) == true );
    }

    SECTION("bool less than") {
        auto lt = jit->lookup_symbol<bool(*)(type_t, type_t)>("lt");
        REQUIRE(lt != nullptr);
        REQUIRE( lt(true, true) == false );
        REQUIRE( lt(false, false) == false );
        REQUIRE( lt(true, false) == false );
        REQUIRE( lt(false, true) == true );
    }

    SECTION("bool less or equal") {
        auto le = jit->lookup_symbol<bool(*)(type_t, type_t)>("le");
        REQUIRE(le != nullptr);
        REQUIRE( le(true, true) == true );
        REQUIRE( le(false, false) == true );
        REQUIRE( le(true, false) == false );
        REQUIRE( le(false, true) == true );
    }

    SECTION("bool greater than") {
        auto gt = jit->lookup_symbol<bool(*)(type_t, type_t)>("gt");
        REQUIRE(gt != nullptr);
        REQUIRE( gt(true, true) == false );
        REQUIRE( gt(false, false) == false );
        REQUIRE( gt(true, false) == true );
        REQUIRE( gt(false, true) == false );
    }

    SECTION("bool greater or equal") {
        auto ge = jit->lookup_symbol<bool(*)(type_t, type_t)>("ge");
        REQUIRE(ge != nullptr);
        REQUIRE( ge(true, true) == true );
        REQUIRE( ge(false, false) == true );
        REQUIRE( ge(true, false) == true );
        REQUIRE( ge(false, true) == false );
    }
}

// ============================================================================
// INTERACTION WITH LOOPS AND COMBINED USES
// ============================================================================

TEST_CASE("Increment/decrement in loops", "[gen][incr-decr][loop]") {
    auto jit = gen_jit(R"SRC(
        module __incr_loops__;

        // Sum 0..n-1 using postfix ++
        sum_postfix_incr(n : int) : int {
            r : int = 0;
            i : int = 0;
            while (i < n) {
                r = r + i;
                i++;
            }
            return r;
        }

        // Sum 0..n-1 using prefix ++
        sum_prefix_incr(n : int) : int {
            r : int = 0;
            i : int = 0;
            while (i < n) {
                r = r + i;
                ++i;
            }
            return r;
        }

        // Count down from n using postfix --
        countdown_postfix_decr(n : int) : int {
            count : int = 0;
            i : int = n;
            while (i > 0) {
                count++;
                i--;
            }
            return count;
        }

        // Count down from n using prefix --
        countdown_prefix_decr(n : int) : int {
            count : int = 0;
            i : int = n;
            while (i > 0) {
                ++count;
                --i;
            }
            return count;
        }

        // for-loop style with prefix ++
        for_prefix_incr(n : int) : int {
            r : int = 0;
            for (i : int = 0; i < n; ++i) {
                r += i;
            }
            return r;
        }

        // for-loop style with postfix ++
        for_postfix_incr(n : int) : int {
            r : int = 0;
            for (i : int = 0; i < n; i++) {
                r += i;
            }
            return r;
        }
    )SRC");
    REQUIRE(jit);

    SECTION("sum with postfix increment") {
        auto f = jit->lookup_symbol<int(*)(int)>("sum_postfix_incr");
        REQUIRE(f != nullptr);
        REQUIRE(f(0) == 0);
        REQUIRE(f(1) == 0);
        REQUIRE(f(5) == 10);  // 0+1+2+3+4
        REQUIRE(f(10) == 45); // 0+1+...+9
    }

    SECTION("sum with prefix increment") {
        auto f = jit->lookup_symbol<int(*)(int)>("sum_prefix_incr");
        REQUIRE(f != nullptr);
        REQUIRE(f(0) == 0);
        REQUIRE(f(1) == 0);
        REQUIRE(f(5) == 10);
        REQUIRE(f(10) == 45);
    }

    SECTION("countdown with postfix decrement") {
        auto f = jit->lookup_symbol<int(*)(int)>("countdown_postfix_decr");
        REQUIRE(f != nullptr);
        REQUIRE(f(0) == 0);
        REQUIRE(f(1) == 1);
        REQUIRE(f(5) == 5);
        REQUIRE(f(10) == 10);
    }

    SECTION("countdown with prefix decrement") {
        auto f = jit->lookup_symbol<int(*)(int)>("countdown_prefix_decr");
        REQUIRE(f != nullptr);
        REQUIRE(f(0) == 0);
        REQUIRE(f(1) == 1);
        REQUIRE(f(5) == 5);
        REQUIRE(f(10) == 10);
    }

    SECTION("for-loop with prefix increment") {
        auto f = jit->lookup_symbol<int(*)(int)>("for_prefix_incr");
        REQUIRE(f != nullptr);
        REQUIRE(f(0) == 0);
        REQUIRE(f(1) == 0);
        REQUIRE(f(5) == 10);
        REQUIRE(f(10) == 45);
    }

    SECTION("for-loop with postfix increment") {
        auto f = jit->lookup_symbol<int(*)(int)>("for_postfix_incr");
        REQUIRE(f != nullptr);
        REQUIRE(f(0) == 0);
        REQUIRE(f(1) == 0);
        REQUIRE(f(5) == 10);
        REQUIRE(f(10) == 45);
    }
}

// ============================================================================
// PREFIX vs POSTFIX DISTINCTION
// ============================================================================

TEST_CASE("Prefix and postfix distinction", "[gen][incr-decr][prefix-vs-postfix]") {
    auto jit = gen_jit(R"SRC(
        module __prefix_vs_postfix__;

        // Compute a + b where a is prefix-incremented before use
        // Expected: (a+1) + b
        add_prefix_incr(a : int, b : int) : int {
            return (++a) + b;
        }

        // Compute a + b where a is postfix-incremented before use
        // Expected: a + b (old value of a used, then a gets incremented)
        add_postfix_incr(a : int, b : int) : int {
            return (a++) + b;
        }

        // Compute a - b where a is prefix-decremented before use
        // Expected: (a-1) - b
        sub_prefix_decr(a : int, b : int) : int {
            return (--a) - b;
        }

        // Compute a - b where a is postfix-decremented before use
        // Expected: a - b (old value of a used)
        sub_postfix_decr(a : int, b : int) : int {
            return (a--) - b;
        }
    )SRC");
    REQUIRE(jit);

    SECTION("prefix increment uses new value in expression") {
        auto f = jit->lookup_symbol<int(*)(int, int)>("add_prefix_incr");
        REQUIRE(f != nullptr);
        REQUIRE(f(5, 3) == 9);   // (5+1) + 3 = 9
        REQUIRE(f(0, 10) == 11); // (0+1) + 10 = 11
        REQUIRE(f(-1, 5) == 5);  // (-1+1) + 5 = 5
    }

    SECTION("postfix increment uses old value in expression") {
        auto f = jit->lookup_symbol<int(*)(int, int)>("add_postfix_incr");
        REQUIRE(f != nullptr);
        REQUIRE(f(5, 3) == 8);   // 5 + 3 = 8 (old a=5 used)
        REQUIRE(f(0, 10) == 10); // 0 + 10 = 10
        REQUIRE(f(-1, 5) == 4);  // -1 + 5 = 4
    }

    SECTION("prefix decrement uses new value in expression") {
        auto f = jit->lookup_symbol<int(*)(int, int)>("sub_prefix_decr");
        REQUIRE(f != nullptr);
        REQUIRE(f(5, 3) == 1);   // (5-1) - 3 = 1
        REQUIRE(f(10, 4) == 5);  // (10-1) - 4 = 5
        REQUIRE(f(1, 5) == -5);  // (1-1) - 5 = -5
    }

    SECTION("postfix decrement uses old value in expression") {
        auto f = jit->lookup_symbol<int(*)(int, int)>("sub_postfix_decr");
        REQUIRE(f != nullptr);
        REQUIRE(f(5, 3) == 2);   // 5 - 3 = 2 (old a=5 used)
        REQUIRE(f(10, 4) == 6);  // 10 - 4 = 6
        REQUIRE(f(1, 5) == -4);  // 1 - 5 = -4
    }
}

// ============================================================================
// UNSIGNED INTEGER INCREMENT/DECREMENT
// ============================================================================

TEST_CASE("Increment/decrement on unsigned integer types", "[gen][incr-decr][unsigned]") {
    auto jit = gen_jit(R"SRC(
        module __incr_unsigned__;

        prefix_incr_ubyte(a : byte) : byte {
            return ++a;
        }
        prefix_decr_ubyte(a : byte) : byte {
            return --a;
        }
        postfix_incr_ubyte(a : byte) : byte {
            return a++;
        }
        postfix_decr_ubyte(a : byte) : byte {
            return a--;
        }

        prefix_incr_ulong(a : unsigned long) : unsigned long {
            return ++a;
        }
        prefix_decr_ulong(a : unsigned long) : unsigned long {
            return --a;
        }
        postfix_incr_ulong(a : unsigned long) : unsigned long {
            return a++;
        }
        postfix_decr_ulong(a : unsigned long) : unsigned long {
            return a--;
        }
    )SRC");
    REQUIRE(jit);

    SECTION("prefix increment unsigned byte") {
        auto f = jit->lookup_symbol<unsigned char(*)(unsigned char)>("prefix_incr_ubyte");
        REQUIRE(f != nullptr);
        REQUIRE(f(0) == 1);
        REQUIRE(f(41) == 42);
        REQUIRE(f(100) == 101);
    }

    SECTION("prefix decrement unsigned byte") {
        auto f = jit->lookup_symbol<unsigned char(*)(unsigned char)>("prefix_decr_ubyte");
        REQUIRE(f != nullptr);
        REQUIRE(f(1) == 0);
        REQUIRE(f(42) == 41);
        REQUIRE(f(101) == 100);
    }

    SECTION("postfix increment unsigned byte returns old value") {
        auto f = jit->lookup_symbol<unsigned char(*)(unsigned char)>("postfix_incr_ubyte");
        REQUIRE(f != nullptr);
        REQUIRE(f(0) == 0);
        REQUIRE(f(41) == 41);
        REQUIRE(f(100) == 100);
    }

    SECTION("postfix decrement unsigned byte returns old value") {
        auto f = jit->lookup_symbol<unsigned char(*)(unsigned char)>("postfix_decr_ubyte");
        REQUIRE(f != nullptr);
        REQUIRE(f(1) == 1);
        REQUIRE(f(42) == 42);
        REQUIRE(f(101) == 101);
    }

    SECTION("prefix increment unsigned long") {
        auto f = jit->lookup_symbol<uint64_t(*)(uint64_t)>("prefix_incr_ulong");
        REQUIRE(f != nullptr);
        REQUIRE(f(0UL) == 1UL);
        REQUIRE(f(41UL) == 42UL);
        REQUIRE(f(1000000000UL) == 1000000001UL);
    }

    SECTION("prefix decrement unsigned long") {
        auto f = jit->lookup_symbol<uint64_t(*)(uint64_t)>("prefix_decr_ulong");
        REQUIRE(f != nullptr);
        REQUIRE(f(1UL) == 0UL);
        REQUIRE(f(42UL) == 41UL);
        REQUIRE(f(1000000001UL) == 1000000000UL);
    }

    SECTION("postfix increment unsigned long returns old value") {
        auto f = jit->lookup_symbol<uint64_t(*)(uint64_t)>("postfix_incr_ulong");
        REQUIRE(f != nullptr);
        REQUIRE(f(0UL) == 0UL);
        REQUIRE(f(41UL) == 41UL);
    }

    SECTION("postfix decrement unsigned long returns old value") {
        auto f = jit->lookup_symbol<uint64_t(*)(uint64_t)>("postfix_decr_ulong");
        REQUIRE(f != nullptr);
        REQUIRE(f(1UL) == 1UL);
        REQUIRE(f(42UL) == 42UL);
    }
}

