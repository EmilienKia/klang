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

#include "../model/imported.hpp"

#include <llvm/IR/Verifier.h>

#include <queue>
#include <unordered_map>
#include <unordered_set>

namespace k::model::gen {

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
                throw_error(0x0010, std::nullopt,
                    "Base class '{}' of struct '{}' is not found",
                    {bs.raw_name, st.get_short_name()});
            }

            // A final struct cannot be used as a base class
            if (base_st->is_final()) {
                throw_error(0x0012, std::nullopt,
                    "Cannot inherit from '{}' in struct '{}': '{}' is declared final and cannot be used as a base class",
                    {bs.raw_name, st.get_short_name(), bs.raw_name});
            }

            // A const struct cannot inherit from a mutable (non-const) struct
            if (st.is_const_struct() && !base_st->is_const_struct()) {
                throw_error(0x0033, std::nullopt,
                    "const struct '{}' cannot inherit from mutable struct '{}': "
                    "a const struct may only inherit from other const structs",
                    {st.get_short_name(), bs.raw_name});
            }

            // Cross-type inheritance (struct/class mix) is forbidden.
            // Interfaces count as class-like for this check (a class may implement an interface,
            // and an interface may extend another interface).
            bool st_is_class_like   = st.is_class()       || std::dynamic_pointer_cast<const model::interface>(st.shared_as<const element>()) != nullptr;
            bool base_is_class_like = base_st->is_class() || std::dynamic_pointer_cast<model::interface>(base_st) != nullptr;
            if (st_is_class_like != base_is_class_like) {
                std::string kind_st   = st.is_class()       ? "class" : "struct";
                std::string kind_base = base_st->is_class() ? "class" : "struct";
                throw_error(0x0035, std::nullopt,
                    "{} '{}' cannot inherit from {} '{}': "
                    "cross-inheritance between class and struct is not allowed",
                    {kind_st, st.get_short_name(), kind_base, bs.raw_name});
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
            throw_error(0x0011, std::nullopt,
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
                warn(0x0010, std::nullopt,
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

    // ── Implicit destructor: generate if absent and struct needs one ──────────
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
                    if (auto mv_st_type = std::dynamic_pointer_cast<struct_type>(mv->get_type())) {
                        if (mv_st_type->get_struct() && mv_st_type->get_struct()->get_destructor()) {
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
}

// type_reference_resolver::visit_aggregate
// -----------------------------------------
// Resolves all type references inside an aggregate and validates overload sets.
// Note: const-struct method promotion is already performed in symbol_resolver phase.
//
// Steps:
//  1. Visit nested aggregate children first (depth-first), so their resolved LLVM
//     types are available before outer members reference them.
//  2. Visit all remaining children (functions, constructors, destructor, variables) —
//     excluding nested aggregates already handled in step 1.  This resolves parameter
//     types, return types, and expression types inside each member.
//  3. After all members are resolved, check for method overload collisions
//     (duplicate signatures) and constructor overload collisions within this aggregate.
// Note: class-specific LLVM vtable struct-type building is done in visit_klass.
void type_reference_resolver::visit_aggregate(aggregate& st) {
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
