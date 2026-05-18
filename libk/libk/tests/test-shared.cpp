/*
 * K Language standard library — Shared<T> tests
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
 * Tests for ::k::Shared<T>.
 *
 * These tests exercise the behaviour of the libk Shared smart pointer
 * by JIT-compiling small K programs that use the stdlib type.
 *
 * The base standard library (module "k") is implicitly imported by the
 * compiler — no explicit "import k;" is needed in the K sources.
 *
 * Shared<T> is a template struct, so each test instantiates it with a
 * concrete class type (IntBox or Tracker).
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
//  1. Null/empty Shared
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Shared<IntBox> — default constructor is null", "[libk][shared]") {
    auto j = jit_k(R"SRC(
        module __shared_null__;
        class IntBox {
            public _val : int;
            IntBox() { _val = 0; }
            IntBox(v : int) { _val = v; }
        }
        test() : int {
            s : Shared<IntBox>;
            if (s.isNull()) return 1;
            return 0;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

TEST_CASE("Shared<IntBox> — null get() returns null", "[libk][shared]") {
    auto j = jit_k(R"SRC(
        module __shared_null_get__;
        class IntBox {
            public _val : int;
            IntBox() { _val = 0; }
            IntBox(v : int) { _val = v; }
        }
        test() : int {
            s : Shared<IntBox>;
            p : IntBox* = s.get();
            if (p == null) return 1;
            return 0;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

TEST_CASE("Shared<IntBox> — null useCount() returns 0", "[libk][shared]") {
    auto j = jit_k(R"SRC(
        module __shared_null_uc__;
        class IntBox {
            public _val : int;
            IntBox() { _val = 0; }
            IntBox(v : int) { _val = v; }
        }
        test() : int {
            s : Shared<IntBox>;
            return s.useCount();
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 0);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  2. Acquire from owner
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Shared<IntBox> — acquire from owner", "[libk][shared]") {
    auto j = jit_k(R"SRC(
        module __shared_acquire__;
        class IntBox {
            public _val : int;
            IntBox() { _val = 0; }
            IntBox(v : int) { _val = v; }
        }
        test() : int {
            p : IntBox! = new IntBox(42);
            s : Shared<IntBox>;
            s.acquire(p);
            result : int = 0;
            if (!s.isNull()) { result = result + 1; }
            if (s.useCount() == 1) { result = result + 10; }
            ptr : IntBox* = s.get();
            if (ptr != null) {
                if (ptr->_val == 42) { result = result + 100; }
            }
            if (p == null) { result = result + 1000; }
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1111);
}

TEST_CASE("Shared<IntBox> — constructor from owner drains", "[libk][shared]") {
    auto j = jit_k(R"SRC(
        module __shared_ctor_drain__;
        class IntBox {
            public _val : int;
            IntBox() { _val = 0; }
            IntBox(v : int) { _val = v; }
        }
        test() : int {
            p : IntBox! = new IntBox(99);
            s : Shared<IntBox>(p);
            result : int = 0;
            if (!s.isNull()) { result = result + 1; }
            if (s.useCount() == 1) { result = result + 10; }
            ptr : IntBox* = s.get();
            if (ptr != null) {
                if (ptr->_val == 99) { result = result + 100; }
            }
            if (p == null) { result = result + 1000; }
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1111);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  3. Share (copy)
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Shared<IntBox> — share increases useCount", "[libk][shared]") {
    auto j = jit_k(R"SRC(
        module __shared_share__;
        class IntBox {
            public _val : int;
            IntBox() { _val = 0; }
            IntBox(v : int) { _val = v; }
        }
        test() : int {
            p : IntBox! = new IntBox(77);
            s1 : Shared<IntBox>;
            s1.acquire(p);
            s2 : Shared<IntBox>;
            s2.share(s1);
            result : int = 0;
            if (s1.useCount() == 2) { result = result + 1; }
            if (s2.useCount() == 2) { result = result + 10; }
            if (s1.get() == s2.get()) { result = result + 100; }
            ptr : IntBox* = s2.get();
            if (ptr != null) {
                if (ptr->_val == 77) { result = result + 1000; }
            }
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1111);
}

TEST_CASE("Shared<IntBox> — multiple shares", "[libk][shared]") {
    auto j = jit_k(R"SRC(
        module __shared_multi__;
        class IntBox {
            public _val : int;
            IntBox() { _val = 0; }
            IntBox(v : int) { _val = v; }
        }
        test() : int {
            p : IntBox! = new IntBox(55);
            s1 : Shared<IntBox>;
            s1.acquire(p);
            s2 : Shared<IntBox>;
            s2.share(s1);
            s3 : Shared<IntBox>;
            s3.share(s2);
            result : int = 0;
            if (s1.useCount() == 3) { result = result + 1; }
            if (s2.useCount() == 3) { result = result + 10; }
            if (s3.useCount() == 3) { result = result + 100; }
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 111);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  4. Reset releases participation
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Shared<IntBox> — reset decreases useCount", "[libk][shared]") {
    auto j = jit_k(R"SRC(
        module __shared_reset__;
        class IntBox {
            public _val : int;
            IntBox() { _val = 0; }
            IntBox(v : int) { _val = v; }
        }
        test() : int {
            p : IntBox! = new IntBox(33);
            s1 : Shared<IntBox>;
            s1.acquire(p);
            s2 : Shared<IntBox>;
            s2.share(s1);
            s1.reset();
            result : int = 0;
            if (s1.isNull()) { result = result + 1; }
            if (s2.useCount() == 1) { result = result + 10; }
            ptr : IntBox* = s2.get();
            if (ptr != null) {
                if (ptr->_val == 33) { result = result + 100; }
            }
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 111);
}

TEST_CASE("Shared<IntBox> — reset on null is no-op", "[libk][shared]") {
    auto j = jit_k(R"SRC(
        module __shared_reset_null__;
        class IntBox {
            public _val : int;
            IntBox() { _val = 0; }
            IntBox(v : int) { _val = v; }
        }
        test() : int {
            s : Shared<IntBox>;
            s.reset();
            if (s.isNull()) return 1;
            return 0;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  5. Destructor releases properly
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Shared<IntBox> — destructor of copy preserves object", "[libk][shared]") {
    auto j = jit_k(R"SRC(
        module __shared_dtor_copy__;
        class IntBox {
            public _val : int;
            IntBox() { _val = 0; }
            IntBox(v : int) { _val = v; }
        }
        test() : int {
            p : IntBox! = new IntBox(88);
            s1 : Shared<IntBox>;
            s1.acquire(p);
            {
                s2 : Shared<IntBox>;
                s2.share(s1);
            }
            result : int = 0;
            if (s1.useCount() == 1) { result = result + 1; }
            ptr : IntBox* = s1.get();
            if (ptr != null) {
                if (ptr->_val == 88) { result = result + 10; }
            }
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 11);
}

TEST_CASE("Shared<IntBox> — original destroyed first, copy survives", "[libk][shared]") {
    auto j = jit_k(R"SRC(
        module __shared_dtor_orig__;
        class IntBox {
            public _val : int;
            IntBox() { _val = 0; }
            IntBox(v : int) { _val = v; }
        }
        test() : int {
            s2 : Shared<IntBox>;
            {
                p : IntBox! = new IntBox(66);
                s1 : Shared<IntBox>;
                s1.acquire(p);
                s2.share(s1);
            }
            result : int = 0;
            if (s2.useCount() == 1) { result = result + 1; }
            ptr : IntBox* = s2.get();
            if (ptr != null) {
                if (ptr->_val == 66) { result = result + 10; }
            }
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 11);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  6. Class type with destructor side-effect
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Shared<class> — destructor called on last release", "[libk][shared]") {
    auto j = jit_k(R"SRC(
        module __shared_dtor_class__;
        flag : int = 0;
        class Tracker {
            public _id : int;
            Tracker() { _id = 0; }
            ~Tracker() { flag = flag + 1; }
        }
        test() : int {
            {
                s1 : Shared<Tracker>;
                t : Tracker! = new Tracker();
                t->_id = 7;
                s1.acquire(t);
                s2 : Shared<Tracker>;
                s2.share(s1);
                s1.reset();
                if (flag != 0) { return 99; }
            }
            return flag;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  7. Reset with new pointer
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Shared<IntBox> — reset with new pointer", "[libk][shared]") {
    auto j = jit_k(R"SRC(
        module __shared_reset_new__;
        class IntBox {
            public _val : int;
            IntBox() { _val = 0; }
            IntBox(v : int) { _val = v; }
        }
        test() : int {
            p1 : IntBox! = new IntBox(10);
            s : Shared<IntBox>;
            s.acquire(p1);
            p2 : IntBox! = new IntBox(20);
            s.reset(p2);
            result : int = 0;
            if (s.useCount() == 1) { result = result + 1; }
            ptr : IntBox* = s.get();
            if (ptr != null) {
                if (ptr->_val == 20) { result = result + 10; }
            }
            return result;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 11);
}

// ═══════════════════════════════════════════════════════════════════════════════
//  8. Share with null shared is no-op
// ═══════════════════════════════════════════════════════════════════════════════

TEST_CASE("Shared<IntBox> — share with null remains null", "[libk][shared]") {
    auto j = jit_k(R"SRC(
        module __shared_share_null__;
        class IntBox {
            public _val : int;
            IntBox() { _val = 0; }
            IntBox(v : int) { _val = v; }
        }
        test() : int {
            s1 : Shared<IntBox>;
            s2 : Shared<IntBox>;
            s2.share(s1);
            if (s2.isNull()) return 1;
            return 0;
        }
    )SRC");
    REQUIRE(j);
    auto fn = j->lookup_symbol<int(*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 1);
}
