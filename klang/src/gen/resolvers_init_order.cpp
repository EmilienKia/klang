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
#include "resolvers_init_order.hpp"
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
// init_order_resolver


// =============================================================================
// init_order_resolver — Implementation
// =============================================================================

std::string init_order_resolver::node_label(const node_t& n) {
    if (auto sc = std::get_if<std::shared_ptr<static_constructor>>(&n)) {
        return "static_ctor(" + (*sc)->get_fq_name() + ")";
    } else if (auto gv = std::get_if<std::shared_ptr<global_variable_definition>>(&n)) {
        return "global_var(" + (*gv)->get_fq_name() + ")";
    }
    return "<unknown>";
}

lex::opt_any_lexeme init_order_resolver::node_lexeme(const node_t& n) {
    if (auto sc = std::get_if<std::shared_ptr<static_constructor>>(&n)) {
        if (auto decl = (*sc)->get_ast_function_decl()) return lex::any_lexeme{decl->name};
    } else if (auto gv = std::get_if<std::shared_ptr<global_variable_definition>>(&n)) {
        if (auto init_expr = (*gv)->get_init_expr()) return init_expr->first_lexeme();
    }
    return std::nullopt;
}

/**
 * Recursively walk an expression tree and collect:
 *  - all global_variable_definition targets reached via symbol_expression
 *  - all struct_types referenced (for constructor invocations and variable types)
 *
 * visited_funcs prevents infinite recursion when traversing function bodies.
 */
void init_order_resolver::collect_global_deps_from_expr(
        const std::shared_ptr<expression>& expr,
        std::vector<std::shared_ptr<global_variable_definition>>& out_globals,
        std::vector<std::shared_ptr<struct_type>>&               out_struct_types,
        std::unordered_set<const function*>&                     visited_funcs)
{
    if (!expr) return;

    // symbol_expression — may refer to a global variable
    if (auto sym = std::dynamic_pointer_cast<symbol_expression>(expr)) {
        if (sym->is_variable_def()) {
            if (auto gv = std::dynamic_pointer_cast<global_variable_definition>(sym->get_variable_def())) {
                out_globals.push_back(gv);
            }
        }
        return;
    }

    // constructor_invocation_expression — struct type dependency + dig into ctor body
    if (auto cie = std::dynamic_pointer_cast<constructor_invocation_expression>(expr)) {
        auto ctor = cie->get_constructor();
        if (ctor) {
            auto owner = ctor->get_owner();
            if (owner && owner->get_struct_type()) {
                out_struct_types.push_back(owner->get_struct_type());
            }
            // Recurse into constructor body
            if (visited_funcs.insert(ctor.get()).second) {
                if (auto blk = ctor->get_block()) {
                    for (auto& stmt : blk->get_statements()) {
                        if (auto es = std::dynamic_pointer_cast<expression_statement>(stmt)) {
                            collect_global_deps_from_expr(es->get_expression(), out_globals, out_struct_types, visited_funcs);
                        }
                    }
                }
            }
        }
        for (auto& arg : cie->arguments()) {
            collect_global_deps_from_expr(arg, out_globals, out_struct_types, visited_funcs);
        }
        return;
    }

    // function_invocation_expression — recurse into callee body
    if (auto fie = std::dynamic_pointer_cast<function_invocation_expression>(expr)) {
        // Recurse into arguments first
        for (auto& arg : fie->arguments()) {
            collect_global_deps_from_expr(arg, out_globals, out_struct_types, visited_funcs);
        }
        // Recurse into callee body if it's a symbol_expression resolving to a function
        auto callee_sym = std::dynamic_pointer_cast<symbol_expression>(fie->callee_expr());
        if (callee_sym && callee_sym->is_function()) {
            auto fn = callee_sym->get_function();
            if (fn && visited_funcs.insert(fn.get()).second) {
                if (auto blk = fn->get_block()) {
                    for (auto& stmt : blk->get_statements()) {
                        if (auto es = std::dynamic_pointer_cast<expression_statement>(stmt)) {
                            collect_global_deps_from_expr(es->get_expression(), out_globals, out_struct_types, visited_funcs);
                        } else if (auto rs = std::dynamic_pointer_cast<return_statement>(stmt)) {
                            if (rs->get_expression()) {
                                collect_global_deps_from_expr(rs->get_expression(), out_globals, out_struct_types, visited_funcs);
                            }
                        }
                    }
                }
            }
        }
        return;
    }

    // unary_expression — recurse into sub_expr
    if (auto ue = std::dynamic_pointer_cast<unary_expression>(expr)) {
        collect_global_deps_from_expr(ue->sub_expr(), out_globals, out_struct_types, visited_funcs);
        return;
    }

    // binary_expression — recurse into left and right
    if (auto be = std::dynamic_pointer_cast<binary_expression>(expr)) {
        collect_global_deps_from_expr(be->left(), out_globals, out_struct_types, visited_funcs);
        collect_global_deps_from_expr(be->right(), out_globals, out_struct_types, visited_funcs);
        return;
    }

    // member_of_object_expression — recurse into sub_expr
    if (auto moe = std::dynamic_pointer_cast<member_of_object_expression>(expr)) {
        collect_global_deps_from_expr(moe->sub_expr(), out_globals, out_struct_types, visited_funcs);
        return;
    }

    // value_expression — no dependencies
}

