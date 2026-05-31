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

#include <llvm/Support/raw_ostream.h>

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

TEST_CASE("compiler: debug metadata contains parameters, locals and lexical blocks", "[klangc][debug][ir]") {
    auto comp = k::compiler::create();
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
    comp->set_file_resolver(resolver);
    comp->set_debug_info_options(k::DebugInfoOptions{.enabled = true, .line_tables_only = false, .dwarf_version = 5});

    comp->parse_source("debug_ir_test.k", R"(
        module debug_ir_test;

        compute(a: int) : int {
            result: int = a;
            if (a > 0) {
                inner: int = result + 1;
                result = inner;
            }
            return result;
        }
    )", true, false);

    std::string ir;
    llvm::raw_string_ostream os(ir);
    comp->get_context_for_test()->module().print(os, nullptr);
    os.flush();

    INFO(ir);
    REQUIRE(ir.find("llvm.dbg.declare") != std::string::npos);
    REQUIRE(ir.find("!DILocalVariable(name: \"a\", arg: 1") != std::string::npos);
    REQUIRE(ir.find("!DILocalVariable(name: \"result\"") != std::string::npos);
    REQUIRE(ir.find("!DILocalVariable(name: \"inner\"") != std::string::npos);
    REQUIRE(ir.find("!DILexicalBlock(") != std::string::npos);
}

TEST_CASE("compiler: debug metadata covers loop scopes and catch variables", "[klangc][debug][ir]") {
    auto comp = k::compiler::create();
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
    comp->set_file_resolver(resolver);
    comp->set_debug_info_options(k::DebugInfoOptions{.enabled = true, .line_tables_only = false, .dwarf_version = 5});

    comp->parse_source("debug_flow_test.k", R"(
        module debug_flow_test;

        flow(a: int) : int {
            result: int = a;
            while (result > 0) {
                result = result - 1;
            }
            try {
                result = result + 1;
            } catch (caught: Exception*) {
                result = 0;
            }
            return result;
        }
    )", true, false);

    std::string ir;
    llvm::raw_string_ostream os(ir);
    comp->get_context_for_test()->module().print(os, nullptr);
    os.flush();

    INFO(ir);
    REQUIRE(ir.find("llvm.dbg.declare") != std::string::npos);
    REQUIRE(ir.find("!DILocalVariable(name: \"caught\"") != std::string::npos);
    REQUIRE(ir.find("!DILexicalBlock(") != std::string::npos);
}

TEST_CASE("klangc: debug DWARF contains catch variables and lexical blocks", "[klangc][debug][dwarf]") {
    auto klangc = find_klangc();

    char k_file[] = "/tmp/klang_dbg_throw_XXXXXX";
    int fd = ::mkstemp(k_file);
    REQUIRE(fd != -1);
    ::close(fd);

    std::string k_path = std::string(k_file) + ".k";
    std::string so_path = std::string(k_file) + ".so";
    std::filesystem::remove(k_file);

    {
        std::ofstream ofs(k_path);
        ofs << R"(
            module dbg_throw;

            class MyErr : public Exception { }

            flow(a: int) : int {
                result: int = a;
                try {
                    throw MyErr();
                } catch (caught: Exception*) {
                    result = 0;
                }
                return result;
            }
        )";
    }

    auto res = k::tools::run_process(klangc.string(), {"--dyn-lib", "-g", "--no-emit-kdi", "-o", so_path, k_path});
    INFO("klangc stdout: " << res.out);
    INFO("klangc stderr: " << res.err);
    REQUIRE(res.exit_code == 0);
    REQUIRE(std::filesystem::exists(so_path));

    auto dwarf = k::tools::run_process("/usr/bin/llvm-dwarfdump", {"--debug-info", so_path});
    INFO("llvm-dwarfdump stdout: " << dwarf.out);
    INFO("llvm-dwarfdump stderr: " << dwarf.err);
    REQUIRE(dwarf.exit_code == 0);
    REQUIRE(dwarf.out.find("caught") != std::string::npos);
    REQUIRE(dwarf.out.find("DW_TAG_lexical_block") != std::string::npos);

    std::filesystem::remove(so_path);
    std::filesystem::remove(k_path);
}

