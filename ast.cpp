//
// Created by Emilien Kia <emilien.kia+dev@gmail.com>.
//

#include "ast.hpp"


namespace k::parse {


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

void ast::identified_type_specifier::visit(ast_visitor &visitor) {
    visitor.visit_identified_type_specifier(*this);
}

void ast::keyword_type_specifier::visit(ast_visitor &visitor) {
    visitor.visit_keyword_type_specifier(*this);
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

void default_ast_visitor::visit_variable_decl(ast::variable_decl &) {

}

void default_ast_visitor::visit_function_decl(ast::function_decl &) {

}

void default_ast_visitor::visit_block_statement(ast::block_statement &) {

}

void default_ast_visitor::visit_return_statement(ast::return_statement &) {

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

void default_ast_visitor::visit_identifier_expr(ast::identifier_expr &) {

}

void default_ast_visitor::visit_comma_expr(ast::expr_list_expr &) {

}


} // k::parse
