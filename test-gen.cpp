//
// Created by Emilien Kia <emilien.kia+dev@gmail.com>.
//

#include <catch2/catch.hpp>

#include "parser.hpp"
#include "ast_dump.hpp"
#include "unit.hpp"
#include "ast_unit_visitor.hpp"
#include "unit_dump.hpp"
#include "symbol_type_resolver.hpp"
#include "unit_llvm_ir_gen.hpp"


std::unique_ptr<k::unit::gen::unit_llvm_jit> gen(std::string_view src, bool dump = false) {
    k::parse::parser parser(src);
    k::parse::ast::unit ast_unit = parser.parse_unit();

    if(dump) {
        k::parse::dump::ast_dump_visitor visit(std::cout);
        std::cout << "#" << std::endl << "# Parsing" << std::endl << "#" << std::endl;
        visit.visit_unit(ast_unit);
    }

    k::unit::unit unit;
    k::parse::ast_unit_visitor::visit(ast_unit, unit);

    if(dump) {
        k::unit::dump::unit_dump unit_dump(std::cout);
        std::cout << "#" << std::endl << "# Unit construction" << std::endl << "#" << std::endl;
        unit_dump.dump(unit);
    }

    k::unit::symbol_type_resolver var_resolver(unit);
    var_resolver.resolve();

    if(dump) {
        k::unit::dump::unit_dump unit_dump(std::cout);
        std::cout << "#" << std::endl << "# Variable resolution" << std::endl << "#" << std::endl;
        unit_dump.dump(unit);
    }

    k::unit::gen::unit_llvm_ir_gen gen(unit);
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
        )SRC", true);
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
}

TEST_CASE("Boolean arithmetic", "[gen][bool][arithmetic]") {
    auto jit = gen(R"SRC(
        module __bool__;
        ret_true() : bool {
            return true;
        }
        ret_false() : bool {
            return false;
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
}


