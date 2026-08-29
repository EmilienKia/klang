/*
 * K Language compiler
 *
 * Copyright 2026 Emilien Kia
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

#include "resolvers_constexpr.hpp"
#include "resolvers_common.hpp"
#include "resolvers_scope_lookup.hpp"

#include "../model/model.hpp"
#include "../model/aggregate_value.hpp"
#include "../model/constant_value.hpp"
#include "../model/constant_evaluator.hpp"
#include "../model/type.hpp"
#include "../model/template.hpp"
#include "../parse/ast.hpp"
#include "../errors.hpp"

#include <cmath>
#include <cstring>
#include <map>
#include <unordered_map>

namespace k::model::gen {

namespace {

using k::parse::ast::expression;
using k::parse::ast::literal_expr;
using k::parse::ast::identifier_expr;
using k::parse::ast::unary_prefix_expr;
using k::parse::ast::binary_operator_expr;
using k::parse::ast::conditional_expr;
using k::parse::ast::cast_expr;
using k::parse::ast::brace_init_list;
using k::parse::ast::designated_init_element;

struct eval_outcome {
    bool ok = false;
    constant_value value;
    unsigned int error_code = 0;
    std::string message;
    std::vector<std::string> message_args;

    static eval_outcome success(constant_value v) {
        eval_outcome o;
        o.ok = true;
        o.value = std::move(v);
        return o;
    }
    static eval_outcome defer() {
        eval_outcome o;
        o.ok = false;
        o.error_code = 0;
        return o;
    }
    static eval_outcome error(unsigned int code, std::string msg, std::vector<std::string> args = {}) {
        eval_outcome o;
        o.ok = false;
        o.error_code = code;
        o.message = std::move(msg);
        o.message_args = std::move(args);
        return o;
    }
    bool is_hard_error() const { return !ok && error_code != 0; }
};

// ─────────────────────────────────────────────────────────────────────────────
// Enum-entry / dependent value-parameter identifier resolution
// ─────────────────────────────────────────────────────────────────────────────

/** Mirrors the "EnumName::entryName" / "UnionName::Kind::entryName" / "ns::Enum::entryName"
 *  resolution performed for ordinary symbol expressions in gen_expressions.cpp, but
 *  operating directly on the raw (unresolved) AST qualified-identifier. */
std::shared_ptr<enumeration> lookup_enum_for_qualified_entry(
    const element& context_elem,
    const k::parse::ast::qualified_identifier& qident,
    unit* unit_ptr,
    const std::shared_ptr<context>& ctx) {

    if (qident.has_root_prefix() || qident.size() < 2) return nullptr;

    if (qident.size() == 2) {
        const std::string enum_name = qident[0];
        for (auto cur = context_elem.shared_as<const element>(); cur; cur = cur->parent<element>()) {
            if (auto eh = std::dynamic_pointer_cast<const enum_holder>(cur)) {
                if (auto en = eh->get_enum(enum_name)) return en;
            }
        }
    } else if (qident.size() == 3 && qident[1] == "Kind") {
        const std::string union_name = qident[0];
        for (auto cur = context_elem.shared_as<const element>(); cur; cur = cur->parent<element>()) {
            if (auto uh = std::dynamic_pointer_cast<const union_holder>(cur)) {
                if (auto un = uh->get_union(union_name)) return un->get_kind_enum();
            }
        }
    } else {
        // Multi-segment qualified enum: e.g. ns1::ns2::Enum::Entry
        std::string enum_name = qident[qident.size() - 2];
        std::vector<std::string> ns_parts;
        for (size_t i = 0; i + 2 < qident.size(); ++i) ns_parts.push_back(qident[i]);

        // Search starting from enclosing scopes
        for (auto cur = context_elem.shared_as<const element>(); cur; cur = cur->parent<element>()) {
            if (auto nspc = std::dynamic_pointer_cast<const ns>(cur)) {
                auto target_ns = nspc;
                bool ok = true;
                for (const auto& part : ns_parts) {
                    if (auto child = target_ns->get_child_namespace(part)) {
                        target_ns = child;
                    } else {
                        ok = false;
                        break;
                    }
                }
                if (ok && target_ns) {
                    if (auto en = target_ns->get_enum(enum_name)) return en;
                }
            }
        }

        // Search from root namespace
        auto root = scope_lookup::root_namespace(context_elem);
        if (root) {
            auto target_ns = root;
            bool ok = true;
            for (const auto& part : ns_parts) {
                if (auto child = target_ns->get_child_namespace(part)) {
                    target_ns = child;
                } else {
                    ok = false;
                    break;
                }
            }
            if (ok && target_ns) {
                if (auto en = target_ns->get_enum(enum_name)) return en;
            }
        }
    }

    // Fallback: imported (cross-module) enum.
    if (unit_ptr) {
        std::vector<std::string> enum_parts;
        for (size_t i = 0; i + 1 < qident.size(); ++i) enum_parts.push_back(qident[i]);
        k::name enum_name{false, std::move(enum_parts)};
        if (auto en = unit_ptr->get_or_create_imported_enum(enum_name, ctx)) return en;
    }
    return nullptr;
}

