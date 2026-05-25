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

ast::expr_ptr parser::parse_expression()
{
    std::vector<ast::expr_ptr> exprs;

    if(ast::expr_ptr first = parse_assignment_expression()) {
        exprs.push_back(first);
    } else {
        return {};
    }

    while(true) {
        auto lcomma = _lexer.get();
        if (lcomma != lex::punctuator::COMMA) {
            _lexer.unget();
            if (exprs.size() == 1) {
                return {exprs[0]};
            } else {
                return std::make_shared<ast::expr_list_expr>(exprs);
            }
        }

        ast::expr_ptr next = parse_assignment_expression();
        if(next) {
            exprs.push_back(next);
        } else {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_EXPRLIST_EXPECT_SUBEXPR), _lexer.pick_current(), "Expression list is expecting a sub expression after a comma ','");
        }
    }
}

ast::expr_ptr parser::parse_expression_list() {
    // Same code than parse_expression(...)
    return parse_expression();
}

ast::expr_ptr parser::parse_assignment_expression()
{
    ast::expr_ptr cond = parse_conditional_expr();
    if(!cond) {
        return {};
    }

    lex::opt_ref_any_lexeme lop = _lexer.get();
    if(lex::is_none_of<lex::operator_::EQUAL,
          lex::operator_::STAR_EQUAL,
          lex::operator_::SLASH_EQUAL,
          lex::operator_::PERCENT_EQUAL,
          lex::operator_::PLUS_EQUAL,
          lex::operator_::MINUS_EQUAL,
          lex::operator_::DOUBLE_CHEVRON_OPEN_EQUAL,
          lex::operator_::DOUBLE_CHEVRON_CLOSE_EQUAL,
          lex::operator_::AMPERSAND_EQUAL,
          lex::operator_::CARET_EQUAL,
          lex::operator_::PIPE_EQUAL>(lop))
    {
        _lexer.unget();
        return cond;
    }

    ast::expr_ptr other = parse_assignment_expression();
    if(other) {
        return std::make_shared<ast::binary_operator_expr>(lex::as<lex::operator_>(lop), cond, other);
    } else {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_ASSIGN_EXPECT_SUBEXPR), _lexer.pick_current(), "Assignment expression is expecting a sub expression after a the asssignmment operator");
    }
}

ast::expr_ptr parser::parse_conditional_expr() {
    ast::expr_ptr left = parse_logical_or_expression();
    if (!left) {
        return {};
    }

    auto lqm = _lexer.get();
    if (lqm != lex::operator_::QUESTION_MARK) {
        _lexer.unget();
        return left;
    }

    ast::expr_ptr middle = parse_logical_or_expression();
    if(!middle) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_COND_EXPECT_THEN_EXPR), _lexer.pick_current(), "Conditional expression is expecting a sub expression after a the question-mark '?' operator");
    }

    auto lcolon = _lexer.get();
    if (lqm != lex::operator_::COLON) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_COND_EXPECT_COLON), _lexer.pick_current(), "Conditional expression is expecting a colon ':' operator after the first sub expression");
    }

    ast::expr_ptr right = parse_logical_or_expression();
    if(!right) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_COND_EXPECT_ELSE_EXPR), _lexer.pick_current(), "Conditional expression is expecting a sub expression after the colon ':' operator");
    }

    return std::make_shared<ast::conditional_expr>(lex::as<lex::operator_>(lqm), lex::as<lex::operator_>(lcolon), left, middle, right);
}

ast::expr_ptr parser::parse_logical_or_expression()
{
    ast::expr_ptr left_expr;

    if(ast::expr_ptr first = parse_logical_and_expression()) {
        left_expr = first;
    } else {
        return {};
    }

    while (true) {
        auto op = _lexer.get();
        if (op != lex::operator_::DOUBLE_PIPE) {
            _lexer.unget();
            return left_expr;
        }

        ast::expr_ptr right_expr = parse_logical_and_expression();
        if(right_expr) {
            left_expr = std::make_shared<ast::binary_operator_expr>(lex::as<lex::operator_>(op), left_expr, right_expr);
        } else {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_LOGOR_EXPECT_SUBEXPR), _lexer.pick_current(), "Logical-OR expression is expecting a sub expression after the double-pipe '||' operator");
        }
    }

}

ast::expr_ptr parser::parse_logical_and_expression()
{
    ast::expr_ptr left_expr;

    if(ast::expr_ptr first = parse_inclusive_bin_or_expr()) {
        left_expr = first;
    } else {
        return {};
    }

    while (true) {
        auto op = _lexer.get();
        if (op != lex::operator_::DOUBLE_AMPERSAND) {
            _lexer.unget();
            return left_expr;
        }

        ast::expr_ptr right_expr = parse_inclusive_bin_or_expr();
        if(right_expr) {
            left_expr = std::make_shared<ast::binary_operator_expr>(lex::as<lex::operator_>(op), left_expr, right_expr);
        } else {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_LOGAND_EXPECT_SUBEXPR), _lexer.pick_current(), "Logical-AND expression is expecting a sub expression after the double-ampersand '&&' operator");
        }
    }

}