/**
 * Collect dependencies of a global_variable_definition node.
 *
 * Steps:
 *   1. Rule 3: if GV has a struct type with a static constructor, depend on that SC.
 *   2. Rules 4-6: inspect init expression for global variable references and struct types
 *      from constructor invocations and function call bodies.
 */
void init_order_resolver::collect_deps_for_global(
        const std::shared_ptr<global_variable_definition>& gv,
        const std::unordered_map<const static_constructor*, size_t>& sctor_index,
        const std::unordered_map<const global_variable_definition*, size_t>& gv_index,
        std::vector<std::vector<size_t>>& adj,
        size_t my_idx)
{
    // Step 1: Rule 3: if GV has a struct type with a static constructor, depend on that SC
    // Rule 3: if GV has a struct type with a static constructor, it depends on that SC
    if (auto st_type = std::dynamic_pointer_cast<struct_type>(gv->get_type())) {
        if (auto st = st_type->get_struct()) {
            if (auto sc = st->get_static_constructor()) {
                auto it = sctor_index.find(sc.get());
                if (it != sctor_index.end()) {
                    adj[it->second].push_back(my_idx); // sc → gv (sc must come before gv)
                }
            }
        }
    }

    // Step 2: Rules 4-6: inspect init expression for global variable references and struct types from construct...
    // Rules 4–6: inspect init expression
    auto init_expr_base = gv->get_init_expr();
    if (!init_expr_base) return;
    auto init_expr = std::dynamic_pointer_cast<constructor_invocation_expression>(init_expr_base);
    if (!init_expr) return; // owner or other non-ctor init — no global deps to track

    std::vector<std::shared_ptr<global_variable_definition>> dep_globals;
    std::vector<std::shared_ptr<struct_type>> dep_structs;
    std::unordered_set<const function*> visited;

    for (size_t i = 0; i < init_expr->size(); ++i) {
        collect_global_deps_from_expr(init_expr->argument(i), dep_globals, dep_structs, visited);
    }

    // Rule 4: direct global variable references
    for (auto& dep_gv : dep_globals) {
        if (dep_gv.get() == gv.get()) continue; // skip self
        auto it = gv_index.find(dep_gv.get());
        if (it != gv_index.end()) {
            adj[it->second].push_back(my_idx); // dep_gv → gv
        }
    }

    // Rule 5: struct type from constructors → their SC must run first
    for (auto& dep_st : dep_structs) {
        if (auto st = dep_st->get_struct()) {
            if (auto sc = st->get_static_constructor()) {
                auto it = sctor_index.find(sc.get());
                if (it != sctor_index.end()) {
                    adj[it->second].push_back(my_idx); // sc → gv
                }
            }
        }
    }
}

/**
 * Collect dependencies of a static_constructor node.
 *
 * Steps:
 *   1. Rule 1: explicit deps from mem-init list (already resolved by symbol_resolver).
 *   2. Implicit: static constructors of base classes must run before this one.
 *   3. Rule 2 (handled elsewhere): static members of the owning struct depend on this SC.
 */
