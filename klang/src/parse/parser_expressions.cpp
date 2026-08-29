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
    if (lcolon != lex::operator_::COLON) {
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

    if(ast::expr_ptr first = parse_spaceship_expr()) {
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

        ast::expr_ptr right_expr = parse_spaceship_expr();
        if(right_expr) {
            left_expr = std::make_shared<ast::binary_operator_expr>(lex::as<lex::operator_>(op), left_expr, right_expr);
        } else {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_RELATIONAL_EXPECT_SUBEXPR), _lexer.pick_current(), "Relational expression is expecting a sub expression after the relational '<', '>', '<=' or '>=' operators");
        }
    }

}

ast::expr_ptr parser::parse_spaceship_expr()
{
    ast::expr_ptr left_expr;

    if(ast::expr_ptr first = parse_shifting_expr()) {
        left_expr = first;
    } else {
        return {};
    }

    while (true) {
        auto op = _lexer.get();
        if (op != lex::operator_::CHEVRON_OPEN_EQUAL_CHEVRON_CLOSE) {
            _lexer.unget();
            return left_expr;
        }

        ast::expr_ptr right_expr = parse_shifting_expr();
        if(right_expr) {
            left_expr = std::make_shared<ast::binary_operator_expr>(lex::as<lex::operator_>(op), left_expr, right_expr);
        } else {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_SPACESHIP_EXPECT_SUBEXPR), _lexer.pick_current(), "Spaceship expression is expecting a sub expression after the '<=>' operator");
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

    auto lopenpar = _lexer.get();
    if(lopenpar != lex::punctuator::PARENTHESIS_OPEN) {
        holder.rollback();
        return parse_unary_expr();
    }

    std::shared_ptr<ast::type_specifier> type = parse_type_spec();
    if(!type) {
        holder.rollback();
        return parse_unary_expr();
    }

    auto lclosepar = _lexer.get();
    if(lclosepar != lex::punctuator::PARENTHESIS_CLOSE) {
        holder.rollback();
        return parse_unary_expr();
    }

    ast::expr_ptr expr = parse_cast_expr();
    if(!expr) {
        holder.rollback();
        return parse_unary_expr();
    }

    auto cast = std::make_shared<ast::cast_expr>(type, expr);
    cast->set_open_paren(lex::as<lex::punctuator>(lopenpar));
    cast->set_close_paren(lex::as<lex::punctuator>(lclosepar));
    return cast;
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
            auto open_bracket_tok = lex::as<lex::punctuator>(peek_bracket);
            std::optional<lex::punctuator> close_bracket_tok;
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
                close_bracket_tok = lex::as<lex::punctuator>(close_bracket);
            } else {
                close_bracket_tok = lex::as<lex::punctuator>(peek_close);
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
            auto res = std::make_shared<ast::new_expr>(new_kw, type, size_expr, brace_init);
            res->set_open_bracket(open_bracket_tok);
            if (close_bracket_tok) res->set_close_bracket(*close_bracket_tok);
            return res;
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
        auto lpar = _lexer.get();
        if (lpar != lex::punctuator::PARENTHESIS_OPEN) {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_OPERATOR_PREINC_EXPECT_UNDERSCORE), _lexer.pick_current(), "'new' expects '(' after the type specifier, '[' for array allocation, or '{' for brace-initialized array");
        }
        auto open_paren_tok = lex::as<lex::punctuator>(lpar);
        std::optional<lex::punctuator> close_paren_tok;
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
                if (sep == lex::punctuator::PARENTHESIS_CLOSE) {
                    close_paren_tok = lex::as<lex::punctuator>(sep);
                    break;
                }
                if (sep != lex::punctuator::COMMA) {
                    throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_UNSUPPORTED_OPERATOR_SYMBOL), _lexer.pick_current(), "'new' argument list expects ',' or ')'");
                }
            }
        } else {
            close_paren_tok = lex::as<lex::punctuator>(lclose);
        }

        // Check for uniform array form: new T(args)[N]
        if (auto peek_bracket = _lexer.get(); peek_bracket == lex::punctuator::BRACKET_OPEN) {
            auto open_bracket_tok = lex::as<lex::punctuator>(peek_bracket);
            ast::expr_ptr size_expr = parse_conditional_expr();
            auto close_bracket = _lexer.get();
            if (close_bracket != lex::punctuator::BRACKET_CLOSE) {
                throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_POSTFIX_OPERATOR_EXPECT_INC_DEC), close_bracket, "'new' uniform array expects a closing bracket ']' after size expression");
            }
            holder.sync();
            auto res = std::make_shared<ast::new_expr>(new_kw, type, args, size_expr, /*uniform_tag=*/true);
            res->set_open_paren(open_paren_tok);
            if (close_paren_tok) res->set_close_paren(*close_paren_tok);
            res->set_open_bracket(open_bracket_tok);
            res->set_close_bracket(lex::as<lex::punctuator>(close_bracket));
            return res;
        } else {
            _lexer.unget();
        }

        holder.sync();
        auto res = std::make_shared<ast::new_expr>(new_kw, type, args);
        res->set_open_paren(open_paren_tok);
        if (close_paren_tok) res->set_close_paren(*close_paren_tok);
        return res;
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
    // this is a template function call (e.g. identity<int>(42)) or a template
    // temporary construction (e.g. Optional<byte>(x)).  If followed by '.' or
    // '->', this is a static/factory member access on a template-qualified type
    // (e.g. Optional<byte>.empty()).  Otherwise, roll back and let '<' be parsed
    // as a comparison operator.
    if (auto ident_expr = std::dynamic_pointer_cast<ast::identifier_expr>(any)) {
        lex::lex_holder tpl_holder(_lexer);
        size_t save_tpl = _lexer.tell();
        auto peek_lt = _lexer.get();
        if (peek_lt == lex::operator_::CHEVRON_OPEN) {
            _lexer.unget(); // put '<' back for parse_template_arg_list
            bool was_explicit = false;
            auto tpl_args = parse_template_arg_list(&was_explicit);
            if (was_explicit && !tpl_args.empty()) {
                // Check if followed by '(' (call/construction) or '.'/'->'
                // (static member access) — confirms a template type/function use.
                auto peek_next = _lexer.get();
                if (peek_next == lex::punctuator::PARENTHESIS_OPEN
                    || peek_next == lex::operator_::DOT
                    || peek_next == lex::operator_::ARROW) {
                    _lexer.unget(); // put the token back for the postfix loop below
                    ident_expr->template_args = std::move(tpl_args);
                    ident_expr->explicit_template_args = true;
                    // Continue — the postfix loop will pick up the '(' / '.' / '->'.
                } else {
                    // Not a call or member access — rollback the template args
                    _lexer.seek(save_tpl);
                    tpl_holder.rollback();
                }
            } else if (was_explicit && tpl_args.empty()) {
                // Empty template arg list <> — also valid for default params
                auto peek_next = _lexer.get();
                if (peek_next == lex::punctuator::PARENTHESIS_OPEN
                    || peek_next == lex::operator_::DOT
                    || peek_next == lex::operator_::ARROW) {
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
            // Array temporary brace postfix: T[]{...}
            // Recognized only for identifier-like callee in exact shape: identifier [] { ... }.
            if (std::dynamic_pointer_cast<ast::identifier_expr>(any)) {
                lex::lex_holder arr_holder(_lexer);
                auto maybe_close = _lexer.get();
                if (maybe_close == lex::punctuator::BRACKET_CLOSE) {
                    auto maybe_open_brace = _lexer.get();
                    if (maybe_open_brace == lex::punctuator::BRACE_OPEN) {
                        arr_holder.sync();
                        auto brace_init = parse_brace_init_list(lex::as<lex::punctuator>(maybe_open_brace));
                        any = std::make_shared<ast::brace_postfix_expr>(
                            any, brace_init, true,
                            lex::as<lex::punctuator>(lop),
                            lex::as<lex::punctuator>(maybe_close));
                        continue;
                    }
                }
                arr_holder.rollback();
            }

            ast::expr_ptr expr = parse_expression();
            if(!expr) {
                throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_BRACKET_EXPECT_SUBEXPR), _lexer.pick_current(), "Bracket postfix expression expects sub-expression");
            }
            auto lclose = _lexer.get();
            if(lclose != lex::punctuator::BRACKET_CLOSE) {
                throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_BRACKET_EXPECT_CLOSE), _lexer.pick_current(), "Bracket postfix expression expects closing bracket ']' after sub-expression");
            }
            auto br_expr = std::make_shared<ast::bracket_postifx_expr>(any, expr);
            br_expr->set_open_bracket(lex::as<lex::punctuator>(lop));
            br_expr->set_close_bracket(lex::as<lex::punctuator>(lclose));
            any = br_expr;
        } else if(lop == lex::punctuator::PARENTHESIS_OPEN) {
            ast::expr_ptr expr = parse_expression_list();
            // expr might be null if expression list is empty
            auto lclose = _lexer.get();
            if(lclose != lex::punctuator::PARENTHESIS_CLOSE) {
                throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_PAREN_POSTFIX_EXPECT_CLOSE), _lexer.pick_current(), "Parenthesis postfix expression expects closing parenthesis ')'");
            }
            auto par_expr = std::make_shared<ast::parenthesis_postifx_expr>(any, expr);
            par_expr->set_open_paren(lex::as<lex::punctuator>(lop));
            par_expr->set_close_paren(lex::as<lex::punctuator>(lclose));
            any = par_expr;
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
                _lexer.unget();
                break;
            }
        } else if(lop == lex::punctuator::ELLIPSIS) {
            // Pack expansion: expr...
            auto pack_expr = std::make_shared<ast::pack_expansion_expr>(any);
            pack_expr->set_ellipsis(lex::as<lex::punctuator>(lop));
            any = pack_expr;
        } else {
            _lexer.unget();
            break;
        }
    }

    return any;
}