ast::expr_ptr parser::parse_inclusive_bin_or_expr()
{
    ast::expr_ptr left_expr;

    if(ast::expr_ptr first = parse_exclusive_bin_or_expr()) {
        left_expr = first;
    } else {
        return {};
    }

    while (true) {
        auto op = _lexer.get();
        if (op != lex::operator_::PIPE) {
            _lexer.unget();
            return left_expr;
        }

        ast::expr_ptr right_expr = parse_exclusive_bin_or_expr();
        if(right_expr) {
            left_expr = std::make_shared<ast::binary_operator_expr>(lex::as<lex::operator_>(op), left_expr, right_expr);
        } else {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_BITOR_EXPECT_SUBEXPR), _lexer.pick_current(), "Binary-OR expression is expecting a sub expression after the pipe '|' operator");
        }
    }

}

ast::expr_ptr parser::parse_exclusive_bin_or_expr()
{
    ast::expr_ptr left_expr;

    if(ast::expr_ptr first = parse_bin_and_expr()) {
        left_expr = first;
    } else {
        return {};
    }

    while (true) {
        auto op = _lexer.get();
        if (op != lex::operator_::CARET) {
            _lexer.unget();
            return left_expr;
        }

        ast::expr_ptr right_expr = parse_bin_and_expr();
        if(right_expr) {
            left_expr = std::make_shared<ast::binary_operator_expr>(lex::as<lex::operator_>(op), left_expr, right_expr);
        } else {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_BITXOR_EXPECT_SUBEXPR), _lexer.pick_current(), "Binary-XOR expression is expecting a sub expression after the caret '^' operator");
        }
    }

}

ast::expr_ptr parser::parse_bin_and_expr()
{
    ast::expr_ptr left_expr;

    if(ast::expr_ptr first = parse_equality_expr()) {
        left_expr = first;
    } else {
        return {};
    }

    while (true) {
        auto op = _lexer.get();
        if (op != lex::operator_::AMPERSAND) {
            _lexer.unget();
            return left_expr;
        }

        ast::expr_ptr right_expr = parse_equality_expr();
        if(right_expr) {
            left_expr = std::make_shared<ast::binary_operator_expr>(lex::as<lex::operator_>(op), left_expr, right_expr);
        } else {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_BITAND_EXPECT_SUBEXPR), _lexer.pick_current(), "Binary-AND expression is expecting a sub expression after the ampersand '&' operator");
        }
    }

}

ast::expr_ptr parser::parse_equality_expr()
{
    ast::expr_ptr left_expr;

    if(ast::expr_ptr first = parse_relational_expr()) {
        left_expr = first;
    } else {
        return {};
    }

    while (true) {
        auto op = _lexer.get();
        if (op != lex::operator_::DOUBLE_EQUAL &&
            op != lex::operator_::EXCLAMATION_MARK_EQUAL) {
            _lexer.unget();
            return left_expr;
        }

        ast::expr_ptr right_expr = parse_relational_expr();
        if(right_expr) {
            left_expr = std::make_shared<ast::binary_operator_expr>(lex::as<lex::operator_>(op), left_expr, right_expr);
        } else {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_EQUALITY_EXPECT_SUBEXPR), _lexer.pick_current(), "Equality expression is expecting a sub expression after the equality '==' or '!=' operators");
        }
    }
}

ast::expr_ptr parser::parse_relational_expr()
{
    ast::expr_ptr left_expr;

    if(ast::expr_ptr first = parse_shifting_expr()) {
        left_expr = first;
    } else {
        return {};
    }

    while (true) {
        auto op = _lexer.get();
        if(lex::is_none_of<
                lex::operator_::CHEVRON_CLOSE,
                lex::operator_::CHEVRON_OPEN,
                lex::operator_::CHEVRON_CLOSE_EQUAL,
                lex::operator_::CHEVRON_OPEN_EQUAL>(op)) {
            _lexer.unget();
            return left_expr;
        }

        ast::expr_ptr right_expr = parse_shifting_expr();
        if(right_expr) {
            left_expr = std::make_shared<ast::binary_operator_expr>(lex::as<lex::operator_>(op), left_expr, right_expr);
        } else {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_RELATIONAL_EXPECT_SUBEXPR), _lexer.pick_current(), "Relational expression is expecting a sub expression after the relational '<', '>', '<=' or '>=' operators");
        }
    }

}