void init_order_resolver::collect_deps_for_sctor(
        const std::shared_ptr<static_constructor>& sctor,
        const std::unordered_map<const static_constructor*, size_t>& sctor_index,
        const std::unordered_map<const global_variable_definition*, size_t>& gv_index,
        std::vector<std::vector<size_t>>& adj,
        size_t my_idx)
{
    // Step 1: Rule 1: explicit deps from mem-init list (already resolved by symbol_resolver)
    // Rule 1: explicit deps from static constructor mem-init list.
    // `static S() : A(), gvar() {}`
    // By this point every static_dep_spec has already been resolved to a concrete model
    // element by symbol_resolver::visit_static_constructor.  We simply read the resolved
    // variant — no name lookup is performed here.
    for (auto& dep : sctor->member_inits()) {
        if (!dep.is_resolved()) {
            // Should not happen: symbol_resolver would have thrown already.
            // Guard against stale data just in case.
            continue;
        }

        if (dep.is_structure()) {
            auto dep_st = dep.get_structure();
            if (auto sc = dep_st->get_static_constructor()) {
                auto it = sctor_index.find(sc.get());
                if (it != sctor_index.end()) {
                    adj[it->second].push_back(my_idx); // SC(dep_st) → SC(sctor)
                }
                // If dep_st has no static ctor, no ordering constraint needed.
            }
        } else if (dep.is_global_variable()) {
            auto dep_gv = dep.get_global_variable();
            auto it = gv_index.find(dep_gv.get());
            if (it != gv_index.end()) {
                adj[it->second].push_back(my_idx); // dep_gv → SC(sctor)
            }
        }
    }

    // Step 2: Implicit: static constructors of base classes must run before this one
    // Implicit: static constructors of BASE CLASSES must run BEFORE this one.
    // (A derived struct's static constructor depends on its bases' static constructors.)
    if (auto owner = sctor->get_owner()) {
        for (auto& bs : owner->get_bases()) {
            if (!bs.base) continue;
            if (auto base_sc = bs.base->get_static_constructor()) {
                auto it = sctor_index.find(base_sc.get());
                if (it != sctor_index.end()) {
                    adj[it->second].push_back(my_idx); // SC(base) → SC(derived)
                }
            }
        }
    }

    // Step 3: Rule 2 (handled elsewhere): static members of the owning struct depend on this SC
    // Rule 2 (implicit): static members of owner struct must be initialized AFTER this SC.
    // This is handled in collect_deps_for_global (rule 3): each static member variable
    // of struct S depends on SC(S).
}

/**
 * Compute the unified ordered init/finit sequence using topological sort.
 *
 * Steps:
 *   1. Collect standalone static destructors (no matching static constructor).
 *   2. Build node index: [static constructors | global variables].
 *   3. Build dependency graph (adjacency list) using collect_deps_for_sctor/global.
 *   4. Kahn's topological sort (BFS) to produce construction order.
 *   5. Detect cycles (if topo sort didn't consume all nodes).
 *   6. Store construction order on global_constructor_function,
 *      reverse as destruction order on global_destructor_function.
 */
