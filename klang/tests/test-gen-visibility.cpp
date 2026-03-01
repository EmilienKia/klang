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

#include <catch2/catch_all.hpp>

#include "../src/common/logger.hpp"
#include "../src/parse/parser.hpp"
#include "../src/model/model.hpp"
#include "../src/gen/generators.hpp"
#include "../src/compiler.hpp"

#include "helpers.hpp"

// =============================================================================
// Visibility — model-level checks (verify _visibility field is set correctly)
// =============================================================================

TEST_CASE("Visibility: default visibility is PUBLIC for namespace members", "[visibility][model]") {
    auto comp = k::compiler::create();
    comp->parse_source("", R"SRC(
        module vis_test;
        x : int;
        foo() : int { return 1; }
    )SRC");

    auto elems = comp->find_elements("x");
    REQUIRE(!elems.empty());
    auto gv = std::dynamic_pointer_cast<k::model::global_variable_definition>(elems[0]);
    REQUIRE(gv);
    REQUIRE(gv->get_visibility() == k::model::PUBLIC);

    auto fn_elems = comp->find_elements("foo");
    REQUIRE(!fn_elems.empty());
    auto fn = std::dynamic_pointer_cast<k::model::function>(fn_elems[0]);
    REQUIRE(fn);
    REQUIRE(fn->get_visibility() == k::model::PUBLIC);
}

TEST_CASE("Visibility: default visibility is PUBLIC for struct members", "[visibility][model]") {
    auto comp = k::compiler::create();
    comp->parse_source("", R"SRC(
        module vis_test;
        struct S {
            x : int;
            foo() : int { return x; }
        }
    )SRC");

    auto st_elems = comp->find_elements("S");
    REQUIRE(!st_elems.empty());
    auto st = std::dynamic_pointer_cast<k::model::structure>(st_elems[0]);
    REQUIRE(st);
    REQUIRE(st->get_visibility() == k::model::PUBLIC);

    auto mv = std::dynamic_pointer_cast<k::model::member_variable_definition>(st->get_variable("x"));
    REQUIRE(mv);
    REQUIRE(mv->get_visibility() == k::model::PUBLIC);

    auto fn = st->get_function("foo");
    REQUIRE(fn);
    REQUIRE(fn->get_visibility() == k::model::PUBLIC);
}

TEST_CASE("Visibility: group specifier sets visibility for subsequent members", "[visibility][model]") {
    auto comp = k::compiler::create();
    comp->parse_source("", R"SRC(
        module vis_test;
        struct S {
        public:
            a : int;
        protected:
            b : int;
        private:
            c : int;
        }
    )SRC");

    auto st_elems = comp->find_elements("S");
    REQUIRE(!st_elems.empty());
    auto st = std::dynamic_pointer_cast<k::model::structure>(st_elems[0]);
    REQUIRE(st);

    auto ma = std::dynamic_pointer_cast<k::model::member_variable_definition>(st->get_variable("a"));
    REQUIRE(ma);
    REQUIRE(ma->get_visibility() == k::model::PUBLIC);

    auto mb = std::dynamic_pointer_cast<k::model::member_variable_definition>(st->get_variable("b"));
    REQUIRE(mb);
    REQUIRE(mb->get_visibility() == k::model::PROTECTED);

    auto mc = std::dynamic_pointer_cast<k::model::member_variable_definition>(st->get_variable("c"));
    REQUIRE(mc);
    REQUIRE(mc->get_visibility() == k::model::PRIVATE);
}

TEST_CASE("Visibility: per-element specifier overrides group visibility", "[visibility][model]") {
    auto comp = k::compiler::create();
    comp->parse_source("", R"SRC(
        module vis_test;
        struct S {
        private:
            a : int;
            public b : int;
            c : int;
        }
    )SRC");

    auto st_elems = comp->find_elements("S");
    REQUIRE(!st_elems.empty());
    auto st = std::dynamic_pointer_cast<k::model::structure>(st_elems[0]);
    REQUIRE(st);

    // a: private (from group)
    auto ma = std::dynamic_pointer_cast<k::model::member_variable_definition>(st->get_variable("a"));
    REQUIRE(ma);
    REQUIRE(ma->get_visibility() == k::model::PRIVATE);

    // b: public (per-element overrides group private)
    auto mb = std::dynamic_pointer_cast<k::model::member_variable_definition>(st->get_variable("b"));
    REQUIRE(mb);
    REQUIRE(mb->get_visibility() == k::model::PUBLIC);

    // c: private (back to group)
    auto mc = std::dynamic_pointer_cast<k::model::member_variable_definition>(st->get_variable("c"));
    REQUIRE(mc);
    REQUIRE(mc->get_visibility() == k::model::PRIVATE);
}

