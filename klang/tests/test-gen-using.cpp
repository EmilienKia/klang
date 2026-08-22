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
        module gen_using_01;
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
        module gen_using_02;
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
        module gen_using_03;
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
        module gen_using_04;
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
        module gen_using_05;
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
        module gen_using_06;
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
        module gen_using_07;
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
        module gen_using_08;
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
        module gen_using_09;
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
        module gen_using_10;
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
        module gen_using_11;
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
        module gen_using_12;
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
        module gen_using_13;
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
        module gen_using_14;
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
        module gen_using_15;
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
        module gen_using_16;
        namespace state {
            struct Counter {
                static count : int = 0;
                static increment() : int {
                    ++count;
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
            module gen_using_17;
            add(a: int, b: int) : int { return a + b; }
        )K",
        R"K(
            module gen_using_18;
            import gen_using_17;
            using namespace gen_using_17;
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
            module gen_using_19;
            mul(a: int, b: int) : int { return a * b; }
            other() : int { return 999; }
        )K",
        R"K(
            module gen_using_20;
            import gen_using_19;
            using gen_using_19::mul;
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
            module gen_using_21;
            struct Point {
                x : int;
                y : int;
                sum() : int { return this.x + this.y; }
            }
        )K",
        R"K(
            module gen_using_22;
            import gen_using_21;
            using namespace gen_using_21;
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
            module gen_using_23;
            struct Vec2 {
                x : int;
                y : int;
            }
        )K",
        R"K(
            module gen_using_24;
            import gen_using_23;
            using gen_using_23::Vec2;
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
            module gen_using_25;
            struct Calculator {
                static compute(a: int, b: int) : int { return a + b; }
            }
        )K",
        R"K(
            module gen_using_26;
            import gen_using_25;
            using namespace gen_using_25;
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
            module gen_using_27;
            get_alpha() : int { return 20; }
        )K"},
        {R"K(
            module gen_using_28;
            get_beta() : int { return 22; }
        )K"}
    };
    auto result = build_exec_with_libs(libs,
        R"K(
            module gen_using_29;
            import gen_using_27;
            import gen_using_28;
            using namespace gen_using_27;
            using namespace gen_using_28;
            main() : int {
                return get_alpha() + get_beta();
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);

    REQUIRE(result.exit_code == 42);
}

TEST_CASE("Imported overload resolution — unqualified call uses best imported overload",
          "[gen][using][import][overload][regression]") {
    auto result = build_exec_with_lib(
        R"K(
            module gen_using_30;
            enum ErrA { a1; }
            enum ErrB { b1; }
            f(x: ErrA) : int { return 11; }
            f(x: ErrB) : int { return 42; }
        )K",
        R"K(
            module gen_using_31;
            import gen_using_30;
            main() : int {
                return f(gen_using_30::ErrB::b1);
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);

    REQUIRE(result.exit_code == 42);
}

TEST_CASE("Imported overload resolution — qualified call uses best imported overload",
          "[gen][using][import][overload][regression]") {
    auto result = build_exec_with_lib(
        R"K(
            module gen_using_32;
            enum ErrA { a1; }
            enum ErrB { b1; }
            f(x: ErrA) : int { return 11; }
            f(x: ErrB) : int { return 42; }
        )K",
        R"K(
            module gen_using_33;
            import gen_using_32;
            main() : int {
                return gen_using_32::f(gen_using_32::ErrB::b1);
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);

    REQUIRE(result.exit_code == 42);
}

TEST_CASE("Using inside function body — imported module function", "[gen][using][import]") {
    auto result = build_exec_with_lib(
        R"K(
            module gen_using_34;
            triple(x: int) : int { return x * 3; }
        )K",
        R"K(
            module gen_using_35;
            import gen_using_34;
            main() : int {
                using namespace gen_using_34;
                return triple(14);
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);

    REQUIRE(result.exit_code == 42);
}

// ── using alias — function alias ────────────────────────────────────────────

