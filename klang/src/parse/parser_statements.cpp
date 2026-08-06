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

#include "parser.hpp"
#include <deque>
#include "../common/logger.hpp"
#include "../errors.hpp"

namespace k::parse {

std::shared_ptr<ast::block_statement> parser::parse_statement_block()
{
    lex::lex_holder holder(_lexer);

    // Look for open brace
    std::optional<lex::punctuator> open_brace;
    if(auto lopenbrace = _lexer.get(); lopenbrace==lex::punctuator::BRACE_OPEN) {
        open_brace = lex::as<lex::punctuator>(lopenbrace);
    } else {
        holder.rollback();
        return {};
        // Err: statement block requires a opening brace.
        //throw parsing_error("Closing brace for statement block is missing" /*, *lopenbrace */);
    }

    trace("[parser::parse_statement_block] parsing statement block", {});

    std::vector<std::shared_ptr<ast::statement>> statements;
    while(auto statement = parse_statement()) {
        if(statement) {
            statements.push_back(statement);
        }
    }

    // Look for closing brace
    std::optional<lex::punctuator> close_brace;
    if(auto lclosebrace = _lexer.get(); lclosebrace == lex::punctuator::BRACE_CLOSE) {
        close_brace = lex::as<lex::punctuator>(lclosebrace);
    } else {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_BLOCK_MISSING_CLOSE_BRACE), _lexer.pick_current(), "Block is expecting a closing brace '}'");
    }

    return std::make_shared<ast::block_statement>(*open_brace, *close_brace, statements);
}

std::shared_ptr<ast::return_statement> parser::parse_return_statement()
{
    lex::lex_holder holder(_lexer);

    std::optional<lex::keyword> ret;
    if(auto lreturn = _lexer.get(); lreturn==lex::keyword::RETURN) {
        ret = lex::as<lex::keyword>(lreturn);
    } else {
        holder.rollback();
        return {};
    }

    ast::expr_ptr expr = parse_expression();

    auto lsemicolon = _lexer.get();
    if(lsemicolon!=lex::punctuator::SEMICOLON) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_RETURN_MISSING_SEMICOLON), _lexer.pick_current(), "Return statement is expecting to finish by a semicolon ';'");
    }

    return std::make_shared<ast::return_statement>(*ret, expr);

}

std::shared_ptr<ast::break_statement> parser::parse_break_statement()
{
    lex::lex_holder holder(_lexer);

    auto lbreak = _lexer.get();
    if(lbreak != lex::keyword::BREAK) {
        holder.rollback();
        return {};
    }

    auto lsemicolon = _lexer.get();
    if(lsemicolon != lex::punctuator::SEMICOLON) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_BREAK_MISSING_SEMICOLON), _lexer.pick_current(), "Break statement is expecting to finish by a semicolon ';'");
    }

    return std::make_shared<ast::break_statement>(lex::as<lex::keyword>(lbreak));
}

std::shared_ptr<ast::continue_statement> parser::parse_continue_statement()
{
    lex::lex_holder holder(_lexer);

    auto lcontinue = _lexer.get();
    if(lcontinue != lex::keyword::CONTINUE) {
        holder.rollback();
        return {};
    }

    auto lsemicolon = _lexer.get();
    if(lsemicolon != lex::punctuator::SEMICOLON) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_CONTINUE_MISSING_SEMICOLON), _lexer.pick_current(), "Continue statement is expecting to finish by a semicolon ';'");
    }

    return std::make_shared<ast::continue_statement>(lex::as<lex::keyword>(lcontinue));
}

std::shared_ptr<ast::throw_statement> parser::parse_throw_statement()
{
    lex::lex_holder holder(_lexer);

    auto lthrow = _lexer.get();
    if(lthrow != lex::keyword::THROW) {
        holder.rollback();
        return {};
    }

    // Allow bare "throw;" for rethrow (no expression)
    auto peek = _lexer.pick_current();
    if(peek == lex::punctuator::SEMICOLON) {
        _lexer.get(); // consume semicolon
        return std::make_shared<ast::throw_statement>(lex::as<lex::keyword>(lthrow), nullptr);
    }

    auto expr = parse_expression();
    if(!expr) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_THROW_EXPECT_EXPRESSION),
                    _lexer.pick_current(), "Throw statement expects an expression or a semicolon ';'");
    }

    auto lsemicolon = _lexer.get();
    if(lsemicolon != lex::punctuator::SEMICOLON) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_THROW_MISSING_SEMICOLON),
                    _lexer.pick_current(), "Throw statement expects a semicolon ';'");
    }

    return std::make_shared<ast::throw_statement>(lex::as<lex::keyword>(lthrow), expr);
}

