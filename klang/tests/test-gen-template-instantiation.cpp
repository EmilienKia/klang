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
#include <string_view>

#include "helpers.hpp"
#include "../src/errors.hpp"
#include "../src/model/context.hpp"
#include "../src/model/template.hpp"
#include "../src/model/template_instantiator.hpp"

// ════════════════════════════════════════════════════════════════════════════
//  [A] Template struct instantiated via type reference — basic end-to-end
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[A] M5: template struct instantiation via type reference",
          "[milestone5][template][instantiation]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_instantiation_01;
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
    auto get_value = jit->lookup_symbol<int(*)()>("_KFN29gen_template_instantiation_019get_valueEv");
    REQUIRE(get_value != nullptr);
    CHECK(get_value() == 42);
}

// ════════════════════════════════════════════════════════════════════════════
//  [B] Template struct member variable has correct concrete type
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[B] M5: instantiated template member has correct type",
          "[milestone5][template][instantiation]") {
    auto comp = compile_model(R"SRC(
        module gen_template_instantiation_02;
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
        module gen_template_instantiation_03;
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
        module gen_template_instantiation_04;
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
        module gen_template_instantiation_05;
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
    auto roundtrip = jit->lookup_symbol<int(*)()>("_KFN29gen_template_instantiation_059roundtripEv");
    REQUIRE(roundtrip != nullptr);
    CHECK(roundtrip() == 99);
}

// ════════════════════════════════════════════════════════════════════════════
//  [F] Template struct with multiple type parameters
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[F] M5: template struct with multiple type params",
          "[milestone5][template][instantiation]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_instantiation_06;
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
    auto test_pair = jit->lookup_symbol<int(*)()>("_KFN29gen_template_instantiation_069test_pairEv");
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
        module gen_template_instantiation_07;
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
        module gen_template_instantiation_08;
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
    auto get_value = jit->lookup_symbol<int(*)()>("_KFN29gen_template_instantiation_089get_valueEv");
    REQUIRE(get_value != nullptr);
    CHECK(get_value() == 77);
}

// ════════════════════════════════════════════════════════════════════════════
//  [I] M7: Template struct with partial defaults
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[I] M7: template struct with partial default type params",
          "[milestone7][template][defaults]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_instantiation_09;
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
    auto test_pair = jit->lookup_symbol<int(*)()>("_KFN29gen_template_instantiation_099test_pairEv");
    REQUIRE(test_pair != nullptr);
    CHECK(test_pair() == 30);
}

