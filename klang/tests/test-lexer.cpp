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

#include <fmt/core.h>
#include <fmt/format.h>
#include <fmt/args.h>

#include "../src/lex/lexer.hpp"

#include "helpers.hpp"

using namespace k::lex;
using namespace k::log;

TEST_CASE( "Lex empty source", "[lexer]" ) {
    test_logger log;
    lexer lex(log);
    k::source src{""};
    auto lexemes = lex.parse(src);
    REQUIRE( lexemes.empty() );
}

TEST_CASE( "Lex one identifier", "[lexer]" ) {
    test_logger log;
    lexer lex(log);

    SECTION("Lex char-only identifier") {
        k::source src{"toto"};
        auto lexemes = lex.parse(src);
        REQUIRE( lexemes.size() == 1 );

        any_lexeme the_lexeme = lexemes[0];
        REQUIRE( std::holds_alternative<identifier>(the_lexeme) );

        identifier l = std::get<identifier>(the_lexeme);
        REQUIRE( l.content == "toto" );

    }

    SECTION("Lex char-and-digit identifier") {
        k::source src{"to42to"};
        auto lexemes = lex.parse(src);
        REQUIRE( lexemes.size() == 1 );

        any_lexeme the_lexeme = lexemes[0];
        REQUIRE( std::holds_alternative<identifier>(the_lexeme) );

        identifier l = std::get<identifier>(the_lexeme);
        REQUIRE( l.content == "to42to" );

    }
}

TEST_CASE( "Lex one keyword", "[lexer]" ) {
    test_logger log;
    lexer lex(log);
/*
    SECTION("Lex true keyword") {
        k::source src{"true "};
        auto lexemes = lex.parse(src);
        REQUIRE( lexemes.size() == 1 );

        any_lexeme the_lexeme = lexemes[0];
        REQUIRE( std::holds_alternative<keyword>(the_lexeme) );

        keyword l = std::get<keyword>(the_lexeme);
        REQUIRE( l.content == "true" );
    }

    SECTION("Lex false keyword") {
        k::source src{"false "};
        auto lexemes = lex.parse(src);
        REQUIRE( lexemes.size() == 1 );

        any_lexeme the_lexeme = lexemes[0];
        REQUIRE( std::holds_alternative<keyword>(the_lexeme) );

        keyword l = std::get<keyword>(the_lexeme);
        REQUIRE( l.content == "false" );
    }

    SECTION("Lex null keyword") {
        k::source src{"null "};
        auto lexemes = lex.parse(src);
        REQUIRE( lexemes.size() == 1 );

        any_lexeme the_lexeme = lexemes[0];
        REQUIRE( std::holds_alternative<keyword>(the_lexeme) );

        keyword l = std::get<keyword>(the_lexeme);
        REQUIRE( l.content == "null" );
    }
    */
}