ast::expr_ptr parser::parse_shifting_expr()
{
    ast::expr_ptr left_expr;

    if(ast::expr_ptr first = parse_additive_expr()) {
        left_expr = first;
    } else {
        return {};
    }

    while (true) {
        auto op = _lexer.get();
        if (op != lex::operator_::DOUBLE_CHEVRON_CLOSE &&
            op != lex::operator_::DOUBLE_CHEVRON_OPEN) {
            _lexer.unget();
            return left_expr;
        }

        ast::expr_ptr right_expr = parse_additive_expr();
        if(right_expr) {
            left_expr = std::make_shared<ast::binary_operator_expr>(lex::as<lex::operator_>(op), left_expr, right_expr);
        } else {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_SHIFT_EXPECT_SUBEXPR), _lexer.pick_current(), "Shifting expression is expecting a sub expression after the shifting '<<' or '>>' operators");
        }
    }
}

ast::expr_ptr parser::parse_additive_expr()
{
    ast::expr_ptr left_expr;

    if(ast::expr_ptr first = parse_multiplicative_expr()) {
        left_expr = first;
    } else {
        return {};
    }

    while (true) {
        auto op = _lexer.get();
        if (op != lex::operator_::PLUS &&
            op != lex::operator_::MINUS) {
            _lexer.unget();
            return left_expr;
        }

        ast::expr_ptr right_expr = parse_multiplicative_expr();
        if(right_expr) {
            left_expr = std::make_shared<ast::binary_operator_expr>(lex::as<lex::operator_>(op), left_expr, right_expr);
        } else {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_ADDITIVE_EXPECT_SUBEXPR), _lexer.pick_current(), "Additive expression is expecting a sub expression after the additive '+' or '-' operators");
        }
    }
}

ast::expr_ptr parser::parse_multiplicative_expr() {
    ast::expr_ptr left_expr;

    if (ast::expr_ptr first = parse_pm_expr()) {
        left_expr = first;
    } else {
        return {};
    }

    while (true) {
        auto op = _lexer.get();
        if (lex::is_none_of<
                lex::operator_::STAR,
                lex::operator_::SLASH,
                lex::operator_::PERCENT>(op)) {
            _lexer.unget();
            return left_expr;
        }

        ast::expr_ptr right_expr = parse_pm_expr();
        if (right_expr) {
            left_expr = std::make_shared<ast::binary_operator_expr>(lex::as<lex::operator_>(op), left_expr, right_expr);
        } else {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_MULTIPLICATIVE_EXPECT_SUBEXPR), _lexer.pick_current(), "Multiplicative expression is expecting a sub expression after the multiplicative '*', '/' or '%' operators");
        }
    }
}

ast::expr_ptr parser::parse_pm_expr() {
    ast::expr_ptr left_expr;

    if (ast::expr_ptr first = parse_cast_expr()) {
        left_expr = first;
    } else {
        return {};
    }

    while (true) {
        auto op = _lexer.get();
        if (op != lex::operator_::DOT_STAR &&
            op != lex::operator_::ARROW_STAR) {
            _lexer.unget();
            return left_expr;
        }

        ast::expr_ptr right_expr = parse_cast_expr();
        if (right_expr) {
            left_expr = std::make_shared<ast::binary_operator_expr>(lex::as<lex::operator_>(op), left_expr, right_expr);
        } else {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_CAST_EXPECT_SUBEXPR), _lexer.pick_current(),
                        "PM expression is expecting a sub expression after the pm '.*' or '.->' operators");
        }
    }
}

ast::expr_ptr parser::parse_cast_expr()
{
    lex::lex_holder holder(_lexer);

    if(auto lopenpar = _lexer.get(); lopenpar != lex::punctuator::PARENTHESIS_OPEN) {
        holder.rollback();
        return parse_unary_expr();
    }

    std::shared_ptr<ast::type_specifier> type = parse_type_spec();
    if(!type) {
        holder.rollback();
        return parse_unary_expr();
    }

    if(auto lclosepar = _lexer.get(); lclosepar != lex::punctuator::PARENTHESIS_CLOSE) {
        holder.rollback();
        return parse_unary_expr();
    }

    ast::expr_ptr expr = parse_cast_expr();
    if(!expr) {
        holder.rollback();
        return parse_unary_expr();
    }

    return std::make_shared<ast::cast_expr>(type, expr);
}

