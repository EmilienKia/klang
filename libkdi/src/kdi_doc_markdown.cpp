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

#include "kdi_doc_markdown.hpp"

#include <cmark-gfm.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <memory>
#include <new>
#include <string_view>
#include <utility>

namespace kdi::doc_markdown {
namespace {

using node_ptr = std::unique_ptr<cmark_node, decltype(&cmark_node_free)>;
using buffer_ptr = std::unique_ptr<char, decltype(&std::free)>;

node_ptr node(cmark_node_type type) {
    node_ptr result(cmark_node_new(type), cmark_node_free);
    if (!result) throw std::bad_alloc();
    return result;
}

node_ptr text_node(const std::string& text, cmark_node_type type = CMARK_NODE_TEXT) {
    auto result = node(type);
    cmark_node_set_literal(result.get(), text.c_str());
    return result;
}

void replace_node(cmark_node* old, node_ptr replacement) {
    cmark_node_replace(old, replacement.get());
    replacement.release();
    cmark_node_free(old);
}

std::string spaces(std::string_view text) {
    std::string result;
    for (unsigned char ch : text) {
        if (std::isspace(ch)) {
            if (result.empty() || result.back() != ' ') result += ' ';
        } else {
            result += static_cast<char>(ch);
        }
    }
    return result;
}

bool safe_url(std::string_view url) {
    std::string prefix;
    for (unsigned char ch : url) {
        if (ch <= ' ' || ch == 0x7f) continue;
        if (ch == '/' || ch == '#' || ch == '?') return true;
        if (ch == ':')
            return prefix == "http" || prefix == "https" || prefix == "mailto";
        prefix += static_cast<char>(std::tolower(ch));
    }
    return true; // Relative URL.
}

void normalize(cmark_node* parent, int heading) {
    for (auto* child = cmark_node_first_child(parent); child;) {
        auto* next = cmark_node_next(child);
        const auto type = cmark_node_get_type(child);
        if (type == CMARK_NODE_HTML_INLINE) {
            replace_node(child, text_node(cmark_node_get_literal(child)));
        } else if (type == CMARK_NODE_HTML_BLOCK) {
            auto paragraph = node(CMARK_NODE_PARAGRAPH);
            auto text = text_node(cmark_node_get_literal(child));
            cmark_node_append_child(paragraph.get(), text.release());
            replace_node(child, std::move(paragraph));
        } else {
            if (type == CMARK_NODE_LINK || type == CMARK_NODE_IMAGE) {
                if (!safe_url(cmark_node_get_url(child)))
                    cmark_node_set_url(child, "");
                cmark_node_set_title(child, spaces(cmark_node_get_title(child)).c_str());
            } else if (type == CMARK_NODE_CODE_BLOCK) {
                const std::string info = cmark_node_get_fence_info(child);
                if (std::all_of(info.begin(), info.end(), [](unsigned char ch) { return std::isspace(ch); }))
                    cmark_node_set_fence_info(child, "k");
            } else if (type == CMARK_NODE_HEADING) {
                cmark_node_set_heading_level(child,
                    std::clamp(heading + cmark_node_get_heading_level(child), 1, 6));
            }
            normalize(child, heading);
        }
        child = next;
    }
}

node_ptr parse(const std::string& source, int heading) {
    node_ptr document(cmark_parse_document(source.data(), source.size(), CMARK_OPT_VALIDATE_UTF8),
                      cmark_node_free);
    if (!document) throw std::bad_alloc();
    normalize(document.get(), heading);
    return document;
}

void normalize_inline(cmark_node* parent) {
    for (auto* child = cmark_node_first_child(parent); child;) {
        auto* next = cmark_node_next(child);
        const auto type = cmark_node_get_type(child);
        if (type == CMARK_NODE_SOFTBREAK || type == CMARK_NODE_LINEBREAK)
            replace_node(child, text_node(" "));
        else if (type == CMARK_NODE_TEXT)
            cmark_node_set_literal(child, spaces(cmark_node_get_literal(child)).c_str());
        else
            normalize_inline(child);
        child = next;
    }
}

void separator(cmark_node* paragraph) {
    if (cmark_node_first_child(paragraph)) {
        auto space = text_node(" ");
        cmark_node_append_child(paragraph, space.release());
    }
}

void flatten(cmark_node* source, cmark_node* paragraph) {
    const auto type = cmark_node_get_type(source);
    if (type == CMARK_NODE_PARAGRAPH || type == CMARK_NODE_HEADING) {
        separator(paragraph);
        normalize_inline(source);
        while (auto* child = cmark_node_first_child(source)) {
            cmark_node_unlink(child);
            cmark_node_append_child(paragraph, child);
        }
    } else if (type == CMARK_NODE_CODE_BLOCK) {
        separator(paragraph);
        auto code = text_node(spaces(cmark_node_get_literal(source)), CMARK_NODE_CODE);
        cmark_node_append_child(paragraph, code.release());
    } else {
        for (auto* child = cmark_node_first_child(source); child; child = cmark_node_next(child))
            flatten(child, paragraph);
    }
}

size_t characters(std::string_view text) {
    return std::count_if(text.begin(), text.end(),
        [](unsigned char ch) { return (ch & 0xc0) != 0x80; });
}

size_t visible_length(cmark_node* parent) {
    size_t result = 0;
    for (auto* child = cmark_node_first_child(parent); child; child = cmark_node_next(child)) {
        const auto type = cmark_node_get_type(child);
        result += type == CMARK_NODE_TEXT || type == CMARK_NODE_CODE
            ? characters(cmark_node_get_literal(child)) : visible_length(child);
    }
    return result;
}

void truncate(cmark_node* parent, size_t& remaining) {
    for (auto* child = cmark_node_first_child(parent); child;) {
        auto* next = cmark_node_next(child);
        if (remaining == 0) {
            cmark_node_unlink(child);
            cmark_node_free(child);
        } else {
            const auto type = cmark_node_get_type(child);
            if (type == CMARK_NODE_TEXT || type == CMARK_NODE_CODE) {
                std::string text = cmark_node_get_literal(child);
                size_t end = 0, count = 0;
                while (end < text.size()) {
                    if ((static_cast<unsigned char>(text[end]) & 0xc0) != 0x80) {
                        if (count == remaining) break;
                        ++count;
                    }
                    ++end;
                }
                text.resize(end);
                cmark_node_set_literal(child, text.c_str());
                remaining -= count;
            } else {
                truncate(child, remaining);
            }
        }
        child = next;
    }
}

std::string table_code(std::string_view literal) {
    std::string result = "<code>";
    for (char ch : literal) {
        switch (ch) {
        case '&': result += "&amp;"; break;
        case '<': result += "&lt;"; break;
        case '>': result += "&gt;"; break;
        case '|': result += "&#124;"; break;
        case '\n': result += "&#10;"; break;
        case '\r': result += "&#13;"; break;
        case '\t': result += "&#9;"; break;
        default:
            // Inline Markdown is still parsed between HTML <code> tags.
            // Entities keep punctuation literal through that second parse.
            if (static_cast<unsigned char>(ch) < 0x80 && std::ispunct(static_cast<unsigned char>(ch)))
                result += "&#" + std::to_string(static_cast<unsigned char>(ch)) + ";";
            else
                result += ch;
            break;
        }
    }
    return result + "</code>";
}

void protect_table_code(cmark_node* parent) {
    for (auto* child = cmark_node_first_child(parent); child;) {
        auto* next = cmark_node_next(child);
        if (cmark_node_get_type(child) == CMARK_NODE_CODE &&
            std::string_view(cmark_node_get_literal(child)).find('|') != std::string_view::npos) {
            // Backslash/entity escapes inside a CommonMark code span change its
            // literal. Trusted inline HTML preserves the code and table boundary.
            auto code = node(CMARK_NODE_CUSTOM_INLINE);
            cmark_node_set_on_enter(code.get(), table_code(cmark_node_get_literal(child)).c_str());
            replace_node(child, std::move(code));
        } else {
            protect_table_code(child);
        }
        child = next;
    }
}

std::string render(cmark_node* document, format output) {
    buffer_ptr buffer(output == format::html
        ? cmark_render_html(document, CMARK_OPT_DEFAULT, nullptr)
        : cmark_render_commonmark(document, CMARK_OPT_DEFAULT, 0), std::free);
    if (!buffer) throw std::bad_alloc();
    return buffer.get();
}

} // namespace

std::string inline_fragment(const std::string& source, format output,
                            inline_context context, size_t max_length) {
    auto document = parse(source, 0);
    auto inline_document = node(CMARK_NODE_DOCUMENT);
    auto paragraph = node(CMARK_NODE_PARAGRAPH);
    flatten(document.get(), paragraph.get());
    cmark_consolidate_text_nodes(paragraph.get());
    if (max_length && visible_length(paragraph.get()) > max_length) {
        size_t remaining = max_length > 3 ? max_length - 3 : 0;
        truncate(paragraph.get(), remaining);
        auto ellipsis = text_node(std::string(std::min(max_length, size_t{3}), '.'));
        cmark_node_append_child(paragraph.get(), ellipsis.release());
    }
    if (output == format::markdown && context == inline_context::table)
        protect_table_code(paragraph.get());
    cmark_node_append_child(inline_document.get(), paragraph.release());
    auto result = render(inline_document.get(), output);
    if (output == format::html) {
        if (result.starts_with("<p>") && result.ends_with("</p>\n"))
            result = result.substr(3, result.size() - 8);
    } else {
        while (!result.empty() && result.back() == '\n') result.pop_back();
        if (context == inline_context::table) {
            std::string escaped;
            for (char ch : result) {
                if (ch == '|') escaped += '\\';
                escaped += ch;
            }
            result = std::move(escaped);
        }
    }
    return result;
}

std::string block_fragment(const std::string& source, format output, int containing_heading) {
    auto document = parse(source, containing_heading);
    return render(document.get(), output);
}

std::string literal_code(const std::string& value) {
    // HTML also handles identifiers containing backticks, newlines, or pipes
    // without allowing them to terminate Markdown delimiters or table cells.
    return table_code(value);
}

} // namespace kdi::doc_markdown
