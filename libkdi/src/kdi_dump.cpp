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

#include "kdi_dump.hpp"

#include <string>
#include <variant>

namespace kdi {

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

std::string indent(int depth) { return std::string(static_cast<size_t>(depth) * 2, ' '); }

std::string vis_str(kdi_visibility v) {
    return v == kdi_visibility::public_ ? "public" : "protected";
}

std::string type_str(const kdi_type& t) {
    return std::visit([](const auto& v) -> std::string {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, kdi_void_type>)   return "void";
        if constexpr (std::is_same_v<T, kdi_bool_type>)   return "bool";
        if constexpr (std::is_same_v<T, kdi_char_type>)   return "char";
        if constexpr (std::is_same_v<T, kdi_int_type>) {
            return (v.is_signed ? "" : "unsigned ") + std::string("int") + std::to_string(v.bits);
        }
        if constexpr (std::is_same_v<T, kdi_float_type>)  return "float" + std::to_string(v.bits);
        if constexpr (std::is_same_v<T, kdi_ref_type>)
            return "&" + (v.inner ? type_str(*v.inner) : "?");
        if constexpr (std::is_same_v<T, kdi_ptr_type>)
            return "*" + (v.inner ? type_str(*v.inner) : "?");
        if constexpr (std::is_same_v<T, kdi_link_type>)
            return "~" + (v.inner ? type_str(*v.inner) : "?");
        if constexpr (std::is_same_v<T, kdi_view_type>)
            return "^" + (v.inner ? type_str(*v.inner) : "?");
        if constexpr (std::is_same_v<T, kdi_drain_type>)
            return "#" + (v.inner ? type_str(*v.inner) : "?");
        if constexpr (std::is_same_v<T, kdi_const_type>)
            return "const " + (v.inner ? type_str(*v.inner) : "?");
        if constexpr (std::is_same_v<T, kdi_array_type>)
            return "[]" + (v.elem ? type_str(*v.elem) : "?");
        if constexpr (std::is_same_v<T, kdi_sized_array_type>)
            return "[" + std::to_string(v.size) + "]" + (v.elem ? type_str(*v.elem) : "?");
        if constexpr (std::is_same_v<T, kdi_fn_ref_type>) {
            std::string s = "fn(";
            for (size_t i = 0; i < v.params.size(); ++i) {
                if (i) s += ", ";
                s += (v.params[i] ? type_str(*v.params[i]) : "?");
            }
            return s + ") : " + (v.ret ? type_str(*v.ret) : "void");
        }
        if constexpr (std::is_same_v<T, kdi_aggregate_ref>)
            return v.fq_name;
        if constexpr (std::is_same_v<T, kdi_enum_ref>)
            return "enum " + v.fq_name;
        return "?";
    }, t.value);
}

std::string params_str(const std::vector<kdi_param>& params) {
    std::string s = "(";
    for (size_t i = 0; i < params.size(); ++i) {
        if (i) s += ", ";
        s += params[i].name + ": " + type_str(params[i].type);
    }
    return s + ")";
}

// ─────────────────────────────────────────────────────────────────────────────
// Forward declarations
// ─────────────────────────────────────────────────────────────────────────────

void dump_aggregate(const kdi_aggregate& agg, std::ostream& out, int depth);
void dump_namespace(const kdi_namespace& ns, std::ostream& out, int depth);

// ─────────────────────────────────────────────────────────────────────────────
// Dump functions
// ─────────────────────────────────────────────────────────────────────────────

