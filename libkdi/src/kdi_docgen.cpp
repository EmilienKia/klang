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
#include <sstream>
#include <string>
#include <type_traits>
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
        if constexpr (std::is_same_v<T, kdi_aggregate_ref>) return v.fq_name;
        if constexpr (std::is_same_v<T, kdi_enum_ref>) return "enum " + v.fq_name;
        if constexpr (std::is_same_v<T, kdi_template_param_ref>) return "$" + v.name;
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

std::string make_signature(const std::string& name,
                           const std::vector<kdi_param>& params,
                           const kdi_type* ret)
{
    std::ostringstream out;
    out << name << "(";
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
    for (const auto& nested : agg.nested)
        nested_rows.push_back({nested.name,
                               qualified_simple_name + "." + nested.name + ".md",
                               compact_doc_brief(nested.doc)});
    for (const auto& nested : agg.nested_unions)
        nested_rows.push_back({nested.name,
                               qualified_simple_name + "." + nested.name + ".md",
                               compact_doc_brief(nested.doc)});
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

    for (size_t i = 0; i < fields.size(); ++i) {
        const auto& field = fields[i];
        add_reference(refs,
                      "field",
                      field.name,
                      strip_prefix(agg.fq_name, root_fq),
                      type_to_string(field.type),
                      type_link + "#field-" + make_slug(field.name) + "-" + std::to_string(i));
    }
    for (size_t i = 0; i < static_vars.size(); ++i) {
        const auto& var = static_vars[i];
        add_reference(refs,
                      "static-variable",
                      var.name,
                      strip_prefix(agg.fq_name, root_fq),
                      type_to_string(var.type),
                      type_link + "#static-var-" + make_slug(var.name) + "-" + std::to_string(i),
                      compact_doc_brief(var.doc));
    }
    for (size_t i = 0; i < methods.size(); ++i) {
        const auto& method = methods[i];
        add_reference(refs,
                      "method",
                      method.name,
                      strip_prefix(agg.fq_name, root_fq),
                      make_signature(method.name, method.params, &method.return_type),
                      type_link + "#method-" + make_slug(method.name) + "-" + std::to_string(i),
                      compact_doc_brief(method.doc));
    }
    for (size_t i = 0; i < constructors.size(); ++i) {
        add_reference(refs,
                      "constructor",
                      agg.name,
                      strip_prefix(agg.fq_name, root_fq),
                      make_signature(agg.name, constructors[i].params, nullptr),
                      type_link + "#ctor-" + std::to_string(i),
                      compact_doc_brief(constructors[i].doc));
    }
    if (agg.destructor.has_value()) {
        add_reference(refs,
                      "destructor",
                      "~" + agg.name,
                      strip_prefix(agg.fq_name, root_fq),
                      "destructor",
                      type_link + "#dtor",
                      compact_doc_brief(agg.destructor->doc));
    }

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
    std::sort(child_ns.begin(), child_ns.end(), [](const auto& a, const auto& b) {
        return a.name < b.name;
    });

    std::vector<kdi_aggregate> aggregates = ns.aggregates;
    std::sort(aggregates.begin(), aggregates.end(), [](const auto& a, const auto& b) {
        return a.name < b.name;
    });

    std::vector<kdi_enum> enums = ns.enums;
    std::sort(enums.begin(), enums.end(), [](const auto& a, const auto& b) {
        return a.name < b.name;
    });

    std::vector<kdi_union> unions = ns.unions;
    std::sort(unions.begin(), unions.end(), [](const auto& a, const auto& b) {
        return a.name < b.name;
    });

    std::vector<kdi_function> functions = ns.functions;
    std::sort(functions.begin(), functions.end(), [](const auto& a, const auto& b) {
        if (a.name != b.name) return a.name < b.name;
        return a.mangled_name < b.mangled_name;
    });

    std::vector<kdi_variable> variables = ns.variables;
    std::sort(variables.begin(), variables.end(), [](const auto& a, const auto& b) {
        return a.name < b.name;
    });

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



