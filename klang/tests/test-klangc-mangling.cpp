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

#ifndef KLANGC_PATH
#error "KLANGC_PATH must be defined -- check CMakeLists.txt"
#endif

TEST_CASE("klangc: mangle-symbol command-line option", "[klangc][mangling]") {
    auto res = k::tools::run_process(KLANGC_PATH, {"--mangle-symbol", "function math::add(int, int)"});
    REQUIRE(res.exit_code == 0);
    REQUIRE(res.out == "_KFN4math3addEii\n");
}

TEST_CASE("klangc: demangle-symbol command-line option", "[klangc][mangling]") {
    auto res = k::tools::run_process(KLANGC_PATH, {"--demangle-symbol", "_KFMKN4demo5Point3lenEv"});
    REQUIRE(res.exit_code == 0);
    REQUIRE(res.out == "const member function demo::Point::len()\n");
}
