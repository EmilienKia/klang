//
// Created by Emilien Kia <emilien.kia+dev@gmail.com>
//

#ifndef KLANG_AST_DUMP_HPP
#define KLANG_AST_DUMP_HPP

#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "ast.hpp"


namespace k::parse::dump {

template<typename OSTM>
class ast_dump_visitor : public k::parse::ast_visitor {
        OSTM& _stm;
        size_t off = 0;
    public:

        static void dump(OSTM& stm, k::parse::ast::ast_node& node) {
            k::parse::dump::ast_dump_visitor visit(stm);
            visit.visit_unit(node);
        }

        ast_dump_visitor(OSTM& stm) : _stm(stm) {}

    protected:
        void inc() {
            off++;
        }

        void dec() {
            off--;
        }

        OSTM& prefix() {
            for(size_t n=0; n<off; ++n) {
                _stm << '\t';
            }
            return _stm;
        }

        class prefix_inc;

        friend class prefix_inc;

        class prefix_inc_holder {
            ast_dump_visitor* _visitor = nullptr;
        protected:
            prefix_inc_holder(ast_dump_visitor* visit) : _visitor(visit) { _visitor->inc(); }
            friend class ast_dump_visitor;
        public:
            prefix_inc_holder() = delete;
            prefix_inc_holder(const prefix_inc& other) = delete;
            prefix_inc_holder(prefix_inc&& other) : _visitor(other._visitor) { other._visitor = nullptr;}
            ~prefix_inc_holder() {
                if(_visitor!=nullptr) {
                    _visitor->dec();
                    _visitor = nullptr;
                }
            }
        };

        prefix_inc_holder prefix_inc() {
            return prefix_inc_holder(this);
        }

    public:
        void visit_unit(ast::unit &unit) override {
            // Module name:
            prefix();
            if(unit.module_name) {
                visit_qualified_identifier(*unit.module_name);
            } else {
                _stm << "<<no-module-name>>";
            }
            _stm << std::endl;

            auto pf = prefix_inc();
            for(auto import : unit.imports) {
                visit_import(import);
            }
            visit_declarations(unit.declarations);
        }

        void visit_declarations(std::vector<ast::decl_ptr>& decls) {
            for(auto& decl : decls) {
                decl->visit(*this);
            }
        }

        void visit_import(ast::import& anImport) override {
            prefix() << "import " << anImport.name.content << std::endl;
        }

        void visit_qualified_identifier(ast::qualified_identifier& identifier) override {
            if(identifier.initial_doublecolon) {
                _stm << "::";
            }
            bool already = false;
            for(auto id : identifier.names) {
                if(already) {
                    _stm << "::";
                }
                _stm << id.content;
                already = true;
            }
        }

        void visit_visibility_decl(ast::visibility_decl& decl) override {
            prefix() << "visibility " << decl.scope.content << std::endl;
        }

        void visit_namespace_decl(ast::namespace_decl& decl) override {
            prefix() << "namespace " << decl.name.value().content << std::endl;
            auto pf = prefix_inc();
            for(auto d : decl.declarations) {
                d->visit(*this);
            }
        }

        void visit_type_specifier(ast::type_specifier& type) override {
            type.name.visit(*this);
        }

        void visit_parameter_specifier(ast::parameter_spec& param) override {
            visit_specifiers(param.specifiers);
            if(param.name) {
                _stm << param.name.value().content << " : ";
            }
            param.type.visit(*this);
        }

        void visit_specifiers(const std::vector<lex::keyword>& specifiers) {
            bool already = false;
            for(auto spec : specifiers) {
                if(already)
                    _stm << ",";
                _stm << spec.content;
                already = true;
            }
            if(!specifiers.empty()) {
                _stm << " ";
            }
        }

        void visit_variable_decl(ast::variable_decl& var) override {
            prefix() << "variable ";
            visit_specifiers(var.specifiers);
            _stm << var.name.content << " : ";
            var.type.visit(*this);

            if(var.init) {
                _stm << " = ";
                var.init->visit(*this);
            }

            _stm << ";" << std::endl;
        }

        void visit_function_decl(ast::function_decl& function) override {
            prefix() << "function ";
            visit_specifiers(function.specifiers);
            _stm << function.name.content << "(";

            if(!function.params.empty()) {
                visit_parameter_specifier(function.params[0]);
                for(size_t n=1; n<function.params.size(); ++n) {
                    _stm << ", ";
                    visit_parameter_specifier(function.params[n]);
                }
            }
            _stm << ")";

            if(function.type) {
                _stm << " : ";
                visit_type_specifier(function.type.value());
            }

            if(function.content) {
                _stm << std::endl;
                function.content->visit(*this);
            } else {
                _stm << ";" << std::endl;
            }
        }

        virtual void visit_block_statement(ast::block_statement& block) override {
            prefix() << "{" << std::endl;
            {
                prefix_inc_holder inc = prefix_inc();
                for(auto& statement : block.statements) {
                    statement->visit(*this);
                }
            }
            prefix() << "}" << std::endl;
        }

        virtual void visit_return_statement(ast::return_statement& ret) override {
            prefix() << "return ";
            if(ret.expr) {
                ret.expr->visit(*this);
            }
            _stm << ";" << std::endl;
        }

        virtual void visit_expression_statement(ast::expression_statement& stmt) override {
            prefix();
            if(stmt.expr) {
                stmt.expr->visit(*this);
            }
            _stm << ";" << std::endl;
        }

        virtual void visit_comma_expr(ast::expr_list_expr& list) override {
            if(list.size() >0 ) {
                auto first = list.expr(0);
                if(first) {
                    first->visit(*this);
                }
                for(size_t i = 1; i<list.size(); ++i) {
                    _stm << ", ";
                    auto next = list.expr(i);
                    if(next) {
                        first->visit(*this);
                    }
                }
            }
        }

        virtual void visit_literal_expr(ast::literal_expr& lit) override {
            _stm << "<<literal:" << lit.literal->content << ">>";
        }

        void visit_binary_operator_expr(ast::binary_operator_expr& expr) override {
            if(expr.lexpr()) {
                expr.lexpr()->visit(*this);
            } else {
                _stm << "<<null>>";
            }
            _stm << " " << expr.op.content << " ";
            if(expr.rexpr()) {
                expr.rexpr()->visit(*this);
            } else {
                _stm << "<<null>>";
            }
        }

        void visit_conditional_expr(ast::conditional_expr& expr) override {
            // TODO
        }

        void visit_keyword_expr(ast::keyword_expr& expr) override {
            _stm << "<<keyword:" <<expr.keyword.content << ">>";
        }

        void visit_this_expr(ast::keyword_expr &) override {
            _stm << "<<kw:this>>";
        }

        void visit_expr_list_expr(ast::expr_list_expr& expr) override {

        }

        void visit_cast_expr(ast::cast_expr& expr) override {

        }

        void visit_unary_prefix_expr(ast::unary_prefix_expr& expr) override {

        }

        void visit_unary_postfix_expr(ast::unary_postfix_expr& expr) override {

        }

        void visit_bracket_postifx_expr(ast::bracket_postifx_expr& expr) override {

        }

        void visit_parenthesis_postifx_expr(ast::parenthesis_postifx_expr& expr) override {

        }

        void visit_identifier_expr(ast::identifier_expr& expr) override {
            _stm << "<<identifier:";
            visit_qualified_identifier(expr.qident);
            _stm << ">>";
        }
};


} // k::parse::dump


#endif //KLANG_AST_HPP
