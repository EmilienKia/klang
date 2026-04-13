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
#include "kdi_json.hpp"

#include <filesystem>
#include <sstream>

using namespace kdi;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/** Round-trip a kdi_file through JSON (write → parse → compare header). */
static kdi_file json_round_trip(const kdi_file& original) {
    std::ostringstream oss;
    kdi_write_json(original, oss);
    std::istringstream iss(oss.str());
    return kdi_read_json(iss);
}

static kdi_file make_minimal_file() {
    kdi_file f;
    f.header.module_name = "test::json";
    f.header.lib_base    = "test.json";
    f.header.lib_path    = "/tmp/libtest.json.so";
    f.header.compiler_ver = "klangc-test";
    f.unit.name = "test::json";
    return f;
}

// ─────────────────────────────────────────────────────────────────────────────
// Header round-trip
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("JSON: minimal file round-trips correctly", "[json][roundtrip]") {
    auto f = make_minimal_file();
    auto f2 = json_round_trip(f);
    REQUIRE( f2.header.schema_major  == f.header.schema_major );
    REQUIRE( f2.header.schema_minor  == f.header.schema_minor );
    REQUIRE( f2.header.module_name   == f.header.module_name );
    REQUIRE( f2.header.lib_base      == f.header.lib_base );
    REQUIRE( f2.header.lib_path      == f.header.lib_path );
    REQUIRE( f2.header.compiler_ver  == f.header.compiler_ver );
}

// ─────────────────────────────────────────────────────────────────────────────
// Type round-trips
// ─────────────────────────────────────────────────────────────────────────────

static kdi_type rt_type(const kdi_type& t) {
    kdi_file f = make_minimal_file();
    kdi_function fn;
    fn.name         = "f";
    fn.fq_name      = "::f";
    fn.return_type  = t;
    fn.mangled_name = "_Kf";
    fn.llvm_def     = "declare void @_Kf()";
    f.unit.root_ns.functions.push_back(fn);
    auto f2 = json_round_trip(f);
    return f2.unit.root_ns.functions.at(0).return_type;
}

TEST_CASE("JSON: void type round-trips", "[json][type]") {
    auto t = rt_type(kdi_type::make_void());
    REQUIRE( std::holds_alternative<kdi_void_type>(t.value) );
}
TEST_CASE("JSON: bool type round-trips", "[json][type]") {
    auto t = rt_type(kdi_type::make_bool());
    REQUIRE( std::holds_alternative<kdi_bool_type>(t.value) );
}
TEST_CASE("JSON: int32 type round-trips", "[json][type]") {
    auto t = rt_type(kdi_type::make_int(32, true));
    REQUIRE( std::holds_alternative<kdi_int_type>(t.value) );
    REQUIRE( std::get<kdi_int_type>(t.value).bits == 32 );
    REQUIRE( std::get<kdi_int_type>(t.value).is_signed == true );
}
TEST_CASE("JSON: uint64 type round-trips", "[json][type]") {
    auto t = rt_type(kdi_type::make_int(64, false));
    REQUIRE( std::holds_alternative<kdi_int_type>(t.value) );
    REQUIRE( std::get<kdi_int_type>(t.value).bits == 64 );
    REQUIRE( std::get<kdi_int_type>(t.value).is_signed == false );
}
TEST_CASE("JSON: float64 type round-trips", "[json][type]") {
    auto t = rt_type(kdi_type::make_float(64));
    REQUIRE( std::holds_alternative<kdi_float_type>(t.value) );
    REQUIRE( std::get<kdi_float_type>(t.value).bits == 64 );
}
TEST_CASE("JSON: ref type round-trips", "[json][type]") {
    kdi_ref_type r; r.inner = std::make_shared<kdi_type>(kdi_type::make_int(32));
    auto t = rt_type(kdi_type{std::move(r)});
    REQUIRE( std::holds_alternative<kdi_ref_type>(t.value) );
}
TEST_CASE("JSON: ptr type round-trips", "[json][type]") {
    kdi_ptr_type p; p.inner = std::make_shared<kdi_type>(kdi_type::make_bool());
    auto t = rt_type(kdi_type{std::move(p)});
    REQUIRE( std::holds_alternative<kdi_ptr_type>(t.value) );
}
TEST_CASE("JSON: const type round-trips", "[json][type]") {
    kdi_const_type c; c.inner = std::make_shared<kdi_type>(kdi_type::make_int(32));
    auto t = rt_type(kdi_type{std::move(c)});
    REQUIRE( std::holds_alternative<kdi_const_type>(t.value) );
}
TEST_CASE("JSON: sized_array type round-trips", "[json][type]") {
    kdi_sized_array_type a;
    a.elem = std::make_shared<kdi_type>(kdi_type::make_int(8));
    a.size = 42;
    auto t = rt_type(kdi_type{std::move(a)});
    REQUIRE( std::holds_alternative<kdi_sized_array_type>(t.value) );
    REQUIRE( std::get<kdi_sized_array_type>(t.value).size == 42 );
}
TEST_CASE("JSON: fn_ref type round-trips", "[json][type]") {
    kdi_fn_ref_type f;
    f.ret = std::make_shared<kdi_type>(kdi_type::make_void());
    f.params.push_back(std::make_shared<kdi_type>(kdi_type::make_int(32)));
    auto t = rt_type(kdi_type{std::move(f)});
    REQUIRE( std::holds_alternative<kdi_fn_ref_type>(t.value) );
    REQUIRE( std::get<kdi_fn_ref_type>(t.value).params.size() == 1 );
}
TEST_CASE("JSON: aggregate_ref type round-trips", "[json][type]") {
    auto t = rt_type(kdi_type::make_aggregate("my::Foo"));
    REQUIRE( std::holds_alternative<kdi_aggregate_ref>(t.value) );
    REQUIRE( std::get<kdi_aggregate_ref>(t.value).fq_name == "my::Foo" );
}

