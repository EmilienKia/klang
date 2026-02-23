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

#include "logger.hpp"

#include <fmt/core.h>
#include <fmt/format.h>
#include <fmt/args.h>

namespace k::log {

std::string format_diagnostic_message(const diagnostic& diag) {
    if (diag.args.empty()) {
        return diag.message;
    }
    fmt::dynamic_format_arg_store<fmt::format_context> store;
    for (const auto& arg : diag.args) {
        store.push_back(arg);
    }
    try {
        return fmt::vformat(diag.message, store);
    } catch (...) {
        return diag.message;
    }
}

} // namespace k::log