TEST_CASE( "Lex one integer", "[lexer][integer]" ) {
    // TODO Add lexing and tests for l64, l128 suffices
    // TODO Add lexing and tests for bi suffices
    // TODO Add lexing, tests and spec for i8, i16, i32, i64, i128, u8, u16, u32, u64 and u128 suffices
    test_logger log;
    lexer lex(log);

    SECTION("Lex decimal", "[decimal]") {

        SECTION("Lex decimal 0 integer") {
            k::source src{"0"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "0");
            REQUIRE(l.int_content() == "0");
            REQUIRE(l.base == numeric_base::DECIMAL);
            REQUIRE(!l.unsigned_num);
            REQUIRE(l.size == integer_size::INT);
        }

        SECTION("Lex decimal 0 unsigned integer", "[unsigned]") {
            k::source src{"0u"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "0u");
            REQUIRE(l.base == numeric_base::DECIMAL);
            REQUIRE(l.unsigned_num);
            REQUIRE(l.size == integer_size::INT);
        }

        SECTION("Lex decimal 0 explicit integer") {
            k::source src{"0i"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "0i");
            REQUIRE(l.int_content() == "0");
            REQUIRE(l.base == numeric_base::DECIMAL);
            REQUIRE(!l.unsigned_num);
            REQUIRE(l.size == integer_size::INT);
        }

        SECTION("Lex decimal 0 unsigned explicit integer", "[unsigned]") {
            k::source src{"0ui"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "0ui");
            REQUIRE(l.int_content() == "0");
            REQUIRE(l.base == numeric_base::DECIMAL);
            REQUIRE(l.unsigned_num);
            REQUIRE(l.size == integer_size::INT);
        }

        SECTION("Lex decimal 0 short integer", "[short]") {
            k::source src{"0s"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "0s");
            REQUIRE(l.int_content() == "0");
            REQUIRE(l.base == numeric_base::DECIMAL);
            REQUIRE(!l.unsigned_num);
            REQUIRE(l.size == integer_size::SHORT);
        }

        SECTION("Lex decimal 0 unsigned short integer", "[unsigned][short]") {
            k::source src{"0us"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "0us");
            REQUIRE(l.int_content() == "0");
            REQUIRE(l.base == numeric_base::DECIMAL);
            REQUIRE(l.unsigned_num);
            REQUIRE(l.size == integer_size::SHORT);
        }

        SECTION("Lex decimal 0 long integer", "[long]") {
            k::source src{"0l"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "0l");
            REQUIRE(l.int_content() == "0");
            REQUIRE(l.base == numeric_base::DECIMAL);
            REQUIRE(!l.unsigned_num);
            REQUIRE(l.size == integer_size::LONG);
        }

        SECTION("Lex decimal 0 unsigned long integer", "[unsigned][long]") {
            k::source src{"0ul"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "0ul");
            REQUIRE(l.base == numeric_base::DECIMAL);
            REQUIRE(l.unsigned_num);
            REQUIRE(l.size == integer_size::LONG);
        }

        SECTION("Lex decimal 0 longlong integer", "[longlong]") {
            k::source src{"0ll"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "0ll");
            REQUIRE(l.int_content() == "0");
            REQUIRE(l.base == numeric_base::DECIMAL);
            REQUIRE(!l.unsigned_num);
            REQUIRE(l.size == integer_size::LONGLONG);
        }

        SECTION("Lex decimal 0 unsigned longlong integer", "[unsigned][longlong]") {
            k::source src{"0ull"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "0ull");
            REQUIRE(l.int_content() == "0");
            REQUIRE(l.base == numeric_base::DECIMAL);
            REQUIRE(l.unsigned_num);
            REQUIRE(l.size == integer_size::LONGLONG);
        }

        SECTION("Lex decimal 1 integer") {
            k::source src{"1"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "1");
            REQUIRE(l.int_content() == "1");
            REQUIRE(l.base == numeric_base::DECIMAL);
            REQUIRE(!l.unsigned_num);
            REQUIRE(l.size == integer_size::INT);
        }

        SECTION("Lex decimal 1 unsigned integer", "[unsigned]") {
            k::source src{"1u"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "1u");
            REQUIRE(l.int_content() == "1");
            REQUIRE(l.base == numeric_base::DECIMAL);
            REQUIRE(l.unsigned_num);
            REQUIRE(l.size == integer_size::INT);
        }

        SECTION("Lex decimal 1 explicit integer") {
            k::source src{"1i"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "1i");
            REQUIRE(l.int_content() == "1");
            REQUIRE(l.base == numeric_base::DECIMAL);
            REQUIRE(!l.unsigned_num);
            REQUIRE(l.size == integer_size::INT);
        }

        SECTION("Lex decimal 1 unsigned explicit integer", "[unsigned]") {
            k::source src{"1ui"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "1ui");
            REQUIRE(l.int_content() == "1");
            REQUIRE(l.base == numeric_base::DECIMAL);
            REQUIRE(l.unsigned_num);
            REQUIRE(l.size == integer_size::INT);
        }

        SECTION("Lex decimal 1 short integer", "[short]") {
            k::source src{"1s"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "1s");
            REQUIRE(l.int_content() == "1");
            REQUIRE(l.base == numeric_base::DECIMAL);
            REQUIRE(!l.unsigned_num);
            REQUIRE(l.size == integer_size::SHORT);
        }

        SECTION("Lex decimal 1 unsigned short integer", "[unsigned][short]") {
            k::source src{"1us"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "1us");
            REQUIRE(l.int_content() == "1");
            REQUIRE(l.base == numeric_base::DECIMAL);
            REQUIRE(l.unsigned_num);
            REQUIRE(l.size == integer_size::SHORT);
        }

        SECTION("Lex decimal 1 long integer", "[long]") {
            k::source src{"1l"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "1l");
            REQUIRE(l.int_content() == "1");
            REQUIRE(l.base == numeric_base::DECIMAL);
            REQUIRE(!l.unsigned_num);
            REQUIRE(l.size == integer_size::LONG);
        }

        SECTION("Lex decimal 1 unsigned long integer", "[unsigned][long]") {
            k::source src{"1ul"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "1ul");
            REQUIRE(l.int_content() == "1");
            REQUIRE(l.base == numeric_base::DECIMAL);
            REQUIRE(l.unsigned_num);
            REQUIRE(l.size == integer_size::LONG);
        }

        SECTION("Lex decimal 1 longlong integer", "[longlong]") {
            k::source src{"1ll"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "1ll");
            REQUIRE(l.int_content() == "1");
            REQUIRE(l.base == numeric_base::DECIMAL);
            REQUIRE(!l.unsigned_num);
            REQUIRE(l.size == integer_size::LONGLONG);
        }

        SECTION("Lex decimal 1 unsigned longlong integer", "[unsigned][longlong]") {
            k::source src{"1ull"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "1ull");
            REQUIRE(l.int_content() == "1");
            REQUIRE(l.base == numeric_base::DECIMAL);
            REQUIRE(l.unsigned_num);
            REQUIRE(l.size == integer_size::LONGLONG);
        }

        SECTION("Lex decimal integer") {
            k::source src{"123"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "123");
            REQUIRE(l.int_content() == "123");
            REQUIRE(l.base == numeric_base::DECIMAL);
            REQUIRE(!l.unsigned_num);
            REQUIRE(l.size == integer_size::INT);
        }

        SECTION("Lex decimal unsigned integer", "[unsigned]") {
            k::source src{"123u"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "123u");
            REQUIRE(l.int_content() == "123");
            REQUIRE(l.base == numeric_base::DECIMAL);
            REQUIRE(l.unsigned_num);
            REQUIRE(l.size == integer_size::INT);
        }

        SECTION("Lex decimal explicit integer") {
            k::source src{"123i"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "123i");
            REQUIRE(l.int_content() == "123");
            REQUIRE(l.base == numeric_base::DECIMAL);
            REQUIRE(!l.unsigned_num);
            REQUIRE(l.size == integer_size::INT);
        }

        SECTION("Lex decimal unsigned explicit integer", "[unsigned]") {
            k::source src{"123ui"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "123ui");
            REQUIRE(l.int_content() == "123");
            REQUIRE(l.base == numeric_base::DECIMAL);
            REQUIRE(l.unsigned_num);
            REQUIRE(l.size == integer_size::INT);
        }

        SECTION("Lex decimal short integer", "[short]") {
            k::source src{"123s"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "123s");
            REQUIRE(l.int_content() == "123");
            REQUIRE(l.base == numeric_base::DECIMAL);
            REQUIRE(!l.unsigned_num);
            REQUIRE(l.size == integer_size::SHORT);
        }

        SECTION("Lex decimal unsigned short integer", "[unsigned][short]") {
            k::source src{"123us"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "123us");
            REQUIRE(l.int_content() == "123");
            REQUIRE(l.base == numeric_base::DECIMAL);
            REQUIRE(l.unsigned_num);
            REQUIRE(l.size == integer_size::SHORT);
        }

        SECTION("Lex decimal long integer", "[long]") {
            k::source src{"123l"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "123l");
            REQUIRE(l.int_content() == "123");
            REQUIRE(l.base == numeric_base::DECIMAL);
            REQUIRE(!l.unsigned_num);
            REQUIRE(l.size == integer_size::LONG);
        }

        SECTION("Lex decimal unsigned long integer", "[unsigned][long]") {
            k::source src{"123ul"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "123ul");
            REQUIRE(l.int_content() == "123");
            REQUIRE(l.base == numeric_base::DECIMAL);
            REQUIRE(l.unsigned_num);
            REQUIRE(l.size == integer_size::LONG);
        }

        SECTION("Lex decimal longlong integer", "[longlong]") {
            k::source src{"123ll"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "123ll");
            REQUIRE(l.int_content() == "123");
            REQUIRE(l.base == numeric_base::DECIMAL);
            REQUIRE(!l.unsigned_num);
            REQUIRE(l.size == integer_size::LONGLONG);
        }

        SECTION("Lex decimal unsigned longqlong integer", "[unsigned][longlong]") {
            k::source src{"123ull"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "123ull");
            REQUIRE(l.int_content() == "123");
            REQUIRE(l.base == numeric_base::DECIMAL);
            REQUIRE(l.unsigned_num);
            REQUIRE(l.size == integer_size::LONGLONG);
        }
    }

    SECTION("Lex hexadecimal", "[hexadecimal]") {

        SECTION("Lex hexadecimal identifier") {
            k::source src{"0x123def"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "0x123def");
            REQUIRE(l.int_content() == "123def");
            REQUIRE(l.base == numeric_base::HEXADECIMAL);
            REQUIRE(!l.unsigned_num);
            REQUIRE(l.size == integer_size::INT);
        }

        SECTION("Lex hexadecimal unsigned identifier", "[unsigned]") {
            k::source src{"0x123defu"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "0x123defu");
            REQUIRE(l.int_content() == "123def");
            REQUIRE(l.base == numeric_base::HEXADECIMAL);
            REQUIRE(l.unsigned_num);
            REQUIRE(l.size == integer_size::INT);
        }

        SECTION("Lex hexadecimal explicit identifier") {
            k::source src{"0x123defi"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "0x123defi");
            REQUIRE(l.int_content() == "123def");
            REQUIRE(l.base == numeric_base::HEXADECIMAL);
            REQUIRE(!l.unsigned_num);
            REQUIRE(l.size == integer_size::INT);
        }

        SECTION("Lex hexadecimal unsigned explicit identifier", "[unsigned]") {
            k::source src{"0x123defui"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "0x123defui");
            REQUIRE(l.int_content() == "123def");
            REQUIRE(l.base == numeric_base::HEXADECIMAL);
            REQUIRE(l.unsigned_num);
            REQUIRE(l.size == integer_size::INT);
        }

        SECTION("Lex hexadecimal short identifier", "[short]") {
            k::source src{"0x123defs"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "0x123defs");
            REQUIRE(l.int_content() == "123def");
            REQUIRE(l.base == numeric_base::HEXADECIMAL);
            REQUIRE(!l.unsigned_num);
            REQUIRE(l.size == integer_size::SHORT);
        }

        SECTION("Lex hexadecimal unsigned short identifier", "[unsigned][short]") {
            k::source src{"0x123defus"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "0x123defus");
            REQUIRE(l.int_content() == "123def");
            REQUIRE(l.base == numeric_base::HEXADECIMAL);
            REQUIRE(l.unsigned_num);
            REQUIRE(l.size == integer_size::SHORT);
        }

        SECTION("Lex hexadecimal long identifier", "[long]") {
            k::source src{"0x123defl"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "0x123defl");
            REQUIRE(l.int_content() == "123def");
            REQUIRE(l.base == numeric_base::HEXADECIMAL);
            REQUIRE(!l.unsigned_num);
            REQUIRE(l.size == integer_size::LONG);
        }

        SECTION("Lex hexadecimal unsigned long identifier", "[unsigned][long]") {
            k::source src{"0x123deful"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "0x123deful");
            REQUIRE(l.int_content() == "123def");
            REQUIRE(l.base == numeric_base::HEXADECIMAL);
            REQUIRE(l.unsigned_num);
            REQUIRE(l.size == integer_size::LONG);
        }

        SECTION("Lex hexadecimal longlong identifier", "[longlong]") {
            k::source src{"0x123defll"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "0x123defll");
            REQUIRE(l.int_content() == "123def");
            REQUIRE(l.base == numeric_base::HEXADECIMAL);
            REQUIRE(!l.unsigned_num);
            REQUIRE(l.size == integer_size::LONGLONG);
        }

        SECTION("Lex hexadecimal unsigned longlong identifier", "[unsigned][longlong]") {
            k::source src{"0x123defull"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "0x123defull");
            REQUIRE(l.int_content() == "123def");
            REQUIRE(l.base == numeric_base::HEXADECIMAL);
            REQUIRE(l.unsigned_num);
            REQUIRE(l.size == integer_size::LONGLONG);
        }
    }

    SECTION("Lex octal", "[hexadecimal]") {

        SECTION("Lex octal identifier") {
            k::source src{"0123"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "0123");
            REQUIRE(l.int_content() == "123");
            REQUIRE(l.base == numeric_base::OCTAL);
            REQUIRE(!l.unsigned_num);
            REQUIRE(l.size == integer_size::INT);
        }

        SECTION("Lex octal unsigned identifier", "[unsigned]") {
            k::source src{"0123u"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "0123u");
            REQUIRE(l.int_content() == "123");
            REQUIRE(l.base == numeric_base::OCTAL);
            REQUIRE(l.unsigned_num);
            REQUIRE(l.size == integer_size::INT);
        }

        SECTION("Lex octal explicit identifier") {
            k::source src{"0123i"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "0123i");
            REQUIRE(l.int_content() == "123");
            REQUIRE(l.base == numeric_base::OCTAL);
            REQUIRE(!l.unsigned_num);
            REQUIRE(l.size == integer_size::INT);
        }

        SECTION("Lex octal unsigned explicit identifier", "[unsigned]") {
            k::source src{"0123ui"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "0123ui");
            REQUIRE(l.int_content() == "123");
            REQUIRE(l.base == numeric_base::OCTAL);
            REQUIRE(l.unsigned_num);
            REQUIRE(l.size == integer_size::INT);
        }

        SECTION("Lex octal short identifier", "[short]") {
            k::source src{"0123s"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "0123s");
            REQUIRE(l.int_content() == "123");
            REQUIRE(l.base == numeric_base::OCTAL);
            REQUIRE(!l.unsigned_num);
            REQUIRE(l.size == integer_size::SHORT);
        }

        SECTION("Lex octal unsigned short identifier", "[unsigned][short]") {
            k::source src{"0123us"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "0123us");
            REQUIRE(l.int_content() == "123");
            REQUIRE(l.base == numeric_base::OCTAL);
            REQUIRE(l.unsigned_num);
            REQUIRE(l.size == integer_size::SHORT);
        }

        SECTION("Lex octal long identifier", "[long]") {
            k::source src{"0123l"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "0123l");
            REQUIRE(l.int_content() == "123");
            REQUIRE(l.base == numeric_base::OCTAL);
            REQUIRE(!l.unsigned_num);
            REQUIRE(l.size == integer_size::LONG);
        }

        SECTION("Lex octal unsigned long identifier", "[unsigned][long]") {
            k::source src{"0123ul"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "0123ul");
            REQUIRE(l.int_content() == "123");
            REQUIRE(l.base == numeric_base::OCTAL);
            REQUIRE(l.unsigned_num);
            REQUIRE(l.size == integer_size::LONG);
        }

        SECTION("Lex octal longlong identifier", "[longlong]") {
            k::source src{"0123ll"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "0123ll");
            REQUIRE(l.int_content() == "123");
            REQUIRE(l.base == numeric_base::OCTAL);
            REQUIRE(!l.unsigned_num);
            REQUIRE(l.size == integer_size::LONGLONG);
        }

        SECTION("Lex octal unsigned longlong identifier", "[unsigned][longlong]") {
            k::source src{"0123ull"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "0123ull");
            REQUIRE(l.int_content() == "123");
            REQUIRE(l.base == numeric_base::OCTAL);
            REQUIRE(l.unsigned_num);
            REQUIRE(l.size == integer_size::LONGLONG);
        }

        SECTION("Lex octal identifier with long prefix") {
            k::source src{"0o123"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "0o123");
            REQUIRE(l.int_content() == "123");
            REQUIRE(l.base == numeric_base::OCTAL);
            REQUIRE(!l.unsigned_num);
            REQUIRE(l.size == integer_size::INT);
        }

        SECTION("Lex octal unsigned identifier with long prefix", "[unsigned]") {
            k::source src{"0o123u"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "0o123u");
            REQUIRE(l.int_content() == "123");
            REQUIRE(l.base == numeric_base::OCTAL);
            REQUIRE(l.unsigned_num);
            REQUIRE(l.size == integer_size::INT);
        }

        SECTION("Lex octal short identifier with long prefix", "[short]") {
            k::source src{"0o123s"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "0o123s");
            REQUIRE(l.int_content() == "123");
            REQUIRE(l.base == numeric_base::OCTAL);
            REQUIRE(!l.unsigned_num);
            REQUIRE(l.size == integer_size::SHORT);
        }

        SECTION("Lex octal unsigned short identifier with long prefix", "[unsigned][short]") {
            k::source src{"0o123us"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "0o123us");
            REQUIRE(l.int_content() == "123");
            REQUIRE(l.base == numeric_base::OCTAL);
            REQUIRE(l.unsigned_num);
            REQUIRE(l.size == integer_size::SHORT);
        }

        SECTION("Lex octal long identifier with long prefix", "[long]") {
            k::source src{"0o123l"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "0o123l");
            REQUIRE(l.int_content() == "123");
            REQUIRE(l.base == numeric_base::OCTAL);
            REQUIRE(!l.unsigned_num);
            REQUIRE(l.size == integer_size::LONG);
        }

        SECTION("Lex octal unsigned long identifier with long prefix", "[unsigned][long]") {
            k::source src{"0o123ul"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "0o123ul");
            REQUIRE(l.int_content() == "123");
            REQUIRE(l.base == numeric_base::OCTAL);
            REQUIRE(l.unsigned_num);
            REQUIRE(l.size == integer_size::LONG);
        }

        SECTION("Lex octal longlong identifier with long prefix", "[longlong]") {
            k::source src{"0o123ll"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "0o123ll");
            REQUIRE(l.int_content() == "123");
            REQUIRE(l.base == numeric_base::OCTAL);
            REQUIRE(!l.unsigned_num);
            REQUIRE(l.size == integer_size::LONGLONG);
        }

        SECTION("Lex octal unsigned longlong identifier with long prefix", "[unsigned][longlong]") {
            k::source src{"0o123ull"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "0o123ull");
            REQUIRE(l.int_content() == "123");
            REQUIRE(l.base == numeric_base::OCTAL);
            REQUIRE(l.unsigned_num);
            REQUIRE(l.size == integer_size::LONGLONG);
        }
    }

    SECTION("Lex binary", "[binary]") {

        SECTION("Lex binary identifier") {
            k::source src{"0b1010"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "0b1010");
            REQUIRE(l.int_content() == "1010");
            REQUIRE(l.base == numeric_base::BINARY);
            REQUIRE(!l.unsigned_num);
            REQUIRE(l.size == integer_size::INT);
        }

        SECTION("Lex binary unsigned identifier", "[unsigned]") {
            k::source src{"0b1010u"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "0b1010u");
            REQUIRE(l.int_content() == "1010");
            REQUIRE(l.base == numeric_base::BINARY);
            REQUIRE(l.unsigned_num);
            REQUIRE(l.size == integer_size::INT);
        }

        SECTION("Lex binary explicit identifier") {
            k::source src{"0b1010i"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "0b1010i");
            REQUIRE(l.int_content() == "1010");
            REQUIRE(l.base == numeric_base::BINARY);
            REQUIRE(!l.unsigned_num);
            REQUIRE(l.size == integer_size::INT);
        }

        SECTION("Lex binary unsigned explicit identifier", "[unsigned]") {
            k::source src{"0b1010ui"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "0b1010ui");
            REQUIRE(l.int_content() == "1010");
            REQUIRE(l.base == numeric_base::BINARY);
            REQUIRE(l.unsigned_num);
            REQUIRE(l.size == integer_size::INT);
        }

        SECTION("Lex binary short identifier", "[short]") {
            k::source src{"0b1010s"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "0b1010s");
            REQUIRE(l.int_content() == "1010");
            REQUIRE(l.base == numeric_base::BINARY);
            REQUIRE(!l.unsigned_num);
            REQUIRE(l.size == integer_size::SHORT);
        }

        SECTION("Lex binary unsigned short identifier", "[unsigned][short]") {
            k::source src{"0b1010us"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "0b1010us");
            REQUIRE(l.int_content() == "1010");
            REQUIRE(l.base == numeric_base::BINARY);
            REQUIRE(l.unsigned_num);
            REQUIRE(l.size == integer_size::SHORT);
        }

        SECTION("Lex binary long identifier", "[long]") {
            k::source src{"0b1010l"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "0b1010l");
            REQUIRE(l.int_content() == "1010");
            REQUIRE(l.base == numeric_base::BINARY);
            REQUIRE(!l.unsigned_num);
            REQUIRE(l.size == integer_size::LONG);
        }

        SECTION("Lex binary unsigned long identifier", "[unsigned][long]") {
            k::source src{"0b1010ul"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "0b1010ul");
            REQUIRE(l.int_content() == "1010");
            REQUIRE(l.base == numeric_base::BINARY);
            REQUIRE(l.unsigned_num);
            REQUIRE(l.size == integer_size::LONG);
        }

        SECTION("Lex binary longlong identifier", "[longlong]") {
            k::source src{"0b1010ll"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "0b1010ll");
            REQUIRE(l.int_content() == "1010");
            REQUIRE(l.base == numeric_base::BINARY);
            REQUIRE(!l.unsigned_num);
            REQUIRE(l.size == integer_size::LONGLONG);
        }

        SECTION("Lex binary unsigned longlong identifier", "[unsigned][longlongggggggg]") {
            k::source src{"0b1010ull"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);

            any_lexeme the_lexeme = lexemes[0];
            REQUIRE(std::holds_alternative<integer>(the_lexeme));

            integer l = std::get<integer>(the_lexeme);
            REQUIRE(l.content == "0b1010ull");
            REQUIRE(l.int_content() == "1010");
            REQUIRE(l.base == numeric_base::BINARY);
            REQUIRE(l.unsigned_num);
            REQUIRE(l.size == integer_size::LONGLONG);
        }
    }
}

