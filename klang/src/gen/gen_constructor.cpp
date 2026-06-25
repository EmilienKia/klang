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
// gen_constructor.cpp — Code generation for K language constructors, destructors,
//                       static constructors/destructors, global constructors/destructors,
//                       and the global main function proxy.
//
// This file contains all visitor method overrides and helper functions
// related to constructors, destructors, and lifecycle management.
#include "resolvers.hpp"
#include "generators.hpp"
#include "gen_helpers.hpp"
#include "gen_intrinsics.hpp"
#include "../model/expressions.hpp"
#include "../model/statements.hpp"
#include "../model/imported.hpp"
#include "../model/mangler.hpp"
#include "../parse/ast.hpp"
#include <llvm/IR/Verifier.h>
#include <llvm/Transforms/Utils/ModuleUtils.h>
#include <algorithm>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include "../errors.hpp"
namespace k::model::gen {

namespace {
/**
 * Normalize a base-class raw_name to the simple name used in a constructor
 * member-initializer. A base may be declared with namespace qualification
 * and/or template arguments (e.g. "k::io::FilterInputStream<byte>"), while the
 * mem-initializer target is always written as a bare identifier
 * (e.g. "FilterInputStream"). This strips any template-argument list (from the
 * first '<') and any namespace qualification (keeping the last "::" component)
 * so the two can be matched.
 */
std::string base_init_simple_name(const std::string& raw) {
    std::string r = raw;
    if (auto lt = r.find('<'); lt != std::string::npos) {
        r = r.substr(0, lt);
    }
    if (auto cc = r.rfind("::"); cc != std::string::npos) {
        r = r.substr(cc + 2);
    }
    return r;
}
} // anonymous namespace

/**
 * Visit a constructor during symbol resolution: resolve parameter types,
 * member-initializer targets, and body symbols.
 *
 * Steps:
 *   1. Resolve parameter types and default expressions.
 *   2. Resolve each member-initializer target (field or base class).
 *   3. Visit the constructor body block.
 */
void symbol_resolver::visit_constructor(constructor& ctor) {
    // Step 1: Resolve parameter types and default expressions
    // Deleted constructors have no body: skip body resolution and member-init injection.
    // The constructor already appears in the overload set because it was added to
    // _constructors by model_builder. Naming and 'this' setup are not needed here
    // because get_best_matching_constructor only looks at parameter count / types,
    // and symbol_resolver has not yet resolved the parameter types anyway.
    if (ctor.is_deleted()) {
        return;
    }

    // For non-static inner structs, inject the implicit 'parent' parameter
    // as the first explicit parameter (position 0, after the implicit 'this').
    // Type is Outer& (reference), consistent with 'this' parameter semantics.
    auto st = ctor.get_owner();
    if (st && st->is_inner()) {
        auto outer_st = st->get_enclosing_structure();
        auto outer_ref_type = outer_st->get_struct_type()->get_reference();
        // Only inject if not already present (avoid double-injection if revisited)
        if (!ctor.get_parameter("__parent__")) {
            ctor.insert_parameter("__parent__", outer_ref_type, 0);
        }
    }

    // ── Mark base-class member_inits and detect copy constructor ──────────────
    if (st) {
        // Build set of base names (direct + transitively-declared virtual bases)
        std::unordered_map<std::string, std::shared_ptr<aggregate>> base_by_name;
        for (auto& bs : st->get_bases()) {
            if (bs.base) base_by_name[base_init_simple_name(bs.raw_name)] = bs.base;
        }
        // Also include transitively-collected virtual bases (e.g., A in D : B,C where B,C : virtual A)
        for (auto& vbase : st->get_all_virtual_base_structs()) {
            base_by_name.emplace(base_init_simple_name(vbase->get_short_name()), vbase);
        }

        // Step 2: Resolve each member-initializer target (field or base class)
        // Mark each explicit mem-init as base-init or member-init
        for (auto& mi : const_cast<std::vector<constructor::member_init_spec>&>(ctor.member_inits())) {
            auto it = base_by_name.find(base_init_simple_name(mi.member_name));
            if (it != base_by_name.end()) {
                mi.is_base_init = true;
                mi.base_struct = it->second;
            }
        }

        // Detect copy constructor: single non-this param whose type is a ref to this struct
        if (ctor.get_parameter_size() == 1 && !ctor.is_compiler_generated()) {
            auto p0 = ctor.get_parameter(0);
            if (p0) {
                auto ptype = p0->get_type();
                if (auto ref = std::dynamic_pointer_cast<reference_type>(ptype)) {
                    if (auto sub_st = std::dynamic_pointer_cast<struct_type>(ref->get_referenced_type())) {
                        if (sub_st->get_struct() && sub_st->get_struct().get() == st.get()) {
                            ctor.set_copy_constructor(true);
                        }
                    }
                }
            }
        }
    }

    // Step 3: Visit the constructor body block
    // Before resolving the block, inject expression_statements for each explicit member
    // initializer into the beginning of the constructor block. This ensures that when
    // visit_function → visit_block visits the block, the symbol expressions inside the
    // mem-init args have a proper parent in the element hierarchy and can resolve
    // parameter references correctly.
    // Injected in struct member declaration order (as in C++), not in the list order.
    //
    // For base inits: we'll inject a constructor_invocation_expression targeting the
    // synthetic __base_X__ subobject field.

    auto blck = ctor.get_block();
    // Note: 'st' already declared above for inner-struct check
    if (blck && st) {
        // Track actual number of base ctor stmts injected in Step 1
        // (used by Step 1b and Step 2 to find the correct insert position)
        size_t insert_idx1 = 0;

        // ── Step 1: inject base constructor calls (in base declaration order) ──
        if (st->has_bases()) {
            // Build lookup: base raw_name → member_init_spec for this constructor
            std::unordered_map<std::string, const constructor::member_init_spec*> base_init_by_name;
            for (auto& mi : ctor.member_inits()) {
                if (mi.is_base_init) {
                    base_init_by_name[base_init_simple_name(mi.member_name)] = &mi;
                }
            }

            for (auto& bs : st->get_bases()) {
                if (!bs.base) continue;
                if (bs.is_virtual) {
                    // Virtual base: sub-object is __vbase_X__ (only constructed in most-derived class)
                    std::string vbase_name = "__vbase_" + bs.sanitised_name() + "__";
                    auto vbase_var_it = st->variables().find(vbase_name);
                    if (vbase_var_it == st->variables().end()) continue; // not the most-derived class
                    auto vbase_var = std::dynamic_pointer_cast<member_variable_definition>(vbase_var_it->second);
                    if (!vbase_var) continue;

                    std::vector<std::shared_ptr<expression>> args;
                    auto it = base_init_by_name.find(base_init_simple_name(bs.raw_name));
                    if (it != base_init_by_name.end()) {
                        for (auto& arg : it->second->args) args.push_back(arg->clone());
                    }
                    auto init_expr = constructor_invocation_expression::make_shared(vbase_var, args);
                    auto stmt = std::make_shared<expression_statement>(blck);
                    stmt->set_expression(init_expr);
                    auto fresh_pos = blck->begin();
                    std::advance(fresh_pos, insert_idx1);
                    blck->insert_statement(fresh_pos, stmt);
                    ++insert_idx1;
                } else {
                    // Non-virtual base: embedded as __base_X__
                    std::string subobj_name = "__base_" + bs.sanitised_name() + "__";
                    auto subobj_var_it = st->variables().find(subobj_name);
                    if (subobj_var_it == st->variables().end()) continue;
                    auto subobj_var = std::dynamic_pointer_cast<member_variable_definition>(subobj_var_it->second);
                    if (!subobj_var) continue;

                    std::vector<std::shared_ptr<expression>> args;
                    auto it = base_init_by_name.find(base_init_simple_name(bs.raw_name));
                    if (it != base_init_by_name.end()) {
                        for (auto& arg : it->second->args) args.push_back(arg->clone());
                    }
                    auto init_expr = constructor_invocation_expression::make_shared(subobj_var, args);
                    auto stmt = std::make_shared<expression_statement>(blck);
                    stmt->set_expression(init_expr);
                    auto fresh_pos = blck->begin();
                    std::advance(fresh_pos, insert_idx1);
                    blck->insert_statement(fresh_pos, stmt);
                    ++insert_idx1;
                }
            }
        }

        // ── Step 1b: inject transitively-collected virtual base constructor calls ──
        // For classes like D that collect virtual bases through non-virtual bases (e.g., D has
        // B,C as non-virtual bases where B,C each declare virtual A), D gets __vbase_A__ in its
        // layout. D must construct A. This is NOT handled by the direct-bases loop above (A is
        // not in D's direct base list). We inject these after all direct-base ctor calls.
        {
            auto vbases = st->get_all_virtual_base_structs();
            if (!vbases.empty()) {
                // Build lookup: virtual base short_name → member_init_spec for this constructor
                std::unordered_map<std::string, const constructor::member_init_spec*> vbase_init_by_name;
                for (auto& mi : ctor.member_inits()) {
                    if (mi.is_base_init) {
                        vbase_init_by_name[base_init_simple_name(mi.member_name)] = &mi;
                    }
                }

                // Insert position: after all ACTUALLY-injected direct-base ctor calls.
                // We use insert_idx1 which was incremented for each actual injection in Step 1.
                size_t insert_idx = insert_idx1;

                for (auto& vbase : vbases) {
                    std::string vbase_name = "__vbase_" + vbase->get_short_name() + "__";
                    // Only inject if this class actually owns the __vbase_X__ field
                    auto vbase_var_it = st->variables().find(vbase_name);
                    if (vbase_var_it == st->variables().end()) continue;
                    auto vbase_var = std::dynamic_pointer_cast<member_variable_definition>(vbase_var_it->second);
                    if (!vbase_var) continue;

                    std::vector<std::shared_ptr<expression>> args;
                    auto it = vbase_init_by_name.find(base_init_simple_name(vbase->get_short_name()));
                    if (it != vbase_init_by_name.end()) {
                        for (auto& arg : it->second->args) {
                            args.push_back(arg->clone());
                        }
                    }
                    auto init_expr = constructor_invocation_expression::make_shared(vbase_var, args);
                    auto stmt = std::make_shared<expression_statement>(blck);
                    stmt->set_expression(init_expr);
                    // Use index-based position to get a fresh iterator (avoids invalidation)
                    auto fresh_pos = blck->begin();
                    std::advance(fresh_pos, insert_idx);
                    blck->insert_statement(fresh_pos, stmt);
                    ++insert_idx;
                }
            }
        }

        // ── Step 2: inject member initializers (in member declaration order) ──
        if (!ctor.member_inits().empty()) {
            // Build a lookup map from member name to mem_init_spec
            std::unordered_map<std::string, const constructor::member_init_spec*> init_by_name;
            for (auto& mi : ctor.member_inits()) {
                if (!mi.is_base_init) init_by_name[mi.member_name] = &mi;
            }

            // Insert after the base-init calls (direct bases + collected virtual bases from step 1b)
            // Use insert_idx1 (actual injected count from Step 1) plus vbase count from Step 1b.
            size_t base_count = insert_idx1;
            // Add count of step 1b injected vbase stmts
            for (auto& vbase : st->get_all_virtual_base_structs()) {
                std::string vn = "__vbase_" + vbase->get_short_name() + "__";
                if (st->variables().count(vn)) ++base_count;
            }
            size_t insert_idx_step2 = base_count;

            for (auto& var_entry : st->variables()) {
                if (auto var = std::dynamic_pointer_cast<member_variable_definition>(var_entry.second)) {
                    // Skip synthetic fields
                    if (var->get_short_name() == "__parent__") continue;
                    if (var->get_short_name().rfind("__base_", 0) == 0) continue;
                    if (var->get_short_name().rfind("__vbptr_", 0) == 0) continue;
                    if (var->get_short_name().rfind("__vbase_", 0) == 0) continue;
                    if (var->get_short_name().rfind("__vptr", 0) == 0) continue;

                    auto it = init_by_name.find(var->get_short_name());
                    if (it == init_by_name.end()) continue;
                    const auto& mi = *it->second;

                    // Clone the args so each constructor gets its own independent copy
                    std::vector<std::shared_ptr<expression>> args;
                    args.reserve(mi.args.size());
                    for (auto& arg : mi.args) {
                        args.push_back(arg->clone());
                    }
                    auto init_expr = constructor_invocation_expression::make_shared(var, args);
                    auto stmt = std::make_shared<expression_statement>(blck);
                    stmt->set_expression(init_expr);
                    auto fresh_pos = blck->begin();
                    std::advance(fresh_pos, insert_idx_step2);
                    blck->insert_statement(fresh_pos, stmt);
                    ++insert_idx_step2;
                }
            }
        }
    }

    // For non-static inner struct constructors, the __parent__ field is stored
    // directly at IR level in implementation_generator::visit_function (constructor prologue).
    // No model-level injection needed here.

    visit_function(ctor);
}

void signature_resolver::visit_constructor(constructor& ctor) {
    visit_function(ctor);
}

/**
 * Resolve types in a constructor: parameter types, member-initializer expressions,
 * and body.
 *
 * Steps:
 *   1. Resolve parameter types (including default expression types).
 *   2. Resolve each member-initializer expression type.
 *   3. Match initializer expressions to constructor overloads for field types.
 *   4. Visit the constructor body block.
 */
void type_reference_resolver::visit_constructor(constructor& ctor) {
    auto st = ctor.get_owner();
    if (!st) {
        lex::opt_any_lexeme ctor_lexeme;
        if (auto ast_fd = ctor.get_ast_function_decl()) ctor_lexeme = lex::any_lexeme{ast_fd->name};
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F003), ctor_lexeme,
            "Internal error: constructor has no owner structure; "
            "every constructor must belong to a struct — this indicates a compiler bug");
    }

    // Step 1: Resolve parameter types (including default expression types)
    // Deleted constructors have no body and must never be called (enforced at resolution time).
    // Ensure _this_param exists (needed by check_constructor_visibility and type queries)
    // and resolve the formal parameter types so overload resolution can match argument types.
    if (ctor.is_deleted()) {
        if (ctor.is_member() && !ctor.is_static() && !ctor.get_this_parameter()) {
            ctor.create_this_parameter();
        }
        for (auto param : ctor.parameters()) {
            param->accept(*this);
        }
        return;
    }

    auto blck = ctor.get_block();

    // Intrinsic constructors: skip all member init injection — the body will be
    // generated entirely by the intrinsic codegen path in implementation_generator.
    if (get_intrinsic_name(ctor).has_value()) {
        visit_function(ctor);
        return;
    }

    // For compiler-generated copy constructor: do NOT inject model-level statements.
    // The memberwise copy will be emitted directly at IR level in implementation_generator::visit_function.
    if (ctor.is_copy_constructor() && ctor.is_compiler_generated()) {
        visit_function(ctor);
        return;
    }

    // Defaulted (-> default ;) constructors: they are compiler-generated and have no
    // user-provided body, but they still need to have member default-value initialisations
    // injected (same as any compiler-generated or user-written constructor without a
    // mem-initializer-list). Fall through to the standard injection logic below.
    // (do NOT return early here)

    // Step 2: Resolve each member-initializer expression type
    // Note : the statements for explicit member_inits and base inits were already injected by
    // symbol_resolver::visit_constructor (in struct member declaration order).
    // Here we insert fallback initialization statements for members NOT listed in the
    // mem-initializer-list, interleaved in declaration order.

    // Build the set of member names and base names with an explicit initializer
    std::unordered_set<std::string> explicit_init_names;
    for (auto& mi : ctor.member_inits()) {
        explicit_init_names.insert(mi.member_name);
    }

    // Walk member declaration order and insert fallback init for each unlisted member
    // at the correct position (interleaved with the already-injected explicit ones).
    // We maintain insert_pos which advances past each already-injected or newly-injected stmt.
    // Use an index counter to avoid iterator invalidation from vector reallocation.
    size_t insert_idx2 = 0;

    // Skip already-injected base init stmts
    for (auto& bs : st->get_bases()) {
        if (!bs.base) continue;
        if (bs.is_virtual) {
            std::string vbase_name = "__vbase_" + bs.sanitised_name() + "__";
            if (st->variables().count(vbase_name)) ++insert_idx2;
        } else {
            std::string sub_name = "__base_" + bs.sanitised_name() + "__";
            if (st->variables().count(sub_name)) ++insert_idx2;
        }
    }
    // Also skip vbase stmts injected in step 1b for transitively-collected virtual bases
    {
        auto vbases = st->get_all_virtual_base_structs();
        for (auto& vbase : vbases) {
            std::string vbase_name = "__vbase_" + vbase->get_short_name() + "__";
            if (st->variables().count(vbase_name)) {
                bool already_counted = false;
                for (auto& bs : st->get_bases()) {
                    if (bs.base && bs.is_virtual && bs.raw_name == vbase->get_short_name()) {
                        already_counted = true; break;
                    }
                }
                if (!already_counted) ++insert_idx2;
            }
        }
    }

    // Step 3: Match initializer expressions to constructor overloads for field types
    for (auto& var_entry : st->variables()) {
        if (auto var = std::dynamic_pointer_cast<member_variable_definition>(var_entry.second)) {
            // Skip __parent__ field — stored directly at IR level in constructor prologue
            if (var->get_short_name() == "__parent__") continue;
            // Skip base subobject fields — already handled above
            if (var->get_short_name().rfind("__base_", 0) == 0) continue;
            // Skip virtual base pointer fields — set at IR level
            if (var->get_short_name().rfind("__vbptr_", 0) == 0) continue;
            // Skip virtual base sub-object fields — handled in base injection loop above
            if (var->get_short_name().rfind("__vbase_", 0) == 0) continue;
            // Skip vptr fields — set at IR level
            if (var->get_short_name().rfind("__vptr", 0) == 0) continue;

            // Step 4: Visit the constructor body block
            if (explicit_init_names.count(var->get_short_name()) > 0) {
                // This member has an explicit initializer already in the block: skip past it
                ++insert_idx2;
            } else {
                // Not in the explicit list: use its own init_expr (if any)
                auto init_expr = var->get_init_expr();
                if (init_expr) {
                    // Clone so each constructor gets its own independent copy.
                    auto stmt = std::make_shared<expression_statement>(blck);
                    stmt->set_expression(init_expr->clone());
                    auto fresh_pos = blck->begin();
                    std::advance(fresh_pos, insert_idx2);
                    blck->insert_statement(fresh_pos, stmt);
                    ++insert_idx2;
                }
                // If no init_expr, zero-initialization covers it (done at IR level).
            }
        }
    }

    visit_function(ctor);
}

