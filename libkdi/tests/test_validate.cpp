#include <catch2/catch_all.hpp>
#include "kdi.hpp"

using namespace kdi;

static kdi_file make_valid_file() {
    kdi_file f;
    f.header.module_name = "valid::mod";
    f.unit.name          = "valid::mod";

    kdi_function fn;
    fn.name         = "foo";
    fn.fq_name      = "valid::mod::foo";
    fn.return_type  = kdi_type::make_void();
    fn.mangled_name = "_KFN5valid3mod3fooEv";
    fn.llvm_def     = "declare void @_KFN5valid3mod3fooEv()";
    f.unit.root_ns.functions.push_back(fn);
    return f;
}

TEST_CASE("validate: minimal valid file passes", "[validate]") {
    auto f = make_valid_file();
    auto r = kdi_validate(f);
    REQUIRE(r.is_valid());
}

TEST_CASE("validate: wrong schema_major fails", "[validate]") {
    auto f = make_valid_file();
    f.header.schema_major = 99;
    auto r = kdi_validate(f);
    REQUIRE(!r.is_valid());
    REQUIRE(r.errors[0].path == "header.schema_major");
}

TEST_CASE("validate: wrong schema_minor fails", "[validate]") {
    auto f = make_valid_file();
    f.header.schema_minor = 99;
    auto r = kdi_validate(f);
    REQUIRE(!r.is_valid());
    REQUIRE(r.errors[0].path == "header.schema_minor");
}

TEST_CASE("validate: empty module_name fails", "[validate]") {
    auto f = make_valid_file();
    f.header.module_name = "";
    auto r = kdi_validate(f);
    REQUIRE(!r.is_valid());
    REQUIRE(r.errors[0].path == "header.module_name");
}

TEST_CASE("validate: function with empty mangled_name fails", "[validate]") {
    auto f = make_valid_file();
    f.unit.root_ns.functions[0].mangled_name = "";
    auto r = kdi_validate(f);
    REQUIRE(!r.is_valid());
}

TEST_CASE("validate: aggregate with unresolved type ref fails", "[validate]") {
    kdi_file f;
    f.header.module_name = "bad::mod";
    f.unit.name          = "bad::mod";
    // Function returns an aggregate type that is NOT in the type table
    kdi_function fn;
    fn.name         = "make";
    fn.fq_name      = "bad::mod::make";
    fn.return_type  = kdi_type::make_aggregate("bad::Missing");
    fn.mangled_name = "_KFN3bad3mod4makeEv";
    fn.llvm_def     = "declare void @_KFN3bad3mod4makeEv()";
    f.unit.root_ns.functions.push_back(fn);
    auto r = kdi_validate(f);
    REQUIRE(!r.is_valid());
}

TEST_CASE("validate: aggregate with resolved type ref passes", "[validate]") {
    kdi_file f;
    f.header.module_name = "ok::mod";
    f.unit.name          = "ok::mod";
    f.types.aggregates.push_back({"ok::Foo", "_KS2ok3Foo"});

    kdi_function fn;
    fn.name         = "make";
    fn.fq_name      = "ok::mod::make";
    fn.return_type  = kdi_type::make_aggregate("ok::Foo");
    fn.mangled_name = "_KFN2ok3mod4makeEv";
    fn.llvm_def     = "declare void @_KFN2ok3mod4makeEv()";
    f.unit.root_ns.functions.push_back(fn);
    auto r = kdi_validate(f);
    REQUIRE(r.is_valid());
}

TEST_CASE("validate: layout with decreasing field index fails", "[validate]") {
    kdi_file f;
    f.header.module_name = "layout::mod";
    f.unit.name          = "layout::mod";
    f.types.aggregates.push_back({"layout::S", "_KSS"});

    kdi_aggregate agg;
    agg.name    = "S";
    agg.fq_name = "layout::S";
    agg.llvm_def = "%struct.layout.S = type { i32, i32 }";

    kdi_layout_member m1, m2;
    m1.llvm_field_index = 5; m1.name = "a"; m1.fq_name = "layout::S::a";
    m1.type = kdi_type::make_int(32); m1.mangled_name = "_x";
    m2.llvm_field_index = 2; m2.name = "b"; m2.fq_name = "layout::S::b";  // ← decreasing
    m2.type = kdi_type::make_int(32); m2.mangled_name = "_y";
    agg.layout.push_back(m1);
    agg.layout.push_back(m2);

    f.unit.root_ns.aggregates.push_back(agg);
    auto r = kdi_validate(f);
    REQUIRE(!r.is_valid());
}

