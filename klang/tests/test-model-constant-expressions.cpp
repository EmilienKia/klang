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

#include "helpers.hpp"
#include "../src/model/constant_value.hpp"

namespace {

std::shared_ptr<k::model::expression> get_return_expr(
    const std::shared_ptr<k::compiler>& comp,
    const std::string& func_name
) {
    if (!comp || !comp->get_unit()) return nullptr;
    auto root_ns = comp->get_unit()->get_root_namespace();
    if (!root_ns) return nullptr;
    auto func = root_ns->get_function(func_name);
    if (!func || !func->get_block()) return nullptr;
    for (auto& stmt : func->get_block()->get_statements()) {
        if (auto ret = std::dynamic_pointer_cast<k::model::return_statement>(stmt)) {
            return ret->get_expression();
        }
    }
    return nullptr;
}

} // anonymous namespace

// ════════════════════════════════════════════════════════════════════════════
//  1. Scalar literals & primitives
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Constant expression — scalar literals", "[model][const_expr][scalar]") {
    auto comp = compile_model(R"SRC(
        module __test_const_scalar__;
        test_int() : int { return 42; }
        test_bool() : bool { return true; }
        test_char() : char { return 'z'; }
    )SRC");
    REQUIRE(comp != nullptr);

    auto e_int = get_return_expr(comp, "test_int");
    REQUIRE(e_int != nullptr);
    CHECK(e_int->is_constant());
    CHECK(e_int->get_constant_value().get_int64() == 42);

    auto e_bool = get_return_expr(comp, "test_bool");
    REQUIRE(e_bool != nullptr);
    CHECK(e_bool->is_constant());
    CHECK(e_bool->get_constant_value().get_bool() == true);

    auto e_char = get_return_expr(comp, "test_char");
    REQUIRE(e_char != nullptr);
    CHECK(e_char->is_constant());
    CHECK(e_char->get_constant_value().get_int64() == 'z');
}

// ════════════════════════════════════════════════════════════════════════════
//  2. Enumerations
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Constant expression — enum entries", "[model][const_expr][enum]") {
    auto comp = compile_model(R"SRC(
        module __test_const_enum__;
        enum Color {
            Red = 1;
            Green = 2;
            Blue = 4;
        }
        test_enum() : Color { return Color::Green; }
    )SRC");
    REQUIRE(comp != nullptr);

    auto expr = get_return_expr(comp, "test_enum");
    REQUIRE(expr != nullptr);
    CHECK(expr->is_constant());
    CHECK(expr->get_constant_value().is_enum());
    CHECK(expr->get_constant_value().get_enum().raw_value == 2);
    CHECK(expr->get_constant_value().get_enum().name == "Green");
}

// ════════════════════════════════════════════════════════════════════════════
//  3. Binary and unary arithmetic operations
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Constant expression — arithmetic operations", "[model][const_expr][arithmetic]") {
    auto comp = compile_model(R"SRC(
        module __test_const_arith__;
        test_add() : int { return 10 + 20; }
        test_sub() : int { return 100 - 35; }
        test_mul() : int { return 6 * 7; }
        test_div() : int { return 42 / 2; }
        test_mod() : int { return 43 % 10; }
        test_prec() : int { return 2 + 3 * 4; }
        test_shl() : int { return 1 << 4; }
        test_band() : int { return 0xFF & 0x0F; }
        test_bor() : int { return 0x10 | 0x01; }
        test_bxor() : int { return 0x11 ^ 0x01; }
        test_neg() : int { return -42; }
        test_bnot() : int { return ~0; }
    )SRC");
    REQUIRE(comp != nullptr);

    auto check_fn = [&](const std::string& name, int64_t expected) {
        auto expr = get_return_expr(comp, name);
        REQUIRE(expr != nullptr);
        CHECK(expr->is_constant());
        CHECK(expr->get_constant_value().get_int64() == expected);
    };

    check_fn("test_add", 30);
    check_fn("test_sub", 65);
    check_fn("test_mul", 42);
    check_fn("test_div", 21);
    check_fn("test_mod", 3);
    check_fn("test_prec", 14);
    check_fn("test_shl", 16);
    check_fn("test_band", 15);
    check_fn("test_bor", 17);
    check_fn("test_bxor", 16);
    check_fn("test_neg", -42);
    check_fn("test_bnot", -1);
}

