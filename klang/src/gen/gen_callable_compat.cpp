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

/**
 * Callable compatibility rules (phase B.7).
 *
 * A callable is invoked through a raw code address: there is no place to insert a
 * pointer adjustment, a sign extension or a floating-point conversion between the
 * caller and the callee. Compatibility is therefore *strict*:
 *
 *   - identity is decided **nominally first** (`type::are_equal`), so a strong alias
 *     (`typedef`) never silently collapses into the type it renames;
 *   - aggregate derivation is the only accepted relaxation, and only when the base
 *     sub-object sits at byte offset 0 (no `this` adjustment needed);
 *   - primitive widening/narrowing is *never* accepted, in either position;
 *   - the addresser (`*`, `?`, `+`, `&`, `!`, `#`) must match exactly, because the
 *     addresser is what the ABI actually passes;
 *   - the source `throws` set must be a subset of the destination's.
 *
 * See `doc/spec/language/functions/callables.md` §9.
 */

#include "gen_callable_helpers.hpp"

#include <algorithm>
#include <unordered_set>

#include <fmt/format.h>

namespace k::model::gen {

namespace {

/** Addresser categories that a callable prototype component may carry. */
enum class indir_kind { value, pointer, link, view, reference, owner, drain };

indir_kind kind_of(const std::shared_ptr<type>& t) {
    if (type::is_pointer(t))   return indir_kind::pointer;
    if (type::is_link(t))      return indir_kind::link;
    if (type::is_view(t))      return indir_kind::view;
    if (type::is_reference(t)) return indir_kind::reference;
    if (type::is_owner(t))     return indir_kind::owner;
    if (type::is_drain(t))     return indir_kind::drain;
    return indir_kind::value;
}

std::string type_name(const std::shared_ptr<type>& t) {
    return t ? t->to_string() : std::string("<none>");
}

/** The aggregate designated by @p t once every const layer has been peeled. */
std::shared_ptr<aggregate> aggregate_of(const std::shared_ptr<type>& t) {
    auto st = std::dynamic_pointer_cast<struct_type>(type::remove_const(t));
    return st ? st->get_struct() : nullptr;
}

/**
 * Decide whether a value of type @p from may be used where @p to is expected inside a
 * callable prototype.
 *
 * `needs_adjustment` is set when a nominal derivation *does* exist but its sub-object
 * is not at offset 0 — the caller turns that into
 * `ERR_CALLABLE_COVARIANCE_NEEDS_ADJUSTMENT` rather than a plain incompatibility.
 */
bool type_usable_as(const std::shared_ptr<type>& from,
                    const std::shared_ptr<type>& to,
                    bool& needs_adjustment,
                    std::string& reason)
{
    needs_adjustment = false;

    if (!from && !to) return true;
    if (!from || !to) {
        reason = fmt::format("'{}' and '{}' do not agree on whether a value is produced",
                             type_name(from), type_name(to));
        return false;
    }

    // Nominal identity first: this is what keeps a `typedef` distinct from the type
    // it renames (type::are_equal is deliberately nominal for alias types).
    if (type::are_equal(from, to)) return true;

    const auto fk = kind_of(from);
    const auto tk = kind_of(to);

    if (fk != tk) {
        reason = fmt::format("'{}' and '{}' do not carry the same addresser; "
                             "a callable never adapts an indirection",
                             type_name(from), type_name(to));
        return false;
    }

    if (fk == indir_kind::value) {
        // Two distinct non-indirection types. No conversion may be inserted at an
        // indirect call site, so this is always a rejection; the message just
        // explains *why* for the most common mistake.
        auto from_nc = type::remove_const(from);
        auto to_nc   = type::remove_const(to);
        if (type::is_primitive(from_nc) && type::is_primitive(to_nc)) {
            reason = fmt::format("'{}' and '{}' are distinct primitive types; "
                                 "a callable accepts neither widening nor narrowing",
                                 type_name(from), type_name(to));
        } else if (type::are_equal(from_nc, to_nc)) {
            reason = fmt::format("'{}' and '{}' differ only by constness, "
                                 "which changes how the value is passed",
                                 type_name(from), type_name(to));
        } else {
            reason = fmt::format("'{}' and '{}' are unrelated types", type_name(from), type_name(to));
        }
        return false;
    }

    auto from_sub = from->get_subtype();
    auto to_sub   = to->get_subtype();
    if (!from_sub || !to_sub) {
        reason = fmt::format("'{}' or '{}' has no resolved pointed type",
                             type_name(from), type_name(to));
        return false;
    }

    // Losing const on the way in would let the callee mutate a const object.
    if (type::is_const(from_sub) && !type::is_const(to_sub)) {
        reason = fmt::format("'{}' points to a const object but '{}' does not; "
                             "constness cannot be dropped",
                             type_name(from), type_name(to));
        return false;
    }

    auto from_pointee = type::remove_const(from_sub);
    auto to_pointee   = type::remove_const(to_sub);
    if (type::are_equal(from_pointee, to_pointee)) return true;

    auto from_agg = aggregate_of(from_pointee);
    auto to_agg   = aggregate_of(to_pointee);
    if (!from_agg || !to_agg) {
        reason = fmt::format("'{}' and '{}' point to unrelated types",
                             type_name(from), type_name(to));
        return false;
    }

    if (!from_agg->is_derived_from(to_agg)) {
        reason = fmt::format("'{}' does not derive from '{}'",
                             from_agg->get_short_name(), to_agg->get_short_name());
        return false;
    }

    if (!callable_base_at_zero_offset(from_agg, to_agg)) {
        needs_adjustment = true;
        reason = fmt::format("the '{}' sub-object of '{}' is not at offset 0, so binding it "
                             "would require a pointer adjustment that an indirect call "
                             "cannot perform",
                             to_agg->get_short_name(), from_agg->get_short_name());
        return false;
    }
    return true;
}

/** True when @p thrown is @p caught or derives from it (a `throws` set is covariant). */
bool throws_covered_by(const std::shared_ptr<type>& thrown, const std::shared_ptr<type>& caught) {
    if (!thrown || !caught) return false;
    if (type::are_equal(thrown, caught)) return true;
    auto t_agg = aggregate_of(thrown);
    auto c_agg = aggregate_of(caught);
    return t_agg && c_agg && t_agg->is_derived_from(c_agg);
}

std::string render(const callable_signature_view& sig) {
    if (!sig.label.empty()) return sig.label;
    std::string out = "(";
    for (size_t i = 0; i < sig.parameter_types.size(); ++i) {
        if (i > 0) out += ", ";
        out += type_name(sig.parameter_types[i]);
    }
    out += ")";
    if (sig.return_type) out += ":" + sig.return_type->to_string();
    return out;
}

} // anonymous namespace


callable_signature_view callable_signature_view::of(const callable_type& ct) {
    callable_signature_view sig;
    sig.return_type     = ct.get_return_type();
    sig.parameter_types = ct.get_parameter_types();
    sig.throws          = ct.get_throws();
    sig.label           = ct.to_string();
    return sig;
}

callable_signature_view callable_signature_view::of(const function& fn) {
    callable_signature_view sig;
    sig.return_type = std::const_pointer_cast<type>(fn.get_return_type());
    for (size_t i = 0; i < fn.get_parameter_size(); ++i) {
        auto p = fn.get_parameter(i);
        sig.parameter_types.push_back(p ? p->get_type() : nullptr);
    }
    sig.throws = fn.get_throws_spec();
    sig.label  = fn.get_fq_name();
    return sig;
}


bool callable_base_at_zero_offset(const std::shared_ptr<aggregate>& derived,
                                  const std::shared_ptr<aggregate>& base)
{
    if (!derived || !base) return false;

    std::unordered_set<const aggregate*> seen;
    std::function<bool(const std::shared_ptr<aggregate>&)> walk =
        [&](const std::shared_ptr<aggregate>& cur) -> bool {
            if (!cur) return false;
            if (cur.get() == base.get()) return true;
            if (!seen.insert(cur.get()).second) return false;
            auto st = cur->get_struct_type();
            if (!st) return false;
            for (const auto& bs : cur->get_bases()) {
                // A virtual base is reached through a `__vbptr_X__` load: its address is
                // never a statically known offset from the derived object.
                if (!bs.base || bs.is_virtual) continue;
                auto field = st->get_member("__base_" + bs.sanitised_name() + "__");
                // LLVM guarantees element 0 starts at byte offset 0; any other index is
                // preceded by at least the vptr of a polymorphic class.
                if (!field || field->index != 0) continue;
                if (walk(bs.base)) return true;
            }
            return false;
        };
    return walk(derived);
}


callable_compat callable_signature_compatible(const callable_signature_view& src,
                                              const callable_signature_view& dst)
{
    callable_compat res;

    if (src.parameter_types.size() != dst.parameter_types.size()) {
        res.result = callable_compat::status::incompatible;
        res.reason = fmt::format("'{}' takes {} parameter(s) but '{}' declares {}",
                                 render(src), src.parameter_types.size(),
                                 render(dst), dst.parameter_types.size());
        return res;
    }

    // Return type — covariant: what the source produces must be usable as what the
    // destination promises.
    {
        bool needs_adjustment = false;
        std::string why;
        if (!type_usable_as(src.return_type, dst.return_type, needs_adjustment, why)) {
            res.result = needs_adjustment ? callable_compat::status::needs_adjustment
                                          : callable_compat::status::incompatible;
            res.reason = fmt::format("the return type of '{}' is not covariant with the return "
                                     "type of '{}': {}", render(src), render(dst), why);
            return res;
        }
    }

    // Parameters — contravariant: what the destination will pass must be usable as
    // what the source expects.
    for (size_t i = 0; i < src.parameter_types.size(); ++i) {
        bool needs_adjustment = false;
        std::string why;
        if (!type_usable_as(dst.parameter_types[i], src.parameter_types[i], needs_adjustment, why)) {
            res.result = needs_adjustment ? callable_compat::status::needs_adjustment
                                          : callable_compat::status::incompatible;
            res.reason = fmt::format("parameter {} of '{}' is not contravariant with parameter {} "
                                     "of '{}': {}", i + 1, render(src), i + 1, render(dst), why);
            return res;
        }
    }

    // throws(src) ⊆ throws(dst)
    for (const auto& thrown : src.throws) {
        const bool covered = std::any_of(dst.throws.begin(), dst.throws.end(),
            [&](const std::shared_ptr<type>& caught) { return throws_covered_by(thrown, caught); });
        if (!covered) {
            res.result = callable_compat::status::incompatible;
            res.throws_violation = true;
            res.reason = fmt::format("'{}' may throw '{}', which is not covered by the throws "
                                     "clause of '{}'",
                                     render(src), type_name(thrown), render(dst));
            return res;
        }
    }

    return res;
}

callable_compat callable_signature_compatible(const callable_type& src, const callable_type& dst) {
    return callable_signature_compatible(callable_signature_view::of(src),
                                         callable_signature_view::of(dst));
}

callable_compat callable_signature_compatible(const function& src, const callable_type& dst) {
    return callable_signature_compatible(callable_signature_view::of(src),
                                         callable_signature_view::of(dst));
}

} // namespace k::model::gen
