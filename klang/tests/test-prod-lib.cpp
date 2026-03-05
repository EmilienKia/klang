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

#include "../src/parse/parser.hpp"
#include "../src/common/process.hpp"
#include "../src/compiler.hpp"

#include "helpers.hpp"


// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

/** True if the nm output for the file contains a defined symbol whose name
 *  includes the given substring. Works for both .so (--dynamic) and .a. */
static bool has_defined_symbol_containing(const std::string& file, const std::string& substr) {
    // For .so: use --dynamic so we inspect the export table.
    // For .a:  nm lists all symbols without --dynamic; the flag is silently
    //          ignored on archives by most nm implementations, so one command
    //          covers both cases.
    auto res = k::tools::lookup_run_process(
        "nm", {"--defined-only", file});
    if (res.exit_code != 0) return false;
    return res.out.find(substr) != std::string::npos;
}


// ---------------------------------------------------------------------------
// unit_name_to_lib_base — pure utility
// ---------------------------------------------------------------------------

TEST_CASE("unit_name_to_lib_base: simple name is unchanged", "[prod-lib][unit-name]") {
    REQUIRE(k::compiler::unit_name_to_lib_base("mylib")        == "mylib");
}

TEST_CASE("unit_name_to_lib_base: :: replaced by .", "[prod-lib][unit-name]") {
    REQUIRE(k::compiler::unit_name_to_lib_base("my::test::lib") == "my.test.lib");
}

TEST_CASE("unit_name_to_lib_base: single separator", "[prod-lib][unit-name]") {
    REQUIRE(k::compiler::unit_name_to_lib_base("math::utils")  == "math.utils");
}

TEST_CASE("unit_name_to_lib_base: empty string", "[prod-lib][unit-name]") {
    REQUIRE(k::compiler::unit_name_to_lib_base("") == "");
}


// ---------------------------------------------------------------------------
// Shared library (.so)
// ---------------------------------------------------------------------------

TEST_CASE("Shared library: simple module without main produces a .so", "[prod-lib][shared]") {
    std::string so_path;
    REQUIRE_NOTHROW(so_path = build_shared_library(R"SRC(
        module math::utils;

        namespace math {
            namespace utils {
                add(a: int, b: int) : int {
                    return a + b;
                }
            }
        }
    )SRC"));

    REQUIRE( !so_path.empty() );
    REQUIRE( std::filesystem::exists(so_path) );
    REQUIRE( std::filesystem::file_size(so_path) > 0 );

    // nm --dynamic --defined-only must report at least the 'add' symbol
    auto nm_res = k::tools::lookup_run_process("nm", {"--dynamic", "--defined-only", so_path});
    INFO( "nm stdout: " << nm_res.out );
    INFO( "nm stderr: " << nm_res.err );
    REQUIRE( nm_res.exit_code == 0 );
    REQUIRE( nm_res.out.find("add") != std::string::npos );

    std::filesystem::remove(so_path);
}

TEST_CASE("Shared library: compound module — symbol 'square' exported", "[prod-lib][shared]") {
    std::string so_path;
    REQUIRE_NOTHROW(so_path = build_shared_library(R"SRC(
        module mylib::core;

        namespace mylib {
            namespace core {
                square(x: int) : int {
                    return x * x;
                }
            }
        }
    )SRC"));

    REQUIRE( std::filesystem::exists(so_path) );
    REQUIRE( has_defined_symbol_containing(so_path, "square") );

    std::filesystem::remove(so_path);
}


// ---------------------------------------------------------------------------
// Static library (.a)
// ---------------------------------------------------------------------------

TEST_CASE("Static library: module without main produces a .a", "[prod-lib][static]") {
    std::string a_path;
    REQUIRE_NOTHROW(a_path = build_static_library(R"SRC(
        module math::utils;

        namespace math {
            namespace utils {
                add(a: int, b: int) : int {
                    return a + b;
                }
            }
        }
    )SRC"));

    REQUIRE( !a_path.empty() );
    REQUIRE( std::filesystem::exists(a_path) );
    REQUIRE( std::filesystem::file_size(a_path) > 0 );

    // nm on an archive must report the 'add' symbol
    auto nm_res = k::tools::lookup_run_process("nm", {"--defined-only", a_path});
    INFO( "nm stdout: " << nm_res.out );
    INFO( "nm stderr: " << nm_res.err );
    REQUIRE( nm_res.exit_code == 0 );
    REQUIRE( nm_res.out.find("add") != std::string::npos );

    std::filesystem::remove(a_path);
}

TEST_CASE("Static library: compound module — symbol 'cube' present", "[prod-lib][static]") {
    std::string a_path;
    REQUIRE_NOTHROW(a_path = build_static_library(R"SRC(
        module math::extra;

        namespace math {
            namespace extra {
                cube(x: int) : int {
                    return x * x * x;
                }
            }
        }
    )SRC"));

    REQUIRE( std::filesystem::exists(a_path) );
    REQUIRE( has_defined_symbol_containing(a_path, "cube") );

    std::filesystem::remove(a_path);
}


// ---------------------------------------------------------------------------
// Both libraries in a single compilation pass (gen_libraries)
// ---------------------------------------------------------------------------

TEST_CASE("Both libraries: single pass produces .so and .a with same symbols", "[prod-lib][both]") {
    auto [so_path, a_path] = build_both_libraries(R"SRC(
        module geometry::shapes;

        namespace geometry {
            namespace shapes {
                area_rect(w: int, h: int) : int {
                    return w * h;
                }
                perimeter_rect(w: int, h: int) : int {
                    return 2 * (w + h);
                }
            }
        }
    )SRC");

    // Both files must exist and be non-empty
    REQUIRE( std::filesystem::exists(so_path) );
    REQUIRE( std::filesystem::file_size(so_path) > 0 );
    REQUIRE( std::filesystem::exists(a_path) );
    REQUIRE( std::filesystem::file_size(a_path) > 0 );

    // The shared library must export both symbols
    auto nm_so = k::tools::lookup_run_process("nm", {"--dynamic", "--defined-only", so_path});
    INFO(".so nm: " << nm_so.out);
    REQUIRE( nm_so.exit_code == 0 );
    REQUIRE( nm_so.out.find("area_rect") != std::string::npos );
    REQUIRE( nm_so.out.find("perimeter_rect") != std::string::npos );

    // The static archive must contain both symbols too
    auto nm_a = k::tools::lookup_run_process("nm", {"--defined-only", a_path});
    INFO(".a  nm: " << nm_a.out);
    REQUIRE( nm_a.exit_code == 0 );
    REQUIRE( nm_a.out.find("area_rect") != std::string::npos );
    REQUIRE( nm_a.out.find("perimeter_rect") != std::string::npos );

    std::filesystem::remove(so_path);
    std::filesystem::remove(a_path);
}

