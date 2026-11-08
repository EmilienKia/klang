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
#include "../../parse/ast.hpp"

#include <kdi.hpp>

#include <stdexcept>
#include <string_view>

namespace k::model {

// ─────────────────────────────────────────────────────────────────────────────
// Forward
// ─────────────────────────────────────────────────────────────────────────────

static std::shared_ptr<type>
convert(const kdi::kdi_type& kdi_t, unit& owner, std::shared_ptr<context> ctx,
        kdi_type_conversion mode);

static bool needs_pinning(const std::shared_ptr<type>& t)
{
    if (t->is_pinned() || std::dynamic_pointer_cast<unresolved_callable_type>(t)) return true;
    auto unresolved = std::dynamic_pointer_cast<unresolved_type>(t);
    return unresolved && unresolved->has_model_only_template_args();
}

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
        for (size_t i = 0; i + 1 < kname.size() && target_ns; ++i) {
            target_ns = target_ns->get_child_namespace(kname[i]);
        }
        if (target_ns) {
            if (auto udef = target_ns->get_union(kname.back())) {
                if (auto st = udef->get_struct_type()) {
                    return st;
                }
            }
        }
    }

    if (auto res = ctx->from_string(kname.to_string())) {
        if (auto st = std::dynamic_pointer_cast<struct_type>(res)) {
            return st;
        }
    }

    // Delegate to unit — creates or retrieves the imported_aggregate node.
    auto agg = owner.get_or_create_imported_aggregate(kname, ctx);
    if (!agg) return nullptr;
    return agg->get_struct_type();
}

// ─────────────────────────────────────────────────────────────────────────────
// Callable
// ─────────────────────────────────────────────────────────────────────────────

/** Map a KDI callable addresser back onto the model enumeration. */
static callable_type::addresser model_addresser(kdi::kdi_callable_addresser a)
{
    switch (a) {
        case kdi::kdi_callable_addresser::none:  return callable_type::addresser::none;
        case kdi::kdi_callable_addresser::view:  return callable_type::addresser::view;
        case kdi::kdi_callable_addresser::link:  return callable_type::addresser::link;
        case kdi::kdi_callable_addresser::ref:   return callable_type::addresser::reference;
        case kdi::kdi_callable_addresser::owner: return callable_type::addresser::owner;
        case kdi::kdi_callable_addresser::ptr:   break;
    }
    return callable_type::addresser::pointer;
}

/**
 * Rebuild a callable_type (or an unbound member_function_reference_type) from
 * its KDI description.
 *
 * A void return is encoded as kdi_void_type, which convert() maps to nullptr —
 * exactly the model representation of "returns nothing", so no special case is
 * needed. A component that cannot be converted makes the whole callable
 * unconvertible: an incomplete signature would silently bind the wrong
 * overload at the use site.
 */
static std::shared_ptr<type>
convert_callable(const kdi::kdi_callable_type& c, unit& owner,
                 std::shared_ptr<context> ctx, kdi_type_conversion mode)
{
    callable_type_builder builder(ctx);
    builder.addresser(model_addresser(c.addresser));
    std::shared_ptr<type> result_type;
    std::vector<std::shared_ptr<type>> parameters;
    bool unresolved = false;

    if (c.ret && !std::holds_alternative<kdi::kdi_void_type>(c.ret->value)) {
        auto ret = convert(*c.ret, owner, ctx, mode);
        if (!ret) return nullptr;
        builder.return_type(ret);
        result_type = ret;
        unresolved |= mode == kdi_type_conversion::ordinary
            ? type::contains_unresolved(ret) : !type::is_resolved(ret);
    }

    for (const auto& p : c.params) {
        if (!p) return nullptr;
        auto pt = convert(*p, owner, ctx, mode);
        if (!pt) return nullptr;
        builder.append_parameter_type(pt);
        parameters.push_back(pt);
        unresolved |= mode == kdi_type_conversion::ordinary
            ? type::contains_unresolved(pt) : !type::is_resolved(pt);
    }

    std::vector<std::shared_ptr<type>> throws;
    for (const auto& th : c.throws) {
        if (!th) return nullptr;
        auto tt = convert(*th, owner, ctx, mode);
        if (!tt) return nullptr;
        unresolved |= mode == kdi_type_conversion::ordinary
            ? type::contains_unresolved(tt) : !type::is_resolved(tt);
        throws.push_back(tt);
    }
    builder.throws(throws);

    std::vector<std::string> parts;
    if (!c.member_of.empty()) {
        std::size_t start = 0;
        while (true) {
            auto pos = c.member_of.find("::", start);
            if (pos == std::string::npos) { parts.push_back(c.member_of.substr(start)); break; }
            parts.push_back(c.member_of.substr(start, pos - start));
            start = pos + 2;
        }
    }
    if (unresolved) {
        return unresolved_callable_type::make_shared(
            k::name{false, parts}, model_addresser(c.addresser), parameters, result_type, throws);
    }
    if (!parts.empty()) {
        auto agg = owner.get_or_create_imported_aggregate(k::name{false, std::move(parts)}, ctx);
        if (!agg) return nullptr;
        builder.member_of(std::static_pointer_cast<aggregate>(agg));
    }

    return builder.build();
}

// ─────────────────────────────────────────────────────────────────────────────
// Main dispatcher
// ─────────────────────────────────────────────────────────────────────────────