void signature_resolver::visit_destructor(destructor& dtor) {
    visit_function(dtor);
}

void type_reference_resolver::visit_destructor(destructor& dtor) {
    auto st = dtor.get_owner();
    if (!st) {
        lex::opt_any_lexeme dtor_lexeme;
        if (auto ast_fd = dtor.get_ast_function_decl()) dtor_lexeme = lex::any_lexeme{ast_fd->name};
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F004), dtor_lexeme,
            "Internal error: destructor has no owner structure; "
            "every destructor must belong to a struct — this indicates a compiler bug");
    }

    // Intrinsic destructors: skip all member/base destructor injection — the body will
    // be generated entirely by the intrinsic codegen path in implementation_generator.
    if (get_intrinsic_name(dtor).has_value()) {
        visit_function(dtor);
        return;
    }

    auto blck = dtor.get_block();
    // Insert calls to members' destructors at the END of the destructor block, in reverse declaration order.
    // Collect member variables that have a destructor (own members, not base subobjs)
    std::vector<std::shared_ptr<member_variable_definition>> dtor_members;
    for (auto& var_entry : st->variables()) {
        if (auto var = std::dynamic_pointer_cast<member_variable_definition>(var_entry.second)) {
            if (var->get_short_name() == "__parent__") continue;
            if (var->get_short_name().rfind("__base_", 0) == 0) continue;
            if (var->get_short_name().rfind("__vbptr_", 0) == 0) continue;
            if (var->get_short_name().rfind("__vbase_", 0) == 0) continue;
            if (var->get_short_name().rfind("__vptr", 0) == 0) continue;
            if (auto st_type = std::dynamic_pointer_cast<struct_type>(var->get_type())) {
                if (st_type->get_struct() && st_type->get_struct()->get_destructor()) {
                    dtor_members.push_back(var);
                }
            }
        }
    }
    // Insert destructor calls for own members in reverse order at end of block
    for (auto it = dtor_members.rbegin(); it != dtor_members.rend(); ++it) {
        (void)*it; // placeholder – IR generation handles this
    }

    // Insert base destructor calls in reverse base-declaration order
    // (bases are destroyed after own members, in reverse order of construction)
    // Placeholder: actual IR generation happens in implementation_generator.
    // We just record the intent; implementation_generator::visit_function handles it.

    visit_function(dtor);
}

