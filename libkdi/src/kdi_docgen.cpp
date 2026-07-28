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

#include "kdi_docgen.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <functional>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <variant>
#include <vector>

namespace kdi {

namespace {

namespace fs = std::filesystem;

struct symbol_ref {
    std::string kind;
    std::string name;
    std::string scope;
    std::string type_desc;
    std::string link;
    std::string brief;
};

// Index of concrete template instantiations (aggregate/union fq_name -> template
// origin), populated once per doc-generation run so that any type reference to
// a compiler-synthesized instantiation (e.g. "k::Expected__unsigned_sint__...")
// can be rendered using the real generic syntax (e.g. "k::Expected<unsigned int32, ...>")
// instead of the mangled/synthesized name.
std::unordered_map<std::string, kdi_template_origin> g_instantiation_origins;

// Strip a leading "::" root-prefix, to match the KDI convention used by
// kdi_aggregate_ref::fq_name (always stored without the prefix).
std::string strip_root_prefix(const std::string& fq) {
    return (fq.size() >= 2 && fq[0] == ':' && fq[1] == ':') ? fq.substr(2) : fq;
}

void collect_instantiation_origins(const kdi_aggregate& agg) {
    if (agg.template_origin) {
        g_instantiation_origins[strip_root_prefix(agg.fq_name)] = *agg.template_origin;
    }
    for (auto& nested : agg.nested)
        collect_instantiation_origins(nested);
    for (auto& nested_union : agg.nested_unions) {
        if (nested_union.template_origin)
            g_instantiation_origins[strip_root_prefix(nested_union.fq_name)] = *nested_union.template_origin;
    }
}

void collect_instantiation_origins(const kdi_namespace& ns) {
    for (auto& agg : ns.aggregates)
        collect_instantiation_origins(agg);
    for (auto& u : ns.unions) {
        if (u.template_origin)
            g_instantiation_origins[strip_root_prefix(u.fq_name)] = *u.template_origin;
    }
    for (auto& child : ns.namespaces)
        collect_instantiation_origins(child);
}

std::string type_to_string(const kdi_type& t) {
    return std::visit([](const auto& v) -> std::string {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, kdi_void_type>) return "void";
        if constexpr (std::is_same_v<T, kdi_bool_type>) return "bool";
        if constexpr (std::is_same_v<T, kdi_char_type>) return "char";
        if constexpr (std::is_same_v<T, kdi_int_type>)
            return (v.is_signed ? "" : "unsigned ") + std::string("int") + std::to_string(v.bits);
        if constexpr (std::is_same_v<T, kdi_float_type>) return "float" + std::to_string(v.bits);
        if constexpr (std::is_same_v<T, kdi_ref_type>) return "&" + (v.inner ? type_to_string(*v.inner) : "?");
        if constexpr (std::is_same_v<T, kdi_ptr_type>) return "*" + (v.inner ? type_to_string(*v.inner) : "?");
        if constexpr (std::is_same_v<T, kdi_link_type>) return "+" + (v.inner ? type_to_string(*v.inner) : "?");
        if constexpr (std::is_same_v<T, kdi_view_type>) return "?" + (v.inner ? type_to_string(*v.inner) : "?");
        if constexpr (std::is_same_v<T, kdi_owner_type>) return "!" + (v.inner ? type_to_string(*v.inner) : "?");
        if constexpr (std::is_same_v<T, kdi_drain_type>) return "#" + (v.inner ? type_to_string(*v.inner) : "?");
        if constexpr (std::is_same_v<T, kdi_const_type>) return "const " + (v.inner ? type_to_string(*v.inner) : "?");
        if constexpr (std::is_same_v<T, kdi_array_type>) return "[]" + (v.elem ? type_to_string(*v.elem) : "?");
        if constexpr (std::is_same_v<T, kdi_sized_array_type>)
            return "[" + std::to_string(v.size) + "]" + (v.elem ? type_to_string(*v.elem) : "?");
        if constexpr (std::is_same_v<T, kdi_fn_ref_type>) {
            std::string s = "fn(";
            for (size_t i = 0; i < v.params.size(); ++i) {
                if (i) s += ", ";
                s += (v.params[i] ? type_to_string(*v.params[i]) : "?");
            }
            return s + ") : " + (v.ret ? type_to_string(*v.ret) : "void");
        }
        if constexpr (std::is_same_v<T, kdi_aggregate_ref>) {
            // If this reference targets a compiler-synthesized concrete template
            // instantiation, render it using its real generic arguments instead
            // of the synthesized/mangled fq_name.
            auto it = g_instantiation_origins.find(v.fq_name);
            if (it != g_instantiation_origins.end()) {
                const kdi_template_origin& origin = it->second;
                std::string s = strip_root_prefix(origin.base_fq_name) + "<";
                for (size_t i = 0; i < origin.args.size(); ++i) {
                    if (i) s += ", ";
                    const auto& arg = origin.args[i];
                    if (arg.type_arg) s += type_to_string(*arg.type_arg);
                    else if (arg.value_arg) s += *arg.value_arg;
                    else s += "?";
                }
                return s + ">";
            }
            return v.fq_name;
        }
        if constexpr (std::is_same_v<T, kdi_enum_ref>) return "enum " + v.fq_name;
        if constexpr (std::is_same_v<T, kdi_template_param_ref>) return v.name;
        if constexpr (std::is_same_v<T, kdi_generic_ref_type>) {
            std::string s = v.name + "<";
            for (size_t i = 0; i < v.args.size(); ++i) {
                if (i) s += ", ";
                s += (v.args[i] ? type_to_string(*v.args[i]) : "?");
            }
            return s + ">";
        }
        return "?";
    }, t.value);
}

std::string aggregate_kind_to_string(kdi_aggregate_kind kind) {
    switch (kind) {
    case kdi_aggregate_kind::struct_: return "struct";
    case kdi_aggregate_kind::class_: return "class";
    case kdi_aggregate_kind::interface_: return "interface";
    case kdi_aggregate_kind::annotation_: return "annotation";
    }
    return "aggregate";
}

std::string visibility_to_string(kdi_visibility visibility) {
    return visibility == kdi_visibility::public_ ? "public" : "protected";
}

/**
 * Translate an internal canonical operator function name (e.g. "__operator_eq_")
 * into its human-readable K declaration syntax (e.g. "operator =="), per the
 * canonical name table in doc/spec/language/functions/operators.md. Cast
 * operators ("__operator_cv_...") become the bare "operator" keyword, since the
 * cast target type is already rendered by make_signature() via the return type
 * (yielding "operator() : T", matching the spec's declaration syntax exactly).
 * Non-operator names are returned unchanged.
 */
std::string operator_display_name(const std::string& name) {
    static const std::string cv_prefix = "__operator_cv_";
    if (name.compare(0, cv_prefix.size(), cv_prefix) == 0)
        return "operator";

    static const std::unordered_map<std::string, std::string> symbols = {
        {"__operator_pl_", "operator +"},
        {"__operator_mi_", "operator -"},
        {"__operator_ml_", "operator *"},
        {"__operator_dv_", "operator /"},
        {"__operator_rm_", "operator %"},
        {"__operator_an_", "operator &"},
        {"__operator_or_", "operator |"},
        {"__operator_eo_", "operator ^"},
        {"__operator_co_", "operator ~"},
        {"__operator_ls_", "operator <<"},
        {"__operator_rs_", "operator >>"},
        {"__operator_aa_", "operator &&"},
        {"__operator_oo_", "operator ||"},
        {"__operator_nt_", "operator !"},
        {"__operator_eq_", "operator =="},
        {"__operator_ne_", "operator !="},
        {"__operator_lt_", "operator <"},
        {"__operator_gt_", "operator >"},
        {"__operator_le_", "operator <="},
        {"__operator_ge_", "operator >="},
        {"__operator_ss_", "operator <=>"},
        {"__operator_aS_", "operator ="},
        {"__operator_pL_", "operator +="},
        {"__operator_mI_", "operator -="},
        {"__operator_mL_", "operator *="},
        {"__operator_dV_", "operator /="},
        {"__operator_rM_", "operator %="},
        {"__operator_aN_", "operator &="},
        {"__operator_oR_", "operator |="},
        {"__operator_eO_", "operator ^="},
        {"__operator_lS_", "operator <<="},
        {"__operator_rS_", "operator >>="},
        {"__operator_pp_", "operator ++_"},
        {"__operator_mm_", "operator --_"},
        {"__operator_PP_", "operator _++"},
        {"__operator_MM_", "operator _--"},
        {"__operator_ix_", "operator []"},
    };
    auto it = symbols.find(name);
    return it != symbols.end() ? it->second : name;
}

std::string make_signature(const std::string& name,
                           const std::vector<kdi_param>& params,
                           const kdi_type* ret)
{
    std::ostringstream out;
    out << operator_display_name(name) << "(";
    for (size_t i = 0; i < params.size(); ++i) {
        if (i) out << ", ";
        out << params[i].name;
        if (params[i].is_varargs)
            out << "...";
        out << ": " << type_to_string(params[i].type);
    }
    out << ")";
    if (ret)
        out << " : " << type_to_string(*ret);
    return out.str();
}

std::string make_slug(std::string value) {
    for (char& ch : value) {
        if (std::isalnum(static_cast<unsigned char>(ch)))
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        else
            ch = '-';
    }
    while (!value.empty() && value.back() == '-')
        value.pop_back();
    return value.empty() ? "item" : value;
}

std::string trim_root_prefix(std::string value) {
    while (!value.empty() && value.front() == ':')
        value.erase(value.begin());
    return value;
}

std::string strip_prefix(const std::string& full, const std::string& prefix) {
    const std::string clean_full = trim_root_prefix(full);
    const std::string clean_prefix = trim_root_prefix(prefix);
    if (clean_prefix.empty())
        return clean_full;
    if (clean_full == clean_prefix)
        return {};
    if (clean_full.rfind(clean_prefix + "::", 0) == 0)
        return clean_full.substr(clean_prefix.size() + 2);
    return clean_full;
}

std::vector<std::string> split_scope(const std::string& fq_name) {
    std::vector<std::string> parts;
    std::string token;
    for (size_t i = 0; i < fq_name.size(); ++i) {
        if (i + 1 < fq_name.size() && fq_name[i] == ':' && fq_name[i + 1] == ':') {
            if (!token.empty()) {
                parts.push_back(token);
                token.clear();
            }
            ++i;
            continue;
        }
        token.push_back(fq_name[i]);
    }
    if (!token.empty())
        parts.push_back(token);
    return parts;
}

std::string parent_scope(const std::string& fq_name) {
    const auto pos = fq_name.rfind("::");
    if (pos == std::string::npos)
        return {};
    return fq_name.substr(0, pos);
}

bool write_file(const fs::path& path, const std::string& content, std::string* error_message) {
    std::ofstream out(path);
    if (!out) {
        if (error_message)
            *error_message = "cannot write file '" + path.string() + "'";
        return false;
    }
    out << content;
    return true;
}

std::string rel_link(const fs::path& from_dir, const fs::path& to_path) {
    return fs::relative(to_path, from_dir).generic_string();
}

std::string md_escape(std::string text) {
    std::string out;
    out.reserve(text.size());
    for (char ch : text) {
        switch (ch) {
        case '\\': out += "\\\\"; break;
        case '`': out += "\\`"; break;
        case '|': out += "\\|"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '\n': out += "<br/>"; break;
        default: out += ch; break;
        }
    }
    return out;
}

std::string code(const std::string& value) {
    return "`" + md_escape(value) + "`";
}

std::string compact_brief(std::string brief, size_t max_len = 96) {
    if (brief.empty())
        return {};

    std::string normalized;
    normalized.reserve(brief.size());
    bool previous_space = false;
    for (char ch : brief) {
        if (std::isspace(static_cast<unsigned char>(ch))) {
            if (!normalized.empty() && !previous_space)
                normalized.push_back(' ');
            previous_space = true;
        } else {
            normalized.push_back(ch);
            previous_space = false;
        }
    }

    while (!normalized.empty() && normalized.back() == ' ')
        normalized.pop_back();

    if (normalized.size() <= max_len)
        return normalized;

    if (max_len <= 3)
        return normalized.substr(0, max_len);

    return normalized.substr(0, max_len - 3) + "...";
}

std::string compact_doc_brief(const std::optional<kdi_doc_block>& doc) {
    if (!doc.has_value())
        return {};
    return compact_brief(doc->brief);
}

std::string compact_doc_brief(const std::optional<kdi_doc_function>& doc) {
    if (!doc.has_value())
        return {};
    return compact_brief(doc->brief);
}

void append_doc_block(std::ostringstream& out, const kdi_doc_block& doc) {
    if (!doc.brief.empty())
        out << "\n**Brief**\n\n" << doc.brief << "\n";
    if (!doc.description.empty())
        out << "\n**Description**\n\n" << doc.description << "\n";
}

void append_doc_block(std::ostringstream& out, const std::optional<kdi_doc_block>& doc) {
    if (!doc.has_value())
        return;
    append_doc_block(out, *doc);
}

void append_doc_function(std::ostringstream& out, const std::optional<kdi_doc_function>& doc) {
    if (!doc.has_value())
        return;
    append_doc_block(out, static_cast<const kdi_doc_block&>(*doc));
    if (!doc->params.empty()) {
        out << "\n**Parameters**\n\n";
        for (const auto& param : doc->params)
            out << "- `" << param.name << "`: " << param.description << "\n";
    }
    if (doc->returns.has_value())
        out << "\n**Returns**\n\n- " << *doc->returns << "\n";
    if (!doc->throws.empty()) {
        out << "\n**Throws**\n\n";
        for (const auto& t : doc->throws)
            out << "- `" << t.type_name << "`: " << t.description << "\n";
    }
}

void add_reference(std::vector<symbol_ref>& refs,
                   std::string kind,
                   std::string name,
                   std::string scope,
                   std::string type_desc,
                   std::string link,
                   std::string brief = {})
{
    refs.push_back({std::move(kind),
                    std::move(name),
                    std::move(scope),
                    std::move(type_desc),
                    std::move(link),
                    std::move(brief)});
}

// ─────────────────────────────────────────────────────────────────────────────
// Inheritance / hierarchy cross-reference index
// ─────────────────────────────────────────────────────────────────────────────
//
// Built once per doc-generation run (see collect_hierarchy_index()) so that
// base (extends/implements) and derived (known direct subclasses / known
// implementors) relationships can be rendered as a link whenever the target
// is part of the module currently being documented. A base/derived type
// defined in another module cannot be linked: kditool docgen processes one
// KDI file at a time and has no way to know where that other module's page
// will be written (a separate, independent invocation of the tool) -- such
// references degrade gracefully to a plain, unlinked fully-qualified name.
//
// Every K inheritance link is virtual, so a class transitively implements
// every interface reachable through any of its bases, however deep; this is
// exposed as a flattened "All implemented interfaces" list, in addition to
// the direct "Base types" list.
//
// A template's own base list (kdi_aggregate_signature::bases, see
// build_generic_template_aggregate_signature() in kdi_exporter.cpp) is raw K
// source text (e.g. "OrderedCollection<T>"), not a resolved fq_name, because
// KDI does not resolve template argument identity. hierarchy_key() extracts
// a matchable identifier from such a reference (dropping the generic
// argument list and any leading "::"); a template node is additionally
// registered under its bare short name (its most common in-source spelling)
// so that this raw form still resolves to the right page.

struct hierarchy_node {
    std::string              canonical_key;  ///< fq-based identity, used for de-duplication
    fs::path                 page_path;
    bool                     is_interface = false;
    std::string              display_name;   ///< short name (+ template params), shown in links
    std::vector<std::string> base_keys;      ///< keys (any registered form) of direct bases
    std::vector<std::string> self_keys;      ///< every key this node is registered under
};

std::unordered_map<std::string, hierarchy_node> g_hierarchy_nodes;
// Direct-base key -> canonical keys of nodes that directly derive from it.
std::unordered_map<std::string, std::vector<std::string>> g_direct_derived;

std::unordered_map<std::string, fs::path>    g_enum_pages;
std::unordered_map<std::string, std::string> g_enum_display;
std::unordered_map<std::string, std::vector<std::string>> g_enum_direct_derived;

std::unordered_map<std::string, fs::path>    g_union_pages;
std::unordered_map<std::string, std::string> g_union_display;
std::unordered_map<std::string, std::vector<std::string>> g_union_direct_derived;

/**
 * Normalise a base-type reference to a matchable identifier: strips a
 * trailing generic argument list ("<...>") and any leading "::" root prefix.
 */
std::string hierarchy_key(const std::string& raw) {
    const auto lt = raw.find('<');
    std::string ident = lt == std::string::npos ? raw : raw.substr(0, lt);
    while (!ident.empty() && std::isspace(static_cast<unsigned char>(ident.back())))
        ident.pop_back();
    return trim_root_prefix(ident);
}

void register_hierarchy_node(const std::string& canonical_key,
                             const std::vector<std::string>& alias_keys,
                             fs::path page_path,
                             bool is_interface,
                             const std::string& display_name,
                             const std::vector<std::string>& raw_bases)
{
    hierarchy_node node;
    node.canonical_key = canonical_key;
    node.page_path = std::move(page_path);
    node.is_interface = is_interface;
    node.display_name = display_name;
    node.self_keys.push_back(canonical_key);
    for (const auto& a : alias_keys)
        node.self_keys.push_back(a);

    for (const auto& raw : raw_bases) {
        std::string key = hierarchy_key(raw);
        // A base referencing a compiler-synthesized concrete template
        // instantiation (e.g. a regular class extending "Container<int>")
        // resolves to the owning template's page, not the (undocumented)
        // synthesized instantiation.
        auto inst_it = g_instantiation_origins.find(key);
        if (inst_it != g_instantiation_origins.end())
            key = hierarchy_key(inst_it->second.base_fq_name);
        node.base_keys.push_back(key);
        g_direct_derived[key].push_back(canonical_key);
    }

    g_hierarchy_nodes[canonical_key] = node;
    for (const auto& a : alias_keys)
        g_hierarchy_nodes.emplace(a, node);
}

/** Render a single hierarchy reference as a link (if locally resolvable) or plain code text. */
std::string render_hierarchy_ref(const std::string& raw, const fs::path& ns_dir) {
    std::string key = hierarchy_key(raw);
    auto inst_it = g_instantiation_origins.find(key);
    if (inst_it != g_instantiation_origins.end())
        key = hierarchy_key(inst_it->second.base_fq_name);
    auto node_it = g_hierarchy_nodes.find(key);
    if (node_it != g_hierarchy_nodes.end())
        return "[" + code(node_it->second.display_name) + "](" +
               rel_link(ns_dir, node_it->second.page_path) + ")";
    return code(trim_root_prefix(raw));
}

/** Render a hierarchy node already identified by its own key (derived/implementor lists). */
std::string render_hierarchy_node_ref(const std::string& key, const fs::path& ns_dir) {
    auto node_it = g_hierarchy_nodes.find(key);
    if (node_it == g_hierarchy_nodes.end())
        return code(key);
    return "[" + code(node_it->second.display_name) + "](" +
           rel_link(ns_dir, node_it->second.page_path) + ")";
}

/**
 * Render the "## Inheritance" section for a class/interface: direct bases,
 * the flattened transitive closure of implemented interfaces, module-local
 * known direct subclasses, and (interfaces only) module-local known
 * implementors (direct or indirect). Shared between regular aggregate pages
 * and template aggregate-signature pages.
 */
void write_aggregate_hierarchy_section(std::ostringstream& out,
                                       const kdi_aggregate& agg,
                                       const fs::path& ns_dir)
{
    if (agg.kind != kdi_aggregate_kind::class_ && agg.kind != kdi_aggregate_kind::interface_)
        return;

    const std::string self_key = hierarchy_key(agg.fq_name);

    out << "\n## Inheritance\n\n";

    out << "- **Base types:** ";
    if (agg.bases.empty()) {
        out << "*(none)*\n";
    } else {
        for (size_t i = 0; i < agg.bases.size(); ++i) {
            if (i) out << ", ";
            out << render_hierarchy_ref(agg.bases[i].fq_name, ns_dir);
        }
        out << "\n";
    }

    std::vector<std::string> all_interfaces;
    {
        std::unordered_set<std::string> visited{self_key};
        std::function<void(const std::string&)> walk = [&](const std::string& key) {
            auto it = g_hierarchy_nodes.find(key);
            if (it == g_hierarchy_nodes.end())
                return;
            for (const auto& base_key : it->second.base_keys) {
                if (!visited.insert(base_key).second)
                    continue;
                auto bit = g_hierarchy_nodes.find(base_key);
                if (bit != g_hierarchy_nodes.end() && bit->second.is_interface)
                    all_interfaces.push_back(bit->second.canonical_key);
                walk(base_key);
            }
        };
        walk(self_key);
    }
    std::sort(all_interfaces.begin(), all_interfaces.end());
    out << "- **All implemented interfaces:** ";
    if (all_interfaces.empty()) {
        out << "*(none)*\n";
    } else {
        for (size_t i = 0; i < all_interfaces.size(); ++i) {
            if (i) out << ", ";
            out << render_hierarchy_node_ref(all_interfaces[i], ns_dir);
        }
        out << "\n";
    }

    std::vector<std::string> direct_sub;
    {
        std::unordered_set<std::string> seen;
        auto it = g_hierarchy_nodes.find(self_key);
        if (it != g_hierarchy_nodes.end()) {
            for (const auto& k : it->second.self_keys) {
                auto dit = g_direct_derived.find(k);
                if (dit == g_direct_derived.end())
                    continue;
                for (const auto& d : dit->second)
                    if (seen.insert(d).second)
                        direct_sub.push_back(d);
            }
        }
    }
    std::sort(direct_sub.begin(), direct_sub.end());
    out << "- **Known direct subclasses (this module):** ";
    if (direct_sub.empty()) {
        out << "*(none)*\n";
    } else {
        for (size_t i = 0; i < direct_sub.size(); ++i) {
            if (i) out << ", ";
            out << render_hierarchy_node_ref(direct_sub[i], ns_dir);
        }
        out << "\n";
    }

    if (agg.kind == kdi_aggregate_kind::interface_) {
        std::vector<std::string> implementors;
        {
            std::unordered_set<std::string> visited;
            std::vector<std::string> queue;
            auto it = g_hierarchy_nodes.find(self_key);
            if (it != g_hierarchy_nodes.end())
                for (const auto& k : it->second.self_keys)
                    queue.push_back(k);
            while (!queue.empty()) {
                std::string cur = queue.back();
                queue.pop_back();
                auto dit = g_direct_derived.find(cur);
                if (dit == g_direct_derived.end())
                    continue;
                for (const auto& d : dit->second) {
                    if (!visited.insert(d).second)
                        continue;
                    implementors.push_back(d);
                    auto nit = g_hierarchy_nodes.find(d);
                    if (nit != g_hierarchy_nodes.end())
                        for (const auto& k2 : nit->second.self_keys)
                            queue.push_back(k2);
                }
            }
        }
        std::sort(implementors.begin(), implementors.end());
        out << "- **Known implementors, direct or indirect (this module):** ";
        if (implementors.empty()) {
            out << "*(none)*\n";
        } else {
            for (size_t i = 0; i < implementors.size(); ++i) {
                if (i) out << ", ";
                out << render_hierarchy_node_ref(implementors[i], ns_dir);
            }
            out << "\n";
        }
    }
}

/** Render the "## Inheritance" section for an enum: direct base enum (ascendant) and
 *  module-local direct derived enums (descendants) -- direct relationships only. */
void write_enum_hierarchy_section(std::ostringstream& out, const kdi_enum& en, const fs::path& ns_dir) {
    const std::string self_key = hierarchy_key(en.fq_name);

    out << "\n## Inheritance\n\n";

    out << "- **Base enum:** ";
    if (en.base_fq_name && !en.base_fq_name->empty()) {
        const std::string key = hierarchy_key(*en.base_fq_name);
        auto it = g_enum_pages.find(key);
        if (it != g_enum_pages.end())
            out << "[" << code(g_enum_display[key]) << "](" << rel_link(ns_dir, it->second) << ")\n";
        else
            out << code(trim_root_prefix(*en.base_fq_name)) << "\n";
    } else {
        out << "*(none)*\n";
    }

    out << "- **Derived enums (this module, direct only):** ";
    auto dit = g_enum_direct_derived.find(self_key);
    if (dit == g_enum_direct_derived.end() || dit->second.empty()) {
        out << "*(none)*\n";
    } else {
        std::vector<std::string> derived = dit->second;
        std::sort(derived.begin(), derived.end());
        for (size_t i = 0; i < derived.size(); ++i) {
            if (i) out << ", ";
            auto pit = g_enum_pages.find(derived[i]);
            out << "[" << code(g_enum_display[derived[i]]) << "]("
                << (pit != g_enum_pages.end() ? rel_link(ns_dir, pit->second) : std::string()) << ")";
        }
        out << "\n";
    }
}

/** Render the "## Inheritance" section for a union: direct base union (ascendant) and
 *  module-local direct derived unions (descendants) -- direct relationships only. */
void write_union_hierarchy_section(std::ostringstream& out, const kdi_union& un, const fs::path& ns_dir) {
    const std::string self_key = hierarchy_key(un.fq_name);

    out << "\n## Inheritance\n\n";

    out << "- **Base union:** ";
    if (!un.base_union_fq_name.empty()) {
        const std::string key = hierarchy_key(un.base_union_fq_name);
        auto it = g_union_pages.find(key);
        if (it != g_union_pages.end())
            out << "[" << code(g_union_display[key]) << "](" << rel_link(ns_dir, it->second) << ")\n";
        else
            out << code(trim_root_prefix(un.base_union_fq_name)) << "\n";
    } else {
        out << "*(none)*\n";
    }

    out << "- **Derived unions (this module, direct only):** ";
    auto dit = g_union_direct_derived.find(self_key);
    if (dit == g_union_direct_derived.end() || dit->second.empty()) {
        out << "*(none)*\n";
    } else {
        std::vector<std::string> derived = dit->second;
        std::sort(derived.begin(), derived.end());
        for (size_t i = 0; i < derived.size(); ++i) {
            if (i) out << ", ";
            auto pit = g_union_pages.find(derived[i]);
            out << "[" << code(g_union_display[derived[i]]) << "]("
                << (pit != g_union_pages.end() ? rel_link(ns_dir, pit->second) : std::string()) << ")";
        }
        out << "\n";
    }
}

bool write_union_page(const kdi_union& un,
                      const fs::path& file_path,
                      const fs::path& module_root,
                      const std::string& root_fq,
                      std::vector<symbol_ref>& refs,
                      std::string* error_message,
                      const std::string& file_name)
{
    std::ostringstream out;
    out << "# " << md_escape(un.name) << "\n\n";
    out << "## Overview\n\n";
    out << "| Property | Value |\n";
    out << "|---|---|\n";
    out << "| Kind | `union` |\n";
    out << "| Fully qualified name | " << code(un.fq_name) << " |\n";
    out << "| Visibility | " << code(visibility_to_string(un.visibility)) << " |\n";
    out << "\n[Back to namespace index](index.md)\n";
    append_doc_block(out, un.doc);

    write_union_hierarchy_section(out, un, file_path.parent_path());

    std::vector<kdi_union_alternative> alternatives = un.alternatives;
    std::sort(alternatives.begin(), alternatives.end(), [](const auto& a, const auto& b) {
        return a.name < b.name;
    });

    out << "\n## Alternatives\n\n";
    if (alternatives.empty()) {
        out << "- *(none)*\n";
    } else {
        for (size_t i = 0; i < alternatives.size(); ++i) {
            const auto& alt = alternatives[i];
            const std::string anchor = "alt-" + make_slug(alt.name) + "-" + std::to_string(i);
            out << "- [" << code(alt.name) << "](#" << anchor << "): " << code(type_to_string(alt.type)) << "\n";
        }
        out << "\n## Alternative Details\n";
        for (size_t i = 0; i < alternatives.size(); ++i) {
            const auto& alt = alternatives[i];
            const std::string anchor = "alt-" + make_slug(alt.name) + "-" + std::to_string(i);
            out << "\n### <a id=\"" << anchor << "\"></a>" << md_escape(alt.name) << "\n\n";
            out << "- Type: " << code(type_to_string(alt.type)) << "\n";
            out << "- Const: " << code(alt.is_const ? "true" : "false") << "\n";
        }
    }

    if (!write_file(file_path, out.str(), error_message))
        return false;

    const fs::path from = module_root;
    const std::string link = rel_link(from, file_path);
    const std::string scope = strip_prefix(parent_scope(un.fq_name), root_fq);
    add_reference(refs, "union", un.name, scope, "union", link, compact_doc_brief(un.doc));

    for (size_t i = 0; i < alternatives.size(); ++i) {
        const auto& alt = alternatives[i];
        add_reference(refs,
                      "union-alternative",
                      alt.name,
                      strip_prefix(un.fq_name, root_fq),
                      type_to_string(alt.type),
                      link + "#alt-" + make_slug(alt.name) + "-" + std::to_string(i));
    }

    (void)file_name;
    return true;
}

bool write_enum_page(const kdi_enum& en,
                     const fs::path& file_path,
                     const fs::path& module_root,
                     const std::string& root_fq,
                     std::vector<symbol_ref>& refs,
                     std::string* error_message)
{
    std::ostringstream out;
    out << "# " << md_escape(en.name) << "\n\n";
    out << "## Overview\n\n";
    out << "| Property | Value |\n";
    out << "|---|---|\n";
    out << "| Kind | `enum` |\n";
    out << "| Fully qualified name | " << code(en.fq_name) << " |\n";
    out << "| Visibility | " << code(visibility_to_string(en.visibility)) << " |\n";
    out << "| Underlying type | " << code(type_to_string(en.underlying_type)) << " |\n";
    out << "\n[Back to namespace index](index.md)\n";
    append_doc_block(out, en.doc);

    write_enum_hierarchy_section(out, en, file_path.parent_path());

    std::vector<kdi_enum_entry> entries = en.entries;
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
        return a.name < b.name;
    });

    out << "\n## Entries\n\n";
    if (entries.empty()) {
        out << "- *(none)*\n";
    } else {
        for (const auto& entry : entries)
            out << "- " << code(entry.name) << " = " << code(std::to_string(entry.value)) << "\n";
    }

    if (!write_file(file_path, out.str(), error_message))
        return false;

    const std::string link = rel_link(module_root, file_path);
    const std::string scope = strip_prefix(parent_scope(en.fq_name), root_fq);
    add_reference(refs, "enum", en.name, scope, "enum", link, compact_doc_brief(en.doc));
    return true;
}

