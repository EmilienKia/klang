/*
 * K Language compiler
 *
 * Copyright 2023-2026 Emilien Kia
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
// Note: Last parser log number: 0x1003C
//

#include "parser.hpp"

#include <deque>

#include "../common/logger.hpp"
#include "../errors.hpp"

namespace k::parse {

std::optional<k::name> lookup_module_name(k::source& src, k::log::logger& logger) {
    try {
        parser p(logger);
        p.parse(src);
        auto mod = p.parse_module_declaration();
        if (mod && mod->qname) {
            return mod->qname->to_name();
        }
    } catch (const parsing_error&) {
        // A malformed module declaration — propagate as absent.
    }
    return std::nullopt;
}


//
// Parser
//

parser::parser(k::log::logger& logger):
    logger_relay(logger),
    _lexer(logger)
{}

parser::parser(k::log::logger& logger, k::source& src):
    logger_relay(logger),
    _lexer(logger)
{
    _lexer.parse(src);
}


void parser::parse(k::source& src) {
    _lexer.parse(src);
}

std::shared_ptr<ast::unit> parser::parse_unit()
{
    trace("[parser::parse_unit] begin");
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
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_MISSING_MODULE_NAME), _lexer.pick_current(), "Module name is missing");
    }

    // Expect a semicolon to end module declaration
    if(auto lsemicolon = _lexer.get(); lsemicolon!=lex::punctuator::SEMICOLON) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_MISSING_SEMICOLON_MODULE), _lexer.pick_previous(), "Semicolon is missing after module name at end of module declaration");
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

    trace("[parser::parse_import] parsing import", {});

    // Expect a qualified identifier (e.g. "math::vec" or "foo")
    auto qname = parse_qualified_identifier();
    if(!qname || qname->names.empty()) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_MISSING_IMPORT_NAME), _lexer.pick_current(), "Import module name is missing");
    }

    // Expect a semicolon to end import declaration
    if(auto lsemicolon = _lexer.get(); lsemicolon!=lex::punctuator::SEMICOLON) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_MISSING_SEMICOLON_IMPORT), _lexer.pick_current(), "Semicolon is missing after module name at end of import declaration");
    }

    return std::make_shared<ast::import>(lex::as<lex::keyword>(limport), std::move(qname));
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

    // Look for a using decl
    if(auto decl = parse_using_decl()) {
        return decl;
    }

    // Look for a friend decl
    if(auto decl = parse_friend_decl()) {
        return decl;
    }

    // Look for a struct decl
    if(auto decl = parse_aggregate_decl()) {
        return decl;
    }

    // Look for an enum decl
    if(auto decl = parse_enum_decl()) {
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
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_MISSING_NS_OPEN_BRACE), _lexer.pick_current(), "Namespace open brace is missing");
    }

    std::vector<ast::decl_ptr> declarations = parse_declarations();

    // Expect a closing brace
    if(lex::opt_ref_any_lexeme lclosingbrace= _lexer.get(); lclosingbrace==lex::punctuator::BRACE_CLOSE) {
        close_par = lex::as<lex::punctuator>(lclosingbrace);
    } else {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_MISSING_NS_CLOSE_BRACE), _lexer.pick_current(), "Namespace closing brace is expected");
    }

    return std::make_shared<ast::namespace_decl>(*ns, *open_par, *close_par, name, declarations);
}

std::shared_ptr<ast::using_decl> parser::parse_using_decl()
{
    lex::lex_holder holder(_lexer);

    // Expect the 'using' keyword
    auto lusing = _lexer.get();
    if (lusing != lex::keyword::USING) {
        holder.rollback();
        return {};
    }

    // Optionally consume a type filter keyword: namespace, struct, interface, class
    std::optional<lex::keyword> element_filter;
    auto lfilter = _lexer.get();
    if (lfilter == lex::keyword::NAMESPACE ||
        lfilter == lex::keyword::STRUCT ||
        lfilter == lex::keyword::INTERFACE ||
        lfilter == lex::keyword::CLASS) {
        element_filter = lex::as<lex::keyword>(lfilter);
    } else {
        _lexer.unget();
    }

    // Optionally consume an alias: identifier '='
    // 'using Foo = X::Y::bar;' or 'using M = namespace X::Y;'
    std::optional<lex::identifier> alias_name;
    {
        auto l1 = _lexer.get();
        if (lex::is<lex::identifier>(l1)) {
            auto l2 = _lexer.get();
            if (l2 == lex::operator_::EQUAL) {
                // Confirmed: this is an alias
                alias_name = lex::as<lex::identifier>(l1);
            } else {
                // Not an alias — put both tokens back
                _lexer.unget();
                _lexer.unget();
            }
        } else {
            _lexer.unget();
        }
    }

    // Expect a qualified identifier
    auto qname = parse_qualified_identifier();
    if (!qname || qname->names.empty()) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_USING_EXPECT_QNAME), _lexer.pick_current(), "Using declaration expects a qualified identifier after 'using'");
    }

    // Expect a semicolon
    if (auto lsemicolon = _lexer.get(); lsemicolon != lex::punctuator::SEMICOLON) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_USING_MISSING_SEMICOLON), _lexer.pick_current(), "Semicolon is missing at end of using declaration");
    }

    return std::make_shared<ast::using_decl>(
            lex::as<lex::keyword>(lusing),
            element_filter,
            alias_name,
            std::move(qname));
}

std::shared_ptr<ast::friend_decl> parser::parse_friend_decl()
{
    lex::lex_holder holder(_lexer);

    // Expect the 'friend' keyword
    auto lfriend = _lexer.get();
    if (lfriend != lex::keyword::FRIEND) {
        holder.rollback();
        return {};
    }

    // Optionally consume a type filter keyword: struct, interface, class
    std::optional<lex::keyword> element_filter;
    auto lfilter = _lexer.get();
    if (lfilter == lex::keyword::STRUCT ||
        lfilter == lex::keyword::INTERFACE ||
        lfilter == lex::keyword::CLASS) {
        element_filter = lex::as<lex::keyword>(lfilter);
    } else {
        _lexer.unget();
    }

    // Expect a qualified identifier
    auto qname = parse_qualified_identifier();
    if (!qname || qname->names.empty()) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_FRIEND_EXPECT_QNAME), _lexer.pick_current(), "Friend declaration expects a qualified identifier after 'friend'");
    }

    // Expect a semicolon
    if (auto lsemicolon = _lexer.get(); lsemicolon != lex::punctuator::SEMICOLON) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_FRIEND_MISSING_SEMICOLON), _lexer.pick_current(), "Semicolon is missing at end of friend declaration");
    }

    return std::make_shared<ast::friend_decl>(
            lex::as<lex::keyword>(lfriend),
            element_filter,
            std::move(qname));
}

std::shared_ptr<ast::annotation_def> parser::parse_annotation_def()
{
    lex::lex_holder holder(_lexer);

    // Expect '@' punctuator
    auto lat = _lexer.get();
    if (lat != lex::punctuator::AT_SIGN) {
        holder.rollback();
        return {};
    }
    auto at_sign = lex::as<lex::punctuator>(lat);

    // Expect a qualified identifier (annotation type name)
    auto qname = parse_qualified_identifier();
    if (!qname || qname->names.empty()) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_ANNOTATION_EXPECT_NAME), _lexer.pick_current(), "Expected annotation type name after '@'");
    }

    // Optional initialization: '(' expr_list ')' or brace_init_list
    auto peek = _lexer.get();

    if (peek == lex::punctuator::PARENTHESIS_OPEN) {
        // Constructor-style initialization: @Name( [args] )
        std::vector<ast::expr_ptr> args;

        // Check for immediate closing paren (empty args)
        auto peek_close = _lexer.get();
        if (peek_close == lex::punctuator::PARENTHESIS_CLOSE) {
            // @Name() — empty args
            return std::make_shared<ast::annotation_def>(at_sign, std::move(qname), args);
        }
        _lexer.unget();

        // Parse expression list
        auto expr = parse_expression_list();
        if (expr) {
            // If it's a comma expr_list, flatten into args
            if (auto expr_list = std::dynamic_pointer_cast<ast::expr_list_expr>(expr)) {
                for (size_t i = 0; i < expr_list->size(); ++i) {
                    args.push_back((*expr_list)[i]);
                }
            } else {
                args.push_back(expr);
            }
        }

        // Expect closing parenthesis
        if (auto lclose = _lexer.get(); lclose != lex::punctuator::PARENTHESIS_CLOSE) {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_ANNOTATION_EXPECT_CLOSE_PAREN), _lexer.pick_current(), "Expected ')' after annotation arguments");
        }
        return std::make_shared<ast::annotation_def>(at_sign, std::move(qname), args);

    } else if (peek == lex::punctuator::BRACE_OPEN) {
        // Brace initialization: @Name{ ... }
        auto open_brace = lex::as<lex::punctuator>(peek);
        auto brace_init = parse_brace_init_list(open_brace);
        return std::make_shared<ast::annotation_def>(at_sign, std::move(qname), std::move(brace_init));

    } else {
        // No initializer — default initialization
        _lexer.unget();
        return std::make_shared<ast::annotation_def>(at_sign, std::move(qname));
    }
}

ast::annotation_def_list parser::parse_annotation_defs()
{
    ast::annotation_def_list annotations;
    while (auto ann = parse_annotation_def()) {
        annotations.push_back(std::move(ann));
    }
    return annotations;
}

ast::template_param_list parser::parse_template_declaration(const char** out_template_kw_start)
{
    lex::lex_holder holder(_lexer);

    // Check for 'template' keyword
    auto ltemplate = _lexer.get();
    if (ltemplate != lex::keyword::TEMPLATE) {
        holder.rollback();
        if (out_template_kw_start) *out_template_kw_start = nullptr;
        return {};
    }

    // Save the start of the 'template' keyword in the source buffer
    if (out_template_kw_start) {
        *out_template_kw_start = lex::as_lexeme(ltemplate).content.data();
    }

    // Expect '<'
    auto lopen = _lexer.get();
    if (lopen != lex::operator_::CHEVRON_OPEN) {
        throw_error(0x10040, _lexer.pick_current(), "Expected '<' after 'template' keyword");
    }

    // Parse template parameters
    ast::template_param_list params;
    auto first_param = parse_template_parameter();
    if (!first_param) {
        throw_error(0x10041, _lexer.pick_current(), "Expected at least one template parameter");
    }
    params.push_back(std::move(first_param));

    while (true) {
        lex::lex_holder comma_holder(_lexer);
        auto maybe_comma = _lexer.get();
        if (maybe_comma == lex::punctuator::COMMA) {
            auto param = parse_template_parameter();
            if (!param) {
                throw_error(0x10042, _lexer.pick_current(), "Expected template parameter after ','");
            }
            params.push_back(std::move(param));
        } else {
            comma_holder.rollback();
            break;
        }
    }

    // Expect '>'
    // Handle '>>' by splitting: consume '>' and replace the current lexeme with '>'
    auto lclose = _lexer.get();
    if (lclose == lex::operator_::DOUBLE_CHEVRON_CLOSE) {
        // Split '>>' into '>' + '>': replace the consumed '>>' with a single '>'
        // so the outer context can consume the remaining '>'
        auto last_mut = _lexer.pick_last_mutable();
        if (last_mut) {
            auto& lex_ref = last_mut->get();
            auto& op = std::get<lex::operator_>(lex_ref);
            // Rewrite to single '>'
            lex_ref = lex::operator_(op.content.substr(0, 1), lex::operator_::CHEVRON_CLOSE);
        }
        _lexer.unget(); // unget so the '>' can be consumed by the caller
    } else if (lclose != lex::operator_::CHEVRON_CLOSE) {
        throw_error(0x10043, _lexer.pick_current(), "Expected '>' to close template parameter list");
    }

    return params;
}

ast::template_param_list parser::parse_generic_declaration(bool* out_is_generic)
{
    lex::lex_holder holder(_lexer);

    if (out_is_generic) *out_is_generic = false;

    // Check for 'generic' keyword
    auto lgeneric = _lexer.get();
    if (lgeneric != lex::keyword::GENERIC) {
        holder.rollback();
        return {};
    }

    if (out_is_generic) *out_is_generic = true;

    // Expect '<'
    auto lopen = _lexer.get();
    if (lopen != lex::operator_::CHEVRON_OPEN) {
        throw_error(0x10050, _lexer.pick_current(), "Expected '<' after 'generic' keyword");
    }

    // Parse type parameters (value parameters are forbidden)
    ast::template_param_list params;
    auto first_param = parse_template_parameter();
    if (!first_param) {
        throw_error(0x10051, _lexer.pick_current(), "Expected at least one generic type parameter");
    }
    if (first_param->is_value_param()) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_GENERIC_VALUE_PARAM_NOT_ALLOWED),
                    _lexer.pick_current(),
                    "Value parameters are not allowed in 'generic' declarations; only type parameters (typename, class, struct, interface) are permitted");
    }
    params.push_back(std::move(first_param));

    while (true) {
        lex::lex_holder comma_holder(_lexer);
        auto maybe_comma = _lexer.get();
        if (maybe_comma == lex::punctuator::COMMA) {
            auto param = parse_template_parameter();
            if (!param) {
                throw_error(0x10052, _lexer.pick_current(), "Expected generic type parameter after ','");
            }
            if (param->is_value_param()) {
                throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_GENERIC_VALUE_PARAM_NOT_ALLOWED),
                            _lexer.pick_current(),
                            "Value parameters are not allowed in 'generic' declarations; only type parameters (typename, class, struct, interface) are permitted");
            }
            params.push_back(std::move(param));
        } else {
            comma_holder.rollback();
            break;
        }
    }

    // Expect '>' (handle '>>' by splitting)
    auto lclose = _lexer.get();
    if (lclose == lex::operator_::DOUBLE_CHEVRON_CLOSE) {
        auto last_mut = _lexer.pick_last_mutable();
        if (last_mut) {
            auto& lex_ref = last_mut->get();
            auto& op = std::get<lex::operator_>(lex_ref);
            lex_ref = lex::operator_(op.content.substr(0, 1), lex::operator_::CHEVRON_CLOSE);
        }
        _lexer.unget();
    } else if (lclose != lex::operator_::CHEVRON_CLOSE) {
        throw_error(0x10053, _lexer.pick_current(), "Expected '>' to close generic parameter list");
    }

    return params;
}

std::shared_ptr<ast::template_parameter> parser::parse_template_parameter()
{
    lex::lex_holder holder(_lexer);

    auto lkind = _lexer.get();
    if (!lkind) {
        holder.rollback();
        return {};
    }

    // Type parameter: typename, struct, class, interface
    if (lkind == lex::keyword::TYPENAME || lkind == lex::keyword::STRUCT
        || lkind == lex::keyword::CLASS || lkind == lex::keyword::INTERFACE) {
        auto kind_kw = lex::as<lex::keyword>(lkind);

        // Check for '...' (parameter pack)
        bool is_pack = false;
        {
            lex::lex_holder ellipsis_holder(_lexer);
            auto maybe_ellipsis = _lexer.get();
            if (maybe_ellipsis == lex::punctuator::ELLIPSIS) {
                is_pack = true;
            } else {
                ellipsis_holder.rollback();
            }
        }

        // Expect parameter name
        auto lname = _lexer.get();
        if (lex::is_not<lex::identifier>(lname)) {
            throw_error(0x10044, _lexer.pick_current(), "Expected template parameter name");
        }
        auto param_name = lex::as<lex::identifier>(lname);

        // Pack parameters cannot have constraints or defaults
        if (is_pack) {
            lex::lex_holder check_holder(_lexer);
            auto next = _lexer.get();
            if (next == lex::operator_::COLON) {
                throw_error(0x10047, _lexer.pick_current(),
                    "Parameter pack '{}' cannot have a type constraint",
                    {std::string{param_name.content}});
            } else if (next == lex::operator_::EQUAL) {
                throw_error(0x10048, _lexer.pick_current(),
                    "Parameter pack '{}' cannot have a default type",
                    {std::string{param_name.content}});
            } else {
                check_holder.rollback();
            }
            return std::make_shared<ast::template_parameter>(kind_kw, param_name, nullptr, nullptr, true);
        }

        // Optional constraint: ':' TypeSpec
        std::shared_ptr<ast::type_specifier> constraint;
        {
            lex::lex_holder colon_holder(_lexer);
            auto maybe_colon = _lexer.get();
            if (maybe_colon == lex::operator_::COLON) {
                constraint = parse_type_spec();
                if (!constraint) {
                    throw_error(0x10045, _lexer.pick_current(), "Expected type specifier after ':' in template parameter constraint");
                }
            } else {
                colon_holder.rollback();
            }
        }

        // Optional default: '=' TypeSpec (for type parameters, the default is a type)
        std::shared_ptr<ast::type_specifier> default_type_spec;
        {
            lex::lex_holder eq_holder(_lexer);
            auto maybe_eq = _lexer.get();
            if (maybe_eq == lex::operator_::EQUAL) {
                default_type_spec = parse_type_spec();
                if (!default_type_spec) {
                    throw_error(0x10046, _lexer.pick_current(), "Expected type specifier after '=' in template parameter default");
                }
            } else {
                eq_holder.rollback();
            }
        }

        return std::make_shared<ast::template_parameter>(kind_kw, param_name, std::move(constraint), std::move(default_type_spec));
    }

    // Value parameter: the kind token was actually a type specifier
    // Roll back and try to parse as type specifier
    holder.rollback();
    auto value_type = parse_type_spec();
    if (!value_type) {
        return {};
    }

    // Expect parameter name
    auto lname = _lexer.get();
    if (lex::is_not<lex::identifier>(lname)) {
        holder.rollback();
        return {};
    }
    auto param_name = lex::as<lex::identifier>(lname);

    // Optional default: '=' ConditionalExpr
    // Note: use primary_expr to avoid consuming '>' or '>>' as relational ops
    ast::expr_ptr default_expr;
    {
        lex::lex_holder eq_holder(_lexer);
        auto maybe_eq = _lexer.get();
        if (maybe_eq == lex::operator_::EQUAL) {
            default_expr = parse_primary_expr();
            if (!default_expr) {
                throw_error(0x10047, _lexer.pick_current(), "Expected expression after '=' in template value parameter default");
            }
        } else {
            eq_holder.rollback();
        }
    }

    return std::make_shared<ast::template_parameter>(std::move(value_type), param_name, std::move(default_expr));
}

ast::template_arg_list parser::parse_template_arg_list(bool* was_explicit)
{
    lex::lex_holder holder(_lexer);

    // Check for '<'
    auto lopen = _lexer.get();
    if (lopen != lex::operator_::CHEVRON_OPEN) {
        holder.rollback();
        if (was_explicit) *was_explicit = false;
        return {};
    }

    // Tentative parse: try to parse template arguments.
    // If we fail, roll back and treat '<' as comparison.
    size_t save_pos = _lexer.tell();

    ast::template_arg_list args;
    int angle_depth = 1;

    try {
        // Handle empty template arg list: <> (for types with all-default params)
        {
            lex::lex_holder empty_holder(_lexer);
            auto maybe_close = _lexer.get();
            if (maybe_close == lex::operator_::CHEVRON_CLOSE) {
                // Empty arg list <> — return empty vector (signals "explicit template args")
                if (was_explicit) *was_explicit = true;
                return args;
            }
            empty_holder.rollback();
        }

        // Try to parse the first argument as a type specifier
        auto type_spec = parse_type_spec();
        if (type_spec) {
            args.push_back(std::make_shared<ast::template_arg>(std::move(type_spec)));
        } else {
            // Try as a value expression.
            // Use parse_primary_expr() to avoid consuming '>' or ',' as
            // binary operators — template value args are restricted to simple
            // expressions (literals, identifiers, parenthesised expressions).
            auto expr = parse_primary_expr();
            if (expr) {
                args.push_back(std::make_shared<ast::template_arg>(std::move(expr)));
            } else {
                // Failed — rollback
                _lexer.seek(save_pos);
                holder.rollback();
                return {};
            }
        }

        // Parse remaining arguments
        while (true) {
            lex::lex_holder comma_holder(_lexer);
            auto maybe_comma = _lexer.get();
            if (maybe_comma == lex::punctuator::COMMA) {
                auto type_spec2 = parse_type_spec();
                if (type_spec2) {
                    args.push_back(std::make_shared<ast::template_arg>(std::move(type_spec2)));
                } else {
                    auto expr2 = parse_primary_expr();
                    if (expr2) {
                        args.push_back(std::make_shared<ast::template_arg>(std::move(expr2)));
                    } else {
                        // Failed — rollback everything
                        _lexer.seek(save_pos);
                        holder.rollback();
                        return {};
                    }
                }
            } else {
                comma_holder.rollback();
                break;
            }
        }

        // Expect '>'
        // Handle '>>' by splitting
        auto lclose = _lexer.get();
        if (lclose == lex::operator_::DOUBLE_CHEVRON_CLOSE) {
            // Split '>>' into '>' + '>'
            auto last_mut = _lexer.pick_last_mutable();
            if (last_mut) {
                auto& lex_ref = last_mut->get();
                auto& op = std::get<lex::operator_>(lex_ref);
                lex_ref = lex::operator_(op.content.substr(0, 1), lex::operator_::CHEVRON_CLOSE);
            }
            _lexer.unget();
        } else if (lclose != lex::operator_::CHEVRON_CLOSE) {
            // Not a valid template arg list — rollback
            _lexer.seek(save_pos);
            holder.rollback();
            return {};
        }
    } catch (const parsing_error&) {
        // Parse failed — treat '<' as comparison operator
        _lexer.seek(save_pos);
        holder.rollback();
        if (was_explicit) *was_explicit = false;
        return {};
    }

    if (was_explicit) *was_explicit = true;
    return args;
}

std::shared_ptr<ast::aggregate_decl> parser::parse_aggregate_decl()
{
    lex::lex_holder holder(_lexer);

    // Parse leading annotation definitions
    ast::annotation_def_list annotations = parse_annotation_defs();

    // Parse optional generic or template declaration
    const char* tpl_kw_start = nullptr;
    bool is_generic = false;
    ast::template_param_list template_params = parse_generic_declaration(&is_generic);
    if (template_params.empty() && !is_generic) {
        template_params = parse_template_declaration(&tpl_kw_start);
    }

    std::vector<lex::keyword> specifiers = parse_specifiers();

    std::optional<lex::keyword> st;
    std::optional<lex::punctuator> open_brace, close_brace;

    // Accept "struct", "class", "interface", "annotation" or "union" keyword
    if(lex::opt_ref_any_lexeme lstruct = _lexer.get(); lstruct==lex::keyword::STRUCT || lstruct==lex::keyword::CLASS || lstruct==lex::keyword::INTERFACE || lstruct==lex::keyword::ANNOTATION || lstruct==lex::keyword::UNION) {
        st = lex::as<lex::keyword>(lstruct);
    } else {
        holder.rollback();
        return {};
    }

    // Expect a name:
    auto lname= _lexer.get();
    if(lex::is_not<lex::identifier>(lname)) {
        holder.rollback();
        return {};
    }

    trace("[parser::parse_aggregate_decl] parsing aggregate '{}'", {std::string{lex::as<lex::identifier>(lname).content}});

    // Optional base-class clause: ':' [vis] Name [',' [vis] Name]*
    std::vector<ast::aggregate_decl::base_clause_entry> bases;
    {
        // ...existing code...
        lex::lex_holder base_holder(_lexer);
        auto maybe_colon = _lexer.get();
        if (maybe_colon == lex::operator_::COLON) {
            // Parse list of base class specs
            while (true) {
                std::optional<lex::keyword> vis_kw;
                // Optional visibility specifier
                lex::lex_holder vis_holder(_lexer);
                auto maybe_vis = _lexer.get();
                if (maybe_vis == lex::keyword::PUBLIC || maybe_vis == lex::keyword::PROTECTED || maybe_vis == lex::keyword::PRIVATE) {
                    vis_kw = lex::as<lex::keyword>(maybe_vis);
                } else {
                    vis_holder.rollback();
                }
                // Expect base class name — may be a qualified name: id ('::' id)*
                auto lbase_name = _lexer.get();
                if (!lex::is<lex::identifier>(lbase_name)) {
                    throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_EXPECTED_BASE_CLASS_NAME), _lexer.pick_current(), "Expected base class name in inheritance clause");
                }
                auto first_id = lex::as<lex::identifier>(lbase_name);
                std::string qualified = std::string{first_id.content};
                // Consume optional '::' id pairs
                while (true) {
                    lex::lex_holder dcolon_holder(_lexer);
                    auto maybe_dcolon = _lexer.get();
                    if (maybe_dcolon == lex::punctuator::DOUBLE_COLON) {
                        auto lnext = _lexer.get();
                        if (lex::is<lex::identifier>(lnext)) {
                            qualified += "::" + std::string{lex::as<lex::identifier>(lnext).content};
                        } else {
                            dcolon_holder.rollback();
                            break;
                        }
                    } else {
                        dcolon_holder.rollback();
                        break;
                    }
                }
                // Try to parse optional template arguments: '<' type (',' type)* '>'
                // If successful, encode them into the qualified name as "Name<Arg1,Arg2,...>"
                {
                    lex::lex_holder tpl_holder(_lexer);
                    auto maybe_lt = _lexer.get();
                    if (maybe_lt == lex::operator_::CHEVRON_OPEN) {
                        // Tentatively parse template args
                        std::vector<std::string> tpl_arg_names;
                        bool tpl_ok = true;
                        try {
                            // Check for empty arg list <>
                            {
                                lex::lex_holder empty_holder(_lexer);
                                auto maybe_gt = _lexer.get();
                                if (maybe_gt == lex::operator_::CHEVRON_CLOSE) {
                                    // Empty template args: Name<>
                                    qualified += "<>";
                                    tpl_ok = true;
                                    goto tpl_done;
                                }
                                empty_holder.rollback();
                            }
                            // Parse first arg as identifier (type name)
                            {
                                auto targ = _lexer.get();
                                if (lex::is<lex::identifier>(targ)) {
                                    tpl_arg_names.push_back(std::string{lex::as<lex::identifier>(targ).content});
                                } else {
                                    tpl_ok = false;
                                }
                            }
                            // Parse remaining args
                            while (tpl_ok) {
                                lex::lex_holder sep_holder(_lexer);
                                auto maybe_sep = _lexer.get();
                                if (maybe_sep == lex::punctuator::COMMA) {
                                    auto targ = _lexer.get();
                                    if (lex::is<lex::identifier>(targ)) {
                                        tpl_arg_names.push_back(std::string{lex::as<lex::identifier>(targ).content});
                                    } else {
                                        tpl_ok = false;
                                    }
                                } else {
                                    sep_holder.rollback();
                                    break;
                                }
                            }
                            // Expect '>'
                            if (tpl_ok) {
                                auto maybe_gt = _lexer.get();
                                if (maybe_gt != lex::operator_::CHEVRON_CLOSE) {
                                    tpl_ok = false;
                                }
                            }
                        } catch (...) {
                            tpl_ok = false;
                        }
                        if (tpl_ok) {
                            qualified += "<";
                            for (size_t ti = 0; ti < tpl_arg_names.size(); ++ti) {
                                if (ti > 0) qualified += ",";
                                qualified += tpl_arg_names[ti];
                            }
                            qualified += ">";
                        } else {
                            tpl_holder.rollback();
                        }
                    } else {
                        tpl_holder.rollback();
                    }
                    tpl_done:;
                }
                ast::aggregate_decl::base_clause_entry entry{vis_kw, first_id, qualified};
                bases.push_back(std::move(entry));
                // Check for ',' to continue
                lex::lex_holder comma_holder(_lexer);
                auto maybe_comma = _lexer.get();
                if (maybe_comma == lex::punctuator::COMMA) {
                    // continue to next base
                } else {
                    comma_holder.rollback();
                    break;
                }
            }
        } else {
            base_holder.rollback();
        }
    }

    // Expect an open brace
    if(lex::opt_ref_any_lexeme lopenbrace= _lexer.get(); lopenbrace==lex::punctuator::BRACE_OPEN) {
        open_brace = lex::as<lex::punctuator>(lopenbrace);
    } else {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_STRUCT_MISSING_OPEN_BRACE), _lexer.pick_current(), "Struct open brace is missing");
    }

    std::vector<ast::decl_ptr> declarations = parse_declarations();

    // Expect a closing brace
    if(lex::opt_ref_any_lexeme lclosingbrace= _lexer.get(); lclosingbrace==lex::punctuator::BRACE_CLOSE) {
        close_brace = lex::as<lex::punctuator>(lclosingbrace);
    } else {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_STRUCT_MISSING_CLOSE_BRACE), _lexer.pick_current(), "Struct closing brace is expected");
    }

    auto result = std::make_shared<ast::aggregate_decl>(specifiers, *st, *open_brace, *close_brace, lex::as<lex::identifier>(lname), bases, declarations, annotations);
    result->template_params = std::move(template_params);
    result->is_generic = is_generic;

    // Capture template source text for KDI export: from 'template' keyword through closing '}'
    // Generic aggregates do not need source text (they are synthesised in their declaration module).
    if (result->is_template() && !result->is_generic && tpl_kw_start) {
        const char* src_end = close_brace->content.data() + close_brace->content.size();
        if (src_end > tpl_kw_start) {
            result->template_source_text = std::string(tpl_kw_start, static_cast<size_t>(src_end - tpl_kw_start));
        }
    }

    return result;
}

std::shared_ptr<ast::enum_decl> parser::parse_enum_decl()
{
    lex::lex_holder holder(_lexer);

    std::vector<lex::keyword> specifiers = parse_specifiers();

    // Expect 'enum' keyword
    if(lex::opt_ref_any_lexeme lenum = _lexer.get(); lenum==lex::keyword::ENUM) {
        // ok
    } else {
        holder.rollback();
        return {};
    }
    auto kw_enum = lex::as<lex::keyword>(_lexer.pick_previous());

    // Expect a name
    auto lname = _lexer.get();
    if(lex::is_not<lex::identifier>(lname)) {
        holder.rollback();
        return {};
    }
    auto enum_name = lex::as<lex::identifier>(lname);

    trace("[parser::parse_enum_decl] parsing enum '{}'", {std::string{enum_name.content}});
    // Optional enum type / base clause: ':' TypeSpec
    std::shared_ptr<ast::type_specifier> explicit_underlying_type;
    std::optional<std::string> base_name;
    {
        lex::lex_holder base_holder(_lexer);
        auto maybe_colon = _lexer.get();
        if (maybe_colon == lex::operator_::COLON) {
            explicit_underlying_type = parse_type_spec();
            if (!explicit_underlying_type) {
                throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_EXPECTED_BASE_ENUM_NAME), _lexer.pick_current(), "Expected base enum name after ':' in enum declaration");
            }

            // Backward-compatibility path: keep a plain identified type name as base_name
            // so the current enum-derivation pipeline continues to work unchanged until
            // semantic disambiguation between base enum and typed underlying type is added.
            if (auto identified = std::dynamic_pointer_cast<ast::identified_type_specifier>(explicit_underlying_type)) {
                std::string qualified;
                if (identified->name.has_root_prefix()) {
                    qualified = "::";
                }
                for (size_t i = 0; i < identified->name.names.size(); ++i) {
                    if (i > 0) {
                        qualified += "::";
                    }
                    qualified += std::string{identified->name.names[i].content};
                }
                base_name = qualified;
            }
        } else {
            base_holder.rollback();
        }
    }

    // Expect an open brace
    lex::punctuator open_brace_val({}, lex::punctuator::BRACE_OPEN);
    if(lex::opt_ref_any_lexeme lopenbrace = _lexer.get(); lopenbrace==lex::punctuator::BRACE_OPEN) {
        open_brace_val = lex::as<lex::punctuator>(lopenbrace);
    } else {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_ENUM_MISSING_OPEN_BRACE), _lexer.pick_current(), "Enum open brace is missing");
    }

    // Parse enum entries
    std::vector<std::shared_ptr<ast::enum_entry>> entries;
    while(true) {
        // Check for closing brace
        lex::lex_holder entry_holder(_lexer);
        auto peek = _lexer.get();
        if(peek == lex::punctuator::BRACE_CLOSE) {
            _lexer.unget(); // will be consumed below
            break;
        }
        entry_holder.rollback();

        // Expect an identifier (entry name)
        auto lentry_name = _lexer.get();
        if(lex::is_not<lex::identifier>(lentry_name)) {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_ENUM_ENTRY_EXPECT_NAME), _lexer.pick_current(), "Expected enum entry name");
        }
        auto entry_name = lex::as<lex::identifier>(lentry_name);

        // Optional explicit initializer
        std::optional<lex::any_literal> literal_value;
        std::optional<lex::identifier> ref_value;
        std::vector<ast::expr_ptr> ctor_args;
        std::shared_ptr<ast::brace_init_list> brace_init;
        bool has_paren_init = false;
        {
            lex::lex_holder init_holder(_lexer);
            auto maybe_init = _lexer.get();
            if(maybe_init == lex::operator_::EQUAL) {
                auto lval = _lexer.get();
                if(lex::is<lex::integer>(lval)) {
                    literal_value = lex::any_literal{lex::as<lex::integer>(lval)};
                } else if(lex::is<lex::float_num>(lval)) {
                    literal_value = lex::any_literal{lex::as<lex::float_num>(lval)};
                } else if(lex::is<lex::character>(lval)) {
                    literal_value = lex::any_literal{lex::as<lex::character>(lval)};
                } else if(lex::is<lex::string>(lval)) {
                    literal_value = lex::any_literal{lex::as<lex::string>(lval)};
                } else if(lex::is<lex::boolean>(lval)) {
                    literal_value = lex::any_literal{lex::as<lex::boolean>(lval)};
                } else if(lex::is<lex::null>(lval)) {
                    literal_value = lex::any_literal{lex::as<lex::null>(lval)};
                } else if(lex::is<lex::identifier>(lval)) {
                    ref_value = lex::as<lex::identifier>(lval);
                } else {
                    throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_ENUM_ENTRY_EXPECT_VALUE), _lexer.pick_current(), "Expected a literal or entry name after '=' in enum entry");
                }
            } else if (maybe_init == lex::punctuator::PARENTHESIS_OPEN) {
                has_paren_init = true;
                auto maybe_close = _lexer.get();
                if (maybe_close != lex::punctuator::PARENTHESIS_CLOSE) {
                    _lexer.unget();
                    while (true) {
                        auto arg = parse_assignment_expression();
                        if (!arg) {
                            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_ENUM_ENTRY_EXPECT_VALUE), _lexer.pick_current(), "Expected an expression in enum entry constructor-style initializer");
                        }
                        ctor_args.push_back(arg);
                        auto sep = _lexer.get();
                        if (sep == lex::punctuator::PARENTHESIS_CLOSE) {
                            break;
                        }
                        if (sep != lex::punctuator::COMMA) {
                            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_ENUM_ENTRY_EXPECT_VALUE), _lexer.pick_current(), "Expected ',' or ')' in enum entry constructor-style initializer");
                        }
                    }
                }
            } else if (maybe_init == lex::punctuator::BRACE_OPEN) {
                brace_init = parse_brace_init_list(lex::as<lex::punctuator>(maybe_init));
            } else {
                init_holder.rollback();
            }
        }

        // Optional 'default' keyword
        bool is_default = false;
        {
            lex::lex_holder def_holder(_lexer);
            auto maybe_default = _lexer.get();
            if(maybe_default == lex::keyword::DEFAULT) {
                is_default = true;
            } else {
                def_holder.rollback();
            }
        }

        // Expect semicolon
        if(auto lsemi = _lexer.get(); lsemi != lex::punctuator::SEMICOLON) {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_ENUM_ENTRY_MISSING_SEMICOLON), _lexer.pick_current(), "Expected ';' after enum entry");
        }

        entries.push_back(std::make_shared<ast::enum_entry>(
            entry_name, literal_value, ref_value, is_default, ctor_args, brace_init, has_paren_init));
    }

    // Expect closing brace
    lex::punctuator close_brace_val({}, lex::punctuator::BRACE_CLOSE);
    if(lex::opt_ref_any_lexeme lclosebrace = _lexer.get(); lclosebrace==lex::punctuator::BRACE_CLOSE) {
        close_brace_val = lex::as<lex::punctuator>(lclosebrace);
    } else {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_ENUM_MISSING_CLOSE_BRACE), _lexer.pick_current(), "Enum closing brace is expected");
    }

    // Expect terminal semicolon
    if(auto lsemi = _lexer.get(); lsemi != lex::punctuator::SEMICOLON) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_ENUM_MISSING_SEMICOLON), _lexer.pick_current(), "Expected ';' after enum declaration");
    }

    return std::make_shared<ast::enum_decl>(specifiers, kw_enum, enum_name, explicit_underlying_type, base_name, open_brace_val, close_brace_val, entries);
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
                lex::keyword::FINAL,
                lex::keyword::OVERRIDE>(lkw)
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
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_QNAME_AFTER_ROOT_SEP), _lexer.pick_current(), "Qualified identifier expect an identifier after initial \"::\"");
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
        } else if (lex::is<lex::keyword>(lname)) {
            auto kw = lex::as<lex::keyword>(lname);
            // Certain keywords are valid as trailing components in qualified
            // identifiers (e.g. Foo::annotation, Foo::class, Foo::interface).
            // They act as regular names; semantic meaning is resolved later.
            if (kw.type == lex::keyword::ANNOTATION
                || kw.type == lex::keyword::CLASS
                || kw.type == lex::keyword::INTERFACE
                || kw.type == lex::keyword::STRUCT) {
                names.push_back(lex::identifier{kw.content});
                holder.sync();
            } else {
                // Other keywords after :: are not valid — roll back.
                holder.rollback();
                break;
            }
        } else {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_QNAME_AFTER_INTERMEDIATE_SEP), _lexer.pick_current(), "Qualified identifier expect an identifier after intermediate \"::\"");
        }
    }

    return std::make_shared<ast::qualified_identifier>(initial, names);
}

std::shared_ptr<ast::function_decl> parser::parse_function_decl() {
    lex::lex_holder holder(_lexer);

    // Parse leading annotation definitions (before specifiers, same as aggregate_decl)
    ast::annotation_def_list annotations = parse_annotation_defs();

    // Parse optional generic or template declaration
    const char* tpl_kw_start = nullptr;
    bool fn_is_generic = false;
    ast::template_param_list template_params = parse_generic_declaration(&fn_is_generic);
    if (template_params.empty() && !fn_is_generic) {
        template_params = parse_template_declaration(&tpl_kw_start);
    }

    std::vector<lex::keyword> specifiers = parse_specifiers();

    // Consume spurious 'fun' prefix (not part of K syntax) and emit a warning
    {
        lex::lex_holder fun_holder(_lexer);
        auto lfun = _lexer.get();
        if (lex::is<lex::identifier>(lfun) && std::string{lex::as<lex::identifier>(lfun).content} == "fun") {
            fun_holder.sync(); // consume 'fun'
            warn(static_cast<unsigned int>(k::diag::parser_diag::WARN_SPURIOUS_FUN_PREFIX),
                 lfun, "'fun' is not a K keyword and should be removed from function declarations");
        } else {
            fun_holder.rollback();
        }
    }

    // Check for operator function syntax: 'operator' OP_SYMBOL
    bool is_operator = false;
    bool is_destructor = false;
    bool is_cast_operator = false;
    std::string canonical_name;
    auto lname= _lexer.get();

    if(lname == lex::keyword::OPERATOR) {
        // Parse the operator symbol and map to canonical name
        is_operator = true;
        auto lop = _lexer.get();
        if(!lop) {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_EXPECTED_OPERATOR_SYMBOL), _lexer.pick_current(), "Expected operator symbol after 'operator' keyword");
        }

        // Check for casting operator: operator() : ReturnType
        if(lop == lex::punctuator::PARENTHESIS_OPEN) {
            auto lclose = _lexer.get();
            if(lclose == lex::punctuator::PARENTHESIS_CLOSE) {
                is_cast_operator = true;
            } else {
                throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_CAST_OPERATOR_EMPTY_PARAMS), _lexer.pick_current(), "Casting operator must have empty parameter list: operator()");
            }
        }

        // Check for subscript operator: operator[] (index)
        bool is_subscript_operator = false;
        if(!is_cast_operator && lop == lex::punctuator::BRACKET_OPEN) {
            auto lclose = _lexer.get();
            if(lclose == lex::punctuator::BRACKET_CLOSE) {
                is_subscript_operator = true;
                canonical_name = "__operator_ix_";
            } else {
                throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_CAST_OPERATOR_EMPTY_PARAMS), _lexer.pick_current(), "Subscript operator must use empty brackets: operator[]");
            }
        }

        if(is_cast_operator) {
            // Casting operator: the return type will be parsed later and injected into the canonical name.
            // For now, use a placeholder name that will be updated after return type is parsed.
            canonical_name = "__operator_cv_";
        } else if(is_subscript_operator) {
            // Subscript operator: canonical_name already set to "__operator_ix_"
            // Parameters will be parsed normally below (single index parameter).
        } else if(lex::is<lex::operator_>(lop)) {
            auto& op = lex::as<lex::operator_>(lop);
            switch(op.type) {
                case lex::operator_::PLUS:  canonical_name = "__operator_pl_"; break;
                case lex::operator_::MINUS: canonical_name = "__operator_mi_"; break;
                case lex::operator_::STAR:  canonical_name = "__operator_ml_"; break;
                case lex::operator_::SLASH: canonical_name = "__operator_dv_"; break;
                case lex::operator_::PERCENT: canonical_name = "__operator_rm_"; break;
                case lex::operator_::AMPERSAND: canonical_name = "__operator_an_"; break;
                case lex::operator_::PIPE:  canonical_name = "__operator_or_"; break;
                case lex::operator_::CARET: canonical_name = "__operator_eo_"; break;
                case lex::operator_::TILDE: canonical_name = "__operator_co_"; break;
                case lex::operator_::DOUBLE_CHEVRON_OPEN: canonical_name = "__operator_ls_"; break;
                case lex::operator_::DOUBLE_CHEVRON_CLOSE: canonical_name = "__operator_rs_"; break;
                case lex::operator_::DOUBLE_AMPERSAND: canonical_name = "__operator_aa_"; break;
                case lex::operator_::DOUBLE_PIPE: canonical_name = "__operator_oo_"; break;
                case lex::operator_::EXCLAMATION_MARK: canonical_name = "__operator_nt_"; break;
                case lex::operator_::DOUBLE_EQUAL: canonical_name = "__operator_eq_"; break;
                case lex::operator_::EXCLAMATION_MARK_EQUAL: canonical_name = "__operator_ne_"; break;
                case lex::operator_::CHEVRON_OPEN: canonical_name = "__operator_lt_"; break;
                case lex::operator_::CHEVRON_CLOSE: canonical_name = "__operator_gt_"; break;
                case lex::operator_::CHEVRON_OPEN_EQUAL: canonical_name = "__operator_le_"; break;
                case lex::operator_::CHEVRON_CLOSE_EQUAL: canonical_name = "__operator_ge_"; break;
                case lex::operator_::EQUAL: canonical_name = "__operator_aS_"; break;
                case lex::operator_::PLUS_EQUAL: canonical_name = "__operator_pL_"; break;
                case lex::operator_::MINUS_EQUAL: canonical_name = "__operator_mI_"; break;
                case lex::operator_::STAR_EQUAL: canonical_name = "__operator_mL_"; break;
                case lex::operator_::SLASH_EQUAL: canonical_name = "__operator_dV_"; break;
                case lex::operator_::PERCENT_EQUAL: canonical_name = "__operator_rM_"; break;
                case lex::operator_::AMPERSAND_EQUAL: canonical_name = "__operator_aN_"; break;
                case lex::operator_::PIPE_EQUAL: canonical_name = "__operator_oR_"; break;
                case lex::operator_::CARET_EQUAL: canonical_name = "__operator_eO_"; break;
                case lex::operator_::DOUBLE_CHEVRON_OPEN_EQUAL: canonical_name = "__operator_lS_"; break;
                case lex::operator_::DOUBLE_CHEVRON_CLOSE_EQUAL: canonical_name = "__operator_rS_"; break;
                case lex::operator_::DOUBLE_PLUS: {
                    // Check for prefix (++_) vs postfix (_++) form
                    lex::lex_holder inc_holder(_lexer);
                    auto lnext = _lexer.get();
                    if(lex::is<lex::identifier>(lnext) && std::string{lex::as<lex::identifier>(lnext).content} == "_") {
                        canonical_name = "__operator_pp_";
                    } else {
                        inc_holder.rollback();
                        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_OPERATOR_PREINC_EXPECT_UNDERSCORE), _lexer.pick_current(), "Expected '_' after '++' in operator declaration (use '++_' for prefix increment or '_++' for postfix increment)");
                    }
                    break;
                }
                case lex::operator_::DOUBLE_MINUS: {
                    // Check for prefix (--_) vs postfix (_--) form
                    lex::lex_holder dec_holder(_lexer);
                    auto lnext = _lexer.get();
                    if(lex::is<lex::identifier>(lnext) && std::string{lex::as<lex::identifier>(lnext).content} == "_") {
                        canonical_name = "__operator_mm_";
                    } else {
                        dec_holder.rollback();
                        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_OPERATOR_PREDEC_EXPECT_UNDERSCORE), _lexer.pick_current(), "Expected '_' after '--' in operator declaration (use '--_' for prefix decrement or '_--' for postfix decrement)");
                    }
                    break;
                }
                default:
                    throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_UNSUPPORTED_OPERATOR_SYMBOL), _lexer.pick_previous(), "Unsupported operator symbol in 'operator' declaration");
            }
        } else if(lex::is<lex::identifier>(lop) && std::string{lex::as<lex::identifier>(lop).content} == "_") {
            // Postfix forms: _++ or _--
            auto lop2 = _lexer.get();
            if(lop2 == lex::operator_::DOUBLE_PLUS) {
                canonical_name = "__operator_PP_";
            } else if(lop2 == lex::operator_::DOUBLE_MINUS) {
                canonical_name = "__operator_MM_";
            } else {
                throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_POSTFIX_OPERATOR_EXPECT_INC_DEC), _lexer.pick_previous(), "Expected '++' or '--' after '_' in postfix operator declaration (use '_++' for postfix increment or '_--' for postfix decrement)");
            }
        } else {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_INVALID_OPERATOR_AFTER_KEYWORD), _lexer.pick_previous(), "Expected a valid operator symbol after 'operator' keyword");
        }
        // Create a synthetic identifier with the canonical name.
        // We need the string to outlive the parser, so we use a static storage deque.
        // A deque is used instead of a vector because deque::push_back never
        // invalidates references to existing elements (no reallocation),
        // which is critical since string_view's into earlier entries must remain valid.
        static std::deque<std::string> operator_name_storage;
        operator_name_storage.push_back(canonical_name);
        std::string_view name_view(operator_name_storage.back());
        // Replace the last consumed lexeme with the synthetic identifier, then re-read it.
        _lexer.replace_last(lex::any_lexeme{lex::identifier(name_view)});
        _lexer.unget();
        lname = _lexer.get();

        // For casting operators, we've already consumed the empty parentheses.
        // Skip the normal parenthesis-looking code below.
        if(!is_cast_operator) {
            // Normal operator handling continues below...
        }
    } else if(lname == lex::operator_::TILDE) {
        // Destructor: expect an identifier after the tilde
        auto lname2 = _lexer.get();
        if(lex::is_not<lex::identifier>(lname2)) {
            holder.rollback();
            return {};
        }
        lname = lname2;
        is_destructor = true;
    } else if(lex::is_not<lex::identifier>(lname)) {
        holder.rollback();
        return {};
    }

    // Look for open parenthesis (skip for casting operators)
    if(!is_cast_operator) {
        if(auto lopenpar = _lexer.get(); lopenpar!=lex::punctuator::PARENTHESIS_OPEN) {
            holder.rollback();
            return {};
        }
    }

    {
        std::string fn_name = is_operator ? canonical_name
                            : is_destructor ? ("~" + std::string{lex::as<lex::identifier>(lname).content})
                            : std::string{lex::as<lex::identifier>(lname).content};
        trace("[parser::parse_function_decl] parsing function '{}'", {fn_name});
    }

    // Look for parameter_spec declarations (skip for casting operators which have no params)
    std::vector<std::shared_ptr<ast::parameter_spec>> params;

    if(!is_cast_operator) {
        auto lex = _lexer.get();
        if(!lex) {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_FUNC_EXPECT_FINALIZE), _lexer.pick_current(), "Function declaration expects finalizing its declaration");
        }
        if(lex!=lex::punctuator::PARENTHESIS_CLOSE) {
            if(is_destructor) {
                throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_DTOR_MUST_HAVE_NO_PARAMS), _lexer.pick_current(), "Destructor declaration must have no parameters");
            }
            _lexer.unget();
            holder.sync();
            // Look for first parameter_spec
            auto param = parse_parameter_spec();
            if(param) {
                params.push_back(param);
            } else {
                throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_FUNC_EXPECT_FIRST_PARAM), _lexer.pick_current(), "Function declaration expects a first parameter declaration");
            }

            while(true) {
                lex = _lexer.get();
                if(!lex) {
                    throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_FUNC_EXPECT_FINALIZE_2), _lexer.pick_current(), "Function declaration expects finalizing its declaration");
                }
                if(lex==lex::punctuator::PARENTHESIS_CLOSE) {
                    break;
                }
                if(lex!=lex::punctuator::COMMA){
                    throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_FUNC_EXPECT_CLOSE_OR_COMMA), _lexer.pick_current(), "Function declaration expects a closing parenthesis ')' for finalizing its prototype or a comma ',' to specify another parameter");
                }

                // Look for next parameter_spec
                auto param = parse_parameter_spec();
                if(param) {
                    params.push_back(param);
                } else {
                    throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_FUNC_EXPECT_PARAM_SPEC), _lexer.pick_current(), "Function declaration expects a parameter specification");
                }
            }

            // Validate: default values must only appear on trailing parameters
            bool found_default = false;
            for(auto& p : params) {
                if(p->default_expr) {
                    found_default = true;
                } else if(found_default) {
                    throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_NO_DEFAULT_AFTER_NON_DEFAULT), _lexer.pick_current(), "Parameter without default value cannot follow a parameter with a default value");
                }
            }

            // Validate varargs constraints
            {
                bool found_varargs = false;
                for(size_t i = 0; i < params.size(); ++i) {
                    if(params[i]->is_varargs) {
                        if(found_varargs) {
                            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_MULTIPLE_VARARGS), _lexer.pick_current(), "Only one varargs parameter is allowed per function");
                        }
                        found_varargs = true;
                        if(i != params.size() - 1) {
                            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_VARARGS_NOT_LAST), _lexer.pick_current(), "Varargs parameter must be the last parameter");
                        }
                        if(params[i]->default_expr) {
                            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_VARARGS_WITH_DEFAULT), _lexer.pick_current(), "Varargs parameter cannot have a default value");
                        }
                    }
                }
            }
        }
    }

    // Look for return type OR mem-initializer-list (both start with ':')
    std::shared_ptr<ast::type_specifier> restype;
    std::vector<ast::member_initializer> member_inits;

    // Named return variable: check for 'identifier :' after ')'.
    // Syntax: func(params) retVarName : RetType [ Initialiser ] { body }
    bool has_named_return = false;
    std::optional<lex::identifier> return_var_name;
    ast::expr_ptr return_var_init_expr;
    bool return_var_is_ctor_init = false;

    holder.sync();
    {
        lex::lex_holder named_ret_holder(_lexer);
        auto maybe_name = _lexer.get();
        if (lex::is<lex::identifier>(maybe_name)) {
            // Peek: if next token is ':', this is a named return variable
            lex::lex_holder colon_holder(_lexer);
            auto maybe_colon = _lexer.get();
            if (maybe_colon == lex::operator_::COLON) {
                // It's a named return variable!
                if (is_destructor) {
                    throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_FRIEND_EXPECT_QNAME), _lexer.pick_current(), "Destructor declaration must not have a named return variable");
                }
                if (lex::keyword::has(specifiers, lex::keyword::ABSTRACT)) {
                    throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_FRIEND_MISSING_SEMICOLON), _lexer.pick_current(), "Abstract function declaration must not have a named return variable");
                }

                return_var_name = lex::as<lex::identifier>(maybe_name);
                has_named_return = true;

                // Parse the return type
                restype = parse_type_spec();
                if (!restype) {
                    throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_NAMED_RET_EXPECT_TYPE), _lexer.pick_current(), "Named return variable expects a type specifier after ':'");
                }

                // Parse optional initialiser: '= expr' or '(args...)'
                {
                    lex::lex_holder init_holder(_lexer);
                    auto maybe_init = _lexer.get();
                    if (maybe_init == lex::operator_::EQUAL) {
                        // Assignment-style init: = expr
                        return_var_init_expr = parse_conditional_expr();
                        if (!return_var_init_expr) {
                            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_NAMED_RET_EXPECT_INIT_EXPR), _lexer.pick_current(), "Named return variable expects an expression after '='");
                        }
                        return_var_is_ctor_init = false;
                    } else if (maybe_init == lex::punctuator::PARENTHESIS_OPEN) {
                        // Constructor-style init: (args...)
                        lex::lex_holder close_holder(_lexer);
                        auto maybe_close = _lexer.get();
                        if (maybe_close == lex::punctuator::PARENTHESIS_CLOSE) {
                            // Empty constructor args — default-constructed (no explicit init expr)
                        } else {
                            close_holder.rollback();
                            auto first_arg = parse_assignment_expression();
                            if (!first_arg) {
                                throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_NAMED_RET_CTOR_EXPECT_EXPR_CLOSE), _lexer.pick_current(), "Named return variable constructor expects an expression or ')'");
                            }
                            // For a single arg, store directly as init expr
                            // For multiple args, wrap in expr_list_expr
                            std::vector<ast::expr_ptr> ctor_args;
                            ctor_args.push_back(first_arg);
                            while (true) {
                                lex::lex_holder comma_holder(_lexer);
                                auto next = _lexer.get();
                                if (next == lex::punctuator::PARENTHESIS_CLOSE) {
                                    break;
                                }
                                if (next != lex::punctuator::COMMA) {
                                    throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_EXPECTED_BASE_ENUM_NAME), _lexer.pick_current(), "Named return variable constructor expects ',' or ')' after expression");
                                }
                                auto arg = parse_assignment_expression();
                                if (!arg) {
                                    throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_NAMED_RET_CTOR_EXPECT_EXPR_COMMA), _lexer.pick_current(), "Named return variable constructor expects an expression after ','");
                                }
                                ctor_args.push_back(arg);
                            }
                            if (ctor_args.size() == 1) {
                                return_var_init_expr = ctor_args[0];
                            } else {
                                return_var_init_expr = std::make_shared<ast::expr_list_expr>(ctor_args);
                            }
                        }
                        return_var_is_ctor_init = true;
                    } else {
                        init_holder.rollback();
                        // No init — default-constructed
                    }
                }

                // Don't fall through to the normal ':' handling
                named_ret_holder.sync();
                goto parse_body;
            }
            colon_holder.rollback();
        }
        named_ret_holder.rollback();
    }

    if(auto lcolon = _lexer.get(); lcolon==lex::operator_::COLON) {
        if(is_destructor) {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_DTOR_MUST_HAVE_NO_RETURN), _lexer.pick_current(), "Destructor declaration must not have a return type");
        }

        // For casting operators, the return type MUST follow the ':' with no mem-initializers
        if(is_cast_operator) {
            lex::lex_holder type_holder(_lexer);
            restype = parse_type_spec();
            if(!restype) {
                throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_CAST_OPERATOR_EXPECT_RETURN_TYPE), _lexer.pick_current(), "Casting operator declaration expects a return type after ':'");
            }
            // type_holder stays synced (consumed) — parse_type_spec() already advanced the lexer

            // Encode the return type into the canonical name
            // Helper lambda to encode type specifier
            std::function<std::string(const std::shared_ptr<ast::type_specifier>&)> encode_type;
            encode_type = [&encode_type](const std::shared_ptr<ast::type_specifier>& ts) -> std::string {
                if(!ts) return "void";

                if(auto identified = std::dynamic_pointer_cast<ast::identified_type_specifier>(ts)) {
                    std::string result;
                    for(size_t i = 0; i < identified->name.names.size(); ++i) {
                        if(i > 0) result += "_";
                        result += std::string(identified->name.names[i].content);
                    }
                    return result;
                }
                if(auto keyword_ts = std::dynamic_pointer_cast<ast::keyword_type_specifier>(ts)) {
                    // Encode keyword types (these are built-in types)
                    switch(keyword_ts->keyword.type) {
                        case lex::keyword::INT: return "int";
                        case lex::keyword::DOUBLE: return "double";
                        case lex::keyword::FLOAT: return "float";
                        case lex::keyword::BOOL: return "bool";
                        case lex::keyword::LONG: return keyword_ts->is_unsigned ? "ulong" : "long";
                        case lex::keyword::SHORT: return keyword_ts->is_unsigned ? "ushort" : "short";
                        case lex::keyword::CHAR: return keyword_ts->is_unsigned ? "uchar" : "char";
                        case lex::keyword::BYTE: return keyword_ts->is_unsigned ? "ubyte" : "byte";
                        default: return "unknown";
                    }
                }
                if(auto ptr = std::dynamic_pointer_cast<ast::pointer_type_specifier>(ts)) {
                    std::string result = encode_type(ptr->subtype);
                    switch(ptr->pointer_type.type) {
                        case lex::operator_::STAR: result += "p"; break;      // pointer
                        case lex::operator_::AMPERSAND: result += "r"; break; // reference
                        case lex::operator_::QUESTION_MARK: result += "v"; break;  // view
                        case lex::operator_::PLUS: result += "lnk"; break;   // link
                        case lex::operator_::EXCLAMATION_MARK: result += "o"; break;  // owner
                        case lex::operator_::HASH: result += "d"; break;     // drain
                        default: break;
                    }
                    return result;
                }
                if(auto const_ts = std::dynamic_pointer_cast<ast::const_type_specifier>(ts)) {
                    return encode_type(const_ts->subtype) + "c";  // const qualifier
                }
                // Default fallback
                return "unknown";
            };

            std::string encoded_type = encode_type(restype);
            canonical_name = "__operator_cv_" + encoded_type;

            // Update lname to use the corrected name (with encoded return type).
            // lname is a reference to the lexeme in the lexer's stream; we update it in-place
            // so that the function_decl constructor picks up the correct name.
            static std::deque<std::string> cast_operator_name_storage;
            cast_operator_name_storage.push_back(canonical_name);
            std::string_view name_view(cast_operator_name_storage.back());
            lname->get() = lex::any_lexeme{lex::identifier(name_view)};
        } else {
            // Normal disambiguation logic for non-casting operators
            // Try to parse a type specifier first (covers the 'return type' case for non-constructors).
        // If it succeeds AND is not followed immediately by a '(' (which would be the first mem-init),
        // it is a return-type specifier. Otherwise, we try to parse as a mem-initializer-list.
        // Disambiguation: a type specifier always comes right before '{', so if after parsing the
        // type spec the next token is '{', it was indeed the return type.
        // For abstract functions a bare ';' also ends the return type (no body follows).
        // But for mem-initializer-list the identifier is followed by '(', not ':' or '[' etc.
        // Strategy: try type-spec; if next is '{' or (abstract + ';') → return type. Else rollback and try mem-init-list.
        {
            lex::lex_holder type_holder(_lexer);
            auto candidate_type = parse_type_spec();
            if(candidate_type) {
                // Peek the next token without consuming it
                lex::lex_holder peek_holder(_lexer);
                auto next = _lexer.get();
                peek_holder.rollback();
                bool is_return_type = (next == lex::punctuator::BRACE_OPEN)
                    || (next == lex::punctuator::SEMICOLON)
                    || (next == lex::operator_::ARROW);
                if(is_return_type) {
                    // It really is a return type
                    restype = candidate_type;
                    // type_holder stays synced (consumed)
                } else {
                    // The ':' was not for a return type but for a mem-initializer-list
                    type_holder.rollback();
                    candidate_type.reset();
                }
            } else {
                type_holder.rollback();
            }
        }
        } // end of if(is_cast_operator) else

        if(!restype) {
            // Try to parse as mem-initializer-list: MEMBER_INIT {',' MEMBER_INIT}*
            // MEMBER_INIT := identifier '(' [EXPRESSION_LIST] ')'
            while(true) {
                lex::lex_holder init_holder(_lexer);
                auto lmname = _lexer.get();
                if(!lex::is<lex::identifier>(lmname)) {
                    throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_NO_DEFAULT_AFTER_NON_DEFAULT), _lexer.pick_current(), "Constructor mem-initializer-list expects a member name identifier");
                }
                auto mem_name = lex::as<lex::identifier>(lmname);
                if(auto lopen = _lexer.get(); lopen != lex::punctuator::PARENTHESIS_OPEN) {
                    throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_MEMINIT_EXPECT_OPEN_PAREN), _lexer.pick_current(), "Constructor mem-initializer expects '(' after member name");
                }
                std::vector<std::shared_ptr<ast::expression>> init_args;
                {
                    lex::lex_holder close_holder(_lexer);
                    auto maybe_close = _lexer.get();
                    if(maybe_close == lex::punctuator::PARENTHESIS_CLOSE) {
                        // empty arg list
                    } else {
                        close_holder.rollback();
                        // Parse comma-separated expression list.
                        // Use parse_assignment_expression() (not parse_expression()) so that
                        // the comma separating arguments is NOT consumed as the comma operator.
                        auto first_expr = parse_assignment_expression();
                        if(!first_expr) {
                            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_MEMINIT_EXPECT_EXPR_OR_CLOSE), _lexer.pick_current(), "Constructor mem-initializer expects an expression or ')'");
                        }
                        init_args.push_back(first_expr);
                        while(true) {
                            lex::lex_holder comma_holder(_lexer);
                            auto maybe_comma = _lexer.get();
                            if(maybe_comma == lex::punctuator::PARENTHESIS_CLOSE) {
                                break;
                            }
                            if(maybe_comma != lex::punctuator::COMMA) {
                                throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_MEMINIT_EXPECT_COMMA_OR_CLOSE), _lexer.pick_current(), "Constructor mem-initializer expects ',' or ')' after expression");
                            }
                            auto arg = parse_assignment_expression();
                            if(!arg) {
                                throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_MEMINIT_EXPECT_EXPR_AFTER_COMMA), _lexer.pick_current(), "Constructor mem-initializer expects an expression after ','");
                            }
                            init_args.push_back(arg);
                        }
                    }
                }
                member_inits.emplace_back(mem_name, std::move(init_args));

                // Look for ',' to continue or stop
                lex::lex_holder comma_holder2(_lexer);
                auto maybe_comma2 = _lexer.get();
                if(maybe_comma2 == lex::punctuator::COMMA) {
                    // continue to next member init
                } else {
                    comma_holder2.rollback();
                    break;
                }
            }
        }
    } else {
        holder.rollback();
    }

    parse_body:
    auto statements = parse_statement_block();
    if(!statements) {
        // Try to parse a function-aliasing declaration: -> ('default'|'delete'|qualifiedId) ';'
        lex::lex_holder alias_holder(_lexer);
        auto larrow = _lexer.get();
        if(larrow == lex::operator_::ARROW) {
            if (has_named_return) {
                throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_NAMED_RET_NO_ALIAS), _lexer.pick_current(), "Function with named return variable must not use '->' aliasing");
            }
            // First, try 'default' or 'delete' (only for constructors)
            auto lkw = _lexer.get();
            if(lkw == lex::keyword::DEFAULT || lkw == lex::keyword::DELETE) {
                ast::function_decl::aliasing_spec_t aliasing;
                if(lkw == lex::keyword::DEFAULT) {
                    aliasing = ast::function_decl::aliasing_spec_t::DEFAULT;
                } else {
                    aliasing = ast::function_decl::aliasing_spec_t::DELETE;
                }
                if(auto lsemi = _lexer.get(); lsemi != lex::punctuator::SEMICOLON) {
                    throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_ALIAS_EXPECT_SEMICOLON), _lexer.pick_current(), "Function aliasing declaration expects ';' after 'default'/'delete'");
                }
                // -> default / -> delete is allowed on non-static constructors and on assignment operator declarations.
                if(is_destructor) {
                    throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_ALIAS_EXPECT_BODY_DEFAULT_DELETE), _lexer.pick_current(),
                        "The '-> default' / '-> delete' specifier is only allowed on non-static constructors or assignment operators, not on destructors");
                }
                if(lex::keyword::has(specifiers, lex::keyword::STATIC)) {
                    throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_ALIAS_INVALID_KEYWORD), _lexer.pick_current(),
                        "The '-> default' / '-> delete' specifier is only allowed on non-static constructors or assignment operators; static functions cannot be defaulted or deleted");
                }
                if(is_operator && aliasing == ast::function_decl::aliasing_spec_t::DEFAULT) {
                    throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_REDIRECT_ABSTRACT_INVALID), _lexer.pick_current(),
                        "'-> default' is not supported on operator declarations; only '-> delete' is allowed");
                }
                auto decl = std::make_shared<ast::function_decl>(specifiers, lex::as<lex::identifier>(lname), params, aliasing);
                decl->is_operator = is_operator;
                decl->annotations = std::move(annotations);
                decl->template_params = std::move(template_params);
                decl->is_generic = fn_is_generic;
                return decl;
            }

            // Not default/delete — try to parse a redirect target: qualifiedId [ '(' type_list ')' ] ';'
            _lexer.unget(); // put back the token we just read
            auto redirect_target = parse_qualified_identifier();
            if(!redirect_target) {
                throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_REDIRECT_EXPECT_TARGET), _lexer.pick_current(), "Function redirect declaration expects a target function name, 'default', or 'delete' after '->'");
            }

            // Optional parameter types for disambiguation: '(' [type_spec {',' type_spec}] ')'
            std::vector<std::shared_ptr<ast::type_specifier>> redirect_param_types;
            bool redirect_has_param_types = false;
            {
                lex::lex_holder paren_holder(_lexer);
                auto maybe_open = _lexer.get();
                if(maybe_open == lex::punctuator::PARENTHESIS_OPEN) {
                    redirect_has_param_types = true;
                    // Check for immediate close
                    lex::lex_holder close_holder(_lexer);
                    auto maybe_close = _lexer.get();
                    if(maybe_close == lex::punctuator::PARENTHESIS_CLOSE) {
                        // empty param type list — disambiguation with zero params
                    } else {
                        close_holder.rollback();
                        // Parse comma-separated type list
                        while(true) {
                            auto ts = parse_type_spec();
                            if(!ts) {
                                throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_REDIRECT_EXPECT_TYPE_OR_CLOSE), _lexer.pick_current(), "Function redirect disambiguation expects a type specifier or ')'");
                            }
                            redirect_param_types.push_back(ts);
                            lex::lex_holder comma_holder(_lexer);
                            auto next = _lexer.get();
                            if(next == lex::punctuator::PARENTHESIS_CLOSE) {
                                break;
                            }
                            if(next != lex::punctuator::COMMA) {
                                throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_REDIRECT_EXPECT_COMMA_OR_CLOSE), _lexer.pick_current(), "Function redirect disambiguation expects ',' or ')' after type specifier");
                            }
                        }
                    }
                } else {
                    paren_holder.rollback();
                }
            }

            if(auto lsemi = _lexer.get(); lsemi != lex::punctuator::SEMICOLON) {
                throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_REDIRECT_EXPECT_SEMICOLON), _lexer.pick_current(), "Function redirect declaration expects ';' after target");
            }
            auto decl = std::make_shared<ast::function_decl>(specifiers, lex::as<lex::identifier>(lname), restype, params,
                redirect_target, redirect_param_types, redirect_has_param_types);
            decl->annotations = std::move(annotations);
            decl->template_params = std::move(template_params);
            decl->is_generic = fn_is_generic;
            return decl;
        }
        alias_holder.rollback();

        // Allow bodyless function declarations: 'fun() : T;' or 'abstract fun() : T;'
        // The model_builder will validate whether a bodyless function is permitted
        // in the current context (interface methods, abstract class methods, etc.).
        {
            lex::lex_holder semi_holder(_lexer);
            auto lsemi = _lexer.get();
            if (lsemi == lex::punctuator::SEMICOLON) {
                // Return a function_decl with no body and no aliasing
                auto decl = std::make_shared<ast::function_decl>(specifiers, lex::as<lex::identifier>(lname), restype, params, member_inits, nullptr, is_destructor);
                decl->is_operator = is_operator;
                decl->annotations = std::move(annotations);
                decl->template_params = std::move(template_params);
                decl->is_generic = fn_is_generic;
                return decl;
            }
            semi_holder.rollback();
        }

        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_FUNC_EXPECT_BODY_BLOCK), _lexer.pick_current(), "Function declaration expects a body block '{ ... }'");
    }
    auto decl = std::make_shared<ast::function_decl>(specifiers, lex::as<lex::identifier>(lname), restype, params, member_inits, statements, is_destructor);
    decl->is_operator = is_operator;
    decl->annotations = std::move(annotations);
    decl->template_params = std::move(template_params);
    decl->is_generic = fn_is_generic;
    if (has_named_return) {
        decl->has_named_return = true;
        decl->return_var_name = return_var_name;
        decl->return_var_init_expr = return_var_init_expr;
        decl->return_var_is_ctor_init = return_var_is_ctor_init;
    }

    // Capture template source text for KDI export: from 'template' keyword through closing '}'
    // Generic functions do not need source text (synthesised in their declaration module).
    if (decl->is_template() && !decl->is_generic && tpl_kw_start && statements) {
        const char* src_end = statements->close_brace.content.data() + statements->close_brace.content.size();
        if (src_end > tpl_kw_start) {
            decl->template_source_text = std::string(tpl_kw_start, static_cast<size_t>(src_end - tpl_kw_start));
        }
    }

    return decl;
}

std::shared_ptr<ast::parameter_spec> parser::parse_parameter_spec()
{
    lex::lex_holder holder(_lexer);

    ast::annotation_def_list annotations = parse_annotation_defs();

    std::vector<lex::keyword> specifiers = parse_specifiers();

    std::optional<lex::identifier> name;
    bool is_varargs = false;
    lex::lex_holder holder_name(_lexer);
    if(auto lname = _lexer.get(); lex::is<lex::identifier>(lname)) {
        // Check for '...' (varargs) before ':'
        lex::lex_holder holder_ellipsis(_lexer);
        auto maybe_ellipsis = _lexer.get();
        if(maybe_ellipsis == lex::punctuator::ELLIPSIS) {
            // Varargs: name... : type
            if(auto lcolon = _lexer.get(); lcolon==lex::operator_::COLON) {
                name = lex::as<lex::identifier>(lname);
                is_varargs = true;
            } else {
                holder_name.rollback();
            }
        } else {
            holder_ellipsis.rollback();
            // Standard path: name : type
            if(auto lcolon = _lexer.get(); lcolon==lex::operator_::COLON) {
                name = lex::as<lex::identifier>(lname);
            } else {
                holder_name.rollback();
            }
        }
    } else {
        holder_name.rollback();
    }

    auto type = parse_type_spec();
    if(!type) {
        holder.rollback();
        return {};
    }

    // Check for pack expansion: TYPE '...' name (no colon separator)
    bool is_pack_expansion = false;
    if (!name.has_value()) {
        lex::lex_holder pack_holder(_lexer);
        auto maybe_ellipsis = _lexer.get();
        if (maybe_ellipsis == lex::punctuator::ELLIPSIS) {
            auto maybe_name = _lexer.get();
            if (lex::is<lex::identifier>(maybe_name)) {
                name = lex::as<lex::identifier>(maybe_name);
                is_pack_expansion = true;
            } else {
                pack_holder.rollback();
            }
        } else {
            pack_holder.rollback();
        }
    }

    // If varargs, wrap the declared type in an unsized array type specifier
    if(is_varargs) {
        lex::punctuator br_open(std::string_view("["), lex::punctuator::BRACKET_OPEN);
        lex::punctuator br_close(std::string_view("]"), lex::punctuator::BRACKET_CLOSE);
        type = std::make_shared<ast::array_type_specifier>(type, br_open, br_close, std::nullopt);
    }

    // Parse optional default value: '=' CONDITIONAL_EXPR
    ast::expr_ptr default_expr;
    {
        lex::lex_holder holder_default(_lexer);
        if(auto lequal = _lexer.get(); lequal == lex::operator_::EQUAL) {
            default_expr = parse_conditional_expr();
            if(!default_expr) {
                throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_EXPECTED_BASE_CLASS_NAME), _lexer.pick_current(), "Expected expression after '=' in parameter default value");
            }
        } else {
            holder_default.rollback();
        }
    }

    return std::make_shared<ast::parameter_spec>(std::move(annotations), specifiers, name, type, std::move(default_expr), is_varargs, is_pack_expansion);
}

} // k::parse
