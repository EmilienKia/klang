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

    struct ast_node  : public std::enable_shared_from_this<ast_node> {
            virtual void visit(ast_visitor &visitor) = 0;

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

        /** List of annotation definitions attached to a declaration. */
        using annotation_def_list = std::vector<std::shared_ptr<annotation_def>>;

        struct import : public ast_node {
            lex::keyword import_kw;
            std::shared_ptr<qualified_identifier> qname;

            import(const lex::keyword& import_kw, std::shared_ptr<qualified_identifier> qname)
                : import_kw(import_kw), qname(std::move(qname)) {}

            virtual void visit(ast_visitor &visitor) override;
        };

        struct qualified_identifier : public ast_node {
            std::optional <lex::punctuator> initial_doublecolon;
            std::vector <lex::identifier> names;

            qualified_identifier(const std::optional <lex::punctuator> &initial_doublecolon,
                                 const std::vector <lex::identifier> &names) :
                    initial_doublecolon(initial_doublecolon), names(names) {}

            qualified_identifier(std::optional <lex::punctuator> &&initial_doublecolon,
                                 std::vector <lex::identifier> &&names) :
                    initial_doublecolon(initial_doublecolon), names(names) {}

            virtual void visit(ast_visitor &visitor) override;

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
        };

        struct type_specifier : public ast_node {

        };

        struct identified_type_specifier : public type_specifier {
            qualified_identifier name;

            identified_type_specifier(const qualified_identifier &name) : name(name) {}

            identified_type_specifier(qualified_identifier &&name) : name(name) {}

            virtual void visit(ast_visitor &visitor) override;

        };

        struct keyword_type_specifier : public type_specifier {
            lex::keyword keyword;
            bool is_unsigned = false;

            keyword_type_specifier(const lex::keyword & keyword, bool is_unsigned = false) : keyword(keyword), is_unsigned(is_unsigned) {}
            keyword_type_specifier(lex::keyword && keyword, bool is_unsigned = false) : keyword(keyword), is_unsigned(is_unsigned) {}

            virtual void visit(ast_visitor &visitor) override;
        };

        struct array_type_specifier : public type_specifier {
            std::shared_ptr<type_specifier> subtype;
            lex::punctuator br_open, br_close;
            std::optional<lex::integer> lex_int;

            array_type_specifier(const std::shared_ptr<type_specifier> &subtype, const lex::punctuator &br_open,
                                 const lex::punctuator &br_close, const std::optional<lex::integer> &lex_int):
                    subtype(subtype), br_open(br_open), br_close(br_close), lex_int(lex_int) {}

            virtual void visit(ast_visitor &visitor) override;
        };

        struct pointer_type_specifier : public type_specifier {
            std::shared_ptr<type_specifier> subtype;
            lex::operator_ pointer_type;

            pointer_type_specifier(const std::shared_ptr<type_specifier> &subtype, const lex::operator_ &pointer_type)
                    : subtype(subtype), pointer_type(pointer_type) {}

            virtual void visit(ast_visitor &visitor) override;
        };

        /**
         * Const type specifier: wraps another type specifier to mark it as const-qualified.
         * Syntax: 'const' TypeSpec   (e.g. "const int", "const int*")
         */
        struct const_type_specifier : public type_specifier {
            lex::keyword const_kw;
            std::shared_ptr<type_specifier> subtype;

            const_type_specifier(const lex::keyword &const_kw, const std::shared_ptr<type_specifier> &subtype)
                    : const_kw(const_kw), subtype(subtype) {}

            virtual void visit(ast_visitor &visitor) override;
        };

        /**
         * Function reference type specifier.
         *
         * Represents a reference (pointer *, view ?, or link +) to a function.
         * Syntax:
         *   RefKind '(' [ TypeSpec { ',' TypeSpec } ] ')'
         *   QualifiedIdentifier '::' RefKind '(' [ TypeSpec { ',' TypeSpec } ] ')'
         *
         * Examples:
         *   *(int, double+)          — pointer to free function (int, double+)
         *   ?(int)                   — view to free function (int)
         *   +()                      — link to free function with no params
         *   MyClass::*(int&)         — pointer to member function of MyClass taking int&
         *
         * No return type is specified (no overloading on return type in K).
         */
        struct function_ref_type_specifier : public type_specifier {
            /** The reference qualifier operator: STAR (*), QUESTION_MARK (?), or PLUS (+). */
            lex::operator_ ref_op;
            /** Optional qualifier for member function pointer: "MyClass::" part. */
            std::optional<qualified_identifier> owner;
            /** Types of function parameters. */
            std::vector<std::shared_ptr<type_specifier>> param_types;

            function_ref_type_specifier(
                const lex::operator_& ref_op,
                const std::optional<qualified_identifier>& owner,
                const std::vector<std::shared_ptr<type_specifier>>& param_types)
                : ref_op(ref_op), owner(owner), param_types(param_types) {}

            virtual void visit(ast_visitor &visitor) override;
        };

        /**
         * Owner type specifier: owning pointer (unique ownership) using '!' suffix.
         * Syntax: TypeSpec '!'
         * Semantics: like std::unique_ptr — single owner, auto-delete on scope exit.
         */
        struct owner_type_specifier : public type_specifier {
            std::shared_ptr<type_specifier> subtype;
            lex::operator_ owner_op; ///< The '!' operator token

            owner_type_specifier(const std::shared_ptr<type_specifier>& subtype, const lex::operator_& owner_op)
                : subtype(subtype), owner_op(owner_op) {}

            virtual void visit(ast_visitor& visitor) override;
        };

        struct expression;
        struct unary_expression;
        struct binary_expression;
        struct ternary_expression;
        struct multi_expression;

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

        struct expression : public ast_node {
            expression() = default;
            expression(const expression &) = default;
            expression(expression &&) = default;
        };

        struct visibility_decl;
        struct namespace_decl;
        struct using_decl;
        struct friend_decl;
        struct variable_decl;
        struct function_decl;

        /**
         * Declaration (member of a namespace).
         */
        struct declaration : public ast_node {
        };

        typedef std::shared_ptr<declaration> decl_ptr;

        struct statement : public ast_node {
        };

        struct block_statement;
        struct return_statement;
        typedef variable_decl declaration_statement;
        struct expression_statement;
        struct if_else_statement;

        struct unary_expression : public expression {
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
        };

        struct binary_expression : public expression {
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

        };

        struct ternary_expression : public expression {
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
       };

        struct multi_expression : public expression {
        protected:
            std::vector<expr_ptr> _exprs;

            multi_expression(const multi_expression&) = default;
            multi_expression(multi_expression&&) = default;

        public:
            virtual void visit(ast_visitor &visitor) {}

            multi_expression(const std::vector <expr_ptr> &exprs) : _exprs(exprs) {}
            multi_expression(std::vector <expr_ptr> &&exprs) : _exprs(exprs) {}

        public:
            size_t size() const { return _exprs.size(); }
            const expr_ptr expr(size_t n) const { return _exprs[n]; }
            expr_ptr expr(size_t n) { return _exprs[n]; }
            const expr_ptr operator[](size_t n) const { return _exprs[n]; }
            expr_ptr operator[](size_t n) { return _exprs[n]; }

            const std::vector<expr_ptr>& exprs() {
                return _exprs;
            }

        };

        struct literal_expr : public expression {
            lex::any_literal literal;

            literal_expr(const lex::any_literal &literal) : literal(literal) {}
            literal_expr(lex::any_literal &&literal) : literal(literal) {}

            virtual void visit(ast_visitor &visitor) override;
        };

        struct keyword_expr : public expression {
            lex::keyword keyword;

            keyword_expr(const lex::keyword &keyword) : keyword(keyword) {}
            keyword_expr(lex::keyword &&keyword) : keyword(keyword) {}

            virtual void visit(ast_visitor &visitor) override;
        };

        struct this_expr : public keyword_expr {
            this_expr(const lex::keyword &keyword) : keyword_expr(keyword) {}
            this_expr(lex::keyword &&keyword) : keyword_expr(keyword) {}

            virtual void visit(ast_visitor &visitor) override;
        };

        struct expr_list_expr : public multi_expression {
            expr_list_expr(const std::vector<expr_ptr> &exprs) : multi_expression(exprs) {}
            expr_list_expr(std::vector<expr_ptr> &&exprs) : multi_expression(exprs) {}

            virtual void visit(ast_visitor &visitor) override;
        };

        struct binary_operator_expr : public binary_expression {
            lex::operator_ op;

            binary_operator_expr(const lex::operator_ &op, expr_ptr lexpr, expr_ptr rexpr)
                    : binary_expression(lexpr, rexpr), op(op) {}

            virtual void visit(ast_visitor &visitor) override;
        };

        struct conditional_expr : public ternary_expression {
            lex::operator_ question_mark;
            lex::operator_ colon;

            conditional_expr(const lex::operator_ &question_mark, const lex::operator_ &colon, expr_ptr lexpr, expr_ptr  mexpr, expr_ptr rexpr)
                    : ternary_expression(lexpr, mexpr, rexpr), question_mark(question_mark), colon(colon) {}

            virtual void visit(ast_visitor &visitor) override;
        };


        struct cast_expr : public unary_expression {
            std::shared_ptr<ast::type_specifier> type;

            cast_expr(const cast_expr&) = default;
            cast_expr(cast_expr&&) = default;

            virtual ~cast_expr() = default;

            cast_expr(const std::shared_ptr<ast::type_specifier>& type, const expr_ptr &expr) : unary_expression(expr), type(type) {}
            cast_expr(std::shared_ptr<ast::type_specifier>&& type, expr_ptr &&expr) : unary_expression(expr), type(type) {}

            virtual void visit(ast_visitor &visitor) override;
        };

        /**
         * New expression: allocates an object on the heap and returns an owner.
         * Syntax (single object): 'new' TypeSpec '(' [args] ')'
         * Syntax (array):         'new' TypeSpec '[' [size_expr] ']' [ '{' [init_list] '}' ]
         */
        struct new_expr : public expression {
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

            virtual void visit(ast_visitor& visitor) override;
        };

        /**
         * Delete expression: explicitly deallocates an owner's object.
         * Syntax: 'delete' expr
         */
        struct delete_expr : public unary_expression {
            lex::keyword delete_kw;

            delete_expr(const lex::keyword& delete_kw, const expr_ptr& expr)
                : unary_expression(expr), delete_kw(delete_kw) {}

            virtual void visit(ast_visitor& visitor) override;
        };

        struct unary_prefix_expr : public unary_expression {
            lex::operator_ op;

            unary_prefix_expr(const unary_prefix_expr&) = default;
            unary_prefix_expr(unary_prefix_expr&&) = default;

            unary_prefix_expr(const lex::operator_& op, const expr_ptr &expr) : unary_expression(expr), op(op) {}
            unary_prefix_expr(lex::operator_&& op, expr_ptr &&expr) : unary_expression(expr), op(op) {}

            virtual void visit(ast_visitor &visitor) override;
        };

        struct unary_postfix_expr : public unary_expression {
            lex::operator_ op;
            unary_postfix_expr(const lex::operator_& op, const expr_ptr &expr) : unary_expression(expr), op(op) {}
            unary_postfix_expr(lex::operator_&& op, expr_ptr &&expr) : unary_expression(expr), op(op) {}

            virtual void visit(ast_visitor &visitor) override;
        };

        struct bracket_postifx_expr : public binary_expression {
            bracket_postifx_expr(const expr_ptr &lexpr, const expr_ptr &rexpr) : binary_expression(lexpr, rexpr) {}
            bracket_postifx_expr(expr_ptr &&rexpr, expr_ptr &&lexpr) : binary_expression(lexpr, rexpr) {}

            virtual void visit(ast_visitor &visitor) override;
        };

        struct parenthesis_postifx_expr : public binary_expression {
            parenthesis_postifx_expr(const expr_ptr &lexpr, const expr_ptr &rexpr) : binary_expression(lexpr, rexpr) {}
            parenthesis_postifx_expr(expr_ptr &&rexpr, expr_ptr &&lexpr) : binary_expression(lexpr, rexpr) {}

            virtual void visit(ast_visitor &visitor) override;
        };

        struct identifier_expr;
        struct member_access_postfix_expr : public unary_expression {
            lex::operator_ op;
            std::shared_ptr<identifier_expr> ident_expr;

            member_access_postfix_expr(const lex::operator_& op, const expr_ptr &expr, const std::shared_ptr<identifier_expr> &ident_expr)
            : unary_expression(expr), op(op), ident_expr(ident_expr) {}

            virtual void visit(ast_visitor &visitor) override;
        };

        struct identifier_expr : public expression {

            ast::qualified_identifier qident;

            identifier_expr(const ast::qualified_identifier& qident) :
                    qident(qident) {}

            virtual void visit(ast_visitor &visitor) override;
        };

        //
        // Statements
        //

        struct expression_statement : public statement {
            ast::expr_ptr expr;

            expression_statement(ast::expr_ptr expr) : expr(expr) {}

            virtual void visit(ast_visitor &visitor) override;
        };

        struct block_statement : public statement {
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

            virtual void visit(ast_visitor &visitor) override;
        };

        struct return_statement : public statement {
            lex::keyword ret;
            ast::expr_ptr expr;

            return_statement(const lex::keyword& ret, ast::expr_ptr expr) : ret(ret), expr(expr) {}

            virtual void visit(ast_visitor &visitor) override;
        };

        struct if_else_statement : public statement {
            lex::keyword if_kw;
            std::optional<lex::keyword> else_kw;
            std::shared_ptr<expression> test_expr;
            std::shared_ptr<statement> then_stmt;
            std::shared_ptr<statement> else_stmt;

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

            virtual void visit(ast_visitor &visitor) override;
        };

        struct while_statement : public statement {
            lex::keyword while_kw;
            std::shared_ptr<expression> test_expr;
            std::shared_ptr<statement> nested_stmt;

            while_statement(const lex::keyword &while_kw,
                              const std::shared_ptr<expression>& test_expr,
                              const std::shared_ptr<statement> &nested_stmt)
                    : while_kw(while_kw), test_expr(test_expr), nested_stmt(nested_stmt) {}


            virtual void visit(ast_visitor &visitor) override;
        };

        struct for_statement : public statement {
            lex::keyword for_kw;
            lex::punctuator first_semicolon_kw;
            lex::punctuator second_semicolon_kw;
            std::shared_ptr<variable_decl> decl_expr;
            std::shared_ptr<expression> test_expr;
            std::shared_ptr<expression> step_expr;
            std::shared_ptr<statement> nested_stmt;


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

            virtual void visit(ast_visitor &visitor) override;
        };

        //
        // Declarations
        //

        struct visibility_decl : public declaration {
            lex::keyword scope;

            visibility_decl(const visibility_decl&) = default;
            visibility_decl(visibility_decl&&) = default;

            visibility_decl(const lex::keyword &scope) : scope(scope) {}
            visibility_decl(lex::keyword &&scope) : scope(scope) {}

            virtual void visit(ast_visitor &visitor) override;
        };

        struct namespace_decl : public declaration {
            lex::keyword ns;
            lex::punctuator open_par, close_par;
            std::optional <lex::identifier> name;
            std::vector <decl_ptr> declarations;

            namespace_decl(const lex::keyword& ns,
                           const lex::punctuator& open_par,
                           const lex::punctuator& close_par,
                           const std::optional <lex::identifier> &name,
                           const std::vector <decl_ptr> &declarations) :
                    ns(ns), open_par(open_par), close_par(close_par), name(name), declarations(declarations) {}

            namespace_decl(lex::keyword&& ns,
                           lex::punctuator&& open_par,
                           lex::punctuator&& close_par,
                           std::optional <lex::identifier> &&name,
                           std::vector <decl_ptr> &&declarations) :
                    ns(ns), open_par(open_par), close_par(close_par), name(name), declarations(declarations) {}

            virtual void visit(ast_visitor &visitor) override;
        };

        /**
         * Using directive declaration.
         * Syntax: 'using' [filter]? [identifier '=']? QUALIFIED_IDENTIFIER ';'
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
        struct using_decl : public declaration, public statement {
            /// The 'using' keyword token.
            lex::keyword using_kw;

            /// Optional element type filter: NAMESPACE, STRUCT, INTERFACE, CLASS, or nullopt.
            std::optional<lex::keyword> element_filter;

            /// Optional alias name (the identifier before '=').
            std::optional<lex::identifier> alias_name;

            /// The qualified name being imported into the current scope.
            std::shared_ptr<qualified_identifier> qname;

            using_decl(const lex::keyword& using_kw,
                       const std::optional<lex::keyword>& element_filter,
                       const std::optional<lex::identifier>& alias_name,
                       std::shared_ptr<qualified_identifier> qname)
                : using_kw(using_kw), element_filter(element_filter),
                  alias_name(alias_name), qname(std::move(qname)) {}

            virtual void visit(ast_visitor &visitor) override;
        };

        /**
         * Friend declaration — grants another entity access to protected members.
         *
         * Syntax: 'friend' ['struct'|'interface'|'class']? qualified_identifier ';'
         *
         * Only valid inside an aggregate (struct/class/interface) body.
         * The optional type filter restricts friendship to the specified element kind.
         */
        struct friend_decl : public declaration {
            /// The 'friend' keyword token.
            lex::keyword friend_kw;

            /// Optional element type filter: STRUCT, INTERFACE, CLASS, or nullopt.
            std::optional<lex::keyword> element_filter;

            /// The qualified name of the friend entity.
            std::shared_ptr<qualified_identifier> qname;

            friend_decl(const lex::keyword& friend_kw,
                        const std::optional<lex::keyword>& element_filter,
                        std::shared_ptr<qualified_identifier> qname)
                : friend_kw(friend_kw), element_filter(element_filter),
                  qname(std::move(qname)) {}

            virtual void visit(ast_visitor &visitor) override;
        };

        /**
         * Annotation definition attached to a declaration.
         * Syntax: '@' qualified-identifier [ '(' [ExpressionList] ')' | DesignatedBraceInitList ]
         *
         * Without parentheses or braces, default initialization is implicit.
         */
        struct annotation_def : public ast_node {
            lex::punctuator at_sign;                          ///< The '@' token
            std::shared_ptr<qualified_identifier> name;       ///< Annotation type name (possibly qualified)
            std::shared_ptr<brace_init_list> brace_init;      ///< Optional designated/positional brace init (mutually exclusive with args)
            std::vector<expr_ptr> args;                       ///< Optional parenthesized argument list
            bool has_parens = false;                          ///< True when (...) was explicitly provided (distinguishes @Foo from @Foo())

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

            virtual void visit(ast_visitor &visitor) override;
        };

        /**
         * Annotation initializer expression — wraps an annotation_def for use
         * inside expression contexts (brace-init lists, argument lists).
         * Syntax: '@' QualifiedIdentifier [ '(' [ExpressionList] ')' | DesignatedBraceInitList ]
         * Example: @Tag("hello") used as an element of an array literal.
         */
        struct annotation_init_expr : public expression {
            std::shared_ptr<annotation_def> annotation;

            annotation_init_expr(std::shared_ptr<annotation_def> annotation)
                : annotation(std::move(annotation)) {}

            virtual void visit(ast_visitor &visitor) override;
        };

        struct aggregate_decl : public declaration {
            annotation_def_list annotations;
            std::vector <lex::keyword> specifiers;
            lex::keyword kw_aggregate_type;
            lex::punctuator open_brace, close_brace;
            lex::identifier name;
            std::vector <decl_ptr> declarations;

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

            virtual void visit(ast_visitor &visitor) override;
        };

        /**
         * A single designated member initializer inside a brace-init list.
         * Represents either:
         *   - Assignment form:    .member = expr
         *   - Constructor form:   .member(args...)
         *   - Qualified form:     .Base::member = expr  or  .Base::member(args...)
         */
        struct designated_init_element : public expression {
            lex::operator_ dot;             ///< The '.' token
            lex::identifier member_name;    ///< The member name (last component)
            /** Optional qualifier for disambiguating inherited members (e.g. "Base" in ".Base::member"). */
            std::vector<lex::identifier> qualifier;
            bool is_call_form;              ///< true → .m(args), false → .m = expr
            expr_ptr value;                 ///< Assignment form: the expression after '='
            std::vector<expr_ptr> args;     ///< Constructor form: the arguments between '(' ')'

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

            /** Returns the fully-qualified member name string (e.g. "Base::x" or just "x"). */
            std::string qualified_member_name() const {
                std::string result;
                for (auto& q : qualifier) {
                    result += std::string{q.content} + "::";
                }
                result += std::string{member_name.content};
                return result;
            }

            virtual void visit(ast_visitor &visitor) override;
        };

        /**
         * Brace initializer list expression.
         * Represents a comma-separated list of expressions inside braces: { e1, e2, ..., eN }
         * An empty slot (two consecutive commas, or trailing comma before '}') yields a nullptr entry
         * to represent default construction.
         * Used for array initialization: arr : int[3] { 1, 2, 3 };
         * When is_designated is true, every element is a designated_init_element.
         */
        struct brace_init_list : public expression {
            lex::punctuator open_brace, close_brace;
            /** Element initializer expressions. nullptr entries represent empty (default-init) slots. */
            std::vector<expr_ptr> elements;
            /** True when this brace-init list uses designated member initializers (.member = expr). */
            bool is_designated = false;

            brace_init_list(const lex::punctuator& open_brace,
                            const lex::punctuator& close_brace,
                            const std::vector<expr_ptr>& elements,
                            bool is_designated = false)
                : open_brace(open_brace), close_brace(close_brace), elements(elements),
                  is_designated(is_designated) {}

            virtual void visit(ast_visitor &visitor) override;
        };

        struct variable_decl : public declaration, public statement {
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

            variable_decl(const std::vector <lex::keyword> &specifiers, const lex::identifier &name,
                          const std::shared_ptr<ast::type_specifier> &type, expr_ptr init = nullptr, bool is_constructor = false, bool is_brace_init = false) :
                    specifiers(specifiers), name(name), type(type), init(init), is_constructor(is_constructor), is_brace_init(is_brace_init) {}

            variable_decl(std::vector <lex::keyword> &&specifiers, lex::identifier &&name, std::shared_ptr<ast::type_specifier> &&type, expr_ptr init, bool is_constructor, bool is_brace_init = false) :
                    specifiers(specifiers), name(name), type(type), init(init), is_constructor(is_constructor), is_brace_init(is_brace_init) {}

            virtual void visit(ast_visitor &visitor) override;
        };

        struct parameter_spec : public ast_node {
            std::vector <lex::keyword> specifiers;
            std::optional <lex::identifier> name;
            std::shared_ptr<ast::type_specifier> type;
            /** Optional default value expression (e.g. '= 42' or '= a + 1'). */
            expr_ptr default_expr;

            parameter_spec(const std::vector <lex::keyword> &specifiers, const std::optional <lex::identifier> &name,
                           const std::shared_ptr<ast::type_specifier> &type, expr_ptr default_expr = nullptr) :
                    specifiers(specifiers), name(name), type(type), default_expr(std::move(default_expr)) {}

            parameter_spec(std::vector <lex::keyword> &&specifiers, std::optional <lex::identifier> &&name,
                           std::shared_ptr<ast::type_specifier> &&type, expr_ptr default_expr = nullptr) :
                    specifiers(specifiers), name(name), type(type), default_expr(std::move(default_expr)) {}

            virtual void visit(ast_visitor &visitor) override;
        };

        /**
         * A single member initializer in a constructor's mem-initializer-list.
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

        struct function_decl : public declaration {
            /** Specifier for function-aliasing declarations (-> default ; / -> delete ; / -> target ;). */
            enum class aliasing_spec_t { NONE, DEFAULT, DELETE, REDIRECT };

            /** Annotations applied to this function (parsed before specifiers). */
            annotation_def_list annotations;
            std::vector<lex::keyword> specifiers;
            lex::identifier name;
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

            virtual void visit(ast_visitor &visitor) override;
        };

        /**
         * A single entry in an enum declaration.
         * Syntax: identifier [ '=' ( integer_literal | identifier ) ] [ 'default' ] ';'
         */
        struct enum_entry : public ast_node {
            lex::identifier name;
            /** Optional explicit value: either an integer literal or a reference to another entry name. */
            std::optional<lex::any_literal> literal_value;
            /** Optional reference to another entry (by identifier). Mutually exclusive with literal_value in practice. */
            std::optional<lex::identifier> ref_value;
            /** True when the 'default' keyword follows the entry. */
            bool is_default = false;

            enum_entry(const lex::identifier& name,
                       const std::optional<lex::any_literal>& literal_value,
                       const std::optional<lex::identifier>& ref_value,
                       bool is_default)
                : name(name), literal_value(literal_value), ref_value(ref_value), is_default(is_default) {}

            virtual void visit(ast_visitor &visitor) override;
        };

        /**
         * Enum declaration.
         * Syntax: SPECIFIERS 'enum' identifier [ ':' QualifiedName ] '{' ENUM_ENTRY* '}' ';'
         */
        struct enum_decl : public declaration {
            std::vector<lex::keyword> specifiers;
            lex::keyword kw_enum;
            lex::identifier name;
            /** Optional base enum name for enum derivation (e.g. "Base" or "ns::Base"). */
            std::optional<std::string> base_name;
            lex::punctuator open_brace, close_brace;
            std::vector<std::shared_ptr<enum_entry>> entries;

            enum_decl(const std::vector<lex::keyword>& specifiers,
                      const lex::keyword& kw_enum,
                      const lex::identifier& name,
                      const std::optional<std::string>& base_name,
                      const lex::punctuator& open_brace,
                      const lex::punctuator& close_brace,
                      const std::vector<std::shared_ptr<enum_entry>>& entries)
                : specifiers(specifiers), kw_enum(kw_enum), name(name),
                  base_name(base_name),
                  open_brace(open_brace), close_brace(close_brace), entries(entries) {}

            virtual void visit(ast_visitor &visitor) override;
        };

        struct module_name : public ast_node {
            lex::keyword module;
            std::shared_ptr<qualified_identifier> qname;

            module_name(const lex::keyword& module, const std::shared_ptr<ast::qualified_identifier>& qname):
                module(module), qname(qname) {};
            module_name(lex::keyword&& module, std::shared_ptr<ast::qualified_identifier>&& qname):
                    module(module), qname(qname) {};

            virtual void visit(ast_visitor &visitor) override;
        };


        /**
         * Unit.
         */
        struct unit : public ast_node {
            /** Unit module name, if any, null otherwise */
            std::shared_ptr<ast::module_name> module_name;

            /** Import declarations */
            std::vector<std::shared_ptr<import>> imports;

            // TODO remove it:
            std::vector <decl_ptr> declarations;

            virtual void visit(ast_visitor &visitor) override;
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
        virtual void visit_function_ref_type_specifier(ast::function_ref_type_specifier &) = 0;
        virtual void visit_owner_type_specifier(ast::owner_type_specifier &) = 0;

        virtual void visit_parameter_specifier(ast::parameter_spec &) = 0;

        virtual void visit_qualified_identifier(ast::qualified_identifier &) = 0;

        virtual void visit_visibility_decl(ast::visibility_decl &) = 0;
        virtual void visit_namespace_decl(ast::namespace_decl &) = 0;
        virtual void visit_using_decl(ast::using_decl &) = 0;
        virtual void visit_friend_decl(ast::friend_decl &) = 0;
        virtual void visit_aggregate_decl(ast::aggregate_decl &) = 0;
        virtual void visit_enum_decl(ast::enum_decl &) = 0;
        virtual void visit_variable_decl(ast::variable_decl &) = 0;
        virtual void visit_function_decl(ast::function_decl &) = 0;

        virtual void visit_block_statement(ast::block_statement &) = 0;
        virtual void visit_return_statement(ast::return_statement &) = 0;
        virtual void visit_if_else_statement(ast::if_else_statement &) = 0;
        virtual void visit_while_statement(ast::while_statement &) = 0;
        virtual void visit_for_statement(ast::for_statement &) = 0;
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
        virtual void visit_identifier_expr(ast::identifier_expr &) = 0;

        virtual void visit_new_expr(ast::new_expr &) = 0;
        virtual void visit_delete_expr(ast::delete_expr &) = 0;

        virtual void visit_brace_init_list(ast::brace_init_list &) = 0;
        virtual void visit_designated_init_element(ast::designated_init_element &) = 0;

        virtual void visit_annotation_def(ast::annotation_def &) = 0;
        virtual void visit_annotation_init_expr(ast::annotation_init_expr &) = 0;

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
        void visit_function_ref_type_specifier(ast::function_ref_type_specifier &) override;
        void visit_owner_type_specifier(ast::owner_type_specifier &) override;

        void visit_parameter_specifier(ast::parameter_spec &) override;
        void visit_qualified_identifier(ast::qualified_identifier &) override;

        void visit_visibility_decl(ast::visibility_decl &) override;
        void visit_namespace_decl(ast::namespace_decl &) override;
        void visit_using_decl(ast::using_decl &) override;
        void visit_friend_decl(ast::friend_decl &) override;
        void visit_aggregate_decl(ast::aggregate_decl &) override;
        void visit_enum_decl(ast::enum_decl &) override;
        void visit_variable_decl(ast::variable_decl &) override;
        void visit_function_decl(ast::function_decl &) override;

        void visit_block_statement(ast::block_statement &) override;
        void visit_return_statement(ast::return_statement &) override;
        void visit_if_else_statement(ast::if_else_statement &) override;
        void visit_while_statement(ast::while_statement &) override;
        void visit_for_statement(ast::for_statement &) override;
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
        void visit_identifier_expr(ast::identifier_expr &) override;

        void visit_new_expr(ast::new_expr &) override;
        void visit_delete_expr(ast::delete_expr &) override;

        void visit_brace_init_list(ast::brace_init_list &) override;
        void visit_designated_init_element(ast::designated_init_element &) override;

        void visit_annotation_def(ast::annotation_def &) override;
        void visit_annotation_init_expr(ast::annotation_init_expr &) override;

        void visit_comma_expr(ast::expr_list_expr &) override;
    };

} // namespace k::parse
#endif //KLANG_AST_HPP
