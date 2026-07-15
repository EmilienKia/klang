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

#ifndef KLANG_GEN_OPERATORS_HELPERS_HPP
#define KLANG_GEN_OPERATORS_HELPERS_HPP
#include "resolvers.hpp"
#include "generators.hpp"
#include "gen_helpers.hpp"
#include "../common/operator_names.hpp"
#include "../parse/ast.hpp"

#include "../errors.hpp"

namespace k::model::gen {



namespace {

/**
 * Encode a resolved model type to the same string format used by the parser
 * for casting operator canonical names.
 * E.g. int → "int", double* → "doublep", const int& → "intcr", struct Foo → "Foo"
 */
std::string encode_type_for_cast_operator(const std::shared_ptr<type>& t) {
    if (!t) return "void";

    // Remove const wrapper
    if (type::is_const(t)) {
        return encode_type_for_cast_operator(t->get_subtype()) + "c";
    }

    // Primitive types
    if (auto pt = std::dynamic_pointer_cast<primitive_type>(t)) {
        switch (pt->get_type()) {
            case primitive_type::BOOL: return "bool";
            case primitive_type::CHAR: return "char";
            case primitive_type::BYTE: return "byte";
            case primitive_type::UNSIGNED_BYTE: return "ubyte";
            case primitive_type::SHORT: return pt->is_unsigned() ? "ushort" : "short";
            case primitive_type::INT: return pt->is_unsigned() ? "uint" : "int";
            case primitive_type::LONG: return pt->is_unsigned() ? "ulong" : "long";
            case primitive_type::LONG_LONG: return pt->is_unsigned() ? "ulonglong" : "longlong";
            case primitive_type::FLOAT: return "float";
            case primitive_type::DOUBLE: return "double";
            default: return "unknown";
        }
    }

    // Pointer types
    if (type::is_pointer(t)) return encode_type_for_cast_operator(t->get_subtype()) + "p";
    if (type::is_reference(t)) return encode_type_for_cast_operator(t->get_subtype()) + "r";
    if (type::is_link(t)) return encode_type_for_cast_operator(t->get_subtype()) + "lnk";
    if (type::is_view(t)) return encode_type_for_cast_operator(t->get_subtype()) + "l";
    if (type::is_owner(t)) return encode_type_for_cast_operator(t->get_subtype()) + "o";

    // Struct types
    if (auto st = std::dynamic_pointer_cast<struct_type>(t)) {
        return st->name();
    }

    return "unknown";
}

} // anonymous namespace

namespace {

/**
 * Swap pairing used by comparison-operator fallback synthesis (and by direct spaceship-
 * fallback codegen): the operator obtained by reversing operand order.
 * == <-> ==, != <-> !=, < <-> >, <= <-> >=. Returns empty string for non-comparison
 * operator names. Shared between gen_operators_overload.cpp (tier resolution) and
 * gen_operators_logical.cpp (SPACESHIP_SWAP codegen).
 */
std::string swap_of_cmp_op(const std::string& op) {
    if (op == k::op::OP_EQ) return k::op::OP_EQ;
    if (op == k::op::OP_NE) return k::op::OP_NE;
    if (op == k::op::OP_LT) return k::op::OP_GT;
    if (op == k::op::OP_GT) return k::op::OP_LT;
    if (op == k::op::OP_LE) return k::op::OP_GE;
    if (op == k::op::OP_GE) return k::op::OP_LE;
    return {};
}

} // anonymous namespace

namespace {

/**
 * True if `t` (once const-stripped) is a signed integer or floating-point primitive —
 * the only return types accepted for a declared `operator <=>` in Phase 1. Shared between
 * the spaceship direct-use type resolution (gen_operators_spaceship.cpp) and the
 * comparison-operator fallback synthesis (resolve_comparison_with_fallback, in
 * gen_operators_overload.cpp) which probes `operator <=>` as a fallback source. See
 * doc/spec/language/functions/operators.md, "Overloadable operators".
 */
bool is_valid_spaceship_return_type(const std::shared_ptr<type>& t) {
    if (!t) return false;
    auto rt = type::remove_const(t);
    auto pt = std::dynamic_pointer_cast<primitive_type>(rt);
    if (!pt) return false;
    if (pt->is_float()) return true;
    return pt->is_integer() && pt->is_signed();
}

} // anonymous namespace

namespace {

/**
 * Get the canonical operator function name for a binary expression.
 * Returns empty string if the expression type does not map to an overloadable operator.
 */
std::string get_binary_operator_name(const binary_expression& expr) {
    if (dynamic_cast<const addition_expression*>(&expr)) return "__operator_pl_";
    if (dynamic_cast<const substraction_expression*>(&expr)) return "__operator_mi_";
    if (dynamic_cast<const multiplication_expression*>(&expr)) return "__operator_ml_";
    if (dynamic_cast<const division_expression*>(&expr)) return "__operator_dv_";
    if (dynamic_cast<const modulo_expression*>(&expr)) return "__operator_rm_";
    if (dynamic_cast<const bitwise_and_expression*>(&expr)) return "__operator_an_";
    if (dynamic_cast<const bitwise_or_expression*>(&expr)) return "__operator_or_";
    if (dynamic_cast<const bitwise_xor_expression*>(&expr)) return "__operator_eo_";
    if (dynamic_cast<const left_shift_expression*>(&expr)) return "__operator_ls_";
    if (dynamic_cast<const right_shift_expression*>(&expr)) return "__operator_rs_";
    if (dynamic_cast<const logical_and_expression*>(&expr)) return "__operator_aa_";
    if (dynamic_cast<const logical_or_expression*>(&expr)) return "__operator_oo_";
    if (dynamic_cast<const equal_expression*>(&expr)) return "__operator_eq_";
    if (dynamic_cast<const different_expression*>(&expr)) return "__operator_ne_";
    if (dynamic_cast<const lesser_expression*>(&expr)) return "__operator_lt_";
    if (dynamic_cast<const greater_expression*>(&expr)) return "__operator_gt_";
    if (dynamic_cast<const lesser_equal_expression*>(&expr)) return "__operator_le_";
    if (dynamic_cast<const greater_equal_expression*>(&expr)) return "__operator_ge_";
    if (dynamic_cast<const spaceship_expression*>(&expr)) return "__operator_ss_";
    if (dynamic_cast<const simple_assignation_expression*>(&expr)) return "__operator_aS_";
    if (dynamic_cast<const additition_assignation_expression*>(&expr)) return "__operator_pL_";
    if (dynamic_cast<const substraction_assignation_expression*>(&expr)) return "__operator_mI_";
    if (dynamic_cast<const multiplication_assignation_expression*>(&expr)) return "__operator_mL_";
    if (dynamic_cast<const division_assignation_expression*>(&expr)) return "__operator_dV_";
    if (dynamic_cast<const modulo_assignation_expression*>(&expr)) return "__operator_rM_";
    if (dynamic_cast<const bitwise_and_assignation_expression*>(&expr)) return "__operator_aN_";
    if (dynamic_cast<const bitwise_or_assignation_expression*>(&expr)) return "__operator_oR_";
    if (dynamic_cast<const bitwise_xor_assignation_expression*>(&expr)) return "__operator_eO_";
    if (dynamic_cast<const left_shift_assignation_expression*>(&expr)) return "__operator_lS_";
    if (dynamic_cast<const right_shift_assignation_expression*>(&expr)) return "__operator_rS_";
    if (dynamic_cast<const subscript_expression*>(&expr)) return "__operator_ix_";
    return "";
}

/**
 * Get the canonical operator function name for a unary expression.
 * Returns empty string if the expression type does not map to an overloadable operator.
 */
std::string get_unary_operator_name(const unary_expression& expr) {
    if (dynamic_cast<const unary_plus_expression*>(&expr)) return "__operator_pl_";
    if (dynamic_cast<const unary_minus_expression*>(&expr)) return "__operator_mi_";
    if (dynamic_cast<const bitwise_not_expression*>(&expr)) return "__operator_co_";
    if (dynamic_cast<const logical_not_expression*>(&expr)) return "__operator_nt_";
    if (dynamic_cast<const prefix_increment_expression*>(&expr)) return "__operator_pp_";
    if (dynamic_cast<const prefix_decrement_expression*>(&expr)) return "__operator_mm_";
    if (dynamic_cast<const postfix_increment_expression*>(&expr)) return "__operator_PP_";
    if (dynamic_cast<const postfix_decrement_expression*>(&expr)) return "__operator_MM_";
    return "";
}

/**
 * Get a human-readable operator symbol from the canonical operator function name.
 * Used in error messages.
 */
std::string get_operator_symbol(const std::string& op_name) {
    return k::op::get_operator_symbol(op_name);
}

/**
 * Collect member operator functions from an aggregate and its full inheritance
 * hierarchy, following C++-style name-hiding semantics:
 *  - If the aggregate itself declares any function named op_name, return those only.
 *  - Otherwise, recurse into direct bases (BFS order, diamond-safe via visited set).
 * This ensures inherited operators are found when a derived class does not re-declare them.
 */
static std::vector<std::shared_ptr<function>>
collect_member_operators_from_hierarchy(
    const std::shared_ptr<aggregate>& agg,
    const std::string& op_name,
    std::vector<const aggregate*>& visited)
{
    if (!agg) return {};
    for (auto* v : visited) if (v == agg.get()) return {};
    visited.push_back(agg.get());

    // Direct members first (name hiding: if anything declared here, stop)
    auto direct = agg->get_functions(op_name);
    if (!direct.empty()) return direct;

    // Nothing at this level — recurse into bases
    std::vector<std::shared_ptr<function>> result;
    for (const auto& bs : agg->get_bases()) {
        if (!bs.base) continue;
        auto from_base = collect_member_operators_from_hierarchy(bs.base, op_name, visited);
        for (auto& f : from_base) {
            if (std::find(result.begin(), result.end(), f) == result.end())
                result.push_back(f);
        }
    }
    return result;
}

static std::vector<std::shared_ptr<function>>
collect_member_operators_from_hierarchy(
    const std::shared_ptr<aggregate>& agg,
    const std::string& op_name)
{
    std::vector<const aggregate*> visited;
    return collect_member_operators_from_hierarchy(agg, op_name, visited);
}

} // anonymous namespace

} // namespace k::model::gen

#endif //KLANG_GEN_OPERATORS_HELPERS_HPP
