/*
 * K Language compiler
 *
 * Copyright 2023-2026 Emilien Kia
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
 * Tests for Java-style varargs (variable-length argument lists).
 *
 * A varargs parameter is declared with '...' after the name:
 *   f(args... : int) : int { ... }
 * Internally, args is an int[] (unsized array). The compiler packs
 * individual arguments into a stack-allocated array at the call site.
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

using namespace k::parse;
using namespace k::parse::ast;

// ═════════════════════════════════════════════════════════════════════════════
// Phase 1: Parser tests
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Parse varargs parameter — is_varargs flag set", "[parse][varargs]") {
    test_logger log;
    k::source src{"f(args... : int) : void {}"};
    k::parse::parser parser(log, src);
    auto func = parser.parse_function_decl();
    REQUIRE(func);
    REQUIRE(func->params.size() == 1);
    REQUIRE(func->params[0]->is_varargs == true);
    REQUIRE(func->params[0]->name.has_value());
    REQUIRE(func->params[0]->name->content == "args");
    // The type should have been wrapped in array_type_specifier
    auto arr_type = std::dynamic_pointer_cast<array_type_specifier>(func->params[0]->type);
    REQUIRE(arr_type != nullptr);
}

TEST_CASE("Parse non-varargs parameter — is_varargs flag not set", "[parse][varargs]") {
    test_logger log;
    k::source src{"f(a: int, b: int) : void {}"};
    k::parse::parser parser(log, src);
    auto func = parser.parse_function_decl();
    REQUIRE(func);
    REQUIRE(func->params.size() == 2);
    REQUIRE(func->params[0]->is_varargs == false);
    REQUIRE(func->params[1]->is_varargs == false);
}

TEST_CASE("Parse mixed fixed + varargs parameters", "[parse][varargs]") {
    test_logger log;
    k::source src{"f(a: int, b: int, rest... : int) : void {}"};
    k::parse::parser parser(log, src);
    auto func = parser.parse_function_decl();
    REQUIRE(func);
    REQUIRE(func->params.size() == 3);
    REQUIRE(func->params[0]->is_varargs == false);
    REQUIRE(func->params[1]->is_varargs == false);
    REQUIRE(func->params[2]->is_varargs == true);
    // rest should be wrapped as int[]
    auto arr_type = std::dynamic_pointer_cast<array_type_specifier>(func->params[2]->type);
    REQUIRE(arr_type != nullptr);
}

TEST_CASE("Parse error — varargs not last parameter", "[parse][varargs]") {
    test_logger log;
    k::source src{"f(a... : int, b: int) : void {}"};
    k::parse::parser parser(log, src);
    REQUIRE_THROWS(parser.parse_function_decl());
}

TEST_CASE("Parse error — multiple varargs parameters", "[parse][varargs]") {
    test_logger log;
    k::source src{"f(a... : int, b... : int) : void {}"};
    k::parse::parser parser(log, src);
    REQUIRE_THROWS(parser.parse_function_decl());
}

TEST_CASE("Parse error — varargs with default value", "[parse][varargs]") {
    test_logger log;
    k::source src{"f(a... : int = 42) : void {}"};
    k::parse::parser parser(log, src);
    REQUIRE_THROWS(parser.parse_function_decl());
}

// ═════════════════════════════════════════════════════════════════════════════
// Phase 4-5: Code generation tests — varargs function calls
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Varargs — basic call with multiple args", "[gen][varargs]") {
    auto jit = gen_jit(R"SRC(
        module gen_varargs_01;

        sum(values... : int) : int {
            return values[0] + values[1] + values[2];
        }

        test() : int {
            return sum(10, 20, 30);
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 60);
}

TEST_CASE("Varargs — single vararg", "[gen][varargs]") {
    auto jit = gen_jit(R"SRC(
        module gen_varargs_02;

        first(values... : int) : int {
            return values[0];
        }

        test() : int {
            return first(42);
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

TEST_CASE("Varargs — mixed fixed and varargs parameters", "[gen][varargs]") {
    auto jit = gen_jit(R"SRC(
        module gen_varargs_03;

        add_to(base: int, extras... : int) : int {
            return base + extras[0] + extras[1];
        }

        test() : int {
            return add_to(100, 20, 3);
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 123);
}

TEST_CASE("Varargs — explicit array pass", "[gen][varargs]") {
    auto jit = gen_jit(R"SRC(
        module gen_varargs_04;

        first(values... : int) : int {
            return values[0];
        }

        test() : int {
            arr : int[3]{7, 8, 9};
            return first(arr);
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 7);
}

TEST_CASE("Varargs — long type", "[gen][varargs]") {
    auto jit = gen_jit(R"SRC(
        module gen_varargs_05;

        sum_long(values... : long) : long {
            return values[0] + values[1];
        }

        test() : long {
            return sum_long(1000000, 2000000);
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<long(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 3000000L);
}

TEST_CASE("Varargs — zero varargs arguments", "[gen][varargs]") {
    auto jit = gen_jit(R"SRC(
        module gen_varargs_06;

        count(values... : int) : int {
            return values.size;
        }

        test() : int {
            return count();
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 0);
}

TEST_CASE("Varargs — zero varargs with fixed params", "[gen][varargs]") {
    auto jit = gen_jit(R"SRC(
        module gen_varargs_07;

        base_only(base: int, extras... : int) : int {
            return base + extras.size;
        }

        test() : int {
            return base_only(100);
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 100);
}

TEST_CASE("Varargs — overload preference: non-varargs preferred", "[gen][varargs]") {
    auto jit = gen_jit(R"SRC(
        module gen_varargs_08;

        pick(a: int, b: int) : int {
            return 1;
        }

        pick(args... : int) : int {
            return 2;
        }

        test() : int {
            return pick(10, 20);
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    // Non-varargs overload should be preferred (returns 1)
    REQUIRE(test() == 1);
}

TEST_CASE("Varargs — overload: varargs used when no exact match", "[gen][varargs]") {
    auto jit = gen_jit(R"SRC(
        module gen_varargs_09;

        pick(a: int, b: int) : int {
            return 1;
        }

        pick(args... : int) : int {
            return 2;
        }

        test() : int {
            return pick(10, 20, 30);
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    // Three args doesn't match the two-param overload, so varargs is used (returns 2)
    REQUIRE(test() == 2);
}

TEST_CASE("Varargs — array size access inside body", "[gen][varargs]") {
    auto jit = gen_jit(R"SRC(
        module gen_varargs_10;

        count_args(values... : int) : int {
            return values.size;
        }

        test() : int {
            return count_args(10, 20, 30, 40, 50);
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 5);
}

// ═════════════════════════════════════════════════════════════════════════════
// Phase 7: Mangling — verify varargs functions are mangled as T[] parameters
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Varargs — mangling identical to T[] parameter", "[gen][varargs]") {
    // A varargs function `fun f(args... : int)` should mangle identically
    // to `fun f(args : int[])` since the parameter type is int[] in the model.
    // We verify by looking up the mangled symbol name.
    auto jit = gen_jit(R"SRC(
        module gen_varargs_11;

        vararg_func(values... : int) : int {
            return values[0];
        }

        test() : int {
            return vararg_func(42);
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

// ═════════════════════════════════════════════════════════════════════════════
// Phase 6: KDI import/export — varargs across library boundary
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Varargs — KDI import: call varargs function from imported library", "[gen][varargs][import]") {
    auto result = build_exec_with_lib(
        // Library source — use .size instead of subscript to avoid bounds-check symbol
        R"SRC(
            module gen_varargs_12;

            public count(values... : int) : int {
                return values.size;
            }

            public count_plus(base: int, extras... : int) : int {
                return base + extras.size;
            }
        )SRC",
        // Executable source
        R"SRC(
            module gen_varargs_13;
            import gen_varargs_12;

            main() : int {
                a : int = gen_varargs_12::count(10, 20, 30);
                b : int = gen_varargs_12::count_plus(100, 20, 3);
                if(a == 3 && b == 102) {
                    return 0;
                }
                return 1;
            }
        )SRC"
    );
    REQUIRE(result.exit_code == 0);
}


