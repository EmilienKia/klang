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
 * Tests for passing pre-compiled .o files to klangc alongside .k sources.
 * The external symbols from the .o must appear in the final linked output.
 */
#include <catch2/catch_all.hpp>
#include <filesystem>
#include <fstream>
#include <unistd.h>
#include "helpers.hpp"

// ---------------------------------------------------------------------------
// Helper: compile a C source string into a .o file via clang -c
// Returns the path to the generated .o (in /tmp).
// ---------------------------------------------------------------------------
static std::string compile_c_to_object(const std::string& c_source) {
    // Write C source to a temp .c file
    char c_file[] = "/tmp/klang_c_src_XXXXXX";
    int fd = ::mkstemp(c_file);
    if (fd == -1) throw std::runtime_error("Cannot create temp .c file");
    std::string c_path = std::string(c_file) + ".c";
    ::close(fd);
    std::filesystem::remove(c_file);

    {
        std::ofstream ofs(c_path);
        ofs << c_source;
    }

    std::string o_path = std::filesystem::path(c_path).replace_extension(".o").string();

    // Compile with clang -c -fPIC
    auto res = k::tools::lookup_run_process("clang", {"-c", "-fPIC", "-o", o_path, c_path});
    std::filesystem::remove(c_path);
    if (res.exit_code != 0) {
        throw std::runtime_error("clang -c failed: " + res.err);
    }
    return o_path;
}

// ===========================================================================
// Test: .o linked into a shared library — symbol visible via nm
// ===========================================================================

TEST_CASE("klangc: .o file linked into shared library exports C symbol", "[klangc][object-input]") {
    auto klangc = find_klangc();

    // 1. Compile a trivial C function into a .o
    std::string c_src = R"(
        int external_c_answer(void) {
            return 42;
        }
    )";
    std::string o_path = compile_c_to_object(c_src);
    REQUIRE(std::filesystem::exists(o_path));

    // 2. Write a minimal K source that compiles into a shared library
    char k_file[] = "/tmp/klang_objtest_XXXXXX";
    int fd = ::mkstemp(k_file);
    REQUIRE(fd != -1);
    std::string k_path = std::string(k_file) + ".k";
    ::close(fd);
    std::filesystem::remove(k_file);
    {
        std::ofstream ofs(k_path);
        ofs << R"(
            module klangc_object_input_01;
            namespace klangc_object_input_01 {
                k_func() : int {
                    return 7;
                }
            }
        )";
    }

    // 3. Build the shared library via klangc, passing both the .k and .o
    std::string so_path = std::string(k_file) + ".so";
    auto res = k::tools::run_process(
        klangc.string(),
        {"--dyn-lib", "--no-emit-kdi", "-o", so_path, k_path, o_path}
    );

    INFO("klangc stdout: " << res.out);
    INFO("klangc stderr: " << res.err);
    REQUIRE(res.exit_code == 0);
    REQUIRE(std::filesystem::exists(so_path));

    // 4. Verify that the C symbol is present in the .so via nm
    auto nm_res = k::tools::lookup_run_process("nm", {"--dynamic", "--defined-only", so_path});
    INFO("nm stdout: " << nm_res.out);
    INFO("nm stderr: " << nm_res.err);
    REQUIRE(nm_res.exit_code == 0);
    REQUIRE(nm_res.out.find("external_c_answer") != std::string::npos);

    // Also verify that the K symbol is present
    REQUIRE(nm_res.out.find("k_func") != std::string::npos);

    // Cleanup
    std::filesystem::remove(so_path);
    std::filesystem::remove(o_path);
    std::filesystem::remove(k_path);
}

// ===========================================================================
// Test: .o linked into an executable — symbol visible and callable
// ===========================================================================

