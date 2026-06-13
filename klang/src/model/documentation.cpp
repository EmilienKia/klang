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

namespace k::model::doc {

namespace {

/// Split content at the first whitespace character.
/// Returns { first_word, rest_trimmed }.
std::pair<std::string, std::string> split_first_word(const std::string& content) {
    auto pos = content.find_first_of(" \t");
    if (pos == std::string::npos) {
        return { content, {} };
    }
    std::string first = content.substr(0, pos);
    std::string rest  = content.substr(pos + 1);
    // Trim leading whitespace from rest.
    size_t start = 0;
    while (start < rest.size() && (rest[start] == ' ' || rest[start] == '\t')) ++start;
    return { first, rest.substr(start) };
}

} // anonymous namespace

std::shared_ptr<doc_entity> build_doc_entity(
    const std::shared_ptr<element>& owner,
    const parse::ast::documentation& doc)
{
    auto out = std::make_shared<doc_entity>();
    out->brief       = doc.brief;
    out->description = doc.description;
    out->owner       = owner;
    return out;
}

std::shared_ptr<function_doc> build_function_doc(
    const std::shared_ptr<element>& owner,
    const parse::ast::documentation& doc)
{
    auto out = std::make_shared<function_doc>();
    out->brief       = doc.brief;
    out->description = doc.description;
    out->owner       = owner;

    // Semantic interpretation of generic doc_entry list.
    // The tag vocabulary is open: well-known tags are mapped to typed fields;
    // any unrecognised tag is stored as a tagged_doc for tooling consumption.
    //
    // Well-known tags (case-insensitive; already lower-cased by the parser):
    //   param              → param_doc { name, description }
    //   return / returns   → return_doc { description }
    //   throws / throw /
    //     exception        → throws_doc { type_name, description }
    //   tparam / typeparam → template_param_doc { name, description }
    //   <anything else>    → tagged_doc { tag, value }

    for (const auto& entry : doc.entries) {
        const auto& tag     = entry.tag;     // already lower-cased
        const auto& content = entry.content;

        if (tag == "param") {
            auto [name, desc] = split_first_word(content);
            out->params.push_back(param_doc{ name, desc });

        } else if (tag == "return" || tag == "returns") {
            out->returns = return_doc{ content };

        } else if (tag == "throws" || tag == "throw" || tag == "exception") {
            auto [type_name, desc] = split_first_word(content);
            out->throws.push_back(throws_doc{ type_name, desc });

        } else if (tag == "tparam" || tag == "typeparam") {
            auto [name, desc] = split_first_word(content);
            out->template_params.push_back(template_param_doc{ name, desc });

        } else {
            out->tags.push_back(tagged_doc{ tag, content });
        }
    }

    return out;
}

} // namespace k::model::doc
