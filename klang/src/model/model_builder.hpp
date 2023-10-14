/*
 * K Language compiler
 *
 * Copyright 2023 Emilien Kia
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


#ifndef KLANG_MODEL_BUILDER_HPP
#define KLANG_MODEL_BUILDER_HPP

#include "../parse/ast.hpp"
#include "model.hpp"
#include "expressions.hpp"
#include "statements.hpp"
#include "../common/logger.hpp"
#include "../lex/lexer.hpp"
#include <vector>

namespace k::model {

class model_builder : public k::parse::default_ast_visitor, protected lex::lexeme_logger {
protected:

    typedef k::parse::default_ast_visitor super;

    k::model::unit& _unit;

    struct context {
        virtual ~context() = default;
    };

    template<typename T>
    struct generic_context : public context {
        typedef T content_t;
        std::shared_ptr<content_t> content;

        generic_context() = default;
        generic_context(std::shared_ptr<content_t> content): content(content) {}
    };

    struct ns_context : public generic_context<model::ns> {
        typedef generic_context<model::ns> super_t;

        model::visibility visibility = model::DEFAULT;

        ns_context() = default;
        ns_context(std::shared_ptr<model::ns> ns): super_t(ns) {}
    };

    typedef generic_context<model::function> func_context;
    typedef generic_context<model::block> block_context;
    typedef generic_context<model::return_statement> return_context;
    typedef generic_context<model::if_else_statement> if_else_context;
    typedef generic_context<model::while_statement> while_context;
    typedef generic_context<model::for_statement> for_context;
    typedef generic_context<model::expression_statement> expr_stmt_context;

    template<typename T>
    class stack {
    private:
        std::shared_ptr<T> _ctx;
        std::vector<std::shared_ptr<context>>& _contexts;
    public:
        template<class... Args>
        stack(std::vector<std::shared_ptr<context>>& contexts, Args&&... args) :
        _ctx(std::make_shared<T>(std::forward<Args>(args)...)),
        _contexts(contexts)
        { _contexts.push_back(_ctx); }

        ~stack() {_contexts.pop_back();}
    };

    /** Stack of contexts. */
    std::vector<std::shared_ptr<context>> _contexts;

    /** Last generated expression. */
    std::shared_ptr<model::expression> _expr;
    /** Last generated statement. */
    std::shared_ptr<model::statement> _stmt;


    model_builder(k::log::logger& logger, k::model::unit& unit) :
        lex::lexeme_logger(logger, 0x20000),
        _unit(unit) {}


    void visit_unit(parse::ast::unit &) override;
    void visit_module_name(parse::ast::module_name &) override;
    void visit_import(parse::ast::import &) override;

    void visit_identified_type_specifier(parse::ast::identified_type_specifier &) override;
    void visit_keyword_type_specifier(parse::ast::keyword_type_specifier &) override;

    void visit_parameter_specifier(parse::ast::parameter_spec &) override;

    void visit_qualified_identifier(parse::ast::qualified_identifier &) override;

    void visit_visibility_decl(parse::ast::visibility_decl &) override;
    void visit_namespace_decl(parse::ast::namespace_decl &) override;
    void visit_variable_decl(parse::ast::variable_decl &) override;
    void visit_function_decl(parse::ast::function_decl &) override;

    void visit_block_statement(parse::ast::block_statement &) override;
    void visit_return_statement(parse::ast::return_statement &) override;
    void visit_if_else_statement(parse::ast::if_else_statement &) override;
    void visit_while_statement(parse::ast::while_statement &) override;
    void visit_for_statement(parse::ast::for_statement &) override;
    void visit_expression_statement(parse::ast::expression_statement &) override;

    void visit_literal_expr(parse::ast::literal_expr &) override;
    void visit_keyword_expr(parse::ast::keyword_expr &) override;
    void visit_this_expr(parse::ast::keyword_expr &) override;
    void visit_expr_list_expr(parse::ast::expr_list_expr &) override;
    void visit_conditional_expr(parse::ast::conditional_expr &) override;
    void visit_binary_operator_expr(parse::ast::binary_operator_expr &) override;

    void visit_cast_expr(parse::ast::cast_expr &) override;
    void visit_unary_prefix_expr(parse::ast::unary_prefix_expr &) override;
    void visit_unary_postfix_expr(parse::ast::unary_postfix_expr &) override;
    void visit_bracket_postifx_expr(parse::ast::bracket_postifx_expr &) override;
    void visit_parenthesis_postifx_expr(parse::ast::parenthesis_postifx_expr &) override;
    void visit_identifier_expr(parse::ast::identifier_expr &) override;

    void visit_comma_expr(parse::ast::expr_list_expr &) override;

    [[noreturn]] void throw_error(unsigned int code, const lex::lexeme& lexeme, const std::string& message, const std::vector<std::string>& args = {}) {
        error(code, lexeme, message, args);
        throw parse::parsing_error(message);
    }

    [[noreturn]] void throw_error(unsigned int code, const lex::opt_ref_any_lexeme& lexeme, const std::string& message, const std::vector<std::string>& args = {}) {
        error(code, lexeme, message, args);
        throw parse::parsing_error(message);
    }

public:
    static void visit(k::log::logger& logger, k::parse::ast::unit& src, k::model::unit& unit);


};


} // k::parse

#endif //KLANG_MODEL_BUILDER_HPP
