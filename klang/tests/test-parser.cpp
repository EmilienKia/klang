#include <catch2/catch.hpp>

#include "../src/lexer.hpp"
#include "../src/logger.hpp"
#include "../src/parser.hpp"
#include "../src/unit.hpp"

using namespace k::parse;

//
// Tooling
//

bool is_same(const k::parse::ast::qualified_identifier& ident1, const k::name& ident2 ) {
    if(ident1.has_root_prefix() != ident2.has_root_prefix()) {
        return false;
    }

    if(ident1.size() != ident2.size()) {
        return false;
    }

    for(size_t idx = 0; idx <ident1.size(); idx++) {
        if(ident1[idx]!=ident2[idx]) {
            return false;
        }
    }

    return true;
}

bool is_same(const k::parse::ast::identifier_expr& ident1, const k::name& ident2 ) {
    return is_same(ident1.qident, ident2);
}

//
// Parse identifiers
//

TEST_CASE( "Parse empty identifier", "[parser][expression][identifier]" ) {
    k::log::logger log;
    k::parse::parser parser(log, "");
    auto expr = parser.parse_identifier_expr();

    REQUIRE( !expr );

}

TEST_CASE( "Parse identifier without prefix", "[parser][expression][identifier]" ) {
    k::log::logger log;
    k::parse::parser parser(log, "first");
    auto expr = parser.parse_identifier_expr();

    auto identifier_expr = std::dynamic_pointer_cast<ast::identifier_expr>(expr);

    REQUIRE( identifier_expr );
    REQUIRE( identifier_expr->qident.has_root_prefix() == false );
    REQUIRE( identifier_expr->qident.names.size() == 1 );
    REQUIRE( identifier_expr->qident.names[0].content == "first" );
}

TEST_CASE( "Parse identifier with prefix", "[parser][expression][identifier]" ) {
    k::log::logger log;
    k::parse::parser parser(log, "::top");
    auto expr = parser.parse_identifier_expr();

    auto identifier_expr = std::dynamic_pointer_cast<ast::identifier_expr>(expr);

    REQUIRE( identifier_expr );
    REQUIRE( identifier_expr->qident.has_root_prefix() == true );
    REQUIRE( identifier_expr->qident.size() == 1 );
    REQUIRE( identifier_expr->qident.names[0].content == "top" );
}

TEST_CASE( "Parse identifiers without prefix", "[parser][expression][identifier]" ) {
    k::log::logger log;
    k::parse::parser parser(log, "first::second");
    auto expr = parser.parse_identifier_expr();

    auto identifier_expr = std::dynamic_pointer_cast<ast::identifier_expr>(expr);

    REQUIRE( identifier_expr );
    REQUIRE( identifier_expr->qident.initial_doublecolon.has_value() == false );
    REQUIRE( identifier_expr->qident.size() == 2 );
    REQUIRE( identifier_expr->qident[0] == "first" );
    REQUIRE( identifier_expr->qident[1] == "second" );
}

TEST_CASE( "Parse identifiers with prefix", "[parser][expression][identifier]" ) {
    k::log::logger log;
    k::parse::parser parser(log, "::first::second");
    auto expr = parser.parse_identifier_expr();

    auto identifier_expr = std::dynamic_pointer_cast<ast::identifier_expr>(expr);

    REQUIRE( identifier_expr );
    REQUIRE( identifier_expr->qident.has_root_prefix() == true );
    REQUIRE( identifier_expr->qident.size() == 2 );
    REQUIRE( identifier_expr->qident[0] == "first" );
    REQUIRE( identifier_expr->qident[1] == "second" );
}

//
// Parse Primary expressions
//

TEST_CASE( "Parse character primary expression", "[parser][expression][primary_expr]") {
    k::log::logger log;
    k::parse::parser parser(log, "'a'");
    auto expr = parser.parse_primary_expr();
    REQUIRE( expr );

    auto lit = std::dynamic_pointer_cast<ast::literal_expr>(expr);
    REQUIRE( lit );
    REQUIRE( lit->literal.is<k::lex::character>() );

    auto c = lit->literal.get<k::lex::character>();
    REQUIRE( c.content == "'a'");
}

TEST_CASE( "Parse string primary expression", "[parser][expression][primary_expr]") {
    k::log::logger log;
    k::parse::parser parser(log, "\"a b c\"");
    auto expr = parser.parse_primary_expr();
    REQUIRE( expr );

    auto lit = std::dynamic_pointer_cast<ast::literal_expr>(expr);
    REQUIRE( lit );
    REQUIRE( lit->literal.is<k::lex::string>() );

    auto c = lit->literal.get<k::lex::string>();
    REQUIRE( c.content == "\"a b c\"");
}

