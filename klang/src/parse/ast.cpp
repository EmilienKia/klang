/*
 * K Language compiler
 *
 * Copyright 2023-2024 Emilien Kia
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

#include "ast.hpp"


namespace k::parse {
//
// AST nodes
//

namespace ast {

// Helper to convert any_literal to opt_any_lexeme
static lex::opt_any_lexeme any_literal_to_opt_any_lexeme(const lex::any_literal& lit) {
    if (std::holds_alternative<lex::integer>(lit)) return {lit.get<lex::integer>()};
    if (std::holds_alternative<lex::float_num>(lit)) return {lit.get<lex::float_num>()};
    if (std::holds_alternative<lex::character>(lit)) return {lit.get<lex::character>()};
    if (std::holds_alternative<lex::string>(lit)) return {lit.get<lex::string>()};
    if (std::holds_alternative<lex::boolean>(lit)) return {lit.get<lex::boolean>()};
    if (std::holds_alternative<lex::null>(lit)) return {lit.get<lex::null>()};
    return std::nullopt;
}

//
// ast_node
//

lex::opt_any_lexeme ast_node::get_first_lexeme() const {
    return std::nullopt;
}

lex::opt_any_lexeme ast_node::get_last_lexeme() const {
    return std::nullopt;
}

lex::opt_any_lexeme ast_node::get_interest_lexeme() const {
    return std::nullopt;
}

//
// import
//

lex::opt_any_lexeme import::get_first_lexeme() const {
    return {import_kw};
}

lex::opt_any_lexeme import::get_last_lexeme() const {
    return semicolon ? lex::opt_any_lexeme{*semicolon} : (qname ? qname->get_last_lexeme() : lex::opt_any_lexeme{import_kw});
}

lex::opt_any_lexeme import::get_interest_lexeme() const {
    return qname ? qname->get_interest_lexeme() : lex::opt_any_lexeme{import_kw};
}

//
// qualified_identifier
//

lex::opt_any_lexeme qualified_identifier::get_first_lexeme() const {
    return initial_doublecolon ? lex::opt_any_lexeme{*initial_doublecolon} : (names.empty() ? lex::opt_any_lexeme{} : lex::opt_any_lexeme{names.front()});
}

lex::opt_any_lexeme qualified_identifier::get_last_lexeme() const {
    return names.empty() ? (initial_doublecolon ? lex::opt_any_lexeme{*initial_doublecolon} : lex::opt_any_lexeme{}) : lex::opt_any_lexeme{names.back()};
}

lex::opt_any_lexeme qualified_identifier::get_interest_lexeme() const {
    return names.empty() ? (initial_doublecolon ? lex::opt_any_lexeme{*initial_doublecolon} : lex::opt_any_lexeme{}) : lex::opt_any_lexeme{names.back()};
}

//
// identified_type_specifier
//

lex::opt_any_lexeme identified_type_specifier::get_first_lexeme() const {
    return name.get_first_lexeme();
}

lex::opt_any_lexeme identified_type_specifier::get_last_lexeme() const {
    if (close_angle) return lex::opt_any_lexeme{*close_angle};
    if (!template_args.empty() && template_args.back()) return template_args.back()->get_last_lexeme();
    return name.get_last_lexeme();
}

lex::opt_any_lexeme identified_type_specifier::get_interest_lexeme() const {
    return name.get_interest_lexeme();
}

//
// keyword_type_specifier
//

lex::opt_any_lexeme keyword_type_specifier::get_first_lexeme() const {
    return unsigned_kw ? lex::opt_any_lexeme{*unsigned_kw} : lex::opt_any_lexeme{keyword};
}

lex::opt_any_lexeme keyword_type_specifier::get_last_lexeme() const {
    return second_kw ? lex::opt_any_lexeme{*second_kw} : lex::opt_any_lexeme{keyword};
}

lex::opt_any_lexeme keyword_type_specifier::get_interest_lexeme() const {
    return {keyword};
}

//
// array_type_specifier
//

lex::opt_any_lexeme array_type_specifier::get_first_lexeme() const {
    return subtype ? subtype->get_first_lexeme() : (br_open ? lex::opt_any_lexeme{*br_open} : lex::opt_any_lexeme{});
}

lex::opt_any_lexeme array_type_specifier::get_last_lexeme() const {
    if (br_close) return lex::opt_any_lexeme{*br_close};
    if (size_expr) return size_expr->get_last_lexeme();
    if (lex_int) return lex::opt_any_lexeme{*lex_int};
    if (subtype) return subtype->get_last_lexeme();
    return lex::opt_any_lexeme{};
}

lex::opt_any_lexeme array_type_specifier::get_interest_lexeme() const {
    return subtype ? subtype->get_interest_lexeme() : (br_open ? lex::opt_any_lexeme{*br_open} : lex::opt_any_lexeme{});
}

//
// pointer_type_specifier
//

lex::opt_any_lexeme pointer_type_specifier::get_first_lexeme() const {
    return subtype ? subtype->get_first_lexeme() : lex::opt_any_lexeme{pointer_type};
}

lex::opt_any_lexeme pointer_type_specifier::get_last_lexeme() const {
    return {pointer_type};
}

lex::opt_any_lexeme pointer_type_specifier::get_interest_lexeme() const {
    return {pointer_type};
}

//
// const_type_specifier
//

lex::opt_any_lexeme const_type_specifier::get_first_lexeme() const {
    return {const_kw};
}

lex::opt_any_lexeme const_type_specifier::get_last_lexeme() const {
    return subtype ? subtype->get_last_lexeme() : lex::opt_any_lexeme{const_kw};
}

lex::opt_any_lexeme const_type_specifier::get_interest_lexeme() const {
    return {const_kw};
}

//
// callable_type_specifier
//

lex::opt_any_lexeme callable_type_specifier::get_first_lexeme() const {
    if (owner) return owner->get_first_lexeme();
    if (addresser) return lex::opt_any_lexeme{*addresser};
    if (open_paren) return lex::opt_any_lexeme{*open_paren};
    if (!param_types.empty() && param_types.front()) return param_types.front()->get_first_lexeme();
    return std::nullopt;
}

lex::opt_any_lexeme callable_type_specifier::get_last_lexeme() const {
    if (throws_close_paren) return lex::opt_any_lexeme{*throws_close_paren};
    if (!throws_spec.empty() && throws_spec.back()) return throws_spec.back()->get_last_lexeme();
    if (throws_kw) return lex::opt_any_lexeme{*throws_kw};
    if (return_type) return return_type->get_last_lexeme();
    if (colon) return lex::opt_any_lexeme{*colon};
    if (close_paren) return lex::opt_any_lexeme{*close_paren};
    if (!param_types.empty() && param_types.back()) return param_types.back()->get_last_lexeme();
    if (open_paren) return lex::opt_any_lexeme{*open_paren};
    if (addresser) return lex::opt_any_lexeme{*addresser};
    if (owner) return owner->get_last_lexeme();
    return std::nullopt;
}

lex::opt_any_lexeme callable_type_specifier::get_interest_lexeme() const {
    if (addresser) return lex::opt_any_lexeme{*addresser};
    if (owner) return owner->get_interest_lexeme();
    if (open_paren) return lex::opt_any_lexeme{*open_paren};
    return get_first_lexeme();
}

//
// owner_type_specifier
//

lex::opt_any_lexeme owner_type_specifier::get_first_lexeme() const {
    return subtype ? subtype->get_first_lexeme() : lex::opt_any_lexeme{owner_op};
}

lex::opt_any_lexeme owner_type_specifier::get_last_lexeme() const {
    return lex::opt_any_lexeme{owner_op};
}

lex::opt_any_lexeme owner_type_specifier::get_interest_lexeme() const {
    return lex::opt_any_lexeme{owner_op};
}

//
// unary_expression
//

lex::opt_any_lexeme unary_expression::get_first_lexeme() const {
    return _expr ? _expr->get_first_lexeme() : lex::opt_any_lexeme{};
}

lex::opt_any_lexeme unary_expression::get_last_lexeme() const {
    return _expr ? _expr->get_last_lexeme() : lex::opt_any_lexeme{};
}

lex::opt_any_lexeme unary_expression::get_interest_lexeme() const {
    return _expr ? _expr->get_interest_lexeme() : lex::opt_any_lexeme{};
}

//
// binary_expression
//

lex::opt_any_lexeme binary_expression::get_first_lexeme() const {
    return _lexpr ? _lexpr->get_first_lexeme() : (_rexpr ? _rexpr->get_first_lexeme() : lex::opt_any_lexeme{});
}

lex::opt_any_lexeme binary_expression::get_last_lexeme() const {
    return _rexpr ? _rexpr->get_last_lexeme() : (_lexpr ? _lexpr->get_last_lexeme() : lex::opt_any_lexeme{});
}

lex::opt_any_lexeme binary_expression::get_interest_lexeme() const {
    return _lexpr ? _lexpr->get_interest_lexeme() : (_rexpr ? _rexpr->get_interest_lexeme() : lex::opt_any_lexeme{});
}

//
// ternary_expression
//

lex::opt_any_lexeme ternary_expression::get_first_lexeme() const {
    return _lexpr ? _lexpr->get_first_lexeme() : (_mexpr ? _mexpr->get_first_lexeme() : (_rexpr ? _rexpr->get_first_lexeme() : lex::opt_any_lexeme{}));
}

lex::opt_any_lexeme ternary_expression::get_last_lexeme() const {
    return _rexpr ? _rexpr->get_last_lexeme() : (_mexpr ? _mexpr->get_last_lexeme() : (_lexpr ? _lexpr->get_last_lexeme() : lex::opt_any_lexeme{}));
}

lex::opt_any_lexeme ternary_expression::get_interest_lexeme() const {
    return _lexpr ? _lexpr->get_interest_lexeme() : (_mexpr ? _mexpr->get_interest_lexeme() : (_rexpr ? _rexpr->get_interest_lexeme() : lex::opt_any_lexeme{}));
}

//
// multi_expression
//

lex::opt_any_lexeme multi_expression::get_first_lexeme() const {
    return !_exprs.empty() && _exprs.front() ? _exprs.front()->get_first_lexeme() : lex::opt_any_lexeme{};
}

lex::opt_any_lexeme multi_expression::get_last_lexeme() const {
    return !_exprs.empty() && _exprs.back() ? _exprs.back()->get_last_lexeme() : lex::opt_any_lexeme{};
}

lex::opt_any_lexeme multi_expression::get_interest_lexeme() const {
    return !_exprs.empty() && _exprs.front() ? _exprs.front()->get_interest_lexeme() : lex::opt_any_lexeme{};
}

//
// expr_list_expr
//

lex::opt_any_lexeme expr_list_expr::get_first_lexeme() const {
    return open_paren ? lex::opt_any_lexeme{*open_paren} : multi_expression::get_first_lexeme();
}

lex::opt_any_lexeme expr_list_expr::get_last_lexeme() const {
    return close_paren ? lex::opt_any_lexeme{*close_paren} : multi_expression::get_last_lexeme();
}

lex::opt_any_lexeme expr_list_expr::get_interest_lexeme() const {
    return multi_expression::get_interest_lexeme();
}

//
// literal_expr
//

lex::opt_any_lexeme literal_expr::get_first_lexeme() const {
    return any_literal_to_opt_any_lexeme(literal);
}

lex::opt_any_lexeme literal_expr::get_last_lexeme() const {
    return any_literal_to_opt_any_lexeme(literal);
}

lex::opt_any_lexeme literal_expr::get_interest_lexeme() const {
    return any_literal_to_opt_any_lexeme(literal);
}

//
// keyword_expr
//

lex::opt_any_lexeme keyword_expr::get_first_lexeme() const {
    return {keyword};
}

lex::opt_any_lexeme keyword_expr::get_last_lexeme() const {
    return {keyword};
}

lex::opt_any_lexeme keyword_expr::get_interest_lexeme() const {
    return {keyword};
}

//
// this_expr
//

lex::opt_any_lexeme this_expr::get_first_lexeme() const {
    return {keyword};
}

lex::opt_any_lexeme this_expr::get_last_lexeme() const {
    return {keyword};
}

lex::opt_any_lexeme this_expr::get_interest_lexeme() const {
    return {keyword};
}

//
// binary_operator_expr
//

lex::opt_any_lexeme binary_operator_expr::get_interest_lexeme() const {
    return {op};
}

//
// conditional_expr
//

lex::opt_any_lexeme conditional_expr::get_interest_lexeme() const {
    return {question_mark};
}

//
// cast_expr
//

lex::opt_any_lexeme cast_expr::get_first_lexeme() const {
    return open_paren ? lex::opt_any_lexeme{*open_paren} : (type ? type->get_first_lexeme() : unary_expression::get_first_lexeme());
}

lex::opt_any_lexeme cast_expr::get_last_lexeme() const {
    return unary_expression::get_last_lexeme();
}

lex::opt_any_lexeme cast_expr::get_interest_lexeme() const {
    return type ? type->get_interest_lexeme() : unary_expression::get_interest_lexeme();
}

//
// new_expr
//

lex::opt_any_lexeme new_expr::get_first_lexeme() const {
    return {new_kw};
}

lex::opt_any_lexeme new_expr::get_last_lexeme() const {
    if (brace_init) {
        return brace_init->get_last_lexeme();
    }
    if (close_bracket) {
        return lex::opt_any_lexeme{*close_bracket};
    }
    if (array_size_expr) {
        return array_size_expr->get_last_lexeme();
    }
    if (close_paren) {
        return lex::opt_any_lexeme{*close_paren};
    }
    if (!args.empty() && args.back()) {
        return args.back()->get_last_lexeme();
    }
    if (type) {
        return type->get_last_lexeme();
    }
    return lex::opt_any_lexeme{new_kw};
}

lex::opt_any_lexeme new_expr::get_interest_lexeme() const {
    return {new_kw};
}

//
// delete_expr
//

lex::opt_any_lexeme delete_expr::get_first_lexeme() const {
    return {delete_kw};
}

lex::opt_any_lexeme delete_expr::get_last_lexeme() const {
    return _expr ? _expr->get_last_lexeme() : lex::opt_any_lexeme{delete_kw};
}

lex::opt_any_lexeme delete_expr::get_interest_lexeme() const {
    return {delete_kw};
}

//
// unary_prefix_expr
//

lex::opt_any_lexeme unary_prefix_expr::get_first_lexeme() const {
    return {op};
}

lex::opt_any_lexeme unary_prefix_expr::get_last_lexeme() const {
    return _expr ? _expr->get_last_lexeme() : lex::opt_any_lexeme{op};
}

lex::opt_any_lexeme unary_prefix_expr::get_interest_lexeme() const {
    return {op};
}

//
// unary_postfix_expr
//

lex::opt_any_lexeme unary_postfix_expr::get_first_lexeme() const {
    return _expr ? _expr->get_first_lexeme() : lex::opt_any_lexeme{op};
}

lex::opt_any_lexeme unary_postfix_expr::get_last_lexeme() const {
    return {op};
}

lex::opt_any_lexeme unary_postfix_expr::get_interest_lexeme() const {
    return {op};
}

//
// bracket_postifx_expr
//

lex::opt_any_lexeme bracket_postifx_expr::get_first_lexeme() const {
    return _lexpr ? _lexpr->get_first_lexeme() : (open_bracket ? lex::opt_any_lexeme{*open_bracket} : (_rexpr ? _rexpr->get_first_lexeme() : lex::opt_any_lexeme{}));
}

lex::opt_any_lexeme bracket_postifx_expr::get_last_lexeme() const {
    return close_bracket ? lex::opt_any_lexeme{*close_bracket} : (_rexpr ? _rexpr->get_last_lexeme() : (_lexpr ? _lexpr->get_last_lexeme() : lex::opt_any_lexeme{}));
}

lex::opt_any_lexeme bracket_postifx_expr::get_interest_lexeme() const {
    return open_bracket ? lex::opt_any_lexeme{*open_bracket} : (_lexpr ? _lexpr->get_interest_lexeme() : lex::opt_any_lexeme{});
}

//
// parenthesis_postifx_expr
//

lex::opt_any_lexeme parenthesis_postifx_expr::get_first_lexeme() const {
    return _lexpr ? _lexpr->get_first_lexeme() : (open_paren ? lex::opt_any_lexeme{*open_paren} : (_rexpr ? _rexpr->get_first_lexeme() : lex::opt_any_lexeme{}));
}

lex::opt_any_lexeme parenthesis_postifx_expr::get_last_lexeme() const {
    return close_paren ? lex::opt_any_lexeme{*close_paren} : (_rexpr ? _rexpr->get_last_lexeme() : (_lexpr ? _lexpr->get_last_lexeme() : lex::opt_any_lexeme{}));
}

lex::opt_any_lexeme parenthesis_postifx_expr::get_interest_lexeme() const {
    return open_paren ? lex::opt_any_lexeme{*open_paren} : (_lexpr ? _lexpr->get_interest_lexeme() : lex::opt_any_lexeme{});
}

//
// member_access_postfix_expr
//

lex::opt_any_lexeme member_access_postfix_expr::get_first_lexeme() const {
    return unary_expression::get_first_lexeme();
}

lex::opt_any_lexeme member_access_postfix_expr::get_last_lexeme() const {
    return ident_expr ? ident_expr->get_last_lexeme() : lex::opt_any_lexeme{op};
}

lex::opt_any_lexeme member_access_postfix_expr::get_interest_lexeme() const {
    return {op};
}

//
// identifier_expr
//

lex::opt_any_lexeme identifier_expr::get_first_lexeme() const {
    return qident.get_first_lexeme();
}

lex::opt_any_lexeme identifier_expr::get_last_lexeme() const {
    if (close_angle) return lex::opt_any_lexeme{*close_angle};
    if (!template_args.empty() && template_args.back()) return template_args.back()->get_last_lexeme();
    return qident.get_last_lexeme();
}

lex::opt_any_lexeme identifier_expr::get_interest_lexeme() const {
    return qident.get_interest_lexeme();
}

//
// brace_postfix_expr
//

lex::opt_any_lexeme brace_postfix_expr::get_first_lexeme() const {
    return callee ? callee->get_first_lexeme() : (brace_init ? brace_init->get_first_lexeme() : lex::opt_any_lexeme{});
}

lex::opt_any_lexeme brace_postfix_expr::get_last_lexeme() const {
    if (brace_init) return brace_init->get_last_lexeme();
    if (array_bracket_close) return lex::opt_any_lexeme{*array_bracket_close};
    if (callee) return callee->get_last_lexeme();
    return lex::opt_any_lexeme{};
}

lex::opt_any_lexeme brace_postfix_expr::get_interest_lexeme() const {
    return callee ? callee->get_interest_lexeme() : (brace_init ? brace_init->get_interest_lexeme() : lex::opt_any_lexeme{});
}

//
// lambda_capture
//

lex::opt_any_lexeme lambda_capture::get_first_lexeme() const {
    if (const_kw) return lex::opt_any_lexeme{*const_kw};
    if (ref_op) return lex::opt_any_lexeme{*ref_op};
    if (this_kw) return lex::opt_any_lexeme{*this_kw};
    if (name) return lex::opt_any_lexeme{*name};
    if (init_expr) return init_expr->get_first_lexeme();
    return std::nullopt;
}

lex::opt_any_lexeme lambda_capture::get_last_lexeme() const {
    if (init_expr) return init_expr->get_last_lexeme();
    if (equal_op) return lex::opt_any_lexeme{*equal_op};
    if (name) return lex::opt_any_lexeme{*name};
    if (this_kw) return lex::opt_any_lexeme{*this_kw};
    if (ref_op) return lex::opt_any_lexeme{*ref_op};
    if (const_kw) return lex::opt_any_lexeme{*const_kw};
    return std::nullopt;
}

lex::opt_any_lexeme lambda_capture::get_interest_lexeme() const {
    if (name) return lex::opt_any_lexeme{*name};
    if (this_kw) return lex::opt_any_lexeme{*this_kw};
    if (ref_op) return lex::opt_any_lexeme{*ref_op};
    if (const_kw) return lex::opt_any_lexeme{*const_kw};
    return get_first_lexeme();
}

//
// lambda_expression
//

lex::opt_any_lexeme lambda_expression::get_first_lexeme() const {
    if (const_kw) return lex::opt_any_lexeme{*const_kw};
    if (capture_open_bracket) return lex::opt_any_lexeme{*capture_open_bracket};
    if (!captures.empty()) return captures.front().get_first_lexeme();
    if (param_open_paren) return lex::opt_any_lexeme{*param_open_paren};
    if (!params.empty() && params.front()) return params.front()->get_first_lexeme();
    if (body) return body->get_first_lexeme();
    return std::nullopt;
}

lex::opt_any_lexeme lambda_expression::get_last_lexeme() const {
    if (body) return body->get_last_lexeme();
    if (return_type) return return_type->get_last_lexeme();
    if (param_close_paren) return lex::opt_any_lexeme{*param_close_paren};
    return get_first_lexeme();
}

lex::opt_any_lexeme lambda_expression::get_interest_lexeme() const {
    if (const_kw) return lex::opt_any_lexeme{*const_kw};
    if (capture_open_bracket) return lex::opt_any_lexeme{*capture_open_bracket};
    if (param_open_paren) return lex::opt_any_lexeme{*param_open_paren};
    if (body) return body->get_interest_lexeme();
    return get_first_lexeme();
}

//
// expression_statement
//

lex::opt_any_lexeme expression_statement::get_first_lexeme() const {
    return expr ? expr->get_first_lexeme() : (semicolon ? lex::opt_any_lexeme{*semicolon} : lex::opt_any_lexeme{});
}

lex::opt_any_lexeme expression_statement::get_last_lexeme() const {
    return semicolon ? lex::opt_any_lexeme{*semicolon} : (expr ? expr->get_last_lexeme() : lex::opt_any_lexeme{});
}

lex::opt_any_lexeme expression_statement::get_interest_lexeme() const {
    return expr ? expr->get_interest_lexeme() : (semicolon ? lex::opt_any_lexeme{*semicolon} : lex::opt_any_lexeme{});
}

//
// block_statement
//

lex::opt_any_lexeme block_statement::get_first_lexeme() const {
    if (!open_brace.content.empty()) return {open_brace};
    if (!statements.empty() && statements.front()) return statements.front()->get_first_lexeme();
    return std::nullopt;
}

lex::opt_any_lexeme block_statement::get_last_lexeme() const {
    if (!close_brace.content.empty()) return {close_brace};
    if (!statements.empty() && statements.back()) return statements.back()->get_last_lexeme();
    return std::nullopt;
}

lex::opt_any_lexeme block_statement::get_interest_lexeme() const {
    if (!open_brace.content.empty()) return {open_brace};
    if (!statements.empty() && statements.front()) return statements.front()->get_interest_lexeme();
    return std::nullopt;
}

//
// return_statement
//

lex::opt_any_lexeme return_statement::get_first_lexeme() const {
    return {ret};
}

lex::opt_any_lexeme return_statement::get_last_lexeme() const {
    return semicolon ? lex::opt_any_lexeme{*semicolon} : (expr ? expr->get_last_lexeme() : lex::opt_any_lexeme{ret});
}

lex::opt_any_lexeme return_statement::get_interest_lexeme() const {
    return {ret};
}

//
// break_statement
//

lex::opt_any_lexeme break_statement::get_first_lexeme() const {
    return {break_kw};
}

lex::opt_any_lexeme break_statement::get_last_lexeme() const {
    return semicolon ? lex::opt_any_lexeme{*semicolon} : lex::opt_any_lexeme{break_kw};
}

lex::opt_any_lexeme break_statement::get_interest_lexeme() const {
    return {break_kw};
}

//
// continue_statement
//

lex::opt_any_lexeme continue_statement::get_first_lexeme() const {
    return {continue_kw};
}

lex::opt_any_lexeme continue_statement::get_last_lexeme() const {
    return semicolon ? lex::opt_any_lexeme{*semicolon} : lex::opt_any_lexeme{continue_kw};
}

lex::opt_any_lexeme continue_statement::get_interest_lexeme() const {
    return {continue_kw};
}

//
// throw_statement
//

lex::opt_any_lexeme throw_statement::get_first_lexeme() const {
    return {throw_kw};
}

lex::opt_any_lexeme throw_statement::get_last_lexeme() const {
    return semicolon ? lex::opt_any_lexeme{*semicolon} : (expr ? expr->get_last_lexeme() : lex::opt_any_lexeme{throw_kw});
}

lex::opt_any_lexeme throw_statement::get_interest_lexeme() const {
    return {throw_kw};
}

//
// catch_clause
//

lex::opt_any_lexeme catch_clause::get_first_lexeme() const {
    return {catch_kw};
}

lex::opt_any_lexeme catch_clause::get_last_lexeme() const {
    if (body) return body->get_last_lexeme();
    if (close_paren) return lex::opt_any_lexeme{*close_paren};
    if (var_type) return var_type->get_last_lexeme();
    return lex::opt_any_lexeme{var_name};
}

lex::opt_any_lexeme catch_clause::get_interest_lexeme() const {
    return {var_name};
}

//
// try_catch_statement
//

lex::opt_any_lexeme try_catch_statement::get_first_lexeme() const {
    return {try_kw};
}

lex::opt_any_lexeme try_catch_statement::get_last_lexeme() const {
    if (finally_body) return finally_body->get_last_lexeme();
    if (!catch_clauses.empty() && catch_clauses.back()) return catch_clauses.back()->get_last_lexeme();
    if (try_body) return try_body->get_last_lexeme();
    return lex::opt_any_lexeme{try_kw};
}

lex::opt_any_lexeme try_catch_statement::get_interest_lexeme() const {
    return {try_kw};
}

//
// if_else_statement
//

lex::opt_any_lexeme if_else_statement::get_first_lexeme() const {
    return {if_kw};
}

lex::opt_any_lexeme if_else_statement::get_last_lexeme() const {
    if (else_stmt) return else_stmt->get_last_lexeme();
    if (then_stmt) return then_stmt->get_last_lexeme();
    if (close_paren) return lex::opt_any_lexeme{*close_paren};
    if (test_expr) return test_expr->get_last_lexeme();
    return lex::opt_any_lexeme{if_kw};
}

lex::opt_any_lexeme if_else_statement::get_interest_lexeme() const {
    return {if_kw};
}

//
// while_statement
//

lex::opt_any_lexeme while_statement::get_first_lexeme() const {
    return {while_kw};
}

lex::opt_any_lexeme while_statement::get_last_lexeme() const {
    if (nested_stmt) return nested_stmt->get_last_lexeme();
    if (close_paren) return lex::opt_any_lexeme{*close_paren};
    if (test_expr) return test_expr->get_last_lexeme();
    return lex::opt_any_lexeme{while_kw};
}

lex::opt_any_lexeme while_statement::get_interest_lexeme() const {
    return {while_kw};
}

//
// for_statement
//

lex::opt_any_lexeme for_statement::get_first_lexeme() const {
    return {for_kw};
}

lex::opt_any_lexeme for_statement::get_last_lexeme() const {
    if (nested_stmt) return nested_stmt->get_last_lexeme();
    if (close_paren) return lex::opt_any_lexeme{*close_paren};
    if (step_expr) return step_expr->get_last_lexeme();
    if (test_expr) return test_expr->get_last_lexeme();
    if (decl_expr) return decl_expr->get_last_lexeme();
    return lex::opt_any_lexeme{for_kw};
}

lex::opt_any_lexeme for_statement::get_interest_lexeme() const {
    return {for_kw};
}

//
// foreach_statement
//

lex::opt_any_lexeme foreach_statement::get_first_lexeme() const {
    return {for_kw};
}

lex::opt_any_lexeme foreach_statement::get_last_lexeme() const {
    if (nested_stmt) return nested_stmt->get_last_lexeme();
    if (close_paren) return lex::opt_any_lexeme{*close_paren};
    if (decl_expr) return decl_expr->get_last_lexeme();
    return lex::opt_any_lexeme{for_kw};
}

lex::opt_any_lexeme foreach_statement::get_interest_lexeme() const {
    return {for_kw};
}

//
// visibility_decl
//

lex::opt_any_lexeme visibility_decl::get_first_lexeme() const {
    return {scope};
}

lex::opt_any_lexeme visibility_decl::get_last_lexeme() const {
    return colon ? lex::opt_any_lexeme{*colon} : lex::opt_any_lexeme{scope};
}

lex::opt_any_lexeme visibility_decl::get_interest_lexeme() const {
    return {scope};
}

//
// namespace_decl
//

lex::opt_any_lexeme namespace_decl::get_first_lexeme() const {
    return {ns};
}

lex::opt_any_lexeme namespace_decl::get_last_lexeme() const {
    if (close_brace) return lex::opt_any_lexeme{*close_brace};
    if (!close_par.content.empty()) return lex::opt_any_lexeme{close_par};
    if (!declarations.empty() && declarations.back()) return declarations.back()->get_last_lexeme();
    if (name) return lex::opt_any_lexeme{*name};
    return lex::opt_any_lexeme{ns};
}

lex::opt_any_lexeme namespace_decl::get_interest_lexeme() const {
    return name ? lex::opt_any_lexeme{*name} : lex::opt_any_lexeme{ns};
}

//
// using_decl
//

lex::opt_any_lexeme using_decl::get_first_lexeme() const {
    return {using_kw};
}

lex::opt_any_lexeme using_decl::get_last_lexeme() const {
    return semicolon ? lex::opt_any_lexeme{*semicolon} : (qname ? qname->get_last_lexeme() : lex::opt_any_lexeme{using_kw});
}

lex::opt_any_lexeme using_decl::get_interest_lexeme() const {
    return alias_name ? lex::opt_any_lexeme{*alias_name} : (qname ? qname->get_interest_lexeme() : lex::opt_any_lexeme{using_kw});
}

//
// alias_decl
//

lex::opt_any_lexeme alias_decl::get_first_lexeme() const {
    return !specifiers.empty() ? lex::opt_any_lexeme{specifiers.front()} : lex::opt_any_lexeme{alias_kw};
}

lex::opt_any_lexeme alias_decl::get_last_lexeme() const {
    if (semicolon) return lex::opt_any_lexeme{*semicolon};
    if (type) return type->get_last_lexeme();
    if (qname) return qname->get_last_lexeme();
    return lex::opt_any_lexeme{name};
}

lex::opt_any_lexeme alias_decl::get_interest_lexeme() const {
    return {name};
}

//
// friend_decl
//

lex::opt_any_lexeme friend_decl::get_first_lexeme() const {
    return {friend_kw};
}

lex::opt_any_lexeme friend_decl::get_last_lexeme() const {
    if (semicolon) return lex::opt_any_lexeme{*semicolon};
    if (close_angle) return lex::opt_any_lexeme{*close_angle};
    if (!template_args.empty() && template_args.back()) return template_args.back()->get_last_lexeme();
    if (qname) return qname->get_last_lexeme();
    return lex::opt_any_lexeme{friend_kw};
}

lex::opt_any_lexeme friend_decl::get_interest_lexeme() const {
    return qname ? qname->get_interest_lexeme() : lex::opt_any_lexeme{friend_kw};
}

//
// annotation_def
//

lex::opt_any_lexeme annotation_def::get_first_lexeme() const {
    return {at_sign};
}

lex::opt_any_lexeme annotation_def::get_last_lexeme() const {
    if (brace_init) return brace_init->get_last_lexeme();
    if (close_paren) return lex::opt_any_lexeme{*close_paren};
    if (!args.empty() && args.back()) return args.back()->get_last_lexeme();
    if (name) return name->get_last_lexeme();
    return lex::opt_any_lexeme{at_sign};
}

lex::opt_any_lexeme annotation_def::get_interest_lexeme() const {
    return name ? name->get_interest_lexeme() : lex::opt_any_lexeme{at_sign};
}

//
// annotation_init_expr
//

lex::opt_any_lexeme annotation_init_expr::get_first_lexeme() const {
    return annotation ? annotation->get_first_lexeme() : lex::opt_any_lexeme{};
}

lex::opt_any_lexeme annotation_init_expr::get_last_lexeme() const {
    return annotation ? annotation->get_last_lexeme() : lex::opt_any_lexeme{};
}

lex::opt_any_lexeme annotation_init_expr::get_interest_lexeme() const {
    return annotation ? annotation->get_interest_lexeme() : lex::opt_any_lexeme{};
}

//
// pack_expansion_expr
//

lex::opt_any_lexeme pack_expansion_expr::get_first_lexeme() const {
    return inner ? inner->get_first_lexeme() : (ellipsis ? lex::opt_any_lexeme{*ellipsis} : lex::opt_any_lexeme{});
}

lex::opt_any_lexeme pack_expansion_expr::get_last_lexeme() const {
    return ellipsis ? lex::opt_any_lexeme{*ellipsis} : (inner ? inner->get_last_lexeme() : lex::opt_any_lexeme{});
}

lex::opt_any_lexeme pack_expansion_expr::get_interest_lexeme() const {
    return inner ? inner->get_interest_lexeme() : (ellipsis ? lex::opt_any_lexeme{*ellipsis} : lex::opt_any_lexeme{});
}

//
// template_parameter
//

lex::opt_any_lexeme template_parameter::get_first_lexeme() const {
    if (kind_kw) return lex::opt_any_lexeme{*kind_kw};
    if (value_type) return value_type->get_first_lexeme();
    return lex::opt_any_lexeme{name};
}

lex::opt_any_lexeme template_parameter::get_last_lexeme() const {
    if (default_expr) return default_expr->get_last_lexeme();
    if (default_type_spec) return default_type_spec->get_last_lexeme();
    if (constraint_type) return constraint_type->get_last_lexeme();
    if (value_type) return value_type->get_last_lexeme();
    if (ellipsis) return lex::opt_any_lexeme{*ellipsis};
    return lex::opt_any_lexeme{name};
}

lex::opt_any_lexeme template_parameter::get_interest_lexeme() const {
    return {name};
}

//
// template_arg
//

lex::opt_any_lexeme template_arg::get_first_lexeme() const {
    return type_arg ? type_arg->get_first_lexeme() : (value_arg ? value_arg->get_first_lexeme() : lex::opt_any_lexeme{});
}

lex::opt_any_lexeme template_arg::get_last_lexeme() const {
    return type_arg ? type_arg->get_last_lexeme() : (value_arg ? value_arg->get_last_lexeme() : lex::opt_any_lexeme{});
}

lex::opt_any_lexeme template_arg::get_interest_lexeme() const {
    return type_arg ? type_arg->get_interest_lexeme() : (value_arg ? value_arg->get_interest_lexeme() : lex::opt_any_lexeme{});
}

//
// aggregate_decl
//

lex::opt_any_lexeme aggregate_decl::get_first_lexeme() const {
    if (!annotations.empty() && annotations.front()) return annotations.front()->get_first_lexeme();
    if (!specifiers.empty()) return lex::opt_any_lexeme{specifiers.front()};
    return lex::opt_any_lexeme{kw_aggregate_type};
}

lex::opt_any_lexeme aggregate_decl::get_last_lexeme() const {
    if (!close_brace.content.empty()) return lex::opt_any_lexeme{close_brace};
    if (!declarations.empty() && declarations.back()) return declarations.back()->get_last_lexeme();
    if (!bases.empty()) return lex::opt_any_lexeme{bases.back().name};
    return lex::opt_any_lexeme{name};
}

lex::opt_any_lexeme aggregate_decl::get_interest_lexeme() const {
    return {name};
}

//
// designated_init_element
//

lex::opt_any_lexeme designated_init_element::get_first_lexeme() const {
    return {dot};
}

lex::opt_any_lexeme designated_init_element::get_last_lexeme() const {
    if (is_call_form) {
        if (close_paren) return lex::opt_any_lexeme{*close_paren};
        if (!args.empty() && args.back()) return args.back()->get_last_lexeme();
        return lex::opt_any_lexeme{member_name};
    }
    if (value) return value->get_last_lexeme();
    if (equal_op) return lex::opt_any_lexeme{*equal_op};
    return lex::opt_any_lexeme{member_name};
}

lex::opt_any_lexeme designated_init_element::get_interest_lexeme() const {
    return {member_name};
}

//
// brace_init_list
//

lex::opt_any_lexeme brace_init_list::get_first_lexeme() const {
    if (!open_brace.content.empty()) return {open_brace};
    if (!elements.empty() && elements.front()) return elements.front()->get_first_lexeme();
    return std::nullopt;
}

lex::opt_any_lexeme brace_init_list::get_last_lexeme() const {
    if (!close_brace.content.empty()) return {close_brace};
    if (!elements.empty() && elements.back()) return elements.back()->get_last_lexeme();
    return std::nullopt;
}

lex::opt_any_lexeme brace_init_list::get_interest_lexeme() const {
    if (!open_brace.content.empty()) return {open_brace};
    if (!elements.empty() && elements.front()) return elements.front()->get_interest_lexeme();
    return std::nullopt;
}

//
// variable_decl
//

lex::opt_any_lexeme variable_decl::get_first_lexeme() const {
    return !specifiers.empty() ? lex::opt_any_lexeme{specifiers.front()} : lex::opt_any_lexeme{name};
}

lex::opt_any_lexeme variable_decl::get_last_lexeme() const {
    if (semicolon) return lex::opt_any_lexeme{*semicolon};
    if (init) return init->get_last_lexeme();
    if (close_paren) return lex::opt_any_lexeme{*close_paren};
    if (type) return type->get_last_lexeme();
    return lex::opt_any_lexeme{name};
}

lex::opt_any_lexeme variable_decl::get_interest_lexeme() const {
    return {name};
}

//
// parameter_spec
//

lex::opt_any_lexeme parameter_spec::get_first_lexeme() const {
    if (!annotations.empty() && annotations.front()) return annotations.front()->get_first_lexeme();
    if (!specifiers.empty()) return lex::opt_any_lexeme{specifiers.front()};
    if (name) return lex::opt_any_lexeme{*name};
    if (type) return type->get_first_lexeme();
    if (ellipsis) return lex::opt_any_lexeme{*ellipsis};
    return std::nullopt;
}

lex::opt_any_lexeme parameter_spec::get_last_lexeme() const {
    if (default_expr) return default_expr->get_last_lexeme();
    if (equal_op) return lex::opt_any_lexeme{*equal_op};
    if (type) return type->get_last_lexeme();
    if (colon) return lex::opt_any_lexeme{*colon};
    if (name) return lex::opt_any_lexeme{*name};
    if (ellipsis) return lex::opt_any_lexeme{*ellipsis};
    return get_first_lexeme();
}

lex::opt_any_lexeme parameter_spec::get_interest_lexeme() const {
    if (name) return lex::opt_any_lexeme{*name};
    if (type) return type->get_interest_lexeme();
    if (ellipsis) return lex::opt_any_lexeme{*ellipsis};
    return get_first_lexeme();
}

//
// function_decl
//

lex::opt_any_lexeme function_decl::get_first_lexeme() const {
    if (!annotations.empty() && annotations.front()) return annotations.front()->get_first_lexeme();
    if (!specifiers.empty()) return lex::opt_any_lexeme{specifiers.front()};
    if (tilde) return lex::opt_any_lexeme{*tilde};
    if (is_operator && operator_) return operator_;
    return lex::opt_any_lexeme{name};
}

lex::opt_any_lexeme function_decl::get_last_lexeme() const {
    if (content) return content->get_last_lexeme();
    if (semicolon) return lex::opt_any_lexeme{*semicolon};
    if (!throws_spec.empty() && throws_spec.back()) return throws_spec.back()->get_last_lexeme();
    if (type) return type->get_last_lexeme();
    if (close_paren) return lex::opt_any_lexeme{*close_paren};
    return lex::opt_any_lexeme{name};
}

lex::opt_any_lexeme function_decl::get_interest_lexeme() const {
    if (is_operator && operator_symbol) return operator_symbol;
    return lex::opt_any_lexeme{name};
}

//
// enum_entry
//

lex::opt_any_lexeme enum_entry::get_first_lexeme() const {
    return {name};
}

lex::opt_any_lexeme enum_entry::get_last_lexeme() const {
    if (semicolon) return lex::opt_any_lexeme{*semicolon};
    if (default_kw) return lex::opt_any_lexeme{*default_kw};
    if (brace_init) return brace_init->get_last_lexeme();
    if (close_paren) return lex::opt_any_lexeme{*close_paren};
    if (!ctor_args.empty() && ctor_args.back()) return ctor_args.back()->get_last_lexeme();
    if (ref_value) return lex::opt_any_lexeme{*ref_value};
    if (literal_value) return any_literal_to_opt_any_lexeme(*literal_value);
    if (equal_op) return lex::opt_any_lexeme{*equal_op};
    return {name};
}

lex::opt_any_lexeme enum_entry::get_interest_lexeme() const {
    return {name};
}

//
// enum_decl
//

lex::opt_any_lexeme enum_decl::get_first_lexeme() const {
    return !specifiers.empty() ? lex::opt_any_lexeme{specifiers.front()} : lex::opt_any_lexeme{kw_enum};
}

lex::opt_any_lexeme enum_decl::get_last_lexeme() const {
    if (semicolon) return lex::opt_any_lexeme{*semicolon};
    if (!close_brace.content.empty()) return lex::opt_any_lexeme{close_brace};
    if (!entries.empty() && entries.back()) return entries.back()->get_last_lexeme();
    return lex::opt_any_lexeme{name};
}

lex::opt_any_lexeme enum_decl::get_interest_lexeme() const {
    return {name};
}

//
// module_name
//

lex::opt_any_lexeme module_name::get_first_lexeme() const {
    return {module};
}

lex::opt_any_lexeme module_name::get_last_lexeme() const {
    return semicolon ? lex::opt_any_lexeme{*semicolon} : (qname ? qname->get_last_lexeme() : lex::opt_any_lexeme{module});
}

lex::opt_any_lexeme module_name::get_interest_lexeme() const {
    return qname ? qname->get_interest_lexeme() : lex::opt_any_lexeme{module};
}

//
// unit
//

lex::opt_any_lexeme unit::get_first_lexeme() const {
    return module_name ? module_name->get_first_lexeme() : (!imports.empty() && imports.front() ? imports.front()->get_first_lexeme() : (!declarations.empty() && declarations.front() ? declarations.front()->get_first_lexeme() : lex::opt_any_lexeme{}));
}

lex::opt_any_lexeme unit::get_last_lexeme() const {
    return !declarations.empty() && declarations.back() ? declarations.back()->get_last_lexeme() : (!imports.empty() && imports.back() ? imports.back()->get_last_lexeme() : (module_name ? module_name->get_last_lexeme() : lex::opt_any_lexeme{}));
}

lex::opt_any_lexeme unit::get_interest_lexeme() const {
    return module_name ? module_name->get_interest_lexeme() : (!imports.empty() && imports.front() ? imports.front()->get_interest_lexeme() : (!declarations.empty() && declarations.front() ? declarations.front()->get_interest_lexeme() : lex::opt_any_lexeme{}));
}

} // namespace ast



//
// Visitor mechanism
//
void ast::unit::visit(ast_visitor &visitor) {
    visitor.visit_unit(*this);
}

void ast::module_name::visit(ast_visitor &visitor) {
    visitor.visit_module_name(*this);
}

void ast::import::visit(ast_visitor &visitor) {
    visitor.visit_import(*this);
}

void ast::qualified_identifier::visit(ast_visitor &visitor) {
    visitor.visit_qualified_identifier(*this);
}

void ast::visibility_decl::visit(ast_visitor &visitor) {
    visitor.visit_visibility_decl(*this);
}

void ast::namespace_decl::visit(ast_visitor &visitor) {
    visitor.visit_namespace_decl(*this);
}

void ast::using_decl::visit(ast_visitor &visitor) {
    visitor.visit_using_decl(*this);
}

void ast::alias_decl::visit(ast_visitor &visitor) {
    visitor.visit_alias_decl(*this);
}

void ast::friend_decl::visit(ast_visitor &visitor) {
    visitor.visit_friend_decl(*this);
}

void ast::aggregate_decl::visit(ast_visitor &visitor) {
    visitor.visit_aggregate_decl(*this);
}

void ast::enum_entry::visit(ast_visitor &visitor) {
    // enum entries are visited as part of enum_decl
}

void ast::enum_decl::visit(ast_visitor &visitor) {
    visitor.visit_enum_decl(*this);
}

void ast::identified_type_specifier::visit(ast_visitor &visitor) {
    visitor.visit_identified_type_specifier(*this);
}

void ast::keyword_type_specifier::visit(ast_visitor &visitor) {
    visitor.visit_keyword_type_specifier(*this);
}

void ast::array_type_specifier::visit(ast_visitor &visitor) {
    visitor.visit_array_type_specifier(*this);
}

void ast::pointer_type_specifier::visit(ast_visitor &visitor) {
    visitor.visit_pointer_type_specifier(*this);
}

void ast::const_type_specifier::visit(ast_visitor &visitor) {
    visitor.visit_const_type_specifier(*this);
}

void ast::callable_type_specifier::visit(ast_visitor &visitor) {
    visitor.visit_callable_type_specifier(*this);
}

void ast::owner_type_specifier::visit(ast_visitor &visitor) {
    visitor.visit_owner_type_specifier(*this);
}

void ast::parameter_spec::visit(ast_visitor &visitor) {
    visitor.visit_parameter_specifier(*this);
}

void ast::variable_decl::visit(ast_visitor &visitor) {
    visitor.visit_variable_decl(*this);
}

void ast::function_decl::visit(ast_visitor &visitor) {
    visitor.visit_function_decl(*this);
}

void ast::block_statement::visit(ast_visitor &visitor) {
    visitor.visit_block_statement(*this);
}

void ast::return_statement::visit(ast_visitor &visitor) {
    visitor.visit_return_statement(*this);
}

void ast::break_statement::visit(ast_visitor &visitor) {
    visitor.visit_break_statement(*this);
}

void ast::continue_statement::visit(ast_visitor &visitor) {
    visitor.visit_continue_statement(*this);
}

void ast::throw_statement::visit(ast_visitor &visitor) {
    visitor.visit_throw_statement(*this);
}

void ast::catch_clause::visit(ast_visitor &visitor) {
    visitor.visit_catch_clause(*this);
}

void ast::try_catch_statement::visit(ast_visitor &visitor) {
    visitor.visit_try_catch_statement(*this);
}

void ast::if_else_statement::visit(ast_visitor &visitor) {
    visitor.visit_if_else_statement(*this);
}

void ast::while_statement::visit(ast_visitor &visitor) {
    visitor.visit_while_statement(*this);
}

void ast::for_statement::visit(ast_visitor &visitor) {
    visitor.visit_for_statement(*this);
}

void ast::foreach_statement::visit(ast_visitor &visitor) {
    visitor.visit_foreach_statement(*this);
}

void ast::expression_statement::visit(ast_visitor& visitor) {
    visitor.visit_expression_statement(*this);
}

void ast::expr_list_expr::visit(ast_visitor& visitor) {
    visitor.visit_comma_expr(*this);
}

void ast::binary_operator_expr::visit(ast_visitor& visitor) {
    visitor.visit_binary_operator_expr(*this);
}

void ast::conditional_expr::visit(ast_visitor& visitor) {
    visitor.visit_conditional_expr(*this);
}

void ast::cast_expr::visit(ast_visitor& visitor) {
    visitor.visit_cast_expr(*this);
}

void ast::unary_prefix_expr::visit(ast_visitor& visitor) {
    visitor.visit_unary_prefix_expr(*this);
}

void ast::unary_postfix_expr::visit(ast_visitor& visitor) {
    visitor.visit_unary_postfix_expr(*this);
}

void ast::bracket_postifx_expr::visit(ast_visitor &visitor)
{
    visitor.visit_bracket_postifx_expr(*this);
}

void ast::parenthesis_postifx_expr::visit(ast_visitor &visitor)
{
    visitor.visit_parenthesis_postifx_expr(*this);
}

void ast::member_access_postfix_expr::visit(ast_visitor &visitor)
{
    visitor.visit_member_access_postfix_expr(*this);
}

void ast::brace_postfix_expr::visit(ast_visitor &visitor)
{
    visitor.visit_brace_postfix_expr(*this);
}

void ast::lambda_capture::visit(ast_visitor &visitor)
{
    visitor.visit_lambda_capture(*this);
}

void ast::lambda_expression::visit(ast_visitor &visitor)
{
    visitor.visit_lambda_expression(*this);
}

void ast::literal_expr::visit(ast_visitor& visitor) {
    visitor.visit_literal_expr(*this);
}

void ast::keyword_expr::visit(ast_visitor &visitor) {
    visitor.visit_keyword_expr(*this);
}

void ast::this_expr::visit(ast_visitor &visitor) {
    visitor.visit_this_expr(*this);
}

void ast::identifier_expr::visit(ast_visitor &visitor) {
    visitor.visit_identifier_expr(*this);
}

void ast::new_expr::visit(ast_visitor &visitor) {
    visitor.visit_new_expr(*this);
}

void ast::delete_expr::visit(ast_visitor &visitor) {
    visitor.visit_delete_expr(*this);
}

void ast::brace_init_list::visit(ast_visitor &visitor) {
    visitor.visit_brace_init_list(*this);
}

void ast::designated_init_element::visit(ast_visitor &visitor) {
    visitor.visit_designated_init_element(*this);
}

void ast::annotation_def::visit(ast_visitor &visitor) {
    visitor.visit_annotation_def(*this);
}

void ast::annotation_init_expr::visit(ast_visitor &visitor) {
    visitor.visit_annotation_init_expr(*this);
}

void ast::pack_expansion_expr::visit(ast_visitor &visitor) {
    visitor.visit_pack_expansion_expr(*this);
}

void ast::template_parameter::visit(ast_visitor &visitor) {
    visitor.visit_template_parameter(*this);
}

void ast::template_arg::visit(ast_visitor &visitor) {
    visitor.visit_template_arg(*this);
}

//
// Default AST visitor
//

void default_ast_visitor::visit_unit(ast::unit& unit) {
    if(unit.module_name) {
        unit.module_name->visit(*this);
    }

    for(auto import : unit.imports) {
        import->visit(*this);
    }
    for(ast::decl_ptr& decl : unit.declarations) {
        decl->visit(*this);
    }
}

void default_ast_visitor::visit_module_name(ast::module_name &) {
}

void default_ast_visitor::visit_import(ast::import &) {
}


void default_ast_visitor::visit_identified_type_specifier(ast::identified_type_specifier &) {

}

void default_ast_visitor::visit_keyword_type_specifier(ast::keyword_type_specifier &) {

}

void default_ast_visitor::visit_array_type_specifier(ast::array_type_specifier &) {

}

void default_ast_visitor::visit_pointer_type_specifier(ast::pointer_type_specifier &) {

}

void default_ast_visitor::visit_const_type_specifier(ast::const_type_specifier &) {

}

void default_ast_visitor::visit_callable_type_specifier(ast::callable_type_specifier &) {

}

void default_ast_visitor::visit_owner_type_specifier(ast::owner_type_specifier &) {

}

void default_ast_visitor::visit_parameter_specifier(ast::parameter_spec &) {

}

void default_ast_visitor::visit_qualified_identifier(ast::qualified_identifier &) {

}

void default_ast_visitor::visit_visibility_decl(ast::visibility_decl &) {

}

void default_ast_visitor::visit_namespace_decl(ast::namespace_decl &ns) {
    for(ast::decl_ptr& decl : ns.declarations) {
        decl->visit(*this);
    }
}

void default_ast_visitor::visit_using_decl(ast::using_decl &) {
}

void default_ast_visitor::visit_alias_decl(ast::alias_decl &) {
}

void default_ast_visitor::visit_friend_decl(ast::friend_decl &) {
}

void default_ast_visitor::visit_aggregate_decl(ast::aggregate_decl &st) {
    for(ast::decl_ptr& decl : st.declarations) {
        decl->visit(*this);
    }
}

void default_ast_visitor::visit_enum_decl(ast::enum_decl &) {

}

void default_ast_visitor::visit_variable_decl(ast::variable_decl &) {

}

void default_ast_visitor::visit_function_decl(ast::function_decl &) {

}

void default_ast_visitor::visit_block_statement(ast::block_statement &) {

}

void default_ast_visitor::visit_return_statement(ast::return_statement &) {

}

void default_ast_visitor::visit_break_statement(ast::break_statement &) {

}

void default_ast_visitor::visit_continue_statement(ast::continue_statement &) {

}

void default_ast_visitor::visit_throw_statement(ast::throw_statement &) {

}

void default_ast_visitor::visit_try_catch_statement(ast::try_catch_statement &) {

}

void default_ast_visitor::visit_catch_clause(ast::catch_clause &) {

}

void default_ast_visitor::visit_if_else_statement(ast::if_else_statement &) {

}

void default_ast_visitor::visit_while_statement(ast::while_statement &) {

}

void default_ast_visitor::visit_for_statement(ast::for_statement &) {

}

void default_ast_visitor::visit_foreach_statement(ast::foreach_statement &) {

}

void default_ast_visitor::visit_expression_statement(ast::expression_statement &) {

}

void default_ast_visitor::visit_literal_expr(ast::literal_expr &) {

}

void default_ast_visitor::visit_keyword_expr(ast::keyword_expr &) {

}

void default_ast_visitor::visit_this_expr(ast::keyword_expr &) {

}

void default_ast_visitor::visit_expr_list_expr(ast::expr_list_expr &) {

}

void default_ast_visitor::visit_conditional_expr(ast::conditional_expr &) {

}

void default_ast_visitor::visit_binary_operator_expr(ast::binary_operator_expr &) {

}

void default_ast_visitor::visit_cast_expr(ast::cast_expr &) {

}

void default_ast_visitor::visit_unary_prefix_expr(ast::unary_prefix_expr &) {

}

void default_ast_visitor::visit_unary_postfix_expr(ast::unary_postfix_expr &) {

}

void default_ast_visitor::visit_bracket_postifx_expr(ast::bracket_postifx_expr &) {

}

void default_ast_visitor::visit_parenthesis_postifx_expr(ast::parenthesis_postifx_expr &) {

}

void default_ast_visitor::visit_member_access_postfix_expr(ast::member_access_postfix_expr &) {

}

void default_ast_visitor::visit_brace_postfix_expr(ast::brace_postfix_expr &) {

}

void default_ast_visitor::visit_lambda_capture(ast::lambda_capture &) {

}

void default_ast_visitor::visit_lambda_expression(ast::lambda_expression &) {

}

void default_ast_visitor::visit_identifier_expr(ast::identifier_expr &) {

}

void default_ast_visitor::visit_new_expr(ast::new_expr &) {

}

void default_ast_visitor::visit_delete_expr(ast::delete_expr &) {

}

void default_ast_visitor::visit_brace_init_list(ast::brace_init_list &) {

}

void default_ast_visitor::visit_designated_init_element(ast::designated_init_element &) {

}

void default_ast_visitor::visit_annotation_def(ast::annotation_def &) {

}

void default_ast_visitor::visit_annotation_init_expr(ast::annotation_init_expr &) {

}

void default_ast_visitor::visit_pack_expansion_expr(ast::pack_expansion_expr &) {

}

void default_ast_visitor::visit_template_parameter(ast::template_parameter &) {

}

void default_ast_visitor::visit_template_arg(ast::template_arg &) {

}

void default_ast_visitor::visit_comma_expr(ast::expr_list_expr &) {

}


} // k::parse