std::shared_ptr<ast::catch_clause> parser::parse_catch_clause()
{
    lex::lex_holder holder(_lexer);

    auto lcatch = _lexer.get();
    if(lcatch != lex::keyword::CATCH) {
        holder.rollback();
        return {};
    }

    auto lpopen = _lexer.get();
    if(lpopen != lex::punctuator::PARENTHESIS_OPEN) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_CATCH_EXPECT_OPEN_PAREN),
                    lpopen, "Catch clause expects an open parenthesis '('");
    }

    // Optional 'const'
    bool is_const = false;
    {
        lex::lex_holder const_holder(_lexer);
        auto lconst = _lexer.get();
        if(lconst == lex::keyword::CONST) {
            is_const = true;
        } else {
            const_holder.rollback();
        }
    }

    // Identifier
    auto lname = _lexer.get();
    if(lex::is_not<lex::identifier>(lname)) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_CATCH_EXPECT_IDENTIFIER),
                    lname, "Catch clause expects a variable name");
    }
    auto var_name = lex::as<lex::identifier>(lname);

    // Colon
    auto lcolon = _lexer.get();
    if(lcolon != lex::operator_::COLON) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_CATCH_EXPECT_COLON),
                    lcolon, "Catch clause expects ':' after variable name");
    }

    // Type specifier
    auto var_type = parse_type_spec();
    if(!var_type) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_CATCH_EXPECT_TYPE),
                    _lexer.pick_current(), "Catch clause expects a type specifier");
    }

    // Close parenthesis
    auto lpclose = _lexer.get();
    if(lpclose != lex::punctuator::PARENTHESIS_CLOSE) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_CATCH_EXPECT_CLOSE_PAREN),
                    lpclose, "Catch clause expects a close parenthesis ')'");
    }

    // Body block
    auto body = parse_statement_block();
    if(!body) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_CATCH_EXPECT_BODY),
                    _lexer.pick_current(), "Catch clause expects a block statement body");
    }

    return std::make_shared<ast::catch_clause>(lex::as<lex::keyword>(lcatch), is_const,
                                                var_name, var_type, body);
}

std::shared_ptr<ast::try_catch_statement> parser::parse_try_catch_statement()
{
    lex::lex_holder holder(_lexer);

    auto ltry = _lexer.get();
    if(ltry != lex::keyword::TRY) {
        holder.rollback();
        return {};
    }

    auto try_body = parse_statement_block();
    if(!try_body) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_TRY_EXPECT_BODY),
                    _lexer.pick_current(), "Try statement expects a block statement body");
    }

    // Parse at least one catch clause
    std::vector<std::shared_ptr<ast::catch_clause>> catch_clauses;
    while(auto clause = parse_catch_clause()) {
        catch_clauses.push_back(clause);
    }

    // Parse optional finally clause
    std::shared_ptr<ast::block_statement> finally_body;
    {
        lex::lex_holder finally_holder(_lexer);
        auto lfinally = _lexer.get();
        if(lfinally == lex::keyword::FINALLY) {
            finally_body = parse_statement_block();
            if(!finally_body) {
                throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_TRY_EXPECT_FINALLY_BODY),
                            _lexer.pick_current(), "Finally clause expects a block statement body");
            }
        } else {
            finally_holder.rollback();
        }
    }

    if(catch_clauses.empty() && !finally_body) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_TRY_EXPECT_CATCH),
                    _lexer.pick_current(), "Try statement requires at least one catch clause or a finally clause");
    }

    return std::make_shared<ast::try_catch_statement>(lex::as<lex::keyword>(ltry), try_body, catch_clauses, finally_body);
}

