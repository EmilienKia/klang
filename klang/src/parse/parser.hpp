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

#ifndef KLANG_PARSER_HPP
#define KLANG_PARSER_HPP

#include "../common/any_of.hpp"
#include "../lex/lexer.hpp"
#include "ast.hpp"
#include "../common/logger.hpp"

#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <variant>
#include <vector>


namespace k::parse {

/**
 * Lightweight lookup: lex + parse only the module declaration from a source.
 * Returns the module name if present, std::nullopt otherwise.
 * Does not report errors when the declaration is simply absent.
 * The source's line index is populated as a side-effect (lexing).
 *
 * @param src    Source to scan (must remain alive while the returned name is used).
 * @param logger Logger for potential lexer/parser errors.
 * @return       The module name, or std::nullopt if no 'module' declaration was found.
 */
std::optional<k::name> lookup_module_name(k::source& src, k::log::logger& logger);



class parsing_error : public k::log::compiler_error {
public:
    explicit parsing_error(k::log::diagnostic diag)
        : k::log::compiler_error(std::move(diag)) {}
};


class parser : protected log::logger_relay {
protected:
    lex::lexer _lexer;

    k::parse::ast::unit _unit;

    [[noreturn]] void throw_error(unsigned int code, const lex::opt_ref_any_lexeme& lexeme, const std::string& message, const std::vector<std::string>& args = {}) {
        k::lex::opt_any_lexeme opt = lexeme ? k::lex::opt_any_lexeme{lexeme->get()} : std::nullopt;
        auto diag = k::log::diagnostic::make_error(with_flag(code), message, args);
        if (opt) diag.at(*opt);
        logger_relay::report(diag);
        throw parsing_error(std::move(diag));
    }

public:

    parser(k::log::logger& logger);

    parser(k::log::logger& logger, k::source& src);


    void parse(k::source& src);

    /**
     * Unit = [ ModuleDeclaration ] , { ImportDeclaration } , { Declaration } ;
     *
     * @return The newly parsed model
     * @throws parsing_error If a parsing error occurs
     */
    std::shared_ptr<ast::unit> parse_unit();

    /**
     * ModuleDeclaration = 'module' , QualifiedIdentifier , ';' ;
     *
     * @return The module declaration, if any, null if not present.
     * @throws parsing_error If a parsing error occurs.
     */
    std::shared_ptr<ast::module_name> parse_module_declaration();

    /**
     * ImportDeclaration = 'import' , QualifiedIdentifier , ';' ;
     *
     * @return An import declaration, if any, null if not present.
     * @throws parsing_error If a parsing error occurs.
     */
    std::shared_ptr<ast::import> parse_import();

    /**
     * { Declaration }
     */
    std::vector<ast::decl_ptr> parse_declarations();

    /**
     * Declaration = VisibilityDecl | NamespaceDecl | UsingDecl | FriendDecl
     *             | AggregateDecl | EnumDecl | FunctionDecl | VariableDecl ;
     */
    ast::decl_ptr parse_declaration();

    /**
     * VisibilityDecl = ( 'public' | 'protected' | 'private' ) , ':' ;
     */
    std::shared_ptr<ast::visibility_decl> parse_visibility_decl();

    /**
     * NamespaceDecl = 'namespace' , [ Identifier ] , '{' , { Declaration } , '}' ;
     */
    std::shared_ptr<ast::namespace_decl> parse_namespace_decl();

    /**
     * UsingDecl = 'using' , [ UsingFilter ] , [ Identifier , '=' ] ,
     *             QualifiedIdentifier , ';' ;
     * UsingFilter = 'namespace' | 'struct' | 'interface' | 'class' ;
     */
    std::shared_ptr<ast::using_decl> parse_using_decl();

    /**
     * FriendDecl = 'friend' , [ FriendFilter ] , QualifiedIdentifier , ';' ;
     * FriendFilter = 'struct' | 'interface' | 'class' ;
     */
    std::shared_ptr<ast::friend_decl> parse_friend_decl();

