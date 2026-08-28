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


TEST_CASE( "int32 arithmetic", "[gen][int32][arithmetic]" ) {

    auto jit = gen_jit(R"SRC(
        module gen_arithmetic_int_06;
        add(a : int, b : int) : int {
            return a + b;
        }
        sub(a : int, b : int) : int {
            return a - b;
        }
        mul(a : int, b : int) : int {
            return a * b;
        }
        div(a : int, b : int) : int {
            return a / b;
        }
        mod(a : int, b : int) : int {
            return a % b;
        }
        and(a : int, b : int) : int {
            return a & b;
        }
        or(a : int, b : int) : int {
            return a | b;
        }
        xor(a : int, b : int) : int {
            return a ^ b;
        }
        lsh(a : int, b : int) : int {
            return a << b;
        }
        rsh(a : int, b : int) : int {
            return a >> b;
        }
        plus(a : int) : int {
            return + a;
        }
        minus(a : int) : int {
            return - a;
        }
        not(a : int) : int {
            return ~ a;
        }
        prefix_incr_int(a : int) : int {
            return ++a;
        }
        prefix_decr_int(a : int) : int {
            return --a;
        }
        postfix_incr_int(a : int) : int {
            return a++;
        }
        postfix_decr_int(a : int) : int {
            return a--;
        }
        // Verify the variable is actually mutated by checking it after increment
        prefix_incr_int_mutates(a : int) : int {
            ++a;
            return a;
        }
        // Verify prefix++ returns the new value (already incremented)
        prefix_incr_int_result_is_new(a : int) : int {
            b : int = ++a;
            return b;
        }

        // Verify variable is mutated after postfix++
        postfix_incr_int_mutates(a : int) : int {
            a++;
            return a;
        }

        // Verify postfix++ returns old value (before increment)
        postfix_incr_int_result_is_old(a : int) : int {
            b : int = a++;
            return b;
        }
        prefix_decr_int_mutates(a : int) : int {
            --a;
            return a;
        }
        prefix_decr_int_result_is_new(a : int) : int {
            b : int = --a;
            return b;
        }
        postfix_decr_int_mutates(a : int) : int {
            a--;
            return a;
        }
        postfix_decr_int_result_is_old(a : int) : int {
            b : int = a--;
            return b;
        }
        eq(a:int, b:int) : bool { return a == b; }
        ne(a:int, b:int) : bool { return a != b; }
        lt(a:int, b:int) : bool { return a < b; }
        le(a:int, b:int) : bool { return a <= b; }
        gt(a:int, b:int) : bool { return a > b; }
        ge(a:int, b:int) : bool { return a >= b; }
        )SRC");
    REQUIRE( jit );

    typedef int type_t;

    SECTION( "int32 addition" ) {
        auto add = jit->lookup_symbol<type_t(*)(type_t, type_t)>("add");
        REQUIRE(add != nullptr);
        REQUIRE( add(0, 0) == 0 );
        REQUIRE( add(2, 3) == 5 );
        REQUIRE( add(-2, -3) == -5 );
        REQUIRE( add(42, -42) == 0 );
    }

    SECTION( "int32 substraction" ) {
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

    SECTION( "int32 multiplication" ) {
        auto mul = jit->lookup_symbol<type_t(*)(type_t, type_t)>("mul");
        REQUIRE(mul != nullptr);
        REQUIRE( mul(0, 0) == 0 );
        REQUIRE( mul(2, 3) == 6 );
        REQUIRE( mul(-2, -3) == 6 );
        REQUIRE( mul(2, -3) == -6 );
        REQUIRE( mul(-2, 3) == -6 );
    }

    SECTION( "int32 division" ) {
        auto div = jit->lookup_symbol<type_t(*)(type_t, type_t)>("div");
        REQUIRE(div != nullptr);
        REQUIRE( div(6, 3) == 2 );
        REQUIRE( div(-6, -2) == 3 );
        REQUIRE( div(6, -3) == -2 );
        REQUIRE( div(-6, 2) == -3 );
    }

    SECTION( "int32 modulo" ) {
        auto mod = jit->lookup_symbol<type_t(*)(type_t, type_t)>("mod");
        REQUIRE(mod != nullptr);
        REQUIRE( mod(6, 2) == 0 );
        REQUIRE( mod(7, 3) == 1 );
    }

    SECTION( "int32 bitwise and" ) {
        auto _and = jit->lookup_symbol<type_t(*)(type_t, type_t)>("and");
        REQUIRE(_and != nullptr);
        REQUIRE( _and(5, 3) == 1 );
    }

    SECTION( "int32 bitwise or" ) {
        auto _or = jit->lookup_symbol<type_t(*)(type_t, type_t)>("or");
        REQUIRE(_or != nullptr);
        REQUIRE( _or(5, 3) == 7 );
    }

    SECTION( "int32 bitwise xor" ) {
        auto _xor = jit->lookup_symbol<type_t(*)(type_t, type_t)>("xor");
        REQUIRE(_xor != nullptr);
        REQUIRE( _xor(5, 3) == 6 );
    }

    SECTION( "int32 left shift" ) {
        auto lsh = jit->lookup_symbol<type_t(*)(type_t, type_t)>("lsh");
        REQUIRE(lsh != nullptr);
        REQUIRE( lsh(21, 2) == 84 );
    }

    SECTION( "int32 right shift" ) {
        auto rsh = jit->lookup_symbol<type_t(*)(type_t, type_t)>("rsh");
        REQUIRE(rsh != nullptr);
        REQUIRE( rsh(84, 2) == 21 );
    }

    SECTION( "int32 plus" ) {
        auto plus = jit->lookup_symbol<type_t(*)(type_t)>("plus");
        REQUIRE(plus != nullptr);
        REQUIRE( plus(42) == 42 );
    }

    SECTION( "int32 minus" ) {
        auto minus = jit->lookup_symbol<type_t(*)(type_t)>("minus");
        REQUIRE(minus != nullptr);
        REQUIRE( minus(42) == -42 );
    }

    SECTION("prefix increment int") {
        auto f = jit->lookup_symbol<int(*)(int)>("prefix_incr_int");
        REQUIRE(f != nullptr);
        REQUIRE(f(0) == 1);
        REQUIRE(f(41) == 42);
        REQUIRE(f(-1) == 0);
        REQUIRE(f(-42) == -41);
    }

    SECTION("prefix decrement int") {
        auto f = jit->lookup_symbol<int(*)(int)>("prefix_decr_int");
        REQUIRE(f != nullptr);
        REQUIRE(f(1) == 0);
        REQUIRE(f(42) == 41);
        REQUIRE(f(0) == -1);
        REQUIRE(f(-41) == -42);
    }

    SECTION("postfix increment int") {
        auto f = jit->lookup_symbol<int(*)(int)>("postfix_incr_int");
        REQUIRE(f != nullptr);
        REQUIRE(f(0) == 0);    // returns old value
        REQUIRE(f(41) == 41);  // returns old value (41, not 42)
        REQUIRE(f(-1) == -1);
        REQUIRE(f(-42) == -42);
    }

    SECTION("postfix decrement int") {
        auto f = jit->lookup_symbol<int(*)(int)>("postfix_decr_int");
        REQUIRE(f != nullptr);
        REQUIRE(f(1) == 1);    // returns old value
        REQUIRE(f(42) == 42);  // returns old value (42, not 41)
        REQUIRE(f(0) == 0);
        REQUIRE(f(-41) == -41);
    }

    SECTION("prefix increment mutates the variable") {
        auto f = jit->lookup_symbol<int(*)(int)>("prefix_incr_int_mutates");
        REQUIRE(f != nullptr);
        REQUIRE(f(5) == 6);
        REQUIRE(f(0) == 1);
        REQUIRE(f(-1) == 0);
    }

    SECTION("prefix increment returns new (incremented) value") {
        auto f = jit->lookup_symbol<int(*)(int)>("prefix_incr_int_result_is_new");
        REQUIRE(f != nullptr);
        REQUIRE(f(5) == 6);   // b = ++a, a was 5, so b = 6
        REQUIRE(f(0) == 1);
        REQUIRE(f(-1) == 0);
    }

    SECTION("prefix decrement mutates the variable") {
        auto f = jit->lookup_symbol<int(*)(int)>("prefix_decr_int_mutates");
        REQUIRE(f != nullptr);
        REQUIRE(f(5) == 4);
        REQUIRE(f(0) == -1);
        REQUIRE(f(1) == 0);
    }

    SECTION("prefix decrement returns new (decremented) value") {
        auto f = jit->lookup_symbol<int(*)(int)>("prefix_decr_int_result_is_new");
        REQUIRE(f != nullptr);
        REQUIRE(f(5) == 4);   // b = --a, a was 5, so b = 4
        REQUIRE(f(1) == 0);
        REQUIRE(f(0) == -1);
    }

    SECTION("postfix increment mutates the variable") {
        auto f = jit->lookup_symbol<int(*)(int)>("postfix_incr_int_mutates");
        REQUIRE(f != nullptr);
        REQUIRE(f(5) == 6);   // variable was incremented
        REQUIRE(f(0) == 1);
        REQUIRE(f(-1) == 0);
    }

    SECTION("postfix increment returns old (pre-increment) value") {
        auto f = jit->lookup_symbol<int(*)(int)>("postfix_incr_int_result_is_old");
        REQUIRE(f != nullptr);
        REQUIRE(f(5) == 5);   // b = a++, a was 5, so b = 5 (old value)
        REQUIRE(f(0) == 0);
        REQUIRE(f(-1) == -1);
    }

    SECTION("postfix decrement mutates the variable") {
        auto f = jit->lookup_symbol<int(*)(int)>("postfix_decr_int_mutates");
        REQUIRE(f != nullptr);
        REQUIRE(f(5) == 4);   // variable was decremented
        REQUIRE(f(0) == -1);
        REQUIRE(f(1) == 0);
    }

    SECTION("postfix decrement returns old (pre-decrement) value") {
        auto f = jit->lookup_symbol<int(*)(int)>("postfix_decr_int_result_is_old");
        REQUIRE(f != nullptr);
        REQUIRE(f(5) == 5);   // b = a--, a was 5, so b = 5 (old value)
        REQUIRE(f(0) == 0);
        REQUIRE(f(1) == 1);
    }

    SECTION( "int32 not" ) {
        auto _not = jit->lookup_symbol<type_t(*)(type_t)>("not");
        REQUIRE(_not != nullptr);
        REQUIRE( _not(42) == -43 );
    }

    SECTION("int32 equal") {
        auto eq = jit->lookup_symbol<bool(*)(type_t, type_t)>("eq");
        REQUIRE(eq != nullptr);
        REQUIRE( eq(42, 42) == true );
        REQUIRE( eq(42, 24) == false );
    }

    SECTION("int32 not equal") {
        auto ne = jit->lookup_symbol<bool(*)(type_t, type_t)>("ne");
        REQUIRE(ne != nullptr);
        REQUIRE( ne(42, 42) == false );
        REQUIRE( ne(42, 24) == true );
    }

    SECTION("int32 less than") {
        auto lt = jit->lookup_symbol<bool(*)(type_t, type_t)>("lt");
        REQUIRE(lt != nullptr);
        REQUIRE( lt(42, 42) == false );
        REQUIRE( lt(42, 24) == false );
        REQUIRE( lt(24, 42) == true );
    }

    SECTION("int32 less or equal") {
        auto le = jit->lookup_symbol<bool(*)(type_t, type_t)>("le");
        REQUIRE(le != nullptr);
        REQUIRE( le(42, 42) == true );
        REQUIRE( le(42, 24) == false );
        REQUIRE( le(24, 42) == true );
    }

    SECTION("int32 greater than") {
        auto gt = jit->lookup_symbol<bool(*)(type_t, type_t)>("gt");
        REQUIRE(gt != nullptr);
        REQUIRE( gt(42, 42) == false );
        REQUIRE( gt(42, 24) == true );
        REQUIRE( gt(24, 42) == false );
    }

    SECTION("int32 greater or equal") {
        auto ge = jit->lookup_symbol<bool(*)(type_t, type_t)>("ge");
        REQUIRE(ge != nullptr);
        REQUIRE( ge(42, 42) == true );
        REQUIRE( ge(42, 24) == true );
        REQUIRE( ge(24, 42) == false );
    }
}

TEST_CASE( "uint32 arithmetic", "[gen][uint32][arithmetic]" ) {

    auto jit = gen_jit(R"SRC(
        module gen_arithmetic_int_07;
        add(a : unsigned int, b : unsigned int) : unsigned int {
            return a + b;
        }
        sub(a : unsigned int, b : unsigned int) : unsigned int {
            return a - b;
        }
        mul(a : unsigned int, b : unsigned int) : unsigned int {
            return a * b;
        }
        div(a : unsigned int, b : unsigned int) : unsigned int {
            return a / b;
        }
        mod(a : unsigned int, b : unsigned int) : unsigned int {
            return a % b;
        }
        and(a : unsigned int, b : unsigned int) : unsigned int {
            return a & b;
        }
        or(a : unsigned int, b : unsigned int) : unsigned int {
            return a | b;
        }
        xor(a : unsigned int, b : unsigned int) : unsigned int {
            return a ^ b;
        }
        lsh(a : unsigned int, b : unsigned int) : unsigned int {
            return a << b;
        }
        rsh(a : unsigned int, b : unsigned int) : unsigned int {
            return a >> b;
        }
        plus(a : unsigned int) : unsigned int {
            return + a;
        }
        minus(a : unsigned int) : unsigned int {
            return - a;
        }
        not(a : unsigned int) : unsigned int {
            return ~ a;
        }
        eq(a:unsigned int, b:unsigned int) : bool { return a == b; }
        ne(a:unsigned int, b:unsigned int) : bool { return a != b; }
        lt(a:unsigned int, b:unsigned int) : bool { return a < b; }
        le(a:unsigned int, b:unsigned int) : bool { return a <= b; }
        gt(a:unsigned int, b:unsigned int) : bool { return a > b; }
        ge(a:unsigned int, b:unsigned int) : bool { return a >= b; }
        )SRC");
    REQUIRE( jit );

    typedef uint32_t type_t;

    SECTION( "uint32 addition" ) {
        auto add = jit->lookup_symbol<type_t(*)(type_t, type_t)>("add");
        REQUIRE(add != nullptr);
        REQUIRE( add(0, 0) == 0 );
        REQUIRE( add(2, 3) == 5 );
    }

    SECTION( "uint32 substraction" ) {
        auto sub = jit->lookup_symbol<type_t(*)(type_t, type_t)>("sub");
        REQUIRE(sub != nullptr);
        REQUIRE( sub(0, 0) == 0 );
        REQUIRE( sub(3, 2) == 1 );
        REQUIRE( sub(42, 42) == 0 );
    }

    SECTION( "uint32 multiplication" ) {
        auto mul = jit->lookup_symbol<type_t(*)(type_t, type_t)>("mul");
        REQUIRE(mul != nullptr);
        REQUIRE( mul(0, 0) == 0 );
        REQUIRE( mul(2, 3) == 6 );
    }

    SECTION( "uint32 division" ) {
        auto div = jit->lookup_symbol<type_t(*)(type_t, type_t)>("div");
        REQUIRE(div != nullptr);
        REQUIRE( div(6, 3) == 2 );
    }

    SECTION( "uint32 modulo" ) {
        auto mod = jit->lookup_symbol<type_t(*)(type_t, type_t)>("mod");
        REQUIRE(mod != nullptr);
        REQUIRE( mod(6, 2) == 0 );
        REQUIRE( mod(7, 3) == 1 );
    }

    SECTION( "uint32 bitwise and" ) {
        auto _and = jit->lookup_symbol<type_t(*)(type_t, type_t)>("and");
        REQUIRE(_and != nullptr);
        REQUIRE( _and(5, 3) == 1 );
    }

    SECTION( "uint32 bitwise or" ) {
        auto _or = jit->lookup_symbol<type_t(*)(type_t, type_t)>("or");
        REQUIRE(_or != nullptr);
        REQUIRE( _or(5, 3) == 7 );
    }

    SECTION( "uint32 bitwise xor" ) {
        auto _xor = jit->lookup_symbol<type_t(*)(type_t, type_t)>("xor");
        REQUIRE(_xor != nullptr);
        REQUIRE( _xor(5, 3) == 6 );
    }

    SECTION( "uint32 left shift" ) {
        auto lsh = jit->lookup_symbol<type_t(*)(type_t, type_t)>("lsh");
        REQUIRE(lsh != nullptr);
        REQUIRE( lsh(21, 2) == 84 );
    }

    SECTION( "uint32 right shift" ) {
        auto rsh = jit->lookup_symbol<type_t(*)(type_t, type_t)>("rsh");
        REQUIRE(rsh != nullptr);
        REQUIRE( rsh(84, 2) == 21 );
    }

    SECTION( "uint32 plus" ) {
        auto plus = jit->lookup_symbol<type_t(*)(type_t)>("plus");
        REQUIRE(plus != nullptr);
        REQUIRE( plus(42) == 42 );
    }

    SECTION( "uint32 minus" ) {
        auto minus = jit->lookup_symbol<type_t(*)(type_t)>("minus");
        REQUIRE(minus != nullptr);
        REQUIRE( minus(42) == 4294967254 );
    }

    SECTION( "uint32 not" ) {
        auto _not = jit->lookup_symbol<type_t(*)(type_t)>("not");
        REQUIRE(_not != nullptr);
        REQUIRE( _not(42) == 4294967253 );
    }

    SECTION("uint32 equal") {
        auto eq = jit->lookup_symbol<bool(*)(type_t, type_t)>("eq");
        REQUIRE(eq != nullptr);
        REQUIRE( eq(42, 42) == true );
        REQUIRE( eq(42, 24) == false );
    }

    SECTION("uint32 not equal") {
        auto ne = jit->lookup_symbol<bool(*)(type_t, type_t)>("ne");
        REQUIRE(ne != nullptr);
        REQUIRE( ne(42, 42) == false );
        REQUIRE( ne(42, 24) == true );
    }

    SECTION("uint32 less than") {
        auto lt = jit->lookup_symbol<bool(*)(type_t, type_t)>("lt");
        REQUIRE(lt != nullptr);
        REQUIRE( lt(42, 42) == false );
        REQUIRE( lt(42, 24) == false );
        REQUIRE( lt(24, 42) == true );
    }

    SECTION("uint32 less or equal") {
        auto le = jit->lookup_symbol<bool(*)(type_t, type_t)>("le");
        REQUIRE(le != nullptr);
        REQUIRE( le(42, 42) == true );
        REQUIRE( le(42, 24) == false );
        REQUIRE( le(24, 42) == true );
    }

    SECTION("uint32 greater than") {
        auto gt = jit->lookup_symbol<bool(*)(type_t, type_t)>("gt");
        REQUIRE(gt != nullptr);
        REQUIRE( gt(42, 42) == false );
        REQUIRE( gt(42, 24) == true );
        REQUIRE( gt(24, 42) == false );
    }

    SECTION("uint32 greater or equal") {
        auto ge = jit->lookup_symbol<bool(*)(type_t, type_t)>("ge");
        REQUIRE(ge != nullptr);
        REQUIRE( ge(42, 42) == true );
        REQUIRE( ge(42, 24) == true );
        REQUIRE( ge(24, 42) == false );
    }
}

TEST_CASE( "int64 arithmetic", "[gen][int64][arithmetic]" ) {

    auto jit = gen_jit(R"SRC(
        module gen_arithmetic_int_08;
        add(a : long, b : long) : long {
            return a + b;
        }
        sub(a : long, b : long) : long {
            return a - b;
        }
        mul(a : long, b : long) : long {
            return a * b;
        }
        div(a : long, b : long) : long {
            return a / b;
        }
        mod(a : long, b : long) : long {
            return a % b;
        }
        and(a : long, b : long) : long {
            return a & b;
        }
        or(a : long, b : long) : long {
            return a | b;
        }
        xor(a : long, b : long) : long {
            return a ^ b;
        }
        lsh(a : long, b : long) : long {
            return a << b;
        }
        rsh(a : long, b : long) : long {
            return a >> b;
        }
        plus(a : long) : long {
            return + a;
        }
        minus(a : long) : long {
            return - a;
        }
        not(a : long) : long {
            return ~ a;
        }
        prefix_incr_long(a : long) : long {
            return ++a;
        }
        prefix_decr_long(a : long) : long {
            return --a;
        }
        postfix_incr_long(a : long) : long {
            return a++;
        }
        postfix_decr_long(a : long) : long {
            return a--;
        }
        eq(a:long, b:long) : bool { return a == b; }
        ne(a:long, b:long) : bool { return a != b; }
        lt(a:long, b:long) : bool { return a < b; }
        le(a:long, b:long) : bool { return a <= b; }
        gt(a:long, b:long) : bool { return a > b; }
        ge(a:long, b:long) : bool { return a >= b; }
        )SRC");
    REQUIRE( jit );

    typedef int64_t type_t;

    SECTION( "int64 addition" ) {
        auto add = jit->lookup_symbol<type_t(*)(type_t, type_t)>("add");
        REQUIRE(add != nullptr);
        REQUIRE( add(0, 0) == 0 );
        REQUIRE( add(2, 3) == 5 );
        REQUIRE( add(-2, -3) == -5 );
        REQUIRE( add(42, -42) == 0 );
    }

    SECTION( "int64 substraction" ) {
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

    SECTION( "int64 multiplication" ) {
        auto mul = jit->lookup_symbol<type_t(*)(type_t, type_t)>("mul");
        REQUIRE(mul != nullptr);
        REQUIRE( mul(0, 0) == 0 );
        REQUIRE( mul(2, 3) == 6 );
        REQUIRE( mul(-2, -3) == 6 );
        REQUIRE( mul(2, -3) == -6 );
        REQUIRE( mul(-2, 3) == -6 );
    }

    SECTION( "int64 division" ) {
        auto div = jit->lookup_symbol<type_t(*)(type_t, type_t)>("div");
        REQUIRE(div != nullptr);
        REQUIRE( div(6, 3) == 2 );
        REQUIRE( div(-6, -2) == 3 );
        REQUIRE( div(6, -3) == -2 );
        REQUIRE( div(-6, 2) == -3 );
    }

    SECTION( "int64 modulo" ) {
        auto mod = jit->lookup_symbol<type_t(*)(type_t, type_t)>("mod");
        REQUIRE(mod != nullptr);
        REQUIRE( mod(6, 2) == 0 );
        REQUIRE( mod(7, 3) == 1 );
    }

    SECTION( "int64 bitwise and" ) {
        auto _and = jit->lookup_symbol<type_t(*)(type_t, type_t)>("and");
        REQUIRE(_and != nullptr);
        REQUIRE( _and(5, 3) == 1 );
    }

    SECTION( "int64 bitwise or" ) {
        auto _or = jit->lookup_symbol<type_t(*)(type_t, type_t)>("or");
        REQUIRE(_or != nullptr);
        REQUIRE( _or(5, 3) == 7 );
    }

    SECTION( "int64 bitwise xor" ) {
        auto _xor = jit->lookup_symbol<type_t(*)(type_t, type_t)>("xor");
        REQUIRE(_xor != nullptr);
        REQUIRE( _xor(5, 3) == 6 );
    }

    SECTION( "int64 left shift" ) {
        auto lsh = jit->lookup_symbol<type_t(*)(type_t, type_t)>("lsh");
        REQUIRE(lsh != nullptr);
        REQUIRE( lsh(21, 2) == 84 );
    }

    SECTION( "int64 right shift" ) {
        auto rsh = jit->lookup_symbol<type_t(*)(type_t, type_t)>("rsh");
        REQUIRE(rsh != nullptr);
        REQUIRE( rsh(84, 2) == 21 );
    }

    SECTION( "int64 plus" ) {
        auto plus = jit->lookup_symbol<type_t(*)(type_t)>("plus");
        REQUIRE(plus != nullptr);
        REQUIRE( plus(42) == 42 );
    }

    SECTION( "int64 minus" ) {
        auto minus = jit->lookup_symbol<type_t(*)(type_t)>("minus");
        REQUIRE(minus != nullptr);
        REQUIRE( minus(42) == -42 );
    }

    SECTION("prefix increment long") {
        auto f = jit->lookup_symbol<long(*)(long)>("prefix_incr_long");
        REQUIRE(f != nullptr);
        REQUIRE(f(0L) == 1L);
        REQUIRE(f(41L) == 42L);
        REQUIRE(f(-1L) == 0L);
    }

    SECTION("prefix decrement long") {
        auto f = jit->lookup_symbol<long(*)(long)>("prefix_decr_long");
        REQUIRE(f != nullptr);
        REQUIRE(f(1L) == 0L);
        REQUIRE(f(42L) == 41L);
        REQUIRE(f(0L) == -1L);
    }

    SECTION("postfix increment long") {
        auto f = jit->lookup_symbol<long(*)(long)>("postfix_incr_long");
        REQUIRE(f != nullptr);
        REQUIRE(f(0L) == 0L);
        REQUIRE(f(41L) == 41L);
        REQUIRE(f(-1L) == -1L);
    }

    SECTION("postfix decrement long") {
        auto f = jit->lookup_symbol<long(*)(long)>("postfix_decr_long");
        REQUIRE(f != nullptr);
        REQUIRE(f(1L) == 1L);
        REQUIRE(f(42L) == 42L);
        REQUIRE(f(0L) == 0L);
    }

    SECTION( "int64 not" ) {
        auto _not = jit->lookup_symbol<type_t(*)(type_t)>("not");
        REQUIRE(_not != nullptr);
        REQUIRE( _not(42) == -43 );
    }

    SECTION("int64 equal") {
        auto eq = jit->lookup_symbol<bool(*)(type_t, type_t)>("eq");
        REQUIRE(eq != nullptr);
        REQUIRE( eq(42, 42) == true );
        REQUIRE( eq(42, 24) == false );
    }

    SECTION("int64 not equal") {
        auto ne = jit->lookup_symbol<bool(*)(type_t, type_t)>("ne");
        REQUIRE(ne != nullptr);
        REQUIRE( ne(42, 42) == false );
        REQUIRE( ne(42, 24) == true );
    }

    SECTION("int64 less than") {
        auto lt = jit->lookup_symbol<bool(*)(type_t, type_t)>("lt");
        REQUIRE(lt != nullptr);
        REQUIRE( lt(42, 42) == false );
        REQUIRE( lt(42, 24) == false );
        REQUIRE( lt(24, 42) == true );
    }

    SECTION("int64 less or equal") {
        auto le = jit->lookup_symbol<bool(*)(type_t, type_t)>("le");
        REQUIRE(le != nullptr);
        REQUIRE( le(42, 42) == true );
        REQUIRE( le(42, 24) == false );
        REQUIRE( le(24, 42) == true );
    }

    SECTION("int64 greater than") {
        auto gt = jit->lookup_symbol<bool(*)(type_t, type_t)>("gt");
        REQUIRE(gt != nullptr);
        REQUIRE( gt(42, 42) == false );
        REQUIRE( gt(42, 24) == true );
        REQUIRE( gt(24, 42) == false );
    }

    SECTION("int64 greater or equal") {
        auto ge = jit->lookup_symbol<bool(*)(type_t, type_t)>("ge");
        REQUIRE(ge != nullptr);
        REQUIRE( ge(42, 42) == true );
        REQUIRE( ge(42, 24) == true );
        REQUIRE( ge(24, 42) == false );
    }
}

TEST_CASE( "uint64 arithmetic", "[gen][uint64][arithmetic]" ) {

    auto jit = gen_jit(R"SRC(
        module gen_arithmetic_int_09;
        add(a : unsigned long, b : unsigned long) : unsigned long {
            return a + b;
        }
        sub(a : unsigned long, b : unsigned long) : unsigned long {
            return a - b;
        }
        mul(a : unsigned long, b : unsigned long) : unsigned long {
            return a * b;
        }
        div(a : unsigned long, b : unsigned long) : unsigned long {
            return a / b;
        }
        mod(a : unsigned long, b : unsigned long) : unsigned long {
            return a % b;
        }
        and(a : unsigned long, b : unsigned long) : unsigned long {
            return a & b;
        }
        or(a : unsigned long, b : unsigned long) : unsigned long {
            return a | b;
        }
        xor(a : unsigned long, b : unsigned long) : unsigned long {
            return a ^ b;
        }
        lsh(a : unsigned long, b : unsigned long) : unsigned long {
            return a << b;
        }
        rsh(a : unsigned long, b : unsigned long) : unsigned long {
            return a >> b;
        }
        plus(a : unsigned long) : unsigned long {
            return + a;
        }
        minus(a : unsigned long) : unsigned long {
            return - a;
        }
        not(a : unsigned long) : unsigned long {
            return ~ a;
        }
        eq(a:unsigned long, b:unsigned long) : bool { return a == b; }
        ne(a:unsigned long, b:unsigned long) : bool { return a != b; }
        lt(a:unsigned long, b:unsigned long) : bool { return a < b; }
        le(a:unsigned long, b:unsigned long) : bool { return a <= b; }
        gt(a:unsigned long, b:unsigned long) : bool { return a > b; }
        ge(a:unsigned long, b:unsigned long) : bool { return a >= b; }
        )SRC");
    REQUIRE( jit );

    typedef uint64_t type_t;

    SECTION( "uint64 addition" ) {
        auto add = jit->lookup_symbol<type_t(*)(type_t, type_t)>("add");
        REQUIRE(add != nullptr);
        REQUIRE( add(0, 0) == 0 );
        REQUIRE( add(2, 3) == 5 );
    }

    SECTION( "uint64 substraction" ) {
        auto sub = jit->lookup_symbol<type_t(*)(type_t, type_t)>("sub");
        REQUIRE(sub != nullptr);
        REQUIRE( sub(0, 0) == 0 );
        REQUIRE( sub(3, 2) == 1 );
        REQUIRE( sub(42, 42) == 0 );
    }

    SECTION( "uint64 multiplication" ) {
        auto mul = jit->lookup_symbol<int64_t(*)(int64_t, int64_t)>("mul");
        REQUIRE(mul != nullptr);
        REQUIRE( mul(0, 0) == 0 );
        REQUIRE( mul(2, 3) == 6 );
    }

    SECTION( "uint64 division" ) {
        auto div = jit->lookup_symbol<type_t(*)(type_t, type_t)>("div");
        REQUIRE(div != nullptr);
        REQUIRE( div(6, 3) == 2 );
    }

    SECTION( "uint64 modulo" ) {
        auto mod = jit->lookup_symbol<type_t(*)(type_t, type_t)>("mod");
        REQUIRE(mod != nullptr);
        REQUIRE( mod(6, 2) == 0 );
        REQUIRE( mod(7, 3) == 1 );
    }

    SECTION( "uint64 bitwise and" ) {
        auto _and = jit->lookup_symbol<type_t(*)(type_t, type_t)>("and");
        REQUIRE(_and != nullptr);
        REQUIRE( _and(5, 3) == 1 );
    }

    SECTION( "uint64 bitwise or" ) {
        auto _or = jit->lookup_symbol<type_t(*)(type_t, type_t)>("or");
        REQUIRE(_or != nullptr);
        REQUIRE( _or(5, 3) == 7 );
    }

    SECTION( "uint64 bitwise xor" ) {
        auto _xor = jit->lookup_symbol<type_t(*)(type_t, type_t)>("xor");
        REQUIRE(_xor != nullptr);
        REQUIRE( _xor(5, 3) == 6 );
    }

    SECTION( "uint64 left shift" ) {
        auto lsh = jit->lookup_symbol<type_t(*)(type_t, type_t)>("lsh");
        REQUIRE(lsh != nullptr);
        REQUIRE( lsh(21, 2) == 84 );
    }

    SECTION( "uint64 right shift" ) {
        auto rsh = jit->lookup_symbol<type_t(*)(type_t, type_t)>("rsh");
        REQUIRE(rsh != nullptr);
        REQUIRE( rsh(84, 2) == 21 );
    }

    SECTION( "uint64 plus" ) {
        auto plus = jit->lookup_symbol<type_t(*)(type_t)>("plus");
        REQUIRE(plus != nullptr);
        REQUIRE( plus(42) == 42 );
    }

    SECTION( "uint64 minus" ) {
        auto minus = jit->lookup_symbol<type_t(*)(type_t)>("minus");
        REQUIRE(minus != nullptr);
        REQUIRE( minus(42) == 18446744073709551574ull );
    }

    SECTION( "uint64 not" ) {
        auto _not = jit->lookup_symbol<type_t(*)(type_t)>("not");
        REQUIRE(_not != nullptr);
        REQUIRE( _not(42) == 18446744073709551573ull );
    }

    SECTION("uint64 equal") {
        auto eq = jit->lookup_symbol<bool(*)(type_t, type_t)>("eq");
        REQUIRE(eq != nullptr);
        REQUIRE( eq(42, 42) == true );
        REQUIRE( eq(42, 24) == false );
    }

    SECTION("uint64 not equal") {
        auto ne = jit->lookup_symbol<bool(*)(type_t, type_t)>("ne");
        REQUIRE(ne != nullptr);
        REQUIRE( ne(42, 42) == false );
        REQUIRE( ne(42, 24) == true );
    }

    SECTION("uint64 less than") {
        auto lt = jit->lookup_symbol<bool(*)(type_t, type_t)>("lt");
        REQUIRE(lt != nullptr);
        REQUIRE( lt(42, 42) == false );
        REQUIRE( lt(42, 24) == false );
        REQUIRE( lt(24, 42) == true );
    }

    SECTION("uint64 less or equal") {
        auto le = jit->lookup_symbol<bool(*)(type_t, type_t)>("le");
        REQUIRE(le != nullptr);
        REQUIRE( le(42, 42) == true );
        REQUIRE( le(42, 24) == false );
        REQUIRE( le(24, 42) == true );
    }

    SECTION("uint64 greater than") {
        auto gt = jit->lookup_symbol<bool(*)(type_t, type_t)>("gt");
        REQUIRE(gt != nullptr);
        REQUIRE( gt(42, 42) == false );
        REQUIRE( gt(42, 24) == true );
        REQUIRE( gt(24, 42) == false );
    }

    SECTION("uint64 greater or equal") {
        auto ge = jit->lookup_symbol<bool(*)(type_t, type_t)>("ge");
        REQUIRE(ge != nullptr);
        REQUIRE( ge(42, 42) == true );
        REQUIRE( ge(42, 24) == true );
        REQUIRE( ge(24, 42) == false );
    }
}

