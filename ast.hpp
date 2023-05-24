//
// Created by Emilien Kia <emilien.kia+dev@gmail.com>
//

#ifndef KLANG_AST_HPP
#define KLANG_AST_HPP

#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <sstream>
#include <variant>
#include <vector>

#include "any_of.hpp"
#include "lexer.hpp"

namespace k::parse {

    class ast_visitor;

    namespace ast {

        struct ast_node {
            virtual void visit(ast_visitor &visitor) = 0;
        };

        struct import : public ast_node {
            lex::identifier name;

            import(const lex::identifier &name) : name(name) {}

            import(lex::identifier &&name) : name(name) {}

            virtual void visit(ast_visitor &visitor) override;
        };

        struct qualified_identifier : public ast_node {
            std::optional <lex::punctuator> initial_doublecolon;
            std::vector <lex::identifier> names;

            qualified_identifier(const std::optional <lex::punctuator> &initial_doublecolon,
                                 const std::vector <lex::identifier> &names) :
                    initial_doublecolon(initial_doublecolon), names(names) {}

            qualified_identifier(std::optional <lex::punctuator> &&initial_doublecolon,
                                 std::vector <lex::identifier> &&names) :
                    initial_doublecolon(initial_doublecolon), names(names) {}

            virtual void visit(ast_visitor &visitor) override;

            bool has_root_prefix() const {
                return initial_doublecolon.has_value();
            }

            size_t size() const {
                return names.size();
            }

            const std::string operator[](size_t index)const {
                return names[index].content;
            }
        };

        struct type_specifier : public ast_node {

        };

        struct identified_type_specifier : public type_specifier {
            qualified_identifier name;

            identified_type_specifier(const qualified_identifier &name) : name(name) {}

            identified_type_specifier(qualified_identifier &&name) : name(name) {}

            virtual void visit(ast_visitor &visitor) override;

        };

        struct keyword_type_specifier : public type_specifier {
            lex::keyword keyword;

            keyword_type_specifier(const lex::keyword & keyword) : keyword(keyword) {}
            keyword_type_specifier(lex::keyword && keyword) : keyword(keyword) {}

            virtual void visit(ast_visitor &visitor) override;
        };

        struct expression;
        struct unary_expression;
        struct binary_expression;
        struct ternary_expression;
        struct multi_expression;

        struct literal_expr;

        struct expr_list_expr;

        struct binary_operator_expr;

        struct assignment_expr;
        struct conditional_expr;
        struct logical_or_expr;
        struct logical_and_expr;
        struct inclusive_bin_or_expr;
        struct exclusive_bin_or_expr;
        struct bin_and_expr;
        struct equality_expr;
        struct relational_expr;
        struct shifting_expr;
        struct additive_expr;
        struct multiplicative_expr;
        struct pm_expr;
        struct cast_expr;
        struct unary_prefix_expr;
        struct unary_postfix_expr;

        typedef std::shared_ptr<expression> expr_ptr;

        struct expression : public ast_node {
            expression() = default;
            expression(const expression &) = default;
            expression(expression &&) = default;
        };


        struct visibility_decl;
        struct namespace_decl;
        struct variable_decl;
        struct function_decl;


        /**
         * Declaration (member of a namespace).
         */
        struct declaration : public ast_node {
        };

        typedef anyof::any_of <declaration, visibility_decl, namespace_decl, function_decl, variable_decl> any_declaration;
        typedef anyof::any_of_opt <declaration, visibility_decl, namespace_decl, function_decl, variable_decl> any_declaration_opt;
        typedef std::shared_ptr<declaration> decl_ptr;

        struct statement : public ast_node {
        };

        struct block_statement;
        struct return_statement;
        typedef variable_decl declaration_statement;
        struct expression_statement;

        typedef anyof::any_of <statement, block_statement, return_statement, declaration_statement, expression_statement> any_statement;
        typedef anyof::any_of_opt <statement, block_statement, return_statement, declaration_statement, expression_statement> any_statement_opt;

        struct unary_expression : public expression {
        protected:
            expr_ptr _expr;

        public:
            const expr_ptr expr() const { return _expr; }
            expr_ptr& expr() { return _expr; }

            virtual ~unary_expression() = default;

        protected:
            unary_expression(const unary_expression& other) = default;
            unary_expression(unary_expression&& other) = default;
            unary_expression(expr_ptr expr) : _expr(expr) {}
        };

        struct binary_expression : public expression {
        protected:
            expr_ptr _lexpr;
            expr_ptr _rexpr;

