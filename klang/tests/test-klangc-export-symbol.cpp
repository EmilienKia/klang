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

#include <catch2/catch_all.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <unistd.h>

#include "helpers.hpp"

namespace {

class ExportSymbolFixture {
public:
    std::filesystem::path path;

    ExportSymbolFixture() {
        static std::atomic<unsigned int> counter{0};
        do {
            path = std::filesystem::current_path() /
                (".klang-export-symbol-" + std::to_string(::getpid()) + "-" +
                 std::to_string(counter.fetch_add(1)));
        } while (!std::filesystem::create_directory(path));
    }

    ~ExportSymbolFixture() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }

    std::filesystem::path write_file(const std::string& name, const std::string& content) const {
        const auto file = path / name;
        std::ofstream stream(file);
        stream << content;
        stream.close();
        REQUIRE(stream.good());
        return file;
    }
};

const std::vector<std::string>& malformed_export_names() {
    static const std::vector<std::string> names = {
        "", "*", "native*", "native?", "native[12]", "native]", "native\\*",
        ",", "native,--export-dynamic", "-E", "--export-dynamic", "@symbols",
        "native@VERSION", "native name", "native\nname", "native=other",
        "native;other", std::string(1, '\x80')
    };
    return names;
}

void check_success(const k::tools::exec_result& result) {
    INFO("stdout: " << result.out);
    INFO("stderr: " << result.err);
    REQUIRE(result.exit_code == 0);
}

} // namespace

TEST_CASE("Native export options validate exact names and deduplicate atomically",
          "[compiler][export-symbol]") {
    auto compiler = k::compiler::create();
    compiler->set_export_symbols({"native_callback", "second.callback$1", "native_callback"});
    const std::set<std::string> expected = {"native_callback", "second.callback$1"};
    CHECK(compiler->get_export_symbols() == expected);
    const std::vector<std::string> expected_args = {
        "-Xlinker", "--export-dynamic-symbol=native_callback",
        "-Xlinker", "--export-dynamic-symbol=second.callback$1"
    };
    CHECK(compiler->build_export_symbol_link_args() == expected_args);

    for (const auto& name : malformed_export_names()) {
        INFO("malformed native name: " << name);
        CHECK_THROWS_AS(compiler->set_export_symbols({"valid_name", name}), std::invalid_argument);
        CHECK(compiler->get_export_symbols() == expected);
    }
    CHECK_THROWS_AS(
        compiler->set_export_symbols({std::string("native\0suffix", sizeof("native\0suffix") - 1)}),
        std::invalid_argument);
    CHECK(compiler->get_export_symbols() == expected);

    compiler->set_export_symbols({});
    CHECK(compiler->get_export_symbols().empty());
    CHECK(compiler->build_export_symbol_link_args().empty());
}

TEST_CASE("Native export options reject non-executable compiler output before emission",
          "[compiler][export-symbol]") {
    auto compiler = k::compiler::create();
    compiler->set_export_symbols({"native_callback"});
    ExportSymbolFixture fixture;
    const auto output = fixture.path / "must-not-exist";

    SECTION("object") {
        CHECK_FALSE(compiler->gen_object_file(output.string()));
    }
    SECTION("shared library") {
        CHECK_FALSE(compiler->gen_shared_library(output.string()));
    }
    SECTION("static library") {
        CHECK_FALSE(compiler->gen_static_library(output.string()));
    }
    SECTION("combined libraries") {
        CHECK_FALSE(compiler->gen_libraries(output.string(), (fixture.path / "other.a").string()));
        CHECK_FALSE(std::filesystem::exists(fixture.path / "other.a"));
    }
    SECTION("KDI") {
        CHECK_FALSE(compiler->gen_kdi(output.string()));
    }
    SECTION("JIT") {
        CHECK(compiler->to_jit() == nullptr);
    }
    CHECK(compiler->has_compilation_error());
    CHECK_FALSE(std::filesystem::exists(output));
}

TEST_CASE("klangc rejects malformed native export names and missing arguments",
          "[klangc][export-symbol]") {
    ExportSymbolFixture fixture;
    const auto output = fixture.path / "must-not-exist";
    for (const auto& name : malformed_export_names()) {
        INFO("malformed native name: " << name);
        const auto result = k::tools::run_process(
            find_klangc().string(),
            {"--export-symbol=" + name, "-o", output.string(), "not-read.k"});
        INFO("stdout: " << result.out);
        INFO("stderr: " << result.err);
        CHECK(result.exit_code != 0);
        CHECK((result.out + result.err).find("--export-symbol") != std::string::npos);
        CHECK_FALSE(std::filesystem::exists(output));
    }
    const auto result = k::tools::run_process(find_klangc().string(), {"--export-symbol"});
    INFO("stderr: " << result.err);
    CHECK(result.exit_code != 0);
    CHECK(result.err.find("--export-symbol") != std::string::npos);
}

