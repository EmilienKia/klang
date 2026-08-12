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

// ═════════════════════════════════════════════════════════════════════════════
// 6. Phase 4 — abstract ::k::Application chain: single abstract standard main
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Application chain — single abstract standard main is implemented by the final class",
          "[libk][application][application-chain]") {
    auto jit = jit_k(R"SRC(
        module __app_chain_single__;
        import k;

        abstract class Layer1 : public k::Application {
            public main() : int -> delete;
            public main(args : const String[]) -> delete;
            public main(args : const String[]) : int -> delete;
            public abstract main() : int;
        }

        class Application : public Layer1 {
            public main() : int {
                return 42;
            }
        }
        )SRC");
    REQUIRE(jit);

    static const int argc = 1;
    static const char* argv[] = {"prog", nullptr};
    auto main = jit->lookup_main_entry_symbol< int(*)(int, char**) >();
    REQUIRE(main(argc, (char**)argv) == 42);
}

// ═════════════════════════════════════════════════════════════════════════════
// 7. Phase 4 — delegating standard main + custom abstract main (single level)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Application chain — delegating implementation invokes a custom abstract main",
          "[libk][application][application-chain]") {
    auto jit = jit_k(R"SRC(
        module __app_chain_delegate__;
        import k;

        abstract class Layer1 : public k::Application {
            public main() -> delete;
            public main() : int -> delete;
            public main(args : const String[]) : int -> delete;
            public main(args : const String[]) : int {
                return main(args.size);
            }
            protected abstract main(n : int) : int;
        }

        class Application : public Layer1 {
            protected main(n : int) : int {
                return 100 + n;
            }
        }
        )SRC");
    REQUIRE(jit);

    static const int argc = 3;
    static const char* argv[] = {"prog", "a", "b", nullptr};
    auto main = jit->lookup_main_entry_symbol< int(*)(int, char**) >();
    REQUIRE(main(argc, (char**)argv) == 103);
}

// ═════════════════════════════════════════════════════════════════════════════
// 8. Phase 4 — multi-level abstract chain (2 abstract classes)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Application chain — two-level abstract chain dispatches virtually end to end",
          "[libk][application][application-chain]") {
    auto jit = jit_k(R"SRC(
        module __app_chain_multilevel__;
        import k;

        abstract class Layer1 : public k::Application {
            public main() : int -> delete;
            public main(args : const String[]) -> delete;
            public main(args : const String[]) : int -> delete;
            public abstract main() : int;
        }

        abstract class Layer2 : public Layer1 {
            public main() : int {
                return main(1) + 1;
            }
            protected abstract main(code : int) : int;
        }

        class Application : public Layer2 {
            protected main(code : int) : int {
                return 97 + code;
            }
        }
        )SRC");
    REQUIRE(jit);

    static const int argc = 1;
    static const char* argv[] = {"prog", nullptr};
    auto main = jit->lookup_main_entry_symbol< int(*)(int, char**) >();
    REQUIRE(main(argc, (char**)argv) == 99);
}

// ═════════════════════════════════════════════════════════════════════════════
// 9. Phase 4 — validation errors
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Application chain — all four standard mains deleted without a custom delegate is an error",
          "[libk][application][application-chain]") {
    auto jit = jit_k(R"SRC(
        module __app_chain_err_all_deleted__;
        import k;

        abstract class Layer1 : public k::Application {
            public main() -> delete;
            public main() : int -> delete;
            public main(args : const String[]) -> delete;
            public main(args : const String[]) : int -> delete;
        }

        class Application : public Layer1 {
            public main() {
            }
        }
        )SRC");
    REQUIRE_FALSE(jit);
}

TEST_CASE("Application chain — delegating main without a paired custom abstract main is an error",
          "[libk][application][application-chain]") {
    auto jit = jit_k(R"SRC(
        module __app_chain_err_no_delegate__;
        import k;

        abstract class Layer1 : public k::Application {
            public main() -> delete;
            public main() : int -> delete;
            public main(args : const String[]) : int -> delete;
            public main(args : const String[]) {
            }
        }

        class Application : public Layer1 {
        }
        )SRC");
    REQUIRE_FALSE(jit);
}

TEST_CASE("Application chain — final concrete class not implementing the required main is an error",
          "[libk][application][application-chain]") {
    auto jit = jit_k(R"SRC(
        module __app_chain_err_final_missing__;
        import k;

        abstract class Layer1 : public k::Application {
            public main() : int -> delete;
            public main(args : const String[]) -> delete;
            public main(args : const String[]) : int -> delete;
            public abstract main() : int;
        }

        class Application : public Layer1 {
            public main() : int -> delete;
        }
        )SRC");
    REQUIRE_FALSE(jit);
}
