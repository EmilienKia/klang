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

ast::template_param_list parser::parse_template_declaration()
{
    lex::lex_holder holder(_lexer);

    // Check for 'template' keyword
    auto ltemplate = _lexer.get();
    if (ltemplate != lex::keyword::TEMPLATE) {
        holder.rollback();
        return {};
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

        // Expect parameter name
        auto lname = _lexer.get();
        if (lex::is_not<lex::identifier>(lname)) {
            throw_error(0x10044, _lexer.pick_current(), "Expected template parameter name");
        }
        auto param_name = lex::as<lex::identifier>(lname);

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

        // Optional default: '=' ConditionalExpr
        // Note: use primary_expr to avoid consuming '>' or '>>' as relational ops
        ast::expr_ptr default_expr;
        {
            lex::lex_holder eq_holder(_lexer);
            auto maybe_eq = _lexer.get();
            if (maybe_eq == lex::operator_::EQUAL) {
                default_expr = parse_primary_expr();
                if (!default_expr) {
                    throw_error(0x10046, _lexer.pick_current(), "Expected expression after '=' in template parameter default");
                }
            } else {
                eq_holder.rollback();
            }
        }

        return std::make_shared<ast::template_parameter>(kind_kw, param_name, std::move(constraint), std::move(default_expr));
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

ast::template_arg_list parser::parse_template_arg_list()
{
    lex::lex_holder holder(_lexer);

    // Check for '<'
    auto lopen = _lexer.get();
    if (lopen != lex::operator_::CHEVRON_OPEN) {
        holder.rollback();
        return {};
    }

    // Tentative parse: try to parse template arguments.
    // If we fail, roll back and treat '<' as comparison.
    size_t save_pos = _lexer.tell();

    ast::template_arg_list args;
    int angle_depth = 1;

    try {
        // Try to parse the first argument as a type specifier
        auto type_spec = parse_type_spec();
        if (type_spec) {
            args.push_back(std::make_shared<ast::template_arg>(std::move(type_spec)));
        } else {
            // Try as expression
            auto expr = parse_conditional_expr();
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
                    auto expr2 = parse_conditional_expr();
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
        return {};
    }

    return args;
}

std::shared_ptr<ast::aggregate_decl> parser::parse_aggregate_decl()
{
    lex::lex_holder holder(_lexer);

    // Parse leading annotation definitions
    ast::annotation_def_list annotations = parse_annotation_defs();

    // Parse optional template declaration
    ast::template_param_list template_params = parse_template_declaration();

    std::vector<lex::keyword> specifiers = parse_specifiers();

    std::optional<lex::keyword> st;
    std::optional<lex::punctuator> open_brace, close_brace;

    // Accept "struct", "class", "interface" or "annotation" keyword
    if(lex::opt_ref_any_lexeme lstruct = _lexer.get(); lstruct==lex::keyword::STRUCT || lstruct==lex::keyword::CLASS || lstruct==lex::keyword::INTERFACE || lstruct==lex::keyword::ANNOTATION) {
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
    // Optional base enum clause: ':' QualifiedName
    std::optional<std::string> base_name;
    {
        lex::lex_holder base_holder(_lexer);
        auto maybe_colon = _lexer.get();
        if (maybe_colon == lex::operator_::COLON) {
            auto lbase = _lexer.get();
            if (!lex::is<lex::identifier>(lbase)) {
                throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_EXPECTED_BASE_ENUM_NAME), _lexer.pick_current(), "Expected base enum name after ':' in enum declaration");
            }
            std::string qualified = std::string{lex::as<lex::identifier>(lbase).content};
            // Support qualified names: id ('::' id)*
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
            base_name = qualified;
        } else {
            base_holder.rollback();
        }
    }

    // Expect an open brace
    lex::punctuator open_brace_val({}, lex::punctuator::BRACE_OPEN);
    if(lex::opt_ref_any_lexeme lopenbrace = _lexer.get(); lopenbrace==lex::punctuator::BRACE_OPEN) {
        open_brace_val = lex::as<lex::punctuator>(lopenbrace);
    } else {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_USING_EXPECT_QNAME), _lexer.pick_current(), "Enum open brace is missing");
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
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_USING_MISSING_SEMICOLON), _lexer.pick_current(), "Expected enum entry name");
        }
        auto entry_name = lex::as<lex::identifier>(lentry_name);

        // Optional '=' value
        std::optional<lex::any_literal> literal_value;
        std::optional<lex::identifier> ref_value;
        {
            lex::lex_holder eq_holder(_lexer);
            auto maybe_eq = _lexer.get();
            if(maybe_eq == lex::operator_::EQUAL) {
                // Expect integer literal or identifier
                auto lval = _lexer.get();
                if(lex::is<lex::integer>(lval)) {
                    literal_value = lex::any_literal{lex::as<lex::integer>(lval)};
                } else if(lex::is<lex::identifier>(lval)) {
                    ref_value = lex::as<lex::identifier>(lval);
                } else {
                    throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_ENUM_ENTRY_EXPECT_VALUE), _lexer.pick_current(), "Expected integer literal or entry name after '=' in enum entry");
                }
            } else {
                eq_holder.rollback();
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
            entry_name, literal_value, ref_value, is_default));
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

    return std::make_shared<ast::enum_decl>(specifiers, kw_enum, enum_name, base_name, open_brace_val, close_brace_val, entries);
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

    // Parse optional template declaration
    ast::template_param_list template_params = parse_template_declaration();

    std::vector<lex::keyword> specifiers = parse_specifiers();

    // Consume optional 'fun' keyword (lexed as identifier) — syntactic sugar for function declarations
    {
        lex::lex_holder fun_holder(_lexer);
        auto lfun = _lexer.get();
        if (lex::is<lex::identifier>(lfun) && std::string{lex::as<lex::identifier>(lfun).content} == "fun") {
            fun_holder.sync(); // consume 'fun'
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

        if(is_cast_operator) {
            // Casting operator: the return type will be parsed later and injected into the canonical name.
            // For now, use a placeholder name that will be updated after return type is parsed.
            canonical_name = "__operator_cv_";
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
    if (has_named_return) {
        decl->has_named_return = true;
        decl->return_var_name = return_var_name;
        decl->return_var_init_expr = return_var_init_expr;
        decl->return_var_is_ctor_init = return_var_is_ctor_init;
    }
    return decl;
}

std::shared_ptr<ast::parameter_spec> parser::parse_parameter_spec()
{
    lex::lex_holder holder(_lexer);

    ast::annotation_def_list annotations = parse_annotation_defs();

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

    return std::make_shared<ast::parameter_spec>(std::move(annotations), specifiers, name, type, std::move(default_expr));
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

    trace("[parser::parse_statement_block] parsing statement block", {});

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
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_BLOCK_MISSING_CLOSE_BRACE), _lexer.pick_current(), "Block is expecting a closing brace '}'");
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
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_RETURN_MISSING_SEMICOLON), _lexer.pick_current(), "Return statement is expecting to finish by a semicolon ';'");
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
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_IF_EXPECT_OPEN_PAREN), lpopen, "If statement expect an open parenthesis '(' after the 'if' keyword for the tested expression");
    }

    auto test_expr = parse_expression();
    if(!test_expr) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_IF_EXPECT_CONDITION), _lexer.pick_current(), "If statement expect an expression after the open parenthesis '('");
    }

    auto lpclose = _lexer.get();
    if(lpclose != lex::punctuator::PARENTHESIS_CLOSE) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_IF_EXPECT_CLOSE_PAREN), lpclose, "If statement expect a close parenthesis ')' after the tested expression");
    }

    auto then_stmt = parse_statement();
    if(!then_stmt) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_IF_EXPECT_BODY), lpclose, "If statement expect a statement after the close parenthesis ')'");
    }

    holder.sync();

    auto lelse = _lexer.get();
    if(lelse == lex::keyword::ELSE) {
        auto else_stmt = parse_statement();
        if(!then_stmt) {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_IF_EXPECT_ELSE_BODY), lelse, "If statement expect a statement after the 'else' keyword");
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
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_WHILE_EXPECT_OPEN_PAREN), lpopen, "While statement expect an open parenthesis '(' after the 'while' keyword for the tested expression");
    }

    auto test_expr = parse_expression();
    if(!test_expr) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_WHILE_EXPECT_CONDITION), _lexer.pick_current(), "While statement expect an expression after the open parenthesis '('");
    }

    auto lpclose = _lexer.get();
    if(lpclose != lex::punctuator::PARENTHESIS_CLOSE) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_WHILE_EXPECT_CLOSE_PAREN), lpclose, "While statement expect a close parenthesis ')' after the tested expression");
    }

    auto nested_stmt = parse_statement();
    if(!nested_stmt) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_WHILE_EXPECT_BODY), lpclose, "While statement expect a statement after the close parenthesis ')'");
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
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_FOR_EXPECT_OPEN_PAREN), lpopen, "For statement expect an open parenthesis '(' after the 'for' keyword");
    }

    std::optional<lex::punctuator> first_semicolon_kw;
    std::shared_ptr<ast::variable_decl> decl_stmt;
    if(auto decl = parse_variable_decl()) {
        decl_stmt = decl;
        // TODO Add semicolon ref
    } else if(auto lsemicolon = _lexer.get(); lsemicolon == lex::punctuator::SEMICOLON) {
        first_semicolon_kw = lex::as<lex::punctuator>(lsemicolon);
    } else {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_FOR_EXPECT_INIT_OR_SEMICOLON), lpopen, "For statement expect a variable declaration or a semicolon ';' after the open parenthesis'('");
    }

    std::optional<lex::punctuator> second_semicolon_kw;
    std::shared_ptr<ast::expression> test_expr;
    if(auto expr = parse_expression_statement()) {
        test_expr = expr->expr;
        // TODO Add semicolon ref
    } else if(auto lsemicolon = _lexer.get(); lsemicolon == lex::punctuator::SEMICOLON) {
        second_semicolon_kw = lex::as<lex::punctuator>(lsemicolon);
    } else {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_FOR_EXPECT_COND_OR_SEMICOLON), lpopen, "For statement expect an expression or a semicolon ';' after the first semicolon ';'");
    }

    std::shared_ptr<ast::expression> step_expr;
    if(auto expr = parse_expression()) {
        step_expr = expr;
    }

    auto lpclose = _lexer.get();
    if(lpclose != lex::punctuator::PARENTHESIS_CLOSE) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_FOR_EXPECT_CLOSE_PAREN), lpclose, "For statement expect a closing parenthesis ')' after the optional step expression");
    }

    auto nested_stmt = parse_statement();
    if(!nested_stmt) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_FOR_EXPECT_BODY), lpclose, "For statement expect a statement after the close parenthesis ')'");
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
    trace("[parser::parse_statement] parsing statement", {});

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

    if(auto using_stmt = parse_using_decl()) {
        return using_stmt;
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

    trace("[parser::parse_variable_decl] parsing variable '{}'", {std::string{lex::as<lex::identifier>(lname).content}});

    std::shared_ptr<ast::type_specifier> type = parse_type_spec();
    if(!type) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_VARDECL_EXPECT_TYPE), _lexer.pick_current(), "Variable declaration expects a type specifier after the semicolon ';'");
    }

    bool is_constructor = false;
    bool is_brace_init = false;
    ast::expr_ptr expr;
    auto lequal_or_openp = _lexer.get();
    if(lequal_or_openp==lex::operator_::EQUAL) {
        expr = parse_conditional_expr();
        if(!expr) {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_VARDECL_EXPECT_INIT_EXPR), _lexer.pick_current(), "Variable declaration expects an initialization expression after the equal operator '='");
        }
    } else if (lequal_or_openp==lex::punctuator::PARENTHESIS_OPEN) {
        // Parse arguments inside parentheses (could be constructor init or uniform array init)
        std::vector<ast::expr_ptr> paren_args;
        auto lclose_or_first = _lexer.get();
        if (lclose_or_first != lex::punctuator::PARENTHESIS_CLOSE) {
            _lexer.unget();
            while (true) {
                auto arg = parse_conditional_expr();
                paren_args.push_back(arg);
                auto sep = _lexer.get();
                if (sep == lex::punctuator::PARENTHESIS_CLOSE) break;
                if (sep != lex::punctuator::COMMA) {
                    throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_DTOR_MUST_HAVE_NO_PARAMS), sep, "Variable declaration through constructor with parenthesis initialization expects ',' or closing parenthesis ')'");
                }
            }
        }

        // Check for uniform array init: T(args)[N]
        auto peek_bracket = _lexer.get();
        if (peek_bracket == lex::punctuator::BRACKET_OPEN) {
            // Uniform array init: parse array size expression inside [N]
            auto size_expr = parse_conditional_expr();
            auto close_bracket = _lexer.get();
            if (close_bracket != lex::punctuator::BRACKET_CLOSE) {
                throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_ENUM_ENTRY_MISSING_SEMICOLON), close_bracket, "Uniform array init expects a closing bracket ']' after size expression");
            }
            auto var = std::make_shared<ast::variable_decl>(specifiers, lex::as<lex::identifier>(lname), type);
            var->is_uniform_array_init = true;
            var->uniform_ctor_args = std::move(paren_args);
            var->uniform_array_size = size_expr;

            auto lsemicolon = _lexer.get();
            if(lsemicolon!=lex::punctuator::SEMICOLON) {
                throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_VARDECL_MISSING_SEMICOLON), _lexer.pick_current(), "Variable declaration expects to finish by a semicolon ';'");
            }
            return var;
        } else {
            _lexer.unget();
        }

        // Regular constructor init: T(args)
        // Flatten paren_args into a single expression list if needed
        if (paren_args.size() == 1) {
            expr = paren_args[0];
        } else if (paren_args.size() > 1) {
            expr = std::make_shared<ast::expr_list_expr>(paren_args);
        }
        is_constructor = true;
    } else if (lequal_or_openp==lex::punctuator::BRACE_OPEN) {
        // Brace initializer list: { expr, expr, ... } or designated: { .a = expr, .b(args) }
        auto open_brace = lex::as<lex::punctuator>(lequal_or_openp);
        expr = parse_brace_init_list(open_brace);
        is_brace_init = true;
    } else {
        _lexer.unget();
    }

    auto lsemicolon = _lexer.get();
    if(lsemicolon!=lex::punctuator::SEMICOLON) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_VARDECL_MISSING_SEMICOLON), _lexer.pick_current(), "Variable declaration expects to finish by a semicolon ';'");
    }

    return std::make_shared<ast::variable_decl>(specifiers, lex::as<lex::identifier>(lname), type, expr, is_constructor, is_brace_init);
}

