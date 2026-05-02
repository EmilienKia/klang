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
 * Attempt to deduce template arguments from call-site argument types.
 *
 * Uses exact matching only — no implicit conversions.
 * Supports:
 *   - Direct type parameter deduction (param type is T → arg type becomes T)
 *   - Wrapper deduction (param type is T* → arg type int* deduces T=int)
 *   - Pack deduction (pack expansion param collects remaining arg types)
 *   - Consistency checking (same param deduced to different types → failure)
 *
 * @param ti           Template info of the candidate function.
 * @param params       The function's parameter list (to detect pack expansions).
 * @param arg_types    Types of the actual call-site argument expressions.
 * @return             Deduction result with success/failure and deduced arguments.
 */
deduction_result deduce_template_arguments(
    const tpl_info& ti,
    const std::vector<std::shared_ptr<parameter>>& params,
    const std::vector<std::shared_ptr<type>>& arg_types);

} // namespace k::model

#endif // KLANG_TEMPLATE_DEDUCTION_HPP