/** If `name` is the name of a value template parameter of the nearest
 *  enclosing (already-instantiated, concrete) template aggregate/union/
 *  function of `context_elem`, return its bound concrete value. */
std::optional<k::value_type> lookup_enclosing_instantiated_value_arg(
    const element& context_elem, const std::string& name) {

    auto find_index_and_value = [&](const std::vector<template_param_descriptor>& params,
                                     const std::vector<template_argument>& args)
        -> std::optional<k::value_type> {
        for (size_t i = 0; i < params.size() && i < args.size(); ++i) {
            if (params[i].name == name && params[i].is_value_param() && args[i].is_value()) {
                return args[i].value_arg;
            }
        }
        return std::nullopt;
    };

    for (auto cur = context_elem.shared_as<const element>(); cur; cur = cur->parent<element>()) {
        if (auto agg = std::dynamic_pointer_cast<const aggregate>(cur)) {
            if (agg->has_tpl_args()) {
                if (auto parent_ns = agg->parent<ns>()) {
                    if (auto tpl_def = parent_ns->get_aggregate(agg->get_tpl_base_name())) {
                        if (auto* ti = tpl_def->get_tpl_info()) {
                            if (auto v = find_index_and_value(ti->params, agg->get_tpl_args())) return v;
                        }
                    }
                }
            }
        } else if (auto un = std::dynamic_pointer_cast<const union_type_def>(cur)) {
            if (un->has_tpl_args()) {
                if (auto parent_ns = un->parent<ns>()) {
                    if (auto tpl_def = parent_ns->get_union(un->get_tpl_base_name())) {
                        if (auto* ti = tpl_def->get_tpl_info()) {
                            if (auto v = find_index_and_value(ti->params, un->get_tpl_args())) return v;
                        }
                    }
                }
            }
        } else if (auto fn = std::dynamic_pointer_cast<const function>(cur)) {
            if (fn->has_tpl_args()) {
                std::shared_ptr<function> tpl_def;
                if (auto fh = fn->parent<function_holder>()) {
                    for (auto& cand : fh->get_functions(fn->get_tpl_base_name())) {
                        if (cand->is_template()) { tpl_def = cand; break; }
                    }
                }
                if (tpl_def) {
                    if (auto* ti = tpl_def->get_tpl_info()) {
                        if (auto v = find_index_and_value(ti->params, fn->get_tpl_args())) return v;
                    }
                }
            }
        }
    }
    return std::nullopt;
}

// ─────────────────────────────────────────────────────────────────────────────
// Recursive raw evaluator delegating to constant_evaluator
// ─────────────────────────────────────────────────────────────────────────────

eval_outcome eval_raw(const expression* expr,
                       const element& context_elem,
                       const std::shared_ptr<context>& ctx,
                       unit* unit_ptr);

eval_outcome eval_literal(const literal_expr& lit) {
    auto val = lit.literal.value().value();
    return std::visit([](auto&& v) -> eval_outcome {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, std::monostate> || std::is_same_v<T, std::nullptr_t>) {
            return eval_outcome::defer();
        } else {
            return eval_outcome::success(constant_value(v));
        }
    }, val);
}

