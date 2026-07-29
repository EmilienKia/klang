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

/** Tests for template value parameters with aggregate types. */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"
#include "../src/model/aggregate_value.hpp"
#include "../src/model/template_instantiator.hpp"
TEST_CASE("[A12] M12: designated aggregate value template argument compiles",
          "[milestone12][template][value-param][aggregate][jit]") {
    auto jit = gen_jit(R"SRC(
        module __m12_a__;

        struct Point {
            x : int;
            y : int;
        }

        template<Point P>
        one() : int { return 1; }

        test() : int {
            return one<{ .x = 10, .y = 20 }>();
        }
    )SRC");

    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN9__m12_a__4testEv");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 1);
}

TEST_CASE("[B12] M12: nested designated aggregate value template argument compiles",
          "[milestone12][template][value-param][aggregate][nested][jit]") {
    auto jit = gen_jit(R"SRC(
        module __m12_b__;

        struct Point {
            x : int;
            y : int;
        }

        struct Box {
            origin : Point;
            scale  : int;
        }

        template<Box B>
        one() : int { return 1; }

        test() : int {
            return one<{ .origin = { .x = 3, .y = 4 }, .scale = 2 }>();
        }
    )SRC");

    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN9__m12_b__4testEv");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 1);
}

TEST_CASE("[C12] M12: positional aggregate value template argument is rejected",
          "[milestone12][template][value-param][aggregate][error]") {
    REQUIRE(compile_should_fail(R"SRC(
        module __m12_c__;

        struct Point {
            x : int;
            y : int;
        }

        template<Point P>
        one() : int { return 1; }

        test() : int {
            return one<{ 10, 20 }>();
        }
    )SRC", nullptr));
}

TEST_CASE("[D12] M12: unknown designated member is rejected",
          "[milestone12][template][value-param][aggregate][error]") {
    REQUIRE(compile_should_fail(R"SRC(
        module __m12_d__;

        struct Point {
            x : int;
            y : int;
        }

        template<Point P>
        one() : int { return 1; }

        test() : int {
            return one<{ .x = 10, .z = 20 }>();
        }
    )SRC", nullptr));
}

TEST_CASE("[E12] M12: missing designated member is rejected",
          "[milestone12][template][value-param][aggregate][error]") {
    REQUIRE(compile_should_fail(R"SRC(
        module __m12_e__;

        struct Point {
            x : int;
            y : int;
        }

        template<Point P>
        one() : int { return 1; }

        test() : int {
            return one<{ .x = 10 }>();
        }
    )SRC", nullptr));
}

TEST_CASE("[F12] M12: aggregate values are encoded distinctly in instantiation key/name",
          "[milestone12][template][value-param][aggregate][distinct]") {
    auto v1 = std::make_shared<k::model::aggregate_value>(
        nullptr, std::map<std::string, k::value_type>{{"x", 1}, {"y", 2}});
    auto v2 = std::make_shared<k::model::aggregate_value>(
        nullptr, std::map<std::string, k::value_type>{{"x", 3}, {"y", 4}});
    auto v3 = std::make_shared<k::model::aggregate_value>(
        nullptr, std::map<std::string, k::value_type>{{"x", 1}, {"y", 2}});

    std::vector<k::model::template_argument> args1{
        k::model::template_argument::make_value(k::value_type{v1})};
    std::vector<k::model::template_argument> args2{
        k::model::template_argument::make_value(k::value_type{v2})};
    std::vector<k::model::template_argument> args3{
        k::model::template_argument::make_value(k::value_type{v3})};

    auto k1 = k::model::build_instantiation_key(args1);
    auto k2 = k::model::build_instantiation_key(args2);
    auto k3 = k::model::build_instantiation_key(args3);
    CHECK(k1 != k2);
    CHECK(k1 == k3);

    auto n1 = k::model::build_instantiated_name("one", args1);
    auto n2 = k::model::build_instantiated_name("one", args2);
    auto n3 = k::model::build_instantiated_name("one", args3);
    CHECK(n1 != n2);
    CHECK(n1 == n3);
    CHECK(n1.find("_A") != std::string::npos);
}

TEST_CASE("[G12] M12: aggregate value parameter defaults",
          "[milestone12][template][value-param][aggregate][default][jit]") {
    auto jit = gen_jit(R"SRC(
        module __m12_g__;

        struct Point {
            x : int;
            y : int;
        }

        template<Point P = { .x = 0, .y = 0 }>
        one() : int { return 1; }

        test() : int {
            return one<>() + one();
        }
    )SRC");

    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN9__m12_g__4testEv");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 2);
}

TEST_CASE("[H12] M12: aggregate value parameter member access",
          "[milestone12][template][value-param][aggregate][member][jit]") {
    auto jit = gen_jit(R"SRC(
        module __m12_h__;

        struct Point {
            x : int;
            y : int;
        }

        struct Box {
            origin : Point;
            scale  : int;
        }

        template<Box B>
        sum() : int {
            return B.origin.x + B.origin.y + B.scale;
        }

        test() : int {
            return sum<{ .origin = { .x = 3, .y = 4 }, .scale = 5 }>();
        }
    )SRC");

    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN9__m12_h__4testEv");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 12);
}

TEST_CASE("[I12] M12: aggregate value parameter direct usage",
          "[milestone12][template][value-param][aggregate][direct][jit]") {
    auto jit = gen_jit(R"SRC(
        module __m12_i__;

        struct Point {
            x : int;
            y : int;
        }

        sumPoint(p : Point) : int {
            return p.x + p.y;
        }

        template<Point P>
        asPoint() : Point {
            return P;
        }

        template<Point P>
        localScore() : int {
            local : Point = P;
            return local.x + local.y;
        }

        template<Point P>
        score() : int {
            return sumPoint(P) + sumPoint(asPoint<P>()) + localScore<P>();
        }

        test() : int {
            return score<{ .x = 4, .y = 6 }>();
        }
    )SRC");

    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN9__m12_i__4testEv");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 30);
}

TEST_CASE("[J12] M12: aggregate member value used as full aggregate",
          "[milestone12][template][value-param][aggregate][member][direct][jit]") {
    auto jit = gen_jit(R"SRC(
        module __m12_j__;

        struct Point {
            x : int;
            y : int;
        }

        struct Box {
            origin : Point;
            scale  : int;
        }

        template<Box B>
        pointScore() : int {
            local : Point = B.origin;
            return local.x + local.y;
        }

        test() : int {
            return pointScore<{ .origin = { .x = 7, .y = 8 }, .scale = 1 }>();
        }
    )SRC");

    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN9__m12_j__4testEv");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 15);
}

TEST_CASE("[K12] M12: aggregate default value direct local usage",
          "[milestone12][template][value-param][aggregate][default][direct][jit]") {
    auto jit = gen_jit(R"SRC(
        module __m12_k__;

        struct Point {
            x : int;
            y : int;
        }

        template<Point P = { .x = 1, .y = 2 }>
        score() : int {
            local : Point = P;
            return local.x + local.y;
        }

        test() : int {
            return score<>() + score();
        }
    )SRC");

    REQUIRE(jit != nullptr);
    auto fn = jit->lookup_symbol<int(*)()>("_KFN9__m12_k__4testEv");
    REQUIRE(fn != nullptr);
    CHECK(fn() == 6);
}
