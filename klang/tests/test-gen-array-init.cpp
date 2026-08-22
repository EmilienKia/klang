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

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

using namespace k::parse;
using namespace k::parse::ast;

//
// Phase 1: Parser tests for brace initializer lists
//

TEST_CASE("Parse brace init — primitive array with literal expressions", "[parser][brace-init]") {
    test_logger log;
    k::source src{"arr : int[3] {1, 2, 3};"};
    k::parse::parser parser(log, src);
    auto var = parser.parse_variable_decl();
    REQUIRE(var);
    REQUIRE(var->name.content == "arr");
    REQUIRE(var->is_brace_init == true);
    REQUIRE(var->is_constructor == false);
    REQUIRE(var->init != nullptr);

    auto init = std::dynamic_pointer_cast<brace_init_list>(var->init);
    REQUIRE(init);
    REQUIRE(init->elements.size() == 3);
    // All elements should be non-null expressions
    REQUIRE(init->elements[0] != nullptr);
    REQUIRE(init->elements[1] != nullptr);
    REQUIRE(init->elements[2] != nullptr);
}

TEST_CASE("Parse brace init — empty brace list", "[parser][brace-init]") {
    test_logger log;
    k::source src{"arr : int[0] {};"};
    k::parse::parser parser(log, src);
    auto var = parser.parse_variable_decl();
    REQUIRE(var);
    REQUIRE(var->is_brace_init == true);
    auto init = std::dynamic_pointer_cast<brace_init_list>(var->init);
    REQUIRE(init);
    REQUIRE(init->elements.size() == 0);
}

TEST_CASE("Parse brace init — expressions with arithmetic", "[parser][brace-init]") {
    test_logger log;
    k::source src{"arr : int[5] {1, 1+1, 2+1, 2*2, 10/2};"};
    k::parse::parser parser(log, src);
    auto var = parser.parse_variable_decl();
    REQUIRE(var);
    REQUIRE(var->is_brace_init == true);
    auto init = std::dynamic_pointer_cast<brace_init_list>(var->init);
    REQUIRE(init);
    REQUIRE(init->elements.size() == 5);
    for (size_t i = 0; i < 5; ++i) {
        REQUIRE(init->elements[i] != nullptr);
    }
}

TEST_CASE("Parse brace init — empty slots for default construction", "[parser][brace-init]") {
    test_logger log;
    k::source src{"arr : int[3] {1, , 3};"};
    k::parse::parser parser(log, src);
    auto var = parser.parse_variable_decl();
    REQUIRE(var);
    REQUIRE(var->is_brace_init == true);
    auto init = std::dynamic_pointer_cast<brace_init_list>(var->init);
    REQUIRE(init);
    REQUIRE(init->elements.size() == 3);
    REQUIRE(init->elements[0] != nullptr);
    REQUIRE(init->elements[1] == nullptr); // empty slot
    REQUIRE(init->elements[2] != nullptr);
}

TEST_CASE("Parse brace init — unsized array", "[parser][brace-init]") {
    test_logger log;
    k::source src{"arr : int[] {10, 20, 30, 40};"};
    k::parse::parser parser(log, src);
    auto var = parser.parse_variable_decl();
    REQUIRE(var);
    REQUIRE(var->is_brace_init == true);
    auto init = std::dynamic_pointer_cast<brace_init_list>(var->init);
    REQUIRE(init);
    REQUIRE(init->elements.size() == 4);
}

TEST_CASE("Parse brace init — single element", "[parser][brace-init]") {
    test_logger log;
    k::source src{"arr : int[1] {42};"};
    k::parse::parser parser(log, src);
    auto var = parser.parse_variable_decl();
    REQUIRE(var);
    REQUIRE(var->is_brace_init == true);
    auto init = std::dynamic_pointer_cast<brace_init_list>(var->init);
    REQUIRE(init);
    REQUIRE(init->elements.size() == 1);
    REQUIRE(init->elements[0] != nullptr);
}