eval_outcome eval_identifier(const identifier_expr& id,
                              const element& context_elem,
                              const std::shared_ptr<context>& ctx,
                              unit* unit_ptr) {
    if (id.has_template_args()) return eval_outcome::defer();

    const auto& q = id.qident;

    if (q.size() == 1 && !q.has_root_prefix()) {
        const std::string name = q[0];
        if (auto v = lookup_enclosing_instantiated_value_arg(context_elem, name)) {
            return eval_outcome::success(constant_value(*v));
        }

        // Look for const variable in current scope
        if (auto elem_ptr = context_elem.shared_as<const element>()) {
            if (auto var = scope_lookup::lookup_variable(std::const_pointer_cast<element>(elem_ptr), name)) {
                if (var->is_const() && var->is_constant()) {
                    return eval_outcome::success(var->get_constant_value());
                }
            }
        }
        return eval_outcome::defer();
    }

    if (auto en = lookup_enum_for_qualified_entry(context_elem, q, unit_ptr, ctx)) {
        const std::string entry_name = q[q.size() - 1];
        auto entry = en->get_entry_by_name(entry_name);
        if (!entry.has_value()) {
            // During model_builder, local enums exist but their entries are still in
            // raw form until symbol_resolver::resolve_enumeration(). Defer so that
            // default-value materialization can retry later in a fully-resolved context.
            if (!en->is_resolved()) {
                return eval_outcome::defer();
            }
            return eval_outcome::error(
                static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_VALUE_ARG_NOT_CONSTANT),
                "enum '" + en->get_short_name() + "' has no entry named '" + entry_name + "'",
                {en->get_short_name(), entry_name});
        }
        size_t idx = 0;
        for (const auto& ent : en->entries()) {
            if (ent.name == entry_name) break;
            ++idx;
        }
        enum_value ev{en, idx, entry->value, entry->name};
        return eval_outcome::success(constant_value(ev));
    }

    // Check for static const member: StructName::CONST_VAR or ns::CONST_VAR
    if (q.size() == 2 && !q.has_root_prefix()) {
        if (auto elem_ptr = context_elem.shared_as<const element>()) {
            if (auto agg = scope_lookup::lookup_structure(std::const_pointer_cast<element>(elem_ptr), q[0])) {
                if (auto var = agg->get_variable(q[1])) {
                    if (var->is_const() && var->is_constant()) {
                        return eval_outcome::success(var->get_constant_value());
                    }
                }
            }
        }
    }

    return eval_outcome::defer();
}

eval_outcome eval_unary(const unary_prefix_expr& ue,
                         const element& context_elem,
                         const std::shared_ptr<context>& ctx,
                         unit* unit_ptr) {
    auto sub = eval_raw(ue.expr().get(), context_elem, ctx, unit_ptr);
    if (!sub.ok) return sub;

    if (!sub.value.is_numeric() && !sub.value.is_bool()) {
        return eval_outcome::error(
            static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_VALUE_ARG_TYPE_MISMATCH),
            "unary operator applied to a non-numeric constant value");
    }

    unary_op uop;
    switch (ue.op.type) {
        case lex::operator_::MINUS:
            uop = unary_op::MINUS;
            break;
        case lex::operator_::PLUS:
            uop = unary_op::PLUS;
            break;
        case lex::operator_::EXCLAMATION_MARK:
            uop = unary_op::LOGICAL_NOT;
            break;
        case lex::operator_::TILDE:
            if (sub.value.is_float()) {
                return eval_outcome::error(
                    static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_VALUE_ARG_TYPE_MISMATCH),
                    "bitwise complement '~' requires an integer constant");
            }
            uop = unary_op::BITWISE_NOT;
            break;
        default:
            return eval_outcome::error(
                static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_VALUE_ARG_NOT_CONSTANT),
                "unsupported unary operator in template value argument");
    }

    auto res = constant_evaluator::eval_unary(uop, sub.value, nullptr);
    if (!res.has_value()) {
        return eval_outcome::error(
            static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_VALUE_ARG_NOT_CONSTANT),
            "unsupported unary operator in template value argument");
    }
    return eval_outcome::success(*res);
}