std::shared_ptr<ast::if_else_statement> parser::parse_if_else_statement() {
    lex::lex_holder holder(_lexer);

    auto lif = _lexer.get();
    if(lif != lex::keyword::IF) {
        holder.rollback();
        return {};
    }

    auto lpopen = _lexer.get();
    if(lpopen != lex::punctuator::PARENTHESIS_OPEN) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_IF_EXPECT_OPEN_PAREN), lpopen, "If statement expect an open parenthesis '(' after the 'if' keyword for the tested expression");
    }

    // Try to parse condition variable declaration(s) (if-let / if(vars; test) form):
    //   if (name : type = expr) { ... }                          — single var, classic if-let
    //   if (name : type = expr; test) { ... }                    — single var + test
    //   if (v1 : T1 = e1; v2 : T2 = e2; ...; test) { ... }     — multi var + test
    std::vector<std::shared_ptr<ast::variable_decl>> cond_vars;
    std::shared_ptr<ast::expression> test_expr;
    {
        lex::lex_holder var_holder(_lexer);
        bool parsing_vars = true;

        while(parsing_vars) {
            lex::lex_holder single_var_holder(_lexer);

            std::vector<lex::keyword> specifiers = parse_specifiers();

            auto lname = _lexer.get();
            if(!lex::is<lex::identifier>(lname)) {
                // Not a variable declaration — if we already have vars, treat as test expr
                single_var_holder.rollback();
                if(!cond_vars.empty()) {
                    // Parse what follows as the test expression
                    test_expr = parse_conditional_expr();
                    if(!test_expr) {
                        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_IF_EXPECT_CONDITION), _lexer.pick_current(), "If statement expects a test expression after ';'");
                    }
                    auto lpclose_final = _lexer.get();
                    if(lpclose_final != lex::punctuator::PARENTHESIS_CLOSE) {
                        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_IF_EXPECT_CLOSE_PAREN), lpclose_final, "If statement expect a close parenthesis ')' after the test expression");
                    }
                    var_holder.sync();
                }
                parsing_vars = false;
                break;
            }

            auto lcolon = _lexer.get();
            if(lcolon != lex::operator_::COLON) {
                // Not a variable declaration — rollback this attempt
                single_var_holder.rollback();
                if(!cond_vars.empty()) {
                    // Parse what follows as the test expression
                    test_expr = parse_conditional_expr();
                    if(!test_expr) {
                        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_IF_EXPECT_CONDITION), _lexer.pick_current(), "If statement expects a test expression after ';'");
                    }
                    auto lpclose_final = _lexer.get();
                    if(lpclose_final != lex::punctuator::PARENTHESIS_CLOSE) {
                        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_IF_EXPECT_CLOSE_PAREN), lpclose_final, "If statement expect a close parenthesis ')' after the test expression");
                    }
                    var_holder.sync();
                }
                parsing_vars = false;
                break;
            }

            auto type = parse_type_spec();
            if(!type) {
                single_var_holder.rollback();
                if(!cond_vars.empty()) {
                    test_expr = parse_conditional_expr();
                    if(!test_expr) {
                        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_IF_EXPECT_CONDITION), _lexer.pick_current(), "If statement expects a test expression after ';'");
                    }
                    auto lpclose_final = _lexer.get();
                    if(lpclose_final != lex::punctuator::PARENTHESIS_CLOSE) {
                        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_IF_EXPECT_CLOSE_PAREN), lpclose_final, "If statement expect a close parenthesis ')' after the test expression");
                    }
                    var_holder.sync();
                }
                parsing_vars = false;
                break;
            }

            bool is_constructor = false;
            bool is_brace_init = false;
            ast::expr_ptr init_expr;
            auto lequal_or_openp = _lexer.get();
            if(lequal_or_openp == lex::operator_::EQUAL) {
                init_expr = parse_conditional_expr();
            } else if(lequal_or_openp == lex::punctuator::PARENTHESIS_OPEN) {
                // Constructor init form: T(args...)
                std::vector<ast::expr_ptr> paren_args;
                auto lclose_or_first = _lexer.get();
                if (lclose_or_first != lex::punctuator::PARENTHESIS_CLOSE) {
                    _lexer.unget();
                    while (true) {
                        auto arg = parse_conditional_expr();
                        paren_args.push_back(arg);
                        auto sep = _lexer.get();
                        if (sep == lex::punctuator::PARENTHESIS_CLOSE) break;
                        if (sep != lex::punctuator::COMMA) {
                            break;
                        }
                    }
                }
                if (paren_args.size() == 1) {
                    init_expr = paren_args[0];
                } else if (paren_args.size() > 1) {
                    init_expr = std::make_shared<ast::expr_list_expr>(paren_args);
                }
                is_constructor = true;
            } else if(lequal_or_openp == lex::punctuator::BRACE_OPEN) {
                auto open_brace = lex::as<lex::punctuator>(lequal_or_openp);
                init_expr = parse_brace_init_list(open_brace);
                is_brace_init = true;
            } else {
                _lexer.unget();
            }

            auto var = std::make_shared<ast::variable_decl>(
                specifiers, lex::as<lex::identifier>(lname), type,
                init_expr, is_constructor, is_brace_init);

            // Check what follows: ')' or ';'
            auto lnext = _lexer.get();
            if(lnext == lex::punctuator::SEMICOLON) {
                // More declarations or test expression follows
                single_var_holder.sync();
                cond_vars.push_back(var);
                // Continue loop — next iteration will try to parse another var or test expr
            } else if(lnext == lex::punctuator::PARENTHESIS_CLOSE) {
                // End of if condition
                single_var_holder.sync();
                cond_vars.push_back(var);
                var_holder.sync();
                parsing_vars = false;
                // No test expression — if-let / multi-var soft-fail form
            } else {
                // Unexpected token — bail out
                single_var_holder.rollback();
                parsing_vars = false;
                if(!cond_vars.empty()) {
                    // We have some vars already, this is an error
                    throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_IF_EXPECT_CLOSE_PAREN), lnext, "If statement expect ';' or ')' after condition variable declaration");
                }
            }
        }

        if(cond_vars.empty()) {
            var_holder.rollback();
        }
    }

    // test_expr may already be set from if(var; test) form
    if(cond_vars.empty() && !test_expr) {
        // Classic form: parse expression
        test_expr = parse_expression();
        if(!test_expr) {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_IF_EXPECT_CONDITION), _lexer.pick_current(), "If statement expect an expression after the open parenthesis '('");
        }

        auto lpclose = _lexer.get();
        if(lpclose != lex::punctuator::PARENTHESIS_CLOSE) {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_IF_EXPECT_CLOSE_PAREN), lpclose, "If statement expect a close parenthesis ')' after the tested expression");
        }
    }

    auto then_stmt = parse_statement();
    if(!then_stmt) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_IF_EXPECT_BODY), _lexer.pick_current(), "If statement expect a statement after the close parenthesis ')'");
    }

    holder.sync();

    auto lelse = _lexer.get();
    if(lelse == lex::keyword::ELSE) {
        auto else_stmt = parse_statement();
        if(!else_stmt) {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_IF_EXPECT_ELSE_BODY), lelse, "If statement expect a statement after the 'else' keyword");
        }

        if(!cond_vars.empty()) {
            if(test_expr) {
                return std::make_shared<ast::if_else_statement>(
                            lex::as<lex::keyword>(lif),
                            lex::as<lex::keyword>(lelse),
                            cond_vars,
                            test_expr,
                            then_stmt,
                            else_stmt
                        );
            } else if(cond_vars.size() == 1) {
                // Single var, classic if-let with else
                return std::make_shared<ast::if_else_statement>(
                            lex::as<lex::keyword>(lif),
                            lex::as<lex::keyword>(lelse),
                            cond_vars[0],
                            then_stmt,
                            else_stmt
                        );
            } else {
                // Multi-var soft-fail without test, with else
                return std::make_shared<ast::if_else_statement>(
                            lex::as<lex::keyword>(lif),
                            lex::as<lex::keyword>(lelse),
                            cond_vars,
                            nullptr,
                            then_stmt,
                            else_stmt
                        );
            }
        } else {
            return std::make_shared<ast::if_else_statement>(
                        lex::as<lex::keyword>(lif),
                        lex::as<lex::keyword>(lelse),
                        test_expr,
                        then_stmt,
                        else_stmt
                    );
        }
    } else {
        holder.rollback();
        if(!cond_vars.empty()) {
            if(test_expr) {
                return std::make_shared<ast::if_else_statement>(
                        lex::as<lex::keyword>(lif),
                        cond_vars,
                        test_expr,
                        then_stmt
                );
            } else if(cond_vars.size() == 1) {
                // Single var, classic if-let without else
                return std::make_shared<ast::if_else_statement>(
                        lex::as<lex::keyword>(lif),
                        cond_vars[0],
                        then_stmt
                );
            } else {
                // Multi-var soft-fail without test, without else
                return std::make_shared<ast::if_else_statement>(
                        lex::as<lex::keyword>(lif),
                        cond_vars,
                        nullptr,
                        then_stmt
                );
            }
        } else {
            return std::make_shared<ast::if_else_statement>(
                    lex::as<lex::keyword>(lif),
                    test_expr,
                    then_stmt
            );
        }
    }
}

