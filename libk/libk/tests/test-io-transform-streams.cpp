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
 * These tests compile a user module that extends the template transform stream
 * classes from the prebuilt libk KDI.
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


TEST_CASE("Transform streams one-to-one and buffering", "[libk][io][transform]") {
    auto jit = jit_k(R"SRC(
        module __transform_test__;

        class KeepEvenTimesTen : public k::io::OneToOneTransformInputStream<int, int> {
        public:
            KeepEvenTimesTen(input: k::io::InputStream<int>*) : OneToOneTransformInputStream(input) {}

            override transform(in : const int&) : ::k::Optional<int> {
                if ((in % 2) != 0) {
                    return ::k::Optional<int>();
                }
                return ::k::Optional<int>(in * 10);
            }
        }

        class ExpandOddValues : public k::io::OneToManyTransformInputStream<long, long> {
        public:
            ExpandOddValues(input: k::io::InputStream<long>*) : OneToManyTransformInputStream(input) {}

            override transform(in : const long&) : ::k::Vector<long> {
                out : ::k::Vector<long>;
                if (((in % (long) 2)) == (long) 0) {
                    return out;
                }
                out.append(in);
                out.append(in + (long) 1000);
                return out;
            }
        }

        class PairSum : public k::io::ManyToOneTransformInputStream<int, int> {
        public:
            PairSum(input: k::io::InputStream<int>*) : ManyToOneTransformInputStream(input) {}

            override transform(in : const ::k::Vector<int>&) : ::k::Optional<int> {
                if (in.size() < 2) {
                    return ::k::Optional<int>();
                }
                return ::k::Optional<int>(in[0] + in[1]);
            }
        }

        class TripletExpand : public k::io::ManyToManyTransformInputStream<int, int> {
        public:
            TripletExpand(input: k::io::InputStream<int>*) : ManyToManyTransformInputStream(input) {}

            override transform(in : const ::k::Vector<int>&) : ::k::Vector<int> {
                out : ::k::Vector<int>;
                if (in.size() < 3) {
                    return out;
                }
                out.append(in[0] + in[1]);
                out.append(in[2] - in[0]);
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
            if (first != 3) return 1001 + first;

            dst : int[]! = new int[3];
            n : int = (int) stream.read(dst, 0, 3).getResultOr((unsigned int) 0);
            // Output sequence for src [1,2,3,4,5,6] is [3,2,9,2]; `first` consumed
            // the leading 3, so this bulk read returns the remaining three: [2,9,2].
            if (n != 3) return 2;
            if (dst[0] != 2) return 3;
            if (dst[1] != 9) return 4;
            if (dst[2] != 2) return 5;
            return 0;
        }

        class KeepPositiveTimesTen : public k::io::OneToOneTransformOutputStream<int, int> {
        public:
            KeepPositiveTimesTen(output: k::io::OutputStream<int>*) : OneToOneTransformOutputStream(output) {}

            override transform(in : const int&) : ::k::Optional<int> {
                if (in < 0) {
                    return ::k::Optional<int>();
                }
                return ::k::Optional<int>(in * 10);
            }
        }

        class ExpandOddValuesOut : public k::io::OneToManyTransformOutputStream<int, int> {
        public:
            ExpandOddValuesOut(output: k::io::OutputStream<int>*) : OneToManyTransformOutputStream(output) {}

            override transform(in : const int&) : ::k::Vector<int> {
                out : ::k::Vector<int>;
                if ((in % 2) == 0) {
                    return out;
                }
                out.append(in);
                out.append(in + 1000);
                return out;
            }
        }

        class PairSumOut : public k::io::ManyToOneTransformOutputStream<long, long> {
        public:
            PairSumOut(output: k::io::OutputStream<long>*) : ManyToOneTransformOutputStream(output) {}

            override transform(in : const ::k::Vector<long>&) : ::k::Optional<long> {
                if (in.size() < 2) {
                    return ::k::Optional<long>();
                }
                return ::k::Optional<long>(in[0] + in[1]);
            }
        }

        class TripletExpandOut : public k::io::ManyToManyTransformOutputStream<long, long> {
        public:
            TripletExpandOut(output: k::io::OutputStream<long>*) : ManyToManyTransformOutputStream(output) {}

            override transform(in : const ::k::Vector<long>&) : ::k::Vector<long> {
                out : ::k::Vector<long>;
                if (in.size() < 3) {
                    return out;
                }
                out.append(in[0]);
                out.append(in[1] * (long) 2);
                out.append(in[2] * (long) 3);
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
            if (sink.size() != 3) return 2001 + sink.size();
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
            if (sink.size() != 4) return 3001 + sink.size();
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