eval_outcome eval_binary(const binary_operator_expr& be,
                          const element& context_elem,
                          const std::shared_ptr<context>& ctx,
                          unit* unit_ptr) {
    using op_t = lex::operator_::type_t;
    op_t op = be.op.type;

    // Short-circuit logical operators.
    if (op == op_t::DOUBLE_AMPERSAND || op == op_t::DOUBLE_PIPE) {
        auto l = eval_raw(be.lexpr().get(), context_elem, ctx, unit_ptr);
        if (!l.ok) return l;
        if (!l.value.is_numeric() && !l.value.is_bool()) {
            return eval_outcome::error(
                static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_VALUE_ARG_TYPE_MISMATCH),
                "logical operator applied to a non-numeric constant value");
        }
        bool lbool = l.value.is_bool() ? l.value.get_bool()
            : (l.value.is_float() ? (l.value.get_double() != 0.0) : (l.value.get_int64() != 0));
        if (op == op_t::DOUBLE_AMPERSAND && !lbool) return eval_outcome::success(constant_value(false));
        if (op == op_t::DOUBLE_PIPE && lbool) return eval_outcome::success(constant_value(true));
        auto r = eval_raw(be.rexpr().get(), context_elem, ctx, unit_ptr);
        if (!r.ok) return r;
        if (!r.value.is_numeric() && !r.value.is_bool()) {
            return eval_outcome::error(
                static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_VALUE_ARG_TYPE_MISMATCH),
                "logical operator applied to a non-numeric constant value");
        }
        bool rbool = r.value.is_bool() ? r.value.get_bool()
            : (r.value.is_float() ? (r.value.get_double() != 0.0) : (r.value.get_int64() != 0));
        return eval_outcome::success(constant_value(rbool));
    }

    auto l = eval_raw(be.lexpr().get(), context_elem, ctx, unit_ptr);
    if (!l.ok) return l;
    auto r = eval_raw(be.rexpr().get(), context_elem, ctx, unit_ptr);
    if (!r.ok) return r;

    if (!l.value.is_numeric() || !r.value.is_numeric()) {
        return eval_outcome::error(
            static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_VALUE_ARG_TYPE_MISMATCH),
            "binary operator applied to a non-numeric constant value");
    }

    if (op == op_t::SLASH) {
        if ((r.value.is_float() && r.value.get_double() == 0.0) || (!r.value.is_float() && r.value.get_int64() == 0)) {
            return eval_outcome::error(
                static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_VALUE_ARG_NOT_CONSTANT),
                "division by zero in template value argument constant expression");
        }
    } else if (op == op_t::PERCENT) {
        if (l.value.is_float() || r.value.is_float()) {
            return eval_outcome::error(
                static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_VALUE_ARG_TYPE_MISMATCH),
                "operator '%' requires integer constant operands");
        }
        if (r.value.get_int64() == 0) {
            return eval_outcome::error(
                static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_VALUE_ARG_NOT_CONSTANT),
                "modulo by zero in template value argument constant expression");
        }
    }

    switch (op) {
        case op_t::PLUS:
            if (auto res = constant_evaluator::eval_binary_arithmetic(binary_arith_op::ADD, l.value, r.value, nullptr))
                return eval_outcome::success(*res);
            break;
        case op_t::MINUS:
            if (auto res = constant_evaluator::eval_binary_arithmetic(binary_arith_op::SUB, l.value, r.value, nullptr))
                return eval_outcome::success(*res);
            break;
        case op_t::STAR:
            if (auto res = constant_evaluator::eval_binary_arithmetic(binary_arith_op::MUL, l.value, r.value, nullptr))
                return eval_outcome::success(*res);
            break;
        case op_t::SLASH:
            if (auto res = constant_evaluator::eval_binary_arithmetic(binary_arith_op::DIV, l.value, r.value, nullptr))
                return eval_outcome::success(*res);
            break;
        case op_t::PERCENT:
            if (auto res = constant_evaluator::eval_binary_arithmetic(binary_arith_op::MOD, l.value, r.value, nullptr))
                return eval_outcome::success(*res);
            break;
        case op_t::AMPERSAND:
            if (auto res = constant_evaluator::eval_binary_arithmetic(binary_arith_op::BITWISE_AND, l.value, r.value, nullptr))
                return eval_outcome::success(*res);
            break;
        case op_t::PIPE:
            if (auto res = constant_evaluator::eval_binary_arithmetic(binary_arith_op::BITWISE_OR, l.value, r.value, nullptr))
                return eval_outcome::success(*res);
            break;
        case op_t::CARET:
            if (auto res = constant_evaluator::eval_binary_arithmetic(binary_arith_op::BITWISE_XOR, l.value, r.value, nullptr))
                return eval_outcome::success(*res);
            break;
        case op_t::DOUBLE_CHEVRON_OPEN:
            if (auto res = constant_evaluator::eval_binary_arithmetic(binary_arith_op::SHIFT_LEFT, l.value, r.value, nullptr))
                return eval_outcome::success(*res);
            break;
        case op_t::DOUBLE_CHEVRON_CLOSE:
            if (auto res = constant_evaluator::eval_binary_arithmetic(binary_arith_op::SHIFT_RIGHT, l.value, r.value, nullptr))
                return eval_outcome::success(*res);
            break;
        case op_t::DOUBLE_EQUAL:
            if (auto res = constant_evaluator::eval_comparison(comparison_op::EQUAL, l.value, r.value))
                return eval_outcome::success(*res);
            break;
        case op_t::EXCLAMATION_MARK_EQUAL:
            if (auto res = constant_evaluator::eval_comparison(comparison_op::NOT_EQUAL, l.value, r.value))
                return eval_outcome::success(*res);
            break;
        case op_t::CHEVRON_OPEN:
            if (auto res = constant_evaluator::eval_comparison(comparison_op::LESS, l.value, r.value))
                return eval_outcome::success(*res);
            break;
        case op_t::CHEVRON_CLOSE:
            if (auto res = constant_evaluator::eval_comparison(comparison_op::GREATER, l.value, r.value))
                return eval_outcome::success(*res);
            break;
        case op_t::CHEVRON_OPEN_EQUAL:
            if (auto res = constant_evaluator::eval_comparison(comparison_op::LESS_EQUAL, l.value, r.value))
                return eval_outcome::success(*res);
            break;
        case op_t::CHEVRON_CLOSE_EQUAL:
            if (auto res = constant_evaluator::eval_comparison(comparison_op::GREATER_EQUAL, l.value, r.value))
                return eval_outcome::success(*res);
            break;
        default:
            break;
    }
    return eval_outcome::error(
        static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_VALUE_ARG_NOT_CONSTANT),
        "unsupported operator in template value argument constant expression");
}

