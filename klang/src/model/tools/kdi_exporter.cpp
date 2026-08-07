/*
 * K Language compiler
 *
 * Copyright 2026 Emilien Kia
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * you may obtain a copy of the License at
 *
 *         http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "../tools/kdi_exporter.hpp"
#include "../tools/k_source_emitter.hpp"

#include "../model.hpp"
#include "../type.hpp"
#include "../aggregate_value.hpp"

#include <unordered_map>
#include "../template.hpp"
#include "../mangler.hpp"
#include "../context.hpp"

#include "../../compiler.hpp"

#include <kdi.hpp>

#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/Support/raw_ostream.h>

#include <memory>
#include <string>
#include <type_traits>

namespace k::model {

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

static int64_t integer_literal_to_i64(const lex::integer& lit) {
    return std::visit([](const auto& v) -> int64_t {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
            return static_cast<int64_t>(v);
        }
        return 0;
    }, lit.value());
}

/**
 * True for compiler-synthesized field names that are purely an internal ABI
 * mechanism (vtable pointer(s), enclosing-instance back-reference, base/
 * virtual-base sub-object storage) and must never be surfaced as a
 * user-visible member — neither in a regular aggregate export (where they
 * are already excluded by build_layout(), which turns them into dedicated
 * layout-kind entries instead of named members) nor in a template's
 * declaration-only signature (build_generic_template_aggregate_signature),
 * which walks the model's named variables directly and would otherwise
 * leak them. Mirrors the skip-list used by
 * template_instantiator::clone_member_variable() when copying members into
 * a fresh instantiation.
 */
static bool is_synthetic_member_name(const std::string& name) {
    if (name == "__parent__") return true;
    if (name.rfind("__vptr", 0) == 0) return true;
    if (name.rfind("__base_", 0) == 0) return true;
    if (name.rfind("__vbptr_", 0) == 0) return true;
    if (name.rfind("__vbase_", 0) == 0) return true;
    return false;
}

/**
 * Capture the LLVM IR declaration line of @p fn as a string.
 * The result is a single-line "declare <rettype> @<name>(<params>)" string,
 * without trailing newline, suitable for storage in kdi::*::llvm_def.
 * Returns an empty string if @p fn is null.
 */
std::string llvm_fn_prototype(const llvm::Function* fn) {
    if (!fn) return {};
    std::string out;
    llvm::raw_string_ostream os(out);
    fn->print(os, /*AAW=*/nullptr, /*IsForDebug=*/false, /*IsDeclaration=*/true);
    os.flush();
    // The print() output contains a full definition or declaration block.
    // We only want the first line (the prototype), stripped of trailing whitespace.
    auto nl = out.find('\n');
    if (nl != std::string::npos) out.resize(nl);
    // Trim trailing whitespace / '{'
    while (!out.empty() && (out.back() == ' ' || out.back() == '{'))
        out.pop_back();
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    return out;
}

/**
 * Capture the LLVM IR type definition of @p st as a string.
 * Returns a string of the form "%StructName = type { ... }",
 * suitable for storage in kdi::kdi_aggregate::llvm_def.
 * Returns an empty string if @p st is null or anonymous.
 */
std::string llvm_struct_def(const llvm::StructType* st) {
    if (!st || !st->hasName()) return {};
    std::string out;
    llvm::raw_string_ostream os(out);
    st->print(os, /*IsForDebug=*/false);
    os.flush();
    return out;
}

/**
 * Capture the LLVM IR global variable declaration for a vtable global.
 * Returns "declare ... @<name>" suitable for kdi::kdi_vtable::llvm_def.
 */
std::string llvm_global_def(const llvm::GlobalVariable* gv) {
    if (!gv) return {};
    std::string out;
    llvm::raw_string_ostream os(out);
    gv->printAsOperand(os, /*PrintType=*/true);
    os.flush();
    // For vtable purposes we want the type of the global variable
    std::string full;
    llvm::raw_string_ostream fs(full);
    gv->print(fs, /*IsForDebug=*/false);
    fs.flush();
    auto nl = full.find('\n');
    if (nl != std::string::npos) full.resize(nl);
    return full;
}

std::optional<kdi::kdi_doc_block> to_kdi_doc_block(const element& elem) {
    auto doc = elem.get_documentation();
    if (!doc) return std::nullopt;
    kdi::kdi_doc_block out;
    out.brief = doc->brief;
    out.description = doc->description;
    return out;
}

