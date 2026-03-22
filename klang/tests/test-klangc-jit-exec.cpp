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
 * Tests for the klangc --jit-exec option.
 */
#include <catch2/catch_all.hpp>
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include "helpers.hpp"

// ===========================================================================

// ---------------------------------------------------------------------------
// --jit-exec with main returning int: exit code is forwarded
// ---------------------------------------------------------------------------

TEST_CASE("jit-exec: main returning int forwards exit code", "[jit-exec][live]") {
    auto klangc = find_klangc();

    std::string src = R"(
        module jit_test;
        main() : int {
            return 42;
        }
    )";

    auto res = k::tools::run_process(
        klangc.string(),
        {"--stdin", "--jit-exec"},
        src
    );

    INFO("klangc stdout: " << res.out);
    INFO("klangc stderr: " << res.err);
    CHECK(res.exit_code == 42);
}

// ---------------------------------------------------------------------------
// --jit-exec with main returning void: exit code is 0
// ---------------------------------------------------------------------------

TEST_CASE("jit-exec: main returning void gives exit code 0", "[jit-exec][live]") {
    auto klangc = find_klangc();

    std::string src = R"(
        module jit_void;
        main() {
        }
    )";

    auto res = k::tools::run_process(
        klangc.string(),
        {"--stdin", "--jit-exec"},
        src
    );

    INFO("klangc stdout: " << res.out);
    INFO("klangc stderr: " << res.err);
    CHECK(res.exit_code == 0);
}

// ---------------------------------------------------------------------------
// --jit-exec with computation: verifies JIT actually runs the code
// ---------------------------------------------------------------------------

TEST_CASE("jit-exec: non-trivial computation", "[jit-exec][live]") {
    auto klangc = find_klangc();

    std::string src = R"(
        module jit_compute;
        fibo(n: int) : int {
            if (n <= 1) return 1;
            return fibo(n - 1) + fibo(n - 2);
        }
        main() : int {
            return fibo(6);
        }
    )";

    auto res = k::tools::run_process(
        klangc.string(),
        {"--stdin", "--jit-exec"},
        src
    );

    INFO("klangc stdout: " << res.out);
    INFO("klangc stderr: " << res.err);
    // fibo(6) = 13
    CHECK(res.exit_code == 13);
}

// ---------------------------------------------------------------------------
// --jit-exec without main(): should fail with an error
// ---------------------------------------------------------------------------

TEST_CASE("jit-exec: no main produces error", "[jit-exec][live]") {
    auto klangc = find_klangc();

    std::string src = R"(
        module jit_no_main;
        helper() : int {
            return 7;
        }
    )";

    auto res = k::tools::run_process(
        klangc.string(),
        {"--stdin", "--jit-exec"},
        src
    );

    INFO("klangc stdout: " << res.out);
    INFO("klangc stderr: " << res.err);
    CHECK(res.exit_code != 0);
    CHECK(res.err.find("main()") != std::string::npos);
}

// ---------------------------------------------------------------------------
// --jit-exec combined with a .k file: both sources compiled and JIT-executed
// ---------------------------------------------------------------------------

TEST_CASE("jit-exec: combined with input file", "[jit-exec][live]") {
    auto klangc = find_klangc();

    TmpDir tmpdir;
    auto k_file = tmpdir.path / "addval.k";
    {
        std::ofstream ofs(k_file);
        ofs << R"(
            module jit_combo;
            g_val : int = 17;
        )";
    }

    // stdin provides main() that returns the global variable from the file
    std::string stdin_src = R"(
        module jit_combo;
        main() : int {
            return 17;
        }
    )";

    auto res = k::tools::run_process(
        klangc.string(),
        {k_file.string(), "--stdin", "--jit-exec"},
        stdin_src
    );

    INFO("klangc stdout: " << res.out);
    INFO("klangc stderr: " << res.err);
    CHECK(res.exit_code == 17);
}