std::shared_ptr<ast::while_statement> parser::parse_while_statement() {
    lex::lex_holder holder(_lexer);

    auto lwhile = _lexer.get();
    if(lwhile != lex::keyword::WHILE) {
        holder.rollback();
        return {};
    }

    auto lpopen = _lexer.get();
    if(lpopen != lex::punctuator::PARENTHESIS_OPEN) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_WHILE_EXPECT_OPEN_PAREN), lpopen, "While statement expect an open parenthesis '(' after the 'while' keyword for the tested expression");
    }

    auto test_expr = parse_expression();
    if(!test_expr) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_WHILE_EXPECT_CONDITION), _lexer.pick_current(), "While statement expect an expression after the open parenthesis '('");
    }

    auto lpclose = _lexer.get();
    if(lpclose != lex::punctuator::PARENTHESIS_CLOSE) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_WHILE_EXPECT_CLOSE_PAREN), lpclose, "While statement expect a close parenthesis ')' after the tested expression");
    }

    auto nested_stmt = parse_statement();
    if(!nested_stmt) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_WHILE_EXPECT_BODY), lpclose, "While statement expect a statement after the close parenthesis ')'");
    }

    return std::make_shared<ast::while_statement>(
            lex::as<lex::keyword>(lwhile),
            test_expr,
            nested_stmt
    );
}

std::shared_ptr<ast::foreach_statement> parser::parse_foreach_statement()
{
    lex::lex_holder holder(_lexer);

    auto lfor = _lexer.get();
    if(lfor != lex::keyword::FOR) {
        holder.rollback();
        return {};
    }

    auto lpopen = _lexer.get();
    if(lpopen != lex::punctuator::PARENTHESIS_OPEN) {
        holder.rollback();
        return {};
    }

    std::vector<lex::keyword> specifiers = parse_specifiers();

    auto lname = _lexer.get();
    if(lex::is_not<lex::identifier>(lname)) {
        holder.rollback();
        return {};
    }

    auto lcolon = _lexer.get();
    if(lcolon != lex::operator_::COLON) {
        holder.rollback();
        return {};
    }

    std::shared_ptr<ast::type_specifier> type = parse_type_spec();
    if(!type) {
        holder.rollback();
        return {};
    }

    auto lequal = _lexer.get();
    if(lequal != lex::operator_::EQUAL) {
        // Not a foreach form (constructor-call or brace-init decl belongs to the classic for).
        holder.rollback();
        return {};
    }

    auto init_expr = parse_conditional_expr();
    if(!init_expr) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_FOREACH_EXPECT_INIT_EXPR), _lexer.pick_current(), "Foreach statement expects an initialization expression after the equal operator '='");
    }

    // Disambiguation point: ')' means foreach, ';' means classic for (roll back and let
    // parse_for_statement() reparse it), anything else is a genuine syntax error.
    auto lpclose_or_semicolon = _lexer.get();
    if(lpclose_or_semicolon == lex::punctuator::SEMICOLON) {
        holder.rollback();
        return {};
    }
    if(lpclose_or_semicolon != lex::punctuator::PARENTHESIS_CLOSE) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_FOREACH_EXPECT_CLOSE_OR_SEMICOLON), lpclose_or_semicolon, "Foreach statement expects a closing parenthesis ')' after the initialization expression");
    }

    auto nested_stmt = parse_statement();
    if(!nested_stmt) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_FOREACH_EXPECT_BODY), lpclose_or_semicolon, "Foreach statement expects a statement after the close parenthesis ')'");
    }

    auto decl_expr = std::make_shared<ast::variable_decl>(specifiers, lex::as<lex::identifier>(lname), type, init_expr);

    return std::make_shared<ast::foreach_statement>(
            lex::as<lex::keyword>(lfor),
            decl_expr,
            nested_stmt
    );
}