TEST_CASE( "Parse integer primary expression", "[parser][expression][primary_expr]") {
    k::log::logger log;
    k::parse::parser parser(log, "1");
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
    k::log::logger log;
    k::parse::parser parser(log, "this");
    auto expr = parser.parse_primary_expr();
    REQUIRE( expr );

    auto kw = std::dynamic_pointer_cast<ast::keyword_expr>(expr);
    REQUIRE( kw );
    REQUIRE( kw->keyword.type == k::lex::keyword::THIS );
}

TEST_CASE( "Parse parenthesis primary expression", "[parser][expression][primary_expr]") {
    k::log::logger log;
    k::parse::parser parser(log, "( 1 )");
    auto expr = parser.parse_primary_expr();
    REQUIRE( expr );

    auto lit = std::dynamic_pointer_cast<ast::literal_expr>(expr);
    REQUIRE( lit );
    REQUIRE( lit->literal.is<k::lex::integer>() );

    auto i = lit->literal.get<k::lex::integer>();
    REQUIRE( i.content == "1");
}

TEST_CASE( "Parse identifier primary expression", "[parser][expression][primary_expr]") {
    k::log::logger log;
    k::parse::parser parser(log, "( ident )");
    auto expr = parser.parse_primary_expr();
    REQUIRE( expr );

    auto ident = std::dynamic_pointer_cast<ast::identifier_expr>(expr);
    REQUIRE( ident );
    REQUIRE( is_same(*ident, k::name(false, "ident") ) );
}

TEST_CASE( "Parse complex identifier primary expression", "[parser][expression][primary_expr]") {
    k::log::logger log;
    k::parse::parser parser(log, "( ::ident :: ifier )");
    auto expr = parser.parse_primary_expr();
    REQUIRE( expr );

    auto ident = std::dynamic_pointer_cast<ast::identifier_expr>(expr);
    REQUIRE( ident );
    REQUIRE(  is_same(*ident, k::name(true, {"ident", "ifier"})  ) );
}

TEST_CASE( "Parse parenthesis primary expressions", "[parser][expression][primary_expr]") {
    k::log::logger log;
    k::parse::parser parser(log, "( a + b )");
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
    k::log::logger log;
    k::parse::parser parser(log, "( a + b ) * c");
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
    k::log::logger log;
    k::parse::parser parser(log, "c * ( a + b )");
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
    k::log::logger log;
    k::parse::parser parser(log, "( a + b ) *(c-d)");
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
    k::log::logger log;
    k::parse::parser parser(log, "ident");
    auto expr = parser.parse_postfix_expr();
    REQUIRE( expr );

    auto ident = std::dynamic_pointer_cast<ast::identifier_expr>(expr);
    REQUIRE( ident );
    REQUIRE( is_same(*ident, k::name(false, "ident") ) );
}

TEST_CASE("Parse ++ and -- postfix expression", "[parser][expression][postfix_expr]") {
    k::log::logger log;
    k::parse::parser parser(log, "ident ++ --");
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
    k::log::logger log;
    k::parse::parser parser(log, "ident()");
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
    k::log::logger log;
    k::parse::parser parser(log, "ident(0)");
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
    k::log::logger log;
    k::parse::parser parser(log, "ident ( 0 , a)");

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
    k::log::logger log;
    k::parse::parser parser(log, "ident");
    auto expr = parser.parse_unary_expr();
    REQUIRE( expr );

    auto ident = std::dynamic_pointer_cast<ast::identifier_expr>(expr);
    REQUIRE( ident );
    REQUIRE( is_same(*ident, k::name(false, "ident") ) );
}



TEST_CASE("Parse no unary expression with postfix operator expr", "[parser][expression][unary_expr]") {
    k::log::logger log;
    k::parse::parser parser(log, "ident ++");
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
    k::log::logger log;
    k::parse::parser parser(log, "++ -- * & + - ! ~ ident");
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
    k::log::logger log;
    k::parse::parser parser(log, "ident");
    auto expr = parser.parse_cast_expr();
    REQUIRE( expr );

    auto ident = std::dynamic_pointer_cast<ast::identifier_expr>(expr);
    REQUIRE( ident );
    REQUIRE( is_same(*ident, k::name(false, "ident") ) );
}