// ─────────────────────────────────────────────────────────────────────────────
// Function / variable round-trip
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("JSON: function with params round-trips", "[json][function]") {
    kdi_file f = make_minimal_file();
    kdi_function fn;
    fn.name         = "add";
    fn.fq_name      = "::math::add";
    fn.visibility   = kdi_visibility::public_;
    fn.is_static    = false;
    fn.return_type  = kdi_type::make_int(32);
    fn.mangled_name = "_KFN4math3addEii";
    fn.llvm_def     = "declare i32 @_KFN4math3addEii(i32, i32)";
    kdi_param p1; p1.name = "a"; p1.type = kdi_type::make_int(32);
    kdi_param p2; p2.name = "b"; p2.type = kdi_type::make_int(32);
    fn.params = {p1, p2};
    f.unit.root_ns.functions.push_back(fn);

    auto f2 = json_round_trip(f);
    REQUIRE( f2.unit.root_ns.functions.size() == 1 );
    auto& fn2 = f2.unit.root_ns.functions[0];
    REQUIRE( fn2.name         == "add" );
    REQUIRE( fn2.fq_name      == "::math::add" );
    REQUIRE( fn2.mangled_name == "_KFN4math3addEii" );
    REQUIRE( fn2.params.size() == 2 );
    REQUIRE( fn2.params[0].name == "a" );
    REQUIRE( fn2.params[1].name == "b" );
    REQUIRE( std::holds_alternative<kdi_int_type>(fn2.return_type.value) );
}

TEST_CASE("JSON: global variable round-trips", "[json][variable]") {
    kdi_file f = make_minimal_file();
    kdi_variable v;
    v.name         = "g_count";
    v.fq_name      = "::g_count";
    v.visibility   = kdi_visibility::public_;
    v.type         = kdi_type::make_int(32);
    v.mangled_name = "_KN7g_countE";
    f.unit.root_ns.variables.push_back(v);

    auto f2 = json_round_trip(f);
    REQUIRE( f2.unit.root_ns.variables.size() == 1 );
    REQUIRE( f2.unit.root_ns.variables[0].name == "g_count" );
    REQUIRE( f2.unit.root_ns.variables[0].mangled_name == "_KN7g_countE" );
}

