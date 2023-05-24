//
// Created by Emilien Kia <emilien.kia+dev@gmail.com>.
//

#ifndef KLANG_PARSER_HPP
#define KLANG_PARSER_HPP

#include "any_of.hpp"
#include "lexer.hpp"
#include "ast.hpp"

#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <variant>
#include <vector>


namespace k::parse {


class parsing_error : public std::runtime_error {
protected:
    const lex::lexeme* _lexeme;
public:
    parsing_error(const std::string &arg);
    parsing_error(const char *string);

    parsing_error(const std::string &arg, const lex::lexeme& lexeme);
    parsing_error(const char *string, const lex::lexeme& lexeme);

};



class parser {
protected:
    lex::lexer _lexer;

    k::parse::ast::unit _unit;


public:

    parser() = default;
    parser(std::string_view src);


    ast::unit parse(std::string_view src);



    /**
     * UNIT := ?MODULE_DECLARATION *IMPORT DECLARATIONS
     * @return
     */
    ast::unit parse_unit();

    /**
     * MODULE_DECLARATION := 'module' QUALIFIED_IDENTIFIER ';'
     * @return
     */
    std::optional<ast::qualified_identifier> parse_module_declaration();

    /**
     * IMPORT := 'import' identifier ';'
     * @return
     */
    std::optional<ast::import> parse_import();

    /**
     * DECLARATIONS := *DECLARATION
     */
    std::vector<ast::decl_ptr> parse_declarations();

    /**
     *  DECLARATION := VISIBILITY_DECL | NAMESPACE_DECL | FUNCTION_DECL | VARIABLE_DECL
     * @return
     */
    ast::decl_ptr parse_declaration();

    /**
     * VISIBILITY_DECL := ('public'|'protected'|'private') ':'
     * @return
     */
    std::optional<ast::visibility_decl> parse_visibility_decl();

    /**
     * NAMESPACE_DECL := 'namespace' ?identifier '{' *DECLARATION '}'
     * @return
     */
    std::optional<ast::namespace_decl> parse_namespace_decl();

    /**
     * QUALIFIED_IDENTIFIER := ?'::' identifier *( '::' identifier )
     * @return
     */
    std::optional<ast::qualified_identifier> parse_qualified_identifier();

    /**
     * Current support : FUNCTION_DECL := SPECIFIERS identifier '(' [ PARAMETER *[',' PARAMETER ] ] ')' ?[':' TYPE_SPEC] (';' | STATEMENT_BLOCK )
     * @return
     */
    std::optional<ast::function_decl> parse_function_decl();

    /**
     * Current support : PARAMETER := SPECIFIERS ?[identifier ':'] TYPE_SPEC ';'
     */
    std::optional<ast::parameter_spec> parse_parameter_spec();

    /**
     * Current support : VARIABLE_DECL := SPECIFIERS identifier ':' TYPE_SPEC ?['=' CONDITIONAL_EXPR] ';'
     * TODO VARIABLE_DECL := SPECIFIERS identifier ?[':' TYPE_SPEC ?[(INITIALIZER)] ['=' CONDITIONAL_EXPR]
     */
    std::optional<ast::variable_decl> parse_variable_decl();

    /**
     * Current support : TYPE_SPEC := ('byte'|'char'|'short'|'int'|'long'|'float'|'double')
     *                                  | QUALIFIED_IDENTIFIER
     * TODO support : support unsigned prefix correctly
     */
     std::shared_ptr<ast::type_specifier> parse_type_spec();

    /**
     * SPECIFIERS := *('public'|'protected'|'private'|'static'|'const'|'abstract'|'final')
     * @return
     */
    std::vector<lex::keyword> parse_specifiers();

    /**
     * STATEMENT_BLOCK := '{' *STATEMENT '}'
     * @return
     */
    std::optional<ast::block_statement> parse_statement_block();

    /**
     * RETURN_STATEMENT := 'return' ?[EXPRESSION] ';'
     * @return
     */
     std::optional<ast::return_statement> parse_return_statement();

    /**
     * STATEMENT := STATEMENT_BLOCK | RETURN_STATEMENT | VARIABLE_DECL | EXPRESSION_STATEMENT
     * @return
     */
    ast::any_statement_opt parse_statement();

    /**
     * EXPRESSION_STATEMENT := ?[EXPRESSION] ';'
     */
    std::optional<ast::expression_statement> parse_expression_statement();

    /**
     * EXPRESSION := ASSIGNMENT_EXPR *[ ',' ASSIGNMENT_EXPR]
     */
    ast::expr_ptr parse_expression();

    /**
     * EXPRESSION_LIST := ASSIGNMENT_EXPR *[ ',' ASSIGNMENT_EXPR]
     */
    ast::expr_ptr parse_expression_list();

    /**
     * ASSIGNMENT_EXPR := CONDITIONAL_EXPR ?[ ASSIGNMENT_OPERATOR ASSIGNMENT_EXPR ]
     * ASSIGNMENT_OPERATOR := one of = *= /= %= += -= >>= <<= &= ^= |=
     */
    ast::expr_ptr parse_assignment_expression();

    /**
     * CONDITIONAL_EXPR := LOGICAL_OR_EXPR ?[ '?' CONDITIONAL_EXPR ':' CONDITIONAL_EXPR]
     * @return
     */
    ast::expr_ptr parse_conditional_expr();