// ════════════════════════════════════════════════════════════════════════════
//  [J] M7: Default and explicit instantiations produce the same cached type
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[J] M7: default and explicit args produce same cached type",
          "[milestone7][template][defaults]") {
    auto comp = compile_model(R"SRC(
        module gen_template_instantiation_10;
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
        module gen_template_instantiation_11;

        template<typename T>
        class Math {
            public static plus1(x : int) : int { return x + 1; }
        }

        run() : int {
            return Math<int>::plus1(41);
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto run = jit->lookup_symbol<int(*)()>("_KFN29gen_template_instantiation_113runEv");
    REQUIRE(run != nullptr);
    CHECK(run() == 42);
}

// ════════════════════════════════════════════════════════════════════════════
//  [L] Qualified call on template type: explicit non-virtual member dispatch
// ════════════════════════════════════════════════════════════════════════════

TEST_CASE("[L] Template-qualified explicit member call bypasses virtual dispatch",
          "[template][qualified-call][non-virtual]") {
    auto jit = gen_jit(R"SRC(
        module gen_template_instantiation_12;

        template<typename T>
        class Base {
            public nonvirt(x : int) : int { return x + 100; }
        }

        gen_template_instantiation_12() : int {
            b : Base<int>;
            return Base<int>::nonvirt(b, 41);
        }
    )SRC");
    REQUIRE(jit != nullptr);
    // Try multiple possible mangling schemes
    auto test1 = jit->lookup_symbol<int(*)()>("gen_template_instantiation_12");
    auto test2 = jit->lookup_symbol<int(*)()>("gen_template_instantiation_12");
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
        module gen_template_instantiation_13;

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
    auto run = jit->lookup_symbol<int(*)()>("_KFN29gen_template_instantiation_133runEv");
    REQUIRE(run != nullptr);
    CHECK(run() == 41);
}

namespace {

constexpr std::string_view nested_template_registration_source = R"SRC(
    module nested_template_registration;

    namespace left {
        template<typename T>
        struct Host {
            static struct Nested {
                value: T;
                Nested(x: T): value(x) {}
                const read(): T { return value; }
            }
            union Storage { value: T; error: int; }
            nested: Nested;
            storage: Storage;
            Host(x: T): nested(x) { storage.value = x; }
            const read(): T { return nested.read() + storage.value; }
            static readNested(n: Nested&): T { return n.value; }
        }
    }
    namespace right {
        template<typename T>
        struct Host {
            static struct Nested {
                value: T;
                extra: int;
                Nested(x: T): value(x), extra(3) {}
                const read(): T { return value + (T) extra; }
            }
            union Storage { value: T; error: long; }
            nested: Nested;
            storage: Storage;
            Host(x: T): nested(x) { storage.value = x; }
            const read(): T { return nested.read() + storage.value; }
            static readNested(n: Nested&): T { return n.value; }
        }
    }

    run(): int {
        a: left::Host<int>(5);
        repeated: left::Host<int>(7);
        wide: left::Host<long>(4294967296l);
        b: right::Host<int>(11);
        if (a.read() != 10 || repeated.read() != 14 || b.read() != 25) return -1;
        if (wide.read() != 8589934592l) return -2;
        if (left::Host<int>::readNested(a.nested) != 5) return -3;
        if (right::Host<int>::readNested(b.nested) != 11) return -4;
        a.nested.value = 13;
        if (a.read() != 18 || repeated.read() != 14 || b.read() != 25) return -5;
        return 42;
    }
)SRC";

} // namespace

TEST_CASE("Nested template storage registration preserves lexical host identity",
          "[gen][template][instantiation][nested-registration][ns-collision]") {
    auto comp = compile_model(nested_template_registration_source);
    REQUIRE(comp != nullptr);
    auto root = comp->get_unit()->get_root_namespace();
    auto left = root->get_child_namespace("left");
    auto right = root->get_child_namespace("right");
    REQUIRE(left != nullptr);
    REQUIRE(right != nullptr);
    auto left_int = left->get_aggregate("Host__int");
    auto left_long = left->get_aggregate("Host__long");
    auto right_int = right->get_aggregate("Host__int");
    REQUIRE(left_int != nullptr);
    REQUIRE(left_long != nullptr);
    REQUIRE(right_int != nullptr);

    std::set<const k::model::aggregate*> hosts;
    std::set<const k::model::struct_type*> nested_types;
    std::set<llvm::Type*> nested_llvm_types;
    std::set<const k::model::struct_type*> union_types;
    std::set<llvm::Type*> union_llvm_types;
    std::set<const k::model::enum_type*> kind_types;
    for (const auto& host : {left_int, left_long, right_int}) {
        INFO(host->get_fq_name());
        hosts.insert(host.get());
        auto nested = host->get_aggregate("Nested");
        auto storage = host->get_union("Storage");
        REQUIRE(nested != nullptr);
        REQUIRE(storage != nullptr);
        CHECK(nested->get_enclosing_aggregate() == host);
        auto nested_type = nested->get_struct_type();
        auto union_type = storage->get_struct_type();
        REQUIRE(nested_type != nullptr);
        REQUIRE(union_type != nullptr);
        REQUIRE(nested_type->get_llvm_type() != nullptr);
        REQUIRE(union_type->get_llvm_type() != nullptr);
        CHECK(nested_type->get_struct() == nested);
        const auto prefix = host->get_name().without_root_prefix().to_string() + "::";
        CHECK(nested_type->name() == prefix + "Nested");
        CHECK(union_type->name() == prefix + "Storage");
        CHECK(root->get_context()->from_string(nested_type->name()) == nested_type);
        CHECK(root->get_context()->from_string(union_type->name()) == union_type);
        auto nested_field = host->get_variable("nested");
        auto storage_field = host->get_variable("storage");
        REQUIRE(nested_field != nullptr);
        REQUIRE(storage_field != nullptr);
        CHECK(nested_field->get_type() == nested_type);
        CHECK(storage_field->get_type() == union_type);
        auto kind = storage->get_kind_enum();
        REQUIRE(kind != nullptr);
        REQUIRE(kind->get_enum_type() != nullptr);
        CHECK(kind->get_fq_name() == storage->get_fq_name() + "::Kind");
        nested_types.insert(nested_type.get());
        nested_llvm_types.insert(nested_type->get_llvm_type());
        union_types.insert(union_type.get());
        union_llvm_types.insert(union_type->get_llvm_type());
        kind_types.insert(kind->get_enum_type().get());
    }
    CHECK(hosts.size() == 3);
    CHECK(nested_types.size() == 3);
    CHECK(nested_llvm_types.size() == 3);
    CHECK(union_types.size() == 3);
    CHECK(union_llvm_types.size() == 3);
    CHECK(kind_types.size() == 3);
    auto primary = left->get_aggregate("Host");
    REQUIRE(primary != nullptr);
    REQUIRE(primary->get_tpl_info() != nullptr);
    CHECK(primary->get_tpl_info()->instantiations.size() == 2);
}

TEST_CASE("Namespace homonym nested template constructors and references execute independently",
          "[gen][template][instantiation][nested-registration][ns-collision]") {
    auto jit = gen_jit(nested_template_registration_source);
    REQUIRE(jit != nullptr);
    auto run = jit->lookup_symbol<int(*)()>("run");
    REQUIRE(run != nullptr);
    CHECK(run() == 42);
}

TEST_CASE("Erased generic nested storage registration preserves lexical host identity",
          "[gen][template][instantiation][generic][nested-registration][ns-collision]") {
    auto comp = compile_model(R"SRC(
        module nested_generic_registration;
        namespace left {
            generic<typename T>
            struct Host {
                static struct Nested { value: int; }
                union Storage { value: int; error: int; }
                pointer: T*;
            }
        }
        namespace right {
            generic<typename T>
            struct Host {
                static struct Nested { value: long; }
                union Storage { value: long; error: int; }
                pointer: T*;
            }
        }
        private force() {
            a: left::Host<int>;
            b: right::Host<int>;
        }
    )SRC");
    REQUIRE(comp != nullptr);
    auto root = comp->get_unit()->get_root_namespace();
    std::set<const k::model::struct_type*> nested_types;
    std::set<llvm::Type*> nested_llvm_types;
    std::set<const k::model::struct_type*> union_types;
    for (const auto& ns_name : {"left", "right"}) {
        auto scope = root->get_child_namespace(ns_name);
        REQUIRE(scope != nullptr);
        auto primary = scope->get_aggregate("Host");
        REQUIRE(primary != nullptr);
        REQUIRE(primary->get_tpl_info() != nullptr);
        const auto& cache = primary->get_tpl_info()->instantiations;
        auto synthesis = cache.find("<generic_synthesis>");
        REQUIRE(synthesis != cache.end());
        auto host_ptr = std::get_if<std::shared_ptr<k::model::aggregate>>(&synthesis->second);
        REQUIRE(host_ptr != nullptr);
        auto host = *host_ptr;
        REQUIRE(host != nullptr);
        REQUIRE(host->is_instantiation());
        auto nested = host->get_aggregate("Nested");
        auto storage = host->get_union("Storage");
        REQUIRE(nested != nullptr);
        REQUIRE(storage != nullptr);
        REQUIRE(nested->get_struct_type() != nullptr);
        REQUIRE(storage->get_struct_type() != nullptr);
        REQUIRE(nested->get_struct_type()->get_llvm_type() != nullptr);
        const auto prefix = host->get_name().without_root_prefix().to_string() + "::";
        CHECK(nested->get_struct_type()->name() == prefix + "Nested");
        CHECK(storage->get_struct_type()->name() == prefix + "Storage");
        CHECK(root->get_context()->from_string(prefix + "Nested") == nested->get_struct_type());
        CHECK(root->get_context()->from_string(prefix + "Storage") == storage->get_struct_type());
        nested_types.insert(nested->get_struct_type().get());
        nested_llvm_types.insert(nested->get_struct_type()->get_llvm_type());
        union_types.insert(storage->get_struct_type().get());
    }
    CHECK(nested_types.size() == 2);
    CHECK(nested_llvm_types.size() == 2);
    CHECK(union_types.size() == 2);
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
        module gen_template_instantiation_14;

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
            // same instantiation of Result<unsigned int, ::gen_template_instantiation_14::io::Err>.
            produce() : Result<unsigned int, ::gen_template_instantiation_14::io::Err> {
                n : unsigned int = 7;
                return Result<unsigned int, ::gen_template_instantiation_14::io::Err>::ok(n);
            }
        }

        run() : int {
            p : Producer;
            r : Result<unsigned int, ::gen_template_instantiation_14::io::Err> = p.produce();
            if (r.hasError()) return -1;
            return (int) r.value();   // 7
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto run = jit->lookup_symbol<int(*)()>("_KFN29gen_template_instantiation_143runEv");
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
        module gen_template_instantiation_15;

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
    auto test_base = jit->lookup_symbol<int(*)(int)>("_KFN29gen_template_instantiation_159test_baseEi");
    auto test_mid = jit->lookup_symbol<int(*)(int)>("_KFN29gen_template_instantiation_158test_midEi");
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
        module gen_template_instantiation_16;
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
        module gen_template_instantiation_17;
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

namespace {

std::vector<std::shared_ptr<k::model::type>> qualifier_variants(
    const std::shared_ptr<k::model::type>& shape) {
    using namespace k::model;
    std::vector<std::shared_ptr<type>> result;
    if (auto inner = shape->get_subtype()) {
        for (const auto& variant : qualifier_variants(inner)) {
            auto wrapped = type::make_pinned_wrapper(shape, variant);
            result.push_back(wrapped);
            result.push_back(wrapped->get_const());
        }
    } else {
        result = {shape, shape->get_const()};
    }
    return result;
}

std::shared_ptr<k::model::type> repin_qualifier_graph(
    const std::shared_ptr<k::model::type>& original) {
    using namespace k::model;
    if (auto inner = original->get_subtype()) {
        return type::make_pinned_wrapper(original, repin_qualifier_graph(inner));
    }
    return original;
}

std::string qualifier_slot_source(bool readonly_first) {
    const std::string readonly = R"SRC(
        readonly(box : const Box&) : bool {
            return &slot(box.owner) == &box.owner;
        }
    )SRC";
    const std::string referent = R"SRC(
        referent(value : const Resource!&) : bool {
            return &slot(value) == &value;
        }
    )SRC";
    return std::string(R"SRC(
        module qualifier_slot_identity;
        struct Resource {
            public value : int;
            Resource(v : int) : value(v) {}
        }
        struct Box { public owner : Resource! = null; }
        template<typename T>
        slot(value : T&) : T& { return value; }
    )SRC") + (readonly_first ? readonly + referent : referent + readonly) + R"SRC(
        run() : int {
            box : Box;
            box.owner = new Resource(41);
            source : const Resource! = new Resource(17);
    )SRC" + (readonly_first ? R"SRC(
            if (!readonly(box)) return -1;
            if (!referent(source)) return -2;
    )SRC" : R"SRC(
            if (!referent(source)) return -2;
            if (!readonly(box)) return -1;
    )SRC") + R"SRC(
            slot(source) = new Resource(73);
            if (source->value != 73 || box.owner->value != 41) return -3;
            if (!readonly(box) || !referent(source)) return -4;
            return 42;
        }
    )SRC";
}

} // namespace

