/*
 * K Language standard library — I/O File stream tests
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

/**
 * Tests for k::io::File, k::io::FileDescriptor,
 * k::io::FileInputStream and k::io::FileOutputStream.
 *
 * Exercises file metadata queries, file creation/deletion, single-byte
 * and bulk read/write, append mode, and round-trip I/O.
 *
 * All tests use temporary files under /tmp/klang_test_* and clean up
 * after themselves.
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <filesystem>

#ifndef LIBK_KDI_DIR
#error "LIBK_KDI_DIR must be defined — check CMakeLists.txt"
#endif
#ifndef LIBK_LIB_DIR
#error "LIBK_LIB_DIR must be defined — check CMakeLists.txt"
#endif

namespace {

std::unique_ptr<k::model::gen::jit> jit_k(std::string_view src) {
    return gen_jit_with_stdlib(src, LIBK_KDI_DIR, LIBK_LIB_DIR);
}

/** Helper: ensure a temp file does not exist before/after a test. */
struct tmp_file_guard {
    std::string path;
    explicit tmp_file_guard(const std::string& p) : path(p) {
        std::filesystem::remove(path);
    }
    ~tmp_file_guard() {
        std::filesystem::remove(path);
    }
};

} // anonymous namespace


// ═════════════════════════════════════════════════════════════════════════════
// FileDescriptor — default constructor
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("FileDescriptor default ctor — invalid", "[libk][io][file]") {
    auto jit = jit_k(R"SRC(
        module __fd_default__;

        test() : int {
            fd : k::io::FileDescriptor;
            if (fd.valid()) return 1;
            if (fd.getFd() != -1) return 2;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 0);
}


// ═════════════════════════════════════════════════════════════════════════════
// FileDescriptor — constructor with fd
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("FileDescriptor ctor with fd — valid", "[libk][io][file]") {
    auto jit = jit_k(R"SRC(
        module __fd_valid__;

        test() : int {
            fd : k::io::FileDescriptor(42);
            if (!fd.valid()) return 1;
            if (fd.getFd() != 42) return 2;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 0);
}


// ═════════════════════════════════════════════════════════════════════════════
// File — getPath
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("File getPath returns constructor path", "[libk][io][file]") {
    auto jit = jit_k(R"SRC(
        module __file_getpath__;

        test() : int {
            f : k::io::File("/tmp/hello.txt");
            p : const char[]? = f.getPath();
            if (p == null) return 10;
            if (p[0] != '/') return 1;
            if (p[1] != 't') return 2;
            if (p[4] != '/') return 3;
            if (p[5] != 'h') return 4;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 0);
}


// ═════════════════════════════════════════════════════════════════════════════
// File — exists false for missing file
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("File exists returns false for nonexistent path", "[libk][io][file]") {
    auto jit = jit_k(R"SRC(
        module __file_exists_false__;

        test() : int {
            f : k::io::File("/tmp/klang_test_nonexistent_424242");
            if (f.exists()) return 1;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 0);
}


// ═════════════════════════════════════════════════════════════════════════════
// File — isDirectory
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("File isDirectory returns true for /tmp", "[libk][io][file]") {
    auto jit = jit_k(R"SRC(
        module __file_isdir__;

        test() : int {
            f : k::io::File("/tmp");
            if (!f.isDirectory()) return 1;
            if (f.isFile()) return 2;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 0);
}


// ═════════════════════════════════════════════════════════════════════════════
// File — createNewFile + exists + isFile + remove
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("File createNewFile, exists, isFile, remove", "[libk][io][file]") {
    tmp_file_guard guard("/tmp/klang_test_create_file");

    auto jit = jit_k(R"SRC(
        module __file_create_remove__;

        test() : int {
            f : k::io::File("/tmp/klang_test_create_file");

            // Should not exist yet
            if (f.exists()) return 1;

            // Create
            if (!f.createNewFile()) return 2;

            // Now exists and is a file
            if (!f.exists()) return 3;
            if (!f.isFile()) return 4;
            if (f.isDirectory()) return 5;

            // Remove
            if (!f.remove()) return 6;

            // Gone
            if (f.exists()) return 7;

            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 0);
}


// ═════════════════════════════════════════════════════════════════════════════
// FileOutputStream — write single bytes + close
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("FileOutputStream write single bytes", "[libk][io][file]") {
    tmp_file_guard guard("/tmp/klang_test_fos_single");

    auto jit = jit_k(R"SRC(
        module __fos_single__;

        test() : int {
            fos : k::io::FileOutputStream("/tmp/klang_test_fos_single");
            if (!fos.isOpen()) return 1;
            fos.write(65);
            fos.write(66);
            fos.write(67);
            fos.close();

            // Check file length
            f : k::io::File("/tmp/klang_test_fos_single");
            if (f.length() != 3) return 2;

            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 0);
}


// ═════════════════════════════════════════════════════════════════════════════
// FileInputStream — read single bytes
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("FileInputStream read single bytes written by FileOutputStream", "[libk][io][file]") {
    tmp_file_guard guard("/tmp/klang_test_fis_single");

    auto jit = jit_k(R"SRC(
        module __fis_single__;

        test() : int {
            // Write
            fos : k::io::FileOutputStream("/tmp/klang_test_fis_single");
            fos.write(10);
            fos.write(20);
            fos.write(30);
            fos.close();

            // Read back
            fis : k::io::FileInputStream("/tmp/klang_test_fis_single");
            if (!fis.isOpen()) return 1;

            v0 : int = (int)(unsigned byte) fis.read().getOr((byte) 0);
            v1 : int = (int)(unsigned byte) fis.read().getOr((byte) 0);
            v2 : int = (int)(unsigned byte) fis.read().getOr((byte) 0);
            fis.close();

            if (v0 != 10) return 2;
            if (v1 != 20) return 3;
            if (v2 != 30) return 4;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 0);
}


// ═════════════════════════════════════════════════════════════════════════════
// FileInputStream — read returns -1 at EOF
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("FileInputStream read returns -1 at EOF", "[libk][io][file]") {
    tmp_file_guard guard("/tmp/klang_test_fis_eof");

    auto jit = jit_k(R"SRC(
        module __fis_eof__;

        test() : int {
            // Write 1 byte
            fos : k::io::FileOutputStream("/tmp/klang_test_fis_eof");
            fos.write(42);
            fos.close();

            // Read 1 byte, then expect -1
            fis : k::io::FileInputStream("/tmp/klang_test_fis_eof");
            v0 : int = (int)(unsigned byte) fis.read().getOr((byte) 0);
            atEof : bool = fis.read().hasValue();
            fis.close();

            if (v0 != 42) return 1;
            if (atEof) return 2;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 0);
}


// ═════════════════════════════════════════════════════════════════════════════
// FileOutputStream — append mode
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("FileOutputStream append mode", "[libk][io][file]") {
    tmp_file_guard guard("/tmp/klang_test_fos_append");

    auto jit = jit_k(R"SRC(
        module __fos_append__;

        test() : int {
            // Write 2 bytes
            fos1 : k::io::FileOutputStream("/tmp/klang_test_fos_append");
            fos1.write(1);
            fos1.write(2);
            fos1.close();

            // Append 1 byte
            fos2 : k::io::FileOutputStream("/tmp/klang_test_fos_append", true);
            fos2.write(3);
            fos2.close();

            // Total length should be 3
            f : k::io::File("/tmp/klang_test_fos_append");
            if (f.length() != 3) return 1;

            // Read back all 3 bytes
            fis : k::io::FileInputStream("/tmp/klang_test_fos_append");
            v0 : int = (int)(unsigned byte) fis.read().getOr((byte) 0);
            v1 : int = (int)(unsigned byte) fis.read().getOr((byte) 0);
            v2 : int = (int)(unsigned byte) fis.read().getOr((byte) 0);
            fis.close();

            if (v0 != 1) return 2;
            if (v1 != 2) return 3;
            if (v2 != 3) return 4;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 0);
}


// ═════════════════════════════════════════════════════════════════════════════
// FileInputStream — read into buffer (bulk)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("FileInputStream read into buffer", "[libk][io][file]") {
    tmp_file_guard guard("/tmp/klang_test_fis_bulk");

    auto jit = jit_k(R"SRC(
        module __fis_bulk__;

        test() : int {
            // Write 5 bytes
            fos : k::io::FileOutputStream("/tmp/klang_test_fis_bulk");
            fos.write(10);
            fos.write(20);
            fos.write(30);
            fos.write(40);
            fos.write(50);
            fos.close();

            // Read into buffer
            fis : k::io::FileInputStream("/tmp/klang_test_fis_bulk");
            buf : byte[5];
            n : int = fis.read(buf);
            fis.close();

            if (n != 5) return 1;
            if (buf[0] != (byte) 10) return 2;
            if (buf[1] != (byte) 20) return 3;
            if (buf[2] != (byte) 30) return 4;
            if (buf[3] != (byte) 40) return 5;
            if (buf[4] != (byte) 50) return 6;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 0);
}


// ═════════════════════════════════════════════════════════════════════════════
// FileOutputStream — write buffer (bulk)
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("FileOutputStream write buffer then read back", "[libk][io][file]") {
    tmp_file_guard guard("/tmp/klang_test_fos_bulk");

    auto jit = jit_k(R"SRC(
        module __fos_bulk__;

        test() : int {
            // Write a buffer
            wbuf : byte[4];
            wbuf[0] = (byte) 0xAA;
            wbuf[1] = (byte) 0xBB;
            wbuf[2] = (byte) 0xCC;
            wbuf[3] = (byte) 0xDD;

            fos : k::io::FileOutputStream("/tmp/klang_test_fos_bulk");
            fos.write(wbuf);
            fos.close();

            // Verify length
            f : k::io::File("/tmp/klang_test_fos_bulk");
            if (f.length() != 4) return 1;

            // Read back
            fis : k::io::FileInputStream("/tmp/klang_test_fos_bulk");
            rbuf : byte[4];
            n : int = fis.read(rbuf);
            fis.close();

            if (n != 4) return 2;
            if (rbuf[0] != (byte) 0xAA) return 3;
            if (rbuf[1] != (byte) 0xBB) return 4;
            if (rbuf[2] != (byte) 0xCC) return 5;
            if (rbuf[3] != (byte) 0xDD) return 6;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 0);
}


// ═════════════════════════════════════════════════════════════════════════════
// File — length after write
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("File length returns correct size after write", "[libk][io][file]") {
    tmp_file_guard guard("/tmp/klang_test_file_length");

    auto jit = jit_k(R"SRC(
        module __file_length__;

        test() : int {
            fos : k::io::FileOutputStream("/tmp/klang_test_file_length");
            i : int = 0;
            while (i < 100) {
                fos.write(i);
                i = i + 1;
            }
            fos.close();

            f : k::io::File("/tmp/klang_test_file_length");
            if (f.length() != 100) return 1;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 0);
}


// ═════════════════════════════════════════════════════════════════════════════
// Round-trip: FOS → FIS with multiple byte values
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("FileOutputStream to FileInputStream round-trip", "[libk][io][file]") {
    tmp_file_guard guard("/tmp/klang_test_roundtrip");

    auto jit = jit_k(R"SRC(
        module __roundtrip__;

        test() : int {
            // Write 256 byte values (0..255)
            fos : k::io::FileOutputStream("/tmp/klang_test_roundtrip");
            i : int = 0;
            while (i < 256) {
                fos.write(i);
                i = i + 1;
            }
            fos.close();

            // Read them back and verify
            fis : k::io::FileInputStream("/tmp/klang_test_roundtrip");
            j : int = 0;
            while (j < 256) {
                v : int = (int)(unsigned byte) fis.read().getOr((byte) 0);
                if (v != j) return j + 1;
                j = j + 1;
            }
            // Next read should be EOF
            if (fis.read().hasValue()) return 257;
            fis.close();
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 0);
}


// ═════════════════════════════════════════════════════════════════════════════
// FileInputStream — getFD returns valid descriptor
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("FileInputStream getFD returns valid descriptor", "[libk][io][file]") {
    tmp_file_guard guard("/tmp/klang_test_fis_fd");

    auto jit = jit_k(R"SRC(
        module __fis_fd__;

        test() : int {
            // Create a file first
            fos : k::io::FileOutputStream("/tmp/klang_test_fis_fd");
            fos.write(1);
            fos.close();

            fis : k::io::FileInputStream("/tmp/klang_test_fis_fd");
            fd : k::io::FileDescriptor = fis.getFD();
            fis.close();

            if (!fd.valid()) return 1;
            if (fd.getFd() < 0) return 2;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 0);
}


// ═════════════════════════════════════════════════════════════════════════════
// File — getName
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("File getName extracts filename", "[libk][io][file]") {
    auto jit = jit_k(R"SRC(
        module __file_getname__;

        test() : int {
            f : k::io::File("/tmp/hello.txt");
            name : char[]* = f.getName();
            // "hello.txt" is 9 chars
            if (name[0] != 'h') return 2;
            if (name[4] != 'o') return 3;
            if (name[5] != '.') return 4;
            if (name[8] != 't') return 5;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 0);
}


// ═════════════════════════════════════════════════════════════════════════════
// File — getName with no separator
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("File getName with no separator returns full name", "[libk][io][file]") {
    auto jit = jit_k(R"SRC(
        module __file_getname_nosep__;

        test() : int {
            f : k::io::File("myfile.k");
            name : char[]* = f.getName();
            if (name[0] != 'm') return 2;
            if (name[7] != 'k') return 3;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 0);
}





