//
// Created by Emilien Kia <emilien.kia+dev@gmail.com>.
//
// Note: Last parser log number: 0x20008
//

#include "ast_unit_visitor.hpp"

#include "common.hpp"

namespace k::parse {

    void ast_unit_visitor::visit(k::log::logger& logger, k::parse::ast::unit& src, k::unit::unit& unit) {
        ast_unit_visitor visitor(logger, unit);
        visitor.visit_unit(src);
    }

    void ast_unit_visitor::visit_unit(ast::unit &unit) {
        // Push root ns context
        stack<ns_context> push(_contexts, _unit.get_root_namespace());

        if(unit.module_name) {
            _unit.set_unit_name(unit.module_name->qname->to_name());
        }

        super::visit_unit(unit);
    }

    void ast_unit_visitor::visit_module_name(ast::module_name &) {
        // Not used, proceed at unit level.
    }

    void ast_unit_visitor::visit_import(ast::import &) {
    }

    void ast_unit_visitor::visit_identified_type_specifier(ast::identified_type_specifier &) {

    }

    void ast_unit_visitor::visit_parameter_specifier(ast::parameter_spec &) {

    }

    void ast_unit_visitor::visit_qualified_identifier(ast::qualified_identifier &) {

    }

    void ast_unit_visitor::visit_keyword_type_specifier(ast::keyword_type_specifier &) {

    }

    void ast_unit_visitor::visit_visibility_decl(ast::visibility_decl &visibility) {
        auto scope = std::dynamic_pointer_cast<ns_context>(_contexts.back());
        if(!scope) {
            throw_error(0x0001, visibility.scope, "Current context doesnt support default visibility");
        }

        switch(visibility.scope.type) {
            case lex::keyword::PUBLIC:
                scope->visibility = unit::PUBLIC;
                break;
            case lex::keyword::PROTECTED:
                scope->visibility = unit::PROTECTED;
                break;
            case lex::keyword::PRIVATE:
                scope->visibility = unit::PRIVATE;
                break;
            default:
                throw_error(0x0002, visibility.scope, "Unrecognized visibility context keyword {}", {visibility.scope.content});
                break;
        }
    }

    void ast_unit_visitor::visit_namespace_decl(ast::namespace_decl &ns) {
        auto parent_scope = std::dynamic_pointer_cast<ns_context>(_contexts.back());
        if(!parent_scope) {
            throw_error(0x0003, ns.ns, "Current context is not a namespace namespace");
        }

        std::shared_ptr<k::unit::ns> namesp = parent_scope->content->get_child_namespace(ns.name->content);

        // Push namespace context
        stack<ns_context> push(_contexts, namesp);

        super::visit_namespace_decl(ns);
    }

    void ast_unit_visitor::visit_variable_decl(ast::variable_decl &decl) {
        // TODO refactor variable declaration to make it stack-less
        std::shared_ptr<context> parent_context = _contexts.back();
        std::shared_ptr<unit::variable_holder> parent_scope;

        // TODO Might be unique unit::variable_holder test
        if(auto parent = std::dynamic_pointer_cast<generic_context<unit::ns>>(parent_context)) {
            parent_scope = std::dynamic_pointer_cast<unit::variable_holder>(parent->content);
        } else if(auto parent = std::dynamic_pointer_cast<generic_context<unit::block>>(parent_context)) {
            parent_scope = std::dynamic_pointer_cast<unit::variable_holder>(parent->content);
        } else if(auto parent = std::dynamic_pointer_cast<generic_context<unit::for_statement>>(parent_context)) {
            parent_scope = std::dynamic_pointer_cast<unit::variable_holder>(parent->content);
        } else {
            throw_error(0x0004, decl.name, "Current context doesnt support variable declaration");
        }

        if(!parent_scope) {
            throw_error(0x0005, decl.name, "Current context doesnt support variable declaration");
        }

        std::shared_ptr<unit::variable_definition> var = parent_scope->append_variable(decl.name.content);
        var->set_type(unit::unresolved_type::from_type_specifier(*decl.type));

        if(decl.init) {
            _expr.reset();
            decl.init->visit(*this);
            var->set_init_expr(_expr);
        }
    }

