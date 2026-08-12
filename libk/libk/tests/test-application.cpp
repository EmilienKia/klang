/*
 * K Language standard library — ::k::Application / main(args) tests
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
 * Tests for the `main(args : const String[])` entry-point form and the
 * compiler-synthesised ::k::Application class (env() accessor).
 *
 * These tests JIT-compile small K programs that `import k;` and define a
 * top-level `main` function. The compiler synthesises a private `Application`
 * class (extending ::k::Application) in the module, moves `main` into it, and
 * generates a C-ABI `main(int argc, char** argv)` proxy that constructs the
 * Application instance, optionally builds the `args` array from argv, invokes
 * `main`, and returns the appropriate exit code.
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

#ifndef LIBK_KDI_DIR
#error "LIBK_KDI_DIR must be defined — check CMakeLists.txt"
#endif
#ifndef LIBK_LIB_DIR
#error "LIBK_LIB_DIR must be defined — check CMakeLists.txt"
#endif

namespace {

std::unique_ptr<k::model::gen::jit> jit_k(std::string_view src) {
    return gen_jit_with_stdlib(src, LIBK_KDI_DIR, LIBK_LIB_DIR);
}

} // anonymous namespace


// ═════════════════════════════════════════════════════════════════════════════
// 1. main() with no parameters, no return type
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Application — main() with no args, no return", "[libk][application]") {
    auto jit = jit_k(R"SRC(
        module __app_main_void__;
        import k;

        main() {
        }
        )SRC");
    REQUIRE(jit);

    static const int argc = 1;
    static const char* argv[] = {"prog", nullptr};

    auto main = jit->lookup_main_entry_symbol< int(*)(int, char**) >();
    auto res = main(argc, (char**)argv);
    REQUIRE(res == 0);
}

// ═════════════════════════════════════════════════════════════════════════════
// 2. main() : int with no parameters
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Application — main() : int with no args", "[libk][application]") {
    auto jit = jit_k(R"SRC(
        module __app_main_int__;
        import k;

        main() : int {
            return 42;
        }
        )SRC");
    REQUIRE(jit);

    static const int argc = 1;
    static const char* argv[] = {"prog", nullptr};

    auto main = jit->lookup_main_entry_symbol< int(*)(int, char**) >();
    auto res = main(argc, (char**)argv);
    REQUIRE(res == 42);
}

// ═════════════════════════════════════════════════════════════════════════════
// 3. main(args : const String[]) — argc/argv propagation, no return type
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Application — main(args) receives argv as String[]", "[libk][application]") {
    auto jit = jit_k(R"SRC(
        module __app_main_args__;
        import k;

        argCount : int = -1;
        firstArg : String;

        main(args : const String[]) {
            argCount = args.size;
            if (args.size > 0u) {
                firstArg = args[0];
            }
        }
        )SRC");
    REQUIRE(jit);

    static const int argc = 3;
    static const char* argv[] = {"myprog", "hello", "world", nullptr};

    auto main = jit->lookup_main_entry_symbol< int(*)(int, char**) >();
    auto res = main(argc, (char**)argv);
    REQUIRE(res == 0);

    auto argCount = jit->lookup_symbol< int* >("argCount");
    REQUIRE(*argCount == 3);
}

// ═════════════════════════════════════════════════════════════════════════════
// 4. main(args : const String[]) : int — argc/argv propagation with return value
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Application — main(args) : int returns computed exit code", "[libk][application]") {
    auto jit = jit_k(R"SRC(
        module __app_main_args_int__;
        import k;

        main(args : const String[]) : int {
            return args.size;
        }
        )SRC");
    REQUIRE(jit);

    static const int argc = 4;
    static const char* argv[] = {"myprog", "a", "b", "c", nullptr};

    auto main = jit->lookup_main_entry_symbol< int(*)(int, char**) >();
    auto res = main(argc, (char**)argv);
    REQUIRE(res == 4);
}

// ═════════════════════════════════════════════════════════════════════════════
// 5. ::k::Application::env() — process environment variable access
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Application — env() exposes process environment variables", "[libk][application]") {
    setenv("KLANG_TEST_APP_ENV_VAR", "hello_from_env", 1);

    auto jit = jit_k(R"SRC(
        module __app_env__;
        import k;

        found : bool = false;

        main() {
            e : const EnvironmentMap& = env();
            v : OptionalConstRef<String> = e.get(String("KLANG_TEST_APP_ENV_VAR"));
            if (v.hasValue()) {
                if (v.get() == String("hello_from_env")) {
                    found = true;
                }
            }
        }
        )SRC");
    REQUIRE(jit);

    static const int argc = 1;
    static const char* argv[] = {"prog", nullptr};

    auto main = jit->lookup_main_entry_symbol< int(*)(int, char**) >();
    auto res = main(argc, (char**)argv);
    REQUIRE(res == 0);

    auto found = jit->lookup_symbol< bool* >("found");
    REQUIRE(*found == true);

    unsetenv("KLANG_TEST_APP_ENV_VAR");
}
