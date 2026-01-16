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
//
// Note: Last parser log number: 0x20009
//

#include "model_builder.hpp"

#include "../common/common.hpp"
#include <random>
#include <sstream>
#include <iomanip>

namespace k::model {

    static std::string gen_random_unsigned_id() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<uint16_t> dis(0, 0xFFFF);
        std::ostringstream oss;
        oss << std::hex << std::setw(4) << std::setfill('0') << dis(gen);
        return oss.str();
    }

    void model_builder::visit(k::log::logger& logger, std::shared_ptr<k::model::context> context, k::parse::ast::unit& src, k::model::unit& unit) {
        model_builder visitor(logger, context, unit);
        visitor.visit_unit(src);
    }

    void model_builder::visit_unit(parse::ast::unit &unit) {
        // Push root ns context
        stack<ns_context> push(_contexts, _unit.get_root_namespace());

        super::visit_unit(unit);

        if (_unit.get_unit_name().empty()) {
            // If no module name defined, set a default one
            _unit.set_unit_name(k::name("anon"+gen_random_unsigned_id()));
        }
    }

    void model_builder::visit_module_name(parse::ast::module_name &name) {
        if(name.qname) {
            _unit.set_unit_name(name.qname->to_name());
        }
    }

    void model_builder::visit_import(parse::ast::import &) {
        // TODO Imports not supported yet
    }

    void model_builder::visit_identified_type_specifier(parse::ast::identified_type_specifier &) {

    }

    void model_builder::visit_parameter_specifier(parse::ast::parameter_spec &) {

    }

    void model_builder::visit_qualified_identifier(parse::ast::qualified_identifier &) {

    }

    void model_builder::visit_keyword_type_specifier(parse::ast::keyword_type_specifier &) {

    }

    void model_builder::visit_visibility_decl(parse::ast::visibility_decl &visibility) {
        auto scope = current_context<visibility_context>();
        if(!scope) {
            throw_error(0x0001, visibility.scope, "Current context doesnt support default visibility");
        }

        switch(visibility.scope.type) {
            case lex::keyword::PUBLIC:
                scope->visibility = model::PUBLIC;
                break;
            case lex::keyword::PROTECTED:
                scope->visibility = model::PROTECTED;
                break;
            case lex::keyword::PRIVATE:
                scope->visibility = model::PRIVATE;
                break;
            default:
                throw_error(0x0002, visibility.scope, "Unrecognized visibility context keyword {}", {visibility.scope.content});
                break;
        }
    }

    void model_builder::visit_namespace_decl(parse::ast::namespace_decl &ns) {
        auto parent_ns = current_context_content<model::ns>();
        std::shared_ptr<k::model::ns> namesp = parent_ns->get_child_namespace(ns.name->content);

        // Push namespace context
        stack<ns_context> push(_contexts, namesp);

        super::visit_namespace_decl(ns);
    }

    void model_builder::visit_struct_decl(parse::ast::struct_decl& st) {
        std::shared_ptr<model::structure_holder> parent_scope = current_context_content<model::structure_holder>();
        if(!parent_scope){
            throw_error(0x0009, st.st, "Current context doesnt support structure declaration");
        }

        std::shared_ptr<model::structure> struc = parent_scope->define_structure(st.name.content);


        // Push function context
        stack<struct_context> push(_contexts, struc);

        default_ast_visitor::visit_struct_decl(st);
    }

    void model_builder::visit_variable_decl(parse::ast::variable_decl &decl) {
        // TODO refactor variable declaration to make it stack-less
        std::shared_ptr<model::variable_holder> parent_scope = current_context_content<model::variable_holder>();
        if(!parent_scope){
            throw_error(0x0004, decl.name, "Current context doesnt support variable declaration");
        }

        std::shared_ptr<model::variable_definition> var = parent_scope->append_variable(decl.name.content);
        var->set_type(_context->from_type_specifier(*decl.type));

        if(decl.init) {
            _expr.reset();
            decl.init->visit(*this);
            var->set_init_expr(_expr);
        }
    }

    void model_builder::visit_function_decl(parse::ast::function_decl & func) {
        auto parent_scope = current_context_content<function_holder>();
        if(!parent_scope) {
            throw_error(0x0005, func.name, "Current context doesnt support function declaration");
        }
        std::shared_ptr<model::function> function = parent_scope->define_function(func.name.content);

        // Push function context
        stack<func_context> push(_contexts, function);

        // TODO add function specs

        if(func.type) {
            function->set_return_type(_context->from_type_specifier(*func.type));
        }

        for(auto param : func.params) {
            std::shared_ptr<model::parameter> parameter = function->append_parameter(param->name->content, _context->from_type_specifier(*(param->type)));
            // TODO add param specs
        }

        if(func.content) {
            visit_block_statement(*func.content);
            if(auto block = std::dynamic_pointer_cast<model::block>(_stmt)) {
                function->set_block(block);
            }
        }
    }

    void model_builder::visit_block_statement(parse::ast::block_statement &block_stmt) {
        auto parent_scope = current_context_content<element>(); // Could be a function or a block
        if(!parent_scope) {
            throw_error(0x0006, block_stmt.open_brace, "Current context doesnt support block statement");
        }

        std::shared_ptr<model::block> block = std::make_shared<model::block>(parent_scope);

        // Push function context
        stack<block_context> push(_contexts, block);

        // Visit all children statements
        for(auto& stmt : block_stmt.statements) {
            _stmt.reset();
            stmt->visit(*this);
            if(_stmt) {
                block->append_statement(_stmt);
                _stmt.reset();
            }
        }

        _stmt = block;
    }

    void model_builder::visit_return_statement(parse::ast::return_statement &stmt) {
        auto parent_scope = current_context_content<statement>();
        if(!parent_scope) {
            throw_error(0x0007, stmt.ret, "Current context doesnt support return statement");
        }

        std::shared_ptr<model::return_statement> ret_stmt = std::make_shared<model::return_statement>(parent_scope, stmt.shared_as<parse::ast::return_statement>());

        // Push function context
        stack<return_context> push(_contexts, ret_stmt);

        _expr.reset();
        if(stmt.expr) {
            stmt.expr->visit(*this);
        }
        if(_expr) {
            ret_stmt->set_expression(_expr);
            _expr.reset();
        }

        _stmt = ret_stmt;
    }

    void model_builder::visit_if_else_statement(parse::ast::if_else_statement &stmt) {
        auto parent_scope = current_context_content<statement>();
        if(!parent_scope) {
            throw_error(0x0008, stmt.if_kw, "Current context doesnt support if else statement");
        }

        std::shared_ptr<model::if_else_statement> if_else_stmt = std::make_shared<model::if_else_statement>(parent_scope, stmt.shared_as<parse::ast::if_else_statement>());

        // Push function context
        stack<if_else_context> push(_contexts, if_else_stmt);

        // Test expression
        _expr.reset();
        if(stmt.test_expr) {
            stmt.test_expr->visit(*this);
        } /* else process absence in next if */
        if(_expr) {
            if_else_stmt->set_test_expr(_expr);
            _expr.reset();
        } else {
            // Test expression is mandatory
            // TODO throw an exception
        }

        // Then statement
        _stmt.reset();
        if(stmt.then_stmt) {
            stmt.then_stmt->visit(*this);
        } /* else process absence in next if */
        if(_stmt) {
            if_else_stmt->set_then_stmt(_stmt);
            _stmt.reset();
        } else {
            // Then statement is mandatory
            // TODO throw an exception
        }

        // Else statement
        _stmt.reset();
        if(stmt.else_stmt) {
            stmt.else_stmt->visit(*this);
            if(_stmt) {
                if_else_stmt->set_else_stmt(_stmt);
                _stmt.reset();
            } else {
                // Error in processing else statement
                // TODO throw an exception
            }
        } /* else else statement is not mandatory */

        _stmt = if_else_stmt;
    }

    void model_builder::visit_while_statement(parse::ast::while_statement &stmt) {
        auto parent_scope = current_context_content<statement>();
        if(!parent_scope) {
            throw_error(0x0009, stmt.while_kw, "Current context doesnt support while statement");
        }

        auto while_stmt = std::make_shared<model::while_statement>(parent_scope, stmt.shared_as<parse::ast::while_statement>());

        // Push function context
        stack<while_context> push(_contexts, while_stmt);

        // Test expression
        _expr.reset();
        if(stmt.test_expr) {
            stmt.test_expr->visit(*this);
        } /* else process absence in next if */
        if(_expr) {
            while_stmt->set_test_expr(_expr);
            _expr.reset();
        } else {
            // Test expression is mandatory
            // TODO throw an exception
        }

        // Nested statement
        _stmt.reset();
        if(stmt.nested_stmt) {
            stmt.nested_stmt->visit(*this);
        } /* else process absence in next if */
        if(_stmt) {
            while_stmt->set_nested_stmt(_stmt);
            _stmt.reset();
        } else {
            // Nested statement is mandatory
            // TODO throw an exception
        }

        _stmt = while_stmt;
    }

    void model_builder::visit_for_statement(parse::ast::for_statement &stmt) {
        auto parent_scope = current_context_content<statement>();
        if(!parent_scope) {
            throw_error(0x000A, stmt.for_kw, "Current context doesnt support for statement");
        }

        auto for_stmt = std::make_shared<model::for_statement>(parent_scope, stmt.shared_as<parse::ast::for_statement>());

        // Push function context
        stack<for_context> push(_contexts, for_stmt);

        // Variable decl
        _stmt.reset();
        if(stmt.decl_expr) {
            stmt.decl_expr->visit(*this);
            // Varable supposed to be already registered.
        }
        _stmt.reset();

        // Test expression
        _expr.reset();
        if(stmt.test_expr) {
            stmt.test_expr->visit(*this);
            if(_expr) {
                for_stmt->set_test_expr(_expr);
                _expr.reset();
            } else {
                // Test expression failed
                // TODO throw an exception
            }
        }
        _expr.reset();

        // Step expression
        _expr.reset();
        if(stmt.step_expr) {
            stmt.step_expr->visit(*this);
            if(_expr) {
                for_stmt->set_step_expr(_expr);
                _expr.reset();
            } else {
                // Step expression failed
                // TODO throw an exception
            }
        }
        _expr.reset();

        // Nested statement
        _stmt.reset();
        if(stmt.nested_stmt) {
            stmt.nested_stmt->visit(*this);
        } /* else process absence in next if */
        if(_stmt) {
            for_stmt->set_nested_stmt(_stmt);
            _stmt.reset();
        } else {
            // Nested statement is mandatory
            // TODO throw an exception
        }

        _stmt = for_stmt;
    }

    void model_builder::visit_expression_statement(parse::ast::expression_statement &stmt) {
        auto parent_scope = current_context_content<statement>();
        if(!parent_scope) {
            // TODO throw an exception
//            throw_error(0x000B, stmt., "Current context doesnt support expression statement");
        }

        std::shared_ptr<model::expression_statement> expr = std::make_shared<model::expression_statement>(parent_scope, stmt.shared_as<parse::ast::expression_statement>());

        // Push function context
        stack<expr_stmt_context> push(_contexts, expr);

        _expr.reset();
        if(stmt.expr) {
            stmt.expr->visit(*this);
        }
        if(_expr) {
            expr->set_expression(_expr);
            _expr.reset();
        }

        _stmt = expr;
    }

    void model_builder::visit_literal_expr(parse::ast::literal_expr &expr) {
        _expr = model::value_expression::from_literal(expr.literal);
    }

    void model_builder::visit_keyword_expr(parse::ast::keyword_expr &expr) {
        // Note: Must not happen
    }

    void model_builder::visit_this_expr(parse::ast::keyword_expr &expr) {
        _expr = model::symbol_expression::from_identifier(name("this"));
    }

    void model_builder::visit_expr_list_expr(parse::ast::expr_list_expr &) {

    }

    void model_builder::visit_conditional_expr(parse::ast::conditional_expr &) {

    }

    void model_builder::visit_binary_operator_expr(parse::ast::binary_operator_expr & expr) {

        expr.lexpr()->visit(*this);
        std::shared_ptr<model::expression> lexpr = _expr;
        expr.rexpr()->visit(*this);
        std::shared_ptr<model::expression> rexpr = _expr;

        switch(expr.op.type) {
            case lex::operator_::PLUS:
                _expr = model::addition_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::MINUS:
                _expr = model::substraction_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::STAR:
                _expr = model::multiplication_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::SLASH:
                _expr = model::division_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::PERCENT:
                _expr = model::modulo_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::AMPERSAND:
                _expr = model::bitwise_and_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::PIPE:
                _expr = model::bitwise_or_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::CARET:
                _expr = model::bitwise_xor_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::DOUBLE_CHEVRON_OPEN:
                _expr = model::left_shift_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::DOUBLE_CHEVRON_CLOSE:
                _expr = model::right_shift_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::EQUAL:
                _expr = model::simple_assignation_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::PLUS_EQUAL:
                _expr = model::additition_assignation_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::MINUS_EQUAL:
                _expr = model::substraction_assignation_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::STAR_EQUAL:
                _expr = model::multiplication_assignation_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::SLASH_EQUAL:
                _expr = model::division_assignation_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::PERCENT_EQUAL:
                _expr = model::modulo_assignation_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::AMPERSAND_EQUAL:
                _expr = model::bitwise_and_assignation_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::PIPE_EQUAL:
                _expr = model::bitwise_or_assignation_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::CARET_EQUAL:
                _expr = model::bitwise_xor_assignation_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::DOUBLE_CHEVRON_OPEN_EQUAL:
                _expr = model::left_shift_assignation_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::DOUBLE_CHEVRON_CLOSE_EQUAL:
                _expr = model::right_shift_assignation_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::DOUBLE_AMPERSAND:
                _expr = model::logical_and_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::DOUBLE_PIPE:
                _expr = model::logical_or_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::DOUBLE_EQUAL:
                _expr = model::equal_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::EXCLAMATION_MARK_EQUAL:
                _expr = model::different_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::CHEVRON_OPEN:
                _expr = model::lesser_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::CHEVRON_CLOSE:
                _expr = model::greater_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::CHEVRON_OPEN_EQUAL:
                _expr = model::lesser_equal_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::CHEVRON_CLOSE_EQUAL:
                _expr = model::greater_equal_expression::make_shared(lexpr, rexpr);
                break;
            default: // TODO other operations
                throw_error(0x0007, expr.op, "Binary operator '{}' not supported", {expr.op.content});
                break;
        }
    }

    void model_builder::visit_cast_expr(parse::ast::cast_expr& expr) {
        _expr = nullptr;
        expr.expr()->visit(*this);
        _expr = model::cast_expression::make_shared(_expr, _context->from_type_specifier(*expr.type));
    }

    void model_builder::visit_unary_prefix_expr(parse::ast::unary_prefix_expr& expr) {
        _expr = nullptr;
        expr.expr()->visit(*this);
        auto sub = _expr;

        std::shared_ptr<model::unary_expression> unary;
        switch(expr.op.type) {
            case lex::operator_::PLUS:
                unary = model::unary_plus_expression::make_shared(sub);
                break;
            case lex::operator_::MINUS:
                unary = model::unary_minus_expression::make_shared(sub);
                break;
            case lex::operator_::TILDE:
                unary = model::bitwise_not_expression::make_shared(sub);
                break;
            case lex::operator_::EXCLAMATION_MARK:
                unary = model::logical_not_expression::make_shared(sub);
                break;
            case lex::operator_::AMPERSAND:
                unary = model::address_of_expression::make_shared(sub);
                break;
            case lex::operator_::STAR:
                unary = model::dereference_expression::make_shared(sub);
                break;
            default:
                throw_error(0x0008, expr.op, "Unary operator '{}' not supported", {expr.op.content});
                break;
        }
        unary->set_ast_unary_expr(expr.shared_as<parse::ast::unary_prefix_expr>());
        _expr = unary;
    }

    void model_builder::visit_unary_postfix_expr(parse::ast::unary_postfix_expr &) {

    }

    void model_builder::visit_bracket_postifx_expr(parse::ast::bracket_postifx_expr &expr) {
        expr.lexpr()->visit(*this);
        std::shared_ptr<model::expression> lexpr = _expr;
        expr.rexpr()->visit(*this);
        std::shared_ptr<model::expression> rexpr = _expr;
        _expr = model::subscript_expression::make_shared(lexpr, rexpr);
    }

    void model_builder::visit_parenthesis_postifx_expr(parse::ast::parenthesis_postifx_expr &expr) {
        expr.lexpr()->visit(*this);
        std::shared_ptr<model::expression> callee = _expr;

        _expr = nullptr;
        std::vector<std::shared_ptr<model::expression>> args;
        if(auto list = std::dynamic_pointer_cast<parse::ast::expr_list_expr>(expr.rexpr())) {
            for(auto arg : list->exprs()) {
                arg->visit(*this);
                args.push_back(_expr);
                _expr = nullptr;
            }
        } else if(expr.rexpr()) {
            expr.rexpr()->visit(*this);
            args.push_back(_expr);
        }

        _expr = model::function_invocation_expression::make_shared(callee, args);
    }

    void model_builder::visit_member_access_postfix_expr(parse::ast::member_access_postfix_expr &expr) {
        expr.expr()->visit(*this);
        std::shared_ptr<model::expression> callee = _expr;

        _expr = nullptr;
        expr.ident_expr->visit(*this);
        std::shared_ptr<model::symbol_expression> member = std::dynamic_pointer_cast<symbol_expression>(_expr);
        if(!member) {
            // TODO
            // throw_error(0x2000, expr.ident_expr->qident.names.front(), "Member access requires an identifier");
        }

        switch (expr.op.type) {
            case lex::operator_::DOT:
                _expr = model::member_of_object_expression::make_shared(callee, member);
                break;
            case lex::operator_::ARROW:
                _expr = model::member_of_pointer_expression::make_shared(callee, member);
                break;
            default:
                // TODO
                // throw_error(0x2001, _expr.op, "Member access requires '.' or '::' operator");
                _expr = nullptr;
                break;
        }
    }

    void model_builder::visit_identifier_expr(parse::ast::identifier_expr &expr) {
        bool has_prefix = expr.qident.initial_doublecolon.has_value();
        std::vector<std::string> idents;
        for(auto ident : expr.qident.names){
            idents.push_back(ident.content);
        }
        _expr = model::symbol_expression::from_identifier(name(has_prefix, std::move(idents)));
    }

    void model_builder::visit_comma_expr(parse::ast::expr_list_expr &) {

    }


} // k::parse