//
// Created by Emilien Kia <emilien.kia+dev@gmail.com>.
//

#include "ast_unit_visitor.hpp"

#include "common.hpp"

namespace k::parse {

    void ast_unit_visitor::visit(k::parse::ast::unit& src, k::unit::unit& unit) {
        ast_unit_visitor visitor(unit);
        visitor.visit_unit(src);
    }

    void ast_unit_visitor::visit_unit(ast::unit &unit) {
        // Push root ns context
        stack<ns_context> push(_contexts, _unit.get_root_namespace());

        if(unit.module_name) {
            _unit.set_unit_name(unit.module_name->to_name());
        }

        super::visit_unit(unit);
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
            // TODO throw exception : current context doesnt support default visibility
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
                // TODO throw an exception : Unrecognized visibility context keyword
                break;
        }
    }

    void ast_unit_visitor::visit_namespace_decl(ast::namespace_decl &ns) {
        auto parent_scope = std::dynamic_pointer_cast<ns_context>(_contexts.back());
        if(!parent_scope) {
            // TODO throw exception : current context is not a namespace
        }

        std::shared_ptr<k::unit::ns> namesp = parent_scope->content->get_child_namespace(ns.name->content);

        // Push namespace context
        stack<ns_context> push(_contexts, namesp);

        super::visit_namespace_decl(ns);
    }

    void ast_unit_visitor::visit_variable_decl(ast::variable_decl &decl) {
        std::shared_ptr<context> parent_context = _contexts.back();
        std::shared_ptr<unit::variable_holder> parent_scope;

        if(auto parent = std::dynamic_pointer_cast<generic_context<unit::ns>>(parent_context)) {
            parent_scope = std::dynamic_pointer_cast<unit::variable_holder>(parent->content);
        } else if(auto parent = std::dynamic_pointer_cast<generic_context<unit::block>>(parent_context)) {
            parent_scope = std::dynamic_pointer_cast<unit::variable_holder>(parent->content);
        } else {
            // TODO throw exception : current context doesnt support variable declaration
        }

        if(!parent_scope) {
            // TODO throw exception : current context doesnt support variable declaration
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
            // TODO throw exception : current context doesnt support functions
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
            std::shared_ptr<unit::parameter> parameter = function->append_parameter(param.name->content, unit::unresolved_type::from_type_specifier(*param.type));
            // TODO add param specs
        }

        if(func.content) {
            visit_block_statement(*func.content);
        }
    }

    void ast_unit_visitor::visit_block_statement(ast::block_statement &block_stmt) {
        auto parent_context = _contexts.back();

        std::shared_ptr<unit::block> block;

        if(auto parent = std::dynamic_pointer_cast<func_context>(parent_context) ) {
            // Root block of function
            block = parent->content->get_block();
        } else if(auto parent = std::dynamic_pointer_cast<block_context>(parent_context)) {
            // Nested block
            block = parent->content->append_block_statement();
        }

        // Push function context
        stack<block_context> push(_contexts, block);

        // Visit all children statements
        for(auto& statement : block_stmt.statements) {
            statement->visit(*this);
        }
    }

    void ast_unit_visitor::visit_return_statement(ast::return_statement &stmt) {
        std::shared_ptr<block_context> block = std::dynamic_pointer_cast<block_context>(_contexts.back());

        std::shared_ptr<unit::return_statement> ret_stmt = block->content->append_return_statement();

        // Push function context
        stack<return_context> push(_contexts, ret_stmt);

        _expr.reset();
        if(stmt.expr) {
            stmt.expr->visit(*this);
        }
        if(_expr) {
            ret_stmt->set_expression(_expr);
        }
        _expr.reset();
    }

    void ast_unit_visitor::visit_expression_statement(ast::expression_statement &stmt) {
        std::shared_ptr<block_context> block = std::dynamic_pointer_cast<block_context>(_contexts.back());

        std::shared_ptr<unit::expression_statement> expr = block->content->append_expression_statement();

        // Push function context
        stack<expr_stmt_context> push(_contexts, expr);

        _expr.reset();
        if(stmt.expr) {
            stmt.expr->visit(*this);
        }
        if(_expr) {
            expr->set_expression(_expr);
        }
        _expr.reset();
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
            default: // TODO other operations
                break;
        }
    }

    void ast_unit_visitor::visit_cast_expr(ast::cast_expr& expr) {
        expr.expr()->visit(*this);
        _expr = unit::cast_expression::make_shared(_expr, unit::unresolved_type::from_type_specifier(*expr.type));
    }

    void ast_unit_visitor::visit_unary_prefix_expr(ast::unary_prefix_expr &) {

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