ast::expr_ptr parser::parse_lambda_expression(bool allow_fallback)
{
    lex::lex_holder holder(_lexer);

    bool is_const_lambda = false;
    bool has_capture_list = false;
    std::vector<ast::lambda_capture> captures;

    auto parse_capture = [&]() -> std::optional<ast::lambda_capture> {
        bool capture_const = false;
        bool capture_ref = false;
        bool capture_this = false;
        std::optional<lex::identifier> capture_name;
        ast::expr_ptr init_expr;
        std::optional<lex::keyword> capture_const_kw;
        std::optional<lex::operator_> capture_ref_op;
        std::optional<lex::keyword> capture_this_kw;
        std::optional<lex::operator_> capture_eq_op;

        {
            lex::lex_holder const_holder(_lexer);
            auto lconst = _lexer.get();
            if (lconst == lex::keyword::CONST) {
                capture_const = true;
                capture_const_kw = lex::as<lex::keyword>(lconst);
            } else {
                const_holder.rollback();
            }
        }

        {
            lex::lex_holder ref_holder(_lexer);
            auto lamp = _lexer.get();
            if (lamp == lex::operator_::AMPERSAND) {
                capture_ref = true;
                capture_ref_op = lex::as<lex::operator_>(lamp);
            } else {
                ref_holder.rollback();
            }
        }

        auto lname = _lexer.get();
        if (lname == lex::keyword::THIS) {
            capture_this = true;
            capture_this_kw = lex::as<lex::keyword>(lname);
        } else if (lex::is<lex::identifier>(lname)) {
            capture_name = lex::as<lex::identifier>(lname);
        } else {
            return std::nullopt;
        }

        {
            lex::lex_holder init_holder(_lexer);
            auto leq = _lexer.get();
            if (leq == lex::operator_::EQUAL) {
                capture_eq_op = lex::as<lex::operator_>(leq);
                init_expr = parse_conditional_expr();
                if (!init_expr) {
                    throw_error(static_cast<unsigned int>(k::diag::lambda_diag::ERR_LAMBDA_BAD_CAPTURE_SYNTAX),
                                _lexer.pick_current(),
                                "Lambda init-capture expects an expression after '='");
                }
            } else {
                init_holder.rollback();
            }
        }

        ast::lambda_capture cap(capture_const, capture_ref, capture_this, capture_name, init_expr);
        if (capture_const_kw) cap.set_const_kw(*capture_const_kw);
        if (capture_ref_op) cap.set_ref_op(*capture_ref_op);
        if (capture_this_kw) cap.set_this_kw(*capture_this_kw);
        if (capture_eq_op) cap.set_equal_op(*capture_eq_op);
        return cap;
    };

    std::optional<lex::keyword> lambda_const_kw;
    {
        lex::lex_holder const_holder(_lexer);
        auto lconst = _lexer.get();
        if (lconst == lex::keyword::CONST) {
            is_const_lambda = true;
            lambda_const_kw = lex::as<lex::keyword>(lconst);
        } else {
            const_holder.rollback();
        }
    }

    std::optional<lex::punctuator> capture_open_bracket_tok, capture_close_bracket_tok;
    {
        lex::lex_holder capture_holder(_lexer);
        auto lbrack = _lexer.get();
        if (lbrack == lex::punctuator::BRACKET_OPEN) {
            has_capture_list = true;
            capture_open_bracket_tok = lex::as<lex::punctuator>(lbrack);
            auto maybe_close = _lexer.get();
            if (maybe_close != lex::punctuator::BRACKET_CLOSE) {
                _lexer.unget();
                while (true) {
                    auto cap = parse_capture();
                    if (!cap.has_value()) {
                        throw_error(static_cast<unsigned int>(k::diag::lambda_diag::ERR_LAMBDA_BAD_CAPTURE_SYNTAX),
                                    _lexer.pick_current(),
                                    "Malformed lambda capture list");
                    }
                    captures.push_back(std::move(*cap));

                    auto sep = _lexer.get();
                    if (sep == lex::punctuator::COMMA) {
                        continue;
                    }
                    if (sep == lex::punctuator::BRACKET_CLOSE) {
                        capture_close_bracket_tok = lex::as<lex::punctuator>(sep);
                        break;
                    }
                    throw_error(static_cast<unsigned int>(k::diag::lambda_diag::ERR_LAMBDA_BAD_CAPTURE_SYNTAX),
                                sep, "Lambda capture list expects ',' or ']'");
                }
            } else {
                capture_close_bracket_tok = lex::as<lex::punctuator>(maybe_close);
            }
        } else {
            capture_holder.rollback();
        }
    }

    auto lparen = _lexer.get();
    if (lparen != lex::punctuator::PARENTHESIS_OPEN) {
        if (allow_fallback) {
            holder.rollback();
            return {};
        }
        throw_error(static_cast<unsigned int>(k::diag::lambda_diag::ERR_LAMBDA_BAD_CAPTURE_SYNTAX),
                    lparen, "Lambda expression expects a parameter list enclosed in '(' and ')'");
    }
    auto param_open_paren_tok = lex::as<lex::punctuator>(lparen);
    std::optional<lex::punctuator> param_close_paren_tok;

    std::vector<std::shared_ptr<ast::parameter_spec>> params;
    auto maybe_close = _lexer.get();
    if (maybe_close != lex::punctuator::PARENTHESIS_CLOSE) {
        _lexer.unget();
        while (true) {
            auto param = parse_parameter_spec();
            if (!param || !param->name.has_value() || param->default_expr || param->is_varargs || param->is_pack_expansion) {
                if (allow_fallback) {
                    holder.rollback();
                    return {};
                }
                throw_error(static_cast<unsigned int>(k::diag::lambda_diag::ERR_LAMBDA_BAD_CAPTURE_SYNTAX),
                            _lexer.pick_current(),
                            "Lambda parameter list expects explicit named parameters of the form 'name : Type'");
            }
            params.push_back(param);
            auto sep = _lexer.get();
            if (sep == lex::punctuator::COMMA) {
                continue;
            }
            if (sep == lex::punctuator::PARENTHESIS_CLOSE) {
                param_close_paren_tok = lex::as<lex::punctuator>(sep);
                break;
            }
            if (allow_fallback) {
                holder.rollback();
                return {};
            }
            throw_error(static_cast<unsigned int>(k::diag::lambda_diag::ERR_LAMBDA_BAD_CAPTURE_SYNTAX),
                        sep, "Lambda parameter list expects ',' or ')'");
        }
    } else {
        param_close_paren_tok = lex::as<lex::punctuator>(maybe_close);
    }

    std::shared_ptr<ast::type_specifier> return_type;
    std::optional<lex::operator_> lambda_colon_tok;
    {
        lex::lex_holder ret_holder(_lexer);
        auto maybe_colon = _lexer.get();
        if (maybe_colon == lex::operator_::COLON) {
            lambda_colon_tok = lex::as<lex::operator_>(maybe_colon);
            return_type = parse_type_spec();
            if (!return_type) {
                if (allow_fallback) {
                    holder.rollback();
                    return {};
                }
                throw_error(static_cast<unsigned int>(k::diag::lambda_diag::ERR_LAMBDA_BAD_CAPTURE_SYNTAX),
                            _lexer.pick_current(),
                            "Lambda return type expects a type specifier after ':'");
            }
        } else {
            ret_holder.rollback();
        }
    }

    auto body = parse_statement_block();
    if (!body) {
        if (allow_fallback) {
            holder.rollback();
            return {};
        }
        throw_error(static_cast<unsigned int>(k::diag::lambda_diag::ERR_LAMBDA_BAD_CAPTURE_SYNTAX),
                    _lexer.pick_current(),
                    "Lambda expression expects a block body '{...}'");
    }

    holder.sync();
    auto lambda_res = std::make_shared<ast::lambda_expression>(
        is_const_lambda, has_capture_list, std::move(captures), std::move(params),
        std::move(return_type), std::move(body));
    if (lambda_const_kw) lambda_res->set_const_kw(*lambda_const_kw);
    if (capture_open_bracket_tok) lambda_res->set_capture_open_bracket(*capture_open_bracket_tok);
    if (capture_close_bracket_tok) lambda_res->set_capture_close_bracket(*capture_close_bracket_tok);
    lambda_res->set_param_open_paren(param_open_paren_tok);
    if (param_close_paren_tok) lambda_res->set_param_close_paren(*param_close_paren_tok);
    if (lambda_colon_tok) lambda_res->set_colon(*lambda_colon_tok);
    return lambda_res;
}