TEST_CASE( "Lex one float", "[lexer][float]" ) {
    test_logger log;
    lexer lex(log);

    SECTION("Lex implicit float") {

        SECTION("Lex float 123.45e8") {
            k::source src{"123.45e8"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);
            REQUIRE(std::holds_alternative<float_num>(lexemes[0]));
            float_num l = std::get<float_num>(lexemes[0]);
            REQUIRE(l.content == "123.45e8");
            REQUIRE(l.size == FLOAT);
        }

        SECTION("Lex float 123e8") {
            k::source src{"123e8"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);
            REQUIRE(std::holds_alternative<float_num>(lexemes[0]));
            float_num l = std::get<float_num>(lexemes[0]);
            REQUIRE(l.content == "123e8");
            REQUIRE(l.size == FLOAT);
        }

        SECTION("Lex float 123e-8") {
            k::source src{"123e-8"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);
            REQUIRE(std::holds_alternative<float_num>(lexemes[0]));
            float_num l = std::get<float_num>(lexemes[0]);
            REQUIRE(l.content == "123e-8");
            REQUIRE(l.size == FLOAT);
        }

        SECTION("Lex float .45e8") {
            k::source src{".45e8"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);
            REQUIRE(std::holds_alternative<float_num>(lexemes[0]));
            float_num l = std::get<float_num>(lexemes[0]);
            REQUIRE(l.content == ".45e8");
            REQUIRE(l.size == FLOAT);
        }

        SECTION("Lex float 123.45") {
            k::source src{"123.45"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);
            REQUIRE(std::holds_alternative<float_num>(lexemes[0]));
            float_num l = std::get<float_num>(lexemes[0]);
            REQUIRE(l.content == "123.45");
            REQUIRE(l.size == FLOAT);
        }

        SECTION("Lex float .45") {
            k::source src{".45"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);
            REQUIRE(std::holds_alternative<float_num>(lexemes[0]));
            float_num l = std::get<float_num>(lexemes[0]);
            REQUIRE(l.content == ".45");
            REQUIRE(l.size == FLOAT);
        }

    }

    SECTION("Lex explicit float") {

        SECTION("Lex float 123.45e8f") {
            k::source src{"123.45e8f"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);
            REQUIRE(std::holds_alternative<float_num>(lexemes[0]));
            float_num l = std::get<float_num>(lexemes[0]);
            REQUIRE(l.content == "123.45e8f");
            REQUIRE(l.size == FLOAT);
        }

        SECTION("Lex float 123e8f") {
            k::source src{"123e8f"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);
            REQUIRE(std::holds_alternative<float_num>(lexemes[0]));
            float_num l = std::get<float_num>(lexemes[0]);
            REQUIRE(l.content == "123e8f");
            REQUIRE(l.size == FLOAT);
        }

        SECTION("Lex float 123e-8f") {
            k::source src{"123e-8f"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);
            REQUIRE(std::holds_alternative<float_num>(lexemes[0]));
            float_num l = std::get<float_num>(lexemes[0]);
            REQUIRE(l.content == "123e-8f");
            REQUIRE(l.size == FLOAT);
        }

        SECTION("Lex float .45e8f") {
            k::source src{".45e8f"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);
            REQUIRE(std::holds_alternative<float_num>(lexemes[0]));
            float_num l = std::get<float_num>(lexemes[0]);
            REQUIRE(l.content == ".45e8f");
            REQUIRE(l.size == FLOAT);
        }

        SECTION("Lex float 123.45f") {
            k::source src{"123.45f"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);
            REQUIRE(std::holds_alternative<float_num>(lexemes[0]));
            float_num l = std::get<float_num>(lexemes[0]);
            REQUIRE(l.content == "123.45f");
            REQUIRE(l.size == FLOAT);
        }

        SECTION("Lex float .45f") {
            k::source src{".45f"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);
            REQUIRE(std::holds_alternative<float_num>(lexemes[0]));
            float_num l = std::get<float_num>(lexemes[0]);
            REQUIRE(l.content == ".45f");
            REQUIRE(l.size == FLOAT);
        }

    }

    SECTION("Lex explicit double") {

        SECTION("Lex double 123.45e8d") {
            k::source src{"123.45e8d"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);
            REQUIRE(std::holds_alternative<float_num>(lexemes[0]));
            float_num l = std::get<float_num>(lexemes[0]);
            REQUIRE(l.content == "123.45e8d");
            REQUIRE(l.size == DOUBLE);
        }

        SECTION("Lex double 123e8d") {
            k::source src{"123e8d"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);
            REQUIRE(std::holds_alternative<float_num>(lexemes[0]));
            float_num l = std::get<float_num>(lexemes[0]);
            REQUIRE(l.content == "123e8d");
            REQUIRE(l.size == DOUBLE);
        }

        SECTION("Lex double 123e-8d") {
            k::source src{"123e-8d"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);
            REQUIRE(std::holds_alternative<float_num>(lexemes[0]));
            float_num l = std::get<float_num>(lexemes[0]);
            REQUIRE(l.content == "123e-8d");
            REQUIRE(l.size == DOUBLE);
        }

        SECTION("Lex double .45e8d") {
            k::source src{".45e8d"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);
            REQUIRE(std::holds_alternative<float_num>(lexemes[0]));
            float_num l = std::get<float_num>(lexemes[0]);
            REQUIRE(l.content == ".45e8d");
            REQUIRE(l.size == DOUBLE);
        }

        SECTION("Lex double 123.45d") {
            k::source src{"123.45d"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);
            REQUIRE(std::holds_alternative<float_num>(lexemes[0]));
            float_num l = std::get<float_num>(lexemes[0]);
            REQUIRE(l.content == "123.45d");
            REQUIRE(l.size == DOUBLE);
        }

        SECTION("Lex double .45d") {
            k::source src{".45d"};
            auto lexemes = lex.parse(src);
            REQUIRE(lexemes.size() == 1);
            REQUIRE(std::holds_alternative<float_num>(lexemes[0]));
            float_num l = std::get<float_num>(lexemes[0]);
            REQUIRE(l.content == ".45d");
            REQUIRE(l.size == DOUBLE);
        }

    }

    // TODO
}

