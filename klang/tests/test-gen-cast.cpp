/*
 * K Language compiler
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
 * Tests for explicit primitive-type casts: (type) expr
 *
 * Coverage matrix for primitive cast code generation:
 *
 * ── bool → other ─────────────────────────────────────────────────────────────
 *   [B2I]  (int)  boolVal         bool → int   (ZExt / SExt)
 *   [B2L]  (long) boolVal         bool → long
 *   [B2F]  (float)  boolVal       bool → float  (Select 0.0/1.0)
 *   [B2D]  (double) boolVal       bool → double (Select 0.0/1.0)
 *
 * ── int → other ──────────────────────────────────────────────────────────────
 *   [I2B]  (bool) intVal          int → bool   (ICmpNE 0)
 *   [I2S]  (short) intVal         int → short  (trunc / narrowing)
 *   [I2L]  (long) intVal          int → long   (SExt / widening)
 *   [I2F]  (float) intVal         int → float  (SIToFP)
 *   [I2D]  (double) intVal        int → double (SIToFP)
 *
 * ── float → other ────────────────────────────────────────────────────────────
 *   [F2B]  (bool) floatVal        float → bool  (FCmpUNE 0.0)
 *   [F2I]  (int)  floatVal        float → int   (FPToSI)
 *   [F2D]  (double) floatVal      float → double (FPExt)
 *
 * ── double → other ───────────────────────────────────────────────────────────
 *   [D2B]  (bool)  doubleVal      double → bool  (FCmpUNE 0.0)
 *   [D2I]  (int)   doubleVal      double → int   (FPToSI)
 *   [D2F]  (float) doubleVal      double → float (FPTrunc)
 *
 * ── unsigned casts ───────────────────────────────────────────────────────────
 *   [U2I]  (int)  byteVal         byte (unsigned) → int (signed, widening)
 *   [I2U]  (byte) intVal          int (signed) → byte (unsigned, narrowing)
 *
 * ── identity casts ───────────────────────────────────────────────────────────
 *   [II]   (int)  intVal          int → int  (no-op)
 *
 * ── chained casts ────────────────────────────────────────────────────────────
 *   [CH]   (int)(double) floatVal float → double → int
 *
 * ── enum casts ───────────────────────────────────────────────────────────────
 *   [E2I]  (int) enumVal          enum → int
 *   [I2E]  (EnumType) intVal      int → enum  (not always supported — see notes)
 *   [E2E]  (EnumB) enumAVal       enum → enum
 */

#include <catch2/catch_all.hpp>
#include "helpers.hpp"