void dump_layout(const std::vector<kdi_layout_field>& layout, std::ostream& out, int depth) {
    if (layout.empty()) return;
    out << indent(depth) << "// layout (" << layout.size() << " field(s)):\n";
    for (auto& f : layout) {
        std::visit([&](const auto& v) {
            using T = std::decay_t<decltype(v)>;
            out << indent(depth) << "  [" << v.llvm_field_index << "] ";
            if constexpr (std::is_same_v<T, kdi_layout_member>) {
                out << vis_str(v.visibility) << " " << type_str(v.type) << " "
                    << v.name << ";  // " << v.mangled_name;
            } else if constexpr (std::is_same_v<T, kdi_layout_vptr>) {
                out << "<vptr → " << v.vtable_symbol << ">";
            } else if constexpr (std::is_same_v<T, kdi_layout_vptr_secondary>) {
                out << "<vptr_secondary(" << v.base_fq_name << ") → " << v.vtable_symbol << ">";
            } else if constexpr (std::is_same_v<T, kdi_layout_base_subobject>) {
                out << "<base_subobject: " << v.base_fq_name << ">";
            } else if constexpr (std::is_same_v<T, kdi_layout_vbptr>) {
                out << "<vbptr: " << v.vbase_fq_name << ">";
            } else if constexpr (std::is_same_v<T, kdi_layout_vbase_subobject>) {
                out << "<vbase_subobject: " << v.vbase_fq_name << ">";
            } else if constexpr (std::is_same_v<T, kdi_layout_parent_ref>) {
                out << "<parent_ref: " << v.parent_fq_name << ">";
            } else if constexpr (std::is_same_v<T, kdi_layout_opaque_block>) {
                out << "<opaque_block: " << v.field_count << " field(s), "
                    << v.size_bits << " bits>";
            }
            out << "\n";
        }, f);
    }
}

void dump_vtable(const kdi_vtable& vt, std::ostream& out, int depth) {
    out << indent(depth) << "vtable " << vt.vtable_symbol
        << "  rtti=" << vt.rtti_symbol << " {\n";
    for (auto& s : vt.slots) {
        out << indent(depth + 1) << "[" << s.slot_index << "] "
            << s.introducing_func;
        if (s.is_abstract) out << " = 0";
        else if (!s.override_symbol.empty()) out << " → " << s.override_symbol;
        out << "\n";
    }
    for (auto& sec : vt.secondary) {
        out << indent(depth + 1) << "secondary(" << sec.base_fq_name
            << " offset=" << sec.base_offset << ") " << sec.vtable_symbol << "\n";
    }
    out << indent(depth) << "}\n";
}

void dump_aggregate(const kdi_aggregate& agg, std::ostream& out, int depth) {
    // kind + name
    const char* kw = "struct";
    if (agg.kind == kdi_aggregate_kind::class_)     kw = "class";
    if (agg.kind == kdi_aggregate_kind::interface_) kw = "interface";

    out << indent(depth) << vis_str(agg.visibility) << " " << kw << " " << agg.name;
    if (agg.is_abstract)    out << " abstract";
    if (agg.is_final)       out << " final";
    if (agg.is_const_struct)out << " const";

    // bases
    if (!agg.bases.empty()) {
        out << " : ";
        for (size_t i = 0; i < agg.bases.size(); ++i) {
            if (i) out << ", ";
            if (agg.bases[i].is_virtual) out << "virtual ";
            out << vis_str(agg.bases[i].visibility) << " " << agg.bases[i].fq_name;
        }
    }
    out << "  // " << agg.mangled_name << "\n";
    out << indent(depth) << "{\n";
    if (!agg.llvm_def.empty())
        out << indent(depth + 1) << "// llvm: " << agg.llvm_def << "\n";

    dump_layout(agg.layout, out, depth + 1);

    // vtable
    if (agg.vtable) dump_vtable(*agg.vtable, out, depth + 1);

    // constructors
    for (auto& c : agg.constructors) {
        out << indent(depth + 1) << vis_str(c.visibility) << " ";
        if (c.is_copy_constructor) out << "copy ";
        if (c.is_defaulted)        out << "default ";
        if (c.is_deleted)          out << "deleted ";
        out << "constructor" << params_str(c.params)
            << "  // C1=" << c.mangled_name
            << " C2=" << c.mangled_name_c2 << "\n";
        if (!c.llvm_def.empty())
            out << indent(depth + 2) << "// llvm: " << c.llvm_def << "\n";
    }
    // destructor
    if (agg.destructor) {
        auto& d = *agg.destructor;
        out << indent(depth + 1) << vis_str(d.visibility) << " ";
        if (d.is_virtual) out << "virtual ";
        out << "destructor  // D1=" << d.mangled_name << " D2=" << d.mangled_name_d2 << "\n";
        if (!d.llvm_def.empty())
            out << indent(depth + 2) << "// llvm: " << d.llvm_def << "\n";
    }
    // methods
    for (auto& m : agg.methods) {
        out << indent(depth + 1) << vis_str(m.visibility) << " ";
        if (m.is_static)       out << "static ";
        if (m.is_virtual)      out << "virtual ";
        if (m.is_abstract)     out << "abstract ";
        if (m.is_final)        out << "final ";
        if (m.is_const_member) out << "const ";
        out << m.name << params_str(m.params) << " : " << type_str(m.return_type);
        if (m.vtable_slot >= 0) out << "  // slot=" << m.vtable_slot;
        out << "  // " << m.mangled_name << "\n";
        if (!m.llvm_def.empty())
            out << indent(depth + 2) << "// llvm: " << m.llvm_def << "\n";
    }
    // static vars
    for (auto& v : agg.static_vars) {
        out << indent(depth + 1) << vis_str(v.visibility) << " static ";
        if (v.is_const) out << "const ";
        out << type_str(v.type) << " " << v.name << ";  // " << v.mangled_name << "\n";
    }
    // nested
    for (auto& n : agg.nested) dump_aggregate(n, out, depth + 1);

    out << indent(depth) << "}\n";
}