ast::expr_ptr parser::parse_unary_expr()
{
    lex::lex_holder holder(_lexer);

    // Handle 'new TypeSpec(args)' or 'new TypeSpec[size]{init}' — keyword expression producing an owner
    if (auto lkw = _lexer.get(); lkw == lex::keyword::NEW) {
        lex::keyword new_kw = lex::as<lex::keyword>(lkw);

        // Parse the base type WITHOUT array suffix '[...]'.
        // For 'new T[expr]', we need to parse [expr] ourselves so that expr
        // can be any expression (not just an integer literal).
        auto type = parse_type_spec(/*stop_before_bracket=*/true);
        if (!type) {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_EXPECTED_OPERATOR_SYMBOL), _lexer.pick_current(), "'new' expects a type specifier");
        }

        // Check for array form: new T[expr] or new T[]
        if (auto peek_bracket = _lexer.get(); peek_bracket == lex::punctuator::BRACKET_OPEN) {
            // Array new — parse size expression inside brackets
            ast::expr_ptr size_expr;
            auto peek_close = _lexer.get();
            if (peek_close != lex::punctuator::BRACKET_CLOSE) {
                // Not an immediate ']' — parse an expression for the array size
                _lexer.unget();
                size_expr = parse_conditional_expr();
                auto close_bracket = _lexer.get();
                if (close_bracket != lex::punctuator::BRACKET_CLOSE) {
                    throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_TYPE_ARRAY_EXPECT_CLOSE_BRACKET), close_bracket,
                        "'new' array size expression expects a closing bracket ']'");
                }
            }
            // else: unsized array new T[], size will be inferred from brace init

            // Optionally parse brace initializer list { ... }
            std::shared_ptr<ast::brace_init_list> brace_init;
            auto peek_brace = _lexer.get();
            if (peek_brace == lex::punctuator::BRACE_OPEN) {
                brace_init = parse_brace_init_list(lex::as<lex::punctuator>(peek_brace));
            } else {
                _lexer.unget(); // no brace init
            }

            holder.sync();
            return std::make_shared<ast::new_expr>(new_kw, type, size_expr, brace_init);
        } else {
            _lexer.unget(); // not a bracket — put token back
        }

        // Check for brace initializer without array brackets: new T{...}
        // This is treated as an array-new with size inferred from the brace init list.
        // new T{} → empty array (0 elements); new T{1,2,3} → array of 3 elements.
        if (auto peek_brace = _lexer.get(); peek_brace == lex::punctuator::BRACE_OPEN) {
            auto brace_init = parse_brace_init_list(lex::as<lex::punctuator>(peek_brace));
            holder.sync();
            return std::make_shared<ast::new_expr>(new_kw, type, /*size_expr=*/nullptr, brace_init);
        } else {
            _lexer.unget();
        }

        // Single-object form: new T(args)  OR  uniform array form: new T(args)[N]
        // Parse argument list '(' args ')'
        if (auto lpar = _lexer.get(); lpar != lex::punctuator::PARENTHESIS_OPEN) {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_OPERATOR_PREINC_EXPECT_UNDERSCORE), _lexer.pick_current(), "'new' expects '(' after the type specifier, '[' for array allocation, or '{' for brace-initialized array");
        }
        std::vector<ast::expr_ptr> args;
        auto lclose = _lexer.get();
        if (lclose != lex::punctuator::PARENTHESIS_CLOSE) {
            _lexer.unget();
            while (true) {
                auto arg = parse_expression();
                if (!arg) {
                    throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_OPERATOR_PREDEC_EXPECT_UNDERSCORE), _lexer.pick_current(), "'new' argument list expects an expression");
                }
                args.push_back(arg);
                auto sep = _lexer.get();
                if (sep == lex::punctuator::PARENTHESIS_CLOSE) break;
                if (sep != lex::punctuator::COMMA) {
                    throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_UNSUPPORTED_OPERATOR_SYMBOL), _lexer.pick_current(), "'new' argument list expects ',' or ')'");
                }
            }
        }

        // Check for uniform array form: new T(args)[N]
        if (auto peek_bracket = _lexer.get(); peek_bracket == lex::punctuator::BRACKET_OPEN) {
            ast::expr_ptr size_expr = parse_conditional_expr();
            auto close_bracket = _lexer.get();
            if (close_bracket != lex::punctuator::BRACKET_CLOSE) {
                throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_POSTFIX_OPERATOR_EXPECT_INC_DEC), close_bracket, "'new' uniform array expects a closing bracket ']' after size expression");
            }
            holder.sync();
            return std::make_shared<ast::new_expr>(new_kw, type, args, size_expr, /*uniform_tag=*/true);
        } else {
            _lexer.unget();
        }

        holder.sync();
        return std::make_shared<ast::new_expr>(new_kw, type, args);
    } else {
        _lexer.unget();
    }

    // Handle 'delete expr' — keyword expression that destroys an owner.
    // Note: 'delete' is also used in '-> delete ;' for function aliasing but
    // that context is parsed in parse_function_decl, not here. Here, 'delete'
    // is always followed by an expression (not by ';').
    {
        lex::lex_holder del_holder(_lexer);
        if (auto lkw = _lexer.get(); lkw == lex::keyword::DELETE) {
            lex::keyword delete_kw = lex::as<lex::keyword>(lkw);
            // Peek: if next is ';' this is NOT an expression-delete (safety guard)
            auto peek = _lexer.get();
            if (peek == lex::punctuator::SEMICOLON) {
                // Not an expression-level delete, roll back
                _lexer.unget();
                del_holder.rollback();
            } else {
                _lexer.unget();
                del_holder.sync();
                ast::expr_ptr expr = parse_unary_expr();
                if (!expr) {
                    throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_POSTFIX_OPERATOR_EXPECT_INC_DEC), _lexer.pick_current(), "'delete' expects an expression");
                }
                return std::make_shared<ast::delete_expr>(delete_kw, expr);
            }
        } else {
            _lexer.unget();
        }
    }

    if(auto lop = _lexer.get();
            lex::is_one_of<
                lex::operator_::DOUBLE_PLUS,
                lex::operator_::DOUBLE_MINUS,
                lex::operator_::STAR,
                lex::operator_::AMPERSAND,
                lex::operator_::PLUS,
                lex::operator_::MINUS,
                lex::operator_::EXCLAMATION_MARK,
                lex::operator_::TILDE,
                lex::operator_::HASH>(lop)
            ) {
        ast::expr_ptr expr = parse_cast_expr();
        if(expr) {
            return std::make_shared<ast::unary_prefix_expr>(lex::as<lex::operator_>(lop), expr);
        } else {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_UNARY_EXPECT_SUBEXPR), _lexer.pick_current(), "Unary expression is expecting a sub expression after the unary '++', '--', '*', '&', '+', '-', '!', '~' or '#' operators");
        }
    } else {
        holder.rollback();
        return parse_postfix_expr();
    }
}

