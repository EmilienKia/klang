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
 * Tests for constructor model elements in the context of RTTI synthesis.
 *
 * These tests verify:
 *  - A class with an explicit public constructor exposes it in constructors().
 *  - A class with a default constructor has a compiler-generated ctor.
 *  - A class with a deleted constructor has DELETE aliasing.
 *  - A class with multiple constructors reports the correct count.
 *  - Constructor parameter count is correctly reflected.
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"


// ════════════════════════════════════════════════════════════════════════════
//  Constructor model visibility
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Model: class with explicit public constructor", "[model][constructor]") {
    auto comp = compile_model(R"SRC(
        module __test_ctor_1__;
        class Foo {
            public Foo() {}
            public dummy() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto klass = find_klass(comp, "Foo");
    REQUIRE(klass != nullptr);
    CHECK_FALSE(klass->constructors().empty());
    CHECK(klass->constructors().size() == 1);
}

TEST_CASE("Model: class with default constructor is compiler-generated", "[model][constructor]") {
    auto comp = compile_model(R"SRC(
        module __test_ctor_2__;
        class Bar {
            public Bar() -> default;
            public dummy() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto klass = find_klass(comp, "Bar");
    REQUIRE(klass != nullptr);
    REQUIRE(!klass->constructors().empty());

    auto& ctor = klass->constructors().front();
    CHECK(ctor->get_aliasing() == k::model::function::function_aliasing::DEFAULT);
}

TEST_CASE("Model: class with deleted constructor", "[model][constructor]") {
    auto comp = compile_model(R"SRC(
        module __test_ctor_3__;
        class Baz {
            public Baz() -> delete;
            public dummy() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto klass = find_klass(comp, "Baz");
    REQUIRE(klass != nullptr);
    REQUIRE(!klass->constructors().empty());

    auto& ctor = klass->constructors().front();
    CHECK(ctor->get_aliasing() == k::model::function::function_aliasing::DELETE);
}

TEST_CASE("Model: class with multiple constructors", "[model][constructor]") {
    auto comp = compile_model(R"SRC(
        module __test_ctor_4__;
        class Multi {
            public Multi() {}
            public Multi(x : int) {}
            public Multi(x : int, y : int) {}
            public dummy() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto klass = find_klass(comp, "Multi");
    REQUIRE(klass != nullptr);
    CHECK(klass->constructors().size() == 3);
}

TEST_CASE("Model: constructor parameter count", "[model][constructor]") {
    auto comp = compile_model(R"SRC(
        module __test_ctor_5__;
        class WithParams {
            public WithParams(a : int, b : int, c : int) {}
            public dummy() : int { return 0; }
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto klass = find_klass(comp, "WithParams");
    REQUIRE(klass != nullptr);
    REQUIRE(!klass->constructors().empty());

    auto& ctor = klass->constructors().front();
    // get_parameter_size() counts user-facing parameters (excluding 'this')
    CHECK(ctor->get_parameter_size() == 3);
}