TEST_CASE("Visibility: per-element specifier on namespace-level function", "[visibility][model]") {
    auto comp = k::compiler::create();
    comp->parse_source("", R"SRC(
        module vis_test;
        public  pub_fn() : int { return 1; }
        protected prot_fn() : int { return 2; }
        private priv_fn() : int { return 3; }
    )SRC");

    auto pub_elems = comp->find_elements("pub_fn");
    REQUIRE(!pub_elems.empty());
    REQUIRE(std::dynamic_pointer_cast<k::model::function>(pub_elems[0])->get_visibility() == k::model::PUBLIC);

    auto prot_elems = comp->find_elements("prot_fn");
    REQUIRE(!prot_elems.empty());
    REQUIRE(std::dynamic_pointer_cast<k::model::function>(prot_elems[0])->get_visibility() == k::model::PROTECTED);

    auto priv_elems = comp->find_elements("priv_fn");
    REQUIRE(!priv_elems.empty());
    REQUIRE(std::dynamic_pointer_cast<k::model::function>(priv_elems[0])->get_visibility() == k::model::PRIVATE);
}

// =============================================================================
// Visibility — runtime / execution tests
// =============================================================================

TEST_CASE("Visibility: public struct members accessible from outside", "[visibility][gen]") {
    auto jit = gen_jit(R"SRC(
        module vis_test;
        struct S {
        public:
            x : int;
            get_x() : int { return x; }
        }
        test() : int {
            s : S;
            s.x = 42;
            return s.get_x();
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

TEST_CASE("Visibility: private struct member variable accessible from member function", "[visibility][gen]") {
    auto jit = gen_jit(R"SRC(
        module vis_test;
        struct Counter {
        private:
            count : int;
        public:
            Counter() : count(0) {}
            increment() { count = count + 1; }
            get() : int { return count; }
        }
        test() : int {
            c : Counter;
            c.increment();
            c.increment();
            c.increment();
            return c.get();
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 3);
}

TEST_CASE("Visibility: private struct method accessible from another method of same struct", "[visibility][gen]") {
    auto jit = gen_jit(R"SRC(
        module vis_test;
        struct Helper {
        private:
            compute(p: int, q: int) : int { return p + q; }
        public:
            sum(a: int, b: int) : int { return this.compute(a, b); }
        }
        test() : int {
            h : Helper;
            return h.sum(10, 32);
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

TEST_CASE("Visibility: private namespace function callable from same namespace", "[visibility][gen]") {
    auto jit = gen_jit(R"SRC(
        module vis_test;
        private helper() : int { return 42; }
        test() : int { return helper(); }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

TEST_CASE("Visibility: group visibility switch in namespace", "[visibility][gen]") {
    auto jit = gen_jit(R"SRC(
        module vis_test;
        private:
        helper() : int { return 21; }
        public:
        test() : int { return helper() * 2; }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

TEST_CASE("Visibility: struct visibility set by per-element specifier", "[visibility][model]") {
    auto comp = k::compiler::create();
    comp->parse_source("", R"SRC(
        module vis_test;
        public struct PubS { x : int; }
        private struct PrivS { y : int; }
    )SRC");

    auto pub_elems = comp->find_elements("PubS");
    REQUIRE(!pub_elems.empty());
    auto pub_st = std::dynamic_pointer_cast<k::model::structure>(pub_elems[0]);
    REQUIRE(pub_st);
    REQUIRE(pub_st->get_visibility() == k::model::PUBLIC);

    auto priv_elems = comp->find_elements("PrivS");
    REQUIRE(!priv_elems.empty());
    auto priv_st = std::dynamic_pointer_cast<k::model::structure>(priv_elems[0]);
    REQUIRE(priv_st);
    REQUIRE(priv_st->get_visibility() == k::model::PRIVATE);
}

// =============================================================================
// Visibility — enforcement (access denied)
// =============================================================================

TEST_CASE("Visibility: private member variable not accessible from outside struct", "[visibility][error]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module vis_test;
        struct S {
        private:
            x : int;
        }
        test() : int {
            s : S;
            return s.x;
        }
    )SRC"));
}

TEST_CASE("Visibility: private member function not callable from outside struct", "[visibility][error]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module vis_test;
        struct S {
        private:
            secret() : int { return 42; }
        }
        test() : int {
            s : S;
            return s.secret();
        }
    )SRC"));
}

TEST_CASE("Visibility: protected member variable not accessible from outside struct", "[visibility][error]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module vis_test;
        struct S {
        protected:
            val : int;
        }
        test() : int {
            s : S;
            return s.val;
        }
    )SRC"));
}

TEST_CASE("Visibility: private namespace function not callable from different namespace", "[visibility][error]") {
    // Within a single module we can call private functions from the same ns;
    // but accessing the variable from outside the namespace should fail.
    // Here we test that private VARIABLE is not accessible.
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module vis_test;
        namespace inner {
            private x : int;
        }
        test() : int {
            return inner::x;
        }
    )SRC"));
}

// =============================================================================
// Visibility — PROTECTED: accessible from subclasses
// =============================================================================

TEST_CASE("Visibility: protected member variable accessible from subclass method", "[visibility][gen]") {
    auto jit = gen_jit(R"SRC(
        module vis_test;
        struct Base {
        protected:
            val : int;
        public:
            Base() : val(42) {}
        }
        struct Derived : public Base {
            Derived() {}
            get() : int { return this.val; }   // accesses protected member of Base
        }
        test() : int {
            d : Derived;
            return d.get();
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

TEST_CASE("Visibility: protected member variable not accessible from unrelated struct", "[visibility][error]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module vis_test;
        struct A {
        protected:
            x : int;
        public:
            A() : x(0) {}
        }
        steal(a : A&) : int { return a.x; }   // must be rejected: free function, not a subclass
    )SRC"));
}

TEST_CASE("Visibility: protected method accessible from subclass method", "[visibility][gen]") {
    auto jit = gen_jit(R"SRC(
        module vis_test;
        struct Base {
        protected:
            compute() : int { return 21; }
        }
        struct Derived : public Base {
            Derived() {}
            result() : int { return this.compute() * 2; }   // calls protected method of Base
        }
        test() : int {
            d : Derived;
            return d.result();
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

TEST_CASE("Visibility: protected method not callable from unrelated struct", "[visibility][error]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module vis_test;
        struct A {
        protected:
            secret() : int { return 1; }
        }
        call(a : A&) : int { return a.secret(); }   // must be rejected: free function, not a subclass
    )SRC"));
}

TEST_CASE("Visibility: private member not accessible from subclass", "[visibility][error]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module vis_test;
        struct Base {
        private:
            x : int;
        public:
            Base() : x(0) {}
        }
        struct Derived : public Base {
            Derived() {}
            get() : int { return this.x; }   // must be rejected: private, not accessible from subclass
        }
    )SRC"));
}

// =============================================================================
// Visibility — member access via -> operator
// =============================================================================

TEST_CASE("Visibility: private member variable not accessible via -> from outside struct", "[visibility][error]") {
    REQUIRE_THROWS(gen_jit_throws(R"SRC(
        module vis_test;
        struct S {
        private:
            x : int;
        public:
            S() : x(5) {}
        }
        test() : int {
            s : S;
            p : S* = &s;
            return p->x;   // must be rejected: private via ->
        }
    )SRC"));
}

TEST_CASE("Visibility: public member variable accessible via -> from outside struct", "[visibility][gen]") {
    auto jit = gen_jit(R"SRC(
        module vis_test;
        struct S {
        public:
            x : int;
            S() : x(42) {}
        }
        test() : int {
            s : S;
            p : S* = &s;
            return p->x;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    REQUIRE(test() == 42);
}

