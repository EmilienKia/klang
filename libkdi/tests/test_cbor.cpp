#include <catch2/catch_all.hpp>
#include "kdi.hpp"

#include <sstream>

using namespace kdi;

// Helper: build a minimal valid kdi_file
static kdi_file make_minimal_file() {
    kdi_file f;
    f.header.module_name   = "test::mod";
    f.header.lib_base      = "test.mod";
    f.header.target_triple = "x86_64-pc-linux-gnu";
    f.header.compiler_ver  = "klangc-test";
    f.unit.name            = "test::mod";

    // one global function
    kdi_namespace root;
    kdi_function fn;
    fn.name         = "answer";
    fn.fq_name      = "test::mod::answer";
    fn.return_type  = kdi_type::make_int(32);
    fn.mangled_name = "_KFN4test3mod6answerEv";
    root.functions.push_back(fn);
    f.unit.root_ns  = std::move(root);

    return f;
}

// Helper: build a file with an aggregate
static kdi_file make_aggregate_file() {
    kdi_file f;
    f.header.module_name = "geo";
    f.header.lib_base    = "geo";
    f.unit.name          = "geo";

    // Register type
    f.types.aggregates.push_back({"geo::Point", "_KS3geo5Point"});

    kdi_aggregate agg;
    agg.kind         = kdi_aggregate_kind::struct_;
    agg.name         = "Point";
    agg.fq_name      = "geo::Point";
    agg.mangled_name = "_KS3geo5Point";
    agg.visibility   = kdi_visibility::public_;

    // layout: two int32 members
    kdi_layout_member mx;
    mx.llvm_field_index = 0;
    mx.name             = "x";
    mx.fq_name          = "geo::Point::x";
    mx.visibility       = kdi_visibility::public_;
    mx.type             = kdi_type::make_int(32);
    mx.mangled_name     = "_KVM3geo5Point1x";
    agg.layout.push_back(mx);

    kdi_layout_member my = mx;
    my.llvm_field_index  = 1;
    my.name              = "y";
    my.fq_name           = "geo::Point::y";
    my.mangled_name      = "_KVM3geo5Point1y";
    agg.layout.push_back(my);

    // default constructor
    kdi_constructor ctor;
    ctor.visibility   = kdi_visibility::public_;
    ctor.mangled_name = "_KFMC1N3geo5PointE";
    ctor.mangled_name_c2 = "_KFMC2N3geo5PointE";
    agg.constructors.push_back(ctor);

    // a method
    kdi_method m;
    m.name         = "norm";
    m.fq_name      = "geo::Point::norm";
    m.visibility   = kdi_visibility::public_;
    m.return_type  = kdi_type::make_float(64);
    m.mangled_name = "_KFMN3geo5Point4normEv";
    agg.methods.push_back(m);

    f.unit.root_ns.aggregates.push_back(agg);
    return f;
}

// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("CBOR: minimal file round-trips", "[cbor]") {
    auto original = make_minimal_file();

    std::ostringstream oss(std::ios::binary);
    REQUIRE_NOTHROW(kdi_write_cbor(original, oss));

    std::string bytes = oss.str();
    REQUIRE(!bytes.empty());

    std::istringstream iss(bytes, std::ios::binary);
    kdi_file restored;
    REQUIRE_NOTHROW(restored = kdi_read_cbor(iss));

    REQUIRE(restored.header.schema_major == KDI_SCHEMA_MAJOR);
    REQUIRE(restored.header.schema_minor == KDI_SCHEMA_MINOR);
    REQUIRE(restored.header.module_name  == "test::mod");
    REQUIRE(restored.header.lib_base     == "test.mod");
    REQUIRE(restored.unit.name           == "test::mod");
    REQUIRE(restored.unit.root_ns.functions.size() == 1u);
    REQUIRE(restored.unit.root_ns.functions[0].name == "answer");
    REQUIRE(restored.unit.root_ns.functions[0].mangled_name == "_KFN4test3mod6answerEv");
}

