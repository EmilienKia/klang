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


#ifndef KLANG_AST_HPP
#define KLANG_AST_HPP

#include <iostream>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <sstream>
#include <variant>
#include <vector>

#include "../common/any_of.hpp"
#include "../common/common.hpp"
#include "../lex/lexer.hpp"

namespace k::parse {

    class ast_visitor;

    namespace ast {

        /**
         * A single documentation tag entry.
         *
         * Produced by the doc-comment parser when it encounters a line starting
         * with '@' or '\'.  The tag name is stored in lower-case; the content
         * holds the full raw text that follows the tag name (possibly spanning
         * multiple continuation lines, joined with '\n').
         *
         * The semantic meaning of a tag (e.g. splitting "@param a desc" into
         * name + description) is NOT interpreted at parse time: it is resolved
         * at model-building time by build_function_doc(), keeping the tag set
         * open and extensible without any grammar change.
         */
        struct doc_entry {
            std::string tag;      ///< tag name in lower-case, e.g. "param", "return", "deprecated"
            std::string content;  ///< raw content after the tag name (may be multi-line)
        };

        /**
         * Structured documentation attached to an AST node.
         *
         * Produced in a single pass during parsing: the lexer emits raw
         * doc_comment lexemes; the doc-comment parser cleans markers,
         * concatenates text, splits free text into brief/description, and
         * records @-tag lines as generic doc_entry values — all before model
         * building.
         *
         * Semantic interpretation of entries (param/return/throws/tparam/…)
         * is deferred to model-building time, making the tag vocabulary fully
         * extensible without parser or AST changes.
         */
        struct documentation {
            std::string             brief;        ///< first free-text paragraph
            std::string             description;  ///< subsequent free-text paragraphs
            std::vector<doc_entry>  entries;      ///< ordered list of @-tag entries (generic)

            /** Returns true when all fields are empty (no useful documentation). */
            bool empty() const {
                return brief.empty() && description.empty() && entries.empty();
            }
        };

        /**
         * Base class for all AST nodes.
         *
         * Each node may optionally carry structured documentation (doc_comment) attached to it.
         * Each node may also provide access to its first and last lexemes, as well as an "interest" lexeme that is considered the most relevant for diagnostics.
         */
        struct ast_node : std::enable_shared_from_this<ast_node> {
            /** Structured documentation attached to this node (nullopt when absent). */
            std::optional<documentation> doc;

            virtual void visit(ast_visitor &visitor) = 0;

            /** Returns the first lexeme of this node, if any. */
            virtual lex::opt_any_lexeme get_first_lexeme() const;

            /** Returns the last lexeme of this node, if any. */
            virtual lex::opt_any_lexeme get_last_lexeme() const;

            /** Returns the lexeme that is considered the "interest point" of this node, if any. */
            virtual lex::opt_any_lexeme get_interest_lexeme() const;

            template<typename T>
            std::shared_ptr<T> shared_as() {
                return std::dynamic_pointer_cast<T>(shared_from_this());
            }

            template<typename T>
            std::shared_ptr<const T> shared_as() const {
                return std::dynamic_pointer_cast<T>(shared_from_this());
            }
        };

        // Forward declaration (full definition follows)
        struct qualified_identifier;
        struct brace_init_list;
        struct annotation_def;
        struct template_parameter;
        struct template_arg;
        struct expression;
        typedef std::shared_ptr<expression> expr_ptr;

        /** List of template parameters in a template declaration. */
        using template_param_list = std::vector<std::shared_ptr<template_parameter>>;
        /** List of template arguments in a template instantiation. */
        using template_arg_list = std::vector<std::shared_ptr<template_arg>>;

        /** List of annotation definitions attached to a declaration. */
        using annotation_def_list = std::vector<std::shared_ptr<annotation_def>>;

        /**
         * Import declaration: 'import' QualifiedIdentifier ;
         */
        struct import : ast_node {
            lex::keyword import_kw;
            std::shared_ptr<qualified_identifier> qname;
            std::optional<lex::punctuator> semicolon;

            import(const lex::keyword& import_kw, std::shared_ptr<qualified_identifier> qname)
                : import_kw(import_kw), qname(std::move(qname)) {}

            void set_semicolon(const lex::punctuator& sc) { semicolon = sc; }
            const std::optional<lex::punctuator>& get_semicolon() const { return semicolon; }

            void visit(ast_visitor &visitor) override;

            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;

        };

        /**
         * Qualified identifier: [ '::' ] , Identifier , { '::' , Identifier } ;
         *
         * Represents a sequence of identifiers, optionally prefixed by a root-scope '::'.
         * Examples:
         *   "foo"          — single identifier
         *   "foo::bar"     — two identifiers
         *   "::foo::bar"   — two identifiers with root prefix
         */
        struct qualified_identifier : ast_node {
            std::optional <lex::punctuator> initial_doublecolon;
            std::vector <lex::identifier> names;

            qualified_identifier(const std::optional <lex::punctuator> &initial_doublecolon,
                                 const std::vector <lex::identifier> &names) :
                    initial_doublecolon(initial_doublecolon), names(names) {}

            qualified_identifier(std::optional <lex::punctuator> &&initial_doublecolon,
                                 std::vector <lex::identifier> &&names) :
                    initial_doublecolon(initial_doublecolon), names(names) {}

            bool has_root_prefix() const {
                return initial_doublecolon.has_value();
            }

            size_t size() const {
                return names.size();
            }

            const std::string operator[](size_t index)const {
                return std::string{names[index].content};
            }

            k::name to_name() const {
                std::vector<std::string> idents;
                for(const auto& id : names) {
                    idents.emplace_back(id.content);
                }
                return {has_root_prefix(), idents};
            }

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * Base class for all type specifiers.
         *
         * A type specifier represents a type in the language, which can be a built-in type, a user-defined type, an array type, a pointer type, or a const-qualified type.
         */
        struct type_specifier : ast_node {

        };

        /**
         * Identified type specifier: QualifiedIdentifier [ '<' TemplateArgList '>' ] ;
         *
         * Represents a type identified by a qualified name, optionally with template arguments.
         * Examples:
         *   "int"                 — simple type
         *   "std::vector<int>"    — template type with one argument
         *   "MyNamespace::MyClass<T1, T2>" — template type with two arguments
         */
        struct identified_type_specifier : type_specifier {
            qualified_identifier name;
            /** Template arguments, empty if not a template type. */
            template_arg_list template_args;
            /** True when '<>' or '<args>' was explicitly written (even if template_args is empty). */
            bool has_explicit_template_args = false;
            std::optional<lex::operator_> open_angle;
            std::optional<lex::operator_> close_angle;

            identified_type_specifier(const qualified_identifier &name) : name(name) {}

            identified_type_specifier(qualified_identifier &&name) : name(name) {}

            identified_type_specifier(const qualified_identifier &name, const template_arg_list &template_args)
                : name(name), template_args(template_args), has_explicit_template_args(!template_args.empty()) {}

            identified_type_specifier(const qualified_identifier &name, const template_arg_list &template_args, bool explicit_tpl)
                : name(name), template_args(template_args), has_explicit_template_args(explicit_tpl) {}

            void set_open_angle(const lex::operator_& op) { open_angle = op; }
            void set_close_angle(const lex::operator_& op) { close_angle = op; }
            const std::optional<lex::operator_>& get_open_angle() const { return open_angle; }
            const std::optional<lex::operator_>& get_close_angle() const { return close_angle; }

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;

        };

        /**
         * Keyword type specifier: one of the built-in type keywords (int, long, float, etc.)
         *
         * Represents a built-in type keyword, optionally with modifiers like 'unsigned' or 'long long'.
         * Examples:
         *   "int"                 — simple built-in type
         *   "unsigned int"        — unsigned built-in type
         *   "long long"           — long long built-in type
         */
        struct keyword_type_specifier : type_specifier {
            lex::keyword keyword;
            bool is_unsigned = false;
            bool is_long_long = false;
            std::optional<lex::keyword> unsigned_kw;
            std::optional<lex::keyword> second_kw;

            keyword_type_specifier(const lex::keyword & keyword, bool is_unsigned = false, bool is_long_long = false)
                : keyword(keyword), is_unsigned(is_unsigned), is_long_long(is_long_long) {}
            keyword_type_specifier(lex::keyword && keyword, bool is_unsigned = false, bool is_long_long = false)
                : keyword(keyword), is_unsigned(is_unsigned), is_long_long(is_long_long) {}

            void set_unsigned_kw(const lex::keyword& kw) { unsigned_kw = kw; }
            void set_second_kw(const lex::keyword& kw) { second_kw = kw; }
            const std::optional<lex::keyword>& get_unsigned_kw() const { return unsigned_kw; }
            const std::optional<lex::keyword>& get_second_kw() const { return second_kw; }

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * Array type specifier: wraps another type specifier to mark it as an array type.
         * Syntax: TypeSpec '[' [ IntegerLiteral ] ']'
         * Semantics: represents an array of the given subtype, optionally with a fixed size.
         */
        struct array_type_specifier : type_specifier {
            std::shared_ptr<type_specifier> subtype;
            std::optional<lex::punctuator> br_open, br_close;
            std::optional<lex::integer> lex_int;
            expr_ptr size_expr;

            array_type_specifier(const std::shared_ptr<type_specifier> &subtype,
                                 const std::optional<lex::integer> &lex_int = std::nullopt,
                                 const expr_ptr &size_expr = nullptr):
                    subtype(subtype), lex_int(lex_int), size_expr(size_expr) {}

            array_type_specifier(const std::shared_ptr<type_specifier> &subtype, const lex::punctuator &br_open,
                                 const lex::punctuator &br_close, const std::optional<lex::integer> &lex_int,
                                 const expr_ptr &size_expr = nullptr):
                    subtype(subtype), br_open(br_open), br_close(br_close), lex_int(lex_int), size_expr(size_expr) {}

            void set_br_open(const lex::punctuator& bo) { br_open = bo; }
            void set_br_close(const lex::punctuator& bc) { br_close = bc; }
            const std::optional<lex::punctuator>& get_br_open() const { return br_open; }
            const std::optional<lex::punctuator>& get_br_close() const { return br_close; }

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * Pointer type specifier: wraps another type specifier to mark it as a pointer type.
         * Syntax: TypeSpec '*' | TypeSpec '&' | TypeSpec '+' | TypeSpec '?' | TypeSpec '!' | TypeSpec '#'
         * Semantics: represents a pointer or reference to the given subtype, with different ownership semantics.
         */
        struct pointer_type_specifier : type_specifier {
            std::shared_ptr<type_specifier> subtype;
            lex::operator_ pointer_type;

            pointer_type_specifier(const std::shared_ptr<type_specifier> &subtype, const lex::operator_ &pointer_type)
                    : subtype(subtype), pointer_type(pointer_type) {}

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * Const type specifier: wraps another type specifier to mark it as const-qualified.
         * Syntax: 'const' TypeSpec   (e.g. "const int", "const int*")
         * Semantics: represents a const-qualified type, meaning that the value cannot be modified through this type.
         */
        struct const_type_specifier : type_specifier {
            lex::keyword const_kw;
            std::shared_ptr<type_specifier> subtype;

            const_type_specifier(const lex::keyword &const_kw, const std::shared_ptr<type_specifier> &subtype)
                    : const_kw(const_kw), subtype(subtype) {}

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * Callable type specifier.
         *
         * Represents a callable prototype, optionally prefixed by an addresser.
         * Syntax:
         *   [ Addresser ] '(' [ TypeSpec { ',' TypeSpec } ] ')' [ ':' TypeSpec ]
         *   QualifiedIdentifier '::' Addresser '(' [ TypeList ] ')' [ ':' TypeSpec ]
         *
         * Examples:
         *   (int, double)            — bare prototype, void return (not instantiable)
         *   (int):long               — bare prototype returning long
         *   *(int, double+):int      — pointer to a callable (nullable, rebindable)
         *   ?(int)                   — view callable (nullable, not rebindable)
         *   +()                      — link callable (non-null, rebindable)
         *   &(int):int               — reference callable (non-null, not rebindable)
         *   MyClass::*(int&):int     — unbound member function reference
         *
         * K has no `void` keyword: an omitted `: TypeSpec` *is* the void return.
         *
         * Semantics: represents a callable type, optionally with an addresser, owner, parameter types, return type, and throws specification.
         */
        struct callable_type_specifier : type_specifier {
            /**
             * The addresser operator: STAR (*), QUESTION_MARK (?), PLUS (+) or
             * AMPERSAND (&). Absent means a bare prototype.
             */
            std::optional<lex::operator_> addresser;
            /** Optional qualifier for member function pointer: "MyClass::" part. */
            std::optional<qualified_identifier> owner;
            /** Types of function parameters. */
            std::vector<std::shared_ptr<type_specifier>> param_types;
            /** Declared return type; null means void. */
            std::shared_ptr<type_specifier> return_type;
            /**
             * Declared checked-exception set (`throws A, B`). Empty means the callable
             * declares that it throws nothing.
             */
            std::vector<std::shared_ptr<type_specifier>> throws_spec;
            std::optional<lex::punctuator> open_paren, close_paren;
            std::optional<lex::operator_> colon;
            std::optional<lex::keyword> throws_kw;
            std::optional<lex::punctuator> throws_open_paren, throws_close_paren;