std::shared_ptr<ast::for_statement> parser::parse_for_statement()
{
    lex::lex_holder holder(_lexer);

    auto lfor = _lexer.get();
    if(lfor != lex::keyword::FOR) {
        holder.rollback();
        return {};
    }

    auto lpopen = _lexer.get();
    if(lpopen != lex::punctuator::PARENTHESIS_OPEN) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_FOR_EXPECT_OPEN_PAREN), lpopen, "For statement expect an open parenthesis '(' after the 'for' keyword");
    }

    std::optional<lex::punctuator> first_semicolon_kw;
    std::shared_ptr<ast::variable_decl> decl_stmt;
    if(auto decl = parse_variable_decl()) {
        decl_stmt = decl;
        // TODO Add semicolon ref
    } else if(auto lsemicolon = _lexer.get(); lsemicolon == lex::punctuator::SEMICOLON) {
        first_semicolon_kw = lex::as<lex::punctuator>(lsemicolon);
    } else {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_FOR_EXPECT_INIT_OR_SEMICOLON), lpopen, "For statement expect a variable declaration or a semicolon ';' after the open parenthesis'('");
    }

    std::optional<lex::punctuator> second_semicolon_kw;
    std::shared_ptr<ast::expression> test_expr;
    if(auto expr = parse_expression_statement()) {
        test_expr = expr->expr;
        // TODO Add semicolon ref
    } else if(auto lsemicolon = _lexer.get(); lsemicolon == lex::punctuator::SEMICOLON) {
        second_semicolon_kw = lex::as<lex::punctuator>(lsemicolon);
    } else {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_FOR_EXPECT_COND_OR_SEMICOLON), lpopen, "For statement expect an expression or a semicolon ';' after the first semicolon ';'");
    }

    std::shared_ptr<ast::expression> step_expr;
    if(auto expr = parse_expression()) {
        step_expr = expr;
    }

    auto lpclose = _lexer.get();
    if(lpclose != lex::punctuator::PARENTHESIS_CLOSE) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_FOR_EXPECT_CLOSE_PAREN), lpclose, "For statement expect a closing parenthesis ')' after the optional step expression");
    }

    auto nested_stmt = parse_statement();
    if(!nested_stmt) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_FOR_EXPECT_BODY), lpclose, "For statement expect a statement after the close parenthesis ')'");
    }

    return std::make_shared<ast::for_statement>(
            lex::as<lex::keyword>(lfor),
            *first_semicolon_kw,
            *second_semicolon_kw,
            decl_stmt,
            test_expr,
            step_expr,
            nested_stmt
    );
}

std::shared_ptr<ast::statement> parser::parse_statement()
{
    trace("[parser::parse_statement] parsing statement", {});

    if(auto block = parse_statement_block()) {
        return block;
    }

    if(auto ret = parse_return_statement()) {
        return ret;
    }

    if(auto brk = parse_break_statement()) {
        return brk;
    }

    if(auto cont = parse_continue_statement()) {
        return cont;
    }

    if(auto throw_stmt = parse_throw_statement()) {
        return throw_stmt;
    }

    if(auto try_stmt = parse_try_catch_statement()) {
        return try_stmt;
    }

    if(auto if_else = parse_if_else_statement()) {
        return if_else;
    }

    if(auto while_stmt = parse_while_statement()) {
        return while_stmt;
    }

    if(auto foreach_stmt = parse_foreach_statement()) {
        return foreach_stmt;
    }

    if(auto for_stmt = parse_for_statement()) {
        return for_stmt;
    }

    if(auto using_stmt = parse_using_decl()) {
        return using_stmt;
    }

    if(auto alias_stmt = parse_alias_decl()) {
        return alias_stmt;
    }

    if(auto var = parse_variable_decl()) {
        return var;
    }

    if(auto expr = parse_expression_statement()) {
        return expr;
    }

    return {};
}


