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
#include "resolvers_materializer.hpp"
#include "gen_helpers.hpp"
#include "../model/imported.hpp"
#include "../model/statements.hpp"
#include "../model/expressions.hpp"
#include "../model/template.hpp"
#include "../model/template_instantiator.hpp"
#include "../parse/ast.hpp"
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/DataLayout.h>
#include <queue>
#include <set>
#include <unordered_set>
#include <functional>
#include "../errors.hpp"

namespace k::model::gen {

namespace {

// Collect all direct and transitive bases of st in BFS order, deduplicated by pointer.
std::vector<std::shared_ptr<aggregate>>
vbases_bfs(const aggregate& st) {
    std::vector<std::shared_ptr<aggregate>> result;
    std::unordered_set<const aggregate*> seen;
    std::queue<std::shared_ptr<aggregate>> q;
    for (auto& bs : st.get_bases()) {
        if (bs.base && !seen.count(bs.base.get())) {
            seen.insert(bs.base.get());
            q.push(bs.base);
            result.push_back(bs.base);
        }
    }
    while (!q.empty()) {
        auto cur = q.front(); q.pop();
        for (auto& bs : cur->get_bases()) {
            if (bs.base && !seen.count(bs.base.get())) {
                seen.insert(bs.base.get());
                q.push(bs.base);
                result.push_back(bs.base);
            }
        }
    }
    return result;
}

// True if two non-static member functions share the same virtual signature.
bool mat_same_virtual_sig(const function& a, const function& b) {
    if (a.get_short_name() != b.get_short_name()) return false;
    if (a.is_const_member() != b.is_const_member()) return false;
    if (a.get_parameter_size() != b.get_parameter_size()) return false;
    auto type_match = [](const std::shared_ptr<type>& x, const std::shared_ptr<type>& y,
                         const function& owner_x, const function& owner_y) -> bool {
        auto nx = type::canonical(x);
        auto ny = type::canonical(y);
        if (type::are_equal(nx, ny)) return true;

        auto ux = std::dynamic_pointer_cast<unresolved_type>(nx);
        auto uy = std::dynamic_pointer_cast<unresolved_type>(ny);
        if (ux && uy) {
            auto unresolved_name = [](const unresolved_type& u) {
                return u.type_id().to_string();
            };
            auto unresolved_short_name = [&](const unresolved_type& u) {
                std::string name = u.type_id().to_string();
                if (auto pos = name.rfind("::"); pos != std::string::npos) {
                    name = name.substr(pos + 2);
                }
                return name;
            };
            const std::string nx_name = unresolved_name(*ux);
            const std::string ny_name = unresolved_name(*uy);
            const std::string sx = unresolved_short_name(*ux);
            const std::string sy = unresolved_short_name(*uy);
            const bool x_qualified = nx_name.find("::") != std::string::npos;
            const bool y_qualified = ny_name.find("::") != std::string::npos;
            if (sx == sy && x_qualified != y_qualified) {
                return true;
            }
        }
        if (ux && !ux->is_resolved() && !uy) {
            if (auto ctx = const_cast<function&>(owner_x).get_context()) {
                nx = type::canonical(ctx->resolve_type(nx));
            }
        } else if (uy && !uy->is_resolved() && !ux) {
            if (auto ctx = const_cast<function&>(owner_y).get_context()) {
                ny = type::canonical(ctx->resolve_type(ny));
            }
        } else if (ux && uy && !ux->is_resolved() && !uy->is_resolved()) {
            const bool x_looks_instantiation = ux->has_template_args() || ux->type_id().to_string().find("__") != std::string::npos;
            const bool y_looks_instantiation = uy->has_template_args() || uy->type_id().to_string().find("__") != std::string::npos;
            if (x_looks_instantiation) {
                if (auto ctx = const_cast<function&>(owner_x).get_context()) {
                    nx = type::canonical(ctx->resolve_type(nx));
                }
            }
            if (y_looks_instantiation) {
                if (auto ctx = const_cast<function&>(owner_y).get_context()) {
                    ny = type::canonical(ctx->resolve_type(ny));
                }
            }
        }

        if (auto urx = std::dynamic_pointer_cast<unresolved_type>(nx); urx && urx->is_resolved()) {
            nx = type::canonical(urx->get_resolved());
        }
        if (auto ury = std::dynamic_pointer_cast<unresolved_type>(ny); ury && ury->is_resolved()) {
            ny = type::canonical(ury->get_resolved());
        }

        if (type::are_equal(nx, ny)) return true;

        std::function<bool(const std::shared_ptr<type>&, const std::shared_ptr<type>&)> is_semantic_template_match;
        is_semantic_template_match =
            [&](const std::shared_ptr<type>& a, const std::shared_ptr<type>& b) -> bool {
                if (!a || !b) return false;
                if (type::are_equal(a, b)) return true;

                auto recurse_if_same_wrapper = [&](auto pred) -> bool {
                    if (pred(a) && pred(b)) {
                        return is_semantic_template_match(a->get_subtype(), b->get_subtype());
                    }
                    return false;
                };
                if (recurse_if_same_wrapper(type::is_reference)) return true;
                if (recurse_if_same_wrapper(type::is_pointer)) return true;
                if (recurse_if_same_wrapper(type::is_link)) return true;
                if (recurse_if_same_wrapper(type::is_view)) return true;
                if (recurse_if_same_wrapper(type::is_owner)) return true;
                if (recurse_if_same_wrapper(type::is_drain)) return true;
                if (recurse_if_same_wrapper(type::is_const)) return true;
                if (recurse_if_same_wrapper(type::is_array)) return true;

                auto uu_a = std::dynamic_pointer_cast<unresolved_type>(a);
                auto uu_b = std::dynamic_pointer_cast<unresolved_type>(b);
                if (uu_a && uu_b) {
                    auto full = [](const unresolved_type& u) {
                        return u.type_id().to_string();
                    };
                    auto short_name = [&](const unresolved_type& u) {
                        std::string name = full(u);
                        if (auto pos = name.rfind("::"); pos != std::string::npos) {
                            name = name.substr(pos + 2);
                        }
                        return name;
                    };
                    const std::string a_full = full(*uu_a);
                    const std::string b_full = full(*uu_b);
                    const std::string a_short = short_name(*uu_a);
                    const std::string b_short = short_name(*uu_b);
                    if (a_short == b_short
                        && ((a_full.find("::") != std::string::npos)
                            != (b_full.find("::") != std::string::npos))) {
                        return true;
                    }
                }

                auto u = std::dynamic_pointer_cast<unresolved_type>(a);
                auto s = std::dynamic_pointer_cast<struct_type>(b);
                if (!u || !s) return false;
                auto st = s->get_struct();
                if (!st || !st->has_tpl_args()) return false;

                std::string unresolved_name = u->type_id().to_string();
                if (auto pos = unresolved_name.rfind("::"); pos != std::string::npos) {
                    unresolved_name = unresolved_name.substr(pos + 2);
                }

                std::string tpl_name = st->get_tpl_base_name();
                tpl_name += "<";
                const auto& tpl_args = st->get_tpl_args();
                for (size_t i = 0; i < tpl_args.size(); ++i) {
                    if (i != 0) tpl_name += ",";
                    if (!tpl_args[i].is_type() || !tpl_args[i].type_arg) return false;
                    tpl_name += tpl_args[i].type_arg->to_string();
                }
                tpl_name += ">";
                return unresolved_name == tpl_name;
            };
        if (is_semantic_template_match(nx, ny) || is_semantic_template_match(ny, nx)) return true;

        return nx && ny && nx->to_string() == ny->to_string();
    };
    for (size_t i = 0; i < a.get_parameter_size(); ++i) {
        auto ta = std::const_pointer_cast<type>(a.get_parameter(i)->get_type());
        auto tb = std::const_pointer_cast<type>(b.get_parameter(i)->get_type());
        if (!type_match(ta, tb, a, b)) return false;
    }
    auto ra = std::const_pointer_cast<type>(a.get_return_type());
    auto rb = std::const_pointer_cast<type>(b.get_return_type());
    if (bool(ra) != bool(rb)) return false;
    if (ra && rb && !type_match(ra, rb, a, b)) return false;
    return true;
}

} // anonymous namespace

// model_materializer


void model_materializer::materialize() {
    trace("[model_materializer::materialize] begin");
    visit_unit(_unit);
    trace("[model_materializer::materialize] done");
}

void model_materializer::visit_unit(unit& /*u*/) {
    visit_namespace(*_unit.get_root_namespace());
}

void model_materializer::visit_namespace(ns& n) {
    for (size_t i = 0; i < n.get_children().size(); ++i) {
        n.get_children()[i]->accept(*this);
    }
}

void model_materializer::visit_aggregate(aggregate& st) {
    // Skip template definitions — they are not instantiated yet.
    if (st.is_template()) return;

    // Visit nested aggregates first (depth-first)
    for (auto& child : st.get_children()) {
        if (auto nested = std::dynamic_pointer_cast<aggregate>(child)) {
            nested->accept(*this);
        }
    }
}

void model_materializer::visit_klass(klass& kl) {
    trace("[model_materializer::visit_klass] '{}'", {kl.get_short_name()});
    // Recurse into nested aggregates
    visit_aggregate(kl);

    if (!kl.has_vtable()) return;

    debug("[model_materializer::visit_klass] '{}' has vtable, validating and computing secondary specs", {kl.get_short_name()});

    // 1. Validate vtable consistency
    validate_vtable(kl);

    // 2. Compute secondary vtable thunk specs (requires LLVM struct types to exist)
    compute_secondary_vtable_specs(kl);
}

void model_materializer::visit_interface(interface& iface) {
    visit_klass(iface);
}

bool model_materializer::validate_vtable(klass& kl) {
    auto vt = kl.get_vtable();
    if (!vt) return true;

    lex::opt_any_lexeme kl_lexeme;
    if (auto ast = kl.get_ast_aggregate_decl()) kl_lexeme = lex::any_lexeme{ast->name};

    bool ok = true;
    for (auto& entry : vt->entries) {
        if (!entry.func) continue;
        // If the slot is still occupied by an abstract function and the class is NOT abstract,
        // that is a compilation error (should have been caught by symbol_resolver, but we
        // double-check here as a defensive measure).
        if (entry.func->is_abstract_func() && !kl.is_abstract()) {
            throw_error(static_cast<unsigned int>(k::diag::structure_diag::ERR_INHERITED_ABSTRACT_NOT_IMPL),
                kl_lexeme,
                "class '{}' must implement abstract method '{}' (introduced in '{}') "
                "or be declared 'abstract'",
                {kl.get_short_name(),
                 entry.func->get_short_name(),
                 entry.introducing_func && entry.introducing_func->get_owner()
                     ? entry.introducing_func->get_owner()->get_short_name()
                     : "?"});
            ok = false;
        }
    }

    // Mirror the diamond secondary-base sweep from symbol_resolver::visit_klass.
    // This defensive pass catches abstract methods reachable only through secondary bases
    // in a diamond-shaped interface graph — they are never in vt->entries so the loop
    // above cannot see them.
    if (ok && !kl.is_abstract()) {
        auto has_concrete = [&](const function& sig) -> bool {
            for (auto& e : vt->entries) {
                if (!e.func || e.func->is_abstract_func()) continue;
                if (e.introducing_func && mat_same_virtual_sig(sig, *e.introducing_func))
                    return true;
                if (mat_same_virtual_sig(sig, *e.func))
                    return true;
            }
            return false;
        };

        auto make_key = [](const function& f) -> std::string {
            std::string key = f.get_short_name();
            key += f.is_const_member() ? "|c|" : "||";
            for (size_t i = 0; i < f.get_parameter_size(); ++i) {
                auto t = f.get_parameter(i)->get_type();
                key += (t ? t->to_string() : "?") + ",";
            }
            return key;
        };

        std::unordered_set<std::string> reported;
        for (auto& base : vbases_bfs(kl)) {
            if (auto pk = std::dynamic_pointer_cast<klass>(base)) {
                if (!pk->has_vtable()) continue;
                for (auto& sec_entry : pk->get_vtable()->entries) {
                    if (!sec_entry.func || !sec_entry.func->is_abstract_func()) continue;
                    const function& sig = sec_entry.introducing_func
                        ? *sec_entry.introducing_func : *sec_entry.func;
                    if (has_concrete(sig)) continue;
                    if (!reported.insert(make_key(sig)).second) continue;
                    throw_error(static_cast<unsigned int>(k::diag::structure_diag::ERR_INHERITED_ABSTRACT_NOT_IMPL),
                        kl_lexeme,
                        "class '{}' inherits unimplemented abstract method '{}' from '{}' "
                        "(reachable only through a secondary base in a diamond-inheritance graph) "
                        "but is not declared 'abstract'",
                        {kl.get_short_name(), sig.get_short_name(),
                         sig.get_owner() ? sig.get_owner()->get_short_name() : "?"});
                    ok = false;
                }
            } else if (auto imp = std::dynamic_pointer_cast<imported_aggregate>(base)) {
                if (!imp->has_vtable()) continue;
                for (auto& imp_entry : imp->get_vtable()->entries) {
                    if (!imp_entry.func || !imp_entry.func->is_abstract_func()) continue;
                    const function& sig = imp_entry.introducing_func
                        ? *imp_entry.introducing_func : *imp_entry.func;
                    if (has_concrete(sig)) continue;
                    if (!reported.insert(make_key(sig)).second) continue;
                    throw_error(static_cast<unsigned int>(k::diag::structure_diag::ERR_INHERITED_ABSTRACT_NOT_IMPL),
                        kl_lexeme,
                        "class '{}' inherits unimplemented abstract method '{}' from '{}' "
                        "(reachable only through a secondary base in a diamond-inheritance graph) "
                        "but is not declared 'abstract'",
                        {kl.get_short_name(), sig.get_short_name(),
                         sig.get_owner() ? sig.get_owner()->get_short_name() : "?"});
                    ok = false;
                }
            }
        }
    }

    return ok;
}

/**
 * Compute secondary vtable thunk descriptors for a class with multiple base classes.
 *
 * Steps:
 *   1. Walk all non-virtual sub-objects transitively, computing cumulative byte offsets.
 *   2. For each sub-object with a vtable, build a secondary_vtable_spec with thunk_info
 *      records: slot index, real function, this-adjustment, and needs_thunk flag.
 *   3. Handle virtual bases separately via __vbase_X__ fields.
 *
 * Populates vtable_layout::secondary_vtables.
 */
void model_materializer::compute_secondary_vtable_specs(klass& kl) {
    auto vt = kl.get_vtable();
    if (!vt) return;

    // Clear any previously computed specs (idempotency)
    vt->secondary_vtables.clear();

    auto kl_struct_type = kl.get_struct_type();
    if (!kl_struct_type) return;

    llvm::StructType* kl_llvm_type = llvm::cast_or_null<llvm::StructType>(
        kl_struct_type->get_llvm_type());
    if (!kl_llvm_type) return;

    constexpr size_t PTR_SIZE = 8; // bytes, 64-bit assumption

    // Helper: compute byte offset of a named field in an LLVM struct type (safe estimation when bodies are not yet set)
    auto field_byte_offset = [&](llvm::StructType* sty, unsigned field_idx) -> size_t {
        size_t off = 0;
        for (unsigned fi = 0; sty && !sty->isOpaque() && fi < field_idx && fi < sty->getNumElements(); ++fi) {
            llvm::Type* ft = sty->getElementType(fi);
            if (!ft) { off += PTR_SIZE; continue; }
            if (ft->isPointerTy())      off += PTR_SIZE;
            else if (ft->isIntegerTy()) off += (ft->getIntegerBitWidth() + 7) / 8;
            else if (ft->isFloatTy())   off += 4;
            else if (ft->isDoubleTy())  off += 8;
            else if (auto* sty2 = llvm::dyn_cast<llvm::StructType>(ft))
                                        off += PTR_SIZE * sty2->getNumElements();
            else                        off += PTR_SIZE;
        }
        return off;
    };

    // Helper: does derived_func transitively override base_func?
    auto overrides_base_func = [&](const function& derived_func,
                                   const function& base_func) -> bool {
        const function* cur = &derived_func;
        while (cur) {
            if (cur == &base_func) return true;
            auto ov = cur->get_overrides();
            cur = ov ? ov.get() : nullptr;
        }
        return false;
    };

    // Helper: do two non-static member functions share the same virtual signature?
    // Delegates to the file-scope mat_same_virtual_sig helper.
    auto same_virtual_sig = [](const function& a, const function& b) -> bool {
        return mat_same_virtual_sig(a, b);
    };

    // Helper: build a secondary_vtable_spec for base_klass at byte_offset in kl
    auto build_spec = [&](std::shared_ptr<klass> base_klass, size_t byte_offset) {
        auto base_vt = base_klass->get_vtable();
        if (!base_vt || !base_vt->llvm_type) return;

        secondary_vtable_spec spec;
        spec.base_class  = base_klass;
        spec.base_offset = static_cast<ptrdiff_t>(byte_offset);

        for (auto& base_entry : base_vt->entries) {
            thunk_info ti;
            ti.slot_index = base_entry.slot_index;

            const vtable_entry* derived_entry = nullptr;
            for (auto& de : vt->entries) {
                if (de.func && base_entry.introducing_func
                    && (overrides_base_func(*de.func, *base_entry.introducing_func)
                        || (base_entry.func && overrides_base_func(*de.func, *base_entry.func)))) {
                    derived_entry = &de;
                    break;
                }
            }

            // Fallback: match by virtual signature when the overrides chain is
            // missing AND the base slot is abstract (a genuine interface method
            // with no concrete implementation). This covers the case where the
            // base slot is introduced by a secondary base of an intermediate base
            // (e.g. `interface C : A, B` with B as C's secondary base): a class
            // deriving from C creates a fresh vtable slot for its B-method override
            // without an overrides link back to B's slot. We must NOT apply this
            // fallback when the base slot already has a concrete implementation
            // (e.g. `class D : B, C` where both B and C define value()), because
            // there a same-signature method from a *different* base would be
            // wrongly selected instead of the base's own concrete method.
            if (!derived_entry && base_entry.func && base_entry.func->is_abstract_func()) {
                const function* intro = base_entry.introducing_func
                    ? base_entry.introducing_func.get()
                    : base_entry.func.get();
                if (intro) {
                    for (auto& de : vt->entries) {
                        if (de.func && !de.func->is_abstract_func()
                            && same_virtual_sig(*de.func, *intro)) {
                            derived_entry = &de;
                            break;
                        }
                    }
                }
            }

            if (!derived_entry || !derived_entry->func) {
                ti.real_func       = base_entry.func;
                ti.this_adjustment = 0;
                ti.needs_thunk     = false;
            } else {
                bool is_overridden = (derived_entry->func.get() != base_entry.func.get());
                ti.real_func       = derived_entry->func;
                ti.this_adjustment = is_overridden ? static_cast<ptrdiff_t>(byte_offset) : 0;
                ti.needs_thunk     = is_overridden && (byte_offset > 0);
            }
            spec.slot_thunks.push_back(ti);
        }

        vt->secondary_vtables.push_back(std::move(spec));
    };

    // Step 1: Walk all non-virtual sub-objects transitively, computing cumulative byte offsets
    // Walk ALL non-virtual sub-objects transitively reachable from kl,
    // computing their cumulative byte offsets in kl's layout.
    // For each sub-object with a vtable, generate a secondary_vtable_spec.
    // This includes both "primary" and "secondary" bases at all levels —
    // in K's layout every base sub-object (including the primary) is at a
    // non-zero offset because kl's own __vptr__ occupies field 0.
    // We use `already_processed` to avoid duplicating specs for the same type.
    std::unordered_set<const klass*> already_processed;

    // Step 2: For each sub-object with a vtable, build a secondary_vtable_spec with thunk_info records
    // DFS: for each aggregate, walk its non-virtual bases and build specs for
    // all sub-objects that have a vtable, at their correct cumulative offsets.
    std::function<void(const aggregate&, llvm::StructType*, size_t)> walk;
    walk = [&](const aggregate& cur, llvm::StructType* cur_llvm_type, size_t cum_offset) {
        for (auto& bs : cur.get_bases()) {
            if (!bs.base || bs.is_virtual) continue;
            auto base_klass = std::dynamic_pointer_cast<klass>(bs.base);
            if (!base_klass) continue;

            // Find the field in cur's LLVM struct for this base sub-object
            std::string field_name = "__base_" + bs.sanitised_name() + "__";
            auto field_opt = (cur.get_struct_type())
                ? cur.get_struct_type()->get_member(field_name) : std::nullopt;

            size_t this_offset = cum_offset;
            if (field_opt && cur_llvm_type) {
                this_offset += field_byte_offset(cur_llvm_type, (unsigned)field_opt->index);
            }

            // Build spec for this base if not already done and it has a vtable
            if (!already_processed.count(base_klass.get()) && base_klass->has_vtable()) {
                already_processed.insert(base_klass.get());
                build_spec(base_klass, this_offset);
            }

            // Recurse into this base to pick up its own sub-objects
            auto base_llvm_type = base_klass->get_struct_type()
                ? llvm::cast_or_null<llvm::StructType>(base_klass->get_struct_type()->get_llvm_type())
                : nullptr;
            walk(*base_klass, base_llvm_type, this_offset);
        }
    };

    walk(kl, kl_llvm_type, 0);

    // Step 3: Handle virtual bases separately via __vbase_X__ fields. Populates vtable_layout::secondary_vtables
    // ── Virtual bases: generate secondary vtable specs for __vbase_X__ ───────
    // (same logic as before, unchanged)
    {
        auto vbases = kl.get_all_virtual_base_structs();
        for (auto& vbase_agg : vbases) {
            auto vbase_klass = std::dynamic_pointer_cast<klass>(vbase_agg);
            if (!vbase_klass || !vbase_klass->has_vtable()) continue;

            auto vbase_vt = vbase_klass->get_vtable();
            if (!vbase_vt || !vbase_vt->llvm_type) continue;

            std::string vbase_field_name = "__vbase_" + vbase_klass->get_short_name() + "__";
            auto vbase_field = kl_struct_type->get_member(vbase_field_name);
            if (!vbase_field) continue;

            size_t byte_offset = field_byte_offset(kl_llvm_type, (unsigned)vbase_field->index);

            if (!already_processed.count(vbase_klass.get())) {
                already_processed.insert(vbase_klass.get());
                build_spec(vbase_klass, byte_offset);
            }
        }
    }
}


} // namespace k::model::gen