TEST_CASE("klangc restricts native exports to GNU/Linux ELF executable output",
          "[klangc][export-symbol]") {
    for (const auto& mode : {"-c", "--dyn-lib", "--static-lib", "--jit-exec", "--version",
                             "--print-target-triple", "--print-targets"}) {
        INFO("incompatible mode: " << mode);
        const auto result = k::tools::run_process(
            find_klangc().string(), {"--export-symbol", "native_callback", mode, "not-read.k"});
        INFO("stderr: " << result.err);
        CHECK(result.exit_code != 0);
        CHECK(result.err.find("--export-symbol requires a GNU/Linux ELF executable") !=
              std::string::npos);
    }

    for (const auto& triple : {"x86_64-pc-windows-msvc", "x86_64-apple-darwin",
                               "x86_64-unknown-freebsd"}) {
        INFO("unsupported target: " << triple);
        CHECK_FALSE(k::compiler::supports_export_symbols(triple));
        const auto result = k::tools::run_process(
            find_klangc().string(),
            {"--export-symbol", "native_callback", "--target", triple, "not-read.k"});
        INFO("stderr: " << result.err);
        CHECK(result.exit_code != 0);
        CHECK(result.err.find("GNU/Linux ELF host and target") != std::string::npos);
    }
}

TEST_CASE("klangc does not silently apply native exports to automatic library output",
          "[klangc][export-symbol]") {
#if defined(__linux__)
    ExportSymbolFixture fixture;
    const auto source = fixture.write_file(
        "library.k", "module test::exports::library; value() : int { return 42; }\n");
    const auto output = fixture.path / "must-not-exist.so";
    const auto result = k::tools::run_process(
        find_klangc().string(), {source.string(), "--export-symbol", "native_callback",
                               "-o", output.string()});
    INFO("stdout: " << result.out);
    INFO("stderr: " << result.err);
    CHECK(result.exit_code != 0);
    CHECK((result.out + result.err).find("--export-symbol requires a GNU/Linux ELF executable") !=
          std::string::npos);
    CHECK_FALSE(std::filesystem::exists(output));
    CHECK_FALSE(std::filesystem::exists(fixture.path / "must-not-exist.kdi"));
#else
    SKIP("Native executable exports require GNU/Linux ELF");
#endif
}

TEST_CASE("klangc selectively exports repeated native names from linked C objects",
          "[klangc][export-symbol][object-input]") {
#if defined(__linux__)
    ExportSymbolFixture fixture;
    const auto c_source = fixture.write_file("native.c", R"C(
#include <dlfcn.h>
#include <stddef.h>

int k_export_selected_one(void) { return 19; }
int k_export_selected_two(void) { return 23; }
int k_export_unselected(void) { return 31; }
__attribute__((visibility("hidden"))) int k_export_hidden(void) { return 37; }

int k_export_probe(int selected) {
    void *image = dlopen(NULL, RTLD_NOW);
    if (!image) return 10;
    int (*first)(void) = (int (*)(void))dlsym(image, "k_export_selected_one");
    int (*second)(void) = (int (*)(void))dlsym(image, "k_export_selected_two");
    int status = 0;
    if (selected) {
        if (!first || !second) status = 11;
        else if (first() + second() != 42) status = 12;
    } else if (first || second) {
        status = 13;
    }
    if (dlsym(image, "k_export_unselected")) status = 14;
    if (dlsym(image, "k_export_probe")) status = 15;
    if (!dlsym(image, "__k_rtti_manifest_v1")) status = 16;
    if (dlsym(image, "k_export_missing")) status = 17;
    if (dlsym(image, "k_export_hidden")) status = 18;
    if (dlsym(image, "k_export_archived")) status = 19;
    dlclose(image);
    return status;
}
)C");
    const auto object = fixture.path / "native.o";
    check_success(k::tools::lookup_run_process(
        "clang", {"-fPIC", "-c", c_source.string(), "-o", object.string()}));
    const auto archive_source = fixture.write_file(
        "archived.c", "int k_export_archived(void) { return 41; }\n");
    const auto archive_object = fixture.path / "archived.o";
    const auto archive = fixture.path / "libarchived.a";
    check_success(k::tools::lookup_run_process(
        "clang", {"-fPIC", "-c", archive_source.string(), "-o", archive_object.string()}));
    check_success(k::tools::lookup_run_process(
        "ar", {"rcs", archive.string(), archive_object.string()}));

    bool selected = false;
    std::vector<std::string> export_args;
    SECTION("no extra exports preserves the default visibility") {}
    SECTION("repeatable exports expose only the requested native definitions") {
        selected = true;
        export_args = {
            "--export-symbol", "k_export_selected_one",
            "--export-symbol=k_export_selected_two",
            "--export-symbol", "k_export_selected_one",
            "--export-symbol=k_export_selected_two",
            "--export-symbol", "k_export_missing",
            "--export-symbol", "k_export_hidden",
            "--export-symbol", "k_export_archived"
        };
    }

    const auto source = fixture.write_file("main.k",
        "module test::exports::executable;\n"
        "import k;\n"
        "@ffi::Extern(\"C\") k_export_probe(selected : int) : int;\n"
        "main() : int { return k_export_probe(" + std::string(selected ? "1" : "0") + "); }\n");
    const auto executable = fixture.path / "app";
    std::vector<std::string> args = {
        source.string(), object.string(), "-l", "dl", "-l", archive.string(),
        "-o", executable.string()
    };
    args.insert(args.end(), export_args.begin(), export_args.end());
    check_success(k::tools::run_process(find_klangc().string(), args));
    REQUIRE(std::filesystem::exists(executable));
    ScopedLdLibraryPath library_path(find_libk_dir());
    check_success(k::tools::run_process(executable.string()));
#else
    SKIP("Native executable exports require GNU/Linux ELF");
#endif
}
