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

#include "kdi_mangling.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

namespace kdi {
namespace {

std::string trim(std::string_view sv) {
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.front()))) sv.remove_prefix(1);
    while (!sv.empty() && std::isspace(static_cast<unsigned char>(sv.back()))) sv.remove_suffix(1);
    return std::string(sv);
}

bool consume(std::string_view& sv, std::string_view prefix) {
    if (sv.substr(0, prefix.size()) != prefix) return false;
    sv.remove_prefix(prefix.size());
    return true;
}

std::vector<std::string> split_qualified(std::string_view fq_name) {
    while (fq_name.substr(0, 2) == "::") fq_name.remove_prefix(2);
    std::vector<std::string> parts;
    std::size_t pos = 0;
    while (pos <= fq_name.size()) {
        auto next = fq_name.find("::", pos);
        auto part = next == std::string_view::npos ? fq_name.substr(pos) : fq_name.substr(pos, next - pos);
        if (part.empty()) throw kdi_mangling_error("empty qualified-name component");
        parts.emplace_back(part);
        if (next == std::string_view::npos) break;
        pos = next + 2;
    }
    if (parts.empty()) throw kdi_mangling_error("empty qualified name");
    return parts;
}

std::string join_qualified(const std::vector<std::string>& parts) {
    std::ostringstream out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i) out << "::";
        out << parts[i];
    }
    return out.str();
}

std::string mangle_short(std::string_view part) {
    return std::to_string(part.size()) + std::string(part);
}

std::string mangle_qualified(std::string_view fq_name) {
    std::ostringstream out;
    out << "N";
    for (const auto& part : split_qualified(fq_name)) out << mangle_short(part);
    out << "E";
    return out.str();
}

std::vector<std::string> demangle_qualified(std::string_view mangled, std::size_t& pos) {
    if (pos >= mangled.size() || mangled[pos] != 'N') {
        throw kdi_mangling_error("expected qualified-name prefix 'N'");
    }
    ++pos;
    std::vector<std::string> parts;
    while (pos < mangled.size() && mangled[pos] != 'E') {
        if (!std::isdigit(static_cast<unsigned char>(mangled[pos]))) {
            throw kdi_mangling_error("expected length-prefixed name component");
        }
        std::size_t len = 0;
        while (pos < mangled.size() && std::isdigit(static_cast<unsigned char>(mangled[pos]))) {
            len = len * 10 + static_cast<std::size_t>(mangled[pos] - '0');
            ++pos;
        }
        if (len == 0 || pos + len > mangled.size()) {
            throw kdi_mangling_error("invalid qualified-name component length");
        }
        parts.emplace_back(mangled.substr(pos, len));
        pos += len;
        if (pos < mangled.size() && mangled[pos] == 'I') {
            std::size_t depth = 1;
            std::size_t start = pos++;
            while (pos < mangled.size() && depth > 0) {
                if (mangled[pos] == 'I') ++depth;
                else if (mangled[pos] == 'E') --depth;
                ++pos;
            }
            if (depth != 0) throw kdi_mangling_error("unterminated template arguments");
            parts.back() += std::string(mangled.substr(start, pos - start));
        }
    }
    if (pos >= mangled.size() || mangled[pos] != 'E') {
        throw kdi_mangling_error("unterminated qualified name");
    }
    ++pos;
    if (parts.empty()) throw kdi_mangling_error("empty qualified name");
    return parts;
}

std::string demangle_type(std::string_view mangled, std::size_t& pos);

std::string demangle_type_atom(std::string_view mangled, std::size_t& pos) {
    if (pos >= mangled.size()) throw kdi_mangling_error("unexpected end of type encoding");
    if (mangled.substr(pos, 2) == "_K") {
        pos += 2;
        return demangle_type_atom(mangled, pos);
    }
    switch (mangled[pos++]) {
        case 'v': return "void";
        case 'b': return "bool";
        case 'c': return "char";
        case 'a': return "byte";
        case 'h': return "unsigned byte";
        case 's': return "short";
        case 't': return "unsigned short";
        case 'i': return "int";
        case 'j': return "unsigned int";
        case 'x': return "long";
        case 'y': return "unsigned long";
        case 'n': return "long long";
        case 'o': return "unsigned long long";
        case 'f': return "float";
        case 'd': return "double";
        case 'e': return "long double";
        case 'N': {
            --pos;
            return join_qualified(demangle_qualified(mangled, pos));
        }
        case 'T': {
            if (pos < mangled.size() && mangled[pos] == 'e') {
                ++pos;
                if (mangled.substr(pos, 2) == "_K") pos += 2;
                return "enum " + join_qualified(demangle_qualified(mangled, pos));
            }
            break;
        }
        default:
            break;
    }
    throw kdi_mangling_error("unsupported type encoding");
}