ast::expr_ptr parser::parse_postfix_expr()
{
    lex::lex_holder holder(_lexer);

    ast::expr_ptr any = parse_primary_expr();
    if(!any) {
        holder.rollback();
        return {};
    }

    // ── Template function call disambiguation ────────────────────────────
    // If the primary expression is an identifier and the next token is '<',
    // try to parse template arguments.  If successful AND followed by '(',
    // this is a template function call (e.g. identity<int>(42)).
    // Otherwise, roll back and let '<' be parsed as a comparison operator.
    if (auto ident_expr = std::dynamic_pointer_cast<ast::identifier_expr>(any)) {
        lex::lex_holder tpl_holder(_lexer);
        size_t save_tpl = _lexer.tell();
        auto peek_lt = _lexer.get();
        if (peek_lt == lex::operator_::CHEVRON_OPEN) {
            _lexer.unget(); // put '<' back for parse_template_arg_list
            bool was_explicit = false;
            auto tpl_args = parse_template_arg_list(&was_explicit);
            if (was_explicit && !tpl_args.empty()) {
                // Check if followed by '(' — confirms this is a function call
                auto peek_paren = _lexer.get();
                if (peek_paren == lex::punctuator::PARENTHESIS_OPEN) {
                    _lexer.unget(); // put '(' back for the postfix loop below
                    ident_expr->template_args = std::move(tpl_args);
                    ident_expr->explicit_template_args = true;
                    // Continue — the postfix loop will pick up the '(' and create
                    // a parenthesis_postfix_expr (function call).
                } else {
                    // Not a function call — rollback the template args
                    _lexer.seek(save_tpl);
                    tpl_holder.rollback();
                }
            } else if (was_explicit && tpl_args.empty()) {
                // Empty template arg list <>( — also valid for default params
                auto peek_paren = _lexer.get();
                if (peek_paren == lex::punctuator::PARENTHESIS_OPEN) {
                    _lexer.unget();
                    ident_expr->template_args = {};
                    ident_expr->explicit_template_args = true;
                } else {
                    _lexer.seek(save_tpl);
                    tpl_holder.rollback();
                }
            } else {
                // Failed to parse template args — rollback
                _lexer.seek(save_tpl);
                tpl_holder.rollback();
            }
        } else {
            tpl_holder.rollback();
        }
    }

    while(auto lop = _lexer.get())
    {
        if(lop == lex::operator_::DOUBLE_PLUS || lop == lex::operator_::DOUBLE_MINUS) {
            any = std::make_shared<ast::unary_postfix_expr>(lex::as<lex::operator_>(lop), any);
        } else if(lop == lex::punctuator::BRACKET_OPEN) {
            ast::expr_ptr expr = parse_expression();
            if(!expr) {
                throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_BRACKET_EXPECT_SUBEXPR), _lexer.pick_current(), "Bracket postfix expression expects sub-expression");
            }
            auto lclose = _lexer.get();
            if(lclose != lex::punctuator::BRACKET_CLOSE) {
                throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_BRACKET_EXPECT_CLOSE), _lexer.pick_current(), "Bracket postfix expression expects closing bracket ']' after sub-expression");
            }
            any = std::make_shared<ast::bracket_postifx_expr>(any, expr);
        } else if(lop == lex::punctuator::PARENTHESIS_OPEN) {
            ast::expr_ptr expr = parse_expression_list();
            // expr might be null if expression list is empty
            auto lclose = _lexer.get();
            if(lclose != lex::punctuator::PARENTHESIS_CLOSE) {
                throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_PAREN_POSTFIX_EXPECT_CLOSE), _lexer.pick_current(), "Parenthesis postfix expression expects closing parenthesis ')'");
            }
            any = std::make_shared<ast::parenthesis_postifx_expr>(any, expr);
        } else if(lop == lex::operator_::ARROW || lop == lex::operator_::DOT) {
            ast::expr_ptr expr = parse_identifier_expr();
            auto ident_expr = std::dynamic_pointer_cast<ast::identifier_expr>(expr);
            if(!ident_expr) {
                throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_STRUCT_MISSING_OPEN_BRACE), _lexer.pick_current(), "Member access postfix expression expects an identifier after the '.' or '->' operator");
            }
            // ── Template argument disambiguation on member access ────────────
            // If the member identifier is followed by '<', try to parse template
            // arguments. If successful AND followed by '(', this is a member
            // template call (e.g. obj.method<int>(42)). Otherwise, roll back.
            {
                lex::lex_holder tpl_holder(_lexer);
                size_t save_tpl = _lexer.tell();
                auto peek_lt = _lexer.get();
                if (peek_lt == lex::operator_::CHEVRON_OPEN) {
                    _lexer.unget(); // put '<' back for parse_template_arg_list
                    bool was_explicit = false;
                    auto tpl_args = parse_template_arg_list(&was_explicit);
                    if (was_explicit && !tpl_args.empty()) {
                        auto peek_paren = _lexer.get();
                        if (peek_paren == lex::punctuator::PARENTHESIS_OPEN) {
                            _lexer.unget(); // put '(' back for the postfix loop
                            ident_expr->template_args = std::move(tpl_args);
                            ident_expr->explicit_template_args = true;
                        } else {
                            _lexer.seek(save_tpl);
                            tpl_holder.rollback();
                        }
                    } else if (was_explicit && tpl_args.empty()) {
                        auto peek_paren = _lexer.get();
                        if (peek_paren == lex::punctuator::PARENTHESIS_OPEN) {
                            _lexer.unget();
                            ident_expr->template_args = {};
                            ident_expr->explicit_template_args = true;
                        } else {
                            _lexer.seek(save_tpl);
                            tpl_holder.rollback();
                        }
                    } else {
                        _lexer.seek(save_tpl);
                        tpl_holder.rollback();
                    }
                } else {
                    tpl_holder.rollback();
                }
            }
            any = std::make_shared<ast::member_access_postfix_expr>(lex::as<lex::operator_>(lop), any, ident_expr);
        } else if(lop == lex::punctuator::BRACE_OPEN
                  && std::dynamic_pointer_cast<ast::identifier_expr>(any)) {
            // Brace-init postfix: S{.x=10, .y=20}
            // Only consume as brace postfix when the content starts with '.'
            // (designated initializer), to avoid ambiguity with statement blocks.
            lex::lex_holder brace_peek_holder(_lexer);
            auto peek_first = _lexer.get();
            brace_peek_holder.rollback();

            if (peek_first == lex::operator_::DOT) {
                auto open_brace = lex::as<lex::punctuator>(lop);
                auto brace_init = parse_brace_init_list(open_brace);
                any = std::make_shared<ast::brace_postfix_expr>(any, brace_init);
            } else {
                // Not a brace-init postfix — unget the '{' and stop
                _lexer.unget();
                break;
            }
        } else if(lop == lex::punctuator::ELLIPSIS) {
            // Pack expansion: expr...
            any = std::make_shared<ast::pack_expansion_expr>(any);
        } else {
            _lexer.unget();
            break;
        }
    }

    return any;
}

