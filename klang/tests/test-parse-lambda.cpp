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
 * Parsing of lambda expressions.
 */

#include <catch2/catch_all.hpp>

#include "../src/errors.hpp"
#include "../src/lex/lexer.hpp"

#include "helpers.hpp"

using namespace k::parse;
using namespace k::log;

namespace {

    std::shared_ptr<ast::lambda_expression> parse_lambda(const std::string& text) {
        static test_logger log;
        static std::vector<std::shared_ptr<k::source>> keep;
        auto src = std::make_shared<k::source>(text);
        keep.push_back(src);
        k::parse::parser parser(log, *src);
        return std::dynamic_pointer_cast<ast::lambda_expression>(parser.parse_primary_expr());
    }

    bool parse_lambda_fails(const std::string& text) {
        try {
            test_logger log;
            k::source src{text};
            k::parse::parser parser(log, src);
            parser.parse_primary_expr();
            return false;
        } catch (const k::log::compiler_error&) {
            return true;
        }
    }

} // namespace

TEST_CASE("Parse a capture-free lambda expression", "[parser][lambda]") {
    auto lambda = parse_lambda("[](x: int): int { return x + 1; }");

    REQUIRE(lambda);
    REQUIRE(lambda->is_const == false);
    REQUIRE(lambda->has_capture_list == true);
    REQUIRE(lambda->captures.empty());
    REQUIRE(lambda->params.size() == 1);
    REQUIRE(lambda->params[0]);
    REQUIRE(lambda->params[0]->name.has_value());
    REQUIRE(lambda->params[0]->name->content == "x");
    REQUIRE(lambda->return_type);
    REQUIRE(lambda->body);
    REQUIRE(lambda->body->statements.size() == 1);
}

TEST_CASE("Parse a lambda with reference captures", "[parser][lambda]") {
    auto lambda = parse_lambda("[const & this, &value](x: int) { return x; }");

    REQUIRE(lambda);
    REQUIRE(lambda->has_capture_list == true);
    REQUIRE(lambda->captures.size() == 2);
    REQUIRE(lambda->captures[0].is_const == true);
    REQUIRE(lambda->captures[0].is_reference == true);
    REQUIRE(lambda->captures[0].is_this == true);
    REQUIRE(lambda->captures[1].is_reference == true);
    REQUIRE(lambda->captures[1].name.has_value());
    REQUIRE(lambda->captures[1].name->content == "value");
}

TEST_CASE("Parse a const lambda without explicit capture brackets", "[parser][lambda]") {
    auto lambda = parse_lambda("const(x: int) { return x; }");

    REQUIRE(lambda);
    REQUIRE(lambda->is_const == true);
    REQUIRE(lambda->has_capture_list == false);
    REQUIRE(lambda->captures.empty());
}

TEST_CASE("Reject malformed lambda capture syntax", "[parser][lambda]") {
    REQUIRE(parse_lambda_fails("[&](x: int) { return x; }"));
    REQUIRE(parse_lambda_fails("[const &](x: int) { return x; }"));
    REQUIRE(parse_lambda_fails("[x =](x: int) { return x; }"));
}

