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

TEST_CASE( "byte signed arithmetic", "[gen][byte][arithmetic]" ) {

    auto jit = gen_jit(R"SRC(
        module gen_arithmetic_int_01;
        add(a : byte, b : byte) : byte {
            return a + b;
        }
        sub(a : byte, b : byte) : byte {
            return a - b;
        }
        mul(a : byte, b : byte) : byte {
            return a * b;
        }
        div(a : byte, b : byte) : byte {
            return a / b;
        }
        mod(a : byte, b : byte) : byte {
            return a % b;
        }
        and(a : byte, b : byte) : byte {
            return a & b;
        }
        or(a : byte, b : byte) : byte {
            return a | b;
        }
        xor(a : byte, b : byte) : byte {
            return a ^ b;
        }
        lsh(a : byte, b : byte) : byte {
            return a << b;
        }
        rsh(a : byte, b : byte) : byte {
            return a >> b;
        }
        plus(a : byte) : byte {
            return + a;
        }
        minus(a : byte) : byte {
            return - a;
        }
        not(a : byte) : byte {
            return ~ a;
        }
        prefix_incr_byte(a : byte) : byte {
            return ++a;
        }
        prefix_decr_byte(a : byte) : byte {
            return --a;
        }
        postfix_incr_byte(a : byte) : byte {
            return a++;
        }
        postfix_decr_byte(a : byte) : byte {
            return a--;
        }
        eq(a:char, b:char) : bool { return a == b; }
        ne(a:char, b:char) : bool { return a != b; }
        lt(a:char, b:char) : bool { return a < b; }
        le(a:char, b:char) : bool { return a <= b; }
        gt(a:char, b:char) : bool { return a > b; }
        ge(a:char, b:char) : bool { return a >= b; }
        )SRC");
    REQUIRE( jit );

    typedef int8_t type_t;

    SECTION( "char addition" ) {
        auto add = jit->lookup_symbol<type_t(*)(type_t, type_t)>("add");
        REQUIRE(add != nullptr);
        REQUIRE( add(0, 0) == 0 );
        REQUIRE( add(2, 3) == 5 );
        REQUIRE( add(-2, -3) == -5 );
        REQUIRE( add(42, -42) == 0 );
    }

    SECTION( "char substraction" ) {
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

    SECTION( "char multiplication" ) {
        auto mul = jit->lookup_symbol<type_t(*)(type_t, type_t)>("mul");
        REQUIRE(mul != nullptr);
        REQUIRE( mul(0, 0) == 0 );
        REQUIRE( mul(2, 3) == 6 );
        REQUIRE( mul(-2, -3) == 6 );
        REQUIRE( mul(2, -3) == -6 );
        REQUIRE( mul(-2, 3) == -6 );
    }

    SECTION( "char division" ) {
        auto div = jit->lookup_symbol<type_t(*)(type_t, type_t)>("div");
        REQUIRE(div != nullptr);
        REQUIRE( div(6, 3) == 2 );
        REQUIRE( div(-6, -2) == 3 );
        REQUIRE( div(6, -3) == -2 );
        REQUIRE( div(-6, 2) == -3 );
    }

    SECTION( "char modulo" ) {
        auto mod = jit->lookup_symbol<type_t(*)(type_t, type_t)>("mod");
        REQUIRE(mod != nullptr);
        REQUIRE( mod(6, 2) == 0 );
        REQUIRE( mod(7, 3) == 1 );
    }

    SECTION( "char bitwise and" ) {
        auto _and = jit->lookup_symbol<type_t(*)(type_t, type_t)>("and");
        REQUIRE(_and != nullptr);
        REQUIRE( _and(5, 3) == 1 );
    }

    SECTION( "char bitwise or" ) {
        auto _or = jit->lookup_symbol<type_t(*)(type_t, type_t)>("or");
        REQUIRE(_or != nullptr);
        REQUIRE( _or(5, 3) == 7 );
    }

    SECTION( "char bitwise xor" ) {
        auto _xor = jit->lookup_symbol<type_t(*)(type_t, type_t)>("xor");
        REQUIRE(_xor != nullptr);
        REQUIRE( _xor(5, 3) == 6 );
    }

    SECTION( "char left shift" ) {
        auto lsh = jit->lookup_symbol<type_t(*)(type_t, type_t)>("lsh");
        REQUIRE(lsh != nullptr);
        REQUIRE( lsh(21, 2) == 84 );
    }

    SECTION( "char right shift" ) {
        auto rsh = jit->lookup_symbol<type_t(*)(type_t, type_t)>("rsh");
        REQUIRE(rsh != nullptr);
        REQUIRE( rsh(84, 2) == 21 );
    }

    SECTION( "char plus" ) {
        auto plus = jit->lookup_symbol<type_t(*)(type_t)>("plus");
        REQUIRE(plus != nullptr);
        REQUIRE( plus(42) == 42 );
    }

    SECTION( "char minus" ) {
        auto minus = jit->lookup_symbol<type_t(*)(type_t)>("minus");
        REQUIRE(minus != nullptr);
        REQUIRE( minus(42) == -42 );
    }

    SECTION("prefix increment char") {
        auto f = jit->lookup_symbol<int8_t(*)(int8_t)>("prefix_incr_byte");
        REQUIRE(f != nullptr);
        REQUIRE(f(0) == 1);
        REQUIRE(f(41) == 42);
        REQUIRE(f(-1) == 0);
        REQUIRE(f(-42) == -41);
    }

    SECTION("prefix decrement char") {
        auto f = jit->lookup_symbol<int8_t(*)(int8_t)>("prefix_decr_byte");
        REQUIRE(f != nullptr);
        REQUIRE(f(1) == 0);
        REQUIRE(f(42) == 41);
        REQUIRE(f(0) == -1);
    }

    SECTION("postfix increment char") {
        auto f = jit->lookup_symbol<int8_t(*)(int8_t)>("postfix_incr_byte");
        REQUIRE(f != nullptr);
        REQUIRE(f(0) == 0);    // returns old value
        REQUIRE(f(41) == 41);  // returns old value (41, not 42)
        REQUIRE(f(-1) == -1);
        REQUIRE(f(-42) == -42);
    }

    SECTION( "char not" ) {
        auto _not = jit->lookup_symbol<type_t(*)(type_t)>("not");
        REQUIRE(_not != nullptr);
        REQUIRE( _not(42) == -43 );
    }

    SECTION("postfix decrement char") {
        auto f = jit->lookup_symbol<int8_t(*)(int8_t)>("postfix_decr_byte");
        REQUIRE(f != nullptr);
        REQUIRE(f(1) == 1);    // returns old value
        REQUIRE(f(42) == 42);  // returns old value (42, not 41)
        REQUIRE(f(0) == 0);
    }

    SECTION("char equal") {
        auto eq = jit->lookup_symbol<bool(*)(type_t, type_t)>("eq");
        REQUIRE(eq != nullptr);
        REQUIRE( eq(42, 42) == true );
        REQUIRE( eq(42, 24) == false );
    }

    SECTION("char not equal") {
        auto ne = jit->lookup_symbol<bool(*)(type_t, type_t)>("ne");
        REQUIRE(ne != nullptr);
        REQUIRE( ne(42, 42) == false );
        REQUIRE( ne(42, 24) == true );
    }

    SECTION("char less than") {
        auto lt = jit->lookup_symbol<bool(*)(type_t, type_t)>("lt");
        REQUIRE(lt != nullptr);
        REQUIRE( lt(42, 42) == false );
        REQUIRE( lt(42, 24) == false );
        REQUIRE( lt(24, 42) == true );
    }

    SECTION("char less or equal") {
        auto le = jit->lookup_symbol<bool(*)(type_t, type_t)>("le");
        REQUIRE(le != nullptr);
        REQUIRE( le(42, 42) == true );
        REQUIRE( le(42, 24) == false );
        REQUIRE( le(24, 42) == true );
    }

    SECTION("char greater than") {
        auto gt = jit->lookup_symbol<bool(*)(type_t, type_t)>("gt");
        REQUIRE(gt != nullptr);
        REQUIRE( gt(42, 42) == false );
        REQUIRE( gt(42, 24) == true );
        REQUIRE( gt(24, 42) == false );
    }

    SECTION("char greater or equal") {
        auto ge = jit->lookup_symbol<bool(*)(type_t, type_t)>("ge");
        REQUIRE(ge != nullptr);
        REQUIRE( ge(42, 42) == true );
        REQUIRE( ge(42, 24) == true );
        REQUIRE( ge(24, 42) == false );
    }
}

TEST_CASE( "byte arithmetic", "[gen][byte][arithmetic]" ) {

    auto jit = gen_jit(R"SRC(
        module gen_arithmetic_int_02;
        add(a : byte, b : byte) : byte {
            return a + b;
        }
        sub(a : byte, b : byte) : byte {
            return a - b;
        }
        mul(a : byte, b : byte) : byte {
            return a * b;
        }
        div(a : byte, b : byte) : byte {
            return a / b;
        }
        mod(a : byte, b : byte) : byte {
            return a % b;
        }
        and(a : byte, b : byte) : byte {
            return a & b;
        }
        or(a : byte, b : byte) : byte {
            return a | b;
        }
        xor(a : byte, b : byte) : byte {
            return a ^ b;
        }
        lsh(a : byte, b : byte) : byte {
            return a << b;
        }
        rsh(a : byte, b : byte) : byte {
            return a >> b;
        }
        plus(a : byte) : char {
            return + a;
        }
        minus(a : byte) : char {
            return - a;
        }
        not(a : byte) : char {
            return ~ a;
        }
        prefix_incr_byte(a : byte) : byte {
            return ++a;
        }
        prefix_decr_byte(a : byte) : byte {
            return --a;
        }
        postfix_incr_byte(a : byte) : byte {
            return a++;
        }
        postfix_decr_byte(a : byte) : byte {
            return a--;
        }
        eq(a:byte, b:byte) : bool { return a == b; }
        ne(a:byte, b:byte) : bool { return a != b; }
        lt(a:byte, b:byte) : bool { return a < b; }
        le(a:byte, b:byte) : bool { return a <= b; }
        gt(a:byte, b:byte) : bool { return a > b; }
        ge(a:byte, b:byte) : bool { return a >= b; }
        )SRC");
    REQUIRE( jit );

    typedef uint8_t type_t;

    SECTION( "byte addition" ) {
        auto add = jit->lookup_symbol<type_t(*)(type_t, type_t)>("add");
        REQUIRE(add != nullptr);
        REQUIRE( add(0, 0) == 0 );
        REQUIRE( add(2, 3) == 5 );
    }

    SECTION( "byte substraction" ) {
        auto sub = jit->lookup_symbol<type_t(*)(type_t, type_t)>("sub");
        REQUIRE(sub != nullptr);
        REQUIRE( sub(0, 0) == 0 );
        REQUIRE( sub(3, 2) == 1 );
        REQUIRE( sub(42, 42) == 0 );
    }

    SECTION( "byte multiplication" ) {
        auto mul = jit->lookup_symbol<type_t(*)(type_t, type_t)>("mul");
        REQUIRE(mul != nullptr);
        REQUIRE( mul(0, 0) == 0 );
        REQUIRE( mul(2, 3) == 6 );
    }

    SECTION( "byte division" ) {
        auto div = jit->lookup_symbol<type_t(*)(type_t, type_t)>("div");
        REQUIRE(div != nullptr);
        REQUIRE( div(6, 3) == 2 );
    }

    SECTION( "byte modulo" ) {
        auto mod = jit->lookup_symbol<type_t(*)(type_t, type_t)>("mod");
        REQUIRE(mod != nullptr);
        REQUIRE( mod(6, 2) == 0 );
        REQUIRE( mod(7, 3) == 1 );
    }

    SECTION( "byte bitwise and" ) {
        auto _and = jit->lookup_symbol<type_t(*)(type_t, type_t)>("and");
        REQUIRE(_and != nullptr);
        REQUIRE( _and(5, 3) == 1 );
    }

    SECTION( "byte bitwise or" ) {
        auto _or = jit->lookup_symbol<type_t(*)(type_t, type_t)>("or");
        REQUIRE(_or != nullptr);
        REQUIRE( _or(5, 3) == 7 );
    }

    SECTION( "byte bitwise xor" ) {
        auto _xor = jit->lookup_symbol<type_t(*)(type_t, type_t)>("xor");
        REQUIRE(_xor != nullptr);
        REQUIRE( _xor(5, 3) == 6 );
    }

    SECTION( "byte left shift" ) {
        auto lsh = jit->lookup_symbol<type_t(*)(type_t, type_t)>("lsh");
        REQUIRE(lsh != nullptr);
        REQUIRE( lsh(21, 2) == 84 );
    }

    SECTION( "byte right shift" ) {
        auto rsh = jit->lookup_symbol<type_t(*)(type_t, type_t)>("rsh");
        REQUIRE(rsh != nullptr);
        REQUIRE( rsh(84, 2) == 21 );
    }

    SECTION( "byte plus" ) {
        auto plus = jit->lookup_symbol<type_t(*)(type_t)>("plus");
        REQUIRE(plus != nullptr);
        REQUIRE( plus(42) == 42 );
    }

    SECTION( "byte minus" ) {
        auto minus = jit->lookup_symbol<type_t(*)(type_t)>("minus");
        REQUIRE(minus != nullptr);
        REQUIRE( minus(42) == 214 );
    }

    SECTION("prefix increment byte") {
        auto f = jit->lookup_symbol<unsigned char(*)(unsigned char)>("prefix_incr_byte");
        REQUIRE(f != nullptr);
        REQUIRE(f(0) == 1);
        REQUIRE(f(41) == 42);
        REQUIRE(f(100) == 101);
    }

    SECTION("prefix decrement byte") {
        auto f = jit->lookup_symbol<unsigned char(*)(unsigned char)>("prefix_decr_byte");
        REQUIRE(f != nullptr);
        REQUIRE(f(1) == 0);
        REQUIRE(f(42) == 41);
        REQUIRE(f(100) == 99);
    }

    SECTION("postfix increment byte") {
        auto f = jit->lookup_symbol<unsigned char(*)(unsigned char)>("postfix_incr_byte");
        REQUIRE(f != nullptr);
        REQUIRE(f(0) == 0);
        REQUIRE(f(41) == 41);
        REQUIRE(f(100) == 100);
    }

    SECTION("postfix decrement byte") {
        auto f = jit->lookup_symbol<unsigned char(*)(unsigned char)>("postfix_decr_byte");
        REQUIRE(f != nullptr);
        REQUIRE(f(1) == 1);
        REQUIRE(f(42) == 42);
        REQUIRE(f(100) == 100);
    }

    SECTION( "byte not" ) {
        auto _not = jit->lookup_symbol<type_t(*)(type_t)>("not");
        REQUIRE(_not != nullptr);
        REQUIRE( _not(42) == 213 );
    }

    SECTION("byte equal") {
        auto eq = jit->lookup_symbol<bool(*)(type_t, type_t)>("eq");
        REQUIRE(eq != nullptr);
        REQUIRE( eq(42, 42) == true );
        REQUIRE( eq(42, 24) == false );
    }

    SECTION("byte not equal") {
        auto ne = jit->lookup_symbol<bool(*)(type_t, type_t)>("ne");
        REQUIRE(ne != nullptr);
        REQUIRE( ne(42, 42) == false );
        REQUIRE( ne(42, 24) == true );
    }

    SECTION("byte less than") {
        auto lt = jit->lookup_symbol<bool(*)(type_t, type_t)>("lt");
        REQUIRE(lt != nullptr);
        REQUIRE( lt(42, 42) == false );
        REQUIRE( lt(42, 24) == false );
        REQUIRE( lt(24, 42) == true );
    }

    SECTION("byte less or equal") {
        auto le = jit->lookup_symbol<bool(*)(type_t, type_t)>("le");
        REQUIRE(le != nullptr);
        REQUIRE( le(42, 42) == true );
        REQUIRE( le(42, 24) == false );
        REQUIRE( le(24, 42) == true );
    }

    SECTION("byte greater than") {
        auto gt = jit->lookup_symbol<bool(*)(type_t, type_t)>("gt");
        REQUIRE(gt != nullptr);
        REQUIRE( gt(42, 42) == false );
        REQUIRE( gt(42, 24) == true );
        REQUIRE( gt(24, 42) == false );
    }

    SECTION("byte greater or equal") {
        auto ge = jit->lookup_symbol<bool(*)(type_t, type_t)>("ge");
        REQUIRE(ge != nullptr);
        REQUIRE( ge(42, 42) == true );
        REQUIRE( ge(42, 24) == true );
        REQUIRE( ge(24, 42) == false );
    }
}

TEST_CASE( "unsigned byte arithmetic and conversions", "[gen][unsigned-byte][arithmetic]" ) {

    auto jit = gen_jit(R"SRC(
        module gen_arithmetic_int_03;
        add(a : unsigned byte, b : unsigned byte) : unsigned byte {
            return a + b;
        }
        sub(a : unsigned byte, b : unsigned byte) : unsigned byte {
            return a - b;
        }
        rsh(a : unsigned byte, b : unsigned byte) : unsigned byte {
            return a >> b;
        }
        lt(a : unsigned byte, b : unsigned byte) : bool {
            return a < b;
        }
        from_int(a : int) : unsigned byte {
            ub : unsigned byte = (unsigned byte) a;
            return ub;
        }
        to_uint(a : unsigned byte) : unsigned int {
            return a;
        }
        )SRC");
    REQUIRE( jit );

    typedef uint8_t type_t;

    SECTION( "unsigned byte addition wraps" ) {
        auto add = jit->lookup_symbol<type_t(*)(type_t, type_t)>("add");
        REQUIRE(add != nullptr);
        REQUIRE( add(2, 3) == 5 );
        REQUIRE( add(200, 100) == static_cast<type_t>(300) ); // wraps to 44
    }

    SECTION( "unsigned byte subtraction" ) {
        auto sub = jit->lookup_symbol<type_t(*)(type_t, type_t)>("sub");
        REQUIRE(sub != nullptr);
        REQUIRE( sub(5, 3) == 2 );
    }

    SECTION( "unsigned byte right shift is logical" ) {
        auto rsh = jit->lookup_symbol<type_t(*)(type_t, type_t)>("rsh");
        REQUIRE(rsh != nullptr);
        // 0x80 >> 1 == 0x40 (no sign extension because unsigned)
        REQUIRE( rsh(0x80, 1) == 0x40 );
    }

    SECTION( "unsigned byte comparison is unsigned" ) {
        auto lt = jit->lookup_symbol<bool(*)(type_t, type_t)>("lt");
        REQUIRE(lt != nullptr);
        // 0x80 (128) is greater than 1 when unsigned
        REQUIRE( lt(0x80, 1) == false );
        REQUIRE( lt(1, 0x80) == true );
    }

    SECTION( "cast int to unsigned byte truncates" ) {
        auto from_int = jit->lookup_symbol<type_t(*)(int)>("from_int");
        REQUIRE(from_int != nullptr);
        REQUIRE( from_int(300) == static_cast<type_t>(300) ); // 44
        REQUIRE( from_int(-1) == 0xFF );
    }

    SECTION( "unsigned byte widens to unsigned int without sign extension" ) {
        auto to_uint = jit->lookup_symbol<unsigned int(*)(type_t)>("to_uint");
        REQUIRE(to_uint != nullptr);
        REQUIRE( to_uint(0x80) == 0x80u );
        REQUIRE( to_uint(0xFF) == 0xFFu );
    }
}

TEST_CASE( "int16 arithmetic", "[gen][int16][arithmetic]" ) {

    auto jit = gen_jit(R"SRC(
        module gen_arithmetic_int_04;
        add(a : short, b : short) : short {
            return a + b;
        }
        sub(a : short, b : short) : short {
            return a - b;
        }
        mul(a : short, b : short) : short {
            return a * b;
        }
        div(a : short, b : short) : short {
            return a / b;
        }
        mod(a : short, b : short) : short {
            return a % b;
        }
        and(a : short, b : short) : short {
            return a & b;
        }
        or(a : short, b : short) : short {
            return a | b;
        }
        xor(a : short, b : short) : short {
            return a ^ b;
        }
        lsh(a : short, b : short) : short {
            return a << b;
        }
        rsh(a : short, b : short) : short {
            return a >> b;
        }
        plus(a : short) : short {
            return + a;
        }
        minus(a : short) : short {
            return - a;
        }
        not(a : short) : short {
            return ~ a;
        }
        prefix_incr_short(a : short) : short {
            return ++a;
        }
        prefix_decr_short(a : short) : short {
            return --a;
        }
        postfix_incr_short(a : short) : short {
            return a++;
        }
        postfix_decr_short(a : short) : short {
            return a--;
        }
        eq(a:short, b:short) : bool { return a == b; }
        ne(a:short, b:short) : bool { return a != b; }
        lt(a:short, b:short) : bool { return a < b; }
        le(a:short, b:short) : bool { return a <= b; }
        gt(a:short, b:short) : bool { return a > b; }
        ge(a:short, b:short) : bool { return a >= b; }
        )SRC");
    REQUIRE( jit );

    typedef short type_t;

    SECTION( "int16 addition" ) {
        auto add = jit->lookup_symbol<type_t(*)(type_t, type_t)>("add");
        REQUIRE(add != nullptr);
        REQUIRE( add(0, 0) == 0 );
        REQUIRE( add(2, 3) == 5 );
        REQUIRE( add(-2, -3) == -5 );
        REQUIRE( add(42, -42) == 0 );
    }

    SECTION( "int16 substraction" ) {
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

    SECTION( "int16 multiplication" ) {
        auto mul = jit->lookup_symbol<type_t(*)(type_t, type_t)>("mul");
        REQUIRE(mul != nullptr);
        REQUIRE( mul(0, 0) == 0 );
        REQUIRE( mul(2, 3) == 6 );
        REQUIRE( mul(-2, -3) == 6 );
        REQUIRE( mul(2, -3) == -6 );
        REQUIRE( mul(-2, 3) == -6 );
    }

    SECTION( "int16 division" ) {
        auto div = jit->lookup_symbol<type_t(*)(type_t, type_t)>("div");
        REQUIRE(div != nullptr);
        REQUIRE( div(6, 3) == 2 );
        REQUIRE( div(-6, -2) == 3 );
        REQUIRE( div(6, -3) == -2 );
        REQUIRE( div(-6, 2) == -3 );
    }

    SECTION( "int16 modulo" ) {
        auto mod = jit->lookup_symbol<type_t(*)(type_t, type_t)>("mod");
        REQUIRE(mod != nullptr);
        REQUIRE( mod(6, 2) == 0 );
        REQUIRE( mod(7, 3) == 1 );
    }

    SECTION( "int16 bitwise and" ) {
        auto _and = jit->lookup_symbol<type_t(*)(type_t, type_t)>("and");
        REQUIRE(_and != nullptr);
        REQUIRE( _and(5, 3) == 1 );
    }

    SECTION( "int16 bitwise or" ) {
        auto _or = jit->lookup_symbol<type_t(*)(type_t, type_t)>("or");
        REQUIRE(_or != nullptr);
        REQUIRE( _or(5, 3) == 7 );
    }

    SECTION( "int16 bitwise xor" ) {
        auto _xor = jit->lookup_symbol<type_t(*)(type_t, type_t)>("xor");
        REQUIRE(_xor != nullptr);
        REQUIRE( _xor(5, 3) == 6 );
    }

    SECTION( "int16 left shift" ) {
        auto lsh = jit->lookup_symbol<type_t(*)(type_t, type_t)>("lsh");
        REQUIRE(lsh != nullptr);
        REQUIRE( lsh(21, 2) == 84 );
    }

    SECTION( "int16 right shift" ) {
        auto rsh = jit->lookup_symbol<type_t(*)(type_t, type_t)>("rsh");
        REQUIRE(rsh != nullptr);
        REQUIRE( rsh(84, 2) == 21 );
    }

    SECTION( "int16 plus" ) {
        auto plus = jit->lookup_symbol<type_t(*)(type_t)>("plus");
        REQUIRE(plus != nullptr);
        REQUIRE( plus(42) == 42 );
    }

    SECTION( "int16 minus" ) {
        auto minus = jit->lookup_symbol<type_t(*)(type_t)>("minus");
        REQUIRE(minus != nullptr);
        REQUIRE( minus(42) == -42 );
    }

    SECTION("prefix increment short") {
        auto f = jit->lookup_symbol<short(*)(short)>("prefix_incr_short");
        REQUIRE(f != nullptr);
        REQUIRE(f(0) == 1);
        REQUIRE(f(41) == 42);
        REQUIRE(f(-1) == 0);
    }

    SECTION("prefix decrement short") {
        auto f = jit->lookup_symbol<short(*)(short)>("prefix_decr_short");
        REQUIRE(f != nullptr);
        REQUIRE(f(1) == 0);
        REQUIRE(f(42) == 41);
        REQUIRE(f(0) == -1);
    }

    SECTION("postfix increment short") {
        auto f = jit->lookup_symbol<short(*)(short)>("postfix_incr_short");
        REQUIRE(f != nullptr);
        REQUIRE(f(0) == 0);
        REQUIRE(f(41) == 41);
        REQUIRE(f(-1) == -1);
    }

    SECTION("postfix decrement short") {
        auto f = jit->lookup_symbol<short(*)(short)>("postfix_decr_short");
        REQUIRE(f != nullptr);
        REQUIRE(f(1) == 1);
        REQUIRE(f(42) == 42);
        REQUIRE(f(0) == 0);
    }

    SECTION( "int16 not" ) {
        auto _not = jit->lookup_symbol<type_t(*)(type_t)>("not");
        REQUIRE(_not != nullptr);
        REQUIRE( _not(42) == -43 );
    }

    SECTION("int16 equal") {
        auto eq = jit->lookup_symbol<bool(*)(type_t, type_t)>("eq");
        REQUIRE(eq != nullptr);
        REQUIRE( eq(42, 42) == true );
        REQUIRE( eq(42, 24) == false );
    }

    SECTION("int16 not equal") {
        auto ne = jit->lookup_symbol<bool(*)(type_t, type_t)>("ne");
        REQUIRE(ne != nullptr);
        REQUIRE( ne(42, 42) == false );
        REQUIRE( ne(42, 24) == true );
    }

    SECTION("int16 less than") {
        auto lt = jit->lookup_symbol<bool(*)(type_t, type_t)>("lt");
        REQUIRE(lt != nullptr);
        REQUIRE( lt(42, 42) == false );
        REQUIRE( lt(42, 24) == false );
        REQUIRE( lt(24, 42) == true );
    }

    SECTION("int16 less or equal") {
        auto le = jit->lookup_symbol<bool(*)(type_t, type_t)>("le");
        REQUIRE(le != nullptr);
        REQUIRE( le(42, 42) == true );
        REQUIRE( le(42, 24) == false );
        REQUIRE( le(24, 42) == true );
    }

    SECTION("int16 greater than") {
        auto gt = jit->lookup_symbol<bool(*)(type_t, type_t)>("gt");
        REQUIRE(gt != nullptr);
        REQUIRE( gt(42, 42) == false );
        REQUIRE( gt(42, 24) == true );
        REQUIRE( gt(24, 42) == false );
    }

    SECTION("int16 greater or equal") {
        auto ge = jit->lookup_symbol<bool(*)(type_t, type_t)>("ge");
        REQUIRE(ge != nullptr);
        REQUIRE( ge(42, 42) == true );
        REQUIRE( ge(42, 24) == true );
        REQUIRE( ge(24, 42) == false );
    }
}

TEST_CASE( "uint16 arithmetic", "[gen][uint16][arithmetic]" ) {

    auto jit = gen_jit(R"SRC(
        module gen_arithmetic_int_05;
        add(a : unsigned short, b : unsigned short) : unsigned short {
            return a + b;
        }
        sub(a : unsigned short, b : unsigned short) : unsigned short {
            return a - b;
        }
        mul(a : unsigned short, b : unsigned short) : unsigned short {
            return a * b;
        }
        div(a : unsigned short, b : unsigned short) : unsigned short {
            return a / b;
        }
        mod(a : unsigned short, b : unsigned short) : unsigned short {
            return a % b;
        }
        and(a : unsigned short, b : unsigned short) : unsigned short {
            return a & b;
        }
        or(a : unsigned short, b : unsigned short) : unsigned short {
            return a | b;
        }
        xor(a : unsigned short, b : unsigned short) : unsigned short {
            return a ^ b;
        }
        lsh(a : unsigned short, b : unsigned short) : unsigned short {
            return a << b;
        }
        rsh(a : unsigned short, b : unsigned short) : unsigned short {
            return a >> b;
        }
        plus(a : unsigned short) : unsigned short {
            return + a;
        }
        minus(a : unsigned short) : unsigned short {
            return - a;
        }
        not(a : unsigned short) : unsigned short {
            return ~ a;
        }
        eq(a:unsigned short, b:unsigned short) : bool { return a == b; }
        ne(a:unsigned short, b:unsigned short) : bool { return a != b; }
        lt(a:unsigned short, b:unsigned short) : bool { return a < b; }
        le(a:unsigned short, b:unsigned short) : bool { return a <= b; }
        gt(a:unsigned short, b:unsigned short) : bool { return a > b; }
        ge(a:unsigned short, b:unsigned short) : bool { return a >= b; }
        )SRC");
    REQUIRE( jit );

    typedef uint16_t type_t;

    SECTION( "uint16 addition" ) {
        auto add = jit->lookup_symbol<type_t(*)(type_t, type_t)>("add");
        REQUIRE(add != nullptr);
        REQUIRE( add(0, 0) == 0 );
        REQUIRE( add(2, 3) == 5 );
        REQUIRE( add(42, -42) == 0 );
    }

    SECTION( "uint16 substraction" ) {
        auto sub = jit->lookup_symbol<type_t(*)(type_t, type_t)>("sub");
        REQUIRE(sub != nullptr);
        REQUIRE( sub(0, 0) == 0 );
        REQUIRE( sub(3, 2) == 1 );
        REQUIRE( sub(42, 42) == 0 );
    }

    SECTION( "uint16 multiplication" ) {
        auto mul = jit->lookup_symbol<type_t(*)(type_t, type_t)>("mul");
        REQUIRE(mul != nullptr);
        REQUIRE( mul(0, 0) == 0 );
        REQUIRE( mul(2, 3) == 6 );
    }

    SECTION( "uint16 division" ) {
        auto div = jit->lookup_symbol<type_t(*)(type_t, type_t)>("div");
        REQUIRE(div != nullptr);
        REQUIRE( div(6, 3) == 2 );
    }

    SECTION( "uint16 modulo" ) {
        auto mod = jit->lookup_symbol<type_t(*)(type_t, type_t)>("mod");
        REQUIRE(mod != nullptr);
        REQUIRE( mod(6, 2) == 0 );
        REQUIRE( mod(7, 3) == 1 );
    }

    SECTION( "uint16 bitwise and" ) {
        auto _and = jit->lookup_symbol<type_t(*)(type_t, type_t)>("and");
        REQUIRE(_and != nullptr);
        REQUIRE( _and(5, 3) == 1 );
    }

    SECTION( "uint16 bitwise or" ) {
        auto _or = jit->lookup_symbol<type_t(*)(type_t, type_t)>("or");
        REQUIRE(_or != nullptr);
        REQUIRE( _or(5, 3) == 7 );
    }

    SECTION( "uint16 bitwise xor" ) {
        auto _xor = jit->lookup_symbol<type_t(*)(type_t, type_t)>("xor");
        REQUIRE(_xor != nullptr);
        REQUIRE( _xor(5, 3) == 6 );
    }

    SECTION( "uint16 left shift" ) {
        auto lsh = jit->lookup_symbol<type_t(*)(type_t, type_t)>("lsh");
        REQUIRE(lsh != nullptr);
        REQUIRE( lsh(21, 2) == 84 );
    }

    SECTION( "uint16 right shift" ) {
        auto rsh = jit->lookup_symbol<type_t(*)(type_t, type_t)>("rsh");
        REQUIRE(rsh != nullptr);
        REQUIRE( rsh(84, 2) == 21 );
    }

    SECTION( "uint16 plus" ) {
        auto plus = jit->lookup_symbol<type_t(*)(type_t)>("plus");
        REQUIRE(plus != nullptr);
        REQUIRE( plus(42) == 42 );
    }

    SECTION( "uint16 minus" ) {
        auto minus = jit->lookup_symbol<type_t(*)(type_t)>("minus");
        REQUIRE(minus != nullptr);
        REQUIRE( minus(42) == 65494 );
    }

    SECTION( "uint16 not" ) {
        auto _not = jit->lookup_symbol<type_t(*)(type_t)>("not");
        REQUIRE(_not != nullptr);
        REQUIRE( _not(42) == 65493 );
    }

    SECTION("uint16 equal") {
        auto eq = jit->lookup_symbol<bool(*)(type_t, type_t)>("eq");
        REQUIRE(eq != nullptr);
        REQUIRE( eq(42, 42) == true );
        REQUIRE( eq(42, 24) == false );
    }

    SECTION("uint16 not equal") {
        auto ne = jit->lookup_symbol<bool(*)(type_t, type_t)>("ne");
        REQUIRE(ne != nullptr);
        REQUIRE( ne(42, 42) == false );
        REQUIRE( ne(42, 24) == true );
    }

    SECTION("uint16 less than") {
        auto lt = jit->lookup_symbol<bool(*)(type_t, type_t)>("lt");
        REQUIRE(lt != nullptr);
        REQUIRE( lt(42, 42) == false );
        REQUIRE( lt(42, 24) == false );
        REQUIRE( lt(24, 42) == true );
    }

    SECTION("uint16 less or equal") {
        auto le = jit->lookup_symbol<bool(*)(type_t, type_t)>("le");
        REQUIRE(le != nullptr);
        REQUIRE( le(42, 42) == true );
        REQUIRE( le(42, 24) == false );
        REQUIRE( le(24, 42) == true );
    }

    SECTION("uint16 greater than") {
        auto gt = jit->lookup_symbol<bool(*)(type_t, type_t)>("gt");
        REQUIRE(gt != nullptr);
        REQUIRE( gt(42, 42) == false );
        REQUIRE( gt(42, 24) == true );
        REQUIRE( gt(24, 42) == false );
    }

    SECTION("uint16 greater or equal") {
        auto ge = jit->lookup_symbol<bool(*)(type_t, type_t)>("ge");
        REQUIRE(ge != nullptr);
        REQUIRE( ge(42, 42) == true );
        REQUIRE( ge(42, 24) == true );
        REQUIRE( ge(24, 42) == false );
    }
}

