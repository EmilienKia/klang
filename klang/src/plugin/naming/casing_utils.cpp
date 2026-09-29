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

#include "casing_utils.hpp"
#include <cctype>

namespace k::plugin::naming {

void split_words(std::string_view name,
                 std::string& prefix_underscores,
                 std::vector<std::string>& words,
                 std::string& suffix_underscores) {
    prefix_underscores.clear();
    words.clear();
    suffix_underscores.clear();

    if (name.empty()) return;

    size_t start = 0;
    while (start < name.size() && name[start] == '_') {
        prefix_underscores += '_';
        ++start;
    }

    if (start >= name.size()) {
        // Name consists solely of underscores
        return;
    }

    size_t end = name.size();
    while (end > start && name[end - 1] == '_') {
        suffix_underscores += '_';
        --end;
    }

    std::string current_word;
    for (size_t i = start; i < end; ++i) {
        char c = name[i];
        if (c == '_' || c == '-') {
            if (!current_word.empty()) {
                words.push_back(std::move(current_word));
                current_word.clear();
            }
            continue;
        }

        // Detect camelCase/PascalCase word boundary:
        // Lowercase followed by Uppercase (e.g. "myVar" -> 'y' | 'V')
        // Or Acronym transition (e.g. "HTMLParser" -> 'L' | 'P' where 'P' is followed by lowercase)
        if (!current_word.empty() && std::isupper(static_cast<unsigned char>(c))) {
            bool prev_lower = std::islower(static_cast<unsigned char>(name[i - 1]));
            bool next_lower = (i + 1 < end) && std::islower(static_cast<unsigned char>(name[i + 1]));
            if (prev_lower || (std::isupper(static_cast<unsigned char>(name[i - 1])) && next_lower)) {
                words.push_back(std::move(current_word));
                current_word.clear();
            }
        }

        current_word += c;
    }

    if (!current_word.empty()) {
        words.push_back(std::move(current_word));
    }
}

std::string to_pascal_case(std::string_view name) {
    std::string prefix, suffix;
    std::vector<std::string> words;
    split_words(name, prefix, words, suffix);

    if (words.empty()) return std::string(name);

    std::string result = prefix;
    for (const auto& w : words) {
        if (w.empty()) continue;
        result += static_cast<char>(std::toupper(static_cast<unsigned char>(w[0])));
        for (size_t i = 1; i < w.size(); ++i) {
            result += static_cast<char>(std::tolower(static_cast<unsigned char>(w[i])));
        }
    }
    result += suffix;
    return result;
}

std::string to_camel_case(std::string_view name) {
    std::string prefix, suffix;
    std::vector<std::string> words;
    split_words(name, prefix, words, suffix);

    if (words.empty()) return std::string(name);

    std::string result = prefix;
    for (size_t idx = 0; idx < words.size(); ++idx) {
        const auto& w = words[idx];
        if (w.empty()) continue;
        if (idx == 0) {
            for (char c : w) {
                result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
        } else {
            result += static_cast<char>(std::toupper(static_cast<unsigned char>(w[0])));
            for (size_t i = 1; i < w.size(); ++i) {
                result += static_cast<char>(std::tolower(static_cast<unsigned char>(w[i])));
            }
        }
    }
    result += suffix;
    return result;
}

std::string to_screaming_snake_case(std::string_view name) {
    std::string prefix, suffix;
    std::vector<std::string> words;
    split_words(name, prefix, words, suffix);

    if (words.empty()) return std::string(name);

    std::string result = prefix;
    for (size_t idx = 0; idx < words.size(); ++idx) {
        if (idx > 0) result += '_';
        for (char c : words[idx]) {
            result += static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        }
    }
    result += suffix;
    return result;
}

std::string to_snake_case(std::string_view name) {
    std::string prefix, suffix;
    std::vector<std::string> words;
    split_words(name, prefix, words, suffix);

    if (words.empty()) return std::string(name);

    std::string result = prefix;
    for (size_t idx = 0; idx < words.size(); ++idx) {
        if (idx > 0) result += '_';
        for (char c : words[idx]) {
            result += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    result += suffix;
    return result;
}

} // namespace k::plugin::naming