            callable_type_specifier(
                const std::optional<lex::operator_>& addresser,
                const std::optional<qualified_identifier>& owner,
                const std::vector<std::shared_ptr<type_specifier>>& param_types,
                const std::shared_ptr<type_specifier>& return_type = nullptr,
                const std::vector<std::shared_ptr<type_specifier>>& throws_spec = {})
                : addresser(addresser), owner(owner), param_types(param_types),
                  return_type(return_type), throws_spec(throws_spec) {}

            void set_open_paren(const lex::punctuator& p) { open_paren = p; }
            void set_close_paren(const lex::punctuator& p) { close_paren = p; }
            void set_colon(const lex::operator_& op) { colon = op; }
            void set_throws_kw(const lex::keyword& kw) { throws_kw = kw; }
            void set_throws_open_paren(const lex::punctuator& p) { throws_open_paren = p; }
            void set_throws_close_paren(const lex::punctuator& p) { throws_close_paren = p; }

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * Owner type specifier: owning pointer (unique ownership) using '!' suffix.
         * Syntax: TypeSpec '!'
         * Semantics: like std::unique_ptr — single owner, auto-delete on scope exit.
         */
        struct owner_type_specifier : type_specifier {
            std::shared_ptr<type_specifier> subtype;
            lex::operator_ owner_op; ///< The '!' operator token

            owner_type_specifier(const std::shared_ptr<type_specifier>& subtype, const lex::operator_& owner_op)
                : subtype(subtype), owner_op(owner_op) {}