eval_outcome eval_conditional(const conditional_expr& ce,
                               const element& context_elem,
                               const std::shared_ptr<context>& ctx,
                               unit* unit_ptr) {
    auto cond = eval_raw(ce.lexpr().get(), context_elem, ctx, unit_ptr);
    if (!cond.ok) return cond;
    if (!cond.value.is_numeric() && !cond.value.is_bool()) {
        return eval_outcome::error(
            static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_VALUE_ARG_TYPE_MISMATCH),
            "ternary condition is not a numeric constant");
    }
    bool taken = cond.value.is_bool() ? cond.value.get_bool()
        : (cond.value.is_float() ? (cond.value.get_double() != 0.0) : (cond.value.get_int64() != 0));
    return eval_raw((taken ? ce.mexpr() : ce.rexpr()).get(), context_elem, ctx, unit_ptr);
}

eval_outcome eval_cast(const cast_expr& ce,
                        const element& context_elem,
                        const std::shared_ptr<context>& ctx,
                        unit* unit_ptr) {
    auto sub = eval_raw(ce.expr().get(), context_elem, ctx, unit_ptr);
    if (!sub.ok) return sub;

    if (!ctx) return eval_outcome::defer();
    auto target = ctx->from_type_specifier(*ce.type);
    target = ctx->resolve_type(target);
    if (!target || !type::is_resolved(target)) return eval_outcome::defer();

    auto bare = type::remove_const(target);
    if (!std::dynamic_pointer_cast<primitive_type>(bare) && !std::dynamic_pointer_cast<enum_type>(bare)) {
        return eval_outcome::error(
            static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_VALUE_ARG_TYPE_MISMATCH),
            "cast target in template value argument must be a primitive type");
    }

    if (!sub.value.is_numeric() && !sub.value.is_bool()) {
        return eval_outcome::error(
            static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_VALUE_ARG_TYPE_MISMATCH),
            "cast applied to a non-numeric constant value");
    }

    auto casted = constant_evaluator::cast_to_type(sub.value, target);
    if (!casted.has_value()) {
        return eval_outcome::error(
            static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_VALUE_ARG_TYPE_MISMATCH),
            "cast applied to a non-numeric constant value");
    }
    return eval_outcome::success(*casted);
}

/** Evaluate a brace-initialized expression as a compile-time aggregate value.
 *
 * Requires an expected aggregate type and designated member initializers.
 * Positional braces are intentionally deferred here (member order ambiguity).
 */
