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

#include <filesystem>
#include <fstream>
#include <unistd.h>

#include "helpers.hpp"

TEST_CASE("klangc: -g emits DWARF sections in executable", "[klangc][debug][dwarf]") {
    auto klangc = find_klangc();

    char k_file[] = "/tmp/klang_dbg_exe_XXXXXX";
    int fd = ::mkstemp(k_file);
    REQUIRE(fd != -1);
    ::close(fd);

    std::string k_path = std::string(k_file) + ".k";
    std::string exe_path = std::string(k_file) + ".exe";
    std::filesystem::remove(k_file);

    {
        std::ofstream ofs(k_path);
        ofs << R"(
            module dbg_exe;
            main() : int {
                x: int = 41;
                return x + 1;
            }
        )";
    }

    auto res = k::tools::run_process(klangc.string(), {"-g", "-o", exe_path, k_path});
    INFO("klangc stdout: " << res.out);
    INFO("klangc stderr: " << res.err);
    REQUIRE(res.exit_code == 0);
    REQUIRE(std::filesystem::exists(exe_path));

    REQUIRE(has_section_containing(exe_path, ".debug_info"));
    REQUIRE(has_section_containing(exe_path, ".debug_line"));

    std::filesystem::remove(exe_path);
    std::filesystem::remove(k_path);
}

TEST_CASE("klangc: --dyn-lib -g emits DWARF sections in shared library", "[klangc][debug][dwarf]") {
    auto klangc = find_klangc();

    char k_file[] = "/tmp/klang_dbg_so_XXXXXX";
    int fd = ::mkstemp(k_file);
    REQUIRE(fd != -1);
    ::close(fd);

    std::string k_path = std::string(k_file) + ".k";
    std::string so_path = std::string(k_file) + ".so";
    std::filesystem::remove(k_file);

    {
        std::ofstream ofs(k_path);
        ofs << R"(
            module dbg_so;
            namespace dbg_so {
                plus_one(v: int) : int {
                    return v + 1;
                }
            }
        )";
    }

    auto res = k::tools::run_process(klangc.string(), {"--dyn-lib", "-g", "--no-emit-kdi", "-o", so_path, k_path});
    INFO("klangc stdout: " << res.out);
    INFO("klangc stderr: " << res.err);
    REQUIRE(res.exit_code == 0);
    REQUIRE(std::filesystem::exists(so_path));

    REQUIRE(has_section_containing(so_path, ".debug_info"));
    REQUIRE(has_section_containing(so_path, ".debug_line"));

    std::filesystem::remove(so_path);
    std::filesystem::remove(k_path);
}

