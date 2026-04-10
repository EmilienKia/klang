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
#include <catch2/catch_all.hpp>

#include "../src/lex/lexer.hpp"
#include "../src/parse/parser.hpp"

#include "helpers.hpp"

using namespace k::parse;
using namespace k::log;

// ═══════════════════════════════════════════════════════════════════════════
// Template declaration parsing
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Parse template struct with typename parameter", "[parser][template]") {
    test_logger log;
    k::source src{"template<typename T> struct Foo {}"};
    k::parse::parser parser(log, src);
    auto unit = parser.parse_unit();

    REQUIRE(unit->declarations.size() == 1);
    auto agg = std::dynamic_pointer_cast<ast::aggregate_decl>(unit->declarations[0]);
    REQUIRE(agg);
    REQUIRE(agg->is_struct());
    CHECK(std::string{agg->name.content} == "Foo");
    REQUIRE(agg->is_template());
    REQUIRE(agg->template_params.size() == 1);

    auto& param = agg->template_params[0];
    CHECK(param->is_type_param());
    CHECK(param->kind_kw->type == k::lex::keyword::TYPENAME);
    CHECK(std::string{param->name.content} == "T");
    CHECK(param->constraint_type == nullptr);
    CHECK(param->default_expr == nullptr);
}

TEST_CASE("Parse template function with typename parameter", "[parser][template]") {
    test_logger log;
    k::source src{"template<typename T> swap(a: T+, b: T+) {}"};
    k::parse::parser parser(log, src);
    auto unit = parser.parse_unit();

    REQUIRE(unit->declarations.size() == 1);
    auto func = std::dynamic_pointer_cast<ast::function_decl>(unit->declarations[0]);
    REQUIRE(func);
    CHECK(std::string{func->name.content} == "swap");
    REQUIRE(func->is_template());
    REQUIRE(func->template_params.size() == 1);

    auto& param = func->template_params[0];
    CHECK(param->is_type_param());
    CHECK(param->kind_kw->type == k::lex::keyword::TYPENAME);
    CHECK(std::string{param->name.content} == "T");
}

TEST_CASE("Parse template struct with multiple parameters", "[parser][template]") {
    test_logger log;
    k::source src{"template<typename T, typename U> struct Pair {}"};
    k::parse::parser parser(log, src);
    auto unit = parser.parse_unit();

    REQUIRE(unit->declarations.size() == 1);
    auto agg = std::dynamic_pointer_cast<ast::aggregate_decl>(unit->declarations[0]);
    REQUIRE(agg);
    REQUIRE(agg->is_template());
    REQUIRE(agg->template_params.size() == 2);

    CHECK(std::string{agg->template_params[0]->name.content} == "T");
    CHECK(std::string{agg->template_params[1]->name.content} == "U");
}

TEST_CASE("Parse template with value parameter", "[parser][template]") {
    test_logger log;
    k::source src{"template<typename T, unsigned int N> struct Arr {}"};
    k::parse::parser parser(log, src);
    auto unit = parser.parse_unit();

    REQUIRE(unit->declarations.size() == 1);
    auto agg = std::dynamic_pointer_cast<ast::aggregate_decl>(unit->declarations[0]);
    REQUIRE(agg);
    REQUIRE(agg->is_template());
    REQUIRE(agg->template_params.size() == 2);

    // First param: typename T
    CHECK(agg->template_params[0]->is_type_param());
    CHECK(std::string{agg->template_params[0]->name.content} == "T");

    // Second param: unsigned int N (value parameter)
    CHECK(agg->template_params[1]->is_value_param());
    CHECK(std::string{agg->template_params[1]->name.content} == "N");
    CHECK(agg->template_params[1]->value_type != nullptr);
}

