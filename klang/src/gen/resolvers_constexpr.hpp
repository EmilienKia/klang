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

#ifndef KLANG_RESOLVERS_CONSTEXPR_HPP
#define KLANG_RESOLVERS_CONSTEXPR_HPP

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../common/common.hpp"

namespace k::parse::ast {
struct expression;
struct type_specifier;
}

namespace k::model {
class element;
class context;
class type;
class unit;
}

namespace k::model::gen {

/**
 * Outcome status of a compile-time constant evaluation attempt for a
 * template value argument.
 *
 *  - OK:    evaluation succeeded, `value` holds the result.
 *  - DEFER: the expression could not be evaluated *at this point*, but this
 *           is not necessarily a user error (e.g. the identifier may be
 *           resolved by a different fallback path, or the surrounding
 *           instantiation attempt may simply not apply). Callers should
 *           silently treat this like the historical "extraction failed"
 *           behaviour (no diagnostic, try other paths / report a generic
 *           "unresolved template argument" error further down the pipeline).
 *  - ERROR: the expression was recognised as a (attempted) constant
 *           expression but evaluation failed for a concrete semantic reason
 *           (division by zero, overflow, type mismatch, unknown enum entry,
 *           unsupported operator, ...). Callers should surface this via
 *           `throw_error(error_code, ..., message, message_args)` using the
 *           reserved template_diag codes ERR_TPL_VALUE_ARG_NOT_CONSTANT /
 *           ERR_TPL_VALUE_ARG_TYPE_MISMATCH.
 */
enum class constexpr_eval_status { OK, DEFER, ERROR };

struct constexpr_eval_result {
    constexpr_eval_status status = constexpr_eval_status::DEFER;
    std::optional<k::value_type> value;
    unsigned int error_code = 0;
    std::string message;
    std::vector<std::string> message_args;

    bool ok() const { return status == constexpr_eval_status::OK && value.has_value(); }
    bool is_error() const { return status == constexpr_eval_status::ERROR; }
};

/**
 * Evaluate a raw (unresolved) AST expression used as a template *value*
 * argument (or value-parameter default) into a concrete k::value_type,
 * as a compile-time constant expression.
 *
 * Supported forms:
 *  - Literals (int, long, float, double, bool, char, string).
 *  - Enum constants: `EnumName::Entry` / `UnionName::Kind::Entry`, resolved
 *    via the enclosing scope of `context_elem` (mirrors the enum-entry
 *    lookup performed for ordinary symbol expressions).
 *  - A bare identifier naming a value template parameter of a template
 *    aggregate/union/function that already encloses `context_elem` and has
 *    been instantiated with concrete arguments (dependent value argument,
 *    e.g. `Array<T, N>` inside `template<typename T, int N> struct S { ... }`).
 *  - Unary `- + ! ~`, binary `+ - * / % & | ^ << >> && || == != < > <= >=`,
 *    ternary `?:` and primitive casts, when their operands are themselves
 *    constant expressions per the above.
 *
 * @param expr           The raw AST value-argument expression (never null).
 * @param context_elem   The model element enclosing the template-argument
 *                        use site (used for enum/dependent-value lookup).
 * @param ctx             Compiler context (used to resolve cast target types).
 * @param expected_type  The declared type of the value parameter being
 *                        substituted (`template_param_descriptor::value_type`),
 *                        or nullptr if unknown/unconstrained. When provided,
 *                        the evaluated result is narrowed/validated against it.
 * @param unit_ptr       Optional owning unit, used only to resolve enum
 *                        constants imported from another module (KDI). May
 *                        be nullptr — cross-module enum constants used as
 *                        template value arguments simply won't resolve then.
 */
constexpr_eval_result evaluate_template_value_arg(
    const k::parse::ast::expression* expr,
    const element& context_elem,
    const std::shared_ptr<context>& ctx,
    const std::shared_ptr<type>& expected_type,
    unit* unit_ptr = nullptr);

/**
 * Attempt to reinterpret a template argument that the parser committed to the
 * *type* grammar production (`identified_type_specifier`) as a compile-time
 * constant *value* expression instead.
 *
 * This handles the syntactic ambiguity inherent to a bare qualified name used
 * as a template argument: `Foo<Bar>` and `Foo<Color::Red>` are indistinguishable
 * from the grammar alone (both are qualified identifiers with no call syntax),
 * so `parse_template_arg_list()` always parses them as `identified_type_specifier`.
 * Once the *declared* kind of the corresponding template parameter is known
 * (`template_param_descriptor::kind`), callers can use this function to recover
 * the value interpretation (enum constant, dependent value parameter, ...)
 * when the parameter is actually a value parameter.
 *
 * Returns DEFER (not ERROR) when `spec` is not a bare qualified name (e.g. it
 * has explicit template arguments, or is a pointer/array/... wrapper) — such
 * forms are never valid value-parameter syntax, so callers should fall back to
 * their pre-existing "invalid argument" diagnostic in that case.
 */
constexpr_eval_result evaluate_template_value_arg_from_type_spec(
    const k::parse::ast::type_specifier* spec,
    const element& context_elem,
    const std::shared_ptr<context>& ctx,
    const std::shared_ptr<type>& expected_type,
    unit* unit_ptr = nullptr);

} // namespace k::model::gen

#endif // KLANG_RESOLVERS_CONSTEXPR_HPP