std::string template_param_to_string(const kdi_template_param& p) {
    std::string s;
    if (p.kind == "value")
        s = (p.value_type ? type_to_string(*p.value_type) : "value") + " " + p.name;
    else
        s = p.kind + " " + p.name;
    if (p.constraint_type)
        s += " : " + type_to_string(*p.constraint_type);
    if (p.default_type)
        s += " = " + type_to_string(*p.default_type);
    else if (p.default_value)
        s += " = " + *p.default_value;
    return s;
}

std::string template_params_str(const std::vector<kdi_template_param>& params) {
    std::string s = "<";
    for (size_t i = 0; i < params.size(); ++i) {
        if (i) s += ", ";
        s += template_param_to_string(params[i]);
    }
    return s + ">";
}

/**
 * Build the "<P1, P2, ...>" suffix (bare parameter names only) appended to a
 * template's short or fully-qualified name wherever it is displayed, so a
 * generic declaration reads as e.g. "Expected<R, E>" rather than the bare
 * "Expected" — consistent with how a concrete instantiation would be named.
 * Returns an empty string for a non-generic (parameterless) template.
 */
std::string template_param_names_str(const std::vector<kdi_template_param>& params) {
    if (params.empty())
        return {};
    std::string s = "<";
    for (size_t i = 0; i < params.size(); ++i) {
        if (i) s += ", ";
        s += params[i].name;
    }
    return s + ">";
}

