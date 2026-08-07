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

#ifndef KLANG_OPERATOR_NAMES_HPP
#define KLANG_OPERATOR_NAMES_HPP

#include <string>
#include <string_view>
#include <set>

/**
 * Centralized operator canonical name registry.
 *
 * Internal canonical names follow the pattern: __operator_XX_
 *   - Double underscore prefix to avoid collisions with user identifiers
 *   - XX is the Itanium C++ ABI 2-letter operator code
 *   - Trailing underscore for unambiguous parsing
 *
 * Mangling emits the raw 2-letter code (no length prefix).
 *
 * Itanium reference: https://itanium-cxx-abi.github.io/cxx-abi/abi.html#mangling-operator
 */
namespace k::op {

// ── Binary arithmetic ────────────────────────────────────────────────────────
inline constexpr const char* OP_ADD       = "__operator_pl_";   // +
inline constexpr const char* OP_SUB       = "__operator_mi_";   // -
inline constexpr const char* OP_MUL       = "__operator_ml_";   // *
inline constexpr const char* OP_DIV       = "__operator_dv_";   // /
inline constexpr const char* OP_MOD       = "__operator_rm_";   // %

// ── Bitwise ──────────────────────────────────────────────────────────────────
inline constexpr const char* OP_BITAND    = "__operator_an_";   // &
inline constexpr const char* OP_BITOR     = "__operator_or_";   // |
inline constexpr const char* OP_BITXOR    = "__operator_eo_";   // ^
inline constexpr const char* OP_BITNOT    = "__operator_co_";   // ~

// ── Shift ────────────────────────────────────────────────────────────────────
inline constexpr const char* OP_SHL       = "__operator_ls_";   // <<
inline constexpr const char* OP_SHR       = "__operator_rs_";   // >>

// ── Logical ──────────────────────────────────────────────────────────────────
inline constexpr const char* OP_AND       = "__operator_aa_";   // &&
inline constexpr const char* OP_OR        = "__operator_oo_";   // ||
inline constexpr const char* OP_NOT       = "__operator_nt_";   // !

// ── Comparison ───────────────────────────────────────────────────────────────
inline constexpr const char* OP_EQ        = "__operator_eq_";   // ==
inline constexpr const char* OP_NE        = "__operator_ne_";   // !=
inline constexpr const char* OP_LT        = "__operator_lt_";   // <
inline constexpr const char* OP_GT        = "__operator_gt_";   // >
inline constexpr const char* OP_LE        = "__operator_le_";   // <=
inline constexpr const char* OP_GE        = "__operator_ge_";   // >=
inline constexpr const char* OP_SPACESHIP = "__operator_ss_";   // <=> (three-way comparison)

// ── Assignment ───────────────────────────────────────────────────────────────
inline constexpr const char* OP_ASSIGN    = "__operator_aS_";   // =
inline constexpr const char* OP_ADD_ASGN  = "__operator_pL_";   // +=
inline constexpr const char* OP_SUB_ASGN  = "__operator_mI_";   // -=
inline constexpr const char* OP_MUL_ASGN  = "__operator_mL_";   // *=
inline constexpr const char* OP_DIV_ASGN  = "__operator_dV_";   // /=
inline constexpr const char* OP_MOD_ASGN  = "__operator_rM_";   // %=
inline constexpr const char* OP_BITAND_ASGN = "__operator_aN_"; // &=
inline constexpr const char* OP_BITOR_ASGN  = "__operator_oR_"; // |=
inline constexpr const char* OP_BITXOR_ASGN = "__operator_eO_"; // ^=
inline constexpr const char* OP_SHL_ASGN  = "__operator_lS_";   // <<=
inline constexpr const char* OP_SHR_ASGN  = "__operator_rS_";   // >>=

// ── Increment / Decrement ────────────────────────────────────────────────────
inline constexpr const char* OP_PREFIX_INC  = "__operator_pp_";  // ++_ (prefix)
inline constexpr const char* OP_PREFIX_DEC  = "__operator_mm_";  // --_ (prefix)
inline constexpr const char* OP_POSTFIX_INC = "__operator_PP_";  // _++ (postfix)
inline constexpr const char* OP_POSTFIX_DEC = "__operator_MM_";  // _-- (postfix)

// ── Subscript ────────────────────────────────────────────────────────────────
inline constexpr const char* OP_SUBSCRIPT  = "__operator_ix_";   // [] (index/subscript)

// ── Call ─────────────────────────────────────────────────────────────────────
inline constexpr const char* OP_CALL       = "__operator_cl_";   // () (call operator)

// ── Cast ─────────────────────────────────────────────────────────────────────
// Cast operators use "__operator_cv_" as prefix, followed by the encoded target type.
// Example: "__operator_cv_int"
inline constexpr const char* OP_CAST_PREFIX = "__operator_cv_";  // (type) conversion


// ═════════════════════════════════════════════════════════════════════════════
// Helpers
// ═════════════════════════════════════════════════════════════════════════════

/**
 * Check whether a canonical function name is an operator name (__operator_XX_ pattern).
 */
inline bool is_operator_name(const std::string& name) {
    // Minimum length: __operator_XX_ = 14 chars
    return name.size() >= 14
        && name[0] == '_' && name[1] == '_'
        && name.compare(2, 9, "operator_") == 0;
}

/**
 * Check whether a canonical function name is a cast operator (__operator_cv_<type>).
 */
inline bool is_cast_operator(const std::string& name) {
    constexpr std::string_view prefix = "__operator_cv_";
    return name.size() > prefix.size()
        && name.compare(0, prefix.size(), prefix) == 0;
}

/**
 * Check whether a canonical function name is the call operator (__operator_cl_).
 */
inline bool is_call_operator(const std::string& name) {
    return name == OP_CALL;
}

/**
 * Check whether a canonical function name is the subscript operator (__operator_ix_).
 */
inline bool is_subscript_operator(const std::string& name) {
    return name == OP_SUBSCRIPT;
}

/**
 * Check whether a canonical function name is an assignment operator
 * (simple = or compound +=, -=, etc.).
 */
inline bool is_assignment_operator(const std::string& name) {
    static const std::set<std::string> assignment_ops = {
        OP_ASSIGN,
        OP_ADD_ASGN, OP_SUB_ASGN, OP_MUL_ASGN, OP_DIV_ASGN, OP_MOD_ASGN,
        OP_BITAND_ASGN, OP_BITOR_ASGN, OP_BITXOR_ASGN,
        OP_SHL_ASGN, OP_SHR_ASGN
    };
    return assignment_ops.count(name) > 0;
}

/**
 * Extract the Itanium 2-letter code from an __operator_XX_ name.
 * Returns the code portion (e.g. "pl" from "__operator_pl_").
 * For cast operators, returns "cv" (the type suffix must be handled separately).
 * Returns empty string if the name is not an operator name.
 */
inline std::string get_operator_mangling_code(const std::string& name) {
    if (!is_operator_name(name)) return "";
    constexpr size_t prefix_len = 11; // "__operator_"
    if (is_cast_operator(name)) {
        return "cv"; // cast: type mangling handled separately
    }
    // Extract code between "__operator_" and trailing "_"
    if (name.back() == '_' && name.size() > prefix_len + 1) {
        return name.substr(prefix_len, name.size() - prefix_len - 1);
    }
    return name.substr(prefix_len);
}

/**
 * Get the human-readable operator symbol from a canonical operator name.
 * Used in error/diagnostic messages.
 */
inline std::string get_operator_symbol(const std::string& name) {
    if (name == OP_ADD)       return "+";
    if (name == OP_SUB)       return "-";
    if (name == OP_MUL)       return "*";
    if (name == OP_DIV)       return "/";
    if (name == OP_MOD)       return "%";
    if (name == OP_BITAND)    return "&";
    if (name == OP_BITOR)     return "|";
    if (name == OP_BITXOR)    return "^";
    if (name == OP_BITNOT)    return "~";
    if (name == OP_SHL)       return "<<";
    if (name == OP_SHR)       return ">>";
    if (name == OP_AND)       return "&&";
    if (name == OP_OR)        return "||";
    if (name == OP_NOT)       return "!";
    if (name == OP_EQ)        return "==";
    if (name == OP_NE)        return "!=";
    if (name == OP_LT)        return "<";
    if (name == OP_GT)        return ">";
    if (name == OP_LE)        return "<=";
    if (name == OP_GE)        return ">=";
    if (name == OP_SPACESHIP) return "<=>";
    if (name == OP_ASSIGN)    return "=";
    if (name == OP_ADD_ASGN)  return "+=";
    if (name == OP_SUB_ASGN)  return "-=";
    if (name == OP_MUL_ASGN)  return "*=";
    if (name == OP_DIV_ASGN)  return "/=";
    if (name == OP_MOD_ASGN)  return "%=";
    if (name == OP_BITAND_ASGN) return "&=";
    if (name == OP_BITOR_ASGN)  return "|=";
    if (name == OP_BITXOR_ASGN) return "^=";
    if (name == OP_SHL_ASGN)  return "<<=";
    if (name == OP_SHR_ASGN)  return ">>=";
    if (name == OP_PREFIX_INC)  return "++_";
    if (name == OP_PREFIX_DEC)  return "--_";
    if (name == OP_POSTFIX_INC) return "_++";
    if (name == OP_POSTFIX_DEC) return "_--";
    if (name == OP_SUBSCRIPT)   return "[]";
    if (name == OP_CALL)        return "()";
    if (is_cast_operator(name)) return "(cast)";
    return name;
}

} // namespace k::op

#endif // KLANG_OPERATOR_NAMES_HPP