void dump_namespace(const kdi_namespace& ns, std::ostream& out, int depth) {
    if (!ns.name.empty()) {
        out << indent(depth) << "namespace " << ns.name << " {\n";
    }

    for (auto& agg : ns.aggregates) dump_aggregate(agg, out, depth + (ns.name.empty() ? 0 : 1));

    for (auto& e : ns.enums) {
        int d = depth + (ns.name.empty() ? 0 : 1);
        out << indent(d) << vis_str(e.visibility) << " enum " << e.name;
        if (e.base_fq_name.has_value())
            out << " : " << *e.base_fq_name;
        out << " [" << type_str(e.underlying_type) << "] {\n";
        for (auto& en : e.entries) {
            out << indent(d + 1) << en.name << " = " << en.value;
            if (en.is_default) out << " (default)";
            out << "\n";
        }
        out << indent(d) << "}\n";
    }

    for (auto& f : ns.functions) {
        int d = depth + (ns.name.empty() ? 0 : 1);
        out << indent(d) << vis_str(f.visibility) << " ";
        if (f.is_static) out << "static ";
        out << f.name << params_str(f.params) << " : " << type_str(f.return_type)
            << "  // " << f.mangled_name << "\n";
        if (!f.llvm_def.empty())
            out << indent(d + 1) << "// llvm: " << f.llvm_def << "\n";
    }

    for (auto& v : ns.variables) {
        int d = depth + (ns.name.empty() ? 0 : 1);
        out << indent(d) << vis_str(v.visibility) << " ";
        if (v.is_const) out << "const ";
        out << type_str(v.type) << " " << v.name << ";  // " << v.mangled_name << "\n";
    }

    for (auto& child : ns.namespaces) dump_namespace(child, out, depth + (ns.name.empty() ? 0 : 1));

    if (!ns.name.empty()) {
        out << indent(depth) << "} // namespace " << ns.name << "\n";
    }
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

void kdi_dump(const kdi_file& file, std::ostream& out) {
    auto& h = file.header;
    out << "// KDI schema " << h.schema_major << "." << h.schema_minor << "\n"
        << "// module:  " << h.module_name   << "\n"
        << "// lib:     " << h.lib_base      << "\n";
    if (!h.lib_path.empty())      out << "// path:    " << h.lib_path << "\n";
    if (!h.target_triple.empty()) out << "// target:  " << h.target_triple << "\n";
    if (!h.compiler_ver.empty())  out << "// compiler:" << h.compiler_ver << "\n";

    if (!file.types.aggregates.empty()) {
        out << "\n// Type table (" << file.types.aggregates.size() << " aggregate(s)):\n";
        for (auto& e : file.types.aggregates) {
            out << "//   " << e.fq_name << " => " << e.mangled_name << "\n";
        }
    }

    if (!file.types.enums.empty()) {
        out << "\n// Enum type table (" << file.types.enums.size() << " enum(s)):\n";
        for (auto& e : file.types.enums) {
            out << "//   " << e.fq_name << "\n";
        }
    }

    out << "\nmodule " << file.unit.name << ";\n\n";
    dump_namespace(file.unit.root_ns, out, 0);
}

} // namespace kdi

