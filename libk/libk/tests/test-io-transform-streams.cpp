/*
 * K Language standard library — I/O transform stream tests
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
 * Tests for the transform stream decorators in `k::io`.
 *
 * These tests compile the stdlib sources together with a small `module k`
 * extension so the templated base classes resolve in the same module.
 */

#include <catch2/catch_all.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "helpers.hpp"

#ifndef LIBK_KDI_DIR
#error "LIBK_KDI_DIR must be defined — check CMakeLists.txt"
#endif
#ifndef LIBK_LIB_DIR
#error "LIBK_LIB_DIR must be defined — check CMakeLists.txt"
#endif

namespace {

std::filesystem::path get_libk_source_root() {
    return std::filesystem::path(LIBK_KDI_DIR).parent_path().parent_path().parent_path() / "libk/libk/src";
}

std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        throw std::runtime_error("Could not open stdlib source file: " + path.string());
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

std::unique_ptr<k::model::gen::jit> jit_k_full(std::string_view extra_src) {
    auto root = get_libk_source_root();
    std::vector<std::pair<std::string, std::string>> sources;
    const std::vector<std::string> rel_paths = {
        "ffi.k",
        "annotations.k",
        "object.k",
        "rtti.k",
        "exception.k",
        "memory.k",
        "shared.k",
        "expected.k",
        "optional.k",
        "collections.k",
        "string.k",
        "io/stream.k",
        "io/array_stream.k",
        "io/filter_stream.k",
        "io/transform_stream.k",
        "io/buffered_stream.k",
        "io/data_stream.k",
        "io/print_stream.k",
        "io/file.k",
        "io/stdio.k",
        "math/math.k",
    };

    for (const auto& rel : rel_paths) {
        auto path = root / rel;
        sources.emplace_back(path.string(), read_text_file(path));
    }

    sources.emplace_back((root / "transform_stream_test_module.k").string(), std::string(extra_src));
    return gen_jit_multi_throws(std::move(sources), false, true, "k");
}

} // anonymous namespace