TEST_CASE( "Lex one char", "[lexer]" ) {
    test_logger log;
    lexer lex(log);

    SECTION("Lex char char") {
        k::source src{"'c'"};
        auto lexemes = lex.parse(src);
        REQUIRE( lexemes.size() == 1 );

        any_lexeme the_lexeme = lexemes[0];
        REQUIRE( std::holds_alternative<character>(the_lexeme) );

        character l = std::get<character>(the_lexeme);
        REQUIRE( l.content == "'c'" );

        k::value_type val = l.value();
        REQUIRE( std::holds_alternative<char>(val) );
        REQUIRE( std::get<char>(val) == 'c' );
    }

    SECTION("Lex digit char") {
        k::source src{"'0'"};
        auto lexemes = lex.parse(src);
        REQUIRE( lexemes.size() == 1 );

        any_lexeme the_lexeme = lexemes[0];
        REQUIRE( std::holds_alternative<character>(the_lexeme) );

        character l = std::get<character>(the_lexeme);
        REQUIRE( l.content == "'0'" );

        k::value_type val = l.value();
        REQUIRE( std::holds_alternative<char>(val) );
        REQUIRE( std::get<char>(val) == '0' );
    }

    SECTION("Lex special char") {
        k::source src{"'&'"};
        auto lexemes = lex.parse(src);
        REQUIRE( lexemes.size() == 1 );

        any_lexeme the_lexeme = lexemes[0];
        REQUIRE( std::holds_alternative<character>(the_lexeme) );

        character l = std::get<character>(the_lexeme);
        REQUIRE( l.content == "'&'" );

        k::value_type val = l.value();
        REQUIRE( std::holds_alternative<char>(val) );
        REQUIRE( std::get<char>(val) == '&' );
    }

    SECTION("Lex anti-slash escape char") {
        k::source src{"'\\\\'"};
        auto lexemes = lex.parse(src);
        REQUIRE( lexemes.size() == 1 );

        any_lexeme the_lexeme = lexemes[0];
        REQUIRE( std::holds_alternative<character>(the_lexeme) );

        character l = std::get<character>(the_lexeme);
        REQUIRE( l.content == "'\\\\'" );

        k::value_type val = l.value();
        REQUIRE( std::holds_alternative<char>(val) );
        // TODO add escape decoding
        // REQUIRE( std::get<char>(val) == '\\' );
    }

    SECTION("Lex simple quote escape char") {
        k::source src{"'\\\''"};
        auto lexemes = lex.parse(src);
        REQUIRE( lexemes.size() == 1 );

        any_lexeme the_lexeme = lexemes[0];
        REQUIRE( std::holds_alternative<character>(the_lexeme) );

        character l = std::get<character>(the_lexeme);
        REQUIRE( l.content == "'\\\''" );

        k::value_type val = l.value();
        REQUIRE( std::holds_alternative<char>(val) );
        // TODO add escape decoding
        // REQUIRE( std::get<char>(val) == '\'' );
    }

    // TODO add unicode escape tests
}

