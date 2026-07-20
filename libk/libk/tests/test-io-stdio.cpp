/*
 * K Language standard library — I/O standard streams tests
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
 * Tests for k::io::stdin, k::io::stdout, k::io::stderr global standard I/O
 * references.
 *
 * These tests compile a small K program into an executable and run it in a
 * subprocess, feeding data on stdin and verifying captured stdout/stderr.
 */
#include <catch2/catch_all.hpp>
#include "helpers.hpp"
#include <cstdio>
#include <filesystem>
#include <unistd.h>

// compile_text is defined in klang-test-helpers but not declared in the header.
// We forward-declare it here for use in subprocess tests.
extern bool compile_text(const std::string_view& source, const std::string& out_file,
                          const IgnoredDiagCodes& ignored_diag_codes = {});

namespace {

/**
 * Compile a K source string into a temporary executable and run it in a
 * subprocess, optionally feeding stdin_data.  The executable is linked
 * against the real libk.so (standard library).
 */
k::tools::exec_result compile_and_run(const std::string_view& src,
                                       const std::optional<std::string>& stdin_data = std::nullopt)
{
    char out_file[] = "/tmp/klang_stdio_test_XXXXXX";
    int fd = ::mkstemp(out_file);
    REQUIRE(fd != -1);
    ::close(fd);

    bool ok = compile_text(src, out_file);
    if (!ok) {
        std::filesystem::remove(out_file);
        FAIL("Failed to compile K source for stdio test");
    }

    ScopedLdLibraryPath ld_scope(find_libk_dir());
    auto res = k::tools::run_process(out_file, {}, stdin_data);
    std::filesystem::remove(out_file);
    return res;
}

} // anonymous namespace


// ═════════════════════════════════════════════════════════════════════════════
// Test A — println on stdout and stderr
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("stdio: println writes to stdout and stderr", "[libk][io][stdio]") {
    auto res = compile_and_run(R"SRC(
        module __stdio_println__;
        main() : int {
            k::io::stdout.println("hello stdout");
            k::io::stderr.println("hello stderr");
            return 0;
        }
    )SRC");
    INFO("stdout: " << res.out);
    INFO("stderr: " << res.err);
    CHECK(res.exit_code == 0);
    CHECK(res.out == "hello stdout\n");
    CHECK(res.err == "hello stderr\n");
}


// ═════════════════════════════════════════════════════════════════════════════
// Test B — read from stdin, echo to stdout
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("stdio: read stdin and echo to stdout", "[libk][io][stdio]") {
    auto res = compile_and_run(R"SRC(
        module __stdio_echo__;
        main() : int {
            buf : byte[1];
            n : int = (int) k::io::stdin.read(buf, 0, 1).getResultOr((unsigned int) 0);
            while (n > 0) {
                k::io::stdout.write((int) buf[0]);
                n = (int) k::io::stdin.read(buf, 0, 1).getResultOr((unsigned int) 0);
            }
            k::io::stdout.flush();
            return 0;
        }
    )SRC", "ABCDE");
    INFO("stdout: " << res.out);
    INFO("stderr: " << res.err);
    CHECK(res.exit_code == 0);
    // run_process reads output line-by-line with getline and re-appends '\n',
    // so raw binary output "ABCDE" becomes "ABCDE\n".
    CHECK(res.out == "ABCDE\n");
}


// ═════════════════════════════════════════════════════════════════════════════
// Test C — read stdin, write to stdout and stderr simultaneously
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("stdio: read stdin, write stdout and stderr", "[libk][io][stdio]") {
    auto res = compile_and_run(R"SRC(
        module __stdio_rw__;
        main() : int {
            k::io::stderr.print("start ");
            buf : byte[1];
            n : int = (int) k::io::stdin.read(buf, 0, 1).getResultOr((unsigned int) 0);
            while (n > 0) {
                k::io::stdout.write((int) buf[0]);
                n = (int) k::io::stdin.read(buf, 0, 1).getResultOr((unsigned int) 0);
            }
            k::io::stdout.flush();
            k::io::stderr.println("done");
            return 0;
        }
    )SRC", "XYZ");
    INFO("stdout: " << res.out);
    INFO("stderr: " << res.err);
    CHECK(res.exit_code == 0);
    CHECK(res.out == "XYZ\n");
    CHECK(res.err == "start done\n");
}