    /**
     * AnnotationDef = '@' , QualifiedIdentifier
     *               | '@' , QualifiedIdentifier , '(' , [ ExpressionList ] , ')'
     *               | '@' , QualifiedIdentifier , BraceInitList ;
     */
    std::shared_ptr<ast::annotation_def> parse_annotation_def();

    /**
     * { AnnotationDef }
     */
    ast::annotation_def_list parse_annotation_defs();

    /**
     * AggregateDecl = { AnnotationDef } , { Specifier } ,
     *                 ( 'struct' | 'class' | 'interface' | 'annotation' ) ,
     *                 Identifier , [ ':' , BaseClause ] ,
     *                 '{' , { Declaration } , '}' ;
     * BaseClause = BaseSpec , { ',' , BaseSpec } ;
     * BaseSpec = [ 'public' | 'protected' | 'private' ] , QualifiedIdentifier ;
     */
    std::shared_ptr<ast::aggregate_decl> parse_aggregate_decl();

    /**
     * EnumDecl = { Specifier } , 'enum' , Identifier ,
     *            [ ':' , QualifiedIdentifier ] ,
     *            '{' , { EnumEntry } , '}' , ';' ;
     * EnumEntry = Identifier , [ '=' , ( IntegerLiteral | Identifier ) ] ,
     *             [ 'default' ] , ';' ;
     */
    std::shared_ptr<ast::enum_decl> parse_enum_decl();

    /**
     * QualifiedIdentifier = [ '::' ] , Identifier , { '::' , Identifier } ;
     *
     * @return The qualified identifier, if parsed correctly completely.
     * @throws parsing_error If a parsing error occurs
     */
    std::shared_ptr<ast::qualified_identifier> parse_qualified_identifier();

    /**
     * FunctionDecl = { AnnotationDef } , { Specifier } , [ 'fun' ] ,
     *               ( FunctionHead | OperatorFunctionHead | DestructorHead ) ,
     *               '(' , [ ParameterList ] , ')' ,
     *               [ NamedReturnVar ] ,
     *               [ ':' , ReturnTypeOrMemberInitList ] ,
     *               FunctionBody ;
     *
     * FunctionHead          = Identifier ;
     * DestructorHead        = '~' , Identifier ;
     * OperatorFunctionHead  = 'operator' , OperatorSymbol
     *                       | 'operator' , '(' , ')' ;
     * NamedReturnVar        = Identifier , ':' , TypeSpec , [ NamedReturnInit ] ;
     * NamedReturnInit       = '=' , ConditionalExpr
     *                       | '(' , [ ExpressionList ] , ')' ;
     * ReturnTypeOrMemberInitList = TypeSpec | MemberInitList | StaticDepList ;
     * MemberInitList        = MemberInit , { ',' , MemberInit } ;
     * MemberInit            = Identifier , '(' , [ ExpressionList ] , ')' ;
     * FunctionBody          = BlockStatement
     *                       | '->' , ( 'default' | 'delete' ) , ';'
     *                       | '->' , QualifiedIdentifier ,
     *                                [ '(' , [ TypeSpecList ] , ')' ] , ';'
     *                       | ';' ;
     */
    std::shared_ptr<ast::function_decl> parse_function_decl();

    /**
     * ParameterSpec = { AnnotationDef } , { Specifier } ,
     *                 [ Identifier , ':' ] , TypeSpec ,
     *                 [ '=' , ConditionalExpr ] ;
     */
    std::shared_ptr<ast::parameter_spec> parse_parameter_spec();

    /**
     * VariableDecl = { Specifier } , Identifier , ':' , TypeSpec ,
     *                [ Initialiser ] , ';' ;
     * Initialiser = '=' , ConditionalExpr
     *             | '(' , [ ExpressionList ] , ')' , [ '[' , ConditionalExpr , ']' ]
     *             | BraceInitList ;
     */
    std::shared_ptr<ast::variable_decl> parse_variable_decl();