TEST_CASE( "Lex one string", "[lexer]" ) {
    test_logger log;
    lexer lex(log);

    SECTION("Lex string") {
        k::source src{"\"Hell0\\\' world \\\\ !\""};
        auto lexemes = lex.parse(src);
        REQUIRE( lexemes.size() == 1 );

        any_lexeme the_lexeme = lexemes[0];
        REQUIRE( std::holds_alternative<string>(the_lexeme) );

        string l = std::get<string>(the_lexeme);
        REQUIRE( l.content == "\"Hell0\\\' world \\\\ !\"" );

        k::value_type val = l.value();
        REQUIRE( std::holds_alternative<std::string>(val) );
        // TODO add escape decoding
        // REQUIRE( std::get<std::string>(val) == '\\' );
    }

    // TODO add unicode escape tests
}

TEST_CASE( "Lex one boolean", "[lexer]" ) {
    test_logger log;
    lexer lex(log);

    SECTION("Lex true boolean") {
        k::source src{"true"};
        auto lexemes = lex.parse(src);
        REQUIRE( lexemes.size() == 1 );

        any_lexeme the_lexeme = lexemes[0];
        REQUIRE( std::holds_alternative<boolean>(the_lexeme) );

        boolean l = std::get<boolean>(the_lexeme);
        REQUIRE( l.content == "true" );

        k::value_type val = l.value();
        REQUIRE( std::holds_alternative<bool>(val) );
        REQUIRE( std::get<bool>(val) == true );
    }

    SECTION("Lex false boolean") {
        k::source src{"false"};
        auto lexemes = lex.parse(src);
        REQUIRE( lexemes.size() == 1 );

        any_lexeme the_lexeme = lexemes[0];
        REQUIRE( std::holds_alternative<boolean>(the_lexeme) );

        boolean l = std::get<boolean>(the_lexeme);
        REQUIRE( l.content == "false" );

        k::value_type val = l.value();
        REQUIRE( std::holds_alternative<bool>(val) );
        REQUIRE( std::get<bool>(val) == false );
    }
}