TEST_CASE("Parse template with value parameter and default", "[parser][template]") {
    test_logger log;
    k::source src{"template<typename T, unsigned int N = 10> struct Arr {}"};
    k::parse::parser parser(log, src);
    auto unit = parser.parse_unit();

    REQUIRE(unit->declarations.size() == 1);
    auto agg = std::dynamic_pointer_cast<ast::aggregate_decl>(unit->declarations[0]);
    REQUIRE(agg);
    REQUIRE(agg->is_template());
    REQUIRE(agg->template_params.size() == 2);

    auto& val_param = agg->template_params[1];
    CHECK(val_param->is_value_param());
    CHECK(std::string{val_param->name.content} == "N");
    CHECK(val_param->default_expr != nullptr);
}

TEST_CASE("Parse template with class constraint", "[parser][template]") {
    test_logger log;
    k::source src{"template<class T : Base> doSomething(t: T&) : int {}"};
    k::parse::parser parser(log, src);
    auto unit = parser.parse_unit();

    REQUIRE(unit->declarations.size() == 1);
    auto func = std::dynamic_pointer_cast<ast::function_decl>(unit->declarations[0]);
    REQUIRE(func);
    REQUIRE(func->is_template());
    REQUIRE(func->template_params.size() == 1);

    auto& param = func->template_params[0];
    CHECK(param->is_type_param());
    CHECK(param->kind_kw->type == k::lex::keyword::CLASS);
    CHECK(std::string{param->name.content} == "T");
    CHECK(param->constraint_type != nullptr);
}

TEST_CASE("Parse Pair<int> as type specifier", "[parser][template]") {
    test_logger log;
    k::source src{"x : Pair<int>"};
    k::parse::parser parser(log, src);
    // Parse as a variable declaration (identifier ':' type ';' is needed, but we can at least verify
    // the type is parsed with template args)
    // Use parse_type_spec directly after consuming 'x' ':'
    auto lx = parser.parse_identifier_expr(); // consume 'x'
    // Actually, let's parse a full variable decl
    // Re-create parser to properly test
    test_logger log2;
    k::source src2{"v : Pair<int>;"};
    k::parse::parser parser2(log2, src2);
    auto unit = parser2.parse_unit();

    REQUIRE(unit->declarations.size() == 1);
    auto var = std::dynamic_pointer_cast<ast::variable_decl>(unit->declarations[0]);
    REQUIRE(var);
    CHECK(std::string{var->name.content} == "v");

    auto id_type = std::dynamic_pointer_cast<ast::identified_type_specifier>(var->type);
    REQUIRE(id_type);
    CHECK(id_type->name.names.size() == 1);
    CHECK(std::string{id_type->name.names[0].content} == "Pair");
    REQUIRE(id_type->template_args.size() == 1);
    CHECK(id_type->template_args[0]->is_type());
}

TEST_CASE("Parse nested template args: Pair<Pair<int>>", "[parser][template]") {
    test_logger log;
    k::source src{"v : Pair<Pair<int>>;"};
    k::parse::parser parser(log, src);
    auto unit = parser.parse_unit();

    REQUIRE(unit->declarations.size() == 1);
    auto var = std::dynamic_pointer_cast<ast::variable_decl>(unit->declarations[0]);
    REQUIRE(var);

    auto id_type = std::dynamic_pointer_cast<ast::identified_type_specifier>(var->type);
    REQUIRE(id_type);
    CHECK(std::string{id_type->name.names[0].content} == "Pair");
    REQUIRE(id_type->template_args.size() == 1);
    CHECK(id_type->template_args[0]->is_type());

    // The inner type should also be a Pair<int>
    auto inner = std::dynamic_pointer_cast<ast::identified_type_specifier>(id_type->template_args[0]->type_arg);
    REQUIRE(inner);
    CHECK(std::string{inner->name.names[0].content} == "Pair");
    REQUIRE(inner->template_args.size() == 1);
}

TEST_CASE("Non-template struct still parses correctly", "[parser][template]") {
    test_logger log;
    k::source src{"struct Plain {}"};
    k::parse::parser parser(log, src);
    auto unit = parser.parse_unit();

    REQUIRE(unit->declarations.size() == 1);
    auto agg = std::dynamic_pointer_cast<ast::aggregate_decl>(unit->declarations[0]);
    REQUIRE(agg);
    CHECK(!agg->is_template());
    CHECK(agg->template_params.empty());
}

