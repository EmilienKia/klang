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
 * Tests for the 'using' directive.
 *
 * Covered scenarios:
 *  - 'using namespace X::Y;' — all members of X::Y are resolvable as if
 *    they were direct members of the enclosing scope (functions, variables,
 *    nested types).
 *  - 'using X::Y::foo;' — only 'foo' is injected into the current scope.
 *  - using in declaration context (namespace body, struct body).
 *  - using in statement context (function body / block).
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

// ── using namespace — inject all members ────────────────────────────────────

TEST_CASE("Using namespace — call function from nested namespace", "[gen][using]") {
    auto jit = gen_jit(R"SRC(
        module __using_ns_func__;
        namespace math {
            add(a : int, b : int) : int {
                return a + b;
            }
        }
        using namespace math;
        test() : int {
            return add(10, 32);
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

TEST_CASE("Using namespace — access global variable from nested namespace", "[gen][using]") {
    auto jit = gen_jit(R"SRC(
        module __using_ns_var__;
        namespace vals {
            answer : int = 42;
        }
        using namespace vals;
        test() : int {
            return answer;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

TEST_CASE("Using namespace — resolve type from nested namespace", "[gen][using]") {
    auto jit = gen_jit(R"SRC(
        module __using_ns_type__;
        namespace geom {
            struct Point {
                x : int;
                y : int;
            }
        }
        using namespace geom;
        test() : int {
            p : Point;
            p.x = 10;
            p.y = 32;
            return p.x + p.y;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

// ── using specific element ──────────────────────────────────────────────────

TEST_CASE("Using specific function — call injected function", "[gen][using]") {
    auto jit = gen_jit(R"SRC(
        module __using_specific_func__;
        namespace math {
            mul(a : int, b : int) : int {
                return a * b;
            }
            other() : int {
                return 999;
            }
        }
        using math::mul;
        test() : int {
            return mul(6, 7);
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

TEST_CASE("Using specific type — resolve injected struct", "[gen][using]") {
    auto jit = gen_jit(R"SRC(
        module __using_specific_type__;
        namespace geom {
            struct Vec2 {
                x : int;
                y : int;
            }
        }
        using geom::Vec2;
        test() : int {
            v : Vec2;
            v.x = 20;
            v.y = 22;
            return v.x + v.y;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

// ── using inside a function body (statement context) ────────────────────────

TEST_CASE("Using namespace inside function body", "[gen][using]") {
    auto jit = gen_jit(R"SRC(
        module __using_in_func__;
        namespace helpers {
            double_it(x : int) : int {
                return x * 2;
            }
        }
        test() : int {
            using namespace helpers;
            return double_it(21);
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

TEST_CASE("Using specific function inside function body", "[gen][using]") {
    auto jit = gen_jit(R"SRC(
        module __using_in_func_specific__;
        namespace ops {
            sub(a : int, b : int) : int {
                return a - b;
            }
        }
        test() : int {
            using ops::sub;
            return sub(50, 8);
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

// ── using with deeply nested namespaces ─────────────────────────────────────

TEST_CASE("Using namespace — deeply nested", "[gen][using]") {
    auto jit = gen_jit(R"SRC(
        module __using_deep__;
        namespace a {
            namespace b {
                namespace c {
                    value() : int {
                        return 42;
                    }
                }
            }
        }
        using namespace a::b::c;
        test() : int {
            return value();
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

// ── multiple using directives ───────────────────────────────────────────────

TEST_CASE("Multiple using namespace directives", "[gen][using]") {
    auto jit = gen_jit(R"SRC(
        module __using_multi__;
        namespace alpha {
            get_a() : int {
                return 20;
            }
        }
        namespace beta {
            get_b() : int {
                return 22;
            }
        }
        using namespace alpha;
        using namespace beta;
        test() : int {
            return get_a() + get_b();
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

// ── using with static struct members ────────────────────────────────────────

TEST_CASE("Using namespace — call static function of struct in namespace", "[gen][using][static]") {
    auto jit = gen_jit(R"SRC(
        module __using_static_func__;
        namespace math {
            struct Calculator {
                static add(a : int, b : int) : int {
                    return a + b;
                }
            }
        }
        using namespace math;
        test() : int {
            return Calculator::add(10, 32);
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

TEST_CASE("Using namespace — access static variable of struct in namespace", "[gen][using][static]") {
    auto jit = gen_jit(R"SRC(
        module __using_static_var__;
        namespace config {
            struct Settings {
                static value : int = 42;
            }
        }
        using namespace config;
        test() : int {
            return Settings::value;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

TEST_CASE("Using specific struct — call static method via injected type", "[gen][using][static]") {
    auto jit = gen_jit(R"SRC(
        module __using_specific_static_func__;
        namespace tools {
            struct Converter {
                static double_it(x : int) : int {
                    return x * 2;
                }
            }
        }
        using tools::Converter;
        test() : int {
            return Converter::double_it(21);
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

TEST_CASE("Using specific struct — access static variable via injected type", "[gen][using][static]") {
    auto jit = gen_jit(R"SRC(
        module __using_specific_static_var__;
        namespace data {
            struct Constants {
                static answer : int = 42;
            }
        }
        using data::Constants;
        test() : int {
            return Constants::answer;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

TEST_CASE("Using namespace inside function — call static method of struct", "[gen][using][static]") {
    auto jit = gen_jit(R"SRC(
        module __using_func_static_method__;
        namespace ops {
            struct Math {
                static negate(x : int) : int {
                    return 0 - x;
                }
            }
        }
        test() : int {
            using namespace ops;
            return Math::negate(0 - 42);
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

TEST_CASE("Using namespace — static method and instance method of struct", "[gen][using][static]") {
    auto jit = gen_jit(R"SRC(
        module __using_static_and_instance__;
        namespace geom {
            struct Rect {
                w : int;
                h : int;
                static create(pw : int, ph : int) : Rect {
                    r : Rect;
                    r.w = pw;
                    r.h = ph;
                    return r;
                }
                area() : int {
                    return w * h;
                }
            }
        }
        using namespace geom;
        test() : int {
            r : Rect = Rect::create(6, 7);
            return r.area();
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

TEST_CASE("Using namespace — write and read static variable of struct", "[gen][using][static]") {
    auto jit = gen_jit(R"SRC(
        module __using_static_var_rw__;
        namespace state {
            struct Counter {
                static count : int = 0;
                static increment() : int {
                    count = count + 1;
                    return count;
                }
            }
        }
        using namespace state;
        test() : int {
            Counter::count = 40;
            Counter::increment();
            Counter::increment();
            return Counter::count;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

// ── using with imported libraries ───────────────────────────────────────────

TEST_CASE("Using namespace — imported module function", "[gen][using][import]") {
    auto result = build_exec_with_lib(
        R"K(
            module mathlib;
            add(a: int, b: int) : int { return a + b; }
        )K",
        R"K(
            module consumer;
            import mathlib;
            using namespace mathlib;
            main() : int {
                return add(10, 32);
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);

    REQUIRE(result.exit_code == 42);
}

TEST_CASE("Using specific function — imported module", "[gen][using][import]") {
    auto result = build_exec_with_lib(
        R"K(
            module arithlib;
            mul(a: int, b: int) : int { return a * b; }
            other() : int { return 999; }
        )K",
        R"K(
            module consumer;
            import arithlib;
            using arithlib::mul;
            main() : int {
                return mul(6, 7);
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);

    REQUIRE(result.exit_code == 42);
}

TEST_CASE("Using namespace — imported module struct with method", "[gen][using][import]") {
    auto result = build_exec_with_lib(
        R"K(
            module shapelib;
            struct Point {
                x : int;
                y : int;
                sum() : int { return this.x + this.y; }
            }
        )K",
        R"K(
            module consumer;
            import shapelib;
            using namespace shapelib;
            main() : int {
                p : Point;
                p.x = 20;
                p.y = 22;
                return p.sum();
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);

    REQUIRE(result.exit_code == 42);
}

TEST_CASE("Using specific struct — imported module", "[gen][using][import]") {
    auto result = build_exec_with_lib(
        R"K(
            module veclib;
            struct Vec2 {
                x : int;
                y : int;
            }
        )K",
        R"K(
            module consumer;
            import veclib;
            using veclib::Vec2;
            main() : int {
                v : Vec2;
                v.x = 20;
                v.y = 22;
                return v.x + v.y;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);

    REQUIRE(result.exit_code == 42);
}

TEST_CASE("Using namespace — imported module with static struct method", "[gen][using][import][static]") {
    auto result = build_exec_with_lib(
        R"K(
            module calclib;
            struct Calculator {
                static compute(a: int, b: int) : int { return a + b; }
            }
        )K",
        R"K(
            module consumer;
            import calclib;
            using namespace calclib;
            main() : int {
                return Calculator::compute(10, 32);
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);

    REQUIRE(result.exit_code == 42);
}

TEST_CASE("Using namespace — imported module with multiple members", "[gen][using][import]") {
    std::vector<LibSpec> libs = {
        {R"K(
            module alpha_lib;
            get_alpha() : int { return 20; }
        )K"},
        {R"K(
            module beta_lib;
            get_beta() : int { return 22; }
        )K"}
    };
    auto result = build_exec_with_libs(libs,
        R"K(
            module consumer;
            import alpha_lib;
            import beta_lib;
            using namespace alpha_lib;
            using namespace beta_lib;
            main() : int {
                return get_alpha() + get_beta();
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);

    REQUIRE(result.exit_code == 42);
}

TEST_CASE("Using inside function body — imported module function", "[gen][using][import]") {
    auto result = build_exec_with_lib(
        R"K(
            module helperlib;
            triple(x: int) : int { return x * 3; }
        )K",
        R"K(
            module consumer;
            import helperlib;
            main() : int {
                using namespace helperlib;
                return triple(14);
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);

    REQUIRE(result.exit_code == 42);
}