TEST_CASE( "Lex null", "[lexer]" ) {
    test_logger log;
    lexer lex(log);

    SECTION("Lex one null") {
        k::source src{"null"};
        auto lexemes = lex.parse(src);
        REQUIRE( lexemes.size() == 1 );

        any_lexeme the_lexeme = lexemes[0];
        REQUIRE( std::holds_alternative<null>(the_lexeme) );

        null l = std::get<null>(the_lexeme);
        REQUIRE( l.content == "null" );

        k::value_type val = l.value();
        REQUIRE( std::holds_alternative<nullptr_t>(val) );
        REQUIRE( std::get<nullptr_t>(val) == nullptr );
    }
}

TEST_CASE( "Lex one comment", "[lexer]" ) {
    test_logger log;
    lexer lex(log);

    SECTION("Lex end-of-line comment") {
        k::source src{"// Hello my comment\n"};
        auto lexemes = lex.parse(src);
        REQUIRE( lexemes.size() == 1 );

        any_lexeme the_lexeme = lexemes[0];
        REQUIRE( std::holds_alternative<comment>(the_lexeme) );

        comment l = std::get<comment>(the_lexeme);
        REQUIRE( l.content == "// Hello my comment" );
    }

    SECTION("Lex end-of-line end-of-file comment") {
        k::source src{"// Hello my comment"};
        auto lexemes = lex.parse(src);
        REQUIRE( lexemes.size() == 1 );

        any_lexeme the_lexeme = lexemes[0];
        REQUIRE( std::holds_alternative<comment>(the_lexeme) );

        comment l = std::get<comment>(the_lexeme);
        REQUIRE( l.content == "// Hello my comment" );
    }

    SECTION("Lex multi-line comment") {
        k::source src{"/* Hello my\n comment*/"};
        auto lexemes = lex.parse(src);
        REQUIRE( lexemes.size() == 1 );

        any_lexeme the_lexeme = lexemes[0];
        REQUIRE( std::holds_alternative<comment>(the_lexeme) );

        comment l = std::get<comment>(the_lexeme);
        REQUIRE( l.content == "/* Hello my\n comment*/" );
    }

}