            void visit(ast_visitor& visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        struct expression;
        struct unary_expression;
        struct binary_expression;
        struct ternary_expression;
        struct multi_expression;
        struct block_statement;
        struct parameter_spec;

        struct literal_expr;

        struct expr_list_expr;

        struct binary_operator_expr;

        struct assignment_expr;
        struct conditional_expr;
        struct logical_or_expr;
        struct logical_and_expr;
        struct inclusive_bin_or_expr;
        struct exclusive_bin_or_expr;
        struct bin_and_expr;
        struct equality_expr;
        struct relational_expr;
        struct shifting_expr;
        struct additive_expr;
        struct multiplicative_expr;
        struct pm_expr;
        struct cast_expr;
        struct unary_prefix_expr;
        struct unary_postfix_expr;

        typedef std::shared_ptr<expression> expr_ptr;

        /**
         * Base class for all expressions.
         *
         * Expressions are AST nodes that produce a value when evaluated.
         */
        struct expression : ast_node {
            expression() = default;
            expression(const expression &) = default;
            expression(expression &&) = default;
        };

        struct visibility_decl;
        struct namespace_decl;
        struct using_decl;
        struct alias_decl;
        struct friend_decl;
        struct variable_decl;
        struct function_decl;

        /**
         * Declaration (member of a namespace).
         */
        struct declaration : ast_node {
        };

        typedef std::shared_ptr<declaration> decl_ptr;

        /**
         * Base class for all statements.
         *
         * Statements are AST nodes that perform actions but do not produce a value.
         */
        struct statement : ast_node {
        };

        struct block_statement;
        struct return_statement;
        struct break_statement;
        struct continue_statement;
        struct throw_statement;
        struct try_catch_statement;
        struct catch_clause;
        typedef variable_decl declaration_statement;
        struct expression_statement;
        struct if_else_statement;


        /**
         * Base class for all unary expressions.
         *
         * Unary expressions are AST nodes that operate on a single operand.
         */
        struct unary_expression : expression {
        protected:
            expr_ptr _expr;

        public:
            const expr_ptr expr() const { return _expr; }
            expr_ptr& expr() { return _expr; }

            virtual ~unary_expression() = default;

        protected:
            unary_expression(const unary_expression& other) = default;
            unary_expression(unary_expression&& other) = default;
            unary_expression(expr_ptr expr) : _expr(expr) {}

            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * Base class for all binary expressions.
         *
         * Binary expressions are AST nodes that operate on two operands.
         */
        struct binary_expression : expression {
        protected:
            expr_ptr _lexpr;
            expr_ptr _rexpr;

        public:
            const expr_ptr& lexpr() const { return _lexpr; }
            expr_ptr& lexpr() { return _lexpr; }

            const expr_ptr& rexpr() const { return _rexpr; }
            expr_ptr& rexpr() { return _rexpr; }

        protected:
            binary_expression(const binary_expression&) = default;
            binary_expression(binary_expression&&) = default;

            binary_expression(expr_ptr lexpr, expr_ptr rexpr) : _lexpr(lexpr), _rexpr(rexpr) {}

            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * Base class for all ternary expressions.
         *
         * Ternary expressions are AST nodes that operate on three operands.
         */
        struct ternary_expression : expression {
        protected:
            expr_ptr _lexpr, _mexpr, _rexpr;

        public:
            const expr_ptr& lexpr() const { return _lexpr; }
            expr_ptr& lexpr() { return _lexpr; }

            const expr_ptr& mexpr() const { return _mexpr; }
            expr_ptr& mexpr() { return _mexpr; }

            const expr_ptr& rexpr() const { return _rexpr; }
            expr_ptr& rexpr() { return _rexpr; }

        protected:
            ternary_expression(const ternary_expression&) = default;
            ternary_expression(ternary_expression&&) = default;

            ternary_expression(expr_ptr lexpr, expr_ptr mexpr, expr_ptr rexpr) : _lexpr(lexpr), _mexpr(mexpr), _rexpr(rexpr) {}

            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
       };

        /**
         * Base class for expressions that contain multiple sub-expressions.
         *
         * Multi-expressions are AST nodes that operate on a list of operands.
         */
        struct multi_expression : expression {
        protected:
            std::vector<expr_ptr> _exprs;

            multi_expression(const multi_expression&) = default;
            multi_expression(multi_expression&&) = default;

        public:
            virtual void visit(ast_visitor &visitor) {}

            multi_expression(const std::vector <expr_ptr> &exprs) : _exprs(exprs) {}
            multi_expression(std::vector <expr_ptr> &&exprs) : _exprs(exprs) {}

            size_t size() const { return _exprs.size(); }
            const expr_ptr expr(size_t n) const { return _exprs[n]; }
            expr_ptr expr(size_t n) { return _exprs[n]; }
            const expr_ptr operator[](size_t n) const { return _exprs[n]; }
            expr_ptr operator[](size_t n) { return _exprs[n]; }

            const std::vector<expr_ptr>& exprs() {
                return _exprs;
            }

            std::vector<expr_ptr>& mutable_exprs() {
                return _exprs;
            }

            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * Literal expression: represents a literal value in the source code.
         *
         * Examples:
         *   42          — integer literal
         *   3.14        — floating-point literal
         *   "hello"     — string literal
         *   true        — boolean literal
         */
        struct literal_expr : expression {
            lex::any_literal literal;

            literal_expr(const lex::any_literal &literal) : literal(literal) {}
            literal_expr(lex::any_literal &&literal) : literal(literal) {}

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * Keyword expression: represents a keyword in the source code.
         *
         * Examples:
         *   this        — 'this' keyword
         *   super       — 'super' keyword
         */
        struct keyword_expr : expression {
            lex::keyword keyword;

            keyword_expr(const lex::keyword &keyword) : keyword(keyword) {}
            keyword_expr(lex::keyword &&keyword) : keyword(keyword) {}

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * 'this' expression: represents the 'this' keyword in the source code.
         *
         * Example:
         *   this        — 'this' keyword
         */
        struct this_expr : public keyword_expr {
            this_expr(const lex::keyword &keyword) : keyword_expr(keyword) {}
            this_expr(lex::keyword &&keyword) : keyword_expr(keyword) {}

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * Expression list: represents a list of expressions in the source code.
         *
         * Example:
         *   (expr1, expr2, expr3)   — a list of expressions
         */
        struct expr_list_expr : multi_expression {
            std::optional<lex::punctuator> open_paren, close_paren;

            expr_list_expr(const std::vector<expr_ptr> &exprs) : multi_expression(exprs) {}
            expr_list_expr(std::vector<expr_ptr> &&exprs) : multi_expression(exprs) {}

            void set_open_paren(const lex::punctuator& p) { open_paren = p; }
            void set_close_paren(const lex::punctuator& p) { close_paren = p; }

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * Binary operator expression: represents a binary operation in the source code.
         *
         * Example:
         *   expr1 + expr2    — a binary addition operation
         */
        struct binary_operator_expr : binary_expression {
            lex::operator_ op;

            binary_operator_expr(const lex::operator_ &op, expr_ptr lexpr, expr_ptr rexpr)
                    : binary_expression(lexpr, rexpr), op(op) {}

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * Conditional expression: represents a ternary conditional operation in the source code.
         *
         * Example:
         *  condition ? expr1 : expr2   — a ternary conditional operation
         */
        struct conditional_expr : ternary_expression {
            lex::operator_ question_mark;
            lex::operator_ colon;

            conditional_expr(const lex::operator_ &question_mark, const lex::operator_ &colon, expr_ptr lexpr, expr_ptr  mexpr, expr_ptr rexpr)
                    : ternary_expression(lexpr, mexpr, rexpr), question_mark(question_mark), colon(colon) {}

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * Cast expression: represents a type cast operation in the source code.
         *
         * Example:
         *   (TypeSpec) expr   — a type cast operation
         */
        struct cast_expr : unary_expression {
            std::shared_ptr<type_specifier> type;
            std::optional<lex::punctuator> open_paren, close_paren;

            cast_expr(const cast_expr&) = default;
            cast_expr(cast_expr&&) = default;

            virtual ~cast_expr() = default;

            cast_expr(const std::shared_ptr<type_specifier>& type, const expr_ptr &expr) : unary_expression(expr), type(type) {}
            cast_expr(std::shared_ptr<type_specifier>&& type, expr_ptr &&expr) : unary_expression(expr), type(type) {}

            void set_open_paren(const lex::punctuator& p) { open_paren = p; }
            void set_close_paren(const lex::punctuator& p) { close_paren = p; }

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * New expression: allocates an object on the heap and returns an owner.
         * Syntax (single object): 'new' TypeSpec '(' [args] ')'
         * Syntax (array):         'new' TypeSpec '[' [size_expr] ']' [ '{' [init_list] '}' ]
         * Semantics: allocates a new object or array of the specified type, optionally with constructor arguments or initializer list, and returns an owning pointer to it.
         */
        struct new_expr : expression {
            lex::keyword new_kw;
            std::shared_ptr<ast::type_specifier> type;
            std::vector<expr_ptr> args;

            /** True when this is an array allocation: new T[N]{...} */
            bool is_array = false;
            /** True when this is a uniform array allocation: new T(args)[N] */
            bool is_uniform_array = false;
            /** Constructor arguments for uniform array init (each element gets these). */
            std::vector<expr_ptr> uniform_ctor_args;
            /** Array size expression (inside []), nullptr if size is inferred from init list. */
            expr_ptr array_size_expr;
            /** Array brace initializer list, nullptr if no brace init provided. */
            std::shared_ptr<brace_init_list> brace_init;
            std::optional<lex::punctuator> open_paren, close_paren;
            std::optional<lex::punctuator> open_bracket, close_bracket;

            // Single-object constructor
            new_expr(const lex::keyword& new_kw,
                     const std::shared_ptr<ast::type_specifier>& type,
                     const std::vector<expr_ptr>& args)
                : new_kw(new_kw), type(type), args(args), is_array(false) {}

            // Array constructor
            new_expr(const lex::keyword& new_kw,
                     const std::shared_ptr<ast::type_specifier>& type,
                     const expr_ptr& array_size_expr,
                     const std::shared_ptr<brace_init_list>& brace_init)
                : new_kw(new_kw), type(type), is_array(true),
                  array_size_expr(array_size_expr), brace_init(brace_init) {}

            // Uniform array constructor: new T(args)[N]
            new_expr(const lex::keyword& new_kw,
                     const std::shared_ptr<ast::type_specifier>& type,
                     const std::vector<expr_ptr>& uniform_ctor_args,
                     const expr_ptr& array_size_expr,
                     bool /*uniform_tag*/)
                : new_kw(new_kw), type(type), is_array(true), is_uniform_array(true),
                  uniform_ctor_args(uniform_ctor_args), array_size_expr(array_size_expr) {}

            void set_open_paren(const lex::punctuator& p) { open_paren = p; }
            void set_close_paren(const lex::punctuator& p) { close_paren = p; }
            void set_open_bracket(const lex::punctuator& b) { open_bracket = b; }
            void set_close_bracket(const lex::punctuator& b) { close_bracket = b; }

            void visit(ast_visitor& visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * Delete expression: explicitly deallocates an owner's object.
         * Syntax: 'delete' expr
         * Semantics: deallocates the object pointed to by the owner expression, calling its destructor and freeing memory. The expression must be an owner type.
         */
        struct delete_expr : unary_expression {
            lex::keyword delete_kw;

            delete_expr(const lex::keyword& delete_kw, const expr_ptr& expr)
                : unary_expression(expr), delete_kw(delete_kw) {}

            void visit(ast_visitor& visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * Prefix expression: represents a unary operation with the operator before the operand.
         * Syntax: Operator Expr
         * Semantics: applies the unary operator to the operand expression.
         */
        struct unary_prefix_expr : unary_expression {
            lex::operator_ op;

            unary_prefix_expr(const unary_prefix_expr&) = default;
            unary_prefix_expr(unary_prefix_expr&&) = default;

            unary_prefix_expr(const lex::operator_& op, const expr_ptr &expr) : unary_expression(expr), op(op) {}
            unary_prefix_expr(lex::operator_&& op, expr_ptr &&expr) : unary_expression(expr), op(op) {}

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * Postfix expression: represents a unary operation with the operator after the operand.
         * Syntax: Expr Operator
         * Semantics: applies the unary operator to the operand expression.
         */
        struct unary_postfix_expr : unary_expression {
            lex::operator_ op;
            unary_postfix_expr(const lex::operator_& op, const expr_ptr &expr) : unary_expression(expr), op(op) {}
            unary_postfix_expr(lex::operator_&& op, expr_ptr &&expr) : unary_expression(expr), op(op) {}

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * Bracket postfix expression: represents an array indexing operation.
         * Syntax: Expr '[' Expr ']'
         * Semantics: accesses the element at the given index of the array or pointer expression.
         */
        struct bracket_postifx_expr : binary_expression {
            std::optional<lex::punctuator> open_bracket, close_bracket;

            bracket_postifx_expr(const expr_ptr &lexpr, const expr_ptr &rexpr) : binary_expression(lexpr, rexpr) {}
            bracket_postifx_expr(expr_ptr &&rexpr, expr_ptr &&lexpr) : binary_expression(lexpr, rexpr) {}

            void set_open_bracket(const lex::punctuator& b) { open_bracket = b; }
            void set_close_bracket(const lex::punctuator& b) { close_bracket = b; }

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * Parenthesis postfix expression: represents a function call operation.
         * Syntax: Expr '(' [ ExprList ] ')'
         * Semantics: calls the function represented by the expression with the given arguments.
         */
        struct parenthesis_postifx_expr : binary_expression {
            std::optional<lex::punctuator> open_paren, close_paren;

            parenthesis_postifx_expr(const expr_ptr &lexpr, const expr_ptr &rexpr) : binary_expression(lexpr, rexpr) {}
            parenthesis_postifx_expr(expr_ptr &&rexpr, expr_ptr &&lexpr) : binary_expression(lexpr, rexpr) {}

            void set_open_paren(const lex::punctuator& p) { open_paren = p; }
            void set_close_paren(const lex::punctuator& p) { close_paren = p; }

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        struct identifier_expr;

        /**
         * Member access postfix expression: represents a member access operation.
         * Syntax: Expr ('.' | '->') Identifier
         * Semantics: accesses the member of the object represented by the expression.
         */
        struct member_access_postfix_expr : unary_expression {
            lex::operator_ op;
            std::shared_ptr<identifier_expr> ident_expr;

            member_access_postfix_expr(const lex::operator_& op, const expr_ptr &expr, const std::shared_ptr<identifier_expr> &ident_expr)
            : unary_expression(expr), op(op), ident_expr(ident_expr) {}

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;

        };

        /**
         * Identifier expression: represents a qualified identifier in the source code.
         * Syntax: QualifiedIdentifier [ '<' TemplateArgList '>' ]
         * Semantics: refers to a variable, function, type, or other named entity in the program.
         */
        struct identifier_expr : expression {

            ast::qualified_identifier qident;

            /** Optional template arguments — set when parsing func<T>(args). */
            template_arg_list template_args;

            /** True when '<>' or '<args>' was explicitly written (even if template_args is empty). */
            bool explicit_template_args = false;

            /**
             * True when template_args qualify the leading type part in a qualified
             * identifier expression, e.g. Type<T>::method.
             */
            bool qualifier_explicit_template_args = false;
            std::optional<lex::operator_> open_angle, close_angle;

            identifier_expr(const ast::qualified_identifier& qident) :
                    qident(qident) {}

            identifier_expr(const ast::qualified_identifier& qident, template_arg_list tpl_args) :
                    qident(qident), template_args(std::move(tpl_args)),
                    explicit_template_args(!tpl_args.empty()) {}

            identifier_expr(const ast::qualified_identifier& qident,
                            template_arg_list tpl_args,
                            bool explicit_tpl,
                            bool qualifier_tpl) :
                    qident(qident), template_args(std::move(tpl_args)),
                    explicit_template_args(explicit_tpl),
                    qualifier_explicit_template_args(qualifier_tpl) {}

            void set_open_angle(const lex::operator_& op) { open_angle = op; }
            void set_close_angle(const lex::operator_& op) { close_angle = op; }

            /** True if this identifier carries explicit template arguments (including empty <>). */
            bool has_template_args() const { return explicit_template_args || !template_args.empty(); }

            /** True if explicit template arguments qualify the leading type in 'Type<T>::member'. */
            bool has_qualifier_template_args() const {
                return qualifier_explicit_template_args && has_template_args();
            }

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * Brace-init postfix expression: callee { init_list }
         * Used for temporary anonymous construction with brace initializers:
         *   S{.x = 10, .y = 20}   (struct with designated init)
         *   S{expr, expr, ...}     (struct/array with positional init)
         * Syntax: Expr '{' [ InitList ] '}'
         * Semantics: constructs a temporary object of the type of the callee expression, using
         */
        struct brace_postfix_expr : expression {
            expr_ptr callee;
            std::shared_ptr<brace_init_list> brace_init;
            /** True for array-type temporary form: T[]{...}. */
            bool is_array_type_form = false;
            /** Brackets tokens for array-type form (only valid when is_array_type_form=true). */
            std::optional<lex::punctuator> array_bracket_open;
            std::optional<lex::punctuator> array_bracket_close;

            brace_postfix_expr(const expr_ptr& callee, const std::shared_ptr<brace_init_list>& brace_init)
                : callee(callee), brace_init(brace_init) {}

            brace_postfix_expr(const expr_ptr& callee,
                               const std::shared_ptr<brace_init_list>& brace_init,
                               bool is_array_type_form,
                               const std::optional<lex::punctuator>& array_bracket_open = std::nullopt,
                               const std::optional<lex::punctuator>& array_bracket_close = std::nullopt)
                : callee(callee), brace_init(brace_init), is_array_type_form(is_array_type_form),
                  array_bracket_open(array_bracket_open), array_bracket_close(array_bracket_close) {}

            void set_array_bracket_open(const lex::punctuator& bo) { array_bracket_open = bo; }
            void set_array_bracket_close(const lex::punctuator& bc) { array_bracket_close = bc; }

            void visit(ast_visitor& visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * A single capture in a lambda capture list.
         * Syntax: [ 'const' ] [ '&' ] [ 'this' | Identifier [ '=' Expr ] ]
         * Semantics: represents a single variable or 'this' captured by a lambda expression, optionally with const/reference qualifiers and an initializer expression.
         */
        struct lambda_capture : ast_node {
            bool is_const = false;
            bool is_reference = false;
            bool is_this = false;
            std::optional<lex::identifier> name;
            expr_ptr init_expr;
            std::optional<lex::keyword> const_kw;
            std::optional<lex::operator_> ref_op;
            std::optional<lex::keyword> this_kw;
            std::optional<lex::operator_> equal_op;

            lambda_capture(bool is_const,
                           bool is_reference,
                           bool is_this,
                           std::optional<lex::identifier> name = std::nullopt,
                           expr_ptr init_expr = nullptr)
                : is_const(is_const), is_reference(is_reference), is_this(is_this),
                  name(std::move(name)), init_expr(std::move(init_expr)) {}

            void set_const_kw(const lex::keyword& kw) { const_kw = kw; }
            void set_ref_op(const lex::operator_& op) { ref_op = op; }
            void set_this_kw(const lex::keyword& kw) { this_kw = kw; }
            void set_equal_op(const lex::operator_& op) { equal_op = op; }

            void visit(ast_visitor& visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * Lambda expression: represents an anonymous function (lambda) in the source code.
         * Syntax: [ 'const' ] [ CaptureList ] '(' [ ParameterList ] ')'
         * Semantics: represents an anonymous function (lambda) in the source code, optionally with const qualifier, capture list, parameters, return type, and body.
         */
        struct lambda_expression :  expression {
            bool is_const = false;
            bool has_capture_list = false;
            std::vector<lambda_capture> captures;
            std::vector<std::shared_ptr<parameter_spec>> params;
            std::shared_ptr<type_specifier> return_type;
            std::shared_ptr<block_statement> body;
            std::optional<lex::keyword> const_kw;
            std::optional<lex::punctuator> capture_open_bracket, capture_close_bracket;
            std::optional<lex::punctuator> param_open_paren, param_close_paren;
            std::optional<lex::operator_> colon;

            lambda_expression(bool is_const,
                              bool has_capture_list,
                              std::vector<lambda_capture> captures,
                              std::vector<std::shared_ptr<parameter_spec>> params,
                              std::shared_ptr<type_specifier> return_type,
                              std::shared_ptr<block_statement> body)
                : is_const(is_const), has_capture_list(has_capture_list),
                  captures(std::move(captures)), params(std::move(params)),
                  return_type(std::move(return_type)), body(std::move(body)) {}

            void set_const_kw(const lex::keyword& kw) { const_kw = kw; }
            void set_capture_open_bracket(const lex::punctuator& b) { capture_open_bracket = b; }
            void set_capture_close_bracket(const lex::punctuator& b) { capture_close_bracket = b; }
            void set_param_open_paren(const lex::punctuator& p) { param_open_paren = p; }
            void set_param_close_paren(const lex::punctuator& p) { param_close_paren = p; }
            void set_colon(const lex::operator_& op) { colon = op; }

            void visit(ast_visitor& visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        //
        // Statements
        //

        /**
         * Expression statement: represents a statement that consists of a single expression.
         * Syntax: Expr ';'
         * Semantics: evaluates the expression and discards its result.
         */
        struct expression_statement : statement {
            expr_ptr expr;
            std::optional<lex::punctuator> semicolon;

            expression_statement(expr_ptr expr) : expr(expr) {}

            void set_semicolon(const lex::punctuator& sc) { semicolon = sc; }
            const std::optional<lex::punctuator>& get_semicolon() const { return semicolon; }

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * Block statement: represents a block of statements enclosed in braces.
         * Syntax: '{' { Statement } '}'
         * Semantics: executes the statements in order, with their own scope.
         */
        struct block_statement : statement {
            lex::punctuator open_brace, close_brace;
            std::vector<std::shared_ptr<statement>> statements;

            block_statement(const lex::punctuator& open_brace,
                            const lex::punctuator& close_brace,
                            const std::vector<std::shared_ptr<statement>> &statements) :
                            open_brace(open_brace),
                            close_brace(close_brace),
                            statements(statements) {}

            block_statement(lex::punctuator&& open_brace,
                            lex::punctuator&& close_brace,
                            std::vector<std::shared_ptr<statement>> &&statements) :
                            open_brace(open_brace),
                            close_brace(close_brace),
                            statements(statements) {}

            block_statement(const std::vector<std::shared_ptr<statement>>& statements = {})
                : open_brace(lex::punctuator{std::string_view("{"), lex::punctuator::BRACE_OPEN}),
                  close_brace(lex::punctuator{std::string_view("}"), lex::punctuator::BRACE_CLOSE}),
                  statements(statements) {}

            void set_open_brace(const lex::punctuator& ob) { open_brace = ob; }
            void set_close_brace(const lex::punctuator& cb) { close_brace = cb; }

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * Return statement: represents a return statement in a function.
         * Syntax: 'return' [ Expr ] ';'
         * Semantics: returns from the current function, optionally with a return value.
         */
        struct return_statement : statement {
            lex::keyword ret;
            expr_ptr expr;
            std::optional<lex::punctuator> semicolon;

            return_statement(const lex::keyword& ret, expr_ptr expr) : ret(ret), expr(expr) {}

            void set_semicolon(const lex::punctuator& sc) { semicolon = sc; }
            const std::optional<lex::punctuator>& get_semicolon() const { return semicolon; }

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * Break statement: represents a break statement in a loop or switch.
         * Syntax: 'break' ';'
         * Semantics: exits the nearest enclosing loop or switch statement.
         */
        struct break_statement : statement {
            lex::keyword break_kw;
            std::optional<lex::punctuator> semicolon;

            break_statement(const lex::keyword& break_kw) : break_kw(break_kw) {}

            void set_semicolon(const lex::punctuator& sc) { semicolon = sc; }
            const std::optional<lex::punctuator>& get_semicolon() const { return semicolon; }

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * Continue statement: represents a continue statement in a loop.
         * Syntax: 'continue' ';'
         * Semantics: skips the rest of the current loop iteration and continues with the next iteration
         */
        struct continue_statement : statement {
            lex::keyword continue_kw;
            std::optional<lex::punctuator> semicolon;

            continue_statement(const lex::keyword& continue_kw) : continue_kw(continue_kw) {}

            void set_semicolon(const lex::punctuator& sc) { semicolon = sc; }
            const std::optional<lex::punctuator>& get_semicolon() const { return semicolon; }

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * Throw statement: represents a throw statement in a function.
         * Syntax: 'throw' Expr ';'
         * Semantics: throws an exception with the given expression as the exception object.
         */
        struct throw_statement : statement {
            lex::keyword throw_kw;
            expr_ptr expr;
            std::optional<lex::punctuator> semicolon;

            throw_statement(const lex::keyword& throw_kw, ast::expr_ptr expr)
                : throw_kw(throw_kw), expr(std::move(expr)) {}

            void set_semicolon(const lex::punctuator& sc) { semicolon = sc; }
            const std::optional<lex::punctuator>& get_semicolon() const { return semicolon; }

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * Catch clause: represents a catch clause in a try-catch statement.
         * Syntax: 'catch' [ 'const' ] Identifier ':' TypeSpec '{' { Statement } '}'
         * Semantics: catches an exception of the specified type and executes the statements in the body.
         */
        struct catch_clause : ast_node {
            lex::keyword catch_kw;
            bool is_const = false;
            lex::identifier var_name;
            std::shared_ptr<type_specifier> var_type;
            std::shared_ptr<block_statement> body;
            std::optional<lex::punctuator> open_paren, close_paren;
            std::optional<lex::keyword> const_kw;
            std::optional<lex::operator_> colon;

            catch_clause(const lex::keyword& catch_kw, bool is_const,
                         const lex::identifier& var_name,
                         std::shared_ptr<type_specifier> var_type,
                         std::shared_ptr<block_statement> body)
                : catch_kw(catch_kw), is_const(is_const), var_name(var_name),
                  var_type(std::move(var_type)), body(std::move(body)) {}

            void set_open_paren(const lex::punctuator& p) { open_paren = p; }
            void set_close_paren(const lex::punctuator& p) { close_paren = p; }
            void set_const_kw(const lex::keyword& kw) { const_kw = kw; }
            void set_colon(const lex::operator_& op) { colon = op; }

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * Try-catch statement: represents a try-catch statement in a function.
         * Syntax: 'try' '{' { Statement } '}' { CatchClause } [ 'finally' '{' { Statement } '}' ]
         * Semantics: executes the statements in the try block, and if an exception is thrown, executes the appropriate catch clause or finally block.
         */
        struct try_catch_statement : statement {
            lex::keyword try_kw;
            std::shared_ptr<block_statement> try_body;
            std::vector<std::shared_ptr<catch_clause>> catch_clauses;
            std::shared_ptr<block_statement> finally_body;
            std::optional<lex::keyword> finally_kw;

            try_catch_statement(const lex::keyword& try_kw,
                                std::shared_ptr<block_statement> try_body,
                                std::vector<std::shared_ptr<catch_clause>> catch_clauses,
                                std::shared_ptr<block_statement> finally_body = nullptr)
                : try_kw(try_kw), try_body(std::move(try_body)),
                  catch_clauses(std::move(catch_clauses)),
                  finally_body(std::move(finally_body)) {}

            void set_finally_kw(const lex::keyword& kw) { finally_kw = kw; }

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * If-else statement: represents an if-else statement in a function.
         * Syntax: 'if' '(' Expr ')' Statement [ 'else' Statement ]
         * Semantics: evaluates the expression, and if it is true, executes the first statement; otherwise, executes the second statement if present.
         */
        struct if_else_statement : statement {
            lex::keyword if_kw;
            std::optional<lex::keyword> else_kw;
            std::shared_ptr<expression> test_expr;
            std::shared_ptr<statement> then_stmt;
            std::shared_ptr<statement> else_stmt;
            std::optional<lex::punctuator> open_paren, close_paren;

            /** Optional condition variable declarations (if-let / if(vars; test) forms).
             *  When set and test_expr is null — the boolean test is derived from
             *  the (single) variable's init expression (classic if-let).
             *  When set and test_expr is also set — the test expression determines branching. */
            std::vector<std::shared_ptr<variable_decl>> cond_var_decls;

            if_else_statement(const lex::keyword &if_kw,
                              const lex::keyword &else_kw,
                              const std::shared_ptr<expression>& test_expr,
                              const std::shared_ptr<statement> &then_stmt,
                              const std::shared_ptr<statement> &else_stmt)
                    : if_kw(if_kw), else_kw(else_kw), test_expr(test_expr), then_stmt(then_stmt), else_stmt(else_stmt) {}

            if_else_statement(const lex::keyword &if_kw,
                              const std::shared_ptr<expression>& test_expr,
                              const std::shared_ptr<statement> &then_stmt)
                    : if_kw(if_kw), test_expr(test_expr), then_stmt(then_stmt) {}

            /** Constructor for if-let form with else (single var). */
            if_else_statement(const lex::keyword &if_kw,
                              const lex::keyword &else_kw,
                              const std::shared_ptr<variable_decl>& cond_var_decl,
                              const std::shared_ptr<statement> &then_stmt,
                              const std::shared_ptr<statement> &else_stmt)
                    : if_kw(if_kw), else_kw(else_kw), then_stmt(then_stmt), else_stmt(else_stmt),
                      cond_var_decls({cond_var_decl}) {}

            /** Constructor for if-let form without else (single var). */
            if_else_statement(const lex::keyword &if_kw,
                              const std::shared_ptr<variable_decl>& cond_var_decl,
                              const std::shared_ptr<statement> &then_stmt)
                    : if_kw(if_kw), then_stmt(then_stmt),
                      cond_var_decls({cond_var_decl}) {}

            /** Constructor for multi-var form with else. */
            if_else_statement(const lex::keyword &if_kw,
                              const lex::keyword &else_kw,
                              const std::vector<std::shared_ptr<variable_decl>>& cond_var_decls,
                              const std::shared_ptr<expression>& test_expr,
                              const std::shared_ptr<statement> &then_stmt,
                              const std::shared_ptr<statement> &else_stmt)
                    : if_kw(if_kw), else_kw(else_kw), test_expr(test_expr), then_stmt(then_stmt), else_stmt(else_stmt),
                      cond_var_decls(cond_var_decls) {}

            /** Constructor for multi-var form without else. */
            if_else_statement(const lex::keyword &if_kw,
                              const std::vector<std::shared_ptr<variable_decl>>& cond_var_decls,
                              const std::shared_ptr<expression>& test_expr,
                              const std::shared_ptr<statement> &then_stmt)
                    : if_kw(if_kw), test_expr(test_expr), then_stmt(then_stmt),
                      cond_var_decls(cond_var_decls) {}

            void set_open_paren(const lex::punctuator& p) { open_paren = p; }
            void set_close_paren(const lex::punctuator& p) { close_paren = p; }

            /** True when this if uses condition variable declaration(s). */
            bool has_cond_var() const { return !cond_var_decls.empty(); }

            /** True when this if uses condition variable(s) and a separate test expression. */
            bool has_cond_var_with_test() const { return !cond_var_decls.empty() && test_expr != nullptr; }

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * While statement: represents a while loop in a function.
         * Syntax: 'while' '(' Expr ')' Statement
         * Semantics: repeatedly evaluates the expression and executes the statement as long as the expression is true.
         */
        struct while_statement : statement {
            lex::keyword while_kw;
            std::shared_ptr<expression> test_expr;
            std::shared_ptr<statement> nested_stmt;
            std::optional<lex::punctuator> open_paren, close_paren;

            while_statement(const lex::keyword &while_kw,
                              const std::shared_ptr<expression>& test_expr,
                              const std::shared_ptr<statement> &nested_stmt)
                    : while_kw(while_kw), test_expr(test_expr), nested_stmt(nested_stmt) {}

            void set_open_paren(const lex::punctuator& p) { open_paren = p; }
            void set_close_paren(const lex::punctuator& p) { close_paren = p; }

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * For statement: represents a for loop in a function.
         * Syntax: 'for' '(' [VariableDecl] ';' [Expr] ';' [Expr] ')' Statement
         * Semantics: repeatedly executes the statement, initializing with the variable declaration, testing the expression, and stepping with the second expression.
         */
        struct for_statement : statement {
            lex::keyword for_kw;
            lex::punctuator first_semicolon_kw;
            lex::punctuator second_semicolon_kw;
            std::shared_ptr<variable_decl> decl_expr;
            std::shared_ptr<expression> test_expr;
            std::shared_ptr<expression> step_expr;
            std::shared_ptr<statement> nested_stmt;
            std::optional<lex::punctuator> open_paren, close_paren;

            for_statement(const lex::keyword &for_kw,
                          const std::shared_ptr<variable_decl> &decl_expr,
                          const std::shared_ptr<expression> &test_expr,
                          const std::shared_ptr<expression> &step_expr,
                          const std::shared_ptr<statement> &nested_stmt) :
                    for_kw(for_kw),
                    first_semicolon_kw(lex::punctuator{std::string_view(";"), lex::punctuator::SEMICOLON}),
                    second_semicolon_kw(lex::punctuator{std::string_view(";"), lex::punctuator::SEMICOLON}),
                    decl_expr(decl_expr), test_expr(test_expr),
                    step_expr(step_expr),
                    nested_stmt(nested_stmt) {}

            for_statement(const lex::keyword &for_kw,
                          const lex::punctuator &first_semicolon_kw,
                          const lex::punctuator &second_semicolon_kw,
                          const std::shared_ptr<variable_decl> &decl_expr,
                          const std::shared_ptr<expression> &test_expr,
                          const std::shared_ptr<expression> &step_expr,
                          const std::shared_ptr<statement> &nested_stmt) :
                    for_kw(for_kw),
                    first_semicolon_kw(first_semicolon_kw),
                    second_semicolon_kw(second_semicolon_kw),
                    decl_expr(decl_expr), test_expr(test_expr),
                    step_expr(step_expr),
                    nested_stmt(nested_stmt) {}

            void set_open_paren(const lex::punctuator& p) { open_paren = p; }
            void set_close_paren(const lex::punctuator& p) { close_paren = p; }
            void set_first_semicolon(const lex::punctuator& sc) { first_semicolon_kw = sc; }
            void set_second_semicolon(const lex::punctuator& sc) { second_semicolon_kw = sc; }

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * Foreach statement: represents a foreach loop in a function.
         * Syntax: 'for' '(' [Specifier] Identifier ':' TypeSpec '=' ConditionalExpr ')' Statement
         * Semantics: iterates over a collection, executing the statement for each element, with the loop variable declared and initialized to each element in turn.
         *
         * Unlike the classic for_statement, there is a single nested expression between the
         * parentheses (no ';' separators). The loop variable declaration is reused as-is
         * (ast::variable_decl), its 'init' member holding the iterated expression (array,
         * iterator or sequence — the actual variant is only known after type resolution).
         */
        struct foreach_statement : statement {
            lex::keyword for_kw;
            std::shared_ptr<variable_decl> decl_expr;
            std::shared_ptr<statement> nested_stmt;
            std::optional<lex::punctuator> open_paren, close_paren;

            foreach_statement(const lex::keyword &for_kw,
                               const std::shared_ptr<variable_decl> &decl_expr,
                               const std::shared_ptr<statement> &nested_stmt) :
                    for_kw(for_kw), decl_expr(decl_expr), nested_stmt(nested_stmt) {}

            void set_open_paren(const lex::punctuator& p) { open_paren = p; }
            void set_close_paren(const lex::punctuator& p) { close_paren = p; }

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        //
        // Declarations
        //

        /**
         * Visibility declaration: represents a visibility specifier in a class or namespace.
         * Syntax: 'public' | 'protected' | 'private'
         * Semantics: specifies the visibility of subsequent members in a class or namespace.
         */
        struct visibility_decl : declaration {
            lex::keyword scope;
            std::optional<lex::operator_> colon;

            visibility_decl(const visibility_decl&) = default;
            visibility_decl(visibility_decl&&) = default;

            visibility_decl(const lex::keyword &scope) : scope(scope) {}
            visibility_decl(lex::keyword &&scope) : scope(scope) {}

            void set_colon(const lex::operator_& c) { colon = c; }
            const std::optional<lex::operator_>& get_colon() const { return colon; }

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * Namespace declaration: represents a namespace in the source code.
         * Syntax: 'namespace' [Identifier] '{' { Declaration } '}'
         * Semantics: defines a named or anonymous namespace, containing declarations that are scoped within it.
         */
        struct namespace_decl :  declaration {
            lex::keyword ns;
            lex::punctuator open_par, close_par;
            std::optional<lex::punctuator> open_brace, close_brace;
            std::optional <lex::identifier> name;
            std::vector <decl_ptr> declarations;

            namespace_decl(const lex::keyword& ns,
                           const std::optional <lex::identifier> &name,
                           const std::vector <decl_ptr> &declarations) :
                    ns(ns),
                    open_par(lex::punctuator{std::string_view("{"), lex::punctuator::BRACE_OPEN}),
                    close_par(lex::punctuator{std::string_view("}"), lex::punctuator::BRACE_CLOSE}),
                    open_brace(open_par), close_brace(close_par),
                    name(name), declarations(declarations) {}

            namespace_decl(const lex::keyword& ns,
                           const lex::punctuator& open_par,
                           const lex::punctuator& close_par,
                           const std::optional <lex::identifier> &name,
                           const std::vector <decl_ptr> &declarations) :
                    ns(ns), open_par(open_par), close_par(close_par), open_brace(open_par), close_brace(close_par), name(name), declarations(declarations) {}

            namespace_decl(lex::keyword&& ns,
                           lex::punctuator&& open_par,
                           lex::punctuator&& close_par,
                           std::optional <lex::identifier> &&name,
                           std::vector <decl_ptr> &&declarations) :
                    ns(ns), open_par(open_par), close_par(close_par), open_brace(open_par), close_brace(close_par), name(name), declarations(declarations) {}

            void set_open_brace(const lex::punctuator& ob) { open_brace = ob; open_par = ob; }
            void set_close_brace(const lex::punctuator& cb) { close_brace = cb; close_par = cb; }

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * Using directive declaration.
         * Syntax: 'using' [filter]? [identifier '=']? QUALIFIED_IDENTIFIER ';'
         * Semantics: imports the specified qualified identifier into the current scope, optionally with a filter and/or alias.
         *
         * Can appear in both declaration context (namespace, aggregate body) and
         * statement context (function body, block).
         *
         * Without alias: makes the targeted element(s) resolvable as if they
         * were direct members of the enclosing scope.
         * With alias: the target is accessible under the alias name only.
         *   - For namespaces: 'using M = namespace X::Y;' makes X::Y accessible
         *     as M::member (members are NOT injected directly).
         *   - For other elements: 'using Foo = X::Y::bar;' makes bar accessible
         *     as Foo in the current scope.
         */
        struct using_decl : declaration, statement {
            /// The 'using' keyword token.
            lex::keyword using_kw;

            /// Optional element type filter: NAMESPACE, STRUCT, INTERFACE, CLASS, or nullopt.
            std::optional<lex::keyword> element_filter;

            /// Optional alias name (the identifier before '=').
            std::optional<lex::identifier> alias_name;

            /// The qualified name being imported into the current scope.
            std::shared_ptr<qualified_identifier> qname;
            std::optional<lex::operator_> equal_op;
            std::optional<lex::punctuator> semicolon;

            using_decl(const lex::keyword& using_kw,
                       const std::optional<lex::keyword>& element_filter,
                       const std::optional<lex::identifier>& alias_name,
                       std::shared_ptr<qualified_identifier> qname)
                : using_kw(using_kw), element_filter(element_filter),
                  alias_name(alias_name), qname(std::move(qname)) {}

            void set_equal_op(const lex::operator_& op) { equal_op = op; }
            void set_semicolon(const lex::punctuator& sc) { semicolon = sc; }

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * Exported aliasing declaration: 'alias' (soft) or 'typedef' (strong).
         *
         * Syntax:
         *   'alias'   IDENTIFIER ':' QUALIFIED_IDENTIFIER ';'
         *   'typedef' IDENTIFIER ':' TYPE_SPECIFIER ';'
         * Semantics: introduces a new name for an existing type or symbol, either as a soft alias (alias) or a strong alias (typedef).
         *
         * Unlike using_decl, an alias/typedef declared at unit, namespace or
         * aggregate level is exported through the KDI and is therefore visible
         * to importing modules.  Declared inside a statement block, it is
         * implicitly private and restricted to that block.
         *
         * 'alias' is a soft alias: a pure convenience renaming of any symbol
         * (type, function, global/static variable).  Namespaces are excluded;
         * 'using N = namespace X;' remains the only way to alias a namespace.
         *
         * 'typedef' is a strong alias: it names a type that is nominally
         * distinct from its underlying type, even though both share the exact
         * same representation.
         */
        struct alias_decl : declaration, statement {
            /// Declaration specifiers ('public', 'protected', 'private') written
            /// directly in front of the declaration, as opposed to a block-level
            /// visibility declaration.
            std::vector<lex::keyword> specifiers;

            /// The 'alias' or 'typedef' keyword token.
            lex::keyword alias_kw;

            /// True for 'typedef' (strong alias), false for 'alias' (soft alias).
            bool is_strong = false;

            /// The name introduced by this declaration.
            lex::identifier name;

            /// Aliased symbol, for the 'alias' form (null for 'typedef').
            std::shared_ptr<qualified_identifier> qname;

            /// Aliased type specification, for the 'typedef' form (null for 'alias').
            std::shared_ptr<type_specifier> type;

            /// Template parameters of a parameterised alias, empty otherwise.
            /// Set by a 'template<...>' clause preceding the alias declaration.
            template_param_list template_params;
            /// Raw source text of a parameterised alias, from the 'template'
            /// keyword through the closing ';'. Round-tripped through KDI so an
            /// importing module can re-parse the declaration.
            std::string template_source_text;
            std::optional<lex::operator_> colon;
            std::optional<lex::punctuator> semicolon;

            /// True when the declaration is preceded by a 'template<...>' clause.
            bool is_template() const { return !template_params.empty(); }

            alias_decl(const lex::keyword& alias_kw, bool is_strong,
                       const lex::identifier& name,
                       std::shared_ptr<qualified_identifier> qname,
                       std::shared_ptr<type_specifier> type,
                       template_param_list template_params = {})
                : alias_kw(alias_kw), is_strong(is_strong), name(name),
                  qname(std::move(qname)), type(std::move(type)),
                  template_params(std::move(template_params)) {}

            void set_colon(const lex::operator_& c) { colon = c; }
            void set_semicolon(const lex::punctuator& sc) { semicolon = sc; }

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * Friend declaration — grants another entity access to protected members.
         *
         * Syntax: 'friend' ['struct'|'interface'|'class']? qualified_identifier ';'
         * Semantics: grants the specified entity (class, struct, interface, or function) access to the protected members of the enclosing aggregate (struct/class/interface).
         *
         * Only valid inside an aggregate (struct/class/interface) body.
         * The optional type filter restricts friendship to the specified element kind.
         */
        struct friend_decl : declaration {
            /// The 'friend' keyword token.
            lex::keyword friend_kw;

            /// Optional element type filter: STRUCT, INTERFACE, CLASS, or nullopt.
            std::optional<lex::keyword> element_filter;

            /// The qualified name of the friend entity.
            std::shared_ptr<qualified_identifier> qname;

            /// Optional explicit template argument list: e.g. <T> or <int,float>.
            /// Empty when has_explicit_template_args is false.
            template_arg_list template_args;

            /// True when '<' was written (even if template_args is empty, as in '<>').
            bool has_explicit_template_args = false;
            std::optional<lex::operator_> open_angle, close_angle;
            std::optional<lex::punctuator> semicolon;

            friend_decl(const lex::keyword& friend_kw,
                        const std::optional<lex::keyword>& element_filter,
                        std::shared_ptr<qualified_identifier> qname,
                        template_arg_list template_args = {},
                        bool has_explicit = false)
                : friend_kw(friend_kw), element_filter(element_filter),
                  qname(std::move(qname)),
                  template_args(std::move(template_args)),
                  has_explicit_template_args(has_explicit) {}

            void set_open_angle(const lex::operator_& op) { open_angle = op; }
            void set_close_angle(const lex::operator_& op) { close_angle = op; }
            void set_semicolon(const lex::punctuator& sc) { semicolon = sc; }

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * Annotation definition attached to a declaration.
         * Syntax: '@' qualified-identifier [ '(' [ExpressionList] ')' | DesignatedBraceInitList ]
         * Semantics: represents an annotation attached to a declaration, optionally with arguments or a brace initializer.
         *
         * Without parentheses or braces, default initialization is implicit.
         */
        struct annotation_def : ast_node {
            lex::punctuator at_sign;                          ///< The '@' token
            std::shared_ptr<qualified_identifier> name;       ///< Annotation type name (possibly qualified)
            std::shared_ptr<brace_init_list> brace_init;      ///< Optional designated/positional brace init (mutually exclusive with args)
            std::vector<expr_ptr> args;                       ///< Optional parenthesized argument list
            bool has_parens = false;                          ///< True when (...) was explicitly provided (distinguishes @Foo from @Foo())
            std::optional<lex::punctuator> open_paren, close_paren;

            annotation_def(const lex::punctuator& at_sign,
                           std::shared_ptr<qualified_identifier> name)
                : at_sign(at_sign), name(std::move(name)) {}

            annotation_def(const lex::punctuator& at_sign,
                           std::shared_ptr<qualified_identifier> name,
                           const std::vector<expr_ptr>& args)
                : at_sign(at_sign), name(std::move(name)), args(args), has_parens(true) {}

            annotation_def(const lex::punctuator& at_sign,
                           std::shared_ptr<qualified_identifier> name,
                           std::shared_ptr<brace_init_list> brace_init)
                : at_sign(at_sign), name(std::move(name)), brace_init(std::move(brace_init)) {}

            void set_open_paren(const lex::punctuator& p) { open_paren = p; }
            void set_close_paren(const lex::punctuator& p) { close_paren = p; }

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
          * Annotation initializer expression — wraps an annotation_def for use
          * inside expression contexts (brace-init lists, argument lists).
          * Syntax: '@' QualifiedIdentifier [ '(' [ExpressionList] ')' | DesignatedBraceInitList ]
          * Semantics: represents an annotation used as an expression, typically as an element of an array literal or argument list.
          * Example: @Tag("hello") used as an element of an array literal.
          */
        struct annotation_init_expr : expression {
            std::shared_ptr<annotation_def> annotation;

            annotation_init_expr(std::shared_ptr<annotation_def> annotation)
                : annotation(std::move(annotation)) {}

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * Pack expansion expression — wraps an expression followed by '...'.
         * Used in function call arguments to expand a parameter pack.
         * Syntax: expr '...'
         * Semantics: represents the expansion of a parameter pack in a function call or similar context.
         * Example: args... in f(args...)
         */
        struct pack_expansion_expr : expression {
            /** The inner expression being expanded (typically an identifier). */
            expr_ptr inner;
            std::optional<lex::punctuator> ellipsis;

            pack_expansion_expr(expr_ptr inner)
                : inner(std::move(inner)) {}

            void set_ellipsis(const lex::punctuator& el) { ellipsis = el; }

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * A single template parameter in a template declaration.
         * Syntax: TemplateParameterKind Identifier [ ':' TypeSpec ] [ '=' ConditionalExpr ]
         * Semantics: represents a single parameter in a template declaration, which can be either a type parameter (typename/struct/class/interface) or a value parameter (with an explicit type).
         *
         * kind_kw determines the parameter kind:
         *   - TYPENAME: any type
         *   - STRUCT/CLASS/INTERFACE: constrained to that aggregate kind
         *   - For value parameters, kind_kw is absent and value_type is set.
         */
        struct template_parameter : ast_node {
            /** The kind keyword: TYPENAME, STRUCT, CLASS, INTERFACE, or nullopt for value params. */
            std::optional<lex::keyword> kind_kw;
            /** Parameter name. */
            lex::identifier name;
            /** Optional constraint type (the type after ':'). */
            std::shared_ptr<type_specifier> constraint_type;
            /** Optional default value expression (for value parameters). */
            expr_ptr default_expr;
            /** Optional default type specifier (for type parameters, e.g. '= int'). */
            std::shared_ptr<type_specifier> default_type_spec;
            /** For value parameters: the explicit type specifier (e.g. 'unsigned int'). */
            std::shared_ptr<type_specifier> value_type;
            /** True if this is a parameter pack (e.g. typename... Ts). Only valid for type params. */
            bool is_pack = false;
            std::optional<lex::operator_> colon;
            std::optional<lex::operator_> equal_op;
            std::optional<lex::punctuator> ellipsis;

            // Type parameter constructor
            template_parameter(const lex::keyword& kind_kw,
                               const lex::identifier& name,
                               std::shared_ptr<type_specifier> constraint_type = nullptr,
                               std::shared_ptr<type_specifier> default_type_spec = nullptr,
                               bool is_pack = false)
                : kind_kw(kind_kw), name(name), constraint_type(std::move(constraint_type)),
                  default_type_spec(std::move(default_type_spec)), is_pack(is_pack) {}

            // Value parameter constructor
            template_parameter(std::shared_ptr<type_specifier> value_type,
                               const lex::identifier& name,
                               expr_ptr default_expr = nullptr)
                : name(name), default_expr(std::move(default_expr)),
                  value_type(std::move(value_type)) {}

            void set_colon(const lex::operator_& c) { colon = c; }
            void set_equal_op(const lex::operator_& eq) { equal_op = eq; }
            void set_ellipsis(const lex::punctuator& el) { ellipsis = el; }

            /** True if this is a type parameter (typename/struct/class/interface). */
            bool is_type_param() const { return kind_kw.has_value(); }
            /** True if this is a value parameter (e.g. N : unsigned int). */
            bool is_value_param() const { return !kind_kw.has_value(); }

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * Template parameter list: a list of template_parameter nodes.
         * Syntax: '<' TemplateParameter { ',' TemplateParameter } '>'
         * Semantics: represents the list of parameters in a template declaration.
         * Can be either a type argument or a value (expression) argument.
         */
        struct template_arg : ast_node {
            /** Type argument (mutually exclusive with value_arg). */
            std::shared_ptr<type_specifier> type_arg;
            /** Value argument expression (mutually exclusive with type_arg). */
            expr_ptr value_arg;

            // Type argument constructor
            explicit template_arg(std::shared_ptr<type_specifier> type_arg)
                : type_arg(std::move(type_arg)) {}

            // Value argument constructor
            explicit template_arg(expr_ptr value_arg)
                : value_arg(std::move(value_arg)) {}

            bool is_type() const { return type_arg != nullptr; }
            bool is_value() const { return value_arg != nullptr; }

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * Aggregate declaration: represents a struct, class, interface, annotation, or union.
         * Syntax: [Specifier] ('struct' | 'class' | 'interface' | 'annotation' | 'union') Identifier '{' { Declaration } '}'
         * Semantics: defines an aggregate type with the specified name and body of declarations. Can also be a template with parameters.
         */
        struct aggregate_decl : declaration {
            annotation_def_list annotations;
            /** Template parameters, empty if not a template. */
            template_param_list template_params;
            /**
             * True when the declaration uses the 'generic' keyword instead of 'template'.
             * When true, only type parameters are allowed (no value parameters), and all
             * uses of type params in the body must be via addressers.
             */
            bool is_generic = false;
            /** Raw K source text of the complete template declaration (from 'template' keyword
             *  through closing '}'), captured for KDI export. Empty if not a template. */
            std::string template_source_text;
            std::vector <lex::keyword> specifiers;
            lex::keyword kw_aggregate_type;
            lex::punctuator open_brace, close_brace;
            lex::identifier name;
            std::vector <decl_ptr> declarations;
            std::optional<lex::operator_> colon;

            /**
             * A single entry in the base-class clause.
             * E.g. "public Base" or "public ns::Base" (visibility is optional).
             */
            struct base_clause_entry {
                std::optional<lex::keyword> visibility_kw; ///< 'public', 'protected', 'private' or absent
                lex::identifier name;                      ///< first component of the base-class name
                std::string     qualified_name;            ///< full qualified name "ns::Base" (or just "Base")
            };
            /** Base-class clause entries, in declaration order. Empty if no inheritance. */
            std::vector<base_clause_entry> bases;

            /** True if this declaration uses the 'class' keyword (vs 'struct'). */
            bool is_class() const { return kw_aggregate_type.type == lex::keyword::CLASS; }
            bool is_struct() const { return kw_aggregate_type.type == lex::keyword::STRUCT; }
            bool is_interface() const { return kw_aggregate_type.type == lex::keyword::INTERFACE; }
            bool is_annotation() const { return kw_aggregate_type.type == lex::keyword::ANNOTATION; }
            bool is_union() const { return kw_aggregate_type.type == lex::keyword::UNION; }
            bool is_template() const { return !template_params.empty(); }

            aggregate_decl(const std::vector <lex::keyword>& specifiers,
                            const lex::keyword& kw_aggregate_type,
                            const lex::identifier& name,
                            const std::vector<base_clause_entry>& bases,
                            const std::vector <decl_ptr> &declarations,
                            const annotation_def_list& annotations = {}) :
                    annotations(annotations), specifiers(specifiers), kw_aggregate_type(kw_aggregate_type),
                    open_brace(lex::punctuator{std::string_view("{"), lex::punctuator::BRACE_OPEN}),
                    close_brace(lex::punctuator{std::string_view("}"), lex::punctuator::BRACE_CLOSE}),
                    name(name), bases(bases), declarations(declarations) {}

            aggregate_decl(const std::vector <lex::keyword>& specifiers,
                            const lex::keyword& kw_aggregate_type,
                            const lex::punctuator& open_brace,
                            const lex::punctuator& close_brace,
                            const lex::identifier& name,
                            const std::vector <decl_ptr> &declarations,
                            const annotation_def_list& annotations = {}) :
                    annotations(annotations), specifiers(specifiers), kw_aggregate_type(kw_aggregate_type), open_brace(open_brace), close_brace(close_brace), name(name), declarations(declarations) {}

            aggregate_decl(const std::vector <lex::keyword>& specifiers,
                            const lex::keyword& kw_aggregate_type,
                            const lex::punctuator& open_brace,
                            const lex::punctuator& close_brace,
                            const lex::identifier& name,
                            const std::vector<base_clause_entry>& bases,
                            const std::vector <decl_ptr> &declarations,
                            const annotation_def_list& annotations = {}) :
                    annotations(annotations), specifiers(specifiers), kw_aggregate_type(kw_aggregate_type), open_brace(open_brace), close_brace(close_brace), name(name), bases(bases), declarations(declarations) {}

            aggregate_decl(std::vector <lex::keyword>&& specifiers,
                            lex::keyword&& kw_aggregate_type,
                            lex::punctuator&& open_brace,
                            lex::punctuator&& close_brace,
                            lex::identifier &&name,
                            std::vector <decl_ptr> &&declarations,
                            annotation_def_list&& annotations = {}) :
                    annotations(std::move(annotations)), specifiers(specifiers), kw_aggregate_type(kw_aggregate_type), open_brace(open_brace), close_brace(close_brace), name(name), declarations(declarations) {}

            void set_open_brace(const lex::punctuator& ob) { open_brace = ob; }
            void set_close_brace(const lex::punctuator& cb) { close_brace = cb; }
            void set_colon(const lex::operator_& c) { colon = c; }

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * A single designated member initializer inside a brace-init list.
         * Syntax: '.' [Base::] member_name '=' expr  or  '.' [Base::] member_name '(' args... ')'
         * Semantics: represents a single member initialization in a designated initializer list, either as an assignment or constructor call. The optional qualifier disambiguates inherited members.
         * Represents either:
         *   - Assignment form:    .member = expr
         *   - Constructor form:   .member(args...)
         *   - Qualified form:     .Base::member = expr  or  .Base::member(args...)
         */
        struct designated_init_element : expression {
            lex::operator_ dot;             ///< The '.' token
            lex::identifier member_name;    ///< The member name (last component)
            /** Optional qualifier for disambiguating inherited members (e.g. "Base" in ".Base::member"). */
            std::vector<lex::identifier> qualifier;
            bool is_call_form;              ///< true → .m(args), false → .m = expr
            expr_ptr value;                 ///< Assignment form: the expression after '='
            std::vector<expr_ptr> args;     ///< Constructor form: the arguments between '(' ')'
            std::optional<lex::operator_> equal_op;
            std::optional<lex::punctuator> open_paren, close_paren;

            // Assignment form constructor
            designated_init_element(const lex::operator_& dot,
                                    const lex::identifier& member_name,
                                    const std::vector<lex::identifier>& qualifier,
                                    const expr_ptr& value)
                : dot(dot), member_name(member_name), qualifier(qualifier),
                  is_call_form(false), value(value) {}

            // Constructor form constructor
            designated_init_element(const lex::operator_& dot,
                                    const lex::identifier& member_name,
                                    const std::vector<lex::identifier>& qualifier,
                                    const std::vector<expr_ptr>& args)
                : dot(dot), member_name(member_name), qualifier(qualifier),
                  is_call_form(true), args(args) {}

            void set_equal_op(const lex::operator_& eq) { equal_op = eq; }
            void set_open_paren(const lex::punctuator& p) { open_paren = p; }
            void set_close_paren(const lex::punctuator& p) { close_paren = p; }

            /** Returns the fully-qualified member name string (e.g. "Base::x" or just "x"). */
            std::string qualified_member_name() const {
                std::string result;
                for (auto& q : qualifier) {
                    result += std::string{q.content} + "::";
                }
                result += std::string{member_name.content};
                return result;
            }

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * Brace initializer list expression.
         * Syntax: '{' [ DesignatedInitElement { ',' DesignatedInitElement } ] '}'
         * Semantics: represents a list of expressions enclosed in braces, typically used for aggregate initialization. Each element can be a designated initializer or a regular expression. Empty slots (represented by nullptr) indicate default initialization.
         * Represents a comma-separated list of expressions inside braces: { e1, e2, ..., eN }
         * An empty slot (two consecutive commas, or trailing comma before '}') yields a nullptr entry
         * to represent default construction.
         * Used for array initialization: arr : int[3] { 1, 2, 3 };
         * When is_designated is true, every element is a designated_init_element.
         */
        struct brace_init_list : expression {
            lex::punctuator open_brace, close_brace;
            /** Element initializer expressions. nullptr entries represent empty (default-init) slots. */
            std::vector<expr_ptr> elements;
            /** True when this brace-init list uses designated member initializers (.member = expr). */
            bool is_designated = false;

            brace_init_list(const std::vector<expr_ptr>& elements,
                            bool is_designated = false)
                : open_brace(lex::punctuator{std::string_view("{"), lex::punctuator::BRACE_OPEN}),
                  close_brace(lex::punctuator{std::string_view("}"), lex::punctuator::BRACE_CLOSE}),
                  elements(elements), is_designated(is_designated) {}

            brace_init_list(const lex::punctuator& open_brace,
                            const lex::punctuator& close_brace,
                            const std::vector<expr_ptr>& elements,
                            bool is_designated = false)
                : open_brace(open_brace), close_brace(close_brace), elements(elements),
                  is_designated(is_designated) {}

            void set_open_brace(const lex::punctuator& ob) { open_brace = ob; }
            void set_close_brace(const lex::punctuator& cb) { close_brace = cb; }

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * Variable declaration: represents a variable declaration in a function or block.
         * Syntax: [Specifier] Identifier ':' TypeSpec ['=' ConditionalExpr] ';'
         * Semantics: declares a variable with the specified name, type, and optional initializer.
         */
        struct variable_decl : declaration, statement {
            std::vector <lex::keyword> specifiers;
            lex::identifier name;
            std::shared_ptr<ast::type_specifier> type;
            expr_ptr init;
            bool is_constructor = false;
            bool is_brace_init = false;

            /** True when this is a uniform array init: var : T(args)[N]; */
            bool is_uniform_array_init = false;
            /** Constructor arguments for uniform array init (each element gets these). */
            std::vector<expr_ptr> uniform_ctor_args;
            /** Array size expression for uniform array init. */
            expr_ptr uniform_array_size;
            std::optional<lex::operator_> colon;
            std::optional<lex::operator_> equal_op;
            std::optional<lex::punctuator> open_paren, close_paren;
            std::optional<lex::punctuator> semicolon;

            variable_decl(const std::vector <lex::keyword> &specifiers, const lex::identifier &name,
                          const std::shared_ptr<ast::type_specifier> &type, expr_ptr init = nullptr, bool is_constructor = false, bool is_brace_init = false) :
                    specifiers(specifiers), name(name), type(type), init(init), is_constructor(is_constructor), is_brace_init(is_brace_init) {}

            variable_decl(std::vector <lex::keyword> &&specifiers, lex::identifier &&name, std::shared_ptr<ast::type_specifier> &&type, expr_ptr init, bool is_constructor, bool is_brace_init = false) :
                    specifiers(specifiers), name(name), type(type), init(init), is_constructor(is_constructor), is_brace_init(is_brace_init) {}

            void set_colon(const lex::operator_& c) { colon = c; }
            void set_equal_op(const lex::operator_& eq) { equal_op = eq; }
            void set_open_paren(const lex::punctuator& p) { open_paren = p; }
            void set_close_paren(const lex::punctuator& p) { close_paren = p; }
            void set_semicolon(const lex::punctuator& sc) { semicolon = sc; }

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * Function parameter specification: represents a single parameter in a function declaration.
         * Syntax: [Annotation] [Specifier] Identifier ':' TypeSpec ['=' ConditionalExpr] | '...' | 'Ts... args'
         * Semantics: defines a single parameter for a function, including its name, type, optional default value, and whether it is a varargs or pack expansion parameter.
         */
        struct parameter_spec : ast_node {
            /** Annotations applied to this parameter (parsed before specifiers). */
            annotation_def_list annotations;
            std::vector <lex::keyword> specifiers;
            std::optional <lex::identifier> name;
            std::shared_ptr<ast::type_specifier> type;
            /** Optional default value expression (e.g. '= 42' or '= a + 1'). */
            expr_ptr default_expr;
            /** True when declared with '...' (varargs parameter). */
            bool is_varargs = false;
            /** True when declared as a pack expansion parameter (e.g. Ts... args). */
            bool is_pack_expansion = false;
            std::optional<lex::operator_> colon;
            std::optional<lex::operator_> equal_op;
            std::optional<lex::punctuator> ellipsis;

            parameter_spec(annotation_def_list annotations,
                           const std::vector <lex::keyword> &specifiers, const std::optional <lex::identifier> &name,
                           const std::shared_ptr<ast::type_specifier> &type, expr_ptr default_expr = nullptr,
                           bool is_varargs = false, bool is_pack_expansion = false) :
                    annotations(std::move(annotations)), specifiers(specifiers), name(name), type(type),
                    default_expr(std::move(default_expr)), is_varargs(is_varargs), is_pack_expansion(is_pack_expansion) {}

            parameter_spec(annotation_def_list annotations,
                           std::vector <lex::keyword> &&specifiers, std::optional <lex::identifier> &&name,
                           std::shared_ptr<ast::type_specifier> &&type, expr_ptr default_expr = nullptr,
                           bool is_varargs = false, bool is_pack_expansion = false) :
                    annotations(std::move(annotations)), specifiers(specifiers), name(name), type(type),
                    default_expr(std::move(default_expr)), is_varargs(is_varargs), is_pack_expansion(is_pack_expansion) {}

            void set_colon(const lex::operator_& c) { colon = c; }
            void set_equal_op(const lex::operator_& eq) { equal_op = eq; }
            void set_ellipsis(const lex::punctuator& el) { ellipsis = el; }

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * A single member initializer in a constructor's mem-initializer-list.
         * Syntax: member_name '(' [ExpressionList] ')' | member_name '=' ConditionalExpr
         * Semantics: represents a single member initialization in a constructor's member initializer list, either as a constructor call or an assignment.
         * The member name can be qualified to disambiguate inherited members
         * E.g. the "x(a+1)" part in "ctor() : x(a+1), y(b) { ... }"
         */
        struct member_initializer {
            lex::identifier name;
            std::vector<std::shared_ptr<ast::expression>> args;

            member_initializer(const lex::identifier &name, const std::vector<std::shared_ptr<ast::expression>> &args) :
                    name(name), args(args) {}
            member_initializer(lex::identifier &&name, std::vector<std::shared_ptr<ast::expression>> &&args) :
                    name(std::move(name)), args(std::move(args)) {}
        };

        /**
         * Function declaration: represents a function declaration or definition.
         * Syntax: [Annotation] [Specifier] Identifier '(' [ParameterList] ')' [':' TypeSpec] ['->' (default|delete|qualifiedId)] ['throws' QualifiedIdentifierList] ['{' BlockStatement '}']
         * Semantics: defines a function with the specified name, parameters, return type, and body. Can also be a template with parameters. Supports aliasing (default/delete/redirect) and exception specifications.
         */
        struct function_decl : declaration {
            /** Specifier for function-aliasing declarations (-> default ; / -> delete ; / -> target ;). */
            enum class aliasing_spec_t { NONE, DEFAULT, DELETE, REDIRECT };

            /** Annotations applied to this function (parsed before specifiers). */
            annotation_def_list annotations;
            /** Template parameters, empty if not a template function. */
            template_param_list template_params;
            /** Raw K source text of the complete template function declaration (from 'template' keyword
             *  through closing '}'), captured for KDI export. Empty if not a template. */
            std::string template_source_text;
            std::vector<lex::keyword> specifiers;
            lex::identifier name;
            lex::opt_any_lexeme operator_; /** Lexeme for the operator keyword, if this is an operator function declaration. */
            lex::opt_any_lexeme operator_symbol; /** Lexeme for the operator symbol, if this is an operator function declaration. */
            std::shared_ptr<ast::type_specifier> type;
            std::vector<std::shared_ptr<parameter_spec>> params;
            /** Member initializer list (only for constructors). */
            std::vector<member_initializer> member_inits;
            std::shared_ptr<block_statement> content;
            bool is_destructor = false;
            /** True if this is an operator function declaration (e.g. operator +(...)). */
            bool is_operator = false;
            /** Aliasing specifier: NONE = regular body, DEFAULT = -> default ;, DELETE = -> delete ;, REDIRECT = -> target ; */
            aliasing_spec_t aliasing_spec = aliasing_spec_t::NONE;

            /** Target function for REDIRECT aliasing: the qualified identifier of the target function. */
            std::shared_ptr<qualified_identifier> redirect_target;
            /** Optional parameter types for disambiguation when redirecting to an overloaded function. */
            std::vector<std::shared_ptr<type_specifier>> redirect_param_types;
            /** True if redirect_param_types was explicitly provided (even if empty). */
            bool redirect_has_param_types = false;

            /** True if this is a template function declaration. */
            bool is_template() const { return !template_params.empty(); }

            /**
             * True when the declaration uses the 'generic' keyword instead of 'template'.
             * When true, only type parameters are allowed (no value parameters), and all
             * uses of type params must be via addressers.
             */
            bool is_generic = false;

            /** Named return variable — present when function uses named return syntax:
             *  func(params) retVarName : RetType [ Initialiser ] { body }
             */
            bool has_named_return = false;
            /** The identifier for the named return variable. */
            std::optional<lex::identifier> return_var_name;
            /** Init expression for the named return variable (assignment-style: = expr). */
            expr_ptr return_var_init_expr;
            /** True when init is constructor-style (args...) vs assignment-style (= expr). */
            bool return_var_is_ctor_init = false;

            /** Exception specification: list of exception types this function may throw.
             *  Empty means the function is implicitly noexcept. */
            std::vector<std::shared_ptr<qualified_identifier>> throws_spec;
            /** True if a 'throws' clause was explicitly written. */
            bool has_throws_clause = false;

            std::optional<lex::operator_> tilde;
            std::optional<lex::punctuator> open_paren, close_paren;
            std::optional<lex::operator_> colon;
            std::optional<lex::operator_> arrow;
            std::optional<lex::keyword> throws_kw;
            std::optional<lex::punctuator> semicolon;

            function_decl(const std::vector <lex::keyword> &specifiers, const lex::identifier &name,
                          const std::shared_ptr<ast::type_specifier> &type, const std::vector<std::shared_ptr<parameter_spec>> &params,
                          const std::shared_ptr <block_statement> &content, bool is_destructor = false) :
                    specifiers(specifiers), name(name), type(type), params(params), content(content), is_destructor(is_destructor) {}

            function_decl(const std::vector <lex::keyword> &specifiers, const lex::identifier &name,
                          const std::shared_ptr<ast::type_specifier> &type, const std::vector<std::shared_ptr<parameter_spec>> &params,
                          const std::vector<member_initializer> &member_inits,
                          const std::shared_ptr <block_statement> &content, bool is_destructor = false) :
                    specifiers(specifiers), name(name), type(type), params(params), member_inits(member_inits), content(content), is_destructor(is_destructor) {}

            function_decl(std::vector <lex::keyword> &&specifiers, lex::identifier &&name,
                          std::shared_ptr<ast::type_specifier> &&type, std::vector<std::shared_ptr<parameter_spec>> &&params,
                          std::vector<member_initializer> &&member_inits,
                          std::shared_ptr <block_statement> &&content, bool is_destructor = false) :
                    specifiers(specifiers), name(name), type(type), params(params), member_inits(std::move(member_inits)), content(content), is_destructor(is_destructor) {}

            /** Constructor for aliasing declarations (-> default ; / -> delete ;). No content block. */
            function_decl(const std::vector <lex::keyword> &specifiers, const lex::identifier &name,
                          const std::vector<std::shared_ptr<parameter_spec>> &params,
                          aliasing_spec_t aliasing) :
                    specifiers(specifiers), name(name), params(params), aliasing_spec(aliasing) {}

            /** Constructor for redirect declarations (-> qualifiedId ; or -> qualifiedId(types...) ;). */
            function_decl(const std::vector<lex::keyword> &specifiers, const lex::identifier &name,
                          const std::shared_ptr<ast::type_specifier> &type,
                          const std::vector<std::shared_ptr<parameter_spec>> &params,
                          const std::shared_ptr<qualified_identifier> &redirect_target,
                          const std::vector<std::shared_ptr<type_specifier>> &redirect_param_types,
                          bool redirect_has_param_types) :
                    specifiers(specifiers), name(name), type(type), params(params),
                    aliasing_spec(aliasing_spec_t::REDIRECT),
                    redirect_target(redirect_target),
                    redirect_param_types(redirect_param_types),
                    redirect_has_param_types(redirect_has_param_types) {}

            void set_tilde(const lex::operator_& t) { tilde = t; }
            void set_open_paren(const lex::punctuator& p) { open_paren = p; }
            void set_close_paren(const lex::punctuator& p) { close_paren = p; }
            void set_colon(const lex::operator_& c) { colon = c; }
            void set_arrow(const lex::operator_& a) { arrow = a; }
            void set_throws_kw(const lex::keyword& kw) { throws_kw = kw; }
            void set_semicolon(const lex::punctuator& sc) { semicolon = sc; }

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * A single entry in an enum declaration.
         * Syntax: identifier
         *       , [ '=' , ( literal | identifier )
         *         | '(' , [ ExpressionList ] , ')'
         *         | BraceInitList ]
         *       , [ 'default' ] , ';'
         * Semantics: represents a single enumerator in an enum declaration, optionally with an explicit value (either a literal or a reference to another enumerator),
         * constructor-style arguments, or a brace initializer.
         * The 'default' keyword indicates that this entry is the default enumerator for the enum.
         */
        struct enum_entry : ast_node {
            lex::identifier name;
            /** Optional explicit value: either an integer literal or a reference to another entry name. */
            std::optional<lex::any_literal> literal_value;
            /** Optional reference to another entry (by identifier). Mutually exclusive with literal_value in practice. */
            std::optional<lex::identifier> ref_value;
            /** Optional constructor-style initializer arguments: VALUE(...). */
            std::vector<expr_ptr> ctor_args;
            /** Optional brace initializer: VALUE{...}. */
            std::shared_ptr<brace_init_list> brace_init;
            /** True when constructor-style initialization syntax VALUE(...) was explicitly written. */
            bool has_paren_init = false;
            /** True when the 'default' keyword follows the entry. */
            bool is_default = false;
            std::optional<lex::operator_> equal_op;
            std::optional<lex::keyword> default_kw;
            std::optional<lex::punctuator> open_paren, close_paren;
            std::optional<lex::punctuator> semicolon;

            enum_entry(const lex::identifier& name,
                       const std::optional<lex::any_literal>& literal_value,
                       const std::optional<lex::identifier>& ref_value,
                       bool is_default,
                       const std::vector<expr_ptr>& ctor_args = {},
                       std::shared_ptr<brace_init_list> brace_init = nullptr,
                       bool has_paren_init = false)
                : name(name), literal_value(literal_value), ref_value(ref_value),
                  ctor_args(ctor_args), brace_init(std::move(brace_init)),
                  has_paren_init(has_paren_init), is_default(is_default) {}

            void set_equal_op(const lex::operator_& eq) { equal_op = eq; }
            void set_default_kw(const lex::keyword& kw) { default_kw = kw; }
            void set_open_paren(const lex::punctuator& p) { open_paren = p; }
            void set_close_paren(const lex::punctuator& p) { close_paren = p; }
            void set_semicolon(const lex::punctuator& sc) { semicolon = sc; }

            bool has_literal_initializer() const { return literal_value.has_value(); }
            bool has_ref_initializer() const { return ref_value.has_value(); }
            bool has_paren_initializer() const { return has_paren_init; }
            bool has_brace_initializer() const { return brace_init != nullptr; }
            bool has_explicit_initializer() const {
                return has_literal_initializer() || has_ref_initializer() || has_paren_initializer() || has_brace_initializer();
            }

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * Enum declaration.
         * Syntax: SPECIFIERS 'enum' identifier [ ':' TypeSpec ] '{' ENUM_ENTRY* '}' ';'
         * Semantics: defines an enumeration type with the specified name, optional underlying type, and a list of enumerator entries.
         * Can also specify a base enum for inheritance.
         */
        struct enum_decl : declaration {
            std::vector<lex::keyword> specifiers;
            lex::keyword kw_enum;
            lex::identifier name;
            /** Optional explicit type written after ':'. Kept even when it may later resolve as a base enum name. */
            std::shared_ptr<type_specifier> explicit_underlying_type;
            /** Optional base enum name for enum derivation (e.g. "Base" or "ns::Base"). */
            std::optional<std::string> base_name;
            lex::punctuator open_brace, close_brace;
            std::vector<std::shared_ptr<enum_entry>> entries;
            std::optional<lex::operator_> colon;
            std::optional<lex::punctuator> semicolon;

            enum_decl(const std::vector<lex::keyword>& specifiers,
                      const lex::keyword& kw_enum,
                      const lex::identifier& name,
                      std::shared_ptr<type_specifier> explicit_underlying_type,
                      const std::optional<std::string>& base_name,
                      const std::vector<std::shared_ptr<enum_entry>>& entries)
                : specifiers(specifiers), kw_enum(kw_enum), name(name),
                  explicit_underlying_type(std::move(explicit_underlying_type)),
                  base_name(base_name),
                  open_brace(lex::punctuator{std::string_view("{"), lex::punctuator::BRACE_OPEN}),
                  close_brace(lex::punctuator{std::string_view("}"), lex::punctuator::BRACE_CLOSE}),
                  entries(entries) {}

            enum_decl(const std::vector<lex::keyword>& specifiers,
                      const lex::keyword& kw_enum,
                      const lex::identifier& name,
                      std::shared_ptr<type_specifier> explicit_underlying_type,
                      const std::optional<std::string>& base_name,
                      const lex::punctuator& open_brace,
                      const lex::punctuator& close_brace,
                      const std::vector<std::shared_ptr<enum_entry>>& entries)
                : specifiers(specifiers), kw_enum(kw_enum), name(name),
                  explicit_underlying_type(std::move(explicit_underlying_type)),
                  base_name(base_name),
                  open_brace(open_brace), close_brace(close_brace), entries(entries) {}

            void set_open_brace(const lex::punctuator& ob) { open_brace = ob; }
            void set_close_brace(const lex::punctuator& cb) { close_brace = cb; }
            void set_colon(const lex::operator_& c) { colon = c; }
            void set_semicolon(const lex::punctuator& sc) { semicolon = sc; }

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

        /**
         * Module name: represents the module declaration at the top of a source file.
         * Syntax: 'module' QualifiedIdentifier ';'
         * Semantics: specifies the module name for the source file, which is used for module imports and visibility.
         */
        struct module_name : ast_node {
            lex::keyword module;
            std::shared_ptr<qualified_identifier> qname;
            std::optional<lex::punctuator> semicolon;

            module_name(const lex::keyword& module, const std::shared_ptr<ast::qualified_identifier>& qname):
                module(module), qname(qname) {};
            module_name(lex::keyword&& module, std::shared_ptr<ast::qualified_identifier>&& qname):
                    module(module), qname(qname) {};

            void set_semicolon(const lex::punctuator& sc) { semicolon = sc; }

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };


        /**
         * Unit.
         * Syntax: [ModuleName] { ImportDeclaration } { Declaration }
         * Semantics: represents a complete source file, including an optional module declaration, zero or more import declarations, and zero or more other declarations.
         */
        struct unit : ast_node {
            /** Unit module name, if any, null otherwise */
            std::shared_ptr<ast::module_name> module_name;

            /** Import declarations */
            std::vector<std::shared_ptr<import>> imports;

            // TODO remove it:
            std::vector <decl_ptr> declarations;

            void visit(ast_visitor &visitor) override;
            lex::opt_any_lexeme get_first_lexeme() const override;
            lex::opt_any_lexeme get_last_lexeme() const override;
            lex::opt_any_lexeme get_interest_lexeme() const override;
        };

    } // namespace ast

    class ast_visitor {
    public:
        virtual void visit_unit(ast::unit &) = 0;
        virtual void visit_module_name(ast::module_name &) = 0;
        virtual void visit_import(ast::import &) = 0;

        virtual void visit_identified_type_specifier(ast::identified_type_specifier &) = 0;
        virtual void visit_keyword_type_specifier(ast::keyword_type_specifier &) = 0;
        virtual void visit_array_type_specifier(ast::array_type_specifier &) = 0;
        virtual void visit_pointer_type_specifier(ast::pointer_type_specifier &) = 0;
        virtual void visit_const_type_specifier(ast::const_type_specifier &) = 0;
        virtual void visit_callable_type_specifier(ast::callable_type_specifier &) = 0;
        virtual void visit_owner_type_specifier(ast::owner_type_specifier &) = 0;

        virtual void visit_parameter_specifier(ast::parameter_spec &) = 0;

        virtual void visit_qualified_identifier(ast::qualified_identifier &) = 0;

        virtual void visit_visibility_decl(ast::visibility_decl &) = 0;
        virtual void visit_namespace_decl(ast::namespace_decl &) = 0;
        virtual void visit_using_decl(ast::using_decl &) = 0;
        virtual void visit_alias_decl(ast::alias_decl &) = 0;
        virtual void visit_friend_decl(ast::friend_decl &) = 0;
        virtual void visit_aggregate_decl(ast::aggregate_decl &) = 0;
        virtual void visit_enum_decl(ast::enum_decl &) = 0;
        virtual void visit_variable_decl(ast::variable_decl &) = 0;
        virtual void visit_function_decl(ast::function_decl &) = 0;

        virtual void visit_block_statement(ast::block_statement &) = 0;
        virtual void visit_return_statement(ast::return_statement &) = 0;
        virtual void visit_break_statement(ast::break_statement &) = 0;
        virtual void visit_continue_statement(ast::continue_statement &) = 0;
        virtual void visit_throw_statement(ast::throw_statement &) = 0;
        virtual void visit_try_catch_statement(ast::try_catch_statement &) = 0;
        virtual void visit_catch_clause(ast::catch_clause &) = 0;
        virtual void visit_if_else_statement(ast::if_else_statement &) = 0;
        virtual void visit_while_statement(ast::while_statement &) = 0;
        virtual void visit_for_statement(ast::for_statement &) = 0;
        virtual void visit_foreach_statement(ast::foreach_statement &) = 0;
        virtual void visit_expression_statement(ast::expression_statement &) = 0;

        virtual void visit_literal_expr(ast::literal_expr &) = 0;
        virtual void visit_keyword_expr(ast::keyword_expr &) = 0;
        virtual void visit_this_expr(ast::keyword_expr &) = 0;
        virtual void visit_expr_list_expr(ast::expr_list_expr &) = 0;
        virtual void visit_conditional_expr(ast::conditional_expr &) = 0;
        virtual void visit_binary_operator_expr(ast::binary_operator_expr &) = 0;

        virtual void visit_cast_expr(ast::cast_expr &) = 0;
        virtual void visit_unary_prefix_expr(ast::unary_prefix_expr &) = 0;
        virtual void visit_unary_postfix_expr(ast::unary_postfix_expr &) = 0;
        virtual void visit_bracket_postifx_expr(ast::bracket_postifx_expr &) = 0;
        virtual void visit_parenthesis_postifx_expr(ast::parenthesis_postifx_expr &) = 0;
        virtual void visit_member_access_postfix_expr(ast::member_access_postfix_expr &) = 0;
        virtual void visit_brace_postfix_expr(ast::brace_postfix_expr &) = 0;
        virtual void visit_lambda_capture(ast::lambda_capture &) = 0;
        virtual void visit_lambda_expression(ast::lambda_expression &) = 0;
        virtual void visit_identifier_expr(ast::identifier_expr &) = 0;

        virtual void visit_new_expr(ast::new_expr &) = 0;
        virtual void visit_delete_expr(ast::delete_expr &) = 0;

        virtual void visit_brace_init_list(ast::brace_init_list &) = 0;
        virtual void visit_designated_init_element(ast::designated_init_element &) = 0;

        virtual void visit_annotation_def(ast::annotation_def &) = 0;
        virtual void visit_annotation_init_expr(ast::annotation_init_expr &) = 0;
        virtual void visit_pack_expansion_expr(ast::pack_expansion_expr &) = 0;

        virtual void visit_template_parameter(ast::template_parameter &) = 0;
        virtual void visit_template_arg(ast::template_arg &) = 0;

        virtual void visit_comma_expr(ast::expr_list_expr &) = 0;

    };

    class default_ast_visitor : public ast_visitor {
    public:
        void visit_unit(ast::unit &) override;
        void visit_module_name(ast::module_name &) override;
        void visit_import(ast::import &) override;

        void visit_identified_type_specifier(ast::identified_type_specifier &) override;
        void visit_keyword_type_specifier(ast::keyword_type_specifier &) override;
        void visit_array_type_specifier(ast::array_type_specifier &) override;
        void visit_pointer_type_specifier(ast::pointer_type_specifier &) override;
        void visit_const_type_specifier(ast::const_type_specifier &) override;
        void visit_callable_type_specifier(ast::callable_type_specifier &) override;
        void visit_owner_type_specifier(ast::owner_type_specifier &) override;

        void visit_parameter_specifier(ast::parameter_spec &) override;
        void visit_qualified_identifier(ast::qualified_identifier &) override;

        void visit_visibility_decl(ast::visibility_decl &) override;
        void visit_namespace_decl(ast::namespace_decl &) override;
        void visit_using_decl(ast::using_decl &) override;
        void visit_alias_decl(ast::alias_decl &) override;
        void visit_friend_decl(ast::friend_decl &) override;
        void visit_aggregate_decl(ast::aggregate_decl &) override;
        void visit_enum_decl(ast::enum_decl &) override;
        void visit_variable_decl(ast::variable_decl &) override;
        void visit_function_decl(ast::function_decl &) override;

        void visit_block_statement(ast::block_statement &) override;
        void visit_return_statement(ast::return_statement &) override;
        void visit_break_statement(ast::break_statement &) override;
        void visit_continue_statement(ast::continue_statement &) override;
        void visit_throw_statement(ast::throw_statement &) override;
        void visit_try_catch_statement(ast::try_catch_statement &) override;
        void visit_catch_clause(ast::catch_clause &) override;
        void visit_if_else_statement(ast::if_else_statement &) override;
        void visit_while_statement(ast::while_statement &) override;
        void visit_for_statement(ast::for_statement &) override;
        void visit_foreach_statement(ast::foreach_statement &) override;
        void visit_expression_statement(ast::expression_statement &) override;

        void visit_literal_expr(ast::literal_expr &) override;
        void visit_keyword_expr(ast::keyword_expr &) override;
        void visit_this_expr(ast::keyword_expr &) override;
        void visit_expr_list_expr(ast::expr_list_expr &) override;
        void visit_conditional_expr(ast::conditional_expr &) override;
        void visit_binary_operator_expr(ast::binary_operator_expr &) override;

        void visit_cast_expr(ast::cast_expr &) override;
        void visit_unary_prefix_expr(ast::unary_prefix_expr &) override;
        void visit_unary_postfix_expr(ast::unary_postfix_expr &) override;
        void visit_bracket_postifx_expr(ast::bracket_postifx_expr &) override;
        void visit_parenthesis_postifx_expr(ast::parenthesis_postifx_expr &) override;
        void visit_member_access_postfix_expr(ast::member_access_postfix_expr &) override;
        void visit_brace_postfix_expr(ast::brace_postfix_expr &) override;
        void visit_lambda_capture(ast::lambda_capture &) override;
        void visit_lambda_expression(ast::lambda_expression &) override;
        void visit_identifier_expr(ast::identifier_expr &) override;

        void visit_new_expr(ast::new_expr &) override;
        void visit_delete_expr(ast::delete_expr &) override;

        void visit_brace_init_list(ast::brace_init_list &) override;
        void visit_designated_init_element(ast::designated_init_element &) override;

        void visit_annotation_def(ast::annotation_def &) override;
        void visit_annotation_init_expr(ast::annotation_init_expr &) override;
        void visit_pack_expansion_expr(ast::pack_expansion_expr &) override;

        void visit_template_parameter(ast::template_parameter &) override;
        void visit_template_arg(ast::template_arg &) override;

        void visit_comma_expr(ast::expr_list_expr &) override;
    };

} // namespace k::parse
#endif //KLANG_AST_HPP