TEST_CASE("compiler: debug metadata gives loop scopes to single-statement bodies", "[klangc][debug][ir]") {
    auto comp = k::compiler::create();
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
    comp->set_file_resolver(resolver);
    comp->set_debug_info_options(k::DebugInfoOptions{.enabled = true, .line_tables_only = false, .dwarf_version = 5});

    comp->parse_source("debug_single_loop_scopes.k", R"(module debug_single_loop_scopes;

sum(n: int) : int {
    total: int = 0;
    while (n > 0)
        for (i: int = 0; i < 2; i += 1)
            total = total + i;
    return total;
}
)", true, false);

    std::string ir;
    llvm::raw_string_ostream os(ir);
    comp->get_context_for_test()->module().print(os, nullptr);
    os.flush();

    size_t lexical_block_count = 0;
    for (size_t pos = 0; (pos = ir.find("!DILexicalBlock(", pos)) != std::string::npos; ++lexical_block_count, ++pos) {}

    INFO(ir);
    REQUIRE(ir.find("!DILocalVariable(name: \"i\"") != std::string::npos);
    REQUIRE(ir.find("!DILocalVariable(name: \"total\"") != std::string::npos);
    REQUIRE(lexical_block_count >= 2);
}

TEST_CASE("compiler: debug metadata keeps control-flow exits on their source lines", "[klangc][debug][ir]") {
    auto comp = k::compiler::create();
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
    comp->set_file_resolver(resolver);
    comp->set_debug_info_options(k::DebugInfoOptions{.enabled = true, .line_tables_only = false, .dwarf_version = 5});

    comp->parse_source("debug_control_flow_locations.k", R"(module debug_control_flow_locations;

class MyErr : public Exception { }

flow(a: int) : int {
    while (a > 0) {
        break;
    }
    while (a > 1) {
        continue;
    }
    try {
        throw MyErr();
    } catch (caught: Exception*) {
        return a;
    }
}
)", false, false);

    std::string ir;
    llvm::raw_string_ostream os(ir);
    comp->get_context_for_test()->module().print(os, nullptr);
    os.flush();

    INFO(ir);
    REQUIRE(ir.find("!DILocation(line: 6,") != std::string::npos);
    REQUIRE(ir.find("!DILocation(line: 7,") != std::string::npos);
    REQUIRE(ir.find("!DILocation(line: 9,") != std::string::npos);
    REQUIRE(ir.find("!DILocation(line: 10,") != std::string::npos);
    REQUIRE(ir.find("!DILocation(line: 12,") != std::string::npos);
    REQUIRE(ir.find("!DILocation(line: 13,") != std::string::npos);
    REQUIRE(ir.find("!DILocation(line: 15,") != std::string::npos);
}

TEST_CASE("compiler: debug metadata declares implicit this parameter in member functions", "[klangc][debug][ir]") {
    auto comp = k::compiler::create();
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
    comp->set_file_resolver(resolver);
    comp->set_debug_info_options(k::DebugInfoOptions{.enabled = true, .line_tables_only = false, .dwarf_version = 5});

    comp->parse_source("debug_this_param.k", R"(module debug_this_param;

class Counter {
    value: int;

    add(delta: int) : int {
        return delta;
    }
}
)", false, false);

    std::string ir;
    llvm::raw_string_ostream os(ir);
    comp->get_context_for_test()->module().print(os, nullptr);
    os.flush();

    INFO(ir);
    REQUIRE(ir.find("!DILocalVariable(name: \"this\", arg: 1") != std::string::npos);
    REQUIRE(ir.find("!DILocalVariable(name: \"delta\", arg: 2") != std::string::npos);
}