TEST_CASE("Non-template function still parses correctly", "[parser][template]") {
    test_logger log;
    k::source src{"foo() : int {}"};
    k::parse::parser parser(log, src);
    auto unit = parser.parse_unit();

    REQUIRE(unit->declarations.size() == 1);
    auto func = std::dynamic_pointer_cast<ast::function_decl>(unit->declarations[0]);
    REQUIRE(func);
    CHECK(!func->is_template());
    CHECK(func->template_params.empty());
}

TEST_CASE("Parse template with struct kind constraint", "[parser][template]") {
    test_logger log;
    k::source src{"template<struct S> process(s: S&) {}"};
    k::parse::parser parser(log, src);
    auto unit = parser.parse_unit();

    REQUIRE(unit->declarations.size() == 1);
    auto func = std::dynamic_pointer_cast<ast::function_decl>(unit->declarations[0]);
    REQUIRE(func);
    REQUIRE(func->is_template());
    REQUIRE(func->template_params.size() == 1);

    auto& param = func->template_params[0];
    CHECK(param->is_type_param());
    CHECK(param->kind_kw->type == k::lex::keyword::STRUCT);
    CHECK(std::string{param->name.content} == "S");
}

// ═══════════════════════════════════════════════════════════════════════════
// M7: Default template parameters — parser level
// ═══════════════════════════════════════════════════════════════════════════

TEST_CASE("Parse template struct with default type parameter", "[parser][template][defaults]") {
    test_logger log;
    k::source src{"template<typename T = int> struct Box {}"};
    k::parse::parser parser(log, src);
    auto unit = parser.parse_unit();

    REQUIRE(unit->declarations.size() == 1);
    auto agg = std::dynamic_pointer_cast<ast::aggregate_decl>(unit->declarations[0]);
    REQUIRE(agg);
    REQUIRE(agg->is_template());
    REQUIRE(agg->template_params.size() == 1);

    auto& param = agg->template_params[0];
    CHECK(param->is_type_param());
    CHECK(param->kind_kw->type == k::lex::keyword::TYPENAME);
    CHECK(std::string{param->name.content} == "T");
    CHECK(param->default_type_spec != nullptr);
}

TEST_CASE("Parse template struct with multiple params and partial defaults", "[parser][template][defaults]") {
    test_logger log;
    k::source src{"template<typename K, typename V = int> struct Pair {}"};
    k::parse::parser parser(log, src);
    auto unit = parser.parse_unit();

    REQUIRE(unit->declarations.size() == 1);
    auto agg = std::dynamic_pointer_cast<ast::aggregate_decl>(unit->declarations[0]);
    REQUIRE(agg);
    REQUIRE(agg->is_template());
    REQUIRE(agg->template_params.size() == 2);

    auto& p0 = agg->template_params[0];
    CHECK(p0->is_type_param());
    CHECK(std::string{p0->name.content} == "K");
    CHECK(p0->default_type_spec == nullptr);

    auto& p1 = agg->template_params[1];
    CHECK(p1->is_type_param());
    CHECK(std::string{p1->name.content} == "V");
    CHECK(p1->default_type_spec != nullptr);
}

TEST_CASE("Parse type reference with empty template arg list <>", "[parser][template][defaults]") {
    test_logger log;
    k::source src{"foo(b: Box<>) {}"};
    k::parse::parser parser(log, src);
    auto unit = parser.parse_unit();

    REQUIRE(unit->declarations.size() == 1);
    auto func = std::dynamic_pointer_cast<ast::function_decl>(unit->declarations[0]);
    REQUIRE(func);
    REQUIRE(func->params.size() == 1);

    auto& param_type = func->params[0]->type;
    auto ident_ts = std::dynamic_pointer_cast<ast::identified_type_specifier>(param_type);
    REQUIRE(ident_ts != nullptr);
    CHECK(ident_ts->has_explicit_template_args == true);
    CHECK(ident_ts->template_args.empty());
}



