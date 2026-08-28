/*
 * K Language standard library — symbol mangling C API
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

#include "symbol_mangling.h"

#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    char* data;
    size_t len;
    size_t cap;
} k_strbuf;

static int k_buf_reserve(k_strbuf* b, size_t extra) {
    if (b->len + extra + 1 <= b->cap) return 1;
    size_t next = b->cap ? b->cap * 2 : 64;
    while (next < b->len + extra + 1) next *= 2;
    char* p = (char*)realloc(b->data, next);
    if (!p) return 0;
    b->data = p;
    b->cap = next;
    return 1;
}

static int k_buf_append_n(k_strbuf* b, const char* s, size_t n) {
    if (!k_buf_reserve(b, n)) return 0;
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
    return 1;
}

static int k_buf_append(k_strbuf* b, const char* s) {
    return k_buf_append_n(b, s, strlen(s));
}

static int k_buf_append_uint(k_strbuf* b, size_t v) {
    char tmp[32];
    int n = snprintf(tmp, sizeof(tmp), "%zu", v);
    return n > 0 && k_buf_append_n(b, tmp, (size_t)n);
}

static char* k_buf_take(k_strbuf* b) {
    if (!b->data && !k_buf_append(b, "")) return NULL;
    char* out = b->data;
    b->data = NULL;
    b->len = b->cap = 0;
    return out;
}

static void k_trim(const char** start, size_t* len) {
    while (*len && isspace((unsigned char)**start)) {
        ++*start;
        --*len;
    }
    while (*len && isspace((unsigned char)(*start)[*len - 1])) --*len;
}

static int k_starts_with(const char* s, size_t len, const char* prefix) {
    size_t plen = strlen(prefix);
    return len >= plen && memcmp(s, prefix, plen) == 0;
}

static int k_mangle_qualified(k_strbuf* out, const char* name, size_t len) {
    k_trim(&name, &len);
    while (len >= 2 && name[0] == ':' && name[1] == ':') {
        name += 2;
        len -= 2;
    }
    if (!len || !k_buf_append(out, "N")) return 0;
    size_t pos = 0;
    while (pos <= len) {
        size_t next = pos;
        while (next < len && !(name[next] == ':' && next + 1 < len && name[next + 1] == ':')) ++next;
        size_t part_len = next - pos;
        if (!part_len) return 0;
        if (!k_buf_append_uint(out, part_len) || !k_buf_append_n(out, name + pos, part_len)) return 0;
        if (next >= len) break;
        pos = next + 2;
    }
    return k_buf_append(out, "E");
}

static int k_mangle_type(k_strbuf* out, const char* type, size_t len) {
    k_trim(&type, &len);
    if (!len) return 0;
    if (k_starts_with(type, len, "const ")) {
        return k_buf_append(out, "K") && k_mangle_type(out, type + 6, len - 6);
    }
    if (len >= 1) {
        char c = type[len - 1];
        const char* p = NULL;
        if (c == '*') p = "P";
        else if (c == '&') p = "R";
        else if (c == '+') p = "L";
        else if (c == '?') p = "Q";
        else if (c == '!') p = "W";
        else if (c == '#') p = "D";
        if (p) return k_buf_append(out, p) && k_mangle_type(out, type, len - 1);
    }
#define TYPE_MATCH(text, code) if (len == strlen(text) && memcmp(type, text, len) == 0) return k_buf_append(out, code)
    TYPE_MATCH("void", "v");
    TYPE_MATCH("bool", "b");
    TYPE_MATCH("char", "c");
    TYPE_MATCH("byte", "a");
    TYPE_MATCH("unsigned byte", "h");
    TYPE_MATCH("ubyte", "h");
    TYPE_MATCH("short", "s");
    TYPE_MATCH("unsigned short", "t");
    TYPE_MATCH("ushort", "t");
    TYPE_MATCH("int", "i");
    TYPE_MATCH("unsigned int", "j");
    TYPE_MATCH("uint", "j");
    TYPE_MATCH("long", "x");
    TYPE_MATCH("unsigned long", "y");
    TYPE_MATCH("ulong", "y");
    TYPE_MATCH("long long", "n");
    TYPE_MATCH("unsigned long long", "o");
    TYPE_MATCH("float", "f");
    TYPE_MATCH("double", "d");
    TYPE_MATCH("long double", "e");
#undef TYPE_MATCH
    return k_buf_append(out, "_K") && k_mangle_qualified(out, type, len);
}

static int k_mangle_callable(k_strbuf* out, const char* s, size_t len, const char* prefix) {
    const char* open = memchr(s, '(', len);
    const char* close = NULL;
    for (size_t i = len; i > 0; --i) {
        if (s[i - 1] == ')') {
            close = s + i - 1;
            break;
        }
    }
    if (!open || !close || close < open) return 0;
    if (!k_buf_append(out, "_K") || !k_buf_append(out, prefix)
        || !k_mangle_qualified(out, s, (size_t)(open - s))) return 0;
    const char* params = open + 1;
    size_t params_len = (size_t)(close - params);
    k_trim(&params, &params_len);
    if (!params_len || (params_len == 4 && memcmp(params, "void", 4) == 0)) return k_buf_append(out, "v");
    size_t pos = 0;
    while (pos <= params_len) {
        size_t next = pos;
        while (next < params_len && params[next] != ',') ++next;
        if (!k_mangle_type(out, params + pos, next - pos)) return 0;
        if (next >= params_len) break;
        pos = next + 1;
    }
    return 1;
}

char* __k_symbol_mangle(const char* readable) {
    if (!readable) return NULL;
    const char* s = readable;
    size_t len = strlen(readable);
    k_trim(&s, &len);
    k_strbuf out = {0};
    int ok = 0;
    if (k_starts_with(s, len, "function ")) ok = k_mangle_callable(&out, s + 9, len - 9, "F");
    else if (k_starts_with(s, len, "const member function ")) ok = k_mangle_callable(&out, s + 22, len - 22, "FMK");
    else if (k_starts_with(s, len, "virtual member function ")) ok = k_mangle_callable(&out, s + 24, len - 24, "FMv");
    else if (k_starts_with(s, len, "member function ")) ok = k_mangle_callable(&out, s + 16, len - 16, "FM");
    else if (k_starts_with(s, len, "variable ")) ok = k_buf_append(&out, "_KV") && k_mangle_qualified(&out, s + 9, len - 9);
    else if (k_starts_with(s, len, "vtable ")) ok = k_buf_append(&out, "_KTV") && k_mangle_qualified(&out, s + 7, len - 7);
    else if (k_starts_with(s, len, "rtti-function ")) ok = k_buf_append(&out, "_KTRF") && k_mangle_qualified(&out, s + 14, len - 14);
    else if (k_starts_with(s, len, "rtti-constructor ")) ok = k_buf_append(&out, "_KTRC") && k_mangle_qualified(&out, s + 17, len - 17);
    else if (k_starts_with(s, len, "rtti-unit ")) ok = k_buf_append(&out, "_KTRU") && k_mangle_qualified(&out, s + 10, len - 10);
    else if (k_starts_with(s, len, "rtti ")) ok = k_buf_append(&out, "_KTRI") && k_mangle_qualified(&out, s + 5, len - 5);
    else ok = k_buf_append(&out, "_K") && k_mangle_qualified(&out, s, len);
    if (!ok) {
        free(out.data);
        return NULL;
    }
    return k_buf_take(&out);
}

static int k_demangle_qualified(k_strbuf* out, const char* s, size_t len, size_t* pos) {
    if (*pos >= len || s[(*pos)++] != 'N') return 0;
    int first = 1;
    while (*pos < len && s[*pos] != 'E') {
        if (!isdigit((unsigned char)s[*pos])) return 0;
        size_t part_len = 0;
        while (*pos < len && isdigit((unsigned char)s[*pos])) part_len = part_len * 10 + (size_t)(s[(*pos)++] - '0');
        if (!part_len || *pos + part_len > len) return 0;
        if (!first && !k_buf_append(out, "::")) return 0;
        if (!k_buf_append_n(out, s + *pos, part_len)) return 0;
        *pos += part_len;
        first = 0;
    }
    return *pos < len && s[(*pos)++] == 'E' && !first;
}

static int k_demangle_type(k_strbuf* out, const char* s, size_t len, size_t* pos) {
    if (*pos >= len) return 0;
    if (*pos + 1 < len && s[*pos] == '_' && s[*pos + 1] == 'K') *pos += 2;
    if (*pos >= len) return 0;
    char c = s[(*pos)++];
    if (c == 'K') {
        return k_buf_append(out, "const ") && k_demangle_type(out, s, len, pos);
    }
    if (c == 'P' || c == 'R' || c == 'L' || c == 'Q' || c == 'W' || c == 'D') {
        if (!k_demangle_type(out, s, len, pos)) return 0;
        char suffix = c == 'P' ? '*' : c == 'R' ? '&' : c == 'L' ? '+' : c == 'Q' ? '?' : c == 'W' ? '!' : '#';
        return k_buf_append_n(out, &suffix, 1);
    }
    const char* text = NULL;
    switch (c) {
        case 'v': text = "void"; break;
        case 'b': text = "bool"; break;
        case 'c': text = "char"; break;
        case 'a': text = "byte"; break;
        case 'h': text = "unsigned byte"; break;
        case 's': text = "short"; break;
        case 't': text = "unsigned short"; break;
        case 'i': text = "int"; break;
        case 'j': text = "unsigned int"; break;
        case 'x': text = "long"; break;
        case 'y': text = "unsigned long"; break;
        case 'n': text = "long long"; break;
        case 'o': text = "unsigned long long"; break;
        case 'f': text = "float"; break;
        case 'd': text = "double"; break;
        case 'e': text = "long double"; break;
        case 'N':
            --*pos;
            return k_demangle_qualified(out, s, len, pos);
        default:
            return 0;
    }
    return k_buf_append(out, text);
}

char* __k_symbol_demangle(const char* mangled) {
    if (!mangled || strncmp(mangled, "_K", 2) != 0) return NULL;
    const char* s = mangled + 2;
    size_t len = strlen(s);
    const char* label = "symbol ";
    int callable = 0;
    if (strncmp(s, "FMvK", 4) == 0) { label = "const virtual member function "; s += 4; len -= 4; callable = 1; }
    else if (strncmp(s, "FMv", 3) == 0) { label = "virtual member function "; s += 3; len -= 3; callable = 1; }
    else if (strncmp(s, "FMK", 3) == 0) { label = "const member function "; s += 3; len -= 3; callable = 1; }
    else if (strncmp(s, "FM", 2) == 0) { label = "member function "; s += 2; len -= 2; callable = 1; }
    else if (strncmp(s, "F", 1) == 0) { label = "function "; s += 1; len -= 1; callable = 1; }
    else if (strncmp(s, "V", 1) == 0) { label = "variable "; s += 1; len -= 1; }
    else if (strncmp(s, "TV", 2) == 0) { label = "vtable "; s += 2; len -= 2; }
    else if (strncmp(s, "TRF", 3) == 0) { label = "rtti-function "; s += 3; len -= 3; }
    else if (strncmp(s, "TRC", 3) == 0) { label = "rtti-constructor "; s += 3; len -= 3; }
    else if (strncmp(s, "TRU", 3) == 0) { label = "rtti-unit "; s += 3; len -= 3; }
    else if (strncmp(s, "TRI", 3) == 0) { label = "rtti "; s += 3; len -= 3; }
    k_strbuf out = {0};
    size_t pos = 0;
    if (!k_buf_append(&out, label) || !k_demangle_qualified(&out, s, len, &pos)) {
        free(out.data);
        return NULL;
    }
    if (callable) {
        if (!k_buf_append(&out, "(")) {
            free(out.data);
            return NULL;
        }
        int first = 1;
        if (!(pos < len && s[pos] == 'v' && pos + 1 == len)) {
            while (pos < len) {
                if (!first && !k_buf_append(&out, ", ")) {
                    free(out.data);
                    return NULL;
                }
                if (!k_demangle_type(&out, s, len, &pos)) {
                    free(out.data);
                    return NULL;
                }
                first = 0;
            }
        }
        if (!k_buf_append(&out, ")")) {
            free(out.data);
            return NULL;
        }
    } else if (pos != len) {
        free(out.data);
        return NULL;
    }
    return k_buf_take(&out);
}

void __k_symbol_string_free(char* value) {
    free(value);
}