ast::expr_ptr parser::parse_primary_expr()
{
    lex::lex_holder holder(_lexer);

    lex::opt_ref_any_lexeme l = _lexer.get();
    if (lex::is<lex::literal>(l)) {
        return std::make_shared<ast::literal_expr>(lex::as_any_literal(l));
    } else if ( l == lex::keyword::THIS) {
        return std::make_shared<ast::this_expr>(lex::as<lex::keyword>(l));
    } else if( l == lex::punctuator::PARENTHESIS_OPEN) {
        ast::expr_ptr expr = parse_expression();
        if(!expr) {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_PAREN_EXPECT_SUBEXPR), _lexer.pick_current(), "Parenthesis expression expects a sub-expression after open-parenthesis '('");
        }
        lex::opt_ref_any_lexeme r = _lexer.get();
        if(r != lex::punctuator::PARENTHESIS_CLOSE) {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_PAREN_EXPECT_CLOSE), _lexer.pick_current(), "Parenthesis expression expects closing parenthesis ')' after sub-expression");
        }
        return expr;
    } else if (l == lex::punctuator::AT_SIGN) {
        // Annotation initializer expression: @Name(...) used as a value
        holder.rollback();
        auto ann = parse_annotation_def();
        if (!ann) {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_ANNOTATION_EXPECT_NAME_EXPR), _lexer.pick_current(), "Expected annotation type name after '@' in expression context");
        }
        return std::make_shared<ast::annotation_init_expr>(std::move(ann));
    } else if (l == lex::punctuator::BRACE_OPEN) {
        // Brace-init list as a primary expression: {expr, expr, ...}
        auto open_brace = lex::as<lex::punctuator>(l);
        return parse_brace_init_list(open_brace);
    } else {
        holder.rollback();
        return parse_identifier_expr();
    }
}

