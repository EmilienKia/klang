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

#include "template_instantiator.hpp"
#include "context.hpp"
#include "type.hpp"
#include "statements.hpp"
#include "expressions.hpp"
#include "imported.hpp"
#include "aggregate_value.hpp"
#include "../errors.hpp"

#include <sstream>
#include <queue>
#include <cctype>
#include <unordered_map>

namespace k::model {

namespace {

// Guards template_instantiator::instantiate_aggregate() against unbounded
// recursion. Recursion can happen directly (a template instantiates itself
// as a base, e.g. `Node<T> : Node<T*>`) or indirectly through the resolver
// call sites that re-enter instantiate_aggregate while resolving a nested
// member type of the very instantiation being built. Either way, each
// nested call increases this thread-local counter for the lifetime of the
// call; exceeding the limit raises a diagnostic instead of overflowing the
// compiler's own call stack.
constexpr int MAX_TEMPLATE_INSTANTIATION_DEPTH = 256;
thread_local int g_template_instantiation_depth = 0;

struct template_instantiation_depth_guard {
    template_instantiation_depth_guard() { ++g_template_instantiation_depth; }
    ~template_instantiation_depth_guard() { --g_template_instantiation_depth; }
};

} // anonymous namespace

namespace {
constexpr const char* generic_synthesis_key = "<generic_synthesis>";
}

// ═══════════════════════════════════════════════════════════════════════════
// Name / key helpers
// ═══════════════════════════════════════════════════════════════════════════

/** Drop a leading "::" from a fully-qualified name. */
std::string strip_root_prefix(const std::string& fq) {
    if (fq.size() >= 2 && fq[0] == ':' && fq[1] == ':') return fq.substr(2);
    return fq;
}

std::string type_display_name(const std::shared_ptr<type>& t) {
    if (!t) return "?";

    // Leaf user-defined types: prefer the fully-qualified name. `struct_type::to_string()`
    // and `enum_type::to_string()` use the short name, so two same-named types coming from
    // different namespaces would otherwise produce the same instantiation key and the same
    // instantiated aggregate name (a::S and b::S both giving `Box__struct_nS`).
    if (auto st = std::dynamic_pointer_cast<struct_type>(t)) {
        if (auto agg = st->get_struct()) {
            if (auto fq = strip_root_prefix(agg->get_fq_name()); !fq.empty()) return fq;
        }
        return strip_root_prefix(st->name());
    }
    if (auto et = std::dynamic_pointer_cast<enum_type>(t)) {
        if (auto en = et->get_enumeration()) {
            if (auto fq = strip_root_prefix(en->get_fq_name()); !fq.empty()) return fq;
        }
        return t->to_string();
    }

    // Wrapper types: rebuild the display around the (qualified) inner name, mirroring the
    // suffixes produced by the corresponding `to_string()` overloads.
    if (auto inner = t->get_subtype()) {
        if (type::is_const(t))     return "const " + type_display_name(inner);
        if (type::is_pointer(t))   return type_display_name(inner) + "*";
        if (type::is_reference(t)) return type_display_name(inner) + "&";
        if (type::is_link(t))      return type_display_name(inner) + "+";
        if (type::is_view(t))      return type_display_name(inner) + "?";
        if (type::is_owner(t))     return type_display_name(inner) + "!";
        if (type::is_drain(t))     return type_display_name(inner) + "#";
        if (type::is_sized_array(t)) {
            auto sa = std::dynamic_pointer_cast<sized_array_type>(t);
            return type_display_name(inner) + "[" + std::to_string(sa->get_size()) + "]";
        }
        if (type::is_array(t))     return type_display_name(inner) + "[]";
    }

    return t->to_string();
}

/**
 * Build the type name of a nested union cloned into a concrete instantiation.
 *
 * The name must be unique per instantiation: sibling instantiations own distinct
 * `struct_type`s / `llvm::StructType`s whose payload sizes differ. A shared name would
 * make the context's struct registry (keyed by name) and the KDI importer (which
 * deduplicates LLVM type definitions by name) collapse them onto a single layout.
 * Qualifying by the enclosing instantiation's name — itself unique — gives a
 * deterministic name that does not rely on LLVM's `.N` auto-uniquification.
 */
std::string nested_type_name(const aggregate& owner, const std::string& union_name) {
    std::string base = owner.get_fq_name();
    if (base.size() >= 2 && base[0] == ':' && base[1] == ':') {
        base = base.substr(2);
    }
    if (base.empty()) {
        base = owner.get_short_name();
    }
    if (base.empty()) {
        return union_name;
    }
    return base + "::" + union_name;
}

/**
 * Build a cache key from template arguments for instantiation deduplication.
 *
 * This key ensures that different template arguments produce different instantiations
 * and that identical arguments reuse the same cached instantiation.
 *
 * Encoding scheme:
 *   - Type arguments:     type display name (e.g., "int", "MyStruct", "int*")
 *   - Pack arguments:     "{type1,type2,...}" (wrapped in braces to distinguish from individual args)
 *   - Value arguments:
 *     * void / null:      literals "void" / "null"
 *     * bool:             "true" / "false"
 *     * numeric:          decimal representation (e.g., "42", "-3.14")
 *     * string:           quoted representation (e.g., "\"hello\"")
 *     * aggregate:        "AV:" + dump() representation (e.g., "AV:Point{x=1, y=2}")
 *
 * Aggregate value encoding:
 *   The "AV:" prefix ensures aggregate values never collide with other value types.
 *   The dump() representation includes the type name and all fields, making each
 *   distinct aggregate value produce a unique key. Example:
 *     - Point{x=1, y=2} ≠ Point{x=1, y=3}  → different keys
 *     - Point{x=0, y=0} = Point{x=0, y=0}  → same key (deduplication)
 *
 * The entire key is wrapped in "<...>" to visually separate argument boundaries
 * and avoid confusion with nested type syntax.
 */
std::string build_instantiation_key(const std::vector<template_argument>& args) {
    std::ostringstream oss;
    oss << "<";
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) oss << ",";
        if (args[i].is_pack()) {
            // Pack argument: encode as "{type1,type2,...}" to distinguish different pack sizes
            oss << "{";
            for (size_t j = 0; j < args[i].pack_types.size(); ++j) {
                if (j > 0) oss << ",";
                oss << type_display_name(args[i].pack_types[j]);
            }
            oss << "}";
        } else if (args[i].is_type()) {
            oss << type_display_name(args[i].type_arg);
        } else if (args[i].value_arg.has_value()) {
            std::visit([&oss](auto&& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    oss << "void";
                } else if constexpr (std::is_same_v<T, std::nullptr_t>) {
                    oss << "null";
                } else if constexpr (std::is_same_v<T, bool>) {
                    oss << (v ? "true" : "false");
                } else if constexpr (std::is_same_v<T, std::string>) {
                    oss << "\"" << v << "\"";
                } else if constexpr (std::is_same_v<T, std::shared_ptr<k::model::aggregate_value>>) {
                    // Encode aggregate value by its dump representation for cache key uniqueness
                    if (v) {
                        oss << "AV:" << v->dump();
                    } else {
                        oss << "AV:null";
                    }
                } else {
                    oss << v;
                }
            }, *args[i].value_arg);
        } else {
            oss << "?";
        }
    }
    oss << ">";
    return oss.str();
}

/**
 * Escape one component (a type display name, a value, a base name) into an identifier-safe
 * and **injective** form.
 *
 * Every non-alphanumeric character is replaced by a two-character escape `_<letter>`, and
 * `_` itself is escaped as `_u`. Since every escape is `_` followed by a **non-underscore**
 * character, an encoded component can never contain `__`, which lets
 * `build_instantiated_name()` use `__` as an unambiguous argument separator.
 *
 * This is what makes `Box<T*>`, `Box<T&>`, `Box<T!>`, `Box<T+>`, `Box<T?>` and `Box<T#>`
 * distinct instantiations: the previous scheme mapped every non-alphanumeric character to
 * `_`, so all six collapsed onto a single `Box__T_` aggregate sharing one LLVM type.
 */
std::string escape_name_component(const std::string& s) {
    static const std::unordered_map<char, char> escapes = {
        {'_', 'u'}, {'*', 'p'}, {'&', 'r'}, {'!', 'o'}, {'+', 'l'}, {'?', 'v'},
        {'#', 'd'}, {':', 'n'}, {'[', 'a'}, {']', 'e'}, {'<', 't'}, {'>', 'g'},
        {',', 'c'}, {' ', 's'}, {'.', 'f'}, {'-', 'm'}, {'(', 'b'}, {')', 'q'},
        {'"', 'y'}, {'\'', 'j'}, {'=', 'w'}, {'/', 'h'},
    };
    static const char* hex_digits = "0123456789ABCDEF";

    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        if (std::isalnum(c)) {
            out.push_back(static_cast<char>(c));
            continue;
        }
        // '::' gets its own escape so that qualified names stay readable.
        if (c == ':' && i + 1 < s.size() && s[i + 1] == ':') {
            out += "_N";
            ++i;
            continue;
        }
        auto it = escapes.find(static_cast<char>(c));
        if (it != escapes.end()) {
            out.push_back('_');
            out.push_back(it->second);
        } else {
            // Any other byte: '_x' followed by its two hex digits ('x' is never used as a
            // simple escape letter, so this stays unambiguous).
            out.push_back('_');
            out.push_back('x');
            out.push_back(hex_digits[(c >> 4) & 0xF]);
            out.push_back(hex_digits[c & 0xF]);
        }
    }
    return out;
}

std::string build_instantiated_name(const std::string& base_name,
                                      const std::vector<template_argument>& args) {
    // The base name is a plain K identifier and is emitted verbatim so that instantiated
    // names stay readable (`Box__int`, `get_n__42`). The only residual ambiguity is a
    // template whose *own* name contains "__" (e.g. `A__B<x>` vs `A<B, x>`); that case is
    // caught by the duplicate-mangled-name verification instead of miscompiling silently.
    std::ostringstream oss;
    oss << base_name << "__";
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) oss << "__";
        if (args[i].is_pack()) {
            // Pack argument: the leading '_k<count>' distinguishes a single pack argument
            // from the same types passed as separate template arguments.
            oss << "_k" << args[i].pack_types.size();
            for (const auto& pack_type : args[i].pack_types) {
                oss << "_" << escape_name_component(type_display_name(pack_type));
            }
        } else if (args[i].is_type()) {
            oss << escape_name_component(type_display_name(args[i].type_arg));
        } else if (args[i].value_arg.has_value()) {
            // `void`, `null`, `true`, `false` and numeric literals are all reserved words
            // or start with a digit / '-', so none of them can collide with an escaped type
            // name. String values can (`Box<"abc">` vs `Box<abc>`) and therefore keep an
            // explicit `_V` marker.
            std::visit([&oss](auto&& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, std::monostate>) {
                    oss << "void";
                } else if constexpr (std::is_same_v<T, std::nullptr_t>) {
                    oss << "null";
                } else if constexpr (std::is_same_v<T, bool>) {
                    oss << (v ? "true" : "false");
                } else if constexpr (std::is_same_v<T, std::string>) {
                    oss << "_V" << escape_name_component(v);
                } else if constexpr (std::is_same_v<T, char>) {
                    oss << escape_name_component(std::to_string(static_cast<int>(v)));
                } else if constexpr (std::is_same_v<T, std::shared_ptr<k::model::aggregate_value>>) {
                    // Encode aggregate value by its type and dumped representation
                    if (v) {
                        oss << "_A" << escape_name_component(v->dump());
                    } else {
                        oss << "_Anull";
                    }
                } else {
                    oss << escape_name_component(std::to_string(v));
                }
            }, *args[i].value_arg);
        } else {
            oss << "_z";
        }
    }
    return oss.str();
}

tpl_info::generic_usage_descriptor build_generic_usage_descriptor(
    const tpl_info& ti,
    const std::vector<template_argument>& args)
{
    tpl_info::generic_usage_descriptor usage;
    const size_t count = std::min(ti.params.size(), args.size());
    for (size_t i = 0; i < count; ++i) {
        const auto& param = ti.params[i];
        const auto& arg = args[i];
        if (!param.is_type_param() || !arg.is_type() || !arg.type_arg) {
            continue;
        }
        usage.type_bindings[param.name] = arg.type_arg;
    }
    return usage;
}

void record_generic_usage(
    tpl_info& ti,
    const std::vector<template_argument>& args)
{
    if (!ti.is_generic) return;
    const auto key = build_instantiation_key(args);
    ti.generic_usages[key] = build_generic_usage_descriptor(ti, args);
}

// ═══════════════════════════════════════════════════════════════════════════
// Substitute template params in base class raw names
// ═══════════════════════════════════════════════════════════════════════════

namespace {

// Render a resolved template-argument type as the identifier text expected by
// substitute_base_name's textual reassembly (e.g. a struct/enum's short name
// rather than to_string()'s "struct:"/"enum " prefixed form).
std::string render_subst_type_name(const std::shared_ptr<type>& t) {
    // For struct types, use the aggregate's short name (e.g. "Point") rather than
    // to_string() which returns "struct:Point" — the "struct:" prefix would prevent
    // the downstream get_aggregate() lookup from finding the type in the namespace.
    if (auto st = std::dynamic_pointer_cast<struct_type>(t)) {
        if (auto agg = st->get_struct()) {
            return agg->get_short_name();
        }
        // Fallback: strip "struct:" prefix
        const auto ts = st->to_string();
        const std::string prefix = "struct:";
        return (ts.size() > prefix.size() && ts.substr(0, prefix.size()) == prefix)
            ? ts.substr(prefix.size()) : ts;
    }
    // For enum types, use the enumeration's short name (e.g. "Color") rather than
    // to_string() which returns "enum Color" — the "enum " prefix prevents lookup.
    if (auto et = std::dynamic_pointer_cast<enum_type>(t)) {
        if (auto e = et->get_enumeration()) {
            return e->get_short_name();
        }
        // Fallback: strip "enum " prefix
        const auto ts = et->to_string();
        const std::string pfx = "enum ";
        return (ts.size() > pfx.size() && ts.substr(0, pfx.size()) == pfx)
            ? ts.substr(pfx.size()) : ts;
    }
    return t->to_string();
}

/**
 * Split a template-argument list string (the content between the outer '<'
 * and matching '>') by TOP-LEVEL commas, respecting nested angle-bracket
 * depth — so `"Pair<int,int>"` (a single compound argument) is never split
 * at the comma nested inside it. Each piece is trimmed of surrounding
 * whitespace.
 */
std::vector<std::string> split_top_level_args(const std::string& args_str) {
    std::vector<std::string> result;
    size_t pos = 0;
    while (pos < args_str.size()) {
        int depth = 0;
        size_t comma = std::string::npos;
        for (size_t i = pos; i < args_str.size(); ++i) {
            char c = args_str[i];
            if (c == '<') ++depth;
            else if (c == '>') --depth;
            else if (c == ',' && depth == 0) { comma = i; break; }
        }
        std::string arg = (comma == std::string::npos)
            ? args_str.substr(pos)
            : args_str.substr(pos, comma - pos);
        while (!arg.empty() && arg.front() == ' ') arg.erase(arg.begin());
        while (!arg.empty() && arg.back() == ' ') arg.pop_back();
        result.push_back(std::move(arg));
        pos = (comma == std::string::npos) ? args_str.size() : comma + 1;
    }
    return result;
}

} // anonymous namespace