        public:
            const expr_ptr& lexpr() const { return _lexpr; }
            expr_ptr& lexpr() { return _lexpr; }

            const expr_ptr& rexpr() const { return _rexpr; }
            expr_ptr& rexpr() { return _rexpr; }

        protected:
            binary_expression(const binary_expression&) = default;
            binary_expression(binary_expression&&) = default;

            binary_expression(expr_ptr lexpr, expr_ptr rexpr) : _lexpr(lexpr), _rexpr(rexpr) {}

        };

        struct ternary_expression : public expression {
        protected:
            expr_ptr _lexpr, _mexpr, _rexpr;

        public:
            const expr_ptr& lexpr() const { return _lexpr; }
            expr_ptr& lexpr() { return _lexpr; }

            const expr_ptr& mexpr() const { return _mexpr; }
            expr_ptr& mexpr() { return _mexpr; }

            const expr_ptr& rexpr() const { return _rexpr; }
            expr_ptr& rexpr() { return _rexpr; }

        protected:
            ternary_expression(const ternary_expression&) = default;
            ternary_expression(ternary_expression&&) = default;

            ternary_expression(expr_ptr lexpr, expr_ptr mexpr, expr_ptr rexpr) : _lexpr(lexpr), _mexpr(mexpr), _rexpr(rexpr) {}
       };

        struct multi_expression : public expression {
        protected:
            std::vector<expr_ptr> _exprs;

            multi_expression(const multi_expression&) = default;
            multi_expression(multi_expression&&) = default;

        public:
            virtual void visit(ast_visitor &visitor) {}

            multi_expression(const std::vector <expr_ptr> &exprs) : _exprs(exprs) {}
            multi_expression(std::vector <expr_ptr> &&exprs) : _exprs(exprs) {}

        public:
            size_t size() const { return _exprs.size(); }
            const expr_ptr expr(size_t n) const { return _exprs[n]; }
            expr_ptr expr(size_t n) { return _exprs[n]; }
            const expr_ptr operator[](size_t n) const { return _exprs[n]; }
            expr_ptr operator[](size_t n) { return _exprs[n]; }

        };

        struct literal_expr : public expression {
            lex::any_literal literal;

            literal_expr(const lex::any_literal &literal) : literal(literal) {}
            literal_expr(lex::any_literal &&literal) : literal(literal) {}

            virtual void visit(ast_visitor &visitor) override;
        };

        struct keyword_expr : public expression {
            lex::keyword keyword;

            keyword_expr(const lex::keyword &keyword) : keyword(keyword) {}
            keyword_expr(lex::keyword &&keyword) : keyword(keyword) {}

            virtual void visit(ast_visitor &visitor) override;
        };

        struct this_expr : public keyword_expr {
            this_expr(const lex::keyword &keyword) : keyword_expr(keyword) {}
            this_expr(lex::keyword &&keyword) : keyword_expr(keyword) {}

            virtual void visit(ast_visitor &visitor) override;
        };

        struct expr_list_expr : public multi_expression {
            expr_list_expr(const std::vector<expr_ptr> &exprs) : multi_expression(exprs) {}
            expr_list_expr(std::vector<expr_ptr> &&exprs) : multi_expression(exprs) {}

            virtual void visit(ast_visitor &visitor) override;
        };

        struct binary_operator_expr : public binary_expression {
            lex::operator_ op;

            binary_operator_expr(const lex::operator_ &op, expr_ptr lexpr, expr_ptr rexpr)
                    : binary_expression(lexpr, rexpr), op(op) {}

            virtual void visit(ast_visitor &visitor) override;
        };

        struct conditional_expr : public ternary_expression {
            lex::operator_ question_mark;
            lex::operator_ colon;

            conditional_expr(const lex::operator_ &question_mark, const lex::operator_ &colon, expr_ptr lexpr, expr_ptr  mexpr, expr_ptr rexpr)
                    : ternary_expression(lexpr, mexpr, rexpr), question_mark(question_mark), colon(colon) {}

            virtual void visit(ast_visitor &visitor) override;
        };


        struct cast_expr : public unary_expression {
            std::shared_ptr<ast::type_specifier> type;

            cast_expr(const cast_expr&) = default;
            cast_expr(cast_expr&&) = default;

            virtual ~cast_expr() = default;