ast::expr_ptr parser::parse_identifier_expr()
{
    lex::lex_holder holder(_lexer);

    // Parse Type<T>::member as a single identifier expression where template
    // arguments belong to the leading qualifier, not to the terminal symbol.
    {
        lex::lex_holder qualified_tpl_holder(_lexer);

        std::optional<lex::punctuator> initial_doublecolon;
        if (auto linit = _lexer.get(); linit == lex::punctuator::DOUBLE_COLON) {
            initial_doublecolon = lex::as<lex::punctuator>(linit);
        } else {
            _lexer.unget();
        }

        auto first_name = _lexer.get();
        if (lex::is<lex::identifier>(first_name)) {
            bool has_explicit_tpl = false;
            auto qualifier_tpl_args = parse_template_arg_list(&has_explicit_tpl);
            if (has_explicit_tpl) {
                auto ldoublecolon = _lexer.get();
                if (ldoublecolon == lex::punctuator::DOUBLE_COLON) {
                    std::vector<lex::identifier> names;
                    names.push_back(lex::as<lex::identifier>(first_name));

                    auto push_name_after_separator = [&](const lex::opt_ref_any_lexeme& lname) -> bool {
                        if (lex::is<lex::identifier>(lname)) {
                            names.push_back(lex::as<lex::identifier>(lname));
                            return true;
                        }
                        if (lex::is<lex::keyword>(lname)) {
                            auto kw = lex::as<lex::keyword>(lname);
                            if (kw.type == lex::keyword::ANNOTATION
                                || kw.type == lex::keyword::CLASS
                                || kw.type == lex::keyword::INTERFACE
                                || kw.type == lex::keyword::STRUCT) {
                                names.push_back(lex::identifier{kw.content});
                                return true;
                            }
                        }
                        return false;
                    };

                    auto lname = _lexer.get();
                    if (!push_name_after_separator(lname)) {
                        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_QNAME_AFTER_INTERMEDIATE_SEP),
                                    _lexer.pick_current(),
                                    "Qualified identifier expect an identifier after intermediate \"::\"");
                    }

                    while (true) {
                        lex::lex_holder chain_holder(_lexer);
                        auto maybe_dc = _lexer.get();
                        if (maybe_dc != lex::punctuator::DOUBLE_COLON) {
                            chain_holder.rollback();
                            break;
                        }
                        auto next_name = _lexer.get();
                        if (!push_name_after_separator(next_name)) {
                            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_QNAME_AFTER_INTERMEDIATE_SEP),
                                        _lexer.pick_current(),
                                        "Qualified identifier expect an identifier after intermediate \"::\"");
                        }
                        chain_holder.sync();
                    }

                    auto qid = ast::qualified_identifier(initial_doublecolon, names);
                    return std::make_shared<ast::identifier_expr>(qid,
                                                                  std::move(qualifier_tpl_args),
                                                                  true,
                                                                  true);
                }
            }
        }

        qualified_tpl_holder.rollback();
    }

    std::shared_ptr<ast::qualified_identifier> ident = parse_qualified_identifier();
    if(ident) {
        return std::make_shared<ast::identifier_expr>(*ident);
    } else {
        holder.rollback();
        return {};
    }
}