TEST_CASE("Transform streams one-to-one and buffering", "[libk][io][transform]") {
    auto jit = jit_k_full(R"SRC(
        module k;

        namespace io {

        class KeepEvenTimesTen : public OneToOneTransformInputStream<int, int> {
        public:
            KeepEvenTimesTen(input: InputStream<int>*) : OneToOneTransformInputStream(input) {}

            transform(in : const int&) : ::k::Optional<int> {
                if ((in % 2) != 0) {
                    return ::k::Optional<int>();
                }
                return ::k::Optional<int>(in * 10);
            }
        }

        class ExpandOddValues : public OneToManyTransformInputStream<long, long> {
        public:
            ExpandOddValues(input: InputStream<long>*) : OneToManyTransformInputStream(input) {}

            transform(in : const long&) : ::k::Vector<long> {
                out : ::k::Vector<long>;
                if (((in % (long) 2)) == (long) 0) {
                    return out;
                }
                out.pushBack(in);
                out.pushBack(in + (long) 1000);
                return out;
            }
        }

        class PairSum : public ManyToOneTransformInputStream<int, int> {
        public:
            PairSum(input: InputStream<int>*) : ManyToOneTransformInputStream(input) {}

            transform(in : const ::k::Vector<int>&) : ::k::Optional<int> {
                if (in.getSize() < 2) {
                    return ::k::Optional<int>();
                }
                return ::k::Optional<int>(in[0] + in[1]);
            }
        }

        class TripletExpand : public ManyToManyTransformInputStream<int, int> {
        public:
            TripletExpand(input: InputStream<int>*) : ManyToManyTransformInputStream(input) {}

            transform(in : const ::k::Vector<int>&) : ::k::Vector<int> {
                out : ::k::Vector<int>;
                if (in.getSize() < 3) {
                    return out;
                }
                out.pushBack(in[0] + in[1]);
                out.pushBack(in[2] - in[0]);
                return out;
            }
        }

        test_input_one_to_one() : int {
            src : int[]! = new int[6];
            src[0] = 1;
            src[1] = 2;
            src[2] = 3;
            src[3] = 4;
            src[4] = 5;
            src[5] = 6;
            bais : k::io::ArrayInputStream<int>(src, 6);
            stream : KeepEvenTimesTen(&bais);

            first : int = stream.read().getOr(0);
            if (first != 20) return 1;

            skipped : unsigned long = stream.skip(1uL);
            if (skipped != 1uL) return 2;

            next : int = stream.read().getOr(0);
            if (next != 60) return 3;

            if (stream.read().hasValue()) return 4;
            return 0;
        }

        test_input_one_to_many() : int {
            src : long[]! = new long[3];
            src[0] = (long) 1;
            src[1] = (long) 2;
            src[2] = (long) 3;
            bais : k::io::ArrayInputStream<long>(src, 3);
            stream : ExpandOddValues(&bais);

            first : long = stream.read().getOr((long) 0);
            if (first != (long) 1) return 1;

            skipped : unsigned long = stream.skip(1uL);
            if (skipped != 1uL) return 2;

            dst : long[]! = new long[2];
            n : int = (int) stream.read(dst, 0, 2).getResultOr((unsigned int) 0);
            if (n != 2) return 3;
            if (dst[0] != (long) 3) return 4;
            if (dst[1] != (long) 1003) return 5;
            return 0;
        }

        test_input_many_to_one() : int {
            src : int[]! = new int[5];
            src[0] = 10;
            src[1] = 20;
            src[2] = 30;
            src[3] = 40;
            src[4] = 50;
            bais : k::io::ArrayInputStream<int>(src, 5);
            stream : PairSum(&bais);

            first : int = stream.read().getOr(0);
            if (first != 30) return 1;

            skipped : unsigned long = stream.skip(1uL);
            if (skipped != 1uL) return 2;

            if (stream.read().hasValue()) return 3;
            return 0;
        }

        test_input_many_to_many() : int {
            src : int[]! = new int[6];
            src[0] = 1;
            src[1] = 2;
            src[2] = 3;
            src[3] = 4;
            src[4] = 5;
            src[5] = 6;
            bais : k::io::ArrayInputStream<int>(src, 6);
            stream : TripletExpand(&bais);

            first : int = stream.read().getOr(0);
            if (first != 3) return 1;

            dst : int[]! = new int[3];
            n : int = (int) stream.read(dst, 0, 3).getResultOr((unsigned int) 0);
            if (n != 3) return 2;
            if (dst[0] != 2) return 3;
            if (dst[1] != 9) return 4;
            if (dst[2] != 2) return 5;
            return 0;
        }

        class KeepPositiveTimesTen : public OneToOneTransformOutputStream<int, int> {
        public:
            KeepPositiveTimesTen(output: OutputStream<int>*) : OneToOneTransformOutputStream(output) {}

            transform(in : const int&) : ::k::Optional<int> {
                if (in < 0) {
                    return ::k::Optional<int>();
                }
                return ::k::Optional<int>(in * 10);
            }
        }

        class ExpandOddValuesOut : public OneToManyTransformOutputStream<int, int> {
        public:
            ExpandOddValuesOut(output: OutputStream<int>*) : OneToManyTransformOutputStream(output) {}

            transform(in : const int&) : ::k::Vector<int> {
                out : ::k::Vector<int>;
                if ((in % 2) == 0) {
                    return out;
                }
                out.pushBack(in);
                out.pushBack(in + 1000);
                return out;
            }
        }

        class PairSumOut : public ManyToOneTransformOutputStream<long, long> {
        public:
            PairSumOut(output: OutputStream<long>*) : ManyToOneTransformOutputStream(output) {}

            transform(in : const ::k::Vector<long>&) : ::k::Optional<long> {
                if (in.getSize() < 2) {
                    return ::k::Optional<long>();
                }
                return ::k::Optional<long>(in[0] + in[1]);
            }
        }

        class TripletExpandOut : public ManyToManyTransformOutputStream<long, long> {
        public:
            TripletExpandOut(output: OutputStream<long>*) : ManyToManyTransformOutputStream(output) {}

            transform(in : const ::k::Vector<long>&) : ::k::Vector<long> {
                out : ::k::Vector<long>;
                if (in.getSize() < 3) {
                    return out;
                }
                out.pushBack(in[0]);
                out.pushBack(in[1] * (long) 2);
                out.pushBack(in[2] * (long) 3);
                return out;
            }
        }

        test_output_one_to_one() : int {
            sink : k::io::ArrayOutputStream<int>;
            stream : KeepPositiveTimesTen(&sink);
            stream.write(1);
            stream.write(-2);
            stream.write(3);
            stream.write(4);
            arr : int[]* = sink.toArray();
            if (sink.size() != 3) return 1;
            if (arr[0] != 10) return 2;
            if (arr[1] != 30) return 3;
            if (arr[2] != 40) return 4;
            return 0;
        }

        test_output_one_to_many() : int {
            sink : k::io::ArrayOutputStream<int>;
            stream : ExpandOddValuesOut(&sink);
            stream.write(1);
            stream.write(2);
            stream.write(3);
            arr : int[]* = sink.toArray();
            if (sink.size() != 4) return 1;
            if (arr[0] != 1) return 2;
            if (arr[1] != 1001) return 3;
            if (arr[2] != 3) return 4;
            if (arr[3] != 1003) return 5;
            return 0;
        }

        test_output_many_to_one() : int {
            sink : k::io::ArrayOutputStream<long>;
            stream : PairSumOut(&sink);
            stream.write((long) 10);
            stream.write((long) 20);
            stream.write((long) 30);
            stream.write((long) 40);
            stream.write((long) 50);
            arr : long[]* = sink.toArray();
            if (sink.size() != 2) return 1;
            if (arr[0] != (long) 30) return 2;
            if (arr[1] != (long) 70) return 3;
            return 0;
        }

        test_output_many_to_many() : int {
            sink : k::io::ArrayOutputStream<long>;
            stream : TripletExpandOut(&sink);
            stream.write((long) 1);
            stream.write((long) 2);
            stream.write((long) 3);
            stream.write((long) 4);
            stream.write((long) 5);
            stream.write((long) 6);
            stream.flush();
            arr : long[]* = sink.toArray();
            if (sink.size() != 6) return 1;
            if (arr[0] != (long) 1) return 2;
            if (arr[1] != (long) 4) return 3;
            if (arr[2] != (long) 9) return 4;
            if (arr[3] != (long) 4) return 5;
            if (arr[4] != (long) 10) return 6;
            if (arr[5] != (long) 18) return 7;
            return 0;
        }

        } // namespace io
    )SRC");

    REQUIRE(jit);

    const auto check = [&](const char* name, int expected) {
        auto fn = jit->lookup_symbol<int(*)()>(name);
        REQUIRE(fn);
        CHECK(fn() == expected);
    };

    check("test_input_one_to_one", 0);
    check("test_input_one_to_many", 0);
    check("test_input_many_to_one", 0);
    check("test_input_many_to_many", 0);
    check("test_output_one_to_one", 0);
    check("test_output_one_to_many", 0);
    check("test_output_many_to_one", 0);
    check("test_output_many_to_many", 0);
}