// ─────────────────────────────────────────────────────────────────────────────
// Aggregate round-trip
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("JSON: aggregate with method and layout round-trips", "[json][aggregate]") {
    kdi_file f = make_minimal_file();
    kdi_aggregate agg;
    agg.kind         = kdi_aggregate_kind::struct_;
    agg.name         = "Point";
    agg.fq_name      = "::math::Point";
    agg.mangled_name = "_KN4math5PointE";
    agg.visibility   = kdi_visibility::public_;

    // layout: one public int member
    kdi_layout_member lm;
    lm.name             = "x";
    lm.fq_name          = "::math::Point::x";
    lm.visibility       = kdi_visibility::public_;
    lm.llvm_field_index = 0;
    lm.type             = kdi_type::make_int(32);
    lm.mangled_name     = "_KN4math5Point1xE";
    agg.layout.push_back(lm);

    // method
    kdi_method m;
    m.name            = "len";
    m.fq_name         = "::math::Point::len";
    m.visibility      = kdi_visibility::public_;
    m.is_const_member = true;
    m.return_type     = kdi_type::make_float(32);
    m.mangled_name    = "_KFN4math5Point3lenEv";
    m.llvm_def        = "declare float @_KFN4math5Point3lenEv(%struct.math.Point* %this)";
    agg.methods.push_back(m);

    agg.llvm_def = "%struct.math.Point = type { i32 }";
    f.unit.root_ns.aggregates.push_back(agg);
    auto f2 = json_round_trip(f);

    REQUIRE( f2.unit.root_ns.aggregates.size() == 1 );
    auto& a2 = f2.unit.root_ns.aggregates[0];
    REQUIRE( a2.name == "Point" );
    REQUIRE( a2.mangled_name == "_KN4math5PointE" );
    REQUIRE( a2.layout.size() == 1 );
    REQUIRE( std::holds_alternative<kdi_layout_member>(a2.layout[0]) );
    REQUIRE( std::get<kdi_layout_member>(a2.layout[0]).name == "x" );
    REQUIRE( a2.methods.size() == 1 );
    REQUIRE( a2.methods[0].name == "len" );
    REQUIRE( a2.methods[0].is_const_member == true );
}

// ─────────────────────────────────────────────────────────────────────────────
// Aggregate nesting: is_static_nested, enclosing_fq_name, nested children
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("JSON: nested aggregate with enclosing info round-trips", "[json][aggregate][nested]") {
    kdi_file f = make_minimal_file();

    // Outer aggregate
    kdi_aggregate outer;
    outer.kind         = kdi_aggregate_kind::class_;
    outer.name         = "Outer";
    outer.fq_name      = "::ns::Outer";
    outer.mangled_name = "_KN2ns5OuterE";
    outer.llvm_def     = "%struct.ns.Outer = type { ptr }";

    // Inner aggregate (static nested, with enclosing reference)
    kdi_aggregate inner;
    inner.kind              = kdi_aggregate_kind::class_;
    inner.name              = "Inner";
    inner.fq_name           = "::ns::Outer::Inner";
    inner.mangled_name      = "_KN2ns5Outer5InnerE";
    inner.is_static_nested  = true;
    inner.enclosing_fq_name = "::ns::Outer";
    inner.llvm_def          = "%struct.ns.Outer.Inner = type { i32 }";

    outer.nested.push_back(inner);
    f.unit.root_ns.aggregates.push_back(outer);

    auto f2 = json_round_trip(f);

    REQUIRE( f2.unit.root_ns.aggregates.size() == 1 );
    auto& o2 = f2.unit.root_ns.aggregates[0];
    REQUIRE( o2.name == "Outer" );
    REQUIRE( o2.is_static_nested == false );
    REQUIRE( o2.enclosing_fq_name.empty() );

    REQUIRE( o2.nested.size() == 1 );
    auto& i2 = o2.nested[0];
    REQUIRE( i2.name == "Inner" );
    REQUIRE( i2.fq_name == "::ns::Outer::Inner" );
    REQUIRE( i2.is_static_nested == true );
    REQUIRE( i2.enclosing_fq_name == "::ns::Outer" );
}

// ─────────────────────────────────────────────────────────────────────────────
// Layout field variants
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("JSON: all layout field variants round-trip", "[json][layout]") {
    kdi_file f = make_minimal_file();
    kdi_aggregate agg;
    agg.name    = "S"; agg.fq_name = "::S"; agg.mangled_name = "_KS";
    agg.llvm_def = "%struct.S = type { i8**, i8*, i8*, i32, i32, i32 }";

    kdi_layout_vptr vp; vp.llvm_field_index = 0; vp.vtable_symbol = "_ZTV1S";
    agg.layout.push_back(vp);

    kdi_layout_base_subobject bs; bs.llvm_field_index = 1; bs.base_fq_name = "::Base";
    agg.layout.push_back(bs);

    kdi_layout_opaque_block ob; ob.llvm_field_index = 2; ob.field_count = 3; ob.size_bits = 96;
    agg.layout.push_back(ob);

    kdi_layout_parent_ref pr; pr.llvm_field_index = 5; pr.parent_fq_name = "::Outer";
    agg.layout.push_back(pr);

    f.unit.root_ns.aggregates.push_back(agg);
    auto f2 = json_round_trip(f);
    auto& a2 = f2.unit.root_ns.aggregates[0];

    REQUIRE( std::holds_alternative<kdi_layout_vptr>(a2.layout[0]) );
    REQUIRE( std::get<kdi_layout_vptr>(a2.layout[0]).vtable_symbol == "_ZTV1S" );

    REQUIRE( std::holds_alternative<kdi_layout_base_subobject>(a2.layout[1]) );
    REQUIRE( std::get<kdi_layout_base_subobject>(a2.layout[1]).base_fq_name == "::Base" );

    REQUIRE( std::holds_alternative<kdi_layout_opaque_block>(a2.layout[2]) );
    REQUIRE( std::get<kdi_layout_opaque_block>(a2.layout[2]).size_bits == 96 );

    REQUIRE( std::holds_alternative<kdi_layout_parent_ref>(a2.layout[3]) );
    REQUIRE( std::get<kdi_layout_parent_ref>(a2.layout[3]).parent_fq_name == "::Outer" );
}

