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
            p : k::io::file::Path("/tmp/klang_path_text.bin");
            res : int = 0;
            if (p.length() == 24) { ++res; }
            s : const char[]? = p.toString();
            if (s[0] == (char)47) { res += 2; }
            if (s[24] == (char)0) { res += 4; }
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
            p : k::io::file::Path("/tmp/sub/file.txt");
            res : int = 0;

            name : String = p.fileName();
            if (name.size() == 8u) { ++res; }
            if (name[0] == 'f') { res += 2; }

            parent : k::io::file::Path! = p.parent();
            if (parent != null) { res += 4; }
            if (parent->length() == 8) { res += 8; }

            rel : k::io::file::Path("relative.txt");
            noParent : k::io::file::Path! = rel.parent();
            if (noParent == null) { res += 16; }

            delete parent;
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
            base : k::io::file::Path("/tmp/dir");
            res : int = 0;

            child : k::io::file::Path! = base.resolve("f.bin");
            if (child->length() == 14) { ++res; }
            cs : const char[]? = child->toString();
            if (cs[8] == (char)47) { res += 2; }

            slashed : k::io::file::Path("/tmp/dir/");
            child2 : k::io::file::Path! = slashed.resolve("f.bin");
            if (child2->length() == 14) { res += 4; }

            abs : k::io::file::Path! = base.resolve("/etc/hosts");
            if (abs->length() == 10) { res += 8; }

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
            f : k::io::file::Path("/tmp/klang_path_inspect.bin");
            d : k::io::file::Path("/tmp");
            missing : k::io::file::Path("/tmp/klang_path_absent_9182736");
            res : int = 0;
            if (f.exists())         { ++res; }
            if (f.isFile())         { res += 2; }
            if (f.isDirectory() == false) { res += 4; }
            if (f.size() == 5L)     { res += 8; }
            if (d.isDirectory())    { res += 16; }
            if (missing.exists() == false) { res += 32; }
            if (missing.size() == -1L)     { res += 64; }
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
            d : k::io::file::Path("/tmp/klang_path_mkdir");
            f : k::io::file::Path("/tmp/klang_path_remove.bin");
            res : int = 0;
            if (d.createDirectory()) { ++res; }
            if (d.isDirectory())     { res += 2; }
            if (f.remove())          { res += 4; }
            if (f.exists() == false) { res += 8; }
            if (f.remove() == false) { res += 16; }
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 31);

    std::filesystem::remove_all("/tmp/klang_path_mkdir");
}