            cast_expr(const std::shared_ptr<ast::type_specifier>& type, const expr_ptr &expr) : unary_expression(expr), type(type) {}
            cast_expr(std::shared_ptr<ast::type_specifier>&& type, expr_ptr &&expr) : unary_expression(expr), type(type) {}

            virtual void visit(ast_visitor &visitor) override;
        };

        struct unary_prefix_expr : public unary_expression {
            lex::operator_ op;

            unary_prefix_expr(const unary_prefix_expr&) = default;
            unary_prefix_expr(unary_prefix_expr&&) = default;

            unary_prefix_expr(const lex::operator_& op, const expr_ptr &expr) : unary_expression(expr), op(op) {}
            unary_prefix_expr(lex::operator_&& op, expr_ptr &&expr) : unary_expression(expr), op(op) {}

            virtual void visit(ast_visitor &visitor) override;
        };

        struct unary_postfix_expr : public unary_expression {
            lex::operator_ op;
            unary_postfix_expr(const lex::operator_& op, const expr_ptr &expr) : unary_expression(expr), op(op) {}
            unary_postfix_expr(lex::operator_&& op, expr_ptr &&expr) : unary_expression(expr), op(op) {}

            virtual void visit(ast_visitor &visitor) override;
        };

        struct bracket_postifx_expr : public binary_expression {
            bracket_postifx_expr(const expr_ptr &lexpr, const expr_ptr &rexpr) : binary_expression(lexpr, rexpr) {}
            bracket_postifx_expr(expr_ptr &&rexpr, expr_ptr &&lexpr) : binary_expression(lexpr, rexpr) {}

            virtual void visit(ast_visitor &visitor) override;
        };

        struct parenthesis_postifx_expr : public binary_expression {
            parenthesis_postifx_expr(const expr_ptr &lexpr, const expr_ptr &rexpr) : binary_expression(lexpr, rexpr) {}
            parenthesis_postifx_expr(expr_ptr &&rexpr, expr_ptr &&lexpr) : binary_expression(lexpr, rexpr) {}

            virtual void visit(ast_visitor &visitor) override;
        };

        struct identifier_expr : public expression {

            ast::qualified_identifier qident;

            identifier_expr(const ast::qualified_identifier& qident) :
                    qident(qident) {}

            virtual void visit(ast_visitor &visitor) override;
        };

        //
        // Statements
        //

        struct expression_statement : public statement {
            ast::expr_ptr expr;

            expression_statement(ast::expr_ptr expr) : expr(expr) {}

            virtual void visit(ast_visitor &visitor) override;
        };

        struct block_statement : public statement {
            std::vector <any_statement> statements;

            block_statement(const std::vector <any_statement> &statements) : statements(statements) {}

            block_statement(std::vector <any_statement> &&statements) : statements(statements) {}

            virtual void visit(ast_visitor &visitor) override;
        };

        struct return_statement : public statement {
            ast::expr_ptr expr;

            return_statement(ast::expr_ptr expr) : expr(expr) {}

            virtual void visit(ast_visitor &visitor) override;
        };

        //
        // Declarations
        //

        struct visibility_decl : public declaration {
            lex::keyword scope;

            visibility_decl(const visibility_decl&) = default;
            visibility_decl(visibility_decl&&) = default;

            visibility_decl(const lex::keyword &scope) : scope(scope) {}
            visibility_decl(lex::keyword &&scope) : scope(scope) {}

            virtual void visit(ast_visitor &visitor) override;
        };

        struct namespace_decl : public declaration {
            std::optional <lex::identifier> name;
            std::vector <decl_ptr> declarations;

            namespace_decl(const std::optional <lex::identifier> &name,
                           const std::vector <decl_ptr> &declarations) :
                    name(name), declarations(declarations) {}

            namespace_decl(std::optional <lex::identifier> &&name, std::vector <decl_ptr> &&declarations) :
                    name(name), declarations(declarations) {}

            virtual void visit(ast_visitor &visitor) override;
        };

        struct variable_decl : public declaration, public statement {
            std::vector <lex::keyword> specifiers;
            lex::identifier name;
            std::shared_ptr<ast::type_specifier> type;
            expr_ptr init;

            variable_decl(const std::vector <lex::keyword> &specifiers, const lex::identifier &name,
                          const std::shared_ptr<ast::type_specifier> &type, expr_ptr init = nullptr) :
                    specifiers(specifiers), name(name), type(type), init(init) {}

            variable_decl(std::vector <lex::keyword> &&specifiers, lex::identifier &&name, std::shared_ptr<ast::type_specifier> &&type, expr_ptr init) :
                    specifiers(specifiers), name(name), type(type), init(init) {}

