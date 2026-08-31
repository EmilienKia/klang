/*
 * K Language standard library — TextPrinter tests
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

TEST_CASE("TextPrinter: primitive printing and formatting", "[libk][io][printer]") {
    auto jit = jit_k(R"SRC(
        module __test_printer__;

        test_printer_primitives() : int {
            sw : k::io::StringWriter;
            tp : k::io::TextPrinter(&sw);

            tp.print("Count: ").print(42).print(", Flag: ").print(true).println();

            s : k::String = sw.toString();
            if (s != k::String("Count: 42, Flag: true\n")) return 1;
            return 0;
        }

        test_printer_formats() : int {
            sw : k::io::StringWriter;
            tp : k::io::TextPrinter(&sw);

            hexFmt : k::io::IntegerFormat = k::io::IntegerFormat::hex(false, true);
            tp.print(255, hexFmt);
            tp.print(" ");

            binFmt : k::io::IntegerFormat = k::io::IntegerFormat::binary(true);
            tp.print(5, binFmt);

            s : k::String = sw.toString();
            if (s != k::String("0xff 0b101")) return 1;
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn1 = jit->lookup_symbol<int(*)()>("test_printer_primitives");
    REQUIRE(fn1);
    CHECK(fn1() == 0);

    auto fn2 = jit->lookup_symbol<int(*)()>("test_printer_formats");
    REQUIRE(fn2);
    CHECK(fn2() == 0);
}