    /**
     * TypeSpec = FunctionRefType
     *          | QualifiedIdentifier , '::' , FunctionRefType
     *          | [ 'const' ] , ( FundamentalTypeSpec | QualifiedIdentifier ) ,
     *            { TypeSuffix } ;
     * TypeSuffix = '[' , [ IntegerLiteral ] , ']'
     *            | '!' | '*' | '&' | '+' | '?' | '#' ;
     *
     * @param stop_before_bracket  When true, the parser will NOT consume array suffixes '[...]'.
     *        Used by the 'new' expression handler to parse base types separately from array sizes.
     */
    std::shared_ptr<ast::type_specifier> parse_type_spec(bool stop_before_bracket = false);

    /**
     * FundamentalTypeSpec = [ 'unsigned' ] , ( 'byte' | 'char' | 'short' | 'int'
     *                       | 'long' | 'float' | 'double' )
     *                     | 'bool' ;
     */
    std::shared_ptr<ast::type_specifier> parse_fundamental_type_spec();

    /**
     * { Specifier }
     * Specifier = 'public' | 'protected' | 'private'
     *           | 'static' | 'const' | 'abstract' | 'final' ;
     */
    std::vector<lex::keyword> parse_specifiers();

    /**
     * BlockStatement = '{' , { Statement } , '}' ;
     */
    std::shared_ptr<ast::block_statement> parse_statement_block();

    /**
     * ReturnStatement = 'return' , [ Expression ] , ';' ;
     */
     std::shared_ptr<ast::return_statement> parse_return_statement();

     /**
      * IfElseStatement = 'if' , '(' , Expression , ')' , Statement ,
      *                   [ 'else' , Statement ] ;
      */
     std::shared_ptr<ast::if_else_statement> parse_if_else_statement();

    /**
     * WhileStatement = 'while' , '(' , Expression , ')' , Statement ;
     */
    std::shared_ptr<ast::while_statement> parse_while_statement();

    /**
     * ForStatement = 'for' , '(' , ( VariableDecl | ';' ) ,
     *                              [ Expression ] , ';' ,
     *                              [ Expression ] ,
     *                       ')' , Statement ;
     */
    std::shared_ptr<ast::for_statement> parse_for_statement();

    /**
     * Statement = BlockStatement | ReturnStatement | IfElseStatement
     *           | WhileStatement | ForStatement | UsingDecl
     *           | VariableDecl | ExpressionStatement ;
     */
    std::shared_ptr<ast::statement> parse_statement();

    /**
     * ExpressionStatement = [ Expression ] , ';' ;
     */
    std::shared_ptr<ast::expression_statement> parse_expression_statement();

    /**
     * Expression = AssignmentExpr , { ',' , AssignmentExpr } ;
     */
    ast::expr_ptr parse_expression();

    /**
     * ExpressionList = AssignmentExpr , { ',' , AssignmentExpr } ;
     */
    ast::expr_ptr parse_expression_list();

    /**
     * AssignmentExpr = ConditionalExpr , [ AssignmentOperator , AssignmentExpr ] ;
     * AssignmentOperator = '=' | '*=' | '/=' | '%=' | '+=' | '-='
     *                    | '>>=' | '<<=' | '&=' | '^=' | '|=' ;
     */
    ast::expr_ptr parse_assignment_expression();

    /**
     * ConditionalExpr = LogicalOrExpr ,
     *                   [ '?' , ConditionalExpr , ':' , ConditionalExpr ] ;
     */
    ast::expr_ptr parse_conditional_expr();

    /**
     * LogicalOrExpr = LogicalAndExpr , { '||' , LogicalAndExpr } ;
     */
    ast::expr_ptr  parse_logical_or_expression();

