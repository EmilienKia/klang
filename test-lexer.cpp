#include <catch2/catch.hpp>

#include "lexer.hpp"

using namespace k::lex;

TEST_CASE( "Lex empty source", "[lexer]" ) {
    lexer lex;
    auto lexemes = lex.parse_all("");
    REQUIRE( lexemes.empty() );
}

TEST_CASE( "Lex one identifier", "[lexer]" ) {
    lexer lex;

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
    lexer lex;
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

TEST_CASE( "Lex one integer", "[lexer]" ) {
    lexer lex;

    SECTION("Lex decimal 0 integer") {
        auto lexemes = lex.parse_all("0");
        REQUIRE( lexemes.size() == 1 );

        any_lexeme the_lexeme = lexemes[0];
        REQUIRE( std::holds_alternative<integer>(the_lexeme) );

        integer l = std::get<integer>(the_lexeme);
        REQUIRE( l.content == "0" );
    }

    SECTION("Lex decimal 1 integer") {
        auto lexemes = lex.parse_all("1");
        REQUIRE( lexemes.size() == 1 );

        any_lexeme the_lexeme = lexemes[0];
        REQUIRE( std::holds_alternative<integer>(the_lexeme) );

        integer l = std::get<integer>(the_lexeme);
        REQUIRE( l.content == "1" );
    }

    SECTION("Lex decimal integer") {
        auto lexemes = lex.parse_all("123");
        REQUIRE( lexemes.size() == 1 );

        any_lexeme the_lexeme = lexemes[0];
        REQUIRE( std::holds_alternative<integer>(the_lexeme) );

        integer l = std::get<integer>(the_lexeme);
        REQUIRE( l.content == "123" );
    }

    SECTION("Lex hexadecimal identifier") {
        auto lexemes = lex.parse_all("0x123def");
        REQUIRE( lexemes.size() == 1 );

        any_lexeme the_lexeme = lexemes[0];
        REQUIRE( std::holds_alternative<integer>(the_lexeme) );

        integer l = std::get<integer>(the_lexeme);
        REQUIRE( l.content == "0x123def" );
    }

    SECTION("Lex octal identifier") {
        auto lexemes = lex.parse_all("0123");
        REQUIRE( lexemes.size() == 1 );

        any_lexeme the_lexeme = lexemes[0];
        REQUIRE( std::holds_alternative<integer>(the_lexeme) );

        integer l = std::get<integer>(the_lexeme);
        REQUIRE( l.content == "0123" );
    }

    SECTION("Lex octal identifier with long prefix") {
        auto lexemes = lex.parse_all("0o123");
        REQUIRE( lexemes.size() == 1 );

        any_lexeme the_lexeme = lexemes[0];
        REQUIRE( std::holds_alternative<integer>(the_lexeme) );

        integer l = std::get<integer>(the_lexeme);
        REQUIRE( l.content == "0o123" );
    }

    SECTION("Lex binary identifier") {
        auto lexemes = lex.parse_all("0b1010");
        REQUIRE( lexemes.size() == 1 );

        any_lexeme the_lexeme = lexemes[0];
        REQUIRE( std::holds_alternative<integer>(the_lexeme) );

        integer l = std::get<integer>(the_lexeme);
        REQUIRE( l.content == "0b1010" );
    }
}


TEST_CASE( "Lex one char", "[lexer]" ) {
    lexer lex;

    SECTION("Lex char char") {
        auto lexemes = lex.parse_all("'c'");
        REQUIRE( lexemes.size() == 1 );

        any_lexeme the_lexeme = lexemes[0];
        REQUIRE( std::holds_alternative<character>(the_lexeme) );

        character l = std::get<character>(the_lexeme);
        REQUIRE( l.content == "'c'" );
    }

    SECTION("Lex digit char") {
        auto lexemes = lex.parse_all("'0'");
        REQUIRE( lexemes.size() == 1 );

        any_lexeme the_lexeme = lexemes[0];
        REQUIRE( std::holds_alternative<character>(the_lexeme) );

        character l = std::get<character>(the_lexeme);
        REQUIRE( l.content == "'0'" );
    }

    SECTION("Lex special char") {
        auto lexemes = lex.parse_all("'&'");
        REQUIRE( lexemes.size() == 1 );

        any_lexeme the_lexeme = lexemes[0];
        REQUIRE( std::holds_alternative<character>(the_lexeme) );

        character l = std::get<character>(the_lexeme);
        REQUIRE( l.content == "'&'" );
    }

    SECTION("Lex anti-slash escape char") {
        auto lexemes = lex.parse_all("'\\\\'");
        REQUIRE( lexemes.size() == 1 );

        any_lexeme the_lexeme = lexemes[0];
        REQUIRE( std::holds_alternative<character>(the_lexeme) );

        character l = std::get<character>(the_lexeme);
        REQUIRE( l.content == "'\\\\'" );
    }

    SECTION("Lex simple quote escape char") {
        auto lexemes = lex.parse_all("'\\\''");
        REQUIRE( lexemes.size() == 1 );

        any_lexeme the_lexeme = lexemes[0];
        REQUIRE( std::holds_alternative<character>(the_lexeme) );

        character l = std::get<character>(the_lexeme);
        REQUIRE( l.content == "'\\\''" );
    }

    // TODO add unicode escape tests
}


TEST_CASE( "Lex one string", "[lexer]" ) {
    lexer lex;

    SECTION("Lex string") {
        auto lexemes = lex.parse_all("\"Hell0\\\' world \\\\ !\"");
        REQUIRE( lexemes.size() == 1 );

        any_lexeme the_lexeme = lexemes[0];
        REQUIRE( std::holds_alternative<string>(the_lexeme) );

        string l = std::get<string>(the_lexeme);
        REQUIRE( l.content == "\"Hell0\\\' world \\\\ !\"" );
    }

    // TODO add unicode escape tests
}


TEST_CASE( "Lex one boolean", "[lexer]" ) {
    lexer lex;

    SECTION("Lex true boolean") {
        auto lexemes = lex.parse_all("true");
        REQUIRE( lexemes.size() == 1 );

        any_lexeme the_lexeme = lexemes[0];
        REQUIRE( std::holds_alternative<boolean>(the_lexeme) );

        boolean l = std::get<boolean>(the_lexeme);
        REQUIRE( l.content == "true" );
    }

    SECTION("Lex false boolean") {
        auto lexemes = lex.parse_all("false");
        REQUIRE( lexemes.size() == 1 );

        any_lexeme the_lexeme = lexemes[0];
        REQUIRE( std::holds_alternative<boolean>(the_lexeme) );

        boolean l = std::get<boolean>(the_lexeme);
        REQUIRE( l.content == "false" );
    }
}


TEST_CASE( "Lex null", "[lexer]" ) {
    lexer lex;

    SECTION("Lex one null") {
        auto lexemes = lex.parse_all("null");
        REQUIRE( lexemes.size() == 1 );

        any_lexeme the_lexeme = lexemes[0];
        REQUIRE( std::holds_alternative<null>(the_lexeme) );

        null l = std::get<null>(the_lexeme);
        REQUIRE( l.content == "null" );
    }
}

TEST_CASE( "Lex one comment", "[lexer]" ) {
    lexer lex;

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
    lexer lex;

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
    lexer lex;

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
    lexer lex;

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