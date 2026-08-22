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

/**
 * Parsing of the exported aliasing declarations:
 *
 *     alias   name : aliased_symbol;
 *     typedef name : aliased_type;
 *
 * Both forms are accepted in declaration context (unit, namespace, aggregate)
 * and in statement context (inside a block).
 */

#include <catch2/catch_all.hpp>

#include "../src/lex/lexer.hpp"
#include "../src/errors.hpp"

#include "helpers.hpp"

using namespace k::parse;
using namespace k::log;

namespace {

    /** Parse a single declaration out of a source snippet. */
    std::shared_ptr<ast::alias_decl> parse_alias(const std::string& text) {
        static test_logger log;
        static std::vector<std::shared_ptr<k::source>> keep;
        auto src = std::make_shared<k::source>(text);
        keep.push_back(src);
        k::parse::parser parser(log, *src);
        return parser.parse_alias_decl();
    }

    /** True if parsing the snippet as a declaration raises a compiler error. */
    bool alias_parse_fails(const std::string& text) {
        try {
            test_logger log;
            k::source src{text};
            k::parse::parser parser(log, src);
            parser.parse_alias_decl();
            return false;
        } catch (const k::log::compiler_error&) {
            return true;
        }
    }

} // anonymous namespace

//
// Soft alias form
//

TEST_CASE("Parse a soft alias over a simple name", "[parser][alias]") {
    auto decl = parse_alias("alias Short : Long;");

    REQUIRE(decl);
    REQUIRE(decl->is_strong == false);
    REQUIRE(decl->name.content == "Short");
    REQUIRE(decl->qname);
    REQUIRE(decl->qname->names.size() == 1);
    REQUIRE(decl->qname->names[0].content == "Long");
    // A soft alias also keeps the type specification: only symbol resolution can
    // tell whether the aliased name denotes a type, a function or a variable.
    REQUIRE(decl->type);
    REQUIRE(std::dynamic_pointer_cast<ast::identified_type_specifier>(decl->type));
}

TEST_CASE("Parse a soft alias over a qualified name", "[parser][alias]") {
    auto decl = parse_alias("alias Text : k::String;");

    REQUIRE(decl);
    REQUIRE(decl->is_strong == false);
    REQUIRE(decl->name.content == "Text");
    REQUIRE(decl->qname);
    REQUIRE(decl->qname->names.size() == 2);
    REQUIRE(decl->qname->names[0].content == "k");
    REQUIRE(decl->qname->names[1].content == "String");
}

TEST_CASE("Parse a soft alias over a root-qualified name", "[parser][alias]") {
    auto decl = parse_alias("alias Text : ::k::String;");

    REQUIRE(decl);
    REQUIRE(decl->qname);
    REQUIRE(decl->qname->has_root_prefix());
    REQUIRE(decl->qname->names.size() == 2);
}

TEST_CASE("Parse a soft alias over a function name", "[parser][alias]") {
    auto decl = parse_alias("alias add : compute::sum;");

    REQUIRE(decl);
    REQUIRE(decl->is_strong == false);
    REQUIRE(decl->name.content == "add");
    REQUIRE(decl->qname);
    REQUIRE(decl->qname->names.size() == 2);
}

//
// Strong alias (typedef) form
//

TEST_CASE("Parse a typedef over a primitive type", "[parser][typedef]") {
    auto decl = parse_alias("typedef identifier : int;");

    REQUIRE(decl);
    REQUIRE(decl->is_strong == true);
    REQUIRE(decl->name.content == "identifier");
    REQUIRE(decl->type);
    REQUIRE(!decl->qname);

    auto kw_type = std::dynamic_pointer_cast<ast::keyword_type_specifier>(decl->type);
    REQUIRE(kw_type);
    REQUIRE(kw_type->keyword.type == k::lex::keyword::INT);
}