std::optional<kdi::kdi_doc_function> to_kdi_doc_function(const function& fn) {
    auto fdoc = fn.get_documentation_as<doc::function_doc>();
    if (fdoc) {
        kdi::kdi_doc_function out;
        out.brief = fdoc->brief;
        out.description = fdoc->description;
        for (const auto& p : fdoc->params) {
            out.params.push_back(kdi::kdi_doc_param{
                .name = p.name,
                .description = p.description,
            });
        }
        if (fdoc->returns.has_value()) {
            out.returns = fdoc->returns->description;
        }
        for (const auto& t : fdoc->throws) {
            out.throws.push_back(kdi::kdi_doc_throws{
                .type_name = t.type_name,
                .description = t.description,
            });
        }
        for (const auto& tp : fdoc->template_params) {
            out.template_params.push_back(kdi::kdi_doc_template_param{
                .name = tp.name,
                .description = tp.description,
            });
        }
        for (const auto& tag : fdoc->tags) {
            out.tags.push_back(kdi::kdi_doc_tag{
                .tag = tag.tag,
                .value = tag.value,
            });
        }
        return out;
    }

    auto base = fn.get_documentation();
    if (!base) return std::nullopt;
    kdi::kdi_doc_function out;
    out.brief = base->brief;
    out.description = base->description;
    return out;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────

kdi_builder::kdi_builder(context& ctx,
                         const std::string& lib_path,
                         const std::string& compiler_ver)
    : _ctx(ctx)
    , _lib_path(lib_path)
    , _compiler_ver(compiler_ver)
{}

// ─────────────────────────────────────────────────────────────────────────────
// Private helpers
// ─────────────────────────────────────────────────────────────────────────────

kdi::kdi_visibility kdi_builder::to_kdi_vis(visibility v) {
    return v == PROTECTED ? kdi::kdi_visibility::protected_
                          : kdi::kdi_visibility::public_;
}

kdi::kdi_callable_addresser kdi_builder::to_kdi_addresser(callable_type::addresser a) {
    switch (a) {
        case callable_type::addresser::none:      return kdi::kdi_callable_addresser::none;
        case callable_type::addresser::view:      return kdi::kdi_callable_addresser::view;
        case callable_type::addresser::link:      return kdi::kdi_callable_addresser::link;
        case callable_type::addresser::reference: return kdi::kdi_callable_addresser::ref;
        case callable_type::addresser::pointer:   break;
    }
    return kdi::kdi_callable_addresser::ptr;
}

kdi::kdi_callable_type kdi_builder::to_kdi_callable(
    const callable_type& ct,
    const std::function<kdi::kdi_type(const std::shared_ptr<type>&)>& convert) const
{
    kdi::kdi_callable_type k;
    k.addresser = to_kdi_addresser(ct.get_addresser());
    // A null model return type means "returns nothing"; it is encoded as void
    // so that the decoder never has to deal with an absent entry.
    k.ret = std::make_shared<kdi::kdi_type>(
        ct.get_return_type() ? convert(ct.get_return_type()) : kdi::kdi_type::make_void());
    for (const auto& p : ct.get_parameter_types())
        k.params.push_back(std::make_shared<kdi::kdi_type>(convert(p)));
    for (const auto& th : ct.get_throws())
        k.throws.push_back(std::make_shared<kdi::kdi_type>(convert(th)));
    if (auto mfr = dynamic_cast<const member_function_reference_type*>(&ct)) {
        if (auto owner = mfr->get_member_of()) {
            std::string fq = owner->get_fq_name();
            if (fq.size() >= 2 && fq[0] == ':' && fq[1] == ':') fq = fq.substr(2);
            k.member_of = fq;
        }
    }
    return k;
}

kdi::kdi_type kdi_builder::to_kdi_type(const std::shared_ptr<type>& t) const {    if (!t) return kdi::kdi_type::make_void();

    if (auto ct = std::dynamic_pointer_cast<const_type>(t)) {
        kdi::kdi_const_type kct;
        kct.inner = std::make_shared<kdi::kdi_type>(to_kdi_type(ct->get_subtype()));
        return kdi::kdi_type{std::move(kct)};
    }
    if (auto pt = std::dynamic_pointer_cast<primitive_type>(t)) {
        if (pt->is_boolean())                        return kdi::kdi_type::make_bool();
        if (pt->get_type() == primitive_type::CHAR)  return kdi::kdi_type{kdi::kdi_char_type{}};
        if (pt->is_float())                          return kdi::kdi_type::make_float(static_cast<uint32_t>(pt->type_size()));
        return kdi::kdi_type::make_int(static_cast<uint32_t>(pt->type_size()), pt->is_signed());
    }
    if (auto rt = std::dynamic_pointer_cast<reference_type>(t)) {
        kdi::kdi_ref_type k; k.inner = std::make_shared<kdi::kdi_type>(to_kdi_type(rt->get_subtype()));
        return kdi::kdi_type{std::move(k)};
    }
    if (auto pt = std::dynamic_pointer_cast<pointer_type>(t)) {
        kdi::kdi_ptr_type k; k.inner = std::make_shared<kdi::kdi_type>(to_kdi_type(pt->get_subtype()));
        return kdi::kdi_type{std::move(k)};
    }
    if (auto lt = std::dynamic_pointer_cast<link_type>(t)) {
        kdi::kdi_link_type k; k.inner = std::make_shared<kdi::kdi_type>(to_kdi_type(lt->get_subtype()));
        return kdi::kdi_type{std::move(k)};
    }
    if (auto pt = std::dynamic_pointer_cast<view_type>(t)) {
        kdi::kdi_view_type k; k.inner = std::make_shared<kdi::kdi_type>(to_kdi_type(pt->get_subtype()));
        return kdi::kdi_type{std::move(k)};
    }
    if (auto ot = std::dynamic_pointer_cast<owner_type>(t)) {
        kdi::kdi_owner_type k; k.inner = std::make_shared<kdi::kdi_type>(to_kdi_type(ot->get_subtype()));
        return kdi::kdi_type{std::move(k)};
    }
    if (auto dt = std::dynamic_pointer_cast<drain_type>(t)) {
        kdi::kdi_drain_type k; k.inner = std::make_shared<kdi::kdi_type>(to_kdi_type(dt->get_subtype()));
        return kdi::kdi_type{std::move(k)};
    }
    if (auto sat = std::dynamic_pointer_cast<sized_array_type>(t)) {
        kdi::kdi_sized_array_type k;
        k.elem = std::make_shared<kdi::kdi_type>(to_kdi_type(sat->get_subtype()));
        k.size = sat->get_size();
        return kdi::kdi_type{std::move(k)};
    }
    if (auto at = std::dynamic_pointer_cast<array_type>(t)) {
        kdi::kdi_array_type k;
        k.elem = std::make_shared<kdi::kdi_type>(to_kdi_type(at->get_subtype()));
        return kdi::kdi_type{std::move(k)};
    }
    if (auto frt = std::dynamic_pointer_cast<callable_type>(t)) {
        return kdi::kdi_type{to_kdi_callable(
            *frt, [this](const std::shared_ptr<type>& sub) { return to_kdi_type(sub); })};
    }
    if (auto st = std::dynamic_pointer_cast<struct_type>(t)) {
        // Use the fully-qualified name so importing compilers can unambiguously
        // resolve the aggregate type.  aggregate::get_fq_name() returns "::ns::Type"
        // (with leading "::"); we strip that prefix to match the KDI convention
        // (e.g. "geom::Vec2", not "::geom::Vec2").
        std::string fq = st->name(); // fallback: short name
        if (auto agg = st->get_struct()) {
            const std::string& agg_fq = agg->get_fq_name();
            if (!agg_fq.empty()) {
                // Strip leading "::" if present
                fq = (agg_fq.size() >= 2 && agg_fq[0] == ':' && agg_fq[1] == ':')
                     ? agg_fq.substr(2) : agg_fq;
            }
        }
        return kdi::kdi_type::make_aggregate(std::move(fq));
    }
    if (auto at = std::dynamic_pointer_cast<alias_type>(t)) {
        // A strong alias (typedef) is nominally distinct from the type it
        // renames, so the reference is exported as such. A soft alias never
        // reaches this point: it resolves directly to the renamed type.
        std::string fq = at->get_fq_name();
        if (auto ad = at->get_alias()) {
            if (!ad->get_fq_name().empty()) fq = ad->get_fq_name();
        }
        if (fq.size() >= 2 && fq[0] == ':' && fq[1] == ':') fq = fq.substr(2);
        if (!fq.empty()) return kdi::kdi_type::make_alias(std::move(fq));
        // Fallback: export the renamed type when the alias cannot be named.
        return to_kdi_type(at->get_underlying());
    }
    if (auto et = std::dynamic_pointer_cast<enum_type>(t)) {
        auto en = et->get_enumeration();
        if (en) {
            const std::string& fq = en->get_fq_name();
            std::string clean_fq = (fq.size() >= 2 && fq[0] == ':' && fq[1] == ':')
                                   ? fq.substr(2) : fq;
            return kdi::kdi_type::make_enum(std::move(clean_fq));
        }
        // Fallback: export as underlying primitive if enumeration is unknown
        return to_kdi_type(et->get_underlying_type());
    }
    return kdi::kdi_type::make_void();
}

kdi::kdi_type kdi_builder::to_kdi_signature_type(const std::shared_ptr<type>& t,
                                                 const tpl_info& ti) const {
    if (!t) return kdi::kdi_type::make_void();

    if (auto ut = std::dynamic_pointer_cast<unresolved_type>(t)) {
        const std::string id = ut->type_id().to_string();
        for (const auto& param : ti.params) {
            if (param.name == id) {
                return kdi::kdi_type::make_template_param(id);
            }
        }

        // Not a bare template-parameter name. If it is resolved to a concrete
        // type (e.g. imported/instantiated), fall through to the normal
        // conversion below. Otherwise this names another (possibly still
        // uninstantiated) type applied with template arguments — e.g. the
        // member type "MultiSlot<T>" inside the still-generic "Vector<T>"
        // declaration. Preserve that structurally as a kdi_generic_ref_type
        // instead of silently losing it as void.
        if (!ut->is_resolved()) {
            kdi::kdi_generic_ref_type gref;
            gref.name = id;
            for (const auto& arg : ut->get_ast_template_args()) {
                if (arg && arg->is_type() && arg->type_arg) {
                    auto arg_type = _ctx.from_type_specifier(*arg->type_arg);
                    gref.args.push_back(std::make_shared<kdi::kdi_type>(
                        to_kdi_signature_type(arg_type, ti)));
                } else {
                    // Value (non-type) template argument: not structurally
                    // representable in kdi_generic_ref_type (see kdi_types.hpp).
                    gref.args.push_back(std::make_shared<kdi::kdi_type>(kdi::kdi_type::make_void()));
                }
            }
            return kdi::kdi_type{std::move(gref)};
        }
    }

    if (auto ct = std::dynamic_pointer_cast<const_type>(t)) {
        kdi::kdi_const_type kct;
        kct.inner = std::make_shared<kdi::kdi_type>(to_kdi_signature_type(ct->get_subtype(), ti));
        return kdi::kdi_type{std::move(kct)};
    }
    if (auto rt = std::dynamic_pointer_cast<reference_type>(t)) {
        kdi::kdi_ref_type k; k.inner = std::make_shared<kdi::kdi_type>(to_kdi_signature_type(rt->get_subtype(), ti));
        return kdi::kdi_type{std::move(k)};
    }
    if (auto pt = std::dynamic_pointer_cast<pointer_type>(t)) {
        kdi::kdi_ptr_type k; k.inner = std::make_shared<kdi::kdi_type>(to_kdi_signature_type(pt->get_subtype(), ti));
        return kdi::kdi_type{std::move(k)};
    }
    if (auto lt = std::dynamic_pointer_cast<link_type>(t)) {
        kdi::kdi_link_type k; k.inner = std::make_shared<kdi::kdi_type>(to_kdi_signature_type(lt->get_subtype(), ti));
        return kdi::kdi_type{std::move(k)};
    }
    if (auto vt = std::dynamic_pointer_cast<view_type>(t)) {
        kdi::kdi_view_type k; k.inner = std::make_shared<kdi::kdi_type>(to_kdi_signature_type(vt->get_subtype(), ti));
        return kdi::kdi_type{std::move(k)};
    }
    if (auto ot = std::dynamic_pointer_cast<owner_type>(t)) {
        kdi::kdi_owner_type k; k.inner = std::make_shared<kdi::kdi_type>(to_kdi_signature_type(ot->get_subtype(), ti));
        return kdi::kdi_type{std::move(k)};
    }
    if (auto dt = std::dynamic_pointer_cast<drain_type>(t)) {
        kdi::kdi_drain_type k; k.inner = std::make_shared<kdi::kdi_type>(to_kdi_signature_type(dt->get_subtype(), ti));
        return kdi::kdi_type{std::move(k)};
    }
    if (auto at = std::dynamic_pointer_cast<array_type>(t)) {
        kdi::kdi_array_type k; k.elem = std::make_shared<kdi::kdi_type>(to_kdi_signature_type(at->get_subtype(), ti));
        return kdi::kdi_type{std::move(k)};
    }
    if (auto sat = std::dynamic_pointer_cast<sized_array_type>(t)) {
        kdi::kdi_sized_array_type k;
        k.elem = std::make_shared<kdi::kdi_type>(to_kdi_signature_type(sat->get_subtype(), ti));
        k.size = sat->get_size();
        return kdi::kdi_type{std::move(k)};
    }
    if (auto frt = std::dynamic_pointer_cast<callable_type>(t)) {
        return kdi::kdi_type{to_kdi_callable(
            *frt, [this, &ti](const std::shared_ptr<type>& sub) {
                return to_kdi_signature_type(sub, ti);
            })};
    }
    if (auto ufrt = std::dynamic_pointer_cast<unresolved_callable_type>(t)) {
        // A callable appearing in a still-generic signature (e.g. '*(T):R'):
        // its components are template-parameter placeholders, so it cannot be
        // converted through to_kdi_type().
        kdi::kdi_callable_type k;
        k.addresser = to_kdi_addresser(ufrt->get_addresser());
        k.ret = std::make_shared<kdi::kdi_type>(
            ufrt->get_return_type() ? to_kdi_signature_type(ufrt->get_return_type(), ti)
                                    : kdi::kdi_type::make_void());
        for (const auto& p : ufrt->parameter_types())
            k.params.push_back(std::make_shared<kdi::kdi_type>(to_kdi_signature_type(p, ti)));
        for (const auto& th : ufrt->get_throws())
            k.throws.push_back(std::make_shared<kdi::kdi_type>(to_kdi_signature_type(th, ti)));
        if (!ufrt->owner_name().empty()) k.member_of = ufrt->owner_name().to_string();
        return kdi::kdi_type{std::move(k)};
    }

    return to_kdi_type(t);
}

std::vector<kdi::kdi_param> kdi_builder::to_kdi_params(const function& fn) const {
    std::vector<kdi::kdi_param> result;
    for (auto& p : fn.parameters()) {
        if (p->get_short_name() == "this" || p->get_short_name() == "__parent__") continue;
        kdi::kdi_param kp;
        kp.name = p->get_short_name();
        kp.type = to_kdi_type(p->get_type());
        kp.is_varargs = p->is_varargs();
        result.push_back(std::move(kp));
    }
    return result;
}

std::vector<kdi::kdi_param> kdi_builder::to_kdi_signature_params(const function& fn,
                                                                 const tpl_info& ti) const {
    std::vector<kdi::kdi_param> result;
    for (auto& p : fn.parameters()) {
        if (p->get_short_name() == "this" || p->get_short_name() == "__parent__") continue;
        kdi::kdi_param kp;
        kp.name = p->get_short_name();
        kp.type = to_kdi_signature_type(p->get_type(), ti);
        kp.is_varargs = p->is_varargs();
        result.push_back(std::move(kp));
    }
    return result;
}

kdi::kdi_aggregate kdi_builder::build_generic_template_aggregate_signature(const aggregate& agg,
                                                                             const tpl_info& ti,
                                                                             const std::string& fq_name) const {
    kdi::kdi_aggregate sig;
    if (agg.is_annotation()) sig.kind = kdi::kdi_aggregate_kind::annotation_;
    else if (dynamic_cast<const interface*>(&agg) != nullptr) sig.kind = kdi::kdi_aggregate_kind::interface_;
    else if (agg.is_class()) sig.kind = kdi::kdi_aggregate_kind::class_;
    else sig.kind = kdi::kdi_aggregate_kind::struct_;
    sig.name = agg.get_short_name();
    sig.fq_name = fq_name;
    sig.visibility = to_kdi_vis(agg.get_visibility());
    sig.is_abstract = agg.is_abstract();
    sig.is_final = agg.is_final();
    sig.is_const_struct = agg.is_const_struct();
    sig.is_static_nested = agg.is_static_nested();
    sig.doc = to_kdi_doc_block(agg);

    // Export base classes (for templates, use raw_name as identifier)
    for (const auto& bs : agg.get_bases()) {
        kdi::kdi_base kb;
        kb.fq_name          = bs.raw_name;  // raw_name contains e.g. "Collection<T>"
        kb.visibility       = to_kdi_vis(bs.vis);
        kb.is_virtual       = bs.is_virtual;
        kb.base_field_index = -1;
        kb.byte_offset      = 0;
        sig.bases.push_back(std::move(kb));
    }

    uint32_t field_index = 0;
    for (const auto& [name, var] : agg.variables()) {
        auto mv = std::dynamic_pointer_cast<member_variable_definition>(var);
        if (!mv || !is_exported(mv->get_visibility())) continue;
        // Skip compiler-synthesized ABI-mechanism fields (vtable pointer(s),
        // enclosing-instance back-reference, base/virtual-base sub-object
        // storage): they are internal implementation details, not part of
        // the template's documented API surface. A regular (non-template)
        // aggregate export never sees these either — build_layout() turns
        // them into dedicated layout-kind entries instead of named members.
        if (is_synthetic_member_name(mv->get_short_name())) continue;
        kdi::kdi_layout_member member;
        member.name = mv->get_short_name();
        member.fq_name = mv->get_fq_name();
        member.visibility = to_kdi_vis(mv->get_visibility());
        member.llvm_field_index = field_index++;
        member.type = to_kdi_signature_type(mv->get_type(), ti);
        member.is_const = mv->is_const();
        sig.layout.push_back(std::move(member));
    }

    for (auto& ctor : agg.constructors()) {
        if (!ctor || !is_exported(ctor->get_visibility())) continue;
        kdi::kdi_constructor kc;
        kc.visibility = to_kdi_vis(ctor->get_visibility());
        kc.is_copy_constructor = ctor->is_copy_constructor();
        kc.is_defaulted = ctor->is_defaulted();
        kc.is_deleted = ctor->is_deleted();
        kc.params = to_kdi_signature_params(*ctor, ti);
        kc.doc = to_kdi_doc_function(*ctor);
        sig.constructors.push_back(std::move(kc));
    }

     for (const auto& child : agg.get_children()) {
         auto fn = std::dynamic_pointer_cast<function>(child);
         if (!fn || std::dynamic_pointer_cast<constructor>(fn) || std::dynamic_pointer_cast<destructor>(fn)) {
             continue;
         }
         if (!is_exported(fn->get_visibility())) continue;
         kdi::kdi_method km;
         km.name = fn->get_short_name();
        km.fq_name = fn->get_fq_name();
        km.visibility = to_kdi_vis(fn->get_visibility());
        km.is_static = fn->is_static();
        km.is_const_member = fn->is_const_member();
        km.is_virtual = fn->is_virtual();
        km.is_abstract = fn->is_abstract_func();
        km.is_final = fn->is_final_func();
        km.is_operator = fn->is_operator();
        km.vtable_slot = fn->get_vtable_slot();
        km.return_type = to_kdi_signature_type(std::const_pointer_cast<type>(fn->get_return_type()), ti);
        km.params = to_kdi_signature_params(*fn, ti);
        km.doc = to_kdi_doc_function(*fn);
        sig.methods.push_back(std::move(km));
    }

    return sig;
}

kdi::kdi_function kdi_builder::build_generic_template_function_signature(const function& fn,
                                                                          const tpl_info& ti,
                                                                          const std::string& fq_name) const {
    kdi::kdi_function sig;
    sig.name = fn.get_short_name();
    sig.fq_name = fq_name;
    sig.visibility = to_kdi_vis(fn.get_visibility());
    sig.is_static = fn.is_static();
    sig.is_operator = fn.is_operator();
    sig.return_type = to_kdi_signature_type(std::const_pointer_cast<type>(fn.get_return_type()), ti);
    sig.params = to_kdi_signature_params(fn, ti);
    sig.doc = to_kdi_doc_function(fn);
    return sig;
}

void kdi_builder::register_aggregate_type(const aggregate& agg) {
    const std::string fq = agg.get_fq_name();
    for (auto& e : _file.types.aggregates)
        if (e.fq_name == fq) return;
    kdi::kdi_aggregate_type_entry entry;
    entry.fq_name      = fq;
    entry.mangled_name = agg.get_struct_type() ? agg.get_struct_type()->name() : "";
    _file.types.aggregates.push_back(std::move(entry));
}

std::vector<kdi::kdi_layout_field> kdi_builder::build_layout(const aggregate& agg) const {
    std::vector<kdi::kdi_layout_field> result;

    auto st = agg.get_struct_type();
    if (!st) return result;
    auto* llvm_st = llvm::dyn_cast_or_null<llvm::StructType>(st->get_llvm_type());
    if (!llvm_st) return result;

    const llvm::DataLayout& dl = _ctx.module().getDataLayout();

    std::string primary_vtable_symbol;
    if (agg.has_vtable())
        primary_vtable_symbol = mangler::mangle_vtable(agg.get_name());

    const uint32_t num_fields = static_cast<uint32_t>(llvm_st->getNumElements());

    // Opaque block accumulator for private / synthetic fields
    struct Opaque { bool active=false; uint32_t start=0, count=0; uint64_t bits=0; };
    Opaque opaque;

    auto flush = [&]() {
        if (opaque.active && opaque.count > 0) {
            kdi::kdi_layout_opaque_block ob;
            ob.llvm_field_index = opaque.start;
            ob.field_count      = opaque.count;
            ob.size_bits        = opaque.bits;
            result.push_back(std::move(ob));
        }
        opaque = {};
    };
    auto add_opaque = [&](uint32_t fi, uint64_t bits) {
        if (!opaque.active) { opaque.active=true; opaque.start=fi; opaque.count=0; opaque.bits=0; }
        opaque.count++; opaque.bits += bits;
    };

    for (uint32_t fi = 0; fi < num_fields; ++fi) {
        llvm::Type* ft = llvm_st->getElementType(fi);
        uint64_t   fsz = dl.getTypeSizeInBits(ft);

        // Find the struct_type field for this LLVM index
        std::optional<struct_type::field> sf;
        for (auto it = st->fields_begin(); it != st->fields_end(); ++it) {
            if (it->index == fi) { sf = *it; break; }
        }
        if (!sf) { add_opaque(fi, fsz); continue; }

        const std::string& fname = sf->name;

        // ── primary vptr ───────────────────────────────────────────────
        if (fname == "__vptr__") {
            flush();
            kdi::kdi_layout_vptr vp;
            vp.llvm_field_index = fi;
            vp.vtable_symbol    = primary_vtable_symbol;
            result.push_back(std::move(vp));
            continue;
        }
        // ── secondary vptr (__vptr_X__) ────────────────────────────────
        if (fname.rfind("__vptr_", 0) == 0 && fname.size() > 9) {
            flush();
            kdi::kdi_layout_vptr_secondary vs;
            vs.llvm_field_index = fi;
            vs.base_fq_name     = fname.substr(7, fname.size() - 9);
            vs.vtable_symbol    = "";
            result.push_back(std::move(vs));
            continue;
        }
        // ── base subobject (__base_X__) ────────────────────────────────
        if (fname.rfind("__base_", 0) == 0 && fname.size() > 9) {
            flush();
            // Keep the LOCAL sanitised name (as literally used when the struct
            // was built, i.e. base_spec::sanitised_name()) rather than resolving
            // to the base's fully-qualified name. Re-deriving to an FQ form here
            // caused a naming mismatch on import: base_subobject_field_injection
            // (template_instantiator.cpp) and every other local consumer of
            // "__base_X__" fields (casts, vtable offset lookups, etc.) always
            // build the field name from the LOCAL (possibly unqualified,
            // as-written) base name — never from get_fq_name(). Exporting an
            // FQ-derived name meant re-imported/reused struct_types never
            // matched what local code looked up by name (see the vptr_secondary
            // case above, which never had this bug).
            std::string bname = fname.substr(7, fname.size() - 9);
            kdi::kdi_layout_base_subobject bso;
            bso.llvm_field_index = fi;
            bso.base_fq_name     = std::move(bname);
            result.push_back(std::move(bso));
            continue;
        }
        // ── virtual base pointer (__vbptr_X__) ────────────────────────
        if (fname.rfind("__vbptr_", 0) == 0 && fname.size() > 10) {
            flush();
            // See the __base_X__ case above: keep the local sanitised name,
            // not an FQ-resolved one, so import-time field-name reconstruction
            // matches what local codegen looks up by name.
            std::string vbname = fname.substr(8, fname.size() - 10);
            kdi::kdi_layout_vbptr vb;
            vb.llvm_field_index = fi;
            vb.vbase_fq_name    = std::move(vbname);
            result.push_back(std::move(vb));
            continue;
        }
        // ── virtual base subobject (__vbase_X__) ──────────────────────
        if (fname.rfind("__vbase_", 0) == 0 && fname.size() > 10) {
            flush();
            // See the __base_X__ case above: keep the local sanitised name.
            std::string vbname = fname.substr(8, fname.size() - 10);
            kdi::kdi_layout_vbase_subobject vbs;
            vbs.llvm_field_index = fi;
            vbs.vbase_fq_name    = std::move(vbname);
            result.push_back(std::move(vbs));
            continue;
        }
        // ── inner-class parent reference (__parent__) ─────────────────
        if (fname == "__parent__") {
            flush();
            kdi::kdi_layout_parent_ref pr;
            pr.llvm_field_index = fi;
            if (auto enc = agg.get_enclosing_aggregate())
                pr.parent_fq_name = enc->get_fq_name();
            result.push_back(std::move(pr));
            continue;
        }

        // ── regular member variable ────────────────────────────────────
        auto mv_opt = agg.get_variable(fname);
        if (!mv_opt) { add_opaque(fi, fsz); continue; }
        auto mv = std::dynamic_pointer_cast<member_variable_definition>(mv_opt);
        if (!mv)     { add_opaque(fi, fsz); continue; }

        if (mv->get_visibility() == PUBLIC || mv->get_visibility() == PROTECTED) {
            flush();
            kdi::kdi_layout_member lm;
            lm.llvm_field_index = fi;
            lm.name             = mv->get_short_name();
            lm.fq_name          = mv->get_fq_name();
            lm.visibility       = to_kdi_vis(mv->get_visibility());
            lm.type             = to_kdi_type(std::const_pointer_cast<type>(mv->get_type()));
            lm.is_const         = type::is_const(std::const_pointer_cast<type>(mv->get_type()));
            lm.mangled_name     = mv->get_mangled_name();
            result.push_back(std::move(lm));
        } else {
            add_opaque(fi, fsz);
        }
    }
    flush();
    return result;
}

std::optional<kdi::kdi_vtable> kdi_builder::build_vtable(const aggregate& agg) const {
    if (!agg.has_vtable()) return std::nullopt;

    // get_vtable() exists on klass and annotation_type
    std::shared_ptr<vtable_layout> vt;
    if (const auto* kl = dynamic_cast<const klass*>(&agg)) {
        vt = kl->get_vtable();
    } else if (const auto* ann = dynamic_cast<const annotation_type*>(&agg)) {
        vt = ann->get_vtable();
    }
    if (!vt) return std::nullopt;

    kdi::kdi_vtable kvt;
    kvt.vtable_symbol = mangler::mangle_vtable(agg.get_name());
    kvt.rtti_symbol   = mangler::mangle_rtti(agg.get_name());

    // ── LLVM global variable definition for the vtable ────────────────────
    {
        auto* gv = _ctx.module().getNamedGlobal(kvt.vtable_symbol);
        kvt.llvm_def = llvm_global_def(gv);
    }

    for (auto& entry : vt->entries) {
        kdi::kdi_vtable_slot slot;
        slot.slot_index       = static_cast<uint32_t>(entry.slot_index);
        slot.introducing_func = entry.introducing_func
                                ? entry.introducing_func->get_fq_name() : "";
        slot.is_abstract      = entry.func && entry.func->is_abstract_func();
        slot.override_symbol  = (!slot.is_abstract && entry.func)
                                ? entry.func->get_mangled_name() : "";
        kvt.slots.push_back(std::move(slot));
    }

    for (auto& sec : vt->secondary_vtables) {
        if (!sec.base_class) continue;
        kdi::kdi_secondary_vtable ksec;
        ksec.base_fq_name  = sec.base_class->get_fq_name();
        ksec.base_offset   = static_cast<uint64_t>(sec.base_offset);
        ksec.vtable_symbol = mangler::mangle_vtable(agg.get_name())
                             + "_for_" + sec.base_class->get_short_name();
        for (auto& ti : sec.slot_thunks) {
            kdi::kdi_thunk kt;
            kt.slot_index       = static_cast<uint32_t>(ti.slot_index);
            kt.real_func_symbol = ti.real_func ? ti.real_func->get_mangled_name() : "";
            kt.this_adjustment  = static_cast<int32_t>(sec.base_offset);
            kt.needs_thunk      = ti.needs_thunk;
            ksec.thunks.push_back(std::move(kt));
        }
        kvt.secondary.push_back(std::move(ksec));
    }
    return kvt;
}

kdi::kdi_aggregate kdi_builder::begin_aggregate(const aggregate& agg) {
    register_aggregate_type(agg);

    kdi::kdi_aggregate kagg;

    if (dynamic_cast<const model::interface*>(&agg))
        kagg.kind = kdi::kdi_aggregate_kind::interface_;
    else if (agg.is_annotation())
        kagg.kind = kdi::kdi_aggregate_kind::annotation_;
    else if (agg.is_class())
        kagg.kind = kdi::kdi_aggregate_kind::class_;
    else
        kagg.kind = kdi::kdi_aggregate_kind::struct_;

    kagg.name            = agg.get_short_name();
    kagg.fq_name         = agg.get_fq_name();
    kagg.mangled_name    = agg.get_struct_type() ? agg.get_struct_type()->name() : "";
    kagg.visibility      = to_kdi_vis(agg.get_visibility());
    kagg.is_abstract     = agg.is_abstract();
    kagg.is_final        = agg.is_final();
    kagg.is_const_struct = agg.is_const_struct();
    kagg.is_static_nested = agg.is_static_nested();
    kagg.doc             = to_kdi_doc_block(agg);

    // Enclosing aggregate fq_name (empty if top-level)
    if (agg.is_nested()) {
        if (auto enc = agg.get_enclosing_aggregate())
            kagg.enclosing_fq_name = enc->get_fq_name();
    }

    // ── LLVM struct type definition ───────────────────────────────────────
    if (agg.get_struct_type()) {
        auto* llvm_st = llvm::dyn_cast_or_null<llvm::StructType>(
            agg.get_struct_type()->get_llvm_type());
        kagg.llvm_def = llvm_struct_def(llvm_st);
    }

    // ── Bases ────────────────────────────────────────────────────────────
    const llvm::DataLayout& dl = _ctx.module().getDataLayout();
    for (auto& bs : agg.get_bases()) {
        if (!bs.base) continue;
        kdi::kdi_base kb;
        kb.fq_name          = bs.base->get_fq_name();
        kb.visibility       = to_kdi_vis(bs.vis);
        kb.is_virtual       = bs.is_virtual;
        kb.base_field_index = -1;
        kb.byte_offset      = 0;
        if (!bs.is_virtual && agg.get_struct_type()) {
            auto field = agg.get_struct_type()->get_member("__base_" + bs.sanitised_name() + "__");
            if (field) {
                kb.base_field_index = static_cast<int32_t>(field->index);
                if (auto* llvm_st = llvm::dyn_cast_or_null<llvm::StructType>(
                        agg.get_struct_type()->get_llvm_type())) {
                    if (auto* lo = dl.getStructLayout(llvm_st))
                        kb.byte_offset = lo->getElementOffset(field->index);
                }
            }
        }
        kagg.bases.push_back(std::move(kb));
    }

    // ── Layout ───────────────────────────────────────────────────────────
    kagg.layout = build_layout(agg);

    // ── Vtable ───────────────────────────────────────────────────────────
    kagg.vtable = build_vtable(agg);

    // ── Default constructor reference ────────────────────────────────────
    // Find the default (0-parameter, excluding 'this') constructor for
    // designated init support of imported types.
    for (auto& ctor : agg.constructors()) {
        if (!ctor) continue;
        // Count non-this parameters
        size_t user_params = 0;
        for (auto& p : ctor->parameters()) {
            if (p->get_short_name() != "this" && p->get_short_name() != "__parent__") {
                ++user_params;
            }
        }
        if (user_params == 0 && is_exported(ctor->get_visibility())) {
            kagg.default_constructor_mangled_name = ctor->get_mangled_name();
            break;
        }
    }

    return kagg;
}

void kdi_builder::visit_aggregate_body(aggregate& agg, kdi::kdi_aggregate& kagg) {
    // Push the aggregate onto the stack so that visit_constructor,
    // visit_destructor, visit_function and visit_member_variable_definition
    // all deposit into kagg.
    _agg_stack.push_back(&kagg);

    export_aliases(agg, kagg.aliases, agg.get_fq_name());

    for (auto& child : agg.get_children())
        child->accept(*this);

    _agg_stack.pop_back();
}

// ─────────────────────────────────────────────────────────────────────────────
// Visitor implementations
// ─────────────────────────────────────────────────────────────────────────────

void kdi_builder::visit_unit(unit& u) {
    _file.header.module_name  = u.get_unit_name().to_string();
    _file.header.lib_base     = compiler::unit_name_to_lib_base(_file.header.module_name);
    _file.header.lib_path     = _lib_path;
    _file.header.compiler_ver = _compiler_ver;
    _file.unit.name           = _file.header.module_name;

    // Record the direct imports as dependencies so that consumers can
    // perform transitive KDI loading.
    for (const auto& imp : u.get_imports()) {
        if (!imp.module_name.empty())
            _file.header.dependencies.push_back(imp.module_name.to_string());
    }

    if (auto root = u.get_root_namespace())
        root->accept(*this);
}

void kdi_builder::export_aliases(const alias_holder& holder, std::vector<kdi::kdi_alias>& out,
                                 const std::string& scope_fq_name) {
    for (const auto& al : holder.get_aliases()) {
        if (!al || !al->is_exported()) continue;

        kdi::kdi_alias ka;
        ka.name       = al->get_short_name();
        // An alias_definition never gets an fq_name of its own (update_names()
        // only fills it for root-prefixed names), so it is derived from the
        // declaring scope.
        std::string scope = scope_fq_name;
        if (scope.size() >= 2 && scope[0] == ':' && scope[1] == ':') scope = scope.substr(2);
        ka.fq_name = scope.empty() ? ka.name : (scope + "::" + ka.name);
        ka.visibility = to_kdi_vis(al->get_visibility());
        ka.is_strong  = al->is_strong();

        if (al->is_template()) {
            // A parameterised alias renames a family of types: its target
            // carries template parameter placeholders and cannot be expressed
            // as a resolved kdi_type. It is round-tripped as source text, like
            // a template definition, and re-parsed by the importing compiler.
            ka.is_template = true;
            if (auto* ti = al->get_tpl_info()) {
                ka.source = ti->source_text;
                for (const auto& p : ti->params) {
                    kdi::kdi_template_param kp;
                    kp.name = p.name;
                    switch (p.kind) {
                        case template_param_kind::STRUCT:    kp.kind = "struct";    break;
                        case template_param_kind::CLASS:     kp.kind = "class";     break;
                        case template_param_kind::INTERFACE: kp.kind = "interface"; break;
                        default:                             kp.kind = "typename";  break;
                    }
                    if (p.constraint_type && type::is_resolved(p.constraint_type))
                        kp.constraint_type = to_kdi_type(p.constraint_type);
                    if (p.default_type && type::is_resolved(p.default_type))
                        kp.default_type = to_kdi_type(p.default_type);
                    ka.params.push_back(std::move(kp));
                }
            }
        } else {
            // The aliased type is exported in its declared (still alias-bearing)
            // form: an alias is exported exactly as written so an importing module
            // sees the same declaration.
            if (auto tgt = al->get_target_type()) {
                ka.target_type = to_kdi_type(tgt);
            }
            if (!al->get_target_name().empty()) {
                ka.target_fq_name = al->get_target_name().to_string();
            }
        }
        out.push_back(std::move(ka));
    }
}

void kdi_builder::visit_namespace(ns& n) {
    kdi::kdi_namespace kns;
    kns.name    = n.get_short_name();
    kns.fq_name = n.get_fq_name();
    kns.doc     = to_kdi_doc_block(n);

    // Push the new namespace as the current output target
    _ns_stack.push_back(&kns);

    export_aliases(n, kns.aliases, n.get_fq_name());

    for (auto& child : n.get_children())
        child->accept(*this);

    _ns_stack.pop_back();

    // Deposit into parent namespace or root
    if (!_ns_stack.empty())
        _ns_stack.back()->namespaces.push_back(std::move(kns));
    else
        _file.unit.root_ns = std::move(kns);
}

// NOTE (callables): a closure aggregate synthesised for a lambda is an implementation
// detail of the enclosing function and must never appear in a .kdi descriptor — a consumer
// only ever sees the callable type of the lambda, never the aggregate behind it. Closures
// are compiler-generated and left with private visibility, so the is_exported() guards at
// the top of visit_structure()/visit_klass() already skip them; keep that invariant when
// closure support lands.
void kdi_builder::visit_structure(structure& s) {
    if (!is_exported(s.get_visibility())) return;

    // Template definition: export as kdi_template_def, not as a regular aggregate
    if (s.is_template()) {
        if (!_ns_stack.empty() && s.get_tpl_info()) {
            std::string fq = s.get_fq_name();
            if (fq.empty() || fq == "::") {
                // Template definitions may not have their fq_name computed yet
                // (resolution passes skip them).  Reconstruct from namespace stack.
                fq = _ns_stack.back()->fq_name.empty()
                     ? s.get_short_name()
                     : _ns_stack.back()->fq_name + "::" + s.get_short_name();
            }
            _ns_stack.back()->template_defs.push_back(
                build_template_def(s.get_short_name(), fq, "struct",
                                   s.get_visibility(), *s.get_tpl_info(), &s));
        }
        return;
    }

    kdi::kdi_aggregate kagg = begin_aggregate(s);

    // Template origin for concrete instantiations
    if (s.has_tpl_args()) {
        // Derive the base fq_name: replace the instantiated name (e.g. "Box__int") with base_name in the fq path
        std::string inst_fq = s.get_fq_name();
        std::string inst_name = s.get_short_name();
        std::string base_fq = inst_fq;
        auto pos = base_fq.rfind(inst_name);
        if (pos != std::string::npos) {
            base_fq.replace(pos, inst_name.size(), s.get_tpl_base_name());
        }
        kagg.template_origin = build_template_origin(s.get_tpl_base_name(), base_fq, s.get_tpl_args());
    }

    visit_aggregate_body(s, kagg);

    // Deposit into parent namespace or enclosing aggregate
    if (!_agg_stack.empty())
        _agg_stack.back()->nested.push_back(std::move(kagg));
    else if (!_ns_stack.empty())
        _ns_stack.back()->aggregates.push_back(std::move(kagg));
}

void kdi_builder::visit_klass(klass& k) {
    if (!is_exported(k.get_visibility())) return;

    // Template definition: export as kdi_template_def
    if (k.is_template()) {
        if (!_ns_stack.empty() && k.get_tpl_info()) {
            std::string fq = k.get_fq_name();
            if (fq.empty() || fq == "::") {
                fq = _ns_stack.back()->fq_name.empty()
                     ? k.get_short_name()
                     : _ns_stack.back()->fq_name + "::" + k.get_short_name();
            }
            _ns_stack.back()->template_defs.push_back(
                build_template_def(k.get_short_name(), fq, "class",
                                   k.get_visibility(), *k.get_tpl_info(), &k));
        }
        return;
    }

    kdi::kdi_aggregate kagg = begin_aggregate(k);

    // Template origin for concrete instantiations
    if (k.has_tpl_args()) {
        std::string inst_fq = k.get_fq_name();
        std::string inst_name = k.get_short_name();
        std::string base_fq = inst_fq;
        auto pos = base_fq.rfind(inst_name);
        if (pos != std::string::npos) {
            base_fq.replace(pos, inst_name.size(), k.get_tpl_base_name());
        }
        kagg.template_origin = build_template_origin(k.get_tpl_base_name(), base_fq, k.get_tpl_args());
    }

    visit_aggregate_body(k, kagg);

    if (!_agg_stack.empty())
        _agg_stack.back()->nested.push_back(std::move(kagg));
    else if (!_ns_stack.empty())
        _ns_stack.back()->aggregates.push_back(std::move(kagg));
}

void kdi_builder::visit_interface(interface& i) {
    if (!is_exported(i.get_visibility())) return;

    // Template definition: export as kdi_template_def
    if (i.is_template()) {
        if (!_ns_stack.empty() && i.get_tpl_info()) {
            std::string fq = i.get_fq_name();
            if (fq.empty() || fq == "::") {
                fq = _ns_stack.back()->fq_name.empty()
                     ? i.get_short_name()
                     : _ns_stack.back()->fq_name + "::" + i.get_short_name();
            }
            _ns_stack.back()->template_defs.push_back(
                build_template_def(i.get_short_name(), fq, "interface",
                                   i.get_visibility(), *i.get_tpl_info(), &i));
        }
        return;
    }

    kdi::kdi_aggregate kagg = begin_aggregate(i);

    // Template origin for concrete instantiations
    if (i.has_tpl_args()) {
        std::string inst_fq = i.get_fq_name();
        std::string inst_name = i.get_short_name();
        std::string base_fq = inst_fq;
        auto pos = base_fq.rfind(inst_name);
        if (pos != std::string::npos) {
            base_fq.replace(pos, inst_name.size(), i.get_tpl_base_name());
        }
        kagg.template_origin = build_template_origin(i.get_tpl_base_name(), base_fq, i.get_tpl_args());
    }

    visit_aggregate_body(i, kagg);

    if (!_agg_stack.empty())
        _agg_stack.back()->nested.push_back(std::move(kagg));
    else if (!_ns_stack.empty())
        _ns_stack.back()->aggregates.push_back(std::move(kagg));
}

void kdi_builder::visit_annotation_type(annotation_type& a) {
    if (!is_exported(a.get_visibility())) return;

    kdi::kdi_aggregate kagg = begin_aggregate(a);
    visit_aggregate_body(a, kagg);

    if (!_agg_stack.empty())
        _agg_stack.back()->nested.push_back(std::move(kagg));
    else if (!_ns_stack.empty())
        _ns_stack.back()->aggregates.push_back(std::move(kagg));
}

void kdi_builder::visit_enumeration(enumeration& en) {
    if (!is_exported(en.get_visibility())) return;

    kdi::kdi_enum ke;
    ke.name       = en.get_short_name();
    ke.fq_name    = [&]() {
        const std::string& fq = en.get_fq_name();
        return (fq.size() >= 2 && fq[0] == ':' && fq[1] == ':')
               ? fq.substr(2) : fq;
    }();
    ke.visibility = to_kdi_vis(en.get_visibility());
    ke.doc = to_kdi_doc_block(en);

    if (en.get_underlying_type()) {
        ke.underlying_type = to_kdi_type(en.get_underlying_type());
    }
    if (en.is_object_backed()) {
        if (auto obj_type = en.get_object_type()) {
            ke.object_type = to_kdi_type(obj_type);
        }
        if (!en.get_table_symbol().empty()) {
            ke.object_table_symbol = en.get_table_symbol();
        } else if (auto* table = en.get_table_global()) {
            ke.object_table_symbol = table->getName().str();
        } else {
            // Keep import robust even when the table global is not directly reachable here.
            ke.object_table_symbol = "__klang_enum_table_" + en.get_mangled_name() + "__";
        }
    }

    if (en.has_base()) {
        const std::string& bfq = en.get_base()->get_fq_name();
        ke.base_fq_name = (bfq.size() >= 2 && bfq[0] == ':' && bfq[1] == ':')
                          ? bfq.substr(2) : bfq;
    }

    for (auto& entry : en.entries()) {
        kdi::kdi_enum_entry kee;
        kee.name       = entry.name;
        kee.value      = entry.value;
        kee.is_default = entry.is_default;
        if (entry.brace_init && entry.brace_init->is_designated) {
            for (auto& elem : entry.brace_init->elements) {
                auto desig = std::dynamic_pointer_cast<parse::ast::designated_init_element>(elem);
                if (!desig || desig->is_call_form) continue;
                auto lit = std::dynamic_pointer_cast<parse::ast::literal_expr>(desig->value);
                if (!lit) continue;
                auto& any_lit = lit->literal;
                if (any_lit.index() != lex::any_literal_type_index::INTEGER) continue;
                auto& int_lit = any_lit.get<lex::integer>();
                kee.object_init_members.emplace_back(
                    std::string{desig->member_name.content},
                    integer_literal_to_i64(int_lit));
            }
        }
        ke.entries.push_back(std::move(kee));
    }

    // Register in enum type table
    {
        bool found = false;
        for (auto& e : _file.types.enums) {
            if (e.fq_name == ke.fq_name) { found = true; break; }
        }
        if (!found) {
            kdi::kdi_enum_type_entry ete;
            ete.fq_name = ke.fq_name;
            _file.types.enums.push_back(std::move(ete));
        }
    }

    // Deposit into parent namespace
    if (!_ns_stack.empty())
        _ns_stack.back()->enums.push_back(std::move(ke));
}

void kdi_builder::visit_union(union_type_def& un) {
    if (!is_exported(un.get_visibility())) {
        // Private unions nested inside an exported aggregate still need their LLVM type
        // definition to be resolvable by consumers. Without it, the parent aggregate's
        // llvm_def (which references e.g. %_union) stays opaque in the consumer module,
        // causing a crash when LLVM tries to compute the aggregate's size.
        // Emit a minimal KDI union entry with *only* the llvm_def (no alternatives)
        // so that collect_llvm_defs_from_namespace includes it in the combined IR blob
        // parsed by intern_all_llvm_struct_defs. The importer will not create a model
        // node for this entry (detected by alternatives.empty()).
        if (!_agg_stack.empty() && un.get_struct_type()) {
            if (auto* llvm_st = un.get_struct_type()->get_llvm_type()) {
                std::string llvm_str;
                llvm::raw_string_ostream os(llvm_str);
                llvm_st->print(os);
                os.flush();
                if (!llvm_str.empty()) {
                    kdi::kdi_union ku;
                    ku.name        = un.get_short_name();
                    ku.fq_name     = [&]() {
                        const std::string& fq = un.get_fq_name();
                        return (fq.size() >= 2 && fq[0] == ':' && fq[1] == ':')
                               ? fq.substr(2) : fq;
                    }();
                    ku.mangled_name = un.get_mangled_name();
                    ku.visibility   = kdi::kdi_visibility::public_; // visibility unused for layout-only
                    ku.llvm_def     = std::move(llvm_str);
                    // alternatives intentionally empty — signals "layout-only, no model node"
                    _agg_stack.back()->nested_unions.push_back(std::move(ku));
                }
            }
        }
        return;
    }

    // Template definition: export as kdi_template_def, not as a regular union
    if (un.is_template()) {
        if (!_ns_stack.empty() && un.get_tpl_info()) {
            std::string fq = un.get_fq_name();
            if (fq.empty() || fq == "::") {
                fq = _ns_stack.back()->fq_name.empty()
                     ? un.get_short_name()
                     : _ns_stack.back()->fq_name + "::" + un.get_short_name();
            }
            _ns_stack.back()->template_defs.push_back(
                build_template_def(un.get_short_name(), fq, "union",
                                   un.get_visibility(), *un.get_tpl_info(), nullptr));
        }
        return;
    }

    kdi::kdi_union ku;
    ku.name        = un.get_short_name();
    ku.fq_name     = [&]() {
        const std::string& fq = un.get_fq_name();
        return (fq.size() >= 2 && fq[0] == ':' && fq[1] == ':')
               ? fq.substr(2) : fq;
    }();
    ku.mangled_name = un.get_mangled_name();
    ku.visibility   = to_kdi_vis(un.get_visibility());
    ku.doc          = to_kdi_doc_block(un);

    // Export base union name (if this is a derived union)
    if (un.has_base_union() && un.get_base_union()) {
        const std::string& base_fq = un.get_base_union()->get_fq_name();
        ku.base_union_fq_name = (base_fq.size() >= 2 && base_fq[0] == ':' && base_fq[1] == ':')
                                ? base_fq.substr(2) : base_fq;
    }

    // Template origin for concrete instantiations
    if (un.has_tpl_args()) {
        std::string inst_fq = un.get_fq_name();
        std::string inst_name = un.get_short_name();
        std::string base_fq = inst_fq;
        auto pos = base_fq.rfind(inst_name);
        if (pos != std::string::npos) {
            base_fq.replace(pos, inst_name.size(), un.get_tpl_base_name());
        }
        ku.template_origin = build_template_origin(un.get_tpl_base_name(), base_fq, un.get_tpl_args());
    }

    // Export only OWN (directly declared) alternatives; the importer will resolve the
    // base union separately and chain the inheritance. This keeps the KDI compact and
    // allows the consumer to reconstruct the full discriminant range via the base chain.
    for (auto& alt : un.alternatives()) {
        kdi::kdi_union_alternative ka;
        ka.name     = alt.name;
        ka.is_const = alt.is_const;
        if (alt.resolved_type) {
            ka.type = to_kdi_type(alt.resolved_type);
        }
        ku.alternatives.push_back(std::move(ka));
    }

    // LLVM struct type definition string
    if (auto st = un.get_struct_type()) {
        if (auto* llvm_st = st->get_llvm_type()) {
            std::string llvm_str;
            llvm::raw_string_ostream os(llvm_str);
            llvm_st->print(os);
            os.flush();
            ku.llvm_def = std::move(llvm_str);
        }
    }

    // Deposit into enclosing aggregate or namespace
    if (!_agg_stack.empty())
        _agg_stack.back()->nested_unions.push_back(std::move(ku));
    else if (!_ns_stack.empty())
        _ns_stack.back()->unions.push_back(std::move(ku));
}

void kdi_builder::visit_function(function& fn) {
    // Skip compiler-generated and internal functions
    if (fn.is_compiler_generated()) return;
    if (!is_exported(fn.get_visibility())) return;

    // Template definition: export as kdi_template_def, not as a regular function
    if (fn.is_template()) {
        if (!_ns_stack.empty() && fn.get_tpl_info()) {
            std::string fq = fn.get_fq_name();
            if (fq.empty() || fq == "::") {
                fq = _ns_stack.back()->fq_name.empty()
                     ? fn.get_short_name()
                     : _ns_stack.back()->fq_name + "::" + fn.get_short_name();
            }
            _ns_stack.back()->template_defs.push_back(
                build_template_def(fn.get_short_name(), fq, "function",
                                   fn.get_visibility(), *fn.get_tpl_info(), &fn));
        }
        return;
    }

    // For redirected functions, resolve the LLVM function via the redirect target's mangled name
    // (since the redirector is emitted as a GlobalAlias, not an llvm::Function).
    std::string llvm_lookup_name = fn.get_mangled_name();
    if (fn.is_redirected() && fn.get_redirect_target()) {
        llvm_lookup_name = fn.get_redirect_target()->get_mangled_name();
    }

    // Build template origin for concrete instantiations
    std::optional<kdi::kdi_template_origin> tpl_origin;
    if (fn.has_tpl_args()) {
        std::string inst_fq = fn.get_fq_name();
        std::string inst_name = fn.get_short_name();
        std::string base_fq = inst_fq;
        auto pos = base_fq.rfind(inst_name);
        if (pos != std::string::npos) {
            base_fq.replace(pos, inst_name.size(), fn.get_tpl_base_name());
        }
        tpl_origin = build_template_origin(fn.get_tpl_base_name(), base_fq, fn.get_tpl_args());
    }

    if (in_aggregate()) {
        // Member method
        kdi::kdi_method km;
        km.name            = fn.get_short_name();
        km.fq_name         = fn.get_fq_name();
        km.visibility      = to_kdi_vis(fn.get_visibility());
        km.is_static       = fn.is_static();
        km.is_const_member = fn.is_const_member();
        km.is_virtual      = fn.is_virtual();
        km.is_abstract     = fn.is_abstract_func();
        km.is_final        = fn.is_final_func();
        km.is_operator     = fn.is_operator();
        km.vtable_slot     = fn.get_vtable_slot();
        km.return_type     = to_kdi_type(std::const_pointer_cast<type>(fn.get_return_type()));
        km.params          = to_kdi_params(fn);
        km.mangled_name    = fn.get_mangled_name();
        {
            auto* llvm_fn = _ctx.module().getFunction(llvm_lookup_name);
            km.llvm_def = llvm_fn_prototype(llvm_fn);
        }
        km.template_origin = tpl_origin;
        // Export throws spec
        for (const auto& t : fn.get_throws_spec()) {
            km.throws_spec.push_back(to_kdi_type(std::const_pointer_cast<type>(t)));
        }
        km.doc = to_kdi_doc_function(fn);
        _agg_stack.back()->methods.push_back(std::move(km));
    } else {
        // Global function — deposit into current namespace
        if (_ns_stack.empty()) return;
        kdi::kdi_function kf;
        kf.name         = fn.get_short_name();
        kf.fq_name      = fn.get_fq_name();
        kf.visibility   = to_kdi_vis(fn.get_visibility());
        kf.is_static    = fn.is_static();
        kf.is_operator  = fn.is_operator();
        kf.return_type  = to_kdi_type(std::const_pointer_cast<type>(fn.get_return_type()));
        kf.params       = to_kdi_params(fn);
        kf.mangled_name = fn.get_mangled_name();
        {
            auto* llvm_fn = _ctx.module().getFunction(llvm_lookup_name);
            kf.llvm_def = llvm_fn_prototype(llvm_fn);
        }
        kf.template_origin = tpl_origin;
        // Export throws spec
        for (const auto& t : fn.get_throws_spec()) {
            kf.throws_spec.push_back(to_kdi_type(std::const_pointer_cast<type>(t)));
        }
        kf.doc = to_kdi_doc_function(fn);
        _ns_stack.back()->functions.push_back(std::move(kf));
    }
}

void kdi_builder::visit_constructor(constructor& ctor) {
    if (!in_aggregate()) return;
    if (!is_exported(ctor.get_visibility())) return;

    kdi::kdi_constructor kc;
    kc.visibility          = to_kdi_vis(ctor.get_visibility());
    kc.is_copy_constructor = ctor.is_copy_constructor();
    kc.is_defaulted        = ctor.is_defaulted();
    kc.is_deleted          = ctor.is_deleted();
    kc.params              = to_kdi_params(ctor);
    kc.mangled_name        = ctor.get_mangled_name();
    if (auto ctx = ctor.get_context())
        kc.mangled_name_c2 = mangler(ctx).mangle_constructor_c2(ctor);
    else
        kc.mangled_name_c2 = kc.mangled_name;
    {
        auto* llvm_fn = _ctx.module().getFunction(kc.mangled_name);
        kc.llvm_def = llvm_fn_prototype(llvm_fn);
    }
    kc.doc = to_kdi_doc_function(ctor);

    _agg_stack.back()->constructors.push_back(std::move(kc));
}

void kdi_builder::visit_destructor(destructor& dtor) {
    if (!in_aggregate()) return;
    if (!is_exported(dtor.get_visibility())) return;

    kdi::kdi_destructor kd;
    kd.visibility            = to_kdi_vis(dtor.get_visibility());
    kd.is_virtual            = dtor.is_virtual();
    kd.is_compiler_generated = dtor.is_compiler_generated();
    kd.vtable_slot           = dtor.get_vtable_slot();
    kd.mangled_name          = dtor.get_mangled_name();
    if (auto ctx = dtor.get_context())
        kd.mangled_name_d2 = mangler(ctx).mangle_destructor_d2(dtor);
    else
        kd.mangled_name_d2 = kd.mangled_name;
    {
        auto* llvm_fn = _ctx.module().getFunction(kd.mangled_name);
        kd.llvm_def = llvm_fn_prototype(llvm_fn);
    }
    kd.doc = to_kdi_doc_function(dtor);

    _agg_stack.back()->destructor = std::move(kd);
}

void kdi_builder::visit_global_variable_definition(global_variable_definition& var) {
    if (!is_exported(var.get_visibility())) return;

    kdi::kdi_variable kv;
    kv.name         = var.get_short_name();
    kv.fq_name      = var.get_fq_name();
    kv.visibility   = to_kdi_vis(var.get_visibility());
    kv.type         = to_kdi_type(std::const_pointer_cast<type>(var.get_type()));
    kv.is_const     = type::is_const(std::const_pointer_cast<type>(var.get_type()));
    kv.mangled_name = var.get_mangled_name();
    kv.doc          = to_kdi_doc_block(var);

    if (in_aggregate()) {
        // Static member variable
        _agg_stack.back()->static_vars.push_back(std::move(kv));
    } else if (!_ns_stack.empty()) {
        // Global variable
        _ns_stack.back()->variables.push_back(std::move(kv));
    }
}

void kdi_builder::visit_member_variable_definition(member_variable_definition& /*var*/) {
    // Member variables are handled by build_layout() via the LLVM struct field
    // order, not by direct visit, to preserve the physical layout and opaque
    // block grouping.  Nothing to do here.
}

// ─────────────────────────────────────────────────────────────────────────────
// Template export helpers
// ─────────────────────────────────────────────────────────────────────────────

kdi::kdi_template_origin kdi_builder::build_template_origin(
    const std::string& base_name,
    const std::string& fq_name,
    const std::vector<template_argument>& args) const
{
    kdi::kdi_template_origin origin;
    origin.base_name = base_name;

    // Strip leading "::" from fq_name for KDI convention
    if (fq_name.size() >= 2 && fq_name[0] == ':' && fq_name[1] == ':')
        origin.base_fq_name = fq_name.substr(2);
    else
        origin.base_fq_name = fq_name;

    for (auto& arg : args) {
        kdi::kdi_template_arg karg;
        if (arg.is_type()) {
            karg.type_arg = to_kdi_type(arg.type_arg);
        } else if (arg.is_value()) {
            // Serialize value as string representation
            karg.value_arg = std::visit([](auto&& v) -> std::string {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, std::monostate> || std::is_same_v<T, std::nullptr_t>) {
                    return "0";
                } else if constexpr (std::is_same_v<T, bool>) {
                    return v ? "true" : "false";
                } else if constexpr (std::is_same_v<T, std::string>) {
                    return "\"" + v + "\"";
                } else if constexpr (std::is_same_v<T, std::shared_ptr<k::model::aggregate_value>>) {
                    // For now, export aggregate values as their debug dump (not ideal, but placeholder)
                    // Full implementation would need to export structured field values
                    return v ? v->dump() : "<?null>";
                } else {
                    return std::to_string(v);
                }
            }, *arg.value_arg);
            // Also export the type of the value argument so the importer can
            // reconstruct the correct k::value_type variant.
            karg.value_type = std::visit([](auto&& v) -> kdi::kdi_type {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, bool>)
                    return {kdi::kdi_bool_type{}};
                else if constexpr (std::is_same_v<T, char>)
                    return {kdi::kdi_char_type{}};
                else if constexpr (std::is_same_v<T, unsigned char>)
                    return kdi::kdi_type::make_int(8, false);
                else if constexpr (std::is_same_v<T, short>)
                    return kdi::kdi_type::make_int(16, true);
                else if constexpr (std::is_same_v<T, unsigned short>)
                    return kdi::kdi_type::make_int(16, false);
                else if constexpr (std::is_same_v<T, int>)
                    return kdi::kdi_type::make_int(32, true);
                else if constexpr (std::is_same_v<T, unsigned int>)
                    return kdi::kdi_type::make_int(32, false);
                else if constexpr (std::is_same_v<T, long>)
                    return kdi::kdi_type::make_int(64, true);
                else if constexpr (std::is_same_v<T, unsigned long>)
                    return kdi::kdi_type::make_int(64, false);
                else if constexpr (std::is_same_v<T, long long>)
                    return kdi::kdi_type::make_int(128, true);
                else if constexpr (std::is_same_v<T, unsigned long long>)
                    return kdi::kdi_type::make_int(128, false);
                else if constexpr (std::is_same_v<T, float>)
                    return kdi::kdi_type::make_float(32);
                else if constexpr (std::is_same_v<T, double>)
                    return kdi::kdi_type::make_float(64);
                else
                    return kdi::kdi_type::make_int(32, true);
            }, *arg.value_arg);
        }
        origin.args.push_back(std::move(karg));
    }
    return origin;
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper: resolve a relative k::name to a FQ name by walking the model tree
// starting from a given scope element.
// ─────────────────────────────────────────────────────────────────────────────

