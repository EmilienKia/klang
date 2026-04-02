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
#include "../common/operator_names.hpp"
#include "../parse/ast.hpp"

#include "llvm/Support/raw_os_ostream.h"
template<typename STM>
inline STM& operator << (STM& stm, const llvm::Type& type) {
    llvm::raw_os_ostream ross(stm);
    type.print(ross, true);
    return stm;
}

template<typename STM>
inline STM& operator << (STM& stm, const llvm::Value& value) {
    llvm::raw_os_ostream ross(stm);
    value.print(ross, true);
    return stm;
}

namespace k::model::gen {

// Forward declaration for class virtual dispatch helper (defined in gen_class.cpp)
llvm::Value* emit_virtual_dispatch_call(llvm::IRBuilder<>& builder, klass& st, llvm::Value* this_ptr,
    int slot_index, llvm::FunctionType* fn_type, const std::vector<llvm::Value*>& args,
    std::shared_ptr<context> ctx, const std::string& result_name);


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
            case primitive_type::CHAR: return pt->is_unsigned() ? "uchar" : "char";
            case primitive_type::BYTE: return pt->is_unsigned() ? "ubyte" : "byte";
            case primitive_type::SHORT: return pt->is_unsigned() ? "ushort" : "short";
            case primitive_type::INT: return pt->is_unsigned() ? "uint" : "int";
            case primitive_type::LONG: return pt->is_unsigned() ? "ulong" : "long";
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

/**
 * Resolve a binary operator overload for an aggregate type, using cast-weight scoring
 * on the right operand to select the best match among multiple candidates.
 */
std::pair<std::shared_ptr<function>, std::shared_ptr<expression>>
/**
 * Resolve a binary operator overload for an aggregate type using cast-weight scoring.
 *
 * Steps:
 *   1. Collect member operator candidates from the aggregate.
 *   2. Collect non-member operator candidates from enclosing scopes.
 *   3. Score each candidate by cast_weight on the right operand (and left for non-member).
 *   4. Prefer member operators over non-member when scores are equal.
 *   5. Filter by const-this if the left operand is const.
 *
 * @return {best_func, adapted_right} or {nullptr, nullptr} if no viable match.
 */
type_reference_resolver::resolve_binary_operator_overload(
    const binary_expression& expr,
    const std::shared_ptr<aggregate>& left_agg,
    const std::shared_ptr<expression>& left_expr,
    const std::shared_ptr<expression>& right_expr,
    bool is_const_this)
{
    std::string op_name = get_binary_operator_name(expr);
    if (op_name.empty()) return {nullptr, nullptr};

    // Step 1: Collect member operator candidates from the aggregate
    // Collect all candidate functions: member first (with inheritance), then non-member.
    // collect_member_operators_from_hierarchy implements C++-style name hiding: if the left
    // aggregate itself declares any operator with op_name, only those are returned;
    // otherwise the search recurses into base classes (BFS, diamond-safe).
    std::vector<std::shared_ptr<function>> member_funcs =
        collect_member_operators_from_hierarchy(left_agg, op_name);
    std::vector<std::shared_ptr<function>> non_member_funcs = scope_lookup::lookup_functions(left_expr, op_name);
    // Remove any member functions that leaked into non_member_funcs via scope_lookup.
    // (scope_lookup walks up the element parent chain, which includes the enclosing aggregate.)
    non_member_funcs.erase(
        std::remove_if(non_member_funcs.begin(), non_member_funcs.end(),
            [](const std::shared_ptr<function>& f) { return f->is_member(); }),
        non_member_funcs.end());

    // Filter member operators by constness: on a const object, only const member operators are callable.
    if (is_const_this && !member_funcs.empty()) {
        bool had_mutable = false;
        std::vector<std::shared_ptr<function>> const_members;
        for (auto& func : member_funcs) {
            if (func->is_const_member()) {
                const_members.push_back(func);
            } else {
                had_mutable = true;
            }
        }
        if (const_members.empty() && had_mutable && non_member_funcs.empty()) {
            std::string op_sym = get_operator_symbol(op_name);
            std::string type_str = left_agg->get_struct_type() ? left_agg->get_struct_type()->to_string() : left_agg->get_short_name();
            throw_error(0x0087, expr.first_lexeme(),
                "Cannot call mutable member operator '{}' on a const object of type '{}': "
                "only const member operators can be called on const objects; "
                "declare the operator as 'const' to allow calls on const objects",
                {op_sym, type_str});
        }
        member_funcs = std::move(const_members);
    }

    if (member_funcs.empty() && non_member_funcs.empty()) return {nullptr, nullptr};

    struct CandInfo {
        std::shared_ptr<function> func;
        cast_weight score;
        bool is_member;
        std::shared_ptr<expression> adapted_right;
    };

    std::vector<CandInfo> valid;

    // Score member operator functions: parameter list has 1 param (the right operand)
    // The left operand ('this') is always an exact match since we looked up on the correct aggregate.
    for (auto& func : member_funcs) {
        const auto& params = func->parameters();
        if (params.size() != 1) continue; // Binary member operator should have exactly 1 explicit param
        auto right_param_type = params[0]->get_type();
        auto w = compute_cast_weight(right_expr, right_param_type);
        if (w != CAST_IMPOSSIBLE) {
            auto adapted = adapt_type(right_expr, right_param_type);
            valid.push_back({func, w, true, adapted ? adapted : right_expr});
        }
    }

    // Step 2: Collect non-member operator candidates from enclosing scopes
    // Score non-member operator functions: parameter list has 2 params (left, right)
    // Must validate BOTH left (params[0]) and right (params[1]) parameter compatibility.
    for (auto& func : non_member_funcs) {
        const auto& params = func->parameters();
        if (params.size() != 2) continue; // Binary non-member operator should have exactly 2 params

        // Validate left parameter: must be compatible with the left expression
        auto left_param_type = params[0]->get_type();
        auto wl = compute_cast_weight(left_expr, left_param_type);
        if (wl == CAST_IMPOSSIBLE) continue;

        // Score right parameter
        auto right_param_type = params[1]->get_type();
        auto wr = compute_cast_weight(right_expr, right_param_type);
        if (wr == CAST_IMPOSSIBLE) continue;

        // Step 3: Score each candidate by cast_weight on the right operand (and left for non-member)
        // Overall score = worst of left and right
        cast_weight w = std::max(wl, wr);
        auto adapted = adapt_type(right_expr, right_param_type);
        valid.push_back({func, w, false, adapted ? adapted : right_expr});
    }

    // Step 4: Prefer member operators over non-member when scores are equal
    if (valid.empty()) return {nullptr, nullptr};

    // Step 5: Filter by const-this if the left operand is const
    // Best = lowest score; among equal scores, prefer member over non-member
    cast_weight best_score = CAST_IMPOSSIBLE;
    bool best_is_member = false;

    for (auto& c : valid) {
        if (c.score < best_score
            || (c.score == best_score && c.is_member && !best_is_member)) {
            best_score = c.score;
            best_is_member = c.is_member;
        }
    }

    std::vector<CandInfo*> best;
    for (auto& c : valid) {
        if (c.score == best_score && c.is_member == best_is_member)
            best.push_back(&c);
    }

    if (best.size() > 1) {
        std::string op_sym = get_operator_symbol(op_name);
        std::string left_type_str = left_expr->get_type() ? left_expr->get_type()->to_string() : "?";
        auto d = k::log::diagnostic::make_error(0x3000B,
            "Ambiguous operator '{}' for type '{}': {} equally viable overloads",
            {op_sym, left_type_str, std::to_string(best.size())});
        for (auto* c : best) {
            std::string sig;
            bool first = true;
            for (auto& p : c->func->parameters()) {
                if (!first) sig += ", ";
                sig += p->get_type() ? p->get_type()->to_string() : "?";
                first = false;
            }
            d.add_note("  candidate: {} {}({})", {c->is_member ? "[member]" : "[non-member]",
                        c->func->get_fq_name(), sig});
        }
        throw resolution_error(std::move(d));
    }

    return {best[0]->func, best[0]->adapted_right};
}

/**
 * Resolve a unary operator overload for an aggregate type, using cast-weight scoring
 * to select the best match among multiple candidates.
 */
std::shared_ptr<function>
/**
 * Resolve a unary operator overload for an aggregate type using cast-weight scoring.
 *
 * Steps:
 *   1. Collect member operator candidates from the aggregate.
 *   2. Collect non-member operator candidates from enclosing scopes.
 *   3. Score each candidate by cast_weight on the operand.
 *   4. Prefer member operators over non-member when scores are equal.
 *
 * @return The best matching function, or nullptr if no viable match.
 */
type_reference_resolver::resolve_unary_operator_overload(
    const unary_expression& expr,
    const std::shared_ptr<aggregate>& operand_agg,
    const std::shared_ptr<expression>& operand_expr,
    bool is_const_this)
{
    std::string op_name = get_unary_operator_name(expr);
    if (op_name.empty()) return nullptr;

    // Step 1: Collect member operator candidates from the aggregate
    // Collect all candidate functions: member first (with inheritance), then non-member.
    std::vector<std::shared_ptr<function>> member_funcs =
        collect_member_operators_from_hierarchy(operand_agg, op_name);
    std::vector<std::shared_ptr<function>> non_member_funcs = scope_lookup::lookup_functions(operand_expr, op_name);
    // Remove any member functions that leaked into non_member_funcs via scope_lookup.
    non_member_funcs.erase(
        std::remove_if(non_member_funcs.begin(), non_member_funcs.end(),
            [](const std::shared_ptr<function>& f) { return f->is_member(); }),
        non_member_funcs.end());

    // Filter member operators by constness: on a const object, only const member operators are callable.
    if (is_const_this && !member_funcs.empty()) {
        bool had_mutable = false;
        std::vector<std::shared_ptr<function>> const_members;
        for (auto& func : member_funcs) {
            if (func->is_const_member()) {
                const_members.push_back(func);
            } else {
                had_mutable = true;
            }
        }
        if (const_members.empty() && had_mutable && non_member_funcs.empty()) {
            std::string op_sym = get_operator_symbol(op_name);
            std::string type_str = operand_agg->get_struct_type() ? operand_agg->get_struct_type()->to_string() : operand_agg->get_short_name();
            throw_error(0x0088, expr.first_lexeme(),
                "Cannot call mutable member operator '{}' on a const object of type '{}': "
                "only const member operators can be called on const objects; "
                "declare the operator as 'const' to allow calls on const objects",
                {op_sym, type_str});
        }
        member_funcs = std::move(const_members);
    }

    if (member_funcs.empty() && non_member_funcs.empty()) return nullptr;

    struct CandInfo {
        std::shared_ptr<function> func;
        cast_weight score;
        bool is_member;
    };

    std::vector<CandInfo> valid;

    // Score member operator functions: no explicit parameters for unary member operators
    // The operand ('this') is always an exact match since we looked up on the correct aggregate.
    for (auto& func : member_funcs) {
        const auto& params = func->parameters();
        if (!params.empty()) continue; // Unary member operator should have no explicit param
        valid.push_back({func, CAST_NONE, true});
    }

    // Step 2: Collect non-member operator candidates from enclosing scopes
    // Score non-member operator functions: parameter list has 1 param (the operand)
    for (auto& func : non_member_funcs) {
        const auto& params = func->parameters();
        if (params.size() != 1) continue; // Unary non-member operator should have exactly 1 param

        // Step 3: Score each candidate by cast_weight on the operand
        auto operand_param_type = params[0]->get_type();
        auto w = compute_cast_weight(operand_expr, operand_param_type);
        if (w != CAST_IMPOSSIBLE) {
            valid.push_back({func, w, false});
        }
    }

    // Step 4: Prefer member operators over non-member when scores are equal
    if (valid.empty()) return nullptr;

    // Best = lowest score; among equal scores, prefer member over non-member
    cast_weight best_score = CAST_IMPOSSIBLE;
    bool best_is_member = false;

    for (auto& c : valid) {
        if (c.score < best_score
            || (c.score == best_score && c.is_member && !best_is_member)) {
            best_score = c.score;
            best_is_member = c.is_member;
        }
    }

    std::vector<CandInfo*> best;
    for (auto& c : valid) {
        if (c.score == best_score && c.is_member == best_is_member)
            best.push_back(&c);
    }

    if (best.size() > 1) {
        std::string op_sym = get_operator_symbol(op_name);
        std::string operand_type_str = operand_expr->get_type() ? operand_expr->get_type()->to_string() : "?";
        auto d = k::log::diagnostic::make_error(0x3000C,
            "Ambiguous unary operator '{}' for type '{}': {} equally viable overloads",
            {op_sym, operand_type_str, std::to_string(best.size())});
        for (auto* c : best) {
            std::string sig;
            bool first = true;
            for (auto& p : c->func->parameters()) {
                if (!first) sig += ", ";
                sig += p->get_type() ? p->get_type()->to_string() : "?";
                first = false;
            }
            d.add_note("  candidate: {} {}({})", {c->is_member ? "[member]" : "[non-member]",
                        c->func->get_fq_name(), sig});
        }
        throw resolution_error(std::move(d));
    }

    return best[0]->func;
}

/**
 * Resolve a casting operator overload for an aggregate type.
 * Looks for a member function named "__operator_cv_<encoded_type>" matching the
 * target type of the cast, searching the aggregate's hierarchy.
 */
std::shared_ptr<function>
type_reference_resolver::resolve_cast_operator_overload(
    const std::shared_ptr<aggregate>& source_agg,
    const std::shared_ptr<type>& target_type,
    bool is_const_this)
{
    if (!source_agg || !target_type) return nullptr;

    // Build the canonical operator name from the target type
    std::string encoded = encode_type_for_cast_operator(target_type);
    std::string op_name = "__operator_cv_" + encoded;

    // Collect member operator functions from the hierarchy
    std::vector<std::shared_ptr<function>> member_funcs =
        collect_member_operators_from_hierarchy(source_agg, op_name);

    if (member_funcs.empty()) return nullptr;

    // Filter by constness: on a const object, only const member operators are callable.
    if (is_const_this) {
        std::vector<std::shared_ptr<function>> const_members;
        for (auto& func : member_funcs) {
            if (func->is_const_member()) {
                const_members.push_back(func);
            }
        }
        if (const_members.empty()) return nullptr;
        member_funcs = std::move(const_members);
    }

    // Casting operators have no parameters (other than 'this'), so there's no
    // scoring to do — just return the first (and should be only) match.
    for (auto& func : member_funcs) {
        // Verify it has no explicit parameters
        if (func->parameters().empty()) {
            return func;
        }
    }

    return nullptr;
}

namespace {
} // anonymous namespace

/**
 * Compute virtual dispatch info for an operator overload call on a member function.
 * Similar to annotate_dispatch_info for function_invocation_expression.
 */
virtual_dispatch_info compute_operator_dispatch_info(
    const std::shared_ptr<function>& func,
    const std::shared_ptr<type>& receiver_type)
{
    virtual_dispatch_info di;
    di.kind = virtual_dispatch_info::dispatch_kind::DIRECT;

    if (!func || !func->is_virtual() || func->get_vtable_slot() < 0) {
        return di;
    }

    if (!type::is_reference(receiver_type)) {
        return di;
    }

    auto bare_subtype = type::remove_const(receiver_type->get_subtype());
    auto st_type = std::dynamic_pointer_cast<struct_type>(bare_subtype);
    if (!st_type) {
        return di;
    }

    auto kl = std::dynamic_pointer_cast<klass>(st_type->get_struct());
    if (!kl || !kl->has_vtable()) {
        // Check imported aggregates
        auto imp = std::dynamic_pointer_cast<aggregate>(st_type->get_struct());
        if (imp && imp->has_vtable()) {
            di.kind = virtual_dispatch_info::dispatch_kind::VTABLE;
            di.slot_index = func->get_vtable_slot();
            di.imported_dispatch_agg = imp;
            di.this_adjustment = 0;
            return di;
        }
        return di;
    }

    di.kind = virtual_dispatch_info::dispatch_kind::VTABLE;
    di.slot_index = func->get_vtable_slot();
    di.dispatch_class = kl;
    di.this_adjustment = 0;
    return di;
}


//
// Operator overload code generation helpers
//

/**
 * Generate LLVM IR for a binary operator overload function call.
 *
 * Steps:
 *   1. Evaluate left and right operand expressions.
 *   2. Resolve the operator function (member or non-member).
 *   3. For member operators: load 'this' from left operand, call with right as arg.
 *   4. For non-member operators: call with both operands as args.
 *   5. Handle virtual dispatch if the operator function is virtual.
 *   6. Handle sret return for aggregate return types.
 *
 * @return true if an overload was handled, false if not an overload.
 */
bool implementation_generator::generate_binary_operator_overload(binary_expression& expr) {
    if (!expr.has_operator_overload()) return false;

    auto op_func = expr.get_operator_func();

    // Find the LLVM function (may be null for abstract or external virtual operators)
    auto it = _context->_functions.find(op_func);
    if (it == _context->_functions.end()) {
        if (op_func->is_virtual() &&
            (op_func->is_abstract_func() || op_func->is_external())) {
            // Abstract/external virtual operator: no LLVM definition, dispatch via vtable below.
        } else {
            throw_internal_error(0x0060, expr.first_lexeme(),
                "Internal error: operator overload function '{}' has no LLVM definition",
                {op_func->get_short_name()});
        }
    }
    llvm::Function* llvm_func = (it != _context->_functions.end()) ? it->second : nullptr;

    // Build the LLVM FunctionType (from llvm_func if available, or from the model).
    // When llvm_func is non-null its FunctionType already includes the sret parameter
    // (if any).  When reconstructing from the model we must mirror what
    // declaration_generator::visit_function does: prepend a ptr param and use void
    // return when the return type needs sret ABI.
    auto build_fn_type = [&]() -> llvm::FunctionType* {
        if (llvm_func) return llvm_func->getFunctionType();
        // Reconstruct from model
        std::vector<llvm::Type*> param_types;
        // sret parameter comes first (before this)
        bool model_sret = op_func->has_return_type() && needs_sret_return(op_func->get_return_type());
        if (model_sret)
            param_types.push_back(llvm::PointerType::get(**_context, 0));
        if (op_func->is_member() && !op_func->is_static() && op_func->get_this_parameter())
            param_types.push_back(_context->get_llvm_type(op_func->get_this_parameter()->get_type()));
        for (const auto& param : op_func->parameters())
            param_types.push_back(_context->get_llvm_type(param->get_type()));
        llvm::Type* ret_type = llvm::Type::getVoidTy(**_context);
        if (op_func->has_return_type() && !model_sret)
            ret_type = _context->get_llvm_type(op_func->get_return_type());
        return llvm::FunctionType::get(ret_type, param_types, false);
    };

    // Helper: detect whether an operator call uses sret ABI.
    auto op_needs_sret = [&]() -> bool {
        return op_func->has_return_type() && needs_sret_return(op_func->get_return_type());
    };

    // Helper: allocate an sret temporary, insert it at the front of `args`,
    // and track it for destructor cleanup.  Returns the sret alloca pointer.
    auto prepare_sret_for_op = [&](std::vector<llvm::Value*>& args, bool use_sret_destination) -> llvm::AllocaInst* {
        auto ret_type_nc = type::remove_const(op_func->get_return_type());
        llvm::Type* llvm_ret = _context->get_llvm_type(ret_type_nc);
        llvm::AllocaInst* sret_dest = nullptr;
        bool consumed_sret_dest = false;

        if (use_sret_destination && _sret_destination) {
            sret_dest = llvm::dyn_cast<llvm::AllocaInst>(_sret_destination);
            if (!sret_dest) {
                // _sret_destination is not an alloca — create a temp instead
                llvm::Function* cur_fn = _builder->GetInsertBlock()->getParent();
                llvm::IRBuilder<> entry_builder(&cur_fn->getEntryBlock(), cur_fn->getEntryBlock().begin());
                sret_dest = entry_builder.CreateAlloca(llvm_ret, nullptr, "op_sret_tmp");
            } else {
                _sret_destination = nullptr;
                consumed_sret_dest = true;
            }
        } else {
            llvm::Function* cur_fn = _builder->GetInsertBlock()->getParent();
            llvm::IRBuilder<> entry_builder(&cur_fn->getEntryBlock(), cur_fn->getEntryBlock().begin());
            sret_dest = entry_builder.CreateAlloca(llvm_ret, nullptr, "op_sret_tmp");
        }

        args.insert(args.begin(), sret_dest);

        // Track for temporary cleanup only when not consumed from _sret_destination
        if (!consumed_sret_dest) {
            auto ret_st = std::dynamic_pointer_cast<struct_type>(ret_type_nc);
            if (ret_st && ret_st->get_struct()) {
                auto dtor = ret_st->get_struct()->get_destructor();
                if (dtor) {
                    auto dtor_fn = dtor->shared_as<k::model::function>();
                    auto dtor_it = _context->_functions.find(dtor_fn);
                    if (dtor_it != _context->_functions.end())
                        _expression_temporaries.push_back(std::make_pair(sret_dest, dtor_it->second));
                }
            }
        }
        return sret_dest;
    };

    // Build arguments
    std::vector<llvm::Value*> args;

    if (op_func->is_member()) {
        // Member operator: 'this' is the left operand (a reference/pointer to the struct)
        expr.left()->accept(*this);
        if (!_value) {
            throw_internal_error(0x0061, expr.first_lexeme(),
                "Internal error: left operand for operator overload produced no LLVM value");
        }
        args.push_back(_value);

        // Step 1: Evaluate left and right operand expressions
        // Right operand is the argument
        expr.right()->accept(*this);
        if (!_value) {
            throw_internal_error(0x0062, expr.first_lexeme(),
                "Internal error: right operand for operator overload produced no LLVM value");
        }
        args.push_back(_value);
    } else {
        // Non-member operator: both operands are arguments
        expr.left()->accept(*this);
        if (!_value) {
            throw_internal_error(0x0063, expr.first_lexeme(),
                "Internal error: left operand for non-member operator overload produced no LLVM value");
        }
        args.push_back(_value);

        // Step 2: Resolve the operator function (member or non-member)
        expr.right()->accept(*this);
        if (!_value) {
            throw_internal_error(0x0064, expr.first_lexeme(),
                "Internal error: right operand for non-member operator overload produced no LLVM value");
        }
        args.push_back(_value);
    }

    // Check for virtual dispatch
    if (expr.has_operator_dispatch_info()) {
        auto& di = expr.get_operator_dispatch_info();
        if (di.kind == virtual_dispatch_info::dispatch_kind::VTABLE) {
            llvm::FunctionType* fn_type = build_fn_type();
            bool is_sret = fn_type->getReturnType()->isVoidTy() && op_needs_sret();
            if (di.dispatch_class) {
                // Local class: use the standard virtual dispatch helper
                if (is_sret) {
                    auto* sret_tmp = prepare_sret_for_op(args, false);
                    emit_virtual_dispatch_call(*_builder, *di.dispatch_class, args[1],
                        di.slot_index, fn_type, args, _context, "op_vcall");
                    _value = sret_tmp;
                    return true;
                }
                auto result = emit_virtual_dispatch_call(*_builder, *di.dispatch_class, args[0],
                    di.slot_index, fn_type, args, _context, "op_vcall");
                if (result) {
                    _value = result;
                    return true;
                }
                // Fallback: emit_virtual_dispatch_call returned nullptr (vtable not ready?)
            }
            if (di.imported_dispatch_agg) {
                // Imported class: use byte-offset GEP (same as function invocation for imports)
                auto imp_agg = di.imported_dispatch_agg;
                auto* struct_llvm_type = imp_agg->get_struct_type()
                                         ? imp_agg->get_struct_type()->get_llvm_type() : nullptr;
                if (struct_llvm_type) {
                    llvm::LLVMContext& llvm_ctx = **_context;
                    llvm::Type* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);
                    // When sret is used, this_ptr is at args[1] (after sret); GEP on args[0] is wrong
                    if (is_sret) {
                        auto* sret_tmp = prepare_sret_for_op(args, false);
                        // this_ptr is now at args[1]
                        llvm::Value* vptr_addr = _builder->CreateStructGEP(
                            struct_llvm_type, args[1], 0, "op_imp_vptr_addr");
                        llvm::Value* vptr = _builder->CreateLoad(ptr_ty, vptr_addr, "op_imp_vptr");
                        const uint64_t ptr_size = 8;
                        llvm::Value* slot_offset = llvm::ConstantInt::get(
                            llvm::Type::getInt64Ty(llvm_ctx),
                            (di.slot_index + 1) * ptr_size);
                        llvm::Value* fn_ptr_addr = _builder->CreateInBoundsGEP(
                            llvm::Type::getInt8Ty(llvm_ctx), vptr, slot_offset, "op_imp_vtbl_slot");
                        llvm::Value* fn_ptr = _builder->CreateLoad(ptr_ty, fn_ptr_addr, "op_imp_fn_ptr");
                        _builder->CreateCall(fn_type, fn_ptr, args);
                        _value = sret_tmp;
                        return true;
                    }
                    llvm::Value* vptr_addr = _builder->CreateStructGEP(
                        struct_llvm_type, args[0], 0, "op_imp_vptr_addr");
                    llvm::Value* vptr = _builder->CreateLoad(ptr_ty, vptr_addr, "op_imp_vptr");
                    const uint64_t ptr_size = 8;
                    llvm::Value* slot_offset = llvm::ConstantInt::get(
                        llvm::Type::getInt64Ty(llvm_ctx),
                        (di.slot_index + 1) * ptr_size);
                    llvm::Value* fn_ptr_addr = _builder->CreateInBoundsGEP(
                        llvm::Type::getInt8Ty(llvm_ctx), vptr, slot_offset, "op_imp_vtbl_slot");
                    llvm::Value* fn_ptr = _builder->CreateLoad(ptr_ty, fn_ptr_addr, "op_imp_fn_ptr");
                    _value = _builder->CreateCall(fn_type, fn_ptr, args,
                        fn_type->getReturnType()->isVoidTy() ? "" : "op_imp_vcall");
                    return true;
                }
            }
        }
    }