TEST_CASE("Parse a typedef over an unsigned primitive type", "[parser][typedef]") {
    auto decl = parse_alias("typedef counter : unsigned long;");

    REQUIRE(decl);
    REQUIRE(decl->is_strong == true);
    auto kw_type = std::dynamic_pointer_cast<ast::keyword_type_specifier>(decl->type);
    REQUIRE(kw_type);
    REQUIRE(kw_type->is_unsigned == true);
}

TEST_CASE("Parse a typedef over a named type", "[parser][typedef]") {
    auto decl = parse_alias("typedef Name : k::String;");

    REQUIRE(decl);
    REQUIRE(decl->is_strong == true);
    auto id_type = std::dynamic_pointer_cast<ast::identified_type_specifier>(decl->type);
    REQUIRE(id_type);
    REQUIRE(id_type->name.names.size() == 2);
}

TEST_CASE("Parse a typedef over an array type", "[parser][typedef]") {
    auto decl = parse_alias("typedef Buffer : byte[];");

    REQUIRE(decl);
    REQUIRE(decl->is_strong == true);
    REQUIRE(std::dynamic_pointer_cast<ast::array_type_specifier>(decl->type));
}

TEST_CASE("Parse a typedef over a pointer type", "[parser][typedef]") {
    auto decl = parse_alias("typedef Slot : int*;");

    REQUIRE(decl);
    REQUIRE(std::dynamic_pointer_cast<ast::pointer_type_specifier>(decl->type));
}

TEST_CASE("Parse a typedef over an owner type", "[parser][typedef]") {
    auto decl = parse_alias("typedef Handle : Object!;");

    REQUIRE(decl);
    REQUIRE(decl->type);
}

TEST_CASE("Parse a typedef over a const type", "[parser][typedef]") {
    auto decl = parse_alias("typedef Frozen : const int;");

    REQUIRE(decl);
    REQUIRE(std::dynamic_pointer_cast<ast::const_type_specifier>(decl->type));
}

TEST_CASE("Parse a typedef over a template instantiation", "[parser][typedef]") {
    auto decl = parse_alias("typedef IntVec : Vector<int>;");

    REQUIRE(decl);
    auto id_type = std::dynamic_pointer_cast<ast::identified_type_specifier>(decl->type);
    REQUIRE(id_type);
    REQUIRE(id_type->template_args.size() == 1);
}

//
// Non-alias input must not be consumed
//

TEST_CASE("Parsing a non-alias declaration returns nothing", "[parser][alias]") {
    REQUIRE(!parse_alias("using Short = Long;"));
    REQUIRE(!parse_alias("value : int = 3;"));
    REQUIRE(!parse_alias(""));
}

//
// Syntax errors
//

TEST_CASE("Alias declaration without a name is rejected", "[parser][alias]") {
    REQUIRE(alias_parse_fails("alias : Long;"));
    REQUIRE(alias_parse_fails("typedef : int;"));
}

TEST_CASE("Alias declaration without a colon is rejected", "[parser][alias]") {
    REQUIRE(alias_parse_fails("alias Short Long;"));
    REQUIRE(alias_parse_fails("typedef identifier int;"));
}

TEST_CASE("Alias declaration with an equal sign instead of a colon is rejected", "[parser][alias]") {
    REQUIRE(alias_parse_fails("alias Short = Long;"));
}

TEST_CASE("Alias declaration without a target is rejected", "[parser][alias]") {
    REQUIRE(alias_parse_fails("alias Short : ;"));
    REQUIRE(alias_parse_fails("typedef identifier : ;"));
}

TEST_CASE("Alias declaration without a trailing semicolon is rejected", "[parser][alias]") {
    REQUIRE(alias_parse_fails("alias Short : Long"));
    REQUIRE(alias_parse_fails("typedef identifier : int"));
}

//
// Integration into the declaration and statement dispatchers
//

