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
// Note: Last parser log number: 0x1003A
//

#include "parser.hpp"

#include "../common/logger.hpp"

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

//
// Parser
//
parser::parser(k::log::logger& logger, std::string_view src):
    lexeme_logger(logger, 0x10000),
    _lexer(logger)
{
    _lexer.parse(src);
}

std::shared_ptr<ast::unit> parser::parse_unit()
{
    auto unit = std::make_shared<ast::unit>();

    auto module_name = parse_module_declaration();
    if(module_name) {
        unit->module_name = module_name;
    }

    while(auto import = parse_import()) {
        unit->imports.push_back(import);
    }

    for(auto decl : parse_declarations()) {
        unit->declarations.push_back(decl);
    }

    return unit;
}

std::shared_ptr<ast::module_name> parser::parse_module_declaration()
{
    lex::lex_holder holder(_lexer);

    // Not a "module" keyword, skip module declaration
    auto lmod = _lexer.get();
    if(lmod!=lex::keyword::MODULE) {
        holder.rollback();
        return {};
    }

    // Expect a module identifier:
    std::shared_ptr<ast::qualified_identifier> ident = parse_qualified_identifier();
    if(!ident) {
        throw_error(0x0001, _lexer.pick(), "Module name is missing");
    }

    // Expect a semicolon to end module declaration
    if(auto lsemicolon = _lexer.get(); lsemicolon!=lex::punctuator::SEMICOLON) {
        throw_error(0x0002, _lexer.pick(), "Semicolon is missing after module name at end of module declaration");
    }
    return std::make_shared<ast::module_name>(lex::as<lex::keyword>(lmod), ident);
}

std::shared_ptr<ast::import> parser::parse_import()
{
    lex::lex_holder holder(_lexer);

    // Not an "import" keyword, skip import declaration
    auto limport = _lexer.get();
    if(limport!=lex::keyword::IMPORT) {
        holder.rollback();
        return {};
    }

    // Expect an import identifier:
    auto lname= _lexer.get();
    if(lex::is_not<lex::identifier>(lname)) {
        throw_error(0x0003, _lexer.pick(), "Import name identifier is missing");
    }

    // Expect a semicolon to end import declaration
    if(auto lsemicolon = _lexer.get(); lsemicolon!=lex::punctuator::SEMICOLON) {
        throw_error(0x0004, _lexer.pick(), "Semicolon is missing after module name at end of import declaration");
    }

    return std::make_shared<ast::import>(lex::as<lex::keyword>(limport), lex::as<lex::identifier>(lname));
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
        return decl;
    }

    // Look for a namespace decl
    if(auto decl = parse_namespace_decl()) {
        return decl;
    }

    // Look for a function decl
    if(auto decl = parse_function_decl()) {
        return decl;
    }

    // Look for a variable decl
    if(auto decl = parse_variable_decl()) {
        return decl;
    }

    holder.rollback();
    return {};
}

std::shared_ptr<ast::visibility_decl> parser::parse_visibility_decl()
{
    lex::lex_holder holder(_lexer);

    if(auto lkw = _lexer.get()) {
        if(lkw==lex::keyword::PUBLIC || lkw==lex::keyword::PROTECTED || lkw==lex::keyword::PRIVATE) {
            // Expect a colon
            if(lex::opt_ref_any_lexeme lcolon = _lexer.get(); lcolon==lex::operator_::COLON) {
                return std::make_shared<ast::visibility_decl>(lex::as<lex::keyword>(lkw));
            }
        }
    }
    holder.rollback();
    return {};
}