/**
 * Populate the hierarchy cross-reference index (g_hierarchy_nodes and
 * friends) for one aggregate and, recursively, its nested aggregates/unions.
 * Synthesized template instantiations are skipped: they have no doc page and
 * are not part of the module's public hierarchy graph.
 */
void collect_hierarchy_index(const kdi_aggregate& agg,
                             const fs::path& ns_dir,
                             const std::string& file_stem)
{
    if (agg.template_origin)
        return;

    std::vector<std::string> raw_bases;
    raw_bases.reserve(agg.bases.size());
    for (const auto& b : agg.bases)
        raw_bases.push_back(b.fq_name);

    register_hierarchy_node(hierarchy_key(agg.fq_name), {agg.name},
                            ns_dir / (file_stem + ".md"),
                            agg.kind == kdi_aggregate_kind::interface_,
                            agg.name, raw_bases);

    for (const auto& nested : agg.nested) {
        if (nested.template_origin)
            continue;
        collect_hierarchy_index(nested, ns_dir, file_stem + "." + nested.name);
    }
    for (const auto& u : agg.nested_unions) {
        if (u.template_origin)
            continue;
        const std::string ukey = hierarchy_key(u.fq_name);
        g_union_pages[ukey] = ns_dir / (file_stem + "." + u.name + ".md");
        g_union_display[ukey] = u.name;
        if (!u.base_union_fq_name.empty())
            g_union_direct_derived[hierarchy_key(u.base_union_fq_name)].push_back(ukey);
    }
}

