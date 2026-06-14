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
#include "resolvers.hpp"
#include "generators.hpp"
#include "gen_helpers.hpp"
#include "../model/expressions.hpp"
#include "../model/statements.hpp"
#include "../model/operators.hpp"
#include "../model/mangler.hpp"
#include "../model/imported.hpp"
#include "../model/template.hpp"
#include "../model/template_instantiator.hpp"
#include "../model/template_deduction.hpp"
#include "../parse/ast.hpp"
#include "../../../libkdi/src/kdi_aggregates.hpp"
#include "llvm/Support/raw_os_ostream.h"
#include <llvm/IR/Constants.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/Intrinsics.h>
#include <unordered_set>
#include "../errors.hpp"
namespace k::model::gen {
// function_invocation_expression

void symbol_resolver::visit_function_invocation_expression(function_invocation_expression &expr) {
    expr.callee_expr()->accept(*this);
    for (auto arg : expr.arguments()) {
        arg->accept(*this);
    }
    // TODO Add more pre process here ?!?
}

namespace {
/**
 * Phase-3 helper: compute and store a virtual_dispatch_info annotation on a
 * function_invocation_expression after the callee and its 'this' type have been resolved.
 *
 * @param expr          The call expression being annotated.
 * @param func          The resolved function (callee).
 * @param member_callee Non-null when the call is of the form obj.method(...).
 *
 * Rules:
 *  - Qualified call (expr.is_non_virtual_qualified_call()) → DIRECT
 *  - Non-member call / no receiver              → DIRECT
 *  - Function not virtual                       → DIRECT
 *  - Virtual function through a class reference → VTABLE
 *    The dispatch_class is the *static* receiver type (the base class as written
 *    in the source).  The slot_index comes from the vtable layout.
 *    If the receiver is a secondary-base reference (embedded at non-zero offset),
 *    this_adjustment is set from secondary_vtable_spec::base_offset so the generator
 *    can use it if needed (Phase 4).
 */
void annotate_dispatch_info(function_invocation_expression& expr,
                            const std::shared_ptr<function>& func,
                            const std::shared_ptr<member_of_object_expression>& member_callee)
{
    // ── DIRECT cases ─────────────────────────────────────────────────────────
    if (expr.is_non_virtual_qualified_call() || !member_callee || !func) {
        virtual_dispatch_info di;
        di.kind = virtual_dispatch_info::dispatch_kind::DIRECT;
        expr.set_dispatch_info(std::move(di));
        return;
    }

    if (!func->is_virtual() || func->get_vtable_slot() < 0) {
        virtual_dispatch_info di;
        di.kind = virtual_dispatch_info::dispatch_kind::DIRECT;
        expr.set_dispatch_info(std::move(di));
        return;
    }

    // ── Determine the static receiver type ───────────────────────────────────
    auto this_type = member_callee->sub_expr()->get_type();
    if (!type::is_reference(this_type) && !type::is_drain(this_type)) {
        virtual_dispatch_info di;
        di.kind = virtual_dispatch_info::dispatch_kind::DIRECT;
        expr.set_dispatch_info(std::move(di));
        return;
    }

    auto bare_subtype = type::remove_const(this_type->get_subtype());
    auto st_type = std::dynamic_pointer_cast<struct_type>(bare_subtype);
    if (!st_type) {
        virtual_dispatch_info di;
        di.kind = virtual_dispatch_info::dispatch_kind::DIRECT;
        expr.set_dispatch_info(std::move(di));
        return;
    }

    auto kl = std::dynamic_pointer_cast<klass>(st_type->get_struct());
    if (!kl || !kl->has_vtable()) {
        // Check if it is an imported aggregate with a vtable
        // (imported_klass / imported_interface — neither derives from klass).
        auto imp = std::dynamic_pointer_cast<aggregate>(st_type->get_struct());
        if (imp && imp->has_vtable()) {
            virtual_dispatch_info di;
            di.kind                = virtual_dispatch_info::dispatch_kind::VTABLE;
            di.slot_index          = func->get_vtable_slot();
            di.imported_dispatch_agg = imp;
            di.this_adjustment     = 0;
            expr.set_dispatch_info(std::move(di));
            return;
        }
        virtual_dispatch_info di;
        di.kind = virtual_dispatch_info::dispatch_kind::DIRECT;
        expr.set_dispatch_info(std::move(di));
        return;
    }

    // ── Build VTABLE annotation ───────────────────────────────────────────────
    virtual_dispatch_info di;
    di.kind           = virtual_dispatch_info::dispatch_kind::VTABLE;
    di.slot_index     = func->get_vtable_slot();
    di.dispatch_class = kl;
    di.this_adjustment = 0;

    // Check if this receiver class is a secondary base somewhere (for information;
    // the generator may use this in Phase 4 to apply this-adjustment before vptr load).
    // We look for kl in the secondary_vtable_specs of its owner classes.
    // For now, for static dispatch we just store 0: the vptr in the object already
    // points to the right secondary vtable (set up by the constructor via emit_vptr_store).
    // The slot_index here is the index within kl's own vtable.

    expr.set_dispatch_info(std::move(di));
}

} // anonymous namespace

/**
 * Resolve a function invocation expression: overload resolution, argument adaptation,
 * virtual dispatch annotation.
 *
 * Steps:
 *   1. Resolve callee expression and all argument expressions.
 *   2. If callee is a member-of-object: resolve member call (member + unified call syntax).
 *   3. If callee is a symbol: resolve direct call or free-function call.
 *   4. If callee is a function pointer: validate argument types.
 *   5. Perform overload resolution via get_best_matching_function.
 *   6. Adapt arguments to match selected function's parameter types.
 *   7. Annotate virtual dispatch info (for virtual method calls via vtable).
 *   8. Set result type from the selected function's return type.
 */
void type_reference_resolver::visit_function_invocation_expression(function_invocation_expression &expr) {
    // Step 1: Resolve callee expression and all argument expressions
    auto callee = std::dynamic_pointer_cast<symbol_expression>(expr.callee_expr());
    auto member_callee = std::dynamic_pointer_cast<member_of_object_expression>(expr.callee_expr());
    auto pm_callee = std::dynamic_pointer_cast<pm_expression>(expr.callee_expr());
    auto ptr_member_callee = std::dynamic_pointer_cast<member_of_pointer_expression>(expr.callee_expr());

    if(!callee && !member_callee && !pm_callee && !ptr_member_callee) {
        throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_INVOKE_NOT_CALLABLE), expr.first_lexeme(),
            "Unsupported call expression form: only direct function calls ('func(args)'), "
            "member function calls ('obj.method(args)') and pointer-to-member calls "
            "('obj.*mfp(args)') are supported");
    }

    // Resolve and type-check all arguments first
    for(size_t arg_idx = 0; arg_idx < expr.arguments().size(); ++arg_idx) {
        _replacement_expr = nullptr;
        expr.arguments()[arg_idx]->accept(*this);
        if (_replacement_expr) {
            expr.assign_argument(arg_idx, _replacement_expr);
            _replacement_expr = nullptr;
        }
    }

    // Step 2: If callee is a member-of-object: resolve member call (member + unified call syntax)
    // ── Pre-process: ptr->method(args) → (*ptr).method(args) ─────────────────
    // When the callee is a member_of_pointer_expression (ptr->method), transform it
    // into member_of_object_expression(dereference(ptr), method) so that the existing
    // member_callee path handles dispatch uniformly (including vtable dispatch).
    if (ptr_member_callee) {
        auto sym = ptr_member_callee->symbol().shared_as<symbol_expression>();
        auto sub = ptr_member_callee->sub_expr();
        auto deref = dereference_expression::make_shared(sub);
        auto obj_member = member_of_object_expression::make_shared(deref, sym);
        expr.callee_expr(obj_member);
        member_callee = obj_member;
    }

    // ----------------------------------------------------------------
    // Case 0 : pointer-to-member call  "obj.*mfp(args)" or "ptr->*mfp(args)"
    // ----------------------------------------------------------------
    if (pm_callee) {
        // Visit the pm_expression to resolve types of both LHS and RHS
        pm_callee->accept(*this);

        // Retrieve the member function reference type from the RHS
        auto mfp_type = pm_callee->right()->get_type();
        if (auto ref = std::dynamic_pointer_cast<reference_type>(mfp_type)) {
            mfp_type = ref->get_subtype();
        }
        auto frt = std::dynamic_pointer_cast<function_reference_type>(mfp_type);
        if (!frt) {
            throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_DYNAMIC_CAST_BAD_TYPE), expr.first_lexeme(),
                "The '{}' call requires a member function reference type, but got '{}'",
                {pm_callee->is_arrow() ? "->*" : ".*", mfp_type ? mfp_type->to_string() : "?"});
        }

        // Set return type of the invocation expression
        // If the frt has no return type (e.g. it's a parameter with inferred type),
        // propagate from the enclosing function's return type.
        auto ret_type = frt->get_return_type();
        if (!ret_type && !_function_stack.empty()) {
            ret_type = _function_stack.back()->get_return_type();
            if (ret_type) {
                // Cache on the frt so later uses (e.g. in impl_gen) see it
                frt->set_return_type(ret_type);
            }
        }
        expr.set_type(ret_type);

        // Adapt arguments against the frt's parameter types.
        // NOTE: for member_function_reference_type, get_parameter_types() returns ONLY the
        // explicit parameters — the implicit 'this' pointer is NOT in _parameter_types
        // (it appears only in the LLVM FunctionType built by function_reference_type_builder).
        // Therefore param_offset is always 0 here.
        const auto& frt_params = frt->get_parameter_types();
        const auto& call_args = expr.arguments();
        for (size_t i = 0; i < call_args.size() && i < frt_params.size(); ++i) {
            auto adapted = adapt_type(call_args[i], frt_params[i]);
            if (adapted && adapted != call_args[i]) {
                expr.assign_argument(i, adapted);
            }
        }

        // Annotate dispatch info as INDIRECT_MEMBER
        virtual_dispatch_info di;
        di.kind = virtual_dispatch_info::dispatch_kind::INDIRECT_MEMBER;
        expr.set_dispatch_info(std::move(di));
        return;
    }

    // ----------------------------------------------------------------
    // Case 1 : member-of-object call  "obj.method(args)"
    // ----------------------------------------------------------------
    if (member_callee) {
        // Visit the full member_of_object_expression so that upcast injection for inherited
        // methods is triggered (visit_member_of_object_expression injects a cast_expression
        // into sub_expr when the method belongs to a base struct).
        member_callee->accept(*this);

        callee = std::dynamic_pointer_cast<symbol_expression>(
                member_callee->symbol().shared_as<symbol_expression>());
        if (!callee) {
            throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_INVOKE_ARG_TYPE_MISMATCH), expr.first_lexeme(),
                "Unsupported member call form: the right-hand side of '.' must be a simple name, "
                "not a complex expression");
        }

        // sub_expr of member_callee gives the object reference (possibly upcast)
        auto this_expr = member_callee->sub_expr();
        auto this_type = this_expr->get_type(); // should be ref<struct> (possibly base)

        if (!type::is_reference(this_type) && !type::is_struct(this_type)
            && !type::is_drain(this_type) && !type::is_owner(this_type)) {
            throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_INVOKE_TOO_MANY_ARGS), expr.first_lexeme(),
                "The '.' operator requires the left-hand side to have a reference type, "
                "but '{}' is not a reference; did you mean to use a reference parameter?",
                {this_type ? this_type->to_string() : "?"});
        }
        auto subtype = (type::is_reference(this_type) || type::is_drain(this_type) || type::is_owner(this_type))
            ? this_type->get_subtype() : this_type;
        // Detect if the object is accessed through a const reference (ref<const S>)
        bool is_const_this = type::is_const(subtype);
        auto bare_subtype = type::remove_const(subtype);
        if (auto owner_subtype = std::dynamic_pointer_cast<owner_type>(bare_subtype)) {
            bare_subtype = type::remove_const(owner_subtype->get_subtype());
        }
        auto struct_subtype = std::dynamic_pointer_cast<struct_type>(bare_subtype);
        if (!struct_subtype) {
            throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_INVOKE_TOO_FEW_ARGS), expr.first_lexeme(),
                "The '.' operator can only be applied to a struct type, "
                "but the left-hand side has type '{}' which is not a struct",
                {bare_subtype ? bare_subtype->to_string() : "?"});
        }
        auto st = struct_subtype->get_struct();

        // Use the short (unqualified) name for function lookup
        std::string func_short_name = callee->get_name().back();

        // ── Union intrinsic: index() ──────────────────────────────────────────
        // If the struct_type belongs to a union (no owning aggregate), handle
        // the built-in index() method that returns the Kind enum value.
        if (!st && func_short_name == "index" && expr.arguments().empty()) {
            // Search the containing aggregate first (handles template instantiations
            // where template and clone share the same struct_type pointer).
            std::shared_ptr<union_type_def> union_def;
            for (auto cur = expr.shared_as<element>(); cur; cur = cur->parent<element>()) {
                if (auto agg = std::dynamic_pointer_cast<aggregate>(cur)) {
                    union_def = find_union_by_struct_type_in_aggregate(agg, struct_subtype);
                    if (union_def) break;
                }
            }
            if (!union_def) {
                auto root_ns = _unit.get_root_namespace();
                union_def = find_union_by_struct_type(root_ns, struct_subtype);
            }
            if (union_def) {
                // Ensure Kind enum is synthesized (may not be for template instantiations)
                union_def->synthesize_kind_enum();
                auto kind_enum = union_def->get_kind_enum();
                if (kind_enum) {
                    // Ensure enum_type is created
                    if (!kind_enum->get_enum_type()) {
                        auto uint_type = _context->from_type(primitive_type::UNSIGNED_INT);
                        kind_enum->set_underlying_type(uint_type);
                        auto et = std::shared_ptr<enum_type>(new enum_type(kind_enum, uint_type));
                        kind_enum->set_enum_type(et);
                    }
                    expr.set_type(kind_enum->get_enum_type());
                    expr.set_union_index_intrinsic(true);
                    return;
                }
            }
        }

        // If st is null (union type, not a struct/class), and we didn't resolve
        // index() above, throw an error since unions don't have regular methods.
        if (!st) {
            throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_INVOKE_NOT_CALLABLE), expr.first_lexeme(),
                "Cannot call method '{}' on a union type; unions only support the built-in 'index()' method",
                {func_short_name});
        }

        // ── Qualified member call: obj.Base::method(args) or this->Base::method(args) ──
        // If the callee symbol has more than one name component (e.g. Base::method), it is
        // an explicit qualification: bypass virtual dispatch and call the exact named class.
        const bool is_qualified_member_call = (callee->get_name().size() > 1);
        if (is_qualified_member_call) {
            // The qualifying class name is the second-to-last component (e.g. "Base" in Base::method).
            const std::string& qualifying_class_name = callee->get_name()[callee->get_name().size() - 2];

            // Find the qualifying aggregate — it must be the class itself or one of its bases.
            std::shared_ptr<aggregate> qualifying_agg;
            std::function<void(const std::shared_ptr<aggregate>&)> find_class;
            find_class = [&](const std::shared_ptr<aggregate>& agg) {
                if (!agg || qualifying_agg) return;
                if (agg->get_short_name() == qualifying_class_name) {
                    qualifying_agg = agg;
                    return;
                }
                for (auto& bs : agg->get_bases()) {
                    if (bs.base) find_class(bs.base);
                }
            };
            find_class(st);

            if (!qualifying_agg) {
                throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_NEW_EXPECT_STRUCT_OR_PRIM), expr.first_lexeme(),
                    "Qualified member call '{}': '{}' is not a base class of '{}'; "
                    "the qualifying class must be the class itself or one of its base classes",
                    {callee->get_name().to_string(), qualifying_class_name, st->get_short_name()});
            }

            // Collect overloads of func_short_name directly in the qualifying class
            std::vector<std::shared_ptr<function>> qual_candidates;
            for (auto& fn : qualifying_agg->functions()) {
                if (fn && fn->get_short_name() == func_short_name) {
                    qual_candidates.push_back(fn);
                }
            }
            if (qual_candidates.empty()) {
                throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_NEW_CTOR_ARG_MISMATCH), expr.first_lexeme(),
                    "No function named '{}' found in class '{}'",
                    {func_short_name, qualifying_class_name});
            }

            // Upcast this_expr to the qualifying class reference
            auto qual_ref_type = is_const_this
                ? qualifying_agg->get_struct_type()->get_const()->get_reference()
                : qualifying_agg->get_struct_type()->get_reference();
            auto upcast_this = adapt_type(this_expr, qual_ref_type);
            if (upcast_this) this_expr = upcast_this;
            // Update the sub_expr of member_callee so the IR generator uses the upcast
            member_callee->sub_expr() = this_expr;

            auto best = get_best_matching_function(qual_candidates, expr.arguments(), this_expr);
            if (!best.func) return;

            check_function_visibility(*best.func, expr);

            callee->set_target(best.func);
            auto resolved_return_type = resolve_generic_call_return_type(*best.func, this_expr);
            expr.set_type(resolved_return_type ? resolved_return_type : best.func->get_return_type());
            expr.assign_arguments(best.adapted_args);
            // Bypass virtual dispatch — this is an explicit base-class call
            expr.set_non_virtual_qualified_call(true);
            // Phase 3: annotate dispatch info
            annotate_dispatch_info(expr, best.func, member_callee);
            // Exception contract check
            check_call_contract(*best.func, expr.first_lexeme());
            return;
        }

        // Collect all candidate functions (member + free/static from parent scopes)
        std::vector<std::shared_ptr<function>> candidates = scope_lookup::lookup_functions(st, func_short_name);

        // If calling on a const object, only const member functions are callable.
        if (is_const_this) {
            std::vector<std::shared_ptr<function>> const_candidates;
            for (auto& f : candidates) {
                if (!f->is_member() || f->is_static() || f->is_const_member()) {
                    const_candidates.push_back(f);
                }
            }
            if (const_candidates.empty() && !candidates.empty()) {
                throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_CAST_UNSUPPORTED), expr.first_lexeme(),
                    "Cannot call mutable member function '{}' on a const object of type '{}': "
                    "only const member functions can be called on const objects",
                    {func_short_name, struct_subtype->name()});
            }
            candidates = std::move(const_candidates);
        }

        if (candidates.empty()) {
            throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_INVOKE_NO_MATCHING_OVERLOAD), expr.first_lexeme(),
                "No function named '{}' found in struct '{}' or its enclosing scopes; "
                "check the spelling or verify that '{}' is declared as a method or free function",
                {callee->get_name().to_string(), st->get_short_name(),
                 callee->get_name().to_string()});
        }

        // ── Member template function instantiation (explicit template args) ──
        // If the callee carries explicit template arguments (e.g. obj.method<int>(...)),
        // find the template method in the candidates, instantiate it, and replace
        // the candidates list with the concrete instance.
        if (callee && callee->has_ast_template_args()) {
            std::shared_ptr<function> tpl_func;
            for (auto& cand : candidates) {
                if (cand->is_template()) {
                    tpl_func = cand;
                    break;
                }
            }
            if (tpl_func) {
                auto* ti = tpl_func->get_tpl_info();
                bool has_pack = ti && std::any_of(ti->params.begin(), ti->params.end(),
                    [](const template_param_descriptor& p) { return p.is_pack; });
                const auto& ast_args = callee->get_ast_template_args();
                if (ti && (ast_args.size() <= ti->params.size() || has_pack)) {
                    // Convert AST template args to model template_arguments
                    std::vector<template_argument> model_args;
                    bool args_ok = true;
                    for (size_t i = 0; i < ast_args.size(); ++i) {
                        const auto& ast_arg = ast_args[i];
                        if (ast_arg->is_type()) {
                            auto arg_type = _context->from_type_specifier(*ast_arg->type_arg);
                            arg_type = _context->resolve_type(arg_type);
                            if (!arg_type || !type::is_resolved(arg_type) || type::contains_unresolved(arg_type)) {
                                args_ok = false; break;
                            }
                            model_args.push_back(template_argument::make_type(arg_type));
                        } else if (ast_arg->is_value()) {
                            k::value_type val;
                            if (!extract_value_from_ast_expr(ast_arg->value_arg.get(), val)) {
                                args_ok = false; break;
                            }
                            model_args.push_back(template_argument::make_value(val));
                        } else {
                            args_ok = false; break;
                        }
                    }
                    // Fill defaults for trailing non-pack params
                    for (size_t i = ast_args.size(); i < ti->params.size() && args_ok; ++i) {
                        auto& param = ti->params[i];
                        if (param.is_pack) continue;
                        if (param.is_type_param() && param.default_type) {
                            auto def = param.default_type;
                            if (!type::is_resolved(def)) def = _context->resolve_type(def);
                            if (def && type::is_resolved(def)) {
                                model_args.push_back(template_argument::make_type(def));
                            } else { args_ok = false; }
                        } else if (param.is_value_param() && param.default_value.has_value()) {
                            model_args.push_back(template_argument::make_value(*param.default_value));
                        } else if (!param.is_pack) { args_ok = false; }
                    }
                    if (args_ok) {
                        // For member templates, instantiate within the owning aggregate
                        // (instantiate_function expects ns, but aggregates are not ns).
                        // We call define_function directly on the aggregate.
                        auto* ti = tpl_func->get_tpl_info();
                        std::string key = build_instantiation_key(model_args);
                        // Check instantiation cache
                        std::shared_ptr<function> concrete;
                        auto cache_it = ti->instantiations.find(key);
                        if (cache_it != ti->instantiations.end()) {
                            if (auto* fn_ptr = std::get_if<std::shared_ptr<function>>(&cache_it->second)) {
                                concrete = *fn_ptr;
                            }
                        }
                        if (!concrete) {
                            std::string base_name = tpl_func->get_short_name();
                            std::string inst_name = build_instantiated_name(base_name, model_args);
                            auto subst = template_instantiator::build_substitution_map_public(*ti, model_args);
                            auto val_subst = template_instantiator::build_value_substitution_map_public(*ti, model_args);
                            auto pack_subst = template_instantiator::build_pack_substitution_map_public(*ti, model_args);

                            // Create the concrete function as a member of the aggregate
                            concrete = st->define_function(inst_name, tpl_func->is_static());
                            if (concrete) {
                                concrete->set_visibility(tpl_func->get_visibility());
                                concrete->set_const_member(tpl_func->is_const_member());
                                concrete->set_operator(tpl_func->is_operator());
                                concrete->set_aliasing(tpl_func->get_aliasing());
                                concrete->set_compiler_generated(tpl_func->is_compiler_generated());
                                template_instantiator::populate_function_from_template_public(
                                    concrete, *tpl_func, subst, val_subst, pack_subst);
                                concrete->set_tpl_instantiation_info(base_name, model_args);
                                concrete->mark_instantiation();
                                ti->instantiations[key] = concrete;
                            }
                        }
                        if (concrete) {
                            // Run through resolver pipeline
                            {
                                symbol_resolver sr(*this, _context, _unit);
                                concrete->accept(sr);
                            }
                            {
                                signature_resolver sigr(*this, _context, _unit);
                                concrete->accept(sigr);
                            }
                            concrete->accept(*this);

                            // Replace candidates with just the concrete instance
                            callee->set_target(concrete);
                            candidates.clear();
                            candidates.push_back(concrete);
                        }
                    }
                }
            }
        }

        // ── Member template argument deduction (implicit template args) ──
        // If the callee does NOT carry explicit template arguments but there are
        // template candidates, attempt to deduce from the call-site argument types.
        if (callee && !callee->has_ast_template_args()) {
            std::vector<std::shared_ptr<function>> tpl_candidates;
            for (auto& cand : candidates) {
                if (cand->is_template()) {
                    tpl_candidates.push_back(cand);
                }
            }
            if (!tpl_candidates.empty()) {
                std::vector<std::shared_ptr<type>> arg_types;
                for (auto& arg : expr.arguments()) {
                    arg_types.push_back(arg ? arg->get_type() : nullptr);
                }
                // Check if a non-template candidate with matching arity exists
                size_t call_arity = arg_types.size();
                bool has_viable_non_template = false;
                for (auto& cand : candidates) {
                    if (!cand->is_template()) {
                        size_t param_count = 0;
                        for (auto& p : cand->parameters()) {
                            if (p != cand->get_this_parameter()) param_count++;
                        }
                        if (param_count == call_arity) {
                            has_viable_non_template = true;
                            break;
                        }
                    }
                }
                if (!has_viable_non_template) {
                    for (auto& tpl_func : tpl_candidates) {
                        auto* ti = tpl_func->get_tpl_info();
                        if (!ti) continue;
                        auto deduction = k::model::deduce_template_arguments(*ti, tpl_func->parameters(), arg_types);
                        if (!deduction.success) continue;
                        size_t err_idx;
                        std::string err_reason;
                        if (!validate_template_arg_constraints(ti->params, deduction.deduced_args, err_idx, err_reason)) {
                            continue;
                        }
                        // Instantiate within the owning aggregate (not root ns)
                        auto& deduced_args = deduction.deduced_args;
                        std::string key = build_instantiation_key(deduced_args);
                        std::shared_ptr<function> concrete;
                        auto cache_it = ti->instantiations.find(key);
                        if (cache_it != ti->instantiations.end()) {
                            if (auto* fn_ptr = std::get_if<std::shared_ptr<function>>(&cache_it->second)) {
                                concrete = *fn_ptr;
                            }
                        }
                        if (!concrete) {
                            std::string base_name = tpl_func->get_short_name();
                            std::string inst_name = build_instantiated_name(base_name, deduced_args);
                            auto subst = template_instantiator::build_substitution_map_public(*ti, deduced_args);
                            auto val_subst = template_instantiator::build_value_substitution_map_public(*ti, deduced_args);
                            auto pack_subst = template_instantiator::build_pack_substitution_map_public(*ti, deduced_args);
                            concrete = st->define_function(inst_name, tpl_func->is_static());
                            if (concrete) {
                                concrete->set_visibility(tpl_func->get_visibility());
                                concrete->set_const_member(tpl_func->is_const_member());
                                concrete->set_operator(tpl_func->is_operator());
                                concrete->set_aliasing(tpl_func->get_aliasing());
                                concrete->set_compiler_generated(tpl_func->is_compiler_generated());
                                template_instantiator::populate_function_from_template_public(
                                    concrete, *tpl_func, subst, val_subst, pack_subst);
                                concrete->set_tpl_instantiation_info(base_name, deduced_args);
                                concrete->mark_instantiation();
                                ti->instantiations[key] = concrete;
                            }
                        }
                        if (concrete) {
                            {
                                symbol_resolver sr(*this, _context, _unit);
                                concrete->accept(sr);
                            }
                            {
                                signature_resolver sigr(*this, _context, _unit);
                                concrete->accept(sigr);
                            }
                            concrete->accept(*this);
                            callee->set_target(concrete);
                            candidates.erase(
                                std::remove(candidates.begin(), candidates.end(), tpl_func),
                                candidates.end());
                            candidates.push_back(concrete);
                            break;
                        }
                    }
                    // Remove remaining uninstantiated template candidates
                    candidates.erase(
                        std::remove_if(candidates.begin(), candidates.end(),
                            [](const std::shared_ptr<function>& f) { return f->is_template(); }),
                        candidates.end());
                }
            }
        }

        auto best = get_best_matching_function(candidates, expr.arguments(), this_expr);
        if (!best.func) {
            // get_best_matching_function already reported/threw an error
            return;
        }

        // Static constructors and destructors cannot be called explicitly
        if (std::dynamic_pointer_cast<static_constructor>(best.func)) {
            throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_INVOKE_VISIBILITY_DENIED), expr.first_lexeme(),
                "Static constructor '{}' cannot be called explicitly; "
                "it is automatically invoked during program initialization",
                {best.func->get_short_name()});
        }
        if (std::dynamic_pointer_cast<static_destructor>(best.func)) {
            throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_INVOKE_CTOR_RESULT), expr.first_lexeme(),
                "Static destructor '~{}' cannot be called explicitly; "
                "it is automatically invoked during program finalization",
                {best.func->get_short_name()});
        }

        // Check visibility of the resolved function
        check_function_visibility(*best.func, expr);

        callee->set_target(best.func);
        auto resolved_return_type = resolve_generic_call_return_type(*best.func, this_expr);
        expr.set_type(resolved_return_type ? resolved_return_type : best.func->get_return_type());

        // Apply adapted arguments (may include cloned defaults for trailing params)
        expr.assign_arguments(best.adapted_args);
        // Note: if best.is_unified_call, the callee stays as member_of_object_expression
        // but the resolved function is free/static. impl_gen handles this by passing
        // sub_expr() value as first argument when the function is not a member.
        // Phase 3: annotate dispatch info
        annotate_dispatch_info(expr, best.func, member_callee);
        // Exception contract check
        check_call_contract(*best.func, expr.first_lexeme());
        return;
    }

    // Step 3: If callee is a symbol: resolve direct call or free-function call
    // ----------------------------------------------------------------
    // Case 1.5 : indirect call via a function-reference variable "fp(args)"
    //   callee may be unresolved (symbol_resolver deferred it as a potential function call).
    //   Try to resolve it as a variable by walking the scope chain manually.
    //   If a variable with a function_reference_type is found, treat this as indirect.
    // ----------------------------------------------------------------
    if (callee && !callee->is_resolved()) {
        // Walk up the scope chain from the callee expression to find a variable with this name.
        const k::name& sym_name = callee->get_name();
        if (sym_name.size() == 1) {
            const std::string& simple_name = sym_name.back();
            // Walk up: callee → function_invocation → ... → block → function
            std::shared_ptr<element> cur = callee->shared_as<element>();
            while (cur) {
                if (auto vh = std::dynamic_pointer_cast<variable_holder>(cur)) {
                    if (auto vdef = vh->get_variable(sym_name)) {
                        callee->set_target(vdef);
                        break;
                    }
                }
                cur = cur->parent<element>();
            }
        }
    }
    if (callee && callee->is_variable_def()) {
        // Make sure the callee symbol has its type resolved
        callee->accept(*this);
        auto callee_type = callee->get_type();
        if (callee_type) {
            // Unwrap reference / indirection wrapper
            auto inner_type = callee_type;
            while (inner_type && (type::is_reference(inner_type) || type::is_link(inner_type) ||
                                   type::is_pointer(inner_type) || type::is_view(inner_type))) {
                inner_type = inner_type->get_subtype();
            }
            // Also unwrap an unresolved_function_ref_type that has been resolved
            if (auto ufrt = std::dynamic_pointer_cast<unresolved_function_ref_type>(inner_type)) {
                if (ufrt->is_resolved()) {
                    inner_type = ufrt->get_resolved();
                    while (inner_type && (type::is_reference(inner_type) || type::is_link(inner_type) ||
                                           type::is_pointer(inner_type) || type::is_view(inner_type))) {
                        inner_type = inner_type->get_subtype();
                    }
                }
            }
            // If the inner type is a function_reference_type, this is an indirect call
            auto frt = std::dynamic_pointer_cast<function_reference_type>(inner_type);
            if (frt) {
                // Set the return type of the call expression.
                // If the frt has no return type yet (e.g. parameter with no init expression),
                // try to propagate the return type from the enclosing function's context.
                auto ret_type = frt->get_return_type();
                if (!ret_type && !_function_stack.empty()) {
                    // Propagate from the enclosing function's return type
                    ret_type = _function_stack.back()->get_return_type();
                    if (ret_type) {
                        // Mutate the frt in-place to record the inferred return type.
                        frt->set_return_type(ret_type);
                    }
                }
                expr.set_type(ret_type);
                // Type-adapt arguments against the function_reference_type's parameter types
                const auto& params = frt->get_parameter_types();
                for (size_t n = 0; n < expr.arguments().size() && n < params.size(); ++n) {
                    auto arg = expr.arguments().at(n);
                    auto cast = adapt_type(arg, params[n]);
                    if (cast && cast != arg) expr.assign_argument(n, cast);
                }
                // Mark as indirect call — no dispatch annotation needed
                virtual_dispatch_info di;
                di.kind = virtual_dispatch_info::dispatch_kind::INDIRECT;
                expr.set_dispatch_info(std::move(di));
                return;
            }
        }
    }

    // ----------------------------------------------------------------
    // Case 2 : plain symbol call  "func(args)"
    // ----------------------------------------------------------------
    {
        std::string func_name = callee->get_name().back();
        const auto& args = expr.arguments();

        // A qualified name (e.g. Base::value) means the call is non-virtual and targets
        // exactly the named function.  Do NOT collect additional member-function candidates
        // from the first argument's struct type — that would create false ambiguities and
        // would defeat the purpose of the explicit qualification.
        const bool is_qualified_call = (callee->get_name().size() > 1);

        std::vector<std::shared_ptr<function>> all_candidates;
        if (is_qualified_call && callee->has_qualifier_template_args()) {
            const auto qualifier_name = callee->get_name().without_back();

            std::optional<lex::punctuator> root_prefix;
            if (qualifier_name.has_root_prefix()) {
                root_prefix = lex::punctuator{"::", lex::punctuator::DOUBLE_COLON};
            }

            std::vector<lex::identifier> qualifier_parts;
            qualifier_parts.reserve(qualifier_name.size());
            for (const auto& part : qualifier_name.parts()) {
                qualifier_parts.emplace_back(std::string_view(part));
            }

            if (!qualifier_parts.empty()) {
                parse::ast::qualified_identifier ast_qid(root_prefix, qualifier_parts);
                parse::ast::identified_type_specifier ast_identified(ast_qid,
                                                                     callee->get_ast_template_args(),
                                                                     true);
                auto qualifier_type = _context->from_type_specifier(ast_identified);
                if (auto unres = std::dynamic_pointer_cast<unresolved_type>(qualifier_type)) {
                    aggregate_type_resolver helper(_log, _context, _unit);
                    auto resolved_type = helper.try_instantiate_template_type(unres, expr);
                    if (auto st_type = std::dynamic_pointer_cast<struct_type>(resolved_type)) {
                        if (auto owner_struct = st_type->get_struct()) {
                            for (auto& fn : owner_struct->functions()) {
                                if (fn && fn->get_short_name() == func_name) {
                                    all_candidates.push_back(fn);
                                }
                            }
                        }
                    }
                }
            }
        }

        if (is_qualified_call && all_candidates.empty()) {
            // For a qualified name (e.g. Base::value or point::get), collect ALL overloads
            // of the short name within the qualifying context (struct / namespace), not the
            // entire scope chain.  This prevents false ambiguity with functions of the same
            // name in outer scopes while still supporting overload resolution.
            if (callee->is_function() && callee->get_function()) {
                auto resolved_fn = callee->get_function();
                auto owner = resolved_fn->parent<element>();
                if (owner) {
                    // Collect all overloads of func_name directly in the owner element.
                    if (auto fh = dynamic_cast<function_holder*>(owner.get())) {
                        for (auto& fn : fh->functions()) {
                            if (fn && fn->get_short_name() == func_name) {
                                all_candidates.push_back(fn);
                            }
                        }
                    }
                }
                // Fallback: only the resolved function
                if (all_candidates.empty()) {
                    all_candidates.push_back(resolved_fn);
                }
            }
        } else if (!is_qualified_call) {
            all_candidates = scope_lookup::lookup_functions(callee, func_name);
        }

        std::shared_ptr<expression> this_candidate;
        std::vector<std::shared_ptr<expression>> rest_args;

        // For a qualified call (e.g. Base::value(d) or point::get(pt, 6f)):
        // If the first argument is a reference to a struct, treat it as the potential 'this'
        // for member functions (Mode A) while also allowing Mode B matching for static functions.
        // We do NOT restrict to all_are_member because the candidates may be a mix of member
        // and static overloads.
        if (is_qualified_call && !all_candidates.empty() && !args.empty()) {
            auto first_arg_type = args[0]->get_type();
            if (type::is_reference(first_arg_type)) {
                auto bare_sub = type::remove_const(first_arg_type->get_subtype());
                if (std::dynamic_pointer_cast<struct_type>(bare_sub)) {
                    this_candidate = args[0];
                    rest_args = std::vector<std::shared_ptr<expression>>(args.begin() + 1, args.end());
                }
            }
        }

        // ── Implicit 'this' injection for Base::method() from inside a member function ──
        // If we have a qualified call (Base::method) with no explicit 'this' argument yet,
        // and we are inside a non-static member function whose owning class is derived from
        // the qualifying base class, inject 'this' automatically.
        // This enables the pattern:  Base::method()  inside an override instead of
        // the more verbose:  Base::method(this)
        //
        // Also applies to unqualified member-function calls: when calling method() from
        // within another member function, inject 'this' so that the member function
        // candidate can match via Mode A in get_best_matching_function.
        if (!this_candidate && !_function_stack.empty()) {
            auto enclosing_fn = _function_stack.back();
            if (enclosing_fn && enclosing_fn->is_member() && !enclosing_fn->is_static()) {
                auto this_param = enclosing_fn->get_this_parameter();
                if (this_param && this_param->get_type()) {
                    // Check that at least one candidate is a member function (not static)
                    bool any_member = std::any_of(all_candidates.begin(), all_candidates.end(),
                        [](const std::shared_ptr<function>& f){ return f && f->is_member() && !f->is_static(); });
                    if (any_member) {
                        // Build a symbol_expression for 'this'
                        auto this_sym = symbol_expression::from_identifier(k::name("this"));
                        this_sym->set_target(std::const_pointer_cast<parameter>(this_param));
                        this_sym->set_type(this_param->get_type());
                        this_candidate = this_sym;
                        rest_args = args; // all explicit args remain as-is (no args consumed)
                    }
                }
            }
        }

        // Unified-call-syntax: only when the call is NOT a qualified name.
        // For a qualified call "Base::method(d)", d is already handled above.
        if (!is_qualified_call && !args.empty()) {
            auto first_arg_type = args[0]->get_type();
            if (type::is_reference(first_arg_type)) {
                if (auto first_struct = std::dynamic_pointer_cast<struct_type>(first_arg_type->get_subtype())) {
                    auto st = first_struct->get_struct();
                    this_candidate = args[0];
                    rest_args = std::vector<std::shared_ptr<expression>>(args.begin() + 1, args.end());
                    // Collect member functions from the aggregate and all its bases (recursively)
                    std::function<void(const std::shared_ptr<aggregate>&)> collect_member_fns;
                    collect_member_fns = [&](const std::shared_ptr<aggregate>& s) {
                        if (!s) return;
                        for (auto& f : scope_lookup::lookup_functions(s, func_name)) {
                            if (std::find(all_candidates.begin(), all_candidates.end(), f) == all_candidates.end()) {
                                all_candidates.push_back(f);
                            }
                        }
                        for (auto& bs : s->get_bases()) {
                            if (bs.base) collect_member_fns(bs.base);
                        }
                    };
                    collect_member_fns(st);
                }
            }
        }

        // ── Template function instantiation ──────────────────────────────
        // If the callee carries explicit template arguments (e.g. identity<int>),
        // try to find a template function definition, instantiate it with the
        // provided arguments, run it through the resolution pipeline, and
        // replace the candidates with the concrete instance.
        if (callee && callee->has_ast_template_args()) {
            const auto resolve_type_from_instantiation_context = [&](const std::shared_ptr<unresolved_type>& unresolved)
                    -> std::shared_ptr<type> {
                if (!unresolved || unresolved->type_id().has_root_prefix() || unresolved->type_id().size() != 1) {
                    return nullptr;
                }

                const std::string param_name = unresolved->type_id().front();
                const auto lookup_template_param = [&](const auto& ast_params,
                                                       const auto& concrete_args) -> std::shared_ptr<type> {
                    const size_t count = std::min(ast_params.size(), concrete_args.size());
                    for (size_t i = 0; i < count; ++i) {
                        if (ast_params[i] == nullptr) continue;
                        if (std::string(ast_params[i]->name.content) != param_name) continue;
                        if (concrete_args[i].is_type()) {
                            return concrete_args[i].type_arg;
                        }
                        return nullptr;
                    }
                    return nullptr;
                };

                if (_function_stack.empty()) return nullptr;

                auto current_fn = _function_stack.back();
                if (!current_fn) return nullptr;

                if (current_fn->has_tpl_args()) {
                    if (auto ast_fn = current_fn->get_ast_function_decl()) {
                        if (auto resolved = lookup_template_param(ast_fn->template_params, current_fn->get_tpl_args())) {
                            return resolved;
                        }
                    }
                }

                if (current_fn->is_member()) {
                    if (auto owner = current_fn->get_owner()) {
                        if (owner->has_tpl_args()) {
                            if (auto ast_agg = owner->get_ast_aggregate_decl()) {
                                if (auto resolved = lookup_template_param(ast_agg->template_params, owner->get_tpl_args())) {
                                    return resolved;
                                }
                            }
                        }
                    }
                }

                return nullptr;
            };

            const auto resolve_explicit_template_arg_type = [&](const auto& self,
                                                                const std::shared_ptr<type>& arg_type)
                    -> std::shared_ptr<type> {
                if (!arg_type) return nullptr;

                auto resolved = _context->resolve_type(arg_type);
                if (resolved && type::contains_unresolved(resolved) == false && type::is_resolved(resolved)) {
                    return resolved;
                }

                if (auto unres = std::dynamic_pointer_cast<unresolved_type>(arg_type)) {
                    if (auto resolved_from_inst = resolve_type_from_instantiation_context(unres)) {
                        return resolved_from_inst;
                    }
                    resolved = resolve_type_by_name(unres->type_id(), expr);
                    if ((!resolved || !type::is_resolved(resolved)) && unres->type_id().empty() == false) {
                        resolved = _context->from_string(unres->type_id().to_string());
                    }
                    if (resolved && type::contains_unresolved(resolved) == false && type::is_resolved(resolved)) {
                        return resolved;
                    }
                    return nullptr;
                }

                auto subtype = arg_type->get_subtype();
                if (!subtype) return nullptr;

                auto resolved_subtype = self(self, subtype);
                if (!resolved_subtype || type::contains_unresolved(resolved_subtype) || !type::is_resolved(resolved_subtype)) {
                    return nullptr;
                }

                std::shared_ptr<type> rebuilt;
                if (type::is_const(arg_type)) {
                    rebuilt = resolved_subtype->get_const();
                } else if (type::is_pointer(arg_type)) {
                    rebuilt = resolved_subtype->get_pointer();
                } else if (type::is_reference(arg_type)) {
                    rebuilt = resolved_subtype->get_reference();
                } else if (type::is_link(arg_type)) {
                    rebuilt = resolved_subtype->get_link();
                } else if (type::is_view(arg_type)) {
                    rebuilt = resolved_subtype->get_view();
                } else if (type::is_owner(arg_type)) {
                    rebuilt = resolved_subtype->get_owner();
                } else if (type::is_drain(arg_type)) {
                    rebuilt = resolved_subtype->get_drain();
                } else if (type::is_array(arg_type)) {
                    if (auto sized = std::dynamic_pointer_cast<sized_array_type>(arg_type)) {
                        rebuilt = resolved_subtype->get_array(sized->get_size());
                    } else {
                        rebuilt = resolved_subtype->get_array()->get_reference();
                    }
                }

                if (!rebuilt) return nullptr;

                resolved = _context->resolve_type(rebuilt);
                if (resolved && type::contains_unresolved(resolved) == false && type::is_resolved(resolved)) {
                    return resolved;
                }
                return nullptr;
            };

            const auto& ast_args = callee->get_ast_template_args();
            // Look up the template function by base name (could be in all_candidates or root ns)
            std::shared_ptr<function> tpl_func;
            for (auto& cand : all_candidates) {
                if (cand->is_template()) {
                    tpl_func = cand;
                    break;
                }
            }
            if (!tpl_func) {
                auto root_ns = _unit.get_root_namespace();
                if (root_ns) {
                    for (auto& fn : root_ns->functions()) {
                        if (fn->get_short_name() == func_name && fn->is_template()) {
                            tpl_func = fn;
                            break;
                        }
                    }
                }
            }
            if (tpl_func) {
                auto* ti = tpl_func->get_tpl_info();
                // Allow more args than params if any param is a pack
                bool has_pack = ti && std::any_of(ti->params.begin(), ti->params.end(),
                    [](const template_param_descriptor& p) { return p.is_pack; });
                if (ti && (ast_args.size() <= ti->params.size() || has_pack)) {
                    // Convert AST template args to model template_arguments
                    std::vector<template_argument> model_args;
                    bool args_ok = true;
                    for (size_t i = 0; i < ast_args.size(); ++i) {
                        const auto& ast_arg = ast_args[i];
                        if (ast_arg->is_type()) {
                            auto arg_type = _context->from_type_specifier(*ast_arg->type_arg);
                            arg_type = resolve_explicit_template_arg_type(resolve_explicit_template_arg_type, arg_type);
                            if (!arg_type || !type::is_resolved(arg_type) || type::contains_unresolved(arg_type)) {
                                args_ok = false;
                                break;
                            }
                            model_args.push_back(template_argument::make_type(arg_type));
                        } else if (ast_arg->is_value()) {
                            // Value template argument — extract compile-time constant literal
                            k::value_type val;
                            if (!extract_value_from_ast_expr(ast_arg->value_arg.get(), val)) {
                                args_ok = false; break;
                            }
                            model_args.push_back(template_argument::make_value(val));
                        } else {
                            args_ok = false; break;
                        }
                    }
                    // Fill defaults for trailing non-pack params
                    for (size_t i = ast_args.size(); i < ti->params.size() && args_ok; ++i) {
                        auto& param = ti->params[i];
                        if (param.is_pack) {
                            // Pack params with no remaining args get an empty pack — nothing to push
                            continue;
                        }
                        if (param.is_type_param() && param.default_type) {
                            auto def = param.default_type;
                            if (!type::is_resolved(def)) def = _context->resolve_type(def);
                            if (def && type::is_resolved(def)) {
                                model_args.push_back(template_argument::make_type(def));
                            } else { args_ok = false; }
                        } else if (param.is_value_param() && param.default_value.has_value()) {
                            model_args.push_back(template_argument::make_value(*param.default_value));
                        } else if (!param.is_pack) { args_ok = false; }
                    }
                    // Resolve constraint types in template params if still unresolved
                    if (args_ok) {
                        for (auto& param : ti->params) {
                            if (param.is_type_param() && param.constraint_type && !type::is_resolved(param.constraint_type)) {
                                auto resolved = _context->resolve_type(param.constraint_type);
                                if (resolved && type::is_resolved(resolved)) {
                                    param.constraint_type = resolved;
                                }
                            }
                        }
                    }
                    // Validate type constraints (kind filter + base-type constraint)
                    if (args_ok) {
                        size_t err_idx;
                        std::string err_reason;
                        if (!validate_template_arg_constraints(ti->params, model_args, err_idx, err_reason)) {
                            auto [code, msg] = format_constraint_error(
                                tpl_func->get_short_name(), ti->params, model_args, err_idx, err_reason);
                            logger_relay::error(code, lex::opt_any_lexeme{}, msg);
                            args_ok = false;
                        }
                    }
                    if (args_ok) {
                        auto parent_ns = _unit.get_root_namespace();
                        if (auto parent_elem = tpl_func->parent<element>()) {
                            if (auto owner_ns = std::dynamic_pointer_cast<ns>(parent_elem)) {
                                parent_ns = owner_ns;
                            }
                        }
                        auto concrete = template_instantiator::instantiate_function(
                            *tpl_func, model_args, parent_ns, _unit, _context, *this);
                        if (concrete) {
                            // Run the concrete function through the resolver pipeline
                            {
                                symbol_resolver sr(*this, _context, _unit);
                                concrete->accept(sr);
                            }
                            {
                                signature_resolver sigr(*this, _context, _unit);
                                concrete->accept(sigr);
                            }
                            concrete->accept(*this);

                            // Replace all_candidates with just the concrete instance
                            callee->set_target(concrete);
                            all_candidates.clear();
                            all_candidates.push_back(concrete);
                        }
                    }
                }
            }
        }

        // ── Template argument deduction (implicit template args) ──────────────
        // If the callee does NOT carry explicit template arguments, but there are
        // template candidates among all_candidates, attempt to deduce the template
        // arguments from the call-site argument types and instantiate.
        // Deduced instantiations compete with non-template candidates in overload
        // resolution (Step 5), but non-template candidates are always preferred
        // over deduced templates when both are viable with the same arity.
        if (callee && !callee->has_ast_template_args()) {
            // Collect template candidates
            std::vector<std::shared_ptr<function>> tpl_candidates;
            for (auto& cand : all_candidates) {
                if (cand->is_template()) {
                    tpl_candidates.push_back(cand);
                }
            }
            if (!tpl_candidates.empty()) {
                // Collect argument types from already-resolved argument expressions
                std::vector<std::shared_ptr<type>> arg_types;
                for (auto& arg : args) {
                    arg_types.push_back(arg ? arg->get_type() : nullptr);
                }

                // Check if a non-template candidate with matching arity exists
                size_t call_arity = arg_types.size();
                bool has_viable_non_template = false;
                for (auto& cand : all_candidates) {
                    if (!cand->is_template()) {
                        // Count non-this parameters
                        size_t param_count = 0;
                        for (auto& p : cand->parameters()) {
                            if (p != cand->get_this_parameter()) param_count++;
                        }
                        if (param_count == call_arity) {
                            has_viable_non_template = true;
                            break;
                        }
                    }
                }

                if (!has_viable_non_template) {
                    for (auto& tpl_func : tpl_candidates) {
                        auto* ti = tpl_func->get_tpl_info();
                        if (!ti) continue;

                        auto deduction = k::model::deduce_template_arguments(*ti, tpl_func->parameters(), arg_types);
                        if (!deduction.success) continue;

                        // Validate constraints
                        size_t err_idx;
                        std::string err_reason;
                        if (!validate_template_arg_constraints(ti->params, deduction.deduced_args, err_idx, err_reason)) {
                            continue; // Constraint violation — skip this candidate
                        }

                        // Instantiate the function with deduced arguments
                        auto parent_ns = _unit.get_root_namespace();
                        if (auto parent_elem = tpl_func->parent<element>()) {
                            if (auto owner_ns = std::dynamic_pointer_cast<ns>(parent_elem)) {
                                parent_ns = owner_ns;
                            }
                        }
                        auto concrete = template_instantiator::instantiate_function(
                            *tpl_func, deduction.deduced_args, parent_ns, _unit, _context, *this);
                        if (concrete) {
                            // Run through resolver pipeline
                            {
                                symbol_resolver sr(*this, _context, _unit);
                                concrete->accept(sr);
                            }
                            {
                                signature_resolver sigr(*this, _context, _unit);
                                concrete->accept(sigr);
                            }
                            concrete->accept(*this);

                            // Replace template candidate with concrete instance
                            callee->set_target(concrete);
                            all_candidates.erase(
                                std::remove(all_candidates.begin(), all_candidates.end(), tpl_func),
                                all_candidates.end());
                            all_candidates.push_back(concrete);
                            break; // Use first successful deduction
                        }
                    }
                }
                // Remove remaining uninstantiated template candidates
                all_candidates.erase(
                    std::remove_if(all_candidates.begin(), all_candidates.end(),
                        [](const std::shared_ptr<function>& f) { return f->is_template(); }),
                    all_candidates.end());
            }
        }

        // Step 4: If callee is a function pointer: validate argument types
        if (all_candidates.empty()) {
            if (callee->is_function()) {
                auto already_func = callee->get_function();
                expr.set_type(already_func->get_return_type());
                const auto& params = already_func->parameters();
                for (size_t n = 0; n < expr.arguments().size() && n < params.size(); ++n) {
                    auto arg = expr.arguments().at(n);
                    auto w = compute_cast_weight(arg, params[n]->get_type());
                    if (w != CAST_IMPOSSIBLE) {
                        auto cast = adapt_type(arg, params[n]->get_type());
                        if (cast && cast != arg) expr.assign_argument(n, cast);
                    }
                }
                // Phase 3: annotate — already_func is resolved but we have no member_callee here
                {
                    auto mc = std::dynamic_pointer_cast<member_of_object_expression>(expr.callee_expr());
                    annotate_dispatch_info(expr, already_func, mc);
                }
                // Exception contract check
                check_call_contract(*already_func, expr.first_lexeme());
                return;
            }

            // ── Temporary anonymous object construction: S(args...) ────────────
            // The callee name does not resolve to any function. Try to resolve it
            // as a struct/class type. If found, this is a temporary construction
            // expression: allocate a stack temporary, call the constructor, and
            // register for destructor cleanup.
            {
                std::shared_ptr<type> resolved_type;

                // ── Template temporary construction: S<Args...>(args...) ───────
                // If the callee carries explicit template arguments (e.g.
                // Optional<byte>(x)) and no template function matched, treat it as
                // a temporary construction of the instantiated template aggregate:
                // synthesise an unresolved template type reference and instantiate.
                if (callee->has_ast_template_args()) {
                    auto unres = _context->create_unresolved(callee->get_name());
                    unres->set_ast_template_args(callee->get_ast_template_args());
                    resolved_type = try_instantiate_template_type(unres, expr);
                }

                if (!resolved_type) {
                    resolved_type = resolve_type_by_name(callee->get_name(), expr);
                }
                if (resolved_type) {
                    auto resolved_nc = type::remove_const(resolved_type);
                    // Strip reference wrapper if present (resolve_type_by_name may return ref<struct>)
                    if (type::is_reference(resolved_nc))
                        resolved_nc = resolved_nc->get_subtype();
                    auto st_type = std::dynamic_pointer_cast<struct_type>(resolved_nc);
                    if (st_type && st_type->get_struct()) {
                        // Create a temporary_construction_expression
                        auto temp_expr = temporary_construction_expression::make_shared(
                            st_type, expr.arguments());
                        // Copy AST node for diagnostics
                        if (auto ast = expr.get_ast_expression()) {
                            temp_expr->set_ast_expression(ast);
                        }
                        // Visit it to resolve constructor + argument types
                        temp_expr->accept(*this);
                        // Signal to the caller to replace expr with temp_expr
                        _replacement_expr = temp_expr;
                        return;
                    }
                }
            }

            throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_INVOKE_AMBIGUOUS_OVERLOAD), expr.first_lexeme(),
                "No function named '{}' found in the current scope; "
                "check the spelling or add the appropriate declaration",
                {func_name});
        }

        // Step 5: Perform overload resolution via get_best_matching_function
        FunctionCandidate best = get_best_matching_function(all_candidates,
                                                            this_candidate ? rest_args : args,
                                                            this_candidate,
                                                            this_candidate ? &args : nullptr);
        bool is_free_to_member_call = false;

        // Step 6: Adapt arguments to match selected function's parameter types
        if (!best.func) {
            // get_best_matching_function already reported/threw an error
            return;
        }

        // Static constructors and destructors cannot be called explicitly
        if (std::dynamic_pointer_cast<static_constructor>(best.func)) {
            throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_INVOKE_METHOD_ARG_MISMATCH), expr.first_lexeme(),
                "Static constructor '{}' cannot be called explicitly; "
                "it is automatically invoked during program initialization",
                {best.func->get_short_name()});
        }
        if (std::dynamic_pointer_cast<static_destructor>(best.func)) {
            throw_error(static_cast<unsigned int>(k::diag::function_diag::ERR_FUNC_ACCESS_DENIED), expr.first_lexeme(),
                "Static destructor '~{}' cannot be called explicitly; "
                "it is automatically invoked during program finalization",
                {best.func->get_short_name()});
        }

        // Check visibility of the resolved function
        check_function_visibility(*best.func, expr);

        if (this_candidate && best.func->is_member() && !best.func->is_static() && !best.is_unified_call) {
            is_free_to_member_call = true;
        }

        callee->set_target(best.func);
        auto resolved_return_type = resolve_generic_call_return_type(*best.func, this_candidate);
        expr.set_type(resolved_return_type ? resolved_return_type : best.func->get_return_type());

        // If the resolved return type is still an unresolved template type — e.g. a
        // template-qualified static factory call like Optional<byte>::empty() whose
        // declared return type is Optional<T> → Optional<byte> — instantiate it so a
        // chained member access (.getOr(...)) sees a concrete struct type.
        if (auto ret_type = expr.get_type()) {
            auto bare_ret = type::remove_const(ret_type);
            if (auto unres_ret = std::dynamic_pointer_cast<unresolved_type>(bare_ret)) {
                if (unres_ret->has_template_args()) {
                    if (auto inst = try_instantiate_template_type(unres_ret, *best.func)) {
                        if (std::dynamic_pointer_cast<struct_type>(inst) || type::is_resolved(inst)) {
                            expr.set_type(type::is_const(ret_type) ? inst->get_const() : inst);
                        }
                    }
                }
            }
        }

        if (is_free_to_member_call) {
            // Member function found via free-function syntax: func(obj, args...)
            // (also covers qualified calls like Base::method(d))
            auto obj_expr = this_candidate; // first arg is the object

            // For a qualified call targeting a base-class method (e.g. Base::value(d)),
            // adapt the object expression to the expected 'this' type (upcast Derived→Base).
            if (is_qualified_call && best.func->is_member() && !best.func->is_static()) {
                // The implicit 'this' type is ref<OwningClass>.
                auto owner_st = best.func->parent<aggregate>();
                if (owner_st) {
                    auto owner_ref_type = owner_st->get_struct_type()
                        ? owner_st->get_struct_type()->get_reference()
                        : nullptr;
                    if (owner_ref_type) {
                        auto adapted_obj = adapt_type(obj_expr, owner_ref_type);
                        if (adapted_obj) obj_expr = adapted_obj;
                    }
                }
            }

            auto sym_for_member = symbol_expression::from_function(best.func);
            sym_for_member->set_target(best.func);
            auto member_expr = member_of_object_expression::make_shared(obj_expr, sym_for_member);
            expr.assign(member_expr, best.adapted_args);

            // A qualified call (e.g. Base::method(d)) must bypass virtual dispatch
            // and invoke the exact named function directly.
            if (is_qualified_call) {
                expr.set_non_virtual_qualified_call(true);
            }
        } else {
            // Regular/unified call — may include default values for trailing params
            expr.assign_arguments(best.adapted_args);
        }

        // Step 7: Annotate virtual dispatch info (for virtual method calls via vtable)
        // Phase 3: annotate dispatch info
        // After the potential rewrite above, re-read member_callee from the (possibly updated) callee.
        {
            auto updated_member_callee = std::dynamic_pointer_cast<member_of_object_expression>(expr.callee_expr());
            annotate_dispatch_info(expr, best.func, updated_member_callee);
        }

        // Exception contract check: the called function may declare a throws clause
        check_call_contract(*best.func, expr.first_lexeme());
        return;
    }
}

