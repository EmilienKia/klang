/*
 * K Language compiler
 *
 * Copyright 2023-2026 Emilien Kia
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

#include "documentation.hpp"

#include "../parse/ast.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace k::model::doc {

namespace {

std::string trim_copy(const std::string& src) {
    size_t begin = 0;
    while (begin < src.size() && std::isspace(static_cast<unsigned char>(src[begin]))) ++begin;
    size_t end = src.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(src[end - 1]))) --end;
    return src.substr(begin, end - begin);
}

bool is_blank(const std::string& src) {
    return trim_copy(src).empty();
}

std::vector<std::string> split_lines(const std::string& src) {
    std::vector<std::string> lines;
    std::string cur;
    for (size_t i = 0; i < src.size(); ++i) {
        char c = src[i];
        if (c == '\n') {
            lines.push_back(cur);
            cur.clear();
        } else if (c == '\r') {
            lines.push_back(cur);
            cur.clear();
            if (i + 1 < src.size() && src[i + 1] == '\n') ++i;
        } else {
            cur += c;
        }
    }
    lines.push_back(cur);
    return lines;
}

std::string join_trimmed_paragraphs(const std::vector<std::string>& lines) {
    std::string out;
    bool previous_blank = true;
    for (const auto& raw_line : lines) {
        auto line = trim_copy(raw_line);
        if (line.empty()) {
            if (!previous_blank && !out.empty()) out += '\n';
            previous_blank = true;
            continue;
        }
        if (!out.empty() && !previous_blank) out += '\n';
        out += line;
        previous_blank = false;
    }
    return trim_copy(out);
}

void split_brief_and_description(const std::vector<std::string>& free_lines,
                                 std::string& brief, std::string& description) {
    std::vector<std::string> lines = free_lines;
    while (!lines.empty() && is_blank(lines.front())) lines.erase(lines.begin());
    while (!lines.empty() && is_blank(lines.back())) lines.pop_back();
    if (lines.empty()) {
        brief.clear();
        description.clear();
        return;
    }

    size_t first_para_end = 0;
    while (first_para_end < lines.size() && !is_blank(lines[first_para_end])) ++first_para_end;

    std::vector<std::string> brief_lines(lines.begin(), lines.begin() + static_cast<std::ptrdiff_t>(first_para_end));
    brief = join_trimmed_paragraphs(brief_lines);

    size_t rest_begin = first_para_end;
    while (rest_begin < lines.size() && is_blank(lines[rest_begin])) ++rest_begin;
    if (rest_begin < lines.size()) {
        std::vector<std::string> description_lines(lines.begin() + static_cast<std::ptrdiff_t>(rest_begin), lines.end());
        description = join_trimmed_paragraphs(description_lines);
    } else {
        description.clear();
    }
}

struct tag_lex {
    std::string name;
    std::string rest;
    bool valid = false;
};

tag_lex parse_tag_line(const std::string& raw_line) {
    auto line = trim_copy(raw_line);
    if (line.empty()) return {};
    if (line[0] != '@' && line[0] != '\\') return {};

    size_t i = 1;
    while (i < line.size() && (std::isalnum(static_cast<unsigned char>(line[i])) || line[i] == '_' || line[i] == '-')) ++i;
    if (i == 1) return {};

    tag_lex out;
    out.valid = true;
    out.name = line.substr(1, i - 1);
    out.rest = trim_copy(line.substr(i));
    return out;
}

std::string collect_comment_text(const parse::ast::doc_comment_list& comments) {
    std::string out;
    bool first = true;
    for (const auto& block : comments) {
        if (!first) out += '\n';
        out += block.content;
        first = false;
    }
    return out;
}

enum class current_tag_kind {
    NONE,
    PARAM,
    RETURN,
    THROWS,
    TPARAM,
    GENERIC,
};

} // anonymous namespace

void parse_doc_comments(doc_entity& target, const parse::ast::doc_comment_list& comments) {
    auto lines = split_lines(collect_comment_text(comments));
    split_brief_and_description(lines, target.brief, target.description);
}

void parse_doc_comments(function_doc& target, const parse::ast::doc_comment_list& comments) {
    auto lines = split_lines(collect_comment_text(comments));
    std::vector<std::string> free_lines;

    current_tag_kind current_kind = current_tag_kind::NONE;
    size_t current_index = 0;

    auto append_to_current = [&](const std::string& line) {
        auto trimmed = trim_copy(line);
        if (trimmed.empty()) {
            if (current_kind == current_tag_kind::NONE) {
                free_lines.emplace_back();
            }
            return;
        }
        switch (current_kind) {
            case current_tag_kind::PARAM:
                target.params[current_index].description =
                    trim_copy(target.params[current_index].description + "\n" + trimmed);
                break;
            case current_tag_kind::RETURN:
                if (!target.returns.has_value()) target.returns = return_doc{};
                target.returns->description =
                    trim_copy(target.returns->description + "\n" + trimmed);
                break;
            case current_tag_kind::THROWS:
                target.throws[current_index].description =
                    trim_copy(target.throws[current_index].description + "\n" + trimmed);
                break;
            case current_tag_kind::TPARAM:
                target.template_params[current_index].description =
                    trim_copy(target.template_params[current_index].description + "\n" + trimmed);
                break;
            case current_tag_kind::GENERIC:
                target.tags[current_index].value =
                    trim_copy(target.tags[current_index].value + "\n" + trimmed);
                break;
            case current_tag_kind::NONE:
                free_lines.push_back(line);
                break;
        }
    };

    for (const auto& raw_line : lines) {
        auto tag = parse_tag_line(raw_line);
        if (!tag.valid) {
            append_to_current(raw_line);
            continue;
        }

        auto lower_name = tag.name;
        std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        current_kind = current_tag_kind::NONE;
        current_index = 0;

        if (lower_name == "param") {
            std::string param_name;
            std::string desc;
            auto rest = tag.rest;
            auto space_pos = rest.find_first_of(" \t");
            if (space_pos == std::string::npos) {
                param_name = trim_copy(rest);
                desc.clear();
            } else {
                param_name = trim_copy(rest.substr(0, space_pos));
                desc = trim_copy(rest.substr(space_pos + 1));
            }
            target.params.push_back(param_doc{param_name, desc});
            current_kind = current_tag_kind::PARAM;
            current_index = target.params.size() - 1;
        } else if (lower_name == "return" || lower_name == "returns") {
            target.returns = return_doc{trim_copy(tag.rest)};
            current_kind = current_tag_kind::RETURN;
            current_index = 0;
        } else if (lower_name == "throws" || lower_name == "throw" || lower_name == "exception") {
            std::string type_name;
            std::string desc;
            auto rest = tag.rest;
            auto space_pos = rest.find_first_of(" \t");
            if (space_pos == std::string::npos) {
                type_name = trim_copy(rest);
                desc.clear();
            } else {
                type_name = trim_copy(rest.substr(0, space_pos));
                desc = trim_copy(rest.substr(space_pos + 1));
            }
            target.throws.push_back(throws_doc{type_name, desc});
            current_kind = current_tag_kind::THROWS;
            current_index = target.throws.size() - 1;
        } else if (lower_name == "tparam" || lower_name == "typeparam") {
            std::string param_name;
            std::string desc;
            auto rest = tag.rest;
            auto space_pos = rest.find_first_of(" \t");
            if (space_pos == std::string::npos) {
                param_name = trim_copy(rest);
                desc.clear();
            } else {
                param_name = trim_copy(rest.substr(0, space_pos));
                desc = trim_copy(rest.substr(space_pos + 1));
            }
            target.template_params.push_back(template_param_doc{param_name, desc});
            current_kind = current_tag_kind::TPARAM;
            current_index = target.template_params.size() - 1;
        } else {
            target.tags.push_back(tagged_doc{tag.name, trim_copy(tag.rest)});
            current_kind = current_tag_kind::GENERIC;
            current_index = target.tags.size() - 1;
        }
    }

    split_brief_and_description(free_lines, target.brief, target.description);
}

std::shared_ptr<doc_entity> build_doc_entity(
    const std::shared_ptr<element>& owner,
    const parse::ast::doc_comment_list& comments)
{
    auto out = std::make_shared<doc_entity>();
    parse_doc_comments(*out, comments);
    out->owner = owner;
    return out;
}

std::shared_ptr<function_doc> build_function_doc(
    const std::shared_ptr<element>& owner,
    const parse::ast::doc_comment_list& comments)
{
    auto out = std::make_shared<function_doc>();
    parse_doc_comments(*out, comments);
    out->owner = owner;
    return out;
}

} // namespace k::model::doc