/** Populate the hierarchy cross-reference index for one namespace and, recursively,
 *  its child namespaces. See collect_hierarchy_index(kdi_aggregate) above. */
void collect_hierarchy_index(const kdi_namespace& ns,
                             const fs::path& module_root,
                             const std::string& root_fq)
{
    const std::string rel_scope = strip_prefix(ns.fq_name, root_fq);
    fs::path ns_dir = module_root;
    if (!rel_scope.empty())
        for (const auto& part : split_scope(rel_scope))
            ns_dir /= part;

    for (const auto& agg : ns.aggregates)
        collect_hierarchy_index(agg, ns_dir, agg.name);

    for (const auto& en : ns.enums) {
        const std::string key = hierarchy_key(en.fq_name);
        g_enum_pages[key] = ns_dir / (en.name + ".md");
        g_enum_display[key] = en.name;
        if (en.base_fq_name && !en.base_fq_name->empty())
            g_enum_direct_derived[hierarchy_key(*en.base_fq_name)].push_back(key);
    }

    for (const auto& un : ns.unions) {
        if (un.template_origin)
            continue;
        const std::string key = hierarchy_key(un.fq_name);
        g_union_pages[key] = ns_dir / (un.name + ".md");
        g_union_display[key] = un.name;
        if (!un.base_union_fq_name.empty())
            g_union_direct_derived[hierarchy_key(un.base_union_fq_name)].push_back(key);
    }

    for (const auto& td : ns.template_defs) {
        if (td.entity_kind == "function" || !td.aggregate_signature)
            continue;
        std::vector<std::string> raw_bases;
        raw_bases.reserve(td.aggregate_signature->bases.size());
        for (const auto& b : td.aggregate_signature->bases)
            raw_bases.push_back(b.fq_name);
        register_hierarchy_node(hierarchy_key(td.fq_name), {td.name},
                                ns_dir / (td.name + ".md"),
                                td.entity_kind == "interface",
                                td.name + template_param_names_str(td.params),
                                raw_bases);
    }

    for (const auto& child : ns.namespaces)
        collect_hierarchy_index(child, module_root, root_fq);
}