// ─────────────────────────────────────────────────────────────────────────────
// File I/O helpers
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("JSON: write/read file helpers produce identical files", "[json][file-io]") {
    auto tmp = std::filesystem::temp_directory_path() / "kdi_test_json.kdi.json";
    auto f   = make_minimal_file();
    kdi_function fn;
    fn.name = "foo"; fn.fq_name = "::foo";
    fn.return_type = kdi_type::make_void(); fn.mangled_name = "_Kfoo";
    fn.llvm_def    = "declare void @_Kfoo()";
    f.unit.root_ns.functions.push_back(fn);

    REQUIRE( kdi_write_json_file(f, tmp.string()) );
    kdi_file f2;
    REQUIRE_NOTHROW( f2 = kdi_read_json_file(tmp.string()) );    REQUIRE( f2.header.module_name == f.header.module_name );
    REQUIRE( f2.unit.root_ns.functions.size() == 1 );
    REQUIRE( f2.unit.root_ns.functions[0].name == "foo" );

    std::filesystem::remove(tmp);
}

TEST_CASE("JSON: invalid JSON throws kdi_json_error", "[json][error]") {
    std::istringstream bad("{ this is not json }");
    REQUIRE_THROWS_AS( kdi_read_json(bad), kdi_json_error );
}

TEST_CASE("JSON: missing required field throws kdi_json_error", "[json][error]") {
    // header present but unit missing root_ns
    std::istringstream j(R"({"header":{"schema_major":0,"schema_minor":1,"module_name":"x"},
                              "unit":{"name":"x"}})");
    // root_ns missing → json::at() throws → wrapped in kdi_json_error
    REQUIRE_THROWS_AS( kdi_read_json(j), kdi_json_error );
}

// ─────────────────────────────────────────────────────────────────────────────
// CBOR ↔ JSON round-trip
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("JSON: CBOR → JSON → CBOR round-trip preserves content", "[json][cbor-roundtrip]") {
    namespace fs = std::filesystem;

    // Build a file with a function
    kdi_file f = make_minimal_file();
    kdi_function fn;
    fn.name = "compute"; fn.fq_name = "::compute";
    fn.return_type = kdi_type::make_int(32); fn.mangled_name = "_Kcompute";
    kdi_param p; p.name = "x"; p.type = kdi_type::make_int(32);
    fn.params.push_back(p);
    f.unit.root_ns.functions.push_back(fn);

    auto kdi_path  = fs::temp_directory_path() / "kdi_cbor_json_rt.kdi";
    auto json_path = fs::temp_directory_path() / "kdi_cbor_json_rt.kdi.json";

    // Write CBOR
    REQUIRE( kdi_write_cbor_file(f, kdi_path.string()) );

    // Read CBOR → write JSON
    auto f_from_cbor = kdi_read_cbor_file(kdi_path.string());
    REQUIRE( kdi_write_json_file(f_from_cbor, json_path.string()) );

    // Read JSON → write CBOR again
    auto f_from_json = kdi_read_json_file(json_path.string());
    REQUIRE( f_from_json.header.module_name == f.header.module_name );
    REQUIRE( f_from_json.unit.root_ns.functions.size() == 1 );
    REQUIRE( f_from_json.unit.root_ns.functions[0].name == "compute" );
    REQUIRE( f_from_json.unit.root_ns.functions[0].params.size() == 1 );

    fs::remove(kdi_path);
    fs::remove(json_path);
}