    void ast_unit_visitor::visit_function_decl(ast::function_decl & func) {
        auto parent_scope = std::dynamic_pointer_cast<ns_context>(_contexts.back());
        if(!parent_scope) {
            throw_error(0x0006, func.name, "Current context doesnt support functions");
        }

        std::shared_ptr<unit::function> function = parent_scope->content->define_function(func.name.content);

        // Push function context
        stack<func_context> push(_contexts, function);

        // TODO add function specs

        if(func.type) {
            function->return_type(unit::unresolved_type::from_type_specifier(*func.type));
        }

        std::shared_ptr<unit::block> block = function->get_block();

        for(auto param : func.params) {
            std::shared_ptr<unit::parameter> parameter = function->append_parameter(param->name->content, unit::unresolved_type::from_type_specifier(*(param->type)));
            // TODO add param specs
        }

        if(func.content) {
            visit_block_statement(*func.content);
            if(auto block = std::dynamic_pointer_cast<unit::block>(_stmt)) {
                function->set_block(block);
            }
        }
    }

    void ast_unit_visitor::visit_block_statement(ast::block_statement &block_stmt) {
        std::shared_ptr<unit::block> block = std::make_shared<unit::block>();

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

    void ast_unit_visitor::visit_return_statement(ast::return_statement &stmt) {
        std::shared_ptr<unit::return_statement> ret_stmt = std::make_shared<unit::return_statement>(stmt.shared_as<ast::return_statement>());

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

    void ast_unit_visitor::visit_if_else_statement(ast::if_else_statement &stmt) {
        std::shared_ptr<unit::if_else_statement> if_else_stmt = std::make_shared<unit::if_else_statement>(stmt.shared_as<ast::if_else_statement>());

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

    void ast_unit_visitor::visit_while_statement(ast::while_statement &stmt) {
        auto while_stmt = std::make_shared<unit::while_statement>(stmt.shared_as<ast::while_statement>());

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

    void ast_unit_visitor::visit_for_statement(ast::for_statement &stmt) {
        auto for_stmt = std::make_shared<unit::for_statement>(stmt.shared_as<ast::for_statement>());

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

    void ast_unit_visitor::visit_expression_statement(ast::expression_statement &stmt) {
        std::shared_ptr<unit::expression_statement> expr = std::make_shared<unit::expression_statement>(stmt.shared_as<ast::expression_statement>());

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

    void ast_unit_visitor::visit_literal_expr(ast::literal_expr &expr) {
        _expr = unit::value_expression::from_literal(expr.literal);
    }

    void ast_unit_visitor::visit_keyword_expr(ast::keyword_expr &expr) {
        // Note: Must not happen
    }

    void ast_unit_visitor::visit_this_expr(ast::keyword_expr &expr) {
        // TODO keyword "this"
    }

    void ast_unit_visitor::visit_expr_list_expr(ast::expr_list_expr &) {

    }

    void ast_unit_visitor::visit_conditional_expr(ast::conditional_expr &) {

    }

    void ast_unit_visitor::visit_binary_operator_expr(ast::binary_operator_expr & expr) {

        expr.lexpr()->visit(*this);
        std::shared_ptr<unit::expression> lexpr = _expr;
        expr.rexpr()->visit(*this);
        std::shared_ptr<unit::expression> rexpr = _expr;

        switch(expr.op.type) {
            case lex::operator_::PLUS:
                _expr = unit::addition_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::MINUS:
                _expr = unit::substraction_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::STAR:
                _expr = unit::multiplication_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::SLASH:
                _expr = unit::division_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::PERCENT:
                _expr = unit::modulo_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::AMPERSAND:
                _expr = unit::bitwise_and_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::PIPE:
                _expr = unit::bitwise_or_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::CARET:
                _expr = unit::bitwise_xor_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::DOUBLE_CHEVRON_OPEN:
                _expr = unit::left_shift_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::DOUBLE_CHEVRON_CLOSE:
                _expr = unit::right_shift_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::EQUAL:
                _expr = unit::simple_assignation_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::PLUS_EQUAL:
                _expr = unit::additition_assignation_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::MINUS_EQUAL:
                _expr = unit::substraction_assignation_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::STAR_EQUAL:
                _expr = unit::multiplication_assignation_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::SLASH_EQUAL:
                _expr = unit::division_assignation_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::PERCENT_EQUAL:
                _expr = unit::modulo_assignation_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::AMPERSAND_EQUAL:
                _expr = unit::bitwise_and_assignation_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::PIPE_EQUAL:
                _expr = unit::bitwise_or_assignation_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::CARET_EQUAL:
                _expr = unit::bitwise_xor_assignation_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::DOUBLE_CHEVRON_OPEN_EQUAL:
                _expr = unit::left_shift_assignation_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::DOUBLE_CHEVRON_CLOSE_EQUAL:
                _expr = unit::right_shift_assignation_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::DOUBLE_AMPERSAND:
                _expr = unit::logical_and_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::DOUBLE_PIPE:
                _expr = unit::logical_or_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::DOUBLE_EQUAL:
                _expr = unit::equal_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::EXCLAMATION_MARK_EQUAL:
                _expr = unit::different_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::CHEVRON_OPEN:
                _expr = unit::lesser_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::CHEVRON_CLOSE:
                _expr = unit::greater_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::CHEVRON_OPEN_EQUAL:
                _expr = unit::lesser_equal_expression::make_shared(lexpr, rexpr);
                break;
            case lex::operator_::CHEVRON_CLOSE_EQUAL:
                _expr = unit::greater_equal_expression::make_shared(lexpr, rexpr);
                break;
            default: // TODO other operations
                throw_error(0x0007, expr.op, "Binary operator '{}' not supported", {expr.op.content});
                break;
        }
    }

    void ast_unit_visitor::visit_cast_expr(ast::cast_expr& expr) {
        _expr = nullptr;
        expr.expr()->visit(*this);
        _expr = unit::cast_expression::make_shared(_expr, unit::unresolved_type::from_type_specifier(*expr.type));
    }

    void ast_unit_visitor::visit_unary_prefix_expr(ast::unary_prefix_expr& expr) {
        _expr = nullptr;
        expr.expr()->visit(*this);
        auto sub = _expr;

        std::shared_ptr<unit::unary_expression> unary;
        switch(expr.op.type) {
            case lex::operator_::PLUS:
                unary = unit::unary_plus_expression::make_shared(sub);
                break;
            case lex::operator_::MINUS:
                unary = unit::unary_minus_expression::make_shared(sub);
                break;
            case lex::operator_::TILDE:
                unary = unit::bitwise_not_expression::make_shared(sub);
                break;
            case lex::operator_::EXCLAMATION_MARK:
                unary = unit::logical_not_expression::make_shared(sub);
                break;
            default:
                throw_error(0x0008, expr.op, "Unary operator '{}' not supported", {expr.op.content});
                break;
        }
        unary->set_ast_unary_expr(expr.shared_as<ast::unary_prefix_expr>());
        _expr = unary;
    }

    void ast_unit_visitor::visit_unary_postfix_expr(ast::unary_postfix_expr &) {

    }

    void ast_unit_visitor::visit_bracket_postifx_expr(ast::bracket_postifx_expr &) {

    }

    void ast_unit_visitor::visit_parenthesis_postifx_expr(ast::parenthesis_postifx_expr &expr) {
        expr.lexpr()->visit(*this);
        std::shared_ptr<unit::expression> callee = _expr;

        _expr = nullptr;
        std::vector<std::shared_ptr<unit::expression>> args;
        if(auto list = std::dynamic_pointer_cast<ast::expr_list_expr>(expr.rexpr())) {
            for(auto arg : list->exprs()) {
                arg->visit(*this);
                args.push_back(_expr);
                _expr = nullptr;
            }
        } else if(expr.rexpr()) {
            expr.rexpr()->visit(*this);
            args.push_back(_expr);
        }

        _expr = unit::function_invocation_expression::make_shared(callee, args);
    }

    void ast_unit_visitor::visit_identifier_expr(ast::identifier_expr &expr) {
        bool has_prefix = expr.qident.initial_doublecolon.has_value();
        std::vector<std::string> idents;
        for(auto ident : expr.qident.names){
            idents.push_back(ident.content);
        }
        _expr = unit::symbol_expression::from_identifier(name(has_prefix, std::move(idents)));
    }

    void ast_unit_visitor::visit_comma_expr(ast::expr_list_expr &) {

    }


} // k::parse