TEST_CASE("klangc: .o file linked into executable exports C symbol", "[klangc][object-input]") {
    auto klangc = find_klangc();

    // 1. Compile a C function that returns 55
    std::string c_src = R"(
        int get_c_value(void) {
            return 55;
        }
    )";
    std::string o_path = compile_c_to_object(c_src);
    REQUIRE(std::filesystem::exists(o_path));

    // 2. Write a K source with a main() that returns a constant
    //    (we don't call the C function from K here — we just verify it's linked)
    char k_file[] = "/tmp/klang_objtest_exe_XXXXXX";
    int fd = ::mkstemp(k_file);
    REQUIRE(fd != -1);
    std::string k_path = std::string(k_file) + ".k";
    ::close(fd);
    std::filesystem::remove(k_file);
    {
        std::ofstream ofs(k_path);
        ofs << R"(
            module klangc_object_input_02;
            main() : int {
                return 0;
            }
        )";
    }

    // 3. Build executable
    std::string exe_path = std::string(k_file) + ".exe";
    auto res = k::tools::run_process(
        klangc.string(),
        {"-o", exe_path, k_path, o_path}
    );

    INFO("klangc stdout: " << res.out);
    INFO("klangc stderr: " << res.err);
    REQUIRE(res.exit_code == 0);
    REQUIRE(std::filesystem::exists(exe_path));

    // 4. Verify symbol via nm (regular nm for executables)
    auto nm_res = k::tools::lookup_run_process("nm", {"--defined-only", exe_path});
    INFO("nm stdout: " << nm_res.out);
    INFO("nm stderr: " << nm_res.err);
    REQUIRE(nm_res.exit_code == 0);
    REQUIRE(nm_res.out.find("get_c_value") != std::string::npos);

    // 5. Execute it — should return 0
    //    (libk.so is auto-linked; set LD_LIBRARY_PATH so the loader finds it)
    ScopedLdLibraryPath ld_scope(find_libk_dir());
    auto exec_res = k::tools::run_process(exe_path, {});
    REQUIRE(exec_res.exit_code == 0);

    // Cleanup
    std::filesystem::remove(exe_path);
    std::filesystem::remove(o_path);
    std::filesystem::remove(k_path);
}

// ===========================================================================
// Test: .o files rejected with --jit-exec
// ===========================================================================

TEST_CASE("klangc: .o files rejected with --jit-exec", "[klangc][object-input]") {
    auto klangc = find_klangc();

    std::string c_src = "int dummy(void) { return 0; }\n";
    std::string o_path = compile_c_to_object(c_src);

    auto res = k::tools::run_process(
        klangc.string(),
        {"--stdin", "--jit-exec", o_path},
        "module jit_obj; main() : int { return 0; }"
    );

    INFO("klangc stdout: " << res.out);
    INFO("klangc stderr: " << res.err);
    // Should fail because .o + --jit-exec is incompatible
    REQUIRE(res.exit_code != 0);
    // Compiler diagnostics (including CLI-driver errors) are reported through
    // the standard diagnostic infrastructure, which writes to stdout.
    REQUIRE(res.out.find(".o") != std::string::npos);

    std::filesystem::remove(o_path);
}

// ===========================================================================
// Test: .o files rejected with -c (compile-only)
// ===========================================================================

TEST_CASE("klangc: .o files rejected with -c", "[klangc][object-input]") {
    auto klangc = find_klangc();

    std::string c_src = "int dummy(void) { return 0; }\n";
    std::string o_path = compile_c_to_object(c_src);

    auto res = k::tools::run_process(
        klangc.string(),
        {"--stdin", "-c", o_path},
        "module compile_only; foo() : int { return 1; }"
    );

    INFO("klangc stdout: " << res.out);
    INFO("klangc stderr: " << res.err);
    // Should fail because .o + -c is incompatible
    REQUIRE(res.exit_code != 0);
    // Compiler diagnostics (including CLI-driver errors) are reported through
    // the standard diagnostic infrastructure, which writes to stdout.
    REQUIRE(res.out.find(".o") != std::string::npos);

    std::filesystem::remove(o_path);
}