TEST_CASE("Parse no cast expression with prefix and postfix operator", "[parser][expression][cast_expr]") {
    k::log::logger log;
    k::parse::parser parser(log, "++ident++");
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
    k::log::logger log;
    k::parse::parser parser(log, "(long)ident");
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
    k::log::logger log;
    k::parse::parser parser(log, "(int)(long) ident");
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
    k::log::logger log;
    k::parse::parser parser(log, "(long)(a + 2)");
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
    k::log::logger log;
    k::parse::parser parser(log, "(int) ident(0, a)");
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
// Parse PM expression
//

TEST_CASE("Parse no PM expression", "[parser][expression][pm_expr]") {
    k::log::logger log;
    k::parse::parser parser(log, "ident");
    auto expr = parser.parse_pm_expr();
    REQUIRE( expr );

    auto ident = std::dynamic_pointer_cast<ast::identifier_expr>(expr);
    REQUIRE( ident );
    REQUIRE( is_same(*ident, k::name(false, "ident") ) );
}


TEST_CASE("Parse dot-star PM expression", "[parser][expression][pm_expr]") {
    k::log::logger log;
    k::parse::parser parser(log, "ident .* ifier");
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
    k::log::logger log;
    k::parse::parser parser(log, "ident->*ifier");
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
    k::log::logger log;
    k::parse::parser parser(log, "ident.*ifier->*other");
    auto expr = parser.parse_pm_expr();
    REQUIRE( expr );

    auto pm1 = std::dynamic_pointer_cast<ast::binary_operator_expr>(expr);
    REQUIRE( pm1 );

    REQUIRE( pm1->op == k::lex::operator_::DOT_STAR );

    auto ident = std::dynamic_pointer_cast<ast::identifier_expr>(pm1->lexpr());
    REQUIRE( ident );
    REQUIRE( is_same(*ident, k::name(false, "ident") ) );

    auto pm2 = std::dynamic_pointer_cast<ast::binary_operator_expr>(pm1->rexpr());
    REQUIRE( pm2 );

    REQUIRE( pm2->op == k::lex::operator_::ARROW_STAR );

    auto ifier = std::dynamic_pointer_cast<ast::identifier_expr>(pm2->lexpr());
    REQUIRE( ifier );
    REQUIRE( is_same(*ifier, k::name(false, "ifier") ) );

    auto other = std::dynamic_pointer_cast<ast::identifier_expr>(pm2->rexpr());
    REQUIRE( other );
    REQUIRE( is_same(*other, k::name(false, "other") ) );
}


//
// Conditional expression
//

TEST_CASE("No conditional expression", "[parser][expression][conditional_expr]") {
    k::log::logger log;
    k::parse::parser parser(log, "0");
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
    k::log::logger log;
    k::parse::parser parser(log, "a + b * c");
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
    k::log::logger log;
    k::parse::parser parser(log, "a )");
    auto expr = parser.parse_expression();
    REQUIRE( expr );

    auto ident = std::dynamic_pointer_cast<ast::identifier_expr>(expr);
    REQUIRE( ident );
    REQUIRE( is_same(*ident, k::name(false, "a") ) );
}


TEST_CASE( "Parse simple expression list", "[parser][expression]") {
    k::log::logger log;
    k::parse::parser parser(log, "a , 0");
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
    k::log::logger log;
    k::parse::parser parser(log, "a,0)");
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
    k::log::logger log;
    k::parse::parser parser(log, "a(b)");
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
    k::log::logger log;
    k::parse::parser parser(log, "static const plic : int = 0;");
    auto var = parser.parse_variable_decl();
    REQUIRE( var );
    REQUIRE( var->name.content == "plic" );
}



//
// Parse visibility declaration
//
TEST_CASE( "Parse public visibility declaration", "[parser][visibility]") {
    k::log::logger log;
    k::parse::parser parser(log, "public:");
    auto var = parser.parse_visibility_decl();
    REQUIRE( var );
    REQUIRE( var->scope.type == k::lex::keyword::PUBLIC );
}

TEST_CASE( "Parse protected visibility declaration", "[parser][visibility]") {
    k::log::logger log;
    k::parse::parser parser(log, "  protected  :  ");
    auto var = parser.parse_visibility_decl();
    REQUIRE( var );
    REQUIRE( var->scope.type == k::lex::keyword::PROTECTED );
}

TEST_CASE( "Parse private visibility declaration", "[parser][visibility]") {
    k::log::logger log;
    k::parse::parser parser(log, "private:");
    auto var = parser.parse_visibility_decl();
    REQUIRE( var );
    REQUIRE( var->scope.type == k::lex::keyword::PRIVATE );
}


//
// Various cases
//
TEST_CASE( "Parse expression : titi + (long) toto", "[parser][expression]") {
    k::log::logger log;
    k::parse::parser parser(log, "titi + (long) toto");
    auto expr = parser.parse_expression();
    REQUIRE( expr );

    auto add = std::dynamic_pointer_cast<ast::binary_operator_expr>(expr);
    REQUIRE( add );
    REQUIRE( add->op == k::lex::operator_::PLUS );

    auto titi = std::dynamic_pointer_cast<ast::identifier_expr>(add->lexpr());
    REQUIRE( titi );
    REQUIRE( is_same(*titi, k::name(false, {"titi"}) ) );

    auto cast = std::dynamic_pointer_cast<ast::cast_expr>(add->rexpr());
    REQUIRE( cast );

    auto toto = std::dynamic_pointer_cast<ast::identifier_expr>(cast->expr());
    REQUIRE( toto );
    REQUIRE( is_same(*toto, k::name(false, {"toto"}) ) );
}

TEST_CASE( "Parse return expression : return a + (long)b;", "[parser][expression]") {
    k::log::logger log;
    k::parse::parser parser(log, "return a + (long)b;");
    auto stmt = parser.parse_return_statement();
    REQUIRE( stmt );

    auto expr = stmt->expr;
    REQUIRE( expr );

    auto add = std::dynamic_pointer_cast<ast::binary_operator_expr>(expr);
    REQUIRE( add );
    REQUIRE( add->op == k::lex::operator_::PLUS );

    auto a = std::dynamic_pointer_cast<ast::identifier_expr>(add->lexpr());
    REQUIRE( a );
    REQUIRE( is_same(*a, k::name(false, {"a"}) ) );

    auto cast = std::dynamic_pointer_cast<ast::cast_expr>(add->rexpr());
    REQUIRE( cast );

    auto b = std::dynamic_pointer_cast<ast::identifier_expr>(cast->expr());
    REQUIRE( b );
    REQUIRE( is_same(*b, k::name(false, {"b"}) ) );
}

//
// If then else
//

TEST_CASE( "Parse if-only statement", "[parser][if-else]") {
    k::log::logger log;
    k::parse::parser parser(log, "if(a==b) { return true; } ");
    auto stmt = parser.parse_if_else_statement();
    REQUIRE( stmt );

    REQUIRE( stmt->if_kw == k::lex::keyword::IF );
    REQUIRE( ! stmt->else_kw );

    REQUIRE( stmt->test_expr );
    auto test = std::dynamic_pointer_cast<ast::binary_operator_expr>(stmt->test_expr);
    REQUIRE( test );
    REQUIRE( test->op == k::lex::operator_::DOUBLE_EQUAL );

    REQUIRE( stmt->then_stmt );
    auto block = std::dynamic_pointer_cast<ast::block_statement>(stmt->then_stmt);
    REQUIRE( block );
    REQUIRE( block->statements.size() == 1 );
    REQUIRE( std::dynamic_pointer_cast<ast::return_statement>(block->statements[0]) );

    REQUIRE( stmt->else_stmt == nullptr );
}

TEST_CASE( "Parse if-else statement", "[parser][if-else]") {
    k::log::logger log;
    k::parse::parser parser(log, "if(a!=b) { return true; } else return false; ");
    auto stmt = parser.parse_if_else_statement();
    REQUIRE( stmt );

    REQUIRE( stmt->if_kw == k::lex::keyword::IF );
    REQUIRE( stmt->else_kw );
    REQUIRE( *(stmt->else_kw) == k::lex::keyword::ELSE );

    REQUIRE( stmt->test_expr );
    auto test = std::dynamic_pointer_cast<ast::binary_operator_expr>(stmt->test_expr);
    REQUIRE( test );
    REQUIRE( test->op == k::lex::operator_::EXCLAMATION_MARK_EQUAL );

    REQUIRE( stmt->then_stmt );
    auto block = std::dynamic_pointer_cast<ast::block_statement>(stmt->then_stmt);
    REQUIRE( block );
    REQUIRE( block->statements.size() == 1 );
    REQUIRE( std::dynamic_pointer_cast<ast::return_statement>(block->statements[0]) );

    REQUIRE( stmt->else_stmt );
    auto ret = std::dynamic_pointer_cast<ast::return_statement>(stmt->else_stmt);
    REQUIRE( ret );
}