std::shared_ptr<type> template_instantiator::resolve_base_type_name(
    const std::string& name,
    const type_substitution_map& subst,
    const std::shared_ptr<ns>& parent_ns,
    k::model::unit& unit_ref,
    const std::shared_ptr<context>& ctx,
    k::log::logger& logger)
{
    static const std::unordered_map<std::string, primitive_type::PRIMITIVE_TYPE> prim_map = {
        {"bool", primitive_type::BOOL}, {"byte", primitive_type::BYTE},
        {"char", primitive_type::CHAR}, {"short", primitive_type::SHORT},
        {"int", primitive_type::INT}, {"long", primitive_type::LONG},
        {"float", primitive_type::FLOAT}, {"double", primitive_type::DOUBLE},
    };

    auto lt_pos = name.find('<');
    if (lt_pos == std::string::npos) {
        // Plain (non-generic) name.
        // Priority 1: arg_name is itself a template parameter name of the
        // *enclosing* instantiation (e.g. "I"/"O" in TransformInputStream<I, O>).
        // Must be checked before any name-based type lookup below, otherwise
        // a template parameter named "I" can spuriously resolve to an unrelated
        // user-defined type also named "I".
        auto subst_it = subst.find(name);
        if (subst_it != subst.end() && subst_it->second) return subst_it->second;
        auto prim_it = prim_map.find(name);
        if (prim_it != prim_map.end()) return ctx->from_type(prim_it->second);
        // Try as user-defined struct/class/interface type
        if (auto agg = parent_ns->get_aggregate(name)) {
            if (agg->get_struct_type()) return agg->get_struct_type();
        }
        // Try as enum type or other named type via context lookup
        if (auto resolved = ctx->from_string(name); resolved && type::is_resolved(resolved)) return resolved;
        // Search every module actually imported by this compilation unit (mirrors
        // the fallback used for simple/non-generic base names just below in the
        // caller, and scope_lookup::lookup_structure_or_import's import fallback).
        // Without this, a generic base template argument that names an imported
        // class type (e.g. the "String" in "Box<String>", imported via "import k;")
        // never resolves here — parent_ns->get_aggregate() only looks at the
        // template's own enclosing namespace, not at namespaces reached only via
        // import — silently leaving bs.base unset for that generic base and
        // breaking is_derived_from()/upcast checks against it.
        for (const auto& imp_mod : unit_ref.get_imports()) {
            if (imp_mod.module_name.empty()) continue;
            if (auto imp = unit_ref.get_or_create_imported_aggregate(
                    imp_mod.module_name.with_back(name), ctx)) {
                if (auto imp_agg = std::static_pointer_cast<aggregate>(imp); imp_agg->get_struct_type()) {
                    return imp_agg->get_struct_type();
                }
            }
        }
        // Final fallback: reverse-map arg_name to the original type object via
        // the substitution map. substitute_base_name uses type->to_string() for
        // non-struct/non-enum types (e.g. "Object*" for pointer_type, "Object!"
        // for owner_type), so matching name against each substituted value's
        // to_string() recovers the correct type object.
        for (const auto& [_, sv] : subst) {
            if (sv && sv->to_string() == name) return sv;
        }
        return nullptr;
    }

    // Generic name: "Base<Arg1,Arg2,...>" — recurse into each top-level argument.
    auto gt_pos = name.rfind('>');
    if (gt_pos == std::string::npos || gt_pos <= lt_pos) return nullptr;
    std::string tpl_base_name = name.substr(0, lt_pos);
    std::string args_str = name.substr(lt_pos + 1, gt_pos - lt_pos - 1);

    auto tpl_base_agg = parent_ns->get_aggregate(tpl_base_name);
    if (!tpl_base_agg || !tpl_base_agg->is_template()) return nullptr;
    auto* base_ti = tpl_base_agg->get_tpl_info();
    if (!base_ti) return nullptr;

    std::vector<template_argument> base_args;
    for (const auto& arg_name : split_top_level_args(args_str)) {
        auto arg_type = resolve_base_type_name(arg_name, subst, parent_ns, unit_ref, ctx, logger);
        if (!arg_type || !type::is_resolved(arg_type)) return nullptr;
        base_args.push_back(template_argument::make_type(arg_type));
    }
    if (base_args.empty()) return nullptr;

    auto base_agg = template_instantiator::instantiate_aggregate(
        *tpl_base_agg, base_args, parent_ns, unit_ref, ctx, logger);
    if (!base_agg) return nullptr;

    // Ensure the instantiated base has a struct_type that is UNIFIED through the
    // unit instantiation registry, so it shares a single struct_type with any
    // KDI-imported instantiation of the same template (e.g. an imported class
    // deriving from the same base). Without this, struct_type identity
    // comparisons (is_derived_from, pointer upcast) fail across the import
    // boundary because two distinct struct_types would denote the same
    // concrete instantiation.
    if (!base_agg->get_struct_type()) {
        std::string base_origin =
            !base_ti->origin_module_ns_fq.empty()
            ? base_ti->origin_module_ns_fq
            : (parent_ns ? parent_ns->get_fq_name() : std::string{});
        const std::string base_key =
            k::model::unit::make_instantiation_registry_key(base_origin, base_agg->get_short_name());
        auto st = unit_ref.find_instantiation_struct_type(base_key);
        if (st) {
            st->reassign_aggregate(base_agg->shared_as<aggregate>());
        } else {
            st = std::make_shared<struct_type>(base_agg->get_short_name(), base_agg->shared_as<aggregate>());
            ctx->add_struct(st);
            unit_ref.register_instantiation_struct_type(base_key, st);
        }
        base_agg->set_struct_type(st);
        // Mirror the "normal" instantiation path (resolvers_aggregate.cpp /
        // resolvers_type_ref.cpp), which re-triggers update_mangled_name()
        // right after set_struct_type(): instantiate_aggregate() computes
        // the mangled name BEFORE a struct_type exists (see its own comment,
        // "struct_type may not be set yet"), so it can come out empty. Since
        // this recursive base-resolution path may be the ONLY place that ever
        // sets base_agg's struct_type (e.g. an interface reached solely as a
        // grand-base, like Entry<K,V> under MutableEntry<K,V> under
        // MapEntry<K,V>), we must (re-)assign its FQ name and mangled name
        // here too (ensure_agg_names_assigned handles both, plus its
        // constructor/method children), or it keeps an empty mangled name
        // forever.
        ensure_agg_names_assigned(base_agg);
    }
    return base_agg->get_struct_type();
}

/**
 * Given a base class raw_name that may contain template arguments
 * (e.g. "Collection<T>"), substitute type parameter names using the
 * substitution map (e.g. T→int → "Collection<int>").
 *
 * Top-level template arguments are split respecting nested angle-bracket
 * depth (so `Iter<Pair<K,V>>`'s single top-level argument is recognised as
 * `Pair<K,V>`, not incorrectly split at the comma inside it). A top-level
 * argument that is itself a compound generic (contains '<') is substituted
 * by recursing into this same function, so that placeholders nested inside
 * it (e.g. the 'K'/'V' inside `Pair<K,V>`) are substituted too — a bare
 * top-level exact-match lookup would never fire for a compound argument.
 */