std::shared_ptr<ast::brace_init_list> parser::parse_brace_init_list(const lex::punctuator& open_brace) {
    std::vector<ast::expr_ptr> elements;
    auto peek_close_brace = _lexer.get();
    if (peek_close_brace != lex::punctuator::BRACE_CLOSE) {
        _lexer.unget();

        // Peek ahead to determine if this is a designated init list.
        // A designated init starts with '.' followed by an identifier.
        enum class init_mode { UNKNOWN, POSITIONAL, DESIGNATED };
        init_mode mode = init_mode::UNKNOWN;

        bool expect_more = true;
        while (expect_more) {
            auto next = _lexer.get();

            // Check for designated initializer: '.' IDENTIFIER
            if (next == lex::operator_::DOT) {
                auto peek_ident = _lexer.get();
                if (lex::is<lex::identifier>(peek_ident)) {
                    // This is a designated init element
                    if (mode == init_mode::POSITIONAL) {
                        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_MIXED_POSITIONAL_DESIGNATED), next, "Cannot mix positional and designated initializers in the same brace-init list");
                    }
                    mode = init_mode::DESIGNATED;

                    auto dot = lex::as<lex::operator_>(next);
                    auto ident = lex::as<lex::identifier>(peek_ident);

                    // Check for qualified name: '.' Ident '::' Ident ['::' Ident ...]
                    std::vector<lex::identifier> qualifier;
                    lex::identifier member_name = ident;
                    while (true) {
                        lex::lex_holder qual_holder(_lexer);
                        auto maybe_dc = _lexer.get();
                        if (maybe_dc == lex::punctuator::DOUBLE_COLON) {
                            auto maybe_next_id = _lexer.get();
                            if (lex::is<lex::identifier>(maybe_next_id)) {
                                qualifier.push_back(member_name);
                                member_name = lex::as<lex::identifier>(maybe_next_id);
                                qual_holder.sync();
                            } else {
                                // Not an identifier after :: — roll back
                                qual_holder.rollback();
                                break;
                            }
                        } else {
                            qual_holder.rollback();
                            break;
                        }
                    }

                    // Now expect '=' (assignment form) or '(' (constructor form)
                    auto after_name = _lexer.get();
                    if (after_name == lex::operator_::EQUAL) {
                        // Assignment form: .member = expr
                        // The value can be a brace_init_list (for nested structs/arrays)
                        ast::expr_ptr value;
                        auto peek_brace = _lexer.get();
                        if (peek_brace == lex::punctuator::BRACE_OPEN) {
                            value = parse_brace_init_list(lex::as<lex::punctuator>(peek_brace));
                        } else {
                            _lexer.unget();
                            value = parse_conditional_expr();
                        }
                        elements.push_back(std::make_shared<ast::designated_init_element>(
                            dot, member_name, qualifier, value));
                    } else if (after_name == lex::punctuator::PARENTHESIS_OPEN) {
                        // Constructor form: .member(args...)
                        std::vector<ast::expr_ptr> args;
                        auto peek_close = _lexer.get();
                        if (peek_close != lex::punctuator::PARENTHESIS_CLOSE) {
                            _lexer.unget();
                            while (true) {
                                auto arg = parse_conditional_expr();
                                args.push_back(arg);
                                auto sep = _lexer.get();
                                if (sep == lex::punctuator::PARENTHESIS_CLOSE) break;
                                if (sep != lex::punctuator::COMMA) {
                                    throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_DESIGNATED_CTOR_EXPECT_COMMA_CLOSE), sep, "Designated initializer constructor form expects ',' or ')' after argument");
                                }
                            }
                        }
                        elements.push_back(std::make_shared<ast::designated_init_element>(
                            dot, member_name, qualifier, args));
                    } else {
                        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_DESIGNATED_EXPECT_EQ_OR_PAREN), after_name, "Expected '=' or '(' after designated member name '." + std::string{member_name.content} + "'");
                    }

                    // Check for comma or closing brace
                    auto sep = _lexer.get();
                    if (sep == lex::punctuator::COMMA) {
                        // continue
                    } else if (sep == lex::punctuator::BRACE_CLOSE) {
                        _lexer.unget();
                        break;
                    } else {
                        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_USING_EXPECT_QNAME), sep, "Brace initializer list expects ',' or '}' after designated initializer");
                    }
                } else {
                    // '.' not followed by an identifier — this is an error in designated context,
                    // or this could be a positional expression starting with '.' (unlikely but rollback)
                    _lexer.unget(); // unget the non-identifier
                    _lexer.unget(); // unget the '.'
                    // Fall through to positional parsing below
                    goto parse_positional_element;
                }
            } else if (next == lex::punctuator::COMMA) {
                // Empty element (default construction) — positional only
                if (mode == init_mode::DESIGNATED) {
                    throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_MIXED_POSITIONAL_DESIGNATED), next, "Cannot mix positional and designated initializers in the same brace-init list");
                }
                mode = init_mode::POSITIONAL;
                elements.push_back(nullptr);
            } else if (next == lex::punctuator::BRACE_CLOSE) {
                _lexer.unget();
                break;
            } else {
                _lexer.unget();
                parse_positional_element:
                if (mode == init_mode::DESIGNATED) {
                    throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_MIXED_POSITIONAL_DESIGNATED), next, "Cannot mix positional and designated initializers in the same brace-init list");
                }
                mode = init_mode::POSITIONAL;
                auto elem_expr = parse_conditional_expr();
                elements.push_back(elem_expr);
                auto sep = _lexer.get();
                if (sep == lex::punctuator::COMMA) {
                    // continue
                } else if (sep == lex::punctuator::BRACE_CLOSE) {
                    _lexer.unget();
                    break;
                } else {
                    throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_ENUM_MISSING_CLOSE_BRACE), sep, "Brace initializer list expects ',' or '}' after expression");
                }
            }
        }
        auto close = _lexer.get();
        if (close != lex::punctuator::BRACE_CLOSE) {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_ENUM_MISSING_SEMICOLON), close, "Brace initializer list expects a closing brace '}'");
        }
        auto close_brace = lex::as<lex::punctuator>(close);
        return std::make_shared<ast::brace_init_list>(open_brace, close_brace, elements,
            mode == init_mode::DESIGNATED);
    } else {
        // Empty brace list {}
        auto close_brace = lex::as<lex::punctuator>(peek_close_brace);
        return std::make_shared<ast::brace_init_list>(open_brace, close_brace, std::vector<ast::expr_ptr>{});
    }
}


} // k::parse
