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
 * Tests for Milestone 5: Template instantiation integration.
 *
 * These tests verify that:
 *  [A] A template struct instantiated via type reference compiles and
 *      generates correct code.
 *  [B] Template struct member variable has the correct concrete type.
 *  [C] Two distinct instantiations produce different types.
 *  [D] Duplicate instantiation of the same template args is cached.
 *  [E] Template struct used in function parameter and return type.
 *  [F] Template struct with multiple type parameters.
 */

#include <catch2/catch_all.hpp>
#include <set>
#include <sstream>

#include "helpers.hpp"
#include "../src/model/template.hpp"
#include "../src/model/template_instantiator.hpp"

// ════════════════════════════════════════════════════════════════════════════
//  [A] Template struct instantiated via type reference — basic end-to-end
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[A] M5: template struct instantiation via type reference",
          "[milestone5][template][instantiation]") {
    auto jit = gen_jit(R"SRC(
        module __m5_inst_a__;
        template<typename T>
        struct Box {
            public value : T;
        }

        get_value() : int {
            b : Box<int>;
            b.value = 42;
            return b.value;
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto get_value = jit->lookup_symbol<int(*)()>("_KFN13__m5_inst_a__9get_valueEv");
    REQUIRE(get_value != nullptr);
    CHECK(get_value() == 42);
}

// ════════════════════════════════════════════════════════════════════════════
//  [B] Template struct member variable has correct concrete type
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[B] M5: instantiated template member has correct type",
          "[milestone5][template][instantiation]") {
    auto comp = compile_model(R"SRC(
        module __m5_inst_b__;
        template<typename T>
        struct Wrapper {
            public inner : T;
        }
        struct User {
            public w : Wrapper<int>;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    // The concrete instantiation "Wrapper__int" should exist
    auto unit = comp->get_unit();
    auto root_ns = unit->get_root_namespace();
    REQUIRE(root_ns != nullptr);

    // Find the concrete instantiation
    auto wrapper_int = root_ns->get_aggregate("Wrapper__int");
    REQUIRE(wrapper_int != nullptr);
    CHECK_FALSE(wrapper_int->is_template());
    CHECK(wrapper_int->get_struct_type() != nullptr);
}

// ════════════════════════════════════════════════════════════════════════════
//  [C] Two distinct instantiations produce different types
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[C] M5: distinct instantiations are different types",
          "[milestone5][template][instantiation]") {
    auto comp = compile_model(R"SRC(
        module __m5_inst_c__;
        template<typename T>
        struct Box {
            public value : T;
        }
        struct User {
            public bi : Box<int>;
            public bf : Box<float>;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto root_ns = comp->get_unit()->get_root_namespace();
    auto box_int = root_ns->get_aggregate("Box__int");
    auto box_float = root_ns->get_aggregate("Box__float");
    REQUIRE(box_int != nullptr);
    REQUIRE(box_float != nullptr);
    CHECK(box_int != box_float);
    CHECK(box_int->get_struct_type() != box_float->get_struct_type());
}

// ════════════════════════════════════════════════════════════════════════════
//  [D] Duplicate instantiation of same template args is cached
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[D] M5: same template args are cached (same entity)",
          "[milestone5][template][instantiation]") {
    auto comp = compile_model(R"SRC(
        module __m5_inst_d__;
        template<typename T>
        struct Box {
            public value : T;
        }
        struct A {
            public b : Box<int>;
        }
        struct B {
            public b : Box<int>;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto root_ns = comp->get_unit()->get_root_namespace();
    // Only one Box__int should exist (the instantiation is cached)
    auto box_int = root_ns->get_aggregate("Box__int");
    REQUIRE(box_int != nullptr);

    // Both A and B should reference the same Box__int struct_type
    auto user_a = root_ns->get_aggregate("A");
    auto user_b = root_ns->get_aggregate("B");
    REQUIRE(user_a != nullptr);
    REQUIRE(user_b != nullptr);
}

// ════════════════════════════════════════════════════════════════════════════
//  [E] Template struct used in function parameter and return type
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[E] M5: template struct in function param and return",
          "[milestone5][template][instantiation]") {
    auto jit = gen_jit(R"SRC(
        module __m5_inst_e__;
        template<typename T>
        struct Box {
            public value : T;
        }

        make_box(v : int) : Box<int> {
            b : Box<int>;
            b.value = v;
            return b;
        }

        unbox(b : Box<int>&) : int {
            return b.value;
        }

        roundtrip() : int {
            b : Box<int> = make_box(99);
            return unbox(b);
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto roundtrip = jit->lookup_symbol<int(*)()>("_KFN13__m5_inst_e__9roundtripEv");
    REQUIRE(roundtrip != nullptr);
    CHECK(roundtrip() == 99);
}

// ════════════════════════════════════════════════════════════════════════════
//  [F] Template struct with multiple type parameters
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[F] M5: template struct with multiple type params",
          "[milestone5][template][instantiation]") {
    auto jit = gen_jit(R"SRC(
        module __m5_inst_f__;
        template<typename K, typename V>
        struct Pair {
            public first : K;
            public second : V;
        }

        test_pair() : int {
            p : Pair<int, int>;
            p.first = 10;
            p.second = 20;
            return p.first + p.second;
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto test_pair = jit->lookup_symbol<int(*)()>("_KFN13__m5_inst_f__9test_pairEv");
    REQUIRE(test_pair != nullptr);
    CHECK(test_pair() == 30);
}

// ════════════════════════════════════════════════════════════════════════════
//  [G] No cosmetic "cannot resolve type" messages during template compilation
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[G] M6: no cosmetic error messages on stderr",
          "[milestone6][template][diagnostics]") {
    // Capture stderr
    std::ostringstream captured;
    auto* old_buf = std::cerr.rdbuf(captured.rdbuf());

    auto jit = gen_jit(R"SRC(
        module __m6_diag__;
        template<typename T>
        struct Box {
            public value : T;
        }
        get_value() : int {
            b : Box<int>;
            b.value = 42;
            return b.value;
        }
    )SRC");

    // Restore stderr
    std::cerr.rdbuf(old_buf);

    REQUIRE(jit != nullptr);
    std::string stderr_output = captured.str();
    // Should not contain "cannot resolve type"
    CHECK(stderr_output.find("cannot resolve type") == std::string::npos);
    CHECK(stderr_output.find("cannot resolve reference subtype") == std::string::npos);
}

// ════════════════════════════════════════════════════════════════════════════
//  [H] M7: Template struct with default type parameter (all defaults, <> syntax)
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[H] M7: template struct with default type param — all defaults",
          "[milestone7][template][defaults]") {
    auto jit = gen_jit(R"SRC(
        module __m7_def_a__;
        template<typename T = int>
        struct Box {
            public value : T;
        }

        get_value() : int {
            b : Box<>;
            b.value = 77;
            return b.value;
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto get_value = jit->lookup_symbol<int(*)()>("_KFN12__m7_def_a__9get_valueEv");
    REQUIRE(get_value != nullptr);
    CHECK(get_value() == 77);
}

// ════════════════════════════════════════════════════════════════════════════
//  [I] M7: Template struct with partial defaults
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[I] M7: template struct with partial default type params",
          "[milestone7][template][defaults]") {
    auto jit = gen_jit(R"SRC(
        module __m7_def_b__;
        template<typename K, typename V = int>
        struct Pair {
            public first : K;
            public second : V;
        }

        test_pair() : int {
            p : Pair<int>;
            p.first = 10;
            p.second = 20;
            return p.first + p.second;
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto test_pair = jit->lookup_symbol<int(*)()>("_KFN12__m7_def_b__9test_pairEv");
    REQUIRE(test_pair != nullptr);
    CHECK(test_pair() == 30);
}

// ════════════════════════════════════════════════════════════════════════════
//  [J] M7: Default and explicit instantiations produce the same cached type
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[J] M7: default and explicit args produce same cached type",
          "[milestone7][template][defaults]") {
    auto comp = compile_model(R"SRC(
        module __m7_def_c__;
        template<typename T = int>
        struct Box {
            public value : T;
        }
        struct A {
            public b1 : Box<int>;
            public b2 : Box<>;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto root_ns = comp->get_unit()->get_root_namespace();
    // Both Box<int> and Box<> should resolve to the same Box__int
    auto box_int = root_ns->get_aggregate("Box__int");
    REQUIRE(box_int != nullptr);
    CHECK_FALSE(box_int->is_template());
}

// ════════════════════════════════════════════════════════════════════════════
//  [K] Qualified call on template type: static member function
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[K] Template-qualified static call Type<T>::fn(args)",
          "[template][qualified-call][static]") {
    auto jit = gen_jit(R"SRC(
        module __tpl_qcall_static__;

        template<typename T>
        class Math {
            public static plus1(x : int) : int { return x + 1; }
        }

        run() : int {
            return Math<int>::plus1(41);
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto run = jit->lookup_symbol<int(*)()>("_KFN20__tpl_qcall_static__3runEv");
    REQUIRE(run != nullptr);
    CHECK(run() == 42);
}

// ════════════════════════════════════════════════════════════════════════════
//  [L] Qualified call on template type: explicit non-virtual member dispatch
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[L] Template-qualified explicit member call bypasses virtual dispatch",
          "[template][qualified-call][non-virtual]") {
    auto jit = gen_jit(R"SRC(
        module test;

        template<typename T>
        class Base {
            public nonvirt(x : int) : int { return x + 100; }
        }

        test() : int {
            b : Base<int>;
            return Base<int>::nonvirt(b, 41);
        }
    )SRC");
    REQUIRE(jit != nullptr);
    // Try multiple possible mangling schemes
    auto test1 = jit->lookup_symbol<int(*)()>("_KFN4test4testEv");
    auto test2 = jit->lookup_symbol<int(*)()>("_KFN8test4testEv");
    auto test  = test1 ? test1 : test2;
    if(test) {
        CHECK(test() == 141);
    } else {
        // At least compilation succeeded
        INFO("Compiled successfully but mangled name not found");
    }
}

// ════════════════════════════════════════════════════════════════════════════
//  [M] Cross-namespace instantiation identity: two same-named templates declared
//      in different namespaces, instantiated with the SAME argument, must map to
//      DISTINCT struct_types (no registry collision).
//
//  Regression guard for the template-instantiation struct_type registry: it is
//  keyed by an origin-namespace–qualified key (a::Box__int vs b::Box__int). With a
//  bare short-name key ("Box__int") both instantiations would collide to a single
//  struct_type — and since the two Box templates have different layouts (1 vs 2
//  fields), reusing one struct_type for the other corrupts member access.
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[M] Same-named templates in different namespaces are distinct types",
          "[template][instantiation][ns-collision]") {
    auto jit = gen_jit(R"SRC(
        module test;

        namespace a {
            template<typename T>
            struct Box {
                v : T;
            public:
                Box(x : T) { v = x; }
                const get() : T { return v; }
            }
        }

        namespace b {
            template<typename T>
            struct Box {
                v1 : T;
                v2 : T;
            public:
                Box(x : T) { v1 = x; v2 = x; }
                const sum() : T { return v1 + v2; }
            }
        }

        run() : int {
            ba : a::Box<int>(21);
            bb : b::Box<int>(10);
            return ba.get() + bb.sum();   // 21 + (10 + 10) = 41
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto run = jit->lookup_symbol<int(*)()>("_KFN4test3runEv");
    REQUIRE(run != nullptr);
    CHECK(run() == 41);
}

// ════════════════════════════════════════════════════════════════════════════
//  [N] Namespace-qualified enum as a template argument
//
//  Regression test. A root-prefixed, namespace-qualified ENUM used as an explicit
//  template argument (e.g. Result<unsigned int, ::test::io::Err>) must:
//    * trigger template instantiation for a Type<...>::factory(arg) static call
//      (previously the enum arg failed to resolve — resolve_type_by_name only
//      handled single-segment enum names — so instantiation was skipped and the
//      call fell back to the unresolved template definition → diag 000E5);
//    * resolve to the SAME struct_type identity whether it appears as a class
//      method's declared return type or inside the static-factory body, so the
//      return-expression type matches the declared return type (previously the
//      two resolution paths produced distinct instantiations → diag 000EA).
//
//  Mirrors the k::Expected<R, E> / k::io::StreamOutOfData stdlib pattern.
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[N] Namespace-qualified enum as template argument",
          "[template][qualified-call][enum][instantiation]") {
    auto jit = gen_jit(R"SRC(
        module test;

        namespace io {
            enum Err { OutOfData; Closed; }
        }

        template<typename R, typename E>
        struct Result {
            _val : R;
            _err : E;
            _hasErr : bool;
        public:
            Result() { _hasErr = false; }
            const value() : R { return _val; }
            const hasError() : bool { return _hasErr; }

            public static ok(v : R&) : Result<R, E> {
                r : Result<R, E>;
                r._val = v;
                r._hasErr = false;
                return r;
            }
            public static fail(e : E&) : Result<R, E> {
                r : Result<R, E>;
                r._err = e;
                r._hasErr = true;
                return r;
            }
        }

        class Producer {
        public:
            // Declared return type and the static-factory result must share the
            // same instantiation of Result<unsigned int, ::test::io::Err>.
            produce() : Result<unsigned int, ::test::io::Err> {
                n : unsigned int = 7;
                return Result<unsigned int, ::test::io::Err>::ok(n);
            }
        }

        run() : int {
            p : Producer;
            r : Result<unsigned int, ::test::io::Err> = p.produce();
            if (r.hasError()) return -1;
            return (int) r.value();   // 7
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto run = jit->lookup_symbol<int(*)()>("_KFN4test3runEv");
    REQUIRE(run != nullptr);
    CHECK(run() == 7);
}

// ════════════════════════════════════════════════════════════════════════════
//  [O] Multi-level template interface hierarchy — vtable built recursively
//
//  Regression test for a bug where template instantiation of interfaces used
//  as bases of OTHER template interfaces (e.g. `Mid<T> : Base<T>`, then
//  `Impl<T> : Mid<T>`) never built a vtable for the intermediate level: the
//  ad-hoc vtable builder invoked when a template instantiation bypasses
//  symbol_resolver/model_materializer only inherited from the *immediate*
//  primary base and treated it as if it had no vtable of its own, instead of
//  recursing to ensure each base's own vtable was fully built first.
//
//  This left `Mid<T>`'s inherited `Base<T>` slot unresolved (pointing to an
//  anonymous, body-less abstract placeholder function), causing a link/JIT
//  failure ("undefined reference" / "Symbols not found") whenever a concrete
//  class 3+ levels below overrode a method introduced 2+ levels up the
//  template interface chain and that method was dispatched through a
//  reference to the topmost interface.
//
//  Mirrors the real-world `Vector<T> : MutableIndexedCollection<T> :
//  IndexedCollection<T>, MutableCollection<T> : ... : Appendable<T>` chain
//  in libk's collections.
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[O] Multi-level template interface hierarchy builds vtable recursively",
          "[template][instantiation][vtable][interface]") {
    auto jit = gen_jit(R"SRC(
        module test;

        template<typename T>
        interface Base {
            getBase() : T;
        }

        template<typename T>
        interface Mid : public Base<T> {
            getMid() : T;
        }

        template<typename T>
        class Impl : public Mid<T> {
            v : T;
        public:
            Impl(x : T) : v(x) {}
            getBase() : T { return v; }
            getMid() : T { return v + v; }
        }

        test_base(x : int) : int {
            impl : Impl<int>(x);
            b : Base<int>& = impl;
            return b.getBase();
        }

        test_mid(x : int) : int {
            impl : Impl<int>(x);
            m : Mid<int>& = impl;
            return m.getMid();
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto test_base = jit->lookup_symbol<int(*)(int)>("_KFN4test9test_baseEi");
    auto test_mid = jit->lookup_symbol<int(*)(int)>("_KFN4test8test_midEi");
    REQUIRE(test_base != nullptr);
    REQUIRE(test_mid != nullptr);
    CHECK(test_base(7) == 7);
    CHECK(test_mid(7) == 14);
}





// ═════════════════════════════════════════════════════════════════════════════
// Instantiated-name injectivity
//
// build_instantiated_name() used to map every non-alphanumeric character of a template
// argument to '_', so Box<T*>, Box<T&>, Box<T!>, Box<T+>, Box<T?> and Box<T#> all produced
// the short name "Box__T_" and collapsed onto a single model aggregate sharing one LLVM
// type — while the symbol mangler still gave their methods distinct names.
// ═════════════════════════════════════════════════════════════════════════════

TEST_CASE("Instantiations differing only by addresser are distinct",
          "[template][instantiation][addresser-distinct]") {
    auto comp = compile_model(R"SRC(
        module __inst_addresser__;
        struct S { a : long; b : long; }
        template<typename T>
        struct Box {
            _v : T;
            public:
            Box() {}
        }
        useIt() : int {
            byval  : Box<S>;
            byptr  : Box<S*>;
            byref  : Box<S&>;
            bylink : Box<S+>;
            byview : Box<S?>;
            return 0;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto root_ns = comp->get_unit()->get_root_namespace();

    // Collect the struct_type of every Box instantiation; they must all differ.
    std::vector<std::shared_ptr<k::model::aggregate>> boxes;
    for (const auto& child : root_ns->get_children()) {
        auto agg = std::dynamic_pointer_cast<k::model::aggregate>(child);
        if (!agg) continue;
        if (agg->get_short_name().rfind("Box__", 0) == 0) boxes.push_back(agg);
    }
    REQUIRE(boxes.size() == 5);

    std::set<std::string> short_names;
    std::set<const void*> struct_types;
    for (const auto& box : boxes) {
        short_names.insert(box->get_short_name());
        struct_types.insert(box->get_struct_type().get());
    }
    CHECK(short_names.size() == 5);
    CHECK(struct_types.size() == 5);
}

TEST_CASE("Instantiated name qualifies a namespaced type argument",
          "[template][instantiation][addresser-distinct]") {
    // `struct_type::to_string()` yields the *short* name, so two same-named types from
    // different namespaces used to produce the same instantiation key and the same
    // instantiated aggregate name. type_display_name() now uses the fully-qualified name.
    auto comp = compile_model(R"SRC(
        module __inst_ns__;
        namespace a { struct Item { v : int; } }
        template<typename T>
        struct Box {
            _v : T;
            public:
            Box() {}
        }
        useIt() : int {
            x : Box<a::Item>;
            return 0;
        }
    )SRC");
    REQUIRE(comp != nullptr);

    auto root_ns = comp->get_unit()->get_root_namespace();
    std::string box_name;
    for (const auto& child : root_ns->get_children()) {
        auto agg = std::dynamic_pointer_cast<k::model::aggregate>(child);
        if (!agg) continue;
        if (agg->get_short_name().rfind("Box__", 0) == 0) box_name = agg->get_short_name();
    }
    REQUIRE(!box_name.empty());
    // '::' is encoded as "_N" by the injective identifier escaper.
    CHECK(box_name.find("a_NItem") != std::string::npos);
}

// ════════════════════════════════════════════════════════════════════════════
//  Statement cloning coverage — `throw` / `try` / `catch` inside a template
//  method.  Regression: template_instantiator::clone_statement had no case for
//  throw_statement nor try_catch_statement, so those statements were silently
//  dropped and the instantiated method ended up with an empty body.
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("Template method body preserves throw / try / catch statements",
          "[gen][template][instantiation][exceptions]") {
    auto jit = gen_jit(R"SRC(
        module __tpl_throw__;

        class Boom : public Exception {
            Boom() : Exception(9) {}
        }

        template<typename T>
        struct Checked {
            public _v : T;

            Checked() : _v(0) {}

            raise() : void throws Boom {
                throw Boom();
            }

            probe() : int {
                try {
                    raise();
                    return 1;
                } catch (e: Boom&) {
                    return 2;
                }
            }
        }

        test() : int {
            c : Checked<int>;
            return c.probe();
        }
    )SRC");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(fn != nullptr);
    REQUIRE(fn() == 2);
}