TEST_CASE("Parse brace init — all empty slots", "[parser][brace-init]") {
    test_logger log;
    k::source src{"arr : int[3] {,,};"};
    k::parse::parser parser(log, src);
    auto var = parser.parse_variable_decl();
    REQUIRE(var);
    REQUIRE(var->is_brace_init == true);
    auto init = std::dynamic_pointer_cast<brace_init_list>(var->init);
    REQUIRE(init);
    REQUIRE(init->elements.size() == 2);
    REQUIRE(init->elements[0] == nullptr);
    REQUIRE(init->elements[1] == nullptr);
}

//
// Phase 2+: Code generation tests for array brace initialization
//

TEST_CASE("Array brace init — local int array with literal values", "[gen][brace-init]") {
    auto jit = gen_jit(R"SRC(
    module gen_array_init_01;
    get_elem(idx : int) : int {
        arr : int[3] {10, 20, 30};
        return arr[idx];
    }
    )SRC");
    REQUIRE(jit);

    auto get_elem = jit->lookup_symbol<int(*)(int)>("get_elem");
    REQUIRE(get_elem != nullptr);
    REQUIRE(get_elem(0) == 10);
    REQUIRE(get_elem(1) == 20);
    REQUIRE(get_elem(2) == 30);
}

TEST_CASE("Array brace init — expressions as initializers", "[gen][brace-init]") {
    auto jit = gen_jit(R"SRC(
    module gen_array_init_02;
    get_elem(idx : int) : int {
        arr : int[5] {1, 1+1, 2+1, 2*2, 10/2};
        return arr[idx];
    }
    )SRC");
    REQUIRE(jit);

    auto get_elem = jit->lookup_symbol<int(*)(int)>("get_elem");
    REQUIRE(get_elem != nullptr);
    REQUIRE(get_elem(0) == 1);
    REQUIRE(get_elem(1) == 2);
    REQUIRE(get_elem(2) == 3);
    REQUIRE(get_elem(3) == 4);
    REQUIRE(get_elem(4) == 5);
}

TEST_CASE("Array brace init — empty slots default to zero", "[gen][brace-init]") {
    auto jit = gen_jit(R"SRC(
    module gen_array_init_03;
    get_elem(idx : int) : int {
        arr : int[3] {42, , 99};
        return arr[idx];
    }
    )SRC");
    REQUIRE(jit);

    auto get_elem = jit->lookup_symbol<int(*)(int)>("get_elem");
    REQUIRE(get_elem != nullptr);
    REQUIRE(get_elem(0) == 42);
    REQUIRE(get_elem(1) == 0);  // default-init = 0
    REQUIRE(get_elem(2) == 99);
}

TEST_CASE("Array brace init — fewer elements than size (padding)", "[gen][brace-init]") {
    auto jit = gen_jit(R"SRC(
    module gen_array_init_04;
    get_elem(idx : int) : int {
        arr : int[5] {10, 20};
        return arr[idx];
    }
    )SRC");
    REQUIRE(jit);

    auto get_elem = jit->lookup_symbol<int(*)(int)>("get_elem");
    REQUIRE(get_elem != nullptr);
    REQUIRE(get_elem(0) == 10);
    REQUIRE(get_elem(1) == 20);
    REQUIRE(get_elem(2) == 0);  // default padded
    REQUIRE(get_elem(3) == 0);
    REQUIRE(get_elem(4) == 0);
}

TEST_CASE("Array brace init — too many elements errors", "[gen][brace-init]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
    module gen_array_init_05;
    gen_array_init_05() : int {
        arr : int[2] {1, 2, 3};
        return arr[0];
    }
    )SRC"));
}

