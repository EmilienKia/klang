/*
 * K Language compiler — libkdi tests
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

#include "kdi.hpp"

#include <cstdlib>
#include <string>

using namespace kdi;

TEST_CASE("mangling: qualified symbols round-trip", "[mangling]") {
    REQUIRE(kdi_mangle_symbol("math::Point") == "_KN4math5PointE");
    REQUIRE(kdi_demangle_symbol("_KN4math5PointE") == "symbol math::Point");
}

TEST_CASE("mangling: function symbol with primitive parameters", "[mangling]") {
    REQUIRE(kdi_mangle_symbol("function math::add(int, int)") == "_KFN4math3addEii");
    REQUIRE(kdi_demangle_symbol("_KFN4math3addEii") == "function math::add(int, int)");
}

TEST_CASE("mangling: member and runtime descriptor symbols", "[mangling]") {
    REQUIRE(kdi_mangle_symbol("const member function demo::Point::len()") == "_KFMKN4demo5Point3lenEv");
    REQUIRE(kdi_demangle_symbol("_KFMKN4demo5Point3lenEv") == "const member function demo::Point::len()");
    REQUIRE(kdi_mangle_symbol("vtable demo::Point") == "_KTVN4demo5PointE");
    REQUIRE(kdi_demangle_symbol("_KTRIN4demo5PointE") == "rtti demo::Point");
}

TEST_CASE("mangling: C API returns allocated strings", "[mangling][c-api]") {
    char* mangled = kdi_symbol_mangle("function math::add(int, int)");
    REQUIRE(mangled != nullptr);
    REQUIRE(std::string(mangled) == "_KFN4math3addEii");

    char* readable = kdi_symbol_demangle(mangled);
    REQUIRE(readable != nullptr);
    REQUIRE(std::string(readable) == "function math::add(int, int)");

    kdi_symbol_string_free(readable);
    kdi_symbol_string_free(mangled);
}

TEST_CASE("mangling: invalid input is reported", "[mangling]") {
    REQUIRE_THROWS_AS(kdi_demangle_symbol("_Z3foov"), kdi_mangling_error);
    REQUIRE(kdi_symbol_demangle("_Z3foov") == nullptr);
}