/**
 * Render the structured body of an aggregate (Member Variables, Members,
 * Nested Types, and the detailed sections) into `out`.
 *
 * Shared between regular aggregate pages and template aggregate pages (whose
 * `aggregate_signature` is a full-fledged kdi_aggregate) so that a template
 * class documents its fields/constructors/methods exactly like its
 * non-template equivalents, instead of a raw source dump.
 */
void write_aggregate_body(std::ostringstream& out,
                          const kdi_aggregate& agg,
                          const std::string& qualified_simple_name,
                          const fs::path& ns_dir)
{
    write_aggregate_hierarchy_section(out, agg, ns_dir);

    std::vector<kdi_layout_member> fields;
    for (const auto& field : agg.layout) {
        if (auto member = std::get_if<kdi_layout_member>(&field))
            fields.push_back(*member);
    }
    std::sort(fields.begin(), fields.end(), [](const auto& a, const auto& b) {
        return a.name < b.name;
    });

    std::vector<kdi_variable> static_vars = agg.static_vars;
    std::sort(static_vars.begin(), static_vars.end(), [](const auto& a, const auto& b) {
        return a.name < b.name;
    });

    std::vector<kdi_method> methods = agg.methods;
    methods.erase(std::remove_if(methods.begin(), methods.end(), [](const auto& m) {
        return m.template_origin.has_value();
    }), methods.end());
    std::sort(methods.begin(), methods.end(), [](const auto& a, const auto& b) {
        if (a.name != b.name) return a.name < b.name;
        return a.mangled_name < b.mangled_name;
    });

    std::vector<kdi_constructor> constructors = agg.constructors;
    std::sort(constructors.begin(), constructors.end(), [](const auto& a, const auto& b) {
        if (a.params.size() != b.params.size()) return a.params.size() < b.params.size();
        return a.mangled_name < b.mangled_name;
    });

    out << "\n## Member Variables\n\n";
    if (fields.empty() && static_vars.empty()) {
        out << "- *(none)*\n";
    } else {
        for (size_t i = 0; i < fields.size(); ++i) {
            const auto& field = fields[i];
            const std::string anchor = "field-" + make_slug(field.name) + "-" + std::to_string(i);
            out << "- [" << code(field.name) << "](#" << anchor << "): " << code(type_to_string(field.type)) << "\n";
        }
        for (size_t i = 0; i < static_vars.size(); ++i) {
            const auto& var = static_vars[i];
            const std::string anchor = "static-var-" + make_slug(var.name) + "-" + std::to_string(i);
            out << "- [" << code(var.name) << "](#" << anchor << "): " << code(type_to_string(var.type)) << " (static)";
            const std::string brief = compact_doc_brief(var.doc);
            if (!brief.empty())
                out << " - " << md_escape(brief);
            out << "\n";
        }
    }

    out << "\n## Members\n\n";
    if (constructors.empty() && methods.empty() && !agg.destructor.has_value()) {
        out << "- *(none)*\n";
    } else {
        for (size_t i = 0; i < constructors.size(); ++i) {
            const auto& ctor = constructors[i];
            const std::string anchor = "ctor-" + std::to_string(i);
            out << "- [" << code(make_signature(agg.name, ctor.params, nullptr)) << "](#" << anchor << ")";
            const std::string brief = compact_doc_brief(ctor.doc);
            if (!brief.empty())
                out << " - " << md_escape(brief);
            out << "\n";
        }
        if (agg.destructor.has_value()) {
            out << "- [" << code("~" + agg.name + "()") << "](#dtor)";
            const std::string brief = compact_doc_brief(agg.destructor->doc);
            if (!brief.empty())
                out << " - " << md_escape(brief);
            out << "\n";
        }
        for (size_t i = 0; i < methods.size(); ++i) {
            const auto& method = methods[i];
            const std::string anchor = "method-" + make_slug(method.name) + "-" + std::to_string(i);
            out << "- [" << code(make_signature(method.name, method.params, &method.return_type))
                << "](#" << anchor << ")";
            const std::string brief = compact_doc_brief(method.doc);
            if (!brief.empty())
                out << " - " << md_escape(brief);
            out << "\n";
        }
    }

    out << "\n## Nested Types\n\n";
    struct nested_row { std::string name; std::string link; std::string brief; };
    std::vector<nested_row> nested_rows;
    for (const auto& nested : agg.nested) {
        if (nested.template_origin.has_value())
            continue; // Skip synthesized template instantiations - not useful in docs.
        nested_rows.push_back({nested.name,
                               qualified_simple_name + "." + nested.name + ".md",
                               compact_doc_brief(nested.doc)});
    }
    for (const auto& nested : agg.nested_unions) {
        if (nested.template_origin.has_value())
            continue; // Skip synthesized template instantiations - not useful in docs.
        nested_rows.push_back({nested.name,
                               qualified_simple_name + "." + nested.name + ".md",
                               compact_doc_brief(nested.doc)});
    }
    std::sort(nested_rows.begin(), nested_rows.end(), [](const auto& a, const auto& b) {
        return a.name < b.name;
    });

    if (nested_rows.empty()) {
        out << "- *(none)*\n";
    } else {
        for (const auto& nested : nested_rows) {
            out << "- [" << nested.name << "](" << nested.link << ")";
            if (!nested.brief.empty())
                out << " - " << md_escape(nested.brief);
            out << "\n";
        }
    }

    out << "\n## Member Variable Details\n";
    for (size_t i = 0; i < fields.size(); ++i) {
        const auto& field = fields[i];
        const std::string anchor = "field-" + make_slug(field.name) + "-" + std::to_string(i);
            out << "\n### <a id=\"" << anchor << "\"></a>" << md_escape(field.name) << "\n\n";
            out << "- Type: " << code(type_to_string(field.type)) << "\n";
            out << "- Visibility: " << code(visibility_to_string(field.visibility)) << "\n";
            out << "- Const: " << code(field.is_const ? "true" : "false") << "\n";
    }
    for (size_t i = 0; i < static_vars.size(); ++i) {
        const auto& var = static_vars[i];
        const std::string anchor = "static-var-" + make_slug(var.name) + "-" + std::to_string(i);
        out << "\n### <a id=\"" << anchor << "\"></a>" << md_escape(var.name) << "\n\n";
        out << "- Type: " << code(type_to_string(var.type)) << "\n";
        out << "- Static: `true`\n";
        append_doc_block(out, var.doc);
    }

    out << "\n## Member Details\n";
    for (size_t i = 0; i < constructors.size(); ++i) {
        const auto& ctor = constructors[i];
        const std::string anchor = "ctor-" + std::to_string(i);
        out << "\n### <a id=\"" << anchor << "\"></a>" << code(make_signature(agg.name, ctor.params, nullptr)) << "\n\n";
        out << "- Visibility: " << code(visibility_to_string(ctor.visibility)) << "\n";
        append_doc_function(out, ctor.doc);
    }
    if (agg.destructor.has_value()) {
        out << "\n### <a id=\"dtor\"></a>" << code("~" + agg.name + "()") << "\n\n";
        out << "- Visibility: " << code(visibility_to_string(agg.destructor->visibility)) << "\n";
        append_doc_function(out, agg.destructor->doc);
    }
    for (size_t i = 0; i < methods.size(); ++i) {
        const auto& method = methods[i];
        const std::string anchor = "method-" + make_slug(method.name) + "-" + std::to_string(i);
        out << "\n### <a id=\"" << anchor << "\"></a>"
            << code(make_signature(method.name, method.params, &method.return_type)) << "\n\n";
        out << "- Visibility: " << code(visibility_to_string(method.visibility)) << "\n";
        out << "- Static: `" << (method.is_static ? "true" : "false") << "`\n";
        append_doc_function(out, method.doc);
    }
}

/**
 * Emit symbol-reference entries (for typed-references.md) for an aggregate's
 * fields/static variables/constructors/destructor/methods. Shared between
 * regular aggregate pages and template aggregate pages.
 */
