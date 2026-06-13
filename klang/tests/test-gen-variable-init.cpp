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

/*
 * Regression tests for variable-initialiser type checking.
 *
 * Background: initialising a primitive/enum variable from an incompatible
 * value (e.g. a struct value, or a struct returned by a function) used to be
 * silently accepted by the variable-initialisation path — unlike the plain
 * assignment path which already rejected it. The accepted code reinterpreted
 * the struct memory as the primitive, producing garbage values or run-time
 * crashes (including a SIGSEGV in the libk I/O tests that initialised an `int`
 * from `read()`, whose return type had changed to `Optional<byte>`).
 *
 * These tests pin the behaviour: an incompatible primitive initialiser must be
 * a compile-time error (diag ERR_VAR_INIT_TYPE_MISMATCH / 0x00E3).
 */

#include <catch2/catch_all.hpp>

#include "helpers.hpp"

TEST_CASE("Variable init: struct temporary to int is a type error",
          "[gen][variable-init][error]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __var_init_struct_temp__;

        struct Pair {
            a : int;
            b : int;
        }

        makePair() : Pair { p : Pair; return p; }

        test() : int {
            v : int = makePair();
            return v;
        }
        )SRC"), k::log::compiler_error);
}

TEST_CASE("Variable init: named struct value to int is a type error",
          "[gen][variable-init][error]") {
    REQUIRE_THROWS_AS(gen_jit_throws(R"SRC(
        module __var_init_struct_named__;

        struct Pair {
            a : int;
            b : int;
        }

        test() : int {
            p : Pair;
            v : int = p;
            return v;
        }
        )SRC"), k::log::compiler_error);
}

TEST_CASE("Variable init: compatible primitive init still compiles",
          "[gen][variable-init]") {
    auto jit = gen_jit(R"SRC(
        module __var_init_ok__;

        test() : int {
            a : int = 40;
            b : long = a;       // widening, allowed
            c : int = (int) b;  // explicit narrowing, allowed
            return a + c - 78;
        }
        )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 2);
}

