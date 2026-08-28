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

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>

#ifndef KDITOOL_PATH
#error "KDITOOL_PATH must be defined -- check CMakeLists.txt"
#endif

using namespace kdi;

namespace {

kdi_file make_query_file() {
    kdi_file f;
    f.header.module_name = "demo";
    f.header.lib_base = "demo";
    f.unit.name = "demo";
    f.unit.root_ns.fq_name = "demo";

    kdi_function fn;
    fn.name = "add";
    fn.fq_name = "demo::add";
    fn.return_type = kdi_type::make_int(32);
    fn.params.push_back({"a", kdi_type::make_int(32), false});
    fn.params.push_back({"b", kdi_type::make_int(32), false});
    fn.mangled_name = "_KFN4demo3addEii";
    f.unit.root_ns.functions.push_back(fn);

    kdi_aggregate agg;
    agg.kind = kdi_aggregate_kind::class_;
    agg.name = "Point";
    agg.fq_name = "demo::Point";
    agg.mangled_name = "_KN4demo5PointE";

    kdi_constructor ctor;
    ctor.params.push_back({"x", kdi_type::make_int(32), false});
    ctor.mangled_name = "_KFMC1N4demo5PointEi";
    ctor.mangled_name_c2 = "_KFMC2N4demo5PointEi";
    agg.constructors.push_back(ctor);

    kdi_layout_member x;
    x.name = "x";
    x.fq_name = "demo::Point::x";
    x.llvm_field_index = 0;
    x.type = kdi_type::make_int(32);
    x.mangled_name = "_KVMN4demo5Point1xE";
    agg.layout.push_back(x);

    kdi_method len;
    len.name = "len";
    len.fq_name = "demo::Point::len";
    len.is_const_member = true;
    len.return_type = kdi_type::make_float(64);
    len.mangled_name = "_KFMKN4demo5Point3lenEv";
    agg.methods.push_back(len);

    f.unit.root_ns.aggregates.push_back(agg);
    return f;
}

struct command_result {
    int exit_code = -1;
    std::string out;
};

command_result run_command(const std::string& command) {
    FILE* pipe = ::popen(command.c_str(), "r");
    REQUIRE(pipe != nullptr);
    std::array<char, 256> buf{};
    std::string out;
    while (std::fgets(buf.data(), static_cast<int>(buf.size()), pipe)) {
        out += buf.data();
    }
    int rc = ::pclose(pipe);
    int exit_code = WIFEXITED(rc) ? WEXITSTATUS(rc) : rc;
    return {exit_code, out};
}

std::filesystem::path write_temp_kdi(const kdi_file& file) {
    auto path = std::filesystem::temp_directory_path()
              / ("kdi_query_test_" + std::to_string(::getpid()) + ".kdi");
    REQUIRE(kdi_write_cbor_file(file, path.string()));
    return path;
}

} // namespace

TEST_CASE("query: symbols can be listed and searched", "[query]") {
    auto file = make_query_file();
    auto all = kdi_list_symbols(file);
    REQUIRE(std::ranges::any_of(all, [](const auto& row) {
        return row.kind == "function" && row.fq_name == "demo::add"
            && row.signature == "add(a: int, b: int) : int";
    }));
    REQUIRE(std::ranges::any_of(all, [](const auto& row) {
        return row.kind == "method" && row.fq_name == "demo::Point::len";
    }));

    auto filtered = kdi_list_symbols(file, "add");
    REQUIRE(filtered.size() == 1);
    REQUIRE(filtered[0].mangled_name == "_KFN4demo3addEii");
}

TEST_CASE("query: aggregate members are direct and TSV-formatted", "[query]") {
    auto file = make_query_file();
    auto rows = kdi_list_aggregate_members(file, "demo::Point");
    REQUIRE(std::ranges::any_of(rows, [](const auto& row) {
        return row.kind == "field" && row.fq_name == "demo::Point::x";
    }));
    REQUIRE(std::ranges::any_of(rows, [](const auto& row) {
        return row.kind == "method" && row.signature == "const len() : double";
    }));

    std::ostringstream out;
    kdi_write_symbol_rows_tsv(rows, out, true);
    REQUIRE(out.str().find("kind\tfq_name\towner_fq_name\tname\tmangled_name\tsignature\n") == 0);
    REQUIRE(out.str().find("method\tdemo::Point::len\tdemo::Point\tlen\t_KFMKN4demo5Point3lenEv\tconst len() : double\n")
            != std::string::npos);
}

TEST_CASE("kditool: symbols and members commands emit TSV", "[query][cli]") {
    auto path = write_temp_kdi(make_query_file());

    auto symbols = run_command(std::string("\"") + KDITOOL_PATH + "\" symbols --headers \""
                               + path.string() + "\" add");
    REQUIRE(symbols.exit_code == 0);
    REQUIRE(symbols.out.find("kind\tfq_name\towner_fq_name\tname\tmangled_name\tsignature\n") == 0);
    REQUIRE(symbols.out.find("function\tdemo::add\tdemo\tadd\t_KFN4demo3addEii\tadd(a: int, b: int) : int\n")
            != std::string::npos);

    auto members = run_command(std::string("\"") + KDITOOL_PATH + "\" members \""
                               + path.string() + "\" demo::Point");
    REQUIRE(members.exit_code == 0);
    REQUIRE(members.out.find("field\tdemo::Point::x\tdemo::Point\tx\t_KVMN4demo5Point1xE\tx: int @0\n")
            != std::string::npos);

    std::filesystem::remove(path);
}