//
// Static constructor
// Registers the static constructor with the global initializer function.
//

/**
 * Visit a static constructor: resolve mem-init list dependency names and body.
 *
 * Steps:
 *   1. For each static_dep_spec: resolve the target name to a structure or global variable.
 *   2. Visit the static constructor body block.
 */
void symbol_resolver::visit_static_constructor(static_constructor& sctor) {
    visit_function(sctor);

    // Step 1: For each static_dep_spec: resolve the target name to a structure or global variable
    // Resolve each dependency name declared in the mem-init list to a concrete model element.
    // Resolution is: name → structure (requires static ctor) OR global_variable_definition.
    // The scope walk starts from the owning structure and climbs to the root namespace.
    // This is the ONLY place where static_dep_spec names are resolved; the model itself
    // holds no resolution logic.
    auto owner = sctor.get_owner();
    if (!owner) return;

    // Step 2: Visit the static constructor body block
    auto start = std::dynamic_pointer_cast<element>(owner);

    for (auto& dep : sctor.mutable_member_inits()) {
        // Try to find a structure with this name in scope
        if (auto st = scope_lookup::lookup_structure(start, dep.name)) {
            dep.resolved = st;
            continue;
        }
        // Try to find a global variable with this name in scope
        if (auto var = scope_lookup::lookup_variable(start, dep.name)) {
            if (auto gv = std::dynamic_pointer_cast<global_variable_definition>(var)) {
                dep.resolved = gv;
                continue;
            }
        }
        // Not found — report error
        lex::opt_any_lexeme sctor_lexeme;
        if (auto ast_fd = sctor.get_ast_function_decl()) sctor_lexeme = lex::any_lexeme{ast_fd->name};
        throw_error(static_cast<unsigned int>(k::diag::symbol_diag::ERR_STATIC_CTOR_INIT_FAILED), sctor_lexeme,
            "In static constructor '{}': dependency '{}' in the mem-init list "
            "does not refer to any known struct or global variable in scope",
            {sctor.get_fq_name(), dep.name});
    }
}