    /**
     * LogicalAndExpr = InclusiveBinOrExpr , { '&&' , InclusiveBinOrExpr } ;
     */
    ast::expr_ptr  parse_logical_and_expression();

    /**
     * InclusiveBinOrExpr = ExclusiveBinOrExpr , { '|' , ExclusiveBinOrExpr } ;
     */
    ast::expr_ptr parse_inclusive_bin_or_expr();

    /**
     * ExclusiveBinOrExpr = BinAndExpr , { '^' , BinAndExpr } ;
     */
    ast::expr_ptr parse_exclusive_bin_or_expr();

    /**
     * BinAndExpr = EqualityExpr , { '&' , EqualityExpr } ;
     */
    ast::expr_ptr parse_bin_and_expr();

    /**
     * EqualityExpr = RelationalExpr , { ( '==' | '!=' ) , RelationalExpr } ;
     */
    ast::expr_ptr parse_equality_expr();

    /**
     * RelationalExpr = ShiftingExpr , { ( '<' | '>' | '<=' | '>=' ) , ShiftingExpr } ;
     */
    ast::expr_ptr parse_relational_expr();

    /**
     * ShiftingExpr = AdditiveExpr , { ( '<<' | '>>' ) , AdditiveExpr } ;
     */
    ast::expr_ptr parse_shifting_expr();

    /**
     * AdditiveExpr = MultiplicativeExpr , { ( '+' | '-' ) , MultiplicativeExpr } ;
     */
    ast::expr_ptr parse_additive_expr();

    /**
     * MultiplicativeExpr = PmExpr , { ( '*' | '/' | '%' ) , PmExpr } ;
     */
    ast::expr_ptr parse_multiplicative_expr();

    /**
     * PmExpr = CastExpr , { ( '.*' | '->*' ) , CastExpr } ;
     */
    ast::expr_ptr parse_pm_expr();

    /**
     * CastExpr = '(' , TypeSpec , ')' , CastExpr
     *          | UnaryExpr ;
     */
    ast::expr_ptr parse_cast_expr();

    /**
     * UnaryExpr = ( '++' | '--' | '*' | '&' | '+' | '-' | '!' | '~' | '#' ) , CastExpr
     *           | NewExpr
     *           | 'delete' , CastExpr
     *           | PostfixExpr ;
     *
     * NewExpr = 'new' , TypeName , '(' , [ ExpressionList ] , ')'
     *         | 'new' , TypeName , '(' , [ ExpressionList ] , ')' , '[' , Expression , ']'
     *         | 'new' , TypeName , '[' , [ Expression ] , ']' , [ BraceInitList ]
     *         | 'new' , TypeName , BraceInitList ;
     */
    ast::expr_ptr parse_unary_expr();

    /**
     * BraceInitList = '{' , '}'
     *               | '{' , InitElement , { ',' , InitElement } , '}'
     *               | '{' , DesignatedInitElement , { ',' , DesignatedInitElement } , '}' ;
     *
     * The opening brace token has already been consumed (passed as open_brace).
     */
    std::shared_ptr<ast::brace_init_list> parse_brace_init_list(const lex::punctuator& open_brace);

    /**
     * PostfixExpr = PrimaryExpr , { PostfixOp } ;
     * PostfixOp = '++'
     *           | '--'
     *           | '[' , Expression , ']'
     *           | '(' , [ ExpressionList ] , ')'
     *           | ( '.' | '->' ) , IdentifierExpr ;
     */
    ast::expr_ptr parse_postfix_expr();

    /**
     * PrimaryExpr = Literal
     *             | 'this'
     *             | '(' , Expression , ')'
     *             | AnnotationInitExpr
     *             | BraceInitList
     *             | IdentifierExpr ;
     */
    ast::expr_ptr parse_primary_expr();

    /**
     * IdentifierExpr = QualifiedIdentifier ;
     */
    ast::expr_ptr parse_identifier_expr();

};


} // k::parse
#endif //KLANG_PARSER_HPP