eval_outcome eval_aggregate_init(const brace_init_list& bil,
                                  const element& context_elem,
                                  const std::shared_ptr<context>& ctx,
                                  const std::shared_ptr<type>& expected_type,
                                  unit* unit_ptr) {
    if (!expected_type || !ctx) {
        return eval_outcome::defer();
    }

    auto resolved_expected = expected_type;
    if (!type::is_resolved(resolved_expected)) {
        auto rt = ctx->resolve_type(resolved_expected);
        if (!rt || !type::is_resolved(rt)) return eval_outcome::defer();
        resolved_expected = rt;
    }

    auto expected_struct = std::dynamic_pointer_cast<struct_type>(type::remove_const(resolved_expected));
    if (!expected_struct || !expected_struct->get_struct()) {
        return eval_outcome::defer();
    }
    auto agg = expected_struct->get_struct();

    if (!bil.is_designated) {
        return eval_outcome::error(
            static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_VALUE_ARG_TYPE_MISMATCH),
            "aggregate template value arguments must use designated brace initialization");
    }

    std::unordered_map<std::string, std::shared_ptr<member_variable_definition>> members;
    for (const auto& [name, var] : agg->variables()) {
        auto m = std::dynamic_pointer_cast<member_variable_definition>(var);
        if (!m) continue;
        // Skip synthetic internals (e.g. __parent__).
        if (name.rfind("__", 0) == 0) continue;
        members[name] = m;
    }

    std::map<std::string, constant_value> field_values;
    for (const auto& elem : bil.elements) {
        auto desig = std::dynamic_pointer_cast<designated_init_element>(elem);
        if (!desig) {
            return eval_outcome::error(
                static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_VALUE_ARG_TYPE_MISMATCH),
                "aggregate template value argument must use designated members (.field = value)");
        }

        if (desig->is_call_form) {
            return eval_outcome::error(
                static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_VALUE_ARG_NOT_CONSTANT),
                "designated constructor-form member initializers are not supported in template value arguments");
        }
        if (!desig->qualifier.empty()) {
            return eval_outcome::error(
                static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_VALUE_ARG_NOT_CONSTANT),
                "qualified designated member initializers are not supported in template value arguments");
        }
        if (!desig->value) {
            return eval_outcome::error(
                static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_VALUE_ARG_NOT_CONSTANT),
                "designated member initializer is missing a value expression");
        }

        const std::string member_name{desig->member_name.content};
        auto it_member = members.find(member_name);
        if (it_member == members.end()) {
            return eval_outcome::error(
                static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_VALUE_ARG_TYPE_MISMATCH),
                "unknown designated member '" + member_name + "' for aggregate '" + agg->get_short_name() + "'");
        }
        if (field_values.count(member_name) != 0) {
            return eval_outcome::error(
                static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_VALUE_ARG_TYPE_MISMATCH),
                "duplicate designated member initializer for '" + member_name + "'");
        }

        auto member_type = it_member->second->get_type();
        if (member_type && !type::is_resolved(member_type)) {
            auto resolved_member = ctx->resolve_type(member_type);
            if (resolved_member && type::is_resolved(resolved_member)) {
                member_type = resolved_member;
            }
        }

        auto member_eval = evaluate_template_value_arg(
            desig->value.get(), context_elem, ctx, member_type, unit_ptr);
        if (member_eval.is_error()) {
            return eval_outcome::error(member_eval.error_code, member_eval.message, member_eval.message_args);
        }
        if (!member_eval.ok()) {
            return eval_outcome::error(
                static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_VALUE_ARG_NOT_CONSTANT),
                "designated member '" + member_name + "' is not a compile-time constant");
        }

        field_values.emplace(member_name, constant_value(*member_eval.value));
    }

    for (const auto& [member_name, _] : members) {
        if (field_values.count(member_name) == 0) {
            return eval_outcome::error(
                static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_VALUE_ARG_TYPE_MISMATCH),
                "missing designated initializer for member '" + member_name + "'");
        }
    }

    auto res = constant_evaluator::eval_struct_init(agg, field_values);
    if (!res.has_value()) {
        return eval_outcome::error(
            static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_VALUE_ARG_NOT_CONSTANT),
            "failed to construct compile-time constant aggregate");
    }

    return eval_outcome::success(*res);
}

