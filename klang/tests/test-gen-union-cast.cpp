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
#include <cstring>

#include "helpers.hpp"

extern "C" void __k_test_clear_duplicate_union_owner(void* slot, void* extracted) {
    void* stored = nullptr;
    std::memcpy(&stored, slot, sizeof(stored));
    if (stored == extracted) {
        // Avoid double deletion when the regression duplicates the owner.
        void* empty = nullptr;
        std::memcpy(slot, &empty, sizeof(empty));
    }
}

TEST_CASE("Union owner value cast transfers the active owner slot",
          "[gen][union][cast][owner][regression]") {
    const bool optimize = GENERATE(false, true);
    auto jit = gen_jit(R"K(
        module union_owner_value_cast;
        drops: int = 0;
        class Resource {
        public:
            Resource() {}
            ~Resource() { ++drops; }
        }
        union Storage { empty: byte; value: Resource!; }
        @ffi::Extern("C")
        __k_test_clear_duplicate_union_owner(slot: Resource!*, value: Resource*);

        test(): int {
            drops = 0;
            result: int = 0;
            {
                source: Storage(new Resource());
                {
                    value: Resource! = (Resource!) source;
                    if (value == null) result = -2;
                    else if (source.value != null) {
                        __k_test_clear_duplicate_union_owner(&source.value, (Resource*) value);
                        result = -1;
                    }
                    if (drops != 0 && result == 0) result = -3;
                    if (source.index() != 1 && result == 0) result = -6;
                }
                if (drops != 1 && result == 0) result = -4;
            }
            if (drops != 1 && result == 0) result = -5;
            return result == 0 ? 42 : result;
        }
    )K", false, optimize);
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("Union owner argument adaptation transfers the active owner slot",
          "[gen][union][cast][owner][regression]") {
    const bool optimize = GENERATE(false, true);
    auto jit = gen_jit(R"K(
        module union_owner_argument;
        drops: int = 0;
        class Resource {
        public:
            Resource() {}
            ~Resource() { ++drops; }
        }
        union Storage { empty: byte; value: Resource!; }
        @ffi::Extern("C")
        __k_test_clear_duplicate_union_owner(slot: Resource!*, value: Resource*);

        consume(value: Resource!, source: Storage&): int {
            if (value == null) return -2;
            if (source.value != null) {
                __k_test_clear_duplicate_union_owner(&source.value, (Resource*) value);
                return -1;
            }
            if (drops != 0) return -3;
            if (source.index() != 1) return -6;
            return 0;
        }
        test(): int {
            drops = 0;
            result: int = 0;
            {
                source: Storage(new Resource());
                result = consume(source, source);
                if (drops != 1 && result == 0) result = -4;
            }
            if (drops != 1 && result == 0) result = -5;
            return result == 0 ? 42 : result;
        }
    )K", false, optimize);
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("Union owner extraction checks once and preserves borrowed and null slots",
          "[gen][union][cast][owner][regression]") {
    const bool optimize = GENERATE(false, true);
    auto jit = gen_jit(R"K(
        module union_owner_extract_edges;
        drops: int = 0;
        evaluations: int = 0;
        class Resource {
        public:
            Resource() {}
            ~Resource() { ++drops; }
        }
        union Storage { empty: byte; value: Resource!; }
        @ffi::Extern("C")
        __k_test_clear_duplicate_union_owner(slot: Resource!*, value: Resource*);
        select(source: Storage&): Storage& { ++evaluations; return source; }
        inspect(value: Resource!&): bool { return value != null; }
        consume(value: Resource!, source: Storage&): int {
            if (value == null) return -2;
            if (source.value != null) {
                __k_test_clear_duplicate_union_owner(&source.value, (Resource*) value);
                return -1;
            }
            return evaluations == 1 && source.index() == 1 ? 0 : -3;
        }
        empty(value: Resource!, source: Storage&): bool {
            return value == null && source.value == null && source.index() == 1;
        }
        mismatch(): int {
            source: Storage;
            source.empty = 17;
            if (value: Resource! = (Resource!) source) {
                return -1;
            }
            return source.index() == 0 && source.empty == 17 ? 42 : -2;
        }
        test(): int {
            drops = 0;
            evaluations = 0;
            result: int = 0;
            {
                source: Storage(new Resource());
                if (!inspect((Resource!&) source) || source.value == null) return -4;
                {
                    value: Resource! = (Resource!) select(source);
                    if (value == null) result = -5;
                    else if (source.value != null) {
                        __k_test_clear_duplicate_union_owner(&source.value, (Resource*) value);
                        result = -1;
                    }
                    if (evaluations != 1 && result == 0) result = -6;
                }
            }
            if (result != 0 || drops != 1) return -7;
            evaluations = 0;
            {
                source: Storage(new Resource());
                result = consume(select(source), source);
            }
            if (result != 0 || drops != 2) return -8;
            {
                noValue: Resource! = null;
                source: Storage(noValue);
                value: Resource! = (Resource!) source;
                if (value != null || source.value != null || source.index() != 1) return -9;
                if (!empty(source, source)) return -10;
            }
            return drops == 2 ? 42 : -11;
        }
    )K", false, optimize);
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    auto mismatch = jit->lookup_symbol<int (*)()>("mismatch");
    REQUIRE(fn);
    REQUIRE(mismatch);
    CHECK(fn() == 42);
    CHECK(mismatch() == 42);
}

TEST_CASE("Union owner if-let cleans up on early return",
          "[gen][union][cast][owner][regression]") {
    const bool optimize = GENERATE(false, true);
    auto jit = gen_jit(R"K(
        module union_owner_if_let_return;
        drops: int = 0;
        class Resource {
        public:
            Resource() {}
            ~Resource() { ++drops; }
        }
        union Storage { empty: byte; value: Resource!; }
        @ffi::Extern("C")
        __k_test_clear_duplicate_union_owner(slot: Resource!*, value: Resource*);
        take(): int {
            source: Storage(new Resource());
            if (value: Resource! = (Resource!) source) {
                if (source.value != null) {
                    __k_test_clear_duplicate_union_owner(&source.value, (Resource*) value);
                    return -1;
                }
                return 42;
            }
            return -2;
        }
        test(): int {
            drops = 0;
            result: int = take();
            return result == 42 && drops == 1 ? 42 : -3;
        }
    )K", false, optimize);
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("Union owner extraction transfers temporary and returned union payloads",
          "[gen][union][cast][owner][temporary][regression]") {
    const bool optimize = GENERATE(false, true);
    auto jit = gen_jit(R"K(
        module union_owner_extract_temporary;
        drops: int = 0;
        evaluations: int = 0;
        class Resource {
        public:
            Resource() {}
            ~Resource() {
                if (destination != null)
                    __k_test_clear_duplicate_union_owner(&destination, (Resource*) destination);
                ++drops;
            }
        }
        union Storage { empty: byte; value: Resource!; }
        destination: Resource!;
        @ffi::Extern("C")
        __k_test_clear_duplicate_union_owner(slot: Resource!*, value: Resource*);
        make(): Storage {
            ++evaluations;
            return Storage(new Resource());
        }
        consume(value: Resource!) { destination = value; }
        test(): int {
            drops = 0;
            evaluations = 0;
            destination = (Resource!) Storage(new Resource());
            if (destination == null || drops != 0) return -1;
            delete destination;
            if (drops != 1) return -2;
            consume(Storage(new Resource()));
            if (destination == null || drops != 1) return -3;
            delete destination;
            if (drops != 2) return -4;
            destination = (Resource!) make();
            if (destination == null || drops != 2 || evaluations != 1) return -5;
            delete destination;
            if (drops != 3) return -6;
            consume(make());
            if (destination == null || drops != 3 || evaluations != 2) return -7;
            delete destination;
            return drops == 4 ? 42 : -8;
        }
    )K", false, optimize);
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("Union owner extraction respects const bindings",
          "[gen][union][cast][owner][const][regression]") {
    const std::string declarations = R"K(
        module union_owner_extract_const;
        class Resource {}
        union Storage { empty: byte; value: Resource!; }
        union Frozen { empty: byte; const value: Resource!; }
        union ConstPointee { empty: byte; value: const Resource!; }
        consume(value: Resource!) {}
        consumeConst(value: const Resource!) {}
    )K";
    auto immutable = Catch::Matchers::Predicate<k::log::compiler_error>(
        [](const auto& error) {
            return error.get_diagnostic().message.find(
                "Cannot move owner alternative") != std::string::npos;
        }, "rejects moving an owner out of a const union or alternative");
    auto compile = [&](const std::string& body) {
        return gen_jit_throws(declarations + body);
    };
    REQUIRE_THROWS_MATCHES(compile(R"K(
        test(source: const Storage&) { value: Resource! = (Resource!) source; }
    )K"), k::log::compiler_error, immutable);
    REQUIRE_THROWS_MATCHES(compile(R"K(
        test(source: const Storage&) { consume(source); }
    )K"), k::log::compiler_error, immutable);
    REQUIRE_THROWS_MATCHES(compile(R"K(
        test(source: Frozen&) { value: Resource! = (Resource!) source; }
    )K"), k::log::compiler_error, immutable);
    REQUIRE_THROWS_MATCHES(compile(R"K(
        test(source: Frozen&) { consume(source); }
    )K"), k::log::compiler_error, immutable);
    REQUIRE_THROWS_MATCHES(compile(R"K(
        test(source: const Storage*) { value: Resource! = (Resource!) source; }
    )K"), k::log::compiler_error, immutable);
    auto jit = compile(R"K(
        test(source: ConstPointee&) { consumeConst(source); }
    )K");
    REQUIRE(jit);
}

TEST_CASE("Union value casts copy their alternative while references borrow",
          "[gen][union][cast][copy][regression]") {
    auto jit = gen_jit(R"K(
        module union_cast_copy;
        copies: int = 0;
        class Value {
        public:
            value: int = 42;
            Value() {}
            Value(other: const Value&) -> delete;
            Value(other: Value&) : value(other.value) { ++copies; }
        }
        union Storage { empty: byte; value: Value; }
        test(): int {
            source: Value;
            storage: Storage(source);
            ref: Value& = (Value&) storage;
            copy: Value = (Value) storage;
            return ref.value + copy.value + copies - 44;
        }
    )K");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("Implicit union alternative initialization copies a copyable class",
          "[gen][union][cast][copy][regression]") {
    auto jit = gen_jit(R"K(
        module union_implicit_copy;
        copies: int = 0;
        class Value {
        public:
            value: int = 42;
            Value() {}
            Value(other: const Value&) : value(other.value) { ++copies; }
        }
        union Storage { empty: byte; value: Value; }
        copyFrom(storage: const Storage&): int {
            copy: Value = storage;
            return copy.value;
        }
        test(): int {
            source: Value;
            storage: Storage(source);
            first: Value = storage;
            second: int = copyFrom(storage);
            if (copies != 3) return -1;
            return first.value + second - 42;
        }
    )K");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
}

TEST_CASE("Union alternative copies retain constructor friendship",
          "[gen][union][cast][copy][friend][regression]") {
    auto jit = gen_jit(R"K(
        module union_copy_friend;
        class Value {
            friend allowed;
        private:
            Value(other: const Value&) {}
        public:
            Value() {}
        }
        union Storage { empty: byte; value: Value; }
        allowed(value: const Value&): int {
            storage: Storage(value);
            copy: Value = (Value) storage;
            return 42;
        }
        test(): int { value: Value; return allowed(value); }
    )K");
    REQUIRE(jit);
    auto fn = jit->lookup_symbol<int (*)()>("test");
    REQUIRE(fn);
    CHECK(fn() == 42);
    REQUIRE_THROWS(gen_jit_throws(R"K(
        module union_copy_private;
        class Value {
        private:
            Value(other: const Value&) {}
        public:
            Value() {}
        }
        union Storage { empty: byte; value: Value; }
        denied(value: const Value&): int { storage: Storage(value); return 0; }
    )K"));
}

TEST_CASE("Union cast: explicit cast by value and reference", "[gen][union][cast]") {
    auto jit = gen_jit(R"SRC(
        module gen_union_cast_01;
        union NumVal {
            i: int;
            d: double;
        }

        test_cast_val() : int {
            u : NumVal;
            u.i = 42;
            x : int = (int) u;
            return x;
        }

        test_cast_ref() : int {
            u : NumVal;
            u.i = 50;
            (int&) u = 100;
            return u.i;
        }

        test_cast_const_ref() : int {
            u : NumVal;
            u.i = 77;
            cr : const int& = (const int&) u;
            return cr;
        }
    )SRC");
    REQUIRE(jit);
    auto f_val = jit->lookup_symbol<int(*)()>("test_cast_val");
    auto f_ref = jit->lookup_symbol<int(*)()>("test_cast_ref");
    auto f_cr  = jit->lookup_symbol<int(*)()>("test_cast_const_ref");
    REQUIRE(f_val); REQUIRE(f_ref); REQUIRE(f_cr);
    REQUIRE(f_val() == 42);
    REQUIRE(f_ref() == 100);
    REQUIRE(f_cr() == 77);
}

TEST_CASE("Union cast: implicit adaptation in var init, assign, and param passing", "[gen][union][cast]") {
    auto jit = gen_jit(R"SRC(
        module gen_union_cast_02;
        union Value {
            i: int;
            d: double;
        }

        take_int(x: int) : int {
            return x * 2;
        }

        take_ref(r: int&) : int {
            r = r + 5;
            return r;
        }

        test_init() : int {
            u : Value;
            u.i = 10;
            x : int = u;
            return x;
        }

        test_assign() : int {
            u : Value;
            u.i = 20;
            x : int = 0;
            x = u;
            return x;
        }

        test_param_val() : int {
            u : Value;
            u.i = 15;
            return take_int(u);
        }

        test_param_ref() : int {
            u : Value;
            u.i = 30;
            res : int = take_ref(u);
            return u.i;
        }
    )SRC");
    REQUIRE(jit);
    auto f_init = jit->lookup_symbol<int(*)()>("test_init");
    auto f_asgn = jit->lookup_symbol<int(*)()>("test_assign");
    auto f_pval = jit->lookup_symbol<int(*)()>("test_param_val");
    auto f_pref = jit->lookup_symbol<int(*)()>("test_param_ref");
    REQUIRE(f_init); REQUIRE(f_asgn); REQUIRE(f_pval); REQUIRE(f_pref);
    REQUIRE(f_init() == 10);
    REQUIRE(f_asgn() == 20);
    REQUIRE(f_pval() == 30);
    REQUIRE(f_pref() == 35);
}

TEST_CASE("Union cast: mismatch traps in normal execution", "[gen][union][cast]") {
    auto result = build_and_exec(R"SRC(
        module gen_union_cast_03;
        union Value {
            i: int;
            d: double;
        }

        main() : int {
            u : Value;
            u.d = 3.14;
            x : int = (int) u; // should trap on mismatch
            return x;
        }
    )SRC");
    REQUIRE(result.exit_code != 0);
}

TEST_CASE("Union cast: soft-fail in if conditions", "[gen][union][cast][if]") {
    auto jit = gen_jit(R"SRC(
        module gen_union_cast_04;
        union Value {
            i: int;
            d: double;
        }

        test_if_hit() : int {
            u : Value;
            u.i = 99;
            if (x : int = u) {
                return x;
            } else {
                return -1;
            }
        }

        test_if_miss() : int {
            u : Value;
            u.d = 2.71;
            if (x : int = u) {
                return x;
            } else {
                return 42;
            }
        }

        test_if_ref_miss() : int {
            u : Value;
            u.d = 2.71;
            if (r : int& = u) {
                return r;
            } else {
                return 88;
            }
        }
    )SRC");
    REQUIRE(jit);
    auto f_hit  = jit->lookup_symbol<int(*)()>("test_if_hit");
    auto f_miss = jit->lookup_symbol<int(*)()>("test_if_miss");
    auto f_rmis = jit->lookup_symbol<int(*)()>("test_if_ref_miss");
    REQUIRE(f_hit); REQUIRE(f_miss); REQUIRE(f_rmis);
    REQUIRE(f_hit() == 99);
    REQUIRE(f_miss() == 42);
    REQUIRE(f_rmis() == 88);
}

TEST_CASE("Union cast: &u addresser conversions and null policy", "[gen][union][cast][addresser]") {
    auto jit = gen_jit(R"SRC(
        module gen_union_cast_05;
        union Value {
            i: int;
            d: double;
        }

        test_ptr_hit() : int {
            u : Value;
            u.i = 123;
            p : int* = &u;
            if (p != null) {
                return *p;
            }
            return -1;
        }

        test_ptr_miss() : int {
            u : Value;
            u.d = 3.14;
            p : int* = &u;
            if (p == null) {
                return 1;
            }
            return 0;
        }

        test_view_hit() : int {
            u : Value;
            u.i = 456;
            v : int? = &u;
            if (v != null) {
                return *v;
            }
            return -1;
        }

        test_view_miss() : int {
            u : Value;
            u.d = 1.23;
            v : int? = &u;
            if (v == null) {
                return 1;
            }
            return 0;
        }

        test_if_ptr_cond() : int {
            u : Value;
            u.d = 9.99;
            if (p : int* = &u) {
                return *p;
            } else {
                return 777;
            }
        }

        test_link_hit() : int {
            u : Value;
            u.i = 321;
            l : int+ = &u;
            return *l;
        }
    )SRC");
    REQUIRE(jit);
    auto f_phit = jit->lookup_symbol<int(*)()>("test_ptr_hit");
    auto f_pmis = jit->lookup_symbol<int(*)()>("test_ptr_miss");
    auto f_vhit = jit->lookup_symbol<int(*)()>("test_view_hit");
    auto f_vmis = jit->lookup_symbol<int(*)()>("test_view_miss");
    auto f_ifp  = jit->lookup_symbol<int(*)()>("test_if_ptr_cond");
    auto f_lhit = jit->lookup_symbol<int(*)()>("test_link_hit");
    REQUIRE(f_phit); REQUIRE(f_pmis); REQUIRE(f_vhit); REQUIRE(f_vmis); REQUIRE(f_ifp); REQUIRE(f_lhit);
    REQUIRE(f_phit() == 123);
    REQUIRE(f_pmis() == 1);
    REQUIRE(f_vhit() == 456);
    REQUIRE(f_vmis() == 1);
    REQUIRE(f_ifp() == 777);
    REQUIRE(f_lhit() == 321);
}

TEST_CASE("Union cast: polymorphic union to base and ancestor references / addressers", "[gen][union][cast][polymorphic]") {
    auto jit = gen_jit(R"SRC(
        module gen_union_cast_06;
        abstract class Animal {
        public:
            abstract sound() : int;
        }
        class Dog : Animal {
        public:
            override sound() : int { return 10; }
        }
        class Cat : Animal {
        public:
            override sound() : int { return 20; }
        }
        union AnyAnimal : Animal {
            dog: Dog;
            cat: Cat;
        }

        call_animal_ref(a: Animal&) : int {
            return a.sound();
        }

        test_poly_base_ref_dog() : int {
            u : AnyAnimal;
            u.dog = Dog();
            a : Animal& = u;
            return a.sound();
        }

        test_poly_base_ref_cat() : int {
            u : AnyAnimal;
            u.cat = Cat();
            a : Animal& = u;
            return a.sound();
        }

        test_poly_param() : int {
            u : AnyAnimal;
            u.cat = Cat();
            return call_animal_ref(u);
        }

        test_poly_ptr() : int {
            u : AnyAnimal;
            u.dog = Dog();
            p : Animal* = &u;
            if (p != null) {
                return p->sound();
            }
            return -1;
        }

        test_specific_alt_hit() : int {
            u : AnyAnimal;
            u.dog = Dog();
            d : Dog& = u;
            return d.sound();
        }
    )SRC");
    REQUIRE(jit);
    auto f_dog = jit->lookup_symbol<int(*)()>("test_poly_base_ref_dog");
    auto f_cat = jit->lookup_symbol<int(*)()>("test_poly_base_ref_cat");
    auto f_par = jit->lookup_symbol<int(*)()>("test_poly_param");
    auto f_ptr = jit->lookup_symbol<int(*)()>("test_poly_ptr");
    auto f_spe = jit->lookup_symbol<int(*)()>("test_specific_alt_hit");
    REQUIRE(f_dog); REQUIRE(f_cat); REQUIRE(f_par); REQUIRE(f_ptr); REQUIRE(f_spe);
    REQUIRE(f_dog() == 10);
    REQUIRE(f_cat() == 20);
    REQUIRE(f_par() == 20);
    REQUIRE(f_ptr() == 10);
    REQUIRE(f_spe() == 10);
}

TEST_CASE("Union cast: compile errors for invalid casts", "[gen][union][cast][error]") {
    SECTION("Cast to non-existent alternative type") {
        REQUIRE(compile_should_fail(R"SRC(
            module gen_union_cast_err_01;
            union Value {
                i: int;
                d: double;
            }
            bad(u: Value&) : void {
                s : byte = (byte) u;
            }
        )SRC", nullptr));
    }

    SECTION("Cast when multiple alternatives share the same type (ambiguous)") {
        REQUIRE(compile_should_fail(R"SRC(
            module gen_union_cast_err_02;
            union Ambiguous {
                a: int;
                b: int;
            }
            bad(u: Ambiguous&) : void {
                x : int = (int) u;
            }
        )SRC", nullptr));
    }
}
