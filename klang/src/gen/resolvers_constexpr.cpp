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

#include "../model/model.hpp"
#include "../model/type.hpp"
#include "../model/template.hpp"
#include "../parse/ast.hpp"
#include "../errors.hpp"

#include <cmath>
#include <cstring>

namespace k::model::gen {

namespace {

using k::parse::ast::expression;
using k::parse::ast::literal_expr;
using k::parse::ast::identifier_expr;
using k::parse::ast::unary_prefix_expr;
using k::parse::ast::binary_operator_expr;
using k::parse::ast::conditional_expr;
using k::parse::ast::cast_expr;

/** Intermediate evaluation result: a scalar value plus, when it directly
 *  denotes an enum entry, the enumeration it belongs to (used to validate
 *  enum-typed value parameters strictly — arithmetic on enum entries is not
 *  considered to still be "of that enum type"). */
struct raw_result {
    k::value_type value;
    std::shared_ptr<enumeration> source_enum;
};

struct eval_outcome {
    bool ok = false;
    raw_result result;
    unsigned int error_code = 0;
    std::string message;
    std::vector<std::string> message_args;

    static eval_outcome success(k::value_type v, std::shared_ptr<enumeration> en = nullptr) {
        eval_outcome o;
        o.ok = true;
        o.result = raw_result{std::move(v), std::move(en)};
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

/** Extract a generic numeric view (int64_t + double, tagged is_float) from a k::value_type.
 *  Returns false for monostate/nullptr/string (non-numeric). */
bool as_numeric(const k::value_type& v, bool& is_float, int64_t& ival, double& fval) {
    return std::visit([&](auto&& x) -> bool {
        using T = std::decay_t<decltype(x)>;
        if constexpr (std::is_same_v<T, std::monostate> || std::is_same_v<T, std::nullptr_t>
                      || std::is_same_v<T, std::string>) {
            return false;
        } else if constexpr (std::is_floating_point_v<T>) {
            is_float = true;
            fval = static_cast<double>(x);
            ival = static_cast<int64_t>(x);
            return true;
        } else {
            is_float = false;
            ival = static_cast<int64_t>(x);
            fval = static_cast<double>(x);
            return true;
        }
    }, v);
}

/** Build a k::value_type of exactly the C++ alternative matching `kind`, from a signed
 *  64-bit magnitude (used both for narrowing to a declared parameter type and for
 *  reconstructing an enum entry's underlying representation). */
k::value_type narrow_int_to_primitive(int64_t v, primitive_type::PRIMITIVE_TYPE kind) {
    switch (kind) {
        case primitive_type::BOOL:               return k::value_type{ v != 0 };
        case primitive_type::CHAR:                return k::value_type{ static_cast<char>(v) };
        case primitive_type::UNSIGNED_BYTE:       return k::value_type{ static_cast<unsigned char>(v) };
        case primitive_type::SHORT:               return k::value_type{ static_cast<short>(v) };
        case primitive_type::UNSIGNED_SHORT:      return k::value_type{ static_cast<unsigned short>(v) };
        case primitive_type::INT:                 return k::value_type{ static_cast<int>(v) };
        case primitive_type::UNSIGNED_INT:        return k::value_type{ static_cast<unsigned int>(v) };
        case primitive_type::LONG:                return k::value_type{ static_cast<long>(v) };
        case primitive_type::UNSIGNED_LONG:       return k::value_type{ static_cast<unsigned long>(v) };
        case primitive_type::LONG_LONG:           return k::value_type{ static_cast<long long>(v) };
        case primitive_type::UNSIGNED_LONG_LONG:  return k::value_type{ static_cast<unsigned long long>(v) };
        case primitive_type::FLOAT:               return k::value_type{ static_cast<float>(v) };
        case primitive_type::DOUBLE:              return k::value_type{ static_cast<double>(v) };
        default:                                  return k::value_type{ static_cast<int>(v) };
    }
}

k::value_type narrow_double_to_primitive(double v, primitive_type::PRIMITIVE_TYPE kind) {
    switch (kind) {
        case primitive_type::FLOAT:  return k::value_type{ static_cast<float>(v) };
        case primitive_type::DOUBLE: return k::value_type{ v };
        default:                     return narrow_int_to_primitive(static_cast<int64_t>(v), kind);
    }
}

/** Default (un-narrowed) representation, used when no expected type is known. */
k::value_type default_int_value(int64_t v) { return k::value_type{ static_cast<int>(v) }; }
k::value_type default_float_value(double v) { return k::value_type{ v }; }

/** Resolve `expected_type` (possibly const/wrapped) down to a primitive_type or enum_type,
 *  or nullptr if neither (in which case narrowing is skipped). */
void unwrap_expected_type(const std::shared_ptr<type>& expected_type,
                           std::shared_ptr<primitive_type>& out_prim,
                           std::shared_ptr<enum_type>& out_enum) {
    if (!expected_type) return;
    auto t = type::remove_const(expected_type);
    out_prim = std::dynamic_pointer_cast<primitive_type>(t);
    out_enum = std::dynamic_pointer_cast<enum_type>(t);
}

// ─────────────────────────────────────────────────────────────────────────────
// Enum-entry / dependent value-parameter identifier resolution
// ─────────────────────────────────────────────────────────────────────────────

/** Mirrors the "EnumName::entryName" / "UnionName::Kind::entryName" resolution
 *  performed for ordinary symbol expressions in gen_expressions.cpp, but
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
// Recursive raw evaluator
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
            return eval_outcome::success(k::value_type{v});
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
            return eval_outcome::success(*v);
        }
        return eval_outcome::defer();
    }

    if (auto en = lookup_enum_for_qualified_entry(context_elem, q, unit_ptr, ctx)) {
        const std::string entry_name = q[q.size() - 1];
        auto entry = en->get_entry_by_name(entry_name);
        if (!entry.has_value()) {
            return eval_outcome::error(
                static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_VALUE_ARG_NOT_CONSTANT),
                "enum '" + en->get_short_name() + "' has no entry named '" + entry_name + "'",
                {en->get_short_name(), entry_name});
        }
        k::value_type underlying;
        if (auto ut = en->get_underlying_type()) {
            underlying = narrow_int_to_primitive(entry->value, ut->get_type());
        } else {
            underlying = default_int_value(entry->value);
        }
        return eval_outcome::success(std::move(underlying), en);
    }

    return eval_outcome::defer();
}

eval_outcome eval_unary(const unary_prefix_expr& ue,
                         const element& context_elem,
                         const std::shared_ptr<context>& ctx,
                         unit* unit_ptr) {
    auto sub = eval_raw(ue.expr().get(), context_elem, ctx, unit_ptr);
    if (!sub.ok) return sub;

    bool is_float = false; int64_t ival = 0; double fval = 0.0;
    if (!as_numeric(sub.result.value, is_float, ival, fval)) {
        return eval_outcome::error(
            static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_VALUE_ARG_TYPE_MISMATCH),
            "unary operator applied to a non-numeric constant value");
    }

    switch (ue.op.type) {
        case lex::operator_::MINUS:
            return is_float ? eval_outcome::success(default_float_value(-fval))
                             : eval_outcome::success(default_int_value(-ival));
        case lex::operator_::PLUS:
            return is_float ? eval_outcome::success(default_float_value(fval))
                             : eval_outcome::success(default_int_value(ival));
        case lex::operator_::EXCLAMATION_MARK:
            return eval_outcome::success(k::value_type{ is_float ? (fval == 0.0) : (ival == 0) });
        case lex::operator_::TILDE:
            if (is_float) {
                return eval_outcome::error(
                    static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_VALUE_ARG_TYPE_MISMATCH),
                    "bitwise complement '~' requires an integer constant");
            }
            return eval_outcome::success(default_int_value(~ival));
        default:
            return eval_outcome::error(
                static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_VALUE_ARG_NOT_CONSTANT),
                "unsupported unary operator in template value argument");
    }
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
        bool lf; int64_t li; double ld;
        if (!as_numeric(l.result.value, lf, li, ld)) {
            return eval_outcome::error(
                static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_VALUE_ARG_TYPE_MISMATCH),
                "logical operator applied to a non-numeric constant value");
        }
        bool lbool = lf ? (ld != 0.0) : (li != 0);
        if (op == op_t::DOUBLE_AMPERSAND && !lbool) return eval_outcome::success(k::value_type{false});
        if (op == op_t::DOUBLE_PIPE && lbool) return eval_outcome::success(k::value_type{true});
        auto r = eval_raw(be.rexpr().get(), context_elem, ctx, unit_ptr);
        if (!r.ok) return r;
        bool rf; int64_t ri; double rd;
        if (!as_numeric(r.result.value, rf, ri, rd)) {
            return eval_outcome::error(
                static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_VALUE_ARG_TYPE_MISMATCH),
                "logical operator applied to a non-numeric constant value");
        }
        bool rbool = rf ? (rd != 0.0) : (ri != 0);
        return eval_outcome::success(k::value_type{ rbool });
    }

    auto l = eval_raw(be.lexpr().get(), context_elem, ctx, unit_ptr);
    if (!l.ok) return l;
    auto r = eval_raw(be.rexpr().get(), context_elem, ctx, unit_ptr);
    if (!r.ok) return r;

    bool lf, rf; int64_t li, ri; double ld, rd;
    if (!as_numeric(l.result.value, lf, li, ld) || !as_numeric(r.result.value, rf, ri, rd)) {
        return eval_outcome::error(
            static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_VALUE_ARG_TYPE_MISMATCH),
            "binary operator applied to a non-numeric constant value");
    }
    bool use_float = lf || rf;

    switch (op) {
        case op_t::PLUS:  return use_float ? eval_outcome::success(default_float_value(ld + rd))
                                            : eval_outcome::success(default_int_value(li + ri));
        case op_t::MINUS: return use_float ? eval_outcome::success(default_float_value(ld - rd))
                                            : eval_outcome::success(default_int_value(li - ri));
        case op_t::STAR:  return use_float ? eval_outcome::success(default_float_value(ld * rd))
                                            : eval_outcome::success(default_int_value(li * ri));
        case op_t::SLASH:
            if (use_float) {
                if (rd == 0.0) {
                    return eval_outcome::error(
                        static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_VALUE_ARG_NOT_CONSTANT),
                        "division by zero in template value argument constant expression");
                }
                return eval_outcome::success(default_float_value(ld / rd));
            }
            if (ri == 0) {
                return eval_outcome::error(
                    static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_VALUE_ARG_NOT_CONSTANT),
                    "division by zero in template value argument constant expression");
            }
            return eval_outcome::success(default_int_value(li / ri));
        case op_t::PERCENT:
            if (use_float) {
                return eval_outcome::error(
                    static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_VALUE_ARG_TYPE_MISMATCH),
                    "operator '%' requires integer constant operands");
            }
            if (ri == 0) {
                return eval_outcome::error(
                    static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_VALUE_ARG_NOT_CONSTANT),
                    "modulo by zero in template value argument constant expression");
            }
            return eval_outcome::success(default_int_value(li % ri));
        case op_t::AMPERSAND:
            if (use_float) break;
            return eval_outcome::success(default_int_value(li & ri));
        case op_t::PIPE:
            if (use_float) break;
            return eval_outcome::success(default_int_value(li | ri));
        case op_t::CARET:
            if (use_float) break;
            return eval_outcome::success(default_int_value(li ^ ri));
        case op_t::DOUBLE_CHEVRON_OPEN:
            if (use_float) break;
            return eval_outcome::success(default_int_value(li << ri));
        case op_t::DOUBLE_CHEVRON_CLOSE:
            if (use_float) break;
            return eval_outcome::success(default_int_value(li >> ri));
        case op_t::DOUBLE_EQUAL:
            return eval_outcome::success(k::value_type{ use_float ? (ld == rd) : (li == ri) });
        case op_t::EXCLAMATION_MARK_EQUAL:
            return eval_outcome::success(k::value_type{ use_float ? (ld != rd) : (li != ri) });
        case op_t::CHEVRON_OPEN:
            return eval_outcome::success(k::value_type{ use_float ? (ld < rd) : (li < ri) });
        case op_t::CHEVRON_CLOSE:
            return eval_outcome::success(k::value_type{ use_float ? (ld > rd) : (li > ri) });
        case op_t::CHEVRON_OPEN_EQUAL:
            return eval_outcome::success(k::value_type{ use_float ? (ld <= rd) : (li <= ri) });
        case op_t::CHEVRON_CLOSE_EQUAL:
            return eval_outcome::success(k::value_type{ use_float ? (ld >= rd) : (li >= ri) });
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
    bool cf; int64_t ci; double cd;
    if (!as_numeric(cond.result.value, cf, ci, cd)) {
        return eval_outcome::error(
            static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_VALUE_ARG_TYPE_MISMATCH),
            "ternary condition is not a numeric constant");
    }
    bool taken = cf ? (cd != 0.0) : (ci != 0);
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

    std::shared_ptr<primitive_type> prim;
    std::shared_ptr<enum_type> en;
    unwrap_expected_type(target, prim, en);
    if (!prim) {
        return eval_outcome::error(
            static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_VALUE_ARG_TYPE_MISMATCH),
            "cast target in template value argument must be a primitive type");
    }

    bool isf; int64_t iv; double fv;
    if (!as_numeric(sub.result.value, isf, iv, fv)) {
        return eval_outcome::error(
            static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_VALUE_ARG_TYPE_MISMATCH),
            "cast applied to a non-numeric constant value");
    }
    return eval_outcome::success(isf ? narrow_double_to_primitive(fv, prim->get_type())
                                      : narrow_int_to_primitive(iv, prim->get_type()));
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

    std::shared_ptr<primitive_type> exp_prim;
    std::shared_ptr<enum_type> exp_enum;
    unwrap_expected_type(expected_type, exp_prim, exp_enum);

    if (exp_enum) {
        auto target_enum = exp_enum->get_enumeration();
        if (!raw.result.source_enum || raw.result.source_enum != target_enum) {
            out.status = constexpr_eval_status::ERROR;
            out.error_code = static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_VALUE_ARG_TYPE_MISMATCH);
            out.message = "template value argument must be a constant of enum type '"
                + (target_enum ? target_enum->get_short_name() : std::string("?")) + "'";
            return out;
        }
        out.status = constexpr_eval_status::OK;
        out.value = raw.result.value;
        return out;
    }

    if (exp_prim) {
        bool isf; int64_t iv; double fv;
        if (!as_numeric(raw.result.value, isf, iv, fv)) {
            out.status = constexpr_eval_status::ERROR;
            out.error_code = static_cast<unsigned int>(k::diag::template_diag::ERR_TPL_VALUE_ARG_TYPE_MISMATCH);
            out.message = "template value argument is not compatible with the declared parameter type";
            return out;
        }
        out.status = constexpr_eval_status::OK;
        out.value = isf ? narrow_double_to_primitive(fv, exp_prim->get_type())
                         : narrow_int_to_primitive(iv, exp_prim->get_type());
        return out;
    }

    // No expected type known: keep the value as evaluated (string literals pass through here too).
    out.status = constexpr_eval_status::OK;
    out.value = raw.result.value;
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
