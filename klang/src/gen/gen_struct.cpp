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
//
// gen_struct.cpp — Code generation for K language aggregates and structures  (classes have their own file).
//
// This file contains all visitor method overrides and helper functions
// related to the 'aggregated' and 'struct' feature:

#include "resolvers.hpp"
#include "generators.hpp"
#include "gen_helpers.hpp"
#include "../common/operator_names.hpp"

#include "../model/imported.hpp"
#include "../model/template_instantiator.hpp"
#include "../parse/ast.hpp"

#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Verifier.h>
#include <llvm/ADT/ScopeExit.h>
#include <llvm/Transforms/Utils/ModuleUtils.h>

#include <limits>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include "../errors.hpp"

namespace k::model::gen {

namespace {
const base_spec* enum_implicit_object_base(const std::shared_ptr<aggregate>& agg) {
    if (!agg || !agg->is_class() || agg->is_inner()) return nullptr;
    auto ast = agg->get_ast_aggregate_decl();
    if (!ast || !ast->bases.empty() || agg->get_bases().size() != 1) return nullptr;
    const auto& base = agg->get_bases().front();
    return base.base && base.base->get_name().without_root_prefix().to_string() == "k::Object"
        && !base.is_virtual ? &base : nullptr;
}

bool enum_constant_safe_type(const std::shared_ptr<type>& source, bool nested = false) {
    auto t = type::canonical(type::remove_const(source));
    if (!t || type::is_owner(t) || type::is_drain(t)
        || type::is_reference(t) || type::is_link(t)) return false;
    if (auto array = std::dynamic_pointer_cast<sized_array_type>(t))
        return enum_constant_safe_type(array->get_subtype(), true);
    auto st = std::dynamic_pointer_cast<struct_type>(t);
    if (!st) return true;
    auto agg = st->get_struct();
    if (!agg) return false;
    const auto* object_base = enum_implicit_object_base(agg);
    if (nested && object_base) return false;
    if (agg->has_bases() && !object_base) {
        if (!agg->is_struct()) return false;
        for (const auto& base : agg->get_bases()) {
            if (!base.base || base.is_virtual || !base.base->is_struct()
                || !enum_constant_safe_type(base.base->get_struct_type(), true)) return false;
        }
    }
    auto dtor = agg->get_destructor();
    if (dtor && !dtor->is_compiler_generated()) return false;
    auto copy = agg->get_copy_constructor();
    if (copy && !copy->is_compiler_generated()) return false;
    if (nested) {
        const auto& ctors = agg->constructors();
        if (std::none_of(ctors.begin(), ctors.end(), [](const auto& ctor) {
                return ctor && ctor->is_compiler_generated() && !ctor->is_deleted()
                    && ctor->parameters().empty();
            })) return false;
    }
    auto trivial_base_field = [&](const std::string& name) {
        return !object_base && std::any_of(agg->get_bases().begin(), agg->get_bases().end(),
            [&](const auto& base) { return name == "__base_" + base.sanitised_name() + "__"; });
    };
    for (auto it = st->fields_begin(); it != st->fields_end(); ++it) {
        if (it->name.rfind("__", 0) == 0 &&
            (!object_base ||
             (it->name != "__vptr__" && it->name != "__base_Object__")) &&
            !trivial_base_field(it->name)) return false;
    }
    for (const auto& [name, var] : agg->variables()) {
        auto member = std::dynamic_pointer_cast<member_variable_definition>(var);
        if (!member) continue;
        if (object_base &&
            (name == "__vptr__" || name == "__base_Object__")) continue;
        auto init = member->get_init_expr();
        auto default_init = std::dynamic_pointer_cast<constructor_invocation_expression>(init);
        if (init && (!default_init || !default_init->empty())) return false;
        if (!enum_constant_safe_type(member->get_type(), true)) return false;
    }
    return true;
}

bool enum_runtime_literal_argument(std::shared_ptr<expression> arg) {
    while (auto cast = std::dynamic_pointer_cast<cast_expression>(arg))
        arg = cast->sub_expr();
    auto value = std::dynamic_pointer_cast<value_expression>(arg);
    return value && value->is_literal();
}

bool enum_entry_requires_runtime_table(const enum_entry_def& entry,
                                       const std::shared_ptr<type>& object_type) {
    return !entry.is_alias && entry.has_paren_init &&
        (!entry.resolved_constructor || !entry.resolved_constructor->is_compiler_generated()
         || !entry.adapted_ctor_args.empty() || !enum_constant_safe_type(object_type));
}

bool enum_requires_runtime_table(const enumeration& en) {
    if (en.is_imported()) return en.has_runtime_table();
    if (auto base = en.get_base(); base && (base->is_imported() || enum_requires_runtime_table(*base)))
        return true;
    const size_t inherited = en.get_base() ? en.get_base()->entries().size() : 0;
    return std::any_of(en.entries().begin() + inherited, en.entries().end(),
        [&](const enum_entry_def& entry) {
            return enum_entry_requires_runtime_table(entry, en.get_object_type());
        });
}
}

// ─────────────────────────────────────────────────────────────────────────────
// Shared annotation resolution & @Target validation
// ─────────────────────────────────────────────────────────────────────────────

/**
 * Resolve and validate annotation instances on any annotation_holder.
 *
 * Steps:
 *   1. For each annotation instance: look up the annotation type by name.
 *   2. Validate that the resolved type is an annotation_type.
 *   3. Check @Target constraints: the element_kind must be in the allowed list.
 *   4. Resolve annotation argument expressions.
 */
void symbol_resolver::resolve_and_validate_annotations(
    annotation_holder& holder,
    element& scope,
    const std::string& element_name,
    const lex::opt_any_lexeme& err_lexeme,
    const std::string& element_kind)
{
    //── Phase 1: resolve each annotation_instance to a concrete annotation_type
    for (auto& ann_inst : holder.get_annotations_mutable()) {
        if (ann_inst.resolved_type) continue; // already resolved

        // Step 1: For each annotation instance: look up the annotation type by name
        // Look up by name in local scope
        auto ann_agg = scope_lookup::lookup_structure(scope.shared_as<element>(), ann_inst.raw_name);

        // If not found and the name is qualified (contains ::), try local
        // namespace traversal: walk up to a namespace parent and descend
        // through child namespaces to find the annotation type.
        if (!ann_agg && ann_inst.raw_name.find("::") != std::string::npos) {
            auto qname = k::name::from(ann_inst.raw_name);
            const auto& parts = qname.parts();
            // Walk up the scope chain looking for a namespace that has the first part
            for (auto current = scope.shared_as<element>(); current && !ann_agg;
                 current = current->parent<element>()) {
                auto ns_ptr = std::dynamic_pointer_cast<k::model::ns>(current);
                if (!ns_ptr) continue;
                // Try descending through namespaces (const lookup — must NOT
                // create missing child namespaces, as that would mutate
                // _children and invalidate iterators of any outer visit loop).
                std::shared_ptr<const k::model::ns> cur_ns = ns_ptr;
                bool found = true;
                for (size_t i = 0; i + 1 < parts.size() && found; ++i) {
                    auto child = cur_ns->get_child_namespace(parts[i]);
                    if (child) {
                        cur_ns = child;
                    } else {
                        found = false;
                    }
                }
                if (found && cur_ns) {
                    ann_agg = cur_ns->get_aggregate(parts.back());
                }
            }
        }

        if (!ann_agg) {
            // Try imported modules
            auto qname = k::name::from(ann_inst.raw_name);
            if (auto imp_agg = _unit.get_or_create_imported_aggregate(qname, _context)) {
                ann_agg = std::dynamic_pointer_cast<aggregate>(imp_agg);
            }
        }
        if (!ann_agg) {
            throw_error(static_cast<unsigned int>(k::diag::structure_diag::ERR_ANNOTATION_MISSING_TARGET), err_lexeme,
                "Annotation type '{}' not found on '{}'",
                std::vector<std::string>{ann_inst.raw_name, element_name});
        }
        if (!ann_agg->is_annotation()) {
            throw_error(static_cast<unsigned int>(k::diag::structure_diag::ERR_ANNOTATION_BAD_TYPE), err_lexeme,
                "'{}' is not an annotation type; only annotation types can be used as annotations",
                std::vector<std::string>{ann_inst.raw_name});
        }
        ann_inst.resolved_type = ann_agg;
    }

    // Step 2: Validate that the resolved type is an annotation_type
    //── Phase 2: validate @Target constraints on resolved annotations
    for (auto& ann_inst : holder.get_annotations_mutable()) {
        if (!ann_inst.resolved_type) continue;

        auto& ann_type_anns = ann_inst.resolved_type->get_annotations();
        for (auto& meta : ann_type_anns) {
            if (!meta.resolved_type) continue;
            std::string meta_fqn = meta.resolved_type->get_fq_name();
            if (meta_fqn != "k::annotations::Target"
                && meta_fqn != "::k::annotations::Target"
                && meta.raw_name != "Target") continue;

            if (!meta.ast_node) continue;
            auto* ast = meta.ast_node.get();

            // Collect allowed element type names from the AST
            std::vector<std::string> allowed_types;

            auto collect_from_brace = [&](const std::shared_ptr<k::parse::ast::brace_init_list>& brace) {
                if (!brace) return;
                for (auto& elem : brace->elements) {
                    if (auto ident = std::dynamic_pointer_cast<k::parse::ast::identifier_expr>(elem)) {
                        if (!ident->qident.names.empty()) {
                            allowed_types.push_back(std::string{ident->qident.names.back().content});
                        }
                    }
                }
            };

            // @Target({...}) — positional arg with brace-init list
            if (ast->has_parens && !ast->args.empty()) {
                if (auto brace = std::dynamic_pointer_cast<k::parse::ast::brace_init_list>(ast->args[0])) {
                    collect_from_brace(brace);
                }
            }
            // @Target{...} — brace-init
            else if (ast->brace_init) {
                collect_from_brace(ast->brace_init);
            }

            // Step 3: Check @Target constraints: the element_kind must be in the allowed list
            if (!allowed_types.empty()) {
                bool found = false;
                for (auto& allowed : allowed_types) {
                    if (allowed == element_kind) { found = true; break; }
                }
                if (!found) {
                    std::string allowed_str;
                    for (size_t i = 0; i < allowed_types.size(); ++i) {
                        if (i > 0) allowed_str += ", ";
                        allowed_str += allowed_types[i];
                    }
                    throw_error(static_cast<unsigned int>(k::diag::structure_diag::ERR_ANNOTATION_TARGET_MISMATCH), err_lexeme,
                        "Annotation @{} cannot be applied to {} '{}': "
                        "@Target restricts it to [{}]",
                        std::vector<std::string>{ann_inst.raw_name, element_kind, element_name, allowed_str});
                }
            }
        }
    }
}

//
// Aggregate / Structure / Klass
//

void symbol_resolver::visit_nested_aggregates(aggregate& st) {
    for (auto& child : st.get_children()) {
        auto nested = std::dynamic_pointer_cast<aggregate>(child);
        if (!nested || _visited_aggregates.count(nested.get())) continue;

        // A nested type may inherit from its enclosing type. Defer it until
        // that base's layout (including its vtable) is complete.
        std::unordered_set<const aggregate*> seen;
        std::function<bool(const aggregate&)> depends_on_resolving_base;
        depends_on_resolving_base = [&](const aggregate& current) {
            if (!seen.insert(&current).second) return false;
            for (const auto& bs : current.get_bases()) {
                if (!bs.base) continue;
                if (_resolving_aggregates.count(bs.base.get())
                    || depends_on_resolving_base(*bs.base)) {
                    return true;
                }
            }
            return false;
        };
        if (!depends_on_resolving_base(*nested)) {
            nested->accept(*this);
        }
    }
}

// symbol_resolver::visit_aggregate
// ---------------------------------
// Resolves symbols and builds the structural skeleton of an aggregate (struct or class).
//
// Steps:
//  1. Resolve the named-element identity (name lookup, scope registration).
//  2. Pre-declare the aggregate's LLVM struct type and register it in the context
//     so recursive/forward references can succeed.
//  3. Visit nested aggregates, deferring those inheriting from an active enclosing base.
//  4. For each declared base:
//     a. Resolve the base name to a concrete aggregate via scope lookup.
//     b. Validate constraints: final base, const/mutable mismatch, struct/class cross-inheritance.
//     c. Emit warnings for inner↔outer circular nesting patterns.
//  5. Detect inheritance cycles, then fully visit local bases regardless of source order.
//  6. Inject synthetic member-variable fields for each direct base (in reverse declaration order
//     so they appear in declaration order in the LLVM struct layout):
//     - Non-virtual base → __base_X__ (embedded sub-object).
//     - Virtual base → __vbptr_X__ (pointer-to-virtual-base slot).
//  7. Inject __vbase_X__ sub-object fields for transitively-collected virtual bases
//     (only in the "collector" class — the most-derived class that owns the single copy).
//  8. For non-static inner aggregates, inject a synthetic __parent__ field (Outer&).
//  9. Visit member variable children (resolves their types and init expressions).
// 10. Visit static/global variable children.
// 11. For const structs: implicitly promote non-static, non-ctor/dtor member functions to const.
// 12. Visit all function children (methods, constructors, destructor).
// 13. Generate a default (0-arg) constructor if none is present.
// 14. For structs only: generate a default copy constructor if bases or struct-typed members exist and no
//     copy constructor was declared.
// 15. Generate an implicit destructor if any base or member struct has a destructor.
// Note: class-specific processing (vtable layout, vptr injection) is done in visit_klass.
void symbol_resolver::visit_aggregate(aggregate& st) {
    // Skip template definitions — they are not instantiated yet.
    if (st.is_template()) {
        trace("[symbol_resolver::visit_aggregate] skipping template '{}'", {st.get_short_name()});
        return;
    }
    // Guard: prevent double-processing when an aggregate is first visited via
    // accept() from a derived-class base-resolution path (gen_struct.cpp line
    // ~384) and then again by the namespace iterator.  Double-processing injects
    // duplicate fields (__vptr__, __base_X__) and overwrites the struct_type
    // pointer, producing an unresolved struct_type that leads to LLVM struct-
    // layout cycles.
    if (_visited_aggregates.count(&st)) {
        trace("[symbol_resolver::visit_aggregate] '{}' already processed, skipping", {st.get_short_name()});
        return;
    }
    _resolving_aggregates.insert(&st);
    trace("[symbol_resolver::visit_aggregate] '{}'", {st.get_short_name()});
    lex::opt_any_lexeme st_lexeme;
    if (auto ast_ad = st.get_ast_aggregate_decl()) st_lexeme = lex::any_lexeme{ast_ad->name};

    visit_named_element(st);

    check_alias_declarations(st, st);

    // Pre declare type
    // TODO Mangle struct name to avoid collisions
    // Guard: do NOT create/overwrite the struct_type if the aggregate already has
    // one.  A template-instantiated base may have received its struct_type from the
    // derived-class base-resolution path (gen_struct.cpp line ~384) before this
    // visit runs.  Creating a fresh struct_type here would call add_struct() (a
    // no-op when the name is already registered) but still overwrite the
    // aggregate's pointer, leaving the aggregate referencing an *unregistered*
    // struct_type whose LLVM body is never materialized — producing LLVM struct
    // layout cycles at code-gen time.
    std::shared_ptr<struct_type> st_type = st.get_struct_type();
    if (!st_type) {
        st_type = std::make_shared<struct_type>(st.get_short_name()/*st.get_mangled_name()*/, st.shared_as<aggregate>());
        st.set_struct_type(st_type);
    }
    _context->add_struct(st_type);

    // Visit nested aggregate children first unless they depend on this layout.
    visit_nested_aggregates(st);
    for(auto& child : st.get_children()) {
        if(auto nested_un = std::dynamic_pointer_cast<union_type_def>(child)) {
            nested_un->accept(*this);
        }
    }

    // ── Inheritance: resolve base class names ──────────────────────────────────
    if (st.has_bases() && !st.bases_resolved()) {
        // Build a set of all ancestors to detect cycles
        std::function<bool(const aggregate*, std::unordered_set<const aggregate*>&)> detect_cycle;
        detect_cycle = [&](const aggregate* cur, std::unordered_set<const aggregate*>& visited) -> bool {
            for (auto& bs : cur->get_bases()) {
                if (!bs.base) continue;
                if (visited.count(bs.base.get())) return true;
                visited.insert(bs.base.get());
                if (detect_cycle(bs.base.get(), visited)) return true;
                visited.erase(bs.base.get());
            }
            return false;
        };

        for (auto& bs : st.get_bases_mutable()) {

            // Resolve the base name from the current structure scope upward.
            // bs.raw_name may be a simple name ("Base") or a qualified name
            // ("ns::Base") if the base comes from an imported module.
            // It may also contain template arguments ("Collection<int>").
            std::shared_ptr<aggregate> base_st;

            // Check if the raw_name contains template arguments
            auto lt_pos = bs.raw_name.find('<');
            if (lt_pos != std::string::npos) {
                // Template base class: "Name<Arg1,Arg2,...>"
                auto gt_pos = bs.raw_name.rfind('>');
                if (gt_pos != std::string::npos && gt_pos > lt_pos) {
                    std::string tpl_base_name = bs.raw_name.substr(0, lt_pos);
                    std::string args_str = bs.raw_name.substr(lt_pos + 1, gt_pos - lt_pos - 1);

                    // Look up the template aggregate by name.
                    // tpl_base_name may be a simple or qualified name (e.g. "k::io::X").
                    // Split on "::" to build a k::name for hierarchical lookup.
                    std::shared_ptr<aggregate> tpl_agg;
                    {
                        std::vector<std::string> name_parts;
                        {
                            std::size_t pos = 0;
                            while (true) {
                                auto sep = tpl_base_name.find("::", pos);
                                if (sep == std::string::npos) {
                                    name_parts.push_back(tpl_base_name.substr(pos));
                                    break;
                                }
                                name_parts.push_back(tpl_base_name.substr(pos, sep - pos));
                                pos = sep + 2;
                            }
                        }
                        const std::string& short_name = name_parts.back();

                        if (name_parts.size() == 1) {
                            // Simple name: standard scope-chain lookup
                            tpl_agg = scope_lookup::lookup_structure(st.shared_as<element>(), short_name);
                        }
                        if (!tpl_agg) {
                            if (auto root_ns_ptr = scope_lookup::root_namespace(st)) {
                                // Try full qualified path from root (e.g. k::io::X at k/io/X)
                                k::name qname{false, name_parts};
                                if (auto res = aggregate_type_resolver::resolve_struct_from(*root_ns_ptr, qname)) {
                                    tpl_agg = res;
                                }
                                // KDI-materialized templates are homed at the consumer root
                                // namespace (due to the module-flatten trick in kdi_importer).
                                // For a qualified name like "k::io::OneToOneTransformInputStream",
                                // the template may live at root under its short name.
                                if (!tpl_agg && name_parts.size() > 1) {
                                    k::name short_qname{false, {short_name}};
                                    if (auto res = aggregate_type_resolver::resolve_struct_from(*root_ns_ptr, short_qname)) {
                                        tpl_agg = res;
                                    }
                                }
                                // Fallback: search imported modules with short name prefix
                                if (!tpl_agg) {
                                    for (const auto& imp : _unit.get_imports()) {
                                        if (imp.module_name.empty()) continue;
                                        auto imported_name = imp.module_name.with_back(short_name);
                                        if (auto res = aggregate_type_resolver::resolve_struct_from(*root_ns_ptr, imported_name)) {
                                            tpl_agg = res;
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }

                    if (tpl_agg && tpl_agg->is_template()) {
                        auto* ti = tpl_agg->get_tpl_info();
                        if (ti) {
                            // Parse comma-separated arg names and resolve to types
                            std::vector<std::string> arg_names;
                            {
                                size_t pos = 0;
                                while (pos < args_str.size()) {
                                    auto comma = args_str.find(',', pos);
                                    std::string arg = (comma == std::string::npos)
                                        ? args_str.substr(pos)
                                        : args_str.substr(pos, comma - pos);
                                    while (!arg.empty() && arg.front() == ' ') arg.erase(arg.begin());
                                    while (!arg.empty() && arg.back() == ' ') arg.pop_back();
                                    arg_names.push_back(arg);
                                    pos = (comma == std::string::npos) ? args_str.size() : comma + 1;
                                }
                            }

                            // Resolve each arg name to a model type
                            std::vector<template_argument> model_args;
                            bool all_resolved = true;
                            for (const auto& arg_name : arg_names) {
                                std::shared_ptr<type> arg_type;
                                // Try primitive type names
                                static const std::unordered_map<std::string, primitive_type::PRIMITIVE_TYPE> prim_map = {
                                    {"bool", primitive_type::BOOL}, {"byte", primitive_type::BYTE},
                                    {"char", primitive_type::CHAR}, {"short", primitive_type::SHORT},
                                    {"int", primitive_type::INT}, {"long", primitive_type::LONG},
                                    {"float", primitive_type::FLOAT}, {"double", primitive_type::DOUBLE},
                                    {"unsigned byte", primitive_type::UNSIGNED_BYTE},
                                    {"unsigned short", primitive_type::UNSIGNED_SHORT},
                                    {"unsigned int", primitive_type::UNSIGNED_INT},
                                    {"unsigned long", primitive_type::UNSIGNED_LONG},
                                };
                                auto prim_it = prim_map.find(arg_name);
                                if (prim_it != prim_map.end()) {
                                    arg_type = _context->from_type(prim_it->second);
                                } else {
                                    // Try as user-defined aggregate type
                                    auto agg = scope_lookup::lookup_structure(st.shared_as<element>(), arg_name);
                                    if (agg && agg->get_struct_type()) {
                                        arg_type = agg->get_struct_type();
                                    }
                                }
                                // A nominal aggregate is usable as a template
                                // argument before its LLVM layout is materialized.
                                if (arg_type && (type::is_resolved(arg_type)
                                    || std::dynamic_pointer_cast<struct_type>(arg_type))) {
                                    model_args.push_back(template_argument::make_type(arg_type));
                                } else {
                                    all_resolved = false;
                                    break;
                                }
                            }

                            if (all_resolved && !model_args.empty()) {
                                // Find the parent namespace of the template
                                auto parent_ns = std::dynamic_pointer_cast<ns>(tpl_agg->parent<element>());
                                if (!parent_ns) parent_ns = _unit.get_root_namespace();
                                base_st = template_instantiator::instantiate_aggregate(
                                    *tpl_agg, model_args, parent_ns, _unit, _context, _log);
                                // Ensure the instantiated base has a struct_type so that
                                // context::resolve_types() can materialize its LLVM type.
                                if (base_st && !base_st->get_struct_type()) {
                                    auto st_type = std::make_shared<struct_type>(
                                        base_st->get_short_name(), base_st->shared_as<aggregate>());
                                    _context->add_struct(st_type);
                                    base_st->set_struct_type(st_type);
                                }
                            }
                        }
                    }
                }
            }

            if (!base_st) {
            // Qualified/imported-aware lookup: handles simple names, namespace-qualified
            // names (e.g. "k::Object") resolved among locally-declared aggregates, and
            // names only reachable through a KDI-imported module.
            base_st = scope_lookup::lookup_structure_or_import(_unit, _context, st.shared_as<element>(), bs.raw_name);
            } // end if (!base_st) — template base was already resolved above

            if (!base_st) {
                throw_error(static_cast<unsigned int>(k::diag::structure_diag::ERR_BASE_NOT_FOUND), st_lexeme,
                    "Base class '{}' of struct '{}' is not found",
                    {bs.raw_name, st.get_short_name()});
            }

            // A final struct cannot be used as a base class
            if (base_st->is_final()) {
                throw_error(static_cast<unsigned int>(k::diag::structure_diag::ERR_BASE_IS_FINAL), st_lexeme,
                    "Cannot inherit from '{}' in struct '{}': '{}' is declared final and cannot be used as a base class",
                    {bs.raw_name, st.get_short_name(), bs.raw_name});
            }

            // A const struct cannot inherit from a mutable (non-const) struct.
            // Annotation types are excluded: they implicitly inherit from
            // ::k::Annotation (a const class) which may not carry the const flag
            // when imported; the implicit constness is guaranteed by the compiler.
            if (st.is_const_struct() && !base_st->is_const_struct() && !st.is_annotation()) {
                throw_error(static_cast<unsigned int>(k::diag::structure_diag::ERR_CONST_STRUCT_MUTABLE_BASE), st_lexeme,
                    "const struct '{}' cannot inherit from mutable struct '{}': "
                    "a const struct may only inherit from other const structs",
                    {st.get_short_name(), bs.raw_name});
            }

            // Cross-type inheritance (struct/class mix) is forbidden.
            // Interfaces count as class-like for this check (a class may implement an interface,
            // and an interface may extend another interface).
            // Annotation types are excluded: they inherit from ::k::Annotation (a class).
            if (!st.is_annotation() && !base_st->is_annotation()) {
                bool st_is_class_like   = st.is_class()       || std::dynamic_pointer_cast<const model::interface>(st.shared_as<const element>()) != nullptr;
                bool base_is_class_like = base_st->is_class() || std::dynamic_pointer_cast<model::interface>(base_st) != nullptr;
                if (st_is_class_like != base_is_class_like) {
                    std::string kind_st   = st.is_class()       ? "class" : "struct";
                    std::string kind_base = base_st->is_class() ? "class" : "struct";
                    throw_error(static_cast<unsigned int>(k::diag::structure_diag::ERR_CROSS_STRUCT_CLASS), st_lexeme,
                        "{} '{}' cannot inherit from {} '{}': "
                        "cross-inheritance between class and struct is not allowed",
                        {kind_st, st.get_short_name(), kind_base, bs.raw_name});
                }
            }

            bs.base = base_st;

            // Warn if inner struct inherits from outer or outer inherits from inner
            if (st.is_nested() || base_st->is_nested()) {
                auto outer = st.get_enclosing_structure();
                if (outer && (base_st.get() == outer.get() || outer->is_derived_from(base_st))) {
                    warn(static_cast<unsigned int>(k::diag::structure_diag::WARN_NESTED_INHERITS_ENCLOSING), st_lexeme,
                        "inner struct '{}' inherits from enclosing struct '{}'",
                        {st.get_short_name(), base_st->get_short_name()});
                }
                if (base_st->is_nested()) {
                    auto base_outer = base_st->get_enclosing_structure();
                    if (base_outer && (base_outer.get() == &st || st.is_derived_from(base_outer))) {
                        warn(static_cast<unsigned int>(k::diag::structure_diag::WARN_NESTED_INHERITS_INNER), st_lexeme,
                            "struct '{}' inherits from inner struct '{}'",
                            {st.get_short_name(), base_st->get_short_name()});
                    }
                }
            }
        }

        // Detect cycles after resolution
        std::unordered_set<const aggregate*> visited;
        visited.insert(&st);
        if (detect_cycle(&st, visited)) {
            throw_error(static_cast<unsigned int>(k::diag::structure_diag::ERR_STRUCT_SELF_INHERIT), st_lexeme,
                "Circular inheritance detected in struct '{}'",
                {st.get_short_name()});
        }

        // Resolve every local base before inheriting fields, destructors or
        // virtual slots. Name lookup alone does not construct a forward base.
        for (const auto& bs : st.get_bases()) {
            if (bs.base && !_visited_aggregates.count(bs.base.get())
                && !std::dynamic_pointer_cast<imported_aggregate>(bs.base)) {
                bs.base->accept(*this);
            }
        }

        // Inject base sub-objects as synthetic member variables in DECLARATION ORDER.
        // Non-virtual bases: embed as __base_X__. Virtual bases: inject __vbptr_X__.
        std::vector<base_spec>& bases_mutable = st.get_bases_mutable();
        for (auto it = bases_mutable.rbegin(); it != bases_mutable.rend(); ++it) {
            auto& bs = *it;
            if (!bs.base || !bs.base->get_struct_type()) continue;
            if (bs.is_virtual) {
                std::string vbptr_name = "__vbptr_" + bs.base->get_short_name() + "__";
                if (!st._vars.count(vbptr_name)) {
                    auto vbptr_field = member_variable_definition::make_shared(st.shared_as<aggregate>(), vbptr_name);
                    st._vars.insert({vbptr_name, vbptr_field});
                    st._children.insert(st._children.begin(), vbptr_field);
                }
            } else {
                std::string subobj_name = "__base_" + bs.sanitised_name() + "__";
                if (st._vars.count(subobj_name)) continue;
                auto subobj_field = member_variable_definition::make_shared(st.shared_as<aggregate>(), subobj_name);
                subobj_field->set_type(bs.base->get_struct_type());
                st._vars.insert({subobj_name, subobj_field});
                st._children.insert(st._children.begin(), subobj_field);
            }
        }

        // ── Virtual base sub-objects in the "collector" class only ─────────────
        {
            auto vbases = st.get_all_virtual_base_structs();
            for (auto& vbase : vbases) {
                std::string vbase_name = "__vbase_" + vbase->get_short_name() + "__";
                std::string vbptr_name = "__vbptr_" + vbase->get_short_name() + "__";

                std::function<bool(const aggregate&)> has_vbptr_in_vars;
                has_vbptr_in_vars = [&](const aggregate& base_st) -> bool {
                    if (base_st._vars.count(vbptr_name)) return true;
                    for (auto& b : base_st.get_bases()) {
                        if (!b.base) continue;
                        if (b.is_virtual) continue;
                        if (has_vbptr_in_vars(*b.base)) return true;
                    }
                    return false;
                };

                // Collect transitive virtual interfaces even when their vbptrs
                // are reachable only through another virtual base.
                bool has_collector_base = vbase->is_interface();
                for (auto& bs : st.get_bases()) {
                    if (has_collector_base) break;
                    if (!bs.base) continue;
                    if (bs.is_virtual) {
                        if (bs.base->_vars.count(vbptr_name)) {
                            has_collector_base = true;
                            break;
                        }
                    } else {
                        if (has_vbptr_in_vars(*bs.base)) {
                            has_collector_base = true;
                            break;
                        }
                    }
                }

                if (has_collector_base && !st._vars.count(vbase_name)) {
                    auto vbase_field = member_variable_definition::make_shared(st.shared_as<aggregate>(), vbase_name);
                    vbase_field->set_type(vbase->get_struct_type());
                    st._vars.insert({vbase_name, vbase_field});
                    st._children.push_back(vbase_field);
                }
            }
        }
        st.set_bases_resolved(true);
    }

    // For non-static inner aggregates, inject a synthetic __parent__ member variable.
    if (st.is_inner() && !st._vars.count("__parent__")) {
        auto outer_st = st.get_enclosing_structure();
        auto outer_ref_type = outer_st->get_struct_type()->get_reference();
        auto parent_field = member_variable_definition::make_shared(st.shared_as<aggregate>(), "__parent__");
        parent_field->set_type(outer_ref_type);
        st._vars.insert({"__parent__", parent_field});
        st._children.insert(st._children.begin(), parent_field);
        st._parent_field = parent_field;
    }

    // Visit enumeration children (must come before member variables, since
    // member variable types may reference inner enum types, e.g. policy : Policy).
    for(auto& child : st.get_children()) {
        if(auto en = std::dynamic_pointer_cast<enumeration>(child)) {
            en->accept(*this);
        }
    }

    // Visit member variable children
    for(auto& child : st.get_children()) {
        if(auto var = std::dynamic_pointer_cast<member_variable_definition>(child)) {
            var->accept(*this);
        }
    }

    // Visit global/static variable children
    for(auto& child : st.get_children()) {
        if(auto var = std::dynamic_pointer_cast<global_variable_definition>(child)) {
            var->accept(*this);
        }
    }

    // For a const struct: implicitly promote non-static, non-ctor/dtor member functions to const.
    if (st.is_const_struct()) {
        for(auto& child : st.get_children()) {
            auto func = std::dynamic_pointer_cast<function>(child);
            if (!func) continue;
            if (func->is_static()) continue;
            if (std::dynamic_pointer_cast<constructor>(func)) continue;
            if (std::dynamic_pointer_cast<destructor>(func)) continue;
            if (!func->is_const_member()) {
                warn(static_cast<unsigned int>(k::diag::structure_diag::ERR_BASE_NOT_FOUND), st_lexeme,
                    "member function '{}' of const struct '{}' is not declared 'const'; "
                    "it is implicitly promoted to const",
                    {func->get_short_name(), st.get_short_name()});
                func->set_const_member(true);
            }
        }
    }

    // Visit function children (includes constructors and destructor)
    for(auto& child : st.get_children()) {
        if(auto func = std::dynamic_pointer_cast<function>(child)) {
            func->accept(*this);
        }
    }
    // Generate default constructor if absent
    if (st.constructors().empty()) {
        auto default_constructor = constructor::make_shared(st.shared_as<aggregate>());
        default_constructor->set_compiler_generated(true);
        st._constructors.push_back(default_constructor);
        st._children.push_back(default_constructor);
        default_constructor->accept(*this);
    }

    // ── Copy constructor: generate if absent and struct has bases or struct members ──
    // Only for structs (not classes, interfaces, or annotations).
    // Classes and annotations must not generate copy constructors automatically.
    if (st.is_struct()) {
        bool needs_copy_ctor = st.has_bases();
        if (!needs_copy_ctor) {
            for (auto& [name, var] : st.variables()) {
                if (auto mv = std::dynamic_pointer_cast<member_variable_definition>(var)) {
                    if (type::is_struct(mv->get_type())) { needs_copy_ctor = true; break; }
                }
            }
        }
        // A memberwise bytewise (memcpy) default copy constructor is only safe to
        // auto-generate when the struct is trivially copyable (no destructor and no
        // copy constructor anywhere in its layout, recursively). For a struct that
        // manages a resource (e.g. has its own destructor, such as Vector<T> owning a
        // heap buffer), silently synthesizing a memcpy-based copy constructor would
        // alias the resource between the original and the copy — a double-free
        // waiting to happen. Leave get_copy_constructor() null in that case so the
        // programmer must supply an explicit deep-copy constructor; callers that
        // attempt to copy such a type without one are rejected at the copy site
        // (see aggregate_type_has_resource_field() in gen_helpers.hpp).
        if (needs_copy_ctor && st_type && !aggregate_type_is_trivially_copyable(st_type)) {
            needs_copy_ctor = false;
        }
        if (needs_copy_ctor && !st.get_copy_constructor()) {
            warn(static_cast<unsigned int>(k::diag::structure_diag::WARN_IMPLICIT_COPY_CTOR_GENERATED), st_lexeme,
                "struct '{}' has bases or struct members but no copy constructor; "
                "a default copy constructor will be generated",
                {st.get_short_name()});
            auto copy_ctor = constructor::make_shared(st.shared_as<aggregate>());
            copy_ctor->set_compiler_generated(true);
            copy_ctor->set_copy_constructor(true);
            copy_ctor->append_parameter("other", st_type->get_reference());
            st._constructors.push_back(copy_ctor);
            st._children.push_back(copy_ctor);
            copy_ctor->accept(*this);
        }
    }

    // ── Implicit copy assignment operator for structs: generate if absent and not deleted ──
    // Only for structs (not classes/interfaces/annotations). If the user explicitly declares
    // __operator_aS_ (with body or -> delete), we don't generate one.
    if (st.is_struct()) {
        auto existing_asgn = st.get_functions("__operator_aS_");
        bool has_user_asgn = false;
        for (auto& f : existing_asgn) {
            has_user_asgn = true;
            break;
        }
        if (!has_user_asgn && st_type) {
            // Generate: operator=(other: StructType&) : StructType& { memberwise copy }
            auto copy_asgn = st.define_function("__operator_aS_", false);
            copy_asgn->set_operator(true);
            copy_asgn->set_compiler_generated(true);
            copy_asgn->set_return_type(st_type->get_reference());
            auto const_st_type = st_type->get_const();
            copy_asgn->append_parameter("other", const_st_type->get_reference());
            copy_asgn->accept(*this);
        }
    }

    // ── Implicit destructor: generate if absent and struct needs one ──────────
    // At this point member variable types may still be unresolved_type (e.g. "Inner"),
    // so we look up aggregates by name via scope lookup to check for destructors.
    if (!st.get_destructor()) {
        bool needs_dtor = false;
        for (auto& bs : st.get_bases()) {
            if (!bs.base) continue;
            if (bs.base->get_destructor()) { needs_dtor = true; break; }
        }
        if (!needs_dtor) {
            for (auto& vbase : st.get_all_virtual_base_structs()) {
                if (vbase->get_destructor()) { needs_dtor = true; break; }
            }
        }
        if (!needs_dtor) {
            for (auto& [vname, var] : st.variables()) {
                if (auto mv = std::dynamic_pointer_cast<member_variable_definition>(var)) {
                    if (vname.rfind("__", 0) == 0) continue;
                    // If the type is already resolved to struct_type, check directly
                    if (auto mv_st_type = std::dynamic_pointer_cast<struct_type>(mv->get_type())) {
                        if (mv_st_type->get_struct() && mv_st_type->get_struct()->get_destructor()) {
                            needs_dtor = true; break;
                        }
                    }
                    // If the type is still unresolved, try to look up the aggregate by name
                    else if (auto unres = std::dynamic_pointer_cast<unresolved_type>(mv->get_type())) {
                        auto member_agg = scope_lookup::lookup_structure(st.shared_as<element>(), unres->type_id().to_string());
                        if (member_agg && member_agg->get_destructor()) {
                            needs_dtor = true; break;
                        }
                    }
                }
            }
        }
        if (needs_dtor) {
            auto implicit_dtor = destructor::make_shared(st.shared_as<aggregate>());
            implicit_dtor->set_compiler_generated(true);
            st._destructor = implicit_dtor;
            st._children.push_back(implicit_dtor);
            implicit_dtor->accept(*this);
        }
    }
    // Note: class-specific processing (vtable) is done in visit_klass

    // ── Resolve annotation instances and validate @Target ─────────────────────
    {
        std::string element_kind;
        if (st.is_annotation()) {
            element_kind = "ANNOTATION";
        } else if (std::dynamic_pointer_cast<model::interface>(st.shared_as<element>())) {
            element_kind = "INTERFACE";
        } else if (st.is_class()) {
            element_kind = "CLASS";
        } else {
            element_kind = "STRUCT";
        }
        resolve_and_validate_annotations(st, st, st.get_short_name(), st_lexeme, element_kind);
    }

    // ── Propagate @Inherited annotations from base classes ────────────────
    // For each base class, check if any of its annotations are marked @Inherited.
    // If the derived class does not already have that annotation type, copy it.
    // Per spec: @Inherited has no effect on interfaces; only class inheritance
    // propagates annotations, so we skip interface bases.
    if (st.is_class() && st.has_bases()) {
        for (auto& bs : st.get_bases()) {
            if (!bs.base) continue;
            // Only propagate from class bases, not interfaces
            if (!bs.base->is_class()) continue;
            for (auto& base_ann : bs.base->get_annotations()) {
                if (!base_ann.resolved_type) continue;

                // Check if this annotation type has @Inherited
                bool is_inherited = false;
                for (auto& meta : base_ann.resolved_type->get_annotations()) {
                    if (!meta.resolved_type) continue;
                    std::string meta_fqn = meta.resolved_type->get_fq_name();
                    if (meta_fqn == "k::annotations::Inherited"
                        || meta_fqn == "::k::annotations::Inherited"
                        || meta.raw_name == "Inherited") {
                        is_inherited = true;
                        break;
                    }
                }
                if (!is_inherited) continue;

                // Check if the derived class already has this annotation type
                bool already_has = false;
                for (auto& ann_inst : st.get_annotations()) {
                    if (ann_inst.resolved_type == base_ann.resolved_type) {
                        already_has = true;
                        break;
                    }
                }
                if (already_has) continue;

                // Copy the annotation instance to the derived class
                annotation_instance inherited_ann;
                inherited_ann.raw_name = base_ann.raw_name;
                inherited_ann.ast_node = base_ann.ast_node;
                inherited_ann.resolved_type = base_ann.resolved_type;
                inherited_ann.resolved_field_constants = base_ann.resolved_field_constants;
                st.add_annotation(std::move(inherited_ann));
            }
        }
    }

    // Mark this aggregate as fully processed so that double-visitation is
    // detected and skipped (see guard at the top of this function).
    _visited_aggregates.insert(&st);
    if (!std::dynamic_pointer_cast<klass>(st.shared_as<element>())) {
        _resolving_aggregates.erase(&st);
        visit_nested_aggregates(st);
    }
}

// signature_resolver::visit_aggregate
// ------------------------------------
// Pre-resolve function signatures (parameter + return types) within an aggregate,
// without processing function bodies or member variable init expressions.
void signature_resolver::visit_aggregate(aggregate& st) {
    // Skip template definitions — they are not instantiated yet.
    if (st.is_template()) return;

    // Visit nested aggregate children first (depth-first)
    for (auto& child : st.get_children()) {
        if (auto nested_st = std::dynamic_pointer_cast<aggregate>(child)) {
            nested_st->accept(*this);
        }
    }

    // Visit only function/constructor/destructor children to resolve their
    // parameter and return types. Skip member variables and other children
    // to avoid side-effects (e.g. global constructor registration for static
    // member variables).
    for (auto& child : st.get_children()) {
        if (std::dynamic_pointer_cast<aggregate>(child)) continue;
        if (std::dynamic_pointer_cast<function>(child)) {
            child->accept(*this);
        }
    }
}


// type_reference_resolver::visit_aggregate
// ------------------------------------------
// Steps:
//  1. Visit nested aggregate children first (depth-first), so their
//     types are available before outer members reference them.
//  2. Visit all remaining children (functions, constructors, destructor, variables) —
//     excluding nested aggregates already handled in step 1.  This resolves parameter
//     types, return types, and expression types inside each member.
//  3. After all members are resolved, check for method overload collisions
//     (duplicate signatures) and constructor overload collisions within this aggregate.
// Note: class-specific LLVM vtable struct-type building is done in visit_klass.
void type_reference_resolver::visit_aggregate(aggregate& st) {
    // Skip template definitions — they are not instantiated yet.
    if (st.is_template()) {
        trace("[type_reference_resolver::visit_aggregate] skipping template '{}'", {st.get_short_name()});
        return;
    }
    trace("[type_reference_resolver::visit_aggregate] '{}'", {st.get_short_name()});
    lex::opt_any_lexeme st_lexeme;
    if (auto ast_ad = st.get_ast_aggregate_decl()) st_lexeme = lex::any_lexeme{ast_ad->name};
    // Note: const-struct method promotion is already done in symbol_resolver phase.

    materialize_aliases(st, st);

    // Visit nested aggregate and union children first
    for(auto& child : st.get_children()) {
        if(auto nested_st = std::dynamic_pointer_cast<aggregate>(child)) {
            nested_st->accept(*this);
        } else if(auto nested_un = std::dynamic_pointer_cast<union_type_def>(child)) {
            nested_un->accept(*this);
        }
    }

    // Instantiations created after Pass A missed implicit destructor synthesis.
    // Resolve their value fields and finish that skeleton before visiting bodies.
    for (auto& [name, var] : st.variables()) {
        auto member = std::dynamic_pointer_cast<member_variable_definition>(var);
        if (!member || !member->get_type() || type::is_resolved(member->get_type())) continue;
        auto resolved = resolve_type_chain(member->get_type(), member.get());
        if (resolved && (type::is_resolved(resolved)
                         || std::dynamic_pointer_cast<struct_type>(resolved))) {
            member->set_type(resolved);
        }
    }
    finalize_aggregate_destructor(st);

    // Visit all other children (functions, constructors, destructors), skip nested aggregates and unions.
    const auto members = st.get_children();
    for(const auto& child : members) {
        if(std::dynamic_pointer_cast<aggregate>(child)) continue;
        if(std::dynamic_pointer_cast<union_type_def>(child)) continue;
        child->accept(*this);
    }

    // After all members are resolved, check for overload collisions.
    check_overload_collisions(st);
    check_constructor_overload_collisions(st);

    // Note: class-specific LLVM vtable type building is done in visit_klass
}


// declaration_generator::visit_aggregate
// ----------------------------------------
// Emits forward LLVM IR declarations for all members of an aggregate.
// The struct type itself is already pre-created in the symbol-resolution phase;
// this pass fills in its body and declares all member functions.
//
// Steps:
//  1. Push the aggregate onto the struct stack so nested visitors know
//     the current owning aggregate context.
//  2. Visit every child (member variables, methods, constructors, destructor,
//     nested aggregates) — each child emits its own LLVM function declaration
//     or type definition.
//  3. Pop the struct stack.
// Note: class vtable global-variable emission is done in visit_klass.
void declaration_generator::visit_aggregate(aggregate& st) {
    // Skip template definitions — they are not instantiated yet.
    if (st.is_template()) {
        trace("[declaration_generator::visit_aggregate] skipping template '{}'", {st.get_short_name()});
        return;
    }
    trace("[declaration_generator::visit_aggregate] '{}'", {st.get_short_name()});
    _struct_stack.push(st.shared_as<aggregate>());

    // Visit all children (variables, methods, constructors, destructor, nested aggregates).
    for(auto& child : st.get_children()) {
        child->accept(*this);
    }
    // Note: class vtable emission is done in visit_klass

    _struct_stack.pop();
}

// declaration_generator::visit_enumeration
// -----------------------------------------
// For integer enums, there are no LLVM-level declarations to emit.
// For object-backed typed enums, emits a backing array and, when construction
// is required, a lazy accessor for runtime `E → const T&` conversions.
void declaration_generator::visit_enumeration(enumeration& en) {
    if (!en.is_resolved() || !en.is_object_backed()) return;
    if (en.get_table_global() || en.is_imported()) return;
    if (auto base = en.get_base(); base && !base->is_imported() && !base->get_table_global())
        visit_enumeration(*base);

    auto obj_type = en.get_object_type();
    if (!obj_type || !obj_type->is_resolved()) {
        throw_error(static_cast<unsigned int>(k::diag::structure_diag::ERR_ENUM_UNSUPPORTED_INITIALIZER),
            en.get_interest_lexeme(), "Cannot build enum '{}' backing table: unresolved backing type",
            {en.get_short_name()});
    }

    auto* llvm_st = llvm::dyn_cast_or_null<llvm::StructType>(
        _context->get_llvm_type(obj_type));
    if (!llvm_st) {
        throw_error(static_cast<unsigned int>(k::diag::structure_diag::ERR_ENUM_UNSUPPORTED_INITIALIZER),
            en.get_interest_lexeme(), "Cannot build enum '{}' backing table: missing aggregate layout",
            {en.get_short_name()});
    }
    if (llvm_st->isOpaque()) {
        throw_error(static_cast<unsigned int>(k::diag::structure_diag::ERR_ENUM_UNSUPPORTED_INITIALIZER),
            en.get_interest_lexeme(), "Cannot build enum '{}' backing table: incomplete aggregate layout",
            {en.get_short_name()});
    }

    auto reject_initializer = [&](const enum_entry_def& entry, const std::string& reason) {
        auto location = entry.ast_entry
            ? lex::opt_any_lexeme{lex::any_lexeme{entry.ast_entry->name}}
            : en.get_interest_lexeme();
        throw_error(static_cast<unsigned int>(k::diag::structure_diag::ERR_ENUM_UNSUPPORTED_INITIALIZER),
            location, "Cannot build backing table for enum entry '{}': {}", {entry.name, reason});
    };

    auto aggregate = obj_type->get_struct();
    if (enum_implicit_object_base(aggregate) &&
        (!obj_type->get_member("__vptr__") || !obj_type->get_member("__base_Object__"))) {
        throw_error(static_cast<unsigned int>(k::diag::structure_diag::ERR_ENUM_UNSUPPORTED_INITIALIZER),
            en.get_interest_lexeme(), "Cannot build enum '{}' backing table: incomplete class layout",
            {en.get_short_name()});
    }
    auto zero_default_supported = [&] { return enum_constant_safe_type(obj_type); };

    // Only the existing literal designated form can be represented as a constant.
    auto build_entry_constant = [&](const enum_entry_def& entry) -> llvm::Constant* {
        if (entry.has_paren_init &&
            (!entry.resolved_constructor || !entry.resolved_constructor->is_compiler_generated()
             || !entry.adapted_ctor_args.empty() || !zero_default_supported())) {
            reject_initializer(entry, "constructor execution requires runtime table initialization");
        }
        if (!entry.brace_init && !entry.has_paren_init) {
            if (!zero_default_supported())
                reject_initializer(entry, "implicit initialization requires runtime table initialization");
        }
        if (entry.brace_init && !zero_default_supported())
            reject_initializer(entry, "this designated value requires runtime table initialization");
        if (entry.brace_init && !entry.brace_init->is_designated)
            reject_initializer(entry, "only designated integer literal initializers are supported");

        // Class dispatch pointers must refer to the same tables as a constructed object.
        std::vector<llvm::Constant*> field_values;
        for (auto it = obj_type->fields_begin(); it != obj_type->fields_end(); ++it) {
            if (it->index >= llvm_st->getNumElements())
                reject_initializer(entry, "backing field has no LLVM layout");
            auto* field_llvm_type = llvm_st->getElementType(it->index);
            if (auto base = enum_implicit_object_base(aggregate);
                base && (it->name == "__vptr__" || it->name == "__base_Object__")) {
                auto cls = std::dynamic_pointer_cast<klass>(aggregate);
                auto vt = cls ? cls->get_vtable() : nullptr;
                if (!vt || !vt->llvm_global)
                    reject_initializer(entry, "class vtable is unavailable for constant initialization");
                if (it->name == "__vptr__") {
                    if (!field_llvm_type->isPointerTy())
                        reject_initializer(entry, "invalid class vtable field");
                    field_values.push_back(vt->llvm_global);
                } else {
                    auto* base_type = llvm::dyn_cast<llvm::StructType>(field_llvm_type);
                    if (!base_type || base_type->isOpaque() || base_type->getNumElements() != 1
                        || !base_type->getElementType(0)->isPointerTy())
                        reject_initializer(entry, "unsupported Object base layout");
                    llvm::GlobalVariable* base_vtable = nullptr;
                    for (const auto& [base_agg, secondary] : cls->get_secondary_vtables()) {
                        if (base_agg == base->base && secondary) {
                            base_vtable = secondary->llvm_global;
                            break;
                        }
                    }
                    if (!base_vtable)
                        reject_initializer(entry, "Object base vtable is unavailable for constant initialization");
                    field_values.push_back(llvm::ConstantStruct::get(base_type, {base_vtable}));
                }
                continue;
            }
            auto ft = it->field_type.lock();
            if (ft && ft->get_llvm_type()) {
                field_values.push_back(llvm::Constant::getNullValue(ft->get_llvm_type()));
            } else {
                reject_initializer(entry, "backing field has no LLVM type");
            }
        }

        if (entry.brace_init && entry.brace_init->is_designated) {
            std::unordered_set<size_t> initialized_fields;
            for (auto& elem : entry.brace_init->elements) {
                if (!elem) reject_initializer(entry, "invalid designated initializer");
                auto desig = std::dynamic_pointer_cast<k::parse::ast::designated_init_element>(elem);
                if (!desig || desig->is_call_form)
                    reject_initializer(entry, "only designated integer literal initializers are supported");

                std::string field_name{desig->member_name.content};
                auto field_opt = obj_type->get_member(field_name);
                if (!field_opt) reject_initializer(entry, "unknown designated field");
                size_t field_idx = field_opt->index;
                if (field_idx >= field_values.size()) reject_initializer(entry, "invalid designated field");
                if (!initialized_fields.insert(field_idx).second)
                    reject_initializer(entry, "duplicate designated field");

                // Evaluate the value expression — only integer literals are supported for constants
                auto lit = std::dynamic_pointer_cast<k::parse::ast::literal_expr>(desig->value);
                if (!lit) reject_initializer(entry, "designated field value is not an integer literal");
                auto& any_lit = lit->literal;
                if (any_lit.index() != lex::any_literal_type_index::INTEGER)
                    reject_initializer(entry, "designated field value is not an integer literal");
                auto& int_lit = any_lit.get<lex::integer>();
                auto ft = field_opt->field_type.lock();
                if (!ft || !ft->get_llvm_type() || !ft->get_llvm_type()->isIntegerTy())
                    reject_initializer(entry, "designated field is not an integer");
                field_values[field_idx] = llvm::ConstantInt::get(
                    ft->get_llvm_type(), int_lit.to_unsigned_int());
            }
        }

        return llvm::ConstantStruct::get(llvm_st, field_values);
    };

    // Helper: implicit progression from previous value (copy + heuristic ++ on last user integer field).
    auto build_implicit_from_previous = [&](const enum_entry_def& entry, llvm::Constant* previous) -> llvm::Constant* {
        std::vector<llvm::Constant*> fields;
        fields.reserve(llvm_st->getNumElements());
        for (unsigned i = 0; i < llvm_st->getNumElements(); ++i) {
            auto* field = previous ? previous->getAggregateElement(i) : nullptr;
            if (!field) reject_initializer(entry, "previous entry is not a constant aggregate");
            fields.push_back(field);
        }

        int target_index = -1;
        for (auto it = obj_type->fields_begin(); it != obj_type->fields_end(); ++it) {
            if (it->name.rfind("__", 0) == 0) continue;
            if (it->index >= fields.size()) continue;
            if (!fields[it->index]->getType()->isIntegerTy()) continue;
            target_index = static_cast<int>(it->index);
        }

        if (target_index < 0) reject_initializer(entry, "implicit increment requires an integer field");
        auto* int_ty = llvm::cast<llvm::IntegerType>(fields[static_cast<size_t>(target_index)]->getType());
        auto* one = llvm::ConstantInt::get(int_ty, 1, /*isSigned=*/false);
        fields[static_cast<size_t>(target_index)] =
            llvm::ConstantExpr::getAdd(fields[static_cast<size_t>(target_index)], one);
        return llvm::ConstantStruct::get(llvm_st, fields);
    };

    const size_t inherited = en.get_base() ? en.get_base()->entries().size() : 0;
    if (enum_requires_runtime_table(en)) {
        size_t table_size = 0;
        std::vector<std::pair<size_t, const enum_entry_def*>> ordered_entries;
        for (size_t i = 0; i < en.entries().size(); ++i) {
            const auto& entry = en.entries()[i];
            if (!entry.is_alias) ordered_entries.emplace_back(i, &entry);
        }
        std::sort(ordered_entries.begin(), ordered_entries.end(), [](const auto& a, const auto& b) {
            return a.second->value < b.second->value;
        });
        std::vector<llvm::Constant*> elements;
        for (const auto& [i, entry_ptr] : ordered_entries) {
            const auto& entry = *entry_ptr;
            if (entry.value != static_cast<int64_t>(table_size))
                reject_initializer(entry, "backing slots must be contiguous");
            auto* initial = llvm::Constant::getNullValue(llvm_st);
            if (i >= inherited) {
                if (entry.has_paren_init) {
                    if (!entry.resolved_constructor)
                        reject_initializer(entry, "runtime tables require resolved constructors");
                    for (const auto& arg : entry.adapted_ctor_args) {
                        if (!enum_runtime_literal_argument(arg))
                            reject_initializer(entry, "runtime table constructor arguments must be literals");
                    }
                } else if (entry.is_implicit_increment) {
                    if (!zero_default_supported())
                        reject_initializer(entry, "implicit increment requires a trivially copyable backing type");
                    // Validate the increment field using the same rules as constant tables.
                    build_implicit_from_previous(entry, initial);
                } else {
                    initial = build_entry_constant(entry);
                }
            }
            elements.push_back(initial);
            ++table_size;
        }
        auto* arr_ty = llvm::ArrayType::get(llvm_st, table_size);
        auto name = "__klang_enum_table_" + en.get_mangled_name() + "__";
        auto* table = new llvm::GlobalVariable(_context->module(), arr_ty, false,
            llvm::GlobalValue::InternalLinkage,
            llvm::ConstantArray::get(arr_ty, elements), name);
        en.set_table_global(table);
        auto* accessor_type = llvm::FunctionType::get(llvm::PointerType::get(**_context, 0), false);
        auto linkage = en.get_visibility() == k::model::PUBLIC
            ? llvm::GlobalValue::ExternalLinkage : llvm::GlobalValue::InternalLinkage;
        auto* accessor = llvm::Function::Create(accessor_type, linkage,
            name + ".get", _context->module());
        en.set_table_accessor(accessor);
        en.set_table_symbol(accessor->getName().str());
        en.set_runtime_table(true);
        return;
    }

    // Build the array initializer by unique backing index (aliases share slots).
    size_t table_size = 0;
    for (auto& entry : en.entries()) {
        if (entry.value >= 0) {
            table_size = std::max(table_size, static_cast<size_t>(entry.value + 1));
        }
    }

    auto* arr_ty = llvm::ArrayType::get(llvm_st, table_size);
    std::vector<llvm::Constant*> elements(table_size, nullptr);

    // Pass 1: explicit non-alias entries (designated / ctor-args).
    for (auto& entry : en.entries()) {
        if (entry.is_alias || entry.is_implicit_increment || entry.value < 0) continue;
        const size_t idx = static_cast<size_t>(entry.value);
        if (idx >= elements.size() || elements[idx] != nullptr) continue;
        elements[idx] = build_entry_constant(entry);
    }

    // Pass 2: implicit non-alias entries from previous slot.
    for (auto& entry : en.entries()) {
        if (entry.is_alias || !entry.is_implicit_increment || entry.value < 0) continue;
        const size_t idx = static_cast<size_t>(entry.value);
        if (idx >= elements.size() || elements[idx] != nullptr) continue;
        if (idx > 0 && elements[idx - 1] != nullptr) {
            elements[idx] = build_implicit_from_previous(entry, elements[idx - 1]);
        }
    }

    // Every unique slot must have a real initializer.
    for (size_t i = 0; i < elements.size(); ++i) {
        if (!elements[i]) {
            throw_error(static_cast<unsigned int>(k::diag::structure_diag::ERR_ENUM_UNSUPPORTED_INITIALIZER),
                en.get_interest_lexeme(), "Cannot build enum '{}' backing table: missing initializer for slot {}",
                {en.get_short_name(), std::to_string(i)});
        }
    }

    auto* arr_init = llvm::ConstantArray::get(arr_ty, elements);

    // Emit as a constant global: @__klang_enum_table_<MangledName>
    // Use ExternalLinkage for public enums (cross-module visibility), PrivateLinkage for private.
    std::string table_name = "__klang_enum_table_" + en.get_mangled_name() + "__";
    auto linkage = (en.get_visibility() == k::model::PUBLIC)
                   ? llvm::GlobalValue::ExternalLinkage
                   : llvm::GlobalValue::PrivateLinkage;
    auto* gv = new llvm::GlobalVariable(
        _context->module(),
        arr_ty,
        /*isConstant=*/true,
        linkage,
        arr_init,
        table_name);
    gv->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

    en.set_table_global(gv);
}

void implementation_generator::visit_enumeration(enumeration& en) {
    auto* accessor = en.get_table_accessor();
    auto* table = en.get_table_global();
    if (!accessor || !table || !accessor->empty()) return;

    auto& ctx = _context->llvm_context();
    auto& mod = get_module();
    auto* ptr_ty = llvm::PointerType::get(ctx, 0);
    auto* i32_ty = llvm::Type::getInt32Ty(ctx);
    auto* i64_ty = llvm::Type::getInt64Ty(ctx);
    auto* void_ty = llvm::Type::getVoidTy(ctx);
    auto* arr_ty = llvm::cast<llvm::ArrayType>(table->getValueType());
    auto* guard = new llvm::GlobalVariable(mod, i64_ty, false,
        llvm::GlobalValue::InternalLinkage, llvm::ConstantInt::get(i64_ty, 0),
        table->getName() + ".guard");
    auto* count = new llvm::GlobalVariable(mod, i64_ty, false,
        llvm::GlobalValue::InternalLinkage, llvm::ConstantInt::get(i64_ty, 0),
        table->getName() + ".count");
    auto* guard_fn_ty = llvm::FunctionType::get(void_ty, {ptr_ty}, false);
    auto acquire = mod.getOrInsertFunction("__cxa_guard_acquire",
        llvm::FunctionType::get(i32_ty, {ptr_ty}, false));
    auto release = mod.getOrInsertFunction("__cxa_guard_release", guard_fn_ty);
    auto abort = mod.getOrInsertFunction("__cxa_guard_abort", guard_fn_ty);
    auto personality = mod.getOrInsertFunction("__gxx_personality_v0",
        llvm::FunctionType::get(i32_ty, true));
    accessor->setPersonalityFn(llvm::cast<llvm::Constant>(personality.getCallee()));

    llvm::Function* destructor_fn = nullptr;
    auto object = en.get_object_type()->get_struct();
    if (object && object->get_destructor()) {
        auto it = _context->_functions.find(object->get_destructor()->shared_as<function>());
        if (it == _context->_functions.end() || !it->second)
            throw_error(static_cast<unsigned int>(k::diag::structure_diag::ERR_ENUM_UNSUPPORTED_INITIALIZER),
                en.get_interest_lexeme(), "Cannot find destructor for enum '{}' backing type",
                {en.get_short_name()});
        destructor_fn = it->second;
    }

    llvm::IRBuilderBase::InsertPointGuard insertion_guard(*_builder);
    auto previous_cleanup = std::exchange(_cleanup_lpad_stack, {});
    auto previous_catches = std::exchange(_landing_pad_stack, {});
    auto previous_depth = std::exchange(_lpad_depth_counter, 0);
    auto* previous_exc_ptr = std::exchange(_exc_ptr_slot, nullptr);
    auto* previous_exc_sel = std::exchange(_exc_sel_slot, nullptr);
    auto* previous_sret = std::exchange(_sret_destination, nullptr);
    auto previous_temporaries = std::exchange(_expression_temporaries, {});
    auto* previous_null_failure = std::exchange(_null_failure_bb, nullptr);
    auto* previous_union_failure = std::exchange(_union_failure_bb, nullptr);
    auto* previous_scope = std::exchange(_current_debug_scope, nullptr);
    auto previous_debug_loc = _builder->getCurrentDebugLocation();
    auto* previous_value = _value;
    auto restore = llvm::make_scope_exit([&]() {
        _cleanup_lpad_stack = std::move(previous_cleanup);
        _landing_pad_stack = std::move(previous_catches);
        _lpad_depth_counter = previous_depth;
        _exc_ptr_slot = previous_exc_ptr;
        _exc_sel_slot = previous_exc_sel;
        _sret_destination = previous_sret;
        _expression_temporaries = std::move(previous_temporaries);
        _null_failure_bb = previous_null_failure;
        _union_failure_bb = previous_union_failure;
        _current_debug_scope = previous_scope;
        _builder->SetCurrentDebugLocation(previous_debug_loc);
        _value = previous_value;
    });
    _builder->SetCurrentDebugLocation(llvm::DebugLoc{});

    auto* entry_bb = llvm::BasicBlock::Create(ctx, "entry", accessor);
    auto* init_bb = llvm::BasicBlock::Create(ctx, "initialize", accessor);
    auto* done_bb = llvm::BasicBlock::Create(ctx, "ready", accessor);
    auto* unwind_bb = llvm::BasicBlock::Create(ctx, "init_unwind", accessor);
    _builder->SetInsertPoint(entry_bb);
    auto* acquired = _builder->CreateCall(acquire, {guard});
    _builder->CreateCondBr(_builder->CreateICmpNE(acquired, _builder->getInt32(0)),
                           init_bb, done_bb);
    _builder->SetInsertPoint(init_bb);
    cleanup_lpad_context cleanup{};
    cleanup.lpad_bb = unwind_bb;
    cleanup.continuation = cleanup_lpad_context::RESUME;
    cleanup.depth = ++_lpad_depth_counter;
    _cleanup_lpad_stack.push(cleanup);

    const auto base = en.get_base();
    const size_t inherited = base ? base->entries().size() : 0;
    llvm::Value* base_table = nullptr;
    llvm::ArrayType* base_arr_ty = nullptr;
    llvm::Function* copy_fn = nullptr;
    if (inherited) {
        auto* base_accessor = ensure_enum_object_table_accessor_reference(base, _context);
        auto* base_global = base_accessor ? nullptr : ensure_enum_object_table_reference(base, _context);
        base_arr_ty = enum_object_table_type(base, _context);
        if (!base_arr_ty || (!base_accessor && !base_global))
            throw_error(static_cast<unsigned int>(k::diag::structure_diag::ERR_ENUM_UNSUPPORTED_INITIALIZER),
                en.get_interest_lexeme(), "Cannot find base table for enum '{}'", {en.get_short_name()});
        base_table = base_accessor
            ? create_call_or_invoke(base_accessor->getFunctionType(), base_accessor, {}, "base_enum_table")
            : static_cast<llvm::Value*>(base_global);
        // A constant table was emitted only for a constant-safe backing type;
        // copying its complete value must not require a synthetic mutable copy
        // constructor whose compiler-generated status is absent from old KDI.
        const bool constant_base = base->is_imported() && !base->has_runtime_table();
        auto copy = !constant_base && object ? object->get_copy_constructor() : nullptr;
        if (copy && !copy->is_deleted()) {
            auto param = copy->get_parameter(0);
            auto ref = param ? std::dynamic_pointer_cast<reference_type>(type::remove_const(param->get_type())) : nullptr;
            if (!ref || (!type::is_const(ref->get_subtype()) && !copy->is_compiler_generated()))
                throw_error(static_cast<unsigned int>(k::diag::codegen_diag::ERR_TYPE_NOT_COPYABLE),
                    en.get_interest_lexeme(), "Inherited enum backing type '{}' requires a copy constructor accepting const reference",
                    {en.get_object_type()->to_string()});
            auto it = _context->_functions.find(copy->shared_as<function>());
            if (it != _context->_functions.end()) copy_fn = it->second;
            if (!copy_fn)
                throw_error(static_cast<unsigned int>(k::diag::structure_diag::ERR_ENUM_UNSUPPORTED_INITIALIZER),
                    en.get_interest_lexeme(), "Cannot find copy constructor for enum '{}'", {en.get_short_name()});
        } else if (!constant_base && (copy || !aggregate_type_is_trivially_copyable(en.get_object_type()))) {
            throw_error(static_cast<unsigned int>(k::diag::codegen_diag::ERR_TYPE_NOT_COPYABLE),
                en.get_interest_lexeme(), "Inherited enum backing type '{}' is not copyable",
                {en.get_object_type()->to_string()});
        }
    }

    std::vector<std::pair<size_t, const enum_entry_def*>> ordered_entries;
    for (size_t i = 0; i < en.entries().size(); ++i) {
        const auto& entry = en.entries()[i];
        if (!entry.is_alias) ordered_entries.emplace_back(i, &entry);
    }
    std::sort(ordered_entries.begin(), ordered_entries.end(), [](const auto& a, const auto& b) {
        return a.second->value < b.second->value;
    });
    for (const auto& [i, entry_ptr] : ordered_entries) {
        const auto& entry = *entry_ptr;
        llvm::Value* indices[] = {_builder->getInt64(0), _builder->getInt64(entry.value)};
        auto* slot = _builder->CreateInBoundsGEP(arr_ty, table, indices, "enum_slot");
        if (i < inherited) {
            auto* source = _builder->CreateInBoundsGEP(base_arr_ty, base_table, indices, "base_enum_slot");
            if (copy_fn) {
                create_call_or_invoke(copy_fn->getFunctionType(), copy_fn, {slot, source});
            } else {
                auto* value = _builder->CreateLoad(arr_ty->getElementType(), source);
                _builder->CreateStore(value, slot);
            }
            _builder->CreateStore(_builder->getInt64(entry.value + 1), count);
            continue;
        }
        if (!entry.has_paren_init) {
            if (entry.is_implicit_increment) {
                llvm::Value* previous_indices[] = {
                    _builder->getInt64(0), _builder->getInt64(entry.value - 1)};
                auto* previous = _builder->CreateInBoundsGEP(arr_ty, table, previous_indices);
                _builder->CreateStore(_builder->CreateLoad(arr_ty->getElementType(), previous), slot);
                size_t field_index = 0;
                for (auto field = en.get_object_type()->fields_begin();
                     field != en.get_object_type()->fields_end(); ++field) {
                    if (field->name.rfind("__", 0) != 0
                        && field->index < llvm::cast<llvm::StructType>(arr_ty->getElementType())->getNumElements()
                        && llvm::cast<llvm::StructType>(arr_ty->getElementType())
                               ->getElementType(field->index)->isIntegerTy()) {
                        field_index = field->index;
                    }
                }
                auto* struct_ty = llvm::cast<llvm::StructType>(arr_ty->getElementType());
                auto* field_ptr = _builder->CreateStructGEP(struct_ty, slot, field_index);
                auto* field_ty = struct_ty->getElementType(field_index);
                auto* value = _builder->CreateLoad(field_ty, field_ptr);
                _builder->CreateStore(_builder->CreateAdd(value, llvm::ConstantInt::get(field_ty, 1)), field_ptr);
            }
            _builder->CreateStore(_builder->getInt64(entry.value + 1), count);
            continue;
        }
        auto it = _context->_functions.find(entry.resolved_constructor->shared_as<function>());
        if (it == _context->_functions.end() || !it->second)
            throw_error(static_cast<unsigned int>(k::diag::structure_diag::ERR_ENUM_UNSUPPORTED_INITIALIZER),
                en.get_interest_lexeme(), "Cannot find constructor for enum entry '{}'", {entry.name});
        std::vector<llvm::Value*> args{slot};
        for (const auto& arg : entry.adapted_ctor_args) {
            _value = nullptr;
            arg->accept(*this);
            if (!_value)
                throw_error(static_cast<unsigned int>(k::diag::structure_diag::ERR_ENUM_UNSUPPORTED_INITIALIZER),
                    en.get_interest_lexeme(), "Cannot generate constructor argument for enum entry '{}'",
                    {entry.name});
            args.push_back(_value);
        }
        create_call_or_invoke(it->second->getFunctionType(), it->second, args);
        _builder->CreateStore(_builder->getInt64(entry.value + 1), count);
    }
    _cleanup_lpad_stack.pop();
    _builder->CreateCall(release, {guard});
    _builder->CreateBr(done_bb);
    _builder->SetInsertPoint(done_bb);
    _builder->CreateRet(table);

    // Both shutdown and rollback consume the completed prefix, in reverse order.
    auto emit_cleanup = [&](llvm::Function* fn, bool unwinding, llvm::Value* exception) {
        auto* loop = llvm::BasicBlock::Create(ctx, "destroy_test", fn);
        auto* destroy = llvm::BasicBlock::Create(ctx, "destroy_entry", fn);
        auto* finish = llvm::BasicBlock::Create(ctx, "destroy_done", fn);
        llvm::Value* saved_chain = nullptr;
        llvm::Value* saved_type = nullptr;
        llvm::Value* chain_slot = nullptr;
        llvm::Value* type_slot = nullptr;
        if (unwinding && destructor_fn) {
            chain_slot = get_thrown_state_slot("__k_thrown_typeinfo_chain_addr");
            type_slot = get_thrown_state_slot("__k_thrown_typeinfo_addr");
            saved_chain = _builder->CreateLoad(ptr_ty, chain_slot);
            saved_type = _builder->CreateLoad(ptr_ty, type_slot);
        }
        _builder->CreateBr(loop);
        _builder->SetInsertPoint(loop);
        auto* live = _builder->CreateLoad(i64_ty, count);
        _builder->CreateCondBr(_builder->CreateICmpNE(live, _builder->getInt64(0)), destroy, finish);
        _builder->SetInsertPoint(destroy);
        auto* index = _builder->CreateSub(live, _builder->getInt64(1));
        _builder->CreateStore(index, count);
        if (destructor_fn) {
            llvm::Value* indices[] = {_builder->getInt64(0), index};
            auto* slot = _builder->CreateInBoundsGEP(arr_ty, table, indices, "enum_destroy_slot");
            if (unwinding) {
                auto* caught = llvm::BasicBlock::Create(ctx, "destroy_exception", fn);
                _builder->CreateInvoke(destructor_fn, loop, caught, {slot});
                _builder->SetInsertPoint(caught);
                auto* secondary = _builder->CreateLandingPad(
                    llvm::StructType::get(ctx, {ptr_ty, i32_ty}), 1);
                secondary->addClause(llvm::ConstantPointerNull::get(ptr_ty));
                auto begin = mod.getOrInsertFunction("__cxa_begin_catch",
                    llvm::FunctionType::get(ptr_ty, {ptr_ty}, false));
                auto end = mod.getOrInsertFunction("__cxa_end_catch",
                    llvm::FunctionType::get(void_ty, false));
                _builder->CreateCall(begin, {_builder->CreateExtractValue(secondary, 0)});
                _builder->CreateCall(end, {});
                _builder->CreateStore(saved_chain, chain_slot);
                _builder->CreateStore(saved_type, type_slot);
                _builder->CreateBr(loop);
            } else {
                _builder->CreateCall(destructor_fn, {slot});
                _builder->CreateBr(loop);
            }
        } else {
            _builder->CreateBr(loop);
        }
        _builder->SetInsertPoint(finish);
        if (unwinding) {
            if (destructor_fn) {
                _builder->CreateStore(saved_chain, chain_slot);
                _builder->CreateStore(saved_type, type_slot);
            }
            _builder->CreateCall(abort, {guard});
            _builder->CreateResume(exception);
        } else {
            _builder->CreateRetVoid();
        }
    };

    _builder->SetInsertPoint(unwind_bb);
    auto* exception = _builder->CreateLandingPad(
        llvm::StructType::get(ctx, {ptr_ty, i32_ty}), 0, "enum_exception");
    exception->setCleanup(true);
    emit_cleanup(accessor, true, exception);

    auto* dtor = llvm::Function::Create(llvm::FunctionType::get(void_ty, false),
        llvm::GlobalValue::InternalLinkage, table->getName() + ".destroy", mod);
    _builder->SetInsertPoint(llvm::BasicBlock::Create(ctx, "entry", dtor));
    emit_cleanup(dtor, false, nullptr);
    if (llvm::verifyFunction(*accessor) || llvm::verifyFunction(*dtor))
        throw_error(static_cast<unsigned int>(k::diag::structure_diag::ERR_ENUM_UNSUPPORTED_INITIALIZER),
            en.get_interest_lexeme(), "Cannot generate runtime backing table for enum '{}'",
            {en.get_short_name()});
    llvm::appendToGlobalDtors(mod, dtor, 65535);
}

// declaration_generator::visit_union
// -----------------------------------
// Finalize the LLVM struct body for a union type.
// The opaque struct type was created in aggregate_type_resolver; here we set its body
// now that the LLVM module (and DataLayout) is available.
void declaration_generator::visit_union(union_type_def& un) {
    // Skip template definitions — only instantiations produce LLVM types
    if (un.is_template()) return;

    auto st_type = un.get_struct_type();
    if (!st_type) return;

    auto* llvm_st = llvm::dyn_cast_or_null<llvm::StructType>(st_type->get_llvm_type());
    if (!llvm_st) return;

    // If body already set, skip
    if (!llvm_st->isOpaque()) {
        trace("[declaration_generator::visit_union] union '{}' already has body",
            {un.get_short_name()});
        return;
    }

    auto& llvm_ctx = _context->llvm_context();
    auto& data_layout = _context->module().getDataLayout();

    // Ensure the base union's body is set first (so DataLayout sees its types correctly)
    if (un.has_base_union()) {
        if (auto base = un.get_base_union()) {
            visit_union(*base); // no-op if body already set
        }
    }

    // Compute max size of ALL alternatives in the full inheritance chain
    uint64_t max_size = 0;
    for (const auto* alt_ptr : un.all_alternatives_ptrs()) {
        if (alt_ptr && alt_ptr->resolved_type && type::is_resolved(alt_ptr->resolved_type)) {
            auto* llvm_type = alt_ptr->resolved_type->get_llvm_type();
            if (llvm_type && llvm_type->isSized()) {
                uint64_t alt_size = data_layout.getTypeAllocSize(llvm_type);
                if (alt_size > max_size) max_size = alt_size;
            }
        }
    }

    // Minimum storage of 1 byte
    if (max_size == 0) max_size = 1;

    // Set body: { i64 discriminant, [max_size x i8] storage }
    auto* disc_type = llvm::Type::getInt64Ty(llvm_ctx);
    auto* storage_type = llvm::ArrayType::get(llvm::Type::getInt8Ty(llvm_ctx), max_size);
    llvm_st->setBody({disc_type, storage_type}, /*isPacked=*/false);

    trace("[declaration_generator::visit_union] finalized union '{}': storage={} bytes",
        {un.get_short_name(), std::to_string(max_size)});
}

// symbol_resolver::visit_enumeration
// ------------------------------------
// Resolves an enumeration: base lookup, entry value resolution, underlying type
// selection, and enum_type creation. Supports enum derivation (single inheritance,
// multi-level) and deferred resolution (no pre-declaration ordering requirement).
void symbol_resolver::visit_enumeration(enumeration& en) {
    visit_named_element(en);
    resolve_enumeration(en);
    if (!en.is_object_backed() || en._initializer_symbols_resolved) return;
    en._initializer_symbols_resolved = true;
    for (const auto& entry : en.raw_entries()) {
        for (const auto& arg : entry.ctor_args) {
            if (arg) arg->accept(*this);
        }
    }
}

void type_reference_resolver::visit_enumeration(enumeration& en) {
    if (!en.is_resolved() || !en.is_object_backed() || en._initializers_resolved || en.is_imported()) return;
    if (auto base = en.get_base()) {
        visit_enumeration(*base);
        for (size_t i = 0; i < base->entries().size(); ++i) {
            en._entries[i].ctor_args = base->entries()[i].ctor_args;
            en._entries[i].resolved_constructor = base->entries()[i].resolved_constructor;
            en._entries[i].adapted_ctor_args = base->entries()[i].adapted_ctor_args;
        }
    }

    auto object = en.get_object_type();
    auto aggregate = object ? object->get_struct() : nullptr;
    const size_t inherited = en.get_base() ? en.get_base()->entries().size() : 0;
    for (size_t i = inherited; i < en._entries.size(); ++i) {
        auto& entry = en._entries[i];
        auto location = entry.ast_entry
            ? lex::opt_any_lexeme{lex::any_lexeme{entry.ast_entry->name}}
            : en.get_interest_lexeme();
        if (entry.is_alias) continue;
        if (!entry.has_paren_init) {
            if (!entry.brace_init && !enum_constant_safe_type(object)) {
                throw_error(static_cast<unsigned int>(k::diag::structure_diag::ERR_ENUM_UNSUPPORTED_INITIALIZER),
                    location, "Implicit entry '{}' requires a trivially copyable backing type",
                    {entry.name});
            }
            continue;
        }

        for (auto& arg : entry.ctor_args) {
            _replacement_expr = nullptr;
            arg->accept(*this);
            if (_replacement_expr) {
                arg = _replacement_expr;
                _replacement_expr = nullptr;
            }
        }
        en.attach_initializer_arguments(entry.ctor_args);
        if (!aggregate || aggregate->is_abstract()) {
            throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_MEMBER_FUNC_NO_MATCH),
                location, "Cannot construct backing type for enum entry '{}'", {entry.name});
        }
        if (aggregate->is_inner()) {
            throw_error(static_cast<unsigned int>(k::diag::structure_diag::ERR_ENUM_UNSUPPORTED_INITIALIZER),
                location, "Enum entry '{}' cannot construct an inner aggregate without an enclosing instance",
                {entry.name});
        }
        // Defaults must be typed even when the backing declaration follows this enum.
        for (const auto& ctor : aggregate->constructors()) {
            if (!ctor || ctor->is_template()) continue;
            for (const auto& param : ctor->parameters()) {
                if (param->has_default_expr()) param->accept(*this);
            }
        }
        auto [ctor, adapted] = get_best_matching_constructor(
            aggregate->constructors(), entry.ctor_args, true, location);
        if (!ctor) {
            throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_INVOKE_MEMBER_NO_MATCH),
                location, "No matching constructor for enum entry '{}' of type '{}'",
                {entry.name, object->to_string()});
        }
        check_constructor_visibility(*ctor, en);
        entry.resolved_constructor = std::move(ctor);
        entry.adapted_ctor_args = std::move(adapted);
        en.attach_adapted_initializer_arguments(entry);
    }
    const auto local_begin = en._entries.begin() + inherited;
    if (std::any_of(local_begin, en._entries.end(), [&](const enum_entry_def& entry) {
            return enum_entry_requires_runtime_table(entry, object);
        })) {
        for (auto it = local_begin; it != en._entries.end(); ++it) {
            if (it->is_alias || !it->brace_init) continue;
            auto location = it->ast_entry
                ? lex::opt_any_lexeme{lex::any_lexeme{it->ast_entry->name}}
                : en.get_interest_lexeme();
            throw_error(static_cast<unsigned int>(k::diag::structure_diag::ERR_ENUM_UNSUPPORTED_INITIALIZER),
                location, "Enum entry '{}' cannot mix designated initialization with runtime constructors",
                {it->name});
        }
    }
    const auto base = en.get_base();
    const bool constant_import = base && base->is_imported() && !base->has_runtime_table();
    if (inherited && !constant_import && enum_requires_runtime_table(en) && aggregate) {
        if (auto copy = aggregate->get_copy_constructor(); copy && !copy->is_deleted())
            check_constructor_visibility(*copy, en);
    }
    en._initializers_resolved = true;
}

/**
 * Resolve an enumeration: base enum, underlying type, and entry values.
 *
 * Steps:
 *   1. Resolve the base enum (if derived) recursively.
 *   2. Determine the underlying type (explicit or inherited from base).
 *   3. Build the enum_type from the enumeration model.
 *   4. Resolve each entry's value expression, checking for duplicates.
 */
void symbol_resolver::resolve_enumeration(enumeration& en) {
    if (en.is_resolved()) return;

    lex::opt_any_lexeme en_lexeme;
    if (auto ast_ed = en.get_ast_enum_decl()) en_lexeme = lex::any_lexeme{ast_ed->name};

    // Cycle detection
    if (en._resolving) {
        throw_error(static_cast<unsigned int>(k::diag::structure_diag::ERR_ENUM_ENTRY_AMBIGUOUS), en_lexeme,
            "Circular enum derivation detected involving enum '{}'",
            {en.get_short_name()});
    }
    en._resolving = true;

    // Step 1: Resolve the base enum (if derived) recursively
    // ── 1. Resolve base enum if present ──
    if (en.get_base_name().has_value()) {
        const std::string& base_name_str = *en.get_base_name();
        std::shared_ptr<enumeration> base_en;
        std::shared_ptr<struct_type> aliased_object_type;

        const auto base_name = k::name::from(base_name_str);
        if (scope_lookup::lookup_alias(en.shared_as<element>(), base_name)) {
            aggregate_type_resolver type_resolver(_log, _context, _unit);
            std::function<std::shared_ptr<type>(const k::name&, const element&)> resolve_alias_name;
            resolve_alias_name = [&](const k::name& name, const element& scope) -> std::shared_ptr<type> {
                if (auto alias = scope_lookup::lookup_alias(scope.shared_as<const element>(), name)) {
                    if (!alias->is_soft()) return {};
                    bool cycle = false;
                    auto result = scope_lookup::materialize_alias_type(
                        alias, _context, resolve_alias_name, cycle,
                        [&](const std::shared_ptr<type>& target, const element& declaration_scope) {
                            auto unresolved = std::dynamic_pointer_cast<unresolved_type>(target);
                            if (!unresolved || unresolved->has_template_args()) return std::shared_ptr<type>{};
                            return resolve_alias_name(unresolved->type_id(), declaration_scope);
                        }, true);
                    if (cycle) {
                        throw_error(static_cast<unsigned int>(k::diag::alias_diag::ERR_ALIAS_CYCLE),
                                    alias->get_decl_lexeme(),
                                    "Alias '{}' is defined in terms of itself", {alias->get_fq_name()});
                    }
                    return result;
                }

                // Predeclare a forward aggregate's type without resolving its
                // body or layout out of order.
                auto relative = name.without_root_prefix();
                auto root = scope_lookup::root_namespace(scope);
                if (name.has_root_prefix() && root) {
                    auto module = root->get_name().without_root_prefix();
                    if (!module.empty() && relative.start_with(module))
                        relative = relative.without_front(module.size());
                }
                auto current = name.has_root_prefix()
                    ? std::static_pointer_cast<const element>(root)
                    : scope.shared_as<const element>();
                const auto predeclare = [&](const element& from, const k::name& lookup) {
                    auto agg = aggregate_type_resolver::resolve_struct_from(from, lookup);
                    if (!agg) return false;
                    if (!agg->get_struct_type() && !agg->is_template()) {
                        auto st = std::make_shared<struct_type>(agg->get_short_name(), agg);
                        agg->set_struct_type(st);
                    }
                    return true;
                };
                for (; current; current = current->parent<const element>()) {
                    if (predeclare(*current, relative)) break;
                    bool found = false;
                    if (!name.has_root_prefix()) {
                        if (auto holder = std::dynamic_pointer_cast<const using_holder>(current)) {
                            for (const auto& directive : holder->get_using_directives()) {
                                auto target_name = directive.target_name;
                                auto lookup = name;
                                if (directive.is_namespace()) {
                                    if (directive.has_alias()) {
                                        if (name.size() < 2 || name.front() != *directive.alias_name) continue;
                                        lookup = name.without_front();
                                    }
                                } else {
                                    const auto& visible = directive.has_alias()
                                        ? *directive.alias_name : target_name.back();
                                    if (name.front() != visible) continue;
                                    lookup = k::name{target_name.back()};
                                    for (size_t i = 1; i < name.size(); ++i)
                                        lookup = lookup.with_back(name[i]);
                                    target_name = target_name.without_back();
                                }
                                auto target = target_name.empty()
                                    ? std::static_pointer_cast<const element>(root)
                                    : resolve_using_target(target_name, _unit);
                                if (target && predeclare(*target, lookup)) {
                                    found = true;
                                    break;
                                }
                            }
                        }
                    }
                    if (found || name.has_root_prefix()
                        || scope_lookup::is_definition_namespace_boundary(*current)) break;
                }
                return type_resolver.resolve_type_by_name(name, scope);
            };
            aliased_object_type = std::dynamic_pointer_cast<struct_type>(
                resolve_alias_name(base_name, en));
        }

        // First try local scope lookup (simple name)
        if (!aliased_object_type) {
            base_en = scope_lookup::lookup_enumeration(
                en.shared_as<element>(), base_name_str);
        }

        // If not found and name is qualified, try imported enums
        if (!aliased_object_type && !base_en && base_name_str.find("::") != std::string::npos) {
            // Parse the qualified name into a k::name
            std::vector<std::string> parts;
            std::size_t pos = 0;
            while (true) {
                auto sep = base_name_str.find("::", pos);
                if (sep == std::string::npos) {
                    parts.push_back(base_name_str.substr(pos));
                    break;
                }
                parts.push_back(base_name_str.substr(pos, sep - pos));
                pos = sep + 2;
            }
            k::name qualified_name{false, std::move(parts)};
            base_en = _unit.get_or_create_imported_enum(qualified_name, _context);
        }

        if (!base_en) {
            // Not an enum — try to find as a struct (object-backed typed enum)
            auto obj_agg = aliased_object_type
                ? aliased_object_type->get_struct()
                : scope_lookup::lookup_structure(en.shared_as<element>(), base_name_str);
            if (!obj_agg && base_name_str.find("::") != std::string::npos) {
                // Also try via qualified name resolution
                std::vector<std::string> parts;
                std::size_t pos = 0;
                while (true) {
                    auto sep = base_name_str.find("::", pos);
                    if (sep == std::string::npos) { parts.push_back(base_name_str.substr(pos)); break; }
                    parts.push_back(base_name_str.substr(pos, sep - pos)); pos = sep + 2;
                }
                k::name qname{false, std::move(parts)};
                if (auto imported_agg = _unit.get_or_create_imported_aggregate(qname, _context))
                    obj_agg = imported_agg;
            }
            if (obj_agg) {
                // Object-backed typed enum: entries are represented as unsigned indices
                en.set_object_type(aliased_object_type ? aliased_object_type : obj_agg->get_struct_type());
                // Object-backed enums have no base enum (derivation not supported)
                // Fall through to entry resolution below (index assignment)
            } else {
                throw_error(static_cast<unsigned int>(k::diag::structure_diag::ERR_ENUM_ENTRY_NOT_FOUND), en_lexeme,
                    "Enum '{}': base enum or underlying type '{}' not found",
                    {en.get_short_name(), base_name_str});
            }
        } else {
            // Step 2: Determine the underlying type (explicit or inherited from base)
            // Recursively resolve the base if it hasn't been resolved yet
            resolve_enumeration(*base_en);
            en.set_base(base_en);
        }
    }

    // Inherit object-backed typed mode from base enum (typed derivation).
    if (!en.is_object_backed() && en.has_base() && en.get_base()->is_object_backed()) {
        en.set_object_type(en.get_base()->get_object_type());
    }

    // ── 2. Build the combined list of work entries ──
    // For derived enums, prepend the base entries (already resolved) so that
     // references and auto-increment from local entries can see them.
    struct work_entry {
        std::string name;
        bool from_base = false;
        bool has_literal = false;
        int64_t literal_value = 0;
        std::string ref_name;
        bool is_default = false;
        bool resolved = false;
        int64_t resolved_value = 0;
        // For object-backed entries: the brace_init AST node (may be null for zero-init)
        std::shared_ptr<k::parse::ast::brace_init_list> brace_init;
        // For object-backed entries: constructor args for `ENTRY(...)`
        std::vector<std::shared_ptr<expression>> ctor_args;
        // For implicitly incremented object-backed entries
        bool is_implicit_increment = false;
        // True for alias entries (`ENTRY = OTHER`).
        bool is_alias = false;
    };
    std::vector<work_entry> work;

    // Collect base entries
    if (en.has_base()) {
        for (auto& be : en.get_base()->entries()) {
            work_entry we;
            we.name = be.name;
            we.from_base = true;
            we.has_literal = true;
            we.literal_value = be.value;
            we.resolved = true;
            we.resolved_value = be.value;
            we.is_default = be.is_default;
            we.brace_init = be.brace_init;
            we.ctor_args = be.ctor_args;
            we.is_implicit_increment = be.is_implicit_increment;
            we.is_alias = be.is_alias;
            work.push_back(std::move(we));
        bool has_paren_init = false;
        std::shared_ptr<k::parse::ast::enum_entry> ast_entry;
        }
    }

    // Collect local raw entries
    size_t local_start = work.size();
    for (auto& re : en.raw_entries()) {
        work_entry we;
        we.name = re.name;
        we.from_base = false;
        we.is_default = re.is_default;

        if (en.is_object_backed()) {
            // Object-backed entries keep rich initializer shape for table construction.
            we.brace_init = re.brace_init;
            we.ctor_args = re.ctor_args;
            if (!re.ref_name.empty()) {
                we.ref_name = re.ref_name;
                we.is_alias = true;
            }
        } else {
            we.has_paren_init = be.has_paren_init;
            we.ast_entry = be.ast_entry;
            if (re.explicit_value.has_value()) {
                we.has_literal = true;
                we.literal_value = *re.explicit_value;
                we.resolved = true;
                we.resolved_value = *re.explicit_value;
            } else if (!re.ref_name.empty()) {
                we.ref_name = re.ref_name;
            }
        }
        // Check for name shadowing with base entries (warning)
        if (en.has_base()) {
            for (size_t i = 0; i < local_start; ++i) {
                if (work[i].name == we.name) {
        we.ast_entry = re.ast_entry;
        we.has_paren_init = re.has_paren_init;
                    warn(static_cast<unsigned int>(k::diag::structure_diag::WARN_ENUM_ENTRY_SHADOW), en_lexeme,
                        "Enum '{}': entry '{}' shadows an inherited entry from base enum '{}'",
                        {en.get_short_name(), we.name, en.get_base()->get_short_name()});
            if (re.explicit_value) {
                lex::opt_any_lexeme entry_lexeme = re.ast_entry
                    ? lex::opt_any_lexeme{lex::any_lexeme{re.ast_entry->name}} : en_lexeme;
                throw_error(static_cast<unsigned int>(k::diag::structure_diag::ERR_ENUM_OBJECT_VALUE_INITIALIZER),
                    entry_lexeme, "Object-backed enum entry '{}' cannot use an integer value initializer",
                    {re.name});
            }
                    break;
                }
            }
        }
        work.push_back(std::move(we));
    }

    // For object-backed enums: resolve references and compute implicit entries
    if (en.is_object_backed()) {
        // Assign indices to non-alias entries first.
        int64_t next_index = 0;
        for (auto& we : work) {
            if (we.is_alias) continue;
            we.resolved = true;
            we.resolved_value = next_index++;
        }

        // Resolve aliases (supports chains, rejects cycles/missing targets).
        bool changed = true;
        const size_t max_iter = work.size() + 1;
        for (size_t iter = 0; iter < max_iter && changed; ++iter) {
            changed = false;
            for (auto& we : work) {
                if (!we.is_alias || we.resolved) continue;
                for (auto& other : work) {
                    if (other.name == we.ref_name && other.resolved) {
                        we.resolved = true;
                        we.resolved_value = other.resolved_value;
                        changed = true;
                        break;
                    }
                }
            }
        }
        for (auto& we : work) {
            if (we.is_alias && !we.resolved) {
                throw_error(static_cast<unsigned int>(k::diag::structure_diag::ERR_ENUM_ENTRY_AMBIGUOUS), en_lexeme,
                    "Enum entry '{}' references unresolvable entry '{}' (cycle or missing entry)",
                    {we.name, we.ref_name});
            }
        }

        // Mark implicit entries (non-alias entries without explicit init and not first effective entry).
        for (size_t i = 1; i < work.size(); ++i) {
            auto& we = work[i];
            if (we.is_alias) continue;
            if (we.brace_init || !we.ctor_args.empty()) continue;
            we.is_implicit_increment = true;
        }
    } else {
        // For integer enums
        // ── 3. Resolve references and auto-increment ──
        bool changed = true;
        size_t max_iter = work.size() + 1;
        for (size_t iter = 0; iter < max_iter && changed; ++iter) {
            changed = false;
            for (size_t i = 0; i < work.size(); ++i) {
                auto& we = work[i];
                if (we.resolved) continue;

                if (!we.ref_name.empty()) {
                    // Try to resolve the reference against all entries (base + local)
                    for (auto& other : work) {
                        if (other.name == we.ref_name && other.resolved) {
                            we.resolved = true;
                            we.resolved_value = other.resolved_value;
                            changed = true;
                            break;
                        }
                    }
                } else {
                    // Auto-increment: value = previous entry's value + 1, or 0 if first
                    if (i == 0) {
                        we.resolved = true;
                        we.resolved_value = 0;
                        changed = true;
                    } else if (work[i-1].resolved) {
                        we.resolved = true;
                        we.resolved_value = work[i-1].resolved_value + 1;
                        changed = true;
                    }
                }
            }
        }

        // Check for unresolved entries
        for (auto& we : work) {
            if (!we.resolved) {
                if (!we.ref_name.empty()) {
                    throw_error(static_cast<unsigned int>(k::diag::structure_diag::ERR_ENUM_UNDERLYING_NOT_INT), en_lexeme,
                        "Enum entry '{}' references unresolvable entry '{}' (cycle or missing entry)",
                        {we.name, we.ref_name});
                } else {
                    throw_error(static_cast<unsigned int>(k::diag::structure_diag::ERR_ENUM_BASE_NOT_ENUM), en_lexeme,
                        "Enum entry '{}' could not be resolved (depends on unresolved previous entry)",
                        {we.name});
                }
            }
        }
    }

    // ── 4. Determine whether the derived enum overrides the default ──
    bool has_local_default = false;
    for (size_t i = local_start; i < work.size(); ++i) {
        if (work[i].is_default) { has_local_default = true; break; }
    }
    // If the derived has a local default, clear the inherited default flag
    if (has_local_default && en.has_base()) {
        for (size_t i = 0; i < local_start; ++i) {
            work[i].is_default = false;
        }
    }
    // If no explicit default anywhere, mark the first entry as default
    bool any_default = false;
    for (auto& we : work) { if (we.is_default) { any_default = true; break; } }
    if (!any_default && !work.empty()) {
        work[0].is_default = true;
    }

    // ── 5. Determine the underlying type ──
    // Either the explicit primitive type from 'enum X : <primitive>', or (absent that)
    // the smallest primitive type that can hold all declared entry values.
    std::shared_ptr<primitive_type> underlying;
    if (en.is_object_backed()) {
        // Object-backed: runtime representation is an unsigned index into the backing table.
        // Use the smallest unsigned type that can hold all entry indices.
        size_t n = 0;
        for (auto& we : work) {
            if (we.resolved_value >= 0) {
                n = std::max(n, static_cast<size_t>(we.resolved_value + 1));
            }
        }
        primitive_type::PRIMITIVE_TYPE prim_type;
        if (n <= 256)         prim_type = primitive_type::UNSIGNED_BYTE;
        else if (n <= 65536)  prim_type = primitive_type::UNSIGNED_SHORT;
        else                  prim_type = primitive_type::UNSIGNED_INT;
        underlying = _context->from_type(prim_type);
    } else {
        int64_t min_val = 0, max_val = 0;
        for (auto& we : work) {
            if (we.resolved_value < min_val) min_val = we.resolved_value;
            if (we.resolved_value > max_val) max_val = we.resolved_value;
        }

        if (auto explicit_underlying = en.get_explicit_underlying_type()) {
            // Explicit underlying type from 'enum X : <primitive>' (grammar.ebnf EnumDecl
            // disambiguation): use it verbatim instead of the smallest-fit computation,
            // after checking it can actually hold every declared entry value.
            int64_t type_min = 0, type_max = 0;
            bool is_integer = true;
            switch (explicit_underlying->get_type()) {
                case primitive_type::BYTE:                type_min = -128; type_max = 127; break;
                case primitive_type::UNSIGNED_BYTE:       type_min = 0; type_max = 255; break;
                case primitive_type::SHORT:               type_min = -32768; type_max = 32767; break;
                case primitive_type::UNSIGNED_SHORT:      type_min = 0; type_max = 65535; break;
                case primitive_type::INT:                 type_min = -2147483648LL; type_max = 2147483647LL; break;
                case primitive_type::UNSIGNED_INT:        type_min = 0; type_max = 4294967295LL; break;
                case primitive_type::LONG:
                case primitive_type::LONG_LONG:
                    type_min = std::numeric_limits<int64_t>::min();
                    type_max = std::numeric_limits<int64_t>::max();
                    break;
                case primitive_type::UNSIGNED_LONG:
                case primitive_type::UNSIGNED_LONG_LONG:
                    type_min = 0;
                    type_max = std::numeric_limits<int64_t>::max();
                    break;
                default:
                    is_integer = false;
                    break;
            }
            if (!is_integer) {
                throw_error(static_cast<unsigned int>(k::diag::structure_diag::ERR_ENUM_EXPLICIT_UNDERLYING_NOT_INTEGER), en_lexeme,
                    "Enum '{}': explicit underlying type must be an integer primitive type",
                    {en.get_short_name()});
            }
            if (min_val < type_min || max_val > type_max) {
                throw_error(static_cast<unsigned int>(k::diag::structure_diag::ERR_ENUM_EXPLICIT_UNDERLYING_TOO_SMALL), en_lexeme,
                    "Enum '{}': explicit underlying type cannot hold declared entry values (range [{}, {}])",
                    {en.get_short_name(), std::to_string(min_val), std::to_string(max_val)});
            }
            underlying = explicit_underlying;
        } else {
            primitive_type::PRIMITIVE_TYPE prim_type;
            if (min_val >= 0) {
                if (max_val <= 255) prim_type = primitive_type::UNSIGNED_BYTE;
                else if (max_val <= 65535) prim_type = primitive_type::UNSIGNED_SHORT;
                else if (max_val <= 4294967295LL) prim_type = primitive_type::UNSIGNED_INT;
                else prim_type = primitive_type::UNSIGNED_LONG;
            } else {
                if (min_val >= -128 && max_val <= 127) prim_type = primitive_type::BYTE;
                else if (min_val >= -32768 && max_val <= 32767) prim_type = primitive_type::SHORT;
                else if (min_val >= -2147483648LL && max_val <= 2147483647LL) prim_type = primitive_type::INT;
                else prim_type = primitive_type::LONG;
            }
            underlying = _context->from_type(prim_type);
        }
    }
    en.set_underlying_type(underlying);

    // ── 6. Populate the resolved entries ──
    // Store ALL entries (base + local) so that entries() returns the full set.
    for (auto& we : work) {
        en.add_entry(we.name, we.resolved_value, we.is_default, we.brace_init, we.ctor_args,
                     we.is_implicit_increment, we.is_alias, we.has_paren_init, we.ast_entry);
    }

    // Step 3: Build the enum_type from the enumeration model
    // ── 7. Create and register enum_type ──
    auto et = std::shared_ptr<enum_type>(new enum_type(
        std::dynamic_pointer_cast<enumeration>(en.shared_from_this()), underlying));
    en.set_enum_type(et);
    _context->add_enum(en.get_short_name(), et);

    // Step 4: Resolve each entry's value expression, checking for duplicates
    en._resolving = false;
    en.set_resolved(true);
}

// implementation_generator::visit_aggregate
// -------------------------------------------
// Generates the LLVM IR implementations for all members of an aggregate.
// Declarations were already emitted in the declaration pass; this pass fills in bodies.
//
// Steps:
//  1. Push the aggregate onto the struct stack so nested code-generation visitors
//     know the current owning aggregate context.
//  2. Visit every child (member variables, methods, constructors, destructor,
//     nested aggregates) — each child emits its LLVM function body.
//  3. Pop the struct stack.
// Note: vtable initializer filling is done in visit_klass (class-specific).
void implementation_generator::visit_aggregate(aggregate& st) {
    // Skip template definitions — they are not instantiated yet.
    if (st.is_template()) {
        trace("[implementation_generator::visit_aggregate] skipping template '{}'", {st.get_short_name()});
        return;
    }
    trace("[implementation_generator::visit_aggregate] '{}'", {st.get_short_name()});
    _struct_stack.push(st.shared_as<aggregate>());

    // Visit all children (variables, methods, constructors, destructor, nested aggregates).
    for(auto& child : st.get_children()) {
        child->accept(*this);
    }
    // Note: vtable filling is done in visit_klass

    _struct_stack.pop();
}

// symbol_resolver::visit_union
// ------------------------------------
// Resolves a union type definition: validates alternatives and names.
// Creates an early struct_type so that context::resolve_types() (run after
// Pass A) can find nested union types used as member variable types.
// LLVM layout (body) is finalised later in aggregate_type_resolver + declaration_generator.
void symbol_resolver::visit_union(union_type_def& un) {
    // Skip template definitions — only instantiations are resolved
    if (un.is_template()) return;

    visit_named_element(un);

    // Create an opaque struct_type early (like we do for aggregates) so that
    // context::resolve_types() can resolve member variable types that reference
    // this union before aggregate_type_resolver runs.
    if (!un.get_struct_type()) {
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
        _context->add_struct(st_type);
        un.set_struct_type(st_type);
    }

    // ── Resolve base union or polymorphic base (if any) ──────────────────────
    if (un.has_base_clause() && !un.get_base_union() && !un.get_direct_polymorphic_base()) {
        // Cycle detection: if we are already resolving this union, it means
        // the base chain has a cycle.
        if (un._resolving) {
            lex::opt_any_lexeme un_lexeme;
            if (auto ast_decl = un.get_ast_aggregate_decl()) {
                un_lexeme = lex::any_lexeme{ast_decl->kw_aggregate_type};
            }
            throw_error(static_cast<unsigned int>(k::diag::union_diag::ERR_UNION_CIRCULAR_INHERITANCE),
                un_lexeme,
                "Circular union inheritance detected involving union '{}'",
                {un.get_short_name()});
        }
        un._resolving = true;

        const std::string& raw = un.get_base_union_raw_name();
        auto base_un = scope_lookup::lookup_union(un.shared_as<element>(), raw);
        // Fallback: search imported modules' namespaces
        if (!base_un) {
            auto root_ns = scope_lookup::root_namespace(un);
            if (root_ns) {
                for (const auto& imp : _unit.get_imports()) {
                    if (imp.module_name.empty()) continue;
                    // Navigate to the imported module's namespace
                    std::shared_ptr<ns> cur_ns = root_ns;
                    for (size_t i = 0; i < imp.module_name.size() && cur_ns; ++i) {
                        cur_ns = cur_ns->get_child_namespace(imp.module_name[i]);
                    }
                    if (cur_ns) {
                        base_un = cur_ns->get_union(raw);
                        if (base_un) break;
                    }
                }
            }
        }
        if (base_un) {
            // Circularity check via already-resolved chain or via _resolving flag on base
            if (scope_lookup::is_base_union_of(un, *base_un) || base_un.get() == &un || base_un->_resolving) {
                lex::opt_any_lexeme un_lexeme;
                if (auto ast_decl = un.get_ast_aggregate_decl()) {
                    un_lexeme = lex::any_lexeme{ast_decl->kw_aggregate_type};
                }
                un._resolving = false;
                throw_error(static_cast<unsigned int>(k::diag::union_diag::ERR_UNION_CIRCULAR_INHERITANCE),
                    un_lexeme,
                    "Circular union inheritance detected involving union '{}'",
                    {un.get_short_name()});
            }
            // Recursively resolve the base if not yet done
            if (!base_un->get_struct_type()) {
                visit_union(*base_un);
            }
            un.set_base_union(base_un);
            un.reindex_own_alternatives();
            un._resolving = false;
        } else {
            // Not a base union. Check if it is a polymorphic base (class or interface).
            auto base_agg = scope_lookup::lookup_structure_or_import(_unit, _context, un.shared_as<element>(), raw);
            if (base_agg) {
                if (base_agg->is_class() || base_agg->is_interface()) {
                    un.set_polymorphic_base(base_agg);
                    un._resolving = false;
                } else {
                    lex::opt_any_lexeme un_lexeme;
                    if (auto ast_decl = un.get_ast_aggregate_decl()) {
                        un_lexeme = lex::any_lexeme{ast_decl->kw_aggregate_type};
                    }
                    un._resolving = false;
                    throw_error(static_cast<unsigned int>(k::diag::union_diag::ERR_UNION_POLYMORPHIC_BASE_INVALID),
                        un_lexeme,
                        "Base '{}' of union '{}' is a struct or non-class aggregate; polymorphic unions require a class or interface base",
                        {raw, un.get_short_name()});
                }
            } else {
                lex::opt_any_lexeme un_lexeme;
                if (auto ast_decl = un.get_ast_aggregate_decl()) {
                    un_lexeme = lex::any_lexeme{ast_decl->kw_aggregate_type};
                }
                un._resolving = false;
                throw_error(static_cast<unsigned int>(k::diag::union_diag::ERR_UNION_POLYMORPHIC_BASE_INVALID),
                    un_lexeme,
                    "Base '{}' of union '{}' is neither a known union nor a class or interface type",
                    {raw, un.get_short_name()});
            }
        }
    }

    // Union alternatives have their types already stored from model building.
    // Full type resolution will happen in aggregate_type_resolver.

    // Synthesize the Kind enum (now includes full inheritance chain)
    un.synthesize_kind_enum();

    trace("[symbol_resolver::visit_union] resolved union '{}' with {} alternatives ({} own)",
        {un.get_short_name(), std::to_string(un.total_alternative_count()),
         std::to_string(un.alternative_count())});
}

} // namespace k::model::gen
