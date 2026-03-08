#include <catch2/catch_all.hpp>
#include "kdi_types.hpp"
#include "kdi_aggregates.hpp"
#include "kdi_file.hpp"

using namespace kdi;

// ─── kdi_type construction ────────────────────────────────────────────────────

TEST_CASE("kdi_type: make_void holds kdi_void_type", "[dto][types]") {
    auto t = kdi_type::make_void();
    REQUIRE(std::holds_alternative<kdi_void_type>(t.value));
}

TEST_CASE("kdi_type: make_bool holds kdi_bool_type", "[dto][types]") {
    auto t = kdi_type::make_bool();
    REQUIRE(std::holds_alternative<kdi_bool_type>(t.value));
}

TEST_CASE("kdi_type: make_int signed 32", "[dto][types]") {
    auto t = kdi_type::make_int(32, true);
    REQUIRE(std::holds_alternative<kdi_int_type>(t.value));
    auto& i = std::get<kdi_int_type>(t.value);
    REQUIRE(i.bits == 32);
    REQUIRE(i.is_signed);
}

TEST_CASE("kdi_type: make_int unsigned 64", "[dto][types]") {
    auto t = kdi_type::make_int(64, false);
    auto& i = std::get<kdi_int_type>(t.value);
    REQUIRE(i.bits == 64);
    REQUIRE(!i.is_signed);
}

TEST_CASE("kdi_type: make_float 64", "[dto][types]") {
    auto t = kdi_type::make_float(64);
    REQUIRE(std::holds_alternative<kdi_float_type>(t.value));
    REQUIRE(std::get<kdi_float_type>(t.value).bits == 64);
}

TEST_CASE("kdi_type: make_aggregate stores fq_name", "[dto][types]") {
    auto t = kdi_type::make_aggregate("math::Vec3");
    REQUIRE(std::holds_alternative<kdi_aggregate_ref>(t.value));
    REQUIRE(std::get<kdi_aggregate_ref>(t.value).fq_name == "math::Vec3");
}

// ─── kdi_header defaults ─────────────────────────────────────────────────────

TEST_CASE("kdi_header: schema version defaults", "[dto][header]") {
    kdi_header h;
    REQUIRE(h.schema_major == KDI_SCHEMA_MAJOR);
    REQUIRE(h.schema_minor == KDI_SCHEMA_MINOR);
}

TEST_CASE("kdi_header: schema_major is 0 and schema_minor is 1", "[dto][header]") {
    REQUIRE(KDI_SCHEMA_MAJOR == 0u);
    REQUIRE(KDI_SCHEMA_MINOR == 1u);
}

// ─── kdi_aggregate ───────────────────────────────────────────────────────────

TEST_CASE("kdi_aggregate: default kind is struct", "[dto][aggregate]") {
    kdi_aggregate agg;
    REQUIRE(agg.kind == kdi_aggregate_kind::struct_);
}

TEST_CASE("kdi_aggregate: layout_opaque_block stores size_bits", "[dto][aggregate]") {
    kdi_layout_opaque_block ob;
    ob.llvm_field_index = 2;
    ob.field_count      = 3;
    ob.size_bits        = 192;

    kdi_layout_field f = ob;
    REQUIRE(std::holds_alternative<kdi_layout_opaque_block>(f));
    REQUIRE(std::get<kdi_layout_opaque_block>(f).size_bits == 192u);
}

TEST_CASE("kdi_aggregate: layout_vptr stores vtable_symbol", "[dto][aggregate]") {
    kdi_layout_vptr vp;
    vp.llvm_field_index = 0;
    vp.vtable_symbol    = "_KTV4math4Vec3E";

    kdi_layout_field f = vp;
    REQUIRE(std::holds_alternative<kdi_layout_vptr>(f));
    REQUIRE(std::get<kdi_layout_vptr>(f).vtable_symbol == "_KTV4math4Vec3E");
}

// ─── kdi_file round-trip (structural) ────────────────────────────────────────

TEST_CASE("kdi_file: can be constructed and populated", "[dto][file]") {
    kdi_file file;
    file.header.module_name   = "math::utils";
    file.header.lib_base      = "math.utils";
    file.header.target_triple = "x86_64-pc-linux-gnu";
    file.unit.name            = "math::utils";

    kdi_namespace ns;
    ns.name    = "math";
    ns.fq_name = "math";

    kdi_function fn;
    fn.name         = "add";
    fn.fq_name      = "math::add";
    fn.return_type  = kdi_type::make_int(32);
    fn.params.push_back({"a", kdi_type::make_int(32)});
    fn.params.push_back({"b", kdi_type::make_int(32)});
    fn.mangled_name = "_KFN4math3addEii";
    ns.functions.push_back(fn);

    file.unit.root_ns.namespaces.push_back(ns);
    file.types.aggregates.push_back({"math::Vec3", "_KS4math4Vec3"});

    REQUIRE(file.header.module_name == "math::utils");
    REQUIRE(file.unit.root_ns.namespaces.size() == 1u);
    REQUIRE(file.unit.root_ns.namespaces[0].functions.size() == 1u);
    REQUIRE(file.types.aggregates.size() == 1u);
}

