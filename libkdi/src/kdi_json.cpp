/*
 * K Language compiler — libkdi
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

#include "kdi_json.hpp"
#include "kdi_file.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <stdexcept>

using json = nlohmann::json;

namespace kdi {

// ─────────────────────────────────────────────────────────────────────────────
// Forward declarations
// ─────────────────────────────────────────────────────────────────────────────

static json        to_json(const kdi_file&);
static kdi_file    from_json_file(const json&);

static json        to_json(const kdi_header&);
static kdi_header  from_json_header(const json&);

static json             to_json(const kdi_type_table&);
static kdi_type_table   from_json_type_table(const json&);

static json        to_json(const kdi_type&);
static kdi_type    from_json_type(const json&);

static json        to_json(const kdi_param&);
static kdi_param   from_json_param(const json&);

static json        to_json(const kdi_variable&);
static kdi_variable from_json_variable(const json&);

static json        to_json(const kdi_function&);
static kdi_function from_json_function(const json&);

static json        to_json(const kdi_method&);
static kdi_method  from_json_method(const json&);

static json            to_json(const kdi_constructor&);
static kdi_constructor from_json_constructor(const json&);

static json           to_json(const kdi_destructor&);
static kdi_destructor from_json_destructor(const json&);

static json        to_json(const kdi_vtable&);
static kdi_vtable  from_json_vtable(const json&);

static json              to_json(const kdi_layout_field&);
static kdi_layout_field  from_json_layout_field(const json&);

static json          to_json(const kdi_aggregate&);
static kdi_aggregate from_json_aggregate(const json&);

static json      to_json(const kdi_enum&);
static kdi_enum  from_json_enum(const json&);

static json          to_json(const kdi_namespace&);
static kdi_namespace from_json_namespace(const json&);

// ─────────────────────────────────────────────────────────────────────────────
// Visibility helpers
// ─────────────────────────────────────────────────────────────────────────────

static std::string vis_to_str(kdi_visibility v) {
    return v == kdi_visibility::protected_ ? "protected" : "public";
}

static kdi_visibility vis_from_str(const std::string& s) {
    if (s == "protected") return kdi_visibility::protected_;
    return kdi_visibility::public_;
}

// ─────────────────────────────────────────────────────────────────────────────
// kdi_type
// ─────────────────────────────────────────────────────────────────────────────

static json to_json(const kdi_type& t) {
    return std::visit([](auto&& v) -> json {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, kdi_void_type>)
            return {{"kind","void"}};
        else if constexpr (std::is_same_v<T, kdi_bool_type>)
            return {{"kind","bool"}};
        else if constexpr (std::is_same_v<T, kdi_char_type>)
            return {{"kind","char"}};
        else if constexpr (std::is_same_v<T, kdi_int_type>)
            return {{"kind","int"},{"bits",v.bits},{"signed",v.is_signed}};
        else if constexpr (std::is_same_v<T, kdi_float_type>)
            return {{"kind","float"},{"bits",v.bits}};
        else if constexpr (std::is_same_v<T, kdi_ref_type>)
            return {{"kind","ref"},{"inner",to_json(*v.inner)}};
        else if constexpr (std::is_same_v<T, kdi_ptr_type>)
            return {{"kind","ptr"},{"inner",to_json(*v.inner)}};
        else if constexpr (std::is_same_v<T, kdi_link_type>)
            return {{"kind","link"},{"inner",to_json(*v.inner)}};
        else if constexpr (std::is_same_v<T, kdi_view_type>)
            return {{"kind","view"},{"inner",to_json(*v.inner)}};
        else if constexpr (std::is_same_v<T, kdi_owner_type>)
            return {{"kind","owner"},{"inner",to_json(*v.inner)}};
        else if constexpr (std::is_same_v<T, kdi_drain_type>)
            return {{"kind","drain"},{"inner",to_json(*v.inner)}};
        else if constexpr (std::is_same_v<T, kdi_const_type>)
            return {{"kind","const"},{"inner",to_json(*v.inner)}};
        else if constexpr (std::is_same_v<T, kdi_array_type>)
            return {{"kind","array"},{"elem",to_json(*v.elem)}};
        else if constexpr (std::is_same_v<T, kdi_sized_array_type>)
            return {{"kind","sized_array"},{"elem",to_json(*v.elem)},{"size",v.size}};
        else if constexpr (std::is_same_v<T, kdi_fn_ref_type>) {
            json ps = json::array();
            for (auto& p : v.params) ps.push_back(to_json(*p));
            return {{"kind","fn_ref"},{"ret",to_json(*v.ret)},{"params",ps}};
        }
        else if constexpr (std::is_same_v<T, kdi_aggregate_ref>)
            return {{"kind","aggregate"},{"fq_name",v.fq_name}};
        else if constexpr (std::is_same_v<T, kdi_enum_ref>)
            return {{"kind","enum"},{"fq_name",v.fq_name}};
        else if constexpr (std::is_same_v<T, kdi_template_param_ref>)
            return {{"kind","template_param"},{"name",v.name}};
        else
            return {{"kind","unknown"}};
    }, t.value);
}

static kdi_type from_json_type(const json& j) {
    std::string kind = j.at("kind");
    if (kind == "void")   return kdi_type::make_void();
    if (kind == "bool")   return kdi_type::make_bool();
    if (kind == "char")   return kdi_type{kdi_char_type{}};
    if (kind == "int")    return kdi_type::make_int(j.at("bits").get<uint32_t>(),
                                                     j.value("signed", true));
    if (kind == "float")  return kdi_type::make_float(j.at("bits").get<uint32_t>());
    if (kind == "ref") {
        kdi_ref_type r; r.inner = std::make_shared<kdi_type>(from_json_type(j.at("inner")));
        return kdi_type{std::move(r)};
    }
    if (kind == "ptr") {
        kdi_ptr_type r; r.inner = std::make_shared<kdi_type>(from_json_type(j.at("inner")));
        return kdi_type{std::move(r)};
    }
    if (kind == "link") {
        kdi_link_type r; r.inner = std::make_shared<kdi_type>(from_json_type(j.at("inner")));
        return kdi_type{std::move(r)};
    }
    if (kind == "view") {
        kdi_view_type r; r.inner = std::make_shared<kdi_type>(from_json_type(j.at("inner")));
        return kdi_type{std::move(r)};
    }
    if (kind == "owner") {
        kdi_owner_type r; r.inner = std::make_shared<kdi_type>(from_json_type(j.at("inner")));
        return kdi_type{std::move(r)};
    }
    if (kind == "drain") {
        kdi_drain_type r; r.inner = std::make_shared<kdi_type>(from_json_type(j.at("inner")));
        return kdi_type{std::move(r)};
    }
    if (kind == "const") {
        kdi_const_type r; r.inner = std::make_shared<kdi_type>(from_json_type(j.at("inner")));
        return kdi_type{std::move(r)};
    }
    if (kind == "array") {
        kdi_array_type r; r.elem = std::make_shared<kdi_type>(from_json_type(j.at("elem")));
        return kdi_type{std::move(r)};
    }
    if (kind == "sized_array") {
        kdi_sized_array_type r;
        r.elem = std::make_shared<kdi_type>(from_json_type(j.at("elem")));
        r.size = j.at("size").get<uint64_t>();
        return kdi_type{std::move(r)};
    }
    if (kind == "fn_ref") {
        kdi_fn_ref_type r;
        r.ret = std::make_shared<kdi_type>(from_json_type(j.at("ret")));
        for (auto& p : j.at("params"))
            r.params.push_back(std::make_shared<kdi_type>(from_json_type(p)));
        return kdi_type{std::move(r)};
    }
    if (kind == "aggregate")
        return kdi_type::make_aggregate(j.at("fq_name").get<std::string>());
    if (kind == "enum")
        return kdi_type::make_enum(j.at("fq_name").get<std::string>());
    if (kind == "template_param")
        return kdi_type::make_template_param(j.at("name").get<std::string>());
    throw kdi_json_error("unknown type kind: " + kind);
}

// ─────────────────────────────────────────────────────────────────────────────
// kdi_param / kdi_variable
// ─────────────────────────────────────────────────────────────────────────────

static json to_json(const kdi_param& p) {
    json j = {{"name", p.name}, {"type", to_json(p.type)}};
    if (p.is_varargs) j["is_varargs"] = true;
    return j;
}
static kdi_param from_json_param(const json& j) {
    kdi_param p;
    p.name = j.at("name");
    p.type = from_json_type(j.at("type"));
    if (j.contains("is_varargs")) p.is_varargs = j["is_varargs"].get<bool>();
    return p;
}

static json to_json(const kdi_variable& v) {
    return {
        {"name",         v.name},
        {"fq_name",      v.fq_name},
        {"visibility",   vis_to_str(v.visibility)},
        {"type",         to_json(v.type)},
        {"is_const",     v.is_const},
        {"mangled_name", v.mangled_name},
    };
}
static kdi_variable from_json_variable(const json& j) {
    kdi_variable v;
    v.name         = j.at("name");
    v.fq_name      = j.at("fq_name");
    v.visibility   = vis_from_str(j.value("visibility", "public"));
    v.type         = from_json_type(j.at("type"));
    v.is_const     = j.value("is_const", false);
    v.mangled_name = j.value("mangled_name", "");
    return v;
}

// ─────────────────────────────────────────────────────────────────────────────
// Template DTOs
// ─────────────────────────────────────────────────────────────────────────────

static json to_json(const kdi_template_param& p) {
    json j = {{"kind", p.kind}, {"name", p.name}};
    if (p.constraint_type) j["constraint_type"] = to_json(*p.constraint_type);
    if (p.default_type)    j["default_type"] = to_json(*p.default_type);
    if (p.value_type)      j["value_type"] = to_json(*p.value_type);
    if (p.default_value)   j["default_value"] = *p.default_value;
    return j;
}
static kdi_template_param from_json_template_param(const json& j) {
    kdi_template_param p;
    p.kind = j.at("kind");
    p.name = j.at("name");
    if (j.contains("constraint_type")) p.constraint_type = from_json_type(j.at("constraint_type"));
    if (j.contains("default_type"))    p.default_type = from_json_type(j.at("default_type"));
    if (j.contains("value_type"))      p.value_type = from_json_type(j.at("value_type"));
    if (j.contains("default_value"))   p.default_value = j.at("default_value").get<std::string>();
    return p;
}

static json to_json(const kdi_template_arg& a) {
    json j = json::object();
    if (a.type_arg)  j["type_arg"] = to_json(*a.type_arg);
    if (a.value_arg) j["value_arg"] = *a.value_arg;
    if (a.value_type) j["value_type"] = to_json(*a.value_type);
    return j;
}
static kdi_template_arg from_json_template_arg(const json& j) {
    kdi_template_arg a;
    if (j.contains("type_arg"))  a.type_arg = from_json_type(j.at("type_arg"));
    if (j.contains("value_arg")) a.value_arg = j.at("value_arg").get<std::string>();
    if (j.contains("value_type")) a.value_type = from_json_type(j.at("value_type"));
    return a;
}

static json to_json(const kdi_template_origin& o) {
    json args = json::array();
    for (auto& a : o.args) args.push_back(to_json(a));
    return {{"base_name", o.base_name}, {"base_fq_name", o.base_fq_name}, {"args", args}};
}
static kdi_template_origin from_json_template_origin(const json& j) {
    kdi_template_origin o;
    o.base_name    = j.at("base_name");
    o.base_fq_name = j.at("base_fq_name");
    for (auto& a : j.value("args", json::array()))
        o.args.push_back(from_json_template_arg(a));
    return o;
}

static json to_json(const kdi_template_def& d) {
    json params = json::array();
    for (auto& p : d.params) params.push_back(to_json(p));
    json j = {{"name", d.name}, {"fq_name", d.fq_name}, {"entity_kind", d.entity_kind},
              {"visibility", d.visibility}, {"params", params}, {"source", d.source}};
    if (d.is_generic) j["is_generic"] = true;
    if (d.aggregate_signature) j["aggregate_signature"] = to_json(*d.aggregate_signature);
    if (d.function_signature) j["function_signature"] = to_json(*d.function_signature);
    return j;
}
static kdi_template_def from_json_template_def(const json& j) {
    kdi_template_def d;
    d.name        = j.at("name");
    d.fq_name     = j.at("fq_name");
    d.entity_kind = j.at("entity_kind");
    d.visibility  = j.value("visibility", "public");
    d.is_generic  = j.value("is_generic", false);
    for (auto& p : j.value("params", json::array()))
        d.params.push_back(from_json_template_param(p));
    d.source = j.value("source", "");
    if (j.contains("aggregate_signature"))
        d.aggregate_signature = std::make_shared<kdi_aggregate>(from_json_aggregate(j.at("aggregate_signature")));
    if (j.contains("function_signature"))
        d.function_signature = std::make_shared<kdi_function>(from_json_function(j.at("function_signature")));
    return d;
}

// ─────────────────────────────────────────────────────────────────────────────
// kdi_function / kdi_method
// ─────────────────────────────────────────────────────────────────────────────

static json params_to_json(const std::vector<kdi_param>& ps) {
    json arr = json::array();
    for (auto& p : ps) arr.push_back(to_json(p));
    return arr;
}
static std::vector<kdi_param> params_from_json(const json& j) {
    std::vector<kdi_param> ps;
    if (j.is_array()) for (auto& p : j) ps.push_back(from_json_param(p));
    return ps;
}

static json to_json(const kdi_function& f) {
    json j = {
        {"name",         f.name},
        {"fq_name",      f.fq_name},
        {"visibility",   vis_to_str(f.visibility)},
        {"is_static",    f.is_static},
        {"return_type",  to_json(f.return_type)},
        {"params",       params_to_json(f.params)},
        {"mangled_name", f.mangled_name},
        {"llvm_def",     f.llvm_def},
    };
    if (f.is_operator) j["is_operator"] = true;
    if (f.template_origin) j["template_origin"] = to_json(*f.template_origin);
    if (!f.throws_spec.empty()) {
        json ts = json::array();
        for (auto& t : f.throws_spec) ts.push_back(to_json(t));
        j["throws_spec"] = ts;
    }
    return j;
}
static kdi_function from_json_function(const json& j) {
    kdi_function f;
    f.name         = j.at("name");
    f.fq_name      = j.at("fq_name");
    f.visibility   = vis_from_str(j.value("visibility", "public"));
    f.is_static    = j.value("is_static", false);
    f.is_operator  = j.value("is_operator", false);
    f.return_type  = from_json_type(j.at("return_type"));
    f.params       = params_from_json(j.value("params", json::array()));
    f.mangled_name = j.value("mangled_name", "");
    f.llvm_def     = j.at("llvm_def");
    if (j.contains("template_origin")) f.template_origin = from_json_template_origin(j.at("template_origin"));
    if (j.contains("throws_spec")) {
        for (auto& t : j.at("throws_spec")) f.throws_spec.push_back(from_json_type(t));
    }
    return f;
}

static json to_json(const kdi_method& m) {
    json j = {
        {"name",            m.name},
        {"fq_name",         m.fq_name},
        {"visibility",      vis_to_str(m.visibility)},
        {"is_static",       m.is_static},
        {"is_const_member", m.is_const_member},
        {"is_virtual",      m.is_virtual},
        {"is_abstract",     m.is_abstract},
        {"is_final",        m.is_final},
        {"vtable_slot",     m.vtable_slot},
        {"return_type",     to_json(m.return_type)},
        {"params",          params_to_json(m.params)},
        {"mangled_name",    m.mangled_name},
        {"llvm_def",        m.llvm_def},
    };
    if (m.is_operator) j["is_operator"] = true;
    if (m.template_origin) j["template_origin"] = to_json(*m.template_origin);
    if (!m.throws_spec.empty()) {
        json ts = json::array();
        for (auto& t : m.throws_spec) ts.push_back(to_json(t));
        j["throws_spec"] = ts;
    }
    return j;
}
static kdi_method from_json_method(const json& j) {
    kdi_method m;
    m.name            = j.at("name");
    m.fq_name         = j.at("fq_name");
    m.visibility      = vis_from_str(j.value("visibility", "public"));
    m.is_static       = j.value("is_static", false);
    m.is_const_member = j.value("is_const_member", false);
    m.is_virtual      = j.value("is_virtual", false);
    m.is_abstract     = j.value("is_abstract", false);
    m.is_final        = j.value("is_final", false);
    m.is_operator     = j.value("is_operator", false);
    m.vtable_slot     = j.value("vtable_slot", -1);
    m.return_type     = from_json_type(j.at("return_type"));
    m.params          = params_from_json(j.value("params", json::array()));
    m.mangled_name    = j.value("mangled_name", "");
    m.llvm_def        = j.at("llvm_def");
    if (j.contains("template_origin")) m.template_origin = from_json_template_origin(j.at("template_origin"));
    if (j.contains("throws_spec")) {
        for (auto& t : j.at("throws_spec")) m.throws_spec.push_back(from_json_type(t));
    }
    return m;
}

// ─────────────────────────────────────────────────────────────────────────────
// kdi_constructor / kdi_destructor
// ─────────────────────────────────────────────────────────────────────────────

static json to_json(const kdi_constructor& c) {
    return {
        {"visibility",          vis_to_str(c.visibility)},
        {"is_copy_constructor", c.is_copy_constructor},
        {"is_defaulted",        c.is_defaulted},
        {"is_deleted",          c.is_deleted},
        {"params",              params_to_json(c.params)},
        {"mangled_name",        c.mangled_name},
        {"mangled_name_c2",     c.mangled_name_c2},
        {"llvm_def",            c.llvm_def},
    };
}
static kdi_constructor from_json_constructor(const json& j) {
    kdi_constructor c;
    c.visibility          = vis_from_str(j.value("visibility", "public"));
    c.is_copy_constructor = j.value("is_copy_constructor", false);
    c.is_defaulted        = j.value("is_defaulted", false);
    c.is_deleted          = j.value("is_deleted", false);
    c.params              = params_from_json(j.value("params", json::array()));
    c.mangled_name        = j.value("mangled_name", "");
    c.mangled_name_c2     = j.value("mangled_name_c2", "");
    c.llvm_def            = j.at("llvm_def");
    return c;
}

static json to_json(const kdi_destructor& d) {
    return {
        {"visibility",            vis_to_str(d.visibility)},
        {"is_virtual",            d.is_virtual},
        {"is_compiler_generated", d.is_compiler_generated},
        {"mangled_name",          d.mangled_name},
        {"mangled_name_d2",       d.mangled_name_d2},
        {"llvm_def",              d.llvm_def},
    };
}
static kdi_destructor from_json_destructor(const json& j) {
    kdi_destructor d;
    d.visibility            = vis_from_str(j.value("visibility", "public"));
    d.is_virtual            = j.value("is_virtual", false);
    d.is_compiler_generated = j.value("is_compiler_generated", false);
    d.mangled_name          = j.value("mangled_name", "");
    d.mangled_name_d2       = j.value("mangled_name_d2", "");
    d.llvm_def              = j.at("llvm_def");
    return d;
}

// ─────────────────────────────────────────────────────────────────────────────
// kdi_vtable
// ─────────────────────────────────────────────────────────────────────────────

static json to_json(const kdi_vtable& vt) {
    json slots = json::array();
    for (auto& s : vt.slots)
        slots.push_back({{"slot_index",s.slot_index},{"introducing_func",s.introducing_func},
                         {"override_symbol",s.override_symbol},{"is_abstract",s.is_abstract}});
    json sec = json::array();
    for (auto& sv : vt.secondary) {
        json thunks = json::array();
        for (auto& t : sv.thunks)
            thunks.push_back({{"slot_index",t.slot_index},{"real_func_symbol",t.real_func_symbol},
                              {"this_adjustment",t.this_adjustment},{"needs_thunk",t.needs_thunk}});
        sec.push_back({{"base_fq_name",sv.base_fq_name},{"base_offset",sv.base_offset},
                       {"vtable_symbol",sv.vtable_symbol},{"thunks",thunks}});
    }
    return {{"vtable_symbol",vt.vtable_symbol},{"rtti_symbol",vt.rtti_symbol},
            {"llvm_def",vt.llvm_def},
            {"slots",slots},{"secondary",sec}};
}
static kdi_vtable from_json_vtable(const json& j) {
    kdi_vtable vt;
    vt.vtable_symbol = j.value("vtable_symbol", "");
    vt.rtti_symbol   = j.value("rtti_symbol", "");
    vt.llvm_def      = j.at("llvm_def");
    for (auto& s : j.value("slots", json::array())) {
        kdi_vtable_slot sl;
        sl.slot_index       = s.value("slot_index", 0u);
        sl.introducing_func = s.value("introducing_func", "");
        sl.override_symbol  = s.value("override_symbol", "");
        sl.is_abstract      = s.value("is_abstract", false);
        vt.slots.push_back(sl);
    }
    for (auto& sv : j.value("secondary", json::array())) {
        kdi_secondary_vtable sec;
        sec.base_fq_name  = sv.value("base_fq_name", "");
        sec.base_offset   = sv.value("base_offset", uint64_t{0});
        sec.vtable_symbol = sv.value("vtable_symbol", "");
        for (auto& t : sv.value("thunks", json::array())) {
            kdi_thunk th;
            th.slot_index       = t.value("slot_index", 0u);
            th.real_func_symbol = t.value("real_func_symbol", "");
            th.this_adjustment  = t.value("this_adjustment", 0);
            th.needs_thunk      = t.value("needs_thunk", false);
            sec.thunks.push_back(th);
        }
        vt.secondary.push_back(sec);
    }
    return vt;
}

// ─────────────────────────────────────────────────────────────────────────────
// kdi_layout_field
// ─────────────────────────────────────────────────────────────────────────────

static json to_json(const kdi_layout_field& lf) {
    return std::visit([](auto&& v) -> json {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, kdi_layout_member>)
            return {{"kind","member"},{"name",v.name},{"fq_name",v.fq_name},
                    {"visibility",vis_to_str(v.visibility)},{"llvm_field_index",v.llvm_field_index},
                    {"type",to_json(v.type)},{"is_const",v.is_const},{"mangled_name",v.mangled_name}};
        else if constexpr (std::is_same_v<T, kdi_layout_vptr>)
            return {{"kind","vptr"},{"llvm_field_index",v.llvm_field_index},{"vtable_symbol",v.vtable_symbol}};
        else if constexpr (std::is_same_v<T, kdi_layout_vptr_secondary>)
            return {{"kind","vptr_secondary"},{"llvm_field_index",v.llvm_field_index},
                    {"base_fq_name",v.base_fq_name},{"vtable_symbol",v.vtable_symbol}};
        else if constexpr (std::is_same_v<T, kdi_layout_base_subobject>)
            return {{"kind","base_subobject"},{"llvm_field_index",v.llvm_field_index},{"base_fq_name",v.base_fq_name}};
        else if constexpr (std::is_same_v<T, kdi_layout_vbptr>)
            return {{"kind","vbptr"},{"llvm_field_index",v.llvm_field_index},{"vbase_fq_name",v.vbase_fq_name}};
        else if constexpr (std::is_same_v<T, kdi_layout_vbase_subobject>)
            return {{"kind","vbase_subobject"},{"llvm_field_index",v.llvm_field_index},{"vbase_fq_name",v.vbase_fq_name}};
        else if constexpr (std::is_same_v<T, kdi_layout_parent_ref>)
            return {{"kind","parent_ref"},{"llvm_field_index",v.llvm_field_index},{"parent_fq_name",v.parent_fq_name}};
        else if constexpr (std::is_same_v<T, kdi_layout_opaque_block>)
            return {{"kind","opaque_block"},{"llvm_field_index",v.llvm_field_index},
                    {"field_count",v.field_count},{"size_bits",v.size_bits}};
        else
            return {{"kind","unknown"}};
    }, lf);
}

static kdi_layout_field from_json_layout_field(const json& j) {
    std::string kind = j.at("kind");
    if (kind == "member") {
        kdi_layout_member m;
        m.name             = j.at("name");
        m.fq_name          = j.at("fq_name");
        m.visibility       = vis_from_str(j.value("visibility", "public"));
        m.llvm_field_index = j.value("llvm_field_index", 0u);
        m.type             = from_json_type(j.at("type"));
        m.is_const         = j.value("is_const", false);
        m.mangled_name     = j.value("mangled_name", "");
        return m;
    }
    if (kind == "vptr") {
        kdi_layout_vptr v;
        v.llvm_field_index = j.value("llvm_field_index", 0u);
        v.vtable_symbol    = j.value("vtable_symbol", "");
        return v;
    }
    if (kind == "vptr_secondary") {
        kdi_layout_vptr_secondary v;
        v.llvm_field_index = j.value("llvm_field_index", 0u);
        v.base_fq_name     = j.value("base_fq_name", "");
        v.vtable_symbol    = j.value("vtable_symbol", "");
        return v;
    }
    if (kind == "base_subobject") {
        kdi_layout_base_subobject b;
        b.llvm_field_index = j.value("llvm_field_index", 0u);
        b.base_fq_name     = j.value("base_fq_name", "");
        return b;
    }
    if (kind == "vbptr") {
        kdi_layout_vbptr v;
        v.llvm_field_index = j.value("llvm_field_index", 0u);
        v.vbase_fq_name    = j.value("vbase_fq_name", "");
        return v;
    }
    if (kind == "vbase_subobject") {
        kdi_layout_vbase_subobject v;
        v.llvm_field_index = j.value("llvm_field_index", 0u);
        v.vbase_fq_name    = j.value("vbase_fq_name", "");
        return v;
    }
    if (kind == "parent_ref") {
        kdi_layout_parent_ref r;
        r.llvm_field_index = j.value("llvm_field_index", 0u);
        r.parent_fq_name   = j.value("parent_fq_name", "");
        return r;
    }
    if (kind == "opaque_block") {
        kdi_layout_opaque_block o;
        o.llvm_field_index = j.value("llvm_field_index", 0u);
        o.field_count      = j.value("field_count", 0u);
        o.size_bits        = j.value("size_bits", uint64_t{0});
        return o;
    }
    throw kdi_json_error("unknown layout field kind: " + kind);
}

// ─────────────────────────────────────────────────────────────────────────────
// kdi_aggregate
// ─────────────────────────────────────────────────────────────────────────────

static std::string agg_kind_to_str(kdi_aggregate_kind k) {
    switch (k) {
        case kdi_aggregate_kind::class_:      return "class";
        case kdi_aggregate_kind::interface_:  return "interface";
        case kdi_aggregate_kind::annotation_: return "annotation";
        default:                              return "struct";
    }
}
static kdi_aggregate_kind agg_kind_from_str(const std::string& s) {
    if (s == "class")      return kdi_aggregate_kind::class_;
    if (s == "interface")  return kdi_aggregate_kind::interface_;
    if (s == "annotation") return kdi_aggregate_kind::annotation_;
    return kdi_aggregate_kind::struct_;
}

static json to_json(const kdi_aggregate& a) {
    json bases = json::array();
    for (auto& b : a.bases)
        bases.push_back({{"fq_name",b.fq_name},{"visibility",vis_to_str(b.visibility)},
                         {"is_virtual",b.is_virtual},{"base_field_index",b.base_field_index},
                         {"byte_offset",b.byte_offset}});

    json layout = json::array();
    for (auto& lf : a.layout) layout.push_back(to_json(lf));

    json ctors = json::array();
    for (auto& c : a.constructors) ctors.push_back(to_json(c));

    json methods = json::array();
    for (auto& m : a.methods) methods.push_back(to_json(m));

    json static_vars = json::array();
    for (auto& v : a.static_vars) static_vars.push_back(to_json(v));

    json nested = json::array();
    for (auto& n : a.nested) nested.push_back(to_json(n));

    json nested_uns = json::array();
    for (auto& u : a.nested_unions) {
        json uobj = {{"name", u.name}, {"fq_name", u.fq_name}, {"mangled_name", u.mangled_name},
                     {"visibility", vis_to_str(u.visibility)}};
        json alts = json::array();
        for (auto& alt : u.alternatives) {
            json aobj = {{"name", alt.name}, {"is_const", alt.is_const},
                         {"type", to_json(alt.type)}};
            alts.push_back(std::move(aobj));
        }
        uobj["alternatives"] = std::move(alts);
        if (!u.llvm_def.empty()) uobj["llvm_def"] = u.llvm_def;
        if (u.template_origin) uobj["template_origin"] = to_json(*u.template_origin);
        nested_uns.push_back(std::move(uobj));
    }

    json obj = {
        {"kind",          agg_kind_to_str(a.kind)},
        {"name",          a.name},
        {"fq_name",       a.fq_name},
        {"mangled_name",  a.mangled_name},
        {"visibility",    vis_to_str(a.visibility)},
        {"is_abstract",   a.is_abstract},
        {"is_final",      a.is_final},
        {"is_const_struct",a.is_const_struct},
        {"is_static_nested",a.is_static_nested},
        {"bases",         bases},
        {"layout",        layout},
        {"constructors",  ctors},
        {"methods",       methods},
        {"static_vars",   static_vars},
        {"nested",        nested},
        {"nested_unions", nested_uns},
        {"llvm_def",      a.llvm_def},
    };
    if (!a.default_constructor_mangled_name.empty())
        obj["default_constructor_mangled_name"] = a.default_constructor_mangled_name;
    if (!a.enclosing_fq_name.empty())
        obj["enclosing_fq_name"] = a.enclosing_fq_name;
    if (a.destructor) obj["destructor"] = to_json(*a.destructor);
    if (a.vtable)     obj["vtable"]     = to_json(*a.vtable);
    if (a.template_origin) obj["template_origin"] = to_json(*a.template_origin);
    return obj;
}

static kdi_aggregate from_json_aggregate(const json& j) {
    kdi_aggregate a;
    a.kind          = agg_kind_from_str(j.value("kind", "struct"));
    a.name          = j.at("name");
    a.fq_name       = j.at("fq_name");
    a.mangled_name  = j.value("mangled_name", "");
    a.visibility    = vis_from_str(j.value("visibility", "public"));
    a.is_abstract   = j.value("is_abstract", false);
    a.is_final      = j.value("is_final", false);
    a.is_const_struct = j.value("is_const_struct", false);
    a.is_static_nested = j.value("is_static_nested", false);
    a.enclosing_fq_name = j.value("enclosing_fq_name", "");

    for (auto& b : j.value("bases", json::array())) {
        kdi_base kb;
        kb.fq_name          = b.at("fq_name");
        kb.visibility       = vis_from_str(b.value("visibility","public"));
        kb.is_virtual       = b.value("is_virtual", false);
        kb.base_field_index = b.value("base_field_index", -1);
        kb.byte_offset      = b.value("byte_offset", uint64_t{0});
        a.bases.push_back(kb);
    }
    for (auto& lf : j.value("layout", json::array()))
        a.layout.push_back(from_json_layout_field(lf));
    for (auto& c : j.value("constructors", json::array()))
        a.constructors.push_back(from_json_constructor(c));
    if (j.contains("destructor"))
        a.destructor = from_json_destructor(j.at("destructor"));
    for (auto& m : j.value("methods", json::array()))
        a.methods.push_back(from_json_method(m));
    for (auto& v : j.value("static_vars", json::array()))
        a.static_vars.push_back(from_json_variable(v));
    if (j.contains("vtable"))
        a.vtable = from_json_vtable(j.at("vtable"));
    for (auto& n : j.value("nested", json::array()))
        a.nested.push_back(from_json_aggregate(n));
    for (auto& u : j.value("nested_unions", json::array())) {
        kdi_union ku;
        ku.name         = u.at("name");
        ku.fq_name      = u.value("fq_name", "");
        ku.mangled_name = u.value("mangled_name", "");
        ku.visibility   = vis_from_str(u.value("visibility", "public"));
        for (auto& alt : u.value("alternatives", json::array())) {
            kdi_union_alternative ka;
            ka.name     = alt.at("name");
            ka.is_const = alt.value("is_const", false);
            if (alt.contains("type")) ka.type = from_json_type(alt.at("type"));
            ku.alternatives.push_back(std::move(ka));
        }
        ku.llvm_def = u.value("llvm_def", "");
        if (u.contains("template_origin")) ku.template_origin = from_json_template_origin(u.at("template_origin"));
        a.nested_unions.push_back(std::move(ku));
    }
    a.llvm_def = j.at("llvm_def");
    a.default_constructor_mangled_name = j.value("default_constructor_mangled_name", "");
    if (j.contains("template_origin")) a.template_origin = from_json_template_origin(j.at("template_origin"));
    return a;
}

// ─────────────────────────────────────────────────────────────────────────────
// kdi_enum
// ─────────────────────────────────────────────────────────────────────────────

static json to_json(const kdi_enum& e) {
    json entries = json::array();
    for (auto& en : e.entries) {
        json je = {{"name",en.name},{"value",en.value},{"is_default",en.is_default}};
        if (!en.object_init_members.empty()) {
            json members = json::array();
            for (const auto& [member_name, member_value] : en.object_init_members) {
                members.push_back({{"name", member_name}, {"value", member_value}});
            }
            je["object_init_members"] = std::move(members);
        }
        entries.push_back(std::move(je));
    }
    json obj = {
        {"name",            e.name},
        {"fq_name",         e.fq_name},
        {"visibility",      vis_to_str(e.visibility)},
        {"underlying_type", to_json(e.underlying_type)},
        {"entries",         entries},
    };
    if (e.object_type.has_value())
        obj["object_type"] = to_json(*e.object_type);
    if (e.object_table_symbol.has_value())
        obj["object_table_symbol"] = *e.object_table_symbol;
    if (e.base_fq_name.has_value())
        obj["base_fq_name"] = *e.base_fq_name;
    return obj;
}

static kdi_enum from_json_enum(const json& j) {
    kdi_enum e;
    e.name            = j.at("name");
    e.fq_name         = j.at("fq_name");
    e.visibility      = vis_from_str(j.value("visibility", "public"));
    e.underlying_type = from_json_type(j.at("underlying_type"));
    if (j.contains("object_type"))
        e.object_type = from_json_type(j.at("object_type"));
    if (j.contains("object_table_symbol"))
        e.object_table_symbol = j.at("object_table_symbol").get<std::string>();
    if (j.contains("base_fq_name"))
        e.base_fq_name = j.at("base_fq_name").get<std::string>();
    for (auto& en : j.value("entries", json::array())) {
        kdi_enum_entry entry;
        entry.name       = en.at("name");
        entry.value      = en.at("value").get<int64_t>();
        entry.is_default = en.value("is_default", false);
        for (auto& mem : en.value("object_init_members", json::array())) {
            entry.object_init_members.emplace_back(
                mem.at("name").get<std::string>(),
                mem.at("value").get<int64_t>());
        }
        e.entries.push_back(entry);
    }
    return e;
}

// ─────────────────────────────────────────────────────────────────────────────
// kdi_namespace
// ─────────────────────────────────────────────────────────────────────────────

static json to_json(const kdi_namespace& ns) {
    json sub = json::array();
    for (auto& n : ns.namespaces) sub.push_back(to_json(n));
    json aggs = json::array();
    for (auto& a : ns.aggregates) aggs.push_back(to_json(a));
    json enums = json::array();
    for (auto& e : ns.enums) enums.push_back(to_json(e));
    json fns = json::array();
    for (auto& f : ns.functions) fns.push_back(to_json(f));
    json vars = json::array();
    for (auto& v : ns.variables) vars.push_back(to_json(v));
    json tdefs = json::array();
    for (auto& td : ns.template_defs) tdefs.push_back(to_json(td));
    json obj = {{"name",ns.name},{"fq_name",ns.fq_name},
            {"namespaces",sub},{"aggregates",aggs},{"enums",enums},
            {"functions",fns},{"variables",vars}};
    if (!tdefs.empty()) obj["template_defs"] = tdefs;
    return obj;
}
static kdi_namespace from_json_namespace(const json& j) {
    kdi_namespace ns;
    ns.name    = j.value("name", "");
    ns.fq_name = j.value("fq_name", "");
    for (auto& n : j.value("namespaces", json::array()))
        ns.namespaces.push_back(from_json_namespace(n));
    for (auto& a : j.value("aggregates", json::array()))
        ns.aggregates.push_back(from_json_aggregate(a));
    for (auto& e : j.value("enums", json::array()))
        ns.enums.push_back(from_json_enum(e));
    for (auto& f : j.value("functions", json::array()))
        ns.functions.push_back(from_json_function(f));
    for (auto& v : j.value("variables", json::array()))
        ns.variables.push_back(from_json_variable(v));
    for (auto& td : j.value("template_defs", json::array()))
        ns.template_defs.push_back(from_json_template_def(td));
    return ns;
}

// ─────────────────────────────────────────────────────────────────────────────
// kdi_header / kdi_type_table
// ─────────────────────────────────────────────────────────────────────────────

static json to_json(const kdi_header& h) {
    json j = {
        {"schema_major",  h.schema_major},
        {"schema_minor",  h.schema_minor},
        {"module_name",   h.module_name},
        {"lib_base",      h.lib_base},
        {"lib_path",      h.lib_path},
        {"target_triple", h.target_triple},
        {"compiler_ver",  h.compiler_ver},
    };
    if (!h.dependencies.empty()) {
        json deps = json::array();
        for (const auto& dep : h.dependencies) deps.push_back(dep);
        j["dependencies"] = deps;
    }
    return j;
}
static kdi_header from_json_header(const json& j) {
    kdi_header h;
    h.schema_major  = j.value("schema_major",  KDI_SCHEMA_MAJOR);
    h.schema_minor  = j.value("schema_minor",  KDI_SCHEMA_MINOR);
    h.module_name   = j.value("module_name",   "");
    h.lib_base      = j.value("lib_base",      "");
    h.lib_path      = j.value("lib_path",      "");
    h.target_triple = j.value("target_triple", "");
    h.compiler_ver  = j.value("compiler_ver",  "");
    if (j.contains("dependencies") && j["dependencies"].is_array()) {
        for (const auto& dep : j["dependencies"]) {
            if (dep.is_string()) h.dependencies.push_back(dep.get<std::string>());
        }
    }
    return h;
}

static json to_json(const kdi_type_table& tt) {
    json arr = json::array();
    for (auto& e : tt.aggregates)
        arr.push_back({{"fq_name",e.fq_name},{"mangled_name",e.mangled_name}});
    json enums = json::array();
    for (auto& e : tt.enums)
        enums.push_back({{"fq_name",e.fq_name}});
    json obj = {{"aggregates", arr}};
    if (!enums.empty()) obj["enums"] = enums;
    return obj;
}
static kdi_type_table from_json_type_table(const json& j) {
    kdi_type_table tt;
    for (auto& e : j.value("aggregates", json::array())) {
        kdi_aggregate_type_entry entry;
        entry.fq_name      = e.value("fq_name", "");
        entry.mangled_name = e.value("mangled_name", "");
        tt.aggregates.push_back(entry);
    }
    for (auto& e : j.value("enums", json::array())) {
        kdi_enum_type_entry entry;
        entry.fq_name = e.value("fq_name", "");
        tt.enums.push_back(entry);
    }
    return tt;
}

// ─────────────────────────────────────────────────────────────────────────────
// kdi_file
// ─────────────────────────────────────────────────────────────────────────────

static json to_json(const kdi_file& f) {
    return {
        {"header",   to_json(f.header)},
        {"types",    to_json(f.types)},
        {"unit",     {{"name", f.unit.name}, {"root_ns", to_json(f.unit.root_ns)}}},
    };
}

static kdi_file from_json_file(const json& j) {
    kdi_file f;
    f.header = from_json_header(j.at("header"));
    if (j.contains("types")) f.types = from_json_type_table(j.at("types"));
    if (j.contains("unit")) {
        auto& u = j.at("unit");
        f.unit.name    = u.value("name", "");
        f.unit.root_ns = from_json_namespace(u.at("root_ns"));
    }
    return f;
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

void kdi_write_json(const kdi_file& file, std::ostream& out) {
    out << to_json(file).dump(2);
    if (!out) throw std::runtime_error("kdi_write_json: write error");
}

kdi_file kdi_read_json(std::istream& in) {
    json j;
    try {
        in >> j;
    } catch (const json::parse_error& e) {
        throw kdi_json_error(std::string("JSON parse error: ") + e.what());
    }
    try {
        return from_json_file(j);
    } catch (const json::exception& e) {
        throw kdi_json_error(std::string("KDI JSON schema error: ") + e.what());
    }
}

bool kdi_write_json_file(const kdi_file& file, const std::string& path) {
    std::ofstream out(path);
    if (!out) return false;
    try {
        kdi_write_json(file, out);
        return true;
    } catch (...) {
        return false;
    }
}

kdi_file kdi_read_json_file(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("kdi_read_json_file: cannot open '" + path + "'");
    return kdi_read_json(in);
}

} // namespace kdi