void signature_resolver::visit_static_constructor(static_constructor& sctor) {
    visit_function(sctor);
    // Do NOT register with global constructor — that happens in the full pass.
}

void type_reference_resolver::visit_static_constructor(static_constructor& sctor) {
    visit_function(sctor);

    // Register this static constructor with the unit's global constructor function.
    // The actual call order is determined later by init_order_resolver.
    sctor.ancestor<unit>()->get_global_constructor_function().add_static_constructor(sctor.shared_as<static_constructor>());
}

//
// Static destructor
// No direct registration needed: init_order_resolver derives the destruction order
// as the exact reverse of the construction order.
//

void symbol_resolver::visit_static_destructor(static_destructor& sdtor) {
    visit_function(sdtor);
}

void signature_resolver::visit_static_destructor(static_destructor& sdtor) {
    visit_function(sdtor);
}

void type_reference_resolver::visit_static_destructor(static_destructor& sdtor) {
    visit_function(sdtor);
    // Registration in the global destructor function is handled by init_order_resolver.
}

//
// Global constructor function
// This generate the unique global constructor function (if needed) and register it to llvm.global_ctors
// Note: Global constructor is processed at the end of the unit (but before global destructor)
//
void type_reference_resolver::visit_global_constructor_function(global_constructor_function& func) {
    const auto& items = func.get_ordered_items();
    if (items.empty()) return;

    auto blck = func.get_block();
    // Only global variable initializations need a model-level statement (for type resolution);
    // static constructor calls are emitted directly at IR level.
    for (auto& item : items) {
        if (auto gv = std::get_if<std::shared_ptr<global_variable_definition>>(&item)) {
            auto init_expr = (*gv)->get_init_expr();
            if (init_expr) {
                auto stmt = std::make_shared<expression_statement>(blck);
                stmt->set_expression(init_expr);
                blck->append_statement(stmt);
            }
        }
    }
    visit_function(func);
}