TEST_CASE("Path: segments, extensions and fluent mutations", "[libk][io][path]") {
    auto jit = jit_k(R"SRC(
        module __path_segments_ext__;
        test() : int {
            p : k::io::file::Path("/usr/local/bin/compiler.klang");
            res : int = 0;

            if (p.isAbsolute()) { ++res; }
            if (p.segmentCount() == 4u) { res += 2; }
            seg0 : String = p.segment(0u);
            if (seg0 == String("usr")) { res += 4; }

            ext : String = p.extension();
            if (ext == String("klang")) { res += 8; }
            if (p.hasExtension()) { res += 16; }

            st : String = p.stem();
            if (st == String("compiler")) { res += 32; }

            p2 : k::io::file::Path! = p.withExtension("k");
            ext2 : String = p2->extension();
            if (ext2 == String("k")) { res += 64; }

            p3 : k::io::file::Path! = p.withFileName("other.txt");
            if (p3->fileName() == String("other.txt")) { res += 128; }

            delete p2;
            delete p3;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 255);
}

TEST_CASE("Path: prefix, last, normalize and relative", "[libk][io][path]") {
    auto jit = jit_k(R"SRC(
        module __path_transforms__;
        test() : int {
            p : k::io::file::Path("b/c");
            pref : k::io::file::Path("a");
            res : int = 0;

            combined : k::io::file::Path! = p.prefix(pref);
            if (combined->segmentCount() == 3u && combined->segment(0u) == String("a")) { ++res; }

            lastElem : k::io::file::Path! = combined->last();
            if (lastElem->fileName() == String("c") && lastElem->isRelative()) { res += 2; }

            unnorm : k::io::file::Path("/a/b/../c/./d");
            norm : k::io::file::Path! = unnorm.normalize();
            if (norm->toPathString() == String("/a/c/d")) { res += 4; }

            base : k::io::file::Path("/a/b");
            target : k::io::file::Path("/a/b/c/d");
            rel : k::io::file::Path! = target.relative(base);
            if (rel->toPathString() == String("c/d")) { res += 8; }

            delete combined;
            delete lastElem;
            delete norm;
            delete rel;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 15);
}

TEST_CASE("Path: URI mapping", "[libk][io][path]") {
    auto jit = jit_k(R"SRC(
        module __path_uri__;
        test() : int {
            p : k::io::file::Path("/tmp/my file.txt");
            res : int = 0;

            u : k::io::Uri! = p.toUri();
            if (u->scheme() == String("file")) { ++res; }

            p2 : k::io::file::Path! = k::io::file::Path::fromUri(*u);
            if (p2 != null && p2->toPathString() == String("/tmp/my file.txt")) { res += 2; }

            u2 : k::io::Uri("file:///etc/hosts");
            p3 : k::io::file::Path! = u2.toPath();
            if (p3 != null && p3->toPathString() == String("/etc/hosts")) { res += 4; }

            delete u;
            delete p2;
            delete p3;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 7);
}

TEST_CASE("FileSystem: local operations and AnyFileSystemEntry", "[libk][io][path]") {
    std::filesystem::remove_all("/tmp/klang_fs_test");
    std::filesystem::create_directories("/tmp/klang_fs_test/sub");
    write_file("/tmp/klang_fs_test/sub/hello.txt", "world");

    auto jit = jit_k(R"SRC(
        module __fs_operations__;
        test() : int {
            fs : k::io::file::FileSystem& = k::io::file::FileSystem::local();
            res : int = 0;

            pRoot : k::io::file::Path("/tmp/klang_fs_test");
            pSub : k::io::file::Path("/tmp/klang_fs_test/sub");
            pFile : k::io::file::Path("/tmp/klang_fs_test/sub/hello.txt");

            if (fs.exists(pRoot)) { ++res; }
            if (fs.entryType(pSub) == k::io::file::FileSystemEntryType::Directory) { res += 2; }
            if (fs.entryType(pFile) == k::io::file::FileSystemEntryType::File) { res += 4; }

            f : k::io::file::File(pFile, fs);
            if (f.exists()) { res += 8; }
            if (f.size() == 5L) { res += 16; }

            d : k::io::file::Directory(pSub, fs);
            if (d.exists()) { res += 32; }

            pCopy : k::io::file::Path("/tmp/klang_fs_test/sub/hello2.txt");
            if (fs.copy(pFile, pCopy, true)) { res += 64; }
            if (fs.exists(pCopy)) { res += 128; }

            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 255);

    std::filesystem::remove_all("/tmp/klang_fs_test");
}

TEST_CASE("DirectoryFileSystem: sandboxed sub-tree operations", "[libk][io][path]") {
    std::filesystem::remove_all("/tmp/klang_dfs_sandbox");
    std::filesystem::create_directories("/tmp/klang_dfs_sandbox/data");
    write_file("/tmp/klang_dfs_sandbox/data/item.txt", "item");

    auto jit = jit_k(R"SRC(
        module __dfs_sandbox__;
        test() : int {
            rootP : k::io::file::Path("/tmp/klang_dfs_sandbox");
            dfs : k::io::file::DirectoryFileSystem! = k::io::file::DirectoryFileSystem::of(rootP);
            res : int = 0;

            vFile : k::io::file::Path("/data/item.txt");
            if (dfs->exists(vFile)) { ++res; }
            if (dfs->size(vFile) == 4L) { res += 2; }

            vEscape : k::io::file::Path("/../../etc/passwd");
            if (dfs->exists(vEscape) == false) { res += 4; }

            vNewDir : k::io::file::Path("/data/newdir");
            if (dfs->createDirectory(vNewDir)) { res += 8; }
            if (dfs->exists(vNewDir)) { res += 16; }

            delete dfs;
            return res;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    REQUIRE(fn() == 31);

    std::filesystem::remove_all("/tmp/klang_dfs_sandbox");
}
