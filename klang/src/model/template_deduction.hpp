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

#ifndef KLANG_TEMPLATE_DEDUCTION_HPP
#define KLANG_TEMPLATE_DEDUCTION_HPP

#include "template.hpp"
#include "type.hpp"

#include <memory>
#include <string>
#include <vector>

namespace k::model {

class parameter;

/**
 * Result of template argument deduction from call-site argument types.
 */
struct deduction_result {
    /** True if deduction succeeded for all template parameters. */
    bool success = false;

    /** Deduced template arguments (one per template parameter, in order). */
    std::vector<template_argument> deduced_args;

    /** Human-readable reason for failure (empty on success). */
    std::string failure_reason;
};

/**
 * Attempt to deduce template arguments from call-site argument types and/or target return type.
 *
 * Uses exact matching only — no implicit conversions.
 * Supports:
 *   - Direct type parameter deduction (param type is T → arg type becomes T)
 *   - Wrapper deduction (param type is T* → arg type int* deduces T=int)
 *   - Pack deduction (pack expansion param collects remaining arg types)
 *   - Consistency checking (same param deduced to different types → failure)
 *   - Return-type / target-type contextual deduction (deduces un-deduced params from expected return type)
 *
 * @param ti                   Template info of the candidate function.
 * @param params               The function's parameter list (to detect pack expansions).
 * @param arg_types            Types of the actual call-site argument expressions.
 * @param explicit_args        Explicit template arguments provided at the call site (if any).
 * @param tpl_return_type      Declared return type of the template function (if any).
 * @param expected_target_type Expected target / return type from the surrounding context (if any).
 * @return                     Deduction result with success/failure and deduced arguments.
 */
deduction_result deduce_template_arguments(
    const tpl_info& ti,
    const std::vector<std::shared_ptr<parameter>>& params,
    const std::vector<std::shared_ptr<type>>& arg_types,
    const std::vector<template_argument>& explicit_args = {},
    const std::shared_ptr<type>& tpl_return_type = nullptr,
    const std::shared_ptr<type>& expected_target_type = nullptr);

class aggregate;
class constructor;

/**
 * A viable candidate for Class Template Argument Deduction (CTAD).
 */
struct ctad_candidate {
    /** The constructor candidate that matched (nullptr for synthesized guides). */
    std::shared_ptr<constructor> ctor;

    /** Deduced concrete template arguments for the aggregate. */
    std::vector<template_argument> deduced_args;

    /** True if this candidate was generated from the implicit copy/move deduction guide. */
    bool is_copy_guide = false;

    /** True if this candidate was generated from default construction. */
    bool is_default_guide = false;
};

/**
 * Result of CTAD candidate collection and deduction.
 */
struct ctad_deduction_result {
    /** True if at least one viable candidate was deduced. */
    bool success = false;

    /** Viable CTAD candidates. */
    std::vector<ctad_candidate> candidates;

    /** Human-readable reason for failure if no candidate was viable. */
    std::string failure_reason;
};

/**
 * Perform Class Template Argument Deduction (CTAD) for an aggregate template
 * against a list of call-site argument types.
 *
 * Considers:
 *   - Explicit constructors defined on the template aggregate.
 *   - The implicit copy/move deduction guide: S(const S<...>&) -> S<...>
 *   - Default construction if arg_types is empty and all params have defaults.
 *
 * @param tpl_agg   The template aggregate to deduce arguments for.
 * @param arg_types Types of the constructor arguments at the construction site.
 * @return          CTAD result containing all viable deduction candidates.
 */
ctad_deduction_result deduce_aggregate_ctad_candidates(
    const aggregate& tpl_agg,
    const std::vector<std::shared_ptr<type>>& arg_types);

} // namespace k::model

#endif // KLANG_TEMPLATE_DEDUCTION_HPP