// ════════════════════════════════════════════════════════════════════════════
//  4. Binary comparisons and spaceship
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Constant expression — comparisons", "[model][const_expr][comparison]") {
    auto comp = compile_model(R"SRC(
        module __test_const_cmp__;
        test_eq() : bool { return 10 == 10; }
        test_ne() : bool { return 10 != 20; }
        test_lt() : bool { return 5 < 10; }
        test_le() : bool { return 10 <= 10; }
        test_gt() : bool { return 20 > 10; }
        test_ge() : bool { return 10 >= 15; }
        test_space_lt() : int { return 5 <=> 10; }
        test_space_eq() : int { return 10 <=> 10; }
        test_space_gt() : int { return 20 <=> 10; }
    )SRC");
    REQUIRE(comp != nullptr);

    auto check_bool = [&](const std::string& name, bool expected) {
        auto expr = get_return_expr(comp, name);
        REQUIRE(expr != nullptr);
        CHECK(expr->is_constant());
        CHECK(expr->get_constant_value().get_bool() == expected);
    };

    auto check_int = [&](const std::string& name, int64_t expected) {
        auto expr = get_return_expr(comp, name);
        REQUIRE(expr != nullptr);
        CHECK(expr->is_constant());
        CHECK(expr->get_constant_value().get_int64() == expected);
    };

    check_bool("test_eq", true);
    check_bool("test_ne", true);
    check_bool("test_lt", true);
    check_bool("test_le", true);
    check_bool("test_gt", true);
    check_bool("test_ge", false);

    check_int("test_space_lt", -1);
    check_int("test_space_eq", 0);
    check_int("test_space_gt", 1);
}

// ════════════════════════════════════════════════════════════════════════════
//  5. Logical expressions and ternary conditionals
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Constant expression — logical and ternary", "[model][const_expr][logical]") {
    auto comp = compile_model(R"SRC(
        module __test_const_log__;
        test_and_t() : bool { return true && true; }
        test_and_f() : bool { return true && false; }
        test_or_t() : bool { return false || true; }
        test_or_f() : bool { return false || false; }
        test_not() : bool { return !true; }
        test_ternary_t() : int { return true ? 42 : 100; }
        test_ternary_f() : int { return false ? 42 : 100; }
    )SRC");
    REQUIRE(comp != nullptr);

    auto check_bool = [&](const std::string& name, bool expected) {
        auto expr = get_return_expr(comp, name);
        REQUIRE(expr != nullptr);
        CHECK(expr->is_constant());
        CHECK(expr->get_constant_value().get_bool() == expected);
    };

    auto check_int = [&](const std::string& name, int64_t expected) {
        auto expr = get_return_expr(comp, name);
        REQUIRE(expr != nullptr);
        CHECK(expr->is_constant());
        CHECK(expr->get_constant_value().get_int64() == expected);
    };

    check_bool("test_and_t", true);
    check_bool("test_and_f", false);
    check_bool("test_or_t", true);
    check_bool("test_or_f", false);
    check_bool("test_not", false);

    check_int("test_ternary_t", 42);
    check_int("test_ternary_f", 100);
}

// ════════════════════════════════════════════════════════════════════════════
//  6. Primitive casts
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Constant expression — primitive casts", "[model][const_expr][cast]") {
    auto comp = compile_model(R"SRC(
        module __test_const_cast__;
        test_cast_int() : int { return (int)3.14; }
        test_cast_byte() : unsigned byte { return (unsigned byte)257; }
    )SRC");
    REQUIRE(comp != nullptr);

    auto e1 = get_return_expr(comp, "test_cast_int");
    REQUIRE(e1 != nullptr);
    CHECK(e1->is_constant());
    CHECK(e1->get_constant_value().get_int64() == 3);

    auto e2 = get_return_expr(comp, "test_cast_byte");
    REQUIRE(e2 != nullptr);
    CHECK(e2->is_constant());
    CHECK(e2->get_constant_value().get_uint64() == 1);
}