void add_aggregate_member_refs(std::vector<symbol_ref>& refs,
                               const kdi_aggregate& agg,
                               const std::string& type_link,
                               const std::string& fq_scope)
{
    std::vector<kdi_layout_member> fields;
    for (const auto& field : agg.layout) {
        if (auto member = std::get_if<kdi_layout_member>(&field))
            fields.push_back(*member);
    }
    std::sort(fields.begin(), fields.end(), [](const auto& a, const auto& b) {
        return a.name < b.name;
    });
    std::vector<kdi_variable> static_vars = agg.static_vars;
    std::sort(static_vars.begin(), static_vars.end(), [](const auto& a, const auto& b) {
        return a.name < b.name;
    });
    std::vector<kdi_method> methods = agg.methods;
    methods.erase(std::remove_if(methods.begin(), methods.end(), [](const auto& m) {
        return m.template_origin.has_value();
    }), methods.end());
    std::sort(methods.begin(), methods.end(), [](const auto& a, const auto& b) {
        if (a.name != b.name) return a.name < b.name;
        return a.mangled_name < b.mangled_name;
    });
    std::vector<kdi_constructor> constructors = agg.constructors;
    std::sort(constructors.begin(), constructors.end(), [](const auto& a, const auto& b) {
        if (a.params.size() != b.params.size()) return a.params.size() < b.params.size();
        return a.mangled_name < b.mangled_name;
    });

    for (size_t i = 0; i < fields.size(); ++i) {
        const auto& field = fields[i];
        add_reference(refs,
                      "field",
                      field.name,
                      fq_scope,
                      type_to_string(field.type),
                      type_link + "#field-" + make_slug(field.name) + "-" + std::to_string(i));
    }
    for (size_t i = 0; i < static_vars.size(); ++i) {
        const auto& var = static_vars[i];
        add_reference(refs,
                      "static-variable",
                      var.name,
                      fq_scope,
                      type_to_string(var.type),
                      type_link + "#static-var-" + make_slug(var.name) + "-" + std::to_string(i),
                      compact_doc_brief(var.doc));
    }
    for (size_t i = 0; i < methods.size(); ++i) {
        const auto& method = methods[i];
        add_reference(refs,
                      "method",
                      operator_display_name(method.name),
                      fq_scope,
                      make_signature(method.name, method.params, &method.return_type),
                      type_link + "#method-" + make_slug(method.name) + "-" + std::to_string(i),
                      compact_doc_brief(method.doc));
    }
    for (size_t i = 0; i < constructors.size(); ++i) {
        add_reference(refs,
                      "constructor",
                      agg.name,
                      fq_scope,
                      make_signature(agg.name, constructors[i].params, nullptr),
                      type_link + "#ctor-" + std::to_string(i),
                      compact_doc_brief(constructors[i].doc));
    }
    if (agg.destructor.has_value()) {
        add_reference(refs,
                      "destructor",
                      "~" + agg.name,
                      fq_scope,
                      "destructor",
                      type_link + "#dtor",
                      compact_doc_brief(agg.destructor->doc));
    }
}

bool write_template_def_page(const kdi_template_def& def,
                             const fs::path& file_path,
                             const fs::path& module_root,
                             const std::string& root_fq,
                             std::vector<symbol_ref>& refs,
                             std::string* error_message)
{
    std::ostringstream out;
    const std::string param_suffix = template_param_names_str(def.params);
    out << "# " << md_escape(def.name + param_suffix) << "\n\n";
    out << "## Overview\n\n";
    out << "| Property | Value |\n";
    out << "|---|---|\n";
    out << "| Kind | " << code("template " + def.entity_kind) << " |\n";
    out << "| Fully qualified name | " << code(def.fq_name + param_suffix) << " |\n";
    out << "| Visibility | " << code(def.visibility) << " |\n";
    out << "| Generic | " << code(def.is_generic ? "true" : "false") << " |\n";
    if (!def.origin_module.empty())
        out << "| Origin module | " << code(def.origin_module) << " |\n";
    out << "\n[Back to namespace index](index.md)\n";

    out << "\n## Template Parameters\n\n";
    if (def.params.empty()) {
        out << "- *(none)*\n";
    } else {
        out << "| Name | Kind | Constraint / Value type | Default |\n";
        out << "|---|---|---|---|\n";
        for (const auto& p : def.params) {
            std::string constraint;
            if (p.constraint_type) constraint = type_to_string(*p.constraint_type);
            else if (p.value_type) constraint = type_to_string(*p.value_type);
            std::string default_val;
            if (p.default_type) default_val = type_to_string(*p.default_type);
            else if (p.default_value) default_val = *p.default_value;
            out << "| " << code(p.name) << " | " << code(p.kind) << " | "
                << (constraint.empty() ? "-" : code(constraint)) << " | "
                << (default_val.empty() ? "-" : code(default_val)) << " |\n";
        }
    }

    // Structured rendering: an aggregate-kind template documents its
    // fields/constructors/methods with the exact same sections as a regular
    // (non-template) aggregate page — template-parameter-dependent types are
    // tagged distinctly (kdi_template_param_ref / kdi_generic_ref_type)
    // rather than collapsed into an opaque source dump. A function-kind
    // template gets an equivalent Signature/Parameters/Return type rendering.
    if (def.aggregate_signature) {
        write_aggregate_body(out, *def.aggregate_signature, def.name, file_path.parent_path());
    } else if (def.function_signature) {
        out << "\n## Signature\n\n";
        out << "- " << code(make_signature(def.function_signature->name,
                                           def.function_signature->params,
                                           &def.function_signature->return_type)) << "\n";
        out << "\n## Parameters\n\n";
        if (def.function_signature->params.empty()) {
            out << "- *(none)*\n";
        } else {
            out << "| Name | Type |\n";
            out << "|---|---|\n";
            for (const auto& p : def.function_signature->params)
                out << "| " << code(p.name) << " | " << code(type_to_string(p.type)) << " |\n";
        }
        out << "\n## Return Type\n\n";
        out << code(type_to_string(def.function_signature->return_type)) << "\n";
        append_doc_function(out, def.function_signature->doc);
    }

    // The raw K declaration source is kept as a supplementary reference
    // (default member initializers, annotations, and other syntax not
    // captured by the structured signature above are only visible there).
    // Generic templates are signature-only and have no re-emittable source.
    if (!def.source.empty()) {
        out << "\n## Declaration Source\n\n";
        out << "```k\n" << def.source << "\n```\n";
    } else if (!def.aggregate_signature && !def.function_signature) {
        out << "\n*No declaration source or signature available for this template.*\n";
    }
    if (!write_file(file_path, out.str(), error_message))
        return false;

    const std::string link = rel_link(module_root, file_path);
    const std::string scope = strip_prefix(parent_scope(def.fq_name), root_fq);
    add_reference(refs,
                  "template " + def.entity_kind,
                  def.name + param_suffix,
                  scope,
                  "template" + template_params_str(def.params),
                  link);

    if (def.aggregate_signature) {
        add_aggregate_member_refs(refs, *def.aggregate_signature, link,
                                  strip_prefix(def.fq_name, root_fq));
    }

    return true;
}

bool write_aggregate_page(const kdi_aggregate& agg,
                          const fs::path& file_path,
                          const fs::path& ns_dir,
                          const fs::path& module_root,
                          const std::string& root_fq,
                          const std::string& qualified_simple_name,
                          std::vector<symbol_ref>& refs,
                          std::string* error_message);

bool write_aggregate_nested(const kdi_aggregate& agg,
                            const fs::path& ns_dir,
                            const fs::path& module_root,
                            const std::string& root_fq,
                            const std::string& name_prefix,
                            std::vector<symbol_ref>& refs,
                            std::string* error_message)
{
    const std::string file_stem = name_prefix.empty() ? agg.name : name_prefix + "." + agg.name;
    const fs::path file_path = ns_dir / (file_stem + ".md");
    if (!write_aggregate_page(agg,
                              file_path,
                              ns_dir,
                              module_root,
                              root_fq,
                              file_stem,
                              refs,
                              error_message))
        return false;

    for (const auto& nested : agg.nested) {
        if (nested.template_origin.has_value())
            continue; // Skip synthesized template instantiations - not useful in docs.
        if (!write_aggregate_nested(nested,
                                    ns_dir,
                                    module_root,
                                    root_fq,
                                    file_stem,
                                    refs,
                                    error_message))
            return false;
    }
    for (const auto& un : agg.nested_unions) {
        if (un.template_origin.has_value())
            continue; // Skip synthesized template instantiations - not useful in docs.
        const std::string nested_file = file_stem + "." + un.name;
        const fs::path un_path = ns_dir / (nested_file + ".md");
        if (!write_union_page(un,
                              un_path,
                              module_root,
                              root_fq,
                              refs,
                              error_message,
                              nested_file))
            return false;
    }

    return true;
}

