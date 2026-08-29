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

#include "kdi_query.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <variant>

namespace kdi {
namespace {

std::string vis_str(kdi_visibility v) {
    return v == kdi_visibility::public_ ? "public" : "protected";
}

std::string aggregate_kind_str(kdi_aggregate_kind kind) {
    switch (kind) {
        case kdi_aggregate_kind::class_: return "class";
        case kdi_aggregate_kind::interface_: return "interface";
        case kdi_aggregate_kind::annotation_: return "annotation";
        case kdi_aggregate_kind::struct_: break;
    }
    return "struct";
}

std::string lower(std::string_view value) {
    std::string out(value);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

bool contains_ci(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) return true;
    return lower(haystack).find(lower(needle)) != std::string::npos;
}

bool row_matches(const kdi_symbol_row& row, std::string_view query) {
    return query.empty()
        || contains_ci(row.kind, query)
        || contains_ci(row.fq_name, query)
        || contains_ci(row.owner_fq_name, query)
        || contains_ci(row.name, query)
        || contains_ci(row.mangled_name, query)
        || contains_ci(row.signature, query);
}

std::string params_to_string(const std::vector<kdi_param>& params) {
    std::ostringstream out;
    out << "(";
    for (std::size_t i = 0; i < params.size(); ++i) {
        if (i) out << ", ";
        out << params[i].name;
        if (params[i].is_varargs) out << "...";
        out << ": " << kdi_type_to_string(params[i].type);
    }
    out << ")";
    return out.str();
}

std::string throws_to_string(const std::vector<kdi_type>& throws_spec) {
    if (throws_spec.empty()) return {};
    std::ostringstream out;
    out << " throws(";
    for (std::size_t i = 0; i < throws_spec.size(); ++i) {
        if (i) out << ", ";
        out << kdi_type_to_string(throws_spec[i]);
    }
    out << ")";
    return out.str();
}

std::string function_signature(const kdi_function& fn) {
    std::ostringstream out;
    if (fn.is_static) out << "static ";
    if (fn.is_operator) out << "operator ";
    out << fn.name << params_to_string(fn.params) << " : " << kdi_type_to_string(fn.return_type)
        << throws_to_string(fn.throws_spec);
    return out.str();
}

std::string method_signature(const kdi_method& method) {
    std::ostringstream out;
    if (method.is_static) out << "static ";
    if (method.is_virtual) out << "virtual ";
    if (method.is_abstract) out << "abstract ";
    if (method.is_final) out << "final ";
    if (method.is_const_member) out << "const ";
    if (method.is_operator) out << "operator ";
    out << method.name << params_to_string(method.params) << " : "
        << kdi_type_to_string(method.return_type) << throws_to_string(method.throws_spec);
    return out.str();
}

std::string constructor_signature(const kdi_constructor& ctor, const kdi_aggregate& owner) {
    std::ostringstream out;
    if (ctor.is_copy_constructor) out << "copy ";
    if (ctor.is_defaulted) out << "default ";
    if (ctor.is_deleted) out << "deleted ";
    out << owner.name << params_to_string(ctor.params);
    return out.str();
}

std::string destructor_signature(const kdi_destructor& dtor, const kdi_aggregate& owner) {
    std::ostringstream out;
    if (dtor.is_virtual) out << "virtual ";
    if (dtor.is_compiler_generated) out << "compiler-generated ";
    out << "~" << owner.name << "()";
    return out.str();
}

std::string alias_signature(const kdi_alias& alias) {
    if (alias.is_template && !alias.source.empty()) return alias.source;
    std::ostringstream out;
    out << (alias.is_strong ? "typedef " : "alias ") << alias.name << " : ";
    if (alias.target_type) out << kdi_type_to_string(*alias.target_type);
    else if (alias.target_fq_name) out << *alias.target_fq_name;
    else out << "?";
    return out.str();
}

void add_row(std::vector<kdi_symbol_row>& rows,
             std::string kind,
             std::string fq_name,
             std::string owner,
             std::string name,
             std::string mangled,
             std::string signature,
             std::string_view query)
{
    kdi_symbol_row row{
        std::move(kind),
        std::move(fq_name),
        std::move(owner),
        std::move(name),
        std::move(mangled),
        std::move(signature),
    };
    if (row_matches(row, query)) rows.push_back(std::move(row));
}

void collect_aggregate_members(const kdi_aggregate& agg,
                               std::vector<kdi_symbol_row>& rows,
                               std::string_view query)
{
    for (const auto& base : agg.bases) {
        std::ostringstream sig;
        sig << (base.is_virtual ? "virtual " : "") << vis_str(base.visibility)
            << " base " << base.fq_name << " offset=" << base.byte_offset;
        add_row(rows, "base", base.fq_name, agg.fq_name, base.fq_name, "", sig.str(), query);
    }
    for (const auto& field : agg.layout) {
        if (const auto* member = std::get_if<kdi_layout_member>(&field)) {
            std::ostringstream sig;
            sig << (member->is_const ? "const " : "") << member->name << ": "
                << kdi_type_to_string(member->type) << " @" << member->llvm_field_index;
            add_row(rows, "field", member->fq_name, agg.fq_name, member->name,
                    member->mangled_name, sig.str(), query);
        }
    }
    for (const auto& ctor : agg.constructors) {
        add_row(rows, "constructor", agg.fq_name + "::" + agg.name, agg.fq_name, agg.name,
                ctor.mangled_name, constructor_signature(ctor, agg), query);
        if (!ctor.mangled_name_c2.empty() && ctor.mangled_name_c2 != ctor.mangled_name) {
            add_row(rows, "constructor-c2", agg.fq_name + "::" + agg.name, agg.fq_name,
                    agg.name, ctor.mangled_name_c2, constructor_signature(ctor, agg), query);
        }
    }
    if (agg.destructor) {
        add_row(rows, "destructor", agg.fq_name + "::~" + agg.name, agg.fq_name,
                "~" + agg.name, agg.destructor->mangled_name,
                destructor_signature(*agg.destructor, agg), query);
        if (!agg.destructor->mangled_name_d2.empty()
            && agg.destructor->mangled_name_d2 != agg.destructor->mangled_name) {
            add_row(rows, "destructor-d2", agg.fq_name + "::~" + agg.name, agg.fq_name,
                    "~" + agg.name, agg.destructor->mangled_name_d2,
                    destructor_signature(*agg.destructor, agg), query);
        }
    }
    for (const auto& method : agg.methods) {
        add_row(rows, "method", method.fq_name, agg.fq_name, method.name,
                method.mangled_name, method_signature(method), query);
    }
    for (const auto& var : agg.static_vars) {
        add_row(rows, "static-variable", var.fq_name, agg.fq_name, var.name,
                var.mangled_name,
                std::string(var.is_const ? "const " : "") + var.name + ": " + kdi_type_to_string(var.type),
                query);
    }
    if (agg.vtable) {
        add_row(rows, "vtable", agg.fq_name, agg.fq_name, agg.name,
                agg.vtable->vtable_symbol, "vtable " + agg.name, query);
        add_row(rows, "rtti", agg.fq_name, agg.fq_name, agg.name,
                agg.vtable->rtti_symbol, "rtti " + agg.name, query);
    }
    for (const auto& alias : agg.aliases) {
        add_row(rows, alias.is_strong ? "typedef" : "alias", alias.fq_name, agg.fq_name,
                alias.name, "", alias_signature(alias), query);
    }
    for (const auto& nested : agg.nested) {
        add_row(rows, "nested-" + aggregate_kind_str(nested.kind), nested.fq_name, agg.fq_name,
                nested.name, nested.mangled_name, aggregate_kind_str(nested.kind) + " " + nested.name, query);
    }
    for (const auto& un : agg.nested_unions) {
        add_row(rows, "nested-union", un.fq_name, agg.fq_name, un.name,
                un.mangled_name, "union " + un.name, query);
    }
}

void collect_aggregate_recursive(const kdi_aggregate& agg,
                                 std::vector<kdi_symbol_row>& rows,
                                 std::string_view query);

void collect_namespace(const kdi_namespace& ns,
                       std::vector<kdi_symbol_row>& rows,
                       std::string_view query)
{
    if (!ns.fq_name.empty()) {
        add_row(rows, "namespace", ns.fq_name, "", ns.name, "", "namespace " + ns.fq_name, query);
    }
    for (const auto& child : ns.namespaces) collect_namespace(child, rows, query);
    for (const auto& agg : ns.aggregates) collect_aggregate_recursive(agg, rows, query);
    for (const auto& en : ns.enums) {
        add_row(rows, "enum", en.fq_name, ns.fq_name, en.name, en.object_table_symbol.value_or(""),
                "enum " + en.name, query);
    }
    for (const auto& alias : ns.aliases) {
        add_row(rows, alias.is_strong ? "typedef" : "alias", alias.fq_name, ns.fq_name,
                alias.name, "", alias_signature(alias), query);
    }
    for (const auto& un : ns.unions) {
        add_row(rows, "union", un.fq_name, ns.fq_name, un.name, un.mangled_name,
                "union " + un.name, query);
    }
    for (const auto& fn : ns.functions) {
        add_row(rows, "function", fn.fq_name, ns.fq_name, fn.name, fn.mangled_name,
                function_signature(fn), query);
    }
    for (const auto& var : ns.variables) {
        add_row(rows, "variable", var.fq_name, ns.fq_name, var.name, var.mangled_name,
                std::string(var.is_const ? "const " : "") + var.name + ": " + kdi_type_to_string(var.type),
                query);
    }
    for (const auto& tpl : ns.template_defs) {
        add_row(rows, "template", tpl.fq_name, ns.fq_name, tpl.name, "",
                tpl.visibility + " " + (tpl.is_generic ? "generic " : "template ") + tpl.entity_kind + " " + tpl.name,
                query);
    }
}

void collect_aggregate_recursive(const kdi_aggregate& agg,
                                 std::vector<kdi_symbol_row>& rows,
                                 std::string_view query)
{
    add_row(rows, aggregate_kind_str(agg.kind), agg.fq_name, agg.enclosing_fq_name, agg.name,
            agg.mangled_name, aggregate_kind_str(agg.kind) + " " + agg.name, query);
    collect_aggregate_members(agg, rows, query);
    for (const auto& nested : agg.nested) collect_aggregate_recursive(nested, rows, query);
    for (const auto& un : agg.nested_unions) {
        add_row(rows, "union", un.fq_name, agg.fq_name, un.name, un.mangled_name,
                "union " + un.name, query);
    }
}

void find_aggregates(const kdi_aggregate& agg,
                     std::string_view query,
                     std::vector<const kdi_aggregate*>& matches)
{
    if (query == agg.fq_name || query == agg.name || query == agg.mangled_name
        || contains_ci(agg.fq_name, query)) {
        matches.push_back(&agg);
    }
    for (const auto& nested : agg.nested) find_aggregates(nested, query, matches);
}

void find_aggregates(const kdi_namespace& ns,
                     std::string_view query,
                     std::vector<const kdi_aggregate*>& matches)
{
    for (const auto& agg : ns.aggregates) find_aggregates(agg, query, matches);
    for (const auto& child : ns.namespaces) find_aggregates(child, query, matches);
}

std::string escape_tsv(std::string value) {
    for (char& c : value) {
        if (c == '\t' || c == '\n' || c == '\r') c = ' ';
    }
    return value;
}

} // namespace

std::string kdi_type_to_string(const kdi_type& type) {
    return std::visit([](const auto& v) -> std::string {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, kdi_void_type>) return "void";
        if constexpr (std::is_same_v<T, kdi_bool_type>) return "bool";
        if constexpr (std::is_same_v<T, kdi_char_type>) return "char";
        if constexpr (std::is_same_v<T, kdi_int_type>) {
            if (v.bits == 8) return v.is_signed ? "byte" : "ubyte";
            if (v.bits == 16) return v.is_signed ? "short" : "ushort";
            if (v.bits == 32) return v.is_signed ? "int" : "uint";
            if (v.bits == 64) return v.is_signed ? "long" : "ulong";
            return std::string(v.is_signed ? "int" : "uint") + std::to_string(v.bits);
        }
        if constexpr (std::is_same_v<T, kdi_float_type>) return v.bits == 32 ? "float" : "double";
        if constexpr (std::is_same_v<T, kdi_ref_type>) return (v.inner ? kdi_type_to_string(*v.inner) : "?") + "&";
        if constexpr (std::is_same_v<T, kdi_ptr_type>) return (v.inner ? kdi_type_to_string(*v.inner) : "?") + "*";
        if constexpr (std::is_same_v<T, kdi_link_type>) return (v.inner ? kdi_type_to_string(*v.inner) : "?") + "+";
        if constexpr (std::is_same_v<T, kdi_view_type>) return (v.inner ? kdi_type_to_string(*v.inner) : "?") + "?";
        if constexpr (std::is_same_v<T, kdi_owner_type>) return (v.inner ? kdi_type_to_string(*v.inner) : "?") + "!";
        if constexpr (std::is_same_v<T, kdi_drain_type>) return (v.inner ? kdi_type_to_string(*v.inner) : "?") + "#";
        if constexpr (std::is_same_v<T, kdi_const_type>) return "const " + (v.inner ? kdi_type_to_string(*v.inner) : "?");
        if constexpr (std::is_same_v<T, kdi_array_type>) return (v.elem ? kdi_type_to_string(*v.elem) : "?") + "[]";
        if constexpr (std::is_same_v<T, kdi_sized_array_type>) return (v.elem ? kdi_type_to_string(*v.elem) : "?") + "[" + std::to_string(v.size) + "]";
        if constexpr (std::is_same_v<T, kdi_callable_type>) {
            std::ostringstream out;
            if (!v.member_of.empty()) out << v.member_of << "::";
            out << callable_addresser_symbol(v.addresser) << "(";
            for (std::size_t i = 0; i < v.params.size(); ++i) {
                if (i) out << ", ";
                out << (v.params[i] ? kdi_type_to_string(*v.params[i]) : "?");
            }
            out << ")";
            if (v.ret && !std::holds_alternative<kdi_void_type>(v.ret->value)) {
                out << " : " << kdi_type_to_string(*v.ret);
            }
            if (!v.throws.empty()) {
                out << " throws(";
                for (std::size_t i = 0; i < v.throws.size(); ++i) {
                    if (i) out << ", ";
                    out << (v.throws[i] ? kdi_type_to_string(*v.throws[i]) : "?");
                }
                out << ")";
            }
            return out.str();
        }
        if constexpr (std::is_same_v<T, kdi_aggregate_ref>) return v.fq_name;
        if constexpr (std::is_same_v<T, kdi_enum_ref>) return "enum " + v.fq_name;
        if constexpr (std::is_same_v<T, kdi_alias_ref>) return v.fq_name;
        if constexpr (std::is_same_v<T, kdi_template_param_ref>) return v.name;
        if constexpr (std::is_same_v<T, kdi_generic_ref_type>) {
            std::ostringstream out;
            out << v.name << "<";
            for (std::size_t i = 0; i < v.args.size(); ++i) {
                if (i) out << ", ";
                out << (v.args[i] ? kdi_type_to_string(*v.args[i]) : "?");
            }
            out << ">";
            return out.str();
        }
        return "?";
    }, type.value);
}