/**
 * Generate LLVM IR for the global constructor function (__k_global_ctor).
 *
 * Steps:
 *   1. Get or create the LLVM function.
 *   2. Create entry basic block.
 *   3. Iterate ordered init items (static constructors and global variables).
 *   4. For static constructors: emit a call to the static constructor function.
 *   5. For global variables with init expressions: evaluate and store.
 *   6. Emit ret void.
 */
void implementation_generator::visit_global_constructor_function(global_constructor_function& func) {
    const auto& items = func.get_ordered_items();
    if (items.empty()) return;

    // Step 1: Create the LLVM function with InternalLinkage.
    // InternalLinkage is required because this function is only called through
    // .init_array entries; ExternalLinkage would cause ELF symbol interposition
    // across shared libraries (e.g. libk.so and the user module both producing
    // a __K_global_init symbol, making the dynamic linker call one of them twice).
    {
        llvm::FunctionType* ft = llvm::FunctionType::get(llvm::Type::getVoidTy(**_context), false);
        llvm::Function* llvm_func = llvm::Function::Create(
            ft, llvm::Function::InternalLinkage, func.get_mangled_name(), *_context->_module);
        _context->_functions.insert({func.shared_as<function>(), llvm_func});
    }

    // Generate the function body (global variable constructor-invocation statements are in the block).
    visit_function(func);

    auto it_func = _context->_functions.find(func.shared_as<function>());
    if (it_func == _context->_functions.end()) {
        lex::opt_any_lexeme fn_lexeme;
        if (auto ast_fd = func.get_ast_function_decl()) fn_lexeme = lex::any_lexeme{ast_fd->name};
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F01C), fn_lexeme,
            "Internal error: global constructor function not found in LLVM function table; "
            "the declaration pass may not have run");
    }

    // Emit static constructor calls in order, interleaved with global-variable inits.
    // Global variable init expressions are already emitted by visit_function in order from the block.
    // We need to insert static_ctor calls at the right position in the IR.
    // Strategy: build an ordered list of static ctor calls only, then insert them
    // just before the ret terminator (after all variable inits).
    // NOTE: variable inits are already in the block (emitted by visit_function).
    //       Static ctors are emitted in their correct order relative to each other
    //       and relative to variable inits by placing them just before the final ret.
    //       The unified ordering ensures that all dependencies are respected.
    // The IR order within the function body therefore is:
    //   [var-init calls in block order] then [static ctor calls before ret]
    // Because init_order_resolver ensures the ordering is correct, and the block was built
    // with vars in dependency order, static ctors will logically precede their dependent vars.
    // BUT: to achieve FULL correct interleaving at IR level, we use a different approach:
    // We collect static ctor calls from items in order and insert them AFTER their position
    // in the block by using move-instruction sequencing.
    // For simplicity and correctness (since ordering is resolved), we emit static ctor calls
    // in the order they appear in items, just before the terminator.

    llvm::Function* llvm_func = it_func->second;
    llvm::BasicBlock& last_bb = llvm_func->back();
    llvm::IRBuilder<> ctor_builder(&last_bb, last_bb.getTerminator()->getIterator());

    // Step 2: Create entry basic block
    // Walk ordered items: for each static_constructor, emit a call just before the terminator.
    // Global variable inits are already emitted by visit_function in order from the block.
    // To achieve interleaved ordering (static ctors and var inits mixed), we collect
    // all variable-init instructions from the block and reorder them with the static calls.
    // Simpler approach: since visit_function already emitted var-init calls in the block
    // in the order appended to the block (which matches items order for gv), we only
    // need to insert static ctor calls. But they must appear BETWEEN variable inits if needed.
    // Full interleaving: rebuild the entire function IR in items order.
    // For correctness: emit all var-init calls from the block already (done), then
    // append static ctor calls at the end of the entry block before ret.
    // This is correct IF the unified ordering places all static ctors BEFORE all global vars
    // that depend on them — which init_order_resolver guarantees.
    // The IR order within the function body therefore is:
    //   [var-init calls in block order] then [static ctor calls before ret]
    // Because init_order_resolver ensures the ordering is correct, and the block was built
    // with vars in dependency order, static ctors will logically precede their dependent vars.
    // BUT: to achieve FULL correct interleaving at IR level, we use a different approach:
    // We collect static ctor calls from items in order and insert them AFTER their position
    // in the block by using move-instruction sequencing.
    // For simplicity and correctness (since ordering is resolved), we emit static ctor calls
    // in the order they appear in items, just before the terminator.

    // Step 3: Iterate ordered init items (static constructors and global variables)
    for (auto& item : items) {
        if (auto sc = std::get_if<std::shared_ptr<static_constructor>>(&item)) {
            auto sctor_it = _context->_functions.find(*sc);
            if (sctor_it == _context->_functions.end()) continue;
            ctor_builder.CreateCall(sctor_it->second, {});
        }
    }

    // Step 4: For static constructors: emit a call to the static constructor function
    // Register the global constructor function with the runtime
    llvm::appendToGlobalCtors(get_module(), llvm_func, 65535);
}