// ════════════════════════════════════════════════════════════════════════════
//  7. Struct designated initializers and member access
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Constant expression — struct construction and field access", "[model][const_expr][struct]") {
    auto comp = compile_model(R"SRC(
        module __test_const_struct__;
        struct Point {
            public x : int;
            public y : int;
        }

        struct Rect {
            public origin : Point;
            public width : int;
            public height : int;
        }

        test_field_x() : int {
            return Point{ .x = 10, .y = 20 }.x;
        }

        test_field_y() : int {
            return Point{ .x = 10, .y = 20 }.y;
        }

        test_field_arith() : int {
            return Point{ .x = 10, .y = 20 }.x + Point{ .x = 3, .y = 7 }.y;
        }

        test_partial_init() : int {
            return Point{ .x = 15 }.y;
        }

        test_nested_struct() : int {
            return Rect{ .origin = Point{ .x = 5, .y = 12 }, .width = 100, .height = 50 }.origin.y;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto check_fn = [&](const std::string& name, int64_t expected) {
        auto expr = get_return_expr(comp, name);
        REQUIRE(expr != nullptr);
        CHECK(expr->is_constant());
        CHECK(expr->get_constant_value().get_int64() == expected);
    };

    check_fn("test_field_x", 10);
    check_fn("test_field_y", 20);
    check_fn("test_field_arith", 17);
    check_fn("test_partial_init", 0);
    check_fn("test_nested_struct", 12);
}

// ════════════════════════════════════════════════════════════════════════════
//  8. Union construction and alternative access
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Constant expression — union construction and alternative access", "[model][const_expr][union]") {
    auto comp = compile_model(R"SRC(
        module __test_const_union__;
        union Value {
            i : int;
            d : double;
        }

        test_union_direct() : int {
            return Value(42).i;
        }

        test_union_arith() : int {
            return Value(21).i * 2;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto check_fn = [&](const std::string& name, int64_t expected) {
        auto expr = get_return_expr(comp, name);
        REQUIRE(expr != nullptr);
        CHECK(expr->is_constant());
        CHECK(expr->get_constant_value().get_int64() == expected);
    };

    check_fn("test_union_direct", 42);
    check_fn("test_union_arith", 42);
}

// ════════════════════════════════════════════════════════════════════════════
//  9. Complex combined expressions (struct + enum + union + arithmetic)
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Constant expression — combined operations", "[model][const_expr][combined]") {
    auto comp = compile_model(R"SRC(
        module __test_const_combined__;
        enum Flags {
            A = 1;
            B = 2;
            C = 4;
        }

        struct Point {
            public x : int;
            public y : int;
        }

        struct Rect {
            public origin : Point;
            public width : int;
            public height : int;
        }

        union Val {
            i : int;
            d : double;
        }

        test_enum_arith() : int {
            return Flags::A + Flags::B * 3;
        }

        test_enum_cmp() : bool {
            return (Flags::A < Flags::B) && (Flags::B == Flags::B);
        }

        test_complex_struct_union() : int {
            return Point{ .x = 10, .y = 20 }.x * 2 +
                   Rect{ .origin = Point{ .x = 5, .y = 15 }, .width = 100, .height = 50 }.origin.y +
                   Val(30).i;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto e1 = get_return_expr(comp, "test_enum_arith");
    REQUIRE(e1 != nullptr);
    CHECK(e1->is_constant());
    CHECK(e1->get_constant_value().get_int64() == 7);

    auto e2 = get_return_expr(comp, "test_enum_cmp");
    REQUIRE(e2 != nullptr);
    CHECK(e2->is_constant());
    CHECK(e2->get_constant_value().get_bool() == true);

    auto e3 = get_return_expr(comp, "test_complex_struct_union");
    REQUIRE(e3 != nullptr);
    CHECK(e3->is_constant());
    CHECK(e3->get_constant_value().get_int64() == (10 * 2 + 15 + 30));
}

// ════════════════════════════════════════════════════════════════════════════
//  10. Propagation of const-declared variables (local and global)
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Constant expression — const variable propagation", "[model][const_expr][const_var]") {
    auto comp = compile_model(R"SRC(
        module __test_const_var_prop__;

        const GLOBAL_CONST : int = 100;

        struct Point {
            public x : int;
            public y : int;
        }

        const GLOBAL_POINT : Point = Point{ .x = 5, .y = 10 };

        union Val {
            i : int;
            d : double;
        }

        const GLOBAL_UNION : Val = Val(42);

        test_local_const() : int {
            const a : int = 10;
            return a + 5;
        }

        test_chained_const() : int {
            const x : int = 2;
            const y : int = x * 10;
            return y + 3;
        }

        test_global_const() : int {
            return GLOBAL_CONST * 2;
        }

        test_local_struct_const() : int {
            const pt : Point = Point{ .x = 10, .y = 20 };
            return pt.x + pt.y;
        }

        test_local_struct_brace_const() : int {
            const pt : Point { .x = 15, .y = 25 };
            return pt.x * 2 + pt.y;
        }

        test_global_struct_const() : int {
            return GLOBAL_POINT.x + GLOBAL_POINT.y;
        }

        test_global_union_const() : int {
            return GLOBAL_UNION.i + 8;
        }

        test_mutable_not_propagated() : int {
            x : int = 10;
            return x + 5;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto check_const = [&](const std::string& name, int64_t expected) {
        auto expr = get_return_expr(comp, name);
        REQUIRE(expr != nullptr);
        CHECK(expr->is_constant());
        CHECK(expr->get_constant_value().get_int64() == expected);
    };

    check_const("test_local_const", 15);
    check_const("test_chained_const", 23);
    check_const("test_global_const", 200);
    check_const("test_local_struct_const", 30);
    check_const("test_local_struct_brace_const", 55);
    check_const("test_global_struct_const", 15);
    check_const("test_global_union_const", 50);

    auto e_mut = get_return_expr(comp, "test_mutable_not_propagated");
    REQUIRE(e_mut != nullptr);
    CHECK_FALSE(e_mut->is_constant());
}

// ════════════════════════════════════════════════════════════════════════════
//  11. Non-constant expressions boundary checks
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Constant expression — non-constant expressions remain non-const", "[model][const_expr][boundary]") {
    auto comp = compile_model(R"SRC(
        module __test_non_const__;
        test_param(x : int) : int {
            return x + 10;
        }

        class MyClass {
            public v : int;
        }

        test_class() : int {
            c : MyClass;
            return c.v;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto e_param = get_return_expr(comp, "test_param");
    REQUIRE(e_param != nullptr);
    CHECK_FALSE(e_param->is_constant());

    auto e_class = get_return_expr(comp, "test_class");
    REQUIRE(e_class != nullptr);
    CHECK_FALSE(e_class->is_constant());
}