TEST_CASE("compiler: debug metadata anchors explicit casts on cast lines", "[klangc][debug][ir]") {
    auto comp = k::compiler::create();
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
    comp->set_file_resolver(resolver);
    comp->set_debug_info_options(k::DebugInfoOptions{.enabled = true, .line_tables_only = false, .dwarf_version = 5});

    comp->parse_source("debug_cast_locations.k", R"(module debug_cast_locations;

cast_line(v: short) : int {
    widened: int =
        (int)
        v;
    return widened;
}
)", false, false);

    std::string ir;
    llvm::raw_string_ostream os(ir);
    comp->get_context_for_test()->module().print(os, nullptr);
    os.flush();

    auto extract_dbg_line_for_instruction = [&](const std::string& inst_text) -> int {
        const auto inst_pos = ir.find(inst_text);
        if (inst_pos == std::string::npos) {
            return -1;
        }
        const auto dbg_pos = ir.find("!dbg !", inst_pos);
        if (dbg_pos == std::string::npos) {
            return -1;
        }
        const auto id_start = dbg_pos + 6;
        const auto id_end = ir.find_first_not_of("0123456789", id_start);
        if (id_end == std::string::npos || id_end == id_start) {
            return -1;
        }
        const auto id = ir.substr(id_start, id_end - id_start);
        const auto meta_pos = ir.find("!" + id + " = !DILocation(line: ");
        if (meta_pos == std::string::npos) {
            return -1;
        }
        const auto line_start = meta_pos + std::string("!" + id + " = !DILocation(line: ").size();
        const auto line_end = ir.find_first_not_of("0123456789", line_start);
        if (line_end == std::string::npos || line_end == line_start) {
            return -1;
        }
        return std::stoi(ir.substr(line_start, line_end - line_start));
    };

    INFO(ir);
    REQUIRE(ir.find("!DILocation(line: 4,") != std::string::npos);
    REQUIRE(ir.find("!DILocation(line: 6,") != std::string::npos);
    REQUIRE(extract_dbg_line_for_instruction("sext i16") == 6);
}

TEST_CASE("compiler: catch-return finally path anchors __cxa_end_catch on source line", "[klangc][debug][ir]") {
    auto comp = k::compiler::create();
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
    comp->set_file_resolver(resolver);
    comp->set_debug_info_options(k::DebugInfoOptions{.enabled = true, .line_tables_only = false, .dwarf_version = 5});

    comp->parse_source("debug_finally_catch_return.k", R"(module debug_finally_catch_return;

class MyErr : public Exception { }

flow(a: int) : int {
    try {
        throw MyErr();
    } catch (caught: Exception*) {
        return a;
    } finally {
        a = a + 1;
    }
}
)", false, false);

    std::string ir;
    llvm::raw_string_ostream os(ir);
    comp->get_context_for_test()->module().print(os, nullptr);
    os.flush();

    auto extract_dbg_line_for_call = [&](const std::string& call_text) -> int {
        const auto call_pos = ir.find(call_text);
        if (call_pos == std::string::npos) {
            return -1;
        }
        const auto dbg_pos = ir.find("!dbg !", call_pos);
        if (dbg_pos == std::string::npos) {
            return -1;
        }
        const auto id_start = dbg_pos + 6;
        const auto id_end = ir.find_first_not_of("0123456789", id_start);
        if (id_end == std::string::npos || id_end == id_start) {
            return -1;
        }
        const auto id = ir.substr(id_start, id_end - id_start);
        const auto meta_pos = ir.find("!" + id + " = !DILocation(line: ");
        if (meta_pos == std::string::npos) {
            return -1;
        }
        const auto line_start = meta_pos + std::string("!" + id + " = !DILocation(line: ").size();
        const auto line_end = ir.find_first_not_of("0123456789", line_start);
        if (line_end == std::string::npos || line_end == line_start) {
            return -1;
        }
        return std::stoi(ir.substr(line_start, line_end - line_start));
    };

    INFO(ir);
    const int cxa_end_line = extract_dbg_line_for_call("call void @__cxa_end_catch()");
    REQUIRE(cxa_end_line == 9);
}

