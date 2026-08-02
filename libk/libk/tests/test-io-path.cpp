/*
 * K Language standard library — Path tests (Phase 4)
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

/**
 * Tests for k::io::Path.
 *
 * Coverage:
 *  - path text round-trip and length
 *  - fileName() / parent() decomposition
 *  - resolve() with relative and absolute components
 *  - existence, kind and size inspection
 *  - directory creation and file removal
 */

#include <catch2/catch_all.hpp>

#include "../../klang/tests/helpers.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>

#ifndef LIBK_KDI_DIR
#error "LIBK_KDI_DIR not defined — set via CMake target_compile_definitions"
#endif
#ifndef LIBK_LIB_DIR
#error "LIBK_LIB_DIR not defined — set via CMake target_compile_definitions"
#endif

namespace {

std::unique_ptr<k::model::gen::jit> jit_k(std::string_view src) {
    return gen_jit_with_stdlib(src, LIBK_KDI_DIR, LIBK_LIB_DIR);
}

void write_file(const std::string& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(content.data(), static_cast<std::streamsize>(content.size()));
}

} // anonymous namespace

TEST_CASE("Path: stores its text and length", "[libk][io][path]") {
    auto jit = jit_k(R"SRC(
        module __path_text__;
        test() : int {
            p : k::io::Path("/tmp/klang_path_text.bin");
            res : int = 0;
            if (p.length() == 24) { res = res + 1; }
            s : const char[]? = p.toString();
            if (s[0] == (char)47) { res = res + 2; }
            if (s[24] == (char)0) { res = res + 4; }
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 7);
}

TEST_CASE("Path: fileName and parent decompose the path", "[libk][io][path]") {
    auto jit = jit_k(R"SRC(
        module __path_decompose__;
        test() : int {
            p : k::io::Path("/tmp/sub/file.txt");
            res : int = 0;

            name : char[]! = p.fileName();
            if (name.size == 9) { res = res + 1; }
            if (name[0] == (char)102) { res = res + 2; }

            parent : k::io::Path! = p.parent();
            if (parent != null) { res = res + 4; }
            if (parent->length() == 8) { res = res + 8; }

            rel : k::io::Path("relative.txt");
            noParent : k::io::Path! = rel.parent();
            if (noParent == null) { res = res + 16; }

            delete parent;
            delete name;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 31);
}

TEST_CASE("Path: resolve appends and absolute paths replace", "[libk][io][path]") {
    auto jit = jit_k(R"SRC(
        module __path_resolve__;
        test() : int {
            base : k::io::Path("/tmp/dir");
            res : int = 0;

            child : k::io::Path! = base.resolve("f.bin");
            if (child->length() == 14) { res = res + 1; }
            cs : const char[]? = child->toString();
            if (cs[8] == (char)47) { res = res + 2; }

            slashed : k::io::Path("/tmp/dir/");
            child2 : k::io::Path! = slashed.resolve("f.bin");
            if (child2->length() == 14) { res = res + 4; }

            abs : k::io::Path! = base.resolve("/etc/hosts");
            if (abs->length() == 10) { res = res + 8; }

            delete abs;
            delete child2;
            delete child;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 15);
}

TEST_CASE("Path: inspects existence, kind and size", "[libk][io][path]") {
    const std::string file = "/tmp/klang_path_inspect.bin";
    write_file(file, "hello");

    auto jit = jit_k(R"SRC(
        module __path_inspect__;
        test() : int {
            f : k::io::Path("/tmp/klang_path_inspect.bin");
            d : k::io::Path("/tmp");
            missing : k::io::Path("/tmp/klang_path_absent_9182736");
            res : int = 0;
            if (f.exists())         { res = res + 1; }
            if (f.isFile())         { res = res + 2; }
            if (f.isDirectory() == false) { res = res + 4; }
            if (f.size() == 5L)     { res = res + 8; }
            if (d.isDirectory())    { res = res + 16; }
            if (missing.exists() == false) { res = res + 32; }
            if (missing.size() == -1L)     { res = res + 64; }
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 127);

    std::filesystem::remove(file);
}

TEST_CASE("Path: creates directories and removes files", "[libk][io][path]") {
    std::filesystem::remove_all("/tmp/klang_path_mkdir");
    const std::string file = "/tmp/klang_path_remove.bin";
    write_file(file, "x");

    auto jit = jit_k(R"SRC(
        module __path_mutate__;
        test() : int {
            d : k::io::Path("/tmp/klang_path_mkdir");
            f : k::io::Path("/tmp/klang_path_remove.bin");
            res : int = 0;
            if (d.createDirectory()) { res = res + 1; }
            if (d.isDirectory())     { res = res + 2; }
            if (f.remove())          { res = res + 4; }
            if (f.exists() == false) { res = res + 8; }
            if (f.remove() == false) { res = res + 16; }
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 31);

    std::filesystem::remove_all("/tmp/klang_path_mkdir");
}
