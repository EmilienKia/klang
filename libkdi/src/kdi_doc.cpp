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

#include "kdi_doc.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

namespace kdi {

using json = nlohmann::json;

namespace {

std::string trim_root_prefix(std::string value) {
    while (!value.empty() && value.front() == ':')
        value.erase(value.begin());
    return value;
}

std::string strip_root_namespace(const std::string& value, const std::string& root) {
    const std::string clean_value = trim_root_prefix(value);
    const std::string clean_root  = trim_root_prefix(root);
    if (clean_root.empty())
        return clean_value;
    if (clean_value == clean_root)
        return std::string{};
    if (clean_value.rfind(clean_root + "::", 0) == 0)
        return clean_value.substr(clean_root.size() + 2);
    return clean_value;
}

std::vector<std::string> make_search_keys(const std::string& symbol,
                                          const std::string& root)
{
    std::vector<std::string> keys;
    auto add = [&](std::string key) {
        if (key.empty())
            return;
        if (std::find(keys.begin(), keys.end(), key) == keys.end())
            keys.push_back(std::move(key));
    };

    add(trim_root_prefix(symbol));
    add(strip_root_namespace(symbol, root));
    return keys;
}

std::vector<std::string> make_entry_keys(const kdi_doc_symbol& symbol,
                                        const std::string& root)
{
    std::vector<std::string> keys;
    auto add = [&](std::string key) {
        if (key.empty())
            return;
        if (std::find(keys.begin(), keys.end(), key) == keys.end())
            keys.push_back(std::move(key));
    };

    add(trim_root_prefix(symbol.fq_name));
    add(strip_root_namespace(symbol.fq_name, root));
    add(trim_root_prefix(symbol.name));
    add(strip_root_namespace(symbol.name, root));
    return keys;
}

bool match_any(const std::vector<std::string>& a, const std::vector<std::string>& b) {
    for (const auto& x : a) {
        for (const auto& y : b) {
            if (x == y)
                return true;
        }
    }
    return false;
}

json block_doc_to_json(const kdi_doc_block& doc) {
    json j;
    j["brief"] = doc.brief;
    j["description"] = doc.description;
    return j;
}

json function_doc_to_json(const kdi_doc_function& doc) {
    json j = block_doc_to_json(static_cast<const kdi_doc_block&>(doc));

    if (!doc.params.empty()) {
        j["params"] = json::array();
        for (const auto& p : doc.params) {
            j["params"].push_back({
                {"name", p.name},
                {"description", p.description},
            });
        }
    }
    if (doc.returns.has_value())
        j["returns"] = *doc.returns;
    if (!doc.throws.empty()) {
        j["throws"] = json::array();
        for (const auto& t : doc.throws) {
            j["throws"].push_back({
                {"type_name", t.type_name},
                {"description", t.description},
            });
        }
    }
    if (!doc.template_params.empty()) {
        j["template_params"] = json::array();
        for (const auto& p : doc.template_params) {
            j["template_params"].push_back({
                {"name", p.name},
                {"description", p.description},
            });
        }
    }
    if (!doc.tags.empty()) {
        j["tags"] = json::array();
        for (const auto& tag : doc.tags) {
            j["tags"].push_back({
                {"tag", tag.tag},
                {"value", tag.value},
            });
        }
    }

    return j;
}

std::string indent(unsigned depth) {
    return std::string(depth * 2U, ' ');
}

void append_block_doc(std::ostringstream& out, const kdi_doc_block& doc, unsigned depth) {
    if (!doc.brief.empty())
        out << indent(depth) << "brief: " << doc.brief << "\n";
    if (!doc.description.empty()) {
        out << indent(depth) << "description:\n";
        out << indent(depth + 1) << doc.description << "\n";
    }
}

void append_function_doc(std::ostringstream& out, const kdi_doc_function& doc, unsigned depth) {
    append_block_doc(out, doc, depth);
    if (!doc.params.empty()) {
        out << indent(depth) << "params:\n";
        for (const auto& p : doc.params)
            out << indent(depth + 1) << p.name << ": " << p.description << "\n";
    }
    if (doc.returns.has_value())
        out << indent(depth) << "returns: " << *doc.returns << "\n";
    if (!doc.throws.empty()) {
        out << indent(depth) << "throws:\n";
        for (const auto& t : doc.throws)
            out << indent(depth + 1) << t.type_name << ": " << t.description << "\n";
    }
    if (!doc.template_params.empty()) {
        out << indent(depth) << "template_params:\n";
        for (const auto& p : doc.template_params)
            out << indent(depth + 1) << p.name << ": " << p.description << "\n";
    }
    if (!doc.tags.empty()) {
        out << indent(depth) << "tags:\n";
        for (const auto& tag : doc.tags)
            out << indent(depth + 1) << tag.tag << ": " << tag.value << "\n";
    }
}

void collect_namespace(const kdi_namespace& ns, std::vector<kdi_doc_symbol>& out);
void collect_aggregate(const kdi_aggregate& agg, std::vector<kdi_doc_symbol>& out);

void collect_union(const kdi_union& un, std::vector<kdi_doc_symbol>& out) {
    out.push_back({
        .kind = kdi_doc_kind::union_,
        .name = un.name,
        .fq_name = un.fq_name,
        .mangled_name = un.mangled_name,
        .block_doc = un.doc,
        .function_doc = std::nullopt,
    });
}

void collect_enum(const kdi_enum& en, std::vector<kdi_doc_symbol>& out) {
    out.push_back({
        .kind = kdi_doc_kind::enum_,
        .name = en.name,
        .fq_name = en.fq_name,
        .mangled_name = std::string{},
        .block_doc = en.doc,
        .function_doc = std::nullopt,
    });
}

void collect_variable(const kdi_variable& var, std::vector<kdi_doc_symbol>& out) {
    out.push_back({
        .kind = kdi_doc_kind::variable,
        .name = var.name,
        .fq_name = var.fq_name,
        .mangled_name = var.mangled_name,
        .block_doc = var.doc,
        .function_doc = std::nullopt,
    });
}

void collect_function(const kdi_function& fn, std::vector<kdi_doc_symbol>& out) {
    out.push_back({
        .kind = kdi_doc_kind::function,
        .name = fn.name,
        .fq_name = fn.fq_name,
        .mangled_name = fn.mangled_name,
        .block_doc = std::nullopt,
        .function_doc = fn.doc,
    });
}

void collect_method(const kdi_method& fn, const std::string& agg_fq, std::vector<kdi_doc_symbol>& out) {
    out.push_back({
        .kind = kdi_doc_kind::method,
        .name = fn.name,
        .fq_name = agg_fq + "::" + fn.name,
        .mangled_name = fn.mangled_name,
        .block_doc = std::nullopt,
        .function_doc = fn.doc,
    });
}

void collect_constructor(const kdi_constructor& ctor,
                         const kdi_aggregate& agg,
                         std::vector<kdi_doc_symbol>& out)
{
    out.push_back({
        .kind = kdi_doc_kind::constructor,
        .name = agg.name,
        .fq_name = agg.fq_name + "::" + agg.name,
        .mangled_name = ctor.mangled_name,
        .block_doc = std::nullopt,
        .function_doc = ctor.doc,
    });
}

void collect_destructor(const kdi_destructor& dtor,
                        const kdi_aggregate& agg,
                        std::vector<kdi_doc_symbol>& out)
{
    out.push_back({
        .kind = kdi_doc_kind::destructor,
        .name = "~" + agg.name,
        .fq_name = agg.fq_name + "::~" + agg.name,
        .mangled_name = dtor.mangled_name,
        .block_doc = std::nullopt,
        .function_doc = dtor.doc,
    });
}

void collect_aggregate(const kdi_aggregate& agg, std::vector<kdi_doc_symbol>& out) {
    out.push_back({
        .kind = kdi_doc_kind::aggregate,
        .name = agg.name,
        .fq_name = agg.fq_name,
        .mangled_name = agg.mangled_name,
        .block_doc = agg.doc,
        .function_doc = std::nullopt,
    });

    for (const auto& ctor : agg.constructors)
        collect_constructor(ctor, agg, out);

    if (agg.destructor.has_value())
        collect_destructor(*agg.destructor, agg, out);

    for (const auto& method : agg.methods)
        collect_method(method, agg.fq_name, out);

    for (const auto& var : agg.static_vars)
        collect_variable(var, out);

    for (const auto& nested : agg.nested)
        collect_aggregate(nested, out);

    for (const auto& un : agg.nested_unions)
        collect_union(un, out);
}

void collect_namespace(const kdi_namespace& ns, std::vector<kdi_doc_symbol>& out) {
    out.push_back({
        .kind = kdi_doc_kind::namespace_,
        .name = ns.name,
        .fq_name = ns.fq_name,
        .mangled_name = std::string{},
        .block_doc = ns.doc,
        .function_doc = std::nullopt,
    });

    for (const auto& child : ns.namespaces)
        collect_namespace(child, out);
    for (const auto& agg : ns.aggregates)
        collect_aggregate(agg, out);
    for (const auto& en : ns.enums)
        collect_enum(en, out);
    for (const auto& un : ns.unions)
        collect_union(un, out);
    for (const auto& fn : ns.functions)
        collect_function(fn, out);
    for (const auto& var : ns.variables)
        collect_variable(var, out);
}

} // anonymous namespace

std::string kdi_doc_kind_to_string(kdi_doc_kind kind) {
    switch (kind) {
    case kdi_doc_kind::namespace_:   return "namespace";
    case kdi_doc_kind::aggregate:    return "aggregate";
    case kdi_doc_kind::enum_:        return "enum";
    case kdi_doc_kind::union_:       return "union";
    case kdi_doc_kind::function:     return "function";
    case kdi_doc_kind::method:       return "method";
    case kdi_doc_kind::constructor:   return "constructor";
    case kdi_doc_kind::destructor:    return "destructor";
    case kdi_doc_kind::variable:     return "variable";
    }
    return "unknown";
}

std::vector<kdi_doc_symbol> kdi_find_doc_symbols(const kdi_file& file,
                                                 const std::string& symbol)
{
    std::vector<kdi_doc_symbol> all;
    collect_namespace(file.unit.root_ns, all);

    std::vector<kdi_doc_symbol> exact_mangled;
    for (const auto& entry : all) {
        if (!entry.mangled_name.empty() && entry.mangled_name == symbol)
            exact_mangled.push_back(entry);
    }
    if (!exact_mangled.empty()) {
        std::sort(exact_mangled.begin(), exact_mangled.end(), [](const auto& a, const auto& b) {
            if (a.kind != b.kind)
                return kdi_doc_kind_to_string(a.kind) < kdi_doc_kind_to_string(b.kind);
            if (a.fq_name != b.fq_name)
                return a.fq_name < b.fq_name;
            return a.mangled_name < b.mangled_name;
        });
        return exact_mangled;
    }

    const std::string root = file.header.module_name;
    const auto search_keys = make_search_keys(symbol, root);

    std::vector<kdi_doc_symbol> matches;
    for (const auto& entry : all) {
        if (match_any(make_entry_keys(entry, root), search_keys))
            matches.push_back(entry);
    }

    std::sort(matches.begin(), matches.end(), [](const auto& a, const auto& b) {
        if (a.kind != b.kind)
            return kdi_doc_kind_to_string(a.kind) < kdi_doc_kind_to_string(b.kind);
        if (a.fq_name != b.fq_name)
            return a.fq_name < b.fq_name;
        return a.mangled_name < b.mangled_name;
    });
    return matches;
}

std::string kdi_format_doc_text(const kdi_doc_symbol& symbol) {
    std::ostringstream out;
    out << kdi_doc_kind_to_string(symbol.kind) << " " << symbol.fq_name;
    if (!symbol.mangled_name.empty())
        out << "  // " << symbol.mangled_name;
    out << "\n";

    if (symbol.function_doc.has_value())
        append_function_doc(out, *symbol.function_doc, 1);
    else if (symbol.block_doc.has_value())
        append_block_doc(out, *symbol.block_doc, 1);
    else
        out << "  (no documentation)\n";

    return out.str();
}

std::string kdi_format_doc_json(const kdi_doc_symbol& symbol) {
    json j;
    j["kind"] = kdi_doc_kind_to_string(symbol.kind);
    j["name"] = symbol.name;
    j["fq_name"] = symbol.fq_name;
    if (!symbol.mangled_name.empty())
        j["mangled_name"] = symbol.mangled_name;
    if (symbol.function_doc.has_value())
        j["doc"] = function_doc_to_json(*symbol.function_doc);
    else if (symbol.block_doc.has_value())
        j["doc"] = block_doc_to_json(*symbol.block_doc);
    else
        j["doc"] = nullptr;
    return j.dump(2);
}

} // namespace kdi