std::shared_ptr<ast::variable_decl> parser::parse_variable_decl()
{
    lex::lex_holder holder(_lexer);

    std::vector<lex::keyword> specifiers = parse_specifiers();

    // Expect a name:
    auto lname = _lexer.get();
    if(lex::is_not<lex::identifier>(lname)) {
        holder.rollback();
        return {};
        // Err: variable declaration requires at least and identifier.
    }

    // Look for the type specifier
    auto lcolon = _lexer.get();
    if(lcolon!=lex::operator_::COLON) {
        // Err: variable declaration requires at least and identifier and a colon.
        // Err: variable declaration requires a type specifier prefixed by colon.
       holder.rollback();
        return {};
    }

    trace("[parser::parse_variable_decl] parsing variable '{}'", {std::string{lex::as<lex::identifier>(lname).content}});

    std::shared_ptr<ast::type_specifier> type = parse_type_spec();
    if(!type) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_VARDECL_EXPECT_TYPE), _lexer.pick_current(), "Variable declaration expects a type specifier after the semicolon ';'");
    }

    bool is_constructor = false;
    bool is_brace_init = false;
    ast::expr_ptr expr;
    auto lequal_or_openp = _lexer.get();
    if(lequal_or_openp==lex::operator_::EQUAL) {
        expr = parse_conditional_expr();
        if(!expr) {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_VARDECL_EXPECT_INIT_EXPR), _lexer.pick_current(), "Variable declaration expects an initialization expression after the equal operator '='");
        }
    } else if (lequal_or_openp==lex::punctuator::PARENTHESIS_OPEN) {
        // Parse arguments inside parentheses (could be constructor init or uniform array init)
        std::vector<ast::expr_ptr> paren_args;
        auto lclose_or_first = _lexer.get();
        if (lclose_or_first != lex::punctuator::PARENTHESIS_CLOSE) {
            _lexer.unget();
            while (true) {
                auto arg = parse_conditional_expr();
                paren_args.push_back(arg);
                auto sep = _lexer.get();
                if (sep == lex::punctuator::PARENTHESIS_CLOSE) break;
                if (sep != lex::punctuator::COMMA) {
                    throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_DTOR_MUST_HAVE_NO_PARAMS), sep, "Variable declaration through constructor with parenthesis initialization expects ',' or closing parenthesis ')'");
                }
            }
        }

        // Check for uniform array init: T(args)[N]
        auto peek_bracket = _lexer.get();
        if (peek_bracket == lex::punctuator::BRACKET_OPEN) {
            // Uniform array init: parse array size expression inside [N]
            auto size_expr = parse_conditional_expr();
            auto close_bracket = _lexer.get();
            if (close_bracket != lex::punctuator::BRACKET_CLOSE) {
                throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_ENUM_ENTRY_MISSING_SEMICOLON), close_bracket, "Uniform array init expects a closing bracket ']' after size expression");
            }
            auto var = std::make_shared<ast::variable_decl>(specifiers, lex::as<lex::identifier>(lname), type);
            var->is_uniform_array_init = true;
            var->uniform_ctor_args = std::move(paren_args);
            var->uniform_array_size = size_expr;

            auto lsemicolon = _lexer.get();
            if(lsemicolon!=lex::punctuator::SEMICOLON) {
                throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_VARDECL_MISSING_SEMICOLON), _lexer.pick_current(), "Variable declaration expects to finish by a semicolon ';'");
            }
            return var;
        } else {
            _lexer.unget();
        }

        // Regular constructor init: T(args)
        // Flatten paren_args into a single expression list if needed
        if (paren_args.size() == 1) {
            expr = paren_args[0];
        } else if (paren_args.size() > 1) {
            expr = std::make_shared<ast::expr_list_expr>(paren_args);
        }
        is_constructor = true;
    } else if (lequal_or_openp==lex::punctuator::BRACE_OPEN) {
        // Brace initializer list: { expr, expr, ... } or designated: { .a = expr, .b(args) }
        auto open_brace = lex::as<lex::punctuator>(lequal_or_openp);
        expr = parse_brace_init_list(open_brace);
        is_brace_init = true;
    } else {
        _lexer.unget();
    }

    auto lsemicolon = _lexer.get();
    if(lsemicolon!=lex::punctuator::SEMICOLON) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_VARDECL_MISSING_SEMICOLON), _lexer.pick_current(), "Variable declaration expects to finish by a semicolon ';'");
    }

    return std::make_shared<ast::variable_decl>(specifiers, lex::as<lex::identifier>(lname), type, expr, is_constructor, is_brace_init);
}

/**
 * Look ahead from just after a '(' to decide whether the parenthesised region is a
 * *bare* callable prototype (no addresser) rather than a parenthesised expression.
 *
 * The lexer position is restored before returning in every case.
 *
 * A bare prototype is only accepted when the token that follows the matching ')' is
 * ':' (explicit return type), ';' or '='. Any other follower — most importantly ')'
 * and ',' — keeps the region available to the tentative cast-expression parser.
 */
bool parser::is_callable_prototype_ahead()
{
    lex::lex_holder holder(_lexer);
    unsigned int depth = 1;
    bool matched = false;
    // Bound the scan so a malformed source cannot spin forever.
    for (unsigned int guard = 0; guard < 4096u; ++guard) {
        auto tok = _lexer.get();
        if (!tok) break;
        if (tok == lex::punctuator::PARENTHESIS_OPEN) {
            ++depth;
        } else if (tok == lex::punctuator::PARENTHESIS_CLOSE) {
            if (--depth == 0) { matched = true; break; }
        } else if (tok == lex::punctuator::BRACE_OPEN || tok == lex::punctuator::SEMICOLON) {
            break; // certainly not a type list
        }
    }
    bool result = false;
    if (matched) {
        auto next = _lexer.get();
        result = next == lex::operator_::COLON
              || next == lex::punctuator::SEMICOLON
              || next == lex::operator_::EQUAL;
    }
    holder.rollback();
    return result;
}