std::shared_ptr<ast::namespace_decl> parser::parse_namespace_decl()
{
    lex::lex_holder holder(_lexer);

    std::optional<lex::keyword> ns;
    std::optional<lex::punctuator> open_par, close_par;

    // Not a "namespace" keyword, skip namespace declaration
    if(lex::opt_ref_any_lexeme lnamespace = _lexer.get(); lnamespace==lex::keyword::NAMESPACE) {
        ns = lex::as<lex::keyword>(lnamespace);
    } else {
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
    if(lex::opt_ref_any_lexeme lopenbrace= _lexer.get(); lopenbrace==lex::punctuator::BRACE_OPEN) {
        open_par = lex::as<lex::punctuator>(lopenbrace);
    } else {
        throw_error(0x0005, _lexer.pick(), "Namespace open brace is missing");
    }

    std::vector<ast::decl_ptr> declarations = parse_declarations();

    // Expect a closing brace
    if(lex::opt_ref_any_lexeme lclosingbrace= _lexer.get(); lclosingbrace==lex::punctuator::BRACE_CLOSE) {
        close_par = lex::as<lex::punctuator>(lclosingbrace);
    } else {
        throw_error(0x0006, _lexer.pick(), "Namespace closing brace is expected");
    }

    return std::make_shared<ast::namespace_decl>(*ns, *open_par, *close_par, name, declarations);
}

std::vector<lex::keyword> parser::parse_specifiers()
{
    std::vector<lex::keyword> res;
    lex::lex_holder holder(_lexer);

    lex::opt_ref_any_lexeme lkw;
    while(lkw = _lexer.get(), lex::is<lex::keyword>(lkw)) {
        if( lex::is_one_of<lex::keyword::PUBLIC,
                lex::keyword::PROTECTED,
                lex::keyword::PRIVATE,
                lex::keyword::STATIC,
                lex::keyword::CONST,
                lex::keyword::ABSTRACT,
                lex::keyword::FINAL>(lkw)
        ) {
            res.push_back(lex::as<lex::keyword>(lkw));
            holder.sync();
        }
    }
    holder.rollback();
    return res;
}

std::shared_ptr<ast::qualified_identifier> parser::parse_qualified_identifier()
{
    lex::lex_holder holder(_lexer);

    std::optional<lex::punctuator> initial;
    if(lex::opt_ref_any_lexeme linitdoublecolon= _lexer.get(); linitdoublecolon==lex::punctuator::DOUBLE_COLON) {
        initial = lex::as<lex::punctuator>(linitdoublecolon);
    } else {
        holder.rollback();
    }

    std::vector<lex::identifier> names;

    // Expect a first name:
    if(auto lname= _lexer.get(); lex::is<lex::identifier>(lname)) {
        names.push_back(lex::as<lex::identifier>(lname));
    } else {
        // No identifier:
        if(!initial) {
            holder.rollback();
            return {};
        } else {
            throw_error(0x0007, _lexer.pick(), "Qualified identifier expect an identifier after initial \"::\"");
        }
    }

    holder.sync();

    // Look for following identifiers
    while(lex::opt_ref_any_lexeme ldoublecolon= _lexer.get()) {
        if(!(ldoublecolon==lex::punctuator::DOUBLE_COLON)) {
            holder.rollback();
            break;
        }
        if(auto lname= _lexer.get(); lex::is<lex::identifier>(lname)) {
            names.push_back(lex::as<lex::identifier>(lname));
            holder.sync();
        } else {
            throw_error(0x0008, _lexer.pick(), "Qualified identifier expect an identifier after intermediate \"::\"");
        }
    }

    return std::make_shared<ast::qualified_identifier>(initial, names);
}

std::shared_ptr<ast::function_decl> parser::parse_function_decl() {
    lex::lex_holder holder(_lexer);

    std::vector<lex::keyword> specifiers = parse_specifiers();

    // Expect a name:
    auto lname= _lexer.get();
    if(lex::is_not<lex::identifier>(lname)) {
        holder.rollback();
        return {};
    }

    // Look for open parenthesis
    if(auto lopenpar = _lexer.get(); lopenpar!=lex::punctuator::PARENTHESIS_OPEN) {
        holder.rollback();
        return {};
    }

    // Look for parameter_spec declarations
    std::vector<std::shared_ptr<ast::parameter_spec>> params;
    auto lex = _lexer.get();
    if(!lex) {
        throw_error(0x0009, _lexer.pick(), "Function declaration expects finalizing its declaration");
    }
    if(lex!=lex::punctuator::PARENTHESIS_CLOSE) {
        _lexer.unget();
        holder.sync();
        // Look for first parameter_spec
        auto param = parse_parameter_spec();
        if(param) {
            params.push_back(param);
        } else {
            throw_error(0x000A, _lexer.pick(), "Function declaration expects a first parameter declaration");
        }

        while(true) {
            lex = _lexer.get();
            if(!lex) {
                throw_error(0x000B, _lexer.pick(), "Function declaration expects finalizing its declaration");
            }
            if(lex==lex::punctuator::PARENTHESIS_CLOSE) {
                break;
            }
            if(lex!=lex::punctuator::COMMA){
                throw_error(0x000C, _lexer.pick(), "Function declaration expects a closing parenthesis ')' for finalizing its prototype or a comma ',' to specify another parameter");
            }

            // Look for next parameter_spec
            auto param = parse_parameter_spec();
            if(param) {
                params.push_back(param);
            } else {
                throw_error(0x000D, _lexer.pick(), "Function declaration expects a parameter specification");
            }
        }
    }

    // Look for return type
    std::shared_ptr<ast::type_specifier> restype;
    holder.sync();
    if(auto lcolon = _lexer.get(); lcolon==lex::operator_::COLON) {
        restype = parse_type_spec();
        if(!restype) {
            throw_error(0x000E, _lexer.pick(), "Function declaration expects a return type specifier after the colon ':'");
        }
    } else {
        holder.rollback();
    }

    auto statements = parse_statement_block();
    if(statements) {
        return std::make_shared<ast::function_decl>(specifiers, lex::as<lex::identifier>(lname), restype, params, statements);
    } else
    // Look for final semicolon
    // TODO remove function declaration-only.
    if(auto lsemicolon = _lexer.get(); lsemicolon!=lex::punctuator::SEMICOLON) {
        throw_error(0x000F, _lexer.pick(), "Function declaration expects a final semicolon ';'");
    }
    return std::make_shared<ast::function_decl>(specifiers, lex::as<lex::identifier>(lname), restype, params);
}

std::shared_ptr<ast::parameter_spec> parser::parse_parameter_spec()
{
    lex::lex_holder holder(_lexer);

    std::vector<lex::keyword> specifiers = parse_specifiers();

    std::optional<lex::identifier> name;
    lex::lex_holder holder_name(_lexer);
    if(auto lname = _lexer.get(); lex::is<lex::identifier>(lname)) {
        if(auto lcolon = _lexer.get(); lcolon==lex::operator_::COLON) {
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

    return std::make_shared<ast::parameter_spec>(specifiers, name, type);
}

std::shared_ptr<ast::block_statement> parser::parse_statement_block()
{
    lex::lex_holder holder(_lexer);

    // Look for open brace
    std::optional<lex::punctuator> open_brace;
    if(auto lopenbrace = _lexer.get(); lopenbrace==lex::punctuator::BRACE_OPEN) {
        open_brace = lex::as<lex::punctuator>(lopenbrace);
    } else {
        holder.rollback();
        return {};
        // Err: statement block requires a opening brace.
        //throw parsing_error("Closing brace for statement block is missing" /*, *lopenbrace */);
    }

    std::vector<std::shared_ptr<ast::statement>> statements;
    while(auto statement = parse_statement()) {
        if(statement) {
            statements.push_back(statement);
        }
    }

    // Look for closing brace
    std::optional<lex::punctuator> close_brace;
    if(auto lclosebrace = _lexer.get(); lclosebrace == lex::punctuator::BRACE_CLOSE) {
        close_brace = lex::as<lex::punctuator>(lclosebrace);
    } else {
        throw_error(0x0010, _lexer.pick(), "Block is expecting a closing brace '}'");
    }

    return std::make_shared<ast::block_statement>(*open_brace, *close_brace, statements);
}

std::shared_ptr<ast::return_statement> parser::parse_return_statement()
{
    lex::lex_holder holder(_lexer);

    std::optional<lex::keyword> ret;
    if(auto lreturn = _lexer.get(); lreturn==lex::keyword::RETURN) {
        ret = lex::as<lex::keyword>(lreturn);
    } else {
        holder.rollback();
        return {};
    }

    ast::expr_ptr expr = parse_expression();

    auto lsemicolon = _lexer.get();
    if(lsemicolon!=lex::punctuator::SEMICOLON) {
        throw_error(0x0011, _lexer.pick(), "Return statement is expecting to finish by a semicolon ';'");
    }

    return std::make_shared<ast::return_statement>(*ret, expr);

}

std::shared_ptr<ast::if_else_statement> parser::parse_if_else_statement() {
    lex::lex_holder holder(_lexer);

    auto lif = _lexer.get();
    if(lif != lex::keyword::IF) {
        holder.rollback();
        return {};
    }

    auto lpopen = _lexer.get();
    if(lpopen != lex::punctuator::PARENTHESIS_OPEN) {
        throw_error(0x002C, lpopen, "If statement expect an open parenthesis '(' after the 'if' keyword for the tested expression");
    }

    auto test_expr = parse_expression();
    if(!test_expr) {
        throw_error(0x002D, _lexer.pick(), "If statement expect an expression after the open parenthesis '('");
    }

    auto lpclose = _lexer.get();
    if(lpclose != lex::punctuator::PARENTHESIS_CLOSE) {
        throw_error(0x002E, lpclose, "If statement expect a close parenthesis ')' after the tested expression");
    }

    auto then_stmt = parse_statement();
    if(!then_stmt) {
        throw_error(0x002F, lpclose, "If statement expect a statement after the close parenthesis ')'");
    }

    holder.sync();

    auto lelse = _lexer.get();
    if(lelse == lex::keyword::ELSE) {
        auto else_stmt = parse_statement();
        if(!then_stmt) {
            throw_error(0x0030, lelse, "If statement expect a statement after the 'else' keyword");
        }

        return std::make_shared<ast::if_else_statement>(
                    lex::as<lex::keyword>(lif),
                    lex::as<lex::keyword>(lelse),
                    test_expr,
                    then_stmt,
                    else_stmt
                );
    } else {
        holder.rollback();
        return std::make_shared<ast::if_else_statement>(
                lex::as<lex::keyword>(lif),
                test_expr,
                then_stmt
        );
    }
}

std::shared_ptr<ast::while_statement> parser::parse_while_statement() {
    lex::lex_holder holder(_lexer);

    auto lwhile = _lexer.get();
    if(lwhile != lex::keyword::WHILE) {
        holder.rollback();
        return {};
    }

    auto lpopen = _lexer.get();
    if(lpopen != lex::punctuator::PARENTHESIS_OPEN) {
        throw_error(0x0031, lpopen, "While statement expect an open parenthesis '(' after the 'while' keyword for the tested expression");
    }

    auto test_expr = parse_expression();
    if(!test_expr) {
        throw_error(0x0032, _lexer.pick(), "While statement expect an expression after the open parenthesis '('");
    }

    auto lpclose = _lexer.get();
    if(lpclose != lex::punctuator::PARENTHESIS_CLOSE) {
        throw_error(0x0033, lpclose, "While statement expect a close parenthesis ')' after the tested expression");
    }

    auto nested_stmt = parse_statement();
    if(!nested_stmt) {
        throw_error(0x0034, lpclose, "While statement expect a statement after the close parenthesis ')'");
    }

    return std::make_shared<ast::while_statement>(
            lex::as<lex::keyword>(lwhile),
            test_expr,
            nested_stmt
    );
}

std::shared_ptr<ast::for_statement> parser::parse_for_statement()
{
    lex::lex_holder holder(_lexer);

    auto lfor = _lexer.get();
    if(lfor != lex::keyword::FOR) {
        holder.rollback();
        return {};
    }

    auto lpopen = _lexer.get();
    if(lpopen != lex::punctuator::PARENTHESIS_OPEN) {
        throw_error(0x0035, lpopen, "For statement expect an open parenthesis '(' after the 'for' keyword");
    }

    std::optional<lex::punctuator> first_semicolon_kw;
    std::shared_ptr<ast::variable_decl> decl_stmt;
    if(auto decl = parse_variable_decl()) {
        decl_stmt = decl;
        // TODO Add semicolon ref
    } else if(auto lsemicolon = _lexer.get(); lsemicolon == lex::punctuator::SEMICOLON) {
        first_semicolon_kw = lex::as<lex::punctuator>(lsemicolon);
    } else {
        throw_error(0x0036, lpopen, "For statement expect a variable declaration or a semicolon ';' after the open parenthesis'('");
    }

    std::optional<lex::punctuator> second_semicolon_kw;
    std::shared_ptr<ast::expression> test_expr;
    if(auto expr = parse_expression_statement()) {
        test_expr = expr->expr;
        // TODO Add semicolon ref
    } else if(auto lsemicolon = _lexer.get(); lsemicolon == lex::punctuator::SEMICOLON) {
        second_semicolon_kw = lex::as<lex::punctuator>(lsemicolon);
    } else {
        throw_error(0x0037, lpopen, "For statement expect an expression or a semicolon ';' after the first semicolon ';'");
    }

    std::shared_ptr<ast::expression> step_expr;
    if(auto expr = parse_expression()) {
        step_expr = expr;
    }

    auto lpclose = _lexer.get();
    if(lpclose != lex::punctuator::PARENTHESIS_CLOSE) {
        throw_error(0x0038, lpclose, "For statement expect a closing parenthesis ')' after the optional step expression");
    }

    auto nested_stmt = parse_statement();
    if(!nested_stmt) {
        throw_error(0x0039, lpclose, "For statement expect a statement after the close parenthesis ')'");
    }

    return std::make_shared<ast::for_statement>(
            lex::as<lex::keyword>(lfor),
            *first_semicolon_kw,
            *second_semicolon_kw,
            decl_stmt,
            test_expr,
            step_expr,
            nested_stmt
    );
}

std::shared_ptr<ast::statement> parser::parse_statement()
{
    if(auto block = parse_statement_block()) {
        return block;
    }

    if(auto ret = parse_return_statement()) {
        return ret;
    }

    if(auto if_else = parse_if_else_statement()) {
        return if_else;
    }

    if(auto while_stmt = parse_while_statement()) {
        return while_stmt;
    }

    if(auto for_stmt = parse_for_statement()) {
        return for_stmt;
    }

    if(auto var = parse_variable_decl()) {
        return var;
    }

    if(auto expr = parse_expression_statement()) {
        return expr;
    }

    return {};
}


std::shared_ptr<ast::variable_decl> parser::parse_variable_decl()
{
    lex::lex_holder holder(_lexer);

    std::vector<lex::keyword> specifiers = parse_specifiers();

    // Expect a name:
    auto lname = _lexer.get();
    if(lex::is_not<lex::identifier>(lname)) {
        holder.rollback();
        return {};
        // Err: variable declaration requires at least and identifier.
    }

    // Look for the type specifier
    auto lcolon = _lexer.get();
    if(lcolon!=lex::operator_::COLON) {
        // Err: variable declaration requires at least and identifier and a colon.
        // Err: variable declaration requires a type specifier prefixed by colon.
       holder.rollback();
        return {};
    }
    std::shared_ptr<ast::type_specifier> type = parse_type_spec();
    if(!type) {
        throw_error(0x0012, _lexer.pick(), "Variable declaration expects a type specifier after the semicolon ';'");
    }

    ast::expr_ptr expr;
    auto lequal = _lexer.get();
    if(lequal==lex::operator_::EQUAL) {
        expr = parse_conditional_expr();
        if(!expr) {
            throw_error(0x0013, _lexer.pick(), "Variable declaration expects an initialization exppression after the equal operator '='");
        }
    } else {
        _lexer.unget();
    }

    auto lsemicolon = _lexer.get();
    if(lsemicolon!=lex::punctuator::SEMICOLON) {
        throw_error(0x0014, _lexer.pick(), "Variable declaration expects to finish by a semicolon ';'");
    }

    return std::make_shared<ast::variable_decl>(specifiers, lex::as<lex::identifier>(lname), type, expr);
}

std::shared_ptr<ast::type_specifier> parser::parse_type_spec()
{
    std::shared_ptr<ast::type_specifier> res;

    res = parse_fundamental_type_spec();

    lex::lex_holder holder(_lexer);
    if(!res) {
        // Expect a type qualified identifier:
        std::shared_ptr<ast::qualified_identifier> qid = parse_qualified_identifier();
        if(qid) {
            res = std::make_shared<ast::identified_type_specifier>(*qid);
        } else {
            holder.rollback();
            return {};
        }
    }

    while(true) {
        holder.sync();
        auto lex = _lexer.get();

        if(lex == lex::operator_::STAR || lex == lex::operator_::AMPERSAND) {
            res = std::make_shared<ast::pointer_type_specifier>(res, lex::as<lex::operator_>(lex));
            continue;
        }

        if(lex == lex::punctuator::BRACKET_OPEN) {

            auto lint = _lexer.get();
            std::optional<lex::integer> int_index;
            if (lex::is<lex::integer>(lint)) {
                int_index = lex::as<lex::integer>(lint);
            } else {
                _lexer.unget();
            }

            auto lbrclose = _lexer.get();
            if (lbrclose != lex::punctuator::BRACKET_CLOSE) {
                throw_error(0x003A, lbrclose, "Type specifier array index expect a closing bracket");
            }

            res = std::make_shared<ast::array_type_specifier>(res, lex::as<lex::punctuator>(lex), lex::as<lex::punctuator>(lbrclose), int_index);
            continue;
        }

        holder.rollback();
        break;
    }

    return res;
}

std::shared_ptr<ast::type_specifier> parser::parse_fundamental_type_spec() {
    lex::lex_holder holder(_lexer);

    // Look for type prefix
    bool is_unsigned = false;
    auto lprefix = _lexer.get();
    if(lprefix == lex::keyword::UNSIGNED) {
        is_unsigned = true;
    } else {
        _lexer.unget();
    }

    // Expect a type keyword
    auto ltype = _lexer.get();
    if(lex::is_one_of<
            lex::keyword::BOOL,
            lex::keyword::BYTE,
            lex::keyword::CHAR,
            lex::keyword::SHORT,
            lex::keyword::INT,
            lex::keyword::LONG,
            lex::keyword::FLOAT,
            lex::keyword::DOUBLE>(ltype)){
        return std::make_shared<ast::keyword_type_specifier>( std::get<lex::keyword>(ltype.value().get()) , is_unsigned);
    }
    holder.rollback();
    return {};
}


std::shared_ptr<ast::expression_statement> parser::parse_expression_statement()
{
    ast::expr_ptr expr = parse_expression();
    if(!expr) {
        return {};
    }

    auto lsemicolon = _lexer.get();
    if(lsemicolon!=lex::punctuator::SEMICOLON) {
        throw_error(0x0015, _lexer.pick(), "Expression statement expects to finish by a semicolon ';'");
    }

    return std::make_shared<ast::expression_statement>(expr);
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
        if (lcomma != lex::punctuator::COMMA) {
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
            throw_error(0x0016, _lexer.pick(), "Expression list is expecting a sub expression after a comma ','");
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
    if(lex::is_none_of<lex::operator_::EQUAL,
          lex::operator_::STAR_EQUAL,
          lex::operator_::SLASH_EQUAL,
          lex::operator_::PERCENT_EQUAL,
          lex::operator_::PLUS_EQUAL,
          lex::operator_::MINUS_EQUAL,
          lex::operator_::DOUBLE_CHEVRON_OPEN_EQUAL,
          lex::operator_::DOUBLE_CHEVRON_CLOSE_EQUAL,
          lex::operator_::AMPERSAND_EQUAL,
          lex::operator_::CARET_EQUAL,
          lex::operator_::PIPE_EQUAL>(lop))
    {
        _lexer.unget();
        return cond;
    }

    ast::expr_ptr other = parse_assignment_expression();
    if(other) {
        return std::make_shared<ast::binary_operator_expr>(lex::as<lex::operator_>(lop), cond, other);
    } else {
        throw_error(0x0017, _lexer.pick(), "Assignment expression is expecting a sub expression after a the asssignmment operator");
    }
}

ast::expr_ptr parser::parse_conditional_expr() {
    ast::expr_ptr left = parse_logical_or_expression();
    if (!left) {
        return {};
    }

    auto lqm = _lexer.get();
    if (lqm != lex::operator_::QUESTION_MARK) {
        _lexer.unget();
        return left;
    }

    ast::expr_ptr middle = parse_logical_or_expression();
    if(!middle) {
        throw_error(0x0018, _lexer.pick(), "Conditional expression is expecting a sub expression after a the question-mark '?' operator");
    }

    auto lcolon = _lexer.get();
    if (lqm != lex::operator_::COLON) {
        throw_error(0x0019, _lexer.pick(), "Conditional expression is expecting a colon ':' operator after the first sub expression");
        // Err: conditional expression requires a colon after sub expression.
        throw parsing_error("Colon of conditional expression is missing" /*, *lcolon */);
    }

    ast::expr_ptr right = parse_logical_or_expression();
    if(!right) {
        throw_error(0x001A, _lexer.pick(), "Conditional expression is expecting a sub expression after the colon ':' operator");
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
    if (op != lex::operator_::DOUBLE_PIPE) {
        _lexer.unget();
        return left_expr;
    }

    ast::expr_ptr right_expr = parse_logical_or_expression();
    if(right_expr) {
        return std::make_shared<ast::binary_operator_expr>(lex::as<lex::operator_>(op), left_expr, right_expr);
    } else {
        throw_error(0x001B, _lexer.pick(), "Logical-OR expression is expecting a sub expression after the double-pipe '||' operator");
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
    if (op != lex::operator_::DOUBLE_AMPERSAND) {
        _lexer.unget();
        return left_expr;
    }

    ast::expr_ptr right_expr = parse_logical_and_expression();
    if(right_expr) {
        return std::make_shared<ast::binary_operator_expr>(lex::as<lex::operator_>(op), left_expr, right_expr);
    } else {
        throw_error(0x001C, _lexer.pick(), "Logical-AND expression is expecting a sub expression after the double-ampersand '&&' operator");
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
    if (op != lex::operator_::PIPE) {
        _lexer.unget();
        return left_expr;
    }

    ast::expr_ptr right_expr = parse_inclusive_bin_or_expr();
    if(right_expr) {
        return std::make_shared<ast::binary_operator_expr>(lex::as<lex::operator_>(op), left_expr, right_expr);
    } else {
        throw_error(0x001D, _lexer.pick(), "Binary-OR expression is expecting a sub expression after the pipe '|' operator");
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
    if (op != lex::operator_::CARET) {
        _lexer.unget();
        return left_expr;
    }

    ast::expr_ptr right_expr = parse_exclusive_bin_or_expr();
    if(right_expr) {
        return std::make_shared<ast::binary_operator_expr>(lex::as<lex::operator_>(op), left_expr, right_expr);
    } else {
        throw_error(0x001E, _lexer.pick(), "Binary-XOR expression is expecting a sub expression after the caret '^' operator");
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
    if (op != lex::operator_::AMPERSAND) {
        _lexer.unget();
        return left_expr;
    }

    ast::expr_ptr right_expr = parse_bin_and_expr();
    if(right_expr) {
        return std::make_shared<ast::binary_operator_expr>(lex::as<lex::operator_>(op), left_expr, right_expr);
    } else {
        throw_error(0x001F, _lexer.pick(), "Binary-AND expression is expecting a sub expression after the ampersand '&' operator");
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
    if (op != lex::operator_::DOUBLE_EQUAL &&
        op != lex::operator_::EXCLAMATION_MARK_EQUAL) {
        _lexer.unget();
        return left_expr;
    }

    ast::expr_ptr right_expr = parse_equality_expr();
    if(right_expr) {
        return std::make_shared<ast::binary_operator_expr>(lex::as<lex::operator_>(op), left_expr, right_expr);
    } else {
        throw_error(0x0020, _lexer.pick(), "Equality expression is expecting a sub expression after the equality '==' or '!=' operators");
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
    if(lex::is_none_of<
            lex::operator_::CHEVRON_CLOSE,
            lex::operator_::CHEVRON_OPEN,
            lex::operator_::CHEVRON_CLOSE_EQUAL,
            lex::operator_::CHEVRON_OPEN_EQUAL>(op)) {
        _lexer.unget();
        return left_expr;
    }

    ast::expr_ptr right_expr = parse_relational_expr();
    if(right_expr) {
        return std::make_shared<ast::binary_operator_expr>(lex::as<lex::operator_>(op), left_expr, right_expr);
    } else {
        throw_error(0x0021, _lexer.pick(), "Relational expression is expecting a sub expression after the relational '<', '>', '<=' or '>=' operators");
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
    if (op != lex::operator_::DOUBLE_CHEVRON_CLOSE &&
        op != lex::operator_::DOUBLE_CHEVRON_OPEN) {
        _lexer.unget();
        return left_expr;
    }

    ast::expr_ptr right_expr = parse_shifting_expr();
    if(right_expr) {
        return std::make_shared<ast::binary_operator_expr>(lex::as<lex::operator_>(op), left_expr, right_expr);
    } else {
        throw_error(0x0022, _lexer.pick(), "Shifting expression is expecting a sub expression after the shifting '<<' or '>>' operators");
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
    if (op != lex::operator_::PLUS &&
        op != lex::operator_::MINUS) {
        _lexer.unget();
        return left_expr;
    }

    ast::expr_ptr right_expr = parse_additive_expr();
    if(right_expr) {
        return std::make_shared<ast::binary_operator_expr>(lex::as<lex::operator_>(op), left_expr, right_expr);
    } else {
        throw_error(0x0023, _lexer.pick(), "Additive expression is expecting a sub expression after the additive '+' or '-' operators");
    }
}

ast::expr_ptr parser::parse_multiplicative_expr() {
    ast::expr_ptr left_expr;

    if (ast::expr_ptr first = parse_pm_expr()) {
        left_expr = first;
    } else {
        return {};
    }

    auto op = _lexer.get();
    if (lex::is_none_of<
            lex::operator_::STAR,
            lex::operator_::SLASH,
            lex::operator_::PERCENT>(op)) {
        _lexer.unget();
        return left_expr;
    }

    ast::expr_ptr right_expr = parse_multiplicative_expr();
    if (right_expr) {
        return std::make_shared<ast::binary_operator_expr>(lex::as<lex::operator_>(op), left_expr, right_expr);
    } else {
        throw_error(0x0024, _lexer.pick(), "Multiplicative expression is expecting a sub expression after the multiplicative '*', '/' or '%' operators");
    }
}

ast::expr_ptr parser::parse_pm_expr() {
    ast::expr_ptr left_expr;

    if (ast::expr_ptr first = parse_cast_expr()) {
        left_expr = first;
    } else {
        return {};
    }

    auto op = _lexer.get();
    if (op != lex::operator_::DOT_STAR &&
        op != lex::operator_::ARROW_STAR) {
        _lexer.unget();
        return left_expr;
    }

    ast::expr_ptr right_expr = parse_pm_expr();
    if (right_expr) {
        return std::make_shared<ast::binary_operator_expr>(lex::as<lex::operator_>(op), left_expr, right_expr);
    } else {
        throw_error(0x0025, _lexer.pick(),
                    "PM expression is expecting a sub expression after the pm '.*' or '.->' operators");
    }
}

ast::expr_ptr parser::parse_cast_expr()
{
    lex::lex_holder holder(_lexer);

    if(auto lopenpar = _lexer.get(); lopenpar != lex::punctuator::PARENTHESIS_OPEN) {
        holder.rollback();
        return parse_unary_expr();
    }

    std::shared_ptr<ast::type_specifier> type = parse_type_spec();
    if(!type) {
        holder.rollback();
        return parse_unary_expr();
    }

    if(auto lclosepar = _lexer.get(); lclosepar != lex::punctuator::PARENTHESIS_CLOSE) {
        holder.rollback();
        return parse_unary_expr();
    }

    ast::expr_ptr expr = parse_cast_expr();
    if(!expr) {
        holder.rollback();
        return parse_unary_expr();
    }

    return std::make_shared<ast::cast_expr>(type, expr);
}

ast::expr_ptr parser::parse_unary_expr()
{
    lex::lex_holder holder(_lexer);

    if(auto lop = _lexer.get();
            lex::is_one_of<
                lex::operator_::DOUBLE_PLUS,
                lex::operator_::DOUBLE_MINUS,
                lex::operator_::STAR,
                lex::operator_::AMPERSAND,
                lex::operator_::PLUS,
                lex::operator_::MINUS,
                lex::operator_::EXCLAMATION_MARK,
                lex::operator_::TILDE>(lop)
            ) {
        ast::expr_ptr expr = parse_cast_expr();
        if(expr) {
            return std::make_shared<ast::unary_prefix_expr>(lex::as<lex::operator_>(lop), expr);
        } else {
            throw_error(0x0026, _lexer.pick(), "Unary expression is expecting a sub expression after the unary '++', '--', '*', '&', '+', '-', '!' or '~' operators");
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
                throw_error(0x0027, _lexer.pick(), "Bracket postfix expression exppects sub-expression");
            }
            auto lclose = _lexer.get();
            if(lclose != lex::punctuator::BRACKET_CLOSE) {
                throw_error(0x0028, _lexer.pick(), "Bracket postfix expression exppects closing bracket ']' after sub-expression");
            }
            any = std::make_shared<ast::bracket_postifx_expr>(any, expr);
        } else if(lop == lex::punctuator::PARENTHESIS_OPEN) {
            ast::expr_ptr expr = parse_expression_list();
            // expr might be null if expression list is empty
            auto lclose = _lexer.get();
            if(lclose != lex::punctuator::PARENTHESIS_CLOSE) {
                throw_error(0x0029, _lexer.pick(), "Parenthesis postfix expression expects closing parenthesis ')'");
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
            throw_error(0x002A, _lexer.pick(), "Parenthesis expression expects a sub-expression after open-parenthesis '('");
        }
        lex::opt_ref_any_lexeme r = _lexer.get();
        if(r != lex::punctuator::PARENTHESIS_CLOSE) {
            throw_error(0x002B, _lexer.pick(), "Parenthesis expression expects closing parenthesis ')' after sub-expression");
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

    std::shared_ptr<ast::qualified_identifier> ident = parse_qualified_identifier();
    if(ident) {
        return std::make_shared<ast::identifier_expr>(*ident);
    } else {
        holder.rollback();
        return {};
    }
}

} // k::parse_all