    if (!llvm_func) {
        throw_internal_error(0x0060, expr.first_lexeme(),
            "Internal error: operator overload function '{}' has no LLVM definition and is not dispatched virtually",
            {op_func->get_short_name()});
    }

    // Step 3: For member operators: load 'this' from left operand, call with right as arg
    // Direct call
    bool op_uses_sret = llvm_func->getReturnType()->isVoidTy() && op_needs_sret();
    if (op_uses_sret) {
        auto* sret_dest = prepare_sret_for_op(args, true);
        _builder->CreateCall(llvm_func, args);
        _value = sret_dest;
    } else {
        _value = _builder->CreateCall(llvm_func, args,
            llvm_func->getReturnType()->isVoidTy() ? "" : "op_call");
    }
    return true;
}

/**
 * Generate LLVM IR for a unary operator overload function call.
 *
 * Steps:
 *   1. Evaluate the operand expression.
 *   2. Resolve the operator function (member or non-member).
 *   3. For member operators: load 'this' from operand, call with no additional args.
 *   4. For non-member operators: call with operand as arg.
 *   5. Handle virtual dispatch and sret return.
 *
 * @return true if an overload was handled, false if not an overload.
 */
bool implementation_generator::generate_unary_operator_overload(unary_expression& expr) {
    // Step 1: Evaluate the operand expression
    if (!expr.has_operator_overload()) return false;

    // Step 2: Resolve the operator function (member or non-member)
    auto op_func = expr.get_operator_func();

    // Find the LLVM function (may be null for abstract or external virtual operators)
    auto it = _context->_functions.find(op_func);
    if (it == _context->_functions.end()) {
        if (op_func->is_virtual() &&
            (op_func->is_abstract_func() || op_func->is_external())) {
            // Abstract/external virtual operator: no LLVM definition, dispatch via vtable below.
        } else {
            throw_internal_error(0x0065, expr.first_lexeme(),
                "Internal error: operator overload function '{}' has no LLVM definition",
                {op_func->get_short_name()});
        }
    }
    llvm::Function* llvm_func = (it != _context->_functions.end()) ? it->second : nullptr;

    // Build the LLVM FunctionType (from llvm_func if available, or from the model).
    // Must match declaration_generator::visit_function: sret param first, void return.
    auto build_fn_type = [&]() -> llvm::FunctionType* {
        if (llvm_func) return llvm_func->getFunctionType();
        // Reconstruct from model
        std::vector<llvm::Type*> param_types;
        bool model_sret = op_func->has_return_type() && needs_sret_return(op_func->get_return_type());
        if (model_sret)
            param_types.push_back(llvm::PointerType::get(**_context, 0));
        if (op_func->is_member() && !op_func->is_static() && op_func->get_this_parameter())
            param_types.push_back(_context->get_llvm_type(op_func->get_this_parameter()->get_type()));
        for (const auto& param : op_func->parameters())
            param_types.push_back(_context->get_llvm_type(param->get_type()));
        llvm::Type* ret_type = llvm::Type::getVoidTy(**_context);
        if (op_func->has_return_type() && !model_sret)
            ret_type = _context->get_llvm_type(op_func->get_return_type());
        return llvm::FunctionType::get(ret_type, param_types, false);
    };

    auto op_needs_sret = [&]() -> bool {
        return op_func->has_return_type() && needs_sret_return(op_func->get_return_type());
    };

    // Helper: allocate sret temp, insert at front of args, track cleanup. Returns alloca.
    auto prepare_sret_for_uop = [&](std::vector<llvm::Value*>& args) -> llvm::AllocaInst* {
        auto ret_type_nc = type::remove_const(op_func->get_return_type());
        llvm::Type* llvm_ret = _context->get_llvm_type(ret_type_nc);
        llvm::Function* cur_fn = _builder->GetInsertBlock()->getParent();
        llvm::IRBuilder<> entry_builder(&cur_fn->getEntryBlock(), cur_fn->getEntryBlock().begin());
        auto* sret_tmp = entry_builder.CreateAlloca(llvm_ret, nullptr, "uop_sret_tmp");
        args.insert(args.begin(), sret_tmp);
        // Track for cleanup
        auto ret_st = std::dynamic_pointer_cast<struct_type>(ret_type_nc);
        if (ret_st && ret_st->get_struct()) {
            auto dtor = ret_st->get_struct()->get_destructor();
            if (dtor) {
                auto dtor_fn = dtor->shared_as<k::model::function>();
                auto dtor_it = _context->_functions.find(dtor_fn);
                if (dtor_it != _context->_functions.end())
                    _expression_temporaries.push_back(std::make_pair(sret_tmp, dtor_it->second));
            }
        }
        return sret_tmp;
    };

    // Step 3: For member operators: load 'this' from operand, call with no additional args
    // Build arguments
    std::vector<llvm::Value*> args;

    // Step 4: For non-member operators: call with operand as arg
    if (op_func->is_member()) {
        // Member operator: 'this' is the operand (a reference/pointer to the struct)
        expr.sub_expr()->accept(*this);
        if (!_value) {
            throw_internal_error(0x0066, expr.first_lexeme(),
                "Internal error: operand for unary operator overload produced no LLVM value");
        }
        args.push_back(_value);
    } else {
        // Non-member operator: operand is the argument
        expr.sub_expr()->accept(*this);
        if (!_value) {
            throw_internal_error(0x0067, expr.first_lexeme(),
                "Internal error: operand for non-member unary operator overload produced no LLVM value");
        }
        args.push_back(_value);
    }

    // Step 5: Handle virtual dispatch and sret return
    // Check for virtual dispatch
    if (expr.has_operator_dispatch_info()) {
        auto& di = expr.get_operator_dispatch_info();
        if (di.kind == virtual_dispatch_info::dispatch_kind::VTABLE) {
            llvm::FunctionType* fn_type = build_fn_type();
            bool is_sret = fn_type->getReturnType()->isVoidTy() && op_needs_sret();
            if (di.dispatch_class) {
                if (is_sret) {
                    auto* sret_tmp = prepare_sret_for_uop(args);
                    emit_virtual_dispatch_call(*_builder, *di.dispatch_class, args[1],
                        di.slot_index, fn_type, args, _context, "uop_vcall");
                    _value = sret_tmp;
                    return true;
                }
                auto result = emit_virtual_dispatch_call(*_builder, *di.dispatch_class, args[0],
                    di.slot_index, fn_type, args, _context, "uop_vcall");
                if (result) {
                    _value = result;
                    return true;
                }
            }
            if (di.imported_dispatch_agg) {
                auto imp_agg = di.imported_dispatch_agg;
                auto* struct_llvm_type = imp_agg->get_struct_type()
                                         ? imp_agg->get_struct_type()->get_llvm_type() : nullptr;
                if (struct_llvm_type) {
                    llvm::LLVMContext& llvm_ctx = **_context;
                    llvm::Type* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);
                    if (is_sret) {
                        auto* sret_tmp = prepare_sret_for_uop(args);
                        llvm::Value* vptr_addr = _builder->CreateStructGEP(
                            struct_llvm_type, args[1], 0, "uop_imp_vptr_addr");
                        llvm::Value* vptr = _builder->CreateLoad(ptr_ty, vptr_addr, "uop_imp_vptr");
                        const uint64_t ptr_size = 8;
                        llvm::Value* slot_offset = llvm::ConstantInt::get(
                            llvm::Type::getInt64Ty(llvm_ctx),
                            (di.slot_index + 1) * ptr_size);
                        llvm::Value* fn_ptr_addr = _builder->CreateInBoundsGEP(
                            llvm::Type::getInt8Ty(llvm_ctx), vptr, slot_offset, "uop_imp_vtbl_slot");
                        llvm::Value* fn_ptr = _builder->CreateLoad(ptr_ty, fn_ptr_addr, "uop_imp_fn_ptr");
                        _builder->CreateCall(fn_type, fn_ptr, args);
                        _value = sret_tmp;
                        return true;
                    }
                    llvm::Value* vptr_addr = _builder->CreateStructGEP(
                        struct_llvm_type, args[0], 0, "uop_imp_vptr_addr");
                    llvm::Value* vptr = _builder->CreateLoad(ptr_ty, vptr_addr, "uop_imp_vptr");
                    const uint64_t ptr_size = 8;
                    llvm::Value* slot_offset = llvm::ConstantInt::get(
                        llvm::Type::getInt64Ty(llvm_ctx),
                        (di.slot_index + 1) * ptr_size);
                    llvm::Value* fn_ptr_addr = _builder->CreateInBoundsGEP(
                        llvm::Type::getInt8Ty(llvm_ctx), vptr, slot_offset, "uop_imp_vtbl_slot");
                    llvm::Value* fn_ptr = _builder->CreateLoad(ptr_ty, fn_ptr_addr, "uop_imp_fn_ptr");
                    _value = _builder->CreateCall(fn_type, fn_ptr, args,
                        fn_type->getReturnType()->isVoidTy() ? "" : "uop_imp_vcall");
                    return true;
                }
            }
        }
    }

    if (!llvm_func) {
        throw_internal_error(0x0065, expr.first_lexeme(),
            "Internal error: operator overload function '{}' has no LLVM definition and is not dispatched virtually",
            {op_func->get_short_name()});
    }

    // Direct call
    bool op_uses_sret = llvm_func->getReturnType()->isVoidTy() && op_needs_sret();
    if (op_uses_sret) {
        auto* sret_tmp = prepare_sret_for_uop(args);
        _builder->CreateCall(llvm_func, args);
        _value = sret_tmp;
    } else {
        _value = _builder->CreateCall(llvm_func, args,
            llvm_func->getReturnType()->isVoidTy() ? "" : "uop_call");
    }
    return true;
}

/**
 * Generate LLVM IR for a casting operator overload function call.
 *
 * Steps:
 *   1. Evaluate the source expression.
 *   2. Resolve the operator_cast function on the source aggregate type.
 *   3. Call the casting operator with 'this' pointer.
 *   4. Handle virtual dispatch and sret return.
 *
 * @return true if an overload was handled, false if not an overload.
 */