TEST_CASE("compiler: debug metadata anchors dereference loads on source line", "[klangc][debug][ir]") {
    auto comp = k::compiler::create();
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
    comp->set_file_resolver(resolver);
    comp->set_debug_info_options(k::DebugInfoOptions{.enabled = true, .line_tables_only = false, .dwarf_version = 5});

    comp->parse_source("debug_deref_line.k", R"(module debug_deref_line;

read(ptr: int*) : int {
    value: int =
        *ptr;
    return value;
}
)", false, false);

    std::string ir;
    llvm::raw_string_ostream os(ir);
    comp->get_context_for_test()->module().print(os, nullptr);
    os.flush();

    auto extract_dbg_line_for_instruction = [&](const std::string& inst_text) -> int {
        const auto inst_pos = ir.find(inst_text);
        if (inst_pos == std::string::npos) {
            return -1;
        }
        const auto dbg_pos = ir.find("!dbg !", inst_pos);
        if (dbg_pos == std::string::npos) {
            return -1;
        }
        const auto id_start = dbg_pos + 6;
        const auto id_end = ir.find_first_not_of("0123456789", id_start);
        if (id_end == std::string::npos || id_end == id_start) {
            return -1;
        }
        const auto id = ir.substr(id_start, id_end - id_start);
        const auto meta_pos = ir.find("!" + id + " = !DILocation(line: ");
        if (meta_pos == std::string::npos) {
            return -1;
        }
        const auto line_start = meta_pos + std::string("!" + id + " = !DILocation(line: ").size();
        const auto line_end = ir.find_first_not_of("0123456789", line_start);
        if (line_end == std::string::npos || line_end == line_start) {
            return -1;
        }
        return std::stoi(ir.substr(line_start, line_end - line_start));
    };

    INFO(ir);
    const int deref_line = extract_dbg_line_for_instruction("deref_load = load");
    const int deref_null_check_line = extract_dbg_line_for_instruction("deref_is_null");
    REQUIRE(deref_line == 5);
    REQUIRE(deref_null_check_line == 5);
}

TEST_CASE("compiler: debug metadata anchors dynamic-cast RTTI compare on cast line", "[klangc][debug][ir]") {
    auto comp = k::compiler::create();
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
    comp->set_file_resolver(resolver);
    comp->set_debug_info_options(k::DebugInfoOptions{.enabled = true, .line_tables_only = false, .dwarf_version = 5});

    comp->parse_source("debug_dynamic_cast_line.k", R"(module debug_dynamic_cast_line;

class Base {
    id() : int { return 0; }
}

class Derived : public Base {
    id() : int { return 1; }
}

cast_to_derived(base: Base*) : Derived* {
    derived: Derived* =
        (Derived*)
        base;
    return derived;
}
)", false, false);

    std::string ir;
    llvm::raw_string_ostream os(ir);
    comp->get_context_for_test()->module().print(os, nullptr);
    os.flush();

    auto extract_dbg_line_for_instruction = [&](const std::string& inst_text) -> int {
        const auto inst_pos = ir.find(inst_text);
        if (inst_pos == std::string::npos) {
            return -1;
        }
        const auto dbg_pos = ir.find("!dbg !", inst_pos);
        if (dbg_pos == std::string::npos) {
            return -1;
        }
        const auto id_start = dbg_pos + 6;
        const auto id_end = ir.find_first_not_of("0123456789", id_start);
        if (id_end == std::string::npos || id_end == id_start) {
            return -1;
        }
        const auto id = ir.substr(id_start, id_end - id_start);
        const auto meta_pos = ir.find("!" + id + " = !DILocation(line: ");
        if (meta_pos == std::string::npos) {
            return -1;
        }
        const auto line_start = meta_pos + std::string("!" + id + " = !DILocation(line: ").size();
        const auto line_end = ir.find_first_not_of("0123456789", line_start);
        if (line_end == std::string::npos || line_end == line_start) {
            return -1;
        }
        return std::stoi(ir.substr(line_start, line_end - line_start));
    };

    INFO(ir);
    const int rtti_cmp_line = extract_dbg_line_for_instruction("dyncast_rtti_match");
    const int result_line = extract_dbg_line_for_instruction("dyncast_result");
    REQUIRE(rtti_cmp_line == 14);
    REQUIRE(result_line == 14);
}