void init_order_resolver::resolve() {
    trace("[init_order_resolver::resolve] begin");
    auto& ctor_func = _unit.get_global_constructor_function();
    auto& dtor_func = _unit.get_global_destructor_function();

    // Step 1: Collect standalone static destructors (no matching static constructor)
    const auto& raw_sctors = ctor_func.get_static_constructors();
    const auto& raw_gvars  = ctor_func.get_global_variables();

    // Collect static destructors that have NO matching static constructor.
    // These structs need finalization but no initialization.
    // We gather them by scanning all structures in the unit.
    std::vector<std::shared_ptr<static_destructor>> standalone_sdtors;
    {
        std::unordered_set<const aggregate*> has_sctor;
        for (auto& sc : raw_sctors) {
            if (auto owner = sc->get_owner()) has_sctor.insert(owner.get());
        }
        // Walk the root namespace recursively
        std::function<void(const ns&)> scan_ns = [&](const ns& n) {
            for (auto& child : n.get_children()) {
                if (auto agg = std::dynamic_pointer_cast<aggregate>(child)) {
                    if (!has_sctor.count(agg.get())) {
                        if (auto sdtor = agg->get_static_destructor()) {
                            standalone_sdtors.push_back(sdtor);
                        }
                    }
                } else if (auto sub_ns = std::dynamic_pointer_cast<ns>(child)) {
                    scan_ns(*sub_ns);
                }
            }
        };
        if (auto root = _unit.get_root_namespace()) {
            scan_ns(*root);
        }
    }

    // Nothing to do if no items at all
    if (raw_sctors.empty() && raw_gvars.empty() && standalone_sdtors.empty()) return;

    // -------------------------------------------------------------------------
    // Build index: node pointer → index in combined nodes array
    // Nodes layout: [sctors 0..S-1] [gvars S..S+G-1]
    // -------------------------------------------------------------------------
    const size_t S = raw_sctors.size();
    const size_t G = raw_gvars.size();
    const size_t N = S + G;

    // Step 2: Build node index: [static constructors | global variables]
    std::unordered_map<const static_constructor*, size_t>           sctor_index;
    std::unordered_map<const global_variable_definition*, size_t>   gv_index;

    for (size_t i = 0; i < S; ++i) sctor_index[raw_sctors[i].get()] = i;
    for (size_t i = 0; i < G; ++i) gv_index[raw_gvars[i].get()]     = S + i;

    // Build combined node list
    std::vector<node_t> nodes;
    nodes.reserve(N);
    for (auto& sc : raw_sctors) nodes.push_back(sc);
    for (auto& gv : raw_gvars)  nodes.push_back(gv);

    // -------------------------------------------------------------------------
    // Build adjacency list: adj[i] = list of nodes that must come AFTER node i
    // (i.e. i is a dependency of adj[i][j])
    // -------------------------------------------------------------------------
    std::vector<std::vector<size_t>> adj(N);

    for (size_t i = 0; i < S; ++i) {
        collect_deps_for_sctor(raw_sctors[i], sctor_index, gv_index, adj, i);
    }
    for (size_t i = 0; i < G; ++i) {
        collect_deps_for_global(raw_gvars[i], sctor_index, gv_index, adj, S + i);
    }

    // Step 3: Build dependency graph (adjacency list) using collect_deps_for_sctor/global
    // Deduplicate adjacency lists
    for (auto& list : adj) {
        std::sort(list.begin(), list.end());
        list.erase(std::unique(list.begin(), list.end()), list.end());
    }

    // Step 4: Kahn's topological sort (BFS) to produce construction order
    // -------------------------------------------------------------------------
    // Kahn's topological sort (BFS)
    // -------------------------------------------------------------------------
    std::vector<size_t> in_degree(N, 0);
    for (size_t i = 0; i < N; ++i) {
        for (size_t j : adj[i]) {
            if (j < N) ++in_degree[j];
        }
    }

    std::queue<size_t> ready;
    for (size_t i = 0; i < N; ++i) {
        if (in_degree[i] == 0) ready.push(i);
    }

    std::vector<node_t> construction_order;
    construction_order.reserve(N);

    while (!ready.empty()) {
        size_t cur = ready.front(); ready.pop();
        construction_order.push_back(nodes[cur]);
        for (size_t next : adj[cur]) {
            if (--in_degree[next] == 0) ready.push(next);
        }
    }

    // Step 5: Detect cycles (if topo sort didn't consume all nodes)
    // -------------------------------------------------------------------------
    // Cycle detection
    // -------------------------------------------------------------------------
    if (construction_order.size() < N) {
        // Collect all nodes still in a cycle (in_degree > 0)
        std::string cycle_members;
        lex::opt_any_lexeme cycle_lexeme;
        for (size_t i = 0; i < N; ++i) {
            if (in_degree[i] > 0) {
                if (!cycle_members.empty()) cycle_members += ", ";
                cycle_members += node_label(nodes[i]);
                if (!cycle_lexeme) cycle_lexeme = node_lexeme(nodes[i]);
            }
        }
        throw_error(static_cast<unsigned int>(k::diag::symbol_diag::ERR_INIT_ORDER_CYCLE), cycle_lexeme,
            "Cycle detected in global initialization dependency graph. "
            "The following items form a circular dependency: {}",
            {cycle_members});
    }

    // Step 6: Store construction order on global_constructor_function, reverse as destruction order on global_d...
    // -------------------------------------------------------------------------
    // Store construction order into the constructor function,
    // and the REVERSE as the destruction order into the destructor function.
    // Standalone static destructors (no matching static ctor) are appended to
    // the destruction order at the front (they run first during finalization,
    // i.e. they were logically "initialized last" — but have no init step).
    // -------------------------------------------------------------------------
    ctor_func.set_ordered_items(construction_order);

    std::vector<node_t> destruction_order(construction_order.rbegin(), construction_order.rend());
    // Prepend standalone static dtors: they finalize first (no ordering constraint
    // relative to items in the main graph since they have no static ctor node).
    // Use static_constructor as a sentinel carrier — we wrap them as init_items
    // containing the static_constructor of the same struct if it exists.
    // Since these are standalone (no static ctor), we emit them via a special path
    // in implementation_generator: we store them as a separate list on dtor_func.
    dtor_func.set_ordered_items(std::move(destruction_order));
    dtor_func.add_standalone_static_dtors(standalone_sdtors);
}




} // namespace k::model::gen