bool implementation_generator::generate_cast_operator_overload(cast_expression& expr) {
    if (!expr.has_operator_overload()) return false;

    auto op_func = expr.get_operator_func();

    // Find the LLVM function (may be null for abstract or external virtual operators)
    auto it = _context->_functions.find(op_func);
    if (it == _context->_functions.end()) {
        if (op_func->is_virtual() &&
            (op_func->is_abstract_func() || op_func->is_external())) {
            // Abstract/external virtual operator: no LLVM definition, dispatch via vtable below.
        } else {
            throw_internal_error(0x0068, expr.first_lexeme(),
                "Internal error: casting operator function '{}' has no LLVM definition",
                {op_func->get_short_name()});
        }
    }
    llvm::Function* llvm_func = (it != _context->_functions.end()) ? it->second : nullptr;

    // Build the LLVM FunctionType (from llvm_func if available, or from the model).
    // Must match declaration_generator::visit_function: sret param first, void return.
    auto build_fn_type = [&]() -> llvm::FunctionType* {
        if (llvm_func) return llvm_func->getFunctionType();
        // Reconstruct from model
        std::vector<llvm::Type*> param_types;
        bool model_sret = op_func->has_return_type() && needs_sret_return(op_func->get_return_type());
        if (model_sret)
            param_types.push_back(llvm::PointerType::get(**_context, 0));
        if (op_func->is_member() && !op_func->is_static() && op_func->get_this_parameter())
            param_types.push_back(_context->get_llvm_type(op_func->get_this_parameter()->get_type()));
        for (const auto& param : op_func->parameters())
            param_types.push_back(_context->get_llvm_type(param->get_type()));
        llvm::Type* ret_type = llvm::Type::getVoidTy(**_context);
        if (op_func->has_return_type() && !model_sret)
            ret_type = _context->get_llvm_type(op_func->get_return_type());
        return llvm::FunctionType::get(ret_type, param_types, false);
    };

    auto op_needs_sret = [&]() -> bool {
        return op_func->has_return_type() && needs_sret_return(op_func->get_return_type());
    };

    // Helper: allocate sret temp, insert at front of args, track cleanup. Returns alloca.
    auto prepare_sret_for_cast = [&](std::vector<llvm::Value*>& args, bool use_sret_destination) -> llvm::AllocaInst* {
        auto ret_type_nc = type::remove_const(op_func->get_return_type());
        llvm::Type* llvm_ret = _context->get_llvm_type(ret_type_nc);
        llvm::AllocaInst* sret_dest = nullptr;
        bool consumed_sret_dest = false;

        if (use_sret_destination && _sret_destination) {
            sret_dest = llvm::dyn_cast<llvm::AllocaInst>(_sret_destination);
            if (!sret_dest) {
                llvm::Function* cur_fn = _builder->GetInsertBlock()->getParent();
                llvm::IRBuilder<> entry_builder(&cur_fn->getEntryBlock(), cur_fn->getEntryBlock().begin());
                sret_dest = entry_builder.CreateAlloca(llvm_ret, nullptr, "cast_sret_tmp");
            } else {
                _sret_destination = nullptr;
                consumed_sret_dest = true;
            }
        } else {
            llvm::Function* cur_fn = _builder->GetInsertBlock()->getParent();
            llvm::IRBuilder<> entry_builder(&cur_fn->getEntryBlock(), cur_fn->getEntryBlock().begin());
            sret_dest = entry_builder.CreateAlloca(llvm_ret, nullptr, "cast_sret_tmp");
        }

        args.insert(args.begin(), sret_dest);

        if (!consumed_sret_dest) {
            auto ret_st = std::dynamic_pointer_cast<struct_type>(ret_type_nc);
            if (ret_st && ret_st->get_struct()) {
                auto dtor = ret_st->get_struct()->get_destructor();
                if (dtor) {
                    auto dtor_fn = dtor->shared_as<k::model::function>();
                    auto dtor_it = _context->_functions.find(dtor_fn);
                    if (dtor_it != _context->_functions.end())
                        _expression_temporaries.push_back(std::make_pair(sret_dest, dtor_it->second));
                }
            }
        }
        return sret_dest;
    };

    // Step 1: Evaluate the source expression
    // Build arguments: only 'this' (the source object being cast)
    std::vector<llvm::Value*> args;

    // Step 2: Resolve the operator_cast function on the source aggregate type
    // Member casting operator: 'this' is the source operand (a reference/pointer to the struct)
    expr.sub_expr()->accept(*this);
    if (!_value) {
        throw_internal_error(0x0069, expr.first_lexeme(),
            "Internal error: source operand for casting operator overload produced no LLVM value");
    }
    args.push_back(_value);

    // Step 3: Call the casting operator with 'this' pointer
    // Check for virtual dispatch
    if (expr.has_operator_dispatch_info()) {
        auto& di = expr.get_operator_dispatch_info();
        if (di.kind == virtual_dispatch_info::dispatch_kind::VTABLE) {
            llvm::FunctionType* fn_type = build_fn_type();
            bool is_sret = fn_type->getReturnType()->isVoidTy() && op_needs_sret();
            if (di.dispatch_class) {
                if (is_sret) {
                    auto* sret_tmp = prepare_sret_for_cast(args, false);
                    emit_virtual_dispatch_call(*_builder, *di.dispatch_class, args[1],
                        di.slot_index, fn_type, args, _context, "cast_vcall");
                    _value = sret_tmp;
                    return true;
                }
                auto result = emit_virtual_dispatch_call(*_builder, *di.dispatch_class, args[0],
                    di.slot_index, fn_type, args, _context, "cast_vcall");
                if (result) {
                    _value = result;
                    return true;
                }
            }
            if (di.imported_dispatch_agg) {
                auto imp_agg = di.imported_dispatch_agg;
                auto* struct_llvm_type = imp_agg->get_struct_type()
                                         ? imp_agg->get_struct_type()->get_llvm_type() : nullptr;
                if (struct_llvm_type) {
                    llvm::LLVMContext& llvm_ctx = **_context;
                    llvm::Type* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);
                    if (is_sret) {
                        auto* sret_tmp = prepare_sret_for_cast(args, false);
                        llvm::Value* vptr_addr = _builder->CreateStructGEP(
                            struct_llvm_type, args[1], 0, "cast_imp_vptr_addr");
                        llvm::Value* vptr = _builder->CreateLoad(ptr_ty, vptr_addr, "cast_imp_vptr");
                        const uint64_t ptr_size = 8;
                        llvm::Value* slot_offset = llvm::ConstantInt::get(
                            llvm::Type::getInt64Ty(llvm_ctx),
                            (di.slot_index + 1) * ptr_size);
                        llvm::Value* fn_ptr_addr = _builder->CreateInBoundsGEP(
                            llvm::Type::getInt8Ty(llvm_ctx), vptr, slot_offset, "cast_imp_vtbl_slot");
                        llvm::Value* fn_ptr = _builder->CreateLoad(ptr_ty, fn_ptr_addr, "cast_imp_fn_ptr");
                        _builder->CreateCall(fn_type, fn_ptr, args);
                        _value = sret_tmp;
                        return true;
                    }
                    llvm::Value* vptr_addr = _builder->CreateStructGEP(
                        struct_llvm_type, args[0], 0, "cast_imp_vptr_addr");
                    llvm::Value* vptr = _builder->CreateLoad(ptr_ty, vptr_addr, "cast_imp_vptr");
                    const uint64_t ptr_size = 8;
                    llvm::Value* slot_offset = llvm::ConstantInt::get(
                        llvm::Type::getInt64Ty(llvm_ctx),
                        (di.slot_index + 1) * ptr_size);
                    llvm::Value* fn_ptr_addr = _builder->CreateInBoundsGEP(
                        llvm::Type::getInt8Ty(llvm_ctx), vptr, slot_offset, "cast_imp_vtbl_slot");
                    llvm::Value* fn_ptr = _builder->CreateLoad(ptr_ty, fn_ptr_addr, "cast_imp_fn_ptr");
                    _value = _builder->CreateCall(fn_type, fn_ptr, args,
                        fn_type->getReturnType()->isVoidTy() ? "" : "cast_imp_vcall");
                    return true;
                }
            }
        }
    }

    if (!llvm_func) {
        throw_internal_error(0x0068, expr.first_lexeme(),
            "Internal error: casting operator function '{}' has no LLVM definition and is not dispatched virtually",
            {op_func->get_short_name()});
    }

    // Step 4: Handle virtual dispatch and sret return
    // Direct call
    bool op_uses_sret = llvm_func->getReturnType()->isVoidTy() && op_needs_sret();
    if (op_uses_sret) {
        auto* sret_dest = prepare_sret_for_cast(args, true);
        _builder->CreateCall(llvm_func, args);
        _value = sret_dest;
    } else {
        _value = _builder->CreateCall(llvm_func, args,
            llvm_func->getReturnType()->isVoidTy() ? "" : "cast_call");
    }
    return true;
}


//
// Arithmetic binary expression
//

void symbol_resolver::process_arithmetic(binary_expression& expr) {
    visit_binary_expression(expr);
}

void type_reference_resolver::process_arithmetic(binary_expression& expr) {
    // TODO Rework conversions and promotions
    visit_binary_expression(expr);

    auto left = expr.left();
    auto right = expr.right();

    auto left_type = left->get_type();
    auto target_type = left_type;
    if(type::is_reference(target_type)) {
        // Target type must be de-referenced
        target_type = std::dynamic_pointer_cast<reference_type>(target_type)->get_subtype();
    } else if(type::is_drain(target_type)) {
        // Drain type must be unwrapped like a reference
        target_type = std::dynamic_pointer_cast<drain_type>(target_type)->get_drained_type();
    }
    // Detect constness before stripping const qualifier
    bool is_const_left = type::is_const(target_type);
    // Strip const qualifier for arithmetic type checks (const is compile-time only)
    target_type = type::remove_const(target_type);

    // ── Enum → underlying primitive conversion for both operands ──
    if (auto left_enum = std::dynamic_pointer_cast<enum_type>(target_type)) {
        target_type = left_enum->get_underlying_type();
        left = adapt_type(left, target_type);
        if (left) expr.assign_left(left);
    }

    // ── Operator overload for aggregate (struct/class/interface) references ──
    if(type::is_struct(target_type)) {
        auto st_type = std::dynamic_pointer_cast<struct_type>(type::remove_const(target_type));
        if (st_type) {
            auto agg = st_type->get_struct();
            if (agg) {
                auto [op_func, adapted_right] = resolve_binary_operator_overload(expr, agg, left, right, is_const_left);
                if (op_func) {
                    // Store the resolved operator function on the expression
                    expr.set_operator_func(op_func);
                    // Apply the adapted right operand (implicit cast if needed)
                    if (adapted_right && adapted_right != right) {
                        expr.assign_right(adapted_right);
                    }
                    // Set the expression type to the return type of the operator function
                    if (op_func->has_return_type()) {
                        expr.set_type(op_func->get_return_type());
                    } else {
                        expr.set_type(target_type);
                    }
                    // Compute dispatch info for virtual calls
                    if (op_func->is_member()) {
                        auto di = compute_operator_dispatch_info(op_func, left_type);
                        expr.set_operator_dispatch_info(std::move(di));
                    } else {
                        virtual_dispatch_info di;
                        di.kind = virtual_dispatch_info::dispatch_kind::DIRECT;
                        expr.set_operator_dispatch_info(std::move(di));
                    }
                    return;
                }
            }
        }
        throw_error(0x0001, expr.first_lexeme(),
            "No matching operator overload found for non-primitive type: "
            "the left operand has type '{}'; define an operator function or use primitive types",
            {target_type ? target_type->to_string() : "?"});
    }

    if(!type::is_primitive(target_type)) {
        throw_error(0x0001, expr.first_lexeme(),
            "Arithmetic operators are not supported for non-primitive types: "
            "the left operand has type '{}'; only numeric primitive types are supported",
            {target_type ? target_type->to_string() : "?"});
    }
    if(type::is_prim_bool(target_type)) {
        throw_error(0x0002, expr.first_lexeme(),
            "Arithmetic operators cannot be applied to boolean operands: "
            "use logical operators ('&&', '||', '!') instead of arithmetic operators for boolean values");
    }

    expr.set_type(target_type);

    auto source_type = right->get_type();
    if(type::is_pointer(source_type)) {
        throw_error(0x0003, expr.first_lexeme(),
            "Arithmetic operators are not supported for pointer types: "
            "the right operand has a pointer type '{}'; pointer arithmetic is not allowed",
            {source_type ? source_type->to_string() : "?"});
    }
    // If source type is reference, deref it
    if(type::is_reference(source_type)) {
        // Source type must be de-referenced
        right = load_value_expression::make_shared(right);
        source_type = std::dynamic_pointer_cast<reference_type>(source_type)->get_subtype();
        right->set_type(source_type);
        expr.assign_right(right);
    } else if(type::is_drain(source_type)) {
        // Drain type must be dereferenced like a reference
        right = load_value_expression::make_shared(right);
        source_type = std::dynamic_pointer_cast<drain_type>(source_type)->get_drained_type();
        right->set_type(source_type);
        expr.assign_right(right);
    }
    // Convert right enum to underlying primitive too
    source_type = type::remove_const(source_type);
    if (auto right_enum = std::dynamic_pointer_cast<enum_type>(source_type)) {
        source_type = right_enum->get_underlying_type();
        right = adapt_type(right, source_type);
        if (right) {
            expr.assign_right(right);
        }
    }

    // TODO Promote to largest target_type instead to align to left operand.
    auto cast = adapt_type(right, target_type);
    if(!cast) {
        throw_error(0x0004, expr.first_lexeme(),
            "Incompatible types in arithmetic expression: "
            "the right operand of type '{}' cannot be implicitly converted to the left operand type '{}'; "
            "use an explicit cast if a narrowing conversion is intended",
            {right->get_type() ? right->get_type()->to_string() : "?",
             target_type ? target_type->to_string() : "?"});
    } else if(cast != right) {
        // Casted, assign casted expression instead of right source.
        expr.assign_right(cast);
    } else {
        // Compatible target_type, no need to cast.
    }
}

void symbol_resolver::visit_arithmetic_binary_expression(arithmetic_binary_expression &expr) {
    process_arithmetic(expr);
}

void type_reference_resolver::visit_arithmetic_binary_expression(arithmetic_binary_expression &expr) {
    process_arithmetic(expr);
}

//
// Addition expression (+)
//

void implementation_generator::visit_addition_expression(addition_expression &expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    // If left operand is a reference, dereference it.
    // Right is supposed to be already dereferenced
    if(type::is_reference(expr.left()->get_type()) || type::is_drain(expr.left()->get_type())) {
        llvm::Type* type = _context->get_llvm_type(expr.left()->get_type()->get_subtype());
        left = _builder->CreateLoad(type, left);
    }

    if(type::is_prim_integer(expr.get_type())) {
        _value = _builder->CreateAdd(left, right);
    } else if(type::is_prim_float(expr.get_type())) {
        _value = _builder->CreateFAdd(left, right);
    } else {
        // TODO: Support other types
    }
}

//
// Substraction expression (-)
//

void implementation_generator::visit_substraction_expression(substraction_expression &expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    // If left operand is a reference, dereference it.
    // Right is supposed to be already dereferenced
    if(type::is_reference(expr.left()->get_type()) || type::is_drain(expr.left()->get_type())) {
        llvm::Type* type = _context->get_llvm_type(expr.left()->get_type()->get_subtype());
        left = _builder->CreateLoad(type, left);
    }

    if(type::is_prim_integer(expr.get_type())) {
        _value = _builder->CreateSub(left, right);
    } else if(type::is_prim_float(expr.get_type())) {
        _value = _builder->CreateFSub(left, right);
    } else {
        // TODO: Support other types
    }
}

//
// Multiplication expression (*)
//

void implementation_generator::visit_multiplication_expression(multiplication_expression &expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    // If left operand is a reference, dereference it.
    // Right is supposed to be already dereferenced
    if(type::is_reference(expr.left()->get_type()) || type::is_drain(expr.left()->get_type())) {
        llvm::Type* type = _context->get_llvm_type(expr.left()->get_type()->get_subtype());
        left = _builder->CreateLoad(type, left);
    }

    // TODO: Check for type alignement
    if(type::is_prim_integer(expr.get_type())) {
        // TODO Should poison for int/uint multiplication overflow ?
        _value = _builder->CreateMul(left, right);
    } else if(type::is_prim_float(expr.get_type())) {
        _value = _builder->CreateFMul(left, right);
    } else {
        // TODO: Support other types
    }
}

//
// Division expression (/)
//

void implementation_generator::visit_division_expression(division_expression &expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    // If left operand is a reference, dereference it.
    // Right is supposed to be already dereferenced
    if(type::is_reference(expr.left()->get_type()) || type::is_drain(expr.left()->get_type())) {
        llvm::Type* type = _context->get_llvm_type(expr.left()->get_type()->get_subtype());
        left = _builder->CreateLoad(type, left);
    }

    if(auto prim = std::dynamic_pointer_cast<primitive_type>(type::remove_const(expr.get_type()))) {
        if(prim->is_integer()) {
            if(prim->is_unsigned()) {
                _value = _builder->CreateUDiv(left, right);
            } else {
                _value = _builder->CreateSDiv(left, right);
            }
        } else if(prim->is_float()) {
            _value = _builder->CreateFDiv(left, right);
        }
    } else {
        // TODO: Support other types
    }
}

//
// Modulo expression (%)
//

void implementation_generator::visit_modulo_expression(modulo_expression &expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    // If left operand is a reference, dereference it.
    // Right is supposed to be already dereferenced
    if(type::is_reference(expr.left()->get_type()) || type::is_drain(expr.left()->get_type())) {
        llvm::Type* type = _context->get_llvm_type(expr.left()->get_type()->get_subtype());
        left = _builder->CreateLoad(type, left);
    }

    if(auto prim = std::dynamic_pointer_cast<primitive_type>(type::remove_const(expr.get_type()))) {
        if(prim->is_integer()) {
            if(prim->is_unsigned()) {
                _value = _builder->CreateURem(left, right);
            } else {
                _value = _builder->CreateSRem(left, right);
            }
        } else if(prim->is_float()) {
            _value = _builder->CreateFRem(left, right);
        }
    } else {
        // TODO: Support other types
    }
}

//
// Bitwise and expression
//

void implementation_generator::visit_bitwise_and_expression(bitwise_and_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    // If left operand is a reference, dereference it.
    // Right is supposed to be already dereferenced
    if(type::is_reference(expr.left()->get_type()) || type::is_drain(expr.left()->get_type())) {
        llvm::Type* type = _context->get_llvm_type(expr.left()->get_type()->get_subtype());
        left = _builder->CreateLoad(type, left);
    }

    if(auto prim = std::dynamic_pointer_cast<primitive_type>(type::remove_const(expr.get_type()))) {
        if(prim->is_integer()) {
            _value = _builder->CreateAnd(left, right);
        } else if(prim->is_float()) {
            throw_error(0x0005, expr.first_lexeme(),
                "Bitwise AND ('&') cannot be applied to floating-point values: "
                "bitwise operations are only defined for integer types; "
                "the operand has type '{}'",
                {prim->to_string()});
        }
    } else {
        // TODO: Support other types
    }
}

//
// Bitwise or expression
//

void implementation_generator::visit_bitwise_or_expression(bitwise_or_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    // If left operand is a reference, dereference it.
    // Right is supposed to be already dereferenced
    if(type::is_reference(expr.left()->get_type()) || type::is_drain(expr.left()->get_type())) {
        llvm::Type* type = _context->get_llvm_type(expr.left()->get_type()->get_subtype());
        left = _builder->CreateLoad(type, left);
    }

    if(auto prim = std::dynamic_pointer_cast<primitive_type>(type::remove_const(expr.get_type()))) {
        if(prim->is_integer()) {
            _value = _builder->CreateOr(left, right);
        } else if(prim->is_float()) {
            throw_error(0x0006, expr.first_lexeme(),
                "Bitwise OR ('|') cannot be applied to floating-point values: "
                "bitwise operations are only defined for integer types; "
                "the operand has type '{}'",
                {prim->to_string()});
        }
    } else {
        // TODO: Support other types
    }
}

//
// Bitwise xor expression
//

void implementation_generator::visit_bitwise_xor_expression(bitwise_xor_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    // If left operand is a reference, dereference it.
    // Right is supposed to be already dereferenced
    if(type::is_reference(expr.left()->get_type()) || type::is_drain(expr.left()->get_type())) {
        llvm::Type* type = _context->get_llvm_type(expr.left()->get_type()->get_subtype());
        left = _builder->CreateLoad(type, left);
    }

    if(auto prim = std::dynamic_pointer_cast<primitive_type>(type::remove_const(expr.get_type()))) {
        if(prim->is_integer()) {
            _value = _builder->CreateXor(left, right);
        } else if(prim->is_float()) {
            throw_error(0x0007, expr.first_lexeme(),
                "Bitwise XOR ('^') cannot be applied to floating-point values: "
                "bitwise operations are only defined for integer types; "
                "the operand has type '{}'",
                {prim->to_string()});
        }
    } else {
        // TODO: Support other types
    }
}

//
// Left shift expression
//

void implementation_generator::visit_left_shift_expression(left_shift_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    // If left operand is a reference, dereference it.
    // Right is supposed to be already dereferenced
    if(type::is_reference(expr.left()->get_type()) || type::is_drain(expr.left()->get_type())) {
        llvm::Type* type = _context->get_llvm_type(expr.left()->get_type()->get_subtype());
        left = _builder->CreateLoad(type, left);
    }

    if(auto prim = std::dynamic_pointer_cast<primitive_type>(type::remove_const(expr.get_type()))) {
        if(prim->is_integer()) {
            // TODO may it poison when overflow ?
            _value = _builder->CreateShl(left, right);
        } else if(prim->is_float()) {
            throw_error(0x0008, expr.first_lexeme(),
                "Left shift ('<<') cannot be applied to floating-point values: "
                "shift operations are only defined for integer types; "
                "the operand has type '{}'",
                {prim->to_string()});
        }
    } else {
        // TODO: Support other types
    }
}