static std::shared_ptr<type>
convert(const kdi::kdi_type& kdi_t, unit& owner, std::shared_ptr<context> ctx,
        kdi_type_conversion mode)
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
            auto inner = convert(*v.inner, owner, ctx, mode);
            if (!inner) return nullptr;
            return mode == kdi_type_conversion::ordinary && !needs_pinning(inner) ? inner->get_reference()
                                                         : type::make_pinned_reference(inner);
        }
        else if constexpr (std::is_same_v<T, kdi::kdi_ptr_type>) {
            if (!v.inner) return nullptr;
            auto inner = convert(*v.inner, owner, ctx, mode);
            if (!inner) return nullptr;
            return mode == kdi_type_conversion::ordinary && !needs_pinning(inner) ? inner->get_pointer()
                                                         : type::make_pinned_pointer(inner);
        }
        else if constexpr (std::is_same_v<T, kdi::kdi_link_type>) {
            // link (~T) — represented as pointer in K model for now
            if (!v.inner) return nullptr;
            auto inner = convert(*v.inner, owner, ctx, mode);
            if (!inner) return nullptr;
            if (mode == kdi_type_conversion::ordinary)
                return needs_pinning(inner) ? type::make_pinned_pointer(inner) : inner->get_pointer();
            return type::make_pinned_link(inner);
        }
        else if constexpr (std::is_same_v<T, kdi::kdi_view_type>) {
            if (!v.inner) return nullptr;
            auto inner = convert(*v.inner, owner, ctx, mode);
            if (!inner) return nullptr;
            return mode == kdi_type_conversion::ordinary && !needs_pinning(inner) ? inner->get_view()
                                                         : type::make_pinned_view(inner);
        }
        else if constexpr (std::is_same_v<T, kdi::kdi_owner_type>) {
            if (!v.inner) return nullptr;
            auto inner = convert(*v.inner, owner, ctx, mode);
            if (!inner) return nullptr;
            return mode == kdi_type_conversion::ordinary && !needs_pinning(inner) ? inner->get_owner()
                                                         : type::make_pinned_owner(inner);
        }
        else if constexpr (std::is_same_v<T, kdi::kdi_drain_type>) {
            if (!v.inner) return nullptr;
            auto inner = convert(*v.inner, owner, ctx, mode);
            if (!inner) return nullptr;
            return mode == kdi_type_conversion::ordinary && !needs_pinning(inner) ? inner->get_drain()
                                                         : type::make_pinned_drain(inner);
        }
        else if constexpr (std::is_same_v<T, kdi::kdi_const_type>) {
            if (!v.inner) return nullptr;
            auto inner = convert(*v.inner, owner, ctx, mode);
            if (!inner) return nullptr;
            return mode == kdi_type_conversion::ordinary && !needs_pinning(inner) ? inner->get_const()
                                                         : type::make_pinned_const(inner);
        }
        else if constexpr (std::is_same_v<T, kdi::kdi_array_type>) {
            if (!v.elem) return nullptr;
            auto elem = convert(*v.elem, owner, ctx, mode);
            if (!elem) return nullptr;
            return mode == kdi_type_conversion::ordinary && !needs_pinning(elem) ? elem->get_array()
                                                         : type::make_pinned_array(elem);
        }
        else if constexpr (std::is_same_v<T, kdi::kdi_sized_array_type>) {
            if (!v.elem) return nullptr;
            auto elem = convert(*v.elem, owner, ctx, mode);
            if (!elem) return nullptr;
            return mode == kdi_type_conversion::ordinary && !needs_pinning(elem) ? elem->get_array(static_cast<unsigned long>(v.size))
                : type::make_pinned_sized_array(elem, static_cast<unsigned long>(v.size));
        }
        else if constexpr (std::is_same_v<T, kdi::kdi_callable_type>) {
            return convert_callable(v, owner, ctx, mode);
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
            if (mode == kdi_type_conversion::ordinary) return ctx->from_string(v.name);
            if (mode == kdi_type_conversion::concrete_contract || v.name.empty()) return nullptr;
            k::parse::ast::qualified_identifier name{
                std::nullopt, std::vector<k::lex::identifier>{k::lex::identifier(v.name)}};
            k::parse::ast::identified_type_specifier specifier{name};
            auto placeholder = std::dynamic_pointer_cast<unresolved_type>(ctx->from_type_specifier(specifier));
            if (placeholder) placeholder->set_template_param_placeholder();
            return placeholder;
        }
        else if constexpr (std::is_same_v<T, kdi::kdi_erased_param_type>) {
            auto byte_type = ctx->from_type(primitive_type::BYTE);
            if (!byte_type) return nullptr;
            return byte_type->get_erased_param();
        }
        else if constexpr (std::is_same_v<T, kdi::kdi_generic_ref_type>) {
            auto name = k::name::from(v.name);
            std::vector<std::shared_ptr<type>> args;
            args.reserve(v.args.size());
            for (const auto& arg : v.args) {
                if (!arg) return nullptr;
                if (std::holds_alternative<kdi::kdi_void_type>(arg->value)) {
                    if (mode == kdi_type_conversion::concrete_contract) return nullptr;
                    args.push_back(nullptr);
                    continue;
                }
                auto converted = convert(*arg, owner, ctx,
                    mode == kdi_type_conversion::ordinary ? kdi_type_conversion::template_signature : mode);
                if (!converted) return nullptr;
                args.push_back(std::move(converted));
            }
            return unresolved_type::make_model_template(name, std::move(args));
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
                       std::shared_ptr<context> ctx,
                       kdi_type_conversion mode)
{
    return convert(kdi_t, owner, ctx, mode);
}

} // namespace k::model