TEST_CASE("Using alias — function alias", "[gen][using][alias]") {
    auto jit = gen_jit(R"SRC(
        module gen_using_36;
        namespace math {
            add(a : int, b : int) : int {
                return a + b;
            }
        }
        using sum = math::add;
        test() : int {
            return sum(10, 32);
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

TEST_CASE("Using alias — variable alias", "[gen][using][alias]") {
    auto jit = gen_jit(R"SRC(
        module gen_using_37;
        namespace config {
            answer : int = 42;
        }
        using the_answer = config::answer;
        test() : int {
            return the_answer;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

// ── using alias — type alias ────────────────────────────────────────────────

TEST_CASE("Using alias — type alias for struct", "[gen][using][alias]") {
    auto jit = gen_jit(R"SRC(
        module gen_using_38;
        namespace geom {
            struct Vector2 {
                x : int;
                y : int;
            }
        }
        using Vec2 = geom::Vector2;
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

TEST_CASE("Using alias — type alias with method call", "[gen][using][alias]") {
    auto jit = gen_jit(R"SRC(
        module gen_using_39;
        namespace shapes {
            struct Rect {
                w : int;
                h : int;
                area() : int { return w * h; }
            }
        }
        using Box = shapes::Rect;
        test() : int {
            b : Box;
            b.w = 6;
            b.h = 7;
            return b.area();
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

// ── using alias — namespace alias ───────────────────────────────────────────

TEST_CASE("Using alias — namespace alias, access function via prefix", "[gen][using][alias]") {
    auto jit = gen_jit(R"SRC(
        module gen_using_40;
        namespace very_long_namespace_name {
            compute(x : int) : int {
                return x * 2;
            }
        }
        using namespace ns = very_long_namespace_name;
        test() : int {
            return ns::compute(21);
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

TEST_CASE("Using alias — namespace alias, access type via prefix", "[gen][using][alias]") {
    auto jit = gen_jit(R"SRC(
        module gen_using_41;
        namespace geo {
            struct Point {
                x : int;
                y : int;
            }
        }
        using namespace g = geo;
        test() : int {
            p : g::Point;
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

TEST_CASE("Using alias — namespace alias, access variable via prefix", "[gen][using][alias]") {
    auto jit = gen_jit(R"SRC(
        module gen_using_42;
        namespace globals {
            value : int = 42;
        }
        using namespace g = globals;
        test() : int {
            return g::value;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

TEST_CASE("Using alias — namespace alias for deeply nested namespace", "[gen][using][alias]") {
    auto jit = gen_jit(R"SRC(
        module gen_using_43;
        namespace a {
            namespace b {
                namespace c {
                    get_val() : int {
                        return 42;
                    }
                }
            }
        }
        using namespace deep = a::b::c;
        test() : int {
            return deep::get_val();
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

// ── using alias — static struct members ─────────────────────────────────────

TEST_CASE("Using alias — alias struct with static method access", "[gen][using][alias][static]") {
    auto jit = gen_jit(R"SRC(
        module gen_using_44;
        namespace tools {
            struct Calculator {
                static add(a : int, b : int) : int {
                    return a + b;
                }
            }
        }
        using Calc = tools::Calculator;
        test() : int {
            return Calc::add(10, 32);
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

TEST_CASE("Using alias — alias struct with static variable access", "[gen][using][alias][static]") {
    auto jit = gen_jit(R"SRC(
        module gen_using_45;
        namespace data {
            struct Config {
                static val : int = 42;
            }
        }
        using Cfg = data::Config;
        test() : int {
            return Cfg::val;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

TEST_CASE("Using alias — namespace alias gives access to struct static members", "[gen][using][alias][static]") {
    auto jit = gen_jit(R"SRC(
        module gen_using_46;
        namespace math {
            struct Ops {
                static mul(a : int, b : int) : int {
                    return a * b;
                }
            }
        }
        using namespace m = math;
        test() : int {
            return m::Ops::mul(6, 7);
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

// ── using alias — inside function body ──────────────────────────────────────

TEST_CASE("Using alias — function alias inside function body", "[gen][using][alias]") {
    auto jit = gen_jit(R"SRC(
        module gen_using_47;
        namespace ops {
            negate(x : int) : int {
                return 0 - x;
            }
        }
        test() : int {
            using neg = ops::negate;
            return neg(0 - 42);
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

TEST_CASE("Using alias — namespace alias inside function body", "[gen][using][alias]") {
    auto jit = gen_jit(R"SRC(
        module gen_using_48;
        namespace helpers {
            double_it(x : int) : int {
                return x * 2;
            }
        }
        test() : int {
            using namespace h = helpers;
            return h::double_it(21);
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

TEST_CASE("Using alias — type alias inside function body", "[gen][using][alias]") {
    auto jit = gen_jit(R"SRC(
        module gen_using_49;
        namespace geom {
            struct Vec {
                x : int;
                y : int;
            }
        }
        test() : int {
            using V = geom::Vec;
            v : V;
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

// ── using alias — with imported libraries ───────────────────────────────────

TEST_CASE("Using alias — function alias from imported module", "[gen][using][alias][import]") {
    auto result = build_exec_with_lib(
        R"K(
            module gen_using_50;
            add(a: int, b: int) : int { return a + b; }
        )K",
        R"K(
            module gen_using_51;
            import gen_using_50;
            using sum = gen_using_50::add;
            main() : int {
                return sum(10, 32);
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);

    REQUIRE(result.exit_code == 42);
}

TEST_CASE("Using alias — type alias from imported module", "[gen][using][alias][import]") {
    auto result = build_exec_with_lib(
        R"K(
            module gen_using_52;
            struct Point {
                x : int;
                y : int;
            }
        )K",
        R"K(
            module gen_using_53;
            import gen_using_52;
            using Pt = gen_using_52::Point;
            main() : int {
                p : Pt;
                p.x = 20;
                p.y = 22;
                return p.x + p.y;
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);

    REQUIRE(result.exit_code == 42);
}

TEST_CASE("Using alias — namespace alias for imported module", "[gen][using][alias][import]") {
    auto result = build_exec_with_lib(
        R"K(
            module gen_using_54;
            compute(x: int) : int { return x * 2; }
        )K",
        R"K(
            module gen_using_55;
            import gen_using_54;
            using namespace m = gen_using_54;
            main() : int {
                return m::compute(21);
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);

    REQUIRE(result.exit_code == 42);
}

TEST_CASE("Using alias — namespace alias for imported module with struct", "[gen][using][alias][import]") {
    auto result = build_exec_with_lib(
        R"K(
            module gen_using_56;
            struct Circle {
                r : int;
                diameter() : int { return this.r * 2; }
            }
        )K",
        R"K(
            module gen_using_57;
            import gen_using_56;
            using namespace g = gen_using_56;
            main() : int {
                c : g::Circle;
                c.r = 21;
                return c.diameter();
            }
        )K");

    if (!result.out.empty()) INFO("stdout: " << result.out);
    if (!result.err.empty()) INFO("stderr: " << result.err);

    REQUIRE(result.exit_code == 42);
}

// ── using alias — original name still works ─────────────────────────────────

TEST_CASE("Using alias — original qualified name still works alongside alias", "[gen][using][alias]") {
    auto jit = gen_jit(R"SRC(
        module gen_using_58;
        namespace math {
            add(a : int, b : int) : int {
                return a + b;
            }
        }
        using sum = math::add;
        test() : int {
            r1 : int = sum(10, 12);
            r2 : int = math::add(10, 10);
            return r1 + r2;
        }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}