//
// Global destructor function
// This generates the unique global destructor function (if needed) and registers it to llvm.global_dtors
// Destruction order is the exact REVERSE of the construction order.
//

// type_reference_resolver::visit_global_destructor_function
// -----------------------------------------------------------
// Prepares the global destructor function for IR generation.
// Unlike the global constructor function, the destructor body contains no
// model-level statements (all calls are emitted directly at IR level).
//
// Steps:
//  1. Early-exit if there are no ordered items and no standalone static dtors.
//  2. Delegate to visit_function so the function's metadata (name, return type, etc.)
//     is resolved.  The body block is empty at model level; actual calls are
//     inserted directly in implementation_generator::visit_global_destructor_function.
void type_reference_resolver::visit_global_destructor_function(global_destructor_function& func) {
    const auto& items = func.get_ordered_items();
    const auto& standalone = func.get_standalone_static_dtors();
    if (items.empty() && standalone.empty()) return;
    visit_function(func);
}

// implementation_generator::visit_global_destructor_function
// ------------------------------------------------------------
// Emits the IR body of the global destructor function and registers it with
// the LLVM global_dtors table.
// Items are processed in the order stored in the model (reverse-construction order,
// set by init_order_resolver).
//
// Steps:
//  1. Early-exit if there are no items and no standalone static destructors.
//  2. Check whether there is any real work: at least one standalone static dtor,
//     a static constructor whose struct has a static destructor, or a global
//     variable whose type has a struct destructor.  Early-exit if nothing to do.
//  3. Create a new void() llvm::Function with ExternalLinkage and the mangled name,
//     register it in the context, and create the entry BasicBlock.
//  4. First: emit calls to standalone static destructors (structs that have a
//     static destructor but no static constructor).
//  5. Then: walk the ordered items (reverse-construction order):
//     - For a static_constructor item: look up the owning struct's static destructor
//       and emit a direct call to it.
//     - For a global_variable_definition item: if the variable's type is a struct
//       with a destructor, GEP to the global variable and emit a destructor call.
//  6. Emit a ret-void terminator.
//  7. Verify the function with llvm::verifyFunction.
//  8. Register the function with the LLVM global_dtors table at priority 65535
//     via llvm::appendToGlobalDtors, ensuring it runs at program shutdown.
/**
 * Generate LLVM IR for the global destructor function (__k_global_dtor).
 *
 * Steps:
 *   1. Get or create the LLVM function.
 *   2. Create entry basic block.
 *   3. Iterate ordered finit items (reverse construction order).
 *   4. For static destructors: emit a call to the static destructor function.
 *   5. For global variables with destructors: emit destructor calls.
 *   6. Emit standalone static destructors (no matching static constructor).
 *   7. Emit ret void.
 */