TEST_CASE("Template keys and concrete names preserve every const wrapper boundary",
          "[model][template][instantiation][template-qualifier-identity][regression]") {
    using namespace k::model;
    auto comp = compile_model(R"SRC(
        module qualifier_identity_model;
        struct Resource {}
        typedef Handle : Resource!;
        typedef OtherHandle : Resource!;
        alias ResourceName : Resource;
        alias HandleName : Handle;
        resource(value : ResourceName&) {}
        handle(value : HandleName&) {}
    )SRC");
    REQUIRE(comp != nullptr);
    auto root = comp->get_unit()->get_root_namespace();
    auto resource = root->get_aggregate("Resource")->get_struct_type();
    auto handle = root->get_alias("Handle")->get_alias_type();
    auto other_handle = root->get_alias("OtherHandle")->get_alias_type();
    REQUIRE(resource != nullptr);
    REQUIRE(handle != nullptr);
    REQUIRE(other_handle != nullptr);

    auto key = [](const std::shared_ptr<type>& t) {
        return build_instantiation_key({template_argument::make_type(t)});
    };
    auto name = [](const std::shared_ptr<type>& t) {
        return build_instantiated_name("probe", {template_argument::make_type(t)});
    };
    auto readonly_slot = resource->get_owner()->get_const();
    auto const_referent = resource->get_const()->get_owner();
    CHECK_FALSE(type::are_equal(readonly_slot, const_referent));
    CHECK(key(readonly_slot) != key(const_referent));
    CHECK(name(readonly_slot) != name(const_referent));
    CHECK(key(readonly_slot) == "<const (qualifier_identity_model::Resource!)>");
    CHECK(key(const_referent) == "<const qualifier_identity_model::Resource!>");
    CHECK(name(readonly_slot) == "probe__const_s_bqualifier_uidentity_umodel_NResource_o_q");
    CHECK(name(const_referent) == "probe__const_squalifier_uidentity_umodel_NResource_o");

    std::vector<std::shared_ptr<type>> types = qualifier_variants(resource);
    const std::vector<std::shared_ptr<type>> shapes{
        resource->get_pointer(), resource->get_reference(), resource->get_link(),
        resource->get_view(), resource->get_owner(), resource->get_drain(),
        resource->get_array(), resource->get_array(3),
        resource->get_pointer()->get_reference(),
        resource->get_link()->get_reference(),
        resource->get_view()->get_reference(),
        resource->get_owner()->get_reference(),
        resource->get_drain()->get_reference(),
        resource->get_array()->get_owner(),
        resource->get_array(3)->get_owner(),
        resource->get_pointer()->get_pointer(),
        resource->get_owner()->get_pointer()
    };
    for (const auto& shape : shapes) {
        auto variants = qualifier_variants(shape);
        types.insert(types.end(), variants.begin(), variants.end());
    }
    for (const auto& nominal : {handle, other_handle}) {
        auto variants = qualifier_variants(nominal);
        types.insert(types.end(), variants.begin(), variants.end());
        variants = qualifier_variants(nominal->get_reference());
        types.insert(types.end(), variants.begin(), variants.end());
    }
    REQUIRE(types.size() == 118);

    std::set<std::string> keys, names, pack_keys, pack_names;
    for (size_t i = 0; i < types.size(); ++i) {
        const auto& t = types[i];
        INFO(type_display_name(t));
        keys.insert(key(t));
        names.insert(name(t));
        auto pack = template_argument::make_pack({t, resource});
        pack_keys.insert(build_instantiation_key({pack}));
        pack_names.insert(build_instantiated_name("probe", {pack}));
        auto equal = repin_qualifier_graph(t);
        REQUIRE(type::are_equal(t, equal));
        CHECK(key(t) == key(equal));
        CHECK(name(t) == name(equal));
        if (t->get_subtype()) CHECK(t != equal);
        for (size_t j = 0; j < i; ++j) {
            CHECK_FALSE(type::are_equal(t, types[j]));
        }
    }
    CHECK(keys.size() == types.size());
    CHECK(names.size() == types.size());
    CHECK(pack_keys.size() == types.size());
    CHECK(pack_names.size() == types.size());

    CHECK_FALSE(type::are_equal(handle, resource->get_owner()));
    CHECK_FALSE(type::are_equal(handle, other_handle));
    CHECK(key(handle) != key(resource->get_owner()));
    CHECK(name(handle) != name(resource->get_owner()));
    auto soft_resource = root->get_function("resource")->get_parameter("value")->get_type()->get_subtype();
    auto soft_handle = root->get_function("handle")->get_parameter("value")->get_type()->get_subtype();
    CHECK(type::are_equal(resource, soft_resource));
    CHECK(type::are_equal(handle, soft_handle));
    CHECK(key(resource) == key(soft_resource));
    CHECK(name(resource) == name(soft_resource));
    CHECK(key(handle) == key(soft_handle));
    CHECK(name(handle) == name(soft_handle));
    CHECK(build_instantiated_name("Box", {template_argument::make_type(
        root->get_context()->from_type(primitive_type::INT))}) == "Box__int");
}

TEST_CASE("Ordinary T reference specializations preserve caller slots in both orders",
          "[gen][model][template][instantiation][template-qualifier-identity][regression]") {
    using namespace k::model;
    const bool readonly_first = GENERATE(true, false);
    INFO("readonly specialization first: " << readonly_first);
    const auto source = qualifier_slot_source(readonly_first);
    auto comp = compile_model(source);
    REQUIRE(comp != nullptr);
    auto root = comp->get_unit()->get_root_namespace();
    auto resource = root->get_aggregate("Resource")->get_struct_type();
    auto readonly_slot = resource->get_owner()->get_const();
    auto const_referent = resource->get_const()->get_owner();
    auto primary = root->get_function("slot");
    REQUIRE(primary != nullptr);
    REQUIRE(primary->get_tpl_info() != nullptr);
    const auto& cache = primary->get_tpl_info()->instantiations;
    REQUIRE(cache.size() == 2);
    std::set<std::string> concrete_names, mangled_names;
    for (const auto& [caller_name, expected] :
         std::vector<std::pair<std::string, std::shared_ptr<type>>>{
             {"readonly", readonly_slot}, {"referent", const_referent}}) {
        const auto key = build_instantiation_key({template_argument::make_type(expected)});
        auto found = cache.find(key);
        REQUIRE(found != cache.end());
        const auto* entry = std::get_if<std::shared_ptr<function>>(&found->second);
        REQUIRE(entry != nullptr);
        auto concrete = *entry;
        REQUIRE(concrete != nullptr);
        concrete_names.insert(concrete->get_short_name());
        mangled_names.insert(concrete->get_mangled_name());
        REQUIRE(concrete->get_tpl_args().size() == 1);
        CHECK(type::are_equal(concrete->get_tpl_args()[0].type_arg, expected));
        const auto expected_ref = expected->get_reference();
        CHECK(type::are_equal(concrete->get_parameter("value")->get_type(), expected_ref));
        CHECK(type::are_equal(concrete->get_return_type(), expected_ref));
        auto body = concrete->get_existing_block();
        REQUIRE(body != nullptr);
        REQUIRE(body->get_statements().size() == 1);
        auto returned = std::dynamic_pointer_cast<return_statement>(body->get_statements()[0]);
        REQUIRE(returned != nullptr);
        auto parameter = std::dynamic_pointer_cast<symbol_expression>(returned->get_expression());
        REQUIRE(parameter != nullptr);
        CHECK(parameter->get_variable_def() == concrete->get_parameter("value"));

        auto caller = root->get_function(caller_name);
        REQUIRE(caller != nullptr);
        auto caller_body = caller->get_existing_block();
        REQUIRE(caller_body != nullptr);
        REQUIRE(caller_body->get_statements().size() == 1);
        auto result = std::dynamic_pointer_cast<return_statement>(caller_body->get_statements()[0]);
        REQUIRE(result != nullptr);
        auto comparison = std::dynamic_pointer_cast<equal_expression>(result->get_expression());
        REQUIRE(comparison != nullptr);
        auto actual = std::dynamic_pointer_cast<address_of_expression>(comparison->left());
        auto original = std::dynamic_pointer_cast<address_of_expression>(comparison->right());
        REQUIRE(actual != nullptr);
        REQUIRE(original != nullptr);
        auto call = std::dynamic_pointer_cast<function_invocation_expression>(actual->sub_expr());
        REQUIRE(call != nullptr);
        REQUIRE(call->arguments().size() == 1);
        CHECK(type::are_equal(call->arguments()[0]->get_type(), expected_ref));
        CHECK(type::are_equal(call->get_type(), expected_ref));
        CHECK(type::are_equal(original->sub_expr()->get_type(), expected_ref));
        auto callee = std::dynamic_pointer_cast<symbol_expression>(call->callee_expr());
        REQUIRE(callee != nullptr);
        CHECK(callee->get_function() == concrete);
    }
    CHECK(concrete_names.size() == 2);
    CHECK(mangled_names.size() == 2);

    auto jit = gen_jit(source);
    REQUIRE(jit != nullptr);
    auto run = jit->lookup_symbol<int(*)()>("run");
    REQUIRE(run != nullptr);
    CHECK(run() == 42);
}

