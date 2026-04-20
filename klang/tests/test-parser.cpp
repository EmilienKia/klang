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

#include "../src/lex/lexer.hpp"
#include "../src/errors.hpp"

#include "helpers.hpp"

#include <fmt/core.h>
#include <fmt/format.h>

using namespace k::parse;
using namespace k::log;

//
// Parse identifiers
//

TEST_CASE( "Parse empty identifier", "[parser][expression][identifier]" ) {
    test_logger log;
    k::source src{""};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_identifier_expr();

    REQUIRE( !expr );

}

TEST_CASE( "Parse identifier without prefix", "[parser][expression][identifier]" ) {
    test_logger log;
    k::source src{"first"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_identifier_expr();

    auto identifier_expr = std::dynamic_pointer_cast<ast::identifier_expr>(expr);

    REQUIRE( identifier_expr );
    REQUIRE( identifier_expr->qident.has_root_prefix() == false );
    REQUIRE( identifier_expr->qident.names.size() == 1 );
    REQUIRE( identifier_expr->qident.names[0].content == "first" );
}

TEST_CASE( "Parse identifier with prefix", "[parser][expression][identifier]" ) {
    test_logger log;
    k::source src{"::top"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_identifier_expr();

    auto identifier_expr = std::dynamic_pointer_cast<ast::identifier_expr>(expr);

    REQUIRE( identifier_expr );
    REQUIRE( identifier_expr->qident.has_root_prefix() == true );
    REQUIRE( identifier_expr->qident.size() == 1 );
    REQUIRE( identifier_expr->qident.names[0].content == "top" );
}

TEST_CASE( "Parse identifiers without prefix", "[parser][expression][identifier]" ) {
    test_logger log;
    k::source src{"first::second"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_identifier_expr();

    auto identifier_expr = std::dynamic_pointer_cast<ast::identifier_expr>(expr);

    REQUIRE( identifier_expr );
    REQUIRE( identifier_expr->qident.initial_doublecolon.has_value() == false );
    REQUIRE( identifier_expr->qident.size() == 2 );
    REQUIRE( identifier_expr->qident[0] == "first" );
    REQUIRE( identifier_expr->qident[1] == "second" );
}

TEST_CASE( "Parse identifiers with prefix", "[parser][expression][identifier]" ) {
    test_logger log;
    k::source src{"::first::second"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_identifier_expr();

    auto identifier_expr = std::dynamic_pointer_cast<ast::identifier_expr>(expr);

    REQUIRE( identifier_expr );
    REQUIRE( identifier_expr->qident.has_root_prefix() == true );
    REQUIRE( identifier_expr->qident.size() == 2 );
    REQUIRE( identifier_expr->qident[0] == "first" );
    REQUIRE( identifier_expr->qident[1] == "second" );
}

//
// Parse type specifiers
//

TEST_CASE( "Parse int[4][] type spec", "[parser][type]") {
    test_logger log;
    k::source src{"int[][4]"};
    k::parse::parser parser(log, src);
    auto spec = parser.parse_type_spec();
    REQUIRE( spec );

    auto arr_spec1 = std::dynamic_pointer_cast<ast::array_type_specifier>(spec);
    REQUIRE( arr_spec1 );
    REQUIRE( arr_spec1->lex_int );
    REQUIRE( arr_spec1->lex_int->int_content() == "4" );
    REQUIRE( arr_spec1->subtype );

    auto arr_spec2 = std::dynamic_pointer_cast<ast::array_type_specifier>(arr_spec1->subtype);
    REQUIRE( arr_spec2 );
    REQUIRE( !arr_spec2->lex_int );
    REQUIRE( arr_spec2->subtype );

    auto subtype = std::dynamic_pointer_cast<ast::keyword_type_specifier>(arr_spec2->subtype);
    REQUIRE( subtype );
    REQUIRE( subtype->keyword.type == k::lex::keyword::INT );
}

TEST_CASE("Parse function pointer type spec *(int)", "[parser][type][function_ref_type]") {
    test_logger log;
    k::source src{"*(int)"};
    k::parse::parser parser(log, src);
    auto spec = parser.parse_type_spec();
    REQUIRE(spec);

    auto frt = std::dynamic_pointer_cast<ast::function_ref_type_specifier>(spec);
    REQUIRE(frt);
    REQUIRE(frt->ref_op.type == k::lex::operator_::STAR);
    REQUIRE(!frt->owner.has_value());
    REQUIRE(frt->param_types.size() == 1);
    auto pt = std::dynamic_pointer_cast<ast::keyword_type_specifier>(frt->param_types[0]);
    REQUIRE(pt);
    REQUIRE(pt->keyword.type == k::lex::keyword::INT);
}

TEST_CASE("Parse function view type spec ?(int, double+)", "[parser][type][function_ref_type]") {
    test_logger log;
    k::source src{"?(int, double+)"};
    k::parse::parser parser(log, src);
    auto spec = parser.parse_type_spec();
    REQUIRE(spec);

    auto frt = std::dynamic_pointer_cast<ast::function_ref_type_specifier>(spec);
    REQUIRE(frt);
    REQUIRE(frt->ref_op.type == k::lex::operator_::QUESTION_MARK);
    REQUIRE(!frt->owner.has_value());
    REQUIRE(frt->param_types.size() == 2);
}

TEST_CASE("Parse function link type spec +()", "[parser][type][function_ref_type]") {
    test_logger log;
    k::source src{"+()"};
    k::parse::parser parser(log, src);
    auto spec = parser.parse_type_spec();
    REQUIRE(spec);

    auto frt = std::dynamic_pointer_cast<ast::function_ref_type_specifier>(spec);
    REQUIRE(frt);
    REQUIRE(frt->ref_op.type == k::lex::operator_::PLUS);
    REQUIRE(!frt->owner.has_value());
    REQUIRE(frt->param_types.empty());
}

TEST_CASE("Parse member function pointer type spec MyClass::*(int)", "[parser][type][function_ref_type]") {
    test_logger log;
    k::source src{"MyClass::*(int)"};
    k::parse::parser parser(log, src);
    auto spec = parser.parse_type_spec();
    REQUIRE(spec);

    auto frt = std::dynamic_pointer_cast<ast::function_ref_type_specifier>(spec);
    REQUIRE(frt);
    REQUIRE(frt->ref_op.type == k::lex::operator_::STAR);
    REQUIRE(frt->owner.has_value());
    REQUIRE(frt->owner->names.size() == 1);
    REQUIRE(std::string{frt->owner->names[0].content} == "MyClass");
    REQUIRE(frt->param_types.size() == 1);
}

TEST_CASE("Parse * type spec does NOT parse as function ref when not followed by (", "[parser][type][function_ref_type]") {
    test_logger log;
    k::source src{"int*"};
    k::parse::parser parser(log, src);
    auto spec = parser.parse_type_spec();
    REQUIRE(spec);
    // Should be a pointer_type_specifier, NOT a function_ref_type_specifier
    auto frt = std::dynamic_pointer_cast<ast::function_ref_type_specifier>(spec);
    REQUIRE(!frt);
    auto ptr = std::dynamic_pointer_cast<ast::pointer_type_specifier>(spec);
    REQUIRE(ptr);
    REQUIRE(ptr->pointer_type.type == k::lex::operator_::STAR);
}

TEST_CASE( "Parse character primary expression", "[parser][expression][primary_expr]") {
    test_logger log;
    k::source src{"'a'"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_primary_expr();
    REQUIRE( expr );

    auto lit = std::dynamic_pointer_cast<ast::literal_expr>(expr);
    REQUIRE( lit );
    REQUIRE( lit->literal.is<k::lex::character>() );

    auto c = lit->literal.get<k::lex::character>();
    REQUIRE( c.content == "'a'");
}

TEST_CASE( "Parse string primary expression", "[parser][expression][primary_expr]") {
    test_logger log;
    k::source src{"\"a b c\""};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_primary_expr();
    REQUIRE( expr );

    auto lit = std::dynamic_pointer_cast<ast::literal_expr>(expr);
    REQUIRE( lit );
    REQUIRE( lit->literal.is<k::lex::string>() );

    auto c = lit->literal.get<k::lex::string>();
    REQUIRE( c.content == "\"a b c\"");
}

TEST_CASE( "Parse integer primary expression", "[parser][expression][primary_expr]") {
    test_logger log;
    k::source src{"1"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_primary_expr();
    REQUIRE( expr );

    auto lit = std::dynamic_pointer_cast<ast::literal_expr>(expr);
    REQUIRE( lit );
    REQUIRE( lit->literal->content == "1" );
    REQUIRE( lit->literal.is<k::lex::integer>() );

    auto c = lit->literal.get<k::lex::integer>();
    REQUIRE( c.content == "1");
}

TEST_CASE( "Parse this primary expression", "[parser][expression][primary_expr]") {
    test_logger log;
    k::source src{"this"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_primary_expr();
    REQUIRE( expr );

    auto kw = std::dynamic_pointer_cast<ast::keyword_expr>(expr);
    REQUIRE( kw );
    REQUIRE( kw->keyword.type == k::lex::keyword::THIS );
}

TEST_CASE( "Parse parenthesis primary expression", "[parser][expression][primary_expr]") {
    test_logger log;
    k::source src{"( 1 )"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_primary_expr();
    REQUIRE( expr );

    auto lit = std::dynamic_pointer_cast<ast::literal_expr>(expr);
    REQUIRE( lit );
    REQUIRE( lit->literal.is<k::lex::integer>() );

    auto i = lit->literal.get<k::lex::integer>();
    REQUIRE( i.content == "1");
}

TEST_CASE( "Parse identifier primary expression", "[parser][expression][primary_expr]") {
    test_logger log;
    k::source src{"( ident )"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_primary_expr();
    REQUIRE( expr );

    auto ident = std::dynamic_pointer_cast<ast::identifier_expr>(expr);
    REQUIRE( ident );
    REQUIRE( is_same(*ident, k::name(false, "ident") ) );
}

TEST_CASE( "Parse complex identifier primary expression", "[parser][expression][primary_expr]") {
    test_logger log;
    k::source src{"( ::ident :: ifier )"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_primary_expr();
    REQUIRE( expr );

    auto ident = std::dynamic_pointer_cast<ast::identifier_expr>(expr);
    REQUIRE( ident );
    REQUIRE(  is_same(*ident, k::name(true, {"ident", "ifier"})  ) );
}

TEST_CASE( "Parse parenthesis primary expressions", "[parser][expression][primary_expr]") {
    test_logger log;
    k::source src{"( a + b )"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_expression();
    REQUIRE( expr );

    auto add = std::dynamic_pointer_cast<ast::binary_operator_expr>(expr);
    REQUIRE( add );
    REQUIRE( add->op == k::lex::operator_::PLUS );

    auto a = std::dynamic_pointer_cast<ast::identifier_expr>(add->lexpr());
    REQUIRE( a );
    REQUIRE(  is_same(*a, k::name(false, {"a"})  ) );

    auto b = std::dynamic_pointer_cast<ast::identifier_expr>(add->rexpr());
    REQUIRE( b );
    REQUIRE(  is_same(*b, k::name(false, {"b"})  ) );
}

TEST_CASE( "Parse parenthesis primary expressions at right of binary expr", "[parser][expression][primary_expr]") {
    test_logger log;
    k::source src{"( a + b ) * c"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_expression();
    REQUIRE( expr );

    auto mul = std::dynamic_pointer_cast<ast::binary_operator_expr>(expr);
    REQUIRE( mul );
    REQUIRE( mul->op == k::lex::operator_::STAR );

    auto add = std::dynamic_pointer_cast<ast::binary_operator_expr>(mul->lexpr());
    REQUIRE( add );
    REQUIRE( add->op == k::lex::operator_::PLUS );

    auto a = std::dynamic_pointer_cast<ast::identifier_expr>(add->lexpr());
    REQUIRE( a );
    REQUIRE(  is_same(*a, k::name(false, {"a"})  ) );

    auto b = std::dynamic_pointer_cast<ast::identifier_expr>(add->rexpr());
    REQUIRE( b );
    REQUIRE(  is_same(*b, k::name(false, {"b"})  ) );

    auto c = std::dynamic_pointer_cast<ast::identifier_expr>(mul->rexpr());
    REQUIRE( c );
    REQUIRE(  is_same(*c, k::name(false, {"c"})  ) );
}

TEST_CASE( "Parse parenthesis primary expressions at left of binary expr", "[parser][expression][primary_expr]") {
    test_logger log;
    k::source src{"c * ( a + b )"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_expression();
    REQUIRE( expr );

    auto mul = std::dynamic_pointer_cast<ast::binary_operator_expr>(expr);
    REQUIRE( mul );
    REQUIRE( mul->op == k::lex::operator_::STAR );

    auto c = std::dynamic_pointer_cast<ast::identifier_expr>(mul->lexpr());
    REQUIRE( c );
    REQUIRE(  is_same(*c, k::name(false, {"c"})  ) );

    auto add = std::dynamic_pointer_cast<ast::binary_operator_expr>(mul->rexpr());
    REQUIRE( add );
    REQUIRE( add->op == k::lex::operator_::PLUS );

    auto a = std::dynamic_pointer_cast<ast::identifier_expr>(add->lexpr());
    REQUIRE( a );
    REQUIRE(  is_same(*a, k::name(false, {"a"})  ) );

    auto b = std::dynamic_pointer_cast<ast::identifier_expr>(add->rexpr());
    REQUIRE( b );
    REQUIRE(  is_same(*b, k::name(false, {"b"})  ) );
}

TEST_CASE( "Parse parenthesis primary expressions at left and right of binary expr", "[parser][expression][primary_expr]") {
    test_logger log;
    k::source src{"( a + b ) *(c-d)"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_expression();
    REQUIRE( expr );

    auto mul = std::dynamic_pointer_cast<ast::binary_operator_expr>(expr);
    REQUIRE( mul );
    REQUIRE( mul->op == k::lex::operator_::STAR );

    auto add = std::dynamic_pointer_cast<ast::binary_operator_expr>(mul->lexpr());
    REQUIRE( add );
    REQUIRE( add->op == k::lex::operator_::PLUS );

    auto a = std::dynamic_pointer_cast<ast::identifier_expr>(add->lexpr());
    REQUIRE( a );
    REQUIRE(  is_same(*a, k::name(false, {"a"})  ) );

    auto b = std::dynamic_pointer_cast<ast::identifier_expr>(add->rexpr());
    REQUIRE( b );
    REQUIRE(  is_same(*b, k::name(false, {"b"})  ) );

    auto sub = std::dynamic_pointer_cast<ast::binary_operator_expr>(mul->rexpr());
    REQUIRE( sub );
    REQUIRE( sub->op == k::lex::operator_::MINUS );

    auto c = std::dynamic_pointer_cast<ast::identifier_expr>(sub->lexpr());
    REQUIRE( c );
    REQUIRE(  is_same(*c, k::name(false, {"c"})  ) );

    auto d = std::dynamic_pointer_cast<ast::identifier_expr>(sub->rexpr());
    REQUIRE( d );
    REQUIRE(  is_same(*d, k::name(false, {"d"})  ) );
}

//
// Postfix expr
//

TEST_CASE("Parse no postfix expression", "[parser][expression][postfix_expr]") {
    test_logger log;
    k::source src{"ident"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_postfix_expr();
    REQUIRE( expr );

    auto ident = std::dynamic_pointer_cast<ast::identifier_expr>(expr);
    REQUIRE( ident );
    REQUIRE( is_same(*ident, k::name(false, "ident") ) );
}

TEST_CASE("Parse ++ and -- postfix expression", "[parser][expression][postfix_expr]") {
    test_logger log;
    k::source src{"ident ++ --"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_postfix_expr();
    REQUIRE( expr );

    auto unary_minus = std::dynamic_pointer_cast<ast::unary_postfix_expr>(expr);
    REQUIRE( unary_minus );
    REQUIRE( unary_minus->op == k::lex::operator_::DOUBLE_MINUS );
    REQUIRE( unary_minus->expr() );

    auto unary_plus = std::dynamic_pointer_cast<ast::unary_postfix_expr>(unary_minus->expr());
    REQUIRE( unary_plus );
    REQUIRE( unary_plus->op == k::lex::operator_::DOUBLE_PLUS );
    REQUIRE( unary_plus->expr() );

    auto ident = std::dynamic_pointer_cast<ast::identifier_expr>(unary_plus->expr());
    REQUIRE( ident );
    REQUIRE( is_same(*ident, k::name(false, "ident") ) );
}

TEST_CASE("Parse () postfix expression with no second expr", "[parser][expression][postfix_expr]") {
    test_logger log;
    k::source src{"ident()"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_postfix_expr();
    REQUIRE( expr );

    auto parenthesis = std::dynamic_pointer_cast<ast::parenthesis_postifx_expr>(expr);
    REQUIRE( parenthesis );

    auto ident = std::dynamic_pointer_cast<ast::identifier_expr>(parenthesis->lexpr());
    REQUIRE( ident );
    REQUIRE( is_same(*ident, k::name(false, "ident") ) );

    auto zero = std::dynamic_pointer_cast<ast::literal_expr>(parenthesis->rexpr());
    REQUIRE( !zero );
}

TEST_CASE("Parse () postfix expression with one second expr", "[parser][expression][postfix_expr]") {
    test_logger log;
    k::source src{"ident(0)"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_postfix_expr();
    REQUIRE( expr );

    auto parenthesis = std::dynamic_pointer_cast<ast::parenthesis_postifx_expr>(expr);
    REQUIRE(parenthesis );

    auto ident = std::dynamic_pointer_cast<ast::identifier_expr>(parenthesis->lexpr());
    REQUIRE( ident );
    REQUIRE( is_same(*ident, k::name(false, "ident") ) );

    auto zero = std::dynamic_pointer_cast<ast::literal_expr>(parenthesis->rexpr());
    REQUIRE( zero );
    REQUIRE( zero->literal.is<k::lex::integer>() );
    auto i = zero->literal.get<k::lex::integer>();
    REQUIRE( i.content == "0");
}

TEST_CASE("Parse () postfix expression with many second expr", "[parser][expression][postfix_expr]") {
    test_logger log;
    k::source src{"ident ( 0 , a)"};
    k::parse::parser parser(log, src);

    std::shared_ptr<ast::expression> expr;
    SECTION("Parse () postfix expression with many second expr as postfix") {
        expr = parser.parse_postfix_expr();
    }
    SECTION("Parse () postfix expression with many second expr as expression") {
        expr = parser.parse_expression();
    }
    REQUIRE( expr );

    auto parenthesis = std::dynamic_pointer_cast<ast::parenthesis_postifx_expr>(expr);
    REQUIRE(parenthesis );

    auto ident = std::dynamic_pointer_cast<ast::identifier_expr>(parenthesis->lexpr());
    REQUIRE( ident );
    REQUIRE( is_same(*ident, k::name(false, "ident") ) );

    auto list = std::dynamic_pointer_cast<ast::expr_list_expr>(parenthesis->rexpr());
    REQUIRE( list );
    REQUIRE( list->size() == 2 );

    auto zero = std::dynamic_pointer_cast<ast::literal_expr>(list->expr(0));
    REQUIRE( zero );
    REQUIRE( zero->literal.is<k::lex::integer>() );
    auto i = zero->literal.get<k::lex::integer>();
    REQUIRE( i.content == "0");

    auto a = std::dynamic_pointer_cast<ast::identifier_expr>(list->expr(1));
    REQUIRE( a );
    REQUIRE( is_same(*a, k::name(false, "a") ) );
}

#if TODO
TEST_CASE("Parse [] postfix expression", "[parser][expression][postfix_expr]") {
    k::log::logger log;
    k::parse::parser parser(log, "ident [ 0 ]");
    auto expr = parser.parse_postfix_expr();
    REQUIRE( expr );

    auto brackets = std::dynamic_pointer_cast<ast::bracket_postifx_expr>(expr);
    REQUIRE( brackets );

    auto ident = std::dynamic_pointer_cast<ast::identifier_expr>(brackets->lexpr());
    REQUIRE( ident );
    REQUIRE( *ident == k::parse::ast::identifier_expr(false, {{"ident"}}) );

    auto zero = std::dynamic_pointer_cast<ast::literal_expr>(brackets->rexpr());
    REQUIRE( zero );
    REQUIRE( zero->literal.is<k::lex::integer>() );
    auto i = zero->literal.get<k::lex::integer>();
    REQUIRE( i.content == "0");
}
#endif

//
// Parse unary expressions
//

TEST_CASE("Parse no unary expression", "[parser][expression][unary_expr]") {
    test_logger log;
    k::source src{"ident"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_unary_expr();
    REQUIRE( expr );

    auto ident = std::dynamic_pointer_cast<ast::identifier_expr>(expr);
    REQUIRE( ident );
    REQUIRE( is_same(*ident, k::name(false, "ident") ) );
}

TEST_CASE("Parse no unary expression with postfix operator expr", "[parser][expression][unary_expr]") {
    test_logger log;
    k::source src{"ident ++"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_unary_expr();
    REQUIRE( expr );

    auto unary_plus = std::dynamic_pointer_cast<ast::unary_postfix_expr>(expr);
    REQUIRE( unary_plus );
    REQUIRE( unary_plus->op == k::lex::operator_::DOUBLE_PLUS );
    REQUIRE( unary_plus->expr() );

    auto ident = std::dynamic_pointer_cast<ast::identifier_expr>(unary_plus->expr());
    REQUIRE( ident );
    REQUIRE( is_same(*ident, k::name(false, "ident") ) );

}

TEST_CASE("Parse prefix operator unary expression", "[parser][expression][unary_expr]") {
    test_logger log;
    k::source src{"++ -- * & + - ! ~ ident"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_unary_expr();
    REQUIRE( expr );

    auto plus_plus = std::dynamic_pointer_cast<ast::unary_prefix_expr>(expr);
    REQUIRE( plus_plus );
    REQUIRE( plus_plus->op == k::lex::operator_::DOUBLE_PLUS );

    auto minus_minus = std::dynamic_pointer_cast<ast::unary_prefix_expr>(plus_plus->expr());
    REQUIRE( minus_minus );
    REQUIRE( minus_minus->op == k::lex::operator_::DOUBLE_MINUS );

    auto star = std::dynamic_pointer_cast<ast::unary_prefix_expr>(minus_minus->expr());
    REQUIRE( star );
    REQUIRE( star->op == k::lex::operator_::STAR );

    auto ampersand = std::dynamic_pointer_cast<ast::unary_prefix_expr>(star->expr());
    REQUIRE( ampersand );
    REQUIRE( ampersand->op == k::lex::operator_::AMPERSAND );

    auto plus = std::dynamic_pointer_cast<ast::unary_prefix_expr>(ampersand->expr());
    REQUIRE( plus );
    REQUIRE( plus->op == k::lex::operator_::PLUS );

    auto minus = std::dynamic_pointer_cast<ast::unary_prefix_expr>(plus->expr());
    REQUIRE( minus );
    REQUIRE( minus->op == k::lex::operator_::MINUS );

    auto exclamation = std::dynamic_pointer_cast<ast::unary_prefix_expr>(minus->expr());
    REQUIRE( exclamation );
    REQUIRE( exclamation->op == k::lex::operator_::EXCLAMATION_MARK );

    auto tilde = std::dynamic_pointer_cast<ast::unary_prefix_expr>(exclamation->expr());
    REQUIRE( tilde );
    REQUIRE( tilde->op == k::lex::operator_::TILDE );

    auto ident = std::dynamic_pointer_cast<ast::identifier_expr>(tilde->expr());
    REQUIRE( ident );
    REQUIRE( is_same(*ident, k::name(false, "ident") ) );
}

//
// Parse cast expression
//

TEST_CASE("Parse no cast expression", "[parser][expression][cast_expr]") {
    test_logger log;
    k::source src{"ident"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_cast_expr();
    REQUIRE( expr );

    auto ident = std::dynamic_pointer_cast<ast::identifier_expr>(expr);
    REQUIRE( ident );
    REQUIRE( is_same(*ident, k::name(false, "ident") ) );
}

TEST_CASE("Parse no cast expression with prefix and postfix operator", "[parser][expression][cast_expr]") {
    test_logger log;
    k::source src{"++ident++"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_cast_expr();
    REQUIRE( expr );

    auto prefix_plus = std::dynamic_pointer_cast<ast::unary_prefix_expr>(expr);
    REQUIRE( prefix_plus );
    REQUIRE( prefix_plus->op == k::lex::operator_::DOUBLE_PLUS );
    REQUIRE( prefix_plus->expr() );

    auto postfix_plus = std::dynamic_pointer_cast<ast::unary_postfix_expr>(prefix_plus->expr());
    REQUIRE( postfix_plus );
    REQUIRE( postfix_plus->op == k::lex::operator_::DOUBLE_PLUS );
    REQUIRE( postfix_plus->expr() );

    auto ident = std::dynamic_pointer_cast<ast::identifier_expr>(postfix_plus->expr());
    REQUIRE( ident );
    REQUIRE( is_same(*ident, k::name(false, "ident") ) );
}

TEST_CASE("Parse cast expression", "[parser][expression][cast_expr]") {
    test_logger log;
    k::source src{"(long)ident"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_cast_expr();
    REQUIRE( expr );

    auto long_cast = std::dynamic_pointer_cast<ast::cast_expr>(expr);
    REQUIRE( long_cast );
    // TODO: REQUIRE( long_cast->type.name );
    REQUIRE( long_cast->expr() );

    auto ident = std::dynamic_pointer_cast<ast::identifier_expr>(long_cast->expr());
    REQUIRE( ident );
    REQUIRE( is_same(*ident, k::name(false, "ident") ) );
}

TEST_CASE("Parse multiple cast expression", "[parser][expression][cast_expr]") {
    test_logger log;
    k::source src{"(int)(long) ident"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_cast_expr();
    REQUIRE( expr );

    auto int_cast = std::dynamic_pointer_cast<ast::cast_expr>(expr);
    REQUIRE( int_cast );
    // TODO: REQUIRE( int_cast->type.name );
    REQUIRE( int_cast->expr() );

    auto long_cast = std::dynamic_pointer_cast<ast::cast_expr>(int_cast->expr());
    REQUIRE( long_cast );
    // TODO: REQUIRE( long_cast->type.name );
    REQUIRE( long_cast->expr() );

    auto ident = std::dynamic_pointer_cast<ast::identifier_expr>(long_cast->expr());
    REQUIRE( ident );
    REQUIRE( is_same(*ident, k::name(false, "ident") ) );
}

TEST_CASE("Parse cast of parenthesis expression", "[parser][expression][cast_expr]") {
    test_logger log;
    k::source src{"(long)(a + 2)"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_cast_expr();
    REQUIRE( expr );

    auto long_cast = std::dynamic_pointer_cast<ast::cast_expr>(expr);
    REQUIRE( long_cast );
    // TODO: REQUIRE( int_cast->type.name );
    REQUIRE( long_cast->expr() );

    auto add = std::dynamic_pointer_cast<ast::binary_operator_expr>(long_cast->expr());
    REQUIRE( add );
    REQUIRE( add->op == k::lex::operator_::PLUS );

    auto a = std::dynamic_pointer_cast<ast::identifier_expr>(add->lexpr());
    REQUIRE( a );
    REQUIRE(  is_same(*a, k::name(false, {"a"})  ) );

    auto lit = std::dynamic_pointer_cast<ast::literal_expr>(add->rexpr());
    REQUIRE( lit );
    REQUIRE( lit->literal.is<k::lex::integer>() );
    auto i = lit->literal.get<k::lex::integer>();
    REQUIRE( i.content == "2");
}

TEST_CASE("Parse cast of function invocation", "[parser][expression][postfix_expr][cast_expr]") {
    test_logger log;
    k::source src{"(int) ident(0, a)"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_expression();
    REQUIRE( expr );

    auto cast = std::dynamic_pointer_cast<ast::cast_expr>(expr);
    REQUIRE( cast );

    auto parenthesis = std::dynamic_pointer_cast<ast::parenthesis_postifx_expr>(cast->expr());
    REQUIRE(parenthesis );

    auto ident = std::dynamic_pointer_cast<ast::identifier_expr>(parenthesis->lexpr());
    REQUIRE( ident );
    REQUIRE( is_same(*ident, k::name(false, "ident") ) );

    auto list = std::dynamic_pointer_cast<ast::expr_list_expr>(parenthesis->rexpr());
    REQUIRE( list );
    REQUIRE( list->size() == 2 );

    auto zero = std::dynamic_pointer_cast<ast::literal_expr>(list->expr(0));
    REQUIRE( zero );
    REQUIRE( zero->literal.is<k::lex::integer>() );
    auto i = zero->literal.get<k::lex::integer>();
    REQUIRE( i.content == "0");

    auto a = std::dynamic_pointer_cast<ast::identifier_expr>(list->expr(1));
    REQUIRE( a );
    REQUIRE( is_same(*a, k::name(false, "a") ) );
}

//
// PM expression
//

TEST_CASE("Parse no PM expression", "[parser][expression][pm_expr]") {
    test_logger log;
    k::source src{"ident"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_pm_expr();
    REQUIRE( expr );

    auto ident = std::dynamic_pointer_cast<ast::identifier_expr>(expr);
    REQUIRE( ident );
    REQUIRE( is_same(*ident, k::name(false, "ident") ) );
}

TEST_CASE("Parse dot-star PM expression", "[parser][expression][pm_expr]") {
    test_logger log;
    k::source src{"ident .* ifier"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_pm_expr();
    REQUIRE( expr );

    auto pm = std::dynamic_pointer_cast<ast::binary_operator_expr>(expr);
    REQUIRE( pm );
    REQUIRE( pm->op == k::lex::operator_::DOT_STAR );

    auto ident = std::dynamic_pointer_cast<ast::identifier_expr>(pm->lexpr());
    REQUIRE( ident );
    REQUIRE( is_same(*ident, k::name(false, "ident") ) );

    auto ifier = std::dynamic_pointer_cast<ast::identifier_expr>(pm->rexpr());
    REQUIRE( ifier );
    REQUIRE( is_same(*ifier, k::name(false, "ifier") ) );
}

TEST_CASE("Parse arrow-star PM expression", "[parser][expression][pm_expr]") {
    test_logger log;
    k::source src{"ident->*ifier"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_pm_expr();
    REQUIRE( expr );

    auto pm = std::dynamic_pointer_cast<ast::binary_operator_expr>(expr);
    REQUIRE( pm );

    REQUIRE( pm->op == k::lex::operator_::ARROW_STAR );

    auto ident = std::dynamic_pointer_cast<ast::identifier_expr>(pm->lexpr());
    REQUIRE( ident );
    REQUIRE( is_same(*ident, k::name(false, "ident") ) );

    auto ifier = std::dynamic_pointer_cast<ast::identifier_expr>(pm->rexpr());
    REQUIRE( ifier );
    REQUIRE( is_same(*ifier, k::name(false, "ifier") ) );
}

TEST_CASE("Parse PM expression", "[parser][expression][pm_expr]") {
    test_logger log;
    k::source src{"ident.*ifier->*other"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_pm_expr();
    REQUIRE( expr );

    // Left-associative: (ident .* ifier) ->* other
    auto pm1 = std::dynamic_pointer_cast<ast::binary_operator_expr>(expr);
    REQUIRE( pm1 );

    REQUIRE( pm1->op == k::lex::operator_::ARROW_STAR );

    auto pm2 = std::dynamic_pointer_cast<ast::binary_operator_expr>(pm1->lexpr());
    REQUIRE( pm2 );

    REQUIRE( pm2->op == k::lex::operator_::DOT_STAR );

    auto ident = std::dynamic_pointer_cast<ast::identifier_expr>(pm2->lexpr());
    REQUIRE( ident );
    REQUIRE( is_same(*ident, k::name(false, "ident") ) );

    auto ifier = std::dynamic_pointer_cast<ast::identifier_expr>(pm2->rexpr());
    REQUIRE( ifier );
    REQUIRE( is_same(*ifier, k::name(false, "ifier") ) );

    auto other = std::dynamic_pointer_cast<ast::identifier_expr>(pm1->rexpr());
    REQUIRE( other );
    REQUIRE( is_same(*other, k::name(false, "other") ) );
}

//
// Conditional expression
//

TEST_CASE("No conditional expression", "[parser][expression][conditional_expr]") {
    test_logger log;
    k::source src{"0"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_conditional_expr();
    REQUIRE( expr );

    auto lit = std::dynamic_pointer_cast<ast::literal_expr>(expr);
    REQUIRE( lit );
    REQUIRE( lit->literal->content == "0" );
}

//
// TODO
//

//
// Parse expression
//

TEST_CASE( "Parse expression", "[parser][expression]") {
    test_logger log;
    k::source src{"a + b * c"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_expression();
    REQUIRE( expr );

    auto add = std::dynamic_pointer_cast<ast::binary_operator_expr>(expr);
    REQUIRE( add );
    REQUIRE( add->op == k::lex::operator_::PLUS );

    auto a = std::dynamic_pointer_cast<ast::identifier_expr>(add->lexpr());
    REQUIRE( a );

    auto mul = std::dynamic_pointer_cast<ast::binary_operator_expr>(add->rexpr());
    REQUIRE( mul );
    REQUIRE( mul->op == k::lex::operator_::STAR );

    auto b = std::dynamic_pointer_cast<ast::identifier_expr>(mul->lexpr());
    REQUIRE( b );

    auto c = std::dynamic_pointer_cast<ast::identifier_expr>(mul->rexpr());
    REQUIRE( c );

}

TEST_CASE( "Parse simple expression with additional token", "[parser][expression]") {
    test_logger log;
    k::source src{"a )"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_expression();
    REQUIRE( expr );

    auto ident = std::dynamic_pointer_cast<ast::identifier_expr>(expr);
    REQUIRE( ident );
    REQUIRE( is_same(*ident, k::name(false, "a") ) );
}

TEST_CASE( "Parse simple expression list", "[parser][expression]") {
    test_logger log;
    k::source src{"a , 0"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_expression();
    REQUIRE( expr );

    auto list = std::dynamic_pointer_cast<ast::expr_list_expr>(expr);
    REQUIRE( list );
    REQUIRE( list->size() == 2 );

    auto ident = std::dynamic_pointer_cast<ast::identifier_expr>(list->expr(0));
    REQUIRE( ident );
    REQUIRE( is_same(*ident, k::name(false, "a") ) );

    auto lit = std::dynamic_pointer_cast<ast::literal_expr>(list->expr(1));
    REQUIRE( lit );
    REQUIRE( lit->literal->content == "0" );
}

TEST_CASE( "Parse simple expression list with additional token", "[parser][expression]") {
    test_logger log;
    k::source src{"a,0)"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_expression();
    REQUIRE( expr );

    auto list = std::dynamic_pointer_cast<ast::expr_list_expr>(expr);
    REQUIRE( list );
    REQUIRE( list->size() == 2 );

    auto ident = std::dynamic_pointer_cast<ast::identifier_expr>(list->expr(0));
    REQUIRE( ident );
    REQUIRE( is_same(*ident, k::name(false, "a") ) );

    auto lit = std::dynamic_pointer_cast<ast::literal_expr>(list->expr(1));
    REQUIRE( lit );
    REQUIRE( lit->literal->content == "0" );
}

//
// Parse function invocation expression
//
TEST_CASE( "Parse expression of simple function invocation", "[parser][expression]") {
    test_logger log;
    k::source src{"a(b)"};
    k::parse::parser parser(log, src);
    auto expr = parser.parse_expression();
    REQUIRE( expr );

    auto func = std::dynamic_pointer_cast<ast::parenthesis_postifx_expr>(expr);
    REQUIRE( func );

    auto ident = std::dynamic_pointer_cast<ast::identifier_expr>(func->lexpr());
    REQUIRE( ident );
    REQUIRE( is_same(*ident, k::name(false, "a") ) );

}

//
// Parse variable declaration
//
TEST_CASE( "Parse variable declaration", "[parser][variable]") {
    test_logger log;
    k::source src{"static const plic : int = 0;"};
    k::parse::parser parser(log, src);
    auto var = parser.parse_variable_decl();
    REQUIRE( var );
    REQUIRE( var->name.content == "plic" );
}

//
// Parse visibility declaration
//
TEST_CASE( "Parse public visibility declaration", "[parser][visibility]") {
    test_logger log;
    k::source src{"public:"};
    k::parse::parser parser(log, src);
    auto var = parser.parse_visibility_decl();
    REQUIRE( var );
    REQUIRE( var->scope.type == k::lex::keyword::PUBLIC );
}

TEST_CASE( "Parse protected visibility declaration", "[parser][visibility]") {
    test_logger log;
    k::source src{"  protected  :  "};
    k::parse::parser parser(log, src);
    auto var = parser.parse_visibility_decl();
    REQUIRE( var );
    REQUIRE( var->scope.type == k::lex::keyword::PROTECTED );
}

TEST_CASE( "Parse private visibility declaration", "[parser][visibility]") {
    test_logger log;
    k::source src{"private:"};
    k::parse::parser parser(log, src);
    auto var = parser.parse_visibility_decl();
    REQUIRE( var );
    REQUIRE( var->scope.type == k::lex::keyword::PRIVATE );
}

TEST_CASE("Parse typed enum with object-backed entry forms", "[parser][enum][typed]") {
    test_logger log;
    k::source src{R"(
        enum MyEnum : MyStruct {
            FIRST_VALUE(1, 2);
            SECOND_VALUE() default;
            THIRD_VALUE{.a = 42};
            ANOTHER_SECOND_VALUE = SECOND_VALUE;
            IMPLICIT_VALUE;
        };
    )"};
    k::parse::parser parser(log, src);
    auto decl = parser.parse_enum_decl();

    REQUIRE(decl);
    REQUIRE(decl->explicit_underlying_type);

    auto underlying = std::dynamic_pointer_cast<ast::identified_type_specifier>(decl->explicit_underlying_type);
    REQUIRE(underlying);
    REQUIRE(underlying->name.names.size() == 1);
    REQUIRE(std::string{underlying->name.names[0].content} == "MyStruct");

    // Compatibility path kept for current enum derivation pipeline.
    REQUIRE(decl->base_name.has_value());
    REQUIRE(*decl->base_name == "MyStruct");

    REQUIRE(decl->entries.size() == 5);

    auto& first = *decl->entries[0];
    REQUIRE(first.has_paren_initializer());
    REQUIRE(first.ctor_args.size() == 2);
    REQUIRE_FALSE(first.is_default);

    auto& second = *decl->entries[1];
    REQUIRE(second.has_paren_initializer());
    REQUIRE(second.ctor_args.empty());
    REQUIRE(second.is_default);

    auto& third = *decl->entries[2];
    REQUIRE(third.has_brace_initializer());
    REQUIRE(third.brace_init);
    REQUIRE(third.brace_init->is_designated);
    REQUIRE(third.brace_init->elements.size() == 1);

    auto& alias = *decl->entries[3];
    REQUIRE(alias.has_ref_initializer());
    REQUIRE(alias.ref_value.has_value());
    REQUIRE(std::string{alias.ref_value->content} == "SECOND_VALUE");

    auto& implicit = *decl->entries[4];
    REQUIRE_FALSE(implicit.has_explicit_initializer());
    REQUIRE_FALSE(implicit.is_default);
}

TEST_CASE("Parse enum with explicit integer underlying type", "[parser][enum][typed]") {
    test_logger log;
    k::source src{R"(
        enum Small : unsigned byte {
            A = 1;
            B = 2;
        };
    )"};
    k::parse::parser parser(log, src);
    auto decl = parser.parse_enum_decl();

    REQUIRE(decl);
    REQUIRE(decl->explicit_underlying_type);

    auto underlying = std::dynamic_pointer_cast<ast::keyword_type_specifier>(decl->explicit_underlying_type);
    REQUIRE(underlying);
    REQUIRE(underlying->keyword.type == k::lex::keyword::BYTE);
    REQUIRE(underlying->is_unsigned);

    REQUIRE_FALSE(decl->base_name.has_value());
    REQUIRE(decl->entries.size() == 2);
    REQUIRE(decl->entries[0]->has_literal_initializer());
    REQUIRE(decl->entries[1]->has_literal_initializer());
}

TEST_CASE("Parse typed enum reports missing open brace diagnostic", "[parser][enum][typed][error]") {
    test_logger log;
    k::source src{"enum Broken : MyStruct A = 1; };"};
    k::parse::parser parser(log, src);

    try {
        (void)parser.parse_enum_decl();
        FAIL("Expected parsing_error");
    } catch (const k::parse::parsing_error& err) {
        REQUIRE(err.get_diagnostic().code
                == static_cast<unsigned int>(k::diag::parser_diag::ERR_ENUM_MISSING_OPEN_BRACE));
    }
}

TEST_CASE("Parse typed enum reports missing entry name diagnostic", "[parser][enum][typed][error]") {
    test_logger log;
    k::source src{"enum Broken : MyStruct { = 1; };"};
    k::parse::parser parser(log, src);

    try {
        (void)parser.parse_enum_decl();
        FAIL("Expected parsing_error");
    } catch (const k::parse::parsing_error& err) {
        REQUIRE(err.get_diagnostic().code
                == static_cast<unsigned int>(k::diag::parser_diag::ERR_ENUM_ENTRY_EXPECT_NAME));
    }
}



//
// Typed enum semantic and diagnostic cases
//

