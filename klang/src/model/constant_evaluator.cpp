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

#include "constant_evaluator.hpp"
#include "type.hpp"
#include "model_aggregate.hpp"
#include "model_enum.hpp"
#include "model_union.hpp"
#include "../parse/ast.hpp"

#include <cmath>

namespace k::model {

namespace {

bool is_synthetic_member(const std::string& name) {
    return name.size() >= 4 && name[0] == '_' && name[1] == '_'
        && name[name.size() - 1] == '_' && name[name.size() - 2] == '_';
}

std::shared_ptr<type> unwrap_resolved(std::shared_ptr<type> t) {
    if (!t) return nullptr;
    t = type::canonical(type::remove_const(t));
    if (auto unres = std::dynamic_pointer_cast<unresolved_type>(t)) {
        if (unres->get_resolved()) {
            return unwrap_resolved(unres->get_resolved());
        }
    }
    return t;
}

} // anonymous namespace

std::optional<constant_value> constant_evaluator::cast_to_type(
    const constant_value& val,
    const std::shared_ptr<type>& target_type
) {
    if (!val.is_valid() || !target_type) {
        return std::nullopt;
    }

    auto bare_target = unwrap_resolved(target_type);
    if (type::is_reference(bare_target)) {
        bare_target = unwrap_resolved(bare_target->get_subtype());
    }

    if (auto prim = std::dynamic_pointer_cast<primitive_type>(bare_target)) {
        bool is_float = false;
        int64_t ival = 0;
        double fval = 0.0;
        if (!val.as_numeric(is_float, ival, fval)) {
            return std::nullopt;
        }

        switch (prim->get_type()) {
            case primitive_type::BOOL:
                return constant_value(is_float ? (fval != 0.0) : (ival != 0));
            case primitive_type::CHAR:
                return constant_value(static_cast<char>(is_float ? static_cast<int64_t>(fval) : ival));
            case primitive_type::BYTE:
            case primitive_type::UNSIGNED_BYTE:
                return constant_value(static_cast<unsigned char>(is_float ? static_cast<int64_t>(fval) : ival));
            case primitive_type::SHORT:
                return constant_value(static_cast<short>(is_float ? static_cast<int64_t>(fval) : ival));
            case primitive_type::UNSIGNED_SHORT:
                return constant_value(static_cast<unsigned short>(is_float ? static_cast<int64_t>(fval) : ival));
            case primitive_type::INT:
                return constant_value(static_cast<int>(is_float ? static_cast<int64_t>(fval) : ival));
            case primitive_type::UNSIGNED_INT:
                return constant_value(static_cast<unsigned int>(is_float ? static_cast<uint64_t>(fval) : static_cast<uint64_t>(ival)));
            case primitive_type::LONG:
                return constant_value(static_cast<long>(is_float ? static_cast<int64_t>(fval) : ival));
            case primitive_type::UNSIGNED_LONG:
                return constant_value(static_cast<unsigned long>(is_float ? static_cast<uint64_t>(fval) : static_cast<uint64_t>(ival)));
            case primitive_type::LONG_LONG:
                return constant_value(static_cast<long long>(is_float ? static_cast<int64_t>(fval) : ival));
            case primitive_type::UNSIGNED_LONG_LONG:
                return constant_value(static_cast<unsigned long long>(is_float ? static_cast<uint64_t>(fval) : static_cast<uint64_t>(ival)));
            case primitive_type::FLOAT:
                return constant_value(static_cast<float>(is_float ? fval : static_cast<double>(ival)));
            case primitive_type::DOUBLE:
                return constant_value(is_float ? fval : static_cast<double>(ival));
            default:
                return std::nullopt;
        }
    }

    if (auto en_type = std::dynamic_pointer_cast<enum_type>(bare_target)) {
        auto en_def = en_type->get_enumeration();
        if (!en_def) return std::nullopt;

        if (val.is_enum()) {
            const auto& ev = val.get_enum();
            if (ev.enum_def == en_def) {
                return val;
            }
        }

        bool is_float = false;
        int64_t ival = 0;
        double fval = 0.0;
        if (val.as_numeric(is_float, ival, fval)) {
            int64_t target_int = is_float ? static_cast<int64_t>(fval) : ival;
            size_t idx = 0;
            for (const auto& entry : en_def->entries()) {
                if (entry.value == target_int) {
                    enum_value ev{en_def, idx, entry.value, entry.name};
                    return constant_value(ev);
                }
                ++idx;
            }
            // If not found in entries, still build enum_value with matching integer
            enum_value ev{en_def, 0, target_int, ""};
            return constant_value(ev);
        }
    }

    if (auto st_type = std::dynamic_pointer_cast<struct_type>(bare_target)) {
        if (val.is_struct()) {
            return val;
        }
        if (val.is_union()) {
            return val;
        }
    }

    if (auto arr_type = std::dynamic_pointer_cast<array_type>(bare_target)) {
        if (val.is_array()) {
            return val;
        }
    }

    if (type::is_any_indirection(bare_target)) {
        if (val.is_null()) {
            return val;
        }
    }

    return std::nullopt;
}

std::optional<constant_value> constant_evaluator::default_value_for_type(
    const std::shared_ptr<type>& target_type
) {
    if (!target_type) return std::nullopt;

    auto bare = unwrap_resolved(target_type);
    if (type::is_reference(bare)) {
        bare = unwrap_resolved(bare->get_subtype());
    }

    if (auto prim = std::dynamic_pointer_cast<primitive_type>(bare)) {
        if (prim->is_boolean()) return constant_value(false);
        if (prim->is_float()) return constant_value(prim->get_type() == primitive_type::FLOAT ? 0.0f : 0.0);
        return cast_to_type(constant_value(0), prim);
    }

    if (auto en_type = std::dynamic_pointer_cast<enum_type>(bare)) {
        auto en_def = en_type->get_enumeration();
        if (en_def && !en_def->entries().empty()) {
            // Find default entry or first entry
            size_t def_idx = 0;
            for (size_t i = 0; i < en_def->entries().size(); ++i) {
                if (en_def->entries()[i].is_default) {
                    def_idx = i;
                    break;
                }
            }
            const auto& e = en_def->entries()[def_idx];
            enum_value ev{en_def, def_idx, e.value, e.name};
            return constant_value(ev);
        }
        return std::nullopt;
    }

    if (auto st_type = std::dynamic_pointer_cast<struct_type>(bare)) {
        auto agg = st_type->get_struct();
        if (agg) {
            return eval_struct_init(agg, {});
        }
    }

    if (type::is_any_indirection(bare)) {
        return constant_value(nullptr);
    }

    return std::nullopt;
}

std::optional<constant_value> constant_evaluator::eval_binary_arithmetic(
    binary_arith_op op,
    const constant_value& left,
    const constant_value& right,
    const std::shared_ptr<type>& result_type
) {
    bool is_float_l = false, is_float_r = false;
    int64_t ival_l = 0, ival_r = 0;
    double fval_l = 0.0, fval_r = 0.0;

    if (!left.as_numeric(is_float_l, ival_l, fval_l) ||
        !right.as_numeric(is_float_r, ival_r, fval_r)) {
        return std::nullopt;
    }

    bool compute_as_float = is_float_l || is_float_r;
    if (result_type && type::is_prim_float(result_type)) {
        compute_as_float = true;
    }

    if (compute_as_float) {
        double l = is_float_l ? fval_l : static_cast<double>(ival_l);
        double r = is_float_r ? fval_r : static_cast<double>(ival_r);
        double res = 0.0;

        switch (op) {
            case binary_arith_op::ADD: res = l + r; break;
            case binary_arith_op::SUB: res = l - r; break;
            case binary_arith_op::MUL: res = l * r; break;
            case binary_arith_op::DIV:
                if (r == 0.0) return std::nullopt;
                res = l / r;
                break;
            case binary_arith_op::MOD:
                if (r == 0.0) return std::nullopt;
                res = std::fmod(l, r);
                break;
            default:
                return std::nullopt; // Bitwise operations not supported on float
        }

        if (result_type) {
            return cast_to_type(constant_value(res), result_type);
        }
        return constant_value(res);
    }

    // Integer operations
    int64_t l = ival_l;
    int64_t r = ival_r;
    int64_t res = 0;

    switch (op) {
        case binary_arith_op::ADD: res = l + r; break;
        case binary_arith_op::SUB: res = l - r; break;
        case binary_arith_op::MUL: res = l * r; break;
        case binary_arith_op::DIV:
            if (r == 0) return std::nullopt; // Division by zero
            res = l / r;
            break;
        case binary_arith_op::MOD:
            if (r == 0) return std::nullopt; // Modulo by zero
            res = l % r;
            break;
        case binary_arith_op::BITWISE_AND: res = l & r; break;
        case binary_arith_op::BITWISE_OR:  res = l | r; break;
        case binary_arith_op::BITWISE_XOR: res = l ^ r; break;
        case binary_arith_op::SHIFT_LEFT:  res = l << r; break;
        case binary_arith_op::SHIFT_RIGHT: res = l >> r; break;
        default:
            return std::nullopt;
    }

    if (result_type) {
        return cast_to_type(constant_value(res), result_type);
    }
    return constant_value(res);
}

std::optional<constant_value> constant_evaluator::eval_comparison(
    comparison_op op,
    const constant_value& left,
    const constant_value& right
) {
    if (left.is_string() && right.is_string()) {
        const auto& s1 = left.get_string();
        const auto& s2 = right.get_string();
        switch (op) {
            case comparison_op::EQUAL: return constant_value(s1 == s2);
            case comparison_op::NOT_EQUAL: return constant_value(s1 != s2);
            case comparison_op::LESS: return constant_value(s1 < s2);
            case comparison_op::LESS_EQUAL: return constant_value(s1 <= s2);
            case comparison_op::GREATER: return constant_value(s1 > s2);
            case comparison_op::GREATER_EQUAL: return constant_value(s1 >= s2);
            case comparison_op::SPACESHIP:
                if (s1 < s2) return constant_value(-1);
                if (s1 == s2) return constant_value(0);
                return constant_value(1);
        }
    }

    bool is_float_l = false, is_float_r = false;
    int64_t ival_l = 0, ival_r = 0;
    double fval_l = 0.0, fval_r = 0.0;

    if (!left.as_numeric(is_float_l, ival_l, fval_l) ||
        !right.as_numeric(is_float_r, ival_r, fval_r)) {
        if (left.is_null() && right.is_null()) {
            if (op == comparison_op::EQUAL) return constant_value(true);
            if (op == comparison_op::NOT_EQUAL) return constant_value(false);
        }
        return std::nullopt;
    }

    if (is_float_l || is_float_r) {
        double l = is_float_l ? fval_l : static_cast<double>(ival_l);
        double r = is_float_r ? fval_r : static_cast<double>(ival_r);
        switch (op) {
            case comparison_op::EQUAL: return constant_value(l == r);
            case comparison_op::NOT_EQUAL: return constant_value(l != r);
            case comparison_op::LESS: return constant_value(l < r);
            case comparison_op::LESS_EQUAL: return constant_value(l <= r);
            case comparison_op::GREATER: return constant_value(l > r);
            case comparison_op::GREATER_EQUAL: return constant_value(l >= r);
            case comparison_op::SPACESHIP:
                if (l < r) return constant_value(-1);
                if (l == r) return constant_value(0);
                return constant_value(1);
        }
    } else {
        int64_t l = ival_l;
        int64_t r = ival_r;
        switch (op) {
            case comparison_op::EQUAL: return constant_value(l == r);
            case comparison_op::NOT_EQUAL: return constant_value(l != r);
            case comparison_op::LESS: return constant_value(l < r);
            case comparison_op::LESS_EQUAL: return constant_value(l <= r);
            case comparison_op::GREATER: return constant_value(l > r);
            case comparison_op::GREATER_EQUAL: return constant_value(l >= r);
            case comparison_op::SPACESHIP:
                if (l < r) return constant_value(-1);
                if (l == r) return constant_value(0);
                return constant_value(1);
        }
    }

    return std::nullopt;
}

std::optional<constant_value> constant_evaluator::eval_logical_binary(
    bool is_and,
    const constant_value& left,
    const constant_value& right
) {
    if (is_and) {
        return constant_value(left.get_bool() && right.get_bool());
    } else {
        return constant_value(left.get_bool() || right.get_bool());
    }
}

std::optional<constant_value> constant_evaluator::eval_unary(
    unary_op op,
    const constant_value& operand,
    const std::shared_ptr<type>& result_type
) {
    if (!operand.is_valid()) return std::nullopt;

    if (op == unary_op::LOGICAL_NOT) {
        return constant_value(!operand.get_bool());
    }

    bool is_float = false;
    int64_t ival = 0;
    double fval = 0.0;
    if (!operand.as_numeric(is_float, ival, fval)) {
        return std::nullopt;
    }

    switch (op) {
        case unary_op::PLUS:
            return result_type ? cast_to_type(operand, result_type) : operand;
        case unary_op::MINUS:
            if (is_float) {
                constant_value res(-fval);
                return result_type ? cast_to_type(res, result_type) : res;
            } else {
                constant_value res(-ival);
                return result_type ? cast_to_type(res, result_type) : res;
            }
        case unary_op::BITWISE_NOT:
            if (is_float) return std::nullopt;
            {
                constant_value res(~ival);
                return result_type ? cast_to_type(res, result_type) : res;
            }
        default:
            return std::nullopt;
    }
}

std::optional<constant_value> constant_evaluator::eval_ternary(
    const constant_value& cond,
    const constant_value& then_val,
    const constant_value& else_val
) {
    if (!cond.is_valid()) return std::nullopt;
    return cond.get_bool() ? then_val : else_val;
}

std::optional<constant_value> constant_evaluator::eval_member_access(
    const constant_value& base,
    const std::string& member_name
) {
    if (base.is_struct()) {
        auto sv = base.get_struct();
        if (sv) {
            return sv->get_field(member_name);
        }
    } else if (base.is_union()) {
        auto uv = base.get_union();
        if (uv && uv->get_alternative_name() == member_name) {
            return uv->get_active_value();
        }
    } else if (base.is_array()) {
        auto av = base.get_array();
        if (av && member_name == "size") {
            return constant_value(static_cast<unsigned int>(av->size()));
        }
    }
    return std::nullopt;
}

std::optional<constant_value> constant_evaluator::eval_array_subscript(
    const constant_value& base,
    const constant_value& index
) {
    if (base.is_array() && index.is_integer()) {
        auto av = base.get_array();
        if (!av) return std::nullopt;
        int64_t idx = index.get_int64();
        if (idx >= 0 && static_cast<size_t>(idx) < av->size()) {
            return av->get_element(static_cast<size_t>(idx));
        }
    }
    return std::nullopt;
}

std::optional<constant_value> constant_evaluator::eval_struct_init(
    const std::shared_ptr<aggregate>& struct_def,
    const std::map<std::string, constant_value>& field_values
) {
    if (!struct_def) return std::nullopt;

    // Collect all accessible member variables from the struct and its bases
    std::map<std::string, std::shared_ptr<type>> all_fields;

    // Gather from bases first
    auto all_bases = struct_def->get_all_bases();
    for (auto& base : all_bases) {
        if (!base.base) continue;
        for (auto& [name, var] : base.base->variables()) {
            if (is_synthetic_member(name)) continue;
            if (var && var->get_type()) {
                all_fields[name] = var->get_type();
            }
        }
    }

    // Gather from the struct itself
    for (auto& [name, var] : struct_def->variables()) {
        if (is_synthetic_member(name)) continue;
        if (var && var->get_type()) {
            all_fields[name] = var->get_type();
        }
    }

    std::map<std::string, constant_value> resolved_fields;
    for (const auto& [name, field_type] : all_fields) {
        auto it = field_values.find(name);
        if (it != field_values.end()) {
            auto cast_val = cast_to_type(it->second, field_type);
            if (!cast_val) return std::nullopt;
            resolved_fields[name] = *cast_val;
        } else {
            auto def_val = default_value_for_type(field_type);
            if (!def_val) return std::nullopt;
            resolved_fields[name] = *def_val;
        }
    }

    return constant_value(std::make_shared<struct_value>(struct_def, std::move(resolved_fields)));
}

std::optional<constant_value> constant_evaluator::eval_union_init(
    const std::shared_ptr<union_type_def>& union_def,
    const std::string& alt_name,
    const constant_value& alt_value
) {
    if (!union_def) return std::nullopt;

    const auto* alt = union_def->get_alternative_by_name(alt_name);
    if (!alt) return std::nullopt;

    auto cast_val = cast_to_type(alt_value, alt->resolved_type);
    if (!cast_val) return std::nullopt;

    return constant_value(std::make_shared<union_value>(union_def, alt->index, alt_name, *cast_val));
}

} // namespace k::model