bool write_aggregate_page(const kdi_aggregate& agg,
                          const fs::path& file_path,
                          const fs::path& ns_dir,
                          const fs::path& module_root,
                          const std::string& root_fq,
                          const std::string& qualified_simple_name,
                          std::vector<symbol_ref>& refs,
                          std::string* error_message)
{
    std::ostringstream out;
    out << "# " << md_escape(agg.name) << "\n\n";
    out << "## Overview\n\n";
    out << "| Property | Value |\n";
    out << "|---|---|\n";
    out << "| Kind | " << code(aggregate_kind_to_string(agg.kind)) << " |\n";
    out << "| Fully qualified name | " << code(agg.fq_name) << " |\n";
    out << "| Visibility | " << code(visibility_to_string(agg.visibility)) << " |\n";
    out << "| Abstract | " << code(agg.is_abstract ? "true" : "false") << " |\n";
    out << "| Final | " << code(agg.is_final ? "true" : "false") << " |\n";
    out << "\n[Back to namespace index](index.md)\n";
    append_doc_block(out, agg.doc);

    write_aggregate_body(out, agg, qualified_simple_name, ns_dir);

    if (!write_file(file_path, out.str(), error_message))
        return false;

    const std::string type_link = rel_link(module_root, file_path);
    const std::string type_scope = strip_prefix(parent_scope(agg.fq_name), root_fq);
    add_reference(refs,
                  aggregate_kind_to_string(agg.kind),
                  agg.name,
                  type_scope,
                  aggregate_kind_to_string(agg.kind),
                  type_link,
                  compact_doc_brief(agg.doc));

    add_aggregate_member_refs(refs, agg, type_link, strip_prefix(agg.fq_name, root_fq));

    return true;
}

std::string namespace_display_name(const kdi_namespace& ns, const std::string& root_fq) {
    if (ns.fq_name.empty())
        return "<root>";
    const std::string rel = strip_prefix(ns.fq_name, root_fq);
    return rel.empty() ? root_fq : rel;
}

bool write_namespace_tree(const kdi_namespace& ns,
                          const fs::path& module_root,
                          const std::string& root_fq,
                          std::vector<symbol_ref>& refs,
                          std::string* error_message,
                          bool with_module_header,
                          const kdi_file* file)
{
    const std::string rel_scope = strip_prefix(ns.fq_name, root_fq);
    fs::path ns_dir = module_root;
    if (!rel_scope.empty()) {
        for (const auto& part : split_scope(rel_scope))
            ns_dir /= part;
    }

    std::error_code ec;
    fs::create_directories(ns_dir, ec);
    if (ec) {
        if (error_message)
            *error_message = "cannot create directory '" + ns_dir.string() + "': " + ec.message();
        return false;
    }

    std::vector<kdi_namespace> child_ns = ns.namespaces;
    // Defensive guard against a pre-existing KDI export quirk: an auto-imported
    // module (e.g. the implicit `import k;`) can surface as an inert, fully-empty
    // "mirror" child namespace whose fq_name fails to properly extend the parent's
    // (collapsing to the parent's own scope). Writing such a child would resolve to
    // the very same directory as the current namespace and silently overwrite its
    // freshly written index page. These mirrors carry no real content (they only
    // exist to shadow imported symbols resolved elsewhere), so they are safely skipped.
    child_ns.erase(std::remove_if(child_ns.begin(), child_ns.end(), [&](const kdi_namespace& c) {
        return strip_prefix(c.fq_name, root_fq).empty();
    }), child_ns.end());
    std::sort(child_ns.begin(), child_ns.end(), [](const auto& a, const auto& b) {
        return a.name < b.name;
    });

    std::vector<kdi_aggregate> aggregates = ns.aggregates;
    aggregates.erase(std::remove_if(aggregates.begin(), aggregates.end(), [](const auto& a) {
        return a.template_origin.has_value();
    }), aggregates.end());
    std::sort(aggregates.begin(), aggregates.end(), [](const auto& a, const auto& b) {
        return a.name < b.name;
    });

    std::vector<kdi_enum> enums = ns.enums;
    std::sort(enums.begin(), enums.end(), [](const auto& a, const auto& b) {
        return a.name < b.name;
    });

    std::vector<kdi_union> unions = ns.unions;
    unions.erase(std::remove_if(unions.begin(), unions.end(), [](const auto& u) {
        return u.template_origin.has_value();
    }), unions.end());
    std::sort(unions.begin(), unions.end(), [](const auto& a, const auto& b) {
        return a.name < b.name;
    });

    std::vector<kdi_function> functions = ns.functions;
    functions.erase(std::remove_if(functions.begin(), functions.end(), [](const auto& f) {
        return f.template_origin.has_value();
    }), functions.end());
    std::sort(functions.begin(), functions.end(), [](const auto& a, const auto& b) {
        if (a.name != b.name) return a.name < b.name;
        return a.mangled_name < b.mangled_name;
    });

    std::vector<kdi_variable> variables = ns.variables;
    std::sort(variables.begin(), variables.end(), [](const auto& a, const auto& b) {
        return a.name < b.name;
    });

    // Collect names of template instantiations at this scope, so that member
    // templates re-walked from within an instantiation's cloned body (e.g. a
    // generic method still nested under `UniSlot__byte`) can be excluded below:
    // they are pure synthesis duplicates of the method already documented on
    // the original template declaration's page.
    std::set<std::string> instantiation_names;
    for (const auto& a : ns.aggregates)
        if (a.template_origin) instantiation_names.insert(a.name);
    for (const auto& u : ns.unions)
        if (u.template_origin) instantiation_names.insert(u.name);

    std::vector<kdi_template_def> template_defs = ns.template_defs;
    template_defs.erase(std::remove_if(template_defs.begin(), template_defs.end(), [&](const auto& td) {
        const std::string parent = parent_scope(td.fq_name);
        const auto pos = parent.rfind("::");
        const std::string parent_short = pos == std::string::npos ? parent : parent.substr(pos + 2);
        return instantiation_names.count(parent_short) > 0;
    }), template_defs.end());
    std::sort(template_defs.begin(), template_defs.end(), [](const auto& a, const auto& b) {
        return a.name < b.name;
    });
    std::vector<kdi_template_def> function_templates;
    for (const auto& td : template_defs)
        if (td.entity_kind == "function")
            function_templates.push_back(td);

    std::ostringstream out;
    if (with_module_header && file) {
        out << "# Module " << file->header.module_name << "\n\n";
        out << "| Property | Value |\n";
        out << "|---|---|\n";
        out << "| Schema | " << code(std::to_string(file->header.schema_major) + "." + std::to_string(file->header.schema_minor)) << " |\n";
        if (!file->header.lib_base.empty())
            out << "| Library base | " << code(file->header.lib_base) << " |\n";
        if (!file->header.lib_path.empty())
            out << "| Library path | " << code(file->header.lib_path) << " |\n";
        if (!file->header.target_triple.empty())
            out << "| Target | " << code(file->header.target_triple) << " |\n";
        if (!file->header.compiler_ver.empty())
            out << "| Compiler | " << code(file->header.compiler_ver) << " |\n";
        out << "\n";
    }

    out << "# Namespace " << namespace_display_name(ns, root_fq) << "\n";
    append_doc_block(out, ns.doc);

    out << "\n## Namespaces\n\n";
    if (child_ns.empty()) {
        out << "- *(none)*\n";
    } else {
        for (const auto& child : child_ns) {
            fs::path child_path = ns_dir / child.name / "index.md";
            out << "- [" << child.name << "](" << rel_link(ns_dir, child_path) << ")";
            const std::string brief = compact_doc_brief(child.doc);
            if (!brief.empty())
                out << " - " << md_escape(brief);
            out << "\n";
        }
    }

    out << "\n## Types\n\n";
    struct type_row { std::string name; std::string kind; std::string file; std::string brief; };
    std::vector<type_row> rows;
    for (const auto& agg : aggregates)
        rows.push_back({agg.name, aggregate_kind_to_string(agg.kind), agg.name + ".md", compact_doc_brief(agg.doc)});
    for (const auto& en : enums)
        rows.push_back({en.name, "enum", en.name + ".md", compact_doc_brief(en.doc)});
    for (const auto& un : unions)
        rows.push_back({un.name, "union", un.name + ".md", compact_doc_brief(un.doc)});
    for (const auto& td : template_defs)
        if (td.entity_kind != "function")
            rows.push_back({td.name + template_param_names_str(td.params),
                            "template " + td.entity_kind, td.name + ".md", std::string()});
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
        return a.name < b.name;
    });

    if (rows.empty()) {
        out << "- *(none)*\n";
    } else {
        out << "| Name | Kind | Brief |\n";
        out << "|---|---|---|\n";
        for (const auto& row : rows)
            out << "| [" << code(row.name) << "](" << row.file << ") | " << code(row.kind)
                << " | " << md_escape(row.brief) << " |\n";
    }

    std::vector<std::string> fn_anchors(functions.size());
    out << "\n## Global Functions\n\n";
    if (functions.empty()) {
        out << "- *(none)*\n";
    } else {
        out << "| Signature | Brief |\n";
        out << "|---|---|\n";
        for (size_t i = 0; i < functions.size(); ++i) {
            const auto& fn = functions[i];
            fn_anchors[i] = "fn-" + make_slug(fn.name) + "-" + std::to_string(i);
            out << "| [" << code(make_signature(fn.name, fn.params, &fn.return_type))
                << "](#" << fn_anchors[i] << ") | " << md_escape(compact_doc_brief(fn.doc)) << " |\n";
        }
    }

    out << "\n## Function Templates\n\n";
    if (function_templates.empty()) {
        out << "- *(none)*\n";
    } else {
        out << "| Name | Parameters |\n";
        out << "|---|---|\n";
        for (const auto& td : function_templates)
            out << "| [" << code(td.name + template_param_names_str(td.params)) << "](" << td.name << ".md) | "
                << code(template_params_str(td.params)) << " |\n";
    }

    std::vector<std::string> var_anchors(variables.size());
    out << "\n## Global Variables\n\n";
    if (variables.empty()) {
        out << "- *(none)*\n";
    } else {
        out << "| Name | Type | Brief |\n";
        out << "|---|---|---|\n";
        for (size_t i = 0; i < variables.size(); ++i) {
            const auto& var = variables[i];
            var_anchors[i] = "var-" + make_slug(var.name) + "-" + std::to_string(i);
            out << "| [" << code(var.name) << "](#" << var_anchors[i] << ") | " << code(type_to_string(var.type))
                << " | " << md_escape(compact_doc_brief(var.doc)) << " |\n";
        }
    }

    out << "\n## Function Details\n";
    for (size_t i = 0; i < functions.size(); ++i) {
        const auto& fn = functions[i];
        out << "\n### <a id=\"" << fn_anchors[i] << "\"></a>"
            << code(make_signature(fn.name, fn.params, &fn.return_type)) << "\n\n";
        out << "- Visibility: " << code(visibility_to_string(fn.visibility)) << "\n";
        out << "- Static: `" << (fn.is_static ? "true" : "false") << "`\n";
        append_doc_function(out, fn.doc);
    }

    out << "\n## Variable Details\n";
    for (size_t i = 0; i < variables.size(); ++i) {
        const auto& var = variables[i];
        out << "\n### <a id=\"" << var_anchors[i] << "\"></a>" << code(var.name) << "\n\n";
        out << "- Type: " << code(type_to_string(var.type)) << "\n";
        out << "- Visibility: " << code(visibility_to_string(var.visibility)) << "\n";
        out << "- Const: `" << (var.is_const ? "true" : "false") << "`\n";
        append_doc_block(out, var.doc);
    }

    if (!write_file(ns_dir / "index.md", out.str(), error_message))
        return false;

    const std::string ns_name = ns.name.empty() ? root_fq : ns.name;
    add_reference(refs,
                  "namespace",
                  ns_name,
                  strip_prefix(parent_scope(ns.fq_name), root_fq),
                  "namespace",
                  rel_link(module_root, ns_dir / "index.md"),
                  compact_doc_brief(ns.doc));

    for (size_t i = 0; i < functions.size(); ++i) {
        const auto& fn = functions[i];
        add_reference(refs,
                      "function",
                      fn.name,
                      strip_prefix(parent_scope(fn.fq_name), root_fq),
                      make_signature(fn.name, fn.params, &fn.return_type),
                      rel_link(module_root, ns_dir / "index.md") + "#" + fn_anchors[i],
                      compact_doc_brief(fn.doc));
    }
    for (size_t i = 0; i < variables.size(); ++i) {
        const auto& var = variables[i];
        add_reference(refs,
                      "variable",
                      var.name,
                      strip_prefix(parent_scope(var.fq_name), root_fq),
                      type_to_string(var.type),
                      rel_link(module_root, ns_dir / "index.md") + "#" + var_anchors[i],
                      compact_doc_brief(var.doc));
    }

    for (const auto& agg : aggregates) {
        if (!write_aggregate_nested(agg,
                                    ns_dir,
                                    module_root,
                                    root_fq,
                                    "",
                                    refs,
                                    error_message))
            return false;
    }
    for (const auto& en : enums) {
        if (!write_enum_page(en,
                             ns_dir / (en.name + ".md"),
                             module_root,
                             root_fq,
                             refs,
                             error_message))
            return false;
    }
    for (const auto& un : unions) {
        if (!write_union_page(un,
                              ns_dir / (un.name + ".md"),
                              module_root,
                              root_fq,
                              refs,
                              error_message,
                              un.name))
            return false;
    }
    for (const auto& td : template_defs) {
        if (!write_template_def_page(td,
                                     ns_dir / (td.name + ".md"),
                                     module_root,
                                     root_fq,
                                     refs,
                                     error_message))
            return false;
    }

    for (const auto& child : child_ns) {
        if (!write_namespace_tree(child,
                                  module_root,
                                  root_fq,
                                  refs,
                                  error_message,
                                  false,
                                  file))
            return false;
    }

    return true;
}

