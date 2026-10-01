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
 * Tests for the uncaught-exception guard of the generated C entry point when the
 * program runs in the JIT: the host process must survive (no abort()) and get the
 * outcome through jit::run_main().
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

TEST_CASE("JIT main: uncaught exception does not kill the host", "[gen][exceptions][main-uncaught]") {
    auto jit = gen_jit(R"(
        module gen_main_uncaught_01;
        main() : int {
            throw FatalError(3);
            return 0;
        }
    )");
    REQUIRE(jit != nullptr);

    auto run = jit->run_main();
    REQUIRE(run.has_value());
    CHECK(run->uncaught_exception);
    CHECK(run->status == k::model::gen::k_main_uncaught_status);
}

TEST_CASE("JIT main: frames are unwound before the host regains control", "[gen][exceptions][main-uncaught]") {
    auto jit = gen_jit(R"(
        module gen_main_uncaught_02;

        cleanup_count : int = 0;

        struct Guard {
            ~Guard() { cleanup_count = cleanup_count + 1; }
        }

        deep(n: int) : int {
            g : Guard;
            if (n == 0) {
                throw FatalError(n);
            }
            return deep(n - 1);
        }

        main() : int {
            return deep(3);
        }
    )");
    REQUIRE(jit != nullptr);

    auto run = jit->run_main();
    REQUIRE(run.has_value());
    CHECK(run->uncaught_exception);

    auto cleanup_count = jit->lookup_symbol<int*>("cleanup_count");
    REQUIRE(cleanup_count != nullptr);
    CHECK(*cleanup_count == 4);
}

TEST_CASE("JIT main: void main with args and uncaught exception", "[gen][exceptions][main-uncaught]") {
    auto jit = gen_jit(R"(
        module gen_main_uncaught_03;
        main(args : const String[]) {
            throw FatalError(1);
        }
    )");
    REQUIRE(jit != nullptr);

    auto run = jit->run_main();
    REQUIRE(run.has_value());
    CHECK(run->uncaught_exception);
}

TEST_CASE("JIT main: caught exception is not reported as uncaught", "[gen][exceptions][main-uncaught]") {
    auto jit = gen_jit(R"(
        module gen_main_uncaught_04;
        main() : int {
            try {
                throw FatalError(3);
            } catch (e: FatalError&) {
                return 7;
            }
            return 0;
        }
    )");
    REQUIRE(jit != nullptr);

    auto run = jit->run_main();
    REQUIRE(run.has_value());
    CHECK_FALSE(run->uncaught_exception);
    CHECK(run->status == 7);
}

TEST_CASE("JIT main: normal exit code is preserved", "[gen][exceptions][main-uncaught]") {
    auto jit = gen_jit(R"(
        module gen_main_uncaught_05;
        main() : int {
            return 42;
        }
    )");
    REQUIRE(jit != nullptr);

    auto run = jit->run_main();
    REQUIRE(run.has_value());
    CHECK_FALSE(run->uncaught_exception);
    CHECK(run->status == 42);
}

TEST_CASE("JIT main: the entry point can be run again after an uncaught exception", "[gen][exceptions][main-uncaught]") {
    auto jit = gen_jit(R"(
        module gen_main_uncaught_06;

        calls : int = 0;

        main() : int {
            calls = calls + 1;
            if (calls == 1) {
                throw FatalError(1);
            }
            return calls;
        }
    )");
    REQUIRE(jit != nullptr);

    auto first = jit->run_main();
    REQUIRE(first.has_value());
    CHECK(first->uncaught_exception);

    auto second = jit->run_main();
    REQUIRE(second.has_value());
    CHECK_FALSE(second->uncaught_exception);
    CHECK(second->status == 2);
}

TEST_CASE("JIT main: run_main reports a missing entry point", "[gen][exceptions][main-uncaught]") {
    auto jit = gen_jit(R"(
        module gen_main_uncaught_07;
        helper() : int {
            return 1;
        }
    )");
    REQUIRE(jit != nullptr);

    CHECK_FALSE(jit->run_main().has_value());
}
