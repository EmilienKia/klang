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
#include <iterator>
#include <sstream>
#include <unistd.h>
#include <llvm/ADT/SmallString.h>
#include <llvm/Support/FileSystem.h>
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

namespace {

struct ArchiveTestEnv {
    std::filesystem::path previous = std::filesystem::current_path();
    std::filesystem::path root;

    ArchiveTestEnv() {
        llvm::SmallString<128> directory;
        auto ec = llvm::sys::fs::createUniqueDirectory(
            (std::filesystem::temp_directory_path() / "klang-archive-test").string(), directory);
        REQUIRE_FALSE(ec);
        root = directory.c_str();
        REQUIRE(std::filesystem::create_directory(root / "archive fixtures"));
        std::filesystem::current_path(root / "archive fixtures");
    }

    ~ArchiveTestEnv() {
        std::error_code ec;
        std::filesystem::current_path(previous, ec);
        CHECK_FALSE(ec);
        std::filesystem::remove_all(root, ec);
        CHECK_FALSE(ec);
    }
};

void write_archive_fixture(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream file(path);
    REQUIRE(file.is_open());
    file << contents;
    file.close();
    REQUIRE_FALSE(file.fail());
}

std::string read_archive_fixture(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    REQUIRE(file.is_open());
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}

std::vector<std::string> archive_members(const std::string& archive) {
    auto result = k::tools::lookup_run_process("ar", {"t", archive});
    INFO("ar stdout: " << result.out);
    INFO("ar stderr: " << result.err);
    REQUIRE(result.exit_code == 0);
    std::vector<std::string> members;
    std::istringstream lines(result.out);
    for (std::string member; std::getline(lines, member);) {
        members.push_back(member);
    }
    return members;
}

void require_archive_consumer(const std::filesystem::path& klangc,
                              const std::string& module, int expected) {
    const std::string base = "lib" + module;
    // Remove only this fixture's shared output, forcing the imported library to resolve to .a.
    if (std::filesystem::exists(base + ".so")) {
        REQUIRE(std::filesystem::remove(base + ".so"));
    }
    write_archive_fixture("consumer source.k",
        "module archive_consumer; import " + module + "; "
        "main() : int { return " + module + "::answer(); }\n");
    auto link = k::tools::run_process(klangc.string(),
        {"-o", "archive consumer", "consumer source.k", "-i", module + "=" + base + ".kdi",
         "-I", ".", "-L", "."});
    INFO("consumer link stdout: " << link.out);
    INFO("consumer link stderr: " << link.err);
    REQUIRE(link.exit_code == 0);
    ScopedLdLibraryPath ld_scope(find_libk_dir());
    auto run = k::tools::run_process(
        (std::filesystem::current_path() / "archive consumer").string(), {});
    INFO("consumer stdout: " << run.out);
    INFO("consumer stderr: " << run.err);
    REQUIRE(run.exit_code == expected);
}

void require_no_archive_temporaries() {
    for (const auto& entry : std::filesystem::directory_iterator(".")) {
        CHECK_FALSE(entry.is_directory());
    }
}

void check_archive_replacement(bool combined) {
    const auto klangc = find_klangc();
    ArchiveTestEnv env;
    const std::string module = combined ? "archive_replace_combined" : "archive_replace_static";
    const std::string base = "lib" + module;
    const std::string archive = base + ".a";
    const std::string source = "library source.k";
    std::vector<std::string> args = {"--static-lib"};
    if (combined) {
        args.push_back("--dyn-lib");
    } else {
        args.insert(args.end(), {"-o", archive});
    }
    args.push_back(source);
    args.push_back("current marker.o");

    write_archive_fixture("removed marker.c", "int obsolete_c_marker(void) { return 17; }\n");
    auto c_object = k::tools::lookup_run_process("clang",
        {"-c", "-fPIC", "-o", "removed marker.o", "removed marker.c"});
    INFO("clang stdout: " << c_object.out);
    INFO("clang stderr: " << c_object.err);
    REQUIRE(c_object.exit_code == 0);
    write_archive_fixture("current marker.c", "int current_c_marker(void) { return 23; }\n");
    auto current_object = k::tools::lookup_run_process("clang",
        {"-c", "-fPIC", "-o", "current marker.o", "current marker.c"});
    INFO("current clang stdout: " << current_object.out);
    INFO("current clang stderr: " << current_object.err);
    REQUIRE(current_object.exit_code == 0);

    write_archive_fixture(source, "module " + module +
        "; answer() : int { return 1; } obsoleteRoot() : int { return 17; }\n");
    auto first_args = args;
    first_args.push_back("removed marker.o");
    auto first = k::tools::run_process(klangc.string(), first_args);
    INFO("first generation stdout: " << first.out);
    INFO("first generation stderr: " << first.err);
    REQUIRE(first.exit_code == 0);
    const auto before = archive_members(archive);
    REQUIRE(before.size() == 3);
    REQUIRE(before[1] == "current marker.o");
    REQUIRE(before[2] == "removed marker.o");
    REQUIRE(std::filesystem::exists(base + ".kdi"));
    if (combined) REQUIRE(std::filesystem::exists(base + ".so"));

    write_archive_fixture(source, "module " + module + "; answer() : int { return 2; }\n");
    auto second = k::tools::run_process(klangc.string(), args);
    INFO("second generation stdout: " << second.out);
    INFO("second generation stderr: " << second.err);
    REQUIRE(second.exit_code == 0);
    const auto after = archive_members(archive);
    REQUIRE(after.size() == 2);
    REQUIRE(after.front() != before.front());
    REQUIRE(after[1] == "current marker.o");
    auto symbols = k::tools::lookup_run_process("nm", {"--defined-only", archive});
    INFO("nm stdout: " << symbols.out);
    INFO("nm stderr: " << symbols.err);
    REQUIRE(symbols.exit_code == 0);
    CHECK(symbols.out.find("current_c_marker") != std::string::npos);
    CHECK(symbols.out.find("obsolete_c_marker") == std::string::npos);
    CHECK(symbols.out.find("obsoleteRoot") == std::string::npos);
    require_no_archive_temporaries();
    require_archive_consumer(klangc, module, 2);
}

void check_archive_failure(bool combined) {
    const auto klangc = find_klangc();
    ArchiveTestEnv env;
    const std::string module = combined ? "archive_failure_combined" : "archive_failure_static";
    const std::string base = "lib" + module;
    const std::string archive = base + ".a";
    const std::string source = "library source.k";
    std::vector<std::string> args = {"--static-lib"};
    if (combined) {
        args.push_back("--dyn-lib");
    } else {
        args.insert(args.end(), {"-o", archive});
    }
    args.push_back(source);
    write_archive_fixture(source, "module " + module + "; answer() : int { return 1; }\n");
    auto first = k::tools::run_process(klangc.string(), args);
    INFO("first generation stdout: " << first.out);
    INFO("first generation stderr: " << first.err);
    REQUIRE(first.exit_code == 0);
    const auto good_archive = read_archive_fixture(archive);
    const auto good_kdi = read_archive_fixture(base + ".kdi");
    const auto good_members = archive_members(archive);

    write_archive_fixture(source, "module " + module + "; answer() : int { return 2; }\n");
    args.push_back("missing object.o");
    auto failed = k::tools::run_process(klangc.string(), args);
    INFO("failed generation stdout: " << failed.out);
    INFO("failed generation stderr: " << failed.err);
    REQUIRE(failed.exit_code != 0);
    CHECK(failed.err.find("missing object.o") != std::string::npos);
    CHECK(failed.out.find("ar exited with status") != std::string::npos);
    CHECK(read_archive_fixture(archive) == good_archive);
    CHECK(read_archive_fixture(base + ".kdi") == good_kdi);
    CHECK(archive_members(archive) == good_members);
    require_no_archive_temporaries();
    require_archive_consumer(klangc, module, 1);
}

} // namespace

TEST_CASE("klangc: static archive rebuild replaces removed inputs and module code",
          "[klangc][object-input][archive-replacement][static-only]") {
    check_archive_replacement(false);
}

TEST_CASE("klangc: combined library rebuild replaces removed archive inputs and module code",
          "[klangc][object-input][archive-replacement][combined]") {
    check_archive_replacement(true);
}

TEST_CASE("klangc: static archive failure preserves the previous archive and KDI",
          "[klangc][object-input][archive-replacement][archive-failure][static-only]") {
    check_archive_failure(false);
}

TEST_CASE("klangc: combined archive failure preserves the previous archive and KDI",
          "[klangc][object-input][archive-replacement][archive-failure][combined]") {
    check_archive_failure(true);
}