namespace {
    /**
     * True for the simple (non-compound) primitive-type keywords, i.e. those
     * whose source spelling is, on its own, a valid key of
     * context::from_string()'s primitive type map. Compound forms such as
     * 'unsigned int' or 'long long' are intentionally excluded (see caller).
     */
    bool is_simple_primitive_type_keyword(lex::keyword::type_t type) {
        switch (type) {
            case lex::keyword::BOOL:
            case lex::keyword::BYTE:
            case lex::keyword::CHAR:
            case lex::keyword::SHORT:
            case lex::keyword::INT:
            case lex::keyword::LONG:
            case lex::keyword::FLOAT:
            case lex::keyword::DOUBLE:
                return true;
            default:
                return false;
        }
    }
}

namespace {
    bool looks_like_lambda_paren_list(k::lex::lexer& lexer) {
        lex::lex_holder holder(lexer);

        int depth = 0;
        bool first_token = true;
        bool saw_top_level_question = false;
        while (true) {
            auto tok = lexer.get();
            if (!tok) {
                holder.rollback();
                return false;
            }

            if (tok == lex::punctuator::PARENTHESIS_CLOSE && first_token) {
                holder.rollback();
                return true;
            }

            first_token = false;
            if (tok == lex::punctuator::PARENTHESIS_OPEN) {
                ++depth;
            } else if (tok == lex::punctuator::PARENTHESIS_CLOSE) {
                if (depth == 0) {
                    holder.rollback();
                    return false;
                }
                --depth;
            } else if (tok == lex::operator_::QUESTION_MARK && depth == 0) {
                saw_top_level_question = true;
            } else if (tok == lex::operator_::COLON && depth == 0) {
                holder.rollback();
                // Top-level ':' usually indicates a lambda signature "(x: T)" or
                // a lambda return annotation "() : T", but in a parenthesized
                // ternary "(cond ? a : b)" it must stay an expression.
                return !saw_top_level_question;
            }
        }
    }
}

