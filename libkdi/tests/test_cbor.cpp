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
    fn.llvm_def     = "declare i32 @_KFN4test3mod6answerEv()";
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
    ctor.visibility      = kdi_visibility::public_;
    ctor.mangled_name    = "_KFMC1N3geo5PointE";
    ctor.mangled_name_c2 = "_KFMC2N3geo5PointE";
    ctor.llvm_def        = "declare void @_KFMC1N3geo5PointE(%struct.geo.Point* %this)";
    agg.constructors.push_back(ctor);

    // a method
    kdi_method m;
    m.name         = "norm";
    m.fq_name      = "geo::Point::norm";
    m.visibility   = kdi_visibility::public_;
    m.return_type  = kdi_type::make_float(64);
    m.mangled_name = "_KFMN3geo5Point4normEv";
    m.llvm_def     = "declare double @_KFMN3geo5Point4normEv(%struct.geo.Point* %this)";
    agg.methods.push_back(m);

    agg.llvm_def = "%struct.geo.Point = type { i32, i32 }";
    f.unit.root_ns.aggregates.push_back(agg);
    return f;
}

static kdi_type rt_type(const kdi_type& t) {
    auto f = make_minimal_file();
    f.unit.root_ns.functions[0].return_type = t;

    std::ostringstream oss(std::ios::binary);
    kdi_write_cbor(f, oss);

    std::istringstream iss(oss.str(), std::ios::binary);
    auto restored = kdi_read_cbor(iss);
    return restored.unit.root_ns.functions.at(0).return_type;
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

TEST_CASE("CBOR: owner type round-trips", "[cbor][type]") {
    auto t = rt_type(kdi_type::make_owner(kdi_type::make_template_param("T")));
    REQUIRE(std::holds_alternative<kdi_owner_type>(t.value));
    auto& inner = *std::get<kdi_owner_type>(t.value).inner;
    REQUIRE(std::holds_alternative<kdi_template_param_ref>(inner.value));
    REQUIRE(std::get<kdi_template_param_ref>(inner.value).name == "T");
}

TEST_CASE("CBOR: generic_ref type round-trips", "[cbor][type]") {
    // Reference to another (possibly still-uninstantiated) template applied
    // with template type arguments, e.g. "MultiSlot<T>" inside "Vector<T>".
    kdi_generic_ref_type gref;
    gref.name = "MultiSlot";
    gref.args.push_back(std::make_shared<kdi_type>(kdi_type::make_template_param("T")));

    auto t = rt_type(kdi_type{std::move(gref)});
    REQUIRE(std::holds_alternative<kdi_generic_ref_type>(t.value));
    auto& g = std::get<kdi_generic_ref_type>(t.value);
    REQUIRE(g.name == "MultiSlot");
    REQUIRE(g.args.size() == 1u);
    REQUIRE(std::holds_alternative<kdi_template_param_ref>(g.args[0]->value));
    REQUIRE(std::get<kdi_template_param_ref>(g.args[0]->value).name == "T");
}

TEST_CASE("CBOR: opaque_block round-trips", "[cbor]") {
    kdi_file f;
    f.header.module_name = "priv::mod";
    f.unit.name          = "priv::mod";
    f.types.aggregates.push_back({"priv::Impl", "_KS4priv4Impl"});

    kdi_aggregate agg;
    agg.name    = "Impl";
    agg.fq_name = "priv::Impl";
    agg.llvm_def = "%struct.priv.Impl = type { i8**, i32, i32 }";

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

TEST_CASE("CBOR: nested aggregate with enclosing info round-trips", "[cbor][aggregate][nested]") {
    kdi_file f;
    f.header.module_name = "nest::mod";
    f.unit.name          = "nest::mod";

    // Outer aggregate
    kdi_aggregate outer;
    outer.kind         = kdi_aggregate_kind::class_;
    outer.name         = "Outer";
    outer.fq_name      = "nest::Outer";
    outer.mangled_name = "_KN4nest5OuterE";
    outer.llvm_def     = "%struct.nest.Outer = type { ptr }";

    // Inner aggregate (static nested, with enclosing reference)
    kdi_aggregate inner;
    inner.kind              = kdi_aggregate_kind::class_;
    inner.name              = "Inner";
    inner.fq_name           = "nest::Outer::Inner";
    inner.mangled_name      = "_KN4nest5Outer5InnerE";
    inner.is_static_nested  = true;
    inner.enclosing_fq_name = "nest::Outer";
    inner.llvm_def          = "%struct.nest.Outer.Inner = type { i32 }";

    outer.nested.push_back(inner);
    f.unit.root_ns.aggregates.push_back(outer);

    std::ostringstream oss(std::ios::binary);
    REQUIRE_NOTHROW(kdi_write_cbor(f, oss));

    std::istringstream iss(oss.str(), std::ios::binary);
    kdi_file restored;
    REQUIRE_NOTHROW(restored = kdi_read_cbor(iss));

    REQUIRE(restored.unit.root_ns.aggregates.size() == 1u);
    auto& o2 = restored.unit.root_ns.aggregates[0];
    REQUIRE(o2.name == "Outer");
    REQUIRE(o2.is_static_nested == false);
    REQUIRE(o2.enclosing_fq_name.empty());

    REQUIRE(o2.nested.size() == 1u);
    auto& i2 = o2.nested[0];
    REQUIRE(i2.name == "Inner");
    REQUIRE(i2.fq_name == "nest::Outer::Inner");
    REQUIRE(i2.is_static_nested == true);
    REQUIRE(i2.enclosing_fq_name == "nest::Outer");
}

TEST_CASE("CBOR: vtable round-trips", "[cbor]") {
    kdi_file f;
    f.header.module_name = "virt::mod";
    f.unit.name          = "virt::mod";

    kdi_aggregate agg;
    agg.kind    = kdi_aggregate_kind::class_;
    agg.name    = "Animal";
    agg.fq_name = "virt::Animal";
    agg.llvm_def = "%struct.virt.Animal = type { i8** }";

    kdi_vtable vt;
    vt.vtable_symbol = "_KTVNvirtAnimalE";
    vt.rtti_symbol   = "_KTRINvirtAnimalE";
    vt.llvm_def      = "@_KTVNvirtAnimalE = constant [3 x i8*] zeroinitializer";

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
        fn.llvm_def     = "declare void @_KFN" + name + "()";
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
    add_fn("f_view",   kdi_type{kdi_view_type{std::make_shared<kdi_type>(kdi_type::make_int(32))}});
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
    REQUIRE(std::holds_alternative<kdi_view_type>(fns[10].return_type.value));
    REQUIRE(std::holds_alternative<kdi_const_type>(fns[11].return_type.value));
    REQUIRE(std::holds_alternative<kdi_array_type>(fns[12].return_type.value));
    auto& sarr = std::get<kdi_sized_array_type>(fns[13].return_type.value);
    REQUIRE(sarr.size == 10u);
    REQUIRE(std::get<kdi_aggregate_ref>(fns[14].return_type.value).fq_name == "types::Foo");
}

TEST_CASE("CBOR: callable type round-trips", "[cbor][type][callable]") {
    kdi_callable_type c;
    c.addresser = kdi_callable_addresser::ptr;
    c.ret = std::make_shared<kdi_type>(kdi_type::make_int(32));
    c.params.push_back(std::make_shared<kdi_type>(kdi_type::make_int(32)));
    c.params.push_back(std::make_shared<kdi_type>(kdi_type{kdi_bool_type{}}));

    auto t = rt_type(kdi_type{std::move(c)});
    REQUIRE(std::holds_alternative<kdi_callable_type>(t.value));
    auto& r = std::get<kdi_callable_type>(t.value);
    REQUIRE(r.addresser == kdi_callable_addresser::ptr);
    REQUIRE(std::holds_alternative<kdi_int_type>(r.ret->value));
    REQUIRE(r.params.size() == 2u);
    REQUIRE(std::holds_alternative<kdi_bool_type>(r.params[1]->value));
    REQUIRE(r.throws.empty());
    REQUIRE(r.member_of.empty());
}

TEST_CASE("CBOR: callable type round-trips every addresser", "[cbor][type][callable]") {
    const kdi_callable_addresser all[] = {
        kdi_callable_addresser::none, kdi_callable_addresser::ptr,
        kdi_callable_addresser::view, kdi_callable_addresser::link,
        kdi_callable_addresser::ref,  kdi_callable_addresser::owner};
    for (auto a : all) {
        auto t = rt_type(kdi_type::make_callable(a, kdi_type{kdi_void_type{}}));
        REQUIRE(std::get<kdi_callable_type>(t.value).addresser == a);
    }
}

TEST_CASE("CBOR: callable type round-trips throws and member owner", "[cbor][type][callable]") {
    kdi_callable_type c;
    c.addresser = kdi_callable_addresser::ref;
    c.ret = std::make_shared<kdi_type>(kdi_type{kdi_void_type{}});
    c.throws.push_back(std::make_shared<kdi_type>(kdi_type::make_aggregate("::k::IOException")));
    c.throws.push_back(std::make_shared<kdi_type>(kdi_type::make_aggregate("::k::FatalError")));
    c.member_of = "::my::Counter";

    auto t = rt_type(kdi_type{std::move(c)});
    auto& r = std::get<kdi_callable_type>(t.value);
    REQUIRE(r.throws.size() == 2u);
    REQUIRE(std::get<kdi_aggregate_ref>(r.throws[0]->value).fq_name == "::k::IOException");
    REQUIRE(std::get<kdi_aggregate_ref>(r.throws[1]->value).fq_name == "::k::FatalError");
    REQUIRE(r.member_of == "::my::Counter");
}

TEST_CASE("CBOR: nested callable type round-trips", "[cbor][type][callable]") {
    auto inner = std::make_shared<kdi_type>(
        kdi_type::make_callable(kdi_callable_addresser::ptr, kdi_type::make_int(32)));
    kdi_callable_type outer;
    outer.addresser = kdi_callable_addresser::none;
    outer.ret = inner;
    outer.params.push_back(inner);

    auto t = rt_type(kdi_type{std::move(outer)});
    auto& r = std::get<kdi_callable_type>(t.value);
    REQUIRE(std::holds_alternative<kdi_callable_type>(r.ret->value));
    REQUIRE(std::holds_alternative<kdi_callable_type>(r.params[0]->value));
    REQUIRE(std::get<kdi_callable_type>(r.params[0]->value).addresser
            == kdi_callable_addresser::ptr);
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

TEST_CASE("CBOR: template_origin round-trips on function", "[cbor][template]") {
    kdi_file f;
    f.header.module_name = "tpl";
    f.header.lib_base    = "tpl";
    f.unit.name          = "tpl";

    kdi_function fn;
    fn.name         = "identity__int__";
    fn.fq_name      = "tpl::identity__int__";
    fn.return_type  = kdi_type::make_int(32);
    fn.mangled_name = "_KFN3tpl8identityIiEi";
    fn.llvm_def     = "declare i32 @_KFN3tpl8identityIiEi(i32)";
    fn.params.push_back({"x", kdi_type::make_int(32)});

    kdi_template_origin origin;
    origin.base_name    = "identity";
    origin.base_fq_name = "tpl::identity";
    kdi_template_arg arg1;
    arg1.type_arg = kdi_type::make_int(32);
    origin.args.push_back(arg1);
    fn.template_origin = origin;

    f.unit.root_ns.functions.push_back(fn);

    std::ostringstream oss(std::ios::binary);
    REQUIRE_NOTHROW(kdi_write_cbor(f, oss));
    std::istringstream iss(oss.str(), std::ios::binary);
    kdi_file restored;
    REQUIRE_NOTHROW(restored = kdi_read_cbor(iss));

    REQUIRE(restored.unit.root_ns.functions.size() == 1);
    auto& rfn = restored.unit.root_ns.functions[0];
    REQUIRE(rfn.template_origin.has_value());
    REQUIRE(rfn.template_origin->base_name == "identity");
    REQUIRE(rfn.template_origin->base_fq_name == "tpl::identity");
    REQUIRE(rfn.template_origin->args.size() == 1);
    REQUIRE(rfn.template_origin->args[0].type_arg.has_value());
    auto& targ = std::get<kdi_int_type>(rfn.template_origin->args[0].type_arg->value);
    REQUIRE(targ.bits == 32);
    REQUIRE(targ.is_signed);
}

TEST_CASE("CBOR: template_origin round-trips on aggregate", "[cbor][template]") {
    kdi_file f;
    f.header.module_name = "tpl";
    f.header.lib_base    = "tpl";
    f.unit.name          = "tpl";

    kdi_aggregate agg;
    agg.kind         = kdi_aggregate_kind::struct_;
    agg.name         = "Box__int__";
    agg.fq_name      = "tpl::Box__int__";
    agg.mangled_name = "_KS3tpl3BoxIiE";
    agg.llvm_def     = "%struct.tpl.Box__int__ = type { i32 }";

    kdi_template_origin origin;
    origin.base_name    = "Box";
    origin.base_fq_name = "tpl::Box";
    kdi_template_arg arg1;
    arg1.type_arg = kdi_type::make_int(32);
    origin.args.push_back(arg1);
    // Also test value arg with value_type
    kdi_template_arg arg2;
    arg2.value_arg  = "10";
    arg2.value_type = kdi_type::make_int(32, true);
    origin.args.push_back(arg2);
    agg.template_origin = origin;

    f.unit.root_ns.aggregates.push_back(agg);

    std::ostringstream oss(std::ios::binary);
    REQUIRE_NOTHROW(kdi_write_cbor(f, oss));
    std::istringstream iss(oss.str(), std::ios::binary);
    kdi_file restored;
    REQUIRE_NOTHROW(restored = kdi_read_cbor(iss));

    REQUIRE(restored.unit.root_ns.aggregates.size() == 1);
    auto& ragg = restored.unit.root_ns.aggregates[0];
    REQUIRE(ragg.template_origin.has_value());
    REQUIRE(ragg.template_origin->base_name == "Box");
    REQUIRE(ragg.template_origin->base_fq_name == "tpl::Box");
    REQUIRE(ragg.template_origin->args.size() == 2);
    // First arg: type
    REQUIRE(ragg.template_origin->args[0].type_arg.has_value());
    // Second arg: value with type
    REQUIRE(ragg.template_origin->args[1].value_arg.has_value());
    REQUIRE(*ragg.template_origin->args[1].value_arg == "10");
    REQUIRE(ragg.template_origin->args[1].value_type.has_value());
    auto& vt = std::get<kdi_int_type>(ragg.template_origin->args[1].value_type->value);
    REQUIRE(vt.bits == 32);
    REQUIRE(vt.is_signed);
}

TEST_CASE("CBOR: template_def round-trips in namespace", "[cbor][template]") {
    kdi_file f;
    f.header.module_name = "tpl";
    f.header.lib_base    = "tpl";
    f.unit.name          = "tpl";

    kdi_template_def td;
    td.name        = "Box";
    td.fq_name     = "tpl::Box";
    td.entity_kind = "struct";
    td.visibility  = "public";
    kdi_template_param tp;
    tp.kind = "typename";
    tp.name = "T";
    td.params.push_back(tp);
    td.source = "template<typename T> struct Box { val: T; }";
    f.unit.root_ns.template_defs.push_back(td);

    std::ostringstream oss(std::ios::binary);
    REQUIRE_NOTHROW(kdi_write_cbor(f, oss));
    std::istringstream iss(oss.str(), std::ios::binary);
    kdi_file restored;
    REQUIRE_NOTHROW(restored = kdi_read_cbor(iss));

    REQUIRE(restored.unit.root_ns.template_defs.size() == 1);
    auto& rtd = restored.unit.root_ns.template_defs[0];
    REQUIRE(rtd.name == "Box");
    REQUIRE(rtd.fq_name == "tpl::Box");
    REQUIRE(rtd.entity_kind == "struct");
    REQUIRE(rtd.params.size() == 1);
    REQUIRE(rtd.params[0].kind == "typename");
    REQUIRE(rtd.params[0].name == "T");
    REQUIRE(rtd.source == "template<typename T> struct Box { val: T; }");
}

TEST_CASE("CBOR: generic template_def round-trips with signature and no source", "[cbor][template][generic]") {
    kdi_file f;
    f.header.module_name = "tpl";
    f.header.lib_base    = "tpl";
    f.unit.name          = "tpl";

    kdi_template_def td;
    td.name = "Box";
    td.fq_name = "tpl::Box";
    td.entity_kind = "struct";
    td.visibility = "public";
    td.is_generic = true;
    td.params.push_back(kdi_template_param{"typename", "T", std::nullopt, std::nullopt, std::nullopt, std::nullopt});

    auto sig = std::make_shared<kdi_aggregate>();
    sig->kind = kdi_aggregate_kind::struct_;
    sig->name = "Box";
    sig->fq_name = "tpl::Box";

    kdi_layout_member member;
    member.llvm_field_index = 0;
    member.name = "value";
    member.fq_name = "tpl::Box::value";
    member.visibility = kdi_visibility::public_;
    kdi_ref_type ref_t;
    ref_t.inner = std::make_shared<kdi_type>(kdi_type::make_template_param("T"));
    member.type = kdi_type{std::move(ref_t)};
    sig->layout.push_back(member);

    td.aggregate_signature = sig;
    f.unit.root_ns.template_defs.push_back(td);

    std::ostringstream oss(std::ios::binary);
    REQUIRE_NOTHROW(kdi_write_cbor(f, oss));
    std::istringstream iss(oss.str(), std::ios::binary);
    kdi_file restored;
    REQUIRE_NOTHROW(restored = kdi_read_cbor(iss));

    REQUIRE(restored.unit.root_ns.template_defs.size() == 1);
    auto& rtd = restored.unit.root_ns.template_defs[0];
    REQUIRE(rtd.is_generic);
    REQUIRE(rtd.source.empty());
    REQUIRE(rtd.aggregate_signature != nullptr);
    REQUIRE(rtd.aggregate_signature->layout.size() == 1);
    auto* rmember = std::get_if<kdi_layout_member>(&rtd.aggregate_signature->layout[0]);
    REQUIRE(rmember != nullptr);
    REQUIRE(std::holds_alternative<kdi_ref_type>(rmember->type.value));
    auto& inner = *std::get<kdi_ref_type>(rmember->type.value).inner;
    REQUIRE(std::holds_alternative<kdi_template_param_ref>(inner.value));
    REQUIRE(std::get<kdi_template_param_ref>(inner.value).name == "T");
}

TEST_CASE("CBOR: object-backed enum metadata round-trips", "[cbor][enum][typed]") {
    kdi_file f;
    f.header.module_name = "typed::enum";
    f.header.lib_base    = "typed.enum";
    f.unit.name          = "typed::enum";

    kdi_enum en;
    en.name                = "Dir";
    en.fq_name             = "typed::enum::Dir";
    en.underlying_type     = kdi_type::make_int(8, false);
    en.object_type         = kdi_type::make_aggregate("typed::enum::Vec2");
    en.object_table_symbol = "__klang_enum_table_Dir__";

    kdi_enum_entry up;
    up.name = "UP";
    up.value = 0;
    up.is_default = true;
    up.object_init_members.emplace_back("x", 0);
    up.object_init_members.emplace_back("y", 1);
    en.entries.push_back(up);
    f.unit.root_ns.enums.push_back(en);

    std::ostringstream oss(std::ios::binary);
    REQUIRE_NOTHROW(kdi_write_cbor(f, oss));
    std::istringstream iss(oss.str(), std::ios::binary);
    kdi_file restored;
    REQUIRE_NOTHROW(restored = kdi_read_cbor(iss));

    REQUIRE(restored.unit.root_ns.enums.size() == 1);
    auto& ren = restored.unit.root_ns.enums[0];
    REQUIRE(ren.object_type.has_value());
    REQUIRE(std::holds_alternative<kdi_aggregate_ref>(ren.object_type->value));
    REQUIRE(std::get<kdi_aggregate_ref>(ren.object_type->value).fq_name == "typed::enum::Vec2");
    REQUIRE(ren.object_table_symbol.has_value());
    REQUIRE(*ren.object_table_symbol == "__klang_enum_table_Dir__");
    REQUIRE(ren.entries.size() == 1);
    REQUIRE(ren.entries[0].object_init_members.size() == 2);
}

