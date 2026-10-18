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

#include "doc_comment_parser.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string_view>

namespace k::parse {

namespace {

// ---------------------------------------------------------------------------
// Lexical cleaning helpers (marker stripping)
// These implement the "DocBlockBody" and "DocLineText" lexical rules
// described in grammar.ebnf §1 (Documentation comments).
// ---------------------------------------------------------------------------

/// Recognise legacy '=' banners without mistaking Markdown rules or fences
/// for comment decoration.
bool is_border_line(std::string_view line) {
    return line.size() >= 3 && line.find_first_not_of('=') == std::string_view::npos;
}

bool is_blank(std::string_view line) {
    return std::all_of(line.begin(), line.end(),
                       [](unsigned char c) { return std::isspace(c); });
}

size_t leading_padding(std::string_view line) {
    const auto end = line.find_first_not_of(" \t");
    return end == std::string_view::npos ? line.size() : end;
}

/// Clean a single-line doc comment (/// or //!).
/// Strips the 3-char prefix and one optional leading space.
std::string clean_line_doc(std::string_view raw, std::string_view prefix) {
    std::string_view body = raw;
    if (body.starts_with(prefix)) body = body.substr(prefix.size());
    if (!body.empty() && body.front() == ' ') body = body.substr(1);
    return std::string(body);
}

/// Clean a block doc comment (/** or /*!).
/// Strips the opening marker, the closing */, per-line leading * decoration,
/// paired '=' banners, and normalises common leading indentation.
std::string clean_block_doc(std::string_view raw, std::string_view opening) {
    std::string_view body = raw;
    if (body.starts_with(opening)) body = body.substr(opening.size());
    if (body.ends_with("*/")) {
        body = body.substr(0, body.size() - 2);
        // One space immediately before an inline closing marker is padding;
        // any preceding spaces still belong to the Markdown text.
        if (!body.empty() && body.back() == ' ') body.remove_suffix(1);
    }

    // Split into lines.
    std::vector<std::string> lines;
    std::string cur;
    for (size_t i = 0; i < body.size(); ++i) {
        char c = body[i];
        if (c == '\n') {
            lines.push_back(cur);
            cur.clear();
        } else if (c == '\r') {
            lines.push_back(cur);
            cur.clear();
            if (i + 1 < body.size() && body[i + 1] == '\n') ++i;
        } else {
            cur += c;
        }
    }
    lines.push_back(cur);

    // The opening line has no source indentation inside the comment. Other
    // lines share only their common margin, not their Markdown indentation.
    std::string margin;
    bool have_margin = false;
    bool decorated = true;
    for (size_t i = 1; i < lines.size(); ++i) {
        if (is_blank(lines[i])) continue;
        const auto padding = leading_padding(lines[i]);
        const auto prefix = lines[i].substr(0, padding);
        if (!have_margin) {
            margin = prefix;
            have_margin = true;
        } else {
            size_t common = 0;
            while (common < margin.size() && common < prefix.size()
                   && margin[common] == prefix[common]) ++common;
            margin.resize(common);
        }
    }
    for (size_t i = 1; i < lines.size(); ++i) {
        if (is_blank(lines[i])) continue;
        const auto line = std::string_view(lines[i]).substr(margin.size());
        if (line.empty() || (line.front() != '*' && line.front() != '!')
            || (line.size() > 1 && line[1] != ' ' && line[1] != '\t')) {
            decorated = false;
            break;
        }
    }
    if (!lines.front().empty() && lines.front().front() == ' ')
        lines.front().erase(0, 1);
    // Inline text establishes the baseline of an undecorated block. Dedenting
    // only its continuation could otherwise erase an entire code example.
    if (!decorated && !is_blank(lines.front())) margin.clear();
    for (size_t i = 1; i < lines.size(); ++i) {
        auto& line = lines[i];
        size_t common = 0;
        while (common < margin.size() && common < line.size()
               && margin[common] == line[common]) ++common;
        line.erase(0, common);
        if (decorated && !line.empty() && (line.front() == '*' || line.front() == '!')) {
            line.erase(0, 1);
            if (!line.empty() && line.front() == ' ') line.erase(0, 1);
        }
    }

    // Remove framing blank lines and paired legacy banners. A lone '=' line
    // can be a Markdown heading underline and must not disappear.
    size_t first = 0, last = lines.size();
    while (first < last && is_blank(lines[first])) ++first;
    while (last > first && is_blank(lines[last - 1])) --last;
    if (last > first + 1 && is_border_line(lines[first]) && lines[first] == lines[last - 1]) {
        ++first;
        --last;
    }

    // Join remaining lines.
    std::string result;
    for (size_t i = first; i < last; ++i) {
        if (i > first) result += '\n';
        result += lines[i];
    }
    return result;
}

/// Convert a single raw doc_comment lexeme to its cleaned text.
std::string clean_doc_comment(const lex::doc_comment& dc) {
    using dt = lex::doc_comment::doc_type;
    switch (dc.type) {
        case dt::LINE_FWD: return clean_line_doc(dc.content, "///");
        case dt::LINE_BWD: return clean_line_doc(dc.content, "//!");
        case dt::BLOCK_FWD: return clean_block_doc(dc.content, "/**");
        case dt::BLOCK_BWD: return clean_block_doc(dc.content, "/*!");
    }
    return {};  // unreachable
}

// ---------------------------------------------------------------------------
// Structural parsing helpers (DocBody grammar)
// These implement DocBriefSection, DocDescriptionSection, DocEntry.
// ---------------------------------------------------------------------------

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

std::string join_lines(const std::vector<std::string>& lines) {
    size_t first = 0, last = lines.size();
    while (first < last && is_blank(lines[first])) ++first;
    while (last > first && is_blank(lines[last - 1])) --last;
    std::string out;
    for (size_t i = first; i < last; ++i) {
        if (i != first) out += '\n';
        out += lines[i];
    }
    return out;
}

struct markdown_code {
    char marker = 0;
    size_t fence_length = 0;
    size_t container_indent = 0;
    size_t list_indent = 0;
    size_t quote_depth = 0;