TEST_CASE( "Lex one punctuator", "[lexer]" ) {
    test_logger log;
    lexer lex(log);

    SECTION("Lex parenthesis") {
        k::source src{"("};
        auto lexemes = lex.parse(src);
        REQUIRE( lexemes.size() == 1 );

        any_lexeme the_lexeme = lexemes[0];
        REQUIRE( std::holds_alternative<punctuator>(the_lexeme) );
        REQUIRE( the_lexeme == punctuator::PARENTHESIS_OPEN );
    }

    SECTION("Lex two parenthesis") {
        k::source src{"( )"};
        auto lexemes = lex.parse(src);
        REQUIRE( lexemes.size() == 2 );

        any_lexeme lex0 = lexemes[0];
        REQUIRE( std::holds_alternative<punctuator>(lex0) );
        REQUIRE( lex0 == punctuator::PARENTHESIS_OPEN );

        any_lexeme lex1 = lexemes[1];
        REQUIRE( std::holds_alternative<punctuator>(lex1) );
        REQUIRE( lex1 == punctuator::PARENTHESIS_CLOSE );
    }

    SECTION("Lex two parenthesis without separator") {
        k::source src{"()"};
        auto lexemes = lex.parse(src);
        REQUIRE( lexemes.size() == 2 );

        any_lexeme lex0 = lexemes[0];
        REQUIRE( std::holds_alternative<punctuator>(lex0) );
        REQUIRE( lex0 == punctuator::PARENTHESIS_OPEN );

        any_lexeme lex1 = lexemes[1];
        REQUIRE( std::holds_alternative<punctuator>(lex1) );
        REQUIRE( lex1 == punctuator::PARENTHESIS_CLOSE );
    }

    SECTION("Lex semicolon") {
        k::source src{";"};
        auto lexemes = lex.parse(src);
        REQUIRE( lexemes.size() == 1 );

        any_lexeme the_lexeme = lexemes[0];
        REQUIRE( std::holds_alternative<punctuator>(the_lexeme) );
        REQUIRE( the_lexeme == punctuator::SEMICOLON );
    }
}