TEST_CASE("JSON: template_origin round-trips on function", "[json][template]") {
    kdi_file f = make_minimal_file();

    kdi_function fn;
    fn.name         = "identity__int__";
    fn.fq_name      = "test::json::identity__int__";
    fn.return_type  = kdi_type::make_int(32);
    fn.mangled_name = "_KFN8identityIiEi";
    fn.llvm_def     = "declare i32 @_KFN8identityIiEi(i32)";
    fn.params.push_back({"x", kdi_type::make_int(32)});

    kdi_template_origin origin;
    origin.base_name    = "identity";
    origin.base_fq_name = "test::json::identity";
    kdi_template_arg arg1;
    arg1.type_arg = kdi_type::make_int(32);
    origin.args.push_back(arg1);
    fn.template_origin = origin;

    f.unit.root_ns.functions.push_back(fn);

    auto restored = json_round_trip(f);
    REQUIRE(restored.unit.root_ns.functions.size() == 1);
    auto& rfn = restored.unit.root_ns.functions[0];
    REQUIRE(rfn.template_origin.has_value());
    REQUIRE(rfn.template_origin->base_name == "identity");
    REQUIRE(rfn.template_origin->base_fq_name == "test::json::identity");
    REQUIRE(rfn.template_origin->args.size() == 1);
    REQUIRE(rfn.template_origin->args[0].type_arg.has_value());
    auto& targ = std::get<kdi_int_type>(rfn.template_origin->args[0].type_arg->value);
    REQUIRE(targ.bits == 32);
    REQUIRE(targ.is_signed);
}

TEST_CASE("JSON: template_origin round-trips on aggregate", "[json][template]") {
    kdi_file f = make_minimal_file();

    kdi_aggregate agg;
    agg.kind         = kdi_aggregate_kind::struct_;
    agg.name         = "Box__int__";
    agg.fq_name      = "test::json::Box__int__";
    agg.mangled_name = "_KS3BoxIiE";
    agg.llvm_def     = "%struct.Box__int__ = type { i32 }";

    kdi_template_origin origin;
    origin.base_name    = "Box";
    origin.base_fq_name = "test::json::Box";
    kdi_template_arg arg1;
    arg1.type_arg = kdi_type::make_int(32);
    origin.args.push_back(arg1);
    kdi_template_arg arg2;
    arg2.value_arg  = "42";
    arg2.value_type = kdi_type::make_int(32, true);
    origin.args.push_back(arg2);
    agg.template_origin = origin;

    f.unit.root_ns.aggregates.push_back(agg);

    auto restored = json_round_trip(f);
    REQUIRE(restored.unit.root_ns.aggregates.size() == 1);
    auto& ragg = restored.unit.root_ns.aggregates[0];
    REQUIRE(ragg.template_origin.has_value());
    REQUIRE(ragg.template_origin->base_name == "Box");
    REQUIRE(ragg.template_origin->base_fq_name == "test::json::Box");
    REQUIRE(ragg.template_origin->args.size() == 2);
    REQUIRE(ragg.template_origin->args[0].type_arg.has_value());
    REQUIRE(ragg.template_origin->args[1].value_arg.has_value());
    REQUIRE(*ragg.template_origin->args[1].value_arg == "42");
    REQUIRE(ragg.template_origin->args[1].value_type.has_value());
}

TEST_CASE("JSON: template_def round-trips in namespace", "[json][template]") {
    kdi_file f = make_minimal_file();

    kdi_template_def td;
    td.name        = "Pair";
    td.fq_name     = "test::json::Pair";
    td.entity_kind = "struct";
    td.visibility  = "public";
    kdi_template_param tp1;
    tp1.kind = "typename";
    tp1.name = "A";
    td.params.push_back(tp1);
    kdi_template_param tp2;
    tp2.kind = "typename";
    tp2.name = "B";
    tp2.default_type = kdi_type::make_int(32);
    td.params.push_back(tp2);
    td.source = "template<typename A, typename B = int> struct Pair { first: A; second: B; }";
    f.unit.root_ns.template_defs.push_back(td);

    auto restored = json_round_trip(f);
    REQUIRE(restored.unit.root_ns.template_defs.size() == 1);
    auto& rtd = restored.unit.root_ns.template_defs[0];
    REQUIRE(rtd.name == "Pair");
    REQUIRE(rtd.fq_name == "test::json::Pair");
    REQUIRE(rtd.entity_kind == "struct");
    REQUIRE(rtd.params.size() == 2);
    REQUIRE(rtd.params[0].name == "A");
    REQUIRE(rtd.params[1].name == "B");
    REQUIRE(rtd.params[1].default_type.has_value());
    REQUIRE(rtd.source.find("Pair") != std::string::npos);
}