std::string substitute_base_name(const std::string& raw_name,
                                  const type_substitution_map& subst) {
    // Find '<' — if absent, no substitution needed
    auto lt_pos = raw_name.find('<');
    if (lt_pos == std::string::npos) return raw_name;
    auto gt_pos = raw_name.rfind('>');
    if (gt_pos == std::string::npos || gt_pos <= lt_pos) return raw_name;

    std::string prefix = raw_name.substr(0, lt_pos + 1); // "Collection<"
    std::string args_str = raw_name.substr(lt_pos + 1, gt_pos - lt_pos - 1); // "T" or "T,U" or "Pair<K,V>"

    // Split by top-level ',' (respecting nested '<'/'>' depth) and substitute each arg
    std::string result = prefix;
    size_t pos = 0;
    bool first = true;
    while (pos < args_str.size()) {
        if (!first) result += ",";
        first = false;
        // Find the next top-level comma: track bracket depth so a comma nested
        // inside a compound argument (e.g. the one inside "Pair<K,V>") is skipped.
        int depth = 0;
        size_t comma = std::string::npos;
        for (size_t i = pos; i < args_str.size(); ++i) {
            char c = args_str[i];
            if (c == '<') ++depth;
            else if (c == '>') --depth;
            else if (c == ',' && depth == 0) { comma = i; break; }
        }
        std::string arg = (comma == std::string::npos)
            ? args_str.substr(pos)
            : args_str.substr(pos, comma - pos);
        // Trim whitespace
        while (!arg.empty() && arg.front() == ' ') arg.erase(arg.begin());
        while (!arg.empty() && arg.back() == ' ') arg.pop_back();

        if (arg.find('<') != std::string::npos) {
            // Compound generic argument (e.g. "Pair<K,V>"): recurse so any
            // placeholder nested inside it is substituted too. A bare exact-match
            // lookup below would never match a compound string against a param name.
            result += substitute_base_name(arg, subst);
        } else {
            // Check if this arg name is a template parameter to substitute
            auto it = subst.find(arg);
            if (it != subst.end() && it->second) {
                result += render_subst_type_name(it->second);
            } else {
                result += arg;
            }
        }
        pos = (comma == std::string::npos) ? args_str.size() : comma + 1;
    }
    result += ">";
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// Build substitution map
// ═══════════════════════════════════════════════════════════════════════════

type_substitution_map template_instantiator::build_substitution_map(
    const tpl_info& ti,
    const std::vector<template_argument>& args)
{
    type_substitution_map result;
    size_t arg_idx = 0;
    for (size_t i = 0; i < ti.params.size() && arg_idx < args.size(); ++i) {
        if (ti.params[i].is_pack) {
            // Skip pack params — handled by build_pack_substitution_map.
            // Consume the args that belong to this pack.
            size_t remaining_params_after = 0;
            for (size_t j = i + 1; j < ti.params.size(); ++j) {
                if (!ti.params[j].is_pack) remaining_params_after++;
            }
            size_t remaining_args = args.size() - arg_idx;
            size_t pack_count = remaining_args > remaining_params_after ? remaining_args - remaining_params_after : 0;
            arg_idx += pack_count;
        } else if (args[arg_idx].is_type() && args[arg_idx].type_arg) {
            result[ti.params[i].name] = args[arg_idx].type_arg;
            arg_idx++;
        } else {
            arg_idx++;
        }
    }
    return result;
}

value_substitution_map template_instantiator::build_value_substitution_map(
    const tpl_info& ti,
    const std::vector<template_argument>& args)
{
    value_substitution_map result;
    size_t count = std::min(ti.params.size(), args.size());
    for (size_t i = 0; i < count; ++i) {
        if (args[i].is_value() && args[i].value_arg.has_value()) {
            std::shared_ptr<type> declared_type = ti.params[i].value_type;
            if (auto agg_val = std::get_if<std::shared_ptr<k::model::aggregate_value>>(&*args[i].value_arg)) {
                if (*agg_val && (*agg_val)->get_type() && (*agg_val)->get_type()->get_struct_type()) {
                    declared_type = (*agg_val)->get_type()->get_struct_type();
                }
            } else if (declared_type && !type::is_resolved(declared_type)) {
                // Keep unresolved declared types out of value substitutions.
                // Scalar arguments can be inferred safely from the concrete value,
                // while aggregate arguments are explicitly retyped above.
                declared_type.reset();
            }
            result[ti.params[i].name] = value_param_binding{*args[i].value_arg, declared_type};
        }
    }
    return result;
}

pack_substitution_map template_instantiator::build_pack_substitution_map(
    const tpl_info& ti,
    const std::vector<template_argument>& args)
{
    pack_substitution_map result;
    size_t arg_idx = 0;
    for (size_t i = 0; i < ti.params.size(); ++i) {
        if (ti.params[i].is_pack) {
            std::vector<std::shared_ptr<type>> pack_types;
            if (arg_idx < args.size() && args[arg_idx].is_pack()) {
                // Already packed into a single argument
                pack_types = args[arg_idx].pack_types;
                arg_idx++;
            } else {
                // Consume individual type args until we run out or hit the next non-pack param
                size_t remaining_params_after = 0;
                for (size_t j = i + 1; j < ti.params.size(); ++j) {
                    if (!ti.params[j].is_pack) remaining_params_after++;
                }
                size_t remaining_args = args.size() - arg_idx;
                size_t pack_count = remaining_args > remaining_params_after ? remaining_args - remaining_params_after : 0;
                for (size_t j = 0; j < pack_count && arg_idx < args.size(); ++j) {
                    if (args[arg_idx].is_type() && args[arg_idx].type_arg) {
                        pack_types.push_back(args[arg_idx].type_arg);
                    }
                    arg_idx++;
                }
            }
            result[ti.params[i].name] = std::move(pack_types);
        } else {
            if (arg_idx < args.size()) arg_idx++;
        }
    }
    return result;
}

static type_substitution_map build_generic_substitution_map(
    const tpl_info& ti,
    const std::shared_ptr<context>& ctx)
{
    type_substitution_map result;
    if (!ctx) return result;

    auto byte_type = ctx->from_type(primitive_type::BYTE);
    if (!byte_type) return result;

    // Generic synthesis uses a uniform opaque pointer model type (i8*).
    auto opaque_ptr_type = byte_type->get_pointer();
    for (const auto& param : ti.params) {
        if (param.is_type_param() && !param.name.empty()) {
            result[param.name] = opaque_ptr_type;
        }
    }
    return result;
}

// ═══════════════════════════════════════════════════════════════════════════
// Expression type substitution
// ═══════════════════════════════════════════════════════════════════════════

void template_instantiator::substitute_expr_types(
    std::shared_ptr<expression> expr,
    const type_substitution_map& subst)
{
    if (!expr) return;

    // Substitute the expression's own type
    if (expr->get_type()) {
        auto new_type = substitute_type(expr->get_type(), subst);
        if (new_type != expr->get_type()) {
            expr->set_type(new_type);
        }
    }

    // Substitute type in cast expressions
    if (auto ce = std::dynamic_pointer_cast<cast_expression>(expr)) {
        if (ce->get_cast_type()) {
            auto new_cast = substitute_type(ce->get_cast_type(), subst);
            if (new_cast != ce->get_cast_type()) {
                ce->set_cast_type(new_cast);
            }
        }
    }

    // Recurse into sub-expressions via the expression hierarchy
    if (auto ue = std::dynamic_pointer_cast<unary_expression>(expr)) {
        substitute_expr_types(std::const_pointer_cast<expression>(ue->sub_expr()), subst);
    } else if (auto be = std::dynamic_pointer_cast<binary_expression>(expr)) {
        substitute_expr_types(be->left(), subst);
        substitute_expr_types(be->right(), subst);
    } else if (auto fie = std::dynamic_pointer_cast<function_invocation_expression>(expr)) {
        substitute_expr_types(std::const_pointer_cast<expression>(fie->callee_expr()), subst);
        for (auto& arg : fie->arguments()) {
            substitute_expr_types(std::const_pointer_cast<expression>(arg), subst);
        }
    } else if (auto cie = std::dynamic_pointer_cast<constructor_invocation_expression>(expr)) {
        substitute_expr_types(std::static_pointer_cast<expression>(cie->constructed_symbol()), subst);
        for (auto& arg : cie->arguments()) {
            substitute_expr_types(std::const_pointer_cast<expression>(arg), subst);
        }
    } else if (auto aie = std::dynamic_pointer_cast<array_init_expression>(expr)) {
        for (auto& elem : aie->elements()) {
            substitute_expr_types(std::const_pointer_cast<expression>(elem), subst);
        }
    } else if (auto dsie = std::dynamic_pointer_cast<designated_struct_init_expression>(expr)) {
        for (auto& mi : dsie->members_mutable()) {
            if (mi.value) substitute_expr_types(mi.value, subst);
            for (auto& a : mi.args) substitute_expr_types(a, subst);
        }
    } else if (auto tce = std::dynamic_pointer_cast<temporary_construction_expression>(expr)) {
        for (auto& arg : tce->arguments()) {
            substitute_expr_types(std::const_pointer_cast<expression>(arg), subst);
        }
    } else if (auto ne = std::dynamic_pointer_cast<new_expression>(expr)) {
        if (ne->allocated_type()) {
            auto new_alloc_type = substitute_type(ne->allocated_type(), subst);
            if (new_alloc_type != ne->allocated_type()) {
                ne->allocated_type(new_alloc_type);
            }
        }
        for (auto& arg : ne->arguments()) {
            substitute_expr_types(std::const_pointer_cast<expression>(arg), subst);
        }
        substitute_expr_types(std::const_pointer_cast<expression>(ne->array_size_expr()), subst);
    } else if (auto cbe = std::dynamic_pointer_cast<callable_bind_expression>(expr)) {
        substitute_expr_types(std::const_pointer_cast<expression>(cbe->get_context()), subst);
    } else if (auto cive = std::dynamic_pointer_cast<callable_invocation_expression>(expr)) {
        substitute_expr_types(std::const_pointer_cast<expression>(cive->get_callee()), subst);
        for (auto& arg : cive->arguments()) {
            substitute_expr_types(std::const_pointer_cast<expression>(arg), subst);
        }
    } else if (auto de = std::dynamic_pointer_cast<delete_expression>(expr)) {
        substitute_expr_types(std::const_pointer_cast<expression>(de->sub_expr()), subst);
    }
}

std::shared_ptr<expression> template_instantiator::clone_and_substitute_expr(
    const std::shared_ptr<expression>& src,
    const type_substitution_map& subst,
    const value_substitution_map& val_subst)
{
    if (!src) return nullptr;

    auto cloned = src->clone();
    substitute_expr_types(cloned, subst);
    if (!val_subst.empty()) {
        substitute_value_params(cloned, val_subst);
    }
    return cloned;
}

void template_instantiator::substitute_value_params(
    std::shared_ptr<expression>& expr,
    const value_substitution_map& val_subst)
{
    if (!expr || val_subst.empty()) return;

    auto make_value_expr = [](const k::value_type& value,
                              const std::shared_ptr<type>& declared_type = nullptr) -> std::shared_ptr<expression> {
        auto out = std::visit([](auto&& v) -> std::shared_ptr<expression> {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                return value_expression::from_value<int>(0);
            } else if constexpr (std::is_same_v<T, std::nullptr_t>) {
                return value_expression::from_value<int>(0);
            } else if constexpr (std::is_same_v<T, std::string>) {
                return value_expression::from_value(v);
            } else {
                return value_expression::from_value<T>(v);
            }
        }, value);

        if (declared_type) {
            out->set_type(declared_type);
            return out;
        }

        // Keep aggregate value carriers typed so chained substitutions
        // (e.g. P.inner.x) can be recognized before regular type resolution.
        if (auto agg_val = std::get_if<std::shared_ptr<k::model::aggregate_value>>(&value)) {
            if (*agg_val && (*agg_val)->get_type() && (*agg_val)->get_type()->get_struct_type()) {
                out->set_type((*agg_val)->get_type()->get_struct_type());
            }
        }
        return out;
    };

    // Check if this expression itself is a symbol matching a value parameter
    if (auto sym = std::dynamic_pointer_cast<symbol_expression>(expr)) {
        if (!sym->is_resolved()) {
            const auto& nm = sym->get_name();
            if (nm.size() == 1 && !nm.has_root_prefix()) {
                auto it = val_subst.find(nm.front());
                if (it != val_subst.end()) {
                    const auto& binding = it->second;
                    expr = make_value_expr(binding.value, binding.declared_type);
                    return;
                }
            }
        }
    }

    // Recurse into sub-expressions
    if (auto be = std::dynamic_pointer_cast<binary_expression>(expr)) {
        auto l = be->left();
        auto r = be->right();
        substitute_value_params(l, val_subst);
        substitute_value_params(r, val_subst);
        if (l != be->left()) be->assign_left(l);
        if (r != be->right()) be->assign_right(r);
    } else if (auto mem_obj = std::dynamic_pointer_cast<member_of_object_expression>(expr)) {
        auto sub = std::const_pointer_cast<expression>(mem_obj->sub_expr());
        substitute_value_params(sub, val_subst);
        if (sub != mem_obj->sub_expr()) {
            mem_obj->sub_expr() = sub;
            if (sub) sub->set_parent_expression(mem_obj);
        }

        auto sub_val = std::dynamic_pointer_cast<value_expression>(sub);
        if (!sub_val) return;

        auto agg = std::get_if<std::shared_ptr<k::model::aggregate_value>>(&sub_val->get_value());
        if (!agg || !*agg) return;

        const auto& sym_name = mem_obj->symbol().get_name();
        const std::string member_name = sym_name.size() > 1 ? sym_name.back() : sym_name.to_string();
        auto field_value = (*agg)->get_field(member_name);
        if (!field_value.has_value()) return;

        std::shared_ptr<type> field_declared_type;
        if (auto agg_type = (*agg)->get_type()) {
            if (auto field = agg_type->get_variable(member_name)) {
                field_declared_type = field->get_type();
            }
        }

        expr = make_value_expr(*field_value, field_declared_type);
        return;
    } else if (auto mem_ptr = std::dynamic_pointer_cast<member_of_pointer_expression>(expr)) {
        auto sub = std::const_pointer_cast<expression>(mem_ptr->sub_expr());
        substitute_value_params(sub, val_subst);
        if (sub != mem_ptr->sub_expr()) {
            mem_ptr->sub_expr() = sub;
            if (sub) sub->set_parent_expression(mem_ptr);
        }
    } else if (auto ue = std::dynamic_pointer_cast<unary_expression>(expr)) {
        auto s = std::const_pointer_cast<expression>(ue->sub_expr());
        substitute_value_params(s, val_subst);
        if (s != ue->sub_expr()) ue->assign(s);
    } else if (auto fie = std::dynamic_pointer_cast<function_invocation_expression>(expr)) {
        auto callee_expr = std::const_pointer_cast<expression>(fie->callee_expr());
        substitute_value_params(callee_expr, val_subst);
        if (callee_expr != fie->callee_expr()) {
            std::vector<std::shared_ptr<expression>> args;
            for (const auto& a : fie->arguments()) {
                args.push_back(std::const_pointer_cast<expression>(a));
            }
            fie->assign(callee_expr, args);
        }
        for (size_t i = 0; i < fie->arguments().size(); ++i) {
            auto arg = std::const_pointer_cast<expression>(fie->arguments()[i]);
            substitute_value_params(arg, val_subst);
            if (arg != fie->arguments()[i]) fie->assign_argument(i, arg);
        }
    } else if (auto cie = std::dynamic_pointer_cast<constructor_invocation_expression>(expr)) {
        for (size_t i = 0; i < cie->arguments().size(); ++i) {
            auto arg = std::const_pointer_cast<expression>(cie->arguments()[i]);
            substitute_value_params(arg, val_subst);
            if (arg != cie->arguments()[i]) cie->assign_argument(i, arg);
        }
    } else if (auto ce = std::dynamic_pointer_cast<cast_expression>(expr)) {
        auto s = std::const_pointer_cast<expression>(ce->sub_expr());
        substitute_value_params(s, val_subst);
        if (s != ce->sub_expr()) ce->assign(s);
    } else if (auto aie = std::dynamic_pointer_cast<array_init_expression>(expr)) {
        for (size_t i = 0; i < aie->elements().size(); ++i) {
            auto arg = std::const_pointer_cast<expression>(aie->elements()[i]);
            substitute_value_params(arg, val_subst);
            if (arg != aie->elements()[i]) aie->assign_element(i, arg);
        }
    } else if (auto dsie = std::dynamic_pointer_cast<designated_struct_init_expression>(expr)) {
        auto& members = dsie->members_mutable();
        for (auto& member : members) {
            if (member.value) {
                substitute_value_params(member.value, val_subst);
            }
            for (auto& arg : member.args) {
                substitute_value_params(arg, val_subst);
            }
        }
    } else if (auto tce = std::dynamic_pointer_cast<temporary_construction_expression>(expr)) {
        for (size_t i = 0; i < tce->arguments().size(); ++i) {
            auto arg = std::const_pointer_cast<expression>(tce->arguments()[i]);
            substitute_value_params(arg, val_subst);
            if (arg != tce->arguments()[i]) tce->assign_argument(i, arg);
        }
    } else if (auto ne = std::dynamic_pointer_cast<new_expression>(expr)) {
        for (size_t i = 0; i < ne->arguments().size(); ++i) {
            auto arg = std::const_pointer_cast<expression>(ne->arguments()[i]);
            substitute_value_params(arg, val_subst);
            if (arg != ne->arguments()[i]) ne->assign_argument(i, arg);
        }
        auto array_size_expr = std::const_pointer_cast<expression>(ne->array_size_expr());
        substitute_value_params(array_size_expr, val_subst);
    } else if (auto de = std::dynamic_pointer_cast<delete_expression>(expr)) {
        auto sub = std::const_pointer_cast<expression>(de->sub_expr());
        substitute_value_params(sub, val_subst);
        if (sub != de->sub_expr()) de->assign(sub);
    }
}

