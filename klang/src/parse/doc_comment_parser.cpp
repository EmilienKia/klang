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

/// Returns true when every character in the line is a pure-decoration
/// character (=, -, *, _, /, #, ~, +).  Such lines are dropped from
/// the cleaned output (they are border/separator lines with no content).
bool is_border_line(std::string_view line) {
    if (line.empty()) return false;
    for (char c : line) {
        if (c != '=' && c != '-' && c != '*' && c != '_'
            && c != '/' && c != '#' && c != '~' && c != '+') {
            return false;
        }
    }
    return true;
}

/// Clean a single-line doc comment (/// or //!).
/// Strips the 3-char prefix and one optional leading space.
std::string clean_line_doc(std::string_view raw, std::string_view prefix) {
    std::string_view body = raw;
    if (body.starts_with(prefix)) body = body.substr(prefix.size());
    if (!body.empty() && body.front() == ' ') body = body.substr(1);
    while (!body.empty() && (body.back() == ' ' || body.back() == '\t' || body.back() == '\r'))
        body = body.substr(0, body.size() - 1);
    return std::string(body);
}

/// Clean a block doc comment (/** or /*!).
/// Strips the opening marker, the closing */, per-line leading * decoration,
/// pure-decoration border lines, and normalises common leading indentation.
std::string clean_block_doc(std::string_view raw, std::string_view opening) {
    std::string_view body = raw;
    if (body.starts_with(opening)) body = body.substr(opening.size());
    if (body.ends_with("*/")) body = body.substr(0, body.size() - 2);

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

    // Per-line: strip leading whitespace, then a leading * or ! (but not */), then one space.
    std::vector<std::string> processed;
    processed.reserve(lines.size());
    for (auto& line : lines) {
        size_t start = 0;
        while (start < line.size() && (line[start] == ' ' || line[start] == '\t')) ++start;
        std::string s = line.substr(start);
        if (!s.empty() && (s[0] == '*' || s[0] == '!')) {
            if (s.size() < 2 || s[1] != '/') {  // don't eat '*/'
                s = s.substr(1);
                if (!s.empty() && s[0] == ' ') s = s.substr(1);
            }
        }
        // Trim trailing whitespace.
        while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
            s.pop_back();
        processed.push_back(std::move(s));
    }

    // Remove leading and trailing blank or pure-decoration lines.
    size_t first = 0, last = processed.size();
    while (first < last && (processed[first].empty() || is_border_line(processed[first]))) ++first;
    while (last > first && (processed[last - 1].empty() || is_border_line(processed[last - 1]))) --last;

    // Join remaining lines.
    std::string result;
    for (size_t i = first; i < last; ++i) {
        if (i > first) result += '\n';
        result += processed[i];
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
    while (!lines.empty() && is_blank(lines.back()))  lines.pop_back();
    if (lines.empty()) {
        brief.clear();
        description.clear();
        return;
    }

    size_t first_para_end = 0;
    while (first_para_end < lines.size() && !is_blank(lines[first_para_end])) ++first_para_end;

    std::vector<std::string> brief_lines(lines.begin(),
                                          lines.begin() + static_cast<std::ptrdiff_t>(first_para_end));
    brief = join_trimmed_paragraphs(brief_lines);

    size_t rest_begin = first_para_end;
    while (rest_begin < lines.size() && is_blank(lines[rest_begin])) ++rest_begin;
    if (rest_begin < lines.size()) {
        std::vector<std::string> desc_lines(
            lines.begin() + static_cast<std::ptrdiff_t>(rest_begin), lines.end());
        description = join_trimmed_paragraphs(desc_lines);
    } else {
        description.clear();
    }
}

/// Attempt to parse a DocEntry line: a line that starts (after trimming) with
/// '@' or '\' followed by an identifier (DocTagName).
/// Returns { true, tag_name_lower, rest_of_line } on success, { false, "", "" } otherwise.
struct tag_parse_result {
    bool valid   = false;
    std::string name;   ///< tag name, lower-cased
    std::string rest;   ///< content after the tag name (trimmed)
};

tag_parse_result parse_tag_line(const std::string& raw_line) {
    auto line = trim_copy(raw_line);
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
    out.rest  = trim_copy(line.substr(i));
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
    // Each @-tag line starts a new DocEntry; subsequent non-tag, non-blank
    // lines are appended to that entry's content (continuation).

    auto lines = split_lines(full_text);

    ast::documentation   result;
    std::vector<std::string> free_lines;
    bool in_entry = false;  // true once we have seen at least one @-tag

    for (const auto& raw_line : lines) {
        auto tag = parse_tag_line(raw_line);

        if (tag.valid) {
            // Start a new DocEntry.
            result.entries.push_back(ast::doc_entry{ tag.name, tag.rest });
            in_entry = true;
            continue;
        }

        if (in_entry) {
            // Continuation line for the current DocEntry.
            auto trimmed = trim_copy(raw_line);
            if (!trimmed.empty()) {
                auto& cur = result.entries.back();
                if (cur.content.empty()) {
                    cur.content = trimmed;
                } else {
                    cur.content = trim_copy(cur.content + "\n" + trimmed);
                }
            }
            // Blank continuation lines are silently dropped (Javadoc convention).
        } else {
            // DocFreeLine or DocBlankLine in the free-text section.
            free_lines.push_back(raw_line);
        }
    }

    // Step 3 — Split free text into DocBriefSection + DocDescriptionSection.
    split_brief_and_description(free_lines, result.brief, result.description);

    if (result.empty()) return std::nullopt;
    return result;
}

} // namespace k::parse