static std::string resolve_target_fq_name(const k::name& target_name,
                                           const element* scope) {
    if (target_name.empty()) return {};

    // Walk up the scope chain to find a namespace or aggregate that contains
    // the first component of target_name.
    for (auto current = scope; current; ) {
        // Try namespace scope
        if (auto ns_ptr = dynamic_cast<const ns*>(current)) {
            // Try to walk down through target_name components
            auto child_ns = ns_ptr->get_child_namespace(target_name.front());
            if (child_ns) {
                if (target_name.size() == 1) {
                    // target is a namespace itself
                    return k_source_emitter::fq_name_for_source(child_ns->get_fq_name());
                }
                // Walk deeper
                std::shared_ptr<const element> elem = child_ns;
                for (size_t i = 1; i < target_name.size(); ++i) {
                    if (auto ns_e = std::dynamic_pointer_cast<const ns>(elem)) {
                        auto deeper_ns = ns_e->get_child_namespace(target_name[i]);
                        if (deeper_ns) {
                            elem = deeper_ns;
                            if (i == target_name.size() - 1) {
                                return k_source_emitter::fq_name_for_source(deeper_ns->get_fq_name());
                            }
                            continue;
                        }
                    }
                    if (auto ah = std::dynamic_pointer_cast<const aggregate_holder>(elem)) {
                        if (auto agg = ah->get_aggregate(target_name[i])) {
                            return k_source_emitter::fq_name_for_source(agg->get_fq_name());
                        }
                    }
                    break;
                }
            }
            // Try aggregate directly in this namespace
            if (auto ah = dynamic_cast<const aggregate_holder*>(current)) {
                if (auto agg = ah->get_aggregate(target_name.front())) {
                    if (target_name.size() == 1) {
                        return k_source_emitter::fq_name_for_source(agg->get_fq_name());
                    }
                }
            }
        }

        // Try aggregate scope
        if (auto ah = dynamic_cast<const aggregate_holder*>(current)) {
            if (auto agg = ah->get_aggregate(target_name.front())) {
                if (target_name.size() == 1) {
                    return k_source_emitter::fq_name_for_source(agg->get_fq_name());
                }
            }
        }

        // Move up to parent
        auto parent = dynamic_cast<const element*>(current)->parent<const element>();
        current = parent.get();
    }
    return {};
}

