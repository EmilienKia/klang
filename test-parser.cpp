#include <catch2/catch.hpp>

#include "lexer.hpp"
#include "parser.hpp"
#include "unit.hpp"

using namespace k::parse;

//
// Tooling
//

bool is_same(const k::parse::ast::qualified_identifier& ident1, const k::unit::name& ident2 ) {
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

bool is_same(const k::parse::ast::identifier_expr& ident1, const k::unit::name& ident2 ) {
    return is_same(ident1.qident, ident2);
}

//
// Parse identifiers
//

TEST_CASE( "Parse empty identifier", "[parser][expression][identifier]" ) {
    k::parse::parser parser("");
    auto expr = parser.parse_identifier_expr();

    REQUIRE( !expr );

}

TEST_CASE( "Parse identifier without prefix", "[parser][expression][identifier]" ) {
    k::parse::parser parser("first");
    auto expr = parser.parse_identifier_expr();

    auto identifier_expr = std::dynamic_pointer_cast<ast::identifier_expr>(expr);

    REQUIRE( identifier_expr );
    REQUIRE( identifier_expr->qident.has_root_prefix() == false );
    REQUIRE( identifier_expr->qident.names.size() == 1 );
    REQUIRE( identifier_expr->qident.names[0].content == "first" );
}

TEST_CASE( "Parse identifier with prefix", "[parser][expression][identifier]" ) {
    k::parse::parser parser("::top");
    auto expr = parser.parse_identifier_expr();

    auto identifier_expr = std::dynamic_pointer_cast<ast::identifier_expr>(expr);

    REQUIRE( identifier_expr );
    REQUIRE( identifier_expr->qident.has_root_prefix() == true );
    REQUIRE( identifier_expr->qident.size() == 1 );
    REQUIRE( identifier_expr->qident.names[0].content == "top" );
}

TEST_CASE( "Parse identifiers without prefix", "[parser][expression][identifier]" ) {
    k::parse::parser parser("first::second");
    auto expr = parser.parse_identifier_expr();

    auto identifier_expr = std::dynamic_pointer_cast<ast::identifier_expr>(expr);

    REQUIRE( identifier_expr );
    REQUIRE( identifier_expr->qident.initial_doublecolon.has_value() == false );
    REQUIRE( identifier_expr->qident.size() == 2 );
    REQUIRE( identifier_expr->qident[0] == "first" );
    REQUIRE( identifier_expr->qident[1] == "second" );
}

TEST_CASE( "Parse identifiers with prefix", "[parser][expression][identifier]" ) {
    k::parse::parser parser("::first::second");
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
    k::parse::parser parser("'a'");
    auto expr = parser.parse_primary_expr();
    REQUIRE( expr );

    auto lit = std::dynamic_pointer_cast<ast::literal_expr>(expr);
    REQUIRE( lit );
    REQUIRE( lit->literal.is<k::lex::character>() );

    auto c = lit->literal.get<k::lex::character>();
    REQUIRE( c.content == "'a'");
}

TEST_CASE( "Parse string primary expression", "[parser][expression][primary_expr]") {
    k::parse::parser parser("\"a b c\"");
    auto expr = parser.parse_primary_expr();
    REQUIRE( expr );

    auto lit = std::dynamic_pointer_cast<ast::literal_expr>(expr);
    REQUIRE( lit );
    REQUIRE( lit->literal.is<k::lex::string>() );

    auto c = lit->literal.get<k::lex::string>();
    REQUIRE( c.content == "\"a b c\"");
}

TEST_CASE( "Parse integer primary expression", "[parser][expression][primary_expr]") {
    k::parse::parser parser("1");
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
    k::parse::parser parser("this");
    auto expr = parser.parse_primary_expr();
    REQUIRE( expr );

    auto kw = std::dynamic_pointer_cast<ast::keyword_expr>(expr);
    REQUIRE( kw );
    REQUIRE( kw->keyword.type == k::lex::keyword::THIS );
}

TEST_CASE( "Parse parenthesis primary expression", "[parser][expression][primary_expr]") {
    k::parse::parser parser("( 1 )");
    auto expr = parser.parse_primary_expr();
    REQUIRE( expr );

    auto lit = std::dynamic_pointer_cast<ast::literal_expr>(expr);
    REQUIRE( lit );
    REQUIRE( lit->literal.is<k::lex::integer>() );

    auto i = lit->literal.get<k::lex::integer>();
    REQUIRE( i.content == "1");
}

TEST_CASE( "Parse identifier primary expression", "[parser][expression][primary_expr]") {
    k::parse::parser parser("( ident )");
    auto expr = parser.parse_primary_expr();
    REQUIRE( expr );

    auto ident = std::dynamic_pointer_cast<ast::identifier_expr>(expr);
    REQUIRE( ident );
    REQUIRE( is_same(*ident, k::unit::name(false, "ident") ) );
}

TEST_CASE( "Parse complex identifier primary expression", "[parser][expression][primary_expr]") {
    k::parse::parser parser("( ::ident :: ifier )");
    auto expr = parser.parse_primary_expr();
    REQUIRE( expr );

    auto ident = std::dynamic_pointer_cast<ast::identifier_expr>(expr);
    REQUIRE( ident );
    REQUIRE(  is_same(*ident, k::unit::name(true, {"ident", "ifier"})  ) );
}


//
// Postfix expr
//

TEST_CASE("Parse no postfix expression", "[parser][expression][postfix_expr]") {
    k::parse::parser parser("ident");
    auto expr = parser.parse_postfix_expr();
    REQUIRE( expr );

    auto ident = std::dynamic_pointer_cast<ast::identifier_expr>(expr);
    REQUIRE( ident );
    REQUIRE( is_same(*ident, k::unit::name(false, "ident") ) );
}

TEST_CASE("Parse ++ and -- postfix expression", "[parser][expression][postfix_expr]") {
    k::parse::parser parser("ident ++ --");
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
    REQUIRE( is_same(*ident, k::unit::name(false, "ident") ) );
}

#if TODO
TEST_CASE("Parse [] postfix expression", "[parser][expression][postfix_expr]") {
    k::parse::parser parser("ident [ 0 ]");
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

#if TODO
TEST_CASE("Parse () postfix expression", "[parser][expression][postfix_expr]") {
    k::parse::parser parser("ident ( 0 , 'a' )");
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
    k::parse::parser parser("ident");
    auto expr = parser.parse_unary_expr();
    REQUIRE( expr );

    auto ident = std::dynamic_pointer_cast<ast::identifier_expr>(expr);
    REQUIRE( ident );
    REQUIRE( is_same(*ident, k::unit::name(false, "ident") ) );
}



TEST_CASE("Parse no unary expression with postfix operator expr", "[parser][expression][unary_expr]") {
    k::parse::parser parser("ident ++");
    auto expr = parser.parse_unary_expr();
    REQUIRE( expr );

    auto unary_plus = std::dynamic_pointer_cast<ast::unary_postfix_expr>(expr);
    REQUIRE( unary_plus );
    REQUIRE( unary_plus->op == k::lex::operator_::DOUBLE_PLUS );
    REQUIRE( unary_plus->expr() );

    auto ident = std::dynamic_pointer_cast<ast::identifier_expr>(unary_plus->expr());
    REQUIRE( ident );
    REQUIRE( is_same(*ident, k::unit::name(false, "ident") ) );

}


TEST_CASE("Parse prefix operator unary expression", "[parser][expression][unary_expr]") {
    k::parse::parser parser("++ -- * & + - ! ~ ident");
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
    REQUIRE( is_same(*ident, k::unit::name(false, "ident") ) );
}


//
// Parse cast expression
//

TEST_CASE("Parse no cast expression", "[parser][expression][cast_expr]") {
    k::parse::parser parser("ident");
    auto expr = parser.parse_cast_expr();
    REQUIRE( expr );

    auto ident = std::dynamic_pointer_cast<ast::identifier_expr>(expr);
    REQUIRE( ident );
    REQUIRE( is_same(*ident, k::unit::name(false, "ident") ) );
}

TEST_CASE("Parse no cast expression with prefix and postfix operator", "[parser][expression][cast_expr]") {
    k::parse::parser parser("++ident++");
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
    REQUIRE( is_same(*ident, k::unit::name(false, "ident") ) );
}

TEST_CASE("Parse cast expression", "[parser][expression][cast_expr]") {
    k::parse::parser parser("(long)ident");
    auto expr = parser.parse_cast_expr();
    REQUIRE( expr );

    auto long_cast = std::dynamic_pointer_cast<ast::cast_expr>(expr);
    REQUIRE( long_cast );
    // TODO: REQUIRE( long_cast->type.name );
    REQUIRE( long_cast->expr() );

    auto ident = std::dynamic_pointer_cast<ast::identifier_expr>(long_cast->expr());
    REQUIRE( ident );
    REQUIRE( is_same(*ident, k::unit::name(false, "ident") ) );
}

TEST_CASE("Parse multiple cast expression", "[parser][expression][cast_expr]") {
    // TODO k::parse::parser parser("(int)(long) ident"); // NOTE: ")(" without space is not supported yet
    k::parse::parser parser("(int) (long) ident");
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
    REQUIRE( is_same(*ident, k::unit::name(false, "ident") ) );
}

//
// Parse PM expression
//

TEST_CASE("Parse no PM expression", "[parser][expression][pm_expr]") {
    k::parse::parser parser("ident");
    auto expr = parser.parse_pm_expr();
    REQUIRE( expr );

    auto ident = std::dynamic_pointer_cast<ast::identifier_expr>(expr);
    REQUIRE( ident );
    REQUIRE( is_same(*ident, k::unit::name(false, "ident") ) );
}


TEST_CASE("Parse dot-star PM expression", "[parser][expression][pm_expr]") {
    k::parse::parser parser("ident .* ifier");
    auto expr = parser.parse_pm_expr();
    REQUIRE( expr );

    auto pm = std::dynamic_pointer_cast<ast::binary_operator_expr>(expr);
    REQUIRE( pm );
    REQUIRE( pm->op == k::lex::operator_::DOT_STAR );

    auto ident = std::dynamic_pointer_cast<ast::identifier_expr>(pm->lexpr());
    REQUIRE( ident );
    REQUIRE( is_same(*ident, k::unit::name(false, "ident") ) );

    auto ifier = std::dynamic_pointer_cast<ast::identifier_expr>(pm->rexpr());
    REQUIRE( ifier );
    REQUIRE( is_same(*ifier, k::unit::name(false, "ifier") ) );
}

TEST_CASE("Parse arrow-star PM expression", "[parser][expression][pm_expr]") {
    k::parse::parser parser("ident->*ifier");
    auto expr = parser.parse_pm_expr();
    REQUIRE( expr );

    auto pm = std::dynamic_pointer_cast<ast::binary_operator_expr>(expr);
    REQUIRE( pm );

    REQUIRE( pm->op == k::lex::operator_::ARROW_STAR );

    auto ident = std::dynamic_pointer_cast<ast::identifier_expr>(pm->lexpr());
    REQUIRE( ident );
    REQUIRE( is_same(*ident, k::unit::name(false, "ident") ) );

    auto ifier = std::dynamic_pointer_cast<ast::identifier_expr>(pm->rexpr());
    REQUIRE( ifier );
    REQUIRE( is_same(*ifier, k::unit::name(false, "ifier") ) );
}

TEST_CASE("Parse PM expression", "[parser][expression][pm_expr]") {
    k::parse::parser parser("ident.*ifier->*other");
    auto expr = parser.parse_pm_expr();
    REQUIRE( expr );

    auto pm1 = std::dynamic_pointer_cast<ast::binary_operator_expr>(expr);
    REQUIRE( pm1 );

    REQUIRE( pm1->op == k::lex::operator_::DOT_STAR );

    auto ident = std::dynamic_pointer_cast<ast::identifier_expr>(pm1->lexpr());
    REQUIRE( ident );
    REQUIRE( is_same(*ident, k::unit::name(false, "ident") ) );

    auto pm2 = std::dynamic_pointer_cast<ast::binary_operator_expr>(pm1->rexpr());
    REQUIRE( pm2 );

    REQUIRE( pm2->op == k::lex::operator_::ARROW_STAR );

    auto ifier = std::dynamic_pointer_cast<ast::identifier_expr>(pm2->lexpr());
    REQUIRE( ifier );
    REQUIRE( is_same(*ifier, k::unit::name(false, "ifier") ) );

    auto other = std::dynamic_pointer_cast<ast::identifier_expr>(pm2->rexpr());
    REQUIRE( other );
    REQUIRE( is_same(*other, k::unit::name(false, "other") ) );
}


//
// Conditional expression
//

TEST_CASE("No conditional expression", "[parser][expression][conditional_expr]") {
    k::parse::parser parser("0");
    auto expr = parser.parse_conditional_expr();
    REQUIRE( expr );

    auto lit = std::dynamic_pointer_cast<ast::literal_expr>(expr);
    REQUIRE( lit );
    REQUIRE( lit->literal->content == "0" );
}

//
// Parse expression
//

TEST_CASE( "Parse expression", "[parser][expression]") {
    k::parse::parser parser("a + b * c");
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

//
// Parse variable declaration
//
TEST_CASE( "Parse variable declaration", "[parser][variable]") {
    k::parse::parser parser("static const plic : int = 0;");
    auto var = parser.parse_variable_decl();
    REQUIRE( var );
    REQUIRE( var->name.content == "plic" );

}


//
// Parse visibility declaration
//
TEST_CASE( "Parse public visibility declaration", "[parser][visibility]") {
    k::parse::parser parser("public:");
    auto var = parser.parse_visibility_decl();
    REQUIRE( var );
    REQUIRE( var->scope.type == k::lex::keyword::PUBLIC );
}

TEST_CASE( "Parse protected visibility declaration", "[parser][visibility]") {
    k::parse::parser parser("  protected  :  ");
    auto var = parser.parse_visibility_decl();
    REQUIRE( var );
    REQUIRE( var->scope.type == k::lex::keyword::PROTECTED );
}

TEST_CASE( "Parse private visibility declaration", "[parser][visibility]") {
    k::parse::parser parser("private:");
    auto var = parser.parse_visibility_decl();
    REQUIRE( var );
    REQUIRE( var->scope.type == k::lex::keyword::PRIVATE );
}
