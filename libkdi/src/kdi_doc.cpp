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

kdi_doc_symbol make_namespace_symbol(const kdi_namespace& ns);
kdi_doc_symbol make_aggregate_symbol(const kdi_aggregate& agg);

kdi_doc_symbol make_union_symbol(const kdi_union& un) {
    kdi_doc_symbol symbol;
    symbol.kind = kdi_doc_kind::union_;
    symbol.name = un.name;
    symbol.fq_name = un.fq_name;
    symbol.mangled_name = un.mangled_name;
    symbol.block_doc = un.doc;
    return symbol;
}

kdi_doc_symbol make_enum_symbol(const kdi_enum& en) {
    kdi_doc_symbol symbol;
    symbol.kind = kdi_doc_kind::enum_;
    symbol.name = en.name;
    symbol.fq_name = en.fq_name;
    symbol.block_doc = en.doc;
    return symbol;
}

kdi_doc_symbol make_variable_symbol(const kdi_variable& var) {
    kdi_doc_symbol symbol;
    symbol.kind = kdi_doc_kind::variable;
    symbol.name = var.name;
    symbol.fq_name = var.fq_name;
    symbol.mangled_name = var.mangled_name;
    symbol.block_doc = var.doc;
    return symbol;
}

kdi_doc_symbol make_function_symbol(const kdi_function& fn) {
    kdi_doc_symbol symbol;
    symbol.kind = kdi_doc_kind::function;
    symbol.name = fn.name;
    symbol.fq_name = fn.fq_name;
    symbol.mangled_name = fn.mangled_name;
    symbol.function_doc = fn.doc;
    return symbol;
}

kdi_doc_symbol make_method_symbol(const kdi_method& fn, const std::string& agg_fq) {
    kdi_doc_symbol symbol;
    symbol.kind = kdi_doc_kind::method;
    symbol.name = fn.name;
    symbol.fq_name = agg_fq + "::" + fn.name;
    symbol.mangled_name = fn.mangled_name;
    symbol.function_doc = fn.doc;
    return symbol;
}

kdi_doc_symbol make_constructor_symbol(const kdi_constructor& ctor, const kdi_aggregate& agg) {
    kdi_doc_symbol symbol;
    symbol.kind = kdi_doc_kind::constructor;
    symbol.name = agg.name;
    symbol.fq_name = agg.fq_name + "::" + agg.name;
    symbol.mangled_name = ctor.mangled_name;
    symbol.function_doc = ctor.doc;
    return symbol;
}

kdi_doc_symbol make_destructor_symbol(const kdi_destructor& dtor, const kdi_aggregate& agg) {
    kdi_doc_symbol symbol;
    symbol.kind = kdi_doc_kind::destructor;
    symbol.name = "~" + agg.name;
    symbol.fq_name = agg.fq_name + "::~" + agg.name;
    symbol.mangled_name = dtor.mangled_name;
    symbol.function_doc = dtor.doc;
    return symbol;
}

kdi_doc_symbol make_aggregate_symbol(const kdi_aggregate& agg) {
    kdi_doc_symbol symbol;
    symbol.kind = kdi_doc_kind::aggregate;
    symbol.name = agg.name;
    symbol.fq_name = agg.fq_name;
    symbol.mangled_name = agg.mangled_name;
    symbol.block_doc = agg.doc;

    for (const auto& ctor : agg.constructors)
        symbol.children.push_back(make_constructor_symbol(ctor, agg));
    if (agg.destructor.has_value())
        symbol.children.push_back(make_destructor_symbol(*agg.destructor, agg));
    for (const auto& method : agg.methods)
        symbol.children.push_back(make_method_symbol(method, agg.fq_name));
    for (const auto& var : agg.static_vars)
        symbol.children.push_back(make_variable_symbol(var));
    for (const auto& nested : agg.nested)
        symbol.children.push_back(make_aggregate_symbol(nested));
    for (const auto& un : agg.nested_unions)
        symbol.children.push_back(make_union_symbol(un));

    return symbol;
}

kdi_doc_symbol make_namespace_symbol(const kdi_namespace& ns) {
    kdi_doc_symbol symbol;
    symbol.kind = kdi_doc_kind::namespace_;
    symbol.name = ns.name;
    symbol.fq_name = ns.fq_name;
    symbol.block_doc = ns.doc;

    for (const auto& child : ns.namespaces)
        symbol.children.push_back(make_namespace_symbol(child));
    for (const auto& agg : ns.aggregates)
        symbol.children.push_back(make_aggregate_symbol(agg));
    for (const auto& en : ns.enums)
        symbol.children.push_back(make_enum_symbol(en));
    for (const auto& un : ns.unions)
        symbol.children.push_back(make_union_symbol(un));
    for (const auto& fn : ns.functions)
        symbol.children.push_back(make_function_symbol(fn));
    for (const auto& var : ns.variables)
        symbol.children.push_back(make_variable_symbol(var));

    return symbol;
}

void flatten_symbols(const kdi_doc_symbol& symbol, std::vector<kdi_doc_symbol>& out) {
    out.push_back(symbol);
    for (const auto& child : symbol.children)
        flatten_symbols(child, out);
}

std::string summarize_symbol(const kdi_doc_symbol& symbol) {
    std::ostringstream out;
    out << kdi_doc_kind_to_string(symbol.kind) << " " << symbol.fq_name;
    if (!symbol.mangled_name.empty())
        out << "  // " << symbol.mangled_name;
    return out.str();
}

json summarize_symbol_json(const kdi_doc_symbol& symbol) {
    json j;
    j["kind"] = kdi_doc_kind_to_string(symbol.kind);
    j["name"] = symbol.name;
    j["fq_name"] = symbol.fq_name;
    if (!symbol.mangled_name.empty())
        j["mangled_name"] = symbol.mangled_name;
    return j;
}

std::string render_children_text(const std::vector<kdi_doc_symbol>& children, unsigned depth) {
    std::ostringstream out;
    if (children.empty())
        return {};
    out << indent(depth) << "children:\n";
    for (const auto& child : children) {
        out << indent(depth + 1) << "- " << summarize_symbol(child) << "\n";
    }
    return out.str();
}

json render_children_json(const std::vector<kdi_doc_symbol>& children) {
    json out = json::array();
    for (const auto& child : children)
        out.push_back(summarize_symbol_json(child));
    return out;
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
    case kdi_doc_kind::constructor:  return "constructor";
    case kdi_doc_kind::destructor:   return "destructor";
    case kdi_doc_kind::variable:     return "variable";
    }
    return "unknown";
}

std::vector<kdi_doc_symbol> kdi_find_doc_symbols(const kdi_file& file,
                                                 const std::string& symbol)
{
    std::vector<kdi_doc_symbol> all;
    flatten_symbols(make_namespace_symbol(file.unit.root_ns), all);

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

std::string kdi_format_doc_text(const kdi_doc_symbol& symbol, bool list_children) {
    std::ostringstream out;
    out << summarize_symbol(symbol) << "\n";

    if (symbol.function_doc.has_value())
        append_function_doc(out, *symbol.function_doc, 1);
    else if (symbol.block_doc.has_value())
        append_block_doc(out, *symbol.block_doc, 1);
    else
        out << "  (no documentation)\n";

    if (list_children)
        out << render_children_text(symbol.children, 1);

    return out.str();
}

std::string kdi_format_doc_json(const kdi_doc_symbol& symbol, bool list_children) {
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
    if (list_children)
        j["children"] = render_children_json(symbol.children);
    return j.dump(2);
}

} // namespace kdi