std::string demangle_type(std::string_view mangled, std::size_t& pos) {
    if (pos >= mangled.size()) throw kdi_mangling_error("unexpected end of type encoding");
    char c = mangled[pos];
    if (c == 'K') {
        ++pos;
        return "const " + demangle_type(mangled, pos);
    }
    if (c == 'P' || c == 'R' || c == 'L' || c == 'Q' || c == 'W' || c == 'D') {
        ++pos;
        std::string inner = demangle_type(mangled, pos);
        switch (c) {
            case 'P': return inner + "*";
            case 'R': return inner + "&";
            case 'L': return inner + "+";
            case 'Q': return inner + "?";
            case 'W': return inner + "!";
            case 'D': return inner + "#";
        }
    }
    if (c == 'A') {
        ++pos;
        std::string size;
        while (pos < mangled.size() && std::isdigit(static_cast<unsigned char>(mangled[pos]))) {
            size.push_back(mangled[pos++]);
        }
        if (!size.empty()) {
            if (pos >= mangled.size() || mangled[pos++] != '_') {
                throw kdi_mangling_error("invalid sized-array type encoding");
            }
        }
        return demangle_type(mangled, pos) + "[" + size + "]";
    }
    return demangle_type_atom(mangled, pos);
}

std::vector<std::string> split_params(std::string_view params) {
    std::vector<std::string> out;
    std::size_t start = 0;
    int nesting = 0;
    for (std::size_t i = 0; i < params.size(); ++i) {
        if (params[i] == '<' || params[i] == '(' || params[i] == '[') ++nesting;
        else if (params[i] == '>' || params[i] == ')' || params[i] == ']') --nesting;
        else if (params[i] == ',' && nesting == 0) {
            out.push_back(trim(params.substr(start, i - start)));
            start = i + 1;
        }
    }
    auto last = trim(params.substr(start));
    if (!last.empty()) out.push_back(std::move(last));
    return out;
}

std::string mangle_type(std::string_view type_name) {
    std::string t = trim(type_name);
    if (t.empty()) throw kdi_mangling_error("empty type");
    if (t.substr(0, 6) == "const ") return "K" + mangle_type(std::string_view(t).substr(6));
    if (t.size() >= 2 && t.substr(t.size() - 2) == "[]") return "A" + mangle_type(std::string_view(t).substr(0, t.size() - 2));
    if (t.size() >= 1) {
        char last = t.back();
        if (last == '*' || last == '&' || last == '+' || last == '?' || last == '!' || last == '#') {
            t.pop_back();
            const char* prefix = last == '*' ? "P" : last == '&' ? "R" : last == '+' ? "L" : last == '?' ? "Q" : last == '!' ? "W" : "D";
            return std::string(prefix) + mangle_type(t);
        }
    }
    if (t == "void") return "v";
    if (t == "bool") return "b";
    if (t == "char") return "c";
    if (t == "byte") return "a";
    if (t == "unsigned byte" || t == "ubyte") return "h";
    if (t == "short") return "s";
    if (t == "unsigned short" || t == "ushort") return "t";
    if (t == "int") return "i";
    if (t == "unsigned int" || t == "uint") return "j";
    if (t == "long") return "x";
    if (t == "unsigned long" || t == "ulong") return "y";
    if (t == "long long") return "n";
    if (t == "unsigned long long") return "o";
    if (t == "float") return "f";
    if (t == "double") return "d";
    if (t == "long double") return "e";
    if (t.substr(0, 5) == "enum ") return "Te_K" + mangle_qualified(std::string_view(t).substr(5));
    return "_K" + mangle_qualified(t);
}

std::string strip_keyword(std::string& s, std::string_view keyword) {
    if (s.substr(0, keyword.size()) != keyword) return {};
    s = trim(std::string_view(s).substr(keyword.size()));
    return std::string(keyword);
}

std::string mangle_callable(std::string_view readable, std::string_view prefix) {
    auto open = readable.find('(');
    auto close = readable.rfind(')');
    if (open == std::string_view::npos || close == std::string_view::npos || close < open) {
        throw kdi_mangling_error("callable symbol must use name(type, ...) syntax");
    }
    std::string name = trim(readable.substr(0, open));
    std::string params = trim(readable.substr(open + 1, close - open - 1));
    std::ostringstream out;
    out << "_K" << prefix << mangle_qualified(name);
    auto parsed_params = split_params(params);
    if (parsed_params.empty() || (parsed_params.size() == 1 && parsed_params[0] == "void")) {
        out << "v";
    } else {
        for (const auto& p : parsed_params) out << mangle_type(p);
    }
    return out.str();
}

