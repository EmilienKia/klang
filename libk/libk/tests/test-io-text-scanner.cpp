/*
 * K Language standard library — TextScanner tests
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

TEST_CASE("TextScanner: read tokens, booleans, and numbers", "[libk][io][scanner]") {
    auto jit = jit_k(R"SRC(
        module __test_scanner__;

        test_scanner_parsing() : int {
            s : k::String = "true false 42 -12345 255 12.5 text_token";
            sr : k::io::StringReader(s);
            scanner : k::io::TextScanner(&sr);

            if (!scanner.hasNext()) return 1;
            b1 : bool = scanner.readBool();
            if (!b1) return 2;

            b2 : bool = scanner.readBool();
            if (b2) return 3;

            n1 : int = scanner.readInt(10u);
            if (n1 != 42) return 4;

            n2 : long = scanner.readLong(10u);
            if (n2 != -12345L) return 5;

            // Hex parse for 255 -> 0xFF -> "255" in decimal or hex token
            n3 : unsigned int = scanner.readUnsignedInt(10u);
            if (n3 != 255u) return 6;

            d : double = scanner.readDouble();
            if (d < 12.49 || d > 12.51) return 7;

            tokOpt : k::Optional<k::String> = scanner.nextToken();
            if (!tokOpt.hasValue() || tokOpt.get() != k::String("text_token")) return 8;

            if (scanner.hasNext()) return 9;
            return 0;
        }

        test_scanner_radix() : int {
            s : k::String = "1010 FF 77";
            sr : k::io::StringReader(s);
            scanner : k::io::TextScanner(&sr);

            bin : int = scanner.readInt(2u);
            if (bin != 10) return 1;

            hex : int = scanner.readInt(16u);
            if (hex != 255) return 2;

            oct : int = scanner.readInt(8u);
            if (oct != 63) return 3;

            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn1 = jit->lookup_symbol<int(*)()>("test_scanner_parsing");
    REQUIRE(fn1);
    CHECK(fn1() == 0);

    auto fn2 = jit->lookup_symbol<int(*)()>("test_scanner_radix");
    REQUIRE(fn2);
    CHECK(fn2() == 0);
}
