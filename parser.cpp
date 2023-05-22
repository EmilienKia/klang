//
// Created by Emilien Kia <emilien.kia+dev@gmail.com>.
//

#include "parser.hpp"


namespace k::parse {


//
// Exceptions
//
parsing_error::parsing_error(const std::string &arg) :
        runtime_error(arg)
{}

parsing_error::parsing_error(const char *string) :
        runtime_error(string)
{}

parsing_error::parsing_error(const std::string &arg, const lex::lexeme& lexeme) :
        runtime_error(arg),
        _lexeme(&lexeme)
{}

parsing_error::parsing_error(const char *string, const lex::lexeme& lexeme) :
        runtime_error(string),
        _lexeme(&lexeme)
{}


//
// Parser
//
parser::parser(std::string_view src)
{
    _lexer.parse(src);
}

ast::unit parser::parse(std::string_view src)
{
    _lexer.parse(src);

    return parse_unit();
}

ast::unit parser::parse_unit()
{
    auto module_name = parse_module_declaration();
    if(module_name) {
        _unit.module_name = module_name;
    }

    while(std::optional<ast::import> import = parse_import()) {
        _unit.imports.push_back(std::move(import.value()));
    }

    for(auto decl : parse_declarations()) {
        _unit.declarations.push_back(decl);
    }

    return _unit;
}

std::optional<ast::qualified_identifier> parser::parse_module_declaration()
{
    lex::lex_holder holder(_lexer);

    // Not a "module" keyword, skip module declaration
    if(auto lmod = _lexer.get(); !(lmod && lmod==lex::keyword::MODULE)) {
        holder.rollback();
        return {};
    }

    // Expect a module identifier:
    std::optional<ast::qualified_identifier> ident = parse_qualified_identifier();
    if(!ident) {
        // Err : module name as identifier is missing.
        throw parsing_error("Qualified identifier for module name is missing" /*, *lname */);
    }

    // Expect a semicolon to end module declaration
    if(auto lsemicolon = _lexer.get(); !(lsemicolon && lsemicolon==lex::punctuator::SEMICOLON)) {
        // Err : semicolon is missing
        throw parsing_error("Semicolon is missing at end of module declaration" /*, *lsemicolon */);
    }
    return ident;
}

std::optional<ast::import> parser::parse_import()
{
    lex::lex_holder holder(_lexer);

    // Not an "import" keyword, skip import declaration
    if(auto limport = _lexer.get(); !(limport && limport==lex::keyword::IMPORT)) {
        holder.rollback();
        return {};
    }

    // Expect an import identifier:
    auto lname= _lexer.get();
    if(!(lname && lex::is<lex::identifier>(lname))) {
        // Err : import name as identifier is missing.
        throw parsing_error("Identifier for import name is missing" /*, *lname */);
    }

    // Expect a semicolon to end import declaration
    if(auto lsemicolon = _lexer.get(); !(lsemicolon && lsemicolon==lex::punctuator::SEMICOLON)) {
        // Err : semicolon is missing
        throw parsing_error("Semicolon is missing at end of import declaration" /*, *lsemicolon */);
    }

    return {lex::as<lex::identifier>(lname)};
}

std::vector<ast::decl_ptr> parser::parse_declarations()
{
    std::vector<ast::decl_ptr> declarations;
    while(ast::decl_ptr declaration = parse_declaration()) {
        declarations.push_back(declaration);
    }
    return declarations;
}

ast::decl_ptr parser::parse_declaration()
{
    lex::lex_holder holder(_lexer);

    // Look for a visibility decl
    if(auto decl = parse_visibility_decl()) {
        return std::make_shared<ast::visibility_decl>(decl.value());
    }

    // Look for a namespace decl
    if(auto decl = parse_namespace_decl()) {
        return std::make_shared<ast::namespace_decl>(decl.value());
    }

    // Look for a function decl
    if(auto decl = parse_function_decl()) {
        return std::make_shared<ast::function_decl>(decl.value());
    }

    // Look for a variable decl
    if(auto decl = parse_variable_decl()) {
        return std::make_shared<ast::variable_decl>(decl.value());
    }

    holder.rollback();
    return {};
}

std::optional<ast::visibility_decl> parser::parse_visibility_decl()
{
    lex::lex_holder holder(_lexer);

    if(auto lkw = _lexer.get()) {
        if(lkw==lex::keyword::PUBLIC || lkw==lex::keyword::PROTECTED || lkw==lex::keyword::PRIVATE) {
            // Expect a colon
            if(lex::opt_ref_any_lexeme lcolon = _lexer.get(); lcolon && lcolon==lex::operator_::COLON) {
                return ast::visibility_decl{lex::as<lex::keyword>(lkw)};
            }
        }
    }
    holder.rollback();
    return {};
}

std::optional<ast::namespace_decl> parser::parse_namespace_decl()
{
    lex::lex_holder holder(_lexer);

    // Not a "namespace" keyword, skip namespace declaration
    if(lex::opt_ref_any_lexeme lnamespace = _lexer.get(); !(lnamespace && lnamespace==lex::keyword::NAMESPACE)) {
        holder.rollback();
        return {};
    }

    // Eventually expect an import identifier
    lex::opt_ref_any_lexeme lname = _lexer.get();
    std::optional<lex::identifier> name;
    if(lname && lex::is<lex::identifier>(lname)) {
        name = lex::as<lex::identifier>(lname);
    } else {
        _lexer.unget();
        lname.reset();
    }

    // Expect an open brace
    if(lex::opt_ref_any_lexeme lopenbrace= _lexer.get(); !(lopenbrace && lopenbrace==lex::punctuator::BRACE_OPEN)) {
        // Err: namespace declaration shall have an open brace.
        throw parsing_error("Open brace for namespace is missing" /*, *lopenbrace */);
    }

    std::vector<ast::decl_ptr> declarations = parse_declarations();

    // Expect a closing brace
    if(lex::opt_ref_any_lexeme lclosingbrace= _lexer.get(); !(lclosingbrace && lclosingbrace==lex::punctuator::BRACE_CLOSE)) {
        // Err: namespace declaration shall have a closing brace.
        throw parsing_error("Closing brace for namespace is missing" /*, *lclosingbrace */);
    }

    return {{name, declarations}};
}

std::vector<lex::keyword> parser::parse_specifiers()
{
    std::vector<lex::keyword> res;
    lex::lex_holder holder(_lexer);

    lex::opt_ref_any_lexeme lkw;
    while(lkw = _lexer.get(), lex::is<lex::keyword>(lkw)) {
        if( lkw==lex::keyword::PUBLIC
            || lkw==lex::keyword::PROTECTED
            || lkw==lex::keyword::PRIVATE
            || lkw==lex::keyword::STATIC
            || lkw==lex::keyword::CONST
            || lkw==lex::keyword::ABSTRACT
            || lkw==lex::keyword::FINAL
                ) {
            res.push_back(lex::as<lex::keyword>(lkw));
            holder.sync();
        }
    }
    holder.rollback();
    return res;
}

std::optional<ast::qualified_identifier> parser::parse_qualified_identifier()
{
    lex::lex_holder holder(_lexer);

    std::optional<lex::punctuator> initial;
    if(lex::opt_ref_any_lexeme linitdoublecolon= _lexer.get(); linitdoublecolon && linitdoublecolon==lex::punctuator::DOUBLE_COLON) {
        initial = lex::as<lex::punctuator>(linitdoublecolon);
    } else {
        holder.rollback();
    }

    std::vector<lex::identifier> names;

    // Expect a first name:
    if(auto lname= _lexer.get(); lname && lex::is<lex::identifier>(lname)) {
        names.push_back(lex::as<lex::identifier>(lname));
    } else {
        // No identifier:
        if(!initial) {
            holder.rollback();
            return {};
        } else {
            // Err: qualified identifier requires at least and identifier when have initial "::".
            throw parsing_error("Identifier for qualified identifier is missing" /*, *lname */);
        }
    }

    holder.sync();

    // Look for following identifiers
    while(lex::opt_ref_any_lexeme ldoublecolon= _lexer.get()) {
        if(!(ldoublecolon==lex::punctuator::DOUBLE_COLON)) {
            holder.rollback();
            break;
        }
        if(auto lname= _lexer.get(); lname && lex::is<lex::identifier>(lname)) {
            names.push_back(lex::as<lex::identifier>(lname));
            holder.sync();
        } else {
            // Err: qualified identifier requires at least and identifier.
            throw parsing_error("Identifier for qualified identifier is missing" /*, *lname */);
        }
    }

    return {{initial, names}};
}

std::optional<ast::function_decl> parser::parse_function_decl() {
    lex::lex_holder holder(_lexer);

    std::vector<lex::keyword> specifiers = parse_specifiers();

    // Expect a name:
    auto lname= _lexer.get();
    if(!(lname && lex::is<lex::identifier>(lname))) {
        holder.rollback();
        return {};
        // Err: function declaration requires at least and identifier.
        //throw parsing_error("Identifier for function declaration is missing" /*, *lname */);
    }

    // Look for open parenthesis
    if(auto lopenpar = _lexer.get(); !(lopenpar && lopenpar==lex::punctuator::PARENTHESIS_OPEN)) {
        holder.rollback();
        return {};
        // Err: function declaration requires an open parenthesis.
        //throw parsing_error("Open parenthesis for function declaration is missing" /*, *lopenpar */);
    }

    // Look for parameter_spec declarations
    std::vector<ast::parameter_spec> params;
    auto lex = _lexer.get();
    if(!lex) {
        // Err: function declaration requires a closing parenthesis.
        throw parsing_error("Closing parenthesis for function declaration is missing" /*, *lex */);
    }
    if(!(lex==lex::punctuator::PARENTHESIS_CLOSE)) {
        _lexer.unget();
        holder.sync();
        // Look for first parameter_spec
        auto param = parse_parameter_spec();
        if(param) {
            params.push_back(param.value());
        } else {
            // Err: function declaration requires a parameter_spec declaration.
            throw parsing_error("Parameter declaration for function declaration is missing" /*, *lex */);
        }

        while(true) {
            lex = _lexer.get();
            if(!lex) {
                // Err: function declaration requires a closing parenthesis or a comma.
                throw parsing_error("Closing parenthesis or comma for function declaration is missing" /*, *lex */);
            }
            if(lex==lex::punctuator::PARENTHESIS_CLOSE) {
                break;
            }
            if(!(lex==lex::punctuator::COMMA)){
                // Err: function declaration requires a closing parenthesis or a comma.
                throw parsing_error("Closing parenthesis or comma for function declaration is missing" /*, *lex */);
            }

            // Look for next parameter_spec
            auto param = parse_parameter_spec();
            if(param) {
                params.push_back(param.value());
            } else {
                // Err: function declaration requires a parameter_spec declaration.
                throw parsing_error("Parameter declaration for function declaration is missing" /*, *lex */);
            }
        }
    }

    // Look for return type
    std::optional<ast::type_specifier> restype;
    holder.sync();
    if(auto lcolon = _lexer.get(); lcolon && lcolon==lex::operator_::COLON) {
        restype = parse_type_spec();
        if(!restype) {
            // Err: function declaration requires a return type declaration.
            throw parsing_error("Return type declaration for function declaration is missing" /*, *lex */);
        }
    } else {
        holder.rollback();
    }

    auto statements = parse_statement_block();
    if(statements) {
        return {{specifiers, lex::as<lex::identifier>(lname), restype, params, statements}};
    } else
    // Look for final semicolon
    if(auto lsemicolon = _lexer.get(); !(lsemicolon && lsemicolon==lex::punctuator::SEMICOLON)) {
        // Err: function declaration requires a final semiclon.
        throw parsing_error("Final semicolon for function declaration is missing" /*, *lsemicolon */);
    }

    return {{specifiers, lex::as<lex::identifier>(lname), restype, params, {}}};
}

std::optional<ast::parameter_spec> parser::parse_parameter_spec()
{
    lex::lex_holder holder(_lexer);

    std::vector<lex::keyword> specifiers = parse_specifiers();

    std::optional<lex::identifier> name;
    lex::lex_holder holder_name(_lexer);
    if(auto lname = _lexer.get(); lname && lex::is<lex::identifier>(lname)) {
        if(auto lcolon = _lexer.get(); lcolon && lcolon==lex::operator_::COLON) {
            name = lex::as<lex::identifier>(lname);
        } else {
            holder_name.rollback();
        }
    } else {
        holder_name.rollback();
    }

    auto type = parse_type_spec();
    if(!type) {
        holder.rollback();
        return {};
    }

    return {{specifiers, name, type.value()}};
}

std::optional<ast::block_statement> parser::parse_statement_block()
{
    lex::lex_holder holder(_lexer);

    // Look for open brace
    if(auto lopenbrace = _lexer.get(); !(lopenbrace && lopenbrace==lex::punctuator::BRACE_OPEN)) {
        holder.rollback();
        return {};
        // Err: statement block requires a opening brace.
        //throw parsing_error("Closing brace for statement block is missing" /*, *lopenbrace */);
    }

    std::vector<ast::any_statement> statements;
    while(auto statement = parse_statement()) {
        if(statement) {
            statements.push_back(ast::any_statement{statement});
        }
    }

    // Look for closing brace
    if(auto lclosebrace = _lexer.get(); !(lclosebrace && lclosebrace == lex::punctuator::BRACE_CLOSE)) {
        // Err: statement block requires a closing brace.
        throw parsing_error("Final closing brace for statement block is missing" /*, *lclosebrace */);
    }

    return {{statements}};
}

std::optional<ast::return_statement> parser::parse_return_statement()
{
    lex::lex_holder holder(_lexer);

    if(auto lreturn = _lexer.get(); !(lreturn && lreturn==lex::keyword::RETURN)) {
        holder.rollback();
        return {};
    }

    ast::expr_ptr expr = parse_expression();

    auto lsemicolon = _lexer.get();
    if(!(lsemicolon && lsemicolon==lex::punctuator::SEMICOLON)) {
        // Err: expression statement requires to be finished by a semicolon.
        throw parsing_error("Semicolon for return statement is missing" /*, *lsemicolon */);
    }

    return {{expr}};

}

ast::any_statement_opt parser::parse_statement()
{
    auto block = parse_statement_block();
    if(block) {
        return ast::any_statement_opt{block.value()};
    }

    auto ret = parse_return_statement();
    if(ret) {
        return ast::any_statement_opt{ret.value()};
    }

    auto var = parse_variable_decl();
    if(var) {
        return ast::any_statement_opt{var.value()};
    }

    auto expr = parse_expression_statement();
    if(expr) {
        return ast::any_statement_opt{expr.value()};
    }

    return {};
}


std::optional<ast::variable_decl> parser::parse_variable_decl()
{
    lex::lex_holder holder(_lexer);

    std::vector<lex::keyword> specifiers = parse_specifiers();

    // Expect a name:
    auto lname = _lexer.get();
    if(!(lname && lex::is<lex::identifier>(lname))) {
        holder.rollback();
        return {};
        // Err: variable declaration requires at least and identifier.
        //throw parsing_error("Identifier for variable declaration is missing" /*, *lname */);
    }

    // Look for the type specifier
    auto lcolon = _lexer.get();
    if(!(lcolon && lcolon==lex::operator_::COLON)) {
        // Err: variable declaration requires at least and identifier and a colon.
        // Err: variable declaration requires a type specifier prefixed by colon.
        //throw parsing_error("Colon for variable type declaration is missing" /*, *lcolon */);
        holder.rollback();
        return {};
    }
    std::optional<ast::type_specifier> type = parse_type_spec();
    if(!type) {
        // Err: variable declaration requires a type specifier prefixed by colon.
        throw parsing_error("Colon for variable type declaration is missing" /*, *lcolon */);
    }

    ast::expr_ptr expr;
    auto lequal = _lexer.get();
    if(lequal==lex::operator_::EQUAL) {
        expr = parse_conditional_expr();
        if(!expr) {
            // Err: variable declaration requires an expression after an equal sign.
            throw parsing_error("Expression for variable initialization after the equal sign is missing" /*, *lequal */);
        }
    } else {
        _lexer.unget();
    }

    auto lsemicolon = _lexer.get();
    if(!(lsemicolon && lsemicolon==lex::punctuator::SEMICOLON)) {
        // Err: variable declaration requires to be finished by a semicolon.
        throw parsing_error("Semicolon for variable declaration is missing" /*, *lsemicolon */);
    }

    return {{specifiers, lex::as<lex::identifier>(lname), type.value(), expr}};
}

std::optional<ast::type_specifier> parser::parse_type_spec()
{
    lex::lex_holder holder(_lexer);

    // Expect a type qualified identifier:
    std::optional<ast::qualified_identifier> qid = parse_qualified_identifier();
    if(!qid) {
        holder.rollback();
        return {};
    }

    return {{*qid}};
}

std::optional<ast::expression_statement> parser::parse_expression_statement()
{
    ast::expr_ptr expr = parse_expression();
    if(!expr) {
        return {};
    }

    auto lsemicolon = _lexer.get();
    if(!(lsemicolon && lsemicolon==lex::punctuator::SEMICOLON)) {
        // Err: expression statement requires to be finished by a semicolon.
        throw parsing_error("Semicolon for expression statement is missing" /*, *lsemicolon */);
    }

    return {{expr}};
}

ast::expr_ptr parser::parse_expression()
{
    std::vector<ast::expr_ptr> exprs;

    if(ast::expr_ptr first = parse_assignment_expression()) {
        exprs.push_back(first);
    } else {
        return {};
    }

    while(true) {
        auto lcomma = _lexer.get();
        if (!(lcomma && lcomma == lex::punctuator::COMMA)) {
            _lexer.unget();
            if (exprs.size() == 1) {
                return {exprs[0]};
            } else {
                return std::make_shared<ast::expr_list_expr>(exprs);
            }
        }

        ast::expr_ptr next = parse_assignment_expression();
        if(next) {
            exprs.push_back(next);
        } else {
            // Err: expression requires a sub expression after a comma.
            throw parsing_error("Sub expression after a comma for expression is missing" /*, *lcomma */);
        }
    }
}

ast::expr_ptr parser::parse_expression_list() {
    // Same code than parse_expression(...)
    return parse_expression();
}

ast::expr_ptr parser::parse_assignment_expression()
{
    ast::expr_ptr cond = parse_conditional_expr();
    if(!cond) {
        return {};
    }

    lex::opt_ref_any_lexeme lop = _lexer.get();
    if(!(lex::is<lex::operator_>(lop) && (
                   lop==lex::operator_::EQUAL
                || lop==lex::operator_::STAR_EQUAL
                || lop==lex::operator_::SLASH_EQUAL
                || lop==lex::operator_::PERCENT_EQUAL
                || lop==lex::operator_::PLUS_EQUAL
                || lop==lex::operator_::MINUS_EQUAL
                || lop==lex::operator_::DOUBLE_CHEVRON_OPEN_EQUAL
                || lop==lex::operator_::DOUBLE_CHEVRON_CLOSE_EQUAL
                || lop==lex::operator_::AMPERSAND_EQUAL
                || lop==lex::operator_::CARET_EQUAL
                || lop==lex::operator_::PIPE_EQUAL
            )))
    {
        _lexer.unget();
        return cond;
    }

    ast::expr_ptr other = parse_assignment_expression();
    if(other) {
        return std::make_shared<ast::binary_operator_expr>(lex::as<lex::operator_>(lop), cond, other);
    } else {
        // Err: assignment expression requires a sub expression after the operator.
        throw parsing_error("Sub expression after assignment expression is missing" /*, *lop */);
    }
}

ast::expr_ptr parser::parse_conditional_expr() {
    ast::expr_ptr left = parse_logical_or_expression();
    if (!left) {
        return {};
    }

    auto lqm = _lexer.get();
    if (!(lqm && lqm == lex::operator_::QUESTION_MARK)) {
        _lexer.unget();
        return left;
    }

    ast::expr_ptr middle = parse_logical_or_expression();
    if(!middle) {
        // Err: conditional expression requires a sub expression after the operator.
        throw parsing_error("Sub expression after question mark of conditional expression is missing" /*, *lqm */);
    }

    auto lcolon = _lexer.get();
    if (!(lqm && lqm == lex::operator_::COLON)) {
        // Err: conditional expression requires a colon after sub expression.
        throw parsing_error("Colon of conditional expression is missing" /*, *lcolon */);
    }

    ast::expr_ptr right = parse_logical_or_expression();
    if(!right) {
        // Err: conditional expression requires a sub expression after the operator.
        throw parsing_error("Sub expression after colon of conditional expression is missing" /*, *lcolon */);
    }

    return std::make_shared<ast::conditional_expr>(lex::as<lex::operator_>(lqm), lex::as<lex::operator_>(lcolon), left, middle, right);
}

ast::expr_ptr parser::parse_logical_or_expression()
{
    ast::expr_ptr left_expr;

    if(ast::expr_ptr first = parse_logical_and_expression()) {
        left_expr = first;
    } else {
        return {};
    }

    auto op = _lexer.get();
    if (!(op && op == lex::operator_::DOUBLE_PIPE)) {
        _lexer.unget();
        return left_expr;
    }

    ast::expr_ptr right_expr = parse_logical_or_expression();
    if(right_expr) {
        return std::make_shared<ast::binary_operator_expr>(lex::as<lex::operator_>(op), left_expr, right_expr);
    } else {
        // Err: expression requires a sub expression after a double pipe.
        throw parsing_error("Sub expression after a double pipe for expression is missing" /*, *op */);
    }

}

ast::expr_ptr parser::parse_logical_and_expression()
{
    ast::expr_ptr left_expr;

    if(ast::expr_ptr first = parse_inclusive_bin_or_expr()) {
        left_expr = first;
    } else {
        return {};
    }

    auto op = _lexer.get();
    if (!(op && op == lex::operator_::DOUBLE_AMPERSAND)) {
        _lexer.unget();
        return left_expr;
    }

    ast::expr_ptr right_expr = parse_logical_and_expression();
    if(right_expr) {
        return std::make_shared<ast::binary_operator_expr>(lex::as<lex::operator_>(op), left_expr, right_expr);
    } else {
        // Err: expression requires a sub expression after a double ampersand.
        throw parsing_error("Sub expression after a double ampersand for expression is missing" /*, *op */);
    }

}

ast::expr_ptr parser::parse_inclusive_bin_or_expr()
{
    ast::expr_ptr left_expr;

    if(ast::expr_ptr first = parse_exclusive_bin_or_expr()) {
        left_expr = first;
    } else {
        return {};
    }

    auto op = _lexer.get();
    if (!(op && op == lex::operator_::PIPE)) {
        _lexer.unget();
        return left_expr;
    }

    ast::expr_ptr right_expr = parse_inclusive_bin_or_expr();
    if(right_expr) {
        return std::make_shared<ast::binary_operator_expr>(lex::as<lex::operator_>(op), left_expr, right_expr);
    } else {
        // Err: expression requires a sub expression after a pipe.
        throw parsing_error("Sub expression after a pipe for expression is missing" /*, *op */);
    }

}

ast::expr_ptr parser::parse_exclusive_bin_or_expr()
{
    ast::expr_ptr left_expr;

    if(ast::expr_ptr first = parse_bin_and_expr()) {
        left_expr = first;
    } else {
        return {};
    }

    auto op = _lexer.get();
    if (!(op && op == lex::operator_::CARET)) {
        _lexer.unget();
        return left_expr;
    }

    ast::expr_ptr right_expr = parse_exclusive_bin_or_expr();
    if(right_expr) {
        return std::make_shared<ast::binary_operator_expr>(lex::as<lex::operator_>(op), left_expr, right_expr);
    } else {
        // Err: expression requires a sub expression after a caret.
        throw parsing_error("Sub expression after a caret for expression is missing" /*, *op */);
    }

}

ast::expr_ptr parser::parse_bin_and_expr()
{
    ast::expr_ptr left_expr;

    if(ast::expr_ptr first = parse_equality_expr()) {
        left_expr = first;
    } else {
        return {};
    }

    auto op = _lexer.get();
    if (!(op && op == lex::operator_::AMPERSAND)) {
        _lexer.unget();
        return left_expr;
    }

    ast::expr_ptr right_expr = parse_bin_and_expr();
    if(right_expr) {
        return std::make_shared<ast::binary_operator_expr>(lex::as<lex::operator_>(op), left_expr, right_expr);
    } else {
        // Err: expression requires a sub expression after an ampersand.
        throw parsing_error("Sub expression after an ampersand for expression is missing" /*, *op */);
    }


}

ast::expr_ptr parser::parse_equality_expr()
{
    ast::expr_ptr left_expr;

    if(ast::expr_ptr first = parse_relational_expr()) {
        left_expr = first;
    } else {
        return {};
    }

    auto op = _lexer.get();
    if (!(op && (op == lex::operator_::DOUBLE_EQUAL || op == lex::operator_::EXCLAMATION_MARK_EQUAL))) {
        _lexer.unget();
        return left_expr;
    }

    ast::expr_ptr right_expr = parse_equality_expr();
    if(right_expr) {
        return std::make_shared<ast::binary_operator_expr>(lex::as<lex::operator_>(op), left_expr, right_expr);
    } else {
        // Err: expression requires a sub expression after an equality/difference.
        throw parsing_error("Sub expression after an equality/difference for expression is missing" /*, *lop */);
    }


}

ast::expr_ptr parser::parse_relational_expr()
{
    ast::expr_ptr left_expr;

    if(ast::expr_ptr first = parse_shifting_expr()) {
        left_expr = first;
    } else {
        return {};
    }

    auto op = _lexer.get();
    if (!(op && (op == lex::operator_::CHEVRON_CLOSE || op == lex::operator_::CHEVRON_OPEN
                  || op == lex::operator_::CHEVRON_CLOSE_EQUAL || op == lex::operator_::CHEVRON_OPEN_EQUAL))) {
        _lexer.unget();
        return left_expr;
    }

    ast::expr_ptr right_expr = parse_relational_expr();
    if(right_expr) {
        return std::make_shared<ast::binary_operator_expr>(lex::as<lex::operator_>(op), left_expr, right_expr);
    } else {
        // Err: expression requires a sub expression after a relational operator.
        throw parsing_error("Sub expression after an relational operator for expression is missing" /*, *lop */);
    }

}

ast::expr_ptr parser::parse_shifting_expr()
{
    ast::expr_ptr left_expr;

    if(ast::expr_ptr first = parse_additive_expr()) {
        left_expr = first;
    } else {
        return {};
    }

    auto op = _lexer.get();
    if (!(op && (op == lex::operator_::DOUBLE_CHEVRON_CLOSE || op == lex::operator_::DOUBLE_CHEVRON_OPEN))) {
        _lexer.unget();
        return left_expr;
    }

    ast::expr_ptr right_expr = parse_shifting_expr();
    if(right_expr) {
        return std::make_shared<ast::binary_operator_expr>(lex::as<lex::operator_>(op), left_expr, right_expr);
    } else {
        // Err: expression requires a sub expression after a shifting operator.
        throw parsing_error("Sub expression after a shifting operator for expression is missing" /*, *lop */);
    }

}

ast::expr_ptr parser::parse_additive_expr()
{
    ast::expr_ptr left_expr;

    if(ast::expr_ptr first = parse_multiplicative_expr()) {
        left_expr = first;
    } else {
        return {};
    }

    auto op = _lexer.get();
    if (!(op && (op == lex::operator_::PLUS || op == lex::operator_::MINUS))) {
        _lexer.unget();
        return left_expr;
    }

    ast::expr_ptr right_expr = parse_additive_expr();
    if(right_expr) {
        return std::make_shared<ast::binary_operator_expr>(lex::as<lex::operator_>(op), left_expr, right_expr);
    } else {
        // Err: expression requires a sub expression after an additive operator.
        throw parsing_error("Sub expression after an additive operator for expression is missing" /*, *lop */);
    }

}

ast::expr_ptr parser::parse_multiplicative_expr()
{
    ast::expr_ptr left_expr;

    if(ast::expr_ptr first = parse_pm_expr()) {
        left_expr = first;
    } else {
        return {};
    }

    auto op = _lexer.get();
    if (!(op && (op == lex::operator_::STAR || op == lex::operator_::SLASH || op == lex::operator_::PERCENT))) {
        _lexer.unget();
        return left_expr;
    }

    ast::expr_ptr right_expr = parse_multiplicative_expr();
    if(right_expr) {
        return std::make_shared<ast::binary_operator_expr>(lex::as<lex::operator_>(op), left_expr, right_expr);
    } else {
        // Err: expression requires a sub expression after a multiplicative operator.
        throw parsing_error("Sub expression after a multiplicative operator for expression is missing" /*, *lop */);
    }


}

ast::expr_ptr parser::parse_pm_expr()
{
    ast::expr_ptr left_expr;

    if(ast::expr_ptr first = parse_cast_expr()) {
        left_expr = first;
    } else {
        return {};
    }

    auto op = _lexer.get();
    if (!(op && (op == lex::operator_::DOT_STAR || op == lex::operator_::ARROW_STAR))) {
        _lexer.unget();
        return left_expr;
    }

    ast::expr_ptr right_expr = parse_pm_expr();
    if(right_expr) {
        return std::make_shared<ast::binary_operator_expr>(lex::as<lex::operator_>(op), left_expr, right_expr);
    } else {
        // Err: expression requires a sub expression after a pointer-member operator.
        throw parsing_error("Sub expression after a pointer-member operator for expression is missing" /*, *lop */);
    }


}

ast::expr_ptr parser::parse_cast_expr()
{
    lex::lex_holder holder(_lexer);

    if(auto lopenpar = _lexer.get(); !(lopenpar && lopenpar == lex::punctuator::PARENTHESIS_OPEN)) {
        holder.rollback();
        return parse_unary_expr();
    }

    std::optional<ast::type_specifier> type = parse_type_spec();
    if(!type) {
        holder.rollback();
        return parse_unary_expr();
    }

    if(auto lclosepar = _lexer.get(); !(lclosepar && lclosepar == lex::punctuator::PARENTHESIS_CLOSE)) {
        // Err: cast expression requires to have a closing parenthesis.
        throw parsing_error("Sub expression after a pointer-member operator for expression is missing" /*, *lclosepar */);
    }

    ast::expr_ptr expr = parse_cast_expr();
    if(!expr) {
        // Err: cast expression requires to have an expression.
        throw parsing_error("Sub expression after a casting operator for expression is missing" /*, *lclosepar */);
    }

    return std::make_shared<ast::cast_expr>(type.value(), expr);
}

ast::expr_ptr parser::parse_unary_expr()
{
    lex::lex_holder holder(_lexer);

    if(auto lop = _lexer.get(); lop && (
                    lop == lex::operator_::DOUBLE_PLUS
                ||  lop == lex::operator_::DOUBLE_MINUS
                ||  lop == lex::operator_::STAR
                ||  lop == lex::operator_::AMPERSAND
                ||  lop == lex::operator_::PLUS
                ||  lop == lex::operator_::MINUS
                ||  lop == lex::operator_::EXCLAMATION_MARK
                ||  lop == lex::operator_::TILDE
            )) {
        ast::expr_ptr expr = parse_cast_expr();
        if(expr) {
            return std::make_shared<ast::unary_prefix_expr>(lex::as<lex::operator_>(lop), expr);
        } else {
            // Err: unary prefix expression requires to have an expression.
            throw parsing_error("Sub expression after a unary prefix for expression is missing" /*, *lop */);
        }
    } else {
        holder.rollback();
        return parse_postfix_expr();
    }
}

ast::expr_ptr parser::parse_postfix_expr()
{
    lex::lex_holder holder(_lexer);

    ast::expr_ptr any = parse_primary_expr();
    if(!any) {
        holder.rollback();
        return {};
    }

    while(auto lop = _lexer.get())
    {
        if(lop == lex::operator_::DOUBLE_PLUS || lop == lex::operator_::DOUBLE_MINUS) {
            any = std::make_shared<ast::unary_postfix_expr>(lex::as<lex::operator_>(lop), any);
        } else if(lop == lex::punctuator::BRACKET_OPEN) {
            ast::expr_ptr expr = parse_expression();
            if(!expr) {
                // Err: Expression with postfix bracket index requires to have a sub-expression.
                throw parsing_error("Sub expression of bracket index postfix for expression is missing" /*, *lop */);
            }
            auto lclose = _lexer.get();
            if(!(lclose && lop == lex::punctuator::BRAKET_CLOSE)) {
                // Err:  Expression with postfix bracket index requires to have a closing bracket.
                throw parsing_error("Closing bracket of bracket index suffix for expression is missing" /*, *lop */);
            }
            any = std::make_shared<ast::bracket_postifx_expr>(any, expr);
        } else if(lop == lex::punctuator::PARENTHESIS_OPEN) {
            ast::expr_ptr expr = parse_expression_list();
            // expr might be null if expression list is empty
            auto lclose = _lexer.get();
            if(!(lclose && lop == lex::punctuator::PARENTHESIS_CLOSE)) {
                // Err:  Expression with postfix parenthesis expression list requires to have a closing bracket.
                throw parsing_error("Closing bracket of parenthesis expression list suffix for expression is missing" /*, *lop */);
            }
            any = std::make_shared<ast::parenthesis_postifx_expr>(any, expr);
        } else {
            _lexer.unget();
            break;
        }
    }

    return any;
}

ast::expr_ptr parser::parse_primary_expr()
{
    lex::lex_holder holder(_lexer);

    lex::opt_ref_any_lexeme l = _lexer.get();
    if (lex::is<lex::literal>(l)) {
        return std::make_shared<ast::literal_expr>(lex::as_any_literal(l));
    } else if ( l == lex::keyword::THIS) {
        return std::make_shared<ast::this_expr>(lex::as<lex::keyword>(l));
    } else if( l == lex::punctuator::PARENTHESIS_OPEN) {
        ast::expr_ptr expr = parse_expression();
        if(!expr) {
            // Err: Primary expression with open parenthesis requires an expression
            throw parsing_error("Primary expression with open parenthesis expects an expression" /*, *expr */);
        }
        lex::opt_ref_any_lexeme r = _lexer.get();
        if(!r || r != lex::punctuator::PARENTHESIS_CLOSE) {
            // Err: Primary expression with open parenthesis then expression requires a closing parenthesis
            throw parsing_error("Primary expression with open parenthesis then expression requires a closing parenthesis" /*, *expr */);
        }
        return expr;
    } else {
        holder.rollback();
        return parse_identifier_expr();
    }
}

ast::expr_ptr parser::parse_identifier_expr()
{
    lex::lex_holder holder(_lexer);

    std::optional<ast::qualified_identifier> ident = parse_qualified_identifier();
    if(ident) {
        return std::make_shared<ast::identifier_expr>(*ident);
    } else {
        holder.rollback();
        return {};
    }
}

} // k::parse_all
