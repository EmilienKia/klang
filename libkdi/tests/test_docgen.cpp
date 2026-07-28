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

    // Regression coverage: a method whose return type references a concrete
    // template instantiation (kdi_aggregate_ref pointing at the compiler-
    // synthesized "Container__int") must render using the real generic
    // arguments ("Container<int32>"), not the synthesized/mangled name.
    kdi_method get_container;
    get_container.name = "getContainer";
    get_container.return_type = kdi_type::make_aggregate("demo::mod::Container__int");
    thing.methods.push_back(get_container);

    // Regression coverage: operator methods must render using their human-
    // readable K declaration syntax ("operator ==", "operator []"), not the
    // internal canonical name ("__operator_eq_", "__operator_ix_"), per
    // doc/spec/language/functions/operators.md. Anchors/slugs stay based on
    // the raw internal name so links remain stable.
    kdi_method eq_op;
    eq_op.name = "__operator_eq_";
    eq_op.is_operator = true;
    eq_op.return_type = kdi_type::make_bool();
    kdi_param eq_other;
    eq_other.name = "other";
    eq_other.type = kdi_type{kdi_ref_type{std::make_shared<kdi_type>(kdi_type::make_aggregate("demo::mod::Thing"))}};
    eq_op.params.push_back(eq_other);
    thing.methods.push_back(eq_op);

    kdi_method ix_op;
    ix_op.name = "__operator_ix_";
    ix_op.is_operator = true;
    ix_op.return_type = kdi_type::make_int(32, true);
    kdi_param ix_index;
    ix_index.name = "index";
    ix_index.type = kdi_type::make_int(32, false);
    ix_op.params.push_back(ix_index);
    thing.methods.push_back(ix_op);

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

    // Regression coverage for the "## Inheritance" section (regular
    // class/interface hierarchy): an interface with no bases, a class
    // implementing it directly, and a further class deriving from that one
    // (transitively implementing the interface). Exercises: direct bases,
    // the transitive "implemented interfaces" closure, module-local direct
    // subclasses, and (interfaces only) module-local direct-or-indirect
    // implementors.
    {
        kdi_aggregate greeter;
        greeter.kind = kdi_aggregate_kind::interface_;
        greeter.name = "Greeter";
        greeter.fq_name = "demo::mod::Greeter";
        file.unit.root_ns.aggregates.push_back(greeter);

        kdi_aggregate talker;
        talker.kind = kdi_aggregate_kind::class_;
        talker.name = "Talker";
        talker.fq_name = "demo::mod::Talker";
        kdi_base talker_base;
        talker_base.fq_name = "demo::mod::Greeter";
        talker_base.is_virtual = true;
        talker.bases.push_back(talker_base);
        file.unit.root_ns.aggregates.push_back(talker);

        kdi_aggregate super_talker;
        super_talker.kind = kdi_aggregate_kind::class_;
        super_talker.name = "SuperTalker";
        super_talker.fq_name = "demo::mod::SuperTalker";
        kdi_base super_talker_base;
        super_talker_base.fq_name = "demo::mod::Talker";
        super_talker_base.is_virtual = true;
        super_talker.bases.push_back(super_talker_base);
        file.unit.root_ns.aggregates.push_back(super_talker);
    }

    // Regression coverage for enum ascendant/descendant relationships
    // (direct only): a base enum with no parent, and a derived enum
    // referencing it via base_fq_name.
    {
        kdi_enum status;
        status.name = "Status";
        status.fq_name = "demo::mod::Status";
        status.underlying_type = kdi_type::make_int(32, true);
        file.unit.root_ns.enums.push_back(status);

        kdi_enum extended_status;
        extended_status.name = "ExtendedStatus";
        extended_status.fq_name = "demo::mod::ExtendedStatus";
        extended_status.underlying_type = kdi_type::make_int(32, true);
        extended_status.base_fq_name = "demo::mod::Status";
        file.unit.root_ns.enums.push_back(extended_status);
    }

    // Regression coverage for union ascendant/descendant relationships
    // (direct only): a base union with no parent, and a derived union
    // referencing it via base_union_fq_name.
    {
        kdi_union result;
        result.name = "Result";
        result.fq_name = "demo::mod::Result";
        file.unit.root_ns.unions.push_back(result);

        kdi_union extended_result;
        extended_result.name = "ExtendedResult";
        extended_result.fq_name = "demo::mod::ExtendedResult";
        extended_result.base_union_fq_name = "demo::mod::Result";
        file.unit.root_ns.unions.push_back(extended_result);
    }

    // Aggregate (class) template definition — exercises the "Types" listing
    // and dedicated template page (covers collection-like templates such as
    // Vector/List/Set, which were previously silently dropped by docgen).
    kdi_template_def container_tpl;
    container_tpl.name = "Container";
    container_tpl.fq_name = "demo::mod::Container";
    container_tpl.entity_kind = "class";
    container_tpl.visibility = "public";
    container_tpl.is_generic = false;
    kdi_template_param type_param;
    type_param.kind = "typename";
    type_param.name = "T";
    container_tpl.params.push_back(type_param);
    container_tpl.source =
        "template<typename T>\nclass Container {\n    value : T;\n    box : Box<T>;\n}\n";

    // Structured signature — mirrors what kdi_exporter now always builds for
    // templates (not just is_generic ones), so a template documents its
    // fields/constructors/methods the same way as a regular aggregate,
    // rather than only a raw source dump. "value" exercises a bare
    // template-parameter reference; "box" exercises a nested generic
    // reference to another (possibly still-uninstantiated) template applied
    // with the enclosing template's own parameter (e.g. "MultiSlot<T>" in
    // Vector<T>).
    {
        kdi_aggregate sig;
        sig.kind = kdi_aggregate_kind::class_;
        sig.name = "Container";
        sig.fq_name = "demo::mod::Container";
        sig.visibility = kdi_visibility::public_;

        kdi_layout_member value;
        value.name = "value";
        value.type = kdi_type::make_template_param("T");
        value.visibility = kdi_visibility::public_;
        sig.layout.push_back(value);

        kdi_layout_member box;
        box.name = "box";
        kdi_generic_ref_type box_ref;
        box_ref.name = "Box";
        box_ref.args.push_back(std::make_shared<kdi_type>(kdi_type::make_template_param("T")));
        box.type = kdi_type{std::move(box_ref)};
        box.visibility = kdi_visibility::public_;
        sig.layout.push_back(box);

        kdi_constructor ctor;
        ctor.visibility = kdi_visibility::public_;
        kdi_param ctor_param;
        ctor_param.name = "value";
        ctor_param.type = kdi_type::make_template_param("T");
        ctor.params.push_back(ctor_param);
        sig.constructors.push_back(ctor);

        kdi_method get_value;
        get_value.name = "getValue";
        get_value.return_type = kdi_type::make_template_param("T");
        get_value.visibility = kdi_visibility::public_;
        sig.methods.push_back(get_value);

        // Regression coverage: a template's base reference is raw K source
        // text ("Boxed<T>"), not a resolved fq_name, since KDI cannot resolve
        // template argument identity at export time. The hierarchy resolver
        // must match this against the "Boxed" template def registered below
        // via its bare short-name alias.
        kdi_base container_base;
        container_base.fq_name = "Boxed<T>";
        container_base.is_virtual = true;
        sig.bases.push_back(container_base);

        container_tpl.aggregate_signature = std::make_shared<kdi_aggregate>(std::move(sig));
    }

    file.unit.root_ns.template_defs.push_back(container_tpl);

    // Template interface with no bases, referenced above by Container<T> via
    // its bare short name ("Boxed<T>") rather than a fully-qualified name.
    {
        kdi_template_def boxed_tpl;
        boxed_tpl.name = "Boxed";
        boxed_tpl.fq_name = "demo::mod::Boxed";
        boxed_tpl.entity_kind = "interface";
        boxed_tpl.visibility = "public";
        boxed_tpl.is_generic = false;
        kdi_template_param boxed_type_param;
        boxed_type_param.kind = "typename";
        boxed_type_param.name = "T";
        boxed_tpl.params.push_back(boxed_type_param);

        kdi_aggregate boxed_sig;
        boxed_sig.kind = kdi_aggregate_kind::interface_;
        boxed_sig.name = "Boxed";
        boxed_sig.fq_name = "demo::mod::Boxed";
        boxed_sig.visibility = kdi_visibility::public_;
        boxed_tpl.aggregate_signature = std::make_shared<kdi_aggregate>(std::move(boxed_sig));

        file.unit.root_ns.template_defs.push_back(boxed_tpl);
    }

    // Function template definition — exercises the "Function Templates" section.
    kdi_template_def make_container_tpl;
    make_container_tpl.name = "makeContainer";
    make_container_tpl.fq_name = "demo::mod::makeContainer";
    make_container_tpl.entity_kind = "function";
    make_container_tpl.visibility = "public";
    make_container_tpl.is_generic = false;
    kdi_template_param fn_type_param;
    fn_type_param.kind = "typename";
    fn_type_param.name = "T";
    make_container_tpl.params.push_back(fn_type_param);
    make_container_tpl.source =
        "template<typename T>\nmakeContainer(value : T) : Container<T> {\n    return Container<T>(value);\n}\n";

    // Structured function signature — same rationale as above.
    {
        kdi_function sig;
        sig.name = "makeContainer";
        sig.fq_name = "demo::mod::makeContainer";
        sig.visibility = kdi_visibility::public_;
        kdi_param value_param;
        value_param.name = "value";
        value_param.type = kdi_type::make_template_param("T");
        sig.params.push_back(value_param);
        kdi_generic_ref_type ret_ref;
        ret_ref.name = "Container";
        ret_ref.args.push_back(std::make_shared<kdi_type>(kdi_type::make_template_param("T")));
        sig.return_type = kdi_type{std::move(ret_ref)};
        make_container_tpl.function_signature = std::make_shared<kdi_function>(std::move(sig));
    }

    file.unit.root_ns.template_defs.push_back(make_container_tpl);

    // Concrete template instantiation artifacts (compiler-synthesized derivatives
    // of Container<T>/makeContainer<T>, analogous to real-world "UniSlot__byte").
    // These must be entirely excluded from generated docs (no page, no namespace
    // index entry, no nested-type/reference listing) while still round-tripping
    // through the KDI model itself (still needed for cross-module link dedup).
    {
        kdi_aggregate inst;
        inst.kind = kdi_aggregate_kind::class_;
        inst.name = "Container__int";
        inst.fq_name = "demo::mod::Container__int";
        inst.doc = kdi_doc_block{};
        inst.doc->brief = "Should never appear in generated docs.";
        inst.template_origin = kdi_template_origin{};
        inst.template_origin->base_name = "Container";
        inst.template_origin->base_fq_name = "demo::mod::Container";
        kdi_template_arg int_arg;
        int_arg.type_arg = kdi_type::make_int(32, true);
        inst.template_origin->args.push_back(int_arg);

        kdi_method inst_method;
        inst_method.name = "getValue";
        inst_method.return_type = kdi_type::make_int(32, true);
        inst.methods.push_back(inst_method);

        file.unit.root_ns.aggregates.push_back(inst);

        // Instantiated member-function template nested inside the instantiation
        // (e.g. a synthesized argument-forwarding overload) — must also be
        // excluded, mirroring the real UniSlot__byte::construct case.
        kdi_template_def nested_fn_inst;
        nested_fn_inst.name = "construct";
        nested_fn_inst.fq_name = "demo::mod::Container__int::construct";
        nested_fn_inst.entity_kind = "function";
        nested_fn_inst.visibility = "public";
        file.unit.root_ns.template_defs.push_back(nested_fn_inst);
    }
    {
        kdi_function inst_fn;
        inst_fn.name = "makeContainer__int";
        inst_fn.fq_name = "demo::mod::makeContainer__int";
        inst_fn.return_type = kdi_type::make_void();
        inst_fn.doc = kdi_doc_function{};
        inst_fn.doc->brief = "Should never appear in generated docs.";
        inst_fn.template_origin = kdi_template_origin{};
        inst_fn.template_origin->base_name = "makeContainer";
        inst_fn.template_origin->base_fq_name = "demo::mod::makeContainer";
        file.unit.root_ns.functions.push_back(inst_fn);
    }

    // Regression coverage for a KDI export quirk: an auto-imported module
    // (e.g. the implicit `import k;`) can surface as an inert "mirror" child
    // namespace whose fq_name fails to properly extend the parent scope
    // (collapsing back onto it once the root prefix is stripped). docgen
    // must skip such children instead of recursing into them and silently
    // overwriting the parent namespace's own freshly written index page.
    kdi_namespace mirror;
    mirror.name = "mirror";
    mirror.fq_name = "";
    file.unit.root_ns.namespaces.push_back(mirror);

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
    REQUIRE(fs::exists(module_root / "Container.md"));
    REQUIRE(fs::exists(module_root / "makeContainer.md"));

    const std::string root_index = read_text_file(module_root / "index.md");
    REQUIRE(root_index.find("# Module demo::mod") != std::string::npos);
    REQUIRE(root_index.find("[util](util/index.md)") != std::string::npos);
    REQUIRE(root_index.find("| Name | Kind | Brief |") != std::string::npos);
    REQUIRE(root_index.find("| [`Thing`](Thing.md) | `class` | Main sample class used by docgen tests. |") != std::string::npos);
    REQUIRE(root_index.find("| [`Container&lt;T&gt;`](Container.md) | `template class` |") != std::string::npos);
    REQUIRE(root_index.find("| Signature | Brief |") != std::string::npos);
    REQUIRE(root_index.find("| [`makeThing() : void`](#fn-makething-0) | Create a Thing. |") != std::string::npos);
    REQUIRE(root_index.find("## Function Templates") != std::string::npos);
    REQUIRE(root_index.find("[`makeContainer&lt;T&gt;`](makeContainer.md)") != std::string::npos);
    REQUIRE(root_index.find("| Name | Type | Brief |") != std::string::npos);
    REQUIRE(root_index.find("| [`VERSION`](#var-version-0) | `unsigned int32` | Public API version exposed to consumers. |") != std::string::npos);
    REQUIRE(root_index.find("[details](#") == std::string::npos);
    REQUIRE(root_index.find("## Function Details") != std::string::npos);
    // Regression: the inert "mirror" child namespace must not overwrite this page.
    REQUIRE(root_index.find("mirror") == std::string::npos);
    // Regression: concrete template instantiation artifacts (Container__int,
    // makeContainer__int) must not appear in the namespace index at all.
    REQUIRE(root_index.find("Container__int") == std::string::npos);
    REQUIRE(root_index.find("makeContainer__int") == std::string::npos);

    // Regression: instantiation artifacts must not get their own dedicated page,
    // nor a nested member-template page (e.g. a synthesized ::construct).
    REQUIRE_FALSE(fs::exists(module_root / "Container__int.md"));
    REQUIRE_FALSE(fs::exists(module_root / "makeContainer__int.md"));
    REQUIRE_FALSE(fs::exists(module_root / "construct.md"));

    const std::string thing_md = read_text_file(module_root / "Thing.md");
    REQUIRE(thing_md.find("- [`ping() : bool`](#method-ping-3) - Checks that the thing responds to a liveness probe.") != std::string::npos);
    REQUIRE(thing_md.find("- [Inner](Thing.Inner.md) - Nested helper payload.") != std::string::npos);
    // Regression: a method returning a concrete template instantiation must
    // render using the real generic arguments ("Container<int32>"), not the
    // compiler-synthesized/mangled name ("Container__int").
    REQUIRE(thing_md.find("getContainer() : demo::mod::Container&lt;int32&gt;") != std::string::npos);
    REQUIRE(thing_md.find("Container__int") == std::string::npos);
    // Regression: operator methods render with human-readable K declaration
    // syntax ("operator ==", "operator []"), not the raw internal canonical
    // name ("__operator_eq_", "__operator_ix_"). Anchors stay on the raw name.
    REQUIRE(thing_md.find("operator ==(other: &demo::mod::Thing) : bool") != std::string::npos);
    REQUIRE(thing_md.find("operator [](index: unsigned int32) : int32") != std::string::npos);
    REQUIRE(thing_md.find("__operator_eq_") == std::string::npos);
    REQUIRE(thing_md.find("__operator_ix_") == std::string::npos);

    const std::string container_md = read_text_file(module_root / "Container.md");
    REQUIRE(container_md.find("`template class`") != std::string::npos);
    REQUIRE(container_md.find("## Template Parameters") != std::string::npos);
    REQUIRE(container_md.find("`T`") != std::string::npos);
    REQUIRE(container_md.find("`typename`") != std::string::npos);
    // Structured rendering (parity with regular aggregate pages): fields and
    // methods show up as distinct entities, not just embedded in raw source.
    REQUIRE(container_md.find("## Member Variables") != std::string::npos);
    REQUIRE(container_md.find("`value`") != std::string::npos);
    // Bare template-parameter reference renders as its plain name "T".
    REQUIRE(container_md.find("value`](#field-value-1): `T`") != std::string::npos);
    // Nested generic reference (Box<T>) is preserved structurally, not
    // collapsed to void/blank.
    REQUIRE(container_md.find("box`](#field-box-0): `Box&lt;T&gt;`") != std::string::npos);
    REQUIRE(container_md.find("## Members") != std::string::npos);
    REQUIRE(container_md.find("getValue() : T") != std::string::npos);
    // Raw source is now a supplementary section, not the primary content.
    REQUIRE(container_md.find("## Declaration Source") != std::string::npos);
    REQUIRE(container_md.find("class Container {") != std::string::npos);
    // Regression: a template's raw generic base reference ("Boxed<T>") must
    // resolve, via the bare short-name alias, to the "Boxed" template def's
    // own dedicated page.
    REQUIRE(container_md.find("## Inheritance") != std::string::npos);
    REQUIRE(container_md.find("**Base types:** [`Boxed&lt;T&gt;`](Boxed.md)") != std::string::npos);

    // Regression: regular class/interface hierarchy — direct bases, the
    // transitive "implemented interfaces" closure, module-local direct
    // subclasses, and (interfaces only) module-local direct-or-indirect
    // implementors, all linked to their dedicated pages.
    const std::string greeter_md = read_text_file(module_root / "Greeter.md");
    REQUIRE(greeter_md.find("**Base types:** *(none)*") != std::string::npos);
    REQUIRE(greeter_md.find("**Known direct subclasses (this module):** [`Talker`](Talker.md)") != std::string::npos);
    REQUIRE(greeter_md.find("**Known implementors, direct or indirect (this module):** [`SuperTalker`](SuperTalker.md), [`Talker`](Talker.md)") != std::string::npos);

    const std::string talker_md = read_text_file(module_root / "Talker.md");
    REQUIRE(talker_md.find("**Base types:** [`Greeter`](Greeter.md)") != std::string::npos);
    REQUIRE(talker_md.find("**All implemented interfaces:** [`Greeter`](Greeter.md)") != std::string::npos);
    REQUIRE(talker_md.find("**Known direct subclasses (this module):** [`SuperTalker`](SuperTalker.md)") != std::string::npos);

    const std::string super_talker_md = read_text_file(module_root / "SuperTalker.md");
    REQUIRE(super_talker_md.find("**Base types:** [`Talker`](Talker.md)") != std::string::npos);
    REQUIRE(super_talker_md.find("**All implemented interfaces:** [`Greeter`](Greeter.md)") != std::string::npos);
    REQUIRE(super_talker_md.find("**Known direct subclasses (this module):** *(none)*") != std::string::npos);

    // Regression: enum direct-only ascendant/descendant relationships.
    const std::string status_md = read_text_file(module_root / "Status.md");
    REQUIRE(status_md.find("**Base enum:** *(none)*") != std::string::npos);
    REQUIRE(status_md.find("**Derived enums (this module, direct only):** [`ExtendedStatus`](ExtendedStatus.md)") != std::string::npos);
    const std::string extended_status_md = read_text_file(module_root / "ExtendedStatus.md");
    REQUIRE(extended_status_md.find("**Base enum:** [`Status`](Status.md)") != std::string::npos);

    // Regression: union direct-only ascendant/descendant relationships.
    const std::string result_md = read_text_file(module_root / "Result.md");
    REQUIRE(result_md.find("**Base union:** *(none)*") != std::string::npos);
    REQUIRE(result_md.find("**Derived unions (this module, direct only):** [`ExtendedResult`](ExtendedResult.md)") != std::string::npos);
    const std::string extended_result_md = read_text_file(module_root / "ExtendedResult.md");
    REQUIRE(extended_result_md.find("**Base union:** [`Result`](Result.md)") != std::string::npos);

    const std::string make_container_md = read_text_file(module_root / "makeContainer.md");
    REQUIRE(make_container_md.find("`template function`") != std::string::npos);
    REQUIRE(make_container_md.find("## Signature") != std::string::npos);
    REQUIRE(make_container_md.find("## Parameters") != std::string::npos);
    REQUIRE(make_container_md.find("## Return Type") != std::string::npos);
    // Structured return type: nested generic reference to Container<T>.
    REQUIRE(make_container_md.find("Container&lt;T&gt;") != std::string::npos);
    REQUIRE(make_container_md.find("## Declaration Source") != std::string::npos);
    REQUIRE(make_container_md.find("makeContainer(value : T) : Container<T>") != std::string::npos);

    const std::string refs = read_text_file(module_root / "name-references.md");
    REQUIRE(refs.find("| Name | Kind | Scope | Type | Brief |") != std::string::npos);
    REQUIRE(refs.find("[`Thing`](Thing.md)") != std::string::npos);
    REQUIRE(refs.find("Main sample class used by docgen tests.") != std::string::npos);
    REQUIRE(refs.find("`namespace`") != std::string::npos);
    REQUIRE(refs.find("[doc](") == std::string::npos);
    // Regression: instantiation artifacts must not leak into the reference tables.
    REQUIRE(refs.find("Container__int") == std::string::npos);
    REQUIRE(refs.find("makeContainer__int") == std::string::npos);
    // Regression: the reference-table "Name" column shows the human-readable
    // operator form, not the raw internal name.
    REQUIRE(refs.find("[`operator ==`](Thing.md") != std::string::npos);
    REQUIRE(refs.find("[`operator []`](Thing.md") != std::string::npos);
    REQUIRE(refs.find("__operator_eq_") == std::string::npos);
    REQUIRE(refs.find("__operator_ix_") == std::string::npos);

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
    REQUIRE(fs::exists(module_root / "Container.html"));
    REQUIRE(fs::exists(module_root / "makeContainer.html"));

    const std::string root_index = read_text_file(module_root / "index.html");
    REQUIRE(root_index.find("<a href=\"util/index.html\">util</a>") != std::string::npos);
    REQUIRE(root_index.find("<a href=\"Thing.html\">Thing</a>") != std::string::npos);
    REQUIRE(root_index.find("<a href=\"Container.html\">Container&lt;T&gt;</a>") != std::string::npos);
    REQUIRE(root_index.find("<a href=\"#fn-makething-0\">makeThing() : void</a>") != std::string::npos);
    REQUIRE(root_index.find("<a href=\"makeContainer.html\">makeContainer&lt;T&gt;</a>") != std::string::npos);
    REQUIRE(root_index.find("<a href=\"#var-version-0\">VERSION</a>") != std::string::npos);
    REQUIRE(root_index.find("<th>Brief</th>") != std::string::npos);
    REQUIRE(root_index.find("Main sample class used by docgen tests.") != std::string::npos);
    REQUIRE(root_index.find("Create a Thing.") != std::string::npos);
    REQUIRE(root_index.find("Public API version exposed to consumers.") != std::string::npos);
    REQUIRE(root_index.find(">open<") == std::string::npos);
    REQUIRE(root_index.find(">detail<") == std::string::npos);
    // Regression: the inert "mirror" child namespace must not overwrite this page.
    REQUIRE(root_index.find("mirror") == std::string::npos);
    // Regression: concrete template instantiation artifacts must not appear
    // in the namespace index at all.
    REQUIRE(root_index.find("Container__int") == std::string::npos);
    REQUIRE(root_index.find("makeContainer__int") == std::string::npos);

    // Regression: instantiation artifacts must not get their own dedicated page,
    // nor a nested member-template page (e.g. a synthesized ::construct).
    REQUIRE_FALSE(fs::exists(module_root / "Container__int.html"));
    REQUIRE_FALSE(fs::exists(module_root / "makeContainer__int.html"));
    REQUIRE_FALSE(fs::exists(module_root / "construct.html"));

    const std::string thing_page = read_text_file(module_root / "Thing.html");
    REQUIRE(thing_page.find("<a href=\"Thing.Inner.html\">Inner</a>") != std::string::npos);
    REQUIRE(thing_page.find("Checks that the thing responds to a liveness probe.") != std::string::npos);
    REQUIRE(thing_page.find("Nested helper payload.") != std::string::npos);
    REQUIRE(thing_page.find(">detail<") == std::string::npos);
    // Regression: a method returning a concrete template instantiation must
    // render using the real generic arguments ("Container<int32>"), not the
    // compiler-synthesized/mangled name ("Container__int").
    REQUIRE(thing_page.find("getContainer() : demo::mod::Container&lt;int32&gt;") != std::string::npos);
    REQUIRE(thing_page.find("Container__int") == std::string::npos);
    // Regression: operator methods render with human-readable K declaration
    // syntax ("operator ==", "operator []"), not the raw internal name.
    REQUIRE(thing_page.find("operator ==(other: &amp;demo::mod::Thing) : bool") != std::string::npos);
    REQUIRE(thing_page.find("operator [](index: unsigned int32) : int32") != std::string::npos);
    REQUIRE(thing_page.find("__operator_eq_") == std::string::npos);
    REQUIRE(thing_page.find("__operator_ix_") == std::string::npos);

    const std::string container_page = read_text_file(module_root / "Container.html");
    REQUIRE(container_page.find("template class") != std::string::npos);
    REQUIRE(container_page.find("Template Parameters") != std::string::npos);
    // Structured rendering: fields/methods documented as distinct entities.
    REQUIRE(container_page.find("Member Variables") != std::string::npos);
    REQUIRE(container_page.find(">value</a>") != std::string::npos);
    REQUIRE(container_page.find("<code>T</code>") != std::string::npos);
    // Nested generic reference (Box<T>) preserved structurally.
    REQUIRE(container_page.find("Box&lt;T&gt;") != std::string::npos);
    REQUIRE(container_page.find("getValue() : T") != std::string::npos);
    REQUIRE(container_page.find("Declaration Source") != std::string::npos);
    REQUIRE(container_page.find("class Container {") != std::string::npos);
    // Regression: a template's raw generic base reference ("Boxed<T>") must
    // resolve, via the bare short-name alias, to the "Boxed" template def's
    // own dedicated page.
    REQUIRE(container_page.find("Inheritance") != std::string::npos);
    REQUIRE(container_page.find("<a href=\"Boxed.html\"><code>Boxed&lt;T&gt;</code></a>") != std::string::npos);

    // Regression: regular class/interface hierarchy — direct bases, the
    // transitive "implemented interfaces" closure, module-local direct
    // subclasses, and (interfaces only) module-local direct-or-indirect
    // implementors, all linked to their dedicated pages.
    const std::string greeter_page = read_text_file(module_root / "Greeter.html");
    REQUIRE(greeter_page.find("<strong>Known direct subclasses (this module):</strong> <a href=\"Talker.html\"><code>Talker</code></a>") != std::string::npos);
    REQUIRE(greeter_page.find("<strong>Known implementors, direct or indirect (this module):</strong> <a href=\"SuperTalker.html\"><code>SuperTalker</code></a>, <a href=\"Talker.html\"><code>Talker</code></a>") != std::string::npos);

    const std::string talker_page = read_text_file(module_root / "Talker.html");
    REQUIRE(talker_page.find("<strong>Base types:</strong> <a href=\"Greeter.html\"><code>Greeter</code></a>") != std::string::npos);
    REQUIRE(talker_page.find("<strong>All implemented interfaces:</strong> <a href=\"Greeter.html\"><code>Greeter</code></a>") != std::string::npos);

    const std::string super_talker_page = read_text_file(module_root / "SuperTalker.html");
    REQUIRE(super_talker_page.find("<strong>Base types:</strong> <a href=\"Talker.html\"><code>Talker</code></a>") != std::string::npos);
    REQUIRE(super_talker_page.find("<strong>All implemented interfaces:</strong> <a href=\"Greeter.html\"><code>Greeter</code></a>") != std::string::npos);

    // Regression: enum/union direct-only ascendant/descendant relationships.
    const std::string status_page = read_text_file(module_root / "Status.html");
    REQUIRE(status_page.find("<strong>Derived enums (this module, direct only):</strong> <a href=\"ExtendedStatus.html\"><code>ExtendedStatus</code></a>") != std::string::npos);
    const std::string result_page = read_text_file(module_root / "Result.html");
    REQUIRE(result_page.find("<strong>Derived unions (this module, direct only):</strong> <a href=\"ExtendedResult.html\"><code>ExtendedResult</code></a>") != std::string::npos);

    const std::string make_container_page = read_text_file(module_root / "makeContainer.html");
    REQUIRE(make_container_page.find("template function") != std::string::npos);
    REQUIRE(make_container_page.find("Signature") != std::string::npos);
    REQUIRE(make_container_page.find("Parameters") != std::string::npos);
    REQUIRE(make_container_page.find("Return Type") != std::string::npos);
    REQUIRE(make_container_page.find("Container&lt;T&gt;") != std::string::npos);
    REQUIRE(make_container_page.find("Declaration Source") != std::string::npos);

    const std::string name_refs = read_text_file(module_root / "name-references.html");
    REQUIRE(name_refs.find("<th>Brief</th>") != std::string::npos);
    REQUIRE(name_refs.find("<a href=\"Thing.html\">Thing</a>") != std::string::npos);
    REQUIRE(name_refs.find("Main sample class used by docgen tests.") != std::string::npos);
    REQUIRE(name_refs.find(">doc<") == std::string::npos);
    // Regression: instantiation artifacts must not leak into the reference tables.
    REQUIRE(name_refs.find("Container__int") == std::string::npos);
    REQUIRE(name_refs.find("makeContainer__int") == std::string::npos);
    // Regression: operator methods render with human-readable K declaration
    // syntax in the reference table's "Name" column, not the raw internal name.
    REQUIRE(name_refs.find(">operator ==</a>") != std::string::npos);
    REQUIRE(name_refs.find(">operator []</a>") != std::string::npos);
    REQUIRE(name_refs.find("__operator_eq_") == std::string::npos);
    REQUIRE(name_refs.find("__operator_ix_") == std::string::npos);

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


