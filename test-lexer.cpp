#include <catch2/catch.hpp>

#include "lexer.hpp"
#include "logger.hpp"

using namespace k::lex;
using namespace k::log;

TEST_CASE( "Lex empty source", "[lexer]" ) {
    logger log;
    lexer lex(log);
    auto lexemes = lex.parse_all("");
    REQUIRE( lexemes.empty() );
}

TEST_CASE( "Lex one identifier", "[lexer]" ) {
    logger log;
    lexer lex(log);

    SECTION("Lex char-only identifier") {
        auto lexemes = lex.parse_all("toto");
        REQUIRE( lexemes.size() == 1 );

        any_lexeme the_lexeme = lexemes[0];
        REQUIRE( std::holds_alternative<identifier>(the_lexeme) );

        identifier l = std::get<identifier>(the_lexeme);
        REQUIRE( l.content == "toto" );

    }

    SECTION("Lex char-and-digit identifier") {
        auto lexemes = lex.parse_all("to42to");
        REQUIRE( lexemes.size() == 1 );

        any_lexeme the_lexeme = lexemes[0];
        REQUIRE( std::holds_alternative<identifier>(the_lexeme) );

        identifier l = std::get<identifier>(the_lexeme);
        REQUIRE( l.content == "to42to" );

    }
}


