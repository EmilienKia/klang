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
#include "../src/common/process.hpp"

using namespace k::tools;

// ─────────────────────────────────────────────────────────────────────────────
// tool_not_found
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("tool_not_found: inherits from std::runtime_error", "[process]") {
    tool_not_found ex("mytool");
    REQUIRE( dynamic_cast<const std::runtime_error*>(&ex) != nullptr );
}

TEST_CASE("tool_not_found: what() contains tool name", "[process]") {
    tool_not_found ex("clang");
    std::string msg(ex.what());
    REQUIRE( msg.find("clang") != std::string::npos );
}

TEST_CASE("tool_not_found: tool_name member is set", "[process]") {
    tool_not_found ex("ar");
    REQUIRE( ex.tool_name == "ar" );
}

// ─────────────────────────────────────────────────────────────────────────────
// lookup_tool
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("lookup_tool: finds a known tool (nm)", "[process][live]") {
    // nm is required by our test suite anyway; if it's missing all kdi tests
    // would also fail.
    std::filesystem::path p;
    REQUIRE_NOTHROW( p = lookup_tool("nm") );
    REQUIRE( !p.empty() );
    REQUIRE( p.is_absolute() );
}

TEST_CASE("lookup_tool: finds a known tool (ar)", "[process][live]") {
    std::filesystem::path p;
    REQUIRE_NOTHROW( p = lookup_tool("ar") );
    REQUIRE( !p.empty() );
}

TEST_CASE("lookup_tool: throws tool_not_found for non-existent tool", "[process]") {
    REQUIRE_THROWS_AS(
        lookup_tool("__klang_test_nonexistent_tool_xyz_99__"),
        tool_not_found
    );
}

TEST_CASE("lookup_tool: thrown exception contains tool name", "[process]") {
    try {
        lookup_tool("__klang_test_nonexistent_tool_xyz_99__");
        FAIL("Should have thrown");
    } catch (const tool_not_found& ex) {
        REQUIRE( std::string(ex.what()).find("__klang_test_nonexistent_tool_xyz_99__")
                 != std::string::npos );
        REQUIRE( ex.tool_name == "__klang_test_nonexistent_tool_xyz_99__" );
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// lookup_run_process
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("lookup_run_process: throws tool_not_found for non-existent tool", "[process]") {
    REQUIRE_THROWS_AS(
        lookup_run_process("__klang_test_nonexistent_tool_xyz_99__", {}),
        tool_not_found
    );
}

TEST_CASE("lookup_run_process: runs a real tool (nm --version)", "[process][live]") {
    exec_result r;
    REQUIRE_NOTHROW( r = lookup_run_process("nm", {"--version"}) );
    REQUIRE( r.exit_code == 0 );
    REQUIRE( (r.out.find("nm") != std::string::npos
              || r.err.find("nm") != std::string::npos) );
}