std::string remove_leading_word(std::string& s) {
    auto sp = s.find(' ');
    if (sp == std::string::npos) return {};
    auto word = s.substr(0, sp);
    s = trim(std::string_view(s).substr(sp + 1));
    return word;
}

char* dup_c_string(const std::string& value) {
    char* out = static_cast<char*>(std::malloc(value.size() + 1));
    if (!out) return nullptr;
    std::memcpy(out, value.c_str(), value.size() + 1);
    return out;
}

} // namespace

std::string kdi_mangle_symbol(std::string_view readable) {
    std::string s = trim(readable);
    if (s.empty()) throw kdi_mangling_error("empty readable symbol");

    if (s.substr(0, 8) == "symbol: ") s = trim(std::string_view(s).substr(8));
    if (s.substr(0, 7) == "symbol ") s = trim(std::string_view(s).substr(7));

    if (s.substr(0, 22) == "const member function ") {
        s = trim(std::string_view(s).substr(22));
        return mangle_callable(s, "FMK");
    }
    if (s.substr(0, 24) == "virtual member function ") {
        s = trim(std::string_view(s).substr(24));
        return mangle_callable(s, "FMv");
    }
    if (s.substr(0, 16) == "member function ") {
        s = trim(std::string_view(s).substr(16));
        return mangle_callable(s, "FM");
    }
    if (s.substr(0, 9) == "function ") {
        s = trim(std::string_view(s).substr(9));
        return mangle_callable(s, "F");
    }
    if (s.substr(0, 9) == "variable ") {
        s = trim(std::string_view(s).substr(9));
        return "_KV" + mangle_qualified(s);
    }
    if (s.substr(0, 7) == "vtable ") {
        s = trim(std::string_view(s).substr(7));
        return "_KTV" + mangle_qualified(s);
    }
    if (s.substr(0, 14) == "rtti-function ") {
        s = trim(std::string_view(s).substr(14));
        return "_KTRF" + mangle_qualified(s);
    }
    if (s.substr(0, 17) == "rtti-constructor ") {
        s = trim(std::string_view(s).substr(17));
        return "_KTRC" + mangle_qualified(s);
    }
    if (s.substr(0, 10) == "rtti-unit ") {
        s = trim(std::string_view(s).substr(10));
        return "_KTRU" + mangle_qualified(s);
    }
    if (s.substr(0, 5) == "rtti ") {
        s = trim(std::string_view(s).substr(5));
        return "_KTRI" + mangle_qualified(s);
    }
    return "_K" + mangle_qualified(s);
}

std::string kdi_demangle_symbol(std::string_view mangled) {
    std::string_view sv = mangled;
    if (!consume(sv, "_K")) throw kdi_mangling_error("K symbol must start with _K");

    std::string label = "symbol ";
    bool callable = false;
    if (consume(sv, "FMvK")) { label = "const virtual member function "; callable = true; }
    else if (consume(sv, "FMv")) { label = "virtual member function "; callable = true; }
    else if (consume(sv, "FMK")) { label = "const member function "; callable = true; }
    else if (consume(sv, "FM")) { label = "member function "; callable = true; }
    else if (consume(sv, "F")) { label = "function "; callable = true; }
    else if (consume(sv, "V")) label = "variable ";
    else if (consume(sv, "TV")) label = "vtable ";
    else if (consume(sv, "TRF")) label = "rtti-function ";
    else if (consume(sv, "TRC")) label = "rtti-constructor ";
    else if (consume(sv, "TRU")) label = "rtti-unit ";
    else if (consume(sv, "TRI")) label = "rtti ";

    std::size_t pos = 0;
    std::string name = join_qualified(demangle_qualified(sv, pos));
    if (!callable) {
        if (pos != sv.size()) throw kdi_mangling_error("trailing data after non-callable symbol");
        return label + name;
    }

    std::vector<std::string> params;
    if (pos < sv.size() && sv[pos] == 'v' && pos + 1 == sv.size()) {
        ++pos;
    } else {
        while (pos < sv.size()) params.push_back(demangle_type(sv, pos));
    }
    std::ostringstream out;
    out << label << name << "(";
    for (std::size_t i = 0; i < params.size(); ++i) {
        if (i) out << ", ";
        out << params[i];
    }
    out << ")";
    return out.str();
}

} // namespace kdi

char* kdi_symbol_mangle(const char* readable) {
    if (!readable) return nullptr;
    try {
        return kdi::dup_c_string(kdi::kdi_mangle_symbol(readable));
    } catch (...) {
        return nullptr;
    }
}

char* kdi_symbol_demangle(const char* mangled) {
    if (!mangled) return nullptr;
    try {
        return kdi::dup_c_string(kdi::kdi_demangle_symbol(mangled));
    } catch (...) {
        return nullptr;
    }
}

void kdi_symbol_string_free(char* value) {
    std::free(value);
}