std::shared_ptr<ast::type_specifier> parser::parse_type_spec(bool stop_before_bracket)
{
    // ── Try function-reference type first (standalone or member) ──────────────
    // Syntax: [ QualId '::' ] ('*'|'^'|'~') '(' [ TypeSpec {',' TypeSpec} ] ')'
    // A RefKind immediately followed by '(' (when there is no preceding base type)
    // is a function reference type, not a dereference/unary op.
    {
        lex::lex_holder fn_holder(_lexer);

        // Attempt to parse an optional owner prefix of the form "Ident (:: Ident)* ::"
        // followed by a ref-kind operator and '('.
        // We MUST NOT call parse_qualified_identifier() here because it throws when
        // the token after '::' is not an identifier (e.g. when it is '*').
        std::optional<ast::qualified_identifier> owner_opt;

        // Peek ahead: is there a "Ident ... :: RefKind (" sequence?
        // Strategy: collect identifiers separated by "::", stopping when we see
        //   ":: RefKind(" (found owner), or a non-identifier/non-"::" (no owner).
        bool is_function_ref = false;
        {
            lex::lex_holder peek_holder(_lexer);
            std::vector<lex::identifier> names;

            // Try to collect qualified identifier parts
            auto t = _lexer.get();
            if (lex::is<lex::identifier>(t)) {
                names.push_back(lex::as<lex::identifier>(t));
                // Try additional ":: Ident" parts — stop on ":: RefKind("
                while (true) {
                    lex::lex_holder seg(_lexer);
                    auto dc = _lexer.get();
                    if (dc != lex::punctuator::DOUBLE_COLON) {
                        _lexer.unget();
                        // No more ::, names is a qualified identifier WITHOUT the "::" owner suffix.
                        // This means no owner — just a plain identifier, not a function ref prefix.
                        break;
                    }
                    auto next = _lexer.get();
                    if (next == lex::operator_::STAR || next == lex::operator_::QUESTION_MARK || next == lex::operator_::PLUS) {
                        // ":: RefKind" — check for '('
                        auto par = _lexer.get();
                        if (par == lex::punctuator::PARENTHESIS_OPEN) {
                            // Found "names :: RefKind (" → owner = names, function ref found
                            _lexer.unget(); // unget '('
                            _lexer.unget(); // unget RefKind
                            // :: is consumed — that's intentional (we're past it)
                            owner_opt = ast::qualified_identifier(std::nullopt, names);
                            seg.sync();
                            is_function_ref = true;
                            break;
                        } else {
                            _lexer.unget(); // unget par
                            _lexer.unget(); // unget RefKind
                            _lexer.unget(); // unget ::
                            break;
                        }
                    } else if (lex::is<lex::identifier>(next)) {
                        names.push_back(lex::as<lex::identifier>(next));
                        seg.sync();
                    } else {
                        _lexer.unget(); // unget next
                        _lexer.unget(); // unget ::
                        break;
                    }
                }
            } else {
                _lexer.unget(); // put back first token (not an identifier)
            }
            if (!is_function_ref) {
                peek_holder.rollback(); // restore all
            } else {
                peek_holder.sync();
            }
        }

        // Now try to read the ref-kind operator (with or without owner)
        auto ref_tok = _lexer.get();
        if (ref_tok == lex::operator_::STAR ||
            ref_tok == lex::operator_::QUESTION_MARK ||
            ref_tok == lex::operator_::PLUS) {
            // Must be immediately followed by '(' to be a function ref type
            auto par_tok = _lexer.get();
            if (par_tok == lex::punctuator::PARENTHESIS_OPEN) {
                lex::operator_ ref_op = lex::as<lex::operator_>(ref_tok);
                std::vector<std::shared_ptr<ast::type_specifier>> params;

                // Parse parameter type list (may be empty)
                auto close_par = _lexer.get();
                if (close_par != lex::punctuator::PARENTHESIS_CLOSE) {
                    _lexer.unget(); // put back first token of first type
                    while (true) {
                        auto pt = parse_type_spec();
                        if (!pt) {
                            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_BRACE_INIT_NESTED_ERROR), _lexer.pick_current(),
                                "Expected a type specifier in function reference type parameter list");
                        }
                        params.push_back(pt);
                        auto sep = _lexer.get();
                        if (sep == lex::punctuator::PARENTHESIS_CLOSE) {
                            break; // end of param list
                        } else if (sep == lex::punctuator::COMMA) {
                            continue; // next param
                        } else {
                            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_BRACE_INIT_SEP_ERROR), sep,
                                "Expected ',' or ')' in function reference type parameter list");
                        }
                    }
                }
                fn_holder.sync();
                return std::make_shared<ast::function_ref_type_specifier>(ref_op, owner_opt, params);
            } else {
                _lexer.unget(); // unget par_tok
                _lexer.unget(); // unget ref_tok
            }
        } else {
            _lexer.unget(); // unget ref_tok
        }
        fn_holder.rollback();
    }

    std::shared_ptr<ast::type_specifier> res;

    // Optional 'const' prefix: 'const' applies to the base type only (not to pointer suffixes).
    // 'const int*' is parsed as pointer<const int>, not const<pointer<int>>.
    std::optional<lex::keyword> const_kw;
    {
        lex::lex_holder const_holder(_lexer);
        auto lconst = _lexer.get();
        if (lconst == lex::keyword::CONST) {
            const_holder.sync();
            const_kw = lex::as<lex::keyword>(lconst);
        } else {
            _lexer.unget();
        }
    }

    res = parse_fundamental_type_spec();

    lex::lex_holder holder(_lexer);
    if(!res) {
        // Expect a type qualified identifier:
        std::shared_ptr<ast::qualified_identifier> qid = parse_qualified_identifier();
        if(qid) {
            // Try to parse template arguments after the identifier
            auto tpl_args = parse_template_arg_list();
            res = std::make_shared<ast::identified_type_specifier>(*qid, tpl_args);
        } else {
            holder.rollback();
            return {};
        }
    }

    // Apply const wrapper to the base type (before pointer/array suffixes)
    if(const_kw.has_value()) {
        res = std::make_shared<ast::const_type_specifier>(*const_kw, res);
    }

    while(true) {
        holder.sync();
        auto lex = _lexer.get();

        if(lex == lex::operator_::STAR || lex == lex::operator_::AMPERSAND
            || lex == lex::operator_::PLUS || lex == lex::operator_::QUESTION_MARK
            || lex == lex::operator_::HASH) {
            res = std::make_shared<ast::pointer_type_specifier>(res, lex::as<lex::operator_>(lex));
            continue;
        }

        if(lex == lex::operator_::EXCLAMATION_MARK) {
            res = std::make_shared<ast::owner_type_specifier>(res, lex::as<lex::operator_>(lex));
            continue;
        }

        if(lex == lex::punctuator::BRACKET_OPEN && !stop_before_bracket) {

            auto lint = _lexer.get();
            std::optional<lex::integer> int_index;
            if (lex::is<lex::integer>(lint)) {
                int_index = lex::as<lex::integer>(lint);
            } else {
                _lexer.unget();
            }

            auto lbrclose = _lexer.get();
            if (lbrclose != lex::punctuator::BRACKET_CLOSE) {
                throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_TYPE_ARRAY_EXPECT_CLOSE_BRACKET), lbrclose, "Type specifier array index expect a closing bracket");
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
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_EXPRSTMT_MISSING_SEMICOLON), _lexer.pick_current(), "Expression statement expects to finish by a semicolon ';'");
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
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_EXPRLIST_EXPECT_SUBEXPR), _lexer.pick_current(), "Expression list is expecting a sub expression after a comma ','");
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
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_ASSIGN_EXPECT_SUBEXPR), _lexer.pick_current(), "Assignment expression is expecting a sub expression after a the asssignmment operator");
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
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_COND_EXPECT_THEN_EXPR), _lexer.pick_current(), "Conditional expression is expecting a sub expression after a the question-mark '?' operator");
    }

    auto lcolon = _lexer.get();
    if (lqm != lex::operator_::COLON) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_COND_EXPECT_COLON), _lexer.pick_current(), "Conditional expression is expecting a colon ':' operator after the first sub expression");
    }

    ast::expr_ptr right = parse_logical_or_expression();
    if(!right) {
        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_COND_EXPECT_ELSE_EXPR), _lexer.pick_current(), "Conditional expression is expecting a sub expression after the colon ':' operator");
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

    while (true) {
        auto op = _lexer.get();
        if (op != lex::operator_::DOUBLE_PIPE) {
            _lexer.unget();
            return left_expr;
        }

        ast::expr_ptr right_expr = parse_logical_and_expression();
        if(right_expr) {
            left_expr = std::make_shared<ast::binary_operator_expr>(lex::as<lex::operator_>(op), left_expr, right_expr);
        } else {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_LOGOR_EXPECT_SUBEXPR), _lexer.pick_current(), "Logical-OR expression is expecting a sub expression after the double-pipe '||' operator");
        }
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

    while (true) {
        auto op = _lexer.get();
        if (op != lex::operator_::DOUBLE_AMPERSAND) {
            _lexer.unget();
            return left_expr;
        }

        ast::expr_ptr right_expr = parse_inclusive_bin_or_expr();
        if(right_expr) {
            left_expr = std::make_shared<ast::binary_operator_expr>(lex::as<lex::operator_>(op), left_expr, right_expr);
        } else {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_LOGAND_EXPECT_SUBEXPR), _lexer.pick_current(), "Logical-AND expression is expecting a sub expression after the double-ampersand '&&' operator");
        }
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

    while (true) {
        auto op = _lexer.get();
        if (op != lex::operator_::PIPE) {
            _lexer.unget();
            return left_expr;
        }

        ast::expr_ptr right_expr = parse_exclusive_bin_or_expr();
        if(right_expr) {
            left_expr = std::make_shared<ast::binary_operator_expr>(lex::as<lex::operator_>(op), left_expr, right_expr);
        } else {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_BITOR_EXPECT_SUBEXPR), _lexer.pick_current(), "Binary-OR expression is expecting a sub expression after the pipe '|' operator");
        }
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

    while (true) {
        auto op = _lexer.get();
        if (op != lex::operator_::CARET) {
            _lexer.unget();
            return left_expr;
        }

        ast::expr_ptr right_expr = parse_bin_and_expr();
        if(right_expr) {
            left_expr = std::make_shared<ast::binary_operator_expr>(lex::as<lex::operator_>(op), left_expr, right_expr);
        } else {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_BITXOR_EXPECT_SUBEXPR), _lexer.pick_current(), "Binary-XOR expression is expecting a sub expression after the caret '^' operator");
        }
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

    while (true) {
        auto op = _lexer.get();
        if (op != lex::operator_::AMPERSAND) {
            _lexer.unget();
            return left_expr;
        }

        ast::expr_ptr right_expr = parse_equality_expr();
        if(right_expr) {
            left_expr = std::make_shared<ast::binary_operator_expr>(lex::as<lex::operator_>(op), left_expr, right_expr);
        } else {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_BITAND_EXPECT_SUBEXPR), _lexer.pick_current(), "Binary-AND expression is expecting a sub expression after the ampersand '&' operator");
        }
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

    while (true) {
        auto op = _lexer.get();
        if (op != lex::operator_::DOUBLE_EQUAL &&
            op != lex::operator_::EXCLAMATION_MARK_EQUAL) {
            _lexer.unget();
            return left_expr;
        }

        ast::expr_ptr right_expr = parse_relational_expr();
        if(right_expr) {
            left_expr = std::make_shared<ast::binary_operator_expr>(lex::as<lex::operator_>(op), left_expr, right_expr);
        } else {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_EQUALITY_EXPECT_SUBEXPR), _lexer.pick_current(), "Equality expression is expecting a sub expression after the equality '==' or '!=' operators");
        }
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

    while (true) {
        auto op = _lexer.get();
        if(lex::is_none_of<
                lex::operator_::CHEVRON_CLOSE,
                lex::operator_::CHEVRON_OPEN,
                lex::operator_::CHEVRON_CLOSE_EQUAL,
                lex::operator_::CHEVRON_OPEN_EQUAL>(op)) {
            _lexer.unget();
            return left_expr;
        }

        ast::expr_ptr right_expr = parse_shifting_expr();
        if(right_expr) {
            left_expr = std::make_shared<ast::binary_operator_expr>(lex::as<lex::operator_>(op), left_expr, right_expr);
        } else {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_RELATIONAL_EXPECT_SUBEXPR), _lexer.pick_current(), "Relational expression is expecting a sub expression after the relational '<', '>', '<=' or '>=' operators");
        }
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

    while (true) {
        auto op = _lexer.get();
        if (op != lex::operator_::DOUBLE_CHEVRON_CLOSE &&
            op != lex::operator_::DOUBLE_CHEVRON_OPEN) {
            _lexer.unget();
            return left_expr;
        }

        ast::expr_ptr right_expr = parse_additive_expr();
        if(right_expr) {
            left_expr = std::make_shared<ast::binary_operator_expr>(lex::as<lex::operator_>(op), left_expr, right_expr);
        } else {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_SHIFT_EXPECT_SUBEXPR), _lexer.pick_current(), "Shifting expression is expecting a sub expression after the shifting '<<' or '>>' operators");
        }
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

    while (true) {
        auto op = _lexer.get();
        if (op != lex::operator_::PLUS &&
            op != lex::operator_::MINUS) {
            _lexer.unget();
            return left_expr;
        }

        ast::expr_ptr right_expr = parse_multiplicative_expr();
        if(right_expr) {
            left_expr = std::make_shared<ast::binary_operator_expr>(lex::as<lex::operator_>(op), left_expr, right_expr);
        } else {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_ADDITIVE_EXPECT_SUBEXPR), _lexer.pick_current(), "Additive expression is expecting a sub expression after the additive '+' or '-' operators");
        }
    }
}