eval_outcome eval_array_init(const brace_init_list& bil,
                             const element& context_elem,
                             const std::shared_ptr<context>& ctx,
                             const std::shared_ptr<type>& expected_type,
                             unit* unit_ptr) {
    if (!expected_type || !ctx) {
        return eval_outcome::defer();
    }

    auto resolved_expected = expected_type;
    if (!type::is_resolved(resolved_expected)) {
        auto rt = ctx->resolve_type(resolved_expected);
        if (!rt || !type::is_resolved(rt)) return eval_outcome::defer();
        resolved_expected = rt;
    }

    auto expected_arr = std::dynamic_pointer_cast<array_type>(type::remove_const(resolved_expected));
    if (!expected_arr) {
        return eval_outcome::defer();
    }

    if (bil.is_designated) {
        return eval_outcome::error(
            static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_VALUE_ARG_TYPE_MISMATCH),
            "array template value arguments must use positional brace initialization");
    }

    auto elem_type = expected_arr->get_subtype();
    if (elem_type && !type::is_resolved(elem_type)) {
        auto resolved_elem = ctx->resolve_type(elem_type);
        if (resolved_elem && type::is_resolved(resolved_elem)) {
            elem_type = resolved_elem;
        }
    }

    auto sarr = std::dynamic_pointer_cast<sized_array_type>(expected_arr);
    size_t target_size = sarr ? sarr->get_size() : bil.elements.size();

    if (bil.elements.size() > target_size) {
        return eval_outcome::error(
            static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_VALUE_ARG_TYPE_MISMATCH),
            "too many initializers in array template value argument");
    }

    std::vector<constant_value> elements;
    elements.reserve(target_size);

    auto default_elem = constant_evaluator::default_value_for_type(elem_type);

    for (size_t i = 0; i < target_size; ++i) {
        if (i < bil.elements.size() && bil.elements[i]) {
            auto elem_eval = evaluate_template_value_arg(
                bil.elements[i].get(), context_elem, ctx, elem_type, unit_ptr);
            if (elem_eval.is_error()) {
                return eval_outcome::error(elem_eval.error_code, elem_eval.message, elem_eval.message_args);
            }
            if (!elem_eval.ok()) {
                return eval_outcome::error(
                    static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_VALUE_ARG_NOT_CONSTANT),
                    "array element is not a compile-time constant");
            }
            elements.emplace_back(*elem_eval.value);
        } else {
            if (!default_elem) {
                return eval_outcome::error(
                    static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_VALUE_ARG_NOT_CONSTANT),
                    "cannot default-initialize array element in template value argument");
            }
            elements.push_back(*default_elem);
        }
    }

    auto final_arr_type = sarr ? sarr : (elem_type ? elem_type->get_array(elements.size()) : nullptr);
    auto av = std::make_shared<array_value>(final_arr_type, std::move(elements));
    return eval_outcome::success(constant_value(av));
}

eval_outcome eval_raw(const expression* expr,
                       const element& context_elem,
                       const std::shared_ptr<context>& ctx,
                       unit* unit_ptr) {
    if (!expr) return eval_outcome::defer();

    if (auto lit = dynamic_cast<const literal_expr*>(expr)) return eval_literal(*lit);
    if (auto id = dynamic_cast<const identifier_expr*>(expr)) return eval_identifier(*id, context_elem, ctx, unit_ptr);
    if (auto ue = dynamic_cast<const unary_prefix_expr*>(expr)) return eval_unary(*ue, context_elem, ctx, unit_ptr);
    if (auto be = dynamic_cast<const binary_operator_expr*>(expr)) return eval_binary(*be, context_elem, ctx, unit_ptr);
    if (auto ce = dynamic_cast<const conditional_expr*>(expr)) return eval_conditional(*ce, context_elem, ctx, unit_ptr);
    if (auto cs = dynamic_cast<const cast_expr*>(expr)) return eval_cast(*cs, context_elem, ctx, unit_ptr);
    if (auto bil = dynamic_cast<const brace_init_list*>(expr))
        return eval_aggregate_init(*bil, context_elem, ctx, nullptr, unit_ptr);

    // Anything else (function calls, member access on runtime objects, 'new', ...)
    // is not a supported compile-time constant form: defer, no diagnostic here.
    return eval_outcome::defer();
}

} // anonymous namespace