// ─────────────────────────────────────────────────────────────────────────────
// Helper: build a map of using-alias names → FQ type names from the scope chain
// of a template entity.  This map allows the emitter to de-alias unresolved
// types that appear in template bodies (function bodies are not visited by the
// type resolver and thus may still contain alias names).
// ─────────────────────────────────────────────────────────────────────────────

static std::unordered_map<std::string, std::string>
build_using_alias_map(const element* entity) {
    std::unordered_map<std::string, std::string> map;
    if (!entity) return map;

    for (auto current = entity->parent<const element>(); current;
         current = current->parent<const element>()) {
        auto uh = dynamic_cast<const using_holder*>(current.get());
        if (!uh) continue;
        for (const auto& dir : uh->get_using_directives()) {
            if (dir.has_alias() && !dir.is_namespace()) {
                // Aliased using: using Alias = X::Y::Z;
                auto fq = resolve_target_fq_name(dir.target_name, current.get());
                if (!fq.empty()) {
                    map[*dir.alias_name] = fq;
                }
            } else if (dir.is_namespace() && !dir.has_alias()) {
                // Anonymous namespace using: using namespace X;
                // Find the target namespace and add all its types to the map
                auto fq = resolve_target_fq_name(dir.target_name, current.get());
                if (fq.empty()) continue;

                // Find the actual namespace element
                auto ns_ptr = dynamic_cast<const ns*>(current.get());
                if (!ns_ptr) continue;
                auto target_ns = ns_ptr->get_child_namespace(dir.target_name.front());
                // Walk deeper for multi-component target names
                for (size_t i = 1; target_ns && i < dir.target_name.size(); ++i) {
                    target_ns = target_ns->get_child_namespace(dir.target_name[i]);
                }
                if (!target_ns) continue;

                // Add all aggregates in the target namespace to the map
                if (auto ah = dynamic_cast<const aggregate_holder*>(target_ns.get())) {
                    for (auto& child : target_ns->get_children()) {
                        if (auto agg = std::dynamic_pointer_cast<const aggregate>(child)) {
                            map[agg->get_short_name()] =
                                k_source_emitter::fq_name_for_source(agg->get_fq_name());
                        }
                    }
                }
            }
        }
    }
    return map;
}

