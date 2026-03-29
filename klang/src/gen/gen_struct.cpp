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
#include "../common/operator_names.hpp"

#include "../model/imported.hpp"
#include "../parse/ast.hpp"

#include <llvm/IR/Verifier.h>

#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace k::model::gen {

// ─────────────────────────────────────────────────────────────────────────────
// Shared annotation resolution & @Target validation
// ─────────────────────────────────────────────────────────────────────────────

void symbol_resolver::resolve_and_validate_annotations(
    annotation_holder& holder,
    element& scope,
    const std::string& element_name,
    const lex::opt_any_lexeme& err_lexeme,
    const std::string& element_kind)
{
    // ── Phase 1: resolve each annotation_instance to a concrete annotation_type
    for (auto& ann_inst : holder.get_annotations_mutable()) {
        if (ann_inst.resolved_type) continue; // already resolved

        // Look up by name in local scope
        auto ann_agg = scope_lookup::lookup_structure(scope.shared_as<element>(), ann_inst.raw_name);
        if (!ann_agg) {
            // Try imported modules
            std::vector<std::string> parts;
            std::size_t start = 0;
            while (true) {
                auto pos = ann_inst.raw_name.find("::", start);
                if (pos == std::string::npos) {
                    parts.push_back(ann_inst.raw_name.substr(start));
                    break;
                }
                parts.push_back(ann_inst.raw_name.substr(start, pos - start));
                start = pos + 2;
            }
            k::name qname{false, parts};
            if (auto imp_agg = _unit.get_or_create_imported_aggregate(qname, _context)) {
                ann_agg = std::dynamic_pointer_cast<aggregate>(imp_agg);
            }
        }
        if (!ann_agg) {
            throw_error(0x003A, err_lexeme,
                "Annotation type '{}' not found on '{}'",
                std::vector<std::string>{ann_inst.raw_name, element_name});
        }
        if (!ann_agg->is_annotation()) {
            throw_error(0x003B, err_lexeme,
                "'{}' is not an annotation type; only annotation types can be used as annotations",
                std::vector<std::string>{ann_inst.raw_name});
        }
        ann_inst.resolved_type = ann_agg;
    }

    // ── Phase 2: validate @Target constraints on resolved annotations
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
                    throw_error(0x003C, err_lexeme,
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

// symbol_resolver::visit_aggregate
// ---------------------------------
// Resolves symbols and builds the structural skeleton of an aggregate (struct or class).
//
// Steps:
//  1. Resolve the named-element identity (name lookup, scope registration).
//  2. Pre-declare the aggregate's LLVM struct type and register it in the context
//     so recursive/forward references can succeed.
//  3. Recursively visit nested aggregate children first, so their types exist
//     before the outer aggregate's members reference them.
//  4. For each declared base:
//     a. Resolve the base name to a concrete aggregate via scope lookup.
//     b. Validate constraints: final base, const/mutable mismatch, struct/class cross-inheritance.
//     c. Emit warnings for inner↔outer circular nesting patterns.
//  5. Detect cycles in the inheritance graph (DFS-based visited set).
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
// 14. Generate a default copy constructor if bases or struct-typed members exist and no
//     copy constructor was declared.
// 15. Generate an implicit destructor if any base or member struct has a destructor.
// Note: class-specific processing (vtable layout, vptr injection) is done in visit_klass.
void symbol_resolver::visit_aggregate(aggregate& st) {
    lex::opt_any_lexeme st_lexeme;
    if (auto ast_ad = st.get_ast_aggregate_decl()) st_lexeme = lex::any_lexeme{ast_ad->name};

    visit_named_element(st);

    // Pre declare type
    // TODO Mangle struct name to avoid collisions
    std::shared_ptr<struct_type> st_type{new struct_type(st.get_short_name()/*st.get_mangled_name()*/, st.shared_as<aggregate>())};
    _context->add_struct(st_type);
    st.set_struct_type(st_type);

    // Visit nested aggregate children first (they need their own types declared)
    for(auto& child : st.get_children()) {
        if(auto nested_st = std::dynamic_pointer_cast<aggregate>(child)) {
            nested_st->accept(*this);
        }
    }

    // ── Inheritance: resolve base class names ──────────────────────────────────
    if (st.has_bases()) {
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
            std::shared_ptr<aggregate> base_st;

            // Try scope-local lookup first (simple name or namespace-qualified)
            if (bs.raw_name.find("::") == std::string::npos) {
                // Simple name: standard scope-chain lookup
                base_st = scope_lookup::lookup_structure(st.shared_as<element>(), bs.raw_name);
            } else {
                // Qualified name: split on "::" and descend namespaces from root
                auto root_ns_ptr = scope_lookup::root_namespace(st);
                if (root_ns_ptr) {
                    std::vector<std::string> parts;
                    std::size_t start = 0;
                    while (true) {
                        auto pos = bs.raw_name.find("::", start);
                        if (pos == std::string::npos) {
                            parts.push_back(bs.raw_name.substr(start));
                            break;
                        }
                        parts.push_back(bs.raw_name.substr(start, pos - start));
                        start = pos + 2;
                    }
                    k::name qname{false, parts};
                    if (auto res = aggregate_type_resolver::resolve_struct_from(*root_ns_ptr, qname)) {
                        base_st = res;
                    }
                }
            }

            // Fallback: search imported modules
            if (!base_st) {
                std::vector<std::string> parts;
                std::size_t start = 0;
                const auto& raw = bs.raw_name;
                while (true) {
                    auto pos = raw.find("::", start);
                    if (pos == std::string::npos) {
                        parts.push_back(raw.substr(start));
                        break;
                    }
                    parts.push_back(raw.substr(start, pos - start));
                    start = pos + 2;
                }
                k::name qname{false, parts};
                if (auto imp_agg = _unit.get_or_create_imported_aggregate(qname, _context)) {
                    // imported_aggregate is also an aggregate (via inheritance)
                    base_st = std::dynamic_pointer_cast<aggregate>(imp_agg);
                }
            }

            if (!base_st) {
                throw_error(0x0010, st_lexeme,
                    "Base class '{}' of struct '{}' is not found",
                    {bs.raw_name, st.get_short_name()});
            }

            // A final struct cannot be used as a base class
            if (base_st->is_final()) {
                throw_error(0x0012, st_lexeme,
                    "Cannot inherit from '{}' in struct '{}': '{}' is declared final and cannot be used as a base class",
                    {bs.raw_name, st.get_short_name(), bs.raw_name});
            }

            // A const struct cannot inherit from a mutable (non-const) struct.
            // Annotation types are excluded: they implicitly inherit from
            // ::k::Annotation (a const class) which may not carry the const flag
            // when imported; the implicit constness is guaranteed by the compiler.
            if (st.is_const_struct() && !base_st->is_const_struct() && !st.is_annotation()) {
                throw_error(0x0033, st_lexeme,
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
                    throw_error(0x0035, st_lexeme,
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
                    std::clog << "Warning: inner struct '" << st.get_short_name()
                              << "' inherits from enclosing struct '" << base_st->get_short_name() << "'" << std::endl;
                }
                if (base_st->is_nested()) {
                    auto base_outer = base_st->get_enclosing_structure();
                    if (base_outer && (base_outer.get() == &st || st.is_derived_from(base_outer))) {
                        std::clog << "Warning: struct '" << st.get_short_name()
                                  << "' inherits from inner struct '" << base_st->get_short_name() << "'" << std::endl;
                    }
                }
            }
        }

        // Detect cycles after resolution
        std::unordered_set<const aggregate*> visited;
        visited.insert(&st);
        if (detect_cycle(&st, visited)) {
            throw_error(0x0011, st_lexeme,
                "Circular inheritance detected in struct '{}'",
                {st.get_short_name()});
        }

        // Inject base sub-objects as synthetic member variables in DECLARATION ORDER.
        // Non-virtual bases: embed as __base_X__. Virtual bases: inject __vbptr_X__.
        std::vector<base_spec>& bases_mutable = st.get_bases_mutable();
        for (auto it = bases_mutable.rbegin(); it != bases_mutable.rend(); ++it) {
            auto& bs = *it;
            if (!bs.base || !bs.base->get_struct_type()) continue;
            if (bs.is_virtual) {
                std::string vbptr_name = "__vbptr_" + bs.sanitised_name() + "__";
                if (!st._vars.count(vbptr_name)) {
                    auto vbptr_field = member_variable_definition::make_shared(st.shared_as<aggregate>(), vbptr_name);
                    st._vars.insert({vbptr_name, vbptr_field});
                    st._children.insert(st._children.begin(), vbptr_field);
                }
            } else {
                std::string subobj_name = "__base_" + bs.sanitised_name() + "__";
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

                bool has_collector_base = false;
                for (auto& bs : st.get_bases()) {
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
    }

    // For non-static inner aggregates, inject a synthetic __parent__ member variable.
    if (st.is_inner()) {
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
                warn(0x0010, st_lexeme,
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
    bool needs_copy_ctor = st.has_bases();
    if (!needs_copy_ctor) {
        for (auto& [name, var] : st.variables()) {
            if (auto mv = std::dynamic_pointer_cast<member_variable_definition>(var)) {
                if (type::is_struct(mv->get_type())) { needs_copy_ctor = true; break; }
            }
        }
    }
    if (needs_copy_ctor && !st.get_copy_constructor()) {
        std::clog << "Warning: struct '" << st.get_short_name()
                  << "' has bases or struct members but no copy constructor; "
                     "a default copy constructor will be generated." << std::endl;
        auto copy_ctor = constructor::make_shared(st.shared_as<aggregate>());
        copy_ctor->set_compiler_generated(true);
        copy_ctor->set_copy_constructor(true);
        copy_ctor->append_parameter("other", st_type->get_reference());
        st._constructors.push_back(copy_ctor);
        st._children.push_back(copy_ctor);
        copy_ctor->accept(*this);
    }

    // ── Implicit copy assignment operator for structs: generate if absent and not deleted ──
    // Only for structs (not classes/interfaces). If the user explicitly declares
    // __operator_aS_ (with body or -> delete), we don't generate one.
    if (!st.is_class() && !std::dynamic_pointer_cast<interface>(st.shared_as<aggregate>())) {
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
}

// signature_resolver::visit_aggregate
// ------------------------------------
// Pre-resolve function signatures (parameter + return types) within an aggregate,
// without processing function bodies or member variable init expressions.
void signature_resolver::visit_aggregate(aggregate& st) {
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
    lex::opt_any_lexeme st_lexeme;
    if (auto ast_ad = st.get_ast_aggregate_decl()) st_lexeme = lex::any_lexeme{ast_ad->name};
    // Note: const-struct method promotion is already done in symbol_resolver phase.

    // Visit nested aggregate children first
    for(auto& child : st.get_children()) {
        if(auto nested_st = std::dynamic_pointer_cast<aggregate>(child)) {
            nested_st->accept(*this);
        }
    }

    // Visit all other children (functions, constructors, destructors), skip nested aggregates.
    for(auto& child : st.get_children()) {
        if(std::dynamic_pointer_cast<aggregate>(child)) continue;
        child->accept(*this);
    }

    // After all members are resolved, check for overload collisions.
    check_overload_collisions(st);
    check_constructor_overload_collisions(st);

    // Warn if assignment operators don't return a reference to the owning type
    auto st_type = st.get_struct_type();
    if (st_type) {
        auto expected_ret = st_type->get_reference();
        for (auto& fn : st.functions()) {
            if (!fn || !fn->is_operator()) continue;
            auto name = fn->get_short_name();
            bool is_asgn = k::op::is_assignment_operator(name);
            if (!is_asgn) continue;
            if (fn->is_deleted()) continue; // deleted operators have no return type
            if (!fn->has_return_type()) {
                warn(0x00B1, st_lexeme,
                    "Assignment operator '{}' in '{}' has no return type; "
                    "conventionally it should return '{}' to allow chaining (e.g. a = b = c)",
                    {name, st.get_short_name(), expected_ret ? expected_ret->to_string() : st.get_short_name() + "&"});
            } else {
                auto ret = fn->get_return_type();
                if (!type::is_reference(ret) ||
                    !type::are_equal(type::remove_const(std::dynamic_pointer_cast<reference_type>(ret)->get_subtype()),
                                     type::remove_const(st_type))) {
                    warn(0x00B2, st_lexeme,
                        "Assignment operator '{}' in '{}' returns '{}' instead of '{}'; "
                        "returning a reference to the owning type is recommended for chaining",
                        {name, st.get_short_name(),
                         ret ? ret->to_string() : "void",
                         expected_ret ? expected_ret->to_string() : st.get_short_name() + "&"});
                }
            }
        }
    }

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
// Enums have no LLVM-level declarations; their underlying type is already handled
// by the symbol resolver. This override exists only to prevent the default visitor
// from trying to visit children that don't exist.
void declaration_generator::visit_enumeration(enumeration&) {
    // Nothing to do.
}

// symbol_resolver::visit_enumeration
// ------------------------------------
// Resolves an enumeration: base lookup, entry value resolution, underlying type
// selection, and enum_type creation. Supports enum derivation (single inheritance,
// multi-level) and deferred resolution (no pre-declaration ordering requirement).
void symbol_resolver::visit_enumeration(enumeration& en) {
    visit_named_element(en);
    resolve_enumeration(en);
}

void symbol_resolver::resolve_enumeration(enumeration& en) {
    if (en.is_resolved()) return;

    lex::opt_any_lexeme en_lexeme;
    if (auto ast_ed = en.get_ast_enum_decl()) en_lexeme = lex::any_lexeme{ast_ed->name};

    // Cycle detection
    if (en._resolving) {
        throw_error(0x0090, en_lexeme,
            "Circular enum derivation detected involving enum '{}'",
            {en.get_short_name()});
    }
    en._resolving = true;

    // ── 1. Resolve base enum if present ──
    if (en.get_base_name().has_value()) {
        const std::string& base_name_str = *en.get_base_name();
        std::shared_ptr<enumeration> base_en;

        // First try local scope lookup (simple name)
        base_en = scope_lookup::lookup_enumeration(
            en.shared_as<element>(), base_name_str);

        // If not found and name is qualified, try imported enums
        if (!base_en && base_name_str.find("::") != std::string::npos) {
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
            throw_error(0x0091, en_lexeme,
                "Enum '{}': base enum '{}' not found",
                {en.get_short_name(), base_name_str});
        }
        // Recursively resolve the base if it hasn't been resolved yet
        resolve_enumeration(*base_en);
        en.set_base(base_en);
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
            work.push_back(std::move(we));
        }
    }

    // Collect local raw entries
    size_t local_start = work.size();
    for (auto& re : en.raw_entries()) {
        work_entry we;
        we.name = re.name;
        we.from_base = false;
        we.is_default = re.is_default;
        if (re.explicit_value.has_value()) {
            we.has_literal = true;
            we.literal_value = *re.explicit_value;
            we.resolved = true;
            we.resolved_value = *re.explicit_value;
        } else if (!re.ref_name.empty()) {
            we.ref_name = re.ref_name;
        }
        // Check for name shadowing with base entries (warning)
        if (en.has_base()) {
            for (size_t i = 0; i < local_start; ++i) {
                if (work[i].name == we.name) {
                    logger_relay::warn(with_flag(0x0092), en_lexeme,
                        "Enum '{}': entry '{}' shadows an inherited entry from base enum '{}'",
                        {en.get_short_name(), we.name, en.get_base()->get_short_name()});
                    break;
                }
            }
        }
        work.push_back(std::move(we));
    }

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
                throw_error(0x0073, en_lexeme,
                    "Enum entry '{}' references unresolvable entry '{}' (cycle or missing entry)",
                    {we.name, we.ref_name});
            } else {
                throw_error(0x0074, en_lexeme,
                    "Enum entry '{}' could not be resolved (depends on unresolved previous entry)",
                    {we.name});
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

    // ── 5. Determine the smallest underlying type ──
    int64_t min_val = 0, max_val = 0;
    for (auto& we : work) {
        if (we.resolved_value < min_val) min_val = we.resolved_value;
        if (we.resolved_value > max_val) max_val = we.resolved_value;
    }

    primitive_type::PRIMITIVE_TYPE prim_type;
    if (min_val >= 0) {
        if (max_val <= 255) prim_type = primitive_type::BYTE;
        else if (max_val <= 65535) prim_type = primitive_type::UNSIGNED_SHORT;
        else if (max_val <= 4294967295LL) prim_type = primitive_type::UNSIGNED_INT;
        else prim_type = primitive_type::UNSIGNED_LONG;
    } else {
        if (min_val >= -128 && max_val <= 127) prim_type = primitive_type::CHAR;
        else if (min_val >= -32768 && max_val <= 32767) prim_type = primitive_type::SHORT;
        else if (min_val >= -2147483648LL && max_val <= 2147483647LL) prim_type = primitive_type::INT;
        else prim_type = primitive_type::LONG;
    }

    auto underlying = _context->from_type(prim_type);
    en.set_underlying_type(underlying);

    // ── 6. Populate the resolved entries ──
    // Store ALL entries (base + local) so that entries() returns the full set.
    for (auto& we : work) {
        en.add_entry(we.name, we.resolved_value, we.is_default);
    }

    // ── 7. Create and register enum_type ──
    auto et = std::shared_ptr<enum_type>(new enum_type(
        std::dynamic_pointer_cast<enumeration>(en.shared_from_this()), underlying));
    en.set_enum_type(et);
    _context->add_enum(en.get_short_name(), et);

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
    _struct_stack.push(st.shared_as<aggregate>());

    // Visit all children (variables, methods, constructors, destructor, nested aggregates).
    for(auto& child : st.get_children()) {
        child->accept(*this);
    }
    // Note: vtable filling is done in visit_klass

    _struct_stack.pop();
}

} // namespace k::model::gen
