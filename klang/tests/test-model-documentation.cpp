/*
 * K Language compiler
 *
 * Copyright 2026 Emilien Kia
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

#include "helpers.hpp"

using namespace k::model;

TEST_CASE("Model docs: module doc-comment attaches to unit", "[model][documentation]") {
    auto comp = compile_model(R"SRC(
        /// Unit brief
        ///
        /// Unit description.
        module docs_unit;
        struct Dummy {}
    )SRC");
    REQUIRE(comp != nullptr);
    REQUIRE(comp->get_unit() != nullptr);

    auto doc = comp->get_unit()->get_documentation_as<doc::unit_doc>();
    REQUIRE(doc != nullptr);
    CHECK(doc->brief == "Unit brief");
    CHECK(doc->description == "Unit description.");
}

TEST_CASE("Model docs: namespace/aggregate/enum/variable/function docs are attached", "[model][documentation]") {
    auto comp = compile_model(R"SRC(
        module docs_all;

        /// Namespace brief
        namespace N {
            /**
             * Struct brief
             *
             * Struct description.
             */
            struct S {
                /// Member brief
                value : int;

                /**
                 * Sum brief
                 *
                 * Sum long description.
                 * @param a first value
                 * @param b second value line1
                 *   line2
                 * @return computed sum
                 * @throws Error invalid values
                 */
                sum(a: int, b: int) : int { return a + b; }
            }

            /// Enum brief
            enum E { A; };
        }
    )SRC");
    REQUIRE(comp != nullptr);
    auto unit = comp->get_unit();
    REQUIRE(unit != nullptr);
    auto root = unit->get_root_namespace();
    REQUIRE(root != nullptr);

    auto ns = root->get_child_namespace("N");
    REQUIRE(ns != nullptr);
    auto ns_doc = ns->get_documentation_as<doc::namespace_doc>();
    REQUIRE(ns_doc != nullptr);
    CHECK(ns_doc->brief == "Namespace brief");

    auto agg = ns->get_aggregate("S");
    REQUIRE(agg != nullptr);
    auto agg_doc = agg->get_documentation_as<doc::aggregate_doc>();
    REQUIRE(agg_doc != nullptr);
    CHECK(agg_doc->brief == "Struct brief");
    CHECK(agg_doc->description == "Struct description.");

    auto member = agg->get_variable("value");
    REQUIRE(member != nullptr);
    auto member_elem = std::dynamic_pointer_cast<element>(member);
    REQUIRE(member_elem != nullptr);
    auto member_doc = member_elem->get_documentation_as<doc::variable_doc>();
    REQUIRE(member_doc != nullptr);
    CHECK(member_doc->brief == "Member brief");

    auto fn = agg->get_function("sum");
    REQUIRE(fn != nullptr);
    auto fn_doc = fn->get_documentation_as<doc::function_doc>();
    REQUIRE(fn_doc != nullptr);
    CHECK(fn_doc->brief == "Sum brief");
    CHECK(fn_doc->description == "Sum long description.");
    REQUIRE(fn_doc->params.size() == 2);
    CHECK(fn_doc->params[0].name == "a");
    CHECK(fn_doc->params[0].description == "first value");
    CHECK(fn_doc->params[1].name == "b");
    CHECK(fn_doc->params[1].description == "second value line1\nline2");
    REQUIRE(fn_doc->returns.has_value());
    CHECK(fn_doc->returns->description == "computed sum");
    REQUIRE(fn_doc->throws.size() == 1);
    CHECK(fn_doc->throws[0].type_name == "Error");
    CHECK(fn_doc->throws[0].description == "invalid values");

    auto en = ns->get_enum("E");
    REQUIRE(en != nullptr);
    auto enum_doc = en->get_documentation_as<doc::enum_doc>();
    REQUIRE(enum_doc != nullptr);
    CHECK(enum_doc->brief == "Enum brief");
}

TEST_CASE("Model docs: backward doc-comment attaches to variable doc", "[model][documentation]") {
    auto comp = compile_model(R"SRC(
        module docs_backward;
        x : int; //! backward variable doc
    )SRC");
    REQUIRE(comp != nullptr);
    auto unit = comp->get_unit();
    REQUIRE(unit != nullptr);
    auto root = unit->get_root_namespace();
    REQUIRE(root != nullptr);

    auto x = root->get_variable("x");
    REQUIRE(x != nullptr);
    auto x_elem = std::dynamic_pointer_cast<element>(x);
    REQUIRE(x_elem != nullptr);
    auto x_doc = x_elem->get_documentation_as<doc::variable_doc>();
    REQUIRE(x_doc != nullptr);
    CHECK(x_doc->brief == "backward variable doc");
}