/**
 * Generate LLVM IR for a function invocation expression.
 *
 * Steps:
 *   1. Evaluate all argument expressions.
 *   2. If virtual dispatch: load vptr, GEP to vtable slot, indirect call.
 *   3. If direct call: resolve LLVM function, emit direct call instruction.
 *   4. If sret return: allocate temp or use _sret_destination, pass as first arg.
 *   5. Handle function pointer calls (indirect call through loaded pointer).
 *   6. Handle member function pointer calls (.* / ->*).
 *   7. Set _value to the call result.
 */
void implementation_generator::visit_function_invocation_expression(function_invocation_expression &expr) {
    // ── Union index() intrinsic ─────────────────────────────────────────────
    // Load the discriminant (uint32 at struct index 0) and return it as the
    // Kind enum value (same underlying representation).
    if (expr.is_union_index_intrinsic()) {
        auto member_callee = std::dynamic_pointer_cast<member_of_object_expression>(expr.callee_expr());
        if (member_callee) {
            // Visit the sub-expression to get a pointer to the union struct
            member_callee->sub_expr()->accept(*this);
            auto* union_ptr = _value;
            // If _value is a load (not a pointer), we need the address
            // The sub_expr should yield a pointer to the union struct { i32, [N x i8] }
            // Load the discriminant at GEP index 0
            auto* union_llvm_type = llvm::cast<llvm::StructType>(
                std::dynamic_pointer_cast<struct_type>(
                    type::remove_const(
                        type::is_reference(member_callee->sub_expr()->get_type())
                        ? member_callee->sub_expr()->get_type()->get_subtype()
                        : member_callee->sub_expr()->get_type())
                )->get_llvm_type());
            auto* disc_ptr = _builder->CreateStructGEP(union_llvm_type, union_ptr, 0, "union_idx_ptr");
            _value = _builder->CreateLoad(llvm::Type::getInt32Ty(_builder->getContext()), disc_ptr, "union_idx");
        }
        return;
    }

    auto callee = std::dynamic_pointer_cast<symbol_expression>(expr.callee_expr());
    auto member_callee = std::dynamic_pointer_cast<member_of_object_expression>(expr.callee_expr());
    auto pm_callee = std::dynamic_pointer_cast<pm_expression>(expr.callee_expr());

    if(!callee && !member_callee && !pm_callee) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F026), expr.first_lexeme(),
            "Internal error: unsupported call expression form during code generation; "
            "only direct, member and pointer-to-member function calls are supported");
    }

    // ── Helper state: track whether sret destination was consumed ──────────
    bool _sret_dest_was_consumed = false;

    // ── Helper lambda: handle sret call result ─────────────────────────────
    // For functions that return non-primitive types via sret, _value after the call
    // is the sret alloca pointer (already written by the callee).
    // If the struct has a destructor and this is a temporary, track it for cleanup.
    auto handle_sret_result = [&](llvm::Value* sret_ptr_val) {
        _value = sret_ptr_val;

        // If the sret destination was consumed from _sret_destination (variable init),
        // it's NOT a temporary — don't track it for cleanup (the variable owns it).
        if (_sret_dest_was_consumed) {
            _sret_dest_was_consumed = false;
            return;
        }

        // Track for temporary cleanup if the struct has a destructor
        if (!expr.get_type()) return;
        auto ret_type_nc = type::remove_const(expr.get_type());
        auto ret_st = std::dynamic_pointer_cast<struct_type>(ret_type_nc);
        if (!ret_st) return;
        auto st = ret_st->get_struct();
        if (st) {
            auto dtor = st->get_destructor();
            if (dtor) {
                auto dtor_fn = dtor->shared_as<k::model::function>();
                auto dtor_it = _context->_functions.find(dtor_fn);
                if (dtor_it != _context->_functions.end()) {
                    // Only track if the value is an AllocaInst (temporary)
                    if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(sret_ptr_val)) {
                        _expression_temporaries.push_back({alloca, dtor_it->second, nullptr});
                    }
                }
            }
        }
    };

    // ── Helper lambda: create or get sret destination for a call ──────────────
    // If _sret_destination is set (from variable_statement or return), use it directly.
    // Otherwise create a new temporary alloca.
    auto get_sret_ptr_for_call = [&]() -> llvm::Value* {
        if (_sret_destination) {
            // Caller provided a destination — use it directly (no temporary)
            llvm::Value* dest = _sret_destination;
            _sret_destination = nullptr; // consume it
            _sret_dest_was_consumed = true;
            return dest;
        }
        _sret_dest_was_consumed = false;
        // Create a temporary alloca for the sret result
        auto ret_type_nc = type::remove_const(expr.get_type());
        llvm::Type* llvm_ret = _context->get_llvm_type(ret_type_nc);
        llvm::Function* cur_fn = _builder->GetInsertBlock()->getParent();
        llvm::IRBuilder<> entry_builder(&cur_fn->getEntryBlock(), cur_fn->getEntryBlock().begin());
        return entry_builder.CreateAlloca(llvm_ret, nullptr, "sret_tmp");
    };

    // ── Helper lambda: emit a call with sret if needed ──────────────────────
    // Wraps CreateCall/CreateInvoke: if the callee uses sret ABI, prepend the sret pointer.
    // Uses invoke when inside a try-catch block for exception unwinding.
    // Returns the sret pointer (or nullptr if not sret).
    auto emit_sret_call = [&](llvm::FunctionType* fn_type, llvm::Value* callee_val,
                               std::vector<llvm::Value*>& call_args,
                               const std::string& name) -> llvm::Value* {
        bool callee_is_sret = fn_type->getReturnType()->isVoidTy()
            && expr.get_type() && needs_sret_return(expr.get_type());
        if (callee_is_sret) {
            llvm::Value* sret_ptr = get_sret_ptr_for_call();
            call_args.insert(call_args.begin(), sret_ptr);

            // Rebuild fn_type with the sret param prepended
            std::vector<llvm::Type*> param_types;
            param_types.push_back(llvm::PointerType::get(**_context, 0));
            for (auto* pt : fn_type->params())
                param_types.push_back(pt);
            auto* sret_fn_type = llvm::FunctionType::get(
                llvm::Type::getVoidTy(**_context), param_types, false);

            create_call_or_invoke(sret_fn_type, callee_val, call_args);
            return sret_ptr;
        }
        // Non-sret call
        _value = create_call_or_invoke(fn_type, callee_val, call_args,
            fn_type->getReturnType()->isVoidTy() ? "" : name);
        return nullptr;
    };

    // ── INDIRECT_MEMBER call via pointer-to-member  obj.*mfp(args) ────────────
    if (expr.has_dispatch_info() &&
        expr.get_dispatch_info().kind == virtual_dispatch_info::dispatch_kind::INDIRECT_MEMBER) {
        // pm_callee->left() = object expression (this), pm_callee->right() = mfp variable
        if (!pm_callee) {
            throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F048), expr.first_lexeme(),
                "Internal error: INDIRECT_MEMBER dispatch without a pm_expression callee");
        }

        // 1. Evaluate the object (this) pointer
        _value = nullptr;
        pm_callee->left()->accept(*this);
        llvm::Value* this_val = _value;

        // If the object is a ref/indirection, load the actual pointer
        auto obj_type = pm_callee->left()->get_type();
        if (auto ref = std::dynamic_pointer_cast<reference_type>(obj_type)) {
            auto inner = ref->get_subtype();
            if (pm_callee->is_arrow()) {
                // ->*: load the pointer value from the ref, then we have a ptr-to-struct
                this_val = _builder->CreateLoad(_context->get_llvm_type(inner), this_val, "pm_ptr_load");
                // Null-check for nullable indirections
                if (std::dynamic_pointer_cast<pointer_type>(inner) ||
                    std::dynamic_pointer_cast<view_type>(inner)) {
                    set_debug_location(expr.first_lexeme());
                    auto* fatal = get_or_declare_fatal_null_function("__k_fatal_null_dereference");
                    emit_null_check(this_val, fatal, "pm_arrow");
                }
            }
            // For .*, this_val is already the struct alloca address (which is what we want as `this`)
        }

        // 2. Evaluate the member function pointer variable (load fn pointer)
        _value = nullptr;
        pm_callee->right()->accept(*this);
        llvm::Value* mfp_alloca = _value;

        auto mfp_type = pm_callee->right()->get_type();
        if (auto ref = std::dynamic_pointer_cast<reference_type>(mfp_type)) {
            mfp_type = ref->get_subtype();
        }
        auto frt = std::dynamic_pointer_cast<function_reference_type>(mfp_type);
        if (!frt || !mfp_alloca) {
            throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F049), expr.first_lexeme(),
                "Internal error: INDIRECT_MEMBER call: could not obtain function pointer");
        }

        // Load the actual function pointer from the alloca
        llvm::Type* frt_llvm = frt->get_llvm_type();
        llvm::Value* fn_ptr = _builder->CreateLoad(frt_llvm, mfp_alloca, "mfp_fn_ptr");

        // 3. Build LLVM function type from frt parameter types
        //    For member_function_reference_type the first param is implicit `this` (ptr)
        std::vector<llvm::Type*> param_llvm_types;
        // Always prepend `this` as opaque ptr
        param_llvm_types.push_back(llvm::PointerType::getUnqual(**_context));
        for (const auto& pt : frt->get_parameter_types()) {
            // Skip the implicit this param if it's already in get_parameter_types()
            auto llt = _context->get_llvm_type(pt);
            if (!llt) continue;
            param_llvm_types.push_back(llt);
        }
        llvm::Type* ret_llvm = frt->get_return_type()
            ? _context->get_llvm_type(frt->get_return_type())
            : llvm::Type::getVoidTy(**_context);
        auto llvm_fn_type = llvm::FunctionType::get(ret_llvm, param_llvm_types, false);

        // 4. Build call arguments: this_val first, then expression arguments
        std::vector<llvm::Value*> call_args;
        call_args.push_back(this_val);
        for (auto& arg : expr.arguments()) {
            _value = nullptr;
            arg->accept(*this);
            if (_value) call_args.push_back(_value);
        }

        auto* sret_result = emit_sret_call(llvm_fn_type, fn_ptr, call_args, "mfp_call");
        if (sret_result) {
            handle_sret_result(sret_result);
        }
        return;
    }

    // ── INDIRECT call via function-reference variable ─────────────────────────
    if (expr.has_dispatch_info() &&
        expr.get_dispatch_info().kind == virtual_dispatch_info::dispatch_kind::INDIRECT) {
        // callee is a symbol_expression that holds a variable of function_reference_type.
        // We already visited the callee in type_reference_resolver, so its type is set.
        // In impl_gen, visiting a variable symbol gives us the *address* of the variable (alloca).
        // For function-reference variables, we must load the function pointer from that address.
        _value = nullptr;
        if (callee) callee->accept(*this);
        llvm::Value* var_addr = _value;
        if (!var_addr) {
            throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F044), expr.first_lexeme(),
                "Internal error: indirect call through function reference produced no LLVM value");
        }

        // Build the LLVM function type from the function_reference_type in the call expression.
        auto callee_type = callee ? callee->get_type() : nullptr;
        auto inner_type = callee_type;
        while (inner_type && (type::is_reference(inner_type) || type::is_link(inner_type) ||
                               type::is_pointer(inner_type) || type::is_view(inner_type))) {
            inner_type = inner_type->get_subtype();
        }
        auto frt = std::dynamic_pointer_cast<function_reference_type>(inner_type);
        if (!frt) {
            throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F045), expr.first_lexeme(),
                "Internal error: indirect call without a function_reference_type annotation");
        }

        // The function_reference_type has a ptr (opaque pointer) as its LLVM type.
        // Load the actual function pointer from the variable's address.
        llvm::Type* frt_llvm = _context->get_llvm_type(inner_type); // = opaque ptr
        llvm::Value* fn_ptr = _builder->CreateLoad(frt_llvm, var_addr, "fn_ptr_load");

        // Build LLVM parameter types from the function_reference_type
        std::vector<llvm::Type*> param_llvm_types;
        for (const auto& pt : frt->get_parameter_types()) {
            auto llt = _context->get_llvm_type(pt);
            if (!llt) {
                throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F046), expr.first_lexeme(),
                    "Internal error: could not map K parameter type to LLVM type for indirect call");
            }
            param_llvm_types.push_back(llt);
        }
        llvm::Type* ret_llvm_type = frt->get_return_type()
            ? _context->get_llvm_type(frt->get_return_type())
            : llvm::Type::getVoidTy(**_context);
        auto llvm_fn_type = llvm::FunctionType::get(ret_llvm_type, param_llvm_types, false);

        // Generate arguments
        std::vector<llvm::Value*> call_args;
        for (auto& arg : expr.arguments()) {
            _value = nullptr;
            arg->accept(*this);
            if (!_value) {
                throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F047), expr.first_lexeme(),
                    "Internal error: an argument for an indirect call produced no LLVM value");
            }
            call_args.push_back(_value);
        }

        auto* sret_result = emit_sret_call(llvm_fn_type, fn_ptr, call_args, "ind_call");
        if (sret_result) {
            handle_sret_result(sret_result);
        }
        return;
    }

    // Generate arguments and add them to the args list (for non-indirect calls)
    std::vector<llvm::Value*> args;
    if (member_callee) {
        callee = std::dynamic_pointer_cast<symbol_expression>(member_callee->symbol().shared_as<symbol_expression>());
        if (!callee) {
            throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F027), expr.first_lexeme(),
                "Internal error: member function call has a non-symbol callee; "
                "this should have been rejected during type resolution");
        }

        // First argument is the object pointer (this)
        member_callee->sub_expr()->accept(*this);
        if(!_value) {
            throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F028), expr.first_lexeme(),
                "Internal error: failed to generate the 'this' argument for member function call '{}'; "
                "the object expression produced no LLVM value",
                {callee ? callee->get_name().to_string() : "<unknown>"});
        }

        // If the object is an owner (ref<owner<T>>), load the owned pointer
        // so we pass the actual object address (not the alloca of the owner slot).
        auto sub_type = member_callee->sub_expr()->get_type();
        if (type::is_reference(sub_type)) {
            auto inner = std::dynamic_pointer_cast<reference_type>(sub_type)->get_subtype();
            if (type::is_owner(inner)) {
                _value = _builder->CreateLoad(
                    llvm::PointerType::get(_builder->getContext(), 0), _value, "owner_this_load");
            }
        }

        args.push_back(_value);
    }
    // Save outer _sret_destination — it's meant for the call result, not for arguments
    llvm::Value* saved_sret_destination = _sret_destination;
    _sret_destination = nullptr;

    // Step 1: Evaluate all argument expressions
    for(auto arg : expr.arguments()) {
        _value = nullptr;

        // ── Argument copy elision for by-value struct parameters ──────────
        // When a by-value struct argument is the direct result of a sret-
        // returning function call (prvalue), set _sret_destination so the
        // inner call writes directly into a staging alloca without tracking
        // it as a temporary. This avoids an extra destructor call.
        bool arg_elision_set = false;
        bool arg_is_struct = arg->get_type() && type::is_struct(arg->get_type())
            && !type::is_reference(arg->get_type())
            && !type::is_any_indirection(arg->get_type());
        bool arg_is_fn_call = std::dynamic_pointer_cast<function_invocation_expression>(arg) != nullptr;
        if (arg_is_struct
            && needs_sret_return(arg->get_type())
            && !_sret_destination
            && arg_is_fn_call)
        {
            auto st_type_nc = type::remove_const(arg->get_type());
            llvm::Type* llvm_st = _context->get_llvm_type(st_type_nc);
            llvm::Function* cur_fn = _builder->GetInsertBlock()->getParent();
            llvm::IRBuilder<> entry_builder(&cur_fn->getEntryBlock(), cur_fn->getEntryBlock().begin());
            auto* staging_alloca = entry_builder.CreateAlloca(llvm_st, nullptr, "arg_staging");
            _sret_destination = staging_alloca;
            arg_elision_set = true;
        }

        arg->accept(*this);

        // Only clear _sret_destination if WE set it (and it wasn't consumed)
        if (arg_elision_set && _sret_destination) {
            _sret_destination = nullptr;
        }

        if(!_value) {
            throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F029), expr.first_lexeme(),
                "Internal error: a call argument for '{}' produced no LLVM value during code generation; "
                "this indicates a bug in expression code generation",
                {callee ? callee->get_name().to_string() : "<unknown>"});
        }
        // If the argument is a struct rvalue (bare struct type, not ref) and _value is
        // an alloca (pointer), we need to load the aggregate to pass it by value.
        // This happens when a function return value is materialized into an alloca.
        if (arg->get_type() && type::is_struct(arg->get_type())) {
            if (auto* alloca = llvm::dyn_cast<llvm::AllocaInst>(_value)) {
                auto st_type_nc = type::remove_const(arg->get_type());
                llvm::Type* llvm_st = _context->get_llvm_type(st_type_nc);
                _value = _builder->CreateLoad(llvm_st, alloca, "struct_arg_load");
            }
        }
        args.push_back(_value);
    }

    // Restore outer _sret_destination for the call result
    _sret_destination = saved_sret_destination;

    // ── Varargs packing ─────────────────────────────────────────────────────
    // If the called function has a varargs last parameter, pack trailing
    // arguments into a stack-allocated array { i32 count, [N x T] data }.
    {
        auto va_function = callee->get_function();
        if (va_function && va_function->has_varargs()) {
            const auto& va_params = va_function->parameters();
            // Number of fixed parameters (excluding varargs param, but including 'this' in args)
            size_t this_offset = (member_callee && va_function->is_member() && !va_function->is_static()) ? 1 : 0;
            size_t n_fixed = va_params.size() - 1; // model params exclude 'this'
            size_t n_fixed_in_args = n_fixed + this_offset;

            auto varargs_param = va_params.back();
            auto varargs_param_type = varargs_param->get_type();
            // Unwrap reference wrapper if present (parameter type may be ref<T[]>)
            if (type::is_reference(varargs_param_type))
                varargs_param_type = varargs_param_type->get_subtype();
            auto varargs_array_type = std::dynamic_pointer_cast<array_type>(varargs_param_type);
            if (varargs_array_type) {
                auto elem_type = varargs_array_type->get_subtype();
                llvm::Type* llvm_elem_type = _context->get_llvm_type(elem_type);

                size_t n_varargs = (args.size() > n_fixed_in_args) ? (args.size() - n_fixed_in_args) : 0;

                // Check if a single explicit array was passed (no packing needed)
                bool is_direct_array_pass = false;
                if (n_varargs == 1 && args.size() == n_fixed_in_args + 1) {
                    // Check if the last expression arg in the model is already an array type
                    auto& last_model_arg = expr.arguments().back();
                    if (last_model_arg->get_type()) {
                        auto arg_type = last_model_arg->get_type();
                        // Unwrap reference if present (variables are ref<T>)
                        if (type::is_reference(arg_type))
                            arg_type = arg_type->get_subtype();
                        if (type::is_array(arg_type)) {
                            is_direct_array_pass = true;
                        }
                    }
                }

                if (!is_direct_array_pass) {
                    // Pack trailing args into a sized array on the stack
                    auto sized_arr_type = varargs_array_type->with_size(n_varargs);
                    llvm::Type* llvm_arr_struct = _context->get_llvm_type(sized_arr_type);

                    llvm::Function* cur_fn = _builder->GetInsertBlock()->getParent();
                    llvm::IRBuilder<> entry_builder(&cur_fn->getEntryBlock(), cur_fn->getEntryBlock().begin());
                    llvm::Value* arr_alloca = entry_builder.CreateAlloca(llvm_arr_struct, nullptr, "varargs_pack");

                    // Store count in field 0
                    auto* count_ptr = _builder->CreateStructGEP(llvm_arr_struct, arr_alloca, array_type::FIELD_SIZE, "varargs_count_ptr");
                    _builder->CreateStore(
                        llvm::ConstantInt::get(llvm::Type::getInt32Ty(**_context), n_varargs),
                        count_ptr);

                    // Store each trailing arg into the data array (field 1)
                    if (n_varargs > 0) {
                        auto* llvm_data_arr_type = sized_arr_type->get_llvm_data_array_type();
                        auto* data_ptr = _builder->CreateStructGEP(llvm_arr_struct, arr_alloca, array_type::FIELD_DATA, "varargs_data_ptr");
                        for (size_t i = 0; i < n_varargs; ++i) {
                            auto* elem_ptr = _builder->CreateConstInBoundsGEP2_32(
                                llvm_data_arr_type, data_ptr, 0, static_cast<unsigned>(i), "varargs_elem_ptr");
                            _builder->CreateStore(args[n_fixed_in_args + i], elem_ptr);
                        }
                    }

                    // Replace trailing args with the array pointer
                    args.resize(n_fixed_in_args);
                    args.push_back(arr_alloca);
                }
            }
        }
    }

    // Find the function definition
    auto function = callee->get_function();
    auto it = _context->_functions.find(function);
    if(it==_context->_functions.end()) {
        // Late materialization path for functions that appear after the main
        // declaration walk (e.g. template-instantiated methods).
        if (function) {
            std::vector<llvm::Type*> late_param_types;
            if (function->is_member() && !function->is_static()) {
                auto this_param = function->get_this_parameter();
                if (this_param && this_param->get_type()) {
                    if (auto* this_ty = _context->get_llvm_type(this_param->get_type())) {
                        late_param_types.push_back(this_ty);
                    }
                }
            }
            bool bad_param = false;
            for (const auto& p : function->parameters()) {
                auto* pty = _context->get_llvm_type(p->get_type());
                if (!pty) {
                    bad_param = true;
                    break;
                }
                late_param_types.push_back(pty);
            }

            // Fallback: derive the function signature directly from generated call args.
            // This is safe for codegen at this call-site and avoids hard-failing on
            // late-instantiated methods whose model parameter types are still opaque.
            if (bad_param || late_param_types.size() != args.size()) {
                late_param_types.clear();
                late_param_types.reserve(args.size());
                for (auto* av : args) {
                    if (!av) {
                        bad_param = true;
                        break;
                    }
                    late_param_types.push_back(av->getType());
                }
                bad_param = late_param_types.empty();
            }

            if (!bad_param) {
                llvm::Type* late_ret_type = nullptr;
                bool late_use_sret = false;
                if (function->has_return_type()) {
                    if (needs_sret_return(function->get_return_type())) {
                        late_param_types.insert(late_param_types.begin(), llvm::PointerType::get(**_context, 0));
                        late_ret_type = llvm::Type::getVoidTy(**_context);
                        late_use_sret = true;
                    } else {
                        late_ret_type = _context->get_llvm_type(function->get_return_type());
                        if (!late_ret_type && expr.get_type()) {
                            late_ret_type = _context->get_llvm_type(expr.get_type());
                        }
                    }
                } else {
                    late_ret_type = llvm::Type::getVoidTy(**_context);
                }

                if (late_ret_type) {
                    llvm::FunctionType* late_ft = llvm::FunctionType::get(late_ret_type, late_param_types, false);
                    llvm::Function* late_fn = _context->_module->getFunction(function->get_mangled_name());
                    if (!late_fn) {
                        late_fn = llvm::Function::Create(
                            late_ft, llvm::Function::ExternalLinkage,
                            function->get_mangled_name(), *_context->_module);
                    }
                    if (late_use_sret) {
                        late_fn->addParamAttr(0, llvm::Attribute::get(**_context, llvm::Attribute::StructRet,
                            _context->get_llvm_type(function->get_return_type())));
                    }
                    _context->_functions.insert({function, late_fn});
                    it = _context->_functions.find(function);
                }
            }
        }

        // Abstract virtual functions and imported virtual functions without a
        // concrete LLVM declaration can still be dispatched through the vtable.
        if (function && function->is_virtual() &&
            (function->is_abstract_func() || function->is_external())) {
            // Fall through: llvm_func will remain null; virtual dispatch handles it below.
        } else {
            throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F02A), expr.first_lexeme(),
                "Internal error: LLVM declaration not found for function '{}' during code generation; "
                "the declaration pass must be run before the implementation pass",
                {function ? function->get_fq_name() : "<null>"});
        }
    }
    llvm::Function* llvm_func = (it != _context->_functions.end()) ? it->second : nullptr;
    if(llvm_func == nullptr &&
       !(function && (function->is_abstract_func() ||
                      (function->is_virtual() && function->is_external())))) {
        throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F02B), expr.first_lexeme(),
            "Internal error: LLVM function object is null for '{}'; "
            "this indicates a compiler bug in the declaration pass",
            {function ? function->get_fq_name() : "<null>"});
    }

    // ── Virtual dispatch ─────────────────────────────────────────────────────
    // Phase 3/4: dispatch_info is normally set by type_reference_resolver.
    // If absent (e.g. a synthetic node that bypassed the resolver), treat as DIRECT.
    const bool is_vtable_dispatch =
        expr.has_dispatch_info()
        && expr.get_dispatch_info().kind == virtual_dispatch_info::dispatch_kind::VTABLE
        && (expr.get_dispatch_info().dispatch_class != nullptr
            || expr.get_dispatch_info().imported_dispatch_agg != nullptr);

    if (is_vtable_dispatch) {
        const auto& di = expr.get_dispatch_info();

        // ── Local klass dispatch ──────────────────────────────────────────
        auto kl = di.dispatch_class;
        if (kl && kl->has_vtable() && !args.empty()) {
            llvm::FunctionType* fn_type = nullptr;
            if (llvm_func) {
                fn_type = llvm_func->getFunctionType();
            } else {
                std::vector<llvm::Type*> param_types;
                // For sret: prepend sret pointer parameter
                if (function->has_return_type() && needs_sret_return(function->get_return_type()))
                    param_types.push_back(llvm::PointerType::get(**_context, 0));
                if (function->is_member() && !function->is_static())
                    param_types.push_back(_context->get_llvm_type(function->get_this_parameter()->get_type()));
                for (const auto& param : function->parameters())
                    param_types.push_back(_context->get_llvm_type(param->get_type()));
                llvm::Type* ret_type_llvm = llvm::Type::getVoidTy(**_context);
                if (function->has_return_type() && !needs_sret_return(function->get_return_type()))
                    ret_type_llvm = _context->get_llvm_type(function->get_return_type());
                fn_type = llvm::FunctionType::get(ret_type_llvm, param_types, false);
            }
            // Check if sret ABI is used
            bool call_uses_sret = fn_type->getReturnType()->isVoidTy()
                && expr.get_type() && needs_sret_return(expr.get_type());
            if (call_uses_sret) {
                llvm::Value* sret_alloca = get_sret_ptr_for_call();
                args.insert(args.begin(), sret_alloca);
                _value = emit_virtual_dispatch_call(*_builder, *kl, args[1], di.slot_index, fn_type, args, _context, "");
                handle_sret_result(sret_alloca);
            } else {
                _value = emit_virtual_dispatch_call(*_builder, *kl, args[0], di.slot_index, fn_type, args, _context, "");
            }
            return;
        }

        // ── Imported aggregate dispatch (imported_klass / imported_interface) ──
        // The LLVM struct type was interned from llvm_def — field 0 is always the
        // primary vptr.  The vtable layout is:  { RTTI ptr, slot0 ptr, slot1 ptr, … }
        // so the function pointer is at index  (slot_index + 1).
        auto imp_agg = di.imported_dispatch_agg;
        if (imp_agg && imp_agg->has_vtable() && !args.empty()) {
            // Build the callee FunctionType from the LLVM declaration if we have it,
            // or reconstruct from K model types as fallback.
            llvm::FunctionType* fn_type = nullptr;
            if (llvm_func) {
                fn_type = llvm_func->getFunctionType();
            } else if (function) {
                std::vector<llvm::Type*> param_types;
                if (function->has_return_type() && needs_sret_return(function->get_return_type()))
                    param_types.push_back(llvm::PointerType::get(**_context, 0));
                if (function->is_member() && !function->is_static() && function->get_this_parameter())
                    param_types.push_back(_context->get_llvm_type(function->get_this_parameter()->get_type()));
                for (const auto& param : function->parameters())
                    param_types.push_back(_context->get_llvm_type(param->get_type()));
                llvm::Type* ret_type_llvm = llvm::Type::getVoidTy(**_context);
                if (function->has_return_type() && !needs_sret_return(function->get_return_type()))
                    ret_type_llvm = _context->get_llvm_type(function->get_return_type());
                fn_type = llvm::FunctionType::get(ret_type_llvm, param_types, false);
            }
            if (!fn_type) {
                throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F02F), expr.first_lexeme(),
                    "Internal error: cannot build FunctionType for imported virtual dispatch of '{}'",
                    {function ? function->get_fq_name() : "<null>"});
            }

            auto* struct_llvm_type = imp_agg->get_struct_type()
                                     ? imp_agg->get_struct_type()->get_llvm_type() : nullptr;
            if (!struct_llvm_type) {
                throw_error(static_cast<unsigned int>(k::diag::codegen_diag::INTERNAL_ERR_F030), expr.first_lexeme(),
                    "Internal error: imported aggregate '{}' has no LLVM struct type",
                    {imp_agg->get_fq_name()});
            }

            llvm::LLVMContext& llvm_ctx = **_context;
            llvm::Type* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);

            // Find the vptr field index from the KDI layout (first kdi_layout_vptr).
            uint32_t vptr_field_index = 0; // default: field 0 (primary vptr)
            auto imp_agg_cast = std::dynamic_pointer_cast<imported_aggregate>(imp_agg);
            if (imp_agg_cast) {
                const auto* kdi_agg = imp_agg_cast->get_kdi_aggregate();
                if (kdi_agg) {
                    for (const auto& lf : kdi_agg->layout) {
                        if (auto* vp = std::get_if<kdi::kdi_layout_vptr>(&lf)) {
                            vptr_field_index = vp->llvm_field_index;
                            break;
                        }
                    }
                }
            }

            // Step 2: If virtual dispatch: load vptr, GEP to vtable slot, indirect call
            // Load the vptr
            llvm::Value* vptr_addr = _builder->CreateStructGEP(
                struct_llvm_type, args[0], vptr_field_index, "imp_vptr_addr");
            llvm::Value* vptr = _builder->CreateLoad(ptr_ty, vptr_addr, "imp_vptr");

            // Step 3: If direct call: resolve LLVM function, emit direct call instruction
            // The vtable layout is { RTTI, fn0, fn1, … } so slot i → index i+1.
            // We use a byte-offset GEP because we don't have the vtable's StructType.
            // On all supported 64-bit targets a pointer is 8 bytes.
            const uint64_t ptr_size = 8;
            llvm::Value* slot_offset = llvm::ConstantInt::get(
                llvm::Type::getInt64Ty(llvm_ctx),
                (di.slot_index + 1) * ptr_size);
            llvm::Value* fn_ptr_addr = _builder->CreateInBoundsGEP(
                llvm::Type::getInt8Ty(llvm_ctx), vptr, slot_offset, "imp_vtbl_slot");
            llvm::Value* fn_ptr = _builder->CreateLoad(ptr_ty, fn_ptr_addr, "imp_fn_ptr");
            bool call_uses_sret = fn_type->getReturnType()->isVoidTy()
                && expr.get_type() && needs_sret_return(expr.get_type());
            if (call_uses_sret) {
                llvm::Value* sret_alloca = get_sret_ptr_for_call();
                // Step 4: If sret return: allocate temp or use _sret_destination, pass as first arg
                args.insert(args.begin(), sret_alloca);
                create_call_or_invoke(fn_type, fn_ptr, args);
                handle_sret_result(sret_alloca);
            } else {
                _value = create_call_or_invoke(fn_type, fn_ptr, args,
                    fn_type->getReturnType()->isVoidTy() ? "" : "imp_vcall");
            }
            return;
        }
    }
    // ── Direct call (non-virtual, or qualified, or free function) ────────────
    bool call_uses_sret = llvm_func->getReturnType()->isVoidTy()
        && expr.get_type() && needs_sret_return(expr.get_type());
    if (call_uses_sret) {
        llvm::Value* sret_alloca = get_sret_ptr_for_call();
        args.insert(args.begin(), sret_alloca);
        create_call_or_invoke(llvm_func->getFunctionType(), llvm_func, args);
        handle_sret_result(sret_alloca);
    } else {
        _value = create_call_or_invoke(llvm_func->getFunctionType(), llvm_func, args);
    }
}