    /**
     * LOGICAL_OR_EXPR := LOGICAL_AND_EXPR *[ '||' LOGICAL_AND_EXPR]
     * aka
     * LOGICAL_OR_EXPR := LOGICAL_AND_EXPR ?[ '||' LOGICAL_OR_EXPR]
     * @return
     */
    ast::expr_ptr  parse_logical_or_expression();

    /**
     * LOGICAL_AND_EXPR := INCLUSIVE_BIN_OR_EXPR *[ '&&' INCLUSIVE_BIN_OR_EXPR]
     * aka
     * LOGICAL_AND_EXPR := INCLUSIVE_BIN_OR_EXPR ?[ '&&' LOGICAL_AND_EXPR]
     * @return
     */
    ast::expr_ptr  parse_logical_and_expression();

    /**
     * INCLUSIVE_BIN_OR_EXPR := EXCLUSIVE_BIN_OR_EXPR *[ '|' EXCLUSIVE_BIN_OR_EXPR]
     * aka
     * INCLUSIVE_BIN_OR_EXPR := EXCLUSIVE_BIN_OR_EXPR ?[ '|' INCLUSIVE_BIN_OR_EXPR]
     * @return
     */
    ast::expr_ptr parse_inclusive_bin_or_expr();

    /**
     * EXCLUSIVE_BIN_OR_EXPR := BIN_AND_EXPR *[ '^' BIN_AND_EXPR]
     * aka
     * EXCLUSIVE_BIN_OR_EXPR := BIN_AND_EXPR ?[ '^' EXCLUSIVE_BIN_OR_EXPR]
     * @return
     */
    ast::expr_ptr parse_exclusive_bin_or_expr();

    /**
     * BIN_AND_EXPR := EQUALITY_EXPR *[ '&' EQUALITY_EXPR]
     * aka
     * BIN_AND_EXPR := EQUALITY_EXPR ?[ '&' BIN_AND_EXPR]
     * @return
     */
    ast::expr_ptr parse_bin_and_expr();

    /**
     * EQUALITY_EXPR := RELATIONAL_EXPR *[ ('=='|'!=') RELATIONAL_EXPR]
     * aka
     * EQUALITY_EXPR := RELATIONAL_EXPR ?[ ('=='|'!=') EQUALITY_EXPR]
     * @return
     */
    ast::expr_ptr parse_equality_expr();

    /**
     * RELATIONAL_EXPR := SHIFTING_EXPR *[ ('<'|'>'|'<='|'>=') SHIFTING_EXPR]
     * aka
     * RELATIONAL_EXPR := SHIFTING_EXPR ?[ ('<'|'>'|'<='|'>=') RELATIONAL_EXPR]
     * @return
     */
    ast::expr_ptr parse_relational_expr();

    /**
     * SHIFTING_EXPR := ADDITIVE_EXPR *[ ('<<'|'>>') ADDITIVE_EXPR]
     * aka
     * * SHIFTING_EXPR := ADDITIVE_EXPR ?[ ('<<'|'>>') SHIFTING_EXPR]
     * @return
     */
    ast::expr_ptr parse_shifting_expr();

    /**
     * ADDITIVE_EXPR := MULTIPLICATIVE_EXPR *[ ('+'|'-') MULTIPLICATIVE_EXPR]
     * aka
     * ADDITIVE_EXPR := MULTIPLICATIVE_EXPR ?[ ('+'|'-') ADDITIVE_EXPR]
     * @return
     */
    ast::expr_ptr parse_additive_expr();

    /**
     * MULTIPLICATIVE_EXPR := PM_EXPR *[ ('*'|'/'|'%') PM_EXPR]
     * aka
     * MULTIPLICATIVE_EXPR := PM_EXPR ?[ ('*'|'/'|'%') MULTIPLICATIVE_EXPR]
     * @return
     */
    ast::expr_ptr parse_multiplicative_expr();

    /**
     * PM_EXPR := CAST_EXPR *[ ('.*'|'->*') CAST_EXPR]
     * aka
     * PM_EXPR := CAST_EXPR ?[ ('.*'|'->*') PM_EXPR]
     * @return
     */
    ast::expr_ptr parse_pm_expr();

    /**
     * CAST_EXPR := '(' TYPE_SPECIFIER ')' CAST_EXPR
     *            | UNARY_EXPR
     * @return
     */
    ast::expr_ptr parse_cast_expr();

    /**
     * UNARY_EXPR := ('++'|'--'|'*'|'&'|'+'|'-'|'!'|'~') CAST_EXPR
     *             | POSTFIX_EXPR
     * TODO: support keyword operators like new, delete, sizeof ...
     * @return
     */
    ast::expr_ptr parse_unary_expr();

    /**
     * POSTFIX_EXPR := PRIMARY_EXPR *[ '++'|'--'
     *                                  | [ '[' EXPRESSION ']' ]
     *                                  | [ '(' EXPRESSION_LIST ')' ]
     *                               ]
     * TODO: support braces and point/Arrow
     * @return
     */
    ast::expr_ptr parse_postfix_expr();

    /**
     * PRIMARY_EXPR :=  LITERAL
     *              |   'this'
     *              |   '(' Expression  ')'
     *              |   IDENTIFIER_EXPRESSION
     * @return
     */
    ast::expr_ptr parse_primary_expr();

    /**
     * IDENTIFIER_EXPR := ?QUALIFIED_IDENTIFIER
     * @return
     */
    ast::expr_ptr parse_identifier_expr();

};


} // k::parse
#endif //KLANG_PARSER_HPP
