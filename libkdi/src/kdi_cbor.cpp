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

/**
 * @file kdi_cbor.cpp
 *
 * CBOR serialisation / deserialisation for KDI files.
 *
 * Uses libcbor (https://libcbor.readthedocs.io/) for low-level CBOR encoding.
 *
 * Encoding strategy
 * -----------------
 * Every DTO is encoded as a CBOR map with text-string keys matching the field
 * names used in the abstract schema (kdi-schema-abstract.md).
 * Optional fields are omitted from the map when absent.
 * Enum values are encoded as unsigned integers (their underlying uint8_t value).
 *
 * NOTE (phase 1): this file contains only stub implementations that throw
 * std::runtime_error("not implemented").  Full implementations are provided
 * in phase 2.
 */

#include "kdi_cbor.hpp"

#include <cbor.h>

#include <fstream>
#include <stdexcept>
#include <vector>

namespace kdi {

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers — forward declarations
// ─────────────────────────────────────────────────────────────────────────────

namespace {

// ── Serialisation helpers ────────────────────────────────────────────────────

cbor_item_t* encode_file(const kdi_file& file);

// ── Deserialisation helpers ──────────────────────────────────────────────────

kdi_file decode_file(cbor_item_t* item);

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

void kdi_write_cbor(const kdi_file& file, std::ostream& out) {
    cbor_item_t* root = encode_file(file);

    // Serialise to a heap buffer
    unsigned char* buffer = nullptr;
    size_t buffer_size    = 0;
    size_t written        = cbor_serialize_alloc(root, &buffer, &buffer_size);
    cbor_decref(&root);

    if (written == 0 || buffer == nullptr) {
        if (buffer) free(buffer);
        throw std::runtime_error("kdi_write_cbor: cbor_serialize_alloc failed");
    }

    out.write(reinterpret_cast<const char*>(buffer), static_cast<std::streamsize>(written));
    free(buffer);

    if (!out) {
        throw std::runtime_error("kdi_write_cbor: I/O error writing to stream");
    }
}

kdi_file kdi_read_cbor(std::istream& in) {
    // Read the entire stream into a buffer
    std::vector<unsigned char> buf(
        (std::istreambuf_iterator<char>(in)),
        std::istreambuf_iterator<char>());

    if (buf.empty()) {
        throw kdi_parse_error("kdi_read_cbor: empty input");
    }

    struct cbor_load_result load_result{};
    cbor_item_t* root = cbor_load(buf.data(), buf.size(), &load_result);

    if (load_result.error.code != CBOR_ERR_NONE) {
        throw kdi_parse_error(
            std::string("kdi_read_cbor: CBOR load error at offset ")
            + std::to_string(load_result.error.position));
    }

    kdi_file result;
    try {
        result = decode_file(root);
    } catch (...) {
        cbor_decref(&root);
        throw;
    }
    cbor_decref(&root);
    return result;
}

bool kdi_write_cbor_file(const kdi_file& file, const std::string& path) {
    std::ofstream out(path, std::ios::binary);
    if (!out.is_open()) return false;
    try {
        kdi_write_cbor(file, out);
        return true;
    } catch (...) {
        return false;
    }
}

kdi_file kdi_read_cbor_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        throw std::runtime_error("kdi_read_cbor_file: cannot open '" + path + "'");
    }
    return kdi_read_cbor(in);
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal implementation
// ─────────────────────────────────────────────────────────────────────────────

namespace {

// ─── tiny helpers ────────────────────────────────────────────────────────────

/** Create a CBOR text-string item from a std::string. */
cbor_item_t* cbor_str(const std::string& s) {
    return cbor_build_string(s.c_str());
}

/** Create a CBOR boolean item. */
cbor_item_t* cbor_bool(bool v) {
    return cbor_build_bool(v);
}

/** Append a key/value pair to a CBOR map. */
void map_push(cbor_item_t* map, const std::string& key, cbor_item_t* value) {
    struct cbor_pair pair;
    pair.key   = cbor_move(cbor_str(key));
    pair.value = cbor_move(value);
    cbor_map_add(map, pair);
}

/** Get a value from a CBOR map by text-string key. Returns nullptr if absent. */
cbor_item_t* map_get(cbor_item_t* map, const std::string& key) {
    if (!cbor_isa_map(map)) return nullptr;
    size_t n = cbor_map_size(map);
    struct cbor_pair* pairs = cbor_map_handle(map);
    for (size_t i = 0; i < n; ++i) {
        if (cbor_isa_string(pairs[i].key)) {
            std::string k(reinterpret_cast<const char*>(cbor_string_handle(pairs[i].key)),
                          cbor_string_length(pairs[i].key));
            if (k == key) return pairs[i].value;
        }
    }
    return nullptr;
}

/** Read a text string from a CBOR item, throw on type mismatch. */
std::string read_string(cbor_item_t* item, const std::string& path) {
    if (!item || !cbor_isa_string(item)) {
        throw kdi_parse_error("expected string at " + path);
    }
    return {reinterpret_cast<const char*>(cbor_string_handle(item)),
            cbor_string_length(item)};
}

/** Read a required string field from a map. */
std::string req_string(cbor_item_t* map, const std::string& key, const std::string& path) {
    auto* v = map_get(map, key);
    return read_string(v, path + "." + key);
}

/** Read an optional string field (returns "" if absent). */
std::string opt_string(cbor_item_t* map, const std::string& key) {
    auto* v = map_get(map, key);
    if (!v) return {};
    if (!cbor_isa_string(v)) return {};
    return {reinterpret_cast<const char*>(cbor_string_handle(v)),
            cbor_string_length(v)};
}

/** Read a uint from a CBOR item. */
uint64_t read_uint(cbor_item_t* item, const std::string& path) {
    if (!item || !cbor_isa_uint(item)) {
        throw kdi_parse_error("expected uint at " + path);
    }
    return cbor_get_uint64(item);
}

/** Read a required uint from a map. */
uint64_t req_uint(cbor_item_t* map, const std::string& key, const std::string& path) {
    auto* v = map_get(map, key);
    return read_uint(v, path + "." + key);
}

/** Read an optional uint (returns default if absent). */
uint64_t opt_uint(cbor_item_t* map, const std::string& key, uint64_t def = 0) {
    auto* v = map_get(map, key);
    if (!v) return def;
    if (cbor_isa_uint(v)) return cbor_get_uint64(v);
    return def;
}

/** Read a boolean from a CBOR item. */
bool read_bool(cbor_item_t* item, const std::string& path) {
    if (!item || !cbor_is_bool(item)) {
        throw kdi_parse_error("expected bool at " + path);
    }
    return cbor_ctrl_value(item) == CBOR_CTRL_TRUE;
}

bool opt_bool(cbor_item_t* map, const std::string& key, bool def = false) {
    auto* v = map_get(map, key);
    if (!v || !cbor_is_bool(v)) return def;
    return cbor_ctrl_value(v) == CBOR_CTRL_TRUE;
}

// ─── int (may be negative) ────────────────────────────────────────────────────

cbor_item_t* cbor_int32(int32_t v) {
    if (v >= 0) return cbor_build_uint32(static_cast<uint32_t>(v));
    return cbor_build_negint32(static_cast<uint32_t>(-(v + 1)));
}

int32_t read_int32(cbor_item_t* item, const std::string& path) {
    if (!item) throw kdi_parse_error("expected int at " + path);
    if (cbor_isa_uint(item))   return static_cast<int32_t>(cbor_get_uint64(item));
    if (cbor_isa_negint(item)) return -static_cast<int32_t>(cbor_get_uint64(item)) - 1;
    throw kdi_parse_error("expected int at " + path);
}

int32_t opt_int32(cbor_item_t* map, const std::string& key, int32_t def = 0) {
    auto* v = map_get(map, key);
    if (!v) return def;
    if (cbor_isa_uint(v))   return static_cast<int32_t>(cbor_get_uint64(v));
    if (cbor_isa_negint(v)) return -static_cast<int32_t>(cbor_get_uint64(v)) - 1;
    return def;
}

// ─── visibility ───────────────────────────────────────────────────────────────

cbor_item_t* encode_visibility(kdi_visibility v) {
    return cbor_build_uint8(static_cast<uint8_t>(v));
}

kdi_visibility decode_visibility(cbor_item_t* map, const std::string& key,
                                  const std::string& path) {
    auto raw = static_cast<uint8_t>(opt_uint(map, key, 0));
    return static_cast<kdi_visibility>(raw);
}

// ─────────────────────────────────────────────────────────────────────────────
// Type encoding / decoding
// ─────────────────────────────────────────────────────────────────────────────

cbor_item_t* encode_type(const kdi_type& t);
kdi_type     decode_type(cbor_item_t* item, const std::string& path);

cbor_item_t* encode_type(const kdi_type& t) {
    cbor_item_t* m = cbor_new_indefinite_map();
    std::visit([&](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, kdi_void_type>) {
            map_push(m, "kind", cbor_str("void"));
        } else if constexpr (std::is_same_v<T, kdi_bool_type>) {
            map_push(m, "kind", cbor_str("bool"));
        } else if constexpr (std::is_same_v<T, kdi_char_type>) {
            map_push(m, "kind", cbor_str("char"));
        } else if constexpr (std::is_same_v<T, kdi_int_type>) {
            map_push(m, "kind",   cbor_str("int"));
            map_push(m, "bits",   cbor_build_uint32(v.bits));
            map_push(m, "signed", cbor_bool(v.is_signed));
        } else if constexpr (std::is_same_v<T, kdi_float_type>) {
            map_push(m, "kind", cbor_str("float"));
            map_push(m, "bits", cbor_build_uint32(v.bits));
        } else if constexpr (std::is_same_v<T, kdi_ref_type>) {
            map_push(m, "kind",  cbor_str("ref"));
            map_push(m, "inner", encode_type(*v.inner));
        } else if constexpr (std::is_same_v<T, kdi_ptr_type>) {
            map_push(m, "kind",  cbor_str("ptr"));
            map_push(m, "inner", encode_type(*v.inner));
        } else if constexpr (std::is_same_v<T, kdi_link_type>) {
            map_push(m, "kind",  cbor_str("link"));
            map_push(m, "inner", encode_type(*v.inner));
        } else if constexpr (std::is_same_v<T, kdi_view_type>) {
            map_push(m, "kind",  cbor_str("view"));
            map_push(m, "inner", encode_type(*v.inner));
        } else if constexpr (std::is_same_v<T, kdi_drain_type>) {
            map_push(m, "kind",  cbor_str("drain"));
            map_push(m, "inner", encode_type(*v.inner));
        } else if constexpr (std::is_same_v<T, kdi_const_type>) {
            map_push(m, "kind",  cbor_str("const"));
            map_push(m, "inner", encode_type(*v.inner));
        } else if constexpr (std::is_same_v<T, kdi_array_type>) {
            map_push(m, "kind", cbor_str("array"));
            map_push(m, "elem", encode_type(*v.elem));
        } else if constexpr (std::is_same_v<T, kdi_sized_array_type>) {
            map_push(m, "kind", cbor_str("sized_array"));
            map_push(m, "elem", encode_type(*v.elem));
            map_push(m, "size", cbor_build_uint64(v.size));
        } else if constexpr (std::is_same_v<T, kdi_fn_ref_type>) {
            map_push(m, "kind", cbor_str("fn_ref"));
            map_push(m, "ret",  encode_type(*v.ret));
            cbor_item_t* pa = cbor_new_indefinite_array();
            for (auto& p : v.params) cbor_array_push(pa, cbor_move(encode_type(*p)));
            map_push(m, "params", pa);
        } else if constexpr (std::is_same_v<T, kdi_aggregate_ref>) {
            map_push(m, "kind",    cbor_str("aggregate"));
            map_push(m, "fq_name", cbor_str(v.fq_name));
        } else if constexpr (std::is_same_v<T, kdi_enum_ref>) {
            map_push(m, "kind",    cbor_str("enum"));
            map_push(m, "fq_name", cbor_str(v.fq_name));
        }
    }, t.value);
    return m;
}

kdi_type decode_type(cbor_item_t* item, const std::string& path) {
    if (!item || !cbor_isa_map(item)) {
        throw kdi_parse_error("expected type map at " + path);
    }
    std::string kind = req_string(item, "kind", path);

    if (kind == "void")   return {kdi_void_type{}};
    if (kind == "bool")   return {kdi_bool_type{}};
    if (kind == "char")   return {kdi_char_type{}};
    if (kind == "int") {
        kdi_int_type t;
        t.bits      = static_cast<uint32_t>(opt_uint(item, "bits", 32));
        t.is_signed = opt_bool(item, "signed", true);
        return {t};
    }
    if (kind == "float") {
        kdi_float_type t;
        t.bits = static_cast<uint32_t>(opt_uint(item, "bits", 64));
        return {t};
    }
    auto decode_inner = [&](const std::string& k) {
        auto* inner = map_get(item, k);
        if (!inner) throw kdi_parse_error("missing '" + k + "' at " + path);
        return std::make_shared<kdi_type>(decode_type(inner, path + "." + k));
    };
    if (kind == "ref")    return {kdi_ref_type{decode_inner("inner")}};
    if (kind == "ptr")    return {kdi_ptr_type{decode_inner("inner")}};
    if (kind == "link")   return {kdi_link_type{decode_inner("inner")}};
    if (kind == "view") return {kdi_view_type{decode_inner("inner")}};
    if (kind == "drain") return {kdi_drain_type{decode_inner("inner")}};
    if (kind == "const")  return {kdi_const_type{decode_inner("inner")}};
    if (kind == "array")  return {kdi_array_type{decode_inner("elem")}};
    if (kind == "sized_array") {
        kdi_sized_array_type t;
        t.elem = decode_inner("elem");
        t.size = opt_uint(item, "size", 0);
        return {t};
    }
    if (kind == "fn_ref") {
        kdi_fn_ref_type t;
        auto* rp = map_get(item, "ret");
        if (!rp) throw kdi_parse_error("missing 'ret' at " + path);
        t.ret = std::make_shared<kdi_type>(decode_type(rp, path + ".ret"));
        auto* pa = map_get(item, "params");
        if (pa && cbor_isa_array(pa)) {
            size_t n = cbor_array_size(pa);
            for (size_t i = 0; i < n; ++i) {
                t.params.push_back(std::make_shared<kdi_type>(
                    decode_type(cbor_array_get(pa, i),
                                path + ".params[" + std::to_string(i) + "]")));
            }
        }
        return {t};
    }
    if (kind == "aggregate") {
        return {kdi_aggregate_ref{req_string(item, "fq_name", path)}};
    }
    if (kind == "enum") {
        return {kdi_enum_ref{req_string(item, "fq_name", path)}};
    }
    throw kdi_parse_error("unknown type kind '" + kind + "' at " + path);
}

// ─────────────────────────────────────────────────────────────────────────────
// Param
// ─────────────────────────────────────────────────────────────────────────────

cbor_item_t* encode_param(const kdi_param& p) {
    cbor_item_t* m = cbor_new_indefinite_map();
    map_push(m, "name", cbor_str(p.name));
    map_push(m, "type", encode_type(p.type));
    return m;
}

kdi_param decode_param(cbor_item_t* item, const std::string& path) {
    if (!item || !cbor_isa_map(item)) throw kdi_parse_error("expected param map at " + path);
    kdi_param p;
    p.name = req_string(item, "name", path);
    auto* tp = map_get(item, "type");
    if (!tp) throw kdi_parse_error("missing 'type' at " + path);
    p.type = decode_type(tp, path + ".type");
    return p;
}

cbor_item_t* encode_params(const std::vector<kdi_param>& params) {
    cbor_item_t* a = cbor_new_indefinite_array();
    for (auto& p : params) cbor_array_push(a, cbor_move(encode_param(p)));
    return a;
}

std::vector<kdi_param> decode_params(cbor_item_t* map, const std::string& key,
                                     const std::string& path) {
    std::vector<kdi_param> result;
    auto* a = map_get(map, key);
    if (!a || !cbor_isa_array(a)) return result;
    size_t n = cbor_array_size(a);
    for (size_t i = 0; i < n; ++i) {
        result.push_back(decode_param(cbor_array_get(a, i),
                                      path + "." + key + "[" + std::to_string(i) + "]"));
    }
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Variable / Function / Method
// ─────────────────────────────────────────────────────────────────────────────

cbor_item_t* encode_variable(const kdi_variable& v) {
    cbor_item_t* m = cbor_new_indefinite_map();
    map_push(m, "name",         cbor_str(v.name));
    map_push(m, "fq_name",      cbor_str(v.fq_name));
    map_push(m, "visibility",   encode_visibility(v.visibility));
    map_push(m, "type",         encode_type(v.type));
    if (v.is_const) map_push(m, "is_const", cbor_bool(true));
    map_push(m, "mangled_name", cbor_str(v.mangled_name));
    return m;
}

kdi_variable decode_variable(cbor_item_t* item, const std::string& path) {
    kdi_variable v;
    v.name         = req_string(item, "name", path);
    v.fq_name      = req_string(item, "fq_name", path);
    v.visibility   = decode_visibility(item, "visibility", path);
    auto* tp = map_get(item, "type");
    if (!tp) throw kdi_parse_error("missing 'type' at " + path);
    v.type         = decode_type(tp, path + ".type");
    v.is_const     = opt_bool(item, "is_const");
    v.mangled_name = req_string(item, "mangled_name", path);
    return v;
}

cbor_item_t* encode_function(const kdi_function& f) {
    cbor_item_t* m = cbor_new_indefinite_map();
    map_push(m, "name",         cbor_str(f.name));
    map_push(m, "fq_name",      cbor_str(f.fq_name));
    map_push(m, "visibility",   encode_visibility(f.visibility));
    if (f.is_static)   map_push(m, "is_static",   cbor_bool(true));
    if (f.is_operator) map_push(m, "is_operator",  cbor_bool(true));
    map_push(m, "return_type",  encode_type(f.return_type));
    map_push(m, "params",       encode_params(f.params));
    map_push(m, "mangled_name", cbor_str(f.mangled_name));
    map_push(m, "llvm_def",     cbor_str(f.llvm_def));
    return m;
}

kdi_function decode_function(cbor_item_t* item, const std::string& path) {
    kdi_function f;
    f.name         = req_string(item, "name", path);
    f.fq_name      = req_string(item, "fq_name", path);
    f.visibility   = decode_visibility(item, "visibility", path);
    f.is_static    = opt_bool(item, "is_static");
    f.is_operator  = opt_bool(item, "is_operator");
    auto* rt = map_get(item, "return_type");
    if (!rt) throw kdi_parse_error("missing 'return_type' at " + path);
    f.return_type  = decode_type(rt, path + ".return_type");
    f.params       = decode_params(item, "params", path);
    f.mangled_name = req_string(item, "mangled_name", path);
    f.llvm_def     = req_string(item, "llvm_def", path);
    return f;
}

cbor_item_t* encode_method(const kdi_method& m) {
    cbor_item_t* map = cbor_new_indefinite_map();
    map_push(map, "name",         cbor_str(m.name));
    map_push(map, "fq_name",      cbor_str(m.fq_name));
    map_push(map, "visibility",   encode_visibility(m.visibility));
    if (m.is_static)       map_push(map, "is_static",        cbor_bool(true));
    if (m.is_const_member) map_push(map, "is_const_member",  cbor_bool(true));
    if (m.is_virtual)      map_push(map, "is_virtual",       cbor_bool(true));
    if (m.is_abstract)     map_push(map, "is_abstract",      cbor_bool(true));
    if (m.is_final)        map_push(map, "is_final",         cbor_bool(true));
    if (m.is_operator)     map_push(map, "is_operator",      cbor_bool(true));
    if (m.vtable_slot >= 0) map_push(map, "vtable_slot",     cbor_build_uint32(static_cast<uint32_t>(m.vtable_slot)));
    map_push(map, "return_type",  encode_type(m.return_type));
    map_push(map, "params",       encode_params(m.params));
    map_push(map, "mangled_name", cbor_str(m.mangled_name));
    map_push(map, "llvm_def",     cbor_str(m.llvm_def));
    return map;
}

kdi_method decode_method(cbor_item_t* item, const std::string& path) {
    kdi_method m;
    m.name           = req_string(item, "name", path);
    m.fq_name        = req_string(item, "fq_name", path);
    m.visibility     = decode_visibility(item, "visibility", path);
    m.is_static      = opt_bool(item, "is_static");
    m.is_const_member= opt_bool(item, "is_const_member");
    m.is_virtual     = opt_bool(item, "is_virtual");
    m.is_abstract    = opt_bool(item, "is_abstract");
    m.is_final       = opt_bool(item, "is_final");
    m.is_operator    = opt_bool(item, "is_operator");
    m.vtable_slot    = opt_int32(item, "vtable_slot", -1);
    auto* rt = map_get(item, "return_type");
    if (!rt) throw kdi_parse_error("missing 'return_type' at " + path);
    m.return_type    = decode_type(rt, path + ".return_type");
    m.params         = decode_params(item, "params", path);
    m.mangled_name   = req_string(item, "mangled_name", path);
    m.llvm_def       = req_string(item, "llvm_def", path);
    return m;
}

cbor_item_t* encode_constructor(const kdi_constructor& c) {
    cbor_item_t* m = cbor_new_indefinite_map();
    map_push(m, "visibility",   encode_visibility(c.visibility));
    if (c.is_copy_constructor) map_push(m, "is_copy_constructor", cbor_bool(true));
    if (c.is_defaulted)        map_push(m, "is_defaulted",        cbor_bool(true));
    if (c.is_deleted)          map_push(m, "is_deleted",          cbor_bool(true));
    map_push(m, "params",          encode_params(c.params));
    map_push(m, "mangled_name",    cbor_str(c.mangled_name));
    map_push(m, "mangled_name_c2", cbor_str(c.mangled_name_c2));
    map_push(m, "llvm_def",        cbor_str(c.llvm_def));
    return m;
}

kdi_constructor decode_constructor(cbor_item_t* item, const std::string& path) {
    kdi_constructor c;
    c.visibility         = decode_visibility(item, "visibility", path);
    c.is_copy_constructor= opt_bool(item, "is_copy_constructor");
    c.is_defaulted       = opt_bool(item, "is_defaulted");
    c.is_deleted         = opt_bool(item, "is_deleted");
    c.params             = decode_params(item, "params", path);
    c.mangled_name       = req_string(item, "mangled_name", path);
    c.mangled_name_c2    = opt_string(item, "mangled_name_c2");
    c.llvm_def           = req_string(item, "llvm_def", path);
    return c;
}

cbor_item_t* encode_destructor(const kdi_destructor& d) {
    cbor_item_t* m = cbor_new_indefinite_map();
    map_push(m, "visibility",   encode_visibility(d.visibility));
    if (d.is_virtual)            map_push(m, "is_virtual",            cbor_bool(true));
    if (d.is_compiler_generated) map_push(m, "is_compiler_generated", cbor_bool(true));
    map_push(m, "mangled_name",    cbor_str(d.mangled_name));
    map_push(m, "mangled_name_d2", cbor_str(d.mangled_name_d2));
    map_push(m, "llvm_def",        cbor_str(d.llvm_def));
    return m;
}

kdi_destructor decode_destructor(cbor_item_t* item, const std::string& path) {
    kdi_destructor d;
    d.visibility            = decode_visibility(item, "visibility", path);
    d.is_virtual            = opt_bool(item, "is_virtual");
    d.is_compiler_generated = opt_bool(item, "is_compiler_generated");
    d.mangled_name          = req_string(item, "mangled_name", path);
    d.mangled_name_d2       = opt_string(item, "mangled_name_d2");
    d.llvm_def              = req_string(item, "llvm_def", path);
    return d;
}

// ─────────────────────────────────────────────────────────────────────────────
// Layout fields
// ─────────────────────────────────────────────────────────────────────────────

cbor_item_t* encode_layout_field(const kdi_layout_field& f) {
    cbor_item_t* m = cbor_new_indefinite_map();
    std::visit([&](const auto& v) {
        using T = std::decay_t<decltype(v)>;
        map_push(m, "llvm_field_index", cbor_build_uint32(v.llvm_field_index));
        if constexpr (std::is_same_v<T, kdi_layout_member>) {
            map_push(m, "kind",         cbor_str("member"));
            map_push(m, "name",         cbor_str(v.name));
            map_push(m, "fq_name",      cbor_str(v.fq_name));
            map_push(m, "visibility",   encode_visibility(v.visibility));
            map_push(m, "type",         encode_type(v.type));
            if (v.is_const) map_push(m, "is_const", cbor_bool(true));
            map_push(m, "mangled_name", cbor_str(v.mangled_name));
        } else if constexpr (std::is_same_v<T, kdi_layout_vptr>) {
            map_push(m, "kind",          cbor_str("vptr"));
            map_push(m, "vtable_symbol", cbor_str(v.vtable_symbol));
        } else if constexpr (std::is_same_v<T, kdi_layout_vptr_secondary>) {
            map_push(m, "kind",          cbor_str("vptr_secondary"));
            map_push(m, "base_fq_name",  cbor_str(v.base_fq_name));
            map_push(m, "vtable_symbol", cbor_str(v.vtable_symbol));
        } else if constexpr (std::is_same_v<T, kdi_layout_base_subobject>) {
            map_push(m, "kind",         cbor_str("base_subobject"));
            map_push(m, "base_fq_name", cbor_str(v.base_fq_name));
        } else if constexpr (std::is_same_v<T, kdi_layout_vbptr>) {
            map_push(m, "kind",          cbor_str("vbptr"));
            map_push(m, "vbase_fq_name", cbor_str(v.vbase_fq_name));
        } else if constexpr (std::is_same_v<T, kdi_layout_vbase_subobject>) {
            map_push(m, "kind",          cbor_str("vbase_subobject"));
            map_push(m, "vbase_fq_name", cbor_str(v.vbase_fq_name));
        } else if constexpr (std::is_same_v<T, kdi_layout_parent_ref>) {
            map_push(m, "kind",            cbor_str("parent_ref"));
            map_push(m, "parent_fq_name",  cbor_str(v.parent_fq_name));
        } else if constexpr (std::is_same_v<T, kdi_layout_opaque_block>) {
            map_push(m, "kind",        cbor_str("opaque_block"));
            map_push(m, "field_count", cbor_build_uint32(v.field_count));
            map_push(m, "size_bits",   cbor_build_uint64(v.size_bits));
        }
    }, f);
    return m;
}

kdi_layout_field decode_layout_field(cbor_item_t* item, const std::string& path) {
    if (!item || !cbor_isa_map(item)) throw kdi_parse_error("expected layout field map at " + path);
    std::string kind = req_string(item, "kind", path);
    uint32_t idx = static_cast<uint32_t>(opt_uint(item, "llvm_field_index", 0));

    if (kind == "member") {
        kdi_layout_member f;
        f.llvm_field_index = idx;
        f.name         = req_string(item, "name", path);
        f.fq_name      = req_string(item, "fq_name", path);
        f.visibility   = decode_visibility(item, "visibility", path);
        auto* tp = map_get(item, "type");
        if (!tp) throw kdi_parse_error("missing 'type' at " + path);
        f.type         = decode_type(tp, path + ".type");
        f.is_const     = opt_bool(item, "is_const");
        f.mangled_name = opt_string(item, "mangled_name");
        return f;
    }
    if (kind == "vptr") {
        kdi_layout_vptr f;
        f.llvm_field_index = idx;
        f.vtable_symbol    = req_string(item, "vtable_symbol", path);
        return f;
    }
    if (kind == "vptr_secondary") {
        kdi_layout_vptr_secondary f;
        f.llvm_field_index = idx;
        f.base_fq_name     = req_string(item, "base_fq_name", path);
        f.vtable_symbol    = req_string(item, "vtable_symbol", path);
        return f;
    }
    if (kind == "base_subobject") {
        kdi_layout_base_subobject f;
        f.llvm_field_index = idx;
        f.base_fq_name     = req_string(item, "base_fq_name", path);
        return f;
    }
    if (kind == "vbptr") {
        kdi_layout_vbptr f;
        f.llvm_field_index = idx;
        f.vbase_fq_name    = req_string(item, "vbase_fq_name", path);
        return f;
    }
    if (kind == "vbase_subobject") {
        kdi_layout_vbase_subobject f;
        f.llvm_field_index = idx;
        f.vbase_fq_name    = req_string(item, "vbase_fq_name", path);
        return f;
    }
    if (kind == "parent_ref") {
        kdi_layout_parent_ref f;
        f.llvm_field_index = idx;
        f.parent_fq_name   = req_string(item, "parent_fq_name", path);
        return f;
    }
    if (kind == "opaque_block") {
        kdi_layout_opaque_block f;
        f.llvm_field_index = idx;
        f.field_count      = static_cast<uint32_t>(opt_uint(item, "field_count", 0));
        f.size_bits        = opt_uint(item, "size_bits", 0);
        return f;
    }
    throw kdi_parse_error("unknown layout field kind '" + kind + "' at " + path);
}

// ─────────────────────────────────────────────────────────────────────────────
// Vtable
// ─────────────────────────────────────────────────────────────────────────────

cbor_item_t* encode_vtable(const kdi_vtable& vt) {
    cbor_item_t* m = cbor_new_indefinite_map();
    map_push(m, "vtable_symbol", cbor_str(vt.vtable_symbol));
    map_push(m, "rtti_symbol",   cbor_str(vt.rtti_symbol));
    map_push(m, "llvm_def",      cbor_str(vt.llvm_def));

    cbor_item_t* slots = cbor_new_indefinite_array();
    for (auto& s : vt.slots) {
        cbor_item_t* sm = cbor_new_indefinite_map();
        map_push(sm, "slot_index",       cbor_build_uint32(s.slot_index));
        map_push(sm, "introducing_func", cbor_str(s.introducing_func));
        map_push(sm, "override_symbol",  cbor_str(s.override_symbol));
        if (s.is_abstract) map_push(sm, "is_abstract", cbor_bool(true));
        cbor_array_push(slots, cbor_move(sm));
    }
    map_push(m, "slots", slots);

    cbor_item_t* secs = cbor_new_indefinite_array();
    for (auto& sec : vt.secondary) {
        cbor_item_t* sm = cbor_new_indefinite_map();
        map_push(sm, "base_fq_name",   cbor_str(sec.base_fq_name));
        map_push(sm, "base_offset",    cbor_build_uint64(sec.base_offset));
        map_push(sm, "vtable_symbol",  cbor_str(sec.vtable_symbol));
        cbor_item_t* thunks = cbor_new_indefinite_array();
        for (auto& t : sec.thunks) {
            cbor_item_t* tm = cbor_new_indefinite_map();
            map_push(tm, "slot_index",       cbor_build_uint32(t.slot_index));
            map_push(tm, "real_func_symbol", cbor_str(t.real_func_symbol));
            map_push(tm, "this_adjustment",  cbor_int32(t.this_adjustment));
            if (t.needs_thunk) map_push(tm, "needs_thunk", cbor_bool(true));
            cbor_array_push(thunks, cbor_move(tm));
        }
        map_push(sm, "thunks", thunks);
        cbor_array_push(secs, cbor_move(sm));
    }
    map_push(m, "secondary", secs);
    return m;
}

kdi_vtable decode_vtable(cbor_item_t* item, const std::string& path) {
    kdi_vtable vt;
    vt.vtable_symbol = req_string(item, "vtable_symbol", path);
    vt.rtti_symbol   = req_string(item, "rtti_symbol", path);
    vt.llvm_def      = req_string(item, "llvm_def", path);

    auto* sa = map_get(item, "slots");
    if (sa && cbor_isa_array(sa)) {
        size_t n = cbor_array_size(sa);
        for (size_t i = 0; i < n; ++i) {
            auto* si = cbor_array_get(sa, i);
            auto sp = path + ".slots[" + std::to_string(i) + "]";
            kdi_vtable_slot s;
            s.slot_index       = static_cast<uint32_t>(opt_uint(si, "slot_index", i));
            s.introducing_func = req_string(si, "introducing_func", sp);
            s.override_symbol  = opt_string(si, "override_symbol");
            s.is_abstract      = opt_bool(si, "is_abstract");
            vt.slots.push_back(s);
        }
    }

    auto* sea = map_get(item, "secondary");
    if (sea && cbor_isa_array(sea)) {
        size_t n = cbor_array_size(sea);
        for (size_t i = 0; i < n; ++i) {
            auto* sei = cbor_array_get(sea, i);
            auto sep = path + ".secondary[" + std::to_string(i) + "]";
            kdi_secondary_vtable sec;
            sec.base_fq_name  = req_string(sei, "base_fq_name", sep);
            sec.base_offset   = opt_uint(sei, "base_offset", 0);
            sec.vtable_symbol = opt_string(sei, "vtable_symbol");
            auto* ta = map_get(sei, "thunks");
            if (ta && cbor_isa_array(ta)) {
                size_t tn = cbor_array_size(ta);
                for (size_t j = 0; j < tn; ++j) {
                    auto* ti = cbor_array_get(ta, j);
                    kdi_thunk t;
                    t.slot_index      = static_cast<uint32_t>(opt_uint(ti, "slot_index", j));
                    t.real_func_symbol= opt_string(ti, "real_func_symbol");
                    t.this_adjustment = opt_int32(ti, "this_adjustment", 0);
                    t.needs_thunk     = opt_bool(ti, "needs_thunk");
                    sec.thunks.push_back(t);
                }
            }
            vt.secondary.push_back(sec);
        }
    }
    return vt;
}

// ─────────────────────────────────────────────────────────────────────────────
// Base
// ─────────────────────────────────────────────────────────────────────────────

cbor_item_t* encode_base(const kdi_base& b) {
    cbor_item_t* m = cbor_new_indefinite_map();
    map_push(m, "fq_name",          cbor_str(b.fq_name));
    map_push(m, "visibility",       encode_visibility(b.visibility));
    if (b.is_virtual) map_push(m, "is_virtual", cbor_bool(true));
    map_push(m, "base_field_index", cbor_int32(b.base_field_index));
    map_push(m, "byte_offset",      cbor_build_uint64(b.byte_offset));
    return m;
}

kdi_base decode_base(cbor_item_t* item, const std::string& path) {
    kdi_base b;
    b.fq_name          = req_string(item, "fq_name", path);
    b.visibility       = decode_visibility(item, "visibility", path);
    b.is_virtual       = opt_bool(item, "is_virtual");
    b.base_field_index = opt_int32(item, "base_field_index", -1);
    b.byte_offset      = opt_uint(item, "byte_offset", 0);
    return b;
}

// ─────────────────────────────────────────────────────────────────────────────
// Aggregate
// ─────────────────────────────────────────────────────────────────────────────

cbor_item_t* encode_aggregate(const kdi_aggregate& agg);
kdi_aggregate decode_aggregate(cbor_item_t* item, const std::string& path);

cbor_item_t* encode_aggregate(const kdi_aggregate& agg) {
    cbor_item_t* m = cbor_new_indefinite_map();

    // kind
    const char* kind_str = "struct";
    if (agg.kind == kdi_aggregate_kind::class_)     kind_str = "class";
    if (agg.kind == kdi_aggregate_kind::interface_) kind_str = "interface";
    map_push(m, "kind",         cbor_str(kind_str));
    map_push(m, "name",         cbor_str(agg.name));
    map_push(m, "fq_name",      cbor_str(agg.fq_name));
    map_push(m, "mangled_name", cbor_str(agg.mangled_name));
    map_push(m, "visibility",   encode_visibility(agg.visibility));
    if (agg.is_abstract)    map_push(m, "is_abstract",     cbor_bool(true));
    if (agg.is_final)       map_push(m, "is_final",        cbor_bool(true));
    if (agg.is_const_struct)map_push(m, "is_const_struct", cbor_bool(true));
    if (agg.is_static_nested) map_push(m, "is_static_nested", cbor_bool(true));
    if (!agg.enclosing_fq_name.empty())
        map_push(m, "enclosing_fq_name", cbor_str(agg.enclosing_fq_name));

    // bases
    cbor_item_t* bases = cbor_new_indefinite_array();
    for (auto& b : agg.bases) cbor_array_push(bases, cbor_move(encode_base(b)));
    map_push(m, "bases", bases);

    // layout
    cbor_item_t* layout = cbor_new_indefinite_array();
    for (auto& f : agg.layout) cbor_array_push(layout, cbor_move(encode_layout_field(f)));
    map_push(m, "layout", layout);

    // constructors
    cbor_item_t* ctors = cbor_new_indefinite_array();
    for (auto& c : agg.constructors) cbor_array_push(ctors, cbor_move(encode_constructor(c)));
    map_push(m, "constructors", ctors);

    // destructor
    if (agg.destructor) map_push(m, "destructor", encode_destructor(*agg.destructor));

    // methods
    cbor_item_t* methods = cbor_new_indefinite_array();
    for (auto& mth : agg.methods) cbor_array_push(methods, cbor_move(encode_method(mth)));
    map_push(m, "methods", methods);

    // static_vars
    cbor_item_t* svars = cbor_new_indefinite_array();
    for (auto& v : agg.static_vars) cbor_array_push(svars, cbor_move(encode_variable(v)));
    map_push(m, "static_vars", svars);

    // vtable
    if (agg.vtable) map_push(m, "vtable", encode_vtable(*agg.vtable));

    // nested
    cbor_item_t* nested = cbor_new_indefinite_array();
    for (auto& n : agg.nested) cbor_array_push(nested, cbor_move(encode_aggregate(n)));
    map_push(m, "nested", nested);

    // llvm_def
    map_push(m, "llvm_def", cbor_str(agg.llvm_def));

    // default_constructor_mangled_name
    if (!agg.default_constructor_mangled_name.empty())
        map_push(m, "default_constructor_mangled_name", cbor_str(agg.default_constructor_mangled_name));

    return m;
}

kdi_aggregate decode_aggregate(cbor_item_t* item, const std::string& path) {
    kdi_aggregate agg;
    std::string kind_str = opt_string(item, "kind");
    if (kind_str == "class")     agg.kind = kdi_aggregate_kind::class_;
    else if (kind_str == "interface") agg.kind = kdi_aggregate_kind::interface_;
    else                         agg.kind = kdi_aggregate_kind::struct_;

    agg.name         = req_string(item, "name", path);
    agg.fq_name      = req_string(item, "fq_name", path);
    agg.mangled_name = opt_string(item, "mangled_name");
    agg.visibility   = decode_visibility(item, "visibility", path);
    agg.is_abstract  = opt_bool(item, "is_abstract");
    agg.is_final     = opt_bool(item, "is_final");
    agg.is_const_struct = opt_bool(item, "is_const_struct");
    agg.is_static_nested = opt_bool(item, "is_static_nested");
    agg.enclosing_fq_name = opt_string(item, "enclosing_fq_name");

    auto* ba = map_get(item, "bases");
    if (ba && cbor_isa_array(ba)) {
        size_t n = cbor_array_size(ba);
        for (size_t i = 0; i < n; ++i)
            agg.bases.push_back(decode_base(cbor_array_get(ba, i),
                                            path + ".bases[" + std::to_string(i) + "]"));
    }
    auto* la = map_get(item, "layout");
    if (la && cbor_isa_array(la)) {
        size_t n = cbor_array_size(la);
        for (size_t i = 0; i < n; ++i)
            agg.layout.push_back(decode_layout_field(cbor_array_get(la, i),
                                                      path + ".layout[" + std::to_string(i) + "]"));
    }
    auto* ca = map_get(item, "constructors");
    if (ca && cbor_isa_array(ca)) {
        size_t n = cbor_array_size(ca);
        for (size_t i = 0; i < n; ++i)
            agg.constructors.push_back(decode_constructor(cbor_array_get(ca, i),
                                                           path + ".constructors[" + std::to_string(i) + "]"));
    }
    auto* di = map_get(item, "destructor");
    if (di) agg.destructor = decode_destructor(di, path + ".destructor");

    auto* ma = map_get(item, "methods");
    if (ma && cbor_isa_array(ma)) {
        size_t n = cbor_array_size(ma);
        for (size_t i = 0; i < n; ++i)
            agg.methods.push_back(decode_method(cbor_array_get(ma, i),
                                                path + ".methods[" + std::to_string(i) + "]"));
    }
    auto* sva = map_get(item, "static_vars");
    if (sva && cbor_isa_array(sva)) {
        size_t n = cbor_array_size(sva);
        for (size_t i = 0; i < n; ++i)
            agg.static_vars.push_back(decode_variable(cbor_array_get(sva, i),
                                                       path + ".static_vars[" + std::to_string(i) + "]"));
    }
    auto* vti = map_get(item, "vtable");
    if (vti) agg.vtable = decode_vtable(vti, path + ".vtable");

    auto* na = map_get(item, "nested");
    if (na && cbor_isa_array(na)) {
        size_t n = cbor_array_size(na);
        for (size_t i = 0; i < n; ++i)
            agg.nested.push_back(decode_aggregate(cbor_array_get(na, i),
                                                  path + ".nested[" + std::to_string(i) + "]"));
    }
    agg.llvm_def = req_string(item, "llvm_def", path);
    agg.default_constructor_mangled_name = opt_string(item, "default_constructor_mangled_name");
    return agg;
}

// ─────────────────────────────────────────────────────────────────────────────
// Enum
// ─────────────────────────────────────────────────────────────────────────────

cbor_item_t* encode_int64(int64_t v) {
    if (v >= 0) return cbor_build_uint64(static_cast<uint64_t>(v));
    return cbor_build_negint64(static_cast<uint64_t>(-(v + 1)));
}

int64_t read_int64(cbor_item_t* item, const std::string& path) {
    if (!item) throw kdi_parse_error("expected int at " + path);
    if (cbor_isa_uint(item))   return static_cast<int64_t>(cbor_get_uint64(item));
    if (cbor_isa_negint(item)) return -static_cast<int64_t>(cbor_get_uint64(item)) - 1;
    throw kdi_parse_error("expected int at " + path);
}

cbor_item_t* encode_enum(const kdi_enum& e) {
    cbor_item_t* m = cbor_new_indefinite_map();
    map_push(m, "name",            cbor_str(e.name));
    map_push(m, "fq_name",         cbor_str(e.fq_name));
    map_push(m, "visibility",      encode_visibility(e.visibility));
    map_push(m, "underlying_type", encode_type(e.underlying_type));
    if (e.base_fq_name.has_value())
        map_push(m, "base_fq_name", cbor_str(*e.base_fq_name));
    cbor_item_t* entries = cbor_new_indefinite_array();
    for (auto& en : e.entries) {
        cbor_item_t* em = cbor_new_indefinite_map();
        map_push(em, "name",       cbor_str(en.name));
        map_push(em, "value",      encode_int64(en.value));
        if (en.is_default) map_push(em, "is_default", cbor_bool(true));
        cbor_array_push(entries, cbor_move(em));
    }
    map_push(m, "entries", entries);
    return m;
}

kdi_enum decode_enum(cbor_item_t* item, const std::string& path) {
    kdi_enum e;
    e.name       = req_string(item, "name", path);
    e.fq_name    = req_string(item, "fq_name", path);
    e.visibility = decode_visibility(item, "visibility", path);
    auto* ut = map_get(item, "underlying_type");
    if (ut) e.underlying_type = decode_type(ut, path + ".underlying_type");
    auto* bn = map_get(item, "base_fq_name");
    if (bn && cbor_isa_string(bn))
        e.base_fq_name = read_string(bn, path + ".base_fq_name");
    auto* ea = map_get(item, "entries");
    if (ea && cbor_isa_array(ea)) {
        size_t n = cbor_array_size(ea);
        for (size_t i = 0; i < n; ++i) {
            auto* ei = cbor_array_get(ea, i);
            auto ep = path + ".entries[" + std::to_string(i) + "]";
            kdi_enum_entry en;
            en.name       = req_string(ei, "name", ep);
            en.value      = read_int64(map_get(ei, "value"), ep + ".value");
            en.is_default = opt_bool(ei, "is_default");
            e.entries.push_back(en);
        }
    }
    return e;
}

// ─────────────────────────────────────────────────────────────────────────────
// Namespace
// ─────────────────────────────────────────────────────────────────────────────

cbor_item_t* encode_namespace(const kdi_namespace& ns);
kdi_namespace decode_namespace(cbor_item_t* item, const std::string& path);

cbor_item_t* encode_namespace(const kdi_namespace& ns) {
    cbor_item_t* m = cbor_new_indefinite_map();
    map_push(m, "name",    cbor_str(ns.name));
    map_push(m, "fq_name", cbor_str(ns.fq_name));

    cbor_item_t* aggs = cbor_new_indefinite_array();
    for (auto& a : ns.aggregates) cbor_array_push(aggs, cbor_move(encode_aggregate(a)));
    map_push(m, "aggregates", aggs);

    cbor_item_t* enums = cbor_new_indefinite_array();
    for (auto& e : ns.enums) cbor_array_push(enums, cbor_move(encode_enum(e)));
    map_push(m, "enums", enums);

    cbor_item_t* fns = cbor_new_indefinite_array();
    for (auto& f : ns.functions) cbor_array_push(fns, cbor_move(encode_function(f)));
    map_push(m, "functions", fns);

    cbor_item_t* vars = cbor_new_indefinite_array();
    for (auto& v : ns.variables) cbor_array_push(vars, cbor_move(encode_variable(v)));
    map_push(m, "variables", vars);

    cbor_item_t* nss = cbor_new_indefinite_array();
    for (auto& n : ns.namespaces) cbor_array_push(nss, cbor_move(encode_namespace(n)));
    map_push(m, "namespaces", nss);

    return m;
}

kdi_namespace decode_namespace(cbor_item_t* item, const std::string& path) {
    kdi_namespace ns;
    ns.name    = opt_string(item, "name");
    ns.fq_name = opt_string(item, "fq_name");

    auto* aa = map_get(item, "aggregates");
    if (aa && cbor_isa_array(aa)) {
        size_t n = cbor_array_size(aa);
        for (size_t i = 0; i < n; ++i)
            ns.aggregates.push_back(decode_aggregate(cbor_array_get(aa, i),
                                                     path + ".aggregates[" + std::to_string(i) + "]"));
    }
    auto* ea = map_get(item, "enums");
    if (ea && cbor_isa_array(ea)) {
        size_t n = cbor_array_size(ea);
        for (size_t i = 0; i < n; ++i)
            ns.enums.push_back(decode_enum(cbor_array_get(ea, i),
                                           path + ".enums[" + std::to_string(i) + "]"));
    }
    auto* fa = map_get(item, "functions");
    if (fa && cbor_isa_array(fa)) {
        size_t n = cbor_array_size(fa);
        for (size_t i = 0; i < n; ++i)
            ns.functions.push_back(decode_function(cbor_array_get(fa, i),
                                                   path + ".functions[" + std::to_string(i) + "]"));
    }
    auto* va = map_get(item, "variables");
    if (va && cbor_isa_array(va)) {
        size_t n = cbor_array_size(va);
        for (size_t i = 0; i < n; ++i)
            ns.variables.push_back(decode_variable(cbor_array_get(va, i),
                                                   path + ".variables[" + std::to_string(i) + "]"));
    }
    auto* na = map_get(item, "namespaces");
    if (na && cbor_isa_array(na)) {
        size_t n = cbor_array_size(na);
        for (size_t i = 0; i < n; ++i)
            ns.namespaces.push_back(decode_namespace(cbor_array_get(na, i),
                                                     path + ".namespaces[" + std::to_string(i) + "]"));
    }
    return ns;
}

// ─────────────────────────────────────────────────────────────────────────────
// Type table
// ─────────────────────────────────────────────────────────────────────────────

cbor_item_t* encode_type_table(const kdi_type_table& tt) {
    cbor_item_t* m = cbor_new_indefinite_map();
    cbor_item_t* a = cbor_new_indefinite_array();
    for (auto& e : tt.aggregates) {
        cbor_item_t* em = cbor_new_indefinite_map();
        map_push(em, "fq_name",      cbor_str(e.fq_name));
        map_push(em, "mangled_name", cbor_str(e.mangled_name));
        cbor_array_push(a, cbor_move(em));
    }
    map_push(m, "aggregates", a);
    if (!tt.enums.empty()) {
        cbor_item_t* ea = cbor_new_indefinite_array();
        for (auto& e : tt.enums) {
            cbor_item_t* em = cbor_new_indefinite_map();
            map_push(em, "fq_name", cbor_str(e.fq_name));
            cbor_array_push(ea, cbor_move(em));
        }
        map_push(m, "enums", ea);
    }
    return m;
}

kdi_type_table decode_type_table(cbor_item_t* item, const std::string& path) {
    kdi_type_table tt;
    auto* a = map_get(item, "aggregates");
    if (a && cbor_isa_array(a)) {
        size_t n = cbor_array_size(a);
        for (size_t i = 0; i < n; ++i) {
            auto* ei = cbor_array_get(a, i);
            auto ep = path + ".aggregates[" + std::to_string(i) + "]";
            kdi_aggregate_type_entry e;
            e.fq_name      = req_string(ei, "fq_name", ep);
            e.mangled_name = opt_string(ei, "mangled_name");
            tt.aggregates.push_back(e);
        }
    }
    auto* ea = map_get(item, "enums");
    if (ea && cbor_isa_array(ea)) {
        size_t n = cbor_array_size(ea);
        for (size_t i = 0; i < n; ++i) {
            auto* ei = cbor_array_get(ea, i);
            auto ep = path + ".enums[" + std::to_string(i) + "]";
            kdi_enum_type_entry e;
            e.fq_name = req_string(ei, "fq_name", ep);
            tt.enums.push_back(e);
        }
    }
    return tt;
}

// ─────────────────────────────────────────────────────────────────────────────
// Header
// ─────────────────────────────────────────────────────────────────────────────

cbor_item_t* encode_header(const kdi_header& h) {
    cbor_item_t* m = cbor_new_indefinite_map();
    map_push(m, "schema_major",  cbor_build_uint32(h.schema_major));
    map_push(m, "schema_minor",  cbor_build_uint32(h.schema_minor));
    map_push(m, "module_name",   cbor_str(h.module_name));
    map_push(m, "lib_base",      cbor_str(h.lib_base));
    map_push(m, "lib_path",      cbor_str(h.lib_path));
    map_push(m, "target_triple", cbor_str(h.target_triple));
    map_push(m, "compiler_ver",  cbor_str(h.compiler_ver));
    // Encode dependencies as an array of strings (only if non-empty)
    if (!h.dependencies.empty()) {
        cbor_item_t* deps = cbor_new_indefinite_array();
        for (const auto& dep : h.dependencies)
            cbor_array_push(deps, cbor_move(cbor_str(dep)));
        map_push(m, "dependencies", deps);
    }
    return m;
}

kdi_header decode_header(cbor_item_t* item, const std::string& path) {
    kdi_header h;
    h.schema_major  = static_cast<uint32_t>(opt_uint(item, "schema_major", KDI_SCHEMA_MAJOR));
    h.schema_minor  = static_cast<uint32_t>(opt_uint(item, "schema_minor", KDI_SCHEMA_MINOR));
    h.module_name   = req_string(item, "module_name", path);
    h.lib_base      = opt_string(item, "lib_base");
    h.lib_path      = opt_string(item, "lib_path");
    h.target_triple = opt_string(item, "target_triple");
    h.compiler_ver  = opt_string(item, "compiler_ver");
    // Decode optional dependencies array
    if (auto* deps_item = map_get(item, "dependencies")) {
        if (cbor_isa_array(deps_item)) {
            size_t n = cbor_array_size(deps_item);
            h.dependencies.reserve(n);
            for (size_t i = 0; i < n; ++i) {
                auto* si = cbor_array_get(deps_item, i);
                if (si && cbor_isa_string(si)) {
                    h.dependencies.emplace_back(
                        reinterpret_cast<const char*>(cbor_string_handle(si)),
                        cbor_string_length(si));
                }
            }
        }
    }
    return h;
}

// ─────────────────────────────────────────────────────────────────────────────
// Top-level file
// ─────────────────────────────────────────────────────────────────────────────

cbor_item_t* encode_file(const kdi_file& file) {
    cbor_item_t* root = cbor_new_indefinite_map();

    map_push(root, "header", encode_header(file.header));
    map_push(root, "types",  encode_type_table(file.types));

    // unit
    cbor_item_t* unit = cbor_new_indefinite_map();
    map_push(unit, "name",    cbor_str(file.unit.name));
    map_push(unit, "root_ns", encode_namespace(file.unit.root_ns));
    map_push(root, "unit", unit);

    return root;
}

kdi_file decode_file(cbor_item_t* item) {
    if (!item || !cbor_isa_map(item)) {
        throw kdi_parse_error("top-level item must be a CBOR map");
    }
    kdi_file f;

    auto* hi = map_get(item, "header");
    if (!hi) throw kdi_parse_error("missing 'header'");
    f.header = decode_header(hi, "header");

    auto* ti = map_get(item, "types");
    if (ti) f.types = decode_type_table(ti, "types");

    auto* ui = map_get(item, "unit");
    if (!ui) throw kdi_parse_error("missing 'unit'");
    f.unit.name    = opt_string(ui, "name");
    auto* rni = map_get(ui, "root_ns");
    if (!rni) throw kdi_parse_error("missing 'unit.root_ns'");
    f.unit.root_ns = decode_namespace(rni, "unit.root_ns");

    return f;
}

} // anonymous namespace
} // namespace kdi


