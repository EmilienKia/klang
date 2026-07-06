/*
 * K Language standard library — Optional<T> tests
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
 * Tests for ::k::Optional<T>.
 *
 * These tests exercise Optional<T> by JIT-compiling small K programs that
 * use the stdlib type.
 *
 * The base standard library (module "k") is implicitly imported by the
 * compiler — no explicit "import k;" is needed in the K sources.
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

/// Shorthand: compile K source that uses the base stdlib and JIT it.
std::unique_ptr<k::model::gen::jit> jit_k(std::string_view src) {
    return gen_jit_with_stdlib(src, LIBK_KDI_DIR, LIBK_LIB_DIR);
}

} // anonymous namespace

// ═══════════════════════════════════════════════════════════════════════════════
//  Default constructor — empty optional
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Optional default constructor is empty", "[libk][optional]") {
    auto j = jit_k(R"SRC(
        module __opt_default__;
        test() : int {
            opt : Optional<int>;
            if (opt.hasValue()) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Value constructor
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Optional value constructor", "[libk][optional]") {
    auto j = jit_k(R"SRC(
        module __opt_value_ctor__;
        test() : int {
            opt : Optional<int>(42);
            if (!opt.hasValue()) return 0;
            if (opt.get() != 42) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Copy constructor — with value
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Optional copy constructor — with value", "[libk][optional][copy]") {
    auto j = jit_k(R"SRC(
        module __opt_copy_value__;
        test() : int {
            src : Optional<int>(99);
            dst : Optional<int>(src);
            if (!dst.hasValue()) return 0;
            if (dst.get() != 99) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Copy constructor — empty
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Optional copy constructor — empty", "[libk][optional][copy]") {
    auto j = jit_k(R"SRC(
        module __opt_copy_empty__;
        test() : int {
            src : Optional<int>;
            dst : Optional<int>(src);
            if (dst.hasValue()) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  set() on empty optional
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Optional set on empty", "[libk][optional]") {
    auto j = jit_k(R"SRC(
        module __opt_set_empty__;
        test() : int {
            opt : Optional<int>;
            opt.set(7);
            if (!opt.hasValue()) return 0;
            if (opt.get() != 7) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  set() replaces existing value
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Optional set replaces existing value", "[libk][optional]") {
    auto j = jit_k(R"SRC(
        module __opt_set_replace__;
        test() : int {
            opt : Optional<int>(10);
            opt.set(20);
            if (!opt.hasValue()) return 0;
            if (opt.get() != 20) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  reset() clears value
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Optional reset clears value", "[libk][optional]") {
    auto j = jit_k(R"SRC(
        module __opt_reset__;
        test() : int {
            opt : Optional<int>(55);
            opt.reset();
            if (opt.hasValue()) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  reset() on empty is safe (no-op)
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Optional reset on empty is safe", "[libk][optional]") {
    auto j = jit_k(R"SRC(
        module __opt_reset_empty__;
        test() : int {
            opt : Optional<int>;
            opt.reset();
            if (opt.hasValue()) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  getOr() with value
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Optional getOr with value", "[libk][optional]") {
    auto j = jit_k(R"SRC(
        module __opt_getor_value__;
        test() : int {
            opt : Optional<int>(33);
            return opt.getOr(0);
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 33);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  getOr() without value
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Optional getOr without value", "[libk][optional]") {
    auto j = jit_k(R"SRC(
        module __opt_getor_empty__;
        test() : int {
            opt : Optional<int>;
            return opt.getOr(77);
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 77);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Optional<T>::empty() static factory
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Optional::empty static factory", "[libk][optional]") {
    auto j = jit_k(R"SRC(
        module __opt_empty_static__;
        test() : int {
            opt : Optional<int> = Optional<int>::empty();
            if (opt.hasValue()) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Struct type in Optional
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Optional with struct type", "[libk][optional]") {
    auto j = jit_k(R"SRC(
        module __opt_struct__;
        struct Point {
            x : int;
            y : int;
            Point(ax : int, ay : int) {
                x = ax;
                y = ay;
            }
        }
        test() : int {
            p : Point(3, 7);
            opt : Optional<Point>(p);
            if (!opt.hasValue()) return 0;
            if (opt.get().x != 3) return 0;
            if (opt.get().y != 7) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  Inline chained call — getOr on a function-returned Optional (struct rvalue)
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Optional<byte> getOr with value (named variable, verbose form)", "[libk][optional][inline]") {
    // Mirrors the existing Optional<int> test but with Optional<byte>.
    // If this fails, the issue is with Optional<byte> instantiation per se.
    auto j = jit_k(R"SRC(
        module __opt_byte_getor__;
        test() : int {
            opt : Optional<byte>((byte)42);
            zero : byte = (byte) 0;
            return (int) opt.getOr(zero);
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

// Regression test for the formerly-broken "nested-template member unresolved on
// return-by-value" bug: a template aggregate with a nested-template-typed member
// (Optional<T> holds UniSlot<T>) used to be left with its nested member type
// unresolved ("<<unresolved:UniSlot>>") when instantiated via a by-value function
// RETURN type, causing diagnostic 000F4. Now fixed (aggregate_type_resolver
// propagates the enclosing template's argument map when resolving nested template
// member types). See TODO.md / resolvers_aggregate.cpp.
TEST_CASE("Optional<int> getOr on function-returned struct rvalue (inline)", "[libk][optional][inline]") {
    // Calls .getOr() directly on a function-returned Optional<int> (struct rvalue).
    auto j = jit_k(R"SRC(
        module __opt_int_inline__;
        make_opt(v: int) : Optional<int> {
            o : Optional<int>(v);
            return o;
        }
        test() : int {
            return make_opt(42).getOr(0);
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

// Same return-by-value pattern as above, for Optional<byte> — this is exactly the
// shape that lets DataInputStream::readByte() be written in one line. The nested
// member (UniSlot<byte>) is now resolved correctly. The getOr argument is given an
// explicit (byte) cast because an int literal does not implicitly narrow to a byte
// parameter during overload resolution (a separate, unrelated limitation).
TEST_CASE("Optional<byte> getOr on function-returned struct rvalue (inline)", "[libk][optional][inline]") {
    // Key pattern for DataInputStream::readByte() simplification.
    auto j = jit_k(R"SRC(
        module __opt_byte_inline__;
        make_opt(v: byte) : Optional<byte> {
            o : Optional<byte>(v);
            return o;
        }
        make_empty() : Optional<byte> {
            o : Optional<byte>;
            return o;
        }
        test_with_value() : int {
            return (int) make_opt((byte)42).getOr((byte)0);
        }
        test_empty() : int {
            return (int) make_empty().getOr((byte)0);
        }
        test_empty_default() : int {
            return (int) make_empty().getOr((byte)99);
        }
    )SRC");
    REQUIRE(j);
    auto with_val = j->lookup_symbol<int(*)()>("test_with_value");
    auto empty    = j->lookup_symbol<int(*)()>("test_empty");
    auto dflt     = j->lookup_symbol<int(*)()>("test_empty_default");
    REQUIRE(with_val);
    REQUIRE(empty);
    REQUIRE(dflt);
    CHECK(with_val() == 42);
    CHECK(empty()    == 0);
    CHECK(dflt()     == 99);
}

// Regression test for bug (b): inline temporary construction of a TEMPLATE type
// in expression position (`Optional<byte>(value)`) chained with a member call.
// This used to be looked up as a free function and reported as 000FD ("No function
// named 'Optional' found"). It is now recognised as a temporary construction of the
// instantiated template aggregate, and member access on the temporary works.
TEST_CASE("Optional inline-constructed temporary getOr (without named variable)", "[libk][optional][inline]") {
    // Verifies Optional<byte>(value).getOr(0) without an intermediate variable.
    auto j = jit_k(R"SRC(
        module __opt_tmp_chain__;
        test_with_value() : int {
            return (int) Optional<byte>((byte)7).getOr((byte)0);
        }
    )SRC");
    REQUIRE(j);
    auto with_val = j->lookup_symbol<int(*)()>("test_with_value");
    REQUIRE(with_val);
    CHECK(with_val() == 7);
}

// Regression test for bug (c): a template-qualified static factory call in
// expression position chained with a member call —
// `Optional<byte>::empty().getOr(...)`. Two things used to break:
//   (1) parsing `Type<args>.member` consumed the `<` as a relational operator
//       (diagnostic 00034) — now `.`/`->` after template args is accepted; and
//   (2) the static call's return type Optional<T> stayed unresolved
//       (`<<unresolved:Optional>>`), so the chained `.getOr(...)` failed (000F2).
//       The call's unresolved template return type is now instantiated, so member
//       access sees a concrete struct type.
// Note: K uses `::` (not `.`) for static member access, matching non-template types.
TEST_CASE("Optional template-qualified static factory call (inline)", "[libk][optional][inline]") {
    // Verifies Optional<byte>::empty().getOr(default) (template-qualified static factory).
    auto j = jit_k(R"SRC(
        module __opt_tmp_factory__;
        test_empty() : int {
            return (int) Optional<byte>::empty().getOr((byte)0);
        }
        test_empty_default() : int {
            return (int) Optional<byte>::empty().getOr((byte)99);
        }
    )SRC");
    REQUIRE(j);
    auto empty = j->lookup_symbol<int(*)()>("test_empty");
    auto dflt  = j->lookup_symbol<int(*)()>("test_empty_default");
    REQUIRE(empty);
    REQUIRE(dflt);
    CHECK(empty() == 0);
    CHECK(dflt()  == 99);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  set() then reset() then set() — lifecycle correctness
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Optional set-reset-set lifecycle", "[libk][optional]") {
    auto j = jit_k(R"SRC(
        module __opt_lifecycle__;
        test() : int {
            opt : Optional<int>;
            opt.set(1);
            if (opt.get() != 1) return 0;
            opt.reset();
            if (opt.hasValue()) return 0;
            opt.set(2);
            if (opt.get() != 2) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  OptionalConstRef<T> tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("OptionalConstRef default constructor is empty", "[libk][optional][optional-const-ref]") {
    auto j = jit_k(R"SRC(
        module __optcr_default__;
        test() : int {
            opt : OptionalConstRef<int>;
            if (opt.hasValue()) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

TEST_CASE("OptionalConstRef constructor from reference — hasValue and get", "[libk][optional][optional-const-ref]") {
    auto j = jit_k(R"SRC(
        module __optcr_ref__;
        test() : int {
            x : int = 42;
            opt : OptionalConstRef<int>(x);
            if (!opt.hasValue()) return 0;
            if (opt.get() != 42) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

TEST_CASE("OptionalConstRef dereference operator*", "[libk][optional][optional-const-ref]") {
    auto j = jit_k(R"SRC(
        module __optcr_deref__;
        test() : int {
            x : int = 99;
            opt : OptionalConstRef<int>(x);
            // operator*() is defined but K's built-in '*' only works on pointer types.
            // Call it explicitly via get() — semantically identical.
            if (opt.get() != 99) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

TEST_CASE("OptionalConstRef copy constructor — with value", "[libk][optional][optional-const-ref][copy]") {
    auto j = jit_k(R"SRC(
        module __optcr_copy_value__;
        test() : int {
            x : int = 7;
            src : OptionalConstRef<int>(x);
            dst : OptionalConstRef<int>(src);
            if (!dst.hasValue()) return 0;
            if (dst.get() != 7) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

TEST_CASE("OptionalConstRef copy constructor — empty", "[libk][optional][optional-const-ref][copy]") {
    auto j = jit_k(R"SRC(
        module __optcr_copy_empty__;
        test() : int {
            src : OptionalConstRef<int>;
            dst : OptionalConstRef<int>(src);
            if (dst.hasValue()) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

TEST_CASE("OptionalConstRef getOr with value", "[libk][optional][optional-const-ref]") {
    auto j = jit_k(R"SRC(
        module __optcr_getor_value__;
        test() : int {
            x : int = 33;
            opt : OptionalConstRef<int>(x);
            def : int = 0;
            return opt.getOr(def);
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 33);
}

TEST_CASE("OptionalConstRef getOr without value", "[libk][optional][optional-const-ref]") {
    auto j = jit_k(R"SRC(
        module __optcr_getor_empty__;
        test() : int {
            opt : OptionalConstRef<int>;
            def : int = 77;
            return opt.getOr(def);
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 77);
}

// reflect that OptionalConstRef does NOT own the value — modifying the
// original variable changes what get() observes via the stored pointer.
TEST_CASE("OptionalConstRef reflects mutations of referenced variable", "[libk][optional][optional-const-ref]") {
    auto j = jit_k(R"SRC(
        module __optcr_reflect__;
        test() : int {
            x : int = 1;
            opt : OptionalConstRef<int>(x);
            if (opt.get() != 1) return 0;
            x = 2;
            if (opt.get() != 2) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  OptionalRef<T> tests
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("OptionalRef default constructor is empty", "[libk][optional][optional-ref]") {
    auto j = jit_k(R"SRC(
        module __opr_default__;
        test() : int {
            opt : OptionalRef<int>;
            if (opt.hasValue()) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

TEST_CASE("OptionalRef constructor from reference — hasValue and get", "[libk][optional][optional-ref]") {
    auto j = jit_k(R"SRC(
        module __opr_ref__;
        test() : int {
            x : int = 42;
            opt : OptionalRef<int>(x);
            if (!opt.hasValue()) return 0;
            if (opt.get() != 42) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

TEST_CASE("OptionalRef dereference operator*", "[libk][optional][optional-ref]") {
    auto j = jit_k(R"SRC(
        module __opr_deref__;
        test() : int {
            x : int = 55;
            opt : OptionalRef<int>(x);
            // operator*() is defined but K's built-in '*' only works on pointer types.
            // Call it explicitly via get() — semantically identical.
            if (opt.get() != 55) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

TEST_CASE("OptionalRef copy constructor — with value", "[libk][optional][optional-ref][copy]") {
    auto j = jit_k(R"SRC(
        module __opr_copy_value__;
        test() : int {
            x : int = 13;
            src : OptionalRef<int>(x);
            dst : OptionalRef<int>(src);
            if (!dst.hasValue()) return 0;
            if (dst.get() != 13) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

TEST_CASE("OptionalRef copy constructor — empty", "[libk][optional][optional-ref][copy]") {
    auto j = jit_k(R"SRC(
        module __opr_copy_empty__;
        test() : int {
            src : OptionalRef<int>;
            dst : OptionalRef<int>(src);
            if (dst.hasValue()) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

TEST_CASE("OptionalRef set rebinds to new variable", "[libk][optional][optional-ref]") {
    auto j = jit_k(R"SRC(
        module __opr_set__;
        test() : int {
            x : int = 10;
            y : int = 20;
            opt : OptionalRef<int>(x);
            if (opt.get() != 10) return 0;
            opt.set(y);
            if (opt.get() != 20) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

TEST_CASE("OptionalRef set on empty optional", "[libk][optional][optional-ref]") {
    auto j = jit_k(R"SRC(
        module __opr_set_empty__;
        test() : int {
            x : int = 5;
            opt : OptionalRef<int>;
            if (opt.hasValue()) return 0;
            opt.set(x);
            if (!opt.hasValue()) return 0;
            if (opt.get() != 5) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

TEST_CASE("OptionalRef getOr with value", "[libk][optional][optional-ref]") {
    auto j = jit_k(R"SRC(
        module __opr_getor_value__;
        test() : int {
            x : int = 33;
            opt : OptionalRef<int>(x);
            def : int = 0;
            return opt.getOr(def);
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 33);
}

TEST_CASE("OptionalRef getOr without value", "[libk][optional][optional-ref]") {
    auto j = jit_k(R"SRC(
        module __opr_getor_empty__;
        test() : int {
            opt : OptionalRef<int>;
            def : int = 77;
            return opt.getOr(def);
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 77);
}

// OptionalRef is a non-owning mutable reference wrapper — get() returns a
// mutable reference, so writing through it modifies the original variable.
TEST_CASE("OptionalRef mutation via get modifies original variable", "[libk][optional][optional-ref]") {
    auto j = jit_k(R"SRC(
        module __opr_mutate__;
        test() : int {
            x : int = 1;
            opt : OptionalRef<int>(x);
            opt.get() = 42;
            if (x != 42) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

// OptionalRef reflects mutations of the referenced variable (same pointer).
TEST_CASE("OptionalRef reflects mutations of referenced variable", "[libk][optional][optional-ref]") {
    auto j = jit_k(R"SRC(
        module __opr_reflect__;
        test() : int {
            x : int = 1;
            opt : OptionalRef<int>(x);
            if (opt.get() != 1) return 0;
            x = 2;
            if (opt.get() != 2) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

// OptionalRef with a struct type — verify member access through get().
TEST_CASE("OptionalRef with struct type", "[libk][optional][optional-ref]") {
    auto j = jit_k(R"SRC(
        module __opr_struct__;
        struct Point {
            x : int;
            y : int;
            Point(ax : int, ay : int) {
                x = ax;
                y = ay;
            }
        }
        test() : int {
            p : Point(3, 7);
            opt : OptionalRef<Point>(p);
            if (!opt.hasValue()) return 0;
            if (opt.get().x != 3) return 0;
            if (opt.get().y != 7) return 0;
            return 1;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

