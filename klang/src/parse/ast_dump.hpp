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
                unit.module_name->visit(*this);
            } else {
                _stm << "<<no-module-name>>" << std::endl;
            }

            auto pf = prefix_inc();
            for(auto import : unit.imports) {
                visit_import(*import);
            }
            visit_declarations(unit.declarations);
        }

        void visit_declarations(std::vector<ast::decl_ptr>& decls) {
            for(auto& decl : decls) {
                decl->visit(*this);
            }
        }

        void visit_module_name(ast::module_name& name) override {
            if(name.qname) {
                _stm << "module ";
                name.qname->visit(*this);
            } else {
                _stm << "module <<unamed-module>>";
            }
            _stm << std::endl;
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

        void visit_keyword_type_specifier(ast::keyword_type_specifier &identifier) override  {
            _stm  <<  "<<kwtype:" << identifier.keyword.content << ">>";
        }

        void visit_array_type_specifier(ast::array_type_specifier &arr) override {
            arr.subtype->visit(*this);

            if(arr.lex_int)
                _stm << "[" << arr.lex_int->content << "]";
            else
                _stm << "[<<undef>>]";
        }

        void visit_pointer_type_specifier(ast::pointer_type_specifier &ptr) override {
            ptr.subtype->visit(*this);
            _stm << ptr.pointer_type.content;
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

        void visit_identified_type_specifier(ast::identified_type_specifier& type) override {
            type.name.visit(*this);
        }

        void visit_parameter_specifier(ast::parameter_spec& param) override {
            visit_specifiers(param.specifiers);
            if(param.name) {
                _stm << param.name.value().content << " : ";
            }
            param.type->visit(*this);
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
            var.type->visit(*this);

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
                visit_parameter_specifier(*function.params[0]);
                for(size_t n=1; n<function.params.size(); ++n) {
                    _stm << ", ";
                    visit_parameter_specifier(*function.params[n]);
                }
            }
            _stm << ")";

            if(function.type) {
                _stm << " : ";
                function.type->visit(*this);
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

        virtual void visit_if_else_statement(ast::if_else_statement& stmt) override {
            prefix() << "if ( ";
            stmt.test_expr->visit(*this);
            _stm << " ) " << std::endl;
            {
                auto pf = prefix_inc();
                stmt.then_stmt->visit(*this);
            }
            if(stmt.else_stmt) {
                prefix() << "else" << std::endl;
                auto pf = prefix_inc();
                stmt.else_stmt->visit(*this);
            } else {
                prefix() << "<<no-else>>" << std::endl;
            }
        }

        virtual void visit_while_statement(ast::while_statement& stmt) override {
            prefix() << "while ( ";
            stmt.test_expr->visit(*this);
            _stm << " ) " << std::endl;
            auto pf = prefix_inc();
            stmt.nested_stmt->visit(*this);
        }

        virtual void visit_for_statement(ast::for_statement& stmt) override {
            prefix() << "for ( ";
            stmt.decl_expr->visit(*this);
            _stm << " , ";
            stmt.test_expr->visit(*this);
            _stm << " , ";
            stmt.step_expr->visit(*this);
            _stm << " ) " << std::endl;
            auto pf = prefix_inc();
            stmt.nested_stmt->visit(*this);
        }

        virtual void visit_expression_statement(ast::expression_statement& stmt) override {
            prefix();
            if(stmt.expr) {
                stmt.expr->visit(*this);
            }
            _stm << ";" << std::endl;
        }

        virtual void visit_comma_expr(ast::expr_list_expr& list) override {
            if(list.size()==0) {
                _stm << "<<list-expr:empty>>";
            } else {
                if(list.expr(0)) {
                    list.expr(0)->visit(*this);
                } else {
                    _stm << "<<null>>";
                }
                for(size_t n=1; n<list.size(); ++n) {
                    _stm << ", ";
                    if(list.expr(n)) {
                        list.expr(n)->visit(*this);
                    } else {
                        _stm << "<<null>>";
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
            if(expr.size()==0) {
                _stm << "<<list-expr:empty>>";
            } else {
                if(expr.expr(0)) {
                    expr.expr(0)->visit(*this);
                } else {
                    _stm << "<<null>>";
                }
                for(size_t n=1; n<expr.size(); ++n) {
                    _stm << ", ";
                    if(expr.expr(n)) {
                        expr.expr(n)->visit(*this);
                    } else {
                        _stm << "<<null>>";
                    }
                }
            }
        }

        void visit_cast_expr(ast::cast_expr& expr) override {
            _stm << "(cast:";
            if(expr.type) {
                expr.type->visit(*this);
            } else {
                _stm << "<<notype>>";
            }
            _stm << ":";
            if(expr.expr()) {
                expr.expr()->visit(*this);
            } else {
                _stm << "<<noexpr>>";
            }
            _stm << ")";
        }

        void visit_unary_prefix_expr(ast::unary_prefix_expr& expr) override {
            _stm << expr.op.content << " ";
            if(expr.expr()) {
                expr.expr()->visit(*this);
            }
        }

        void visit_unary_postfix_expr(ast::unary_postfix_expr& expr) override {

        }

        void visit_bracket_postifx_expr(ast::bracket_postifx_expr& expr) override {

        }

        void visit_parenthesis_postifx_expr(ast::parenthesis_postifx_expr& expr) override {
            expr.lexpr()->visit(*this);
            _stm << "(";
            if(expr.rexpr()) {
                expr.rexpr()->visit(*this);
            }
            _stm << ")";
        }

        void visit_identifier_expr(ast::identifier_expr& expr) override {
            _stm << "<<identifier:";
            visit_qualified_identifier(expr.qident);
            _stm << ">>";
        }
};


} // k::parse::dump


#endif //KLANG_AST_HPP