std::shared_ptr<ast::type_specifier> parser::parse_type_spec(bool stop_before_bracket)
{
    // ── Try a callable type first (prototype, addressed, or unbound member) ───
    // Syntax: [ 'const' ] [ QualId '::' ] [ '*'|'?'|'+'|'&' ] '(' [ TypeList ] ')'
    //         [ ':' TypeSpec ]
    // An addresser immediately followed by '(' (when there is no preceding base type)
    // is a callable type, not a dereference/unary operator.
    //
    // A *bare* prototype (no addresser) is only recognised when the token following
    // the matching ')' is ':' , ';' or '=' — otherwise '(' would steal parenthesised
    // expressions from the tentative cast-expression parser.
    {
        lex::lex_holder fn_holder(_lexer);

        // Optional leading 'const': qualifies the callable value, not its components.
        std::optional<lex::keyword> callable_const_kw;
        if (auto lconst = _lexer.get(); lconst == lex::keyword::CONST) {
            callable_const_kw = lex::as<lex::keyword>(lconst);
        } else {
            _lexer.unget();
        }

        // Attempt to parse an optional owner prefix of the form "Ident (:: Ident)* ::"
        // followed by an addresser and '('.
        // We MUST NOT call parse_qualified_identifier() here because it throws when
        // the token after '::' is not an identifier (e.g. when it is '*').
        std::optional<ast::qualified_identifier> owner_opt;

        // Peek ahead: is there a "Ident ... :: Addresser (" sequence?
        // Strategy: collect identifiers separated by "::", stopping when we see
        //   ":: Addresser(" (found owner), or a non-identifier/non-"::" (no owner).
        bool is_function_ref = false;
        {
            lex::lex_holder peek_holder(_lexer);
            std::vector<lex::identifier> names;

            // Try to collect qualified identifier parts
            auto t = _lexer.get();
            if (lex::is<lex::identifier>(t)) {
                names.push_back(lex::as<lex::identifier>(t));
                // Try additional ":: Ident" parts — stop on ":: Addresser("
                while (true) {
                    lex::lex_holder seg(_lexer);
                    auto dc = _lexer.get();
                    if (dc != lex::punctuator::DOUBLE_COLON) {
                        _lexer.unget();
                        // No more ::, names is a qualified identifier WITHOUT the "::" owner suffix.
                        // This means no owner — just a plain identifier, not a callable prefix.
                        break;
                    }
                    auto next = _lexer.get();
                    if (next == lex::operator_::STAR || next == lex::operator_::QUESTION_MARK || next == lex::operator_::PLUS) {
                        // ":: Addresser" — check for '('
                        auto par = _lexer.get();
                        if (par == lex::punctuator::PARENTHESIS_OPEN) {
                            // Found "names :: Addresser (" → owner = names, callable found
                            _lexer.unget(); // unget '('
                            _lexer.unget(); // unget Addresser
                            // :: is consumed — that's intentional (we're past it)
                            owner_opt = ast::qualified_identifier(std::nullopt, names);
                            seg.sync();
                            is_function_ref = true;
                            break;
                        } else {
                            _lexer.unget(); // unget par
                            _lexer.unget(); // unget Addresser
                            _lexer.unget(); // unget ::
                            break;
                        }
                    } else if (lex::is<lex::identifier>(next)) {
                        names.push_back(lex::as<lex::identifier>(next));
                        seg.sync();
                    } else {
                        _lexer.unget(); // unget next
                        _lexer.unget(); // unget ::
                        break;
                    }
                }
            } else {
                _lexer.unget(); // put back first token (not an identifier)
            }
            if (!is_function_ref) {
                peek_holder.rollback(); // restore all
            } else {
                peek_holder.sync();
            }
        }

        // Now try to read the addresser (with or without owner), or a bare '('.
        std::optional<lex::operator_> addresser_op;
        bool callable_open = false;
        {
            auto addr_tok = _lexer.get();
            if (addr_tok == lex::operator_::STAR ||
                addr_tok == lex::operator_::QUESTION_MARK ||
                addr_tok == lex::operator_::PLUS ||
                addr_tok == lex::operator_::AMPERSAND) {
                // Must be immediately followed by '(' to be a callable type
                auto par_tok = _lexer.get();
                if (par_tok == lex::punctuator::PARENTHESIS_OPEN) {
                    addresser_op = lex::as<lex::operator_>(addr_tok);
                    callable_open = true;
                } else {
                    _lexer.unget(); // unget par_tok
                    _lexer.unget(); // unget addr_tok
                }
            } else if (addr_tok == lex::punctuator::PARENTHESIS_OPEN && !owner_opt.has_value()) {
                // Bare prototype candidate — only accept it when what follows the
                // matching ')' cannot be the continuation of an expression.
                if (is_callable_prototype_ahead()) {
                    callable_open = true;
                } else {
                    _lexer.unget();
                }
            } else {
                _lexer.unget(); // unget addr_tok
            }
        }

        if (callable_open) {
            const bool tentative = !addresser_op.has_value();
            std::vector<std::shared_ptr<ast::type_specifier>> params;
            bool failed = false;

            // Parse parameter type list (may be empty)
            auto close_par = _lexer.get();
            if (close_par != lex::punctuator::PARENTHESIS_CLOSE) {
                _lexer.unget(); // put back first token of first type
                while (true) {
                    auto pt = parse_type_spec();
                    if (!pt) {
                        if (tentative) { failed = true; break; }
                        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_BRACE_INIT_NESTED_ERROR), _lexer.pick_current(),
                            "Expected a type specifier in callable type parameter list");
                    }
                    params.push_back(pt);
                    auto sep = _lexer.get();
                    if (sep == lex::punctuator::PARENTHESIS_CLOSE) {
                        break; // end of param list
                    } else if (sep == lex::punctuator::COMMA) {
                        continue; // next param
                    } else {
                        if (tentative) { failed = true; break; }
                        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_BRACE_INIT_SEP_ERROR), sep,
                            "Expected ',' or ')' in callable type parameter list");
                    }
                }
            }

            if (!failed) {
                // Greedy return type: a ':' right after the closing ')' always
                // introduces the callable return type. K has no 'void' keyword —
                // an omitted ': TypeSpec' *is* the void return.
                std::shared_ptr<ast::type_specifier> ret_type;
                if (auto lcolon = _lexer.get(); lcolon == lex::operator_::COLON) {
                    ret_type = parse_type_spec(stop_before_bracket);
                    if (!ret_type) {
                        if (tentative) { failed = true; }
                        else {
                            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_BRACE_INIT_NESTED_ERROR), _lexer.pick_current(),
                                "Expected a type specifier as callable return type after ':'");
                        }
                    }
                } else {
                    _lexer.unget();
                }

                if (!failed) {
                    fn_holder.sync();
                    std::shared_ptr<ast::type_specifier> result =
                        std::make_shared<ast::callable_type_specifier>(addresser_op, owner_opt, params, ret_type);
                    if (callable_const_kw.has_value()) {
                        result = std::make_shared<ast::const_type_specifier>(*callable_const_kw, result);
                    }
                    return result;
                }
            }
        }
        fn_holder.rollback();
    }

    std::shared_ptr<ast::type_specifier> res;

    // Optional 'const' prefix: 'const' applies to the base type only (not to pointer suffixes).
    // 'const int*' is parsed as pointer<const int>, not const<pointer<int>>.
    std::optional<lex::keyword> const_kw;
    {
        lex::lex_holder const_holder(_lexer);
        auto lconst = _lexer.get();
        if (lconst == lex::keyword::CONST) {
            const_holder.sync();
            const_kw = lex::as<lex::keyword>(lconst);
        } else {
            _lexer.unget();
        }
    }

    res = parse_fundamental_type_spec();

    lex::lex_holder holder(_lexer);
    if(!res) {
        // Expect a type qualified identifier:
        std::shared_ptr<ast::qualified_identifier> qid = parse_qualified_identifier();
        if(qid) {
            // Try to parse template arguments after the identifier
            bool tpl_explicit = false;
            auto tpl_args = parse_template_arg_list(&tpl_explicit);
            res = std::make_shared<ast::identified_type_specifier>(*qid, tpl_args, tpl_explicit);
        } else {
            holder.rollback();
            return {};
        }
    }

    // Apply const wrapper to the base type (before pointer/array suffixes)
    if(const_kw.has_value()) {
        res = std::make_shared<ast::const_type_specifier>(*const_kw, res);
    }

    while(true) {
        holder.sync();
        auto lex = _lexer.get();

        if(lex == lex::operator_::STAR || lex == lex::operator_::AMPERSAND
            || lex == lex::operator_::PLUS || lex == lex::operator_::QUESTION_MARK
            || lex == lex::operator_::HASH) {
            res = std::make_shared<ast::pointer_type_specifier>(res, lex::as<lex::operator_>(lex));
            continue;
        }

        if(lex == lex::operator_::EXCLAMATION_MARK) {
            res = std::make_shared<ast::owner_type_specifier>(res, lex::as<lex::operator_>(lex));
            continue;
        }

        if(lex == lex::punctuator::BRACKET_OPEN && !stop_before_bracket) {

            auto lind_or_close = _lexer.get();
            std::optional<lex::integer> int_index;
            lex::opt_ref_any_lexeme lbrclose;

            if (lind_or_close == lex::punctuator::BRACKET_CLOSE) {
                // Unsized array suffix: []
                lbrclose = lind_or_close;
            } else if (lex::is<lex::integer>(lind_or_close)) {
                // Sized array suffix: [N]
                int_index = lex::as<lex::integer>(lind_or_close);
                lbrclose = _lexer.get();
                if (lbrclose != lex::punctuator::BRACKET_CLOSE) {
                    throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_TYPE_ARRAY_EXPECT_CLOSE_BRACKET), lbrclose, "Type specifier array index expect a closing bracket");
                }
            } else {
                // Not a type suffix (e.g. expression subscript in tentative parsing).
                // Roll back to before '[' so the caller can parse it in expression context.
                _lexer.unget();
                holder.rollback();
                break;
            }

            res = std::make_shared<ast::array_type_specifier>(res, lex::as<lex::punctuator>(lex), lex::as<lex::punctuator>(lbrclose), int_index);
            continue;
        }

        holder.rollback();
        break;
    }

    return res;
}