    void reset() {
        marker = 0;
        fence_length = 0;
        container_indent = 0;
        list_indent = 0;
        quote_depth = 0;
    }

    bool consume(std::string_view line) {
        if (is_blank(line)) {
            if (quote_depth != 0) reset();
            return marker != 0;
        }
        const bool inside = marker != 0;
        size_t start = 0, columns = 0, container_columns = 0, quotes = 0;
        size_t list_columns = 0, quote_columns = 0;
        auto advance = [&] {
            columns += line[start] == '\t' ? 4 - columns % 4 : 1;
            ++start;
        };
        for (;;) {
            while (start < line.size() && (line[start] == ' ' || line[start] == '\t'))
                advance();
            if (columns - container_columns > (inside ? list_indent + 3 : 3)
                || start == line.size()) break;
            if (line[start] == '>' && (!inside || quotes < quote_depth)) {
                ++quotes;
                const auto quote_start = columns;
                advance();
                if (start < line.size() && line[start] == ' ') advance();
                quote_columns += columns - quote_start;
                container_columns = columns;
                continue;
            }
            // List markers can introduce a fence, but a marker inside a fence
            // is code, not a prefix that can introduce its closing delimiter.
            if (inside) break;
            size_t end = start;
            if (line[start] == '-' || line[start] == '+' || line[start] == '*') {
                ++end;
            } else {
                while (end < line.size() && end - start < 9
                       && std::isdigit(static_cast<unsigned char>(line[end]))) ++end;
                if (end == start || end == line.size()
                    || (line[end] != '.' && line[end] != ')')) break;
                ++end;
            }
            if (end == line.size() || (line[end] != ' ' && line[end] != '\t')) break;
            while (start <= end) advance();
            list_columns += columns - container_columns;
            container_columns = columns;
        }
        if (inside && (quotes != quote_depth || columns - quote_columns < list_indent)) {
            // Leaving a container ends its fence, unlike an unclosed top-level
            // fence. Reclassify the same line so an actual tag is not swallowed.
            reset();
            return consume(line);
        }
        const auto baseline = inside ? std::max(container_indent, container_columns) : container_columns;
        if (columns - std::min(columns, baseline) >= 4) return true;
        if (start == line.size() || (line[start] != '`' && line[start] != '~'))
            return inside;
        const char candidate = line[start];
        size_t end = start;
        while (end < line.size() && line[end] == candidate) ++end;
        const size_t length = end - start;
        if (inside) {
            if (candidate == marker && length >= fence_length && is_blank(line.substr(end))) {
                reset();
            }
            return true;
        }
        if (length < 3 || (candidate == '`' && line.substr(end).find('`') != std::string_view::npos))
            return false;
        marker = candidate;
        fence_length = length;
        container_indent = container_columns;
        list_indent = list_columns;
        quote_depth = quotes;
        return true;
    }
};

void split_brief_and_description(const std::vector<std::string>& free_lines,
                                   std::string& brief, std::string& description) {
    std::vector<std::string> lines = free_lines;
    while (!lines.empty() && is_blank(lines.front())) lines.erase(lines.begin());
    while (!lines.empty() && is_blank(lines.back()))  lines.pop_back();
    if (lines.empty()) {
        brief.clear();
        description.clear();
        return;
    }

    size_t first_para_end = 0;
    markdown_code code;
    while (first_para_end < lines.size()) {
        const bool in_code = code.consume(lines[first_para_end]);
        if (!in_code && is_blank(lines[first_para_end])) break;
        ++first_para_end;
    }

    std::vector<std::string> brief_lines(lines.begin(),
                                          lines.begin() + static_cast<std::ptrdiff_t>(first_para_end));
    brief = join_lines(brief_lines);

    size_t rest_begin = first_para_end;
    while (rest_begin < lines.size() && is_blank(lines[rest_begin])) ++rest_begin;
    if (rest_begin < lines.size()) {
        std::vector<std::string> desc_lines(
            lines.begin() + static_cast<std::ptrdiff_t>(rest_begin), lines.end());
        description = join_lines(desc_lines);
    } else {
        description.clear();
    }
}

/// Attempt to parse a DocEntry line: a line that starts (after leading padding) with
/// '@' or '\' followed by an identifier (DocTagName), outside Markdown code.
/// Returns { true, tag_name_lower, rest_of_line } on success, { false, "", "" } otherwise.
struct tag_parse_result {
    bool valid   = false;
    std::string name;   ///< tag name, lower-cased
    std::string rest;   ///< content after the tag name and separator whitespace
};

tag_parse_result parse_tag_line(const std::string& raw_line) {
    auto line = std::string_view(raw_line).substr(leading_padding(raw_line));
    if (line.empty()) return {};
    if (line[0] != '@' && line[0] != '\\') return {};

    size_t i = 1;
    while (i < line.size() && (std::isalnum(static_cast<unsigned char>(line[i]))
                                 || line[i] == '_' || line[i] == '-')) {
        ++i;
    }
    if (i == 1) return {};  // '@' with no identifier

    tag_parse_result out;
    out.valid = true;
    out.name  = line.substr(1, i - 1);
    // Lower-case the tag name so that @Param, @PARAM, @param are all equivalent.
    std::transform(out.name.begin(), out.name.end(), out.name.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) ++i;
    out.rest = line.substr(i);
    return out;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::optional<ast::documentation> parse_documentation(
    const std::vector<lex::doc_comment>& raw_comments)
{
    if (raw_comments.empty()) return std::nullopt;

    // Step 1 — Lexical: clean each lexeme and concatenate (DocBody).
    std::string full_text;
    bool first_block = true;
    for (const auto& dc : raw_comments) {
        std::string cleaned = clean_doc_comment(dc);
        if (!first_block) full_text += '\n';
        full_text += cleaned;
        first_block = false;
    }

    // Step 2 — Structural: split into lines and classify.
    //
    // Grammar rules applied here (see grammar.ebnf §1 — Documentation comments):
    //   DocBody            ::= [ DocBriefSection ] { DocDescriptionSection } { DocEntry }
    //   DocBriefSection    ::= DocFreeLine+
    //   DocDescriptionSection ::= DocBlankLine DocFreeLine+
    //   DocEntry           ::= ('@'|'\') DocTagName DocEntryContent
    //   DocEntryContent    ::= { DocFreeLine }   (* continuation lines *)
    //
    // Free lines (non-tag lines) before the first @-tag form the free-text
    // portion (brief + description separated by blank lines).
    // Each @-tag line outside Markdown code starts a new DocEntry; subsequent
    // lines are appended to that entry's content (continuation).

    auto lines = split_lines(full_text);

    ast::documentation   result;
    std::vector<std::string> free_lines;
    std::vector<std::string> entry_lines;
    markdown_code code;
    bool in_entry = false;  // true once we have seen at least one @-tag

    for (const auto& raw_line : lines) {
        auto tag = code.consume(raw_line) ? tag_parse_result{} : parse_tag_line(raw_line);

        if (tag.valid) {
            if (in_entry) result.entries.back().content = join_lines(entry_lines);
            // Start a new DocEntry.
            result.entries.push_back(ast::doc_entry{ tag.name, {} });
            entry_lines = {tag.rest};
            code.consume(tag.rest);
            in_entry = true;
            continue;
        }

        if (in_entry) {
            // Continuation line for the current DocEntry.
            entry_lines.push_back(raw_line);
        } else {
            // DocFreeLine or DocBlankLine in the free-text section.
            free_lines.push_back(raw_line);
        }
    }
    if (in_entry) result.entries.back().content = join_lines(entry_lines);

    // Step 3 — Split free text into DocBriefSection + DocDescriptionSection.
    split_brief_and_description(free_lines, result.brief, result.description);

    if (result.empty()) return std::nullopt;
    return result;
}

} // namespace k::parse