kdi::kdi_template_def kdi_builder::build_template_def(
    const std::string& name,
    const std::string& fq_name,
    const std::string& entity_kind,
    visibility vis,
    const tpl_info& ti,
    const element* entity) const
{
    kdi::kdi_template_def def;
    def.name = name;
    // Strip leading "::"
    if (fq_name.size() >= 2 && fq_name[0] == ':' && fq_name[1] == ':')
        def.fq_name = fq_name.substr(2);
    else
        def.fq_name = fq_name;
    def.entity_kind = entity_kind;
    def.visibility = (vis == PROTECTED) ? "protected" : "public";

    // Record the true origin module of a re-exported template (empty when this
    // template is owned by the module producing the KDI). Lets a downstream
    // importer tag the template with its real origin so every module that
    // instantiates it synthesises the same origin-absolute symbol (COMDAT dedup).
    def.origin_module = ti.origin_module_ns_fq;

    bool is_generic = ti.is_generic;
    if (entity) {
        if (auto agg_ptr = dynamic_cast<const aggregate*>(entity)) {
            is_generic = is_generic || agg_ptr->is_generic();
            if (!is_generic) {
                if (auto ast = agg_ptr->get_ast_aggregate_decl()) {
                    is_generic = ast->is_generic;
                }
            }
        } else if (auto fn_ptr = dynamic_cast<const function*>(entity)) {
            is_generic = is_generic || fn_ptr->is_generic();
            if (!is_generic) {
                if (auto ast = fn_ptr->get_ast_function_decl()) {
                    is_generic = ast->is_generic;
                }
            }
        }
    }
    if (!is_generic && !ti.is_imported_signature_only && ti.source_text.empty()) {
        is_generic = true;
    }
    def.is_generic = is_generic;

    if (!is_generic) {
        // Determine if using-alias resolution is needed (emitter resolves aliases
        // to their canonical names so the consumer can find the types).
        std::unordered_map<std::string, std::string> alias_map;
        if (entity) {
            alias_map = build_using_alias_map(entity);
        }

        if (!ti.source_text.empty() && alias_map.empty()) {
            // Prefer raw source_text (captured verbatim from the parser) when no
            // alias resolution is needed. It preserves annotations, member template
            // clauses, and other syntax that k_source_emitter may not reconstruct.
            def.source = ti.source_text;
        } else {
            // Use model-based reconstruction (resolves using-aliases to canonical names)
            // with fallback to raw source_text.
            std::string emitted;
            if (entity) {
                k_source_emitter emitter;
                if (!alias_map.empty()) {
                    emitter.set_alias_map(std::move(alias_map));
                }
                if (auto agg_ptr = dynamic_cast<const aggregate*>(entity)) {
                    emitted = emitter.emit_template_aggregate(*agg_ptr);
                } else if (auto fn_ptr = dynamic_cast<const function*>(entity)) {
                    emitted = emitter.emit_template_function(*fn_ptr);
                }
            }
            if (!emitted.empty()) {
                def.source = std::move(emitted);
            } else {
                def.source = ti.source_text;
            }
        }
    } else {
        if (auto agg_ptr = dynamic_cast<const aggregate*>(entity)) {
            // Generic aggregates remain signature-only.
            def.source.clear();
            def.aggregate_signature = std::make_shared<kdi::kdi_aggregate>(
                build_generic_template_aggregate_signature(*agg_ptr, ti, def.fq_name));
        } else if (auto fn_ptr = dynamic_cast<const function*>(entity)) {
            // Generic function templates keep source so importing modules can instantiate bodies.
            std::string emitted;
            if (entity) {
                k_source_emitter emitter;
                auto alias_map = build_using_alias_map(entity);
                if (!alias_map.empty()) {
                    emitter.set_alias_map(std::move(alias_map));
                }
                emitted = emitter.emit_template_function(*fn_ptr);
            }
            if (!emitted.empty()) {
                def.source = std::move(emitted);
            } else {
                def.source = ti.source_text;
            }
            if (def.source.empty()) {
                // Fallback for legacy/partial importers.
                def.function_signature = std::make_shared<kdi::kdi_function>(
                    build_generic_template_function_signature(*fn_ptr, ti, def.fq_name));
            }
        } else {
            def.source.clear();
        }
    }

    // Always populate a structured signature alongside the raw source (when
    // not already built above for the generic-template case). This lets
    // documentation tooling (kditool docgen) render a template's fields,
    // constructors and methods the same way as a regular (non-template)
    // aggregate/function, with template-parameter-dependent types tagged as
    // distinct entities (kdi_template_param_ref / kdi_generic_ref_type)
    // rather than only a raw source dump. `def.source` itself is left
    // untouched — it remains the authoritative form used by the compiler to
    // re-parse/re-instantiate imported templates cross-module.
    if (!def.aggregate_signature) {
        if (auto agg_ptr = dynamic_cast<const aggregate*>(entity)) {
            def.aggregate_signature = std::make_shared<kdi::kdi_aggregate>(
                build_generic_template_aggregate_signature(*agg_ptr, ti, def.fq_name));
        }
    }
    if (!def.function_signature) {
        if (auto fn_ptr = dynamic_cast<const function*>(entity)) {
            def.function_signature = std::make_shared<kdi::kdi_function>(
                build_generic_template_function_signature(*fn_ptr, ti, def.fq_name));
        }
    }

    for (auto& param : ti.params) {
        kdi::kdi_template_param kparam;
        kparam.name = param.name;

        switch (param.kind) {
            case template_param_kind::TYPENAME:  kparam.kind = "typename";  break;
            case template_param_kind::STRUCT:    kparam.kind = "struct";    break;
            case template_param_kind::CLASS:     kparam.kind = "class";     break;
            case template_param_kind::INTERFACE: kparam.kind = "interface"; break;
            case template_param_kind::VALUE:     kparam.kind = "value";     break;
        }

        if (param.constraint_type)
            kparam.constraint_type = to_kdi_signature_type(param.constraint_type, ti);
        if (param.default_type)
            kparam.default_type = to_kdi_signature_type(param.default_type, ti);
        if (param.value_type)
            kparam.value_type = to_kdi_signature_type(param.value_type, ti);
        if (param.default_value.has_value()) {
            kparam.default_value = std::visit([](auto&& v) -> std::string {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, std::monostate> || std::is_same_v<T, std::nullptr_t>) {
                    return "0";
                } else if constexpr (std::is_same_v<T, bool>) {
                    return v ? "true" : "false";
                } else if constexpr (std::is_same_v<T, std::string>) {
                    return "\"" + v + "\"";
                } else if constexpr (std::is_same_v<T, std::shared_ptr<k::model::aggregate_value>>) {
                    return v ? v->dump() : "<?null>";
                } else {
                    return std::to_string(v);
                }
            }, *param.default_value);
        }

        def.params.push_back(std::move(kparam));
    }
    return def;
}

// ─────────────────────────────────────────────────────────────────────────────
// Convenience free function
// ─────────────────────────────────────────────────────────────────────────────

kdi::kdi_file build_kdi(context& ctx,
                         const unit& u,
                         const std::string& lib_path,
                         const std::string& compiler_ver) {
    kdi_builder builder(ctx, lib_path, compiler_ver);
    // unit::accept() takes a non-const visitor; the unit itself is traversed
    // read-only by kdi_builder, but the visitor interface is non-const.
    const_cast<unit&>(u).accept(builder);
    return builder.take();
}

} // namespace k::model