TEST_CASE("Alias and typedef are parsed as unit declarations", "[parser][alias]") {
    test_logger log;
    k::source src{R"(
        module parse_alias_01;
        alias Short : Long;
        typedef identifier : int;
    )"};
    k::parse::parser parser(log, src);
    auto unit = parser.parse_unit();

    REQUIRE(unit);
    REQUIRE(unit->declarations.size() == 2);

    auto soft = std::dynamic_pointer_cast<ast::alias_decl>(unit->declarations[0]);
    REQUIRE(soft);
    REQUIRE(soft->is_strong == false);
    REQUIRE(soft->name.content == "Short");

    auto strong = std::dynamic_pointer_cast<ast::alias_decl>(unit->declarations[1]);
    REQUIRE(strong);
    REQUIRE(strong->is_strong == true);
    REQUIRE(strong->name.content == "identifier");
}

TEST_CASE("Alias and typedef are parsed inside a namespace", "[parser][alias]") {
    test_logger log;
    k::source src{R"(
        module parse_alias_02;
        namespace inner {
            typedef identifier : int;
        }
    )"};
    k::parse::parser parser(log, src);
    auto unit = parser.parse_unit();

    REQUIRE(unit);
    REQUIRE(unit->declarations.size() == 1);
    auto nsdecl = std::dynamic_pointer_cast<ast::namespace_decl>(unit->declarations[0]);
    REQUIRE(nsdecl);
    REQUIRE(nsdecl->declarations.size() == 1);
    REQUIRE(std::dynamic_pointer_cast<ast::alias_decl>(nsdecl->declarations[0]));
}

TEST_CASE("Alias and typedef are parsed inside a statement block", "[parser][alias]") {
    test_logger log;
    k::source src{R"(
        module parse_alias_03;
        f() : int {
            typedef identifier : int;
            alias Short : Long;
            return 0;
        }
    )"};
    k::parse::parser parser(log, src);
    auto unit = parser.parse_unit();

    REQUIRE(unit);
    REQUIRE(unit->declarations.size() == 1);
    auto fn = std::dynamic_pointer_cast<ast::function_decl>(unit->declarations[0]);
    REQUIRE(fn);
    REQUIRE(fn->content);
    REQUIRE(fn->content->statements.size() == 3);
    REQUIRE(std::dynamic_pointer_cast<ast::alias_decl>(fn->content->statements[0]));
    REQUIRE(std::dynamic_pointer_cast<ast::alias_decl>(fn->content->statements[1]));
}

TEST_CASE("A parameterised alias is parsed", "[parser][alias][template]") {
    test_logger log;
    k::source src{R"(
        module parse_alias_04;
        template<typename T> alias Vec : Array<T, 16>;
        template<typename T, typename U> typedef Pair : Tuple<T, U>;
    )"};
    k::parse::parser parser(log, src);
    auto unit = parser.parse_unit();

    REQUIRE(unit);
    REQUIRE(unit->declarations.size() == 2);

    auto soft = std::dynamic_pointer_cast<ast::alias_decl>(unit->declarations[0]);
    REQUIRE(soft);
    CHECK_FALSE(soft->is_strong);
    CHECK(soft->is_template());
    REQUIRE(soft->template_params.size() == 1);
    CHECK(soft->template_params[0]->name.content == "T");
    CHECK_FALSE(soft->template_source_text.empty());

    auto strong = std::dynamic_pointer_cast<ast::alias_decl>(unit->declarations[1]);
    REQUIRE(strong);
    CHECK(strong->is_strong);
    CHECK(strong->is_template());
    REQUIRE(strong->template_params.size() == 2);
}

TEST_CASE("A non-parameterised alias carries no template parameter", "[parser][alias][template]") {
    test_logger log;
    k::source src{R"(
        module parse_alias_05;
        alias Short : Long;
    )"};
    k::parse::parser parser(log, src);
    auto unit = parser.parse_unit();

    REQUIRE(unit);
    REQUIRE(unit->declarations.size() == 1);
    auto al = std::dynamic_pointer_cast<ast::alias_decl>(unit->declarations[0]);
    REQUIRE(al);
    CHECK_FALSE(al->is_template());
    CHECK(al->template_source_text.empty());
}