TEST_CASE( "Lex one keyword", "[lexer]" ) {
    logger log;
    lexer lex(log);
/*
    SECTION("Lex true keyword") {
        auto lexemes = lex.parse("true ");
        REQUIRE( lexemes.size() == 1 );

        any_lexeme the_lexeme = lexemes[0];
        REQUIRE( std::holds_alternative<keyword>(the_lexeme) );

        keyword l = std::get<keyword>(the_lexeme);
        REQUIRE( l.content == "true" );
    }

    SECTION("Lex false keyword") {
        auto lexemes = lex.parse("false ");
        REQUIRE( lexemes.size() == 1 );

        any_lexeme the_lexeme = lexemes[0];
        REQUIRE( std::holds_alternative<keyword>(the_lexeme) );

        keyword l = std::get<keyword>(the_lexeme);
        REQUIRE( l.content == "false" );
    }

    SECTION("Lex null keyword") {
        auto lexemes = lex.parse_all("null ");
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
    logger log;
    lexer lex(log);

    SECTION("Lex decimal", "[decimal]") {

        SECTION("Lex decimal 0 integer") {
            auto lexemes = lex.parse_all("0");
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
            auto lexemes = lex.parse_all("0u");
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
            auto lexemes = lex.parse_all("0i");
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
            auto lexemes = lex.parse_all("0ui");
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
            auto lexemes = lex.parse_all("0s");
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
            auto lexemes = lex.parse_all("0us");
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
            auto lexemes = lex.parse_all("0l");
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
            auto lexemes = lex.parse_all("0ul");
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
            auto lexemes = lex.parse_all("0ll");
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
            auto lexemes = lex.parse_all("0ull");
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
            auto lexemes = lex.parse_all("1");
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
            auto lexemes = lex.parse_all("1u");
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
            auto lexemes = lex.parse_all("1i");
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
            auto lexemes = lex.parse_all("1ui");
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
            auto lexemes = lex.parse_all("1s");
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
            auto lexemes = lex.parse_all("1us");
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
            auto lexemes = lex.parse_all("1l");
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
            auto lexemes = lex.parse_all("1ul");
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
            auto lexemes = lex.parse_all("1ll");
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
            auto lexemes = lex.parse_all("1ull");
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
            auto lexemes = lex.parse_all("123");
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
            auto lexemes = lex.parse_all("123u");
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
            auto lexemes = lex.parse_all("123i");
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
            auto lexemes = lex.parse_all("123ui");
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
            auto lexemes = lex.parse_all("123s");
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
            auto lexemes = lex.parse_all("123us");
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
            auto lexemes = lex.parse_all("123l");
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
            auto lexemes = lex.parse_all("123ul");
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
            auto lexemes = lex.parse_all("123ll");
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
            auto lexemes = lex.parse_all("123ull");
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
            auto lexemes = lex.parse_all("0x123def");
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
            auto lexemes = lex.parse_all("0x123defu");
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
            auto lexemes = lex.parse_all("0x123defi");
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
            auto lexemes = lex.parse_all("0x123defui");
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
            auto lexemes = lex.parse_all("0x123defs");
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
            auto lexemes = lex.parse_all("0x123defus");
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
            auto lexemes = lex.parse_all("0x123defl");
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
            auto lexemes = lex.parse_all("0x123deful");
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
            auto lexemes = lex.parse_all("0x123defll");
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
            auto lexemes = lex.parse_all("0x123defull");
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
            auto lexemes = lex.parse_all("0123");
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
            auto lexemes = lex.parse_all("0123u");
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
            auto lexemes = lex.parse_all("0123i");
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
            auto lexemes = lex.parse_all("0123ui");
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
            auto lexemes = lex.parse_all("0123s");
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
            auto lexemes = lex.parse_all("0123us");
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
            auto lexemes = lex.parse_all("0123l");
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
            auto lexemes = lex.parse_all("0123ul");
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
            auto lexemes = lex.parse_all("0123ll");
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
            auto lexemes = lex.parse_all("0123ull");
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
            auto lexemes = lex.parse_all("0o123");
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
            auto lexemes = lex.parse_all("0o123u");
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
            auto lexemes = lex.parse_all("0o123s");
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
            auto lexemes = lex.parse_all("0o123us");
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
            auto lexemes = lex.parse_all("0o123l");
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
            auto lexemes = lex.parse_all("0o123ul");
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
            auto lexemes = lex.parse_all("0o123ll");
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
            auto lexemes = lex.parse_all("0o123ull");
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
            auto lexemes = lex.parse_all("0b1010");
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
            auto lexemes = lex.parse_all("0b1010u");
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
            auto lexemes = lex.parse_all("0b1010i");
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
            auto lexemes = lex.parse_all("0b1010ui");
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
            auto lexemes = lex.parse_all("0b1010s");
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
            auto lexemes = lex.parse_all("0b1010us");
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
            auto lexemes = lex.parse_all("0b1010l");
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
            auto lexemes = lex.parse_all("0b1010ul");
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
            auto lexemes = lex.parse_all("0b1010ll");
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
            auto lexemes = lex.parse_all("0b1010ull");
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
    lexer lex;

    SECTION("Lex implicit float") {

        SECTION("Lex float 123.45e8") {
            auto lexemes = lex.parse_all("123.45e8");
            REQUIRE(lexemes.size() == 1);
            REQUIRE(std::holds_alternative<float_num>(lexemes[0]));
            float_num l = std::get<float_num>(lexemes[0]);
            REQUIRE(l.content == "123.45e8");
            REQUIRE(l.size == FLOAT);
        }

        SECTION("Lex float 123e8") {
            auto lexemes = lex.parse_all("123e8");
            REQUIRE(lexemes.size() == 1);
            REQUIRE(std::holds_alternative<float_num>(lexemes[0]));
            float_num l = std::get<float_num>(lexemes[0]);
            REQUIRE(l.content == "123e8");
            REQUIRE(l.size == FLOAT);
        }

        SECTION("Lex float 123e-8") {
            auto lexemes = lex.parse_all("123e-8");
            REQUIRE(lexemes.size() == 1);
            REQUIRE(std::holds_alternative<float_num>(lexemes[0]));
            float_num l = std::get<float_num>(lexemes[0]);
            REQUIRE(l.content == "123e-8");
            REQUIRE(l.size == FLOAT);
        }

        SECTION("Lex float .45e8") {
            auto lexemes = lex.parse_all(".45e8");
            REQUIRE(lexemes.size() == 1);
            REQUIRE(std::holds_alternative<float_num>(lexemes[0]));
            float_num l = std::get<float_num>(lexemes[0]);
            REQUIRE(l.content == ".45e8");
            REQUIRE(l.size == FLOAT);
        }

        SECTION("Lex float 123.45") {
            auto lexemes = lex.parse_all("123.45");
            REQUIRE(lexemes.size() == 1);
            REQUIRE(std::holds_alternative<float_num>(lexemes[0]));
            float_num l = std::get<float_num>(lexemes[0]);
            REQUIRE(l.content == "123.45");
            REQUIRE(l.size == FLOAT);
        }

        SECTION("Lex float .45") {
            auto lexemes = lex.parse_all(".45");
            REQUIRE(lexemes.size() == 1);
            REQUIRE(std::holds_alternative<float_num>(lexemes[0]));
            float_num l = std::get<float_num>(lexemes[0]);
            REQUIRE(l.content == ".45");
            REQUIRE(l.size == FLOAT);
        }

    }

    SECTION("Lex explicit float") {

        SECTION("Lex float 123.45e8f") {
            auto lexemes = lex.parse_all("123.45e8f");
            REQUIRE(lexemes.size() == 1);
            REQUIRE(std::holds_alternative<float_num>(lexemes[0]));
            float_num l = std::get<float_num>(lexemes[0]);
            REQUIRE(l.content == "123.45e8f");
            REQUIRE(l.size == FLOAT);
        }

        SECTION("Lex float 123e8f") {
            auto lexemes = lex.parse_all("123e8f");
            REQUIRE(lexemes.size() == 1);
            REQUIRE(std::holds_alternative<float_num>(lexemes[0]));
            float_num l = std::get<float_num>(lexemes[0]);
            REQUIRE(l.content == "123e8f");
            REQUIRE(l.size == FLOAT);
        }

        SECTION("Lex float 123e-8f") {
            auto lexemes = lex.parse_all("123e-8f");
            REQUIRE(lexemes.size() == 1);
            REQUIRE(std::holds_alternative<float_num>(lexemes[0]));
            float_num l = std::get<float_num>(lexemes[0]);
            REQUIRE(l.content == "123e-8f");
            REQUIRE(l.size == FLOAT);
        }

        SECTION("Lex float .45e8f") {
            auto lexemes = lex.parse_all(".45e8f");
            REQUIRE(lexemes.size() == 1);
            REQUIRE(std::holds_alternative<float_num>(lexemes[0]));
            float_num l = std::get<float_num>(lexemes[0]);
            REQUIRE(l.content == ".45e8f");
            REQUIRE(l.size == FLOAT);
        }

        SECTION("Lex float 123.45f") {
            auto lexemes = lex.parse_all("123.45f");
            REQUIRE(lexemes.size() == 1);
            REQUIRE(std::holds_alternative<float_num>(lexemes[0]));
            float_num l = std::get<float_num>(lexemes[0]);
            REQUIRE(l.content == "123.45f");
            REQUIRE(l.size == FLOAT);
        }

        SECTION("Lex float .45f") {
            auto lexemes = lex.parse_all(".45f");
            REQUIRE(lexemes.size() == 1);
            REQUIRE(std::holds_alternative<float_num>(lexemes[0]));
            float_num l = std::get<float_num>(lexemes[0]);
            REQUIRE(l.content == ".45f");
            REQUIRE(l.size == FLOAT);
        }

    }


    SECTION("Lex explicit double") {

        SECTION("Lex double 123.45e8d") {
            auto lexemes = lex.parse_all("123.45e8d");
            REQUIRE(lexemes.size() == 1);
            REQUIRE(std::holds_alternative<float_num>(lexemes[0]));
            float_num l = std::get<float_num>(lexemes[0]);
            REQUIRE(l.content == "123.45e8d");
            REQUIRE(l.size == DOUBLE);
        }

        SECTION("Lex double 123e8d") {
            auto lexemes = lex.parse_all("123e8d");
            REQUIRE(lexemes.size() == 1);
            REQUIRE(std::holds_alternative<float_num>(lexemes[0]));
            float_num l = std::get<float_num>(lexemes[0]);
            REQUIRE(l.content == "123e8d");
            REQUIRE(l.size == DOUBLE);
        }

        SECTION("Lex double 123e-8d") {
            auto lexemes = lex.parse_all("123e-8d");
            REQUIRE(lexemes.size() == 1);
            REQUIRE(std::holds_alternative<float_num>(lexemes[0]));
            float_num l = std::get<float_num>(lexemes[0]);
            REQUIRE(l.content == "123e-8d");
            REQUIRE(l.size == DOUBLE);
        }

        SECTION("Lex double .45e8d") {
            auto lexemes = lex.parse_all(".45e8d");
            REQUIRE(lexemes.size() == 1);
            REQUIRE(std::holds_alternative<float_num>(lexemes[0]));
            float_num l = std::get<float_num>(lexemes[0]);
            REQUIRE(l.content == ".45e8d");
            REQUIRE(l.size == DOUBLE);
        }

        SECTION("Lex double 123.45d") {
            auto lexemes = lex.parse_all("123.45d");
            REQUIRE(lexemes.size() == 1);
            REQUIRE(std::holds_alternative<float_num>(lexemes[0]));
            float_num l = std::get<float_num>(lexemes[0]);
            REQUIRE(l.content == "123.45d");
            REQUIRE(l.size == DOUBLE);
        }

        SECTION("Lex double .45d") {
            auto lexemes = lex.parse_all(".45d");
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
    logger log;
    lexer lex(log);

    SECTION("Lex char char") {
        auto lexemes = lex.parse_all("'c'");
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
        auto lexemes = lex.parse_all("'0'");
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
        auto lexemes = lex.parse_all("'&'");
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
        auto lexemes = lex.parse_all("'\\\\'");
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
        auto lexemes = lex.parse_all("'\\\''");
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
    logger log;
    lexer lex(log);

    SECTION("Lex string") {
        auto lexemes = lex.parse_all("\"Hell0\\\' world \\\\ !\"");
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
    logger log;
    lexer lex(log);

    SECTION("Lex true boolean") {
        auto lexemes = lex.parse_all("true");
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
        auto lexemes = lex.parse_all("false");
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
    logger log;
    lexer lex(log);

    SECTION("Lex one null") {
        auto lexemes = lex.parse_all("null");
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
    logger log;
    lexer lex(log);

    SECTION("Lex end-of-line comment") {
        auto lexemes = lex.parse_all("// Hello my comment\n");
        REQUIRE( lexemes.size() == 1 );

        any_lexeme the_lexeme = lexemes[0];
        REQUIRE( std::holds_alternative<comment>(the_lexeme) );

        comment l = std::get<comment>(the_lexeme);
        REQUIRE( l.content == "// Hello my comment" );
    }

    SECTION("Lex end-of-line end-of-file comment") {
        auto lexemes = lex.parse_all("// Hello my comment");
        REQUIRE( lexemes.size() == 1 );

        any_lexeme the_lexeme = lexemes[0];
        REQUIRE( std::holds_alternative<comment>(the_lexeme) );

        comment l = std::get<comment>(the_lexeme);
        REQUIRE( l.content == "// Hello my comment" );
    }

    SECTION("Lex multi-line comment") {
        auto lexemes = lex.parse_all("/* Hello my\n comment*/");
        REQUIRE( lexemes.size() == 1 );

        any_lexeme the_lexeme = lexemes[0];
        REQUIRE( std::holds_alternative<comment>(the_lexeme) );

        comment l = std::get<comment>(the_lexeme);
        REQUIRE( l.content == "/* Hello my\n comment*/" );
    }

}