std::shared_ptr<ast::type_specifier> parser::parse_fundamental_type_spec() {
    lex::lex_holder holder(_lexer);

    // Look for type prefix
    bool is_unsigned = false;
    auto lprefix = _lexer.get();
    if(lprefix == lex::keyword::UNSIGNED) {
        is_unsigned = true;
    } else {
        _lexer.unget();
    }

    // Expect a type keyword
    auto ltype = _lexer.get();
    if(lex::is_one_of<
            lex::keyword::BOOL,
            lex::keyword::BYTE,
            lex::keyword::CHAR,
            lex::keyword::SHORT,
            lex::keyword::INT,
            lex::keyword::LONG,
            lex::keyword::FLOAT,
            lex::keyword::DOUBLE>(ltype)) {
        bool is_long_long = false;
        if (ltype == lex::keyword::LONG) {
            lex::lex_holder long_holder(_lexer);
            auto lsecond = _lexer.get();
            if (lsecond == lex::keyword::LONG) {
                is_long_long = true;
                long_holder.sync();
            } else {
                _lexer.unget();
            }
        }
        return std::make_shared<ast::keyword_type_specifier>(
            std::get<lex::keyword>(ltype.value().get()), is_unsigned, is_long_long);
    }
    holder.rollback();
    return {};
}


std::shared_ptr<ast::expression_statement> parser::parse_expression_statement()
{
    ast::expr_ptr expr = parse_expression();
    if(!expr) {
        return {};
    }

    auto lsemicolon = _lexer.get();
    if(lsemicolon!=lex::punctuator::SEMICOLON) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_EXPRSTMT_MISSING_SEMICOLON), _lexer.pick_current(), "Expression statement expects to finish by a semicolon ';'");
    }

    return std::make_shared<ast::expression_statement>(expr);
}

} // k::parse