constexpr_eval_result evaluate_template_value_arg(
    const k::parse::ast::expression* expr,
    const element& context_elem,
    const std::shared_ptr<context>& ctx,
    const std::shared_ptr<type>& expected_type,
    unit* unit_ptr) {

    constexpr_eval_result out;
    if (!expr) return out;

    // Special-case aggregate or array brace-init when the expected type is specified.
    if (auto bil = dynamic_cast<const brace_init_list*>(expr)) {
        if (expected_type && type::is_array(type::remove_const(expected_type))) {
            auto arr_eval = eval_array_init(*bil, context_elem, ctx, expected_type, unit_ptr);
            if (arr_eval.ok) {
                auto vt = arr_eval.value.to_value_type();
                if (vt.has_value()) {
                    out.status = constexpr_eval_status::OK;
                    out.value = *vt;
                    return out;
                }
            }
            if (arr_eval.is_hard_error()) {
                out.status = constexpr_eval_status::ERROR;
                out.error_code = arr_eval.error_code;
                out.message = arr_eval.message;
                out.message_args = arr_eval.message_args;
                return out;
            }
        } else {
            auto agg_eval = eval_aggregate_init(*bil, context_elem, ctx, expected_type, unit_ptr);
            if (agg_eval.ok) {
                auto vt = agg_eval.value.to_value_type();
                if (vt.has_value()) {
                    out.status = constexpr_eval_status::OK;
                    out.value = *vt;
                    return out;
                }
            }
            if (agg_eval.is_hard_error()) {
                out.status = constexpr_eval_status::ERROR;
                out.error_code = agg_eval.error_code;
                out.message = agg_eval.message;
                out.message_args = agg_eval.message_args;
                return out;
            }
        }
    }

    auto raw = eval_raw(expr, context_elem, ctx, unit_ptr);
    if (!raw.ok) {
        if (raw.is_hard_error()) {
            out.status = constexpr_eval_status::ERROR;
            out.error_code = raw.error_code;
            out.message = raw.message;
            out.message_args = raw.message_args;
        }
        return out;
    }

    if (expected_type) {
        auto bare_expected = type::remove_const(expected_type);

        if (auto exp_enum = std::dynamic_pointer_cast<enum_type>(bare_expected)) {
            auto target_enum = exp_enum->get_enumeration();
            if (!raw.value.is_enum() || raw.value.get_enum().enum_def != target_enum) {
                out.status = constexpr_eval_status::ERROR;
                out.error_code = static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_VALUE_ARG_TYPE_MISMATCH);
                out.message = "template value argument must be a constant of enum type '"
                    + (target_enum ? target_enum->get_short_name() : std::string("?")) + "'";
                return out;
            }
            auto vt = raw.value.to_value_type();
            if (vt.has_value()) {
                out.status = constexpr_eval_status::OK;
                out.value = *vt;
                return out;
            }
        }

        if (auto exp_prim = std::dynamic_pointer_cast<primitive_type>(bare_expected)) {
            if (!raw.value.is_numeric() && !raw.value.is_bool()) {
                out.status = constexpr_eval_status::ERROR;
                out.error_code = static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_VALUE_ARG_TYPE_MISMATCH);
                out.message = "template value argument is not compatible with the declared parameter type";
                return out;
            }
            auto casted = constant_evaluator::cast_to_type(raw.value, exp_prim);
            if (!casted.has_value()) {
                out.status = constexpr_eval_status::ERROR;
                out.error_code = static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_VALUE_ARG_TYPE_MISMATCH);
                out.message = "template value argument is not compatible with the declared parameter type";
                return out;
            }
            auto vt = casted->to_value_type();
            if (vt.has_value()) {
                out.status = constexpr_eval_status::OK;
                out.value = *vt;
                return out;
            }
        }

        if (auto exp_struct = std::dynamic_pointer_cast<struct_type>(bare_expected)) {
            auto target_agg = exp_struct->get_struct();
            if (!raw.value.is_struct() || !raw.value.get_struct() || !target_agg || raw.value.get_struct()->get_type() != target_agg) {
                out.status = constexpr_eval_status::ERROR;
                out.error_code = static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_VALUE_ARG_TYPE_MISMATCH);
                out.message = "template value argument is not compatible with the declared aggregate parameter type";
                return out;
            }
            auto vt = raw.value.to_value_type();
            if (vt.has_value()) {
                out.status = constexpr_eval_status::OK;
                out.value = *vt;
                return out;
            }
        }
    }

    // No expected type known or passthrough (strings, unconstrained primitives, etc.)
    auto vt = raw.value.to_value_type();
    if (vt.has_value()) {
        out.status = constexpr_eval_status::OK;
        out.value = *vt;
    } else {
        out.status = constexpr_eval_status::DEFER;
    }
    return out;
}

constexpr_eval_result evaluate_template_value_arg_from_type_spec(
    const k::parse::ast::type_specifier* spec,
    const element& context_elem,
    const std::shared_ptr<context>& ctx,
    const std::shared_ptr<type>& expected_type,
    unit* unit_ptr) {

    constexpr_eval_result out;
    auto ident_spec = dynamic_cast<const k::parse::ast::identified_type_specifier*>(spec);
    if (!ident_spec || ident_spec->has_explicit_template_args) {
        return out; // DEFER: not a bare qualified name
    }
    k::parse::ast::identifier_expr fake_ident(ident_spec->name);
    return evaluate_template_value_arg(&fake_ident, context_elem, ctx, expected_type, unit_ptr);
}

} // namespace k::model::gen