//
// Right shift expression
//

void implementation_generator::visit_right_shift_expression(right_shift_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    // If left operand is a reference, dereference it.
    // Right is supposed to be already dereferenced
    if(type::is_reference(expr.left()->get_type()) || type::is_drain(expr.left()->get_type())) {
        llvm::Type* type = _context->get_llvm_type(expr.left()->get_type()->get_subtype());
        left = _builder->CreateLoad(type, left);
    }

    if(auto prim = std::dynamic_pointer_cast<primitive_type>(type::remove_const(expr.get_type()))) {
        if(prim->is_integer()) {
            if(prim->is_unsigned()) {
                // TODO may it poison when overflow ?
                _value = _builder->CreateLShr(left, right);
            } else {
                // TODO may it poison when overflow ?
                _value = _builder->CreateAShr(left, right);
            }
        } else if(prim->is_float()) {
            throw_error(0x0009, expr.first_lexeme(),
                "Right shift ('>>') cannot be applied to floating-point values: "
                "shift operations are only defined for integer types; "
                "the operand has type '{}'",
                {prim->to_string()});
        }
    } else {
        // TODO: Support other types
    }
}

//
// Assignation expression
//

/**
 * Resolve an assignment expression (=, +=, -=, etc.): validate target, type-check operands.
 *
 * Steps:
 *   1. Resolve left and right sub-expressions.
 *   2. Validate that the left operand is assignable (reference, not const).
 *   3. For struct types: check for operator= overload or direct copy.
 *   4. For owner types: validate move semantics (right must be owner or new).
 *   5. For pointer/link/view: validate type compatibility and const-correctness.
 *   6. For primitives: adapt right operand type to match left.
 *   7. Set result type.
 */
void type_reference_resolver::visit_assignation_expression(assignation_expression &expr) {
    // TODO Rework conversions and promotions and mutualize with symbol_type_resolver::process_arithmetic(...)
    visit_binary_expression(expr);

    auto left = expr.left();
    auto right = expr.right();

    auto left_type = left->get_type();

    if(!type::is_reference(left_type)) {
        throw_error(0x000A, expr.first_lexeme(),
            "The left operand of an assignment must be assignable (an lvalue): "
            "the left-hand side has type '{}' which is not a reference; "
            "you can only assign to a variable, parameter, or array element",
            {left_type ? left_type->to_string() : "?"});
    }
    auto ref_target_type = std::dynamic_pointer_cast<reference_type>(left_type);
    auto target_type = ref_target_type->get_subtype();

    // ── Const-check ──────────────────────────────────────────────────────────
    // If the target type is const-qualified (ref<const T>), assignment is forbidden.
    if (type::is_const(target_type)) {
        throw_error(0x0080, expr.first_lexeme(),
            "Cannot assign to a const variable: "
            "the left-hand side has type '{}' which is const; "
            "const variables cannot be modified after initialisation",
            {target_type ? target_type->to_string() : "?"});
    }
    // ─────────────────────────────────────────────────────────────────────────

    // Step 1: Resolve left and right sub-expressions
    if(type::is_reference(target_type)) {
        // Left hand is ref-to-ref: assignment acts on the underlying object.
        left = load_value_expression::make_shared(left);
        left->set_type(target_type);
        expr.assign_left(left);
        target_type = std::dynamic_pointer_cast<reference_type>(target_type)->get_subtype();
    } else if (type::is_link(target_type)) {
        // Left hand is ref-to-link.
        // Determine if this is a rebind (RHS is an indirection) or
        // an assignment to the pointed object (RHS is a value).
        auto link_subtype = std::dynamic_pointer_cast<link_type>(target_type)->get_linked_type();
        auto rhs_type = right->get_type();
        // Unwrap ref<indirection> from rhs_type
        auto rhs_effective = rhs_type;
        if (type::is_reference(rhs_type)) {
            auto inner = std::dynamic_pointer_cast<reference_type>(rhs_type)->get_subtype();
            if (type::is_link(inner) || type::is_pointer(inner) || type::is_view(inner)) {
                rhs_effective = inner;
            }
        }
        // Helper lambda: check if rhs_effective is an indirection compatible with link_subtype (same, static upcast, or dynamic downcast)
        auto is_rebind_compatible = [&]() -> bool {
            if (!type::is_any_indirection(rhs_effective) || !rhs_effective->get_subtype() || !link_subtype)
                return false;
            auto rhs_sub_nc = type::remove_const(rhs_effective->get_subtype());
            auto lnk_sub_nc = type::remove_const(link_subtype);
            if (type::are_equal(rhs_sub_nc, lnk_sub_nc)) return true;
            auto src_st = std::dynamic_pointer_cast<struct_type>(rhs_sub_nc);
            auto tgt_st = std::dynamic_pointer_cast<struct_type>(lnk_sub_nc);
            if (!src_st || !tgt_st || !src_st->get_struct() || !tgt_st->get_struct()) return false;
            // Static upcast: rhs points to Derived, link points to Base
            if (src_st->get_struct()->is_derived_from(tgt_st->get_struct())) return true;
            // Dynamic downcast: rhs points to Base, link points to Derived (klass/interface only)
            if (tgt_st->get_struct()->is_derived_from(src_st->get_struct()) &&
                tgt_st->get_struct()->has_rtti() &&
                std::dynamic_pointer_cast<klass>(tgt_st->get_struct()) != nullptr) return true;
            return false;
        };
        // If RHS is an indirection compatible (same or upcast) with link_subtype: REBIND
        if (is_rebind_compatible()) {
            // Rebind: check const compatibility (const T~ ← T~ is OK; T~ ← const T~ is not)
            if (type::is_const(rhs_effective->get_subtype()) && !type::is_const(link_subtype)) {
                throw_error(0x0082, expr.first_lexeme(),
                    "Cannot rebind a link-to-mutable ('{}') from a link-to-const ('{}'): "
                    "this would allow modification of a const object",
                    {target_type ? target_type->to_string() : "?",
                     rhs_type ? rhs_type->to_string() : "?"});
            }
            // Rebind: load the source address and store into the link alloca.
            // If source is nullable, warn — null-check at IR level.
            if (type::is_nullable_indirection(rhs_effective)) {
                auto diag = k::log::diagnostic::make_warning(with_flag(0x0072),
                    "Rebinding a link from a nullable indirection (type '{}'): "
                    "a runtime null-check will be inserted",
                    {rhs_type ? rhs_type->to_string() : "?"});
                logger_relay::report(diag);
            }
            // Unwrap the ref wrapper from rhs if needed
            if (type::is_reference(rhs_type)) {
                right = load_value_expression::make_shared(right);
                rhs_type = rhs_effective;
                right->set_type(rhs_type);
                expr.assign_right(right);
            }
            // Determine whether to use static upcast or dynamic downcast
            {
                auto rhs_sub_nc = type::remove_const(right->get_type()->get_subtype());
                auto lnk_sub_nc = type::remove_const(link_subtype);
                if (!type::are_equal(rhs_sub_nc, lnk_sub_nc)) {
                    auto src_st = std::dynamic_pointer_cast<struct_type>(rhs_sub_nc);
                    auto tgt_st = std::dynamic_pointer_cast<struct_type>(lnk_sub_nc);
                    bool is_static_upcast = src_st && tgt_st &&
                        src_st->get_struct() && tgt_st->get_struct() &&
                        src_st->get_struct()->is_derived_from(tgt_st->get_struct());
                    if (is_static_upcast) {
                        auto upcast = cast_expression::make_shared(right, target_type);
                        upcast->set_type(target_type);
                        expr.assign_right(upcast);
                    } else {
                        // Dynamic downcast — lien is non-null, so fatal on null result
                        auto dc = cast_expression::make_shared(right, target_type, /*null_is_fatal=*/true);
                        expr.assign_right(dc);
                    }
                }
            }
            // The assignment stores a new address into the link alloca.
            expr.set_type(ref_target_type);
            return;
        }
        // Otherwise: transparent reference — assignment to the pointed object.
        left = load_value_expression::make_shared(left);
        left->set_type(target_type);
        auto ref_to_target = link_subtype->get_reference();
        left->set_type(ref_to_target);
        expr.assign_left(left);
        target_type = link_subtype;
        ref_target_type = ref_to_target;
    } else if (type::is_view(target_type)) {
        throw_error(0x0070, expr.first_lexeme(),
            "Cannot assign to a view indirection (type '{}'): "
            "a view ('?') is immutable after initialisation",
            {target_type ? target_type->to_string() : "?"});
    }

    auto source_type = right->get_type();

    // Unwrap ref<link/ptr/pin> for source-side checks
    auto effective_source_type = source_type;
    if (type::is_reference(source_type)) {
        auto inner = std::dynamic_pointer_cast<reference_type>(source_type)->get_subtype();
        if (type::is_link(inner) || type::is_pointer(inner) || type::is_view(inner)) {
            effective_source_type = inner;
        }
    }

    if(type::is_pointer(target_type)) {
        // Null literal: always compatible with any pointer type.
        if(type::is_null(effective_source_type) || type::is_null(source_type)) {
            expr.set_type(ref_target_type);
            return;
        }
        if(type::is_pointer(effective_source_type) || type::is_link(effective_source_type)
           || type::is_view(effective_source_type)) {
            auto src_sub = effective_source_type->get_subtype();
            auto tgt_sub = target_type->get_subtype();
            // Strip const from both sides for structural comparison
            auto src_sub_nc = type::remove_const(src_sub);
            auto tgt_sub_nc = type::remove_const(tgt_sub);
            if (src_sub_nc != tgt_sub_nc) {
                // Check static upcast: ptr<Derived>→ptr<Base>
                auto src_st = std::dynamic_pointer_cast<struct_type>(src_sub_nc);
                auto tgt_st = std::dynamic_pointer_cast<struct_type>(tgt_sub_nc);
                bool is_static_upcast = src_st && tgt_st &&
                                 src_st->get_struct() && tgt_st->get_struct() &&
                                 src_st->get_struct()->is_derived_from(tgt_st->get_struct());
                bool is_dynamic_downcast = !is_static_upcast && src_st && tgt_st &&
                                 src_st->get_struct() && tgt_st->get_struct() &&
                                 tgt_st->get_struct()->has_rtti();
                                 std::dynamic_pointer_cast<klass>(tgt_st->get_struct()) != nullptr;
                if (!is_static_upcast && !is_dynamic_downcast) {
                    throw_error(0x000B, expr.first_lexeme(),
                        "Pointer assignment type mismatch: "
                        "cannot assign a '{}' to a '{}'; pointer subtypes must match "
                        "or source must be a derived type of the target",
                        {source_type ? source_type->to_string() : "?",
                         target_type ? target_type->to_string() : "?"});
                }
                // Forbid const T* → T* (would lose const-ness on pointed object)
                if (type::is_const(src_sub) && !type::is_const(tgt_sub)) {
                    throw_error(0x0081, expr.first_lexeme(),
                        "Cannot assign a pointer-to-const ('{}') to a pointer-to-mutable ('{}'): "
                        "this would allow modification of a const object through the mutable pointer",
                        {source_type ? source_type->to_string() : "?",
                         target_type ? target_type->to_string() : "?"});
                }
                // Unwrap ref wrapper if needed
                if (type::is_reference(source_type)) {
                    right = load_value_expression::make_shared(right);
                    source_type = std::dynamic_pointer_cast<reference_type>(source_type)->get_subtype();
                    right->set_type(source_type);
                    expr.assign_right(right);
                }
                if (is_static_upcast) {
                    auto upcast = cast_expression::make_shared(right, target_type);
                    upcast->set_type(target_type);
                    expr.assign_right(upcast);
                } else {
                    // Dynamic downcast — ptr can be null, not fatal
                    auto dc = cast_expression::make_shared(right, target_type, /*null_is_fatal=*/false);
                    expr.assign_right(dc);
                }
                expr.set_type(ref_target_type);
                return;
            }
            // Forbid const T* → T* (would lose const-ness on pointed object)
            if (type::is_const(src_sub) && !type::is_const(tgt_sub)) {
                throw_error(0x0081, expr.first_lexeme(),
                    "Cannot assign a pointer-to-const ('{}') to a pointer-to-mutable ('{}'): "
                    "this would allow modification of a const object through the mutable pointer",
                    {source_type ? source_type->to_string() : "?",
                     target_type ? target_type->to_string() : "?"});
            }
            if (type::is_reference(source_type)) {
                right = load_value_expression::make_shared(right);
                source_type = std::dynamic_pointer_cast<reference_type>(source_type)->get_subtype();
                right->set_type(source_type);
                expr.assign_right(right);
            }
            expr.set_type(ref_target_type);
            return;
        } else {
            throw_error(0x000C, expr.first_lexeme(),
                "Pointer assignment requires a pointer or link on the right-hand side: "
                "cannot assign a value of type '{}' to a pointer of type '{}'",
                {source_type ? source_type->to_string() : "?",
                 target_type ? target_type->to_string() : "?"});
        }
    } else if (type::is_link(target_type)) {
        // Direct link rebind (reached after link-to-link case not matched above).
        if (!type::is_any_indirection(effective_source_type)) {
            throw_error(0x0071, expr.first_lexeme(),
                "Link assignment requires an indirection on the right-hand side, "
                "but got type '{}'",
                {source_type ? source_type->to_string() : "?"});
        }
        if (type::is_nullable_indirection(effective_source_type)) {
            auto diag = k::log::diagnostic::make_warning(with_flag(0x0072),
                "Assigning a nullable indirection (type '{}') to a link: "
                "a runtime null-check will be inserted",
                {source_type ? source_type->to_string() : "?"});
            logger_relay::report(diag);
        }
        if (type::is_reference(source_type)) {
            right = load_value_expression::make_shared(right);
            source_type = std::dynamic_pointer_cast<reference_type>(source_type)->get_subtype();
            right->set_type(source_type);
            expr.assign_right(right);
        }
        expr.set_type(ref_target_type);
        return;
    } else if (type::is_sized_array(target_type)) {
        // Array = array : element-wise copy (see spec).
        // Source must be a reference to a sized array of the same element type.
        auto dest_arr = std::dynamic_pointer_cast<sized_array_type>(target_type);
        std::shared_ptr<type> src_inner_type = source_type;
        if (type::is_reference(source_type)) {
            src_inner_type = std::dynamic_pointer_cast<reference_type>(source_type)->get_subtype();
        }
        if (!type::is_sized_array(src_inner_type)) {
            throw_error(0x0060, expr.first_lexeme(),
                "Array assignment: the right-hand side must be an array of the same element type, "
                "but '{}' is not a sized array",
                {source_type ? source_type->to_string() : "?"});
        }
        auto src_arr = std::dynamic_pointer_cast<sized_array_type>(src_inner_type);
        if (!type::are_equal(dest_arr->get_subtype(), src_arr->get_subtype())) {
            throw_error(0x0061, expr.first_lexeme(),
                "Array assignment: element type mismatch — cannot copy from '{}' to '{}'",
                {source_type ? source_type->to_string() : "?",
                 target_type ? target_type->to_string() : "?"});
        }
        // Type of the assignment expression is ref<dest array>
        expr.set_type(ref_target_type);
        // Ensure the source is referenced (if it isn't already)
        if (!type::is_reference(source_type)) {
            right = load_value_expression::make_shared(right);
            right->set_type(source_type->get_reference());
            expr.assign_right(right);
        }
        return; // code generation handled in visit_simple_assignation_expression
    } else if (type::is_function_reference(target_type)) {
        // Assigning a function address (or another frt variable) to a function-pointer variable.
        // target_type is a function_reference_type; source should be ref<frt> (function symbol)
        // or frt itself (another variable). The ref wrapper is stripped below if present.
        //
        // Check: only pointer (*) frt is rebindable; view (?) and link (+) are immutable.
        auto frt_target = std::dynamic_pointer_cast<function_reference_type>(target_type);
        if (frt_target && frt_target->get_ref_kind() != function_reference_type::ref_kind::pointer) {
            throw_error(0x0090, expr.first_lexeme(),
                "Cannot assign to an immutable function reference (type '{}'): "
                "only pointer (*) function references are rebindable",
                {target_type ? target_type->to_string() : "?"});
        }
        // Unwrap ref<frt> on the source side if needed.
        // For a direct function symbol (is_function()), impl_gen returns the Function* directly —
        // no load needed. For a frt variable, impl_gen returns the alloca address — needs a load.
        if (type::is_reference(source_type)) {
            auto inner = std::dynamic_pointer_cast<reference_type>(source_type)->get_subtype();
            if (type::is_function_reference(inner)) {
                auto rhs_sym = std::dynamic_pointer_cast<symbol_expression>(right);
                if (!rhs_sym || !rhs_sym->is_function()) {
                    // Variable of frt type: load the stored function pointer from the alloca
                    right = load_value_expression::make_shared(right);
                    source_type = inner;
                    right->set_type(source_type);
                    expr.assign_right(right);
                }
                // else: direct function symbol → keep ref<frt>; impl_gen produces Function* directly
            }
        }
        expr.set_type(ref_target_type);
        return;
    } else if (type::is_owner(target_type)) {
        // ── Owner assignment: destroy old object (if any), transfer ownership ─────
        //   - null literal    → destroy current + set null
        //   - ref<owner<T>>  → move (load + null source), same or compatible subtype
        //   - owner<T>       → direct (from new_expression or already an owner value)

        // Detect null literal (both parsed 'null' and programmatic nullptr)
        bool rhs_is_null = type::is_null(source_type);
        if (!rhs_is_null) {
            if (auto ve = std::dynamic_pointer_cast<value_expression>(right)) {
                if (ve->is_literal() && ve->any_literal().has_value()) {
                    rhs_is_null = std::holds_alternative<lex::null>(ve->any_literal());
                } else {
                    rhs_is_null = std::holds_alternative<std::nullptr_t>(ve->get_value());
                }
            }
        }
        if (rhs_is_null) {
            // Assign null: destroy current owned object and store null
            expr.set_type(ref_target_type);
            return;
        }

        if (type::is_reference(source_type)) {
            auto inner = std::dynamic_pointer_cast<reference_type>(source_type)->get_subtype();
            if (type::is_owner(inner)) {
                // Move: wrap source in owner_move_expression (load + null source alloca)
                auto own_src_nc = type::remove_const(inner->get_subtype());
                auto own_tgt_nc = type::remove_const(target_type->get_subtype());
                auto move = owner_move_expression::make_shared(right);
                move->set_type(inner);
                std::shared_ptr<expression> new_right = move;
                if (!type::are_equal(own_src_nc, own_tgt_nc)) {
                    // Check upcast: owner<Derived> → owner<Base>
                    auto src_st = std::dynamic_pointer_cast<struct_type>(own_src_nc);
                    auto tgt_st = std::dynamic_pointer_cast<struct_type>(own_tgt_nc);
                    if (!src_st || !tgt_st || !src_st->get_struct() || !tgt_st->get_struct() ||
                        !src_st->get_struct()->is_derived_from(tgt_st->get_struct())) {
                        throw_error(0x00A1, expr.first_lexeme(),
                            "Owner assignment type mismatch: cannot move '{}' into '{}'",
                            {source_type->to_string(), target_type->to_string()});
                    }
                    auto upcast = cast_expression::make_shared(move, target_type);
                    upcast->set_type(target_type);
                    new_right = upcast;
                }
                expr.assign_right(new_right);
                expr.set_type(ref_target_type);
                return;
            }
        }
        if (type::is_owner(source_type)) {
            // Direct owner value (e.g. from new_expression or already an owner_move_expression)
            expr.set_type(ref_target_type);
            return;
        }
        throw_error(0x00A0, expr.first_lexeme(),
            "Owner assignment: right-hand side must be an owner value, "
            "another owner variable (move), or null; got type '{}'",
            {source_type ? source_type->to_string() : "?"});
    } else if(type::is_struct(target_type)) {
        // ── Struct assignment: try operator overload ──
        auto nc_target = type::remove_const(target_type);
        auto st_type = std::dynamic_pointer_cast<struct_type>(nc_target);
        if (st_type) {
            auto agg = st_type->get_struct();
            if (agg) {
                // Check if the operator is explicitly deleted
                std::string op_name = get_binary_operator_name(expr);
                if (!op_name.empty()) {
                    auto member_funcs = collect_member_operators_from_hierarchy(agg, op_name);
                    for (auto& f : member_funcs) {
                        if (f->is_deleted()) {
                            throw_error(0x00B0, expr.first_lexeme(),
                                "Use of deleted operator '{}' on type '{}': "
                                "this operator was explicitly deleted with '-> delete'",
                                {get_operator_symbol(op_name),
                                 target_type ? target_type->to_string() : "?"});
                        }
                    }
                }
                bool is_const_left = type::is_const(target_type);
                auto [op_func, adapted_right] = resolve_binary_operator_overload(expr, agg, left, right, is_const_left);
                if (op_func) {
                    // Store the resolved operator function on the expression
                    expr.set_operator_func(op_func);
                    // Apply the adapted right operand (implicit cast if needed)
                    if (adapted_right && adapted_right != right) {
                        expr.assign_right(adapted_right);
                    }
                    // Set the expression type to the return type of the operator function
                    if (op_func->has_return_type()) {
                        expr.set_type(op_func->get_return_type());
                    } else {
                        expr.set_type(ref_target_type);
                    }
                    // Compute dispatch info for virtual calls
                    if (op_func->is_member()) {
                        auto di = compute_operator_dispatch_info(op_func, left_type);
                        expr.set_operator_dispatch_info(std::move(di));
                    } else {
                        virtual_dispatch_info di;
                        di.kind = virtual_dispatch_info::dispatch_kind::DIRECT;
                        expr.set_operator_dispatch_info(std::move(di));
                    }
                    return;
                }
            }
        }
        // No operator overload found — fall through to default struct assignment (memcpy/store).
        expr.set_type(ref_target_type);
        // If source type is reference, deref it
        if(type::is_reference(source_type)) {
            right = load_value_expression::make_shared(right);
            source_type = std::dynamic_pointer_cast<reference_type>(source_type)->get_subtype();
            right->set_type(source_type);
            expr.assign_right(right);
        }
        return;
    } else if(!type::is_primitive(target_type)) {
        throw_error(0x000D, expr.first_lexeme(),
            "Assignment to a non-primitive, non-pointer type is not yet supported: "
            "the target has type '{}'; only assignments to primitive types, pointers and arrays are supported",
            {target_type ? target_type->to_string() : "?"});
    } else if(type::is_prim_bool(target_type)) {
        throw_error(0x000E, expr.first_lexeme(),
            "Direct arithmetic assignment to a boolean variable is not supported: "
            "use a comparison or logical expression to produce a boolean value for '{}'",
            {target_type ? target_type->to_string() : "?"});
    }

    // Type of an assignation is a reference
    expr.set_type(ref_target_type);

    // Step 2: Validate that the left operand is assignable (reference, not const)
    // If source type is reference, deref it
    if(type::is_reference(source_type)) {
        // Source type must be de-referenced
        right = load_value_expression::make_shared(right);
        source_type = std::dynamic_pointer_cast<reference_type>(source_type)->get_subtype();
        right->set_type(source_type);
        expr.assign_right(right);
    }

    // Step 3: For struct types: check for operator= overload or direct copy
    // Step 6: For primitives: adapt right operand type to match left
    // TODO Promote to largest target_type instead to align to left operand.
    auto cast = adapt_type(right, target_type);
    if(!cast) {
        throw_error(0x000F, expr.first_lexeme(),
            "Incompatible types in assignment: "
            "the right-hand side of type '{}' cannot be implicitly converted to the target type '{}'; "
            "use an explicit cast if a narrowing conversion is intended",
            {right->get_type() ? right->get_type()->to_string() : "?",
             target_type ? target_type->to_string() : "?"});
    } else if(cast != right) {
        // Casted, assign casted expression instead of right source.
        expr.assign_right(cast);
    } else {
        // Compatible target_type, no need to cast.
    }
}