ast::expr_ptr parser::parse_primary_expr()
{
    lex::lex_holder holder(_lexer);

    lex::opt_ref_any_lexeme l = _lexer.get();
    if (lex::is<lex::literal>(l)) {
        return std::make_shared<ast::literal_expr>(lex::as_any_literal(l));
    } else if (l == lex::punctuator::BRACKET_OPEN) {
        _lexer.unget();
        return parse_lambda_expression(false);
    } else if (l == lex::keyword::CONST) {
        {
            lex::lex_holder const_holder(_lexer);
            if (_lexer.get() == lex::punctuator::PARENTHESIS_OPEN && looks_like_lambda_paren_list(_lexer)) {
                const_holder.rollback();
                _lexer.unget();
                if (auto lambda = parse_lambda_expression(true)) {
                    return lambda;
                }
            }
            const_holder.rollback();
        }
    } else if ( l == lex::keyword::THIS) {
        return std::make_shared<ast::this_expr>(lex::as<lex::keyword>(l));
    } else if( l == lex::punctuator::PARENTHESIS_OPEN) {
        if (looks_like_lambda_paren_list(_lexer)) {
            _lexer.unget();
            if (auto lambda = parse_lambda_expression(true)) {
                return lambda;
            }
        }
        ast::expr_ptr expr = parse_expression();
        if(!expr) {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_PAREN_EXPECT_SUBEXPR), _lexer.pick_current(), "Parenthesis expression expects a sub-expression after open-parenthesis '('");
        }
        lex::opt_ref_any_lexeme r = _lexer.get();
        if(r != lex::punctuator::PARENTHESIS_CLOSE) {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_PAREN_EXPECT_CLOSE), _lexer.pick_current(), "Parenthesis expression expects closing parenthesis ')' after sub-expression");
        }
        if (auto expr_list = std::dynamic_pointer_cast<ast::expr_list_expr>(expr)) {
            expr_list->set_open_paren(lex::as<lex::punctuator>(l));
            expr_list->set_close_paren(lex::as<lex::punctuator>(r));
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
    } else if (lex::is<lex::keyword>(l) && is_simple_primitive_type_keyword(lex::as<lex::keyword>(l).type)
               && _lexer.get() == lex::punctuator::BRACKET_OPEN) {
        // Recognize a single (non-compound) primitive-type keyword immediately
        // followed by '[' as an identifier-like expression, so that a primitive
        // array temporary (e.g. `int[]{1, 2, 3}`) can be parsed via the existing
        // T[]{...} postfix-brace rule below in parse_postfix_expr(), mirroring
        // the pre-existing support for struct/class type names. Compound forms
        // ('unsigned int', 'long long') are not handled here: synthesizing a
        // combined identifier from two keyword tokens would require an owned
        // string, whereas `lexeme::content` is a non-owning string_view that
        // must point into the original source buffer.
        _lexer.unget();
        auto kw = lex::as<lex::keyword>(l);
        return std::make_shared<ast::identifier_expr>(
            ast::qualified_identifier(std::nullopt, std::vector<lex::identifier>{lex::identifier{kw.content}}));
    } else {
        holder.rollback();
        return parse_identifier_expr();
    }

    return {};
}