bool write_reference_indexes(const fs::path& module_root,
                             std::vector<symbol_ref> refs,
                             std::string* error_message)
{
    std::sort(refs.begin(), refs.end(), [](const auto& a, const auto& b) {
        if (a.name != b.name) return a.name < b.name;
        if (a.scope != b.scope) return a.scope < b.scope;
        return a.kind < b.kind;
    });

    std::ostringstream by_name;
    by_name << "# Name References\n\n";
    by_name << "- Total symbols: `" << refs.size() << "`\n\n";
    by_name << "| Name | Kind | Scope | Type | Brief |\n";
    by_name << "|---|---|---|---|---|\n";
    for (const auto& ref : refs) {
        by_name << "| [`" << ref.name << "`](" << ref.link << ") | `" << ref.kind << "` | `"
                << (ref.scope.empty() ? "<root>" : ref.scope) << "` | `"
                << ref.type_desc << "` | " << md_escape(ref.brief) << " |\n";
    }
    if (!write_file(module_root / "name-references.md", by_name.str(), error_message))
        return false;

    std::sort(refs.begin(), refs.end(), [](const auto& a, const auto& b) {
        if (a.kind != b.kind) return a.kind < b.kind;
        if (a.name != b.name) return a.name < b.name;
        return a.scope < b.scope;
    });

    std::ostringstream by_type;
    by_type << "# Typed References\n\n";

    std::string current_kind;
    for (const auto& ref : refs) {
        if (ref.kind != current_kind) {
            current_kind = ref.kind;
            by_type << "## " << current_kind << "\n\n";
            by_type << "| Name | Scope | Type | Brief |\n";
            by_type << "|---|---|---|---|\n";
        }
        by_type << "| [`" << ref.name << "`](" << ref.link << ") | `"
                << (ref.scope.empty() ? "<root>" : ref.scope)
                << "` | `" << ref.type_desc << "` | " << md_escape(ref.brief) << " |\n";
    }

    return write_file(module_root / "typed-references.md", by_type.str(), error_message);
}

} // anonymous namespace

bool kdi_generate_markdown_doc(const kdi_file& file,
                               const std::string& destination_dir,
                               std::string* error_message)
{
    try {
        // (Re)build the concrete-instantiation index used by type_to_string() so
        // that references to synthesized template instantiations render with
        // their real generic arguments (see collect_instantiation_origins()).
        g_instantiation_origins.clear();
        collect_instantiation_origins(file.unit.root_ns);

        const fs::path destination = destination_dir.empty() ? fs::path(".") : fs::path(destination_dir);

        std::error_code ec;
        if (fs::exists(destination, ec) && fs::is_regular_file(destination, ec)) {
            if (error_message)
                *error_message = "destination path is a file: '" + destination.string() + "'";
            return false;
        }

        fs::create_directories(destination, ec);
        if (ec) {
            if (error_message)
                *error_message = "cannot create destination directory '" + destination.string() + "': " + ec.message();
            return false;
        }

        if (file.header.module_name.empty()) {
            if (error_message)
                *error_message = "module name is empty in KDI header";
            return false;
        }

        const fs::path module_root = destination / file.header.module_name;
        fs::create_directories(module_root, ec);
        if (ec) {
            if (error_message)
                *error_message = "cannot create module directory '" + module_root.string() + "': " + ec.message();
            return false;
        }

        const std::string root_fq = !file.unit.root_ns.fq_name.empty()
                                        ? file.unit.root_ns.fq_name
                                        : file.header.module_name;

        // (Re)build the hierarchy cross-reference index (bases/derived, per
        // module) used to render the "## Inheritance" sections with links.
        g_hierarchy_nodes.clear();
        g_direct_derived.clear();
        g_enum_pages.clear();
        g_enum_display.clear();
        g_enum_direct_derived.clear();
        g_union_pages.clear();
        g_union_display.clear();
        g_union_direct_derived.clear();
        collect_hierarchy_index(file.unit.root_ns, module_root, root_fq);

        std::vector<symbol_ref> refs;
        if (!write_namespace_tree(file.unit.root_ns,
                                  module_root,
                                  root_fq,
                                  refs,
                                  error_message,
                                  true,
                                  &file))
            return false;

        if (!write_reference_indexes(module_root, refs, error_message))
            return false;

        return true;
    } catch (const std::exception& e) {
        if (error_message)
            *error_message = e.what();
        return false;
    }
}

} // namespace kdi