void template_instantiator::retarget_init_expr(
    const std::shared_ptr<expression>& init_expr,
    const std::shared_ptr<variable_definition>& new_var)
{
    if (!init_expr || !new_var) return;

    // constructor_invocation_expression
    if (auto cie = std::dynamic_pointer_cast<constructor_invocation_expression>(init_expr)) {
        if (cie->constructed_symbol()) {
            cie->constructed_symbol()->set_target(new_var);
        }
        return;
    }

    // designated_struct_init_expression
    if (auto dsie = std::dynamic_pointer_cast<designated_struct_init_expression>(init_expr)) {
        auto sym = dsie->constructed_symbol();
        if (sym) {
            sym->set_target(new_var);
        }
        return;
    }

    // array_init_expression
    if (auto aie = std::dynamic_pointer_cast<array_init_expression>(init_expr)) {
        auto sym = aie->constructed_symbol();
        if (sym) {
            sym->set_target(new_var);
        }
        return;
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Statement cloning with type substitution
// ═══════════════════════════════════════════════════════════════════════════

std::shared_ptr<statement> template_instantiator::clone_statement(
    const statement& src,
    std::shared_ptr<statement> parent_stmt,
    const type_substitution_map& subst,
    const value_substitution_map& val_subst)
{
    // Return statement
    if (auto rs = dynamic_cast<const return_statement*>(&src)) {
        auto new_rs = std::make_shared<return_statement>(parent_stmt);
        new_rs->_ast_node = rs->get_ast_node(); // optional, for diagnostics
        new_rs->_documentation = rs->get_documentation();
        if (rs->get_expression()) {
            new_rs->set_expression(clone_and_substitute_expr(
                std::const_pointer_cast<expression>(rs->get_expression()), subst, val_subst));
        }
        return new_rs;
    }

    // Break statement
    if (auto bs = dynamic_cast<const break_statement*>(&src)) {
        auto new_bs = std::make_shared<break_statement>(parent_stmt);
        new_bs->_ast_node = bs->get_ast_node();
        new_bs->_documentation = bs->get_documentation();
        return new_bs;
    }

    // Continue statement
    if (auto cs = dynamic_cast<const continue_statement*>(&src)) {
        auto new_cs = std::make_shared<continue_statement>(parent_stmt);
        new_cs->_ast_node = cs->get_ast_node();
        new_cs->_documentation = cs->get_documentation();
        return new_cs;
    }

    // If-else statement
    if (auto ies = dynamic_cast<const if_else_statement*>(&src)) {
        auto new_ies = std::make_shared<if_else_statement>(parent_stmt);
        new_ies->_ast_node = ies->get_ast_node();
        new_ies->_documentation = ies->get_documentation();
        if (ies->has_cond_var()) {
            // Clone the condition variable by cloning it as a statement
            auto cloned_var = clone_statement(*ies->get_cond_var(), new_ies, subst, val_subst);
            // The variable_holder mechanism should have registered it via on_variable_defined
        }
        if (ies->get_test_expr()) {
            new_ies->set_test_expr(clone_and_substitute_expr(
                std::const_pointer_cast<expression>(ies->get_test_expr()), subst, val_subst));
        }
        if (ies->get_then_stmt()) {
            new_ies->set_then_stmt(clone_statement(*ies->get_then_stmt(), new_ies, subst, val_subst));
        }
        if (ies->get_else_stmt()) {
            new_ies->set_else_stmt(clone_statement(*ies->get_else_stmt(), new_ies, subst, val_subst));
        }
        return new_ies;
    }

    // While statement
    if (auto ws = dynamic_cast<const while_statement*>(&src)) {
        auto new_ws = std::make_shared<while_statement>(parent_stmt);
        new_ws->_ast_node = ws->get_ast_node();
        new_ws->_documentation = ws->get_documentation();
        if (ws->get_test_expr()) {
            new_ws->set_test_expr(clone_and_substitute_expr(
                std::const_pointer_cast<expression>(ws->get_test_expr()), subst, val_subst));
        }
        if (ws->get_nested_stmt()) {
            new_ws->set_nested_stmt(clone_statement(*ws->get_nested_stmt(), new_ws, subst, val_subst));
        }
        return new_ws;
    }

    // Expression statement
    if (auto es = dynamic_cast<const expression_statement*>(&src)) {
        auto new_es = std::make_shared<expression_statement>(parent_stmt);
        new_es->_ast_node = es->get_ast_node();
        new_es->_documentation = es->get_documentation();
        if (es->get_expression()) {
            new_es->set_expression(clone_and_substitute_expr(
                std::const_pointer_cast<expression>(es->get_expression()), subst, val_subst));
        }
        return new_es;
    }

    // Variable statement
    if (auto vs = dynamic_cast<const variable_statement*>(&src)) {
        auto new_vs = variable_statement::make_shared(parent_stmt, vs->get_short_name());
        new_vs->_ast_node = vs->get_ast_node();
        new_vs->_documentation = vs->get_documentation();
        new_vs->set_type(substitute_type(std::const_pointer_cast<type>(vs->get_type()), subst));
        new_vs->set_const(vs->is_const());
        if (vs->get_init_expr()) {
            // variable_definition::set_init_expr(shared_ptr<expression>) is the base version
            auto cloned_init = clone_and_substitute_expr(
                    std::const_pointer_cast<expression>(vs->get_init_expr()), subst, val_subst);
            // If the init expression is a constructor_invocation_expression, retarget its
            // constructed_symbol to the new cloned variable.  Without this, the CIE would
            // still reference the original template's variable_statement, which was never
            // registered in the LLVM codegen context.
            if (auto cie = std::dynamic_pointer_cast<constructor_invocation_expression>(cloned_init)) {
                auto new_sym = symbol_expression::from_variable(
                    std::static_pointer_cast<variable_definition>(new_vs));
                new_sym->set_parent_expression(cie);
                cie->constructed_symbol(new_sym);
            }
            static_cast<variable_definition*>(new_vs.get())->set_init_expr(cloned_init);
        }
        // Register in the block's variable holder if parent is a block
        if (auto blk = std::dynamic_pointer_cast<block>(parent_stmt)) {
            // The variable is already created with the parent; it needs to be
            // registered in the block's variable map. append_variable would
            // create a new one, so we manually register.
            blk->_vars[vs->get_short_name()] = new_vs;
        }
        return new_vs;
    }

    // Block (nested)
    if (auto blk = dynamic_cast<const block*>(&src)) {
        auto new_blk = std::make_shared<block>(parent_stmt);
        new_blk->_ast_node = blk->get_ast_node();
        new_blk->_documentation = blk->get_documentation();
        clone_block_contents(*blk, new_blk, subst, val_subst);
        return new_blk;
    }

    // For statement
    if (auto fs = dynamic_cast<const for_statement*>(&src)) {
        auto new_fs = std::make_shared<for_statement>(parent_stmt);
        new_fs->_ast_node = fs->get_ast_node();
        new_fs->_documentation = fs->get_documentation();
        if (fs->get_decl_stmt()) {
            auto cloned_decl = std::dynamic_pointer_cast<variable_statement>(
                clone_statement(*fs->get_decl_stmt(), new_fs, subst, val_subst));
            new_fs->set_decl_stmt(cloned_decl);
        }
        if (fs->get_test_expr()) {
            new_fs->set_test_expr(clone_and_substitute_expr(
                std::const_pointer_cast<expression>(fs->get_test_expr()), subst, val_subst));
        }
        if (fs->get_step_expr()) {
            new_fs->set_step_expr(clone_and_substitute_expr(
                std::const_pointer_cast<expression>(fs->get_step_expr()), subst, val_subst));
        }
        if (fs->get_nested_stmt()) {
            new_fs->set_nested_stmt(clone_statement(*fs->get_nested_stmt(), new_fs, subst, val_subst));
        }
        return new_fs;
    }

    // Throw statement
    if (auto ts = dynamic_cast<const throw_statement*>(&src)) {
        auto new_ts = std::make_shared<throw_statement>(parent_stmt);
        new_ts->_ast_node = ts->get_ast_node();
        new_ts->_documentation = ts->get_documentation();
        if (ts->get_expression()) {
            new_ts->set_expression(clone_and_substitute_expr(
                std::const_pointer_cast<expression>(ts->get_expression()), subst, val_subst));
        }
        return new_ts;
    }

    // Try-catch statement
    if (auto tcs = dynamic_cast<const try_catch_statement*>(&src)) {
        auto new_tcs = std::make_shared<try_catch_statement>(parent_stmt);
        new_tcs->_ast_node = tcs->get_ast_node();
        new_tcs->_documentation = tcs->get_documentation();

        auto clone_body = [&](const std::shared_ptr<const block>& src_body,
                              const std::shared_ptr<statement>& owner)
                -> std::shared_ptr<block> {
            if (!src_body) return {};
            auto new_body = std::make_shared<block>(owner);
            new_body->_ast_node = src_body->get_ast_node();
            new_body->_documentation = src_body->get_documentation();
            clone_block_contents(*src_body, new_body, subst, val_subst);
            return new_body;
        };

        new_tcs->set_try_body(clone_body(tcs->get_try_body(), new_tcs));

        for (const auto& cc : tcs->get_catch_clauses()) {
            if (!cc) continue;
            auto new_cc = std::make_shared<catch_clause>(
                std::static_pointer_cast<statement>(new_tcs));
            new_cc->_ast_node = cc->get_ast_node();
            new_cc->_documentation = cc->get_documentation();
            new_cc->set_const(cc->is_const());

            // The caught-exception variable belongs to the catch clause's own
            // variable holder, not to the enclosing block, so it is cloned here
            // and registered directly instead of going through clone_statement().
            if (auto ev = cc->get_exception_var()) {
                auto new_ev = variable_statement::make_shared(
                    std::static_pointer_cast<statement>(new_cc), ev->get_short_name());
                new_ev->_ast_node = ev->get_ast_node();
                new_ev->_documentation = ev->get_documentation();
                new_ev->set_type(substitute_type(
                    std::const_pointer_cast<type>(ev->get_type()), subst));
                new_ev->set_const(ev->is_const());
                new_cc->set_exception_var(new_ev);
                new_cc->_vars[ev->get_short_name()] = new_ev;
            }

            new_cc->set_body(clone_body(cc->get_body(), new_cc));
            new_tcs->add_catch_clause(new_cc);
        }

        new_tcs->set_finally_body(clone_body(tcs->get_finally_body(), new_tcs));
        return new_tcs;
    }

    // Fallback: unknown statement type — return empty
    return nullptr;
}

void template_instantiator::clone_block_contents(
    const block& src,
    std::shared_ptr<block> dst,
    const type_substitution_map& subst,
    const value_substitution_map& val_subst)
{
    // Block-local alias/typedef declarations live in the block's alias_holder,
    // not in its statement list: clone them before the statements so a lookup
    // inside the instantiated body still finds them.
    for (const auto& src_alias : src.get_aliases()) {
        if (!src_alias) continue;
        auto cloned_alias = alias_definition::make_shared(dst, src_alias->get_short_name(),
                                                          src_alias->get_kind());
        cloned_alias->set_block_local(true);
        cloned_alias->set_visibility(PRIVATE);
        cloned_alias->set_target_name(src_alias->get_target_name());
        cloned_alias->set_decl_lexeme(src_alias->get_decl_lexeme());
        if (auto tgt = src_alias->get_target_type()) {
            cloned_alias->set_target_type(substitute_type(tgt, subst));
        }
        dst->add_alias(cloned_alias);
    }

    for (auto& stmt : src.get_statements()) {
        if (!stmt) continue;
        auto cloned = clone_statement(*stmt, dst, subst, val_subst);
        if (cloned) {
            dst->append_statement(cloned);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Clone member variable
// ═══════════════════════════════════════════════════════════════════════════

void template_instantiator::clone_member_variable(
    const member_variable_definition& src,
    std::shared_ptr<aggregate> target,
    const type_substitution_map& subst,
    const value_substitution_map& val_subst)
{
    // Skip the synthetic __parent__ field
    if (src.get_short_name() == "__parent__") return;
    // Skip synthetic vptr fields (__vptr__) — they are re-injected by the
    // resolution passes via klass::inject_vptr_field after vtable building.
    if (src.get_short_name().rfind("__vptr", 0) == 0) return;
    // Skip synthetic base sub-object fields (__base_X__, __vbptr_X__, __vbase_X__)
    if (src.get_short_name().rfind("__base_", 0) == 0) return;
    if (src.get_short_name().rfind("__vbptr_", 0) == 0) return;
    if (src.get_short_name().rfind("__vbase_", 0) == 0) return;

    bool is_static = false;
    auto new_var = target->append_variable(src.get_short_name(), is_static);
    if (!new_var) return;

    // Substitute type
    auto src_type = std::const_pointer_cast<type>(src.get_type());
    new_var->set_type(substitute_type(src_type, subst));
    new_var->set_const(src.is_const());

    // Clone init expression and retarget to new variable
    if (src.get_init_expr()) {
        auto cloned_init = clone_and_substitute_expr(
            std::const_pointer_cast<expression>(src.get_init_expr()), subst, val_subst);
        new_var->set_init_expr(cloned_init);
        retarget_init_expr(cloned_init, new_var);
    }

    // Copy visibility
    if (auto mv = std::dynamic_pointer_cast<member_variable_definition>(new_var)) {
        mv->set_visibility(src.get_visibility());
    }

    // Copy AST node reference (optional, for diagnostics)
    if (auto elem = std::dynamic_pointer_cast<element>(new_var)) {
        elem->_ast_node = src.get_ast_node();
        elem->_documentation = src.get_documentation();
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Pack expansion helpers
// ═══════════════════════════════════════════════════════════════════════════

namespace {

using pack_names_map = std::unordered_map<std::string, std::vector<std::string>>;

/**
 * Expand pack_expansion_expression arguments in a function_invocation_expression.
 * Replaces `f(args...)` with `f(args_0, args_1, ..., args_N)`.
 */
void expand_pack_in_invocation_args(
    std::vector<std::shared_ptr<expression>>& args,
    const pack_names_map& pack_expansion_names)
{
    std::vector<std::shared_ptr<expression>> new_args;
    bool expanded = false;
    for (auto& arg : args) {
        if (auto pe = std::dynamic_pointer_cast<pack_expansion_expression>(arg)) {
            // Look up the pack name in the expansion map
            const auto& pack_name = pe->pack_name();
            auto it = pack_expansion_names.find(pack_name);
            if (it != pack_expansion_names.end()) {
                // Replace with symbol expressions referencing each concrete parameter
                for (const auto& concrete_name : it->second) {
                    auto sym = symbol_expression::from_identifier(
                        name(false, {concrete_name}));
                    new_args.push_back(sym);
                }
                expanded = true;
            } else {
                new_args.push_back(arg);
            }
        } else {
            new_args.push_back(arg);
        }
    }
    if (expanded) {
        args = std::move(new_args);
    }
}

/**
 * Recursively walk an expression tree and expand pack expressions in invocations.
 */
void expand_pack_in_expr(
    std::shared_ptr<expression>& expr,
    const pack_names_map& pack_expansion_names)
{
    if (!expr) return;

    if (auto fie = std::dynamic_pointer_cast<function_invocation_expression>(expr)) {
        auto callee = std::const_pointer_cast<expression>(fie->callee_expr());
        expand_pack_in_expr(callee, pack_expansion_names);
        // Expand packs in arguments
        std::vector<std::shared_ptr<expression>> args;
        for (auto& a : fie->arguments()) {
            args.push_back(std::const_pointer_cast<expression>(a));
        }
        expand_pack_in_invocation_args(args, pack_expansion_names);
        fie->arguments(args);
    } else if (auto cie = std::dynamic_pointer_cast<constructor_invocation_expression>(expr)) {
        std::vector<std::shared_ptr<expression>> args;
        for (auto& a : cie->arguments()) {
            args.push_back(std::const_pointer_cast<expression>(a));
        }
        expand_pack_in_invocation_args(args, pack_expansion_names);
        cie->arguments(args);
    } else if (auto ne = std::dynamic_pointer_cast<new_expression>(expr)) {
        std::vector<std::shared_ptr<expression>> args;
        for (auto& a : ne->arguments()) {
            args.push_back(std::const_pointer_cast<expression>(a));
        }
        expand_pack_in_invocation_args(args, pack_expansion_names);
        ne->assign_arguments(args);
    }
}

/**
 * Walk all statements in a block and expand pack expressions in invocations.
 */
void expand_pack_in_block(
    std::shared_ptr<block> blk,
    const pack_names_map& pack_expansion_names);

void expand_pack_in_statement(
    std::shared_ptr<statement> stmt,
    const pack_names_map& pack_expansion_names)
{
    if (!stmt) return;

    if (auto es = std::dynamic_pointer_cast<expression_statement>(stmt)) {
        auto expr = es->get_expression();
        expand_pack_in_expr(expr, pack_expansion_names);
        es->set_expression(expr);
    } else if (auto rs = std::dynamic_pointer_cast<return_statement>(stmt)) {
        auto expr = rs->get_expression();
        expand_pack_in_expr(expr, pack_expansion_names);
        if (expr) rs->set_expression(expr);
    } else if (auto bs = std::dynamic_pointer_cast<block>(stmt)) {
        expand_pack_in_block(bs, pack_expansion_names);
    } else if (auto ifs = std::dynamic_pointer_cast<if_else_statement>(stmt)) {
        expand_pack_in_statement(std::const_pointer_cast<statement>(ifs->get_then_stmt()), pack_expansion_names);
        expand_pack_in_statement(std::const_pointer_cast<statement>(ifs->get_else_stmt()), pack_expansion_names);
    } else if (auto ws = std::dynamic_pointer_cast<while_statement>(stmt)) {
        expand_pack_in_statement(std::const_pointer_cast<statement>(ws->get_nested_stmt()), pack_expansion_names);
    } else if (auto fs = std::dynamic_pointer_cast<for_statement>(stmt)) {
        expand_pack_in_statement(std::const_pointer_cast<statement>(fs->get_nested_stmt()), pack_expansion_names);
    } else if (auto vs = std::dynamic_pointer_cast<variable_statement>(stmt)) {
        auto expr = vs->get_init_expr();
        if (expr) {
            expand_pack_in_expr(expr, pack_expansion_names);
            vs->variable_definition::set_init_expr(expr);
        }
    }
}

void expand_pack_in_block(
    std::shared_ptr<block> blk,
    const pack_names_map& pack_expansion_names)
{
    if (!blk) return;
    for (auto& stmt : blk->get_statements()) {
        expand_pack_in_statement(stmt, pack_expansion_names);
    }
}

} // anonymous namespace

void template_instantiator::expand_pack_expressions_in_block(
    std::shared_ptr<block> blk,
    const std::unordered_map<std::string, std::vector<std::string>>& pack_expansion_names)
{
    expand_pack_in_block(blk, pack_expansion_names);
}

// ═══════════════════════════════════════════════════════════════════════════
// Populate function from template source
// ═══════════════════════════════════════════════════════════════════════════

void template_instantiator::populate_function_from_template(
    std::shared_ptr<function> dst,
    const function& src,
    const type_substitution_map& subst,
    const value_substitution_map& val_subst,
    const pack_substitution_map& pack_subst)
{
    // Set return type
    if (src.has_return_type()) {
        dst->set_return_type(substitute_type(
            std::const_pointer_cast<type>(src.get_return_type()), subst));
    }

    // Clone parameters (skip 'this' — will be recreated by resolution passes)
    // Map from original pack param names to generated concrete param names
    std::unordered_map<std::string, std::vector<std::string>> pack_expansion_names;
    for (auto& param : src.parameters()) {
        if (param == src.get_this_parameter()) continue;

        if (param->is_pack_expansion() && !param->pack_param_name().empty()) {
            // Expand pack parameter into N concrete parameters
            auto it = pack_subst.find(param->pack_param_name());
            if (it != pack_subst.end()) {
                const auto& pack_types = it->second;
                std::vector<std::string> generated_names;
                for (size_t i = 0; i < pack_types.size(); ++i) {
                    std::string concrete_name = param->get_short_name() + "_" + std::to_string(i);
                    auto new_param = dst->append_parameter(concrete_name, pack_types[i]);
                    new_param->set_const(param->is_const());
                    new_param->_ast_node = param->get_ast_node();
                    new_param->_documentation = param->get_documentation();
                    generated_names.push_back(concrete_name);
                }
                pack_expansion_names[param->get_short_name()] = std::move(generated_names);
            } else {
                // Pack param not in pack_subst — preserve as-is for member templates
                // whose inner template parameters haven't been instantiated yet.
                auto param_type = substitute_type(
                    std::const_pointer_cast<type>(param->get_type()), subst);
                auto new_param = dst->append_parameter(param->get_short_name(), param_type);
                new_param->set_const(param->is_const());
                new_param->set_pack_expansion(true);
                new_param->set_pack_param_name(param->pack_param_name());
                new_param->_ast_node = param->get_ast_node();
                new_param->_documentation = param->get_documentation();
            }
        } else {
            auto param_type = substitute_type(
                std::const_pointer_cast<type>(param->get_type()), subst);
            auto new_param = dst->append_parameter(param->get_short_name(), param_type);
            new_param->set_const(param->is_const());
            new_param->set_varargs(param->is_varargs());
            new_param->_ast_node = param->get_ast_node(); // diagnostics
            new_param->_documentation = param->get_documentation();
            // Preserve the default value expression (e.g. "flag: bool = true"),
            // otherwise overload resolution on the instantiated function would
            // require all arguments to be provided explicitly.
            new_param->set_default_expr(param->get_default_expr());
        }
    }

    // Clone body only when the source function actually has one.
    // Using get_block() would synthesize empty blocks for signature-only imports.
    auto src_block = src._block;
    if (src_block) {
        auto dst_block = dst->get_block();
        if (dst_block) {
            clone_block_contents(*src_block, dst_block, subst, val_subst);
            // Post-process: expand pack_expansion_expression in function/constructor invocations
            if (!pack_expansion_names.empty()) {
                expand_pack_expressions_in_block(dst_block, pack_expansion_names);
            }
        }
    }

    // Copy throws spec (raw names — will be resolved by the resolution passes)
    for (auto& raw : src.get_throws_spec_raw()) {
        dst->add_throws_spec_raw(raw);
    }

    // Copy AST node (optional, for diagnostics)
    dst->_ast_node = src.get_ast_node();
    dst->_documentation = src.get_documentation();

    // Copy annotations (preserves @Intrinsic and other compile-time annotations)
    for (auto& ann : src.get_annotations()) {
        dst->add_annotation(ann);
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Clone method
// ═══════════════════════════════════════════════════════════════════════════

void template_instantiator::clone_method(
    const function& src,
    std::shared_ptr<aggregate> target,
    const type_substitution_map& subst,
    const value_substitution_map& val_subst)
{
    auto new_func = target->define_function(src.get_short_name(), src.is_static());
    if (!new_func) return;

    // Copy flags
    new_func->set_visibility(src.get_visibility());
    new_func->set_const_member(src.is_const_member());
    new_func->set_operator(src.is_operator());
    new_func->set_virtual(src.is_virtual());
    new_func->set_abstract_func(src.is_abstract_func());
    new_func->set_final_func(src.is_final_func());
    new_func->set_override_specifier(src.is_override_specifier());
    new_func->set_default_method(src.is_default_method());
    new_func->set_aliasing(src.get_aliasing());
    new_func->set_compiler_generated(src.is_compiler_generated());

    // If the source method is itself a template (member template function),
    // preserve its tpl_info on the cloned method. The method's own template
    // parameters remain unresolved — they will be instantiated when the member
    // template is invoked with explicit template arguments.
    if (src.get_tpl_info()) {
        auto* src_ti = src.get_tpl_info();
        auto new_ti = std::make_unique<tpl_info>();
        new_ti->params = src_ti->params;
        new_ti->is_generic = src_ti->is_generic;
        new_ti->source_text = src_ti->source_text;
        new_func->set_tpl_info(std::move(new_ti));
        // For member templates, apply outer-struct type substitution to the
        // return type and body, but do NOT expand the method's own template params.
        // We still need to populate the function signature/body with the outer subst applied.
    }

    populate_function_from_template(new_func, src, subst, val_subst);
}

// ═══════════════════════════════════════════════════════════════════════════
// Clone constructor
// ═══════════════════════════════════════════════════════════════════════════

void template_instantiator::clone_constructor(
    const constructor& src,
    std::shared_ptr<aggregate> target,
    const type_substitution_map& subst,
    const value_substitution_map& val_subst)
{
    // define_function with the aggregate name creates a constructor
    auto new_func = target->define_function(target->get_short_name(), false);
    auto new_ctor = std::dynamic_pointer_cast<constructor>(new_func);
    if (!new_ctor) return;

    // Copy flags
    new_ctor->set_visibility(src.get_visibility());
    new_ctor->set_aliasing(src.get_aliasing());
    new_ctor->set_compiler_generated(src.is_compiler_generated());
    new_ctor->set_copy_constructor(src.is_copy_constructor());

    // Clone member inits
    for (auto& mi : src.member_inits()) {
        std::vector<std::shared_ptr<expression>> new_args;
        for (auto& arg : mi.args) {
            new_args.push_back(clone_and_substitute_expr(arg, subst, val_subst));
        }
        new_ctor->add_member_init(mi.member_name, std::move(new_args), mi.is_base_init);
    }

    populate_function_from_template(new_ctor, src, subst, val_subst);
}

// ═══════════════════════════════════════════════════════════════════════════
// Clone destructor
// ═══════════════════════════════════════════════════════════════════════════

void template_instantiator::clone_destructor(
    const destructor& src,
    std::shared_ptr<aggregate> target,
    const type_substitution_map& subst,
    const value_substitution_map& val_subst)
{
    // define_function with "~" + aggregate name creates a destructor
    auto new_func = target->define_function("~" + target->get_short_name(), false);
    auto new_dtor = std::dynamic_pointer_cast<destructor>(new_func);
    if (!new_dtor) return;

    new_dtor->set_visibility(src.get_visibility());
    populate_function_from_template(new_dtor, src, subst, val_subst);
}

// ═══════════════════════════════════════════════════════════════════════════
// Clone nested aggregate
// ═══════════════════════════════════════════════════════════════════════════

void template_instantiator::clone_nested_aggregate(
    const aggregate& src,
    std::shared_ptr<aggregate> target,
    const type_substitution_map& subst,
    const value_substitution_map& val_subst,
    std::shared_ptr<context> ctx)
{
    // Create the nested aggregate inside target (not in the parent namespace).
    std::shared_ptr<aggregate> nested;
    if (src.is_class()) {
        nested = target->define_class(src.get_short_name());
    } else {
        nested = target->define_structure(src.get_short_name());
    }
    if (!nested) return;

    // Copy aggregate flags
    nested->set_final(src.is_final());
    nested->set_abstract(src.is_abstract());
    nested->set_const_struct(src.is_const_struct());
    nested->set_visibility(src.get_visibility());
    nested->_ast_node = src.get_ast_node();
    nested->_documentation = src.get_documentation();
    // Nested aggregate of an instantiation is itself part of the instantiation.
    nested->mark_instantiation();

    // Give the nested aggregate a struct_type named after the enclosing instantiation.
    // The later resolution passes would otherwise create it from the bare short name, so
    // Outer<int>::Inner and Outer<long>::Inner would both be called "Inner" and rely on
    // LLVM's compilation-order-dependent ".N" auto-uniquification — which then leaks into
    // the exported KDI and makes cross-module type identity non-deterministic.
    if (ctx && !nested->get_struct_type()) {
        std::shared_ptr<struct_type> nested_st{
            new struct_type(nested_type_name(*target, src.get_short_name()),
                            nested->shared_as<aggregate>())};
        ctx->add_struct(nested_st);
        nested->set_struct_type(nested_st);
    }

    // Copy base class specs (raw names — resolved later by resolution passes)
    // Substitute template type parameters in base names (e.g. "Collection<T>" → "Collection<int>")
    for (auto& bs : src.get_bases()) {
        nested->add_base(substitute_base_name(bs.raw_name, subst), bs.vis);
        if (bs.is_virtual) {
            nested->get_bases_mutable().back().is_virtual = true;
        }
    }

    // Clone all children with type substitution
    for (auto& child : src.get_children()) {
        if (auto mv = std::dynamic_pointer_cast<member_variable_definition>(child)) {
            clone_member_variable(*mv, nested, subst, val_subst);
        } else if (auto ctor = std::dynamic_pointer_cast<constructor>(child)) {
            clone_constructor(*ctor, nested, subst, val_subst);
        } else if (auto dtor = std::dynamic_pointer_cast<destructor>(child)) {
            clone_destructor(*dtor, nested, subst, val_subst);
        } else if (auto fn = std::dynamic_pointer_cast<function>(child)) {
            clone_method(*fn, nested, subst, val_subst);
        } else if (auto inner = std::dynamic_pointer_cast<aggregate>(child)) {
            // Recursively clone deeper nested aggregates
            clone_nested_aggregate(*inner, nested, subst, val_subst, ctx);
        } else if (auto inner_un = std::dynamic_pointer_cast<union_type_def>(child)) {
            // Clone nested union types (e.g. an inner union inside a nested struct)
            clone_nested_union(*inner_un, nested, subst, ctx);
        }
    }

    // Generate a default constructor when no explicit constructor was cloned
    if (nested->constructors().empty()) {
        auto default_ctor = constructor::make_shared(nested->shared_as<aggregate>());
        default_ctor->set_compiler_generated(true);
        nested->_constructors.push_back(default_ctor);
        nested->_children.push_back(default_ctor);
    }

    nested->update_mangled_name();
}

// ═══════════════════════════════════════════════════════════════════════════
// Clone: nested union inside a template aggregate
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Clone a nested union_type_def from a template aggregate into a concrete
 * aggregate, applying type substitution to each alternative's type.
 *
 * After cloning, the nested union is registered in target->_unions (via
 * define_union()), making it visible to scope lookups that search for
 * "UnionName::Kind::entry" inside method bodies of the concrete aggregate.
 */
void template_instantiator::clone_nested_union(
    const union_type_def& src,
    std::shared_ptr<aggregate> target,
    const type_substitution_map& subst,
    std::shared_ptr<context> ctx)
{
    // Create the nested union inside target (registers it in _unions).
    auto nested = target->define_union(src.get_short_name());
    if (!nested) return;

    nested->set_visibility(src.get_visibility());

    // Give the nested union a **fresh** struct_type and LLVM struct type, one per
    // instantiation. Reusing the template definition's struct_type (as this code used to
    // do) makes every sibling instantiation share a single llvm::StructType; since
    // declaration_generator::visit_union() only computes the payload size of the first
    // instantiation it finalises and skips the others, an Expected<long,E> could end up
    // with the 4-byte payload computed for Expected<int,E> and silently truncate values.
    if (ctx) {
        const std::string st_name = nested_type_name(*target, src.get_short_name());
        auto st_type = std::make_shared<struct_type>(st_name, std::weak_ptr<aggregate>{});
        auto* union_llvm_type = llvm::StructType::create(ctx->llvm_context(), st_name + "_union");
        ctx->attach_llvm_struct_type(st_type, union_llvm_type);
        ctx->add_struct(st_type);
        nested->set_struct_type(st_type);
    }

    // Clone alternatives with type substitution.
    for (const auto& alt : src.alternatives()) {
        std::string raw = alt.raw_type_name;
        // Substitute raw type name if it refers to a template parameter (e.g. "R" → "int").
        auto subst_it = subst.find(raw);
        if (subst_it != subst.end() && subst_it->second) {
            raw = subst_it->second->to_string();
        }
        nested->add_alternative(alt.name, raw, alt.is_const);

        // Substitute the resolved type as well.
        auto& new_alt = nested->alternatives_mutable().back();
        if (alt.resolved_type) {
            new_alt.resolved_type = substitute_type(alt.resolved_type, subst);
        } else if (!raw.empty() && ctx) {
            auto from_ctx = ctx->from_string(raw);
            if (from_ctx) new_alt.resolved_type = from_ctx;
        }
    }

    nested->update_mangled_name();
}

// ═══════════════════════════════════════════════════════════════════════════
// Instantiation: aggregate
// ═══════════════════════════════════════════════════════════════════════════

std::shared_ptr<aggregate> template_instantiator::instantiate_aggregate(
    aggregate& tpl_def,
    const std::vector<template_argument>& args,
    std::shared_ptr<ns> parent_ns,
    k::model::unit& unit,
    std::shared_ptr<context> ctx,
    k::log::logger& logger)
{
    if (g_template_instantiation_depth >= MAX_TEMPLATE_INSTANTIATION_DEPTH) {
        auto diag = k::log::diagnostic::make_error(
            static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_INSTANTIATION_DEPTH_EXCEEDED),
            "template instantiation of '{0}' exceeded the maximum recursion depth ({1}); "
            "check for a recursive template definition (e.g. a template that instantiates "
            "itself as a base or member with an ever-changing argument)",
            {tpl_def.get_short_name(), std::to_string(MAX_TEMPLATE_INSTANTIATION_DEPTH)});
        logger.report(diag);
        throw k::log::compiler_error(std::move(diag));
    }
    template_instantiation_depth_guard depth_guard;

    auto* ti = tpl_def.get_tpl_info();
    if (!ti) return nullptr;

    // Check instantiation cache
    std::string key = build_instantiation_key(args);
    auto it = ti->instantiations.find(key);
    if (it != ti->instantiations.end()) {
        if (auto* agg_ptr = std::get_if<std::shared_ptr<aggregate>>(&it->second)) {
            return *agg_ptr;
        }
    }

    // Build the instantiated name
    std::string base_name = tpl_def.get_short_name();
    std::string inst_name = build_instantiated_name(base_name, args);

    // Build type substitution map
    auto subst = build_substitution_map(*ti, args);
    auto val_subst = build_value_substitution_map(*ti, args);

    // 1. Create a new concrete aggregate in the parent namespace
    std::shared_ptr<aggregate> concrete;
    if (std::dynamic_pointer_cast<model::interface>(tpl_def.shared_as<element>())) {
        concrete = parent_ns->define_interface(inst_name);
    } else if (tpl_def.is_class()) {
        concrete = parent_ns->define_class(inst_name);
    } else if (tpl_def.is_annotation()) {
        concrete = parent_ns->define_annotation(inst_name);
    } else {
        concrete = parent_ns->define_structure(inst_name);
    }
    if (!concrete) return nullptr;

    // Copy aggregate flags
    concrete->set_final(tpl_def.is_final());
    concrete->set_abstract(tpl_def.is_abstract());
    concrete->set_const_struct(tpl_def.is_const_struct());
    concrete->set_visibility(tpl_def.get_visibility());

    // Copy AST reference (optional, for diagnostics)
    concrete->_ast_node = tpl_def.get_ast_node();
    concrete->_documentation = tpl_def.get_documentation();

    // Store template instantiation info for mangling (I…E encoding)
    concrete->set_tpl_instantiation_info(base_name, args);
    // Mark as a synthesised instantiation so codegen applies linkonce_odr + COMDAT.
    concrete->mark_instantiation();

    // Copy friend directives from the template definition, substituting template parameters.
    // For 'friend Foo<T>;' with T=int: the concrete instantiation gets 'friend Foo<int>'.
    for (const auto& dir : tpl_def.get_friend_directives()) {
        friend_directive new_dir;
        new_dir.filter = dir.filter;
        new_dir.target_name = dir.target_name;
        new_dir.has_explicit_template_args = dir.has_explicit_template_args;
        new_dir.ast_node = dir.ast_node;
        // Substitute template parameters in each raw arg name.
        new_dir.raw_template_arg_names = dir.raw_template_arg_names;
        for (const auto& raw_name : dir.raw_template_arg_names) {
            auto it = subst.find(raw_name);
            if (it != subst.end() && it->second) {
                new_dir.resolved_tpl_arg_types.push_back(it->second);
            } else {
                // Not a template parameter — try to resolve as a known type from context.
                std::shared_ptr<type> resolved;
                if (ctx) {
                    resolved = ctx->from_string(raw_name);
                }
                new_dir.resolved_tpl_arg_types.push_back(resolved);
            }
        }
        concrete->add_friend_directive(std::move(new_dir));
    }

    // Copy bases (with template parameter substitution in raw names). Propagate
    // the generic template's is_virtual flags so that instantiations are born with
    // the correct virtual-base edges *before* any layout materialisation — this is
    // essential for diamonds that only exist within a template hierarchy, whose
    // intermediates may materialise on-demand before the derived diamond is seen.
    for (auto& bs : tpl_def.get_bases()) {
        concrete->add_base(substitute_base_name(bs.raw_name, subst), bs.vis);
        if (bs.is_virtual) {
            concrete->get_bases_mutable().back().is_virtual = true;
        }
    }

    // Resolve template base classes immediately (e.g. "Collection<int>")
    for (auto& bs : concrete->get_bases_mutable()) {
        if (bs.base) continue; // Already resolved
        auto lt_pos = bs.raw_name.find('<');
        if (lt_pos != std::string::npos) {
            // resolve_base_type_name() recurses into nested generic arguments
            // (e.g. the "Pair<int,int>" inside "Iter<Pair<int,int>>"), instantiating
            // them as needed — a plain top-level split-and-lookup (as this used to
            // do inline) can never resolve a compound nested argument, leaving
            // bs.base permanently unresolved for multi-level generic bases.
            if (auto resolved = resolve_base_type_name(bs.raw_name, subst, parent_ns, unit, ctx, logger)) {
                if (auto st = std::dynamic_pointer_cast<struct_type>(resolved)) {
                    if (auto agg = st->get_struct()) {
                        bs.base = agg;
                    }
                }
            }
        } else {
            // Simple (non-template) base name: look up directly.
            // Non-generic bases (e.g. "Sized") of an imported template are NOT
            // re-homed/flattened into the consumer's root namespace the way
            // imported *template* definitions are (see the "re-parse trick" comment
            // above in try_instantiate_template_type) — so a flat lookup in
            // parent_ns's direct children misses them. Fall back to resolving the
            // base via the template's origin module (KDI-backed imported aggregate
            // registry), which correctly locates non-template imported bases
            // regardless of namespace nesting.
            if (auto found = parent_ns->get_aggregate(bs.raw_name)) {
                bs.base = found;
            } else if (ti && !ti->origin_module_ns_fq.empty()) {
                std::vector<std::string> parts;
                std::size_t pos = 0;
                const std::string& origin = ti->origin_module_ns_fq;
                while (true) {
                    auto sep = origin.find("::", pos);
                    if (sep == std::string::npos) { parts.push_back(origin.substr(pos)); break; }
                    parts.push_back(origin.substr(pos, sep - pos));
                    pos = sep + 2;
                }
                parts.push_back(bs.raw_name);
                if (auto imp = unit.get_or_create_imported_aggregate(k::name{false, std::move(parts)}, ctx)) {
                    bs.base = std::static_pointer_cast<aggregate>(imp);
                }
            }

            // Final fallback: an unqualified base name that isn't reachable from
            // the template's own origin module — this is the case for a base
            // implicitly injected on the TEMPLATE DEFINITION itself (e.g. every
            // class/interface with no declared base implicitly extends
            // "::k::Object", and every annotation implicitly extends
            // "::k::Annotation" — see symbol_resolver::visit_unit's implicit-base
            // pre-passes in gen_unit.cpp). That injection runs once on the
            // template definition and is copied verbatim into every instantiation
            // (loop just above), but the name was never namespace-qualified nor
            // tied to the template's origin module, so neither lookup above finds
            // it. Search every module actually imported by THIS compilation unit
            // (mirrors scope_lookup::lookup_structure_or_import's import fallback,
            // used by the equivalent non-template base-resolution path in
            // gen_struct.cpp) — this generically covers "Object", "Annotation",
            // and any other implicitly-injected or otherwise reachable base name,
            // not just ones specific to k::Object.
            if (!bs.base) {
                for (const auto& imp_mod : unit.get_imports()) {
                    if (imp_mod.module_name.empty()) continue;
                    if (auto imp = unit.get_or_create_imported_aggregate(
                            imp_mod.module_name.with_back(bs.raw_name), ctx)) {
                        bs.base = std::static_pointer_cast<aggregate>(imp);
                        break;
                    }
                }
            }
        }
    }

    // Diamond detection: all of 'concrete's bases (and their transitively
    // instantiated bases) are now fully resolved (bs.base assigned), but NONE
    // of them have had their base sub-object fields (__base_X__/__vbptr_X__)
    // injected yet — that happens later, in a separate visitor pass (either
    // symbol_resolver::visit_aggregate in gen_struct.cpp, or explicitly via
    // inject_base_subobject_fields() below/at the call sites in
    // resolvers_aggregate.cpp / resolvers_type_ref.cpp). This is exactly the
    // right point to run diamond detection for template-instantiated
    // hierarchies: the early global prepass (gen_unit.cpp,
    // klass::compute_virtual_bases) runs before any template is instantiated
    // and cannot see these bases (their raw_name contains '<' and is skipped),
    // so without this call, diamonds that only exist within a template
    // hierarchy (e.g. Derived<T> : Mid1<T>, Mid2<T> both -> Base<T>, or the
    // interface diamond MutableIndexedCollection<T> : IndexedCollection<T>,
    // MutableCollection<T> both -> Collection<T>) would never be marked
    // virtual, causing "Ambiguous access to member" errors later.
    klass::compute_virtual_bases_single(*concrete);

    // 2. Clone children from the template aggregate
    for (auto& child : tpl_def.get_children()) {
        if (auto mv = std::dynamic_pointer_cast<member_variable_definition>(child)) {
            clone_member_variable(*mv, concrete, subst, val_subst);
        } else if (auto ctor = std::dynamic_pointer_cast<constructor>(child)) {
            clone_constructor(*ctor, concrete, subst, val_subst);
        } else if (auto dtor = std::dynamic_pointer_cast<destructor>(child)) {
            clone_destructor(*dtor, concrete, subst, val_subst);
        } else if (auto fn = std::dynamic_pointer_cast<function>(child)) {
            clone_method(*fn, concrete, subst, val_subst);
        } else if (auto gv = std::dynamic_pointer_cast<global_variable_definition>(child)) {
            // Static member variable — clone similarly to member variable
            auto new_var = concrete->append_variable(gv->get_short_name(), /*is_static=*/true);
            if (new_var) {
                auto src_type = std::const_pointer_cast<type>(gv->get_type());
                new_var->set_type(substitute_type(src_type, subst));
                new_var->set_const(gv->is_const());
                if (gv->get_init_expr()) {
                    auto cloned_init = clone_and_substitute_expr(
                        std::const_pointer_cast<expression>(gv->get_init_expr()), subst, val_subst);
                    new_var->set_init_expr(cloned_init);
                    retarget_init_expr(cloned_init, new_var);
                }
                if (auto gv_new = std::dynamic_pointer_cast<global_variable_definition>(new_var)) {
                    gv_new->set_visibility(gv->get_visibility());
                }
            }
        } else if (auto inner = std::dynamic_pointer_cast<aggregate>(child)) {
            // Clone nested aggregate types (structs/classes defined inside the template)
            clone_nested_aggregate(*inner, concrete, subst, val_subst, ctx);
        } else if (auto inner_un = std::dynamic_pointer_cast<union_type_def>(child)) {
            // Clone nested union types (e.g. Expected<R,E>::Storage)
            clone_nested_union(*inner_un, concrete, subst, ctx);
        }
        // Enums and using declarations — handled as needed in future
    }

    // 3. Post-instantiation: generate default constructor and set up this parameters
    //    The concrete aggregate was not visited by symbol_resolver, so it needs
    //    these essential setup steps that symbol_resolver::visit_aggregate normally does.

    // 3a. Generate a default constructor if no explicit constructor was cloned
    if (concrete->constructors().empty()) {
        auto default_ctor = constructor::make_shared(concrete->shared_as<aggregate>());
        default_ctor->set_compiler_generated(true);
        concrete->_constructors.push_back(default_ctor);
        concrete->_children.push_back(default_ctor);
    }

    // 3b. Set up 'this' parameters for all member functions and constructors
    //     (requires struct_type to be set — callers must ensure this after creating the struct_type)

    // 3c. Update mangled names (if possible — struct_type may not be set yet)
    concrete->update_mangled_name();

    // 4. Register in the instantiation cache
    ti->instantiations[key] = concrete;

    return concrete;
}

std::shared_ptr<aggregate> template_instantiator::synthesize_generic_aggregate(
    aggregate& tpl_def,
    std::shared_ptr<ns> parent_ns,
    k::model::unit& unit,
    std::shared_ptr<context> ctx,
    k::log::logger& logger)
{
    auto* ti = tpl_def.get_tpl_info();
    if (!ti || !ti->is_generic) return nullptr;

    auto cached = ti->instantiations.find(generic_synthesis_key);
    if (cached != ti->instantiations.end()) {
        if (auto* agg_ptr = std::get_if<std::shared_ptr<aggregate>>(&cached->second)) {
            return *agg_ptr;
        }
    }

    const std::string base_name = tpl_def.get_short_name();
    auto subst = build_generic_substitution_map(*ti, ctx);
    value_substitution_map val_subst;

    std::shared_ptr<aggregate> concrete;
    if (std::dynamic_pointer_cast<model::interface>(tpl_def.shared_as<element>())) {
        concrete = parent_ns->define_interface(base_name);
    } else if (tpl_def.is_class()) {
        concrete = parent_ns->define_class(base_name);
    } else if (tpl_def.is_annotation()) {
        concrete = parent_ns->define_annotation(base_name);
    } else {
        concrete = parent_ns->define_structure(base_name);
    }
    if (!concrete) return nullptr;

    (void)unit;
    (void)logger;

    concrete->set_final(tpl_def.is_final());
    concrete->set_abstract(tpl_def.is_abstract());
    concrete->set_const_struct(tpl_def.is_const_struct());
    concrete->set_visibility(tpl_def.get_visibility());
    concrete->_ast_node = tpl_def.get_ast_node();
    concrete->_documentation = tpl_def.get_documentation();
    // Mark as a synthesised instantiation so codegen applies linkonce_odr + COMDAT.
    concrete->mark_instantiation();

    // Keep the synthesized symbol on the base aggregate name (no arg suffix).

    for (auto& bs : tpl_def.get_bases()) {
        concrete->add_base(substitute_base_name(bs.raw_name, subst), bs.vis);
        if (bs.is_virtual) {
            concrete->get_bases_mutable().back().is_virtual = true;
        }
    }

    for (auto& child : tpl_def.get_children()) {
        if (auto mv = std::dynamic_pointer_cast<member_variable_definition>(child)) {
            clone_member_variable(*mv, concrete, subst, val_subst);
        } else if (auto ctor = std::dynamic_pointer_cast<constructor>(child)) {
            clone_constructor(*ctor, concrete, subst, val_subst);
        } else if (auto dtor = std::dynamic_pointer_cast<destructor>(child)) {
            clone_destructor(*dtor, concrete, subst, val_subst);
        } else if (auto fn = std::dynamic_pointer_cast<function>(child)) {
            clone_method(*fn, concrete, subst, val_subst);
        } else if (auto gv = std::dynamic_pointer_cast<global_variable_definition>(child)) {
            auto new_var = concrete->append_variable(gv->get_short_name(), /*is_static=*/true);
            if (new_var) {
                auto src_type = std::const_pointer_cast<type>(gv->get_type());
                new_var->set_type(substitute_type(src_type, subst));
                new_var->set_const(gv->is_const());
                if (gv->get_init_expr()) {
                    auto cloned_init = clone_and_substitute_expr(
                        std::const_pointer_cast<expression>(gv->get_init_expr()), subst, val_subst);
                    new_var->set_init_expr(cloned_init);
                    retarget_init_expr(cloned_init, new_var);
                }
                if (auto gv_new = std::dynamic_pointer_cast<global_variable_definition>(new_var)) {
                    gv_new->set_visibility(gv->get_visibility());
                }
            }
        } else if (auto inner = std::dynamic_pointer_cast<aggregate>(child)) {
            // Clone nested aggregate types (e.g. private Node struct inside LinkedList)
            clone_nested_aggregate(*inner, concrete, subst, val_subst);
        } else if (auto inner_un = std::dynamic_pointer_cast<union_type_def>(child)) {
            // Clone nested union types
            clone_nested_union(*inner_un, concrete, subst, ctx);
        }
    }

    if (concrete->constructors().empty()) {
        auto default_ctor = constructor::make_shared(concrete->shared_as<aggregate>());
        default_ctor->set_compiler_generated(true);
        concrete->_constructors.push_back(default_ctor);
        concrete->_children.push_back(default_ctor);
    }

    concrete->update_mangled_name();
    ti->instantiations[generic_synthesis_key] = concrete;
    return concrete;
}

// ═══════════════════════════════════════════════════════════════════════════
// Instantiation: function
// ═══════════════════════════════════════════════════════════════════════════

std::shared_ptr<function> template_instantiator::instantiate_function(
    function& tpl_def,
    const std::vector<template_argument>& args,
    std::shared_ptr<ns> parent_ns,
    k::model::unit& unit,
    std::shared_ptr<context> ctx,
    k::log::logger& logger)
{
    auto* ti = tpl_def.get_tpl_info();
    if (!ti) return nullptr;

    // Check instantiation cache
    std::string key = build_instantiation_key(args);
    auto it = ti->instantiations.find(key);
    if (it != ti->instantiations.end()) {
        if (auto* fn_ptr = std::get_if<std::shared_ptr<function>>(&it->second)) {
            return *fn_ptr;
        }
    }

    // Build the instantiated name
    std::string base_name = tpl_def.get_short_name();
    std::string inst_name = build_instantiated_name(base_name, args);

    // Build type substitution map
    auto subst = build_substitution_map(*ti, args);
    auto val_subst = build_value_substitution_map(*ti, args);
    auto pack_subst = build_pack_substitution_map(*ti, args);

    // 1. Create a new concrete function in the parent namespace
    auto concrete = parent_ns->define_function(inst_name, tpl_def.is_static());
    if (!concrete) return nullptr;

    // Copy flags
    concrete->set_visibility(tpl_def.get_visibility());
    concrete->set_const_member(tpl_def.is_const_member());
    concrete->set_operator(tpl_def.is_operator());
    concrete->set_aliasing(tpl_def.get_aliasing());
    concrete->set_compiler_generated(tpl_def.is_compiler_generated());

    // 2. Populate from template (params, return type, body)
    populate_function_from_template(concrete, tpl_def, subst, val_subst, pack_subst);

    // Store template instantiation info for mangling (I…E encoding)
    concrete->set_tpl_instantiation_info(base_name, args);
    // Mark as a synthesised instantiation so codegen applies linkonce_odr + COMDAT.
    concrete->mark_instantiation();

    // Store the type substitution map so that type_reference_resolver can
    // resolve template aggregate types (e.g. Expected<R,E>) used inside the
    // function body where original template params are no longer in scope.
    concrete->set_tpl_instantiation_subst(subst);

    // 3. Register in the instantiation cache
    ti->instantiations[key] = concrete;

    return concrete;
}

// ═══════════════════════════════════════════════════════════════════════════
// Post-instantiation symbol resolution for method bodies
// ═══════════════════════════════════════════════════════════════════════════

// Walk an expression tree and resolve unresolved symbol_expression nodes
// by climbing the element parent chain (block → function → aggregate).
// This mimics what symbol_resolver::resolve_symbol does for simple names.
static void resolve_symbols_in_expr(const std::shared_ptr<expression>& expr) {
    if (!expr) return;

    if (auto sym = std::dynamic_pointer_cast<symbol_expression>(expr)) {
        if (!sym->is_resolved() && sym->get_name().size() == 1
            && !sym->get_name().has_root_prefix()) {
            const std::string& var_name = sym->get_name().front();
            // Walk up the element parent chain
            for (auto cur = sym->parent<element>(); cur;
                 cur = cur->parent<element>()) {
                // Check variable holders (block locals, aggregate members)
                if (auto* vh = dynamic_cast<variable_holder*>(cur.get())) {
                    if (auto var = vh->get_variable(var_name)) {
                        sym->set_target(var);
                        break;
                    }
                }
                // Check function parameters
                if (auto blk = std::dynamic_pointer_cast<block>(cur)) {
                    if (auto fn = blk->get_direct_function()) {
                        if (auto param = fn->get_parameter(var_name)) {
                            sym->set_target(
                                std::const_pointer_cast<parameter>(param));
                            break;
                        }
                    }
                }
                // Also check inherited members when reaching an aggregate
                if (auto agg = std::dynamic_pointer_cast<aggregate>(cur)) {
                    std::queue<std::shared_ptr<aggregate>> base_queue;
                    for (auto& bs : agg->get_bases()) {
                        if (bs.base) base_queue.push(bs.base);
                    }
                    bool found = false;
                    while (!base_queue.empty()) {
                        auto base = base_queue.front();
                        base_queue.pop();
                        if (auto var = base->get_variable(var_name)) {
                            sym->set_target(var);
                            found = true;
                            break;
                        }
                        for (auto& bs : base->get_bases()) {
                            if (bs.base) base_queue.push(bs.base);
                        }
                    }
                    if (found) break;
                }
            }
        }
    }

    // Recurse into sub-expressions
    if (auto be = std::dynamic_pointer_cast<binary_expression>(expr)) {
        resolve_symbols_in_expr(be->left());
        resolve_symbols_in_expr(be->right());
    } else if (auto ue = std::dynamic_pointer_cast<unary_expression>(expr)) {
        resolve_symbols_in_expr(
            std::const_pointer_cast<expression>(ue->sub_expr()));
    } else if (auto fie =
                   std::dynamic_pointer_cast<function_invocation_expression>(
                       expr)) {
        resolve_symbols_in_expr(
            std::const_pointer_cast<expression>(fie->callee_expr()));
        for (auto& arg : fie->arguments()) {
            resolve_symbols_in_expr(
                std::const_pointer_cast<expression>(arg));
        }
    } else if (auto cie =
                   std::dynamic_pointer_cast<constructor_invocation_expression>(
                       expr)) {
        for (auto& arg : cie->arguments()) {
            resolve_symbols_in_expr(
                std::const_pointer_cast<expression>(arg));
        }
    } else if (auto dsie =
                   std::dynamic_pointer_cast<designated_struct_init_expression>(
                       expr)) {
        for (auto& mi : dsie->members_mutable()) {
            if (mi.value) resolve_symbols_in_expr(mi.value);
            for (auto& a : mi.args) resolve_symbols_in_expr(a);
        }
    } else if (auto tce =
                   std::dynamic_pointer_cast<temporary_construction_expression>(
                       expr)) {
        for (auto& arg : tce->arguments()) {
            resolve_symbols_in_expr(
                std::const_pointer_cast<expression>(arg));
        }
    } else if (auto ne = std::dynamic_pointer_cast<new_expression>(expr)) {
        for (auto& arg : ne->arguments()) {
            resolve_symbols_in_expr(
                std::const_pointer_cast<expression>(arg));
        }
        resolve_symbols_in_expr(
            std::const_pointer_cast<expression>(ne->array_size_expr()));
    } else if (auto de = std::dynamic_pointer_cast<delete_expression>(expr)) {
        resolve_symbols_in_expr(
            std::const_pointer_cast<expression>(de->sub_expr()));
    } else if (auto ce = std::dynamic_pointer_cast<cast_expression>(expr)) {
        resolve_symbols_in_expr(
            std::const_pointer_cast<expression>(ce->sub_expr()));
    } else if (auto aie =
                   std::dynamic_pointer_cast<array_init_expression>(expr)) {
        for (auto& elem : aie->elements()) {
            resolve_symbols_in_expr(
                std::const_pointer_cast<expression>(elem));
        }
    } else if (auto cbe =
                   std::dynamic_pointer_cast<callable_bind_expression>(expr)) {
        resolve_symbols_in_expr(
            std::const_pointer_cast<expression>(cbe->get_context()));
    } else if (auto cive =
                   std::dynamic_pointer_cast<callable_invocation_expression>(expr)) {
        resolve_symbols_in_expr(
            std::const_pointer_cast<expression>(cive->get_callee()));
        for (auto& arg : cive->arguments()) {
            resolve_symbols_in_expr(
                std::const_pointer_cast<expression>(arg));
        }
    }
}

// Walk a statement tree and resolve unresolved symbols in all expressions.
static void resolve_symbols_in_stmt(const std::shared_ptr<statement>& stmt) {
    if (!stmt) return;

    if (auto rs = std::dynamic_pointer_cast<return_statement>(stmt)) {
        if (rs->get_expression())
            resolve_symbols_in_expr(rs->get_expression());
    } else if (auto es = std::dynamic_pointer_cast<expression_statement>(stmt)) {
        if (es->get_expression())
            resolve_symbols_in_expr(es->get_expression());
    } else if (auto vs = std::dynamic_pointer_cast<variable_statement>(stmt)) {
        // Compute the fully-qualified name for the variable (equivalent to
        // symbol_resolver::visit_named_element) so that codegen can find it.
        if (vs->get_fq_name().empty() && !vs->get_short_name().empty()) {
            if (auto ancestor = vs->ancestor<named_element>()) {
                vs->assign_name(ancestor->get_name().with_back(vs->get_short_name()));
            }
        }
        if (vs->get_init_expr())
            resolve_symbols_in_expr(
                std::const_pointer_cast<expression>(vs->get_init_expr()));
    } else if (auto ies = std::dynamic_pointer_cast<if_else_statement>(stmt)) {
        if (ies->has_cond_var()) {
            resolve_symbols_in_stmt(ies->get_cond_var());
        }
        if (ies->get_test_expr())
            resolve_symbols_in_expr(
                std::const_pointer_cast<expression>(ies->get_test_expr()));
        resolve_symbols_in_stmt(ies->get_then_stmt());
        resolve_symbols_in_stmt(ies->get_else_stmt());
    } else if (auto ws = std::dynamic_pointer_cast<while_statement>(stmt)) {
        if (ws->get_test_expr())
            resolve_symbols_in_expr(
                std::const_pointer_cast<expression>(ws->get_test_expr()));
        resolve_symbols_in_stmt(ws->get_nested_stmt());
    } else if (auto fs = std::dynamic_pointer_cast<for_statement>(stmt)) {
        if (fs->get_decl_stmt())
            resolve_symbols_in_stmt(fs->get_decl_stmt());
        if (fs->get_test_expr())
            resolve_symbols_in_expr(
                std::const_pointer_cast<expression>(fs->get_test_expr()));
        if (fs->get_step_expr())
            resolve_symbols_in_expr(
                std::const_pointer_cast<expression>(fs->get_step_expr()));
        resolve_symbols_in_stmt(fs->get_nested_stmt());
    } else if (auto ts = std::dynamic_pointer_cast<throw_statement>(stmt)) {
        if (ts->get_expression())
            resolve_symbols_in_expr(ts->get_expression());
    } else if (auto tcs = std::dynamic_pointer_cast<try_catch_statement>(stmt)) {
        resolve_symbols_in_stmt(tcs->get_try_body());
        for (auto& cc : tcs->get_catch_clauses()) {
            if (!cc) continue;
            if (auto ev = cc->get_exception_var()) {
                resolve_symbols_in_stmt(std::static_pointer_cast<statement>(ev));
            }
            resolve_symbols_in_stmt(cc->get_body());
        }
        resolve_symbols_in_stmt(tcs->get_finally_body());
    } else if (auto blk = std::dynamic_pointer_cast<block>(stmt)) {
        for (auto& s : blk->get_statements()) {
            resolve_symbols_in_stmt(s);
        }
    }
}

void template_instantiator::resolve_body_symbols(
    std::shared_ptr<aggregate> concrete)
{
    std::unordered_set<aggregate*> visited;
    resolve_body_symbols_rec(concrete, visited);
}

void template_instantiator::resolve_body_symbols_rec(
    std::shared_ptr<aggregate> concrete,
    std::unordered_set<aggregate*>& visited)
{
    if (!concrete) return;
    if (!visited.insert(concrete.get()).second) return;

    // ── Assign fully-qualified names to all children (functions, nested aggregates)
    //    and their local variable statements.  The symbol_resolver normally does this,
    //    but template-instantiated aggregates are created after that pass finishes.
    auto assign_names_recursive = [](aggregate& agg) {
        for (auto& child : agg.get_children()) {
            if (auto fn = std::dynamic_pointer_cast<function>(child)) {
                // Assign FQ name to the function itself
                if (fn->get_fq_name().empty() && !fn->get_short_name().empty()) {
                    if (auto anc = fn->ancestor<named_element>()) {
                        fn->assign_name(anc->get_name().with_back(fn->get_short_name()));
                    }
                }
                // Assign FQ names to parameters
                for (auto& param : fn->parameters()) {
                    if (param && param->get_fq_name().empty() && !param->get_short_name().empty()) {
                        param->assign_name(fn->get_name().with_back(param->get_short_name()));
                    }
                }
            }
        }
    };
    assign_names_recursive(*concrete);
    for (auto& child : concrete->get_children()) {
        if (auto nested = std::dynamic_pointer_cast<aggregate>(child)) {
            assign_names_recursive(*nested);
        }
    }

    auto resolve_fn_bodies = [](aggregate& agg) {
        for (auto& child : agg.get_children()) {
            if (auto fn = std::dynamic_pointer_cast<function>(child)) {
                auto blk = fn->_block;
                if (blk) {
                    for (auto& stmt : blk->get_statements()) {
                        resolve_symbols_in_stmt(stmt);
                    }
                }
            }
        }
    };

    resolve_fn_bodies(*concrete);

    // Also resolve body symbols in nested aggregates
    for (auto& child : concrete->get_children()) {
        if (auto nested = std::dynamic_pointer_cast<aggregate>(child)) {
            resolve_fn_bodies(*nested);
        }
    }

    // Recurse into instantiated base classes. A template base reached only
    // indirectly (as a base-specifier of another template) is never itself the
    // top-level target of instantiate_aggregate, so without this its cloned
    // method bodies keep unresolved bare-name references to inherited members
    // (Bug D). Imported bases carry no cloned bodies and are skipped.
    for (auto& bs : concrete->get_bases()) {
        if (!bs.base) continue;
        if (std::dynamic_pointer_cast<imported_aggregate>(bs.base)) continue;
        if (bs.base->is_instantiation()) {
            resolve_body_symbols_rec(bs.base, visited);
        }
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// Post-instantiation: inject constructor member-initializer expressions
// ═══════════════════════════════════════════════════════════════════════════

/**
 * Recursively walk an expression tree and re-target any symbol_expression that
 * references a parameter definition to point at the matching parameter in
 * @p param_by_name (by short name).  This is needed because cloned member-init
 * arg expressions still point at the template constructor's parameters.
 */
static void retarget_param_refs(
    std::shared_ptr<expression>& expr,
    const std::unordered_map<std::string, std::shared_ptr<parameter>>& param_by_name)
{
    if (!expr) return;

    if (auto sym = std::dynamic_pointer_cast<symbol_expression>(expr)) {
        if (sym->is_variable_def()) {
            auto vd = sym->get_variable_def();
            if (auto pd = std::dynamic_pointer_cast<parameter>(vd)) {
                auto it = param_by_name.find(pd->get_short_name());
                if (it != param_by_name.end()) {
                    expr = symbol_expression::from_variable(it->second);
                }
            }
        } else if (!sym->is_resolved()) {
            // Unresolved name — try to match against a concrete param
            const auto& nm = sym->get_name();
            if (nm.size() == 1 && !nm.has_root_prefix()) {
                auto it = param_by_name.find(nm.front());
                if (it != param_by_name.end()) {
                    expr = symbol_expression::from_variable(it->second);
                }
            }
        }
        return;
    }

    // Recurse into sub-expressions
    if (auto be = std::dynamic_pointer_cast<binary_expression>(expr)) {
        retarget_param_refs(be->left(), param_by_name);
        retarget_param_refs(be->right(), param_by_name);
    } else if (auto ue = std::dynamic_pointer_cast<unary_expression>(expr)) {
        auto sub = std::const_pointer_cast<expression>(ue->sub_expr());
        retarget_param_refs(sub, param_by_name);
    } else if (auto fie = std::dynamic_pointer_cast<function_invocation_expression>(expr)) {
        for (auto& a : fie->arguments()) {
            auto mut = std::const_pointer_cast<expression>(a);
            retarget_param_refs(mut, param_by_name);
        }
    } else if (auto cie = std::dynamic_pointer_cast<constructor_invocation_expression>(expr)) {
        for (auto& a : cie->arguments()) {
            auto mut = std::const_pointer_cast<expression>(a);
            retarget_param_refs(mut, param_by_name);
        }
    }
}

// Assign a fully-qualified name and mangled name to `agg` (if not already
// set) and to all of its function/constructor children. Mirrors the
// "6c/6d" logic in resolvers_type_ref.cpp / "5c/5d" in resolvers_aggregate.cpp,
// which normally runs once for the top-level concrete aggregate returned by
// instantiate_aggregate — but bases instantiated RECURSIVELY while resolving
// that top-level aggregate's own base list (e.g. Base<int> as a base of
// Mid<int>, itself a base of Impl<int>) never go through that logic, since
// only the top-level aggregate is visited by the caller. Without this, such
// an intermediate base's compiler-generated constructor keeps an empty
// mangled name, and once something (e.g. a base-constructor-call injected by
// inject_constructor_member_inits) actually triggers code generation for it,
// declaration_generator/implementation_generator emit it as an anonymous
// LLVM function — which the JIT's object linking layer rejects as an
// "unexpected definition".
void template_instantiator::ensure_agg_names_assigned(std::shared_ptr<aggregate> agg) {
    if (!agg) return;
    if (agg->get_fq_name().empty() && !agg->get_short_name().empty()) {
        if (auto ancestor = agg->template ancestor<named_element>()) {
            agg->assign_name(ancestor->get_name().with_back(agg->get_short_name()));
        }
    }
    agg->update_mangled_name();
    for (auto& child : agg->get_children()) {
        if (auto fn = std::dynamic_pointer_cast<function>(child)) {
            if (fn->get_fq_name().empty()) {
                if (auto parent_named = fn->template parent<named_element>()) {
                    fn->assign_name(parent_named->get_name().with_back(fn->get_short_name()));
                }
            }
            if (fn->get_mangled_name().empty()) {
                fn->update_mangled_name();
            }
        }
    }
}

void template_instantiator::inject_base_subobject_fields(std::shared_ptr<aggregate> concrete) {
    if (!concrete || !concrete->has_bases()) return;

    // Insert new __base_X__/__vbptr_X__ fields right after the existing __vptr__
    // field (if the aggregate already has one) instead of unconditionally at
    // _children.begin(). This function recurses into already-instantiated base
    // aggregates (e.g. a shared virtual-base interface reached again through a
    // different derived path); if that base already materialised its own
    // __vptr__ at position 0, inserting a new field at begin() would push it
    // BEFORE __vptr__, corrupting the layout every consumer (codegen GEPs,
    // constructor vbptr repoint logic, etc.) assumes for offset 0.
    auto insert_pos = [&]() {
        auto it = concrete->_children.begin();
        for (; it != concrete->_children.end(); ++it) {
            if (auto mv = std::dynamic_pointer_cast<member_variable_definition>(*it)) {
                if (mv->get_short_name() == "__vptr__") { ++it; return it; }
            }
        }
        return concrete->_children.begin();
    };

    auto& bases_mutable = concrete->get_bases_mutable();
    for (auto it = bases_mutable.rbegin(); it != bases_mutable.rend(); ++it) {
        auto& bs = *it;
        if (!bs.base || !bs.base->get_struct_type()) continue;

        // Ensure the base itself (and its constructors/methods) carry a
        // proper FQ/mangled name before anything downstream might trigger
        // code generation for it (see ensure_agg_names_assigned above).
        ensure_agg_names_assigned(bs.base);

        if (bs.is_virtual) {
            // Name the vbptr after the resolved base aggregate's short name so it
            // matches the collector (__vbase_X__) and constructor vbptr-setup code,
            // which key off get_short_name(). For a template instantiation the raw
            // base name ("Coll<int>") and the resolved short name ("Coll__int")
            // differ, so using the raw sanitised name here would desynchronise them.
            std::string vbptr_name = "__vbptr_" + bs.base->get_short_name() + "__";
            if (!concrete->_vars.count(vbptr_name)) {
                auto vbptr_field = member_variable_definition::make_shared(concrete->shared_as<aggregate>(), vbptr_name);
                concrete->_vars.insert({vbptr_name, vbptr_field});
                concrete->_children.insert(insert_pos(), vbptr_field);
            }
        } else {
            std::string subobj_name = "__base_" + bs.sanitised_name() + "__";
            if (!concrete->_vars.count(subobj_name)) {
                auto subobj_field = member_variable_definition::make_shared(concrete->shared_as<aggregate>(), subobj_name);
                subobj_field->set_type(bs.base->get_struct_type());
                concrete->_vars.insert({subobj_name, subobj_field});
                concrete->_children.insert(insert_pos(), subobj_field);
            }
        }

        // Recurse into the base itself, so intermediate levels of a multi-level
        // template hierarchy (instantiated on-demand while resolving THIS
        // aggregate's base list) also get their own __base_X__ fields.
        inject_base_subobject_fields(bs.base);
    }
}

bool template_instantiator::ensure_virtual_base_layout_fields(std::shared_ptr<aggregate> concrete) {
    if (!concrete) return false;
    bool added = false;

    // Helper: position of the __vptr__ field in _children (so a vbptr is inserted
    // right after it, never before — __vptr__ must stay at offset 0).
    auto vptr_insert_pos = [&]() {
        auto it = concrete->_children.begin();
        for (; it != concrete->_children.end(); ++it) {
            if (auto mv = std::dynamic_pointer_cast<member_variable_definition>(*it)) {
                if (mv->get_short_name() == "__vptr__") { ++it; return it; }
            }
        }
        return concrete->_children.begin();
    };

    // 1. __vbptr_X__ for each direct virtual base that lacks one.
    for (auto& bs : concrete->get_bases()) {
        if (!bs.base || !bs.is_virtual || !bs.base->get_struct_type()) continue;
        std::string vbptr_name = "__vbptr_" + bs.base->get_short_name() + "__";
        if (concrete->_vars.count(vbptr_name)) continue;
        auto vbptr_field = member_variable_definition::make_shared(
            concrete->shared_as<aggregate>(), vbptr_name);
        concrete->_vars.insert({vbptr_name, vbptr_field});
        concrete->_children.insert(vptr_insert_pos(), vbptr_field);
        added = true;
    }

    // 2. Collector __vbase_X__ for transitively-virtual interface bases where this
    //    aggregate is the collector (mirrors gen_struct.cpp visit_aggregate).
    auto vbases = concrete->get_all_virtual_base_structs();
    for (auto& vbase : vbases) {
        if (!vbase->get_struct_type()) continue;
        std::string vbase_name = "__vbase_" + vbase->get_short_name() + "__";
        std::string vbptr_name = "__vbptr_" + vbase->get_short_name() + "__";
        if (concrete->_vars.count(vbase_name)) continue;

        std::function<bool(const aggregate&)> has_vbptr_in_vars;
        has_vbptr_in_vars = [&](const aggregate& base_st) -> bool {
            if (base_st._vars.count(vbptr_name)) return true;
            for (auto& b : base_st.get_bases()) {
                if (!b.base || b.is_virtual) continue;
                if (has_vbptr_in_vars(*b.base)) return true;
            }
            return false;
        };

        bool is_collector = false;
        for (auto& bs : concrete->get_bases()) {
            if (!bs.base) continue;
            if (bs.is_virtual) {
                if (bs.base.get() == vbase.get() && vbase->is_interface()) { is_collector = true; break; }
                if (bs.base->_vars.count(vbptr_name)) { is_collector = true; break; }
            } else if (has_vbptr_in_vars(*bs.base)) {
                is_collector = true; break;
            }
        }
        if (!is_collector) continue;

        auto vbase_field = member_variable_definition::make_shared(
            concrete->shared_as<aggregate>(), vbase_name);
        vbase_field->set_type(vbase->get_struct_type());
        concrete->_vars.insert({vbase_name, vbase_field});
        concrete->_children.push_back(vbase_field);
        added = true;
    }

    return added;
}


void template_instantiator::inject_constructor_member_inits(std::shared_ptr<aggregate> concrete) {
    if (!concrete) return;

    // Normalize a base-class raw_name to the simple name used in a constructor
    // member-initializer (see the identical helper in gen/gen_constructor.cpp — kept
    // local here since that one lives in an anonymous namespace under k::model::gen).
    auto base_init_simple_name = [](const std::string& raw) -> std::string {
        std::string r = raw;
        if (auto lt = r.find('<'); lt != std::string::npos) {
            r = r.substr(0, lt);
        }
        if (auto cc = r.rfind("::"); cc != std::string::npos) {
            r = r.substr(cc + 2);
        }
        return r;
    };

    auto inject_for_aggregate = [&base_init_simple_name](std::shared_ptr<aggregate> agg) {
        for (auto& ctor : agg->constructors()) {
            // Note: compiler-generated constructors (default or copy) still need base
            // constructor calls injected below — symbol_resolver::visit_constructor
            // does the same for non-template classes regardless of is_compiler_generated().
            // Only deleted constructors (no meaningful body) and the compiler-generated
            // COPY constructor (handled entirely at IR level, see
            // type_reference_resolver::visit_constructor) must be skipped.
            if (!ctor || ctor->is_deleted()) continue;
            if (ctor->is_copy_constructor() && ctor->is_compiler_generated()) continue;
            // Idempotency guard: see constructor::_base_inits_injected's doc comment.
            if (ctor->are_base_inits_injected()) continue;
            ctor->set_base_inits_injected(true);

            auto blck = ctor->get_block();
            if (!blck) continue;

            // Build a map from old parameter names to new concrete parameters for re-targeting.
            std::unordered_map<std::string, std::shared_ptr<parameter>> param_by_name;
            for (auto& p : ctor->parameters()) {
                if (p == ctor->get_this_parameter()) continue;
                param_by_name[p->get_short_name()] = p;
            }

            // ── Step 0: inject base constructor calls (in base declaration order) ──
            // Mirrors symbol_resolver::visit_constructor's Step 1/1b. Needed because
            // template-instantiated aggregates (whether synthesised during the
            // aggregate_type_resolver pass or on-demand during type_reference_resolver)
            // are created AFTER symbol_resolver::visit_constructor has already run on the
            // original (generic, unresolved) template — so base ctor-call statements were
            // never injected for these fresh, concrete constructor bodies. Without this,
            // type_reference_resolver::visit_constructor's later fallback member-init pass
            // miscounts the expected statement offset (it assumes base/vbase ctor calls
            // were already injected), causing an out-of-range std::vector::insert.
            size_t insert_idx = 0;
            if (agg->has_bases()) {
                std::unordered_map<std::string, const constructor::member_init_spec*> base_init_by_name;
                for (auto& mi : ctor->member_inits()) {
                    if (mi.is_base_init) base_init_by_name[base_init_simple_name(mi.member_name)] = &mi;
                }
                for (auto& bs : agg->get_bases()) {
                    if (!bs.base) continue;
                    std::string subobj_name = bs.is_virtual
                        ? ("__vbase_" + bs.sanitised_name() + "__")
                        : ("__base_" + bs.sanitised_name() + "__");
                    auto subobj_var_it = agg->variables().find(subobj_name);
                    if (subobj_var_it == agg->variables().end()) continue;
                    auto subobj_var = std::dynamic_pointer_cast<member_variable_definition>(subobj_var_it->second);
                    if (!subobj_var) continue;

                    std::vector<std::shared_ptr<expression>> args;
                    auto it = base_init_by_name.find(base_init_simple_name(bs.raw_name));
                    if (it != base_init_by_name.end()) {
                        for (auto& arg : it->second->args) {
                            auto cloned = arg->clone();
                            retarget_param_refs(cloned, param_by_name);
                            args.push_back(cloned);
                        }
                    }
                    auto init_expr = constructor_invocation_expression::make_shared(subobj_var, args);
                    auto stmt = std::make_shared<expression_statement>(blck);
                    stmt->set_expression(init_expr);
                    auto pos = blck->begin();
                    std::advance(pos, insert_idx);
                    blck->insert_statement(pos, stmt);
                    ++insert_idx;
                }

                // Step 0b: inject transitively-collected virtual base constructor calls
                // (e.g. D : B, C where B, C each declare virtual base A — A is not in D's
                // direct base list but D must still construct it once).
                for (auto& vbase : agg->get_all_virtual_base_structs()) {
                    std::string vbase_name = "__vbase_" + vbase->get_short_name() + "__";
                    // Skip if already injected as a direct virtual base above.
                    bool already_direct = false;
                    for (auto& bs : agg->get_bases()) {
                        if (bs.base && bs.is_virtual && bs.raw_name == vbase->get_short_name()) {
                            already_direct = true; break;
                        }
                    }
                    if (already_direct) continue;
                    auto vbase_var_it = agg->variables().find(vbase_name);
                    if (vbase_var_it == agg->variables().end()) continue;
                    auto vbase_var = std::dynamic_pointer_cast<member_variable_definition>(vbase_var_it->second);
                    if (!vbase_var) continue;

                    std::vector<std::shared_ptr<expression>> args;
                    auto it = base_init_by_name.find(base_init_simple_name(vbase->get_short_name()));
                    if (it != base_init_by_name.end()) {
                        for (auto& arg : it->second->args) {
                            auto cloned = arg->clone();
                            retarget_param_refs(cloned, param_by_name);
                            args.push_back(cloned);
                        }
                    }
                    auto init_expr = constructor_invocation_expression::make_shared(vbase_var, args);
                    auto stmt = std::make_shared<expression_statement>(blck);
                    stmt->set_expression(init_expr);
                    auto pos = blck->begin();
                    std::advance(pos, insert_idx);
                    blck->insert_statement(pos, stmt);
                    ++insert_idx;
                }
            }

            if (ctor->member_inits().empty()) continue;

            // Build a lookup map from member name to mem_init_spec
            std::unordered_map<std::string, const constructor::member_init_spec*> init_by_name;
            for (auto& mi : ctor->member_inits()) {
                if (!mi.is_base_init) init_by_name[mi.member_name] = &mi;
            }

            // Insert member-init statements after the base ctor calls injected above, in
            // member declaration order (same logic as symbol_resolver::visit_constructor step 2).
            for (auto& var_entry : agg->variables()) {
                if (auto var = std::dynamic_pointer_cast<member_variable_definition>(var_entry.second)) {
                    // Skip synthetic fields
                    if (var->get_short_name() == "__parent__") continue;
                    if (var->get_short_name().rfind("__base_", 0) == 0) continue;
                    if (var->get_short_name().rfind("__vbptr_", 0) == 0) continue;
                    if (var->get_short_name().rfind("__vbase_", 0) == 0) continue;
                    if (var->get_short_name().rfind("__vptr", 0) == 0) continue;

                    auto it = init_by_name.find(var->get_short_name());
                    if (it == init_by_name.end()) continue;
                    const auto& mi = *it->second;

                    std::vector<std::shared_ptr<expression>> args;
                    args.reserve(mi.args.size());
                    for (auto& arg : mi.args) {
                        auto cloned = arg->clone();
                        retarget_param_refs(cloned, param_by_name);
                        args.push_back(cloned);
                    }
                    auto init_expr = constructor_invocation_expression::make_shared(var, args);
                    auto stmt = std::make_shared<expression_statement>(blck);
                    stmt->set_expression(init_expr);
                    auto pos = blck->begin();
                    std::advance(pos, insert_idx);
                    blck->insert_statement(pos, stmt);
                    ++insert_idx;
                }
            }
        }
    };

    inject_for_aggregate(concrete);

    // Also inject for nested aggregates
    for (auto& child : concrete->get_children()) {
        if (auto nested = std::dynamic_pointer_cast<aggregate>(child)) {
            inject_for_aggregate(nested);
        }
    }

    // Recurse into resolved bases (and their own bases, transitively), mirroring
    // inject_base_subobject_fields()'s recursion. Without this, an intermediate
    // level of a multi-level template hierarchy that is only ever reached as a
    // BASE (e.g. Map<K,V> under MutableSortedMap<K,V> under TreeMap<K,V>, never
    // directly named as a type elsewhere) never gets ITS OWN constructor's
    // base-init statements injected — its "__base_X__" subobject fields exist
    // (added by inject_base_subobject_fields, called before this function at
    // every top-level call site) but nothing ever calls
    // constructor_invocation_expression for them, silently leaving that
    // intermediate level's own bases uninitialised (e.g. never invoking
    // Sequence<Entry<K,V>>'s constructor from within Map<K,V>'s constructor).
    // The per-constructor `are_base_inits_injected()` guard makes this safe to
    // call repeatedly for a base reached via multiple derived paths (diamonds).
    for (auto& bs : concrete->get_bases()) {
        if (bs.base) {
            inject_constructor_member_inits(bs.base);
        }
    }
}

} // namespace k::model


// ═══════════════════════════════════════════════════════════════════════════
// instantiate_union
// ═══════════════════════════════════════════════════════════════════════════

namespace k::model {

std::shared_ptr<union_type_def> template_instantiator::instantiate_union(
    union_type_def& tpl_def,
    const std::vector<template_argument>& args,
    std::shared_ptr<ns> parent_ns,
    k::model::unit& unit,
    std::shared_ptr<context> ctx,
    k::log::logger& logger)
{
    auto* ti = tpl_def.get_tpl_info();
    if (!ti) return nullptr;

    // Check instantiation cache
    std::string key = build_instantiation_key(args);
    auto it = ti->instantiations.find(key);
    if (it != ti->instantiations.end()) {
        if (auto* un_ptr = std::get_if<std::shared_ptr<union_type_def>>(&it->second)) {
            return *un_ptr;
        }
    }

    // Build the instantiated name
    std::string base_name = tpl_def.get_short_name();
    std::string inst_name = build_instantiated_name(base_name, args);

    // Build type substitution map
    auto subst = build_substitution_map(*ti, args);
    auto val_subst = build_value_substitution_map(*ti, args);

    // Create a new concrete union in the parent namespace
    auto concrete = parent_ns->define_union(inst_name);
    if (!concrete) return nullptr;

    concrete->set_visibility(tpl_def.get_visibility());

    // Copy alternatives with type substitution
    for (const auto& alt : tpl_def.alternatives()) {
        std::string raw = alt.raw_type_name;
        // Apply type substitution to the raw_type_name (for template params like "T")
        auto subst_it = subst.find(raw);
        if (subst_it != subst.end() && subst_it->second) {
            raw = subst_it->second->to_string();
        }
        concrete->add_alternative(alt.name, raw, alt.is_const);

        // Substitute the resolved type as well
        auto& new_alt = concrete->alternatives_mutable().back();
        if (alt.resolved_type) {
            new_alt.resolved_type = substitute_type(alt.resolved_type, subst);
        } else if (!raw.empty()) {
            // Try to get model type from substitution map or context
            auto from_ctx = ctx->from_string(raw);
            if (from_ctx) new_alt.resolved_type = from_ctx;
        }
    }

    // Store template instantiation info for mangling
    concrete->set_tpl_instantiation_info(base_name, args);

    // Cache the instantiation
    ti->instantiations[key] = concrete;

    // Assign FQ name
    if (concrete->get_fq_name().empty() && !concrete->get_short_name().empty()) {
        if (auto ancestor = concrete->template ancestor<named_element>()) {
            concrete->assign_name(ancestor->get_name().with_back(concrete->get_short_name()));
        }
    }
    concrete->update_mangled_name();

    return concrete;
}

} // namespace k::model