            virtual void visit(ast_visitor &visitor) override;
        };

        struct parameter_spec : public ast_node {
            std::vector <lex::keyword> specifiers;
            std::optional <lex::identifier> name;
            std::shared_ptr<ast::type_specifier> type;

            parameter_spec(const std::vector <lex::keyword> &specifiers, const std::optional <lex::identifier> &name,
                           const std::shared_ptr<ast::type_specifier> &type) :
                    specifiers(specifiers), name(name), type(type) {}

            parameter_spec(std::vector <lex::keyword> &&specifiers, std::optional <lex::identifier> &&name,
                           std::shared_ptr<ast::type_specifier> &&type) :
                    specifiers(specifiers), name(name), type(type) {}

            virtual void visit(ast_visitor &visitor) override;
        };

        struct function_decl : public declaration {
            std::vector <lex::keyword> specifiers;
            lex::identifier name;
            std::shared_ptr<ast::type_specifier> type;
            std::vector <parameter_spec> params;
            std::optional <block_statement> content;

            function_decl(const std::vector <lex::keyword> &specifiers, const lex::identifier &name,
                          const std::shared_ptr<ast::type_specifier> &type, const std::vector <parameter_spec> &params,
                          const std::optional <block_statement> &content) :
                    specifiers(specifiers), name(name), type(type), params(params), content(content) {}

            function_decl(std::vector <lex::keyword> &&specifiers, lex::identifier &&name,
                          std::shared_ptr<ast::type_specifier> &&type, std::vector <parameter_spec> &&params,
                          std::optional <block_statement> &&content) :
                    specifiers(specifiers), name(name), type(type), params(params), content(content) {}

            virtual void visit(ast_visitor &visitor) override;
        };




        /**
         * Unit.
         */
        struct unit : public ast_node {
            /** Unit module name */
            std::optional <ast::qualified_identifier> module_name;

            /** Import declarations */
            std::vector <import> imports;

            // TODO remove it:
            std::vector <decl_ptr> declarations;

            virtual void visit(ast_visitor &visitor) override;
        };

    } // namespace ast

    class ast_visitor {
    public:
        virtual void visit_unit(ast::unit &) = 0;
        virtual void visit_import(ast::import &) = 0;

        virtual void visit_identified_type_specifier(ast::identified_type_specifier &) = 0;
        virtual void visit_keyword_type_specifier(ast::keyword_type_specifier &) = 0;

        virtual void visit_parameter_specifier(ast::parameter_spec &) = 0;

        virtual void visit_qualified_identifier(ast::qualified_identifier &) = 0;

        virtual void visit_visibility_decl(ast::visibility_decl &) = 0;
        virtual void visit_namespace_decl(ast::namespace_decl &) = 0;
        virtual void visit_variable_decl(ast::variable_decl &) = 0;
        virtual void visit_function_decl(ast::function_decl &) = 0;

        virtual void visit_block_statement(ast::block_statement &) = 0;
        virtual void visit_return_statement(ast::return_statement &) = 0;
        virtual void visit_expression_statement(ast::expression_statement &) = 0;

        virtual void visit_literal_expr(ast::literal_expr &) = 0;
        virtual void visit_keyword_expr(ast::keyword_expr &) = 0;
        virtual void visit_this_expr(ast::keyword_expr &) = 0;
        virtual void visit_expr_list_expr(ast::expr_list_expr &) = 0;
        virtual void visit_conditional_expr(ast::conditional_expr &) = 0;
        virtual void visit_binary_operator_expr(ast::binary_operator_expr &) = 0;

        virtual void visit_cast_expr(ast::cast_expr &) = 0;
        virtual void visit_unary_prefix_expr(ast::unary_prefix_expr &) = 0;
        virtual void visit_unary_postfix_expr(ast::unary_postfix_expr &) = 0;
        virtual void visit_bracket_postifx_expr(ast::bracket_postifx_expr &) = 0;
        virtual void visit_parenthesis_postifx_expr(ast::parenthesis_postifx_expr &) = 0;
        virtual void visit_identifier_expr(ast::identifier_expr &) = 0;

        virtual void visit_comma_expr(ast::expr_list_expr &) = 0;

    };

    class default_ast_visitor : public ast_visitor {
    public:
        void visit_unit(ast::unit &) override;
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
    };

} // namespace k::parse
#endif //KLANG_AST_HPP
