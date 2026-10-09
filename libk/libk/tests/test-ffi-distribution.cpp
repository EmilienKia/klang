/*
 * K Language standard library — downstream runtime FFI distribution tests
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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <unistd.h>

#include "common/process.hpp"

#ifndef LIBK_LIB_DIR
#error "LIBK_LIB_DIR must identify the built runtime and libk.pc"
#endif
#ifndef LIBK_KDI_DIR
#error "LIBK_KDI_DIR must identify the built runtime descriptors"
#endif
#ifndef LIBK_FFI_RUNTIME_INCLUDE_DIR
#error "LIBK_FFI_RUNTIME_INCLUDE_DIR must identify src/runtime"
#endif
#ifndef KLANGC_PATH
#error "KLANGC_PATH must identify the built klangc executable"
#endif

namespace {

namespace fs = std::filesystem;

class Workspace {
public:
    fs::path path;

    Workspace() {
        static std::atomic<unsigned int> counter{0};
        do {
            path = fs::current_path() /
                (".libk-ffi-distribution-" + std::to_string(::getpid()) + "-" +
                 std::to_string(counter.fetch_add(1)));
        } while (!fs::create_directory(path));
    }

    ~Workspace() {
        std::error_code error;
        fs::remove_all(path, error);
    }

    fs::path write(std::string_view name, std::string_view source) const {
        const auto file = path / name;
        std::ofstream stream(file);
        stream << source;
        stream.close();
        REQUIRE(stream.good());
        return file;
    }
};

class ScopedCurrentDirectory {
    fs::path previous;

public:
    explicit ScopedCurrentDirectory(const fs::path& directory)
        : previous(fs::current_path()) {
        fs::current_path(directory);
    }

    ~ScopedCurrentDirectory() {
        std::error_code error;
        fs::current_path(previous, error);
    }
};

void check_success(const k::tools::exec_result& result) {
    INFO("stdout: " << result.out);
    INFO("stderr: " << result.err);
    REQUIRE(result.exit_code == 0);
}

std::string trim(std::string value) {
    const auto whitespace = [](unsigned char c) { return std::isspace(c) != 0; };
    while (!value.empty() && whitespace(value.back())) value.pop_back();
    const auto begin = std::find_if_not(value.begin(), value.end(), whitespace);
    value.erase(value.begin(), begin);
    return value;
}

// Decode pkg-config's shell escaping into argv without ever invoking a shell.
std::vector<std::string> split_flags(std::string_view text) {
    enum class Quote { NONE, SINGLE, DOUBLE };
    Quote quote = Quote::NONE;
    std::vector<std::string> flags;
    std::string flag;
    bool started = false;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (quote == Quote::SINGLE) {
            if (c == '\'') quote = Quote::NONE;
            else flag += c;
        } else if (quote == Quote::DOUBLE) {
            if (c == '"') {
                quote = Quote::NONE;
            } else if (c == '\\') {
                if (i + 1 == text.size()) {
                    throw std::invalid_argument("Trailing escape in pkg-config flags");
                }
                const char next = text[i + 1];
                if (next == '"' || next == '\\' || next == '$' || next == '`') {
                    flag += next;
                    ++i;
                } else if (next == '\n') {
                    ++i;
                } else {
                    flag += c;
                }
            } else {
                flag += c;
            }
        } else if (c == '\'' || c == '"') {
            quote = c == '\'' ? Quote::SINGLE : Quote::DOUBLE;
            started = true;
        } else if (c == '\\') {
            if (++i == text.size()) {
                throw std::invalid_argument("Trailing escape in pkg-config flags");
            }
            if (text[i] != '\n') {
                flag += text[i];
                started = true;
            }
        } else if (std::isspace(static_cast<unsigned char>(c))) {
            if (started) {
                flags.push_back(flag);
                flag.clear();
                started = false;
            }
        } else {
            flag += c;
            started = true;
        }
    }
    if (quote != Quote::NONE) {
        throw std::invalid_argument("Unterminated quote in pkg-config flags");
    }
    if (started) flags.push_back(flag);
    return flags;
}

k::tools::exec_result pkg_config(const fs::path& package,
                                std::vector<std::string> options) {
    options.push_back(package.string());
    auto result = k::tools::lookup_run_process("pkg-config", options);
    check_success(result);
    return result;
}

std::vector<std::string> pkg_flags(const fs::path& package,
                                  std::vector<std::string> options) {
    return split_flags(pkg_config(package, std::move(options)).out);
}

bool contains(const std::vector<std::string>& flags, std::string_view flag) {
    return std::find(flags.begin(), flags.end(), flag) != flags.end();
}

void copy_package(const fs::path& destination, bool shared) {
    fs::create_directory(destination);
    for (const char* name : {"libk.pc", "libk.a"}) {
        const auto source = fs::path(LIBK_LIB_DIR) / name;
        REQUIRE(fs::is_regular_file(source));
        fs::copy_file(source, destination / name);
    }
    for (const char* name : {"libk.kdi", "k.kdi"}) {
        const auto source = fs::path(LIBK_KDI_DIR) / name;
        REQUIRE(fs::is_regular_file(source));
        fs::copy_file(source, destination / name);
    }
    if (shared) {
        const auto source = fs::path(LIBK_LIB_DIR) / "libk.so";
        REQUIRE(fs::is_regular_file(source));
        fs::copy_file(source, destination / "libk.so");
    }
}

std::vector<std::string> explicit_runtime(std::vector<std::string> flags,
                                          const fs::path& runtime) {
    unsigned int replacements = 0;
    for (auto& flag : flags) {
        if (flag == "-lk") {
            flag = runtime.string();
            ++replacements;
        }
    }
    REQUIRE(replacements == 1);
    return flags;
}

void check_runtime_dependency(const fs::path& executable, bool shared) {
    const auto result = k::tools::lookup_run_process("readelf", {"-d", executable.string()});
    check_success(result);
    INFO(result.out);
    std::istringstream lines(result.out);
    std::string line;
    bool runtime_needed = false;
    while (std::getline(lines, line)) {
        if (line.find("(NEEDED)") != std::string::npos &&
            line.find("libk.so") != std::string::npos) {
            runtime_needed = true;
        }
    }
    REQUIRE(runtime_needed == shared);
}

// klangc's -l accepts libraries, not arbitrary native driver flags.
void append_native_dependencies(std::vector<std::string>& args, const fs::path& package) {
    for (const auto& flag : pkg_flags(package, {"--libs-only-L", "--static"})) {
        REQUIRE(flag.starts_with("-L"));
        REQUIRE(flag.size() > 2);
        args.insert(args.end(), {"-L", flag.substr(2)});
    }
    std::vector<std::string> libraries;
    for (const auto& flag : pkg_flags(package, {"--libs-only-l", "--static"})) {
        REQUIRE(flag.starts_with("-l"));
        REQUIRE(flag.size() > 2);
        if (flag != "-lk") libraries.push_back(flag.substr(2));
    }
    for (const auto& flag : pkg_flags(package, {"--libs-only-other", "--static"})) {
        if (flag == "-pthread") {
            if (!contains(libraries, "pthread")) libraries.push_back("pthread");
        } else {
            FAIL("Static package requires an unsupported klangc native driver option: " << flag);
        }
    }
    REQUIRE(contains(libraries, "ffi"));
    REQUIRE(contains(libraries, "uring"));
    for (const auto& library : libraries) args.insert(args.end(), {"-l", library});
}

constexpr std::string_view NATIVE_CONSUMER = R"(
#include "runtime_ffi.h"
#include <cerrno>

extern "C" int distribution_native(int value) {
    errno = 37;
    return value + 2;
}

int main() {
    const int32_t parameters[] = {K_FFI_C_INT};
    int32_t status = K_FFI_OK;
    KFFISignature *signature = k_ffi_prepare(K_FFI_C_INT, parameters, 1,
                                            K_FFI_ABI_DEFAULT, &status);
    if (status != K_FFI_OK || signature == nullptr) return 1;
    KFFIFrame *frame = k_ffi_frame_create(signature, &status);
    k_ffi_signature_release(signature);
    if (status != K_FFI_OK || frame == nullptr) return 2;
    int failure = 0;
    int64_t value = 0;
    int32_t error_number = 0;
    if (k_ffi_frame_set_signed(frame, 0, K_FFI_C_INT, 40) != K_FFI_OK) failure = 3;
    else if (k_ffi_frame_invoke(reinterpret_cast<void *>(&distribution_native),
                               frame, 1, 1) != K_FFI_OK) failure = 4;
    else if (k_ffi_frame_result_signed(frame, &value) != K_FFI_OK || value != 42) failure = 5;
    else if (k_ffi_frame_result_errno(frame, &error_number) != K_FFI_OK ||
             error_number != 37) failure = 6;
    k_ffi_frame_release(frame);
    return failure;
}
)";

constexpr std::string_view K_CONSUMER = R"(
module __ffi_distribution__;

using namespace ::k::ffi;
using namespace ::k::ffi::c;

main() : int {
    app : Shared<ApplicationModule> = applicationModule();
    if (!app) return 1;
    lookup : Expected<NativeSymbol, SymbolLookupError>
        = app.get()->lookupSymbol(String("klang_ffi_distribution_host"));
    if (!lookup.hasResult()) return 2;
    symbol : NativeSymbol = lookup.getResult();
    if (!symbol.hasAddress()) return 3;
    builder : CSignatureBuilder;
    prepared : Expected<CPreparedSignature, FfiError>
        = builder.returns(CType::INT).parameter(CType::INT).prepare();
    if (!prepared.hasResult()) return 4;
    bound : Expected<CFunction, FfiError>
        = CFunction::bind(symbol, prepared.getResult(), true);
    if (!bound.hasResult()) return 5;
    function : CFunction = bound.getResult();
    result : Expected<CallResult, FfiError>
        = function.call().argument(CValue(40)).invoke();
    if (!result.hasResult()) return 6;
    value : Expected<long, FfiError> = result.getResult().asSigned();
    if (!value.hasResult() || value.getResult() != 42L) return 7;
    errorNumber : Expected<int, FfiError> = result.getResult().getErrno();
    if (!errorNumber.hasResult() || errorNumber.getResult() != 37) return 8;
    return 0;
}
)";

} // namespace

TEST_CASE("Distribution pkg-config flag decoding preserves quoted paths",
          "[libk][ffi][distribution]") {
    const auto flags = split_flags(R"(-L"/path with spaces" -L/a\ b -lffi 'literal\path' ""
        -D"quoted\"value" -L'/path with a'\''quote')");
    const std::vector<std::string> expected = {
        "-L/path with spaces", "-L/a b", "-lffi", "literal\\path", "",
        "-Dquoted\"value", "-L/path with a'quote"
    };
    CHECK(flags == expected);
    CHECK(split_flags("\"\\q\"") == std::vector<std::string>{"\\q"});
    CHECK(split_flags("-l\\\nffi") == std::vector<std::string>{"-lffi"});
    CHECK_THROWS_AS(split_flags("'unclosed"), std::invalid_argument);
    CHECK_THROWS_AS(split_flags("\"unclosed"), std::invalid_argument);
    CHECK_THROWS_AS(split_flags("trailing\\"), std::invalid_argument);
}

TEST_CASE("Distributed libk metadata includes native static dependencies and relocates",
          "[libk][ffi][distribution]") {
    const auto package = fs::path(LIBK_LIB_DIR) / "libk.pc";
    pkg_config(package, {"--exists"});
    const auto requirements = pkg_config(package, {"--print-requires-private"}).out;
    CHECK(requirements.find("libffi") != std::string::npos);
    CHECK(requirements.find("liburing") != std::string::npos);
    const auto shared = pkg_flags(package, {"--libs"});
    REQUIRE(contains(shared, "-lk"));
    const auto native = pkg_flags(package, {"--libs", "--static"});
    CHECK(contains(native, "-lk"));
    CHECK(contains(native, "-lffi"));
    CHECK(contains(native, "-luring"));
    CHECK(contains(native, "-ldl"));
    CHECK(contains(native, "-lm"));
    CHECK((contains(native, "-pthread") || contains(native, "-lpthread")));

    Workspace workspace;
    const auto relocated = workspace.path / "relocated runtime's files";
    copy_package(relocated, true);
    const auto relocated_package = relocated / "libk.pc";
    pkg_config(relocated_package, {"--exists", "--static"});
    for (const char* variable : {"libdir", "kdi_dir"}) {
        const auto value = trim(pkg_config(relocated_package,
            {std::string("--variable=") + variable}).out);
        CHECK(fs::weakly_canonical(value) == fs::weakly_canonical(relocated));
    }
    const auto relocated_flags = pkg_flags(relocated_package, {"--libs", "--static"});
    CHECK(contains(relocated_flags, "-L" + relocated.string()));
    CHECK_FALSE(contains(relocated_flags, "-L" + std::string(LIBK_LIB_DIR)));
}

TEST_CASE("Native downstream FFI consumer links the actual shared runtime or archive",
          "[libk][ffi][distribution]") {
    Workspace workspace;
    const auto source = workspace.write("consumer.cpp", NATIVE_CONSUMER);
    const auto output = workspace.path / "native-consumer";
    const auto package = fs::path(LIBK_LIB_DIR) / "libk.pc";
    bool static_link = false;
    SECTION("shared runtime") { static_link = false; }
    SECTION("explicit runtime archive") { static_link = true; }

    const auto runtime = fs::path(LIBK_LIB_DIR) / (static_link ? "libk.a" : "libk.so");
    REQUIRE(fs::is_regular_file(runtime));
    auto options = std::vector<std::string>{"--libs"};
    if (static_link) options.push_back("--static");
    const auto flags = explicit_runtime(pkg_flags(package, options), runtime);
    std::vector<std::string> args = {
        "-std=c++20", "-I", LIBK_FFI_RUNTIME_INCLUDE_DIR, source.string(), "-o", output.string()
    };
    args.insert(args.end(), flags.begin(), flags.end());
    args.insert(args.end(), {"-Xlinker", "-rpath", "-Xlinker", LIBK_LIB_DIR});
    check_success(k::tools::lookup_run_process("clang++", args));
    check_runtime_dependency(output, !static_link);
    check_success(k::tools::run_process(output.string()));
}

TEST_CASE("Downstream K executable invokes an exported C symbol with archive-only libk",
          "[libk][ffi][distribution]") {
    Workspace workspace;
    const auto runtime = workspace.path / "static-only runtime's files";
    copy_package(runtime, false);
    REQUIRE_FALSE(fs::exists(runtime / "libk.so"));
    const auto source = workspace.write("consumer.k", K_CONSUMER);
    const auto native_source = workspace.write("host.c", R"(
        #include <errno.h>
        int klang_ffi_distribution_host(int value) {
            errno = 37;
            return value + 2;
        }
    )");
    const auto object = workspace.path / "host.o";
    const auto output = workspace.path / "k-consumer";
    check_success(k::tools::lookup_run_process("clang",
        {"-c", "-fPIC", native_source.string(), "-o", object.string()}));
    REQUIRE(fs::path(KLANGC_PATH).is_absolute());
    std::vector<std::string> args = {
        "--no-lib-path-env", "-I", runtime.string(), "-I", LIBK_KDI_DIR,
        "-L", runtime.string(), "--export-symbol", "klang_ffi_distribution_host",
        source.string(), object.string(), "-o", output.string()
    };
    append_native_dependencies(args, runtime / "libk.pc");
    {
        // Current-directory resolution precedes -I and the configured shared runtime.
        ScopedCurrentDirectory directory(runtime);
        check_success(k::tools::run_process(KLANGC_PATH, args));
    }
    check_runtime_dependency(output, false);
    check_success(k::tools::run_process(output.string()));
}