TEST_CASE("compiler: debug metadata anchors dynamic-cast nullable guard on cast line", "[klangc][debug][ir]") {
    auto comp = k::compiler::create();
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
    comp->set_file_resolver(resolver);
    comp->set_debug_info_options(k::DebugInfoOptions{.enabled = true, .line_tables_only = false, .dwarf_version = 5});

    comp->parse_source("debug_dynamic_cast_guard_line.k", R"(module debug_dynamic_cast_guard_line;

class Base {
    id() : int { return 0; }
}

class Derived : public Base {
    id() : int { return 1; }
}

guarded(base: Base*) : int {
    if (derived: Derived+ =
            (Derived+)
            base) {
        return 1;
    }
    return 0;
}
)", false, false);

    std::string ir;
    llvm::raw_string_ostream os(ir);
    comp->get_context_for_test()->module().print(os, nullptr);
    os.flush();

    auto extract_dbg_line_for_instruction = [&](const std::string& inst_text) -> int {
        const auto inst_pos = ir.find(inst_text);
        if (inst_pos == std::string::npos) {
            return -1;
        }
        const auto dbg_pos = ir.find("!dbg !", inst_pos);
        if (dbg_pos == std::string::npos) {
            return -1;
        }
        const auto id_start = dbg_pos + 6;
        const auto id_end = ir.find_first_not_of("0123456789", id_start);
        if (id_end == std::string::npos || id_end == id_start) {
            return -1;
        }
        const auto id = ir.substr(id_start, id_end - id_start);
        const auto meta_pos = ir.find("!" + id + " = !DILocation(line: ");
        if (meta_pos == std::string::npos) {
            return -1;
        }
        const auto line_start = meta_pos + std::string("!" + id + " = !DILocation(line: ").size();
        const auto line_end = ir.find_first_not_of("0123456789", line_start);
        if (line_end == std::string::npos || line_end == line_start) {
            return -1;
        }
        return std::stoi(ir.substr(line_start, line_end - line_start));
    };

    INFO(ir);
    const int guard_line = extract_dbg_line_for_instruction("dyncast_src_null");
    REQUIRE(guard_line == 14);
}

TEST_CASE("compiler: debug metadata anchors allocation null-check on new expression line", "[klangc][debug][ir]") {
    auto comp = k::compiler::create();
    auto resolver = std::make_shared<k::path_lookup_file_resolver>();
    resolver->add_search_dir(KLANG_STDLIB_LIB_DIR);
    comp->set_file_resolver(resolver);
    comp->set_debug_info_options(k::DebugInfoOptions{.enabled = true, .line_tables_only = false, .dwarf_version = 5});

    comp->parse_source("debug_alloc_null_line.k", R"(module debug_alloc_null_line;

class Box {
    value: int;
}

make() : Box! {
    return new Box();
}
)", false, false);

    std::string ir;
    llvm::raw_string_ostream os(ir);
    comp->get_context_for_test()->module().print(os, nullptr);
    os.flush();

    auto extract_dbg_line_for_instruction = [&](const std::string& inst_text) -> int {
        const auto inst_pos = ir.find(inst_text);
        if (inst_pos == std::string::npos) {
            return -1;
        }
        const auto dbg_pos = ir.find("!dbg !", inst_pos);
        if (dbg_pos == std::string::npos) {
            return -1;
        }
        const auto id_start = dbg_pos + 6;
        const auto id_end = ir.find_first_not_of("0123456789", id_start);
        if (id_end == std::string::npos || id_end == id_start) {
            return -1;
        }
        const auto id = ir.substr(id_start, id_end - id_start);
        const auto meta_pos = ir.find("!" + id + " = !DILocation(line: ");
        if (meta_pos == std::string::npos) {
            return -1;
        }
        const auto line_start = meta_pos + std::string("!" + id + " = !DILocation(line: ").size();
        const auto line_end = ir.find_first_not_of("0123456789", line_start);
        if (line_end == std::string::npos || line_end == line_start) {
            return -1;
        }
        return std::stoi(ir.substr(line_start, line_end - line_start));
    };

    INFO(ir);
    const int alloc_guard_line = extract_dbg_line_for_instruction("new_single_is_null");
    REQUIRE(alloc_guard_line == 7);
}