void implementation_generator::visit_global_destructor_function(global_destructor_function& func) {
    // The destructor function holds items in REVERSE construction order
    // (set by init_order_resolver). We iterate forward through them.
    const auto& items = func.get_ordered_items();
    const auto& standalone_sdtors = func.get_standalone_static_dtors();
    if (items.empty() && standalone_sdtors.empty()) return;

    // Check if there is anything to do (struct dtors or static dtors)
    bool has_work = !standalone_sdtors.empty();
    if (!has_work) {
        for (auto& item : items) {
            if (auto sc = std::get_if<std::shared_ptr<static_constructor>>(&item)) {
                // Corresponding static destructor
                auto owner = (*sc)->get_owner();
                if (owner && owner->get_static_destructor()) { has_work = true; break; }
            } else if (auto gv = std::get_if<std::shared_ptr<global_variable_definition>>(&item)) {
                if (auto st_type = std::dynamic_pointer_cast<struct_type>((*gv)->get_type())) {
                    if (st_type->get_struct() && st_type->get_struct()->get_destructor()) { has_work = true; break; }
                }
            }
        }
    }
    if (!has_work) return;

    // Step 1: Get or create the LLVM function
    // Generate a void() function for the global destructor.
    // Use InternalLinkage — this function is only called through .fini_array;
    // ExternalLinkage would cause symbol interposition across shared libraries.
    llvm::FunctionType* func_type = llvm::FunctionType::get(llvm::Type::getVoidTy(**_context), false);
    llvm::Function* llvm_func = llvm::Function::Create(func_type, llvm::Function::InternalLinkage,
                                                        func.get_mangled_name(), *_context->_module);
    _context->_functions.insert({func.shared_as<function>(), llvm_func});

    // Step 2: Create entry basic block
    llvm::BasicBlock* entry = llvm::BasicBlock::Create(**_context, "entry", llvm_func);
    llvm::IRBuilder<> dtor_builder(entry);

    // First: emit standalone static destructors (structs with ~S() but no S()).
    for (auto& sdtor : standalone_sdtors) {
        auto sdtor_it = _context->_functions.find(sdtor->shared_as<k::model::function>());
        if (sdtor_it == _context->_functions.end()) continue;
        dtor_builder.CreateCall(sdtor_it->second, {});
    }

    // Step 3: Iterate ordered finit items (reverse construction order)
    // Then: emit finalization in the order stored in items (reverse-construction order).
    for (auto& item : items) {
        if (auto sc = std::get_if<std::shared_ptr<static_constructor>>(&item)) {
            auto owner = (*sc)->get_owner();
            if (!owner) continue;
            auto sdtor = owner->get_static_destructor();
            if (!sdtor) continue;
            auto sdtor_it = _context->_functions.find(sdtor->shared_as<k::model::function>());
            if (sdtor_it == _context->_functions.end()) continue;
            dtor_builder.CreateCall(sdtor_it->second, {});
        } else if (auto gv = std::get_if<std::shared_ptr<global_variable_definition>>(&item)) {
            auto st_type = std::dynamic_pointer_cast<struct_type>((*gv)->get_type());
            if (!st_type) continue;
            auto st = st_type->get_struct();
            if (!st || !st->get_destructor()) continue;
            auto var_it = _context->_global_vars.find(*gv);
            if (var_it == _context->_global_vars.end()) continue;
            llvm::GlobalVariable* global_var = var_it->second;
            auto dtor_it = _context->_functions.find(st->get_destructor()->shared_as<function>());
            if (dtor_it == _context->_functions.end()) continue;
            dtor_builder.CreateCall(dtor_it->second, {global_var});
        }
    }

    // Step 4: For static destructors: emit a call to the static destructor function
    // Step 7: Emit ret void
    dtor_builder.CreateRetVoid();
    llvm::verifyFunction(*llvm_func);
    llvm::appendToGlobalDtors(get_module(), llvm_func, 65535);
}

