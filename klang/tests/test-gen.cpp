/*
 * K Language compiler
 *
 * Copyright 2023 Emilien Kia
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

#include <catch2/catch.hpp>

#include "../src/common/logger.hpp"
#include "../src/parse/parser.hpp"
#include "../src/parse/ast_dump.hpp"
#include "../src/model/model.hpp"
#include "../src/model/model_builder.hpp"
#include "../src/model/model_dump.hpp"
#include "../src/gen/symbol_type_resolver.hpp"
#include "../src/gen/unit_llvm_ir_gen.hpp"


std::unique_ptr<k::model::gen::unit_llvm_jit> gen(std::string_view src, bool dump = false) {
    k::log::logger log;
    k::parse::parser parser(log, src);
    std::shared_ptr<k::parse::ast::unit> ast_unit = parser.parse_unit();

    if(dump) {
        k::parse::dump::ast_dump_visitor visit(std::cout);
        std::cout << "#" << std::endl << "# Parsing" << std::endl << "#" << std::endl;
        visit.visit_unit(*ast_unit);
    }

    k::model::unit unit;
    k::model::model_builder::visit(log, *ast_unit, unit);

    if(dump) {
        k::model::dump::unit_dump unit_dump(std::cout);
        std::cout << "#" << std::endl << "# Unit construction" << std::endl << "#" << std::endl;
        unit_dump.dump(unit);
    }

    k::model::gen::symbol_type_resolver var_resolver(log, unit);
    var_resolver.resolve();

    if(dump) {
        k::model::dump::unit_dump unit_dump(std::cout);
        std::cout << "#" << std::endl << "# Variable resolution" << std::endl << "#" << std::endl;
        unit_dump.dump(unit);
    }

    k::model::gen::unit_llvm_ir_gen gen(log, unit);
    unit.accept(gen);
    gen.verify();

    if(dump) {
        std::cout << "#" << std::endl << "# LLVM Module" << std::endl << "#" << std::endl;
        gen.dump();
    }

    gen.optimize_functions();
    gen.verify();

    if(dump) {
        std::cout << "#" << std::endl << "# LLVM Optimize Module" << std::endl << "#" << std::endl;
        gen.dump();
    }

    return gen.to_jit();
}


TEST_CASE( "Simple method", "[gen]" ) {

    SECTION("Simple int() method") {
        auto jit = gen(R"SRC(
        module test;
        test() : int {
            return 42;
        }
        )SRC");
        REQUIRE(jit);

        auto test = jit->lookup_symbol < int(*)() > ("test");
        REQUIRE(test != nullptr);
        REQUIRE(test() == 42);
    }

    SECTION( "Simple int(int) method" ) {
        auto jit = gen(R"SRC(
        module test;
        increment(i : int) : int {
            return i + 1;
        }
        )SRC");
        REQUIRE( jit );

        auto increment = jit->lookup_symbol<int(*)(int)>("increment");
        REQUIRE(increment != nullptr);
        REQUIRE( increment(41) == 42 );
    }

    SECTION( "Simple int(int, int) method" ) {
        auto jit = gen(R"SRC(
        module test;
        multiply(a : int, b : int) : int {
            return a * b;
        }
        )SRC");
        REQUIRE( jit );

        auto multiply = jit->lookup_symbol<int(*)(int, int)>("multiply");
        REQUIRE(multiply != nullptr);
        REQUIRE( multiply(2, 3) == 6 );
    }
}

TEST_CASE( "char arithmetic", "[gen][char][arithmetic]" ) {

    auto jit = gen(R"SRC(
        module __int8__;
        add(a : char, b : char) : char {
            return a + b;
        }
        sub(a : char, b : char) : char {
            return a - b;
        }
        mul(a : char, b : char) : char {
            return a * b;
        }
        div(a : char, b : char) : char {
            return a / b;
        }
        mod(a : char, b : char) : char {
            return a % b;
        }
        and(a : char, b : char) : char {
            return a & b;
        }
        or(a : char, b : char) : char {
            return a | b;
        }
        xor(a : char, b : char) : char {
            return a ^ b;
        }
        lsh(a : char, b : char) : char {
            return a << b;
        }
        rsh(a : char, b : char) : char {
            return a >> b;
        }
        plus(a : char) : char {
            return + a;
        }
        minus(a : char) : char {
            return - a;
        }
        not(a : char) : char {
            return ~ a;
        }
        eq(a:char, b:char) : bool { return a == b; }
        ne(a:char, b:char) : bool { return a != b; }
        lt(a:char, b:char) : bool { return a < b; }
        le(a:char, b:char) : bool { return a <= b; }
        gt(a:char, b:char) : bool { return a > b; }
        ge(a:char, b:char) : bool { return a >= b; }
        )SRC");
    REQUIRE( jit );

    typedef char type_t;

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

    SECTION( "char not" ) {
        auto _not = jit->lookup_symbol<type_t(*)(type_t)>("not");
        REQUIRE(_not != nullptr);
        REQUIRE( _not(42) == -43 );
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

    auto jit = gen(R"SRC(
        module __uint8__;
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


TEST_CASE( "int16 arithmetic", "[gen][int16][arithmetic]" ) {

    auto jit = gen(R"SRC(
        module __int16__;
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

    auto jit = gen(R"SRC(
        module __int16__;
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

TEST_CASE( "int32 arithmetic", "[gen][int32][arithmetic]" ) {

    auto jit = gen(R"SRC(
        module __int32__;
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

    auto jit = gen(R"SRC(
        module __uint32__;
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

    auto jit = gen(R"SRC(
        module __int64__;
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

    auto jit = gen(R"SRC(
        module __uint64__;
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

TEST_CASE( "float arithmetic", "[gen][float][arithmetic]" ) {

    auto jit = gen(R"SRC(
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

    auto jit = gen(R"SRC(
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
    auto jit = gen(R"SRC(
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
    auto jit = gen(R"SRC(
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



//
// If-then-else
//

TEST_CASE("If-then-else", "[gen][if-else]") {
    auto jit = gen(R"SRC(
        module __if__;
        min(a: int, b: int) : int {
            if(a<b)
                return a;
            else
                return b;
        }
        max(a: int, b: int) : int {
            if(a>b) {
                return a;
            } else {
                return b;
            }
        }
        fibo(i: unsigned short) : unsigned long {
            if(i==0) return 1;
            else if(i==1) return 1;
            return fibo(i-1) + fibo(i-2);
        }
        )SRC");
    REQUIRE(jit);

    SECTION("if-then-else simple return statement") {
        auto min = jit->lookup_symbol<int(*)(int,int)>("min");
        REQUIRE(min != nullptr);
        REQUIRE(min(4,2) == 2);
        REQUIRE(min(2,4) == 2);
    }

    SECTION("if-then-else with blocks") {
        auto max = jit->lookup_symbol<int(*)(int,int)>("max");
        REQUIRE(max != nullptr);
        REQUIRE(max(4,2) == 4);
        REQUIRE(max(2,4) == 4);
    }

    SECTION("if-then-else with nested if and no else") {
        auto fibo = jit->lookup_symbol<uint64_t(*)(unsigned short)>("fibo");
        REQUIRE(fibo != nullptr);
        REQUIRE(fibo(0) == 1);
        REQUIRE(fibo(1) == 1);
        REQUIRE(fibo(2) == 2);
        REQUIRE(fibo(3) == 3);
        REQUIRE(fibo(4) == 5);
        REQUIRE(fibo(5) == 8);
    }
}



//
// While
//

TEST_CASE("While", "[gen][while]") {
    auto jit = gen(R"SRC(
        module __while__;
        cumul(i : int) : int {
            r : int;
            r = 0;
            while(i>0) {
                r += i;
                i = i - 1;
            }
            return r;
        }
        )SRC");
    REQUIRE(jit);

    SECTION("while simple statement") {
        auto cumul = jit->lookup_symbol<int(*)(int)>("cumul");
        REQUIRE(cumul != nullptr);
        REQUIRE(cumul(0) == 0);
        REQUIRE(cumul(1) == 1);
        REQUIRE(cumul(2) == 3);
        REQUIRE(cumul(3) == 6);
        REQUIRE(cumul(4) == 10);
        REQUIRE(cumul(5) == 15);
    }

}


//
// For
//

TEST_CASE("For", "[gen][for]") {
    auto jit = gen(R"SRC(
        module __for__;
        sum(i : short) : int {
            r : int;
            r = 0;
            for(n: short = 0; n<i; n+=1) {
                r += n;
            }
            return r;
        }
        )SRC");
    REQUIRE(jit);

    SECTION("for simple statement") {
        auto sum = jit->lookup_symbol<int(*)(short)>("sum");
        REQUIRE(sum != nullptr);
        REQUIRE(sum(0) == 0);
        REQUIRE(sum(1) == 0);
        REQUIRE(sum(2) == 1);
        REQUIRE(sum(3) == 3);
        REQUIRE(sum(4) == 6);
        REQUIRE(sum(5) == 10);
    }

}

//
// Pointer, addresses and value-of
//

TEST_CASE("Pointers", "[gen][pointers]") {
    auto jit = gen(R"SRC(
        module __pointers__;
        a : int;
        b : int;

        init() {
            a = 4;
            b = 5;
        }

        assign(i: int, j: int) : int {
            p : int*;
            if(i<j) {
                p = &a;
            } else {
                p = &b;
            }
            *p += i + j;
            return *p;
        }
        )SRC");
    REQUIRE(jit);

    auto init = jit.get()->lookup_symbol < void(*)() > ("init");
    REQUIRE(init != nullptr);
    init();

    auto assign = jit.get()->lookup_symbol< int(*)(int, int) >("assign");
    REQUIRE(assign != nullptr);
    REQUIRE(assign(1, 2) == 7);
    REQUIRE(assign(2, 1) == 8);

}