ast::expr_ptr parser::parse_multiplicative_expr() {
    ast::expr_ptr left_expr;

    if (ast::expr_ptr first = parse_pm_expr()) {
        left_expr = first;
    } else {
        return {};
    }

    while (true) {
        auto op = _lexer.get();
        if (lex::is_none_of<
                lex::operator_::STAR,
                lex::operator_::SLASH,
                lex::operator_::PERCENT>(op)) {
            _lexer.unget();
            return left_expr;
        }

        ast::expr_ptr right_expr = parse_pm_expr();
        if (right_expr) {
            left_expr = std::make_shared<ast::binary_operator_expr>(lex::as<lex::operator_>(op), left_expr, right_expr);
        } else {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_MULTIPLICATIVE_EXPECT_SUBEXPR), _lexer.pick_current(), "Multiplicative expression is expecting a sub expression after the multiplicative '*', '/' or '%' operators");
        }
    }
}

ast::expr_ptr parser::parse_pm_expr() {
    ast::expr_ptr left_expr;

    if (ast::expr_ptr first = parse_cast_expr()) {
        left_expr = first;
    } else {
        return {};
    }

    while (true) {
        auto op = _lexer.get();
        if (op != lex::operator_::DOT_STAR &&
            op != lex::operator_::ARROW_STAR) {
            _lexer.unget();
            return left_expr;
        }

        ast::expr_ptr right_expr = parse_cast_expr();
        if (right_expr) {
            left_expr = std::make_shared<ast::binary_operator_expr>(lex::as<lex::operator_>(op), left_expr, right_expr);
        } else {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_CAST_EXPECT_SUBEXPR), _lexer.pick_current(),
                        "PM expression is expecting a sub expression after the pm '.*' or '.->' operators");
        }
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

    // Handle 'new TypeSpec(args)' or 'new TypeSpec[size]{init}' — keyword expression producing an owner
    if (auto lkw = _lexer.get(); lkw == lex::keyword::NEW) {
        lex::keyword new_kw = lex::as<lex::keyword>(lkw);

        // Parse the base type WITHOUT array suffix '[...]'.
        // For 'new T[expr]', we need to parse [expr] ourselves so that expr
        // can be any expression (not just an integer literal).
        auto type = parse_type_spec(/*stop_before_bracket=*/true);
        if (!type) {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_EXPECTED_OPERATOR_SYMBOL), _lexer.pick_current(), "'new' expects a type specifier");
        }

        // Check for array form: new T[expr] or new T[]
        if (auto peek_bracket = _lexer.get(); peek_bracket == lex::punctuator::BRACKET_OPEN) {
            // Array new — parse size expression inside brackets
            ast::expr_ptr size_expr;
            auto peek_close = _lexer.get();
            if (peek_close != lex::punctuator::BRACKET_CLOSE) {
                // Not an immediate ']' — parse an expression for the array size
                _lexer.unget();
                size_expr = parse_conditional_expr();
                auto close_bracket = _lexer.get();
                if (close_bracket != lex::punctuator::BRACKET_CLOSE) {
                    throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_TYPE_ARRAY_EXPECT_CLOSE_BRACKET), close_bracket,
                        "'new' array size expression expects a closing bracket ']'");
                }
            }
            // else: unsized array new T[], size will be inferred from brace init

            // Optionally parse brace initializer list { ... }
            std::shared_ptr<ast::brace_init_list> brace_init;
            auto peek_brace = _lexer.get();
            if (peek_brace == lex::punctuator::BRACE_OPEN) {
                brace_init = parse_brace_init_list(lex::as<lex::punctuator>(peek_brace));
            } else {
                _lexer.unget(); // no brace init
            }

            holder.sync();
            return std::make_shared<ast::new_expr>(new_kw, type, size_expr, brace_init);
        } else {
            _lexer.unget(); // not a bracket — put token back
        }

        // Check for brace initializer without array brackets: new T{...}
        // This is treated as an array-new with size inferred from the brace init list.
        // new T{} → empty array (0 elements); new T{1,2,3} → array of 3 elements.
        if (auto peek_brace = _lexer.get(); peek_brace == lex::punctuator::BRACE_OPEN) {
            auto brace_init = parse_brace_init_list(lex::as<lex::punctuator>(peek_brace));
            holder.sync();
            return std::make_shared<ast::new_expr>(new_kw, type, /*size_expr=*/nullptr, brace_init);
        } else {
            _lexer.unget();
        }

        // Single-object form: new T(args)  OR  uniform array form: new T(args)[N]
        // Parse argument list '(' args ')'
        if (auto lpar = _lexer.get(); lpar != lex::punctuator::PARENTHESIS_OPEN) {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_OPERATOR_PREINC_EXPECT_UNDERSCORE), _lexer.pick_current(), "'new' expects '(' after the type specifier, '[' for array allocation, or '{' for brace-initialized array");
        }
        std::vector<ast::expr_ptr> args;
        auto lclose = _lexer.get();
        if (lclose != lex::punctuator::PARENTHESIS_CLOSE) {
            _lexer.unget();
            while (true) {
                auto arg = parse_expression();
                if (!arg) {
                    throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_OPERATOR_PREDEC_EXPECT_UNDERSCORE), _lexer.pick_current(), "'new' argument list expects an expression");
                }
                args.push_back(arg);
                auto sep = _lexer.get();
                if (sep == lex::punctuator::PARENTHESIS_CLOSE) break;
                if (sep != lex::punctuator::COMMA) {
                    throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_UNSUPPORTED_OPERATOR_SYMBOL), _lexer.pick_current(), "'new' argument list expects ',' or ')'");
                }
            }
        }

        // Check for uniform array form: new T(args)[N]
        if (auto peek_bracket = _lexer.get(); peek_bracket == lex::punctuator::BRACKET_OPEN) {
            ast::expr_ptr size_expr = parse_conditional_expr();
            auto close_bracket = _lexer.get();
            if (close_bracket != lex::punctuator::BRACKET_CLOSE) {
                throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_POSTFIX_OPERATOR_EXPECT_INC_DEC), close_bracket, "'new' uniform array expects a closing bracket ']' after size expression");
            }
            holder.sync();
            return std::make_shared<ast::new_expr>(new_kw, type, args, size_expr, /*uniform_tag=*/true);
        } else {
            _lexer.unget();
        }

        holder.sync();
        return std::make_shared<ast::new_expr>(new_kw, type, args);
    } else {
        _lexer.unget();
    }

    // Handle 'delete expr' — keyword expression that destroys an owner.
    // Note: 'delete' is also used in '-> delete ;' for function aliasing but
    // that context is parsed in parse_function_decl, not here. Here, 'delete'
    // is always followed by an expression (not by ';').
    {
        lex::lex_holder del_holder(_lexer);
        if (auto lkw = _lexer.get(); lkw == lex::keyword::DELETE) {
            lex::keyword delete_kw = lex::as<lex::keyword>(lkw);
            // Peek: if next is ';' this is NOT an expression-delete (safety guard)
            auto peek = _lexer.get();
            if (peek == lex::punctuator::SEMICOLON) {
                // Not an expression-level delete, roll back
                _lexer.unget();
                del_holder.rollback();
            } else {
                _lexer.unget();
                del_holder.sync();
                ast::expr_ptr expr = parse_unary_expr();
                if (!expr) {
                    throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_POSTFIX_OPERATOR_EXPECT_INC_DEC), _lexer.pick_current(), "'delete' expects an expression");
                }
                return std::make_shared<ast::delete_expr>(delete_kw, expr);
            }
        } else {
            _lexer.unget();
        }
    }

    if(auto lop = _lexer.get();
            lex::is_one_of<
                lex::operator_::DOUBLE_PLUS,
                lex::operator_::DOUBLE_MINUS,
                lex::operator_::STAR,
                lex::operator_::AMPERSAND,
                lex::operator_::PLUS,
                lex::operator_::MINUS,
                lex::operator_::EXCLAMATION_MARK,
                lex::operator_::TILDE,
                lex::operator_::HASH>(lop)
            ) {
        ast::expr_ptr expr = parse_cast_expr();
        if(expr) {
            return std::make_shared<ast::unary_prefix_expr>(lex::as<lex::operator_>(lop), expr);
        } else {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_UNARY_EXPECT_SUBEXPR), _lexer.pick_current(), "Unary expression is expecting a sub expression after the unary '++', '--', '*', '&', '+', '-', '!', '~' or '#' operators");
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
                throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_BRACKET_EXPECT_SUBEXPR), _lexer.pick_current(), "Bracket postfix expression expects sub-expression");
            }
            auto lclose = _lexer.get();
            if(lclose != lex::punctuator::BRACKET_CLOSE) {
                throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_BRACKET_EXPECT_CLOSE), _lexer.pick_current(), "Bracket postfix expression expects closing bracket ']' after sub-expression");
            }
            any = std::make_shared<ast::bracket_postifx_expr>(any, expr);
        } else if(lop == lex::punctuator::PARENTHESIS_OPEN) {
            ast::expr_ptr expr = parse_expression_list();
            // expr might be null if expression list is empty
            auto lclose = _lexer.get();
            if(lclose != lex::punctuator::PARENTHESIS_CLOSE) {
                throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_PAREN_POSTFIX_EXPECT_CLOSE), _lexer.pick_current(), "Parenthesis postfix expression expects closing parenthesis ')'");
            }
            any = std::make_shared<ast::parenthesis_postifx_expr>(any, expr);
        } else if(lop == lex::operator_::ARROW || lop == lex::operator_::DOT) {
            ast::expr_ptr expr = parse_identifier_expr();
            auto ident_expr = std::dynamic_pointer_cast<ast::identifier_expr>(expr);
            if(!ident_expr) {
                throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_STRUCT_MISSING_OPEN_BRACE), _lexer.pick_current(), "Member access postfix expression expects an identifier after the '.' or '->' operator");
            }
            any = std::make_shared<ast::member_access_postfix_expr>(lex::as<lex::operator_>(lop), any, ident_expr);
        } else if(lop == lex::punctuator::BRACE_OPEN
                  && std::dynamic_pointer_cast<ast::identifier_expr>(any)) {
            // Brace-init postfix: S{.x=10, .y=20}
            // Only consume as brace postfix when the content starts with '.'
            // (designated initializer), to avoid ambiguity with statement blocks.
            lex::lex_holder brace_peek_holder(_lexer);
            auto peek_first = _lexer.get();
            brace_peek_holder.rollback();

            if (peek_first == lex::operator_::DOT) {
                auto open_brace = lex::as<lex::punctuator>(lop);
                auto brace_init = parse_brace_init_list(open_brace);
                any = std::make_shared<ast::brace_postfix_expr>(any, brace_init);
            } else {
                // Not a brace-init postfix — unget the '{' and stop
                _lexer.unget();
                break;
            }
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
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_PAREN_EXPECT_SUBEXPR), _lexer.pick_current(), "Parenthesis expression expects a sub-expression after open-parenthesis '('");
        }
        lex::opt_ref_any_lexeme r = _lexer.get();
        if(r != lex::punctuator::PARENTHESIS_CLOSE) {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_PAREN_EXPECT_CLOSE), _lexer.pick_current(), "Parenthesis expression expects closing parenthesis ')' after sub-expression");
        }
        return expr;
    } else if (l == lex::punctuator::AT_SIGN) {
        // Annotation initializer expression: @Name(...) used as a value
        holder.rollback();
        auto ann = parse_annotation_def();
        if (!ann) {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_ANNOTATION_EXPECT_NAME_EXPR), _lexer.pick_current(), "Expected annotation type name after '@' in expression context");
        }
        return std::make_shared<ast::annotation_init_expr>(std::move(ann));
    } else if (l == lex::punctuator::BRACE_OPEN) {
        // Brace-init list as a primary expression: {expr, expr, ...}
        auto open_brace = lex::as<lex::punctuator>(l);
        return parse_brace_init_list(open_brace);
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

std::shared_ptr<ast::brace_init_list> parser::parse_brace_init_list(const lex::punctuator& open_brace) {
    std::vector<ast::expr_ptr> elements;
    auto peek_close_brace = _lexer.get();
    if (peek_close_brace != lex::punctuator::BRACE_CLOSE) {
        _lexer.unget();

        // Peek ahead to determine if this is a designated init list.
        // A designated init starts with '.' followed by an identifier.
        enum class init_mode { UNKNOWN, POSITIONAL, DESIGNATED };
        init_mode mode = init_mode::UNKNOWN;

        bool expect_more = true;
        while (expect_more) {
            auto next = _lexer.get();

            // Check for designated initializer: '.' IDENTIFIER
            if (next == lex::operator_::DOT) {
                auto peek_ident = _lexer.get();
                if (lex::is<lex::identifier>(peek_ident)) {
                    // This is a designated init element
                    if (mode == init_mode::POSITIONAL) {
                        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_MIXED_POSITIONAL_DESIGNATED), next, "Cannot mix positional and designated initializers in the same brace-init list");
                    }
                    mode = init_mode::DESIGNATED;

                    auto dot = lex::as<lex::operator_>(next);
                    auto ident = lex::as<lex::identifier>(peek_ident);

                    // Check for qualified name: '.' Ident '::' Ident ['::' Ident ...]
                    std::vector<lex::identifier> qualifier;
                    lex::identifier member_name = ident;
                    while (true) {
                        lex::lex_holder qual_holder(_lexer);
                        auto maybe_dc = _lexer.get();
                        if (maybe_dc == lex::punctuator::DOUBLE_COLON) {
                            auto maybe_next_id = _lexer.get();
                            if (lex::is<lex::identifier>(maybe_next_id)) {
                                qualifier.push_back(member_name);
                                member_name = lex::as<lex::identifier>(maybe_next_id);
                                qual_holder.sync();
                            } else {
                                // Not an identifier after :: — roll back
                                qual_holder.rollback();
                                break;
                            }
                        } else {
                            qual_holder.rollback();
                            break;
                        }
                    }

                    // Now expect '=' (assignment form) or '(' (constructor form)
                    auto after_name = _lexer.get();
                    if (after_name == lex::operator_::EQUAL) {
                        // Assignment form: .member = expr
                        // The value can be a brace_init_list (for nested structs/arrays)
                        ast::expr_ptr value;
                        auto peek_brace = _lexer.get();
                        if (peek_brace == lex::punctuator::BRACE_OPEN) {
                            value = parse_brace_init_list(lex::as<lex::punctuator>(peek_brace));
                        } else {
                            _lexer.unget();
                            value = parse_conditional_expr();
                        }
                        elements.push_back(std::make_shared<ast::designated_init_element>(
                            dot, member_name, qualifier, value));
                    } else if (after_name == lex::punctuator::PARENTHESIS_OPEN) {
                        // Constructor form: .member(args...)
                        std::vector<ast::expr_ptr> args;
                        auto peek_close = _lexer.get();
                        if (peek_close != lex::punctuator::PARENTHESIS_CLOSE) {
                            _lexer.unget();
                            while (true) {
                                auto arg = parse_conditional_expr();
                                args.push_back(arg);
                                auto sep = _lexer.get();
                                if (sep == lex::punctuator::PARENTHESIS_CLOSE) break;
                                if (sep != lex::punctuator::COMMA) {
                                    throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_DESIGNATED_CTOR_EXPECT_COMMA_CLOSE), sep, "Designated initializer constructor form expects ',' or ')' after argument");
                                }
                            }
                        }
                        elements.push_back(std::make_shared<ast::designated_init_element>(
                            dot, member_name, qualifier, args));
                    } else {
                        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_DESIGNATED_EXPECT_EQ_OR_PAREN), after_name, "Expected '=' or '(' after designated member name '." + std::string{member_name.content} + "'");
                    }

                    // Check for comma or closing brace
                    auto sep = _lexer.get();
                    if (sep == lex::punctuator::COMMA) {
                        // continue
                    } else if (sep == lex::punctuator::BRACE_CLOSE) {
                        _lexer.unget();
                        break;
                    } else {
                        throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_USING_EXPECT_QNAME), sep, "Brace initializer list expects ',' or '}' after designated initializer");
                    }
                } else {
                    // '.' not followed by an identifier — this is an error in designated context,
                    // or this could be a positional expression starting with '.' (unlikely but rollback)
                    _lexer.unget(); // unget the non-identifier
                    _lexer.unget(); // unget the '.'
                    // Fall through to positional parsing below
                    goto parse_positional_element;
                }
            } else if (next == lex::punctuator::COMMA) {
                // Empty element (default construction) — positional only
                if (mode == init_mode::DESIGNATED) {
                    throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_MIXED_POSITIONAL_DESIGNATED), next, "Cannot mix positional and designated initializers in the same brace-init list");
                }
                mode = init_mode::POSITIONAL;
                elements.push_back(nullptr);
            } else if (next == lex::punctuator::BRACE_CLOSE) {
                _lexer.unget();
                break;
            } else {
                _lexer.unget();
                parse_positional_element:
                if (mode == init_mode::DESIGNATED) {
                    throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_MIXED_POSITIONAL_DESIGNATED), next, "Cannot mix positional and designated initializers in the same brace-init list");
                }
                mode = init_mode::POSITIONAL;
                auto elem_expr = parse_conditional_expr();
                elements.push_back(elem_expr);
                auto sep = _lexer.get();
                if (sep == lex::punctuator::COMMA) {
                    // continue
                } else if (sep == lex::punctuator::BRACE_CLOSE) {
                    _lexer.unget();
                    break;
                } else {
                    throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_ENUM_MISSING_CLOSE_BRACE), sep, "Brace initializer list expects ',' or '}' after expression");
                }
            }
        }
        auto close = _lexer.get();
        if (close != lex::punctuator::BRACE_CLOSE) {
            throw_error(static_cast<unsigned int>(k::diag::parser_diag::ERR_ENUM_MISSING_SEMICOLON), close, "Brace initializer list expects a closing brace '}'");
        }
        auto close_brace = lex::as<lex::punctuator>(close);
        return std::make_shared<ast::brace_init_list>(open_brace, close_brace, elements,
            mode == init_mode::DESIGNATED);
    } else {
        // Empty brace list {}
        auto close_brace = lex::as<lex::punctuator>(peek_close_brace);
        return std::make_shared<ast::brace_init_list>(open_brace, close_brace, std::vector<ast::expr_ptr>{});
    }
}

} // k::parse_all

