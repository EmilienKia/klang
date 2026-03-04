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

#ifndef KLANG_HELPERS_HPP
#define KLANG_HELPERS_HPP

#include <memory>
#include <string>

#include "../src/common/logger.hpp"
#include "../src/common/common.hpp"
#include "../src/common/process.hpp"
#include "../src/gen/resolvers.hpp"
#include "../src/gen/generators.hpp"

std::unique_ptr<k::model::gen::jit> gen_jit(std::string_view src, bool dump = false, bool optimize = true);

/**
 * Like gen_jit() but lets k::log::compiler_error (and its subclasses) propagate
 * to the caller instead of catching them.  Use this in tests that verify the
 * compiler throws the expected exception for invalid input:
 *
 *   REQUIRE_THROWS_AS(gen_jit_throws(src), k::model::gen::resolution_error);
 */
std::unique_ptr<k::model::gen::jit> gen_jit_throws(std::string_view src, bool dump = false, bool optimize = true);

k::tools::exec_result build_and_exec(const std::string_view& src);

class test_logger : public k::log::logger {
public:
    /** All diagnostics reported via this logger (for inspection in tests). */
    std::vector<k::log::diagnostic> diagnostics;

    void report(const k::log::diagnostic& diag) override;

    /** True if at least one warning-level diagnostic was reported. */
    bool has_warning() const {
        return std::any_of(diagnostics.begin(), diagnostics.end(), [](const k::log::diagnostic& d){
            return d.level == k::log::diagnostic::severity::warning;
        });
    }

    /** True if at least one error-or-fatal-level diagnostic was reported. */
    bool has_error() const {
        return std::any_of(diagnostics.begin(), diagnostics.end(), [](const k::log::diagnostic& d){
            return d.level == k::log::diagnostic::severity::error
                || d.level == k::log::diagnostic::severity::fatal;
        });
    }

    /** Reset collected diagnostics. */
    void clear() { diagnostics.clear(); }
};



#endif //KLANG_HELPERS_HPP