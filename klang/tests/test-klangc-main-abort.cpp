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
 * Tests for the generated C entry point guard: any exception reaching the
 * application runner (`main`) must terminate the process with abort() (SIGABRT).
 */
#include <catch2/catch_all.hpp>
#include <csignal>
#include <filesystem>
#include <unistd.h>
#include "helpers.hpp"

namespace {

// The process wrapper reports either the raw signal number or the shell-style
// 128 + signal number, depending on how the child status is decoded.
bool is_sigabrt(int exit_code) {
    return exit_code == SIGABRT || exit_code == 128 + SIGABRT;
}

struct TempExe {
    char path[64] = "/tmp/klang_main_abort_XXXXXX";
    TempExe() {
        int fd = ::mkstemp(path);
        REQUIRE(fd != -1);
        ::close(fd);
    }
    ~TempExe() { std::filesystem::remove(path); }
};

k::tools::exec_result compile_and_run(const std::string& src) {
    auto klangc = find_klangc();
    TempExe exe;

    auto res = k::tools::run_process(klangc.string(), {"--stdin", "-o", exe.path}, src);
    INFO("klangc stdout: " << res.out);
    INFO("klangc stderr: " << res.err);
    REQUIRE(res.exit_code == 0);

    ScopedLdLibraryPath ld_scope(find_libk_dir());
    return k::tools::run_process(exe.path, {});
}

} // namespace

// ---------------------------------------------------------------------------
// Uncaught exception in main()
// ---------------------------------------------------------------------------

TEST_CASE("main abort: uncaught throwable in main aborts (executable)", "[main-abort][live]") {
    auto run = compile_and_run(R"(
        module main_abort_01;
        main() : int {
            throw FatalError(3);
            return 0;
        }
    )");
    INFO("exec stderr: " << run.err);
    CHECK(is_sigabrt(run.exit_code));
}

TEST_CASE("main abort: uncaught throwable in main aborts (jit-exec)", "[main-abort][live]") {
    auto klangc = find_klangc();
    auto res = k::tools::run_process(klangc.string(), {"--stdin", "--jit-exec"}, R"(
        module main_abort_02;
        main() : int {
            throw FatalError(3);
            return 0;
        }
    )");
    INFO("klangc stderr: " << res.err);
    CHECK(is_sigabrt(res.exit_code));
}

TEST_CASE("main abort: throwable propagating through nested frames aborts", "[main-abort][live]") {
    auto run = compile_and_run(R"(
        module main_abort_03;

        struct Guard {
            ~Guard() { }
        }

        deep(n: int) : int {
            g : Guard;
            if (n == 0) {
                throw FatalError(n);
            }
            return deep(n - 1);
        }

        main() : int {
            return deep(3);
        }
    )");
    CHECK(is_sigabrt(run.exit_code));
}

TEST_CASE("main abort: void main with args aborts", "[main-abort][live]") {
    auto run = compile_and_run(R"(
        module main_abort_04;
        main(args : const String[]) {
            throw FatalError(1);
        }
    )");
    CHECK(is_sigabrt(run.exit_code));
}

TEST_CASE("main abort: exception thrown by Application constructor aborts", "[main-abort][live]") {
    auto run = compile_and_run(R"(
        module main_abort_05;

        class Application : public k::Application {
            public Application() {
                throw FatalError(5);
            }
            public main() : int {
                return 0;
            }
        }
    )");
    CHECK(is_sigabrt(run.exit_code));
}

// ---------------------------------------------------------------------------
// No regression on the normal paths
// ---------------------------------------------------------------------------

TEST_CASE("main abort: caught exception does not abort", "[main-abort][live]") {
    auto run = compile_and_run(R"(
        module main_abort_06;
        main() : int {
            try {
                throw FatalError(3);
            } catch (e: FatalError&) {
                return 7;
            }
            return 0;
        }
    )");
    CHECK(run.exit_code == 7);
}

TEST_CASE("main abort: normal exit code is preserved with args", "[main-abort][live]") {
    auto run = compile_and_run(R"(
        module main_abort_07;
        main(args : const String[]) : int {
            return 11;
        }
    )");
    CHECK(run.exit_code == 11);
}
