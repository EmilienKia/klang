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

#include "structural_pass.hpp"
#include "../../model/model.hpp"
#include "../../model/model_aggregate.hpp"
#include "../../model/model_function.hpp"
#include "../../model/statements.hpp"
#include "../../model/expressions.hpp"
#include "../../model/operators.hpp"
#include "../../model/type.hpp"
#include "../../parse/ast.hpp"

namespace k::plugin::structural {

namespace {

enum class AccessMode {
    DEFAULT,
    NO_ACCESS,
    PUBLIC,
    PROTECTED
};

static AccessMode parse_access_mode_from_expr(const std::shared_ptr<k::parse::ast::expression>& expr) {
    if (!expr) return AccessMode::DEFAULT;
    if (auto ident = std::dynamic_pointer_cast<k::parse::ast::identifier_expr>(expr)) {
        if (!ident->qident.names.empty()) {
            std::string_view name = ident->qident.names.back().content;
            if (name == "NO_ACCESS") return AccessMode::NO_ACCESS;
            if (name == "PUBLIC") return AccessMode::PUBLIC;
            if (name == "PROTECTED") return AccessMode::PROTECTED;
            if (name == "DEFAULT") return AccessMode::DEFAULT;
        }
    }
    return AccessMode::DEFAULT;
}

static std::optional<model::visibility> resolve_visibility(AccessMode mode, model::visibility default_vis) {
    switch (mode) {
        case AccessMode::NO_ACCESS:
            return std::nullopt; // Generation blocked
        case AccessMode::DEFAULT:
            return default_vis;
        case AccessMode::PUBLIC:
            return model::PUBLIC;
        case AccessMode::PROTECTED:
            return model::PROTECTED;
    }
    return default_vis;
}

struct GetterConfig {
    AccessMode const_access = AccessMode::DEFAULT;
    AccessMode mut_access   = AccessMode::DEFAULT;
};

static GetterConfig extract_getter_config(const model::annotation_instance& ann) {
    GetterConfig config;
    auto* ast = ann.ast_node.get();
    if (!ast) return config;

    // 1. Positional arguments: @Getter(constAccess, mutAccess)
    if (ast->has_parens && !ast->args.empty()) {
        if (ast->args.size() >= 1) {
            config.const_access = parse_access_mode_from_expr(ast->args[0]);
        }
        if (ast->args.size() >= 2) {
            config.mut_access = parse_access_mode_from_expr(ast->args[1]);
        }
    }
    // 2. Designated brace init: @Getter{.constAccess = ..., .mutAccess = ...}
    else if (ast->brace_init && ast->brace_init->is_designated) {
        for (const auto& elem : ast->brace_init->elements) {
            auto desig = std::dynamic_pointer_cast<k::parse::ast::designated_init_element>(elem);
            if (!desig) continue;
            std::string_view member = desig->member_name.content;
            auto val_expr = desig->is_call_form && !desig->args.empty() ? desig->args[0] : desig->value;
            if (member == "constAccess") {
                config.const_access = parse_access_mode_from_expr(val_expr);
            } else if (member == "mutAccess") {
                config.mut_access = parse_access_mode_from_expr(val_expr);
            }
        }
    }
    // 3. Positional brace init: @Getter{constAccess, mutAccess}
    else if (ast->brace_init && !ast->brace_init->is_designated) {
        if (ast->brace_init->elements.size() >= 1) {
            config.const_access = parse_access_mode_from_expr(ast->brace_init->elements[0]);
        }
        if (ast->brace_init->elements.size() >= 2) {
            config.mut_access = parse_access_mode_from_expr(ast->brace_init->elements[1]);
        }
    }

    return config;
}

struct SetterConfig {
    AccessMode access = AccessMode::DEFAULT;
};

static SetterConfig extract_setter_config(const model::annotation_instance& ann) {
    SetterConfig config;
    auto* ast = ann.ast_node.get();
    if (!ast) return config;

    // 1. Positional arguments: @Setter(access)
    if (ast->has_parens && !ast->args.empty()) {
        config.access = parse_access_mode_from_expr(ast->args[0]);
    }
    // 2. Designated brace init: @Setter{.access = ...}
    else if (ast->brace_init && ast->brace_init->is_designated) {
        for (const auto& elem : ast->brace_init->elements) {
            auto desig = std::dynamic_pointer_cast<k::parse::ast::designated_init_element>(elem);
            if (!desig) continue;
            std::string_view member = desig->member_name.content;
            auto val_expr = desig->is_call_form && !desig->args.empty() ? desig->args[0] : desig->value;
            if (member == "access") {
                config.access = parse_access_mode_from_expr(val_expr);
            }
        }
    }
    // 3. Positional brace init: @Setter{access}
    else if (ast->brace_init && !ast->brace_init->is_designated && !ast->brace_init->elements.empty()) {
        config.access = parse_access_mode_from_expr(ast->brace_init->elements[0]);
    }

    return config;
}

bool is_no_arg_ctor_annotation(const model::annotation_instance& ann) {
    if (ann.resolved_type) {
        const auto& fq = ann.resolved_type->get_fq_name();
        if (fq == "k::lang::NoArgConstructor" || fq == "::k::lang::NoArgConstructor") return true;
    }
    return ann.raw_name == "NoArgConstructor" || ann.raw_name == "lang::NoArgConstructor"
        || ann.raw_name == "k::lang::NoArgConstructor" || ann.raw_name == "::k::lang::NoArgConstructor";
}

bool is_copy_ctor_annotation(const model::annotation_instance& ann) {
    if (ann.resolved_type) {
        const auto& fq = ann.resolved_type->get_fq_name();
        if (fq == "k::lang::CopyConstructor" || fq == "::k::lang::CopyConstructor") return true;
    }
    return ann.raw_name == "CopyConstructor" || ann.raw_name == "lang::CopyConstructor"
        || ann.raw_name == "k::lang::CopyConstructor" || ann.raw_name == "::k::lang::CopyConstructor";
}

bool is_all_args_ctor_annotation(const model::annotation_instance& ann) {
    if (ann.resolved_type) {
        const auto& fq = ann.resolved_type->get_fq_name();
        if (fq == "k::lang::AllArgsConstructor" || fq == "::k::lang::AllArgsConstructor") return true;
    }
    return ann.raw_name == "AllArgsConstructor" || ann.raw_name == "lang::AllArgsConstructor"
        || ann.raw_name == "k::lang::AllArgsConstructor" || ann.raw_name == "::k::lang::AllArgsConstructor";
}

bool is_copy_assigner_annotation(const model::annotation_instance& ann) {
    if (ann.resolved_type) {
        const auto& fq = ann.resolved_type->get_fq_name();
        if (fq == "k::lang::CopyAssigner" || fq == "::k::lang::CopyAssigner") return true;
    }
    return ann.raw_name == "CopyAssigner" || ann.raw_name == "lang::CopyAssigner"
        || ann.raw_name == "k::lang::CopyAssigner" || ann.raw_name == "::k::lang::CopyAssigner";
}

static AccessMode extract_access_property(const model::annotation_instance& ann) {
    auto* ast = ann.ast_node.get();
    if (!ast) return AccessMode::DEFAULT;

    // 1. Positional arguments: @Annotation(access)
    if (ast->has_parens && !ast->args.empty()) {
        return parse_access_mode_from_expr(ast->args[0]);
    }
    // 2. Designated brace init: @Annotation{.access = ...}
    else if (ast->brace_init && ast->brace_init->is_designated) {
        for (const auto& elem : ast->brace_init->elements) {
            auto desig = std::dynamic_pointer_cast<k::parse::ast::designated_init_element>(elem);
            if (!desig) continue;
            std::string_view member = desig->member_name.content;
            auto val_expr = desig->is_call_form && !desig->args.empty() ? desig->args[0] : desig->value;
            if (member == "access") {
                return parse_access_mode_from_expr(val_expr);
            }
        }
    }
    // 3. Positional brace init: @Annotation{access}
    else if (ast->brace_init && !ast->brace_init->is_designated && !ast->brace_init->elements.empty()) {
        return parse_access_mode_from_expr(ast->brace_init->elements[0]);
    }

    return AccessMode::DEFAULT;
}

bool is_getter_annotation(const model::annotation_instance& ann) {
    if (ann.resolved_type) {
        const auto& fq = ann.resolved_type->get_fq_name();
        if (fq == "k::lang::Getter" || fq == "::k::lang::Getter") return true;
    }
    return ann.raw_name == "Getter" || ann.raw_name == "lang::Getter"
        || ann.raw_name == "k::lang::Getter" || ann.raw_name == "::k::lang::Getter";
}

bool is_setter_annotation(const model::annotation_instance& ann) {
    if (ann.resolved_type) {
        const auto& fq = ann.resolved_type->get_fq_name();
        if (fq == "k::lang::Setter" || fq == "::k::lang::Setter") return true;
    }
    return ann.raw_name == "Setter" || ann.raw_name == "lang::Setter"
        || ann.raw_name == "k::lang::Setter" || ann.raw_name == "::k::lang::Setter";
}

} // anonymous namespace

void structural_pass::visit_aggregate(model::aggregate& agg) {
    // Only process structures and classes
    if (agg.is_struct() || agg.is_class()) {
        std::optional<AccessMode> no_arg_access;
        std::optional<AccessMode> copy_ctor_access;
        std::optional<AccessMode> all_args_access;
        std::optional<AccessMode> copy_asgn_access;

        for (const auto& ann : agg.get_annotations()) {
            if (is_no_arg_ctor_annotation(ann)) {
                no_arg_access = extract_access_property(ann);
            }
            if (is_copy_ctor_annotation(ann)) {
                copy_ctor_access = extract_access_property(ann);
            }
            if (is_all_args_ctor_annotation(ann)) {
                all_args_access = extract_access_property(ann);
            }
            if (is_copy_assigner_annotation(ann)) {
                copy_asgn_access = extract_access_property(ann);
            }
        }

        // Collect non-static user fields in declaration order
        std::vector<std::shared_ptr<model::member_variable_definition>> non_static_fields;
        for (const auto& child : agg.get_children()) {
            if (auto mv = std::dynamic_pointer_cast<model::member_variable_definition>(child)) {
                if (mv->get_short_name().rfind("__", 0) == 0) continue;
                non_static_fields.push_back(mv);
            }
        }

        // 1. @NoArgConstructor
        if (no_arg_access.has_value()) {
            auto vis_opt = resolve_visibility(*no_arg_access, model::PUBLIC);
            if (vis_opt.has_value()) {
                bool has_no_arg_ctor = false;
                for (const auto& c : agg.constructors()) {
                    if (c && c->parameters().empty()) {
                        has_no_arg_ctor = true;
                        break;
                    }
                }
                if (!has_no_arg_ctor) {
                    auto fn = agg.define_function(agg.get_short_name(), false);
                    auto ctor = std::dynamic_pointer_cast<model::constructor>(fn);
                    if (ctor) {
                        ctor->assign_name(agg.get_name().with_back(agg.get_short_name()));
                        ctor->set_visibility(*vis_opt);
                        ctor->set_compiler_generated(true);
                        ctor->create_this_parameter();

                        auto blk = std::make_shared<model::block>(ctor);
                        ctor->set_block(blk);

                        mark_modified();
                    }
                }
            }
        }

        // 2. @CopyConstructor
        if (copy_ctor_access.has_value()) {
            auto vis_opt = resolve_visibility(*copy_ctor_access, model::PUBLIC);
            if (vis_opt.has_value()) {
                bool has_copy_ctor = false;
                for (const auto& c : agg.constructors()) {
                    if (c && c->parameters().size() == 1) {
                        auto ptype = c->parameters()[0]->get_type();
                        if (ptype && model::type::is_reference(ptype)) {
                            auto bare = model::type::canonical(model::type::remove_const(ptype->get_subtype()));
                            if (bare && bare == agg.get_struct_type()) {
                                has_copy_ctor = true;
                                break;
                            }
                        }
                    }
                }
                if (!has_copy_ctor) {
                    auto fn = agg.define_function(agg.get_short_name(), false);
                    auto copy_ctor = std::dynamic_pointer_cast<model::constructor>(fn);
                    if (copy_ctor) {
                        copy_ctor->assign_name(agg.get_name().with_back(agg.get_short_name()));
                        copy_ctor->set_visibility(*vis_opt);
                        copy_ctor->set_compiler_generated(true);
                        copy_ctor->set_copy_constructor(true);
                        copy_ctor->create_this_parameter();

                        auto const_ref_type = agg.get_struct_type()->get_const()->get_reference();
                        copy_ctor->append_parameter("other", const_ref_type);

                        auto blk = std::make_shared<model::block>(copy_ctor);
                        copy_ctor->set_block(blk);

                        mark_modified();
                    }
                }
            }
        }

        // 3. @AllArgsConstructor
        if (all_args_access.has_value()) {
            auto vis_opt = resolve_visibility(*all_args_access, model::PUBLIC);
            if (vis_opt.has_value() && !non_static_fields.empty()) {
                bool has_matching_ctor = false;
                for (const auto& c : agg.constructors()) {
                    if (c && c->parameters().size() == non_static_fields.size()) {
                        bool match = true;
                        for (size_t i = 0; i < non_static_fields.size(); ++i) {
                            if (!model::type::are_equal(c->parameters()[i]->get_type(), non_static_fields[i]->get_type())) {
                                match = false;
                                break;
                            }
                        }
                        if (match) {
                            has_matching_ctor = true;
                            break;
                        }
                    }
                }
                if (!has_matching_ctor) {
                    auto fn = agg.define_function(agg.get_short_name(), false);
                    auto ctor = std::dynamic_pointer_cast<model::constructor>(fn);
                    if (ctor) {
                        ctor->assign_name(agg.get_name().with_back(agg.get_short_name()));
                        ctor->set_visibility(*vis_opt);
                        ctor->set_compiler_generated(true);
                        ctor->create_this_parameter();

                        auto blk = std::make_shared<model::block>(ctor);
                        ctor->set_block(blk);

                        auto this_param = ctor->get_this_parameter();
                        for (auto& field_var : non_static_fields) {
                            auto ftype = field_var->get_type();
                            auto param = ctor->append_parameter(field_var->get_short_name(), ftype);

                            auto this_expr = model::symbol_expression::from_variable(this_param);
                            auto field_sym = model::symbol_expression::from_variable(field_var);
                            auto target_expr = model::member_of_object_expression::make_shared(this_expr, field_sym);

                            auto param_sym = model::symbol_expression::from_variable(param);
                            auto asgn_expr = model::simple_assignation_expression::make_shared(target_expr, param_sym);

                            auto asgn_stmt = std::make_shared<model::expression_statement>(blk);
                            asgn_stmt->set_expression(asgn_expr);
                            blk->append_statement(asgn_stmt);
                        }

                        mark_modified();
                    }
                }
            }
        }

        // 4. @CopyAssigner
        if (copy_asgn_access.has_value()) {
            auto vis_opt = resolve_visibility(*copy_asgn_access, model::PUBLIC);
            if (vis_opt.has_value()) {
                bool has_copy_asgn = false;
                for (const auto& f : agg.get_functions("__operator_aS_")) {
                    if (f && f->parameters().size() == 1) {
                        auto ptype = f->parameters()[0]->get_type();
                        if (ptype && model::type::is_reference(ptype)) {
                            auto bare = model::type::canonical(model::type::remove_const(ptype->get_subtype()));
                            if (bare && bare == agg.get_struct_type()) {
                                has_copy_asgn = true;
                                break;
                            }
                        }
                    }
                }
                if (!has_copy_asgn) {
                    auto copy_asgn = agg.define_function("__operator_aS_", false);
                    copy_asgn->set_operator(true);
                    copy_asgn->set_visibility(*vis_opt);
                    copy_asgn->set_compiler_generated(true);
                    copy_asgn->create_this_parameter();

                    auto st_type = agg.get_struct_type();
                    copy_asgn->set_return_type(st_type->get_reference());

                    auto const_st_type = st_type->get_const()->get_reference();
                    auto other_param = copy_asgn->append_parameter("other", const_st_type);

                    auto blk = std::make_shared<model::block>(copy_asgn);
                    copy_asgn->set_block(blk);

                    auto this_param = copy_asgn->get_this_parameter();

                    // Memberwise assignment for all non-static fields
                    for (auto& field_var : non_static_fields) {
                        auto this_expr = model::symbol_expression::from_variable(this_param);
                        auto field_sym = model::symbol_expression::from_variable(field_var);
                        auto target_expr = model::member_of_object_expression::make_shared(this_expr, field_sym);

                        auto other_expr = model::symbol_expression::from_variable(other_param);
                        auto other_field_sym = model::symbol_expression::from_variable(field_var);
                        auto value_expr = model::member_of_object_expression::make_shared(other_expr, other_field_sym);

                        auto asgn_expr = model::simple_assignation_expression::make_shared(target_expr, value_expr);
                        auto asgn_stmt = std::make_shared<model::expression_statement>(blk);
                        asgn_stmt->set_expression(asgn_expr);
                        blk->append_statement(asgn_stmt);
                    }

                    // return this;
                    auto ret_this = model::symbol_expression::from_variable(this_param);
                    auto ret_stmt = std::make_shared<model::return_statement>(blk);
                    ret_stmt->set_expression(ret_this);
                    blk->append_statement(ret_stmt);

                    mark_modified();
                }
            }
        }

        auto vars_copy = agg.variables();
        for (const auto& [var_name, var_def] : vars_copy) {
            if (!var_def) continue;

            std::optional<GetterConfig> getter_cfg;
            std::optional<SetterConfig> setter_cfg;
            for (const auto& ann : var_def->get_annotations()) {
                if (is_getter_annotation(ann)) getter_cfg = extract_getter_config(ann);
                if (is_setter_annotation(ann)) setter_cfg = extract_setter_config(ann);
            }

            if (!getter_cfg.has_value() && !setter_cfg.has_value()) continue;

            auto var_type = var_def->get_type();
            if (!var_type) continue;

            bool is_static = (std::dynamic_pointer_cast<model::global_variable_definition>(var_def) != nullptr);
            auto existing_funcs = agg.get_functions(var_name);

            // ── Generate Getters (Lombok-style accessors) ────────────────────
            if (getter_cfg.has_value()) {
                auto const_vis = resolve_visibility(getter_cfg->const_access, model::PUBLIC);
                auto mut_vis   = resolve_visibility(getter_cfg->mut_access, model::PROTECTED);

                if (!is_static) {
                    bool has_const_getter = false;
                    bool has_mut_getter = false;
                    for (const auto& f : existing_funcs) {
                        if (f && f->parameters().empty() && !f->is_static()) {
                            if (f->is_const_member()) has_const_getter = true;
                            else has_mut_getter = true;
                        }
                    }

                    // 1. Const Getter: var() const : const T&
                    if (!has_const_getter && const_vis.has_value()) {
                        auto const_fn = agg.define_function(var_name, /*is_static=*/false);
                        const_fn->assign_name(agg.get_name().with_back(var_name));
                        const_fn->set_visibility(*const_vis);
                        const_fn->set_const_member(true);
                        const_fn->set_compiler_generated(true);
                        const_fn->create_this_parameter();

                        auto const_ref_type = var_type->get_const()->get_reference();
                        const_fn->set_return_type(const_ref_type);

                        auto blk = std::make_shared<model::block>(const_fn);
                        const_fn->set_block(blk);

                        auto this_param = const_fn->get_this_parameter();
                        auto this_expr = model::symbol_expression::from_variable(this_param);
                        auto var_sym = model::symbol_expression::from_variable(var_def);
                        auto member_expr = model::member_of_object_expression::make_shared(this_expr, var_sym);

                        auto ret_stmt = std::make_shared<model::return_statement>(blk);
                        ret_stmt->set_expression(member_expr);
                        blk->append_statement(ret_stmt);

                        mark_modified();
                    }

                    // 2. Mutable Getter: var() : T&
                    if (!has_mut_getter && mut_vis.has_value()) {
                        auto mut_fn = agg.define_function(var_name, /*is_static=*/false);
                        mut_fn->assign_name(agg.get_name().with_back(var_name));
                        mut_fn->set_visibility(*mut_vis);
                        mut_fn->set_const_member(false);
                        mut_fn->set_compiler_generated(true);
                        mut_fn->create_this_parameter();

                        auto mut_ref_type = var_type->get_reference();
                        mut_fn->set_return_type(mut_ref_type);

                        auto blk = std::make_shared<model::block>(mut_fn);
                        mut_fn->set_block(blk);

                        auto this_param = mut_fn->get_this_parameter();
                        auto this_expr = model::symbol_expression::from_variable(this_param);
                        auto var_sym = model::symbol_expression::from_variable(var_def);
                        auto member_expr = model::member_of_object_expression::make_shared(this_expr, var_sym);

                        auto ret_stmt = std::make_shared<model::return_statement>(blk);
                        ret_stmt->set_expression(member_expr);
                        blk->append_statement(ret_stmt);

                        mark_modified();
                    }
                } else {
                    // Static Getter: var() : T& (controlled by const_access)
                    if (const_vis.has_value()) {
                        bool has_static_getter = false;
                        for (const auto& f : existing_funcs) {
                            if (f && f->parameters().empty() && f->is_static()) {
                                has_static_getter = true;
                                break;
                            }
                        }

                        if (!has_static_getter) {
                            auto stat_fn = agg.define_function(var_name, /*is_static=*/true);
                            stat_fn->assign_name(agg.get_name().with_back(var_name));
                            stat_fn->set_visibility(*const_vis);
                            stat_fn->set_compiler_generated(true);

                            auto ref_type = var_type->get_reference();
                            stat_fn->set_return_type(ref_type);

                            auto blk = std::make_shared<model::block>(stat_fn);
                            stat_fn->set_block(blk);

                            auto var_sym = model::symbol_expression::from_variable(var_def);
                            auto ret_stmt = std::make_shared<model::return_statement>(blk);
                            ret_stmt->set_expression(var_sym);
                            blk->append_statement(ret_stmt);

                            mark_modified();
                        }
                    }
                }
            }

            // ── Generate Setter ─────────────────────────────────────────────
            if (setter_cfg.has_value()) {
                auto setter_vis = resolve_visibility(setter_cfg->access, model::PROTECTED);

                if (setter_vis.has_value()) {
                    if (!is_static) {
                        bool has_setter = false;
                        for (const auto& f : existing_funcs) {
                            if (f && f->parameters().size() == 1 && !f->is_static()) {
                                has_setter = true;
                                break;
                            }
                        }

                        if (!has_setter) {
                            auto setter_fn = agg.define_function(var_name, /*is_static=*/false);
                            setter_fn->assign_name(agg.get_name().with_back(var_name));
                            setter_fn->set_visibility(*setter_vis);
                            setter_fn->set_const_member(false);
                            setter_fn->set_compiler_generated(true);
                            setter_fn->create_this_parameter();

                            auto const_ref_type = var_type->get_const()->get_reference();
                            auto val_param = setter_fn->append_parameter("value", const_ref_type);

                            auto this_ref_type = agg.get_struct_type()->get_reference();
                            setter_fn->set_return_type(this_ref_type);

                            auto blk = std::make_shared<model::block>(setter_fn);
                            setter_fn->set_block(blk);

                            auto this_param = setter_fn->get_this_parameter();
                            auto this_expr = model::symbol_expression::from_variable(this_param);
                            auto var_sym = model::symbol_expression::from_variable(var_def);
                            auto target_expr = model::member_of_object_expression::make_shared(this_expr, var_sym);

                            auto val_expr = model::symbol_expression::from_variable(val_param);
                            auto asgn_expr = model::simple_assignation_expression::make_shared(target_expr, val_expr);

                            auto asgn_stmt = std::make_shared<model::expression_statement>(blk);
                            asgn_stmt->set_expression(asgn_expr);
                            blk->append_statement(asgn_stmt);

                            auto ret_this = model::symbol_expression::from_variable(this_param);
                            auto ret_stmt = std::make_shared<model::return_statement>(blk);
                            ret_stmt->set_expression(ret_this);
                            blk->append_statement(ret_stmt);

                            mark_modified();
                        }
                    } else {
                        bool has_static_setter = false;
                        for (const auto& f : existing_funcs) {
                            if (f && f->parameters().size() == 1 && f->is_static()) {
                                has_static_setter = true;
                                break;
                            }
                        }

                        if (!has_static_setter) {
                            auto setter_fn = agg.define_function(var_name, /*is_static=*/true);
                            setter_fn->assign_name(agg.get_name().with_back(var_name));
                            setter_fn->set_visibility(*setter_vis);
                            setter_fn->set_compiler_generated(true);

                            auto const_ref_type = var_type->get_const()->get_reference();
                            auto val_param = setter_fn->append_parameter("value", const_ref_type);

                            auto blk = std::make_shared<model::block>(setter_fn);
                            setter_fn->set_block(blk);

                            auto var_sym = model::symbol_expression::from_variable(var_def);
                            auto val_expr = model::symbol_expression::from_variable(val_param);
                            auto asgn_expr = model::simple_assignation_expression::make_shared(var_sym, val_expr);

                            auto asgn_stmt = std::make_shared<model::expression_statement>(blk);
                            asgn_stmt->set_expression(asgn_expr);
                            blk->append_statement(asgn_stmt);

                            mark_modified();
                        }
                    }
                }
            }
        }
    }

    // Recurse down children for nested aggregates
    model_pass::visit_aggregate(agg);
}

} // namespace k::plugin::structural