TEST_CASE( "Lex one operator", "[lexer]" ) {
    test_logger log;
    lexer lex(log);

    SECTION("Lex dot") {
        k::source src{"."};
        auto lexemes = lex.parse(src);
        REQUIRE( lexemes.size() == 1 );

        any_lexeme the_lexeme = lexemes[0];
        REQUIRE( std::holds_alternative<operator_>(the_lexeme) );
        REQUIRE( the_lexeme == operator_::DOT );
    }

    SECTION("Lex arrow") {
        k::source src{"->"};
        auto lexemes = lex.parse(src);
        REQUIRE( lexemes.size() == 1 );

        any_lexeme the_lexeme = lexemes[0];
        REQUIRE( std::holds_alternative<operator_>(the_lexeme) );
        REQUIRE( the_lexeme == operator_::ARROW );
    }
}

TEST_CASE("Additional lexer tests", "[lexer]") {
    test_logger log;
    lexer lex(log);

    SECTION("Lex \"ident(0)\"") {
        k::source src{"ident(0)"};
        auto lexemes = lex.parse(src);
        REQUIRE( lexemes.size() == 4 );

        any_lexeme lex0 = lexemes[0];
        REQUIRE( std::holds_alternative<identifier>(lex0) );
        identifier l0 = std::get<identifier>(lex0);
        REQUIRE( l0.content == "ident" );

        any_lexeme lex1 = lexemes[1];
        REQUIRE( std::holds_alternative<punctuator>(lex1) );
        REQUIRE( lex1 == punctuator::PARENTHESIS_OPEN );

        any_lexeme lex2 = lexemes[2];
        REQUIRE( std::holds_alternative<integer>(lex2) );
        integer l2 = std::get<integer>(lex2);
        REQUIRE( l2.content == "0" );

        any_lexeme lex3 = lexemes[3];
        REQUIRE( std::holds_alternative<punctuator>(lex3) );
        REQUIRE( lex3 == punctuator::PARENTHESIS_CLOSE );
    }
}

//
// Test k::name parsing
//

TEST_CASE("Name parsing - nominal cases",  "[lexer][name]") {
    using Case = std::tuple<std::string, bool, std::vector<std::string>>;
    auto [input, expected_abs, expected_parts] = GENERATE(table<std::string, bool, std::vector<std::string>>({
        // Simple relatives
        Case{"Alpha",                       false, {"Alpha"}},
        Case{"_hidden",                     false, {"_hidden"}},
        Case{"_",                           false, {"_"}},

        // Qualified relatives
        Case{"AppCore::EngineX::Renderer2D", false, {"AppCore","EngineX","Renderer2D"}},
        Case{"Layer1::_Private2::Codec3_",    false, {"Layer1","_Private2","Codec3_"}},
        Case{"N0::N1::N2::N3",                false, {"N0","N1","N2","N3"}},

        // Simple absolutes
        Case{"::Rooted",                    true,  {"Rooted"}},
        // Qualified absolutes
        Case{"::RootProject::Module_42::Service9", true, {"RootProject","Module_42","Service9"}},
        Case{"::Top::_Inner::_Leaf1",       true,  {"Top","_Inner","_Leaf1"}},

        // Letters, digits and underscore mixes
        Case{"Alpha1::_2::C3_",             false, {"Alpha1","_2","C3_"}},
        Case{"Sys2D::Filter_3",             false, {"Sys2D","Filter_3"}},

        // Various additional cases
        Case{"Project", false, {"Project"}},
        Case{"Model_X", false, {"Model_X"}},
        Case{"Sys2D::Filter_3", false, {"Sys2D", "Filter_3"}},
        Case{"::Root::FeatureA", true, {"Root", "FeatureA"}},
        Case{"::Top::_Inner::_Leaf1", true, {"Top", "_Inner", "_Leaf1"}},
        Case{"Alpha1::_2::C3_", false, {"Alpha1", "_2", "C3_"}}
    }));

    CAPTURE(input);

    auto n = k::name::from(input);

    // Check for root prefix
    CHECK(n.has_root_prefix() == expected_abs);

    // Check size and order
    REQUIRE(n.size() == expected_parts.size());
    CHECK(n.parts() == expected_parts);

    // Indexes
    for (std::size_t i = 0; i < expected_parts.size(); ++i) {
        CHECK(n[i] == expected_parts[i]);
    }

    // Front and back access
    CHECK(n.front() == expected_parts.front());
    CHECK(n.back()  == expected_parts.back());
}

TEST_CASE("Name parsing - Invalid names", "[lexer][name]") {
    const std::vector<std::string> invalids = {
        "",            // Empty
        "::",          // Empty with heading prefix
        "A::",         // Finishing with separator
        "1Alpha",      // Starting with a digit
        "Alpha::9B",   // Starting with a digit
        "Alpha:::Beta",// Triple ':'
        "Alpha:B",     // Unique ':'
        "Alpha::Beta::", // Finishing by '::'
        "::Gamma::",   // Finishing by '::'
        "App:: Core",  // Space
        "App ::Core",  // Space
        "Némo",        // Non ASCII
        "Node-1",      // Dash
    };

    for (const auto& s : invalids) {
        CAPTURE(s);
        REQUIRE_THROWS_AS(k::name::from(s), std::runtime_error);
    }
}