//
// Constructor invocation
//

void symbol_resolver::visit_constructor_invocation_expression(constructor_invocation_expression& expr) {
    for (auto arg : expr.arguments()) {
        arg->accept(*this);
    }
}

void symbol_resolver::visit_temporary_construction_expression(temporary_construction_expression& expr) {
    for (auto& arg : expr.arguments()) {
        if (arg) arg->accept(*this);
    }
}

//
// New expression
//

void symbol_resolver::visit_new_expression(new_expression& expr) {
    if (expr.is_uniform_array()) {
        if (expr.array_size_expr()) expr.array_size_expr()->accept(*this);
        for (auto& a : expr.uniform_ctor_args()) {
            if (a) a->accept(*this);
        }
    } else if (expr.is_array()) {
        if (expr.array_size_expr()) expr.array_size_expr()->accept(*this);
        for (auto& e : expr.array_init_elements()) {
            if (e) e->accept(*this);
        }
    } else {
        for (auto& arg : expr.arguments()) {
            arg->accept(*this);
        }
    }
}

/**
 * Resolve a new expression (heap allocation): type resolution, constructor resolution.
 *
 * Steps:
 *   1. Resolve the target type (class/struct or array).
 *   2. For arrays: validate size expression, set result type to owner<array<T>>.
 *   3. For structs: resolve constructor overload with provided arguments.
 *   4. Set result type to owner<T>.
 */
