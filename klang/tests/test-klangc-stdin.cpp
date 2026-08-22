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
 * Tests for the klangc --stdin option.
 */
#include <catch2/catch_all.hpp>
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include "helpers.hpp"

// ---------------------------------------------------------------------------
// --stdin only (no input files): compile and produce an executable
// ---------------------------------------------------------------------------

TEST_CASE("stdin: compile from stdin alone produces executable", "[stdin][live]") {
    auto klangc = find_klangc();

    // Create a temp file path for the output executable
    char out_file[] = "/tmp/klang_stdin_test_XXXXXX";
    int fd = ::mkstemp(out_file);
    REQUIRE(fd != -1);
    ::close(fd);

    std::string src = R"(
        module klangc_stdin_01;
        main() : int {
            return 42;
        }
    )";

    auto res = k::tools::run_process(
        klangc.string(),
        {"--stdin", "-o", out_file},
        src
    );

    INFO("klangc stdout: " << res.out);
    INFO("klangc stderr: " << res.err);
    REQUIRE(res.exit_code == 0);
    REQUIRE(std::filesystem::exists(out_file));

    // Run the produced executable (needs libk.so in LD_LIBRARY_PATH)
    ScopedLdLibraryPath ld_scope(find_libk_dir());
    auto run = k::tools::run_process(out_file, {});
    INFO("exec stdout: " << run.out);
    INFO("exec stderr: " << run.err);
    CHECK(run.exit_code == 42);

    std::filesystem::remove(out_file);
}

// ---------------------------------------------------------------------------
// --stdin with -c: compile-only, produce an object file
// ---------------------------------------------------------------------------

TEST_CASE("stdin: --stdin with -c produces object file", "[stdin][live]") {
    auto klangc = find_klangc();

    char out_file[] = "/tmp/klang_stdin_obj_XXXXXX";
    int fd = ::mkstemp(out_file);
    REQUIRE(fd != -1);
    ::close(fd);
    std::string obj_path = std::string(out_file) + ".o";
    std::filesystem::remove(out_file);

    std::string src = R"(
        module klangc_stdin_02;
        foo() : int {
            return 7;
        }
    )";

    auto res = k::tools::run_process(
        klangc.string(),
        {"--stdin", "-c", "-o", obj_path},
        src
    );

    INFO("klangc stdout: " << res.out);
    INFO("klangc stderr: " << res.err);
    REQUIRE(res.exit_code == 0);
    REQUIRE(std::filesystem::exists(obj_path));
    REQUIRE(std::filesystem::file_size(obj_path) > 0);

    std::filesystem::remove(obj_path);
}

// ---------------------------------------------------------------------------
// --stdin combined with a file argument: both sources are compiled together
// ---------------------------------------------------------------------------

TEST_CASE("stdin: --stdin combined with input file", "[stdin][live]") {
    auto klangc = find_klangc();

    // Write a .k file with a helper function
    TmpDir tmpdir;
    auto k_file = tmpdir.path / "helper.k";
    {
        std::ofstream ofs(k_file);
        ofs << R"(
            module klangc_stdin_03;
            helper() : int {
                return 10;
            }
        )";
    }

    char out_file[] = "/tmp/klang_stdin_combo_XXXXXX";
    int fd = ::mkstemp(out_file);
    REQUIRE(fd != -1);
    ::close(fd);

    // The stdin source provides main() which returns a constant.
    // Both files are compiled together as the same module.
    std::string stdin_src = R"(
        module klangc_stdin_03; // <- Same module as helper.k
        main() : int {
            return 10;
        }
    )";

    auto res = k::tools::run_process(
        klangc.string(),
        {k_file.string(), "--stdin", "-o", out_file},
        stdin_src
    );

    INFO("klangc stdout: " << res.out);
    INFO("klangc stderr: " << res.err);
    REQUIRE(res.exit_code == 0);
    REQUIRE(std::filesystem::exists(out_file));

    // Run the produced executable (needs libk.so in LD_LIBRARY_PATH)
    ScopedLdLibraryPath ld_scope(find_libk_dir());
    auto run = k::tools::run_process(out_file, {});
    INFO("exec stdout: " << run.out);
    INFO("exec stderr: " << run.err);
    CHECK(run.exit_code == 10);

    std::filesystem::remove(out_file);
}

// ---------------------------------------------------------------------------
// --stdin with -c and no -o: default output name is "stdin.o"
// ---------------------------------------------------------------------------

TEST_CASE("stdin: --stdin with -c and no -o defaults to stdin.o", "[stdin][live]") {
    auto klangc = find_klangc();

    // Run in a temporary directory to avoid polluting the workspace
    TmpDir tmpdir;

    std::string src = R"(
        module klangc_stdin_05;
        bar() : int {
            return 99;
        }
    )";

    // We need to run klangc from the tmp directory so stdin.o lands there.
    // run_process doesn't support chdir, so use a wrapper via /bin/sh.
    auto res = k::tools::run_process(
        "/bin/sh",
        {"-c", "cd " + tmpdir.path.string() + " && " + klangc.string() + " --stdin -c"},
        src
    );

    INFO("klangc stdout: " << res.out);
    INFO("klangc stderr: " << res.err);
    REQUIRE(res.exit_code == 0);

    auto expected_obj = tmpdir.path / "stdin.o";
    REQUIRE(std::filesystem::exists(expected_obj));
    REQUIRE(std::filesystem::file_size(expected_obj) > 0);
}

// ---------------------------------------------------------------------------
// No --stdin and no input files: should fail with an error
// ---------------------------------------------------------------------------

TEST_CASE("stdin: no --stdin and no files produces error", "[stdin][live]") {
    auto klangc = find_klangc();

    auto res = k::tools::run_process(
        klangc.string(),
        {},
        std::nullopt
    );

    // The compiler should report an error and exit with a non-zero code
    CHECK(res.exit_code != 0);
    CHECK(res.err.find("No input file") != std::string::npos);
}

// ===========================================================================
// --jit-exec tests
// ===========================================================================