//
// Global main function
// This generate the main entry point proxy code
//

// type_reference_resolver::visit_global_main_function
// -----------------------------------------------------
// Synthesizes the C-ABI "main" entry-point proxy that wraps the user-defined
// 'main' function.  This proxy is what the linker and OS call at startup.
//
// Steps:
//  1. Validate that the user's 'main' function takes no parameters (parameters
//     are not yet supported); throw a compile error if parameters are present.
//  2. Resolve the 'int' primitive type from the context.
//  3. Configure the proxy function metadata:
//     - Assign the well-known name "main" (C-ABI entry point).
//     - Set return type to 'int'.
//     - Add 'argc' (int) and 'argv' (unsigned char**) parameters.
//  4. Build the proxy body:
//     - If the user's 'main' returns a value:
//       a. Create a function_invocation_expression calling the user function.
//       b. Adapt/cast the return value to 'int' (implicit cast if needed).
//       c. Wrap in a return_statement.
//     - If the user's 'main' returns void:
//       a. Create an expression_statement for the call (result discarded).
//       b. Append it to the block.
//       c. Append a return_statement returning the integer literal 0.
//  5. The block is now complete; visit_function will be called later in the
//     normal visitor flow to resolve types within the synthesized body.
/**
 * Resolve the global main function: wrap the user's main() into the runtime entry point.
 *
 * Steps:
 *   1. Find the user-defined main() function in the unit.
 *   2. Build a wrapper block that calls global_ctor, user main, global_dtor.
 *   3. Resolve all expressions in the wrapper block.
 */
void type_reference_resolver::visit_global_main_function(global_main_function& main_func) {

    std::vector<std::shared_ptr<expression>> args;

    // Step 1: Find the user-defined main() function in the unit
    // Look at the compatible prototypes
    // TODO Add a better method prototype compatibility checking/searching
    if (main_func.get_real_func().has_parameter()) {
        lex::opt_any_lexeme main_lexeme;
        if (auto ast_fd = main_func.get_real_func().get_ast_function_decl()) main_lexeme = lex::any_lexeme{ast_fd->name};
        throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_MAIN_WRONG_RETURN_TYPE), main_lexeme,
            "'main' function does not support parameters yet; "
            "declare it as 'func main() : int' or 'func main() : void'");
    }

    auto int_type = _context->from_type(primitive_type::INT);

    main_func.assign_name(name(true, "main"));
    main_func.set_return_type(int_type);
    main_func.append_parameter("argc", int_type);
    main_func.append_parameter("argv", _context->from_type(primitive_type::UNSIGNED_CHAR)->get_pointer()->get_pointer());

    // Step 2: Build a wrapper block that calls global_ctor, user main, global_dtor
    auto main_block = main_func.get_block();
    auto ret_stmt = std::make_shared<model::return_statement>(main_block);

    // Step 3: Resolve all expressions in the wrapper block
    std::shared_ptr<expression> invoke = function_invocation_expression::make_shared(main_func.get_real_func().shared_as<function>(), args);

    // Annotate with DIRECT dispatch_info — the real 'main' function is always
    // a direct call (not virtual).  This synthetic node bypasses the normal
    // type_reference_resolver path, so we set the annotation manually.
    {
        virtual_dispatch_info di;
        di.kind = virtual_dispatch_info::dispatch_kind::DIRECT;
        std::dynamic_pointer_cast<function_invocation_expression>(invoke)->set_dispatch_info(std::move(di));
    }

    if (main_func.get_real_func().has_return_type()) {
        // Cast invocation result to int
        auto cast = adapt_type(invoke, int_type);
        if(!cast) {
            lex::opt_any_lexeme main_lexeme;
            if (auto ast_fd = main_func.get_real_func().get_ast_function_decl()) main_lexeme = lex::any_lexeme{ast_fd->name};
            throw_error(static_cast<unsigned int>(k::diag::operator_diag::ERR_MAIN_WRONG_PARAMS), main_lexeme,
                "'main' function return type '{}' cannot be implicitly cast to 'int'; "
                "the return type must be 'int', 'void', or a type castable to 'int'",
                {main_func.get_real_func().get_return_type() ? main_func.get_real_func().get_return_type()->to_string() : "?"});
        } else if(cast != invoke) {
            // Casted, assign casted expression as return expr.
            invoke = cast;
        } else {
            // Compatible type, no need to cast.
        }
        // Return casted result
        ret_stmt->set_expression(invoke);
        main_func.get_block()->append_statement(ret_stmt);
    } else {
        // Create statement for this invocation
        auto call_stmt = std::make_shared<model::expression_statement>(main_block);
        call_stmt->set_expression(invoke);
        main_func.get_block()->append_statement(call_stmt);
        // Create return statement with returning 0
        ret_stmt->set_expression(value_expression::from_value(0));
        main_func.get_block()->append_statement(ret_stmt);
    }
}

} // namespace k::model::gen
