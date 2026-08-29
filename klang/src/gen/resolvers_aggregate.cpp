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
#include "resolvers_aggregate.hpp"
#include "resolvers_scope_lookup.hpp"
#include "resolvers_constexpr.hpp"
#include "gen_helpers.hpp"
#include "../model/imported.hpp"
#include "../model/statements.hpp"
#include "../model/expressions.hpp"
#include "../model/template.hpp"
#include "../model/template_instantiator.hpp"
#include "../parse/ast.hpp"
#include <llvm/IR/DerivedTypes.h>
#include <queue>
#include <set>
#include <unordered_set>
#include <functional>
#include "../errors.hpp"
namespace k::model::gen {
// aggregate_type_resolver

// Forward declaration: resolves a single (possibly composite) type reference,
// triggering template instantiation for unresolved template types. Defined
// later in this file; declared here so try_instantiate_template_type can use it
// to transitively resolve nested-template member-variable types.
static std::shared_ptr<type>
resolve_one_type(const std::shared_ptr<type>& t,
                 aggregate_type_resolver& resolver,
                 const element& context_elem,
                 std::shared_ptr<context> ctx);

// ensure_klass_vtable_built
// -------------------------
// See declaration in resolvers_common.hpp. Builds a vtable on demand for
// template instantiations that bypass symbol_resolver's normal pass, handling
// arbitrary-depth base hierarchies (recursing into each base first) and
// linking overrides for methods that override a secondary (non-primary)
// base's slot, transitively.
void ensure_klass_vtable_built(klass& kl) {
    if (kl.has_vtable()) return;

    // 1. Recursively ensure all local-klass bases have their own vtable built
    //    first, so multi-level template interface hierarchies are fully resolved
    //    bottom-up before we process `kl` itself. Virtual bases are included here:
    //    a virtual base still owns a vtable and its methods still need slots for
    //    dispatch (only its placement in the layout / primary chain differs).
    //    Imported bases (e.g. ::k::Object loaded from a KDI) already have a
    //    fully-resolved vtable_layout by construction — nothing to do for those.
    for (auto& bs : kl.get_bases()) {
        if (!bs.base) continue;
        if (auto base_kl = std::dynamic_pointer_cast<klass>(bs.base)) {
            ensure_klass_vtable_built(*base_kl);
        }
    }

    auto vt = std::make_shared<vtable_layout>();
    size_t next_slot = 0;

    // 2. Determine the primary base (first declared base with a vtable) and
    //    inherit its full (now complete) entry list. The primary base can be
    //    either a local klass or an IMPORTED aggregate (e.g. ::k::Object, when
    //    the current module imports k rather than defining it) — mirrors
    //    gen_class.cpp's build_vtable_layout(), which handles both cases.
    //
    // Note: K's inheritance model makes EVERY base virtual (diamond-safe) by
    // design — klass::compute_virtual_bases_single() (called by the template
    // instantiator right before this function) sets base_spec::is_virtual =
    // true on every base edge, not just genuine diamond cases. A base_spec::
    // is_virtual check here would therefore always be true and skip every
    // base unconditionally, silently degrading every template instantiation's
    // vtable to "no inherited slots at all" — this used to be exactly that
    // bug (mirrors gen_class.cpp's build_vtable_layout(), which never checks
    // is_virtual for this same reason).
    std::shared_ptr<klass> primary_base;
    std::shared_ptr<imported_aggregate> primary_base_imp;
    for (auto& bs : kl.get_bases()) {
        if (!bs.base) continue;
        if (auto base_kl = std::dynamic_pointer_cast<klass>(bs.base)) {
            if (base_kl->has_vtable()) {
                primary_base = base_kl;
                break;
            }
        } else if (auto imp = std::dynamic_pointer_cast<imported_aggregate>(bs.base)) {
            if (imp->has_vtable() && !primary_base_imp) {
                primary_base_imp = imp;
                // Don't break — prefer a local klass if one comes first (matches
                // declaration order priority in gen_class.cpp).
            }
        }
    }
    if (primary_base) {
        for (auto& entry : primary_base->get_vtable()->entries) {
            vtable_entry inherited;
            inherited.slot_index = entry.slot_index;
            inherited.introducing_func = entry.introducing_func;
            inherited.func = entry.func;
            vt->entries.push_back(inherited);
            next_slot = std::max(next_slot, entry.slot_index + 1);
        }
    } else if (primary_base_imp) {
        if (auto base_vt = primary_base_imp->get_vtable()) {
            for (auto& entry : base_vt->entries) {
                vtable_entry inherited;
                inherited.slot_index = entry.slot_index;
                inherited.introducing_func = entry.introducing_func;
                inherited.func = entry.func;
                vt->entries.push_back(inherited);
                next_slot = std::max(next_slot, (size_t)entry.slot_index + 1);
            }
        }
    }

    // ── Destructor slot (universal slot 0) ──────────────────────────────────
    // Mirrors the destructor-slot handling in gen_class.cpp's build_vtable_layout():
    // ::k::Object declares the first-ever virtual destructor at vtable slot 0,
    // and every class/interface reaching Object via its primary base chain
    // inherits that same slot 0, overridden by its own (explicit or
    // compiler-generated) destructor. Template instantiations bypass
    // symbol_resolver's normal build_vtable_layout() pass, so this must be
    // replicated here too — otherwise the destructor slot is silently dropped
    // and the instantiation's first own virtual method collides with slot 0,
    // causing it to be dispatched through as if it were the destructor.
    {
        bool inherited_dtor_slot = !vt->entries.empty()
            && std::dynamic_pointer_cast<destructor>(vt->entries[0].introducing_func) != nullptr;

        auto own_dtor = kl.get_destructor();

        if (inherited_dtor_slot) {
            if (own_dtor) {
                auto& dtor_entry = vt->entries[0];
                own_dtor->set_virtual(true);
                own_dtor->set_vtable_slot(0);
                own_dtor->set_overrides(dtor_entry.func);
                dtor_entry.func = own_dtor;
            }
        } else if (own_dtor && !kl.has_bases()) {
            // Root aggregate with its own destructor and no bases at all: this is
            // ::k::Object itself — not expected to reach this template-instantiation
            // path, but handled defensively for symmetry with build_vtable_layout().
            own_dtor->set_virtual(true);
            own_dtor->set_vtable_slot(0);
            vtable_entry dtor_slot;
            dtor_slot.slot_index = next_slot++;
            dtor_slot.introducing_func = own_dtor;
            dtor_slot.func = own_dtor;
            vt->entries.insert(vt->entries.begin(), dtor_slot);
        }
    }

    // Signature comparator (name + const-qualification + parameter/return types,
    // parameter identifiers ignored), falling back to canonical spelling so
    // cross-module template instantiations compare equal — mirrors
    // have_same_virtual_signature() in gen_class.cpp).
    auto same_sig = [](const function& a, const function& b) -> bool {
        if (a.get_short_name() != b.get_short_name()) return false;
        if (a.is_const_member() != b.is_const_member()) return false;
        if (a.get_parameter_size() != b.get_parameter_size()) return false;
        auto type_match = [](const std::shared_ptr<type>& x, const std::shared_ptr<type>& y) -> bool {
            if (type::are_equal(x, y)) return true;
            return x && y && x->to_string() == y->to_string();
        };
        for (size_t i = 0; i < a.get_parameter_size(); ++i) {
            auto ta = std::const_pointer_cast<type>(a.get_parameter(i)->get_type());
            auto tb = std::const_pointer_cast<type>(b.get_parameter(i)->get_type());
            if (!type_match(ta, tb)) return false;
        }
        // Note: return type is deliberately NOT compared here. Self-referencing
        // return types (e.g. `OutputStream<T>&` returned from a method of
        // `OutputStream<T>` itself, or of a class deriving from it) may still be
        // unresolved (`<<unresolved:...>>`) at the point this function runs, since
        // ensure_klass_vtable_built() executes before type_reference_resolver has
        // finished resolving member return types. K does not support overloading
        // by return type alone, so name + parameter-type matching is sufficient
        // and avoids spurious mismatches here.
        return true;
    };

    // 3. Collect ALL bases transitively (BFS), for secondary-override detection.
    std::vector<std::shared_ptr<aggregate>> all_bases;
    {
        std::unordered_set<const aggregate*> seen;
        std::queue<std::shared_ptr<aggregate>> q;
        for (auto& bs : kl.get_bases()) {
            if (bs.base && !seen.count(bs.base.get())) {
                seen.insert(bs.base.get());
                q.push(bs.base);
                all_bases.push_back(bs.base);
            }
        }
        while (!q.empty()) {
            auto cur = q.front(); q.pop();
            for (auto& bs : cur->get_bases()) {
                if (bs.base && !seen.count(bs.base.get())) {
                    seen.insert(bs.base.get());
                    q.push(bs.base);
                    all_bases.push_back(bs.base);
                }
            }
        }
    }

    // 4. Process own functions: match against inherited primary-base slots,
    //    otherwise introduce a new slot and link a secondary-base override
    //    (transitively, at any depth) if applicable.
    for (auto& child : kl.get_children()) {
        auto func = std::dynamic_pointer_cast<function>(child);
        if (!func) continue;
        if (func->is_static()) continue;
        if (func->is_template()) {
            func->set_virtual(false);
            func->set_vtable_slot(-1);
            continue;
        }
        if (std::dynamic_pointer_cast<constructor>(func)) continue;
        if (std::dynamic_pointer_cast<destructor>(func)) continue;
        if (func->get_visibility() == PRIVATE) continue;

        bool found_override = false;
        for (auto& entry : vt->entries) {
            if (entry.introducing_func && same_sig(*func, *entry.introducing_func)) {
                func->set_virtual(true);
                func->set_vtable_slot((int)entry.slot_index);
                func->set_overrides(entry.func);
                entry.func = func;
                found_override = true;
                break;
            }
        }

        if (!found_override) {
            func->set_virtual(true);
            func->set_vtable_slot((int)next_slot);
            vtable_entry new_entry;
            new_entry.slot_index = next_slot++;
            new_entry.introducing_func = func;
            new_entry.func = func;
            vt->entries.push_back(new_entry);

            std::shared_ptr<function> secondary_override;
            for (auto& base : all_bases) {
                if (primary_base && base.get() == primary_base.get()) continue;
                auto pk = std::dynamic_pointer_cast<klass>(base);
                if (!pk || !pk->has_vtable()) continue;
                for (auto& sec_entry : pk->get_vtable()->entries) {
                    if (sec_entry.introducing_func && same_sig(*func, *sec_entry.introducing_func)) {
                        secondary_override = sec_entry.func ? sec_entry.func : sec_entry.introducing_func;
                        break;
                    }
                }
                if (secondary_override) break;
            }
            if (secondary_override && !func->get_overrides()) {
                func->set_overrides(secondary_override);
            }
        }
    }

    if (!vt->entries.empty()) {
        kl.set_vtable(vt);
        if (kl.get_vptrs().empty()) {
            kl.inject_vptr_field("__vptr__");
        }
    }
}

std::shared_ptr<aggregate>
aggregate_type_resolver::resolve_struct_from(const element& elem, const k::name& qualified_name) {
    if (qualified_name.empty()) return {};

    if (qualified_name.size() == 1) {
        if (auto st_holder = dynamic_cast<const aggregate_holder*>(&elem)) {
            if (auto agg = st_holder->get_aggregate(qualified_name.front())) return agg;
        }
        return {};
    }

    const auto& first = qualified_name.front();
    const auto  rest  = qualified_name.without_front();

    if (auto nspc = dynamic_cast<const ns*>(&elem)) {
        if (auto child = nspc->get_child_namespace(first)) {
            if (auto st = resolve_struct_from(*child, rest)) return st;
        }
    }
    if (auto st_holder = dynamic_cast<const aggregate_holder*>(&elem)) {
        if (auto agg = st_holder->get_aggregate(first)) {
            if (auto nested = resolve_struct_from(*agg, rest)) return nested;
        }
    }
    return {};
}

std::shared_ptr<type>
aggregate_type_resolver::resolve_type_from_root(const k::name& name_without_prefix) {
    if (name_without_prefix.empty()) return {};
    auto root_ns = _unit.get_root_namespace();
    if (!root_ns) return {};

    const auto& unit_name = _unit.get_unit_name();
    if (!unit_name.empty() && name_without_prefix.front() == unit_name.back()) {
        auto rest = name_without_prefix.without_front();
        if (!rest.empty()) {
            if (auto st = resolve_struct_from(*root_ns, rest)) return st->get_struct_type();
        }
    }
    if (auto st = resolve_struct_from(*root_ns, name_without_prefix)) return st->get_struct_type();

    // Enums and unions: navigate the namespace tree from the root to find a
    // (possibly qualified) enum/union type. resolve_struct_from only handles
    // aggregates, so multi-part enum/union names (e.g. ::k::io::StreamOutOfData)
    // must be resolved here explicitly.
    {
        // Try both the raw name and, when the leading segment is the current
        // module name (redundant self-prefix, e.g. ::k::io::SOD inside module k),
        // the name with that segment stripped.
        std::vector<k::name> candidates_names;
        candidates_names.push_back(name_without_prefix);
        if (!unit_name.empty() && name_without_prefix.front() == unit_name.back()) {
            auto rest = name_without_prefix.without_front();
            if (!rest.empty()) candidates_names.push_back(rest);
        }
        for (const auto& cand : candidates_names) {
            if (cand.size() == 1) {
                if (auto en = root_ns->get_enum(cand.front())) return en->get_enum_type();
                if (auto un = root_ns->get_union(cand.front())) return un->get_struct_type();
                continue;
            }
            auto target_ns = root_ns;
            bool found_path = true;
            for (size_t i = 0; i + 1 < cand.size(); ++i) {
                auto child = target_ns->get_child_namespace(cand[i]);
                if (child) {
                    target_ns = child;
                } else {
                    found_path = false;
                    break;
                }
            }
            if (found_path) {
                if (auto en = target_ns->get_enum(cand.back())) return en->get_enum_type();
                if (auto un = target_ns->get_union(cand.back())) return un->get_struct_type();
            }
        }
    }

    // Fallback: search imported modules.
    if (auto agg = _unit.get_or_create_imported_aggregate(name_without_prefix, _context)) {
        return agg->get_struct_type();
    }
    // Allow redundant imported-module prefixes (e.g. ::k::Expected).
    for (const auto& imp : _unit.get_imports()) {
        if (imp.module_name.empty()) continue;
        if (name_without_prefix.size() <= imp.module_name.size()) continue;
        if (!name_without_prefix.start_with(imp.module_name)) continue;
        auto rest = name_without_prefix.without_front(imp.module_name.size());
        if (auto st = resolve_struct_from(*root_ns, rest)) return st->get_struct_type();
        if (auto agg = _unit.get_or_create_imported_aggregate(rest, _context)) {
            return agg->get_struct_type();
        }
    }
    // Fallback: search imported enums (e.g. ::k::io::StreamOutOfData used as a
    // template argument). Mirrors type_reference_resolver::resolve_type_from_root
    // Strategy 4 — without this, a root-prefixed imported enum used as a template
    // argument (Expected<unsigned int, ::k::io::StreamOutOfData>) fails to resolve
    // here, so try_instantiate_template_type returns {} and the qualified static
    // factory call (Expected<...>::expected(x)) falls back to the un-instantiated
    // template method whose R& parameter cannot match the argument.
    if (auto en = _unit.get_or_create_imported_enum(name_without_prefix, _context)) {
        return en->get_enum_type();
    }
    for (const auto& imp : _unit.get_imports()) {
        if (imp.module_name.empty()) continue;
        if (name_without_prefix.size() <= imp.module_name.size()) continue;
        if (!name_without_prefix.start_with(imp.module_name)) continue;
        auto rest = name_without_prefix.without_front(imp.module_name.size());
        if (auto en = _unit.get_or_create_imported_enum(rest, _context)) {
            return en->get_enum_type();
        }
    }
    return {};
}

// ── Template instantiation from type reference (aggregate_type_resolver) ─────

std::shared_ptr<type> aggregate_type_resolver::try_instantiate_template_type(
    const std::shared_ptr<unresolved_type>& unres,
    const element& context_elem)
{
    const auto& base_name = unres->type_id();
    const auto& ast_args = unres->get_ast_template_args();

    // 0. A parameterised alias renames a family of types and is resolved by
    // substituting the arguments into the renamed type, never by instantiating
    // an entity of its own.
    if (auto al = scope_lookup::lookup_alias(context_elem.shared_as<const element>(), base_name)) {
        if (auto aliased = scope_lookup::resolve_alias_template(
                al, unres, context_elem, _context,
                [this](const std::shared_ptr<type>& t, const element& e) {
                    return resolve_one_type(t, *this, e, _context);
                },
                [this, &al](unsigned int code, const std::string& msg, const std::vector<std::string>& args) {
                    throw_error(code, al->get_decl_lexeme(), msg, args);
                })) {
            return aliased;
        }
    }

    // 1. Look up the template aggregate by base name (walking scope chain)
    std::shared_ptr<aggregate> tpl_agg;
    for (auto current = context_elem.shared_as<const element>(); current; current = current->parent<element>()) {
        if (auto st = resolve_struct_from(*current, base_name)) {
            if (st->is_template()) { tpl_agg = st; break; }
            return {}; // Found non-template — not a template instantiation
        }
    }
    if (!tpl_agg) {
        auto root_ns = _unit.get_root_namespace();
        if (root_ns) {
            if (auto st = resolve_struct_from(*root_ns, base_name)) {
                if (st->is_template()) tpl_agg = st;
            }
        }
    }
    // Fallback for unqualified names imported from modules (e.g. implicit import k).
    if (!tpl_agg && base_name.size() == 1) {
        if (auto root_ns = _unit.get_root_namespace()) {
            for (const auto& imp : _unit.get_imports()) {
                if (imp.module_name.empty()) continue;
                auto imported_name = imp.module_name.with_back(base_name.front());
                if (auto st = resolve_struct_from(*root_ns, imported_name)) {
                    if (st->is_template()) {
                        tpl_agg = st;
                        break;
                    }
                }
            }
        }
    }
    if (!tpl_agg && base_name.size() > 1) {
        for (const auto& imp : _unit.get_imports()) {
            if (imp.module_name.empty()) continue;
            if (!base_name.start_with(imp.module_name)) continue;
            auto rest = base_name.without_front(imp.module_name.size());
            if (rest.empty()) continue;
            if (auto root_ns = _unit.get_root_namespace()) {
                if (auto st = resolve_struct_from(*root_ns, rest)) {
                    if (st->is_template()) {
                        tpl_agg = st;
                        break;
                    }
                }
            }
            if (auto imp_st = _unit.get_or_create_imported_aggregate(rest, _context)) {
                auto st = std::dynamic_pointer_cast<aggregate>(imp_st);
                if (st && st->is_template()) {
                    tpl_agg = st;
                    break;
                }
            }
            // Imported template definitions with bodies are re-homed (flattened)
            // directly into the consumer's root namespace under their SHORT name by
            // the kdi_importer `module <ns>;` re-parse trick (intermediate namespaces
            // such as `io` are dropped). So when the remainder still carries namespace
            // components (e.g. [io, FilterInputStream]), also try the bare short name.
            if (rest.size() > 1) {
                if (auto root_ns = _unit.get_root_namespace()) {
                    if (auto st = resolve_struct_from(*root_ns, k::name(false, rest.back()))) {
                        if (st->is_template()) {
                            tpl_agg = st;
                            break;
                        }
                    }
                }
            }
        }
    }
    // 1b. If no template aggregate found, look for template unions
    std::shared_ptr<union_type_def> tpl_union;
    if (!tpl_agg) {
        for (auto current = context_elem.shared_as<const element>(); current; current = current->parent<element>()) {
            if (auto uh = std::dynamic_pointer_cast<const union_holder>(current)) {
                if (base_name.size() == 1) {
                    if (auto un = uh->get_union(base_name.front())) {
                        if (un->is_template()) { tpl_union = un; break; }
                        return {};
                    }
                }
            }
        }
        if (!tpl_union) {
            auto root_ns = _unit.get_root_namespace();
            if (root_ns && base_name.size() == 1) {
                if (auto un = root_ns->get_union(base_name.front())) {
                    if (un->is_template()) tpl_union = un;
                }
            }
        }
        if (!tpl_union && base_name.size() == 1) {
            if (auto root_ns = _unit.get_root_namespace()) {
                for (const auto& imp : _unit.get_imports()) {
                    if (imp.module_name.empty()) continue;
                    auto imported_ns = root_ns;
                    for (std::size_t i = 0; imported_ns && i < imp.module_name.size(); ++i) {
                        imported_ns = imported_ns->get_child_namespace(imp.module_name[i]);
                    }
                    if (!imported_ns) continue;
                    if (auto un = imported_ns->get_union(base_name.front())) {
                        if (un->is_template()) { tpl_union = un; break; }
                    }
                }
            }
        }
    }
    if (!tpl_agg && !tpl_union) return {};

    tpl_info* ti = tpl_agg ? tpl_agg->get_tpl_info() : tpl_union->get_tpl_info();
    if (!ti) return {};

    // 2. Validate argument count (allow fewer args if trailing params have defaults)
    if (ast_args.size() > ti->params.size()) return {};
    if (ast_args.size() < ti->params.size()) {
        // Check that all missing params have defaults
        for (size_t i = ast_args.size(); i < ti->params.size(); ++i) {
            auto& param = ti->params[i];
            if (param.is_type_param() && !param.default_type) return {};
            if (param.is_value_param() && !param.default_value.has_value()) return {};
        }
    }

    // 3. Convert AST template args to model template_arguments
    std::vector<template_argument> model_args;
    model_args.reserve(ti->params.size());
    // Pre-resolved model-level template arguments, set by substitute_type() during
    // template instantiation when the AST arg name (e.g. "T") is no longer in scope.
    const auto& model_targs = unres->get_model_template_args();
    for (size_t i = 0; i < ast_args.size(); ++i) {
        const auto& ast_arg = ast_args[i];
        // Resolve the value parameter's declared type once, up-front, so that
        // enum-typed value params (still an unresolved_type placeholder at
        // model_builder time) are properly validated by the evaluator below.
        std::shared_ptr<type> expected_value_type;
        if (i < ti->params.size()) {
            expected_value_type = ti->params[i].value_type;
            if (expected_value_type && !type::is_resolved(expected_value_type)) {
                auto resolved_vt = _context->resolve_type(expected_value_type);
                if (resolved_vt && type::is_resolved(resolved_vt)) expected_value_type = resolved_vt;
            }
        }
        if (i < ti->params.size() && ti->params[i].is_value_param() && ast_arg->is_type()) {
            // The parser always parses a bare qualified name (e.g. "Color::Red")
            // as a type-specifier template argument, since it cannot tell types
            // and value expressions like enum constants / dependent value params
            // apart syntactically. Since we know this parameter position is
            // actually a VALUE parameter, reinterpret it as a constant expression.
            auto eval = evaluate_template_value_arg_from_type_spec(
                ast_arg->type_arg.get(), context_elem, _context, expected_value_type, &_unit);
            if (eval.is_error()) {
                throw_error(eval.error_code, lex::opt_any_lexeme{}, eval.message, eval.message_args);
            }
            if (eval.ok()) {
                model_args.push_back(template_argument::make_value(*eval.value, expected_value_type));
                continue;
            }
            return {};
        }
        if (ast_arg->is_type()) {
            // Prefer a pre-resolved model-level template argument when present.
            if (i < model_targs.size() && model_targs[i]
                && (type::is_resolved(model_targs[i])
                    || std::dynamic_pointer_cast<struct_type>(model_targs[i]))) {
                model_args.push_back(template_argument::make_type(model_targs[i]));
                continue;
            }
            // A nested template reference produced by substitution (e.g. 'Box<T>'
            // in 'Pair<Box<T>, int>') is itself an unresolved_type carrying
            // substituted model arguments: resolve it recursively.
            if (i < model_targs.size() && model_targs[i]) {
                if (auto nested = std::dynamic_pointer_cast<unresolved_type>(model_targs[i])) {
                    if (nested->has_model_template_args()) {
                        if (auto nested_res = try_instantiate_template_type(nested, context_elem)) {
                            if (type::is_resolved(nested_res)) {
                                model_args.push_back(template_argument::make_type(nested_res));
                                continue;
                            }
                        }
                    }
                }
            }
            auto arg_type = _context->from_type_specifier(*ast_arg->type_arg);
            if (!arg_type || !type::is_resolved(arg_type)) {
                if (auto unres_arg = std::dynamic_pointer_cast<unresolved_type>(arg_type)) {
                    // A bare template-parameter placeholder (e.g. 'I'/'O'/'T' used
                    // inside the defining template's own body, such as
                    // `buffer : Vector<I>;` in `ManyToOneTransformInputStream<I,O>`)
                    // must NOT be resolved via a global/namespace name lookup — an
                    // unrelated user type that happens to share the same short name
                    // (e.g. `interface I { ... }`) would be spuriously matched.
                    // Only the substitution-map recovery paths below (enclosing
                    // concrete function/aggregate) may legitimately resolve it.
                    if (!unres_arg->is_template_param_placeholder()
                        && !is_enclosing_template_param_name(context_elem, unres_arg->type_id().to_string())) {
                        auto resolved = resolve_type_by_name(unres_arg->type_id(), context_elem);
                        if (resolved && type::is_resolved(resolved)) {
                            arg_type = resolved;
                        }
                    }
                    // If still unresolved, the arg may be a template parameter name
                    // (e.g. "T") that is no longer in scope because we are resolving a
                    // NESTED template type (UniSlot<T>) inside an already-instantiated
                    // enclosing template (Optional<int>).  Recover the concrete type
                    // from the substitution map carried by the enclosing concrete
                    // function or aggregate.  Mirrors type_reference_resolver step 3.
                    if (!type::is_resolved(arg_type)) {
                        // (a) Enclosing concrete function's substitution map.
                        const function* owning_fn = dynamic_cast<const function*>(&context_elem);
                        if (!owning_fn) {
                            if (auto parent_fn = context_elem.ancestor<function>()) {
                                owning_fn = parent_fn.get();
                            }
                        }
                        if (owning_fn && owning_fn->has_tpl_instantiation_subst()) {
                            const auto& fn_subst = owning_fn->get_tpl_instantiation_subst();
                            auto sit = fn_subst.find(unres_arg->type_id().to_string());
                            if (sit == fn_subst.end() && !unres_arg->type_id().empty()) {
                                sit = fn_subst.find(unres_arg->type_id().back());
                            }
                            if (sit != fn_subst.end() && sit->second && type::is_resolved(sit->second)) {
                                arg_type = sit->second;
                            }
                        }
                    }
                    if (!type::is_resolved(arg_type)) {
                        // (b) Enclosing concrete aggregate's template param→arg mapping.
                        const aggregate* owning_agg = dynamic_cast<const aggregate*>(&context_elem);
                        if (!owning_agg) {
                            if (auto parent_agg = context_elem.ancestor<aggregate>()) {
                                owning_agg = parent_agg.get();
                            }
                        }
                        if (owning_agg && owning_agg->has_tpl_args()) {
                            auto tpl_base_name = owning_agg->get_tpl_base_name();
                            std::shared_ptr<aggregate> tpl_def;
                            if (auto parent_ns = owning_agg->parent<ns>()) {
                                tpl_def = parent_ns->get_aggregate(tpl_base_name);
                            }
                            if (tpl_def && tpl_def->get_tpl_info()) {
                                const auto& params = tpl_def->get_tpl_info()->params;
                                const auto& args = owning_agg->get_tpl_args();
                                for (size_t pi = 0; pi < params.size() && pi < args.size(); ++pi) {
                                    if (params[pi].is_type_param() && args[pi].is_type()) {
                                        std::string pname = params[pi].name;
                                        if (pname == unres_arg->type_id().to_string() ||
                                            (!unres_arg->type_id().empty() && pname == unres_arg->type_id().back())) {
                                            if (args[pi].type_arg && type::is_resolved(args[pi].type_arg)) {
                                                arg_type = args[pi].type_arg;
                                                break;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                } else if (arg_type) {
                    // Wrapper type (pointer, owner, reference, etc.) around an unresolved inner type.
                    // Peel the wrapper, resolve the inner type, then rebuild.
                    auto resolved = _context->resolve_type(arg_type);
                    if (resolved && type::is_resolved(resolved)) {
                        arg_type = resolved;
                    } else {
                        // Try resolve_type_by_name on the innermost unresolved type
                        auto inner = arg_type;
                        while (inner && inner->get_subtype() && !std::dynamic_pointer_cast<unresolved_type>(inner))
                            inner = inner->get_subtype();
                        if (auto unres_inner = std::dynamic_pointer_cast<unresolved_type>(inner)) {
                            auto resolved_inner = resolve_type_by_name(unres_inner->type_id(), context_elem);
                            if (resolved_inner && type::is_resolved(resolved_inner)) {
                                // Rebuild wrapper chain around resolved inner
                                // Use context->resolve_type which should now succeed
                                // since the inner type is resolved in the cache
                                auto retry = _context->resolve_type(arg_type);
                                if (retry && type::is_resolved(retry)) {
                                    arg_type = retry;
                                }
                            }
                        }
                    }
                }
            }
            if (!arg_type || !type::is_resolved(arg_type)) return {};
            model_args.push_back(template_argument::make_type(arg_type));
        } else {
            // Value template argument — evaluate as a compile-time constant
            // expression (literal, enum constant, dependent value parameter,
            // or an arithmetic/logical/cast combination thereof).
            auto eval = evaluate_template_value_arg(ast_arg->value_arg.get(), context_elem, _context, expected_value_type, &_unit);
            if (eval.is_error()) {
                throw_error(eval.error_code, lex::opt_any_lexeme{}, eval.message, eval.message_args);
            }
            if (!eval.ok()) return {};
            model_args.push_back(template_argument::make_value(*eval.value, expected_value_type));
        }
    }
    // 3b. Fill in default arguments for missing trailing parameters
    for (size_t i = ast_args.size(); i < ti->params.size(); ++i) {
        auto& param = ti->params[i];
        if (param.is_type_param() && param.default_type) {
            // Resolve the default type if needed
            auto def_type = param.default_type;
            if (!type::is_resolved(def_type)) {
                def_type = _context->resolve_type(def_type);
                if (!def_type || !type::is_resolved(def_type)) {
                    if (auto unres = std::dynamic_pointer_cast<unresolved_type>(param.default_type)) {
                        auto resolved = resolve_type_by_name(unres->type_id(), context_elem);
                        if (resolved && type::is_resolved(resolved)) def_type = resolved;
                    }
                }
            }
            if (!def_type || !type::is_resolved(def_type)) return {};
            model_args.push_back(template_argument::make_type(def_type));
        } else if (param.is_value_param() && param.default_value.has_value()) {
            model_args.push_back(template_argument::make_value(*param.default_value, param.value_type));
        } else {
            return {};
        }
    }

    // 3c. Resolve constraint types in template params if still unresolved
    {
        const element& constraint_ctx = tpl_agg ? static_cast<const element&>(*tpl_agg) : static_cast<const element&>(*tpl_union);
        for (auto& param : ti->params) {
            if (param.is_type_param() && param.constraint_type && !type::is_resolved(param.constraint_type)) {
                auto resolved = _context->resolve_type(param.constraint_type);
                if (resolved && type::is_resolved(resolved)) {
                    param.constraint_type = resolved;
                } else if (auto unres = std::dynamic_pointer_cast<unresolved_type>(param.constraint_type)) {
                    auto r = resolve_type_by_name(unres->type_id(), constraint_ctx);
                    if (r && type::is_resolved(r)) param.constraint_type = r;
                }
            }
        }
    }

    // 3d. Validate type constraints (kind filter + base-type constraint)
    {
        std::string tpl_short_name = tpl_agg ? tpl_agg->get_short_name() : tpl_union->get_short_name();
        size_t err_idx;
        std::string err_reason;
        if (!validate_template_arg_constraints(ti->params, model_args, err_idx, err_reason)) {
            auto [code, msg] = format_constraint_error(
                tpl_short_name, ti->params, model_args, err_idx, err_reason);
            throw_error(code, lex::opt_any_lexeme{}, msg);
        }
    }

    // 4. Instantiate the template (aggregate or union)
    if (tpl_union) {
        // Template union instantiation
        auto parent_ns = scope_lookup::enclosing_namespace(*tpl_union);
        if (!parent_ns) return {};

        auto concrete_union = template_instantiator::instantiate_union(
            *tpl_union, model_args, parent_ns, _unit, _context, *this);
        if (!concrete_union) return {};

        // If already has a struct_type (from cache), return it
        if (concrete_union->get_struct_type()) return concrete_union->get_struct_type();

        // Resolve alternative types
        for (auto& alt : concrete_union->alternatives_mutable()) {
            if (alt.resolved_type && !type::is_resolved(alt.resolved_type)) {
                auto resolved = _context->resolve_type(alt.resolved_type);
                if (resolved && type::is_resolved(resolved)) {
                    alt.resolved_type = resolved;
                } else if (auto unres_alt = std::dynamic_pointer_cast<unresolved_type>(alt.resolved_type)) {
                    auto by_name = resolve_type_by_name(unres_alt->type_id(), context_elem);
                    if (by_name && type::is_resolved(by_name)) {
                        alt.resolved_type = by_name;
                    }
                }
            }
        }

        // Create LLVM opaque struct type (body finalized in declaration_generator::visit_union)
        auto& llvm_ctx = _context->llvm_context();
        auto* union_llvm_type = llvm::StructType::create(llvm_ctx, concrete_union->get_mangled_name() + "_union");
        // Use the FQ name (stripped of leading "::") so that KDI type resolution
        // (convert_aggregate_ref) can unambiguously locate the union in the namespace tree.
        std::string st_name = concrete_union->get_short_name();
        {
            const std::string& fq = concrete_union->get_fq_name();
            if (fq.size() >= 2 && fq[0] == ':' && fq[1] == ':') {
                st_name = fq.substr(2);
            } else if (!fq.empty()) {
                st_name = fq;
            }
        }
        auto st_type = std::make_shared<struct_type>(st_name, std::weak_ptr<aggregate>{});
        _context->attach_llvm_struct_type(st_type, union_llvm_type);
        concrete_union->set_struct_type(st_type);
        _context->add_struct(st_type);

        return st_type;
    }

    // Template aggregate instantiation
    auto parent_ns = scope_lookup::enclosing_namespace(*tpl_agg);
    if (!parent_ns) return {};

    std::shared_ptr<aggregate> concrete;
    if (tpl_agg->is_generic()) {
        concrete = template_instantiator::synthesize_generic_aggregate(
            *tpl_agg, parent_ns, _unit, _context, *this);
        if (concrete) {
            // Track concrete-argument usages while keeping a single synthesized body.
            const auto key = build_instantiation_key(model_args);
            ti->instantiations[key] = concrete;
            record_generic_usage(*ti, model_args);
        }
    } else {
        concrete = template_instantiator::instantiate_aggregate(
            *tpl_agg, model_args, parent_ns, _unit, _context, *this);
    }
    if (!concrete) return {};

    // 4b. Resolve unresolved member-variable references in method bodies.
    //     The symbol_resolver skipped the template aggregate, so bare names
    //     like 'x' (meaning 'this.x') are still unresolved in the cloned body.
    template_instantiator::resolve_body_symbols(concrete);

    // 4bb. Inject base sub-object fields (__base_X__) for resolved bases, and
    //      recursively for every base's own bases (see
    //      template_instantiator::inject_base_subobject_fields for why the
    //      recursion matters). Must run BEFORE inject_constructor_member_inits
    //      below, which looks up these fields to inject base-constructor-call
    //      statements — without them present yet, that injection silently
    //      no-ops, leaving base constructors uncalled (e.g. base vtable
    //      pointers never initialized).
    template_instantiator::inject_base_subobject_fields(concrete);

    // Inject collector __vbase_X__ storage (and any missing vbptrs) for virtual
    // interface bases now that all base struct types are available.
    template_instantiator::ensure_virtual_base_layout_fields(concrete);

    // 4c. Inject member-initializer expressions into concrete constructor blocks.
    //     symbol_resolver::visit_constructor normally does this, but template
    //     definitions are skipped and the concrete ctors are created after that pass.
    template_instantiator::inject_constructor_member_inits(concrete);

    // 5. Return existing struct_type or create a new one
    if (concrete->get_struct_type()) {
        return concrete->get_struct_type();
    }

    // Unify with any KDI-imported instantiation of the same template via the registry
    // on unit. The key is qualified by the template's originating namespace (its
    // origin tag for imported templates, or its enclosing namespace for locally
    // declared ones) so that same-named templates from different namespaces never
    // collide (see unit::make_instantiation_registry_key / _instantiation_struct_types).
    std::string origin_ns_fq = (ti && !ti->origin_module_ns_fq.empty())
                               ? ti->origin_module_ns_fq
                               : (parent_ns ? parent_ns->get_fq_name() : std::string{});
    const std::string inst_key = unit::make_instantiation_registry_key(
        origin_ns_fq, concrete->get_short_name());
    std::shared_ptr<struct_type> st_type;
    if (auto existing = _unit.find_instantiation_struct_type(inst_key)) {
        // Reuse the struct_type already created for this instantiation (possibly by
        // the KDI importer). Rebind it to this locally-synthesised concrete aggregate,
        // which carries real method/constructor bodies for code generation.
        st_type = existing;
        st_type->reassign_aggregate(concrete->shared_as<aggregate>());
    } else {
        st_type = std::shared_ptr<struct_type>{
            new struct_type(concrete->get_short_name(), concrete->shared_as<aggregate>())};
        _context->add_struct(st_type);
        _unit.register_instantiation_struct_type(inst_key, st_type);
    }
    concrete->set_struct_type(st_type);

    // 5b. Create 'this' parameters for member functions (requires struct_type)
    for (auto& child : concrete->get_children()) {
        if (auto fn = std::dynamic_pointer_cast<function>(child)) {
            if (fn->is_member() && !fn->is_static()) {
                fn->create_this_parameter();
            }
        }
    }
    // 5c. Assign FQ (fully-qualified) name to the concrete aggregate.
    //     symbol_resolver::visit_named_element normally does this, but the
    //     concrete aggregate was created after that pass already ran.
    //     Without a root-prefixed FQ name, update_mangled_name() produces
    //     an empty mangled name which breaks code generation and the JIT.
    if (concrete->get_fq_name().empty() && !concrete->get_short_name().empty()) {
        if (auto ancestor = concrete->template ancestor<named_element>()) {
            concrete->assign_name(ancestor->get_name().with_back(concrete->get_short_name()));
        }
    }
    // For instantiations of IMPORTED templates, override with an ORIGIN-ABSOLUTE
    // name (::<origin>::Name) — independent of the synthesising module — so the
    // mangled symbol is identical across every module that instantiates it and
    // linkonce_odr/COMDAT can merge the copies (C++ ODR model). Local-template
    // instantiations keep their model-tree-derived name (already origin-correct).
    if (ti && !ti->origin_module_ns_fq.empty()) {
        concrete->assign_name(unit::make_origin_absolute_name(
            ti->origin_module_ns_fq, concrete->get_short_name()));
    }
    concrete->update_mangled_name();

    // 5d. Update FQ names and mangled names for children (functions, constructors, etc.)
    for (auto& child : concrete->get_children()) {
        if (auto fn = std::dynamic_pointer_cast<function>(child)) {
            // Build FQ name from parent chain (mirrors symbol_resolver::visit_named_element).
            // Re-derive unconditionally so an origin-absolute rename of the parent
            // propagates to the methods' mangled symbols.
            if (auto parent_named = fn->template parent<named_element>()) {
                fn->assign_name(parent_named->get_name().with_back(fn->get_short_name()));
            }
            fn->update_mangled_name();
        } else if (auto gv = std::dynamic_pointer_cast<global_variable_definition>(child)) {
            // Static member variables (cloned as global_variable_definition) also need
            // their FQ/mangled name re-derived here now that the parent aggregate has
            // its own root-prefixed name: without it, declaration_generator::
            // visit_global_variable_definition emits the LLVM global with an empty
            // name (get_mangled_name() == ""), which LLVM auto-numbers as an anonymous
            // global (e.g. "@0"), breaking JIT module loading.
            if (auto parent_named = gv->template parent<named_element>()) {
                gv->assign_name(parent_named->get_name().with_back(gv->get_short_name()));
            }
        } else if (auto nested_un = std::dynamic_pointer_cast<union_type_def>(child)) {
            // Nested unions (e.g. Expected<R,E>::Storage) need the same treatment: they
            // were created by template_instantiator before the enclosing instantiation had
            // a root-prefixed name, so their mangled name would otherwise stay empty and
            // every instantiation's union would be emitted as the same anonymous LLVM type.
            if (auto parent_named = nested_un->template parent<named_element>()) {
                nested_un->assign_name(parent_named->get_name().with_back(nested_un->get_short_name()));
            }
            nested_un->update_mangled_name();
            if (auto kind_enum = nested_un->get_kind_enum()) {
                // assign_name() recomputes the mangled name.
                kind_enum->assign_name(nested_un->get_name().with_back(kind_enum->get_short_name()));
            }
        } else if (auto nested_agg = std::dynamic_pointer_cast<aggregate>(child)) {
            if (auto parent_named = nested_agg->template parent<named_element>()) {
                nested_agg->assign_name(parent_named->get_name().with_back(nested_agg->get_short_name()));
            }
            nested_agg->update_mangled_name();
            for (auto& nested_child : nested_agg->get_children()) {
                if (auto nested_fn = std::dynamic_pointer_cast<function>(nested_child)) {
                    if (auto parent_named = nested_fn->template parent<named_element>()) {
                        nested_fn->assign_name(parent_named->get_name().with_back(nested_fn->get_short_name()));
                    }
                    nested_fn->update_mangled_name();
                }
            }
        }
    }

    // 5e. (base sub-object field injection moved earlier — see 4bb above,
    //     which must run before inject_constructor_member_inits.)

    // 5f. Build vtable for class/interface instantiations (symbol_resolver didn't
    //     visit them because they didn't exist yet during Pass A). See
    //     ensure_klass_vtable_built() in resolvers_common.hpp for the recursive,
    //     multi-level-hierarchy-aware implementation (fixes a bug where a
    //     template interface hierarchy like MutableIndexedCollection<T> :
    //     IndexedCollection<T>, MutableCollection<T> only got a "flattened"
    //     own-methods-only vtable instead of the full inherited slot list).
    if (auto kl = std::dynamic_pointer_cast<model::klass>(concrete)) {
        ensure_klass_vtable_built(*kl);
    }

    // 5g. Transitively resolve member-variable types that still carry an
    //     unresolved nested template type (e.g. _slot : UniSlot<T> -> UniSlot<int>).
    //     The instantiator substitutes the template parameter, but a nested
    //     template member remains an unresolved_type carrying template args and
    //     must itself be instantiated. The type_reference_resolver (Pass 1.b)
    //     does this inline; the aggregate_type_resolver must do it too, because
    //     a template instantiated via a by-value function RETURN type is added
    //     to its own (possibly already-processed) parent namespace and the
    //     namespace walk never revisits it to resolve its members. Mirrors
    //     type_reference_resolver::try_instantiate_template_type step 7.
    auto resolve_member_vars = [&](aggregate& agg) {
        for (auto& child : agg.get_children()) {
            auto mv = std::dynamic_pointer_cast<member_variable_definition>(child);
            if (!mv) continue;
            auto var_type = mv->get_type();
            if (!var_type || type::is_resolved(var_type)) continue;
            auto resolved = resolve_one_type(var_type, *this, *mv, _context);
            if (resolved && type::is_resolved(resolved)) {
                mv->set_type(resolved);
            }
        }
    };
    resolve_member_vars(*concrete);
    for (auto& child : concrete->get_children()) {
        if (auto nested = std::dynamic_pointer_cast<aggregate>(child)) {
            resolve_member_vars(*nested);
        }
    }

    // 6. Resolve the LLVM struct type immediately (member types are already
    //    concrete thanks to the instantiator's type substitution).
    std::unordered_set<struct_type*> in_progress;
    _context->resolve_struct_type(st_type, in_progress);

    return st_type;
}

std::shared_ptr<type>
/**
 * Resolve a type by qualified name from a context element, walking up the scope chain
 * (aggregate_type_resolver version, used during Phase 1.a).
 *
 * Steps:
 *   1. Root-prefixed: delegate to resolve_type_from_root.
 *   2. Try primitive types via context->from_string.
 *   3. Walk up the scope chain looking for aggregates and enumerations.
 *   4. At each scope level, check using directives (anonymous, aliased, specific).
 *   5. Fallback: imported aggregates and enums.
 */
aggregate_type_resolver::resolve_type_by_name(const k::name& type_name, const element& context_elem) {
    // Step 1: Root-prefixed: delegate to resolve_type_from_root
    if (type_name.empty()) return {};

    if (type_name.has_root_prefix()) {
        return resolve_type_from_root(type_name.without_root_prefix());
    }

    // Step 2: Try primitive types via context->from_string
    if (type_name.size() == 1) {
        auto prim = _context->from_string(type_name.front());
        if (prim && type::is_resolved(prim)) return prim;
    }


    // Step 2b: aliases and typedefs. An alias is looked up before aggregates and
    // enums so that it can also shadow an outer type name, exactly like a using
    // declaration would.
    if (auto al = scope_lookup::lookup_alias(context_elem.shared_as<const element>(), type_name)) {
        bool cycle = false;
        auto aliased = scope_lookup::materialize_alias_type(
            al, _context,
            [this](const k::name& n, const element& e) { return resolve_type_by_name(n, e); },
            cycle,
            [this](const std::shared_ptr<type>& t, const element& e) -> std::shared_ptr<type> {
                // Phase 1.a has no full type resolver: only a callable prototype
                // needs the extra hand here, everything else is name-resolvable.
                if (auto uc = std::dynamic_pointer_cast<unresolved_callable_type>(t))
                    return resolve_unresolved_callable_type(uc, e);
                return nullptr;
            });
        if (cycle) {
            throw_error(static_cast<unsigned int>(k::diag::alias_diag::ERR_ALIAS_CYCLE), al->get_decl_lexeme(),
                "Alias '{}' is defined in terms of itself", {al->get_fq_name()});
        }
        if (aliased) return aliased;
    }

    // Step 3: Walk up the scope chain looking for aggregates, unions, and enumerations
    for (auto current = context_elem.shared_as<const element>(); current; current = current->parent<element>()) {
        if (auto st = resolve_struct_from(*current, type_name)) return st->get_struct_type();
        // Look for union types
        if (type_name.size() == 1) {
            if (auto uh_ptr = std::dynamic_pointer_cast<const union_holder>(current)) {
                if (auto un = uh_ptr->get_union(type_name.front())) {
                    return un->get_struct_type();
                }
            }
        } else {
            // Multi-part union name: navigate namespaces to find the union
            if (auto nspc = std::dynamic_pointer_cast<const ns>(current)) {
                auto target_ns = nspc;
                bool found_path = true;
                for (size_t i = 0; i + 1 < type_name.size(); ++i) {
                    auto child = target_ns->get_child_namespace(type_name[i]);
                    if (child) {
                        target_ns = child;
                    } else {
                        found_path = false;
                        break;
                    }
                }
                if (found_path) {
                    if (auto un = target_ns->get_union(type_name.back())) {
                        return un->get_struct_type();
                    }
                }
            }
            // Multi-part union name: navigate through aggregates to find the union
            // e.g. Outer::Inner where Outer is a struct and Inner is a nested union
            if (auto ah_ptr = std::dynamic_pointer_cast<const aggregate_holder>(current)) {
                if (auto first_agg = ah_ptr->get_aggregate(type_name.front())) {
                    // Navigate through intermediate aggregates
                    std::shared_ptr<const aggregate> nav_agg = first_agg;
                    bool found_path = true;
                    for (size_t i = 1; i + 1 < type_name.size(); ++i) {
                        auto nested = nav_agg->get_aggregate(type_name[i]);
                        if (nested) {
                            nav_agg = nested;
                        } else {
                            found_path = false;
                            break;
                        }
                    }
                    if (found_path) {
                        if (auto un = nav_agg->get_union(type_name.back())) {
                            return un->get_struct_type();
                        }
                    }
                }
            }
        }
        // Also look for enum types (simple names only for now)
        if (type_name.size() == 1) {
            if (auto eh = std::dynamic_pointer_cast<const enum_holder>(current)) {
                if (auto en = eh->get_enum(type_name.front())) {
                    return en->get_enum_type();
                }
            }
        } else {
            // Multi-part enum name: navigate namespaces to find the enum
            // (mirrors the multi-part union navigation above).
            if (auto nspc = std::dynamic_pointer_cast<const ns>(current)) {
                auto target_ns = nspc;
                bool found_path = true;
                for (size_t i = 0; i + 1 < type_name.size(); ++i) {
                    auto child = target_ns->get_child_namespace(type_name[i]);
                    if (child) {
                        target_ns = child;
                    } else {
                        found_path = false;
                        break;
                    }
                }
                if (found_path) {
                    if (auto en = target_ns->get_enum(type_name.back())) {
                        return en->get_enum_type();
                    }
                }
            }
            // Multi-part enum name: navigate through aggregates to find a nested enum
            // e.g. Outer::Inner where Outer is a struct and Inner is a nested enum.
            if (auto ah_ptr = std::dynamic_pointer_cast<const aggregate_holder>(current)) {
                if (auto first_agg = ah_ptr->get_aggregate(type_name.front())) {
                    std::shared_ptr<const aggregate> nav_agg = first_agg;
                    bool found_path = true;
                    for (size_t i = 1; i + 1 < type_name.size(); ++i) {
                        auto nested = nav_agg->get_aggregate(type_name[i]);
                        if (nested) {
                            nav_agg = nested;
                        } else {
                            found_path = false;
                            break;
                        }
                    }
                    if (found_path) {
                        if (auto en = nav_agg->get_enum(type_name.back())) {
                            return en->get_enum_type();
                        }
                    }
                }
            }
        }

        // Step 4: At each scope level, check using directives
        // Check using directives at this scope level for type resolution
        if (auto uh = std::dynamic_pointer_cast<const using_holder>(current)) {
            for (const auto& dir : uh->get_using_directives()) {
                if (dir.is_namespace() && !dir.has_alias()) {
                    // 'using namespace X::Y;' (anonymous) — search type_name within the target
                    auto target_elem = resolve_using_target(dir.target_name, _unit);
                    if (!target_elem) {
                        if (auto imp_agg = _unit.get_or_create_imported_aggregate(dir.target_name, _context)) {
                            target_elem = std::dynamic_pointer_cast<const element>(imp_agg);
                        }
                    }
                    if (target_elem) {
                        if (auto st = resolve_struct_from(*target_elem, type_name)) return st->get_struct_type();
                        if (type_name.size() == 1) {
                            if (auto uh_ptr = std::dynamic_pointer_cast<const union_holder>(target_elem)) {
                                if (auto un = uh_ptr->get_union(type_name.front())) {
                                    return un->get_struct_type();
                                }
                            }
                            if (auto eh = std::dynamic_pointer_cast<const enum_holder>(target_elem)) {
                                if (auto en = eh->get_enum(type_name.front())) {
                                    return en->get_enum_type();
                                }
                            }
                        }
                    }

                } else if (dir.is_namespace() && dir.has_alias()) {
                    // 'using M = namespace X::Y;' — M acts as a prefix: M::Type
                    if (type_name.front() == *dir.alias_name && type_name.size() > 1) {
                        auto rest = type_name.without_front();
                        auto target_elem = resolve_using_target(dir.target_name, _unit);
                        if (!target_elem) {
                            if (auto imp_agg = _unit.get_or_create_imported_aggregate(dir.target_name, _context)) {
                                target_elem = std::dynamic_pointer_cast<const element>(imp_agg);
                            }
                        }
                        if (target_elem) {
                            if (auto st = resolve_struct_from(*target_elem, rest)) return st->get_struct_type();
                            if (rest.size() == 1) {
                                if (auto eh = std::dynamic_pointer_cast<const enum_holder>(target_elem)) {
                                    if (auto en = eh->get_enum(rest.front())) {
                                        return en->get_enum_type();
                                    }
                                }
                            }
                        }
                        // Fallback: construct FQ name and search imported modules
                        {
                            auto fq = dir.target_name;
                            for (size_t i = 0; i < rest.size(); ++i) fq = fq.with_back(rest[i]);
                            if (auto imp_agg = _unit.get_or_create_imported_aggregate(fq, _context)) {
                                return imp_agg->get_struct_type();
                            }
                            if (auto imp_en = _unit.get_or_create_imported_enum(fq, _context)) {
                                return imp_en->get_enum_type();
                            }
                        }
                    }

                } else {
                    // Specific using, with or without alias
                    const std::string& real_name = dir.target_name.back();
                    const std::string& lookup_name = dir.has_alias() ? *dir.alias_name : real_name;
                    if (type_name.front() == lookup_name) {
                        auto parent_name = dir.target_name.without_back();
                        std::shared_ptr<const element> parent_elem;
                        if (parent_name.empty()) {
                            parent_elem = _unit.get_root_namespace();
                        } else {
                            parent_elem = resolve_using_target(parent_name, _unit);
                            if (!parent_elem) {
                                if (auto imp_agg = _unit.get_or_create_imported_aggregate(parent_name, _context)) {
                                    parent_elem = std::dynamic_pointer_cast<const element>(imp_agg);
                                }
                            }
                        }
                        if (parent_elem) {
                            if (type_name.size() == 1) {
                                if (auto st = resolve_struct_from(*parent_elem, k::name{real_name})) return st->get_struct_type();
                                if (auto eh = std::dynamic_pointer_cast<const enum_holder>(parent_elem)) {
                                    if (auto en = eh->get_enum(real_name)) return en->get_enum_type();
                                }
                            } else {
                                if (auto ah = std::dynamic_pointer_cast<const aggregate_holder>(parent_elem)) {
                                    if (auto agg = ah->get_aggregate(real_name)) {
                                        if (auto st = resolve_struct_from(*agg, type_name.without_front())) return st->get_struct_type();
                                    }
                                }
                                if (auto nspc = std::dynamic_pointer_cast<const ns>(parent_elem)) {
                                    if (auto child = nspc->get_child_namespace(real_name)) {
                                        if (auto st = resolve_struct_from(*child, type_name.without_front())) return st->get_struct_type();
                                    }
                                }
                            }
                        }
                        // Fallback: try imported modules directly using the full target name
                        if (type_name.size() == 1) {
                            if (auto imp_agg = _unit.get_or_create_imported_aggregate(dir.target_name, _context)) {
                                return imp_agg->get_struct_type();
                            }
                            if (auto imp_en = _unit.get_or_create_imported_enum(dir.target_name, _context)) {
                                return imp_en->get_enum_type();
                            }
                        }
                    }
                }
            }
        }
    }

    // Step 5: Fallback: imported aggregates and enums
    // Fallback: search imported modules (relative name, scope chain exhausted)
    if (auto agg = _unit.get_or_create_imported_aggregate(type_name, _context)) {
        return agg->get_struct_type();
    }
    for (const auto& imp : _unit.get_imports()) {
        if (imp.module_name.empty()) continue;
        if (type_name.size() <= imp.module_name.size()) continue;
        if (!type_name.start_with(imp.module_name)) continue;
        auto rest = type_name.without_front(imp.module_name.size());
        if (auto agg = _unit.get_or_create_imported_aggregate(rest, _context)) {
            return agg->get_struct_type();
        }
    }
    // Fallback: search imported enums
    if (auto en = _unit.get_or_create_imported_enum(type_name, _context)) {
        return en->get_enum_type();
    }
    for (const auto& imp : _unit.get_imports()) {
        if (imp.module_name.empty()) continue;
        if (type_name.size() <= imp.module_name.size()) continue;
        if (!type_name.start_with(imp.module_name)) continue;
        auto rest = type_name.without_front(imp.module_name.size());
        if (auto en = _unit.get_or_create_imported_enum(rest, _context)) {
            return en->get_enum_type();
        }
    }
    return {};
}

// ── Resolve a single type reference (for parameters and member variables) ────

// ── Resolve a single type reference (for parameters and member variables) ────

static std::shared_ptr<type>
resolve_one_type(const std::shared_ptr<type>& t,
                 aggregate_type_resolver& resolver,
                 const element& context_elem,
                 std::shared_ptr<context> ctx) {
    if (type::is_resolved(t)) return t;

    // A callable prototype ('(int):bool') is a leaf of its own: its components
    // are resolved individually, never by name lookup on the whole thing.
    if (auto ucall = std::dynamic_pointer_cast<unresolved_callable_type>(t)) {
        return resolver.resolve_unresolved_callable_type(ucall, context_elem);
    }

    // ── Template instantiation path (try FIRST for template types) ───────
    // If the type is an unresolved_type carrying AST template arguments
    // (e.g. Box<int>), try template instantiation before calling resolve_type,
    // which would emit a spurious "cannot resolve type" for the base name.
    if (auto unres = std::dynamic_pointer_cast<unresolved_type>(t)) {
        // Template parameter placeholders (a bare 'T'/'E'/'R' inside a template
        // definition) must remain unresolved until instantiation — never bind
        // them to an ambient same-named symbol in the consumer scope.
        if (unres->is_template_param_placeholder()) return t;
        if (unres->has_template_args()) {
            auto tpl_resolved = resolver.try_instantiate_template_type(unres, context_elem);
            if (tpl_resolved && type::is_resolved(tpl_resolved)) return tpl_resolved;
        }
    }

    // Composite wrapper path (e.g. Box<int>!, Box<int>&, const Box<int>?, ...)
    // Peel wrappers until the inner unresolved type, resolve it, then rebuild
    // the wrapper chain around the resolved inner type.
    {
        enum class WrapKind { Ref, Ptr, Link, View, Const, Owner, Drain, Array, SizedArray };
        struct wrap_item {
            WrapKind kind;
            unsigned long size = 0;
        };

        std::vector<wrap_item> wrappers;
        auto inner = t;
        while (inner && !std::dynamic_pointer_cast<unresolved_type>(inner)) {
            if (type::is_reference(inner)) {
                wrappers.push_back({WrapKind::Ref});
            } else if (type::is_pointer(inner)) {
                wrappers.push_back({WrapKind::Ptr});
            } else if (type::is_link(inner)) {
                wrappers.push_back({WrapKind::Link});
            } else if (type::is_view(inner)) {
                wrappers.push_back({WrapKind::View});
            } else if (type::is_const(inner)) {
                wrappers.push_back({WrapKind::Const});
            } else if (type::is_owner(inner)) {
                wrappers.push_back({WrapKind::Owner});
            } else if (type::is_drain(inner)) {
                wrappers.push_back({WrapKind::Drain});
            } else if (auto sat = std::dynamic_pointer_cast<sized_array_type>(inner)) {
                wrappers.push_back({WrapKind::SizedArray, sat->get_size()});
            } else if (type::is_array(inner)) {
                wrappers.push_back({WrapKind::Array});
            } else {
                break;
            }
            inner = inner->get_subtype();
        }

        if (auto unres_inner = std::dynamic_pointer_cast<unresolved_type>(inner)) {
            // A wrapped template parameter placeholder (e.g. member of type 'E&'
            // inside a template definition) must stay unresolved until instantiation.
            if (!unres_inner->is_template_param_placeholder()) {
            std::shared_ptr<type> resolved_inner;
            if (unres_inner->has_template_args()) {
                resolved_inner = resolver.try_instantiate_template_type(unres_inner, context_elem);
            }
            if (!resolved_inner || !type::is_resolved(resolved_inner)) {
                resolved_inner = resolver.resolve_type_by_name(unres_inner->type_id(), context_elem);
            }
            if (!resolved_inner || !type::is_resolved(resolved_inner)) {
                resolved_inner = ctx->from_string(unres_inner->type_id());
            }
            if (resolved_inner && type::is_resolved(resolved_inner)) {
                auto rebuilt = resolved_inner;
                for (auto it = wrappers.rbegin(); it != wrappers.rend(); ++it) {
                    switch (it->kind) {
                        case WrapKind::Ref:        rebuilt = rebuilt->get_reference();        break;
                        case WrapKind::Ptr:        rebuilt = rebuilt->get_pointer();          break;
                        case WrapKind::Link:       rebuilt = rebuilt->get_link();             break;
                        case WrapKind::View:       rebuilt = rebuilt->get_view();             break;
                        case WrapKind::Const:      rebuilt = rebuilt->get_const();            break;
                        case WrapKind::Owner:      rebuilt = rebuilt->get_owner();            break;
                        case WrapKind::Drain:      rebuilt = rebuilt->get_drain();            break;
                        case WrapKind::Array:      rebuilt = rebuilt->get_array();            break;
                        case WrapKind::SizedArray: rebuilt = rebuilt->get_array(it->size);    break;
                    }
                }
                if (rebuilt && type::is_resolved(rebuilt)) {
                    // An addresser applied to a name that turned out to denote a
                    // callable re-addresses it in place instead of wrapping it.
                    if (auto collapsed = ctx->collapse_callable_addresser(rebuilt)) return collapsed;
                    return rebuilt;
                }
            }
            } // end !is_template_param_placeholder
        }
    }

    // Composite type (reference_type, pointer_type, etc. wrapping an unresolved subtype)
    auto resolved_composite = ctx->resolve_type(t);
    if (type::is_resolved(resolved_composite)) return resolved_composite;

    auto unres = std::dynamic_pointer_cast<unresolved_type>(t);
    if (!unres) return t; // cannot resolve further

    auto resolved = resolver.resolve_type_by_name(unres->type_id(), context_elem);
    if (!resolved || !type::is_resolved(resolved)) {
        resolved = ctx->from_string(unres->type_id());
    }
    return resolved;
}

// ── Visitors ─────────────────────────────────────────────────────────────────

void aggregate_type_resolver::resolve() {
    trace("[aggregate_type_resolver::resolve] begin");
    visit_unit(_unit);
    trace("[aggregate_type_resolver::resolve] done");
}

void aggregate_type_resolver::visit_unit(unit& /*unit*/) {
    visit_namespace(*_unit.get_root_namespace());
    // Note: global_constructor_function, global_destructor_function and global_main_function
    // are handled by type_reference_resolver — they contain expressions and statements.
    // aggregate_type_resolver only handles declaration-level type resolution.
}

void aggregate_type_resolver::visit_namespace(ns& ns) {
    // Use index-based loop: template instantiation can add new aggregates
    // to this namespace's children list, invalidating range-based iterators.
    for (size_t i = 0; i < ns.get_children().size(); ++i) {
        ns.get_children()[i]->accept(*this);
    }
}

void aggregate_type_resolver::visit_aggregate(aggregate& st) {
    // Skip template definitions — they are not instantiated yet.
    // But first resolve constraint_type and default_type in their tpl_info
    // so that constraint validation works correctly.
    if (st.is_template()) {
        if (auto* ti = st.get_tpl_info()) {
            for (auto& param : ti->params) {
                if (param.constraint_type && !type::is_resolved(param.constraint_type)) {
                    auto resolved = _context->resolve_type(param.constraint_type);
                    if (resolved && type::is_resolved(resolved)) {
                        param.constraint_type = resolved;
                    } else if (auto unres = std::dynamic_pointer_cast<unresolved_type>(param.constraint_type)) {
                        auto r = resolve_type_by_name(unres->type_id(), st);
                        if (r && type::is_resolved(r)) param.constraint_type = r;
                    }
                }
                if (param.default_type && !type::is_resolved(param.default_type)) {
                    auto resolved = _context->resolve_type(param.default_type);
                    if (resolved && type::is_resolved(resolved)) {
                        param.default_type = resolved;
                    } else if (auto unres = std::dynamic_pointer_cast<unresolved_type>(param.default_type)) {
                        auto r = resolve_type_by_name(unres->type_id(), st);
                        if (r && type::is_resolved(r)) param.default_type = r;
                    }
                }
            }
        }

        // Also resolve non-placeholder types inside the template body so that
        // using aliases are resolved before KDI export.  Template parameter
        // placeholders (is_template_param_placeholder) remain unresolved by
        // design — resolve_one_type / resolve_type_by_name will simply return
        // nullptr for those, leaving them as-is.

        // Visit nested aggregate children first (depth-first), so their types
        // are available before we process members of the outer template.
        // This is needed for e.g. a nested static struct with self-referential
        // owner fields (like a LinkedListNode inside a LinkedList template).
        // Because the symbol_resolver skips template aggregates entirely,
        // non-template nested aggregates inside templates never get their
        // struct_type created. We pre-create it here before visiting.
        for (auto& child : st.get_children()) {
            if (auto nested = std::dynamic_pointer_cast<aggregate>(child)) {
                if (!nested->is_template() && !nested->get_struct_type()) {
                    std::shared_ptr<struct_type> nested_st_type{
                        new struct_type(nested->get_short_name(), nested->shared_as<aggregate>())};
                    _context->add_struct(nested_st_type);
                    nested->set_struct_type(nested_st_type);

                    // Create 'this' parameters for member functions
                    // (normally done by symbol_resolver, which skips templates).
                    for (auto& nc : nested->get_children()) {
                        if (auto fn = std::dynamic_pointer_cast<function>(nc)) {
                            if (fn->is_member() && !fn->is_static()) {
                                fn->create_this_parameter();
                            }
                        }
                    }
                    for (auto& ctor : nested->constructors()) {
                        if (ctor && !ctor->get_this_parameter()) {
                            ctor->create_this_parameter();
                        }
                    }
                    if (auto dtor = nested->get_destructor()) {
                        if (!dtor->get_this_parameter()) {
                            dtor->create_this_parameter();
                        }
                    }
                }
                nested->accept(*this);
            } else if (auto nested_un = std::dynamic_pointer_cast<union_type_def>(child)) {
                nested_un->accept(*this);
            }
        }

        // Member variables and static members
        for (auto& child : st.get_children()) {
            if (auto mv = std::dynamic_pointer_cast<member_variable_definition>(child)) {
                mv->accept(*this);
            } else if (auto gv = std::dynamic_pointer_cast<global_variable_definition>(child)) {
                gv->accept(*this);
            } else if (auto fn = std::dynamic_pointer_cast<function>(child)) {
                // Resolve function signatures (parameters + return type) but
                // NOT bodies — those are handled during instantiation.
                if (!fn->is_compiler_generated())
                    visit_function(*fn);
            }
        }

        // Constructors
        for (auto& ctor : st.constructors()) {
            if (ctor && !ctor->is_compiler_generated())
                visit_function(*ctor);
        }

        // Destructor
        if (auto dtor = st.get_destructor()) {
            if (!dtor->is_compiler_generated())
                visit_function(*dtor);
        }

        return;
    }

    // Visit nested aggregate and union children first (depth-first), so their types are available
    // before we process members of the outer aggregate.
    for (auto& child : st.get_children()) {
        if (auto nested = std::dynamic_pointer_cast<aggregate>(child)) {
            nested->accept(*this);
        } else if (auto nested_un = std::dynamic_pointer_cast<union_type_def>(child)) {
            nested_un->accept(*this);
        }
    }

    // Visit member variable children to resolve their types.
    // This triggers template instantiation for types like Wrapper<int>,
    // ensuring concrete instantiations exist before resolve_types() builds
    // LLVM struct types.
    for (auto& child : st.get_children()) {
        if (auto mv = std::dynamic_pointer_cast<member_variable_definition>(child)) {
            mv->accept(*this);
        } else if (auto gv = std::dynamic_pointer_cast<global_variable_definition>(child)) {
            gv->accept(*this);
        }
    }

    // For template instantiations, the symbol_resolver/signature_resolver passes
    // ran before this aggregate existed, so their constructor and function
    // signatures (parameter and return types referencing template arguments,
    // e.g. Iface<T>* → Iface<int>*) were never resolved. Resolve them here,
    // reusing visit_function which consumes the instantiation's model template
    // arguments. Bodies are handled later by type_reference_resolver.
    if (st.has_tpl_args()) {
        for (auto& child : st.get_children()) {
            if (auto fn = std::dynamic_pointer_cast<function>(child)) {
                if (!fn->is_compiler_generated())
                    visit_function(*fn);
            }
        }
        for (auto& ctor : st.constructors()) {
            if (ctor && !ctor->is_compiler_generated())
                visit_function(*ctor);
        }
        if (auto dtor = st.get_destructor()) {
            if (!dtor->is_compiler_generated())
                visit_function(*dtor);
        }
    }
}

void aggregate_type_resolver::visit_klass(klass& klass) {
    visit_aggregate(klass);

    // If vtable is missing (e.g. template instantiation created after symbol_resolver),
    // build it now via the canonical, shared algorithm (handles the universal
    // destructor slot 0, arbitrary-depth base recursion, and secondary-base
    // override linking) — see resolvers_common.hpp's ensure_klass_vtable_built().
    // Not calling this used to duplicate that logic in a stale, incomplete copy
    // that omitted destructor-slot handling entirely, silently letting the first
    // own virtual method collide with slot 0 (the destructor's slot).
    if (!klass.has_vtable() && (klass.is_class() || std::dynamic_pointer_cast<model::interface>(klass.shared_as<element>()))) {
        ensure_klass_vtable_built(klass);
    }

    // Build the LLVM struct type for the vtable (mirrors type_reference_resolver::visit_klass)
    if (!klass.has_vtable()) return;

    auto vt = klass.get_vtable();
    size_t num_slots = vt->slot_count();

    llvm::LLVMContext& llvm_ctx = **_context;
    llvm::Type* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);

    std::vector<llvm::Type*> vtable_fields;
    vtable_fields.push_back(ptr_ty); // RTTI placeholder
    for (size_t i = 0; i < num_slots; ++i) {
        vtable_fields.push_back(ptr_ty);
    }
    std::string vtable_struct_name = "__vtable_" + klass.get_short_name() + "__";
    // Only create if not already created (idempotency)
    auto* existing = llvm::StructType::getTypeByName(llvm_ctx, vtable_struct_name);
    if (!existing) {
        existing = llvm::StructType::create(llvm_ctx, vtable_fields, vtable_struct_name);
    }
    vt->llvm_type = existing;
}

void aggregate_type_resolver::visit_interface(interface& iface) {
    visit_klass(iface);
}

void aggregate_type_resolver::visit_member_variable_definition(member_variable_definition& var) {
    // __parent__ is already assigned a resolved type by symbol_resolver
    if (var.get_short_name() == "__parent__") return;
    // Skip members with no type yet (e.g. annotation fields before resolution)
    if (!var.get_type()) return;

    if (!type::is_resolved(var.get_type())) {
        if (auto ufrt = std::dynamic_pointer_cast<unresolved_callable_type>(var.get_type())) {
            // A callable-typed member (`fn : *(int):int;`) is resolved like a global one:
            // resolve_one_type() does not know about callable signatures.
            if (auto resolved = resolve_unresolved_callable_type(ufrt, var)) {
                var.set_type(resolved);
            }
            return;
        }
        auto resolved = resolve_one_type(var.get_type(), *this, var, _context);
        if (resolved && type::is_resolved(resolved)) {
            var.set_type(resolved);
        }
    }
    // Do NOT visit init expressions — those are expressions, handled by type_reference_resolver
}

/**
 * Visit and resolve the type of a global variable during Phase 1.a.
 *
 * Handles unresolved_callable_type by resolving parameter types.
 * For other types, delegates to resolve_one_type.
 * Does NOT visit init expressions (Phase 1.b handles those).
 */
/**
 * Best-effort early resolution of an unresolved callable type during phase 1.a.
 *
 * A type_reference_resolver (which owns the full resolve_callable_type) has not run
 * yet, so the signature is rebuilt from the components that context::from_type_specifier
 * already resolved, falling back to a by-name lookup in @p scope for the others.
 */
std::shared_ptr<type> aggregate_type_resolver::resolve_unresolved_callable_type(
    const std::shared_ptr<unresolved_callable_type>& ufrt,
    const element& scope)
{
    if (!ufrt) return nullptr;
    callable_type_builder builder(_context);
    builder.addresser(ufrt->get_addresser());
    bool all_resolved = true;

    // A component of a callable signature can be an arbitrary type expression
    // (a template instantiation, a wrapped name, another callable...), so it is
    // resolved with the very same routine as any other Phase 1.a type reference.
    auto resolve_component = [&](const std::shared_ptr<type>& t) -> std::shared_ptr<type> {
        if (!t) return nullptr;
        if (type::is_resolved(t)) return t;
        if (auto nested = std::dynamic_pointer_cast<unresolved_callable_type>(t)) {
            return resolve_unresolved_callable_type(nested, scope);
        }
        auto r = resolve_one_type(t, *this, scope, _context);
        return (r && type::is_resolved(r)) ? r : nullptr;
    };

    // Unbound member function reference (e.g. Counter::*(int):int)
    if (!ufrt->owner_name().empty()) {
        std::shared_ptr<aggregate> owner_agg = resolve_struct_from(scope, ufrt->owner_name());
        if (!owner_agg) {
            auto root_ns = _unit.get_root_namespace();
            if (root_ns) owner_agg = resolve_struct_from(*root_ns, ufrt->owner_name());
        }
        if (owner_agg) builder.member_of(owner_agg);
        else all_resolved = false;
    }
    for (const auto& pt : ufrt->parameter_types()) {
        auto rpt = resolve_component(pt);
        if (!rpt) { all_resolved = false; break; }
        builder.append_parameter_type(rpt);
    }
    if (all_resolved && ufrt->get_return_type()) {
        auto rt = resolve_component(ufrt->get_return_type());
        if (rt) builder.return_type(rt);
        else all_resolved = false;
    }
    if (all_resolved && !ufrt->get_throws().empty()) {
        std::vector<std::shared_ptr<type>> thrown;
        for (const auto& tt : ufrt->get_throws()) {
            auto rtt = resolve_component(tt);
            if (!rtt) { all_resolved = false; break; }
            thrown.push_back(rtt);
        }
        if (all_resolved) builder.throws(thrown);
    }
    if (!all_resolved) return nullptr;
    return builder.build();
}

void aggregate_type_resolver::visit_global_variable_definition(global_variable_definition& var) {
    if (!var.get_type()) return; // No type yet (e.g. unprocessed static member)
    if (!type::is_resolved(var.get_type())) {
        if (auto ufrt = std::dynamic_pointer_cast<unresolved_callable_type>(var.get_type())) {
            if (auto resolved = resolve_unresolved_callable_type(ufrt, var)) {
                var.set_type(resolved);
            }
        } else {
            auto resolved = resolve_one_type(var.get_type(), *this, var, _context);
            if (resolved && type::is_resolved(resolved)) {
                var.set_type(resolved);
            }
        }
    }
    // Do NOT register in global_constructor — that is done by type_reference_resolver
    // Do NOT visit init expressions — those are expressions, handled by type_reference_resolver
}

/**
 * Resolve the type of a function parameter during Phase 1.a (signatures only).
 *
 * Steps:
 *   1. Handle unresolved_callable_type: resolve parameter types and optional owner.
 *   2. For other types: try context->resolve_type, then peel composite wrappers to find
 *      the inner unresolved_type, resolve it by name, and re-apply wrappers.
 *
 * Does NOT process default expressions (type_reference_resolver handles those).
 */
void aggregate_type_resolver::visit_parameter(parameter& param) {
    // Some synthesized/template-only params can be temporarily typeless during early resolution.
    if (!param.get_type()) return;

    // Step 1: Handle unresolved_callable_type: resolve parameter types and optional owner
    // Resolve the type only (no default expressions — those are handled by type_reference_resolver)
    if (!type::is_resolved(param.get_type())) {
        // Handle unresolved_callable_type (function pointer/pin/link parameter type)
        if (auto ufrt = std::dynamic_pointer_cast<unresolved_callable_type>(param.get_type())) {
            // Delegate to the shared routine: it resolves every component of the
            // signature (parameters, return, *throws set*, nested callables) with the
            // regular Phase 1.a machinery. Rebuilding the callable here would silently
            // drop the throws set, and two callable types differing only by their
            // throws clause would then mangle to the same symbol.
            const element& scope = param.parent<function>()
                ? static_cast<const element&>(*param.parent<function>())
                : static_cast<const element&>(param);
            if (auto resolved = resolve_unresolved_callable_type(ufrt, scope)) {
                param.set_type(resolved);
            }
            return;
        }
        auto res_type = _context->resolve_type(param.get_type());
        if (!type::is_resolved(res_type)) {
            // Try name-based resolution.
            // The parameter type may be a composite wrapping an unresolved_type
            // (e.g. reference_type("iface_one::ICounter&")), so we peel wrappers to
            // Step 2: For other types: try context->resolve_type, then peel composite wrappers to find the inner unreso...
            // find the inner unresolved name, resolve the inner aggregate, then
            // rebuild the composite wrapper around the resolved inner type.
            auto owner_func = param.parent<function>();
            if (owner_func) {
                // Collect wrapper kinds (from outermost to innermost unresolved_type)
                enum class WrapKind { Ref, Ptr, Link, View, Const, Owner, Drain, Array };
                std::vector<WrapKind> wrappers;
                auto inner = param.get_type();
                while (inner && !std::dynamic_pointer_cast<unresolved_type>(inner)) {
                    if      (type::is_reference(inner))  wrappers.push_back(WrapKind::Ref);
                    else if (type::is_pointer(inner))    wrappers.push_back(WrapKind::Ptr);
                    else if (type::is_link(inner))       wrappers.push_back(WrapKind::Link);
                    else if (type::is_view(inner))       wrappers.push_back(WrapKind::View);
                    else if (type::is_const(inner))      wrappers.push_back(WrapKind::Const);
                    else if (type::is_owner(inner))      wrappers.push_back(WrapKind::Owner);
                    else if (type::is_drain(inner))      wrappers.push_back(WrapKind::Drain);
                    else if (type::is_array(inner))      wrappers.push_back(WrapKind::Array);
                    else break;
                    inner = inner->get_subtype();
                }
                auto unres = std::dynamic_pointer_cast<unresolved_type>(inner);
                if (unres && !unres->type_id().empty() && !unres->is_template_param_placeholder()) {
                    // Resolve the inner aggregate type
                    std::shared_ptr<type> inner_resolved;
                    // If the inner type has template args, try template instantiation first
                    if (unres->has_template_args()) {
                        inner_resolved = try_instantiate_template_type(unres, *owner_func);
                    }
                    if (!inner_resolved || !type::is_resolved(inner_resolved)) {
                        inner_resolved = resolve_type_by_name(unres->type_id(), *owner_func);
                    }
                    if (type::is_resolved(inner_resolved)) {
                        // Re-apply wrappers in reverse order (innermost first)
                        res_type = inner_resolved;
                        for (auto it = wrappers.rbegin(); it != wrappers.rend(); ++it) {
                            switch (*it) {
                                case WrapKind::Ref:   res_type = res_type->get_reference(); break;
                                case WrapKind::Ptr:   res_type = res_type->get_pointer();   break;
                                case WrapKind::Link:  res_type = res_type->get_link();      break;
                                case WrapKind::View:  res_type = res_type->get_view();      break;
                                case WrapKind::Const: res_type = res_type->get_const();     break;
                                case WrapKind::Owner: res_type = res_type->get_owner();     break;
                                case WrapKind::Drain: res_type = res_type->get_drain();     break;
                                case WrapKind::Array: res_type = res_type->get_array();     break;
                            }
                        }
                    }
                }
            }
        }
        if (type::is_resolved(res_type)) {
            param.set_type(res_type);
        }
    }
}

/**
 * Resolve function signatures during Phase 1.a: this parameter, parameters, and return type.
 *
 * Steps:
 *   1. Resolve 'this' parameter type for non-static member functions.
 *   2. Resolve all parameter types.
 *   3. Resolve return type (including function pointer return types).
 *
 * Does NOT visit the function body (Phase 1.b).
 */
void aggregate_type_resolver::visit_function(function& fn) {
    // Step 1: Resolve 'this' parameter type for non-static member functions
    // Resolve 'this' parameter type for non-static member functions
    if (fn.is_member() && !fn.is_static() && fn.get_this_parameter()) {
        auto this_param = std::const_pointer_cast<parameter>(fn.get_this_parameter());
        this_param->accept(*this);
    }

    // Step 2: Resolve all parameter types
    // Resolve parameter types (signatures only, no default expressions)
    for (auto param : fn.parameters()) {
        param->accept(*this);
    }

    // Step 3: Resolve return type (including function pointer return types)
    // Resolve return type
    if (fn.get_return_type() && !type::is_resolved(fn.get_return_type())) {
        // Handle unresolved_callable_type (function pointer return type)
        if (auto ufrt = std::dynamic_pointer_cast<unresolved_callable_type>(fn.get_return_type())) {
            callable_type_builder builder(_context);
            builder.addresser(ufrt->get_addresser());
            bool all_resolved = true;
            // Unbound member function reference return type (e.g. Counter::*(int):int)
            if (!ufrt->owner_name().empty()) {
                std::shared_ptr<aggregate> owner_agg = resolve_struct_from(fn, ufrt->owner_name());
                if (!owner_agg) {
                    auto root_ns = _unit.get_root_namespace();
                    if (root_ns) owner_agg = resolve_struct_from(*root_ns, ufrt->owner_name());
                }
                if (owner_agg) builder.member_of(owner_agg);
                else all_resolved = false;
            }
            for (const auto& pt : ufrt->parameter_types()) {
                if (!type::is_resolved(pt)) {
                    if (auto u = std::dynamic_pointer_cast<unresolved_type>(pt)) {
                        auto rpt = resolve_type_by_name(u->type_id(), fn);
                        if (!rpt || !type::is_resolved(rpt)) { all_resolved = false; break; }
                        builder.append_parameter_type(rpt);
                    } else { all_resolved = false; break; }
                } else {
                    builder.append_parameter_type(pt);
                }
            }
            if (all_resolved && ufrt->get_return_type()) {
                auto rt = ufrt->get_return_type();
                if (!type::is_resolved(rt)) {
                    if (auto u = std::dynamic_pointer_cast<unresolved_type>(rt)) {
                        rt = resolve_type_by_name(u->type_id(), fn);
                    } else {
                        rt = nullptr;
                    }
                }
                if (rt && type::is_resolved(rt)) builder.return_type(rt);
                else all_resolved = false;
            }
            if (all_resolved) {
                auto resolved = builder.build();
                if (resolved) fn.set_return_type(resolved);
            }
        } else {
        // Try template instantiation for return types with template args (e.g. Box<int>)
        auto unres_ret = std::dynamic_pointer_cast<unresolved_type>(fn.get_return_type());
        // Template parameter placeholders (e.g. a bare 'T'/'E' return type inside a
        // template definition) must remain unresolved until instantiation — never bind
        // them to an ambient same-named symbol in the consumer scope.
        if (unres_ret && unres_ret->is_template_param_placeholder()) {
            // leave as-is
        } else {
        std::shared_ptr<type> resolved;
        if (unres_ret && unres_ret->has_template_args()) {
            resolved = try_instantiate_template_type(unres_ret, fn);
        }
        if (!resolved || !type::is_resolved(resolved)) {
            resolved = resolve_type_by_name(
                unres_ret ? unres_ret->type_id() : k::name{},
                fn);
        }
        if (resolved && type::is_resolved(resolved)) {
            fn.set_return_type(resolved);
        } else {
            auto resolved2 = _context->resolve_type(fn.get_return_type());
            if (type::is_resolved(resolved2)) fn.set_return_type(resolved2);
        }
        } // end else (placeholder return type)
        } // end else (not unresolved_callable_type)
    }

    // NOTE: the block / body is NOT visited here.
    // That is the responsibility of type_reference_resolver (Phase 1.b).
}

void aggregate_type_resolver::visit_constructor(constructor& ctor) {
    visit_function(ctor);
}

void aggregate_type_resolver::visit_destructor(destructor& dtor) {
    visit_function(dtor);
}

void aggregate_type_resolver::visit_static_constructor(static_constructor& sctor) {
    visit_function(sctor);
}

void aggregate_type_resolver::visit_static_destructor(static_destructor& sdtor) {
    visit_function(sdtor);
}

void aggregate_type_resolver::visit_global_constructor_function(global_constructor_function& func) {
    visit_function(func);
}

void aggregate_type_resolver::visit_global_destructor_function(global_destructor_function& func) {
    visit_function(func);
}

void aggregate_type_resolver::visit_global_main_function(global_main_function& /*func*/) {
    // Nothing to do — global_main_function is created and resolved in type_reference_resolver
}

void aggregate_type_resolver::visit_union(union_type_def& un) {
    // Skip template definitions — only instantiations are resolved
    if (un.is_template()) return;

    // Skip a union nested inside a template *definition*: it is a blueprint, not a type.
    // Resolving it would give it a struct_type / LLVM type that every instantiation would
    // then share (template_instantiator::clone_nested_union used to reuse it), collapsing
    // sibling instantiations with differently-sized payloads onto a single layout.
    for (auto parent = un.parent<element>(); parent; parent = parent->parent<element>()) {
        if (auto agg = std::dynamic_pointer_cast<aggregate>(parent)) {
            if (agg->is_template()) return;
        }
    }

    // ── Ensure base union is fully resolved first ─────────────────────────────
    if (un.has_base_union() && un.get_base_union()) {
        auto base = un.get_base_union();
        if (!base->get_struct_type() || !base->get_struct_type()->get_llvm_type()) {
            visit_union(*base);
        }
        // Re-apply reindex (idempotent) in case the base gained more alternatives
        // from its own base resolution during this same pass.
        un.reindex_own_alternatives();
    }

    // Resolve each own alternative's type
    for (auto& alt : un.alternatives_mutable()) {
        if (alt.resolved_type && !type::is_resolved(alt.resolved_type)) {
            // First try context->resolve_type (handles primitive wrappers, pointers, etc.)
            auto resolved = _context->resolve_type(alt.resolved_type);
            if (resolved && type::is_resolved(resolved)) {
                alt.resolved_type = resolved;
            } else {
                // Try resolve_type_by_name for aggregate/enum types
                if (auto unres = std::dynamic_pointer_cast<unresolved_type>(alt.resolved_type)) {
                    // Template parameter placeholders must remain unresolved until
                    // instantiation — never bind them to an ambient same-named symbol.
                    if (!unres->is_template_param_placeholder()) {
                        auto by_name = resolve_type_by_name(unres->type_id(), un);
                        if (by_name && type::is_resolved(by_name)) {
                            alt.resolved_type = by_name;
                        }
                    }
                }
            }
        }
    }

    // ── Validate polymorphic union alternatives ──────────────────────────────
    if (un.is_polymorphic()) {
        auto poly_base = un.get_polymorphic_base();
        for (const auto& alt : un.alternatives()) {
            if (!alt.resolved_type) continue;
            auto bare_alt = type::remove_const(alt.resolved_type);
            auto st_type = std::dynamic_pointer_cast<struct_type>(bare_alt);
            if (!st_type || !st_type->get_struct()) {
                lex::opt_any_lexeme un_lexeme;
                if (auto ast_decl = un.get_ast_aggregate_decl()) {
                    un_lexeme = lex::any_lexeme{ast_decl->kw_aggregate_type};
                }
                throw_error(static_cast<unsigned int>(k::diag::union_diag::ERR_UNION_POLYMORPHIC_ALT_NOT_CLASS),
                    un_lexeme,
                    "Alternative '{}' in polymorphic union '{}' has type '{}' which is not a class type",
                    {alt.name, un.get_short_name(), alt.resolved_type->to_string()});
            }
            auto alt_agg = st_type->get_struct();
            if (alt_agg->is_abstract() || alt_agg->is_interface()) {
                lex::opt_any_lexeme un_lexeme;
                if (auto ast_decl = un.get_ast_aggregate_decl()) {
                    un_lexeme = lex::any_lexeme{ast_decl->kw_aggregate_type};
                }
                throw_error(static_cast<unsigned int>(k::diag::union_diag::ERR_UNION_POLYMORPHIC_ALT_ABSTRACT),
                    un_lexeme,
                    "Alternative '{}' in polymorphic union '{}' is {} '{}'; abstract classes and interfaces cannot be union alternatives",
                    {alt.name, un.get_short_name(), alt_agg->is_interface() ? "an interface" : "an abstract class", alt_agg->get_short_name()});
            }
            if (alt_agg.get() != poly_base.get() && !alt_agg->is_derived_from(poly_base)) {
                lex::opt_any_lexeme un_lexeme;
                if (auto ast_decl = un.get_ast_aggregate_decl()) {
                    un_lexeme = lex::any_lexeme{ast_decl->kw_aggregate_type};
                }
                throw_error(static_cast<unsigned int>(k::diag::union_diag::ERR_UNION_POLYMORPHIC_ALT_NOT_DERIVED),
                    un_lexeme,
                    "Alternative '{}' of type '{}' in polymorphic union '{}' does not inherit from base type '{}'",
                    {alt.name, alt_agg->get_short_name(), un.get_short_name(), poly_base->get_short_name()});
            }
        }
    }

    // If the union already has a struct_type with an LLVM type attached
    // (set during template instantiation or KDI import materialisation),
    // skip LLVM type creation to avoid replacing the struct_type pointer
    // that other type references (e.g. parameters) use.
    // However, if struct_type exists but has NO LLVM type (created early by
    // symbol_resolver for nested union resolution), we must still attach one.
    if (un.get_struct_type() && un.get_struct_type()->get_llvm_type()) {
        trace("[aggregate_type_resolver::visit_union] union '{}' already has struct_type with LLVM type, skipping",
            {un.get_short_name()});
        // Still need to set up Kind enum for this union
        un.resynthesise_kind_enum();
        if (auto kind_enum = un.get_kind_enum()) {
            if (!kind_enum->get_enum_type()) {
                auto uint_type = _context->from_type(primitive_type::UNSIGNED_INT);
                kind_enum->set_underlying_type(uint_type);
                auto et = std::shared_ptr<enum_type>(new enum_type(kind_enum, uint_type));
                kind_enum->set_enum_type(et);
                _context->add_enum(kind_enum->get_fq_name(), et);
            }
        }
        return;
    }

    // Create an opaque LLVM struct type for the union.
    // The body will be set in declaration_generator::visit_union() once the
    // LLVM module (and DataLayout) is available.
    auto& llvm_ctx = _context->llvm_context();
    auto* union_llvm_type = llvm::StructType::create(llvm_ctx, un.get_mangled_name() + "_union");

    if (un.get_struct_type()) {
        // struct_type was created early by symbol_resolver; attach the LLVM type to it.
        _context->attach_llvm_struct_type(un.get_struct_type(), union_llvm_type);
    } else {
        // Create a struct_type wrapping this opaque LLVM type and register with context.
        // Use the FQ name (stripped of leading "::") so that the KDI exporter can produce
        // a fully-qualified aggregate_ref for function parameter types referencing this union.
        std::string st_name = un.get_short_name();
        {
            const std::string& fq = un.get_fq_name();
            if (fq.size() >= 2 && fq[0] == ':' && fq[1] == ':') {
                st_name = fq.substr(2);
            } else if (!fq.empty()) {
                st_name = fq;
            }
        }
        auto st_type = std::make_shared<struct_type>(st_name, std::weak_ptr<aggregate>{});
        _context->attach_llvm_struct_type(st_type, union_llvm_type);
        un.set_struct_type(st_type);
        _context->add_struct(st_type);
    }

    trace("[aggregate_type_resolver::visit_union] resolved union '{}' (opaque LLVM type created)",
        {un.get_short_name()});

    // Synthesize the Kind enum after the union struct type is established
    // (force resynthesis to include the full inheritance chain)
    un.resynthesise_kind_enum();
    if (auto kind_enum = un.get_kind_enum()) {
        if (!kind_enum->get_enum_type()) {
            // Set underlying type to uint32 (matches discriminant field)
            auto uint_type = _context->from_type(primitive_type::UNSIGNED_INT);
            kind_enum->set_underlying_type(uint_type);
            // Create and register the enum_type in context
            auto et = std::shared_ptr<enum_type>(new enum_type(kind_enum, uint_type));
            kind_enum->set_enum_type(et);
            std::string fq = kind_enum->get_fq_name();
            if (fq.empty()) fq = kind_enum->get_short_name();
            _context->add_enum(fq, et);
        }
    }
}

//
// Model materializer (Phase 2)
//

} // namespace k::model::gen
