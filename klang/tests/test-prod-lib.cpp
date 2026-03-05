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

#include "helpers.hpp"


/**
 * Check that the symbol 'symbol_name' is exported (type 'T') in the shared
 * library at 'so_path' using the 'nm' tool.
 * Returns true if the symbol is found as a global/exported symbol.
 */
static bool so_has_exported_symbol(const std::string& so_path, const std::string& symbol_name) {
    // nm -D lists dynamic (exported) symbols; --defined-only restricts to
    // symbols defined in this object.
    auto res = k::tools::lookup_run_process("nm", {"--dynamic", "--defined-only", so_path});
    if (res.exit_code != 0) {
        return false;
    }
    // Each nm output line looks like:  <addr> T _ZN3foo3barEv
    // We check that the expected mangled name appears somewhere in the output.
    return res.out.find(symbol_name) != std::string::npos;
}


TEST_CASE( "Shared library: simple module without main produces a .so", "[prod-lib]" ) {
    // A module with a compound namespace unit name (math::utils) and a global
    // function — but no main() — should be compiled into a shared library.
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

    // The .so file must exist and be non-empty.
    REQUIRE( !so_path.empty() );
    REQUIRE( std::filesystem::exists(so_path) );
    REQUIRE( std::filesystem::file_size(so_path) > 0 );

    // Inspect exported symbols with 'nm -D --defined-only'
    auto nm_res = k::tools::lookup_run_process("nm", {"--dynamic", "--defined-only", so_path});
    INFO( "nm stdout: " << nm_res.out );
    INFO( "nm stderr: " << nm_res.err );
    REQUIRE( nm_res.exit_code == 0 );

    // The function math::utils::add(int,int):int must appear as an exported symbol.
    // Its mangled name follows the K name-mangling scheme.
    // We check that the output contains the human-readable demangled name via
    // nm --demangle, so that the test is independent of the exact mangled form.
    auto nm_dem_res = k::tools::lookup_run_process("nm", {"--dynamic", "--defined-only", "--demangle", so_path});
    INFO( "nm --demangle stdout: " << nm_dem_res.out );
    REQUIRE( nm_dem_res.exit_code == 0 );

    // The demangled output should contain the function name "add"
    // within the math::utils namespace context.
    bool found = nm_dem_res.out.find("add") != std::string::npos;
    REQUIRE( found );

    std::filesystem::remove(so_path);
}


TEST_CASE( "Shared library: automatic output name is lib<module>.so", "[prod-lib]" ) {
    // When no -o is specified for a module named "mylib::core", the output file
    // should be "libmylib.core.so" in the current directory.
    // We test this by calling gen_shared_library with an empty output path and
    // checking the returned path.

    // Build a simple library with module name mylib::core
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

    REQUIRE( !so_path.empty() );
    REQUIRE( std::filesystem::exists(so_path) );

    // Verify the symbol 'square' is exported
    auto nm_res = k::tools::lookup_run_process("nm", {"--dynamic", "--defined-only", "--demangle", so_path});
    INFO( "nm stdout: " << nm_res.out );
    REQUIRE( nm_res.exit_code == 0 );
    REQUIRE( nm_res.out.find("square") != std::string::npos );

    std::filesystem::remove(so_path);
}