TEST_CASE( "Lex one punctuator", "[lexer]" ) {
    logger log;
    lexer lex(log);

    SECTION("Lex parenthesis") {
        auto lexemes = lex.parse_all("(");
        REQUIRE( lexemes.size() == 1 );

        any_lexeme the_lexeme = lexemes[0];
        REQUIRE( std::holds_alternative<punctuator>(the_lexeme) );
        REQUIRE( the_lexeme == punctuator::PARENTHESIS_OPEN );
    }

    SECTION("Lex two parenthesis") {
        auto lexemes = lex.parse_all("( )");
        REQUIRE( lexemes.size() == 2 );

        any_lexeme lex0 = lexemes[0];
        REQUIRE( std::holds_alternative<punctuator>(lex0) );
        REQUIRE( lex0 == punctuator::PARENTHESIS_OPEN );

        any_lexeme lex1 = lexemes[1];
        REQUIRE( std::holds_alternative<punctuator>(lex1) );
        REQUIRE( lex1 == punctuator::PARENTHESIS_CLOSE );
    }

    SECTION("Lex two parenthesis without separator") {
        auto lexemes = lex.parse_all("()");
        REQUIRE( lexemes.size() == 2 );

        any_lexeme lex0 = lexemes[0];
        REQUIRE( std::holds_alternative<punctuator>(lex0) );
        REQUIRE( lex0 == punctuator::PARENTHESIS_OPEN );

        any_lexeme lex1 = lexemes[1];
        REQUIRE( std::holds_alternative<punctuator>(lex1) );
        REQUIRE( lex1 == punctuator::PARENTHESIS_CLOSE );
    }

    SECTION("Lex semicolon") {
        auto lexemes = lex.parse_all(";");
        REQUIRE( lexemes.size() == 1 );

        any_lexeme the_lexeme = lexemes[0];
        REQUIRE( std::holds_alternative<punctuator>(the_lexeme) );
        REQUIRE( the_lexeme == punctuator::SEMICOLON );
    }
}

TEST_CASE( "Lex one operator", "[lexer]" ) {
    logger log;
    lexer lex(log);

    SECTION("Lex dot") {
        auto lexemes = lex.parse_all(".");
        REQUIRE( lexemes.size() == 1 );

        any_lexeme the_lexeme = lexemes[0];
        REQUIRE( std::holds_alternative<operator_>(the_lexeme) );
        REQUIRE( the_lexeme == operator_::DOT );
    }

    SECTION("Lex arrow") {
        auto lexemes = lex.parse_all("->");
        REQUIRE( lexemes.size() == 1 );

        any_lexeme the_lexeme = lexemes[0];
        REQUIRE( std::holds_alternative<operator_>(the_lexeme) );
        REQUIRE( the_lexeme == operator_::ARROW );
    }
}

TEST_CASE("Additional lexer tests", "[lexer]") {
    logger log;
    lexer lex(log);

    SECTION("Lex \"ident(0)\"") {
        auto lexemes = lex.parse_all("ident(0)");
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