// =============================================================================
// [B2I] bool → int
// =============================================================================
TEST_CASE("Explicit cast: (int) bool true → 1", "[gen][cast][primitive][bool][int]") {
    auto jit = gen_jit(R"SRC(
        module __cast_b2i_true__;
        test() : int {
            b : bool = true;
            return (int) b;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 1);
}

TEST_CASE("Explicit cast: (int) bool false → 0", "[gen][cast][primitive][bool][int]") {
    auto jit = gen_jit(R"SRC(
        module __cast_b2i_false__;
        test() : int {
            b : bool = false;
            return (int) b;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 0);
}

// =============================================================================
// [B2L] bool → long
// =============================================================================
TEST_CASE("Explicit cast: (long) bool true → 1L", "[gen][cast][primitive][bool][long]") {
    auto jit = gen_jit(R"SRC(
        module __cast_b2l__;
        test() : long {
            b : bool = true;
            return (long) b;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int64_t(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 1L);
}

// =============================================================================
// [B2F] bool → float
// =============================================================================
TEST_CASE("Explicit cast: (float) bool true → 1.0f", "[gen][cast][primitive][bool][float]") {
    auto jit = gen_jit(R"SRC(
        module __cast_b2f__;
        test() : float {
            b : bool = true;
            return (float) b;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<float(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 1.0f);
}

TEST_CASE("Explicit cast: (float) bool false → 0.0f", "[gen][cast][primitive][bool][float]") {
    auto jit = gen_jit(R"SRC(
        module __cast_b2f_false__;
        test() : float {
            b : bool = false;
            return (float) b;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<float(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 0.0f);
}

// =============================================================================
// [B2D] bool → double
// =============================================================================
TEST_CASE("Explicit cast: (double) bool true → 1.0", "[gen][cast][primitive][bool][double]") {
    auto jit = gen_jit(R"SRC(
        module __cast_b2d__;
        test() : double {
            b : bool = true;
            return (double) b;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<double(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 1.0);
}

// =============================================================================
// [I2B] int → bool
// =============================================================================
TEST_CASE("Explicit cast: (bool) nonzero int → true", "[gen][cast][primitive][int][bool]") {
    auto jit = gen_jit(R"SRC(
        module __cast_i2b_nonzero__;
        test() : int {
            v : int = 42;
            b : bool = (bool) v;
            if(b) { return 1; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 1);
}

TEST_CASE("Explicit cast: (bool) zero int → false", "[gen][cast][primitive][int][bool]") {
    auto jit = gen_jit(R"SRC(
        module __cast_i2b_zero__;
        test() : int {
            v : int = 0;
            b : bool = (bool) v;
            if(b) { return 1; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 0);
}

TEST_CASE("Explicit cast: (bool) negative int → true", "[gen][cast][primitive][int][bool]") {
    auto jit = gen_jit(R"SRC(
        module __cast_i2b_neg__;
        test() : int {
            v : int = -5;
            b : bool = (bool) v;
            if(b) { return 1; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 1);
}

// =============================================================================
// [I2S] int → short (narrowing)
// =============================================================================
TEST_CASE("Explicit cast: (short) int narrowing", "[gen][cast][primitive][int][short]") {
    auto jit = gen_jit(R"SRC(
        module __cast_i2s__;
        test() : short {
            v : int = 300;
            return (short) v;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int16_t(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 300);
}

TEST_CASE("Explicit cast: (short) int truncation", "[gen][cast][primitive][int][short][trunc]") {
    auto jit = gen_jit(R"SRC(
        module __cast_i2s_trunc__;
        test() : short {
            v : int = 70000;
            return (short) v;   // 70000 & 0xFFFF = 4464, signed: 4464
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int16_t(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == static_cast<int16_t>(70000));  // truncation
}

// =============================================================================
// [I2L] int → long (widening)
// =============================================================================
TEST_CASE("Explicit cast: (long) int widening", "[gen][cast][primitive][int][long]") {
    auto jit = gen_jit(R"SRC(
        module __cast_i2l__;
        test() : long {
            v : int = 123456;
            return (long) v;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int64_t(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 123456L);
}

TEST_CASE("Explicit cast: (long) negative int widening preserves sign", "[gen][cast][primitive][int][long]") {
    auto jit = gen_jit(R"SRC(
        module __cast_i2l_neg__;
        test() : long {
            v : int = -42;
            return (long) v;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int64_t(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == -42L);
}

// =============================================================================
// [I2F] int → float
// =============================================================================
TEST_CASE("Explicit cast: (float) int", "[gen][cast][primitive][int][float]") {
    auto jit = gen_jit(R"SRC(
        module __cast_i2f__;
        test() : float {
            v : int = 42;
            return (float) v;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<float(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42.0f);
}

// =============================================================================
// [I2D] int → double
// =============================================================================
TEST_CASE("Explicit cast: (double) int", "[gen][cast][primitive][int][double]") {
    auto jit = gen_jit(R"SRC(
        module __cast_i2d__;
        test() : double {
            v : int = 99;
            return (double) v;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<double(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 99.0);
}

// =============================================================================
// [F2B] float → bool
// =============================================================================
TEST_CASE("Explicit cast: (bool) nonzero float → true", "[gen][cast][primitive][float][bool]") {
    auto jit = gen_jit(R"SRC(
        module __cast_f2b_nonzero__;
        test() : int {
            f : float = 3.14f;
            b : bool = (bool) f;
            if(b) { return 1; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 1);
}

TEST_CASE("Explicit cast: (bool) zero float → false", "[gen][cast][primitive][float][bool]") {
    auto jit = gen_jit(R"SRC(
        module __cast_f2b_zero__;
        test() : int {
            f : float = 0.0f;
            b : bool = (bool) f;
            if(b) { return 1; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 0);
}

// =============================================================================
// [F2I] float → int
// =============================================================================
TEST_CASE("Explicit cast: (int) float truncates toward zero", "[gen][cast][primitive][float][int]") {
    auto jit = gen_jit(R"SRC(
        module __cast_f2i__;
        test() : int {
            f : float = 3.75f;
            return (int) f;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 3);  // truncates toward zero
}

TEST_CASE("Explicit cast: (int) negative float truncates toward zero", "[gen][cast][primitive][float][int]") {
    auto jit = gen_jit(R"SRC(
        module __cast_f2i_neg__;
        test() : int {
            f : float = -2.9f;
            return (int) f;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == -2);  // truncates toward zero, not floor
}

// =============================================================================
// [F2D] float → double (widening)
// =============================================================================
TEST_CASE("Explicit cast: (double) float widening", "[gen][cast][primitive][float][double]") {
    auto jit = gen_jit(R"SRC(
        module __cast_f2d__;
        test() : double {
            f : float = 1.5f;
            return (double) f;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<double(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 1.5);
}

// =============================================================================
// [D2B] double → bool
// =============================================================================
TEST_CASE("Explicit cast: (bool) nonzero double → true", "[gen][cast][primitive][double][bool]") {
    auto jit = gen_jit(R"SRC(
        module __cast_d2b_nonzero__;
        test() : int {
            d : double = 0.001d;
            b : bool = (bool) d;
            if(b) { return 1; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 1);
}

TEST_CASE("Explicit cast: (bool) zero double → false", "[gen][cast][primitive][double][bool]") {
    auto jit = gen_jit(R"SRC(
        module __cast_d2b_zero__;
        test() : int {
            d : double = 0.0d;
            b : bool = (bool) d;
            if(b) { return 1; }
            return 0;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 0);
}

// =============================================================================
// [D2I] double → int
// =============================================================================
TEST_CASE("Explicit cast: (int) double truncates", "[gen][cast][primitive][double][int]") {
    auto jit = gen_jit(R"SRC(
        module __cast_d2i__;
        test() : int {
            d : double = 99.99d;
            return (int) d;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 99);
}

// =============================================================================
// [D2F] double → float (narrowing)
// =============================================================================
TEST_CASE("Explicit cast: (float) double narrowing", "[gen][cast][primitive][double][float]") {
    auto jit = gen_jit(R"SRC(
        module __cast_d2f__;
        test() : float {
            d : double = 2.5d;
            return (float) d;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<float(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 2.5f);
}

// =============================================================================
// [U2I] byte (unsigned) → int (signed, widening)
// =============================================================================
TEST_CASE("Explicit cast: (int) byte unsigned → int signed widening", "[gen][cast][primitive][byte][int]") {
    auto jit = gen_jit(R"SRC(
        module __cast_u2i__;
        test() : int {
            b : unsigned byte = 200;
            return (int) b;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 200);  // zero-extended, so 200 stays 200
}

// =============================================================================
// [I2U] int (signed) → byte (unsigned, narrowing)
// =============================================================================
TEST_CASE("Explicit cast: (byte) int signed → byte unsigned narrowing", "[gen][cast][primitive][int][byte]") {
    auto jit = gen_jit(R"SRC(
        module __cast_i2u__;
        test() : int {
            v : int = 257;
            b : byte = (byte) v;
            return (int) b;    // 257 & 0xFF = 1
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 1);
}

// =============================================================================
// [S2L] short → long (widening)
// =============================================================================
TEST_CASE("Explicit cast: (long) short widening", "[gen][cast][primitive][short][long]") {
    auto jit = gen_jit(R"SRC(
        module __cast_s2l__;
        test() : long {
            s : short = -1000;
            return (long) s;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int64_t(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == -1000L);
}

// =============================================================================
// [L2S] long → short (narrowing)
// =============================================================================
TEST_CASE("Explicit cast: (short) long narrowing truncates", "[gen][cast][primitive][long][short]") {
    auto jit = gen_jit(R"SRC(
        module __cast_l2s__;
        test() : short {
            l : long = 32767;
            return (short) l;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int16_t(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 32767);
}

// =============================================================================
// [II] Identity cast: (int) int — no-op
// =============================================================================
TEST_CASE("Explicit cast: (int) int identity is no-op", "[gen][cast][primitive][identity]") {
    auto jit = gen_jit(R"SRC(
        module __cast_identity__;
        test() : int {
            v : int = 77;
            return (int) v;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 77);
}

// =============================================================================
// [CH] Chained cast: (int)(double) float — float → double → int
// =============================================================================
TEST_CASE("Explicit cast: chained (int)(double) float", "[gen][cast][primitive][chained]") {
    auto jit = gen_jit(R"SRC(
        module __cast_chained__;
        test() : int {
            f : float = 7.9f;
            return (int)(double) f;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 7);
}

// =============================================================================
// Cast in expression context: arithmetic with mixed types
// =============================================================================
TEST_CASE("Explicit cast: cast inside arithmetic expression", "[gen][cast][primitive][expr]") {
    auto jit = gen_jit(R"SRC(
        module __cast_in_expr__;
        test() : int {
            d : double = 10.7d;
            i : int = 3;
            return (int) d + i;   // 10 + 3 = 13
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 13);
}

// =============================================================================
// Cast of function return value
// =============================================================================
TEST_CASE("Explicit cast: cast of function return value", "[gen][cast][primitive][func]") {
    auto jit = gen_jit(R"SRC(
        module __cast_func_ret__;
        get_pi() : double {
            return 3.14159d;
        }
        test() : int {
            return (int) get_pi();
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 3);
}

// =============================================================================
// Cast of literal value
// =============================================================================
TEST_CASE("Explicit cast: (long) integer literal", "[gen][cast][primitive][literal]") {
    auto jit = gen_jit(R"SRC(
        module __cast_literal__;
        test() : long {
            return (long) 42;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int64_t(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 42L);
}

// =============================================================================
// [E2I] Enum → int explicit cast
// =============================================================================
TEST_CASE("Explicit cast: (int) enum value", "[gen][cast][enum][int]") {
    auto jit = gen_jit(R"SRC(
        module __cast_e2i__;
        enum Color {
            RED = 0;
            GREEN = 10;
            BLUE = 20;
        };
        test() : int {
            c : Color = Color::BLUE;
            return (int) c;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 20);
}

TEST_CASE("Explicit cast: (int) enum value RED=0", "[gen][cast][enum][int]") {
    auto jit = gen_jit(R"SRC(
        module __cast_e2i_red__;
        enum Color {
            RED;
            GREEN;
            BLUE;
        };
        test() : int {
            c : Color = Color::RED;
            return (int) c;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 0);
}

// =============================================================================
// Cast used as function argument
// =============================================================================
TEST_CASE("Explicit cast: cast as function argument", "[gen][cast][primitive][arg]") {
    auto jit = gen_jit(R"SRC(
        module __cast_arg__;
        square(x : int) : int {
            return x * x;
        }
        test() : int {
            d : double = 5.9d;
            return square((int) d);   // square(5) = 25
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 25);
}

// =============================================================================
// Unsigned integer → float
// =============================================================================
TEST_CASE("Explicit cast: (float) byte unsigned → float", "[gen][cast][primitive][byte][float]") {
    auto jit = gen_jit(R"SRC(
        module __cast_u2f__;
        test() : float {
            b : unsigned byte = 200;
            return (float) b;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<float(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 200.0f);
}

// =============================================================================
// Float → unsigned integer
// =============================================================================
TEST_CASE("Explicit cast: (byte) float → byte unsigned", "[gen][cast][primitive][float][byte]") {
    auto jit = gen_jit(R"SRC(
        module __cast_f2u__;
        test() : int {
            f : float = 100.9f;
            b : byte = (byte) f;
            return (int) b;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 100);
}

// =============================================================================
// Long → double
// =============================================================================
TEST_CASE("Explicit cast: (double) long", "[gen][cast][primitive][long][double]") {
    auto jit = gen_jit(R"SRC(
        module __cast_l2d__;
        test() : double {
            l : long = 1000000;
            return (double) l;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<double(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 1000000.0);
}

// =============================================================================
// Double → long
// =============================================================================
TEST_CASE("Explicit cast: (long) double", "[gen][cast][primitive][double][long]") {
    auto jit = gen_jit(R"SRC(
        module __cast_d2l__;
        test() : long {
            d : double = 999999.99d;
            return (long) d;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int64_t(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 999999L);
}

// =============================================================================
// Implicit user-defined cast operator
// =============================================================================
TEST_CASE("Implicit cast: user-defined cast operator applies for init and args",
          "[gen][cast][implicit][operator]") {
    auto jit = gen_jit(R"SRC(
        module __cast_udc_implicit__;

        class Wrapper {
            public value : int;
            public Wrapper(v : int) : value(v) {}
            public operator() : int { return value; }
        }

        plus_one(v : int) : int {
            return v + 1;
        }

        test() : int {
            w : Wrapper(41);
            a : int = w;
            b : int = plus_one(w);
            return a + b;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 83);
}

// =============================================================================
// Constant integer casts (safe range vs out-of-range)
// =============================================================================

TEST_CASE("Constant integer casts: safe constant casts evaluate correctly", "[gen][cast][constant]") {
    auto jit = gen_jit(R"SRC(
        module __cast_const_safe__;

        test() : int {
            // Unsigned to signed safe
            u : unsigned int = 42u;
            s1 : int = (int) 100u;
            s2 : int = (int) (50u + 50u);
            // Signed to unsigned safe
            u1 : unsigned int = (unsigned int) 200;
            u2 : unsigned int = (unsigned int) (100 + 100);
            return s1 + s2 + (int)u1 + (int)u2 + (int)u;
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 642);
}

TEST_CASE("Constant integer casts: various integer widths safe casts", "[gen][cast][constant]") {
    auto jit = gen_jit(R"SRC(
        module __cast_const_widths__;

        test() : int {
            // byte -> unsigned byte
            ub : unsigned byte = (unsigned byte) 127;
            // unsigned short -> short
            s : short = (short) 30000us;
            // long -> unsigned long
            ul : unsigned long = (unsigned long) 1000000l;
            // unsigned long -> long
            l : long = (long) 5000000ul;
            return (int)ub + (int)s + (int)(ul / 100000ul) + (int)(l / 500000l);
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 127 + 30000 + 10 + 10);
}


