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

/**
 * Tests for per-code diagnostic suppression (k::compiler::set_ignored_diagnostic_codes /
 * add_ignored_diagnostic_code), which allows silencing specific noisy warning codes
 * (e.g. repeated sign-conversion warnings during test suites) without raising the
 * global log-level threshold.
 *
 * Tests covered:
 *  - A warning code present in the ignore set is not printed.
 *  - A warning code NOT present in the ignore set is still printed.
 *  - An error/fatal diagnostic is NEVER suppressed, even if its numeric code
 *    happens to be present in the ignore set (severity guard).
 *  - End-to-end: compiling real K source that emits WARN_UNUSED_EXPR_RESULT
 *    (0x0164, "bare 'new' expression result discarded") is silenced when its
 *    code is ignored, and still emitted otherwise — exercised through the same
 *    test-helper plumbing (IgnoredDiagCodes / gen_jit) used by other test files.
 */

#include <catch2/catch_all.hpp>
#include <fstream>
#include <sstream>
#include <cstdio>
#include <unistd.h>

#include "helpers.hpp"
#include "../src/errors.hpp"

namespace {

/** Read the full content of a text file into a string. */
std::string read_file_content(const std::filesystem::path& path) {
    std::ifstream in(path);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

/**
 * RAII helper that redirects the process's real stdout (the C FILE* used by
 * fmt::print(), which is what k::compiler::report() writes warnings/errors
 * to — set_log_file() only affects trace/debug messages) to a file, and
 * restores it on destruction.
 */
struct StdoutCapture {
    int saved_fd;
    std::filesystem::path path;

    explicit StdoutCapture(std::filesystem::path p) : path(std::move(p)) {
        fflush(stdout);
        saved_fd = dup(fileno(stdout));
        FILE* f = freopen(path.c_str(), "w", stdout);
        REQUIRE(f != nullptr);
    }

    ~StdoutCapture() {
        fflush(stdout);
        dup2(saved_fd, fileno(stdout));
        ::close(saved_fd);
    }

    std::string content() {
        fflush(stdout);
        return read_file_content(path);
    }
};

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Direct compiler API tests
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("Ignored diagnostic codes silence matching warnings", "[gen][diagnostics][suppression]") {
    TmpDir dir;
    auto cap_path = dir.path / "stdout.txt";

    constexpr unsigned int ignored_code    = 0x0170; // arbitrary, does not need to be a real diag code
    constexpr unsigned int not_ignored_code = 0x0194; // arbitrary, distinct from ignored_code

    auto comp = k::compiler::create(make_pic_target_machine());
    comp->set_ignored_diagnostic_codes({ignored_code});

    REQUIRE(comp->get_ignored_diagnostic_codes().count(ignored_code) == 1);

    std::string out;
    {
        StdoutCapture cap(cap_path);
        comp->diagnostics().warn(ignored_code, "this warning must be silenced");
        comp->diagnostics().warn(not_ignored_code, "this warning must be printed");
        out = cap.content();
    }

    REQUIRE(out.find("this warning must be silenced") == std::string::npos);
    REQUIRE(out.find("this warning must be printed") != std::string::npos);
}

TEST_CASE("Ignored diagnostic codes never suppress errors or fatals", "[gen][diagnostics][suppression]") {
    TmpDir dir;
    auto cap_path = dir.path / "stdout.txt";

    constexpr unsigned int code = 0x0170;

    auto comp = k::compiler::create(make_pic_target_machine());
    comp->set_ignored_diagnostic_codes({code});

    std::string out;
    {
        StdoutCapture cap(cap_path);
        // Same numeric code as an ignored warning, but reported as error/fatal —
        // must NOT be suppressed (severity guard in compiler::report()).
        comp->diagnostics().error(code, "this error must never be silenced");
        out = cap.content();
    }

    REQUIRE(out.find("this error must never be silenced") != std::string::npos);
}

TEST_CASE("add_ignored_diagnostic_code is cumulative with set_ignored_diagnostic_codes", "[gen][diagnostics][suppression]") {
    TmpDir dir;
    auto cap_path = dir.path / "stdout.txt";

    auto comp = k::compiler::create(make_pic_target_machine());
    comp->set_ignored_diagnostic_codes({0x1000});
    comp->add_ignored_diagnostic_code(0x2000);

    REQUIRE(comp->get_ignored_diagnostic_codes().count(0x1000) == 1);
    REQUIRE(comp->get_ignored_diagnostic_codes().count(0x2000) == 1);

    std::string out;
    {
        StdoutCapture cap(cap_path);
        comp->diagnostics().warn(0x1000, "silenced one");
        comp->diagnostics().warn(0x2000, "silenced two");
        comp->diagnostics().warn(0x3000, "kept");
        out = cap.content();
    }

    REQUIRE(out.find("silenced one") == std::string::npos);
    REQUIRE(out.find("silenced two") == std::string::npos);
    REQUIRE(out.find("kept") != std::string::npos);
}


// ─────────────────────────────────────────────────────────────────────────────
// End-to-end test: a real compiler-emitted warning (WARN_UNUSED_EXPR_RESULT)
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("End-to-end: WARN_UNUSED_EXPR_RESULT is suppressed via set_ignored_diagnostic_codes", "[gen][diagnostics][suppression][jit]") {
    constexpr unsigned int code = static_cast<unsigned int>(k::diag::statement_diag::WARN_UNUSED_EXPR_RESULT);

    const std::string_view src = R"SRC(
        module __diag_suppress_e2e__;
        test() : int {
            new int(42);   // triggers WARN_UNUSED_EXPR_RESULT: bare 'new' result discarded
            return 1;
        }
    )SRC";

    auto compile_and_capture_stdout = [&](const IgnoredDiagCodes& ignored) -> std::string {
        TmpDir dir;
        auto cap_path = dir.path / "stdout.txt";
        auto comp = k::compiler::create(make_pic_target_machine());
        auto resolver = std::make_shared<k::path_lookup_file_resolver>();
        resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
        comp->set_file_resolver(resolver);
        if (!ignored.empty()) comp->set_ignored_diagnostic_codes(ignored);
        std::string out;
        {
            StdoutCapture cap(cap_path);
            comp->parse_source("test.k", src, /*optimize=*/false, /*dump=*/false);
            out = cap.content();
        }
        return out;
    };

    SECTION("without suppression: the warning is printed") {
        std::string out = compile_and_capture_stdout({});
        REQUIRE(out.find("immediately discarded") != std::string::npos);
    }

    SECTION("with the code ignored: the warning is silenced") {
        std::string out = compile_and_capture_stdout({code});
        REQUIRE(out.find("immediately discarded") == std::string::npos);
    }
}