//
// Simple assignment expression (=)
//

/**
 * Generate LLVM IR for simple assignment (=).
 *
 * Steps:
 *   1. Evaluate left and right operands.
 *   2. For operator= overload: delegate to generate_binary_operator_overload.
 *   3. For owner assignment: emit owner_move + null the source.
 *   4. For struct copy: emit memcpy or copy constructor call.
 *   5. For primitives/pointers: emit store instruction.
 */
void implementation_generator::visit_simple_assignation_expression(simple_assignation_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    // Step 1: Evaluate left and right operands
    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        throw_internal_error(0x001B, expr.first_lexeme(),
            "Internal error: assignment expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    // Step 2: For operator= overload: delegate to generate_binary_operator_overload
    // left is a pointer to the storage.
    // Determine what the target type really is after one level of ref-unwrap.
    auto expr_left_type = expr.left()->get_type();
    auto left_ref_type  = std::dynamic_pointer_cast<reference_type>(expr_left_type);
    auto target_type    = left_ref_type ? left_ref_type->get_subtype() : nullptr;

    // If target is ref-to-ref, unwrap one more level (variable access pattern).
    if (target_type && type::is_reference(target_type)) {
        target_type = std::dynamic_pointer_cast<reference_type>(target_type)->get_subtype();
    }

    // ------------------------------------------------------------------
    // Array assignment: element-wise copy (spec: partial copy, no resize)
    // ------------------------------------------------------------------
    if (target_type && type::is_sized_array(target_type)) {
        auto dest_arr  = std::dynamic_pointer_cast<sized_array_type>(target_type);
        auto* struct_llvm    = dest_arr->get_llvm_struct_type();
        auto* data_arr_llvm  = dest_arr->get_llvm_data_array_type();
        auto* elem_llvm      = _context->get_llvm_type(dest_arr->get_subtype());
        auto  dest_n         = static_cast<uint64_t>(dest_arr->get_size());

        // right is the pointer to the source struct { i32, [N x T] }
        auto* i32_t = llvm::Type::getInt32Ty(_builder->getContext());

        // Source capacity (runtime value from field 0)
        llvm::Value* src_size_ptr = _builder->CreateStructGEP(struct_llvm, right,
            sized_array_type::FIELD_SIZE, "src_sz_ptr");
        llvm::Value* src_n = _builder->CreateLoad(i32_t, src_size_ptr, "src_n");

        // Data pointers
        llvm::Value* src_data  = _builder->CreateStructGEP(struct_llvm, right,
            sized_array_type::FIELD_DATA, "src_data");
        llvm::Value* dest_data = _builder->CreateStructGEP(struct_llvm, left,
            sized_array_type::FIELD_DATA, "dst_data");

        // copy_n = min(dest_n, src_n)
        auto* dest_n_val = llvm::ConstantInt::get(i32_t, dest_n, false);
        llvm::Value* copy_n = _builder->CreateSelect(
            _builder->CreateICmpULT(src_n, dest_n_val), src_n, dest_n_val, "copy_n");

        // Emit copy loop
        auto* fn = _builder->GetInsertBlock()->getParent();
        auto* pre_bb   = _builder->GetInsertBlock();
        auto* loop_bb  = llvm::BasicBlock::Create(_builder->getContext(), "arr_asgn_loop", fn);
        auto* done_bb  = llvm::BasicBlock::Create(_builder->getContext(), "arr_asgn_done", fn);

        _builder->CreateCondBr(
            _builder->CreateICmpUGT(copy_n, llvm::ConstantInt::get(i32_t, 0, false)),
            loop_bb, done_bb);

        _builder->SetInsertPoint(loop_bb);
        auto* idx = _builder->CreatePHI(i32_t, 2, "asgn_idx");
        idx->addIncoming(llvm::ConstantInt::get(i32_t, 0, false), pre_bb);

        llvm::Value* s = _builder->CreateGEP(data_arr_llvm, src_data,
            {_builder->getInt32(0), idx}, "s_elem");
        llvm::Value* d = _builder->CreateGEP(data_arr_llvm, dest_data,
            {_builder->getInt32(0), idx}, "d_elem");
        _builder->CreateStore(_builder->CreateLoad(elem_llvm, s, "ev"), d);

        auto* nxt = _builder->CreateAdd(idx, llvm::ConstantInt::get(i32_t, 1), "nxt");
        idx->addIncoming(nxt, loop_bb);
        _builder->CreateCondBr(_builder->CreateICmpULT(nxt, copy_n), loop_bb, done_bb);

        _builder->SetInsertPoint(done_bb);
        _value = left;
        return;
    }

    // ------------------------------------------------------------------
    // Owner assignment: delete old object (if any), store new pointer
    // ------------------------------------------------------------------
    if (target_type && type::is_owner(target_type)) {
        auto& llvm_ctx = _builder->getContext();
        auto* ptr_ty = llvm::PointerType::get(llvm_ctx, 0);

        // Step 3: For owner assignment: emit owner_move + null the source
        // If non-null, destroy + free the existing object (don't null-out, we're about to store the new value)
        auto own_type = std::dynamic_pointer_cast<owner_type>(target_type);
        emit_owner_cleanup_if_nonnull(_builder.get(), get_module(), _context->_functions,
            left, own_type->get_owned_type(), "owner_asgn", /*null_out=*/false);

        // Determine the new pointer value to store:
        // - right may be null (from null literal → visit_value_expression returns nullptr LLVM val)
        // - right may be an owner ptr (from owner_move_expression)
        llvm::Value* new_ptr = right;
        if (!new_ptr) {
            // null literal case
            new_ptr = llvm::ConstantPointerNull::get(ptr_ty);
        }
        _builder->CreateStore(new_ptr, left);
        _value = left;
        return;
    }

    // ------------------------------------------------------------------
    // Scalar / pointer assignment (existing behaviour)
    // ------------------------------------------------------------------

    // Step 4: For struct copy: emit memcpy or copy constructor call
    // Link rebind from nullable source: emit null-check before store.
    // (The resolver emits warning 0x0072 at compile-time; we add the runtime guard here.)
    if (target_type && type::is_link(target_type)) {
        // Pierce cast_expression to find the real source nullability
        auto rhs_model = expr.right();
        auto rhs_type = rhs_model ? rhs_model->get_type() : nullptr;
        // Also check original type through a cast (upcast Derived→Base wraps nullable ptr)
        if (auto cast_e = std::dynamic_pointer_cast<cast_expression>(rhs_model)) {
            auto inner_type = cast_e->sub_expr()->get_type();
            if (inner_type && type::is_nullable_indirection(inner_type)) {
                rhs_type = inner_type;
            }
        }
        if (rhs_type && type::is_nullable_indirection(rhs_type)) {
            auto* fatal = get_or_declare_fatal_null_function("__k_fatal_null_assignation");
            emit_null_check(right, fatal, "link_rebind");
        }
    }

    // Step 5: For primitives/pointers: emit store instruction
    _value = right;
    _value = _builder->CreateStore(_value, left);
    _value = left;

}

//
// Arithmetic assignation expression
//

void symbol_resolver::visit_arithmetic_assignation_expression(arithmetic_assignation_expression &expr) {
    visit_assignation_expression(expr);
}

void type_reference_resolver::visit_arithmetic_assignation_expression(arithmetic_assignation_expression &expr) {
    visit_assignation_expression(expr);

    // If an operator overload was resolved, no further checks needed.
    if (expr.has_operator_overload()) return;

    auto left = expr.left();
    auto right = expr.right();

    auto left_type = left->get_type();
    auto ref_target_type = std::dynamic_pointer_cast<reference_type>(left_type);
    auto target_type = ref_target_type->get_subtype();
    if(type::is_pointer(target_type)) {
        throw_error(0x0011, expr.first_lexeme(),
            "Arithmetic-assignment operators (e.g. '+=', '-=') cannot be applied to pointer types: "
            "the target has type '{}'; pointer arithmetic is not supported",
            {target_type ? target_type->to_string() : "?"});
    }
}

//
// Addition assignment expression (+=)
//

void implementation_generator::visit_addition_assignation_expression(additition_assignation_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        throw_internal_error(0x001C, expr.first_lexeme(),
            "Internal error: '+=' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    auto left_ref_type = std::dynamic_pointer_cast<reference_type>(expr.left()->get_type());
    auto left_type = left_ref_type->get_subtype();
    auto llvm_type = _context->get_llvm_type(left_type);

    auto left_val = _builder->CreateLoad(llvm_type, left);
    if(type::is_prim_integer(left_type)) {
        _value = _builder->CreateAdd(left_val, right);
    } else if(type::is_prim_float(left_type)) {
        _value = _builder->CreateFAdd(left_val, right);
    } else {
        // TODO: Support other types
    }

    // Store the value, return the left ref
    _value = _builder->CreateStore(_value, left);
    _value = left;
}

//
// Substraction assignment expression (-=)
//

void implementation_generator::visit_substraction_assignation_expression(substraction_assignation_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        throw_internal_error(0x001D, expr.first_lexeme(),
            "Internal error: '-=' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    auto left_ref_type = std::dynamic_pointer_cast<reference_type>(expr.left()->get_type());
    auto left_type = left_ref_type->get_subtype();
    auto llvm_type = _context->get_llvm_type(left_type);

    auto left_val = _builder->CreateLoad(llvm_type, left);
    if(type::is_prim_integer(left_type)) {
        _value = _builder->CreateSub(left_val, right);
    } else if(type::is_prim_float(left_type)) {
        _value = _builder->CreateFSub(left_val, right);
    } else {
        // TODO: Support other types
    }

    // Store the value, return the left ref
    _value = _builder->CreateStore(_value, left);
    _value = left;
}

//
// Multiplication assignment expression (*=)
//

void implementation_generator::visit_multiplication_assignation_expression(multiplication_assignation_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        throw_internal_error(0x001E, expr.first_lexeme(),
            "Internal error: '*=' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    auto left_ref_type = std::dynamic_pointer_cast<reference_type>(expr.left()->get_type());
    auto left_type = left_ref_type->get_subtype();
    auto llvm_type = _context->get_llvm_type(left_type);

    auto left_val = _builder->CreateLoad(llvm_type, left);
    if(type::is_prim_integer(left_type)) {
        _value = _builder->CreateMul(left_val, right);
    } else if(type::is_prim_float(left_type)) {
        _value = _builder->CreateFMul(left_val, right);
    } else {
        // TODO: Support other types
    }

    // Store the value, return the left ref
    _value = _builder->CreateStore(_value, left);
    _value = left;
}

//
// Division assignment expression (/=)
//

void implementation_generator::visit_division_assignation_expression(division_assignation_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        throw_internal_error(0x001F, expr.first_lexeme(),
            "Internal error: '/=' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    auto left_ref_type = std::dynamic_pointer_cast<reference_type>(expr.left()->get_type());
    auto left_type = type::remove_const(left_ref_type->get_subtype());
    auto llvm_type = _context->get_llvm_type(left_type);

    auto left_val = _builder->CreateLoad(llvm_type, left);
    if(auto prim = std::dynamic_pointer_cast<primitive_type>(left_type)) {
        if(prim->is_integer()) {
            if(prim->is_unsigned()) {
                _value = _builder->CreateUDiv(left_val, right);
            } else {
                _value = _builder->CreateSDiv(left_val, right);
            }
        } else if(prim->is_float()) {
            _value = _builder->CreateFDiv(left_val, right);
        }
    } else {
        // TODO: Support other types
    }

    // Store the value, return the left ref
    _value = _builder->CreateStore(_value, left);
    _value = left;
}

//
// Modulo assignment expression (%=)
//

void implementation_generator::visit_modulo_assignation_expression(modulo_assignation_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        throw_internal_error(0x0020, expr.first_lexeme(),
            "Internal error: '%=' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    auto left_ref_type = std::dynamic_pointer_cast<reference_type>(expr.left()->get_type());
    auto left_type = type::remove_const(left_ref_type->get_subtype());
    auto llvm_type = _context->get_llvm_type(left_type);

    auto left_val = _builder->CreateLoad(llvm_type, left);
    if(auto prim = std::dynamic_pointer_cast<primitive_type>(left_type)) {
        if(prim->is_integer()) {
            if(prim->is_unsigned()) {
                _value = _builder->CreateURem(left_val, right);
            } else {
                _value = _builder->CreateSRem(left_val, right);
            }
        } else if(prim->is_float()) {
            _value = _builder->CreateFRem(left_val, right);
        }
    } else {
        // TODO: Support other types
    }

    // Store the value, return the left ref
    _value = _builder->CreateStore(_value, left);
    _value = left;
}

//
// Bitwise and assignment expression
//

void implementation_generator::visit_bitwise_and_assignation_expression(bitwise_and_assignation_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        throw_internal_error(0x0021, expr.first_lexeme(),
            "Internal error: '&=' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    auto left_ref_type = std::dynamic_pointer_cast<reference_type>(expr.left()->get_type());
    auto left_type = type::remove_const(left_ref_type->get_subtype());
    auto llvm_type = _context->get_llvm_type(left_type);

    auto left_val = _builder->CreateLoad(llvm_type, left);
    if(auto prim = std::dynamic_pointer_cast<primitive_type>(left_type)) {
        if(prim->is_integer()) {
            _value = _builder->CreateAnd(left_val, right);
        } else if(prim->is_float()) {
            throw_error(0x0018, expr.first_lexeme(),
                "Bitwise AND-assignment ('&=') cannot be applied to floating-point values: "
                "bitwise operations are only defined for integer types; "
                "the operand has type '{}'",
                {prim->to_string()});
        }
    } else {
        // TODO: Support other types
    }

    // Store the value, return the left ref
    _value = _builder->CreateStore(_value, left);
    _value = left;
}

//
// Bitwise or assignment expression
//

void implementation_generator::visit_bitwise_or_assignation_expression(bitwise_or_assignation_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        throw_internal_error(0x0022, expr.first_lexeme(),
            "Internal error: '|=' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    auto left_ref_type = std::dynamic_pointer_cast<reference_type>(expr.left()->get_type());
    auto left_type = type::remove_const(left_ref_type->get_subtype());
    auto llvm_type = _context->get_llvm_type(left_type);

    auto left_val = _builder->CreateLoad(llvm_type, left);
    if(auto prim = std::dynamic_pointer_cast<primitive_type>(left_type)) {
        if(prim->is_integer()) {
            _value = _builder->CreateOr(left_val, right);
        } else if(prim->is_float()) {
            throw_error(0x001A, expr.first_lexeme(),
                "Bitwise OR-assignment ('|=') cannot be applied to floating-point values: "
                "bitwise operations are only defined for integer types; "
                "the operand has type '{}'",
                {prim->to_string()});
        }
    } else {
        // TODO: Support other types
    }

    // Store the value, return the left ref
    _value = _builder->CreateStore(_value, left);
    _value = left;
}

//
// Bitwise xor assignment expression
//

void implementation_generator::visit_bitwise_xor_assignation_expression(bitwise_xor_assignation_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        throw_internal_error(0x0023, expr.first_lexeme(),
            "Internal error: '^=' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    auto left_ref_type = std::dynamic_pointer_cast<reference_type>(expr.left()->get_type());
    auto left_type = type::remove_const(left_ref_type->get_subtype());
    auto llvm_type = _context->get_llvm_type(left_type);

    auto left_val = _builder->CreateLoad(llvm_type, left);
    if(auto prim = std::dynamic_pointer_cast<primitive_type>(left_type)) {
        if(prim->is_integer()) {
            _value = _builder->CreateXor(left_val, right);
        } else if(prim->is_float()) {
            throw_error(0x001C, expr.first_lexeme(),
                "Bitwise XOR-assignment ('^=') cannot be applied to floating-point values: "
                "bitwise operations are only defined for integer types; "
                "the operand has type '{}'",
                {prim->to_string()});
        }
    } else {
        // TODO: Support other types
    }

    // Store the value, return the left ref
    _value = _builder->CreateStore(_value, left);
    _value = left;
}

//
// Left shift assignment expression
//

void implementation_generator::visit_left_shift_assignation_expression(left_shift_assignation_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        throw_internal_error(0x0024, expr.first_lexeme(),
            "Internal error: '<<=' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    auto left_ref_type = std::dynamic_pointer_cast<reference_type>(expr.left()->get_type());
    auto left_type = type::remove_const(left_ref_type->get_subtype());
    auto llvm_type = _context->get_llvm_type(left_type);

    auto left_val = _builder->CreateLoad(llvm_type, left);
    if(auto prim = std::dynamic_pointer_cast<primitive_type>(left_type)) {
        if(prim->is_integer()) {
            // TODO may it poison when overflow ?
            _value = _builder->CreateShl(left_val, right);
        } else if(prim->is_float()) {
            throw_error(0x001E, expr.first_lexeme(),
                "Left shift-assignment ('<<=') cannot be applied to floating-point values: "
                "shift operations are only defined for integer types; "
                "the operand has type '{}'",
                {prim->to_string()});
        }
    } else {
        // TODO: Support other types
    }

    // Store the value, return the left ref
    _value = _builder->CreateStore(_value, left);
    _value = left;
}

//
// Right shift assignment expression
//

void implementation_generator::visit_right_shift_assignation_expression(right_shift_assignation_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        throw_internal_error(0x0025, expr.first_lexeme(),
            "Internal error: '>>=' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    auto left_ref_type = std::dynamic_pointer_cast<reference_type>(expr.left()->get_type());
    auto left_type = type::remove_const(left_ref_type->get_subtype());
    auto llvm_type = _context->get_llvm_type(left_type);

    auto left_val = _builder->CreateLoad(llvm_type, left);
    if(auto prim = std::dynamic_pointer_cast<primitive_type>(left_type)) {
        if(prim->is_integer()) {
            if(prim->is_unsigned()) {
                // TODO may it poison when overflow ?
                _value = _builder->CreateLShr(left_val, right);
            } else {
                // TODO may it poison when overflow ?
                _value = _builder->CreateAShr(left_val, right);
            }
        } else if(prim->is_float()) {
            throw_error(0x0020, expr.first_lexeme(),
                "Right shift-assignment ('>>=') cannot be applied to floating-point values: "
                "shift operations are only defined for integer types; "
                "the operand has type '{}'",
                {prim->to_string()});
        }
    } else {
        // TODO: Support other types
    }

    // Store the value, return the left ref
    _value = _builder->CreateStore(_value, left);
    _value = left;
}

//
// Arithmetic unary expression
//

void type_reference_resolver::visit_arithmetic_unary_expression(arithmetic_unary_expression& expr) {
    visit_unary_expression(expr);

    auto& sub = expr.sub_expr();
    auto type = sub->get_type();

    if(type::is_pointer(type)) {
        throw_error(0x0021, expr.first_lexeme(),
            "Unary arithmetic operators cannot be applied to pointer types: "
            "the operand has type '{}'; only numeric primitive types are supported",
            {type ? type->to_string() : "?"});
    }

    auto orig_type = type;
    if(type::is_reference(type)) {
        // Dereference type, if needed
        type = type->get_subtype();
    }

    // ── Operator overload for aggregate types ──
    bool is_const_operand = type::is_const(type);
    auto check_type = type::remove_const(type);
    if(type::is_struct(check_type)) {
        auto st_type = std::dynamic_pointer_cast<struct_type>(check_type);
        if (st_type) {
            auto agg = st_type->get_struct();
            if (agg) {
                auto op_func = resolve_unary_operator_overload(expr, agg, sub, is_const_operand);
                if (op_func) {
                    expr.set_operator_func(op_func);
                    if (op_func->has_return_type()) {
                        expr.set_type(op_func->get_return_type());
                    } else {
                        expr.set_type(type);
                    }
                    if (op_func->is_member()) {
                        auto di = compute_operator_dispatch_info(op_func, orig_type);
                        expr.set_operator_dispatch_info(std::move(di));
                    } else {
                        virtual_dispatch_info di;
                        di.kind = virtual_dispatch_info::dispatch_kind::DIRECT;
                        expr.set_operator_dispatch_info(std::move(di));
                    }
                    return;
                }
            }
        }
    }

    if(!type::is_primitive(type)) {
        throw_error(0x0022, expr.first_lexeme(),
            "Unary arithmetic operators are not supported for non-primitive types: "
            "the operand has type '{}'; only numeric primitive types are supported",
            {type ? type->to_string() : "?"});
    }

    expr.set_type(type);
}

//
// Prefix increment expression (++expr)
//

void type_reference_resolver::visit_prefix_increment_expression(prefix_increment_expression& expr) {
    visit_unary_expression(expr);

    auto& sub = expr.sub_expr();
    auto type = sub->get_type();

    if(!type::is_reference(type)) {
        throw_error(0x002F, expr.first_lexeme(),
            "The operand of prefix '++' must be an assignable lvalue (a variable or dereferenced pointer), "
            "but got a non-reference type '{}'",
            {type ? type->to_string() : "?"});
    }

    auto ref_type = std::dynamic_pointer_cast<reference_type>(type);
    auto value_type = ref_type->get_subtype();
    if(type::is_reference(value_type)) {
        value_type = value_type->get_subtype();
    }

    if(type::is_const(value_type)) {
        throw_error(0x0083, expr.first_lexeme(),
            "Cannot apply prefix '++' to a const variable of type '{}'",
            {value_type ? value_type->to_string() : "?"});
    }

    // ── Operator overload for aggregate types ──
    auto check_type = type::remove_const(value_type);
    if(type::is_struct(check_type)) {
        auto st_type = std::dynamic_pointer_cast<struct_type>(check_type);
        if (st_type) {
            auto agg = st_type->get_struct();
            if (agg) {
                auto op_func = resolve_unary_operator_overload(expr, agg, sub);
                if (op_func) {
                    expr.set_operator_func(op_func);
                    if (op_func->has_return_type()) {
                        expr.set_type(op_func->get_return_type());
                    } else {
                        expr.set_type(ref_type);
                    }
                    if (op_func->is_member()) {
                        auto di = compute_operator_dispatch_info(op_func, type);
                        expr.set_operator_dispatch_info(std::move(di));
                    } else {
                        virtual_dispatch_info di;
                        di.kind = virtual_dispatch_info::dispatch_kind::DIRECT;
                        expr.set_operator_dispatch_info(std::move(di));
                    }
                    return;
                }
            }
        }
    }

    if(!type::is_primitive(value_type)) {
        throw_error(0x0030, expr.first_lexeme(),
            "Prefix '++' requires a numeric primitive operand, but got type '{}'",
            {value_type ? value_type->to_string() : "?"});
    }
    if(type::is_prim_bool(value_type)) {
        throw_error(0x0031, expr.first_lexeme(),
            "Prefix '++' cannot be applied to a boolean operand");
    }

    // Prefix increment returns a reference to the (now updated) variable
    expr.set_type(ref_type);
}

void implementation_generator::visit_prefix_increment_expression(prefix_increment_expression& expr) {
    if (generate_unary_operator_overload(expr)) return;

    // Get the pointer (alloca) to the variable
    auto ptr = process_unary_expression(expr);
    if(!ptr) {
        _value = nullptr;
        return;
    }

    auto sub_type = expr.sub_expr()->get_type();
    // sub_type is a reference; get the underlying value type
    auto ref_type = std::dynamic_pointer_cast<reference_type>(sub_type);
    auto value_type = ref_type->get_subtype();
    if(type::is_reference(value_type)) {
        // ref-to-ref: load once more to get the actual pointer
        ptr = _builder->CreateLoad(_context->get_llvm_type(value_type), ptr);
        value_type = std::dynamic_pointer_cast<reference_type>(value_type)->get_subtype();
    }

    auto llvm_type = _context->get_llvm_type(value_type);
    auto old_val = _builder->CreateLoad(llvm_type, ptr);

    llvm::Value* new_val = nullptr;
    if(auto prim = std::dynamic_pointer_cast<primitive_type>(value_type)) {
        if(prim->is_integer()) {
            new_val = _builder->CreateAdd(old_val, llvm::ConstantInt::get(llvm_type, 1));
        } else if(prim->is_float()) {
            new_val = _builder->CreateFAdd(old_val, llvm::ConstantFP::get(llvm_type, 1.0));
        }
    }
    if(new_val) {
        _builder->CreateStore(new_val, ptr);
    }
    // Return the pointer (reference) to the updated variable
    _value = ptr;
}


//
// Prefix decrement expression (--expr)
//

void type_reference_resolver::visit_prefix_decrement_expression(prefix_decrement_expression& expr) {
    visit_unary_expression(expr);

    auto& sub = expr.sub_expr();
    auto type = sub->get_type();

    if(!type::is_reference(type)) {
        throw_error(0x0032, expr.first_lexeme(),
            "The operand of prefix '--' must be an assignable lvalue (a variable or dereferenced pointer), "
            "but got a non-reference type '{}'",
            {type ? type->to_string() : "?"});
    }

    auto ref_type = std::dynamic_pointer_cast<reference_type>(type);
    auto value_type = ref_type->get_subtype();
    if(type::is_reference(value_type)) {
        value_type = value_type->get_subtype();
    }

    if(type::is_const(value_type)) {
        throw_error(0x0084, expr.first_lexeme(),
            "Cannot apply prefix '--' to a const variable of type '{}'",
            {value_type ? value_type->to_string() : "?"});
    }

    // ── Operator overload for aggregate types ──
    auto check_type = type::remove_const(value_type);
    if(type::is_struct(check_type)) {
        auto st_type = std::dynamic_pointer_cast<struct_type>(check_type);
        if (st_type) {
            auto agg = st_type->get_struct();
            if (agg) {
                auto op_func = resolve_unary_operator_overload(expr, agg, sub);
                if (op_func) {
                    expr.set_operator_func(op_func);
                    if (op_func->has_return_type()) {
                        expr.set_type(op_func->get_return_type());
                    } else {
                        expr.set_type(ref_type);
                    }
                    if (op_func->is_member()) {
                        auto di = compute_operator_dispatch_info(op_func, type);
                        expr.set_operator_dispatch_info(std::move(di));
                    } else {
                        virtual_dispatch_info di;
                        di.kind = virtual_dispatch_info::dispatch_kind::DIRECT;
                        expr.set_operator_dispatch_info(std::move(di));
                    }
                    return;
                }
            }
        }
    }

    if(!type::is_primitive(value_type)) {
        throw_error(0x0033, expr.first_lexeme(),
            "Prefix '--' requires a numeric primitive operand, but got type '{}'",
            {value_type ? value_type->to_string() : "?"});
    }
    if(type::is_prim_bool(value_type)) {
        throw_error(0x0034, expr.first_lexeme(),
            "Prefix '--' cannot be applied to a boolean operand");
    }

    // Prefix decrement returns a reference to the (now updated) variable
    expr.set_type(ref_type);
}

void implementation_generator::visit_prefix_decrement_expression(prefix_decrement_expression& expr) {
    if (generate_unary_operator_overload(expr)) return;

    auto ptr = process_unary_expression(expr);
    if(!ptr) {
        _value = nullptr;
        return;
    }

    auto sub_type = expr.sub_expr()->get_type();
    auto ref_type = std::dynamic_pointer_cast<reference_type>(sub_type);
    auto value_type = ref_type->get_subtype();
    if(type::is_reference(value_type)) {
        ptr = _builder->CreateLoad(_context->get_llvm_type(value_type), ptr);
        value_type = std::dynamic_pointer_cast<reference_type>(value_type)->get_subtype();
    }

    auto llvm_type = _context->get_llvm_type(value_type);
    auto old_val = _builder->CreateLoad(llvm_type, ptr);

    llvm::Value* new_val = nullptr;
    if(auto prim = std::dynamic_pointer_cast<primitive_type>(value_type)) {
        if(prim->is_integer()) {
            new_val = _builder->CreateSub(old_val, llvm::ConstantInt::get(llvm_type, 1));
        } else if(prim->is_float()) {
            new_val = _builder->CreateFSub(old_val, llvm::ConstantFP::get(llvm_type, 1.0));
        }
    }
    if(new_val) {
        _builder->CreateStore(new_val, ptr);
    }
    // Return the pointer (reference) to the updated variable
    _value = ptr;
}


//
// Postfix increment expression (expr++)
//

void type_reference_resolver::visit_postfix_increment_expression(postfix_increment_expression& expr) {
    visit_unary_expression(expr);

    auto& sub = expr.sub_expr();
    auto type = sub->get_type();

    if(!type::is_reference(type)) {
        throw_error(0x0035, expr.first_lexeme(),
            "The operand of postfix '++' must be an assignable lvalue (a variable or dereferenced pointer), "
            "but got a non-reference type '{}'",
            {type ? type->to_string() : "?"});
    }

    auto ref_type = std::dynamic_pointer_cast<reference_type>(type);
    auto value_type = ref_type->get_subtype();
    if(type::is_reference(value_type)) {
        value_type = value_type->get_subtype();
    }

    if(type::is_const(value_type)) {
        throw_error(0x0085, expr.first_lexeme(),
            "Cannot apply postfix '++' to a const variable of type '{}'",
            {value_type ? value_type->to_string() : "?"});
    }

    // ── Operator overload for aggregate types ──
    auto check_type = type::remove_const(value_type);
    if(type::is_struct(check_type)) {
        auto st_type = std::dynamic_pointer_cast<struct_type>(check_type);
        if (st_type) {
            auto agg = st_type->get_struct();
            if (agg) {
                auto op_func = resolve_unary_operator_overload(expr, agg, sub);
                if (op_func) {
                    expr.set_operator_func(op_func);
                    if (op_func->has_return_type()) {
                        expr.set_type(op_func->get_return_type());
                    } else {
                        expr.set_type(value_type);
                    }
                    if (op_func->is_member()) {
                        auto di = compute_operator_dispatch_info(op_func, type);
                        expr.set_operator_dispatch_info(std::move(di));
                    } else {
                        virtual_dispatch_info di;
                        di.kind = virtual_dispatch_info::dispatch_kind::DIRECT;
                        expr.set_operator_dispatch_info(std::move(di));
                    }
                    return;
                }
            }
        }
    }

    if(!type::is_primitive(value_type)) {
        throw_error(0x0036, expr.first_lexeme(),
            "Postfix '++' requires a numeric primitive operand, but got type '{}'",
            {value_type ? value_type->to_string() : "?"});
    }
    if(type::is_prim_bool(value_type)) {
        throw_error(0x0037, expr.first_lexeme(),
            "Postfix '++' cannot be applied to a boolean operand");
    }

    // Postfix increment returns the old value (not a reference)
    expr.set_type(value_type);
}

void implementation_generator::visit_postfix_increment_expression(postfix_increment_expression& expr) {
    if (generate_unary_operator_overload(expr)) return;

    auto ptr = process_unary_expression(expr);
    if(!ptr) {
        _value = nullptr;
        return;
    }

    auto sub_type = expr.sub_expr()->get_type();
    auto ref_type = std::dynamic_pointer_cast<reference_type>(sub_type);
    auto value_type = ref_type->get_subtype();
    if(type::is_reference(value_type)) {
        ptr = _builder->CreateLoad(_context->get_llvm_type(value_type), ptr);
        value_type = std::dynamic_pointer_cast<reference_type>(value_type)->get_subtype();
    }

    auto llvm_type = _context->get_llvm_type(value_type);
    // Save old value before increment
    auto old_val = _builder->CreateLoad(llvm_type, ptr);

    llvm::Value* new_val = nullptr;
    if(auto prim = std::dynamic_pointer_cast<primitive_type>(value_type)) {
        if(prim->is_integer()) {
            new_val = _builder->CreateAdd(old_val, llvm::ConstantInt::get(llvm_type, 1));
        } else if(prim->is_float()) {
            new_val = _builder->CreateFAdd(old_val, llvm::ConstantFP::get(llvm_type, 1.0));
        }
    }
    if(new_val) {
        _builder->CreateStore(new_val, ptr);
    }
    // Return the old (pre-increment) value
    _value = old_val;
}


//
// Postfix decrement expression (expr--)
//

void type_reference_resolver::visit_postfix_decrement_expression(postfix_decrement_expression& expr) {
    visit_unary_expression(expr);

    auto& sub = expr.sub_expr();
    auto type = sub->get_type();

    if(!type::is_reference(type)) {
        throw_error(0x0038, expr.first_lexeme(),
            "The operand of postfix '--' must be an assignable lvalue (a variable or dereferenced pointer), "
            "but got a non-reference type '{}'",
            {type ? type->to_string() : "?"});
    }

    auto ref_type = std::dynamic_pointer_cast<reference_type>(type);
    auto value_type = ref_type->get_subtype();
    if(type::is_reference(value_type)) {
        value_type = value_type->get_subtype();
    }

    if(type::is_const(value_type)) {
        throw_error(0x0086, expr.first_lexeme(),
            "Cannot apply postfix '--' to a const variable of type '{}'",
            {value_type ? value_type->to_string() : "?"});
    }

    // ── Operator overload for aggregate types ──
    auto check_type = type::remove_const(value_type);
    if(type::is_struct(check_type)) {
        auto st_type = std::dynamic_pointer_cast<struct_type>(check_type);
        if (st_type) {
            auto agg = st_type->get_struct();
            if (agg) {
                auto op_func = resolve_unary_operator_overload(expr, agg, sub);
                if (op_func) {
                    expr.set_operator_func(op_func);
                    if (op_func->has_return_type()) {
                        expr.set_type(op_func->get_return_type());
                    } else {
                        expr.set_type(value_type);
                    }
                    if (op_func->is_member()) {
                        auto di = compute_operator_dispatch_info(op_func, type);
                        expr.set_operator_dispatch_info(std::move(di));
                    } else {
                        virtual_dispatch_info di;
                        di.kind = virtual_dispatch_info::dispatch_kind::DIRECT;
                        expr.set_operator_dispatch_info(std::move(di));
                    }
                    return;
                }
            }
        }
    }

    if(!type::is_primitive(value_type)) {
        throw_error(0x0039, expr.first_lexeme(),
            "Postfix '--' requires a numeric primitive operand, but got type '{}'",
            {value_type ? value_type->to_string() : "?"});
    }
    if(type::is_prim_bool(value_type)) {
        throw_error(0x003A, expr.first_lexeme(),
            "Postfix '--' cannot be applied to a boolean operand");
    }

    // Postfix decrement returns the old value (not a reference)
    expr.set_type(value_type);
}

void implementation_generator::visit_postfix_decrement_expression(postfix_decrement_expression& expr) {
    if (generate_unary_operator_overload(expr)) return;

    auto ptr = process_unary_expression(expr);
    if(!ptr) {
        _value = nullptr;
        return;
    }

    auto sub_type = expr.sub_expr()->get_type();
    auto ref_type = std::dynamic_pointer_cast<reference_type>(sub_type);
    auto value_type = ref_type->get_subtype();
    if(type::is_reference(value_type)) {
        ptr = _builder->CreateLoad(_context->get_llvm_type(value_type), ptr);
        value_type = std::dynamic_pointer_cast<reference_type>(value_type)->get_subtype();
    }

    auto llvm_type = _context->get_llvm_type(value_type);
    // Save old value before decrement
    auto old_val = _builder->CreateLoad(llvm_type, ptr);

    llvm::Value* new_val = nullptr;
    if(auto prim = std::dynamic_pointer_cast<primitive_type>(value_type)) {
        if(prim->is_integer()) {
            new_val = _builder->CreateSub(old_val, llvm::ConstantInt::get(llvm_type, 1));
        } else if(prim->is_float()) {
            new_val = _builder->CreateFSub(old_val, llvm::ConstantFP::get(llvm_type, 1.0));
        }
    }
    if(new_val) {
        _builder->CreateStore(new_val, ptr);
    }
    // Return the old (pre-decrement) value
    _value = old_val;
}


//
// Unary plus expression
//

void implementation_generator::visit_unary_plus_expression(unary_plus_expression& expr) {
    if (generate_unary_operator_overload(expr)) return;

    auto val = process_unary_expression(expr);
    if(!val) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    auto type = expr.sub_expr()->get_type();
    if(type::is_reference(type)) {
        type = type->get_subtype();
        // If reference, dereference it.
        val = _builder->CreateLoad(_context->get_llvm_type(type), val);
    }

    if(type::is_primitive(type)) {
        // When primitive, return the value itself
        _value = val;
    } else {
        // TODO: Support other types
    }
}

//
// Unary minus expression
//

void implementation_generator::visit_unary_minus_expression(unary_minus_expression& expr) {
    if (generate_unary_operator_overload(expr)) return;

    auto val = process_unary_expression(expr);
    if(!val) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    auto type = expr.sub_expr()->get_type();
    if(type::is_reference(type)) {
        type = type->get_subtype();
        // If reference, dereference it.
        val = _builder->CreateLoad(_context->get_llvm_type(type), val);
    }

    if(auto prim = std::dynamic_pointer_cast<primitive_type>(type::remove_const(type))) {
        // When primitive, return the value itself
        if(prim->is_integer_or_bool()) {
            // TODO may it poison when overflow ?
            //_value = _builder->CreateSub(_builder->getIntN(prim->type_size(), 0), val);
            _value = _builder->CreateNeg(val);
        } else if(prim->is_float()) {
            _value = _builder->CreateFNeg(val);
        } else {
            // TODO: Support other types
        };
    } else {
        // TODO: Support other types
    }
}

//
// Bitwise not expression
//

void implementation_generator::visit_bitwise_not_expression(bitwise_not_expression& expr) {
    if (generate_unary_operator_overload(expr)) return;

    auto val = process_unary_expression(expr);
    if(!val) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    auto type = expr.sub_expr()->get_type();
    if(type::is_reference(type)) {
        type = type->get_subtype();
        // If reference, dereference it.
        val = _builder->CreateLoad(_context->get_llvm_type(type), val);
    }

    if(auto prim = std::dynamic_pointer_cast<primitive_type>(type::remove_const(type))) {
        // When primitive, return the value itself
        if(prim->is_integer_or_bool()) {
            _value = _builder->CreateNot(val);
        } else if(prim->is_float()) {
            throw_error(0x0023, expr.first_lexeme(),
                "Bitwise NOT ('~') cannot be applied to floating-point values: "
                "bitwise operations are only defined for integer and boolean types; "
                "the operand has type '{}'",
                {prim->to_string()});
        } else {
            // TODO: Support other types
        };
    } else {
        // TODO: Support other types
    }
}

//
// Logical binary expression
//

/**
 * Resolve a logical binary expression (&& or ||): validate both operands are boolean
 * or can be converted to boolean.
 *
 * Steps:
 *   1. Resolve both operands.
 *   2. For struct types: look for operator overload.
 *   3. For non-bool types: insert implicit cast to bool.
 *   4. Set result type to bool.
 */
void type_reference_resolver::visit_logical_binary_expression(logical_binary_expression& expr) {
    // Step 1: Resolve both operands
    visit_binary_expression(expr);

    auto left = expr.left();
    auto right = expr.right();

    auto left_type = left->get_type();
    auto right_type = right->get_type();

    // Helper: is the type boolean-compatible? (primitive or indirection/null → bool via adapt_type)
    auto is_bool_compatible = [](const std::shared_ptr<type>& t) {
        if (type::is_primitive(t)) return true;
        if (type::is_pointer(t) || type::is_link(t) || type::is_view(t)
            || type::is_owner(t) || type::is_null(t)) return true;
        // Also accept ref<indirection>
        if (type::is_reference(t)) {
            auto inner = t->get_subtype();
            if (type::is_pointer(inner) || type::is_link(inner) ||
                type::is_view(inner) || type::is_owner(inner)) return true;
        }
        return false;
    };

    // Step 2: For struct types: look for operator overload
    // ── Operator overload for aggregate types (before reference stripping) ──
    {
        auto check_left = left_type;
        if (type::is_reference(check_left)) {
            check_left = check_left->get_subtype();
        }
        bool is_const_left = type::is_const(check_left);
        check_left = type::remove_const(check_left);
        if (type::is_struct(check_left)) {
            auto st_type = std::dynamic_pointer_cast<struct_type>(check_left);
            if (st_type) {
                auto agg = st_type->get_struct();
                if (agg) {
                    auto [op_func, adapted_right] = resolve_binary_operator_overload(expr, agg, left, right, is_const_left);
                    if (op_func) {
                        expr.set_operator_func(op_func);
                        // Apply the adapted right operand (implicit cast if needed)
                        if (adapted_right && adapted_right != right) {
                            expr.assign_right(adapted_right);
                        }
                        if (op_func->has_return_type()) {
                            expr.set_type(op_func->get_return_type());
                        } else {
                            expr.set_type(_context->from_type(primitive_type::BOOL));
                        }
                        if (op_func->is_member()) {
                            auto di = compute_operator_dispatch_info(op_func, left_type);
                            expr.set_operator_dispatch_info(std::move(di));
                        } else {
                            virtual_dispatch_info di;
                            di.kind = virtual_dispatch_info::dispatch_kind::DIRECT;
                            expr.set_operator_dispatch_info(std::move(di));
                        }
                        return;
                    }
                }
            }
        }
    }

    if(type::is_reference(left_type)) {
        // For ref<indirection>, don't unwrap — adapt_type handles ref<indirection>→bool.
        auto inner = left_type->get_subtype();
        if (type::is_pointer(inner) || type::is_link(inner) || type::is_view(inner)
            || type::is_owner(inner)) {
            // Leave as-is; adapt_type will handle ref<indirection>→bool.
        } else {
            left = adapt_reference_load_value(left);
            expr.assign_left(left);
            left_type = left_type->get_subtype();
        }
    } else if(type::is_drain(left_type)) {
        left = adapt_reference_load_value(left);
        expr.assign_left(left);
        left_type = left_type->get_subtype();
    }

    if(type::is_reference(right_type)) {
        auto inner = right_type->get_subtype();
        if (type::is_pointer(inner) || type::is_link(inner) || type::is_view(inner)
            || type::is_owner(inner)) {
            // Leave as-is; adapt_type will handle ref<indirection>→bool.
        } else {
            right = adapt_reference_load_value(right);
            expr.assign_right(right);
            right_type = right_type->get_subtype();
        }
    } else if(type::is_drain(right_type)) {
        right = adapt_reference_load_value(right);
        expr.assign_right(right);
        right_type = right_type->get_subtype();
    }

    if(!is_bool_compatible(left->get_type()) || !is_bool_compatible(right->get_type())) {
        throw_error(0x0024, expr.first_lexeme(),
            "Logical operators ('&&', '||') are not supported for non-primitive types: "
            "operands must be of a primitive type or indirection type convertible to boolean, "
            "but found '{}' and '{}'",
            {left->get_type() ? left->get_type()->to_string() : "?",
             right->get_type() ? right->get_type()->to_string() : "?"});
    }

    auto bool_type = _context->from_type(primitive_type::BOOL);

    // Step 3: For non-bool types: insert implicit cast to bool
    auto cast_left = adapt_type(left, bool_type);
    if(!cast_left) {
        throw_error(0x0025, expr.first_lexeme(),
            "The left operand of a logical operator cannot be implicitly converted to bool: "
            "the operand has type '{}'; logical operators require boolean-compatible operands",
            {left->get_type() ? left->get_type()->to_string() : "?"});
    } else if(cast_left != left ) {
        // Casted, assign casted expression instead of source.
        expr.assign_left(cast_left);
    } else {
        // Compatible type, no need to cast.
    }

    // Step 4: Set result type to bool
    auto cast_right = adapt_type(right, bool_type);
    if(!cast_right) {
        throw_error(0x0026, expr.first_lexeme(),
            "The right operand of a logical operator cannot be implicitly converted to bool: "
            "the operand has type '{}'; logical operators require boolean-compatible operands",
            {right->get_type() ? right->get_type()->to_string() : "?"});
    } else if(cast_right != right ) {
        // Casted, assign casted expression instead of source.
        expr.assign_right(cast_right);
    } else {
        // Compatible type, no need to cast.
    }

    // For primitive type, logical is always returning boolean
    expr.set_type(_context->from_type(primitive_type::BOOL));
}

//
// Logical and expression (&&)
//

void implementation_generator::visit_logical_and_expression(logical_and_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    // ── Short-circuit evaluation (and-then) ─────────────────────────────────
    // Evaluate left first; if false, skip right entirely and yield false.

    // 1. Evaluate left operand
    _value = nullptr;
    expr.left()->accept(*this);
    llvm::Value* left = _value;
    if (!left) {
        _value = nullptr;
        return;
    }

    // If left operand is a reference, dereference it.
    if (type::is_reference(expr.left()->get_type()) || type::is_drain(expr.left()->get_type())) {
        llvm::Type* ty = _context->get_llvm_type(expr.left()->get_type());
        left = _builder->CreateLoad(ty, left);
    }

    // 2. Create basic blocks for short-circuit
    llvm::Function* func = _builder->GetInsertBlock()->getParent();
    llvm::BasicBlock* entry_bb = _builder->GetInsertBlock();
    llvm::BasicBlock* rhs_bb   = llvm::BasicBlock::Create(**_context, "land-rhs", func);
    llvm::BasicBlock* merge_bb = llvm::BasicBlock::Create(**_context, "land-merge");

    // 3. Branch: if left is true, evaluate right; otherwise skip to merge with false
    _builder->CreateCondBr(left, rhs_bb, merge_bb);

    // 4. Evaluate right operand (only reached if left was true)
    _builder->SetInsertPoint(rhs_bb);
    _value = nullptr;
    expr.right()->accept(*this);
    llvm::Value* right = _value;
    if (!right) {
        _value = nullptr;
        return;
    }
    // Capture the actual block after visiting right (it may have created sub-blocks)
    llvm::BasicBlock* rhs_end_bb = _builder->GetInsertBlock();
    _builder->CreateBr(merge_bb);

    // 5. Merge block with PHI
    func->insert(func->end(), merge_bb);
    _builder->SetInsertPoint(merge_bb);
    llvm::PHINode* phi = _builder->CreatePHI(_builder->getInt1Ty(), 2, "land");
    phi->addIncoming(_builder->getFalse(), entry_bb);  // left was false → result is false
    phi->addIncoming(right, rhs_end_bb);               // left was true → result is right

    _value = phi;
}


void implementation_generator::visit_logical_or_expression(logical_or_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    // ── Short-circuit evaluation (or-else) ─────────────────────────────────
    // Evaluate left first; if true, skip right entirely and yield true.

    // 1. Evaluate left operand
    _value = nullptr;
    expr.left()->accept(*this);
    llvm::Value* left = _value;
    if (!left) {
        _value = nullptr;
        return;
    }

    // If left operand is a reference, dereference it.
    if (type::is_reference(expr.left()->get_type()) || type::is_drain(expr.left()->get_type())) {
        llvm::Type* ty = _context->get_llvm_type(expr.left()->get_type());
        left = _builder->CreateLoad(ty, left);
    }

    // 2. Create basic blocks for short-circuit
    llvm::Function* func = _builder->GetInsertBlock()->getParent();
    llvm::BasicBlock* entry_bb = _builder->GetInsertBlock();
    llvm::BasicBlock* rhs_bb   = llvm::BasicBlock::Create(**_context, "lor-rhs", func);
    llvm::BasicBlock* merge_bb = llvm::BasicBlock::Create(**_context, "lor-merge");

    // 3. Branch: if left is true, skip to merge with true; otherwise evaluate right
    _builder->CreateCondBr(left, merge_bb, rhs_bb);

    // 4. Evaluate right operand (only reached if left was false)
    _builder->SetInsertPoint(rhs_bb);
    _value = nullptr;
    expr.right()->accept(*this);
    llvm::Value* right = _value;
    if (!right) {
        _value = nullptr;
        return;
    }
    // Capture the actual block after visiting right (it may have created sub-blocks)
    llvm::BasicBlock* rhs_end_bb = _builder->GetInsertBlock();
    _builder->CreateBr(merge_bb);

    // 5. Merge block with PHI
    func->insert(func->end(), merge_bb);
    _builder->SetInsertPoint(merge_bb);
    llvm::PHINode* phi = _builder->CreatePHI(_builder->getInt1Ty(), 2, "lor");
    phi->addIncoming(_builder->getTrue(), entry_bb);   // left was true → result is true
    phi->addIncoming(right, rhs_end_bb);               // left was false → result is right

    _value = phi;
}

//
// Logical not expression (!)
//

/**
 * Resolve a logical not expression (!expr): validate operand is boolean or convertible.
 *
 * Steps:
 *   1. Resolve the operand.
 *   2. For struct types: look for operator! overload.
 *   3. For non-bool types: insert implicit cast to bool.
 *   4. Set result type to bool.
 */
void type_reference_resolver::visit_logical_not_expression(logical_not_expression& expr) {
    // Step 1: Resolve the operand
    visit_unary_expression(expr);

    auto& sub = expr.sub_expr();
    auto type = sub->get_type();

    if(type::is_reference(type)) {
        // For ref<indirection>, don't unwrap — adapt_type handles it.
        auto inner = type->get_subtype();
        if (!type::is_pointer(inner) && !type::is_link(inner) &&
            !type::is_view(inner) && !type::is_owner(inner)) {
            type = type->get_subtype();
        }
    }

    // Step 2: For struct types: look for operator! overload
    // ── Operator overload for aggregate types ──
    {
        auto check_type = type::remove_const(type);
        bool is_const_operand = type::is_const(type);
        if (type::is_struct(check_type)) {
            auto st_type = std::dynamic_pointer_cast<struct_type>(check_type);
            if (st_type) {
                auto agg = st_type->get_struct();
                if (agg) {
                    auto op_func = resolve_unary_operator_overload(expr, agg, sub, is_const_operand);
                    if (op_func) {
                        expr.set_operator_func(op_func);
                        if (op_func->has_return_type()) {
                            expr.set_type(op_func->get_return_type());
                        } else {
                            expr.set_type(_context->from_type(primitive_type::BOOL));
                        }
                        auto orig_type = sub->get_type();
                        if (op_func->is_member()) {
                            auto di = compute_operator_dispatch_info(op_func, orig_type);
                            expr.set_operator_dispatch_info(std::move(di));
                        } else {
                            virtual_dispatch_info di;
                            di.kind = virtual_dispatch_info::dispatch_kind::DIRECT;
                            expr.set_operator_dispatch_info(std::move(di));
                        }
                        return;
                    }
                }
            }
        }
    }

    // Check bool-compatibility: primitive, indirection, or null.
    auto is_bool_compatible = [](const std::shared_ptr<k::model::type>& t) {
        if (type::is_primitive(t)) return true;
        if (type::is_pointer(t) || type::is_link(t) || type::is_view(t)
            || type::is_owner(t) || type::is_null(t)) return true;
        // Also accept ref<indirection>
        if (type::is_reference(t)) {
            auto inner = t->get_subtype();
            if (type::is_pointer(inner) || type::is_link(inner) ||
                type::is_view(inner) || type::is_owner(inner)) return true;
        }
        return false;
    };
    if(!is_bool_compatible(type)) {
        throw_error(0x0029, expr.first_lexeme(),
            "Logical NOT ('!') is not supported for non-primitive types: "
            "the operand has type '{}'; only primitive or indirection types convertible to boolean are supported",
            {type ? type->to_string() : "?"});
    }

    // Step 3: For non-bool types: insert implicit cast to bool
    static auto bool_type = _context->from_type(primitive_type::BOOL);
    auto cast = adapt_type(sub, bool_type);
    if(!cast) {
        throw_error(0x002A, expr.first_lexeme(),
            "The operand of logical NOT ('!') cannot be implicitly converted to bool: "
            "the operand has type '{}'; logical NOT requires a boolean-compatible operand",
            {type ? type->to_string() : "?"});
    } else if(cast != sub ) {
        // Casted, assign casted expression instead of source.
        expr.assign(cast);
    } else {
        // Compatible type, no need to cast.
    }

    // Step 4: Set result type to bool
    // For primitive type, logical is always returning boolean
    expr.set_type(bool_type);
}

void implementation_generator::visit_logical_not_expression(logical_not_expression& expr) {
    if (generate_unary_operator_overload(expr)) return;

    auto value = process_unary_expression(expr);

    if(!value) {
        // TODO throw exception ?
        _value = nullptr;
        return;
    }

    auto& sub = expr.sub_expr();
    auto type = sub->get_type();

    if(type::is_reference(type)) {
        // Dereference
        type = type->get_subtype();
        value = _builder->CreateLoad(_context->get_llvm_type(type), value);
    }

    if(!type::is_primitive(type)) {
        throw_internal_error(0x0028, expr.first_lexeme(),
            "Internal error: '!' operator has a non-primitive operand during code generation; "
            "this should have been rejected during type resolution");
    }

    _value = _builder->CreateNot(value);
}

//
// Comparison expressions
//
/**
 * Resolve a comparison expression (==, !=, <, >, <=, >=): validate operand types,
 * check for operator overloads on struct types.
 *
 * Steps:
 *   1. Resolve both operands.
 *   2. If either operand is a struct type: look for operator overload.
 *   3. For pointers: validate pointed-type compatibility.
 *   4. For primitives: adapt types for comparison.
 *   5. Set result type to bool.
 */
void type_reference_resolver::visit_comparison_expression(comparison_expression& expr) {
    // Step 1: Resolve both operands
    visit_binary_expression(expr);

    auto& left = expr.left();
    auto& right = expr.right();

    auto left_type = left->get_type();
    auto right_type = right->get_type();

    // ── Helper: is this type address-comparable? ─────────────────────────────
    // Pointer, link, pinned, owner, and the null literal type can all participate
    // in address equality/inequality comparisons.
    auto is_address_comparable = [](const std::shared_ptr<type>& t) -> bool {
        return type::is_pointer(t) || type::is_link(t) || type::is_view(t)
            || type::is_owner(t)   || type::is_null(t);
    };

    // Step 2: If either operand is a struct type: look for operator overload
    // Strip one level of reference to get the underlying type.
    // For ref<ptr<T>>, ref<link<T>>, ref<pin<T>>, ref<owner<T>>:
    //   load the stored pointer/link/pin/owner so we can compare addresses.
    auto unwrap_ref_indirection = [&](std::shared_ptr<expression>& operand,
                                      std::shared_ptr<type>& operand_type) {
        if (type::is_reference(operand_type)) {
            auto inner = std::dynamic_pointer_cast<reference_type>(operand_type)->get_subtype();
            if (is_address_comparable(inner)) {
                operand = adapt_reference_load_value(operand);
                operand_type = inner;
            }
        }
    };

    // Step 3: For pointers: validate pointed-type compatibility
    unwrap_ref_indirection(left, left_type);
    unwrap_ref_indirection(right, right_type);

    // ── Address comparison path ──────────────────────────────────────────────
    if (is_address_comparable(left_type) || is_address_comparable(right_type)) {
        // Both sides must be address-comparable.
        if (!is_address_comparable(left_type) || !is_address_comparable(right_type)) {
            throw_error(0x002C, expr.first_lexeme(),
                "Address comparison requires both operands to be indirections "
                "(pointer, link, pinned, owner) or null, but found '{}' and '{}'",
                {left_type ? left_type->to_string() : "?",
                 right_type ? right_type->to_string() : "?"});
        }
        // Only == and != are valid for address comparison (not <, >, <=, >=).
        if (!dynamic_cast<equal_expression*>(&expr) &&
            !dynamic_cast<different_expression*>(&expr)) {
            throw_error(0x002E, expr.first_lexeme(),
                "Only '==' and '!=' are valid for address comparison between indirections; "
                "relational operators (<, >, <=, >=) are not supported for types '{}' and '{}'",
                {left_type ? left_type->to_string() : "?",
                 right_type ? right_type->to_string() : "?"});
        }
        // Update the expression operands if unwrapped
        expr.assign_left(left);
        expr.assign_right(right);

        static auto bool_type = _context->from_type(primitive_type::BOOL);
        expr.set_type(bool_type);
        return;
    }
    // ─────────────────────────────────────────────────────────────────────────

    // ── Operator overload for aggregate types (before reference stripping) ──
    {
        auto check_left = left_type;
        if (type::is_reference(check_left)) {
            check_left = check_left->get_subtype();
        }
        bool is_const_left = type::is_const(check_left);
        check_left = type::remove_const(check_left);
        if (type::is_struct(check_left)) {
            auto st_type = std::dynamic_pointer_cast<struct_type>(check_left);
            if (st_type) {
                auto agg = st_type->get_struct();
                if (agg) {
                    auto [op_func, adapted_right] = resolve_binary_operator_overload(expr, agg, left, right, is_const_left);
                    if (op_func) {
                        expr.set_operator_func(op_func);
                        // Apply the adapted right operand (implicit cast if needed)
                        if (adapted_right && adapted_right != right) {
                            expr.assign_right(adapted_right);
                        }
                        if (op_func->has_return_type()) {
                            expr.set_type(op_func->get_return_type());
                        } else {
                            static auto bool_type_cached = _context->from_type(primitive_type::BOOL);
                            expr.set_type(bool_type_cached);
                        }
                        if (op_func->is_member()) {
                            auto di = compute_operator_dispatch_info(op_func, left_type);
                            expr.set_operator_dispatch_info(std::move(di));
                        } else {
                            virtual_dispatch_info di;
                            di.kind = virtual_dispatch_info::dispatch_kind::DIRECT;
                            expr.set_operator_dispatch_info(std::move(di));
                        }
                        return;
                    }
                }
            }
        }
    }

    // ── Primitive comparison (existing path) ─────────────────────────────────
    if(type::is_reference(left_type)) {
        left = adapt_reference_load_value(left);
        expr.assign_left(left);
        left_type = left_type->get_subtype();
    } else if(type::is_drain(left_type)) {
        left = adapt_reference_load_value(left);
        expr.assign_left(left);
        left_type = left_type->get_subtype();
    }
    left_type = type::remove_const(left_type);

    if(type::is_reference(right_type)) {
        right = adapt_reference_load_value(right);
        expr.assign_right(right);
        right_type = right_type->get_subtype();
    } else if(type::is_drain(right_type)) {
        right = adapt_reference_load_value(right);
        expr.assign_right(right);
        right_type = right_type->get_subtype();
    }
    right_type = type::remove_const(right_type);

    // ── Enum comparison: convert enum operands to their underlying primitive type ──
    {
        auto left_enum = std::dynamic_pointer_cast<enum_type>(left_type);
        auto right_enum = std::dynamic_pointer_cast<enum_type>(right_type);
        if (left_enum || right_enum) {
            // Determine the common underlying primitive type for comparison
            auto left_underlying = left_enum ? left_enum->get_underlying_type()
                                             : std::dynamic_pointer_cast<primitive_type>(left_type);
            auto right_underlying = right_enum ? right_enum->get_underlying_type()
                                               : std::dynamic_pointer_cast<primitive_type>(right_type);
            if (!left_underlying || !right_underlying) {
                throw_error(0x0085, expr.first_lexeme(),
                    "Cannot compare enum with non-primitive type: "
                    "operands have types '{}' and '{}'",
                    {left_type ? left_type->to_string() : "?",
                     right_type ? right_type->to_string() : "?"});
            }
            // Adapt both operands to a common type (use right's underlying if both are enum, left otherwise)
            auto common_type = left_underlying;
            if (right_underlying->type_size() > left_underlying->type_size()) {
                common_type = right_underlying;
            }
            left = adapt_type(left, common_type);
            right = adapt_type(right, common_type);
            if (left) expr.assign_left(left);
            if (right) expr.assign_right(right);
            left_type = common_type;
            right_type = common_type;
        }
    }

    if(!type::is_primitive(left_type) || !type::is_primitive(right_type)) {
        throw_error(0x002C, expr.first_lexeme(),
            "Comparison operators are not supported for non-primitive types: "
            "operands must be primitive types, but found '{}' and '{}'",
            {left_type ? left_type->to_string() : "?",
             right_type ? right_type->to_string() : "?"});
    }

    auto left_prim_type = std::dynamic_pointer_cast<primitive_type>(type::remove_const(left_type));
    auto right_prim_type = std::dynamic_pointer_cast<primitive_type>(type::remove_const(right_type));

    auto adapted_left = left;
    auto adapted_right = right;

    if(left_prim_type->is_boolean() && !right_prim_type->is_boolean()) {
        // Adapt right to boolean
        adapted_right = adapt_type(right, left_prim_type);
    } else if(!left_prim_type->is_boolean() && right_prim_type->is_boolean()) {
        // Adapt left to boolean
        adapted_left = adapt_type(left, right_prim_type);
    }  else {
        // Adapt right to left type
        // TODO rework to promote to biggest integer of both
        adapted_right = adapt_type(right, left_prim_type);
    }

    // Step 4: For primitives: adapt types for comparison
    if(!adapted_left || !adapted_right) {
        throw_error(0x002D, expr.first_lexeme(),
            "Incompatible types in comparison: "
            "cannot align operand types '{}' and '{}' for comparison; "
            "use an explicit cast to make the types comparable",
            {left_type ? left_type->to_string() : "?",
             right_type ? right_type->to_string() : "?"});
    }

    if(adapted_left!=left) {
        expr.assign_left(adapted_left);
    }
    if(adapted_right!=right) {
        expr.assign_right(adapted_right);
    }

    // Step 5: Set result type to bool
    // For primitive type, logical is always returning boolean
    static auto bool_type = _context->from_type(primitive_type::BOOL);
    expr.set_type(bool_type);
}

//
// Equal expression (==)
//

void implementation_generator::visit_equal_expression(equal_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        throw_internal_error(0x0029, expr.first_lexeme(),
            "Internal error: '==' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    auto left_type = expr.left()->get_type();
    auto right_type = expr.right()->get_type();

    // ── Address comparison for indirection types ─────────────────────────────
    auto is_addr = [](const std::shared_ptr<type>& t) {
        return type::is_pointer(t) || type::is_link(t) || type::is_view(t)
            || type::is_owner(t)   || type::is_null(t);
    };
    if (is_addr(left_type) || is_addr(right_type)) {
        auto* ptr_ty = llvm::PointerType::get(_builder->getContext(), 0);
        if (left->getType() != ptr_ty) left = _builder->CreateBitCast(left, ptr_ty);
        if (right->getType() != ptr_ty) right = _builder->CreateBitCast(right, ptr_ty);
        _value = _builder->CreateICmpEQ(left, right);
        return;
    }
    // ─────────────────────────────────────────────────────────────────────────

    // If operands are references or drains, dereference them.
    llvm::Type* type = _context->get_llvm_type(expr.get_type());
    if(type::is_reference(left_type) || type::is_drain(left_type)) {
        left = _builder->CreateLoad(type, left);
    }
    if(type::is_reference(right_type) || type::is_drain(right_type)) {
        right = _builder->CreateLoad(type, right);
    }

    if(!type::is_primitive(left_type) || !type::is_primitive(right_type)) {
        throw_internal_error(0x002A, expr.first_lexeme(),
            "Internal error: '==' operator has a non-primitive operand during code generation; "
            "this should have been rejected during type resolution");
    }

    // For primitives, operand types are supposed to be aligned
    static auto bool_type = _context->from_type(primitive_type::BOOL);
    auto prim_left = (type::is_reference(left_type) || type::is_drain(left_type)) ? left_type->get_subtype() : left_type;
    auto prim = std::dynamic_pointer_cast<primitive_type>(type::remove_const(prim_left));

    if(prim->is_integer_or_bool()) {
        _value = _builder->CreateICmpEQ(left, right);
    } else if(prim->is_float()) {
        _value = _builder->CreateFCmpOEQ(left, right);
    } else {
        // TODO support for other types
    }
}

//
// Different expression (!=)
//

void implementation_generator::visit_different_expression(different_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        throw_internal_error(0x002B, expr.first_lexeme(),
            "Internal error: '!=' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    auto left_type = expr.left()->get_type();
    auto right_type = expr.right()->get_type();

    // ── Address comparison for indirection types ─────────────────────────────
    auto is_addr = [](const std::shared_ptr<type>& t) {
        return type::is_pointer(t) || type::is_link(t) || type::is_view(t)
            || type::is_owner(t)   || type::is_null(t);
    };
    if (is_addr(left_type) || is_addr(right_type)) {
        auto* ptr_ty = llvm::PointerType::get(_builder->getContext(), 0);
        if (left->getType() != ptr_ty) left = _builder->CreateBitCast(left, ptr_ty);
        if (right->getType() != ptr_ty) right = _builder->CreateBitCast(right, ptr_ty);
        _value = _builder->CreateICmpNE(left, right);
        return;
    }
    // ─────────────────────────────────────────────────────────────────────────

    // If operands are references or drains, dereference them.
    llvm::Type* type = _context->get_llvm_type(expr.get_type());
    if(type::is_reference(left_type) || type::is_drain(left_type)) {
        left = _builder->CreateLoad(type, left);
    }
    if(type::is_reference(right_type) || type::is_drain(right_type)) {
        right = _builder->CreateLoad(type, right);
    }

    if(!type::is_primitive(left_type) || !type::is_primitive(right_type)) {
        throw_internal_error(0x002C, expr.first_lexeme(),
            "Internal error: '!=' operator has non-primitive operand during code generation; "
            "this should have been rejected during type resolution");
    }

    // For primitives, operand types are supposed to be aligned
    static auto bool_type = _context->from_type(primitive_type::BOOL);
    auto prim_left_ne = (type::is_reference(left_type) || type::is_drain(left_type)) ? left_type->get_subtype() : left_type;
    auto prim = std::dynamic_pointer_cast<primitive_type>(type::remove_const(prim_left_ne));

    if(prim->is_integer_or_bool()) {
        _value = _builder->CreateICmpNE(left, right);
    } else if(prim->is_float()) {
        _value = _builder->CreateFCmpONE(left, right);
    } else {
        // TODO support for other types
    }
}

//
// Lesser than expression (<)
//

void implementation_generator::visit_lesser_expression(lesser_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        throw_internal_error(0x002D, expr.first_lexeme(),
            "Internal error: '<' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    // If operands are references, dereference them.
    llvm::Type* type = _context->get_llvm_type(expr.get_type());
    if(type::is_reference(expr.left()->get_type()) || type::is_drain(expr.left()->get_type())) {
        left = _builder->CreateLoad(type, left);
    }
    if(type::is_reference(expr.right()->get_type())) {
        right = _builder->CreateLoad(type, right);
    }

    if(!type::is_primitive(expr.left()->get_type()) || !type::is_primitive(expr.right()->get_type())) {
        throw_internal_error(0x002E, expr.first_lexeme(),
            "Internal error: '<' operator has non-primitive operand during code generation; "
            "this should have been rejected during type resolution");
    }

    // For primitives, operand types are supposed to be aligned
    static auto bool_type = _context->from_type(primitive_type::BOOL);
    auto prim_left_lt = (type::is_reference(expr.left()->get_type()) || type::is_drain(expr.left()->get_type())) ? expr.left()->get_type()->get_subtype() : expr.left()->get_type();
    auto prim = std::dynamic_pointer_cast<primitive_type>(type::remove_const(prim_left_lt));

    if(prim->is_integer_or_bool()) {
        if(prim->is_unsigned()) {
            _value = _builder->CreateICmpULT(left, right);
        } else {
            _value = _builder->CreateICmpSLT(left, right);
        }
    } else if(prim->is_float()) {
        _value = _builder->CreateFCmpOLT(left, right);
    } else {
        // TODO support for other types
    }
}

//
// Greater than expression (>)
//

void implementation_generator::visit_greater_expression(greater_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        throw_internal_error(0x002F, expr.first_lexeme(),
            "Internal error: '>' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    // If operands are references, dereference them.
    llvm::Type* type = _context->get_llvm_type(expr.get_type());
    if(type::is_reference(expr.left()->get_type()) || type::is_drain(expr.left()->get_type())) {
        left = _builder->CreateLoad(type, left);
    }
    if(type::is_reference(expr.right()->get_type())) {
        right = _builder->CreateLoad(type, right);
    }

    if(!type::is_primitive(expr.left()->get_type()) || !type::is_primitive(expr.right()->get_type())) {
        throw_internal_error(0x0030, expr.first_lexeme(),
            "Internal error: '>' operator has non-primitive operand during code generation; "
            "this should have been rejected during type resolution");
    }

    // For primitives, operand types are supposed to be aligned
    static auto bool_type = _context->from_type(primitive_type::BOOL);
    auto prim_left_gt = (type::is_reference(expr.left()->get_type()) || type::is_drain(expr.left()->get_type())) ? expr.left()->get_type()->get_subtype() : expr.left()->get_type();
    auto prim = std::dynamic_pointer_cast<primitive_type>(type::remove_const(prim_left_gt));

    if(prim->is_integer_or_bool()) {
        if(prim->is_unsigned()) {
            _value = _builder->CreateICmpUGT(left, right);
        } else {
            _value = _builder->CreateICmpSGT(left, right);
        }
    } else if(prim->is_float()) {
        _value = _builder->CreateFCmpOGT(left, right);
    } else {
        // TODO support for other types
    }
}

//
// Lesser than or equal expression (<=)
//

void implementation_generator::visit_lesser_equal_expression(lesser_equal_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        throw_internal_error(0x0031, expr.first_lexeme(),
            "Internal error: '<=' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    // If operands are references, dereference them.
    llvm::Type* type = _context->get_llvm_type(expr.get_type());
    if(type::is_reference(expr.left()->get_type()) || type::is_drain(expr.left()->get_type())) {
        left = _builder->CreateLoad(type, left);
    }
    if(type::is_reference(expr.right()->get_type())) {
        right = _builder->CreateLoad(type, right);
    }

    if(!type::is_primitive(expr.left()->get_type()) || !type::is_primitive(expr.right()->get_type())) {
        throw_internal_error(0x0032, expr.first_lexeme(),
            "Internal error: '<=' operator has non-primitive operand during code generation; "
            "this should have been rejected during type resolution");
    }

    // For primitives, operand types are supposed to be aligned
    static auto bool_type = _context->from_type(primitive_type::BOOL);
    auto prim_left_le = (type::is_reference(expr.left()->get_type()) || type::is_drain(expr.left()->get_type())) ? expr.left()->get_type()->get_subtype() : expr.left()->get_type();
    auto prim = std::dynamic_pointer_cast<primitive_type>(type::remove_const(prim_left_le));

    if(prim->is_integer_or_bool()) {
        if(prim->is_unsigned()) {
            _value = _builder->CreateICmpULE(left, right);
        } else {
            _value = _builder->CreateICmpSLE(left, right);
        }
    } else if(prim->is_float()) {
        _value = _builder->CreateFCmpOLE(left, right);
    } else {
        // TODO support for other types
    }
}

//
// Greater than or equal expression (>=)
//

void implementation_generator::visit_greater_equal_expression(greater_equal_expression& expr) {
    if (generate_binary_operator_overload(expr)) return;

    auto [left, right] = process_binary_expression(expr);
    if(!left || !right) {
        throw_internal_error(0x0034, expr.first_lexeme(),
            "Internal error: '>=' expression produced a null left or right LLVM value; "
            "this indicates a code-generation bug in an operand expression");
    }

    // If operands are references, dereference them.
    llvm::Type* type = _context->get_llvm_type(expr.get_type());
    if(type::is_reference(expr.left()->get_type()) || type::is_drain(expr.left()->get_type())) {
        left = _builder->CreateLoad(type, left);
    }
    if(type::is_reference(expr.right()->get_type())) {
        right = _builder->CreateLoad(type, right);
    }

    if(!type::is_primitive(expr.left()->get_type()) || !type::is_primitive(expr.right()->get_type())) {
        throw_internal_error(0x0034, expr.first_lexeme(),
            "Internal error: '>=' operator has non-primitive operand during code generation; "
            "this should have been rejected during type resolution");
    }

    // For primitives, operand types are supposed to be aligned
    static auto bool_type = _context->from_type(primitive_type::BOOL);
    auto prim_left_ge = (type::is_reference(expr.left()->get_type()) || type::is_drain(expr.left()->get_type())) ? expr.left()->get_type()->get_subtype() : expr.left()->get_type();
    auto prim = std::dynamic_pointer_cast<primitive_type>(type::remove_const(prim_left_ge));

    if(prim->is_integer_or_bool()) {
        if(prim->is_unsigned()) {
            _value = _builder->CreateICmpUGE(left, right);
        } else {
            _value = _builder->CreateICmpSGE(left, right);
        }
    } else if(prim->is_float()) {
        _value = _builder->CreateFCmpOGE(left, right);
    } else {
        // TODO support for other types
    }
}

} // namespace k::model::gen