TEST_CASE("CBOR: aggregate file round-trips", "[cbor]") {
    auto original = make_aggregate_file();

    std::ostringstream oss(std::ios::binary);
    REQUIRE_NOTHROW(kdi_write_cbor(original, oss));

    std::istringstream iss(oss.str(), std::ios::binary);
    kdi_file restored;
    REQUIRE_NOTHROW(restored = kdi_read_cbor(iss));

    REQUIRE(restored.types.aggregates.size() == 1u);
    REQUIRE(restored.types.aggregates[0].fq_name == "geo::Point");

    REQUIRE(restored.unit.root_ns.aggregates.size() == 1u);
    auto& agg = restored.unit.root_ns.aggregates[0];
    REQUIRE(agg.name    == "Point");
    REQUIRE(agg.fq_name == "geo::Point");
    REQUIRE(agg.layout.size() == 2u);
    REQUIRE(std::holds_alternative<kdi_layout_member>(agg.layout[0]));
    REQUIRE(std::get<kdi_layout_member>(agg.layout[0]).name == "x");
    REQUIRE(std::get<kdi_layout_member>(agg.layout[1]).name == "y");
    REQUIRE(agg.constructors.size() == 1u);
    REQUIRE(agg.constructors[0].mangled_name == "_KFMC1N3geo5PointE");
    REQUIRE(agg.methods.size() == 1u);
    REQUIRE(agg.methods[0].name == "norm");
}

TEST_CASE("CBOR: opaque_block round-trips", "[cbor]") {
    kdi_file f;
    f.header.module_name = "priv::mod";
    f.unit.name          = "priv::mod";
    f.types.aggregates.push_back({"priv::Impl", "_KS4priv4Impl"});

    kdi_aggregate agg;
    agg.name    = "Impl";
    agg.fq_name = "priv::Impl";

    // public vptr at field 0
    kdi_layout_vptr vp;
    vp.llvm_field_index = 0;
    vp.vtable_symbol    = "_KTVN4priv4ImplE";
    agg.layout.push_back(vp);

    // opaque block covering 2 private int32 fields
    kdi_layout_opaque_block ob;
    ob.llvm_field_index = 1;
    ob.field_count      = 2;
    ob.size_bits        = 64;
    agg.layout.push_back(ob);

    f.unit.root_ns.aggregates.push_back(agg);

    std::ostringstream oss(std::ios::binary);
    REQUIRE_NOTHROW(kdi_write_cbor(f, oss));

    std::istringstream iss(oss.str(), std::ios::binary);
    kdi_file restored;
    REQUIRE_NOTHROW(restored = kdi_read_cbor(iss));

    auto& ragg = restored.unit.root_ns.aggregates[0];
    REQUIRE(ragg.layout.size() == 2u);
    REQUIRE(std::holds_alternative<kdi_layout_vptr>(ragg.layout[0]));
    REQUIRE(std::get<kdi_layout_vptr>(ragg.layout[0]).vtable_symbol == "_KTVN4priv4ImplE");
    REQUIRE(std::holds_alternative<kdi_layout_opaque_block>(ragg.layout[1]));
    auto& rob = std::get<kdi_layout_opaque_block>(ragg.layout[1]);
    REQUIRE(rob.field_count == 2u);
    REQUIRE(rob.size_bits   == 64u);
}

TEST_CASE("CBOR: vtable round-trips", "[cbor]") {
    kdi_file f;
    f.header.module_name = "virt::mod";
    f.unit.name          = "virt::mod";

    kdi_aggregate agg;
    agg.kind    = kdi_aggregate_kind::class_;
    agg.name    = "Animal";
    agg.fq_name = "virt::Animal";

    kdi_vtable vt;
    vt.vtable_symbol = "_KTVNvirtAnimalE";
    vt.rtti_symbol   = "_KTRINvirtAnimalE";

    kdi_vtable_slot s0;
    s0.slot_index       = 0;
    s0.introducing_func = "virt::Animal::speak";
    s0.is_abstract      = true;
    vt.slots.push_back(s0);

    agg.vtable = vt;
    f.unit.root_ns.aggregates.push_back(agg);

    std::ostringstream oss(std::ios::binary);
    REQUIRE_NOTHROW(kdi_write_cbor(f, oss));

    std::istringstream iss(oss.str(), std::ios::binary);
    kdi_file restored;
    REQUIRE_NOTHROW(restored = kdi_read_cbor(iss));

    auto& ragg = restored.unit.root_ns.aggregates[0];
    REQUIRE(ragg.vtable.has_value());
    REQUIRE(ragg.vtable->vtable_symbol == "_KTVNvirtAnimalE");
    REQUIRE(ragg.vtable->slots.size() == 1u);
    REQUIRE(ragg.vtable->slots[0].is_abstract);
    REQUIRE(ragg.vtable->slots[0].introducing_func == "virt::Animal::speak");
}