TEST_CASE("Array brace init — empty brace list for zero-size array", "[gen][brace-init]") {
    auto jit = gen_jit(R"SRC(
    module gen_array_init_06;
    gen_array_init_06() : int {
        arr : int[0] {};
        return 42;
    }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("gen_array_init_06");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

TEST_CASE("Array brace init — size inferred from init list", "[gen][brace-init]") {
    auto jit = gen_jit(R"SRC(
    module gen_array_init_07;
    get_elem(idx : int) : int {
        arr : int[] {100, 200, 300, 400};
        return arr[idx];
    }
    )SRC");
    REQUIRE(jit);

    auto get_elem = jit->lookup_symbol<int(*)(int)>("get_elem");
    REQUIRE(get_elem != nullptr);
    REQUIRE(get_elem(0) == 100);
    REQUIRE(get_elem(1) == 200);
    REQUIRE(get_elem(2) == 300);
    REQUIRE(get_elem(3) == 400);
}

TEST_CASE("Array brace init — single element", "[gen][brace-init]") {
    auto jit = gen_jit(R"SRC(
    module gen_array_init_08;
    gen_array_init_08() : int {
        arr : int[1] {42};
        return arr[0];
    }
    )SRC");
    REQUIRE(jit);

    auto test = jit->lookup_symbol<int(*)()>("gen_array_init_08");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

TEST_CASE("Array brace init — double type", "[gen][brace-init]") {
    auto jit = gen_jit(R"SRC(
    module gen_array_init_09;
    get_elem(idx : int) : double {
        arr : double[3] {1.5d, 2.5d, 3.5d};
        return arr[idx];
    }
    )SRC");
    REQUIRE(jit);

    auto get_elem = jit->lookup_symbol<double(*)(int)>("get_elem");
    REQUIRE(get_elem != nullptr);
    REQUIRE(get_elem(0) == Catch::Approx(1.5));
    REQUIRE(get_elem(1) == Catch::Approx(2.5));
    REQUIRE(get_elem(2) == Catch::Approx(3.5));
}

//
// Phase 5: Global array variables with brace initialization
//

TEST_CASE("Array brace init — global array with constant values (static init)", "[gen][brace-init][global]") {
    auto jit = gen_jit(R"SRC(
    module gen_array_init_10;
    garr : int[3] {10, 20, 30};
    get_elem(idx : int) : int {
        return garr[idx];
    }
    )SRC");
    REQUIRE(jit);

    auto get_elem = jit->lookup_symbol<int(*)(int)>("get_elem");
    REQUIRE(get_elem != nullptr);
    REQUIRE(get_elem(0) == 10);
    REQUIRE(get_elem(1) == 20);
    REQUIRE(get_elem(2) == 30);
}

TEST_CASE("Array brace init — global array with expression values (dynamic init)", "[gen][brace-init][global]") {
    auto jit = gen_jit(R"SRC(
    module gen_array_init_11;
    base : int = 100;
    garr : int[3] {base, base+1, base+2};
    get_elem(idx : int) : int {
        return garr[idx];
    }
    )SRC");
    REQUIRE(jit);

    auto get_elem = jit->lookup_symbol<int(*)(int)>("get_elem");
    REQUIRE(get_elem != nullptr);
    REQUIRE(get_elem(0) == 100);
    REQUIRE(get_elem(1) == 101);
    REQUIRE(get_elem(2) == 102);
}

TEST_CASE("Array brace init — global array inferred size", "[gen][brace-init][global]") {
    auto jit = gen_jit(R"SRC(
    module gen_array_init_12;
    garr : int[] {5, 10, 15, 20};
    get_elem(idx : int) : int {
        return garr[idx];
    }
    )SRC");
    REQUIRE(jit);

    auto get_elem = jit->lookup_symbol<int(*)(int)>("get_elem");
    REQUIRE(get_elem != nullptr);
    REQUIRE(get_elem(0) == 5);
    REQUIRE(get_elem(1) == 10);
    REQUIRE(get_elem(2) == 15);
    REQUIRE(get_elem(3) == 20);
}