TEST_CASE("validate: vtable with non-contiguous slots fails", "[validate]") {
    kdi_file f;
    f.header.module_name = "vt::mod";
    f.unit.name          = "vt::mod";

    kdi_aggregate agg;
    agg.kind = kdi_aggregate_kind::class_;
    agg.name = "A"; agg.fq_name = "vt::A";
    agg.llvm_def = "%struct.vt.A = type { i8** }";

    kdi_vtable vt;
    vt.vtable_symbol = "_KTV";
    vt.rtti_symbol   = "_KTRI";
    vt.llvm_def      = "@_KTV = constant [3 x i8*] zeroinitializer";
    kdi_vtable_slot s;
    s.slot_index = 3;  // not 0 — invalid
    s.introducing_func = "vt::A::f";
    vt.slots.push_back(s);
    agg.vtable = vt;

    f.unit.root_ns.aggregates.push_back(agg);
    auto r = kdi_validate(f);
    REQUIRE(!r.is_valid());
}

TEST_CASE("validate: duplicate fq_name in namespace fails", "[validate]") {
    kdi_file f;
    f.header.module_name = "dup::mod";
    f.unit.name          = "dup::mod";

    kdi_aggregate a1, a2;
    a1.name = "Foo"; a1.fq_name = "dup::Foo";
    a1.llvm_def = "%struct.dup.Foo = type { i32 }";
    a2.name = "Foo"; a2.fq_name = "dup::Foo";  // duplicate
    a2.llvm_def = "%struct.dup.Foo = type { i32 }";

    kdi_constructor ctor;
    ctor.mangled_name = "_KFMC1N3dupFooE";
    ctor.llvm_def     = "declare void @_KFMC1N3dupFooE(%struct.dup.Foo* %this)";
    a1.constructors.push_back(ctor);
    a2.constructors.push_back(ctor);

    f.unit.root_ns.aggregates.push_back(a1);
    f.unit.root_ns.aggregates.push_back(a2);
    auto r = kdi_validate(f);
    REQUIRE(!r.is_valid());
}

TEST_CASE("validate: integer-backed enum remains valid without typed metadata", "[validate][enum]") {
    kdi_file f;
    f.header.module_name = "enum::ok";
    f.unit.name          = "enum::ok";

    kdi_enum en;
    en.name            = "Color";
    en.fq_name         = "enum::ok::Color";
    en.underlying_type = kdi_type::make_int(32, true);
    en.entries.push_back(kdi_enum_entry{"RED", 0, true, {}});
    en.entries.push_back(kdi_enum_entry{"GREEN", 1, false, {}});
    f.unit.root_ns.enums.push_back(en);

    auto r = kdi_validate(f);
    REQUIRE(r.is_valid());
}

TEST_CASE("validate: object-backed enum requires table symbol", "[validate][enum]") {
    kdi_file f;
    f.header.module_name = "enum::bad";
    f.unit.name          = "enum::bad";
    f.types.aggregates.push_back({"enum::bad::Vec2", "_KS_enum_bad_Vec2"});

    kdi_enum en;
    en.name            = "Dir";
    en.fq_name         = "enum::bad::Dir";
    en.underlying_type = kdi_type::make_int(8, false);
    en.object_type     = kdi_type::make_aggregate("enum::bad::Vec2");
    en.entries.push_back(kdi_enum_entry{"UP", 0, true, {{"x", 0}, {"y", 1}}});
    f.unit.root_ns.enums.push_back(en);

    auto r = kdi_validate(f);
    REQUIRE(!r.is_valid());
}

TEST_CASE("validate: integer-backed enum rejects object-backed-only fields", "[validate][enum]") {
    kdi_file f;
    f.header.module_name = "enum::bad2";
    f.unit.name          = "enum::bad2";

    kdi_enum en;
    en.name                = "Color";
    en.fq_name             = "enum::bad2::Color";
    en.underlying_type     = kdi_type::make_int(32, true);
    en.object_table_symbol = "__klang_enum_table_bad__";
    kdi_enum_entry e;
    e.name = "RED";
    e.value = 0;
    e.is_default = true;
    e.object_init_members.emplace_back("x", 0);
    en.entries.push_back(std::move(e));
    f.unit.root_ns.enums.push_back(std::move(en));

    auto r = kdi_validate(f);
    REQUIRE(!r.is_valid());
}

