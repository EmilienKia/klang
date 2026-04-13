#include <catch2/catch_all.hpp>
#include "kdi_types.hpp"
#include "kdi_aggregates.hpp"
#include "kdi_file.hpp"

using namespace kdi;

// ─── kdi_type construction — primitives ───────────────────────────────────────

TEST_CASE("kdi_type: make_void holds kdi_void_type", "[dto][types]") {
    auto t = kdi_type::make_void();
    REQUIRE(std::holds_alternative<kdi_void_type>(t.value));
}

TEST_CASE("kdi_type: make_bool holds kdi_bool_type", "[dto][types]") {
    auto t = kdi_type::make_bool();
    REQUIRE(std::holds_alternative<kdi_bool_type>(t.value));
}

TEST_CASE("kdi_type: make_char holds kdi_char_type", "[dto][types]") {
    auto t = kdi_type::make_char();
    REQUIRE(std::holds_alternative<kdi_char_type>(t.value));
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

TEST_CASE("kdi_type: make_int defaults to signed", "[dto][types]") {
    auto t = kdi_type::make_int(16);
    auto& i = std::get<kdi_int_type>(t.value);
    REQUIRE(i.bits == 16);
    REQUIRE(i.is_signed);
}

TEST_CASE("kdi_type: make_float 32", "[dto][types]") {
    auto t = kdi_type::make_float(32);
    REQUIRE(std::holds_alternative<kdi_float_type>(t.value));
    REQUIRE(std::get<kdi_float_type>(t.value).bits == 32);
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

TEST_CASE("kdi_type: make_enum stores fq_name", "[dto][types]") {
    auto t = kdi_type::make_enum("color::Color");
    REQUIRE(std::holds_alternative<kdi_enum_ref>(t.value));
    REQUIRE(std::get<kdi_enum_ref>(t.value).fq_name == "color::Color");
}

// ─── kdi_type construction — indirections ─────────────────────────────────────

TEST_CASE("kdi_type: ref_type wraps inner type", "[dto][types]") {
    auto inner = std::make_shared<kdi_type>(kdi_type::make_int(32));
    kdi_type t{kdi_ref_type{inner}};
    REQUIRE(std::holds_alternative<kdi_ref_type>(t.value));
    auto& ref = std::get<kdi_ref_type>(t.value);
    REQUIRE(ref.inner != nullptr);
    REQUIRE(std::holds_alternative<kdi_int_type>(ref.inner->value));
    REQUIRE(std::get<kdi_int_type>(ref.inner->value).bits == 32);
}

TEST_CASE("kdi_type: ptr_type wraps inner type", "[dto][types]") {
    auto inner = std::make_shared<kdi_type>(kdi_type::make_bool());
    kdi_type t{kdi_ptr_type{inner}};
    REQUIRE(std::holds_alternative<kdi_ptr_type>(t.value));
    REQUIRE(std::holds_alternative<kdi_bool_type>(std::get<kdi_ptr_type>(t.value).inner->value));
}

TEST_CASE("kdi_type: link_type wraps inner type", "[dto][types]") {
    auto inner = std::make_shared<kdi_type>(kdi_type::make_float(64));
    kdi_type t{kdi_link_type{inner}};
    REQUIRE(std::holds_alternative<kdi_link_type>(t.value));
    REQUIRE(std::holds_alternative<kdi_float_type>(std::get<kdi_link_type>(t.value).inner->value));
}

TEST_CASE("kdi_type: view_type wraps inner type", "[dto][types]") {
    auto inner = std::make_shared<kdi_type>(kdi_type::make_char());
    kdi_type t{kdi_view_type{inner}};
    REQUIRE(std::holds_alternative<kdi_view_type>(t.value));
    REQUIRE(std::holds_alternative<kdi_char_type>(std::get<kdi_view_type>(t.value).inner->value));
}

TEST_CASE("kdi_type: drain_type wraps inner type", "[dto][types]") {
    auto inner = std::make_shared<kdi_type>(kdi_type::make_int(64, false));
    kdi_type t{kdi_drain_type{inner}};
    REQUIRE(std::holds_alternative<kdi_drain_type>(t.value));
    auto& drain_inner = std::get<kdi_drain_type>(t.value).inner;
    REQUIRE(std::get<kdi_int_type>(drain_inner->value).bits == 64);
    REQUIRE(!std::get<kdi_int_type>(drain_inner->value).is_signed);
}

// ─── kdi_type construction — qualifiers ───────────────────────────────────────

TEST_CASE("kdi_type: const_type wraps inner type", "[dto][types]") {
    auto inner = std::make_shared<kdi_type>(kdi_type::make_int(32));
    kdi_type t{kdi_const_type{inner}};
    REQUIRE(std::holds_alternative<kdi_const_type>(t.value));
    REQUIRE(std::holds_alternative<kdi_int_type>(std::get<kdi_const_type>(t.value).inner->value));
}

// ─── kdi_type construction — arrays ───────────────────────────────────────────

TEST_CASE("kdi_type: array_type wraps element type", "[dto][types]") {
    auto elem = std::make_shared<kdi_type>(kdi_type::make_int(8));
    kdi_type t{kdi_array_type{elem}};
    REQUIRE(std::holds_alternative<kdi_array_type>(t.value));
    auto& arr = std::get<kdi_array_type>(t.value);
    REQUIRE(std::holds_alternative<kdi_int_type>(arr.elem->value));
    REQUIRE(std::get<kdi_int_type>(arr.elem->value).bits == 8);
}

TEST_CASE("kdi_type: sized_array_type stores element and size", "[dto][types]") {
    auto elem = std::make_shared<kdi_type>(kdi_type::make_float(32));
    kdi_type t{kdi_sized_array_type{elem, 256}};
    REQUIRE(std::holds_alternative<kdi_sized_array_type>(t.value));
    auto& sarr = std::get<kdi_sized_array_type>(t.value);
    REQUIRE(sarr.size == 256u);
    REQUIRE(std::holds_alternative<kdi_float_type>(sarr.elem->value));
}

// ─── kdi_type construction — function reference ───────────────────────────────

TEST_CASE("kdi_type: fn_ref_type stores return and params", "[dto][types]") {
    kdi_fn_ref_type fn;
    fn.ret = std::make_shared<kdi_type>(kdi_type::make_int(32));
    fn.params.push_back(std::make_shared<kdi_type>(kdi_type::make_int(32)));
    fn.params.push_back(std::make_shared<kdi_type>(kdi_type::make_float(64)));

    kdi_type t{std::move(fn)};
    REQUIRE(std::holds_alternative<kdi_fn_ref_type>(t.value));
    auto& fnr = std::get<kdi_fn_ref_type>(t.value);
    REQUIRE(std::holds_alternative<kdi_int_type>(fnr.ret->value));
    REQUIRE(fnr.params.size() == 2u);
    REQUIRE(std::holds_alternative<kdi_int_type>(fnr.params[0]->value));
    REQUIRE(std::holds_alternative<kdi_float_type>(fnr.params[1]->value));
}

// ─── kdi_type construction — nested indirections ──────────────────────────────

TEST_CASE("kdi_type: nested indirection (const ref to int)", "[dto][types]") {
    auto int_t = std::make_shared<kdi_type>(kdi_type::make_int(32));
    auto ref_t = std::make_shared<kdi_type>(kdi_type{kdi_ref_type{int_t}});
    kdi_type t{kdi_const_type{ref_t}};

    REQUIRE(std::holds_alternative<kdi_const_type>(t.value));
    auto& inner_ref = std::get<kdi_const_type>(t.value).inner;
    REQUIRE(std::holds_alternative<kdi_ref_type>(inner_ref->value));
    auto& innermost = std::get<kdi_ref_type>(inner_ref->value).inner;
    REQUIRE(std::holds_alternative<kdi_int_type>(innermost->value));
}

// ─── kdi_int_type / kdi_float_type defaults ───────────────────────────────────

TEST_CASE("kdi_int_type: default is signed 32-bit", "[dto][types]") {
    kdi_int_type it;
    REQUIRE(it.bits == 32);
    REQUIRE(it.is_signed);
}

TEST_CASE("kdi_float_type: default is 64-bit", "[dto][types]") {
    kdi_float_type ft;
    REQUIRE(ft.bits == 64);
}

TEST_CASE("kdi_sized_array_type: default size is 0", "[dto][types]") {
    kdi_sized_array_type sa;
    REQUIRE(sa.size == 0u);
    REQUIRE(sa.elem == nullptr);
}

// ─── kdi_header ──────────────────────────────────────────────────────────────

TEST_CASE("kdi_header: schema version defaults", "[dto][header]") {
    kdi_header h;
    REQUIRE(h.schema_major == KDI_SCHEMA_MAJOR);
    REQUIRE(h.schema_minor == KDI_SCHEMA_MINOR);
}

TEST_CASE("kdi_header: schema_major is 0 and schema_minor is 1", "[dto][header]") {
    REQUIRE(KDI_SCHEMA_MAJOR == 0u);
    REQUIRE(KDI_SCHEMA_MINOR == 1u);
}

TEST_CASE("kdi_header: string fields default to empty", "[dto][header]") {
    kdi_header h;
    REQUIRE(h.module_name.empty());
    REQUIRE(h.lib_base.empty());
    REQUIRE(h.lib_path.empty());
    REQUIRE(h.target_triple.empty());
    REQUIRE(h.compiler_ver.empty());
    REQUIRE(h.dependencies.empty());
}

TEST_CASE("kdi_header: dependencies can be populated", "[dto][header]") {
    kdi_header h;
    h.dependencies = {"math::core", "io::stream"};
    REQUIRE(h.dependencies.size() == 2u);
    REQUIRE(h.dependencies[0] == "math::core");
    REQUIRE(h.dependencies[1] == "io::stream");
}

// ─── kdi_visibility ──────────────────────────────────────────────────────────

TEST_CASE("kdi_visibility: enum values are distinct", "[dto][visibility]") {
    REQUIRE(kdi_visibility::public_ != kdi_visibility::protected_);
}

// ─── kdi_param ───────────────────────────────────────────────────────────────

TEST_CASE("kdi_param: stores name and type", "[dto][param]") {
    kdi_param p;
    p.name = "count";
    p.type = kdi_type::make_int(32);
    REQUIRE(p.name == "count");
    REQUIRE(std::holds_alternative<kdi_int_type>(p.type.value));
}

// ─── kdi_variable ────────────────────────────────────────────────────────────

TEST_CASE("kdi_variable: defaults", "[dto][variable]") {
    kdi_variable v;
    REQUIRE(v.name.empty());
    REQUIRE(v.fq_name.empty());
    REQUIRE(v.visibility == kdi_visibility::public_);
    REQUIRE(v.is_const == false);
    REQUIRE(v.mangled_name.empty());
}

TEST_CASE("kdi_variable: stores all fields", "[dto][variable]") {
    kdi_variable v;
    v.name         = "count";
    v.fq_name      = "mod::count";
    v.visibility   = kdi_visibility::protected_;
    v.type         = kdi_type::make_int(64);
    v.is_const     = true;
    v.mangled_name = "_KV3mod5countE";
    REQUIRE(v.name == "count");
    REQUIRE(v.visibility == kdi_visibility::protected_);
    REQUIRE(v.is_const);
    REQUIRE(std::holds_alternative<kdi_int_type>(v.type.value));
}

// ─── kdi_function ────────────────────────────────────────────────────────────

TEST_CASE("kdi_function: defaults", "[dto][function]") {
    kdi_function fn;
    REQUIRE(fn.name.empty());
    REQUIRE(fn.fq_name.empty());
    REQUIRE(fn.visibility == kdi_visibility::public_);
    REQUIRE(fn.is_static == false);
    REQUIRE(fn.is_operator == false);
    REQUIRE(fn.params.empty());
    REQUIRE(fn.mangled_name.empty());
    REQUIRE(fn.llvm_def.empty());
    REQUIRE(!fn.template_origin.has_value());
}

TEST_CASE("kdi_function: stores params and return type", "[dto][function]") {
    kdi_function fn;
    fn.name         = "add";
    fn.fq_name      = "math::add";
    fn.return_type  = kdi_type::make_int(32);
    fn.params.push_back({"a", kdi_type::make_int(32)});
    fn.params.push_back({"b", kdi_type::make_int(32)});
    fn.mangled_name = "_KFN4math3addEii";
    fn.llvm_def     = "declare i32 @_KFN4math3addEii(i32, i32)";

    REQUIRE(fn.params.size() == 2u);
    REQUIRE(fn.params[0].name == "a");
    REQUIRE(fn.params[1].name == "b");
    REQUIRE(std::holds_alternative<kdi_int_type>(fn.return_type.value));
}

// ─── kdi_method ──────────────────────────────────────────────────────────────

TEST_CASE("kdi_method: defaults", "[dto][method]") {
    kdi_method m;
    REQUIRE(m.name.empty());
    REQUIRE(m.visibility == kdi_visibility::public_);
    REQUIRE(m.is_static == false);
    REQUIRE(m.is_const_member == false);
    REQUIRE(m.is_virtual == false);
    REQUIRE(m.is_abstract == false);
    REQUIRE(m.is_final == false);
    REQUIRE(m.is_operator == false);
    REQUIRE(m.vtable_slot == -1);
    REQUIRE(m.params.empty());
    REQUIRE(!m.template_origin.has_value());
}

TEST_CASE("kdi_method: virtual method with vtable slot", "[dto][method]") {
    kdi_method m;
    m.name            = "speak";
    m.fq_name         = "Animal::speak";
    m.is_virtual      = true;
    m.is_abstract     = true;
    m.vtable_slot     = 0;
    m.return_type     = kdi_type::make_void();
    m.mangled_name    = "_KFMN6Animal5speakEv";
    REQUIRE(m.is_virtual);
    REQUIRE(m.is_abstract);
    REQUIRE(m.vtable_slot == 0);
}

// ─── kdi_constructor ─────────────────────────────────────────────────────────

TEST_CASE("kdi_constructor: defaults", "[dto][constructor]") {
    kdi_constructor ctor;
    REQUIRE(ctor.visibility == kdi_visibility::public_);
    REQUIRE(ctor.is_copy_constructor == false);
    REQUIRE(ctor.is_defaulted == false);
    REQUIRE(ctor.is_deleted == false);
    REQUIRE(ctor.params.empty());
    REQUIRE(ctor.mangled_name.empty());
    REQUIRE(ctor.mangled_name_c2.empty());
    REQUIRE(ctor.llvm_def.empty());
}

TEST_CASE("kdi_constructor: copy constructor with C1 and C2 symbols", "[dto][constructor]") {
    kdi_constructor ctor;
    ctor.is_copy_constructor = true;
    ctor.params.push_back({"other", kdi_type{kdi_ref_type{
        std::make_shared<kdi_type>(kdi_type::make_aggregate("my::Foo"))}}});
    ctor.mangled_name    = "_KFMC1N2my3FooERS_";
    ctor.mangled_name_c2 = "_KFMC2N2my3FooERS_";
    REQUIRE(ctor.is_copy_constructor);
    REQUIRE(ctor.params.size() == 1u);
    REQUIRE(!ctor.mangled_name.empty());
    REQUIRE(!ctor.mangled_name_c2.empty());
}

// ─── kdi_destructor ──────────────────────────────────────────────────────────

TEST_CASE("kdi_destructor: defaults", "[dto][destructor]") {
    kdi_destructor dtor;
    REQUIRE(dtor.visibility == kdi_visibility::public_);
    REQUIRE(dtor.is_virtual == false);
    REQUIRE(dtor.is_compiler_generated == false);
    REQUIRE(dtor.mangled_name.empty());
    REQUIRE(dtor.mangled_name_d2.empty());
    REQUIRE(dtor.llvm_def.empty());
}

TEST_CASE("kdi_destructor: virtual destructor with D1 and D2 symbols", "[dto][destructor]") {
    kdi_destructor dtor;
    dtor.is_virtual       = true;
    dtor.mangled_name     = "_KFMD1N6AnimalE";
    dtor.mangled_name_d2  = "_KFMD2N6AnimalE";
    dtor.llvm_def         = "declare void @_KFMD1N6AnimalE(%struct.Animal* %this)";
    REQUIRE(dtor.is_virtual);
    REQUIRE(!dtor.mangled_name.empty());
    REQUIRE(!dtor.mangled_name_d2.empty());
}

// ─── kdi_vtable types ────────────────────────────────────────────────────────

TEST_CASE("kdi_vtable_slot: defaults", "[dto][vtable]") {
    kdi_vtable_slot s;
    REQUIRE(s.slot_index == 0u);
    REQUIRE(s.introducing_func.empty());
    REQUIRE(s.override_symbol.empty());
    REQUIRE(s.is_abstract == false);
}

TEST_CASE("kdi_thunk: defaults", "[dto][vtable]") {
    kdi_thunk th;
    REQUIRE(th.slot_index == 0u);
    REQUIRE(th.real_func_symbol.empty());
    REQUIRE(th.this_adjustment == 0);
    REQUIRE(th.needs_thunk == false);
}

TEST_CASE("kdi_secondary_vtable: stores base info and thunks", "[dto][vtable]") {
    kdi_secondary_vtable sv;
    sv.base_fq_name  = "Base::Iface";
    sv.base_offset   = 16;
    sv.vtable_symbol = "_KTVN6Base5IfaceE";

    kdi_thunk th;
    th.slot_index       = 0;
    th.real_func_symbol = "_KFMN7Derived3fooEv";
    th.this_adjustment  = -16;
    th.needs_thunk      = true;
    sv.thunks.push_back(th);

    REQUIRE(sv.base_fq_name == "Base::Iface");
    REQUIRE(sv.base_offset == 16u);
    REQUIRE(sv.thunks.size() == 1u);
    REQUIRE(sv.thunks[0].needs_thunk);
    REQUIRE(sv.thunks[0].this_adjustment == -16);
}

TEST_CASE("kdi_vtable: stores slots and secondary vtables", "[dto][vtable]") {
    kdi_vtable vt;
    vt.vtable_symbol = "_KTVN6AnimalE";
    vt.rtti_symbol   = "_KTRIN6AnimalE";
    vt.llvm_def      = "@_KTVN6AnimalE = constant [2 x ptr] zeroinitializer";

    kdi_vtable_slot s0;
    s0.slot_index       = 0;
    s0.introducing_func = "Animal::speak";
    s0.is_abstract      = true;
    vt.slots.push_back(s0);

    kdi_vtable_slot s1;
    s1.slot_index       = 1;
    s1.introducing_func = "Animal::move";
    s1.override_symbol  = "_KFMN6Animal4moveEv";
    vt.slots.push_back(s1);

    REQUIRE(vt.vtable_symbol == "_KTVN6AnimalE");
    REQUIRE(vt.slots.size() == 2u);
    REQUIRE(vt.slots[0].is_abstract);
    REQUIRE(!vt.slots[1].is_abstract);
    REQUIRE(vt.secondary.empty());
}

// ─── kdi_layout_field variants ───────────────────────────────────────────────

TEST_CASE("kdi_layout_member: stores all fields", "[dto][layout]") {
    kdi_layout_member lm;
    lm.name             = "x";
    lm.fq_name          = "Point::x";
    lm.visibility       = kdi_visibility::public_;
    lm.llvm_field_index = 0;
    lm.type             = kdi_type::make_float(32);
    lm.is_const         = false;
    lm.mangled_name     = "_KVM5Point1x";

    kdi_layout_field f = lm;
    REQUIRE(std::holds_alternative<kdi_layout_member>(f));
    auto& m = std::get<kdi_layout_member>(f);
    REQUIRE(m.name == "x");
    REQUIRE(m.fq_name == "Point::x");
    REQUIRE(m.llvm_field_index == 0u);
    REQUIRE(std::holds_alternative<kdi_float_type>(m.type.value));
}

TEST_CASE("kdi_aggregate: layout_vptr stores vtable_symbol", "[dto][layout]") {
    kdi_layout_vptr vp;
    vp.llvm_field_index = 0;
    vp.vtable_symbol    = "_KTV4math4Vec3E";

    kdi_layout_field f = vp;
    REQUIRE(std::holds_alternative<kdi_layout_vptr>(f));
    REQUIRE(std::get<kdi_layout_vptr>(f).vtable_symbol == "_KTV4math4Vec3E");
}

TEST_CASE("kdi_layout_vptr_secondary: stores base info", "[dto][layout]") {
    kdi_layout_vptr_secondary vps;
    vps.llvm_field_index = 2;
    vps.base_fq_name     = "Iface";
    vps.vtable_symbol    = "_KTVN5IfaceE";

    kdi_layout_field f = vps;
    REQUIRE(std::holds_alternative<kdi_layout_vptr_secondary>(f));
    auto& v = std::get<kdi_layout_vptr_secondary>(f);
    REQUIRE(v.base_fq_name == "Iface");
    REQUIRE(v.vtable_symbol == "_KTVN5IfaceE");
}

TEST_CASE("kdi_layout_base_subobject: stores base fq_name", "[dto][layout]") {
    kdi_layout_base_subobject bs;
    bs.llvm_field_index = 1;
    bs.base_fq_name     = "math::Shape";

    kdi_layout_field f = bs;
    REQUIRE(std::holds_alternative<kdi_layout_base_subobject>(f));
    REQUIRE(std::get<kdi_layout_base_subobject>(f).base_fq_name == "math::Shape");
}

TEST_CASE("kdi_layout_vbptr: stores vbase fq_name", "[dto][layout]") {
    kdi_layout_vbptr vb;
    vb.llvm_field_index = 3;
    vb.vbase_fq_name    = "Serializable";

    kdi_layout_field f = vb;
    REQUIRE(std::holds_alternative<kdi_layout_vbptr>(f));
    REQUIRE(std::get<kdi_layout_vbptr>(f).vbase_fq_name == "Serializable");
}

TEST_CASE("kdi_layout_vbase_subobject: stores vbase fq_name", "[dto][layout]") {
    kdi_layout_vbase_subobject vbs;
    vbs.llvm_field_index = 4;
    vbs.vbase_fq_name    = "Serializable";

    kdi_layout_field f = vbs;
    REQUIRE(std::holds_alternative<kdi_layout_vbase_subobject>(f));
    REQUIRE(std::get<kdi_layout_vbase_subobject>(f).vbase_fq_name == "Serializable");
}

TEST_CASE("kdi_layout_parent_ref: stores parent fq_name", "[dto][layout]") {
    kdi_layout_parent_ref pr;
    pr.llvm_field_index = 0;
    pr.parent_fq_name   = "Outer";

    kdi_layout_field f = pr;
    REQUIRE(std::holds_alternative<kdi_layout_parent_ref>(f));
    REQUIRE(std::get<kdi_layout_parent_ref>(f).parent_fq_name == "Outer");
}

TEST_CASE("kdi_aggregate: layout_opaque_block stores size_bits", "[dto][layout]") {
    kdi_layout_opaque_block ob;
    ob.llvm_field_index = 2;
    ob.field_count      = 3;
    ob.size_bits        = 192;

    kdi_layout_field f = ob;
    REQUIRE(std::holds_alternative<kdi_layout_opaque_block>(f));
    REQUIRE(std::get<kdi_layout_opaque_block>(f).size_bits == 192u);
}

// ─── kdi_base ────────────────────────────────────────────────────────────────

TEST_CASE("kdi_base: defaults", "[dto][base]") {
    kdi_base b;
    REQUIRE(b.fq_name.empty());
    REQUIRE(b.visibility == kdi_visibility::public_);
    REQUIRE(b.is_virtual == false);
    REQUIRE(b.base_field_index == -1);
    REQUIRE(b.byte_offset == 0u);
}

TEST_CASE("kdi_base: non-virtual base with offset", "[dto][base]") {
    kdi_base b;
    b.fq_name         = "math::Shape";
    b.visibility       = kdi_visibility::public_;
    b.is_virtual       = false;
    b.base_field_index = 1;
    b.byte_offset      = 8;
    REQUIRE(b.fq_name == "math::Shape");
    REQUIRE(b.base_field_index == 1);
    REQUIRE(b.byte_offset == 8u);
}

TEST_CASE("kdi_base: virtual base has base_field_index -1", "[dto][base]") {
    kdi_base b;
    b.fq_name    = "Serializable";
    b.is_virtual = true;
    REQUIRE(b.is_virtual);
    REQUIRE(b.base_field_index == -1);
}

// ─── kdi_aggregate ───────────────────────────────────────────────────────────

TEST_CASE("kdi_aggregate: default kind is struct", "[dto][aggregate]") {
    kdi_aggregate agg;
    REQUIRE(agg.kind == kdi_aggregate_kind::struct_);
}

TEST_CASE("kdi_aggregate: all aggregate kinds", "[dto][aggregate]") {
    REQUIRE(static_cast<uint8_t>(kdi_aggregate_kind::struct_)     != static_cast<uint8_t>(kdi_aggregate_kind::class_));
    REQUIRE(static_cast<uint8_t>(kdi_aggregate_kind::class_)      != static_cast<uint8_t>(kdi_aggregate_kind::interface_));
    REQUIRE(static_cast<uint8_t>(kdi_aggregate_kind::interface_)  != static_cast<uint8_t>(kdi_aggregate_kind::annotation_));
}

TEST_CASE("kdi_aggregate: defaults", "[dto][aggregate]") {
    kdi_aggregate agg;
    REQUIRE(agg.name.empty());
    REQUIRE(agg.fq_name.empty());
    REQUIRE(agg.mangled_name.empty());
    REQUIRE(agg.visibility == kdi_visibility::public_);
    REQUIRE(agg.is_abstract == false);
    REQUIRE(agg.is_final == false);
    REQUIRE(agg.is_const_struct == false);
    REQUIRE(agg.is_static_nested == false);
    REQUIRE(agg.enclosing_fq_name.empty());
    REQUIRE(agg.bases.empty());
    REQUIRE(agg.layout.empty());
    REQUIRE(agg.constructors.empty());
    REQUIRE(!agg.destructor.has_value());
    REQUIRE(agg.methods.empty());
    REQUIRE(agg.static_vars.empty());
    REQUIRE(!agg.vtable.has_value());
    REQUIRE(agg.default_constructor_mangled_name.empty());
    REQUIRE(agg.nested.empty());
    REQUIRE(agg.llvm_def.empty());
    REQUIRE(!agg.template_origin.has_value());
}

TEST_CASE("kdi_aggregate: class with inheritance and vtable", "[dto][aggregate]") {
    kdi_aggregate agg;
    agg.kind         = kdi_aggregate_kind::class_;
    agg.name         = "Dog";
    agg.fq_name      = "zoo::Dog";
    agg.mangled_name = "_KS3zoo3Dog";
    agg.is_final     = true;

    kdi_base b;
    b.fq_name         = "zoo::Animal";
    b.base_field_index = 0;
    b.byte_offset      = 0;
    agg.bases.push_back(b);

    kdi_vtable vt;
    vt.vtable_symbol = "_KTVN3zoo3DogE";
    vt.rtti_symbol   = "_KTRIN3zoo3DogE";
    agg.vtable       = vt;

    REQUIRE(agg.kind == kdi_aggregate_kind::class_);
    REQUIRE(agg.is_final);
    REQUIRE(agg.bases.size() == 1u);
    REQUIRE(agg.vtable.has_value());
    REQUIRE(agg.vtable->vtable_symbol == "_KTVN3zoo3DogE");
}

TEST_CASE("kdi_aggregate: nested aggregates", "[dto][aggregate]") {
    kdi_aggregate outer;
    outer.name    = "Outer";
    outer.fq_name = "ns::Outer";

    kdi_aggregate inner;
    inner.name              = "Inner";
    inner.fq_name           = "ns::Outer::Inner";
    inner.is_static_nested  = true;
    inner.enclosing_fq_name = "ns::Outer";

    outer.nested.push_back(inner);
    REQUIRE(outer.nested.size() == 1u);
    REQUIRE(outer.nested[0].fq_name == "ns::Outer::Inner");
    REQUIRE(outer.nested[0].is_static_nested);
    REQUIRE(outer.nested[0].enclosing_fq_name == "ns::Outer");
}

// ─── kdi_enum ────────────────────────────────────────────────────────────────

TEST_CASE("kdi_enum_entry: defaults", "[dto][enum]") {
    kdi_enum_entry e;
    REQUIRE(e.name.empty());
    REQUIRE(e.value == 0);
    REQUIRE(e.is_default == false);
}

TEST_CASE("kdi_enum: stores entries and underlying type", "[dto][enum]") {
    kdi_enum en;
    en.name            = "Color";
    en.fq_name         = "gfx::Color";
    en.visibility      = kdi_visibility::public_;
    en.underlying_type = kdi_type::make_int(32);

    kdi_enum_entry red;   red.name   = "RED";   red.value = 0; red.is_default = true;
    kdi_enum_entry green; green.name = "GREEN"; green.value = 1;
    kdi_enum_entry blue;  blue.name  = "BLUE";  blue.value = 2;
    en.entries = {red, green, blue};

    REQUIRE(en.name == "Color");
    REQUIRE(en.entries.size() == 3u);
    REQUIRE(en.entries[0].is_default);
    REQUIRE(en.entries[1].value == 1);
    REQUIRE(en.entries[2].name == "BLUE");
    REQUIRE(std::holds_alternative<kdi_int_type>(en.underlying_type.value));
}

TEST_CASE("kdi_enum: derived enum with base_fq_name", "[dto][enum]") {
    kdi_enum en;
    en.name         = "ExtColor";
    en.fq_name      = "gfx::ExtColor";
    en.base_fq_name = "gfx::Color";
    en.underlying_type = kdi_type::make_int(32);

    kdi_enum_entry alpha; alpha.name = "ALPHA"; alpha.value = 3;
    en.entries.push_back(alpha);

    REQUIRE(en.base_fq_name.has_value());
    REQUIRE(*en.base_fq_name == "gfx::Color");
}

// ─── kdi_type_table ──────────────────────────────────────────────────────────

TEST_CASE("kdi_type_table: aggregate entries", "[dto][type_table]") {
    kdi_type_table tt;
    tt.aggregates.push_back({"math::Vec3", "_KS4math4Vec3"});
    tt.aggregates.push_back({"math::Mat4", "_KS4math4Mat4"});
    REQUIRE(tt.aggregates.size() == 2u);
    REQUIRE(tt.aggregates[0].fq_name == "math::Vec3");
    REQUIRE(tt.aggregates[1].mangled_name == "_KS4math4Mat4");
}

TEST_CASE("kdi_type_table: enum entries", "[dto][type_table]") {
    kdi_type_table tt;
    tt.enums.push_back({"gfx::Color"});
    REQUIRE(tt.enums.size() == 1u);
    REQUIRE(tt.enums[0].fq_name == "gfx::Color");
}

// ─── kdi_namespace ───────────────────────────────────────────────────────────

TEST_CASE("kdi_namespace: defaults are empty", "[dto][namespace]") {
    kdi_namespace ns;
    REQUIRE(ns.name.empty());
    REQUIRE(ns.fq_name.empty());
    REQUIRE(ns.namespaces.empty());
    REQUIRE(ns.aggregates.empty());
    REQUIRE(ns.enums.empty());
    REQUIRE(ns.functions.empty());
    REQUIRE(ns.variables.empty());
    REQUIRE(ns.template_defs.empty());
}

TEST_CASE("kdi_namespace: nested namespace tree", "[dto][namespace]") {
    kdi_namespace root;
    root.name    = "";
    root.fq_name = "";

    kdi_namespace child;
    child.name    = "math";
    child.fq_name = "math";

    kdi_namespace grandchild;
    grandchild.name    = "linalg";
    grandchild.fq_name = "math::linalg";
    child.namespaces.push_back(grandchild);

    root.namespaces.push_back(child);
    REQUIRE(root.namespaces.size() == 1u);
    REQUIRE(root.namespaces[0].namespaces.size() == 1u);
    REQUIRE(root.namespaces[0].namespaces[0].fq_name == "math::linalg");
}

// ─── kdi_unit ────────────────────────────────────────────────────────────────

TEST_CASE("kdi_unit: defaults", "[dto][unit]") {
    kdi_unit u;
    REQUIRE(u.name.empty());
    REQUIRE(u.root_ns.name.empty());
}

// ─── Template DTOs ───────────────────────────────────────────────────────────

TEST_CASE("kdi_template_param: type parameter", "[dto][template]") {
    kdi_template_param tp;
    tp.kind = "typename";
    tp.name = "T";
    REQUIRE(tp.kind == "typename");
    REQUIRE(tp.name == "T");
    REQUIRE(!tp.constraint_type.has_value());
    REQUIRE(!tp.default_type.has_value());
    REQUIRE(!tp.value_type.has_value());
    REQUIRE(!tp.default_value.has_value());
}

TEST_CASE("kdi_template_param: type parameter with constraint and default", "[dto][template]") {
    kdi_template_param tp;
    tp.kind            = "class";
    tp.name            = "T";
    tp.constraint_type = kdi_type::make_aggregate("Comparable");
    tp.default_type    = kdi_type::make_int(32);
    REQUIRE(tp.constraint_type.has_value());
    REQUIRE(std::holds_alternative<kdi_aggregate_ref>(tp.constraint_type->value));
    REQUIRE(tp.default_type.has_value());
    REQUIRE(std::holds_alternative<kdi_int_type>(tp.default_type->value));
}

TEST_CASE("kdi_template_param: value parameter", "[dto][template]") {
    kdi_template_param tp;
    tp.kind          = "value";
    tp.name          = "N";
    tp.value_type    = kdi_type::make_int(32);
    tp.default_value = "10";
    REQUIRE(tp.kind == "value");
    REQUIRE(tp.value_type.has_value());
    REQUIRE(tp.default_value.has_value());
    REQUIRE(*tp.default_value == "10");
}

TEST_CASE("kdi_template_arg: type argument", "[dto][template]") {
    kdi_template_arg arg;
    arg.type_arg = kdi_type::make_int(32);
    REQUIRE(arg.type_arg.has_value());
    REQUIRE(!arg.value_arg.has_value());
    REQUIRE(!arg.value_type.has_value());
}

TEST_CASE("kdi_template_arg: value argument with type", "[dto][template]") {
    kdi_template_arg arg;
    arg.value_arg  = "42";
    arg.value_type = kdi_type::make_int(64, false);
    REQUIRE(!arg.type_arg.has_value());
    REQUIRE(arg.value_arg.has_value());
    REQUIRE(*arg.value_arg == "42");
    REQUIRE(arg.value_type.has_value());
    REQUIRE(std::get<kdi_int_type>(arg.value_type->value).bits == 64);
}

TEST_CASE("kdi_template_origin: stores base name and args", "[dto][template]") {
    kdi_template_origin origin;
    origin.base_name    = "Pair";
    origin.base_fq_name = "containers::Pair";

    kdi_template_arg a1;
    a1.type_arg = kdi_type::make_int(32);
    origin.args.push_back(a1);

    kdi_template_arg a2;
    a2.type_arg = kdi_type::make_float(64);
    origin.args.push_back(a2);

    REQUIRE(origin.base_name == "Pair");
    REQUIRE(origin.base_fq_name == "containers::Pair");
    REQUIRE(origin.args.size() == 2u);
    REQUIRE(origin.args[0].type_arg.has_value());
    REQUIRE(origin.args[1].type_arg.has_value());
}

TEST_CASE("kdi_template_def: stores complete definition", "[dto][template]") {
    kdi_template_def td;
    td.name        = "Box";
    td.fq_name     = "containers::Box";
    td.entity_kind = "struct";
    td.visibility  = "public";
    td.source      = "template<typename T> struct Box { val: T; }";

    kdi_template_param tp;
    tp.kind = "typename";
    tp.name = "T";
    td.params.push_back(tp);

    REQUIRE(td.name == "Box");
    REQUIRE(td.fq_name == "containers::Box");
    REQUIRE(td.entity_kind == "struct");
    REQUIRE(td.visibility == "public");
    REQUIRE(td.params.size() == 1u);
    REQUIRE(td.params[0].name == "T");
    REQUIRE(!td.source.empty());
}

TEST_CASE("kdi_function: with template_origin", "[dto][template]") {
    kdi_function fn;
    fn.name         = "identity__int__";
    fn.fq_name      = "tpl::identity__int__";
    fn.return_type  = kdi_type::make_int(32);
    fn.mangled_name = "_KFN3tpl8identityIiEi";

    kdi_template_origin origin;
    origin.base_name    = "identity";
    origin.base_fq_name = "tpl::identity";
    kdi_template_arg arg;
    arg.type_arg = kdi_type::make_int(32);
    origin.args.push_back(arg);
    fn.template_origin = origin;

    REQUIRE(fn.template_origin.has_value());
    REQUIRE(fn.template_origin->base_name == "identity");
    REQUIRE(fn.template_origin->args.size() == 1u);
}

TEST_CASE("kdi_aggregate: with template_origin", "[dto][template]") {
    kdi_aggregate agg;
    agg.name    = "Box__int__";
    agg.fq_name = "tpl::Box__int__";

    kdi_template_origin origin;
    origin.base_name    = "Box";
    origin.base_fq_name = "tpl::Box";
    kdi_template_arg arg;
    arg.type_arg = kdi_type::make_int(32);
    origin.args.push_back(arg);
    agg.template_origin = origin;

    REQUIRE(agg.template_origin.has_value());
    REQUIRE(agg.template_origin->base_name == "Box");
    REQUIRE(agg.template_origin->args.size() == 1u);
}

// ─── kdi_file round-trip (structural) ────────────────────────────────────────

TEST_CASE("kdi_file: default construction", "[dto][file]") {
    kdi_file file;
    REQUIRE(file.header.schema_major == KDI_SCHEMA_MAJOR);
    REQUIRE(file.header.module_name.empty());
    REQUIRE(file.types.aggregates.empty());
    REQUIRE(file.types.enums.empty());
    REQUIRE(file.unit.name.empty());
}

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

TEST_CASE("kdi_file: full file with aggregates, enums, and template defs", "[dto][file]") {
    kdi_file file;
    file.header.module_name   = "demo";
    file.header.lib_base      = "demo";
    file.header.target_triple = "x86_64-pc-linux-gnu";
    file.header.compiler_ver  = "klangc-0.1";
    file.header.dependencies  = {"std::core"};
    file.unit.name            = "demo";

    // Type table
    file.types.aggregates.push_back({"demo::Point", "_KS4demo5Point"});
    file.types.enums.push_back({"demo::Color"});

    // Aggregate in namespace
    kdi_aggregate agg;
    agg.kind         = kdi_aggregate_kind::struct_;
    agg.name         = "Point";
    agg.fq_name      = "demo::Point";
    agg.mangled_name = "_KS4demo5Point";
    agg.llvm_def     = "%struct.demo.Point = type { f32, f32 }";

    kdi_layout_member mx;
    mx.name = "x"; mx.fq_name = "demo::Point::x";
    mx.llvm_field_index = 0; mx.type = kdi_type::make_float(32);
    mx.mangled_name = "_KVM4demo5Point1x";
    agg.layout.push_back(mx);

    kdi_constructor ctor;
    ctor.mangled_name = "_KFMC1N4demo5PointE";
    ctor.llvm_def     = "declare void @_KFMC1N4demo5PointE(%struct.demo.Point*)";
    agg.constructors.push_back(ctor);
    agg.default_constructor_mangled_name = "_KFMC1N4demo5PointE";

    file.unit.root_ns.aggregates.push_back(agg);

    // Enum in namespace
    kdi_enum en;
    en.name            = "Color";
    en.fq_name         = "demo::Color";
    en.underlying_type = kdi_type::make_int(32);
    en.entries.push_back({"RED", 0, true});
    en.entries.push_back({"GREEN", 1, false});
    file.unit.root_ns.enums.push_back(en);

    // Template def
    kdi_template_def td;
    td.name        = "Wrapper";
    td.fq_name     = "demo::Wrapper";
    td.entity_kind = "struct";
    td.visibility  = "public";
    td.params.push_back({"typename", "T", {}, {}, {}, {}});
    td.source      = "template<typename T> struct Wrapper { val: T; }";
    file.unit.root_ns.template_defs.push_back(td);

    // Verify the full structure
    REQUIRE(file.header.module_name == "demo");
    REQUIRE(file.header.dependencies.size() == 1u);
    REQUIRE(file.types.aggregates.size() == 1u);
    REQUIRE(file.types.enums.size() == 1u);
    REQUIRE(file.unit.root_ns.aggregates.size() == 1u);
    REQUIRE(file.unit.root_ns.aggregates[0].constructors.size() == 1u);
    REQUIRE(file.unit.root_ns.aggregates[0].default_constructor_mangled_name == "_KFMC1N4demo5PointE");
    REQUIRE(file.unit.root_ns.enums.size() == 1u);
    REQUIRE(file.unit.root_ns.enums[0].entries.size() == 2u);
    REQUIRE(file.unit.root_ns.template_defs.size() == 1u);
}

