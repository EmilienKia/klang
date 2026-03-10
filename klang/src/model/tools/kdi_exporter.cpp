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

#include "../model.hpp"
#include "../type.hpp"
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

namespace k::model {

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers
// ─────────────────────────────────────────────────────────────────────────────

namespace {

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

kdi::kdi_type kdi_builder::to_kdi_type(const std::shared_ptr<type>& t) const {
    if (!t) return kdi::kdi_type::make_void();

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
    if (auto pt = std::dynamic_pointer_cast<pinned_type>(t)) {
        kdi::kdi_pinned_type k; k.inner = std::make_shared<kdi::kdi_type>(to_kdi_type(pt->get_subtype()));
        return kdi::kdi_type{std::move(k)};
    }
    if (auto ot = std::dynamic_pointer_cast<owner_type>(t)) {
        // owner (!) exported as kdi_ptr_type for now (KDI has no owner type yet)
        kdi::kdi_ptr_type k; k.inner = std::make_shared<kdi::kdi_type>(to_kdi_type(ot->get_subtype()));
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
    if (auto frt = std::dynamic_pointer_cast<function_reference_type>(t)) {
        kdi::kdi_fn_ref_type k;
        k.ret = std::make_shared<kdi::kdi_type>(to_kdi_type(frt->get_return_type()));
        for (auto& p : frt->get_parameter_types())
            k.params.push_back(std::make_shared<kdi::kdi_type>(to_kdi_type(p)));
        return kdi::kdi_type{std::move(k)};
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
    return kdi::kdi_type::make_void();
}

std::vector<kdi::kdi_param> kdi_builder::to_kdi_params(const function& fn) const {
    std::vector<kdi::kdi_param> result;
    for (auto& p : fn.parameters()) {
        if (p->get_short_name() == "this" || p->get_short_name() == "__parent__") continue;
        kdi::kdi_param kp;
        kp.name = p->get_short_name();
        kp.type = to_kdi_type(p->get_type());
        result.push_back(std::move(kp));
    }
    return result;
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
            std::string bname = fname.substr(7, fname.size() - 9);
            std::string bfq   = bname;
            for (auto& bs : agg.get_bases())
                if (bs.sanitised_name() == bname && bs.base) { bfq = bs.base->get_fq_name(); break; }
            kdi::kdi_layout_base_subobject bso;
            bso.llvm_field_index = fi;
            bso.base_fq_name     = std::move(bfq);
            result.push_back(std::move(bso));
            continue;
        }
        // ── virtual base pointer (__vbptr_X__) ────────────────────────
        if (fname.rfind("__vbptr_", 0) == 0 && fname.size() > 10) {
            flush();
            std::string vbname = fname.substr(8, fname.size() - 10);
            std::string vbfq   = vbname;
            for (auto& bs : agg.get_all_bases())
                if (bs.is_virtual && bs.base && bs.base->get_short_name() == vbname)
                    { vbfq = bs.base->get_fq_name(); break; }
            kdi::kdi_layout_vbptr vb;
            vb.llvm_field_index = fi;
            vb.vbase_fq_name    = std::move(vbfq);
            result.push_back(std::move(vb));
            continue;
        }
        // ── virtual base subobject (__vbase_X__) ──────────────────────
        if (fname.rfind("__vbase_", 0) == 0 && fname.size() > 10) {
            flush();
            std::string vbname = fname.substr(8, fname.size() - 10);
            std::string vbfq   = vbname;
            for (auto& bs : agg.get_all_bases())
                if (bs.is_virtual && bs.base && bs.base->get_short_name() == vbname)
                    { vbfq = bs.base->get_fq_name(); break; }
            kdi::kdi_layout_vbase_subobject vbs;
            vbs.llvm_field_index = fi;
            vbs.vbase_fq_name    = std::move(vbfq);
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

    // get_vtable() only exists on klass
    const auto* kl = dynamic_cast<const klass*>(&agg);
    if (!kl) return std::nullopt;
    auto vt = kl->get_vtable();
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

    return kagg;
}

void kdi_builder::visit_aggregate_body(aggregate& agg, kdi::kdi_aggregate& kagg) {
    // Push the aggregate onto the stack so that visit_constructor,
    // visit_destructor, visit_function and visit_member_variable_definition
    // all deposit into kagg.
    _agg_stack.push_back(&kagg);

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

void kdi_builder::visit_namespace(ns& n) {
    kdi::kdi_namespace kns;
    kns.name    = n.get_short_name();
    kns.fq_name = n.get_fq_name();

    // Push the new namespace as the current output target
    _ns_stack.push_back(&kns);

    for (auto& child : n.get_children())
        child->accept(*this);

    _ns_stack.pop_back();

    // Deposit into parent namespace or root
    if (!_ns_stack.empty())
        _ns_stack.back()->namespaces.push_back(std::move(kns));
    else
        _file.unit.root_ns = std::move(kns);
}

void kdi_builder::visit_structure(structure& s) {
    if (!is_exported(s.get_visibility())) return;

    kdi::kdi_aggregate kagg = begin_aggregate(s);
    visit_aggregate_body(s, kagg);

    // Deposit into parent namespace or enclosing aggregate
    if (!_agg_stack.empty())
        _agg_stack.back()->nested.push_back(std::move(kagg));
    else if (!_ns_stack.empty())
        _ns_stack.back()->aggregates.push_back(std::move(kagg));
}

void kdi_builder::visit_klass(klass& k) {
    if (!is_exported(k.get_visibility())) return;

    kdi::kdi_aggregate kagg = begin_aggregate(k);
    visit_aggregate_body(k, kagg);

    if (!_agg_stack.empty())
        _agg_stack.back()->nested.push_back(std::move(kagg));
    else if (!_ns_stack.empty())
        _ns_stack.back()->aggregates.push_back(std::move(kagg));
}

void kdi_builder::visit_interface(interface& i) {
    if (!is_exported(i.get_visibility())) return;

    kdi::kdi_aggregate kagg = begin_aggregate(i);
    visit_aggregate_body(i, kagg);

    if (!_agg_stack.empty())
        _agg_stack.back()->nested.push_back(std::move(kagg));
    else if (!_ns_stack.empty())
        _ns_stack.back()->aggregates.push_back(std::move(kagg));
}

void kdi_builder::visit_function(function& fn) {
    // Skip compiler-generated and internal functions
    if (fn.is_compiler_generated()) return;
    if (!is_exported(fn.get_visibility())) return;

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
        km.vtable_slot     = fn.get_vtable_slot();
        km.return_type     = to_kdi_type(std::const_pointer_cast<type>(fn.get_return_type()));
        km.params          = to_kdi_params(fn);
        km.mangled_name    = fn.get_mangled_name();
        {
            auto* llvm_fn = _ctx.module().getFunction(fn.get_mangled_name());
            km.llvm_def = llvm_fn_prototype(llvm_fn);
        }
        _agg_stack.back()->methods.push_back(std::move(km));
    } else {
        // Global function — deposit into current namespace
        if (_ns_stack.empty()) return;
        kdi::kdi_function kf;
        kf.name         = fn.get_short_name();
        kf.fq_name      = fn.get_fq_name();
        kf.visibility   = to_kdi_vis(fn.get_visibility());
        kf.is_static    = fn.is_static();
        kf.return_type  = to_kdi_type(std::const_pointer_cast<type>(fn.get_return_type()));
        kf.params       = to_kdi_params(fn);
        kf.mangled_name = fn.get_mangled_name();
        {
            auto* llvm_fn = _ctx.module().getFunction(fn.get_mangled_name());
            kf.llvm_def = llvm_fn_prototype(llvm_fn);
        }
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

    _agg_stack.back()->constructors.push_back(std::move(kc));
}

void kdi_builder::visit_destructor(destructor& dtor) {
    if (!in_aggregate()) return;
    if (!is_exported(dtor.get_visibility())) return;

    kdi::kdi_destructor kd;
    kd.visibility            = to_kdi_vis(dtor.get_visibility());
    kd.is_virtual            = dtor.is_virtual();
    kd.is_compiler_generated = dtor.is_compiler_generated();
    kd.mangled_name          = dtor.get_mangled_name();
    if (auto ctx = dtor.get_context())
        kd.mangled_name_d2 = mangler(ctx).mangle_destructor_d2(dtor);
    else
        kd.mangled_name_d2 = kd.mangled_name;
    {
        auto* llvm_fn = _ctx.module().getFunction(kd.mangled_name);
        kd.llvm_def = llvm_fn_prototype(llvm_fn);
    }

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

