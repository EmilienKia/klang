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
#include "kdi_docgen.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

using namespace kdi;

namespace {

namespace fs = std::filesystem;

std::string read_text_file(const fs::path& path) {
    std::ifstream in(path);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

kdi_file make_docgen_model() {
    kdi_file file;
    file.header.module_name = "demo::mod";
    file.unit.name = "demo::mod";
    file.unit.root_ns.name = "";
    file.unit.root_ns.fq_name = "demo::mod";

    kdi_namespace util;
    util.name = "util";
    util.fq_name = "demo::mod::util";
    util.doc = kdi_doc_block{};
    util.doc->brief = "Utility helpers namespace.";
    file.unit.root_ns.namespaces.push_back(util);

    kdi_function make_thing;
    make_thing.name = "makeThing";
    make_thing.fq_name = "demo::mod::makeThing";
    make_thing.return_type = kdi_type::make_void();
    make_thing.doc = kdi_doc_function{};
    make_thing.doc->brief = "Create a Thing.";
    file.unit.root_ns.functions.push_back(make_thing);

    kdi_variable version;
    version.name = "VERSION";
    version.fq_name = "demo::mod::VERSION";
    version.type = kdi_type::make_int(32, false);
    version.doc = kdi_doc_block{};
    version.doc->brief = "Public API version exposed to consumers.";
    file.unit.root_ns.variables.push_back(version);

    kdi_aggregate thing;
    thing.kind = kdi_aggregate_kind::class_;
    thing.name = "Thing";
    thing.fq_name = "demo::mod::Thing";
    thing.doc = kdi_doc_block{};
    thing.doc->brief = "Main sample class used by docgen tests.";

    kdi_method ping;
    ping.name = "ping";
    ping.return_type = kdi_type::make_bool();
    ping.doc = kdi_doc_function{};
    ping.doc->brief = "Checks that the thing responds to a liveness probe.";
    thing.methods.push_back(ping);

    kdi_layout_member id;
    id.name = "id";
    id.type = kdi_type::make_int(32, true);
    thing.layout.push_back(id);

    kdi_aggregate inner;
    inner.kind = kdi_aggregate_kind::struct_;
    inner.name = "Inner";
    inner.fq_name = "demo::mod::Thing::Inner";
    inner.doc = kdi_doc_block{};
    inner.doc->brief = "Nested helper payload.";
    thing.nested.push_back(inner);

    file.unit.root_ns.aggregates.push_back(thing);

    kdi_enum color;
    color.name = "Color";
    color.fq_name = "demo::mod::Color";
    color.underlying_type = kdi_type::make_int(32, true);
    color.doc = kdi_doc_block{};
    color.doc->brief = "Enumeration of display colors.";
    file.unit.root_ns.enums.push_back(color);

    return file;
}

} // anonymous namespace

TEST_CASE("docgen: generate markdown tree with module root index", "[docgen]") {
    const fs::path output_dir = fs::temp_directory_path() / "kdi-docgen-test-output";
    std::error_code ec;
    fs::remove_all(output_dir, ec);

    const auto file = make_docgen_model();

    std::string error_message;
    REQUIRE(kdi_generate_markdown_doc(file, output_dir.string(), &error_message));

    const fs::path module_root = output_dir / "demo::mod";
    REQUIRE(fs::exists(module_root / "index.md"));
    REQUIRE(fs::exists(module_root / "name-references.md"));
    REQUIRE(fs::exists(module_root / "typed-references.md"));
    REQUIRE(fs::exists(module_root / "Thing.md"));
    REQUIRE(fs::exists(module_root / "Thing.Inner.md"));
    REQUIRE(fs::exists(module_root / "Color.md"));
    REQUIRE(fs::exists(module_root / "util" / "index.md"));

    const std::string root_index = read_text_file(module_root / "index.md");
    REQUIRE(root_index.find("# Module demo::mod") != std::string::npos);
    REQUIRE(root_index.find("[util](util/index.md)") != std::string::npos);
    REQUIRE(root_index.find("| Name | Kind | Brief |") != std::string::npos);
    REQUIRE(root_index.find("| [`Thing`](Thing.md) | `class` | Main sample class used by docgen tests. |") != std::string::npos);
    REQUIRE(root_index.find("| Signature | Brief |") != std::string::npos);
    REQUIRE(root_index.find("| [`makeThing() : void`](#fn-makething-0) | Create a Thing. |") != std::string::npos);
    REQUIRE(root_index.find("| Name | Type | Brief |") != std::string::npos);
    REQUIRE(root_index.find("| [`VERSION`](#var-version-0) | `unsigned int32` | Public API version exposed to consumers. |") != std::string::npos);
    REQUIRE(root_index.find("[details](#") == std::string::npos);
    REQUIRE(root_index.find("## Function Details") != std::string::npos);

    const std::string thing_md = read_text_file(module_root / "Thing.md");
    REQUIRE(thing_md.find("- [`ping() : bool`](#method-ping-0) - Checks that the thing responds to a liveness probe.") != std::string::npos);
    REQUIRE(thing_md.find("- [Inner](Thing.Inner.md) - Nested helper payload.") != std::string::npos);

    const std::string refs = read_text_file(module_root / "name-references.md");
    REQUIRE(refs.find("| Name | Kind | Scope | Type | Brief |") != std::string::npos);
    REQUIRE(refs.find("[`Thing`](Thing.md)") != std::string::npos);
    REQUIRE(refs.find("Main sample class used by docgen tests.") != std::string::npos);
    REQUIRE(refs.find("`namespace`") != std::string::npos);
    REQUIRE(refs.find("[doc](") == std::string::npos);

    const std::string typed_refs = read_text_file(module_root / "typed-references.md");
    REQUIRE(typed_refs.find("| Name | Scope | Type | Brief |") != std::string::npos);
    REQUIRE(typed_refs.find("[`Thing`](Thing.md)") != std::string::npos);
    REQUIRE(typed_refs.find("Main sample class used by docgen tests.") != std::string::npos);
    REQUIRE(typed_refs.find("[doc](") == std::string::npos);

    fs::remove_all(output_dir, ec);
}

TEST_CASE("docgen: generate html tree with direct links on names", "[docgen]") {
    const fs::path output_dir = fs::temp_directory_path() / "kdi-docgen-html-test-output";
    std::error_code ec;
    fs::remove_all(output_dir, ec);

    const auto file = make_docgen_model();

    std::string error_message;
    REQUIRE(kdi_generate_html_doc(file, output_dir.string(), &error_message));

    const fs::path module_root = output_dir / "demo::mod";
    REQUIRE(fs::exists(module_root / "index.html"));
    REQUIRE(fs::exists(module_root / "name-references.html"));
    REQUIRE(fs::exists(module_root / "typed-references.html"));
    REQUIRE(fs::exists(module_root / "kdoc.css"));
    REQUIRE(fs::exists(module_root / "Thing.html"));
    REQUIRE(fs::exists(module_root / "Thing.Inner.html"));
    REQUIRE(fs::exists(module_root / "Color.html"));
    REQUIRE(fs::exists(module_root / "util" / "index.html"));

    const std::string root_index = read_text_file(module_root / "index.html");
    REQUIRE(root_index.find("<a href=\"util/index.html\">util</a>") != std::string::npos);
    REQUIRE(root_index.find("<a href=\"Thing.html\">Thing</a>") != std::string::npos);
    REQUIRE(root_index.find("<a href=\"#fn-makething-0\">makeThing() : void</a>") != std::string::npos);
    REQUIRE(root_index.find("<a href=\"#var-version-0\">VERSION</a>") != std::string::npos);
    REQUIRE(root_index.find("<th>Brief</th>") != std::string::npos);
    REQUIRE(root_index.find("Main sample class used by docgen tests.") != std::string::npos);
    REQUIRE(root_index.find("Create a Thing.") != std::string::npos);
    REQUIRE(root_index.find("Public API version exposed to consumers.") != std::string::npos);
    REQUIRE(root_index.find(">open<") == std::string::npos);
    REQUIRE(root_index.find(">detail<") == std::string::npos);

    const std::string thing_page = read_text_file(module_root / "Thing.html");
    REQUIRE(thing_page.find("<a href=\"Thing.Inner.html\">Inner</a>") != std::string::npos);
    REQUIRE(thing_page.find("Checks that the thing responds to a liveness probe.") != std::string::npos);
    REQUIRE(thing_page.find("Nested helper payload.") != std::string::npos);
    REQUIRE(thing_page.find(">detail<") == std::string::npos);

    const std::string name_refs = read_text_file(module_root / "name-references.html");
    REQUIRE(name_refs.find("<th>Brief</th>") != std::string::npos);
    REQUIRE(name_refs.find("<a href=\"Thing.html\">Thing</a>") != std::string::npos);
    REQUIRE(name_refs.find("Main sample class used by docgen tests.") != std::string::npos);
    REQUIRE(name_refs.find(">doc<") == std::string::npos);

    const std::string typed_refs = read_text_file(module_root / "typed-references.html");
    REQUIRE(typed_refs.find("<th>Brief</th>") != std::string::npos);
    REQUIRE(typed_refs.find("<a href=\"Thing.html\">Thing</a>") != std::string::npos);
    REQUIRE(typed_refs.find("Main sample class used by docgen tests.") != std::string::npos);
    REQUIRE(typed_refs.find(">doc<") == std::string::npos);

    fs::remove_all(output_dir, ec);
}

TEST_CASE("docgen: destination path must not be a file", "[docgen]") {
    const fs::path destination_file = fs::temp_directory_path() / "kdi-docgen-destination-file";
    {
        std::ofstream out(destination_file);
        out << "not a directory";
    }

    std::string error_message;
    const auto file = make_docgen_model();
    REQUIRE_FALSE(kdi_generate_markdown_doc(file, destination_file.string(), &error_message));
    REQUIRE(error_message.find("destination path is a file") != std::string::npos);

    std::error_code ec;
    fs::remove(destination_file, ec);
}