ast::expr_ptr parser::parse_identifier_expr()
{
    lex::lex_holder holder(_lexer);

    // Parse [::]ns::...::Type<T>::member as a single identifier expression where
    // template arguments belong to the qualifier part, not to the terminal symbol.
    {
        lex::lex_holder qualified_tpl_holder(_lexer);

        std::optional<lex::punctuator> initial_doublecolon;
        if (auto linit = _lexer.get(); linit == lex::punctuator::DOUBLE_COLON) {
            initial_doublecolon = lex::as<lex::punctuator>(linit);
        } else {
            _lexer.unget();
        }

        std::vector<lex::identifier> names;

        auto append_segment = [&](const lex::opt_ref_any_lexeme& lname) -> bool {
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

        auto first_name = _lexer.get();
        if (append_segment(first_name)) {
            while (true) {
                bool has_explicit_tpl = false;
                auto qualifier_tpl_args = parse_template_arg_list(&has_explicit_tpl);
                if (has_explicit_tpl) {
                    auto ldoublecolon = _lexer.get();
                    if (ldoublecolon != lex::punctuator::DOUBLE_COLON) {
                        // Not a qualified type call (likely func<T>(...)); let fallback parse it.
                        break;
                    }

                    auto member_name = _lexer.get();
                    if (!append_segment(member_name)) {
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
                        if (!append_segment(next_name)) {
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

                auto maybe_dc = _lexer.get();
                if (maybe_dc != lex::punctuator::DOUBLE_COLON) {
                    _lexer.unget();
                    break;
                }

                auto next_name = _lexer.get();
                if (!append_segment(next_name)) {
                    throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_QNAME_AFTER_INTERMEDIATE_SEP),
                                _lexer.pick_current(),
                                "Qualified identifier expect an identifier after intermediate \"::\"");
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
                        auto des_elem = std::make_shared<ast::designated_init_element>(
                            dot, member_name, qualifier, value);
                        des_elem->set_equal_op(lex::as<lex::operator_>(after_name));
                        elements.push_back(des_elem);
                    } else if (after_name == lex::punctuator::PARENTHESIS_OPEN) {
                        // Constructor form: .member(args...)
                        std::vector<ast::expr_ptr> args;
                        std::optional<lex::punctuator> close_paren_tok;
                        auto peek_close = _lexer.get();
                        if (peek_close != lex::punctuator::PARENTHESIS_CLOSE) {
                            _lexer.unget();
                            while (true) {
                                auto arg = parse_conditional_expr();
                                args.push_back(arg);
                                auto sep = _lexer.get();
                                if (sep == lex::punctuator::PARENTHESIS_CLOSE) {
                                    close_paren_tok = lex::as<lex::punctuator>(sep);
                                    break;
                                }
                                if (sep != lex::punctuator::COMMA) {
                                    throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_DESIGNATED_CTOR_EXPECT_COMMA_CLOSE), sep, "Designated initializer constructor form expects ',' or ')' after argument");
                                }
                            }
                        } else {
                            close_paren_tok = lex::as<lex::punctuator>(peek_close);
                        }
                        auto des_elem = std::make_shared<ast::designated_init_element>(
                            dot, member_name, qualifier, args);
                        des_elem->set_open_paren(lex::as<lex::punctuator>(after_name));
                        if (close_paren_tok) des_elem->set_close_paren(*close_paren_tok);
                        elements.push_back(des_elem);
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