void type_reference_resolver::visit_new_expression(new_expression& expr) {
    if (expr.is_uniform_array()) {
        // ── Uniform array form: new T(args)[N] ──

        // Resolve uniform ctor args
        for (auto& a : expr._uniform_ctor_args) {
            if (a) a->accept(*this);
        }

        // Resolve the array size expression
        if (expr._array_size_expr) {
            expr._array_size_expr->accept(*this);
        }

        // Resolve the element type
        auto elem_type = expr.allocated_type();
        if (!type::is_resolved(elem_type)) {
            if (auto unres = std::dynamic_pointer_cast<unresolved_type>(elem_type)) {
                auto resolved = resolve_type_by_name(unres->type_id(), static_cast<const element&>(expr));
                if (!resolved || !type::is_resolved(resolved)) {
                    auto imported_agg = _unit.get_or_create_imported_aggregate(unres->type_id(), _context);
                    if (imported_agg) resolved = imported_agg->get_struct_type();
                }
                if (resolved && type::is_resolved(resolved)) {
                    expr.allocated_type(resolved);
                    elem_type = resolved;
                }
            }
        }
        if (!type::is_resolved(elem_type)) {
            throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_NEW_TYPE_NOT_FOUND), expr.first_lexeme(),
                "Cannot resolve element type of 'new' uniform array expression: type '{}' is unknown",
                {elem_type ? elem_type->to_string() : "<null>"});
            return;
        }

        // Determine the array size (static or dynamic)
        size_t arr_size = 0;
        bool is_dynamic = false;

        if (expr._array_size_expr) {
            auto size_val = std::dynamic_pointer_cast<value_expression>(expr._array_size_expr);
            if (size_val && size_val->is_literal()
                && std::holds_alternative<lex::integer>(size_val->any_literal())) {
                auto& int_lit = size_val->any_literal().get<lex::integer>();
                arr_size = int_lit.to_unsigned_int();
            } else {
                // Dynamic size
                is_dynamic = true;
                auto uint_type = _context->from_type(primitive_type::UNSIGNED_INT);
                auto adapted_size = adapt_type(expr._array_size_expr, uint_type);
                if (!adapted_size) {
                    throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_ARRAY_ALLOC_SIZE_NOT_INT), expr.first_lexeme(),
                        "Uniform array size expression must be convertible to an unsigned integer; "
                        "expression has type '{}'",
                        {expr._array_size_expr->get_type() ? expr._array_size_expr->get_type()->to_string() : "?"});
                    return;
                }
                if (adapted_size != expr._array_size_expr) {
                    expr._array_size_expr = adapted_size;
                    adapted_size->set_parent_expression(expr.shared_as<expression>());
                }
            }
        }

        // Check for abstract types
        if (auto st_type = std::dynamic_pointer_cast<struct_type>(elem_type)) {
            auto struct_model = st_type->get_struct();
            if (struct_model && struct_model->is_abstract()) {
                throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_ARRAY_ALLOC_NOT_POINTER), expr.first_lexeme(),
                    "Cannot create uniform array of abstract class '{}'",
                    {struct_model->get_short_name()});
                return;
            }
        }

        // Step 1: Resolve the target type (class/struct or array)
        // Resolve the constructor / type-check for the uniform args
        if (auto st_type = std::dynamic_pointer_cast<struct_type>(elem_type)) {
            auto struct_model = st_type->get_struct();
            if (!struct_model) {
                throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_ARRAY_ELEM_ABSTRACT), expr.first_lexeme(),
                    "Cannot resolve struct for uniform 'new {}(...)[]': aggregate not resolved",
                    {st_type->to_string()});
                return;
            }
            auto [best_ctor, adapted_args] = get_best_matching_constructor(
                struct_model->constructors(), expr._uniform_ctor_args);
            if (!best_ctor) {
                throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_ARRAY_ALLOC_NOT_ARRAY), expr.first_lexeme(),
                    "No matching constructor for uniform array init of type '{}'",
                    {st_type->to_string()});
                return;
            }
            check_constructor_visibility(*best_ctor, expr);
            check_call_contract(*best_ctor, expr.first_lexeme());
            expr._uniform_constructor = best_ctor;
            expr.set_uniform_ctor_args(adapted_args);
        } else if (type::is_primitive(elem_type)) {
            // Primitive: must have exactly one arg convertible to the element type
            if (expr._uniform_ctor_args.size() > 1) {
                throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_ARRAY_ALLOC_TYPE_MISMATCH), expr.first_lexeme(),
                    "Uniform array init for primitive type '{}' expects at most one argument, got {}",
                    {elem_type->to_string(), std::to_string(expr._uniform_ctor_args.size())});
                return;
            }
            if (!expr._uniform_ctor_args.empty() && expr._uniform_ctor_args[0]) {
                auto cast = adapt_type(expr._uniform_ctor_args[0], elem_type);
                if (!cast) {
                    throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_ARRAY_ALLOC_TYPE_MISMATCH), expr.first_lexeme(),
                        "Cannot convert uniform init value to primitive element type '{}'",
                        {elem_type->to_string()});
                    return;
                }
                if (cast != expr._uniform_ctor_args[0]) {
                    expr.assign_uniform_ctor_arg(0, cast);
                }
            }
        }

        if (is_dynamic) {
            expr._is_dynamic_size = true;
            expr._array_size = 0;
            auto arr_type_unsized = elem_type->get_array();
            expr.set_type(arr_type_unsized->get_owner());
        } else {
            expr._array_size = arr_size;
            auto arr_type_unsized = elem_type->get_array();
            auto sized_arr_type = arr_type_unsized->with_size(arr_size);
            expr.set_type(sized_arr_type->get_owner());
        }
        return;
    }

    if (expr.is_array()) {
        // ── Array form: new T[N]{e0, e1, ...} ──

        // Resolve array size expression
        if (expr._array_size_expr) {
            expr._array_size_expr->accept(*this);
        }

        // Resolve element init expressions
        // For function_invocation_expression elements, only resolve their arguments
        // (the callee is a struct name, not a function — constructor resolution happens below)
        for (size_t i = 0; i < expr._array_init_elements.size(); ++i) {
            if (auto& e = expr._array_init_elements[i]) {
                auto func_inv = std::dynamic_pointer_cast<function_invocation_expression>(e);
                if (func_inv) {
                    // Only resolve the arguments, not the callee
                    for (auto& arg : func_inv->arguments()) {
                        if (arg) arg->accept(*this);
                    }
                } else {
                    e->accept(*this);
                }
            }
        }

        // Resolve the element type
        auto elem_type = expr.allocated_type();
        if (!type::is_resolved(elem_type)) {
            if (auto unres = std::dynamic_pointer_cast<unresolved_type>(elem_type)) {
                auto resolved = resolve_type_by_name(unres->type_id(), static_cast<const element&>(expr));
                if (!resolved || !type::is_resolved(resolved)) {
                    auto imported_agg = _unit.get_or_create_imported_aggregate(unres->type_id(), _context);
                    if (imported_agg) resolved = imported_agg->get_struct_type();
                }
                if (resolved && type::is_resolved(resolved)) {
                    expr.allocated_type(resolved);
                    elem_type = resolved;
                }
            }
        }
        if (!type::is_resolved(elem_type)) {
            throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_NEW_TYPE_NOT_FOUND), expr.first_lexeme(),
                "Cannot resolve element type of 'new[]' expression: type '{}' is unknown",
                {elem_type ? elem_type->to_string() : "<null>"});
            return;
        }

        // Determine the array size
        size_t init_count = expr._array_init_elements.size();
        size_t arr_size = 0;
        bool has_explicit_size = (expr._array_size_expr != nullptr);
        bool is_dynamic = false;

        if (has_explicit_size) {
            // Try to evaluate the size expression as a compile-time constant
            auto size_val = std::dynamic_pointer_cast<value_expression>(expr._array_size_expr);
            if (size_val && size_val->is_literal()
                && std::holds_alternative<lex::integer>(size_val->any_literal())) {
                auto& int_lit = size_val->any_literal().get<lex::integer>();
                arr_size = int_lit.to_unsigned_int();
            } else {
                // ── Dynamic size: runtime expression ──
                is_dynamic = true;

                // The size expression must be convertible to unsigned int
                auto uint_type = _context->from_type(primitive_type::UNSIGNED_INT);
                auto adapted_size = adapt_type(expr._array_size_expr, uint_type);
                if (!adapted_size) {
                    throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_ARRAY_SIZE_NOT_INT), expr.first_lexeme(),
                        "Array size expression for 'new[]' must be convertible to an unsigned integer; "
                        "expression has type '{}'",
                        {expr._array_size_expr->get_type() ? expr._array_size_expr->get_type()->to_string() : "?"});
                    return;
                }
                if (adapted_size != expr._array_size_expr) {
                    expr._array_size_expr = adapted_size;
                    adapted_size->set_parent_expression(expr.shared_as<expression>());
                }

                // Brace initializers are not allowed for dynamic-sized arrays
                if (expr._has_brace_init) {
                    throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_ARRAY_BRACE_INIT_DYNAMIC), expr.first_lexeme(),
                        "Brace initializer lists are not allowed for dynamically-sized 'new[]' arrays; "
                        "all elements will be default-initialized");
                    return;
                }
            }
        } else {
            // Size inferred from init list (or empty brace init → 0 elements)
            arr_size = init_count;
            if (arr_size == 0 && !expr._has_brace_init) {
                // new T[] with no brace init at all → cannot infer the size
                throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_ARRAY_SUBSCRIPT_BAD_TYPE), expr.first_lexeme(),
                    "Cannot infer array size for 'new[]': "
                    "either provide an explicit size or a brace initializer list");
                return;
            }
            // new T[]{} → arr_size == 0 is valid (empty array)
        }

        if (is_dynamic) {
            // ── Dynamic-sized array: new T[expr] ──
            // No brace init, no static size. All elements default-initialized.
            expr._is_dynamic_size = true;
            expr._array_size = 0; // not meaningful for dynamic

            // For struct element types, resolve the default constructor
            if (auto st_type = std::dynamic_pointer_cast<struct_type>(elem_type)) {
                auto struct_model = st_type->get_struct();
                if (!struct_model) {
                    throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_ARRAY_ELEM_ABSTRACT), expr.first_lexeme(),
                        "Cannot resolve struct for 'new {}[]': aggregate not resolved",
                        {st_type->to_string()});
                    return;
                }
                if (struct_model->is_abstract()) {
                    throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_ARRAY_ELEM_NO_CTOR), expr.first_lexeme(),
                        "Cannot 'new' array of abstract class '{}'",
                        {struct_model->get_short_name()});
                    return;
                }
                // Resolve default constructor (needed for each element)
                auto [default_ctor, default_args] = get_best_matching_constructor(
                    struct_model->constructors(), std::vector<std::shared_ptr<expression>>{});
                // Store in element_constructors[0] as the single default ctor to use
                expr._element_constructors.resize(1, nullptr);
                expr._element_constructors[0] = default_ctor;
            }

            // Result type: owner<array_type> (unsized) → T[]!
            auto arr_type_unsized = elem_type->get_array();
            expr.set_type(arr_type_unsized->get_owner());
            return;
        }

        // ── Static-sized array: new T[N]{...} ──

        // Validate init count vs array size
        if (init_count > arr_size) {
            throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_ARRAY_INIT_NO_MATCH), expr.first_lexeme(),
                "Array initializer list for 'new {}[{}]' has {} elements: too many initializers",
                {elem_type->to_string(), std::to_string(arr_size), std::to_string(init_count)});
            return;
        }
        if (init_count < arr_size && init_count > 0) {
            warn(static_cast<unsigned int>(k::diag::type_diag::WARN_ARRAY_INIT_EXTRA),
                "Array initializer list for 'new {}[{}]' has only {} elements: "
                "remaining {} elements will be default-initialized",
                {elem_type->to_string(), std::to_string(arr_size), std::to_string(init_count),
                 std::to_string(arr_size - init_count)});
        }

        expr._array_size = arr_size;

        // Type-check and adapt each element + resolve constructors
        expr._element_constructors.resize(arr_size, nullptr);

        if (type::is_primitive(elem_type)) {
            for (size_t i = 0; i < init_count; ++i) {
                auto e = expr._array_init_elements[i];
                if (!e) continue;
                auto cast = adapt_type(e, elem_type);
                if (!cast) {
                    throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_ARRAY_ELEM_INIT_MISMATCH), expr.first_lexeme(),
                        "Cannot convert element {} to type '{}' in 'new[]' initializer",
                        {std::to_string(i), elem_type->to_string()});
                } else if (cast != e) {
                    expr.assign_array_init_element(i, cast);
                }
            }
        } else if (auto st_type = std::dynamic_pointer_cast<struct_type>(elem_type)) {
            auto struct_model = st_type->get_struct();
            if (!struct_model) {
                throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_ARRAY_ELEM_ABSTRACT), expr.first_lexeme(),
                    "Cannot resolve struct for 'new {}[]': aggregate not resolved",
                    {st_type->to_string()});
                return;
            }
            if (struct_model->is_abstract()) {
                throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_ARRAY_ELEM_NO_CTOR), expr.first_lexeme(),
                    "Cannot 'new' array of abstract class '{}'",
                    {struct_model->get_short_name()});
                return;
            }
            for (size_t i = 0; i < init_count; ++i) {
                auto e = expr._array_init_elements[i];
                if (!e) continue; // default-init

                auto func_inv = std::dynamic_pointer_cast<function_invocation_expression>(e);
                if (func_inv) {
                    // Explicit constructor call
                    std::vector<std::shared_ptr<expression>> ctor_args;
                    for (auto& arg : func_inv->arguments()) ctor_args.push_back(arg);
                    auto [best_ctor, adapted_args] = get_best_matching_constructor(struct_model->constructors(), ctor_args);
                    if (!best_ctor) {
                        throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_ARRAY_CTOR_NO_SINGLE_PARAM), expr.first_lexeme(),
                            "No matching constructor for element {} of type '{}' in 'new[]'",
                            {std::to_string(i), st_type->to_string()});
                    }
                    check_constructor_visibility(*best_ctor, expr);
                    check_call_contract(*best_ctor, expr.first_lexeme());
                    expr._element_constructors[i] = best_ctor;
                    func_inv->assign_arguments(adapted_args);
                } else {
                    // Implicit single-param constructor
                    std::vector<std::shared_ptr<expression>> ctor_args = {e};
                    auto [best_ctor, adapted_args] = get_best_matching_constructor(struct_model->constructors(), ctor_args);
                    if (!best_ctor) {
                        throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_ARRAY_CTOR_PARAM_MISMATCH), expr.first_lexeme(),
                            "No matching single-parameter constructor for element {} of type '{}' "
                            "with argument type '{}' in 'new[]'",
                            {std::to_string(i), st_type->to_string(),
                             e->get_type() ? e->get_type()->to_string() : "?"});
                    }
                    check_constructor_visibility(*best_ctor, expr);
                    check_call_contract(*best_ctor, expr.first_lexeme());
                    expr._element_constructors[i] = best_ctor;
                    if (!adapted_args.empty() && adapted_args[0] != e) {
                        expr.assign_array_init_element(i, adapted_args[0]);
                    }
                }
            }
            // For uninitialized elements, find default constructor
            // Only search if there are actually elements that need default-init
            bool needs_default_ctor = false;
            for (size_t i = 0; i < arr_size; ++i) {
                if (i >= init_count || !expr._array_init_elements[i]) {
                    needs_default_ctor = true;
                    break;
                }
            }
            if (needs_default_ctor) {
                auto [default_ctor, default_args] = get_best_matching_constructor(
                    struct_model->constructors(), std::vector<std::shared_ptr<expression>>{});
                if (default_ctor) {
                    check_call_contract(*default_ctor, expr.first_lexeme());
                }
                for (size_t i = 0; i < arr_size; ++i) {
                    if (i >= init_count || !expr._array_init_elements[i]) {
                        expr._element_constructors[i] = default_ctor;
                    }
                }
            }
        }

        // Step 2: For arrays: validate size expression, set result type to owner<array<T>>
        // Build the sized_array_type and set the expression type to owner<sized_array_type>
        auto arr_type_unsized = elem_type->get_array();
        auto sized_arr_type = arr_type_unsized->with_size(arr_size);
        expr.set_type(sized_arr_type->get_owner());

        return;
    }

    // ── Single-object form (unchanged) ──

    // Resolve arguments first
    for (auto& arg : expr.arguments()) {
        arg->accept(*this);
    }

    // Step 3: For structs: resolve constructor overload with provided arguments
    auto alloc_type = expr.allocated_type();
    if (!type::is_resolved(alloc_type)) {
        // Try to resolve unresolved type
        if (auto unres = std::dynamic_pointer_cast<unresolved_type>(alloc_type)) {
            std::shared_ptr<type> resolved;
            // Try template instantiation first (e.g. new Node<int>(...))
            if (unres->has_template_args()) {
                resolved = try_instantiate_template_type(unres, static_cast<const element&>(expr));
            }
            if (!resolved || !type::is_resolved(resolved)) {
                resolved = resolve_type_by_name(unres->type_id(), static_cast<const element&>(expr));
            }
            if (!resolved || !type::is_resolved(resolved)) {
                auto imported_agg = _unit.get_or_create_imported_aggregate(unres->type_id(), _context);
                if (imported_agg) resolved = imported_agg->get_struct_type();
            }
            if (resolved && type::is_resolved(resolved)) {
                expr.allocated_type(resolved);
                alloc_type = resolved;
            }
        }
    }

    if (!type::is_resolved(alloc_type)) {
        throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_NEW_TYPE_NOT_FOUND), expr.first_lexeme(),
            "Cannot resolve the type of 'new' expression: type '{}' is unknown",
            {alloc_type ? alloc_type->to_string() : "<null>"});
    }

    // Step 4: Set result type to owner<T>
    // Set the expression type to owner<allocated_type>
    expr.set_type(alloc_type->get_owner());

    // If the allocated type is a struct, find the best matching constructor
    if (auto st_type = std::dynamic_pointer_cast<struct_type>(alloc_type)) {
        auto st = st_type->get_struct();
        if (!st) {
            throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_NEW_INIT_TYPE_MISMATCH), expr.first_lexeme(),
                "Cannot 'new' a struct type '{}': the aggregate is not resolved",
                {st_type->to_string()});
        }
        if (st->is_abstract()) {
            throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_NEW_ABSTRACT_CLASS), expr.first_lexeme(),
                "Cannot 'new' abstract class '{}': abstract classes cannot be directly instantiated",
                {st->get_short_name()});
        }
        auto [best_ctor, adapted_args] = get_best_matching_constructor(st->constructors(), expr.arguments());
        if (!best_ctor) {
            throw_error(static_cast<unsigned int>(k::diag::type_diag::ERR_NEW_ARRAY_BAD_INIT), expr.first_lexeme(),
                "No matching constructor found for 'new {}': none of the available constructors "
                "can be called with the provided arguments",
                {st_type->to_string()});
        }
        check_constructor_visibility(*best_ctor, expr);
        // Check exception contract for throwing constructors
        check_call_contract(*best_ctor, expr.first_lexeme());
        expr.set_constructor(best_ctor);
        expr.assign_arguments(adapted_args);
    } else if (type::is_primitive(alloc_type)) {
        // Primitive type: adapt the single argument if any
        if (!expr.arguments().empty()) {
            auto cast = adapt_type(expr.arguments()[0], alloc_type);
            if (cast && cast != expr.arguments()[0]) {
                expr.assign_argument(0, cast);
            }
        }
    }
}


} // namespace k::model::gen
