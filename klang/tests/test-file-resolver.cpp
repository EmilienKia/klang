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

#include "../src/common/file_resolver.hpp"
#include "../src/common/path_lookup_file_resolver.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static std::atomic<int> g_tmp_counter{0};

/// Create a temporary directory with RAII cleanup.
struct TmpDir {
    fs::path path;
    TmpDir() {
        path = fs::temp_directory_path() / ("klang_test_resolver_" +
               std::to_string(++g_tmp_counter));
        fs::create_directories(path);
    }
    ~TmpDir() { fs::remove_all(path); }
    /// Create a file inside the tmp dir; returns its path.
    fs::path create_file(const std::string& name) const {
        auto p = path / name;
        std::ofstream{p.string()}.close();
        return p;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// file_resolver::module_name_to_file_base
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("file_resolver::module_name_to_file_base — simple", "[resolver]") {
    REQUIRE( k::file_resolver::module_name_to_file_base("foo") == "foo" );
}

TEST_CASE("file_resolver::module_name_to_file_base — two parts", "[resolver]") {
    REQUIRE( k::file_resolver::module_name_to_file_base("math::vec") == "math.vec" );
}

TEST_CASE("file_resolver::module_name_to_file_base — three parts", "[resolver]") {
    REQUIRE( k::file_resolver::module_name_to_file_base("a::b::c") == "a.b.c" );
}

// ─────────────────────────────────────────────────────────────────────────────
// path_lookup_file_resolver — basic resolution
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("path_lookup_file_resolver — file not found returns nullopt", "[resolver]") {
    k::path_lookup_file_resolver r;
    REQUIRE_FALSE( r.resolve("no::such::module", ".kdi") );
}

TEST_CASE("path_lookup_file_resolver — found via search dir (.kdi)", "[resolver][live]") {
    TmpDir tmp;
    tmp.create_file("math.vec.kdi");

    k::path_lookup_file_resolver r;
    r.add_search_dir(tmp.path);

    auto result = r.resolve("math::vec", ".kdi");
    REQUIRE( result.has_value() );
    REQUIRE( result->filename().string() == "math.vec.kdi" );
}

TEST_CASE("path_lookup_file_resolver — found via search dir (.so)", "[resolver][live]") {
    TmpDir tmp;
    tmp.create_file("libmath.vec.so");

    k::path_lookup_file_resolver r;
    r.add_search_dir(tmp.path);

    auto result = r.resolve("math::vec", ".so");
    REQUIRE( result.has_value() );
    REQUIRE( result->filename().string() == "libmath.vec.so" );
}

TEST_CASE("path_lookup_file_resolver — first dir wins over second", "[resolver][live]") {
    TmpDir tmp1, tmp2;
    auto p1 = tmp1.create_file("foo.kdi");
    tmp2.create_file("foo.kdi");

    k::path_lookup_file_resolver r;
    r.add_search_dir(tmp1.path);
    r.add_search_dir(tmp2.path);

    auto result = r.resolve("foo", ".kdi");
    REQUIRE( result.has_value() );
    REQUIRE( result->parent_path() == tmp1.path );
}

TEST_CASE("path_lookup_file_resolver — explicit path takes priority over dir", "[resolver][live]") {
    TmpDir tmp;
    auto f_explicit = tmp.create_file("explicit.kdi");
    tmp.create_file("math.vec.kdi");

    k::path_lookup_file_resolver r;
    r.add_explicit_path("math::vec", f_explicit);
    r.add_search_dir(tmp.path);

    auto result = r.resolve("math::vec", ".kdi");
    REQUIRE( result.has_value() );
    REQUIRE( result->filename().string() == "explicit.kdi" );
}

TEST_CASE("path_lookup_file_resolver — env var dirs are searched", "[resolver][live]") {
    TmpDir tmp;
    tmp.create_file("mylib.kdi");

    // Set the env var to point to our tmp dir
    const std::string env_name = "KLANG_TEST_RESOLVER_PATH_" +
        std::to_string(++g_tmp_counter);
    ::setenv(env_name.c_str(), tmp.path.c_str(), 1);

    k::path_lookup_file_resolver r;
    r.add_dirs_from_env(env_name);

    auto result = r.resolve("mylib", ".kdi");
    REQUIRE( result.has_value() );
    REQUIRE( result->filename().string() == "mylib.kdi" );

    ::unsetenv(env_name.c_str());
}

// ─────────────────────────────────────────────────────────────────────────────
// file_resolver::chain
// ─────────────────────────────────────────────────────────────────────────────

TEST_CASE("file_resolver::chain — first finds, second not called", "[resolver][live]") {
    TmpDir tmp1, tmp2;
    tmp1.create_file("lib.kdi");

    auto r1 = std::make_unique<k::path_lookup_file_resolver>();
    r1->add_search_dir(tmp1.path);

    auto r2 = std::make_unique<k::path_lookup_file_resolver>();
    r2->add_search_dir(tmp2.path);

    auto chain = k::file_resolver::chain(std::move(r1), std::move(r2));
    auto result = chain->resolve("lib", ".kdi");
    REQUIRE( result.has_value() );
    REQUIRE( result->parent_path() == tmp1.path );
}

TEST_CASE("file_resolver::chain — first fails, second finds", "[resolver][live]") {
    TmpDir tmp1, tmp2;
    tmp2.create_file("lib.kdi");

    auto r1 = std::make_unique<k::path_lookup_file_resolver>();
    r1->add_search_dir(tmp1.path);

    auto r2 = std::make_unique<k::path_lookup_file_resolver>();
    r2->add_search_dir(tmp2.path);

    auto chain = k::file_resolver::chain(std::move(r1), std::move(r2));
    auto result = chain->resolve("lib", ".kdi");
    REQUIRE( result.has_value() );
    REQUIRE( result->parent_path() == tmp2.path );
}

TEST_CASE("file_resolver::chain — both fail returns nullopt", "[resolver][live]") {
    TmpDir tmp1, tmp2;
    // no files

    auto r1 = std::make_unique<k::path_lookup_file_resolver>();
    r1->add_search_dir(tmp1.path);

    auto r2 = std::make_unique<k::path_lookup_file_resolver>();
    r2->add_search_dir(tmp2.path);

    auto chain = k::file_resolver::chain(std::move(r1), std::move(r2));
    REQUIRE_FALSE( chain->resolve("lib", ".kdi") );
}