std::vector<kdi_symbol_row> kdi_list_symbols(const kdi_file& file,
                                             std::string_view query)
{
    std::vector<kdi_symbol_row> rows;
    collect_namespace(file.unit.root_ns, rows, query);
    return rows;
}

std::vector<kdi_symbol_row> kdi_list_aggregate_members(const kdi_file& file,
                                                       std::string_view aggregate)
{
    std::vector<const kdi_aggregate*> matches;
    find_aggregates(file.unit.root_ns, aggregate, matches);

    std::vector<kdi_symbol_row> rows;
    if (matches.size() != 1) return rows;
    collect_aggregate_members(*matches.front(), rows, {});
    return rows;
}

void kdi_write_symbol_rows_tsv(const std::vector<kdi_symbol_row>& rows,
                               std::ostream& out,
                               bool include_header)
{
    if (include_header) {
        out << "kind\tfq_name\towner_fq_name\tname\tmangled_name\tsignature\n";
    }
    for (const auto& row : rows) {
        out << escape_tsv(row.kind) << '\t'
            << escape_tsv(row.fq_name) << '\t'
            << escape_tsv(row.owner_fq_name) << '\t'
            << escape_tsv(row.name) << '\t'
            << escape_tsv(row.mangled_name) << '\t'
            << escape_tsv(row.signature) << '\n';
    }
}

} // namespace kdi