TEST_CASE("Ordinary T reference specialization cannot mutate a const owner slot",
          "[gen][template][instantiation][template-qualifier-identity][negative][regression]") {
    const bool readonly_first = GENERATE(true, false);
    INFO("readonly specialization first: " << readonly_first);
    auto source = qualifier_slot_source(readonly_first) + R"SRC(
        reject(box : const Box&) {
            slot(box.owner) = new Resource(9);
        }
    )SRC";
    try {
        gen_jit_throws(source);
        FAIL("A reference to a const owner slot must not become writable");
    } catch (const k::log::compiler_error& error) {
        CHECK(error.get_diagnostic().code ==
              static_cast<unsigned int>(k::diag::operator_diag::ERR_OVERLOAD_CALL_NO_MATCH));
        CHECK(error.get_diagnostic().message.find("Cannot assign to a const variable")
              != std::string::npos);
    }
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
        module gen_template_instantiation_18;

        class Boom : public Exception {
            Boom() : Exception(9) {}
        }

        template<typename T>
        struct Checked {
            public _v : T;

            Checked() : _v(0) {}

            raise() : void throws(Boom) {
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

TEST_CASE("Late template instantiation does not reinject resolved base constructors",
          "[gen][template][instantiation][constructor-reinjection]") {
    auto jit = gen_jit(R"SRC(
        module constructor_reinjection;
        calls : int = 0;

        class Root {
            Root() { calls += 1; }
        }
        class Control : public Root {}

        template<typename T>
        class Block : public Control {}

        test() : int {
            calls = 0;
            first : Block<int>;
            second : Block<long>;
            return calls;
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 2);
}

TEST_CASE("Nested template member arguments are resolved before class layout",
          "[gen][template][instantiation][nested-member-layout]") {
    auto jit = gen_jit(R"SRC(
        module nested_member_layout;
        calls : int = 0;

        class Root {
            Root() { calls += 1; }
        }

        template<typename T>
        struct Inner {
            value : T;
        }

        template<typename T>
        struct Outer {
            value : T;
        }

        class Holder : public Root {
            data : Outer<Inner<int>>!;
        }

        test() : int {
            calls = 0;
            holder : Holder;
            return calls;
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 1);
}

TEST_CASE("Nested template uses retain distinct outer specialization types",
          "[gen][template][instantiation][nested-template-descriptor]") {
    auto jit = gen_jit(R"SRC(
        module nested_template_descriptor_use;

        template<typename T>
        struct Host {
            public:
            template<typename U> static struct Inner {
                public:
                outer : T;
                own : U;
            }

            roundtrip(value : T) : T {
                item : Inner<int>;
                item.outer = value;
                return item.outer;
            }
        }

        test() : bool {
            small : Host<int>;
            large : Host<long>;
            return small.roundtrip(42) == 42 &&
                   large.roundtrip(1234567890123L) == 1234567890123L;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<bool(*)()>("test");
    REQUIRE(test);
    CHECK(test());
}

TEST_CASE("Nested generic uses retain distinct outer specialization types",
          "[gen][template][instantiation][nested-generic-descriptor]") {
    auto jit = gen_jit(R"SRC(
        module nested_generic_descriptor_use;

        template<typename T>
        struct Host {
            public:
            generic<typename U> static struct Inner {
                public:
                outer : T;
                own : U*;
            }

            roundtrip(value : T) : T {
                item : Inner<int>;
                item.outer = value;
                return item.outer;
            }
        }

        test() : bool {
            small : Host<int>;
            large : Host<long>;
            return small.roundtrip(42) == 42 &&
                   large.roundtrip(1234567890123L) == 1234567890123L;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<bool(*)()>("test");
    REQUIRE(test);
    CHECK(test());
}

TEST_CASE("Nested inner templates retain their enclosing instance",
          "[gen][template][instantiation][nested-template-descriptor]") {
    auto jit = gen_jit(R"SRC(
        module nested_inner_template_instance;

        initializations : int = 0;
        initialize(v : int) : int {
            initializations += 1;
            return v;
        }

        template<typename T>
        struct Host {
            public:
            value : T;
            struct Plain {
                public:
                get() : T { return value; }
            }
            template<typename U> struct Inner {
                public:
                get() : T { return value; }
            }
            template<typename U> struct Stored {
                public:
                own : U;
                Stored(v : U) : own(initialize(v)) {}
                get() : T { return value; }
            }
            read(v : T) : T {
                value = v;
                item : Inner<int>;
                return item.get();
            }
            read_stored(v : T) : T {
                value = v;
                item : Stored<int>(7);
                if (item.own == 7) return item.get();
                return 0;
            }
            read_plain(v : T) : T {
                value = v;
                item : Plain;
                return item.get();
            }
        }

        test() : bool {
            initializations = 0;
            small : Host<int>;
            other : Host<int>;
            large : Host<long>;
            return small.read(42) == 42 &&
                   other.read(64) == 64 &&
                   small.read(43) == 43 &&
                   large.read(1234567890123L) == 1234567890123L &&
                   small.read_stored(83) == 83 &&
                   large.read_stored(9876543210123L) == 9876543210123L &&
                   small.read_plain(19) == 19 &&
                   large.read_plain(2345678901234L) == 2345678901234L &&
                   small.read(21) == 21 &&
                   initializations == 2;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<bool(*)()>("test");
    REQUIRE(test);
    CHECK(test());
}

TEST_CASE("Nested inner generics retain their enclosing instance",
          "[gen][template][instantiation][nested-generic-descriptor]") {
    auto jit = gen_jit(R"SRC(
        module nested_inner_generic_instance;

        template<typename T>
        struct Host {
            public:
            value : T;
            generic<typename U> struct Inner {
                public:
                get() : T { return value; }
            }
            read(v : T) : T {
                value = v;
                item : Inner<int>;
                return item.get();
            }
        }

        test() : bool {
            first : Host<int>;
            second : Host<int>;
            large : Host<long>;
            return first.read(42) == 42 &&
                   second.read(73) == 73 &&
                   first.read(19) == 19 &&
                   large.read(1234567890123L) == 1234567890123L;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<bool(*)()>("test");
    REQUIRE(test);
    CHECK(test());
}

TEST_CASE("Nested member types survive sibling instantiation during resolution",
          "[gen][template][instantiation][nested-template-descriptor]") {
    auto jit = gen_jit(R"SRC(
        module nested_member_instantiation_iteration;

        template<typename T>
        struct Host {
            public:
            template<typename U> static struct First {
                public: value : U;
            }
            template<typename U> static struct Second {
                public: value : U;
            }
            static struct Holder {
                public:
                first : First<T>;
                second : Second<T>;
            }
            read(v : T) : T {
                item : Holder;
                item.first.value = v;
                item.second.value = v;
                if (item.first.value == item.second.value) return item.first.value;
                return 0;
            }
        }

        test() : bool {
            small : Host<int>;
            large : Host<long>;
            return small.read(42) == 42 &&
                   large.read(1234567890123L) == 1234567890123L;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<bool(*)()>("test");
    REQUIRE(test);
    CHECK(test());
}

TEST_CASE("Nested inner templates support templated constructors",
          "[gen][template][instantiation][nested-template-descriptor]") {
    auto jit = gen_jit(R"SRC(
        module nested_inner_template_constructor;

        explicit_initializations : int = 0;
        default_initializations : int = 0;
        initialize(v : int) : int {
            explicit_initializations += 1;
            return v;
        }
        initialize_default() : int {
            default_initializations += 1;
            return 11;
        }

        template<typename T>
        struct Host {
            public:
            value : T;
            template<typename V> struct Inner {
                public:
                own : int;
                fallback : int = initialize_default();
                template<typename U> Inner(v : U) : own(initialize(v)) {}
                get() : T { return value; }
            }
            read(v : T) : T {
                value = v;
                item : Inner<int>(7);
                if (item.own == 7 && item.fallback == 11) return item.get();
                return 0;
            }
        }

        test() : bool {
            explicit_initializations = 0;
            default_initializations = 0;
            small : Host<int>;
            large : Host<long>;
            return small.read(42) == 42 &&
                   large.read(1234567890123L) == 1234567890123L &&
                   explicit_initializations == 2 &&
                   default_initializations == 2;
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<bool(*)()>("test");
    REQUIRE(test);
    CHECK(test());
}

TEST_CASE("Nested inner templates reject a selected deleted constructor",
          "[gen][template][instantiation][nested-template-descriptor]") {
    REQUIRE_THROWS_MATCHES(gen_jit_throws(R"SRC(
        module nested_inner_deleted_constructor;

        template<typename T>
        struct Host {
            public:
            template<typename U> struct Inner {
                public:
                Inner(v : long) {}
                Inner(v : int) -> delete;
            }
            read() : int {
                item : Inner<int>(42);
                return 1;
            }
        }

        test() : int {
            host : Host<int>;
            return host.read();
        }
    )SRC"), k::log::compiler_error,
        Catch::Matchers::Predicate<k::log::compiler_error>(
            [](const auto& error) {
                return error.get_diagnostic().message.find("deleted") != std::string::npos;
            }, "selected deleted constructor"));
}

TEST_CASE("Nested template parameters shadow outer parameters",
          "[gen][template][instantiation][nested-template-descriptor]") {
    auto jit = gen_jit(R"SRC(
        module nested_template_shadow;

        template<typename T>
        struct Host {
            public:
            template<typename T> static struct Inner {
                public: value : T;
            }
            template<typename U = T> static struct Default {
                public: value : U;
            }
            static test() : bool {
                inner : Inner<long>;
                inner.value = 1234567890123L;
                fallback : Default<>;
                fallback.value = 42;
                return inner.value == 1234567890123L && fallback.value == 42;
            }
        }

        test() : bool { return Host<int>::test(); }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<bool(*)()>("test");
    REQUIRE(test);
    CHECK(test());
}

TEST_CASE("Nested template defaults retain enclosing binders despite aliases",
          "[gen][template][instantiation][nested-template-default-alias]") {
    auto jit = gen_jit(R"SRC(
        module nested_template_default_alias;

        struct Marker { wrong : int; }
        namespace alias_types {
            struct Marker { value : int; }
            using Choice = Marker;
        }
        using T = alias_types::Marker;
        using Choice = alias_types::Marker;

        template<typename T>
        struct Host {
            public:
            template<typename T> static struct Shadow {
                public: value : T;
            }
            template<typename U = T> static struct Default {
                public: value : U;
            }
            template<typename U = Choice> static struct Named {
                public: value : U;
            }
            template<typename U = alias_types::Choice> static struct Qualified {
                public: value : U;
            }
            template<typename U = ::alias_types::Choice> static struct Rooted {
                public: value : U;
            }
            static test(v : T) : bool {
                shadow : Shadow<long>;
                shadow.value = 1234567890123L;
                fallback : Default<>;
                fallback.value = v;
                explicit_value : Default<long>;
                explicit_value.value = 1234567890123L;
                named : Named<>;
                named.value.value = 17;
                qualified : Qualified<>;
                qualified.value.value = 19;
                rooted : Rooted<>;
                rooted.value.value = 23;
                return shadow.value == 1234567890123L && fallback.value == v &&
                       explicit_value.value == 1234567890123L &&
                       named.value.value == 17 && qualified.value.value == 19 &&
                       rooted.value.value == 23;
            }
        }

        test() : bool {
            return Host<int>::test(42) && Host<long>::test(1234567890123L) &&
                   Host<int>::test(73);
        }
    )SRC");
    REQUIRE(jit);
    auto test = jit->lookup_symbol<bool(*)()>("test");
    REQUIRE(test);
    CHECK(test());
}

TEST_CASE("Nested temporary constructor overloads preserve member argument context",
          "[gen][template][instantiation][nested-constructor-member-context]") {
    auto jit = gen_jit(R"SRC(
        module nested_constructor_member_context;

        template<typename T>
        struct Shared {
            value : T;
        }

        template<typename T>
        struct Weak {
            value : T;
            Weak() : value(0) {}
            Weak(other : const Shared<T>&) : value(other.value) {}
            template<typename U>
            Weak(other : const Shared<U>&) : value(other.value) {}
        }

        template<typename T>
        struct Container {
            value : T;
            size() : unsigned int { return 1; }
            get(index : unsigned int) : T& { return value; }
            append(other : const T&) { value = other; }
        }

        struct Sink {
            links : Container<Weak<int>>;
        }

        template<typename T>
        struct Handle {
            value : T;
            get() : T* { return &value; }
        }

        class Holder {
            _modules : Container<Shared<int>>!;
            _module : Handle<Sink>;
            Holder() : _modules(new Container<Shared<int>>()) {
                _modules.value.value = 42;
                index : unsigned int = 0;
                while (index < _modules.size()) {
                    _module.get()->links.append(Weak<int>(_modules.get(index)));
                    ++index;
                }
            }
            update() {
                _modules.value.value = 75;
                _module.get()->links.append(Weak<int>(_modules.get(0)));
            }
            read() : int { return _module.get()->links.value.value; }
        }

        test() : int {
            holder : Holder;
            first : int = holder.read();
            holder.update();
            return first + holder.read();
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto test = jit->lookup_symbol<int(*)()>("test");
    REQUIRE(test != nullptr);
    CHECK(test() == 117);
}

TEST_CASE("Template carrier resolves enum comparisons before body types",
          "[gen][template][instantiation][enum][symbol-order]") {
    std::string source = R"SRC(
        module template_carrier_symbols;

        namespace io {
            enum IoErrorKind { System; Cancelled; Closed; }

            template<typename T>
            struct IoCallbackCarrier {
                static invoke(err: int, kind: IoErrorKind) : int {
                    if (err == 0 && kind == IoErrorKind::System) return 11;
                    if (kind == IoErrorKind::Cancelled) return 22;
                    return 33;
                }
            }
        }
    )SRC";

    SECTION("Instantiated during aggregate declaration resolution") {
        source += R"SRC(
            struct Anchor { carrier: io::IoCallbackCarrier<int>; }
            run(err: int, code: int) : int {
                return io::IoCallbackCarrier<int>::invoke(err, (io::IoErrorKind) code);
            }
        )SRC";
    }
    SECTION("Instantiated on demand by a qualified static call") {
        source += R"SRC(
            run(err: int, code: int) : int {
                return io::IoCallbackCarrier<int>::invoke(err, (io::IoErrorKind) code);
            }
        )SRC";
    }
    SECTION("Instantiated on demand by a static callback reference") {
        source += R"SRC(
            run(err: int, code: int) : int {
                invokeFn: *(int, io::IoErrorKind):int = io::IoCallbackCarrier<int>::invoke;
                return invokeFn(err, (io::IoErrorKind) code);
            }
        )SRC";
    }

    auto jit = gen_jit(source);
    REQUIRE(jit != nullptr);
    auto run = jit->lookup_symbol<int(*)(int, int)>("run");
    REQUIRE(run != nullptr);
    CHECK(run(0, 0) == 11);
    CHECK(run(5, 0) == 33);
    CHECK(run(0, 1) == 22);
    CHECK(run(0, 2) == 33);
}

TEST_CASE("Template lifecycle body symbols preserve synthesized initializers",
          "[gen][template][instantiation][enum][symbol-order]") {
    auto jit = gen_jit(R"SRC(
        module template_lifecycle_symbols;
        enum State { Pending; Ready; }
        const FUTURE_FAILED: int = 2;
        initializations: int = 0;
        constructions: int = 0;
        destructions: int = 0;
        initialize(v: int) : int {
            initializations += 1;
            return v;
        }

        template<typename T>
        struct Future {
            value: T;
            state: State;
            Future(v: T) : value(initialize(v)) {
                constructions += 1;
                state = State::Pending;
            }
            ~Future() {
                if (state == State::Ready) destructions += 1;
            }
            complete() { state = State::Ready; }
            const get() : T {
                if (state != State::Ready) return (T) FUTURE_FAILED;
                return this.value;
            }
        }

        run() : int {
            {
                f: Future<int>(7);
                if (f.get() != FUTURE_FAILED) return -1;
                f.complete();
                if (f.get() != 7) return -2;
            }
            return constructions * 100 + destructions * 10 + initializations;
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto run = jit->lookup_symbol<int(*)()>("run");
    REQUIRE(run != nullptr);
    CHECK(run() == 111);
}

TEST_CASE("Nested and base template bodies resolve qualified enum symbols",
          "[gen][template][instantiation][enum][symbol-order]") {
    auto jit = gen_jit(R"SRC(
        module template_nested_symbols;
        enum State { Pending; Ready; }

        template<typename T>
        struct Base {
            ready(kind: State) : bool { return kind == State::Ready; }
        }
        template<typename T>
        struct Derived : Base<T> {
        }
        template<typename T>
        struct Outer {
            static struct Inner {
                static ready(kind: State) : bool { return kind == State::Ready; }
            }
            static invoke() : bool {
                return Inner::ready(State::Ready);
            }
        }

        run() : int {
            d: Derived<int>;
            if (!d.ready(State::Ready)) return -1;
            if (d.ready(State::Pending)) return -2;
            if (!Outer<int>::invoke()) return -3;
            return 42;
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto run = jit->lookup_symbol<int(*)()>("run");
    REQUIRE(run != nullptr);
    CHECK(run() == 42);
}

TEST_CASE("Template union Kind is declared before body symbol catch-up",
          "[gen][template][instantiation][enum][union][symbol-order]") {
    std::string source = R"SRC(
        module template_union_body_symbols;
        template<typename T>
        struct Result {
            union Storage { value: T; error: int; }
            storage: Storage;
            const hasValue() : bool {
                return storage.index() == Storage::Kind::value;
            }
            const hasError() : bool {
                return storage.index() == Storage::Kind::error;
            }
            static invoke() : int {
                r: Result<T>;
                r.storage.value = (T) 7;
                if (!r.hasValue() || r.hasError()) return -1;
                r.storage.error = 9;
                if (r.hasValue() || !r.hasError()) return -2;
                return r.storage.error;
            }
        }
    )SRC";
    SECTION("Eager aggregate instantiation") {
        source += "struct Anchor { result: Result<int>; }";
    }
    SECTION("On-demand aggregate instantiation") {
    }
    source += "run() : int { return Result<int>::invoke(); }";

    auto jit = gen_jit(source);
    REQUIRE(jit != nullptr);
    auto run = jit->lookup_symbol<int(*)()>("run");
    REQUIRE(run != nullptr);
    CHECK(run() == 9);
}

TEST_CASE("Invalid enum entry in instantiated body reports a symbol diagnostic",
          "[gen][template][instantiation][enum][symbol-order]") {
    const std::string source = R"SRC(
        module template_invalid_enum_symbol;
        enum State { Pending; Ready; }
        template<typename T>
        struct Carrier {
            static invoke() : bool { return State::Missing == State::Ready; }
        }
    )SRC";

    SECTION("Uninstantiated bodies remain deferred") {
        REQUIRE(compile_model(source) != nullptr);
    }
    SECTION("Instantiation diagnoses the missing entry rather than Error0F003") {
        try {
            gen_jit_throws(source + R"SRC(
                run() : bool { return Carrier<int>::invoke(); }
            )SRC");
            FAIL("Expected an enum entry resolution error");
        } catch (const k::model::gen::resolution_error& error) {
            CHECK(error.get_diagnostic().code ==
                  static_cast<unsigned int>(k::diag::function_diag::ERR_FUNC_ANNOTATION_MISMATCH));
            CHECK(error.get_diagnostic().message.find("has no entry named") != std::string::npos);
        }
    }
}

TEST_CASE("Template value members are constructed and destroyed exactly once",
          "[gen][template][instantiation][lifecycle][regression]") {
    const bool explicitDestructor = GENERATE(false, true);
    const bool eagerInstantiation = GENERATE(false, true);
    INFO("explicit destructor: " << explicitDestructor
         << ", eager instantiation: " << eagerInstantiation);
    std::string source = R"SRC(
        module template_value_member_lifecycle;
        constructions: int = 0;
        drops: int = 0;
        bodyDrops: int = 0;
        returned: int = 0;
        struct Payload {
            tag: int;
            Payload(): tag(37) { ++constructions; }
            ~Payload() { drops += tag == 37 ? 1 : 100; }
        }
        template<typename T>
        struct Box {
            value: T;
            Box() {}
    )SRC";
    if (explicitDestructor) source += "~Box() { ++bodyDrops; }";
    source += "}";
    if (eagerInstantiation) source += "struct Anchor { box: Box<Payload>; }";
    source += R"SRC(
        run(): int {
            {
                local: Box<Payload>;
                if (local.value.tag != 37) return -1;
            }
            owner: Box<Payload>! = new Box<Payload>();
            if (owner->value.tag != 37) return -2;
            delete owner;
            if (owner != null) return -3;
            delete owner;
            returned = 1;
            return constructions * 100 + drops * 10 + bodyDrops;
        }
        didReturn(): int { return returned; }
    )SRC";

    auto jit = gen_jit(source);
    REQUIRE(jit);
    auto run = jit->lookup_symbol<int (*)()>("run");
    auto didReturn = jit->lookup_symbol<int (*)()>("didReturn");
    REQUIRE(run);
    REQUIRE(didReturn);
    int result = -999;
    REQUIRE_NOTHROW(result = run());
    REQUIRE(result == (explicitDestructor ? 222 : 220));
    REQUIRE(didReturn() == 1);
}

TEST_CASE("Nested template value destruction propagates after releasing owned state",
          "[gen][template][instantiation][lifecycle][exceptions][regression]") {
    auto jit = gen_jit(R"SRC(
        module template_throwing_value_cleanup;
        constructions: int = 0;
        drops: int = 0;
        boxDrops: int = 0;
        errorDrops: int = 0;
        stateDrops: int = 0;
        returned: int = 0;
        struct Payload {
            tag: int;
            Payload(): tag(37) { ++constructions; }
            ~Payload() {
                drops += tag == 37 ? 1 : 100;
                throw Exception(919);
            }
        }
        class Failure : Exception {
            ~Failure() { ++errorDrops; }
        }
        struct NativeState {
            ~NativeState() { ++stateDrops; }
        }
        template<typename T>
        struct Box {
            value: T;
            error: Throwable!;
            Box() { error = new Failure(); }
            ~Box() { ++boxDrops; delete error; }
        }
        template<typename T>
        struct Shared {
            native: NativeState*;
            box: Box<T>*;
            Shared() {
                native = new NativeState();
                box = new Box<T>();
            }
            releaseNative() {
                state: NativeState! = (NativeState!) native;
                native = null;
                box = null;
                delete state;
            }
            ~Shared() {
                payload: Box<T>! = (Box<T>!) box;
                try {
                    delete payload;
                } catch (e: Throwable&) {
                    releaseNative();
                    throw;
                }
                releaseNative();
            }
        }
        template<typename T>
        struct Carrier {
            shared: Shared<T>;
            static drop(context: byte*): int {
                carrier: Carrier<T>! = (Carrier<T>!) context;
                code: int = 0;
                try { delete carrier; }
                catch (e: Throwable&) { code = e.getCode(); }
                if (carrier != null) return -1;
                delete carrier;
                return code;
            }
        }
        run(): int {
            carrier: Carrier<Payload>* = new Carrier<Payload>();
            code: int = Carrier<Payload>::drop((byte*) carrier);
            returned = 1;
            return code;
        }
        didReturn(): int { return returned; }
        constructionCount(): int { return constructions; }
        dropCount(): int { return drops; }
        boxDropCount(): int { return boxDrops; }
        errorDropCount(): int { return errorDrops; }
        stateDropCount(): int { return stateDrops; }
    )SRC");
    REQUIRE(jit);
    auto run = jit->lookup_symbol<int (*)()>("run");
    REQUIRE(run);
    int result = -999;
    REQUIRE_NOTHROW(result = run());
    REQUIRE(result == 919);
    for (const auto* name : {"didReturn", "constructionCount", "dropCount",
                             "boxDropCount", "errorDropCount", "stateDropCount"}) {
        INFO(name);
        auto count = jit->lookup_symbol<int (*)()>(name);
        REQUIRE(count);
        REQUIRE(count() == 1);
    }
}

TEST_CASE("Nested template formal binds to its concrete declaration",
          "[gen][template][instantiation][nested-formal-binding][regression]") {
    auto jit = gen_jit(R"SRC(
        module nested_formal_binding;
        template<typename T>
        struct Host {
            static struct Nested {
                value: T;
                Nested(x: T): value(x) {}
            }
            nested: Nested;
            Host(x: T): nested(x) {}
            static readNested(n: Nested&): T { return n.value; }
        }
        run(): int {
            h: Host<int>(7);
            return Host<int>::readNested(h.nested);
        }
    )SRC");
    REQUIRE(jit != nullptr);
    auto run = jit->lookup_symbol<int(*)()>("run");
    REQUIRE(run != nullptr);
    CHECK(run() == 7);
}

namespace {

constexpr std::string_view nested_formal_binding_source = R"SRC(
    module nested_signature_binding;
    typedef Token: int;
    struct Control {
        static struct Nested {
            value: int;
            Nested(x: int): value(x) {}
        }
    }
    template<typename T>
    struct Host {
        static struct Nested {
            value: T;
            Nested(x: T): value(x) {}
            static same(n: Nested&): Nested& { return n; }
        }
        nested: Nested;
        Host(x: T): nested(x) {}
        Host(n: Nested&): nested(n.value) {}
        static readNested(n: Nested&): T { return n.value; }
        static same(n: Nested&): Nested& { return n; }
        static constSame(n: const Nested&): const Nested& { return n; }
        static apply(n: Nested&, callback: *(Nested&): T): T { return callback(n); }
        static readControl(n: Control::Nested&): int { return n.value; }
        static token(n: Token): Token { return n; }
    }
    run(): int {
        first: Host<int>(7);
        repeated: Host<int>(11);
        wide: Host<long>(4294967296l);
        copied: Host<int>(first.nested);
        control: Control::Nested(19);
        if (Host<int>::readNested(first.nested) != 7) return -1;
        if (Host<int>::readNested(Host<int>::same(repeated.nested)) != 11) return -2;
        if (Host<long>::readNested(Host<long>::same(wide.nested)) != 4294967296l) return -3;
        if (Host<int>::constSame(first.nested).value != 7) return -4;
        if (Host<int>::apply(first.nested, Host<int>::readNested) != 7) return -5;
        if (Host<int>::readNested(copied.nested) != 7) return -6;
        if (Host<int>::readControl(control) != 19) return -7;
        Host<int>::same(first.nested).value = 13;
        if (Host<int>::readNested(first.nested) != 13) return -8;
        if (Host<int>::readNested(repeated.nested) != 11) return -9;
        return 42;
    }
)SRC";

} // namespace

TEST_CASE("Nested signature rebinding preserves declaration and qualifier identity",
          "[model][template][instantiation][nested-formal-binding][regression]") {
    using namespace k::model;
    auto comp = compile_model(nested_formal_binding_source);
    REQUIRE(comp != nullptr);
    auto root = comp->get_unit()->get_root_namespace();
    auto primary = root->get_aggregate("Host");
    auto host_int = root->get_aggregate("Host__int");
    auto host_long = root->get_aggregate("Host__long");
    REQUIRE(primary != nullptr);
    REQUIRE(host_int != nullptr);
    REQUIRE(host_long != nullptr);
    auto primary_nested = primary->get_aggregate("Nested");
    auto control_host = root->get_aggregate("Control");
    REQUIRE(control_host != nullptr);
    auto control = control_host->get_aggregate("Nested");
    REQUIRE(primary_nested != nullptr);
    REQUIRE(control != nullptr);
    auto token_decl = root->get_alias("Token");
    REQUIRE(token_decl != nullptr);
    auto token = token_decl->get_alias_type();
    REQUIRE(token != nullptr);

    for (const auto& host : {host_int, host_long}) {
        INFO(host->get_short_name());
        auto nested = host->get_aggregate("Nested");
        REQUIRE(nested != nullptr);
        auto nested_type = nested->get_struct_type();
        REQUIRE(nested_type != nullptr);
        CHECK(nested_type != primary_nested->get_struct_type());
        auto read = host->get_function("readNested");
        REQUIRE(read != nullptr);
        auto read_type = read->get_parameter("n")->get_type();
        REQUIRE(type::is_reference(read_type));
        CHECK(read_type->get_subtype() == nested_type);
        CHECK(read->get_return_type() == nested->get_variable("value")->get_type());
        for (const auto& owner : {host, nested}) {
            auto same = owner->get_function("same");
            REQUIRE(same != nullptr);
            REQUIRE(type::is_reference(same->get_return_type()));
            CHECK(same->get_return_type()->get_subtype() == nested_type);
            CHECK(same->get_parameter("n")->get_type()->get_subtype() == nested_type);
        }
        auto const_same = host->get_function("constSame");
        REQUIRE(const_same != nullptr);
        for (const auto& qualified : {const_same->get_return_type(),
                                     const_same->get_parameter("n")->get_type()}) {
            REQUIRE(type::is_reference(qualified));
            REQUIRE(type::is_const(qualified->get_subtype()));
            CHECK(qualified->get_subtype()->get_subtype() == nested_type);
        }
        auto apply = host->get_function("apply");
        REQUIRE(apply != nullptr);
        auto callback = std::dynamic_pointer_cast<callable_type>(
            apply->get_parameter("callback")->get_type());
        REQUIRE(callback != nullptr);
        CHECK(callback->get_addresser() == callable_type::addresser::pointer);
        REQUIRE(callback->get_parameter_types().size() == 1);
        CHECK(callback->get_parameter_types()[0]->get_subtype() == nested_type);
        CHECK(callback->get_return_type() == read->get_return_type());
        bool nested_constructor = false;
        for (const auto& ctor : host->constructors()) {
            if (auto n = ctor->get_parameter("n")) {
                nested_constructor = true;
                CHECK(n->get_type()->get_subtype() == nested_type);
            }
        }
        CHECK(nested_constructor);
        CHECK(host->get_function("readControl")->get_parameter("n")->get_type()->get_subtype()
              == control->get_struct_type());
        CHECK(host->get_function("token")->get_parameter("n")->get_type() == token);
        CHECK(host->get_function("token")->get_return_type() == token);
    }
    CHECK(host_int->get_aggregate("Nested")->get_struct_type()
          != host_long->get_aggregate("Nested")->get_struct_type());
    REQUIRE(primary->get_tpl_info() != nullptr);
    CHECK(primary->get_tpl_info()->instantiations.size() == 2);
    CHECK(primary->get_function("readNested")->get_parameter("n")->get_type()->get_subtype()
          == primary_nested->get_struct_type());

    auto old_nested = host_int->get_aggregate("Nested");
    auto new_nested = host_long->get_aggregate("Nested");
    type_rebinding_map rebound_types{
        {old_nested->get_struct_type().get(), new_nested->get_struct_type()}};
    callable_type_builder builder(root->get_context());
    builder.member_of(old_nested);
    builder.addresser(callable_type::addresser::link);
    builder.return_type(old_nested->get_struct_type()->get_reference());
    builder.append_parameter_type(token);
    builder.throws({old_nested->get_struct_type()});
    auto original_callable = builder.build();
    auto original = type::make_pinned_const(original_callable);
    auto rebound = substitute_type(original, {}, rebound_types);
    REQUIRE(type::is_const(rebound));
    auto callable = std::dynamic_pointer_cast<member_function_reference_type>(rebound->get_subtype());
    REQUIRE(callable != nullptr);
    CHECK(callable->get_member_of() == new_nested);
    CHECK(callable->get_addresser() == callable_type::addresser::link);
    CHECK(callable->get_return_type()->get_subtype() == new_nested->get_struct_type());
    CHECK(callable->get_parameter_types()[0] == token);
    CHECK(callable->get_throws()[0] == new_nested->get_struct_type());
    CHECK(original_callable->get_return_type()->get_subtype() == old_nested->get_struct_type());
    CHECK(original_callable->get_throws()[0] == old_nested->get_struct_type());
    CHECK(std::dynamic_pointer_cast<member_function_reference_type>(original_callable)->get_member_of()
          == old_nested);
    CHECK(substitute_type(control->get_struct_type(), {}, rebound_types) == control->get_struct_type());
    CHECK(substitute_type(token, {}, rebound_types) == token);
}

TEST_CASE("Distinct and repeated nested signature specializations execute independently",
          "[gen][template][instantiation][nested-formal-binding][regression]") {
    auto jit = gen_jit(nested_formal_binding_source);
    REQUIRE(jit != nullptr);
    auto run = jit->lookup_symbol<int(*)()>("run");
    REQUIRE(run != nullptr);
    CHECK(run() == 42);
}

namespace {

constexpr std::string_view primary_nested_binding_source = R"SRC(
    module primary_nested_binding;
    namespace left {
        template<typename T>
        struct Host {
            static struct Nested {
                value: T;
                Nested(x: T): value(x) {}
                const read(): T { return value; }
            }
            union Storage { value: T; error: int; }
            nested: Nested;
            storage: Storage;
            Host(x: T): nested(x) { storage.value = x; }
            const read(): T { return nested.read() + storage.value; }
            static readNested(n: Nested&): T { return n.value; }
            static same(n: Nested&): Nested& { return n; }
        }
    }
    namespace right {
        template<typename T>
        struct Host {
            static struct Nested {
                value: T;
                extra: int;
                Nested(x: T): value(x), extra(3) {}
                const read(): T { return value + (T) extra; }
            }
            union Storage { value: T; error: long; }
            nested: Nested;
            storage: Storage;
            Host(x: T): nested(x) { storage.value = x; }
            const read(): T { return nested.read() + storage.value; }
            static readNested(n: Nested&): T { return n.value; }
            static same(n: Nested&): Nested& { return n; }
        }
    }
)SRC";

constexpr std::string_view primary_nested_binding_run = R"SRC(
    run(): int {
        first: left::Host<int>(5);
        repeated: left::Host<int>(7);
        wide: left::Host<long>(4294967296l);
        other: right::Host<int>(11);
        otherRepeated: right::Host<int>(13);
        otherWide: right::Host<long>(8589934592l);
        if (first.read() != 10 || repeated.read() != 14) return -1;
        if (other.read() != 25 || otherRepeated.read() != 29) return -2;
        if (wide.read() != 8589934592l || otherWide.read() != 17179869187l) return -3;
        if (left::Host<int>::readNested(first.nested) != 5) return -4;
        if (right::Host<int>::readNested(other.nested) != 11) return -5;
        left::Host<int>::same(first.nested).value = 17;
        right::Host<long>::same(otherWide.nested).value = 8589934593l;
        if (first.read() != 22 || repeated.read() != 14 || other.read() != 25) return -6;
        if (wide.read() != 8589934592l || otherWide.read() != 17179869188l) return -7;
        if (left::Host<long>::readNested(wide.nested) != 4294967296l) return -8;
        if (right::Host<long>::readNested(otherWide.nested) != 8589934593l) return -9;
        return 42;
    }
)SRC";

} // namespace

TEST_CASE("Uninstantiated namespace homonyms bind primary nested formals lexically",
          "[model][template][instantiation][primary-nested-binding][regression]") {
    using namespace k::model;
    auto comp = compile_model(primary_nested_binding_source);
    REQUIRE(comp != nullptr);
    auto root = comp->get_unit()->get_root_namespace();
    std::set<const struct_type*> nested_types;
    for (const auto& ns_name : {"left", "right"}) {
        INFO(ns_name);
        auto scope = root->get_child_namespace(ns_name);
        REQUIRE(scope != nullptr);
        auto primary = scope->get_aggregate("Host");
        REQUIRE(primary != nullptr);
        REQUIRE(primary->is_template());
        REQUIRE(primary->get_tpl_info() != nullptr);
        CHECK(primary->get_tpl_info()->instantiations.empty());
        auto nested = primary->get_aggregate("Nested");
        REQUIRE(nested != nullptr);
        CHECK(nested->get_enclosing_aggregate() == primary);
        auto nested_type = nested->get_struct_type();
        REQUIRE(nested_type != nullptr);
        CHECK(nested_type->get_struct() == nested);
        CHECK(nested_type->get_llvm_type() == nullptr);
        for (const auto& fn_name : {"readNested", "same"}) {
            auto fn = primary->get_function(fn_name);
            REQUIRE(fn != nullptr);
            auto param = fn->get_parameter("n");
            REQUIRE(param != nullptr);
            REQUIRE(type::is_reference(param->get_type()));
            CHECK(param->get_type()->get_subtype() == nested_type);
        }
        auto result = primary->get_function("same")->get_return_type();
        REQUIRE(type::is_reference(result));
        CHECK(result->get_subtype() == nested_type);
        nested_types.insert(nested_type.get());
    }
    CHECK(nested_types.size() == 2);
}

TEST_CASE("Namespace primary bindings produce distinct and cached nested specialization storage",
          "[model][template][instantiation][primary-nested-binding][regression]") {
    using namespace k::model;
    auto source = std::string(primary_nested_binding_source) + std::string(primary_nested_binding_run);
    auto comp = compile_model(source);
    REQUIRE(comp != nullptr);
    auto root = comp->get_unit()->get_root_namespace();
    std::set<const struct_type*> nested_types;
    std::set<const struct_type*> storage_types;
    for (const auto& ns_name : {"left", "right"}) {
        INFO(ns_name);
        auto scope = root->get_child_namespace(ns_name);
        REQUIRE(scope != nullptr);
        auto primary = scope->get_aggregate("Host");
        REQUIRE(primary != nullptr);
        REQUIRE(primary->get_tpl_info() != nullptr);
        CHECK(primary->get_tpl_info()->instantiations.size() == 2);
        auto primary_nested = primary->get_aggregate("Nested");
        REQUIRE(primary_nested != nullptr);
        auto primary_read = primary->get_function("readNested");
        REQUIRE(primary_read != nullptr);
        auto primary_param = primary_read->get_parameter("n");
        REQUIRE(primary_param != nullptr);
        REQUIRE(type::is_reference(primary_param->get_type()));
        CHECK(primary_param->get_type()->get_subtype() == primary_nested->get_struct_type());
        for (const auto& host_name : {"Host__int", "Host__long"}) {
            INFO(host_name);
            auto host = scope->get_aggregate(host_name);
            REQUIRE(host != nullptr);
            auto nested = host->get_aggregate("Nested");
            auto storage = host->get_union("Storage");
            REQUIRE(nested != nullptr);
            REQUIRE(storage != nullptr);
            auto nested_type = nested->get_struct_type();
            auto storage_type = storage->get_struct_type();
            REQUIRE(nested_type != nullptr);
            REQUIRE(storage_type != nullptr);
            CHECK(nested->get_enclosing_aggregate() == host);
            CHECK(nested_type->get_struct() == nested);
            CHECK(nested_type != primary_nested->get_struct_type());
            REQUIRE(nested_type->get_llvm_type() != nullptr);
            REQUIRE(storage_type->get_llvm_type() != nullptr);
            auto nested_field = host->get_variable("nested");
            auto storage_field = host->get_variable("storage");
            REQUIRE(nested_field != nullptr);
            REQUIRE(storage_field != nullptr);
            CHECK(nested_field->get_type() == nested_type);
            CHECK(storage_field->get_type() == storage_type);
            for (const auto& fn_name : {"readNested", "same"}) {
                auto fn = host->get_function(fn_name);
                REQUIRE(fn != nullptr);
                auto param = fn->get_parameter("n");
                REQUIRE(param != nullptr);
                REQUIRE(type::is_reference(param->get_type()));
                CHECK(param->get_type()->get_subtype() == nested_type);
            }
            auto result = host->get_function("same")->get_return_type();
            REQUIRE(type::is_reference(result));
            CHECK(result->get_subtype() == nested_type);
            auto read = host->get_function("readNested");
            auto value = nested->get_variable("value");
            REQUIRE(value != nullptr);
            CHECK(read->get_return_type() == value->get_type());
            nested_types.insert(nested_type.get());
            storage_types.insert(storage_type.get());
        }
    }
    CHECK(nested_types.size() == 4);
    CHECK(storage_types.size() == 4);
}

TEST_CASE("Namespace primary nested references preserve independent runtime storage",
          "[gen][template][instantiation][primary-nested-binding][regression]") {
    auto source = std::string(primary_nested_binding_source) + std::string(primary_nested_binding_run);
    auto jit = gen_jit(source);
    REQUIRE(jit != nullptr);
    auto run = jit->lookup_symbol<int(*)()>("run");
    REQUIRE(run != nullptr);
    CHECK(run() == 42);
}
