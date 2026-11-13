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

#include "../src/common/file_resolver.hpp"

namespace fs = std::filesystem;

TEST_CASE("temporary directories are unique and clean up nested files",
          "[resolver][live][test-temp]") {
    TmpDir parent;
    fs::path first_path;
    fs::path second_path;
    {
        TmpDir first(parent.path);
        TmpDir second(parent.path);
        first_path = first.path;
        second_path = second.path;
        REQUIRE(first_path.parent_path() == parent.path);
        REQUIRE(second_path.parent_path() == parent.path);
        REQUIRE(first_path != second_path);
        REQUIRE(fs::create_directory(first_path / "nested"));
        std::ofstream(first_path / "nested" / "data") << "test";
        REQUIRE(fs::exists(first_path / "nested" / "data"));
    }
    CHECK_FALSE(fs::exists(first_path));
    CHECK_FALSE(fs::exists(second_path));
}

TEST_CASE("temporary directory cleans up during exception unwinding",
          "[resolver][live][test-temp]") {
    TmpDir parent;
    fs::path child;
    try {
        TmpDir temporary(parent.path);
        child = temporary.path;
        temporary.create_file("data");
        throw std::runtime_error("expected");
    } catch (const std::runtime_error&) {}
    REQUIRE_FALSE(child.empty());
    CHECK_FALSE(fs::exists(child));
    REQUIRE_THROWS(TmpDir(parent.path / "missing"));
}

TEST_CASE("temporary paths are escaped in generated K string literals",
          "[resolver][test-temp]") {
    const fs::path path{R"(name"with\quotes)"};
    CHECK(replace_k_path(R"(open("@PATH@"); open("@PATH@");)", "@PATH@", path) ==
          R"(open("name\"with\\quotes"); open("name\"with\\quotes");)");
}

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
    static std::atomic<int> s_env_counter{0};
    const std::string env_name = "KLANG_TEST_RESOLVER_PATH_" +
        std::to_string(++s_env_counter);
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
