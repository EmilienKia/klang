/*
 * K Language compiler — libkdi tests
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

#include "kdi.hpp"
#include "kdi_doc.hpp"

using namespace kdi;

static kdi_file make_doc_file() {
    kdi_file f;
    f.header.module_name = "root::mod";
    f.unit.name = "root::mod";
    f.unit.root_ns.fq_name = "root::mod";
    return f;
}

TEST_CASE("doc: exact mangled symbol resolves first", "[doc]") {
    auto f = make_doc_file();
    kdi_function fn;
    fn.name = "foo";
    fn.fq_name = "root::mod::foo";
    fn.return_type = kdi_type::make_void();
    fn.mangled_name = "_KFN4root3mod3fooEv";
    fn.doc = kdi_doc_function{};
    fn.doc->brief = "brief";
    f.unit.root_ns.functions.push_back(fn);

    auto matches = kdi_find_doc_symbols(f, "_KFN4root3mod3fooEv");
    REQUIRE(matches.size() == 1);
    REQUIRE(matches[0].kind == kdi_doc_kind::function);
    REQUIRE(matches[0].mangled_name == "_KFN4root3mod3fooEv");
}

TEST_CASE("doc: module root prefix can be stripped", "[doc]") {
    auto f = make_doc_file();
    kdi_function fn;
    fn.name = "bar";
    fn.fq_name = "root::mod::ns::bar";
    fn.return_type = kdi_type::make_void();
    fn.mangled_name = "_KFN4root3mod2ns3barEv";
    fn.doc = kdi_doc_function{};
    fn.doc->brief = "brief";
    f.unit.root_ns.functions.push_back(fn);

    auto matches = kdi_find_doc_symbols(f, "ns::bar");
    REQUIRE(matches.size() == 1);
    REQUIRE(matches[0].fq_name == "root::mod::ns::bar");
}

TEST_CASE("doc: ambiguous nominal symbol returns every candidate", "[doc]") {
    auto f = make_doc_file();

    kdi_function a;
    a.name = "foo";
    a.fq_name = "root::mod::foo";
    a.return_type = kdi_type::make_void();
    a.mangled_name = "_KFN4root3mod3fooEv";
    f.unit.root_ns.functions.push_back(a);

    kdi_function b = a;
    b.mangled_name = "_KFN4root3mod3fooEi";
    b.doc = kdi_doc_function{};
    f.unit.root_ns.functions.push_back(b);

    auto matches = kdi_find_doc_symbols(f, "foo");
    REQUIRE(matches.size() == 2);
    REQUIRE(matches[0].fq_name == "root::mod::foo");
    REQUIRE(matches[1].fq_name == "root::mod::foo");
}

TEST_CASE("doc: direct children are preserved on resolved symbol", "[doc]") {
    auto f = make_doc_file();

    kdi_aggregate agg;
    agg.name = "Box";
    agg.fq_name = "root::mod::Box";
    agg.mangled_name = "_KSB";

    kdi_constructor ctor;
    ctor.mangled_name = "_KBC1";
    agg.constructors.push_back(ctor);

    kdi_method method;
    method.name = "size";
    method.fq_name = "root::mod::Box::size";
    method.return_type = kdi_type::make_int(32);
    method.mangled_name = "_KBsize";
    agg.methods.push_back(method);

    kdi_aggregate nested;
    nested.name = "Inner";
    nested.fq_name = "root::mod::Box::Inner";
    nested.mangled_name = "_KBInner";
    agg.nested.push_back(nested);

    f.unit.root_ns.aggregates.push_back(agg);

    auto matches = kdi_find_doc_symbols(f, "_KSB");
    REQUIRE(matches.size() == 1);
    REQUIRE(matches[0].children.size() == 3);
    REQUIRE(matches[0].children[0].kind == kdi_doc_kind::constructor);
    REQUIRE(matches[0].children[1].kind == kdi_doc_kind::method);
    REQUIRE(matches[0].children[2].kind == kdi_doc_kind::aggregate);

    auto text = kdi_format_doc_text(matches[0], true);
    REQUIRE(text.find("children:") != std::string::npos);
    REQUIRE(text.find("constructor root::mod::Box::Box") != std::string::npos);
}
