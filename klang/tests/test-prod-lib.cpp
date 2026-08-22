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

#include <kdi.hpp>
#include <kdi_symbols.hpp>

#include "helpers.hpp"

#include <set>

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
        module math::utils_pl01;

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
        module math::utils_pl02;

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

// ---------------------------------------------------------------------------
// KDI file generation
// ---------------------------------------------------------------------------

TEST_CASE("KDI: .kdi file generated alongside shared library", "[prod-lib][kdi][shared]") {
    std::string so_path;
    REQUIRE_NOTHROW(so_path = build_shared_library(R"SRC(
        module kdi::test::sharedlib;

        namespace kdi {
            namespace test {
                namespace sharedlib {
                    compute(x: int) : int {
                        return x * 2;
                    }
                }
            }
        }
    )SRC"));

    REQUIRE( std::filesystem::exists(so_path) );

    // A .kdi file must have been produced next to the .so
    auto kdi_p = kdi_path_for(so_path);
    INFO("Expected KDI path: " << kdi_p);
    REQUIRE( std::filesystem::exists(kdi_p) );
    REQUIRE( std::filesystem::file_size(kdi_p) > 0 );

    // Parse and validate the KDI file
    kdi::kdi_file kdi_file;
    REQUIRE_NOTHROW( kdi_file = kdi::kdi_read_cbor_file(kdi_p.string()) );

    // Schema version must match current KDI_SCHEMA constants
    REQUIRE( kdi_file.header.schema_major == kdi::KDI_SCHEMA_MAJOR );
    REQUIRE( kdi_file.header.schema_minor == kdi::KDI_SCHEMA_MINOR );

    // Module name must match
    REQUIRE( kdi_file.header.module_name == "kdi::test::sharedlib" );

    // Lib base must be derived correctly
    REQUIRE( kdi_file.header.lib_base == "kdi.test.sharedlib" );

    // Walk the namespace tree to find the 'compute' function
    // root_ns → kdi → test → sharedlib → functions
    bool found_compute = false;
    for (auto& ns1 : kdi_file.unit.root_ns.namespaces) {
        if (ns1.name != "kdi") continue;
        for (auto& ns2 : ns1.namespaces) {
            if (ns2.name != "test") continue;
            for (auto& ns3 : ns2.namespaces) {
                if (ns3.name != "sharedlib") continue;
                for (auto& fn : ns3.functions) {
                    if (fn.name == "compute") {
                        found_compute = true;
                        // Mangled name must be non-empty
                        REQUIRE_FALSE( fn.mangled_name.empty() );
                        // Return type: int (32-bit signed)
                        REQUIRE( std::holds_alternative<kdi::kdi_int_type>(fn.return_type.value) );
                        // One parameter: x: int
                        REQUIRE( fn.params.size() == 1 );
                        REQUIRE( fn.params[0].name == "x" );
                    }
                }
            }
        }
    }
    INFO("KDI root_ns.namespaces: " << kdi_file.unit.root_ns.namespaces.size());
    REQUIRE( found_compute );

    // kdi validate must report VALID
    auto val_result = kdi::kdi_validate(kdi_file);
    for (auto& e : val_result.errors) INFO("KDI validation error: " << e.path << ": " << e.message);
    REQUIRE( val_result.is_valid() );

    std::filesystem::remove(so_path);
    std::filesystem::remove(kdi_p);
}

TEST_CASE("KDI: .kdi file generated alongside static library", "[prod-lib][kdi][static]") {
    std::string a_path;
    REQUIRE_NOTHROW(a_path = build_static_library(R"SRC(
        module kdi::test::staticlib;

        namespace kdi {
            namespace test {
                namespace staticlib {
                    triple(x: int) : int {
                        return x * 3;
                    }
                }
            }
        }
    )SRC"));

    REQUIRE( std::filesystem::exists(a_path) );

    auto kdi_p = kdi_path_for(a_path);
    INFO("Expected KDI path: " << kdi_p);
    REQUIRE( std::filesystem::exists(kdi_p) );

    kdi::kdi_file kdi_file2;
    REQUIRE_NOTHROW( kdi_file2 = kdi::kdi_read_cbor_file(kdi_p.string()) );
    REQUIRE( kdi_file2.header.module_name == "kdi::test::staticlib" );
    REQUIRE( kdi_file2.header.schema_major == kdi::KDI_SCHEMA_MAJOR );
    REQUIRE( kdi_file2.header.schema_minor == kdi::KDI_SCHEMA_MINOR );

    // Find 'triple'
    bool found = false;
    for (auto& ns1 : kdi_file2.unit.root_ns.namespaces) {
        if (ns1.name != "kdi") continue;
        for (auto& ns2 : ns1.namespaces) {
            if (ns2.name != "test") continue;
            for (auto& ns3 : ns2.namespaces) {
                if (ns3.name != "staticlib") continue;
                for (auto& fn : ns3.functions) {
                    if (fn.name == "triple") { found = true; REQUIRE_FALSE(fn.mangled_name.empty()); }
                }
            }
        }
    }
    REQUIRE( found );

    std::filesystem::remove(a_path);
    std::filesystem::remove(kdi_p);
}

TEST_CASE("KDI: both-library pass produces a single .kdi keyed on .so", "[prod-lib][kdi][both]") {
    auto [so_path, a_path] = build_both_libraries(R"SRC(
        module kdi::test::bothlibs;

        namespace kdi {
            namespace test {
                namespace bothlibs {
                    halve(x: int) : int {
                        return x / 2;
                    }
                }
            }
        }
    )SRC");

    REQUIRE( std::filesystem::exists(so_path) );
    REQUIRE( std::filesystem::exists(a_path) );

    // KDI is keyed on the .so
    auto kdi_p = kdi_path_for(so_path);
    REQUIRE( std::filesystem::exists(kdi_p) );

    kdi::kdi_file kdi_file3;
    REQUIRE_NOTHROW( kdi_file3 = kdi::kdi_read_cbor_file(kdi_p.string()) );
    REQUIRE( kdi_file3.header.module_name == "kdi::test::bothlibs" );

    bool found = false;
    for (auto& ns1 : kdi_file3.unit.root_ns.namespaces) {
        if (ns1.name != "kdi") continue;
        for (auto& ns2 : ns1.namespaces) {
            if (ns2.name != "test") continue;
            for (auto& ns3 : ns2.namespaces) {
                if (ns3.name != "bothlibs") continue;
                for (auto& fn : ns3.functions) {
                    if (fn.name == "halve") { found = true; }
                }
            }
        }
    }
    REQUIRE( found );

    std::filesystem::remove(so_path);
    std::filesystem::remove(a_path);
    std::filesystem::remove(kdi_p);
}

// ---------------------------------------------------------------------------
// check-symbols: cross-check .kdi against the produced binary
// ---------------------------------------------------------------------------

TEST_CASE("check-symbols: .kdi vs .so — all symbols present", "[prod-lib][check-symbols][so]") {
    std::string so_path;
    REQUIRE_NOTHROW(so_path = build_shared_library(R"SRC(
        module chksym::sotest;

        namespace chksym {
            namespace sotest {
                multiply(a: int, b: int) : int {
                    return a * b;
                }
                subtract(a: int, b: int) : int {
                    return a - b;
                }
            }
        }
    )SRC"));

    REQUIRE( std::filesystem::exists(so_path) );
    auto kdi_p = kdi_path_for(so_path);
    REQUIRE( std::filesystem::exists(kdi_p) );

    kdi::kdi_file kdi_file;
    REQUIRE_NOTHROW( kdi_file = kdi::kdi_read_cbor_file(kdi_p.string()) );

    // Collect symbols from the .so and check
    std::set<std::string> binary_syms;
    REQUIRE_NOTHROW( binary_syms = kdi::kdi_collect_binary_symbols(so_path) );
    REQUIRE( !binary_syms.empty() );

    auto result = kdi::kdi_check_symbols(kdi_file, binary_syms);
    INFO( "Missing symbols:" );
    for (auto& m : result.missing) INFO( "  [" << m.context << "] " << m.mangled_name );
    REQUIRE( result.is_ok() );

    std::filesystem::remove(so_path);
    std::filesystem::remove(kdi_p);
}

TEST_CASE("check-symbols: .kdi vs .a — all symbols present", "[prod-lib][check-symbols][a]") {
    std::string a_path;
    REQUIRE_NOTHROW(a_path = build_static_library(R"SRC(
        module chksym::atest;

        namespace chksym {
            namespace atest {
                divide(a: int, b: int) : int {
                    return a / b;
                }
            }
        }
    )SRC"));

    REQUIRE( std::filesystem::exists(a_path) );
    auto kdi_p = kdi_path_for(a_path);
    REQUIRE( std::filesystem::exists(kdi_p) );

    kdi::kdi_file kdi_file;
    REQUIRE_NOTHROW( kdi_file = kdi::kdi_read_cbor_file(kdi_p.string()) );

    auto result = kdi::kdi_check_symbols(kdi_file, a_path);
    INFO( "Missing symbols:" );
    for (auto& m : result.missing) INFO( "  [" << m.context << "] " << m.mangled_name );
    REQUIRE( result.is_ok() );

    std::filesystem::remove(a_path);
    std::filesystem::remove(kdi_p);
}

TEST_CASE("check-symbols: convenience overload (path) works end-to-end", "[prod-lib][check-symbols][so]") {
    std::string so_path;
    REQUIRE_NOTHROW(so_path = build_shared_library(R"SRC(
        module chksym::conv;

        namespace chksym {
            namespace conv {
                negate(x: int) : int {
                    return 0 - x;
                }
            }
        }
    )SRC"));

    REQUIRE( std::filesystem::exists(so_path) );
    auto kdi_p = kdi_path_for(so_path);
    REQUIRE( std::filesystem::exists(kdi_p) );

    kdi::kdi_file kdi_file;
    REQUIRE_NOTHROW( kdi_file = kdi::kdi_read_cbor_file(kdi_p.string()) );

    // Use the convenience overload (passes binary path directly)
    kdi::kdi_symbol_check_result result;
    REQUIRE_NOTHROW( result = kdi::kdi_check_symbols(kdi_file, so_path) );
    INFO( "Missing symbols:" );
    for (auto& m : result.missing) INFO( "  [" << m.context << "] " << m.mangled_name );
    REQUIRE( result.is_ok() );

    std::filesystem::remove(so_path);
    std::filesystem::remove(kdi_p);
}

