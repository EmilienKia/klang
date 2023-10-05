//
// Created by Emilien Kia <emilien.kia+dev@gmail.com>.
//

#ifndef KLANG_AST_UNIT_VISITOR_HPP
#define KLANG_AST_UNIT_VISITOR_HPP

#include "ast.hpp"
#include "unit.hpp"
#include "logger.hpp"
#include "lexer.hpp"
#include <vector>

namespace k::parse {

class ast_unit_visitor : public k::parse::default_ast_visitor, protected lex::lexeme_logger {
protected:

    typedef k::parse::default_ast_visitor super;

    k::unit::unit& _unit;

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

    struct ns_context : public generic_context<unit::ns> {
        typedef generic_context<unit::ns> super_t;

        unit::visibility visibility = unit::DEFAULT;

        ns_context() = default;
        ns_context(std::shared_ptr<unit::ns> ns): super_t(ns) {}
    };

    typedef generic_context<unit::function> func_context;
    typedef generic_context<unit::block> block_context;
    typedef generic_context<unit::return_statement> return_context;
    typedef generic_context<unit::if_else_statement> if_else_context;
    typedef generic_context<unit::expression_statement> expr_stmt_context;

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
    std::shared_ptr<unit::expression> _expr;
    /** Last generated statement. */
    std::shared_ptr<unit::statement> _stmt;


    ast_unit_visitor(k::log::logger& logger, k::unit::unit& unit) :
        lex::lexeme_logger(logger, 0x20000),
        _unit(unit) {}


    void visit_unit(ast::unit &) override;
    void visit_module_name(ast::module_name &) override;
    void visit_import(ast::import &) override;

    void visit_identified_type_specifier(ast::identified_type_specifier &) override;
    void visit_keyword_type_specifier(ast::keyword_type_specifier &) override;

    void visit_parameter_specifier(ast::parameter_spec &) override;

    void visit_qualified_identifier(ast::qualified_identifier &) override;

    void visit_visibility_decl(ast::visibility_decl &) override;
    void visit_namespace_decl(ast::namespace_decl &) override;
    void visit_variable_decl(ast::variable_decl &) override;
    void visit_function_decl(ast::function_decl &) override;

    void visit_block_statement(ast::block_statement &) override;
    void visit_return_statement(ast::return_statement &) override;
    void visit_if_else_statement(ast::if_else_statement &) override;
    void visit_expression_statement(ast::expression_statement &) override;

    void visit_literal_expr(ast::literal_expr &) override;
    void visit_keyword_expr(ast::keyword_expr &) override;
    void visit_this_expr(ast::keyword_expr &) override;
    void visit_expr_list_expr(ast::expr_list_expr &) override;
    void visit_conditional_expr(ast::conditional_expr &) override;
    void visit_binary_operator_expr(ast::binary_operator_expr &) override;

    void visit_cast_expr(ast::cast_expr &) override;
    void visit_unary_prefix_expr(ast::unary_prefix_expr &) override;
    void visit_unary_postfix_expr(ast::unary_postfix_expr &) override;
    void visit_bracket_postifx_expr(ast::bracket_postifx_expr &) override;
    void visit_parenthesis_postifx_expr(ast::parenthesis_postifx_expr &) override;
    void visit_identifier_expr(ast::identifier_expr &) override;

    void visit_comma_expr(ast::expr_list_expr &) override;

    [[noreturn]] void throw_error(unsigned int code, const lex::lexeme& lexeme, const std::string& message, const std::vector<std::string>& args = {}) {
        error(code, lexeme, message, args);
        throw parsing_error(message);
    }

    [[noreturn]] void throw_error(unsigned int code, const lex::opt_ref_any_lexeme& lexeme, const std::string& message, const std::vector<std::string>& args = {}) {
        error(code, lexeme, message, args);
        throw parsing_error(message);
    }

public:
    static void visit(k::log::logger& logger, k::parse::ast::unit& src, k::unit::unit& unit);


};


} // k::parse

#endif //KLANG_AST_UNIT_VISITOR_HPP
