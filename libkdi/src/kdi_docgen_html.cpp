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

/**
 * @file kdi_docgen_html.cpp
 *
 * Static HTML documentation generator for KDI files.
 * Produces a self-contained documentation tree with modern styling.
 */

#include "kdi_docgen.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>

namespace kdi {

namespace {

namespace fs = std::filesystem;

// ─── CSS ─────────────────────────────────────────────────────────────────────

static const char* KDOC_CSS = R"css(
/* K Language Documentation — kdoc.css */
:root {
  --primary: #1e3a5f;
  --accent: #0066cc;
  --accent-h: #0052a3;
  --bg: #ffffff;
  --bg2: #f5f6f8;
  --border: #dde1e7;
  --text: #1c2128;
  --muted: #6b7785;
  --code-bg: #eef0f3;
  --code-fg: #b91c1c;
  --fn: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, Arial, sans-serif;
  --fm: "SFMono-Regular", Consolas, "Liberation Mono", Menlo, monospace;
  --r: 6px;
}
* { box-sizing: border-box; margin: 0; padding: 0; }
body {
  font-family: var(--fn);
  font-size: 15px;
  line-height: 1.65;
  color: var(--text);
  background: var(--bg);
  display: flex;
  flex-direction: column;
  min-height: 100vh;
}
/* Top bar */
.topbar {
  background: var(--primary);
  color: #fff;
  height: 52px;
  display: flex;
  align-items: center;
  padding: 0 1.5rem;
  gap: .85rem;
  position: sticky;
  top: 0;
  z-index: 100;
  box-shadow: 0 2px 6px rgba(0,0,0,.28);
}
.tb-title { font-size: 1.05rem; font-weight: 700; color: #fff; text-decoration: none; }
.tb-sub { font-size: .78rem; color: rgba(255,255,255,.5); margin-top: 1px; }
/* Layout */
.layout { display: flex; flex: 1; }
/* Sidebar */
nav.sidebar {
  width: 266px;
  min-width: 200px;
  flex-shrink: 0;
  background: var(--bg2);
  border-right: 1px solid var(--border);
  padding: 1.1rem 0 2rem;
  overflow-y: auto;
  position: sticky;
  top: 52px;
  height: calc(100vh - 52px);
}
.ng { margin-bottom: .85rem; }
.ng-title {
  font-size: .67rem;
  font-weight: 700;
  text-transform: uppercase;
  letter-spacing: .1em;
  color: var(--muted);
  padding: .4rem 1.1rem .18rem;
}
.ni {
  display: block;
  padding: .29rem 1.1rem;
  font-size: .845rem;
  color: var(--accent);
  text-decoration: none;
  border-left: 3px solid transparent;
  white-space: nowrap;
  overflow: hidden;
  text-overflow: ellipsis;
}
.ni:hover { background: #e5eaf3; border-left-color: var(--accent); }
.ni.active { background: rgba(0,102,204,.08); border-left-color: var(--accent); font-weight: 600; }
.ni.sub { padding-left: 1.75rem; font-size: .82rem; }
/* Main */
main.main {
  flex: 1;
  padding: 1.75rem 2.5rem 3rem;
  max-width: 1040px;
  min-width: 0;
}
/* Breadcrumbs */
.bc {
  display: flex;
  align-items: center;
  gap: .35rem;
  font-size: .77rem;
  color: var(--muted);
  margin-bottom: 1.6rem;
  flex-wrap: wrap;
}
.bc a { color: var(--accent); text-decoration: none; }
.bc a:hover { text-decoration: underline; }
.bc-sep { color: var(--border); user-select: none; }
/* Page title */
h1.ptitle {
  font-size: 1.6rem;
  font-weight: 700;
  color: var(--primary);
  letter-spacing: -.025em;
  line-height: 1.2;
  margin-bottom: .55rem;
}
/* Kind badge */
.kb {
  display: inline-block;
  font-size: .65rem;
  font-weight: 700;
  text-transform: uppercase;
  letter-spacing: .07em;
  padding: .16rem .55rem;
  border-radius: 99px;
  vertical-align: middle;
  margin-left: .65rem;
  background: var(--primary);
  color: #fff;
}
.kb-class    { background: #166534; }
.kb-interface{ background: #6d28d9; }
.kb-enum     { background: #92400e; }
.kb-union    { background: #0369a1; }
.kb-struct   { background: #1d4e89; }
.kb-annot    { background: #9d174d; }
.kb-ns       { background: #4b5563; }
.kb-module   { background: var(--primary); }
/* Section titles */
h2.sh {
  font-size: 1.05rem;
  font-weight: 600;
  color: var(--primary);
  margin: 2rem 0 .75rem;
  padding-bottom: .35rem;
  border-bottom: 2px solid var(--border);
}
/* Overview card */
.ov {
  border: 1px solid var(--border);
  border-radius: var(--r);
  overflow: hidden;
  max-width: 540px;
  margin-bottom: 1.5rem;
  box-shadow: 0 1px 3px rgba(0,0,0,.07);
}
.ov table { border-collapse: collapse; width: 100%; font-size: .875rem; }
.ov th {
  background: var(--primary);
  color: #fff;
  font-size: .72rem;
  font-weight: 700;
  text-transform: uppercase;
  letter-spacing: .05em;
  padding: .5rem 1rem;
  width: 36%;
  text-align: left;
}
.ov td { padding: .48rem 1rem; border-bottom: 1px solid var(--border); }
.ov tr:last-child td { border-bottom: none; }
.ov tr:nth-child(even) td { background: var(--bg2); }
/* Summary table */
.stbl {
  width: 100%;
  border-collapse: collapse;
  font-size: .875rem;
  border: 1px solid var(--border);
  border-radius: var(--r);
  overflow: hidden;
  margin-bottom: 1.25rem;
  box-shadow: 0 1px 3px rgba(0,0,0,.05);
}
.stbl thead th {
  background: var(--bg2);
  border-bottom: 2px solid var(--border);
  padding: .47rem 1rem;
  font-size: .75rem;
  font-weight: 700;
  text-transform: uppercase;
  letter-spacing: .05em;
  color: var(--muted);
  text-align: left;
}
.stbl tbody tr { border-bottom: 1px solid var(--border); }
.stbl tbody tr:last-child { border-bottom: none; }
.stbl tbody tr:hover { background: #edf1f9; }
.stbl td { padding: .47rem 1rem; vertical-align: middle; }
.stbl td.tn { font-family: var(--fm); font-size: .83rem; font-weight: 600; color: var(--primary); }
.stbl td.tk { font-size: .79rem; color: var(--muted); }
.stbl td.ts { font-family: var(--fm); font-size: .77rem; word-break: break-all; }
.stbl td.tt { font-family: var(--fm); font-size: .81rem; }
.stbl td.tl { white-space: nowrap; }
.stbl a { color: var(--accent); text-decoration: none; font-weight: 500; font-size: .82rem; }
.stbl a:hover { text-decoration: underline; }
/* Code */
code {
  font-family: var(--fm);
  font-size: .845em;
  background: var(--code-bg);
  color: var(--code-fg);
  padding: .1em .35em;
  border-radius: 3px;
}
/* Detail block */
.dblock { border: 1px solid var(--border); border-radius: var(--r); margin: 1.2rem 0; overflow: hidden; }
.dhead {
  padding: .6rem 1rem;
  background: var(--bg2);
  border-bottom: 1px solid var(--border);
  border-left: 4px solid var(--accent);
  font-family: var(--fm);
  font-size: .875rem;
  font-weight: 600;
  color: var(--primary);
  word-break: break-all;
}
.dbody { padding: .75rem 1rem; font-size: .875rem; }
/* Meta row */
.meta { display: flex; gap: .7rem; flex-wrap: wrap; align-items: center; margin: .3rem 0 .55rem; font-size: .82rem; }
/* Tags */
.tag { display: inline-block; font-size: .69rem; font-weight: 700; padding: .1em .42em; border-radius: 3px; }
.t-pub   { background: #dbeafe; color: #1d4ed8; }
.t-prot  { background: #fef3c7; color: #b45309; }
.t-stat  { background: #e0f2fe; color: #0369a1; }
.t-abs   { background: #ede9fe; color: #6d28d9; }
.t-fin   { background: #fce7f3; color: #9d174d; }
.t-const { background: #dcfce7; color: #15803d; }
/* Doc */
.doc-brief { margin: .4rem 0 .8rem; line-height: 1.7; font-size: .95rem; }
.doc-desc  { margin: .3rem 0 .8rem; line-height: 1.7; font-size: .87rem; color: var(--muted); }
dl.dparams { margin: .5rem 0; }
dl.dparams dt { font-family: var(--fm); font-size: .82rem; font-weight: 600; color: var(--primary); margin-top: .45rem; }
dl.dparams dd { margin: .1rem 0 .35rem 1.15rem; font-size: .855rem; color: var(--muted); }
/* Empty */
.empty { font-size: .875rem; color: var(--muted); font-style: italic; padding: .6rem 0; }
/* Source code block (template declarations) */
.src-block {
  background: var(--code-bg);
  border: 1px solid var(--border);
  border-radius: var(--r);
  padding: .9rem 1rem;
  overflow-x: auto;
  font-family: var(--fm);
  font-size: .82rem;
  line-height: 1.55;
  color: var(--text);
  white-space: pre;
}
.kb-template  { background: #7c2d12; }
/* Footer */
footer.footer {
  background: var(--bg2);
  border-top: 1px solid var(--border);
  text-align: center;
  padding: .9rem 2rem;
  font-size: .77rem;
  color: var(--muted);
}
)css";

// ─── Utility structs ──────────────────────────────────────────────────────────

struct symbol_ref_html {
    std::string kind;
    std::string name;
    std::string scope;
    std::string type_desc;
    std::string link;
    std::string brief;
};

struct html_ctx {
    std::string module_name;
    fs::path module_root;
    std::string root_fq;
    // (display_name, rel_path_from_module_root/index.html)
    std::vector<std::pair<std::string, std::string>> ns_nav;
};

// ─── Shared utilities (mirrors kdi_docgen.cpp anonymous helpers) ──────────────

// Index of concrete template instantiations (aggregate/union fq_name -> template
// origin), populated once per doc-generation run so that any type reference to
// a compiler-synthesized instantiation (e.g. "k::Expected__unsigned_sint__...")
// can be rendered using the real generic syntax (e.g. "k::Expected<unsigned int32, ...>")
// instead of the mangled/synthesized name.
static std::unordered_map<std::string, kdi_template_origin> g_instantiation_origins_h;

// Strip a leading "::" root-prefix, to match the KDI convention used by
// kdi_aggregate_ref::fq_name (always stored without the prefix).
static std::string strip_root_prefix_h(const std::string& fq) {
    return (fq.size() >= 2 && fq[0] == ':' && fq[1] == ':') ? fq.substr(2) : fq;
}

static void collect_instantiation_origins_h(const kdi_aggregate& agg) {
    if (agg.template_origin)
        g_instantiation_origins_h[strip_root_prefix_h(agg.fq_name)] = *agg.template_origin;
    for (auto& nested : agg.nested)
        collect_instantiation_origins_h(nested);
    for (auto& nested_union : agg.nested_unions) {
        if (nested_union.template_origin)
            g_instantiation_origins_h[strip_root_prefix_h(nested_union.fq_name)] = *nested_union.template_origin;
    }
}

static void collect_instantiation_origins_h(const kdi_namespace& ns) {
    for (auto& agg : ns.aggregates)
        collect_instantiation_origins_h(agg);
    for (auto& u : ns.unions) {
        if (u.template_origin)
            g_instantiation_origins_h[strip_root_prefix_h(u.fq_name)] = *u.template_origin;
    }
    for (auto& child : ns.namespaces)
        collect_instantiation_origins_h(child);
}

static std::string type_to_string_h(const kdi_type& t) {
    return std::visit([](const auto& v) -> std::string {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, kdi_void_type>) return "void";
        if constexpr (std::is_same_v<T, kdi_bool_type>) return "bool";
        if constexpr (std::is_same_v<T, kdi_char_type>) return "char";
        if constexpr (std::is_same_v<T, kdi_int_type>)
            return (v.is_signed ? "" : "unsigned ") + std::string("int") + std::to_string(v.bits);
        if constexpr (std::is_same_v<T, kdi_float_type>) return "float" + std::to_string(v.bits);
        if constexpr (std::is_same_v<T, kdi_ref_type>)   return "&" + (v.inner ? type_to_string_h(*v.inner) : "?");
        if constexpr (std::is_same_v<T, kdi_ptr_type>)   return "*" + (v.inner ? type_to_string_h(*v.inner) : "?");
        if constexpr (std::is_same_v<T, kdi_link_type>)  return "+" + (v.inner ? type_to_string_h(*v.inner) : "?");
        if constexpr (std::is_same_v<T, kdi_view_type>)  return "?" + (v.inner ? type_to_string_h(*v.inner) : "?");
        if constexpr (std::is_same_v<T, kdi_owner_type>) return "!" + (v.inner ? type_to_string_h(*v.inner) : "?");
        if constexpr (std::is_same_v<T, kdi_drain_type>) return "#" + (v.inner ? type_to_string_h(*v.inner) : "?");
        if constexpr (std::is_same_v<T, kdi_const_type>) return "const " + (v.inner ? type_to_string_h(*v.inner) : "?");
        if constexpr (std::is_same_v<T, kdi_array_type>) return "[]" + (v.elem ? type_to_string_h(*v.elem) : "?");
        if constexpr (std::is_same_v<T, kdi_sized_array_type>)
            return "[" + std::to_string(v.size) + "]" + (v.elem ? type_to_string_h(*v.elem) : "?");
        if constexpr (std::is_same_v<T, kdi_fn_ref_type>) {
            std::string s = "fn(";
            for (size_t i = 0; i < v.params.size(); ++i) {
                if (i) s += ", ";
                s += (v.params[i] ? type_to_string_h(*v.params[i]) : "?");
            }
            return s + ") : " + (v.ret ? type_to_string_h(*v.ret) : "void");
        }
        if constexpr (std::is_same_v<T, kdi_aggregate_ref>) {
            // If this reference targets a compiler-synthesized concrete template
            // instantiation, render it using its real generic arguments instead
            // of the synthesized/mangled fq_name.
            auto it = g_instantiation_origins_h.find(v.fq_name);
            if (it != g_instantiation_origins_h.end()) {
                const kdi_template_origin& origin = it->second;
                std::string s = strip_root_prefix_h(origin.base_fq_name) + "<";
                for (size_t i = 0; i < origin.args.size(); ++i) {
                    if (i) s += ", ";
                    const auto& arg = origin.args[i];
                    if (arg.type_arg) s += type_to_string_h(*arg.type_arg);
                    else if (arg.value_arg) s += *arg.value_arg;
                    else s += "?";
                }
                return s + ">";
            }
            return v.fq_name;
        }
        if constexpr (std::is_same_v<T, kdi_enum_ref>)      return "enum " + v.fq_name;
        if constexpr (std::is_same_v<T, kdi_template_param_ref>) return v.name;
        if constexpr (std::is_same_v<T, kdi_generic_ref_type>) {
            std::string s = v.name + "<";
            for (size_t i = 0; i < v.args.size(); ++i) {
                if (i) s += ", ";
                s += (v.args[i] ? type_to_string_h(*v.args[i]) : "?");
            }
            return s + ">";
        }
        return "?";
    }, t.value);
}

static std::string agg_kind_str(kdi_aggregate_kind k) {
    switch (k) {
    case kdi_aggregate_kind::struct_:     return "struct";
    case kdi_aggregate_kind::class_:      return "class";
    case kdi_aggregate_kind::interface_:  return "interface";
    case kdi_aggregate_kind::annotation_: return "annotation";
    }
    return "aggregate";
}

static std::string vis_str(kdi_visibility v) {
    return v == kdi_visibility::public_ ? "public" : "protected";
}

/**
 * Translate an internal canonical operator function name (e.g. "__operator_eq_")
 * into its human-readable K declaration syntax (e.g. "operator =="), per the
 * canonical name table in doc/spec/language/functions/operators.md. Cast
 * operators ("__operator_cv_...") become the bare "operator" keyword, since the
 * cast target type is already rendered by make_sig() via the return type
 * (yielding "operator() : T", matching the spec's declaration syntax exactly).
 * Non-operator names are returned unchanged.
 */
static std::string operator_display_name_h(const std::string& name) {
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

static std::string make_sig(const std::string& name,
                             const std::vector<kdi_param>& params,
                             const kdi_type* ret)
{
    std::ostringstream out;
    out << operator_display_name_h(name) << "(";
    for (size_t i = 0; i < params.size(); ++i) {
        if (i) out << ", ";
        out << params[i].name;
        if (params[i].is_varargs) out << "...";
        out << ": " << type_to_string_h(params[i].type);
    }
    out << ")";
    if (ret) out << " : " << type_to_string_h(*ret);
    return out.str();
}

static std::string make_slug_h(std::string v) {
    for (char& ch : v) {
        if (std::isalnum(static_cast<unsigned char>(ch)))
            ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        else
            ch = '-';
    }
    while (!v.empty() && v.back() == '-') v.pop_back();
    return v.empty() ? "item" : v;
}

static std::string trim_root_h(std::string v) {
    while (!v.empty() && v.front() == ':') v.erase(v.begin());
    return v;
}

static std::string strip_pfx(const std::string& full, const std::string& prefix) {
    const std::string cf = trim_root_h(full);
    const std::string cp = trim_root_h(prefix);
    if (cp.empty()) return cf;
    if (cf == cp) return {};
    if (cf.rfind(cp + "::", 0) == 0) return cf.substr(cp.size() + 2);
    return cf;
}

static std::vector<std::string> split_scope_h(const std::string& fq) {
    std::vector<std::string> parts;
    std::string tok;
    for (size_t i = 0; i < fq.size(); ++i) {
        if (i + 1 < fq.size() && fq[i] == ':' && fq[i+1] == ':') {
            if (!tok.empty()) { parts.push_back(tok); tok.clear(); }
            ++i; continue;
        }
        tok.push_back(fq[i]);
    }
    if (!tok.empty()) parts.push_back(tok);
    return parts;
}

static std::string parent_scope_h(const std::string& fq) {
    const auto pos = fq.rfind("::");
    if (pos == std::string::npos) return {};
    return fq.substr(0, pos);
}

static bool write_file_h(const fs::path& path, const std::string& content, std::string* err) {
    std::ofstream out(path);
    if (!out) {
        if (err) *err = "cannot write file '" + path.string() + "'";
        return false;
    }
    out << content;
    return true;
}

static std::string rel_link_h(const fs::path& from_dir, const fs::path& to_path) {
    return fs::relative(to_path, from_dir).generic_string();
}

// ─── HTML-specific helpers ────────────────────────────────────────────────────

static std::string html_escape(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (char ch : text) {
        switch (ch) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;";  break;
        case '>': out += "&gt;";  break;
        case '"': out += "&quot;"; break;
        default:  out += ch;      break;
        }
    }
    return out;
}

static std::string hcode(const std::string& v) {
    return "<code>" + html_escape(v) + "</code>";
}

static std::string compact_brief_h(std::string brief, size_t max_len = 96) {
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

static std::string compact_doc_brief_h(const std::optional<kdi_doc_block>& doc) {
    if (!doc.has_value())
        return {};
    return compact_brief_h(doc->brief);
}

static std::string compact_doc_brief_h(const std::optional<kdi_doc_function>& doc) {
    if (!doc.has_value())
        return {};
    return compact_brief_h(doc->brief);
}

static std::string kind_badge(const std::string& kind) {
    std::string cls = "kb";
    if      (kind == "class")      cls += " kb-class";
    else if (kind == "interface")  cls += " kb-interface";
    else if (kind == "enum")       cls += " kb-enum";
    else if (kind == "union")      cls += " kb-union";
    else if (kind == "struct")     cls += " kb-struct";
    else if (kind == "annotation") cls += " kb-annot";
    else if (kind == "namespace")  cls += " kb-ns";
    else if (kind == "module")     cls += " kb-module";
    else if (kind.rfind("template", 0) == 0) cls += " kb-template";
    return "<span class=\"" + cls + "\">" + html_escape(kind) + "</span>";
}

static std::string vis_tag(kdi_visibility v) {
    if (v == kdi_visibility::public_)
        return "<span class=\"tag t-pub\">public</span>";
    return "<span class=\"tag t-prot\">protected</span>";
}

static std::string vis_tag_str(const std::string& v) {
    if (v == "public")
        return "<span class=\"tag t-pub\">public</span>";
    return "<span class=\"tag t-prot\">protected</span>";
}

// ─── Navigation helpers ───────────────────────────────────────────────────────

static void collect_ns_nav(const kdi_namespace& ns,
                            const std::string& root_fq,
                            std::vector<std::pair<std::string,std::string>>& items,
                            bool is_root)
{
    if (!is_root) {
        const std::string rel_scope = strip_pfx(ns.fq_name, root_fq);
        std::string ns_path;
        for (const auto& part : split_scope_h(rel_scope))
            ns_path += (ns_path.empty() ? "" : "/") + part;
        items.emplace_back(rel_scope, ns_path + "/index.html");
    }
    for (const auto& child : ns.namespaces)
        collect_ns_nav(child, root_fq, items, false);
}

static std::string make_sidebar(const html_ctx& ctx, const fs::path& page_dir) {
    auto link = [&](const std::string& rel_from_root) -> std::string {
        return rel_link_h(page_dir, ctx.module_root / rel_from_root);
    };

    std::ostringstream sb;
    sb << "  <div class=\"ng\">\n"
       << "    <div class=\"ng-title\">Module</div>\n"
       << "    <a class=\"ni\" href=\"" << link("index.html") << "\">"
       << html_escape(ctx.module_name) << "</a>\n"
       << "  </div>\n";

    if (!ctx.ns_nav.empty()) {
        sb << "  <div class=\"ng\">\n"
           << "    <div class=\"ng-title\">Namespaces</div>\n";
        for (const auto& [name, rel] : ctx.ns_nav) {
            sb << "    <a class=\"ni sub\" href=\"" << link(rel) << "\">"
               << html_escape(name) << "</a>\n";
        }
        sb << "  </div>\n";
    }

    sb << "  <div class=\"ng\">\n"
       << "    <div class=\"ng-title\">Indexes</div>\n"
       << "    <a class=\"ni\" href=\"" << link("name-references.html") << "\">Name References</a>\n"
       << "    <a class=\"ni\" href=\"" << link("typed-references.html") << "\">Typed References</a>\n"
       << "  </div>\n";

    return sb.str();
}

static std::string make_page(const html_ctx& ctx,
                              const fs::path& page_dir,
                              const std::string& title,
                              const std::string& breadcrumbs_html,
                              const std::string& content_html)
{
    const std::string css_path = rel_link_h(page_dir, ctx.module_root / "kdoc.css");
    const std::string sidebar  = make_sidebar(ctx, page_dir);

    std::ostringstream out;
    out << "<!DOCTYPE html>\n"
        << "<html lang=\"en\">\n"
        << "<head>\n"
        << "<meta charset=\"utf-8\">\n"
        << "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n"
        << "<title>" << html_escape(title) << " \xe2\x80\x94 "
        << html_escape(ctx.module_name) << "</title>\n"
        << "<link rel=\"stylesheet\" href=\"" << css_path << "\">\n"
        << "</head>\n"
        << "<body>\n"
        << "<header class=\"topbar\">\n"
        << "  <span class=\"tb-title\">" << html_escape(ctx.module_name) << "</span>\n"
        << "  <span class=\"tb-sub\">K Language Documentation</span>\n"
        << "</header>\n"
        << "<div class=\"layout\">\n"
        << "<nav class=\"sidebar\">\n" << sidebar << "</nav>\n"
        << "<main class=\"main\">\n"
        << "<nav class=\"bc\">" << breadcrumbs_html << "</nav>\n"
        << content_html
        << "</main>\n"
        << "</div>\n"
        << "<footer class=\"footer\">Generated by kditool &mdash; K Language Documentation</footer>\n"
        << "</body>\n"
        << "</html>\n";
    return out.str();
}

// Build breadcrumbs from scope parts + final current label
// scope_parts: list of (label, href) — href empty means current page
static std::string make_breadcrumbs(
    const std::vector<std::pair<std::string, std::string>>& parts)
{
    std::ostringstream bc;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) bc << "<span class=\"bc-sep\">/</span>";
        if (parts[i].second.empty())
            bc << "<span>" << html_escape(parts[i].first) << "</span>";
        else
            bc << "<a href=\"" << parts[i].second << "\">" << html_escape(parts[i].first) << "</a>";
    }
    return bc.str();
}

// ─── Doc block helpers ────────────────────────────────────────────────────────

static void append_doc_block_html(std::ostringstream& out, const kdi_doc_block& doc) {
    if (!doc.brief.empty())
        out << "<p class=\"doc-brief\">" << html_escape(doc.brief) << "</p>\n";
    if (!doc.description.empty())
        out << "<p class=\"doc-desc\">" << html_escape(doc.description) << "</p>\n";
}

static void append_doc_block_html(std::ostringstream& out, const std::optional<kdi_doc_block>& doc) {
    if (doc.has_value()) append_doc_block_html(out, *doc);
}

static void append_doc_function_html(std::ostringstream& out, const std::optional<kdi_doc_function>& doc) {
    if (!doc.has_value()) return;
    append_doc_block_html(out, static_cast<const kdi_doc_block&>(*doc));
    if (!doc->params.empty()) {
        out << "<dl class=\"dparams\">\n";
        for (const auto& p : doc->params)
            out << "  <dt>" << html_escape(p.name) << "</dt>"
                << "<dd>" << html_escape(p.description) << "</dd>\n";
        out << "</dl>\n";
    }
    if (doc->returns.has_value())
        out << "<p class=\"doc-desc\"><strong>Returns:</strong> " << html_escape(*doc->returns) << "</p>\n";
    if (!doc->throws.empty()) {
        out << "<dl class=\"dparams\">\n";
        for (const auto& t : doc->throws)
            out << "  <dt>" << html_escape(t.type_name) << "</dt>"
                << "<dd>" << html_escape(t.description) << "</dd>\n";
        out << "</dl>\n";
    }
}

// ─── Reference accumulation ───────────────────────────────────────────────────

static void add_ref(std::vector<symbol_ref_html>& refs,
                    std::string kind, std::string name, std::string scope,
                    std::string type_desc, std::string link, std::string brief = {})
{
    refs.push_back({std::move(kind), std::move(name), std::move(scope),
                    std::move(type_desc), std::move(link), std::move(brief)});
}

// ─── Compute namespace directory from root ────────────────────────────────────

static fs::path ns_dir_from_root(const html_ctx& ctx, const kdi_namespace& ns) {
    const std::string rel_scope = strip_pfx(ns.fq_name, ctx.root_fq);
    fs::path dir = ctx.module_root;
    if (!rel_scope.empty())
        for (const auto& part : split_scope_h(rel_scope))
            dir /= part;
    return dir;
}

// ─── Page writers ─────────────────────────────────────────────────────────────

static bool write_enum_page_html(const kdi_enum& en,
                                  const html_ctx& ctx,
                                  const fs::path& ns_dir,
                                  std::vector<symbol_ref_html>& refs,
                                  std::string* err)
{
    const fs::path file_path = ns_dir / (en.name + ".html");

    // Breadcrumbs
    std::vector<std::pair<std::string,std::string>> bc_parts;
    const std::string rel_scope = strip_pfx(parent_scope_h(en.fq_name), ctx.root_fq);
    bc_parts.emplace_back(ctx.module_name, rel_link_h(ns_dir, ctx.module_root / "index.html"));
    if (!rel_scope.empty()) {
        // Add intermediate namespace parts
        std::string built;
        for (const auto& part : split_scope_h(rel_scope)) {
            built += (built.empty() ? "" : "/") + part;
            bc_parts.emplace_back(part, rel_link_h(ns_dir, ctx.module_root / built / "index.html"));
        }
    }
    bc_parts.emplace_back(en.name, "");

    // Overview card
    std::ostringstream content;
    content << "<h1 class=\"ptitle\">" << html_escape(en.name)
            << kind_badge("enum") << "</h1>\n";

    content << "<div class=\"ov\"><table>\n"
            << "<tr><th>Property</th><th>Value</th></tr>\n"
            << "<tr><td>Fully qualified name</td><td>" << hcode(en.fq_name) << "</td></tr>\n"
            << "<tr><td>Visibility</td><td>" << vis_tag(en.visibility) << "</td></tr>\n"
            << "<tr><td>Underlying type</td><td>" << hcode(type_to_string_h(en.underlying_type)) << "</td></tr>\n"
            << "</table></div>\n";

    append_doc_block_html(content, en.doc);

    std::vector<kdi_enum_entry> entries = en.entries;
    std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) { return a.name < b.name; });

    content << "<h2 class=\"sh\">Entries</h2>\n";
    if (entries.empty()) {
        content << "<p class=\"empty\"><em>None.</em></p>\n";
    } else {
        content << "<table class=\"stbl\">\n"
                << "<thead><tr><th>Name</th><th>Value</th></tr></thead>\n<tbody>\n";
        for (const auto& e : entries)
            content << "<tr><td class=\"tn\">" << html_escape(e.name) << "</td>"
                    << "<td class=\"ts\">" << hcode(std::to_string(e.value)) << "</td></tr>\n";
        content << "</tbody></table>\n";
    }

    const std::string page_html = make_page(ctx, ns_dir,
        en.name + " — enum", make_breadcrumbs(bc_parts), content.str());

    if (!write_file_h(file_path, page_html, err)) return false;

    const std::string link = rel_link_h(ctx.module_root, file_path);
    const std::string scope = strip_pfx(parent_scope_h(en.fq_name), ctx.root_fq);
    add_ref(refs, "enum", en.name, scope, "enum", link, compact_doc_brief_h(en.doc));
    return true;
}

static std::string template_param_to_string_h(const kdi_template_param& p) {
    std::string s;
    if (p.kind == "value")
        s = (p.value_type ? type_to_string_h(*p.value_type) : "value") + " " + p.name;
    else
        s = p.kind + " " + p.name;
    if (p.constraint_type)
        s += " : " + type_to_string_h(*p.constraint_type);
    if (p.default_type)
        s += " = " + type_to_string_h(*p.default_type);
    else if (p.default_value)
        s += " = " + *p.default_value;
    return s;
}

static std::string template_params_str_h(const std::vector<kdi_template_param>& params) {
    std::string s = "<";
    for (size_t i = 0; i < params.size(); ++i) {
        if (i) s += ", ";
        s += template_param_to_string_h(params[i]);
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
static std::string template_param_names_str_h(const std::vector<kdi_template_param>& params) {
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
 * Render the structured body of an aggregate (Member Variables, Members,
 * Nested Types, and the detailed sections) into `content`.
 *
 * Shared between regular aggregate pages and template aggregate pages (whose
 * `aggregate_signature` is a full-fledged kdi_aggregate) so that a template
 * class documents its fields/constructors/methods exactly like its
 * non-template equivalents, instead of a raw source dump.
 */
static void write_aggregate_body_html(std::ostringstream& content,
                                       const kdi_aggregate& agg,
                                       const std::string& file_stem)
{
    // Collect and sort members
    std::vector<kdi_layout_member> fields;
    for (const auto& f : agg.layout)
        if (auto* m = std::get_if<kdi_layout_member>(&f)) fields.push_back(*m);
    std::sort(fields.begin(), fields.end(), [](const auto& a, const auto& b) { return a.name < b.name; });

    std::vector<kdi_variable> svars = agg.static_vars;
    std::sort(svars.begin(), svars.end(), [](const auto& a, const auto& b) { return a.name < b.name; });

    std::vector<kdi_constructor> ctors = agg.constructors;
    std::sort(ctors.begin(), ctors.end(), [](const auto& a, const auto& b) {
        if (a.params.size() != b.params.size()) return a.params.size() < b.params.size();
        return a.mangled_name < b.mangled_name;
    });

    std::vector<kdi_method> methods = agg.methods;
    methods.erase(std::remove_if(methods.begin(), methods.end(), [](const auto& m) {
        return m.template_origin.has_value();
    }), methods.end());
    std::sort(methods.begin(), methods.end(), [](const auto& a, const auto& b) {
        if (a.name != b.name) return a.name < b.name;
        return a.mangled_name < b.mangled_name;
    });

    // Nested types
    std::vector<std::pair<std::string,std::string>> nested_types;
    for (const auto& n : agg.nested) {
        if (n.template_origin.has_value())
            continue; // Skip synthesized template instantiations - not useful in docs.
        nested_types.emplace_back(n.name, file_stem + "." + n.name + ".html");
    }
    for (const auto& n : agg.nested_unions) {
        if (n.template_origin.has_value())
            continue; // Skip synthesized template instantiations - not useful in docs.
        nested_types.emplace_back(n.name, file_stem + "." + n.name + ".html");
    }
    std::sort(nested_types.begin(), nested_types.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

    // ── Member Variables summary ──
    content << "<h2 class=\"sh\">Member Variables</h2>\n";
    if (fields.empty() && svars.empty()) {
        content << "<p class=\"empty\"><em>None.</em></p>\n";
    } else {
        content << "<table class=\"stbl\">\n"
                << "<thead><tr><th>Name</th><th>Type</th><th>Visibility</th><th>Brief</th></tr></thead>\n<tbody>\n";
        for (size_t i = 0; i < fields.size(); ++i) {
            const auto& f = fields[i];
            const std::string id = "field-" + make_slug_h(f.name) + "-" + std::to_string(i);
            content << "<tr><td class=\"tn\"><a href=\"#" << id << "\">" << html_escape(f.name) << "</a></td>"
                    << "<td class=\"tt\">" << hcode(type_to_string_h(f.type)) << "</td>"
                    << "<td>" << vis_tag(f.visibility) << "</td>"
                    << "<td class=\"tk\"></td></tr>\n";
        }
        for (size_t i = 0; i < svars.size(); ++i) {
            const auto& v = svars[i];
            const std::string id = "svar-" + make_slug_h(v.name) + "-" + std::to_string(i);
            content << "<tr><td class=\"tn\"><a href=\"#" << id << "\">" << html_escape(v.name) << "</a>"
                    << " <span class=\"tag t-stat\">static</span></td>"
                    << "<td class=\"tt\">" << hcode(type_to_string_h(v.type)) << "</td>"
                    << "<td>" << vis_tag(v.visibility) << "</td>"
                    << "<td class=\"tk\">" << html_escape(compact_doc_brief_h(v.doc)) << "</td></tr>\n";
        }
        content << "</tbody></table>\n";
    }

    // ── Members (constructors, destructor, methods) summary ──
    content << "<h2 class=\"sh\">Members</h2>\n";
    if (ctors.empty() && !agg.destructor.has_value() && methods.empty()) {
        content << "<p class=\"empty\"><em>None.</em></p>\n";
    } else {
        content << "<table class=\"stbl\">\n"
                << "<thead><tr><th>Signature</th><th>Visibility</th><th>Brief</th></tr></thead>\n<tbody>\n";
        for (size_t i = 0; i < ctors.size(); ++i) {
            const auto& c = ctors[i];
            const std::string id = "ctor-" + std::to_string(i);
            content << "<tr><td class=\"ts\"><a href=\"#" << id << "\">"
                    << html_escape(make_sig(agg.name, c.params, nullptr)) << "</a></td>"
                    << "<td>" << vis_tag(c.visibility) << "</td>"
                    << "<td class=\"tk\">" << html_escape(compact_doc_brief_h(c.doc)) << "</td></tr>\n";
        }
        if (agg.destructor.has_value()) {
            content << "<tr><td class=\"ts\"><a href=\"#dtor\">"
                    << html_escape("~" + agg.name + "()") << "</a></td>"
                    << "<td>" << vis_tag(agg.destructor->visibility) << "</td>"
                    << "<td class=\"tk\">" << html_escape(compact_doc_brief_h(agg.destructor->doc)) << "</td></tr>\n";
        }
        for (size_t i = 0; i < methods.size(); ++i) {
            const auto& m = methods[i];
            const std::string id = "method-" + make_slug_h(m.name) + "-" + std::to_string(i);
            content << "<tr><td class=\"ts\"><a href=\"#" << id << "\">"
                    << html_escape(make_sig(m.name, m.params, &m.return_type)) << "</a></td>"
                    << "<td>" << vis_tag(m.visibility)
                    << (m.is_static ? " <span class=\"tag t-stat\">static</span>" : "")
                    << "</td>"
                    << "<td class=\"tk\">" << html_escape(compact_doc_brief_h(m.doc)) << "</td></tr>\n";
        }
        content << "</tbody></table>\n";
    }

    // ── Nested types ──
    content << "<h2 class=\"sh\">Nested Types</h2>\n";
    if (nested_types.empty()) {
        content << "<p class=\"empty\"><em>None.</em></p>\n";
    } else {
        content << "<table class=\"stbl\">\n"
                << "<thead><tr><th>Name</th><th>Brief</th></tr></thead>\n<tbody>\n";
        for (const auto& [name, link] : nested_types) {
            std::string brief;
            for (const auto& n : agg.nested) {
                if (n.name == name) {
                    brief = compact_doc_brief_h(n.doc);
                    break;
                }
            }
            if (brief.empty()) {
                for (const auto& n : agg.nested_unions) {
                    if (n.name == name) {
                        brief = compact_doc_brief_h(n.doc);
                        break;
                    }
                }
            }
            content << "<tr><td class=\"tn\"><a href=\"" << link << "\">"
                    << html_escape(name) << "</a></td>"
                    << "<td class=\"tk\">" << html_escape(brief) << "</td></tr>\n";
        }
        content << "</tbody></table>\n";
    }

    // ── Member Variable Details ──
    if (!fields.empty() || !svars.empty()) {
        content << "<h2 class=\"sh\">Member Variable Details</h2>\n";
        for (size_t i = 0; i < fields.size(); ++i) {
            const auto& f = fields[i];
            const std::string id = "field-" + make_slug_h(f.name) + "-" + std::to_string(i);
            content << "<div class=\"dblock\" id=\"" << id << "\">\n"
                    << "  <div class=\"dhead\">" << html_escape(f.name) << "</div>\n"
                    << "  <div class=\"dbody\">\n"
                    << "    <div class=\"meta\">"
                    << vis_tag(f.visibility)
                    << " <span>Type: " << hcode(type_to_string_h(f.type)) << "</span>"
                    << (f.is_const ? " <span class=\"tag t-const\">const</span>" : "")
                    << "</div>\n"
                    << "  </div>\n</div>\n";
        }
        for (size_t i = 0; i < svars.size(); ++i) {
            const auto& v = svars[i];
            const std::string id = "svar-" + make_slug_h(v.name) + "-" + std::to_string(i);
            content << "<div class=\"dblock\" id=\"" << id << "\">\n"
                    << "  <div class=\"dhead\">" << html_escape(v.name)
                    << " <span class=\"tag t-stat\">static</span></div>\n"
                    << "  <div class=\"dbody\">\n"
                    << "    <div class=\"meta\">"
                    << vis_tag(v.visibility)
                    << " <span>Type: " << hcode(type_to_string_h(v.type)) << "</span>"
                    << "</div>\n";
            append_doc_block_html(content, v.doc);
            content << "  </div>\n</div>\n";
        }
    }

    // ── Member Details ──
    if (!ctors.empty() || agg.destructor.has_value() || !methods.empty()) {
        content << "<h2 class=\"sh\">Member Details</h2>\n";
        for (size_t i = 0; i < ctors.size(); ++i) {
            const auto& c = ctors[i];
            const std::string id = "ctor-" + std::to_string(i);
            content << "<div class=\"dblock\" id=\"" << id << "\">\n"
                    << "  <div class=\"dhead\">"
                    << html_escape(make_sig(agg.name, c.params, nullptr)) << "</div>\n"
                    << "  <div class=\"dbody\">\n"
                    << "    <div class=\"meta\">" << vis_tag(c.visibility) << "</div>\n";
            append_doc_function_html(content, c.doc);
            content << "  </div>\n</div>\n";
        }
        if (agg.destructor.has_value()) {
            content << "<div class=\"dblock\" id=\"dtor\">\n"
                    << "  <div class=\"dhead\">" << html_escape("~" + agg.name + "()") << "</div>\n"
                    << "  <div class=\"dbody\">\n"
                    << "    <div class=\"meta\">" << vis_tag(agg.destructor->visibility) << "</div>\n";
            append_doc_function_html(content, agg.destructor->doc);
            content << "  </div>\n</div>\n";
        }
        for (size_t i = 0; i < methods.size(); ++i) {
            const auto& m = methods[i];
            const std::string id = "method-" + make_slug_h(m.name) + "-" + std::to_string(i);
            content << "<div class=\"dblock\" id=\"" << id << "\">\n"
                    << "  <div class=\"dhead\">"
                    << html_escape(make_sig(m.name, m.params, &m.return_type)) << "</div>\n"
                    << "  <div class=\"dbody\">\n"
                    << "    <div class=\"meta\">"
                    << vis_tag(m.visibility)
                    << (m.is_static ? " <span class=\"tag t-stat\">static</span>" : "")
                    << "</div>\n";
            append_doc_function_html(content, m.doc);
            content << "  </div>\n</div>\n";
        }
    }
}

/**
 * Emit symbol-reference entries (for typed-references.md) for an aggregate's
 * fields/static variables/constructors/destructor/methods. Shared between
 * regular aggregate pages and template aggregate pages.
 */
static void add_aggregate_member_refs_html(std::vector<symbol_ref_html>& refs,
                                            const kdi_aggregate& agg,
                                            const std::string& type_link,
                                            const std::string& fq_scope)
{
    std::vector<kdi_layout_member> fields;
    for (const auto& f : agg.layout)
        if (auto* m = std::get_if<kdi_layout_member>(&f)) fields.push_back(*m);
    std::sort(fields.begin(), fields.end(), [](const auto& a, const auto& b) { return a.name < b.name; });

    std::vector<kdi_variable> svars = agg.static_vars;
    std::sort(svars.begin(), svars.end(), [](const auto& a, const auto& b) { return a.name < b.name; });

    std::vector<kdi_constructor> ctors = agg.constructors;
    std::sort(ctors.begin(), ctors.end(), [](const auto& a, const auto& b) {
        if (a.params.size() != b.params.size()) return a.params.size() < b.params.size();
        return a.mangled_name < b.mangled_name;
    });

    std::vector<kdi_method> methods = agg.methods;
    methods.erase(std::remove_if(methods.begin(), methods.end(), [](const auto& m) {
        return m.template_origin.has_value();
    }), methods.end());
    std::sort(methods.begin(), methods.end(), [](const auto& a, const auto& b) {
        if (a.name != b.name) return a.name < b.name;
        return a.mangled_name < b.mangled_name;
    });

    for (size_t i = 0; i < fields.size(); ++i) {
        const auto& f = fields[i];
        add_ref(refs, "field", f.name, fq_scope, type_to_string_h(f.type),
                type_link + "#field-" + make_slug_h(f.name) + "-" + std::to_string(i));
    }
    for (size_t i = 0; i < svars.size(); ++i) {
        const auto& v = svars[i];
        add_ref(refs,
                "static-variable",
                v.name,
                fq_scope,
                type_to_string_h(v.type),
                type_link + "#svar-" + make_slug_h(v.name) + "-" + std::to_string(i),
                compact_doc_brief_h(v.doc));
    }
    for (size_t i = 0; i < methods.size(); ++i) {
        const auto& m = methods[i];
        add_ref(refs, "method", operator_display_name_h(m.name), fq_scope,
                make_sig(m.name, m.params, &m.return_type),
                type_link + "#method-" + make_slug_h(m.name) + "-" + std::to_string(i),
                compact_doc_brief_h(m.doc));
    }
    for (size_t i = 0; i < ctors.size(); ++i) {
        add_ref(refs, "constructor", agg.name, fq_scope,
                make_sig(agg.name, ctors[i].params, nullptr),
                type_link + "#ctor-" + std::to_string(i),
                compact_doc_brief_h(ctors[i].doc));
    }
    if (agg.destructor.has_value())
        add_ref(refs, "destructor", "~" + agg.name, fq_scope,
                "destructor", type_link + "#dtor", compact_doc_brief_h(agg.destructor->doc));
}

static bool write_template_def_page_html(const kdi_template_def& def,
                                          const html_ctx& ctx,
                                          const fs::path& ns_dir,
                                          std::vector<symbol_ref_html>& refs,
                                          std::string* err)
{
    const fs::path file_path = ns_dir / (def.name + ".html");
    const std::string param_suffix = template_param_names_str_h(def.params);

    std::vector<std::pair<std::string,std::string>> bc_parts;
    const std::string rel_scope = strip_pfx(parent_scope_h(def.fq_name), ctx.root_fq);
    bc_parts.emplace_back(ctx.module_name, rel_link_h(ns_dir, ctx.module_root / "index.html"));
    if (!rel_scope.empty()) {
        std::string built;
        for (const auto& part : split_scope_h(rel_scope)) {
            built += (built.empty() ? "" : "/") + part;
            bc_parts.emplace_back(part, rel_link_h(ns_dir, ctx.module_root / built / "index.html"));
        }
    }
    bc_parts.emplace_back(def.name + param_suffix, "");

    std::ostringstream content;
    content << "<h1 class=\"ptitle\">" << html_escape(def.name + param_suffix)
            << kind_badge("template " + def.entity_kind) << "</h1>\n";

    content << "<div class=\"ov\"><table>\n"
            << "<tr><th>Property</th><th>Value</th></tr>\n"
            << "<tr><td>Fully qualified name</td><td>" << hcode(def.fq_name + param_suffix) << "</td></tr>\n"
            << "<tr><td>Visibility</td><td>" << vis_tag_str(def.visibility) << "</td></tr>\n"
            << "<tr><td>Generic</td><td>" << hcode(def.is_generic ? "true" : "false") << "</td></tr>\n";
    if (!def.origin_module.empty())
        content << "<tr><td>Origin module</td><td>" << hcode(def.origin_module) << "</td></tr>\n";
    content << "</table></div>\n";

    content << "<h2 class=\"sh\">Template Parameters</h2>\n";
    if (def.params.empty()) {
        content << "<p class=\"empty\"><em>None.</em></p>\n";
    } else {
        content << "<table class=\"stbl\">\n"
                << "<thead><tr><th>Name</th><th>Kind</th><th>Constraint / Value type</th><th>Default</th></tr></thead>\n<tbody>\n";
        for (const auto& p : def.params) {
            std::string constraint;
            if (p.constraint_type) constraint = type_to_string_h(*p.constraint_type);
            else if (p.value_type) constraint = type_to_string_h(*p.value_type);
            std::string default_val;
            if (p.default_type) default_val = type_to_string_h(*p.default_type);
            else if (p.default_value) default_val = *p.default_value;
            content << "<tr><td class=\"tn\">" << html_escape(p.name) << "</td>"
                    << "<td class=\"tk\">" << html_escape(p.kind) << "</td>"
                    << "<td class=\"tt\">" << (constraint.empty() ? "-" : hcode(constraint)) << "</td>"
                    << "<td class=\"tt\">" << (default_val.empty() ? "-" : hcode(default_val)) << "</td></tr>\n";
        }
        content << "</tbody></table>\n";
    }

    // Structured rendering: an aggregate-kind template documents its
    // fields/constructors/methods with the exact same sections as a regular
    // (non-template) aggregate page — template-parameter-dependent types are
    // tagged distinctly (kdi_template_param_ref / kdi_generic_ref_type)
    // rather than collapsed into an opaque source dump. A function-kind
    // template gets an equivalent Signature/Parameters/Return type rendering.
    if (def.aggregate_signature) {
        write_aggregate_body_html(content, *def.aggregate_signature, def.name);
    } else if (def.function_signature) {
        content << "<h2 class=\"sh\">Signature</h2>\n";
        content << "<p>" << hcode(make_sig(def.function_signature->name,
                                          def.function_signature->params,
                                          &def.function_signature->return_type)) << "</p>\n";
        content << "<h2 class=\"sh\">Parameters</h2>\n";
        if (def.function_signature->params.empty()) {
            content << "<p class=\"empty\"><em>None.</em></p>\n";
        } else {
            content << "<table class=\"stbl\">\n"
                    << "<thead><tr><th>Name</th><th>Type</th></tr></thead>\n<tbody>\n";
            for (const auto& p : def.function_signature->params)
                content << "<tr><td class=\"tn\">" << html_escape(p.name) << "</td>"
                        << "<td class=\"tt\">" << hcode(type_to_string_h(p.type)) << "</td></tr>\n";
            content << "</tbody></table>\n";
        }
        content << "<h2 class=\"sh\">Return Type</h2>\n";
        content << "<p>" << hcode(type_to_string_h(def.function_signature->return_type)) << "</p>\n";
        append_doc_function_html(content, def.function_signature->doc);
    }

    // The raw K declaration source is kept as a supplementary reference
    // (default member initializers, annotations, and other syntax not
    // captured by the structured signature above are only visible there).
    // Generic templates are signature-only and have no re-emittable source.
    if (!def.source.empty()) {
        content << "<h2 class=\"sh\">Declaration Source</h2>\n";
        content << "<pre class=\"src-block\">" << html_escape(def.source) << "</pre>\n";
    } else if (!def.aggregate_signature && !def.function_signature) {
        content << "<p class=\"empty\"><em>No declaration source or signature available for this template.</em></p>\n";
    }

    const std::string page_html = make_page(ctx, ns_dir,
        def.name + param_suffix + " — template " + def.entity_kind, make_breadcrumbs(bc_parts), content.str());

    if (!write_file_h(file_path, page_html, err)) return false;

    const std::string link = rel_link_h(ctx.module_root, file_path);
    const std::string scope = strip_pfx(parent_scope_h(def.fq_name), ctx.root_fq);
    add_ref(refs, "template " + def.entity_kind, def.name + param_suffix, scope,
            "template" + template_params_str_h(def.params), link);

    if (def.aggregate_signature) {
        add_aggregate_member_refs_html(refs, *def.aggregate_signature, link,
                                       strip_pfx(def.fq_name, ctx.root_fq));
    }

    return true;
}

static bool write_union_page_html(const kdi_union& un,
                                   const html_ctx& ctx,
                                   const fs::path& ns_dir,
                                   std::vector<symbol_ref_html>& refs,
                                   std::string* err,
                                   const std::string& file_stem)
{
    const fs::path file_path = ns_dir / (file_stem + ".html");

    std::vector<std::pair<std::string,std::string>> bc_parts;
    const std::string rel_scope = strip_pfx(parent_scope_h(un.fq_name), ctx.root_fq);
    bc_parts.emplace_back(ctx.module_name, rel_link_h(ns_dir, ctx.module_root / "index.html"));
    if (!rel_scope.empty()) {
        std::string built;
        for (const auto& part : split_scope_h(rel_scope)) {
            built += (built.empty() ? "" : "/") + part;
            bc_parts.emplace_back(part, rel_link_h(ns_dir, ctx.module_root / built / "index.html"));
        }
    }
    bc_parts.emplace_back(un.name, "");

    std::ostringstream content;
    content << "<h1 class=\"ptitle\">" << html_escape(un.name)
            << kind_badge("union") << "</h1>\n";

    content << "<div class=\"ov\"><table>\n"
            << "<tr><th>Property</th><th>Value</th></tr>\n"
            << "<tr><td>Fully qualified name</td><td>" << hcode(un.fq_name) << "</td></tr>\n"
            << "<tr><td>Visibility</td><td>" << vis_tag(un.visibility) << "</td></tr>\n"
            << "</table></div>\n";

    append_doc_block_html(content, un.doc);

    std::vector<kdi_union_alternative> alts = un.alternatives;
    std::sort(alts.begin(), alts.end(), [](const auto& a, const auto& b) { return a.name < b.name; });

    content << "<h2 class=\"sh\">Alternatives</h2>\n";
    if (alts.empty()) {
        content << "<p class=\"empty\"><em>None.</em></p>\n";
    } else {
        content << "<table class=\"stbl\">\n"
                << "<thead><tr><th>Name</th><th>Type</th><th>Const</th></tr></thead>\n<tbody>\n";
        for (size_t i = 0; i < alts.size(); ++i) {
            const auto& alt = alts[i];
            const std::string anchor = "alt-" + make_slug_h(alt.name) + "-" + std::to_string(i);
            content << "<tr>"
                    << "<td class=\"tn\"><a href=\"#" << anchor << "\">" << html_escape(alt.name) << "</a></td>"
                    << "<td class=\"tt\">" << hcode(type_to_string_h(alt.type)) << "</td>"
                    << "<td>" << (alt.is_const ? "<span class=\"tag t-const\">const</span>" : "") << "</td>"
                    << "</tr>\n";
        }
        content << "</tbody></table>\n";

        content << "<h2 class=\"sh\">Alternative Details</h2>\n";
        for (size_t i = 0; i < alts.size(); ++i) {
            const auto& alt = alts[i];
            const std::string anchor = "alt-" + make_slug_h(alt.name) + "-" + std::to_string(i);
            content << "<div class=\"dblock\" id=\"" << anchor << "\">\n"
                    << "  <div class=\"dhead\">" << html_escape(alt.name) << "</div>\n"
                    << "  <div class=\"dbody\">\n"
                    << "    <div class=\"meta\">"
                    << "<span>Type: " << hcode(type_to_string_h(alt.type)) << "</span>"
                    << (alt.is_const ? " <span class=\"tag t-const\">const</span>" : "")
                    << "</div>\n"
                    << "  </div>\n"
                    << "</div>\n";
        }
    }

    const std::string page_html = make_page(ctx, ns_dir,
        un.name + " — union", make_breadcrumbs(bc_parts), content.str());

    if (!write_file_h(file_path, page_html, err)) return false;

    const std::string link = rel_link_h(ctx.module_root, file_path);
    const std::string scope = strip_pfx(parent_scope_h(un.fq_name), ctx.root_fq);
    add_ref(refs, "union", un.name, scope, "union", link, compact_doc_brief_h(un.doc));

    for (size_t i = 0; i < alts.size(); ++i) {
        const auto& alt = alts[i];
        add_ref(refs, "union-alternative", alt.name, strip_pfx(un.fq_name, ctx.root_fq),
                type_to_string_h(alt.type),
                link + "#alt-" + make_slug_h(alt.name) + "-" + std::to_string(i));
    }
    return true;
}

// Forward declaration
static bool write_aggregate_page_html(const kdi_aggregate& agg,
                                       const html_ctx& ctx,
                                       const fs::path& ns_dir,
                                       const std::string& file_stem,
                                       std::vector<symbol_ref_html>& refs,
                                       std::string* err);

static bool write_aggregate_nested_html(const kdi_aggregate& agg,
                                         const html_ctx& ctx,
                                         const fs::path& ns_dir,
                                         const std::string& name_prefix,
                                         std::vector<symbol_ref_html>& refs,
                                         std::string* err)
{
    const std::string stem = name_prefix.empty() ? agg.name : name_prefix + "." + agg.name;
    if (!write_aggregate_page_html(agg, ctx, ns_dir, stem, refs, err)) return false;

    for (const auto& nested : agg.nested) {
        if (nested.template_origin.has_value())
            continue; // Skip synthesized template instantiations - not useful in docs.
        if (!write_aggregate_nested_html(nested, ctx, ns_dir, stem, refs, err)) return false;
    }

    for (const auto& un : agg.nested_unions) {
        if (un.template_origin.has_value())
            continue; // Skip synthesized template instantiations - not useful in docs.
        const std::string ustem = stem + "." + un.name;
        if (!write_union_page_html(un, ctx, ns_dir, refs, err, ustem)) return false;
    }
    return true;
}

static bool write_aggregate_page_html(const kdi_aggregate& agg,
                                       const html_ctx& ctx,
                                       const fs::path& ns_dir,
                                       const std::string& file_stem,
                                       std::vector<symbol_ref_html>& refs,
                                       std::string* err)
{
    const fs::path file_path = ns_dir / (file_stem + ".html");
    const std::string kind = agg_kind_str(agg.kind);

    // Breadcrumbs
    std::vector<std::pair<std::string,std::string>> bc_parts;
    const std::string rel_scope = strip_pfx(parent_scope_h(agg.fq_name), ctx.root_fq);
    bc_parts.emplace_back(ctx.module_name, rel_link_h(ns_dir, ctx.module_root / "index.html"));
    if (!rel_scope.empty()) {
        std::string built;
        for (const auto& part : split_scope_h(rel_scope)) {
            built += (built.empty() ? "" : "/") + part;
            bc_parts.emplace_back(part, rel_link_h(ns_dir, ctx.module_root / built / "index.html"));
        }
    }
    bc_parts.emplace_back(agg.name, "");

    std::ostringstream content;
    content << "<h1 class=\"ptitle\">" << html_escape(agg.name)
            << kind_badge(kind) << "</h1>\n";

    // Overview card
    content << "<div class=\"ov\"><table>\n"
            << "<tr><th>Property</th><th>Value</th></tr>\n"
            << "<tr><td>Kind</td><td>" << hcode(kind) << "</td></tr>\n"
            << "<tr><td>Fully qualified name</td><td>" << hcode(agg.fq_name) << "</td></tr>\n"
            << "<tr><td>Visibility</td><td>" << vis_tag(agg.visibility) << "</td></tr>\n"
            << "<tr><td>Abstract</td><td>" << (agg.is_abstract ? "<span class=\"tag t-abs\">abstract</span>" : "<em>no</em>") << "</td></tr>\n"
            << "<tr><td>Final</td><td>" << (agg.is_final ? "<span class=\"tag t-fin\">final</span>" : "<em>no</em>") << "</td></tr>\n"
            << "</table></div>\n";

    append_doc_block_html(content, agg.doc);

    write_aggregate_body_html(content, agg, file_stem);

    const std::string page_html = make_page(ctx, ns_dir,
        agg.name + " — " + kind, make_breadcrumbs(bc_parts), content.str());

    if (!write_file_h(file_path, page_html, err)) return false;

    // Register references
    const std::string type_link = rel_link_h(ctx.module_root, file_path);
    const std::string type_scope = strip_pfx(parent_scope_h(agg.fq_name), ctx.root_fq);
    add_ref(refs, kind, agg.name, type_scope, kind, type_link, compact_doc_brief_h(agg.doc));

    add_aggregate_member_refs_html(refs, agg, type_link, strip_pfx(agg.fq_name, ctx.root_fq));

    return true;
}

static bool write_namespace_tree_html(const kdi_namespace& ns,
                                       const html_ctx& ctx,
                                       std::vector<symbol_ref_html>& refs,
                                       std::string* err,
                                       bool is_root,
                                       const kdi_file* file)
{
    const fs::path ns_dir = ns_dir_from_root(ctx, ns);

    std::error_code ec;
    fs::create_directories(ns_dir, ec);
    if (ec) {
        if (err) *err = "cannot create directory '" + ns_dir.string() + "': " + ec.message();
        return false;
    }

    // Sort children (dropping inert self/import "mirror" namespaces whose fq_name
    // fails to properly extend root_fq — see kdi_docgen.cpp for full rationale;
    // writing them would collide with and overwrite the current namespace's own page)
    auto child_ns = ns.namespaces;
    child_ns.erase(std::remove_if(child_ns.begin(), child_ns.end(), [&](const kdi_namespace& c) {
        return strip_pfx(c.fq_name, ctx.root_fq).empty();
    }), child_ns.end());
    std::sort(child_ns.begin(), child_ns.end(), [](const auto& a, const auto& b) { return a.name < b.name; });

    auto aggregates = ns.aggregates;
    aggregates.erase(std::remove_if(aggregates.begin(), aggregates.end(), [](const auto& a) {
        return a.template_origin.has_value();
    }), aggregates.end());
    std::sort(aggregates.begin(), aggregates.end(), [](const auto& a, const auto& b) { return a.name < b.name; });

    auto enums = ns.enums;
    std::sort(enums.begin(), enums.end(), [](const auto& a, const auto& b) { return a.name < b.name; });

    auto unions = ns.unions;
    unions.erase(std::remove_if(unions.begin(), unions.end(), [](const auto& u) {
        return u.template_origin.has_value();
    }), unions.end());
    std::sort(unions.begin(), unions.end(), [](const auto& a, const auto& b) { return a.name < b.name; });

    auto functions = ns.functions;
    functions.erase(std::remove_if(functions.begin(), functions.end(), [](const auto& f) {
        return f.template_origin.has_value();
    }), functions.end());
    std::sort(functions.begin(), functions.end(), [](const auto& a, const auto& b) {
        if (a.name != b.name) return a.name < b.name;
        return a.mangled_name < b.mangled_name;
    });

    auto variables = ns.variables;
    std::sort(variables.begin(), variables.end(), [](const auto& a, const auto& b) { return a.name < b.name; });

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

    auto template_defs = ns.template_defs;
    template_defs.erase(std::remove_if(template_defs.begin(), template_defs.end(), [&](const auto& td) {
        const std::string parent = parent_scope_h(td.fq_name);
        const auto pos = parent.rfind("::");
        const std::string parent_short = pos == std::string::npos ? parent : parent.substr(pos + 2);
        return instantiation_names.count(parent_short) > 0;
    }), template_defs.end());
    std::sort(template_defs.begin(), template_defs.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
    std::vector<kdi_template_def> function_templates;
    for (const auto& td : template_defs)
        if (td.entity_kind == "function")
            function_templates.push_back(td);

    // Breadcrumbs
    std::vector<std::pair<std::string,std::string>> bc_parts;
    if (!is_root) {
        bc_parts.emplace_back(ctx.module_name, rel_link_h(ns_dir, ctx.module_root / "index.html"));
        const std::string rel_scope = strip_pfx(ns.fq_name, ctx.root_fq);
        std::string built;
        auto parts = split_scope_h(rel_scope);
        for (size_t i = 0; i < parts.size(); ++i) {
            built += (built.empty() ? "" : "/") + parts[i];
            if (i + 1 < parts.size())
                bc_parts.emplace_back(parts[i],
                    rel_link_h(ns_dir, ctx.module_root / built / "index.html"));
            else
                bc_parts.emplace_back(parts[i], "");
        }
    } else {
        bc_parts.emplace_back(ctx.module_name, "");
    }

    // Build content
    std::ostringstream content;

    // Module header on root page
    if (is_root && file) {
        content << "<h1 class=\"ptitle\">Module " << html_escape(ctx.module_name)
                << kind_badge("module") << "</h1>\n";

        content << "<div class=\"ov\"><table>\n"
                << "<tr><th>Property</th><th>Value</th></tr>\n";
        content << "<tr><td>Schema</td><td>" << hcode(std::to_string(file->header.schema_major)
                + "." + std::to_string(file->header.schema_minor)) << "</td></tr>\n";
        if (!file->header.lib_base.empty())
            content << "<tr><td>Library base</td><td>" << hcode(file->header.lib_base) << "</td></tr>\n";
        if (!file->header.lib_path.empty())
            content << "<tr><td>Library path</td><td>" << hcode(file->header.lib_path) << "</td></tr>\n";
        if (!file->header.target_triple.empty())
            content << "<tr><td>Target</td><td>" << hcode(file->header.target_triple) << "</td></tr>\n";
        if (!file->header.compiler_ver.empty())
            content << "<tr><td>Compiler</td><td>" << hcode(file->header.compiler_ver) << "</td></tr>\n";
        content << "</table></div>\n";

        append_doc_block_html(content, ns.doc);
    } else {
        const std::string ns_display = strip_pfx(ns.fq_name, ctx.root_fq);
        content << "<h1 class=\"ptitle\">Namespace "
                << html_escape(ns_display.empty() ? ctx.root_fq : ns_display)
                << kind_badge("namespace") << "</h1>\n";
        append_doc_block_html(content, ns.doc);
    }

    // ── Namespaces ──
    content << "<h2 class=\"sh\">Namespaces</h2>\n";
    if (child_ns.empty()) {
        content << "<p class=\"empty\"><em>None.</em></p>\n";
    } else {
        content << "<table class=\"stbl\">\n"
                << "<thead><tr><th>Name</th><th>Brief</th></tr></thead>\n<tbody>\n";
        for (const auto& child : child_ns) {
            const fs::path child_idx = ns_dir_from_root(ctx, child) / "index.html";
            content << "<tr><td class=\"tn\"><a href=\"" << rel_link_h(ns_dir, child_idx)
                    << "\">" << html_escape(child.name) << "</a></td>"
                    << "<td class=\"tk\">" << html_escape(compact_doc_brief_h(child.doc)) << "</td></tr>\n";
        }
        content << "</tbody></table>\n";
    }

    // ── Types ──
    struct type_row { std::string name; std::string kind; std::string file; std::string brief; };
    std::vector<type_row> rows;
    for (const auto& a : aggregates) rows.push_back({a.name, agg_kind_str(a.kind), a.name + ".html", compact_doc_brief_h(a.doc)});
    for (const auto& e : enums)      rows.push_back({e.name, "enum",  e.name + ".html", compact_doc_brief_h(e.doc)});
    for (const auto& u : unions)     rows.push_back({u.name, "union", u.name + ".html", compact_doc_brief_h(u.doc)});
    for (const auto& td : template_defs)
        if (td.entity_kind != "function")
            rows.push_back({td.name + template_param_names_str_h(td.params),
                            "template " + td.entity_kind, td.name + ".html", std::string()});
    std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.name < b.name; });

    content << "<h2 class=\"sh\">Types</h2>\n";
    if (rows.empty()) {
        content << "<p class=\"empty\"><em>None.</em></p>\n";
    } else {
        content << "<table class=\"stbl\">\n"
                << "<thead><tr><th>Name</th><th>Kind</th><th>Brief</th></tr></thead>\n<tbody>\n";
        for (const auto& row : rows)
            content << "<tr><td class=\"tn\"><a href=\"" << row.file << "\">"
                    << html_escape(row.name) << "</a></td>"
                    << "<td class=\"tk\">" << kind_badge(row.kind) << "</td>"
                    << "<td class=\"tk\">" << html_escape(row.brief) << "</td></tr>\n";
        content << "</tbody></table>\n";
    }

    // ── Functions ──
    std::vector<std::string> fn_ids(functions.size());
    content << "<h2 class=\"sh\">Global Functions</h2>\n";
    if (functions.empty()) {
        content << "<p class=\"empty\"><em>None.</em></p>\n";
    } else {
        content << "<table class=\"stbl\">\n"
                << "<thead><tr><th>Signature</th><th>Visibility</th><th>Brief</th></tr></thead>\n<tbody>\n";
        for (size_t i = 0; i < functions.size(); ++i) {
            const auto& fn = functions[i];
            fn_ids[i] = "fn-" + make_slug_h(fn.name) + "-" + std::to_string(i);
            content << "<tr><td class=\"ts\"><a href=\"#" << fn_ids[i] << "\">"
                    << html_escape(make_sig(fn.name, fn.params, &fn.return_type)) << "</a></td>"
                    << "<td>" << vis_tag(fn.visibility)
                    << (fn.is_static ? " <span class=\"tag t-stat\">static</span>" : "")
                    << "</td>"
                    << "<td class=\"tk\">" << html_escape(compact_doc_brief_h(fn.doc)) << "</td></tr>\n";
        }
        content << "</tbody></table>\n";
    }

    // ── Function Templates ──
    content << "<h2 class=\"sh\">Function Templates</h2>\n";
    if (function_templates.empty()) {
        content << "<p class=\"empty\"><em>None.</em></p>\n";
    } else {
        content << "<table class=\"stbl\">\n"
                << "<thead><tr><th>Name</th><th>Parameters</th></tr></thead>\n<tbody>\n";
        for (const auto& td : function_templates)
            content << "<tr><td class=\"tn\"><a href=\"" << td.name << ".html\">"
                    << html_escape(td.name + template_param_names_str_h(td.params)) << "</a></td>"
                    << "<td class=\"tt\">" << hcode(template_params_str_h(td.params)) << "</td></tr>\n";
        content << "</tbody></table>\n";
    }

    // ── Variables ──
    std::vector<std::string> var_ids(variables.size());
    content << "<h2 class=\"sh\">Global Variables</h2>\n";
    if (variables.empty()) {
        content << "<p class=\"empty\"><em>None.</em></p>\n";
    } else {
        content << "<table class=\"stbl\">\n"
                << "<thead><tr><th>Name</th><th>Type</th><th>Visibility</th><th>Brief</th></tr></thead>\n<tbody>\n";
        for (size_t i = 0; i < variables.size(); ++i) {
            const auto& v = variables[i];
            var_ids[i] = "var-" + make_slug_h(v.name) + "-" + std::to_string(i);
            content << "<tr><td class=\"tn\"><a href=\"#" << var_ids[i] << "\">"
                    << html_escape(v.name) << "</a></td>"
                    << "<td class=\"tt\">" << hcode(type_to_string_h(v.type)) << "</td>"
                    << "<td>" << vis_tag(v.visibility) << "</td>"
                    << "<td class=\"tk\">" << html_escape(compact_doc_brief_h(v.doc)) << "</td></tr>\n";
        }
        content << "</tbody></table>\n";
    }

    // ── Function Details ──
    if (!functions.empty()) {
        content << "<h2 class=\"sh\">Function Details</h2>\n";
        for (size_t i = 0; i < functions.size(); ++i) {
            const auto& fn = functions[i];
            content << "<div class=\"dblock\" id=\"" << fn_ids[i] << "\">\n"
                    << "  <div class=\"dhead\">"
                    << html_escape(make_sig(fn.name, fn.params, &fn.return_type)) << "</div>\n"
                    << "  <div class=\"dbody\">\n"
                    << "    <div class=\"meta\">"
                    << vis_tag(fn.visibility)
                    << (fn.is_static ? " <span class=\"tag t-stat\">static</span>" : "")
                    << "</div>\n";
            append_doc_function_html(content, fn.doc);
            content << "  </div>\n</div>\n";
        }
    }

    // ── Variable Details ──
    if (!variables.empty()) {
        content << "<h2 class=\"sh\">Variable Details</h2>\n";
        for (size_t i = 0; i < variables.size(); ++i) {
            const auto& v = variables[i];
            content << "<div class=\"dblock\" id=\"" << var_ids[i] << "\">\n"
                    << "  <div class=\"dhead\">" << html_escape(v.name) << "</div>\n"
                    << "  <div class=\"dbody\">\n"
                    << "    <div class=\"meta\">"
                    << vis_tag(v.visibility)
                    << (v.is_const ? " <span class=\"tag t-const\">const</span>" : "")
                    << " <span>Type: " << hcode(type_to_string_h(v.type)) << "</span>"
                    << "</div>\n";
            append_doc_block_html(content, v.doc);
            content << "  </div>\n</div>\n";
        }
    }

    const std::string page_html = make_page(ctx, ns_dir,
        is_root ? "Module " + ctx.module_name : "Namespace " + ns.name,
        make_breadcrumbs(bc_parts), content.str());

    if (!write_file_h(ns_dir / "index.html", page_html, err)) return false;

    // Register namespace reference
    const std::string ns_name = ns.name.empty() ? ctx.root_fq : ns.name;
    add_ref(refs, "namespace", ns_name,
            strip_pfx(parent_scope_h(ns.fq_name), ctx.root_fq), "namespace",
            rel_link_h(ctx.module_root, ns_dir / "index.html"),
            compact_doc_brief_h(ns.doc));

    // Register function/variable references
    for (size_t i = 0; i < functions.size(); ++i) {
        const auto& fn = functions[i];
        add_ref(refs, "function", fn.name,
                strip_pfx(parent_scope_h(fn.fq_name), ctx.root_fq),
                make_sig(fn.name, fn.params, &fn.return_type),
                rel_link_h(ctx.module_root, ns_dir / "index.html") + "#" + fn_ids[i],
                compact_doc_brief_h(fn.doc));
    }
    for (size_t i = 0; i < variables.size(); ++i) {
        const auto& v = variables[i];
        add_ref(refs, "variable", v.name,
                strip_pfx(parent_scope_h(v.fq_name), ctx.root_fq),
                type_to_string_h(v.type),
                rel_link_h(ctx.module_root, ns_dir / "index.html") + "#" + var_ids[i],
                compact_doc_brief_h(v.doc));
    }

    // Write type pages
    for (const auto& agg : aggregates)
        if (!write_aggregate_nested_html(agg, ctx, ns_dir, "", refs, err)) return false;
    for (const auto& en : enums)
        if (!write_enum_page_html(en, ctx, ns_dir, refs, err)) return false;
    for (const auto& un : unions)
        if (!write_union_page_html(un, ctx, ns_dir, refs, err, un.name)) return false;
    for (const auto& td : template_defs)
        if (!write_template_def_page_html(td, ctx, ns_dir, refs, err)) return false;

    // Recurse into child namespaces
    for (const auto& child : child_ns)
        if (!write_namespace_tree_html(child, ctx, refs, err, false, file)) return false;

    return true;
}

static bool write_reference_indexes_html(const html_ctx& ctx,
                                          std::vector<symbol_ref_html> refs,
                                          std::string* err)
{
    // ── Name References ──
    std::sort(refs.begin(), refs.end(), [](const auto& a, const auto& b) {
        if (a.name != b.name) return a.name < b.name;
        if (a.scope != b.scope) return a.scope < b.scope;
        return a.kind < b.kind;
    });

    {
        std::ostringstream content;
        std::vector<std::pair<std::string,std::string>> bc = {
            {ctx.module_name, "index.html"}, {"Name References", ""}};

        content << "<h1 class=\"ptitle\">Name References</h1>\n"
                << "<p class=\"doc-brief\">All public symbols sorted by name. "
                << "Total: <code>" << refs.size() << "</code> symbol(s).</p>\n"
                << "<h2 class=\"sh\">All Symbols</h2>\n"
                << "<table class=\"stbl\">\n"
                << "<thead><tr><th>Name</th><th>Kind</th><th>Scope</th><th>Type</th><th>Brief</th></tr></thead>\n"
                << "<tbody>\n";
        for (const auto& r : refs)
            content << "<tr>"
                    << "<td class=\"tn\"><a href=\"" << r.link << "\">" << html_escape(r.name) << "</a></td>"
                    << "<td class=\"tk\">" << kind_badge(r.kind) << "</td>"
                    << "<td class=\"tk\">" << html_escape(r.scope.empty() ? "<root>" : r.scope) << "</td>"
                    << "<td class=\"tt\">" << hcode(r.type_desc) << "</td>"
                    << "<td class=\"tk\">" << html_escape(r.brief) << "</td>"
                    << "</tr>\n";
        content << "</tbody></table>\n";

        const std::string page = make_page(ctx, ctx.module_root,
            "Name References", make_breadcrumbs(bc), content.str());
        if (!write_file_h(ctx.module_root / "name-references.html", page, err)) return false;
    }

    // ── Typed References ──
    std::sort(refs.begin(), refs.end(), [](const auto& a, const auto& b) {
        if (a.kind != b.kind) return a.kind < b.kind;
        if (a.name != b.name) return a.name < b.name;
        return a.scope < b.scope;
    });

    {
        std::ostringstream content;
        std::vector<std::pair<std::string,std::string>> bc = {
            {ctx.module_name, "index.html"}, {"Typed References", ""}};

        content << "<h1 class=\"ptitle\">Typed References</h1>\n"
                << "<p class=\"doc-brief\">All public symbols grouped by kind.</p>\n";

        std::string current_kind;
        for (const auto& r : refs) {
            if (r.kind != current_kind) {
                if (!current_kind.empty()) content << "</tbody></table>\n";
                current_kind = r.kind;
                content << "<h2 class=\"sh\">" << kind_badge(r.kind)
                        << " " << html_escape(r.kind) << "</h2>\n"
                        << "<table class=\"stbl\">\n"
                        << "<thead><tr><th>Name</th><th>Scope</th><th>Type</th><th>Brief</th></tr></thead>\n"
                        << "<tbody>\n";
            }
            content << "<tr>"
                    << "<td class=\"tn\"><a href=\"" << r.link << "\">" << html_escape(r.name) << "</a></td>"
                    << "<td class=\"tk\">" << html_escape(r.scope.empty() ? "<root>" : r.scope) << "</td>"
                    << "<td class=\"tt\">" << hcode(r.type_desc) << "</td>"
                    << "<td class=\"tk\">" << html_escape(r.brief) << "</td>"
                    << "</tr>\n";
        }
        if (!current_kind.empty()) content << "</tbody></table>\n";

        const std::string page = make_page(ctx, ctx.module_root,
            "Typed References", make_breadcrumbs(bc), content.str());
        if (!write_file_h(ctx.module_root / "typed-references.html", page, err)) return false;
    }

    return true;
}

} // anonymous namespace

// ─── Public entry point ───────────────────────────────────────────────────────

bool kdi_generate_html_doc(const kdi_file& file,
                            const std::string& destination_dir,
                            std::string* error_message)
{
    try {
        // (Re)build the concrete-instantiation index used by type_to_string_h() so
        // that references to synthesized template instantiations render with
        // their real generic arguments (see collect_instantiation_origins_h()).
        g_instantiation_origins_h.clear();
        collect_instantiation_origins_h(file.unit.root_ns);

        const fs::path destination = destination_dir.empty()
                                         ? fs::path(".")
                                         : fs::path(destination_dir);

        std::error_code ec;
        if (fs::exists(destination, ec) && fs::is_regular_file(destination, ec)) {
            if (error_message)
                *error_message = "destination path is a file: '" + destination.string() + "'";
            return false;
        }

        fs::create_directories(destination, ec);
        if (ec) {
            if (error_message)
                *error_message = "cannot create destination directory '"
                                 + destination.string() + "': " + ec.message();
            return false;
        }

        if (file.header.module_name.empty()) {
            if (error_message) *error_message = "module name is empty in KDI header";
            return false;
        }

        const fs::path module_root = destination / file.header.module_name;
        fs::create_directories(module_root, ec);
        if (ec) {
            if (error_message)
                *error_message = "cannot create module directory '"
                                 + module_root.string() + "': " + ec.message();
            return false;
        }

        const std::string root_fq = !file.unit.root_ns.fq_name.empty()
                                        ? file.unit.root_ns.fq_name
                                        : file.header.module_name;

        // Build navigation context
        html_ctx ctx;
        ctx.module_name = file.header.module_name;
        ctx.module_root = module_root;
        ctx.root_fq     = root_fq;
        collect_ns_nav(file.unit.root_ns, root_fq, ctx.ns_nav, true);

        // Write CSS
        if (!write_file_h(module_root / "kdoc.css", KDOC_CSS, error_message)) return false;

        // Generate pages
        std::vector<symbol_ref_html> refs;
        if (!write_namespace_tree_html(file.unit.root_ns, ctx, refs, error_message, true, &file))
            return false;

        if (!write_reference_indexes_html(ctx, refs, error_message))
            return false;

        return true;
    } catch (const std::exception& e) {
        if (error_message) *error_message = e.what();
        return false;
    }
}

} // namespace kdi



