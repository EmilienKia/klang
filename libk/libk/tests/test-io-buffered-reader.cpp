/*
 * K Language standard library — BufferedReader & LineReader tests
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
} // anonymous namespace

TEST_CASE("BufferedReader: peek and readLine with different line endings", "[libk][io][buffered_reader]") {
    auto jit = jit_k(R"SRC(
        module __test_br_lines__;

        test_buffered_reader_lines() : int {
            // Text containing \n, \r\n, \r, and no trailing newline
            s : k::String = "First line\nSecond line\r\nThird line\rFourth line without newline";
            sr : k::io::StringReader(s);
            br : k::io::BufferedReader(&sr, 16); // Small buffer to test refill

            p : k::Optional<char> = br.peek();
            if (!p.hasValue() || p.get() != 'F') return 1;

            l1 : k::Optional<k::String> = br.readLine();
            if (!l1.hasValue() || l1.get() != k::String("First line")) return 2;

            l2 : k::Optional<k::String> = br.readLine();
            if (!l2.hasValue() || l2.get() != k::String("Second line")) return 3;

            l3 : k::Optional<k::String> = br.readLine();
            if (!l3.hasValue() || l3.get() != k::String("Third line")) return 4;

            l4 : k::Optional<k::String> = br.readLine();
            if (!l4.hasValue() || l4.get() != k::String("Fourth line without newline")) return 5;

            l5 : k::Optional<k::String> = br.readLine();
            if (l5.hasValue()) return 6;

            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_buffered_reader_lines");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

TEST_CASE("BufferedWriter: buffering and newLine", "[libk][io][buffered_writer]") {
    auto jit = jit_k(R"SRC(
        module __test_bw__;

        test_buffered_writer() : int {
            sw : k::io::StringWriter;
            bw : k::io::BufferedWriter(&sw, 16);

            bw.write("Line 1");
            bw.newLine();
            bw.write("Line 2");
            bw.flush();

            s : k::String = sw.toString();
            if (s != k::String("Line 1\nLine 2")) return 1;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test_buffered_writer");
    REQUIRE(fn);
    CHECK(fn() == 0);
}
