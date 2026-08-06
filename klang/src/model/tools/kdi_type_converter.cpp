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

#include "../tools/kdi_type_converter.hpp"

#include "../model.hpp"
#include "../imported.hpp"
#include "../context.hpp"
#include "../type.hpp"

#include <kdi.hpp>

#include <stdexcept>

namespace k::model {

// ─────────────────────────────────────────────────────────────────────────────
// Forward
// ─────────────────────────────────────────────────────────────────────────────

static std::shared_ptr<type>
convert(const kdi::kdi_type& kdi_t, unit& owner, std::shared_ptr<context> ctx);

// ─────────────────────────────────────────────────────────────────────────────
// Primitive helpers
// ─────────────────────────────────────────────────────────────────────────────

static std::shared_ptr<type>
convert_primitive(const kdi::kdi_int_type& t, std::shared_ptr<context> ctx)
{
    // Map KDI int_type → primitive_type via context::from_type() to avoid
    // string-lookup mismatches (e.g. "ubyte" is not in context::from_string()).
    if (t.is_signed) {
        switch (t.bits) {
            case  8: return ctx->from_type(primitive_type::BYTE);
            case 16: return ctx->from_type(primitive_type::SHORT);
            case 32: return ctx->from_type(primitive_type::INT);
            case 64: return ctx->from_type(primitive_type::LONG);
            case 128: return ctx->from_type(primitive_type::LONG_LONG);
        }
    } else {
        switch (t.bits) {
            case  8: return ctx->from_type(primitive_type::UNSIGNED_BYTE);
            case 16: return ctx->from_type(primitive_type::UNSIGNED_SHORT);
            case 32: return ctx->from_type(primitive_type::UNSIGNED_INT);
            case 64: return ctx->from_type(primitive_type::UNSIGNED_LONG);
            case 128: return ctx->from_type(primitive_type::UNSIGNED_LONG_LONG);
        }
    }
    return nullptr; // unsupported width
}

static std::shared_ptr<type>
convert_primitive(const kdi::kdi_float_type& t, std::shared_ptr<context> ctx)
{
    switch (t.bits) {
        case 32: return ctx->from_string("float");
        case 64: return ctx->from_string("double");
    }
    return nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// Alias reference
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Resolve a reference to an imported alias / typedef.
 *
 * The alias declarations of a namespace are materialised before its functions
 * and variables, so the alias_definition is already in place — and, because its
 * target type comes fully resolved from the KDI, already finalised.
 */
static std::shared_ptr<type>
convert_alias_ref(const kdi::kdi_alias_ref& ref, unit& owner,
                  std::shared_ptr<context> ctx)
{
    std::vector<std::string> parts;
    const std::string& fq = ref.fq_name;
    std::size_t start = 0;
    while (true) {
        auto pos = fq.find("::", start);
        if (pos == std::string::npos) { parts.push_back(fq.substr(start)); break; }
        parts.push_back(fq.substr(start, pos - start));
        start = pos + 2;
    }
    if (parts.empty()) return nullptr;

    auto nspc = owner.get_root_namespace();
    for (std::size_t i = 0; i + 1 < parts.size() && nspc; ++i) {
        nspc = nspc->get_child_namespace(parts[i]);
    }
    if (!nspc) return nullptr;

    auto adef = nspc->get_alias(parts.back());
    if (!adef) return nullptr;
    return adef->get_declared_type();
}

// ─────────────────────────────────────────────────────────────────────────────
// Aggregate reference
// ─────────────────────────────────────────────────────────────────────────────

static std::shared_ptr<type>
convert_aggregate_ref(const kdi::kdi_aggregate_ref& ref, unit& owner,
                      std::shared_ptr<context> ctx)
{
    // Parse fq_name into a k::name.  Separator is "::".
    std::vector<std::string> parts;
    const std::string& fq = ref.fq_name;
    std::size_t start = 0;
    while (true) {
        auto pos = fq.find("::", start);
        if (pos == std::string::npos) {
            parts.push_back(fq.substr(start));
            break;
        }
        parts.push_back(fq.substr(start, pos - start));
        start = pos + 2;
    }
    k::name kname(false, std::move(parts));

    // Check if this name refers to an already-materialised union type.
    // Unions are stored in the namespace hierarchy; navigate to the parent
    // namespace and look for a union with the leaf name.
    {
        auto target_ns = owner.get_root_namespace();
        for (size_t i = 0; i + 1 < kname.size(); ++i) {
            target_ns = target_ns->get_child_namespace(kname[i]);
        }
        if (auto udef = target_ns->get_union(kname.back())) {
            if (auto st = udef->get_struct_type()) {
                return st;
            }
        }
    }

    // Delegate to unit — creates or retrieves the imported_aggregate node.
    auto agg = owner.get_or_create_imported_aggregate(kname, ctx);
    if (!agg) return nullptr;
    return agg->get_struct_type();
}

// ─────────────────────────────────────────────────────────────────────────────
// Main dispatcher
// ─────────────────────────────────────────────────────────────────────────────

static std::shared_ptr<type>
convert(const kdi::kdi_type& kdi_t, unit& owner, std::shared_ptr<context> ctx)
{
    return std::visit([&](const auto& v) -> std::shared_ptr<type> {
        using T = std::decay_t<decltype(v)>;

        if constexpr (std::is_same_v<T, kdi::kdi_void_type>) {
            return nullptr; // void is represented as nullptr return type in K
        }
        else if constexpr (std::is_same_v<T, kdi::kdi_bool_type>) {
            return ctx->from_string("bool");
        }
        else if constexpr (std::is_same_v<T, kdi::kdi_char_type>) {
            return ctx->from_string("char");
        }
        else if constexpr (std::is_same_v<T, kdi::kdi_int_type>) {
            return convert_primitive(v, ctx);
        }
        else if constexpr (std::is_same_v<T, kdi::kdi_float_type>) {
            return convert_primitive(v, ctx);
        }
        else if constexpr (std::is_same_v<T, kdi::kdi_ref_type>) {
            if (!v.inner) return nullptr;
            auto inner = convert(*v.inner, owner, ctx);
            if (!inner) return nullptr;
            return inner->get_reference();
        }
        else if constexpr (std::is_same_v<T, kdi::kdi_ptr_type>) {
            if (!v.inner) return nullptr;
            auto inner = convert(*v.inner, owner, ctx);
            if (!inner) return nullptr;
            return inner->get_pointer();
        }
        else if constexpr (std::is_same_v<T, kdi::kdi_link_type>) {
            // link (~T) — represented as pointer in K model for now
            if (!v.inner) return nullptr;
            auto inner = convert(*v.inner, owner, ctx);
            if (!inner) return nullptr;
            return inner->get_pointer();
        }
        else if constexpr (std::is_same_v<T, kdi::kdi_view_type>) {
            if (!v.inner) return nullptr;
            auto inner = convert(*v.inner, owner, ctx);
            if (!inner) return nullptr;
            return inner->get_view();
        }
        else if constexpr (std::is_same_v<T, kdi::kdi_owner_type>) {
            if (!v.inner) return nullptr;
            auto inner = convert(*v.inner, owner, ctx);
            if (!inner) return nullptr;
            return inner->get_owner();
        }
        else if constexpr (std::is_same_v<T, kdi::kdi_drain_type>) {
            if (!v.inner) return nullptr;
            auto inner = convert(*v.inner, owner, ctx);
            if (!inner) return nullptr;
            return inner->get_drain();
        }
        else if constexpr (std::is_same_v<T, kdi::kdi_const_type>) {
            if (!v.inner) return nullptr;
            auto inner = convert(*v.inner, owner, ctx);
            if (!inner) return nullptr;
            return inner->get_const();
        }
        else if constexpr (std::is_same_v<T, kdi::kdi_array_type>) {
            if (!v.elem) return nullptr;
            auto elem = convert(*v.elem, owner, ctx);
            if (!elem) return nullptr;
            return elem->get_array();
        }
        else if constexpr (std::is_same_v<T, kdi::kdi_sized_array_type>) {
            if (!v.elem) return nullptr;
            auto elem = convert(*v.elem, owner, ctx);
            if (!elem) return nullptr;
            return elem->get_array(static_cast<unsigned long>(v.size));
        }
        else if constexpr (std::is_same_v<T, kdi::kdi_fn_ref_type>) {
            // Function reference — not yet supported in model, return nullptr
            return nullptr;
        }
        else if constexpr (std::is_same_v<T, kdi::kdi_aggregate_ref>) {
            return convert_aggregate_ref(v, owner, ctx);
        }
        else if constexpr (std::is_same_v<T, kdi::kdi_enum_ref>) {
            // Parse fq_name into a k::name
            std::vector<std::string> parts;
            const std::string& fq = v.fq_name;
            std::size_t start = 0;
            while (true) {
                auto pos = fq.find("::", start);
                if (pos == std::string::npos) {
                    parts.push_back(fq.substr(start));
                    break;
                }
                parts.push_back(fq.substr(start, pos - start));
                start = pos + 2;
            }
            k::name kname(false, std::move(parts));
            auto en = owner.get_or_create_imported_enum(kname, ctx);
            if (!en) return nullptr;
            return en->get_enum_type();
        }
        else if constexpr (std::is_same_v<T, kdi::kdi_alias_ref>) {
            return convert_alias_ref(v, owner, ctx);
        }
        else if constexpr (std::is_same_v<T, kdi::kdi_template_param_ref>) {
            return ctx->from_string(v.name);
        }
        else {
            return nullptr;
        }
    }, kdi_t.value);
}

// ─────────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────────

std::shared_ptr<type>
kdi_type_to_model_type(const kdi::kdi_type& kdi_t,
                       unit& owner,
                       std::shared_ptr<context> ctx)
{
    return convert(kdi_t, owner, ctx);
}

} // namespace k::model