TEST_CASE("CBOR: all type kinds round-trip", "[cbor][types]") {
    kdi_file f;
    f.header.module_name = "types::test";
    f.unit.name          = "types::test";

    auto add_fn = [&](const std::string& name, kdi_type ret) {
        kdi_function fn;
        fn.name         = name;
        fn.fq_name      = "types::test::" + name;
        fn.return_type  = std::move(ret);
        fn.mangled_name = "_KFN" + name;
        f.unit.root_ns.functions.push_back(fn);
    };

    add_fn("f_void",   kdi_type{kdi_void_type{}});
    add_fn("f_bool",   kdi_type{kdi_bool_type{}});
    add_fn("f_char",   kdi_type{kdi_char_type{}});
    add_fn("f_int32",  kdi_type::make_int(32, true));
    add_fn("f_uint64", kdi_type::make_int(64, false));
    add_fn("f_f32",    kdi_type::make_float(32));
    add_fn("f_f64",    kdi_type::make_float(64));
    add_fn("f_ref",    kdi_type{kdi_ref_type{std::make_shared<kdi_type>(kdi_type::make_int(32))}});
    add_fn("f_ptr",    kdi_type{kdi_ptr_type{std::make_shared<kdi_type>(kdi_type::make_int(32))}});
    add_fn("f_link",   kdi_type{kdi_link_type{std::make_shared<kdi_type>(kdi_type::make_int(32))}});
    add_fn("f_pinned", kdi_type{kdi_pinned_type{std::make_shared<kdi_type>(kdi_type::make_int(32))}});
    add_fn("f_const",  kdi_type{kdi_const_type{std::make_shared<kdi_type>(kdi_type::make_int(32))}});
    add_fn("f_arr",    kdi_type{kdi_array_type{std::make_shared<kdi_type>(kdi_type::make_int(32))}});
    add_fn("f_sarr",   kdi_type{kdi_sized_array_type{std::make_shared<kdi_type>(kdi_type::make_int(32)), 10}});
    add_fn("f_agg",    kdi_type::make_aggregate("types::Foo"));

    std::ostringstream oss(std::ios::binary);
    REQUIRE_NOTHROW(kdi_write_cbor(f, oss));

    std::istringstream iss(oss.str(), std::ios::binary);
    kdi_file restored;
    REQUIRE_NOTHROW(restored = kdi_read_cbor(iss));

    auto& fns = restored.unit.root_ns.functions;
    REQUIRE(fns.size() == 15u);
    REQUIRE(std::holds_alternative<kdi_void_type>(fns[0].return_type.value));
    REQUIRE(std::holds_alternative<kdi_bool_type>(fns[1].return_type.value));
    REQUIRE(std::holds_alternative<kdi_char_type>(fns[2].return_type.value));
    auto& i32 = std::get<kdi_int_type>(fns[3].return_type.value);
    REQUIRE(i32.bits == 32); REQUIRE(i32.is_signed);
    auto& u64 = std::get<kdi_int_type>(fns[4].return_type.value);
    REQUIRE(u64.bits == 64); REQUIRE(!u64.is_signed);
    REQUIRE(std::get<kdi_float_type>(fns[5].return_type.value).bits == 32u);
    REQUIRE(std::get<kdi_float_type>(fns[6].return_type.value).bits == 64u);
    REQUIRE(std::holds_alternative<kdi_ref_type>(fns[7].return_type.value));
    REQUIRE(std::holds_alternative<kdi_ptr_type>(fns[8].return_type.value));
    REQUIRE(std::holds_alternative<kdi_link_type>(fns[9].return_type.value));
    REQUIRE(std::holds_alternative<kdi_pinned_type>(fns[10].return_type.value));
    REQUIRE(std::holds_alternative<kdi_const_type>(fns[11].return_type.value));
    REQUIRE(std::holds_alternative<kdi_array_type>(fns[12].return_type.value));
    auto& sarr = std::get<kdi_sized_array_type>(fns[13].return_type.value);
    REQUIRE(sarr.size == 10u);
    REQUIRE(std::get<kdi_aggregate_ref>(fns[14].return_type.value).fq_name == "types::Foo");
}

TEST_CASE("CBOR: file/path helpers work", "[cbor]") {
    auto f = make_aggregate_file();
    std::string path = "/tmp/test_kdi_cbor_roundtrip.kdi";
    REQUIRE(kdi_write_cbor_file(f, path));
    kdi_file restored;
    REQUIRE_NOTHROW(restored = kdi_read_cbor_file(path));
    REQUIRE(restored.header.module_name == "geo");
    REQUIRE(restored.unit.root_ns.aggregates[0].name == "Point");
    std::remove(path.c_str());
}

